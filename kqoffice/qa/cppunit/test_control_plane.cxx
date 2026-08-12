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
#include <string>
#include <unistd.h>
#include <vector>
#include <rtl/string.hxx>

#include "SessionStore.hxx"
#include "SafeRestore.hxx"
#include "PermissionCenter.hxx"
#include "PermissionGrant.hxx"
#include "WritebackPermission.hxx"
#include "ErrorClassifier.hxx"
#include "DiagnosticBundle.hxx"
#include "ComposerQueue.hxx"
#include "WorkbenchPhase.hxx"
#include "ContextUsage.hxx"
#include "AiFirstRunGate.hxx"
#include "WorkbenchStatus.hxx"
#include "FactRouter.hxx"
#include "ProviderSlotManifest.hxx"
#include "PolicyEvolutionGuard.hxx"
#include "EventLedger.hxx"
#include "ExternalWriteDenier.hxx"
#include "StallDetector.hxx"
#include "SandboxCage.hxx"
#include "HandoffBrief.hxx"
#include "AiPaths.hxx"

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

    // WritebackPermission (Ask / once / session / YOLO ladder)
    void testWritebackDefaultAsk();
    void testWritebackExplicitHumanApproval();
    void testWritebackSessionAllow();
    void testWritebackResolveDenyOnceSession();
    void testWritebackActionIdStable();
    void testWritebackScopeFromPlanTargets();

    // ErrorClassifier + DiagnosticBundle
    void testErrorClassifierDecks();
    void testErrorClassifierRedact();
    void testDiagnosticBundleExport();

    // Workbench P1: queue / phase / context / first-run
    void testComposerQueueFifo();
    void testWorkbenchPhaseReadyAndSteps();
    void testContextUsageEstimate();
    void testAiFirstRunGateSkipComplete();
    void testWorkbenchStatusDashboard();
    void testFactRouterShapes();
    void testProviderSlotManifest();
    void testPolicyEvolutionGuard();
    void testEventLedgerPersistBeforeBroadcast();
    void testExternalWriteDenier();
    void testStallDetectorPositiveAndNegative();
    void testSandboxCageCanaries();
    void testHandoffBriefFromEvents();

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
    CPPUNIT_TEST(testWritebackDefaultAsk);
    CPPUNIT_TEST(testWritebackExplicitHumanApproval);
    CPPUNIT_TEST(testWritebackSessionAllow);
    CPPUNIT_TEST(testWritebackResolveDenyOnceSession);
    CPPUNIT_TEST(testWritebackActionIdStable);
    CPPUNIT_TEST(testWritebackScopeFromPlanTargets);
    CPPUNIT_TEST(testErrorClassifierDecks);
    CPPUNIT_TEST(testErrorClassifierRedact);
    CPPUNIT_TEST(testDiagnosticBundleExport);
    CPPUNIT_TEST(testComposerQueueFifo);
    CPPUNIT_TEST(testWorkbenchPhaseReadyAndSteps);
    CPPUNIT_TEST(testContextUsageEstimate);
    CPPUNIT_TEST(testAiFirstRunGateSkipComplete);
    CPPUNIT_TEST(testWorkbenchStatusDashboard);
    CPPUNIT_TEST(testFactRouterShapes);
    CPPUNIT_TEST(testProviderSlotManifest);
    CPPUNIT_TEST(testPolicyEvolutionGuard);
    CPPUNIT_TEST(testEventLedgerPersistBeforeBroadcast);
    CPPUNIT_TEST(testExternalWriteDenier);
    CPPUNIT_TEST(testStallDetectorPositiveAndNegative);
    CPPUNIT_TEST(testSandboxCageCanaries);
    CPPUNIT_TEST(testHandoffBriefFromEvents);
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

// ---- WritebackPermission ------------------------------------------------

void ControlPlaneTest::testWritebackDefaultAsk()
{
    using kqoffice::ai::control::WritebackGateRequest;
    using kqoffice::ai::control::WritebackPermission;
    using kqoffice::ai::control::WritebackTier;

    ::unsetenv("KQOFFICE_AI_WRITEBACK_YOLO");
    WritebackPermission::clearSessionWritebackGrants();

    CPPUNIT_ASSERT(WritebackPermission::defaultTier() == WritebackTier::Ask);
    CPPUNIT_ASSERT(!WritebackPermission::yoloEnabled());

    WritebackGateRequest req;
    req.surface = u"writer"_ustr;
    auto r = WritebackPermission::evaluate(req);
    CPPUNIT_ASSERT(!r.allowed);
    CPPUNIT_ASSERT(r.needsUi);
    CPPUNIT_ASSERT_EQUAL(u"human-approval-required"_ustr, r.errorCode);
    CPPUNIT_ASSERT(r.reasonZh.indexOf(u"Ask") >= 0 || r.reasonZh.indexOf(u"确认") >= 0);
}

