/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W2: Cmd+K Command Palette).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Day-1a unit tests for RecentStore JSON round-trip + bump merge +
 * applyFrequencies → FuzzyMatcher ranking integration. Pure-logic, no
 * disk, no UNO.
 */

#include <sal/types.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/plugin/TestPlugIn.h>

#include <commandpalette/FuzzyMatcher.hxx>
#include <commandpalette/RecentStore.hxx>

using cui::commandpalette::CommandEntry;
using cui::commandpalette::FuzzyMatcher;
using cui::commandpalette::RecentEntry;
using cui::commandpalette::RecentStore;

namespace
{
class RecentStoreTest : public CppUnit::TestFixture
{
public:
    void testParseEmptyReturnsEmpty();
    void testParseRejectsWrongVersion();
    void testRoundTripPreservesEntries();
    void testParseHandlesEmptyEntriesArray();
    void testBumpCreatesNewEntry();
    void testBumpIncrementsExistingEntry();
    void testBumpSortsByUseCount();
    void testBumpCapsAtMaxEntries();
    void testApplyFrequenciesUpdatesCorpus();
    void testRecencyBoostAffectsFuzzyRanking();

    CPPUNIT_TEST_SUITE(RecentStoreTest);
    CPPUNIT_TEST(testParseEmptyReturnsEmpty);
    CPPUNIT_TEST(testParseRejectsWrongVersion);
    CPPUNIT_TEST(testRoundTripPreservesEntries);
    CPPUNIT_TEST(testParseHandlesEmptyEntriesArray);
    CPPUNIT_TEST(testBumpCreatesNewEntry);
    CPPUNIT_TEST(testBumpIncrementsExistingEntry);
    CPPUNIT_TEST(testBumpSortsByUseCount);
    CPPUNIT_TEST(testBumpCapsAtMaxEntries);
    CPPUNIT_TEST(testApplyFrequenciesUpdatesCorpus);
    CPPUNIT_TEST(testRecencyBoostAffectsFuzzyRanking);
    CPPUNIT_TEST_SUITE_END();
};

void RecentStoreTest::testParseEmptyReturnsEmpty()
{
    CPPUNIT_ASSERT(RecentStore::parseRecentJson(OString()).empty());
    CPPUNIT_ASSERT(RecentStore::parseRecentJson(OString("{}")).empty());
}

void RecentStoreTest::testParseRejectsWrongVersion()
{
    // Schema bump in the future would change `version` — older readers
    // must opt out cleanly rather than misinterpret newer payloads.
    OString body(
        "{\"version\": 2,\"entries\":["
        "{\"unoCommand\":\".uno:Bold\",\"lastUsed\":\"x\",\"useCount\":1}"
        "]}");
    CPPUNIT_ASSERT(RecentStore::parseRecentJson(body).empty());
}

void RecentStoreTest::testRoundTripPreservesEntries()
{
    std::vector<RecentEntry> in;
    in.push_back({u".uno:Bold"_ustr, u"2026-05-08T13:00:00"_ustr, 42});
    in.push_back({u".uno:InsertGraphic"_ustr,
                  u"2026-05-08T12:30:00"_ustr, 7});

    OString json = RecentStore::serializeRecentJson(in);
    auto out = RecentStore::parseRecentJson(json);
    CPPUNIT_ASSERT_EQUAL(in.size(), out.size());
    CPPUNIT_ASSERT_EQUAL(in[0].unoCommand, out[0].unoCommand);
    CPPUNIT_ASSERT_EQUAL(in[0].lastUsed, out[0].lastUsed);
    CPPUNIT_ASSERT_EQUAL(in[0].useCount, out[0].useCount);
    CPPUNIT_ASSERT_EQUAL(in[1].unoCommand, out[1].unoCommand);
    CPPUNIT_ASSERT_EQUAL(in[1].useCount, out[1].useCount);
}

void RecentStoreTest::testParseHandlesEmptyEntriesArray()
{
    OString body("{\"version\": 1, \"entries\": []}");
    CPPUNIT_ASSERT(RecentStore::parseRecentJson(body).empty());
}

void RecentStoreTest::testBumpCreatesNewEntry()
{
    auto entries = RecentStore::bump({}, u".uno:Bold"_ustr,
                                     u"2026-05-08T13:00:00"_ustr);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), entries.size());
    CPPUNIT_ASSERT_EQUAL(u".uno:Bold"_ustr, entries[0].unoCommand);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(1), entries[0].useCount);
}

