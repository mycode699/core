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

#include <osl/file.hxx>
#include <rtl/ustring.hxx>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <rtl/string.hxx>

#include "SessionStore.hxx"
#include "SafeRestore.hxx"
#include "PermissionCenter.hxx"
#include "PermissionGrant.hxx"

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

            OUString full = stat.getFileURL();
            if (stat.getFileType() == osl::FileStatus::Directory)
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

    // PermissionCenter tests
    void testPermissionDefaultDeniesNetwork();
    void testPermissionDirectoryGrantRevoke();
    void testPermissionRejectsSymlinkEscape();
    void testPermissionNetworkDisclosureRequiresConfirm();
    void testPermissionCapabilityLabelsZh();
    void testPermissionRiskConfirmDenyOnceSession();
    void testPermissionSettingsSurfaceSummary();

    // PermissionGrant (Wave D4 session allow + clarify headless)
    void testPermissionGrantSessionAllow();
    void testPermissionGrantApplyDecision();
    void testPermissionGrantResolveHeadless();
    void testPermissionGrantDecisionLabelsZh();

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
    CPPUNIT_TEST(testPermissionDefaultDeniesNetwork);
    CPPUNIT_TEST(testPermissionDirectoryGrantRevoke);
    CPPUNIT_TEST(testPermissionRejectsSymlinkEscape);
    CPPUNIT_TEST(testPermissionNetworkDisclosureRequiresConfirm);
    CPPUNIT_TEST(testPermissionCapabilityLabelsZh);
    CPPUNIT_TEST(testPermissionRiskConfirmDenyOnceSession);
    CPPUNIT_TEST(testPermissionSettingsSurfaceSummary);
    CPPUNIT_TEST(testPermissionGrantSessionAllow);
    CPPUNIT_TEST(testPermissionGrantApplyDecision);
    CPPUNIT_TEST(testPermissionGrantResolveHeadless);
    CPPUNIT_TEST(testPermissionGrantDecisionLabelsZh);
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

    // Load with maxLines=10 — should get only the last 10 lines (40..49).
    OUString out;
    bool loaded = store.loadScrollback(u"sf.maxlines"_ustr, out, 10);
    CPPUNIT_ASSERT(loaded);
    CPPUNIT_ASSERT(out.indexOf(u"line 40") >= 0);
    CPPUNIT_ASSERT(out.indexOf(u"line 49") >= 0);
    // Older lines must be dropped.
    CPPUNIT_ASSERT(out.indexOf(u"line 0\n") < 0);
    CPPUNIT_ASSERT(out.indexOf(u"line 39") < 0);
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

// ---- PermissionCenter tests --------------------------------------------

void ControlPlaneTest::testPermissionDefaultDeniesNetwork()
{
    char templ[] = "/tmp/kqoffice-perm-XXXXXX";
    char* dir = ::mkdtemp(templ);
    CPPUNIT_ASSERT(dir != nullptr);
    ::setenv("KQOFFICE_AI_PERMISSION_DIR", dir, 1);

    kqoffice::ai::control::PermissionCenter pc;
    CPPUNIT_ASSERT(!pc.isGranted(kqoffice::ai::control::CapabilityPermission::NetworkEgress));
    CPPUNIT_ASSERT(!pc.isGranted(kqoffice::ai::control::CapabilityPermission::Microphone));
    CPPUNIT_ASSERT(!pc.isGranted(kqoffice::ai::control::CapabilityPermission::ScreenCapture));
    CPPUNIT_ASSERT(!pc.hasAnyAuthorizedDirectory());

    ::unsetenv("KQOFFICE_AI_PERMISSION_DIR");
}

void ControlPlaneTest::testPermissionDirectoryGrantRevoke()
{
    char templ[] = "/tmp/kqoffice-perm-XXXXXX";
    char* dir = ::mkdtemp(templ);
    CPPUNIT_ASSERT(dir != nullptr);
    ::setenv("KQOFFICE_AI_PERMISSION_DIR", dir, 1);

    char scanT[] = "/tmp/kqoffice-authorized-XXXXXX";
    char* scanDir = ::mkdtemp(scanT);
    CPPUNIT_ASSERT(scanDir != nullptr);
    const OUString scanRoot = OUString::createFromAscii(scanDir);

    kqoffice::ai::control::PermissionCenter pc;
    CPPUNIT_ASSERT(pc.grantDirectory(scanRoot, true));
    CPPUNIT_ASSERT(pc.hasAnyAuthorizedDirectory());
    CPPUNIT_ASSERT(pc.isPathAuthorized(scanRoot));
    CPPUNIT_ASSERT(!pc.isPathAuthorized(u"/tmp/definitely-not-authorized"_ustr));
    CPPUNIT_ASSERT(pc.revokeDirectory(scanRoot));
    CPPUNIT_ASSERT(!pc.hasAnyAuthorizedDirectory());

    ::unsetenv("KQOFFICE_AI_PERMISSION_DIR");
}

