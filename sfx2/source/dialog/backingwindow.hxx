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

#ifndef INCLUDED_SFX2_SOURCE_DIALOG_BACKINGWINDOW_HXX
#define INCLUDED_SFX2_SOURCE_DIALOG_BACKINGWINDOW_HXX

#include <rtl/ustring.hxx>

#include <vcl/InterimItemWindow.hxx>
#include <vcl/weld/ComboBox.hxx>
#include <vcl/weld/Entry.hxx>
#include <vcl/weld/MenuButton.hxx>
#include <vcl/weld/TreeView.hxx>
#include <vcl/idle.hxx>
#include <vcl/timer.hxx>

struct ImplSVEvent;

#include <AIFileManager.hxx>
#include <BatchJob.hxx>

#include <recentdocsview.hxx>
#include <templatedefaultview.hxx>

#include <svtools/acceleratorexecute.hxx>

#include <com/sun/star/datatransfer/dnd/XDropTargetListener.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <com/sun/star/frame/XDispatchProvider.hpp>
#include <com/sun/star/frame/XFrame.hpp>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

class BrandImage;
class SfxDocumentTemplates;
class FileWorkbenchScanThread;

class BackingWindow : public InterimItemWindow
{
    friend class FileWorkbenchScanThread;

    css::uno::Reference<css::uno::XComponentContext> mxContext;
    css::uno::Reference<css::frame::XDispatchProvider> mxDesktopDispatchProvider;
    css::uno::Reference<css::frame::XFrame> mxFrame;

    /** helper for drag&drop. */
    css::uno::Reference<css::datatransfer::dnd::XDropTargetListener> mxDropTargetListener;

    std::unique_ptr<weld::Button> mxOpenButton;
    std::unique_ptr<weld::ToggleButton> mxRecentButton;
    std::unique_ptr<weld::Button> mxRemoteButton;
    std::unique_ptr<weld::ToggleButton> mxTemplateButton;
    std::unique_ptr<weld::ToggleButton> mxFileWorkbenchButton;

    std::unique_ptr<weld::Label> mxCreateLabel;
    std::unique_ptr<weld::Label> mxAllRecentLabel;
    std::unique_ptr<weld::Label> mxLocalViewLabel;
    std::unique_ptr<weld::Label> mxFileWorkbenchLabel;
    std::unique_ptr<weld::Container> mxFileWorkbenchBox;
    std::unique_ptr<weld::Entry> mxFileWorkbenchSearch;
    std::unique_ptr<weld::Button> mxFileWorkbenchAuthorize;
    std::unique_ptr<weld::Button> mxFileWorkbenchRevoke;
    std::unique_ptr<weld::Button> mxFileWorkbenchNetworkRevoke;
    std::unique_ptr<weld::Button> mxFileWorkbenchRefresh;
    std::unique_ptr<weld::Button> mxFileWorkbenchPin;
    std::unique_ptr<weld::Entry> mxFileWorkbenchTag;
    std::unique_ptr<weld::Button> mxFileWorkbenchTagBtn;
    std::unique_ptr<weld::Button> mxFileWorkbenchOpen;
    std::unique_ptr<weld::Button> mxFileWorkbenchDelete;
    std::unique_ptr<weld::Button> mxFileWorkbenchExportPdf;
    std::unique_ptr<weld::Button> mxFileWorkbenchExportOffice;
    std::unique_ptr<weld::Label> mxFileWorkbenchStatus;
    std::unique_ptr<weld::Label> mxFileWorkbenchNetworkStatus;
    std::unique_ptr<weld::Label> mxFileWorkbenchCapStatus;
    std::unique_ptr<weld::Label> mxFileWorkbenchRiskHint;
    std::unique_ptr<weld::TreeView> mxFileWorkbenchAuthTree;
    std::unique_ptr<weld::TreeView> mxFileWorkbenchTree;
    std::unique_ptr<weld::Label> mxScenarioLabel;
    std::unique_ptr<weld::Label> mxScenarioWriterGroup;
    std::unique_ptr<weld::Label> mxScenarioCalcGroup;
    std::unique_ptr<weld::Label> mxScenarioImpressGroup;
    std::unique_ptr<weld::Label> mxScenarioCompatGroup;
    std::unique_ptr<weld::Label> mxScenarioFallbackHint;
    std::unique_ptr<weld::Entry> mxTemplateSearch;
    std::unique_ptr<weld::ComboBox> mxTemplateCategory;
    std::unique_ptr<weld::TreeView> mxTemplateMarketTree;
    std::unique_ptr<weld::Label> mxAltHelpLabel;
    std::unique_ptr<weld::ComboBox> mxFilter;
    std::unique_ptr<weld::MenuButton> mxActions;

    std::unique_ptr<weld::Button> mxWriterAllButton;
    std::unique_ptr<weld::Button> mxCalcAllButton;
    std::unique_ptr<weld::Button> mxImpressAllButton;
    std::unique_ptr<weld::Button> mxDrawAllButton;
    std::unique_ptr<weld::Button> mxDBAllButton;
    std::unique_ptr<weld::Button> mxMathAllButton;
    std::unique_ptr<weld::Label> mxAiCreateLabel;
    std::unique_ptr<weld::Button> mxAiDraftWriterButton;
    std::unique_ptr<weld::Button> mxAiDraftCalcButton;
    std::unique_ptr<weld::Button> mxAiDraftImpressButton;
    std::unique_ptr<weld::Container> mxScenarioBox;
    std::unique_ptr<weld::Button> mxScenarioReportButton;
    std::unique_ptr<weld::Button> mxScenarioMinutesButton;
    std::unique_ptr<weld::Button> mxScenarioNoticeButton;
    std::unique_ptr<weld::Button> mxScenarioPlanButton;
    std::unique_ptr<weld::Button> mxScenarioOutlineButton;
    std::unique_ptr<weld::Button> mxScenarioBudgetButton;
    std::unique_ptr<weld::Button> mxScenarioSalesButton;
    std::unique_ptr<weld::Button> mxScenarioScheduleButton;
    std::unique_ptr<weld::Button> mxScenarioPitchButton;
    std::unique_ptr<weld::Button> mxScenarioProjectReportButton;
    std::unique_ptr<weld::Button> mxScenarioCompatOpenButton;
    struct ScenarioTemplate
    {
        weld::Button* pButton;
        std::u16string_view aFileName;
        std::u16string_view aFallbackTitle;
        FILTER_APPLICATION eFilter;
    };

    std::unique_ptr<weld::Button> mxScenarioCoursewareButton;
    std::unique_ptr<BrandImage> mxBrandImage;
    std::unique_ptr<weld::CustomWeld> mxBrandImageWeld;

    std::unique_ptr<weld::Button> mxHelpButton;
    std::unique_ptr<weld::Button> mxExtensionsButton;

    std::unique_ptr<weld::Container> mxAllButtonsBox;
    std::unique_ptr<weld::Container> mxButtonsBox;
    std::unique_ptr<weld::Container> mxSmallButtonsBox;

    std::unique_ptr<sfx2::RecentDocsView> mxAllRecentThumbnails;
    std::unique_ptr<weld::CustomWeld> mxAllRecentThumbnailsWin;
    std::unique_ptr<TemplateDefaultView> mxLocalView;
    std::unique_ptr<weld::CustomWeld> mxLocalViewWin;

    bool mbLocalViewInitialized;
    bool mbSecondaryInitDone = false;
    /// Idle (DEFAULT_IDLE) for DeferredSecondaryInitHdl — after first paint; Stop in dispose.
    Idle maDeferredSecondaryInitIdle;
    /// Fallback PostUserEvent id when KQOFFICE_DEFSEC_IDLE=0; cleared in handler / dispose.
    ImplSVEvent* mpDeferredSecondaryInitEvent = nullptr;

    /// Drop targets registered for open-file (recent + local view + workbench tree).
    std::vector<css::uno::Reference<css::datatransfer::dnd::XDropTarget>> mxDropTargets;

    void attachOpenFileDrop(const css::uno::Reference<css::datatransfer::dnd::XDropTarget>& xDrop);

    bool mbInitControls;
    /// One-shot first Paint log for KQOFFICE_STARTUP_TIMING (cold-start segments).
    bool mbFirstPaintLogged = false;
    std::unique_ptr<svt::AcceleratorExecute> mpAccExec;

    std::unique_ptr<FileWorkbenchScanThread> mxFileWorkbenchScanThread;
    AutoTimer maFileWorkbenchPollTimer;
    std::atomic_bool mbFileWorkbenchScanCancelled{ false };
    std::mutex maFileWorkbenchResultMutex;
    std::vector<kqoffice::ai::filemgr::FileEntry> maFileWorkbenchFiles;
    sal_Int64 mnFileWorkbenchScanDurationMs = 0;
    sal_Int32 mnFileWorkbenchAuthorizedRootCount = 0;
    bool mbFileWorkbenchScanReady = false;

    void dispatchURL(const OUString& i_rURL, const OUString& i_rTarget = u"_default"_ustr,
                     const css::uno::Reference<css::frame::XDispatchProvider>& i_xProv
                     = css::uno::Reference<css::frame::XDispatchProvider>(),
                     const css::uno::Sequence<css::beans::PropertyValue>& = css::uno::Sequence<
                         css::beans::PropertyValue>());

    DECL_LINK(ToggleHdl, weld::Toggleable&, void);
    DECL_LINK(FilterHdl, weld::ComboBox&, void);
    DECL_LINK(ClickHdl, weld::Button&, void);
    DECL_LINK(ClickHelpHdl, weld::Button&, void);
    DECL_LINK(MenuSelectHdl, const OUString&, void);
    DECL_STATIC_LINK(BackingWindow, ExtLinkClickHdl, weld::Button&, void);
    DECL_LINK(CreateContextMenuHdl, TemplateViewItem*, void);
    DECL_LINK(OpenTemplateHdl, const OUString&, void);
    DECL_LINK(EditTemplateHdl, const OUString&, void);
    DECL_LINK(OpenScenarioHdl, weld::Button&, void);
    DECL_LINK(OpenCompatibilityHdl, weld::Button&, void);
    DECL_LINK(AiDraftHdl, weld::Button&, void);
    DECL_LINK(FileWorkbenchAuthorizeHdl, weld::Button&, void);
    DECL_LINK(FileWorkbenchRevokeHdl, weld::Button&, void);
    DECL_LINK(FileWorkbenchNetworkRevokeHdl, weld::Button&, void);
    DECL_LINK(FileWorkbenchRefreshHdl, weld::Button&, void);
    DECL_LINK(FileWorkbenchPinHdl, weld::Button&, void);
    DECL_LINK(FileWorkbenchTagHdl, weld::Button&, void);
    DECL_LINK(FileWorkbenchOpenHdl, weld::Button&, void);
    DECL_LINK(FileWorkbenchDeleteHdl, weld::Button&, void);
    DECL_LINK(FileWorkbenchExportPdfHdl, weld::Button&, void);
    DECL_LINK(FileWorkbenchExportOfficeHdl, weld::Button&, void);
    DECL_LINK(FileWorkbenchSearchHdl, weld::Entry&, void);
    DECL_LINK(FileWorkbenchRowActivatedHdl, weld::TreeView&, bool);
    DECL_LINK(FileWorkbenchPollHdl, Timer*, void);
    /// Decode recent-doc thumbnails after first paint (cold-start).
    DECL_LINK(DeferredRecentReloadHdl, void*, void);
    /// Populate template LocalView (disk scan + thumbnails) after first paint.
    DECL_LINK(DeferredTemplateInitHdl, void*, void);
    /// Wire secondary handlers + style + content after first paint (Idle or PostUserEvent).
    DECL_LINK(DeferredSecondaryInitIdleHdl, Timer*, void);
    DECL_LINK(DeferredSecondaryInitHdl, void*, void);
    void runDeferredSecondaryInit();
    DECL_LINK(TemplateSearchHdl, weld::Entry&, void);
    DECL_LINK(TemplateCategoryHdl, weld::ComboBox&, void);
    DECL_LINK(TemplateMarketActivateHdl, weld::TreeView&, bool);

    void initControls();

    /// Lazy-create heavy CustomWeld views (cold-start: not in ctor).
    void ensureRecentThumbnails();
    void ensureLocalView();
    void ensureBrandImage();
    void ensureDesktopDispatch();

    void initializeLocalView();
    void refreshTemplateMarket();
    void openTemplateMarketSelection();
    /// PDF tools (L1–L2): open / page-info / merge&split guided workflows — not Acrobat.
    void runPdfTool(std::u16string_view rToolId);
    void showFileWorkbenchPane(bool bShow);
    void refreshFileWorkbench(bool bRescan = true);
    void startFileWorkbenchScan();
    void cancelFileWorkbenchScan();
    void renderFileWorkbench();
    void renderPermissionPanel();
    void openFileWorkbenchSelection();
    OUString selectedFileWorkbenchPath() const;
    /// Paths of all selected workbench tree rows (multi-select aware).
    std::vector<OUString> selectedFileWorkbenchPaths() const;
    /// Shared batch convert flow for PDF / Office export buttons.
    void runFileWorkbenchBatchConvert(
        kqoffice::ai::filemgr::BatchJobKind eKind, const OUString& rActionId,
        const OUString& rPromptMessageZh);
    /// Office batch: group selection by target kind (DOCX/XLSX/PPTX) and run.
    void runFileWorkbenchBatchConvertOffice();
    OUString selectedAuthorizedDirectory() const;

    void checkInstalledModules();
    bool resolveTemplatePathByFileName(const SfxDocumentTemplates& rTemplates,
                                       std::u16string_view rTemplateFileName,
                                       OUString& rTemplatePath);
    void showTemplateHub(FILTER_APPLICATION eFilter);
    std::array<ScenarioTemplate, 11> getScenarioTemplates();
    void openScenarioTemplate(std::u16string_view rTemplateFileName,
                              std::u16string_view rFallbackTitle,
                              FILTER_APPLICATION eFilter);
    /// Queue AI scenario + open blank factory doc (Writer/Calc/Impress).
    void openAiDraft(std::u16string_view rScenarioId, const OUString& rFactoryUrl);

    void DataChanged(const DataChangedEvent&) override;

    template <typename WidgetClass> void setLargerFont(WidgetClass&, const vcl::Font&);
    void ApplyStyleSettings();

private:
    void applyFilter();

public:
    explicit BackingWindow(vcl::Window* pParent);
    virtual ~BackingWindow() override;
    virtual void dispose() override;

    virtual void Paint(vcl::RenderContext& rRenderContext, const tools::Rectangle& rRect) override;
    virtual bool PreNotify(NotifyEvent& rNEvt) override;
    virtual void GetFocus() override;

    void setOwningFrame(const css::uno::Reference<css::frame::XFrame>& xFrame);

    void clearRecentFileList();
};

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
