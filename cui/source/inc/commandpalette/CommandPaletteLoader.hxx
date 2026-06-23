/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * V2 W2 Day-1b — build command corpus from SfxSlotPool for the active view.
 */

#ifndef INCLUDED_CUI_SOURCE_INC_COMMANDPALETTE_COMMANDPALETTELOADER_HXX
#define INCLUDED_CUI_SOURCE_INC_COMMANDPALETTE_COMMANDPALETTELOADER_HXX

#include <commandpalette/FuzzyMatcher.hxx>

#include <vector>

class SfxViewFrame;

namespace cui::commandpalette
{
class CommandPaletteLoader
{
public:
    static std::vector<CommandEntry> buildCorpus(SfxViewFrame& rFrame);
};

} // namespace cui::commandpalette

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */