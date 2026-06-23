/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1: In-app AI chat).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatPanel.hxx"

#include "AIChatContentOpener.hxx"
#include "AIChatContentRegistry.hxx"
#include "AIChatContentObjectStore.hxx"
#include "AIChatContentReviewStore.hxx"
#include "AIChatEvidenceInspector.hxx"
#include "AIChatFormattingReviewStore.hxx"
#include "AIChatHistoryStore.hxx"
#include "AIChatMarkdownRenderer.hxx"
#include "AIChatPreviewMatrix.hxx"
#include "AIChatReviewQueueStore.hxx"
#include "AIChatReviewStateSyncStore.hxx"
#include "AIChatWorkspaceActionBarStore.hxx"
#include "AIChatWorkspaceSessionStore.hxx"

#include <com/sun/star/ai/ProviderRequest.hpp>
#include <com/sun/star/ai/XProvider.hpp>
#include <com/sun/star/lang/IllegalArgumentException.hpp>
#include <com/sun/star/uno/Exception.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <comphelper/processfactory.hxx>
#include <rtl/ustrbuf.hxx>
#include <vcl/weld/Builder.hxx>
#include <vcl/weld/Entry.hxx>
#include <vcl/weld/TextView.hxx>
#include <vcl/weld/TreeView.hxx>
#include <vcl/weld/weld.hxx>
#include <vcl/vclenum.hxx>

#include <algorithm>

