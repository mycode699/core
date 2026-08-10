/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <dispatch/AIInputDispatcher.hxx>
#include <dispatch/AIInlineEditDispatcher.hxx>
#include <dispatch/KqNotebookDispatcher.hxx>

#include <DocumentAIInputPrefs.hxx>
#include <DocumentAIScreenCapture.hxx>
#include <DocumentAIVoiceInput.hxx>
#include <AgentChatSelectionCapture.hxx>
#include <AiPaths.hxx>
#include <AiResourceEnvelope.hxx>
#include <MembershipClient.hxx>
#include <VaultManager.hxx>

#include "../sidebar/AIChatPanel.hxx"

#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/XDispatch.hpp>
#include <com/sun/star/frame/XDispatchProvider.hpp>
#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/util/URL.hpp>
#include <com/sun/star/util/URLTransformer.hpp>
#include <comphelper/processfactory.hxx>
#include <osl/file.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <rtl/ustring.hxx>
#include <sfx2/viewfrm.hxx>
#include <vcl/svapp.hxx>
#include <vcl/vclenum.hxx>
#include <vcl/weld/MessageDialog.hxx>
#include <vcl/weld/Builder.hxx>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

using namespace kqoffice::ai::chat;

namespace sfx2
{
namespace
{
bool isMembershipSlash(const OUString& t)
{
    return t.startsWith(u"/quota"_ustr) || t.startsWith(u"/会员额度"_ustr) || t == u"/额度"_ustr
           || t.startsWith(u"/checkin"_ustr) || t.startsWith(u"/签到"_ustr)
           || t.startsWith(u"/rush"_ustr) || t.startsWith(u"/抢包"_ustr)
           || t.startsWith(u"/加油包"_ustr);
}

OUString membershipActionFromSlash(const OUString& t)
{
    if (t.startsWith(u"/checkin"_ustr) || t.startsWith(u"/签到"_ustr))
        return u"checkin"_ustr;
    if (t.startsWith(u"/rush"_ustr) || t.startsWith(u"/抢包"_ustr))
        return u"rush_grab"_ustr;
    return u"status"_ustr;
}

OUString membershipInjectPath()
{
    const OUString cfg = kqoffice::ai::kqofficeAiConfigDir();
    if (cfg.isEmpty())
        return {};
    return cfg + u"/pending-prompt-inject"_ustr;
}

OUString membershipResultPath()
{
    const OUString cfg = kqoffice::ai::kqofficeAiConfigDir();
    if (cfg.isEmpty())
        return {};
    return cfg + u"/last-membership-result.txt"_ustr;
}

bool writeUtf8File(const OUString& rSysPath, const OUString& rText)
{
    const OUString parent = kqoffice::ai::kqofficeParentDir(rSysPath);
    if (!parent.isEmpty())
    {
        OUString dirUrl;
        if (osl::FileBase::getFileURLFromSystemPath(parent, dirUrl) == osl::FileBase::E_None)
            osl::Directory::createPath(dirUrl);
    }
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSysPath, url) != osl::FileBase::E_None)
        return false;
    osl::File f(url);
    osl::FileBase::RC e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
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

OUString readAndMaybeRemoveInject(bool bRemoveIfMembership)
{
    const OUString path = membershipInjectPath();
    if (path.isEmpty())
        return {};
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(path, url) != osl::FileBase::E_None)
        return {};
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return {};
    sal_uInt64 sz = 0;
    f.getSize(sz);
    if (sz == 0 || sz > 64 * 1024)
    {
        f.close();
        if (bRemoveIfMembership)
            osl::File::remove(url);
        return {};
    }
    std::vector<char> buf(static_cast<size_t>(sz));
    sal_uInt64 n = 0;
    f.read(buf.data(), sz, n);
    f.close();
    if (n == 0)
        return {};
    const OUString text
        = OUString::fromUtf8(std::string_view(buf.data(), static_cast<size_t>(n))).trim();
    if (bRemoveIfMembership && isMembershipSlash(text))
        osl::File::remove(url);
    return text;
}

// Last captured screenshot path for panel / prompt injection via env bridge
// (panel polls pending injection file — simple & cross-module).
void queuePromptInjection(const OUString& rText)
{
    (void)writeUtf8File(membershipInjectPath(), rText);
}
} // namespace

