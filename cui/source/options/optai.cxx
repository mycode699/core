/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 */

#include "optai.hxx"

#include <DocumentAIInputPrefs.hxx>
#include <DocumentAIScenarioStore.hxx>
#include <EvidenceRecorder.hxx>
#include <ModelRoles.hxx>
#include <ModelRoutingConfig.hxx>
#include <OllamaAdapter.hxx>
#include <PermissionCenter.hxx>

#include <com/sun/star/ui/dialogs/ExecutableDialogResults.hpp>
#include <com/sun/star/ui/dialogs/XFolderPicker2.hpp>
#include <comphelper/processfactory.hxx>
#include <osl/file.hxx>
#include <rtl/ustrbuf.hxx>
#include <sfx2/filedlghelper.hxx>
#include <vcl/transfer.hxx>
#include <vcl/vclenum.hxx>
#include <vcl/weld/Builder.hxx>
#include <vcl/weld/ComboBox.hxx>
#include <vcl/weld/Entry.hxx>
#include <vcl/weld/TreeView.hxx>
#include <vcl/weld/weld.hxx>

#include <vector>

using namespace kqoffice::ai;
using namespace kqoffice::ai::chat;

namespace
{
void scenarioReorderThunk(void* pUser)
{
    if (pUser)
        static_cast<OptAiTabPage*>(pUser)->OnScenarioTreeReordered();
}
}

