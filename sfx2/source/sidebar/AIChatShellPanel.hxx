/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Stage1 shell panel: InterimItemWindow-safe first paint for 可圈 AI.
 * Full AIChatPanel remains for later lazy upgrade (aichatpanel_full.ui).
 */

#pragma once

#include <sfx2/sidebar/PanelLayout.hxx>
#include <tools/link.hxx>
#include <vcl/timer.hxx>

#include <memory>

namespace weld
{
class Button;
class Entry;
class Label;
class TextView;
}

namespace sfx2::sidebar
{

/** Lightweight AI chat surface without agent/workspace store construction. */
class AIChatShellPanel final : public PanelLayout
{
public:
    explicit AIChatShellPanel(weld::Widget* pParent);
    ~AIChatShellPanel() override;

private:
    DECL_LINK(OnInjectPollTick, Timer*, void);
    DECL_LINK(OnSendClicked, weld::Button&, void);
    DECL_LINK(OnClearClicked, weld::Button&, void);
    DECL_LINK(OnScenario0, weld::Button&, void);
    DECL_LINK(OnScenario1, weld::Button&, void);
    DECL_LINK(OnScenario2, weld::Button&, void);
    DECL_LINK(OnScenario3, weld::Button&, void);
    DECL_LINK(OnApproveStub, weld::Button&, void);
    DECL_LINK(OnDiffStub, weld::Button&, void);
    DECL_LINK(OnRejectStub, weld::Button&, void);
    DECL_LINK(OnUndoStub, weld::Button&, void);

    void AppendLine(const OUString& rLine);
    void SetStatus(const OUString& rStatus);
    void SeedScenario(std::u16string_view rLabel);
    void ConsumePendingPromptInject();
    void ConsumePendingScenarioRun();

    std::unique_ptr<weld::Label> m_xStatusLabel;
    std::unique_ptr<weld::Label> m_xHintLabel;
    std::unique_ptr<weld::TextView> m_xTranscriptView;
    std::unique_ptr<weld::Entry> m_xPromptEntry;
    std::unique_ptr<weld::Button> m_xSendButton;
    std::unique_ptr<weld::Button> m_xClearButton;
    std::unique_ptr<weld::Button> m_xApproveBtn;
    std::unique_ptr<weld::Button> m_xDiffBtn;
    std::unique_ptr<weld::Button> m_xRejectBtn;
    std::unique_ptr<weld::Button> m_xUndoBtn;
    std::unique_ptr<weld::Button> m_xScenario0;
    std::unique_ptr<weld::Button> m_xScenario1;
    std::unique_ptr<weld::Button> m_xScenario2;
    std::unique_ptr<weld::Button> m_xScenario3;

    AutoTimer m_aInjectPoll;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
