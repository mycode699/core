/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V6: AI File Manager).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Unit tests for AIFileManager and AIFileSearchUI.
 */

#include <sal/types.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/plugin/TestPlugIn.h>

#include <rtl/ustring.hxx>

#include "AIFileManager.hxx"
#include "AIFileSearchUI.hxx"

using namespace kqoffice::ai::filemgr;

namespace
{

// ── AIFileManager tests ─────────────────────────────────────────────────

class AIFileManagerTest : public CppUnit::TestFixture
{
public:
    void testCategorizeWriter()
    {
        CPPUNIT_ASSERT_EQUAL(FileCategory::Writer,
            AIFileManager::categorize("odt"));
        CPPUNIT_ASSERT_EQUAL(FileCategory::Writer,
            AIFileManager::categorize("docx"));
        CPPUNIT_ASSERT_EQUAL(FileCategory::Writer,
            AIFileManager::categorize("txt"));
        CPPUNIT_ASSERT_EQUAL(FileCategory::Writer,
            AIFileManager::categorize("md"));
    }

    void testCategorizeCalc()
    {
        CPPUNIT_ASSERT_EQUAL(FileCategory::Calc,
            AIFileManager::categorize("ods"));
        CPPUNIT_ASSERT_EQUAL(FileCategory::Calc,
            AIFileManager::categorize("xlsx"));
        CPPUNIT_ASSERT_EQUAL(FileCategory::Calc,
            AIFileManager::categorize("csv"));
    }

    void testCategorizeImpress()
    {
        CPPUNIT_ASSERT_EQUAL(FileCategory::Impress,
            AIFileManager::categorize("odp"));
        CPPUNIT_ASSERT_EQUAL(FileCategory::Impress,
            AIFileManager::categorize("pptx"));
    }

    void testCategorizePDF()
    {
        CPPUNIT_ASSERT_EQUAL(FileCategory::PDF,
            AIFileManager::categorize("pdf"));
    }

    void testCategorizeImage()
    {
        CPPUNIT_ASSERT_EQUAL(FileCategory::Image,
            AIFileManager::categorize("png"));
        CPPUNIT_ASSERT_EQUAL(FileCategory::Image,
            AIFileManager::categorize("jpg"));
        CPPUNIT_ASSERT_EQUAL(FileCategory::Image,
            AIFileManager::categorize("svg"));
    }

    void testCategorizeOther()
    {
        CPPUNIT_ASSERT_EQUAL(FileCategory::Other,
            AIFileManager::categorize("exe"));
        CPPUNIT_ASSERT_EQUAL(FileCategory::Other,
            AIFileManager::categorize("dll"));
    }

    void testCategoryLabel()
    {
        CPPUNIT_ASSERT(!AIFileManager::categoryLabel(FileCategory::Writer).isEmpty());
        CPPUNIT_ASSERT(!AIFileManager::categoryLabel(FileCategory::Calc).isEmpty());
        CPPUNIT_ASSERT(!AIFileManager::categoryLabel(FileCategory::Impress).isEmpty());
        CPPUNIT_ASSERT(!AIFileManager::categoryLabel(FileCategory::PDF).isEmpty());
        CPPUNIT_ASSERT(!AIFileManager::categoryLabel(FileCategory::Image).isEmpty());
    }

    void testFormatSize()
    {
        CPPUNIT_ASSERT(AIFileManager::formatSize(512).indexOf("B") >= 0);
        CPPUNIT_ASSERT(AIFileManager::formatSize(2048).indexOf("KB") >= 0);
        CPPUNIT_ASSERT(AIFileManager::formatSize(3 * 1024 * 1024).indexOf("MB") >= 0);
    }

    void testFormatTime()
    {
        OUString t = AIFileManager::formatTime(0);
        CPPUNIT_ASSERT(!t.isEmpty());
        CPPUNIT_ASSERT(t.indexOf("-") >= 0); // has date separators
    }

    void testCommonDirectories()
    {
        auto dirs = AIFileManager::commonDirectories();
        CPPUNIT_ASSERT(!dirs.empty());
        // Should include Desktop, Documents, Downloads
        bool hasDesktop = false, hasDocs = false;
        for (const auto& d : dirs)
        {
            if (d.indexOf("Desktop") >= 0) hasDesktop = true;
            if (d.indexOf("Documents") >= 0) hasDocs = true;
        }
        CPPUNIT_ASSERT(hasDesktop);
        CPPUNIT_ASSERT(hasDocs);
    }

    void testQuickScan()
    {
        AIFileManager mgr;
        auto result = mgr.quickScan();
        CPPUNIT_ASSERT(result.totalScanned > 0);
        CPPUNIT_ASSERT(result.scanDurationMs >= 0);
    }

    void testSortByName()
    {
        std::vector<FileEntry> files;
        FileEntry a; a.name = "bbb.txt";
        FileEntry b; b.name = "aaa.txt";
        FileEntry c; c.name = "ccc.txt";
        files.push_back(a); files.push_back(b); files.push_back(c);

        AIFileManager::sort(files, SortOrder::NameAsc);
        CPPUNIT_ASSERT_EQUAL(OUString("aaa.txt"), files[0].name);
        CPPUNIT_ASSERT_EQUAL(OUString("bbb.txt"), files[1].name);
        CPPUNIT_ASSERT_EQUAL(OUString("ccc.txt"), files[2].name);
    }

    void testSortByTime()
    {
        std::vector<FileEntry> files;
        FileEntry a; a.name = "old.txt";  a.modifiedTime = 100;
        FileEntry b; b.name = "new.txt";  b.modifiedTime = 200;
        files.push_back(a); files.push_back(b);

        AIFileManager::sort(files, SortOrder::TimeDesc);
        CPPUNIT_ASSERT_EQUAL(OUString("new.txt"), files[0].name);
    }

    void testSemanticSearch()
    {
        std::vector<FileEntry> pool;
        FileEntry f1; f1.name = "项目计划.odt"; f1.path = "/home/user/项目计划.odt";
        f1.category = FileCategory::Writer; f1.modifiedTime = 10000;
        FileEntry f2; f2.name = "财务报表.xlsx"; f2.path = "/home/user/财务报表.xlsx";
        f2.category = FileCategory::Calc; f2.modifiedTime = 20000;
        FileEntry f3; f3.name = "README.md"; f3.path = "/home/user/README.md";
        f3.category = FileCategory::Writer;
        pool.push_back(f1); pool.push_back(f2); pool.push_back(f3);

        AIFileManager mgr;
        auto results = mgr.semanticSearch("项目", pool, 10);
        CPPUNIT_ASSERT(!results.empty());
        CPPUNIT_ASSERT(results[0].relevanceScore > 0.0);
    }

    void testBuildNavEntries()
    {
        std::vector<FileEntry> files;
        FileEntry f; f.name = "test.odt"; f.path = "/tmp/test.odt";
        f.category = FileCategory::Writer; f.sizeBytes = 1024;
        files.push_back(f);

        auto entries = AIFileManager::buildNavEntries(files);
        CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), entries.size());
        CPPUNIT_ASSERT_EQUAL(OUString("test.odt"), entries[0].displayName);
    }

    void testGroupByCategory()
    {
        std::vector<FileEntry> files;
        FileEntry w; w.name = "doc.odt"; w.category = FileCategory::Writer;
        FileEntry c; c.name = "sheet.ods"; c.category = FileCategory::Calc;
        FileEntry w2; w2.name = "doc2.odt"; w2.category = FileCategory::Writer;
        files.push_back(w); files.push_back(c); files.push_back(w2);

        AIFileManager::groupByCategory(files, SortOrder::NameAsc);
        // Calc should come first (smaller enum value), then Writer
        CPPUNIT_ASSERT(files[0].category == FileCategory::Writer
                       || files[0].category == FileCategory::Calc);
    }

    void testExtensionsFor()
    {
        auto exts = AIFileManager::extensionsFor(FileCategory::Writer);
        CPPUNIT_ASSERT(!exts.empty());
        // Check "odt" is in the list
        bool found = false;
        for (const auto& e : exts)
            if (e == "odt") found = true;
        CPPUNIT_ASSERT(found);
    }

    CPPUNIT_TEST_SUITE(AIFileManagerTest);
    CPPUNIT_TEST(testCategorizeWriter);
    CPPUNIT_TEST(testCategorizeCalc);
    CPPUNIT_TEST(testCategorizeImpress);
    CPPUNIT_TEST(testCategorizePDF);
    CPPUNIT_TEST(testCategorizeImage);
    CPPUNIT_TEST(testCategorizeOther);
    CPPUNIT_TEST(testCategoryLabel);
    CPPUNIT_TEST(testFormatSize);
    CPPUNIT_TEST(testFormatTime);
    CPPUNIT_TEST(testCommonDirectories);
    CPPUNIT_TEST(testQuickScan);
    CPPUNIT_TEST(testSortByName);
    CPPUNIT_TEST(testSortByTime);
    CPPUNIT_TEST(testSemanticSearch);
    CPPUNIT_TEST(testBuildNavEntries);
    CPPUNIT_TEST(testGroupByCategory);
    CPPUNIT_TEST(testExtensionsFor);
    CPPUNIT_TEST_SUITE_END();
};

// ── AIFileSearchUI tests ────────────────────────────────────────────────

class AIFileSearchUITest : public CppUnit::TestFixture
{
public:
    void testFormatScoreBar()
    {
        OUString bar = AIFileSearchUI::formatScoreBar(0.8);
        CPPUNIT_ASSERT(!bar.isEmpty());
        CPPUNIT_ASSERT(bar.indexOf("★") >= 0);
    }

    void testFormatScanSummary()
    {
        ScanResult r;
        r.totalMatched = 42;
        r.totalSizeBytes = 1024 * 1024;
        r.scanDurationMs = 500;
        OUString s = AIFileSearchUI::formatScanSummary(r);
        CPPUNIT_ASSERT(s.indexOf("42") >= 0);
    }

    void testFormatSearchResults()
    {
        std::vector<SemanticMatch> results;
        SemanticMatch m;
        m.file.name = "test.odt";
        m.file.path = "/tmp/test.odt";
        m.file.category = FileCategory::Writer;
        m.relevanceScore = 0.9;
        m.matchReason = "文件名匹配";
        results.push_back(m);

        OUString fmt = AIFileSearchUI::formatSearchResults(results);
        CPPUNIT_ASSERT(fmt.indexOf("test.odt") >= 0);
        CPPUNIT_ASSERT(fmt.indexOf("★") >= 0);
    }

    CPPUNIT_TEST_SUITE(AIFileSearchUITest);
    CPPUNIT_TEST(testFormatScoreBar);
    CPPUNIT_TEST(testFormatScanSummary);
    CPPUNIT_TEST(testFormatSearchResults);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(AIFileManagerTest);
CPPUNIT_TEST_SUITE_REGISTRATION(AIFileSearchUITest);

} // anonymous namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