OptAiTabPage::OptAiTabPage(weld::Container* pPage, weld::DialogController* pController,
                           const SfxItemSet& rSet)
    : SfxTabPage(pPage, pController, u"cui/ui/optaipage.ui"_ustr, u"OptAiPage"_ustr, &rSet)
    , m_xStatusLabel(m_xBuilder->weld_label(u"status_label"_ustr))
    , m_xPathLabel(m_xBuilder->weld_label(u"path_label"_ustr))
    , m_xBackend(m_xBuilder->weld_entry(u"backend"_ustr))
    , m_xBaseUrl(m_xBuilder->weld_entry(u"baseurl"_ustr))
    , m_xPrimary(m_xBuilder->weld_entry(u"primary"_ustr))
    , m_xLight(m_xBuilder->weld_entry(u"light"_ustr))
    , m_xAgent(m_xBuilder->weld_entry(u"agent"_ustr))
    , m_xPlan(m_xBuilder->weld_entry(u"plan"_ustr))
    , m_xReview(m_xBuilder->weld_entry(u"review"_ustr))
    , m_xInstalledModels(m_xBuilder->weld_combo_box(u"installed_models"_ustr))
    , m_xProbeBtn(m_xBuilder->weld_button(u"probe_btn"_ustr))
    , m_xApplyPrimaryBtn(m_xBuilder->weld_button(u"apply_primary_btn"_ustr))
    , m_xComboSingleBtn(m_xBuilder->weld_button(u"combo_single_btn"_ustr))
    , m_xComboDualBtn(m_xBuilder->weld_button(u"combo_dual_btn"_ustr))
    , m_xOpenPathBtn(m_xBuilder->weld_button(u"open_path_btn"_ustr))
    , m_xScenarioList(m_xBuilder->weld_tree_view(u"scenario_list"_ustr))
    , m_xScId(m_xBuilder->weld_entry(u"sc_id"_ustr))
    , m_xScTitle(m_xBuilder->weld_entry(u"sc_title"_ustr))
    , m_xScCategory(m_xBuilder->weld_entry(u"sc_category"_ustr))
    , m_xScSurface(m_xBuilder->weld_entry(u"sc_surface"_ustr))
    , m_xScCapability(m_xBuilder->weld_entry(u"sc_capability"_ustr))
    , m_xScSlash(m_xBuilder->weld_entry(u"sc_slash"_ustr))
    , m_xScPrompt(m_xBuilder->weld_entry(u"sc_prompt"_ustr))
    , m_xScSortOrder(m_xBuilder->weld_entry(u"sc_sort_order"_ustr))
    , m_xScEnabled(m_xBuilder->weld_check_button(u"sc_enabled"_ustr))
    , m_xScShowBtn(m_xBuilder->weld_check_button(u"sc_show_btn"_ustr))
    , m_xScPinned(m_xBuilder->weld_check_button(u"sc_pinned"_ustr))
    , m_xScAttachSel(m_xBuilder->weld_check_button(u"sc_attach_sel"_ustr))
    , m_xScDocCtx(m_xBuilder->weld_check_button(u"sc_doc_ctx"_ustr))
    , m_xScAutoSubmit(m_xBuilder->weld_check_button(u"sc_auto_submit"_ustr))
    , m_xScAgent(m_xBuilder->weld_check_button(u"sc_agent"_ustr))
    , m_xScApproval(m_xBuilder->weld_check_button(u"sc_approval"_ustr))
    , m_xScNewBtn(m_xBuilder->weld_button(u"sc_new_btn"_ustr))
    , m_xScSaveBtn(m_xBuilder->weld_button(u"sc_save_btn"_ustr))
    , m_xScDeleteBtn(m_xBuilder->weld_button(u"sc_delete_btn"_ustr))
    , m_xScResetBtn(m_xBuilder->weld_button(u"sc_reset_btn"_ustr))
    , m_xScUpBtn(m_xBuilder->weld_button(u"sc_up_btn"_ustr))
    , m_xScDownBtn(m_xBuilder->weld_button(u"sc_down_btn"_ustr))
    , m_xScPathLabel(m_xBuilder->weld_label(u"sc_path_label"_ustr))
    , m_xVoiceEnabled(m_xBuilder->weld_check_button(u"voice_enabled"_ustr))
    , m_xVoiceBackend(m_xBuilder->weld_combo_box(u"voice_backend"_ustr))
    , m_xVoiceCmd(m_xBuilder->weld_entry(u"voice_cmd"_ustr))
    , m_xVoicePtt(m_xBuilder->weld_check_button(u"voice_ptt"_ustr))
    , m_xVoiceFnHint(m_xBuilder->weld_check_button(u"voice_fn_hint"_ustr))
    , m_xScreenshotEnabled(m_xBuilder->weld_check_button(u"screenshot_enabled"_ustr))
    , m_xScreenshotMode(m_xBuilder->weld_combo_box(u"screenshot_mode"_ustr))
    , m_xScreenshotAttach(m_xBuilder->weld_check_button(u"screenshot_attach"_ustr))
    , m_xScreenshotOpenAi(m_xBuilder->weld_check_button(u"screenshot_open_ai"_ustr))
    , m_xScreenshotClipboard(m_xBuilder->weld_check_button(u"screenshot_clipboard"_ustr))
    , m_xScheduleAutoSend(m_xBuilder->weld_check_button(u"schedule_auto_send"_ustr))
    , m_xWsDirList(m_xBuilder->weld_tree_view(u"ws_dir_list"_ustr))
    , m_xWsGrantBtn(m_xBuilder->weld_button(u"ws_grant_btn"_ustr))
    , m_xWsRevokeBtn(m_xBuilder->weld_button(u"ws_revoke_btn"_ustr))
    , m_xWsRevokeAllBtn(m_xBuilder->weld_button(u"ws_revoke_all_btn"_ustr))
    , m_xWsClearSessionBtn(m_xBuilder->weld_button(u"ws_clear_session_btn"_ustr))
    , m_xWsNetworkRevokeBtn(m_xBuilder->weld_button(u"ws_network_revoke_btn"_ustr))
    , m_xWsNetworkStatus(m_xBuilder->weld_label(u"ws_network_status"_ustr))
    , m_xWsCapMicStatus(m_xBuilder->weld_label(u"ws_cap_mic_status"_ustr))
    , m_xWsCapShotStatus(m_xBuilder->weld_label(u"ws_cap_shot_status"_ustr))
    , m_xWsRiskPolicyLabel(m_xBuilder->weld_label(u"ws_risk_policy_label"_ustr))
    , m_xWsStatusLabel(m_xBuilder->weld_label(u"ws_status_label"_ustr))
{
    m_xProbeBtn->connect_clicked(LINK(this, OptAiTabPage, OnProbeClicked));
    m_xApplyPrimaryBtn->connect_clicked(LINK(this, OptAiTabPage, OnApplyPrimaryToAllClicked));
    m_xComboSingleBtn->connect_clicked(LINK(this, OptAiTabPage, OnComboSingleClicked));
    m_xComboDualBtn->connect_clicked(LINK(this, OptAiTabPage, OnComboDualClicked));
    m_xOpenPathBtn->connect_clicked(LINK(this, OptAiTabPage, OnOpenConfigDirClicked));
    if (m_xScenarioList)
    {
        m_xScenarioList->set_selection_mode(SelectionMode::Single);
        m_xScenarioList->connect_selection_changed(LINK(this, OptAiTabPage, OnScenarioListChanged));
        // Drag reorder within list
        m_xScenarioDragHelper = new TransferDataContainer;
        m_xScenarioDragHelper->CopyString(u"kqoffice-scenario"_ustr);
        m_xScenarioList->enable_drag_source(m_xScenarioDragHelper, DND_ACTION_MOVE);
        m_xScenarioDropTarget = std::make_unique<ScenarioListDropTarget>(
            *m_xScenarioList, this, &scenarioReorderThunk);
    }
    m_xScNewBtn->connect_clicked(LINK(this, OptAiTabPage, OnScenarioNewClicked));
    m_xScSaveBtn->connect_clicked(LINK(this, OptAiTabPage, OnScenarioSaveClicked));
    m_xScDeleteBtn->connect_clicked(LINK(this, OptAiTabPage, OnScenarioDeleteClicked));
    m_xScResetBtn->connect_clicked(LINK(this, OptAiTabPage, OnScenarioResetClicked));
    m_xScUpBtn->connect_clicked(LINK(this, OptAiTabPage, OnScenarioUpClicked));
    m_xScDownBtn->connect_clicked(LINK(this, OptAiTabPage, OnScenarioDownClicked));
    if (m_xWsDirList)
        m_xWsDirList->set_selection_mode(SelectionMode::Single);
    if (m_xWsGrantBtn)
        m_xWsGrantBtn->connect_clicked(LINK(this, OptAiTabPage, OnWorkspaceGrantClicked));
    if (m_xWsRevokeBtn)
        m_xWsRevokeBtn->connect_clicked(LINK(this, OptAiTabPage, OnWorkspaceRevokeClicked));
    if (m_xWsRevokeAllBtn)
        m_xWsRevokeAllBtn->connect_clicked(LINK(this, OptAiTabPage, OnWorkspaceRevokeAllClicked));
    if (m_xWsClearSessionBtn)
        m_xWsClearSessionBtn->connect_clicked(
            LINK(this, OptAiTabPage, OnWorkspaceClearSessionClicked));
    if (m_xWsNetworkRevokeBtn)
        m_xWsNetworkRevokeBtn->connect_clicked(LINK(this, OptAiTabPage, OnNetworkRevokeClicked));
}