void RecentStoreTest::testBumpIncrementsExistingEntry()
{
    std::vector<RecentEntry> seed;
    seed.push_back({u".uno:Bold"_ustr, u"2026-05-08T12:00:00"_ustr, 5});
    auto bumped = RecentStore::bump(seed, u".uno:Bold"_ustr,
                                    u"2026-05-08T13:00:00"_ustr);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), bumped.size());
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(6), bumped[0].useCount);
    CPPUNIT_ASSERT_EQUAL(u"2026-05-08T13:00:00"_ustr, bumped[0].lastUsed);
}

void RecentStoreTest::testBumpSortsByUseCount()
{
    std::vector<RecentEntry> seed;
    seed.push_back({u".uno:Italic"_ustr, u"2026-05-08T11:00:00"_ustr, 1});
    seed.push_back({u".uno:Bold"_ustr,   u"2026-05-08T12:00:00"_ustr, 5});
    // bump Italic 4 times → still 5, but lastUsed newer than Bold
    auto bumped = RecentStore::bump(std::move(seed), u".uno:Italic"_ustr,
                                    u"2026-05-08T13:00:00"_ustr);
    bumped = RecentStore::bump(std::move(bumped), u".uno:Italic"_ustr,
                               u"2026-05-08T13:01:00"_ustr);
    bumped = RecentStore::bump(std::move(bumped), u".uno:Italic"_ustr,
                               u"2026-05-08T13:02:00"_ustr);
    bumped = RecentStore::bump(std::move(bumped), u".uno:Italic"_ustr,
                               u"2026-05-08T13:03:00"_ustr);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(2), bumped.size());
    // Tie on useCount=5 → newer lastUsed (Italic) wins.
    CPPUNIT_ASSERT_EQUAL(u".uno:Italic"_ustr, bumped[0].unoCommand);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(5), bumped[0].useCount);
}

void RecentStoreTest::testBumpCapsAtMaxEntries()
{
    std::vector<RecentEntry> seed;
    for (int i = 0; i < 30; ++i)
    {
        OUString cmd = u".uno:X"_ustr + OUString::number(i);
        // Lower-index commands have higher useCount so they stay top-3.
        seed.push_back({cmd, u"2026-05-08T13:00:00"_ustr,
                        static_cast<sal_Int32>(30 - i)});
    }
    auto out = RecentStore::bump(std::move(seed), u".uno:NewCommand"_ustr,
                                 u"2026-05-08T13:01:00"_ustr,
                                 /*maxEntries=*/3);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(3), out.size());
    CPPUNIT_ASSERT_EQUAL(u".uno:X0"_ustr, out[0].unoCommand);
}

void RecentStoreTest::testApplyFrequenciesUpdatesCorpus()
{
    std::vector<CommandEntry> corpus = {
        {u".uno:Bold"_ustr, u"Bold"_ustr, u"粗体"_ustr,
         u"ct"_ustr, u"cuti"_ustr, 0},
        {u".uno:Italic"_ustr, u"Italic"_ustr, u"斜体"_ustr,
         u"xt"_ustr, u"xieti"_ustr, 0},
    };
    std::vector<RecentEntry> recents = {
        {u".uno:Bold"_ustr, u"2026-05-08T13:00:00"_ustr, 7},
    };
    RecentStore::applyFrequencies(corpus, recents);
    CPPUNIT_ASSERT_EQUAL(static_cast<int>(70), corpus[0].frequency);
    CPPUNIT_ASSERT_EQUAL(static_cast<int>(0), corpus[1].frequency);
}

void RecentStoreTest::testRecencyBoostAffectsFuzzyRanking()
{
    // Two entries that tie purely on pinyin-prefix score (+80). The
    // recent boost (frequency / 10) must be enough to push the
    // recently-used one to the top.
    std::vector<CommandEntry> corpus = {
        {u".uno:Bold"_ustr, u"Bold"_ustr, u"粗体"_ustr,
         u"ct"_ustr, u"cuti"_ustr, 0},
        {u".uno:CutText"_ustr, u"Cut"_ustr, u"剪切"_ustr,
         u"ct"_ustr, u"cutext"_ustr, 0},
    };
    // Cut has been used 12 times → frequency=120 → +12 boost.
    std::vector<RecentEntry> recents = {
        {u".uno:CutText"_ustr, u"2026-05-08T13:00:00"_ustr, 12},
    };
    RecentStore::applyFrequencies(corpus, recents);

    auto hits = FuzzyMatcher::match(u"ct"_ustr, corpus);
    CPPUNIT_ASSERT(!hits.empty());
    CPPUNIT_ASSERT_EQUAL(u".uno:CutText"_ustr,
                         hits.front().entry->unoCommand);
}

CPPUNIT_TEST_SUITE_REGISTRATION(RecentStoreTest);
} // namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