#if defined(MACOSX)
extern "C" void kqoffice_ai_register_global_hotkeys();
extern "C" void kqoffice_ai_reload_global_hotkeys();
#endif

AIInputDispatcher::AIInputDispatcher()
    : m_aMembershipInjectPoll("KqMembershipInjectPoll")
{
    // Background poll: membership slash inject works even when AI panel not open.
    // Safe — MembershipClient never mutates the main document.
    using kqoffice::ai::control::AiResourceEnvelope;
    m_aMembershipInjectPoll.SetTimeout(AiResourceEnvelope::membershipInjectPollMs());
    m_aMembershipInjectPoll.SetInvokeHandler(
        LINK(this, AIInputDispatcher, OnMembershipInjectPoll));
    m_aMembershipInjectPoll.Start();

    // Install-default 资料盘 + folder permission seed (WPS/Quark download-path style).
    // Idempotent + process-cached; no UI modal.
    try
    {
        (void)kqoffice::ai::vault::VaultManager::ensureInstallDefaults();
    }
    catch (...)
    {
    }
}

void AIInputDispatcher::PollMembershipInject()
{
    using kqoffice::ai::control::AiResourceEnvelope;
    // Adaptive cadence: stretch when idle (resource envelope).
    m_aMembershipInjectPoll.SetTimeout(AiResourceEnvelope::membershipInjectPollMs());

    // Cheap skip: no inject file → no disk read of membership secrets path churn.
    if (!AiResourceEnvelope::pendingInjectLikelyPresent())
        return;

    // If full panel is active it owns inject + auto-send for membership.
    if (sfx2::sidebar::AIChatPanel::GetActivePanel())
        return;

    const OUString text = readAndMaybeRemoveInject(/*bRemoveIfMembership*/ true);
    if (text.isEmpty() || !isMembershipSlash(text))
        return;

    const OUString action = membershipActionFromSlash(text);
    const kqoffice::ai::MembershipBoostResult br
        = kqoffice::ai::membershipBoostAction(action);

    OUStringBuffer out;
    out.append(u"ok="_ustr);
    out.append(br.ok ? u"1"_ustr : u"0"_ustr);
    out.append(u"\nslash="_ustr);
    out.append(text);
    out.append(u"\naction="_ustr);
    out.append(action);
    out.append(u"\npacks="_ustr);
    out.append(br.packs);
    out.append(u"\ndayFastRem="_ustr);
    out.append(br.dayFastRem);
    out.append(u"\nemail="_ustr);
    out.append(br.email);
    out.append(u"\ncode="_ustr);
    out.append(br.rawCode);
    out.append(u"\nmessage=\n"_ustr);
    out.append(br.messageZh);
    out.append(u"\n"_ustr);
    (void)writeUtf8File(membershipResultPath(), out.makeStringAndClear());
}

IMPL_LINK_NOARG(AIInputDispatcher, OnMembershipInjectPoll, Timer*, void)
{
    PollMembershipInject();
}

AIInputDispatcher& AIInputDispatcher::Get()
{
    static AIInputDispatcher a;
    static bool bPrefsHook = false;
    if (!bPrefsHook)
    {
        bPrefsHook = true;
        DocumentAIInputPrefs::setOnSavedCallback(+[]() {
#if defined(MACOSX)
            kqoffice_ai_reload_global_hotkeys();
#endif
            // Inline auto-ghost on/off + idle ms without restart.
            AIInlineEditDispatcher::Get().EnsureAutoGhostWatch();
        });
    }
#if defined(MACOSX)
    // Lazy-register global hotkeys once (Spokenly hold-to-talk).
    static bool bHotkeys = false;
    if (!bHotkeys)
    {
        bHotkeys = true;
        kqoffice_ai_register_global_hotkeys();
    }
#endif
    // Always (re)arm auto-ghost watch from prefs (default off; no-op when disabled).
    AIInlineEditDispatcher::Get().EnsureAutoGhostWatch();
    return a;
}

void AIInputDispatcher::ReloadHotkeys()
{
#if defined(MACOSX)
    kqoffice_ai_reload_global_hotkeys();
#endif
}

void AIInputDispatcher::openAiDeck(SfxViewFrame* pFrame)
{
    if (!pFrame)
        pFrame = SfxViewFrame::Current();
    if (!pFrame)
        return;
    try
    {
        css::util::URL aUrl;
        aUrl.Complete = u".uno:SidebarDeck.AIChatDeck"_ustr;
        auto xTrans = css::util::URLTransformer::create(comphelper::getProcessComponentContext());
        if (xTrans.is())
            xTrans->parseStrict(aUrl);
        css::uno::Reference<css::frame::XDispatchProvider> xProv(
            pFrame->GetFrame().GetFrameInterface(), css::uno::UNO_QUERY);
        if (!xProv.is())
            return;
        auto xDisp = xProv->queryDispatch(aUrl, u"_self"_ustr, 0);
        if (xDisp.is())
            xDisp->dispatch(aUrl, {});
    }
    catch (...)
    {
    }
}

void AIInputDispatcher::injectIntoAiPrompt(const OUString& rText, bool bOpenDeck,
                                           SfxViewFrame* pFrame)
{
    if (rText.isEmpty())
        return;
    queuePromptInjection(rText);
    if (bOpenDeck)
        openAiDeck(pFrame);
}

namespace
{
OUString documentAssistSeed(const OUString& rIntent)
{
    if (rIntent == u"outline"_ustr || rIntent == u"structure"_ustr)
        return u"【文档结构 · M-W1 可写回 · 主文档不自动改】"
               "请基于当前文档骨架（document-tools 的 para:N）输出：\n"
               "1) 可读大纲（层级标题 + 每章一句话目的）\n"
               "2) 结构问题（冗余/跳跃/缺节）\n"
               "3) **可批准写回块**（机器解析，必须）：\n"
               "===可圈大纲写回===\n"
               "para:段落号|H1|建议标题文案\n"
               "para:段落号|H2|建议标题文案\n"
               "（H1/H2/H3 对应标题 1/2/3 样式；只列需要改层级的段落；"
               "段落号必须来自骨架，勿臆造）\n"
               "写回须用户点「批准写回」后才改主文档。"_ustr;
    if (rIntent == u"proofread"_ustr || rIntent == u"review"_ustr)
        return u"【全文审阅 · M-W1 可写回 · 主文档不自动改】"
               "请审阅当前文档并输出：\n"
               "A) 清单：①事实/数据风险 ②语气与得体 ③冗余与歧义 ④结构建议；"
               "每条给位置线索 + 问题说明。\n"
               "B) **可批准修复块**（机器解析，有把握再写）：\n"
               "===可圈审阅修复===\n"
               "FIX|原文片段|建议改写\n"
               "（原文须尽量与文档一致，便于定位；最多 8 条）\n"
               "不要声称已改主文档；须用户批准后才写回。"_ustr;
    if (rIntent == u"continue"_ustr)
        return u"【续写 · 写回须批准】"
               "请接在当前选区末尾（无选区则接文档逻辑结尾）续写 2–4 段，"
               "保持语气与事实一致。输出完整续写正文；"
               "若给写回体请用可圈 AI 标准提议格式，主文档须用户批准后才改。"_ustr;
    if (rIntent == u"doc-summary"_ustr || rIntent == u"summary"_ustr)
        return u"【全文总结 · 咨询】"
               "请基于当前文档骨架与必要读块，输出："
               "一句话摘要；3–7 条要点；未决问题/行动项。"
               "仅依据文档内容，勿编造。"_ustr;
    return u"请协助处理当前文档（说明你的建议；改主文档须用户批准）："_ustr;
}
}

void AIInputDispatcher::SendSelectionToAi(SfxViewFrame* pFrame, const OUString& rIntentId)
{
    openAiDeck(pFrame);
    // Allow panel to construct and register as active.
    if (Application::IsInMain())
    {
        for (int i = 0; i < 8; ++i)
            Application::Reschedule();
    }

    OUString intent = rIntentId.trim().toAsciiLowerCase();
    if (intent.isEmpty())
        intent = u"formal"_ustr;

    // Document-level intents: no selection required.
    if (intent == u"outline"_ustr || intent == u"structure"_ustr || intent == u"proofread"_ustr
        || intent == u"review"_ustr || intent == u"continue"_ustr || intent == u"doc-summary"_ustr
        || intent == u"summary"_ustr)
    {
        RunDocumentAssist(pFrame, intent);
        return;
    }

    OUString seed;
    if (intent == u"formal"_ustr || intent == u"polish"_ustr)
    {
        intent = u"formal"_ustr;
        seed = u"请将以下内容改成正式、得体的书面语气（公文/商务均可），保留关键事实与数据，输出完整改写："_ustr;
    }
    else if (intent == u"shorten"_ustr)
        seed = u"请精简以下内容，保留要点："_ustr;
    else if (intent == u"expand"_ustr)
        seed = u"请扩写并补充细节："_ustr;
    else if (intent == u"summarize"_ustr)
        seed = u"请总结要点："_ustr;
    else
    {
        intent = u"rewrite"_ustr;
        seed = u"请改写得更清晰专业："_ustr;
    }

    if (auto* pPanel = sfx2::sidebar::AIChatPanel::GetActivePanel())
    {
        const auto sel = kqoffice::ai::chat::AgentChatSelectionCapture::captureCurrent();
        if (sel.length <= 0)
        {
            // No selection: open panel with hint, do not auto-submit empty.
            injectIntoAiPrompt(u"请先在文档中选中文字，再使用「发给 AI」；"
                               "或用「大纲 / 审阅 / 续写 / 全文总结」做整篇协助。"_ustr,
                               false, pFrame);
            return;
        }
        pPanel->RunQuickIntent(intent, seed);
        return;
    }

    // Panel not yet active: queue seed prompt for inject poll.
    injectIntoAiPrompt(seed, false, pFrame);
}

void AIInputDispatcher::RunDocumentAssist(SfxViewFrame* pFrame, const OUString& rIntentId)
{
    openAiDeck(pFrame);
    if (Application::IsInMain())
    {
        for (int i = 0; i < 10; ++i)
            Application::Reschedule();
    }

    OUString intent = rIntentId.trim().toAsciiLowerCase();
    if (intent.isEmpty())
        intent = u"outline"_ustr;
    if (intent == u"structure"_ustr)
        intent = u"outline"_ustr;
    if (intent == u"review"_ustr)
        intent = u"proofread"_ustr;
    if (intent == u"summary"_ustr)
        intent = u"doc-summary"_ustr;

    const OUString seed = documentAssistSeed(intent);
    if (auto* pPanel = sfx2::sidebar::AIChatPanel::GetActivePanel())
    {
        // outline/proofread/doc-summary → plan or summarize capability; continue → expand
        OUString cap = intent;
        if (intent == u"outline"_ustr)
            cap = u"plan"_ustr;
        else if (intent == u"proofread"_ustr)
            cap = u"review"_ustr;
        else if (intent == u"doc-summary"_ustr)
            cap = u"summarize"_ustr;
        else if (intent == u"continue"_ustr)
            cap = u"expand"_ustr;
        pPanel->RunQuickIntent(cap, seed);
        return;
    }
    injectIntoAiPrompt(seed, true, pFrame);
}

namespace
{
OUString calcAssistSeed(const OUString& rIntent)
{
    if (rIntent == u"formula"_ustr)
        return u"【表格公式 · M-C1 可写回 · 须批准】"
               "请根据当前选区（列语义/表头）生成公式：\n"
               "1) 简短中文说明（适用列、空值处理）\n"
               "2) **可批准写回块**（机器解析）：\n"
               "===可圈公式写回===\n"
               "cell:目标格|=公式|一行说明\n"
               "（目标格优先：活动单元格；若选区为数据列，汇总写到旁列或列底下一格；"
               "公式必须 = 开头；可多行多格）\n"
               "不要声称已改表；写入须用户批准。"_ustr;
    if (rIntent == u"clean"_ustr || rIntent == u"cleanse"_ustr)
        return u"【数据清洗 · M-C1 旁列写回 · 须批准】"
               "请检查选区，输出：\n"
               "A) 清单：①空值 ②重复 ③类型混杂 ④异常；每条位置线索+问题。\n"
               "B) **可批准清洗公式**（写入旁列，不覆盖原列）：\n"
               "===可圈清洗写回===\n"
               "cell:旁列格|=TRIM(A2)|去空格示例\n"
               "（旁列 = 选区右侧一列对应行；可多行；禁止直接覆盖源列）\n"
               "主表不自动改；须批准后写回。"_ustr;
    if (rIntent == u"interpret"_ustr || rIntent == u"insight"_ustr)
        return u"【数据解读 · 咨询】"
               "请解读当前选区/表数据：关键结论 3–7 条、趋势/对比、风险或异常、"
               "可跟进的分析问题。仅依据表内数据，勿编造。"
               "若需派生指标，可另附 ===可圈公式写回=== 块（仍须批准）。"_ustr;
    if (rIntent == u"aggregate"_ustr || rIntent == u"summary"_ustr)
        return u"【汇总 · M-C1 可写回 · 须批准】"
               "请对选区给出合计/平均/计数等：\n"
               "===可圈公式写回===\n"
               "cell:目标格|=SUM(...)/AVERAGE(...)/COUNT(...)\n"
               "随后说明口径。目标格优先旁列或选区下方。写入须用户批准。"_ustr;
    return u"请协助分析当前表格（改表须用户批准）："_ustr;
}
}

