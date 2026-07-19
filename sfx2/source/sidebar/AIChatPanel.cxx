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
#include <ScheduledTask.hxx>
#include <TaskRunner.hxx>
#include <ScheduledTaskDispatcher.hxx>
#include <DocumentAIApply.hxx>
#include <DocumentAIContext.hxx>
#include <DocumentAILocalRag.hxx>
#include <DocumentAIScenarioStore.hxx>
#include <DocumentAIScenarios.hxx>
#include <DocumentAIVoiceInput.hxx>
#include <DocumentAIInputPrefs.hxx>
#include <DocumentAIMaterialReader.hxx>
#include <DocumentAIScreenCapture.hxx>
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
#include <ModelRoutingConfig.hxx>

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
    if (rIntentId == u"shorten"_ustr)
        return u"精简"_ustr;
    if (rIntentId == u"expand"_ustr)
        return u"扩写"_ustr;
    if (rIntentId == u"summarize"_ustr)
        return u"总结"_ustr;
    if (rIntentId == u"plan"_ustr)
        return u"规划"_ustr;
    if (rIntentId == u"agent"_ustr)
        return u"多步协作"_ustr;
    if (rIntentId == u"review"_ustr)
        return u"审查"_ustr;
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

AIChatPanel::AIChatPanel(weld::Widget* pParent)
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
    , m_xIntentShortenBtn(m_xBuilder->weld_button(u"intent_shorten_btn"_ustr))
    , m_xIntentExpandBtn(m_xBuilder->weld_button(u"intent_expand_btn"_ustr))
    , m_xIntentSummarizeBtn(m_xBuilder->weld_button(u"intent_summarize_btn"_ustr))
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
    , m_xPendingPlanChip(m_xBuilder->weld_button(u"pending_plan_chip"_ustr))
    , m_xApprovalActionRow(m_xBuilder->weld_widget(u"approval_action_row"_ustr))
    , m_xApprovalHintLabel(m_xBuilder->weld_label(u"approval_hint_label"_ustr))
    , m_xChatApproveBtn(m_xBuilder->weld_button(u"chat_approve_btn"_ustr))
    , m_xChatDiffBtn(m_xBuilder->weld_button(u"chat_diff_btn"_ustr))
    , m_xChatRejectBtn(m_xBuilder->weld_button(u"chat_reject_btn"_ustr))
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
    m_xPromptEntry->set_placeholder_text(u"描述你要做的事，或点意图芯片 / 上方方案…"_ustr);
    m_xPromptEntry->connect_insert_text(LINK(this, AIChatPanel, OnPromptInsertText));

    m_xPromptEntry->connect_changed(LINK(this, AIChatPanel, OnPromptChanged));
    m_xPromptEntry->connect_activate(LINK(this, AIChatPanel, OnPromptActivated));
    if (m_xIntentRewriteBtn)
        m_xIntentRewriteBtn->connect_clicked(LINK(this, AIChatPanel, OnIntentRewriteClicked));
    if (m_xIntentShortenBtn)
        m_xIntentShortenBtn->connect_clicked(LINK(this, AIChatPanel, OnIntentShortenClicked));
    if (m_xIntentExpandBtn)
        m_xIntentExpandBtn->connect_clicked(LINK(this, AIChatPanel, OnIntentExpandClicked));
    if (m_xIntentSummarizeBtn)
        m_xIntentSummarizeBtn->connect_clicked(LINK(this, AIChatPanel, OnIntentSummarizeClicked));
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
    if (m_xAgentRunBtn)
        m_xAgentRunBtn->connect_clicked(LINK(this, AIChatPanel, OnAgentRunClicked));
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
    if (m_xPendingPlanChip)
        m_xPendingPlanChip->connect_clicked(LINK(this, AIChatPanel, OnPendingPlanChipClicked));
    if (m_xChatApproveBtn)
        m_xChatApproveBtn->connect_clicked(LINK(this, AIChatPanel, OnChatApproveClicked));
    if (m_xChatDiffBtn)
        m_xChatDiffBtn->connect_clicked(LINK(this, AIChatPanel, OnChatDiffClicked));
    if (m_xChatRejectBtn)
        m_xChatRejectBtn->connect_clicked(LINK(this, AIChatPanel, OnChatRejectClicked));
    if (m_xLocateRagBtn)
        m_xLocateRagBtn->connect_clicked(LINK(this, AIChatPanel, OnLocateRagClicked));
    if (m_xRoutingDiagBtn)
        m_xRoutingDiagBtn->connect_clicked(LINK(this, AIChatPanel, OnRoutingDiagClicked));

    // Critical path only: chat history + chrome. Workspace trees + Ollama probe
    // are deferred so opening 可圈 AI does not stall first paint (cold-open).
    LoadDocumentHistory();
    ReloadScenarioPicker();
    UpdateSelectionChip();
    UpdatePendingPlanChip();
    UpdateApprovalChrome();
    if (m_xRoutingDiagLabel)
        m_xRoutingDiagLabel->set_label(u"路由：探测中…"_ustr);
    ConsumePendingScenarioRun();
    ConsumePendingPromptInject();
    // Keep consuming injects while panel is alive (速览→AI / 记事本→AI when already open).
    // 1.2s is enough for handoff injects without burning main-thread timers.
    m_aInjectPoll.SetTimeout(1200);
    m_aInjectPoll.SetInvokeHandler(LINK(this, AIChatPanel, OnInjectPollTick));
    m_aInjectPoll.Start();
    // Warm workspace + routing after first frame (not on Start Center cold start).
    m_aDeferredWarmup.SetTimeout(900);
    m_aDeferredWarmup.SetInvokeHandler(LINK(this, AIChatPanel, OnDeferredWarmupTick));
    m_aDeferredWarmup.Start();
    // Local scheduled tasks: scan due ledger every 60s while AI panel is open.
    // Dispatch writes pending-prompt-inject; m_aInjectPoll consumes it into the prompt.
    m_aScheduleTick.SetTimeout(60'000);
    m_aScheduleTick.SetInvokeHandler(LINK(this, AIChatPanel, OnScheduleTick));
    m_aScheduleTick.Start();
    // One immediate due scan so tasks already past nextRunAt fire without waiting 60s.
    kqoffice::ai::cowork::processDueScheduledTasks();
    // Drag files onto prompt entry → @文件: attach (does not open document).
    if (m_xPromptEntry)
    {
        class AIChatAttachDropHelper final : public DropTargetHelper
        {
            AIChatPanel& m_rPanel;

        public:
            AIChatAttachDropHelper(
                AIChatPanel& rPanel,
                const css::uno::Reference<css::datatransfer::dnd::XDropTarget>& xDrop)
                : DropTargetHelper(xDrop)
                , m_rPanel(rPanel)
            {
            }

            sal_Int8 AcceptDrop(const AcceptDropEvent& /*rEvt*/) override
            {
                if (IsDropFormatSupported(SotClipboardFormatId::FILE_LIST)
                    || IsDropFormatSupported(SotClipboardFormatId::SIMPLE_FILE))
                    return DND_ACTION_COPY;
                return DND_ACTION_NONE;
            }

            sal_Int8 ExecuteDrop(const ExecuteDropEvent& rEvt) override
            {
                TransferableDataHelper aHelper(rEvt.maDropEvent.Transferable);
                std::vector<OUString> paths;
                FileList aFileList;
                if (aHelper.GetFileList(SotClipboardFormatId::FILE_LIST, aFileList))
                {
                    const sal_uInt32 nCount = aFileList.Count();
                    for (sal_uInt32 i = 0; i < nCount; ++i)
                        paths.push_back(aFileList.GetFile(i));
                }
                else
                {
                    OUString path;
                    if (aHelper.GetString(SotClipboardFormatId::SIMPLE_FILE, path)
                        && !path.isEmpty())
                        paths.push_back(path);
                }
                if (paths.empty())
                    return DND_ACTION_NONE;
                m_rPanel.AttachLocalFilePaths(paths);
                return DND_ACTION_COPY;
            }
        };
        m_xAttachDropHelper = std::make_unique<AIChatAttachDropHelper>(
            *this, m_xPromptEntry->get_drop_target());
    }
    SetState(AIChatPanelState::Idle);
    UpdateActions();
    FocusPrompt();
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
    EnsureWorkspaceDataLoaded();
    if (!m_bRoutingDiagDone)
    {
        m_bRoutingDiagDone = true;
        // Ollama probe can take hundreds of ms when offline — keep off open path.
        RunRoutingDiagnostics(/*bAppendTranscript*/ false);
    }
}

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
                         u"Markdown 已拒绝渲染："_ustr + aRendered.RejectionReason
                             + u" · 主文档未改"_ustr);
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

OUString AIChatPanel::LocalizeProviderStatusZh(const OUString& rStatus)
{
    if (rStatus.isEmpty() || rStatus == u"ok"_ustr || rStatus == u"success"_ustr
        || rStatus == u"completed"_ustr)
        return u"成功"_ustr;
    if (rStatus == u"cancelled"_ustr || rStatus == u"canceled"_ustr || rStatus == u"stopped"_ustr)
        return u"已停止"_ustr;
    if (rStatus == u"timeout"_ustr || rStatus == u"provider-timeout"_ustr)
        return u"模型超时"_ustr;
    if (rStatus == u"offline"_ustr || rStatus == u"provider-offline"_ustr)
        return u"模型离线"_ustr;
    if (rStatus == u"policy-denied"_ustr)
        return u"策略拒绝"_ustr;
    if (rStatus == u"provider-error"_ustr || rStatus == u"error"_ustr || rStatus == u"failed"_ustr)
        return u"模型调用失败"_ustr;
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
    if (!m_xHistoryStore)
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
    m_xTranscriptView->set_text(OUString());
    AppendTranscript(u"System"_ustr,
                     bCleared ? u"history-cleared for current document · 已清空本机对话记录"_ustr
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
    rInsertedText = aContent.Reference;

    const OUString sMessage = u"materialized-content reference="_ustr + aContent.Reference
                              + u" type="_ustr
                              + AIChatContentObjectStore::DetectTypeLabel(aContent.Type);
    AppendTranscript(u"System"_ustr, sMessage);
    m_xStatusLabel->set_label(u"已登记生成内容："_ustr + aContent.Reference);
    LoadArtifactNavigator();
    RecordWorkspaceActivity(u"artifact-created"_ustr, u"artifacts"_ustr, aContent.ObjectId,
                            u"evidence:local-materialized:"_ustr + aContent.ObjectId,
                            aContent.Reference, u"sidebar-preview"_ustr);
    SaveSessionSnapshot(aContent.ObjectId, u"evidence:local-materialized:"_ustr + aContent.ObjectId,
                        u"metadata-summary"_ustr, OUString(), aContent.Reference);
    return true;
}

OUString AIChatPanel::LocalizeArtifactType(const OUString& rType)
{
    if (rType == u"assistant-output"_ustr)
        return u"助手输出"_ustr;
    if (rType == u"review-item"_ustr)
        return u"审查项"_ustr;
    if (rType == u"formatting-preview"_ustr)
        return u"排版预览"_ustr;
    if (rType == u"apply-plan"_ustr)
        return u"写回计划"_ustr;
    if (rType == u"selection"_ustr)
        return u"选区"_ustr;
    if (rType == u"document-section"_ustr)
        return u"文档片段"_ustr;
    if (rType == u"task-step"_ustr)
        return u"任务步骤"_ustr;
    if (rType == u"evidence-record"_ustr)
        return u"证据记录"_ustr;
    if (rType == u"local-file"_ustr)
        return u"本地文件"_ustr;
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
        = rEntry.EvidenceId.isEmpty() ? u"无证据"_ustr : u"有证据"_ustr;

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
        if (AIChatContentOpener::LoadTextPreviewBody(rEntry, aPreview, sBody, sDetail)
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
            m_xStatusLabel->set_label(u"打开失败：未选择生成内容"_ustr);
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
        if (!aResult.PreviewBody.isEmpty())
        {
            // Full body from open path (may exceed selection-teaser length).
            const sal_Int32 nMarker = sDetails.indexOf(u"—— 预览正文 ——"_ustr);
            if (nMarker >= 0)
                sDetails = sDetails.copy(0, nMarker);
            sDetails += u"\n\n—— 预览正文 ——\n"_ustr + aResult.PreviewBody;
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
    m_xPromptEntry->grab_focus();
    m_xPromptEntry->set_position(-1);
}

void AIChatPanel::UpdateActions()
{
    const bool bHasPrompt = !m_xPromptEntry->get_text().trim().isEmpty();
    const bool bBusy = IsRunBusy();
    // DuMate: keep composer editable while running so user can append/replace task.
    m_xPromptEntry->set_sensitive(true);
    m_xSendButton->set_sensitive(bHasPrompt);
    m_xCancelButton->set_sensitive(bBusy);
    m_xRetryButton->set_sensitive(!m_sLastPrompt.isEmpty() && !bBusy);
    m_xClearHistoryButton->set_sensitive(!bBusy);
    // Intent chips stay clickable when idle/awaiting; disabled while busy.
    auto setIntent = [&](weld::Button* p) {
        if (p)
            p->set_sensitive(!bBusy);
    };
    setIntent(m_xIntentRewriteBtn.get());
    setIntent(m_xIntentShortenBtn.get());
    setIntent(m_xIntentExpandBtn.get());
    setIntent(m_xIntentSummarizeBtn.get());
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
            else
                card = u"活动：待命 · 意图「"_ustr + intentZh
                       + u"」· 选中文字后点芯片，或直接输入"_ustr;
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
            if (!m_sLastOutcomeDetail.isEmpty())
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
        if (m_xIntentShortenBtn)
            m_xIntentShortenBtn->set_label(u"清洗"_ustr);
        if (m_xIntentExpandBtn)
            m_xIntentExpandBtn->set_label(u"汇总"_ustr);
        if (m_xIntentSummarizeBtn)
            m_xIntentSummarizeBtn->set_label(u"解读"_ustr);
        if (m_xIntentPlanBtn)
            m_xIntentPlanBtn->set_label(u"规划"_ustr);
        if (m_xIntentAgentBtn)
            m_xIntentAgentBtn->set_label(u"多步"_ustr);
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
    if (m_xPromptEntry)
    {
        if (m_xPromptEntry->get_text().trim().isEmpty())
            m_xPromptEntry->set_text(rSeedPrompt);
        FocusPrompt();
    }
    UpdateComposerChrome();
    UpdateActivityCard();
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(u"意图："_ustr + IntentIdToZh(rIntentId)
                                  + u" · 主文档不会自动改"_ustr);
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
                u"AI 服务不可用，请到「工具 → 选项 → 可圈 AI」检查模型配置"_ustr);

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
            }
        }

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
                AppendTranscript(u"System"_ustr,
                                 u"local-rag · 本地检索：已附带本文档相关片段 · 无外传"_ustr);
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
                             u"路由 · 能力="_ustr + cap + u" · 槽="_ustr + resolved.slotName
                                 + u" · 模型="_ustr
                                 + (resolved.model.isEmpty() ? u"未配置"_ustr : resolved.model)
                                 + u" · 角色="_ustr + resolved.roleName);
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
        m_xPendingPlanChip->set_tooltip_text(u"暂无待批准写回计划"_ustr);
        m_xPendingPlanChip->set_sensitive(false);
        return;
    }
    OUStringBuffer b;
    if (m_aPendingPlan.planId == u"ap-chart-insert"_ustr)
        b.append(u"计划：待批图表 · 点此去审核 "_ustr);
    else
        b.append(u"计划：待批 · 点此去审核 "_ustr);
    b.append(static_cast<sal_Int32>(m_aPendingPlan.operations.size()));
    b.append(u" 步"_ustr);
    if (!m_aPendingPlan.planId.isEmpty())
    {
        b.append(u" · "_ustr);
        b.append(m_aPendingPlan.planId);
    }
    m_xPendingPlanChip->set_label(b.makeStringAndClear());
    m_xPendingPlanChip->set_sensitive(true);
    if (m_aPendingPlan.planId == u"ap-chart-insert"_ustr)
        m_xPendingPlanChip->set_tooltip_text(
            u"点击跳到「审核」页。批准后打开「插入图表」向导（需显式批准）"_ustr);
    else
        m_xPendingPlanChip->set_tooltip_text(
            u"点击跳到「审核」页 · 可用「批准写回 / 查看 Diff / 拒绝」"_ustr);
}

void AIChatPanel::UpdateApprovalChrome()
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
                u"写回需你批准 · 主文档尚未修改 · 建议先 Diff 再批准"_ustr);
        else
            m_xApprovalHintLabel->set_label(
                u"写回需你批准 · 生成建议后此处会出现操作按钮"_ustr);
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

    // Reinforce review-tab primary actions when a plan is staged.
    if (m_xApproveSelectedButton && bPending)
        m_xApproveSelectedButton->set_label(u"批准写回"_ustr);
    if (m_xOpenDiffReviewButton)
        m_xOpenDiffReviewButton->set_label(u"查看 Diff"_ustr);
    if (m_xRejectSelectedButton && bPending)
        m_xRejectSelectedButton->set_label(u"拒绝"_ustr);

    UpdatePendingPlanChip();
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

IMPL_LINK_NOARG(AIChatPanel, OnSelectionChipClicked, weld::Button&, void)
{
    UpdateSelectionChip();
    ReloadScenarioPicker();
    m_xStatusLabel->set_label(u"已刷新选区芯片"_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnPendingPlanChipClicked, weld::Button&, void)
{
    // Single audit chain: 去审核 → 审核 Tab + 同一 Diff 预览路径（与「查看 Diff」一致）。
    if (m_bHasPendingPlan)
    {
        ShowReviewTab();
        TryShowDiffReviewAfterApply(m_aPendingPlan.planId, u"pending-preview"_ustr, false);
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(
                u"已打开「审核」与 Diff 预览 · 主文档尚未修改 · 请点「批准写回」或「拒绝」"_ustr);
        AppendTranscript(
            u"System"_ustr,
            u"diff-review-opened source=pending-plan-chip plan="_ustr + m_aPendingPlan.planId
                + u" applied=false main-document-mutation=false"_ustr,
            /*bPersistHistory*/ false);
    }
    else if (m_xStatusLabel)
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
    ShowReviewTab();
    if (m_bHasPendingPlan)
    {
        TryShowDiffReviewAfterApply(m_aPendingPlan.planId, u"pending-preview"_ustr, false);
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(
                u"已打开 Diff 预览 · 主文档尚未修改 · 确认后点「批准写回」"_ustr);
        AppendTranscript(
            u"System"_ustr,
            u"diff-review-opened source=chat-diff plan="_ustr + m_aPendingPlan.planId
                + u" applied=false main-document-mutation=false"_ustr,
            /*bPersistHistory*/ false);
        return;
    }
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
        if (!d.ollamaReachable)
            shortLabel = u"路由：网关离线 · 本地技能 "_ustr + OUString::number(nSkills);
        else
        {
            // Compact five-slot health chip (primary/light/agent/plan/review).
            auto slot = [](const OUString& m) {
                return m.isEmpty() ? u"?"_ustr : m;
            };
            shortLabel = u"五槽 主="_ustr + slot(d.primaryResolved) + u" 轻="_ustr
                         + slot(d.lightResolved) + u" Ag="_ustr + slot(d.agentResolved)
                         + u" 规="_ustr + slot(d.planResolved) + u" 审="_ustr
                         + slot(d.reviewResolved) + u" · 技能"_ustr
                         + OUString::number(nSkills);
        }
        m_xRoutingDiagLabel->set_label(shortLabel);
        OUString tip = d.summaryZh + u"\n本地技能（方案，非云市场）共 "_ustr
                       + OUString::number(nSkills) + u" · 常用 "_ustr
                       + OUString::number(nPinned);
        if (!skillsList.isEmpty())
            tip += u"\n"_ustr + skillsList;
        tip += u"\n管理：工具 → 选项 → 可圈 AI → AI 方案"_ustr;
        m_xRoutingDiagLabel->set_tooltip_text(tip);
    }
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(d.summaryZh.replaceAll(u"\n"_ustr, u" · "_ustr)
                                  + u" · 本地技能 "_ustr + OUString::number(nSkills));

    // Audit trail: write evidence JSON (local-first, never throws).
    kqoffice::ai::EvidenceRecord rec;
    rec.serviceMode = u"offline"_ustr;
    rec.provider = u"routing-diag light="_ustr
                   + (d.lightResolved.isEmpty() ? u"?"_ustr : d.lightResolved)
                   + u" review="_ustr
                   + (d.reviewResolved.isEmpty() ? u"?"_ustr : d.reviewResolved)
                   + u" primary="_ustr
                   + (d.primaryResolved.isEmpty() ? u"?"_ustr : d.primaryResolved)
                   + u" skills="_ustr + OUString::number(nSkills);
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
        msg += u"\n本地技能（方案）共 "_ustr + OUString::number(nSkills) + u" 个 · 常用 "_ustr
               + OUString::number(nPinned);
        if (!skillsList.isEmpty())
            msg += u"\n技能清单："_ustr + skillsList;
        msg += u"\n说明：本地方案 = Skills 可见面（非云端技能市场）；管理在「选项 → 可圈 AI」"_ustr;
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
            // Prefer plan capability for this run (cleared after CallProvider use).
            m_sForcedCapability = u"plan"_ustr;
            if (m_xOptAgentPipeline)
                m_xOptAgentPipeline->set_active(true);
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
    // Multi-step agent path: Plan → Act → Review (five-slot Provider routing).
    // Triggered by checkbox「多步 Agent」or agent/cowork keywords.
    const OUString lowerPrompt = sPrompt.toAsciiLowerCase();
    const bool bAgentOpt = m_xOptAgentPipeline && m_xOptAgentPipeline->get_active();
    const bool bAgentPipeline
        = bAgentOpt || lowerPrompt.indexOf(u"子代理"_ustr) >= 0
          || lowerPrompt.indexOf(u"协作"_ustr) >= 0 || lowerPrompt.indexOf(u"agent"_ustr) >= 0
          || lowerPrompt.startsWith(u"/agent"_ustr) || lowerPrompt.indexOf(u"cowork"_ustr) >= 0
          || lowerPrompt.indexOf(u"多步"_ustr) >= 0 || lowerPrompt.indexOf(u"plan-act"_ustr) >= 0;

    // Complex-task start confirm (not normal rewrite/summarize/chat):
    // agent pipeline, forced/scenario capability agent|plan (e.g. design-apply), or agent intent.
    {
        const OUString taskCap = !m_sForcedCapability.isEmpty()
                                     ? m_sForcedCapability
                                     : DetectComposerIntent(sPrompt);
        const bool bComplexTaskStart = bAgentPipeline || taskCap == u"agent"_ustr
                                       || taskCap == u"plan"_ustr;
        if (!ConfirmComplexAiTaskStart(bComplexTaskStart))
            return;
    }

    m_sLastPrompt = sPrompt;
    m_sStreamingBuffer.clear();
    SetState(AIChatPanelState::Requesting);
    AppendTranscript(u"User"_ustr, sPrompt);
    // Human-readable task narrative (Copilot-style) before model call.
    {
        const OUString intent = !m_sForcedCapability.isEmpty()
                                    ? m_sForcedCapability
                                    : DetectComposerIntent(sPrompt);
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
        const kqoffice::ai::chat::DocumentAIBinding aBind
            = kqoffice::ai::chat::DocumentAIContext::bindUserInput(sPrompt);
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"多步协作启动 · "_ustr + aBind.statusLabel
                                      + u" · 主文档不会自动改"_ustr);
        AppendTranscript(u"System"_ustr,
                         u"agent-pipeline · 多步协作启动：规划 → 执行 → 审查 · "_ustr
                             + aBind.statusLabel + u" · 不直接改主文档"_ustr);
        // Reset 任务 tab + step bar for this run.
        m_aAgentStepCache.clear();
        if (m_xAgentTree)
            m_xAgentTree->clear();
        PushAgentStepRow(u"1. 规划"_ustr, u"运行中"_ustr);
        PushAgentStepRow(u"2. 执行"_ustr, u"排队中"_ustr);
        PushAgentStepRow(u"3. 审查"_ustr, u"排队中"_ustr);
        LoadAgentSteps();
        UpdateAgentStepBar(0, u"规划中…"_ustr);
        m_bAgentRunActive = true;
        // Let cancel / append events process between long provider steps.
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
            = aBind.enrichedPrompt.isEmpty() ? sPrompt : aBind.enrichedPrompt;
        const kqoffice::ai::AgentPipelineResult pipe
            = kqoffice::ai::AgentStepRunner::runPlanActReview(sGoal, aBind.providerContext);
        m_bAgentRunActive = false;

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

        // Rebuild step list from pipeline results (Chinese labels + status).
        m_aAgentStepCache.clear();
        if (m_xAgentTree)
            m_xAgentTree->clear();
        for (size_t i = 0; i < pipe.steps.size(); ++i)
        {
            const auto& st = pipe.steps[i];
            const sal_Int32 stepIdx = (st.stepKind == u"plan"_ustr)     ? 0
                                      : (st.stepKind == u"act"_ustr)    ? 1
                                      : (st.stepKind == u"review"_ustr) ? 2
                                                                        : static_cast<sal_Int32>(i);
            UpdateAgentStepBar(stepIdx,
                               st.stepKind + u" "_ustr
                                   + LocalizeAgentStepStatus(st.status));
            OUString title = st.stepKind;
            if (title == u"plan"_ustr)
                title = u"1. 规划"_ustr;
            else if (title == u"act"_ustr)
                title = u"2. 执行"_ustr;
            else if (title == u"review"_ustr)
                title = u"3. 审查"_ustr;
            else
                title = OUString::number(static_cast<sal_Int32>(i) + 1) + u". "_ustr + title;

            OUString statusZh = LocalizeAgentStepStatus(st.status);

            // Short preview in status column when available.
            if (!st.content.isEmpty() && st.status == u"ok"_ustr)
            {
                OUString preview = st.content;
                if (preview.getLength() > 48)
                    preview = preview.copy(0, 48) + u"…"_ustr;
                // Collapse whitespace for tree cell.
                preview = preview.replaceAll(u"\n"_ustr, u" "_ustr);
                statusZh = u"完成 · "_ustr + preview;
            }
            PushAgentStepRow(title, statusZh);

            OUString body = st.content;
            if (body.getLength() > 400)
                body = body.copy(0, 400) + u"…"_ustr;
            AppendTranscript(u"Agent·"_ustr + title,
                             (body.isEmpty() ? (u"状态="_ustr + statusZh) : body),
                             /*bPersistHistory*/ false);
        }
        if (pipe.steps.empty())
        {
            PushAgentStepRow(u"1. 规划"_ustr, pipe.success ? u"完成"_ustr : u"失败"_ustr);
            PushAgentStepRow(u"2. 执行"_ustr, pipe.success ? u"完成"_ustr : u"未运行"_ustr);
            PushAgentStepRow(u"3. 审查"_ustr, pipe.success ? u"完成"_ustr : u"未运行"_ustr);
        }
        LoadAgentSteps();

        SetState(AIChatPanelState::Streaming);
        if (!pipe.combinedContent.isEmpty())
            AppendAssistantMarkdown(pipe.combinedContent);
        else
            AppendAssistantChunk(u"（多步协作无返回正文）"_ustr);

        if (pipe.success)
        {
            UpdateAgentStepBar(3, u"完成 · 待批准写回"_ustr);
            SetAgentStepBar(u"步骤：✓ 规划 → ✓ 执行 → ✓ 审查 · 待批准写回（主文档未改）"_ustr);
            m_sLastOutcomeDetail.clear();
            SetState(AIChatPanelState::AwaitingApproval);
            AppendTerminalEvidence(u"ok"_ustr, pipe.finalEvidenceId);
            if (m_xStatusLabel)
                m_xStatusLabel->set_label(
                    FormatEvidenceUserSummary(u"ok"_ustr, pipe.finalEvidenceId));
            AppendTranscript(
                u"System"_ustr,
                u"多步协作完成 · 步骤="_ustr
                    + OUString::number(static_cast<sal_Int32>(pipe.steps.size()))
                    + u" · 证据="_ustr + pipe.finalEvidenceId
                    + u" · 请到「审核」页批准写回 · 主文档未改"_ustr);
            const OUString sArtifactBody
                = !pipe.applyCandidateContent.isEmpty() ? pipe.applyCandidateContent
                                                        : pipe.combinedContent;
            RegisterAssistantArtifact(sArtifactBody, pipe.finalEvidenceId, u"agent"_ustr);
            // Stage act output only — review is advisory; never auto-apply.
            StagePendingApplyPlan(pipe.applyCandidateContent, pipe.finalEvidenceId);
        }
        else
        {
            const OUString failDetail
                = ShortenUserDetail(pipe.failureReason.isEmpty()
                                        ? LocalizeProviderStatusZh(u"provider-error"_ustr)
                                        : pipe.failureReason);
            m_sLastOutcomeDetail = failDetail;
            SetAgentStepBar(u"步骤：失败 · 主文档未改 · "_ustr + failDetail);
            ClearPendingPlan();
            SetState(AIChatPanelState::Failed);
            AppendTerminalEvidence(u"provider-error"_ustr, pipe.finalEvidenceId);
            if (m_xStatusLabel)
                m_xStatusLabel->set_label(u"失败 · 主文档未改 · "_ustr + failDetail);
            AppendTranscript(u"System"_ustr,
                             u"多步协作失败："_ustr + failDetail + u" · 主文档未改"_ustr);
            LoadReviewQueue();
        }
        FocusPrompt();
        return;
    }

    const bool bDocRag = kqoffice::ai::chat::DocumentAILocalRag::wantsDocumentRag(sPrompt)
                         || sPrompt.indexOf(u"问本文档"_ustr) >= 0
                         || sPrompt.startsWith(u"/问"_ustr);
    const css::ai::ProviderResponse aResponse = CallProvider(sPrompt);

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
    if (bDocRag && aResponse.status == u"ok"_ustr && !aResponse.content.isEmpty())
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
    else if (aResponse.content.isEmpty())
    {
        AppendAssistantChunk(u"（空响应）"_ustr);
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
        m_sLastOutcomeDetail.clear();
        SetState(AIChatPanelState::AwaitingApproval);
        AppendTerminalEvidence(aResponse.status, aResponse.evidenceId);
        RegisterAssistantArtifact(displayContent.isEmpty() ? aResponse.content : displayContent,
                                  aResponse.evidenceId,
                                  bDocRag ? u"ask-document"_ustr : u"chat"_ustr);
        // Stage only — never mutate the main document before explicit approval.
        // Pure Q&A cards usually don't need apply; still stage raw model text if useful.
        StagePendingApplyPlan(aResponse.content, aResponse.evidenceId);
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
        // Failure: never stage / never mutate main doc; surface Chinese reason + evidence line.
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
        AppendTranscript(u"System"_ustr,
                         u"请求失败："_ustr + detail + u" · 主文档未改 · 可改指令后重试"_ustr,
                         /*bPersistHistory*/ false);
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
    UpdatePendingPlanChip();
    LoadReviewQueue();
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
            u"plan-stage-skipped reason=no-valid-apply-plan main-document-mutation=false"
            " · 未生成可写回计划 · 主文档未改 · 可继续对话或改选区后重试"_ustr);
        if (m_xStatusLabel)
            m_xStatusLabel->set_label(u"未生成可写回计划 · 主文档未改 · 可改指令后重试"_ustr);
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
        m_xStatusLabel->set_label(u"计划已暂存，请到「审核」页批准写回: "_ustr
                                  + m_aPendingPlan.planId);
    UpdatePendingPlanChip();
    UpdateSelectionChip();
    UpdateActions();
    LoadReviewQueue();
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

    // Document AI Fabric: Writer → native ApplyEngine (undo-grouped);
    // Calc/Impress → UNO DiffApplier; chart → InsertObjectChart dispatch.
    // Only after explicit human approval + permission prompt.
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
                             u"已打开插入图表向导；请在向导中完成放置。文档变更可撤销。"_ustr,
                             /*bPersistHistory*/ false);
        }
        else
        {
            SetAgentStepBar(u"步骤：✓ 已批准写回 · "_ustr + aResult.engine);
            if (aResult.surface == u"impress"_ustr)
            {
                AppendTranscript(
                    u"System"_ustr,
                    u"设计流下一步：点「④导出」或菜单 文件→导出为→PPTX…（须你选择路径；AI 不静默导出）"_ustr,
                    /*bPersistHistory*/ false);
                if (m_xStatusLabel)
                    m_xStatusLabel->set_label(u"已写回 · 可导出 PPTX · 证据 "_ustr + evid);
            }
            // Writer ApplyEngine already opens Diff Review; for UNO path (Calc/Impress)
            // open the shared Diff Review dialog so write-back UX stays aligned.
            TryShowDiffReviewAfterApply(sPlanId, aResult.engine, true);
        }
        ClearPendingPlan();
        m_sLastOutcomeDetail.clear();
        SetState(AIChatPanelState::Applied);
        m_xStatusLabel->set_label(u"已写回（可撤销）· "_ustr + aResult.engine + u" · "_ustr
                                  + sPlanId
                                  + (evid.isEmpty() ? OUString()
                                                    : (u" · 证据 "_ustr + evid)));
        kqoffice::ai::workbench::WorkTelemetryStore::recordSimple(u"ai_apply"_ustr, 1);
        RecordWorkspaceActivity(u"action-invoked"_ustr, u"reviews"_ustr, sPlanId, evid,
                                OUString(), bChart ? u"chart-insert"_ustr : u"diff-review"_ustr);
        return true;
    }

    const OUString sFailReason
        = ShortenUserDetail(aResult.error.isEmpty()
                                ? (aResult.engine.isEmpty() ? u"未知原因"_ustr : aResult.engine)
                                : aResult.error);
    m_sLastOutcomeDetail = sFailReason;
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
                           " · 写回失败 · 主文档未改"_ustr
                         + (failEvid.isEmpty() ? OUString()
                                               : (u" · 证据 "_ustr + failEvid)));
    m_xStatusLabel->set_label(u"写回失败 · 主文档未改 · "_ustr + sFailReason
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
        ApplyComposerIntent(u"rewrite"_ustr,
                            u"请为当前选区写出可写入单元格的公式，第一行以 = 开头："_ustr);
    else if (surface == u"impress"_ustr)
        ApplyComposerIntent(u"rewrite"_ustr, u"请改写本页标题与要点，更清晰专业："_ustr);
    else
        ApplyComposerIntent(u"rewrite"_ustr, u"请改写得更清晰专业："_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnIntentShortenClicked, weld::Button&, void)
{
    const OUString surface = CurrentDocumentSurface();
    if (surface == u"calc"_ustr)
        ApplyComposerIntent(u"shorten"_ustr,
                            u"请指出选区数据清洗问题，并尽量给出 = 公式："_ustr);
    else
        ApplyComposerIntent(u"shorten"_ustr, u"请精简以下内容，保留要点："_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnIntentExpandClicked, weld::Button&, void)
{
    const OUString surface = CurrentDocumentSurface();
    if (surface == u"calc"_ustr)
        ApplyComposerIntent(u"expand"_ustr,
                            u"请对选区做汇总（合计/平均/计数），第一行 = 公式："_ustr);
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
        ApplyComposerIntent(u"summarize"_ustr, u"请解读当前表格数据的关键结论："_ustr);
    else
        ApplyComposerIntent(u"summarize"_ustr, u"请总结要点："_ustr);
}

IMPL_LINK_NOARG(AIChatPanel, OnIntentPlanClicked, weld::Button&, void)
{
    const OUString surface = CurrentDocumentSurface();
    if (surface == u"impress"_ustr)
        ApplyComposerIntent(
            u"plan"_ustr,
            u"【多方案板】请给出方案A/B/C（页序对比、适合场景；不要 ## 写回体）："_ustr);
    else
        ApplyComposerIntent(u"plan"_ustr, u"请做一份分步规划："_ustr);
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
        m_xStatusLabel->set_label(u"正在运行任务：规划 → 执行 → 审查…"_ustr);
    SubmitPrompt();
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
    OUString source = u"inject"_ustr;
    if (text.indexOf(u"【可圈定时任务】"_ustr) >= 0)
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

    // Optional auto-send for due scheduled tasks only (default off — human review).
    bool bAutoSend = false;
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
        if (source == u"schedule"_ustr)
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
        AppendTranscript(u"System"_ustr,
                         u"schedule-auto-send=1 · 主文档写回仍须批准"_ustr,
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