void ControlPlaneTest::testPermissionRejectsSymlinkEscape()
{
    char storeT[] = "/tmp/kqoffice-perm-XXXXXX";
    char rootT[] = "/tmp/kqoffice-root-XXXXXX";
    char outsideT[] = "/tmp/kqoffice-outside-XXXXXX";
    char* storeDir = ::mkdtemp(storeT);
    char* rootDir = ::mkdtemp(rootT);
    char* outsideDir = ::mkdtemp(outsideT);
    CPPUNIT_ASSERT(storeDir && rootDir && outsideDir);
    ::setenv("KQOFFICE_AI_PERMISSION_DIR", storeDir, 1);

    const OString linkPath = OString(rootDir) + "/outside";
    CPPUNIT_ASSERT_EQUAL(0, ::symlink(outsideDir, linkPath.getStr()));

    kqoffice::ai::control::PermissionCenter pc;
    const OUString root = OUString::createFromAscii(rootDir);
    CPPUNIT_ASSERT(pc.grantDirectory(root, true));
    CPPUNIT_ASSERT(pc.isPathAuthorized(root));
    CPPUNIT_ASSERT(!pc.isPathAuthorized(
        OUString::createFromAscii(linkPath.getStr())));

    ::unsetenv("KQOFFICE_AI_PERMISSION_DIR");
}

void ControlPlaneTest::testPermissionNetworkDisclosureRequiresConfirm()
{
    char templ[] = "/tmp/kqoffice-perm-XXXXXX";
    char* dir = ::mkdtemp(templ);
    CPPUNIT_ASSERT(dir != nullptr);
    ::setenv("KQOFFICE_AI_PERMISSION_DIR", dir, 1);

    kqoffice::ai::control::PermissionCenter pc;
    kqoffice::ai::control::NetworkDisclosure d;
    d.provider = u"openai-compatible:corp"_ustr;
    d.endpoint = u"https://ai.example.corp/v1"_ustr;
    d.scopeSummaryZh = u"仅选中段落（约 120 字）"_ustr;
    d.userConfirmed = true;

    // Without NetworkEgress grant, disclosure must fail.
    CPPUNIT_ASSERT(!pc.discloseNetworkSend(d));

    CPPUNIT_ASSERT(pc.grant(kqoffice::ai::control::CapabilityPermission::NetworkEgress));
    d.userConfirmed = false;
    CPPUNIT_ASSERT(!pc.discloseNetworkSend(d));
    d.userConfirmed = true;
    CPPUNIT_ASSERT(pc.discloseNetworkSend(d));
    CPPUNIT_ASSERT(pc.lastNetworkDisclosureSummary().indexOf(u"corp") >= 0);

    ::unsetenv("KQOFFICE_AI_PERMISSION_DIR");
}

void ControlPlaneTest::testPermissionCapabilityLabelsZh()
{
    using kqoffice::ai::control::CapabilityPermission;
    using kqoffice::ai::control::PermissionCenter;
    using kqoffice::ai::control::PermissionDecision;
    using kqoffice::ai::control::PermissionState;
    CPPUNIT_ASSERT(!PermissionCenter::capabilityLabelZh(CapabilityPermission::FolderScan).isEmpty());
    CPPUNIT_ASSERT(!PermissionCenter::capabilityLabelZh(CapabilityPermission::NetworkEgress).isEmpty());
    CPPUNIT_ASSERT_EQUAL(u"已授权"_ustr,
        PermissionCenter::stateLabelZh(PermissionState::Granted));
    CPPUNIT_ASSERT_EQUAL(u"拒绝"_ustr,
        PermissionCenter::riskConfirmationLabelZh(PermissionDecision::Deny));
    CPPUNIT_ASSERT(!PermissionCenter::riskConfirmationLabelZh(PermissionDecision::AllowOnce).isEmpty());
    CPPUNIT_ASSERT(!PermissionCenter::riskConfirmationLabelZh(PermissionDecision::AllowSession).isEmpty());
}