void ControlPlaneTest::testWritebackExplicitHumanApproval()
{
    using kqoffice::ai::control::WritebackGateRequest;
    using kqoffice::ai::control::WritebackPermission;
    using kqoffice::ai::control::WritebackTier;

    ::unsetenv("KQOFFICE_AI_WRITEBACK_YOLO");
    WritebackPermission::clearSessionWritebackGrants();

    WritebackGateRequest req;
    req.explicitHumanApproval = true;
    req.surface = u"calc"_ustr;
    auto r = WritebackPermission::evaluate(req);
    CPPUNIT_ASSERT(r.allowed);
    CPPUNIT_ASSERT(!r.needsUi);
    CPPUNIT_ASSERT(r.effectiveTier == WritebackTier::AllowOnce);
    CPPUNIT_ASSERT_EQUAL(u"writeback-human-approval"_ustr, r.errorCode);
}

void ControlPlaneTest::testWritebackSessionAllow()
{
    using kqoffice::ai::control::PermissionDecision;
    using kqoffice::ai::control::WritebackGateRequest;
    using kqoffice::ai::control::WritebackPermission;
    using kqoffice::ai::control::WritebackScope;
    using kqoffice::ai::control::WritebackTier;

    ::unsetenv("KQOFFICE_AI_WRITEBACK_YOLO");
    WritebackPermission::clearSessionWritebackGrants();

    WritebackGateRequest req;
    req.scope = WritebackScope::Selection;
    req.surface = u"writer"_ustr;
    req.docKey = u"doc-abc"_ustr;

    auto denied = WritebackPermission::evaluate(req);
    CPPUNIT_ASSERT(!denied.allowed);

    auto once = WritebackPermission::resolve(req, PermissionDecision::AllowOnce);
    CPPUNIT_ASSERT(once.allowed);
    CPPUNIT_ASSERT(once.effectiveTier == WritebackTier::AllowOnce);
    // AllowOnce must not stick
    auto stillNeeds = WritebackPermission::evaluate(req);
    CPPUNIT_ASSERT(!stillNeeds.allowed);

    auto session = WritebackPermission::resolve(req, PermissionDecision::AllowSession);
    CPPUNIT_ASSERT(session.allowed);
    CPPUNIT_ASSERT(session.effectiveTier == WritebackTier::AllowSession);

    WritebackGateRequest again = req;
    again.explicitHumanApproval = false;
    auto cached = WritebackPermission::evaluate(again);
    CPPUNIT_ASSERT(cached.allowed);
    CPPUNIT_ASSERT(cached.fromSessionCache);

    WritebackPermission::clearSessionWritebackGrants();
    auto cleared = WritebackPermission::evaluate(req);
    CPPUNIT_ASSERT(!cleared.allowed);
}

void ControlPlaneTest::testWritebackResolveDenyOnceSession()
{
    using kqoffice::ai::control::PermissionDecision;
    using kqoffice::ai::control::WritebackGateRequest;
    using kqoffice::ai::control::WritebackPermission;

    WritebackPermission::clearSessionWritebackGrants();

    WritebackGateRequest req;
    req.surface = u"impress"_ustr;
    auto denied = WritebackPermission::resolve(req, PermissionDecision::Deny);
    CPPUNIT_ASSERT(!denied.allowed);
    CPPUNIT_ASSERT_EQUAL(u"writeback-denied"_ustr, denied.errorCode);
    CPPUNIT_ASSERT(denied.reasonZh.indexOf(u"拒绝") >= 0);
}

void ControlPlaneTest::testWritebackActionIdStable()
{
    using kqoffice::ai::control::WritebackPermission;
    using kqoffice::ai::control::WritebackScope;

    CPPUNIT_ASSERT_EQUAL(u"writeback.selection@writer"_ustr,
                         WritebackPermission::actionId(WritebackScope::Selection, u"writer"_ustr));
    CPPUNIT_ASSERT_EQUAL(u"writeback.document@calc"_ustr,
                         WritebackPermission::actionId(WritebackScope::Document, u"scalc"_ustr));
    CPPUNIT_ASSERT_EQUAL(
        u"writeback.selection@writer#doc-1"_ustr,
        WritebackPermission::actionId(WritebackScope::Selection, u"Writer"_ustr, u"doc-1"_ustr));
}

void ControlPlaneTest::testWritebackScopeFromPlanTargets()
{
    using kqoffice::ai::control::WritebackPermission;
    using kqoffice::ai::control::WritebackScope;

    CPPUNIT_ASSERT(WritebackPermission::scopeFromPlanTargets(true, false)
                   == WritebackScope::Selection);
    CPPUNIT_ASSERT(WritebackPermission::scopeFromPlanTargets(false, false)
                   == WritebackScope::Document);
    CPPUNIT_ASSERT(WritebackPermission::scopeFromPlanTargets(false, true)
                   == WritebackScope::Workspace);
}