namespace sfx2::sidebar
{
namespace
{
constexpr OUStringLiteral V2_PROVIDER_SERVICE_NAME = u"com.sun.star.ai.Provider";
constexpr sal_Int32 PROVIDER_TIMEOUT_MS = 30000;
constexpr OUStringLiteral CONTEXT_MENTION_SUGGESTIONS
    = u"Context: @selection, @doc, @connector:<id>";

css::ai::ProviderResponse MakeLocalFailure(const OUString& rStatus, const OUString& rMessage)
{
    css::ai::ProviderResponse aResponse;
    aResponse.status = rStatus;
    aResponse.content = rMessage;
    aResponse.evidenceId = OUString();
    aResponse.durationMs = 0;
    return aResponse;
}

bool IsMentionBoundary(sal_Unicode c)
{
    return c == u' ' || c == u'\t' || c == u'\n' || c == u'\r' || c == u',' || c == u';'
           || c == u'.' || c == u')' || c == u']' || c == u'}';
}

bool IsConnectorIdChar(sal_Unicode c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') || c == u'-';
}

bool IsValidConnectorMention(const OUString& rMention)
{
    constexpr OUStringLiteral CONNECTOR_PREFIX = u"@connector:";
    const OUString sPrefix(CONNECTOR_PREFIX);
    if (!rMention.startsWith(sPrefix))
        return false;

    const OUString sId = rMention.copy(sPrefix.getLength());
    if (sId.isEmpty())
        return false;

    for (sal_Int32 i = 0; i < sId.getLength(); ++i)
    {
        if (!IsConnectorIdChar(sId[i]))
            return false;
    }
    return true;
}
}

AIChatPanel::AIChatPanel(weld::Widget* pParent)
    : PanelLayout(pParent, u"AIChatPanel"_ustr, u"sfx/ui/aichatpanel.ui"_ustr)
    , m_xStatusLabel(m_xBuilder->weld_label(u"status_label"_ustr))
    , m_xTranscriptView(m_xBuilder->weld_text_view(u"transcript_view"_ustr))
    , m_xPromptEntry(m_xBuilder->weld_entry(u"prompt_entry"_ustr))
    , m_xSendButton(m_xBuilder->weld_button(u"send_button"_ustr))
    , m_xCancelButton(m_xBuilder->weld_button(u"cancel_button"_ustr))
    , m_xRetryButton(m_xBuilder->weld_button(u"retry_button"_ustr))
    , m_xClearHistoryButton(m_xBuilder->weld_button(u"clear_history_button"_ustr))
    , m_xArtifactTree(m_xBuilder->weld_tree_view(u"artifact_tree"_ustr))
    , m_xArtifactDetailsLabel(m_xBuilder->weld_label(u"artifact_details_label"_ustr))
    , m_xRefreshArtifactsButton(m_xBuilder->weld_button(u"refresh_artifacts_button"_ustr))
    , m_xOpenArtifactButton(m_xBuilder->weld_button(u"open_artifact_button"_ustr))
    , m_xOpenDiffReviewButton(m_xBuilder->weld_button(u"open_diff_review_button"_ustr))
    , m_xReviewArtifactButton(m_xBuilder->weld_button(u"review_artifact_button"_ustr))
    , m_xFormatArtifactButton(m_xBuilder->weld_button(u"format_artifact_button"_ustr))
    , m_xInspectEvidenceButton(m_xBuilder->weld_button(u"inspect_evidence_button"_ustr))
    , m_xApproveSelectedButton(m_xBuilder->weld_button(u"approve_selected_button"_ustr))
    , m_xRejectSelectedButton(m_xBuilder->weld_button(u"reject_selected_button"_ustr))
    , m_xCopyReferenceButton(m_xBuilder->weld_button(u"copy_reference_button"_ustr))
    , m_xExportEvidenceButton(m_xBuilder->weld_button(u"export_evidence_button"_ustr))
    , m_xFilterWorkspaceButton(m_xBuilder->weld_button(u"filter_workspace_button"_ustr))
    , m_xSortWorkspaceButton(m_xBuilder->weld_button(u"sort_workspace_button"_ustr))
    , m_xRemoveArtifactButton(m_xBuilder->weld_button(u"remove_artifact_button"_ustr))
    , m_xHistoryStore(std::make_unique<AIChatHistoryStore>())
    , m_xContentObjectStore(std::make_unique<AIChatContentObjectStore>())
    , m_xContentReviewStore(std::make_unique<AIChatContentReviewStore>())
    , m_xEvidenceInspector(std::make_unique<AIChatEvidenceInspector>())
    , m_xFormattingReviewStore(std::make_unique<AIChatFormattingReviewStore>())
    , m_xReviewQueueStore(std::make_unique<AIChatReviewQueueStore>())
    , m_xReviewStateSyncStore(std::make_unique<AIChatReviewStateSyncStore>())
    , m_xWorkspaceActionBarStore(std::make_unique<AIChatWorkspaceActionBarStore>())
    , m_xSessionStore(std::make_unique<AIChatWorkspaceSessionStore>(
          m_xHistoryStore->GetDocumentKey()))
{
    m_xTranscriptView->set_editable(false);
    m_xArtifactTree->set_selection_mode(SelectionMode::Single);
    m_xPromptEntry->set_placeholder_text(u"询问、审查或整理当前内容"_ustr);
    m_xPromptEntry->connect_insert_text(LINK(this, AIChatPanel, OnPromptInsertText));

    m_xPromptEntry->connect_changed(LINK(this, AIChatPanel, OnPromptChanged));
    m_xPromptEntry->connect_activate(LINK(this, AIChatPanel, OnPromptActivated));
    m_xSendButton->connect_clicked(LINK(this, AIChatPanel, OnSendClicked));
    m_xCancelButton->connect_clicked(LINK(this, AIChatPanel, OnCancelClicked));
    m_xRetryButton->connect_clicked(LINK(this, AIChatPanel, OnRetryClicked));
    m_xClearHistoryButton->connect_clicked(LINK(this, AIChatPanel, OnClearHistoryClicked));
    m_xArtifactTree->connect_selection_changed(
        LINK(this, AIChatPanel, OnArtifactSelectionChanged));
    m_xArtifactTree->connect_row_activated(LINK(this, AIChatPanel, OnArtifactRowActivated));
    m_xRefreshArtifactsButton->connect_clicked(
        LINK(this, AIChatPanel, OnRefreshArtifactsClicked));
    m_xOpenArtifactButton->connect_clicked(LINK(this, AIChatPanel, OnOpenArtifactClicked));
    m_xOpenDiffReviewButton->connect_clicked(LINK(this, AIChatPanel, OnOpenDiffReviewClicked));
    m_xReviewArtifactButton->connect_clicked(LINK(this, AIChatPanel, OnReviewArtifactClicked));
    m_xFormatArtifactButton->connect_clicked(LINK(this, AIChatPanel, OnFormatArtifactClicked));
    m_xInspectEvidenceButton->connect_clicked(LINK(this, AIChatPanel, OnInspectEvidenceClicked));
    m_xApproveSelectedButton->connect_clicked(
        LINK(this, AIChatPanel, OnApproveSelectedClicked));
    m_xRejectSelectedButton->connect_clicked(LINK(this, AIChatPanel, OnRejectSelectedClicked));
    m_xCopyReferenceButton->connect_clicked(LINK(this, AIChatPanel, OnCopyReferenceClicked));
    m_xExportEvidenceButton->connect_clicked(LINK(this, AIChatPanel, OnExportEvidenceClicked));
    m_xFilterWorkspaceButton->connect_clicked(LINK(this, AIChatPanel, OnFilterWorkspaceClicked));
    m_xSortWorkspaceButton->connect_clicked(LINK(this, AIChatPanel, OnSortWorkspaceClicked));
    m_xRemoveArtifactButton->connect_clicked(
        LINK(this, AIChatPanel, OnRemoveArtifactClicked));

    LoadDocumentHistory();
    LoadArtifactNavigator();
    LoadSessionSnapshot();
    SetState(AIChatPanelState::Idle);
    UpdateActions();
    FocusPrompt();
}

AIChatPanel::~AIChatPanel() = default;

void AIChatPanel::AppendTranscript(const OUString& rSpeaker, const OUString& rMessage)
{
    AppendTranscript(rSpeaker, rMessage, true);
}

void AIChatPanel::AppendTranscript(const OUString& rSpeaker, const OUString& rMessage,
                                   bool bPersistHistory)
{
    OUString sText = m_xTranscriptView->get_text();
    if (!sText.isEmpty())
        sText += u"\n\n"_ustr;
    sText += rSpeaker + u": "_ustr + rMessage;
    m_xTranscriptView->set_text(sText);
    m_xTranscriptView->set_position(-1);

    if (bPersistHistory && m_xHistoryStore)
        m_xHistoryStore->AppendMessage(rSpeaker, rMessage);
}

void AIChatPanel::AppendAssistantMarkdown(const OUString& rMarkdown)
{
    const AIChatMarkdownRenderResult aRendered = RenderMarkdownSubset(rMarkdown);
    if (aRendered.Rejected)
    {
        AppendTranscript(u"System"_ustr,
                         u"Markdown rejected: "_ustr + aRendered.RejectionReason);
        return;
    }

    AppendTranscript(u"AI"_ustr, aRendered.Text);
}

void AIChatPanel::AppendAssistantChunk(const OUString& rChunk)
{
    if (!m_sStreamingBuffer.isEmpty())
        m_sStreamingBuffer += u" "_ustr;
    m_sStreamingBuffer += rChunk;
    AppendAssistantMarkdown(rChunk);
}

void AIChatPanel::AppendTerminalEvidence(const OUString& rStatus, const OUString& rEvidenceId)
{
    OUString aMessage = u"terminal-state="_ustr + rStatus;
    if (!rEvidenceId.isEmpty())
        aMessage += u" evidence="_ustr + rEvidenceId;
    AppendTranscript(u"System"_ustr, aMessage);
}

void AIChatPanel::LoadDocumentHistory()
{
    if (!m_xHistoryStore)
        return;

    const OUString sTranscript = m_xHistoryStore->LoadTranscript();
    if (sTranscript.isEmpty())
        return;

    m_xTranscriptView->set_text(sTranscript);
    m_xTranscriptView->set_position(-1);
    AppendTranscript(u"System"_ustr,
                     u"history-loaded document-id-hash="_ustr + m_xHistoryStore->GetDocumentKey(),
                     false);
}

void AIChatPanel::ClearDocumentHistory()
{
    if (!m_xHistoryStore)
        return;

    const bool bCleared = m_xHistoryStore->Clear();
    m_xTranscriptView->set_text(OUString());
    AppendTranscript(u"System"_ustr,
                     bCleared ? u"history-cleared for current document"_ustr
                              : u"history-clear-failed for current document"_ustr,
                     false);
    FocusPrompt();
}

AIChatContextMentions AIChatPanel::ParseContextMentions(const OUString& rPrompt)
{
    AIChatContextMentions aResult;

    sal_Int32 nPos = 0;
    while (nPos < rPrompt.getLength())
    {
        if (rPrompt[nPos] != u'@')
        {
            ++nPos;
            continue;
        }

        sal_Int32 nEnd = nPos + 1;
        while (nEnd < rPrompt.getLength() && !IsMentionBoundary(rPrompt[nEnd]))
            ++nEnd;

        const OUString sMention = rPrompt.copy(nPos, nEnd - nPos);
        if (sMention == u"@selection"_ustr || sMention == u"@doc"_ustr
            || IsValidConnectorMention(sMention))
        {
            aResult.ValidMentions.push_back(sMention);
        }
        else
        {
            aResult.InvalidMentions.push_back(sMention);
        }

        nPos = nEnd;
    }

    return aResult;
}

OUString AIChatPanel::FormatContextMentionSummary(const AIChatContextMentions& rMentions)
{
    if (rMentions.HasInvalid())
        return u"Invalid context mention: "_ustr + rMentions.InvalidMentions.front()
               + u" (allowed: @selection, @doc, @connector:<id>)"_ustr;

    if (rMentions.ValidMentions.empty())
        return CONTEXT_MENTION_SUGGESTIONS;

    OUStringBuffer aBuffer(u"Explicit context: "_ustr);
    for (size_t i = 0; i < rMentions.ValidMentions.size(); ++i)
    {
        if (i > 0)
            aBuffer.append(u", "_ustr);
        aBuffer.append(rMentions.ValidMentions[i]);
    }
    return aBuffer.makeStringAndClear();
}

void AIChatPanel::UpdateContextMentions()
{
    const OUString sPrompt = m_xPromptEntry->get_text();
    const AIChatContextMentions aMentions = ParseContextMentions(sPrompt);

    m_xPromptEntry->set_message_type(aMentions.HasInvalid() ? weld::EntryMessageType::Error
                                                            : weld::EntryMessageType::Normal);

    if (sPrompt.indexOf('@') >= 0)
        m_xStatusLabel->set_label(FormatContextMentionSummary(aMentions));
    else
        m_xStatusLabel->set_label(u"State: "_ustr + StateToLabel(m_eState));
}

bool AIChatPanel::ValidateContextMentions(const OUString& rPrompt)
{
    const AIChatContextMentions aMentions = ParseContextMentions(rPrompt);
    if (!aMentions.HasInvalid())
        return true;

    m_xPromptEntry->set_message_type(weld::EntryMessageType::Error);
    AppendTranscript(u"System"_ustr, FormatContextMentionSummary(aMentions));
    SetState(AIChatPanelState::Failed);
    FocusPrompt();
    return false;
}

bool AIChatPanel::MaterializeInsertedContent(OUString& rInsertedText)
{
    if (!m_xContentObjectStore || !m_xContentObjectStore->ShouldMaterializeText(rInsertedText))
        return true;

    const AIChatMaterializedContent aContent = m_xContentObjectStore->MaterializeText(rInsertedText);
    rInsertedText = aContent.Reference;

    const OUString sMessage = u"materialized-content reference="_ustr + aContent.Reference
                              + u" type="_ustr
                              + AIChatContentObjectStore::DetectTypeLabel(aContent.Type);
    AppendTranscript(u"System"_ustr, sMessage);
    m_xStatusLabel->set_label(u"Content object inserted: "_ustr + aContent.Reference);
    LoadArtifactNavigator();
    RecordWorkspaceActivity(u"artifact-created"_ustr, u"artifacts"_ustr, aContent.ObjectId,
                            u"evidence:local-materialized:"_ustr + aContent.ObjectId,
                            aContent.Reference, u"sidebar-preview"_ustr);
    SaveSessionSnapshot(aContent.ObjectId, u"evidence:local-materialized:"_ustr + aContent.ObjectId,
                        u"metadata-summary"_ustr, OUString(), aContent.Reference);
    return true;
}

OUString AIChatPanel::FormatArtifactRow(const AIChatContentRegistryEntry& rEntry)
{
    OUString sBadge = rEntry.EvidenceId.isEmpty() ? u"no-evidence"_ustr : u"evidence"_ustr;
    return rEntry.Type + u" | "_ustr + rEntry.State + u" | "_ustr + sBadge + u" | "_ustr
           + rEntry.HashReference;
}

OUString AIChatPanel::FormatArtifactDetails(const AIChatContentRegistryEntry& rEntry)
{
    AIChatPreviewMatrix aPreviewMatrix;
    const AIChatPreviewResult aPreview = aPreviewMatrix.BuildPreview(rEntry);

    return u"id="_ustr + rEntry.ObjectId + u"\n"_ustr + u"type="_ustr + rEntry.Type
           + u"\n"_ustr + u"source="_ustr + rEntry.SourceSurface + u"\n"_ustr
           + u"state="_ustr + rEntry.State + u"\n"_ustr + u"evidence="_ustr
           + (rEntry.EvidenceId.isEmpty() ? u"(none)"_ustr : rEntry.EvidenceId)
           + u"\n"_ustr + u"open-target="_ustr + rEntry.OpenTarget + u"\n"_ustr
           + u"preview-target="_ustr + aPreview.Target + u"\n"_ustr
           + u"preview-mode="_ustr + aPreview.Mode + u"\n"_ustr
           + u"preview-summary="_ustr + aPreview.Summary + u"\n"_ustr
           + u"source-metadata="_ustr + aPreview.SourceMetadata;
}

void AIChatPanel::LoadArtifactNavigator()
{
    AIChatContentRegistry aRegistry;
    m_aArtifacts = aRegistry.LoadEntries();

    m_xArtifactTree->clear();
    for (const auto& rEntry : m_aArtifacts)
        m_xArtifactTree->append(rEntry.ObjectId, FormatArtifactRow(rEntry));

    if (!m_aArtifacts.empty())
        m_xArtifactTree->select(0);

    UpdateArtifactDetails();
}

OUString AIChatPanel::GetSelectedArtifactId() const
{
    const int nSelected = m_xArtifactTree->get_selected_index();
    if (nSelected < 0)
        return OUString();
    return m_xArtifactTree->get_id(nSelected);
}

const AIChatContentRegistryEntry* AIChatPanel::FindSelectedArtifact() const
{
    const OUString sSelectedId = GetSelectedArtifactId();
    if (sSelectedId.isEmpty())
        return nullptr;

    const auto it = std::find_if(m_aArtifacts.begin(), m_aArtifacts.end(),
                                 [&sSelectedId](const AIChatContentRegistryEntry& rEntry) {
                                     return rEntry.ObjectId == sSelectedId;
                                 });
    return it == m_aArtifacts.end() ? nullptr : &*it;
}

void AIChatPanel::UpdateArtifactDetails()
{
    const AIChatContentRegistryEntry* pSelected = FindSelectedArtifact();
    const bool bHasSelection = pSelected != nullptr;
    const bool bBusy = m_eState == AIChatPanelState::Requesting
                       || m_eState == AIChatPanelState::Streaming;
    const bool bHasRetryPrompt = !m_sLastPrompt.isEmpty();

    m_xOpenArtifactButton->set_sensitive(AIChatWorkspaceActionBarStore::IsCommandEnabled(
        u"open-preview"_ustr, pSelected, bBusy, bHasRetryPrompt));
    m_xOpenDiffReviewButton->set_sensitive(AIChatWorkspaceActionBarStore::IsCommandEnabled(
        u"open-diff-review"_ustr, pSelected, bBusy, bHasRetryPrompt));
    m_xReviewArtifactButton->set_sensitive(
        bHasSelection && AIChatContentReviewStore::IsSupportedSourceType(pSelected->Type));
    m_xFormatArtifactButton->set_sensitive(
        bHasSelection && AIChatFormattingReviewStore::IsSupportedFormattingScope(pSelected->Type));
    m_xInspectEvidenceButton->set_sensitive(AIChatWorkspaceActionBarStore::IsCommandEnabled(
        u"export-evidence"_ustr, pSelected, bBusy, bHasRetryPrompt)
        && bHasSelection && AIChatEvidenceInspector::IsSupportedSourceType(pSelected->Type));
    m_xApproveSelectedButton->set_sensitive(AIChatWorkspaceActionBarStore::IsCommandEnabled(
        u"approve-selected"_ustr, pSelected, bBusy, bHasRetryPrompt));
    m_xRejectSelectedButton->set_sensitive(AIChatWorkspaceActionBarStore::IsCommandEnabled(
        u"reject-selected"_ustr, pSelected, bBusy, bHasRetryPrompt));
    m_xCopyReferenceButton->set_sensitive(AIChatWorkspaceActionBarStore::IsCommandEnabled(
        u"copy-reference"_ustr, pSelected, bBusy, bHasRetryPrompt));
    m_xExportEvidenceButton->set_sensitive(AIChatWorkspaceActionBarStore::IsCommandEnabled(
        u"export-evidence"_ustr, pSelected, bBusy, bHasRetryPrompt));
    m_xFilterWorkspaceButton->set_sensitive(AIChatWorkspaceActionBarStore::IsCommandEnabled(
        u"filter"_ustr, pSelected, bBusy, bHasRetryPrompt));
    m_xSortWorkspaceButton->set_sensitive(AIChatWorkspaceActionBarStore::IsCommandEnabled(
        u"sort"_ustr, pSelected, bBusy, bHasRetryPrompt));
    m_xRemoveArtifactButton->set_sensitive(bHasSelection);

    if (!bHasSelection)
    {
        m_xArtifactDetailsLabel->set_label(u"No artifact selected"_ustr);
        return;
    }

    m_xArtifactDetailsLabel->set_label(FormatArtifactDetails(*pSelected));
}

void AIChatPanel::OpenSelectedArtifact()
{
    const AIChatContentRegistryEntry* pSelected = FindSelectedArtifact();
    if (!pSelected)
    {
        AppendTranscript(u"System"_ustr,
                         u"open-failed reason=missing-registry-entry id="_ustr
                             + GetSelectedArtifactId());
        return;
    }

    if (!DispatchWorkspaceAction(u"open-preview"_ustr))
        return;
    AIChatContentOpener aOpener;
    const AIChatContentOpenResult aResult = aOpener.OpenReadOnlyPreview(*pSelected);
    AppendTranscript(u"System"_ustr, aResult.Message);
    m_xStatusLabel->set_label(aResult.Success ? u"Opened artifact: "_ustr + pSelected->ObjectId
                                              : u"Open failed: "_ustr + pSelected->ObjectId);
    RecordWorkspaceActivity(aResult.Success ? u"content-opened"_ustr : u"failure-reported"_ustr,
                            u"previews"_ustr, pSelected->ObjectId, pSelected->EvidenceId,
                            pSelected->HashReference, aResult.Target);
    SaveSessionSnapshot(pSelected->ObjectId, pSelected->EvidenceId, aResult.PreviewMode,
                        aResult.Success ? OUString() : aResult.Message,
                        pSelected->HashReference);
}

void AIChatPanel::ReviewSelectedArtifact()
{
    const OUString sSelectedId = GetSelectedArtifactId();
    if (sSelectedId.isEmpty())
        return;

    const auto it = std::find_if(m_aArtifacts.begin(), m_aArtifacts.end(),
                                 [&sSelectedId](const AIChatContentRegistryEntry& rEntry) {
                                     return rEntry.ObjectId == sSelectedId;
                                 });
    if (it == m_aArtifacts.end())
    {
        AppendTranscript(u"System"_ustr,
                         u"review-create-failed reason=missing-registry-entry id="_ustr
                             + sSelectedId);
        return;
    }

    const AIChatContentReviewCreateResult aReview
        = m_xContentReviewStore->CreateReviewFromSource(*it);
    AppendTranscript(u"System"_ustr, aReview.Message);
    if (!aReview.Success)
    {
        m_xStatusLabel->set_label(u"Review failed: "_ustr + sSelectedId);
        RecordWorkspaceActivity(u"failure-reported"_ustr, u"reviews"_ustr, it->ObjectId,
                                it->EvidenceId, it->HashReference, u"review-queue"_ustr);
        SaveReviewSessionSnapshot(it->ObjectId, OUString(), it->EvidenceId,
                                  u"diff-preview"_ustr, u"failed"_ustr, aReview.Message,
                                  it->HashReference);
        return;
    }

    AIChatContentOpener aOpener;
    const AIChatContentOpenResult aOpenResult
        = aOpener.OpenReadOnlyPreview(aReview.RegistryEntry);
    AppendTranscript(u"System"_ustr, aOpenResult.Message);

    m_xStatusLabel->set_label(u"Review queued: "_ustr + aReview.Review.ReviewId);
    RecordWorkspaceReviewActivity(u"review-opened"_ustr, u"reviews"_ustr, it->ObjectId,
                                  aReview.Review.ReviewId, aReview.Review.EvidenceId,
                                  aReview.Review.HashReference, aReview.Review.OpenTarget);
    RecordWorkspaceReviewActivity(u"review-state-changed"_ustr, u"reviews"_ustr, it->ObjectId,
                                  aReview.Review.ReviewId, aReview.Review.EvidenceId,
                                  aReview.Review.HashReference, aReview.Review.OpenTarget);
    SyncReviewState(aReview.Review.ReviewId, u"open"_ustr, aReview.Review.State,
                    u"diff-review"_ustr, aReview.Review.EvidenceId,
                    aReview.Review.HashReference, aReview.Review.OpenTarget,
                    aReview.Review.PreviewMode);
    SaveReviewSessionSnapshot(it->ObjectId, aReview.Review.ReviewId,
                              aReview.Review.EvidenceId, aReview.Review.PreviewMode,
                              aReview.Review.State, aOpenResult.Success ? OUString()
                                                                        : aOpenResult.Message,
                              aReview.Review.HashReference);
    LoadArtifactNavigator();
}

void AIChatPanel::ReviewSelectedFormatting()
{
    const OUString sSelectedId = GetSelectedArtifactId();
    if (sSelectedId.isEmpty())
        return;

    const auto it = std::find_if(m_aArtifacts.begin(), m_aArtifacts.end(),
                                 [&sSelectedId](const AIChatContentRegistryEntry& rEntry) {
                                     return rEntry.ObjectId == sSelectedId;
                                 });
    if (it == m_aArtifacts.end())
    {
        AppendTranscript(u"System"_ustr,
                         u"formatting-review-create-failed reason=missing-registry-entry id="_ustr
                             + sSelectedId);
        return;
    }

    const AIChatFormattingReviewCreateResult aReview
        = m_xFormattingReviewStore->CreateReviewFromSource(*it);
    AppendTranscript(u"System"_ustr, aReview.Message);
    if (!aReview.Success)
    {
        m_xStatusLabel->set_label(u"Formatting review failed: "_ustr + sSelectedId);
        RecordWorkspaceActivity(u"failure-reported"_ustr, u"reviews"_ustr, it->ObjectId,
                                it->EvidenceId, it->HashReference, u"review-queue"_ustr);
        SaveReviewSessionSnapshot(it->ObjectId, OUString(), it->EvidenceId,
                                  u"diff-preview"_ustr, u"failed"_ustr, aReview.Message,
                                  it->HashReference);
        return;
    }

    AIChatContentOpener aOpener;
    const AIChatContentOpenResult aOpenResult
        = aOpener.OpenReadOnlyPreview(aReview.RegistryEntry);
    AppendTranscript(u"System"_ustr, aOpenResult.Message);

    m_xStatusLabel->set_label(u"Formatting review queued: "_ustr + aReview.Review.ReviewId);
    RecordWorkspaceReviewActivity(u"review-opened"_ustr, u"reviews"_ustr, it->ObjectId,
                                  aReview.Review.ReviewId, aReview.Review.EvidenceId,
                                  aReview.Review.HashReference, aReview.Review.OpenTarget);
    RecordWorkspaceReviewActivity(u"review-state-changed"_ustr, u"reviews"_ustr, it->ObjectId,
                                  aReview.Review.ReviewId, aReview.Review.EvidenceId,
                                  aReview.Review.HashReference, aReview.Review.OpenTarget);
    SyncReviewState(aReview.Review.ReviewId, u"open"_ustr, aReview.Review.State,
                    u"diff-review"_ustr, aReview.Review.EvidenceId,
                    aReview.Review.HashReference, aReview.Review.OpenTarget,
                    aReview.Review.PreviewMode);
    SaveReviewSessionSnapshot(it->ObjectId, aReview.Review.ReviewId,
                              aReview.Review.EvidenceId, aReview.Review.PreviewMode,
                              aReview.Review.State, aOpenResult.Success ? OUString()
                                                                        : aOpenResult.Message,
                              aReview.Review.HashReference);
    LoadArtifactNavigator();
}

void AIChatPanel::InspectSelectedEvidence()
{
    const AIChatContentRegistryEntry* pSelected = FindSelectedArtifact();
    if (!pSelected)
    {
        AppendTranscript(u"System"_ustr,
                         u"evidence-inspection-failed reason=missing-registry-entry id="_ustr
                             + GetSelectedArtifactId());
        return;
    }

    if (!DispatchWorkspaceAction(u"export-evidence"_ustr))
        return;
    const AIChatEvidenceInspectionResult aInspection = m_xEvidenceInspector->Inspect(*pSelected);
    AppendTranscript(u"System"_ustr, aInspection.Summary);
    m_xStatusLabel->set_label(aInspection.Success ? u"Evidence inspected: "_ustr
                                                        + pSelected->ObjectId
                                                  : u"Evidence inspection failed: "_ustr
                                                        + pSelected->ObjectId);
    RecordWorkspaceActivity(aInspection.Success ? u"evidence-linked"_ustr
                                                : u"failure-reported"_ustr,
                            u"evidence"_ustr, pSelected->ObjectId, pSelected->EvidenceId,
                            pSelected->HashReference, u"evidence-inspector"_ustr);
    SaveSessionSnapshot(pSelected->ObjectId, pSelected->EvidenceId, u"evidence-summary"_ustr,
                        aInspection.Success ? OUString() : aInspection.Summary,
                        pSelected->HashReference);
}

bool AIChatPanel::DispatchWorkspaceAction(const OUString& rCommand)
{
    if (!m_xWorkspaceActionBarStore)
        return false;

    const bool bBusy = m_eState == AIChatPanelState::Requesting
                       || m_eState == AIChatPanelState::Streaming;
    const AIChatContentRegistryEntry* pSelected = FindSelectedArtifact();
    const AIChatWorkspaceActionBarDispatchResult aResult
        = m_xWorkspaceActionBarStore->DispatchCommand(rCommand, pSelected, bBusy,
                                                      !m_sLastPrompt.isEmpty());
    AppendTranscript(u"System"_ustr, aResult.Message);

    if (!aResult.Success)
    {
        m_xStatusLabel->set_label(u"Workspace action failed: "_ustr + rCommand);
        RecordWorkspaceActivity(u"failure-reported"_ustr, u"action-bar"_ustr,
                                aResult.TargetId, aResult.EvidenceId, aResult.HashReference,
                                aResult.OpenTarget);
        SaveSessionSnapshot(aResult.TargetId, aResult.EvidenceId, aResult.PreviewMode,
                            aResult.Message, aResult.HashReference);
        return false;
    }

    if ((rCommand == u"approve-selected"_ustr || rCommand == u"reject-selected"_ustr)
        && m_xReviewQueueStore)
    {
        AIChatReviewQueueEntry aQueueEntry;
        aQueueEntry.ReviewId = aResult.TargetId;
        aQueueEntry.ItemType = pSelected ? AIChatReviewQueueStore::ResolveItemType(*pSelected)
                                         : u"content-review"_ustr;
        aQueueEntry.State = aResult.ReviewState;
        aQueueEntry.SourceSurface = u"review-queue"_ustr;
        aQueueEntry.EvidenceId = aResult.EvidenceId;
        aQueueEntry.HashReference = aResult.HashReference;
        aQueueEntry.OpenTarget = aResult.OpenTarget;
        aQueueEntry.PreviewMode = aResult.PreviewMode;
        const OUString sTransition
            = rCommand == u"approve-selected"_ustr ? u"approve"_ustr : u"reject"_ustr;
        const bool bTransitioned = m_xReviewQueueStore->TransitionState(
            aQueueEntry, aResult.ReviewState, sTransition);
        OUString sTransitionMessage;
        if (bTransitioned)
        {
            sTransitionMessage
                = u"review-state-transitioned id="_ustr + aResult.TargetId + u" state="_ustr
                  + aResult.ReviewState
                  + u" explicit-human-approval=true main-document-mutation=false"_ustr;
        }
        else
        {
            sTransitionMessage = u"review-state-transition-failed id="_ustr + aResult.TargetId
                                 + u" state="_ustr + aResult.ReviewState;
        }
        AppendTranscript(u"System"_ustr, sTransitionMessage);
    }

    const OUString sSurface
        = (rCommand == u"approve-selected"_ustr || rCommand == u"reject-selected"_ustr)
              ? u"reviews"_ustr
              : (rCommand == u"export-evidence"_ustr ? u"evidence"_ustr
                                                      : u"action-bar"_ustr);

    if (!aResult.ReviewState.isEmpty())
    {
        const OUString sTransition
            = rCommand == u"approve-selected"_ustr ? u"approve"_ustr
              : rCommand == u"reject-selected"_ustr
                  ? u"reject"_ustr
                  : AIChatReviewStateSyncStore::TransitionForState(aResult.ReviewState);
        SyncReviewState(aResult.TargetId, sTransition, aResult.ReviewState,
                        rCommand == u"open-diff-review"_ustr ? u"diff-review"_ustr
                                                             : u"action-bar"_ustr,
                        aResult.EvidenceId, aResult.HashReference, aResult.OpenTarget,
                        aResult.PreviewMode);
        RecordWorkspaceReviewActivity(u"review-state-changed"_ustr, sSurface,
                                      pSelected ? pSelected->ObjectId : aResult.TargetId,
                                      aResult.TargetId, aResult.EvidenceId,
                                      aResult.HashReference, aResult.OpenTarget);
        SaveReviewSessionSnapshot(pSelected ? pSelected->ObjectId : aResult.TargetId,
                                  aResult.TargetId, aResult.EvidenceId,
                                  aResult.PreviewMode, aResult.ReviewState, OUString(),
                                  aResult.HashReference);
    }
    else
    {
        RecordWorkspaceActivity(u"action-invoked"_ustr, sSurface, aResult.TargetId,
                                aResult.EvidenceId, aResult.HashReference,
                                aResult.OpenTarget);
        SaveSessionSnapshot(aResult.TargetId, aResult.EvidenceId, aResult.PreviewMode,
                            OUString(), aResult.HashReference);
    }

    m_xStatusLabel->set_label(u"Workspace action: "_ustr + rCommand);
    if (rCommand == u"filter"_ustr)
        m_xStatusLabel->set_label(u"Workspace filter visible: state,type,surface"_ustr);
    else if (rCommand == u"sort"_ustr)
        m_xStatusLabel->set_label(u"Workspace sort visible: recent-first"_ustr);
    return true;
}

void AIChatPanel::SyncReviewState(const OUString& rReviewId, const OUString& rTransitionEvent,
                                  const OUString& rState, const OUString& rSurface,
                                  const OUString& rEvidenceId,
                                  const OUString& rHashReference,
                                  const OUString& rOpenTarget,
                                  const OUString& rPreviewMode)
{
    if (!m_xReviewStateSyncStore || rReviewId.isEmpty())
        return;

    const AIChatReviewStateSyncResult aSync = m_xReviewStateSyncStore->RecordTransition(
        rReviewId, rTransitionEvent, AIChatReviewStateSyncStore::NormalizeRegistryState(rState),
        rSurface, rEvidenceId, rHashReference, rOpenTarget, rPreviewMode);
    AppendTranscript(u"System"_ustr, aSync.Message);
    if (!aSync.Success)
    {
        RecordWorkspaceReviewActivity(u"failure-reported"_ustr, rSurface, rReviewId, rReviewId,
                                      rEvidenceId, rHashReference, rOpenTarget);
        SaveReviewSessionSnapshot(rReviewId, rReviewId, rEvidenceId, rPreviewMode,
                                  u"failed"_ustr, aSync.Message, rHashReference);
    }
}

void AIChatPanel::RemoveSelectedArtifact()
{
    const OUString sSelectedId = GetSelectedArtifactId();
    if (sSelectedId.isEmpty())
        return;

    AIChatContentRegistry aRegistry;
    if (aRegistry.ArchiveObject(sSelectedId))
    {
        AppendTranscript(u"System"_ustr, u"artifact-archived id="_ustr + sSelectedId);
        RecordWorkspaceActivity(u"action-invoked"_ustr, u"artifacts"_ustr, sSelectedId,
                                OUString(), u"@artifact:"_ustr + sSelectedId,
                                u"sidebar-preview"_ustr);
        SaveSessionSnapshot(OUString(), OUString(), u"metadata-summary"_ustr, OUString(),
                            u"@artifact:"_ustr + sSelectedId);
        LoadArtifactNavigator();
    }
    else
    {
        AppendTranscript(u"System"_ustr, u"artifact-archive-failed id="_ustr + sSelectedId);
    }
}

void AIChatPanel::LoadSessionSnapshot()
{
    if (!m_xSessionStore)
        return;

    const AIChatSessionSnapshot aSnapshot = m_xSessionStore->LoadSnapshot();
    if (aSnapshot.DocumentBinding.isEmpty())
        return;

    AppendTranscript(u"System"_ustr,
                     u"resume-summary document-id-hash="_ustr + aSnapshot.DocumentBinding
                         + u" open-artifact="_ustr + aSnapshot.OpenArtifactId
                         + u" open-review="_ustr + aSnapshot.OpenReviewId
                         + u" preview-mode="_ustr + aSnapshot.PreviewMode
                         + u" review-state="_ustr + aSnapshot.ReviewState
                         + u" activity-cursor="_ustr + aSnapshot.ActivityCursor,
                     false);
}

void AIChatPanel::RecordWorkspaceActivity(const OUString& rEvent, const OUString& rSurface,
                                          const OUString& rArtifactId,
                                          const OUString& rEvidenceId,
                                          const OUString& rHashReference,
                                          const OUString& rOpenTarget)
{
    if (!m_xSessionStore)
        return;

    AIChatWorkspaceActivityEntry aEntry;
    aEntry.Event = rEvent;
    aEntry.Surface = rSurface;
    aEntry.Actor = u"user"_ustr;
    aEntry.Timestamp = AIChatWorkspaceSessionStore::MakeTimestamp();
    aEntry.ArtifactId = rArtifactId;
    aEntry.ReviewId = OUString();
    aEntry.EvidenceId = rEvidenceId;
    aEntry.HashReference = rHashReference;
    aEntry.OpenTarget = rOpenTarget;
    m_xSessionStore->RecordActivity(aEntry);
}

void AIChatPanel::RecordWorkspaceReviewActivity(const OUString& rEvent, const OUString& rSurface,
                                                const OUString& rArtifactId,
                                                const OUString& rReviewId,
                                                const OUString& rEvidenceId,
                                                const OUString& rHashReference,
                                                const OUString& rOpenTarget)
{
    if (!m_xSessionStore)
        return;

    AIChatWorkspaceActivityEntry aEntry;
    aEntry.Event = rEvent;
    aEntry.Surface = rSurface;
    aEntry.Actor = u"user"_ustr;
    aEntry.Timestamp = AIChatWorkspaceSessionStore::MakeTimestamp();
    aEntry.ArtifactId = rArtifactId;
    aEntry.ReviewId = rReviewId;
    aEntry.EvidenceId = rEvidenceId;
    aEntry.HashReference = rHashReference;
    aEntry.OpenTarget = rOpenTarget;
    m_xSessionStore->RecordActivity(aEntry);
}

void AIChatPanel::SaveSessionSnapshot(const OUString& rOpenArtifactId,
                                      const OUString& rActiveEvidenceId,
                                      const OUString& rPreviewMode,
                                      const OUString& rFailureState,
                                      const OUString& rHashReference)
{
    if (!m_xSessionStore)
        return;

    AIChatSessionSnapshot aSnapshot;
    aSnapshot.DocumentBinding = m_xSessionStore->GetDocumentBinding();
    aSnapshot.Timestamp = AIChatWorkspaceSessionStore::MakeTimestamp();
    aSnapshot.ActiveTaskId = OUString();
    aSnapshot.OpenArtifactId = rOpenArtifactId;
    aSnapshot.OpenReviewId = OUString();
    aSnapshot.ActiveEvidenceId = rActiveEvidenceId;
    aSnapshot.PreviewMode = rPreviewMode;
    aSnapshot.ReviewState = u"none"_ustr;
    aSnapshot.ActivityCursor = aSnapshot.Timestamp;
    aSnapshot.FailureState = rFailureState;
    aSnapshot.HashReference = rHashReference;
    m_xSessionStore->SaveSnapshot(aSnapshot);
}

void AIChatPanel::SaveReviewSessionSnapshot(const OUString& rOpenArtifactId,
                                            const OUString& rOpenReviewId,
                                            const OUString& rActiveEvidenceId,
                                            const OUString& rPreviewMode,
                                            const OUString& rReviewState,
                                            const OUString& rFailureState,
                                            const OUString& rHashReference)
{
    if (!m_xSessionStore)
        return;

    AIChatSessionSnapshot aSnapshot;
    aSnapshot.DocumentBinding = m_xSessionStore->GetDocumentBinding();
    aSnapshot.Timestamp = AIChatWorkspaceSessionStore::MakeTimestamp();
    aSnapshot.ActiveTaskId = OUString();
    aSnapshot.OpenArtifactId = rOpenArtifactId;
    aSnapshot.OpenReviewId = rOpenReviewId;
    aSnapshot.ActiveEvidenceId = rActiveEvidenceId;
    aSnapshot.PreviewMode = rPreviewMode;
    aSnapshot.ReviewState = rReviewState;
    aSnapshot.ActivityCursor = aSnapshot.Timestamp;
    aSnapshot.FailureState = rFailureState;
    aSnapshot.HashReference = rHashReference;
    m_xSessionStore->SaveSnapshot(aSnapshot);
}

void AIChatPanel::FocusPrompt()
{
    m_xPromptEntry->grab_focus();
    m_xPromptEntry->set_position(-1);
}

void AIChatPanel::UpdateActions()
{
    const bool bHasPrompt = !m_xPromptEntry->get_text().trim().isEmpty();
    const bool bBusy = m_eState == AIChatPanelState::Requesting
                       || m_eState == AIChatPanelState::Streaming;
    m_xPromptEntry->set_sensitive(!bBusy);
    m_xSendButton->set_sensitive(bHasPrompt && !bBusy);
    m_xCancelButton->set_sensitive(bBusy);
    m_xRetryButton->set_sensitive(!m_sLastPrompt.isEmpty() && !bBusy);
    m_xClearHistoryButton->set_sensitive(!bBusy);
    UpdateArtifactDetails();
}

OUString AIChatPanel::StateToLabel(AIChatPanelState eState)
{
    switch (eState)
    {
        case AIChatPanelState::Idle:
            return u"idle"_ustr;
        case AIChatPanelState::Requesting:
            return u"requesting"_ustr;
        case AIChatPanelState::Streaming:
            return u"streaming"_ustr;
        case AIChatPanelState::AwaitingRuntime:
            return u"awaiting-runtime"_ustr;
        case AIChatPanelState::AwaitingApproval:
            return u"awaiting-approval"_ustr;
        case AIChatPanelState::Applied:
            return u"applied"_ustr;
        case AIChatPanelState::Failed:
            return u"failed"_ustr;
        case AIChatPanelState::Cancelled:
            return u"cancelled"_ustr;
    }
    return u"idle"_ustr;
}

void AIChatPanel::SetState(AIChatPanelState eState)
{
    m_eState = eState;
    m_xStatusLabel->set_label(u"State: "_ustr + StateToLabel(eState));
    if (m_xPromptEntry && m_xPromptEntry->get_text().indexOf('@') >= 0)
        UpdateContextMentions();
    UpdateActions();
}

css::ai::ProviderResponse AIChatPanel::CallProvider(const OUString& rPrompt)
{
    try
    {
        css::uno::Reference<css::uno::XComponentContext> xContext
            = comphelper::getProcessComponentContext();
        if (!xContext.is() || !xContext->getServiceManager().is())
            return MakeLocalFailure(u"provider-error"_ustr,
                                    u"V2 Provider service manager is unavailable"_ustr);

        css::uno::Reference<css::ai::XProvider> xProvider(
            xContext->getServiceManager()->createInstanceWithContext(V2_PROVIDER_SERVICE_NAME,
                                                                      xContext),
            css::uno::UNO_QUERY);
        if (!xProvider.is())
            return MakeLocalFailure(u"provider-error"_ustr,
                                    u"V2 Provider service is unavailable"_ustr);

        css::ai::ProviderRequest aRequest;
        aRequest.capability = u"summarize"_ustr;
        aRequest.prompt = rPrompt;
        aRequest.context = OUString();
        aRequest.timeoutMs = PROVIDER_TIMEOUT_MS;
        return xProvider->call(aRequest);
    }
    catch (const css::lang::IllegalArgumentException& rException)
    {
        return MakeLocalFailure(u"provider-error"_ustr, rException.Message);
    }
    catch (const css::uno::Exception& rException)
    {
        return MakeLocalFailure(u"provider-error"_ustr, rException.Message);
    }
}

void AIChatPanel::SubmitPrompt()
{
    const OUString sPrompt = m_xPromptEntry->get_text().trim();
    if (sPrompt.isEmpty())
        return;
    if (!ValidateContextMentions(sPrompt))
        return;

    m_sLastPrompt = sPrompt;
    m_sStreamingBuffer.clear();
    SetState(AIChatPanelState::Requesting);
    AppendTranscript(u"User"_ustr, sPrompt);
    m_xPromptEntry->set_text(OUString());

    const css::ai::ProviderResponse aResponse = CallProvider(sPrompt);
    SetState(AIChatPanelState::Streaming);

    if (aResponse.content.isEmpty())
    {
        AppendAssistantChunk(u"(empty provider response)"_ustr);
    }
    else
    {
        sal_Int32 nIndex = 0;
        do
        {
            OUString aChunk = aResponse.content.getToken(0, ' ', nIndex);
            if (!aChunk.isEmpty())
                AppendAssistantChunk(aChunk);
        } while (nIndex >= 0);
    }

    if (aResponse.status == u"ok"_ustr)
    {
        SetState(AIChatPanelState::AwaitingApproval);
        AppendTerminalEvidence(aResponse.status, aResponse.evidenceId);
    }
    else
    {
        SetState(AIChatPanelState::Failed);
        AppendTerminalEvidence(aResponse.status, aResponse.evidenceId);
    }
    FocusPrompt();
}

IMPL_LINK_NOARG(AIChatPanel, OnPromptChanged, weld::Entry&, void)
{
    UpdateContextMentions();
    UpdateActions();
}

IMPL_LINK(AIChatPanel, OnPromptInsertText, OUString&, rInsertedText, bool)
{
    return MaterializeInsertedContent(rInsertedText);
}

IMPL_LINK_NOARG(AIChatPanel, OnArtifactSelectionChanged, weld::TreeView&, void)
{
    UpdateArtifactDetails();
}

IMPL_LINK_NOARG(AIChatPanel, OnArtifactRowActivated, weld::TreeView&, bool)
{
    OpenSelectedArtifact();
    return true;
}

IMPL_LINK_NOARG(AIChatPanel, OnRefreshArtifactsClicked, weld::Button&, void)
{
    LoadArtifactNavigator();
}

IMPL_LINK_NOARG(AIChatPanel, OnOpenArtifactClicked, weld::Button&, void)
{
    OpenSelectedArtifact();
}

IMPL_LINK_NOARG(AIChatPanel, OnOpenDiffReviewClicked, weld::Button&, void)
{
    const AIChatContentRegistryEntry* pSelected = FindSelectedArtifact();
    if (!DispatchWorkspaceAction(u"open-diff-review"_ustr) || !pSelected)
        return;

    AIChatContentOpener aOpener;
    const AIChatContentOpenResult aResult = aOpener.OpenReadOnlyPreview(*pSelected);
    AppendTranscript(u"System"_ustr, aResult.Message);
    m_xStatusLabel->set_label(aResult.Success ? u"DiffReview opened: "_ustr
                                                    + pSelected->ObjectId
                                              : u"DiffReview open failed: "_ustr
                                                    + pSelected->ObjectId);
    RecordWorkspaceReviewActivity(aResult.Success ? u"review-opened"_ustr
                                                  : u"failure-reported"_ustr,
                                  u"reviews"_ustr, pSelected->ObjectId, pSelected->ObjectId,
                                  pSelected->EvidenceId, pSelected->HashReference,
                                  aResult.Target);
    if (aResult.Success)
        SyncReviewState(pSelected->ObjectId, u"open"_ustr, u"open"_ustr, u"diff-review"_ustr,
                        pSelected->EvidenceId, pSelected->HashReference, aResult.Target,
                        aResult.PreviewMode);
    SaveReviewSessionSnapshot(pSelected->ObjectId, pSelected->ObjectId,
                              pSelected->EvidenceId, aResult.PreviewMode, pSelected->State,
                              aResult.Success ? OUString() : aResult.Message,
                              pSelected->HashReference);
}

IMPL_LINK_NOARG(AIChatPanel, OnReviewArtifactClicked, weld::Button&, void)
{
    ReviewSelectedArtifact();
}

IMPL_LINK_NOARG(AIChatPanel, OnFormatArtifactClicked, weld::Button&, void)
{
    ReviewSelectedFormatting();
}

IMPL_LINK_NOARG(AIChatPanel, OnInspectEvidenceClicked, weld::Button&, void)
{
    InspectSelectedEvidence();
}

IMPL_LINK_NOARG(AIChatPanel, OnApproveSelectedClicked, weld::Button&, void)
{
    DispatchWorkspaceAction(u"approve-selected"_ustr);
    LoadArtifactNavigator();
}

IMPL_LINK_NOARG(AIChatPanel, OnRejectSelectedClicked, weld::Button&, void)
{
    DispatchWorkspaceAction(u"reject-selected"_ustr);
    LoadArtifactNavigator();
}

IMPL_LINK_NOARG(AIChatPanel, OnCopyReferenceClicked, weld::Button&, void)
{
    DispatchWorkspaceAction(u"copy-reference"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnExportEvidenceClicked, weld::Button&, void)
{
    InspectSelectedEvidence();
}

IMPL_LINK_NOARG(AIChatPanel, OnFilterWorkspaceClicked, weld::Button&, void)
{
    DispatchWorkspaceAction(u"filter"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnSortWorkspaceClicked, weld::Button&, void)
{
    DispatchWorkspaceAction(u"sort"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnRemoveArtifactClicked, weld::Button&, void)
{
    RemoveSelectedArtifact();
}

IMPL_LINK_NOARG(AIChatPanel, OnPromptActivated, weld::Entry&, bool)
{
    SubmitPrompt();
    return true;
}

IMPL_LINK_NOARG(AIChatPanel, OnSendClicked, weld::Button&, void)
{
    SubmitPrompt();
}

IMPL_LINK_NOARG(AIChatPanel, OnCancelClicked, weld::Button&, void)
{
    if (m_eState != AIChatPanelState::Requesting && m_eState != AIChatPanelState::Streaming)
        return;

    DispatchWorkspaceAction(u"cancel"_ustr);
    AppendTerminalEvidence(u"cancelled"_ustr, OUString());
    SetState(AIChatPanelState::Cancelled);
    FocusPrompt();
}

IMPL_LINK_NOARG(AIChatPanel, OnRetryClicked, weld::Button&, void)
{
    if (m_sLastPrompt.isEmpty())
        return;

    DispatchWorkspaceAction(u"retry"_ustr);
    m_xPromptEntry->set_text(m_sLastPrompt);
    SetState(AIChatPanelState::Idle);
    FocusPrompt();
}

IMPL_LINK_NOARG(AIChatPanel, OnClearHistoryClicked, weld::Button&, void)
{
    ClearDocumentHistory();
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
