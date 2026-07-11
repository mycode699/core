/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Cursor-style Ctrl/Cmd+K inline AI edit + Tab-accept + light-slot complete.
 */

#include <dispatch/AIInlineEditDispatcher.hxx>

#include <AgentChatDiffExtractor.hxx>
#include <AgentChatSelectionCapture.hxx>
#include <DocumentAIApply.hxx>
#include <DocumentAIContext.hxx>
#include <DocumentAIScenarioStore.hxx>
#include <ModelRoles.hxx>

#include <com/sun/star/ai/ProviderRequest.hpp>
#include <com/sun/star/ai/ProviderResponse.hpp>
#include <com/sun/star/ai/XProvider.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <comphelper/processfactory.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>
#include <sfx2/viewfrm.hxx>
#include <tools/gen.hxx>
#include <tools/link.hxx>
#include <vcl/event.hxx>
#include <vcl/svapp.hxx>
#include <vcl/timer.hxx>
#include <vcl/window.hxx>
#include <vcl/weld/Builder.hxx>
#include <vcl/weld/Entry.hxx>
#include <vcl/weld/Popover.hxx>
#include <vcl/weld/TextView.hxx>
#include <vcl/weld/TreeView.hxx>
#include <vcl/weld/weld.hxx>
#include <vcl/weld/weldutils.hxx>

#include <memory>
#include <vector>

namespace sfx2
{
namespace
{
constexpr OUStringLiteral V2_PROVIDER = u"com.sun.star.ai.Provider";
constexpr sal_Int32 PROVIDER_TIMEOUT_MS = 45000;

struct SuggestItem
{
    OUString id;
    OUString label;
    OUString instruction;
    OUString capability;
};

enum class InlineMode
{
    Edit, // Ctrl+K: instruct + rewrite/complete
    Complete // Ctrl+Alt+Space: auto light-slot continuation
};

class AIInlineEditPopover final
{
public:
    AIInlineEditPopover(weld::Widget* pParent, const tools::Rectangle& rAnchor, InlineMode eMode)
        : m_pAnchor(pParent)
        , m_aAnchor(rAnchor)
        , m_eMode(eMode)
        , m_xBuilder(Application::CreateBuilder(pParent, u"sfx/ui/aiinlineedit.ui"_ustr))
        , m_xPopover(m_xBuilder->weld_popover(u"AIInlineEdit"_ustr))
        , m_xContext(m_xBuilder->weld_label(u"context_label"_ustr))
        , m_xInstruction(m_xBuilder->weld_entry(u"instruction_entry"_ustr))
        , m_xGhostBanner(m_xBuilder->weld_label(u"ghost_banner"_ustr))
        , m_xSuggest(m_xBuilder->weld_tree_view(u"suggest_view"_ustr))
        , m_xSuggestScroll(m_xBuilder->weld_widget(u"suggest_scroll"_ustr))
        , m_xPreview(m_xBuilder->weld_text_view(u"preview_view"_ustr))
        , m_xPreviewScroll(m_xBuilder->weld_widget(u"preview_scroll"_ustr))
        , m_xChipRow(m_xBuilder->weld_widget(u"chip_row"_ustr))
        , m_xStatus(m_xBuilder->weld_label(u"status_label"_ustr))
        , m_xGenerate(m_xBuilder->weld_button(u"generate_btn"_ustr))
        , m_xAccept(m_xBuilder->weld_button(u"accept_btn"_ustr))
        , m_xReject(m_xBuilder->weld_button(u"reject_btn"_ustr))
        , m_xChipPolish(m_xBuilder->weld_button(u"chip_polish"_ustr))
        , m_xChipShorten(m_xBuilder->weld_button(u"chip_shorten"_ustr))
        , m_xChipExpand(m_xBuilder->weld_button(u"chip_expand"_ustr))
        , m_xChipEn(m_xBuilder->weld_button(u"chip_en"_ustr))
        , m_xChipComplete(m_xBuilder->weld_button(u"chip_complete"_ustr))
        , m_aAutoGen("AIInlineAutoGen")
    {
        m_aSel = kqoffice::ai::chat::AgentChatSelectionCapture::captureCurrent();
        // Empty selection → complete path (Cursor ghost-style)
        if (m_aSel.length == 0 || m_eMode == InlineMode::Complete)
            m_eMode = InlineMode::Complete;

        refreshContextLabel();
        buildSuggestCorpus();
        refreshSuggestions(u""_ustr);

        m_xInstruction->connect_changed(LINK(this, AIInlineEditPopover, OnInstructionChanged));
        m_xInstruction->connect_activate(LINK(this, AIInlineEditPopover, OnInstructionActivate));
        m_xInstruction->connect_key_press(LINK(this, AIInlineEditPopover, OnKeyPress));
        m_xSuggest->connect_row_activated(LINK(this, AIInlineEditPopover, OnSuggestActivated));
        m_xSuggest->connect_key_press(LINK(this, AIInlineEditPopover, OnKeyPress));
        m_xPreview->connect_key_press(LINK(this, AIInlineEditPopover, OnKeyPress));
        m_xGenerate->connect_clicked(LINK(this, AIInlineEditPopover, OnGenerateClicked));
        m_xAccept->connect_clicked(LINK(this, AIInlineEditPopover, OnAcceptClicked));
        m_xReject->connect_clicked(LINK(this, AIInlineEditPopover, OnRejectClicked));
        m_xChipPolish->connect_clicked(LINK(this, AIInlineEditPopover, OnChipClicked));
        m_xChipShorten->connect_clicked(LINK(this, AIInlineEditPopover, OnChipClicked));
        m_xChipExpand->connect_clicked(LINK(this, AIInlineEditPopover, OnChipClicked));
        m_xChipEn->connect_clicked(LINK(this, AIInlineEditPopover, OnChipClicked));
        m_xChipComplete->connect_clicked(LINK(this, AIInlineEditPopover, OnChipClicked));
        m_xPopover->connect_closed(LINK(this, AIInlineEditPopover, OnClosed));

        m_xPreview->set_editable(false);
        m_xAccept->set_sensitive(false);
        m_xAccept->set_label(u"应用 (Tab)"_ustr);
        m_xReject->set_label(u"关闭 (Esc)"_ustr);
        configureSurfaceChips();

        if (m_eMode == InlineMode::Complete)
        {
            // Compact ghost UI: gray-style preview only, Tab = accept.
            if (m_xGhostBanner)
                m_xGhostBanner->set_visible(true);
            if (m_xChipRow)
                m_xChipRow->set_visible(false);
            if (m_xSuggestScroll)
                m_xSuggestScroll->set_visible(false);
            m_xInstruction->set_visible(false);
            m_xGenerate->set_visible(false);
            if (m_xPreviewScroll)
                m_xPreviewScroll->set_size_request(-1, 160);
            m_xInstruction->set_text(u"自然续写当前段落，只输出续写正文。"_ustr);
            m_sCapability = u"background"_ustr; // light slot
            m_xStatus->set_label(
                u"👻 幽灵灰字预览 · light 槽生成中… · Tab 写入 · Esc 放弃（批准前不改主文档）"_ustr);
            m_xAccept->set_label(u"Tab 接受"_ustr);
            m_xReject->set_label(u"Esc 放弃"_ustr);
            m_aAutoGen.SetTimeout(180);
            m_aAutoGen.SetInvokeHandler(LINK(this, AIInlineEditPopover, OnAutoGenerate));
        }
        else
        {
            if (m_xGhostBanner)
                m_xGhostBanner->set_visible(false);
            m_xStatus->set_label(
                u"Ctrl/Cmd+K · Enter 生成 · Tab 应用 · Esc 关闭 · 下方可自动补全方案"_ustr);
        }
    }

