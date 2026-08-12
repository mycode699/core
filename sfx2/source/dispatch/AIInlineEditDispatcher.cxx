/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Cursor-style Ctrl/Cmd+K inline AI edit + Tab-accept + light-slot complete.
 */

#include <dispatch/AIInlineEditDispatcher.hxx>
#include <startcentertheme.hxx>

#include <AgentChatDiffExtractor.hxx>
#include <AgentChatSelectionCapture.hxx>
#include <DocumentAIApply.hxx>
#include <DocumentAIContext.hxx>
#include <DocumentAIInputPrefs.hxx>
#include <DocumentAIScenarioStore.hxx>
#include <ModelRoles.hxx>

#include <com/sun/star/accessibility/XAccessible.hpp>
#include <com/sun/star/accessibility/XAccessibleContext.hpp>
#include <com/sun/star/accessibility/XAccessibleComponent.hpp>
#include <com/sun/star/accessibility/XAccessibleText.hpp>
#include <com/sun/star/ai/ProviderRequest.hpp>
#include <com/sun/star/ai/ProviderResponse.hpp>
#include <com/sun/star/ai/XProvider.hpp>
#include <com/sun/star/awt/Point.hpp>
#include <com/sun/star/awt/Rectangle.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <comphelper/OAccessible.hxx>
#include <comphelper/processfactory.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>
#include <sfx2/viewfrm.hxx>
#include <tools/gen.hxx>
#include <tools/link.hxx>
#include <vcl/event.hxx>
#include <vcl/help.hxx>
#include <vcl/keycodes.hxx>
#include <vcl/svapp.hxx>
#include <vcl/timer.hxx>
#include <vcl/vclevent.hxx>
#include <vcl/vclptr.hxx>
#include <vcl/window.hxx>
#include <vcl/weld/Builder.hxx>
#include <vcl/weld/Entry.hxx>
#include <vcl/weld/Popover.hxx>
#include <vcl/weld/TextView.hxx>
#include <vcl/weld/TreeView.hxx>
#include <vcl/weld/weld.hxx>
#include <vcl/weld/weldutils.hxx>

#include <algorithm>
#include <chrono>
#include <memory>
#include <vector>

#if defined(MACOSX) || defined(LINUX) || defined(FREEBSD) || defined(NETBSD) || defined(OPENBSD) \
    || defined(DRAGONFLY) || defined(ANDROID) || defined(EMSCRIPTEN)
#include <dlfcn.h>
#endif

namespace sfx2
{
namespace
{
constexpr OUStringLiteral V2_PROVIDER = u"com.sun.star.ai.Provider";
constexpr sal_Int32 PROVIDER_TIMEOUT_EDIT_MS = 45000;
constexpr sal_Int32 PROVIDER_TIMEOUT_GHOST_MS = 12000;

OUString capBefore(const kqoffice::ai::chat::SelectionContext& sel, sal_Int32 nMax)
{
    OUString t = sel.beforeText;
    if (t.isEmpty() && !sel.paraText.isEmpty() && sel.length == 0)
        t = sel.paraText;
    if (t.isEmpty() && !sel.text.isEmpty())
        t = sel.text;
    if (t.getLength() > nMax && nMax > 0)
        t = t.copy(t.getLength() - nMax);
    return t;
}

OUString capAfter(const kqoffice::ai::chat::SelectionContext& sel, sal_Int32 nMax)
{
    OUString t = sel.afterText;
    if (t.getLength() > nMax && nMax > 0)
        t = t.copy(0, nMax);
    return t;
}

/// Soft-trim model noise: fences, leading bullets that break insert, BOM-ish junk.
OUString polishPreviewBody(OUString raw)
{
    raw = raw.trim();
    if (raw.startsWith(u"```"_ustr))
    {
        sal_Int32 nl = raw.indexOf(u'\n');
        if (nl > 0)
            raw = raw.copy(nl + 1);
        sal_Int32 end = raw.lastIndexOf(u"```"_ustr);
        if (end > 0)
            raw = raw.copy(0, end).trim();
    }
    // Drop accidental scheme labels left inside a chunk
    if (raw.startsWith(u"===方案"_ustr) || raw.startsWith(u"---方案"_ustr)
        || raw.startsWith(u"===OPTION"_ustr) || raw.startsWith(u"---OPTION"_ustr))
    {
        const sal_Int32 nl = raw.indexOf(u'\n');
        if (nl >= 0)
            raw = raw.copy(nl + 1).trim();
    }
    // Strip wrapping quotes if whole body is quoted once
    if (raw.getLength() >= 2
        && ((raw[0] == u'"' && raw[raw.getLength() - 1] == u'"')
            || (raw[0] == u'\u201c' && raw[raw.getLength() - 1] == u'\u201d')))
        raw = raw.copy(1, raw.getLength() - 2).trim();
    return raw;
}

void appendCaretContext(OUStringBuffer& prompt, const kqoffice::ai::chat::SelectionContext& sel,
                        sal_Int32 nCtxChars)
{
    const sal_Int32 nBefore = nCtxChars > 0 ? nCtxChars : 600;
    const sal_Int32 nAfter = std::max<sal_Int32>(80, nBefore / 2);
    const OUString before = capBefore(sel, nBefore);
    const OUString after = capAfter(sel, nAfter);
    if (!before.isEmpty())
    {
        prompt.append(u"\n【光标前 · 风格/话题需连贯，勿整段重复】\n"_ustr);
        prompt.append(before);
    }
    if (!after.isEmpty())
    {
        prompt.append(u"\n【光标后 · 续写须与下文衔接，勿冲突/重复】\n"_ustr);
        prompt.append(after);
    }
    if (before.isEmpty() && after.isEmpty() && !sel.text.isEmpty())
    {
        prompt.append(u"\n【上下文】\n"_ustr);
        OUString ctx = sel.text;
        if (ctx.getLength() > nBefore)
            ctx = ctx.copy(ctx.getLength() - nBefore);
        prompt.append(ctx);
    }
}

struct InlinePrefsSnap
{
    bool multiVariant = true;
    sal_Int32 ghostTimeoutMs = PROVIDER_TIMEOUT_GHOST_MS;
    sal_Int32 contextChars = 600;
};

InlinePrefsSnap loadInlinePrefsSnap()
{
    InlinePrefsSnap s;
    try
    {
        const auto prefs = kqoffice::ai::chat::DocumentAIInputPrefs::load();
        s.multiVariant = prefs.inlineMultiVariant;
        if (prefs.inlineGhostTimeoutMs > 0)
            s.ghostTimeoutMs = prefs.inlineGhostTimeoutMs;
        if (prefs.inlineContextChars > 0)
            s.contextChars = prefs.inlineContextChars;
    }
    catch (...)
    {
    }
    return s;
}

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

// Forward: caret bounds helper (defined later with ShowInline).
bool tryCaretRectInWindow(vcl::Window& rWin, tools::Rectangle& rOut);

// —— Caret-adjacent ghost tip (Help::ShowPopover) + optional Writer ExtTextInput ——
// ExtText composition is temporary; cancel path posts empty + End so Writer
// DeleteExtTextInput keeps nothing (sRecord empty → no main-doc write).
// Accept path always clears composition first, then ApplyPlan (approve-before-write).
struct GhostTipState
{
    VclPtr<vcl::Window> pWin;
    void* pId = nullptr;
    bool bExtTextActive = false;
};
GhostTipState g_aGhostTip;

void clearExtTextGhost()
{
    if (!g_aGhostTip.bExtTextActive || !g_aGhostTip.pWin)
    {
        g_aGhostTip.bExtTextActive = false;
        return;
    }
    try
    {
        // Empty composition then End → Writer DeleteExtTextInput with empty record (no insert).
        g_aGhostTip.pWin->PostExtTextInputEvent(VclEventId::ExtTextInput, OUString());
        g_aGhostTip.pWin->PostExtTextInputEvent(VclEventId::EndExtTextInput, OUString());
    }
    catch (...)
    {
    }
    g_aGhostTip.bExtTextActive = false;
}

void showExtTextGhost(vcl::Window* pWin, const OUString& rText)
{
    if (!pWin)
        return;
    OUString body = rText.trim();
    if (body.isEmpty())
        return;
    // Keep composition short for layout stability (multi-line → first line + …).
    const sal_Int32 nl = body.indexOf(u'\n');
    if (nl >= 0)
        body = body.copy(0, nl).trim() + u"…"_ustr;
    if (body.getLength() > 180)
        body = body.copy(0, 180) + u"…"_ustr;
    try
    {
        // Replace any prior composition first.
        if (g_aGhostTip.bExtTextActive)
            clearExtTextGhost();
        pWin->PostExtTextInputEvent(VclEventId::ExtTextInput, body);
        g_aGhostTip.bExtTextActive = true;
        g_aGhostTip.pWin = pWin;
    }
    catch (...)
    {
        g_aGhostTip.bExtTextActive = false;
    }
}

void hideGhostTip()
{
    clearExtTextGhost();
    if (g_aGhostTip.pId && g_aGhostTip.pWin)
    {
        try
        {
            Help::HidePopover(g_aGhostTip.pWin, g_aGhostTip.pId);
        }
        catch (...)
        {
        }
    }
    g_aGhostTip.pId = nullptr;
    // Keep pWin only if still useful for ExtText; clear after tip hide.
    if (!g_aGhostTip.bExtTextActive)
        g_aGhostTip.pWin.reset();
}

void showOrUpdateGhostTip(vcl::Window* pWin, const tools::Rectangle& rWinLocal,
                          const OUString& rText)
{
    if (!pWin)
    {
        hideGhostTip();
        return;
    }
    OUString tip = rText.trim();
    if (tip.isEmpty())
    {
        hideGhostTip();
        return;
    }
    // Single-line tip; multi-line truncated for density.
    const sal_Int32 nl = tip.indexOf(u'\n');
    if (nl >= 0)
        tip = tip.copy(0, nl).trim();
    if (tip.getLength() > 100)
        tip = tip.copy(0, 100) + u"…"_ustr;

    // Sprint C: optional in-document ExtTextInput gray composition (Writer-friendly).
    const auto prefs = kqoffice::ai::chat::DocumentAIInputPrefs::load();
    if (prefs.inlineExtTextGhost)
        showExtTextGhost(pWin, rText);

    tip = prefs.inlineExtTextGhost ? (u"👻 段内灰字 · Tab 批准 · "_ustr + tip)
                                   : (u"👻 "_ustr + tip);

    const Point screen = pWin->OutputToScreenPixel(rWinLocal.TopLeft());
    tools::Rectangle screenRect(
        screen, Size(std::max(rWinLocal.GetWidth(), tools::Long(12)),
                     std::max(rWinLocal.GetHeight(), tools::Long(18))));
    try
    {
        if (g_aGhostTip.pId && g_aGhostTip.pWin.get() == pWin)
        {
            Help::UpdatePopover(g_aGhostTip.pId, pWin, screenRect, tip);
            return;
        }
        // Don't call hideGhostTip() — would clear ExtText; only replace popover.
        if (g_aGhostTip.pId && g_aGhostTip.pWin)
        {
            try
            {
                Help::HidePopover(g_aGhostTip.pWin, g_aGhostTip.pId);
            }
            catch (...)
            {
            }
            g_aGhostTip.pId = nullptr;
        }
        g_aGhostTip.pWin = pWin;
        g_aGhostTip.pId = Help::ShowPopover(
            pWin, screenRect, tip,
            QuickHelpFlags::Left | QuickHelpFlags::Bottom | QuickHelpFlags::NoEvadePointer);
    }
    catch (...)
    {
        if (g_aGhostTip.pId)
        {
            try
            {
                if (g_aGhostTip.pWin)
                    Help::HidePopover(g_aGhostTip.pWin, g_aGhostTip.pId);
            }
            catch (...)
            {
            }
            g_aGhostTip.pId = nullptr;
        }
    }
}

class AIInlineEditPopover final
{
public:
    AIInlineEditPopover(weld::Widget* pParent, const tools::Rectangle& rAnchor, InlineMode eMode,
                        vcl::Window* pEditWin)
        : m_pAnchor(pParent)
        , m_aAnchor(rAnchor)
        , m_pEditWin(pEditWin)
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
        , m_aSuggestDebounce("AIInlineSuggestDebounce")
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
        // Tab / ←→ / R must work when focus is on action buttons (not only instruction).
        m_xAccept->connect_key_press(LINK(this, AIInlineEditPopover, OnKeyPress));
        m_xReject->connect_key_press(LINK(this, AIInlineEditPopover, OnKeyPress));
        m_xGenerate->connect_key_press(LINK(this, AIInlineEditPopover, OnKeyPress));
        m_xChipPolish->connect_clicked(LINK(this, AIInlineEditPopover, OnChipClicked));
        m_xChipShorten->connect_clicked(LINK(this, AIInlineEditPopover, OnChipClicked));
        m_xChipExpand->connect_clicked(LINK(this, AIInlineEditPopover, OnChipClicked));
        m_xChipEn->connect_clicked(LINK(this, AIInlineEditPopover, OnChipClicked));
        m_xChipComplete->connect_clicked(LINK(this, AIInlineEditPopover, OnChipClicked));
        m_xPopover->connect_closed(LINK(this, AIInlineEditPopover, OnClosed));
        m_aSuggestDebounce.SetTimeout(220);
        m_aSuggestDebounce.SetInvokeHandler(LINK(this, AIInlineEditPopover, OnSuggestDebounce));

        m_xPreview->set_editable(false);
        m_xAccept->set_sensitive(false);
        // Align with sidebar 审核 chain vocabulary (批准写回 / 拒绝).
        m_xAccept->set_label(u"批准写回 (Tab)"_ustr);
        m_xReject->set_label(u"拒绝 (Esc)"_ustr);
        configureSurfaceChips();