OptAiTabPage::~OptAiTabPage() = default;

OUString OptAiTabPage::SelectedScenarioId() const
{
    if (!m_xScenarioList)
        return OUString();
    return m_xScenarioList->get_selected_id();
}

void OptAiTabPage::OnScenarioTreeReordered()
{
    if (!m_xScenarioList)
        return;
    std::vector<OUString> ids;
    const int n = m_xScenarioList->n_children();
    ids.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        ids.push_back(m_xScenarioList->get_id(i));
    if (!DocumentAIScenarioStore::reorderByIds(m_aScenarioCatalog, ids))
        return;
    DocumentAIScenarioStore::save(m_aScenarioCatalog);
    const OUString sel = SelectedScenarioId();
    ReloadScenarioList(sel);
    m_xStatusLabel->set_label(u"已通过拖拽更新方案顺序（侧栏网格将按此顺序显示）。"_ustr);
}

void OptAiTabPage::ReloadScenarioList(const OUString& rSelectId)
{
    m_aScenarioCatalog = DocumentAIScenarioStore::load();
    if (!m_xScenarioList)
        return;
    m_xScenarioList->clear();
    int active = 0;
    for (sal_Int32 i = 0; i < static_cast<sal_Int32>(m_aScenarioCatalog.items.size()); ++i)
    {
        const DocumentAIScenario& s = m_aScenarioCatalog.items[static_cast<size_t>(i)];
        OUString label = s.titleZh;
        if (s.options.pinned)
            label = u"📌 "_ustr + label;
        if (!s.enabled)
            label += u" [停用]"_ustr;
        if (!s.options.showAsButton)
            label += u" [无按钮]"_ustr;
        if (s.builtin)
            label += u" ★"_ustr;
        label += u" ·#"_ustr + OUString::number(s.sortOrder);
        m_xScenarioList->append(s.id, label);
        if (!rSelectId.isEmpty() && s.id == rSelectId)
            active = static_cast<int>(i);
    }
    if (m_xScenarioList->n_children() > 0)
    {
        m_xScenarioList->select(active);
        OnScenarioListChanged(*m_xScenarioList);
    }
    m_xScPathLabel->set_label(u"方案配置："_ustr + DocumentAIScenarioStore::defaultConfigPath()
                              + u"  · 可拖拽列表排序"_ustr);
}

void OptAiTabPage::FillScenarioForm(const DocumentAIScenario& r)
{
    m_xScId->set_text(r.id);
    m_xScTitle->set_text(r.titleZh);
    m_xScCategory->set_text(r.category);
    m_xScSurface->set_text(r.preferredSurface);
    m_xScCapability->set_text(r.capabilityHint);
    m_xScSlash->set_text(r.slashCommand);
    m_xScPrompt->set_text(r.promptTemplate);
    if (m_xScSortOrder)
        m_xScSortOrder->set_text(OUString::number(r.sortOrder));
    m_xScEnabled->set_active(r.enabled);
    m_xScShowBtn->set_active(r.options.showAsButton);
    if (m_xScPinned)
        m_xScPinned->set_active(r.options.pinned);
    m_xScAttachSel->set_active(r.options.attachSelection);
    m_xScDocCtx->set_active(r.options.includeDocContext);
    m_xScAutoSubmit->set_active(r.options.autoSubmit);
    m_xScAgent->set_active(r.options.useAgentPipeline);
    m_xScApproval->set_active(r.options.requireApproval);
    // Builtin ids are read-only for identity
    m_xScId->set_sensitive(!r.builtin);
}

void OptAiTabPage::ClearScenarioForm()
{
    m_xScId->set_text(OUString());
    m_xScTitle->set_text(OUString());
    m_xScCategory->set_text(u"general"_ustr);
    m_xScSurface->set_text(u"any"_ustr);
    m_xScCapability->set_text(u"chat"_ustr);
    m_xScSlash->set_text(OUString());
    m_xScPrompt->set_text(u"【自定义】\n{selection}"_ustr);
    if (m_xScSortOrder)
        m_xScSortOrder->set_text(u"500"_ustr);
    m_xScEnabled->set_active(true);
    m_xScShowBtn->set_active(true);
    if (m_xScPinned)
        m_xScPinned->set_active(false);
    m_xScAttachSel->set_active(true);
    m_xScDocCtx->set_active(true);
    m_xScAutoSubmit->set_active(true);
    m_xScAgent->set_active(false);
    m_xScApproval->set_active(true);
    m_xScId->set_sensitive(true);
}

DocumentAIScenario OptAiTabPage::ReadScenarioForm() const
{
    DocumentAIScenario s;
    s.id = m_xScId->get_text().trim();
    s.titleZh = m_xScTitle->get_text().trim();
    s.category = m_xScCategory->get_text().trim();
    s.preferredSurface = m_xScSurface->get_text().trim();
    s.capabilityHint = m_xScCapability->get_text().trim();
    s.slashCommand = m_xScSlash->get_text().trim();
    s.promptTemplate = m_xScPrompt->get_text();
    s.enabled = m_xScEnabled->get_active();
    s.options.showAsButton = m_xScShowBtn->get_active();
    s.options.pinned = m_xScPinned && m_xScPinned->get_active();
    s.options.attachSelection = m_xScAttachSel->get_active();
    s.options.includeDocContext = m_xScDocCtx->get_active();
    s.options.autoSubmit = m_xScAutoSubmit->get_active();
    s.options.useAgentPipeline = m_xScAgent->get_active();
    s.options.requireApproval = m_xScApproval->get_active();
    s.sortOrder = 500;
    s.builtin = false;
    if (const DocumentAIScenario* existing = DocumentAIScenarioStore::find(m_aScenarioCatalog, s.id))
    {
        s.builtin = existing->builtin;
        s.sortOrder = existing->sortOrder;
    }
    // Explicit sortOrder field overrides preserved/default value when valid.
    if (m_xScSortOrder)
    {
        const OUString orderText = m_xScSortOrder->get_text().trim();
        if (!orderText.isEmpty())
        {
            const sal_Int32 parsed = orderText.toInt32();
            // toInt32 returns 0 on total parse failure; accept 0 and negatives as valid
            // only when the text is numeric (has at least one digit).
            bool hasDigit = false;
            for (sal_Int32 i = 0; i < orderText.getLength(); ++i)
            {
                if (orderText[i] >= u'0' && orderText[i] <= u'9')
                {
                    hasDigit = true;
                    break;
                }
            }
            if (hasDigit)
                s.sortOrder = parsed;
        }
    }
    return s;
}

void OptAiTabPage::FillFromSnapshot()
{
    ensureDefaultModelRoutingTemplate();
    const ModelRoutingSnapshot s = loadModelRoutingSnapshot();
    m_xBackend->set_text(s.backend.isEmpty() ? u"ollama"_ustr : s.backend);
    m_xBaseUrl->set_text(s.baseUrl.isEmpty() ? u"http://127.0.0.1:11434"_ustr : s.baseUrl);
    m_xPrimary->set_text(s.primaryModel);
    m_xLight->set_text(s.lightModel);
    m_xAgent->set_text(s.agentModel);
    m_xPlan->set_text(s.planModel);
    m_xReview->set_text(s.reviewModel);
    m_xPathLabel->set_label(u"配置文件："_ustr + modelRoutingConfigPathForDisplay());
    m_xStatusLabel->set_label(
        u"模型五槽 + 权限中心（工作区/网络/麦克风/截图）+ 语音/截图偏好 + 方案。F4 语音 · Ctrl/Cmd+Shift+A 截图。"_ustr);

    ReloadWorkspaceList();

    // Voice / screenshot prefs
    const DocumentAIInputPrefs ip = DocumentAIInputPrefs::load();
    if (m_xVoiceEnabled)
        m_xVoiceEnabled->set_active(ip.voiceEnabled);
    if (m_xVoiceBackend)
    {
        const OUString id = DocumentAIInputPrefs::voiceBackendToString(ip.voiceBackend);
        const int n = m_xVoiceBackend->get_count();
        for (int i = 0; i < n; ++i)
        {
            if (m_xVoiceBackend->get_id(i) == id)
            {
                m_xVoiceBackend->set_active(i);
                break;
            }
        }
    }
    if (m_xVoiceCmd)
        m_xVoiceCmd->set_text(ip.voiceCmd);
    if (m_xVoicePtt)
        m_xVoicePtt->set_active(ip.voicePushToTalk);
    if (m_xVoiceFnHint)
        m_xVoiceFnHint->set_active(ip.voiceShowFnHint);
    if (m_xScreenshotEnabled)
        m_xScreenshotEnabled->set_active(ip.screenshotEnabled);
    if (m_xScreenshotMode)
    {
        const OUString id = DocumentAIInputPrefs::screenshotModeToString(ip.screenshotMode);
        const int n = m_xScreenshotMode->get_count();
        for (int i = 0; i < n; ++i)
        {
            if (m_xScreenshotMode->get_id(i) == id)
            {
                m_xScreenshotMode->set_active(i);
                break;
            }
        }
    }
    if (m_xScreenshotAttach)
        m_xScreenshotAttach->set_active(ip.screenshotAutoAttachChat);
    if (m_xScreenshotOpenAi)
        m_xScreenshotOpenAi->set_active(ip.screenshotOpenAiPanel);
    if (m_xScreenshotClipboard)
        m_xScreenshotClipboard->set_active(ip.screenshotCopyClipboard);
    if (m_xScheduleAutoSend)
        m_xScheduleAutoSend->set_active(ip.scheduleAutoSend);

    ReloadScenarioList();
}

