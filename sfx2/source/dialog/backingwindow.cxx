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
#include <vcl/weld/Menu.hxx>

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
#include <rtl/ustrbuf.hxx>

#include <DocumentAIScenarioStore.hxx>

#include <config_folders.h>
#include <cstdlib>
#include <string>
#include <string_view>

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
    , mxCreateLabel(m_xBuilder->weld_label(u"create_label"_ustr))
    , mxAllRecentLabel(m_xBuilder->weld_label(u"all_recent_label"_ustr))
    , mxLocalViewLabel(m_xBuilder->weld_label(u"local_view_label"_ustr))
    , mxScenarioLabel(m_xBuilder->weld_label(u"scenario_label"_ustr))
    , mxScenarioWriterGroup(m_xBuilder->weld_label(u"scenario_writer_group"_ustr))
    , mxScenarioCalcGroup(m_xBuilder->weld_label(u"scenario_calc_group"_ustr))
    , mxScenarioImpressGroup(m_xBuilder->weld_label(u"scenario_impress_group"_ustr))
    , mxScenarioCompatGroup(m_xBuilder->weld_label(u"scenario_compat_group"_ustr))
    , mxScenarioFallbackHint(m_xBuilder->weld_label(u"scenario_fallback_hint"_ustr))
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
    , mxBrandImage(new BrandImage)
    , mxBrandImageWeld(new weld::CustomWeld(*m_xBuilder, u"daBrand"_ustr, *mxBrandImage))
    , mxHelpButton(m_xBuilder->weld_button(u"help"_ustr))
    , mxExtensionsButton(m_xBuilder->weld_button(u"extensions"_ustr))
    , mxAllButtonsBox(m_xBuilder->weld_container(u"all_buttons_box"_ustr))
    , mxButtonsBox(m_xBuilder->weld_container(u"buttons_box"_ustr))
    , mxSmallButtonsBox(m_xBuilder->weld_container(u"small_buttons_box"_ustr))
    , mxAllRecentThumbnails(new sfx2::RecentDocsView(m_xBuilder->weld_scrolled_window(u"scrollrecent"_ustr, true)))
    , mxAllRecentThumbnailsWin(new weld::CustomWeld(*m_xBuilder, u"all_recent"_ustr, *mxAllRecentThumbnails))
    , mxLocalView(new TemplateDefaultView(m_xBuilder->weld_scrolled_window(u"scrolllocal"_ustr, true),
                                          m_xBuilder->weld_menu(u"localmenu"_ustr)))
    , mxLocalViewWin(new weld::CustomWeld(*m_xBuilder, u"local_view"_ustr, *mxLocalView))
    , mbLocalViewInitialized(false)
    , mbInitControls(false)
{
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

    mxDropTarget = mxAllRecentThumbnails->GetDropTarget();

    try
    {
        mxContext.set( ::comphelper::getProcessComponentContext(), uno::UNO_SET_THROW );
    }
    catch (const Exception&)
    {
        TOOLS_WARN_EXCEPTION( "fwk", "BackingWindow" );
    }

    SetStyle( GetStyle() | WB_DIALOGCONTROL );

    // get dispatch provider
    Reference<XDesktop2> xDesktop = Desktop::create( comphelper::getProcessComponentContext() );
    mxDesktopDispatchProvider = xDesktop;

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
    // deregister drag&drop helper
    if (mxDropTargetListener.is())
    {
        if (mxDropTarget.is())
        {
            mxDropTarget->removeDropTargetListener(mxDropTargetListener);
            mxDropTarget->setActive(false);
        }
        mxDropTargetListener.clear();
    }
    mxDropTarget.clear();
    mxOpenButton.reset();
    mxRemoteButton.reset();
    mxRecentButton.reset();
    mxTemplateButton.reset();
    mxCreateLabel.reset();
    mxAllRecentLabel.reset();
    mxLocalViewLabel.reset();
    mxScenarioLabel.reset();
    mxScenarioWriterGroup.reset();
    mxScenarioCalcGroup.reset();
    mxScenarioImpressGroup.reset();
    mxScenarioCompatGroup.reset();
    mxScenarioFallbackHint.reset();
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

    // collect the URLs of the entries in the File/New menu
    SvtModuleOptions    aModuleOptions;

    if (aModuleOptions.IsWriterInstalled())
        mxAllRecentThumbnails->mnFileTypes |= sfx2::ApplicationType::TYPE_WRITER;

    if (aModuleOptions.IsCalcInstalled())
        mxAllRecentThumbnails->mnFileTypes |= sfx2::ApplicationType::TYPE_CALC;

    if (aModuleOptions.IsImpressInstalled())
        mxAllRecentThumbnails->mnFileTypes |= sfx2::ApplicationType::TYPE_IMPRESS;

    mxAllRecentThumbnails->Reload();
    mxAllRecentThumbnails->ShowTooltips( true );

    mxTemplateButton->set_active(true);
    ToggleHdl(*mxTemplateButton);

    //set handlers
    mxLocalView->setCreateContextMenuHdl(LINK(this, BackingWindow, CreateContextMenuHdl));
    mxLocalView->setOpenTemplateHdl(LINK(this, BackingWindow, OpenTemplateHdl));
    mxLocalView->setEditTemplateHdl(LINK(this, BackingWindow, EditTemplateHdl));
    mxLocalView->ShowTooltips( true );

    checkInstalledModules();

    mxExtensionsButton->connect_clicked(LINK(this, BackingWindow, ExtLinkClickHdl));

    mxOpenButton->connect_clicked(LINK(this, BackingWindow, ClickHdl));

    // Hide OpenRemote button on startpage if the OpenRemote uno command is not available
    SvtCommandOptions aCmdOptions;
    if (SvtCommandOptions().HasEntriesDisabled() && aCmdOptions.LookupDisabled(u"OpenRemote"_ustr))
        mxRemoteButton->set_visible(false);
    else
        mxRemoteButton->connect_clicked(LINK(this, BackingWindow, ClickHdl));

    mxWriterAllButton->connect_clicked(LINK(this, BackingWindow, ClickHdl));
    mxDrawAllButton->connect_clicked(LINK(this, BackingWindow, ClickHdl));
    mxCalcAllButton->connect_clicked(LINK(this, BackingWindow, ClickHdl));
    mxDBAllButton->connect_clicked(LINK(this, BackingWindow, ClickHdl));
    mxImpressAllButton->connect_clicked(LINK(this, BackingWindow, ClickHdl));
    mxMathAllButton->connect_clicked(LINK(this, BackingWindow, ClickHdl));
    if (mxAiDraftWriterButton)
        mxAiDraftWriterButton->connect_clicked(LINK(this, BackingWindow, AiDraftHdl));
    if (mxAiDraftCalcButton)
        mxAiDraftCalcButton->connect_clicked(LINK(this, BackingWindow, AiDraftHdl));
    if (mxAiDraftImpressButton)
        mxAiDraftImpressButton->connect_clicked(LINK(this, BackingWindow, AiDraftHdl));
    for (const auto& rScenario : getScenarioTemplates())
    {
        if (rScenario.pButton)
            rScenario.pButton->connect_clicked(LINK(this, BackingWindow, OpenScenarioHdl));
    }
    if (mxScenarioCompatOpenButton)
        mxScenarioCompatOpenButton->connect_clicked(LINK(this, BackingWindow, OpenCompatibilityHdl));

    mxRecentButton->connect_toggled(LINK(this, BackingWindow, ToggleHdl));
    mxTemplateButton->connect_toggled(LINK(this, BackingWindow, ToggleHdl));

    mxFilter->connect_changed(LINK(this, BackingWindow, FilterHdl));
    mxActions->connect_selected(LINK(this, BackingWindow, MenuSelectHdl));

    ApplyStyleSettings();
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

    // Now set a brand image wide enough to fill this width
    weld::DrawingArea* pDrawingArea = mxBrandImage->GetDrawingArea();
    mxBrandImage->ConfigureForWidth(aPrefSize.Width() -
                                    (pDrawingArea->get_margin_start() + pDrawingArea->get_margin_end()));
    // Refetch because the brand image height to match this width is now set
    aPrefSize = mxAllButtonsBox->get_preferred_size();

    set_height_request(nMenuHeight + aPrefSize.Height() + mxBrandImage->getSize().getHeight());
}