        // Family calm surfaces
        {
            const auto th = sfx2::sc_theme::tokens();
            if (m_xContext)
                m_xContext->set_font_color(th.textSecondary);
            if (m_xStatus)
                m_xStatus->set_font_color(th.textTertiary);
            if (m_xGhostBanner)
                m_xGhostBanner->set_font_color(th.accent);
            if (m_xPreview)
            {
                // Ghost gray body text
                m_xPreview->set_font_color(th.textTertiary);
                m_xPreview->set_background(th.card);
            }
        }

        if (m_eMode == InlineMode::Complete)
        {
            // Compact ghost UI anchored near caret: gray-style preview only, Tab = accept.
            if (m_xGhostBanner)
                m_xGhostBanner->set_visible(true);
            if (m_xChipRow)
                m_xChipRow->set_visible(false);
            if (m_xSuggestScroll)
                m_xSuggestScroll->set_visible(false);
            if (m_xContext)
                m_xContext->set_visible(false);
            m_xInstruction->set_visible(false);
            m_xGenerate->set_visible(false);
            if (m_xPreviewScroll)
                m_xPreviewScroll->set_size_request(420, 120);
            const InlinePrefsSnap snap = loadInlinePrefsSnap();
            if (snap.multiVariant)
            {
                m_xInstruction->set_text(
                    u"自然续写当前段落 1–3 句，给出 2 种续写方案（用 ===方案1=== / ===方案2=== 分隔），"
                    u"每种只输出续写正文；勿重复光标前已有文字。"_ustr);
                m_xStatus->set_label(
                    u"👻 光标旁幽灵补全 · 生成中… · ←→ 方案 · Tab 写入 · Esc 放弃"_ustr);
            }
            else
            {
                m_xInstruction->set_text(
                    u"自然续写当前段落 1–3 句，只输出续写正文；勿重复光标前已有文字。"_ustr);
                m_xStatus->set_label(
                    u"👻 光标旁幽灵补全 · 生成中… · Tab 写入 · Esc 放弃"_ustr);
            }
            m_sCapability = u"background"_ustr; // light slot
            m_xAccept->set_label(u"Tab 写入"_ustr);
            m_xReject->set_label(u"Esc"_ustr);
            m_aAutoGen.SetTimeout(180);
            m_aAutoGen.SetInvokeHandler(LINK(this, AIInlineEditPopover, OnAutoGenerate));
        }
        else
        {
            if (m_xGhostBanner)
                m_xGhostBanner->set_visible(false);
            m_xStatus->set_label(
                u"可圈 AI · Ctrl/Cmd+K · 输入意图匹配建议 · Enter 生成多方案 · "
                u"←→ 选方案 · Tab 批准 · Esc 拒绝"_ustr);
        }
    }

    void show()
    {
        m_xPopover->popup_at_rect(m_pAnchor, m_aAnchor);
        if (m_eMode == InlineMode::Complete)
        {
            // Ghost path: keep focus on accept/preview so Tab works immediately after generate.
            if (m_xAccept)
                m_xAccept->grab_focus();
            m_aAutoGen.Start();
        }
        else
            m_xInstruction->grab_focus();
    }

    void hide()
    {
        m_aAutoGen.Stop();
        hideGhostTip();
        m_xPopover->popdown();
    }

    /// Refresh caret-adjacent tip for Complete mode (true ghost feel).
    void refreshGhostTip();

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
    DECL_LINK(OnSuggestDebounce, Timer*, void);

    void refreshContextLabel();
    void configureSurfaceChips();
    void buildSuggestCorpus();
    void refreshSuggestions(const OUString& rQuery);
    void runChip(const OUString& rInstruction, const OUString& rCapability);
    void generate();
    void updatePreviewDisplay();
    void setActiveVariant(sal_Int32 nIndex);
    void cycleVariant(sal_Int32 nDelta);
    static std::vector<OUString> splitVariants(const OUString& rRaw);
    bool applyPreview();
    void closeSelf();
    css::ai::ProviderResponse callProvider(const OUString& rPrompt, const OUString& rCapability);

    weld::Widget* m_pAnchor;
    tools::Rectangle m_aAnchor;
    VclPtr<vcl::Window> m_pEditWin; ///< view window for caret-adjacent ghost tip
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
    std::vector<OUString> m_aVariants; ///< multi-choice previews (Cursor-style pick)
    sal_Int32 m_nVariant = 0;
    OUString m_sPreviewText;
    OUString m_sCapability; // empty → derive from mode/selection
    bool m_bHasPreview = false;
    Timer m_aSuggestDebounce; ///< rank chips while typing instruction
};

std::unique_ptr<AIInlineEditPopover> g_pActive;

void AIInlineEditPopover::closeSelf()
{
    hide();
    g_pActive.reset();
}

void AIInlineEditPopover::refreshGhostTip()
{
    if (m_eMode != InlineMode::Complete || !m_bHasPreview || m_sPreviewText.isEmpty()
        || !m_pEditWin)
    {
        hideGhostTip();
        return;
    }
    tools::Rectangle caret;
    if (!tryCaretRectInWindow(*m_pEditWin, caret))
        caret = m_aAnchor;
    showOrUpdateGhostTip(m_pEditWin.get(), caret, m_sPreviewText);
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
            u"演示：本页改写 / 讲稿 / 大纲…（禁止一键黑盒成片；Enter 生成）"_ustr);
    }
    else
    {
        m_xChipPolish->set_label(u"润色"_ustr);
        m_xChipShorten->set_label(u"精简"_ustr);
        m_xChipExpand->set_label(u"扩写"_ustr);
        m_xChipEn->set_label(u"译英"_ustr);
        m_xChipComplete->set_label(u"续写"_ustr);
        m_xInstruction->set_placeholder_text(
            u"文字：描述修改，或选芯片/方案…（Enter 生成 · Tab 批准写回）"_ustr);
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
    {
        b.append(u" · 光标续写"_ustr);
        const OUString tail = capBefore(m_aSel, 48);
        if (!tail.isEmpty())
        {
            b.append(u"\n…"_ustr);
            b.append(tail);
        }
    }
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

    // Score-ranked match: prefix on label/id beats substring (Cursor command palette feel).
    struct Ranked
    {
        const SuggestItem* p = nullptr;
        int score = 0;
    };
    std::vector<Ranked> ranked;
    ranked.reserve(m_aCorpus.size());
    for (const auto& s : m_aCorpus)
    {
        if (q.isEmpty())
        {
            ranked.push_back({ &s, 1 });
            continue;
        }
        const OUString lab = s.label.toAsciiLowerCase();
        const OUString id = s.id.toAsciiLowerCase();
        const OUString instr = s.instruction.toAsciiLowerCase();
        int score = 0;
        if (lab.startsWith(q) || id.startsWith(q))
            score = 100;
        else if (lab.indexOf(q) >= 0)
            score = 70;
        else if (instr.indexOf(q) >= 0)
            score = 40;
        else if (OUString(lab + u" "_ustr + instr + u" "_ustr + id).indexOf(q) >= 0)
            score = 20;
        if (score > 0)
            ranked.push_back({ &s, score });
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const Ranked& a, const Ranked& b) { return a.score > b.score; });

    sal_Int32 n = 0;
    for (const auto& r : ranked)
    {
        m_aFiltered.push_back(*r.p);
        m_xSuggest->append(OUString::number(n), r.p->label);
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

        // Ghost complete: lean prompt, skip heavy document skeleton bind for speed.
        const bool bGhost = (m_eMode == InlineMode::Complete)
                            || (rCapability == u"background"_ustr);
        css::ai::ProviderRequest req;
        req.capability = kqoffice::ai::normalizeCapabilityHint(rCapability);
        const InlinePrefsSnap snap = loadInlinePrefsSnap();
        if (bGhost)
        {
            req.prompt = rPrompt;
            // Single-variant: tighter budget (less tokens → faster first paint).
            req.timeoutMs = snap.multiVariant ? snap.ghostTimeoutMs
                                             : std::min(snap.ghostTimeoutMs, sal_Int32(9000));
        }
        else
        {
            const auto bind = kqoffice::ai::chat::DocumentAIContext::bindUserInput(rPrompt);
            req.prompt = bind.enrichedPrompt.isEmpty() ? rPrompt : bind.enrichedPrompt;
            req.context = bind.providerContext;
            req.timeoutMs = PROVIDER_TIMEOUT_EDIT_MS;
        }
        // Yield so the popover can paint "generating…" before blocking call.
        Application::Reschedule(true);
        return xProvider->call(req);
    }
    catch (...)
    {
        return fail;
    }
}

std::vector<OUString> AIInlineEditPopover::splitVariants(const OUString& rRaw)
{
    std::vector<OUString> out;
    OUString body = rRaw.trim();
    // Preferred delimiters: ===方案1=== / ===方案2=== or ---OPTION 1---
    const OUString markers[] = { u"===方案"_ustr, u"===方案 "_ustr, u"---方案"_ustr,
                                 u"===OPTION"_ustr, u"---OPTION"_ustr, u"=== Option"_ustr };
    bool split = false;
    for (const auto& mk : markers)
    {
        if (body.indexOf(mk) >= 0)
        {
            split = true;
            break;
        }
    }
    if (split)
    {
        sal_Int32 pos = 0;
        while (pos < body.getLength())
        {
            sal_Int32 next = body.getLength();
            for (const auto& mk : markers)
            {
                const sal_Int32 p = body.indexOf(mk, pos + 1);
                if (p >= 0 && p < next)
                    next = p;
            }
            OUString chunk = body.copy(pos, next - pos).trim();
            // Drop the marker line itself
            const sal_Int32 nl = chunk.indexOf(u'\n');
            if (nl >= 0 && chunk.indexOf(u"==="_ustr) >= 0)
                chunk = chunk.copy(nl + 1).trim();
            if (!chunk.isEmpty())
                out.push_back(chunk);
            if (next >= body.getLength())
                break;
            pos = next;
        }
    }
    if (out.size() < 2)
    {
        // Fallback: split on double newline into at most 2 chunks if both substantial
        const sal_Int32 mid = body.indexOf(u"\n\n"_ustr);
        if (mid > 40 && mid < body.getLength() - 40 && out.empty())
        {
            out.push_back(body.copy(0, mid).trim());
            out.push_back(body.copy(mid + 2).trim());
        }
    }
    if (out.empty() && !body.isEmpty())
        out.push_back(body);
    // Cap variants
    if (out.size() > 3)
        out.resize(3);
    return out;
}

void AIInlineEditPopover::setActiveVariant(sal_Int32 nIndex)
{
    if (m_aVariants.empty())
        return;
    if (nIndex < 0)
        nIndex = static_cast<sal_Int32>(m_aVariants.size()) - 1;
    if (nIndex >= static_cast<sal_Int32>(m_aVariants.size()))
        nIndex = 0;
    m_nVariant = nIndex;
    m_sPreviewText = m_aVariants[static_cast<size_t>(m_nVariant)];
    m_bHasPreview = !m_sPreviewText.isEmpty();
    m_xAccept->set_sensitive(m_bHasPreview);
    updatePreviewDisplay();
}

void AIInlineEditPopover::cycleVariant(sal_Int32 nDelta)
{
    if (m_aVariants.size() < 2)
        return;
    setActiveVariant(m_nVariant + nDelta);
}

void AIInlineEditPopover::updatePreviewDisplay()
{
    if (!m_bHasPreview)
    {
        m_xPreview->set_text(OUString());
        return;
    }
    const sal_Int32 nTotal = static_cast<sal_Int32>(m_aVariants.empty() ? 1 : m_aVariants.size());
    const sal_Int32 nCur = m_aVariants.empty() ? 1 : (m_nVariant + 1);
    OUStringBuffer head;
    head.append(u"【方案 "_ustr);
    head.append(OUString::number(nCur));
    head.append(u"/"_ustr);
    head.append(OUString::number(nTotal));
    head.append(u" · ←→ 切换 · Tab 批准写回 · Esc 拒绝】\n\n"_ustr);

    // Diff-style preview when rewriting a selection (Codex/Cursor-style split narrative).
    if (m_eMode != InlineMode::Complete && m_aSel.length > 0 && !m_aSel.text.isEmpty())
    {
        OUStringBuffer b;
        b.append(head.makeStringAndClear());
        b.append(u"【原文】\n"_ustr);
        OUString oldP = m_aSel.text;
        if (oldP.getLength() > 800)
            oldP = oldP.copy(0, 800) + u"…"_ustr;
        b.append(oldP);
        b.append(u"\n\n【建议 · 未写入主文档】\n"_ustr);
        b.append(m_sPreviewText);
        m_xPreview->set_text(b.makeStringAndClear());
    }
    else
    {
        // Ghost gray-text metaphor: show caret context + muted completion.
        OUStringBuffer b;
        b.append(head.makeStringAndClear());
        b.append(u"【光标前】"_ustr);
        OUString ctx = capBefore(m_aSel, 160);
        if (ctx.isEmpty())
            b.append(u"（空）"_ustr);
        else
            b.append(ctx);
        const OUString after = capAfter(m_aSel, 80);
        if (!after.isEmpty())
        {
            b.append(u"\n【光标后】"_ustr);
            b.append(after);
        }
        b.append(u"\n\n【幽灵灰字 · 未写入】\n"_ustr);
        b.append(m_sPreviewText);
        m_xPreview->set_text(b.makeStringAndClear());
    }
    if (m_xStatus && m_bHasPreview)
    {
        m_xStatus->set_label(u"预览就绪 · 方案 "_ustr + OUString::number(nCur) + u"/"_ustr
                             + OUString::number(nTotal)
                             + u" · ←→ 切换 · Tab 批准写回 · Esc 拒绝 · R 重新生成"_ustr);
    }
    // Complete mode: also paint caret-adjacent tip (document-local ghost feel).
    if (m_eMode == InlineMode::Complete)
        refreshGhostTip();
    else
        hideGhostTip();
}

void AIInlineEditPopover::generate()
{
    m_aAutoGen.Stop();
    const InlinePrefsSnap snap = loadInlinePrefsSnap();
    const bool bMulti = snap.multiVariant;
    const sal_Int32 nCtx = snap.contextChars;

    OUString instruction = m_xInstruction->get_text().trim();
    if (instruction.isEmpty())
    {
        if (m_eMode == InlineMode::Complete)
            instruction = bMulti
                              ? u"自然续写当前段落 1–3 句；给出 2 种方案，只输出正文。"_ustr
                              : u"自然续写当前段落 1–3 句，只输出续写正文。"_ustr;
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
        if (bMulti)
        {
            prompt.append(u"可选：若存在两种合理公式，用 ===方案1=== / ===方案2=== 分隔；"
                          u"每方案第一行仍须是 = 公式。\n"_ustr);
        }
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
        if (bMulti)
        {
            prompt.append(u"若为页内改写（非大纲），可给 2 种文案方案并用 ===方案1=== / ===方案2=== 分隔。\n"_ustr);
        }
        prompt.append(u"指令："_ustr);
        prompt.append(instruction);
        prompt.append(u"\n选区("_ustr);
        prompt.append(m_aSel.position);
        prompt.append(u")：\n"_ustr);
        prompt.append(m_aSel.text);
        appendCaretContext(prompt, m_aSel, nCtx);
        if (cap.isEmpty())
            cap = u"rewrite"_ustr;
    }
    else if (m_aSel.length > 0 && m_eMode != InlineMode::Complete)
    {
        if (bMulti)
        {
            prompt.append(u"【内联编辑 Ctrl+K · 多方案】按用户指令改写选区。\n"_ustr);
            prompt.append(u"请给出 2 种不同改写方案（语气/详略可略有差异，事实一致）。\n"_ustr);
            prompt.append(u"严格用以下分隔，方案内只写正文、不要解释：\n"_ustr);
            prompt.append(u"===方案1===\n（改写正文）\n===方案2===\n（改写正文）\n"_ustr);
        }
        else
        {
            prompt.append(u"【内联编辑 Ctrl+K · 单方案高速】按用户指令改写选区。\n"_ustr);
            prompt.append(u"只输出改写后的正文，不要解释、不要引号、不要 Markdown 围栏。\n"_ustr);
        }
        prompt.append(u"指令："_ustr);
        prompt.append(instruction);
        prompt.append(u"\n选区：\n"_ustr);
        prompt.append(m_aSel.text);
        // Surrounding sentence context improves fidelity without full-doc bind cost.
        const OUString before = capBefore(m_aSel, std::min(nCtx, sal_Int32(240)));
        const OUString after = capAfter(m_aSel, 120);
        if (!before.isEmpty() || !after.isEmpty())
        {
            prompt.append(u"\n【邻接上下文 · 仅供语气参考，勿写入方案】"_ustr);
            if (!before.isEmpty())
            {
                prompt.append(u"\n前："_ustr);
                prompt.append(before);
            }
            if (!after.isEmpty())
            {
                prompt.append(u"\n后："_ustr);
                prompt.append(after);
            }
        }
        if (cap.isEmpty())
            cap = u"rewrite"_ustr;
    }
    else
    {
        // Ghost / complete — quality hinges on before/after, not full skeleton.
        if (bMulti)
        {
            prompt.append(u"【内联续写/补全 · 多方案 · 光标旁】在光标处自然续写 1–3 句。\n"_ustr);
            prompt.append(u"请给出 2 种续写方案，严格分隔：\n"_ustr);
            prompt.append(u"===方案1===\n（续写正文）\n===方案2===\n（续写正文）\n"_ustr);
        }
        else
        {
            prompt.append(u"【内联续写/补全 · 单方案高速 · 光标旁】在光标处自然续写 1–3 句。\n"_ustr);
            prompt.append(u"只输出续写正文。\n"_ustr);
        }
        prompt.append(u"硬性规则：\n"_ustr);
        prompt.append(u"1) 不要解释、不要引号、不要 Markdown 围栏；\n"_ustr);
        prompt.append(u"2) 勿重复【光标前】已出现的句子或半句；\n"_ustr);
        prompt.append(u"3) 若有【光标后】，续写须与之自然衔接且不冲突；\n"_ustr);
        prompt.append(u"4) 保持与上文一致的语体/人称/时态。\n"_ustr);
        prompt.append(u"指令："_ustr);
        prompt.append(instruction);
        appendCaretContext(prompt, m_aSel, nCtx);
        if (cap.isEmpty())
            cap = u"background"_ustr; // light slot for speed
    }

    // M2.3: multi-phase generating narrative (blocking call; labels bookend the wait).
    if (m_xGhostBanner)
    {
        m_xGhostBanner->set_visible(true);
        m_xGhostBanner->set_label(
            u"⏳ 可圈 AI 生成中 · 主文档未改 · ①准备提示 → ②调用模型…"_ustr);
    }
    {
        const sal_Int32 nBeforeLen = capBefore(m_aSel, nCtx).getLength();
        const sal_Int32 nAfterLen = capAfter(m_aSel, nCtx / 2).getLength();
        m_xStatus->set_label(u"生成中 · ① 上下文前"_ustr + OUString::number(nBeforeLen)
                             + u"+后"_ustr + OUString::number(nAfterLen) + u" 字 · ② 请求（"_ustr
                             + cap + (bMulti ? u" · 多方案）…"_ustr : u" · 单方案高速）…"_ustr));
    }
    m_xGenerate->set_sensitive(false);
    m_xAccept->set_sensitive(false);
    Application::Reschedule(true);

    const css::ai::ProviderResponse rsp = callProvider(prompt.makeStringAndClear(), cap);

    m_xGenerate->set_sensitive(true);
    if (rsp.status != u"ok"_ustr || rsp.content.trim().isEmpty())
    {
        m_bHasPreview = false;
        m_sPreviewText.clear();
        m_aVariants.clear();
        m_xPreview->set_text(OUString());
        OUString failZh = u"生成失败"_ustr;
        if (rsp.status == u"policy-denied"_ustr)
            failZh = u"策略拒绝（当前服务模式不允许该能力）"_ustr;
        else if (rsp.status == u"provider-error"_ustr)
            failZh = u"模型不可用 · 请检查 Ollama/网关与五槽配置"_ustr;
        else if (rsp.status == u"timeout"_ustr
                 || rsp.content.indexOf(u"timeout"_ustr) >= 0
                 || rsp.content.indexOf(u"超时"_ustr) >= 0)
            failZh = u"生成超时 · 可缩短上下文或关多方案后重试（R）"_ustr;
        const OUString detail
            = rsp.content.isEmpty() ? rsp.status : rsp.content.trim();
        if (m_xGhostBanner)
            m_xGhostBanner->set_label(u"生成失败 · 主文档未改 · R 重试 / Esc 关闭"_ustr);
        m_xStatus->set_label(failZh + u"： "_ustr + detail
                             + u" · 主文档未改 · R 重试 · Esc 关闭"_ustr);
        m_xAccept->set_sensitive(false);
        // Keep focus so R / Esc work without clicking back.
        if (m_xReject)
            m_xReject->grab_focus();
        return;
    }

    OUString raw = polishPreviewBody(rsp.content);
    if (bMulti)
        m_aVariants = splitVariants(raw);
    else
    {
        m_aVariants.clear();
        if (!raw.isEmpty())
            m_aVariants.push_back(raw);
    }
    // Polish each variant body (strip residual markers/fences).
    for (auto& v : m_aVariants)
        v = polishPreviewBody(v);
    // Drop empty after polish
    m_aVariants.erase(std::remove_if(m_aVariants.begin(), m_aVariants.end(),
                                     [](const OUString& s) { return s.isEmpty(); }),
                      m_aVariants.end());
    if (m_aVariants.empty() && !raw.isEmpty())
        m_aVariants.push_back(raw);

    m_nVariant = 0;
    m_sPreviewText = m_aVariants.empty() ? raw : m_aVariants[0];
    m_bHasPreview = !m_sPreviewText.isEmpty();
    m_xAccept->set_sensitive(m_bHasPreview);
    updatePreviewDisplay();
    if (m_xGhostBanner)
    {
        m_xGhostBanner->set_visible(m_eMode == InlineMode::Complete);
        m_xGhostBanner->set_label(m_aVariants.size() > 1
                                      ? u"✓ 预览就绪 · ←→ 切换方案 · Tab 批准写回 · Esc 拒绝"_ustr
                                      : u"✓ 预览就绪 · Tab 批准写回 · Esc 拒绝"_ustr);
    }
    const sal_Int32 nVar = static_cast<sal_Int32>(m_aVariants.empty() ? 1 : m_aVariants.size());
    m_xStatus->set_label(u"生成完成 · "_ustr + OUString::number(nVar) + u" 个方案 · "
                         + OUString::number(m_sPreviewText.getLength())
                         + u" 字 · ←→ 选择 · Tab 批准 · Esc 拒绝 · R 重试 · 证据="_ustr
                         + (rsp.evidenceId.isEmpty() ? u"—"_ustr : rsp.evidenceId));
    // Cursor efficiency: focus accept path so Tab works without extra click.
    if (m_bHasPreview)
    {
        if (m_xPreview)
            m_xPreview->grab_focus();
        else if (m_xAccept)
            m_xAccept->grab_focus();
    }
}

bool AIInlineEditPopover::applyPreview()
{
    if (!m_bHasPreview || m_sPreviewText.isEmpty())
        return false;

    // Drop ExtText composition first (empty End → no insert). Real write via ApplyPlan only.
    clearExtTextGhost();
    hideGhostTip();

    // Snapshot mode/surface intent before re-capture (caret may move slightly).
    const InlineMode eApplyMode = m_eMode;
    const OUString sBody = polishPreviewBody(m_sPreviewText);
    if (sBody.isEmpty())
        return false;
    m_sPreviewText = sBody;

    // Re-resolve target at approve time (stale-plan defense); keep surface if capture flaky.
    const OUString sPrevSurface = m_aSel.surface;
    const OUString sPrevPos = m_aSel.position;
    const sal_Int32 nPrevLen = m_aSel.length;
    const OUString sPrevText = m_aSel.text;
    m_aSel = kqoffice::ai::chat::AgentChatSelectionCapture::captureCurrent();
    if (m_aSel.surface.isEmpty() || m_aSel.surface == u"none"_ustr)
        m_aSel.surface = sPrevSurface;
    // Ghost insert: prefer fresh caret; if capture lost position, fall back.
    if (eApplyMode == InlineMode::Complete || nPrevLen == 0)
    {
        if (m_aSel.position.isEmpty())
            m_aSel.position = sPrevPos;
        // Don't accidentally replace a newly selected range when user only meant insert.
        if (m_aSel.length > 0 && nPrevLen == 0 && m_aSel.text != sPrevText)
        {
            m_aSel.length = 0;
            m_aSel.text.clear();
        }
    }
    else if (m_aSel.length == 0 && nPrevLen > 0)
    {
        // Selection lost between preview and Tab — restore original replace target text.
        m_aSel.length = nPrevLen;
        m_aSel.text = sPrevText;
        if (m_aSel.position.isEmpty())
            m_aSel.position = sPrevPos;
    }

    kqoffice::ai::chat::ApplyPlan plan;
    plan.planId = (eApplyMode == InlineMode::Complete) ? u"ap-ghost-complete"_ustr
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
        const bool bReplace = m_aSel.length > 0 && eApplyMode != InlineMode::Complete;
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

    // Tab / 批准写回 = explicit human approval (same chain as sidebar DiffReview).
    const auto result
        = kqoffice::ai::chat::DocumentAIApply::applyApprovedWithRawFallback(plan, m_sPreviewText);
    if (!result.success)
    {
        const OUString sFailZh = kqoffice::ai::chat::DocumentAIApply::userFacingErrorZh(
            result.error, result.engine, result.surface);
        m_xStatus->set_label(u"写回失败："_ustr + sFailZh
                             + u" · 主文档未改 · 可改指令后重试"_ustr);
        return false;
    }
    // Open shared DiffReview deck when available (writer-apply-engine opens its own).
    if (result.engine != u"writer-apply-engine"_ustr && m_xPreview)
    {
#if defined(MACOSX) || defined(LINUX) || defined(FREEBSD) || defined(NETBSD) || defined(OPENBSD) \
    || defined(DRAGONFLY) || defined(ANDROID) || defined(EMSCRIPTEN)
        using ShowFn = void (*)(void*, const sal_Unicode*, sal_Int32, const sal_Unicode*, sal_Int32,
                                const sal_Unicode*, sal_Int32, const sal_Unicode*, sal_Int32, sal_Bool);
        if (void* pSym = dlsym(RTLD_DEFAULT, "kqoffice_show_diff_review"))
        {
            auto pFn = reinterpret_cast<ShowFn>(pSym);
            const OUString patchId = u"p1"_ustr;
            const OUString kind = u"replace"_ustr;
            const OUString status = u"ok"_ustr;
            pFn(m_xPreview.get(), plan.planId.getStr(), plan.planId.getLength(), patchId.getStr(),
                patchId.getLength(), kind.getStr(), kind.getLength(), status.getStr(),
                status.getLength(), sal_True);
        }
#endif
    }
    m_xStatus->set_label(
        u"已批准写回 · 可撤销（Ctrl/Cmd+Z）· 差异审阅已打开 · "_ustr
        + kqoffice::ai::chat::DocumentAIApply::userFacingEngineZh(result.engine) + u" · "_ustr
        + kqoffice::ai::chat::DocumentAIApply::userFacingSurfaceZh(result.surface.isEmpty()
                                                                       ? m_aSel.surface
                                                                       : result.surface));
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
    // Debounce ranking so typing stays fluid (Cursor palette feel).
    m_aSuggestDebounce.Stop();
    m_aSuggestDebounce.Start();
}

IMPL_LINK_NOARG(AIInlineEditPopover, OnSuggestDebounce, Timer*, void)
{
    if (m_xInstruction)
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
            runChip(u"为当前/各页写 30–60 秒口播讲稿，不改幻灯正文。输出：\n"
                    u"===可圈讲稿写回===\n"
                    u"slide:页号|讲稿|口播全文\n"_ustr,
                    u"chat"_ustr);
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
    // Multi-choice: ← → or 1/2/3 pick variant; R regenerate
    if (m_bHasPreview && m_aVariants.size() > 1)
    {
        if (nKey == KEY_LEFT || nKey == KEY_UP)
        {
            cycleVariant(-1);
            return true;
        }
        if (nKey == KEY_RIGHT || nKey == KEY_DOWN)
        {
            // Prefer variant cycle when preview exists; else fall through to suggest list
            if (m_xPreview->has_focus() || m_xAccept->has_focus() || !m_xSuggest->has_focus())
            {
                cycleVariant(1);
                return true;
            }
        }
        if (nKey == KEY_1 || nKey == KEY_2 || nKey == KEY_3)
        {
            const sal_Int32 idx = (nKey == KEY_1) ? 0 : (nKey == KEY_2 ? 1 : 2);
            if (idx < static_cast<sal_Int32>(m_aVariants.size()))
            {
                setActiveVariant(idx);
                return true;
            }
        }
    }
    if ((nKey == KEY_R || nKey == KEY_F5) && !rCode.IsMod1() && !rCode.IsShift())
    {
        // Quick regenerate (Cursor-like retry). Never steal R while typing in instruction.
        const bool bTypingInstr
            = m_xInstruction && m_xInstruction->get_visible() && m_xInstruction->has_focus();
        if (!bTypingInstr
            && (m_eMode == InlineMode::Complete || m_bHasPreview || m_xPreview->has_focus()
                || m_xAccept->has_focus() || m_xReject->has_focus()))
        {
            generate();
            return true;
        }
    }
    if (nKey == KEY_DOWN && m_xSuggest->n_children() > 0 && !m_bHasPreview)
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

/// Walk accessibility tree for focused XAccessibleText; return caret char bounds in screen px
/// relative to rWin client area. Cursor-style: anchor ghost UI under the caret, not window center.
bool tryCaretRectInWindow(vcl::Window& rWin, tools::Rectangle& rOut)
{
    try
    {
        rtl::Reference<comphelper::OAccessible> xRoot = rWin.GetAccessible(/*bCreate*/ true);
        if (!xRoot.is())
            return false;

        // BFS limited depth for focused text accessible
        std::vector<css::uno::Reference<css::accessibility::XAccessible>> queue;
        queue.push_back(css::uno::Reference<css::accessibility::XAccessible>(xRoot));
        for (size_t qi = 0; qi < queue.size() && qi < 80; ++qi)
        {
            auto xAcc = queue[qi];
            if (!xAcc.is())
                continue;
            auto xCtx = xAcc->getAccessibleContext();
            if (!xCtx.is())
                continue;

            css::uno::Reference<css::accessibility::XAccessibleText> xText(xAcc, css::uno::UNO_QUERY);
            if (xText.is())
            {
                sal_Int32 caret = -1;
                try
                {
                    caret = xText->getCaretPosition();
                }
                catch (...)
                {
                    caret = -1;
                }
                if (caret < 0)
                    caret = xText->getCharacterCount();
                if (caret > 0)
                    --caret; // bounds of char before caret (insert point)
                if (caret < 0)
                    caret = 0;
                css::awt::Rectangle aBound;
                try
                {
                    aBound = xText->getCharacterBounds(caret);
                }
                catch (...)
                {
                    aBound = css::awt::Rectangle();
                }
                // Component origin on screen
                css::uno::Reference<css::accessibility::XAccessibleComponent> xComp(
                    xAcc, css::uno::UNO_QUERY);
                sal_Int32 ox = 0, oy = 0;
                if (xComp.is())
                {
                    try
                    {
                        auto loc = xComp->getLocationOnScreen();
                        ox = loc.X;
                        oy = loc.Y;
                    }
                    catch (...)
                    {
                    }
                }
                const AbsoluteScreenPixelPoint aWinScreen
                    = rWin.OutputToAbsoluteScreenPixel(Point(0, 0));
                // Convert absolute screen → window-local
                const tools::Long lx = static_cast<tools::Long>(ox + aBound.X) - aWinScreen.X();
                const tools::Long ly = static_cast<tools::Long>(oy + aBound.Y) - aWinScreen.Y();
                const tools::Long lw = std::max<tools::Long>(aBound.Width, 8);
                const tools::Long lh = std::max<tools::Long>(aBound.Height, 16);
                if (lw > 0 && lh > 0 && lx > -2000 && ly > -2000)
                {
                    // Anchor strip under caret for popover
                    rOut = tools::Rectangle(Point(lx, ly + lh + 2),
                                            Size(std::max(lw, tools::Long(280)), 36));
                    return true;
                }
            }

            const sal_Int64 nChild = xCtx->getAccessibleChildCount();
            const sal_Int64 nLimit = std::min<sal_Int64>(nChild, 24);
            for (sal_Int64 i = 0; i < nLimit; ++i)
            {
                try
                {
                    queue.push_back(xCtx->getAccessibleChild(i));
                }
                catch (...)
                {
                }
            }
        }
    }
    catch (...)
    {
    }
    return false;
}

tools::Rectangle resolveInlineAnchor(SfxViewFrame& rFrame, InlineMode eMode)
{
    vcl::Window& rWin = rFrame.GetWindow();
    tools::Rectangle aWin(Point(0, 0), rWin.GetSizePixel());
    tools::Rectangle aCaret;
    if (tryCaretRectInWindow(rWin, aCaret))
    {
        // Complete mode: tight under caret; Edit mode: slightly wider card
        if (eMode == InlineMode::Complete)
        {
            const tools::Long nw = std::min<tools::Long>(440, aWin.GetWidth() - 24);
            aCaret.SetSize(Size(nw, 40));
            // Keep inside window
            if (aCaret.Right() > aWin.Right() - 12)
                aCaret.Move(aWin.Right() - 12 - aCaret.Right(), 0);
            if (aCaret.Left() < 12)
                aCaret.Move(12 - aCaret.Left(), 0);
            return aCaret;
        }
        {
            const tools::Long nw = std::min<tools::Long>(520, aWin.GetWidth() - 40);
            aCaret.SetSize(Size(nw, 48));
        }
        if (aCaret.Right() > aWin.Right() - 16)
            aCaret.Move(aWin.Right() - 16 - aCaret.Right(), 0);
        return aCaret;
    }
    // Fallback: lower-center of edit area (still better than top-center for typing flow)
    const tools::Long w
        = eMode == InlineMode::Complete ? aWin.GetWidth() / 2 : aWin.GetWidth() * 3 / 5;
    const tools::Long x = (aWin.GetWidth() - w) / 2;
    const tools::Long y = aWin.GetHeight() * 55 / 100;
    return tools::Rectangle(Point(x, y), Size(w, 40));
}

void ShowInlineOnMainThread(SfxViewFrame& rFrame, InlineMode eMode);
void ShowAsync(SfxViewFrame& rFrame, InlineMode eMode);

void ShowInlineOnMainThread(SfxViewFrame& rFrame, InlineMode eMode)
{
    tools::Rectangle aRectangle = resolveInlineAnchor(rFrame, eMode);
    weld::Window* pParent = weld::GetPopupParent(rFrame.GetWindow(), aRectangle);
    if (!pParent)
        return;
    hideGhostTip();
    g_pActive.reset();
    g_pActive = std::make_unique<AIInlineEditPopover>(pParent, aRectangle, eMode,
                                                      &rFrame.GetWindow());
    g_pActive->show();
}

// —— Typing-idle auto ghost (default off; prefs.inlineAutoGhost) ——
class AutoGhostWatch
{
public:
    static AutoGhostWatch& get()
    {
        static AutoGhostWatch inst;
        return inst;
    }

    void reconfigure()
    {
        bool bEnabled = false;
        sal_Int32 nIdle = 900;
        try
        {
            const auto p = kqoffice::ai::chat::DocumentAIInputPrefs::load();
            bEnabled = p.inlineAutoGhost;
            nIdle = p.inlineAutoGhostIdleMs;
        }
        catch (...)
        {
        }
        m_nIdleMs = nIdle;
        m_aTimer.SetTimeout(static_cast<sal_uInt64>(std::max(sal_Int32(400), m_nIdleMs)));
        if (bEnabled && !m_bListening)
        {
            Application::AddKeyListener(LINK(this, AutoGhostWatch, OnKey));
            m_bListening = true;
        }
        else if (!bEnabled && m_bListening)
        {
            Application::RemoveKeyListener(LINK(this, AutoGhostWatch, OnKey));
            m_aTimer.Stop();
            m_bListening = false;
        }
        if (!bEnabled)
            m_aTimer.Stop();
    }

private:
    AutoGhostWatch()
        : m_aTimer("AIInlineAutoGhostIdle")
    {
        m_aTimer.SetInvokeHandler(LINK(this, AutoGhostWatch, OnIdle));
        m_aTimer.SetTimeout(900);
    }

    DECL_LINK(OnKey, VclWindowEvent&, bool);
    DECL_LINK(OnIdle, Timer*, void);

    bool isTypingKey(const KeyEvent& rKEvt) const
    {
        const vcl::KeyCode& c = rKEvt.GetKeyCode();
        if (c.IsMod1() || c.IsMod2() || c.IsMod3())
            return false;
        const sal_uInt16 n = c.GetCode();
        // Don't fire on navigation / accept / escape
        if (n == KEY_ESCAPE || n == KEY_TAB || n == KEY_RETURN || n == KEY_UP || n == KEY_DOWN
            || n == KEY_LEFT || n == KEY_RIGHT || n == KEY_HOME || n == KEY_END || n == KEY_PAGEUP
            || n == KEY_PAGEDOWN || n == KEY_DELETE || n == KEY_BACKSPACE)
            return false;
        // Printable / compose chars, or GetCharCode for CJK input
        if (rKEvt.GetCharCode() != 0)
            return true;
        // ASCII range without modifiers
        if (n >= KEY_A && n <= KEY_Z)
            return true;
        if (n >= KEY_0 && n <= KEY_9)
            return true;
        if (n == KEY_SPACE || n == KEY_COMMA || n == KEY_POINT || n == KEY_SEMICOLON)
            return true;
        return false;
    }

    Timer m_aTimer;
    bool m_bListening = false;
    sal_Int32 m_nIdleMs = 900;
    std::chrono::steady_clock::time_point m_lastFire{};
};

IMPL_LINK(AutoGhostWatch, OnKey, VclWindowEvent&, rEvt, bool)
{
    // Never consume — we only observe typing.
    if (rEvt.GetId() != VclEventId::WindowKeyInput)
        return false;
    auto* pKey = static_cast<KeyEvent*>(rEvt.GetData());
    if (!pKey || !isTypingKey(*pKey))
        return false;
    if (g_pActive)
        return false;
    // Modal dialogs / menu: skip
    if (Application::IsInExecute())
        return false;
    m_aTimer.Stop();
    m_aTimer.Start();
    return false;
}

IMPL_LINK_NOARG(AutoGhostWatch, OnIdle, Timer*, void)
{
    try
    {
        const auto prefs = kqoffice::ai::chat::DocumentAIInputPrefs::load();
        if (!prefs.inlineAutoGhost)
            return;
    }
    catch (...)
    {
        return;
    }
    if (g_pActive)
        return;
    SfxViewFrame* pFrame = SfxViewFrame::Current();
    if (!pFrame || !IsLiveViewFrame(pFrame))
        return;
    // Rate-limit: avoid hammering provider while user keeps typing between idles.
    const auto now = std::chrono::steady_clock::now();
    if (m_lastFire.time_since_epoch().count() != 0
        && std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastFire).count() < 2500)
        return;

    const auto sel = kqoffice::ai::chat::AgentChatSelectionCapture::captureCurrent();
    if (sel.length > 0)
        return; // selection → user likely wants Ctrl+K edit, not auto complete
    // Need a bit of before-context so we don't complete empty docs.
    const OUString before = capBefore(sel, 80);
    if (before.getLength() < 4)
        return;
    // Surface guard: only document surfaces
    if (sel.surface != u"writer"_ustr && sel.surface != u"calc"_ustr
        && sel.surface != u"impress"_ustr)
        return;

    m_lastFire = now;
    ShowAsync(*pFrame, InlineMode::Complete);
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
    static bool bOnce = false;
    if (!bOnce)
    {
        bOnce = true;
        aInstance.EnsureAutoGhostWatch();
    }
    return aInstance;
}

void AIInlineEditDispatcher::Show(SfxViewFrame& rFrame) { ShowAsync(rFrame, InlineMode::Edit); }

void AIInlineEditDispatcher::ShowComplete(SfxViewFrame& rFrame)
{
    ShowAsync(rFrame, InlineMode::Complete);
}

void AIInlineEditDispatcher::EnsureAutoGhostWatch() { AutoGhostWatch::get().reconfigure(); }

bool AIInlineEditDispatcher::IsActive() { return static_cast<bool>(g_pActive); }

} // namespace sfx2

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