void AIInputDispatcher::RunCalcAssist(SfxViewFrame* pFrame, const OUString& rIntentId)
{
    openAiDeck(pFrame);
    if (Application::IsInMain())
    {
        for (int i = 0; i < 10; ++i)
            Application::Reschedule();
    }

    OUString intent = rIntentId.trim().toAsciiLowerCase();
    if (intent.isEmpty())
        intent = u"formula"_ustr;
    if (intent == u"cleanse"_ustr)
        intent = u"clean"_ustr;
    if (intent == u"insight"_ustr)
        intent = u"interpret"_ustr;
    if (intent == u"summary"_ustr)
        intent = u"aggregate"_ustr;

    const OUString seed = calcAssistSeed(intent);
    if (auto* pPanel = sfx2::sidebar::AIChatPanel::GetActivePanel())
    {
        // Prefer capability hints that keep offline policy allow-list happy.
        OUString cap = u"chat"_ustr;
        if (intent == u"formula"_ustr || intent == u"aggregate"_ustr)
            cap = u"chat"_ustr;
        else if (intent == u"clean"_ustr)
            cap = u"extract"_ustr;
        else if (intent == u"interpret"_ustr)
            cap = u"summarize"_ustr;
        pPanel->RunQuickIntent(cap, seed);
        return;
    }
    injectIntoAiPrompt(seed, true, pFrame);
}

namespace
{
OUString impressAssistSeed(const OUString& rIntent)
{
    if (rIntent == u"outline"_ustr || rIntent == u"deck"_ustr)
        return u"【演示大纲 · M-I0 受控成片 · 须批准】"
               "请基于当前演示/选区要点输出多页大纲（禁止黑盒整片乱改）：\n"
               "每页：\n## N. 标题\n- 要点\n讲稿：30–60 秒口播\n"
               "版式：标题内容|标题页|分栏（可选）\n"
               "写回须用户批准。"_ustr;
    if (rIntent == u"notes"_ustr || rIntent == u"speaker"_ustr || rIntent == u"讲稿"_ustr)
        return u"【演示讲稿 · M-I0 仅写备注页 · 须批准】"
               "请为各页写 30–60 秒口播讲稿，不改幻灯标题与要点。\n"
               "输出机器块：\n"
               "===可圈讲稿写回===\n"
               "slide:1|讲稿|口播全文\n"
               "slide:2|讲稿|…\n"
               "（slide 编号与当前文稿一致；批准后只写备注页）\n"
               "主文档不自动改。"_ustr;
    if (rIntent == u"page"_ustr || rIntent == u"slide"_ustr)
        return u"【本页改写 · 须批准】"
               "只改当前页：第一行标题，其后每行要点；可附 版式：/ 讲稿：/ 配图：。"
               "保持事实；写回须用户批准。"_ustr;
    return u"请协助处理当前演示（写回须批准；禁止黑盒整片）："_ustr;
}
}

