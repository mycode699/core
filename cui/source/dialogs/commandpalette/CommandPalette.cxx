/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W2: Cmd+K Command Palette).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <commandpalette/CommandPalette.hxx>

namespace cui::commandpalette
{
CommandPaletteController::CommandPaletteController() = default;

void CommandPaletteController::setCorpus(std::vector<CommandEntry> corpus)
{
    m_corpus = std::move(corpus);
}

std::vector<ScoredEntry> CommandPaletteController::queryToResults(
    const OUString& query) const
{
    return FuzzyMatcher::match(query, m_corpus);
}

} // namespace cui::commandpalette

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
