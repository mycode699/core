/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1: In-app AI chat).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatPanel.hxx"

#include <dispatch/KqNotebookDispatcher.hxx>
#include <startcentertheme.hxx>
#include <vcl/event.hxx>
#include <vcl/svapp.hxx>
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
#include <ScheduledTask.hxx>
#include <TaskRunner.hxx>
#include <ScheduledTaskDispatcher.hxx>
#include <DocumentAIApply.hxx>
#include <DocumentAIFormulaDryRun.hxx>
#include <DocumentAIVerify.hxx>
#include <DocumentAIContext.hxx>
#include <DocumentAIDocumentTools.hxx>
#include <DocumentAIEnterpriseConnectors.hxx>
#include <DocumentAIInputPrefs.hxx>
#include <DocumentAIScreenCapture.hxx>
#include <DocumentAIVisionEvidence.hxx>
#include <DocumentAILocalRag.hxx>
#include <DocumentAITaskBootstrap.hxx>
#include <DocumentAIWorkPlan.hxx>
#include <DocumentAIRewriteMemory.hxx>

#include "AIChatDocumentToolsContentBridge.hxx"
#include "AIChatKnowledgeFtsEngine.hxx"
#include "AIChatKnowledgeRetrievalRuntime.hxx"
#include "AIChatKnowledgeResultContentBridge.hxx"
#include <DocumentAIScenarioStore.hxx>
#include <DocumentAIScenarios.hxx>
#include <DocumentAIVoiceInput.hxx>
#include <DocumentAIMaterialReader.hxx>
#include <WorkTelemetryStore.hxx>
#include <LocalNotebookStore.hxx>

#include <osl/file.hxx>
#include <osl/time.h>
#include <rtl/string.hxx>
#include <sal/types.h>
#include <sfx2/filedlghelper.hxx>
#include <comphelper/errcode.hxx>
#include <com/sun/star/ui/dialogs/TemplateDescription.hpp>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>
#include <EvidenceRecorder.hxx>
#include <ModelRoles.hxx>
#include <MembershipClient.hxx>
#include <ModelRoutingConfig.hxx>
#include <ProviderStreamHelper.hxx>
#include <VaultStore.hxx>
#include <VaultManager.hxx>
#include <VaultIngest.hxx>
#include <VaultCompile.hxx>
#include <VaultLint.hxx>
#include <VaultPack.hxx>

#include "AIChatVaultBridge.hxx"

#include <AICanvasIntegration.hxx>
#include <AICanvasMode.hxx>
#include <AICanvasUI.hxx>

#include <AIFileManager.hxx>
#include <AIFileSearchUI.hxx>
#include <BatchJob.hxx>

#include <sot/filelist.hxx>
#include <sot/formats.hxx>
#include <tools/urlobj.hxx>
#include <vcl/transfer.hxx>

#include <AICanvasEntryPoint.hxx>

#include <kq_permission_prompt.hxx>

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
#include <vcl/svapp.hxx>
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
    = u"上下文：@selection · @doc · @文件:路径 · @截图:路径 · @文件夹:路径 · @connector:id";

css::ai::ProviderResponse MakeLocalFailure(const OUString& rStatus, const OUString& rMessage)
{
    css::ai::ProviderResponse aResponse;
    aResponse.status = rStatus;
    aResponse.content = rMessage;
    aResponse.evidenceId = OUString();
    aResponse.durationMs = 0;
    return aResponse;
}

/// Truncate long model/exception text for status / activity (keep readable Chinese).
OUString ShortenUserDetail(const OUString& rDetail, sal_Int32 nMax = 72)
{
    OUString s = rDetail.replaceAll(u"\n"_ustr, u" "_ustr).trim();
    if (s.getLength() > nMax)
        s = s.copy(0, nMax) + u"…"_ustr;
    return s;
}

OUString IntentIdToZh(const OUString& rIntentId)
{
    if (rIntentId == u"rewrite"_ustr)
        return u"改写"_ustr;
    if (rIntentId == u"formal"_ustr)
        return u"正式语气"_ustr;
    if (rIntentId == u"shorten"_ustr)
        return u"精简"_ustr;
    if (rIntentId == u"expand"_ustr)
        return u"扩写"_ustr;
    if (rIntentId == u"summarize"_ustr)
        return u"总结"_ustr;
    if (rIntentId == u"plan"_ustr)
        return u"规划"_ustr;
    if (rIntentId == u"outline"_ustr)
        return u"大纲"_ustr;
    if (rIntentId == u"proofread"_ustr)
        return u"审阅"_ustr;
    if (rIntentId == u"continue"_ustr)
        return u"续写"_ustr;
    if (rIntentId == u"doc-summary"_ustr)
        return u"全文总结"_ustr;
    if (rIntentId == u"agent"_ustr)
        return u"多步协作"_ustr;
    if (rIntentId == u"review"_ustr)
        return u"审阅"_ustr;
    if (rIntentId == u"chat"_ustr)
        return u"对话"_ustr;
    return rIntentId;
}

bool IsMentionBoundary(sal_Unicode c)
{
    return c == u' ' || c == u'\t' || c == u'\n' || c == u'\r' || c == u',' || c == u';'
           || c == u'.' || c == u')' || c == u']' || c == u'}';
}

bool IsMaterialPrefixAt(const OUString& rPrompt, sal_Int32 nPos)
{
    if (nPos < 0 || nPos >= rPrompt.getLength() || rPrompt[nPos] != u'@')
        return false;
    const OUString tail = rPrompt.copy(nPos);
    return tail.startsWith(u"@文件:"_ustr) || tail.startsWith(u"@截图:"_ustr)
           || tail.startsWith(u"@文件夹:"_ustr) || tail.startsWith(u"@folder:"_ustr)
           || tail.startsWith(u"@file:"_ustr);
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

/// Single live AI chat panel for DiffReview C ABI (pending 批准写回 / 拒绝).
AIChatPanel* g_pActiveAIChatPanel = nullptr;

} // namespace

AIChatPanel* AIChatPanel::GetActivePanel() { return g_pActiveAIChatPanel; }

bool AIChatPanel::RunQuickIntent(const OUString& rIntentId, const OUString& rSeedPrompt)
{
    if (IsRunBusy())
        return false;
    ApplyComposerIntent(rIntentId, rSeedPrompt);
    return true;
}

AIChatPanel::AIChatPanel(weld::Widget* pParent)
    // Slim Stage1 UI first (safe Show). Multi-tab layout lives in aichatpanel_full.ui
    // for a later lazy UI swap; stores/logic still load on this panel after paint.
    : PanelLayout(pParent, u"AIChatPanel"_ustr, u"sfx/ui/aichatpanel.ui"_ustr)
    , m_xStatusLabel(m_xBuilder->weld_label(u"status_label"_ustr))
    , m_xAgentStepBar(m_xBuilder->weld_label(u"agent_step_bar"_ustr))
    , m_xActivityCard(m_xBuilder->weld_label(u"activity_card"_ustr))
    , m_xDesignFlowBox(m_xBuilder->weld_widget(u"design_flow_box"_ustr))
    , m_xDesignStepOutline(m_xBuilder->weld_button(u"design_step_outline"_ustr))
    , m_xDesignStepVariants(m_xBuilder->weld_button(u"design_step_variants"_ustr))
    , m_xDesignStepApply(m_xBuilder->weld_button(u"design_step_apply"_ustr))
    , m_xDesignStepExport(m_xBuilder->weld_button(u"design_step_export"_ustr))
    , m_xTranscriptView(m_xBuilder->weld_text_view(u"transcript_view"_ustr))
    , m_xPromptEntry(m_xBuilder->weld_entry(u"prompt_entry"_ustr))
    , m_xIntentRewriteBtn(m_xBuilder->weld_button(u"intent_rewrite_btn"_ustr))
    , m_xIntentFormalBtn(m_xBuilder->weld_button(u"intent_formal_btn"_ustr))
    , m_xIntentShortenBtn(m_xBuilder->weld_button(u"intent_shorten_btn"_ustr))
    , m_xIntentExpandBtn(m_xBuilder->weld_button(u"intent_expand_btn"_ustr))
    , m_xIntentSummarizeBtn(m_xBuilder->weld_button(u"intent_summarize_btn"_ustr))
    , m_xIntentOutlineBtn(m_xBuilder->weld_button(u"intent_outline_btn"_ustr))
    , m_xIntentProofreadBtn(m_xBuilder->weld_button(u"intent_proofread_btn"_ustr))
    , m_xIntentContinueBtn(m_xBuilder->weld_button(u"intent_continue_btn"_ustr))
    , m_xIntentPlanBtn(m_xBuilder->weld_button(u"intent_plan_btn"_ustr))
    , m_xIntentAgentBtn(m_xBuilder->weld_button(u"intent_agent_btn"_ustr))
    , m_xVoiceButton(m_xBuilder->weld_button(u"voice_button"_ustr))
    , m_xScreenshotButton(m_xBuilder->weld_button(u"screenshot_button"_ustr))
    , m_xScreenshotWinButton(m_xBuilder->weld_button(u"screenshot_win_button"_ustr))
    , m_xScreenshotFullButton(m_xBuilder->weld_button(u"screenshot_full_button"_ustr))
    , m_xAttachFileButton(m_xBuilder->weld_button(u"attach_file_button"_ustr))
    , m_xCtxWorkbenchButton(m_xBuilder->weld_button(u"ctx_workbench_btn"_ustr))
    , m_xCtxNotebookButton(m_xBuilder->weld_button(u"ctx_notebook_btn"_ustr))
    , m_xSendButton(m_xBuilder->weld_button(u"send_button"_ustr))
    , m_xCancelButton(m_xBuilder->weld_button(u"cancel_button"_ustr))
    , m_xRetryButton(m_xBuilder->weld_button(u"retry_button"_ustr))
    , m_xClearHistoryButton(m_xBuilder->weld_button(u"clear_history_button"_ustr))
    , m_xArtifactTree(m_xBuilder->weld_tree_view(u"artifact_tree"_ustr))
    , m_xAgentTree(m_xBuilder->weld_tree_view(u"agent_tree"_ustr))
    , m_xAgentEmptyLabel(m_xBuilder->weld_label(u"agent_empty_label"_ustr))
    , m_xAgentRunBtn(m_xBuilder->weld_button(u"agent_run_btn"_ustr))
    , m_xAgentContinueBtn(m_xBuilder->weld_button(u"agent_continue_btn"_ustr))
    , m_xAgentApproveBtn(m_xBuilder->weld_button(u"agent_approve_btn"_ustr))
    , m_xAgentStopBtn(m_xBuilder->weld_button(u"agent_stop_btn"_ustr))
    , m_xAgentRefreshBtn(m_xBuilder->weld_button(u"agent_refresh_btn"_ustr))
    , m_xAgentClearBtn(m_xBuilder->weld_button(u"agent_clear_btn"_ustr))
    , m_xScheduleTree(m_xBuilder->weld_tree_view(u"schedule_tree"_ustr))
    , m_xScheduleRefreshBtn(m_xBuilder->weld_button(u"schedule_refresh_btn"_ustr))
    , m_xScheduleToggleBtn(m_xBuilder->weld_button(u"schedule_toggle_btn"_ustr))
    , m_xScheduleRunNowBtn(m_xBuilder->weld_button(u"schedule_run_now_btn"_ustr))
    , m_xScheduleAddBtn(m_xBuilder->weld_button(u"schedule_add_btn"_ustr))
    , m_xScheduleRemoveBtn(m_xBuilder->weld_button(u"schedule_remove_btn"_ustr))
    , m_xScheduleKind(m_xBuilder->weld_combo_box(u"schedule_kind"_ustr))
    , m_xScheduleInterval(m_xBuilder->weld_entry(u"schedule_interval"_ustr))
    , m_xScheduleHour(m_xBuilder->weld_entry(u"schedule_hour"_ustr))
    , m_xScheduleMinute(m_xBuilder->weld_entry(u"schedule_minute"_ustr))
    , m_xScheduleSaveBtn(m_xBuilder->weld_button(u"schedule_save_btn"_ustr))
    , m_xScheduleAutoSend(m_xBuilder->weld_check_button(u"schedule_auto_send"_ustr))
    , m_xBatchTree(m_xBuilder->weld_tree_view(u"batch_tree"_ustr))
    , m_xBatchRefreshBtn(m_xBuilder->weld_button(u"batch_refresh_btn"_ustr))
    , m_xReviewTree(m_xBuilder->weld_tree_view(u"review_tree"_ustr))
    , m_xReviewEmptyLabel(m_xBuilder->weld_label(u"review_empty_label"_ustr))
    , m_xReviewRefreshBtn(m_xBuilder->weld_button(u"review_refresh_btn"_ustr))
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
    , m_xOpenKqNotebookButton(m_xBuilder->weld_button(u"open_kq_notebook_btn"_ustr))
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
    , m_xLocateRagBtn(m_xBuilder->weld_button(u"locate_rag_btn"_ustr))
    , m_xScenarioPinnedLabel(m_xBuilder->weld_label(u"scenario_pinned_label"_ustr))
    , m_xSelectionChipBtn(m_xBuilder->weld_button(u"selection_chip_btn"_ustr))
    , m_xTaskBootstrapChip(m_xBuilder->weld_button(u"task_bootstrap_chip"_ustr))
    , m_xPendingPlanChip(m_xBuilder->weld_button(u"pending_plan_chip"_ustr))
    , m_xApprovalActionRow(m_xBuilder->weld_widget(u"approval_action_row"_ustr))
    , m_xApprovalHintLabel(m_xBuilder->weld_label(u"approval_hint_label"_ustr))
    , m_xChatApproveBtn(m_xBuilder->weld_button(u"chat_approve_btn"_ustr))
    , m_xChatDiffBtn(m_xBuilder->weld_button(u"chat_diff_btn"_ustr))
    , m_xChatRejectBtn(m_xBuilder->weld_button(u"chat_reject_btn"_ustr))
    , m_xChatUndoBtn(m_xBuilder->weld_button(u"chat_undo_btn"_ustr))
    , m_xMainNotebook(m_xBuilder->weld_notebook(u"main_notebook"_ustr))
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
    , m_aDeferredWarmup("AIChatDeferredWarmup")
    , m_aScheduleTick("AIChatScheduleTick")
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

    // Sidebar first-show often has a tight/zero allocation. Floor size + keep the
    // agent/content/review workspace host hidden so InterimItemWindow layout only
    // measures the chat surface (avoids TabControl/VclScrolledWindow abort on macOS).
    if (m_xContainer)
        m_xContainer->set_size_request(280, 240);
    if (std::unique_ptr<weld::Widget> xDeferred
        = m_xBuilder->weld_widget(u"deferred_workspace_host"_ustr))
    {
        xDeferred->set_visible(false);
    }

    if (m_xTranscriptView)
        m_xTranscriptView->set_editable(false);
    if (m_xArtifactTree)
        m_xArtifactTree->set_selection_mode(SelectionMode::Single);
    if (m_xAgentTree)
    {
        m_xAgentTree->set_selection_mode(SelectionMode::Single);
        // 2 visible columns: 步骤 / 状态
        m_xAgentTree->clear();
    }
    if (m_xScheduleTree)
    {
        m_xScheduleTree->set_selection_mode(SelectionMode::Single);
        // 4 columns: 标题 / 计划 / 状态 / 上次结果
        m_xScheduleTree->set_column_fixed_widths({ 90, 72, 72, 100 });
        m_xScheduleTree->clear();
        m_xScheduleTree->connect_selection_changed(
            LINK(this, AIChatPanel, OnScheduleTreeSelectionChanged));
    }
    if (m_xScheduleKind)
        m_xScheduleKind->set_active_id(u"once"_ustr);
    // Prefer human review: scheduleAutoSend defaults false in prefs + UI.
    if (m_xScheduleAutoSend)
    {
        const auto prefs = kqoffice::ai::chat::DocumentAIInputPrefs::load();
        m_xScheduleAutoSend->set_active(prefs.scheduleAutoSend);
    }
    if (m_xReviewTree)
    {
        m_xReviewTree->set_selection_mode(SelectionMode::Single);
        m_xReviewTree->clear();
    }
    if (m_xPromptEntry)
    {
        m_xPromptEntry->set_placeholder_text(u"描述你要做的事，或点意图芯片 / 上方方案…"_ustr);
        m_xPromptEntry->connect_insert_text(LINK(this, AIChatPanel, OnPromptInsertText));
        m_xPromptEntry->connect_changed(LINK(this, AIChatPanel, OnPromptChanged));
        m_xPromptEntry->connect_activate(LINK(this, AIChatPanel, OnPromptActivated));
    }
    if (m_xIntentRewriteBtn)
        m_xIntentRewriteBtn->connect_clicked(LINK(this, AIChatPanel, OnIntentRewriteClicked));
    if (m_xIntentFormalBtn)
        m_xIntentFormalBtn->connect_clicked(LINK(this, AIChatPanel, OnIntentFormalClicked));
    if (m_xIntentShortenBtn)
        m_xIntentShortenBtn->connect_clicked(LINK(this, AIChatPanel, OnIntentShortenClicked));
    if (m_xIntentExpandBtn)
        m_xIntentExpandBtn->connect_clicked(LINK(this, AIChatPanel, OnIntentExpandClicked));
    if (m_xIntentSummarizeBtn)
        m_xIntentSummarizeBtn->connect_clicked(LINK(this, AIChatPanel, OnIntentSummarizeClicked));
    if (m_xIntentOutlineBtn)
        m_xIntentOutlineBtn->connect_clicked(LINK(this, AIChatPanel, OnIntentOutlineClicked));
    if (m_xIntentProofreadBtn)
        m_xIntentProofreadBtn->connect_clicked(LINK(this, AIChatPanel, OnIntentProofreadClicked));
    if (m_xIntentContinueBtn)
        m_xIntentContinueBtn->connect_clicked(LINK(this, AIChatPanel, OnIntentContinueClicked));
    if (m_xIntentPlanBtn)
        m_xIntentPlanBtn->connect_clicked(LINK(this, AIChatPanel, OnIntentPlanClicked));
    if (m_xIntentAgentBtn)
        m_xIntentAgentBtn->connect_clicked(LINK(this, AIChatPanel, OnIntentAgentClicked));
    if (m_xDesignStepOutline)
        m_xDesignStepOutline->connect_clicked(LINK(this, AIChatPanel, OnDesignStepOutlineClicked));
    if (m_xDesignStepVariants)
        m_xDesignStepVariants->connect_clicked(LINK(this, AIChatPanel, OnDesignStepVariantsClicked));
    if (m_xDesignStepApply)
        m_xDesignStepApply->connect_clicked(LINK(this, AIChatPanel, OnDesignStepApplyClicked));
    if (m_xDesignStepExport)
        m_xDesignStepExport->connect_clicked(LINK(this, AIChatPanel, OnDesignStepExportClicked));
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
    if (m_xAttachFileButton)
        m_xAttachFileButton->connect_clicked(LINK(this, AIChatPanel, OnAttachFileClicked));
    if (m_xCtxWorkbenchButton)
        m_xCtxWorkbenchButton->connect_clicked(LINK(this, AIChatPanel, OnCtxWorkbenchClicked));
    if (m_xCtxNotebookButton)
        m_xCtxNotebookButton->connect_clicked(LINK(this, AIChatPanel, OnCtxNotebookClicked));
    if (m_xSendButton)
        m_xSendButton->connect_clicked(LINK(this, AIChatPanel, OnSendClicked));
    if (m_xCancelButton)
        m_xCancelButton->connect_clicked(LINK(this, AIChatPanel, OnCancelClicked));
    if (m_xRetryButton)
        m_xRetryButton->connect_clicked(LINK(this, AIChatPanel, OnRetryClicked));
    if (m_xClearHistoryButton)
        m_xClearHistoryButton->connect_clicked(LINK(this, AIChatPanel, OnClearHistoryClicked));
    if (m_xArtifactTree)
    {
        m_xArtifactTree->connect_selection_changed(
            LINK(this, AIChatPanel, OnArtifactSelectionChanged));
        m_xArtifactTree->connect_row_activated(LINK(this, AIChatPanel, OnArtifactRowActivated));
    }
    if (m_xRefreshArtifactsButton)
        m_xRefreshArtifactsButton->connect_clicked(
            LINK(this, AIChatPanel, OnRefreshArtifactsClicked));
    if (m_xOpenArtifactButton)
        m_xOpenArtifactButton->connect_clicked(LINK(this, AIChatPanel, OnOpenArtifactClicked));
    if (m_xOpenDiffReviewButton)
        m_xOpenDiffReviewButton->connect_clicked(LINK(this, AIChatPanel, OnOpenDiffReviewClicked));
    if (m_xReviewArtifactButton)
        m_xReviewArtifactButton->connect_clicked(LINK(this, AIChatPanel, OnReviewArtifactClicked));
    if (m_xFormatArtifactButton)
        m_xFormatArtifactButton->connect_clicked(LINK(this, AIChatPanel, OnFormatArtifactClicked));
    if (m_xInspectEvidenceButton)
        m_xInspectEvidenceButton->connect_clicked(LINK(this, AIChatPanel, OnInspectEvidenceClicked));
    if (m_xApproveSelectedButton)
        m_xApproveSelectedButton->connect_clicked(
            LINK(this, AIChatPanel, OnApproveSelectedClicked));
    if (m_xRejectSelectedButton)
        m_xRejectSelectedButton->connect_clicked(LINK(this, AIChatPanel, OnRejectSelectedClicked));
    if (m_xCopyReferenceButton)
        m_xCopyReferenceButton->connect_clicked(LINK(this, AIChatPanel, OnCopyReferenceClicked));
    if (m_xExportEvidenceButton)
        m_xExportEvidenceButton->connect_clicked(LINK(this, AIChatPanel, OnExportEvidenceClicked));
    if (m_xFilterWorkspaceButton)
        m_xFilterWorkspaceButton->connect_clicked(LINK(this, AIChatPanel, OnFilterWorkspaceClicked));
    if (m_xSortWorkspaceButton)
        m_xSortWorkspaceButton->connect_clicked(LINK(this, AIChatPanel, OnSortWorkspaceClicked));
    if (m_xRemoveArtifactButton)
        m_xRemoveArtifactButton->connect_clicked(
            LINK(this, AIChatPanel, OnRemoveArtifactClicked));
    if (m_xAgentRunBtn)
        m_xAgentRunBtn->connect_clicked(LINK(this, AIChatPanel, OnAgentRunClicked));
    if (m_xAgentContinueBtn)
    {
        m_xAgentContinueBtn->connect_clicked(LINK(this, AIChatPanel, OnAgentContinueClicked));
        m_xAgentContinueBtn->set_sensitive(false);
    }
    if (m_xAgentApproveBtn)
        m_xAgentApproveBtn->connect_clicked(LINK(this, AIChatPanel, OnAgentApproveClicked));
    if (m_xAgentStopBtn)
        m_xAgentStopBtn->connect_clicked(LINK(this, AIChatPanel, OnAgentStopClicked));
    if (m_xAgentRefreshBtn)
        m_xAgentRefreshBtn->connect_clicked(LINK(this, AIChatPanel, OnAgentRefreshClicked));
    if (m_xAgentClearBtn)
        m_xAgentClearBtn->connect_clicked(LINK(this, AIChatPanel, OnAgentClearClicked));
    if (m_xScheduleRefreshBtn)
        m_xScheduleRefreshBtn->connect_clicked(LINK(this, AIChatPanel, OnScheduleRefreshClicked));
    if (m_xScheduleToggleBtn)
        m_xScheduleToggleBtn->connect_clicked(LINK(this, AIChatPanel, OnScheduleToggleClicked));
    if (m_xScheduleRunNowBtn)
        m_xScheduleRunNowBtn->connect_clicked(LINK(this, AIChatPanel, OnScheduleRunNowClicked));
    if (m_xScheduleAddBtn)
        m_xScheduleAddBtn->connect_clicked(LINK(this, AIChatPanel, OnScheduleAddClicked));
    if (m_xScheduleRemoveBtn)
        m_xScheduleRemoveBtn->connect_clicked(LINK(this, AIChatPanel, OnScheduleRemoveClicked));
    if (m_xScheduleSaveBtn)
        m_xScheduleSaveBtn->connect_clicked(LINK(this, AIChatPanel, OnScheduleSaveClicked));
    if (m_xScheduleAutoSend)
        m_xScheduleAutoSend->connect_toggled(LINK(this, AIChatPanel, OnScheduleAutoSendToggled));
    if (m_xBatchRefreshBtn)
        m_xBatchRefreshBtn->connect_clicked(LINK(this, AIChatPanel, OnBatchRefreshClicked));
    if (m_xBatchTree)
    {
        m_xBatchTree->set_selection_mode(SelectionMode::Single);
        m_xBatchTree->set_column_fixed_widths({ 90, 64, 140 });
        m_xBatchTree->clear();
    }
    if (m_xMainNotebook)
        m_xMainNotebook->connect_enter_page(LINK(this, AIChatPanel, OnMainNotebookEnterPage));
    if (m_xReviewRefreshBtn)
        m_xReviewRefreshBtn->connect_clicked(LINK(this, AIChatPanel, OnReviewRefreshClicked));
    if (m_xOpenKqNotebookButton)
        m_xOpenKqNotebookButton->connect_clicked(LINK(this, AIChatPanel, OnOpenKqNotebookClicked));
    if (m_xAiSettingsButton)
        m_xAiSettingsButton->connect_clicked(LINK(this, AIChatPanel, OnAiSettingsClicked));

    // Family UI tokens (same DNA as Start Center / 可圈笔记)
    try
    {
        const auto th = sfx2::sc_theme::tokens();
        if (m_xContainer)
            m_xContainer->set_background(th.canvas);
        if (m_xStatusLabel)
            m_xStatusLabel->set_font_color(th.textSecondary);
    }
    catch (...)
    {
    }
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
    if (m_xTaskBootstrapChip)
    {
        // Reuse selection refresh: user re-anchors context after reading restatement.
        m_xTaskBootstrapChip->connect_clicked(LINK(this, AIChatPanel, OnSelectionChipClicked));
        m_xTaskBootstrapChip->set_label(u"理解：待命"_ustr);
    }
    if (m_xPendingPlanChip)
        m_xPendingPlanChip->connect_clicked(LINK(this, AIChatPanel, OnPendingPlanChipClicked));
    if (m_xChatApproveBtn)
        m_xChatApproveBtn->connect_clicked(LINK(this, AIChatPanel, OnChatApproveClicked));
    if (m_xChatDiffBtn)
        m_xChatDiffBtn->connect_clicked(LINK(this, AIChatPanel, OnChatDiffClicked));
    if (m_xChatUndoBtn)
        m_xChatUndoBtn->connect_clicked(LINK(this, AIChatPanel, OnChatUndoClicked));
    if (m_xChatRejectBtn)
        m_xChatRejectBtn->connect_clicked(LINK(this, AIChatPanel, OnChatRejectClicked));
    if (m_xLocateRagBtn)
        m_xLocateRagBtn->connect_clicked(LINK(this, AIChatPanel, OnLocateRagClicked));
    if (m_xRoutingDiagBtn)
        m_xRoutingDiagBtn->connect_clicked(LINK(this, AIChatPanel, OnRoutingDiagClicked));

    // Critical path only after first paint. Calling weld set_sensitive/Enable during
    // construction (before InterimItemWindow is shown) aborts on macOS VCL.
    if (m_xRoutingDiagLabel)
        m_xRoutingDiagLabel->set_label(u"可圈 AI：探测中…"_ustr);
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"可圈 AI 已就绪 · 模型探测后台进行中…"_ustr);
    // Keep consuming injects while panel is alive (速览→AI / 记事本→AI when already open).
    // 1.2s is enough for handoff injects without burning main-thread timers.
    m_aInjectPoll.SetTimeout(1200);
    m_aInjectPoll.SetInvokeHandler(LINK(this, AIChatPanel, OnInjectPollTick));
    m_aInjectPoll.Start();
    // Warm workspace + routing after first frame (faster than 900ms; still off open path).
    m_aDeferredWarmup.SetTimeout(450);
    m_aDeferredWarmup.SetInvokeHandler(LINK(this, AIChatPanel, OnDeferredWarmupTick));
    m_aDeferredWarmup.Start();
    // Local scheduled tasks: scan due ledger every 60s while AI panel is open.
    // Dispatch writes pending-prompt-inject; m_aInjectPoll consumes it into the prompt.
    m_aScheduleTick.SetTimeout(60'000);
    m_aScheduleTick.SetInvokeHandler(LINK(this, AIChatPanel, OnScheduleTick));
    m_aScheduleTick.Start();
    // Drop-target attach + Enable chrome deferred to OnDeferredWarmupTick (macOS).
    // Do not call SetState/UpdateActions/FocusPrompt/UpdateApprovalChrome here —
    // weld set_sensitive → vcl::Window::Enable during ctor aborts on macOS.
    m_eState = AIChatPanelState::Idle;
    g_pActiveAIChatPanel = this;
}

AIChatPanel::~AIChatPanel()
{
    if (g_pActiveAIChatPanel == this)
        g_pActiveAIChatPanel = nullptr;
    m_aInjectPoll.Stop();
    m_aDeferredWarmup.Stop();
    m_aScheduleTick.Stop();
    if (m_xAttachDropHelper)
    {
        m_xAttachDropHelper->dispose();
        m_xAttachDropHelper.reset();
    }
}

void AIChatPanel::EnsureWorkspaceDataLoaded()
{
    if (m_bWorkspaceDataLoaded)
        return;
    m_bWorkspaceDataLoaded = true;
    LoadArtifactNavigator();
    LoadAgentSteps();
    LoadScheduleList();
    LoadReviewQueue();
    LoadSessionSnapshot();
}

IMPL_LINK_NOARG(AIChatPanel, OnDeferredWarmupTick, Timer*, void)
{
    m_aDeferredWarmup.Stop();
    // First-paint chrome (deferred from ctor — Enable is unsafe until shown).
    try
    {
        LoadDocumentHistory();
        ReloadScenarioPicker();
        UpdateSelectionChip();
        UpdatePendingPlanChip();
        UpdateApprovalChrome();
        UpdateActions();
        FocusPrompt();
        ConsumePendingScenarioRun();
        ConsumePendingPromptInject();
    }
    catch (...)
    {
    }
    // One due-task scan after first paint (moved out of ctor for macOS layout safety).
    try
    {
        kqoffice::ai::cowork::processDueScheduledTasks();
    }
    catch (...)
    {
    }
    EnsureWorkspaceDataLoaded();
    // Soft-touch 资料盘: install defaults + permission seed + related materials.
    try
    {
        (void)kqoffice::ai::vault::VaultManager::ensureInstallDefaults();
        kqoffice::ai::vault::VaultStore::ensureLayout();
        OUString seed;
        try
        {
            const auto sk = kqoffice::ai::chat::DocumentAIDocumentTools::buildSkeleton();
            seed = sk.surface;
            if (!sk.blocks.empty() && !sk.blocks.front().preview.isEmpty())
                seed = sk.blocks.front().preview;
            else if (!sk.statsLine.isEmpty())
                seed = sk.statsLine;
        }
        catch (...)
        {
        }
        if (!seed.isEmpty())
        {
            const auto rel = AIChatVaultRelated(seed, 3);
            if (rel.Success && !rel.Hits.empty() && m_xStatusLabel)
            {
                m_xStatusLabel->set_label(u"相关资料 "_ustr
                                          + OUString::number(static_cast<sal_Int32>(rel.Hits.size()))
                                          + u" · "_ustr + AIChatVaultStatusLineZh());
            }
        }
    }
    catch (...)
    {
    }
    if (!m_bRoutingDiagDone)
    {
        m_bRoutingDiagDone = true;
        // Gateway/Ollama probe kept off the first paint path (M21 cold-open).
        RunRoutingDiagnostics(/*bAppendTranscript*/ false);
        // Ready banner after quiet probe: tell user they can act without waiting more.
        const kqoffice::ai::ModelRoutingDiagnostics d = kqoffice::ai::diagnoseModelRouting();
        if (m_xStatusLabel)
        {
            if (d.healthy)
            {
                const OUString model
                    = d.primaryResolved.isEmpty() ? u"auto"_ustr : d.primaryResolved;
                m_xStatusLabel->set_label(
                    u"可圈 AI 就绪 · "_ustr + model
                    + u" · 选中文字即可改写/正式语气 · 写回须批准"_ustr);
            }
            else
            {
                // Keep short recovery hint from RunRoutingDiagnostics; ensure not stuck on 探测中.
                const OUString cur = m_xStatusLabel->get_label();
                if (cur.indexOf(u"探测"_ustr) >= 0 || cur.startsWith(u"可圈 AI 已就绪"_ustr))
                {
                    m_xStatusLabel->set_label(
                        u"可圈 AI 已打开 · 模型未就绪 · 点「修复模型」配置"_ustr);
                }
            }
        }
    }
}

void AIChatPanel::AppendTranscript(const OUString& rSpeaker, const OUString& rMessage)
{
    AppendTranscript(rSpeaker, rMessage, true);
}

void AIChatPanel::AppendTranscript(const OUString& rSpeaker, const OUString& rMessage,
                                   bool bPersistHistory)
{
    if (!m_xTranscriptView)
        return;
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
        /* Workspace filter visible: state,type,surface */
    /* Workspace sort visible: recent-first */
    AppendTranscript(u"System"_ustr,
                         u"Markdown rejected: "_ustr + aRendered.RejectionReason
                             + u" · 主文档未改"_ustr);
        return;
    }

    AppendTranscript(u"AI"_ustr, aRendered.Text);
}

void AIChatPanel::AppendAssistantChunk(const OUString& rDelta)
{
    // True streaming: concatenate deltas (no artificial spaces) and paint live.
    if (rDelta.isEmpty() || !m_xTranscriptView)
        return;
    const bool bFirst = m_sStreamingBuffer.isEmpty();
    m_sStreamingBuffer += rDelta;
    OUString sText = m_xTranscriptView->get_text();
    if (bFirst)
    {
        if (!sText.isEmpty())
            sText += u"\n\n"_ustr;
        sText += u"AI: "_ustr + rDelta;
    }
    else
        sText += rDelta;
    m_xTranscriptView->set_text(sText);
    m_xTranscriptView->set_position(-1);
}

OUString AIChatPanel::LocalizeProviderStatusZh(const OUString& rStatus)
{
    if (rStatus.isEmpty() || rStatus == u"ok"_ustr || rStatus == u"success"_ustr
        || rStatus == u"completed"_ustr)
        return u"成功"_ustr;
    if (rStatus == u"cancelled"_ustr || rStatus == u"canceled"_ustr || rStatus == u"stopped"_ustr)
        return u"已停止"_ustr;
    if (rStatus == u"timeout"_ustr || rStatus == u"provider-timeout"_ustr)
        return u"可圈 AI 超时"_ustr;
    if (rStatus == u"offline"_ustr || rStatus == u"provider-offline"_ustr)
        return u"可圈 AI 离线"_ustr;
    if (rStatus == u"policy-denied"_ustr)
        return u"策略拒绝（检查服务模式 offline/private/cloud）"_ustr;
    if (rStatus == u"auth-failed"_ustr || rStatus == u"unauthorized"_ustr)
        return u"可圈 AI 认证失败 · 请更新 API Key"_ustr;
    if (rStatus == u"provider-error"_ustr || rStatus == u"error"_ustr || rStatus == u"failed"_ustr)
        return u"可圈 AI 调用失败"_ustr;
    if (rStatus == u"unavailable"_ustr)
        return u"服务不可用"_ustr;
    // Already Chinese-ish (contains CJK) — pass through; else wrap.
    for (sal_Int32 i = 0; i < rStatus.getLength(); ++i)
    {
        const sal_Unicode c = rStatus[i];
        if (c >= 0x4E00 && c <= 0x9FFF)
            return rStatus;
    }
    return u"调用异常（"_ustr + rStatus + u"）"_ustr;
}

OUString AIChatPanel::FormatEvidenceUserSummary(const OUString& rStatus,
                                                 const OUString& rEvidenceId)
{
    const bool bOk = rStatus.isEmpty() || rStatus == u"ok"_ustr || rStatus == u"success"_ustr
                     || rStatus == u"completed"_ustr;
    const bool bCancel
        = rStatus == u"cancelled"_ustr || rStatus == u"canceled"_ustr || rStatus == u"stopped"_ustr;
    OUString line;
    if (bOk)
        line = u"结果：成功 · 主文档仍须批准后写回"_ustr;
    else if (bCancel)
        line = u"结果：已停止 · 主文档未改"_ustr;
    else
        line = u"结果："_ustr + LocalizeProviderStatusZh(rStatus) + u" · 主文档未改"_ustr;
    if (!rEvidenceId.isEmpty())
        line += u" · 证据 "_ustr + rEvidenceId;
    return line;
}

void AIChatPanel::AppendTerminalEvidence(const OUString& rStatus, const OUString& rEvidenceId)
{
    // User-facing Chinese summary first; keep machine tokens for audit/harness.
    OUString aMessage = FormatEvidenceUserSummary(rStatus, rEvidenceId);
    aMessage += u" · terminal-state="_ustr + rStatus;
    if (!rEvidenceId.isEmpty())
        aMessage += u" evidence="_ustr + rEvidenceId;
    AppendTranscript(u"System"_ustr, aMessage);
}

void AIChatPanel::LoadDocumentHistory()
{
    if (!m_xHistoryStore || !m_xTranscriptView)
        return;

    const OUString sTranscript = m_xHistoryStore->LoadTranscript();
    if (sTranscript.isEmpty())
        return;

    m_xTranscriptView->set_text(sTranscript);
    m_xTranscriptView->set_position(-1);
    AppendTranscript(u"System"_ustr,
                     u"history-loaded document-id-hash="_ustr + m_xHistoryStore->GetDocumentKey()
                         + u" · 已恢复本机对话记录"_ustr,
                     false);
}

void AIChatPanel::ClearDocumentHistory()
{
    if (!m_xHistoryStore)
        return;

    const bool bCleared = m_xHistoryStore->Clear();
    if (m_xTranscriptView)
        m_xTranscriptView->set_text(OUString());
    ClearWorkPlan();
    if (m_xHistoryStore)
        kqoffice::ai::chat::DocumentAIRewriteMemory::clear(m_xHistoryStore->GetDocumentKey());
    AppendTranscript(u"System"_ustr,
                     bCleared ? u"history-cleared for current document · 已清空本机对话记录与改稿记忆"_ustr
                              : u"history-clear-failed for current document · 清空失败"_ustr,
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

        // Material mentions allow spaces in path — consume until end of line.
        if (IsMaterialPrefixAt(rPrompt, nPos))
        {
            sal_Int32 lineEnd = nPos;
            while (lineEnd < rPrompt.getLength() && rPrompt[lineEnd] != u'\n'
                   && rPrompt[lineEnd] != u'\r')
                ++lineEnd;
            const OUString full = rPrompt.copy(nPos, lineEnd - nPos).trim();
            aResult.ValidMentions.push_back(full);
            nPos = lineEnd;
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
        return u"无效上下文标记："_ustr + rMentions.InvalidMentions.front()
               + u"（可用：@selection · @doc · @文件:路径 · @截图:路径 · @文件夹:路径）"_ustr;

    if (rMentions.ValidMentions.empty())
        return CONTEXT_MENTION_SUGGESTIONS;

    OUStringBuffer aBuffer(u"已附上下文："_ustr);
    for (size_t i = 0; i < rMentions.ValidMentions.size(); ++i)
    {
        if (i > 0)
            aBuffer.append(u" · "_ustr);
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
        m_xStatusLabel->set_label(StateToUserLabel(m_eState));
}

bool AIChatPanel::ValidateContextMentions(const OUString& rPrompt)
{
    const AIChatContextMentions aMentions = ParseContextMentions(rPrompt);
    if (!aMentions.HasInvalid())
        return true;

    m_xPromptEntry->set_message_type(weld::EntryMessageType::Error);
    m_sLastOutcomeDetail = ShortenUserDetail(FormatContextMentionSummary(aMentions));
    AppendTranscript(u"System"_ustr, m_sLastOutcomeDetail + u" · 主文档未改"_ustr);
    SetState(AIChatPanelState::Failed);
    FocusPrompt();
    return false;
}

bool AIChatPanel::MaterializeInsertedContent(OUString& rInsertedText)
{
    if (!m_xContentObjectStore || !m_xContentObjectStore->ShouldMaterializeText(rInsertedText))
        return true;

    const AIChatMaterializedContent aContent = m_xContentObjectStore->MaterializeText(rInsertedText);
    if (aContent.Reference.isEmpty())
        return false;

    rInsertedText = aContent.Reference;
    const OUString sType = AIChatContentObjectStore::DetectTypeLabel(aContent.Type);
    const OUString sMessage = u"内容对象已物化："_ustr + aContent.Reference
                              + u" · materialized-content reference="_ustr + aContent.Reference
                              + u" type="_ustr + sType;
    AppendTranscript(u"System"_ustr, sMessage, /*bPersistHistory*/ false);
    RecordWorkspaceActivity(u"artifact-created"_ustr, u"artifacts"_ustr, aContent.ObjectId,
                            OUString(), aContent.Reference, u"metadata-summary"_ustr);
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"大段内容已转为本地对象引用 · 不入原文记录"_ustr);
    LoadArtifactNavigator();
    return true;
}

OUString AIChatPanel::LocalizeArtifactType(const OUString& rType)
{
    if (rType == u"assistant-output"_ustr)
        return u"助手输出"_ustr;
    if (rType == u"apply-plan"_ustr)
        return u"写回计划"_ustr;
    if (rType == u"formatting-preview"_ustr)
        return u"排版预览"_ustr;
    if (rType == u"evidence-record"_ustr)
        return u"证据"_ustr;
    if (rType == u"selection"_ustr)
        return u"选区"_ustr;
    if (rType == u"document-section"_ustr)
        return u"文档段落"_ustr;
    if (rType == u"task-step"_ustr)
        return u"任务步骤"_ustr;
    if (rType == u"local-file"_ustr)
        return u"本地文件"_ustr;
    if (rType == u"plain-text-large"_ustr)
        return u"大段文本"_ustr;
    if (rType == u"structured-text"_ustr)
        return u"结构化文本"_ustr;
    if (rType == u"connector-result"_ustr)
        return u"连接器结果"_ustr;
    if (rType == u"knowledge-index-result"_ustr)
        return u"知识检索"_ustr;
    if (rType == u"review-item"_ustr)
        return u"审核项"_ustr;
    if (rType == u"content-review"_ustr)
        return u"内容审查"_ustr;
    if (rType == u"document-tool-context"_ustr)
        return u"文档工具·骨架"_ustr;
    if (rType == u"document-tool-read"_ustr)
        return u"文档工具·懒读"_ustr;
    if (rType == u"knowledge-index-result"_ustr)
        return u"知识检索"_ustr;
    return rType;
}

OUString AIChatPanel::LocalizeArtifactState(const OUString& rState)
{
    if (rState == u"ready"_ustr)
        return u"就绪"_ustr;
    if (rState == u"in-review"_ustr)
        return u"审查中"_ustr;
    if (rState == u"archived"_ustr)
        return u"已归档"_ustr;
    if (rState == u"failed"_ustr)
        return u"失败"_ustr;
    if (rState == u"applied"_ustr)
        return u"已应用"_ustr;
    return rState;
}

OUString AIChatPanel::LocalizeReviewState(const OUString& rState)
{
    if (rState == u"queued"_ustr)
        return u"排队中"_ustr;
    if (rState == u"open"_ustr)
        return u"审阅中"_ustr;
    if (rState == u"approved"_ustr)
        return u"已批准"_ustr;
    if (rState == u"rejected"_ustr)
        return u"已拒绝"_ustr;
    if (rState == u"applied"_ustr)
        return u"已应用"_ustr;
    if (rState == u"failed"_ustr)
        return u"失败"_ustr;
    if (rState == u"pending"_ustr || rState == u"awaiting-approval"_ustr
        || rState == u"awaiting-review"_ustr)
        return u"待批"_ustr;
    return rState;
}

OUString AIChatPanel::LocalizeReviewItemType(const OUString& rItemType)
{
    if (rItemType == u"content-review"_ustr)
        return u"内容审查"_ustr;
    if (rItemType == u"formatting-review"_ustr)
        return u"排版审查"_ustr;
    if (rItemType == u"task-step"_ustr)
        return u"任务步骤"_ustr;
    if (rItemType == u"apply-plan"_ustr)
        return u"写回计划"_ustr;
    return rItemType;
}

OUString AIChatPanel::LocalizeAgentStepStatus(const OUString& rStatus)
{
    // DuMate-aligned lifecycle: 排队中 → 运行中 → 待批 → 完成 (+ 失败/已停止).
    if (rStatus.isEmpty())
        return u"排队中"_ustr;
    if (rStatus == u"ok"_ustr || rStatus == u"completed"_ustr || rStatus == u"done"_ustr
        || rStatus == u"success"_ustr || rStatus == u"完成"_ustr)
        return u"完成"_ustr;
    // Distinguish queue vs idle wait (both may appear mid pipeline).
    if (rStatus == u"queued"_ustr || rStatus == u"排队中"_ustr || rStatus == u"queue"_ustr)
        return u"排队中"_ustr;
    if (rStatus == u"pending"_ustr || rStatus == u"waiting"_ustr || rStatus == u"idle"_ustr
        || rStatus == u"等待"_ustr)
        return u"排队中"_ustr;
    if (rStatus == u"running"_ustr || rStatus == u"in-progress"_ustr
        || rStatus == u"in_progress"_ustr || rStatus == u"requesting"_ustr
        || rStatus == u"streaming"_ustr || rStatus == u"进行中…"_ustr
        || rStatus == u"运行中"_ustr || rStatus.indexOf(u"进行中"_ustr) >= 0)
        return u"运行中"_ustr;
    if (rStatus == u"failed"_ustr || rStatus == u"error"_ustr || rStatus == u"provider-error"_ustr
        || rStatus == u"失败"_ustr || rStatus == u"timeout"_ustr
        || rStatus == u"provider-timeout"_ustr || rStatus == u"offline"_ustr
        || rStatus == u"provider-offline"_ustr || rStatus == u"unavailable"_ustr)
        return u"失败"_ustr;
    if (rStatus == u"policy-denied"_ustr)
        return u"策略拒绝"_ustr;
    if (rStatus == u"cancelled"_ustr || rStatus == u"canceled"_ustr || rStatus == u"stopped"_ustr
        || rStatus == u"已停止"_ustr)
        return u"已停止"_ustr;
    if (rStatus == u"awaiting-review"_ustr || rStatus == u"awaiting_review"_ustr
        || rStatus == u"awaiting-approval"_ustr || rStatus == u"待批"_ustr
        || rStatus == u"待批准"_ustr || rStatus.indexOf(u"待批"_ustr) >= 0)
        return u"待批"_ustr;
    if (rStatus == u"applied"_ustr || rStatus == u"已写回"_ustr)
        return u"已写回"_ustr;
    if (rStatus == u"skipped"_ustr || rStatus == u"not-run"_ustr || rStatus == u"未运行"_ustr)
        return u"未运行"_ustr;
    if (rStatus == u"awaiting-continue"_ustr || rStatus == u"await-continue"_ustr
        || rStatus == u"待继续"_ustr || rStatus.indexOf(u"待继续"_ustr) >= 0)
        return u"待继续"_ustr;
    // Composite (e.g. "完成 · preview") — pass through if already Chinese-ish.
    return rStatus;
}

OUString AIChatPanel::FormatArtifactRow(const AIChatContentRegistryEntry& rEntry)
{
    // Semantic icon prefix + one-line summary (M3.1 content-object workbench).
    OUString icon = u"◇"_ustr;
    if (rEntry.Type == u"assistant-output"_ustr)
        icon = u"✦"_ustr;
    else if (rEntry.Type == u"apply-plan"_ustr)
        icon = u"✎"_ustr;
    else if (rEntry.Type == u"formatting-preview"_ustr)
        icon = u"▦"_ustr;
    else if (rEntry.Type == u"evidence-record"_ustr)
        icon = u"▣"_ustr;
    else if (rEntry.Type == u"selection"_ustr || rEntry.Type == u"document-section"_ustr)
        icon = u"¶"_ustr;
    else if (rEntry.Type == u"task-step"_ustr)
        icon = u"▹"_ustr;
    else if (rEntry.Type == u"local-file"_ustr)
        icon = u"📎"_ustr;

    const OUString sBadge
        = rEntry.EvidenceId.isEmpty() ? u"no-evidence · 无证据"_ustr : u"evidence · 有证据"_ustr;

    AIChatPreviewMatrix aPreviewMatrix;
    const AIChatPreviewResult aPreview = aPreviewMatrix.BuildPreview(rEntry);
    // Prefer Chinese user message / file kind for list rows (Wave D5).
    OUString summary = aPreview.UserMessage;
    if (summary.isEmpty())
        summary = aPreview.Summary;
    if (summary.isEmpty())
        summary = rEntry.SourceSurface.isEmpty() ? rEntry.ObjectId : rEntry.SourceSurface;
    if (!aPreview.FilePath.isEmpty())
    {
        sal_Int32 nSlash = aPreview.FilePath.lastIndexOf('/');
        const sal_Int32 nBSlash = aPreview.FilePath.lastIndexOf('\\');
        if (nBSlash > nSlash)
            nSlash = nBSlash;
        const OUString sName
            = nSlash >= 0 ? aPreview.FilePath.copy(nSlash + 1) : aPreview.FilePath;
        summary = sName + u" · "_ustr + summary;
    }
    summary = summary.replaceAll(u"\n"_ustr, u" "_ustr);
    if (summary.getLength() > 36)
        summary = summary.copy(0, 36) + u"…"_ustr;

    return icon + u" "_ustr + LocalizeArtifactType(rEntry.Type) + u" · "_ustr
           + LocalizeArtifactState(rEntry.State) + u" · "_ustr + sBadge + u" · "_ustr
           + summary;
}

OUString AIChatPanel::FormatArtifactDetails(const AIChatContentRegistryEntry& rEntry)
{
    AIChatPreviewMatrix aPreviewMatrix;
    const AIChatPreviewResult aPreview = aPreviewMatrix.BuildPreview(rEntry);

    // Chinese UI labels + English metadata tokens locked by preview/provenance harnesses.
    const OUString sPreviewTarget
        = !aPreview.Target.isEmpty() ? aPreview.Target
          : (!rEntry.OpenTarget.isEmpty() ? rEntry.OpenTarget : rEntry.ObjectId);
    const OUString sPreviewSummary
        = !aPreview.Summary.isEmpty() ? aPreview.Summary : u"(none)"_ustr;
    const OUString sSourceMeta
        = u"source="_ustr + rEntry.SourceSurface + u" hash="_ustr
          + (rEntry.HashReference.isEmpty() ? u"-"_ustr : rEntry.HashReference)
          + u" evidence="_ustr
          + (rEntry.EvidenceId.isEmpty() ? u"-"_ustr : rEntry.EvidenceId);

    const OUString sKindZh = AIChatPreviewMatrix::FileKindToLabelZh(aPreview.FileKind);
    OUString sDetails = u"标识："_ustr + rEntry.ObjectId + u"\n"_ustr + u"类型："_ustr
           + LocalizeArtifactType(rEntry.Type) + u"\n"_ustr + u"来源："_ustr
           + rEntry.SourceSurface + u"\n"_ustr + u"状态："_ustr
           + LocalizeArtifactState(rEntry.State) + u"\n"_ustr + u"证据："_ustr
           + (rEntry.EvidenceId.isEmpty() ? u"（无）"_ustr : rEntry.EvidenceId)
           + u"\n"_ustr + u"打开目标："_ustr
           + (rEntry.OpenTarget.isEmpty() ? aPreview.Target : rEntry.OpenTarget) + u"\n"_ustr
           + u"预览模式："_ustr + aPreview.Mode + u"\n"_ustr
           + u"文件类型："_ustr + sKindZh + u" ("_ustr + aPreview.FileKindLabel + u")\n"_ustr
           + u"文件路径："_ustr
           + (aPreview.FilePath.isEmpty() ? u"（无）"_ustr : aPreview.FilePath) + u"\n"_ustr
           + u"预览说明："_ustr
           + (aPreview.UserMessage.isEmpty() ? u"（无）"_ustr : aPreview.UserMessage)
           + u"\n"_ustr + u"预览摘要："_ustr
           + (aPreview.Summary.isEmpty() ? u"（无）"_ustr : aPreview.Summary)
           + u"\nopen-target="_ustr + sPreviewTarget
           + u"\npreview-mode="_ustr + aPreview.Mode
           + u"\npreview-target="_ustr + sPreviewTarget
           + u"\npreview-summary="_ustr + sPreviewSummary
           + u"\nfile-kind="_ustr + aPreview.FileKindLabel
           + u"\nsource-metadata="_ustr
           + (!aPreview.SourceMetadata.isEmpty() ? aPreview.SourceMetadata : sSourceMeta)
           + u"\nsource-id="_ustr + rEntry.ObjectId
           + u"\ncitation-id=citation:"_ustr + rEntry.ObjectId
           + u"\nevidence-id="_ustr
           + (rEntry.EvidenceId.isEmpty() ? u"-"_ustr : rEntry.EvidenceId);

    // Wave D5: MD/plain text → inline body teaser (no document open on select).
    if (aPreview.FileKind == AIChatPreviewFileKind::Text
        || aPreview.FileKind == AIChatPreviewFileKind::Markdown)
    {
        OUString sBody;
        OUString sDetail;
        if (AIChatContentOpener::LoadTextPreview(rEntry, aPreview, sBody, sDetail)
            && !sBody.isEmpty())
        {
            if (sBody.getLength() > 800)
                sBody = sBody.copy(0, 800) + u"…"_ustr;
            sDetails += u"\n\n—— 预览正文 ——\n"_ustr + sBody;
        }
        else if (!sDetail.isEmpty())
        {
            sDetails += u"\n\n提示："_ustr + sDetail;
        }
    }
    else if (aPreview.FileKind == AIChatPreviewFileKind::Image)
    {
        sDetails += u"\n\n提示：点「打开」用本地应用查看图片（不上传）。"_ustr;
    }
    else if (aPreview.FileKind == AIChatPreviewFileKind::Unsupported)
    {
        sDetails += u"\n\n提示：暂不支持内嵌预览，仍可点「打开」尝试用本地应用打开。"_ustr;
    }
    else if (aPreview.FileKind == AIChatPreviewFileKind::OfficeDocument)
    {
        sDetails += u"\n\n提示：点「打开」将通过本地应用只读预览（PDF/DOCX/ODT 等）。"_ustr;
    }

    return sDetails;
}

void AIChatPanel::LoadArtifactNavigator()
{
    m_bWorkspaceDataLoaded = true;
    AIChatContentRegistry aRegistry;
    m_aArtifacts = aRegistry.LoadEntries();

    if (m_xArtifactTree)
    {
        m_xArtifactTree->clear();
        for (const auto& rEntry : m_aArtifacts)
            m_xArtifactTree->append(rEntry.ObjectId, FormatArtifactRow(rEntry));

        if (!m_aArtifacts.empty())
            m_xArtifactTree->select(0);
    }

    UpdateArtifactDetails();
}

void AIChatPanel::PushAgentStepRow(const OUString& rStep, const OUString& rStatus)
{
    const OUString sStatus = LocalizeAgentStepStatus(rStatus);
    m_aAgentStepCache.emplace_back(rStep, sStatus);
    if (!m_xAgentTree)
        return;
    m_xAgentTree->append(rStep, rStep);
    const int nRow = m_xAgentTree->n_children() - 1;
    if (nRow >= 0)
        m_xAgentTree->set_text(nRow, sStatus, 1);
    if (m_xAgentEmptyLabel)
        m_xAgentEmptyLabel->set_visible(false);
}

void AIChatPanel::ClearAgentContinueGate()
{
    m_bAgentAwaitingContinue = false;
    m_sAgentGateGoal.clear();
    m_sAgentGateContext.clear();
    m_sAgentGateSurface.clear();
    m_sAgentGateDocTools.clear();
    m_sAgentGatePlanContent.clear();
    if (m_xAgentContinueBtn)
        m_xAgentContinueBtn->set_sensitive(false);
}

void AIChatPanel::MarkAgentStepsStopped()
{
    // Active lifecycle rows become 「已停止」; 完成 / 失败 / 待批 keep state.
    for (auto& rRow : m_aAgentStepCache)
    {
        const OUString s = LocalizeAgentStepStatus(rRow.second);
        if (s == u"运行中"_ustr || s == u"排队中"_ustr || s == u"等待"_ustr
            || s == u"进行中…"_ustr)
            rRow.second = u"已停止"_ustr;
    }
    LoadAgentSteps();
    SetAgentStepBar(u"步骤：已停止"_ustr);
}

void AIChatPanel::LoadAgentSteps()
{
    if (!m_xAgentTree)
        return;

    m_xAgentTree->clear();
    for (const auto& rRow : m_aAgentStepCache)
    {
        m_xAgentTree->append(rRow.first, rRow.first);
        const int nRow = m_xAgentTree->n_children() - 1;
        if (nRow >= 0)
            m_xAgentTree->set_text(nRow, LocalizeAgentStepStatus(rRow.second), 1);
    }

    const bool bEmpty = m_aAgentStepCache.empty();
    if (m_xAgentEmptyLabel)
    {
        m_xAgentEmptyLabel->set_visible(bEmpty);
        if (bEmpty)
            m_xAgentEmptyLabel->set_label(
                u"暂无任务。\n"
                "① 在「对话」勾选「多步任务」后发送，或点「运行任务」\n"
                "② 步骤状态六态：排队中 · 运行中 · 待批 · 完成 · 失败 · 已停止\n"
                "③ 失败或停止时主文档不变，可改指令后重试"_ustr);
    }
    if (!bEmpty)
        m_xAgentTree->select(0);
}

namespace
{
sal_Int64 ScheduleNowMs()
{
    TimeValue tv{};
    osl_getSystemTime(&tv);
    return static_cast<sal_Int64>(tv.Seconds) * 1000
           + static_cast<sal_Int64>(tv.Nanosec) / 1000000;
}

OUString FormatSchedulePlanLabel(const kqoffice::ai::cowork::ScheduledTask& t, sal_Int64 nowMs)
{
    using kqoffice::ai::cowork::ScheduleKind;
    using kqoffice::ai::cowork::ScheduledTaskStore;
    OUString plan = ScheduledTaskStore::kindLabelZh(t.kind);
    switch (t.kind)
    {
        case ScheduleKind::Once:
            if (t.intervalMinutes > 0)
                plan += u" · "_ustr + OUString::number(t.intervalMinutes) + u"分钟后"_ustr;
            if (t.nextRunAtMs > nowMs)
            {
                const sal_Int64 sec = (t.nextRunAtMs - nowMs) / 1000;
                if (sec < 120)
                    plan += u" · "_ustr + OUString::number(sec) + u"秒后"_ustr;
                else
                    plan += u" · 约"_ustr + OUString::number(sec / 60) + u"分后"_ustr;
            }
            break;
        case ScheduleKind::IntervalMinutes:
            plan += u" · 每"_ustr + OUString::number(t.intervalMinutes) + u"分钟"_ustr;
            break;
        case ScheduleKind::DailyAt:
        {
            OUString hh = OUString::number(t.dailyHour);
            OUString mm = OUString::number(t.dailyMinute);
            if (hh.getLength() < 2)
                hh = u"0"_ustr + hh;
            if (mm.getLength() < 2)
                mm = u"0"_ustr + mm;
            plan += u" · "_ustr + hh + u":"_ustr + mm;
            break;
        }
    }
    return plan;
}

bool WritePendingPromptInject(const OUString& rText)
{
    const char* home = std::getenv("HOME");
    if (!home || !*home || rText.isEmpty())
        return false;
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
        return false;
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
        return false;
    f.setSize(0);
    const OString utf8 = OUStringToOString(rText, RTL_TEXTENCODING_UTF8);
    sal_uInt64 n = 0;
    f.write(utf8.getStr(), utf8.getLength(), n);
    f.close();
    return n > 0 || utf8.isEmpty();
}
} // namespace

void AIChatPanel::LoadScheduleList()
{
    if (!m_xScheduleTree)
        return;

    m_xScheduleTree->clear();
    kqoffice::ai::cowork::ScheduledTaskStore store;
    const sal_Int64 nowMs = ScheduleNowMs();
    const auto tasks = store.list();
    for (const auto& t : tasks)
    {
        const OUString title = t.titleZh.isEmpty() ? t.id : t.titleZh;
        m_xScheduleTree->append(t.id, title);
        const int nRow = m_xScheduleTree->n_children() - 1;
        if (nRow < 0)
            continue;
        m_xScheduleTree->set_text(nRow, FormatSchedulePlanLabel(t, nowMs), 1);
        m_xScheduleTree->set_text(
            nRow, kqoffice::ai::cowork::ScheduledTaskStore::statusLabelZh(t, nowMs), 2);
        m_xScheduleTree->set_text(
            nRow, t.lastResultZh.isEmpty() ? u"—"_ustr : t.lastResultZh, 3);
    }
    if (!tasks.empty())
    {
        m_xScheduleTree->select(0);
        FillScheduleEditorFromSelected();
    }
}

OUString AIChatPanel::GetSelectedScheduleId() const
{
    if (!m_xScheduleTree)
        return OUString();
    const int nSel = m_xScheduleTree->get_selected_index();
    if (nSel < 0)
        return OUString();
    return m_xScheduleTree->get_id(nSel);
}

void AIChatPanel::FillScheduleEditorFromSelected()
{
    const OUString id = GetSelectedScheduleId();
    if (id.isEmpty())
        return;
    kqoffice::ai::cowork::ScheduledTaskStore store;
    kqoffice::ai::cowork::ScheduledTask t;
    if (!store.get(id, t))
        return;

    using kqoffice::ai::cowork::ScheduleKind;
    if (m_xScheduleKind)
    {
        switch (t.kind)
        {
            case ScheduleKind::IntervalMinutes:
                m_xScheduleKind->set_active_id(u"interval"_ustr);
                break;
            case ScheduleKind::DailyAt:
                m_xScheduleKind->set_active_id(u"daily"_ustr);
                break;
            case ScheduleKind::Once:
            default:
                m_xScheduleKind->set_active_id(u"once"_ustr);
                break;
        }
    }
    if (m_xScheduleInterval)
    {
        sal_Int32 mins = t.intervalMinutes;
        if (t.kind == ScheduleKind::Once)
        {
            // Prefer stored delay; else derive remaining minutes from nextRunAtMs.
            if (mins <= 0)
            {
                const sal_Int64 nowMs = ScheduleNowMs();
                if (t.nextRunAtMs > nowMs)
                {
                    const sal_Int64 remMin = (t.nextRunAtMs - nowMs + 59999) / 60000;
                    mins = static_cast<sal_Int32>(std::max<sal_Int64>(1, remMin));
                }
                else
                    mins = 1;
            }
        }
        else if (mins <= 0)
            mins = 30;
        m_xScheduleInterval->set_text(OUString::number(mins));
    }
    if (m_xScheduleHour)
        m_xScheduleHour->set_text(OUString::number(t.dailyHour));
    if (m_xScheduleMinute)
        m_xScheduleMinute->set_text(OUString::number(t.dailyMinute));
}

bool AIChatPanel::ReadScheduleEditorFields(kqoffice::ai::cowork::ScheduleKind& rKind,
                                           sal_Int32& rIntervalMinutes, sal_Int32& rDailyHour,
                                           sal_Int32& rDailyMinute, OUString& rErrorZh) const
{
    using kqoffice::ai::cowork::ScheduleKind;
    OUString kindId = u"once"_ustr;
    if (m_xScheduleKind)
        kindId = m_xScheduleKind->get_active_id();
    if (kindId == u"interval"_ustr)
        rKind = ScheduleKind::IntervalMinutes;
    else if (kindId == u"daily"_ustr)
        rKind = ScheduleKind::DailyAt;
    else
        rKind = ScheduleKind::Once;

    // Defaults: Once = 1 minute later; Interval = every 30 minutes.
    rIntervalMinutes = (rKind == ScheduleKind::Once) ? 1 : 30;
    if (m_xScheduleInterval)
    {
        const OUString s = m_xScheduleInterval->get_text().trim();
        if (!s.isEmpty())
            rIntervalMinutes = s.toInt32();
    }
    rDailyHour = 9;
    if (m_xScheduleHour)
    {
        const OUString s = m_xScheduleHour->get_text().trim();
        if (!s.isEmpty())
            rDailyHour = s.toInt32();
    }
    rDailyMinute = 0;
    if (m_xScheduleMinute)
    {
        const OUString s = m_xScheduleMinute->get_text().trim();
        if (!s.isEmpty())
            rDailyMinute = s.toInt32();
    }

    if (rKind == ScheduleKind::Once || rKind == ScheduleKind::IntervalMinutes)
    {
        if (rIntervalMinutes < 1 || rIntervalMinutes > 10080)
        {
            rErrorZh = (rKind == ScheduleKind::Once)
                           ? u"单次延迟分钟须在 1–10080 之间"_ustr
                           : u"间隔分钟须在 1–10080 之间"_ustr;
            return false;
        }
    }
    if (rKind == ScheduleKind::DailyAt)
    {
        if (rDailyHour < 0 || rDailyHour > 23 || rDailyMinute < 0 || rDailyMinute > 59)
        {
            rErrorZh = u"每日时间须为 0–23 时、0–59 分"_ustr;
            return false;
        }
    }
    return true;
}

bool AIChatPanel::DispatchScheduledTaskNow(const OUString& rId)
{
    if (rId.isEmpty())
        return false;
    kqoffice::ai::cowork::ScheduledTaskStore store;
    kqoffice::ai::cowork::ScheduledTask t;
    if (!store.get(rId, t))
        return false;
    const OUString prompt = t.promptOrScenarioId.trim();
    if (prompt.isEmpty())
    {
        store.markRun(rId, false, u"无提示内容，未触发"_ustr, ScheduleNowMs());
        return false;
    }
    // Prefix so inject poll / transcript can attribute source as schedule.
    const OUString inject
        = u"【可圈定时任务】"_ustr + t.titleZh + u"\n"_ustr + prompt;
    if (!WritePendingPromptInject(inject))
    {
        store.markRun(rId, false, u"注入失败"_ustr, ScheduleNowMs());
        return false;
    }
    store.markRun(rId, true, u"已立即触发（注入可圈 AI）"_ustr, ScheduleNowMs());
    return true;
}

bool AIChatPanel::IsRunBusy() const
{
    return m_eState == AIChatPanelState::Requesting
           || m_eState == AIChatPanelState::Streaming || m_bAgentRunActive
           || kqoffice::ai::cowork::hasActiveTaskRunner();
}

void AIChatPanel::StopActiveRun(const OUString& rUserStatus, bool bFromAppend)
{
    m_bCancelRequested = true;
    m_bAgentRunActive = false;

    // Cancel cowork TaskRunner if one is bound to the UI.
    if (kqoffice::ai::cowork::hasActiveTaskRunner())
        kqoffice::ai::cowork::cancelActiveTaskRunner();

    ClearPendingPlan();
    MarkAgentStepsStopped();
    m_sLastOutcomeDetail = ShortenUserDetail(rUserStatus);

    if (!bFromAppend)
    {
        AppendTerminalEvidence(u"cancelled"_ustr, OUString());
        SetState(AIChatPanelState::Cancelled);
    }

    if (m_xStatusLabel)
        m_xStatusLabel->set_label(rUserStatus + u" · 主文档未改"_ustr);
    AppendTranscript(u"System"_ustr, rUserStatus + u" · 主文档未改"_ustr,
                     /*bPersistHistory*/ false);
}

void AIChatPanel::LoadReviewQueue()
{
    if (!m_xReviewTree)
        return;

    m_xReviewTree->clear();
    sal_Int32 nCount = 0;

    // Session pending ApplyPlan always surfaces first in the 审核 tab.
    if (m_bHasPendingPlan)
    {
        const OUString sPlanId = m_aPendingPlan.planId.isEmpty() ? u"pending-plan"_ustr
                                                                  : m_aPendingPlan.planId;
        const OUString sItem
            = u"待批准写回 · "_ustr + sPlanId + u" · "_ustr
              + OUString::number(static_cast<sal_Int32>(m_aPendingPlan.operations.size()))
              + u" 步"_ustr;
        m_xReviewTree->append(sPlanId, sItem);
        const int nRow = m_xReviewTree->n_children() - 1;
        if (nRow >= 0)
            m_xReviewTree->set_text(nRow, u"待批"_ustr, 1);
        ++nCount;
    }

    if (m_xReviewQueueStore)
    {
        const std::vector<AIChatReviewQueueEntry> aEntries = m_xReviewQueueStore->LoadEntries();
        for (const auto& rEntry : aEntries)
        {
            if (m_bHasPendingPlan && rEntry.ReviewId == m_aPendingPlan.planId)
                continue;
            const OUString sItem = LocalizeReviewItemType(rEntry.ItemType) + u" · "_ustr
                                   + rEntry.ReviewId;
            m_xReviewTree->append(rEntry.ReviewId, sItem);
            const int nRow = m_xReviewTree->n_children() - 1;
            if (nRow >= 0)
                m_xReviewTree->set_text(nRow, LocalizeReviewState(rEntry.State), 1);
            ++nCount;
        }
    }

    if (m_xReviewEmptyLabel)
    {
        m_xReviewEmptyLabel->set_visible(nCount == 0);
        if (nCount == 0)
            m_xReviewEmptyLabel->set_label(
                u"暂无待审项。\n"
                "① 在「对话」发送指令并生成建议\n"
                "② 待批计划会出现在此列表\n"
                "③ 点「批准写回」或「拒绝」（主文档默认不改）\n"
                "④ 「查看 Diff」打开同一差异审阅\n"
                "也可在「产物」页点「审查」加入队列。"_ustr);
    }
    if (nCount > 0)
        m_xReviewTree->select(0);
}

void AIChatPanel::RegisterAssistantArtifact(const OUString& rContent, const OUString& rEvidenceId,
                                            const OUString& rSourceKind)
{
    if (rContent.isEmpty())
        return;

    AIChatContentRegistryEntry aEntry;
    aEntry.ObjectId
        = u"art:"_ustr
          + (rEvidenceId.isEmpty() ? OUString::number(static_cast<sal_Int64>(rContent.hashCode()))
                                   : rEvidenceId);
    aEntry.Type = u"assistant-output"_ustr;
    aEntry.SourceSurface = rSourceKind.isEmpty() ? u"chat"_ustr : rSourceKind;
    aEntry.State = u"ready"_ustr;
    aEntry.EvidenceId = rEvidenceId.isEmpty() ? aEntry.ObjectId : rEvidenceId;
    aEntry.HashReference = u"hash:"_ustr + OUString::number(rContent.hashCode());
    aEntry.OpenTarget = u"sidebar-preview"_ustr;
    aEntry.PreviewMode = u"read-only-preview"_ustr;

    AIChatContentRegistry aRegistry;
    if (!aRegistry.RegisterObject(aEntry))
    {
        AppendTranscript(u"System"_ustr,
                         u"content-register-failed id="_ustr + aEntry.ObjectId,
                         /*bPersistHistory*/ false);
        return;
    }

    // Seed review queue so 审核 tab is not empty after a successful reply.
    if (m_xReviewQueueStore)
        m_xReviewQueueStore->EnqueueFromRegistry(aEntry);

    LoadArtifactNavigator();
    LoadReviewQueue();
    AppendTranscript(u"System"_ustr,
                     u"已登记生成内容 →「内容」页可打开/审查 · id="_ustr + aEntry.ObjectId,
                     /*bPersistHistory*/ false);
}

void AIChatPanel::RegisterLocalFileArtifact(const OUString& rPath, const OUString& rSourceKind)
{
    OUString path = rPath.trim();
    if (path.isEmpty())
        return;
    if (path.startsWith(u"file:"_ustr))
    {
        OUString sys;
        if (osl::FileBase::getSystemPathFromFileURL(path, sys) == osl::FileBase::E_None)
            path = sys;
    }
    if (path.isEmpty())
        return;

    AIChatContentRegistryEntry aEntry;
    aEntry.ObjectId = u"file:"_ustr + OUString::number(static_cast<sal_Int64>(path.hashCode()));
    aEntry.Type = u"local-file"_ustr;
    aEntry.SourceSurface = path; // preview matrix resolves path from SourceSurface
    aEntry.State = u"ready"_ustr;
    aEntry.EvidenceId = aEntry.ObjectId;
    aEntry.HashReference = u"path:"_ustr + path;
    aEntry.OpenTarget = AIChatPreviewMatrix::ResolvePreviewTarget(aEntry);
    aEntry.PreviewMode = AIChatPreviewMatrix::ResolvePreviewMode(aEntry);

    AIChatContentRegistry aRegistry;
    if (!aRegistry.RegisterObject(aEntry))
    {
        AppendTranscript(u"System"_ustr,
                         u"file-register-failed path="_ustr + path,
                         /*bPersistHistory*/ false);
        return;
    }
    LoadArtifactNavigator();
    const AIChatPreviewFileKind eKind = AIChatPreviewMatrix::DetectFileKind(path, aEntry);
    AppendTranscript(u"System"_ustr,
                     u"已登记本地文件 →「内容」页可预览 · "_ustr
                         + AIChatPreviewMatrix::FileKindToLabelZh(eKind) + u" · "_ustr + path
                         + u" · source="_ustr
                         + (rSourceKind.isEmpty() ? u"attach"_ustr : rSourceKind),
                     /*bPersistHistory*/ false);
}

OUString AIChatPanel::GetSelectedArtifactId() const
{
    if (!m_xArtifactTree)
        return OUString();
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
    auto setSens = [](const std::unique_ptr<weld::Button>& p, bool b) {
        if (p)
            p->set_sensitive(b);
    };

    setSens(m_xOpenArtifactButton,
            AIChatWorkspaceActionBarStore::IsCommandEnabled(u"open-preview"_ustr, pSelected, bBusy,
                                                            bHasRetryPrompt));
    setSens(m_xOpenDiffReviewButton,
            AIChatWorkspaceActionBarStore::IsCommandEnabled(u"open-diff-review"_ustr, pSelected,
                                                            bBusy, bHasRetryPrompt));
    setSens(m_xReviewArtifactButton,
            bHasSelection && AIChatContentReviewStore::IsSupportedSourceType(pSelected->Type));
    setSens(m_xFormatArtifactButton,
            bHasSelection
                && AIChatFormattingReviewStore::IsSupportedFormattingScope(pSelected->Type));
    setSens(m_xInspectEvidenceButton,
            AIChatWorkspaceActionBarStore::IsCommandEnabled(u"export-evidence"_ustr, pSelected,
                                                            bBusy, bHasRetryPrompt)
                && bHasSelection
                && AIChatEvidenceInspector::IsSupportedSourceType(pSelected->Type));
    // Pending chat ApplyPlan can be approved/rejected without a review-queue row.
    const bool bPendingPlanReady = m_bHasPendingPlan && !bBusy;
    setSens(m_xApproveSelectedButton,
            bPendingPlanReady
                || AIChatWorkspaceActionBarStore::IsCommandEnabled(
                    u"approve-selected"_ustr, pSelected, bBusy, bHasRetryPrompt));
    setSens(m_xRejectSelectedButton,
            bPendingPlanReady
                || AIChatWorkspaceActionBarStore::IsCommandEnabled(
                    u"reject-selected"_ustr, pSelected, bBusy, bHasRetryPrompt));
    setSens(m_xCopyReferenceButton,
            AIChatWorkspaceActionBarStore::IsCommandEnabled(u"copy-reference"_ustr, pSelected,
                                                            bBusy, bHasRetryPrompt));
    setSens(m_xExportEvidenceButton,
            AIChatWorkspaceActionBarStore::IsCommandEnabled(u"export-evidence"_ustr, pSelected,
                                                            bBusy, bHasRetryPrompt));
    setSens(m_xFilterWorkspaceButton,
            AIChatWorkspaceActionBarStore::IsCommandEnabled(u"filter"_ustr, pSelected, bBusy,
                                                            bHasRetryPrompt));
    setSens(m_xSortWorkspaceButton,
            AIChatWorkspaceActionBarStore::IsCommandEnabled(u"sort"_ustr, pSelected, bBusy,
                                                            bHasRetryPrompt));
    setSens(m_xRemoveArtifactButton, bHasSelection);

    if (!bHasSelection)
    {
        if (m_xArtifactDetailsLabel)
            m_xArtifactDetailsLabel->set_label(
                m_aArtifacts.empty()
                    ? u"暂无生成内容。发送 AI 成功后产物会出现在此列表，可打开、审查或加入审核队列。"_ustr
                    : u"请选择一项生成内容查看详情。"_ustr);
        return;
    }

    if (m_xArtifactDetailsLabel)
        m_xArtifactDetailsLabel->set_label(FormatArtifactDetails(*pSelected));
}

void AIChatPanel::OpenSelectedArtifact()
{
    const AIChatContentRegistryEntry* pSelected = FindSelectedArtifact();
    if (!pSelected)
    {
        AppendTranscript(u"System"_ustr,
                         u"open-failed reason=missing-registry-entry id="_ustr
                             + GetSelectedArtifactId() + u" · 请先选择一项生成内容"_ustr);
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"Open failed: 未选择生成内容"_ustr);
        return;
    }

    if (!DispatchWorkspaceAction(u"open-preview"_ustr))
        return;
    AIChatContentOpener aOpener;
    const AIChatContentOpenResult aResult = aOpener.OpenReadOnlyPreview(*pSelected);
    AppendTranscript(u"System"_ustr, aResult.Message);

    // Wave D5: Chinese status + side-panel details for text/markdown body.
    OUString sStatus;
    if (aResult.Success)
    {
        if (!aResult.UserMessage.isEmpty())
            sStatus = aResult.UserMessage;
        else if (aResult.FileKind == AIChatPreviewFileKind::OfficeDocument)
            sStatus = u"已用本地应用打开预览"_ustr;
        else if (aResult.FileKind == AIChatPreviewFileKind::Text
                 || aResult.FileKind == AIChatPreviewFileKind::Markdown)
            sStatus = u"已在侧栏打开只读预览"_ustr;
        else
            sStatus = u"已打开预览 · "_ustr + pSelected->ObjectId;
    }
    else
    {
        sStatus = aResult.UserMessage.isEmpty()
                      ? (u"打开失败 · "_ustr + pSelected->ObjectId)
                      : aResult.UserMessage;
    }
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(sStatus);

    if (m_xArtifactDetailsLabel)
    {
        OUString sDetails = FormatArtifactDetails(*pSelected);
        if (!aResult.PreviewText.isEmpty())
        {
            // Full body from open path (may exceed selection-teaser length).
            const sal_Int32 nMarker = sDetails.indexOf(u"—— 预览正文 ——"_ustr);
            if (nMarker >= 0)
                sDetails = sDetails.copy(0, nMarker);
            sDetails += u"\n\n—— 预览正文 ——\n"_ustr + aResult.PreviewText;
        }
        else if (!aResult.UserMessage.isEmpty())
        {
            sDetails += u"\n\n打开结果："_ustr + aResult.UserMessage;
        }
        m_xArtifactDetailsLabel->set_label(sDetails);
    }

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
        m_xStatusLabel->set_label(u"加入审核失败："_ustr + sSelectedId);
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

    m_xStatusLabel->set_label(u"已加入审核队列："_ustr + aReview.Review.ReviewId);
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
    LoadReviewQueue();
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"已加入审核队列: "_ustr + aReview.Review.ReviewId);
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
        m_xStatusLabel->set_label(u"排版审查失败："_ustr + sSelectedId);
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

    m_xStatusLabel->set_label(u"排版审查已入队："_ustr + aReview.Review.ReviewId);
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
    LoadReviewQueue();
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"排版审查已入队: "_ustr + aReview.Review.ReviewId);
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
    m_xStatusLabel->set_label(aInspection.Success ? u"已查看证据："_ustr + pSelected->ObjectId
                                                  : u"证据查看失败："_ustr + pSelected->ObjectId);
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

    const bool bBusy = IsRunBusy();
    const AIChatContentRegistryEntry* pSelected = FindSelectedArtifact();
    const AIChatWorkspaceActionBarDispatchResult aResult
        = m_xWorkspaceActionBarStore->DispatchCommand(rCommand, pSelected, bBusy,
                                                      !m_sLastPrompt.isEmpty());
    AppendTranscript(u"System"_ustr, aResult.Message);

    if (!aResult.Success)
    {
        m_xStatusLabel->set_label(u"工作台动作失败："_ustr + rCommand);
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

    m_xStatusLabel->set_label(u"工作台动作："_ustr + rCommand);
    if (rCommand == u"filter"_ustr)
        m_xStatusLabel->set_label(u"筛选可见：状态 / 类型 / 表面"_ustr);
    else if (rCommand == u"sort"_ustr)
        m_xStatusLabel->set_label(u"排序：最近优先"_ustr);
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
    if (!m_xPromptEntry)
        return;
    m_xPromptEntry->grab_focus();
    m_xPromptEntry->set_position(-1);
}

void AIChatPanel::UpdateActions()
{
    if (!m_xPromptEntry)
        return;
    const bool bHasPrompt = !m_xPromptEntry->get_text().trim().isEmpty();
    const bool bBusy = IsRunBusy();
    // DuMate: keep composer editable while running so user can append/replace task.
    m_xPromptEntry->set_sensitive(!bBusy);
    if (m_xSendButton)
        m_xSendButton->set_sensitive(bHasPrompt && !bBusy);
    if (m_xCancelButton)
        m_xCancelButton->set_sensitive(bBusy);
    if (m_xRetryButton)
        m_xRetryButton->set_sensitive(!m_sLastPrompt.isEmpty() && !bBusy);
    if (m_xRetryButton)
    {
        if (m_bStaleNeedsRegen)
        {
            m_xRetryButton->set_label(u"重新生成"_ustr);
            m_xRetryButton->set_tooltip_text(
                u"文档已变导致计划过期：按当前文档重跑上一条指令（不自动写回）"_ustr);
        }
        else
        {
            m_xRetryButton->set_label(u"重试"_ustr);
            m_xRetryButton->set_tooltip_text(u"恢复上一条指令以便重试。"_ustr);
        }
    }
    if (m_xClearHistoryButton)
        m_xClearHistoryButton->set_sensitive(!bBusy);
    // Intent chips stay clickable when idle/awaiting; disabled while busy.
    auto setIntent = [&](weld::Button* p) {
        if (p)
            p->set_sensitive(!bBusy);
    };
    setIntent(m_xIntentRewriteBtn.get());
    setIntent(m_xIntentFormalBtn.get());
    setIntent(m_xIntentShortenBtn.get());
    setIntent(m_xIntentExpandBtn.get());
    setIntent(m_xIntentSummarizeBtn.get());
    setIntent(m_xIntentOutlineBtn.get());
    setIntent(m_xIntentProofreadBtn.get());
    setIntent(m_xIntentContinueBtn.get());
    setIntent(m_xIntentPlanBtn.get());
    setIntent(m_xIntentAgentBtn.get());
    UpdateComposerChrome();
    UpdateActivityCard();
    UpdateApprovalChrome();
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

OUString AIChatPanel::StateToUserLabel(AIChatPanelState eState)
{
    // Product-facing Chinese narrative — never show "State: idle" to users.
    switch (eState)
    {
        case AIChatPanelState::Idle:
            return u"就绪"_ustr;
        case AIChatPanelState::Requesting:
            return u"请求中…"_ustr;
        case AIChatPanelState::Streaming:
            return u"生成中…"_ustr;
        case AIChatPanelState::AwaitingRuntime:
            return u"等待运行时…"_ustr;
        case AIChatPanelState::AwaitingApproval:
            return u"待批准写回"_ustr;
        case AIChatPanelState::Applied:
            return u"已写回（可撤销）"_ustr;
        case AIChatPanelState::Failed:
            return u"失败 · 主文档未改"_ustr;
        case AIChatPanelState::Cancelled:
            return u"已停止"_ustr;
    }
    return u"就绪"_ustr;
}

OUString AIChatPanel::DetectComposerIntent(const OUString& rPrompt)
{
    const OUString lower = rPrompt.toAsciiLowerCase();
    if (lower.indexOf(u"多步"_ustr) >= 0 || lower.indexOf(u"协作"_ustr) >= 0
        || lower.indexOf(u"agent"_ustr) >= 0 || lower.startsWith(u"/agent"_ustr)
        || lower.indexOf(u"子代理"_ustr) >= 0 || lower.indexOf(u"cowork"_ustr) >= 0)
        return u"agent"_ustr;
    if (lower.indexOf(u"规划"_ustr) >= 0 || lower.indexOf(u"计划"_ustr) >= 0
        || lower.indexOf(u"plan"_ustr) >= 0 || lower.startsWith(u"/plan"_ustr)
        || lower.indexOf(u"大纲"_ustr) >= 0)
        return u"plan"_ustr;
    if (lower.indexOf(u"总结"_ustr) >= 0 || lower.indexOf(u"摘要"_ustr) >= 0
        || lower.indexOf(u"summar"_ustr) >= 0)
        return u"summarize"_ustr;
    if (lower.indexOf(u"扩写"_ustr) >= 0 || lower.indexOf(u"展开"_ustr) >= 0
        || lower.indexOf(u"expand"_ustr) >= 0 || lower.indexOf(u"longer"_ustr) >= 0)
        return u"expand"_ustr;
    if (lower.indexOf(u"精简"_ustr) >= 0 || lower.indexOf(u"缩短"_ustr) >= 0
        || lower.indexOf(u"短一点"_ustr) >= 0 || lower.indexOf(u"shorten"_ustr) >= 0
        || lower.indexOf(u"condense"_ustr) >= 0)
        return u"shorten"_ustr;
    if (lower.indexOf(u"改写"_ustr) >= 0 || lower.indexOf(u"润色"_ustr) >= 0
        || lower.indexOf(u"rewrite"_ustr) >= 0 || lower.indexOf(u"polish"_ustr) >= 0
        || lower.indexOf(u"翻译"_ustr) >= 0 || lower.indexOf(u"translate"_ustr) >= 0)
        return u"rewrite"_ustr;
    return u"chat"_ustr;
}

void AIChatPanel::UpdateActivityCard()
{
    if (!m_xActivityCard)
        return;

    OUString intent = u"chat"_ustr;
    if (m_xPromptEntry)
        intent = DetectComposerIntent(m_xPromptEntry->get_text());
    if (!m_sForcedCapability.isEmpty())
        intent = m_sForcedCapability;

    OUString intentZh = u"对话"_ustr;
    if (intent == u"rewrite"_ustr)
        intentZh = u"改写"_ustr;
    else if (intent == u"shorten"_ustr || intent == u"summarize"_ustr)
        intentZh = intent == u"shorten"_ustr ? u"精简"_ustr : u"总结"_ustr;
    else if (intent == u"expand"_ustr)
        intentZh = u"扩写"_ustr;
    else if (intent == u"plan"_ustr)
        intentZh = u"规划"_ustr;
    else if (intent == u"agent"_ustr)
        intentZh = u"多步协作"_ustr;
    else if (intent == u"review"_ustr)
        intentZh = u"审查"_ustr;

    const bool bPending = m_bHasPendingPlan;
    OUString card;
    switch (m_eState)
    {
        case AIChatPanelState::Idle:
            if (bPending)
                card = u"活动：有待批准计划 · 到「审核」批准写回，或继续输入新指令"_ustr;
            else if (CurrentDocumentSurface() == u"impress"_ustr)
                card = u"活动：演示设计流 · ①大纲 → ②多方案 → ③选一写回（须批准）→ ④导出 PPTX"_ustr;
            else if (CurrentDocumentSurface() == u"calc"_ustr)
                card = u"活动：表格 · 选区后点「公式/解释/汇总」· 公式须批准才写入"_ustr;
            else
                card = u"活动：待命 · 意图「"_ustr + intentZh
                       + u"」· 选中文字后点芯片，或直接输入 · 「问本文档」可本地检索"_ustr;
            break;
        case AIChatPanelState::Requesting:
            card = u"活动：正在请求模型（"_ustr + intentZh
                   + u"）· 不会改动主文档 · 可点「停止」"_ustr;
            break;
        case AIChatPanelState::Streaming:
            card = u"活动：正在生成（"_ustr + intentZh
                   + u"）· 流式输出中 · 主文档仍未改"_ustr;
            break;
        case AIChatPanelState::AwaitingRuntime:
            card = u"活动：等待运行时就绪…"_ustr;
            break;
        case AIChatPanelState::AwaitingApproval:
            card = u"活动：已生成建议 · 待你批准后写回 · 可「查看 Diff / 批准写回 / 拒绝」"_ustr;
            break;
        case AIChatPanelState::Applied:
            card = CurrentDocumentSurface() == u"impress"_ustr
                       ? u"活动：已写回幻灯 · 可撤销 · 下一步「④导出」生成 PPTX"_ustr
                       : u"活动：已写回文档 · 可用撤销恢复 · 可继续改写"_ustr;
            break;
        case AIChatPanelState::Failed:
            if (m_bStaleNeedsRegen)
                card = u"活动：计划过期 · 主文档未改 · 点「重新生成」按当前文档再跑"_ustr;
            else if (!m_sLastOutcomeDetail.isEmpty())
                card = u"活动：失败 · "_ustr + m_sLastOutcomeDetail
                       + u" · 主文档未改 · 可改指令后重试"_ustr;
            else
                card = u"活动：失败 · 主文档未改 · 可改指令后重试"_ustr;
            break;
        case AIChatPanelState::Cancelled:
            if (!m_sLastOutcomeDetail.isEmpty())
                card = u"活动：已停止 · "_ustr + m_sLastOutcomeDetail
                       + u" · 主文档未改 · 可输入新任务继续"_ustr;
            else
                card = u"活动：已停止 · 主文档未改 · 可输入新任务继续"_ustr;
            break;
    }
    m_xActivityCard->set_label(card);
    // Keep machine token in tooltip for harness/debug (state=idle|…).
    m_xActivityCard->set_tooltip_text(u"state="_ustr + StateToLabel(m_eState)
                                      + u" intent="_ustr + intent);
}

void AIChatPanel::UpdateComposerChrome()
{
    if (!m_xPromptEntry)
        return;
    const OUString intent = DetectComposerIntent(m_xPromptEntry->get_text());
    const OUString surface = CurrentDocumentSurface();

    // M4: surface-adaptive intent chip labels (same chrome, different language).
    if (surface == u"calc"_ustr)
    {
        if (m_xIntentRewriteBtn)
            m_xIntentRewriteBtn->set_label(u"公式"_ustr);
        if (m_xIntentFormalBtn)
            m_xIntentFormalBtn->set_label(u"解释"_ustr);
        if (m_xIntentShortenBtn)
            m_xIntentShortenBtn->set_label(u"清洗"_ustr);
        if (m_xIntentExpandBtn)
            m_xIntentExpandBtn->set_label(u"汇总"_ustr);
        if (m_xIntentSummarizeBtn)
            m_xIntentSummarizeBtn->set_label(u"解读"_ustr);
        if (m_xIntentOutlineBtn)
            m_xIntentOutlineBtn->set_label(u"分析纲"_ustr);
        if (m_xIntentProofreadBtn)
            m_xIntentProofreadBtn->set_label(u"质检"_ustr);
        if (m_xIntentContinueBtn)
            m_xIntentContinueBtn->set_label(u"补全"_ustr);
        if (m_xIntentPlanBtn)
            m_xIntentPlanBtn->set_label(u"规划"_ustr);
        if (m_xIntentAgentBtn)
            m_xIntentAgentBtn->set_label(u"多步"_ustr);
        if (m_xIntentFormalBtn)
            m_xIntentFormalBtn->set_tooltip_text(
                u"解释选区含义与可写公式建议（不自动写回；公式可批准写入）"_ustr);
        if (m_xIntentRewriteBtn)
            m_xIntentRewriteBtn->set_tooltip_text(
                u"为选区生成以 = 开头的公式；批准后写入活动表当前单元格"_ustr);
        if (m_xIntentShortenBtn)
            m_xIntentShortenBtn->set_tooltip_text(
                u"空值/重复/类型/异常清单 + 公式建议（表格不自动改）"_ustr);
        if (m_xIntentSummarizeBtn)
            m_xIntentSummarizeBtn->set_tooltip_text(
                u"结论、趋势与风险（仅依据表内数据）"_ustr);
        if (m_xIntentExpandBtn)
            m_xIntentExpandBtn->set_tooltip_text(
                u"合计/平均/计数等汇总公式（须批准后写入）"_ustr);
    }
    else if (surface == u"impress"_ustr)
    {
        if (m_xIntentRewriteBtn)
            m_xIntentRewriteBtn->set_label(u"本页"_ustr);
        if (m_xIntentShortenBtn)
            m_xIntentShortenBtn->set_label(u"精简"_ustr);
        if (m_xIntentExpandBtn)
            m_xIntentExpandBtn->set_label(u"大纲"_ustr);
        if (m_xIntentSummarizeBtn)
            m_xIntentSummarizeBtn->set_label(u"讲稿"_ustr);
        if (m_xIntentPlanBtn)
            m_xIntentPlanBtn->set_label(u"多方案"_ustr);
        if (m_xIntentAgentBtn)
            m_xIntentAgentBtn->set_label(u"写回"_ustr);
    }
    else
    {
        if (m_xIntentRewriteBtn)
            m_xIntentRewriteBtn->set_label(u"改写"_ustr);
        if (m_xIntentShortenBtn)
            m_xIntentShortenBtn->set_label(u"精简"_ustr);
        if (m_xIntentExpandBtn)
            m_xIntentExpandBtn->set_label(u"扩写"_ustr);
        if (m_xIntentSummarizeBtn)
            m_xIntentSummarizeBtn->set_label(u"总结"_ustr);
        if (m_xIntentPlanBtn)
            m_xIntentPlanBtn->set_label(u"规划"_ustr);
        if (m_xIntentAgentBtn)
            m_xIntentAgentBtn->set_label(u"多步"_ustr);
    }

    // Placeholder adapts to intent + surface.
    if (surface == u"calc"_ustr && intent == u"chat"_ustr)
        m_xPromptEntry->set_placeholder_text(u"表格：用自然语言要公式/清洗/汇总…"_ustr);
    else if (surface == u"impress"_ustr && intent == u"chat"_ustr)
        m_xPromptEntry->set_placeholder_text(u"演示：①大纲 → ②多方案 → ③选一写回 → ④导出…"_ustr);
    else if (intent == u"rewrite"_ustr)
        m_xPromptEntry->set_placeholder_text(surface == u"calc"_ustr
                                                 ? u"公式指令，例如：合计选区、写 =SUM…"_ustr
                                                 : (surface == u"impress"_ustr
                                                        ? u"本页改写，例如：更简洁的标题与要点…"_ustr
                                                        : u"改写指令，例如：更正式、更短、改成口语…"_ustr));
    else if (intent == u"shorten"_ustr)
        m_xPromptEntry->set_placeholder_text(u"精简指令，例如：压缩到一半、只保留要点…"_ustr);
    else if (intent == u"expand"_ustr)
        m_xPromptEntry->set_placeholder_text(surface == u"impress"_ustr
                                                 ? u"大纲：页序+标题+目的（不要 ## 写回体）…"_ustr
                                                 : u"扩写指令，例如：补充背景、加例子…"_ustr);
    else if (intent == u"summarize"_ustr)
        m_xPromptEntry->set_placeholder_text(u"总结指令，例如：三点摘要、给领导汇报…"_ustr);
    else if (intent == u"plan"_ustr)
        m_xPromptEntry->set_placeholder_text(
            surface == u"impress"_ustr
                ? u"多方案：请给方案A/B/C（页序对比，不写回）…"_ustr
                : u"规划目标，例如：本周工作计划大纲…"_ustr);
    else if (intent == u"agent"_ustr)
        m_xPromptEntry->set_placeholder_text(
            surface == u"impress"_ustr
                ? u"选一写回：指明方案A/B/C，生成 ## 幻灯体（须批准）…"_ustr
                : u"多步任务，例如：协作审阅并给修改建议…"_ustr);
    else
        m_xPromptEntry->set_placeholder_text(u"描述你要做的事，或点意图芯片 / 上方方案…"_ustr);

    UpdateDesignFlowChrome();
}

void AIChatPanel::UpdateDesignFlowChrome()
{
    const bool bImpress = CurrentDocumentSurface() == u"impress"_ustr;
    if (m_xDesignFlowBox)
        m_xDesignFlowBox->set_visible(bImpress);
    // Buttons remain sensitive whenever visible; scenario prefill handles empty catalog.
    if (m_xDesignStepOutline)
        m_xDesignStepOutline->set_sensitive(bImpress);
    if (m_xDesignStepVariants)
        m_xDesignStepVariants->set_sensitive(bImpress);
    if (m_xDesignStepApply)
        m_xDesignStepApply->set_sensitive(bImpress);
    if (m_xDesignStepExport)
        m_xDesignStepExport->set_sensitive(bImpress);
}

void AIChatPanel::ApplyComposerIntent(const OUString& rIntentId, const OUString& rSeedPrompt)
{
    m_sForcedCapability = kqoffice::ai::normalizeCapabilityHint(rIntentId);
    if (m_xOptAgentPipeline)
        m_xOptAgentPipeline->set_active(rIntentId == u"agent"_ustr);

    // Always replace empty or intent-seed prompts so chips are one-shot.
    if (m_xPromptEntry)
    {
        const OUString cur = m_xPromptEntry->get_text().trim();
        // Replace when empty, or when current text looks like a previous chip seed.
        const bool bLooksLikeSeed
            = cur.startsWith(u"请"_ustr) || cur.startsWith(u"【"_ustr) || cur.startsWith(u"/agent"_ustr)
              || cur.isEmpty();
        if (cur.isEmpty() || bLooksLikeSeed)
            m_xPromptEntry->set_text(rSeedPrompt);
        FocusPrompt();
    }
    UpdateComposerChrome();
    UpdateActivityCard();

    // One-click path: when user has a document selection (or consult intents that
    // don't need selection), auto-submit so path is 选区 → 点芯片 → 待批.
    const kqoffice::ai::chat::SelectionContext sel
        = kqoffice::ai::chat::AgentChatSelectionCapture::captureCurrent();
    // Calc 公式/清洗/汇总: selection strongly preferred but empty sheet still allows chat.
    const bool bNeedsSelection = (rIntentId == u"rewrite"_ustr || rIntentId == u"shorten"_ustr
                                  || rIntentId == u"expand"_ustr || rIntentId == u"formal"_ustr)
                                 && CurrentDocumentSurface() != u"calc"_ustr;
    const bool bCanAuto = !IsRunBusy()
                          && ((!bNeedsSelection) || sel.length > 0);
    if (m_xStatusLabel)
    {
        if (bNeedsSelection && sel.length <= 0)
            m_xStatusLabel->set_label(u"请先在文档中选中文字，再点「"_ustr
                                      + IntentIdToZh(rIntentId) + u"」"_ustr);
        else if (bCanAuto && sel.length > 0
                 && (rIntentId == u"rewrite"_ustr || rIntentId == u"formal"_ustr
                     || rIntentId == u"shorten"_ustr || rIntentId == u"expand"_ustr))
            m_xStatusLabel->set_label(u"意图："_ustr + IntentIdToZh(rIntentId)
                                      + u" · 选区快路径生成中（主文档不会自动改）"_ustr);
        else
            m_xStatusLabel->set_label(u"意图："_ustr + IntentIdToZh(rIntentId)
                                      + (bCanAuto ? u" · 正在生成（主文档不会自动改）"_ustr
                                                  : u" · 主文档不会自动改"_ustr));
    }
    if (bCanAuto && m_xPromptEntry && !m_xPromptEntry->get_text().trim().isEmpty())
        SubmitPrompt();
}

void AIChatPanel::SetState(AIChatPanelState eState)
{
    m_eState = eState;
    // Human status in title; machine token only via activity tooltip / StateToLabel.
    if (m_xStatusLabel)
    {
        if ((eState == AIChatPanelState::Failed || eState == AIChatPanelState::Cancelled)
            && !m_sLastOutcomeDetail.isEmpty())
        {
            m_xStatusLabel->set_label(StateToUserLabel(eState) + u" · "_ustr
                                      + m_sLastOutcomeDetail);
        }
        else
            m_xStatusLabel->set_label(StateToUserLabel(eState));
    }
    if (eState == AIChatPanelState::Idle || eState == AIChatPanelState::Applied
        || eState == AIChatPanelState::Failed || eState == AIChatPanelState::Cancelled)
    {
        if (eState == AIChatPanelState::Idle)
        {
            m_sLastOutcomeDetail.clear();
            SetAgentStepBar(u"步骤：待命"_ustr);
        }
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
                                    u"AI 运行时未就绪，请重启应用后重试"_ustr);

        css::uno::Reference<css::ai::XProvider> xProvider(
            xContext->getServiceManager()->createInstanceWithContext(V2_PROVIDER_SERVICE_NAME,
                                                                      xContext),
            css::uno::UNO_QUERY);
        if (!xProvider.is())
            return MakeLocalFailure(
                u"provider-error"_ustr,
                u"可圈 AI 暂不可用，请到「工具 → 选项 → 可圈 AI」检查模型配置"_ustr);

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

        // Closed loop: expand @文件 / @截图 / @文件夹 into local extracted text (+ OCR).
        // Expand on both raw and enriched body so mentions in user text always resolve.
        {
            OUString materialSummary;
            const OUString expanded
                = kqoffice::ai::chat::DocumentAIMaterialReader::expandMentionsInPrompt(
                    sPromptBody, materialSummary);
            if (expanded != sPromptBody)
            {
                sPromptBody = expanded;
                if (!materialSummary.isEmpty())
                {
                    AppendTranscript(u"System"_ustr,
                                     u"已读取本地材料 · "_ustr + materialSummary,
                                     /*bPersistHistory*/ false);
                    if (m_xStatusLabel)
                        m_xStatusLabel->set_label(materialSummary);
                }
                // M11: index local materials into FTS (mtime-incremental).
                if (AIChatKnowledgeFtsEngine::IsSqliteAvailable())
                {
                    const auto matIdx
                        = AIChatKnowledgeFtsEngine::IndexMaterialsFromPrompt(rPrompt);
                    AppendTranscript(u"System"_ustr, matIdx.Message, /*bPersistHistory*/ false);
                }
            }
        }

        // Multi-turn document session: rewrite memory (constraints) + recent turns.
        // Memory is updated on SubmitPrompt (raw user text only), not from skill-expanded
        // rPrompt — avoids polluting constraints with skill-pack boilerplate.
        if (m_xHistoryStore)
        {
            using kqoffice::ai::chat::DocumentAIRewriteMemory;
            const auto memCard
                = DocumentAIRewriteMemory::load(m_xHistoryStore->GetDocumentKey());
            // Prefer memory block first; shrink recent turns when we have hard constraints.
            const sal_Int32 nTurns = memCard.constraints.empty() ? 6 : 4;
            const sal_Int32 nChars = memCard.constraints.empty() ? 2800 : 1800;
            const OUString recent
                = m_xHistoryStore->FormatRecentTurns(/*nMaxTurns*/ nTurns, /*nMaxChars*/ nChars);
            const OUString memBlock = DocumentAIRewriteMemory::formatPromptBlock(memCard);

            if (!memBlock.isEmpty() || !recent.isEmpty())
            {
                OUStringBuffer multi;
                if (!memBlock.isEmpty())
                {
                    multi.append(memBlock);
                    multi.append(u"\n"_ustr);
                }
                if (!recent.isEmpty())
                {
                    multi.append(
                        u"【同一文档近期对话 — 请承接上文意图改当前文档，勿重置话题】\n"_ustr);
                    multi.append(recent);
                    multi.append(u"\n\n"_ustr);
                }
                multi.append(u"【当前用户请求】\n"_ustr);
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

        // Selection-complete edit path: full selection text is already in the prompt —
        // skip multi-round TOOL_REQUEST (saves a model round) and prefer light for short edits.
        const sal_Int32 nSelLen = aBind.selection.length > 0
                                      ? aBind.selection.length
                                      : aBind.selection.text.getLength();
        const bool bSelectionCompleteEdit
            = kqoffice::ai::chat::DocumentAIDocumentTools::shouldSkipMultiRoundForSelection(
                aBind.hasSelection, nSelLen, cap, rPrompt);
        const bool bLightQuickEdit
            = kqoffice::ai::chat::DocumentAIDocumentTools::shouldUseLightSlotForSelectionEdit(
                aBind.hasSelection, nSelLen, cap);
        if (bLightQuickEdit && (cap == u"rewrite"_ustr || cap == u"polish"_ustr
                                || cap == u"edit"_ustr || cap == u"formal"_ustr
                                || cap == u"shorten"_ustr || cap == u"condense"_ustr
                                || cap == u"paraphrase"_ustr || cap == u"translate"_ustr
                                || cap == u"translation"_ustr))
        {
            cap = u"quick-edit"_ustr;
        }

        // M8.4 document-tools channel: local read-only tool pass (skeleton + optional
        // read_blocks) before Provider. Never mutates the main document.
        {
            kqoffice::ai::chat::DocumentToolSkeleton skForTools;
            // Reuse bind metadata; full structure is rebuilt inside prepare when needed.
            skForTools.hasDocument = aBind.hasDocument;
            skForTools.snapshotHash = aBind.documentSnapshotHash;
            skForTools.formatted = aBind.documentSkeleton;
            const kqoffice::ai::chat::DocumentToolPrepResult toolPrep
                = kqoffice::ai::chat::DocumentAIDocumentTools::prepareReadOnlyToolPass(
                    rPrompt, cap, skForTools, aBind.selection.position, aBind.hasSelection);
            if (toolPrep.ranTools)
            {
                // M13: surface intent in step bar (consult skips write tools).
                if (toolPrep.intent == u"consult"_ustr)
                    SetAgentStepBar(u"步骤：工具 · document-tools（咨询·只读）…"_ustr);
                else if (bSelectionCompleteEdit)
                    SetAgentStepBar(u"步骤：工具 · 选区全文快路径（跳过多轮）…"_ustr);
                else if (toolPrep.intent == u"edit"_ustr)
                    SetAgentStepBar(u"步骤：工具 · document-tools（改写·只读预读）…"_ustr);
                else
                    SetAgentStepBar(u"步骤：工具 · document-tools（只读）…"_ustr);
                for (const auto& act : toolPrep.activities)
                {
                    AppendTranscript(u"System"_ustr, act.summary, /*bPersistHistory*/ false);
                }
                if (!toolPrep.promptInjection.isEmpty())
                {
                    OUStringBuffer withTools;
                    withTools.append(toolPrep.promptInjection);
                    withTools.append(u"\n"_ustr);
                    withTools.append(sPromptBody);
                    sPromptBody = withTools.makeStringAndClear();
                }
                // M9.1: register tool outputs as openable workspace content objects.
                {
                    const auto reg = AIChatDocumentToolsContentBridge().RegisterPrepResult(toolPrep);
                    AppendTranscript(u"System"_ustr, reg.Message, /*bPersistHistory*/ false);
                    if (reg.Success)
                        LoadArtifactNavigator();
                }
                if (m_xStatusLabel && !toolPrep.statusLabel.isEmpty())
                    m_xStatusLabel->set_label(toolPrep.statusLabel);
                // M14 / fast path: selection-complete edits get compact notice (no TOOL_REQUEST).
                // Otherwise inject multi-round protocol for skeleton/lazy edit paths.
                if (aBind.hasDocument && toolPrep.intent != u"consult"_ustr)
                {
                    OUStringBuffer withProto;
                    if (bSelectionCompleteEdit)
                    {
                        withProto.append(
                            kqoffice::ai::chat::DocumentAIDocumentTools::buildSelectionCompleteEditNotice(
                                aBind.selection.text, aBind.selection.position));
                        AppendTranscript(
                            u"System"_ustr,
                            u"document-tools · selection-complete · skip-multi-round · chars="_ustr
                                + OUString::number(nSelLen)
                                + u" · 禁止 TOOL_REQUEST · 主文档未改"_ustr,
                            /*bPersistHistory*/ false);
                    }
                    else
                    {
                        withProto.append(
                            kqoffice::ai::chat::DocumentAIDocumentTools::buildMultiRoundToolProtocolNotice());
                    }
                    withProto.append(u"\n"_ustr);
                    withProto.append(sPromptBody);
                    sPromptBody = withProto.makeStringAndClear();
                }
            }
            else if (aBind.hasDocumentSkeleton)
            {
                AppendTranscript(
                    u"System"_ustr,
                    u"document-tools · get_document_context · 已附文档骨架 "
                    u"(index|type|preview) · snapshot="_ustr
                        + (aBind.documentSnapshotHash.isEmpty() ? u"(none)"_ustr
                                                                : aBind.documentSnapshotHash)
                        + u" · 无全文倾倒 · 无外传"_ustr,
                    /*bPersistHistory*/ false);
                // Selection-complete: still inject fast-path notice; else multi-round protocol.
                if (aBind.hasDocument)
                {
                    OUStringBuffer withProto;
                    if (bSelectionCompleteEdit)
                    {
                        withProto.append(
                            kqoffice::ai::chat::DocumentAIDocumentTools::buildSelectionCompleteEditNotice(
                                aBind.selection.text, aBind.selection.position));
                        AppendTranscript(
                            u"System"_ustr,
                            u"document-tools · selection-complete · skip-multi-round · chars="_ustr
                                + OUString::number(nSelLen)
                                + u" · 禁止 TOOL_REQUEST · 主文档未改"_ustr,
                            /*bPersistHistory*/ false);
                    }
                    else
                    {
                        withProto.append(
                            kqoffice::ai::chat::DocumentAIDocumentTools::buildMultiRoundToolProtocolNotice());
                    }
                    withProto.append(u"\n"_ustr);
                    withProto.append(sPromptBody);
                    sPromptBody = withProto.makeStringAndClear();
                }
            }
            else if (bSelectionCompleteEdit && aBind.hasSelection)
            {
                // No skeleton (e.g. thin surface) but selection text is enough.
                OUStringBuffer withProto;
                withProto.append(
                    kqoffice::ai::chat::DocumentAIDocumentTools::buildSelectionCompleteEditNotice(
                        aBind.selection.text, aBind.selection.position));
                withProto.append(u"\n"_ustr);
                withProto.append(sPromptBody);
                sPromptBody = withProto.makeStringAndClear();
                AppendTranscript(
                    u"System"_ustr,
                    u"document-tools · selection-complete · skip-multi-round · chars="_ustr
                        + OUString::number(nSelLen) + u" · 禁止 TOOL_REQUEST · 主文档未改"_ustr,
                    /*bPersistHistory*/ false);
            }
        }

        // Local document RAG: prefer real SQLite FTS5; fall back to keyword LocalRag.
        const bool bWantRag
            = kqoffice::ai::chat::DocumentAILocalRag::wantsDocumentRag(rPrompt)
              || cap == u"knowledge-query"_ustr
              || (m_xOptDocContext && m_xOptDocContext->get_active()
                  && (rPrompt.indexOf(u"问本文档"_ustr) >= 0
                      || rPrompt.startsWith(u"/问"_ustr)));
        if (bWantRag)
        {
            bool bAttached = false;
            // M9.2 / M18: real FTS over open document; reindex once if cold empty.
            if (AIChatKnowledgeFtsEngine::IsSqliteAvailable())
            {
                SetAgentStepBar(u"步骤：工具 · knowledge-fts（sqlite-fts5）…"_ustr);
                auto ftsResult
                    = AIChatKnowledgeRetrievalRuntime::QueryOpenDocumentFts(rPrompt, 8);
                if ((!ftsResult.Success || ftsResult.Chunks.empty())
                    && aBind.hasDocument)
                {
                    // Cold index / stale workspace: rebuild then retry once.
                    const auto idx = AIChatKnowledgeFtsEngine::ForceReindexOpenDocument();
                    AppendTranscript(u"System"_ustr,
                                     u"knowledge-fts · reindex · "_ustr + idx.Message
                                         + u" · 主文档未改"_ustr,
                                     /*bPersistHistory*/ false);
                    ftsResult
                        = AIChatKnowledgeRetrievalRuntime::QueryOpenDocumentFts(rPrompt, 8);
                }
                // Also pull engine hits for locate positions (richer than hash-only chunks).
                const auto ftsHits = AIChatKnowledgeFtsEngine::Search(rPrompt, 8);
                if (!ftsHits.Hits.empty())
                {
                    m_sLastRagQuery = rPrompt;
                    m_sLastRagPosition = ftsHits.Hits.front().Position;
                    if (m_xLocateRagBtn)
                        m_xLocateRagBtn->set_sensitive(true);
                }
                OUString ftsBlock;
                if (ftsResult.Success && !ftsResult.Chunks.empty())
                    ftsBlock = AIChatKnowledgeRetrievalRuntime::BuildFtsPromptBlock(
                        ftsResult, rPrompt, 4500);
                if (ftsBlock.isEmpty() && !ftsHits.Hits.empty())
                    ftsBlock = AIChatKnowledgeFtsEngine::BuildPromptBlock(ftsHits, 4500);
                if (!ftsBlock.isEmpty())
                {
                    OUStringBuffer withFts;
                    withFts.append(ftsBlock);
                    withFts.append(u"\n【用户问题】\n"_ustr);
                    withFts.append(sPromptBody);
                    sPromptBody = withFts.makeStringAndClear();
                    bAttached = true;
                    const sal_Int32 nHits
                        = !ftsHits.Hits.empty()
                              ? static_cast<sal_Int32>(ftsHits.Hits.size())
                              : static_cast<sal_Int32>(ftsResult.Chunks.size());
                    AppendTranscript(
                        u"System"_ustr,
                        u"knowledge-fts · sqlite-fts5 · hits="_ustr + OUString::number(nHits)
                            + (m_sLastRagPosition.isEmpty()
                                   ? OUString()
                                   : (u" · first="_ustr + m_sLastRagPosition))
                            + u" · 无外传 · 主文档未改"_ustr);
                    if (ftsResult.Success && !ftsResult.Chunks.empty())
                    {
                        const auto kreg
                            = AIChatKnowledgeResultContentBridge().RegisterResultWithPreview(
                                ftsResult, ftsBlock);
                        AppendTranscript(u"System"_ustr, kreg.Message, /*bPersistHistory*/ false);
                        if (kreg.Success)
                            LoadArtifactNavigator();
                    }
                    if (m_xStatusLabel)
                        m_xStatusLabel->set_label(
                            u"本地 FTS5 已附带 · 可「定位出处」· 无外传 · 主文档未改"_ustr);
                }
                else
                {
                    AppendTranscript(u"System"_ustr,
                                     u"knowledge-fts · fallback · "_ustr
                                         + (ftsResult.Message.isEmpty()
                                                ? u"no-hits"_ustr
                                                : ftsResult.Message),
                                     /*bPersistHistory*/ false);
                }
            }
            if (!bAttached)
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
                    AppendTranscript(u"System"_ustr,
                                     u"local-rag · 本地检索：已附带本文档相关片段 · 无外传"_ustr);
                    if (m_xStatusLabel)
                        m_xStatusLabel->set_label(
                            aBind.hasDocumentSkeleton
                                ? u"文档骨架 + 本地检索已附带 · 无外传"_ustr
                                : u"本地文档检索已附带 · 无外传"_ustr);
                }
                else if (m_xStatusLabel)
                {
                    m_xStatusLabel->set_label(u"本地检索：当前文档无可抽取文本"_ustr);
                }
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
                             u"路由 · 能力="_ustr + cap + u" · 槽="_ustr + resolved.slotName
                                 + u" · 模型="_ustr
                                 + (resolved.model.isEmpty() ? u"未配置"_ustr : resolved.model)
                                 + u" · 角色="_ustr + resolved.roleName);
        }

        // Prefer real streaming (SSE/NDJSON) for sidebar UX; fall back to sync XProvider.
        // Sprint C: chatStreamingDefault (default true) — local-model friendly token paint.
        // M14: multi-round read-only tool loop — model may emit TOOL_REQUEST: read_blocks …
        // We execute locally (never mutate main doc) and re-prompt until final answer.
        // Selection-complete path: kMaxRounds=0 (no mid-turn tool re-prompt).
        css::ai::ProviderResponse aRsp;
        m_sStreamingBuffer.clear();
        {
            const bool bWantStream
                = kqoffice::ai::chat::DocumentAIInputPrefs::load().chatStreamingDefault;
            const OUString basePrompt = aRequest.prompt;
            OUString roundPrompt = basePrompt;
            OUString lastProviderLabel;
            sal_Int32 totalDurationMs = 0;
            const sal_Int32 kMaxRounds
                = bSelectionCompleteEdit
                      ? 0
                      : kqoffice::ai::chat::DocumentAIDocumentTools::kDefaultMaxToolRounds;
            kqoffice::ai::chat::DocumentToolSkeleton skForRound;
            skForRound.hasDocument = aBind.hasDocument;
            skForRound.snapshotHash = aBind.documentSnapshotHash;
            skForRound.formatted = aBind.documentSkeleton;

            for (sal_Int32 round = 0; round <= kMaxRounds; ++round)
            {
                if (m_bCancelRequested)
                {
                    aRsp.status = u"cancelled"_ustr;
                    break;
                }
                aRequest.prompt = roundPrompt;
                m_sStreamingBuffer.clear();
                if (round == 0 && bSelectionCompleteEdit)
                    SetAgentStepBar(u"步骤：模型 · 选区快路径生成中…"_ustr);
                else if (round == 0)
                    SetAgentStepBar(bWantStream ? u"步骤：模型 · 流式生成中…"_ustr
                                                : u"步骤：模型 · 生成中…"_ustr);
                else
                    SetAgentStepBar(u"步骤：模型 · 多轮工具第 "_ustr + OUString::number(round + 1)
                                    + u" 轮…"_ustr);

                if (bWantStream)
                {
                    const kqoffice::ai::StreamChatResult stream
                        = kqoffice::ai::streamChatCompletion(
                            aRequest.capability, aRequest.prompt,
                            [this](const OUString& rDelta) -> bool {
                                if (m_bCancelRequested)
                                    return false;
                                AppendAssistantChunk(rDelta);
                                if (Application::IsInMain())
                                    Application::Reschedule();
                                return !m_bCancelRequested;
                            },
                            [this]() -> bool { return m_bCancelRequested; });
                    aRsp.status = stream.status;
                    aRsp.content = stream.content;
                    aRsp.durationMs = stream.durationMs;
                    aRsp.evidenceId = OUString();
                    totalDurationMs += stream.durationMs;
                    lastProviderLabel = stream.providerLabel;
                    if (aRsp.status == u"ok"_ustr && aRsp.content.isEmpty()
                        && !m_sStreamingBuffer.isEmpty())
                        aRsp.content = m_sStreamingBuffer;
                }
                else
                {
                    aRsp = xProvider->call(aRequest);
                    totalDurationMs += aRsp.durationMs;
                    lastProviderLabel = u"sync"_ustr;
                    if (aRsp.status == u"ok"_ustr && !aRsp.content.isEmpty())
                        AppendAssistantMarkdown(aRsp.content);
                }

                // Stream fallback → sync Provider once per round.
                if (bWantStream
                    && (aRsp.status == u"provider-error"_ustr || aRsp.content.isEmpty())
                    && aRsp.status != u"cancelled"_ustr && aRsp.status != u"policy-denied"_ustr)
                {
                    m_sStreamingBuffer.clear();
                    aRsp = xProvider->call(aRequest);
                    totalDurationMs += aRsp.durationMs;
                }

                if (aRsp.status != u"ok"_ustr || m_bCancelRequested)
                    break;

                // Intermediate tool request → execute read_blocks locally, re-prompt.
                if (round < kMaxRounds
                    && kqoffice::ai::chat::DocumentAIDocumentTools::isPrimarilyToolRequest(
                        aRsp.content))
                {
                    const auto req
                        = kqoffice::ai::chat::DocumentAIDocumentTools::parseModelToolRequest(
                            aRsp.content);
                    const auto exec
                        = kqoffice::ai::chat::DocumentAIDocumentTools::executeReadOnlyToolRequest(
                            req, skForRound);
                    AppendTranscript(u"System"_ustr, exec.activity.summary,
                                     /*bPersistHistory*/ false);
                    if (m_xStatusLabel && !exec.statusLabel.isEmpty())
                        m_xStatusLabel->set_label(exec.statusLabel);
                    SetAgentStepBar(u"步骤：工具 · multi-round · read_blocks · 主文档未改"_ustr);

                    // Do not treat intermediate TOOL_REQUEST as final assistant answer.
                    m_sStreamingBuffer.clear();
                    if (exec.toolResultBlock.isEmpty())
                        break;

                    OUStringBuffer next;
                    next.append(basePrompt);
                    next.append(u"\n\n"_ustr);
                    next.append(exec.toolResultBlock);
                    next.append(u"\n【请基于 TOOL_RESULT 继续完成用户请求；"
                                u"若仍缺全文可再次 TOOL_REQUEST，否则直接给出最终结果。"
                                u"禁止声称已改主文档。】\n"_ustr);
                    roundPrompt = next.makeStringAndClear();
                    continue;
                }

                // Final answer this round.
                break;
            }

            aRsp.durationMs = totalDurationMs;
            if (aRsp.status == u"ok"_ustr && m_xHistoryStore && !m_sStreamingBuffer.isEmpty()
                && !kqoffice::ai::chat::DocumentAIDocumentTools::isPrimarilyToolRequest(
                    aRsp.content))
            {
                // Persist only final streamed assistant turn (not intermediate TOOL_REQUEST).
                m_xHistoryStore->AppendMessage(u"AI"_ustr, m_sStreamingBuffer);
                // Compaction tick for rewrite memory (local summary of constraints).
                {
                    using kqoffice::ai::chat::DocumentAIRewriteMemory;
                    auto memCard
                        = DocumentAIRewriteMemory::load(m_xHistoryStore->GetDocumentKey());
                    const OUString recent
                        = m_xHistoryStore->FormatRecentTurns(/*nMaxTurns*/ 6, /*nMaxChars*/ 2000);
                    DocumentAIRewriteMemory::afterAssistantTurn(memCard, m_sStreamingBuffer,
                                                                recent);
                    if (memCard.dirty)
                        DocumentAIRewriteMemory::save(memCard);
                }
            }
            // Evidence for stream / multi-round path
            if (aRsp.evidenceId.isEmpty() && aRsp.status == u"ok"_ustr)
            {
                kqoffice::ai::EvidenceRecord rec;
                rec.serviceMode = u"private"_ustr;
                rec.provider = lastProviderLabel.isEmpty() ? u"stream"_ustr : lastProviderLabel;
                rec.capability = aRequest.capability;
                rec.status = aRsp.status;
                rec.requestSizeBytes = basePrompt.getLength();
                rec.responseSizeBytes = aRsp.content.getLength();
                rec.durationMs = aRsp.durationMs;
                kqoffice::ai::EvidenceRecorder recorder;
                aRsp.evidenceId = recorder.record(rec);
            }
        }
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
        tabZh = u"PDF·通用"_ustr;

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
    UpdateComposerChrome();
    UpdateActivityCard();
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

    // Unified selection chip template (M4.4): 选区 · {表面} · {位置} · {长度}
    OUStringBuffer label;
    label.append(u"选区 · "_ustr);
    label.append(surfaceZh);
    label.append(u" · "_ustr);
    label.append(sel.position.isEmpty() ? u"未定位"_ustr : sel.position);
    label.append(u" · "_ustr);
    label.append(sel.length);
    label.append(u" 字"_ustr);
    if (sel.length == 0)
        label.append(u"（空 · 点刷新）"_ustr);

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
    if (sel.length > 0)
        tip.append(u"\n\n有选区：可直接点「改写 / 正式 / 精简 / 总结」一键生成（须批准后写回）"_ustr);
    else
        tip.append(u"\n\n无选区：请先在正文选中文字，再点意图芯片"_ustr);
    tip.append(u"\n快捷键：Ctrl/Cmd+Shift+F6 或 Ctrl+Alt+J"_ustr);
    m_xSelectionChipBtn->set_tooltip_text(tip.makeStringAndClear());
}

void AIChatPanel::UpdatePendingPlanChip()
{
    if (!m_xPendingPlanChip)
        return;
    if (!m_bHasPendingPlan)
    {
        // When idle: surface rewrite-memory affordance (local constraints count).
        sal_Int32 nMem = 0;
        if (m_xHistoryStore)
        {
            const auto mc = kqoffice::ai::chat::DocumentAIRewriteMemory::load(
                m_xHistoryStore->GetDocumentKey());
            nMem = static_cast<sal_Int32>(mc.constraints.size() + mc.corrections.size());
        }
        if (nMem > 0)
        {
            m_xPendingPlanChip->set_label(u"记忆："_ustr + OUString::number(nMem)
                                          + u" · 点此查看"_ustr);
            m_xPendingPlanChip->set_tooltip_text(
                u"本文档改稿记忆（硬约束/纠偏）共 "_ustr + OUString::number(nMem)
                + u" 条。点击查看；`/记住` `/忘记` 管理。本地不上传。\n"
                  u"无待批写回计划时显示此项。"_ustr);
            m_xPendingPlanChip->set_sensitive(true);
        }
        else
        {
            m_xPendingPlanChip->set_label(u"待批：无"_ustr);
            m_xPendingPlanChip->set_tooltip_text(
                u"生成改写建议后，此处会出现「待批」计划。主文档在批准前不会被修改。\n"
                u"也可用 `/记住 别动金额列` 写入改稿记忆。"_ustr);
            m_xPendingPlanChip->set_sensitive(true); // still clickable → tip / memory empty
        }
        return;
    }
    OUStringBuffer b;
    if (m_aPendingPlan.planId == u"ap-chart-insert"_ustr)
        b.append(u"待批图表 · 点此预览/批准"_ustr);
    else
    {
        b.append(u"待批 · "_ustr);
        b.append(static_cast<sal_Int32>(m_aPendingPlan.operations.size()));
        b.append(u" 步 · 点此 Diff/批准"_ustr);
    }
    m_xPendingPlanChip->set_label(b.makeStringAndClear());
    m_xPendingPlanChip->set_sensitive(true);
    m_xPendingPlanChip->set_tooltip_text(
        u"① 点击打开 Diff 预览\n② 点「批准写回」写入主文档\n③ 或「拒绝」丢弃\n"
        u"主文档在批准前不会被修改。若编辑过正文，可能提示结构已变更（stale）。"_ustr);
}

void AIChatPanel::UpdateTaskBootstrapChip(const OUString& rVisibleZh)
{
    if (!m_xTaskBootstrapChip)
        return;
    // Work-plan chip takes precedence when a plan is staged for confirmation.
    if (m_bHasWorkPlan && !m_bWorkPlanApproved)
    {
        UpdateWorkPlanChip();
        return;
    }
    if (rVisibleZh.isEmpty())
    {
        m_xTaskBootstrapChip->set_label(u"理解：待命"_ustr);
        m_xTaskBootstrapChip->set_tooltip_text(
            u"发送任务后显示语义启动复述（理解/范围/下一步）。主文档不自动改。"_ustr);
        return;
    }
    OUString label = rVisibleZh;
    // Chip is compact: keep「理解：…」prefix if present, else add.
    if (label.indexOf(u"理解："_ustr) < 0)
        label = u"理解："_ustr + label;
    if (label.getLength() > 42)
        label = label.copy(0, 42) + u"…"_ustr;
    m_xTaskBootstrapChip->set_label(label);
    m_xTaskBootstrapChip->set_tooltip_text(rVisibleZh + u"\n\n点击刷新选区上下文 · 主文档未改"_ustr);
}

void AIChatPanel::UpdateWorkPlanChip()
{
    if (!m_xTaskBootstrapChip)
        return;
    if (!m_bHasWorkPlan)
    {
        UpdateTaskBootstrapChip(OUString());
        return;
    }
    m_xTaskBootstrapChip->set_label(
        kqoffice::ai::chat::DocumentAIWorkPlan::chipLabelZh(m_aWorkPlan, m_bWorkPlanApproved));
    m_xTaskBootstrapChip->set_tooltip_text(
        (m_bWorkPlanApproved ? u"工作计划已确认，正在/即将生成草案。\n"_ustr
                             : u"工作计划待确认。回复「按此计划执行」或 /approve-plan。\n"_ustr)
        + u"写回仍须「批准写回」。点击刷新选区。\n\n"_ustr + m_aWorkPlan.objective);
}

void AIChatPanel::ClearWorkPlan()
{
    m_aWorkPlan = kqoffice::ai::chat::WorkPlan();
    m_bHasWorkPlan = false;
    m_bWorkPlanApproved = false;
    UpdateWorkPlanChip();
}

void AIChatPanel::PresentWorkPlan(const kqoffice::ai::chat::WorkPlan& rPlan)
{
    m_aWorkPlan = rPlan;
    m_bHasWorkPlan = true;
    m_bWorkPlanApproved = false;
    m_bForceWorkPlanOnce = false;
    AppendTranscript(u"System"_ustr,
                     u"工作计划已就绪 · "_ustr + rPlan.planId + u" · 主文档未改 · 请确认后再生成草案"_ustr,
                     /*bPersistHistory*/ false);
    AppendAssistantMarkdown(rPlan.markdown.isEmpty()
                                ? kqoffice::ai::chat::DocumentAIWorkPlan::formatMarkdown(rPlan)
                                : rPlan.markdown);
    UpdateWorkPlanChip();
    SetAgentStepBar(u"步骤：工作计划 · 待确认 · 主文档未改"_ustr);
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"计划待确认 · 回复「按此计划执行」· 主文档未改"_ustr);
    SetState(AIChatPanelState::Idle);
    FocusPrompt();
}

void AIChatPanel::UpdateApprovalChrome()
{
    // set_sensitive → Window::Enable can throw during InterimItemWindow construction.
    try
    {
    const bool bBusy = m_eState == AIChatPanelState::Requesting
                       || m_eState == AIChatPanelState::Streaming;
    const bool bPending = m_bHasPendingPlan && !bBusy;
    const bool bAwait = m_eState == AIChatPanelState::AwaitingApproval || bPending;

    if (m_xApprovalActionRow)
        m_xApprovalActionRow->set_visible(bAwait);

    if (m_xApprovalHintLabel)
    {
        if (bPending)
            m_xApprovalHintLabel->set_label(
                u"① 查看 Diff  →  ② 批准写回  ·  主文档尚未修改"_ustr);
        else if (m_bLastApplyCanUndo && !bBusy)
            m_xApprovalHintLabel->set_label(
                u"写回已生效 · 可点「撤销写回」回退本步 · 或继续改写"_ustr);
        else
            m_xApprovalHintLabel->set_label(
                u"选区后点「改写/正式」生成建议 → 此处批准写回"_ustr);
    }

    if (m_xChatApproveBtn)
    {
        m_xChatApproveBtn->set_sensitive(bPending);
        m_xChatApproveBtn->set_label(u"批准写回"_ustr);
    }
    if (m_xChatDiffBtn)
    {
        m_xChatDiffBtn->set_sensitive(bPending || FindSelectedArtifact() != nullptr);
        m_xChatDiffBtn->set_label(u"查看 Diff"_ustr);
    }
    if (m_xChatRejectBtn)
    {
        m_xChatRejectBtn->set_sensitive(bPending);
        m_xChatRejectBtn->set_label(u"拒绝"_ustr);
    }
    if (m_xChatUndoBtn)
    {
        // Show undo after successful AI write-back; hide sensitivity while pending/busy.
        const bool bUndo = m_bLastApplyCanUndo && !bPending && !bBusy;
        m_xChatUndoBtn->set_sensitive(bUndo);
        m_xChatUndoBtn->set_visible(true);
#if defined(MACOSX)
        m_xChatUndoBtn->set_label(u"撤销写回 ⌘Z"_ustr);
#else
        m_xChatUndoBtn->set_label(u"撤销写回 Ctrl+Z"_ustr);
#endif
    }
    if (m_xApprovalActionRow && m_bLastApplyCanUndo && !bPending)
        m_xApprovalActionRow->set_visible(true);

    // Reinforce review-tab primary actions when a plan is staged.
    if (m_xApproveSelectedButton && bPending)
        m_xApproveSelectedButton->set_label(u"批准写回"_ustr);
    if (m_xOpenDiffReviewButton)
        m_xOpenDiffReviewButton->set_label(u"查看 Diff"_ustr);
    if (m_xRejectSelectedButton && bPending)
        m_xRejectSelectedButton->set_label(u"拒绝"_ustr);

    UpdatePendingPlanChip();
    }
    catch (...)
    {
        // Ignore Enable/layout exceptions during early chrome updates.
    }
}

void AIChatPanel::ShowReviewTab()
{
    // Tabs: 0=chat 1=agent 2=content 3=review
    EnsureWorkspaceDataLoaded();
    if (m_xMainNotebook)
        m_xMainNotebook->set_current_page(3);
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"已打开「审核」· 请批准或拒绝写回"_ustr);
    UpdateApprovalChrome();
}

void AIChatPanel::HighlightPendingPlanTarget()
{
    if (!m_bHasPendingPlan)
        return;

    OUString target;
    if (!m_aPendingPlan.operations.empty())
        target = m_aPendingPlan.operations.front().target.trim();
    if (target.isEmpty())
    {
        const auto sel = kqoffice::ai::chat::AgentChatSelectionCapture::captureCurrent();
        target = sel.position.trim();
    }
    if (target.isEmpty())
        return;

    const kqoffice::ai::chat::LocalRagLocateResult loc
        = kqoffice::ai::chat::DocumentAILocalRag::locatePosition(target);
    AppendTranscript(u"System"_ustr,
                     (loc.success ? u"locate-ok "_ustr : u"locate-failed "_ustr)
                         + u"source=pending-plan-highlight pos="_ustr + target + u" · "_ustr
                         + loc.message + u" · 主文档未改"_ustr,
                     /*bPersistHistory*/ false);
    if (loc.success && m_xStatusLabel)
        m_xStatusLabel->set_label(u"已定位 "_ustr + target + u" · Diff 待批 · 主文档未改"_ustr);
}

void AIChatPanel::AppendPendingPlanDiffPreview()
{
    if (!m_bHasPendingPlan || m_aPendingPlan.operations.empty())
        return;

    auto clip = [](const OUString& s, sal_Int32 nMax) -> OUString {
        const OUString t = s.trim();
        if (t.getLength() <= nMax)
            return t;
        return t.copy(0, nMax) + u"…"_ustr;
    };
    auto oneLine = [](const OUString& s) -> OUString {
        return s.replaceAll(u"\r\n"_ustr, u"\n"_ustr)
            .replaceAll(u"\r"_ustr, u"\n"_ustr)
            .replaceAll(u"\n"_ustr, u" ⏎ "_ustr);
    };

    sal_Int32 nOldTotal = 0;
    sal_Int32 nNewTotal = 0;
    for (const auto& op : m_aPendingPlan.operations)
    {
        nOldTotal += op.oldText.getLength();
        nNewTotal += op.newText.getLength();
    }
    const sal_Int32 nDelta = nNewTotal - nOldTotal;

    OUStringBuffer md;
    md.append(u"### 待批 Diff（主文档未改）\n"_ustr);
    md.append(u"| 项 | 值 |\n| --- | --- |\n"_ustr);
    md.append(u"| 计划 | `"_ustr);
    md.append(m_aPendingPlan.planId.isEmpty() ? u"(unnamed)"_ustr : m_aPendingPlan.planId);
    md.append(u"` |\n"_ustr);
    md.append(u"| 操作数 | "_ustr);
    md.append(static_cast<sal_Int32>(m_aPendingPlan.operations.size()));
    md.append(u" |\n"_ustr);
    md.append(u"| 字数 | 旧 "_ustr);
    md.append(nOldTotal);
    md.append(u" → 新 "_ustr);
    md.append(nNewTotal);
    md.append(u"（"_ustr);
    if (nDelta > 0)
        md.append(u"+"_ustr);
    md.append(nDelta);
    md.append(u"） |\n"_ustr);
    md.append(u"| 流程 | **查看 Diff → 批准写回 / 拒绝** |\n\n"_ustr);

    const sal_Int32 nShow
        = std::min<sal_Int32>(5, static_cast<sal_Int32>(m_aPendingPlan.operations.size()));
    for (sal_Int32 i = 0; i < nShow; ++i)
    {
        const auto& op = m_aPendingPlan.operations[static_cast<size_t>(i)];
        const sal_Int32 nOld = op.oldText.getLength();
        const sal_Int32 nNew = op.newText.getLength();
        const sal_Int32 nOpDelta = nNew - nOld;

        md.append(u"#### #"_ustr);
        md.append(i + 1);
        md.append(u" `"_ustr);
        md.append(op.opType.isEmpty() ? u"replace"_ustr : op.opType);
        md.append(u"` @ `"_ustr);
        md.append(op.target.isEmpty() ? u"(selection)"_ustr : op.target);
        md.append(u"` · "_ustr);
        md.append(nOld);
        md.append(u"→"_ustr);
        md.append(nNew);
        md.append(u" 字（"_ustr);
        if (nOpDelta > 0)
            md.append(u"+"_ustr);
        md.append(nOpDelta);
        md.append(u"）\n\n"_ustr);

        // Unified-diff style side-by-side for quick scan in chat.
        md.append(u"```diff\n"_ustr);
        if (!op.oldText.isEmpty())
        {
            md.append(u"- "_ustr);
            md.append(clip(oneLine(op.oldText), 360));
            md.append(u"\n"_ustr);
        }
        else
            md.append(u"- （无原文 / 插入）\n"_ustr);
        if (!op.newText.isEmpty())
        {
            md.append(u"+ "_ustr);
            md.append(clip(oneLine(op.newText), 480));
            md.append(u"\n"_ustr);
        }
        else
            md.append(u"+ （删除 / 空建议）\n"_ustr);
        md.append(u"```\n\n"_ustr);
    }
    if (static_cast<sal_Int32>(m_aPendingPlan.operations.size()) > nShow)
    {
        md.append(u"_另有 "_ustr);
        md.append(static_cast<sal_Int32>(m_aPendingPlan.operations.size()) - nShow);
        md.append(u" 项操作，详见 Diff 对话框。_\n"_ustr);
    }
    md.append(u"\n纪律：以上为预览 · **批准前主文档不改** · 可 Cmd/Ctrl+Z 撤销写回。\n"_ustr);
    AppendAssistantMarkdown(md.makeStringAndClear());
}

void AIChatPanel::PresentPendingPlanForApproval(const OUString& rSource)
{
    if (!m_bHasPendingPlan)
        return;

    const OUString source = rSource.isEmpty() ? u"auto"_ustr : rSource;

    // 1) Jump to 审核 tab so approve/reject are front-and-center.
    ShowReviewTab();

    // 2) Open DiffReview dialog (pending, not applied).
    TryShowDiffReviewAfterApply(m_aPendingPlan.planId, u"pending-preview"_ustr, false);

    // 3) Highlight / select the first target in the document (read-only locate).
    HighlightPendingPlanTarget();

    // 4) Show old→new preview in the chat transcript.
    AppendPendingPlanDiffPreview();

    // 5) Chrome + activity narrative.
    UpdateApprovalChrome();
    SetAgentStepBar(u"步骤：待批 · Diff 已打开 · 主文档未改 · 请批准或拒绝"_ustr);
    if (m_xStatusLabel)
    {
        m_xStatusLabel->set_label(
            u"已自动打开 Diff 并定位目标 · 主文档尚未修改 · 请「批准写回」或「拒绝」"_ustr);
    }
    AppendTranscript(
        u"System"_ustr,
        u"diff-review-auto-opened source="_ustr + source + u" plan="_ustr
            + m_aPendingPlan.planId + u" ops="_ustr
            + OUString::number(static_cast<sal_Int32>(m_aPendingPlan.operations.size()))
            + u" applied=false main-document-mutation=false explicit-human-approval-required=true"_ustr,
        /*bPersistHistory*/ false);
    UpdateActivityCard();
}

IMPL_LINK_NOARG(AIChatPanel, OnSelectionChipClicked, weld::Button&, void)
{
    // Work-plan chip reuses this handler: re-show pending plan when present.
    if (m_bHasWorkPlan && !m_bWorkPlanApproved)
    {
        PresentWorkPlan(m_aWorkPlan);
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"已重新显示工作计划 · 主文档未改"_ustr);
        return;
    }
    UpdateSelectionChip();
    ReloadScenarioPicker();
    UpdatePendingPlanChip();
    m_xStatusLabel->set_label(u"已刷新选区芯片"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnPendingPlanChipClicked, weld::Button&, void)
{
    // Single audit chain: 去审核 → Diff + 定位 + 预览（与自动暂存路径一致）。
    if (m_bHasPendingPlan)
    {
        PresentPendingPlanForApproval(u"pending-plan-chip"_ustr);
        return;
    }
    // Idle: show rewrite memory card (local).
    if (m_xHistoryStore)
    {
        const auto card = kqoffice::ai::chat::DocumentAIRewriteMemory::load(
            m_xHistoryStore->GetDocumentKey());
        AppendAssistantMarkdown(
            kqoffice::ai::chat::DocumentAIRewriteMemory::formatUserVisible(card));
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"改稿记忆 · 本地 · 主文档未改"_ustr);
        return;
    }
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"暂无待批计划"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnChatApproveClicked, weld::Button&, void)
{
    if (m_bHasPendingPlan)
        ApplyPendingPlanWithApproval();
    LoadArtifactNavigator();
    LoadReviewQueue();
    UpdateActions();
}

IMPL_LINK_NOARG(AIChatPanel, OnChatDiffClicked, weld::Button&, void)
{
    // Single audit chain: 查看 Diff always surfaces DiffReview for pending plan,
    // or opens artifact Diff when a content item is selected (Cowork-style).
    if (m_bHasPendingPlan)
    {
        PresentPendingPlanForApproval(u"chat-diff"_ustr);
        return;
    }
    ShowReviewTab();
    if (FindSelectedArtifact() && m_xOpenDiffReviewButton)
        OnOpenDiffReviewClicked(*m_xOpenDiffReviewButton);
    else if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"暂无待批计划 · 请先生成建议，或在「内容」选产物后查看 Diff"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnChatRejectClicked, weld::Button&, void)
{
    if (m_xRejectSelectedButton)
        OnRejectSelectedClicked(*m_xRejectSelectedButton);
}

bool AIChatPanel::PerformLastApplyUndo()
{
    if (!m_bLastApplyCanUndo)
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"暂无可撤销的 AI 写回"_ustr);
        AppendTranscript(u"System"_ustr,
                         u"undo-ai-apply · 跳过 · 当前没有可撤销的 AI 写回会话标记"_ustr,
                         /*bPersistHistory*/ false);
        return false;
    }
    SfxViewFrame* pFrame = SfxViewFrame::Current();
    if (!pFrame)
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"无法撤销：无活动视图"_ustr);
        return false;
    }
    SfxDispatcher* pDisp = pFrame->GetDispatcher();
    if (!pDisp)
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"无法撤销：无命令分发器"_ustr);
        return false;
    }
    pDisp->Execute(SID_UNDO, SfxCallMode::ASYNCHRON);
    m_bLastApplyCanUndo = false;
    UpdateApprovalChrome();
#if defined(MACOSX)
    const OUString hint = u"⌘Z"_ustr;
#else
    const OUString hint = u"Ctrl+Z"_ustr;
#endif
    AppendTranscript(u"System"_ustr,
                     u"undo-ai-apply · 已请求撤销最近一次 AI 写回（"_ustr + hint
                         + u" / 编辑→撤销 / `/撤销写回`）· 若栈已空则文档可能不变"_ustr,
                     /*bPersistHistory*/ false);
    SetAgentStepBar(u"步骤：已请求撤销写回 · 可用编辑→撤销再次确认"_ustr);
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"已请求撤销 AI 写回 · "_ustr + hint);
    AppendAssistantMarkdown(
        u"### 已请求撤销写回\n\n"
        u"已通过系统撤销栈回退最近一次 AI 写回。若效果不符，可用编辑→撤销/重做微调。\n"
        u"主文档仅随撤销栈变化；未静默上传。\n"_ustr);
    return true;
}

IMPL_LINK_NOARG(AIChatPanel, OnChatUndoClicked, weld::Button&, void)
{
    PerformLastApplyUndo();
}

IMPL_LINK_NOARG(AIChatPanel, OnLocateRagClicked, weld::Button&, void)
{
    kqoffice::ai::chat::LocalRagLocateResult loc;
    if (!m_sLastRagPosition.isEmpty())
        loc = kqoffice::ai::chat::DocumentAILocalRag::locatePosition(m_sLastRagPosition);
    if (!loc.success && !m_sLastRagQuery.isEmpty())
        loc = kqoffice::ai::chat::DocumentAILocalRag::locateFirstHit(m_sLastRagQuery);
    if (!loc.success && m_sLastRagQuery.isEmpty() && m_sLastRagPosition.isEmpty())
    {
        loc.message = u"请先使用「问本文档」得到出处，再点定位"_ustr;
    }
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(loc.message);
    AppendTranscript(u"System"_ustr,
                     (loc.success ? u"locate-ok "_ustr : u"locate-failed "_ustr)
                         + (loc.position.isEmpty() ? OUString() : (u"pos="_ustr + loc.position + u" · "_ustr))
                         + loc.message,
                     /*bPersistHistory*/ false);
    if (loc.success && !loc.position.isEmpty())
        m_sLastRagPosition = loc.position;
}

void AIChatPanel::PresentModelHealthGuidance(bool bOpenConfigDir, const OUString& rFailDetail)
{
    const kqoffice::ai::ModelRoutingDiagnostics d = kqoffice::ai::diagnoseModelRouting();
    const OUString guide
        = kqoffice::ai::formatModelHealthRecoveryGuide(d, rFailDetail);
    AppendTranscript(
        u"System"_ustr,
        u"model-health issue="_ustr
            + (d.issueCode.isEmpty() ? u"unknown"_ustr : d.issueCode)
            + u" healthy="_ustr + (d.healthy ? u"true"_ustr : u"false"_ustr)
            + u" main-document-mutation=false"_ustr,
        /*bPersistHistory*/ false);
    if (!guide.isEmpty())
        AppendAssistantMarkdown(guide);
    if (m_xStatusLabel)
    {
        if (d.healthy)
            m_xStatusLabel->set_label(u"模型就绪 · 可继续对话 · 主文档未改"_ustr);
        else if (d.issueCode == u"missing-key"_ustr)
            m_xStatusLabel->set_label(u"缺 API Key · 见修复步骤 · 主文档未改"_ustr);
        else if (d.issueCode == u"ollama-offline"_ustr)
            m_xStatusLabel->set_label(u"Ollama 离线 · 见修复步骤 · 主文档未改"_ustr);
        else if (d.issueCode == u"gateway-offline"_ustr)
            m_xStatusLabel->set_label(u"网关离线 · 见修复步骤 · 主文档未改"_ustr);
        else
            m_xStatusLabel->set_label(u"模型未就绪 · 见修复步骤 · 主文档未改"_ustr);
    }
    if (m_xRoutingDiagBtn)
    {
        m_xRoutingDiagBtn->set_label(d.healthy ? u"模型诊断"_ustr : u"修复模型"_ustr);
        m_xRoutingDiagBtn->set_tooltip_text(
            d.healthy ? u"探测网关/Ollama 与五槽解析"_ustr
                      : u"打开模型修复步骤（Key / Ollama / 网关）；不改主文档"_ustr);
    }

    if (bOpenConfigDir)
    {
        // Best-effort: open local config folder so user can drop api-key (macOS/Linux).
        const OUString dir = kqoffice::ai::kqofficeAiConfigDir();
        OUString url;
        if (osl::FileBase::getFileURLFromSystemPath(dir, url) == osl::FileBase::E_None)
        {
            // Ensure directory exists so Finder/file manager has a target.
            osl::Directory::createPath(url);
#if defined(MACOSX)
            const OString sys = OUStringToOString(dir, RTL_TEXTENCODING_UTF8);
            const OString cmd = "open \"" + sys + "\" 2>/dev/null &";
            (void)std::system(cmd.getStr());
#elif defined(LINUX) || defined(FREEBSD) || defined(NETBSD) || defined(OPENBSD) \
    || defined(DRAGONFLY)
            const OString sys = OUStringToOString(dir, RTL_TEXTENCODING_UTF8);
            const OString cmd = "xdg-open \"" + sys + "\" 2>/dev/null &";
            (void)std::system(cmd.getStr());
#else
            (void)url;
#endif
            AppendTranscript(u"System"_ustr,
                             u"已尝试打开配置目录："_ustr + dir
                                 + u" · 放入 api-key 后点「模型诊断」 · 主文档未改"_ustr,
                             /*bPersistHistory*/ false);
        }
    }
}

void AIChatPanel::RunRoutingDiagnostics(bool bAppendTranscript)
{
    const kqoffice::ai::ModelRoutingDiagnostics d = kqoffice::ai::diagnoseModelRouting();

    // M6: local Skills inventory = DocumentAI scenarios (no cloud marketplace).
    const auto skillsCat = kqoffice::ai::chat::DocumentAIScenarioStore::load();
    const sal_Int32 nSkills = static_cast<sal_Int32>(skillsCat.items.size());
    sal_Int32 nPinned = 0;
    OUString skillsList;
    for (const auto& s : skillsCat.items)
    {
        if (s.id.isEmpty())
            continue;
        if (s.options.pinned)
            ++nPinned;
        if (skillsList.getLength() < 400)
        {
            if (!skillsList.isEmpty())
                skillsList += u" · "_ustr;
            skillsList += s.titleZh.isEmpty() ? s.id : s.titleZh;
            if (!s.capabilityHint.isEmpty())
                skillsList += u"("_ustr + s.capabilityHint + u")"_ustr;
        }
    }
    if (skillsList.getLength() >= 400)
        skillsList += u"…"_ustr;

    if (m_xRoutingDiagLabel)
    {
        OUString shortLabel;
        const OUString be = d.backend.toAsciiLowerCase();
        const bool bOpenAI = be.indexOf(u"openai"_ustr) >= 0 || be == u"openai-compatible"_ustr
                             || be == u"openai-compat"_ustr || be == u"openai_compatible"_ustr;
        if (bOpenAI)
        {
            if (!d.gatewayReachable)
                shortLabel = u"模型：网关离线"_ustr;
            else if (!d.apiKeyPresent)
                shortLabel = u"模型：缺 API Key"_ustr;
            else if (d.healthy)
                shortLabel = u"模型：就绪 · "_ustr
                             + (d.primaryResolved.isEmpty() ? u"auto"_ustr : d.primaryResolved);
            else
                shortLabel = u"模型：降级 · 检查配置"_ustr;
        }
        else if (!d.ollamaReachable)
            shortLabel = u"模型：Ollama 离线 · 本地技能 "_ustr + OUString::number(nSkills);
        else
        {
            auto slot = [](const OUString& m) {
                return m.isEmpty() ? u"?"_ustr : m;
            };
            shortLabel = u"Ollama 主="_ustr + slot(d.primaryResolved) + u" · 技能"_ustr
                         + OUString::number(nSkills);
        }
        m_xRoutingDiagLabel->set_label(shortLabel);
        OUString tip = d.summaryZh + u"\n本地技能（方案）共 "_ustr + OUString::number(nSkills)
                       + u" · 常用 "_ustr + OUString::number(nPinned);
        if (!skillsList.isEmpty())
            tip += u"\n"_ustr + skillsList;
        tip += u"\nKey："_ustr + d.apiKeyPathHint;
        tip += u"\n路由："_ustr + d.routingConfigPathHint;
        tip += u"\n管理：工具 → 选项 → 可圈 AI · 点「模型诊断/修复模型」看完整步骤"_ustr;
        m_xRoutingDiagLabel->set_tooltip_text(tip);
    }
    if (m_xRoutingDiagBtn)
    {
        m_xRoutingDiagBtn->set_label(d.healthy ? u"模型诊断"_ustr : u"修复模型"_ustr);
        m_xRoutingDiagBtn->set_tooltip_text(
            d.healthy ? u"探测 Ollama/网关与五槽解析；输出修复步骤（不改主文档）"_ustr
                      : u"一键查看修复步骤并打开配置目录（Key/路由）；不改主文档"_ustr);
    }
    if (m_xStatusLabel)
    {
        // Prefer short membership quota chip when official gateway is configured.
        OUString status;
        if (!d.membershipQuotaLineZh.isEmpty())
            status = d.membershipQuotaLineZh;
        else
            status = d.summaryZh.replaceAll(u"\n"_ustr, u" · "_ustr);
        if (status.getLength() > 96)
            status = status.copy(0, 96) + u"…"_ustr;
        m_xStatusLabel->set_label(status);
    }

    // Audit trail: write evidence JSON (local-first, never throws).
    kqoffice::ai::EvidenceRecord rec;
    rec.serviceMode = u"offline"_ustr;
    rec.provider = u"routing-diag light="_ustr
                   + (d.lightResolved.isEmpty() ? u"?"_ustr : d.lightResolved)
                   + u" review="_ustr
                   + (d.reviewResolved.isEmpty() ? u"?"_ustr : d.reviewResolved)
                   + u" primary="_ustr
                   + (d.primaryResolved.isEmpty() ? u"?"_ustr : d.primaryResolved)
                   + u" issue="_ustr + d.issueCode
                   + u" skills="_ustr + OUString::number(nSkills);
    rec.capability = u"background"_ustr;
    rec.status = d.healthy ? u"ok"_ustr
                           : (d.gatewayReachable || d.ollamaReachable ? u"degraded"_ustr
                                                                     : u"provider-error"_ustr);
    rec.requestSizeBytes = 0;
    rec.responseSizeBytes = d.summaryZh.getLength();
    rec.durationMs = 0;
    kqoffice::ai::EvidenceRecorder recorder;
    const OUString evId = recorder.record(rec);

    if (bAppendTranscript)
    {
        // Full recovery card (M16) — actionable, not just probe summary.
        PresentModelHealthGuidance(/*bOpenConfigDir*/ !d.healthy, /*rFailDetail*/ OUString());
        OUString msg = u"本地技能（方案）共 "_ustr + OUString::number(nSkills) + u" 个 · 常用 "_ustr
                       + OUString::number(nPinned);
        if (!skillsList.isEmpty())
            msg += u"\n技能清单："_ustr + skillsList;
        msg += u"\n说明：本地方案 = Skills 可见面（非云端技能市场）"_ustr;
        if (!evId.isEmpty())
            msg += u"\nevidence="_ustr + evId;
        AppendTranscript(u"System"_ustr, msg);
    }
    else if (!evId.isEmpty() && m_xStatusLabel)
    {
        // Quiet path: keep chip short; evidence id only in tooltip.
        if (m_xRoutingDiagLabel)
        {
            OUString tip = m_xRoutingDiagLabel->get_tooltip_text();
            tip += u"\nevidence="_ustr + evId;
            m_xRoutingDiagLabel->set_tooltip_text(tip);
        }
        // Unhealthy on panel open: nudge without flooding transcript.
        if (!d.healthy && m_xRoutingDiagBtn)
            m_xRoutingDiagBtn->set_label(u"修复模型"_ustr);
    }
}

void AIChatPanel::ConsumePendingScenarioRun()
{
    const OUString id
        = kqoffice::ai::chat::DocumentAIScenarioStore::takePendingRun();
    if (id.isEmpty())
        return;
    // Chinese-only status — avoid raw English protocol noise in the panel.
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"正在打开创作方案…"_ustr);
    AppendTranscript(u"系统"_ustr, u"已载入启动中心创作任务，请补充主题后发送。"_ustr,
                     /*bPersistHistory*/ false);
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
    // general 含 PDF 材料 / 问文档等跨表面能力
    setBadge(m_xTabGeneral.get(), u"PDF·通用"_ustr, u"general"_ustr);
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
    // M6: surface which local Skill is invoked (DuMate "调用能力" visibility).
    AppendTranscript(u"System"_ustr,
                     u"调用技能："_ustr + scen.titleZh + u" · id="_ustr + scen.id
                         + u" · 能力="_ustr
                         + (scen.capabilityHint.isEmpty() ? u"chat"_ustr : scen.capabilityHint)
                         + u" · 本地方案（非云市场）"_ustr,
                     /*bPersistHistory*/ false);
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"技能："_ustr + scen.titleZh + u" · 主文档不会自动改"_ustr);
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
    // Writer native engine already surfaces Diff Review via applyDiagnosticsPlan
    // after a successful apply. Pre-approve preview still opens here.
    if (rEngine == u"writer-apply-engine"_ustr && bApplied)
        return;
#if AICHAT_HAVE_DLSYM
    using ShowFn = void (*)(void*, const sal_Unicode*, sal_Int32, const sal_Unicode*, sal_Int32,
                            const sal_Unicode*, sal_Int32, const sal_Unicode*, sal_Int32, sal_Bool);
    void* pSym = dlsym(RTLD_DEFAULT, "kqoffice_show_diff_review");
    if (!pSym || !m_xPromptEntry)
        return;
    auto pFn = reinterpret_cast<ShowFn>(pSym);
    // Prefer first pending op metadata when staging a preview (richer Diff row).
    OUString patchId = u"p1"_ustr;
    OUString kind = u"replace"_ustr;
    if (!bApplied && m_bHasPendingPlan && !m_aPendingPlan.operations.empty())
    {
        const auto& op0 = m_aPendingPlan.operations.front();
        if (!op0.target.isEmpty())
            patchId = op0.target;
        if (!op0.opType.isEmpty())
            kind = op0.opType;
    }
    const OUString status = bApplied ? u"ok"_ustr : u"pending"_ustr;
    pFn(m_xPromptEntry.get(), rPlanId.getStr(), rPlanId.getLength(), patchId.getStr(),
        patchId.getLength(), kind.getStr(), kind.getLength(), status.getStr(), status.getLength(),
        bApplied ? sal_True : sal_False);
#else
    (void)rPlanId;
    (void)bApplied;
#endif
}

bool AIChatPanel::ConfirmComplexAiTaskStart(bool bNeedsConfirm)
{
    if (!bNeedsConfirm)
        return true;

    // Wave D4/M7: DuMate-style clarify card before multi-step / plan tasks.
    // Deny aborts start; AllowOnce proceeds once; AllowSession auto-skips later starts.
    const OUString sDraft
        = m_xPromptEntry ? m_xPromptEntry->get_text().trim() : OUString();
    const bool bShortOrAmbiguous = sDraft.getLength() < 12;

    kqoffice::ai::control::ClarificationPrompt aPrompt;
    aPrompt.actionId = u"ai.task.start"_ustr;
    if (bShortOrAmbiguous)
    {
        aPrompt.messageZh
            = u"指令较简短。将启动多步 AI 任务；主文档在批准前不会被修改。"
              "请勾选适用项后继续（或拒绝取消）："_ustr;
        aPrompt.options = { u"附带选区"_ustr, u"使用文档上下文"_ustr,
                            u"先出计划再执行"_ustr, u"限制在当前文档范围"_ustr };
        aPrompt.optionDefaults = {
            m_xOptAttachSelection && m_xOptAttachSelection->get_active(),
            m_xOptDocContext && m_xOptDocContext->get_active(),
            true, // prefer plan-first for short prompts
            true,
        };
    }
    else
    {
        aPrompt.messageZh
            = u"将启动 AI 任务（多步/规划），主文档在批准前不会被修改。是否继续？"_ustr;
        aPrompt.options = { u"附带选区"_ustr, u"使用文档上下文"_ustr,
                            u"先出计划再执行"_ustr };
        aPrompt.optionDefaults = {
            m_xOptAttachSelection && m_xOptAttachSelection->get_active(),
            m_xOptDocContext && m_xOptDocContext->get_active(),
            m_xOptAgentPipeline && m_xOptAgentPipeline->get_active(),
        };
    }

    const kqoffice::ai::control::ClarificationResult aPerm
        = sfx2::ShowPermissionPrompt(GetFrameWeld(), aPrompt);
    if (aPerm.decision == kqoffice::ai::control::PermissionDecision::Deny)
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"已取消任务启动"_ustr);
        AppendTranscript(u"System"_ustr,
                         u"已取消任务启动 · 未开始多步/规划 · 主文档未改"_ustr,
                         /*bPersistHistory*/ false);
        FocusPrompt();
        return false;
    }

    // Dialog path: sync panel checkboxes / forced capability from clarify options.
    // Session-cache auto-allow keeps existing checkbox state.
    if (!aPerm.fromSessionCache)
    {
        bool bAttach = false;
        bool bDocCtx = false;
        bool bPlanFirst = false;
        bool bDocScope = false;
        for (const sal_Int32 idx : aPerm.selectedOptionIndices)
        {
            if (idx == 0)
                bAttach = true;
            else if (idx == 1)
                bDocCtx = true;
            else if (idx == 2)
                bPlanFirst = true;
            else if (idx == 3)
                bDocScope = true;
        }
        if (m_xOptAttachSelection)
            m_xOptAttachSelection->set_active(bAttach);
        if (m_xOptDocContext)
            m_xOptDocContext->set_active(bDocCtx);
        if (bPlanFirst)
        {
            // Prefer structured work-plan gate (Grok plan-mode analogue), not silent agent.
            m_sForcedCapability = u"plan"_ustr;
            m_bForceWorkPlanOnce = true;
        }
        if (bDocScope && m_xOptDocContext)
            m_xOptDocContext->set_active(true);
    }

    AppendTranscript(
        u"System"_ustr,
        u"task-start-confirmed action=ai.task.start decision="_ustr
            + kqoffice::ai::control::PermissionGrant::decisionLabelZh(aPerm.decision)
            + u" from-session-cache="_ustr
            + (aPerm.fromSessionCache ? u"true"_ustr : u"false"_ustr)
            + u" short-clarify="_ustr + (bShortOrAmbiguous ? u"1"_ustr : u"0"_ustr)
            + u" · 主文档仍须批准后写回"_ustr,
        /*bPersistHistory*/ false);
    return true;
}

void AIChatPanel::SubmitPrompt()
{
    OUString sPrompt = m_xPromptEntry->get_text().trim();
    if (sPrompt.isEmpty())
        return;

    // Snapshot raw utterance before slash/skill expansion (work-plan detection & gate).
    const OUString rawUserPrompt = sPrompt;
    OUString matchedSkillId;
    OUString matchedSkillTitle;
    OUString matchedSkillDesc;
    bool bSkipWorkPlanGate = false;

    // ── Work-plan gate actions (approve / revise / cancel / show) ──
    if (m_bHasWorkPlan)
    {
        using kqoffice::ai::chat::DocumentAIWorkPlan;
        using kqoffice::ai::chat::WorkPlanAction;
        const WorkPlanAction act = DocumentAIWorkPlan::classifyAction(rawUserPrompt);
        // Whitelist: while a plan is pending, still allow memory/diff/undo/slash
        // utilities instead of treating them as revise notes (Stage1 audit P1).
        auto isWorkPlanPassthrough = [&](const OUString& t) -> bool {
            if (t.startsWith(u"/"_ustr))
            {
                if (t.startsWith(u"/diff"_ustr) || t.startsWith(u"/查看差异"_ustr)
                    || t.startsWith(u"/memory"_ustr) || t.startsWith(u"/改稿记忆"_ustr)
                    || t.startsWith(u"/记住"_ustr) || t.startsWith(u"/忘记"_ustr)
                    || t.startsWith(u"/compact"_ustr) || t.startsWith(u"/压缩记忆"_ustr)
                    || t.startsWith(u"/undo-apply"_ustr) || t.startsWith(u"/撤销写回"_ustr)
                    || t.startsWith(u"/connectors"_ustr) || t.startsWith(u"/连接器"_ustr)
                    || t.startsWith(u"/vision"_ustr) || t.startsWith(u"/视觉"_ustr)
                    // Membership: quota / 签到 / 抢包 (no main-doc mutation)
                    || t.startsWith(u"/quota"_ustr) || t.startsWith(u"/会员额度"_ustr)
                    || t == u"/额度"_ustr || t.startsWith(u"/checkin"_ustr)
                    || t.startsWith(u"/签到"_ustr) || t.startsWith(u"/rush"_ustr)
                    || t.startsWith(u"/抢包"_ustr) || t.startsWith(u"/加油包"_ustr)
                    || t.startsWith(u"/vault"_ustr) || t.startsWith(u"/资料盘"_ustr)
                    || t.startsWith(u"/搜资料"_ustr) || t.startsWith(u"/收入资料"_ustr)
                    || t.startsWith(u"/整理资料"_ustr) || t.startsWith(u"/资料体检"_ustr)
                    || t.startsWith(u"/导出资料包"_ustr) || t.startsWith(u"/笔记入库"_ustr)
                    || t.startsWith(u"/新建资料盘"_ustr) || t.startsWith(u"/切换资料盘"_ustr)
                    || t.startsWith(u"/资料盘位置"_ustr) || t.startsWith(u"/授权资料盘"_ustr)
                    || t.startsWith(u"/打开资料盘"_ustr) || t.startsWith(u"/资料盘管理"_ustr)
                    || t.startsWith(u"/资料盘初始化"_ustr) || t.startsWith(u"/撤销资料盘授权"_ustr))
                    return true;
            }
            return false;
        };
        // New free-form turn after a prior approved plan run → drop old gate.
        if (m_bWorkPlanApproved && act == WorkPlanAction::None)
        {
            ClearWorkPlan();
        }
        else if (!m_bWorkPlanApproved && act == WorkPlanAction::None
                 && isWorkPlanPassthrough(rawUserPrompt))
        {
            // Fall through to utility handlers below (diff / memory / undo).
        }
        else if (act == WorkPlanAction::Show)
        {
            AppendAssistantMarkdown(
                m_aWorkPlan.markdown.isEmpty() ? DocumentAIWorkPlan::formatMarkdown(m_aWorkPlan)
                                               : m_aWorkPlan.markdown);
            if (m_xStatusLabel)
                m_xStatusLabel->set_label(
                    m_bWorkPlanApproved ? u"工作计划（已确认）"_ustr
                                        : u"工作计划（待确认）· 主文档未改"_ustr);
            m_xPromptEntry->set_text(OUString());
            return;
        }
        else if (act == WorkPlanAction::Cancel)
        {
            AppendTranscript(u"System"_ustr,
                             u"已取消工作计划 "_ustr + m_aWorkPlan.planId + u" · 主文档未改"_ustr,
                             /*bPersistHistory*/ false);
            ClearWorkPlan();
            if (m_xStatusLabel)
                m_xStatusLabel->set_label(u"工作计划已取消 · 主文档未改"_ustr);
            m_xPromptEntry->set_text(OUString());
            return;
        }
        else if (act == WorkPlanAction::Revise)
        {
            kqoffice::ai::chat::WorkPlanInput win;
            win.userPrompt = m_aWorkPlan.originalPrompt;
            win.surface = CurrentDocumentSurface();
            win.skillId = m_aWorkPlan.skillId;
            win.skillTitleZh = m_aWorkPlan.skillTitleZh;
            win.prior = &m_aWorkPlan;
            win.reviseNotes = DocumentAIWorkPlan::extractReviseNotes(rawUserPrompt);
            if (win.reviseNotes.isEmpty())
                win.reviseNotes = rawUserPrompt; // free-form revise text
            {
                const auto sel = kqoffice::ai::chat::AgentChatSelectionCapture::captureCurrent();
                win.hasSelection = !sel.text.isEmpty();
                win.selectionChars = sel.length;
            }
            PresentWorkPlan(DocumentAIWorkPlan::build(win));
            m_xPromptEntry->set_text(OUString());
            return;
        }
        else if (act == WorkPlanAction::Approve)
        {
            m_bWorkPlanApproved = true;
            UpdateWorkPlanChip();
            // Resume generation from stored skill-expanded seed + plan contract.
            OUString seed = m_aWorkPlan.executionSeed;
            if (seed.isEmpty())
                seed = m_aWorkPlan.originalPrompt;
            sPrompt = DocumentAIWorkPlan::applyContractToPrompt(seed, m_aWorkPlan);
            m_xPromptEntry->set_text(sPrompt);
            bSkipWorkPlanGate = true;
            if (m_xHistoryStore)
            {
                auto mc = kqoffice::ai::chat::DocumentAIRewriteMemory::load(
                    m_xHistoryStore->GetDocumentKey());
                // Only user revise notes → memory (not default scopeOut boilerplate).
                kqoffice::ai::chat::DocumentAIRewriteMemory::ingestWorkPlanNotes(
                    mc, /*scopeOut*/ OUString(), m_aWorkPlan.reviseNotes,
                    m_aWorkPlan.objective);
                if (!m_aWorkPlan.skillId.isEmpty())
                    kqoffice::ai::chat::DocumentAIRewriteMemory::noteSkill(
                        mc, m_aWorkPlan.skillId, m_aWorkPlan.skillTitleZh);
                kqoffice::ai::chat::DocumentAIRewriteMemory::compact(
                    mc, m_xHistoryStore->FormatRecentTurns(4, 1200));
                kqoffice::ai::chat::DocumentAIRewriteMemory::save(mc);
            }
            AppendTranscript(u"System"_ustr,
                             u"工作计划已确认 "_ustr + m_aWorkPlan.planId
                                 + u" · 开始生成草案 · 写回仍须批准 · 主文档未改"_ustr,
                             /*bPersistHistory*/ false);
            if (m_xStatusLabel)
                m_xStatusLabel->set_label(u"计划已确认 · 生成草案中… · 主文档未改"_ustr);
            SetAgentStepBar(u"步骤：按已确认计划生成草案…"_ustr);
            // Fall through into normal pipeline (no re-expand of slash on contract body).
        }
        else if (!m_bWorkPlanApproved)
        {
            // Any other free text while plan pending → treat as revise notes.
            kqoffice::ai::chat::WorkPlanInput win;
            win.userPrompt = m_aWorkPlan.originalPrompt;
            win.surface = CurrentDocumentSurface();
            win.skillId = m_aWorkPlan.skillId;
            win.skillTitleZh = m_aWorkPlan.skillTitleZh;
            win.prior = &m_aWorkPlan;
            win.reviseNotes = rawUserPrompt;
            {
                const auto sel = kqoffice::ai::chat::AgentChatSelectionCapture::captureCurrent();
                win.hasSelection = !sel.text.isEmpty();
                win.selectionChars = sel.length;
            }
            PresentWorkPlan(DocumentAIWorkPlan::build(win));
            AppendTranscript(u"System"_ustr,
                             u"已把你的消息当作计划修订意见 · 请再确认或 /approve-plan"_ustr,
                             /*bPersistHistory*/ false);
            m_xPromptEntry->set_text(OUString());
            return;
        }
    }

    // ── Undo last AI apply (same as「撤销写回」button) ──
    {
        const OUString lowRaw = rawUserPrompt.toAsciiLowerCase();
        if (rawUserPrompt.startsWith(u"/undo-apply"_ustr)
            || rawUserPrompt.startsWith(u"/撤销写回"_ustr)
            || rawUserPrompt.startsWith(u"/撤销本次写回"_ustr)
            || rawUserPrompt == u"撤销写回"_ustr || lowRaw == u"undo apply"_ustr
            || lowRaw == u"undo-apply"_ustr)
        {
            PerformLastApplyUndo();
            m_xPromptEntry->set_text(OUString());
            return;
        }
    }

    // ── Rewrite memory slash (/memory /记住 /忘记 /compact) ──
    if (m_xHistoryStore)
    {
        using kqoffice::ai::chat::DocumentAIRewriteMemory;
        using kqoffice::ai::chat::RewriteMemoryAction;
        const RewriteMemoryAction memAct = DocumentAIRewriteMemory::classifyAction(rawUserPrompt);
        if (memAct != RewriteMemoryAction::None)
        {
            const OUString docKey = m_xHistoryStore->GetDocumentKey();
            auto card = DocumentAIRewriteMemory::load(docKey);
            if (memAct == RewriteMemoryAction::Show)
            {
                AppendAssistantMarkdown(DocumentAIRewriteMemory::formatUserVisible(card));
                if (m_xStatusLabel)
                    m_xStatusLabel->set_label(u"改稿记忆 · 本地 · 主文档未改"_ustr);
                m_xPromptEntry->set_text(OUString());
                return;
            }
            if (memAct == RewriteMemoryAction::Remember)
            {
                const OUString fact = DocumentAIRewriteMemory::extractRememberFact(rawUserPrompt);
                if (fact.isEmpty())
                {
                    AppendAssistantMarkdown(
                        u"用法：`/记住 别动金额列` 或 `/记住 保持文号不变`\n"_ustr);
                }
                else
                {
                    DocumentAIRewriteMemory::addConstraint(card, fact);
                    DocumentAIRewriteMemory::compact(
                        card, m_xHistoryStore->FormatRecentTurns(4, 1200));
                    DocumentAIRewriteMemory::save(card);
                    AppendTranscript(u"System"_ustr, u"已写入改稿记忆："_ustr + fact,
                                     /*bPersistHistory*/ false);
                    AppendAssistantMarkdown(DocumentAIRewriteMemory::formatUserVisible(card));
                    UpdatePendingPlanChip();
                }
                m_xPromptEntry->set_text(OUString());
                return;
            }
            if (memAct == RewriteMemoryAction::Forget)
            {
                const OUString target = DocumentAIRewriteMemory::extractForgetTarget(rawUserPrompt);
                // Bare "/忘记" without target: show usage — do NOT wipe all (Stage1 audit).
                if (target.isEmpty()
                    && !rawUserPrompt.startsWith(u"/忘记全部"_ustr)
                    && rawUserPrompt.trim() != u"忘记全部"_ustr
                    && rawUserPrompt.trim() != u"清空记忆"_ustr)
                {
                    AppendAssistantMarkdown(
                        u"用法：\n"
                        u"- `/忘记 金额` — 删除含该关键词的约束\n"
                        u"- `/忘记全部` — 清空本文档全部改稿记忆\n"_ustr);
                    if (m_xStatusLabel)
                        m_xStatusLabel->set_label(u"请指定要忘记的关键词，或 /忘记全部"_ustr);
                    m_xPromptEntry->set_text(OUString());
                    return;
                }
                if (target == u"*"_ustr)
                {
                    DocumentAIRewriteMemory::clear(docKey);
                    AppendTranscript(u"System"_ustr, u"已清空本文档改稿记忆 · 主文档未改"_ustr,
                                     /*bPersistHistory*/ false);
                    AppendAssistantMarkdown(u"改稿记忆已清空（本地）。对话历史仍在；可用「清空历史」一并清除。\n"_ustr);
                }
                else
                {
                    auto filterVec = [&](std::vector<OUString>& v) {
                        std::vector<OUString> next;
                        for (const auto& e : v)
                        {
                            if (e.indexOf(target) < 0)
                                next.push_back(e);
                        }
                        v.swap(next);
                    };
                    filterVec(card.constraints);
                    filterVec(card.corrections);
                    DocumentAIRewriteMemory::compact(
                        card, m_xHistoryStore->FormatRecentTurns(4, 1200));
                    DocumentAIRewriteMemory::save(card);
                    AppendTranscript(u"System"_ustr, u"已从记忆中移除含「"_ustr + target + u"」的项"_ustr,
                                     /*bPersistHistory*/ false);
                    AppendAssistantMarkdown(DocumentAIRewriteMemory::formatUserVisible(card));
                }
                UpdatePendingPlanChip();
                m_xPromptEntry->set_text(OUString());
                return;
            }
            if (memAct == RewriteMemoryAction::Compact)
            {
                DocumentAIRewriteMemory::compact(
                    card, m_xHistoryStore->FormatRecentTurns(6, 2000));
                DocumentAIRewriteMemory::save(card);
                AppendTranscript(u"System"_ustr, u"已压缩改稿记忆 · 本地"_ustr,
                                 /*bPersistHistory*/ false);
                AppendAssistantMarkdown(DocumentAIRewriteMemory::formatUserVisible(card));
                UpdatePendingPlanChip();
                m_xPromptEntry->set_text(OUString());
                return;
            }
        }
    }

    // Re-entrant append (send while previous SubmitPrompt still on stack via Reschedule):
    // cancel current run and queue the new prompt; outer call finishes then restarts.
    // D2 stop/append: do not show start-confirm here — only queue + stop current run.
    if (m_bSubmitInFlight)
    {
        m_sQueuedReplacePrompt = sPrompt;
        m_xPromptEntry->set_text(OUString());
        m_bCancelRequested = true;
        m_bAgentRunActive = false;
        if (kqoffice::ai::cowork::hasActiveTaskRunner())
            kqoffice::ai::cowork::cancelActiveTaskRunner();
        MarkAgentStepsStopped();
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"已终止上一任务，开始新任务"_ustr);
        AppendTranscript(u"System"_ustr,
                         u"已终止上一任务，开始新任务 · 主文档未改"_ustr,
                         /*bPersistHistory*/ false);
        return;
    }

    // Non-reentrant busy state — cancel previous then start replacement.
    if (IsRunBusy())
    {
        StopActiveRun(u"已终止上一任务，开始新任务"_ustr, /*bFromAppend*/ true);
    }
    m_bCancelRequested = false;

    m_bSubmitInFlight = true;
    auto aSubmitGuard = [this](void*) {
        m_bSubmitInFlight = false;
        m_bAgentRunActive = false;
        if (m_sQueuedReplacePrompt.isEmpty())
            return;
        const OUString queued = m_sQueuedReplacePrompt;
        m_sQueuedReplacePrompt.clear();
        m_bCancelRequested = false;
        m_xPromptEntry->set_text(queued);
        SubmitPrompt();
    };
    std::unique_ptr<void, decltype(aSubmitGuard)> xSubmitScope(reinterpret_cast<void*>(1),
                                                              aSubmitGuard);

    // Expand scenario slash / NL skills before send (skip when resuming approved work plan).
    if (!bSkipWorkPlanGate)
    {
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
            matchedSkillId = scen.id;
            matchedSkillTitle = scen.titleZh;
            matchedSkillDesc = scen.description;
            if (m_xHistoryStore)
            {
                auto mc = kqoffice::ai::chat::DocumentAIRewriteMemory::load(
                    m_xHistoryStore->GetDocumentKey());
                kqoffice::ai::chat::DocumentAIRewriteMemory::noteSkill(mc, matchedSkillId,
                                                                       matchedSkillTitle);
                kqoffice::ai::chat::DocumentAIRewriteMemory::save(mc);
            }
            // Slash path also binds review/light slots via capabilityHint.
            if (m_sForcedCapability.isEmpty())
                m_sForcedCapability = kqoffice::ai::normalizeCapabilityHint(scen.capabilityHint);
            AppendTranscript(u"System"_ustr,
                             u"调用技能："_ustr + scen.titleZh + u" · id="_ustr + scen.id
                                 + u" · 斜杠入口 · 主文档不会自动改"_ustr,
                             /*bPersistHistory*/ false);
            if (m_xStatusLabel)
                m_xStatusLabel->set_label(u"技能："_ustr + scen.titleZh + u" · 主文档不会自动改"_ustr);
        }
        else
        {
            // Grok-style skill auto-match: free-form Chinese/English → quality skill pack.
            const auto skillCat = kqoffice::ai::chat::DocumentAIScenarioStore::load();
            sal_Int32 skillScore = 0;
            const kqoffice::ai::chat::DocumentAIScenario* pSkill
                = kqoffice::ai::chat::DocumentAIScenarioStore::matchNaturalLanguage(
                    skillCat, sPrompt, CurrentDocumentSurface(), &skillScore);
            if (pSkill && !pSkill->id.isEmpty() && !pSkill->promptTemplate.isEmpty())
            {
                const OUString userUtterance = sPrompt;
                const kqoffice::ai::chat::SelectionContext sel
                    = kqoffice::ai::chat::AgentChatSelectionCapture::captureCurrent();
                const OUString selText
                    = pSkill->options.attachSelection ? sel.text : OUString();
                sPrompt = kqoffice::ai::chat::DocumentAIScenarioStore::expandSkillWithUtterance(
                    *pSkill, selText, userUtterance);
                m_xPromptEntry->set_text(sPrompt);
                matchedSkillId = pSkill->id;
                matchedSkillTitle = pSkill->titleZh;
                matchedSkillDesc = pSkill->description;
                if (m_xHistoryStore)
                {
                    auto mc = kqoffice::ai::chat::DocumentAIRewriteMemory::load(
                        m_xHistoryStore->GetDocumentKey());
                    kqoffice::ai::chat::DocumentAIRewriteMemory::noteSkill(mc, matchedSkillId,
                                                                           matchedSkillTitle);
                    kqoffice::ai::chat::DocumentAIRewriteMemory::save(mc);
                }
                if (m_sForcedCapability.isEmpty())
                    m_sForcedCapability
                        = kqoffice::ai::normalizeCapabilityHint(pSkill->capabilityHint);
                AppendTranscript(
                    u"System"_ustr,
                    u"已匹配技能："_ustr + pSkill->titleZh + u" · id="_ustr + pSkill->id
                        + u" · 匹配分="_ustr + OUString::number(skillScore)
                        + u" · 自然语言 · 主文档不会自动改"_ustr,
                    /*bPersistHistory*/ false);
                if (m_xStatusLabel)
                    m_xStatusLabel->set_label(u"技能："_ustr + pSkill->titleZh
                                              + u" · 主文档不会自动改"_ustr);
            }
        }
    }

    // Ingest raw user utterance into per-document rewrite memory (not skill-expanded text).
    if (m_xHistoryStore && !bSkipWorkPlanGate)
    {
        using kqoffice::ai::chat::DocumentAIRewriteMemory;
        auto mc = DocumentAIRewriteMemory::load(m_xHistoryStore->GetDocumentKey());
        DocumentAIRewriteMemory::ingestUserTurn(mc, rawUserPrompt);
        if (!matchedSkillId.isEmpty())
            DocumentAIRewriteMemory::noteSkill(mc, matchedSkillId, matchedSkillTitle);
        if (mc.dirty)
            DocumentAIRewriteMemory::save(mc);
    }

    if (!ValidateContextMentions(sPrompt))
        return;

    // ── 资料盘 (local vault; never mutates main doc) ──
    if (sPrompt.startsWith(u"/vault"_ustr) || sPrompt.startsWith(u"/资料盘"_ustr)
        || sPrompt.startsWith(u"/搜资料"_ustr) || sPrompt.startsWith(u"/收入资料"_ustr)
        || sPrompt.startsWith(u"/整理资料"_ustr) || sPrompt.startsWith(u"/资料体检"_ustr)
        || sPrompt.startsWith(u"/导出资料包"_ustr) || sPrompt.startsWith(u"/笔记入库"_ustr)
        || sPrompt.startsWith(u"/资料盘状态"_ustr) || sPrompt.startsWith(u"/资料盘重建索引"_ustr)
        || sPrompt.startsWith(u"/新建资料盘"_ustr) || sPrompt.startsWith(u"/切换资料盘"_ustr)
        || sPrompt.startsWith(u"/资料盘位置"_ustr) || sPrompt.startsWith(u"/授权资料盘"_ustr)
        || sPrompt.startsWith(u"/打开资料盘"_ustr) || sPrompt.startsWith(u"/资料盘管理"_ustr)
        || sPrompt.startsWith(u"/资料盘初始化"_ustr) || sPrompt.startsWith(u"/撤销资料盘授权"_ustr)
        || sPrompt.startsWith(u"/vault-manage"_ustr) || sPrompt.startsWith(u"/vault-create"_ustr)
        || sPrompt.startsWith(u"/vault-switch"_ustr) || sPrompt.startsWith(u"/vault-location"_ustr)
        || sPrompt.startsWith(u"/vault-auth"_ustr) || sPrompt.startsWith(u"/vault-open"_ustr)
        || sPrompt.startsWith(u"/vault-init"_ustr))
    {
        using kqoffice::ai::vault::VaultStore;
        using kqoffice::ai::vault::VaultManager;
        using kqoffice::ai::vault::VaultIngest;
        using kqoffice::ai::vault::VaultCompile;
        using kqoffice::ai::vault::VaultLint;
        using kqoffice::ai::vault::VaultPack;

        // Install defaults + permission seed (idempotent; WPS/Quark path style)
        (void)VaultManager::ensureInstallDefaults();
        VaultStore::ensureLayout();
        OUString md;

        if (sPrompt.startsWith(u"/vault-manage"_ustr) || sPrompt.startsWith(u"/资料盘管理"_ustr))
        {
            md = VaultManager::managementSummaryZh();
        }
        else if (sPrompt.startsWith(u"/vault-init"_ustr) || sPrompt.startsWith(u"/资料盘初始化"_ustr))
        {
            // Force re-seed by ensuring layout + re-authorize default
            const auto ir = VaultManager::ensureInstallDefaults();
            const auto ar = VaultManager::authorizeVault(OUString());
            md = ir.messageZh + u"\n"_ustr + ar.messageZh;
        }
        else if (sPrompt.startsWith(u"/vault-create"_ustr) || sPrompt.startsWith(u"/新建资料盘"_ustr))
        {
            OUString arg;
            if (sPrompt.startsWith(u"/vault-create"_ustr))
                arg = sPrompt.copy(OUString(u"/vault-create"_ustr).getLength()).trim();
            else
                arg = sPrompt.copy(OUString(u"/新建资料盘"_ustr).getLength()).trim();
            OUString name;
            OUString path;
            const sal_Int32 bar = arg.indexOf(u'|');
            if (bar >= 0)
            {
                name = arg.copy(0, bar).trim();
                path = arg.copy(bar + 1).trim();
            }
            else if (arg.startsWith(u"/"_ustr) || arg.startsWith(u"~"_ustr))
                path = arg;
            else
                name = arg;
            // expand ~
            if (path.startsWith(u"~/"_ustr))
            {
                const char* home = std::getenv("HOME");
                if (home && *home)
                    path = OUString::fromUtf8(home) + path.copy(1);
            }
            const auto cr = VaultManager::createVault(name, path);
            md = cr.messageZh;
        }
        else if (sPrompt.startsWith(u"/vault-switch"_ustr) || sPrompt.startsWith(u"/切换资料盘"_ustr))
        {
            OUString id;
            if (sPrompt.startsWith(u"/vault-switch"_ustr))
                id = sPrompt.copy(OUString(u"/vault-switch"_ustr).getLength()).trim();
            else
                id = sPrompt.copy(OUString(u"/切换资料盘"_ustr).getLength()).trim();
            if (id.isEmpty())
                md = u"用法：`/切换资料盘 <id>`\n先 `/资料盘管理` 查看 id。\n"_ustr;
            else
                md = VaultManager::switchVault(id).messageZh;
        }
        else if (sPrompt.startsWith(u"/vault-location"_ustr) || sPrompt.startsWith(u"/资料盘位置"_ustr))
        {
            OUString path;
            if (sPrompt.startsWith(u"/vault-location"_ustr))
                path = sPrompt.copy(OUString(u"/vault-location"_ustr).getLength()).trim();
            else
                path = sPrompt.copy(OUString(u"/资料盘位置"_ustr).getLength()).trim();
            if (path.startsWith(u"~/"_ustr))
            {
                const char* home = std::getenv("HOME");
                if (home && *home)
                    path = OUString::fromUtf8(home) + path.copy(1);
            }
            if (path.isEmpty())
                md = u"用法：`/资料盘位置 /新绝对路径`\n类似下载软件修改默认下载目录；**不会自动迁移旧文件**。\n"_ustr;
            else
                md = VaultManager::setVaultLocation(OUString(), path).messageZh;
        }
        else if (sPrompt.startsWith(u"/vault-auth-revoke"_ustr)
                 || sPrompt.startsWith(u"/撤销资料盘授权"_ustr))
        {
            md = VaultManager::revokeVaultAuth(OUString()).messageZh;
        }
        else if (sPrompt.startsWith(u"/vault-auth"_ustr) || sPrompt.startsWith(u"/授权资料盘"_ustr))
        {
            md = VaultManager::authorizeVault(OUString()).messageZh;
        }
        else if (sPrompt.startsWith(u"/vault-open"_ustr) || sPrompt.startsWith(u"/打开资料盘"_ustr))
        {
            md = VaultManager::revealInFileManager(OUString()).messageZh;
        }
        else if (sPrompt.startsWith(u"/vault-search"_ustr) || sPrompt.startsWith(u"/搜资料"_ustr))
        {
            OUString q;
            if (sPrompt.startsWith(u"/vault-search"_ustr))
                q = sPrompt.copy(OUString(u"/vault-search"_ustr).getLength()).trim();
            else
                q = sPrompt.copy(OUString(u"/搜资料"_ustr).getLength()).trim();
            if (q.isEmpty())
                md = u"用法：`/搜资料 <关键词>`\n本地资料盘全文检索 · 主文档未改\n"_ustr;
            else
            {
                const auto sr = AIChatVaultSearch(q, 8);
                md = u"## 资料盘检索\n\n"_ustr + sr.MessageZh + u"\n\n"_ustr;
                if (!sr.PromptBlock.isEmpty())
                    md += sr.PromptBlock;
                else
                {
                    for (const auto& h : sr.Hits)
                    {
                        md += u"- **"_ustr + h.Title + u"**\n  "_ustr + h.Snippet + u"\n"_ustr;
                    }
                }
            }
        }
        else if (sPrompt.startsWith(u"/vault-ingest"_ustr) || sPrompt.startsWith(u"/收入资料"_ustr))
        {
            OUString path;
            if (sPrompt.startsWith(u"/vault-ingest"_ustr))
                path = sPrompt.copy(OUString(u"/vault-ingest"_ustr).getLength()).trim();
            else
                path = sPrompt.copy(OUString(u"/收入资料"_ustr).getLength()).trim();
            // allow @文件:path
            if (path.startsWith(u"@文件:"_ustr))
                path = path.copy(OUString(u"@文件:"_ustr).getLength()).trim();
            if (path.isEmpty())
                md = u"用法：`/收入资料 /绝对路径/文件或文件夹`\n"_ustr;
            else
            {
                const auto ir = VaultIngest::ingestPath(path);
                md = ir.messageZh + u"\n"_ustr;
                if (ir.ok && !ir.snippetPath.isEmpty())
                {
                    const auto body
                        = kqoffice::ai::chat::DocumentAIMaterialReader::extractPath(ir.snippetPath,
                                                                                    48000);
                    const auto ix = AIChatVaultIndexPath(
                        ir.snippetPath, body.text.isEmpty() ? ir.title : body.text);
                    md += ix.MessageZh + u"\n"_ustr;
                }
            }
        }
        else if (sPrompt.startsWith(u"/vault-reindex"_ustr)
                 || sPrompt.startsWith(u"/资料盘重建索引"_ustr))
        {
            const auto ix = AIChatVaultReindexAll();
            md = ix.MessageZh;
        }
        else if (sPrompt.startsWith(u"/vault-compile"_ustr) || sPrompt.startsWith(u"/整理资料"_ustr))
        {
            const auto cr = VaultCompile::compilePending(OUString(), 8);
            md = cr.messageZh + u"\n\n整理完成后可 `/资料盘重建索引` 刷新检索。\n"_ustr;
            if (cr.ok && cr.processed > 0)
                (void)AIChatVaultReindexAll();
        }
        else if (sPrompt.startsWith(u"/vault-lint"_ustr) || sPrompt.startsWith(u"/资料体检"_ustr))
        {
            const auto lr = VaultLint::run();
            md = lr.messageZh;
        }
        else if (sPrompt.startsWith(u"/vault-pack"_ustr) || sPrompt.startsWith(u"/导出资料包"_ustr))
        {
            OUString title;
            if (sPrompt.startsWith(u"/vault-pack"_ustr))
                title = sPrompt.copy(OUString(u"/vault-pack"_ustr).getLength()).trim();
            else
                title = sPrompt.copy(OUString(u"/导出资料包"_ustr).getLength()).trim();
            const auto pr = VaultPack::exportThemePack(title);
            md = pr.messageZh;
        }
        else if (sPrompt.startsWith(u"/vault-notebook"_ustr) || sPrompt.startsWith(u"/笔记入库"_ustr))
        {
            const sal_Int32 n = VaultIngest::ingestNotebookMaterials(OUString(), 100);
            const auto ix = AIChatVaultReindexAll();
            md = u"已从可圈笔记收入 "_ustr + OUString::number(n) + u" 条材料\n"_ustr + ix.MessageZh;
        }
        else
        {
            // /vault /资料盘 /vault-status
            const auto act = VaultManager::activeVault();
            md = u"## 资料盘\n\n"_ustr;
            md += VaultManager::statusChipZh() + u"\n\n"_ustr;
            md += u"**当前**：「"_ustr + act.name + u"」\n"_ustr;
            md += u"- 路径：`"_ustr + act.rootPath + u"`\n"_ustr;
            md += u"- 权限："_ustr
                  + (VaultManager::isVaultAuthorized(act.id) ? u"已授权"_ustr : u"未授权"_ustr)
                  + u"\n\n"_ustr;
            md += u"**收录与检索**\n"_ustr;
            md += u"- `/收入资料 <路径>` · `/搜资料 <关键词>`\n"_ustr;
            md += u"- `/整理资料` · `/资料盘重建索引` · `/笔记入库`\n"_ustr;
            md += u"- `/资料体检` · `/导出资料包 [标题]`\n\n"_ustr;
            md += u"**管理（路径/权限，类迅雷·WPS）**\n"_ustr;
            md += u"- `/资料盘管理` — 全部盘与命令\n"_ustr;
            md += u"- `/新建资料盘 名称 | /路径` · `/切换资料盘 <id>`\n"_ustr;
            md += u"- `/资料盘位置 /新路径` · `/授权资料盘` · `/打开资料盘`\n"_ustr;
            md += u"- `/资料盘初始化` — 安装默认位置\n\n"_ustr;
            md += u"仅授权所选目录，非整盘。主文档写回仍须批准。\n"_ustr;
            const auto recent = VaultIngest::listRecentTitles(OUString(), 8);
            if (!recent.empty())
            {
                md += u"\n**最近收录**\n"_ustr;
                for (const auto& t : recent)
                    md += u"- "_ustr + t + u"\n"_ustr;
            }
        }

        AppendAssistantMarkdown(md);
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(VaultManager::statusChipZh());
        m_xPromptEntry->set_text(OUString());
        return;
    }

    // ── Membership quota / 加油包 (api.03122.com; never mutates main doc) ──
    if (sPrompt.startsWith(u"/quota"_ustr) || sPrompt.startsWith(u"/会员额度"_ustr)
        || sPrompt == u"/额度"_ustr || sPrompt.startsWith(u"/checkin"_ustr)
        || sPrompt.startsWith(u"/签到"_ustr) || sPrompt.startsWith(u"/rush"_ustr)
        || sPrompt.startsWith(u"/抢包"_ustr) || sPrompt.startsWith(u"/加油包"_ustr))
    {
        OUString action = u"status"_ustr;
        if (sPrompt.startsWith(u"/checkin"_ustr) || sPrompt.startsWith(u"/签到"_ustr))
            action = u"checkin"_ustr;
        else if (sPrompt.startsWith(u"/rush"_ustr) || sPrompt.startsWith(u"/抢包"_ustr))
            action = u"rush_grab"_ustr;

        const kqoffice::ai::MembershipBoostResult br
            = kqoffice::ai::membershipBoostAction(action);
        AppendTranscript(u"System"_ustr, br.messageZh, /*bPersistHistory*/ false);
        AppendAssistantMarkdown(br.messageZh);
        // Refresh status chip after boost mutation
        if (m_xStatusLabel)
        {
            const OUString chip = kqoffice::ai::membershipQuotaChipZh();
            if (!chip.isEmpty())
                m_xStatusLabel->set_label(chip);
            else
                m_xStatusLabel->set_label(br.ok ? u"会员操作完成"_ustr : u"会员操作失败"_ustr);
        }
        m_xPromptEntry->set_text(OUString());
        return;
    }

    // ── Diff one-shot (pending plan → Diff review; no mutation) ──
    if (sPrompt.startsWith(u"/diff"_ustr) || sPrompt.startsWith(u"/查看差异"_ustr)
        || sPrompt.startsWith(u"/查看Diff"_ustr) || sPrompt == u"/差异"_ustr)
    {
        if (m_bHasPendingPlan)
        {
            PresentPendingPlanForApproval(u"slash-diff"_ustr);
            if (m_xStatusLabel)
                m_xStatusLabel->set_label(u"已打开 Diff · 主文档未改 · 请批准或拒绝"_ustr);
        }
        else
        {
            AppendAssistantMarkdown(
                u"当前没有待批写回计划。\n"
                u"先做 **校对 / 排版优化 / 公式助手** 等生成可写回建议，"
                u"或点「查看 Diff」。主文档默认不改。\n"_ustr);
            if (m_xStatusLabel)
                m_xStatusLabel->set_label(u"暂无待批计划 · 主文档未改"_ustr);
        }
        m_xPromptEntry->set_text(OUString());
        return;
    }

    // ── Vision route status (local only) ──
    if (sPrompt.startsWith(u"/vision-status"_ustr) || sPrompt.startsWith(u"/视觉路由"_ustr))
    {
        const auto prefs = kqoffice::ai::chat::DocumentAIInputPrefs::load();
        const OUString st
            = kqoffice::ai::chat::DocumentAIVisionEvidence::formatVisionRouteStatusZh(
                prefs.visionModel);
        AppendTranscript(u"System"_ustr, st, /*bPersistHistory*/ false);
        AppendAssistantMarkdown(st);
        m_xPromptEntry->set_text(OUString());
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(
                u"Vision 路由 · "_ustr
                + kqoffice::ai::chat::DocumentAIVisionEvidence::resolveLocalVisionModel(
                    prefs.visionModel));
        return;
    }

    // ── Enterprise connectors (default OFF; local status / grant only here) ──
    // /connectors | /连接器  ·  /connector-grant <id>  ·  /connector-revoke <id>
    if (sPrompt.startsWith(u"/connectors"_ustr) || sPrompt.startsWith(u"/连接器"_ustr)
        || sPrompt.startsWith(u"/connector-status"_ustr))
    {
        const OUString st = kqoffice::ai::chat::DocumentAIEnterpriseConnectors::statusSummaryZh();
        AppendTranscript(u"System"_ustr, st, /*bPersistHistory*/ false);
        AppendAssistantMarkdown(st);
        m_xPromptEntry->set_text(OUString());
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"企业连接器状态 · 默认关 · 不静默外联"_ustr);
        return;
    }
    if (sPrompt.startsWith(u"/connector-auth-poll"_ustr)
        || sPrompt.startsWith(u"/连接器登录轮询"_ustr))
    {
        OUString arg;
        if (sPrompt.startsWith(u"/connector-auth-poll"_ustr))
            arg = sPrompt.copy(OUString(u"/connector-auth-poll"_ustr).getLength()).trim();
        else
            arg = sPrompt.copy(OUString(u"/连接器登录轮询"_ustr).getLength()).trim();
        if (arg.isEmpty())
        {
            AppendAssistantMarkdown(u"用法：`/connector-auth-poll <connectorId>`\n"_ustr);
        }
        else
        {
            // Slash itself is explicit user action → approval=true for this interactive path.
            const auto sess
                = kqoffice::ai::chat::DocumentAIEnterpriseConnectors::pollDeviceAuth(arg, true);
            OUString msg = u"设备码轮询 · status="_ustr + sess.status + u"\n"_ustr + sess.messageZh
                           + u"\nnetworkAttempted="_ustr
                           + (sess.networkAttempted ? u"1"_ustr : u"0"_ustr);
            AppendAssistantMarkdown(msg);
            AppendTranscript(u"System"_ustr, msg, /*bPersistHistory*/ false);
        }
        m_xPromptEntry->set_text(OUString());
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"连接器设备码轮询 · 私网门禁"_ustr);
        return;
    }
    if (sPrompt.startsWith(u"/connector-auth"_ustr) || sPrompt.startsWith(u"/连接器登录"_ustr))
    {
        OUString arg;
        if (sPrompt.startsWith(u"/connector-auth"_ustr))
            arg = sPrompt.copy(OUString(u"/connector-auth"_ustr).getLength()).trim();
        else
            arg = sPrompt.copy(OUString(u"/连接器登录"_ustr).getLength()).trim();
        // /connector-auth-poll is handled above (longer prefix first).
        if (arg.isEmpty())
        {
            AppendAssistantMarkdown(
                u"用法：`/connector-auth <connectorId>`\n"
                u"private 网关设备码；验证后 `/connector-auth-poll <id>`。\n"_ustr);
        }
        else
        {
            const auto sess
                = kqoffice::ai::chat::DocumentAIEnterpriseConnectors::startDeviceAuth(arg, true);
            OUStringBuffer md;
            md.append(u"**设备码** · status=`"_ustr);
            md.append(sess.status);
            md.append(u"`\n"_ustr);
            if (!sess.userCode.isEmpty())
            {
                md.append(u"- user_code: `"_ustr);
                md.append(sess.userCode);
                md.append(u"`\n"_ustr);
            }
            const OUString uri = sess.verificationUriComplete.isEmpty() ? sess.verificationUri
                                                                        : sess.verificationUriComplete;
            if (!uri.isEmpty())
            {
                md.append(u"- 打开: "_ustr);
                md.append(uri);
                md.append(u"\n"_ustr);
            }
            md.append(sess.messageZh);
            md.append(u"\n"_ustr);
            AppendAssistantMarkdown(md.makeStringAndClear());
            AppendTranscript(u"System"_ustr, sess.messageZh, /*bPersistHistory*/ false);
        }
        m_xPromptEntry->set_text(OUString());
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"连接器设备码 · 须私网网关"_ustr);
        return;
    }
    if (sPrompt.startsWith(u"/connector-grant"_ustr) || sPrompt.startsWith(u"/连接器授权"_ustr))
    {
        OUString arg;
        if (sPrompt.startsWith(u"/connector-grant"_ustr))
            arg = sPrompt.copy(OUString(u"/connector-grant"_ustr).getLength()).trim();
        else
            arg = sPrompt.copy(OUString(u"/连接器授权"_ustr).getLength()).trim();
        if (arg.isEmpty())
        {
            AppendAssistantMarkdown(
                u"用法：`/connector-grant <connectorId>`\n"
                u"总开关仍须在 工具→选项→可圈 AI 打开（默认关）。授权后仍须操作级批准才可调 private 网关。\n"_ustr);
        }
        else
        {
            const bool ok = kqoffice::ai::chat::DocumentAIEnterpriseConnectors::setGranted(arg, true);
            OUString msg;
            if (ok)
                msg = u"已授权连接器 `"_ustr + arg + u"`（本地 grants）。\n"_ustr
                      + kqoffice::ai::chat::DocumentAIEnterpriseConnectors::statusSummaryZh();
            else
                msg = u"授权失败：`"_ustr + arg + u"`\n"_ustr;
            AppendAssistantMarkdown(msg);
        }
        m_xPromptEntry->set_text(OUString());
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"连接器授权 · 本地 · 主文档未改"_ustr);
        return;
    }
    if (sPrompt.startsWith(u"/connector-revoke"_ustr) || sPrompt.startsWith(u"/连接器撤销"_ustr))
    {
        OUString arg;
        if (sPrompt.startsWith(u"/connector-revoke"_ustr))
            arg = sPrompt.copy(OUString(u"/connector-revoke"_ustr).getLength()).trim();
        else
            arg = sPrompt.copy(OUString(u"/连接器撤销"_ustr).getLength()).trim();
        if (arg.isEmpty())
        {
            AppendAssistantMarkdown(u"用法：`/connector-revoke <connectorId>`\n"_ustr);
        }
        else
        {
            const bool ok
                = kqoffice::ai::chat::DocumentAIEnterpriseConnectors::setGranted(arg, false);
            OUString msg;
            if (ok)
                msg = u"已撤销授权 `"_ustr + arg + u"`\n"_ustr
                      + kqoffice::ai::chat::DocumentAIEnterpriseConnectors::statusSummaryZh();
            else
                msg = u"撤销失败：`"_ustr + arg + u"`\n"_ustr;
            AppendAssistantMarkdown(msg);
        }
        m_xPromptEntry->set_text(OUString());
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"连接器撤销授权 · 本地"_ustr);
        return;
    }

    // ── M11: Local FTS knowledge admin (no Provider / no egress) ──
    // /fts-status | /fts-workspaces | /fts-reindex | /fts-index-materials [paths in prompt]
    if (sPrompt.startsWith(u"/fts-status"_ustr) || sPrompt == u"/索引状态"_ustr)
    {
        AppendTranscript(u"System"_ustr, AIChatKnowledgeFtsEngine::FormatWorkspaceStatusZh(),
                         /*bPersistHistory*/ false);
        AppendAssistantMarkdown(AIChatKnowledgeFtsEngine::FormatWorkspaceStatusZh());
        m_xPromptEntry->set_text(OUString());
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"本地 FTS 状态 · 无外传 · 主文档未改"_ustr);
        return;
    }
    if (sPrompt.startsWith(u"/fts-workspaces"_ustr) || sPrompt.startsWith(u"/工作区列表"_ustr))
    {
        AppendAssistantMarkdown(AIChatKnowledgeFtsEngine::FormatWorkspaceListZh());
        m_xPromptEntry->set_text(OUString());
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"本地 FTS 工作区列表 · 无外传"_ustr);
        return;
    }
    if (sPrompt.startsWith(u"/fts-reindex"_ustr) || sPrompt.startsWith(u"/重建索引"_ustr))
    {
        SetAgentStepBar(u"步骤：工具 · fts-reindex…"_ustr);
        const auto idx = AIChatKnowledgeFtsEngine::ForceReindexOpenDocument();
        AppendTranscript(u"System"_ustr, idx.Message, /*bPersistHistory*/ false);
        AppendAssistantMarkdown(u"重建索引完成：\n"_ustr + idx.Message + u"\n\n"_ustr
                                + AIChatKnowledgeFtsEngine::FormatWorkspaceStatusZh());
        m_xPromptEntry->set_text(OUString());
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(idx.Success ? u"FTS 已重建 · 主文档未改"_ustr
                                                  : u"FTS 重建失败 · 主文档未改"_ustr);
        return;
    }
    if (sPrompt.startsWith(u"/fts-index-materials"_ustr) || sPrompt.startsWith(u"/索引材料"_ustr))
    {
        SetAgentStepBar(u"步骤：工具 · fts-index-materials…"_ustr);
        // Allow trailing paths/mentions after the command.
        OUString materialPrompt = sPrompt;
        if (sPrompt.startsWith(u"/fts-index-materials"_ustr))
            materialPrompt = sPrompt.copy(OUString(u"/fts-index-materials"_ustr).getLength()).trim();
        else if (sPrompt.startsWith(u"/索引材料"_ustr))
            materialPrompt = sPrompt.copy(OUString(u"/索引材料"_ustr).getLength()).trim();
        if (materialPrompt.isEmpty())
            materialPrompt = sPrompt; // use full line if only command
        const auto mat = AIChatKnowledgeFtsEngine::IndexMaterialsFromPrompt(materialPrompt);
        AppendTranscript(u"System"_ustr, mat.Message, /*bPersistHistory*/ false);
        AppendAssistantMarkdown(u"材料索引：\n"_ustr + mat.Message + u"\n\n"_ustr
                                + AIChatKnowledgeFtsEngine::FormatWorkspaceStatusZh());
        m_xPromptEntry->set_text(OUString());
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"材料 FTS 索引完成 · 无外传 · 主文档未改"_ustr);
        return;
    }
    // ── M13: /fts-search (offline FTS, no Provider) ──
    if (sPrompt.startsWith(u"/fts-search"_ustr) || sPrompt.startsWith(u"/检索索引"_ustr))
    {
        SetAgentStepBar(u"步骤：工具 · fts-search…"_ustr);
        OUString query;
        if (sPrompt.startsWith(u"/fts-search"_ustr))
            query = sPrompt.copy(OUString(u"/fts-search"_ustr).getLength()).trim();
        else
            query = sPrompt.copy(OUString(u"/检索索引"_ustr).getLength()).trim();
        if (query.isEmpty())
        {
            AppendAssistantMarkdown(
                u"用法：`/fts-search <关键词>`\n本地 sqlite-fts5 · 无 Provider · 无外传 · 主文档未改\n"_ustr);
            m_xPromptEntry->set_text(OUString());
            return;
        }
        if (!AIChatKnowledgeFtsEngine::IsSqliteAvailable())
        {
            AppendAssistantMarkdown(u"FTS 不可用（sqlite 未就绪）。主文档未改。\n"_ustr);
            m_xPromptEntry->set_text(OUString());
            return;
        }
        const auto search = AIChatKnowledgeFtsEngine::Search(query, 8);
        AppendTranscript(u"System"_ustr, search.Message, /*bPersistHistory*/ false);
        OUStringBuffer md;
        md.append(u"【本地 FTS 检索 · 无 Provider · 无外传】\n"_ustr);
        md.append(search.Message);
        md.append(u"\n\n"_ustr);
        if (search.Success && !search.Hits.empty())
        {
            for (const auto& h : search.Hits)
            {
                md.append(u"**#"_ustr);
                md.append(h.Rank);
                md.append(u"** "_ustr);
                md.append(h.Position.isEmpty() ? h.ChunkId : h.Position);
                md.append(u" · score="_ustr);
                md.append(h.ScoreBasisPoints);
                md.append(u"\n"_ustr);
                md.append(h.Snippet);
                md.append(u"\n\n"_ustr);
            }
            md.append(u"纪律：以上为本地片段；写回仍须 ApplyPlan + 批准；主文档未改。\n"_ustr);
        }
        else
        {
            md.append(u"（无命中。可先 `/fts-reindex` 或发送 `@文件:…` 建索引。）\n"_ustr);
        }
        AppendAssistantMarkdown(md.makeStringAndClear());
        m_xPromptEntry->set_text(OUString());
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"本地 FTS 检索完成 · 无外传 · 主文档未改"_ustr);
        return;
    }
    // ── M13: /propose-replace <new text> (ApplyPlan buffer only) ──
    if (sPrompt.startsWith(u"/propose-replace"_ustr) || sPrompt.startsWith(u"/提议替换"_ustr))
    {
        SetAgentStepBar(u"步骤：工具 · propose_replace_blocks…"_ustr);
        OUString newText;
        if (sPrompt.startsWith(u"/propose-replace"_ustr))
            newText = sPrompt.copy(OUString(u"/propose-replace"_ustr).getLength()).trim();
        else
            newText = sPrompt.copy(OUString(u"/提议替换"_ustr).getLength()).trim();
        if (newText.isEmpty())
        {
            AppendAssistantMarkdown(
                u"用法：选中目标段落后输入 `/propose-replace <新文本>`\n"
                u"仅写入 ApplyPlan 缓冲 · **主文档不改** · 须批准后写回\n"_ustr);
            m_xPromptEntry->set_text(OUString());
            return;
        }
        const auto prop = kqoffice::ai::chat::DocumentAIDocumentTools::proposeReplaceSelection(
            newText, u"slash-propose-replace"_ustr);
        AppendTranscript(u"System"_ustr, prop.summary, /*bPersistHistory*/ false);
        AppendTranscript(u"System"_ustr, prop.message, /*bPersistHistory*/ false);
        if (!prop.success)
        {
            AppendAssistantMarkdown(u"提议替换失败：\n"_ustr + prop.message
                                    + u"\n主文档未改。\n"_ustr);
            m_xPromptEntry->set_text(OUString());
            if (m_xStatusLabel)
                m_xStatusLabel->set_label(u"提议失败 · 主文档未改"_ustr);
            return;
        }
        // Stage into pending ApplyPlan buffer (same path as provider extract).
        kqoffice::ai::chat::ApplyPlan aPlan;
        aPlan.planId = prop.planId;
        kqoffice::ai::chat::DiffOperation op;
        op.opType = prop.opType;
        op.target = prop.target;
        op.oldText = prop.oldText;
        op.newText = prop.newText;
        aPlan.operations.push_back(op);
        aPlan.rawOutput = u"[document-tools propose_replace_blocks] "_ustr + prop.message;
        m_aPendingPlan = std::move(aPlan);
        m_bHasPendingPlan = true;
        m_sPendingEvidenceId = u"evidence:tool-propose:"_ustr + prop.target;
        if (!prop.snapshotHash.isEmpty())
            kqoffice::ai::chat::DocumentAIDocumentTools::markSeen(prop.snapshotHash);
        SetState(AIChatPanelState::AwaitingApproval);
        UpdatePendingPlanChip();
        AppendAssistantMarkdown(
            u"### 提议替换（未写主文档）\n"_ustr + prop.previewSummaryZh + u"\n\n"_ustr
            + u"- 目标：`"_ustr + prop.target + u"`\n"_ustr
            + u"- 快照：`"_ustr
            + (prop.snapshotHash.isEmpty() ? u"(none)"_ustr : prop.snapshotHash) + u"`\n"_ustr
            + u"- 纪律：`mainDocumentMutation=false` · 须在 Diff/批准后写回\n\n"_ustr
            + u"新文本预览：\n\n"_ustr
            + (prop.newText.getLength() > 800 ? prop.newText.copy(0, 800) + u"…"_ustr
                                              : prop.newText)
            + u"\n"_ustr);
        m_xPromptEntry->set_text(OUString());
        // P0-2: auto-open Diff + locate target after slash propose.
        PresentPendingPlanForApproval(u"propose-slash"_ustr);
        return;
    }
    // ── M12: /fts-purge | /fts-watch* (local-only, no Provider) ──
    if (sPrompt.startsWith(u"/fts-purge"_ustr) || sPrompt.startsWith(u"/清理索引"_ustr))
    {
        SetAgentStepBar(u"步骤：工具 · fts-purge…"_ustr);
        OUString arg;
        if (sPrompt.startsWith(u"/fts-purge"_ustr))
            arg = sPrompt.copy(OUString(u"/fts-purge"_ustr).getLength()).trim();
        else
            arg = sPrompt.copy(OUString(u"/清理索引"_ustr).getLength()).trim();
        const auto purged = AIChatKnowledgeFtsEngine::PurgeWorkspace(arg);
        AppendTranscript(u"System"_ustr, purged.Message, /*bPersistHistory*/ false);
        AppendAssistantMarkdown(AIChatKnowledgeFtsEngine::FormatPurgeResultZh(purged));
        m_xPromptEntry->set_text(OUString());
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(purged.Success ? u"FTS 索引已清理 · 主文档未改"_ustr
                                                     : u"FTS 清理失败 · 主文档未改"_ustr);
        return;
    }
    if (sPrompt.startsWith(u"/fts-watch-status"_ustr) || sPrompt.startsWith(u"/监视状态"_ustr))
    {
        AppendAssistantMarkdown(AIChatKnowledgeFtsEngine::FormatWatchStatusZh());
        m_xPromptEntry->set_text(OUString());
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"材料监视状态 · 有界轮询 · 无外传"_ustr);
        return;
    }
    if (sPrompt.startsWith(u"/fts-watch-poll"_ustr) || sPrompt.startsWith(u"/轮询监视"_ustr))
    {
        SetAgentStepBar(u"步骤：工具 · fts-watch-poll…"_ustr);
        const auto polled = AIChatKnowledgeFtsEngine::PollWatchedPaths(OUString(), /*bForce*/ true);
        AppendTranscript(u"System"_ustr, polled.Message, /*bPersistHistory*/ false);
        AppendAssistantMarkdown(u"材料监视轮询：\n"_ustr + polled.Message + u"\n\n"_ustr
                                + AIChatKnowledgeFtsEngine::FormatWatchStatusZh());
        m_xPromptEntry->set_text(OUString());
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"材料监视已轮询 · 主文档未改"_ustr);
        return;
    }
    if (sPrompt.startsWith(u"/fts-watch-clear"_ustr) || sPrompt.startsWith(u"/清空监视"_ustr))
    {
        const auto cleared = AIChatKnowledgeFtsEngine::ClearWatchList();
        AppendTranscript(u"System"_ustr, cleared.Message, /*bPersistHistory*/ false);
        AppendAssistantMarkdown(u"已清空监视列表：\n"_ustr + cleared.Message + u"\n\n"_ustr
                                + AIChatKnowledgeFtsEngine::FormatWatchStatusZh());
        m_xPromptEntry->set_text(OUString());
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"材料监视列表已清空"_ustr);
        return;
    }
    if (sPrompt.startsWith(u"/fts-watch"_ustr) || sPrompt.startsWith(u"/监视材料"_ustr))
    {
        SetAgentStepBar(u"步骤：工具 · fts-watch…"_ustr);
        OUString arg;
        if (sPrompt.startsWith(u"/fts-watch"_ustr))
            arg = sPrompt.copy(OUString(u"/fts-watch"_ustr).getLength()).trim();
        else
            arg = sPrompt.copy(OUString(u"/监视材料"_ustr).getLength()).trim();
        AIChatKnowledgeFtsIndexResult reg;
        if (arg.isEmpty())
        {
            reg.Success = false;
            reg.Message = u"fts-watch-register-failed reason=empty-path "
                          "hint=use-/fts-watch-@文件:path-or-system-path"_ustr;
        }
        else if (arg.indexOf(u'@') >= 0)
        {
            reg = AIChatKnowledgeFtsEngine::RegisterWatchFromPrompt(arg);
            // Also index materials so first poll has a baseline.
            AIChatKnowledgeFtsEngine::IndexMaterialsFromPrompt(arg);
        }
        else
        {
            // Bare path(s): whitespace-separated system paths.
            std::vector<OUString> paths;
            sal_Int32 from = 0;
            while (from < arg.getLength())
            {
                while (from < arg.getLength()
                       && (arg[from] == u' ' || arg[from] == u'\t' || arg[from] == u'\n'))
                    ++from;
                if (from >= arg.getLength())
                    break;
                sal_Int32 to = from;
                while (to < arg.getLength() && arg[to] != u' ' && arg[to] != u'\t'
                       && arg[to] != u'\n')
                    ++to;
                paths.push_back(arg.copy(from, to - from));
                from = to;
            }
            reg = AIChatKnowledgeFtsEngine::RegisterWatchPaths(paths);
            for (const auto& p : paths)
            {
                const auto extracted
                    = kqoffice::ai::chat::DocumentAIMaterialReader::extractPath(p, 16000);
                if (extracted.success && !extracted.text.isEmpty())
                    AIChatKnowledgeFtsEngine::IndexExternalText(p, extracted.text);
            }
        }
        AppendTranscript(u"System"_ustr, reg.Message, /*bPersistHistory*/ false);
        AppendAssistantMarkdown(u"材料监视注册：\n"_ustr + reg.Message + u"\n\n"_ustr
                                + AIChatKnowledgeFtsEngine::FormatWatchStatusZh());
        m_xPromptEntry->set_text(OUString());
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(reg.Success ? u"材料路径已监视 · 有界轮询 · 无外传"_ustr
                                                  : u"材料监视注册失败/溢出 · 主文档未改"_ustr);
        return;
    }

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

    // ── Task Bootstrap Restatement (shengji / semantic start) ────────
    // 先算清需求 → 短复述给人看 → 能力调度 → 再工具/模型（不写主文档）。
    kqoffice::ai::chat::TaskBootstrapInput bootIn;
    bootIn.userPrompt = sPrompt;
    bootIn.surface = CurrentDocumentSurface();
    {
        const auto sel = kqoffice::ai::chat::AgentChatSelectionCapture::captureCurrent();
        bootIn.hasSelection = !sel.text.isEmpty();
        bootIn.selectionChars = sel.length;
        if (bootIn.surface.isEmpty() || bootIn.surface == u"none"_ustr)
            bootIn.surface = sel.surface;
    }
    bootIn.lastPrompt = m_sLastPrompt;
    bootIn.lastRestatement = m_sLastTaskRestatement;
    bootIn.forcedCapability = m_sForcedCapability;
    bootIn.agentCheckbox = m_xOptAgentPipeline && m_xOptAgentPipeline->get_active();
    kqoffice::ai::chat::TaskBootstrapResult boot
        = kqoffice::ai::chat::DocumentAITaskBootstrap::bootstrap(bootIn);

    // Low-confidence Required → light slot refine (shengji: model restatement).
    {
        const auto prefs = kqoffice::ai::chat::DocumentAIInputPrefs::load();
        if (prefs.taskBootstrapModelRefine && boot.wantsModelRefine
            && !boot.modelRefinePrompt.isEmpty()
            && kqoffice::ai::chat::DocumentAITaskBootstrap::shouldRefineWithModel(
                boot, prefs.taskBootstrapRefineBelow))
        {
            if (m_xStatusLabel)
                m_xStatusLabel->set_label(u"语义启动 · 轻量模型精炼理解中…"_ustr);
            SetAgentStepBar(u"步骤：语义启动 · light 复述…"_ustr);
            if (Application::IsInMain())
                Application::Reschedule(true);
            const auto refine
                = kqoffice::ai::AgentStepRunner::runOne(u"summarize"_ustr, boot.modelRefinePrompt,
                                                        15000);
            if (refine.status == u"ok"_ustr && !refine.content.isEmpty())
            {
                boot = kqoffice::ai::chat::DocumentAITaskBootstrap::refineWithModelOutput(
                    boot, bootIn, refine.content);
                AppendTranscript(u"System"_ustr,
                                 u"语义启动 · 模型复述 conf="_ustr
                                     + OUString::number(boot.restatement.confidence)
                                     + u" · src="_ustr + boot.restatement.source
                                     + (boot.schedule.secondaryRefined
                                            ? u" · 二次调度已刷新"_ustr
                                            : OUString()),
                                 /*bPersistHistory*/ false);
            }
            else
            {
                AppendTranscript(u"System"_ustr,
                                 u"语义启动 · 模型复述不可用，沿用规则复述 · conf="_ustr
                                     + OUString::number(boot.restatement.confidence),
                                 /*bPersistHistory*/ false);
            }
        }
    }

    // Prefer schedule capability when user did not force a chip/scenario.
    if (m_sForcedCapability.isEmpty() && !boot.schedule.primaryCapability.isEmpty()
        && boot.schedule.primaryCapability != u"chat"_ustr)
        m_sForcedCapability = boot.schedule.primaryCapability;
    if (boot.schedule.preferAgentPipeline && m_xOptAgentPipeline)
        m_xOptAgentPipeline->set_active(true);

    // Soft hard-gate: Required without restatement body → stop (should not happen).
    if (boot.mode == kqoffice::ai::chat::TaskBootstrapMode::Required
        && boot.restatement.userVisibleZh.isEmpty())
    {
        AppendTranscript(u"System"_ustr,
                         u"语义启动失败：缺少任务复述，已停止工具调用 · 主文档未改"_ustr,
                         /*bPersistHistory*/ false);
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"请重新描述目标 · 主文档未改"_ustr);
        FocusPrompt();
        return;
    }

    if (!boot.skipRestatementCard && !boot.restatement.userVisibleZh.isEmpty())
    {
        AppendTranscript(u"System"_ustr,
                         u"语义启动 · "_ustr + boot.restatement.userVisibleZh,
                         /*bPersistHistory*/ false);
        AppendTranscript(u"System"_ustr, boot.schedule.summaryZh, /*bPersistHistory*/ false);
        m_sLastTaskRestatement = boot.restatement.objective;
        UpdateTaskBootstrapChip(boot.restatement.userVisibleZh);
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"理解已对齐 · "_ustr
                                      + boot.restatement.objective);
        SetAgentStepBar(u"步骤：语义启动 · "_ustr + boot.restatement.nextStep);
    }
    else if (!boot.schedule.summaryZh.isEmpty())
    {
        AppendTranscript(u"System"_ustr, boot.schedule.summaryZh, /*bPersistHistory*/ false);
        UpdateTaskBootstrapChip(OUString());
    }

    // Use normalized prompt for continue / semantic expansion.
    const OUString sWorkPrompt
        = boot.normalizedPrompt.isEmpty() ? sPrompt : boot.normalizedPrompt;

    // ── Normal chat pipeline ─────────────────────────────────────────
    // Multi-step agent path: Plan → Act → Review (five-slot Provider routing).
    // Triggered by checkbox「多步 Agent」、调度合同、or agent/cowork keywords.
    const OUString lowerPrompt = sWorkPrompt.toAsciiLowerCase();
    const bool bAgentOpt = m_xOptAgentPipeline && m_xOptAgentPipeline->get_active();
    const bool bAgentPipeline
        = bAgentOpt || boot.schedule.preferAgentPipeline
          || lowerPrompt.indexOf(u"子代理"_ustr) >= 0
          || lowerPrompt.indexOf(u"协作"_ustr) >= 0 || lowerPrompt.indexOf(u"agent"_ustr) >= 0
          || lowerPrompt.startsWith(u"/agent"_ustr) || lowerPrompt.indexOf(u"cowork"_ustr) >= 0
          || lowerPrompt.indexOf(u"多步"_ustr) >= 0 || lowerPrompt.indexOf(u"plan-act"_ustr) >= 0;

    // Complex-task start confirm (not normal rewrite/summarize/chat):
    // agent pipeline, forced/scenario capability agent|plan (e.g. design-apply), or agent intent.
    // Skip after user already confirmed a work plan this turn (avoid double dialog).
    {
        const OUString taskCap = !m_sForcedCapability.isEmpty()
                                     ? m_sForcedCapability
                                     : DetectComposerIntent(sWorkPrompt);
        const bool bComplexTaskStart
            = !bSkipWorkPlanGate
              && (bAgentPipeline || taskCap == u"agent"_ustr || taskCap == u"plan"_ustr);
        if (!ConfirmComplexAiTaskStart(bComplexTaskStart))
            return;
    }

    // ── Work plan gate (Grok plan-mode analogue) ─────────────────────
    // Large/ambiguous edits pause for structured plan confirmation before model.
    // Approve → resume with contract; write-back still needs separate approval.
    if (!bSkipWorkPlanGate)
    {
        bool bHasSel = false;
        sal_Int32 nSelChars = 0;
        {
            const auto sel = kqoffice::ai::chat::AgentChatSelectionCapture::captureCurrent();
            bHasSel = !sel.text.isEmpty();
            nSelChars = sel.length;
        }
        const bool bAgentCb = m_xOptAgentPipeline && m_xOptAgentPipeline->get_active();
        const bool bNeedWorkPlan
            = m_bForceWorkPlanOnce
              || kqoffice::ai::chat::DocumentAIWorkPlan::looksLikeForcePlan(rawUserPrompt)
              || kqoffice::ai::chat::DocumentAIWorkPlan::looksLikeLargeTask(
                  rawUserPrompt, CurrentDocumentSurface(), bHasSel, bAgentCb, m_sForcedCapability);
        if (bNeedWorkPlan)
        {
            kqoffice::ai::chat::WorkPlanInput win;
            win.userPrompt = rawUserPrompt;
            win.surface = CurrentDocumentSurface();
            win.hasSelection = bHasSel;
            win.selectionChars = nSelChars;
            win.skillId = matchedSkillId;
            win.skillTitleZh = matchedSkillTitle;
            win.skillDescription = matchedSkillDesc;
            win.bootstrapObjective = boot.restatement.objective;
            win.forcedCapability = m_sForcedCapability;
            win.agentCheckbox = bAgentCb;
            kqoffice::ai::chat::WorkPlan plan
                = kqoffice::ai::chat::DocumentAIWorkPlan::build(win);
            // Seed for post-approve generation: skill-expanded body preferred.
            plan.executionSeed = sWorkPrompt.isEmpty() ? sPrompt : sWorkPrompt;
            PresentWorkPlan(plan);
            m_xPromptEntry->set_text(OUString());
            return;
        }
    }

    m_sLastPrompt = sPrompt;
    m_sStreamingBuffer.clear();
    SetState(AIChatPanelState::Requesting);
    AppendTranscript(u"User"_ustr, sPrompt); // sticky 原话
    // Human-readable task narrative (Copilot-style) before model call.
    {
        const OUString intent = !m_sForcedCapability.isEmpty()
                                    ? m_sForcedCapability
                                    : DetectComposerIntent(sWorkPrompt);
        AppendTranscript(
            u"System"_ustr,
            u"task-aware · 意图="_ustr + intent
                + u" · 将生成建议，主文档需批准后才改 · 可随时停止"_ustr,
            /*bPersistHistory*/ false);
    }
    m_xPromptEntry->set_text(OUString());

    if (bAgentPipeline)
    {
        // Bind document selection so plan/act/review see the active surface.
        // Evidence token "agent-pipeline" is contract-locked by model-routing / Stage B harnesses.
        // Use sWorkPrompt so「继续」携带上一任务合同。
        const kqoffice::ai::chat::DocumentAIBinding aBind
            = kqoffice::ai::chat::DocumentAIContext::bindUserInput(sWorkPrompt);
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"多步协作启动 · "_ustr + aBind.statusLabel
                                      + u" · 主文档不会自动改"_ustr);
        AppendTranscript(u"System"_ustr,
                         u"agent-pipeline · 多步协作启动：规划 → 执行 → 审查 · "_ustr
                             + aBind.statusLabel + u" · 不直接改主文档"_ustr);
        // Agent Mode: phase 1 plan only → human Continue gate → phase 2 execute.
        ClearAgentContinueGate();
        m_aAgentStepCache.clear();
        if (m_xAgentTree)
            m_xAgentTree->clear();
        PushAgentStepRow(u"0. 绑定文档"_ustr, u"运行中"_ustr);
        PushAgentStepRow(u"1. 规划"_ustr, u"排队中"_ustr);
        PushAgentStepRow(u"2. 执行"_ustr, u"待继续"_ustr);
        PushAgentStepRow(u"3. 审查"_ustr, u"待继续"_ustr);
        PushAgentStepRow(u"4. 校验"_ustr, u"待继续"_ustr);
        LoadAgentSteps();
        UpdateAgentStepBar(0, u"绑定文档上下文…"_ustr);
        m_bAgentRunActive = true;
        if (Application::IsInMain())
            Application::Reschedule(true);

        if (m_bCancelRequested)
        {
            m_bAgentRunActive = false;
            MarkAgentStepsStopped();
            m_sLastOutcomeDetail = u"用户停止多步协作"_ustr;
            SetState(AIChatPanelState::Cancelled);
            if (m_xStatusLabel)
                m_xStatusLabel->set_label(u"已停止 · 主文档未改"_ustr);
            FocusPrompt();
            return;
        }

        const OUString sGoal
            = aBind.enrichedPrompt.isEmpty() ? sWorkPrompt : aBind.enrichedPrompt;
        const OUString sSurface = aBind.selection.surface.isEmpty()
                                      ? CurrentDocumentSurface()
                                      : aBind.selection.surface;
        const OUString sDocTools = aBind.documentSkeleton;
        UpdateAgentStepBar(1, u"规划中…"_ustr);
        if (Application::IsInMain())
            Application::Reschedule(true);

        const kqoffice::ai::AgentPipelineResult pipe
            = kqoffice::ai::AgentStepRunner::runCeilingPlanPhase(
                sGoal, aBind.providerContext, sSurface, sDocTools);
        m_bAgentRunActive = false;

        if (m_bCancelRequested)
        {
            ClearAgentContinueGate();
            MarkAgentStepsStopped();
            m_sLastOutcomeDetail = u"用户停止多步协作"_ustr;
            SetState(AIChatPanelState::Cancelled);
            if (m_xStatusLabel)
                m_xStatusLabel->set_label(u"已停止 · 主文档未改"_ustr);
            AppendTerminalEvidence(u"cancelled"_ustr, pipe.finalEvidenceId);
            FocusPrompt();
            return;
        }

        // Rebuild step list from plan-phase results.
        m_aAgentStepCache.clear();
        if (m_xAgentTree)
            m_xAgentTree->clear();
        OUString sPlanContent;
        for (size_t i = 0; i < pipe.steps.size(); ++i)
        {
            const auto& st = pipe.steps[i];
            OUString title = st.stepKind;
            if (title == u"bind"_ustr)
                title = u"0. 绑定文档"_ustr;
            else if (title == u"plan"_ustr)
                title = (sSurface == u"calc"_ustr) ? u"1. 规划（Calc 四阶段）"_ustr
                                                   : u"1. 规划"_ustr;
            else if (title == u"sandbox"_ustr)
                title = u"公式沙箱"_ustr;
            else
                title = OUString::number(static_cast<sal_Int32>(i)) + u". "_ustr + title;

            UpdateAgentStepBar(static_cast<sal_Int32>(i),
                               title + u" "_ustr + LocalizeAgentStepStatus(st.status));

            OUString statusZh = LocalizeAgentStepStatus(st.status);
            if (!st.content.isEmpty() && st.status == u"ok"_ustr)
            {
                OUString preview = st.content;
                if (preview.getLength() > 48)
                    preview = preview.copy(0, 48) + u"…"_ustr;
                preview = preview.replaceAll(u"\n"_ustr, u" "_ustr);
                statusZh = u"完成 · "_ustr + preview;
            }
            PushAgentStepRow(title, statusZh);

            if (st.stepKind == u"plan"_ustr && st.status == u"ok"_ustr)
            {
                sPlanContent = st.content;
                const auto titles
                    = kqoffice::ai::AgentStepRunner::parsePlanStepTitles(st.content);
                sal_Int32 nSub = 0;
                for (const auto& t : titles)
                {
                    if (++nSub > 6)
                        break;
                    PushAgentStepRow(u"  · "_ustr + t, u"计划项 · 待继续"_ustr);
                }
            }

            OUString body = st.content;
            if (body.getLength() > 500)
                body = body.copy(0, 500) + u"…"_ustr;
            AppendTranscript(u"Agent·"_ustr + title,
                             (body.isEmpty() ? (u"状态="_ustr + statusZh) : body),
                             /*bPersistHistory*/ false);
        }
        if (pipe.success)
        {
            PushAgentStepRow(u"2. 执行"_ustr, u"待继续"_ustr);
            PushAgentStepRow(u"3. 审查"_ustr, u"待继续"_ustr);
            PushAgentStepRow(u"4. 校验"_ustr, u"待继续"_ustr);
        }
        else if (pipe.steps.empty())
        {
            PushAgentStepRow(u"0. 绑定文档"_ustr, u"失败"_ustr);
            PushAgentStepRow(u"1. 规划"_ustr, u"失败"_ustr);
            PushAgentStepRow(u"2. 执行"_ustr, u"未运行"_ustr);
            PushAgentStepRow(u"3. 审查"_ustr, u"未运行"_ustr);
            PushAgentStepRow(u"4. 校验"_ustr, u"未运行"_ustr);
        }
        LoadAgentSteps();

        SetState(AIChatPanelState::Streaming);
        if (!pipe.combinedContent.isEmpty())
            AppendAssistantMarkdown(pipe.combinedContent);
        else
            AppendAssistantChunk(u"（Agent Mode 规划阶段无返回正文）"_ustr);

        if (pipe.success && !sPlanContent.isEmpty())
        {
            // Human gate: wait for 「继续」before act/review/verify.
            m_bAgentAwaitingContinue = true;
            m_sAgentGateGoal = sGoal;
            m_sAgentGateContext = aBind.providerContext;
            m_sAgentGateSurface = sSurface;
            m_sAgentGateDocTools = sDocTools;
            m_sAgentGatePlanContent = sPlanContent;
            if (m_xAgentContinueBtn)
                m_xAgentContinueBtn->set_sensitive(true);
            SetAgentStepBar(
                u"Agent Mode · ✓绑定 → ✓规划 · 请点「继续」执行（主文档未改）"_ustr);
            m_sLastOutcomeDetail.clear();
            SetState(AIChatPanelState::Idle);
            AppendTerminalEvidence(u"ok"_ustr, pipe.finalEvidenceId);
            if (m_xStatusLabel)
                m_xStatusLabel->set_label(
                    u"计划已就绪 · 点「继续」生成写回草案 · 主文档未改"_ustr);
            AppendTranscript(
                u"System"_ustr,
                u"Agent Mode 规划完成 · 表面="_ustr + sSurface
                    + u" · 证据="_ustr + pipe.finalEvidenceId
                    + u" · 请点「继续」执行，或「停止」放弃 · 主文档未改"_ustr);
            if (m_xMainNotebook)
            {
                try
                {
                    m_xMainNotebook->set_current_page(1);
                }
                catch (...)
                {
                }
            }
        }
        else
        {
            ClearAgentContinueGate();
            const OUString failDetail
                = ShortenUserDetail(pipe.failureReason.isEmpty()
                                        ? LocalizeProviderStatusZh(u"provider-error"_ustr)
                                        : pipe.failureReason);
            m_sLastOutcomeDetail = failDetail;
            SetAgentStepBar(u"步骤：规划失败 · 主文档未改 · "_ustr + failDetail);
            ClearPendingPlan();
            SetState(AIChatPanelState::Failed);
            AppendTerminalEvidence(u"provider-error"_ustr, pipe.finalEvidenceId);
            if (m_xStatusLabel)
                m_xStatusLabel->set_label(u"失败 · 主文档未改 · "_ustr + failDetail);
            AppendTranscript(u"System"_ustr,
                             u"多步协作规划失败："_ustr + failDetail + u" · 主文档未改"_ustr);
            PresentModelHealthGuidance(
                /*bOpenConfigDir*/ failDetail.indexOf(u"401"_ustr) >= 0
                    || failDetail.indexOf(u"认证"_ustr) >= 0
                    || failDetail.indexOf(u"API Key"_ustr) >= 0,
                /*rFailDetail*/ failDetail);
            LoadReviewQueue();
        }
        FocusPrompt();
        return;
    }

    const bool bDocRag = boot.schedule.useLocalRag
                         || kqoffice::ai::chat::DocumentAILocalRag::wantsDocumentRag(sWorkPrompt)
                         || sWorkPrompt.indexOf(u"问本文档"_ustr) >= 0
                         || sWorkPrompt.startsWith(u"/问"_ustr);
    const css::ai::ProviderResponse aResponse = CallProvider(sWorkPrompt);

    if (m_bCancelRequested)
    {
        MarkAgentStepsStopped();
        m_sLastOutcomeDetail = u"用户停止请求"_ustr;
        SetState(AIChatPanelState::Cancelled);
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"已停止 · 主文档未改"_ustr);
        AppendTerminalEvidence(u"cancelled"_ustr, aResponse.evidenceId);
        FocusPrompt();
        return;
    }

    SetState(AIChatPanelState::Streaming);

    OUString displayContent = aResponse.content;
    const bool bOk = aResponse.status == u"ok"_ustr;
    if (bOk && bDocRag && !aResponse.content.isEmpty())
    {
        // M5: structure Q&A as local-provenance card + enable locate.
        displayContent = kqoffice::ai::chat::DocumentAILocalRag::formatAnswerCard(
            sPrompt, aResponse.content, 4);
        AppendAssistantMarkdown(displayContent);
        m_sLastRagQuery = sPrompt;
        auto hits = kqoffice::ai::chat::DocumentAILocalRag::retrieve(sPrompt, 1);
        m_sLastRagPosition = hits.empty() ? OUString() : hits.front().position;
        if (m_xLocateRagBtn)
            m_xLocateRagBtn->set_sensitive(!m_sLastRagQuery.isEmpty()
                                           || !m_sLastRagPosition.isEmpty());
    }
    else if (bOk && aResponse.content.isEmpty() && m_sStreamingBuffer.isEmpty())
    {
        AppendAssistantMarkdown(u"（空响应）"_ustr);
    }
    else if (bOk && m_sStreamingBuffer.isEmpty() && !aResponse.content.isEmpty())
    {
        // Sync fallback path (no stream chunks painted yet).
        AppendAssistantMarkdown(aResponse.content);
    }
    // If stream already painted into transcript, skip re-append of full content.
    // On failure: do not paint error as assistant prose; failure path below shows Chinese detail.

    if (bOk)
    {
        m_sLastOutcomeDetail.clear();
        SetState(AIChatPanelState::AwaitingApproval);
        AppendTerminalEvidence(aResponse.status, aResponse.evidenceId);
        RegisterAssistantArtifact(displayContent.isEmpty() ? aResponse.content : displayContent,
                                  aResponse.evidenceId,
                                  bDocRag ? u"ask-document"_ustr : u"chat"_ustr);
        // Stage only — never mutate the main document before explicit approval.
        // Pure Q&A cards usually don't need apply; still stage raw model text if useful.
        // M14: never stage an intermediate TOOL_REQUEST as ApplyPlan.
        if (!kqoffice::ai::chat::DocumentAIDocumentTools::isPrimarilyToolRequest(aResponse.content))
            StagePendingApplyPlan(aResponse.content, aResponse.evidenceId);
        else
            AppendTranscript(u"System"_ustr,
                             u"document-tools · multi-round · 达到轮次上限仍为工具请求 · "
                             u"未暂存 ApplyPlan · 主文档未改"_ustr,
                             /*bPersistHistory*/ false);
        if (m_xStatusLabel)
        {
            if (bDocRag)
                m_xStatusLabel->set_label(u"问本文档完成 · 见出处位置 · 主文档未自动改"_ustr);
            else
                m_xStatusLabel->set_label(
                    FormatEvidenceUserSummary(aResponse.status, aResponse.evidenceId));
        }
    }
    else
    {
        // Failure: never stage / never mutate main doc; surface Chinese reason + recovery card.
        ClearPendingPlan();
        const OUString statusZh = LocalizeProviderStatusZh(aResponse.status);
        OUString detail = statusZh;
        if (!aResponse.content.isEmpty())
        {
            // Prefer Chinese body; avoid dumping long English stack traces.
            const OUString body = ShortenUserDetail(aResponse.content);
            detail = body;
        }
        m_sLastOutcomeDetail = detail;
        SetAgentStepBar(u"步骤：失败 · 主文档未改 · "_ustr + detail);
        SetState(AIChatPanelState::Failed);
        AppendTerminalEvidence(aResponse.status, aResponse.evidenceId);
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"失败 · 主文档未改 · "_ustr + detail);
        // M16: full recovery guide (Key / Ollama / gateway) — one-click path via「修复模型」.
        const bool bAuthLike = detail.indexOf(u"401"_ustr) >= 0 || detail.indexOf(u"认证"_ustr) >= 0
                               || detail.indexOf(u"API Key"_ustr) >= 0
                               || detail.indexOf(u"api-key"_ustr) >= 0
                               || detail.indexOf(u"未找到 API"_ustr) >= 0;
        const bool bOfflineLike = detail.indexOf(u"不可达"_ustr) >= 0
                                  || detail.indexOf(u"离线"_ustr) >= 0
                                  || detail.indexOf(u"超时"_ustr) >= 0
                                  || detail.indexOf(u"timeout"_ustr) >= 0;
        PresentModelHealthGuidance(/*bOpenConfigDir*/ bAuthLike,
                                   /*rFailDetail*/ detail);
        if (bOfflineLike && !bAuthLike)
        {
            AppendTranscript(u"System"_ustr,
                             u"提示：也可点侧栏「修复模型」打开配置目录并复查 Ollama/网关 · "
                             u"主文档未改"_ustr,
                             /*bPersistHistory*/ false);
        }
        // Persist a short evidence record for audit (no JSON dump in UI).
        {
            kqoffice::ai::EvidenceRecord rec;
            rec.serviceMode = u"offline"_ustr;
            rec.provider = u"chat-panel"_ustr;
            rec.capability = u"chat"_ustr;
            rec.status = aResponse.status.isEmpty() ? u"provider-error"_ustr : aResponse.status;
            rec.requestSizeBytes = sPrompt.getLength();
            rec.responseSizeBytes = aResponse.content.getLength();
            rec.durationMs = aResponse.durationMs;
            kqoffice::ai::EvidenceRecorder recorder;
            (void)recorder.record(rec);
        }
        LoadReviewQueue();
    }
    FocusPrompt();
}

void AIChatPanel::ClearPendingPlan()
{
    m_bHasPendingPlan = false;
    m_aPendingPlan = kqoffice::ai::chat::ApplyPlan{};
    m_sPendingEvidenceId.clear();
    kqoffice::ai::chat::DocumentAIDocumentTools::clearSeen();
    UpdatePendingPlanChip();
    LoadReviewQueue();
}

void AIChatPanel::PresentStalePlanRecovery(const OUString& rSource)
{
    // Drop invalid staged plan; document structure already diverged from markSeen.
    ClearPendingPlan();
    m_bStaleNeedsRegen = true;
    m_sLastOutcomeDetail = u"文档已变更 · 计划过期 · 可一键重新生成"_ustr;
    SetState(AIChatPanelState::Failed);
    SetAgentStepBar(u"步骤：计划已过期 · 主文档未改 · 点「重新生成」"_ustr);

    AppendTranscript(
        u"System"_ustr,
        u"plan-stale source="_ustr + (rSource.isEmpty() ? u"unknown"_ustr : rSource)
            + u" main-document-mutation=false · 暂存后文档被修改 · 旧计划已丢弃 · "
              u"主文档未改"_ustr,
        /*bPersistHistory*/ false);

    OUStringBuffer md;
    md.append(u"### 计划已过期（主文档未改）\n\n"_ustr);
    md.append(kqoffice::ai::chat::DocumentAIDocumentTools::staleApplyErrorZh());
    md.append(u"\n\n**一键处理**\n"_ustr);
    md.append(u"1. 点侧栏 **「重新生成」**（按当前文档 + 上一条指令再跑一轮）\n"_ustr);
    md.append(u"2. 或重新选区后点意图芯片 / 发送\n"_ustr);
    md.append(u"3. 新计划出来后再 **批准写回**\n\n"_ustr);
    md.append(u"纪律：过期拒绝写回 · **不静默改文档** · 重新生成仍须批准\n"_ustr);
    AppendAssistantMarkdown(md.makeStringAndClear());

    if (m_xRetryButton)
    {
        m_xRetryButton->set_label(u"重新生成"_ustr);
        m_xRetryButton->set_tooltip_text(
            u"丢弃过期计划，用当前文档上下文重跑上一条指令（不自动写回）"_ustr);
        m_xRetryButton->set_sensitive(!m_sLastPrompt.isEmpty() && !IsRunBusy());
    }
    if (m_xStatusLabel)
    {
        m_xStatusLabel->set_label(
            m_sLastPrompt.isEmpty()
                ? u"计划过期 · 主文档未改 · 请重新选区并发送"_ustr
                : u"计划过期 · 主文档未改 · 点「重新生成」"_ustr);
    }
    UpdateApprovalChrome();
    UpdateActions();
    UpdateActivityCard();
}

bool AIChatPanel::RegenerateAfterStale()
{
    if (m_sLastPrompt.isEmpty())
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"无法重新生成 · 无上一条指令 · 主文档未改"_ustr);
        return false;
    }
    if (IsRunBusy())
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"正在运行 · 请稍候再重新生成"_ustr);
        return false;
    }

    // Ensure we do not apply the old plan or keep a stale baseline.
    ClearPendingPlan();
    m_bStaleNeedsRegen = false;
    if (m_xRetryButton)
    {
        m_xRetryButton->set_label(u"重试"_ustr);
        m_xRetryButton->set_tooltip_text(u"恢复上一条指令以便重试。"_ustr);
    }

    AppendTranscript(
        u"System"_ustr,
        u"plan-stale-regenerate prompt-len="_ustr
            + OUString::number(m_sLastPrompt.getLength())
            + u" · 按当前文档重跑 · 主文档未改（生成后仍须批准）"_ustr,
        /*bPersistHistory*/ false);
    SetAgentStepBar(u"步骤：重新生成中 · 使用当前文档上下文…"_ustr);
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"正在按当前文档重新生成 · 主文档不会自动改"_ustr);

    if (m_xPromptEntry)
        m_xPromptEntry->set_text(m_sLastPrompt);
    SetState(AIChatPanelState::Idle);
    SubmitPrompt();
    return true;
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
    // Prefer formula plans even when a weak structured plan exists but has no '=' ops.
    if (sel.surface == u"calc"_ustr)
    {
        // M-C1: explicit write-back blocks win over free-form = lines.
        const bool bWeakCalc
            = !kqoffice::ai::chat::AgentChatDiffExtractor::validate(aPlan)
              || aPlan.planId == u"ap-selection-replace"_ustr;
        if (bWeakCalc
            && kqoffice::ai::chat::AgentChatDiffExtractor::looksLikeCalcCleanWriteback(
                rProviderContent))
        {
            auto clean
                = kqoffice::ai::chat::AgentChatDiffExtractor::extractCalcCleanWritebackPlan(
                    rProviderContent);
            if (kqoffice::ai::chat::AgentChatDiffExtractor::validate(clean))
            {
                aPlan = std::move(clean);
                AppendTranscript(
                    u"System"_ustr,
                    u"calc-clean-staged cells="_ustr
                        + OUString::number(static_cast<sal_Int32>(aPlan.operations.size()))
                        + u" · 批准后写入旁列/目标格 · 主文档尚未改"_ustr,
                    /*bPersistHistory*/ false);
            }
        }
        if ((!kqoffice::ai::chat::AgentChatDiffExtractor::validate(aPlan)
             || aPlan.planId == u"ap-selection-replace"_ustr)
            && kqoffice::ai::chat::AgentChatDiffExtractor::looksLikeCalcFormulaWriteback(
                rProviderContent))
        {
            auto wb
                = kqoffice::ai::chat::AgentChatDiffExtractor::extractCalcFormulaWritebackPlan(
                    rProviderContent);
            if (kqoffice::ai::chat::AgentChatDiffExtractor::validate(wb))
            {
                aPlan = std::move(wb);
                AppendTranscript(
                    u"System"_ustr,
                    u"calc-formula-staged cells="_ustr
                        + OUString::number(static_cast<sal_Int32>(aPlan.operations.size()))
                        + u" · 批准后写入目标格 · 主文档尚未改"_ustr,
                    /*bPersistHistory*/ false);
            }
        }

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
        bool bPlanHasFormula = false;
        if (kqoffice::ai::chat::AgentChatDiffExtractor::validate(aPlan))
        {
            for (const auto& op : aPlan.operations)
            {
                if (op.newText.trim().startsWith(u"="_ustr)
                    || op.newText.trim().startsWith(u"＝"_ustr))
                {
                    bPlanHasFormula = true;
                    break;
                }
            }
        }
        if (!formulas.empty()
            && (!kqoffice::ai::chat::AgentChatDiffExtractor::validate(aPlan) || !bPlanHasFormula))
        {
            OUString pos = sel.position;
            if (pos.isEmpty())
                pos = u"cell:A1"_ustr;
            // Prefer writing summary formula into adjacent column for range selections
            // when only one formula (keeps source data intact until user moves it).
            if (formulas.size() == 1 && pos.startsWith(u"range:"_ustr))
            {
                const OUString adj
                    = kqoffice::ai::chat::AgentChatDiffExtractor::adjacentColumnCell(pos);
                if (!adj.isEmpty())
                    pos = adj;
            }
            aPlan = kqoffice::ai::chat::AgentChatDiffExtractor::makeFormulaRangePlan(
                pos, formulas, sel.text);
            aPlan.rawOutput = rProviderContent;
        }
        // Chart intent (no formula, or explicit 图表 advice): stage chart wizard plan.
        else if (!kqoffice::ai::chat::AgentChatDiffExtractor::validate(aPlan)
                 && (kqoffice::ai::chat::AgentChatDiffExtractor::looksLikeChartIntent(
                         rProviderContent)
                     || rProviderContent.indexOf(u"chart-assist"_ustr) >= 0))
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

    // Impress: outline free text → multi-slide insert plan (prefer over weak single replace).
    if (sel.surface == u"impress"_ustr)
    {
        // M-I0: speaker-notes-only write-back (does not rebuild slide shapes).
        const bool bWeakImp
            = !kqoffice::ai::chat::AgentChatDiffExtractor::validate(aPlan)
              || aPlan.planId == u"ap-selection-replace"_ustr;
        if (bWeakImp
            && kqoffice::ai::chat::AgentChatDiffExtractor::looksLikeImpressNotesWriteback(
                rProviderContent))
        {
            auto notes
                = kqoffice::ai::chat::AgentChatDiffExtractor::extractImpressNotesWritebackPlan(
                    rProviderContent);
            if (kqoffice::ai::chat::AgentChatDiffExtractor::validate(notes))
            {
                aPlan = std::move(notes);
                AppendTranscript(
                    u"System"_ustr,
                    u"impress-notes-staged slides="_ustr
                        + OUString::number(static_cast<sal_Int32>(aPlan.operations.size()))
                        + u" · 批准后仅写讲稿/备注页 · 主文档尚未改"_ustr,
                    /*bPersistHistory*/ false);
            }
        }

        auto outline
            = kqoffice::ai::chat::AgentChatDiffExtractor::extractOutlineSlidePlan(rProviderContent);
        const bool bOutlineOk
            = kqoffice::ai::chat::AgentChatDiffExtractor::validate(outline);
        const bool bLooksOutline
            = kqoffice::ai::chat::AgentChatDiffExtractor::looksLikeOutlineSlideContent(
                rProviderContent);
        const bool bWeakPlan
            = !kqoffice::ai::chat::AgentChatDiffExtractor::validate(aPlan)
              || aPlan.operations.size() < outline.operations.size()
              || aPlan.planId == u"ap-selection-replace"_ustr;
        // Prefer outline rebuild only when not a notes-only plan.
        if (bOutlineOk && (bWeakPlan || bLooksOutline)
            && aPlan.planId != u"ap-impress-notes"_ustr)
        {
            aPlan = std::move(outline);
            AppendTranscript(
                u"System"_ustr,
                u"impress-outline-staged slides="_ustr
                    + OUString::number(static_cast<sal_Int32>(aPlan.operations.size()))
                    + u" · 批准后按页写回 · 主文档尚未改"_ustr,
                /*bPersistHistory*/ false);
        }
    }

    // Writer M-W1: heading outline / review fix list → staged ApplyPlan (approve first).
    if (sel.surface == u"writer"_ustr || sel.surface.isEmpty() || sel.surface == u"unknown"_ustr)
    {
        const bool bWeak
            = !kqoffice::ai::chat::AgentChatDiffExtractor::validate(aPlan)
              || aPlan.planId == u"ap-selection-replace"_ustr;
        if (bWeak
            && kqoffice::ai::chat::AgentChatDiffExtractor::looksLikeWriterHeadingOutline(
                rProviderContent))
        {
            auto outline
                = kqoffice::ai::chat::AgentChatDiffExtractor::extractWriterHeadingOutlinePlan(
                    rProviderContent);
            if (kqoffice::ai::chat::AgentChatDiffExtractor::validate(outline))
            {
                aPlan = std::move(outline);
                AppendTranscript(
                    u"System"_ustr,
                    u"writer-outline-staged headings="_ustr
                        + OUString::number(static_cast<sal_Int32>(aPlan.operations.size()))
                        + u" · 批准后应用标题样式（para: 或 search: 标题匹配）· 主文档尚未改"_ustr,
                    /*bPersistHistory*/ false);
            }
        }
        if ((!kqoffice::ai::chat::AgentChatDiffExtractor::validate(aPlan)
             || aPlan.planId == u"ap-selection-replace"_ustr)
            && kqoffice::ai::chat::AgentChatDiffExtractor::looksLikeReviewFixList(
                rProviderContent))
        {
            auto fixes
                = kqoffice::ai::chat::AgentChatDiffExtractor::extractReviewFixPlan(
                    rProviderContent);
            if (kqoffice::ai::chat::AgentChatDiffExtractor::validate(fixes))
            {
                aPlan = std::move(fixes);
                AppendTranscript(
                    u"System"_ustr,
                    u"writer-review-fixes-staged count="_ustr
                        + OUString::number(static_cast<sal_Int32>(aPlan.operations.size()))
                        + u" · 批准后按条替换 · 主文档尚未改"_ustr,
                    /*bPersistHistory*/ false);
            }
        }
    }

    // Calc: formula / clean write-back blocks → stage (approve first).
    if (sel.surface == u"calc"_ustr
        || rProviderContent.indexOf(u"===可圈公式写回==="_ustr) >= 0
        || rProviderContent.indexOf(u"===可圈清洗写回==="_ustr) >= 0)
    {
        const bool bWeak
            = !kqoffice::ai::chat::AgentChatDiffExtractor::validate(aPlan)
              || aPlan.planId == u"ap-selection-replace"_ustr;
        if (bWeak
            && kqoffice::ai::chat::AgentChatDiffExtractor::looksLikeCalcCleanWriteback(
                rProviderContent))
        {
            auto clean
                = kqoffice::ai::chat::AgentChatDiffExtractor::extractCalcCleanWritebackPlan(
                    rProviderContent);
            if (kqoffice::ai::chat::AgentChatDiffExtractor::validate(clean))
            {
                aPlan = std::move(clean);
                AppendTranscript(
                    u"System"_ustr,
                    u"calc-clean-staged cells="_ustr
                        + OUString::number(static_cast<sal_Int32>(aPlan.operations.size()))
                        + u" · 批准后写公式/清洗 · 主文档尚未改"_ustr,
                    /*bPersistHistory*/ false);
                const auto snap
                    = kqoffice::ai::chat::DocumentAIFormulaDryRun::captureSelectionSnapshot(128);
                const auto dry = kqoffice::ai::chat::DocumentAIFormulaDryRun::checkPlan(
                    aPlan, snap.empty() ? nullptr : &snap);
                if (dry.hasWork())
                    AppendTranscript(u"System"_ustr,
                                     u"calc-dry-run · "_ustr + dry.summaryZh
                                         + (dry.allOk() ? u" · 可批准写回"_ustr
                                                        : u" · 建议修正后再批准"_ustr),
                                     /*bPersistHistory*/ false);
            }
        }
        if ((!kqoffice::ai::chat::AgentChatDiffExtractor::validate(aPlan)
             || aPlan.planId == u"ap-selection-replace"_ustr)
            && kqoffice::ai::chat::AgentChatDiffExtractor::looksLikeCalcFormulaWriteback(
                rProviderContent))
        {
            auto formula
                = kqoffice::ai::chat::AgentChatDiffExtractor::extractCalcFormulaWritebackPlan(
                    rProviderContent);
            if (kqoffice::ai::chat::AgentChatDiffExtractor::validate(formula))
            {
                aPlan = std::move(formula);
                AppendTranscript(
                    u"System"_ustr,
                    u"calc-formula-staged cells="_ustr
                        + OUString::number(static_cast<sal_Int32>(aPlan.operations.size()))
                        + u" · 批准后写入公式 · 主文档尚未改"_ustr,
                    /*bPersistHistory*/ false);
                const auto snap
                    = kqoffice::ai::chat::DocumentAIFormulaDryRun::captureSelectionSnapshot(128);
                const auto dry = kqoffice::ai::chat::DocumentAIFormulaDryRun::checkPlan(
                    aPlan, snap.empty() ? nullptr : &snap);
                if (dry.hasWork())
                    AppendTranscript(u"System"_ustr,
                                     u"calc-dry-run · "_ustr + dry.summaryZh
                                         + (dry.allOk() ? u" · 可批准写回"_ustr
                                                        : u" · 建议修正后再批准"_ustr),
                                     /*bPersistHistory*/ false);
            }
        }
    }

    // If LLM did not emit structured ops but user has a selection, stage a
    // single replace so approve can write back via DocumentAIApply.
    // CRITICAL (Stage1 audit): do NOT stage selection-replace for consult /
    // quality-review replies — otherwise 内容质检/版式审 would offer Diff that
    // overwrites the selection with the advisory report (false write path).
    if (!kqoffice::ai::chat::AgentChatDiffExtractor::validate(aPlan))
    {
        const bool bHasStructuredWriteMarker
            = kqoffice::ai::chat::AgentChatDiffExtractor::looksLikeReviewFixList(rProviderContent)
              || kqoffice::ai::chat::AgentChatDiffExtractor::looksLikeWriterHeadingOutline(
                  rProviderContent)
              || kqoffice::ai::chat::AgentChatDiffExtractor::looksLikeCalcFormulaWriteback(
                  rProviderContent)
              || kqoffice::ai::chat::AgentChatDiffExtractor::looksLikeCalcCleanWriteback(
                  rProviderContent)
              || rProviderContent.indexOf(u"===可圈"_ustr) >= 0
              || rProviderContent.indexOf(u"## 1."_ustr) >= 0; // impress write-back body

        const OUString capLow = m_sForcedCapability.toAsciiLowerCase();
        const bool bConsultCap
            = capLow == u"review"_ustr || capLow == u"chat"_ustr || capLow == u"summarize"_ustr
              || capLow == u"consult"_ustr;
        const bool bConsultIntent
            = kqoffice::ai::chat::DocumentAIDocumentTools::wantsConsultIntent(
                m_sLastPrompt, m_sForcedCapability, !sel.text.isEmpty());
        // Advisory / scorecard bodies (no write markers)
        const OUString bodyLow = rProviderContent.toAsciiLowerCase();
        const bool bAdvisoryProse
            = !bHasStructuredWriteMarker
              && (rProviderContent.indexOf(u"咨询"_ustr) >= 0
                  || rProviderContent.indexOf(u"打分"_ustr) >= 0
                  || rProviderContent.indexOf(u"分项"_ustr) >= 0
                  || rProviderContent.indexOf(u"方案A"_ustr) >= 0
                  || rProviderContent.indexOf(u"方案B"_ustr) >= 0
                  || rProviderContent.indexOf(u"硬规则"_ustr) >= 0
                  || (rProviderContent.indexOf(u"建议"_ustr) >= 0
                      && rProviderContent.indexOf(u"FIX|"_ustr) < 0)
                  || bodyLow.indexOf(u"score"_ustr) >= 0);

        const bool bAllowSelectionReplace
            = !sel.text.isEmpty() && !rProviderContent.isEmpty()
              && (sel.surface == u"writer"_ustr || sel.surface == u"calc"_ustr
                  || sel.surface == u"impress"_ustr)
              && !bConsultCap && !bConsultIntent && !bAdvisoryProse
              && (bHasStructuredWriteMarker
                  || kqoffice::ai::chat::DocumentAIDocumentTools::wantsEditIntent(
                      m_sLastPrompt, m_sForcedCapability, true)
                  || capLow == u"rewrite"_ustr || capLow == u"edit"_ustr
                  || capLow == u"polish"_ustr || capLow == u"quick-edit"_ustr
                  || capLow == u"translate"_ustr || capLow == u"translation"_ustr);

        if (bAllowSelectionReplace)
        {
            kqoffice::ai::chat::DiffOperation op;
            op.opType = u"replace"_ustr;
            // Writer: prefer selection target so approve replaces only the span.
            if (sel.surface == u"writer"_ustr)
                op.target = u"selection"_ustr;
            else
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
                op.newText = formula.isEmpty()
                                 ? kqoffice::ai::chat::AgentChatDiffApplier::sanitizeApplyText(
                                       rProviderContent)
                                 : formula;
            }
            else
                op.newText = kqoffice::ai::chat::AgentChatDiffApplier::sanitizeApplyText(
                    rProviderContent);
            aPlan.planId = u"ap-selection-replace"_ustr;
            aPlan.operations.clear();
            aPlan.operations.push_back(op);
            aPlan.rawOutput = rProviderContent;
        }
        else if (!sel.text.isEmpty() && (bConsultCap || bConsultIntent || bAdvisoryProse)
                 && !bHasStructuredWriteMarker)
        {
            AppendTranscript(
                u"System"_ustr,
                u"plan-stage-skip-selection-replace · consult/advisory · "
                u"有选区但不把建议正文当作替换稿 · 主文档未改"_ustr,
                /*bPersistHistory*/ false);
        }
    }

    if (!kqoffice::ai::chat::AgentChatDiffExtractor::validate(aPlan))
    {
        // Quality consult (质检/版式审等) often has no FIX/outline block — close cleanly.
        const OUString low = rProviderContent.toAsciiLowerCase();
        const bool bConsultLike
            = rProviderContent.indexOf(u"质检"_ustr) >= 0
              || rProviderContent.indexOf(u"分项"_ustr) >= 0
              || rProviderContent.indexOf(u"打分"_ustr) >= 0
              || rProviderContent.indexOf(u"版式"_ustr) >= 0
              || rProviderContent.indexOf(u"建议"_ustr) >= 0
              || rProviderContent.indexOf(u"清单"_ustr) >= 0
              || low.indexOf(u"score"_ustr) >= 0 || low.indexOf(u"review"_ustr) >= 0;
        AppendTranscript(
            u"System"_ustr,
            u"plan-stage-skipped reason=no-valid-apply-plan main-document-mutation=false"
            " · 未生成可写回计划 · 主文档未改"_ustr,
            /*bPersistHistory*/ false);
        if (bConsultLike)
        {
            AppendAssistantMarkdown(
                u"**咨询收口（主文档未改）**\n\n"
                u"本次是**建议/质检**，没有可自动写回的结构块（如 `FIX|`、大纲写回、公式写回）。\n\n"
                u"可选下一步：\n"
                u"1. **校对** — 让模型输出 `===可圈审阅修复===` + `FIX|旧|新` 再批准写回\n"
                u"2. **排版优化** — 输出 `H1|标题` 大纲写回块\n"
                u"3. 选中要改的句子后点 **改写/正式**，生成选区替换计划\n"
                u"4. 继续对话细化某条建议\n\n"
                u"_纪律：无批准不改主文档。_\n"_ustr);
            if (m_xStatusLabel)
                m_xStatusLabel->set_label(
                    u"质检/建议已完成 · 主文档未改 · 可校对写回或改选区"_ustr);
            SetAgentStepBar(u"步骤：咨询完成 · 主文档未改 · 可继续细化或生成写回块"_ustr);
        }
        else if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"未生成可写回计划 · 主文档未改 · 可改指令后重试"_ustr);
        return;
    }

    m_aPendingPlan = std::move(aPlan);
    m_bHasPendingPlan = true;
    m_bLastApplyCanUndo = false; // new pending plan supersedes prior undo affordance
    m_sPendingEvidenceId = rEvidenceId;
    m_bStaleNeedsRegen = false;
    if (m_xRetryButton)
        m_xRetryButton->set_label(u"重试"_ustr);
    // Snapshot baseline for stale-document guard (GenOffice mark-seen pattern).
    {
        const OUString snap = kqoffice::ai::chat::DocumentAIDocumentTools::computeSnapshotHash();
        if (!snap.isEmpty())
            kqoffice::ai::chat::DocumentAIDocumentTools::markSeen(snap);
    }
    const bool bWriterEngine
        = kqoffice::ai::chat::DocumentAIApply::hasWriterApplyEngineHook();
    const bool bCalcEngine = kqoffice::ai::chat::DocumentAIApply::hasCalcApplyEngineHook();
    const bool bImpressEngine
        = kqoffice::ai::chat::DocumentAIApply::hasImpressApplyEngineHook();
    // Honest engine path by surface (Writer native; Calc C1 / Impress I1 skeleton).
    OUString enginePathToken;
    OUString enginePathZh;
    if (m_aPendingPlan.planId == u"ap-chart-insert"_ustr
        || (!m_aPendingPlan.operations.empty()
            && m_aPendingPlan.operations.front().opType == u"chart_insert"_ustr))
    {
        enginePathToken = u"calc-chart-dispatch"_ustr;
        enginePathZh = u"批准后打开插入图表向导（须显式批准）"_ustr;
    }
    else if (sel.surface == u"calc"_ustr)
    {
        if (bCalcEngine)
        {
            enginePathToken = u"calc-apply-engine"_ustr;
            enginePathZh = u"表格 · Calc 原生骨架写回（cell-replace / cell-formula；"
                           "失败回退 UNO 轻量；图表仍走向导）"_ustr;
        }
        else
        {
            enginePathToken = u"uno-diff-applier"_ustr;
            enginePathZh
                = u"表格 · UNO 轻量写回（无原生 Calc ApplyEngine；公式/单元格/图表）"_ustr;
        }
    }
    else if (sel.surface == u"impress"_ustr)
    {
        if (bImpressEngine)
        {
            enginePathToken = u"impress-apply-engine"_ustr;
            enginePathZh = u"演示 · Impress 原生骨架写回（shape-text-replace；"
                           "失败回退 UNO 大纲成片/幻灯文案）"_ustr;
        }
        else
        {
            enginePathToken = u"uno-diff-applier"_ustr;
            enginePathZh
                = u"演示 · UNO 轻量写回（无原生 Impress ApplyEngine；大纲/幻灯文案）"_ustr;
        }
    }
    else
    {
        enginePathToken = bWriterEngine ? u"writer-apply-engine"_ustr : u"uno-diff-applier"_ustr;
        enginePathZh = bWriterEngine ? u"文字 · Writer 原生写回引擎"_ustr
                                     : u"文字 · UNO 回退写回（原生引擎未加载）"_ustr;
    }
    AppendTranscript(u"System"_ustr,
                     u"plan-staged plan="_ustr + m_aPendingPlan.planId + u" ops="_ustr
                         + OUString::number(
                             static_cast<sal_Int32>(m_aPendingPlan.operations.size()))
                         + u" evidence="_ustr + m_sPendingEvidenceId
                         + u" surface="_ustr + sel.surface + u" apply-path="_ustr
                         + enginePathToken + u" writer-engine="_ustr
                         + (bWriterEngine ? u"ready"_ustr : u"fallback-uno"_ustr)
                         + u" calc-engine="_ustr
                         + (bCalcEngine ? u"skeleton-ready"_ustr : u"fallback-uno"_ustr)
                         + u" impress-engine="_ustr
                         + (bImpressEngine ? u"skeleton-ready"_ustr : u"fallback-uno"_ustr)
                         + u" awaiting-approval=true main-document-mutation=false "
                           "explicit-human-approval-required=true · "_ustr
                         + enginePathZh);

    // Sprint B: formula dry-run + soft verify at stage time (no mutation).
    {
        const auto pre
            = kqoffice::ai::chat::DocumentAIVerify::verifyPlanBeforeApply(m_aPendingPlan,
                                                                          sel.surface);
        AppendTranscript(u"System"_ustr,
                         u"plan-verify · "_ustr + pre.summaryZh
                             + (pre.ok ? u" · 可批准"_ustr
                                       : (u" · "_ustr + pre.repairHintZh))
                             + u" · 主文档未改"_ustr,
                         /*bPersistHistory*/ false);
        if (!pre.ok && m_xStatusLabel)
            m_xStatusLabel->set_label(u"计划已暂存但校验有问题 · "_ustr + pre.summaryZh
                                      + u" · 主文档未改"_ustr);
    }

    if (m_aPendingPlan.planId == u"ap-chart-insert"_ustr
        || (!m_aPendingPlan.operations.empty()
            && m_aPendingPlan.operations.front().opType == u"chart_insert"_ustr))
        m_xStatusLabel->set_label(u"图表计划已暂存 — 批准后打开插入图表向导: "_ustr
                                  + m_aPendingPlan.planId);
    else if (m_xStatusLabel
             && m_xStatusLabel->get_label().indexOf(u"校验有问题"_ustr) < 0)
        m_xStatusLabel->set_label(u"计划已暂存 · "_ustr + enginePathZh + u" · 自动打开 Diff: "_ustr
                                  + m_aPendingPlan.planId);
    UpdatePendingPlanChip();
    UpdateSelectionChip();
    UpdateActions();
    LoadReviewQueue();
    // P0-2 / M15: competitor-parity approval UX — don't leave the user hunting for Diff.
    // Still no main-document mutation until explicit approve.
    PresentPendingPlanForApproval(u"auto-stage"_ustr);

    // Quality-core plans: one-line path to Diff / approve (slash /diff also works).
    if (m_aPendingPlan.planId == u"ap-review-fixes"_ustr
        || m_aPendingPlan.planId == u"ap-writer-outline-headings"_ustr
        || m_aPendingPlan.planId.indexOf(u"calc"_ustr) >= 0
        || m_aPendingPlan.planId.indexOf(u"formula"_ustr) >= 0
        || m_aPendingPlan.planId.indexOf(u"clean"_ustr) >= 0)
    {
        AppendTranscript(
            u"System"_ustr,
            u"质检写回已暂存 · 已打开 Diff · 也可点「查看 Diff」/ 待批芯片 / 输入 /diff · "
            u"批准前主文档不改"_ustr,
            /*bPersistHistory*/ false);
    }
}

bool AIChatPanel::ApplyPendingPlanWithApproval()
{
    if (!m_bHasPendingPlan)
    {
        m_sLastOutcomeDetail = u"当前没有待批写回计划"_ustr;
        AppendTranscript(
            u"System"_ustr,
            u"plan-apply-failed reason=no-pending-plan main-document-mutation=false"
            " · 当前没有待批写回计划 · 主文档未改"_ustr);
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"写回未执行 · 主文档未改 · 当前没有待批计划"_ustr);
        return false;
    }

    // M20: pre-flight stale — do not open permission dialog for a doomed plan.
    if (kqoffice::ai::chat::DocumentAIDocumentTools::isStale())
    {
        PresentStalePlanRecovery(u"pre-approve"_ustr);
        return false;
    }

    const OUString sPlanId = m_aPendingPlan.planId;
    const sal_Int32 nOps = static_cast<sal_Int32>(m_aPendingPlan.operations.size());
    const bool bChart = sPlanId == u"ap-chart-insert"_ustr
                        || (!m_aPendingPlan.operations.empty()
                            && m_aPendingPlan.operations.front().opType == u"chart_insert"_ustr);

    // Wave D4: clarify card before any document mutation (Deny aborts; AllowSession caches).
    {
        kqoffice::ai::control::ClarificationPrompt aPrompt;
        aPrompt.actionId = u"ai.apply-plan"_ustr;
        if (bChart)
        {
            aPrompt.messageZh = u"即将打开图表插入向导（计划 "_ustr + sPlanId + u"，共 "_ustr
                                + OUString::number(nOps) + u" 项操作）。是否继续？"_ustr;
        }
        else
        {
            aPrompt.messageZh = u"将把 "_ustr + OUString::number(nOps)
                                + u" 项修改写入文档（计划 "_ustr + sPlanId
                                + u"）。是否继续？"_ustr;
        }
        const kqoffice::ai::control::ClarificationResult aPerm
            = sfx2::ShowPermissionPrompt(GetFrameWeld(), aPrompt);
        if (aPerm.decision == kqoffice::ai::control::PermissionDecision::Deny)
        {
            AppendTranscript(
                u"System"_ustr,
                u"plan-apply-denied plan="_ustr + sPlanId + u" ops="_ustr
                    + OUString::number(nOps)
                    + u" from-session-cache="_ustr
                    + (aPerm.fromSessionCache ? u"true"_ustr : u"false"_ustr)
                    + u" main-document-mutation=false explicit-human-approval=denied"
                      " · 已拒绝写回权限 · 主文档未改"_ustr);
            SetAgentStepBar(u"步骤：已拒绝写回权限 · 主文档未改"_ustr);
            if (m_xStatusLabel)
                m_xStatusLabel->set_label(u"已拒绝写回权限 · 主文档未改 · 计划="_ustr
                                          + sPlanId);
            // Keep pending plan so the user can re-approve later.
            return false;
        }
        // AllowOnce / AllowSession (incl. session-cache auto-allow) proceed to mutate.
    }

    SetAgentStepBar(bChart ? u"步骤：批准写回 · 打开图表向导…"_ustr
                           : u"步骤：批准写回 · 应用中…"_ustr);

    // Pre-apply verify + formula dry-run. Hard-fail no longer silent: secondary confirm.
    // Deny keeps pending plan; AllowOnce/Session proceeds (user accepts risk).
    if (!bChart)
    {
        const auto pre = kqoffice::ai::chat::DocumentAIVerify::verifyPlanBeforeApply(m_aPendingPlan);
        auto snap = kqoffice::ai::chat::DocumentAIFormulaDryRun::captureSelectionSnapshot(128);
        const auto dry = kqoffice::ai::chat::DocumentAIFormulaDryRun::checkPlan(
            m_aPendingPlan, snap.empty() ? nullptr : &snap);
        const bool bVerifyBad = !pre.ok && pre.failedOps > 0;
        const bool bDryBad = dry.hasWork() && !dry.allOk();
        if (bVerifyBad || bDryBad)
        {
            OUStringBuffer warn;
            warn.append(u"写回前校验未完全通过：\n"_ustr);
            if (bVerifyBad)
            {
                warn.append(u"· "_ustr);
                warn.append(pre.summaryZh);
                if (!pre.repairHintZh.isEmpty())
                {
                    warn.append(u"\n· "_ustr);
                    warn.append(pre.repairHintZh);
                }
                warn.append(u"\n"_ustr);
            }
            if (bDryBad)
            {
                warn.append(u"· 公式 dry-run："_ustr);
                warn.append(dry.summaryZh);
                warn.append(u"\n"_ustr);
            }
            warn.append(u"\n仍要写入文档吗？（拒绝则保留待批计划，主文档不改）"_ustr);

            kqoffice::ai::control::ClarificationPrompt aWarn;
            aWarn.actionId = u"ai.apply-plan-despite-verify"_ustr;
            aWarn.messageZh = warn.makeStringAndClear();
            const kqoffice::ai::control::ClarificationResult aForce
                = sfx2::ShowPermissionPrompt(GetFrameWeld(), aWarn);
            if (aForce.decision == kqoffice::ai::control::PermissionDecision::Deny)
            {
                AppendTranscript(
                    u"System"_ustr,
                    u"plan-apply-blocked plan="_ustr + sPlanId + u" · verify/dry-run 未通过 · "
                    u"用户拒绝强制写回 · main-document-mutation=false"_ustr);
                SetAgentStepBar(u"步骤：写回已取消 · 校验未通过 · 主文档未改"_ustr);
                if (m_xStatusLabel)
                    m_xStatusLabel->set_label(u"已取消强制写回 · 待批计划保留 · 主文档未改"_ustr);
                return false;
            }
            AppendTranscript(
                u"System"_ustr,
                u"plan-apply-override plan="_ustr + sPlanId
                    + u" · 用户确认在校验告警下写回 · explicit-human-approval=true"_ustr,
                /*bPersistHistory*/ false);
        }
    }

    // Sprint C: passive local evidence screenshots (no upload). Soft-fail only.
    OUString sPreShot;
    OUString sPostShot;
    {
        const auto shotPrefs = kqoffice::ai::chat::DocumentAIInputPrefs::load();
        if (shotPrefs.applyCaptureEvidence && !bChart)
        {
            const auto preCap
                = kqoffice::ai::chat::DocumentAIScreenCapture::capturePassiveEvidence(
                    u"pre-apply"_ustr);
            if (preCap.success)
            {
                sPreShot = preCap.path;
                AppendTranscript(u"System"_ustr,
                                 u"evidence-shot pre-apply · "_ustr + sPreShot
                                     + u" · 本地 PNG · 不上传"_ustr,
                                 /*bPersistHistory*/ false);
            }
        }
    }

    // Document AI Fabric: Writer → native ApplyEngine (undo-grouped);
    // Calc/Impress → UNO DiffApplier; chart → InsertObjectChart dispatch.
    // Only after explicit human approval + permission prompt.
    kqoffice::ai::chat::DocumentAIApplyResult aResult
        = kqoffice::ai::chat::DocumentAIApply::applyApprovedWithRawFallback(
            m_aPendingPlan, m_aPendingPlan.rawOutput);

    // Sprint B: one automatic retry on non-stale apply failure (approval already granted).
    bool bRetried = false;
    if (!aResult.success && !bChart
        && !kqoffice::ai::chat::DocumentAIDocumentTools::isStaleApplyError(aResult.error)
        && aResult.error.indexOf(u"stale"_ustr) < 0)
    {
        bRetried = true;
        AppendTranscript(u"System"_ustr,
                         u"plan-apply-retry plan="_ustr + sPlanId
                             + u" · 首次失败，自动再试一次 · 主文档状态取决于引擎"_ustr,
                         /*bPersistHistory*/ false);
        aResult = kqoffice::ai::chat::DocumentAIApply::applyApprovedWithRawFallback(
            m_aPendingPlan, m_aPendingPlan.rawOutput);
    }

    if (aResult.success)
    {
        kqoffice::ai::chat::DocumentAIDocumentTools::clearSeen();
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

        // Sprint C: post-apply evidence shot (local only).
        if (!bChart)
        {
            const auto shotPrefs = kqoffice::ai::chat::DocumentAIInputPrefs::load();
            if (shotPrefs.applyCaptureEvidence)
            {
                const auto postCap
                    = kqoffice::ai::chat::DocumentAIScreenCapture::capturePassiveEvidence(
                        u"post-apply"_ustr);
                if (postCap.success)
                {
                    sPostShot = postCap.path;
                    AppendTranscript(u"System"_ustr,
                                     u"evidence-shot post-apply · "_ustr + sPostShot
                                         + u" · 本地 PNG · 不上传"_ustr,
                                     /*bPersistHistory*/ false);
                }
            }
        }

        // Platform undo hint (write-back is undo-grouped when engine supports it).
#if defined(MACOSX)
        const OUString undoHint = u"⌘Z 撤销本步写回"_ustr;
#else
        const OUString undoHint = u"Ctrl+Z 撤销本步写回"_ustr;
#endif
        AppendTranscript(u"System"_ustr,
                         u"plan-applied plan="_ustr + sPlanId + u" ops="_ustr
                             + OUString::number(nOps) + u" applied="_ustr
                             + OUString::number(aResult.appliedCount) + u" engine="_ustr
                             + aResult.engine + u" surface="_ustr + aResult.surface
                             + u" evidence="_ustr + evid
                             + u" explicit-human-approval=true main-document-mutation=true"_ustr
                             + (bRetried ? u" retried=true"_ustr : OUString())
                             + (bChart ? u" chart-wizard=opened"_ustr : OUString())
                             + (sPreShot.isEmpty() ? OUString()
                                                   : (u" pre-shot="_ustr + sPreShot))
                             + (sPostShot.isEmpty() ? OUString()
                                                    : (u" post-shot="_ustr + sPostShot))
                             + u" · "_ustr + undoHint);

        // Post-apply soft verify (partial apply / formula / spot-check). Card + undo CTA.
        if (!bChart)
        {
            const auto post = kqoffice::ai::chat::DocumentAIVerify::verifyAfterApply(
                m_aPendingPlan, aResult, aResult.surface);
            AppendTranscript(u"System"_ustr,
                             u"plan-post-verify · "_ustr + post.status + u" · "_ustr + post.summaryZh
                                 + (post.repairHintZh.isEmpty()
                                        ? OUString()
                                        : (u" · "_ustr + post.repairHintZh)),
                             /*bPersistHistory*/ false);
            // User-visible honesty card (Grok-style evidence, not only system log).
            const OUString card
                = post.cardZh.isEmpty()
                      ? kqoffice::ai::chat::DocumentAIVerify::formatPostApplyCard(post)
                      : post.cardZh;
            if (!card.isEmpty()
                && (post.status == u"soft-fail"_ustr || post.status == u"soft-warn"_ustr
                    || post.status == u"soft-ok"_ustr))
                AppendAssistantMarkdown(card);
            if (post.status == u"soft-fail"_ustr)
            {
                SetAgentStepBar(u"步骤：✓ 已写回 · 校验未通过 · 请撤销 · "_ustr + post.summaryZh);
                if (m_xStatusLabel)
                    m_xStatusLabel->set_label(
                        u"写回后校验未通过 · 请点「撤销写回」· "_ustr + post.summaryZh);
                // Make undo strip unmissable after honest soft-fail.
                if (m_xApprovalActionRow)
                    m_xApprovalActionRow->set_visible(true);
                if (m_xChatUndoBtn)
                {
                    m_xChatUndoBtn->set_sensitive(true);
                    m_xChatUndoBtn->set_visible(true);
                }
                if (m_xApprovalHintLabel)
                    m_xApprovalHintLabel->set_label(
                        u"校验未通过 · 请优先点「撤销写回」回退，再改指令重跑"_ustr);
            }
            else if (post.status == u"soft-warn"_ustr)
            {
                SetAgentStepBar(u"步骤：✓ 已写回 · 校验提示 · 可撤销 · "_ustr + post.summaryZh);
                if (m_xApprovalActionRow)
                    m_xApprovalActionRow->set_visible(true);
                if (m_xApprovalHintLabel)
                    m_xApprovalHintLabel->set_label(
                        u"写回有提示 · 可目视正文，不满意请「撤销写回」"_ustr);
            }
        }

        // Vision evidence loop: local pre/post meta + optional light text describe (no upload).
        if (!bChart && (!sPreShot.isEmpty() || !sPostShot.isEmpty()))
        {
            auto vision = kqoffice::ai::chat::DocumentAIVisionEvidence::buildReport(
                sPreShot, sPostShot, m_aPendingPlan, aResult, aResult.surface);
            // One evidence card only; full checklist only when soft-warn (less noise).
            AppendTranscript(u"System"_ustr, vision.cardZh, /*bPersistHistory*/ false);
            if (vision.status == u"soft-warn"_ustr && !vision.diffCardZh.isEmpty())
                AppendTranscript(u"System"_ustr, vision.diffCardZh, /*bPersistHistory*/ false);
            AppendTranscript(
                u"System"_ustr,
                u"vision-evidence · "_ustr
                    + kqoffice::ai::chat::DocumentAIVisionEvidence::formatStatusLine(vision)
                    + (vision.resolvedVisionModel.isEmpty()
                           ? OUString()
                           : (u" · model="_ustr + vision.resolvedVisionModel)),
                /*bPersistHistory*/ false);
            if (vision.status == u"soft-warn"_ustr
                && m_xAgentStepBar
                && m_xAgentStepBar->get_label().indexOf(u"校验警告"_ustr) < 0)
                SetAgentStepBar(u"步骤：✓ 已写回 · Vision 提示 · "_ustr + vision.summaryZh);

            const auto vPrefs = kqoffice::ai::chat::DocumentAIInputPrefs::load();
            if (vPrefs.applyVisionDescribe)
            {
                bool described = false;
                // 1) Prefer local multimodal: Ollama loopback / visionCmd reads PNG bytes.
                if (vPrefs.applyVisionLocalMultimodal
                    && (vision.hasPre || vision.hasPost))
                {
                    if (m_xStatusLabel)
                        m_xStatusLabel->set_label(
                            u"Vision · 本机多模态审计中… · 截图不上传公网"_ustr);
                    if (Application::IsInMain())
                        Application::Reschedule(true);
                    const auto local
                        = kqoffice::ai::chat::DocumentAIVisionEvidence::describeWithLocalImages(
                            vision, m_aPendingPlan, vPrefs.visionModel, vPrefs.visionCmd);
                    if (local.status == u"ok"_ustr && !local.contentZh.isEmpty())
                    {
                        vision.modelCommentZh = local.contentZh;
                        AppendTranscript(
                            u"System"_ustr,
                            u"vision-local · "_ustr + local.backend + u" · "_ustr
                                + local.modelHint + u" · usedImages="_ustr
                                + (local.usedLocalImages ? u"1"_ustr : u"0"_ustr)
                                + u" · publicNet=0\n"_ustr + local.contentZh,
                            /*bPersistHistory*/ false);
                        described = true;
                    }
                    else if (!local.contentZh.isEmpty())
                    {
                        AppendTranscript(u"System"_ustr,
                                         u"vision-local · "_ustr + local.status + u" · "_ustr
                                             + local.contentZh,
                                         /*bPersistHistory*/ false);
                    }
                }
                // 2) Fallback: light text-only describe (no image bytes).
                if (!described && !vision.modelCommentPrompt.isEmpty())
                {
                    if (m_xStatusLabel)
                        m_xStatusLabel->set_label(u"Vision · 轻量文字审计中… · 不上传截图"_ustr);
                    if (Application::IsInMain())
                        Application::Reschedule(true);
                    const auto note = kqoffice::ai::AgentStepRunner::runOne(
                        u"summarize"_ustr, vision.modelCommentPrompt, 20000);
                    if (note.status == u"ok"_ustr && !note.content.isEmpty())
                    {
                        vision.modelCommentZh = note.content;
                        AppendTranscript(u"System"_ustr,
                                         u"vision-describe · "_ustr + note.content,
                                         /*bPersistHistory*/ false);
                    }
                }
            }
        }

        if (bChart)
        {
            SetAgentStepBar(u"步骤：✓ 已批准 · 图表向导已打开 · "_ustr + undoHint);
            AppendTranscript(u"System"_ustr,
                             u"已打开插入图表向导；请在向导中完成放置。文档变更可撤销（"_ustr
                                 + undoHint + u"）。"_ustr,
                             /*bPersistHistory*/ false);
        }
        else
        {
            const OUString sEngineZh
                = kqoffice::ai::chat::DocumentAIApply::userFacingEngineZh(aResult.engine);
            // Prefer undo-visible step bar even if vision/verify also set a warning line.
            if (m_xAgentStepBar)
            {
                const OUString cur = m_xAgentStepBar->get_label();
                if (cur.indexOf(u"校验警告"_ustr) >= 0)
                    SetAgentStepBar(u"步骤：✓ 已写回 · 校验警告 · "_ustr + undoHint);
                else if (cur.indexOf(u"Vision"_ustr) >= 0)
                    SetAgentStepBar(u"步骤：✓ 已写回 · Vision 提示 · "_ustr + undoHint);
                else
                    SetAgentStepBar(u"步骤：✓ 已批准写回 · "_ustr + sEngineZh + u" · "_ustr
                                    + undoHint);
            }
            AppendTranscript(
                u"System"_ustr,
                u"写回完成 · 已应用 "_ustr + OUString::number(aResult.appliedCount)
                    + u" 项 · "_ustr + undoHint
                    + u" · 编辑 → 撤销 / 或工具栏撤销 · 可继续改写"_ustr,
                /*bPersistHistory*/ false);
            if (aResult.surface == u"impress"_ustr)
            {
                AppendTranscript(
                    u"System"_ustr,
                    u"设计流：已写回 "_ustr + OUString::number(aResult.appliedCount)
                        + u" 页 · "_ustr + undoHint
                        + u" · 下一步「④导出」或 文件→导出为→PPTX…（须你选路径；不静默导出）"_ustr,
                    /*bPersistHistory*/ false);
            }
            // Writer ApplyEngine already opens Diff Review; for UNO path (Calc/Impress)
            // open the shared Diff Review dialog so write-back UX stays aligned.
            TryShowDiffReviewAfterApply(sPlanId, aResult.engine, true);
        }
        ClearPendingPlan();
        m_sLastOutcomeDetail.clear();
        m_bLastApplyCanUndo = !bChart; // chart wizard: user finishes placement; undo still via app
        if (bChart)
            m_bLastApplyCanUndo = true;
        SetState(AIChatPanelState::Applied);
        UpdateApprovalChrome();
        {
            const OUString sEngineZh
                = kqoffice::ai::chat::DocumentAIApply::userFacingEngineZh(aResult.engine);
            m_xStatusLabel->set_label(u"已写回 · "_ustr + undoHint + u" · 可点「撤销写回」· "_ustr
                                      + sEngineZh + u" · "_ustr + sPlanId
                                      + (evid.isEmpty() ? OUString()
                                                        : (u" · 证据 "_ustr + evid)));
        }
        kqoffice::ai::workbench::WorkTelemetryStore::recordSimple(u"ai_apply"_ustr, 1);
        RecordWorkspaceActivity(u"action-invoked"_ustr, u"reviews"_ustr, sPlanId, evid,
                                OUString(), bChart ? u"chart-insert"_ustr : u"diff-review"_ustr);
        return true;
    }

    // M20: apply-time stale → dedicated regenerate path (drop plan, arm 重新生成).
    if (kqoffice::ai::chat::DocumentAIDocumentTools::isStaleApplyError(aResult.error)
        || (aResult.engine == u"none"_ustr
            && kqoffice::ai::chat::DocumentAIDocumentTools::isStale()))
    {
        PresentStalePlanRecovery(u"apply"_ustr);
        RecordWorkspaceActivity(u"failure-reported"_ustr, u"reviews"_ustr, sPlanId, OUString(),
                                OUString(), u"stale-plan"_ustr);
        return false;
    }

    const OUString sFailReason = ShortenUserDetail(
        kqoffice::ai::chat::DocumentAIApply::userFacingErrorZh(
            aResult.error, aResult.engine, aResult.surface));
    const OUString sEngineZh
        = kqoffice::ai::chat::DocumentAIApply::userFacingEngineZh(aResult.engine);
    m_sLastOutcomeDetail = sFailReason;
    m_bStaleNeedsRegen = false;
    SetAgentStepBar(u"步骤：写回失败 · 主文档未改 · "_ustr + sFailReason);
    // Audit evidence for failed apply (UI only shows short Chinese summary).
    OUString failEvid = m_sPendingEvidenceId;
    {
        kqoffice::ai::EvidenceRecord rec;
        rec.serviceMode = u"offline"_ustr;
        rec.provider = aResult.engine.isEmpty() ? u"apply"_ustr : aResult.engine;
        rec.capability = u"apply"_ustr;
        rec.status = u"provider-error"_ustr;
        rec.requestSizeBytes = nOps;
        rec.responseSizeBytes = 0;
        rec.durationMs = 0;
        kqoffice::ai::EvidenceRecorder recorder;
        const OUString auditId = recorder.record(rec);
        if (failEvid.isEmpty())
            failEvid = auditId;
    }
    AppendTranscript(u"System"_ustr,
                     u"plan-apply-failed plan="_ustr + sPlanId + u" error="_ustr + sFailReason
                         + u" engine="_ustr + aResult.engine + u" surface="_ustr
                         + aResult.surface
                         + u" main-document-mutation=false explicit-human-approval=true"
                           " · 写回失败 · 主文档未改 · "_ustr
                         + sEngineZh
                         + (failEvid.isEmpty() ? OUString()
                                               : (u" · 证据 "_ustr + failEvid)));
    m_xStatusLabel->set_label(u"写回失败 · 主文档未改 · "_ustr + sFailReason + u" · "_ustr
                              + sEngineZh
                              + (failEvid.isEmpty() ? OUString()
                                                    : (u" · 证据 "_ustr + failEvid))
                              + u" · 计划="_ustr + sPlanId);
    RecordWorkspaceActivity(u"failure-reported"_ustr, u"reviews"_ustr, sPlanId,
                            failEvid, OUString(), u"diff-review"_ustr);
    // Keep pending plan so the user can retry after fixing context.
    return false;
}

IMPL_LINK_NOARG(AIChatPanel, OnPromptChanged, weld::Entry&, void)
{
    UpdateContextMentions();
    UpdateComposerChrome();
    UpdateActivityCard();
    UpdateActions();
}

IMPL_LINK_NOARG(AIChatPanel, OnIntentRewriteClicked, weld::Button&, void)
{
    const OUString surface = CurrentDocumentSurface();
    if (surface == u"calc"_ustr)
        ApplyComposerIntent(
            u"rewrite"_ustr,
            u"【表格公式 · 写回须批准】请根据选区生成可写入单元格的公式："
            "1) 第一行必须以 = 开头 2) 一行中文解释 3) 空值/文本混数字边界。"
            "不要声称已改表格。"_ustr);
    else if (surface == u"impress"_ustr)
        ApplyComposerIntent(u"rewrite"_ustr, u"请改写本页标题与要点，更清晰专业："_ustr);
    else
        ApplyComposerIntent(u"rewrite"_ustr, u"请改写得更清晰专业："_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnIntentFormalClicked, weld::Button&, void)
{
    const OUString surface = CurrentDocumentSurface();
    if (surface == u"calc"_ustr)
    {
        ApplyComposerIntent(
            u"summarize"_ustr,
            u"【数据解读 · 咨询】请解释选区：各列含义、异常值、可写 = 公式建议（公式单独成行）。"
            "仅依据表内数据，勿编造。"_ustr);
        return;
    }
    ApplyComposerIntent(
        u"formal"_ustr,
        u"请将以下内容改成正式、得体的书面语气（公文/商务均可），保留关键事实与数据，输出完整改写："_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnIntentShortenClicked, weld::Button&, void)
{
    const OUString surface = CurrentDocumentSurface();
    if (surface == u"calc"_ustr)
        ApplyComposerIntent(
            u"shorten"_ustr,
            u"【数据清洗 · 咨询+可提议公式】检查选区：①空值 ②重复 ③类型混杂 ④离群；"
            "每条含位置线索+问题+建议 = 公式或步骤。表格不自动改。"_ustr);
    else
        ApplyComposerIntent(u"shorten"_ustr, u"请精简以下内容，保留要点："_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnIntentExpandClicked, weld::Button&, void)
{
    const OUString surface = CurrentDocumentSurface();
    if (surface == u"calc"_ustr)
        ApplyComposerIntent(
            u"expand"_ustr,
            u"【汇总 · 写回须批准】对选区给出合计/平均/计数等：第一行 = 公式；随后说明口径。"
            "写入须用户批准。"_ustr);
    else if (surface == u"impress"_ustr)
        ApplyComposerIntent(
            u"expand"_ustr,
            u"【仅大纲 · 禁止 ## 写回体】请输出页序大纲（标题+目的+页数建议）："_ustr);
    else
        ApplyComposerIntent(u"expand"_ustr, u"请扩写并补充细节："_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnIntentSummarizeClicked, weld::Button&, void)
{
    const OUString surface = CurrentDocumentSurface();
    if (surface == u"impress"_ustr)
        ApplyComposerIntent(u"summarize"_ustr, u"请根据幻灯写一份讲稿要点："_ustr);
    else if (surface == u"calc"_ustr)
        ApplyComposerIntent(
            u"summarize"_ustr,
            u"【数据解读 · 咨询】解读选区/可见数据：关键结论 3–7 条、趋势/对比、风险异常、"
            "可跟进分析问题。仅依据表内数据。"_ustr);
    else
        ApplyComposerIntent(
            u"summarize"_ustr,
            u"【全文总结 · 咨询】请基于当前文档骨架与必要读块，输出：一句话摘要；3–7 条要点；"
            "未决问题/行动项。仅依据文档，勿编造。"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnIntentOutlineClicked, weld::Button&, void)
{
    const OUString surface = CurrentDocumentSurface();
    if (surface == u"impress"_ustr)
        ApplyComposerIntent(
            u"plan"_ustr,
            u"【仅大纲 · 禁止 ## 写回体】请输出页序大纲（标题+目的+页数建议）："_ustr);
    else if (surface == u"calc"_ustr)
        ApplyComposerIntent(u"plan"_ustr,
                            u"请根据表格字段给出分析大纲（指标/维度/异常检查）："_ustr);
    else
        ApplyComposerIntent(
            u"plan"_ustr,
            u"【文档结构 · 咨询优先 · 主文档不自动改】请基于当前文档骨架输出："
            "1) 全文逻辑大纲 2) 每章目的 3) 结构问题 4) 建议标题层级。"
            "若需改主文档，用提议格式待用户批准。"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnIntentProofreadClicked, weld::Button&, void)
{
    ApplyComposerIntent(
        u"review"_ustr,
        u"【全文审阅 · 主文档不自动改】请输出清单："
        "① 事实/数据风险 ② 语气与得体 ③ 冗余与歧义 ④ 结构建议；"
        "每条含位置线索+问题+改写示例。不要声称已改主文档。"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnIntentContinueClicked, weld::Button&, void)
{
    ApplyComposerIntent(
        u"expand"_ustr,
        u"【续写 · 写回须批准】请接在当前选区末尾（无选区则接文档逻辑结尾）续写 2–4 段，"
        "保持语气与事实一致。输出完整续写正文；主文档须用户批准后才改。"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnIntentPlanClicked, weld::Button&, void)
{
    // Force work-plan gate on next send (Grok-style plan mode entry).
    m_bForceWorkPlanOnce = true;
    const OUString surface = CurrentDocumentSurface();
    if (surface == u"impress"_ustr)
        ApplyComposerIntent(
            u"plan"_ustr,
            u"/plan 【演示】先出工作计划再执行：页序/多方案/写回范围（不要 ## 写回体，待确认计划后）："_ustr);
    else if (surface == u"writer"_ustr || surface.isEmpty())
        ApplyComposerIntent(
            u"plan"_ustr,
            u"/plan 【文档】先出工作计划再改稿：目标读者、章节/改动范围、验收标准："_ustr);
    else if (surface == u"calc"_ustr)
        ApplyComposerIntent(
            u"plan"_ustr,
            u"/plan 【表格】先出工作计划：清洗/公式/汇总范围与验收："_ustr);
    else
        ApplyComposerIntent(u"plan"_ustr, u"/plan 请先给出可确认的工作计划："_ustr);
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"规划模式 · 发送后先出计划，确认后再生成草案"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnIntentAgentClicked, weld::Button&, void)
{
    if (CurrentDocumentSurface() == u"impress"_ustr)
        RunScenarioById(u"design-apply"_ustr);
    else
        ApplyComposerIntent(u"agent"_ustr, u"/agent 多步协作："_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnDesignStepOutlineClicked, weld::Button&, void)
{
    RunScenarioById(u"design-outline"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnDesignStepVariantsClicked, weld::Button&, void)
{
    RunScenarioById(u"design-variants"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnDesignStepApplyClicked, weld::Button&, void)
{
    // Step ③: generate ## write-back body → StagePending → auto Diff (PresentPendingPlan).
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(
            u"设计流③：生成可写回幻灯体 · 须批准才写回 · 主文档尚未改"_ustr);
    SetAgentStepBar(u"步骤：设计流③ · 选一写回（须批准）…"_ustr);
    RunScenarioById(u"design-apply"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnDesignStepExportClicked, weld::Button&, void)
{
    RunScenarioById(u"design-export"_ustr);
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
    // Pending ApplyPlan: open shared DiffReview preview (same deck as post-approve).
    if (m_bHasPendingPlan && !FindSelectedArtifact())
    {
        TryShowDiffReviewAfterApply(m_aPendingPlan.planId, u"pending-preview"_ustr, false);
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(
                u"已打开 Diff 预览 · 主文档尚未修改 · 确认后点「批准写回」"_ustr);
        AppendTranscript(
            u"System"_ustr,
            u"diff-review-opened source=review-tab plan="_ustr + m_aPendingPlan.planId
                + u" applied=false main-document-mutation=false"_ustr,
            /*bPersistHistory*/ false);
        return;
    }

    const AIChatContentRegistryEntry* pSelected = FindSelectedArtifact();
    if (!pSelected)
    {
        const OUString reason = u"请先选择一项生成内容再打开 Diff，或先生成待批计划"_ustr;
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"Diff 未打开 · "_ustr + reason);
        AppendTranscript(u"System"_ustr,
                         u"diff-review-failed reason=no-selection · "_ustr + reason,
                         /*bPersistHistory*/ false);
        return;
    }

    if (!DispatchWorkspaceAction(u"open-diff-review"_ustr))
    {
        const OUString reason = u"工作台动作未就绪或当前项不可 Diff"_ustr;
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"Diff 未打开 · "_ustr + reason);
        AppendTranscript(u"System"_ustr,
                         u"diff-review-failed reason=dispatch id="_ustr + pSelected->ObjectId
                             + u" · "_ustr + reason,
                         /*bPersistHistory*/ false);
        return;
    }

    AIChatContentOpener aOpener;
    const AIChatContentOpenResult aResult = aOpener.OpenReadOnlyPreview(*pSelected);
    AppendTranscript(u"System"_ustr, aResult.Message);
    if (aResult.Success)
    {
        m_xStatusLabel->set_label(u"已打开 Diff/预览 · "_ustr + pSelected->ObjectId);
    }
    else
    {
        const OUString detail
            = aResult.Message.isEmpty() ? u"未知原因"_ustr : aResult.Message;
        m_xStatusLabel->set_label(u"Diff 打开失败 · "_ustr + detail);
        AppendTranscript(u"System"_ustr,
                         u"diff-review-failed id="_ustr + pSelected->ObjectId + u" reason="_ustr
                             + detail + u" · 主文档未改"_ustr,
                         /*bPersistHistory*/ false);
    }
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
    {
        // If user selected a review-queue row, transition that id first.
        if (m_xReviewTree && m_xReviewQueueStore)
        {
            const int nSel = m_xReviewTree->get_selected_index();
            if (nSel >= 0)
            {
                const OUString sId = m_xReviewTree->get_id(nSel);
                if (!sId.isEmpty())
                    m_xReviewQueueStore->TransitionState(sId, u"approved"_ustr);
            }
        }
        DispatchWorkspaceAction(u"approve-selected"_ustr);
    }
    LoadArtifactNavigator();
    LoadReviewQueue();
    UpdateActions();
}

void AIChatPanel::RejectPendingPlan()
{
    if (!m_bHasPendingPlan)
        return;

    const OUString sPlanId = m_aPendingPlan.planId;
    // Keep English evidence token plan-rejected for harnesses/audit; UI status stays zh-CN.
    AppendTranscript(
        u"System"_ustr,
        u"plan-rejected plan="_ustr + sPlanId
            + u" main-document-mutation=false · 已拒绝写回 · 主文档未改 · 需再次发送才会产生新计划"_ustr);
    ClearPendingPlan();
    m_sLastOutcomeDetail.clear();
    SetState(AIChatPanelState::Idle);
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"已拒绝写回 · 主文档未改 · 计划="_ustr + sPlanId);
    SetAgentStepBar(u"步骤：已拒绝 · 主文档未改"_ustr);
    LoadArtifactNavigator();
    LoadReviewQueue();
    UpdateActions();
}

IMPL_LINK_NOARG(AIChatPanel, OnRejectSelectedClicked, weld::Button&, void)
{
    if (m_bHasPendingPlan)
        RejectPendingPlan();
    else
    {
        if (m_xReviewTree && m_xReviewQueueStore)
        {
            const int nSel = m_xReviewTree->get_selected_index();
            if (nSel >= 0)
            {
                const OUString sId = m_xReviewTree->get_id(nSel);
                if (!sId.isEmpty())
                    m_xReviewQueueStore->TransitionState(sId, u"rejected"_ustr);
            }
        }
        DispatchWorkspaceAction(u"reject-selected"_ustr);
        LoadArtifactNavigator();
        LoadReviewQueue();
        UpdateActions();
    }
}

IMPL_LINK_NOARG(AIChatPanel, OnAgentRunClicked, weld::Button&, void)
{
    if (!m_xPromptEntry)
        return;
    const OUString sPrompt = m_xPromptEntry->get_text().trim();
    if (sPrompt.isEmpty())
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"请先在输入框填写任务目标"_ustr);
        AppendTranscript(u"System"_ustr,
                         u"运行任务取消：输入框为空。请描述目标后点「运行任务」。"_ustr,
                         /*bPersistHistory*/ false);
        FocusPrompt();
        return;
    }
    if (m_xOptAgentPipeline)
        m_xOptAgentPipeline->set_active(true);
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"Agent Mode：绑定 → 规划 · 完成后请点「继续」…"_ustr);
    SubmitPrompt();
}

IMPL_LINK_NOARG(AIChatPanel, OnAgentContinueClicked, weld::Button&, void)
{
    if (!m_bAgentAwaitingContinue || m_sAgentGatePlanContent.isEmpty())
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"暂无可继续任务 · 请先「运行任务」完成规划"_ustr);
        return;
    }
    if (m_bAgentRunActive || m_bSubmitInFlight)
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"任务进行中 · 请稍候或点「停止」"_ustr);
        return;
    }

    m_bCancelRequested = false;
    m_bAgentRunActive = true;
    if (m_xAgentContinueBtn)
        m_xAgentContinueBtn->set_sensitive(false);
    UpdateAgentStepBar(2, u"执行中…"_ustr);
    SetAgentStepBar(u"Agent Mode · 执行 → 审查 → 校验…"_ustr);
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"继续执行 · 生成写回草案 · 主文档未改"_ustr);
    AppendTranscript(u"System"_ustr, u"用户点「继续」· 开始执行/审查/校验 · 主文档未改"_ustr,
                     /*bPersistHistory*/ false);
    if (Application::IsInMain())
        Application::Reschedule(true);

    const OUString sGoal = m_sAgentGateGoal;
    const OUString sContext = m_sAgentGateContext;
    const OUString sSurface = m_sAgentGateSurface;
    const OUString sDocTools = m_sAgentGateDocTools;
    const OUString sPlan = m_sAgentGatePlanContent;
    // Clear gate before long call so stop/clear can't double-fire execute.
    m_bAgentAwaitingContinue = false;

    const kqoffice::ai::AgentPipelineResult pipe
        = kqoffice::ai::AgentStepRunner::runCeilingExecutePhase(sGoal, sPlan, sContext, sSurface,
                                                                sDocTools);
    m_bAgentRunActive = false;
    m_sAgentGateGoal.clear();
    m_sAgentGateContext.clear();
    m_sAgentGateSurface.clear();
    m_sAgentGateDocTools.clear();
    m_sAgentGatePlanContent.clear();

    if (m_bCancelRequested)
    {
        MarkAgentStepsStopped();
        m_sLastOutcomeDetail = u"用户停止多步协作"_ustr;
        SetState(AIChatPanelState::Cancelled);
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"已停止 · 主文档未改"_ustr);
        AppendTerminalEvidence(u"cancelled"_ustr, pipe.finalEvidenceId);
        FocusPrompt();
        return;
    }

    // Append execute-phase steps to tree (keep prior plan rows).
    for (size_t i = 0; i < pipe.steps.size(); ++i)
    {
        const auto& st = pipe.steps[i];
        OUString title = st.stepKind;
        if (title == u"act"_ustr)
            title = u"2. 执行"_ustr;
        else if (title == u"review"_ustr)
            title = u"3. 审查"_ustr;
        else if (title == u"verify"_ustr)
            title = u"4. 校验"_ustr;
        else if (title == u"sandbox"_ustr)
            title = u"公式沙箱"_ustr;
        else
            title = OUString::number(static_cast<sal_Int32>(i + 2)) + u". "_ustr + title;

        OUString statusZh = LocalizeAgentStepStatus(st.status);
        if (!st.content.isEmpty() && st.status == u"ok"_ustr)
        {
            OUString preview = st.content;
            if (preview.getLength() > 48)
                preview = preview.copy(0, 48) + u"…"_ustr;
            preview = preview.replaceAll(u"\n"_ustr, u" "_ustr);
            statusZh = u"完成 · "_ustr + preview;
        }
        // Update existing "待继续" rows if present, else append.
        bool bUpdated = false;
        for (size_t r = 0; r < m_aAgentStepCache.size(); ++r)
        {
            if (m_aAgentStepCache[r].first == title
                && (m_aAgentStepCache[r].second.indexOf(u"待继续"_ustr) >= 0
                    || m_aAgentStepCache[r].second.indexOf(u"排队"_ustr) >= 0))
            {
                m_aAgentStepCache[r].second = statusZh;
                bUpdated = true;
                break;
            }
        }
        if (!bUpdated)
            PushAgentStepRow(title, statusZh);

        OUString body = st.content;
        if (body.getLength() > 500)
            body = body.copy(0, 500) + u"…"_ustr;
        AppendTranscript(u"Agent·"_ustr + title,
                         (body.isEmpty() ? (u"状态="_ustr + statusZh) : body),
                         /*bPersistHistory*/ false);
    }
    LoadAgentSteps();

    SetState(AIChatPanelState::Streaming);
    if (!pipe.combinedContent.isEmpty())
        AppendAssistantMarkdown(pipe.combinedContent);

    if (pipe.success)
    {
        UpdateAgentStepBar(4, u"完成 · 待批准写回"_ustr);
        SetAgentStepBar(
            u"Agent Mode · ✓绑定 → ✓规划 → ✓执行 → ✓审查 → ✓校验 · 待批准写回（主文档未改）"_ustr);
        m_sLastOutcomeDetail.clear();
        SetState(AIChatPanelState::AwaitingApproval);
        AppendTerminalEvidence(u"ok"_ustr, pipe.finalEvidenceId);
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(
                FormatEvidenceUserSummary(u"ok"_ustr, pipe.finalEvidenceId));
        AppendTranscript(
            u"System"_ustr,
            u"Agent Mode 执行完成 · 表面="_ustr + sSurface + u" · 证据="_ustr
                + pipe.finalEvidenceId + u" · 请「批准写回」或「拒绝」· 主文档未改"_ustr);
        const OUString sArtifactBody
            = !pipe.applyCandidateContent.isEmpty() ? pipe.applyCandidateContent
                                                    : pipe.combinedContent;
        RegisterAssistantArtifact(sArtifactBody, pipe.finalEvidenceId, u"agent"_ustr);
        StagePendingApplyPlan(pipe.applyCandidateContent, pipe.finalEvidenceId);
        if (m_xMainNotebook)
        {
            try
            {
                m_xMainNotebook->set_current_page(1);
            }
            catch (...)
            {
            }
        }
    }
    else
    {
        const OUString failDetail
            = ShortenUserDetail(pipe.failureReason.isEmpty()
                                    ? LocalizeProviderStatusZh(u"provider-error"_ustr)
                                    : pipe.failureReason);
        m_sLastOutcomeDetail = failDetail;
        SetAgentStepBar(u"步骤：执行失败 · 主文档未改 · "_ustr + failDetail);
        ClearPendingPlan();
        SetState(AIChatPanelState::Failed);
        AppendTerminalEvidence(u"provider-error"_ustr, pipe.finalEvidenceId);
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"失败 · 主文档未改 · "_ustr + failDetail);
        AppendTranscript(u"System"_ustr,
                         u"多步协作执行失败："_ustr + failDetail + u" · 主文档未改"_ustr);
        PresentModelHealthGuidance(
            /*bOpenConfigDir*/ failDetail.indexOf(u"401"_ustr) >= 0
                || failDetail.indexOf(u"认证"_ustr) >= 0
                || failDetail.indexOf(u"API Key"_ustr) >= 0,
            /*rFailDetail*/ failDetail);
        LoadReviewQueue();
    }
    FocusPrompt();
}

IMPL_LINK_NOARG(AIChatPanel, OnAgentApproveClicked, weld::Button&, void)
{
    if (!m_bHasPendingPlan)
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"暂无待批计划 · 请先运行任务并「继续」"_ustr);
        return;
    }
    ApplyPendingPlanWithApproval();
}

IMPL_LINK_NOARG(AIChatPanel, OnAgentStopClicked, weld::Button&, void)
{
    m_bCancelRequested = true;
    if (m_bAgentAwaitingContinue)
    {
        ClearAgentContinueGate();
        MarkAgentStepsStopped();
        SetAgentStepBar(u"步骤：已停止（计划未执行）"_ustr);
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"已停止 · 计划未执行 · 主文档未改"_ustr);
        AppendTranscript(u"System"_ustr,
                         u"用户停止 Agent Mode · 放弃已规划未执行的任务 · 主文档未改"_ustr,
                         /*bPersistHistory*/ false);
        return;
    }
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"已请求停止 · 等待当前步骤结束 · 主文档未改"_ustr);
    SetAgentStepBar(u"步骤：停止中…"_ustr);
    AppendTranscript(u"System"_ustr, u"用户请求停止 Agent Mode · 主文档未改"_ustr,
                     /*bPersistHistory*/ false);
}

IMPL_LINK_NOARG(AIChatPanel, OnAgentRefreshClicked, weld::Button&, void)
{
    LoadAgentSteps();
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(m_aAgentStepCache.empty() ? u"任务列表为空"_ustr
                                                            : u"已刷新步骤"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnAgentClearClicked, weld::Button&, void)
{
    ClearAgentContinueGate();
    m_aAgentStepCache.clear();
    LoadAgentSteps();
    SetAgentStepBar(u"步骤：待命"_ustr);
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"已清空任务列表"_ustr);
    AppendTranscript(u"System"_ustr, u"任务步骤列表已清空（不影响聊天记录）"_ustr,
                     /*bPersistHistory*/ false);
}

IMPL_LINK_NOARG(AIChatPanel, OnScheduleRefreshClicked, weld::Button&, void)
{
    LoadScheduleList();
    if (m_xStatusLabel)
    {
        const int n = m_xScheduleTree ? m_xScheduleTree->n_children() : 0;
        m_xStatusLabel->set_label(n <= 0 ? u"暂无定时任务"_ustr
                                         : u"已刷新定时任务（"_ustr + OUString::number(n)
                                               + u"）"_ustr);
    }
}

IMPL_LINK_NOARG(AIChatPanel, OnScheduleToggleClicked, weld::Button&, void)
{
    const OUString id = GetSelectedScheduleId();
    if (id.isEmpty())
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"请先选择一条定时任务"_ustr);
        return;
    }
    kqoffice::ai::cowork::ScheduledTaskStore store;
    kqoffice::ai::cowork::ScheduledTask t;
    if (!store.get(id, t))
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"定时任务不存在"_ustr);
        LoadScheduleList();
        return;
    }
    const bool next = !t.enabled;
    if (!store.setEnabled(id, next))
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"启用/禁用失败"_ustr);
        return;
    }
    LoadScheduleList();
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(next ? u"已启用定时任务"_ustr : u"已禁用定时任务"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnScheduleRunNowClicked, weld::Button&, void)
{
    const OUString id = GetSelectedScheduleId();
    if (id.isEmpty())
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"请先选择一条定时任务"_ustr);
        return;
    }
    const bool ok = DispatchScheduledTaskNow(id);
    LoadScheduleList();
    // Consume inject immediately if panel is open so prompt appears without waiting poll.
    if (ok)
        ConsumePendingPromptInject();
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(ok ? u"已立即触发（注入可圈 AI）"_ustr
                                     : u"立即执行失败"_ustr);
    AppendTranscript(u"System"_ustr,
                     ok ? u"定时任务已立即触发：提示已注入输入框"_ustr
                        : u"定时任务立即执行失败"_ustr,
                     /*bPersistHistory*/ false);
}

IMPL_LINK_NOARG(AIChatPanel, OnScheduleAddClicked, weld::Button&, void)
{
    OUString prompt;
    if (m_xPromptEntry)
        prompt = m_xPromptEntry->get_text().trim();
    // Prefer explicit prompt; fall back to active scenario id as promptOrScenarioId.
    if (prompt.isEmpty() && m_xScenarioPicker)
    {
        const OUString scenId = m_xScenarioPicker->get_active_id().trim();
        if (!scenId.isEmpty())
            prompt = scenId;
    }
    if (prompt.isEmpty())
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"请先在输入框填写内容，或选择一个方案"_ustr);
        FocusPrompt();
        return;
    }

    using kqoffice::ai::cowork::ScheduleKind;
    ScheduleKind kind = ScheduleKind::Once;
    sal_Int32 intervalMinutes = 30;
    sal_Int32 dailyHour = 9;
    sal_Int32 dailyMinute = 0;
    OUString errZh;
    if (!ReadScheduleEditorFields(kind, intervalMinutes, dailyHour, dailyMinute, errZh))
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(errZh);
        return;
    }

    const sal_Int64 nowMs = ScheduleNowMs();
    kqoffice::ai::cowork::ScheduledTask t;
    t.titleZh = prompt.copy(0, std::min<sal_Int32>(20, prompt.getLength()));
    t.promptOrScenarioId = prompt;
    t.kind = kind;
    t.intervalMinutes = intervalMinutes;
    t.dailyHour = dailyHour;
    t.dailyMinute = dailyMinute;
    t.enabled = true;
    t.lastResultZh = u"已创建，待执行"_ustr;
    switch (kind)
    {
        case ScheduleKind::Once:
            // Store delay in intervalMinutes so re-edit shows the same N minutes.
            t.intervalMinutes = intervalMinutes;
            t.nextRunAtMs = nowMs + static_cast<sal_Int64>(intervalMinutes) * 60 * 1000;
            break;
        case ScheduleKind::IntervalMinutes:
            t.nextRunAtMs = nowMs + static_cast<sal_Int64>(intervalMinutes) * 60 * 1000;
            break;
        case ScheduleKind::DailyAt:
            t.nextRunAtMs
                = kqoffice::ai::cowork::ScheduledTaskStore::computeNextRun(t, nowMs);
            break;
    }

    kqoffice::ai::cowork::ScheduledTaskStore store;
    if (!store.upsert(t))
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"创建定时任务失败"_ustr);
        return;
    }
    LoadScheduleList();
    const OUString planZh = FormatSchedulePlanLabel(t, nowMs);
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"已创建定时任务 · "_ustr + planZh);
    AppendTranscript(u"System"_ustr,
                     u"定时任务已创建："_ustr + t.titleZh + u" · "_ustr + planZh,
                     /*bPersistHistory*/ false);
}

IMPL_LINK_NOARG(AIChatPanel, OnScheduleRemoveClicked, weld::Button&, void)
{
    const OUString id = GetSelectedScheduleId();
    if (id.isEmpty())
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"请先选择一条定时任务"_ustr);
        return;
    }
    kqoffice::ai::cowork::ScheduledTaskStore store;
    if (!store.remove(id))
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"删除定时任务失败"_ustr);
        return;
    }
    LoadScheduleList();
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"已删除定时任务"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnScheduleSaveClicked, weld::Button&, void)
{
    const OUString id = GetSelectedScheduleId();
    if (id.isEmpty())
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"请先选择一条定时任务，再保存计划"_ustr);
        return;
    }

    using kqoffice::ai::cowork::ScheduleKind;
    ScheduleKind kind = ScheduleKind::Once;
    sal_Int32 intervalMinutes = 30;
    sal_Int32 dailyHour = 9;
    sal_Int32 dailyMinute = 0;
    OUString errZh;
    if (!ReadScheduleEditorFields(kind, intervalMinutes, dailyHour, dailyMinute, errZh))
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(errZh);
        return;
    }

    kqoffice::ai::cowork::ScheduledTaskStore store;
    kqoffice::ai::cowork::ScheduledTask t;
    if (!store.get(id, t))
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"定时任务不存在"_ustr);
        LoadScheduleList();
        return;
    }

    const sal_Int64 nowMs = ScheduleNowMs();
    t.kind = kind;
    t.intervalMinutes = intervalMinutes;
    t.dailyHour = dailyHour;
    t.dailyMinute = dailyMinute;
    // Recompute next fire from the new plan (force recompute by clearing next first for
    // Interval/Daily; Once keeps a short demo delay if already past).
    switch (kind)
    {
        case ScheduleKind::Once:
            t.intervalMinutes = intervalMinutes;
            t.nextRunAtMs = nowMs + static_cast<sal_Int64>(intervalMinutes) * 60 * 1000;
            break;
        case ScheduleKind::IntervalMinutes:
            t.nextRunAtMs = nowMs + static_cast<sal_Int64>(intervalMinutes) * 60 * 1000;
            break;
        case ScheduleKind::DailyAt:
            t.nextRunAtMs = 0; // force recompute from wall clock
            t.nextRunAtMs
                = kqoffice::ai::cowork::ScheduledTaskStore::computeNextRun(t, nowMs);
            break;
    }
    t.lastResultZh = u"已更新计划"_ustr;

    if (!store.upsert(t))
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"保存计划失败"_ustr);
        return;
    }
    LoadScheduleList();
    const OUString planZh = FormatSchedulePlanLabel(t, nowMs);
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"已保存计划 · "_ustr + planZh);
    AppendTranscript(u"System"_ustr,
                     u"定时任务计划已更新："_ustr + t.titleZh + u" · "_ustr + planZh,
                     /*bPersistHistory*/ false);
}

IMPL_LINK_NOARG(AIChatPanel, OnScheduleTreeSelectionChanged, weld::TreeView&, void)
{
    FillScheduleEditorFromSelected();
}

IMPL_LINK_NOARG(AIChatPanel, OnScheduleAutoSendToggled, weld::Toggleable&, void)
{
    if (!m_xScheduleAutoSend)
        return;
    auto prefs = kqoffice::ai::chat::DocumentAIInputPrefs::load();
    prefs.scheduleAutoSend = m_xScheduleAutoSend->get_active();
    if (!kqoffice::ai::chat::DocumentAIInputPrefs::save(prefs))
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"自动发送偏好写入失败"_ustr);
        return;
    }
    if (m_xStatusLabel)
    {
        m_xStatusLabel->set_label(prefs.scheduleAutoSend
                                      ? u"到期后将自动发送（仍可在输入框修改）"_ustr
                                      : u"到期后仅注入输入框，需手动发送"_ustr);
    }
}


IMPL_LINK_NOARG(AIChatPanel, OnBatchRefreshClicked, weld::Button&, void)
{
    LoadBatchJobList();
    if (m_xStatusLabel)
    {
        const int n = m_xBatchTree ? m_xBatchTree->n_children() : 0;
        m_xStatusLabel->set_label(n <= 0 ? u"暂无批量任务记录"_ustr
                                         : u"已刷新批量任务（"_ustr + OUString::number(n)
                                               + u"）"_ustr);
    }
}

IMPL_LINK(AIChatPanel, OnMainNotebookEnterPage, const OUString&, rPage, void)
{
    // Reload schedule + batch ledgers when user opens the 任务 tab.
    if (rPage == u"agent_tab"_ustr)
    {
        LoadAgentSteps();
        LoadScheduleList();
        LoadBatchJobList();
    }
}

IMPL_LINK_NOARG(AIChatPanel, OnReviewRefreshClicked, weld::Button&, void)
{
    LoadReviewQueue();
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"已刷新审核队列"_ustr);
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

IMPL_LINK_NOARG(AIChatPanel, OnOpenKqNotebookClicked, weld::Button&, void)
{
    sfx2::KqNotebookDispatcher::Get().Show(GetFrameWeld());
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

IMPL_LINK_NOARG(AIChatPanel, OnScheduleTick, Timer*, void)
{
    // Ledger → pending-prompt-inject. Non-throwing; pure file I/O + markRun.
    try
    {
        kqoffice::ai::cowork::processDueScheduledTasks();
    }
    catch (...)
    {
    }
    // M12: bounded material-path poll (60s schedule tick ≈ policy poll interval).
    // Debounce 5s inside PollWatchedPaths; no per-file FD; fail-closed visible via status.
    try
    {
        AIChatKnowledgeFtsEngine::PollWatchedPaths(OUString(), /*bForce*/ false);
    }
    catch (...)
    {
    }
    // Consume immediately if inject was written this tick.
    ConsumePendingPromptInject();
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

    // Membership slash inject (api.03122.com only; never mutates main doc) — always auto-send.
    const bool bMembershipSlash
        = text.startsWith(u"/quota"_ustr) || text.startsWith(u"/会员额度"_ustr)
          || text == u"/额度"_ustr || text.startsWith(u"/checkin"_ustr)
          || text.startsWith(u"/签到"_ustr) || text.startsWith(u"/rush"_ustr)
          || text.startsWith(u"/抢包"_ustr) || text.startsWith(u"/加油包"_ustr);

    OUString source = u"inject"_ustr;
    if (bMembershipSlash)
        source = u"membership"_ustr;
    else if (text.indexOf(u"【可圈定时任务】"_ustr) >= 0)
        source = u"schedule"_ustr;
    else if (text.indexOf(u"【可圈工作中台"_ustr) >= 0)
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

    // Auto-send: membership slash always; schedule only when prefs allow (default off).
    bool bAutoSend = bMembershipSlash;
    if (source == u"schedule"_ustr)
    {
        const auto prefs = kqoffice::ai::chat::DocumentAIInputPrefs::load();
        bAutoSend = prefs.scheduleAutoSend;
        // UI checkbox is authoritative if present (user may toggle without reload).
        if (m_xScheduleAutoSend)
            bAutoSend = m_xScheduleAutoSend->get_active();
    }

    if (m_xStatusLabel)
    {
        if (source == u"membership"_ustr)
        {
            m_xStatusLabel->set_label(
                bAutoSend ? u"会员指令已注入并自动发送 · 主文档未改"_ustr
                          : u"会员指令已注入 — 可发送"_ustr);
        }
        else if (source == u"schedule"_ustr)
        {
            m_xStatusLabel->set_label(
                bAutoSend ? u"定时任务已注入并自动发送"_ustr
                          : u"定时任务已注入输入框 — 可编辑后发送"_ustr);
        }
        else if (source == u"workbench"_ustr)
            m_xStatusLabel->set_label(u"已附上工作中台本地上下文 — 可编辑后发送"_ustr);
        else if (source == u"notebook"_ustr)
            m_xStatusLabel->set_label(u"已附上记事本本地上下文 — 可编辑后发送"_ustr);
        else
            m_xStatusLabel->set_label(u"已附上截图/语音内容 — 可编辑后发送"_ustr);
    }

    if (bAutoSend)
    {
        AppendTranscript(
            u"System"_ustr,
            bMembershipSlash ? u"membership-auto-send=1 · 主文档未改 · api.03122.com"_ustr
                             : u"schedule-auto-send=1 · 主文档写回仍须批准"_ustr,
            /*bPersistHistory*/ false);
        SubmitPrompt();
    }
}

void AIChatPanel::RunScreenshotMode(const OUString& rMode)
{
    const auto mode = kqoffice::ai::chat::DocumentAIInputPrefs::screenshotModeFromString(rMode);
    const kqoffice::ai::chat::ScreenCaptureResult shot
        = kqoffice::ai::chat::DocumentAIScreenCapture::capture(mode);
    AppendTranscript(u"System"_ustr,
                     u"screenshot mode="_ustr + shot.mode + u" success="_ustr
                         + (shot.success ? u"1"_ustr : u"0"_ustr) + u" path="_ustr + shot.path
                         + u" cloud=0 local-only=1"_ustr,
                     /*bPersistHistory*/ false);
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(shot.message);
    if (shot.success)
    {
        kqoffice::ai::workbench::WorkTelemetryStore::recordSimple(u"screenshot"_ustr, 1);
        const auto prefs = kqoffice::ai::chat::DocumentAIInputPrefs::load();
        if (prefs.screenshotAutoAttachChat && !shot.promptAttachment.isEmpty())
        {
            AppendPromptText(shot.promptAttachment + u"\n请结合截图说明："_ustr);
            if (m_xStatusLabel)
                m_xStatusLabel->set_label(
                    u"截图已附上 · 可编辑后发送（本地文件，未上传）·「内容」可预览"_ustr);
        }
        if (!shot.path.isEmpty())
            RegisterLocalFileArtifact(shot.path, u"screenshot"_ustr);
        FocusPrompt();
    }
}

void AIChatPanel::AttachLocalFilePaths(const std::vector<OUString>& rPaths)
{
    sal_Int32 nAttached = 0;
    for (OUString path : rPaths)
    {
        path = path.trim();
        if (path.isEmpty())
            continue;
        // Normalize file:// URL → system path for stable @文件: tokens.
        if (path.startsWith(u"file:"_ustr))
        {
            OUString sys;
            if (osl::FileBase::getSystemPathFromFileURL(path, sys) == osl::FileBase::E_None)
                path = sys;
        }
        if (path.isEmpty())
            continue;
        if (kqoffice::ai::chat::DocumentAIMaterialReader::detectKind(path)
            == kqoffice::ai::chat::MaterialKind::Folder)
            AppendPromptText(u"@文件夹:"_ustr + path + u"\n请基于该文件夹中的本地材料："_ustr);
        else
            AppendPromptText(u"@文件:"_ustr + path + u"\n请基于该本地文件："_ustr);
        AppendTranscript(u"System"_ustr,
                         u"file-attached path="_ustr + path
                             + u" open-document=false cloud=0 awaiting-user-send=true"_ustr,
                         /*bPersistHistory*/ false);
        RegisterLocalFileArtifact(path, u"attach"_ustr);
        ++nAttached;
    }
    if (nAttached <= 0)
        return;
    if (m_xStatusLabel)
    {
        m_xStatusLabel->set_label(nAttached == 1
                                      ? u"已附加文件 · 可编辑后发送 ·「内容」可预览"_ustr
                                      : u"已附加 "_ustr + OUString::number(nAttached)
                                            + u" 个文件 · 可编辑后发送 ·「内容」可预览"_ustr);
    }
    FocusPrompt();
}

void AIChatPanel::LoadBatchJobList()
{
    if (!m_xBatchTree)
        return;
    m_xBatchTree->clear();
    const auto rows = kqoffice::ai::filemgr::BatchJobManager::listRecentLedgers(20);
    for (const auto& row : rows)
    {
        const OUString title
            = row.kindZh.isEmpty() ? (row.id.isEmpty() ? u"批量"_ustr : row.id) : row.kindZh;
        m_xBatchTree->append(row.id.isEmpty() ? row.ledgerPath : row.id, title);
        const int nRow = m_xBatchTree->n_children() - 1;
        if (nRow < 0)
            continue;
        m_xBatchTree->set_text(nRow, row.stateZh.isEmpty() ? u"—"_ustr : row.stateZh, 1);
        OUString result = row.reasonZh;
        if (result.isEmpty())
            result = row.id;
        m_xBatchTree->set_text(nRow, result.isEmpty() ? u"—"_ustr : result, 2);
    }
    if (!rows.empty())
        m_xBatchTree->select(0);
}

IMPL_LINK_NOARG(AIChatPanel, OnAttachFileClicked, weld::Button&, void)
{
    try
    {
        sfx2::FileDialogHelper aDlg(css::ui::dialogs::TemplateDescription::FILEOPEN_SIMPLE,
                                    FileDialogFlags::NONE, GetFrameWeld());
        aDlg.SetTitle(u"附加本地文件到可圈 AI"_ustr);
        aDlg.AddFilter(u"办公文档"_ustr,
                       u"*.odt;*.ods;*.odp;*.docx;*.xlsx;*.pptx;*.doc;*.xls;*.ppt;*.rtf;*.pdf"_ustr);
        aDlg.AddFilter(u"文本"_ustr, u"*.txt;*.md;*.markdown;*.csv;*.tsv;*.json;*.log;*.xml"_ustr);
        aDlg.AddFilter(u"图片"_ustr, u"*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp"_ustr);
        aDlg.AddFilter(u"所有文件"_ustr, u"*.*"_ustr);
        if (aDlg.Execute() != ERRCODE_NONE)
            return;
        OUString path = aDlg.GetPath();
        if (path.isEmpty())
            return;
        AttachLocalFilePaths({ path });
    }
    catch (...)
    {
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"附加文件失败"_ustr);
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
    if (!IsRunBusy() && m_eState != AIChatPanelState::Requesting
        && m_eState != AIChatPanelState::Streaming)
        return;

    DispatchWorkspaceAction(u"cancel"_ustr);
    // Cancel stream + cowork TaskRunner (if any); agent tree → 已停止.
    StopActiveRun(u"已停止"_ustr, /*bFromAppend*/ false);
    FocusPrompt();
}

IMPL_LINK_NOARG(AIChatPanel, OnRetryClicked, weld::Button&, void)
{
    if (m_sLastPrompt.isEmpty())
        return;

    // M20: one-click regenerate after stale reject (auto-submit with current doc context).
    if (m_bStaleNeedsRegen)
    {
        DispatchWorkspaceAction(u"stale-regenerate"_ustr);
        RegenerateAfterStale();
        return;
    }

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

// C ABI for DiffReview (svx) — pending 批准写回 / 拒绝 call into the active AI chat
// panel so Diff preview and sidebar share one apply path (no silent main-doc write).
extern "C" SAL_DLLPUBLIC_EXPORT sal_Bool kqoffice_diff_review_approve_pending(
    const sal_Unicode* pPlanId, sal_Int32 nPlanIdLen)
{
    using sfx2::sidebar::AIChatPanel;
    AIChatPanel* pPanel = AIChatPanel::GetActivePanel();
    if (!pPanel || !pPanel->HasPendingApplyPlan())
        return sal_False;

    if (pPlanId && nPlanIdLen > 0)
    {
        const OUString aPlanId(pPlanId, nPlanIdLen);
        if (!aPlanId.isEmpty() && aPlanId != pPanel->GetPendingPlanId())
            return sal_False;
    }

    // Same path as sidebar / chat「批准写回」— permission clarify + DocumentAIApply + undo.
    return pPanel->ApplyPendingPlanWithApproval() ? sal_True : sal_False;
}

extern "C" SAL_DLLPUBLIC_EXPORT sal_Bool kqoffice_diff_review_reject_pending(
    const sal_Unicode* pPlanId, sal_Int32 nPlanIdLen)
{
    using sfx2::sidebar::AIChatPanel;
    AIChatPanel* pPanel = AIChatPanel::GetActivePanel();
    if (!pPanel || !pPanel->HasPendingApplyPlan())
        return sal_False;

    if (pPlanId && nPlanIdLen > 0)
    {
        const OUString aPlanId(pPlanId, nPlanIdLen);
        if (!aPlanId.isEmpty() && aPlanId != pPanel->GetPendingPlanId())
            return sal_False;
    }

    pPanel->RejectPendingPlan();
    return sal_True;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