void OptAiTabPage::WriteToSnapshot()
{
    ModelRoutingSnapshot s;
    s.backend = m_xBackend->get_text().trim();
    s.baseUrl = m_xBaseUrl->get_text().trim();
    s.primaryModel = m_xPrimary->get_text().trim();
    s.lightModel = m_xLight->get_text().trim();
    s.agentModel = m_xAgent->get_text().trim();
    s.planModel = m_xPlan->get_text().trim();
    s.reviewModel = m_xReview->get_text().trim();
    s.smallFastModel = s.lightModel;
    s.subagentModel = s.agentModel;
    if (s.backend.isEmpty())
        s.backend = u"ollama"_ustr;
    if (!saveModelRoutingSnapshot(s))
    {
        m_xStatusLabel->set_label(u"保存失败：无法写入模型配置文件。"_ustr);
        return;
    }
    // Also persist current scenario form if filled
    DocumentAIScenario form = ReadScenarioForm();
    if (!form.id.isEmpty() && !form.titleZh.isEmpty())
    {
        DocumentAIScenarioStore::upsert(m_aScenarioCatalog, form);
        DocumentAIScenarioStore::save(m_aScenarioCatalog);
    }

    // Voice + screenshot prefs
    DocumentAIInputPrefs ip = DocumentAIInputPrefs::load();
    if (m_xVoiceEnabled)
        ip.voiceEnabled = m_xVoiceEnabled->get_active();
    if (m_xVoiceBackend)
        ip.voiceBackend = DocumentAIInputPrefs::voiceBackendFromString(
            m_xVoiceBackend->get_active_id());
    if (m_xVoiceCmd)
        ip.voiceCmd = m_xVoiceCmd->get_text().trim();
    if (m_xVoicePtt)
        ip.voicePushToTalk = m_xVoicePtt->get_active();
    if (m_xVoiceFnHint)
        ip.voiceShowFnHint = m_xVoiceFnHint->get_active();
    if (m_xScreenshotEnabled)
        ip.screenshotEnabled = m_xScreenshotEnabled->get_active();
    if (m_xScreenshotMode)
        ip.screenshotMode = DocumentAIInputPrefs::screenshotModeFromString(
            m_xScreenshotMode->get_active_id());
    if (m_xScreenshotAttach)
        ip.screenshotAutoAttachChat = m_xScreenshotAttach->get_active();
    if (m_xScreenshotOpenAi)
        ip.screenshotOpenAiPanel = m_xScreenshotOpenAi->get_active();
    if (m_xScreenshotClipboard)
        ip.screenshotCopyClipboard = m_xScreenshotClipboard->get_active();
    if (m_xScheduleAutoSend)
        ip.scheduleAutoSend = m_xScheduleAutoSend->get_active();
    if (!DocumentAIInputPrefs::save(ip))
    {
        m_xStatusLabel->set_label(u"模型已保存；语音/截图/定时偏好写入失败。"_ustr);
        return;
    }

    m_xStatusLabel->set_label(
        u"已保存模型路由、语音/截图/定时偏好与方案配置。F4 语音 · ⌘/Ctrl+Shift+A 截图。"_ustr);
    m_xPathLabel->set_label(u"配置文件："_ustr + modelRoutingConfigPathForDisplay());
}

void OptAiTabPage::Reset(const SfxItemSet*) { FillFromSnapshot(); }

OUString OptAiTabPage::GetAllStrings()
{
    OUStringBuffer sAll;
    const OUString labels[] = { u"label_intro"_ustr, u"label_backend"_ustr, u"label_baseurl"_ustr,
                                u"label_primary"_ustr, u"label_light"_ustr, u"label_agent"_ustr,
                                u"label_plan"_ustr, u"label_review"_ustr, u"label_models"_ustr };
    for (const auto& id : labels)
    {
        if (const auto p = m_xBuilder->weld_label(id))
            sAll.append(p->get_label() + u" "_ustr);
    }
    return sAll.makeStringAndClear().replaceAll(u"_"_ustr, u""_ustr);
}

bool OptAiTabPage::FillItemSet(SfxItemSet*)
{
    WriteToSnapshot();
    return false;
}

std::unique_ptr<SfxTabPage> OptAiTabPage::Create(weld::Container* pPage,
                                                 weld::DialogController* pController,
                                                 const SfxItemSet* rAttrSet)
{
    return std::make_unique<OptAiTabPage>(pPage, pController, *rAttrSet);
}

