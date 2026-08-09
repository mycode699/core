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
#include <DocumentAIWorkPlan.hxx>
#include <com/sun/star/ai/ProviderResponse.hpp>
#include <tools/link.hxx>
#include <vcl/timer.hxx>
#include <vcl/weld/Notebook.hxx>

#include <array>
#include <memory>
#include <vector>

class DropTargetHelper;

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

namespace kqoffice::ai::cowork
{
enum class ScheduleKind : sal_uInt8;
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

    /// Active AI chat panel for DiffReview pending approve/reject C ABI bridge.
    static AIChatPanel* GetActivePanel();

    /// Inject a seed prompt and auto-submit (select-to-act / hotkey path).
    /// Returns false if panel is busy.
    bool RunQuickIntent(const OUString& rIntentId, const OUString& rSeedPrompt);
    bool HasPendingApplyPlan() const { return m_bHasPendingPlan; }
    const OUString& GetPendingPlanId() const { return m_aPendingPlan.planId; }
    /// Explicit human approve → DocumentAIApply (same path as sidebar「批准写回」).
    bool ApplyPendingPlanWithApproval();
    /// Discard staged plan; main document unchanged (same path as sidebar「拒绝」).
    void RejectPendingPlan();
    /**
     * M20: after stale reject, clear pending plan and re-run last prompt against
     * the current document (new skeleton snapshot). Never mutates main doc itself.
     */
    bool RegenerateAfterStale();

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
    DECL_LINK(OnAgentRunClicked, weld::Button&, void);
    DECL_LINK(OnAgentContinueClicked, weld::Button&, void);
    DECL_LINK(OnAgentApproveClicked, weld::Button&, void);
    DECL_LINK(OnAgentStopClicked, weld::Button&, void);
    DECL_LINK(OnAgentRefreshClicked, weld::Button&, void);
    DECL_LINK(OnAgentClearClicked, weld::Button&, void);
    DECL_LINK(OnScheduleRefreshClicked, weld::Button&, void);
    DECL_LINK(OnScheduleToggleClicked, weld::Button&, void);
    DECL_LINK(OnScheduleRunNowClicked, weld::Button&, void);
    DECL_LINK(OnScheduleAddClicked, weld::Button&, void);
    DECL_LINK(OnScheduleRemoveClicked, weld::Button&, void);
    DECL_LINK(OnScheduleSaveClicked, weld::Button&, void);
    DECL_LINK(OnScheduleTreeSelectionChanged, weld::TreeView&, void);
    DECL_LINK(OnScheduleAutoSendToggled, weld::Toggleable&, void);
    DECL_LINK(OnBatchRefreshClicked, weld::Button&, void);
    DECL_LINK(OnMainNotebookEnterPage, const OUString&, void);
    DECL_LINK(OnReviewRefreshClicked, weld::Button&, void);
    DECL_LINK(OnAiSettingsClicked, weld::Button&, void);
    DECL_LINK(OnOpenKqNotebookClicked, weld::Button&, void);
    DECL_LINK(OnRunScenarioClicked, weld::Button&, void);
    DECL_LINK(OnRefreshScenariosClicked, weld::Button&, void);
    DECL_LINK(OnScenarioPickerChanged, weld::ComboBox&, void);
    DECL_LINK(OnScenarioGridClicked, weld::Button&, void);
    DECL_LINK(OnScenarioPinClicked, weld::Button&, void);
    DECL_LINK(OnCategoryTabToggled, weld::Toggleable&, void);
    DECL_LINK(OnFollowDocToggled, weld::Toggleable&, void);
    DECL_LINK(OnSelectionChipClicked, weld::Button&, void);
    DECL_LINK(OnPendingPlanChipClicked, weld::Button&, void);
    DECL_LINK(OnChatApproveClicked, weld::Button&, void);
    DECL_LINK(OnChatDiffClicked, weld::Button&, void);
    DECL_LINK(OnChatRejectClicked, weld::Button&, void);
    DECL_LINK(OnChatUndoClicked, weld::Button&, void);
    DECL_LINK(OnLocateRagClicked, weld::Button&, void);
    DECL_LINK(OnIntentRewriteClicked, weld::Button&, void);
    DECL_LINK(OnIntentFormalClicked, weld::Button&, void);
    DECL_LINK(OnIntentShortenClicked, weld::Button&, void);
    DECL_LINK(OnIntentExpandClicked, weld::Button&, void);
    DECL_LINK(OnIntentSummarizeClicked, weld::Button&, void);
    DECL_LINK(OnIntentOutlineClicked, weld::Button&, void);
    DECL_LINK(OnIntentProofreadClicked, weld::Button&, void);
    DECL_LINK(OnIntentContinueClicked, weld::Button&, void);
    DECL_LINK(OnIntentPlanClicked, weld::Button&, void);
    DECL_LINK(OnIntentAgentClicked, weld::Button&, void);
    DECL_LINK(OnDesignStepOutlineClicked, weld::Button&, void);
    DECL_LINK(OnDesignStepVariantsClicked, weld::Button&, void);
    DECL_LINK(OnDesignStepApplyClicked, weld::Button&, void);
    DECL_LINK(OnDesignStepExportClicked, weld::Button&, void);
    DECL_LINK(OnRoutingDiagClicked, weld::Button&, void);
    DECL_LINK(OnVoiceClicked, weld::Button&, void);
    DECL_LINK(OnVoiceMousePress, const MouseEvent&, bool);
    DECL_LINK(OnVoiceMouseRelease, const MouseEvent&, bool);
    DECL_LINK(OnScreenshotClicked, weld::Button&, void);
    DECL_LINK(OnScreenshotWinClicked, weld::Button&, void);
    DECL_LINK(OnScreenshotFullClicked, weld::Button&, void);
    DECL_LINK(OnAttachFileClicked, weld::Button&, void);
    DECL_LINK(OnCtxWorkbenchClicked, weld::Button&, void);
    DECL_LINK(OnCtxNotebookClicked, weld::Button&, void);
    DECL_LINK(OnPromptInsertText, OUString&, bool);
    DECL_LINK(OnInjectPollTick, Timer*, void);
    DECL_LINK(OnDeferredWarmupTick, Timer*, void);
    /// Periodic local scheduled-task due scan (injects pending-prompt-inject).
    DECL_LINK(OnScheduleTick, Timer*, void);

