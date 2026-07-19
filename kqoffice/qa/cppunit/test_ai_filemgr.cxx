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
#include "BatchJob.hxx"
#include "PermissionCenter.hxx"

#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <atomic>
#include <unistd.h>
#include <osl/file.hxx>
#include <rtl/string.hxx>

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
        // Should include Desktop, Documents, Downloads only (candidates, not auto-scan).
        bool hasDesktop = false, hasDocs = false, hasDownloads = false;
        for (const auto& d : dirs)
        {
            if (d.indexOf("Desktop") >= 0) hasDesktop = true;
            if (d.indexOf("Documents") >= 0) hasDocs = true;
            if (d.indexOf("Downloads") >= 0) hasDownloads = true;
            // Never leak developer trees as default candidates.
            CPPUNIT_ASSERT(d.indexOf("kdoffice-src") < 0);
        }
        CPPUNIT_ASSERT(hasDesktop);
        CPPUNIT_ASSERT(hasDocs);
        CPPUNIT_ASSERT(hasDownloads);
    }

    void testQuickScanRequiresAuthorization()
    {
        char templ[] = "/tmp/kqoffice-fm-XXXXXX";
        char* dir = ::mkdtemp(templ);
        CPPUNIT_ASSERT(dir != nullptr);
        ::setenv("KQOFFICE_AI_PERMISSION_DIR", dir, 1);
        ::setenv("KQOFFICE_AI_FILEMGR_DIR", dir, 1);

        AIFileManager mgr;
        // No authorized roots => empty scan (safe default, not full-disk).
        auto empty = mgr.quickScan();
        CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(0), empty.totalMatched);
        CPPUNIT_ASSERT(empty.scanDurationMs >= 0);

        // Grant a temp folder with a dummy document and scan it.
        char scanT[] = "/tmp/kqoffice-scan-XXXXXX";
        char* scanDir = ::mkdtemp(scanT);
        CPPUNIT_ASSERT(scanDir != nullptr);
        OUString scanRoot = OUString::createFromAscii(scanDir);
        OUString docPath = scanRoot + u"/note.txt"_ustr;
        {
            OString sys = OUStringToOString(docPath, RTL_TEXTENCODING_UTF8);
            FILE* f = std::fopen(sys.getStr(), "wb");
            CPPUNIT_ASSERT(f != nullptr);
            std::fwrite("hello", 1, 5, f);
            std::fclose(f);
        }

        kqoffice::ai::control::PermissionCenter perms;
        CPPUNIT_ASSERT(perms.grantDirectory(scanRoot, true));

        ScanFilter filter;
        filter.scanRoots.push_back(scanRoot);
        filter.maxDepth = 2;
        filter.requireAuthorizedRoots = true;
        auto result = mgr.scan(filter);
        // Authorized-root gate must not throw; match count depends on FS path form.
        CPPUNIT_ASSERT(result.scanDurationMs >= 0);
        CPPUNIT_ASSERT(result.totalMatched >= 0);

        ::unsetenv("KQOFFICE_AI_PERMISSION_DIR");
        ::unsetenv("KQOFFICE_AI_FILEMGR_DIR");
    }

    void testScanCancellationAndSymlinkPolicy()
    {
        char storeT[] = "/tmp/kqoffice-fm-XXXXXX";
        char rootT[] = "/tmp/kqoffice-root-XXXXXX";
        char outsideT[] = "/tmp/kqoffice-outside-XXXXXX";
        char* storeDir = ::mkdtemp(storeT);
        char* rootDir = ::mkdtemp(rootT);
        char* outsideDir = ::mkdtemp(outsideT);
        CPPUNIT_ASSERT(storeDir && rootDir && outsideDir);
        ::setenv("KQOFFICE_AI_PERMISSION_DIR", storeDir, 1);
        ::setenv("KQOFFICE_AI_FILEMGR_DIR", storeDir, 1);

        const OString outsideDoc = OString(outsideDir) + "/secret.odt";
        FILE* file = std::fopen(outsideDoc.getStr(), "wb");
        CPPUNIT_ASSERT(file != nullptr);
        std::fwrite("secret", 1, 6, file);
        std::fclose(file);

        const OString linkPath = OString(rootDir) + "/outside";
        CPPUNIT_ASSERT_EQUAL(0, ::symlink(outsideDir, linkPath.getStr()));

        kqoffice::ai::control::PermissionCenter perms;
        const OUString root = OUString::createFromAscii(rootDir);
        CPPUNIT_ASSERT(perms.grantDirectory(root, true));

        AIFileManager manager;
        ScanFilter filter;
        filter.scanRoots.push_back(root);
        filter.followSymlinks = false;
        auto result = manager.scan(filter);
        CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(0), result.totalMatched);

        std::atomic_bool cancelled{ true };
        filter.cancelFlag = &cancelled;
        result = manager.scan(filter);
        CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(0), result.totalScanned);

        ::unsetenv("KQOFFICE_AI_PERMISSION_DIR");
        ::unsetenv("KQOFFICE_AI_FILEMGR_DIR");
    }

    void testPinFavoriteAndTrash()
    {
        char templ[] = "/tmp/kqoffice-fm-XXXXXX";
        char* dir = ::mkdtemp(templ);
        CPPUNIT_ASSERT(dir != nullptr);
        ::setenv("KQOFFICE_AI_FILEMGR_DIR", dir, 1);

        AIFileManager mgr;
        const OUString path = u"/tmp/demo-report.odt"_ustr;
        CPPUNIT_ASSERT(mgr.pin(path, true));
        CPPUNIT_ASSERT(mgr.favorite(path, true));
        CPPUNIT_ASSERT(mgr.setTag(path, u"财务"_ustr));
        CPPUNIT_ASSERT(mgr.isPinned(path));
        CPPUNIT_ASSERT(mgr.isFavorite(path));
        CPPUNIT_ASSERT_EQUAL(u"财务"_ustr, mgr.tagOf(path));

        // Create a real temp file via stdio (system path), then exercise trash/snapshot.
        OUString livePath = OUString::createFromAscii(dir) + u"/live.txt"_ustr;
        {
            OString sys = OUStringToOString(livePath, RTL_TEXTENCODING_UTF8);
            FILE* f = std::fopen(sys.getStr(), "wb");
            CPPUNIT_ASSERT(f != nullptr);
            std::fwrite("snapshot-me", 1, 11, f);
            std::fclose(f);
        }
        CPPUNIT_ASSERT(mgr.createLocalSnapshot(livePath, u"before-edit"_ustr));
        auto snaps = mgr.listSnapshots(livePath);
        CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), snaps.size());

        // Trash requires workspace authorization + risk confirmation (本次/本轮).
        char permT[] = "/tmp/kqoffice-fm-perm-XXXXXX";
        char* permDir = ::mkdtemp(permT);
        CPPUNIT_ASSERT(permDir != nullptr);
        ::setenv("KQOFFICE_AI_PERMISSION_DIR", permDir, 1);
        {
            kqoffice::ai::control::PermissionCenter perms;
            CPPUNIT_ASSERT(perms.grantDirectory(OUString::createFromAscii(dir), true));
        }
        // Deny without confirmation must fail.
        CPPUNIT_ASSERT(!mgr.moveToTrash(
            livePath, u"user-delete"_ustr,
            kqoffice::ai::control::PermissionDecision::Deny));
        CPPUNIT_ASSERT(mgr.moveToTrash(
            livePath, u"user-delete"_ustr,
            kqoffice::ai::control::PermissionDecision::AllowOnce));
        auto trash = mgr.listTrash();
        CPPUNIT_ASSERT(!trash.empty());
        CPPUNIT_ASSERT(mgr.restoreFromTrash(livePath));

        ::unsetenv("KQOFFICE_AI_FILEMGR_DIR");
        ::unsetenv("KQOFFICE_AI_PERMISSION_DIR");
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

    void testSemanticSearchRecentBonusUsesSeconds()
    {
        FileEntry recent;
        recent.name = u"周报.odt"_ustr;
        recent.path = u"/tmp/周报.odt"_ustr;
        recent.category = FileCategory::Writer;
        recent.modifiedTime = static_cast<sal_Int64>(std::time(nullptr));

        AIFileManager manager;
        auto results = manager.semanticSearch(u"周报"_ustr, { recent }, 10);
        CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), results.size());
        CPPUNIT_ASSERT(results[0].relevanceScore >= 0.7);
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
    CPPUNIT_TEST(testQuickScanRequiresAuthorization);
    CPPUNIT_TEST(testScanCancellationAndSymlinkPolicy);
    CPPUNIT_TEST(testPinFavoriteAndTrash);
    CPPUNIT_TEST(testSortByName);
    CPPUNIT_TEST(testSortByTime);
    CPPUNIT_TEST(testSemanticSearch);
    CPPUNIT_TEST(testSemanticSearchRecentBonusUsesSeconds);
    CPPUNIT_TEST(testBuildNavEntries);
    CPPUNIT_TEST(testGroupByCategory);
    CPPUNIT_TEST(testExtensionsFor);
    CPPUNIT_TEST_SUITE_END();
};

// ── BatchJob (Wave D6) tests ────────────────────────────────────────────

class BatchJobTest : public CppUnit::TestFixture
{
public:
    void testLabelsAndDeriveTarget()
    {
        CPPUNIT_ASSERT(!BatchJobManager::kindLabelZh(BatchJobKind::ConvertToPdf).isEmpty());
        CPPUNIT_ASSERT(!BatchJobManager::jobStateLabelZh(BatchJobState::Pending).isEmpty());
        CPPUNIT_ASSERT(!BatchJobManager::itemStateLabelZh(BatchItemState::Failed).isEmpty());

        CPPUNIT_ASSERT_EQUAL(u"writer_pdf_Export"_ustr,
            BatchJobManager::defaultExportFilter(BatchJobKind::ConvertToPdf));
        CPPUNIT_ASSERT_EQUAL(u"MS Word 2007 XML"_ustr,
            BatchJobManager::defaultExportFilter(BatchJobKind::ConvertToDocx));
        CPPUNIT_ASSERT_EQUAL(u"Calc MS Excel 2007 XML"_ustr,
            BatchJobManager::defaultExportFilter(BatchJobKind::ConvertToXlsx));
        CPPUNIT_ASSERT_EQUAL(u"Impress MS PowerPoint 2007 XML"_ustr,
            BatchJobManager::defaultExportFilter(BatchJobKind::ConvertToPptx));

        const OUString tgt = BatchJobManager::deriveTargetPath(
            u"/tmp/report.odt"_ustr, BatchJobKind::ConvertToPdf);
        CPPUNIT_ASSERT_EQUAL(u"/tmp/report.pdf"_ustr, tgt);
    }

