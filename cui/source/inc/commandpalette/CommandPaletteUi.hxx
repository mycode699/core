/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * V2 W2 Day-1b — popover entry point registered with sfx2::CommandPaletteDispatcher.
 */

#ifndef INCLUDED_CUI_SOURCE_INC_COMMANDPALETTE_COMMANDPALETTEUI_HXX
#define INCLUDED_CUI_SOURCE_INC_COMMANDPALETTE_COMMANDPALETTEUI_HXX

#include <sal/types.h>

class SfxViewFrame;

namespace cui::commandpalette
{
SAL_DLLPUBLIC_EXPORT void ShowCommandPalette(SfxViewFrame& rFrame);
SAL_DLLPUBLIC_EXPORT void HideCommandPalette();

} // namespace cui::commandpalette

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */