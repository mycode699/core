/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1: In-app AI chat).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatPanel.hxx"

#include <vcl/event.hxx>
#include <vcl/weld/ComboBox.hxx>
#include <vcl/weld/weld.hxx>

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
#include "AIChatSlashCommands.hxx"
#include "AIChatWorkspaceActionBarStore.hxx"
#include "AIChatWorkspaceSessionStore.hxx"

#include <AgentChatDiffApplier.hxx>
#include <AgentChatDiffExtractor.hxx>
#include <AgentChatSelectionCapture.hxx>
#include <AgentStepRunner.hxx>
#include <DocumentAIApply.hxx>
#include <DocumentAIContext.hxx>
#include <DocumentAILocalRag.hxx>
#include <DocumentAIScenarioStore.hxx>
#include <DocumentAIScenarios.hxx>
#include <DocumentAIVoiceInput.hxx>
#include <DocumentAIInputPrefs.hxx>
#include <DocumentAIScreenCapture.hxx>
#include <WorkTelemetryStore.hxx>
#include <LocalNotebookStore.hxx>

#include <osl/file.hxx>
#include <rtl/string.hxx>
#include <cstdlib>
#include <string_view>
#include <vector>
#include <EvidenceRecorder.hxx>
#include <ModelRoles.hxx>
#include <ModelRoutingConfig.hxx>

#include <AICanvasIntegration.hxx>
#include <AICanvasMode.hxx>
#include <AICanvasUI.hxx>

#include <AIFileManager.hxx>
#include <AIFileSearchUI.hxx>

#include <AICanvasEntryPoint.hxx>

#include <com/sun/star/ai/ProviderRequest.hpp>
#include <com/sun/star/ai/XProvider.hpp>
#include <com/sun/star/lang/IllegalArgumentException.hpp>
#include <com/sun/star/uno/Exception.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <comphelper/processfactory.hxx>
#include <osl/security.hxx>
#include <rtl/ustrbuf.hxx>

#if defined(MACOSX) || defined(LINUX) || defined(FREEBSD) || defined(NETBSD) || defined(OPENBSD) \
    || defined(DRAGONFLY)
#include <dlfcn.h>
#define AICHAT_HAVE_DLSYM 1
#endif
#include <sfx2/sfxsids.hrc>
#include <sfx2/viewfrm.hxx>
#include <sfx2/dispatch.hxx>
#include <svl/itemset.hxx>
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
    , m_xAgentStepBar(m_xBuilder->weld_label(u"agent_step_bar"_ustr))
    , m_xTranscriptView(m_xBuilder->weld_text_view(u"transcript_view"_ustr))
    , m_xPromptEntry(m_xBuilder->weld_entry(u"prompt_entry"_ustr))
    , m_xVoiceButton(m_xBuilder->weld_button(u"voice_button"_ustr))
    , m_xScreenshotButton(m_xBuilder->weld_button(u"screenshot_button"_ustr))
    , m_xScreenshotWinButton(m_xBuilder->weld_button(u"screenshot_win_button"_ustr))
    , m_xScreenshotFullButton(m_xBuilder->weld_button(u"screenshot_full_button"_ustr))
    , m_xCtxWorkbenchButton(m_xBuilder->weld_button(u"ctx_workbench_btn"_ustr))
    , m_xCtxNotebookButton(m_xBuilder->weld_button(u"ctx_notebook_btn"_ustr))
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
    , m_xAiSettingsButton(m_xBuilder->weld_button(u"ai_settings_button"_ustr))
    , m_xScenarioPicker(m_xBuilder->weld_combo_box(u"scenario_picker"_ustr))
    , m_xRunScenarioBtn(m_xBuilder->weld_button(u"run_scenario_btn"_ustr))
    , m_xRefreshScenariosBtn(m_xBuilder->weld_button(u"refresh_scenarios_btn"_ustr))
    , m_xScenarioSurfaceLabel(m_xBuilder->weld_label(u"scenario_surface_label"_ustr))
    , m_xOptFollowDoc(m_xBuilder->weld_check_button(u"opt_follow_doc"_ustr))
    , m_xTabWriter(m_xBuilder->weld_radio_button(u"tab_writer"_ustr))
    , m_xTabCalc(m_xBuilder->weld_radio_button(u"tab_calc"_ustr))
    , m_xTabImpress(m_xBuilder->weld_radio_button(u"tab_impress"_ustr))
    , m_xTabGeneral(m_xBuilder->weld_radio_button(u"tab_general"_ustr))
    , m_xOptAttachSelection(m_xBuilder->weld_check_button(u"opt_attach_selection"_ustr))
    , m_xOptAgentPipeline(m_xBuilder->weld_check_button(u"opt_agent_pipeline"_ustr))
    , m_xOptDocContext(m_xBuilder->weld_check_button(u"opt_doc_context"_ustr))
    , m_xScenarioPinnedLabel(m_xBuilder->weld_label(u"scenario_pinned_label"_ustr))
    , m_xSelectionChipBtn(m_xBuilder->weld_button(u"selection_chip_btn"_ustr))
    , m_xPendingPlanChip(m_xBuilder->weld_label(u"pending_plan_chip"_ustr))
    , m_xRoutingDiagBtn(m_xBuilder->weld_button(u"routing_diag_btn"_ustr))
    , m_xRoutingDiagLabel(m_xBuilder->weld_label(u"routing_diag_label"_ustr))
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
    , m_aInjectPoll("AIChatInjectPoll")
{
    for (sal_Int32 i = 0; i < kScenarioGridSlots; ++i)
    {
        const OUString id = u"scenario_btn_"_ustr + OUString::number(i);
        m_xScenarioGridBtns[static_cast<size_t>(i)] = m_xBuilder->weld_button(id);
        m_aScenarioGridIds[static_cast<size_t>(i)].clear();
        if (m_xScenarioGridBtns[static_cast<size_t>(i)])
            m_xScenarioGridBtns[static_cast<size_t>(i)]->connect_clicked(
                LINK(this, AIChatPanel, OnScenarioGridClicked));
    }
    for (sal_Int32 i = 0; i < kScenarioPinSlots; ++i)
    {
        const OUString id = u"scenario_pin_"_ustr + OUString::number(i);
        m_xScenarioPinBtns[static_cast<size_t>(i)] = m_xBuilder->weld_button(id);
        m_aScenarioPinIds[static_cast<size_t>(i)].clear();
        if (m_xScenarioPinBtns[static_cast<size_t>(i)])
            m_xScenarioPinBtns[static_cast<size_t>(i)]->connect_clicked(
                LINK(this, AIChatPanel, OnScenarioPinClicked));
    }

    m_xTranscriptView->set_editable(false);
    m_xArtifactTree->set_selection_mode(SelectionMode::Single);
    m_xPromptEntry->set_placeholder_text(u"点方案按钮执行，或输入消息…"_ustr);
    m_xPromptEntry->connect_insert_text(LINK(this, AIChatPanel, OnPromptInsertText));

    m_xPromptEntry->connect_changed(LINK(this, AIChatPanel, OnPromptChanged));
    m_xPromptEntry->connect_activate(LINK(this, AIChatPanel, OnPromptActivated));
    if (m_xVoiceButton)
    {
        m_xVoiceButton->set_tooltip_text(kqoffice::ai::chat::DocumentAIVoiceInput::statusHint());
        m_xVoiceButton->connect_clicked(LINK(this, AIChatPanel, OnVoiceClicked));
        // WeChat-style hold-to-talk: press start, release end
        m_xVoiceButton->connect_mouse_press(LINK(this, AIChatPanel, OnVoiceMousePress));
        m_xVoiceButton->connect_mouse_release(LINK(this, AIChatPanel, OnVoiceMouseRelease));
    }
    if (m_xScreenshotButton)
        m_xScreenshotButton->connect_clicked(LINK(this, AIChatPanel, OnScreenshotClicked));
    if (m_xScreenshotWinButton)
        m_xScreenshotWinButton->connect_clicked(LINK(this, AIChatPanel, OnScreenshotWinClicked));
    if (m_xScreenshotFullButton)
        m_xScreenshotFullButton->connect_clicked(LINK(this, AIChatPanel, OnScreenshotFullClicked));
    if (m_xCtxWorkbenchButton)
        m_xCtxWorkbenchButton->connect_clicked(LINK(this, AIChatPanel, OnCtxWorkbenchClicked));
    if (m_xCtxNotebookButton)
        m_xCtxNotebookButton->connect_clicked(LINK(this, AIChatPanel, OnCtxNotebookClicked));
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
    if (m_xAiSettingsButton)
        m_xAiSettingsButton->connect_clicked(LINK(this, AIChatPanel, OnAiSettingsClicked));
    if (m_xRunScenarioBtn)
        m_xRunScenarioBtn->connect_clicked(LINK(this, AIChatPanel, OnRunScenarioClicked));
    if (m_xRefreshScenariosBtn)
        m_xRefreshScenariosBtn->connect_clicked(LINK(this, AIChatPanel, OnRefreshScenariosClicked));
    if (m_xScenarioPicker)
        m_xScenarioPicker->connect_changed(LINK(this, AIChatPanel, OnScenarioPickerChanged));
    if (m_xOptFollowDoc)
        m_xOptFollowDoc->connect_toggled(LINK(this, AIChatPanel, OnFollowDocToggled));
    if (m_xTabWriter)
        m_xTabWriter->connect_toggled(LINK(this, AIChatPanel, OnCategoryTabToggled));
    if (m_xTabCalc)
        m_xTabCalc->connect_toggled(LINK(this, AIChatPanel, OnCategoryTabToggled));
    if (m_xTabImpress)
        m_xTabImpress->connect_toggled(LINK(this, AIChatPanel, OnCategoryTabToggled));
    if (m_xTabGeneral)
        m_xTabGeneral->connect_toggled(LINK(this, AIChatPanel, OnCategoryTabToggled));
    if (m_xSelectionChipBtn)
        m_xSelectionChipBtn->connect_clicked(LINK(this, AIChatPanel, OnSelectionChipClicked));
    if (m_xRoutingDiagBtn)
        m_xRoutingDiagBtn->connect_clicked(LINK(this, AIChatPanel, OnRoutingDiagClicked));

    LoadDocumentHistory();
    LoadArtifactNavigator();
    LoadSessionSnapshot();
    ReloadScenarioPicker();
    UpdateSelectionChip();
    UpdatePendingPlanChip();
    // Background light/review slot health (short Ollama probe; fail-closed offline).
    RunRoutingDiagnostics(/*bAppendTranscript*/ false);
    ConsumePendingScenarioRun();
    ConsumePendingPromptInject();
    // Keep consuming injects while panel is alive (速览→AI / 记事本→AI when already open).
    // 500ms keeps "AI already open → inject" snappy for hand tests.
    m_aInjectPoll.SetTimeout(500);
    m_aInjectPoll.SetInvokeHandler(LINK(this, AIChatPanel, OnInjectPollTick));
    m_aInjectPoll.Start();
    SetState(AIChatPanelState::Idle);
    UpdateActions();
    FocusPrompt();
}