void ControlPlaneTest::testPermissionRiskConfirmDenyOnceSession()
{
    using kqoffice::ai::control::PermissionCenter;
    using kqoffice::ai::control::PermissionDecision;
    using kqoffice::ai::control::PermissionGrant;
    using kqoffice::ai::control::RiskOperation;

    char storeT[] = "/tmp/kqoffice-perm-XXXXXX";
    char scanT[] = "/tmp/kqoffice-authorized-XXXXXX";
    char* storeDir = ::mkdtemp(storeT);
    char* scanDir = ::mkdtemp(scanT);
    CPPUNIT_ASSERT(storeDir && scanDir);
    ::setenv("KQOFFICE_AI_PERMISSION_DIR", storeDir, 1);
    PermissionGrant::clearAllSessionAllows();

    const OUString root = OUString::createFromAscii(scanDir);
    const OUString file = root + u"/report.odt"_ustr;
    {
        const OString sys = OUStringToOString(file, RTL_TEXTENCODING_UTF8);
        FILE* f = std::fopen(sys.getStr(), "wb");
        CPPUNIT_ASSERT(f != nullptr);
        std::fwrite("x", 1, 1, f);
        std::fclose(f);
    }

    PermissionCenter pc;
    // Outside workspace always denied.
    CPPUNIT_ASSERT(!pc.resolveRiskyOp(RiskOperation::Delete, file, PermissionDecision::AllowOnce));

    CPPUNIT_ASSERT(pc.grantDirectory(root, true));
    CPPUNIT_ASSERT(pc.needsRiskConfirm(RiskOperation::Delete, file));
    CPPUNIT_ASSERT(!pc.resolveRiskyOp(RiskOperation::Delete, file, PermissionDecision::Deny));
    CPPUNIT_ASSERT(pc.needsRiskConfirm(RiskOperation::Delete, file));

    // AllowOnce: pass once, still need confirm next time.
    CPPUNIT_ASSERT(pc.resolveRiskyOp(RiskOperation::Delete, file, PermissionDecision::AllowOnce));
    CPPUNIT_ASSERT(pc.needsRiskConfirm(RiskOperation::Delete, file));

    // AllowSession: subsequent ops under same root skip confirm.
    CPPUNIT_ASSERT(pc.resolveRiskyOp(RiskOperation::Delete, file, PermissionDecision::AllowSession));
    CPPUNIT_ASSERT(!pc.needsRiskConfirm(RiskOperation::Delete, file));
    CPPUNIT_ASSERT(pc.hasSessionRiskGrant(RiskOperation::Delete, file));
    // Deny is ignored once session grant exists.
    CPPUNIT_ASSERT(pc.resolveRiskyOp(RiskOperation::Delete, file, PermissionDecision::Deny));

    // Overwrite is a separate session grant.
    CPPUNIT_ASSERT(pc.needsRiskConfirm(RiskOperation::Overwrite, file));
    CPPUNIT_ASSERT(pc.resolveRiskyOp(RiskOperation::Overwrite, file, PermissionDecision::AllowSession));
    CPPUNIT_ASSERT(!pc.needsRiskConfirm(RiskOperation::Overwrite, file));

    pc.clearSessionRiskGrants();
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(0), pc.sessionRiskGrantCount());
    CPPUNIT_ASSERT(pc.needsRiskConfirm(RiskOperation::Delete, file));
    CPPUNIT_ASSERT(pc.needsRiskConfirm(RiskOperation::Overwrite, file));

    // Persistence: directories survive reload; session grants do not.
    {
        PermissionCenter pc2;
        CPPUNIT_ASSERT(pc2.isPathAuthorized(file));
        CPPUNIT_ASSERT(pc2.needsRiskConfirm(RiskOperation::Delete, file));
    }

    PermissionGrant::clearAllSessionAllows();
    ::unsetenv("KQOFFICE_AI_PERMISSION_DIR");
}

void ControlPlaneTest::testPermissionSettingsSurfaceSummary()
{
    using kqoffice::ai::control::CapabilityPermission;
    using kqoffice::ai::control::PermissionCenter;
    using kqoffice::ai::control::PermissionDecision;
    using kqoffice::ai::control::PermissionState;

    char templ[] = "/tmp/kqoffice-perm-XXXXXX";
    char* dir = ::mkdtemp(templ);
    CPPUNIT_ASSERT(dir != nullptr);
    ::setenv("KQOFFICE_AI_PERMISSION_DIR", dir, 1);

    PermissionCenter pc;

    // Defaults: network off, mic/screenshot first-use OS prompt wording.
    const OUString mic = pc.settingsCapabilityStatusZh(CapabilityPermission::Microphone);
    CPPUNIT_ASSERT(mic.indexOf(u"麦克风") >= 0);
    CPPUNIT_ASSERT(mic.indexOf(u"使用时系统会询问") >= 0);
    const OUString shot = pc.settingsCapabilityStatusZh(CapabilityPermission::ScreenCapture);
    CPPUNIT_ASSERT(shot.indexOf(u"屏幕截图") >= 0);
    CPPUNIT_ASSERT(shot.indexOf(u"使用时系统会询问") >= 0);

    const OUString net = pc.networkStatusLineZh();
    CPPUNIT_ASSERT(net.indexOf(u"网络") >= 0);
    CPPUNIT_ASSERT(net.indexOf(u"默认关闭") >= 0);
    // No English product titles in settings copy.
    CPPUNIT_ASSERT(net.indexOf(u"AI Assistant") < 0);
    CPPUNIT_ASSERT(net.indexOf(u"Permission") < 0);

    const OUString risk = PermissionCenter::riskPolicyHintZh();
    CPPUNIT_ASSERT(risk.indexOf(PermissionCenter::riskConfirmationLabelZh(PermissionDecision::Deny))
                   >= 0);
    CPPUNIT_ASSERT(
        risk.indexOf(PermissionCenter::riskConfirmationLabelZh(PermissionDecision::AllowOnce))
        >= 0);
    CPPUNIT_ASSERT(
        risk.indexOf(PermissionCenter::riskConfirmationLabelZh(PermissionDecision::AllowSession))
        >= 0);

    const OUString summary = pc.settingsSurfaceSummaryZh();
    CPPUNIT_ASSERT(summary.indexOf(u"工作区") >= 0);
    CPPUNIT_ASSERT(summary.indexOf(u"麦克风") >= 0);
    CPPUNIT_ASSERT(summary.indexOf(u"屏幕截图") >= 0);
    CPPUNIT_ASSERT(summary.indexOf(u"AI Assistant") < 0);

    // After grant: status flips; network still requires confirm wording when granted.
    CPPUNIT_ASSERT(pc.grant(CapabilityPermission::NetworkEgress));
    CPPUNIT_ASSERT(pc.grant(CapabilityPermission::Microphone));
    const OUString netGranted = pc.networkStatusLineZh();
    CPPUNIT_ASSERT(netGranted.indexOf(u"已授权") >= 0
                   || netGranted.indexOf(PermissionCenter::stateLabelZh(PermissionState::Granted))
                          >= 0);
    CPPUNIT_ASSERT(netGranted.indexOf(u"确认") >= 0 || netGranted.indexOf(u"披露") >= 0);
    const OUString micGranted
        = pc.settingsCapabilityStatusZh(CapabilityPermission::Microphone);
    CPPUNIT_ASSERT(micGranted.indexOf(u"已授权") >= 0);

    // Revoke network: settings surface reflects closed state.
    CPPUNIT_ASSERT(pc.revoke(CapabilityPermission::NetworkEgress));
    const OUString netRevoked = pc.networkStatusLineZh();
    CPPUNIT_ASSERT(netRevoked.indexOf(u"已撤销") >= 0 || netRevoked.indexOf(u"已关闭") >= 0);

    ::unsetenv("KQOFFICE_AI_PERMISSION_DIR");
}

// ---- PermissionGrant tests (Wave D4) ------------------------------------

void ControlPlaneTest::testPermissionGrantSessionAllow()
{
    using kqoffice::ai::control::PermissionGrant;

    PermissionGrant::clearAllSessionAllows();
    CPPUNIT_ASSERT(!PermissionGrant::isSessionAllowed(u"apply.diff"_ustr));
    CPPUNIT_ASSERT(PermissionGrant::sessionAllowedActions().empty());

    PermissionGrant::grantSession(u"apply.diff"_ustr);
    CPPUNIT_ASSERT(PermissionGrant::isSessionAllowed(u"apply.diff"_ustr));
    CPPUNIT_ASSERT(!PermissionGrant::isSessionAllowed(u"delete.range"_ustr));

    auto listed = PermissionGrant::sessionAllowedActions();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), listed.size());
    CPPUNIT_ASSERT_EQUAL(u"apply.diff"_ustr, listed[0]);

    PermissionGrant::revokeSession(u"apply.diff"_ustr);
    CPPUNIT_ASSERT(!PermissionGrant::isSessionAllowed(u"apply.diff"_ustr));

    // Empty action id is never allowed / stored
    PermissionGrant::grantSession(u""_ustr);
    CPPUNIT_ASSERT(!PermissionGrant::isSessionAllowed(u""_ustr));
    CPPUNIT_ASSERT(PermissionGrant::sessionAllowedActions().empty());

    PermissionGrant::clearAllSessionAllows();
}

void ControlPlaneTest::testPermissionGrantApplyDecision()
{
    using kqoffice::ai::control::PermissionDecision;
    using kqoffice::ai::control::PermissionGrant;

    PermissionGrant::clearAllSessionAllows();

    PermissionGrant::applyDecision(u"apply.diff"_ustr, PermissionDecision::Deny);
    CPPUNIT_ASSERT(!PermissionGrant::isSessionAllowed(u"apply.diff"_ustr));

    PermissionGrant::applyDecision(u"apply.diff"_ustr, PermissionDecision::AllowOnce);
    CPPUNIT_ASSERT(!PermissionGrant::isSessionAllowed(u"apply.diff"_ustr));

    PermissionGrant::applyDecision(u"apply.diff"_ustr, PermissionDecision::AllowSession);
    CPPUNIT_ASSERT(PermissionGrant::isSessionAllowed(u"apply.diff"_ustr));

    auto autoAllow = PermissionGrant::tryAutoAllow(u"apply.diff"_ustr);
    CPPUNIT_ASSERT(autoAllow.has_value());
    CPPUNIT_ASSERT(*autoAllow == PermissionDecision::AllowSession);

    CPPUNIT_ASSERT(!PermissionGrant::tryAutoAllow(u"other.action"_ustr).has_value());

    PermissionGrant::clearAllSessionAllows();
}

void ControlPlaneTest::testPermissionGrantResolveHeadless()
{
    using kqoffice::ai::control::ClarificationPrompt;
    using kqoffice::ai::control::PermissionDecision;
    using kqoffice::ai::control::PermissionGrant;

    PermissionGrant::clearAllSessionAllows();

    ClarificationPrompt prompt;
    prompt.actionId = u"delete.range"_ustr;
    prompt.messageZh = u"将删除选中的 2 段内容，是否继续？"_ustr;
    prompt.options = { u"保留批注"_ustr, u"同步更新目录"_ustr };

    auto denied = PermissionGrant::resolveHeadless(prompt);
    CPPUNIT_ASSERT(denied.decision == PermissionDecision::Deny);
    CPPUNIT_ASSERT(!denied.fromSessionCache);

    PermissionGrant::grantSession(u"delete.range"_ustr);
    auto allowed = PermissionGrant::resolveHeadless(prompt);
    CPPUNIT_ASSERT(allowed.decision == PermissionDecision::AllowSession);
    CPPUNIT_ASSERT(allowed.fromSessionCache);

    PermissionGrant::clearAllSessionAllows();
}

void ControlPlaneTest::testPermissionGrantDecisionLabelsZh()
{
    using kqoffice::ai::control::PermissionDecision;
    using kqoffice::ai::control::PermissionGrant;

    CPPUNIT_ASSERT_EQUAL(u"拒绝"_ustr,
                         PermissionGrant::decisionLabelZh(PermissionDecision::Deny));
    CPPUNIT_ASSERT_EQUAL(u"仅本次"_ustr,
                         PermissionGrant::decisionLabelZh(PermissionDecision::AllowOnce));
    CPPUNIT_ASSERT_EQUAL(u"本轮对话均允许"_ustr,
                         PermissionGrant::decisionLabelZh(PermissionDecision::AllowSession));
}

CPPUNIT_TEST_SUITE_REGISTRATION(ControlPlaneTest);
} // namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