// ---- ErrorClassifier + DiagnosticBundle ---------------------------------

void ControlPlaneTest::testErrorClassifierDecks()
{
    using kqoffice::ai::control::ErrorClassifier;
    using kqoffice::ai::control::ErrorDeck;

    auto auth = ErrorClassifier::classify(u"401 unauthorized api key invalid"_ustr);
    CPPUNIT_ASSERT(auth.deck == ErrorDeck::Auth);

    auto net = ErrorClassifier::classify(u"connection refused timeout DNS"_ustr);
    CPPUNIT_ASSERT(net.deck == ErrorDeck::Network);

    auto perm = ErrorClassifier::classify(u"x"_ustr, u"human-approval-required"_ustr);
    CPPUNIT_ASSERT(perm.deck == ErrorDeck::Permission);
    CPPUNIT_ASSERT_EQUAL(u"human-approval-required"_ustr, perm.code);

    auto apply = ErrorClassifier::classify(u"stale-document-snapshot apply-blocked"_ustr);
    CPPUNIT_ASSERT(apply.deck == ErrorDeck::Apply);

    auto crash = ErrorClassifier::classify(u"SIGABRT lockfile multi-instance"_ustr);
    CPPUNIT_ASSERT(crash.deck == ErrorDeck::Crash);
}

void ControlPlaneTest::testErrorClassifierRedact()
{
    using kqoffice::ai::control::ErrorClassifier;

    const OUString raw = u"Bearer sk-abc1234567890secret and token=supersecrettokenvalue"_ustr;
    const OUString red = ErrorClassifier::redact(raw);
    CPPUNIT_ASSERT(red.indexOf(u"sk-abc1234567890secret") < 0);
    CPPUNIT_ASSERT(red.indexOf(u"[REDACTED]") >= 0);
    CPPUNIT_ASSERT(red.indexOf(u"supersecrettokenvalue") < 0);
}

void ControlPlaneTest::testDiagnosticBundleExport()
{
    using kqoffice::ai::control::DiagnosticBundle;
    using kqoffice::ai::control::DiagnosticBundleRequest;
    using kqoffice::ai::control::DiagnosticErrorSample;

    char templ[] = "/tmp/kqoffice-diag-test-XXXXXX";
    char* dir = ::mkdtemp(templ);
    CPPUNIT_ASSERT(dir != nullptr);
    const OUString outDir = OUString::createFromAscii(dir);

    DiagnosticBundleRequest req;
    req.outputDir = outDir;
    req.productVersion = u"26.8.0-test"_ustr;
    req.buildId = u"unit"_ustr;
    req.platformHint = u"macos-arm64"_ustr;
    DiagnosticErrorSample sample;
    sample.message = u"human-approval-required Bearer sk-should-not-leak-12345678"_ustr;
    sample.errorCodeHint = u"human-approval-required"_ustr;
    sample.atMs = 1;
    req.recentErrors.push_back(sample);
    req.notes.push_back(u"note with api_key=leakedsecretvalue"_ustr);

    auto result = DiagnosticBundle::exportBundle(req);
    CPPUNIT_ASSERT(result.success);
    CPPUNIT_ASSERT(result.fileCount >= 3);
    CPPUNIT_ASSERT(!result.manifestPath.isEmpty());
    CPPUNIT_ASSERT(result.summaryZh.indexOf(u"脱敏") >= 0);

    // Read errors.jsonl and ensure secret redacted
    OUString url;
    CPPUNIT_ASSERT(osl::FileBase::getFileURLFromSystemPath(
                       kqoffice::ai::kqofficePathJoin(outDir, u"errors.jsonl"_ustr), url)
                   == osl::FileBase::E_None);
    osl::File f(url);
    CPPUNIT_ASSERT(f.open(osl_File_OpenFlag_Read) == osl::FileBase::E_None);
    sal_uInt64 size = 0;
    f.getSize(size);
    std::vector<char> buf(static_cast<size_t>(size) + 1, 0);
    sal_uInt64 read = 0;
    f.read(buf.data(), size, read);
    f.close();
    const OUString content = OUString::createFromAscii(buf.data());
    CPPUNIT_ASSERT(content.indexOf(u"sk-should-not-leak") < 0);
    CPPUNIT_ASSERT(content.indexOf(u"permission") >= 0 || content.indexOf(u"human-approval") >= 0);

    // cleanup
    ::system((std::string("rm -rf ") + dir).c_str());
}

// ---- ComposerQueue / WorkbenchPhase / ContextUsage / FirstRun ------------

