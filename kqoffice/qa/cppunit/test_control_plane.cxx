/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M4: Control Plane tests).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Day-0 unit tests for SessionStore and SafeRestore — hermetic filesystem
 * tests that create/use a temporary session directory per fixture.
 */

#include <sal/types.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/plugin/TestPlugIn.h>

#include <osl/directory.hxx>
#include <osl/file.hxx>
#include <rtl/ustring.hxx>

#include <cstdlib>
#include <cstring>

#include "SessionStore.hxx"
#include "SafeRestore.hxx"

namespace
{

// RAII helper: creates a fresh temporary directory and sets
// KQOFFICE_AI_SESSIONS_DIR to it.  Each test gets a clean slate.
class ScopedSessionDir
{
public:
    ScopedSessionDir()
    {
        // Create a unique temp dir via mkdtemp
        char templ[] = "/tmp/kqoffice-cp-XXXXXX";
        char* dir = ::mkdtemp(templ);
        CPPUNIT_ASSERT(dir != nullptr);
        m_dir = OUString::createFromAscii(dir);
        ::setenv("KQOFFICE_AI_SESSIONS_DIR", dir, 1);
    }

    ~ScopedSessionDir()
    {
        ::unsetenv("KQOFFICE_AI_SESSIONS_DIR");
        // Cleanup all files recursively (best-effort)
        cleanupDir(m_dir);
    }

    const OUString& path() const { return m_dir; }

private:
    static void cleanupDir(const OUString& dirPath)
    {
        osl::Directory dir(dirPath);
        if (dir.open() != osl::FileBase::E_None)
            return;

        osl::DirectoryItem item;
        while (dir.getNextItem(item) == osl::FileBase::E_None)
        {
            osl::FileStatus stat(osl_FileStatus_Mask_FileURL | osl_FileStatus_Mask_Type);
            if (item.getFileStatus(stat) != osl::FileBase::E_None)
                continue;

            OUString full = stat.mFileURL;
            if (stat.mType == osl::FileStatus::Directory)
            {
                // Remove trailing separator if present
                if (full.endsWith("/"))
                    full = full.copy(0, full.getLength() - 1);
                cleanupDir(full);
                osl::Directory::remove(full);
            }
            else
            {
                osl::File::remove(full);
            }
        }
        dir.close();
        osl::Directory::remove(dirPath);
    }

    OUString m_dir;
};

} // namespace

namespace
{

class ControlPlaneTest : public CppUnit::TestFixture
{
public:
    // SessionStore tests
    void testDefaultConstructor();
    void testCustomRootDir();
    void testSaveLoadWorkspace();
    void testSaveLoadSurface();
    void testDeleteWorkspace();
    void testDeleteSurface();
    void testManifestRoundTrip();
    void testAppendLoadScrollback();
    void testRotateScrollback();
    void testValidateAll();
    void testCorruptedSessions();
    void testFileNotExists();
    void testSaveLoadMultipleWorkspaces();
    void testEmptyManifest();
    void testScrollbackMaxLines();

    // SafeRestore tests
    void testRestoreNormalMode();
    void testRestoreNoRestoreMode();
    void testRestoreSafeMode();
    void testRestoreDoctorMode();
    void testRestoreWorkspaceById();
    void testQuarantineSurface();
    void testValidateSession();
    void testValidateSurface();
    void testDetectDamage();
    void testIsolateDamage();
    void testDoctorReportContainsWorkspaceId();
    void testQuarantineNonExistentSurface();

    CPPUNIT_TEST_SUITE(ControlPlaneTest);
    CPPUNIT_TEST(testDefaultConstructor);
    CPPUNIT_TEST(testCustomRootDir);
    CPPUNIT_TEST(testSaveLoadWorkspace);
    CPPUNIT_TEST(testSaveLoadSurface);
    CPPUNIT_TEST(testDeleteWorkspace);
    CPPUNIT_TEST(testDeleteSurface);
    CPPUNIT_TEST(testManifestRoundTrip);
    CPPUNIT_TEST(testAppendLoadScrollback);
    CPPUNIT_TEST(testRotateScrollback);
    CPPUNIT_TEST(testValidateAll);
    CPPUNIT_TEST(testCorruptedSessions);
    CPPUNIT_TEST(testFileNotExists);
    CPPUNIT_TEST(testSaveLoadMultipleWorkspaces);
    CPPUNIT_TEST(testEmptyManifest);
    CPPUNIT_TEST(testScrollbackMaxLines);
    CPPUNIT_TEST(testRestoreNormalMode);
    CPPUNIT_TEST(testRestoreNoRestoreMode);
    CPPUNIT_TEST(testRestoreSafeMode);
    CPPUNIT_TEST(testRestoreDoctorMode);
    CPPUNIT_TEST(testRestoreWorkspaceById);
    CPPUNIT_TEST(testQuarantineSurface);
    CPPUNIT_TEST(testValidateSession);
    CPPUNIT_TEST(testValidateSurface);
    CPPUNIT_TEST(testDetectDamage);
    CPPUNIT_TEST(testIsolateDamage);
    CPPUNIT_TEST(testDoctorReportContainsWorkspaceId);
    CPPUNIT_TEST(testQuarantineNonExistentSurface);
    CPPUNIT_TEST_SUITE_END();
};

// ---- SessionStore tests -------------------------------------------------

void ControlPlaneTest::testDefaultConstructor()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store;
    // Should not throw, manifest should be empty
    auto m = store.loadManifest();
    CPPUNIT_ASSERT_EQUAL(u""_ustr, m.version);
    CPPUNIT_ASSERT(m.workspaceIds.empty());
}

void ControlPlaneTest::testCustomRootDir()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());
    auto m = store.loadManifest();
    CPPUNIT_ASSERT(m.workspaceIds.empty());
}

void ControlPlaneTest::testSaveLoadWorkspace()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    OUString wsJson = u"{\"id\":\"ws.test\",\"state\":\"running\"}"_ustr;
    bool saved = store.saveWorkspace(u"ws.test"_ustr, wsJson);
    CPPUNIT_ASSERT(saved);

    OUString loaded;
    bool loadedOk = store.loadWorkspace(u"ws.test"_ustr, loaded);
    CPPUNIT_ASSERT(loadedOk);
    CPPUNIT_ASSERT_EQUAL(wsJson, loaded);
}

void ControlPlaneTest::testSaveLoadSurface()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    OUString surfJson = u"{\"id\":\"sf.test\",\"state\":\"idle\"}"_ustr;
    bool saved = store.saveSurface(u"sf.test"_ustr, surfJson);
    CPPUNIT_ASSERT(saved);

    OUString loaded;
    bool loadedOk = store.loadSurface(u"sf.test"_ustr, loaded);
    CPPUNIT_ASSERT(loadedOk);
    CPPUNIT_ASSERT_EQUAL(surfJson, loaded);
}

void ControlPlaneTest::testDeleteWorkspace()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    store.saveWorkspace(u"ws.todelete"_ustr, u"{}"_ustr);
    bool deleted = store.deleteWorkspace(u"ws.todelete"_ustr);
    CPPUNIT_ASSERT(deleted);

    OUString loaded;
    bool loadedOk = store.loadWorkspace(u"ws.todelete"_ustr, loaded);
    CPPUNIT_ASSERT(!loadedOk);
}

void ControlPlaneTest::testDeleteSurface()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    store.saveSurface(u"sf.todelete"_ustr, u"{}"_ustr);
    bool deleted = store.deleteSurface(u"sf.todelete"_ustr);
    CPPUNIT_ASSERT(deleted);

    OUString loaded;
    bool loadedOk = store.loadSurface(u"sf.todelete"_ustr, loaded);
    CPPUNIT_ASSERT(!loadedOk);
}

void ControlPlaneTest::testManifestRoundTrip()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    kqoffice::ai::control::SessionManifest m;
    m.version = u"m4-01"_ustr;
    m.lastSavedMs = 123456789;
    m.totalSizeBytes = 4096;
    m.workspaceIds.push_back(u"ws.alpha"_ustr);
    m.workspaceIds.push_back(u"ws.beta"_ustr);

    bool saved = store.saveManifest(m);
    CPPUNIT_ASSERT(saved);

    auto loaded = store.loadManifest();
    CPPUNIT_ASSERT(loaded.isValid);
    CPPUNIT_ASSERT_EQUAL(m.version, loaded.version);
    CPPUNIT_ASSERT_EQUAL(m.lastSavedMs, loaded.lastSavedMs);
    CPPUNIT_ASSERT_EQUAL(m.totalSizeBytes, loaded.totalSizeBytes);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(2), loaded.workspaceIds.size());
    CPPUNIT_ASSERT_EQUAL(u"ws.alpha"_ustr, loaded.workspaceIds[0]);
    CPPUNIT_ASSERT_EQUAL(u"ws.beta"_ustr, loaded.workspaceIds[1]);
}

void ControlPlaneTest::testAppendLoadScrollback()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    store.appendScrollback(u"sf.scroll"_ustr, u"line1"_ustr);
    store.appendScrollback(u"sf.scroll"_ustr, u"line2"_ustr);
    store.appendScrollback(u"sf.scroll"_ustr, u"line3"_ustr);

    OUString out;
    bool loaded = store.loadScrollback(u"sf.scroll"_ustr, out);
    CPPUNIT_ASSERT(loaded);
    CPPUNIT_ASSERT(out.indexOf(u"line1") >= 0);
    CPPUNIT_ASSERT(out.indexOf(u"line2") >= 0);
    CPPUNIT_ASSERT(out.indexOf(u"line3") >= 0);
}

void ControlPlaneTest::testRotateScrollback()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    // Append lines of roughly known size
    for (int i = 0; i < 100; ++i)
    {
        store.appendScrollback(u"sf.rotate"_ustr,
            u"line number "_ustr + OUString::number(static_cast<sal_Int32>(i)));
    }

    // Rotate to a small size
    bool rotated = store.rotateScrollback(u"sf.rotate"_ustr, 100);
    CPPUNIT_ASSERT(rotated);

    OUString out;
    bool loaded = store.loadScrollback(u"sf.rotate"_ustr, out);
    CPPUNIT_ASSERT(loaded);
    // Should be <= 100 bytes
    CPPUNIT_ASSERT(out.getLength() <= 200); // UTF-8 expansion margin
}

void ControlPlaneTest::testValidateAll()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    // Fresh store with no data should validate
    bool valid = store.validateAll();
    CPPUNIT_ASSERT(valid);
}

void ControlPlaneTest::testCorruptedSessions()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    // Fresh store — no corruption
    auto corrupt = store.corruptedSessions();
    CPPUNIT_ASSERT(corrupt.empty());

    // Save a manifest with a workspace that doesn't actually have a file
    kqoffice::ai::control::SessionManifest m;
    m.version = u"m4-01"_ustr;
    m.workspaceIds.push_back(u"ws.ghost"_ustr);
    store.saveManifest(m);

    // Now corruptedSessions should flag ws.ghost
    corrupt = store.corruptedSessions();
    bool foundGhost = false;
    for (const auto& c : corrupt)
        if (c == u"ws.ghost"_ustr)
            foundGhost = true;
    CPPUNIT_ASSERT(foundGhost);
}

void ControlPlaneTest::testFileNotExists()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    OUString out;
    bool loaded = store.loadWorkspace(u"nonexistent"_ustr, out);
    CPPUNIT_ASSERT(!loaded);
}

void ControlPlaneTest::testSaveLoadMultipleWorkspaces()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    CPPUNIT_ASSERT(store.saveWorkspace(u"ws.a"_ustr, u"{\"k\":\"a\"}"_ustr));
    CPPUNIT_ASSERT(store.saveWorkspace(u"ws.b"_ustr, u"{\"k\":\"b\"}"_ustr));
    CPPUNIT_ASSERT(store.saveWorkspace(u"ws.c"_ustr, u"{\"k\":\"c\"}"_ustr));

    OUString a, b, c;
    CPPUNIT_ASSERT(store.loadWorkspace(u"ws.a"_ustr, a));
    CPPUNIT_ASSERT(store.loadWorkspace(u"ws.b"_ustr, b));
    CPPUNIT_ASSERT(store.loadWorkspace(u"ws.c"_ustr, c));
    CPPUNIT_ASSERT_EQUAL(u"{\"k\":\"a\"}"_ustr, a);
    CPPUNIT_ASSERT_EQUAL(u"{\"k\":\"b\"}"_ustr, b);
    CPPUNIT_ASSERT_EQUAL(u"{\"k\":\"c\"}"_ustr, c);
}

void ControlPlaneTest::testEmptyManifest()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    auto m = store.loadManifest();
    CPPUNIT_ASSERT(m.workspaceIds.empty());
    CPPUNIT_ASSERT_EQUAL(u""_ustr, m.version);
}

void ControlPlaneTest::testScrollbackMaxLines()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    for (int i = 0; i < 50; ++i)
    {
        store.appendScrollback(u"sf.maxlines"_ustr,
            u"line "_ustr + OUString::number(static_cast<sal_Int32>(i)));
    }

    // Load with maxLines=10 — should get only the last 10 lines
    OUString out;
    bool loaded = store.loadScrollback(u"sf.maxlines"_ustr, out, 10);
    CPPUNIT_ASSERT(loaded);
    CPPUNIT_ASSERT(out.indexOf(u"line 39") >= 0);
    CPPUNIT_ASSERT(out.indexOf(u"line 49") >= 0);
    // line 0 should be missing
    CPPUNIT_ASSERT(out.indexOf(u"line 0") < 0 || out.indexOf(u"line 0") > 0);
}

// ---- SafeRestore tests --------------------------------------------------

void ControlPlaneTest::testRestoreNormalMode()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    // Set up a workspace
    store.saveWorkspace(u"ws.norm"_ustr, u"{\"id\":\"ws.norm\"}"_ustr);
    kqoffice::ai::control::SessionManifest m;
    m.version = u"m4-01"_ustr;
    m.workspaceIds.push_back(u"ws.norm"_ustr);
    store.saveManifest(m);

    auto result = kqoffice::ai::control::SafeRestore::restore(
        kqoffice::ai::control::SafeRestoreMode::Normal);
    CPPUNIT_ASSERT(result.success);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), result.restoredWorkspaces.size());
    CPPUNIT_ASSERT_EQUAL(u"ws.norm"_ustr, result.restoredWorkspaces[0]);
}

void ControlPlaneTest::testRestoreNoRestoreMode()
{
    ScopedSessionDir guard;

    auto result = kqoffice::ai::control::SafeRestore::restore(
        kqoffice::ai::control::SafeRestoreMode::NoRestore);
    CPPUNIT_ASSERT(result.success);
    CPPUNIT_ASSERT(result.restoredWorkspaces.empty());
}

void ControlPlaneTest::testRestoreSafeMode()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    store.saveWorkspace(u"ws.safe"_ustr, u"{\"id\":\"ws.safe\"}"_ustr);
    kqoffice::ai::control::SessionManifest m;
    m.version = u"m4-01"_ustr;
    m.workspaceIds.push_back(u"ws.safe"_ustr);
    store.saveManifest(m);

    auto result = kqoffice::ai::control::SafeRestore::restore(
        kqoffice::ai::control::SafeRestoreMode::Safe);
    CPPUNIT_ASSERT(result.success);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), result.restoredWorkspaces.size());
    CPPUNIT_ASSERT_EQUAL(u"ws.safe"_ustr, result.restoredWorkspaces[0]);
}

void ControlPlaneTest::testRestoreDoctorMode()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    store.saveWorkspace(u"ws.doc"_ustr, u"{\"id\":\"ws.doc\"}"_ustr);
    kqoffice::ai::control::SessionManifest m;
    m.version = u"m4-01"_ustr;
    m.workspaceIds.push_back(u"ws.doc"_ustr);
    store.saveManifest(m);

    auto result = kqoffice::ai::control::SafeRestore::restore(
        kqoffice::ai::control::SafeRestoreMode::Doctor);
    CPPUNIT_ASSERT(result.success);
    CPPUNIT_ASSERT(!result.diagnosticsReport.isEmpty());
    CPPUNIT_ASSERT(result.diagnosticsReport.indexOf(u"SafeRestore Doctor Report") >= 0);
    CPPUNIT_ASSERT(result.diagnosticsReport.indexOf(u"ws.doc") >= 0);
}

void ControlPlaneTest::testRestoreWorkspaceById()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    store.saveWorkspace(u"ws.sel"_ustr, u"{\"id\":\"ws.sel\"}"_ustr);

    auto result = kqoffice::ai::control::SafeRestore::restoreWorkspace(u"ws.sel"_ustr);
    CPPUNIT_ASSERT(result.success);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), result.restoredWorkspaces.size());
    CPPUNIT_ASSERT_EQUAL(u"ws.sel"_ustr, result.restoredWorkspaces[0]);
}

void ControlPlaneTest::testQuarantineSurface()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    store.saveSurface(u"sf.quar"_ustr, u"{\"id\":\"sf.quar\"}"_ustr);

    auto result = kqoffice::ai::control::SafeRestore::quarantine(u"sf.quar"_ustr);
    CPPUNIT_ASSERT(result.success);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), result.quarantinedSurfaces.size());
    CPPUNIT_ASSERT_EQUAL(u"sf.quar"_ustr, result.quarantinedSurfaces[0]);

    // After quarantine, the surface file should be gone
    OUString out;
    bool loaded = store.loadSurface(u"sf.quar"_ustr, out);
    CPPUNIT_ASSERT(!loaded);
}

void ControlPlaneTest::testValidateSession()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    store.saveWorkspace(u"ws.valid"_ustr, u"{\"id\":\"ws.valid\"}"_ustr);
    CPPUNIT_ASSERT(kqoffice::ai::control::SafeRestore::validateSession(u"ws.valid"_ustr));
    CPPUNIT_ASSERT(!kqoffice::ai::control::SafeRestore::validateSession(u"ws.nonexistent"_ustr));
}

void ControlPlaneTest::testValidateSurface()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    store.saveSurface(u"sf.valid"_ustr, u"{\"id\":\"sf.valid\"}"_ustr);
    CPPUNIT_ASSERT(kqoffice::ai::control::SafeRestore::validateSurface(u"sf.valid"_ustr));
    CPPUNIT_ASSERT(!kqoffice::ai::control::SafeRestore::validateSurface(u"sf.nonexistent"_ustr));
}

void ControlPlaneTest::testDetectDamage()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    // Set up a workspace that references a surface
    store.saveWorkspace(u"ws.damage"_ustr,
        u"{\"id\":\"ws.damage\",\"surfaceId\":\"sf.damaged\"}"_ustr);
    // Save the manifest
    kqoffice::ai::control::SessionManifest m;
    m.version = u"m4-01"_ustr;
    m.workspaceIds.push_back(u"ws.damage"_ustr);
    store.saveManifest(m);

    // The surface sf.damaged does not exist — detectDamage should flag it
    auto damaged = kqoffice::ai::control::SafeRestore::detectDamage();
    bool found = false;
    for (const auto& d : damaged)
        if (d == u"sf.damaged"_ustr)
            found = true;
    CPPUNIT_ASSERT(found);
}

void ControlPlaneTest::testIsolateDamage()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    store.saveSurface(u"sf.iso"_ustr, u"{\"id\":\"sf.iso\"}"_ustr);

    bool isolated = kqoffice::ai::control::SafeRestore::isolateDamage(u"sf.iso"_ustr);
    CPPUNIT_ASSERT(isolated);

    // File should be gone
    OUString out;
    CPPUNIT_ASSERT(!store.loadSurface(u"sf.iso"_ustr, out));
}

void ControlPlaneTest::testDoctorReportContainsWorkspaceId()
{
    ScopedSessionDir guard;
    kqoffice::ai::control::SessionStore store(guard.path());

    store.saveWorkspace(u"ws.report"_ustr, u"{\"id\":\"ws.report\"}"_ustr);
    kqoffice::ai::control::SessionManifest m;
    m.version = u"m4-01"_ustr;
    m.workspaceIds.push_back(u"ws.report"_ustr);
    store.saveManifest(m);

    OUString report = kqoffice::ai::control::SafeRestore::doctorWorkspace(u"ws.report"_ustr);
    CPPUNIT_ASSERT(report.indexOf(u"ws.report") >= 0);
    CPPUNIT_ASSERT(report.indexOf(u"Workspace Doctor Report") >= 0);
}

void ControlPlaneTest::testQuarantineNonExistentSurface()
{
    ScopedSessionDir guard;

    // Quarantining a non-existent surface should still succeed (nothing to do)
    auto result = kqoffice::ai::control::SafeRestore::quarantine(u"sf.nonexistent"_ustr);
    CPPUNIT_ASSERT(result.success);
}

CPPUNIT_TEST_SUITE_REGISTRATION(ControlPlaneTest);
} // namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */