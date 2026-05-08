/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W2: Cmd+K Command Palette).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Day-0 unit tests for the pure-logic FuzzyMatcher. No VCL bring-up,
 * no UNO bootstrap — fast (< 100ms target).
 *
 * Cases mirror the worked examples in the W2 spec table:
 *   - "ct"      → 粗体 (pinyinFirst)
 *   - "插入图片" → InsertGraphic (labelZh exact)
 *   - "pdf"     → ExportToPDF (labelEn substring)
 *   - "加粗"    → Bold (labelZh substring)
 */

#include <sal/types.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/plugin/TestPlugIn.h>

#include <commandpalette/FuzzyMatcher.hxx>

using cui::commandpalette::CommandEntry;
using cui::commandpalette::FuzzyMatcher;
using cui::commandpalette::ScoredEntry;

namespace
{
// Day-0 logic is pure C++ with no UNO dependency — avoid BootstrapFixture
// so the test binary does not need a working services.rdb, keeping the
// fast path fast and isolating unit tests from future UNO bootstrap churn.
class FuzzyMatcherTest : public CppUnit::TestFixture
{
public:
    void testEmptyQueryReturnsEmpty();
    void testExactZhMatchHighest();
    void testExactEnMatchHighest();
    void testPinyinFirstPrefix();
    void testLabelZhSubstring();
    void testLabelEnSubstringCaseInsensitive();
    void testFrequencyTiebreak();
    void testTopNCap();

    CPPUNIT_TEST_SUITE(FuzzyMatcherTest);
    CPPUNIT_TEST(testEmptyQueryReturnsEmpty);
    CPPUNIT_TEST(testExactZhMatchHighest);
    CPPUNIT_TEST(testExactEnMatchHighest);
    CPPUNIT_TEST(testPinyinFirstPrefix);
    CPPUNIT_TEST(testLabelZhSubstring);
    CPPUNIT_TEST(testLabelEnSubstringCaseInsensitive);
    CPPUNIT_TEST(testFrequencyTiebreak);
    CPPUNIT_TEST(testTopNCap);
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
            { u".uno:Italic"_ustr,         u"Italic"_ustr,
              u"斜体"_ustr,                 u"xt"_ustr,
              u"xieti"_ustr,                0 },
        };
    }
};

void FuzzyMatcherTest::testEmptyQueryReturnsEmpty()
{
    auto corpus = sampleCorpus();
    auto hits = FuzzyMatcher::match(u""_ustr, corpus);
    CPPUNIT_ASSERT(hits.empty());
}

void FuzzyMatcherTest::testExactZhMatchHighest()
{
    auto corpus = sampleCorpus();
    auto hits = FuzzyMatcher::match(u"粗体"_ustr, corpus);
    CPPUNIT_ASSERT(!hits.empty());
    CPPUNIT_ASSERT_EQUAL(u".uno:Bold"_ustr, hits.front().entry->unoCommand);
    // Exact + substring → 100 + 60 = 160
    CPPUNIT_ASSERT(hits.front().score >= 100);
}

void FuzzyMatcherTest::testExactEnMatchHighest()
{
    auto corpus = sampleCorpus();
    auto hits = FuzzyMatcher::match(u"Bold"_ustr, corpus);
    CPPUNIT_ASSERT(!hits.empty());
    CPPUNIT_ASSERT_EQUAL(u".uno:Bold"_ustr, hits.front().entry->unoCommand);
    CPPUNIT_ASSERT(hits.front().score >= 100);
}

void FuzzyMatcherTest::testPinyinFirstPrefix()
{
    auto corpus = sampleCorpus();
    auto hits = FuzzyMatcher::match(u"ct"_ustr, corpus);
    CPPUNIT_ASSERT(!hits.empty());
    CPPUNIT_ASSERT_EQUAL(u".uno:Bold"_ustr, hits.front().entry->unoCommand);
}

void FuzzyMatcherTest::testLabelZhSubstring()
{
    auto corpus = sampleCorpus();
    auto hits = FuzzyMatcher::match(u"图片"_ustr, corpus);
    CPPUNIT_ASSERT(!hits.empty());
    CPPUNIT_ASSERT_EQUAL(u".uno:InsertGraphic"_ustr,
                         hits.front().entry->unoCommand);
}

void FuzzyMatcherTest::testLabelEnSubstringCaseInsensitive()
{
    auto corpus = sampleCorpus();
    auto hits = FuzzyMatcher::match(u"PDF"_ustr, corpus);
    CPPUNIT_ASSERT(!hits.empty());
    CPPUNIT_ASSERT_EQUAL(u".uno:ExportToPDF"_ustr,
                         hits.front().entry->unoCommand);
    auto hits2 = FuzzyMatcher::match(u"pdf"_ustr, corpus);
    CPPUNIT_ASSERT(!hits2.empty());
    CPPUNIT_ASSERT_EQUAL(u".uno:ExportToPDF"_ustr,
                         hits2.front().entry->unoCommand);
}

void FuzzyMatcherTest::testFrequencyTiebreak()
{
    auto corpus = sampleCorpus();
    // give Italic a high frequency so its substring "xt" pinyin-first hit
    // beats Bold's "ct" tie under same query "x"
    for (auto& e : corpus)
        if (e.unoCommand == u".uno:Italic"_ustr)
            e.frequency = 200; // +20 boost
    auto hits = FuzzyMatcher::match(u"xt"_ustr, corpus);
    CPPUNIT_ASSERT(!hits.empty());
    CPPUNIT_ASSERT_EQUAL(u".uno:Italic"_ustr,
                         hits.front().entry->unoCommand);
}

void FuzzyMatcherTest::testTopNCap()
{
    std::vector<CommandEntry> big;
    for (int i = 0; i < 50; ++i)
    {
        OUString idx = OUString::number(i);
        big.push_back({ u".uno:X"_ustr + idx,
                        u"Action"_ustr + idx,
                        u"动作"_ustr + idx,
                        u"dz"_ustr,
                        u"dongzuo"_ustr,
                        i });
    }
    auto hits = FuzzyMatcher::match(u"dz"_ustr, big, /*topN=*/5);
    CPPUNIT_ASSERT_EQUAL(static_cast<std::size_t>(5), hits.size());
    // Highest frequency wins after pinyin-first ties.
    CPPUNIT_ASSERT_EQUAL(u".uno:X49"_ustr, hits.front().entry->unoCommand);
}

CPPUNIT_TEST_SUITE_REGISTRATION(FuzzyMatcherTest);
} // namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
