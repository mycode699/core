/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W2: Cmd+K Command Palette).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Day-0 unit tests for the CommandPaletteController contract — covers
 * the controller-level invariants the FuzzyMatcher / CommandIndex /
 * RecentStore fast tests don't reach: corpus replacement semantics,
 * query routing through the matcher, top-N cap propagation, and the
 * shouldDispatch invariant that gates W2 Day-1b's popover Enter
 * handler.
 *
 * Pure logic — no VCL bring-up, no UNO bootstrap (matches the
 * FuzzyMatcher / RecentStore fast-test layout). Target < 100ms.
 */

#include <sal/types.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/plugin/TestPlugIn.h>

#include <commandpalette/CommandPalette.hxx>

using cui::commandpalette::CommandEntry;
using cui::commandpalette::CommandPaletteController;
using cui::commandpalette::ScoredEntry;

namespace
{
class CommandPaletteControllerTest : public CppUnit::TestFixture
{
public:
    void testFreshControllerHasEmptyCorpus();
    void testSetCorpusStoresEntries();
    void testSetCorpusReplacesPreviousCorpus();
    void testEmptyQueryReturnsEmpty();
    void testQueryRoutesThroughFuzzyMatcher();
    void testQueryRespectsTopNDefault();
    void testShouldDispatchIsFalseByDefault();

    CPPUNIT_TEST_SUITE(CommandPaletteControllerTest);
    CPPUNIT_TEST(testFreshControllerHasEmptyCorpus);
    CPPUNIT_TEST(testSetCorpusStoresEntries);
    CPPUNIT_TEST(testSetCorpusReplacesPreviousCorpus);
    CPPUNIT_TEST(testEmptyQueryReturnsEmpty);
    CPPUNIT_TEST(testQueryRoutesThroughFuzzyMatcher);
    CPPUNIT_TEST(testQueryRespectsTopNDefault);
    CPPUNIT_TEST(testShouldDispatchIsFalseByDefault);
    CPPUNIT_TEST_SUITE_END();

private:
    static std::vector<CommandEntry> sampleCorpus()
    {
        return {
            { u".uno:Bold"_ustr,           u"Bold"_ustr,
              u"粗体"_ustr,                 u"ct"_ustr,
              u"cuti"_ustr,                 0 },
            { u".uno:InsertGraphic"_ustr,  u"Insert Image"_ustr,
              u"插入图片"_ustr,             u"crtp"_ustr,
              u"charutupian"_ustr,          0 },
            { u".uno:ExportToPDF"_ustr,    u"Export as PDF"_ustr,
              u"导出为 PDF"_ustr,           u"dcwp"_ustr,
              u"daochuweipdf"_ustr,         0 },
        };
    }
};

void CommandPaletteControllerTest::testFreshControllerHasEmptyCorpus()
{
    CommandPaletteController c;
    CPPUNIT_ASSERT(c.corpus().empty());
    // Querying an empty controller returns nothing — invariant for the
    // popover's "no results yet" first-paint state.
    auto hits = c.queryToResults(u"Bold"_ustr);
    CPPUNIT_ASSERT(hits.empty());
}

void CommandPaletteControllerTest::testSetCorpusStoresEntries()
{
    CommandPaletteController c;
    c.setCorpus(sampleCorpus());
    CPPUNIT_ASSERT_EQUAL(static_cast<std::size_t>(3), c.corpus().size());
    CPPUNIT_ASSERT_EQUAL(u".uno:Bold"_ustr, c.corpus()[0].unoCommand);
}

void CommandPaletteControllerTest::testSetCorpusReplacesPreviousCorpus()
{
    CommandPaletteController c;
    c.setCorpus(sampleCorpus());
    // Replacing the corpus must drop the old entries, not append. This
    // matches the W2 Day-1 contract where the CommandIndex re-emits a
    // fresh corpus on .xcu reload rather than diff-patching.
    std::vector<CommandEntry> smaller = {
        { u".uno:Italic"_ustr, u"Italic"_ustr,
          u"斜体"_ustr,         u"xt"_ustr,
          u"xieti"_ustr,        0 },
    };
    c.setCorpus(std::move(smaller));
    CPPUNIT_ASSERT_EQUAL(static_cast<std::size_t>(1), c.corpus().size());
    CPPUNIT_ASSERT_EQUAL(u".uno:Italic"_ustr, c.corpus()[0].unoCommand);

    // The matcher must also see the replaced corpus, not the original.
    auto hits = c.queryToResults(u"Bold"_ustr);
    CPPUNIT_ASSERT(hits.empty());
}

void CommandPaletteControllerTest::testEmptyQueryReturnsEmpty()
{
    CommandPaletteController c;
    c.setCorpus(sampleCorpus());
    auto hits = c.queryToResults(u""_ustr);
    CPPUNIT_ASSERT(hits.empty());
}

void CommandPaletteControllerTest::testQueryRoutesThroughFuzzyMatcher()
{
    CommandPaletteController c;
    c.setCorpus(sampleCorpus());
    // labelZh exact + substring → high score; should beat unrelated entries.
    auto hits = c.queryToResults(u"粗体"_ustr);
    CPPUNIT_ASSERT(!hits.empty());
    CPPUNIT_ASSERT_EQUAL(u".uno:Bold"_ustr, hits.front().entry->unoCommand);

    // pinyin-first prefix routing — same path FuzzyMatcher tests cover,
    // re-asserted here through the controller surface so the routing
    // cannot silently regress (e.g. if queryToResults ever pre-filters).
    auto hits2 = c.queryToResults(u"crtp"_ustr);
    CPPUNIT_ASSERT(!hits2.empty());
    CPPUNIT_ASSERT_EQUAL(u".uno:InsertGraphic"_ustr,
                         hits2.front().entry->unoCommand);
}

void CommandPaletteControllerTest::testQueryRespectsTopNDefault()
{
    CommandPaletteController c;
    // 12 entries that all match "dz" via pinyin-first → matcher must
    // cap the controller's result list at the FuzzyMatcher::match
    // default topN of 8.
    std::vector<CommandEntry> big;
    for (int i = 0; i < 12; ++i)
    {
        OUString idx = OUString::number(i);
        big.push_back({ u".uno:X"_ustr + idx,
                        u"Action"_ustr + idx,
                        u"动作"_ustr + idx,
                        u"dz"_ustr,
                        u"dongzuo"_ustr,
                        i });
    }
    c.setCorpus(std::move(big));
    auto hits = c.queryToResults(u"dz"_ustr);
    CPPUNIT_ASSERT(hits.size() <= static_cast<std::size_t>(8));
    CPPUNIT_ASSERT(!hits.empty());
}

void CommandPaletteControllerTest::testShouldDispatchIsFalseByDefault()
{
    // W2 Day-0 invariant: shouldDispatch is gated until the popover
    // observes Enter. Day-1b will flip this; until then it must remain
    // false for every result index, including out-of-range probes
    // (the popover must never dispatch a row that does not exist).
    CommandPaletteController c;
    c.setCorpus(sampleCorpus());
    CPPUNIT_ASSERT(!c.shouldDispatch(0));
    CPPUNIT_ASSERT(!c.shouldDispatch(1));
    CPPUNIT_ASSERT(!c.shouldDispatch(99));
}

CPPUNIT_TEST_SUITE_REGISTRATION(CommandPaletteControllerTest);
} // namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