AIChatPanel::~AIChatPanel() { m_aInjectPoll.Stop(); }

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
    // Pending chat ApplyPlan can be approved/rejected without a review-queue row.
    const bool bPendingPlanReady = m_bHasPendingPlan && !bBusy;
    m_xApproveSelectedButton->set_sensitive(
        bPendingPlanReady
        || AIChatWorkspaceActionBarStore::IsCommandEnabled(u"approve-selected"_ustr, pSelected,
                                                            bBusy, bHasRetryPrompt));
    m_xRejectSelectedButton->set_sensitive(
        bPendingPlanReady
        || AIChatWorkspaceActionBarStore::IsCommandEnabled(u"reject-selected"_ustr, pSelected,
                                                            bBusy, bHasRetryPrompt));
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
    if (eState == AIChatPanelState::Idle || eState == AIChatPanelState::Applied
        || eState == AIChatPanelState::Failed || eState == AIChatPanelState::Cancelled)
    {
        if (eState == AIChatPanelState::Idle)
            SetAgentStepBar(u"步骤：待命"_ustr);
    }
    if (m_xPromptEntry && m_xPromptEntry->get_text().indexOf('@') >= 0)
        UpdateContextMentions();
    UpdateActions();
}

void AIChatPanel::SetAgentStepBar(const OUString& rLabel)
{
    if (m_xAgentStepBar)
        m_xAgentStepBar->set_label(rLabel);
}

void AIChatPanel::UpdateAgentStepBar(sal_Int32 nActiveStep, const OUString& rDetail)
{
    // 0=plan 1=act 2=review — filled ● / pending ○ / done ✓
    auto mark = [&](sal_Int32 i) -> OUString {
        if (i < nActiveStep)
            return u"✓"_ustr;
        if (i == nActiveStep)
            return u"●"_ustr;
        return u"○"_ustr;
    };
    OUStringBuffer b;
    b.append(u"步骤："_ustr);
    b.append(mark(0));
    b.append(u" 规划 → "_ustr);
    b.append(mark(1));
    b.append(u" 执行 → "_ustr);
    b.append(mark(2));
    b.append(u" 审查"_ustr);
    if (!rDetail.isEmpty())
    {
        b.append(u" · "_ustr);
        b.append(rDetail);
    }
    SetAgentStepBar(b.makeStringAndClear());
}

css::ai::ProviderResponse AIChatPanel::CallProvider(const OUString& rPrompt,
                                                    const OUString& rCapabilityOverride)
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

        // Document AI Fabric: always bind active Writer/Calc/Impress selection.
        const kqoffice::ai::chat::DocumentAIBinding aBind
            = kqoffice::ai::chat::DocumentAIContext::bindUserInput(rPrompt);
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(aBind.statusLabel);

        css::ai::ProviderRequest aRequest;
        // Capability: scenario force → explicit override → prompt heuristics.
        // Five-slot: chat/rewrite→primary, summarize/extract→light,
        // plan→plan, review→review, agent→agent.
        OUString cap = rCapabilityOverride;
        if (cap.isEmpty() && !m_sForcedCapability.isEmpty())
        {
            cap = m_sForcedCapability;
            m_sForcedCapability.clear();
        }
        if (cap.isEmpty())
        {
            const OUString lower = rPrompt.toAsciiLowerCase();
            if (lower.indexOf(u"总结"_ustr) >= 0 || lower.indexOf(u"摘要"_ustr) >= 0
                || lower.indexOf(u"summar"_ustr) >= 0)
                cap = u"summarize"_ustr;
            else if (lower.indexOf(u"规划"_ustr) >= 0 || lower.indexOf(u"计划"_ustr) >= 0
                     || lower.indexOf(u"plan"_ustr) >= 0 || lower.startsWith(u"/plan"_ustr))
                cap = u"plan"_ustr;
            else if (lower.indexOf(u"审查"_ustr) >= 0 || lower.indexOf(u"评审"_ustr) >= 0
                     || lower.indexOf(u"校对"_ustr) >= 0 || lower.indexOf(u"review"_ustr) >= 0
                     || lower.startsWith(u"/review"_ustr) || lower.startsWith(u"/校对"_ustr)
                     || lower.startsWith(u"/清单审查"_ustr))
                cap = u"review"_ustr;
            else if (lower.indexOf(u"子代理"_ustr) >= 0 || lower.indexOf(u"协作"_ustr) >= 0
                     || lower.indexOf(u"agent"_ustr) >= 0 || lower.startsWith(u"/agent"_ustr)
                     || lower.indexOf(u"cowork"_ustr) >= 0 || lower.indexOf(u"多步"_ustr) >= 0)
                cap = u"agent"_ustr;
            else if (lower.indexOf(u"改写"_ustr) >= 0 || lower.indexOf(u"润色"_ustr) >= 0
                     || lower.indexOf(u"rewrite"_ustr) >= 0)
                cap = u"rewrite"_ustr;
            else
                cap = u"chat"_ustr;
        }
        else
            cap = kqoffice::ai::normalizeCapabilityHint(cap);

        aRequest.capability = cap;
        // Enriched prompt carries surface system instruction + selection + user query.
        OUString sPromptBody = aBind.enrichedPrompt.isEmpty() ? rPrompt : aBind.enrichedPrompt;

        // Multi-turn document session: inject recent user/assistant turns so
        // follow-ups like「再短一点」bind to the same document dialogue.
        if (m_xHistoryStore)
        {
            const OUString recent = m_xHistoryStore->FormatRecentTurns(/*nMaxTurns*/ 6,
                                                                       /*nMaxChars*/ 2800);
            if (!recent.isEmpty())
            {
                OUStringBuffer multi;
                multi.append(u"【同一文档近期对话 — 请承接上文意图改当前文档，勿重置话题】\n"_ustr);
                multi.append(recent);
                multi.append(u"\n\n【当前用户请求】\n"_ustr);
                multi.append(sPromptBody);
                sPromptBody = multi.makeStringAndClear();
                // Soft bias: short iterative edits → rewrite slot
                if (cap == u"chat"_ustr)
                {
                    const OUString low = rPrompt.toAsciiLowerCase();
                    if (low.indexOf(u"再"_ustr) >= 0 || low.indexOf(u"更"_ustr) >= 0
                        || low.indexOf(u"改成"_ustr) >= 0 || low.indexOf(u"缩短"_ustr) >= 0
                        || low.indexOf(u"短一点"_ustr) >= 0 || low.indexOf(u"长一点"_ustr) >= 0
                        || low.indexOf(u"again"_ustr) >= 0 || low.indexOf(u"shorter"_ustr) >= 0
                        || low.indexOf(u"longer"_ustr) >= 0 || low.indexOf(u"rewrite"_ustr) >= 0)
                        cap = u"rewrite"_ustr;
                }
            }
        }

        // Local document RAG: inject keyword-ranked chunks from the open document.
        const bool bWantRag
            = kqoffice::ai::chat::DocumentAILocalRag::wantsDocumentRag(rPrompt)
              || cap == u"knowledge-query"_ustr
              || (m_xOptDocContext && m_xOptDocContext->get_active()
                  && (rPrompt.indexOf(u"问本文档"_ustr) >= 0
                      || rPrompt.startsWith(u"/问"_ustr)));
        if (bWantRag)
        {
            const OUString rag
                = kqoffice::ai::chat::DocumentAILocalRag::buildContextBlock(rPrompt, 6, 4500);
            if (!rag.isEmpty())
            {
                OUStringBuffer withRag;
                withRag.append(rag);
                withRag.append(u"\n【用户问题】\n"_ustr);
                withRag.append(sPromptBody);
                sPromptBody = withRag.makeStringAndClear();
                AppendTranscript(u"System"_ustr, u"local-rag injected hits from open document"_ustr);
                if (m_xStatusLabel)
                    m_xStatusLabel->set_label(u"本地文档检索已附带 · 无外传"_ustr);
            }
            else if (m_xStatusLabel)
            {
                m_xStatusLabel->set_label(u"本地检索：当前文档无可抽取文本"_ustr);
            }
        }

        aRequest.capability = cap;
        aRequest.prompt = sPromptBody;
        aRequest.context = aBind.providerContext;
        aRequest.timeoutMs = PROVIDER_TIMEOUT_MS;

        // Surface configured slot (no network probe on hot path).
        {
            kqoffice::ai::ensureDefaultModelRoutingTemplate();
            const auto routing = kqoffice::ai::loadModelRoutingSnapshot();
            const auto resolved
                = kqoffice::ai::resolveModelForCapability(cap, routing, {});
            AppendTranscript(u"System"_ustr,
                             u"route capability="_ustr + cap + u" slot="_ustr
                                 + resolved.slotName + u" model="_ustr
                                 + (resolved.model.isEmpty() ? u"?"_ustr : resolved.model)
                                 + u" role="_ustr + resolved.roleName);
        }

        const css::ai::ProviderResponse aRsp = xProvider->call(aRequest);
        // Workbench insights (local telemetry, non-fatal)
        try
        {
            OUString modelLabel;
            // best-effort from last route transcript not available; use capability
            kqoffice::ai::workbench::WorkTelemetryStore::recordAiCall(
                modelLabel.isEmpty() ? u"provider"_ustr : modelLabel, aRequest.capability,
                aRequest.prompt.getLength(), aRsp.content.getLength(), 0, aRsp.status);
        }
        catch (...)
        {
        }
        return aRsp;
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

OUString AIChatPanel::CurrentDocumentSurface() const
{
    const auto sel = kqoffice::ai::chat::AgentChatSelectionCapture::captureCurrent();
    return sel.surface;
}

OUString AIChatPanel::ActiveCategoryTab() const
{
    if (m_xTabCalc && m_xTabCalc->get_active())
        return u"calc"_ustr;
    if (m_xTabImpress && m_xTabImpress->get_active())
        return u"impress"_ustr;
    if (m_xTabGeneral && m_xTabGeneral->get_active())
        return u"general"_ustr;
    return u"writer"_ustr;
}

void AIChatPanel::SelectCategoryTab(const OUString& rCategory)
{
    const OUString c = rCategory.toAsciiLowerCase();
    if (c == u"calc"_ustr && m_xTabCalc)
        m_xTabCalc->set_active(true);
    else if (c == u"impress"_ustr && m_xTabImpress)
        m_xTabImpress->set_active(true);
    else if ((c == u"general"_ustr || c == u"any"_ustr || c == u"none"_ustr || c.isEmpty()
              || c == u"unknown"_ustr)
             && m_xTabGeneral)
        m_xTabGeneral->set_active(true);
    else if (m_xTabWriter)
        m_xTabWriter->set_active(true);
}

void AIChatPanel::ReloadScenarioPicker()
{
    const OUString surface = CurrentDocumentSurface();
    OUString surfaceZh = surface;
    if (surface == u"writer"_ustr)
        surfaceZh = u"文字"_ustr;
    else if (surface == u"calc"_ustr)
        surfaceZh = u"表格"_ustr;
    else if (surface == u"impress"_ustr)
        surfaceZh = u"演示"_ustr;
    else if (surface == u"none"_ustr || surface.isEmpty())
        surfaceZh = u"无文档"_ustr;
    if (m_xScenarioSurfaceLabel)
        m_xScenarioSurfaceLabel->set_label(u"当前："_ustr + surfaceZh);

    // Follow document → switch category tab to match surface (no re-entrancy)
    if (m_xOptFollowDoc && m_xOptFollowDoc->get_active())
    {
        OUString want = u"general"_ustr;
        if (surface == u"calc"_ustr || surface == u"impress"_ustr || surface == u"writer"_ustr)
            want = surface;
        if (ActiveCategoryTab() != want)
        {
            m_bSuppressCategoryReload = true;
            SelectCategoryTab(want);
            m_bSuppressCategoryReload = false;
        }
    }

    const OUString category = ActiveCategoryTab();
    const auto cat = kqoffice::ai::chat::DocumentAIScenarioStore::load();
    const auto buttons
        = kqoffice::ai::chat::DocumentAIScenarioStore::listExecutableButtonsForCategory(cat,
                                                                                        category);

    UpdateCategoryTabBadges(cat);
    ReloadPinnedStrip(cat);

    // Grid buttons: first kScenarioGridSlots for this tab (pinned already sorted first)
    for (sal_Int32 i = 0; i < kScenarioGridSlots; ++i)
    {
        auto& btn = m_xScenarioGridBtns[static_cast<size_t>(i)];
        m_aScenarioGridIds[static_cast<size_t>(i)].clear();
        if (!btn)
            continue;
        if (static_cast<size_t>(i) < buttons.size())
        {
            const auto& s = buttons[static_cast<size_t>(i)];
            m_aScenarioGridIds[static_cast<size_t>(i)] = s.id;
            OUString label = s.titleZh;
            if (s.options.pinned)
                label = u"📌"_ustr + label;
            btn->set_label(label);
            btn->set_tooltip_text(s.titleZh + u" · "_ustr + s.category + u"\n"_ustr
                                  + (s.slashCommand.isEmpty() ? OUString()
                                                              : s.slashCommand + u" · "_ustr)
                                  + (s.options.pinned ? u"常用 · "_ustr : OUString())
                                  + u"点击执行"_ustr);
            btn->set_visible(true);
            btn->set_sensitive(true);
        }
        else
        {
            btn->set_label(u" "_ustr);
            btn->set_visible(false);
        }
    }

    // Overflow / full list in combo for this tab
    if (m_xScenarioPicker)
    {
        m_xScenarioPicker->clear();
        for (const auto& s : buttons)
        {
            OUString label = s.titleZh;
            if (s.options.pinned)
                label = u"📌 "_ustr + label;
            if (!s.category.isEmpty())
                label += u" · "_ustr + s.category;
            m_xScenarioPicker->append(s.id, label);
        }
        if (m_xScenarioPicker->get_count() > 0)
        {
            m_xScenarioPicker->set_active(0);
            OnScenarioPickerChanged(*m_xScenarioPicker);
        }
    }

    OUString tabZh = u"写作"_ustr;
    if (category == u"calc"_ustr)
        tabZh = u"表格"_ustr;
    else if (category == u"impress"_ustr)
        tabZh = u"演示"_ustr;
    else if (category == u"general"_ustr)
        tabZh = u"通用"_ustr;

    const auto pinned
        = kqoffice::ai::chat::DocumentAIScenarioStore::listPinnedButtons(cat);
    OUStringBuffer status;
    status.append(u"["_ustr);
    status.append(tabZh);
    status.append(u"] 按钮 "_ustr);
    status.append(static_cast<sal_Int32>(
        std::min(static_cast<size_t>(kScenarioGridSlots), buttons.size())));
    status.append(u"/"_ustr);
    status.append(static_cast<sal_Int32>(buttons.size()));
    status.append(u" · 常用 "_ustr);
    status.append(static_cast<sal_Int32>(pinned.size()));
    status.append(u" · 文档="_ustr);
    status.append(surfaceZh);
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(status.makeStringAndClear());

    UpdateSelectionChip();
    UpdatePendingPlanChip();
}

void AIChatPanel::UpdateSelectionChip()
{
    if (!m_xSelectionChipBtn)
        return;
    const kqoffice::ai::chat::SelectionContext sel
        = kqoffice::ai::chat::AgentChatSelectionCapture::captureCurrent();

    OUString surfaceZh = sel.surface;
    if (sel.surface == u"writer"_ustr)
        surfaceZh = u"文字"_ustr;
    else if (sel.surface == u"calc"_ustr)
        surfaceZh = u"表格"_ustr;
    else if (sel.surface == u"impress"_ustr)
        surfaceZh = u"演示"_ustr;
    else if (sel.surface == u"none"_ustr || sel.surface.isEmpty())
        surfaceZh = u"无文档"_ustr;

    OUStringBuffer label;
    label.append(u"选区 · "_ustr);
    label.append(surfaceZh);
    if (!sel.position.isEmpty())
    {
        label.append(u" · "_ustr);
        label.append(sel.position);
    }
    label.append(u" · "_ustr);
    label.append(sel.length);
    label.append(u" 字"_ustr);
    if (sel.length == 0)
        label.append(u"（空）"_ustr);

    m_xSelectionChipBtn->set_label(label.makeStringAndClear());

    OUStringBuffer tip;
    tip.append(u"当前表面："_ustr);
    tip.append(surfaceZh);
    tip.append(u"\n位置："_ustr);
    tip.append(sel.position.isEmpty() ? u"（无）"_ustr : sel.position);
    tip.append(u"\n长度："_ustr);
    tip.append(sel.length);
    tip.append(u" 字\n"_ustr);
    if (!sel.text.isEmpty())
    {
        OUString preview = sel.text;
        if (preview.getLength() > 120)
            preview = preview.copy(0, 120) + u"…"_ustr;
        tip.append(u"预览：\n"_ustr);
        tip.append(preview);
    }
    else
        tip.append(u"无选中内容 — 方案写回可能落到默认目标"_ustr);
    tip.append(u"\n\n快捷键：Ctrl/Cmd+Shift+F6 或 Ctrl+Alt+J"_ustr);
    m_xSelectionChipBtn->set_tooltip_text(tip.makeStringAndClear());
}

