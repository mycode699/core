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
#include <com/sun/star/ai/ProviderResponse.hpp>
#include <tools/link.hxx>

#include <memory>
#include <vector>

namespace weld
{
class Button;
class Entry;
class Label;
class TextView;
class TreeView;
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
    DECL_LINK(OnPromptInsertText, OUString&, bool);

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
    css::ai::ProviderResponse CallProvider(const OUString& rPrompt);
    static OUString StateToLabel(AIChatPanelState eState);

    std::unique_ptr<weld::Label> m_xStatusLabel;
    std::unique_ptr<weld::TextView> m_xTranscriptView;
    std::unique_ptr<weld::Entry> m_xPromptEntry;
    std::unique_ptr<weld::Button> m_xSendButton;
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
    AIChatPanelState m_eState = AIChatPanelState::Idle;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