void ControlPlaneTest::testComposerQueueFifo()
{
    using kqoffice::ai::control::ComposerQueue;

    ComposerQueue q(3);
    CPPUNIT_ASSERT(q.empty());
    CPPUNIT_ASSERT(q.enqueue(u"one"_ustr));
    CPPUNIT_ASSERT(q.enqueue(u"two"_ustr));
    CPPUNIT_ASSERT(q.enqueue(u"three"_ustr));
    bool overflow = false;
    CPPUNIT_ASSERT(q.enqueue(u"four"_ustr, false, &overflow));
    CPPUNIT_ASSERT(overflow);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(3), q.size());
    auto a = q.dequeue();
    CPPUNIT_ASSERT(a.has_value());
    CPPUNIT_ASSERT_EQUAL(u"two"_ustr, a->prompt); // oldest "one" dropped
    auto b = q.dequeue();
    CPPUNIT_ASSERT_EQUAL(u"three"_ustr, b->prompt);

    q.clear();
    CPPUNIT_ASSERT(q.enqueue(u"keep"_ustr));
    CPPUNIT_ASSERT(q.enqueue(u"replace-me"_ustr, /*replaceCurrent*/ true));
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(1), q.size());
    CPPUNIT_ASSERT_EQUAL(u"replace-me"_ustr, q.peek()->prompt);
    CPPUNIT_ASSERT(q.statusLineZh().indexOf(u"排队") >= 0);
}

void ControlPlaneTest::testWorkbenchPhaseReadyAndSteps()
{
    using kqoffice::ai::control::WorkbenchPhase;
    using kqoffice::ai::control::WorkbenchPhaseMachine;

    CPPUNIT_ASSERT(WorkbenchPhaseMachine::isReadyForPrimaryInput(WorkbenchPhase::Idle, false, false));
    CPPUNIT_ASSERT(!WorkbenchPhaseMachine::isReadyForPrimaryInput(WorkbenchPhase::Generating, true, false));
    CPPUNIT_ASSERT(!WorkbenchPhaseMachine::isReadyForPrimaryInput(WorkbenchPhase::ToolsOpen, false, true));
    CPPUNIT_ASSERT(WorkbenchPhaseMachine::isTurnBusy(WorkbenchPhase::Generating, false, false));
    CPPUNIT_ASSERT(WorkbenchPhaseMachine::canTransition(WorkbenchPhase::Idle, WorkbenchPhase::Planning));
    CPPUNIT_ASSERT(WorkbenchPhaseMachine::canTransition(WorkbenchPhase::Planning, WorkbenchPhase::Generating));
    CPPUNIT_ASSERT(WorkbenchPhaseMachine::canTransition(WorkbenchPhase::Generating, WorkbenchPhase::AwaitingApply));

    const auto steps = WorkbenchPhaseMachine::parseStepsFromApproach(
        u"1. 读上下文\n2. 生成草案\n3. Diff 批准\n"_ustr);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(3), steps.size());
    CPPUNIT_ASSERT(steps[0].titleZh.indexOf(u"读上下文") >= 0);

    const auto snap = WorkbenchPhaseMachine::makeSnapshot(
        WorkbenchPhase::Planning, u"wp-1"_ustr, OUString(), steps, 0, false, false);
    CPPUNIT_ASSERT(snap.stepBarZh.indexOf(u"待确认计划") >= 0 || snap.stepBarZh.indexOf(u"步") >= 0);
    CPPUNIT_ASSERT(snap.readyForPrimaryInput);

    const auto busySnap = WorkbenchPhaseMachine::makeSnapshot(
        WorkbenchPhase::Generating, u"wp-1"_ustr, OUString(), steps, 2, true, false);
    CPPUNIT_ASSERT(!busySnap.readyForPrimaryInput);
    CPPUNIT_ASSERT(busySnap.stepBarZh.indexOf(u"排队") >= 0 || busySnap.stepBarZh.indexOf(u"流式") >= 0);
}

void ControlPlaneTest::testContextUsageEstimate()
{
    using kqoffice::ai::control::ContextUsage;
    using kqoffice::ai::control::ContextUsageSample;

    ContextUsageSample s;
    s.userChars = 100;
    s.selectionChars = 400;
    s.documentContextChars = 2000;
    s.historyChars = 1000;
    s.budgetTokens = 1000;
    auto e = ContextUsage::estimate(s);
    CPPUNIT_ASSERT(e.totalChars == 3500);
    CPPUNIT_ASSERT(e.approxTokens > 0);
    CPPUNIT_ASSERT(e.chipZh.indexOf(u"上下文") >= 0);
    CPPUNIT_ASSERT(e.overLimit || e.nearLimit); // 3500*0.6 >> 1000

    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(0), ContextUsage::charsToApproxTokens(0));
    CPPUNIT_ASSERT(ContextUsage::formatK(1500).indexOf(u"k") >= 0);
}

