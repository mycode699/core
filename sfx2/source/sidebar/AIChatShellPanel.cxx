/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "AIChatShellPanel.hxx"

#include <osl/file.hxx>
#include <sal/log.hxx>
#include <vcl/timer.hxx>
#include <vcl/weld/Builder.hxx>
#include <vcl/weld/Entry.hxx>
#include <vcl/weld/TextView.hxx>
#include <vcl/weld/weld.hxx>

#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace sfx2::sidebar
{

namespace
{
OUString ReadHomeFile(std::u16string_view rRelPath, bool bRemove)
{
    const char* home = std::getenv("HOME");
    if (!home || !*home)
        return {};
    const OUString path = OUString::fromUtf8(home) + OUString(rRelPath);
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
        if (bRemove)
            osl::File::remove(url);
        return {};
    }
    std::vector<char> buf(static_cast<size_t>(sz));
    sal_uInt64 n = 0;
    f.read(buf.data(), sz, n);
    f.close();
    if (bRemove)
        osl::File::remove(url);
    if (n == 0)
        return {};
    return OUString::fromUtf8(std::string_view(buf.data(), static_cast<size_t>(n))).trim();
}
} // namespace

AIChatShellPanel::AIChatShellPanel(weld::Widget* pParent)
    : PanelLayout(pParent, u"AIChatShellPanel"_ustr, u"sfx/ui/aichatshell.ui"_ustr)
    , m_xStatusLabel(m_xBuilder->weld_label(u"status_label"_ustr))
    , m_xHintLabel(m_xBuilder->weld_label(u"hint_label"_ustr))
    , m_xTranscriptView(m_xBuilder->weld_text_view(u"transcript_view"_ustr))
    , m_xPromptEntry(m_xBuilder->weld_entry(u"prompt_entry"_ustr))
    , m_xSendButton(m_xBuilder->weld_button(u"send_button"_ustr))
    , m_xClearButton(m_xBuilder->weld_button(u"clear_button"_ustr))
    , m_xApproveBtn(m_xBuilder->weld_button(u"chat_approve_btn"_ustr))
    , m_xDiffBtn(m_xBuilder->weld_button(u"chat_diff_btn"_ustr))
    , m_xRejectBtn(m_xBuilder->weld_button(u"chat_reject_btn"_ustr))
    , m_xUndoBtn(m_xBuilder->weld_button(u"chat_undo_btn"_ustr))
    , m_xScenario0(m_xBuilder->weld_button(u"scenario_btn_0"_ustr))
    , m_xScenario1(m_xBuilder->weld_button(u"scenario_btn_1"_ustr))
    , m_xScenario2(m_xBuilder->weld_button(u"scenario_btn_2"_ustr))
    , m_xScenario3(m_xBuilder->weld_button(u"scenario_btn_3"_ustr))
    , m_aInjectPoll("AIChatShellInjectPoll")
{
    if (m_xTranscriptView)
        m_xTranscriptView->set_editable(false);

    if (m_xSendButton)
        m_xSendButton->connect_clicked(LINK(this, AIChatShellPanel, OnSendClicked));
    if (m_xClearButton)
        m_xClearButton->connect_clicked(LINK(this, AIChatShellPanel, OnClearClicked));
    if (m_xScenario0)
        m_xScenario0->connect_clicked(LINK(this, AIChatShellPanel, OnScenario0));
    if (m_xScenario1)
        m_xScenario1->connect_clicked(LINK(this, AIChatShellPanel, OnScenario1));
    if (m_xScenario2)
        m_xScenario2->connect_clicked(LINK(this, AIChatShellPanel, OnScenario2));
    if (m_xScenario3)
        m_xScenario3->connect_clicked(LINK(this, AIChatShellPanel, OnScenario3));
    if (m_xApproveBtn)
        m_xApproveBtn->connect_clicked(LINK(this, AIChatShellPanel, OnApproveStub));
    if (m_xDiffBtn)
        m_xDiffBtn->connect_clicked(LINK(this, AIChatShellPanel, OnDiffStub));
    if (m_xRejectBtn)
        m_xRejectBtn->connect_clicked(LINK(this, AIChatShellPanel, OnRejectStub));
    if (m_xUndoBtn)
        m_xUndoBtn->connect_clicked(LINK(this, AIChatShellPanel, OnUndoStub));

    AppendLine(u"系统: 可圈 AI 侧栏已打开（Stage1 shell）。"_ustr);
    AppendLine(u"系统: 主文档写回须批准；本地优先，不静默上传。"_ustr);

    // AutoTimer: keep polling while shell is open (Timer would be one-shot).
    m_aInjectPoll.SetTimeout(800);
    m_aInjectPoll.SetInvokeHandler(LINK(this, AIChatShellPanel, OnInjectPollTick));
    m_aInjectPoll.Start();
}

AIChatShellPanel::~AIChatShellPanel()
{
    m_aInjectPoll.Stop();
}

void AIChatShellPanel::SetStatus(const OUString& rStatus)
{
    if (m_xStatusLabel)
        m_xStatusLabel->set_label(rStatus);
}

void AIChatShellPanel::AppendLine(const OUString& rLine)
{
    if (!m_xTranscriptView)
        return;
    OUString s = m_xTranscriptView->get_text();
    if (!s.isEmpty())
        s += u"\n"_ustr;
    s += rLine;
    m_xTranscriptView->set_text(s);
    m_xTranscriptView->set_position(-1);
}

void AIChatShellPanel::SeedScenario(std::u16string_view rLabel)
{
    const OUString label(rLabel);
    if (m_xPromptEntry)
    {
        const OUString cur = m_xPromptEntry->get_text();
        if (cur.isEmpty())
            m_xPromptEntry->set_text(u"【"_ustr + label + u"】请基于当前选区/文档给出建议，不要自动改主文档。"_ustr);
        else
            m_xPromptEntry->set_text(u"【"_ustr + label + u"】"_ustr + cur);
    }
    SetStatus(u"已填入方案："_ustr + label + u" · 可编辑后发送"_ustr);
    AppendLine(u"系统: 方案 «"_ustr + label + u"» 已写入输入框（不自动发送）。"_ustr);
}

void AIChatShellPanel::ConsumePendingPromptInject()
{
    const OUString text
        = ReadHomeFile(u"/.config/kqoffice/pending-prompt-inject"_ustr, /*bRemove*/ true);
    if (text.isEmpty())
        return;
    if (m_xPromptEntry)
    {
        const OUString cur = m_xPromptEntry->get_text();
        m_xPromptEntry->set_text(cur.isEmpty() ? text : (cur + u"\n"_ustr + text));
    }
    AppendLine(u"系统: 已注入 prompt（source=inject）len="_ustr
               + OUString::number(text.getLength()));
    SetStatus(u"已附上注入内容 — 可编辑后发送"_ustr);
}

void AIChatShellPanel::ConsumePendingScenarioRun()
{
    const OUString id
        = ReadHomeFile(u"/.config/kqoffice/pending-scenario-run"_ustr, /*bRemove*/ true);
    if (id.isEmpty())
        return;
    // Map common Stage1 ids to visible Chinese seeds.
    OUString label = id;
    if (id == u"content-quality"_ustr || id.indexOf(u"质检"_ustr) >= 0)
        label = u"内容质检"_ustr;
    else if (id.indexOf(u"公文"_ustr) >= 0 || id.indexOf(u"polish"_ustr) >= 0)
        label = u"公文润色"_ustr;
    else if (id.indexOf(u"排版"_ustr) >= 0 || id.indexOf(u"layout"_ustr) >= 0)
        label = u"排版优化"_ustr;
    SeedScenario(label);
}

IMPL_LINK_NOARG(AIChatShellPanel, OnInjectPollTick, Timer*, void)
{
    ConsumePendingPromptInject();
    ConsumePendingScenarioRun();
}

IMPL_LINK_NOARG(AIChatShellPanel, OnSendClicked, weld::Button&, void)
{
    const OUString prompt = m_xPromptEntry ? m_xPromptEntry->get_text().trim() : OUString();
    if (prompt.isEmpty())
    {
        SetStatus(u"请先输入内容"_ustr);
        return;
    }
    AppendLine(u"你: "_ustr + prompt);
    AppendLine(u"系统: Stage1 shell 不自动调用模型；请在完整 AI 面板或 headless 路径验证生成/写回。"_ustr);
    AppendLine(u"系统: 信任约束仍有效 — 主文档写回须批准，不静默上传。"_ustr);
    SetStatus(u"已记录本地对话（shell）· 写回仍须批准"_ustr);
    if (m_xPromptEntry)
        m_xPromptEntry->set_text(OUString());
}

IMPL_LINK_NOARG(AIChatShellPanel, OnClearClicked, weld::Button&, void)
{
    if (m_xTranscriptView)
        m_xTranscriptView->set_text(OUString());
    if (m_xPromptEntry)
        m_xPromptEntry->set_text(OUString());
    SetStatus(u"已清空本地对话显示"_ustr);
}

IMPL_LINK_NOARG(AIChatShellPanel, OnScenario0, weld::Button&, void)
{
    SeedScenario(u"公文润色"_ustr);
}
IMPL_LINK_NOARG(AIChatShellPanel, OnScenario1, weld::Button&, void)
{
    SeedScenario(u"内容质检"_ustr);
}
IMPL_LINK_NOARG(AIChatShellPanel, OnScenario2, weld::Button&, void)
{
    SeedScenario(u"排版优化"_ustr);
}
IMPL_LINK_NOARG(AIChatShellPanel, OnScenario3, weld::Button&, void)
{
    SeedScenario(u"写作"_ustr);
}

IMPL_LINK_NOARG(AIChatShellPanel, OnApproveStub, weld::Button&, void)
{
    SetStatus(u"当前无待批写回（shell）"_ustr);
    AppendLine(u"系统: 批准写回 — 无待批计划（headless/完整面板可验证 ApplyPlan）。"_ustr);
}
IMPL_LINK_NOARG(AIChatShellPanel, OnDiffStub, weld::Button&, void)
{
    SetStatus(u"当前无 Diff 可查看（shell）"_ustr);
    AppendLine(u"系统: 查看 Diff — 无待批计划。"_ustr);
}
IMPL_LINK_NOARG(AIChatShellPanel, OnRejectStub, weld::Button&, void)
{
    SetStatus(u"已拒绝（无待批）"_ustr);
    AppendLine(u"系统: 拒绝 — 无待批计划。"_ustr);
}
IMPL_LINK_NOARG(AIChatShellPanel, OnUndoStub, weld::Button&, void)
{
    SetStatus(u"无本步写回可撤销（shell）"_ustr);
    AppendLine(u"系统: 撤销写回 — shell 不持有 apply 栈；完整面板支持 SID_UNDO。"_ustr);
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
