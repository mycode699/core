/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * V2 W2 Day-1b — reuses stock CommandPopup UI until cui loader lands (D3b).
 */

#include <dispatch/CommandPaletteDispatcher.hxx>

#include <commandpopup/CommandPopup.hxx>
#include <comphelper/dispatchcommand.hxx>
#include <sfx2/msg.hxx>
#include <sfx2/msgpool.hxx>
#include <sfx2/viewfrm.hxx>

#include <com/sun/star/beans/PropertyValue.hpp>
#include <sal/log.hxx>
#include <tools/gen.hxx>
#include <vcl/svapp.hxx>
#include <vcl/window.hxx>
#include <vcl/weld/weld.hxx>
#include <vcl/weld/weldutils.hxx>

#include <memory>

namespace sfx2
{
namespace
{
CommandPaletteShowFn g_pShowPaletteHook = nullptr;
constexpr OUStringLiteral COMMAND_PALETTE_CHAT_FALLBACK_URL = u".uno:SidebarDeck.AIChatDeck";

struct PendingShowPalette
{
    SfxViewFrame* pFrame;
};

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

void ShowPaletteOnMainThread(SfxViewFrame& rFrame)
{
    if (g_pShowPaletteHook)
    {
        g_pShowPaletteHook(rFrame);
        return;
    }

    tools::Rectangle aRectangle(Point(0, 0), rFrame.GetWindow().GetSizePixel());
    weld::Window* pParent = weld::GetPopupParent(rFrame.GetWindow(), aRectangle);
    CommandPopupHandler aHandler;
    aHandler.showPopup(pParent, rFrame.GetFrame().GetFrameInterface());
}

void ShowPaletteAsync(void*, void* pArg)
{
    std::unique_ptr<PendingShowPalette> pPending(static_cast<PendingShowPalette*>(pArg));
    if (!pPending->pFrame || !IsLiveViewFrame(pPending->pFrame))
    {
        SAL_WARN("sfx.commandpalette", "CommandPaletteDispatcher: view frame disappeared");
        return;
    }

    ShowPaletteOnMainThread(*pPending->pFrame);
}
}

CommandPaletteDispatcher& CommandPaletteDispatcher::Get()
{
    static CommandPaletteDispatcher aInstance;
    return aInstance;
}

void CommandPaletteDispatcher::RegisterShowPaletteHook(CommandPaletteShowFn fn)
{
    g_pShowPaletteHook = fn;
}

void CommandPaletteDispatcher::ShowPalette(SfxViewFrame& rFrame)
{
    if (!Application::IsMainThread())
    {
        auto pPending = std::make_unique<PendingShowPalette>();
        pPending->pFrame = &rFrame;
        if (Application::PostUserEvent(LINK_NONMEMBER(nullptr, ShowPaletteAsync), pPending.get()))
        {
            pPending.release();
            return;
        }

        SAL_WARN("sfx.commandpalette", "CommandPaletteDispatcher: failed to post UI event");
        return;
    }

    ShowPaletteOnMainThread(rFrame);
}

void CommandPaletteDispatcher::trackCommandUse(OUString const& rUrl)
{
    if (rUrl.isEmpty() || rUrl == u".uno:CommandPalette")
        return;
    ++m_frequency[rUrl];
}

bool CommandPaletteDispatcher::dispatchUrl(SfxViewFrame& rFrame, OUString const& rUrl)
{
    trackCommandUse(rUrl);
    if (rUrl.isEmpty() || rUrl == u".uno:CommandPalette")
        return false;

    const SfxSlot* pSlot = SfxSlotPool::GetSlotPool(&rFrame).GetUnoSlot(rUrl);
    if (!pSlot)
    {
        SAL_WARN("sfx", "CommandPaletteDispatcher: unknown slot for " << rUrl);
        return false;
    }

    (void)pSlot;
    if (!comphelper::dispatchCommand(rUrl, css::uno::Sequence<css::beans::PropertyValue>()))
    {
        SAL_WARN("sfx", "CommandPaletteDispatcher: dispatch failed for " << rUrl);
        return false;
    }
    return true;
}

bool CommandPaletteDispatcher::dispatchChatFallback(SfxViewFrame& rFrame, OUString const& rPrompt)
{
    const OUString aPrompt = rPrompt.trim();
    if (aPrompt.isEmpty())
        return false;

    SAL_INFO("sfx.commandpalette", "CommandPaletteDispatcher: chat fallback route");
    return dispatchUrl(rFrame, COMMAND_PALETTE_CHAT_FALLBACK_URL);
}

sal_uInt32 CommandPaletteDispatcher::frequency(OUString const& rUrl) const
{
    auto it = m_frequency.find(rUrl);
    return it == m_frequency.end() ? 0 : it->second;
}

} // namespace sfx2

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
