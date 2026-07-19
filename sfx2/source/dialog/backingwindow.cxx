/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include "backingwindow.hxx"
#include <utility>
#include <vcl/event.hxx>
#include <vcl/help.hxx>
#include <vcl/ptrstyle.hxx>
#include <vcl/settings.hxx>
#include <vcl/svapp.hxx>
#include <vcl/syswin.hxx>
#include <vcl/weld/Builder.hxx>
#include <vcl/weld/DialogController.hxx>
#include <vcl/weld/Entry.hxx>
#include <vcl/weld/Menu.hxx>
#include <vcl/weld/MessageDialog.hxx>
#include <vcl/weld/weld.hxx>

#include <unotools/historyoptions.hxx>
#include <unotools/moduleoptions.hxx>
#include <unotools/cmdoptions.hxx>
#include <unotools/configmgr.hxx>
#include <svtools/openfiledroptargetlistener.hxx>
#include <svtools/colorcfg.hxx>
#include <svtools/langhelp.hxx>
#include <templateviewitem.hxx>

#include <comphelper/processfactory.hxx>
#include <comphelper/propertysequence.hxx>
#include <comphelper/propertyvalue.hxx>
#include <sfx2/app.hxx>
#include <sfx2/doctempl.hxx>
#include <officecfg/Office/Common.hxx>

#include <i18nlangtag/languagetag.hxx>
#include <comphelper/diagnose_ex.hxx>

#include <com/sun/star/configuration/theDefaultProvider.hpp>
#include <com/sun/star/container/XNameAccess.hpp>
#include <com/sun/star/datatransfer/dnd/XDropTarget.hpp>
#include <com/sun/star/document/MacroExecMode.hpp>
#include <com/sun/star/document/UpdateDocMode.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/system/SystemShellExecute.hpp>
#include <com/sun/star/system/SystemShellExecuteFlags.hpp>
#include <com/sun/star/util/URLTransformer.hpp>
#include <com/sun/star/task/InteractionHandler.hpp>

#include <sfx2/viewfrm.hxx>
#include <vcl/commandinfoprovider.hxx>
#include <vcl/timer.hxx>
#include <sfx2/styfitem.hxx>
#include <sfx2/objsh.hxx>
#include <sfx2/docfac.hxx>
#include <sfx2/tplpitem.hxx>
#include <sfx2/sidebar/Sidebar.hxx>
#include <sfx2/sidebar/SidebarController.hxx>
#include <sfx2/childwin.hxx>

#include <svl/itemset.hxx>
#include <sfx2/dispatch.hxx>
#include <sfx2/sfxsids.hrc>
#include <sfx2/strings.hrc>
#include <sfx2/sfxresid.hxx>

#include <osl/file.hxx>
#include <osl/process.h>
#include <osl/thread.hxx>
#include <sal/config.h>
#include <rtl/bootstrap.hxx>
#include <rtl/ustrbuf.hxx>
#include <comphelper/DirectoryHelper.hxx>

#include <DocumentAIScenarioStore.hxx>
#include <AIFileManager.hxx>
#include <BatchJob.hxx>
#include <PermissionCenter.hxx>
#include <kq_permission_prompt.hxx>
#include <vcl/vclenum.hxx>
#include <sfx2/filedlghelper.hxx>
#include <unotools/ucbstreamhelper.hxx>
#include <unotools/tempfile.hxx>
#include <tools/stream.hxx>
#include <tools/urlobj.hxx>
#include <tools/fract.hxx>
#include <vcl/filter/PDFiumLibrary.hxx>
#include <vcl/pdfwriter.hxx>
#include <vcl/bitmap.hxx>
#include <vcl/mapmod.hxx>
#include <basegfx/vector/b2dsize.hxx>

#include <com/sun/star/ui/dialogs/ExecutableDialogResults.hpp>
#include <com/sun/star/ui/dialogs/TemplateDescription.hpp>
#include <com/sun/star/ui/dialogs/XFolderPicker2.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>

#include <config_folders.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
/** Stable stderr segments when KQOFFICE_STARTUP_TIMING=1 (matches desktop app.cxx). */
bool KqStartupTimingEnabled()
{
    static const bool b = (std::getenv("KQOFFICE_STARTUP_TIMING") != nullptr);
    return b;
}

void KqStartupTimeLog(const char* message, long long ms)
{
    if (!KqStartupTimingEnabled())
        return;
    fprintf(stderr, "kqoffice.startuptime %s%lld ms\n", message, ms);
    fflush(stderr);
}

long long KqStartupElapsedMs(std::chrono::high_resolution_clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::high_resolution_clock::now() - start)
        .count();
}
}

class FileWorkbenchScanThread final : public osl::Thread
{
public:
    FileWorkbenchScanThread(BackingWindow& rOwner,
                            kqoffice::ai::filemgr::ScanFilter aFilter)
        : mrOwner(rOwner)
        , maFilter(std::move(aFilter))
    {
    }

    void SAL_CALL run() override;

private:
    BackingWindow& mrOwner;
    kqoffice::ai::filemgr::ScanFilter maFilter;
};

void FileWorkbenchScanThread::run()
{
    osl_setThreadName("KQOfficeFileIndex");
    kqoffice::ai::filemgr::AIFileManager manager;
    auto result = manager.scan(maFilter);
    if (maFilter.cancelFlag && maFilter.cancelFlag->load())
        return;

    std::scoped_lock guard(mrOwner.maFileWorkbenchResultMutex);
    mrOwner.maFileWorkbenchFiles = std::move(result.files);
    mrOwner.mnFileWorkbenchScanDurationMs = result.scanDurationMs;
    mrOwner.mbFileWorkbenchScanReady = true;
}

using namespace ::com::sun::star;
using namespace ::com::sun::star::beans;
using namespace ::com::sun::star::frame;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::document;

class BrandImage final : public weld::CustomWidgetController
{
private:
    Bitmap maBrandImage;
    bool mbIsDark = false;
    Size m_BmpSize;

public:
    const Size & getSize() { return m_BmpSize; }

    virtual void SetDrawingArea(weld::DrawingArea* pDrawingArea) override
    {
        weld::CustomWidgetController::SetDrawingArea(pDrawingArea);

        const StyleSettings& rStyleSettings = Application::GetSettings().GetStyleSettings();
        OutputDevice& rDevice = pDrawingArea->get_ref_device();
        rDevice.SetBackground(Wallpaper(rStyleSettings.GetWindowColor()));

        SetPointer(PointerStyle::RefHand);
    }

    virtual void Resize() override
    {
        auto nWidth = GetOutputSizePixel().Width();
        if (maBrandImage.GetSizePixel().Width() != nWidth)
            LoadImageForWidth(nWidth);
        weld::CustomWidgetController::Resize();
    }

    void LoadImageForWidth(int nWidth)
    {
        mbIsDark = Application::GetSettings().GetStyleSettings().GetDialogColor().IsDark();
        SfxApplication::loadBrandSvg(mbIsDark ? u"shell/logo-sc_inverted" : u"shell/logo-sc",
                                    maBrandImage, nWidth);
    }

    void ConfigureForWidth(int nWidth)
    {
        LoadImageForWidth(nWidth);
        m_BmpSize = maBrandImage.GetSizePixel();
        set_size_request(m_BmpSize.Width(), m_BmpSize.Height());
    }

    virtual void StyleUpdated() override
    {
        const StyleSettings& rStyleSettings = Application::GetSettings().GetStyleSettings();

        // tdf#141857 update background to current theme
        OutputDevice& rDevice = GetDrawingArea()->get_ref_device();
        rDevice.SetBackground(Wallpaper(rStyleSettings.GetWindowColor()));

        const bool bIsDark = rStyleSettings.GetDialogColor().IsDark();
        if (bIsDark != mbIsDark)
            LoadImageForWidth(GetOutputSizePixel().Width());
        weld::CustomWidgetController::StyleUpdated();
    }

    virtual bool MouseButtonUp(const MouseEvent& rMEvt) override
    {
        if (rMEvt.IsLeft())
        {
            OUString sURL = officecfg::Office::Common::Menus::VolunteerURL::get();
            if (sURL.isEmpty())
                return true;
            localizeWebserviceURI(sURL);
            if (sURL.isEmpty())
                return true;

            Reference<css::system::XSystemShellExecute> const xSystemShellExecute(
                css::system::SystemShellExecute::create(
                    ::comphelper::getProcessComponentContext()));
            xSystemShellExecute->execute(sURL, OUString(),
                                         css::system::SystemShellExecuteFlags::URIS_ONLY);
        }
        return true;
    }

    virtual void Paint(vcl::RenderContext& rRenderContext, const tools::Rectangle&) override
    {
        rRenderContext.DrawBitmap(Point(0, 0), maBrandImage);
    }
};

// increase size of the text in the buttons on the left fMultiplier-times
// Slightly calmer scale than stock LO — denser, WPS-like office workbench.
float const g_fMultiplier = 1.12f;

BackingWindow::BackingWindow(vcl::Window* i_pParent)
    : InterimItemWindow(i_pParent, u"sfx/ui/startcenter.ui"_ustr, u"StartCenter"_ustr, false)
    , mxOpenButton(m_xBuilder->weld_button(u"open_all"_ustr))
    , mxRecentButton(m_xBuilder->weld_toggle_button(u"open_recent"_ustr))
    , mxRemoteButton(m_xBuilder->weld_button(u"open_remote"_ustr))
    , mxTemplateButton(m_xBuilder->weld_toggle_button(u"templates_all"_ustr))
    , mxFileWorkbenchButton(m_xBuilder->weld_toggle_button(u"file_workbench"_ustr))
    , mxCreateLabel(m_xBuilder->weld_label(u"create_label"_ustr))
    , mxAllRecentLabel(m_xBuilder->weld_label(u"all_recent_label"_ustr))
    , mxLocalViewLabel(m_xBuilder->weld_label(u"local_view_label"_ustr))
    , mxFileWorkbenchLabel(m_xBuilder->weld_label(u"file_workbench_label"_ustr))
    , mxFileWorkbenchBox(m_xBuilder->weld_container(u"file_workbench_box"_ustr))
    , mxFileWorkbenchSearch(m_xBuilder->weld_entry(u"file_workbench_search"_ustr))
    , mxFileWorkbenchAuthorize(m_xBuilder->weld_button(u"file_workbench_authorize"_ustr))
    , mxFileWorkbenchRevoke(m_xBuilder->weld_button(u"file_workbench_revoke"_ustr))
    , mxFileWorkbenchNetworkRevoke(m_xBuilder->weld_button(u"file_workbench_network_revoke"_ustr))
    , mxFileWorkbenchRefresh(m_xBuilder->weld_button(u"file_workbench_refresh"_ustr))
    , mxFileWorkbenchPin(m_xBuilder->weld_button(u"file_workbench_pin"_ustr))
    , mxFileWorkbenchTag(m_xBuilder->weld_entry(u"file_workbench_tag"_ustr))
    , mxFileWorkbenchTagBtn(m_xBuilder->weld_button(u"file_workbench_tag_btn"_ustr))
    , mxFileWorkbenchOpen(m_xBuilder->weld_button(u"file_workbench_open"_ustr))
    , mxFileWorkbenchDelete(m_xBuilder->weld_button(u"file_workbench_delete"_ustr))
    , mxFileWorkbenchExportPdf(m_xBuilder->weld_button(u"file_workbench_export_pdf"_ustr))
    , mxFileWorkbenchExportOffice(m_xBuilder->weld_button(u"file_workbench_export_office"_ustr))
    , mxFileWorkbenchStatus(m_xBuilder->weld_label(u"file_workbench_status"_ustr))
    , mxFileWorkbenchNetworkStatus(m_xBuilder->weld_label(u"file_workbench_network_status"_ustr))
    , mxFileWorkbenchCapStatus(m_xBuilder->weld_label(u"file_workbench_cap_status"_ustr))
    , mxFileWorkbenchRiskHint(m_xBuilder->weld_label(u"file_workbench_risk_hint"_ustr))
    , mxFileWorkbenchAuthTree(m_xBuilder->weld_tree_view(u"file_workbench_auth_tree"_ustr))
    , mxFileWorkbenchTree(m_xBuilder->weld_tree_view(u"file_workbench_tree"_ustr))
    , mxScenarioLabel(m_xBuilder->weld_label(u"scenario_label"_ustr))
    , mxScenarioWriterGroup(m_xBuilder->weld_label(u"scenario_writer_group"_ustr))
    , mxScenarioCalcGroup(m_xBuilder->weld_label(u"scenario_calc_group"_ustr))
    , mxScenarioImpressGroup(m_xBuilder->weld_label(u"scenario_impress_group"_ustr))
    , mxScenarioCompatGroup(m_xBuilder->weld_label(u"scenario_compat_group"_ustr))
    , mxScenarioFallbackHint(m_xBuilder->weld_label(u"scenario_fallback_hint"_ustr))
    , mxTemplateSearch(m_xBuilder->weld_entry(u"template_search"_ustr))
    , mxTemplateCategory(m_xBuilder->weld_combo_box(u"template_category"_ustr))
    , mxTemplateMarketTree(m_xBuilder->weld_tree_view(u"template_market_tree"_ustr))
    , mxAltHelpLabel(m_xBuilder->weld_label(u"althelplabel"_ustr))
    , mxFilter(m_xBuilder->weld_combo_box(u"cbFilter"_ustr))
    , mxActions(m_xBuilder->weld_menu_button(u"mbActions"_ustr))
    , mxWriterAllButton(m_xBuilder->weld_button(u"writer_all"_ustr))
    , mxCalcAllButton(m_xBuilder->weld_button(u"calc_all"_ustr))
    , mxImpressAllButton(m_xBuilder->weld_button(u"impress_all"_ustr))
    , mxDrawAllButton(m_xBuilder->weld_button(u"draw_all"_ustr))
    , mxDBAllButton(m_xBuilder->weld_button(u"database_all"_ustr))
    , mxMathAllButton(m_xBuilder->weld_button(u"math_all"_ustr))
    , mxAiCreateLabel(m_xBuilder->weld_label(u"ai_create_label"_ustr))
    , mxAiDraftWriterButton(m_xBuilder->weld_button(u"ai_draft_writer"_ustr))
    , mxAiDraftCalcButton(m_xBuilder->weld_button(u"ai_draft_calc"_ustr))
    , mxAiDraftImpressButton(m_xBuilder->weld_button(u"ai_draft_impress"_ustr))
    , mxScenarioBox(m_xBuilder->weld_container(u"scenario_box"_ustr))
    , mxScenarioReportButton(m_xBuilder->weld_button(u"scenario_report"_ustr))
    , mxScenarioMinutesButton(m_xBuilder->weld_button(u"scenario_minutes"_ustr))
    , mxScenarioNoticeButton(m_xBuilder->weld_button(u"scenario_notice"_ustr))
    , mxScenarioPlanButton(m_xBuilder->weld_button(u"scenario_plan"_ustr))
    , mxScenarioOutlineButton(m_xBuilder->weld_button(u"scenario_outline"_ustr))
    , mxScenarioBudgetButton(m_xBuilder->weld_button(u"scenario_budget"_ustr))
    , mxScenarioSalesButton(m_xBuilder->weld_button(u"scenario_sales"_ustr))
    , mxScenarioScheduleButton(m_xBuilder->weld_button(u"scenario_schedule"_ustr))
    , mxScenarioPitchButton(m_xBuilder->weld_button(u"scenario_pitch"_ustr))
    , mxScenarioProjectReportButton(m_xBuilder->weld_button(u"scenario_project_report"_ustr))
    , mxScenarioCompatOpenButton(m_xBuilder->weld_button(u"scenario_compat_open"_ustr))
    , mxScenarioCoursewareButton(m_xBuilder->weld_button(u"scenario_courseware"_ustr))
    // Brand + Recent/Local CustomWeld views: lazy (ensure*) — avoid screen
    // metrics / SfxDocumentTemplates / SVG on the cold-start critical path.
    , mxBrandImage()
    , mxBrandImageWeld()
    , mxHelpButton(m_xBuilder->weld_button(u"help"_ustr))
    , mxExtensionsButton(m_xBuilder->weld_button(u"extensions"_ustr))
    , mxAllButtonsBox(m_xBuilder->weld_container(u"all_buttons_box"_ustr))
    , mxButtonsBox(m_xBuilder->weld_container(u"buttons_box"_ustr))
    , mxSmallButtonsBox(m_xBuilder->weld_container(u"small_buttons_box"_ustr))
    , mxAllRecentThumbnails()
    , mxAllRecentThumbnailsWin()
    , mxLocalView()
    , mxLocalViewWin()
    , mbLocalViewInitialized(false)
    , maDeferredSecondaryInitIdle("BackingWindow DeferredSecondaryInit")
    , mbInitControls(false)
    , maFileWorkbenchPollTimer("FileWorkbenchPoll")
{
    // NOTE: InterimItemWindow base + weld_button list above load startcenter.ui
    // and bind every widget — that cost is included in BackingWindow.ctor below.
    const auto tCtorBody = std::chrono::high_resolution_clock::now();

    // init background, undo InterimItemWindow defaults for this widget
    SetPaintTransparent(false);

    const bool bHasExtensionsURL(!officecfg::Office::Common::Menus::ExtensionsURL::get().isEmpty());

    // square action button
    auto nHeight = mxFilter->get_preferred_size().getHeight();
    mxActions->set_size_request(nHeight, nHeight);

    //set an alternative help label that doesn't hotkey the H of the Help menu
    mxHelpButton->set_label(mxAltHelpLabel->get_label());
    mxHelpButton->connect_clicked(LINK(this, BackingWindow, ClickHelpHdl));

    if (!bHasExtensionsURL)
    {
        mxExtensionsButton->hide();
    }

    try
    {
        mxContext.set( ::comphelper::getProcessComponentContext(), uno::UNO_SET_THROW );
    }
    catch (const Exception&)
    {
        TOOLS_WARN_EXCEPTION( "fwk", "BackingWindow" );
    }

    SetStyle( GetStyle() | WB_DIALOGCONTROL );

    maFileWorkbenchPollTimer.SetTimeout(100);
    maFileWorkbenchPollTimer.SetInvokeHandler(
        LINK(this, BackingWindow, FileWorkbenchPollHdl));

    // Desktop dispatch provider: ensureDesktopDispatch() on first use.
    // Body-only cost (UIXML + weld binds happen in the initializer list / base).
    KqStartupTimeLog("BackingWindow.ctor.body: ", KqStartupElapsedMs(tCtorBody));
}

IMPL_LINK(BackingWindow, ClickHelpHdl, weld::Button&, rButton, void)
{
    if (Help* pHelp = Application::GetHelp())
        pHelp->Start(m_xContainer->get_help_id(), &rButton);
}

BackingWindow::~BackingWindow()
{
    disposeOnce();
}

void BackingWindow::dispose()
{
    // Cancel deferred secondary init before tearing down weld widgets.
    // Without this, --writer / direct factory launch disposes the Start Center
    // while Idle/PostUserEvent is still pending → null-deref / Abort in
    // DeferredSecondaryInitHdl (W2-A/W2-B P0; W3-A RemoveUserEvent guard).
    maDeferredSecondaryInitIdle.Stop();
    if (mpDeferredSecondaryInitEvent)
    {
        Application::RemoveUserEvent(mpDeferredSecondaryInitEvent);
        mpDeferredSecondaryInitEvent = nullptr;
    }
    mbSecondaryInitDone = true;

    cancelFileWorkbenchScan();
    maFileWorkbenchPollTimer.Stop();

    // deregister drag&drop helper (all surfaces: recent / local / workbench)
    if (mxDropTargetListener.is())
    {
        for (const auto& xDrop : mxDropTargets)
        {
            if (xDrop.is())
            {
                try
                {
                    xDrop->removeDropTargetListener(mxDropTargetListener);
                    xDrop->setActive(false);
                }
                catch (const css::uno::Exception&)
                {
                }
            }
        }
        mxDropTargetListener.clear();
    }
    mxDropTargets.clear();
    mxOpenButton.reset();
    mxRemoteButton.reset();
    mxRecentButton.reset();
    mxTemplateButton.reset();
    mxFileWorkbenchButton.reset();
    mxCreateLabel.reset();
    mxAllRecentLabel.reset();
    mxLocalViewLabel.reset();
    mxFileWorkbenchLabel.reset();
    mxFileWorkbenchSearch.reset();
    mxFileWorkbenchAuthorize.reset();
    mxFileWorkbenchRevoke.reset();
    mxFileWorkbenchNetworkRevoke.reset();
    mxFileWorkbenchRefresh.reset();
    mxFileWorkbenchPin.reset();
    mxFileWorkbenchTag.reset();
    mxFileWorkbenchTagBtn.reset();
    mxFileWorkbenchOpen.reset();
    mxFileWorkbenchDelete.reset();
    mxFileWorkbenchExportPdf.reset();
    mxFileWorkbenchExportOffice.reset();
    mxFileWorkbenchStatus.reset();
    mxFileWorkbenchNetworkStatus.reset();
    mxFileWorkbenchCapStatus.reset();
    mxFileWorkbenchRiskHint.reset();
    mxFileWorkbenchAuthTree.reset();
    mxFileWorkbenchTree.reset();
    mxFileWorkbenchBox.reset();
    mxScenarioLabel.reset();
    mxScenarioWriterGroup.reset();
    mxScenarioCalcGroup.reset();
    mxScenarioImpressGroup.reset();
    mxScenarioCompatGroup.reset();
    mxScenarioFallbackHint.reset();
    mxTemplateSearch.reset();
    mxTemplateCategory.reset();
    mxTemplateMarketTree.reset();
    mxAltHelpLabel.reset();
    mxFilter.reset();
    mxActions.reset();
    mxWriterAllButton.reset();
    mxCalcAllButton.reset();
    mxImpressAllButton.reset();
    mxDrawAllButton.reset();
    mxDBAllButton.reset();
    mxMathAllButton.reset();
    mxAiCreateLabel.reset();
    mxAiDraftWriterButton.reset();
    mxAiDraftCalcButton.reset();
    mxAiDraftImpressButton.reset();
    mxScenarioBox.reset();
    mxScenarioReportButton.reset();
    mxScenarioMinutesButton.reset();
    mxScenarioNoticeButton.reset();
    mxScenarioPlanButton.reset();
    mxScenarioOutlineButton.reset();
    mxScenarioBudgetButton.reset();
    mxScenarioSalesButton.reset();
    mxScenarioScheduleButton.reset();
    mxScenarioPitchButton.reset();
    mxScenarioProjectReportButton.reset();
    mxScenarioCompatOpenButton.reset();
    mxScenarioCoursewareButton.reset();
    mxBrandImageWeld.reset();
    mxBrandImage.reset();
    mxHelpButton.reset();
    mxExtensionsButton.reset();
    mxAllButtonsBox.reset();
    mxButtonsBox.reset();
    mxSmallButtonsBox.reset();
    mxAllRecentThumbnailsWin.reset();
    mxAllRecentThumbnails.reset();
    mxLocalViewWin.reset();
    mxLocalView.reset();
    InterimItemWindow::dispose();
}