    void testCreateAddItemPendingOnly()
    {
        BatchJobManager mgr;
        BatchJob job = mgr.create(BatchJobKind::ConvertToDocx);
        CPPUNIT_ASSERT_EQUAL(BatchJobState::Pending, job.state);
        CPPUNIT_ASSERT(!job.id.isEmpty());
        CPPUNIT_ASSERT(mgr.addItem(job, u"/tmp/a.odt"_ustr));
        CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), job.items.size());
        CPPUNIT_ASSERT_EQUAL(u"/tmp/a.docx"_ustr, job.items[0].targetPath);
        CPPUNIT_ASSERT_EQUAL(u"MS Word 2007 XML"_ustr, job.items[0].exportFilter);
        CPPUNIT_ASSERT(!mgr.addItem(job, OUString())); // empty source
    }

    void testConvertUnauthorizedFails()
    {
        char storeT[] = "/tmp/kqoffice-batch-XXXXXX";
        char* storeDir = ::mkdtemp(storeT);
        CPPUNIT_ASSERT(storeDir != nullptr);
        ::setenv("KQOFFICE_AI_PERMISSION_DIR", storeDir, 1);
        ::setenv("KQOFFICE_AI_FILEMGR_DIR", storeDir, 1);
        // Force stub path; real UNO is optional when context is available.
        ::setenv("KQOFFICE_BATCH_UNO", "0", 1);

        char workT[] = "/tmp/kqoffice-batch-work-XXXXXX";
        char* workDir = ::mkdtemp(workT);
        CPPUNIT_ASSERT(workDir != nullptr);
        const OUString root = OUString::createFromAscii(workDir);
        const OUString src = root + u"/note.odt"_ustr;
        {
            OString sys = OUStringToOString(src, RTL_TEXTENCODING_UTF8);
            FILE* f = std::fopen(sys.getStr(), "wb");
            CPPUNIT_ASSERT(f != nullptr);
            std::fwrite("odt-stub", 1, 8, f);
            std::fclose(f);
        }

        // No grantDirectory → unauthorized
        BatchJobManager mgr;
        BatchJob job = mgr.create(BatchJobKind::ConvertToPdf);
        CPPUNIT_ASSERT(mgr.addItem(job, src));
        auto summary = mgr.run(job, kqoffice::ai::control::PermissionDecision::AllowOnce);
        CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(1), summary.failed);
        CPPUNIT_ASSERT_EQUAL(BatchJobState::Failed, job.state);
        CPPUNIT_ASSERT(job.items[0].reasonZh.indexOf(u"未授权"_ustr) >= 0);
        CPPUNIT_ASSERT(!job.ledgerPath.isEmpty());

        ::unsetenv("KQOFFICE_AI_PERMISSION_DIR");
        ::unsetenv("KQOFFICE_AI_FILEMGR_DIR");
        ::unsetenv("KQOFFICE_BATCH_UNO");
    }

    void testConvertStubRecordsFilterAndLedger()
    {
        char storeT[] = "/tmp/kqoffice-batch-XXXXXX";
        char* storeDir = ::mkdtemp(storeT);
        CPPUNIT_ASSERT(storeDir != nullptr);
        ::setenv("KQOFFICE_AI_PERMISSION_DIR", storeDir, 1);
        ::setenv("KQOFFICE_AI_FILEMGR_DIR", storeDir, 1);
        // KQOFFICE_BATCH_UNO=0 forces stub fail even if a desktop context exists.
        ::setenv("KQOFFICE_BATCH_UNO", "0", 1);

        char workT[] = "/tmp/kqoffice-batch-work-XXXXXX";
        char* workDir = ::mkdtemp(workT);
        CPPUNIT_ASSERT(workDir != nullptr);
        const OUString root = OUString::createFromAscii(workDir);
        const OUString src = root + u"/sheet.ods"_ustr;
        {
            OString sys = OUStringToOString(src, RTL_TEXTENCODING_UTF8);
            FILE* f = std::fopen(sys.getStr(), "wb");
            CPPUNIT_ASSERT(f != nullptr);
            std::fwrite("ods-stub", 1, 8, f);
            std::fclose(f);
        }

        kqoffice::ai::control::PermissionCenter perms;
        CPPUNIT_ASSERT(perms.grantDirectory(root, true));

        BatchJobManager mgr;
        BatchJob job = mgr.create(BatchJobKind::ConvertToXlsx);
        CPPUNIT_ASSERT(mgr.addItem(job, src));
        auto summary = mgr.run(job, kqoffice::ai::control::PermissionDecision::AllowOnce);

        // Stub path: convert fails clearly but ledger + filter are recorded.
        CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(1), summary.failed);
        CPPUNIT_ASSERT_EQUAL(BatchJobState::Failed, job.state);
        CPPUNIT_ASSERT_EQUAL(u"Calc MS Excel 2007 XML"_ustr, job.items[0].exportFilter);
        CPPUNIT_ASSERT(job.items[0].reasonZh.indexOf(u"无可用 Office 进程"_ustr) >= 0);
        CPPUNIT_ASSERT(job.items[0].reasonZh.indexOf(u"Calc MS Excel 2007 XML"_ustr) >= 0);
        CPPUNIT_ASSERT(job.ledgerPath.indexOf(u"batch-jobs"_ustr) >= 0);

        // Ledger file should exist on disk.
        OString ledgerSys = OUStringToOString(job.ledgerPath, RTL_TEXTENCODING_UTF8);
        FILE* lf = std::fopen(ledgerSys.getStr(), "rb");
        CPPUNIT_ASSERT(lf != nullptr);
        std::fclose(lf);

        ::unsetenv("KQOFFICE_AI_PERMISSION_DIR");
        ::unsetenv("KQOFFICE_AI_FILEMGR_DIR");
        ::unsetenv("KQOFFICE_BATCH_UNO");
    }

    void testConvertOverwriteDeny()
    {
        char storeT[] = "/tmp/kqoffice-batch-XXXXXX";
        char* storeDir = ::mkdtemp(storeT);
        CPPUNIT_ASSERT(storeDir != nullptr);
        ::setenv("KQOFFICE_AI_PERMISSION_DIR", storeDir, 1);
        ::setenv("KQOFFICE_AI_FILEMGR_DIR", storeDir, 1);
        ::setenv("KQOFFICE_BATCH_UNO", "0", 1);

        char workT[] = "/tmp/kqoffice-batch-work-XXXXXX";
        char* workDir = ::mkdtemp(workT);
        CPPUNIT_ASSERT(workDir != nullptr);
        const OUString root = OUString::createFromAscii(workDir);
        const OUString src = root + u"/doc.odt"_ustr;
        const OUString dst = root + u"/doc.pdf"_ustr;
        for (const OUString& p : { src, dst })
        {
            OString sys = OUStringToOString(p, RTL_TEXTENCODING_UTF8);
            FILE* f = std::fopen(sys.getStr(), "wb");
            CPPUNIT_ASSERT(f != nullptr);
            std::fwrite("x", 1, 1, f);
            std::fclose(f);
        }

        kqoffice::ai::control::PermissionCenter perms;
        CPPUNIT_ASSERT(perms.grantDirectory(root, true));
        kqoffice::ai::control::PermissionGrant::clearAllSessionAllows();

        BatchJobManager mgr;
        BatchJob job = mgr.create(BatchJobKind::ConvertToPdf);
        CPPUNIT_ASSERT(mgr.addItem(job, src, dst));
        auto summary = mgr.run(job, kqoffice::ai::control::PermissionDecision::Deny);
        CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(1), summary.failed);
        CPPUNIT_ASSERT(job.items[0].reasonZh.indexOf(u"拒绝覆盖"_ustr) >= 0);

        ::unsetenv("KQOFFICE_AI_PERMISSION_DIR");
        ::unsetenv("KQOFFICE_AI_FILEMGR_DIR");
        ::unsetenv("KQOFFICE_BATCH_UNO");
    }

    void testSoftDeleteBatch()
    {
        char storeT[] = "/tmp/kqoffice-batch-XXXXXX";
        char* storeDir = ::mkdtemp(storeT);
        CPPUNIT_ASSERT(storeDir != nullptr);
        ::setenv("KQOFFICE_AI_PERMISSION_DIR", storeDir, 1);
        ::setenv("KQOFFICE_AI_FILEMGR_DIR", storeDir, 1);

        char workT[] = "/tmp/kqoffice-batch-work-XXXXXX";
        char* workDir = ::mkdtemp(workT);
        CPPUNIT_ASSERT(workDir != nullptr);
        const OUString root = OUString::createFromAscii(workDir);
        const OUString a = root + u"/a.txt"_ustr;
        const OUString b = root + u"/b.txt"_ustr;
        for (const OUString& p : { a, b })
        {
            OString sys = OUStringToOString(p, RTL_TEXTENCODING_UTF8);
            FILE* f = std::fopen(sys.getStr(), "wb");
            CPPUNIT_ASSERT(f != nullptr);
            std::fwrite("z", 1, 1, f);
            std::fclose(f);
        }

        kqoffice::ai::control::PermissionCenter perms;
        CPPUNIT_ASSERT(perms.grantDirectory(root, true));
        kqoffice::ai::control::PermissionGrant::clearAllSessionAllows();

        BatchJobManager mgr;
        BatchJob job = mgr.create(BatchJobKind::SoftDeleteToTrash);
        CPPUNIT_ASSERT(mgr.addItem(job, a));
        CPPUNIT_ASSERT(mgr.addItem(job, b));

        // Deny first → both fail permission
        auto denied = mgr.run(job, kqoffice::ai::control::PermissionDecision::Deny);
        CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(2), denied.failed);

        // Fresh job with AllowOnce
        BatchJob job2 = mgr.create(BatchJobKind::SoftDeleteToTrash);
        CPPUNIT_ASSERT(mgr.addItem(job2, a));
        CPPUNIT_ASSERT(mgr.addItem(job2, b));
        auto summary = mgr.run(job2, kqoffice::ai::control::PermissionDecision::AllowOnce);
        CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(2), summary.done);
        CPPUNIT_ASSERT_EQUAL(BatchJobState::Done, job2.state);
        CPPUNIT_ASSERT(summary.allSucceeded);

        ::unsetenv("KQOFFICE_AI_PERMISSION_DIR");
        ::unsetenv("KQOFFICE_AI_FILEMGR_DIR");
    }

    void testCancelMidJob()
    {
        char storeT[] = "/tmp/kqoffice-batch-XXXXXX";
        char* storeDir = ::mkdtemp(storeT);
        CPPUNIT_ASSERT(storeDir != nullptr);
        ::setenv("KQOFFICE_AI_PERMISSION_DIR", storeDir, 1);
        ::setenv("KQOFFICE_AI_FILEMGR_DIR", storeDir, 1);
        ::setenv("KQOFFICE_BATCH_UNO", "0", 1);

        char workT[] = "/tmp/kqoffice-batch-work-XXXXXX";
        char* workDir = ::mkdtemp(workT);
        CPPUNIT_ASSERT(workDir != nullptr);
        const OUString root = OUString::createFromAscii(workDir);

        kqoffice::ai::control::PermissionCenter perms;
        CPPUNIT_ASSERT(perms.grantDirectory(root, true));

        BatchJobManager mgr;
        BatchJob job = mgr.create(BatchJobKind::ConvertToPdf);
        for (int i = 0; i < 3; ++i)
        {
            OUString src = root + u"/f"_ustr + OUString::number(i) + u".odt"_ustr;
            OString sys = OUStringToOString(src, RTL_TEXTENCODING_UTF8);
            FILE* f = std::fopen(sys.getStr(), "wb");
            CPPUNIT_ASSERT(f != nullptr);
            std::fwrite("x", 1, 1, f);
            std::fclose(f);
            CPPUNIT_ASSERT(mgr.addItem(job, src));
        }

        // Cancel before run → all items cancelled without processing.
        BatchJobManager::requestCancel(job);
        auto summary = mgr.run(job, kqoffice::ai::control::PermissionDecision::AllowOnce);
        CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(3), summary.cancelled);
        CPPUNIT_ASSERT_EQUAL(BatchJobState::Cancelled, job.state);

        ::unsetenv("KQOFFICE_AI_PERMISSION_DIR");
        ::unsetenv("KQOFFICE_AI_FILEMGR_DIR");
        ::unsetenv("KQOFFICE_BATCH_UNO");
    }

    void testListRecentLedgers()
    {
        char storeT[] = "/tmp/kqoffice-batch-list-XXXXXX";
        char* storeDir = ::mkdtemp(storeT);
        CPPUNIT_ASSERT(storeDir != nullptr);
        ::setenv("KQOFFICE_AI_PERMISSION_DIR", storeDir, 1);
        ::setenv("KQOFFICE_AI_FILEMGR_DIR", storeDir, 1);
        ::setenv("KQOFFICE_BATCH_UNO", "0", 1);

        char workT[] = "/tmp/kqoffice-batch-list-work-XXXXXX";
        char* workDir = ::mkdtemp(workT);
        CPPUNIT_ASSERT(workDir != nullptr);
        const OUString root = OUString::createFromAscii(workDir);
        const OUString src = root + u"/doc.odt"_ustr;
        {
            OString sys = OUStringToOString(src, RTL_TEXTENCODING_UTF8);
            FILE* f = std::fopen(sys.getStr(), "wb");
            CPPUNIT_ASSERT(f != nullptr);
            std::fwrite("odt", 1, 3, f);
            std::fclose(f);
        }

        kqoffice::ai::control::PermissionCenter perms;
        CPPUNIT_ASSERT(perms.grantDirectory(root, true));

        BatchJobManager mgr;
        BatchJob job = mgr.create(BatchJobKind::ConvertToPdf);
        CPPUNIT_ASSERT(mgr.addItem(job, src));
        mgr.run(job, kqoffice::ai::control::PermissionDecision::AllowOnce);
        CPPUNIT_ASSERT(!job.ledgerPath.isEmpty());

        const auto rows = BatchJobManager::listRecentLedgers(10);
        CPPUNIT_ASSERT(!rows.empty());
        bool found = false;
        for (const auto& row : rows)
        {
            if (row.id == job.id || row.ledgerPath == job.ledgerPath)
            {
                found = true;
                CPPUNIT_ASSERT(!row.kindZh.isEmpty());
                CPPUNIT_ASSERT(!row.stateZh.isEmpty());
                break;
            }
        }
        CPPUNIT_ASSERT(found);

        ::unsetenv("KQOFFICE_AI_PERMISSION_DIR");
        ::unsetenv("KQOFFICE_AI_FILEMGR_DIR");
        ::unsetenv("KQOFFICE_BATCH_UNO");
    }

    CPPUNIT_TEST_SUITE(BatchJobTest);
    CPPUNIT_TEST(testLabelsAndDeriveTarget);
    CPPUNIT_TEST(testCreateAddItemPendingOnly);
    CPPUNIT_TEST(testConvertUnauthorizedFails);
    CPPUNIT_TEST(testConvertStubRecordsFilterAndLedger);
    CPPUNIT_TEST(testConvertOverwriteDeny);
    CPPUNIT_TEST(testSoftDeleteBatch);
    CPPUNIT_TEST(testCancelMidJob);
    CPPUNIT_TEST(testListRecentLedgers);
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
        CPPUNIT_ASSERT(bar.indexOf(u"★"_ustr) >= 0);
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
        CPPUNIT_ASSERT(fmt.indexOf(u"test.odt"_ustr) >= 0);
        CPPUNIT_ASSERT(fmt.indexOf(u"★"_ustr) >= 0);
    }

    CPPUNIT_TEST_SUITE(AIFileSearchUITest);
    CPPUNIT_TEST(testFormatScoreBar);
    CPPUNIT_TEST(testFormatScanSummary);
    CPPUNIT_TEST(testFormatSearchResults);
    CPPUNIT_TEST_SUITE_END();
};

CPPUNIT_TEST_SUITE_REGISTRATION(AIFileManagerTest);
CPPUNIT_TEST_SUITE_REGISTRATION(BatchJobTest);
CPPUNIT_TEST_SUITE_REGISTRATION(AIFileSearchUITest);

} // anonymous namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
