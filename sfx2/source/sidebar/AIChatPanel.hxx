/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1: In-app AI chat).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <sfx2/sidebar/PanelLayout.hxx>
#include <AgentChatDiffExtractor.hxx>
#include <DocumentAIVoiceInput.hxx>
#include <com/sun/star/ai/ProviderResponse.hpp>
#include <tools/link.hxx>
#include <vcl/timer.hxx>

#include <array>
#include <memory>
#include <vector>

namespace weld
{
class Button;
class CheckButton;
class ComboBox;
class Entry;
class Label;
class RadioButton;
class TextView;
class Toggleable;
class TreeView;
}

namespace kqoffice::ai::chat
{
struct ScenarioCatalog;
}

namespace sfx2::sidebar
{

class AIChatHistoryStore;
class AIChatContentObjectStore;
class AIChatContentReviewStore;
class AIChatEvidenceInspector;
class AIChatFormattingReviewStore;
class AIChatReviewQueueStore;
class AIChatReviewStateSyncStore;
class AIChatWorkspaceActionBarStore;
class AIChatWorkspaceSessionStore;
struct AIChatContentRegistryEntry;
struct AIChatContentOpenResult;

struct AIChatContextMentions
{
    std::vector<OUString> ValidMentions;
    std::vector<OUString> InvalidMentions;

    bool HasInvalid() const { return !InvalidMentions.empty(); }
};

enum class AIChatPanelState
{
    Idle,
    Requesting,
    Streaming,
    AwaitingRuntime,
    AwaitingApproval,
    Applied,
    Failed,
    Cancelled,
};

class AIChatPanel final : public PanelLayout
{
public:
    explicit AIChatPanel(weld::Widget* pParent);
    ~AIChatPanel() override;

private:
    DECL_LINK(OnPromptChanged, weld::Entry&, void);
    DECL_LINK(OnPromptActivated, weld::Entry&, bool);
    DECL_LINK(OnSendClicked, weld::Button&, void);
    DECL_LINK(OnCancelClicked, weld::Button&, void);
    DECL_LINK(OnRetryClicked, weld::Button&, void);
    DECL_LINK(OnClearHistoryClicked, weld::Button&, void);
    DECL_LINK(OnArtifactSelectionChanged, weld::TreeView&, void);
    DECL_LINK(OnArtifactRowActivated, weld::TreeView&, bool);
    DECL_LINK(OnRefreshArtifactsClicked, weld::Button&, void);
    DECL_LINK(OnOpenArtifactClicked, weld::Button&, void);
    DECL_LINK(OnOpenDiffReviewClicked, weld::Button&, void);
    DECL_LINK(OnReviewArtifactClicked, weld::Button&, void);
    DECL_LINK(OnFormatArtifactClicked, weld::Button&, void);
    DECL_LINK(OnInspectEvidenceClicked, weld::Button&, void);
    DECL_LINK(OnApproveSelectedClicked, weld::Button&, void);
    DECL_LINK(OnRejectSelectedClicked, weld::Button&, void);
    DECL_LINK(OnCopyReferenceClicked, weld::Button&, void);
    DECL_LINK(OnExportEvidenceClicked, weld::Button&, void);
    DECL_LINK(OnFilterWorkspaceClicked, weld::Button&, void);
    DECL_LINK(OnSortWorkspaceClicked, weld::Button&, void);
    DECL_LINK(OnRemoveArtifactClicked, weld::Button&, void);
    DECL_LINK(OnAiSettingsClicked, weld::Button&, void);
    DECL_LINK(OnRunScenarioClicked, weld::Button&, void);
    DECL_LINK(OnRefreshScenariosClicked, weld::Button&, void);
    DECL_LINK(OnScenarioPickerChanged, weld::ComboBox&, void);
    DECL_LINK(OnScenarioGridClicked, weld::Button&, void);
    DECL_LINK(OnScenarioPinClicked, weld::Button&, void);
    DECL_LINK(OnCategoryTabToggled, weld::Toggleable&, void);
    DECL_LINK(OnFollowDocToggled, weld::Toggleable&, void);
    DECL_LINK(OnSelectionChipClicked, weld::Button&, void);
    DECL_LINK(OnRoutingDiagClicked, weld::Button&, void);
    DECL_LINK(OnVoiceClicked, weld::Button&, void);
    DECL_LINK(OnVoiceMousePress, const MouseEvent&, bool);
    DECL_LINK(OnVoiceMouseRelease, const MouseEvent&, bool);
    DECL_LINK(OnScreenshotClicked, weld::Button&, void);
    DECL_LINK(OnScreenshotWinClicked, weld::Button&, void);
    DECL_LINK(OnScreenshotFullClicked, weld::Button&, void);
    DECL_LINK(OnCtxWorkbenchClicked, weld::Button&, void);
    DECL_LINK(OnCtxNotebookClicked, weld::Button&, void);
    DECL_LINK(OnPromptInsertText, OUString&, bool);
    DECL_LINK(OnInjectPollTick, Timer*, void);

    static constexpr sal_Int32 kScenarioGridSlots = 12;
    static constexpr sal_Int32 kScenarioPinSlots = 4;

    void ReloadScenarioPicker();
    void UpdateCategoryTabBadges(const kqoffice::ai::chat::ScenarioCatalog& rCatalog);
    void ReloadPinnedStrip(const kqoffice::ai::chat::ScenarioCatalog& rCatalog);
    void UpdateSelectionChip();
    void UpdatePendingPlanChip();
    void RunScenarioById(const OUString& rScenarioId);
    OUString CurrentDocumentSurface() const;
    OUString ActiveCategoryTab() const;
    void SelectCategoryTab(const OUString& rCategory);
    void TryShowDiffReviewAfterApply(const OUString& rPlanId, const OUString& rEngine,
                                     bool bApplied);

    void AppendTranscript(const OUString& rSpeaker, const OUString& rMessage);
    void AppendTranscript(const OUString& rSpeaker, const OUString& rMessage,
                          bool bPersistHistory);
    void AppendAssistantMarkdown(const OUString& rMarkdown);
    void AppendAssistantChunk(const OUString& rChunk);
    void AppendTerminalEvidence(const OUString& rStatus, const OUString& rEvidenceId);
    void LoadDocumentHistory();
    void ClearDocumentHistory();
    void UpdateContextMentions();
    bool ValidateContextMentions(const OUString& rPrompt);
    static AIChatContextMentions ParseContextMentions(const OUString& rPrompt);
    static OUString FormatContextMentionSummary(const AIChatContextMentions& rMentions);
    bool MaterializeInsertedContent(OUString& rInsertedText);
    void LoadArtifactNavigator();
    void UpdateArtifactDetails();
    void OpenSelectedArtifact();
    void ReviewSelectedArtifact();
    void ReviewSelectedFormatting();
    void InspectSelectedEvidence();
    bool DispatchWorkspaceAction(const OUString& rCommand);
    void SyncReviewState(const OUString& rReviewId, const OUString& rTransitionEvent,
                         const OUString& rState, const OUString& rSurface,
                         const OUString& rEvidenceId, const OUString& rHashReference,
                         const OUString& rOpenTarget, const OUString& rPreviewMode);
    void RemoveSelectedArtifact();
    const AIChatContentRegistryEntry* FindSelectedArtifact() const;
    OUString GetSelectedArtifactId() const;
    void LoadSessionSnapshot();
    void RecordWorkspaceActivity(const OUString& rEvent, const OUString& rSurface,
                                 const OUString& rArtifactId, const OUString& rEvidenceId,
                                 const OUString& rHashReference, const OUString& rOpenTarget);
    void RecordWorkspaceReviewActivity(const OUString& rEvent, const OUString& rSurface,
                                       const OUString& rArtifactId, const OUString& rReviewId,
                                       const OUString& rEvidenceId,
                                       const OUString& rHashReference,
                                       const OUString& rOpenTarget);
    void SaveSessionSnapshot(const OUString& rOpenArtifactId, const OUString& rActiveEvidenceId,
                             const OUString& rPreviewMode, const OUString& rFailureState,
                             const OUString& rHashReference);
    void SaveReviewSessionSnapshot(const OUString& rOpenArtifactId, const OUString& rOpenReviewId,
                                   const OUString& rActiveEvidenceId,
                                   const OUString& rPreviewMode,
                                   const OUString& rReviewState,
                                   const OUString& rFailureState,
                                   const OUString& rHashReference);
    static OUString FormatArtifactRow(const AIChatContentRegistryEntry& rEntry);
    static OUString FormatArtifactDetails(const AIChatContentRegistryEntry& rEntry);
    void FocusPrompt();
    void UpdateActions();
    void SetState(AIChatPanelState eState);
    void SubmitPrompt();
    /// Optional capability override (from scenario capabilityHint / slash). Empty → heuristic.
    css::ai::ProviderResponse CallProvider(const OUString& rPrompt,
                                           const OUString& rCapabilityOverride = OUString());
    void RunRoutingDiagnostics(bool bAppendTranscript);
    /// Consume KQOFFICE_AI_RUN_SCENARIO / pending-scenario-run queue.
    void ConsumePendingScenarioRun();
    /// Visible plan→act→review step bar (Stage B).
    void SetAgentStepBar(const OUString& rLabel);
    void UpdateAgentStepBar(sal_Int32 nActiveStep /*0=plan,1=act,2=review*/,
                            const OUString& rDetail = OUString());
    /// Consume ~/.config/kqoffice/pending-prompt-inject from workbench/notebook/voice/screenshot.
    void ConsumePendingPromptInject();
    void AppendPromptText(const OUString& rText);
    /// mode: region | window | fullscreen
    void RunScreenshotMode(const OUString& rMode);
    void applyVoiceCaptureUi(const kqoffice::ai::chat::VoiceCaptureResult& cap);
    static OUString StateToLabel(AIChatPanelState eState);