void BackingWindow::initControls()
{
    if( mbInitControls )
        return;

    mbInitControls = true;
    const auto tInit = std::chrono::high_resolution_clock::now();
    auto tSeg = tInit;

    // —— Critical path only (must finish before first paint) ——
    // Secondary wiring, module checks, brand SVG style, workbench, LocalView
    // disk scan, and recent thumbnail decode all run in DeferredSecondaryInitHdl.

    // Office/WPS home: if there is nothing to continue, land on templates so the
    // first screen is useful rather than a large empty recent pane.
    // Note: do not call full ToggleHdl before deferred content init (Reload /
    // LocalView::reload pull disk I/O onto the cold-start critical path).
    const bool bHasRecentWork = !SvtHistoryOptions::GetList(EHistoryType::PickList).empty();
    KqStartupTimeLog("BackingWindow.initControls.history: ", KqStartupElapsedMs(tSeg));
    tSeg = std::chrono::high_resolution_clock::now();
    if (bHasRecentWork)
    {
        mxRecentButton->set_active(true);
        mxAllRecentLabel->show();
        mxLocalViewLabel->hide();
        // Recent CustomWeld created in DeferredSecondaryInit / ToggleHdl.
        if (mxLocalView)
            mxLocalView->Hide();
        mxTemplateButton->set_active(false);
        if (mxFileWorkbenchButton)
            mxFileWorkbenchButton->set_active(false);
        showFileWorkbenchPane(false);
        if (mxScenarioBox)
            mxScenarioBox->hide();
        mxActions->show();
    }
    else
    {
        // Light first paint: template market tree only; scenario chrome + LocalView
        // grid filled in DeferredSecondaryInitHdl.
        mxTemplateButton->set_active(true);
        mxRecentButton->set_active(false);
        if (mxFileWorkbenchButton)
            mxFileWorkbenchButton->set_active(false);
        mxAllRecentLabel->hide();
        mxLocalViewLabel->show();
        if (mxAllRecentThumbnails)
            mxAllRecentThumbnails->Hide();
        if (mxLocalView)
            mxLocalView->Hide();
        showFileWorkbenchPane(false);
        // Defer scenario_box show until secondary init (many widgets / fonts).
        if (mxScenarioBox)
            mxScenarioBox->hide();
        mxActions->hide();
    }
    KqStartupTimeLog("BackingWindow.initControls.paneToggle: ", KqStartupElapsedMs(tSeg));
    tSeg = std::chrono::high_resolution_clock::now();

    // Primary left-rail actions (user may click immediately).
    mxOpenButton->connect_clicked(LINK(this, BackingWindow, ClickHdl));
    mxWriterAllButton->connect_clicked(LINK(this, BackingWindow, ClickHdl));
    mxDrawAllButton->connect_clicked(LINK(this, BackingWindow, ClickHdl));
    mxCalcAllButton->connect_clicked(LINK(this, BackingWindow, ClickHdl));
    mxImpressAllButton->connect_clicked(LINK(this, BackingWindow, ClickHdl));
    if (mxAiDraftWriterButton)
        mxAiDraftWriterButton->connect_clicked(LINK(this, BackingWindow, AiDraftHdl));
    if (mxAiDraftCalcButton)
        mxAiDraftCalcButton->connect_clicked(LINK(this, BackingWindow, AiDraftHdl));
    if (mxAiDraftImpressButton)
        mxAiDraftImpressButton->connect_clicked(LINK(this, BackingWindow, AiDraftHdl));

    mxRecentButton->connect_toggled(LINK(this, BackingWindow, ToggleHdl));
    mxTemplateButton->connect_toggled(LINK(this, BackingWindow, ToggleHdl));
    if (mxFileWorkbenchButton)
        mxFileWorkbenchButton->connect_toggled(LINK(this, BackingWindow, ToggleHdl));
    KqStartupTimeLog("BackingWindow.initControls.primaryHandlers: ", KqStartupElapsedMs(tSeg));
    tSeg = std::chrono::high_resolution_clock::now();

    // Template market list is in-memory (no disk) — fill for first useful paint.
    if (mxTemplateCategory)
    {
        static constexpr std::u16string_view kCats[] = {
            u"推荐",       u"热门",       u"AI 场景",   u"信息收集", u"数据分析",
            u"销售管理",   u"行政财务",   u"人力资源",   u"项目管理", u"协作效率",
            u"个人成长",   u"演示文稿",   u"公文信函",   u"PDF"};
        mxTemplateCategory->clear();
        for (const auto& c : kCats)
            mxTemplateCategory->append_text(OUString(c));
        mxTemplateCategory->set_active(0);
        mxTemplateCategory->connect_changed(LINK(this, BackingWindow, TemplateCategoryHdl));
    }
    if (mxTemplateSearch)
        mxTemplateSearch->connect_changed(LINK(this, BackingWindow, TemplateSearchHdl));
    if (mxTemplateMarketTree)
    {
        mxTemplateMarketTree->set_column_fixed_widths({ 200, 72, 420 });
        mxTemplateMarketTree->connect_row_activated(
            LINK(this, BackingWindow, TemplateMarketActivateHdl));
    }
    if (!bHasRecentWork)
        refreshTemplateMarket();
    KqStartupTimeLog("BackingWindow.initControls.templateMarket: ", KqStartupElapsedMs(tSeg));
    tSeg = std::chrono::high_resolution_clock::now();

    showFileWorkbenchPane(false);

    // Single deferred wave: style, modules, secondary handlers, content hydrate.
    // Default: Idle DEFAULT_IDLE so work runs after high-priority paint (W5-B:
    // PostUserEvent logged before firstPaint). Escape hatch: KQOFFICE_DEFSEC_IDLE=0
    // restores PostUserEvent (W3-A / W5 path). dispose() cancels either path.
    maDeferredSecondaryInitIdle.Stop();
    if (mpDeferredSecondaryInitEvent)
    {
        Application::RemoveUserEvent(mpDeferredSecondaryInitEvent);
        mpDeferredSecondaryInitEvent = nullptr;
    }
    const char* pDefSecIdle = std::getenv("KQOFFICE_DEFSEC_IDLE");
    const bool bUseIdle = !(pDefSecIdle && pDefSecIdle[0] == '0' && pDefSecIdle[1] == '\0');
    if (bUseIdle)
    {
        maDeferredSecondaryInitIdle.SetPriority(TaskPriority::DEFAULT_IDLE);
        maDeferredSecondaryInitIdle.SetInvokeHandler(
            LINK(this, BackingWindow, DeferredSecondaryInitIdleHdl));
        maDeferredSecondaryInitIdle.Start();
        KqStartupTimeLog("BackingWindow.initControls.idleSchedule: ", KqStartupElapsedMs(tSeg));
    }
    else
    {
        // bReferenceLink keeps VclPtr until run/remove; still RemoveUserEvent in dispose.
        mpDeferredSecondaryInitEvent = Application::PostUserEvent(
            LINK(this, BackingWindow, DeferredSecondaryInitHdl), nullptr, true);
        KqStartupTimeLog("BackingWindow.initControls.postUserEvent: ", KqStartupElapsedMs(tSeg));
    }
    KqStartupTimeLog("BackingWindow.initControls.total: ", KqStartupElapsedMs(tInit));
}

void BackingWindow::DataChanged(const DataChangedEvent& rDCEvt)
{
    if ((rDCEvt.GetType() != DataChangedEventType::SETTINGS)
        || !(rDCEvt.GetFlags() & AllSettingsFlags::STYLE))
    {
        InterimItemWindow::DataChanged(rDCEvt);
        return;
    }

    ApplyStyleSettings();
    Invalidate();
}

template <typename WidgetClass>
void BackingWindow::setLargerFont(WidgetClass& pWidget, const vcl::Font& rFont)
{
    vcl::Font aFont(rFont);
    aFont.SetFontSize(Size(0, aFont.GetFontSize().Height() * g_fMultiplier));
    pWidget->set_font(aFont);
}

void BackingWindow::ApplyStyleSettings()
{
    const StyleSettings& rStyleSettings = GetSettings().GetStyleSettings();
    const Color aWorkbenchBackground(rStyleSettings.GetDialogColor());
    const Color aRailBackground(rStyleSettings.GetFaceColor());
    const vcl::Font& aButtonFont(rStyleSettings.GetPushButtonFont());
    const vcl::Font& aLabelFont(rStyleSettings.GetLabelFont());

    // setup larger fonts
    setLargerFont(mxOpenButton, aButtonFont);
    setLargerFont(mxRemoteButton, aButtonFont);
    setLargerFont(mxRecentButton, aButtonFont);
    setLargerFont(mxTemplateButton, aButtonFont);
    setLargerFont(mxWriterAllButton, aButtonFont);
    setLargerFont(mxDrawAllButton, aButtonFont);
    setLargerFont(mxCalcAllButton, aButtonFont);
    setLargerFont(mxDBAllButton, aButtonFont);
    setLargerFont(mxImpressAllButton, aButtonFont);
    setLargerFont(mxMathAllButton, aButtonFont);
    if (mxAiDraftWriterButton)
        setLargerFont(mxAiDraftWriterButton, aButtonFont);
    if (mxAiDraftCalcButton)
        setLargerFont(mxAiDraftCalcButton, aButtonFont);
    if (mxAiDraftImpressButton)
        setLargerFont(mxAiDraftImpressButton, aButtonFont);
    const sal_Int32 nScenarioButtonHeight = mxFilter->get_preferred_size().getHeight() + 8;
    for (const auto& rScenario : getScenarioTemplates())
    {
        if (rScenario.pButton)
        {
            rScenario.pButton->set_font(aButtonFont);
            rScenario.pButton->set_size_request(-1, nScenarioButtonHeight);
        }
    }
    if (mxScenarioCompatOpenButton)
    {
        mxScenarioCompatOpenButton->set_font(aButtonFont);
        mxScenarioCompatOpenButton->set_size_request(-1, nScenarioButtonHeight + 2);
    }

    // Section labels: medium-bold, clear hierarchy without oversized hero type.
    vcl::Font aSectionFont(aLabelFont);
    aSectionFont.SetWeight(WEIGHT_SEMIBOLD);
    aSectionFont.SetFontSize(Size(0, aSectionFont.GetFontSize().Height() * 1.12f));
    vcl::Font aWorkspaceTitleFont(aLabelFont);
    aWorkspaceTitleFont.SetWeight(WEIGHT_SEMIBOLD);
    aWorkspaceTitleFont.SetFontSize(Size(0, aWorkspaceTitleFont.GetFontSize().Height() * 1.06f));
    mxCreateLabel->set_font(aSectionFont);
    if (mxAiCreateLabel)
        mxAiCreateLabel->set_font(aSectionFont);
    if (mxAllRecentLabel)
        mxAllRecentLabel->set_font(aWorkspaceTitleFont);
    if (mxLocalViewLabel)
        mxLocalViewLabel->set_font(aWorkspaceTitleFont);
    if (mxScenarioLabel)
        mxScenarioLabel->set_font(aSectionFont);
    if (mxScenarioFallbackHint)
        mxScenarioFallbackHint->set_label_type(weld::LabelType::Warning);

    mxAllButtonsBox->set_background(aRailBackground);
    mxSmallButtonsBox->set_background(aRailBackground);
    SetBackground(aWorkbenchBackground);

    // compute the menubar height
    sal_Int32 nMenuHeight = 0;
    if (SystemWindow* pSystemWindow = GetSystemWindow())
        nMenuHeight = pSystemWindow->GetMenuBarHeight();

    // fdo#34392: we do the layout dynamically, the layout depends on the font,
    // so we should handle data changed events (font changing) of the last child
    // control, at this point all the controls have updated settings (i.e. font).
    Size aPrefSize(mxAllButtonsBox->get_preferred_size());
    set_width_request(aPrefSize.Width());

    // Brand SVG (lazy): only load when sizing the start center chrome.
    ensureBrandImage();
    sal_Int32 nBrandH = 0;
    if (mxBrandImage)
    {
        weld::DrawingArea* pDrawingArea = mxBrandImage->GetDrawingArea();
        if (pDrawingArea)
        {
            mxBrandImage->ConfigureForWidth(
                aPrefSize.Width()
                - (pDrawingArea->get_margin_start() + pDrawingArea->get_margin_end()));
            aPrefSize = mxAllButtonsBox->get_preferred_size();
            nBrandH = mxBrandImage->getSize().getHeight();
        }
    }

    set_height_request(nMenuHeight + aPrefSize.Height() + nBrandH);
}

void BackingWindow::ensureRecentThumbnails()
{
    if (mxAllRecentThumbnails)
        return;
    mxAllRecentThumbnails.reset(new sfx2::RecentDocsView(
        m_xBuilder->weld_scrolled_window(u"scrollrecent"_ustr, true)));
    mxAllRecentThumbnailsWin.reset(
        new weld::CustomWeld(*m_xBuilder, u"all_recent"_ustr, *mxAllRecentThumbnails));
    attachOpenFileDrop(mxAllRecentThumbnails->GetDropTarget());
}

void BackingWindow::attachOpenFileDrop(
    const css::uno::Reference<css::datatransfer::dnd::XDropTarget>& xDrop)
{
    if (!xDrop.is() || !mxDropTargetListener.is())
        return;
    // Avoid double-register on the same target
    for (const auto& existing : mxDropTargets)
    {
        if (existing == xDrop)
            return;
    }
    try
    {
        xDrop->addDropTargetListener(mxDropTargetListener);
        xDrop->setActive(true);
        mxDropTargets.push_back(xDrop);
    }
    catch (const css::uno::Exception&)
    {
    }
}

void BackingWindow::ensureLocalView()
{
    if (mxLocalView)
        return;
    mxLocalView.reset(new TemplateDefaultView(
        m_xBuilder->weld_scrolled_window(u"scrolllocal"_ustr, true),
        m_xBuilder->weld_menu(u"localmenu"_ustr)));
    mxLocalViewWin.reset(new weld::CustomWeld(*m_xBuilder, u"local_view"_ustr, *mxLocalView));
    // Wave multimodal: drop files onto template/local pane to open (same as recent)
    attachOpenFileDrop(mxLocalView->GetDropTarget());
}

void BackingWindow::ensureBrandImage()
{
    if (mxBrandImage)
        return;
    mxBrandImage.reset(new BrandImage);
    mxBrandImageWeld.reset(new weld::CustomWeld(*m_xBuilder, u"daBrand"_ustr, *mxBrandImage));
}

void BackingWindow::ensureDesktopDispatch()
{
    if (mxDesktopDispatchProvider.is())
        return;
    try
    {
        Reference<XDesktop2> xDesktop
            = Desktop::create(comphelper::getProcessComponentContext());
        mxDesktopDispatchProvider = xDesktop;
    }
    catch (const Exception&)
    {
        TOOLS_WARN_EXCEPTION("fwk", "BackingWindow::ensureDesktopDispatch");
    }
}

void BackingWindow::initializeLocalView()
{
    ensureLocalView();
    if (!mxLocalView)
        return;
    if (!mbLocalViewInitialized)
    {
        mbLocalViewInitialized = true;
        mxLocalView->Populate();
        mxLocalView->filterItems(ViewFilter_Application(FILTER_APPLICATION::NONE));
        mxLocalView->showAllTemplates();
    }
}

void BackingWindow::checkInstalledModules()
{
    // Keep remote / extensions / secondary modules out of the first screen.
    // Cloud entry stays hidden until a real connector ships (Wave α parity).
    // Draw slot is productized as 「打开 PDF」(WPS-style fourth create entry).
    // Null-safe: may be called only while Start Center is live, but widgets can
    // already be cleared if a dispose race slipped past the event cancel.
    if (!mxRemoteButton || !mxExtensionsButton || !mxMathAllButton || !mxDBAllButton
        || !mxWriterAllButton || !mxCalcAllButton || !mxImpressAllButton || !mxDrawAllButton
        || !mxTemplateButton || !mxCreateLabel)
        return;

    mxRemoteButton->set_visible(false);
    mxExtensionsButton->set_visible(false);
    mxMathAllButton->set_visible(false);
    mxDBAllButton->set_visible(false);

    if (officecfg::Office::Common::Misc::ViewerAppMode::get())
    {
        mxTemplateButton->set_visible(false);
        mxCreateLabel->set_visible(false);
        mxWriterAllButton->set_visible(false);
        mxCalcAllButton->set_visible(false);
        mxImpressAllButton->set_visible(false);
        mxDrawAllButton->set_visible(false);
        mxMathAllButton->set_visible(false);
        mxDBAllButton->set_visible(false);
        if (mxScenarioBox)
            mxScenarioBox->set_visible(false);
        return;
    }

    SvtModuleOptions aModuleOpt;

    const bool bWriterInstalled = aModuleOpt.IsWriterInstalled();
    const bool bCalcInstalled = aModuleOpt.IsCalcInstalled();
    const bool bImpressInstalled = aModuleOpt.IsImpressInstalled();
    const bool bDrawInstalled = aModuleOpt.IsDrawInstalled();
    const bool bPresentationTasksAvailable = bWriterInstalled || bImpressInstalled;

    mxWriterAllButton->set_sensitive(bWriterInstalled);
    mxCalcAllButton->set_sensitive(bCalcInstalled);
    mxImpressAllButton->set_sensitive(bImpressInstalled);
    // PDF entry uses Draw's PDF import path when available; else still allow Open.
    mxDrawAllButton->set_visible(true);
    mxDrawAllButton->set_sensitive(bDrawInstalled || bWriterInstalled);
    mxMathAllButton->set_sensitive(aModuleOpt.IsMathInstalled());
    mxDBAllButton->set_sensitive(aModuleOpt.IsDataBaseInstalled());

    if (mxScenarioWriterGroup)
        mxScenarioWriterGroup->set_sensitive(bWriterInstalled);
    if (mxScenarioCalcGroup)
        mxScenarioCalcGroup->set_sensitive(bCalcInstalled);
    if (mxScenarioImpressGroup)
        mxScenarioImpressGroup->set_sensitive(bPresentationTasksAvailable);

    const bool bCanOpenCompatibleFiles = bWriterInstalled || bCalcInstalled || bImpressInstalled;
    if (mxScenarioCompatGroup)
        mxScenarioCompatGroup->set_sensitive(bCanOpenCompatibleFiles);
    if (mxScenarioCompatOpenButton)
        mxScenarioCompatOpenButton->set_sensitive(bCanOpenCompatibleFiles);
    for (const auto& rScenario : getScenarioTemplates())
    {
        if (!rScenario.pButton)
            continue;

        bool bEnabled = false;
        switch (rScenario.eFilter)
        {
            case FILTER_APPLICATION::WRITER:
                bEnabled = bWriterInstalled;
                break;
            case FILTER_APPLICATION::CALC:
                bEnabled = bCalcInstalled;
                break;
            case FILTER_APPLICATION::IMPRESS:
                bEnabled = bImpressInstalled;
                break;
            default:
                break;
        }
        rScenario.pButton->set_sensitive(bEnabled);
    }
}

bool BackingWindow::PreNotify(NotifyEvent& rNEvt)
{
    if( rNEvt.GetType() == NotifyEventType::KEYINPUT )
    {
        const KeyEvent* pEvt = rNEvt.GetKeyEvent();
        const vcl::KeyCode& rKeyCode(pEvt->GetKeyCode());

        const bool bThumbnailHasFocus
            = (mxAllRecentThumbnails && mxAllRecentThumbnails->HasFocus())
              || (mxLocalView && mxLocalView->HasFocus());

        // Subwindows of BackingWindow: Sidebar and Thumbnail view
        if( rKeyCode.GetCode() == KEY_F6 )
        {
            if( rKeyCode.IsShift() ) // Shift + F6
            {
                if (bThumbnailHasFocus)
                {
                    mxOpenButton->grab_focus();
                    return true;
                }
            }
            else if ( rKeyCode.IsMod1() ) // Ctrl + F6
            {
                if(mxAllRecentThumbnails && mxAllRecentThumbnails->IsVisible())
                {
                    mxAllRecentThumbnails->GrabFocus();
                    return true;
                }
                else if(mxLocalView && mxLocalView->IsVisible())
                {
                    mxLocalView->GrabFocus();
                    return true;
                }
            }
            else // F6
            {
                if (!bThumbnailHasFocus)
                {
                    if(mxAllRecentThumbnails && mxAllRecentThumbnails->IsVisible())
                    {
                        mxAllRecentThumbnails->GrabFocus();
                        return true;
                    }
                    else if(mxLocalView && mxLocalView->IsVisible())
                    {
                        mxLocalView->GrabFocus();
                        return true;
                    }
                }
            }
        }

        // try the 'normal' accelerators (so that eg. Ctrl+Q works)
        if (!mpAccExec)
        {
            mpAccExec = svt::AcceleratorExecute::createAcceleratorHelper();
            mpAccExec->init( comphelper::getProcessComponentContext(), mxFrame);
        }

        const OUString aCommand = mpAccExec->findCommand(svt::AcceleratorExecute::st_VCLKey2AWTKey(rKeyCode));
        if ((aCommand != "vnd.sun.star.findbar:FocusToFindbar") && pEvt && mpAccExec->execute(rKeyCode))
            return true;
    }
    return InterimItemWindow::PreNotify( rNEvt );
}

void BackingWindow::GetFocus()
{
    GetFocusFlags nFlags = GetParent()->GetGetFocusFlags();
    if( nFlags & GetFocusFlags::F6 )
    {
        if( nFlags & GetFocusFlags::Forward ) // F6
        {
            mxOpenButton->grab_focus();
            return;
        }
        else // Shift + F6 or Ctrl + F6
        {
            if(mxAllRecentThumbnails && mxAllRecentThumbnails->IsVisible())
                mxAllRecentThumbnails->GrabFocus();
            else if(mxLocalView && mxLocalView->IsVisible())
                mxLocalView->GrabFocus();
            return;
        }
    }
    InterimItemWindow::GetFocus();
}

void BackingWindow::setOwningFrame( const css::uno::Reference< css::frame::XFrame >& xFrame )
{
    const auto tOwn = std::chrono::high_resolution_clock::now();
    mxFrame = xFrame;
    if( ! mbInitControls )
        initControls();

    // establish drag&drop: open dropped files on recent / templates / 本地文件 workbench
    auto tSeg = std::chrono::high_resolution_clock::now();
    mxDropTargetListener.set(new OpenFileDropTargetListener(mxContext, mxFrame));
    if (mxAllRecentThumbnails)
        attachOpenFileDrop(mxAllRecentThumbnails->GetDropTarget());
    if (mxLocalView)
        attachOpenFileDrop(mxLocalView->GetDropTarget());
    if (mxFileWorkbenchTree)
        attachOpenFileDrop(mxFileWorkbenchTree->get_drop_target());
    if (mxFileWorkbenchAuthTree)
        attachOpenFileDrop(mxFileWorkbenchAuthTree->get_drop_target());
    KqStartupTimeLog("BackingWindow.setOwningFrame.dnd: ", KqStartupElapsedMs(tSeg));
    tSeg = std::chrono::high_resolution_clock::now();

    ensureDesktopDispatch();
    css::uno::Reference<XFramesSupplier> xFramesSupplier(mxDesktopDispatchProvider, UNO_QUERY);
    if (xFramesSupplier)
        xFramesSupplier->setActiveFrame(mxFrame);
    KqStartupTimeLog("BackingWindow.setOwningFrame.desktopDispatch: ", KqStartupElapsedMs(tSeg));
    KqStartupTimeLog("BackingWindow.setOwningFrame.total: ", KqStartupElapsedMs(tOwn));
}

void BackingWindow::Paint(vcl::RenderContext& rRenderContext, const tools::Rectangle& rRect)
{
    if (!mbFirstPaintLogged)
    {
        mbFirstPaintLogged = true;
        const auto tPaint = std::chrono::high_resolution_clock::now();
        InterimItemWindow::Paint(rRenderContext, rRect);
        KqStartupTimeLog("BackingWindow.firstPaint: ", KqStartupElapsedMs(tPaint));
        return;
    }
    InterimItemWindow::Paint(rRenderContext, rRect);
}

IMPL_STATIC_LINK_NOARG(BackingWindow, ExtLinkClickHdl, weld::Button&, void)
{
    try
    {
        OUString sURL = officecfg::Office::Common::Menus::ExtensionsURL::get();
        if (sURL.isEmpty())
            return;
        sURL += "?LOvers=" + utl::ConfigManager::getProductVersion() +
            "&LOlocale=" + LanguageTag(utl::ConfigManager::getUILocale()).getBcp47();

        Reference<css::system::XSystemShellExecute> const
            xSystemShellExecute(
                css::system::SystemShellExecute::create(
                    ::comphelper::getProcessComponentContext()));
        xSystemShellExecute->execute(sURL, OUString(),
            css::system::SystemShellExecuteFlags::URIS_ONLY);
    }
    catch (const Exception&)
    {
    }
}

namespace
{
FILTER_APPLICATION lclGetTemplateFilter(int nFilter)
{
    if (nFilter == 1)
        return FILTER_APPLICATION::WRITER;
    if (nFilter == 2)
        return FILTER_APPLICATION::CALC;
    if (nFilter == 3)
        return FILTER_APPLICATION::IMPRESS;
    return FILTER_APPLICATION::NONE;
}

sfx2::ApplicationType lclGetRecentFilter(int nFilter)
{
    if (nFilter == 1)
        return sfx2::ApplicationType::TYPE_WRITER;
    if (nFilter == 2)
        return sfx2::ApplicationType::TYPE_CALC;
    if (nFilter == 3)
        return sfx2::ApplicationType::TYPE_IMPRESS;
    return sfx2::ApplicationType::TYPE_NONE;
}

int lclGetFilterIndex(FILTER_APPLICATION eFilter)
{
    switch (eFilter)
    {
        case FILTER_APPLICATION::WRITER:
            return 1;
        case FILTER_APPLICATION::CALC:
            return 2;
        case FILTER_APPLICATION::IMPRESS:
            return 3;
        default:
            return 0;
    }
}

}

void BackingWindow::applyFilter()
{
    const int nFilter = mxFilter ? mxFilter->get_active() : 0;
    if (mxLocalView && mxLocalView->IsVisible())
        mxLocalView->filterItems(ViewFilter_Application(lclGetTemplateFilter(nFilter)));
    else if (mxAllRecentThumbnails)
        mxAllRecentThumbnails->setFilter(lclGetRecentFilter(nFilter));

    if (mxScenarioFallbackHint)
        mxScenarioFallbackHint->hide();
}

IMPL_LINK_NOARG( BackingWindow, FilterHdl, weld::ComboBox&, void )
{
    applyFilter();
}

IMPL_LINK_NOARG(BackingWindow, DeferredRecentReloadHdl, void*, void)
{
    if (isDisposed() || !mxAllRecentThumbnails)
        return;
    mxAllRecentThumbnails->Reload();
    applyFilter();
}

IMPL_LINK_NOARG(BackingWindow, DeferredTemplateInitHdl, void*, void)
{
    // User may have switched away / Start Center disposed before the event runs.
    if (isDisposed() || !mxLocalView || !mxTemplateButton || !mxTemplateButton->get_active())
        return;
    initializeLocalView();
    mxLocalView->Show();
    mxLocalView->reload();
    applyFilter();
}

IMPL_LINK_NOARG(BackingWindow, DeferredSecondaryInitIdleHdl, Timer*, void)
{
    maDeferredSecondaryInitIdle.Stop();
    runDeferredSecondaryInit();
}

IMPL_LINK_NOARG(BackingWindow, DeferredSecondaryInitHdl, void*, void)
{
    // PostUserEvent path (KQOFFICE_DEFSEC_IDLE=0); clear id so dispose won't double-remove.
    mpDeferredSecondaryInitEvent = nullptr;
    runDeferredSecondaryInit();
}

void BackingWindow::runDeferredSecondaryInit()
{
    // Start Center often disposed before first idle when launching with
    // --writer/--calc/etc. Never touch weld widgets after dispose.
    if (isDisposed() || mbSecondaryInitDone)
        return;
    // dispose() resets these; double-check critical path widgets.
    if (!mxOpenButton || !mxRecentButton || !mxTemplateButton || !mxFilter)
        return;
    mbSecondaryInitDone = true;
    const auto tSec = std::chrono::high_resolution_clock::now();
    // Idle DEFAULT_IDLE: first paint should already have run (W5-B / W6-B).
    KqStartupTimeLog("BackingWindow.DeferredSecondaryInit.begin: ", 0);

    // Module availability (config) — after first paint.
    checkInstalledModules();

    if (mxExtensionsButton)
        mxExtensionsButton->connect_clicked(LINK(this, BackingWindow, ExtLinkClickHdl));

    SvtCommandOptions aCmdOptions;
    if (SvtCommandOptions().HasEntriesDisabled() && aCmdOptions.LookupDisabled(u"OpenRemote"_ustr))
    {
        if (mxRemoteButton)
            mxRemoteButton->set_visible(false);
    }
    else if (mxRemoteButton)
        mxRemoteButton->connect_clicked(LINK(this, BackingWindow, ClickHdl));

    if (mxDBAllButton)
        mxDBAllButton->connect_clicked(LINK(this, BackingWindow, ClickHdl));
    if (mxMathAllButton)
        mxMathAllButton->connect_clicked(LINK(this, BackingWindow, ClickHdl));

    for (const auto& rScenario : getScenarioTemplates())
    {
        if (rScenario.pButton)
            rScenario.pButton->connect_clicked(LINK(this, BackingWindow, OpenScenarioHdl));
    }
    if (mxScenarioCompatOpenButton)
        mxScenarioCompatOpenButton->connect_clicked(LINK(this, BackingWindow, OpenCompatibilityHdl));

    // File workbench — not on first paint.
    if (mxFileWorkbenchAuthorize)
        mxFileWorkbenchAuthorize->connect_clicked(LINK(this, BackingWindow, FileWorkbenchAuthorizeHdl));
    if (mxFileWorkbenchRevoke)
        mxFileWorkbenchRevoke->connect_clicked(LINK(this, BackingWindow, FileWorkbenchRevokeHdl));
    if (mxFileWorkbenchNetworkRevoke)
        mxFileWorkbenchNetworkRevoke->connect_clicked(
            LINK(this, BackingWindow, FileWorkbenchNetworkRevokeHdl));
    if (mxFileWorkbenchRefresh)
        mxFileWorkbenchRefresh->connect_clicked(LINK(this, BackingWindow, FileWorkbenchRefreshHdl));
    if (mxFileWorkbenchPin)
        mxFileWorkbenchPin->connect_clicked(LINK(this, BackingWindow, FileWorkbenchPinHdl));
    if (mxFileWorkbenchTagBtn)
        mxFileWorkbenchTagBtn->connect_clicked(LINK(this, BackingWindow, FileWorkbenchTagHdl));
    if (mxFileWorkbenchOpen)
        mxFileWorkbenchOpen->connect_clicked(LINK(this, BackingWindow, FileWorkbenchOpenHdl));
    if (mxFileWorkbenchDelete)
        mxFileWorkbenchDelete->connect_clicked(LINK(this, BackingWindow, FileWorkbenchDeleteHdl));
    if (mxFileWorkbenchExportPdf)
        mxFileWorkbenchExportPdf->connect_clicked(
            LINK(this, BackingWindow, FileWorkbenchExportPdfHdl));
    if (mxFileWorkbenchExportOffice)
        mxFileWorkbenchExportOffice->connect_clicked(
            LINK(this, BackingWindow, FileWorkbenchExportOfficeHdl));
    if (mxFileWorkbenchSearch)
        mxFileWorkbenchSearch->connect_changed(LINK(this, BackingWindow, FileWorkbenchSearchHdl));
    if (mxFileWorkbenchAuthTree)
        mxFileWorkbenchAuthTree->set_column_fixed_widths({ 480, 100 });
    if (mxFileWorkbenchTree)
    {
        // Multi-select for batch convert / multi open paths.
        mxFileWorkbenchTree->set_selection_mode(SelectionMode::Multiple);
        mxFileWorkbenchTree->connect_row_activated(
            LINK(this, BackingWindow, FileWorkbenchRowActivatedHdl));
        mxFileWorkbenchTree->set_column_fixed_widths({ 220, 90, 80, 360 });
    }

    if (mxFilter)
        mxFilter->connect_changed(LINK(this, BackingWindow, FilterHdl));
    if (mxActions)
        mxActions->connect_selected(LINK(this, BackingWindow, MenuSelectHdl));

    // Brand SVG + larger fonts + preferred sizes (was ~hundreds of ms with content).
    ApplyStyleSettings();

    // Style work / weld layout may re-enter the event loop; bail if Start Center
    // was torn down mid-handler (factory document open path).
    if (isDisposed() || !mxRecentButton || !mxTemplateButton)
        return;

    auto wireLocalViewHandlers = [this]() {
        if (!mxLocalView)
            return;
        mxLocalView->setCreateContextMenuHdl(LINK(this, BackingWindow, CreateContextMenuHdl));
        mxLocalView->setOpenTemplateHdl(LINK(this, BackingWindow, OpenTemplateHdl));
        mxLocalView->setEditTemplateHdl(LINK(this, BackingWindow, EditTemplateHdl));
        mxLocalView->ShowTooltips(true);
    };

    // Content hydrate for the active tab.
    if (mxRecentButton->get_active())
    {
        ensureRecentThumbnails();
        if (isDisposed())
            return;
        if (mxAllRecentThumbnails)
        {
            SvtModuleOptions aMod;
            if (aMod.IsWriterInstalled())
                mxAllRecentThumbnails->mnFileTypes |= sfx2::ApplicationType::TYPE_WRITER;
            if (aMod.IsCalcInstalled())
                mxAllRecentThumbnails->mnFileTypes |= sfx2::ApplicationType::TYPE_CALC;
            if (aMod.IsImpressInstalled())
                mxAllRecentThumbnails->mnFileTypes |= sfx2::ApplicationType::TYPE_IMPRESS;
            mxAllRecentThumbnails->ShowTooltips(true);
            mxAllRecentThumbnails->Reload();
        }
        if (!isDisposed())
            applyFilter();
    }
    else if (mxTemplateButton->get_active())
    {
        if (mxScenarioBox)
            mxScenarioBox->show();
        if (mxTemplateMarketTree && mxTemplateMarketTree->n_children() == 0)
            refreshTemplateMarket();
        if (isDisposed())
            return;
        ensureLocalView();
        wireLocalViewHandlers();
        if (mxLocalView)
        {
            initializeLocalView();
            if (isDisposed() || !mxLocalView)
                return;
            mxLocalView->Show();
            mxLocalView->reload();
        }
        if (!isDisposed())
            applyFilter();
    }
    KqStartupTimeLog("BackingWindow.DeferredSecondaryInit.total: ", KqStartupElapsedMs(tSec));
}

IMPL_LINK( BackingWindow, ToggleHdl, weld::Toggleable&, rButton, void )
{
    if (&rButton == mxRecentButton.get())
    {
        mxRecentButton->set_active(true);
        mxAllRecentLabel->show();
        mxLocalViewLabel->hide();
        if (mxLocalView)
            mxLocalView->Hide();
        ensureRecentThumbnails();
        if (mxAllRecentThumbnails)
        {
            mxAllRecentThumbnails->Show();
            mxAllRecentThumbnails->GrabFocus();
        }
        mxTemplateButton->set_active(false);
        if (mxFileWorkbenchButton)
            mxFileWorkbenchButton->set_active(false);
        showFileWorkbenchPane(false);
        if (mxScenarioBox)
            mxScenarioBox->hide();
        mxActions->show();
    }
    else if (mxFileWorkbenchButton && &rButton == mxFileWorkbenchButton.get())
    {
        mxFileWorkbenchButton->set_active(true);
        mxRecentButton->set_active(false);
        mxTemplateButton->set_active(false);
        mxAllRecentLabel->hide();
        mxLocalViewLabel->hide();
        if (mxAllRecentThumbnails)
            mxAllRecentThumbnails->Hide();
        if (mxLocalView)
            mxLocalView->Hide();
        mxActions->hide();
        showFileWorkbenchPane(true);
        if (mxScenarioBox)
            mxScenarioBox->hide();
        refreshFileWorkbench(false);
        if (mxFileWorkbenchSearch)
            mxFileWorkbenchSearch->grab_focus();
    }
    else
    {
        mxTemplateButton->set_active(true);
        mxAllRecentLabel->hide();
        mxLocalViewLabel->show();
        if (mxAllRecentThumbnails)
            mxAllRecentThumbnails->Hide();
        ensureLocalView();
        if (mxLocalView)
        {
            mxLocalView->setCreateContextMenuHdl(LINK(this, BackingWindow, CreateContextMenuHdl));
            mxLocalView->setOpenTemplateHdl(LINK(this, BackingWindow, OpenTemplateHdl));
            mxLocalView->setEditTemplateHdl(LINK(this, BackingWindow, EditTemplateHdl));
            mxLocalView->ShowTooltips(true);
        }
        initializeLocalView();
        if (mxLocalView)
        {
            mxLocalView->Show();
            mxLocalView->reload();
            mxLocalView->GrabFocus();
        }
        mxRecentButton->set_active(false);
        if (mxFileWorkbenchButton)
            mxFileWorkbenchButton->set_active(false);
        showFileWorkbenchPane(false);
        if (mxScenarioBox)
            mxScenarioBox->show();
        mxActions->hide();
    }
    applyFilter();
}

IMPL_LINK( BackingWindow, ClickHdl, weld::Button&, rButton, void )
{
    // dispatch the appropriate URL and end the dialog
    if( &rButton == mxWriterAllButton.get() )
        dispatchURL( u"private:factory/swriter"_ustr );
    else if( &rButton == mxCalcAllButton.get() )
        dispatchURL( u"private:factory/scalc"_ustr );
    else if( &rButton == mxImpressAllButton.get() )
        dispatchURL( u"private:factory/simpress?slot=6686"_ustr );
    else if( &rButton == mxDrawAllButton.get() )
    {
        // Productized as 「打开 PDF」: system open dialog; type detection routes
        // PDF into Draw import (not a full Acrobat editor — honest L1–L2 surface).
        Reference< XDispatchProvider > xFrame( mxFrame, UNO_QUERY );
        dispatchURL( u".uno:Open"_ustr, OUString(), xFrame,
                     { comphelper::makePropertyValue(u"Referer"_ustr, u"private:user"_ustr) } );
    }
    else if( &rButton == mxDBAllButton.get() )
        dispatchURL( u"private:factory/sdatabase?Interactive"_ustr );
    else if( &rButton == mxMathAllButton.get() )
        dispatchURL( u"private:factory/smath"_ustr );
    else if( &rButton == mxOpenButton.get() )
    {
        Reference< XDispatchProvider > xFrame( mxFrame, UNO_QUERY );

        dispatchURL( u".uno:Open"_ustr, OUString(), xFrame, { comphelper::makePropertyValue(u"Referer"_ustr, u"private:user"_ustr) } );
    }
    else if( &rButton == mxRemoteButton.get() )
    {
        Reference< XDispatchProvider > xFrame( mxFrame, UNO_QUERY );

        dispatchURL( u".uno:OpenRemote"_ustr, OUString(), xFrame, {} );
    }
}

IMPL_LINK (BackingWindow, MenuSelectHdl, const OUString&, rId, void)
{
    if (rId == "clear_all")
    {
        SvtHistoryOptions::Clear(EHistoryType::PickList, false);
        ensureRecentThumbnails();
        if (mxAllRecentThumbnails)
            mxAllRecentThumbnails->Reload();
        return;
    }
    else if(rId == "clear_unavailable")
    {
        ensureRecentThumbnails();
        if (mxAllRecentThumbnails)
            mxAllRecentThumbnails->clearUnavailableFiles();
    }
}

namespace
{
/** Normalize template location to a file URL for dispatch, if the file exists. */
OUString lcl_existingTemplateFileURL(const OUString& rPathOrUrl)
{
    if (rPathOrUrl.isEmpty())
        return {};

    OUString aUrl = rPathOrUrl;
    if (!aUrl.startsWith("file://"))
    {
        OUString aConverted;
        if (osl::FileBase::getFileURLFromSystemPath(aUrl, aConverted) == osl::FileBase::E_None
            && !aConverted.isEmpty())
            aUrl = aConverted;
    }

    if (comphelper::DirectoryHelper::fileExists(aUrl))
        return aUrl;
    return {};
}

OUString lcl_brandCommonTemplateURL(std::u16string_view rRelativeUnderCommon)
{
    if (rRelativeUnderCommon.empty())
        return {};

    // macOS: BRAND_SHARE_SUBDIR=Resources; other OS: share via LIBO_SHARE_FOLDER
    OUString aBrandPath = u"$BRAND_BASE_DIR/"_ustr + OUString::createFromAscii(LIBO_SHARE_FOLDER)
                          + u"/template/common/"_ustr + OUString(rRelativeUnderCommon);
    rtl::Bootstrap::expandMacros(aBrandPath);
    return lcl_existingTemplateFileURL(aBrandPath);
}
}

IMPL_LINK(BackingWindow, CreateContextMenuHdl, TemplateViewItem*, pItem, void)
{
    if (!pItem || !mxLocalView)
        return;

    bool bIsInternal = TemplateLocalView::IsInternalTemplate(pItem->getPath());

    mxLocalView->createContextMenu(bIsInternal);

}

IMPL_LINK(BackingWindow, OpenTemplateHdl, const OUString&, rTemplatePath, void)
{
    const OUString aUrl = lcl_existingTemplateFileURL(rTemplatePath);
    if (aUrl.isEmpty())
    {
        SAL_WARN("sfx", "OpenTemplateHdl: template file missing: " << rTemplatePath);
        return;
    }

    uno::Sequence< PropertyValue > aArgs{
        comphelper::makePropertyValue(u"AsTemplate"_ustr, true),
        comphelper::makePropertyValue(u"MacroExecutionMode"_ustr, MacroExecMode::USE_CONFIG),
        comphelper::makePropertyValue(u"UpdateDocMode"_ustr, UpdateDocMode::ACCORDING_TO_CONFIG),
        comphelper::makePropertyValue(u"InteractionHandler"_ustr, task::InteractionHandler::createWithParent( ::comphelper::getProcessComponentContext(), nullptr ))
    };

    Reference< XDispatchProvider > xFrame( mxFrame, UNO_QUERY );

    try
    {
        dispatchURL(aUrl, u"_default"_ustr, xFrame, aArgs);
    }
    catch( const uno::Exception& )
    {
        TOOLS_WARN_EXCEPTION("sfx", "OpenTemplateHdl dispatch failed: " << aUrl);
    }
}

IMPL_LINK(BackingWindow, EditTemplateHdl, const OUString&, rTemplatePath, void)
{
    uno::Sequence< PropertyValue > aArgs{
        comphelper::makePropertyValue(u"AsTemplate"_ustr, false),
        comphelper::makePropertyValue(u"MacroExecutionMode"_ustr, MacroExecMode::USE_CONFIG),
        comphelper::makePropertyValue(u"UpdateDocMode"_ustr, UpdateDocMode::ACCORDING_TO_CONFIG),
    };

    Reference< XDispatchProvider > xFrame( mxFrame, UNO_QUERY );

    try
    {
        dispatchURL(rTemplatePath, u"_default"_ustr, xFrame, aArgs);
    }
    catch( const uno::Exception& )
    {
    }
}

void BackingWindow::showTemplateHub(FILTER_APPLICATION eFilter)
{
    mxTemplateButton->set_active(true);
    ToggleHdl(*mxTemplateButton);
    mxFilter->set_active(lclGetFilterIndex(eFilter));
    applyFilter();
    refreshTemplateMarket();
    if (mxScenarioFallbackHint)
        mxScenarioFallbackHint->show();
}

namespace
{
struct TemplateMarketEntry
{
    OUString category;
    OUString title;
    OUString typeLabel;
    OUString description;
    OUString relPath; // under template/common/ ; empty => AI blank
    OUString aiScenarioId; // non-empty => open AI draft path
    FILTER_APPLICATION filter = FILTER_APPLICATION::NONE;
};

// Office catalog: Feishu-like categories, only real ODF templates or AI blank+prefill.
const std::vector<TemplateMarketEntry>& lcl_templateMarketCatalog()
{
    static const std::vector<TemplateMarketEntry> kCatalog = {
        // 推荐 / 热门 — core CN office
        { u"推荐"_ustr, u"工作汇报"_ustr, u"文字"_ustr,
          u"结构化周报/月报，开箱即写关键结论与进展。"_ustr, u"offimisc/Work_Report_CN.ott"_ustr,
          {}, FILTER_APPLICATION::WRITER },
        { u"推荐"_ustr, u"会议纪要"_ustr, u"文字"_ustr,
          u"议题、决议、待办一页齐，适合会后 10 分钟沉淀。"_ustr,
          u"offimisc/Meeting_Minutes_CN.ott"_ustr, {}, FILTER_APPLICATION::WRITER },
        { u"推荐"_ustr, u"商务路演"_ustr, u"演示"_ustr,
          u"中文商务路演骨架：问题、方案、节奏、下一步。"_ustr, u"presnt/Business_Pitch_CN.otp"_ustr,
          {}, FILTER_APPLICATION::IMPRESS },
        { u"推荐"_ustr, u"预算总览"_ustr, u"表格"_ustr,
          u"科目与金额清晰，适合部门预算与复盘。"_ustr, u"spreadsheets/Budget_CN.ots"_ustr, {},
          FILTER_APPLICATION::CALC },
        { u"热门"_ustr, u"销售跟进"_ustr, u"表格"_ustr,
          u"客户与商机台账，掌握推进阶段（表格，非 CRM 系统）。"_ustr,
          u"spreadsheets/Sales_Tracker_CN.ots"_ustr, {}, FILTER_APPLICATION::CALC },
        { u"热门"_ustr, u"项目排期"_ustr, u"表格"_ustr,
          u"里程碑与责任人一目了然，替代口头排期。"_ustr,
          u"spreadsheets/Project_Schedule_CN.ots"_ustr, {}, FILTER_APPLICATION::CALC },
        { u"热门"_ustr, u"项目汇报"_ustr, u"演示"_ustr,
          u"阶段成果与风险汇报，适合周会/月会。"_ustr, u"presnt/Project_Report_CN.otp"_ustr, {},
          FILTER_APPLICATION::IMPRESS },
        { u"热门"_ustr, u"通知"_ustr, u"文字"_ustr,
          u"规范通知体例，发文更正式。"_ustr, u"officorr/Notice_CN.ott"_ustr, {},
          FILTER_APPLICATION::WRITER },
        { u"推荐"_ustr, u"费用报销 · AI"_ustr, u"表格·AI"_ustr,
          u"高频：本地报销台账，AI 生成表头与示例。"_ustr, {}, u"biz-expense"_ustr,
          FILTER_APPLICATION::CALC },
        { u"推荐"_ustr, u"客户商机 · AI"_ustr, u"表格·AI"_ustr,
          u"高频：客户与商机本地台账（非 CRM）。"_ustr, {}, u"biz-crm-leads"_ustr,
          FILTER_APPLICATION::CALC },
        { u"热门"_ustr, u"待办清单 · AI"_ustr, u"表格·AI"_ustr,
          u"AI 生成待办台账骨架。"_ustr, {}, u"biz-todo-board"_ustr,
          FILTER_APPLICATION::CALC },
        { u"热门"_ustr, u"团队周报 · AI"_ustr, u"文字·AI"_ustr,
          u"AI 生成周报结构，批准后写回。"_ustr, {}, u"biz-weekly-report"_ustr,
          FILTER_APPLICATION::WRITER },
        // AI 场景 — blank + 办公文档骨架（LLM 生成本地 Office 模板，非搭建在线业务系统）
        { u"AI 场景"_ustr, u"空白文档 + AI"_ustr, u"文字·AI"_ustr,
          u"打开空白办公文档并打开可圈 AI，预填起草意图（批准后写回；非业务系统）。"_ustr, {},
          u"blank-draft-writer"_ustr, FILTER_APPLICATION::WRITER },
        { u"AI 场景"_ustr, u"空白表格 + AI"_ustr, u"表格·AI"_ustr,
          u"打开空白表格并预填分析/建表意图。"_ustr, {}, u"blank-draft-calc"_ustr,
          FILTER_APPLICATION::CALC },
        { u"AI 场景"_ustr, u"空白演示 + AI"_ustr, u"演示·AI"_ustr,
          u"打开空白演示并进入设计流（大纲→多方案→选一写回）。"_ustr, {}, u"blank-draft-impress"_ustr,
          FILTER_APPLICATION::IMPRESS },
        { u"AI 场景"_ustr, u"团队工作日报"_ustr, u"文字·AI"_ustr,
          u"AI 生成日报骨架：完成/风险/明日计划（批准后写回）。"_ustr, {},
          u"biz-daily-report"_ustr, FILTER_APPLICATION::WRITER },
        { u"AI 场景"_ustr, u"团队周报"_ustr, u"文字·AI"_ustr,
          u"AI 生成周报结构：目标、进展、数据、问题与支持。"_ustr, {},
          u"biz-weekly-report"_ustr, FILTER_APPLICATION::WRITER },
        { u"AI 场景"_ustr, u"AI 待办清单"_ustr, u"表格·AI"_ustr,
          u"本地待办台账：优先级、负责人、状态、截止日。"_ustr, {}, u"biz-todo-board"_ustr,
          FILTER_APPLICATION::CALC },
        { u"AI 场景"_ustr, u"项目任务清单"_ustr, u"表格·AI"_ustr,
          u"项目任务与进度本地表，非项目管理系统。"_ustr, {}, u"biz-project-tasks"_ustr,
          FILTER_APPLICATION::CALC },
        { u"AI 场景"_ustr, u"费用报销单"_ustr, u"表格·AI"_ustr,
          u"报销台账表头+示例行，本地填写。"_ustr, {}, u"biz-expense"_ustr,
          FILTER_APPLICATION::CALC },
        { u"AI 场景"_ustr, u"请假申请"_ustr, u"文字·AI"_ustr,
          u"请假申请单字段骨架，导出 PDF 前人工确认。"_ustr, {}, u"biz-leave"_ustr,
          FILTER_APPLICATION::WRITER },
        { u"AI 场景"_ustr, u"客户商机台账"_ustr, u"表格·AI"_ustr,
          u"客户/商机阶段本地台账（非 CRM）。"_ustr, {}, u"biz-crm-leads"_ustr,
          FILTER_APPLICATION::CALC },
        { u"AI 场景"_ustr, u"订单台账"_ustr, u"表格·AI"_ustr,
          u"订单号/金额/回款状态本地表。"_ustr, {}, u"biz-orders"_ustr,
          FILTER_APPLICATION::CALC },
        { u"AI 场景"_ustr, u"库存进出台账"_ustr, u"表格·AI"_ustr,
          u"商品+流水双表结构说明与示例（非 WMS）。"_ustr, {}, u"biz-inventory"_ustr,
          FILTER_APPLICATION::CALC },
        { u"AI 场景"_ustr, u"合同台账"_ustr, u"表格·AI"_ustr,
          u"合同编号、金额、到期提醒本地表。"_ustr, {}, u"biz-contracts"_ustr,
          FILTER_APPLICATION::CALC },
        { u"AI 场景"_ustr, u"报价单"_ustr, u"文字·AI"_ustr,
          u"报价正文：明细、合计、付款与签章区。"_ustr, {}, u"biz-quote"_ustr,
          FILTER_APPLICATION::WRITER },
        { u"AI 场景"_ustr, u"考勤与请假台账"_ustr, u"表格·AI"_ustr,
          u"出勤/迟到/请假本地登记表。"_ustr, {}, u"biz-attendance"_ustr,
          FILTER_APPLICATION::CALC },
        { u"AI 场景"_ustr, u"招聘进度表"_ustr, u"表格·AI"_ustr,
          u"岗位候选人阶段跟踪（本地表）。"_ustr, {}, u"biz-recruit"_ustr,
          FILTER_APPLICATION::CALC },
        { u"AI 场景"_ustr, u"OKR 进度表"_ustr, u"表格·AI"_ustr,
          u"目标与关键结果进度本地表。"_ustr, {}, u"biz-okr"_ustr,
          FILTER_APPLICATION::CALC },
        { u"AI 场景"_ustr, u"活动签到表"_ustr, u"文字·AI"_ustr,
          u"活动说明 + 签到表头，可打印。"_ustr, {}, u"biz-meeting-signup"_ustr,
          FILTER_APPLICATION::WRITER },
        { u"AI 场景"_ustr, u"满意度调研表"_ustr, u"表格·AI"_ustr,
          u"评分与反馈收集本地表。"_ustr, {}, u"biz-survey"_ustr,
          FILTER_APPLICATION::CALC },
        { u"AI 场景"_ustr, u"固定资产台账"_ustr, u"表格·AI"_ustr,
          u"资产编号/使用人/状态本地表。"_ustr, {}, u"biz-assets"_ustr,
          FILTER_APPLICATION::CALC },
        { u"AI 场景"_ustr, u"发票与费用台账"_ustr, u"表格·AI"_ustr,
          u"发票登记本地表（无税控验真）。"_ustr, {}, u"biz-invoice"_ustr,
          FILTER_APPLICATION::CALC },
        { u"AI 场景"_ustr, u"工单台账"_ustr, u"表格·AI"_ustr,
          u"工单处理进度本地表（非自动派单）。"_ustr, {}, u"biz-tickets"_ustr,
          FILTER_APPLICATION::CALC },
        { u"AI 场景"_ustr, u"购销合同正文"_ustr, u"文字·AI"_ustr,
          u"合同正文骨架，法律条款需法务审定。"_ustr, {}, u"biz-contract-body"_ustr,
          FILTER_APPLICATION::WRITER },
        { u"AI 场景"_ustr, u"经营复盘演示"_ustr, u"演示·AI"_ustr,
          u"6–10 页经营复盘大纲，批准后写回幻灯。"_ustr, {}, u"biz-ops-review"_ustr,
          FILTER_APPLICATION::IMPRESS },
        { u"AI 场景"_ustr, u"每日销售额汇总"_ustr, u"表格·AI"_ustr,
          u"日销售汇总与合计公式。"_ustr, {}, u"biz-sales-daily"_ustr,
          FILTER_APPLICATION::CALC },
        { u"AI 场景"_ustr, u"会员信息表"_ustr, u"表格·AI"_ustr,
          u"会员等级与积分本地表。"_ustr, {}, u"biz-member"_ustr,
          FILTER_APPLICATION::CALC },
        { u"AI 场景"_ustr, u"月度个人计划"_ustr, u"文字·AI"_ustr,
          u"月目标与周拆解骨架。"_ustr, {}, u"biz-personal-plan"_ustr,
          FILTER_APPLICATION::WRITER },
        { u"AI 场景"_ustr, u"个人记账"_ustr, u"表格·AI"_ustr,
          u"收支类别本地记账表。"_ustr, {}, u"biz-personal-ledger"_ustr,
          FILTER_APPLICATION::CALC },
        { u"AI 场景"_ustr, u"读书笔记"_ustr, u"文字·AI"_ustr,
          u"观点/金句/行动项笔记结构。"_ustr, {}, u"biz-reading-notes"_ustr,
          FILTER_APPLICATION::WRITER },
        { u"AI 场景"_ustr, u"需求与缺陷清单"_ustr, u"表格·AI"_ustr,
          u"需求/缺陷跟踪本地表。"_ustr, {}, u"biz-bug-list"_ustr,
          FILTER_APPLICATION::CALC },
        { u"AI 场景"_ustr, u"证明开具底稿"_ustr, u"文字·AI"_ustr,
          u"在职/收入证明底稿，内容须人工核实。"_ustr, {},
          u"biz-offer-letter-stub"_ustr, FILTER_APPLICATION::WRITER },
        { u"AI 场景"_ustr, u"演示提纲"_ustr, u"文字"_ustr,
          u"先写大纲再导出演示，对标「大纲→成片」成熟路径。"_ustr,
          u"offimisc/PPT_Outline_CN.ott"_ustr, {}, FILTER_APPLICATION::WRITER },
        // 信息收集
        { u"信息收集"_ustr, u"会议纪要（可作签到底稿）"_ustr, u"文字"_ustr,
          u"收集决议与参会信息；复杂在线问卷请用外部表单工具。"_ustr,
          u"offimisc/Meeting_Minutes_CN.ott"_ustr, {}, FILTER_APPLICATION::WRITER },
        { u"信息收集"_ustr, u"简历 / CV"_ustr, u"文字"_ustr,
          u"个人信息结构化填写，适合招聘收集。"_ustr, u"personal/CV.ott"_ustr, {},
          FILTER_APPLICATION::WRITER },
        { u"信息收集"_ustr, u"活动签到表 · AI"_ustr, u"文字·AI"_ustr,
          u"AI 生成签到表说明与表头。"_ustr, {}, u"biz-meeting-signup"_ustr,
          FILTER_APPLICATION::WRITER },
        { u"信息收集"_ustr, u"满意度调研表 · AI"_ustr, u"表格·AI"_ustr,
          u"AI 生成调研列与示例行。"_ustr, {}, u"biz-survey"_ustr,
          FILTER_APPLICATION::CALC },
        // 数据分析
        { u"数据分析"_ustr, u"预算总览"_ustr, u"表格"_ustr,
          u"经营科目汇总，辅助收支分析。"_ustr, u"spreadsheets/Budget_CN.ots"_ustr, {},
          FILTER_APPLICATION::CALC },
        { u"数据分析"_ustr, u"销售跟进"_ustr, u"表格"_ustr,
          u"漏斗与阶段数据沉淀（本地表格）。"_ustr, u"spreadsheets/Sales_Tracker_CN.ots"_ustr, {},
          FILTER_APPLICATION::CALC },
        { u"数据分析"_ustr, u"项目排期"_ustr, u"表格"_ustr,
          u"进度可视化基础表，可再做数据透视。"_ustr,
          u"spreadsheets/Project_Schedule_CN.ots"_ustr, {}, FILTER_APPLICATION::CALC },
        { u"数据分析"_ustr, u"每日销售额 · AI"_ustr, u"表格·AI"_ustr,
          u"AI 生成日销售汇总表与合计公式。"_ustr, {}, u"biz-sales-daily"_ustr,
          FILTER_APPLICATION::CALC },
        { u"数据分析"_ustr, u"OKR 进度 · AI"_ustr, u"表格·AI"_ustr,
          u"AI 生成目标与 KR 进度表。"_ustr, {}, u"biz-okr"_ustr,
          FILTER_APPLICATION::CALC },
        { u"数据分析"_ustr, u"经营复盘演示 · AI"_ustr, u"演示·AI"_ustr,
          u"AI 生成复盘演示大纲。"_ustr, {}, u"biz-ops-review"_ustr,
          FILTER_APPLICATION::IMPRESS },
        // 销售管理
        { u"销售管理"_ustr, u"销售跟进"_ustr, u"表格"_ustr,
          u"客户·商机·阶段本地台账。"_ustr, u"spreadsheets/Sales_Tracker_CN.ots"_ustr, {},
          FILTER_APPLICATION::CALC },
        { u"销售管理"_ustr, u"商务路演"_ustr, u"演示"_ustr,
          u"对客提案与路演演示。"_ustr, u"presnt/Business_Pitch_CN.otp"_ustr, {},
          FILTER_APPLICATION::IMPRESS },
        { u"销售管理"_ustr, u"客户商机 · AI"_ustr, u"表格·AI"_ustr,
          u"AI 生成客户/商机台账（非 CRM）。"_ustr, {}, u"biz-crm-leads"_ustr,
          FILTER_APPLICATION::CALC },
        { u"销售管理"_ustr, u"订单台账 · AI"_ustr, u"表格·AI"_ustr,
          u"AI 生成订单与回款本地表。"_ustr, {}, u"biz-orders"_ustr,
          FILTER_APPLICATION::CALC },
        { u"销售管理"_ustr, u"报价单 · AI"_ustr, u"文字·AI"_ustr,
          u"AI 生成报价正文模板。"_ustr, {}, u"biz-quote"_ustr,
          FILTER_APPLICATION::WRITER },
        { u"销售管理"_ustr, u"合同台账 · AI"_ustr, u"表格·AI"_ustr,
          u"AI 生成合同到期提醒表。"_ustr, {}, u"biz-contracts"_ustr,
          FILTER_APPLICATION::CALC },
        { u"销售管理"_ustr, u"会员信息 · AI"_ustr, u"表格·AI"_ustr,
          u"AI 生成会员本地表。"_ustr, {}, u"biz-member"_ustr,
          FILTER_APPLICATION::CALC },
        // 行政财务
        { u"行政财务"_ustr, u"通知"_ustr, u"文字"_ustr, u"行政发文。"_ustr,
          u"officorr/Notice_CN.ott"_ustr, {}, FILTER_APPLICATION::WRITER },
        { u"行政财务"_ustr, u"商务信函"_ustr, u"文字"_ustr,
          u"正式商务信函版式。"_ustr, u"officorr/Modern_business_letter_sans_serif.ott"_ustr, {},
          FILTER_APPLICATION::WRITER },
        { u"行政财务"_ustr, u"预算总览"_ustr, u"表格"_ustr, u"预算科目管理。"_ustr,
          u"spreadsheets/Budget_CN.ots"_ustr, {}, FILTER_APPLICATION::CALC },
        { u"行政财务"_ustr, u"费用报销 · AI"_ustr, u"表格·AI"_ustr,
          u"AI 生成报销台账。"_ustr, {}, u"biz-expense"_ustr, FILTER_APPLICATION::CALC },
        { u"行政财务"_ustr, u"发票台账 · AI"_ustr, u"表格·AI"_ustr,
          u"AI 生成发票登记表（无验真）。"_ustr, {}, u"biz-invoice"_ustr,
          FILTER_APPLICATION::CALC },
        { u"行政财务"_ustr, u"固定资产 · AI"_ustr, u"表格·AI"_ustr,
          u"AI 生成固定资产本地表。"_ustr, {}, u"biz-assets"_ustr,
          FILTER_APPLICATION::CALC },
        { u"行政财务"_ustr, u"库存进出 · AI"_ustr, u"表格·AI"_ustr,
          u"AI 生成库存基础台账（非 WMS）。"_ustr, {}, u"biz-inventory"_ustr,
          FILTER_APPLICATION::CALC },
        { u"行政财务"_ustr, u"购销合同 · AI"_ustr, u"文字·AI"_ustr,
          u"AI 生成合同正文骨架。"_ustr, {}, u"biz-contract-body"_ustr,
          FILTER_APPLICATION::WRITER },
        // 人力资源
        { u"人力资源"_ustr, u"简历 / CV"_ustr, u"文字"_ustr, u"候选人信息模板。"_ustr,
          u"personal/CV.ott"_ustr, {}, FILTER_APPLICATION::WRITER },
        { u"人力资源"_ustr, u"教学课件"_ustr, u"演示"_ustr,
          u"培训与入职讲解演示。"_ustr, u"presnt/Teaching_Courseware_CN.otp"_ustr, {},
          FILTER_APPLICATION::IMPRESS },
        { u"人力资源"_ustr, u"请假申请 · AI"_ustr, u"文字·AI"_ustr,
          u"AI 生成请假申请单。"_ustr, {}, u"biz-leave"_ustr,
          FILTER_APPLICATION::WRITER },
        { u"人力资源"_ustr, u"考勤台账 · AI"_ustr, u"表格·AI"_ustr,
          u"AI 生成考勤/请假登记表。"_ustr, {}, u"biz-attendance"_ustr,
          FILTER_APPLICATION::CALC },
        { u"人力资源"_ustr, u"招聘进度 · AI"_ustr, u"表格·AI"_ustr,
          u"AI 生成招聘阶段跟踪表。"_ustr, {}, u"biz-recruit"_ustr,
          FILTER_APPLICATION::CALC },
        { u"人力资源"_ustr, u"证明底稿 · AI"_ustr, u"文字·AI"_ustr,
          u"AI 生成在职/收入证明底稿。"_ustr, {}, u"biz-offer-letter-stub"_ustr,
          FILTER_APPLICATION::WRITER },
        // 项目管理
        { u"项目管理"_ustr, u"项目方案"_ustr, u"文字"_ustr,
          u"目标、范围、计划与风险。"_ustr, u"offimisc/Project_Plan_CN.ott"_ustr, {},
          FILTER_APPLICATION::WRITER },
        { u"项目管理"_ustr, u"项目排期"_ustr, u"表格"_ustr, u"任务与节点。"_ustr,
          u"spreadsheets/Project_Schedule_CN.ots"_ustr, {}, FILTER_APPLICATION::CALC },
        { u"项目管理"_ustr, u"项目汇报"_ustr, u"演示"_ustr, u"阶段汇报。"_ustr,
          u"presnt/Project_Report_CN.otp"_ustr, {}, FILTER_APPLICATION::IMPRESS },
        { u"项目管理"_ustr, u"项目任务 · AI"_ustr, u"表格·AI"_ustr,
          u"AI 生成任务与依赖表。"_ustr, {}, u"biz-project-tasks"_ustr,
          FILTER_APPLICATION::CALC },
        { u"项目管理"_ustr, u"待办清单 · AI"_ustr, u"表格·AI"_ustr,
          u"AI 生成待办台账。"_ustr, {}, u"biz-todo-board"_ustr,
          FILTER_APPLICATION::CALC },
        { u"项目管理"_ustr, u"工单台账 · AI"_ustr, u"表格·AI"_ustr,
          u"AI 生成工单进度表。"_ustr, {}, u"biz-tickets"_ustr,
          FILTER_APPLICATION::CALC },
        { u"项目管理"_ustr, u"缺陷清单 · AI"_ustr, u"表格·AI"_ustr,
          u"AI 生成需求/缺陷跟踪表。"_ustr, {}, u"biz-bug-list"_ustr,
          FILTER_APPLICATION::CALC },
        // 协作效率
        { u"协作效率"_ustr, u"工作汇报"_ustr, u"文字"_ustr, u"周报月报。"_ustr,
          u"offimisc/Work_Report_CN.ott"_ustr, {}, FILTER_APPLICATION::WRITER },
        { u"协作效率"_ustr, u"会议纪要"_ustr, u"文字"_ustr, u"会后沉淀。"_ustr,
          u"offimisc/Meeting_Minutes_CN.ott"_ustr, {}, FILTER_APPLICATION::WRITER },
        { u"协作效率"_ustr, u"团队日报 · AI"_ustr, u"文字·AI"_ustr,
          u"AI 生成团队日报骨架。"_ustr, {}, u"biz-daily-report"_ustr,
          FILTER_APPLICATION::WRITER },
        { u"协作效率"_ustr, u"团队周报 · AI"_ustr, u"文字·AI"_ustr,
          u"AI 生成团队周报结构。"_ustr, {}, u"biz-weekly-report"_ustr,
          FILTER_APPLICATION::WRITER },
        // 个人成长
        { u"个人成长"_ustr, u"简历"_ustr, u"文字"_ustr, u"个人履历。"_ustr,
          u"personal/Resume1page.ott"_ustr, {}, FILTER_APPLICATION::WRITER },
        { u"个人成长"_ustr, u"简约演示 Focus"_ustr, u"演示"_ustr,
          u"冷静商务演示主题。"_ustr, u"presnt/Focus.otp"_ustr, {},
          FILTER_APPLICATION::IMPRESS },
        { u"个人成长"_ustr, u"月度计划 · AI"_ustr, u"文字·AI"_ustr,
          u"AI 生成月度个人计划。"_ustr, {}, u"biz-personal-plan"_ustr,
          FILTER_APPLICATION::WRITER },
        { u"个人成长"_ustr, u"个人记账 · AI"_ustr, u"表格·AI"_ustr,
          u"AI 生成收支记账表。"_ustr, {}, u"biz-personal-ledger"_ustr,
          FILTER_APPLICATION::CALC },
        { u"个人成长"_ustr, u"读书笔记 · AI"_ustr, u"文字·AI"_ustr,
          u"AI 生成读书笔记结构。"_ustr, {}, u"biz-reading-notes"_ustr,
          FILTER_APPLICATION::WRITER },
        // 演示文稿
        { u"演示文稿"_ustr, u"Metropolis"_ustr, u"演示"_ustr, u"现代都市风主题。"_ustr,
          u"presnt/Metropolis.otp"_ustr, {}, FILTER_APPLICATION::IMPRESS },
        { u"演示文稿"_ustr, u"Grey Elegant"_ustr, u"演示"_ustr, u"灰调商务。"_ustr,
          u"presnt/Grey_Elegant.otp"_ustr, {}, FILTER_APPLICATION::IMPRESS },
        { u"演示文稿"_ustr, u"Progress"_ustr, u"演示"_ustr, u"进度表达。"_ustr,
          u"presnt/Progress.otp"_ustr, {}, FILTER_APPLICATION::IMPRESS },
        { u"演示文稿"_ustr, u"Portfolio"_ustr, u"演示"_ustr, u"作品集展示。"_ustr,
          u"presnt/Portfolio.otp"_ustr, {}, FILTER_APPLICATION::IMPRESS },
        { u"演示文稿"_ustr, u"经营复盘 · AI"_ustr, u"演示·AI"_ustr,
          u"AI 生成经营复盘页大纲。"_ustr, {}, u"biz-ops-review"_ustr,
          FILTER_APPLICATION::IMPRESS },
        // 公文信函
        { u"公文信函"_ustr, u"通知"_ustr, u"文字"_ustr, u"规范通知。"_ustr,
          u"officorr/Notice_CN.ott"_ustr, {}, FILTER_APPLICATION::WRITER },
        { u"公文信函"_ustr, u"商务信函（无衬线）"_ustr, u"文字"_ustr,
          u"现代商务信。"_ustr, u"officorr/Modern_business_letter_sans_serif.ott"_ustr, {},
          FILTER_APPLICATION::WRITER },
        { u"公文信函"_ustr, u"商务信函（衬线）"_ustr, u"文字"_ustr, u"正式商务信。"_ustr,
          u"officorr/Modern_business_letter_serif.ott"_ustr, {}, FILTER_APPLICATION::WRITER },
        { u"公文信函"_ustr, u"购销合同 · AI"_ustr, u"文字·AI"_ustr,
          u"AI 生成合同正文骨架。"_ustr, {}, u"biz-contract-body"_ustr,
          FILTER_APPLICATION::WRITER },
        { u"公文信函"_ustr, u"证明底稿 · AI"_ustr, u"文字·AI"_ustr,
          u"AI 生成证明开具底稿。"_ustr, {}, u"biz-offer-letter-stub"_ustr,
          FILTER_APPLICATION::WRITER },
        // PDF — L1–L2 product tools (open / info / guided merge-split) + export templates.
        // Honest surface: 非完整 Acrobat（无 OCR / 高级表单设计 / 产品级数字签章）。
        { u"PDF"_ustr, u"打开 PDF"_ustr, u"工具"_ustr,
          u"导入 PDF 到绘图视图（浏览页、注释级编辑；非完整 Acrobat / 非完整 PDF 编辑器）。"_ustr, {},
          u"tool:pdf-open"_ustr, FILTER_APPLICATION::NONE },
        { u"PDF"_ustr, u"PDF 页数与信息"_ustr, u"工具"_ustr,
          u"本地读取页数（PDFium），不上传云端；非 Acrobat 元数据套件。"_ustr, {}, u"tool:pdf-info"_ustr,
          FILTER_APPLICATION::NONE },
        { u"PDF"_ustr, u"合并 PDF"_ustr, u"工具"_ustr,
          u"多选 PDF → 另存为合并结果（图像页；适合打印分发；非完整 Acrobat 矢量合并）。"_ustr, {},
          u"tool:pdf-merge"_ustr, FILTER_APPLICATION::NONE },
        { u"PDF"_ustr, u"拆分 / 提取 PDF"_ustr, u"工具"_ustr,
          u"按页拆到文件夹或按范围提取（本地图像页；非完整 Acrobat）。"_ustr, {},
          u"tool:pdf-split"_ustr, FILTER_APPLICATION::NONE },
        { u"PDF"_ustr, u"导出当前文档为 PDF"_ustr, u"工具"_ustr,
          u"对已打开文档执行「导出为 PDF」；无文档时打开空白文档并提示（导出能力 ≠ Acrobat 编辑）。"_ustr, {},
          u"tool:pdf-export"_ustr, FILTER_APPLICATION::NONE },
        { u"PDF"_ustr, u"工作汇报 → 导出 PDF"_ustr, u"文字"_ustr,
          u"写完后用「导出为 PDF」；默认导出书签与标签（中文友好）。"_ustr,
          u"offimisc/Work_Report_CN.ott"_ustr, {}, FILTER_APPLICATION::WRITER },
        { u"PDF"_ustr, u"会议纪要 → 导出 PDF"_ustr, u"文字"_ustr,
          u"会后定稿再导出 PDF 分发。"_ustr, u"offimisc/Meeting_Minutes_CN.ott"_ustr, {},
          FILTER_APPLICATION::WRITER },
        { u"PDF"_ustr, u"项目方案 → 导出 PDF"_ustr, u"文字"_ustr,
          u"方案正文模板；导出 PDF 前检查字体嵌入预览。"_ustr,
          u"offimisc/Project_Plan_CN.ott"_ustr, {}, FILTER_APPLICATION::WRITER },
        { u"PDF"_ustr, u"商务路演 → 导出 PDF"_ustr, u"演示"_ustr,
          u"演示导出 PDF 便于无 Office 环境阅读。"_ustr, u"presnt/Business_Pitch_CN.otp"_ustr,
          {}, FILTER_APPLICATION::IMPRESS },
        { u"PDF"_ustr, u"预算总览 → 导出 PDF"_ustr, u"表格"_ustr,
          u"表格定稿后导出 PDF（可单表一页，见导出选项）。"_ustr,
          u"spreadsheets/Budget_CN.ots"_ustr, {}, FILTER_APPLICATION::CALC },
        { u"PDF"_ustr, u"购销合同 · AI 再导出"_ustr, u"文字·AI"_ustr,
          u"AI 生成合同骨架 → 审定 → 导出 PDF（非电子签章系统）。"_ustr, {},
          u"biz-contract-body"_ustr, FILTER_APPLICATION::WRITER },
        { u"PDF"_ustr, u"报价单 · AI 再导出"_ustr, u"文字·AI"_ustr,
          u"AI 生成报价 → 批准写回 → 导出 PDF。"_ustr, {}, u"biz-quote"_ustr,
          FILTER_APPLICATION::WRITER },
        { u"PDF"_ustr, u"通知 → 导出 PDF"_ustr, u"文字"_ustr,
          u"正式通知体例，导出 PDF 归档。"_ustr, u"officorr/Notice_CN.ott"_ustr, {},
          FILTER_APPLICATION::WRITER },
        { u"推荐"_ustr, u"打开 PDF"_ustr, u"工具"_ustr,
          u"开始中心第四件套：文字 / 表格 / 演示 / PDF（导入浏览，非完整 Acrobat）。"_ustr, {},
          u"tool:pdf-open"_ustr, FILTER_APPLICATION::NONE },
        { u"热门"_ustr, u"合并 PDF"_ustr, u"工具"_ustr,
          u"本地一键合并（图像页；非完整 Acrobat / 非云端矢量引擎）。"_ustr, {},
          u"tool:pdf-merge"_ustr, FILTER_APPLICATION::NONE },
    };
    return kCatalog;
}
}

void BackingWindow::refreshTemplateMarket()
{
    if (!mxTemplateMarketTree)
        return;

    const OUString cat
        = mxTemplateCategory ? mxTemplateCategory->get_active_text() : u"推荐"_ustr;
    const OUString q = mxTemplateSearch ? mxTemplateSearch->get_text().trim().toAsciiLowerCase()
                                        : OUString();

    mxTemplateMarketTree->clear();
    auto appendRow = [&](const TemplateMarketEntry& e) {
        OUString id;
        if (!e.relPath.isEmpty())
            id = u"path:"_ustr + e.relPath;
        else if (e.aiScenarioId.startsWith(u"tool:"_ustr))
            id = e.aiScenarioId; // tool:pdf-open | tool:pdf-merge | …
        else
            id = u"ai:"_ustr + e.aiScenarioId;
        mxTemplateMarketTree->append(id, e.title);
        const int row = mxTemplateMarketTree->n_children() - 1;
        mxTemplateMarketTree->set_text(row, e.typeLabel, 1);
        mxTemplateMarketTree->set_text(row, e.description, 2);
    };

    for (const auto& e : lcl_templateMarketCatalog())
    {
        if (cat != e.category)
            continue;
        if (!q.isEmpty())
        {
            const OUString hay
                = OUString(e.title + e.description + e.typeLabel + e.category).toAsciiLowerCase();
            if (hay.indexOf(q) < 0)
                continue;
        }
        appendRow(e);
    }

    // Search falls back to all categories when current category has no hit.
    if (mxTemplateMarketTree->n_children() == 0 && !q.isEmpty())
    {
        for (const auto& e : lcl_templateMarketCatalog())
        {
            const OUString hay
                = OUString(e.title + e.description + e.typeLabel + e.category).toAsciiLowerCase();
            if (hay.indexOf(q) < 0)
                continue;
            appendRow(e);
        }
    }
}

void BackingWindow::openTemplateMarketSelection()
{
    if (!mxTemplateMarketTree)
        return;
    const int row = mxTemplateMarketTree->get_selected_index();
    if (row < 0)
        return;
    const OUString id = mxTemplateMarketTree->get_id(row);
    if (id.startsWith("path:"))
    {
        openScenarioTemplate(id.copy(5), u"", FILTER_APPLICATION::NONE);
        return;
    }
    if (id.startsWith("tool:"))
    {
        runPdfTool(std::u16string_view(id.getStr() + 5, id.getLength() - 5));
        return;
    }
    if (id.startsWith("ai:"))
    {
        const OUString sid = id.copy(3);
        OUString factory = u"private:factory/swriter"_ustr;
        const auto catalog = kqoffice::ai::chat::DocumentAIScenarioStore::load();
        if (const auto* s = kqoffice::ai::chat::DocumentAIScenarioStore::find(catalog, sid))
        {
            if (s->preferredSurface == u"calc"_ustr || s->category == u"calc"_ustr)
                factory = u"private:factory/scalc"_ustr;
            else if (s->preferredSurface == u"impress"_ustr || s->category == u"impress"_ustr)
                factory = u"private:factory/simpress?slot=6686"_ustr;
        }
        else if (sid.indexOf(u"calc"_ustr) >= 0 || sid.startsWith(u"biz-todo"_ustr)
                 || sid.startsWith(u"biz-project"_ustr) || sid.startsWith(u"biz-expense"_ustr)
                 || sid.startsWith(u"biz-crm"_ustr) || sid.startsWith(u"biz-order"_ustr)
                 || sid.startsWith(u"biz-inventory"_ustr)
                 || sid == u"biz-contracts"_ustr // not biz-contract-body (Writer)
                 || sid.startsWith(u"biz-attendance"_ustr) || sid.startsWith(u"biz-recruit"_ustr)
                 || sid.startsWith(u"biz-okr"_ustr) || sid.startsWith(u"biz-survey"_ustr)
                 || sid.startsWith(u"biz-asset"_ustr) || sid.startsWith(u"biz-invoice"_ustr)
                 || sid.startsWith(u"biz-ticket"_ustr) || sid.startsWith(u"biz-sales"_ustr)
                 || sid.startsWith(u"biz-member"_ustr) || sid.startsWith(u"biz-personal-ledger"_ustr)
                 || sid.startsWith(u"biz-bug"_ustr))
            factory = u"private:factory/scalc"_ustr;
        else if (sid.indexOf(u"impress"_ustr) >= 0 || sid.startsWith(u"biz-ops-review"_ustr))
            factory = u"private:factory/simpress?slot=6686"_ustr;
        openAiDraft(sid, factory);
    }
}

namespace
{
void lcl_showPdfInfo(weld::Widget* pParent, const OUString& rTitle, const OUString& rMsg)
{
    std::unique_ptr<weld::MessageDialog> xBox(Application::CreateMessageDialog(
        pParent, VclMessageType::Info, VclButtonsType::Ok, rMsg));
    if (xBox)
    {
        xBox->set_title(rTitle);
        xBox->run();
    }
}

sal_Int32 lcl_pdfPageCount(const OUString& rPathOrUrl)
{
    OUString aUrl = rPathOrUrl;
    if (!aUrl.startsWith("file:"))
    {
        OUString aFileUrl;
        if (osl::FileBase::getFileURLFromSystemPath(aUrl, aFileUrl) == osl::FileBase::E_None)
            aUrl = aFileUrl;
    }
    std::unique_ptr<SvStream> pStream(
        ::utl::UcbStreamHelper::CreateStream(aUrl, StreamMode::READ));
    if (!pStream || pStream->GetError())
        return -1;
    const sal_uInt64 nSize = pStream->TellEnd();
    if (nSize == 0 || nSize > 200 * 1024 * 1024) // 200MB guard
        return -1;
    pStream->Seek(0);
    std::vector<sal_uInt8> aBuf(static_cast<size_t>(nSize));
    const size_t nRead = pStream->ReadBytes(aBuf.data(), aBuf.size());
    if (nRead != aBuf.size())
        return -1;
    auto pPdfium = vcl::pdf::PDFiumLibrary::get();
    if (!pPdfium)
        return -1;
    std::unique_ptr<vcl::pdf::PDFiumDocument> pDoc
        = pPdfium->openDocument(aBuf.data(), static_cast<int>(aBuf.size()), OString());
    if (!pDoc)
        return -1;
    return pDoc->getPageCount();
}

OUString lcl_pickPdfFile(weld::Window* pParent, bool bMulti, const OUString& rTitle)
{
    sfx2::FileDialogHelper aDlg(css::ui::dialogs::TemplateDescription::FILEOPEN_SIMPLE,
                                bMulti ? FileDialogFlags::MultiSelection : FileDialogFlags::NONE,
                                pParent);
    aDlg.SetTitle(rTitle);
    aDlg.AddFilter(u"PDF (*.pdf)"_ustr, u"*.pdf"_ustr);
    aDlg.SetCurrentFilter(u"PDF (*.pdf)"_ustr);
    if (aDlg.Execute() != ERRCODE_NONE)
        return {};
    return aDlg.GetPath();
}

OUString lcl_toFileUrl(const OUString& rPathOrUrl)
{
    if (rPathOrUrl.startsWith("file:"))
        return rPathOrUrl;
    OUString aFileUrl;
    if (osl::FileBase::getFileURLFromSystemPath(rPathOrUrl, aFileUrl) == osl::FileBase::E_None)
        return aFileUrl;
    return rPathOrUrl;
}

OUString lcl_toSystemPath(const OUString& rPathOrUrl)
{
    if (!rPathOrUrl.startsWith("file:"))
        return rPathOrUrl;
    OUString aSys;
    if (osl::FileBase::getSystemPathFromFileURL(rPathOrUrl, aSys) == osl::FileBase::E_None)
        return aSys;
    return rPathOrUrl;
}

/** Save-as for a single PDF result. Returns file: URL or empty if cancelled. */
OUString lcl_pickSavePdf(weld::Window* pParent, const OUString& rTitle, const OUString& rDefaultName)
{
    sfx2::FileDialogHelper aDlg(css::ui::dialogs::TemplateDescription::FILESAVE_SIMPLE,
                                FileDialogFlags::NONE, pParent);
    aDlg.SetTitle(rTitle);
    aDlg.AddFilter(u"PDF (*.pdf)"_ustr, u"*.pdf"_ustr);
    aDlg.SetCurrentFilter(u"PDF (*.pdf)"_ustr);
    if (!rDefaultName.isEmpty())
        aDlg.SetFileName(rDefaultName);
    if (aDlg.Execute() != ERRCODE_NONE)
        return {};
    OUString aPath = aDlg.GetPath();
    if (aPath.isEmpty())
        return {};
    if (!aPath.endsWithIgnoreAsciiCase(".pdf"))
        aPath += u".pdf"_ustr;
    return lcl_toFileUrl(aPath);
}

/** Folder for multi-file split output. Returns file: URL or empty. */
OUString lcl_pickOutputFolder(const css::uno::Reference<css::uno::XComponentContext>& xCtx,
                              weld::Window* pParent, const OUString& rTitle)
{
    try
    {
        auto xPicker = sfx2::createFolderPicker(xCtx, pParent);
        if (!xPicker.is())
            return {};
        (void)rTitle; // platform folder pickers often ignore custom titles
        if (xPicker->execute() != css::ui::dialogs::ExecutableDialogResults::OK)
            return {};
        return xPicker->getDirectory();
    }
    catch (const css::uno::Exception&)
    {
        return {};
    }
}

OUString lcl_basenameStem(const OUString& rPathOrUrl)
{
    OUString aSys = lcl_toSystemPath(rPathOrUrl);
    sal_Int32 nSlash = std::max(aSys.lastIndexOf(u'/'), aSys.lastIndexOf(u'\\'));
    OUString aName = nSlash >= 0 ? aSys.copy(nSlash + 1) : aSys;
    if (aName.endsWithIgnoreAsciiCase(".pdf"))
        aName = aName.copy(0, aName.getLength() - 4);
    if (aName.isEmpty())
        aName = u"output"_ustr;
    return aName;
}

void lcl_ensureParentDir(const OUString& rFileUrl)
{
    INetURLObject aUrl(rFileUrl);
    if (aUrl.GetProtocol() == INetProtocol::NotValid)
        return;
    aUrl.removeSegment();
    const OUString aDir = aUrl.GetMainURL(INetURLObject::DecodeMechanism::NONE);
    if (!aDir.isEmpty())
        osl::Directory::createPath(aDir);
}

/** Reveal result: macOS highlights the file (`open -R`); else open the folder. */
void lcl_revealInFileManager(const OUString& rFileOrDirUrl, bool bIsDirectory)
{
    try
    {
        OUString aUrl = lcl_toFileUrl(rFileOrDirUrl);
        if (aUrl.isEmpty())
            return;

#if defined(MACOSX) || defined(__APPLE__)
        if (!bIsDirectory)
        {
            // Finder: select and highlight the file.
            const OUString aSys = lcl_toSystemPath(aUrl);
            if (!aSys.isEmpty())
            {
                OUString aOpen(u"open"_ustr);
                OUString aFlag(u"-R"_ustr);
                rtl_uString* pArgs[] = { aFlag.pData, aSys.pData };
                oslProcess hProc = nullptr;
                if (osl_executeProcess(aOpen.pData, pArgs, 2,
                                       osl_Process_DETACHED | osl_Process_SEARCHPATH
                                           | osl_Process_HIDDEN,
                                       nullptr, nullptr, nullptr, 0, &hProc)
                    == osl_Process_E_None)
                {
                    if (hProc)
                        osl_freeProcessHandle(hProc);
                    return;
                }
            }
        }
#endif
        // Folder open (or file → parent folder fallback).
        OUString aTarget = aUrl;
        if (!bIsDirectory)
        {
            INetURLObject aObj(aUrl);
            if (aObj.GetProtocol() == INetProtocol::NotValid)
                return;
            aObj.removeSegment();
            aTarget = aObj.GetMainURL(INetURLObject::DecodeMechanism::NONE);
        }
        if (aTarget.isEmpty())
            return;
        if (!aTarget.startsWith("file:"))
            aTarget = lcl_toFileUrl(aTarget);

        const css::uno::Reference<css::uno::XComponentContext> xCtx
            = ::comphelper::getProcessComponentContext();
        css::uno::Reference<css::system::XSystemShellExecute> xShell(
            css::system::SystemShellExecute::create(xCtx));
        xShell->execute(aTarget, OUString(),
                        css::system::SystemShellExecuteFlags::URIS_ONLY);
    }
    catch (const css::uno::Exception&)
    {
        TOOLS_WARN_EXCEPTION("sfx", "lcl_revealInFileManager");
    }
}

constexpr int kPdfToolDpi = 120;
constexpr sal_Int32 kPdfToolMaxPages = 80;
constexpr sal_uInt64 kPdfToolMaxFileBytes = 80 * 1024 * 1024;

bool lcl_readPdfFile(const OUString& rPathOrUrl, std::vector<sal_uInt8>& rBuf, OUString& rError)
{
    const OUString aUrl = lcl_toFileUrl(rPathOrUrl);
    std::unique_ptr<SvStream> pStream(
        ::utl::UcbStreamHelper::CreateStream(aUrl, StreamMode::READ));
    if (!pStream || pStream->GetError())
    {
        rError = u"无法读取："_ustr + rPathOrUrl;
        return false;
    }
    const sal_uInt64 nSize = pStream->TellEnd();
    if (nSize == 0 || nSize > kPdfToolMaxFileBytes)
    {
        rError = u"文件过大或为空："_ustr + rPathOrUrl;
        return false;
    }
    pStream->Seek(0);
    rBuf.resize(static_cast<size_t>(nSize));
    if (pStream->ReadBytes(rBuf.data(), rBuf.size()) != rBuf.size())
    {
        rError = u"读取失败："_ustr + rPathOrUrl;
        return false;
    }
    return true;
}

using PdfProgressFn = std::function<void(sal_Int32 nDone, sal_Int32 nTotal, const OUString& rStatus)>;

/** Modeless-looking progress dialog (run while parent is busy). */
class PdfProgressDialog final : public weld::GenericDialogController
{
    std::unique_ptr<weld::Label> m_xStatus;
    std::unique_ptr<weld::ProgressBar> m_xBar;

public:
    explicit PdfProgressDialog(weld::Window* pParent, const OUString& rTitle)
        : GenericDialogController(pParent, u"sfx/ui/pdfprogress.ui"_ustr, u"PdfProgressDialog"_ustr)
        , m_xStatus(m_xBuilder->weld_label(u"status_label"_ustr))
        , m_xBar(m_xBuilder->weld_progress_bar(u"progress"_ustr))
    {
        m_xDialog->set_title(rTitle);
        m_xDialog->show();
        if (m_xBar)
            m_xBar->set_percentage(0);
        Application::Reschedule(true);
    }

    void update(sal_Int32 nDone, sal_Int32 nTotal, const OUString& rStatus)
    {
        if (m_xStatus)
            m_xStatus->set_label(rStatus);
        if (m_xBar && nTotal > 0)
        {
            const int nPct = static_cast<int>(
                std::clamp((100.0 * static_cast<double>(nDone)) / static_cast<double>(nTotal), 0.0,
                           100.0));
            m_xBar->set_percentage(nPct);
            m_xBar->set_text(OUString::number(nPct) + u"%"_ustr);
        }
        Application::Reschedule(true);
    }
};

/** Render selected 0-based pages from an open PDFium doc into one PDFWriter file. */
bool lcl_emitPages(vcl::pdf::PDFium& rPdfium, vcl::pdf::PDFiumDocument& rDoc,
                   const std::vector<int>& rPageIndices, vcl::pdf::PDFWriter& rWriter,
                   sal_Int32& rOutPages, OUString& rError, const PdfProgressFn& rProgress = {},
                   sal_Int32 nProgressBase = 0, sal_Int32 nProgressTotal = 0)
{
    const sal_Int32 nLocalTotal = static_cast<sal_Int32>(rPageIndices.size());
    const sal_Int32 nTotal = nProgressTotal > 0 ? nProgressTotal : nLocalTotal;
    sal_Int32 nLocal = 0;
    for (int iPage : rPageIndices)
    {
        if (iPage < 0 || iPage >= rDoc.getPageCount())
            continue;
        if (rOutPages >= kPdfToolMaxPages)
        {
            rError = u"页数超过上限（"_ustr + OUString::number(kPdfToolMaxPages) + u"）。"_ustr;
            return false;
        }
        const basegfx::B2DSize aPt = rDoc.getPageSize(iPage);
        const double nWpt = std::max(1.0, aPt.getWidth());
        const double nHpt = std::max(1.0, aPt.getHeight());
        int nWpx = static_cast<int>(nWpt * kPdfToolDpi / 72.0);
        int nHpx = static_cast<int>(nHpt * kPdfToolDpi / 72.0);
        nWpx = std::clamp(nWpx, 32, 4096);
        nHpx = std::clamp(nHpx, 32, 4096);

        auto pPage = rDoc.openPage(iPage);
        if (!pPage)
            continue;
        std::unique_ptr<vcl::pdf::PDFiumBitmap> pBmp
            = rPdfium.createBitmap(nWpx, nHpx, /*nAlpha*/ 1);
        if (!pBmp)
        {
            rError = u"无法分配渲染缓冲。"_ustr;
            return false;
        }
        pBmp->fillRect(0, 0, nWpx, nHpx, 0xFFFFFFFF);
        pBmp->renderPageBitmap(&rDoc, pPage.get(), 0, 0, nWpx, nHpx);
        const Bitmap aBitmap = pBmp->createBitmapFromBuffer();

        rWriter.NewPage(nWpt, nHpt);
        rWriter.DrawBitmap(Point(0, 0),
                           Size(static_cast<tools::Long>(nWpt + 0.5),
                                static_cast<tools::Long>(nHpt + 0.5)),
                           aBitmap);
        ++rOutPages;
        ++nLocal;
        if (rProgress)
        {
            const sal_Int32 nDone = nProgressBase + nLocal;
            rProgress(nDone, nTotal,
                      u"正在处理第 "_ustr + OUString::number(nDone) + u" / "_ustr
                          + OUString::number(nTotal) + u" 页…"_ustr);
        }
    }
    return true;
}

/** Merge PDFs via PDFium render + PDFWriter (page images) to rOutFileUrl. */
bool lcl_mergePdfsToFile(const css::uno::Sequence<OUString>& rFiles, const OUString& rOutFileUrl,
                         sal_Int32& rOutPages, OUString& rError, const PdfProgressFn& rProgress = {})
{
    rOutPages = 0;
    auto pPdfium = vcl::pdf::PDFiumLibrary::get();
    if (!pPdfium)
    {
        rError = u"PDFium 不可用，无法合并。"_ustr;
        return false;
    }
    if (rOutFileUrl.isEmpty())
    {
        rError = u"未指定输出路径。"_ustr;
        return false;
    }
    lcl_ensureParentDir(rOutFileUrl);
    // Overwrite if exists
    osl::File::remove(rOutFileUrl);

    // Pre-count pages for progress total.
    sal_Int32 nProgressTotal = 0;
    std::vector<std::vector<sal_uInt8>> aFileBufs;
    aFileBufs.reserve(static_cast<size_t>(rFiles.getLength()));
    for (const auto& rFile : rFiles)
    {
        std::vector<sal_uInt8> aBuf;
        if (!lcl_readPdfFile(rFile, aBuf, rError))
            return false;
        std::unique_ptr<vcl::pdf::PDFiumDocument> pDoc
            = pPdfium->openDocument(aBuf.data(), static_cast<int>(aBuf.size()), OString());
        if (!pDoc)
        {
            rError = u"无法解析 PDF（可能加密）："_ustr + rFile;
            return false;
        }
        nProgressTotal += pDoc->getPageCount();
        aFileBufs.push_back(std::move(aBuf));
    }
    if (nProgressTotal <= 0)
    {
        rError = u"没有可合并的页面。"_ustr;
        return false;
    }

    vcl::pdf::PDFWriter::PDFWriterContext aCtx;
    aCtx.URL = rOutFileUrl;
    aCtx.Version = vcl::pdf::PDFWriter::PDFVersion::PDF_1_6;
    vcl::pdf::PDFWriter aWriter(aCtx, css::uno::Reference<css::beans::XMaterialHolder>());

    sal_Int32 nBase = 0;
    for (size_t f = 0; f < aFileBufs.size(); ++f)
    {
        const auto& aBuf = aFileBufs[f];
        std::unique_ptr<vcl::pdf::PDFiumDocument> pDoc
            = pPdfium->openDocument(aBuf.data(), static_cast<int>(aBuf.size()), OString());
        if (!pDoc)
        {
            rError = u"无法解析 PDF（可能加密）："_ustr + rFiles[static_cast<sal_Int32>(f)];
            return false;
        }
        std::vector<int> aAll;
        aAll.reserve(static_cast<size_t>(pDoc->getPageCount()));
        for (int i = 0; i < pDoc->getPageCount(); ++i)
            aAll.push_back(i);
        if (!lcl_emitPages(*pPdfium, *pDoc, aAll, aWriter, rOutPages, rError, rProgress, nBase,
                           nProgressTotal))
            return false;
        nBase += static_cast<sal_Int32>(aAll.size());
    }

    if (rOutPages <= 0)
    {
        rError = u"没有可合并的页面。"_ustr;
        return false;
    }
    if (rProgress)
        rProgress(nProgressTotal, nProgressTotal, u"正在写入文件…"_ustr);
    if (!aWriter.Emit())
    {
        rError = u"写入合并 PDF 失败。"_ustr;
        return false;
    }
    return true;
}

/** Parse "1-3,5,7-8" (1-based) into 0-based page indices; clamps to [0, nPages). */
bool lcl_parsePageRange(const OUString& rSpec, int nPages, std::vector<int>& rOut, OUString& rError)
{
    rOut.clear();
    if (nPages <= 0)
    {
        rError = u"PDF 没有页面。"_ustr;
        return false;
    }
    OUString aSpec = rSpec.trim();
    if (aSpec.isEmpty())
    {
        rError = u"请输入页码范围。"_ustr;
        return false;
    }
    sal_Int32 nIdx = 0;
    while (nIdx >= 0)
    {
        OUString aTok = aSpec.getToken(0, u',', nIdx).trim();
        if (aTok.isEmpty())
            continue;
        const sal_Int32 nDash = aTok.indexOf(u'-');
        int nFrom = 0;
        int nTo = 0;
        if (nDash < 0)
        {
            nFrom = nTo = aTok.toInt32();
        }
        else
        {
            nFrom = aTok.copy(0, nDash).trim().toInt32();
            nTo = aTok.copy(nDash + 1).trim().toInt32();
        }
        if (nFrom <= 0 || nTo <= 0 || nFrom > nTo)
        {
            rError = u"无效范围："_ustr + aTok;
            return false;
        }
        for (int p = nFrom; p <= nTo; ++p)
        {
            if (p > nPages)
                break;
            rOut.push_back(p - 1);
        }
    }
    if (rOut.empty())
    {
        rError = u"范围未匹配到任何页面（文档共 "_ustr + OUString::number(nPages) + u" 页）。"_ustr;
        return false;
    }
    return true;
}

/** Extract selected pages of one PDF into rOutFileUrl. */
bool lcl_extractPagesToFile(const OUString& rPathOrUrl, const std::vector<int>& rPages,
                            const OUString& rOutFileUrl, sal_Int32& rOutPages, OUString& rError,
                            const PdfProgressFn& rProgress = {})
{
    rOutPages = 0;
    auto pPdfium = vcl::pdf::PDFiumLibrary::get();
    if (!pPdfium)
    {
        rError = u"PDFium 不可用。"_ustr;
        return false;
    }
    std::vector<sal_uInt8> aBuf;
    if (!lcl_readPdfFile(rPathOrUrl, aBuf, rError))
        return false;
    std::unique_ptr<vcl::pdf::PDFiumDocument> pDoc
        = pPdfium->openDocument(aBuf.data(), static_cast<int>(aBuf.size()), OString());
    if (!pDoc)
    {
        rError = u"无法解析 PDF（可能加密）。"_ustr;
        return false;
    }
    if (rOutFileUrl.isEmpty())
    {
        rError = u"未指定输出路径。"_ustr;
        return false;
    }
    lcl_ensureParentDir(rOutFileUrl);
    osl::File::remove(rOutFileUrl);

    vcl::pdf::PDFWriter::PDFWriterContext aCtx;
    aCtx.URL = rOutFileUrl;
    aCtx.Version = vcl::pdf::PDFWriter::PDFVersion::PDF_1_6;
    vcl::pdf::PDFWriter aWriter(aCtx, css::uno::Reference<css::beans::XMaterialHolder>());
    if (!lcl_emitPages(*pPdfium, *pDoc, rPages, aWriter, rOutPages, rError, rProgress))
        return false;
    if (rOutPages <= 0)
    {
        rError = u"没有写出任何页面。"_ustr;
        return false;
    }
    if (rProgress)
        rProgress(rOutPages, rOutPages, u"正在写入文件…"_ustr);
    if (!aWriter.Emit())
    {
        rError = u"写入 PDF 失败。"_ustr;
        return false;
    }
    return true;
}

/** Split each page into rDirUrl/basename_p001.pdf …; returns file URLs. */
bool lcl_splitPdfPerPageToDir(const OUString& rPathOrUrl, const OUString& rDirUrl,
                              std::vector<OUString>& rOutUrls, OUString& rError,
                              const PdfProgressFn& rProgress = {})
{
    rOutUrls.clear();
    auto pPdfium = vcl::pdf::PDFiumLibrary::get();
    if (!pPdfium)
    {
        rError = u"PDFium 不可用。"_ustr;
        return false;
    }
    std::vector<sal_uInt8> aBuf;
    if (!lcl_readPdfFile(rPathOrUrl, aBuf, rError))
        return false;
    std::unique_ptr<vcl::pdf::PDFiumDocument> pDoc
        = pPdfium->openDocument(aBuf.data(), static_cast<int>(aBuf.size()), OString());
    if (!pDoc)
    {
        rError = u"无法解析 PDF（可能加密）。"_ustr;
        return false;
    }
    const int nPages = pDoc->getPageCount();
    if (nPages <= 0)
    {
        rError = u"PDF 没有页面。"_ustr;
        return false;
    }
    if (nPages > kPdfToolMaxPages)
    {
        rError = u"页数超过上限（"_ustr + OUString::number(kPdfToolMaxPages) + u"）。"_ustr;
        return false;
    }
    if (rDirUrl.isEmpty())
    {
        rError = u"未选择输出文件夹。"_ustr;
        return false;
    }
    osl::Directory::createPath(rDirUrl);
    const OUString aStem = lcl_basenameStem(rPathOrUrl);
    OUString aDir = rDirUrl;
    if (!aDir.endsWith("/"))
        aDir += u"/"_ustr;

    for (int i = 0; i < nPages; ++i)
    {
        OUString aName = aStem + u"_p"_ustr;
        // zero-pad to 3 digits
        const int nHuman = i + 1;
        if (nHuman < 10)
            aName += u"00"_ustr;
        else if (nHuman < 100)
            aName += u"0"_ustr;
        aName += OUString::number(nHuman) + u".pdf"_ustr;
        const OUString aOutUrl = aDir + aName;
        osl::File::remove(aOutUrl);

        vcl::pdf::PDFWriter::PDFWriterContext aCtx;
        aCtx.URL = aOutUrl;
        aCtx.Version = vcl::pdf::PDFWriter::PDFVersion::PDF_1_6;
        vcl::pdf::PDFWriter aWriter(aCtx, css::uno::Reference<css::beans::XMaterialHolder>());
        sal_Int32 nWritten = 0;
        std::vector<int> aOne{ i };
        if (!lcl_emitPages(*pPdfium, *pDoc, aOne, aWriter, nWritten, rError, rProgress, i, nPages))
            return false;
        if (!aWriter.Emit() || nWritten != 1)
        {
            rError = u"写出第 "_ustr + OUString::number(i + 1) + u" 页失败。"_ustr;
            return false;
        }
        rOutUrls.push_back(aOutUrl);
        if (rProgress)
            rProgress(i + 1, nPages,
                      u"已写出第 "_ustr + OUString::number(i + 1) + u" / "_ustr
                          + OUString::number(nPages) + u" 个文件…"_ustr);
    }
    return true;
}

class PdfPageRangeDialog final : public weld::GenericDialogController
{
    std::unique_ptr<weld::Label> m_xHint;
    std::unique_ptr<weld::Entry> m_xEntry;

public:
    PdfPageRangeDialog(weld::Window* pParent, sal_Int32 nPages)
        : GenericDialogController(pParent, u"sfx/ui/pdfpagerange.ui"_ustr,
                                  u"PdfPageRangeDialog"_ustr)
        , m_xHint(m_xBuilder->weld_label(u"hint"_ustr))
        , m_xEntry(m_xBuilder->weld_entry(u"range_entry"_ustr))
    {
        if (m_xHint)
        {
            OUStringBuffer b;
            b.append(u"文档共 "_ustr);
            b.append(nPages);
            b.append(u" 页。输入要提取的页码（1 起），例如：1-3,5"_ustr);
            m_xHint->set_label(b.makeStringAndClear());
        }
        if (m_xEntry && nPages > 0)
            m_xEntry->set_text(u"1-"_ustr + OUString::number(nPages));
    }

    OUString getRange() const { return m_xEntry ? m_xEntry->get_text() : OUString(); }
};
}

void BackingWindow::runPdfTool(std::u16string_view rToolId)
{
    weld::Window* pParent = GetFrameWeld();
    const OUString aTool(rToolId);

    if (aTool == u"pdf-open"_ustr)
    {
        Reference<XDispatchProvider> xFrame(mxFrame, UNO_QUERY);
        dispatchURL(u".uno:Open"_ustr, OUString(), xFrame,
                    { comphelper::makePropertyValue(u"Referer"_ustr, u"private:user"_ustr) });
        return;
    }

    if (aTool == u"pdf-info"_ustr)
    {
        const OUString aPath
            = lcl_pickPdfFile(pParent, false, u"选择 PDF 查看页数"_ustr);
        if (aPath.isEmpty())
            return;
        INetURLObject aUrl(aPath);
        if (aUrl.GetProtocol() == INetProtocol::NotValid)
        {
            OUString aFileUrl;
            if (osl::FileBase::getFileURLFromSystemPath(aPath, aFileUrl) == osl::FileBase::E_None)
                aUrl.SetURL(aFileUrl);
            else
                aUrl.SetURL(aPath);
        }
        const sal_Int32 nPages = lcl_pdfPageCount(aUrl.GetMainURL(INetURLObject::DecodeMechanism::NONE));
        OUStringBuffer b;
        b.append(u"文件：\n"_ustr);
        b.append(aPath);
        b.append(u"\n\n"_ustr);
        if (nPages >= 0)
        {
            b.append(u"页数："_ustr);
            b.append(nPages);
            b.append(u"\n\n本地 PDFium 读取，未上传网络。\n"
                     u"完整版式编辑请用「打开 PDF」导入后导出。"_ustr);
        }
        else
            b.append(u"无法读取页数（文件损坏、加密或非 PDF）。"_ustr);
        lcl_showPdfInfo(pParent, u"PDF 页数与信息"_ustr, b.makeStringAndClear());
        return;
    }

    if (aTool == u"pdf-merge"_ustr)
    {
        sfx2::FileDialogHelper aDlg(css::ui::dialogs::TemplateDescription::FILEOPEN_SIMPLE,
                                    FileDialogFlags::MultiSelection, pParent);
        aDlg.SetTitle(u"选择要合并的 PDF（至少 2 个）"_ustr);
        aDlg.AddFilter(u"PDF (*.pdf)"_ustr, u"*.pdf"_ustr);
        aDlg.SetCurrentFilter(u"PDF (*.pdf)"_ustr);
        if (aDlg.Execute() != ERRCODE_NONE)
            return;
        const css::uno::Sequence<OUString> aFiles = aDlg.GetSelectedFiles();
        if (aFiles.getLength() < 2)
        {
            lcl_showPdfInfo(pParent, u"合并 PDF"_ustr,
                            u"请至少选择 2 个 PDF 文件。"_ustr);
            return;
        }

        const OUString aOutUrl
            = lcl_pickSavePdf(pParent, u"保存合并结果"_ustr, u"合并结果.pdf"_ustr);
        if (aOutUrl.isEmpty())
            return;

        sal_Int32 nPages = 0;
        OUString aErr;
        bool bOk = false;
        {
            weld::WaitObject aWait(pParent);
            PdfProgressDialog aProg(pParent, u"正在合并 PDF"_ustr);
            const PdfProgressFn aCb
                = [&aProg](sal_Int32 nDone, sal_Int32 nTotal, const OUString& rSt) {
                      aProg.update(nDone, nTotal, rSt);
                  };
            bOk = lcl_mergePdfsToFile(aFiles, aOutUrl, nPages, aErr, aCb);
        }
        if (!bOk)
        {
            // Fallback: open first + guided insert (vector import path).
            dispatchURL(lcl_toFileUrl(aFiles[0]));
            OUStringBuffer b;
            b.append(u"自动合并未成功："_ustr);
            b.append(aErr);
            b.append(u"\n\n已打开第 1 个文件。可改用引导：\n"
                     u"「插入」→「文件中的页面…」→ 选择其余 PDF →「导出为 PDF」。"_ustr);
            lcl_showPdfInfo(pParent, u"合并 PDF"_ustr, b.makeStringAndClear());
            return;
        }

        dispatchURL(aOutUrl);
        lcl_revealInFileManager(aOutUrl, /*bIsDirectory*/ false);
        OUStringBuffer b;
        b.append(u"已合并 "_ustr);
        b.append(static_cast<sal_Int32>(aFiles.getLength()));
        b.append(u" 个文件 → "_ustr);
        b.append(nPages);
        b.append(u" 页。\n保存位置：\n"_ustr);
        b.append(lcl_toSystemPath(aOutUrl));
        b.append(u"\n\n已在文件管理器中定位该文件。\n\n"
                 u"说明（诚实边界）：\n"
                 u"· 本地 PDFium 渲染 + 重新封装，页面以图像嵌入；\n"
                 u"· 文本不可再选/检索，适合打印与分发预览；\n"
                 u"· 非 Acrobat 工业矢量合并 / PDF/A 归档引擎。"_ustr);
        lcl_showPdfInfo(pParent, u"合并 PDF 完成"_ustr, b.makeStringAndClear());
        return;
    }

    if (aTool == u"pdf-split"_ustr || aTool == u"pdf-extract"_ustr)
    {
        const OUString aPath
            = lcl_pickPdfFile(pParent, false, u"选择要拆分/提取的 PDF"_ustr);
        if (aPath.isEmpty())
            return;
        const OUString aOpen = lcl_toFileUrl(aPath);
        const sal_Int32 nPages = lcl_pdfPageCount(aOpen);
        if (nPages <= 0)
        {
            lcl_showPdfInfo(pParent, u"拆分 PDF"_ustr,
                            u"无法读取页数（损坏、加密或非 PDF）。"_ustr);
            return;
        }

        // Yes = 每页单独文件；No = 按范围提取为一个 PDF（关闭对话框 = 取消）
        std::unique_ptr<weld::MessageDialog> xAsk(Application::CreateMessageDialog(
            pParent, VclMessageType::Question, VclButtonsType::YesNo,
            u"文档共 "_ustr + OUString::number(nPages)
                + u" 页。\n\n"
                  u"「是」：按页拆成多个 PDF（每页一个文件）\n"
                  u"「否」：按页码范围提取为一个 PDF\n\n"
                  u"说明：本地图像页封装，非 Acrobat 矢量拆分。"_ustr));
        xAsk->set_title(u"拆分 / 提取 PDF"_ustr);
        xAsk->set_default_response(RET_YES);
        const int nAns = xAsk->run();
        if (nAns != RET_YES && nAns != RET_NO)
            return;

        if (nAns == RET_YES)
        {
            const OUString aDirUrl
                = lcl_pickOutputFolder(mxContext, pParent, u"选择拆分结果保存文件夹"_ustr);
            if (aDirUrl.isEmpty())
                return;
            std::vector<OUString> aOuts;
            OUString aErr;
            {
                weld::WaitObject aWait(pParent);
                PdfProgressDialog aProg(pParent, u"正在按页拆分 PDF"_ustr);
                const PdfProgressFn aCb
                    = [&aProg](sal_Int32 nDone, sal_Int32 nTotal, const OUString& rSt) {
                          aProg.update(nDone, nTotal, rSt);
                      };
                if (!lcl_splitPdfPerPageToDir(aOpen, aDirUrl, aOuts, aErr, aCb))
                {
                    lcl_showPdfInfo(pParent, u"按页拆分失败"_ustr, aErr);
                    return;
                }
            }
            if (!aOuts.empty())
            {
                dispatchURL(aOuts.front());
                // Highlight first page file when possible; else open folder.
                lcl_revealInFileManager(aOuts.front(), /*bIsDirectory*/ false);
            }
            else
                lcl_revealInFileManager(aDirUrl, /*bIsDirectory*/ true);
            OUStringBuffer b;
            b.append(u"已拆成 "_ustr);
            b.append(static_cast<sal_Int32>(aOuts.size()));
            b.append(u" 个 PDF，并打开第 1 页。\n保存文件夹：\n"_ustr);
            b.append(lcl_toSystemPath(aDirUrl));
            b.append(u"\n命名：原文件名_p001.pdf …\n"
                     u"已在文件管理器中定位结果。\n\n边界：图像页；文本不可再选。"_ustr);
            lcl_showPdfInfo(pParent, u"按页拆分完成"_ustr, b.makeStringAndClear());
            return;
        }

        // RET_NO → page range extract
        PdfPageRangeDialog aRangeDlg(pParent, nPages);
        if (aRangeDlg.run() != RET_OK)
            return;
        std::vector<int> aIndices;
        OUString aErr;
        if (!lcl_parsePageRange(aRangeDlg.getRange(), static_cast<int>(nPages), aIndices, aErr))
        {
            lcl_showPdfInfo(pParent, u"页码无效"_ustr, aErr);
            return;
        }
        const OUString aOutUrl = lcl_pickSavePdf(
            pParent, u"保存提取结果"_ustr, lcl_basenameStem(aOpen) + u"_提取.pdf"_ustr);
        if (aOutUrl.isEmpty())
            return;
        sal_Int32 nOut = 0;
        {
            weld::WaitObject aWait(pParent);
            PdfProgressDialog aProg(pParent, u"正在提取 PDF 页"_ustr);
            const PdfProgressFn aCb
                = [&aProg](sal_Int32 nDone, sal_Int32 nTotal, const OUString& rSt) {
                      aProg.update(nDone, nTotal, rSt);
                  };
            if (!lcl_extractPagesToFile(aOpen, aIndices, aOutUrl, nOut, aErr, aCb))
            {
                lcl_showPdfInfo(pParent, u"提取失败"_ustr, aErr);
                return;
            }
        }
        dispatchURL(aOutUrl);
        lcl_revealInFileManager(aOutUrl, /*bIsDirectory*/ false);
        OUStringBuffer b;
        b.append(u"已提取 "_ustr);
        b.append(nOut);
        b.append(u" 页。\n保存位置：\n"_ustr);
        b.append(lcl_toSystemPath(aOutUrl));
        b.append(u"\n已在文件管理器中定位该文件。\n\n边界：图像页封装，适合打印分发。"_ustr);
        lcl_showPdfInfo(pParent, u"提取 PDF 完成"_ustr, b.makeStringAndClear());
        return;
    }

    if (aTool == u"pdf-export"_ustr)
    {
        // Prefer ExportToPDF on an already-open document frame.
        for (SfxViewFrame* pFrame = SfxViewFrame::GetFirst(); pFrame;
             pFrame = SfxViewFrame::GetNext(*pFrame))
        {
            if (!pFrame->GetObjectShell())
                continue;
            const OUString aFac = pFrame->GetObjectShell()->GetFactory().GetFactoryName();
            if (aFac == u"swriter"_ustr || aFac == u"scalc"_ustr || aFac == u"simpress"_ustr
                || aFac == u"sdraw"_ustr)
            {
                pFrame->GetDispatcher()->Execute(SID_EXPORTDOCASPDF, SfxCallMode::ASYNCHRON);
                return;
            }
        }
        // No document: open Writer + short guide.
        dispatchURL(u"private:factory/swriter"_ustr);
        lcl_showPdfInfo(pParent, u"导出为 PDF"_ustr,
                        u"已打开空白文档。\n\n"
                        u"写好内容后使用：\n"
                        u"「文件」→「导出为 PDF…」或「直接导出为 PDF」。\n\n"
                        u"新产品默认会在导出后打开 PDF 预览（可在选项中关闭）。"_ustr);
        return;
    }

    SAL_WARN("sfx", "runPdfTool: unknown tool " << aTool);
}

IMPL_LINK_NOARG(BackingWindow, TemplateSearchHdl, weld::Entry&, void)
{
    refreshTemplateMarket();
}

IMPL_LINK_NOARG(BackingWindow, TemplateCategoryHdl, weld::ComboBox&, void)
{
    refreshTemplateMarket();
}

IMPL_LINK_NOARG(BackingWindow, TemplateMarketActivateHdl, weld::TreeView&, bool)
{
    openTemplateMarketSelection();
    return true;
}

bool BackingWindow::resolveTemplatePathByFileName(const SfxDocumentTemplates& rTemplates,
                                                  std::u16string_view rTemplateFileName,
                                                  OUString& rTemplatePath)
{
    if (rTemplateFileName.empty())
        return false;

    // Prefer full relative match (…/presnt/Business_Pitch_CN.otp), then basename.
    const OUString aRel(rTemplateFileName);
    OUString aBase = aRel;
    const sal_Int32 nSlash = aRel.lastIndexOf(u'/');
    if (nSlash >= 0 && nSlash + 1 < aRel.getLength())
        aBase = aRel.copy(nSlash + 1);

    const sal_uInt16 nRegionCount = rTemplates.GetRegionCount();
    for (sal_uInt16 nRegion = 0; nRegion < nRegionCount; ++nRegion)
    {
        const sal_uInt16 nTemplateCount = rTemplates.GetCount(nRegion);
        for (sal_uInt16 nTemplate = 0; nTemplate < nTemplateCount; ++nTemplate)
        {
            OUString aCandidatePath = rTemplates.GetPath(nRegion, nTemplate);
            if (aCandidatePath.isEmpty())
                continue;
            if (aCandidatePath.endsWithIgnoreAsciiCase(aRel)
                || (!aBase.isEmpty() && aCandidatePath.endsWithIgnoreAsciiCase(aBase)))
            {
                const OUString aExisting = lcl_existingTemplateFileURL(aCandidatePath);
                if (!aExisting.isEmpty())
                {
                    rTemplatePath = aExisting;
                    return true;
                }
            }
        }
    }

    return false;
}

std::array<BackingWindow::ScenarioTemplate, 11> BackingWindow::getScenarioTemplates()
{
    return { { { mxScenarioReportButton.get(), u"offimisc/Work_Report_CN.ott", u"工作汇报",
                 FILTER_APPLICATION::WRITER },
               { mxScenarioMinutesButton.get(), u"offimisc/Meeting_Minutes_CN.ott", u"会议纪要",
                 FILTER_APPLICATION::WRITER },
               { mxScenarioNoticeButton.get(), u"officorr/Notice_CN.ott", u"通知",
                 FILTER_APPLICATION::WRITER },
               { mxScenarioPlanButton.get(), u"offimisc/Project_Plan_CN.ott", u"项目方案",
                 FILTER_APPLICATION::WRITER },
               { mxScenarioBudgetButton.get(), u"spreadsheets/Budget_CN.ots", u"预算总览",
                 FILTER_APPLICATION::CALC },
               { mxScenarioSalesButton.get(), u"spreadsheets/Sales_Tracker_CN.ots", u"销售跟进",
                 FILTER_APPLICATION::CALC },
               { mxScenarioScheduleButton.get(), u"spreadsheets/Project_Schedule_CN.ots", u"项目排期",
                 FILTER_APPLICATION::CALC },
               { mxScenarioOutlineButton.get(), u"offimisc/PPT_Outline_CN.ott", u"演示提纲",
                 FILTER_APPLICATION::WRITER },
               { mxScenarioPitchButton.get(), u"presnt/Business_Pitch_CN.otp", u"商务路演",
                 FILTER_APPLICATION::IMPRESS },
               { mxScenarioProjectReportButton.get(), u"presnt/Project_Report_CN.otp", u"项目汇报",
                 FILTER_APPLICATION::IMPRESS },
               { mxScenarioCoursewareButton.get(), u"presnt/Teaching_Courseware_CN.otp", u"教学课件",
                 FILTER_APPLICATION::IMPRESS } } };
}

void BackingWindow::openScenarioTemplate(std::u16string_view rTemplateFileName,
                                         std::u16string_view rFallbackTitle,
                                         FILTER_APPLICATION eFilter)
{
    SfxDocumentTemplates aTemplates;
    aTemplates.Update();

    OUString aTemplatePath;
    if (resolveTemplatePathByFileName(aTemplates, rTemplateFileName, aTemplatePath))
    {
        OpenTemplateHdl(aTemplatePath);
        return;
    }

    if (!rFallbackTitle.empty() && aTemplates.GetFull(u"", rFallbackTitle, aTemplatePath)
        && !aTemplatePath.isEmpty())
    {
        const OUString aExisting = lcl_existingTemplateFileURL(aTemplatePath);
        if (!aExisting.isEmpty())
        {
            OpenTemplateHdl(aExisting);
            return;
        }
    }

    // Catalog/registry can lag behind packaging (pruned decorative presnt packs).
    // Resolve on disk under brand share so CN scenario buttons always work.
    const OUString aBrandUrl = lcl_brandCommonTemplateURL(rTemplateFileName);
    if (!aBrandUrl.isEmpty())
    {
        OpenTemplateHdl(aBrandUrl);
        return;
    }

    SAL_WARN("sfx", "openScenarioTemplate: missing template " << OUString(rTemplateFileName));
    showTemplateHub(eFilter);
}

IMPL_LINK(BackingWindow, OpenScenarioHdl, weld::Button&, rButton, void)
{
    for (const auto& rScenario : getScenarioTemplates())
    {
        if (rScenario.pButton && &rButton == rScenario.pButton)
        {
            openScenarioTemplate(rScenario.aFileName, rScenario.aFallbackTitle, rScenario.eFilter);
            return;
        }
    }
}

namespace
{
/** True for Writer/Calc/Impress factory docs — not StartModule / backing shell. */
bool isAiDraftTargetFactory(const OUString& rFactoryName)
{
    return rFactoryName == u"swriter"_ustr || rFactoryName == u"scalc"_ustr
           || rFactoryName == u"simpress"_ustr || rFactoryName.startsWith(u"swriter/"_ustr)
           || rFactoryName.startsWith(u"scalc/"_ustr)
           || rFactoryName.startsWith(u"simpress/"_ustr);
}

/** Dual path: AI panel also polls pending-prompt-inject (Chinese, user-visible). */
void writePendingPromptInjectZh(const OUString& rText)
{
    const char* home = std::getenv("HOME");
    if (!home || !*home || rText.isEmpty())
        return;
    const OUString path
        = OUString::fromUtf8(home) + u"/.config/kqoffice/pending-prompt-inject"_ustr;
    const sal_Int32 slash = path.lastIndexOf(u'/');
    if (slash > 0)
    {
        OUString dirUrl;
        if (osl::FileBase::getFileURLFromSystemPath(path.copy(0, slash), dirUrl)
            == osl::FileBase::E_None)
            osl::Directory::createPath(dirUrl);
    }
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(path, url) != osl::FileBase::E_None)
        return;
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
        return;
    f.setSize(0);
    const OString utf8 = OUStringToOString(rText, RTL_TEXTENCODING_UTF8);
    sal_uInt64 n = 0;
    f.write(utf8.getStr(), utf8.getLength(), n);
    f.close();
}

/**
 * Open AI deck using the official Sidebar API.
 * Returns false if sidebar controller is not ready yet (caller should retry).
 * UNO ".uno:SidebarDeck.*" alone is unreliable right after factory open.
 */
bool tryOpenAiChatDeck(SfxViewFrame* pFrame)
{
    if (!pFrame || !pFrame->GetObjectShell())
        return false;
    if (!isAiDraftTargetFactory(pFrame->GetObjectShell()->GetFactory().GetFactoryName()))
        return false;

    // 1) Force sidebar child window visible
    pFrame->ShowChildWindow(SID_SIDEBAR);

    // 2) Controller is created asynchronously with the child window
    const css::uno::Reference<css::frame::XFrame> xFrame(
        pFrame->GetFrame().GetFrameInterface());
    sfx2::sidebar::SidebarController* pCtrl
        = sfx2::sidebar::SidebarController::GetSidebarControllerForFrame(xFrame);
    if (!pCtrl)
        return false;

    // 3) Switch deck (bToggle=false: never close if already open)
    sfx2::sidebar::Sidebar::ShowDeck(u"AIChatDeck"_ustr, pFrame, /*bToggle*/ false);
    // Expand/focus the AI panel when resource id is known
    sfx2::sidebar::Sidebar::ShowPanel(u"AIChatPanel"_ustr, xFrame, /*bFocus*/ true);
    return true;
}

/**
 * Timer-based opener: factory document open is async.
 * Scan ALL view frames every 250ms; open AI deck once when controller is ready.
 * Singleton — prevents open-storm if user clicks AI 创作 repeatedly.
 */
class AiDraftDeckOpener final
{
public:
    static void ArmOnce()
    {
        if (s_pActive)
        {
            // Refresh budget; do not spawn a second opener (that multi-opens factories).
            s_pActive->m_nLeft = 48;
            if (!s_pActive->m_aTimer.IsActive())
                s_pActive->m_aTimer.Start();
            return;
        }
        s_pActive = new AiDraftDeckOpener;
        s_pActive->m_aTimer.Start();
    }

private:
    AiDraftDeckOpener()
        : m_aTimer("AiDraftDeckOpener")
        , m_nLeft(48) // ~12s
    {
        m_aTimer.SetTimeout(250);
        m_aTimer.SetInvokeHandler(LINK(this, AiDraftDeckOpener, OnTick));
    }

    DECL_LINK(OnTick, Timer*, void);

    Timer m_aTimer;
    sal_Int32 m_nLeft;
    static AiDraftDeckOpener* s_pActive;
};

AiDraftDeckOpener* AiDraftDeckOpener::s_pActive = nullptr;

IMPL_LINK_NOARG(AiDraftDeckOpener, OnTick, Timer*, void)
{
    for (SfxViewFrame* pFrame = SfxViewFrame::GetFirst(); pFrame;
         pFrame = SfxViewFrame::GetNext(*pFrame))
    {
        if (tryOpenAiChatDeck(pFrame))
        {
            m_aTimer.Stop();
            s_pActive = nullptr;
            delete this;
            return;
        }
    }

    if (--m_nLeft <= 0)
    {
        m_aTimer.Stop();
        s_pActive = nullptr;
        delete this;
    }
}

OUString buildAiDraftInjectText(const OUString& rScenarioId)
{
    using kqoffice::ai::chat::DocumentAIScenarioStore;
    const auto cat = DocumentAIScenarioStore::load();
    const auto* p = DocumentAIScenarioStore::find(cat, rScenarioId);

    // Short, user-facing Chinese first — then full template for the model.
    OUStringBuffer b;
    b.append(u"【可圈 AI 创作】"_ustr);
    if (p && !p->titleZh.isEmpty())
    {
        b.append(u" · "_ustr);
        b.append(p->titleZh);
    }
    b.append(u"\n\n"_ustr);
    b.append(u"请在下一行写上你的主题（例如：本周工作汇报 / 销售数据表 / 项目路演）：\n\n"_ustr);
    b.append(u"主题：\n"_ustr);
    if (p && !p->id.isEmpty())
    {
        b.append(u"\n——（以下为 AI 执行说明，可改）——\n"_ustr);
        b.append(DocumentAIScenarioStore::expandPrompt(*p, OUString()));
    }
    b.append(u"\n\n写好主题后，点击右侧「发送」。"_ustr);
    return b.makeStringAndClear();
}
}

void BackingWindow::openAiDraft(std::u16string_view rScenarioId, const OUString& rFactoryUrl)
{
    const OUString aId(rScenarioId);
    // 1) Queue scenario for AIChatPanel::ConsumePendingScenarioRun (structured prefill).
    kqoffice::ai::chat::DocumentAIScenarioStore::queuePendingRun(aId);
    // 2) Also write Chinese pending-prompt-inject — panel polls this every 500ms when open
    //    (covers cases where scenario consume races or user opens AI manually).
    writePendingPromptInjectZh(buildAiDraftInjectText(aId));

    // If a Writer/Calc/Impress frame already exists, only open AI deck — do not
    // open another factory document (open-storm was a crash trigger).
    bool bHasTargetDoc = false;
    for (SfxViewFrame* pFrame = SfxViewFrame::GetFirst(); pFrame;
         pFrame = SfxViewFrame::GetNext(*pFrame))
    {
        if (pFrame->GetObjectShell()
            && isAiDraftTargetFactory(pFrame->GetObjectShell()->GetFactory().GetFactoryName()))
        {
            bHasTargetDoc = true;
            tryOpenAiChatDeck(pFrame);
            break;
        }
    }
    if (!bHasTargetDoc)
    {
        // 3) Open blank factory document (async) — single path only.
        dispatchURL(rFactoryUrl);
    }
    // 4) Timer: wait for real doc frame, open sidebar + AI deck once (singleton).
    AiDraftDeckOpener::ArmOnce();
}

IMPL_LINK(BackingWindow, AiDraftHdl, weld::Button&, rButton, void)
{
    if (mxAiDraftWriterButton && &rButton == mxAiDraftWriterButton.get())
        openAiDraft(u"blank-draft-writer", u"private:factory/swriter"_ustr);
    else if (mxAiDraftCalcButton && &rButton == mxAiDraftCalcButton.get())
        openAiDraft(u"blank-draft-calc", u"private:factory/scalc"_ustr);
    else if (mxAiDraftImpressButton && &rButton == mxAiDraftImpressButton.get())
        openAiDraft(u"blank-draft-impress", u"private:factory/simpress?slot=6686"_ustr);
}

IMPL_LINK_NOARG(BackingWindow, OpenCompatibilityHdl, weld::Button&, void)
{
    Reference< XDispatchProvider > xFrame(mxFrame, UNO_QUERY);
    dispatchURL(u".uno:Open"_ustr, OUString(), xFrame,
                { comphelper::makePropertyValue(u"Referer"_ustr, u"private:user"_ustr) });
}

namespace {

struct ImplDelayedDispatch
{
    Reference< XDispatch >      xDispatch;
    css::util::URL   aDispatchURL;
    Sequence< PropertyValue >   aArgs;

    ImplDelayedDispatch( const Reference< XDispatch >& i_xDispatch,
                         css::util::URL i_aURL,
                         const Sequence< PropertyValue >& i_rArgs )
    : xDispatch( i_xDispatch ),
      aDispatchURL(std::move( i_aURL )),
      aArgs( i_rArgs )
    {
    }
};

}

static void implDispatchDelayed( void*, void* pArg )
{
    struct ImplDelayedDispatch* pDispatch = static_cast<ImplDelayedDispatch*>(pArg);
    try
    {
        pDispatch->xDispatch->dispatch( pDispatch->aDispatchURL, pDispatch->aArgs );
    }
    catch (const Exception&)
    {
    }

    // clean up
    delete pDispatch;
}

void BackingWindow::dispatchURL( const OUString& i_rURL,
                                 const OUString& rTarget,
                                 const Reference< XDispatchProvider >& i_xProv,
                                 const Sequence< PropertyValue >& i_rArgs )
{
    // if no special dispatch provider is given, get the desktop
    if (!i_xProv.is())
        ensureDesktopDispatch();
    Reference< XDispatchProvider > xProvider( i_xProv.is() ? i_xProv : mxDesktopDispatchProvider );

    // check for dispatch provider
    if( !xProvider.is())
        return;

    // get a URL transformer to clean up the URL
    css::util::URL aDispatchURL;
    aDispatchURL.Complete = i_rURL;

    Reference < css::util::XURLTransformer > xURLTransformer(
        css::util::URLTransformer::create( comphelper::getProcessComponentContext() ) );
    try
    {
        // clean up the URL
        xURLTransformer->parseStrict( aDispatchURL );
        // get a Dispatch for the URL and target
        Reference< XDispatch > xDispatch(
            xProvider->queryDispatch( aDispatchURL, rTarget, 0 )
            );
        // dispatch the URL
        if ( xDispatch.is() )
        {
            std::unique_ptr<ImplDelayedDispatch> pDisp(new ImplDelayedDispatch( xDispatch, std::move(aDispatchURL), i_rArgs ));
            if( Application::PostUserEvent( LINK_NONMEMBER( nullptr, implDispatchDelayed ), pDisp.get() ) )
                pDisp.release();
        }
    }
    catch (const css::uno::RuntimeException&)
    {
        throw;
    }
    catch (const css::uno::Exception&)
    {
    }
}

void BackingWindow::clearRecentFileList()
{
    ensureRecentThumbnails();
    if (!mxAllRecentThumbnails)
        return;
    mxAllRecentThumbnails->Clear();
    // tdf#166349 - reload recent documents to show pinned items
    mxAllRecentThumbnails->Reload();
}

void BackingWindow::showFileWorkbenchPane(bool bShow)
{
    if (!mxFileWorkbenchBox)
        return;
    if (bShow)
        mxFileWorkbenchBox->show();
    else
        mxFileWorkbenchBox->hide();
}

void BackingWindow::refreshFileWorkbench(bool bRescan)
{
    if (!mxFileWorkbenchTree || !mxFileWorkbenchStatus)
        return;

    renderPermissionPanel();

    if (bRescan)
    {
        startFileWorkbenchScan();
        return;
    }

    bool hasIndex = false;
    {
        std::scoped_lock guard(maFileWorkbenchResultMutex);
        hasIndex = mbFileWorkbenchScanReady || !maFileWorkbenchFiles.empty();
    }
    if (!hasIndex)
    {
        if (!mxFileWorkbenchScanThread)
            startFileWorkbenchScan();
        return;
    }

    renderFileWorkbench();
}

void BackingWindow::renderPermissionPanel()
{
    using kqoffice::ai::control::CapabilityPermission;
    using kqoffice::ai::control::PermissionCenter;
    using kqoffice::ai::control::PermissionState;

    PermissionCenter perms;
    const auto dirs = perms.authorizedDirectories();
    mnFileWorkbenchAuthorizedRootCount = static_cast<sal_Int32>(dirs.size());

    if (mxFileWorkbenchAuthTree)
    {
        mxFileWorkbenchAuthTree->clear();
        for (const auto& d : dirs)
        {
            mxFileWorkbenchAuthTree->append(d.path, d.path);
            const int row = mxFileWorkbenchAuthTree->n_children() - 1;
            mxFileWorkbenchAuthTree->set_text(
                row, d.recursive ? u"含子目录"_ustr : u"仅本目录"_ustr, 1);
        }
    }

    const auto netState = perms.stateOf(CapabilityPermission::NetworkEgress);
    if (mxFileWorkbenchNetworkStatus)
        mxFileWorkbenchNetworkStatus->set_label(perms.networkStatusLineZh());
    if (mxFileWorkbenchCapStatus)
    {
        mxFileWorkbenchCapStatus->set_label(
            perms.settingsCapabilityStatusZh(CapabilityPermission::Microphone) + u" · "_ustr
            + perms.settingsCapabilityStatusZh(CapabilityPermission::ScreenCapture));
    }
    if (mxFileWorkbenchRiskHint)
        mxFileWorkbenchRiskHint->set_label(PermissionCenter::riskPolicyHintZh());
    if (mxFileWorkbenchNetworkRevoke)
        mxFileWorkbenchNetworkRevoke->set_sensitive(netState == PermissionState::Granted);
    if (mxFileWorkbenchRevoke)
        mxFileWorkbenchRevoke->set_sensitive(!dirs.empty());
}

OUString BackingWindow::selectedAuthorizedDirectory() const
{
    if (!mxFileWorkbenchAuthTree)
        return {};
    const int row = mxFileWorkbenchAuthTree->get_selected_index();
    if (row < 0)
        return {};
    return mxFileWorkbenchAuthTree->get_id(row);
}

void BackingWindow::startFileWorkbenchScan()
{
    if (!mxFileWorkbenchTree || !mxFileWorkbenchStatus)
        return;

    cancelFileWorkbenchScan();
    kqoffice::ai::control::PermissionCenter perms;

    if (!perms.hasAnyAuthorizedDirectory())
    {
        mxFileWorkbenchTree->clear();
        {
            std::scoped_lock guard(maFileWorkbenchResultMutex);
            maFileWorkbenchFiles.clear();
            mbFileWorkbenchScanReady = false;
        }
        mnFileWorkbenchAuthorizedRootCount = 0;
        mxFileWorkbenchStatus->set_label(
            u"尚未授权目录 — 点击「授权目录…」选择文件夹。不会扫描全盘；网络默认关闭且不静默授权；麦克风/截图使用时由系统询问。"_ustr);
        renderPermissionPanel();
        return;
    }

    kqoffice::ai::filemgr::ScanFilter filter;
    filter.requireAuthorizedRoots = true;
    filter.maxDepth = 4;
    filter.followSymlinks = false;
    filter.cancelFlag = &mbFileWorkbenchScanCancelled;
    for (const auto& d : perms.authorizedDirectories())
        filter.scanRoots.push_back(d.path);

    mnFileWorkbenchAuthorizedRootCount
        = static_cast<sal_Int32>(filter.scanRoots.size());
    mbFileWorkbenchScanCancelled = false;
    {
        std::scoped_lock guard(maFileWorkbenchResultMutex);
        maFileWorkbenchFiles.clear();
        mnFileWorkbenchScanDurationMs = 0;
        mbFileWorkbenchScanReady = false;
    }

    mxFileWorkbenchTree->clear();
    mxFileWorkbenchStatus->set_label(
        u"正在建立本地索引… 可继续使用其他功能"_ustr);
    if (mxFileWorkbenchRefresh)
        mxFileWorkbenchRefresh->set_sensitive(false);

    mxFileWorkbenchScanThread
        = std::make_unique<FileWorkbenchScanThread>(*this, std::move(filter));
    if (!mxFileWorkbenchScanThread->create())
    {
        mxFileWorkbenchScanThread.reset();
        if (mxFileWorkbenchRefresh)
            mxFileWorkbenchRefresh->set_sensitive(true);
        mxFileWorkbenchStatus->set_label(u"索引启动失败，请稍后重试"_ustr);
        return;
    }
    maFileWorkbenchPollTimer.Start();
}

void BackingWindow::cancelFileWorkbenchScan()
{
    maFileWorkbenchPollTimer.Stop();
    mbFileWorkbenchScanCancelled = true;
    if (mxFileWorkbenchScanThread)
    {
        mxFileWorkbenchScanThread->join();
        mxFileWorkbenchScanThread.reset();
    }
    if (mxFileWorkbenchRefresh)
        mxFileWorkbenchRefresh->set_sensitive(true);
}

void BackingWindow::renderFileWorkbench()
{
    if (!mxFileWorkbenchTree || !mxFileWorkbenchStatus)
        return;

    std::vector<kqoffice::ai::filemgr::FileEntry> indexed;
    sal_Int64 durationMs = 0;
    {
        std::scoped_lock guard(maFileWorkbenchResultMutex);
        indexed = maFileWorkbenchFiles;
        durationMs = mnFileWorkbenchScanDurationMs;
    }

    kqoffice::ai::filemgr::AIFileManager mgr;
    mgr.applyWorkbenchMetadata(indexed);

    const OUString query
        = mxFileWorkbenchSearch ? mxFileWorkbenchSearch->get_text().trim() : OUString();
    std::vector<kqoffice::ai::filemgr::FileEntry> shown = indexed;
    OUString filterNote;
    if (!query.isEmpty())
    {
        // Structured filters: tag:xxx / #xxx / project:xxx (local metadata, no AI).
        OUString q = query;
        OUString tagFilter;
        OUString projectFilter;
        if (q.startsWith(u"tag:"_ustr))
            tagFilter = q.copy(4).trim();
        else if (q.startsWith(u"#"_ustr) && q.getLength() > 1)
            tagFilter = q.copy(1).trim();
        else if (q.startsWith(u"project:"_ustr) || q.startsWith(u"proj:"_ustr))
        {
            const sal_Int32 colon = q.indexOf(u':');
            projectFilter = q.copy(colon + 1).trim();
        }

        if (!tagFilter.isEmpty())
        {
            const OUString needle = tagFilter.toAsciiLowerCase();
            shown.clear();
            for (const auto& f : indexed)
            {
                if (f.tag.toAsciiLowerCase().indexOf(needle) >= 0)
                    shown.push_back(f);
            }
            filterNote = u" · 标签筛选 "_ustr + tagFilter;
        }
        else if (!projectFilter.isEmpty())
        {
            const OUString needle = projectFilter.toAsciiLowerCase();
            shown.clear();
            for (const auto& f : indexed)
            {
                if (f.projectKey.toAsciiLowerCase().indexOf(needle) >= 0
                    || f.parentDir.toAsciiLowerCase().indexOf(needle) >= 0)
                    shown.push_back(f);
            }
            filterNote = u" · 项目筛选 "_ustr + projectFilter;
        }
        else
        {
            auto matches = mgr.semanticSearch(query, indexed, 100);
            shown.clear();
            shown.reserve(matches.size());
            for (const auto& m : matches)
                shown.push_back(m.file);
        }
    }
    else
    {
        // Default browse: project aggregation (M13 knowledge-style grouping).
        kqoffice::ai::filemgr::AIFileManager::groupByProject(shown);
    }

    mxFileWorkbenchTree->clear();
    sal_Int32 nPinned = 0;
    sal_Int32 nTagged = 0;
    OUString lastProject;
    for (const auto& f : shown)
    {
        // Project section header when browsing (no free-text query).
        if (query.isEmpty() && !f.projectKey.isEmpty() && f.projectKey != lastProject)
        {
            lastProject = f.projectKey;
            // Empty id → not openable as a file row.
            mxFileWorkbenchTree->append(OUString(), u"【项目】 "_ustr + lastProject);
            const int hdr = mxFileWorkbenchTree->n_children() - 1;
            if (hdr >= 0)
            {
                mxFileWorkbenchTree->set_text(hdr, u"分组"_ustr, 1);
                mxFileWorkbenchTree->set_text(hdr, u"—"_ustr, 2);
                mxFileWorkbenchTree->set_text(hdr, f.parentDir, 3);
            }
        }

        OUString meta;
        if (f.pinned)
        {
            meta += u"置顶"_ustr;
            ++nPinned;
        }
        if (f.favorite)
        {
            if (!meta.isEmpty())
                meta += u"·"_ustr;
            meta += u"收藏"_ustr;
        }
        if (!f.tag.isEmpty())
        {
            if (!meta.isEmpty())
                meta += u"·"_ustr;
            meta += f.tag;
            ++nTagged;
        }
        if (!f.projectKey.isEmpty() && !query.isEmpty())
        {
            if (!meta.isEmpty())
                meta += u"·"_ustr;
            meta += f.projectKey;
        }

        // id = full path for open/pin/tag actions
        mxFileWorkbenchTree->append(f.path, f.name);
        const int row = mxFileWorkbenchTree->n_children() - 1;
        mxFileWorkbenchTree->set_text(
            row, kqoffice::ai::filemgr::AIFileManager::categoryLabel(f.category), 1);
        mxFileWorkbenchTree->set_text(row, meta, 2);
        mxFileWorkbenchTree->set_text(row, f.path, 3);
    }

    kqoffice::ai::control::PermissionCenter perms;
    const OUString netState = kqoffice::ai::control::PermissionCenter::stateLabelZh(
        perms.stateOf(kqoffice::ai::control::CapabilityPermission::NetworkEgress));

    OUString status = u"本地索引 · 授权目录 "_ustr
        + OUString::number(mnFileWorkbenchAuthorizedRootCount)
        + u" 个 · 匹配 "_ustr + OUString::number(static_cast<sal_Int32>(shown.size()))
        + u" 个文件 · 索引耗时 "_ustr + OUString::number(durationMs) + u" ms"_ustr
        + u" · 网络 "_ustr + netState
        + u"（默认关闭） · 仅索引已授权路径"_ustr;
    if (nPinned > 0)
        status += u" · 置顶 "_ustr + OUString::number(nPinned);
    if (nTagged > 0)
        status += u" · 已打标签 "_ustr + OUString::number(nTagged);
    status += filterNote;
    if (query.isEmpty())
        status += u" · 按项目分组 · 搜索 tag:标签 / project:名"_ustr;
    mxFileWorkbenchStatus->set_label(status);
    renderPermissionPanel();
}

IMPL_LINK_NOARG(BackingWindow, FileWorkbenchPollHdl, Timer*, void)
{
    bool ready = false;
    {
        std::scoped_lock guard(maFileWorkbenchResultMutex);
        ready = mbFileWorkbenchScanReady;
    }
    if (!ready)
        return;

    maFileWorkbenchPollTimer.Stop();
    if (mxFileWorkbenchScanThread)
    {
        mxFileWorkbenchScanThread->join();
        mxFileWorkbenchScanThread.reset();
    }
    if (mxFileWorkbenchRefresh)
        mxFileWorkbenchRefresh->set_sensitive(true);
    renderFileWorkbench();
}

OUString BackingWindow::selectedFileWorkbenchPath() const
{
    if (!mxFileWorkbenchTree)
        return {};
    const int row = mxFileWorkbenchTree->get_selected_index();
    if (row < 0)
        return {};
    return mxFileWorkbenchTree->get_id(row);
}

std::vector<OUString> BackingWindow::selectedFileWorkbenchPaths() const
{
    std::vector<OUString> paths;
    if (!mxFileWorkbenchTree)
        return paths;

    const std::vector<int> rows = mxFileWorkbenchTree->get_selected_rows();
    paths.reserve(rows.size());
    for (const int row : rows)
    {
        const OUString id = mxFileWorkbenchTree->get_id(row);
        if (!id.isEmpty())
            paths.push_back(id);
    }
    // Fallback: some backends only expose the primary selection index.
    if (paths.empty())
    {
        const OUString one = selectedFileWorkbenchPath();
        if (!one.isEmpty())
            paths.push_back(one);
    }
    return paths;
}

namespace
{
OUString formatBatchRunStatusZh(const kqoffice::ai::filemgr::BatchRunSummary& summary,
                                const kqoffice::ai::filemgr::BatchJob& job,
                                sal_Int32 nUnauthorized = 0)
{
    OUString reason;
    if (!job.reasonZh.isEmpty())
        reason = job.reasonZh;
    else
    {
        for (const auto& item : job.items)
        {
            if ((item.state == kqoffice::ai::filemgr::BatchItemState::Failed
                 || item.state == kqoffice::ai::filemgr::BatchItemState::Skipped)
                && !item.reasonZh.isEmpty())
            {
                if (!reason.isEmpty())
                    reason += u"；"_ustr;
                reason += item.reasonZh;
                if (reason.getLength() > 160)
                {
                    reason += u"…"_ustr;
                    break;
                }
            }
        }
    }
    if (reason.isEmpty())
        reason = kqoffice::ai::filemgr::BatchJobManager::jobStateLabelZh(job.state);

    OUString status = u"成功 "_ustr + OUString::number(summary.done) + u" / 失败 "_ustr
                      + OUString::number(summary.failed + nUnauthorized) + u" / "_ustr + reason;
    if (nUnauthorized > 0)
        status += u"（另有 "_ustr + OUString::number(nUnauthorized) + u" 个未授权路径已跳过）"_ustr;
    return status;
}

OUString extensionOfPath(const OUString& path)
{
    const sal_Int32 slash = std::max(path.lastIndexOf('/'), path.lastIndexOf('\\'));
    const sal_Int32 dot = path.lastIndexOf('.');
    if (dot < 0 || dot < slash || dot + 1 >= path.getLength())
        return {};
    return path.copy(dot + 1).toAsciiLowerCase();
}

std::optional<kqoffice::ai::filemgr::BatchJobKind> officeConvertKindForPath(const OUString& path)
{
    using kqoffice::ai::filemgr::BatchJobKind;
    using kqoffice::ai::filemgr::FileCategory;
    const FileCategory cat
        = kqoffice::ai::filemgr::AIFileManager::categorize(extensionOfPath(path));
    switch (cat)
    {
        case FileCategory::Writer:
            return BatchJobKind::ConvertToDocx;
        case FileCategory::Calc:
            return BatchJobKind::ConvertToXlsx;
        case FileCategory::Impress:
            return BatchJobKind::ConvertToPptx;
        default:
            return std::nullopt;
    }
}
} // namespace

void BackingWindow::runFileWorkbenchBatchConvert(
    kqoffice::ai::filemgr::BatchJobKind eKind, const OUString& rActionId,
    const OUString& rPromptMessageZh)
{
    using kqoffice::ai::control::ClarificationPrompt;
    using kqoffice::ai::control::ClarificationResult;
    using kqoffice::ai::control::PermissionCenter;
    using kqoffice::ai::control::PermissionDecision;
    using kqoffice::ai::filemgr::BatchJobManager;

    const std::vector<OUString> selected = selectedFileWorkbenchPaths();
    if (selected.empty())
    {
        if (mxFileWorkbenchStatus)
            mxFileWorkbenchStatus->set_label(u"请先在文件列表中选择要转换的文件"_ustr);
        return;
    }
    if (static_cast<sal_Int32>(selected.size()) > 20)
    {
        if (mxFileWorkbenchStatus)
            mxFileWorkbenchStatus->set_label(
                u"选中文件过多（"_ustr + OUString::number(static_cast<sal_Int32>(selected.size()))
                + u" 个），请分批（≤20）"_ustr);
        return;
    }

    PermissionCenter perms;
    std::vector<OUString> authorized;
    sal_Int32 nUnauthorized = 0;
    authorized.reserve(selected.size());
    for (const OUString& path : selected)
    {
        if (perms.isPathAuthorized(path))
            authorized.push_back(path);
        else
            ++nUnauthorized;
    }
    if (authorized.empty())
    {
        if (mxFileWorkbenchStatus)
            mxFileWorkbenchStatus->set_label(
                u"所选文件均未授权，无法转换（请先授权所在目录）"_ustr);
        return;
    }

    ClarificationPrompt aPrompt;
    aPrompt.actionId = rActionId;
    aPrompt.messageZh
        = rPromptMessageZh + u"\n共 "_ustr
          + OUString::number(static_cast<sal_Int32>(authorized.size())) + u" 个已授权文件"_ustr
          + (nUnauthorized > 0
                 ? (u"（另跳过 "_ustr + OUString::number(nUnauthorized) + u" 个未授权）"_ustr)
                 : OUString())
          + u"。\n目标已存在时将覆盖写入；是否继续？"_ustr;

    const ClarificationResult aPerm = sfx2::ShowPermissionPrompt(GetFrameWeld(), aPrompt);
    if (aPerm.decision == PermissionDecision::Deny)
    {
        if (mxFileWorkbenchStatus)
            mxFileWorkbenchStatus->set_label(u"已拒绝批量转换"_ustr);
        return;
    }

    if (mxFileWorkbenchStatus)
        mxFileWorkbenchStatus->set_label(u"正在批量转换…"_ustr);

    BatchJobManager mgr;
    auto job = mgr.create(eKind);
    for (const OUString& path : authorized)
        mgr.addItem(job, path);

    const auto summary = mgr.run(job, aPerm.decision);
    if (mxFileWorkbenchStatus)
        mxFileWorkbenchStatus->set_label(formatBatchRunStatusZh(summary, job, nUnauthorized));

    // Pick up newly written targets next to sources.
    if (summary.done > 0)
        refreshFileWorkbench(true);
}

void BackingWindow::runFileWorkbenchBatchConvertOffice()
{
    using kqoffice::ai::control::ClarificationPrompt;
    using kqoffice::ai::control::ClarificationResult;
    using kqoffice::ai::control::PermissionCenter;
    using kqoffice::ai::control::PermissionDecision;
    using kqoffice::ai::filemgr::BatchJobKind;
    using kqoffice::ai::filemgr::BatchJobManager;
    using kqoffice::ai::filemgr::BatchRunSummary;

    const std::vector<OUString> selected = selectedFileWorkbenchPaths();
    if (selected.empty())
    {
        if (mxFileWorkbenchStatus)
            mxFileWorkbenchStatus->set_label(u"请先在文件列表中选择要转换的文件"_ustr);
        return;
    }
    if (static_cast<sal_Int32>(selected.size()) > 20)
    {
        if (mxFileWorkbenchStatus)
            mxFileWorkbenchStatus->set_label(
                u"选中文件过多（"_ustr + OUString::number(static_cast<sal_Int32>(selected.size()))
                + u" 个），请分批（≤20）"_ustr);
        return;
    }

    PermissionCenter perms;
    std::vector<OUString> docxPaths;
    std::vector<OUString> xlsxPaths;
    std::vector<OUString> pptxPaths;
    sal_Int32 nUnauthorized = 0;
    sal_Int32 nUnsupported = 0;
    for (const OUString& path : selected)
    {
        if (!perms.isPathAuthorized(path))
        {
            ++nUnauthorized;
            continue;
        }
        const auto kind = officeConvertKindForPath(path);
        if (!kind)
        {
            ++nUnsupported;
            continue;
        }
        switch (*kind)
        {
            case BatchJobKind::ConvertToDocx:
                docxPaths.push_back(path);
                break;
            case BatchJobKind::ConvertToXlsx:
                xlsxPaths.push_back(path);
                break;
            case BatchJobKind::ConvertToPptx:
                pptxPaths.push_back(path);
                break;
            default:
                ++nUnsupported;
                break;
        }
    }

    const sal_Int32 nAuthorized = static_cast<sal_Int32>(docxPaths.size() + xlsxPaths.size()
                                                         + pptxPaths.size());
    if (nAuthorized == 0)
    {
        if (mxFileWorkbenchStatus)
        {
            if (nUnauthorized > 0 && nUnsupported == 0)
                mxFileWorkbenchStatus->set_label(
                    u"所选文件均未授权，无法转换（请先授权所在目录）"_ustr);
            else if (nUnsupported > 0 && nUnauthorized == 0)
                mxFileWorkbenchStatus->set_label(
                    u"所选文件类型不支持批量转 Office（仅 Writer/Calc/Impress 类文档）"_ustr);
            else
                mxFileWorkbenchStatus->set_label(
                    u"没有可转换的已授权 Office 文档（未授权或类型不支持）"_ustr);
        }
        return;
    }

    ClarificationPrompt aPrompt;
    aPrompt.actionId = u"batch.convert.office"_ustr;
    aPrompt.messageZh
        = u"将把选中文档批量转为 DOCX/XLSX/PPTX（按类型推断，本地写出）：\n共 "_ustr
          + OUString::number(nAuthorized) + u" 个已授权可转文件"_ustr
          + (nUnauthorized > 0
                 ? (u"，跳过未授权 "_ustr + OUString::number(nUnauthorized) + u" 个"_ustr)
                 : OUString())
          + (nUnsupported > 0
                 ? (u"，跳过不支持类型 "_ustr + OUString::number(nUnsupported) + u" 个"_ustr)
                 : OUString())
          + u"。\n目标已存在时将覆盖写入；是否继续？"_ustr;

    const ClarificationResult aPerm = sfx2::ShowPermissionPrompt(GetFrameWeld(), aPrompt);
    if (aPerm.decision == PermissionDecision::Deny)
    {
        if (mxFileWorkbenchStatus)
            mxFileWorkbenchStatus->set_label(u"已拒绝批量转换"_ustr);
        return;
    }

    if (mxFileWorkbenchStatus)
        mxFileWorkbenchStatus->set_label(u"正在批量转 Office…"_ustr);

    BatchJobManager mgr;
    BatchRunSummary total;
    OUString reasonParts;
    auto runGroup = [&](BatchJobKind kind, const std::vector<OUString>& paths) {
        if (paths.empty())
            return;
        auto job = mgr.create(kind);
        for (const OUString& path : paths)
            mgr.addItem(job, path);
        const auto summary = mgr.run(job, aPerm.decision);
        total.total += summary.total;
        total.done += summary.done;
        total.failed += summary.failed;
        total.cancelled += summary.cancelled;
        total.skipped += summary.skipped;
        if (!job.reasonZh.isEmpty())
        {
            if (!reasonParts.isEmpty())
                reasonParts += u"；"_ustr;
            reasonParts += job.reasonZh;
        }
        else
        {
            for (const auto& item : job.items)
            {
                if (item.state == kqoffice::ai::filemgr::BatchItemState::Failed
                    && !item.reasonZh.isEmpty())
                {
                    if (!reasonParts.isEmpty())
                        reasonParts += u"；"_ustr;
                    reasonParts += item.reasonZh;
                    if (reasonParts.getLength() > 160)
                    {
                        reasonParts += u"…"_ustr;
                        break;
                    }
                }
            }
        }
    };
    runGroup(BatchJobKind::ConvertToDocx, docxPaths);
    runGroup(BatchJobKind::ConvertToXlsx, xlsxPaths);
    runGroup(BatchJobKind::ConvertToPptx, pptxPaths);

    total.failed += nUnauthorized + nUnsupported;
    total.allSucceeded = (total.failed == 0 && total.cancelled == 0 && total.done == total.total
                          && nUnauthorized == 0 && nUnsupported == 0);
    if (reasonParts.isEmpty())
        reasonParts = total.allSucceeded ? u"全部完成"_ustr : u"部分失败或已跳过"_ustr;
    if (nUnauthorized > 0)
        reasonParts += u"；未授权 "_ustr + OUString::number(nUnauthorized);
    if (nUnsupported > 0)
        reasonParts += u"；类型不支持 "_ustr + OUString::number(nUnsupported);

    if (mxFileWorkbenchStatus)
    {
        mxFileWorkbenchStatus->set_label(u"成功 "_ustr + OUString::number(total.done)
                                         + u" / 失败 "_ustr + OUString::number(total.failed)
                                         + u" / "_ustr + reasonParts);
    }

    if (total.done > 0)
        refreshFileWorkbench(true);
}

void BackingWindow::openFileWorkbenchSelection()
{
    const std::vector<OUString> paths = selectedFileWorkbenchPaths();
    if (paths.empty())
        return;

    Reference<XDispatchProvider> xFrame(mxFrame, UNO_QUERY);
    // Open each selected path (multi-select); historically single-click open
    // used only the primary selection — batch open follows the same dispatch.
    for (const OUString& path : paths)
    {
        OUString url;
        if (path.startsWith("file://"))
            url = path;
        else if (osl::FileBase::getFileURLFromSystemPath(path, url) != osl::FileBase::E_None
                 || url.isEmpty())
            url = path;
        dispatchURL(url, u"_default"_ustr, xFrame);
    }
}

IMPL_LINK_NOARG(BackingWindow, FileWorkbenchAuthorizeHdl, weld::Button&, void)
{
    try
    {
        auto xPicker = sfx2::createFolderPicker(mxContext, GetFrameWeld());
        if (!xPicker.is())
            return;
        if (xPicker->execute() != css::ui::dialogs::ExecutableDialogResults::OK)
            return;

        OUString dirUrl = xPicker->getDirectory();
        OUString systemPath;
        if (osl::FileBase::getSystemPathFromFileURL(dirUrl, systemPath) != osl::FileBase::E_None
            || systemPath.isEmpty())
            systemPath = dirUrl;

        kqoffice::ai::control::PermissionCenter perms;
        perms.grantDirectory(systemPath, true);
        if (mxFileWorkbenchStatus)
            mxFileWorkbenchStatus->set_label(u"已授权: "_ustr + systemPath);
        renderPermissionPanel();
        refreshFileWorkbench(true);
    }
    catch (const Exception&)
    {
        TOOLS_WARN_EXCEPTION("sfx", "FileWorkbenchAuthorizeHdl");
    }
}

IMPL_LINK_NOARG(BackingWindow, FileWorkbenchRevokeHdl, weld::Button&, void)
{
    OUString path = selectedAuthorizedDirectory();
    if (path.isEmpty())
    {
        if (mxFileWorkbenchStatus)
            mxFileWorkbenchStatus->set_label(u"请先在「已授权目录」列表中选择一项再撤销"_ustr);
        return;
    }
    kqoffice::ai::control::PermissionCenter perms;
    if (!perms.revokeDirectory(path))
    {
        if (mxFileWorkbenchStatus)
            mxFileWorkbenchStatus->set_label(u"撤销失败: "_ustr + path);
        return;
    }
    if (mxFileWorkbenchStatus)
        mxFileWorkbenchStatus->set_label(u"已撤销目录授权: "_ustr + path);
    renderPermissionPanel();
    refreshFileWorkbench(true);
}

IMPL_LINK_NOARG(BackingWindow, FileWorkbenchNetworkRevokeHdl, weld::Button&, void)
{
    kqoffice::ai::control::PermissionCenter perms;
    perms.revoke(kqoffice::ai::control::CapabilityPermission::NetworkEgress);
    if (mxFileWorkbenchStatus)
        mxFileWorkbenchStatus->set_label(u"已关闭网络授权 · 外发需重新确认"_ustr);
    renderPermissionPanel();
}

IMPL_LINK_NOARG(BackingWindow, FileWorkbenchRefreshHdl, weld::Button&, void)
{
    refreshFileWorkbench(true);
}

IMPL_LINK_NOARG(BackingWindow, FileWorkbenchPinHdl, weld::Button&, void)
{
    const OUString path = selectedFileWorkbenchPath();
    if (path.isEmpty())
        return;
    kqoffice::ai::filemgr::AIFileManager mgr;
    const bool nowPinned = !mgr.isPinned(path);
    mgr.pin(path, nowPinned);
    refreshFileWorkbench(false);
}

IMPL_LINK_NOARG(BackingWindow, FileWorkbenchTagHdl, weld::Button&, void)
{
    const auto paths = selectedFileWorkbenchPaths();
    if (paths.empty())
    {
        if (mxFileWorkbenchStatus)
            mxFileWorkbenchStatus->set_label(u"请先选择要打标签的文件"_ustr);
        return;
    }
    const OUString tag
        = mxFileWorkbenchTag ? mxFileWorkbenchTag->get_text().trim() : OUString();
    // Empty tag clears existing tags on selection.
    kqoffice::ai::filemgr::AIFileManager mgr;
    sal_Int32 nOk = 0;
    for (const OUString& path : paths)
    {
        if (path.isEmpty())
            continue;
        if (mgr.setTag(path, tag))
            ++nOk;
    }
    refreshFileWorkbench(false);
    if (mxFileWorkbenchStatus)
    {
        if (nOk <= 0)
            mxFileWorkbenchStatus->set_label(u"打标签失败"_ustr);
        else if (tag.isEmpty())
            mxFileWorkbenchStatus->set_label(u"已清除 "_ustr + OUString::number(nOk)
                                             + u" 个文件的标签"_ustr);
        else
            mxFileWorkbenchStatus->set_label(u"已为 "_ustr + OUString::number(nOk)
                                             + u" 个文件打标签「"_ustr + tag + u"」"_ustr);
    }
}

IMPL_LINK_NOARG(BackingWindow, FileWorkbenchOpenHdl, weld::Button&, void)
{
    openFileWorkbenchSelection();
}

IMPL_LINK_NOARG(BackingWindow, FileWorkbenchDeleteHdl, weld::Button&, void)
{
    const OUString path = selectedFileWorkbenchPath();
    if (path.isEmpty())
    {
        if (mxFileWorkbenchStatus)
            mxFileWorkbenchStatus->set_label(u"请先在文件列表中选择要移入回收站的项"_ustr);
        return;
    }

    kqoffice::ai::control::PermissionCenter perms;
    const kqoffice::ai::control::RiskDecision risk
        = perms.evaluateRisk(kqoffice::ai::control::RiskOperation::Delete, path);
    if (!risk.pathAuthorized)
    {
        if (mxFileWorkbenchStatus)
            mxFileWorkbenchStatus->set_label(
                risk.reasonZh.isEmpty()
                    ? (u"路径未授权，无法移入回收站: "_ustr + path)
                    : risk.reasonZh);
        return;
    }

    // Align prompt actionId with PermissionCenter risk session key (fs.delete@root)
    // so 本轮对话均允许 short-circuits both the dialog and resolveRiskyOp.
    kqoffice::ai::control::ClarificationPrompt aPrompt;
    aPrompt.actionId = risk.sessionActionId.isEmpty() ? u"file.workbench.delete"_ustr
                                                      : risk.sessionActionId;
    aPrompt.messageZh
        = risk.reasonZh.isEmpty()
              ? (u"将把选中文件移入产品回收站（软删除，可恢复）：\n"_ustr + path
                 + u"\n是否继续？"_ustr)
              : risk.reasonZh;

    const kqoffice::ai::control::ClarificationResult aPerm
        = sfx2::ShowPermissionPrompt(GetFrameWeld(), aPrompt);
    if (aPerm.decision == kqoffice::ai::control::PermissionDecision::Deny)
    {
        if (mxFileWorkbenchStatus)
            mxFileWorkbenchStatus->set_label(u"已拒绝移入回收站"_ustr);
        return;
    }

    kqoffice::ai::filemgr::AIFileManager mgr;
    if (mgr.moveToTrash(path, u"workbench-delete"_ustr, aPerm.decision))
    {
        if (mxFileWorkbenchStatus)
            mxFileWorkbenchStatus->set_label(u"已移入回收站: "_ustr + path);
        refreshFileWorkbench(true);
    }
    else
    {
        if (mxFileWorkbenchStatus)
            mxFileWorkbenchStatus->set_label(u"移入回收站失败: "_ustr + path);
    }
}

IMPL_LINK_NOARG(BackingWindow, FileWorkbenchExportPdfHdl, weld::Button&, void)
{
    runFileWorkbenchBatchConvert(
        kqoffice::ai::filemgr::BatchJobKind::ConvertToPdf, u"batch.convert.pdf"_ustr,
        u"将把选中文件批量导出为 PDF（本地转换，不上传）："_ustr);
}

IMPL_LINK_NOARG(BackingWindow, FileWorkbenchExportOfficeHdl, weld::Button&, void)
{
    runFileWorkbenchBatchConvertOffice();
}

IMPL_LINK_NOARG(BackingWindow, FileWorkbenchSearchHdl, weld::Entry&, void)
{
    refreshFileWorkbench(false);
}

IMPL_LINK_NOARG(BackingWindow, FileWorkbenchRowActivatedHdl, weld::TreeView&, bool)
{
    openFileWorkbenchSelection();
    return true;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab:*/
