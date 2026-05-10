/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W2: Cmd+K Command Palette).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * V2 W2 Day-0 — controller skeleton. Wires the .ui glade file to the
 * FuzzyMatcher engine. Day-0 contract:
 *   - Open / close work; ESC dismisses.
 *   - Input field is wired to a debounced search callback (50ms, see spec).
 *   - The corpus is the ad-hoc placeholder set used by tests until the
 *     CommandIndex (W2 Day-1) scans the .xcu files.
 *   - Pressing Enter on the highlighted row will call SfxDispatcher in W2
 *     Day-1; for Day-0 we fire a "selected" signal only.
 * Spec: docs/product/v2/w2-cmd-palette-spec.md.
 */

#ifndef INCLUDED_CUI_SOURCE_INC_COMMANDPALETTE_COMMANDPALETTE_HXX
#define INCLUDED_CUI_SOURCE_INC_COMMANDPALETTE_COMMANDPALETTE_HXX

#include <commandpalette/FuzzyMatcher.hxx>

#include <rtl/ustring.hxx>
#include <vector>

namespace cui::commandpalette
{
/// Minimal Day-0 controller — pure logic, header-only inline impl so the
/// unit test can link against the same definitions without pulling in
/// libcui (mirrors FuzzyMatcher's fdo#47246 layout). The .cxx
/// translation unit is reserved for the popover glue that W2 Day-1b will
/// add (sfx2 dispatch, Enter handler, dismiss-on-ESC) — none of which is
/// pure-logic and none of which is exercised by this test class.
class CommandPaletteController
{
public:
    CommandPaletteController() = default;

    /// Replaces the corpus the matcher searches. Day-0 callers pass an
    /// in-memory list; W2 Day-1 will replace this with a CommandIndex
    /// derived from officecfg .xcu scans.
    void setCorpus(std::vector<CommandEntry> corpus)
    {
        m_corpus = std::move(corpus);
    }

    /// Re-runs FuzzyMatcher against the latest query and returns the
    /// top-N rows. The popover view in CommandPalette.cxx renders this list.
    std::vector<ScoredEntry> queryToResults(const OUString& query) const
    {
        return FuzzyMatcher::match(query, m_corpus);
    }

    /// Read-only access to the current corpus — exposed so tests and the
    /// popover view can verify what setCorpus stored without round-tripping
    /// through queryToResults.
    const std::vector<CommandEntry>& corpus() const { return m_corpus; }

    /// True iff the controller would dispatch the entry at index `i`
    /// of the most recent results. Day-0 invariant: false-by-default;
    /// becomes true once Enter is observed in the popover.
    bool shouldDispatch(std::size_t /*i*/) const { return false; }

private:
    std::vector<CommandEntry> m_corpus;
};

} // namespace cui::commandpalette

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
