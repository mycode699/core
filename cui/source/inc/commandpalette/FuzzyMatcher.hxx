/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W2: Cmd+K Command Palette).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * V2 W2 Day-0 — pure-logic fuzzy match.
 *
 * NOTE: The scoring algorithm is defined inline in this header so the
 * cppunit test can link against it without duplicating .cxx objects
 * that already live in libcui (fdo#47246 forbids double-linkage). When
 * W2 Day-1 adds state (recency persistence, async indexing), the impl
 * can move to FuzzyMatcher.cxx — at that point, expose needed symbols
 * through the cui UNO surface or split into a dedicated helper lib.
 *
 * Spec: docs/product/v2/w2-cmd-palette-spec.md §"Fuzzy Score 算法".
 */

#ifndef INCLUDED_CUI_SOURCE_INC_COMMANDPALETTE_FUZZYMATCHER_HXX
#define INCLUDED_CUI_SOURCE_INC_COMMANDPALETTE_FUZZYMATCHER_HXX

#include <rtl/ustring.hxx>
#include <algorithm>
#include <cstddef>
#include <vector>

namespace cui::commandpalette
{
struct CommandEntry
{
    OUString unoCommand;   ///< ".uno:Bold"
    OUString labelEn;      ///< "Bold"
    OUString labelZh;      ///< "粗体"
    OUString pinyinFirst;  ///< "ct" — first letters of "cuti"
    OUString pinyinFull;   ///< "cuti"
    int      frequency = 0;
};

struct ScoredEntry
{
    const CommandEntry* entry; ///< borrowed; matcher does not own
    int score;
};

/// Pure scoring logic — no I/O, no UNO calls.
/// Header-only so test + library share the single definition without
/// creating the fdo#47246 "linked in twice" collision.
class FuzzyMatcher
{
public:
    /// Score one entry against query.
    /// Algorithm (mirrors W2 spec):
    ///   +100  query == labelZh or labelEn (case-insensitive exact)
    ///   + 80  query is a prefix of pinyinFirst (case-insensitive)
    ///   + 60  labelZh contains query
    ///   + 40  labelEn contains query (case-insensitive)
    ///   +(frequency / 10) recency boost
    /// Empty query → score 0 for every entry.
    static inline int score(const OUString& query, const CommandEntry& entry)
    {
        if (query.isEmpty())
            return 0;

        int s = 0;
        if (entry.labelZh.equalsIgnoreAsciiCase(query)
            || entry.labelEn.equalsIgnoreAsciiCase(query))
        {
            s += 100;
        }
        if (!entry.pinyinFirst.isEmpty()
            && entry.pinyinFirst.startsWithIgnoreAsciiCase(query))
        {
            s += 80;
        }
        if (!entry.labelZh.isEmpty() && entry.labelZh.indexOf(query) >= 0)
        {
            s += 60;
        }
        if (!entry.labelEn.isEmpty()
            && entry.labelEn.toAsciiLowerCase().indexOf(
                   query.toAsciiLowerCase()) >= 0)
        {
            s += 40;
        }
        s += entry.frequency / 10;
        return s;
    }

    /// Returns top-N entries with positive score, descending.
    static inline std::vector<ScoredEntry> match(
        const OUString& query,
        const std::vector<CommandEntry>& corpus,
        std::size_t topN = 8)
    {
        std::vector<ScoredEntry> hits;
        hits.reserve(corpus.size());
        for (const auto& e : corpus)
        {
            int s = score(query, e);
            if (s > 0)
                hits.push_back({&e, s});
        }
        std::sort(hits.begin(), hits.end(),
                  [](const ScoredEntry& a, const ScoredEntry& b) {
                      return a.score > b.score;
                  });
        if (hits.size() > topN)
            hits.resize(topN);
        return hits;
    }
};

} // namespace cui::commandpalette

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
