/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * V2 W2 Day-1b — Cmd+K command palette dispatcher (sfx2 core).
 * Spec: docs/superpowers/specs/2026-05-12-w2-day1b-design.md
 */

#pragma once

#include <sfx2/dllapi.h>

#include <map>
#include <rtl/ustring.hxx>
#include <sal/types.h>

class SfxViewFrame;

namespace sfx2
{
using CommandPaletteShowFn = void (*)(SfxViewFrame&);

class SFX2_DLLPUBLIC CommandPaletteDispatcher
{
public:
    static CommandPaletteDispatcher& Get();

    static void RegisterShowPaletteHook(CommandPaletteShowFn fn);

    void ShowPalette(SfxViewFrame& rFrame);
    /// Increments frequency; no-ops on empty / `.uno:CommandPalette` (recursion guard).
    void trackCommandUse(OUString const& rUrl);
    /// Returns true when the UNO dispatch succeeded.
    bool dispatchUrl(SfxViewFrame& rFrame, OUString const& rUrl);
    /// Opens the V3 W1 chat sidebar deck for non-command CommandPalette text.
    bool dispatchChatFallback(SfxViewFrame& rFrame, OUString const& rPrompt);
    sal_uInt32 frequency(OUString const& rUrl) const;

private:
    CommandPaletteDispatcher() = default;

    std::map<OUString, sal_uInt32> m_frequency;
};

} // namespace sfx2

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