void AIInputDispatcher::RunImpressAssist(SfxViewFrame* pFrame, const OUString& rIntentId)
{
    openAiDeck(pFrame);
    if (Application::IsInMain())
    {
        for (int i = 0; i < 10; ++i)
            Application::Reschedule();
    }

    OUString intent = rIntentId.trim().toAsciiLowerCase();
    if (intent.isEmpty())
        intent = u"outline"_ustr;
    if (intent == u"deck"_ustr || intent == u"structure"_ustr)
        intent = u"outline"_ustr;
    if (intent == u"speaker"_ustr || intent == u"讲稿"_ustr)
        intent = u"notes"_ustr;
    if (intent == u"slide"_ustr)
        intent = u"page"_ustr;

    const OUString seed = impressAssistSeed(intent);
    if (auto* pPanel = sfx2::sidebar::AIChatPanel::GetActivePanel())
    {
        OUString cap = u"plan"_ustr;
        if (intent == u"notes"_ustr)
            cap = u"chat"_ustr;
        else if (intent == u"page"_ustr)
            cap = u"rewrite"_ustr;
        pPanel->RunQuickIntent(cap, seed);
        return;
    }
    injectIntoAiPrompt(seed, true, pFrame);
}

void AIInputDispatcher::TriggerVoice(SfxViewFrame* pFrame)
{
    const auto prefs = DocumentAIInputPrefs::load();
    if (!prefs.voiceEnabled)
    {
        // Non-modal: silent skip for hotkey UX (Spokenly never blocks with dialogs)
        return;
    }

    VoiceCaptureResult cap;
    if (prefs.voicePushToTalk || prefs.voiceBackend == VoiceBackend::PushToTalkRecord)
        cap = DocumentAIVoiceInput::togglePushToTalk();
    else
        cap = DocumentAIVoiceInput::captureOnce();

    if (cap.success && !cap.text.isEmpty())
    {
        if (KqNotebookDispatcher::TryAcceptVoiceText(cap.text))
            return;
        injectIntoAiPrompt(cap.text, true, pFrame);
    }
    else if (cap.listening)
    {
        // Listening: open AI deck lightly; no modal
        if (prefs.voiceShowHud)
            openAiDeck(pFrame);
    }
    else if (!cap.message.isEmpty() && cap.source == u"error"_ustr)
    {
        // Only hard errors — avoid Spokenly-breaking modal spam
        weld::Window* pParent = Application::GetDefDialogParent();
        std::unique_ptr<weld::MessageDialog> xBox(Application::CreateMessageDialog(
            pParent, VclMessageType::Warning, VclButtonsType::Ok, cap.message));
        xBox->run();
    }
}

void AIInputDispatcher::TriggerVoicePress(SfxViewFrame* pFrame)
{
    const auto prefs = DocumentAIInputPrefs::load();
    if (!prefs.voiceEnabled)
        return;
    if (!prefs.voiceHoldToTalk)
    {
        TriggerVoice(pFrame);
        return;
    }
    const VoiceCaptureResult cap = DocumentAIVoiceInput::beginPushToTalk();
    if (cap.listening && prefs.voiceShowHud)
    {
        // Soft open deck so user sees “listening” state
        openAiDeck(pFrame);
    }
    else if (cap.success && !cap.text.isEmpty())
    {
        if (!KqNotebookDispatcher::TryAcceptVoiceText(cap.text))
            injectIntoAiPrompt(cap.text, true, pFrame);
    }
}

void AIInputDispatcher::TriggerVoiceRelease(SfxViewFrame* pFrame)
{
    const auto prefs = DocumentAIInputPrefs::load();
    if (!prefs.voiceEnabled || !prefs.voiceHoldToTalk)
        return;
    if (!DocumentAIVoiceInput::isListening())
        return;
    const VoiceCaptureResult cap = DocumentAIVoiceInput::endPushToTalk();
    if (cap.success && !cap.text.isEmpty())
    {
        if (!KqNotebookDispatcher::TryAcceptVoiceText(cap.text))
            injectIntoAiPrompt(cap.text, true, pFrame);
    }
}

void AIInputDispatcher::TriggerScreenshot(SfxViewFrame* pFrame)
{
    TriggerScreenshotRegion(pFrame); // WeChat default: region
}

void AIInputDispatcher::TriggerScreenshotRegion(SfxViewFrame* pFrame)
{
    const auto prefs = DocumentAIInputPrefs::load();
    if (!prefs.screenshotEnabled)
    {
        weld::Window* pParent = Application::GetDefDialogParent();
        std::unique_ptr<weld::MessageDialog> xBox(Application::CreateMessageDialog(
            pParent, VclMessageType::Info, VclButtonsType::Ok,
            u"截图已关闭。请到「工具 → 选项 → 可圈 AI」开启。"_ustr));
        xBox->run();
        return;
    }
    // Yield UI so interactive capture can grab screen
    Application::Reschedule(true);
    const ScreenCaptureResult shot
        = DocumentAIScreenCapture::capture(ScreenshotMode::Region);
    if (shot.success && prefs.screenshotAutoAttachChat)
    {
        injectIntoAiPrompt(shot.promptAttachment + u"\n请结合截图说明："_ustr,
                           prefs.screenshotOpenAiPanel, pFrame);
    }
    else if (!shot.success && !shot.message.isEmpty())
    {
        // Cancel is silent-enough; only show hard errors
        if (shot.message.indexOf(u"取消"_ustr) < 0)
        {
            weld::Window* pParent = Application::GetDefDialogParent();
            std::unique_ptr<weld::MessageDialog> xBox(Application::CreateMessageDialog(
                pParent, VclMessageType::Warning, VclButtonsType::Ok, shot.message));
            xBox->run();
        }
    }
}

void AIInputDispatcher::TriggerScreenshotWindow(SfxViewFrame* pFrame)
{
    const auto prefs = DocumentAIInputPrefs::load();
    if (!prefs.screenshotEnabled)
        return;
    Application::Reschedule(true);
    const ScreenCaptureResult shot
        = DocumentAIScreenCapture::capture(ScreenshotMode::Window);
    if (shot.success && prefs.screenshotAutoAttachChat)
        injectIntoAiPrompt(shot.promptAttachment + u"\n请结合截图说明："_ustr,
                           prefs.screenshotOpenAiPanel, pFrame);
}

void AIInputDispatcher::TriggerScreenshotFull(SfxViewFrame* pFrame)
{
    const auto prefs = DocumentAIInputPrefs::load();
    if (!prefs.screenshotEnabled)
        return;
    Application::Reschedule(true);
    const ScreenCaptureResult shot
        = DocumentAIScreenCapture::capture(ScreenshotMode::Fullscreen);
    if (shot.success && prefs.screenshotAutoAttachChat)
        injectIntoAiPrompt(shot.promptAttachment + u"\n请结合截图说明："_ustr,
                           prefs.screenshotOpenAiPanel, pFrame);
}

} // namespace sfx2

// C ABI for systray / global hotkey (same library; no hard module edges).
extern "C" SAL_DLLPUBLIC_EXPORT void kqoffice_ai_trigger_voice()
{
    SolarMutexGuard aGuard;
    sfx2::AIInputDispatcher::Get().TriggerVoice(SfxViewFrame::Current());
}

extern "C" SAL_DLLPUBLIC_EXPORT void kqoffice_ai_trigger_voice_press()
{
    SolarMutexGuard aGuard;
    sfx2::AIInputDispatcher::Get().TriggerVoicePress(SfxViewFrame::Current());
}

extern "C" SAL_DLLPUBLIC_EXPORT void kqoffice_ai_trigger_voice_release()
{
    SolarMutexGuard aGuard;
    sfx2::AIInputDispatcher::Get().TriggerVoiceRelease(SfxViewFrame::Current());
}

extern "C" SAL_DLLPUBLIC_EXPORT void kqoffice_ai_trigger_screenshot_region()
{
    SolarMutexGuard aGuard;
    sfx2::AIInputDispatcher::Get().TriggerScreenshotRegion(SfxViewFrame::Current());
}

extern "C" SAL_DLLPUBLIC_EXPORT void kqoffice_ai_trigger_screenshot_window()
{
    SolarMutexGuard aGuard;
    sfx2::AIInputDispatcher::Get().TriggerScreenshotWindow(SfxViewFrame::Current());
}

/// Select-to-act: open AI panel and run intent on current document selection.
/// pIntent may be null → default formal tone.
extern "C" SAL_DLLPUBLIC_EXPORT void kqoffice_ai_send_selection_to_ai(const sal_Unicode* pIntent,
                                                                      sal_Int32 nIntentLen)
{
    SolarMutexGuard aGuard;
    OUString intent;
    if (pIntent && nIntentLen > 0)
        intent = OUString(pIntent, nIntentLen);
    sfx2::AIInputDispatcher::Get().SendSelectionToAi(SfxViewFrame::Current(), intent);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