void BackingWindow::initializeLocalView()
{
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
    // Keep remote and secondary modules out of the first screen, while allowing
    // Writer, Calc, and Impress task flows to define the native workbench.
    mxRemoteButton->set_visible(false);
    mxExtensionsButton->set_visible(false);
    mxDrawAllButton->set_visible(false);
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
    const bool bPresentationTasksAvailable = bWriterInstalled || bImpressInstalled;

    mxWriterAllButton->set_sensitive(bWriterInstalled);
    mxCalcAllButton->set_sensitive(bCalcInstalled);
    mxImpressAllButton->set_sensitive(bImpressInstalled);
    mxDrawAllButton->set_sensitive(aModuleOpt.IsDrawInstalled());
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

        bool bThumbnailHasFocus = mxAllRecentThumbnails->HasFocus() || mxLocalView->HasFocus();

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
                if(mxAllRecentThumbnails->IsVisible())
                {
                    mxAllRecentThumbnails->GrabFocus();
                    return true;
                }
                else if(mxLocalView->IsVisible())
                {
                    mxLocalView->GrabFocus();
                    return true;
                }
            }
            else // F6
            {
                if (!bThumbnailHasFocus)
                {
                    if(mxAllRecentThumbnails->IsVisible())
                    {
                        mxAllRecentThumbnails->GrabFocus();
                        return true;
                    }
                    else if(mxLocalView->IsVisible())
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
            if(mxAllRecentThumbnails->IsVisible())
                mxAllRecentThumbnails->GrabFocus();
            else if(mxLocalView->IsVisible())
                mxLocalView->GrabFocus();
            return;
        }
    }
    InterimItemWindow::GetFocus();
}

void BackingWindow::setOwningFrame( const css::uno::Reference< css::frame::XFrame >& xFrame )
{
    mxFrame = xFrame;
    if( ! mbInitControls )
        initControls();

    // establish drag&drop mode
    mxDropTargetListener.set(new OpenFileDropTargetListener(mxContext, mxFrame));

    if (mxDropTarget.is())
    {
        mxDropTarget->addDropTargetListener(mxDropTargetListener);
        mxDropTarget->setActive(true);
    }

    css::uno::Reference<XFramesSupplier> xFramesSupplier(mxDesktopDispatchProvider, UNO_QUERY);
    if (xFramesSupplier)
        xFramesSupplier->setActiveFrame(mxFrame);
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
    const int nFilter = mxFilter->get_active();
    if (mxLocalView->IsVisible())
        mxLocalView->filterItems(ViewFilter_Application(lclGetTemplateFilter(nFilter)));
    else
        mxAllRecentThumbnails->setFilter(lclGetRecentFilter(nFilter));

    if (mxScenarioFallbackHint)
        mxScenarioFallbackHint->hide();
}

IMPL_LINK_NOARG( BackingWindow, FilterHdl, weld::ComboBox&, void )
{
    applyFilter();
}

IMPL_LINK( BackingWindow, ToggleHdl, weld::Toggleable&, rButton, void )
{
    if (&rButton == mxRecentButton.get())
    {
        mxRecentButton->set_active(true);
        mxAllRecentLabel->show();
        mxLocalViewLabel->hide();
        mxLocalView->Hide();
        mxAllRecentThumbnails->Show();
        mxAllRecentThumbnails->GrabFocus();
        mxTemplateButton->set_active(false);
        mxActions->show();
    }
    else
    {
        mxTemplateButton->set_active(true);
        mxAllRecentLabel->hide();
        mxLocalViewLabel->show();
        mxAllRecentThumbnails->Hide();
        initializeLocalView();
        mxLocalView->Show();
        mxLocalView->reload();
        mxLocalView->GrabFocus();
        mxRecentButton->set_active(false);
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
        dispatchURL( u"private:factory/sdraw"_ustr );
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
        mxAllRecentThumbnails->Reload();
        return;
    }
    else if(rId == "clear_unavailable")
    {
        mxAllRecentThumbnails->clearUnavailableFiles();
    }
}

IMPL_LINK(BackingWindow, CreateContextMenuHdl, TemplateViewItem*, pItem, void)
{
    if (!pItem)
        return;

    bool bIsInternal = TemplateLocalView::IsInternalTemplate(pItem->getPath());

    mxLocalView->createContextMenu(bIsInternal);

}

IMPL_LINK(BackingWindow, OpenTemplateHdl, const OUString&, rTemplatePath, void)
{
    uno::Sequence< PropertyValue > aArgs{
        comphelper::makePropertyValue(u"AsTemplate"_ustr, true),
        comphelper::makePropertyValue(u"MacroExecutionMode"_ustr, MacroExecMode::USE_CONFIG),
        comphelper::makePropertyValue(u"UpdateDocMode"_ustr, UpdateDocMode::ACCORDING_TO_CONFIG),
        comphelper::makePropertyValue(u"InteractionHandler"_ustr, task::InteractionHandler::createWithParent( ::comphelper::getProcessComponentContext(), nullptr ))
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
    if (mxScenarioFallbackHint)
        mxScenarioFallbackHint->show();
}

bool BackingWindow::resolveTemplatePathByFileName(const SfxDocumentTemplates& rTemplates,
                                                  std::u16string_view rTemplateFileName,
                                                  OUString& rTemplatePath)
{
    if (rTemplateFileName.empty())
        return false;

    const sal_uInt16 nRegionCount = rTemplates.GetRegionCount();
    for (sal_uInt16 nRegion = 0; nRegion < nRegionCount; ++nRegion)
    {
        const sal_uInt16 nTemplateCount = rTemplates.GetCount(nRegion);
        for (sal_uInt16 nTemplate = 0; nTemplate < nTemplateCount; ++nTemplate)
        {
            OUString aCandidatePath = rTemplates.GetPath(nRegion, nTemplate);
            if (!aCandidatePath.isEmpty() && aCandidatePath.endsWithIgnoreAsciiCase(rTemplateFileName))
            {
                rTemplatePath = std::move(aCandidatePath);
                return true;
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
    if (resolveTemplatePathByFileName(aTemplates, rTemplateFileName, aTemplatePath)
        || (!rFallbackTitle.empty() && aTemplates.GetFull(u"", rFallbackTitle, aTemplatePath)))
    {
        if (!aTemplatePath.isEmpty())
        {
            OpenTemplateHdl(aTemplatePath);
            return;
        }
    }

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
    mxAllRecentThumbnails->Clear();
    // tdf#166349 - reload recent documents to show pinned items
    mxAllRecentThumbnails->Reload();
}
/* vim:set shiftwidth=4 softtabstop=4 expandtab:*/