    static constexpr sal_Int32 kScenarioGridSlots = 12;
    static constexpr sal_Int32 kScenarioPinSlots = 4;

    void ReloadScenarioPicker();
    void UpdateCategoryTabBadges(const kqoffice::ai::chat::ScenarioCatalog& rCatalog);
    void ReloadPinnedStrip(const kqoffice::ai::chat::ScenarioCatalog& rCatalog);
    void UpdateSelectionChip();
    void UpdatePendingPlanChip();
    /// Pin Task Bootstrap restatement on chip (semantic start).
    void UpdateTaskBootstrapChip(const OUString& rVisibleZh = OUString());
    /// Grok-style work plan gate (large task → confirm plan → then generate).
    void PresentWorkPlan(const kqoffice::ai::chat::WorkPlan& rPlan);
    void ClearWorkPlan();
    void UpdateWorkPlanChip();
    /// Undo last AI write-back (SID_UNDO); same path as「撤销写回」/ `/撤销写回`.
    bool PerformLastApplyUndo();
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
    void LoadAgentSteps();
    /// DuMate-style 定时任务 list from kqoffice::ai::cowork::ScheduledTaskStore.
    void LoadScheduleList();
    /// Selected schedule_tree row id, or empty.
    OUString GetSelectedScheduleId() const;
    /// Fill kind/interval/daily editor from selected row (or clear defaults).
    void FillScheduleEditorFromSelected();
    /// Read editor widgets into kind + interval/daily fields. Returns false if invalid.
    bool ReadScheduleEditorFields(kqoffice::ai::cowork::ScheduleKind& rKind,
                                  sal_Int32& rIntervalMinutes, sal_Int32& rDailyHour,
                                  sal_Int32& rDailyMinute, OUString& rErrorZh) const;
    /// Minimal "run now": inject promptOrScenarioId + markRun (no dispatcher yet).
    bool DispatchScheduledTaskNow(const OUString& rId);
    /// Read-only batch convert/archive ledger list (start center runs jobs).
    void LoadBatchJobList();
    /// Attach local file paths as @文件: prompt mentions (click or drag-drop).
    void AttachLocalFilePaths(const std::vector<OUString>& rPaths);
    /// Register a local path into 内容 tab so MD/PDF/图片 can be previewed (read-only).
    void RegisterLocalFileArtifact(const OUString& rPath, const OUString& rSourceKind);
    void LoadReviewQueue();
    void UpdateArtifactDetails();
    void OpenSelectedArtifact();
    void ReviewSelectedArtifact();
    void ReviewSelectedFormatting();
    void PushAgentStepRow(const OUString& rStep, const OUString& rStatus);
    /// Clear plan→Continue human gate state (pending goal/plan/context).
    void ClearAgentContinueGate();
    /// Mark in-progress agent-tree rows as stopped (DuMate task progress).
    void MarkAgentStepsStopped();
    /// Stop stream + optional cowork TaskRunner; update tree/status (Chinese).
    /// @param rUserStatus visible status, e.g. 「已停止」or「已终止上一任务，开始新任务」
    /// @param bFromAppend when true, keep composer ready for the replacement prompt
    void StopActiveRun(const OUString& rUserStatus, bool bFromAppend);
    bool IsRunBusy() const;
    /// Register AI reply into 内容 registry so 审查/打开 have a real target.
    void RegisterAssistantArtifact(const OUString& rContent, const OUString& rEvidenceId,
                                   const OUString& rSourceKind);
    static OUString LocalizeArtifactType(const OUString& rType);
    static OUString LocalizeArtifactState(const OUString& rState);
    static OUString LocalizeReviewState(const OUString& rState);
    static OUString LocalizeReviewItemType(const OUString& rItemType);
    /// Map raw English agent/cowork step statuses to Chinese (任务 Tab).
    static OUString LocalizeAgentStepStatus(const OUString& rStatus);
    /// Provider/machine status → short Chinese (失败/成功侧栏用，不堆 JSON).
    static OUString LocalizeProviderStatusZh(const OUString& rStatus);
    /// One-line user evidence summary: 结果 + 主文档语义 + 证据编号（无 JSON）.
    static OUString FormatEvidenceUserSummary(const OUString& rStatus,
                                              const OUString& rEvidenceId);
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
    /// Clarify card before multi-step agent / plan starts. false = Deny (abort start).
    bool ConfirmComplexAiTaskStart(bool bNeedsConfirm);
    /// Optional capability override (from scenario capabilityHint / slash). Empty → heuristic.
    css::ai::ProviderResponse CallProvider(const OUString& rPrompt,
                                           const OUString& rCapabilityOverride = OUString());
    void RunRoutingDiagnostics(bool bAppendTranscript);
    /**
     * P0-3 / M16: surface model health recovery (Key / Ollama / gateway).
     * Never mutates the main document. Optionally opens ~/.config/kqoffice.
     */
    void PresentModelHealthGuidance(bool bOpenConfigDir = false,
                                    const OUString& rFailDetail = OUString());
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
    /// Machine token for fixtures (idle/requesting/…).
    static OUString StateToLabel(AIChatPanelState eState);
    /// Human-readable Chinese status for title bar (Copilot-style narrative).
    static OUString StateToUserLabel(AIChatPanelState eState);
    /// Update activity_card process narrative (task-aware workspace).
    void UpdateActivityCard();
    /// Intent chips + placeholder (task-aware composer).
    void UpdateComposerChrome();
    /// Impress design-flow strip: outline → variants → apply → export.
    void UpdateDesignFlowChrome();
    /// Detect rewrite/summarize/plan/agent/… from prompt text.
    static OUString DetectComposerIntent(const OUString& rPrompt);
    /// Apply intent chip: set capability + seed prompt hint.
    void ApplyComposerIntent(const OUString& rIntentId, const OUString& rSeedPrompt);
    /// Show/hide + enable chat-tab approval strip (M1.3).
    void UpdateApprovalChrome();
    /// Jump notebook to review tab (index 3) for pending plan (M3.2).
    void ShowReviewTab();
    /**
     * P0-2 / M15: after staging a pending plan, auto-surface the approval path:
     * review tab + DiffReview dialog + locate/highlight first op target +
     * old→new preview in transcript. Never mutates the main document.
     * @param rSource attribution token (auto-stage | propose-slash | chip | …)
     */
    void PresentPendingPlanForApproval(const OUString& rSource = OUString());
    /// Locate and select first pending op target (or live selection). Read-only.
    void HighlightPendingPlanTarget();
    /// Append a short old→new preview for the pending plan into the transcript.
    void AppendPendingPlanDiffPreview();
    /// Lazy-load 内容/多步/审核 trees + session (not on panel construct — cold open).
    void EnsureWorkspaceDataLoaded();

    /// Stage provider JSON as a pending ApplyPlan; never mutates the main document.
    void StagePendingApplyPlan(const OUString& rProviderContent, const OUString& rEvidenceId);
    void ClearPendingPlan();
    /// Present stale-plan recovery card + arm 「重新生成」button (no main-doc mutate).
    void PresentStalePlanRecovery(const OUString& rSource);

    std::unique_ptr<weld::Label> m_xStatusLabel;
    std::unique_ptr<weld::Label> m_xAgentStepBar;
    std::unique_ptr<weld::Label> m_xActivityCard;
    std::unique_ptr<weld::Widget> m_xDesignFlowBox;
    std::unique_ptr<weld::Button> m_xDesignStepOutline;
    std::unique_ptr<weld::Button> m_xDesignStepVariants;
    std::unique_ptr<weld::Button> m_xDesignStepApply;
    std::unique_ptr<weld::Button> m_xDesignStepExport;
    std::unique_ptr<weld::TextView> m_xTranscriptView;
    std::unique_ptr<weld::Entry> m_xPromptEntry;
    std::unique_ptr<weld::Button> m_xIntentRewriteBtn;
    std::unique_ptr<weld::Button> m_xIntentFormalBtn;
    std::unique_ptr<weld::Button> m_xIntentShortenBtn;
    std::unique_ptr<weld::Button> m_xIntentExpandBtn;
    std::unique_ptr<weld::Button> m_xIntentSummarizeBtn;
    std::unique_ptr<weld::Button> m_xIntentOutlineBtn;
    std::unique_ptr<weld::Button> m_xIntentProofreadBtn;
    std::unique_ptr<weld::Button> m_xIntentContinueBtn;
    std::unique_ptr<weld::Button> m_xIntentPlanBtn;
    std::unique_ptr<weld::Button> m_xIntentAgentBtn;
    std::unique_ptr<weld::Button> m_xVoiceButton;
    std::unique_ptr<weld::Button> m_xScreenshotButton;
    std::unique_ptr<weld::Button> m_xScreenshotWinButton;
    std::unique_ptr<weld::Button> m_xScreenshotFullButton;
    std::unique_ptr<weld::Button> m_xAttachFileButton;
    std::unique_ptr<weld::Button> m_xCtxWorkbenchButton;
    std::unique_ptr<weld::Button> m_xCtxNotebookButton;
    std::unique_ptr<weld::Button> m_xSendButton;
    bool m_bVoiceHoldActive = false;
    std::unique_ptr<weld::Button> m_xCancelButton;
    std::unique_ptr<weld::Button> m_xRetryButton;
    std::unique_ptr<weld::Button> m_xClearHistoryButton;
    std::unique_ptr<weld::TreeView> m_xArtifactTree;
    std::unique_ptr<weld::TreeView> m_xAgentTree;
    std::unique_ptr<weld::Label> m_xAgentEmptyLabel;
    std::unique_ptr<weld::Button> m_xAgentRunBtn;
    std::unique_ptr<weld::Button> m_xAgentContinueBtn;
    std::unique_ptr<weld::Button> m_xAgentApproveBtn;
    std::unique_ptr<weld::Button> m_xAgentStopBtn;
    std::unique_ptr<weld::Button> m_xAgentRefreshBtn;
    std::unique_ptr<weld::Button> m_xAgentClearBtn;
    std::unique_ptr<weld::TreeView> m_xScheduleTree;
    std::unique_ptr<weld::Button> m_xScheduleRefreshBtn;
    std::unique_ptr<weld::Button> m_xScheduleToggleBtn;
    std::unique_ptr<weld::Button> m_xScheduleRunNowBtn;
    std::unique_ptr<weld::Button> m_xScheduleAddBtn;
    std::unique_ptr<weld::Button> m_xScheduleRemoveBtn;
    std::unique_ptr<weld::ComboBox> m_xScheduleKind;
    std::unique_ptr<weld::Entry> m_xScheduleInterval;
    std::unique_ptr<weld::Entry> m_xScheduleHour;
    std::unique_ptr<weld::Entry> m_xScheduleMinute;
    std::unique_ptr<weld::Button> m_xScheduleSaveBtn;
    std::unique_ptr<weld::CheckButton> m_xScheduleAutoSend;
    std::unique_ptr<weld::TreeView> m_xBatchTree;
    std::unique_ptr<weld::Button> m_xBatchRefreshBtn;
    /// Drop files onto prompt → @文件: (lifetime owned by panel).
    std::unique_ptr<DropTargetHelper> m_xAttachDropHelper;
    std::unique_ptr<weld::TreeView> m_xReviewTree;
    std::unique_ptr<weld::Label> m_xReviewEmptyLabel;
    std::unique_ptr<weld::Button> m_xReviewRefreshBtn;
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
    std::unique_ptr<weld::Button> m_xOpenKqNotebookButton;
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
    std::unique_ptr<weld::Button> m_xLocateRagBtn;
    /// Last ask-document query for locate-first-hit (empty when none).
    OUString m_sLastRagQuery;
    OUString m_sLastRagPosition;
    std::array<std::unique_ptr<weld::Button>, kScenarioGridSlots> m_xScenarioGridBtns;
    std::array<OUString, kScenarioGridSlots> m_aScenarioGridIds;
    std::array<std::unique_ptr<weld::Button>, kScenarioPinSlots> m_xScenarioPinBtns;
    std::array<OUString, kScenarioPinSlots> m_aScenarioPinIds;
    std::unique_ptr<weld::Label> m_xScenarioPinnedLabel;
    std::unique_ptr<weld::Button> m_xSelectionChipBtn;
    std::unique_ptr<weld::Button> m_xTaskBootstrapChip;
    std::unique_ptr<weld::Button> m_xPendingPlanChip;
    std::unique_ptr<weld::Widget> m_xApprovalActionRow;
    std::unique_ptr<weld::Label> m_xApprovalHintLabel;
    std::unique_ptr<weld::Button> m_xChatApproveBtn;
    std::unique_ptr<weld::Button> m_xChatDiffBtn;
    std::unique_ptr<weld::Button> m_xChatRejectBtn;
    std::unique_ptr<weld::Button> m_xChatUndoBtn;
    std::unique_ptr<weld::Notebook> m_xMainNotebook;
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
    /// In-session multi-step rows for the 多步 tab (步骤, 状态).
    std::vector<std::pair<OUString, OUString>> m_aAgentStepCache;

    OUString m_sLastPrompt;
    /// Last Task Bootstrap objective (for 继续 thin restatement).
    OUString m_sLastTaskRestatement;
    OUString m_sStreamingBuffer;
    /// Last failure/cancel detail for activity card + status (Chinese, no JSON).
    OUString m_sLastOutcomeDetail;
    OUString m_sPendingEvidenceId;
    kqoffice::ai::chat::ApplyPlan m_aPendingPlan;
    bool m_bHasPendingPlan = false;
    /// Work plan (pre-generation gate) — distinct from ApplyPlan pending write-back.
    kqoffice::ai::chat::WorkPlan m_aWorkPlan;
    bool m_bHasWorkPlan = false;
    bool m_bWorkPlanApproved = false;
    /// Next submit forces work-plan gate (clarify「先出计划」/ 规划按钮 / /plan).
    bool m_bForceWorkPlanOnce = false;
    /// True after a successful AI approve-apply; enables「撤销写回」until next edit session action.
    bool m_bLastApplyCanUndo = false;
    /// After stale reject: Retry becomes one-click regenerate from m_sLastPrompt.
    bool m_bStaleNeedsRegen = false;
    AIChatPanelState m_eState = AIChatPanelState::Idle;
    /// Cooperative cancel for stream / multi-step agent / cowork TaskRunner.
    bool m_bCancelRequested = false;
    /// True while multi-step agent pipeline is in flight (for append/stop UX).
    bool m_bAgentRunActive = false;
    /// After plan phase: waiting for user 「继续」before act/review/verify.
    bool m_bAgentAwaitingContinue = false;
    OUString m_sAgentGateGoal;
    OUString m_sAgentGateContext;
    OUString m_sAgentGateSurface;
    OUString m_sAgentGateDocTools;
    OUString m_sAgentGatePlanContent;
    /// Guards re-entrant SubmitPrompt (e.g. append via Reschedule while running).
    bool m_bSubmitInFlight = false;
    /// When append arrives mid-run, hold the replacement prompt until stop completes.
    OUString m_sQueuedReplacePrompt;
    /// Poll pending-prompt-inject while AI panel stays open (workbench/notebook inject).
    Timer m_aInjectPoll;
    /// Defer Ollama routing probe + workspace tree hydrate off the open critical path.
    Timer m_aDeferredWarmup;
    /// 60s local scheduled-task dispatcher (kqoffice ScheduledTaskDispatcher::processDue).
    Timer m_aScheduleTick;
    bool m_bWorkspaceDataLoaded = false;
    bool m_bRoutingDiagDone = false;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
