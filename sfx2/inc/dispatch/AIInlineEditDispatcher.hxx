/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Cursor-style Ctrl/Cmd+K inline AI edit + Tab-complete for 可圈office.
 */
#pragma once

#include <sfx2/dllapi.h>

class SfxViewFrame;

namespace sfx2
{
/// Opens the AI inline-edit popover (select → instruct → preview → Tab accept).
class SFX2_DLLPUBLIC AIInlineEditDispatcher
{
public:
    static AIInlineEditDispatcher& Get();

    /// Full edit UI (Ctrl/Cmd+K). Empty selection → auto 续写 mode.
    void Show(SfxViewFrame& rFrame);

    /// Ghost-style complete at caret (Ctrl/Cmd+Alt+Space): open + auto-generate via light slot.
    void ShowComplete(SfxViewFrame& rFrame);

private:
    AIInlineEditDispatcher() = default;
};
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