void AIChatPanel::UpdatePendingPlanChip()
{
    if (!m_xPendingPlanChip)
        return;
    if (!m_bHasPendingPlan)
    {
        m_xPendingPlanChip->set_label(u"计划：无"_ustr);
        return;
    }
    OUStringBuffer b;
    if (m_aPendingPlan.planId == u"ap-chart-insert"_ustr)
        b.append(u"计划：待批图表 "_ustr);
    else
        b.append(u"计划：待批 "_ustr);
    b.append(static_cast<sal_Int32>(m_aPendingPlan.operations.size()));
    b.append(u" 步"_ustr);
    if (!m_aPendingPlan.planId.isEmpty())
    {
        b.append(u" · "_ustr);
        b.append(m_aPendingPlan.planId);
    }
    m_xPendingPlanChip->set_label(b.makeStringAndClear());
    if (m_aPendingPlan.planId == u"ap-chart-insert"_ustr)
        m_xPendingPlanChip->set_tooltip_text(
            u"批准后打开「插入图表」向导（基于当前选区），需显式批准"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnSelectionChipClicked, weld::Button&, void)
{
    UpdateSelectionChip();
    ReloadScenarioPicker();
    m_xStatusLabel->set_label(u"已刷新选区芯片"_ustr);
}

void AIChatPanel::RunRoutingDiagnostics(bool bAppendTranscript)
{
    const kqoffice::ai::ModelRoutingDiagnostics d = kqoffice::ai::diagnoseModelRouting();
    if (m_xRoutingDiagLabel)
    {
        OUString shortLabel;
        if (!d.ollamaReachable)
            shortLabel = u"路由：Ollama 离线"_ustr;
        else
        {
            shortLabel = u"路由：轻量="_ustr
                         + (d.lightResolved.isEmpty() ? u"?"_ustr : d.lightResolved)
                         + u" 审查="_ustr
                         + (d.reviewResolved.isEmpty() ? u"?"_ustr : d.reviewResolved);
        }
        m_xRoutingDiagLabel->set_label(shortLabel);
        m_xRoutingDiagLabel->set_tooltip_text(d.summaryZh);
    }
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(d.summaryZh.replaceAll(u"\n"_ustr, u" · "_ustr));

    // Audit trail: write evidence JSON (local-first, never throws).
    kqoffice::ai::EvidenceRecord rec;
    rec.serviceMode = u"offline"_ustr;
    rec.provider = u"routing-diag light="_ustr
                   + (d.lightResolved.isEmpty() ? u"?"_ustr : d.lightResolved)
                   + u" review="_ustr
                   + (d.reviewResolved.isEmpty() ? u"?"_ustr : d.reviewResolved)
                   + u" primary="_ustr
                   + (d.primaryResolved.isEmpty() ? u"?"_ustr : d.primaryResolved);
    rec.capability = u"background"_ustr;
    rec.status = d.ollamaReachable
                     ? (d.lightReady && d.reviewReady ? u"ok"_ustr : u"degraded"_ustr)
                     : u"provider-error"_ustr;
    rec.requestSizeBytes = 0;
    rec.responseSizeBytes = d.summaryZh.getLength();
    rec.durationMs = 0;
    kqoffice::ai::EvidenceRecorder recorder;
    const OUString evId = recorder.record(rec);

    if (bAppendTranscript)
    {
        OUString msg = d.summaryZh;
        if (!evId.isEmpty())
            msg += u"\nevidence="_ustr + evId;
        AppendTranscript(u"System"_ustr, msg);
    }
    else if (!evId.isEmpty() && m_xStatusLabel)
    {
        // Quiet path: keep chip short; evidence id only in tooltip.
        if (m_xRoutingDiagLabel)
            m_xRoutingDiagLabel->set_tooltip_text(d.summaryZh + u"\nevidence="_ustr + evId);
    }
}

void AIChatPanel::ConsumePendingScenarioRun()
{
    const OUString id
        = kqoffice::ai::chat::DocumentAIScenarioStore::takePendingRun();
    if (id.isEmpty())
        return;
    AppendTranscript(u"System"_ustr,
                     u"pending-scenario-run id="_ustr + id
                         + u" source=queue-or-env auto-exec=true"_ustr);
    m_xStatusLabel->set_label(u"自动执行排队方案："_ustr + id);
    RunScenarioById(id);
}

IMPL_LINK_NOARG(AIChatPanel, OnRoutingDiagClicked, weld::Button&, void)
{
    RunRoutingDiagnostics(/*bAppendTranscript*/ true);
    ConsumePendingScenarioRun();
}

void AIChatPanel::UpdateCategoryTabBadges(const kqoffice::ai::chat::ScenarioCatalog& rCatalog)
{
    auto setBadge = [&](weld::RadioButton* pTab, const OUString& base, const OUString& catKey) {
        if (!pTab)
            return;
        const sal_Int32 n
            = kqoffice::ai::chat::DocumentAIScenarioStore::countExecutableButtonsForCategory(
                rCatalog, catKey);
        pTab->set_label(base + u" "_ustr + OUString::number(n));
    };
    setBadge(m_xTabWriter.get(), u"写作"_ustr, u"writer"_ustr);
    setBadge(m_xTabCalc.get(), u"表格"_ustr, u"calc"_ustr);
    setBadge(m_xTabImpress.get(), u"演示"_ustr, u"impress"_ustr);
    setBadge(m_xTabGeneral.get(), u"通用"_ustr, u"general"_ustr);
}

void AIChatPanel::ReloadPinnedStrip(const kqoffice::ai::chat::ScenarioCatalog& rCatalog)
{
    const auto pinned
        = kqoffice::ai::chat::DocumentAIScenarioStore::listPinnedButtons(rCatalog);
    if (m_xScenarioPinnedLabel)
    {
        m_xScenarioPinnedLabel->set_label(u"常用 "_ustr
                                          + OUString::number(static_cast<sal_Int32>(pinned.size())));
        m_xScenarioPinnedLabel->set_visible(true);
    }
    for (sal_Int32 i = 0; i < kScenarioPinSlots; ++i)
    {
        auto& btn = m_xScenarioPinBtns[static_cast<size_t>(i)];
        m_aScenarioPinIds[static_cast<size_t>(i)].clear();
        if (!btn)
            continue;
        if (static_cast<size_t>(i) < pinned.size())
        {
            const auto& s = pinned[static_cast<size_t>(i)];
            m_aScenarioPinIds[static_cast<size_t>(i)] = s.id;
            btn->set_label(s.titleZh);
            btn->set_tooltip_text(u"常用 · "_ustr + s.titleZh + u" · "_ustr + s.category
                                  + (s.slashCommand.isEmpty()
                                         ? OUString()
                                         : u"\n"_ustr + s.slashCommand));
            btn->set_visible(true);
            btn->set_sensitive(true);
        }
        else
        {
            btn->set_label(u" "_ustr);
            btn->set_visible(false);
        }
    }
}

void AIChatPanel::RunScenarioById(const OUString& rScenarioId)
{
    const auto cat = kqoffice::ai::chat::DocumentAIScenarioStore::load();
    const kqoffice::ai::chat::DocumentAIScenario* p
        = kqoffice::ai::chat::DocumentAIScenarioStore::find(cat, rScenarioId);
    if (!p || p->id.isEmpty())
        return;

    kqoffice::ai::chat::DocumentAIScenario scen = *p;
    // Panel checkboxes override scenario options for this run.
    if (m_xOptAttachSelection)
        scen.options.attachSelection = m_xOptAttachSelection->get_active();
    if (m_xOptAgentPipeline)
        scen.options.useAgentPipeline = m_xOptAgentPipeline->get_active();
    if (m_xOptDocContext)
        scen.options.includeDocContext = m_xOptDocContext->get_active();

    const kqoffice::ai::chat::SelectionContext sel
        = kqoffice::ai::chat::AgentChatSelectionCapture::captureCurrent();
    const OUString selText = scen.options.attachSelection ? sel.text : OUString();
    const OUString expanded
        = kqoffice::ai::chat::DocumentAIScenarioStore::expandPrompt(scen, selText);

    // Prefix agent keyword so multi-step pipeline can trigger when requested.
    OUString toSend = expanded;
    if (scen.options.useAgentPipeline && toSend.indexOf(u"多步"_ustr) < 0
        && toSend.indexOf(u"agent"_ustr) < 0)
        toSend = u"/agent "_ustr + toSend;

    // Force provider capability from scenario hint (review → review 槽, etc.).
    m_sForcedCapability = kqoffice::ai::normalizeCapabilityHint(scen.capabilityHint);
    if (scen.options.useAgentPipeline)
        m_sForcedCapability = u"agent"_ustr;

    m_xPromptEntry->set_text(toSend);
    AppendTranscript(u"System"_ustr,
                     u"scenario="_ustr + scen.id + u" title="_ustr + scen.titleZh
                         + u" button-exec=true attach-sel="_ustr
                         + (scen.options.attachSelection ? u"1"_ustr : u"0"_ustr)
                         + u" agent="_ustr
                         + (scen.options.useAgentPipeline ? u"1"_ustr : u"0"_ustr)
                         + u" capability="_ustr + m_sForcedCapability
                         + u" selection-len="_ustr
                         + OUString::number(selText.getLength()));

    if (scen.options.autoSubmit)
        SubmitPrompt();
    else
    {
        // Blank-draft / guided scenarios: prefill prompt and wait for topic + Send.
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"已填入方案「"_ustr + scen.titleZh
                                      + u"」— 可补充主题后发送"_ustr);
        FocusPrompt();
    }
}

void AIChatPanel::TryShowDiffReviewAfterApply(const OUString& rPlanId, const OUString& rEngine,
                                              bool bApplied)
{
    // Writer native engine already surfaces Diff Review via applyDiagnosticsPlan.
    if (rEngine == u"writer-apply-engine"_ustr)
        return;
#if AICHAT_HAVE_DLSYM
    using ShowFn = void (*)(void*, const sal_Unicode*, sal_Int32, const sal_Unicode*, sal_Int32,
                            const sal_Unicode*, sal_Int32, const sal_Unicode*, sal_Int32, sal_Bool);
    void* pSym = dlsym(RTLD_DEFAULT, "kqoffice_show_diff_review");
    if (!pSym || !m_xPromptEntry)
        return;
    auto pFn = reinterpret_cast<ShowFn>(pSym);
    const OUString patchId = u"p1"_ustr;
    const OUString kind = u"replace"_ustr;
    const OUString status = bApplied ? u"ok"_ustr : u"pending"_ustr;
    pFn(m_xPromptEntry.get(), rPlanId.getStr(), rPlanId.getLength(), patchId.getStr(),
        patchId.getLength(), kind.getStr(), kind.getLength(), status.getStr(), status.getLength(),
        bApplied ? sal_True : sal_False);
#else
    (void)rPlanId;
    (void)bApplied;
#endif
}

void AIChatPanel::SubmitPrompt()
{
    OUString sPrompt = m_xPromptEntry->get_text().trim();
    if (sPrompt.isEmpty())
        return;

    // Expand scenario slash commands before send.
    if (kqoffice::ai::chat::DocumentAIScenarios::isScenarioSlash(sPrompt))
    {
        const kqoffice::ai::chat::DocumentAIScenario scen
            = kqoffice::ai::chat::DocumentAIScenarios::findBySlashOrId(sPrompt);
        // Allow trailing free text after slash command.
        OUString extra;
        if (sPrompt.getLength() > scen.slashCommand.getLength())
            extra = sPrompt.copy(scen.slashCommand.getLength()).trim();
        const kqoffice::ai::chat::SelectionContext sel
            = kqoffice::ai::chat::AgentChatSelectionCapture::captureCurrent();
        sPrompt = kqoffice::ai::chat::DocumentAIScenarios::expandPrompt(scen, sel.text);
        if (!extra.isEmpty())
            sPrompt += u"\n\n补充要求："_ustr + extra;
        m_xPromptEntry->set_text(sPrompt);
        // Slash path also binds review/light slots via capabilityHint.
        if (m_sForcedCapability.isEmpty())
            m_sForcedCapability = kqoffice::ai::normalizeCapabilityHint(scen.capabilityHint);
    }

    if (!ValidateContextMentions(sPrompt))
        return;

    // ── V6: File manager routing ────────────────────────────────────
    // /files [scan|list|sort:name|sort:time]  — scan & list files
    // /find <query>                           — AI semantic search
    // /nav [generate]                         — generate navigation index
    if (sPrompt.startsWith("/files"))
    {
        OUString arg = sPrompt.copy(6).trim();
        kqoffice::ai::filemgr::AIFileManager mgr;
        auto files = mgr.quickScan().files;

        kqoffice::ai::filemgr::SortOrder order = kqoffice::ai::filemgr::SortOrder::TimeDesc;
        if (arg.indexOf("sort:name") >= 0 || arg.indexOf("名称") >= 0)
            order = kqoffice::ai::filemgr::SortOrder::NameAsc;
        else if (arg.indexOf("sort:size") >= 0 || arg.indexOf("大小") >= 0)
            order = kqoffice::ai::filemgr::SortOrder::SizeDesc;

        OUString list = kqoffice::ai::filemgr::AIFileSearchUI::formatFileList(files, order);
        AppendTranscript(u"Files"_ustr,
            kqoffice::ai::filemgr::AIFileSearchUI::formatScanSummary(mgr.quickScan()));
        AppendAssistantMarkdown(list);
        m_xPromptEntry->set_text(OUString());
        return;
    }

    if (sPrompt.startsWith("/find"))
    {
        OUString query = sPrompt.copy(5).trim();
        kqoffice::ai::filemgr::AIFileManager mgr;
        auto results = mgr.quickSearch(query.isEmpty() ? u"文档"_ustr : query);

        if (results.empty())
            AppendTranscript(u"Files"_ustr, u"未找到匹配 \""_ustr + query + u"\" 的文件"_ustr);
        else
            AppendAssistantMarkdown(kqoffice::ai::filemgr::AIFileSearchUI::formatSearchResults(results));
        m_xPromptEntry->set_text(OUString());
        return;
    }

    if (sPrompt.startsWith("/nav"))
    {
        kqoffice::ai::filemgr::AIFileManager mgr;
        auto files = mgr.quickScan().files;
        OUString homeDir;
        osl::Security().getHomeDir(homeDir);
        OUString navPath = mgr.generateNavIndex(files, homeDir + u"/Desktop"_ustr);

        AppendTranscript(u"Files"_ustr,
            u"导航索引已生成!\n→ "_ustr + navPath
            + u"\n共 " + OUString::number(static_cast<sal_Int32>(files.size()))
            + u" 个文件"_ustr);
        m_xPromptEntry->set_text(OUString());
        return;
    }

    // ── V5: Canvas mode routing ──────────────────────────────────────
    // /canvas start <goals>   — start canvas mode for current doc type
    // /canvas confirm         — confirm current step
    // /canvas revise <notes>  — request revision
    // /canvas skip            — skip current step
    if (sPrompt.startsWith("/canvas"))
    {
        OUString arg = sPrompt.copy(7).trim();
        if (arg.startsWith("start") || arg.startsWith("开始"))
        {
            OUString goal = arg.copy(arg.indexOf(' ') >= 0 ? arg.indexOf(' ') + 1 : 0).trim();
            if (goal.isEmpty())
                goal = u"创建新文档"_ustr;

            auto docType = kqoffice::ai::canvas::CanvasDocType::Writer;
            // Detect document type from current active document
            OUString activeType; // could be inferred from SfxViewShell
            if (activeType == "calc")
                docType = kqoffice::ai::canvas::CanvasDocType::Calc;
            else if (activeType == "impress")
                docType = kqoffice::ai::canvas::CanvasDocType::Impress;

            kqoffice::ai::canvas::AICanvasIntegration::startCanvasViaChat(docType, goal);
            auto display = kqoffice::ai::canvas::AICanvasUI::buildDisplay(
                kqoffice::ai::canvas::AICanvasIntegration::getCurrentSession());

            AppendTranscript(u"Canvas"_ustr, display.progressBar);
            AppendTranscript(u"AI"_ustr,
                u"画布模式已启动: "_ustr + kqoffice::ai::canvas::AICanvasEntryPoint::getDescription(docType)
                + u"\n\n请描述第1步的需求:"_ustr);

            m_xPromptEntry->set_text(OUString());
            SetState(AIChatPanelState::Idle);
            return;
        }

        if (arg.startsWith("confirm") || arg.startsWith("确认"))
        {
            OUString response = kqoffice::ai::canvas::AICanvasIntegration::processConfirm();
            AppendTranscript(u"Canvas"_ustr, response);
            m_xPromptEntry->set_text(OUString());
            return;
        }

        if (arg.startsWith("revise") || arg.startsWith("修改"))
        {
            OUString feedback = arg.copy(arg.indexOf(' ') >= 0 ? arg.indexOf(' ') + 1 : 0).trim();
            if (feedback.isEmpty())
                feedback = u"请重新生成"_ustr;
            OUString response = kqoffice::ai::canvas::AICanvasIntegration::processRevise(feedback);
            AppendTranscript(u"Canvas"_ustr, response);
            m_xPromptEntry->set_text(OUString());
            return;
        }

        if (arg.startsWith("skip") || arg.startsWith("跳过"))
        {
            // Trigger skip via confirm variant
            OUString response = kqoffice::ai::canvas::AICanvasIntegration::processConfirm();
            AppendTranscript(u"Canvas"_ustr, u"⏭ 已跳过当前步骤\n"_ustr + response);
            m_xPromptEntry->set_text(OUString());
            return;
        }
    }

    // ── V5: Active canvas pipeline ───────────────────────────────────
    if (kqoffice::ai::canvas::AICanvasIntegration::isCanvasActive())
    {
        OUString response = kqoffice::ai::canvas::AICanvasIntegration::processCanvasMessage(sPrompt);
        auto display = kqoffice::ai::canvas::AICanvasUI::buildDisplay(
            kqoffice::ai::canvas::AICanvasIntegration::getCurrentSession());

        AppendTranscript(u"User"_ustr, sPrompt);
        AppendTranscript(u"Canvas"_ustr, display.progressBar);
        AppendAssistantMarkdown(response);

        m_xPromptEntry->set_text(OUString());
        m_sLastPrompt = sPrompt;
        SetState(AIChatPanelState::AwaitingApproval);
        return;
    }

    // ── Normal chat pipeline ─────────────────────────────────────────
    m_sLastPrompt = sPrompt;
    m_sStreamingBuffer.clear();
    SetState(AIChatPanelState::Requesting);
    AppendTranscript(u"User"_ustr, sPrompt);
    m_xPromptEntry->set_text(OUString());

    // Multi-step agent path: Plan → Act → Review (five-slot Provider routing).
    // Triggered by agent/cowork keywords (see CallProvider capability heuristics).
    const OUString lowerPrompt = sPrompt.toAsciiLowerCase();
    const bool bAgentPipeline
        = lowerPrompt.indexOf(u"子代理"_ustr) >= 0 || lowerPrompt.indexOf(u"协作"_ustr) >= 0
          || lowerPrompt.indexOf(u"agent"_ustr) >= 0 || lowerPrompt.startsWith(u"/agent"_ustr)
          || lowerPrompt.indexOf(u"cowork"_ustr) >= 0
          || lowerPrompt.indexOf(u"多步"_ustr) >= 0 || lowerPrompt.indexOf(u"plan-act"_ustr) >= 0;

    if (bAgentPipeline)
    {
        // Bind document selection so plan/act/review see the active surface.
        const kqoffice::ai::chat::DocumentAIBinding aBind
            = kqoffice::ai::chat::DocumentAIContext::bindUserInput(sPrompt);
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(aBind.statusLabel);
        AppendTranscript(u"System"_ustr,
                         u"agent-pipeline start plan→act→review "_ustr + aBind.statusLabel
                             + u" main-document-mutation=false"_ustr);
        // Visible step bar (Stage B): run steps with UI updates between them.
        UpdateAgentStepBar(0, u"规划中…"_ustr);
        const OUString sGoal
            = aBind.enrichedPrompt.isEmpty() ? sPrompt : aBind.enrichedPrompt;
        // Prefer stepwise UI; fall back to batch runner for content assembly.
        const kqoffice::ai::AgentPipelineResult pipe
            = kqoffice::ai::AgentStepRunner::runPlanActReview(sGoal, aBind.providerContext);
        // Reflect completed steps on the bar + transcript labels.
        for (size_t i = 0; i < pipe.steps.size(); ++i)
        {
            const auto& st = pipe.steps[i];
            const sal_Int32 stepIdx = (st.stepKind == u"plan"_ustr)   ? 0
                                      : (st.stepKind == u"act"_ustr)  ? 1
                                      : (st.stepKind == u"review"_ustr) ? 2
                                                                       : static_cast<sal_Int32>(i);
            UpdateAgentStepBar(stepIdx,
                               st.stepKind + u" "_ustr
                                   + (st.status == u"ok"_ustr ? u"完成"_ustr : st.status));
            OUString title = st.stepKind;
            if (title == u"plan"_ustr)
                title = u"规划"_ustr;
            else if (title == u"act"_ustr)
                title = u"执行"_ustr;
            else if (title == u"review"_ustr)
                title = u"审查"_ustr;
            OUString body = st.content;
            if (body.getLength() > 400)
                body = body.copy(0, 400) + u"…"_ustr;
            AppendTranscript(u"Agent·"_ustr + title,
                             (body.isEmpty() ? (u"status="_ustr + st.status) : body),
                             /*bPersistHistory*/ false);
        }
        SetState(AIChatPanelState::Streaming);
        if (!pipe.combinedContent.isEmpty())
            AppendAssistantMarkdown(pipe.combinedContent);
        else
            AppendAssistantChunk(u"(empty agent pipeline response)"_ustr);

        if (pipe.success)
        {
            UpdateAgentStepBar(3, u"完成 · 待批准写回"_ustr);
            SetAgentStepBar(u"步骤：✓ 规划 → ✓ 执行 → ✓ 审查 · 待批准写回（主文档未改）"_ustr);
            SetState(AIChatPanelState::AwaitingApproval);
            AppendTerminalEvidence(u"ok"_ustr, pipe.finalEvidenceId);
            AppendTranscript(
                u"System"_ustr,
                u"agent-pipeline done steps="_ustr
                    + OUString::number(static_cast<sal_Int32>(pipe.steps.size()))
                    + u" evidence="_ustr + pipe.finalEvidenceId
                    + u" awaiting-approval=true"_ustr);
            // Stage act output only — review is advisory; never auto-apply.
            StagePendingApplyPlan(pipe.applyCandidateContent, pipe.finalEvidenceId);
        }
        else
        {
            SetAgentStepBar(u"步骤：失败 · "_ustr + pipe.failureReason);
            ClearPendingPlan();
            SetState(AIChatPanelState::Failed);
            AppendTerminalEvidence(u"provider-error"_ustr, pipe.finalEvidenceId);
            AppendTranscript(u"System"_ustr,
                             u"agent-pipeline-failed reason="_ustr + pipe.failureReason
                                 + u" main-document-mutation=false"_ustr);
        }
        FocusPrompt();
        return;
    }

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
        // Stage only — never mutate the main document before explicit approval.
        StagePendingApplyPlan(aResponse.content, aResponse.evidenceId);
    }
    else
    {
        ClearPendingPlan();
        SetState(AIChatPanelState::Failed);
        AppendTerminalEvidence(aResponse.status, aResponse.evidenceId);
    }
    FocusPrompt();
}

void AIChatPanel::ClearPendingPlan()
{
    m_bHasPendingPlan = false;
    m_aPendingPlan = kqoffice::ai::chat::ApplyPlan{};
    m_sPendingEvidenceId.clear();
    UpdatePendingPlanChip();
}

void AIChatPanel::StagePendingApplyPlan(const OUString& rProviderContent,
                                        const OUString& rEvidenceId)
{
    ClearPendingPlan();
    kqoffice::ai::chat::ApplyPlan aPlan
        = kqoffice::ai::chat::AgentChatDiffExtractor::extract(rProviderContent);
    aPlan.rawOutput = rProviderContent;

    const kqoffice::ai::chat::SelectionContext sel
        = kqoffice::ai::chat::AgentChatSelectionCapture::captureCurrent();

    // Fill missing targets from live selection before validate.
    if (!sel.position.isEmpty())
    {
        for (auto& op : aPlan.operations)
        {
            if (op.target.isEmpty())
                op.target = sel.position;
            // range:A1:B2 → cell of top-left for formula write-back
            if (sel.surface == u"calc"_ustr && op.target.startsWith(u"range:"_ustr))
            {
                OUString rest = op.target.copy(6);
                const sal_Int32 colon = rest.indexOf(u':');
                if (colon > 0)
                    op.target = u"cell:"_ustr + rest.copy(0, colon);
            }
        }
    }
    if (aPlan.planId.isEmpty() && !aPlan.operations.empty())
        aPlan.planId = u"ap-chat-extracted"_ustr;

    // Calc: free-text formula(s) (=SUM…) → cell/range plan (approve-before-apply).
    if (!kqoffice::ai::chat::AgentChatDiffExtractor::validate(aPlan)
        && sel.surface == u"calc"_ustr)
    {
        auto formulas
            = kqoffice::ai::chat::AgentChatDiffExtractor::extractAllFormulas(rProviderContent);
        if (formulas.empty())
        {
            const OUString one
                = kqoffice::ai::chat::AgentChatDiffExtractor::extractLeadingFormula(
                    rProviderContent);
            if (!one.isEmpty())
                formulas.push_back(one);
        }
        if (!formulas.empty())
        {
            OUString pos = sel.position;
            if (pos.isEmpty())
                pos = u"cell:A1"_ustr;
            aPlan = kqoffice::ai::chat::AgentChatDiffExtractor::makeFormulaRangePlan(
                pos, formulas, sel.text);
            aPlan.rawOutput = rProviderContent;
        }
        // Chart intent (no formula, or explicit 图表 advice): stage chart wizard plan.
        else if (kqoffice::ai::chat::AgentChatDiffExtractor::looksLikeChartIntent(
                     rProviderContent)
                 || rProviderContent.indexOf(u"chart-assist"_ustr) >= 0)
        {
            OUString pos = sel.position;
            if (pos.isEmpty())
                pos = u"selection"_ustr;
            aPlan = kqoffice::ai::chat::AgentChatDiffExtractor::makeChartInsertPlan(
                pos, rProviderContent);
        }
    }

    // Also: chart intent even when JSON plan exists but is empty/invalid for calc
    if (!kqoffice::ai::chat::AgentChatDiffExtractor::validate(aPlan)
        && sel.surface == u"calc"_ustr
        && kqoffice::ai::chat::AgentChatDiffExtractor::looksLikeChartIntent(rProviderContent))
    {
        aPlan = kqoffice::ai::chat::AgentChatDiffExtractor::makeChartInsertPlan(
            sel.position.isEmpty() ? u"selection"_ustr : sel.position, rProviderContent);
    }

    // Impress: outline free text → multi-slide insert plan.
    if (!kqoffice::ai::chat::AgentChatDiffExtractor::validate(aPlan)
        && sel.surface == u"impress"_ustr)
    {
        auto outline
            = kqoffice::ai::chat::AgentChatDiffExtractor::extractOutlineSlidePlan(rProviderContent);
        if (kqoffice::ai::chat::AgentChatDiffExtractor::validate(outline))
            aPlan = std::move(outline);
    }

    // If LLM did not emit structured ops but user has a selection, stage a
    // single replace so approve can write back via DocumentAIApply.
    if (!kqoffice::ai::chat::AgentChatDiffExtractor::validate(aPlan))
    {
        if (!sel.text.isEmpty() && !rProviderContent.isEmpty()
            && (sel.surface == u"writer"_ustr || sel.surface == u"calc"_ustr
                || sel.surface == u"impress"_ustr))
        {
            kqoffice::ai::chat::DiffOperation op;
            op.opType = u"replace"_ustr;
            op.target = sel.position;
            if (op.target.isEmpty() && sel.surface == u"writer"_ustr)
                op.target = u"para:1"_ustr;
            if (op.target.isEmpty() && sel.surface == u"calc"_ustr)
                op.target = u"cell:A1"_ustr;
            if (sel.surface == u"calc"_ustr && op.target.startsWith(u"range:"_ustr))
            {
                OUString rest = op.target.copy(6);
                const sal_Int32 colon = rest.indexOf(u':');
                op.target = u"cell:"_ustr + (colon > 0 ? rest.copy(0, colon) : rest);
            }
            if (op.target.isEmpty() && sel.surface == u"impress"_ustr)
                op.target = u"slide:1"_ustr;
            op.oldText = sel.text;
            // Calc: prefer formula line over full prose when present
            if (sel.surface == u"calc"_ustr)
            {
                const OUString formula
                    = kqoffice::ai::chat::AgentChatDiffExtractor::extractLeadingFormula(
                        rProviderContent);
                op.newText = formula.isEmpty() ? rProviderContent.trim() : formula;
            }
            else
                op.newText = rProviderContent.trim();
            aPlan.planId = u"ap-selection-replace"_ustr;
            aPlan.operations.clear();
            aPlan.operations.push_back(op);
            aPlan.rawOutput = rProviderContent;
        }
    }

    if (!kqoffice::ai::chat::AgentChatDiffExtractor::validate(aPlan))
    {
        AppendTranscript(
            u"System"_ustr,
            u"plan-stage-skipped reason=no-valid-apply-plan main-document-mutation=false"_ustr);
        return;
    }

    m_aPendingPlan = std::move(aPlan);
    m_bHasPendingPlan = true;
    m_sPendingEvidenceId = rEvidenceId;
    const bool bWriterEngine
        = kqoffice::ai::chat::DocumentAIApply::hasWriterApplyEngineHook();
    AppendTranscript(u"System"_ustr,
                     u"plan-staged plan="_ustr + m_aPendingPlan.planId + u" ops="_ustr
                         + OUString::number(
                             static_cast<sal_Int32>(m_aPendingPlan.operations.size()))
                         + u" evidence="_ustr + m_sPendingEvidenceId
                         + u" writer-engine="_ustr
                         + (bWriterEngine ? u"ready"_ustr : u"fallback-uno"_ustr)
                         + u" awaiting-approval=true main-document-mutation=false "
                           "explicit-human-approval-required=true"_ustr);
    if (m_aPendingPlan.planId == u"ap-chart-insert"_ustr)
        m_xStatusLabel->set_label(u"图表计划已暂存 — 批准后打开插入图表向导: "_ustr
                                  + m_aPendingPlan.planId);
    else
        m_xStatusLabel->set_label(u"计划已暂存，批准后写入: "_ustr + m_aPendingPlan.planId);
    UpdatePendingPlanChip();
    UpdateSelectionChip();
    UpdateActions();
}

bool AIChatPanel::ApplyPendingPlanWithApproval()
{
    if (!m_bHasPendingPlan)
    {
        AppendTranscript(
            u"System"_ustr,
            u"plan-apply-failed reason=no-pending-plan main-document-mutation=false"_ustr);
        return false;
    }

    const OUString sPlanId = m_aPendingPlan.planId;
    const sal_Int32 nOps = static_cast<sal_Int32>(m_aPendingPlan.operations.size());
    const bool bChart = sPlanId == u"ap-chart-insert"_ustr
                        || (!m_aPendingPlan.operations.empty()
                            && m_aPendingPlan.operations.front().opType == u"chart_insert"_ustr);

    SetAgentStepBar(bChart ? u"步骤：批准写回 · 打开图表向导…"_ustr
                           : u"步骤：批准写回 · 应用中…"_ustr);

    // Document AI Fabric: Writer → native ApplyEngine (undo-grouped);
    // Calc/Impress → UNO DiffApplier; chart → InsertObjectChart dispatch.
    // Only after explicit human approval.
    const kqoffice::ai::chat::DocumentAIApplyResult aResult
        = kqoffice::ai::chat::DocumentAIApply::applyApprovedWithRawFallback(
            m_aPendingPlan, m_aPendingPlan.rawOutput);

    if (aResult.success)
    {
        const OUString sEvidenceId = m_sPendingEvidenceId;
        // Local audit trail for write-back / chart insert.
        kqoffice::ai::EvidenceRecord rec;
        rec.serviceMode = u"offline"_ustr;
        rec.provider = aResult.engine;
        rec.capability = bChart ? u"chart"_ustr : u"apply"_ustr;
        rec.status = u"ok"_ustr;
        rec.requestSizeBytes = static_cast<sal_Int32>(nOps);
        rec.responseSizeBytes = aResult.appliedCount;
        rec.durationMs = 0;
        kqoffice::ai::EvidenceRecorder recorder;
        const OUString auditId = recorder.record(rec);
        const OUString evid = !sEvidenceId.isEmpty() ? sEvidenceId : auditId;

        AppendTranscript(u"System"_ustr,
                         u"plan-applied plan="_ustr + sPlanId + u" ops="_ustr
                             + OUString::number(nOps) + u" applied="_ustr
                             + OUString::number(aResult.appliedCount) + u" engine="_ustr
                             + aResult.engine + u" surface="_ustr + aResult.surface
                             + u" evidence="_ustr + evid
                             + u" explicit-human-approval=true main-document-mutation=true"_ustr
                             + (bChart ? u" chart-wizard=opened"_ustr : OUString()));
        if (bChart)
        {
            SetAgentStepBar(u"步骤：✓ 已批准 · 图表向导已打开（可撤销）"_ustr);
            AppendTranscript(u"System"_ustr,
                             u"chart-insert: InsertObjectChart dispatched; complete wizard to "
                             u"place chart. Undo available for document changes."_ustr,
                             /*bPersistHistory*/ false);
        }
        else
        {
            SetAgentStepBar(u"步骤：✓ 已批准写回 · engine="_ustr + aResult.engine);
            // Writer ApplyEngine already opens Diff Review; for UNO path (Calc/Impress)
            // open the shared Diff Review dialog so write-back UX stays aligned.
            TryShowDiffReviewAfterApply(sPlanId, aResult.engine, true);
        }
        ClearPendingPlan();
        SetState(AIChatPanelState::Applied);
        m_xStatusLabel->set_label(u"已应用 ("_ustr + aResult.engine + u"): "_ustr + sPlanId);
        kqoffice::ai::workbench::WorkTelemetryStore::recordSimple(u"ai_apply"_ustr, 1);
        RecordWorkspaceActivity(u"action-invoked"_ustr, u"reviews"_ustr, sPlanId, evid,
                                OUString(), bChart ? u"chart-insert"_ustr : u"diff-review"_ustr);
        return true;
    }

    SetAgentStepBar(u"步骤：写回失败 · "_ustr + aResult.error);
    AppendTranscript(u"System"_ustr,
                     u"plan-apply-failed plan="_ustr + sPlanId + u" error="_ustr + aResult.error
                         + u" engine="_ustr + aResult.engine + u" surface="_ustr
                         + aResult.surface
                         + u" main-document-mutation=false explicit-human-approval=true"_ustr);
    m_xStatusLabel->set_label(u"应用失败: "_ustr + sPlanId);
    RecordWorkspaceActivity(u"failure-reported"_ustr, u"reviews"_ustr, sPlanId,
                            m_sPendingEvidenceId, OUString(), u"diff-review"_ustr);
    // Keep pending plan so the user can retry after fixing context.
    return false;
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
    // Prefer pending chat ApplyPlan write-back; fall back to review-queue metadata path.
    if (m_bHasPendingPlan)
        ApplyPendingPlanWithApproval();
    else
        DispatchWorkspaceAction(u"approve-selected"_ustr);
    LoadArtifactNavigator();
    UpdateActions();
}

IMPL_LINK_NOARG(AIChatPanel, OnRejectSelectedClicked, weld::Button&, void)
{
    if (m_bHasPendingPlan)
    {
        const OUString sPlanId = m_aPendingPlan.planId;
        AppendTranscript(
            u"System"_ustr,
            u"plan-rejected plan="_ustr + sPlanId
                + u" explicit-human-approval=true main-document-mutation=false"_ustr);
        ClearPendingPlan();
        SetState(AIChatPanelState::Idle);
        m_xStatusLabel->set_label(u"Plan rejected: "_ustr + sPlanId);
    }
    else
    {
        DispatchWorkspaceAction(u"reject-selected"_ustr);
    }
    LoadArtifactNavigator();
    UpdateActions();
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

IMPL_LINK_NOARG(AIChatPanel, OnAiSettingsClicked, weld::Button&, void)
{
    // Open the same Options tree used by the rest of the product.
    // Path: 工具 → 选项 → 语言设置 → 可圈 AI
    if (SfxViewFrame* pFrame = SfxViewFrame::Current())
    {
        if (SfxDispatcher* pDisp = pFrame->GetDispatcher())
        {
            pDisp->Execute(SID_OPTIONS_TREEDIALOG, SfxCallMode::ASYNCHRON);
            AppendTranscript(
                u"System"_ustr,
                u"已打开选项对话框。请选择：语言设置 → 可圈 AI，配置主模型 / 轻量 / 规划 / 审查等角色模型后点确定保存。"_ustr);
            m_xStatusLabel->set_label(u"选项 → 语言设置 → 可圈 AI"_ustr);
            return;
        }
    }
    AppendTranscript(
        u"System"_ustr,
        u"无法打开选项对话框。请手动：工具 → 选项 → 语言设置 → 可圈 AI"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnRunScenarioClicked, weld::Button&, void)
{
    if (!m_xScenarioPicker)
        return;
    const OUString id = m_xScenarioPicker->get_active_id();
    if (id.isEmpty())
    {
        m_xStatusLabel->set_label(u"请点上方方案按钮，或在 选项→可圈 AI 中启用「显示为按钮」。"_ustr);
        return;
    }
    RunScenarioById(id);
}

IMPL_LINK_NOARG(AIChatPanel, OnRefreshScenariosClicked, weld::Button&, void)
{
    ReloadScenarioPicker();
    ConsumePendingScenarioRun();
}

IMPL_LINK(AIChatPanel, OnScenarioGridClicked, weld::Button&, rButton, void)
{
    for (sal_Int32 i = 0; i < kScenarioGridSlots; ++i)
    {
        auto& btn = m_xScenarioGridBtns[static_cast<size_t>(i)];
        if (btn && btn.get() == &rButton)
        {
            const OUString& id = m_aScenarioGridIds[static_cast<size_t>(i)];
            if (!id.isEmpty())
            {
                // Sync picker + option checkboxes from scenario defaults
                if (m_xScenarioPicker)
                {
                    for (sal_Int32 j = 0; j < m_xScenarioPicker->get_count(); ++j)
                    {
                        if (m_xScenarioPicker->get_id(j) == id)
                        {
                            m_xScenarioPicker->set_active(j);
                            break;
                        }
                    }
                    OnScenarioPickerChanged(*m_xScenarioPicker);
                }
                RunScenarioById(id);
            }
            return;
        }
    }
}

IMPL_LINK(AIChatPanel, OnScenarioPinClicked, weld::Button&, rButton, void)
{
    for (sal_Int32 i = 0; i < kScenarioPinSlots; ++i)
    {
        auto& btn = m_xScenarioPinBtns[static_cast<size_t>(i)];
        if (btn && btn.get() == &rButton)
        {
            const OUString& id = m_aScenarioPinIds[static_cast<size_t>(i)];
            if (!id.isEmpty())
            {
                if (m_xScenarioPicker)
                {
                    for (sal_Int32 j = 0; j < m_xScenarioPicker->get_count(); ++j)
                    {
                        if (m_xScenarioPicker->get_id(j) == id)
                        {
                            m_xScenarioPicker->set_active(j);
                            OnScenarioPickerChanged(*m_xScenarioPicker);
                            break;
                        }
                    }
                }
                RunScenarioById(id);
            }
            return;
        }
    }
}

IMPL_LINK_NOARG(AIChatPanel, OnCategoryTabToggled, weld::Toggleable&, void)
{
    if (m_bSuppressCategoryReload)
        return;
    // Manual tab click: stop auto-follow so user choice sticks
    if (m_xOptFollowDoc && m_xOptFollowDoc->get_active())
    {
        m_bSuppressCategoryReload = true;
        m_xOptFollowDoc->set_active(false);
        m_bSuppressCategoryReload = false;
    }
    // Reload only for the newly-active tab (skip deactivate half of radio pair)
    if ((m_xTabWriter && m_xTabWriter->get_active()) || (m_xTabCalc && m_xTabCalc->get_active())
        || (m_xTabImpress && m_xTabImpress->get_active())
        || (m_xTabGeneral && m_xTabGeneral->get_active()))
        ReloadScenarioPicker();
}

IMPL_LINK_NOARG(AIChatPanel, OnFollowDocToggled, weld::Toggleable&, void)
{
    if (m_bSuppressCategoryReload)
        return;
    ReloadScenarioPicker();
}

IMPL_LINK_NOARG(AIChatPanel, OnScenarioPickerChanged, weld::ComboBox&, void)
{
    if (!m_xScenarioPicker)
        return;
    const OUString id = m_xScenarioPicker->get_active_id();
    const auto cat = kqoffice::ai::chat::DocumentAIScenarioStore::load();
    if (const auto* p = kqoffice::ai::chat::DocumentAIScenarioStore::find(cat, id))
    {
        if (m_xOptAttachSelection)
            m_xOptAttachSelection->set_active(p->options.attachSelection);
        if (m_xOptAgentPipeline)
            m_xOptAgentPipeline->set_active(p->options.useAgentPipeline);
        if (m_xOptDocContext)
            m_xOptDocContext->set_active(p->options.includeDocContext);
        m_xStatusLabel->set_label(u"已选："_ustr + p->titleZh
                                  + u" — 点网格按钮或「执行」"_ustr);
    }
}

IMPL_LINK_NOARG(AIChatPanel, OnPromptActivated, weld::Entry&, bool)
{
    SubmitPrompt();
    return true;
}

void AIChatPanel::AppendPromptText(const OUString& rText)
{
    if (!m_xPromptEntry || rText.isEmpty())
        return;
    OUString cur = m_xPromptEntry->get_text();
    if (!cur.isEmpty() && !cur.endsWith(u" ") && !cur.endsWith(u"\n"))
        cur += u" "_ustr;
    m_xPromptEntry->set_text(cur + rText);
    FocusPrompt();
}

IMPL_LINK_NOARG(AIChatPanel, OnInjectPollTick, Timer*, void)
{
    ConsumePendingPromptInject();
    ConsumePendingScenarioRun();
}

void AIChatPanel::ConsumePendingPromptInject()
{
    const char* home = std::getenv("HOME");
    if (!home || !*home)
        return;
    const OUString path
        = OUString::fromUtf8(home) + u"/.config/kqoffice/pending-prompt-inject"_ustr;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(path, url) != osl::FileBase::E_None)
        return;
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return;
    sal_uInt64 sz = 0;
    f.getSize(sz);
    if (sz == 0 || sz > 64 * 1024)
    {
        f.close();
        osl::File::remove(url);
        return;
    }
    std::vector<char> buf(static_cast<size_t>(sz));
    sal_uInt64 n = 0;
    f.read(buf.data(), sz, n);
    f.close();
    osl::File::remove(url);
    if (n == 0)
        return;
    const OUString text
        = OUString::fromUtf8(std::string_view(buf.data(), static_cast<size_t>(n))).trim();
    if (text.isEmpty())
        return;
    AppendPromptText(text);
    OUString source = u"inject"_ustr;
    if (text.indexOf(u"【可圈工作中台"_ustr) >= 0)
        source = u"workbench"_ustr;
    else if (text.indexOf(u"【可圈本地记事本"_ustr) >= 0
             || text.indexOf(u"【来自本地记事本"_ustr) >= 0
             || text.indexOf(u"【请基于以下本地材料"_ustr) >= 0)
        source = u"notebook"_ustr;
    else if (text.indexOf(u"截图"_ustr) >= 0 || text.indexOf(u"screenshot"_ustr) >= 0)
        source = u"screenshot"_ustr;
    else
        source = u"voice-or-global"_ustr;
    AppendTranscript(u"System"_ustr,
                     u"prompt-inject source="_ustr + source + u" len="_ustr
                         + OUString::number(text.getLength()),
                     /*bPersistHistory*/ false);
    if (m_xStatusLabel)
    {
        if (source == u"workbench"_ustr)
            m_xStatusLabel->set_label(u"已附上工作中台本地上下文 — 可编辑后发送"_ustr);
        else if (source == u"notebook"_ustr)
            m_xStatusLabel->set_label(u"已附上记事本本地上下文 — 可编辑后发送"_ustr);
        else
            m_xStatusLabel->set_label(u"已附上截图/语音内容 — 可编辑后发送"_ustr);
    }
}

void AIChatPanel::RunScreenshotMode(const OUString& rMode)
{
    const auto mode = kqoffice::ai::chat::DocumentAIInputPrefs::screenshotModeFromString(rMode);
    const kqoffice::ai::chat::ScreenCaptureResult shot
        = kqoffice::ai::chat::DocumentAIScreenCapture::capture(mode);
    AppendTranscript(u"System"_ustr,
                     u"screenshot mode="_ustr + shot.mode + u" success="_ustr
                         + (shot.success ? u"1"_ustr : u"0"_ustr) + u" path="_ustr + shot.path,
                     /*bPersistHistory*/ false);
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(shot.message);
    if (shot.success)
    {
        kqoffice::ai::workbench::WorkTelemetryStore::recordSimple(u"screenshot"_ustr, 1);
        const auto prefs = kqoffice::ai::chat::DocumentAIInputPrefs::load();
        if (prefs.screenshotAutoAttachChat && !shot.promptAttachment.isEmpty())
            AppendPromptText(shot.promptAttachment + u"\n请结合截图说明："_ustr);
    }
}

void AIChatPanel::applyVoiceCaptureUi(const kqoffice::ai::chat::VoiceCaptureResult& cap)
{
    AppendTranscript(u"System"_ustr,
                     u"voice-entry source="_ustr + cap.source + u" success="_ustr
                         + (cap.success ? u"1"_ustr : u"0"_ustr) + u" listening="_ustr
                         + (cap.listening ? u"1"_ustr : u"0"_ustr) + u" cloud=0"_ustr,
                     /*bPersistHistory*/ false);
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(cap.message);
    if (m_xVoiceButton)
    {
        m_xVoiceButton->set_tooltip_text(kqoffice::ai::chat::DocumentAIVoiceInput::statusHint());
        m_xVoiceButton->set_label(cap.listening ? u"松手结束"_ustr : u"语音"_ustr);
    }
    if (cap.success && !cap.text.isEmpty())
    {
        kqoffice::ai::workbench::WorkTelemetryStore::recordSimple(u"voice"_ustr, 1);
        AppendPromptText(cap.text);
        AppendTranscript(u"System"_ustr,
                         u"voice-text-filled len="_ustr
                             + OUString::number(cap.text.getLength())
                             + u" awaiting-user-send=true"_ustr,
                         /*bPersistHistory*/ false);
        if (m_xVoiceButton)
            m_xVoiceButton->set_label(u"语音"_ustr);
        m_bVoiceHoldActive = false;
    }
    else if (!cap.listening)
    {
        FocusPrompt();
        m_bVoiceHoldActive = false;
    }
}

IMPL_LINK_NOARG(AIChatPanel, OnVoiceClicked, weld::Button&, void)
{
    // Click toggle when not using press-hold path (or as fallback).
    if (m_bVoiceHoldActive)
        return; // release handler will finish
    const auto prefs = kqoffice::ai::chat::DocumentAIInputPrefs::load();
    kqoffice::ai::chat::VoiceCaptureResult cap;
    if (prefs.voicePushToTalk || prefs.voiceBackend == kqoffice::ai::chat::VoiceBackend::PushToTalkRecord)
        cap = kqoffice::ai::chat::DocumentAIVoiceInput::togglePushToTalk();
    else
        cap = kqoffice::ai::chat::DocumentAIVoiceInput::captureOnce();
    applyVoiceCaptureUi(cap);
}

IMPL_LINK(AIChatPanel, OnVoiceMousePress, const MouseEvent&, rMEvt, bool)
{
    if (!rMEvt.IsLeft())
        return false;
    const auto prefs = kqoffice::ai::chat::DocumentAIInputPrefs::load();
    if (!prefs.voicePushToTalk && prefs.voiceBackend != kqoffice::ai::chat::VoiceBackend::PushToTalkRecord)
        return false; // let click handler work
    if (kqoffice::ai::chat::DocumentAIVoiceInput::isListening())
        return false;
    m_bVoiceHoldActive = true;
    const auto cap = kqoffice::ai::chat::DocumentAIVoiceInput::togglePushToTalk();
    applyVoiceCaptureUi(cap);
    return true; // consume — avoid duplicate click toggle
}

IMPL_LINK(AIChatPanel, OnVoiceMouseRelease, const MouseEvent&, rMEvt, bool)
{
    if (!rMEvt.IsLeft() || !m_bVoiceHoldActive)
        return false;
    if (!kqoffice::ai::chat::DocumentAIVoiceInput::isListening())
    {
        m_bVoiceHoldActive = false;
        return true;
    }
    const auto cap = kqoffice::ai::chat::DocumentAIVoiceInput::togglePushToTalk();
    applyVoiceCaptureUi(cap);
    m_bVoiceHoldActive = false;
    return true;
}

IMPL_LINK_NOARG(AIChatPanel, OnScreenshotClicked, weld::Button&, void)
{
    RunScreenshotMode(u"region"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnScreenshotWinClicked, weld::Button&, void)
{
    RunScreenshotMode(u"window"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnCtxWorkbenchClicked, weld::Button&, void)
{
    const OUString brief = kqoffice::ai::workbench::WorkTelemetryStore::formatAiContextBrief();
    if (brief.isEmpty())
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"工作中台上下文为空"_ustr);
        return;
    }
    AppendPromptText(brief);
    AppendTranscript(u"System"_ustr, u"context source=workbench local-first"_ustr,
                     /*bPersistHistory*/ false);
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"已注入工作中台上下文 — 可编辑后发送"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnCtxNotebookClicked, weld::Button&, void)
{
    const OUString brief = kqoffice::ai::notebook::LocalNotebookStore::formatAiContextBrief(5);
    if (brief.isEmpty())
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"记事本上下文为空"_ustr);
        return;
    }
    AppendPromptText(brief);
    AppendTranscript(u"System"_ustr, u"context source=notebook local-first"_ustr,
                     /*bPersistHistory*/ false);
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"已注入记事本上下文 — 可编辑后发送"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnScreenshotFullClicked, weld::Button&, void)
{
    RunScreenshotMode(u"fullscreen"_ustr);
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
    ClearPendingPlan();
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