    void show()
    {
        m_xPopover->popup_at_rect(m_pAnchor, m_aAnchor);
        m_xInstruction->grab_focus();
        if (m_eMode == InlineMode::Complete)
            m_aAutoGen.Start();
    }

    void hide()
    {
        m_aAutoGen.Stop();
        m_xPopover->popdown();
    }

private:
    DECL_LINK(OnInstructionChanged, weld::Entry&, void);
    DECL_LINK(OnInstructionActivate, weld::Entry&, bool);
    DECL_LINK(OnSuggestActivated, weld::TreeView&, bool);
    DECL_LINK(OnGenerateClicked, weld::Button&, void);
    DECL_LINK(OnAcceptClicked, weld::Button&, void);
    DECL_LINK(OnRejectClicked, weld::Button&, void);
    DECL_LINK(OnChipClicked, weld::Button&, void);
    DECL_LINK(OnClosed, weld::Popover&, void);
    DECL_LINK(OnKeyPress, const KeyEvent&, bool);
    DECL_LINK(OnAutoGenerate, Timer*, void);

    void refreshContextLabel();
    void configureSurfaceChips();
    void buildSuggestCorpus();
    void refreshSuggestions(const OUString& rQuery);
    void runChip(const OUString& rInstruction, const OUString& rCapability);
    void generate();
    void updatePreviewDisplay();
    bool applyPreview();
    void closeSelf();
    css::ai::ProviderResponse callProvider(const OUString& rPrompt, const OUString& rCapability);

    weld::Widget* m_pAnchor;
    tools::Rectangle m_aAnchor;
    InlineMode m_eMode = InlineMode::Edit;
    std::unique_ptr<weld::Builder> m_xBuilder;
    std::unique_ptr<weld::Popover> m_xPopover;
    std::unique_ptr<weld::Label> m_xContext;
    std::unique_ptr<weld::Entry> m_xInstruction;
    std::unique_ptr<weld::Label> m_xGhostBanner;
    std::unique_ptr<weld::TreeView> m_xSuggest;
    std::unique_ptr<weld::Widget> m_xSuggestScroll;
    std::unique_ptr<weld::TextView> m_xPreview;
    std::unique_ptr<weld::Widget> m_xPreviewScroll;
    std::unique_ptr<weld::Widget> m_xChipRow;
    std::unique_ptr<weld::Label> m_xStatus;
    std::unique_ptr<weld::Button> m_xGenerate;
    std::unique_ptr<weld::Button> m_xAccept;
    std::unique_ptr<weld::Button> m_xReject;
    std::unique_ptr<weld::Button> m_xChipPolish;
    std::unique_ptr<weld::Button> m_xChipShorten;
    std::unique_ptr<weld::Button> m_xChipExpand;
    std::unique_ptr<weld::Button> m_xChipEn;
    std::unique_ptr<weld::Button> m_xChipComplete;
    Timer m_aAutoGen;

    kqoffice::ai::chat::SelectionContext m_aSel;
    std::vector<SuggestItem> m_aCorpus;
    std::vector<SuggestItem> m_aFiltered;
    OUString m_sPreviewText;
    OUString m_sCapability; // empty → derive from mode/selection
    bool m_bHasPreview = false;
};

std::unique_ptr<AIInlineEditPopover> g_pActive;

void AIInlineEditPopover::closeSelf()
{
    hide();
    g_pActive.reset();
}

void AIInlineEditPopover::configureSurfaceChips()
{
    // Reuse 5 chip slots with surface-native actions (Word/Excel/PPT-like).
    const OUString s = m_aSel.surface;
    if (s == u"calc"_ustr)
    {
        m_xChipPolish->set_label(u"公式"_ustr);
        m_xChipShorten->set_label(u"清洗"_ustr);
        m_xChipExpand->set_label(u"汇总"_ustr);
        m_xChipEn->set_label(u"图表"_ustr);
        m_xChipComplete->set_label(u"续写"_ustr);
        m_xInstruction->set_placeholder_text(
            u"表格：写公式 / 清洗 / 汇总 / 图表… 或点芯片（Enter 生成）"_ustr);
    }
    else if (s == u"impress"_ustr)
    {
        m_xChipPolish->set_label(u"本页"_ustr);
        m_xChipShorten->set_label(u"讲稿"_ustr);
        m_xChipExpand->set_label(u"大纲"_ustr);
        m_xChipEn->set_label(u"译英"_ustr);
        m_xChipComplete->set_label(u"续写"_ustr);
        m_xInstruction->set_placeholder_text(
            u"演示：本页改写 / 讲稿 / 大纲成片… 或点芯片（Enter 生成）"_ustr);
    }
    else
    {
        m_xChipPolish->set_label(u"润色"_ustr);
        m_xChipShorten->set_label(u"精简"_ustr);
        m_xChipExpand->set_label(u"扩写"_ustr);
        m_xChipEn->set_label(u"译英"_ustr);
        m_xChipComplete->set_label(u"续写"_ustr);
        m_xInstruction->set_placeholder_text(
            u"文字：描述修改，或选芯片/方案…（Enter 生成 · Tab 应用）"_ustr);
    }
}

void AIInlineEditPopover::refreshContextLabel()
{
    OUString surface = m_aSel.surface;
    if (surface == u"writer"_ustr)
        surface = u"文字"_ustr;
    else if (surface == u"calc"_ustr)
        surface = u"表格"_ustr;
    else if (surface == u"impress"_ustr)
        surface = u"演示"_ustr;

    OUStringBuffer b;
    if (m_eMode == InlineMode::Complete)
        b.append(u"✨ 续写补全 · "_ustr);
    else
        b.append(u"✏️ 内联编辑 · "_ustr);
    b.append(surface);
    if (!m_aSel.position.isEmpty())
    {
        b.append(u" · "_ustr);
        b.append(m_aSel.position);
    }
    b.append(u" · "_ustr);
    b.append(m_aSel.length);
    b.append(u" 字"_ustr);
    if (m_aSel.length == 0)
        b.append(u" · 光标续写"_ustr);
    else
    {
        OUString preview = m_aSel.text;
        if (preview.getLength() > 60)
            preview = preview.copy(0, 60) + u"…"_ustr;
        b.append(u"\n「"_ustr);
        b.append(preview);
        b.append(u"」"_ustr);
    }
    m_xContext->set_label(b.makeStringAndClear());
}

void AIInlineEditPopover::buildSuggestCorpus()
{
    m_aCorpus.clear();
    auto addChip = [&](const OUString& id, const OUString& label, const OUString& instr,
                       const OUString& cap) {
        SuggestItem s;
        s.id = id;
        s.label = u"⚡ "_ustr + label;
        s.instruction = instr;
        s.capability = cap;
        m_aCorpus.push_back(s);
    };
    addChip(u"polish"_ustr, u"润色"_ustr, u"润色改写，保持原意，更通顺专业。"_ustr, u"rewrite"_ustr);
    addChip(u"shorten"_ustr, u"精简"_ustr, u"精简到约一半篇幅，保留关键信息。"_ustr,
            u"summarize"_ustr);
    addChip(u"expand"_ustr, u"扩写"_ustr, u"在不编造事实前提下扩写丰富细节。"_ustr, u"rewrite"_ustr);
    addChip(u"en"_ustr, u"译英"_ustr, u"将内容译为专业英文。"_ustr, u"rewrite"_ustr);
    addChip(u"complete"_ustr, u"续写"_ustr, u"在当前位置自然续写，只输出续写正文。"_ustr,
            u"background"_ustr);

    const auto cat = kqoffice::ai::chat::DocumentAIScenarioStore::load();
    const OUString surface = m_aSel.surface.toAsciiLowerCase();
    for (const auto& sc : cat.items)
    {
        if (!sc.enabled)
            continue;
        // Prefer scenarios for current surface (+ general / any).
        const OUString catg = sc.category.toAsciiLowerCase();
        const OUString pref = sc.preferredSurface.toAsciiLowerCase();
        const bool surfaceOk = surface.isEmpty() || surface == u"none"_ustr
                               || catg == surface || pref == surface || pref == u"any"_ustr
                               || catg == u"general"_ustr || catg.isEmpty();
        if (!surfaceOk)
            continue;
        SuggestItem s;
        s.id = sc.id;
        s.label = (sc.options.pinned ? u"📌 "_ustr : u"方案 · "_ustr) + sc.titleZh;
        if (!sc.slashCommand.isEmpty())
            s.label += u"  "_ustr + sc.slashCommand;
        s.instruction = sc.promptTemplate.isEmpty()
                            ? (u"按方案「"_ustr + sc.titleZh + u"」处理选区。"_ustr)
                            : sc.promptTemplate;
        s.capability = kqoffice::ai::normalizeCapabilityHint(sc.capabilityHint);
        m_aCorpus.push_back(s);
    }
}

void AIInlineEditPopover::refreshSuggestions(const OUString& rQuery)
{
    m_aFiltered.clear();
    m_xSuggest->clear();
    const OUString q = rQuery.trim().toAsciiLowerCase();
    sal_Int32 n = 0;
    for (const auto& s : m_aCorpus)
    {
        if (!q.isEmpty())
        {
            const OUString hay = OUString(s.label + s.instruction + s.id).toAsciiLowerCase();
            if (hay.indexOf(q) < 0)
                continue;
        }
        m_aFiltered.push_back(s);
        m_xSuggest->append(OUString::number(n), s.label);
        ++n;
        if (n >= 12)
            break;
    }
    if (m_xSuggest->n_children() > 0)
        m_xSuggest->select(0);
}

css::ai::ProviderResponse AIInlineEditPopover::callProvider(const OUString& rPrompt,
                                                            const OUString& rCapability)
{
    css::ai::ProviderResponse fail;
    fail.status = u"provider-error"_ustr;
    fail.content = u"provider unavailable"_ustr;
    try
    {
        auto xContext = comphelper::getProcessComponentContext();
        if (!xContext.is() || !xContext->getServiceManager().is())
            return fail;
        css::uno::Reference<css::ai::XProvider> xProvider(
            xContext->getServiceManager()->createInstanceWithContext(OUString(V2_PROVIDER),
                                                                     xContext),
            css::uno::UNO_QUERY);
        if (!xProvider.is())
            return fail;

        const auto bind = kqoffice::ai::chat::DocumentAIContext::bindUserInput(rPrompt);
        css::ai::ProviderRequest req;
        req.capability = kqoffice::ai::normalizeCapabilityHint(rCapability);
        req.prompt = bind.enrichedPrompt.isEmpty() ? rPrompt : bind.enrichedPrompt;
        req.context = bind.providerContext;
        req.timeoutMs = PROVIDER_TIMEOUT_MS;
        return xProvider->call(req);
    }
    catch (...)
    {
        return fail;
    }
}

void AIInlineEditPopover::updatePreviewDisplay()
{
    if (!m_bHasPreview)
    {
        m_xPreview->set_text(OUString());
        return;
    }
    // Diff-style preview when rewriting a selection
    if (m_eMode != InlineMode::Complete && m_aSel.length > 0 && !m_aSel.text.isEmpty())
    {
        OUStringBuffer b;
        b.append(u"—— 原文 ——\n"_ustr);
        OUString oldP = m_aSel.text;
        if (oldP.getLength() > 800)
            oldP = oldP.copy(0, 800) + u"…"_ustr;
        b.append(oldP);
        b.append(u"\n\n—— 改写（预览）——\n"_ustr);
        b.append(m_sPreviewText);
        b.append(u"\n\n⌘ Tab 应用 · Esc 关闭"_ustr);
        m_xPreview->set_text(b.makeStringAndClear());
    }
    else
    {
        // Ghost gray-text metaphor: show caret context + muted completion.
        OUStringBuffer b;
        b.append(u"〔上下文〕"_ustr);
        OUString ctx = m_aSel.text;
        if (ctx.getLength() > 120)
            ctx = ctx.copy(ctx.getLength() - 120);
        if (ctx.isEmpty())
            b.append(u"（光标处）"_ustr);
        else
            b.append(ctx);
        b.append(u"\n\n〔幽灵灰字 · 未写入〕\n"_ustr);
        b.append(m_sPreviewText);
        b.append(u"\n\n── Tab 接受写入 · Esc 放弃 ──"_ustr);
        m_xPreview->set_text(b.makeStringAndClear());
    }
}

void AIInlineEditPopover::generate()
{
    m_aAutoGen.Stop();
    OUString instruction = m_xInstruction->get_text().trim();
    if (instruction.isEmpty())
    {
        if (m_eMode == InlineMode::Complete)
            instruction = u"自然续写当前段落，只输出续写正文。"_ustr;
        else
        {
            m_xStatus->set_label(u"请输入指令，或点快捷芯片 / 选补全项。"_ustr);
            return;
        }
    }

    if (instruction.indexOf(u"{selection}"_ustr) >= 0)
    {
        const OUString body
            = m_aSel.text.isEmpty() ? u"（无选区，请续写或结合上下文）"_ustr : m_aSel.text;
        instruction = instruction.replaceAll(u"{selection}"_ustr, body);
    }

    OUString cap = m_sCapability;
    OUStringBuffer prompt;
    if (m_aSel.surface == u"calc"_ustr && m_eMode != InlineMode::Complete)
    {
        // Formula-bar feel: first line = formula for Tab write-back into active cell.
        prompt.append(u"【表格就地公式 Ctrl+K】像公式栏一样服务当前单元格/区域。\n"_ustr);
        prompt.append(u"硬性规则：\n"_ustr);
        prompt.append(u"1) 若任务是公式/计算/汇总/清洗派生：第一行必须是以 = 开头的完整公式；\n"_ustr);
        prompt.append(u"2) 第二行起可简短说明（中文）；\n"_ustr);
        prompt.append(u"3) 多公式时每行一个 =…；\n"_ustr);
        prompt.append(u"4) 不要用 Markdown 代码围栏包住公式行。\n"_ustr);
        prompt.append(u"指令："_ustr);
        prompt.append(instruction);
        prompt.append(u"\n选区("_ustr);
        prompt.append(m_aSel.position.isEmpty() ? u"活动单元格"_ustr : m_aSel.position);
        prompt.append(u")：\n"_ustr);
        prompt.append(m_aSel.text.isEmpty() ? u"（空选区 — 请给通用可用公式）"_ustr
                                            : m_aSel.text);
        if (cap.isEmpty())
            cap = u"chat"_ustr;
    }
    else if (m_aSel.surface == u"impress"_ustr && m_eMode != InlineMode::Complete)
    {
        prompt.append(u"【演示内联 Ctrl+K / 成片结构】按指令处理幻灯。\n"_ustr);
        prompt.append(u"若是大纲成片或拆页：每页用\n"_ustr);
        prompt.append(u"## N. 标题\n- 要点\n讲稿：口播一句\n"_ustr);
        prompt.append(u"格式输出，便于多页写回。\n"_ustr);
        prompt.append(u"指令："_ustr);
        prompt.append(instruction);
        prompt.append(u"\n选区("_ustr);
        prompt.append(m_aSel.position);
        prompt.append(u")：\n"_ustr);
        prompt.append(m_aSel.text);
        if (cap.isEmpty())
            cap = u"rewrite"_ustr;
    }
    else if (m_aSel.length > 0 && m_eMode != InlineMode::Complete)
    {
        prompt.append(u"【内联编辑 Ctrl+K】按用户指令改写选区。只输出改写后的正文，不要解释。\n"_ustr);
        prompt.append(u"指令："_ustr);
        prompt.append(instruction);
        prompt.append(u"\n选区：\n"_ustr);
        prompt.append(m_aSel.text);
        if (cap.isEmpty())
            cap = u"rewrite"_ustr;
    }
    else
    {
        prompt.append(
            u"【内联续写/补全】在光标处自然续写 1–3 句。只输出续写正文，不要解释、不要引号。\n"_ustr);
        prompt.append(u"指令："_ustr);
        prompt.append(instruction);
        if (!m_aSel.text.isEmpty())
        {
            prompt.append(u"\n上下文（可参考，勿重复）：\n"_ustr);
            OUString ctx = m_aSel.text;
            if (ctx.getLength() > 400)
                ctx = ctx.copy(ctx.getLength() - 400);
            prompt.append(ctx);
        }
        if (cap.isEmpty())
            cap = u"background"_ustr; // light slot for speed
    }

    m_xStatus->set_label(u"生成中…（"_ustr + cap + u"）"_ustr);
    m_xGenerate->set_sensitive(false);
    m_xAccept->set_sensitive(false);

    const css::ai::ProviderResponse rsp = callProvider(prompt.makeStringAndClear(), cap);

    m_xGenerate->set_sensitive(true);
    if (rsp.status != u"ok"_ustr || rsp.content.trim().isEmpty())
    {
        m_bHasPreview = false;
        m_sPreviewText.clear();
        m_xPreview->set_text(OUString());
        m_xStatus->set_label(u"生成失败："_ustr
                             + (rsp.content.isEmpty() ? rsp.status : rsp.content.trim()));
        m_xAccept->set_sensitive(false);
        return;
    }

    m_sPreviewText = rsp.content.trim();
    if (m_sPreviewText.startsWith(u"```"_ustr))
    {
        sal_Int32 nl = m_sPreviewText.indexOf(u'\n');
        if (nl > 0)
            m_sPreviewText = m_sPreviewText.copy(nl + 1);
        sal_Int32 end = m_sPreviewText.lastIndexOf(u"```"_ustr);
        if (end > 0)
            m_sPreviewText = m_sPreviewText.copy(0, end).trim();
    }
    m_bHasPreview = true;
    m_xAccept->set_sensitive(true);
    updatePreviewDisplay();
    m_xStatus->set_label(u"就绪 · Tab 应用 · Esc 关闭 · evidence="_ustr
                         + (rsp.evidenceId.isEmpty() ? u"—"_ustr : rsp.evidenceId));
}

bool AIInlineEditPopover::applyPreview()
{
    if (!m_bHasPreview || m_sPreviewText.isEmpty())
        return false;

    m_aSel = kqoffice::ai::chat::AgentChatSelectionCapture::captureCurrent();

    kqoffice::ai::chat::ApplyPlan plan;
    plan.planId = (m_eMode == InlineMode::Complete) ? u"ap-ghost-complete"_ustr
                                                    : u"ap-inline-k"_ustr;
    plan.rawOutput = m_sPreviewText;

    // —— Surface-smart apply (Excel/PPT/Word paths) ——
    bool bBuilt = false;
    if (m_aSel.surface == u"calc"_ustr)
    {
        auto formulas
            = kqoffice::ai::chat::AgentChatDiffExtractor::extractAllFormulas(m_sPreviewText);
        if (formulas.empty())
        {
            const OUString one
                = kqoffice::ai::chat::AgentChatDiffExtractor::extractLeadingFormula(m_sPreviewText);
            if (!one.isEmpty())
                formulas.push_back(one);
        }
        if (!formulas.empty())
        {
            OUString pos = m_aSel.position;
            if (pos.isEmpty())
                pos = u"cell:A1"_ustr;
            plan = kqoffice::ai::chat::AgentChatDiffExtractor::makeFormulaRangePlan(
                pos, formulas, m_aSel.text);
            plan.rawOutput = m_sPreviewText;
            bBuilt = kqoffice::ai::chat::AgentChatDiffExtractor::validate(plan);
            if (bBuilt)
                plan.planId = u"ap-inline-formula"_ustr;
        }
    }
    else if (m_aSel.surface == u"impress"_ustr)
    {
        // Outline-style multi-slide when instruction/capability looks like 大纲/plan
        const OUString instr = m_xInstruction->get_text();
        if (m_sCapability == u"plan"_ustr || instr.indexOf(u"大纲"_ustr) >= 0
            || instr.indexOf(u"成片"_ustr) >= 0 || m_sPreviewText.indexOf(u"##"_ustr) >= 0)
        {
            auto outline
                = kqoffice::ai::chat::AgentChatDiffExtractor::extractOutlineSlidePlan(m_sPreviewText);
            if (kqoffice::ai::chat::AgentChatDiffExtractor::validate(outline))
            {
                plan = std::move(outline);
                bBuilt = true;
            }
        }
    }

    if (!bBuilt)
    {
        kqoffice::ai::chat::DiffOperation op;
        const bool bReplace = m_aSel.length > 0 && m_eMode != InlineMode::Complete;
        op.opType = bReplace ? u"replace"_ustr : u"insert"_ustr;
        op.target = m_aSel.position;
        if (op.target.isEmpty())
        {
            if (m_aSel.surface == u"calc"_ustr)
                op.target = u"cell:A1"_ustr;
            else if (m_aSel.surface == u"impress"_ustr)
                op.target = u"slide:1"_ustr;
            else
                op.target = u"para:1"_ustr;
        }
        if (m_aSel.surface == u"calc"_ustr && op.target.startsWith(u"range:"_ustr))
        {
            OUString rest = op.target.copy(6);
            const sal_Int32 colon = rest.indexOf(u':');
            op.target = u"cell:"_ustr + (colon > 0 ? rest.copy(0, colon) : rest);
        }
        op.oldText = bReplace ? m_aSel.text : OUString();
        op.newText = m_sPreviewText;
        plan.operations.push_back(op);
    }

    // Tab / 应用 = explicit human approval
    const auto result
        = kqoffice::ai::chat::DocumentAIApply::applyApprovedWithRawFallback(plan, m_sPreviewText);
    if (!result.success)
    {
        m_xStatus->set_label(u"应用失败："_ustr
                             + (result.error.isEmpty() ? result.engine : result.error));
        return false;
    }
    m_xStatus->set_label(u"已应用 · engine="_ustr + result.engine + u" · surface="_ustr
                         + m_aSel.surface + u" · 可撤销"_ustr);
    return true;
}

void AIInlineEditPopover::runChip(const OUString& rInstruction, const OUString& rCapability)
{
    m_xInstruction->set_text(rInstruction);
    m_sCapability = rCapability;
    if (rCapability == u"background"_ustr || rCapability == u"chat"_ustr)
        m_eMode = InlineMode::Complete;
    generate();
}

IMPL_LINK_NOARG(AIInlineEditPopover, OnInstructionChanged, weld::Entry&, void)
{
    refreshSuggestions(m_xInstruction->get_text());
}

IMPL_LINK_NOARG(AIInlineEditPopover, OnInstructionActivate, weld::Entry&, bool)
{
    generate();
    return true;
}

IMPL_LINK_NOARG(AIInlineEditPopover, OnSuggestActivated, weld::TreeView&, bool)
{
    const int row = m_xSuggest->get_selected_index();
    if (row < 0 || static_cast<size_t>(row) >= m_aFiltered.size())
        return true;
    const SuggestItem& s = m_aFiltered[static_cast<size_t>(row)];
    m_xInstruction->set_text(s.instruction);
    m_sCapability = s.capability;
    generate();
    return true;
}

IMPL_LINK_NOARG(AIInlineEditPopover, OnGenerateClicked, weld::Button&, void) { generate(); }

IMPL_LINK_NOARG(AIInlineEditPopover, OnAcceptClicked, weld::Button&, void)
{
    if (applyPreview())
        closeSelf();
}

IMPL_LINK_NOARG(AIInlineEditPopover, OnRejectClicked, weld::Button&, void) { closeSelf(); }

IMPL_LINK(AIInlineEditPopover, OnChipClicked, weld::Button&, rBtn, void)
{
    const OUString s = m_aSel.surface;
    if (s == u"calc"_ustr)
    {
        if (&rBtn == m_xChipPolish.get())
            runChip(u"就地公式：根据选区写出可写入活动单元格的公式。"
                    u"第一行必须以 = 开头；再简要说明。"_ustr,
                    u"chat"_ustr);
        else if (&rBtn == m_xChipShorten.get())
            runChip(u"数据清洗：指出空值/重复/格式问题；若可公式化，第一行输出 = 公式。"_ustr,
                    u"extract"_ustr);
        else if (&rBtn == m_xChipExpand.get())
            runChip(u"汇总：给出合计/平均/计数等；第一行 = 公式，并说明引用区域。"_ustr,
                    u"summarize"_ustr);
        else if (&rBtn == m_xChipEn.get())
            runChip(u"根据选区建议图表类型（柱/线/饼）、系列与分类轴，并给出插入步骤；"
                    u"若需辅助公式，第一行输出 = 公式。"_ustr,
                    u"plan"_ustr);
        else if (&rBtn == m_xChipComplete.get())
            runChip(u"在当前位置自然续写标签或说明文字。"_ustr, u"background"_ustr);
        return;
    }
    if (s == u"impress"_ustr)
    {
        if (&rBtn == m_xChipPolish.get())
            runChip(u"【页级】只改当前页：第一行标题，其后每行要点；可附 版式：/ 讲稿：/ 配图：。保持事实。"_ustr,
                    u"rewrite"_ustr);
        else if (&rBtn == m_xChipShorten.get())
            runChip(u"为下列要点写 30–60 秒口播讲稿。"_ustr, u"chat"_ustr);
        else if (&rBtn == m_xChipExpand.get())
            runChip(u"拆成编号幻灯：## N. 标题\\n版式：标题内容|标题页|分栏\\n主题：商务蓝\\n"
                    u"- 要点\\n讲稿：…\\n配图：…"_ustr,
                    u"plan"_ustr);
        else if (&rBtn == m_xChipEn.get())
            runChip(u"将幻灯文案译为专业英文。"_ustr, u"rewrite"_ustr);
        else if (&rBtn == m_xChipComplete.get())
            runChip(u"在当前位置自然续写幻灯要点。"_ustr, u"background"_ustr);
        return;
    }
    // Writer / default
    if (&rBtn == m_xChipPolish.get())
        runChip(u"润色改写，保持原意，更通顺专业。"_ustr, u"rewrite"_ustr);
    else if (&rBtn == m_xChipShorten.get())
        runChip(u"精简到约一半篇幅，保留关键信息。"_ustr, u"summarize"_ustr);
    else if (&rBtn == m_xChipExpand.get())
        runChip(u"在不编造事实前提下扩写丰富细节。"_ustr, u"rewrite"_ustr);
    else if (&rBtn == m_xChipEn.get())
        runChip(u"将内容译为专业英文。"_ustr, u"rewrite"_ustr);
    else if (&rBtn == m_xChipComplete.get())
        runChip(u"在当前位置自然续写，只输出续写正文。"_ustr, u"background"_ustr);
}

IMPL_LINK_NOARG(AIInlineEditPopover, OnClosed, weld::Popover&, void)
{
    m_aAutoGen.Stop();
    g_pActive.reset();
}

IMPL_LINK(AIInlineEditPopover, OnKeyPress, const KeyEvent&, rKEvt, bool)
{
    const vcl::KeyCode& rCode = rKEvt.GetKeyCode();
    const sal_uInt16 nKey = rCode.GetCode();
    if (nKey == KEY_ESCAPE)
    {
        closeSelf();
        return true;
    }
    if (nKey == KEY_TAB)
    {
        // Tab = accept when preview ready (Cursor-like)
        if (m_bHasPreview)
        {
            if (applyPreview())
                closeSelf();
            return true;
        }
        // No preview yet: jump to first suggestion
        if (m_xSuggest->n_children() > 0)
        {
            m_xSuggest->grab_focus();
            m_xSuggest->select(0);
            return true;
        }
    }
    if (nKey == KEY_DOWN && m_xSuggest->n_children() > 0)
    {
        m_xSuggest->grab_focus();
        if (m_xSuggest->get_selected_index() < 0)
            m_xSuggest->select(0);
        return true;
    }
    return false;
}

IMPL_LINK_NOARG(AIInlineEditPopover, OnAutoGenerate, Timer*, void)
{
    generate();
}

bool IsLiveViewFrame(const SfxViewFrame* pFrame)
{
    for (SfxViewFrame* pCandidate = SfxViewFrame::GetFirst(nullptr, false); pCandidate;
         pCandidate = SfxViewFrame::GetNext(*pCandidate, nullptr, false))
    {
        if (pCandidate == pFrame)
            return true;
    }
    return false;
}

void ShowInlineOnMainThread(SfxViewFrame& rFrame, InlineMode eMode)
{
    tools::Rectangle aRectangle(Point(0, 0), rFrame.GetWindow().GetSizePixel());
    aRectangle = tools::Rectangle(Point(aRectangle.GetWidth() / 4, aRectangle.GetHeight() / 5),
                                  Size(aRectangle.GetWidth() / 2, 40));
    weld::Window* pParent = weld::GetPopupParent(rFrame.GetWindow(), aRectangle);
    if (!pParent)
        return;
    g_pActive.reset();
    g_pActive = std::make_unique<AIInlineEditPopover>(pParent, aRectangle, eMode);
    g_pActive->show();
}

struct PendingShow
{
    SfxViewFrame* pFrame = nullptr;
    InlineMode eMode = InlineMode::Edit;
};

void ShowInlineAsync(void*, void* pArg)
{
    std::unique_ptr<PendingShow> pPending(static_cast<PendingShow*>(pArg));
    if (!pPending->pFrame || !IsLiveViewFrame(pPending->pFrame))
        return;
    ShowInlineOnMainThread(*pPending->pFrame, pPending->eMode);
}

void ShowAsync(SfxViewFrame& rFrame, InlineMode eMode)
{
    if (!Application::IsMainThread())
    {
        auto pPending = std::make_unique<PendingShow>();
        pPending->pFrame = &rFrame;
        pPending->eMode = eMode;
        if (Application::PostUserEvent(LINK_NONMEMBER(nullptr, ShowInlineAsync), pPending.get()))
        {
            pPending.release();
            return;
        }
        return;
    }
    ShowInlineOnMainThread(rFrame, eMode);
}
} // namespace

AIInlineEditDispatcher& AIInlineEditDispatcher::Get()
{
    static AIInlineEditDispatcher aInstance;
    return aInstance;
}

void AIInlineEditDispatcher::Show(SfxViewFrame& rFrame) { ShowAsync(rFrame, InlineMode::Edit); }

void AIInlineEditDispatcher::ShowComplete(SfxViewFrame& rFrame)
{
    ShowAsync(rFrame, InlineMode::Complete);
}

} // namespace sfx2

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