IMPL_LINK_NOARG(OptAiTabPage, OnProbeClicked, weld::Button&, void)
{
    // Full five-slot diagnostic (light background + review linkage).
    const ModelRoutingDiagnostics d = diagnoseModelRouting();
    m_xInstalledModels->clear();
    if (!d.ollamaReachable)
    {
        m_xStatusLabel->set_label(d.summaryZh);
        return;
    }
    OllamaAdapter adapter;
    const auto models = adapter.listModels();
    if (models.empty())
    {
        m_xStatusLabel->set_label(d.summaryZh + u"\n请执行 ollama pull <model>。"_ustr);
        return;
    }
    for (const auto& m : models)
        m_xInstalledModels->append_text(m);
    m_xInstalledModels->set_active(0);
    if (m_xPrimary->get_text().trim().isEmpty() && !d.primaryResolved.isEmpty())
        m_xPrimary->set_text(d.primaryResolved);
    else if (m_xPrimary->get_text().trim().isEmpty())
        m_xPrimary->set_text(models.front());
    // Suggest light/review if empty so background + review paths have models.
    if (m_xLight->get_text().trim().isEmpty() && !d.lightResolved.isEmpty())
        m_xLight->set_text(d.lightResolved);
    if (m_xReview->get_text().trim().isEmpty() && !d.reviewResolved.isEmpty())
        m_xReview->set_text(d.reviewResolved);

    EvidenceRecord rec;
    rec.serviceMode = u"offline"_ustr;
    rec.provider = u"options-probe light="_ustr
                   + (d.lightResolved.isEmpty() ? u"?"_ustr : d.lightResolved)
                   + u" review="_ustr
                   + (d.reviewResolved.isEmpty() ? u"?"_ustr : d.reviewResolved);
    rec.capability = u"background"_ustr;
    rec.status = d.ollamaReachable
                     ? (d.lightReady && d.reviewReady ? u"ok"_ustr : u"degraded"_ustr)
                     : u"provider-error"_ustr;
    rec.responseSizeBytes = d.summaryZh.getLength();
    EvidenceRecorder recorder;
    const OUString evId = recorder.record(rec);
    OUString status = d.summaryZh;
    if (!evId.isEmpty())
        status += u"\nevidence="_ustr + evId;
    m_xStatusLabel->set_label(status);
}

OUString OptAiTabPage::ResolvePrimaryOrSelected() const
{
    OUString primary = m_xPrimary->get_text().trim();
    if (primary.isEmpty())
        primary = m_xInstalledModels->get_active_text().trim();
    return primary;
}

IMPL_LINK_NOARG(OptAiTabPage, OnApplyPrimaryToAllClicked, weld::Button&, void)
{
    const OUString p = ResolvePrimaryOrSelected();
    if (p.isEmpty())
    {
        m_xStatusLabel->set_label(u"请先填写主模型，或探测 Ollama 后选择模型。"_ustr);
        return;
    }
    m_xPrimary->set_text(p);
    m_xLight->set_text(p);
    m_xAgent->set_text(p);
    m_xPlan->set_text(p);
    m_xReview->set_text(p);
    m_xStatusLabel->set_label(u"已将主模型复制到全部五个槽位。"_ustr);
}

IMPL_LINK_NOARG(OptAiTabPage, OnComboSingleClicked, weld::Button&, void)
{
    const OUString p = ResolvePrimaryOrSelected();
    if (p.isEmpty())
    {
        m_xStatusLabel->set_label(u"单模型预设需要主模型。"_ustr);
        return;
    }
    m_xPrimary->set_text(p);
    m_xLight->set_text(p);
    m_xAgent->set_text(p);
    m_xPlan->set_text(p);
    m_xReview->set_text(p);
    m_xStatusLabel->set_label(u"预设「单模型」已应用。"_ustr);
}

IMPL_LINK_NOARG(OptAiTabPage, OnComboDualClicked, weld::Button&, void)
{
    OUString primary = ResolvePrimaryOrSelected();
    OUString light = m_xLight->get_text().trim();
    const sal_Int32 nCount = m_xInstalledModels->get_count();
    if (nCount >= 2)
    {
        const OUString m0 = m_xInstalledModels->get_text(0).trim();
        const OUString m1 = m_xInstalledModels->get_text(1).trim();
        if (primary.isEmpty())
            primary = m0;
        if (light.isEmpty() || light == primary)
            light = (m1 != primary) ? m1 : m0;
        if (light == primary && m0 != m1)
            light = (primary == m0) ? m1 : m0;
    }
    if (primary.isEmpty())
    {
        m_xStatusLabel->set_label(u"主+轻量预设需要至少一个模型。"_ustr);
        return;
    }
    if (light.isEmpty())
        light = primary;
    m_xPrimary->set_text(primary);
    m_xLight->set_text(light);
    m_xAgent->set_text(primary);
    m_xPlan->set_text(primary);
    m_xReview->set_text(primary);
    m_xStatusLabel->set_label(u"预设「主+轻量」已应用。"_ustr);
}

IMPL_LINK_NOARG(OptAiTabPage, OnOpenConfigDirClicked, weld::Button&, void)
{
    kqoffice::ai::control::PermissionCenter perms;
    m_xStatusLabel->set_label(u"模型："_ustr + modelRoutingConfigPathForDisplay() + u"\n方案："_ustr
                              + DocumentAIScenarioStore::defaultConfigPath()
                              + u"\n工作区权限："_ustr + perms.storePathForDisplay());
}

OUString OptAiTabPage::SelectedWorkspacePath() const
{
    if (!m_xWsDirList)
        return OUString();
    return m_xWsDirList->get_selected_id();
}

void OptAiTabPage::ReloadWorkspaceList()
{
    using kqoffice::ai::control::CapabilityPermission;
    using kqoffice::ai::control::PermissionCenter;
    using kqoffice::ai::control::PermissionState;

    PermissionCenter perms;
    if (m_xWsDirList)
    {
        m_xWsDirList->clear();
        for (const auto& d : perms.authorizedDirectories())
        {
            OUString label = d.path;
            if (d.recursive)
                label += u" （含子目录）"_ustr;
            m_xWsDirList->append(d.path, label);
        }
    }

    if (m_xWsNetworkStatus)
        m_xWsNetworkStatus->set_label(perms.networkStatusLineZh());
    if (m_xWsCapMicStatus)
        m_xWsCapMicStatus->set_label(
            perms.settingsCapabilityStatusZh(CapabilityPermission::Microphone));
    if (m_xWsCapShotStatus)
        m_xWsCapShotStatus->set_label(
            perms.settingsCapabilityStatusZh(CapabilityPermission::ScreenCapture));
    if (m_xWsRiskPolicyLabel)
        m_xWsRiskPolicyLabel->set_label(PermissionCenter::riskPolicyHintZh());

    const auto netState = perms.stateOf(CapabilityPermission::NetworkEgress);
    if (m_xWsNetworkRevokeBtn)
        m_xWsNetworkRevokeBtn->set_sensitive(netState == PermissionState::Granted);
    if (m_xWsRevokeBtn)
        m_xWsRevokeBtn->set_sensitive(perms.hasAnyAuthorizedDirectory());
    if (m_xWsRevokeAllBtn)
        m_xWsRevokeAllBtn->set_sensitive(perms.hasAnyAuthorizedDirectory());
    if (m_xWsClearSessionBtn)
        m_xWsClearSessionBtn->set_sensitive(perms.sessionRiskGrantCount() > 0);

    if (m_xWsStatusLabel)
    {
        // Full multi-line summary for discoverability + store path.
        m_xWsStatusLabel->set_label(perms.settingsSurfaceSummaryZh() + u"\n存储："_ustr
                                    + perms.storePathForDisplay());
    }
}

IMPL_LINK_NOARG(OptAiTabPage, OnWorkspaceGrantClicked, weld::Button&, void)
{
    try
    {
        const css::uno::Reference<css::uno::XComponentContext>& xContext
            = ::comphelper::getProcessComponentContext();
        css::uno::Reference<css::ui::dialogs::XFolderPicker2> xPicker
            = sfx2::createFolderPicker(xContext, GetFrameWeld());
        if (!xPicker.is())
        {
            m_xStatusLabel->set_label(u"无法打开文件夹选择器。"_ustr);
            return;
        }
        if (xPicker->execute() != css::ui::dialogs::ExecutableDialogResults::OK)
            return;

        OUString dirUrl = xPicker->getDirectory();
        OUString systemPath;
        if (osl::FileBase::getSystemPathFromFileURL(dirUrl, systemPath) != osl::FileBase::E_None
            || systemPath.isEmpty())
            systemPath = dirUrl;

        kqoffice::ai::control::PermissionCenter perms;
        if (!perms.grantDirectory(systemPath, true))
        {
            m_xStatusLabel->set_label(
                u"授权失败：不能授权整个用户主目录或系统根路径，请选择更具体的工作文件夹。"_ustr);
            ReloadWorkspaceList();
            return;
        }
        m_xStatusLabel->set_label(u"已授权工作区："_ustr + systemPath);
        ReloadWorkspaceList();
    }
    catch (const css::uno::Exception&)
    {
        m_xStatusLabel->set_label(u"授权目录时发生错误。"_ustr);
    }
}

IMPL_LINK_NOARG(OptAiTabPage, OnWorkspaceRevokeClicked, weld::Button&, void)
{
    const OUString path = SelectedWorkspacePath();
    if (path.isEmpty())
    {
        m_xStatusLabel->set_label(u"请先在列表中选择要撤销的目录。"_ustr);
        return;
    }
    kqoffice::ai::control::PermissionCenter perms;
    if (!perms.revokeDirectory(path))
    {
        m_xStatusLabel->set_label(u"撤销失败："_ustr + path);
        return;
    }
    m_xStatusLabel->set_label(u"已撤销目录授权："_ustr + path);
    ReloadWorkspaceList();
}

IMPL_LINK_NOARG(OptAiTabPage, OnWorkspaceRevokeAllClicked, weld::Button&, void)
{
    kqoffice::ai::control::PermissionCenter perms;
    if (!perms.hasAnyAuthorizedDirectory())
    {
        m_xStatusLabel->set_label(u"当前没有已授权的工作区目录。"_ustr);
        return;
    }
    perms.revokeAllDirectories();
    m_xStatusLabel->set_label(u"已清空全部工作区目录授权。"_ustr);
    ReloadWorkspaceList();
}

IMPL_LINK_NOARG(OptAiTabPage, OnWorkspaceClearSessionClicked, weld::Button&, void)
{
    kqoffice::ai::control::PermissionCenter perms;
    const sal_Int32 before = perms.sessionRiskGrantCount();
    perms.clearSessionRiskGrants();
    m_xStatusLabel->set_label(
        u"已清空本轮写删授权（"_ustr + OUString::number(before)
        + u" 条）。下次删除/覆盖仍会二次确认。"_ustr);
    ReloadWorkspaceList();
}

IMPL_LINK_NOARG(OptAiTabPage, OnNetworkRevokeClicked, weld::Button&, void)
{
    kqoffice::ai::control::PermissionCenter perms;
    if (perms.stateOf(kqoffice::ai::control::CapabilityPermission::NetworkEgress)
        != kqoffice::ai::control::PermissionState::Granted)
    {
        m_xStatusLabel->set_label(u"网络外发当前未授权（默认关闭）。"_ustr);
        ReloadWorkspaceList();
        return;
    }
    perms.revoke(kqoffice::ai::control::CapabilityPermission::NetworkEgress);
    m_xStatusLabel->set_label(u"已关闭网络授权 · 外发需重新确认并披露提供方与范围。"_ustr);
    ReloadWorkspaceList();
}

IMPL_LINK_NOARG(OptAiTabPage, OnScenarioListChanged, weld::TreeView&, void)
{
    const OUString id = SelectedScenarioId();
    if (id.isEmpty())
        return;
    if (const DocumentAIScenario* p = DocumentAIScenarioStore::find(m_aScenarioCatalog, id))
        FillScenarioForm(*p);
}

IMPL_LINK_NOARG(OptAiTabPage, OnScenarioNewClicked, weld::Button&, void)
{
    ClearScenarioForm();
    m_xScId->set_text(u"custom-"_ustr
                      + OUString::number(static_cast<sal_Int32>(m_aScenarioCatalog.items.size() + 1)));
    m_xScTitle->set_text(u"新方案"_ustr);
    m_xStatusLabel->set_label(u"已清空表单：填写后点「保存方案」。"_ustr);
}

IMPL_LINK_NOARG(OptAiTabPage, OnScenarioSaveClicked, weld::Button&, void)
{
    DocumentAIScenario s = ReadScenarioForm();
    if (s.id.isEmpty() || s.titleZh.isEmpty())
    {
        m_xStatusLabel->set_label(u"保存失败：ID 与标题必填。"_ustr);
        return;
    }
    if (s.promptTemplate.isEmpty())
    {
        m_xStatusLabel->set_label(u"保存失败：提示词不能为空。"_ustr);
        return;
    }
    DocumentAIScenarioStore::upsert(m_aScenarioCatalog, s);
    if (!DocumentAIScenarioStore::save(m_aScenarioCatalog))
    {
        m_xStatusLabel->set_label(u"保存失败：无法写入方案配置文件。"_ustr);
        return;
    }
    ReloadScenarioList(s.id);
    m_xStatusLabel->set_label(u"方案已保存："_ustr + s.titleZh
                              + (s.options.showAsButton ? u"（侧栏可执行）"_ustr : u" （已隐藏按钮）"_ustr));
}

IMPL_LINK_NOARG(OptAiTabPage, OnScenarioDeleteClicked, weld::Button&, void)
{
    const OUString id = m_xScId->get_text().trim();
    if (id.isEmpty())
        return;
    if (!DocumentAIScenarioStore::removeById(m_aScenarioCatalog, id))
    {
        m_xStatusLabel->set_label(u"删除失败：未找到方案。"_ustr);
        return;
    }
    DocumentAIScenarioStore::save(m_aScenarioCatalog);
    ReloadScenarioList();
    m_xStatusLabel->set_label(
        u"已删除/停用方案（内置方案仅停用并隐藏按钮，可再启用）。"_ustr);
}

IMPL_LINK_NOARG(OptAiTabPage, OnScenarioResetClicked, weld::Button&, void)
{
    m_aScenarioCatalog.schemaVersion = u"v1-scenarios"_ustr;
    m_aScenarioCatalog.items = DocumentAIScenarioStore::builtinDefaults();
    DocumentAIScenarioStore::save(m_aScenarioCatalog);
    ReloadScenarioList();
    m_xStatusLabel->set_label(u"已恢复全部内置默认方案（用户自定义已清除）。"_ustr);
}

IMPL_LINK_NOARG(OptAiTabPage, OnScenarioUpClicked, weld::Button&, void)
{
    const OUString id = SelectedScenarioId();
    if (id.isEmpty())
        return;
    // Persist form edits first so we don't lose field state
    DocumentAIScenario form = ReadScenarioForm();
    if (!form.id.isEmpty())
        DocumentAIScenarioStore::upsert(m_aScenarioCatalog, form);
    if (!DocumentAIScenarioStore::moveUp(m_aScenarioCatalog, id))
    {
        m_xStatusLabel->set_label(u"已在最前，无法上移。"_ustr);
        return;
    }
    DocumentAIScenarioStore::save(m_aScenarioCatalog);
    ReloadScenarioList(id);
    m_xStatusLabel->set_label(u"已上移方案顺序（侧栏按钮靠前显示）。"_ustr);
}

IMPL_LINK_NOARG(OptAiTabPage, OnScenarioDownClicked, weld::Button&, void)
{
    const OUString id = SelectedScenarioId();
    if (id.isEmpty())
        return;
    DocumentAIScenario form = ReadScenarioForm();
    if (!form.id.isEmpty())
        DocumentAIScenarioStore::upsert(m_aScenarioCatalog, form);
    if (!DocumentAIScenarioStore::moveDown(m_aScenarioCatalog, id))
    {
        m_xStatusLabel->set_label(u"已在最后，无法下移。"_ustr);
        return;
    }
    DocumentAIScenarioStore::save(m_aScenarioCatalog);
    ReloadScenarioList(id);
    m_xStatusLabel->set_label(u"已下移方案顺序。"_ustr);
}