    /// Stage provider JSON as a pending ApplyPlan; never mutates the main document.
    void StagePendingApplyPlan(const OUString& rProviderContent, const OUString& rEvidenceId);
    /// Apply the staged plan only after explicit human approval.
    bool ApplyPendingPlanWithApproval();
    void ClearPendingPlan();
    bool HasPendingApplyPlan() const { return m_bHasPendingPlan; }

    std::unique_ptr<weld::Label> m_xStatusLabel;
    std::unique_ptr<weld::Label> m_xAgentStepBar;
    std::unique_ptr<weld::TextView> m_xTranscriptView;
    std::unique_ptr<weld::Entry> m_xPromptEntry;
    std::unique_ptr<weld::Button> m_xVoiceButton;
    std::unique_ptr<weld::Button> m_xScreenshotButton;
    std::unique_ptr<weld::Button> m_xScreenshotWinButton;
    std::unique_ptr<weld::Button> m_xScreenshotFullButton;
    std::unique_ptr<weld::Button> m_xCtxWorkbenchButton;
    std::unique_ptr<weld::Button> m_xCtxNotebookButton;
    std::unique_ptr<weld::Button> m_xSendButton;
    bool m_bVoiceHoldActive = false;
    std::unique_ptr<weld::Button> m_xCancelButton;
    std::unique_ptr<weld::Button> m_xRetryButton;
    std::unique_ptr<weld::Button> m_xClearHistoryButton;
    std::unique_ptr<weld::TreeView> m_xArtifactTree;
    std::unique_ptr<weld::Label> m_xArtifactDetailsLabel;
    std::unique_ptr<weld::Button> m_xRefreshArtifactsButton;
    std::unique_ptr<weld::Button> m_xOpenArtifactButton;
    std::unique_ptr<weld::Button> m_xOpenDiffReviewButton;
    std::unique_ptr<weld::Button> m_xReviewArtifactButton;
    std::unique_ptr<weld::Button> m_xFormatArtifactButton;
    std::unique_ptr<weld::Button> m_xInspectEvidenceButton;
    std::unique_ptr<weld::Button> m_xApproveSelectedButton;
    std::unique_ptr<weld::Button> m_xRejectSelectedButton;
    std::unique_ptr<weld::Button> m_xCopyReferenceButton;
    std::unique_ptr<weld::Button> m_xExportEvidenceButton;
    std::unique_ptr<weld::Button> m_xFilterWorkspaceButton;
    std::unique_ptr<weld::Button> m_xSortWorkspaceButton;
    std::unique_ptr<weld::Button> m_xRemoveArtifactButton;
    std::unique_ptr<weld::Button> m_xAiSettingsButton;
    std::unique_ptr<weld::ComboBox> m_xScenarioPicker;
    std::unique_ptr<weld::Button> m_xRunScenarioBtn;
    std::unique_ptr<weld::Button> m_xRefreshScenariosBtn;
    std::unique_ptr<weld::Label> m_xScenarioSurfaceLabel;
    std::unique_ptr<weld::CheckButton> m_xOptFollowDoc;
    std::unique_ptr<weld::RadioButton> m_xTabWriter;
    std::unique_ptr<weld::RadioButton> m_xTabCalc;
    std::unique_ptr<weld::RadioButton> m_xTabImpress;
    std::unique_ptr<weld::RadioButton> m_xTabGeneral;
    std::unique_ptr<weld::CheckButton> m_xOptAttachSelection;
    std::unique_ptr<weld::CheckButton> m_xOptAgentPipeline;
    std::unique_ptr<weld::CheckButton> m_xOptDocContext;
    std::array<std::unique_ptr<weld::Button>, kScenarioGridSlots> m_xScenarioGridBtns;
    std::array<OUString, kScenarioGridSlots> m_aScenarioGridIds;
    std::array<std::unique_ptr<weld::Button>, kScenarioPinSlots> m_xScenarioPinBtns;
    std::array<OUString, kScenarioPinSlots> m_aScenarioPinIds;
    std::unique_ptr<weld::Label> m_xScenarioPinnedLabel;
    std::unique_ptr<weld::Button> m_xSelectionChipBtn;
    std::unique_ptr<weld::Label> m_xPendingPlanChip;
    std::unique_ptr<weld::Button> m_xRoutingDiagBtn;
    std::unique_ptr<weld::Label> m_xRoutingDiagLabel;
    bool m_bSuppressCategoryReload = false;
    /// One-shot capability for next CallProvider (scenario / slash); cleared after use.
    OUString m_sForcedCapability;
    std::unique_ptr<AIChatHistoryStore> m_xHistoryStore;
    std::unique_ptr<AIChatContentObjectStore> m_xContentObjectStore;
    std::unique_ptr<AIChatContentReviewStore> m_xContentReviewStore;
    std::unique_ptr<AIChatEvidenceInspector> m_xEvidenceInspector;
    std::unique_ptr<AIChatFormattingReviewStore> m_xFormattingReviewStore;
    std::unique_ptr<AIChatReviewQueueStore> m_xReviewQueueStore;
    std::unique_ptr<AIChatReviewStateSyncStore> m_xReviewStateSyncStore;
    std::unique_ptr<AIChatWorkspaceActionBarStore> m_xWorkspaceActionBarStore;
    std::unique_ptr<AIChatWorkspaceSessionStore> m_xSessionStore;
    std::vector<AIChatContentRegistryEntry> m_aArtifacts;

    OUString m_sLastPrompt;
    OUString m_sStreamingBuffer;
    OUString m_sPendingEvidenceId;
    kqoffice::ai::chat::ApplyPlan m_aPendingPlan;
    bool m_bHasPendingPlan = false;
    AIChatPanelState m_eState = AIChatPanelState::Idle;
    /// Poll pending-prompt-inject while AI panel stays open (workbench/notebook inject).
    Timer m_aInjectPoll;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