void ControlPlaneTest::testAiFirstRunGateSkipComplete()
{
    using kqoffice::ai::control::AiFirstRunGate;
    using kqoffice::ai::control::FirstRunState;

    char templ[] = "/tmp/kqoffice-firstrun-XXXXXX";
    char* dir = ::mkdtemp(templ);
    CPPUNIT_ASSERT(dir != nullptr);
    AiFirstRunGate::setRootDirForTests(OUString::createFromAscii(dir));
    CPPUNIT_ASSERT(AiFirstRunGate::resetForTests());
    CPPUNIT_ASSERT(AiFirstRunGate::shouldShowWelcome());
    CPPUNIT_ASSERT(AiFirstRunGate::welcomeMarkdownZh().indexOf(u"欢迎") >= 0);

    CPPUNIT_ASSERT(AiFirstRunGate::markSkipped());
    CPPUNIT_ASSERT(!AiFirstRunGate::shouldShowWelcome());
    CPPUNIT_ASSERT(AiFirstRunGate::state() == FirstRunState::Skipped);

    CPPUNIT_ASSERT(AiFirstRunGate::markCompleted());
    CPPUNIT_ASSERT(AiFirstRunGate::state() == FirstRunState::Completed);

    AiFirstRunGate::setRootDirForTests(OUString());
    ::system((std::string("rm -rf ") + dir).c_str());
}

void ControlPlaneTest::testWorkbenchStatusDashboard()
{
    using kqoffice::ai::control::WorkbenchPhase;
    using kqoffice::ai::control::WorkbenchStatus;
    using kqoffice::ai::control::WorkbenchStatusInput;

    WorkbenchStatusInput in;
    in.phase = WorkbenchPhase::Generating;
    in.workPlanId = u"wp-9"_ustr;
    in.applyPlanId = u"ap-1"_ustr;
    in.queueSize = 2;
    in.queueStatusZh = u"排队 2 条"_ustr;
    in.streamOpen = true;
    in.surface = u"writer"_ustr;
    in.contextChipZh = u"上下文约 1.2k/24k"_ustr;
    in.membershipChipZh = u"会员 · 今日剩 3"_ustr;

    auto rep = WorkbenchStatus::build(in);
    CPPUNIT_ASSERT(!rep.readyForPrimaryInput);
    CPPUNIT_ASSERT(rep.markdownZh.indexOf(u"工作台状态") >= 0);
    CPPUNIT_ASSERT(rep.markdownZh.indexOf(u"wp-9") >= 0);
    CPPUNIT_ASSERT(rep.markdownZh.indexOf(u"排队") >= 0 || rep.markdownZh.indexOf(u"2") >= 0);
    CPPUNIT_ASSERT(rep.chipZh.indexOf(u"忙") >= 0 || rep.chipZh.indexOf(u"生成") >= 0);
    CPPUNIT_ASSERT(WorkbenchStatus::staticPoliciesMarkdownZh().indexOf(u"写回") >= 0);
    CPPUNIT_ASSERT(WorkbenchStatus::staticPoliciesMarkdownZh().indexOf(u"资源信封") >= 0
                   || WorkbenchStatus::staticPoliciesMarkdownZh().indexOf(u"资源") >= 0);
}

void ControlPlaneTest::testFactRouterShapes()
{
    using kqoffice::ai::control::FactRouteInput;
    using kqoffice::ai::control::FactRouter;
    using kqoffice::ai::control::RouteShape;

    {
        FactRouteInput in;
        in.prompt = u"解释一下这段话是什么意思"_ustr;
        in.knownPureQa = true;
        auto d = FactRouter::route(in);
        CPPUNIT_ASSERT(d.shape == RouteShape::Direct);
        CPPUNIT_ASSERT(!d.needsWorkPlan);
    }
    {
        FactRouteInput in;
        in.prompt = u"请把整篇文章重构并系统整理层级"_ustr;
        in.surface = u"writer"_ustr;
        auto d = FactRouter::route(in);
        CPPUNIT_ASSERT(d.shape == RouteShape::PlanGate);
        CPPUNIT_ASSERT(d.needsWorkPlan);
        CPPUNIT_ASSERT_EQUAL(u"full-doc"_ustr, d.reasonCode);
    }
    {
        FactRouteInput in;
        in.prompt = u"先规划再改"_ustr;
        in.forcePlanOnce = true;
        auto d = FactRouter::route(in);
        CPPUNIT_ASSERT(d.needsWorkPlan);
    }
    {
        FactRouteInput in;
        in.prompt = u"润色一下"_ustr;
        in.hasSelection = true;
        in.selectionChars = 40;
        auto d = FactRouter::route(in);
        CPPUNIT_ASSERT(d.shape == RouteShape::Direct || d.shape == RouteShape::BoundedLoop);
        CPPUNIT_ASSERT(!d.needsWorkPlan || d.preferMultiRoundTools || d.shape == RouteShape::Direct);
    }
    {
        FactRouteInput in;
        in.prompt = u"请改写并润色选中内容，使语气更正式一些"_ustr;
        in.hasSelection = true;
        in.selectionChars = 100;
        auto d = FactRouter::route(in);
        // compound or selection-edit
        CPPUNIT_ASSERT(d.shape == RouteShape::PlanGate || d.shape == RouteShape::BoundedLoop
                       || d.shape == RouteShape::Direct);
    }
}

void ControlPlaneTest::testProviderSlotManifest()
{
    using kqoffice::ai::control::ProviderSlotManifest;
    using kqoffice::ai::control::ProviderSlotRegistry;

    CPPUNIT_ASSERT(ProviderSlotRegistry::isValidId(u"ollama-local"_ustr));
    CPPUNIT_ASSERT(!ProviderSlotRegistry::isValidId(u"../evil"_ustr));
    CPPUNIT_ASSERT(!ProviderSlotRegistry::isValidId(u""_ustr));

    ProviderSlotManifest m;
    const OUString json
        = u"{\"id\":\"my-gw\",\"displayNameZh\":\"测\",\"backend\":\"openai-compatible\","
          "\"baseUrl\":\"http://127.0.0.1:9\",\"aliases\":[\"mine\"],\"enabled\":true}"_ustr;
    CPPUNIT_ASSERT(ProviderSlotRegistry::parseOne(json, m));
    CPPUNIT_ASSERT_EQUAL(u"my-gw"_ustr, m.id);
    CPPUNIT_ASSERT_EQUAL(u"openai-compatible"_ustr, m.backend);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), m.aliases.size());

    auto slots = ProviderSlotRegistry::builtinDefaults();
    CPPUNIT_ASSERT(slots.size() >= 3);
    CPPUNIT_ASSERT(ProviderSlotRegistry::find(slots, u"ollama"_ustr) != nullptr);
    CPPUNIT_ASSERT(ProviderSlotRegistry::find(slots, u"membership"_ustr) != nullptr);
    CPPUNIT_ASSERT(ProviderSlotRegistry::summaryLineZh(slots).indexOf(u"Provider") >= 0
                   || ProviderSlotRegistry::summaryLineZh(slots).indexOf(u"Slot") >= 0);
}

void ControlPlaneTest::testPolicyEvolutionGuard()
{
    using kqoffice::ai::control::PolicyEvolutionGuard;

    auto ok = PolicyEvolutionGuard::validateFieldName(u"router_direct_max_chars"_ustr);
    CPPUNIT_ASSERT(ok.allowed);

    auto yolo = PolicyEvolutionGuard::validateFieldName(u"writeback_yolo_default"_ustr);
    CPPUNIT_ASSERT(!yolo.allowed);

    auto token = PolicyEvolutionGuard::validateFieldName(u"api_token_store"_ustr);
    CPPUNIT_ASSERT(!token.allowed);

    auto unknown = PolicyEvolutionGuard::validateFieldName(u"fancy_new_knob"_ustr);
    CPPUNIT_ASSERT(!unknown.allowed);

    std::vector<OUString> fields = { u"composer_queue_max"_ustr, u"resource_fts_top_k"_ustr };
    CPPUNIT_ASSERT(PolicyEvolutionGuard::validateCandidateFields(fields).allowed);

    std::vector<OUString> bad = { u"composer_queue_max"_ustr, u"sandbox_profile"_ustr };
    CPPUNIT_ASSERT(!PolicyEvolutionGuard::validateCandidateFields(bad).allowed);

    CPPUNIT_ASSERT(PolicyEvolutionGuard::ceilingMayOnlyFall(10, 8));
    CPPUNIT_ASSERT(!PolicyEvolutionGuard::ceilingMayOnlyFall(10, 12));
    CPPUNIT_ASSERT(PolicyEvolutionGuard::policyHintZh().indexOf(u"晋升") >= 0
                   || PolicyEvolutionGuard::policyHintZh().indexOf(u"人工") >= 0);
}

void ControlPlaneTest::testEventLedgerPersistBeforeBroadcast()
{
    using kqoffice::ai::control::EventLedger;
    using kqoffice::ai::control::LedgerEvent;

    char templ[] = "/tmp/kqoffice-ledger-XXXXXX";
    char* dir = ::mkdtemp(templ);
    CPPUNIT_ASSERT(dir != nullptr);
    ::setenv("KQOFFICE_AI_LEDGER_DIR", dir, 1);
    EventLedger::clearSubscribersForTests();

    sal_Int32 broadcasts = 0;
    EventLedger::subscribe([&](const LedgerEvent& ev) {
        ++broadcasts;
        CPPUNIT_ASSERT(ev.sequence > 0);
        CPPUNIT_ASSERT(!ev.type.isEmpty());
    });

    EventLedger ledger(OUString::createFromAscii(dir));
    const sal_Int64 s1 = ledger.append(u"route"_ustr, u"direct short-selection"_ustr, u"run-a"_ustr);
    CPPUNIT_ASSERT(s1 >= 1);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(1), broadcasts);

    const sal_Int64 s2 = ledger.append(u"apply"_ustr, u"ok plan=ap-1"_ustr, u"ap-1"_ustr);
    CPPUNIT_ASSERT(s2 > s1);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(2), broadcasts);

    auto replay = ledger.replaySince(0, 50);
    CPPUNIT_ASSERT(replay.size() >= 2);
    CPPUNIT_ASSERT_EQUAL(s1, replay[0].sequence);

    auto after = ledger.replaySince(s1, 50);
    CPPUNIT_ASSERT(!after.empty());
    CPPUNIT_ASSERT(after.front().sequence > s1);

    // Secret-ish payload redacted on store path via ErrorClassifier
    ledger.append(u"system"_ustr, u"Bearer sk-abcdefghijklmnopqrst"_ustr, OUString());
    auto last = ledger.replaySince(s2, 10);
    bool found = false;
    for (const auto& e : last)
    {
        if (e.type == u"system"_ustr)
        {
            found = true;
            CPPUNIT_ASSERT(e.payload.indexOf(u"sk-abcdefghijklmnopqrst") < 0);
        }
    }
    CPPUNIT_ASSERT(found);

    EventLedger::clearSubscribersForTests();
    ::unsetenv("KQOFFICE_AI_LEDGER_DIR");
    ::system((std::string("rm -rf ") + dir).c_str());
}

void ControlPlaneTest::testExternalWriteDenier()
{
    using kqoffice::ai::control::ExternalWriteDenier;

    CPPUNIT_ASSERT(!ExternalWriteDenier::evaluateCommandLine(u"git push origin main"_ustr).allowed);
    CPPUNIT_ASSERT(
        !ExternalWriteDenier::evaluateCommandLine(u"git push --force origin main"_ustr).allowed);
    CPPUNIT_ASSERT(ExternalWriteDenier::evaluateCommandLine(u"git status"_ustr).allowed);
    CPPUNIT_ASSERT(ExternalWriteDenier::evaluateCommandLine(u"git commit -m x"_ustr).allowed);
    CPPUNIT_ASSERT(ExternalWriteDenier::evaluateCommandLine(u"git clone https://x"_ustr).allowed);
    CPPUNIT_ASSERT(!ExternalWriteDenier::evaluateCommandLine(u"npm publish"_ustr).allowed);
    CPPUNIT_ASSERT(!ExternalWriteDenier::evaluateCommandLine(u"docker push img"_ustr).allowed);
    CPPUNIT_ASSERT(ExternalWriteDenier::evaluateCommandLine(u"curl https://example.com"_ustr).allowed);
    CPPUNIT_ASSERT(
        !ExternalWriteDenier::evaluateCommandLine(u"curl -X POST https://example.com"_ustr).allowed);

    CPPUNIT_ASSERT(!ExternalWriteDenier::evaluateAction(u"push_branch"_ustr).allowed);
    CPPUNIT_ASSERT(!ExternalWriteDenier::evaluateAction(u"publish"_ustr).allowed);
    CPPUNIT_ASSERT(ExternalWriteDenier::evaluateAction(u"apply_approved"_ustr).allowed);

    std::vector<OUString> args = { u"status"_ustr };
    CPPUNIT_ASSERT(ExternalWriteDenier::evaluate(u"/usr/bin/git"_ustr, args).allowed);
    args = { u"push"_ustr, u"origin"_ustr };
    CPPUNIT_ASSERT(!ExternalWriteDenier::evaluate(u"/usr/bin/git"_ustr, args).allowed);
}

void ControlPlaneTest::testStallDetectorPositiveAndNegative()
{
    using kqoffice::ai::control::DetectorEvent;
    using kqoffice::ai::control::DetectorSignalKind;
    using kqoffice::ai::control::StallDetector;
    using kqoffice::ai::control::StallVerdict;

    // Negative: TDD red-green — different fail details then progress
    {
        std::vector<DetectorEvent> h;
        DetectorEvent f1;
        f1.kind = DetectorSignalKind::ToolFail;
        f1.action = u"test"_ustr;
        f1.detail = u"assert A failed"_ustr;
        f1.timeUnit = 1;
        h.push_back(f1);
        DetectorEvent prog;
        prog.kind = DetectorSignalKind::Progress;
        prog.action = u"edit"_ustr;
        prog.timeUnit = 2;
        h.push_back(prog);
        DetectorEvent f2;
        f2.kind = DetectorSignalKind::ToolFail;
        f2.action = u"test"_ustr;
        f2.detail = u"assert B failed"_ustr; // different detail
        f2.timeUnit = 3;
        h.push_back(f2);
        auto d = StallDetector::evaluate(h);
        CPPUNIT_ASSERT(d.verdict == StallVerdict::None
                       || d.verdict != StallVerdict::LoopSuspect);
    }

    // Negative: research read burst with progress at end should not ReadNoWrite
    {
        std::vector<DetectorEvent> h;
        for (sal_Int32 i = 0; i < 15; ++i)
        {
            DetectorEvent r;
            r.kind = DetectorSignalKind::ReadOnly;
            r.action = u"read"_ustr;
            r.timeUnit = i;
            h.push_back(r);
        }
        DetectorEvent p;
        p.kind = DetectorSignalKind::Progress;
        p.action = u"token"_ustr;
        p.timeUnit = 20;
        h.push_back(p);
        auto d = StallDetector::evaluate(h);
        CPPUNIT_ASSERT(d.verdict != StallVerdict::ReadNoWrite);
    }

    // Positive: identical fails
    {
        std::vector<DetectorEvent> h;
        for (int i = 0; i < 2; ++i)
        {
            DetectorEvent f;
            f.kind = DetectorSignalKind::ToolFail;
            f.action = u"compile"_ustr;
            f.detail = u"error 42"_ustr;
            f.timeUnit = i;
            h.push_back(f);
        }
        auto d = StallDetector::evaluate(h);
        CPPUNIT_ASSERT(d.verdict == StallVerdict::LoopSuspect);
        CPPUNIT_ASSERT(d.shouldNudge);
    }

    // Positive: soft stall idle
    {
        std::vector<DetectorEvent> h;
        for (sal_Int32 i = 0; i < 5; ++i)
        {
            DetectorEvent idle;
            idle.kind = DetectorSignalKind::IdleTick;
            idle.timeUnit = i;
            h.push_back(idle);
        }
        auto d = StallDetector::evaluate(h);
        CPPUNIT_ASSERT(d.verdict == StallVerdict::SoftStall);
    }

    // Fingerprint collapses digits
    CPPUNIT_ASSERT_EQUAL(StallDetector::normalizeFingerprint(u"t1"_ustr, u"e99"_ustr),
                         StallDetector::normalizeFingerprint(u"t2"_ustr, u"e00"_ustr));
}

void ControlPlaneTest::testSandboxCageCanaries()
{
    using kqoffice::ai::control::CanaryResult;
    using kqoffice::ai::control::SandboxCage;

    auto denier = SandboxCage::runOne(u"denier-self-test"_ustr);
    CPPUNIT_ASSERT(denier.result == CanaryResult::Pass);

    auto ask = SandboxCage::runOne(u"writeback-default-ask"_ustr);
    CPPUNIT_ASSERT(ask.result == CanaryResult::Pass);

    auto cfg = SandboxCage::runOne(u"config-writable"_ustr);
    CPPUNIT_ASSERT(cfg.result == CanaryResult::Pass || cfg.result == CanaryResult::Fail);

    auto rep = SandboxCage::runCanaries();
    CPPUNIT_ASSERT(rep.checks.size() >= 5);
    CPPUNIT_ASSERT(!rep.summaryZh.isEmpty());
    // Product default: should allow AI run on a normal developer machine
    // (home full-auth Fail is rare). If fails, bypass must still document.
    if (!rep.mayStartAiRun)
        CPPUNIT_ASSERT(!rep.failedCanaryId.isEmpty());
}

void ControlPlaneTest::testHandoffBriefFromEvents()
{
    using kqoffice::ai::control::HandoffBriefBuilder;
    using kqoffice::ai::control::LedgerEvent;

    std::vector<LedgerEvent> evs;
    LedgerEvent e1;
    e1.sequence = 1;
    e1.type = u"route"_ustr;
    e1.payload = u"direct short"_ustr;
    evs.push_back(e1);
    LedgerEvent e2;
    e2.sequence = 2;
    e2.type = u"apply"_ustr;
    e2.payload = u"ok plan=ap-1"_ustr;
    evs.push_back(e2);

    auto b = HandoffBriefBuilder::fromEvents(evs, u"测试交接"_ustr);
    CPPUNIT_ASSERT(b.fromLedger);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(2), b.eventCount);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int64>(2), b.lastSequence);
    CPPUNIT_ASSERT(b.markdownZh.indexOf(u"测试交接") >= 0);
    CPPUNIT_ASSERT(b.markdownZh.indexOf(u"route") >= 0);
    CPPUNIT_ASSERT(b.markdownZh.indexOf(u"硬约束") >= 0);

    auto empty = HandoffBriefBuilder::fromEvents({});
    CPPUNIT_ASSERT(empty.markdownZh.indexOf(u"暂无") >= 0);
}

CPPUNIT_TEST_SUITE_REGISTRATION(ControlPlaneTest);
} // namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
