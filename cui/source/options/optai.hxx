/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 * Options: five-slot models + AI scenario CRUD.
 */
#pragma once

#include <DocumentAIInputPrefs.hxx>
#include <DocumentAIScenarioStore.hxx>
#include <sfx2/tabdlg.hxx>
#include <vcl/transfer.hxx>
#include <vcl/weld/weldutils.hxx>

#include <memory>

namespace weld
{
class Button;
class CheckButton;
class ComboBox;
class Entry;
class Label;
class TreeView;
}

/// Drop target that reorders scenario rows then persists sortOrder.
class ScenarioListDropTarget final : public weld::ReorderingDropTarget
{
public:
    using ReorderCallback = void (*)(void*);
    ScenarioListDropTarget(weld::TreeView& rTreeView, void* pUser, ReorderCallback pCb)
        : weld::ReorderingDropTarget(rTreeView)
        , m_pUser(pUser)
        , m_pCb(pCb)
    {
    }

protected:
    virtual sal_Int8 ExecuteDrop(const ExecuteDropEvent& rEvt) override
    {
        const sal_Int8 nRet = weld::ReorderingDropTarget::ExecuteDrop(rEvt);
        if (m_pCb)
            m_pCb(m_pUser);
        return nRet;
    }

private:
    void* m_pUser = nullptr;
    ReorderCallback m_pCb = nullptr;
};

class OptAiTabPage : public SfxTabPage
{
public:
    OptAiTabPage(weld::Container* pPage, weld::DialogController* pController,
                 const SfxItemSet& rSet);
    virtual ~OptAiTabPage() override;

    static std::unique_ptr<SfxTabPage>
    Create(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet* rAttrSet);

    virtual OUString GetAllStrings() override;
    virtual bool FillItemSet(SfxItemSet* rSet) override;
    virtual void Reset(const SfxItemSet* rSet) override;

    /// Called after TreeView drag-reorder; sync catalog sortOrder + save.
    void OnScenarioTreeReordered();

private:
    DECL_LINK(OnProbeClicked, weld::Button&, void);
    DECL_LINK(OnApplyPrimaryToAllClicked, weld::Button&, void);
    DECL_LINK(OnComboSingleClicked, weld::Button&, void);
    DECL_LINK(OnComboDualClicked, weld::Button&, void);
    DECL_LINK(OnOpenConfigDirClicked, weld::Button&, void);
    DECL_LINK(OnScenarioListChanged, weld::TreeView&, void);
    DECL_LINK(OnScenarioNewClicked, weld::Button&, void);
    DECL_LINK(OnScenarioSaveClicked, weld::Button&, void);
    DECL_LINK(OnScenarioDeleteClicked, weld::Button&, void);
    DECL_LINK(OnScenarioResetClicked, weld::Button&, void);
    DECL_LINK(OnScenarioUpClicked, weld::Button&, void);
    DECL_LINK(OnScenarioDownClicked, weld::Button&, void);

    void FillFromSnapshot();
    void WriteToSnapshot();
    OUString ResolvePrimaryOrSelected() const;

    void ReloadScenarioList(const OUString& rSelectId = OUString());
    void FillScenarioForm(const kqoffice::ai::chat::DocumentAIScenario& r);
    kqoffice::ai::chat::DocumentAIScenario ReadScenarioForm() const;
    void ClearScenarioForm();
    OUString SelectedScenarioId() const;

    std::unique_ptr<weld::Label> m_xStatusLabel;
    std::unique_ptr<weld::Label> m_xPathLabel;
    std::unique_ptr<weld::Entry> m_xBackend;
    std::unique_ptr<weld::Entry> m_xBaseUrl;
    std::unique_ptr<weld::Entry> m_xPrimary;
    std::unique_ptr<weld::Entry> m_xLight;
    std::unique_ptr<weld::Entry> m_xAgent;
    std::unique_ptr<weld::Entry> m_xPlan;
    std::unique_ptr<weld::Entry> m_xReview;
    std::unique_ptr<weld::ComboBox> m_xInstalledModels;
    std::unique_ptr<weld::Button> m_xProbeBtn;
    std::unique_ptr<weld::Button> m_xApplyPrimaryBtn;
    std::unique_ptr<weld::Button> m_xComboSingleBtn;
    std::unique_ptr<weld::Button> m_xComboDualBtn;
    std::unique_ptr<weld::Button> m_xOpenPathBtn;

    // Scenarios CRUD (+ TreeView drag reorder)
    std::unique_ptr<weld::TreeView> m_xScenarioList;
    std::unique_ptr<ScenarioListDropTarget> m_xScenarioDropTarget;
    rtl::Reference<TransferDataContainer> m_xScenarioDragHelper;
    std::unique_ptr<weld::Entry> m_xScId;
    std::unique_ptr<weld::Entry> m_xScTitle;
    std::unique_ptr<weld::Entry> m_xScCategory;
    std::unique_ptr<weld::Entry> m_xScSurface;
    std::unique_ptr<weld::Entry> m_xScCapability;
    std::unique_ptr<weld::Entry> m_xScSlash;
    std::unique_ptr<weld::Entry> m_xScPrompt;
    std::unique_ptr<weld::Entry> m_xScSortOrder;
    std::unique_ptr<weld::CheckButton> m_xScEnabled;
    std::unique_ptr<weld::CheckButton> m_xScShowBtn;
    std::unique_ptr<weld::CheckButton> m_xScPinned;
    std::unique_ptr<weld::CheckButton> m_xScAttachSel;
    std::unique_ptr<weld::CheckButton> m_xScDocCtx;
    std::unique_ptr<weld::CheckButton> m_xScAutoSubmit;
    std::unique_ptr<weld::CheckButton> m_xScAgent;
    std::unique_ptr<weld::CheckButton> m_xScApproval;
    std::unique_ptr<weld::Button> m_xScNewBtn;
    std::unique_ptr<weld::Button> m_xScSaveBtn;
    std::unique_ptr<weld::Button> m_xScDeleteBtn;
    std::unique_ptr<weld::Button> m_xScResetBtn;
    std::unique_ptr<weld::Button> m_xScUpBtn;
    std::unique_ptr<weld::Button> m_xScDownBtn;
    std::unique_ptr<weld::Label> m_xScPathLabel;

    // Voice + screenshot (WeChat-like)
    std::unique_ptr<weld::CheckButton> m_xVoiceEnabled;
    std::unique_ptr<weld::ComboBox> m_xVoiceBackend;
    std::unique_ptr<weld::Entry> m_xVoiceCmd;
    std::unique_ptr<weld::CheckButton> m_xVoicePtt;
    std::unique_ptr<weld::CheckButton> m_xVoiceFnHint;
    std::unique_ptr<weld::CheckButton> m_xScreenshotEnabled;
    std::unique_ptr<weld::ComboBox> m_xScreenshotMode;
    std::unique_ptr<weld::CheckButton> m_xScreenshotAttach;
    std::unique_ptr<weld::CheckButton> m_xScreenshotOpenAi;
    std::unique_ptr<weld::CheckButton> m_xScreenshotClipboard;

    kqoffice::ai::chat::ScenarioCatalog m_aScenarioCatalog;
};
