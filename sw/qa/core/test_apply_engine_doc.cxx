/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * V2 W3 Day-5 — SwDoc-backed apply engine smoke (single paragraph-replace).
 * Uses BootstrapFixture + empty embedded Writer doc; no UNO loadFromURL.
 */

#include <test/bootstrapfixture.hxx>

#include <cppunit/TestAssert.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/plugin/TestPlugIn.h>

#include <IntelligentWriterApplyEngine.hxx>
#include <doc.hxx>
#include <docsh.hxx>
#include <ndtxt.hxx>
#include <swdll.hxx>

#include <IDocumentContentOperations.hxx>

typedef rtl::Reference<SwDocShell> SwDocShellRef;

namespace
{
class ApplyEngineDocTest : public test::BootstrapFixture
{
    SwDoc* m_pDoc = nullptr;
    SwDocShellRef m_xDocShRef;

public:
    void setUp() override;
    void tearDown() override;

    void test_applyEngine_singleParagraphReplace_onFreshDoc();
    void test_tryParseStubRuntimeJson_thenApply_onFreshDoc();

    CPPUNIT_TEST_SUITE(ApplyEngineDocTest);
    CPPUNIT_TEST(test_applyEngine_singleParagraphReplace_onFreshDoc);
    CPPUNIT_TEST(test_tryParseStubRuntimeJson_thenApply_onFreshDoc);
    CPPUNIT_TEST_SUITE_END();
};

void ApplyEngineDocTest::setUp()
{
    BootstrapFixture::setUp();
    SwGlobals::ensure();
    m_pDoc = new SwDoc;
    m_xDocShRef = new SwDocShell(*m_pDoc, SfxObjectCreateMode::EMBEDDED);
    m_xDocShRef->DoInitNew();
}

void ApplyEngineDocTest::tearDown()
{
    m_pDoc = nullptr;
    m_xDocShRef->DoClose();
    m_xDocShRef.clear();
    BootstrapFixture::tearDown();
}

void ApplyEngineDocTest::test_applyEngine_singleParagraphReplace_onFreshDoc()
{
    using namespace sw::intelligent;

    SwNodeIndex aIdx(m_pDoc->GetNodes().GetEndOfContent(), -1);
    SwPaM aPaM(aIdx);
    const OUString aBefore(u"before apply e2e"_ustr);
    m_pDoc->getIDocumentContentOperations().InsertString(aPaM, aBefore);

    std::optional<ApplyPlan> oPlan = BuildSingleParagraphReplacePlan(
        *m_xDocShRef, u"ap-doc-e2e-001"_ustr, u"swpara-1"_ustr, u"after apply e2e"_ustr,
        u"diag-doc-e2e-001"_ustr);
    CPPUNIT_ASSERT_MESSAGE("BuildSingleParagraphReplacePlan failed", oPlan.has_value());

    ApplyResult aResult = ApplyEngine(*m_xDocShRef).run(*oPlan);
    CPPUNIT_ASSERT_EQUAL(ApplyStatus::Ok, aResult.meStatus);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), aResult.mnAppliedCount);

    std::optional<OUString> oText = GetBodyParagraphText(*m_xDocShRef, u"swpara-1"_ustr);
    CPPUNIT_ASSERT(oText.has_value());
    CPPUNIT_ASSERT_EQUAL(u"after apply e2e"_ustr, *oText);
}

void ApplyEngineDocTest::test_tryParseStubRuntimeJson_thenApply_onFreshDoc()
{
    using namespace sw::intelligent;

    SwNodeIndex aIdx(m_pDoc->GetNodes().GetEndOfContent(), -1);
    SwPaM aPaM(aIdx);
    const OUString aBefore(u"before stub runtime json"_ustr);
    m_pDoc->getIDocumentContentOperations().InsertString(aPaM, aBefore);

    const OUString aJson = uR"({
  "schema_version": "v2-w3-runtime-1",
  "plan_id": "ap-stub-runtime-001",
  "source_diagnostic_id": "diag-stub-runtime-001",
  "doc_snapshot_hash": "sha256:0000000000000000000000000000000000000000000000000000000000000000",
  "preview_only": false,
  "patches": [
    {
      "patch_id": "p1",
      "kind": "paragraph-replace",
      "target": {"paragraph_id": "swpara-1"},
      "severity": "minor",
      "rationale": "offline stub runtime plan",
      "after": "after stub runtime json"
    }
  ]
})"_ustr;

    std::optional<ApplyPlan> oPlan = TryParseApplyPlanRuntimeJson(aJson, *m_xDocShRef);
    CPPUNIT_ASSERT_MESSAGE("TryParseApplyPlanRuntimeJson stub envelope failed", oPlan.has_value());
    CPPUNIT_ASSERT(!oPlan->maDocSnapshotHash.isEmpty());
    CPPUNIT_ASSERT(oPlan->maDocSnapshotHash != u"sha256:0000000000000000000000000000000000000000000000000000000000000000"_ustr);

    ApplyResult aResult = ApplyEngine(*m_xDocShRef).run(*oPlan);
    CPPUNIT_ASSERT_EQUAL(ApplyStatus::Ok, aResult.meStatus);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), aResult.mnAppliedCount);

    std::optional<OUString> oText = GetBodyParagraphText(*m_xDocShRef, u"swpara-1"_ustr);
    CPPUNIT_ASSERT(oText.has_value());
    CPPUNIT_ASSERT_EQUAL(u"after stub runtime json"_ustr, *oText);
}

CPPUNIT_TEST_SUITE_REGISTRATION(ApplyEngineDocTest);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */