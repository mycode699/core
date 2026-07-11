/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W1: Provider Runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Day-0 unit tests — covers the three behaviors that must hold even before
 * any backend (Ollama / private / cloud) is wired:
 *   1. ServiceModePolicy default mode is "offline".
 *   2. Offline mode allows the four Day-0 capabilities and denies everything else.
 *   3. Provider.call() rejects empty capability via IllegalArgumentException.
 *   4. Provider.call() returns status="provider-error" for allowed capability
 *      (no backend yet) and status="policy-denied" for disallowed capability.
 */

#include <sal/types.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/plugin/TestPlugIn.h>

#include <com/sun/star/lang/IllegalArgumentException.hpp>
#include <rtl/ref.hxx>
#include <rtl/ustring.hxx>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include "ApplyPlanValidator.hxx"
#include "EvidenceRecorder.hxx"
#include "OllamaAdapter.hxx"
#include "Provider.hxx"
#include "ServiceModePolicy.hxx"

namespace
{
// Day-1 cppunit needs each test to land evidence in an isolated
// directory so testing on a developer machine never collides with a
// previous run. RAII helper sets KQOFFICE_AI_EVIDENCE_DIR for the
// duration of one test.
class ScopedEvidenceDir
{
public:
    ScopedEvidenceDir()
    {
        char templ[] = "/tmp/kqoffice-ev-XXXXXX";
        const char* dir = ::mkdtemp(templ);
        if (dir)
        {
            m_dir = OString(dir);
            ::setenv("KQOFFICE_AI_EVIDENCE_DIR", dir, 1);
        }
        // Day-1 wire: pin probe() off for tests that hardcode
        // "provider-error" assertions, so a developer machine running
        // Ollama on :11434 doesn't flip status to "ok" and break them.
        // Probe-specific tests (testProbe*) don't use ScopedEvidenceDir,
        // so they still exercise the real socket path.
        ::setenv("KQOFFICE_AI_DISABLE_PROBE", "1", 1);
    }
    ~ScopedEvidenceDir()
    {
        ::unsetenv("KQOFFICE_AI_EVIDENCE_DIR");
        ::unsetenv("KQOFFICE_AI_DISABLE_PROBE");
    }

    const OString& path() const { return m_dir; }

private:
    OString m_dir;
};
} // namespace

namespace
{
// Day-0 contract test — pure C++ logic, no UNO bootstrap. Avoids
// BootstrapFixture so the binary does not need a working services.rdb,
// matching the W2 FuzzyMatcher fast-test approach.
class ProviderTest : public CppUnit::TestFixture
{
public:
    void testDefaultModeIsOffline();
    void testOfflineAllowsRewrite();
    void testOfflineDeniesUnknownCapability();
    void testEmptyCapabilityThrows();
    void testAllowedCapabilityReturnsStubError();
    void testDeniedCapabilityReturnsPolicyDenied();
    void testServiceModeAccessor();
    void testEvidenceIdMintedOnProviderError();
    void testEvidenceFileWrittenWithCapability();
    void testEvidenceIdsAreUniqueAcrossCalls();
    void testParseModelsJsonFixture();
    void testBuildGenerateRequestJsonUsesJsonMode();
    void testParseGenerateJsonFixture();
    void testParseGenerateJsonHandlesEscapes();
    void testProbeUnreachableOnClosedPort();
    void testProbeDoesNotHang();
    void testApplyPlanValidFixturePasses();
    void testApplyPlanInvalidFixtureFlagsFailureBehavior();
    void testApplyPlanRejectsWrongSchemaVersion();
    void testApplyPlanRejectsBadIdPattern();
    void testApplyPlanRequiresUndoGroupLabel();
    void testApplyPlanRequiresDeterministicTrue();
    void testApplyPlanRejectsNonObject();
    void testApplyPlanRequiresOperationSummary();
    void testApplyPlanRejectsBadRevisionPrecondition();
    void testApplyPlanRequiresRollbackTrue();
    void testApplyPlanRejectsBadUndoGroupMode();
    void testApplyPlanRequiresFailureMessage();
    void testApplyPlanRequiresRepeatedDiagnosticsTrue();
    void testApplyPlanReportsMissingTopLevelField();
    void testApplyPlanRejectsTooShortId();
    void testApplyPlanIdAcceptsLongestLegal();
    void testApplyPlanIdRejectsTooLong();
    void testApplyPlanIgnoresUnknownTopLevelKeys();
    void testApplyPlanCodeEnumStable();
    void testApplyPlanMessageOkReturnsEmpty();
    void testApplyPlanMessageMentionsFieldPath();
    void testApplyPlanMessageDistinctPerCode();
    void testApplyPlanMessageHandlesEmptyPath();
    void testApvFindTopKeyBasic();
    void testApvFindTopKeyIgnoresNestedSameKey();
    void testApvFindTopKeyIgnoresKeyInStringValue();
    void testApvReadStringDecodesEscapes();
    void testApvReadStringRejectsNonString();
    void testApplyPlanStatusOkString();
    void testApplyPlanStatusDistinctPerCode();
    void testApplyPlanStatusIsAsciiKebab();
    void testApplyPlanAcceptsLongZhCnSummary();
    void testApplyPlanAcceptsEmojiInLabel();
    void testApplyPlanRejectsEmojiInId();
    void testApplyPlanRejectsFullwidthInId();
    void testApplyPlanAcceptsEscapedQuoteInLabel();
    void testListCapabilitiesMatchesPolicy();
    void testDurationMsBoundedWhenProbeDisabled();
    void testStubRuntimeReturnsOkJsonEnvelope();

    CPPUNIT_TEST_SUITE(ProviderTest);
    CPPUNIT_TEST(testDefaultModeIsOffline);
    CPPUNIT_TEST(testOfflineAllowsRewrite);
    CPPUNIT_TEST(testOfflineDeniesUnknownCapability);
    CPPUNIT_TEST(testEmptyCapabilityThrows);
    CPPUNIT_TEST(testAllowedCapabilityReturnsStubError);
    CPPUNIT_TEST(testDeniedCapabilityReturnsPolicyDenied);
    CPPUNIT_TEST(testServiceModeAccessor);
    CPPUNIT_TEST(testEvidenceIdMintedOnProviderError);
    CPPUNIT_TEST(testEvidenceFileWrittenWithCapability);
    CPPUNIT_TEST(testEvidenceIdsAreUniqueAcrossCalls);
    CPPUNIT_TEST(testParseModelsJsonFixture);
    CPPUNIT_TEST(testBuildGenerateRequestJsonUsesJsonMode);
    CPPUNIT_TEST(testParseGenerateJsonFixture);
    CPPUNIT_TEST(testParseGenerateJsonHandlesEscapes);
    CPPUNIT_TEST(testProbeUnreachableOnClosedPort);
    CPPUNIT_TEST(testProbeDoesNotHang);
    CPPUNIT_TEST(testApplyPlanValidFixturePasses);
    CPPUNIT_TEST(testApplyPlanInvalidFixtureFlagsFailureBehavior);
    CPPUNIT_TEST(testApplyPlanRejectsWrongSchemaVersion);
    CPPUNIT_TEST(testApplyPlanRejectsBadIdPattern);
    CPPUNIT_TEST(testApplyPlanRequiresUndoGroupLabel);
    CPPUNIT_TEST(testApplyPlanRequiresDeterministicTrue);
    CPPUNIT_TEST(testApplyPlanRejectsNonObject);
    CPPUNIT_TEST(testApplyPlanRequiresOperationSummary);
    CPPUNIT_TEST(testApplyPlanRejectsBadRevisionPrecondition);
    CPPUNIT_TEST(testApplyPlanRequiresRollbackTrue);
    CPPUNIT_TEST(testApplyPlanRejectsBadUndoGroupMode);
    CPPUNIT_TEST(testApplyPlanRequiresFailureMessage);
    CPPUNIT_TEST(testApplyPlanRequiresRepeatedDiagnosticsTrue);
    CPPUNIT_TEST(testApplyPlanReportsMissingTopLevelField);
    CPPUNIT_TEST(testApplyPlanRejectsTooShortId);
    CPPUNIT_TEST(testApplyPlanIdAcceptsLongestLegal);
    CPPUNIT_TEST(testApplyPlanIdRejectsTooLong);
    CPPUNIT_TEST(testApplyPlanIgnoresUnknownTopLevelKeys);
    CPPUNIT_TEST(testApplyPlanCodeEnumStable);
    CPPUNIT_TEST(testApplyPlanMessageOkReturnsEmpty);
    CPPUNIT_TEST(testApplyPlanMessageMentionsFieldPath);
    CPPUNIT_TEST(testApplyPlanMessageDistinctPerCode);
    CPPUNIT_TEST(testApplyPlanMessageHandlesEmptyPath);
    CPPUNIT_TEST(testApvFindTopKeyBasic);
    CPPUNIT_TEST(testApvFindTopKeyIgnoresNestedSameKey);
    CPPUNIT_TEST(testApvFindTopKeyIgnoresKeyInStringValue);
    CPPUNIT_TEST(testApvReadStringDecodesEscapes);
    CPPUNIT_TEST(testApvReadStringRejectsNonString);
    CPPUNIT_TEST(testApplyPlanStatusOkString);
    CPPUNIT_TEST(testApplyPlanStatusDistinctPerCode);
    CPPUNIT_TEST(testApplyPlanStatusIsAsciiKebab);
    CPPUNIT_TEST(testApplyPlanAcceptsLongZhCnSummary);
    CPPUNIT_TEST(testApplyPlanAcceptsEmojiInLabel);
    CPPUNIT_TEST(testApplyPlanRejectsEmojiInId);
    CPPUNIT_TEST(testApplyPlanRejectsFullwidthInId);
    CPPUNIT_TEST(testApplyPlanAcceptsEscapedQuoteInLabel);
    CPPUNIT_TEST(testListCapabilitiesMatchesPolicy);
    CPPUNIT_TEST(testDurationMsBoundedWhenProbeDisabled);
    CPPUNIT_TEST(testStubRuntimeReturnsOkJsonEnvelope);
    CPPUNIT_TEST_SUITE_END();
};

void ProviderTest::testDefaultModeIsOffline()
{
    kqoffice::ai::ServiceModePolicy p;
    CPPUNIT_ASSERT_EQUAL(u"offline"_ustr, p.modeName());
    CPPUNIT_ASSERT_EQUAL(kqoffice::ai::ServiceModePolicy::Mode::Offline, p.mode());
}

void ProviderTest::testOfflineAllowsRewrite()
{
    kqoffice::ai::ServiceModePolicy p;
    CPPUNIT_ASSERT(p.allows(u"rewrite"_ustr));
    CPPUNIT_ASSERT(p.allows(u"summarize"_ustr));
    CPPUNIT_ASSERT(p.allows(u"format-fix"_ustr));
    CPPUNIT_ASSERT(p.allows(u"intent-to-uno"_ustr));
}

void ProviderTest::testOfflineDeniesUnknownCapability()
{
    kqoffice::ai::ServiceModePolicy p;
    CPPUNIT_ASSERT(!p.allows(u"steal-keys"_ustr));
    CPPUNIT_ASSERT(!p.allows(u""_ustr));
    CPPUNIT_ASSERT(!p.allows(u"summarize-and-upload"_ustr));
}

void ProviderTest::testEmptyCapabilityThrows()
{
    rtl::Reference<kqoffice::ai::Provider> provider = new kqoffice::ai::Provider;
    css::ai::ProviderRequest req;
    req.capability = u""_ustr;
    req.prompt = u"hi"_ustr;
    req.timeoutMs = 5000;
    CPPUNIT_ASSERT_THROW(provider->call(req),
                         css::lang::IllegalArgumentException);
}

void ProviderTest::testAllowedCapabilityReturnsStubError()
{
    ScopedEvidenceDir tmp;
    rtl::Reference<kqoffice::ai::Provider> provider = new kqoffice::ai::Provider;
    css::ai::ProviderRequest req;
    req.capability = u"rewrite"_ustr;
    req.prompt = u"make this better"_ustr;
    req.timeoutMs = 5000;
    auto rsp = provider->call(req);
    CPPUNIT_ASSERT_EQUAL(u"provider-error"_ustr, rsp.status);
    // Day-1: evidenceId is now populated for any non-policy-denied path.
    CPPUNIT_ASSERT(!rsp.evidenceId.isEmpty());
}

void ProviderTest::testDeniedCapabilityReturnsPolicyDenied()
{
    rtl::Reference<kqoffice::ai::Provider> provider = new kqoffice::ai::Provider;
    css::ai::ProviderRequest req;
    req.capability = u"unauthorized-capability"_ustr;
    req.prompt = u"x"_ustr;
    req.timeoutMs = 1000;
    auto rsp = provider->call(req);
    CPPUNIT_ASSERT_EQUAL(u"policy-denied"_ustr, rsp.status);
    CPPUNIT_ASSERT(rsp.evidenceId.isEmpty());
    // Content must mention the mode name so callers can surface it.
    CPPUNIT_ASSERT(rsp.content.indexOf("offline") >= 0);
}

void ProviderTest::testServiceModeAccessor()
{
    rtl::Reference<kqoffice::ai::Provider> provider = new kqoffice::ai::Provider;
    CPPUNIT_ASSERT_EQUAL(u"offline"_ustr, provider->getServiceMode());
}

void ProviderTest::testEvidenceIdMintedOnProviderError()
{
    ScopedEvidenceDir tmp;
    rtl::Reference<kqoffice::ai::Provider> provider = new kqoffice::ai::Provider;
    css::ai::ProviderRequest req;
    req.capability = u"summarize"_ustr;
    req.prompt = u"some text"_ustr;
    req.timeoutMs = 5000;
    auto rsp = provider->call(req);
    CPPUNIT_ASSERT_EQUAL(u"provider-error"_ustr, rsp.status);
    CPPUNIT_ASSERT(!rsp.evidenceId.isEmpty());
    CPPUNIT_ASSERT(rsp.evidenceId.startsWith("ev-"));
}

void ProviderTest::testEvidenceFileWrittenWithCapability()
{
    ScopedEvidenceDir tmp;
    rtl::Reference<kqoffice::ai::Provider> provider = new kqoffice::ai::Provider;
    css::ai::ProviderRequest req;
    req.capability = u"format-fix"_ustr;
    req.prompt = u"hello"_ustr;
    req.timeoutMs = 5000;
    auto rsp = provider->call(req);
    CPPUNIT_ASSERT(!rsp.evidenceId.isEmpty());

    // Find the file we just wrote — it sits under
    // ${root}/YYYY-MM/<id>.json. We don't know the month at compile
    // time, but `find` is overkill — just glob via stdio fopen of all
    // 12 candidate names? Easier: read back any file whose name ends
    // with the evidence id under tmp.path() (only one month dir, only
    // one record).
    OString idLatin = OUStringToOString(rsp.evidenceId,
                                        RTL_TEXTENCODING_UTF8);
    OString cmd = OString::Concat("cat ")
                + tmp.path() + "/*/" + idLatin + ".json";
    FILE* p = ::popen(cmd.getStr(), "r");
    CPPUNIT_ASSERT(p != nullptr);
    char buf[2048];
    size_t n = std::fread(buf, 1, sizeof(buf) - 1, p);
    ::pclose(p);
    buf[n] = 0;
    OString body(buf, static_cast<sal_Int32>(n));
    CPPUNIT_ASSERT(body.indexOf("\"capability\"") >= 0);
    CPPUNIT_ASSERT(body.indexOf("format-fix") >= 0);
    CPPUNIT_ASSERT(body.indexOf("\"service_mode\"") >= 0);
    CPPUNIT_ASSERT(body.indexOf("offline") >= 0);
    CPPUNIT_ASSERT(body.indexOf("\"status\"") >= 0);
    CPPUNIT_ASSERT(body.indexOf("provider-error") >= 0);
}

void ProviderTest::testEvidenceIdsAreUniqueAcrossCalls()
{
    ScopedEvidenceDir tmp;
    rtl::Reference<kqoffice::ai::Provider> provider = new kqoffice::ai::Provider;
    css::ai::ProviderRequest req;
    req.capability = u"rewrite"_ustr;
    req.prompt = u"a"_ustr;
    req.timeoutMs = 5000;
    auto a = provider->call(req);
    auto b = provider->call(req);
    CPPUNIT_ASSERT(!a.evidenceId.isEmpty());
    CPPUNIT_ASSERT(!b.evidenceId.isEmpty());
    CPPUNIT_ASSERT(a.evidenceId != b.evidenceId);
}

void ProviderTest::testParseModelsJsonFixture()
{
    // Canonical /api/tags response shape — name is the first key of each
    // model object, which is exactly what parseModelsJson scans for.
    OString body(
        "{\"models\":["
        "{\"name\":\"qwen2.5:7b\",\"modified_at\":\"2026-01-01T00:00:00Z\",\"size\":4700000000},"
        "{\"name\":\"llama3.2:3b\",\"modified_at\":\"2026-01-02T00:00:00Z\",\"size\":2000000000}"
        "]}");
    std::vector<OUString> names = kqoffice::ai::OllamaAdapter::parseModelsJson(body);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(2), names.size());
    CPPUNIT_ASSERT_EQUAL(u"qwen2.5:7b"_ustr, names[0]);
    CPPUNIT_ASSERT_EQUAL(u"llama3.2:3b"_ustr, names[1]);
}

void ProviderTest::testBuildGenerateRequestJsonUsesJsonMode()
{
    OString body = kqoffice::ai::OllamaAdapter::buildGenerateRequestJson(
        u"qwen\"3:0.6b"_ustr, u"Return JSON with tab\tindent"_ustr);
    CPPUNIT_ASSERT(body.indexOf("\"model\":\"qwen\\\"3:0.6b\"") >= 0);
    CPPUNIT_ASSERT(body.indexOf("\"prompt\":\"Return JSON with tab\\tindent\"") >= 0);
    CPPUNIT_ASSERT(body.indexOf("\"stream\":false") >= 0);
    CPPUNIT_ASSERT(body.indexOf("\"format\":\"json\"") >= 0);
    CPPUNIT_ASSERT(body.indexOf("\"options\":{\"temperature\":0}") >= 0);
}

void ProviderTest::testParseGenerateJsonFixture()
{
    // Canonical /api/generate non-stream response. Only the `response`
    // field is lifted out.
    OString body(
        "{\"model\":\"qwen2.5:7b\",\"created_at\":\"2026-05-08T00:00:00Z\","
        "\"response\":\"Hello, world.\",\"done\":true,"
        "\"total_duration\":123456}");
    OUString text = kqoffice::ai::OllamaAdapter::parseGenerateJson(body);
    CPPUNIT_ASSERT_EQUAL(u"Hello, world."_ustr, text);

    // Missing `response` key → empty OUString, caller maps to provider-error.
    OString empty("{\"model\":\"qwen2.5:7b\",\"done\":true}");
    CPPUNIT_ASSERT(kqoffice::ai::OllamaAdapter::parseGenerateJson(empty).isEmpty());
}

void ProviderTest::testParseGenerateJsonHandlesEscapes()
{
    // Real responses contain newlines and quoted snippets; the parser
    // must honor the standard JSON string escapes it uses in
    // parseModelsJson plus \b/\f for completeness.
    OString body(
        "{\"response\":\"line1\\nline2 \\\"quoted\\\" \\\\slash\\\\\\tdone\","
        "\"done\":true}");
    OUString text = kqoffice::ai::OllamaAdapter::parseGenerateJson(body);
    CPPUNIT_ASSERT_EQUAL(
        u"line1\nline2 \"quoted\" \\slash\\\tdone"_ustr, text);
}

void ProviderTest::testProbeUnreachableOnClosedPort()
{
    // probe() must complete fast on a developer laptop where nothing is
    // bound to 11434. SO_RCVTIMEO is 100ms, give 500ms headroom for
    // scheduler jitter on a loaded box. Result must be one of the two
    // valid sentinels — never throws.
    kqoffice::ai::OllamaAdapter adapter;
    auto t0 = std::chrono::steady_clock::now();
    OUString result = adapter.probe();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - t0)
                       .count();
    CPPUNIT_ASSERT(elapsed < 500);
    CPPUNIT_ASSERT(result == u"reachable"_ustr || result == u"unreachable"_ustr);
}

void ProviderTest::testProbeDoesNotHang()
{
    // Hard upper bound — even on a worst-case laptop probe() must
    // return within 1s. This is the regression guard against a future
    // edit that drops SO_RCVTIMEO or switches to a blocking connect.
    kqoffice::ai::OllamaAdapter adapter;
    auto t0 = std::chrono::steady_clock::now();
    (void)adapter.probe();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - t0)
                       .count();
    CPPUNIT_ASSERT(elapsed < 1000);
}

namespace
{
// Mirror of docs/schemas/fixtures/apply-plan.valid.json — embedded inline
// so the cppunit binary stays hermetic (no disk reads, no SRCDIR lookup).
const char* kApplyPlanValidFixture =
    "{"
    "\"schema_version\": \"m3-02\","
    "\"id\": \"apply.writer.style-spacing\","
    "\"preview_action_id\": \"preview.writer.style-spacing\","
    "\"capability_id\": \"writer.diagnostics.style-spacing\","
    "\"revision_precondition\": \"document-revision-match\","
    "\"operation_summary_zh\": \"在用户确认后按预览结果调整段落间距\","
    "\"deterministic\": true,"
    "\"rollback_required\": true,"
    "\"undo_group\": {"
        "\"mode\": \"one-user-action\","
        "\"label_zh\": \"应用段落间距建议\""
    "},"
    "\"failure_behavior\": {"
        "\"document_mutation_on_failure\": \"none\","
        "\"message_zh\": \"应用失败时文档保持不变\""
    "},"
    "\"repeated_diagnostics_required\": true"
    "}";

// Mirror of apply-plan.invalid.json — diverges in
// failure_behavior.document_mutation_on_failure ("partial" instead of
// the required "none").
const char* kApplyPlanInvalidFixture =
    "{"
    "\"schema_version\": \"m3-02\","
    "\"id\": \"apply.writer.style-spacing\","
    "\"preview_action_id\": \"preview.writer.style-spacing\","
    "\"capability_id\": \"writer.diagnostics.style-spacing\","
    "\"revision_precondition\": \"document-revision-match\","
    "\"operation_summary_zh\": \"在用户确认后按预览结果调整段落间距\","
    "\"deterministic\": true,"
    "\"rollback_required\": true,"
    "\"undo_group\": {"
        "\"mode\": \"one-user-action\","
        "\"label_zh\": \"应用段落间距建议\""
    "},"
    "\"failure_behavior\": {"
        "\"document_mutation_on_failure\": \"partial\","
        "\"message_zh\": \"应用失败时可能保留部分修改\""
    "},"
    "\"repeated_diagnostics_required\": true"
    "}";
} // namespace

void ProviderTest::testApplyPlanValidFixturePasses()
{
    auto r = kqoffice::ai::ApplyPlanValidator::validate(
        OString(kApplyPlanValidFixture));
    CPPUNIT_ASSERT_EQUAL_MESSAGE(
        OUStringToOString(r.errorPath, RTL_TEXTENCODING_UTF8).getStr(),
        kqoffice::ai::ApplyPlanValidationCode::Ok, r.code);
    CPPUNIT_ASSERT(r.ok());
}

void ProviderTest::testApplyPlanInvalidFixtureFlagsFailureBehavior()
{
    // The invalid fixture's only divergence is
    // failure_behavior.document_mutation_on_failure == "partial". The
    // validator must reject *that exact* field — not silently pass or
    // flag a different field.
    auto r = kqoffice::ai::ApplyPlanValidator::validate(
        OString(kApplyPlanInvalidFixture));
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::FailureBehaviorBad, r.code);
    CPPUNIT_ASSERT_EQUAL(
        u"/failure_behavior/document_mutation_on_failure"_ustr, r.errorPath);
}

void ProviderTest::testApplyPlanRejectsWrongSchemaVersion()
{
    // schema_version is the very first thing checked — older readers
    // must opt out cleanly when servers ship m3-03 / m4 / etc.
    OString body(
        "{"
        "\"schema_version\": \"m3-99\","
        "\"id\": \"apply.x\","
        "\"preview_action_id\": \"preview.x\","
        "\"capability_id\": \"writer.x\","
        "\"revision_precondition\": \"document-revision-match\","
        "\"operation_summary_zh\": \"x\","
        "\"deterministic\": true,"
        "\"rollback_required\": true,"
        "\"undo_group\": {\"mode\": \"one-user-action\", \"label_zh\": \"x\"},"
        "\"failure_behavior\": {\"document_mutation_on_failure\": \"none\","
                              " \"message_zh\": \"x\"},"
        "\"repeated_diagnostics_required\": true"
        "}");
    auto r = kqoffice::ai::ApplyPlanValidator::validate(body);
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::SchemaVersionMismatch, r.code);
    CPPUNIT_ASSERT_EQUAL(u"/schema_version"_ustr, r.errorPath);
}

void ProviderTest::testApplyPlanRejectsBadIdPattern()
{
    // id pattern: ^[a-z0-9][a-z0-9.-]{2,80}$ — uppercase + space here
    // both violate. capability_id has the cleanest single-character
    // failure (uppercase 'X') so we point the test at it for clarity.
    OString body(
        "{"
        "\"schema_version\": \"m3-02\","
        "\"id\": \"apply.x\","
        "\"preview_action_id\": \"preview.x\","
        "\"capability_id\": \"Writer.X\","
        "\"revision_precondition\": \"document-revision-match\","
        "\"operation_summary_zh\": \"x\","
        "\"deterministic\": true,"
        "\"rollback_required\": true,"
        "\"undo_group\": {\"mode\": \"one-user-action\", \"label_zh\": \"x\"},"
        "\"failure_behavior\": {\"document_mutation_on_failure\": \"none\","
                              " \"message_zh\": \"x\"},"
        "\"repeated_diagnostics_required\": true"
        "}");
    auto r = kqoffice::ai::ApplyPlanValidator::validate(body);
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::IdPatternMismatch, r.code);
    CPPUNIT_ASSERT_EQUAL(u"/capability_id"_ustr, r.errorPath);
}

void ProviderTest::testApplyPlanRequiresUndoGroupLabel()
{
    // undo_group.label_zh empty must fail with UndoLabelEmpty pointing
    // at the nested path. A localized UI layer surfaces this as a
    // schema-mismatch toast without re-implementing the schema.
    OString body(
        "{"
        "\"schema_version\": \"m3-02\","
        "\"id\": \"apply.x\","
        "\"preview_action_id\": \"preview.x\","
        "\"capability_id\": \"writer.x\","
        "\"revision_precondition\": \"document-revision-match\","
        "\"operation_summary_zh\": \"x\","
        "\"deterministic\": true,"
        "\"rollback_required\": true,"
        "\"undo_group\": {\"mode\": \"one-user-action\", \"label_zh\": \"\"},"
        "\"failure_behavior\": {\"document_mutation_on_failure\": \"none\","
                              " \"message_zh\": \"x\"},"
        "\"repeated_diagnostics_required\": true"
        "}");
    auto r = kqoffice::ai::ApplyPlanValidator::validate(body);
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::UndoLabelEmpty, r.code);
    CPPUNIT_ASSERT_EQUAL(u"/undo_group/label_zh"_ustr, r.errorPath);
}

void ProviderTest::testApplyPlanRequiresDeterministicTrue()
{
    // deterministic === true is a hard guardrail: a non-deterministic
    // plan must never reach the apply engine.
    OString body(
        "{"
        "\"schema_version\": \"m3-02\","
        "\"id\": \"apply.x\","
        "\"preview_action_id\": \"preview.x\","
        "\"capability_id\": \"writer.x\","
        "\"revision_precondition\": \"document-revision-match\","
        "\"operation_summary_zh\": \"x\","
        "\"deterministic\": false,"
        "\"rollback_required\": true,"
        "\"undo_group\": {\"mode\": \"one-user-action\", \"label_zh\": \"x\"},"
        "\"failure_behavior\": {\"document_mutation_on_failure\": \"none\","
                              " \"message_zh\": \"x\"},"
        "\"repeated_diagnostics_required\": true"
        "}");
    auto r = kqoffice::ai::ApplyPlanValidator::validate(body);
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::DeterministicNotTrue, r.code);
    CPPUNIT_ASSERT_EQUAL(u"/deterministic"_ustr, r.errorPath);
}

void ProviderTest::testApplyPlanRejectsNonObject()
{
    // Empty / non-object payloads must short-circuit before key lookup
    // ever runs.
    auto r1 = kqoffice::ai::ApplyPlanValidator::validate(OString());
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::NotJsonObject, r1.code);
    auto r2 = kqoffice::ai::ApplyPlanValidator::validate(OString("[1,2,3]"));
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::NotJsonObject, r2.code);
    auto r3 = kqoffice::ai::ApplyPlanValidator::validate(OString("null"));
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::NotJsonObject, r3.code);
}

void ProviderTest::testApplyPlanRequiresOperationSummary()
{
    // operation_summary_zh is the human-readable diff caption — empty
    // string is a guardrail violation (UI would show no description).
    OString body(
        "{"
        "\"schema_version\": \"m3-02\","
        "\"id\": \"apply.x\","
        "\"preview_action_id\": \"preview.x\","
        "\"capability_id\": \"writer.x\","
        "\"revision_precondition\": \"document-revision-match\","
        "\"operation_summary_zh\": \"\","
        "\"deterministic\": true,"
        "\"rollback_required\": true,"
        "\"undo_group\": {\"mode\": \"one-user-action\", \"label_zh\": \"x\"},"
        "\"failure_behavior\": {\"document_mutation_on_failure\": \"none\","
                              " \"message_zh\": \"x\"},"
        "\"repeated_diagnostics_required\": true"
        "}");
    auto r = kqoffice::ai::ApplyPlanValidator::validate(body);
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::OperationSummaryEmpty, r.code);
    CPPUNIT_ASSERT_EQUAL(u"/operation_summary_zh"_ustr, r.errorPath);
}

void ProviderTest::testApplyPlanRejectsBadRevisionPrecondition()
{
    // revision_precondition is a frozen const ("document-revision-match")
    // — a server that ships any other token is signaling a contract
    // change the apply runtime cannot safely honor.
    OString body(
        "{"
        "\"schema_version\": \"m3-02\","
        "\"id\": \"apply.x\","
        "\"preview_action_id\": \"preview.x\","
        "\"capability_id\": \"writer.x\","
        "\"revision_precondition\": \"any-revision\","
        "\"operation_summary_zh\": \"x\","
        "\"deterministic\": true,"
        "\"rollback_required\": true,"
        "\"undo_group\": {\"mode\": \"one-user-action\", \"label_zh\": \"x\"},"
        "\"failure_behavior\": {\"document_mutation_on_failure\": \"none\","
                              " \"message_zh\": \"x\"},"
        "\"repeated_diagnostics_required\": true"
        "}");
    auto r = kqoffice::ai::ApplyPlanValidator::validate(body);
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::RevisionPreconditionBad,
        r.code);
    CPPUNIT_ASSERT_EQUAL(u"/revision_precondition"_ustr, r.errorPath);
}

void ProviderTest::testApplyPlanRequiresRollbackTrue()
{
    // rollback_required === true is the contract that lets the engine
    // revert mid-apply on failure. false → reject before mutation.
    OString body(
        "{"
        "\"schema_version\": \"m3-02\","
        "\"id\": \"apply.x\","
        "\"preview_action_id\": \"preview.x\","
        "\"capability_id\": \"writer.x\","
        "\"revision_precondition\": \"document-revision-match\","
        "\"operation_summary_zh\": \"x\","
        "\"deterministic\": true,"
        "\"rollback_required\": false,"
        "\"undo_group\": {\"mode\": \"one-user-action\", \"label_zh\": \"x\"},"
        "\"failure_behavior\": {\"document_mutation_on_failure\": \"none\","
                              " \"message_zh\": \"x\"},"
        "\"repeated_diagnostics_required\": true"
        "}");
    auto r = kqoffice::ai::ApplyPlanValidator::validate(body);
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::RollbackRequiredNotTrue,
        r.code);
    CPPUNIT_ASSERT_EQUAL(u"/rollback_required"_ustr, r.errorPath);
}

void ProviderTest::testApplyPlanRejectsBadUndoGroupMode()
{
    // undo_group.mode must be exactly "one-user-action" — any other
    // value would let the apply land as multiple separate undo steps,
    // breaking the rollback contract.
    OString body(
        "{"
        "\"schema_version\": \"m3-02\","
        "\"id\": \"apply.x\","
        "\"preview_action_id\": \"preview.x\","
        "\"capability_id\": \"writer.x\","
        "\"revision_precondition\": \"document-revision-match\","
        "\"operation_summary_zh\": \"x\","
        "\"deterministic\": true,"
        "\"rollback_required\": true,"
        "\"undo_group\": {\"mode\": \"per-paragraph\", \"label_zh\": \"x\"},"
        "\"failure_behavior\": {\"document_mutation_on_failure\": \"none\","
                              " \"message_zh\": \"x\"},"
        "\"repeated_diagnostics_required\": true"
        "}");
    auto r = kqoffice::ai::ApplyPlanValidator::validate(body);
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::UndoGroupModeBad, r.code);
    CPPUNIT_ASSERT_EQUAL(u"/undo_group/mode"_ustr, r.errorPath);
}

void ProviderTest::testApplyPlanRequiresFailureMessage()
{
    // Empty failure_behavior.message_zh would leave the UI with nothing
    // to surface when the apply engine bails — guardrail violation.
    OString body(
        "{"
        "\"schema_version\": \"m3-02\","
        "\"id\": \"apply.x\","
        "\"preview_action_id\": \"preview.x\","
        "\"capability_id\": \"writer.x\","
        "\"revision_precondition\": \"document-revision-match\","
        "\"operation_summary_zh\": \"x\","
        "\"deterministic\": true,"
        "\"rollback_required\": true,"
        "\"undo_group\": {\"mode\": \"one-user-action\", \"label_zh\": \"x\"},"
        "\"failure_behavior\": {\"document_mutation_on_failure\": \"none\","
                              " \"message_zh\": \"\"},"
        "\"repeated_diagnostics_required\": true"
        "}");
    auto r = kqoffice::ai::ApplyPlanValidator::validate(body);
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::FailureMessageEmpty, r.code);
    CPPUNIT_ASSERT_EQUAL(u"/failure_behavior/message_zh"_ustr, r.errorPath);
}

void ProviderTest::testApplyPlanRequiresRepeatedDiagnosticsTrue()
{
    // repeated_diagnostics_required === true is the contract that the
    // engine reruns diagnostics post-apply (so the UI can show
    // before/after deltas). false → reject.
    OString body(
        "{"
        "\"schema_version\": \"m3-02\","
        "\"id\": \"apply.x\","
        "\"preview_action_id\": \"preview.x\","
        "\"capability_id\": \"writer.x\","
        "\"revision_precondition\": \"document-revision-match\","
        "\"operation_summary_zh\": \"x\","
        "\"deterministic\": true,"
        "\"rollback_required\": true,"
        "\"undo_group\": {\"mode\": \"one-user-action\", \"label_zh\": \"x\"},"
        "\"failure_behavior\": {\"document_mutation_on_failure\": \"none\","
                              " \"message_zh\": \"x\"},"
        "\"repeated_diagnostics_required\": false"
        "}");
    auto r = kqoffice::ai::ApplyPlanValidator::validate(body);
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::RepeatedDiagnosticsBad,
        r.code);
    CPPUNIT_ASSERT_EQUAL(u"/repeated_diagnostics_required"_ustr,
                         r.errorPath);
}

void ProviderTest::testApplyPlanReportsMissingTopLevelField()
{
    // schema_version is checked first — drop it and the validator must
    // call out the exact missing key with a JSON-pointer path so the UI
    // can compose a precise schema-mismatch toast.
    OString body(
        "{"
        "\"id\": \"apply.x\","
        "\"preview_action_id\": \"preview.x\","
        "\"capability_id\": \"writer.x\","
        "\"revision_precondition\": \"document-revision-match\","
        "\"operation_summary_zh\": \"x\","
        "\"deterministic\": true,"
        "\"rollback_required\": true,"
        "\"undo_group\": {\"mode\": \"one-user-action\", \"label_zh\": \"x\"},"
        "\"failure_behavior\": {\"document_mutation_on_failure\": \"none\","
                              " \"message_zh\": \"x\"},"
        "\"repeated_diagnostics_required\": true"
        "}");
    auto r = kqoffice::ai::ApplyPlanValidator::validate(body);
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::MissingField, r.code);
    CPPUNIT_ASSERT_EQUAL(u"/schema_version"_ustr, r.errorPath);
}

void ProviderTest::testApplyPlanRejectsTooShortId()
{
    // id pattern requires ≥3 chars total. "ab" is two chars → fail on
    // length, not on the body alphabet. This is the lower-bound
    // regression guard against a future off-by-one in apv_idPatternOk.
    OString body(
        "{"
        "\"schema_version\": \"m3-02\","
        "\"id\": \"ab\","
        "\"preview_action_id\": \"preview.x\","
        "\"capability_id\": \"writer.x\","
        "\"revision_precondition\": \"document-revision-match\","
        "\"operation_summary_zh\": \"x\","
        "\"deterministic\": true,"
        "\"rollback_required\": true,"
        "\"undo_group\": {\"mode\": \"one-user-action\", \"label_zh\": \"x\"},"
        "\"failure_behavior\": {\"document_mutation_on_failure\": \"none\","
                              " \"message_zh\": \"x\"},"
        "\"repeated_diagnostics_required\": true"
        "}");
    auto r = kqoffice::ai::ApplyPlanValidator::validate(body);
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::IdPatternMismatch, r.code);
    CPPUNIT_ASSERT_EQUAL(u"/id"_ustr, r.errorPath);
}

void ProviderTest::testApplyPlanIdAcceptsLongestLegal()
{
    // Pattern upper bound: head char + 80 body chars = 81 total. This
    // is the boundary-inclusive case; a single extra char would trip
    // the too-long guard (see testApplyPlanIdRejectsTooLong).
    OStringBuffer idBuf("a");
    for (int k = 0; k < 80; ++k) idBuf.append('x');
    OString longId = idBuf.makeStringAndClear();
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(81), longId.getLength());

    OString body = "{"
        "\"schema_version\": \"m3-02\","
        "\"id\": \"" + longId + "\","
        "\"preview_action_id\": \"preview.x\","
        "\"capability_id\": \"writer.x\","
        "\"revision_precondition\": \"document-revision-match\","
        "\"operation_summary_zh\": \"x\","
        "\"deterministic\": true,"
        "\"rollback_required\": true,"
        "\"undo_group\": {\"mode\": \"one-user-action\", \"label_zh\": \"x\"},"
        "\"failure_behavior\": {\"document_mutation_on_failure\": \"none\","
                              " \"message_zh\": \"x\"},"
        "\"repeated_diagnostics_required\": true"
        "}";
    auto r = kqoffice::ai::ApplyPlanValidator::validate(body);
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::Ok, r.code);
}

void ProviderTest::testApplyPlanIdRejectsTooLong()
{
    // 82 chars (head + 81 body) must fail. Paired with the "longest
    // legal" case above to nail the exact regex boundary.
    OStringBuffer idBuf("a");
    for (int k = 0; k < 81; ++k) idBuf.append('x');
    OString tooLong = idBuf.makeStringAndClear();
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(82), tooLong.getLength());

    OString body = "{"
        "\"schema_version\": \"m3-02\","
        "\"id\": \"" + tooLong + "\","
        "\"preview_action_id\": \"preview.x\","
        "\"capability_id\": \"writer.x\","
        "\"revision_precondition\": \"document-revision-match\","
        "\"operation_summary_zh\": \"x\","
        "\"deterministic\": true,"
        "\"rollback_required\": true,"
        "\"undo_group\": {\"mode\": \"one-user-action\", \"label_zh\": \"x\"},"
        "\"failure_behavior\": {\"document_mutation_on_failure\": \"none\","
                              " \"message_zh\": \"x\"},"
        "\"repeated_diagnostics_required\": true"
        "}";
    auto r = kqoffice::ai::ApplyPlanValidator::validate(body);
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::IdPatternMismatch, r.code);
    CPPUNIT_ASSERT_EQUAL(u"/id"_ustr, r.errorPath);
}

void ProviderTest::testApplyPlanIgnoresUnknownTopLevelKeys()
{
    // JSON Schema has additionalProperties:false at the object level,
    // but the runtime validator is intentionally permissive on unknown
    // keys — the m3-02 contract is "must have these fields with these
    // values", not "must have *only* these fields". A future server
    // may carry forward-compat extension keys and older readers must
    // still accept the payload. Guard that property here.
    OString body(
        "{"
        "\"schema_version\": \"m3-02\","
        "\"id\": \"apply.x\","
        "\"preview_action_id\": \"preview.x\","
        "\"capability_id\": \"writer.x\","
        "\"revision_precondition\": \"document-revision-match\","
        "\"operation_summary_zh\": \"x\","
        "\"deterministic\": true,"
        "\"rollback_required\": true,"
        "\"undo_group\": {\"mode\": \"one-user-action\", \"label_zh\": \"x\"},"
        "\"failure_behavior\": {\"document_mutation_on_failure\": \"none\","
                              " \"message_zh\": \"x\"},"
        "\"repeated_diagnostics_required\": true,"
        "\"telemetry_hint_v2\": {\"preview_cost_ms\": 42},"
        "\"future_bag\": [1, 2, 3]"
        "}");
    auto r = kqoffice::ai::ApplyPlanValidator::validate(body);
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::Ok, r.code);
}

void ProviderTest::testApplyPlanCodeEnumStable()
{
    // Metaguard: freeze the ValidationCode enum layout. Adding a new
    // code must also add a corresponding CPPUNIT_TEST that produces
    // that code — this assertion trips first, reminding the author.
    CPPUNIT_ASSERT_EQUAL(
        static_cast<int>(0),
        static_cast<int>(kqoffice::ai::ApplyPlanValidationCode::Ok));
    // 14 codes in m3-02: Ok + 13 failure tags. A future reader that
    // adds a 15th code without adding a test will trip this line.
    CPPUNIT_ASSERT_EQUAL(
        static_cast<int>(13),
        static_cast<int>(
            kqoffice::ai::ApplyPlanValidationCode::RepeatedDiagnosticsBad));
}

void ProviderTest::testApplyPlanMessageOkReturnsEmpty()
{
    // No toast on success — the helper returns "" and a UI surface
    // shows nothing. This is the contract.
    kqoffice::ai::ApplyPlanValidationResult r;
    CPPUNIT_ASSERT_EQUAL(kqoffice::ai::ApplyPlanValidationCode::Ok, r.code);
    CPPUNIT_ASSERT(kqoffice::ai::applyPlanValidationMessage(r).isEmpty());
}

void ProviderTest::testApplyPlanMessageMentionsFieldPath()
{
    // The invalid fixture trips FailureBehaviorBad with errorPath
    // "/failure_behavior/document_mutation_on_failure". The localized
    // message must mention the path so the user can tell which field
    // failed without re-reading the schema.
    auto r = kqoffice::ai::ApplyPlanValidator::validate(
        OString(kApplyPlanInvalidFixture));
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::FailureBehaviorBad, r.code);
    OUString msg = kqoffice::ai::applyPlanValidationMessage(r);
    CPPUNIT_ASSERT(!msg.isEmpty());
    CPPUNIT_ASSERT(msg.indexOf(u"failure_behavior") >= 0);
    CPPUNIT_ASSERT(msg.indexOf(u"document_mutation_on_failure") >= 0);
    // zh-CN base text marker — the toast must read in Chinese.
    CPPUNIT_ASSERT(msg.indexOf(u"已取消本次应用") >= 0);
}

void ProviderTest::testApplyPlanMessageDistinctPerCode()
{
    // Each non-Ok code should produce a distinct base message — a
    // copy-paste collision would mask which guardrail fired. Verify
    // by comparing every pair of base messages (errorPath stripped
    // for fairness).
    using Code = kqoffice::ai::ApplyPlanValidationCode;
    Code codes[] = {
        Code::NotJsonObject,
        Code::MissingField,
        Code::SchemaVersionMismatch,
        Code::IdPatternMismatch,
        Code::RevisionPreconditionBad,
        Code::DeterministicNotTrue,
        Code::RollbackRequiredNotTrue,
        Code::UndoGroupModeBad,
        Code::UndoLabelEmpty,
        Code::FailureBehaviorBad,
        Code::FailureMessageEmpty,
        Code::OperationSummaryEmpty,
        Code::RepeatedDiagnosticsBad,
    };
    constexpr size_t N = sizeof(codes) / sizeof(codes[0]);
    OUString msgs[N];
    for (size_t k = 0; k < N; ++k)
    {
        kqoffice::ai::ApplyPlanValidationResult r;
        r.code = codes[k];
        // No errorPath so we compare only the base text.
        msgs[k] = kqoffice::ai::applyPlanValidationMessage(r);
        CPPUNIT_ASSERT(!msgs[k].isEmpty());
    }
    for (size_t a = 0; a < N; ++a)
        for (size_t b = a + 1; b < N; ++b)
            CPPUNIT_ASSERT_MESSAGE("collision in localized messages",
                                   msgs[a] != msgs[b]);
}

void ProviderTest::testApplyPlanMessageHandlesEmptyPath()
{
    // NotJsonObject is the one code where errorPath naturally stays
    // empty (the body never reached key lookup). Message must be
    // sensible (no trailing "（字段：）") and present.
    kqoffice::ai::ApplyPlanValidationResult r;
    r.code = kqoffice::ai::ApplyPlanValidationCode::NotJsonObject;
    OUString msg = kqoffice::ai::applyPlanValidationMessage(r);
    CPPUNIT_ASSERT(!msg.isEmpty());
    // The "field:" suffix must not appear when path is empty.
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(-1),
                         msg.indexOf(u"字段："));
}

void ProviderTest::testApvFindTopKeyBasic()
{
    // Direct micro-test for the parser primitive — locates a top-level
    // "schema_version" and returns the byte index right after the
    // colon (i.e. start of the value with possibly leading ws).
    OString body("{\"schema_version\":\"m3-02\",\"id\":\"x\"}");
    sal_Int32 idx = kqoffice::ai::detail::apv_findTopKey(
        body, "\"schema_version\"");
    CPPUNIT_ASSERT(idx > 0);
    // Byte at idx must be the opening quote of the value.
    CPPUNIT_ASSERT_EQUAL('"', body.getStr()[idx]);
    // Missing key returns -1.
    sal_Int32 miss = kqoffice::ai::detail::apv_findTopKey(
        body, "\"absent_field\"");
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(-1), miss);
}

void ProviderTest::testApvFindTopKeyIgnoresNestedSameKey()
{
    // A "label_zh" buried inside undo_group must NOT be reported as a
    // top-level hit. depth-aware lookup is the whole point of this
    // helper.
    OString body(
        "{"
        "\"schema_version\":\"m3-02\","
        "\"undo_group\":{\"mode\":\"x\",\"label_zh\":\"NESTED\"},"
        "\"label_zh\":\"TOP\""
        "}");
    sal_Int32 idx = kqoffice::ai::detail::apv_findTopKey(
        body, "\"label_zh\"");
    CPPUNIT_ASSERT(idx > 0);
    auto sr = kqoffice::ai::detail::apv_readString(body, idx);
    CPPUNIT_ASSERT(sr.isPresent);
    CPPUNIT_ASSERT_EQUAL(OString("TOP"), sr.value);
}

void ProviderTest::testApvFindTopKeyIgnoresKeyInStringValue()
{
    // "schema_version" appearing inside another value's string literal
    // must not be matched. Quote-awareness regression guard.
    OString body(
        "{"
        "\"operation_summary_zh\":\"text \\\"schema_version\\\" inside\","
        "\"schema_version\":\"m3-02\""
        "}");
    sal_Int32 idx = kqoffice::ai::detail::apv_findTopKey(
        body, "\"schema_version\"");
    CPPUNIT_ASSERT(idx > 0);
    auto sr = kqoffice::ai::detail::apv_readString(body, idx);
    CPPUNIT_ASSERT(sr.isPresent);
    CPPUNIT_ASSERT_EQUAL(OString("m3-02"), sr.value);
}

void ProviderTest::testApvReadStringDecodesEscapes()
{
    // Standard JSON string escapes: \" \\ \/ \n \t \r \b \f. Unknown
    // escape (per the helper's policy) passes through the next char
    // verbatim — locked here so a future refactor doesn't silently
    // change behavior.
    OString body("\"a\\\"b\\\\c\\/d\\ne\\tf\\rg\"");
    auto sr = kqoffice::ai::detail::apv_readString(body, 0);
    CPPUNIT_ASSERT(sr.isPresent);
    CPPUNIT_ASSERT_EQUAL(OString("a\"b\\c/d\ne\tf\rg"), sr.value);
}

void ProviderTest::testApvReadStringRejectsNonString()
{
    // Bool / number / null at the value position must report
    // isPresent=false so callers can fall through to the bool reader.
    OString numeric("42, rest");
    auto a = kqoffice::ai::detail::apv_readString(numeric, 0);
    CPPUNIT_ASSERT(!a.isPresent);

    OString boolean("true");
    auto b = kqoffice::ai::detail::apv_readString(boolean, 0);
    CPPUNIT_ASSERT(!b.isPresent);

    OString empty("");
    auto c = kqoffice::ai::detail::apv_readString(empty, 0);
    CPPUNIT_ASSERT(!c.isPresent);
}

void ProviderTest::testApplyPlanStatusOkString()
{
    // Ok → "ok" — matches what EvidenceRecorder already writes for
    // successful Provider::call() paths, so a future SwDocShell apply
    // surface using applyPlanValidationStatus emits the same status
    // token on the success branch.
    CPPUNIT_ASSERT_EQUAL(
        OString("ok"),
        kqoffice::ai::applyPlanValidationStatus(
            kqoffice::ai::ApplyPlanValidationCode::Ok));
}

void ProviderTest::testApplyPlanStatusDistinctPerCode()
{
    // Every code must map to a distinct token — same collision guard
    // as the localized message test, but for the ASCII status field.
    using Code = kqoffice::ai::ApplyPlanValidationCode;
    Code codes[] = {
        Code::Ok,
        Code::NotJsonObject,
        Code::MissingField,
        Code::SchemaVersionMismatch,
        Code::IdPatternMismatch,
        Code::RevisionPreconditionBad,
        Code::DeterministicNotTrue,
        Code::RollbackRequiredNotTrue,
        Code::UndoGroupModeBad,
        Code::UndoLabelEmpty,
        Code::FailureBehaviorBad,
        Code::FailureMessageEmpty,
        Code::OperationSummaryEmpty,
        Code::RepeatedDiagnosticsBad,
    };
    constexpr size_t N = sizeof(codes) / sizeof(codes[0]);
    OString tokens[N];
    for (size_t k = 0; k < N; ++k)
    {
        tokens[k] = kqoffice::ai::applyPlanValidationStatus(codes[k]);
        CPPUNIT_ASSERT(!tokens[k].isEmpty());
    }
    for (size_t a = 0; a < N; ++a)
        for (size_t b = a + 1; b < N; ++b)
            CPPUNIT_ASSERT_MESSAGE("collision in status tokens",
                                   tokens[a] != tokens[b]);
}

void ProviderTest::testApplyPlanStatusIsAsciiKebab()
{
    // Audit grep relies on the tokens staying ASCII-kebab. Locked
    // here so a future i18n / localization push doesn't accidentally
    // hand non-ASCII bytes back through this function.
    using Code = kqoffice::ai::ApplyPlanValidationCode;
    Code codes[] = {
        Code::NotJsonObject,
        Code::MissingField,
        Code::FailureBehaviorBad,
        Code::RepeatedDiagnosticsBad,
    };
    for (Code c : codes)
    {
        OString tok = kqoffice::ai::applyPlanValidationStatus(c);
        CPPUNIT_ASSERT(!tok.isEmpty());
        // Failure tokens must start with the canonical prefix.
        CPPUNIT_ASSERT_MESSAGE(tok.getStr(),
                               tok.startsWith("apply-plan-"));
        // Every byte must be [a-z0-9-].
        for (sal_Int32 i = 0; i < tok.getLength(); ++i)
        {
            char ch = tok[i];
            bool okCh = (ch >= 'a' && ch <= 'z')
                        || (ch >= '0' && ch <= '9')
                        || ch == '-';
            CPPUNIT_ASSERT_MESSAGE(tok.getStr(), okCh);
        }
    }
}

void ProviderTest::testApplyPlanAcceptsLongZhCnSummary()
{
    // Real-world operation_summary_zh strings routinely run 80+
    // zh-CN characters (≥ 240 UTF-8 bytes). The schema has no upper
    // length on this field — only minLength:1. Lock that property
    // here so a future edit doesn't silently truncate the toast.
    OString longSummary;
    {
        OStringBuffer b;
        // 50 copies of "改善段落间距与首行缩进，保持原有标题层级不变。"
        // ≈ 50 × 24 UTF-8 bytes = 1200 bytes.
        for (int k = 0; k < 50; ++k)
            b.append("\\u6539\\u5584\\u6bb5\\u843d"
                     "\\u95f4\\u8ddd\\u4e0e\\u9996"
                     "\\u884c\\u7f29\\u8fdb");
        longSummary = b.makeStringAndClear();
    }
    OString body = "{"
        "\"schema_version\":\"m3-02\","
        "\"id\":\"apply.x\","
        "\"preview_action_id\":\"preview.x\","
        "\"capability_id\":\"writer.x\","
        "\"revision_precondition\":\"document-revision-match\","
        "\"operation_summary_zh\":\"" + longSummary + "\","
        "\"deterministic\":true,"
        "\"rollback_required\":true,"
        "\"undo_group\":{\"mode\":\"one-user-action\",\"label_zh\":\"x\"},"
        "\"failure_behavior\":{\"document_mutation_on_failure\":\"none\","
                             "\"message_zh\":\"x\"},"
        "\"repeated_diagnostics_required\":true"
        "}";
    auto r = kqoffice::ai::ApplyPlanValidator::validate(body);
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::Ok, r.code);
}

void ProviderTest::testApplyPlanAcceptsEmojiInLabel()
{
    // undo_group.label_zh allowing emoji / supplementary-plane
    // codepoints is a real user-visible feature (custom undo labels
    // in Chinese markets often use ✅ / ⚠️ / 📝). The validator must
    // not choke on a 4-byte UTF-8 sequence in string body.
    //
    // "📝 应用段落间距建议" — emoji (4 UTF-8 bytes) + zh-CN.
    OString body =
        "{"
        "\"schema_version\":\"m3-02\","
        "\"id\":\"apply.x\","
        "\"preview_action_id\":\"preview.x\","
        "\"capability_id\":\"writer.x\","
        "\"revision_precondition\":\"document-revision-match\","
        "\"operation_summary_zh\":\"x\","
        "\"deterministic\":true,"
        "\"rollback_required\":true,"
        "\"undo_group\":{\"mode\":\"one-user-action\","
                       "\"label_zh\":\"\xF0\x9F\x93\x9D "
                       "\xE5\xBA\x94\xE7\x94\xA8\"},"
        "\"failure_behavior\":{\"document_mutation_on_failure\":\"none\","
                             "\"message_zh\":\"x\"},"
        "\"repeated_diagnostics_required\":true"
        "}";
    auto r = kqoffice::ai::ApplyPlanValidator::validate(body);
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::Ok, r.code);
}

void ProviderTest::testApplyPlanRejectsEmojiInId()
{
    // id pattern is ASCII-only by construction. A 4-byte UTF-8
    // codepoint appearing in `id` must fail — one of the emoji
    // bytes has the high bit set, so apv_idPatternOk's byte-level
    // scan rejects it on the isBody() check.
    OString body =
        "{"
        "\"schema_version\":\"m3-02\","
        "\"id\":\"apply.\xF0\x9F\x93\x9D.x\","
        "\"preview_action_id\":\"preview.x\","
        "\"capability_id\":\"writer.x\","
        "\"revision_precondition\":\"document-revision-match\","
        "\"operation_summary_zh\":\"x\","
        "\"deterministic\":true,"
        "\"rollback_required\":true,"
        "\"undo_group\":{\"mode\":\"one-user-action\",\"label_zh\":\"x\"},"
        "\"failure_behavior\":{\"document_mutation_on_failure\":\"none\","
                             "\"message_zh\":\"x\"},"
        "\"repeated_diagnostics_required\":true"
        "}";
    auto r = kqoffice::ai::ApplyPlanValidator::validate(body);
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::IdPatternMismatch, r.code);
    CPPUNIT_ASSERT_EQUAL(u"/id"_ustr, r.errorPath);
}

void ProviderTest::testApplyPlanRejectsFullwidthInId()
{
    // Fullwidth ASCII ("ａｐｐｌｙ") is a common mojibake source in
    // Chinese input methods — rejected because it's 3-byte UTF-8,
    // not 1-byte ASCII a-z.
    OString body =
        "{"
        "\"schema_version\":\"m3-02\","
        "\"id\":\"\xEF\xBD\x81\xEF\xBD\x90\xEF\xBD\x90\xEF\xBD\x8C\xEF\xBD\x99\","
        "\"preview_action_id\":\"preview.x\","
        "\"capability_id\":\"writer.x\","
        "\"revision_precondition\":\"document-revision-match\","
        "\"operation_summary_zh\":\"x\","
        "\"deterministic\":true,"
        "\"rollback_required\":true,"
        "\"undo_group\":{\"mode\":\"one-user-action\",\"label_zh\":\"x\"},"
        "\"failure_behavior\":{\"document_mutation_on_failure\":\"none\","
                             "\"message_zh\":\"x\"},"
        "\"repeated_diagnostics_required\":true"
        "}";
    auto r = kqoffice::ai::ApplyPlanValidator::validate(body);
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::IdPatternMismatch, r.code);
    CPPUNIT_ASSERT_EQUAL(u"/id"_ustr, r.errorPath);
}

void ProviderTest::testApplyPlanAcceptsEscapedQuoteInLabel()
{
    // Real AI responses may produce undo labels like "调整“首行”缩进"
    // which, when JSON-serialized, contain escaped quotes. The
    // validator's parser must walk through them without mistaking
    // the inner quote for the end of the string value.
    //
    // label_zh: `x\"y\"z` → decoded "x\"y\"z" (7 chars, non-empty).
    OString body =
        "{"
        "\"schema_version\":\"m3-02\","
        "\"id\":\"apply.x\","
        "\"preview_action_id\":\"preview.x\","
        "\"capability_id\":\"writer.x\","
        "\"revision_precondition\":\"document-revision-match\","
        "\"operation_summary_zh\":\"x\","
        "\"deterministic\":true,"
        "\"rollback_required\":true,"
        "\"undo_group\":{\"mode\":\"one-user-action\","
                       "\"label_zh\":\"x\\\"y\\\"z\"},"
        "\"failure_behavior\":{\"document_mutation_on_failure\":\"none\","
                             "\"message_zh\":\"x\"},"
        "\"repeated_diagnostics_required\":true"
        "}";
    auto r = kqoffice::ai::ApplyPlanValidator::validate(body);
    CPPUNIT_ASSERT_EQUAL(
        kqoffice::ai::ApplyPlanValidationCode::Ok, r.code);
}

void ProviderTest::testListCapabilitiesMatchesPolicy()
{
    kqoffice::ai::ServiceModePolicy policy;
    auto policyCaps = policy.currentAllowlist();
    // Clavue-aligned offline allowlist: rewrite/summarize/format-fix/intent-to-uno
    // + chat/plan/review/extract/classify/verify
    CPPUNIT_ASSERT_EQUAL(sal_Int32(10), policyCaps.getLength());

    rtl::Reference<kqoffice::ai::Provider> provider(new kqoffice::ai::Provider());
    auto listed = provider->listCapabilities();
    CPPUNIT_ASSERT_EQUAL(policyCaps.getLength(), listed.getLength());
    for (sal_Int32 i = 0; i < listed.getLength(); ++i)
        CPPUNIT_ASSERT(policyCaps[i] == listed[i]);
}

void ProviderTest::testDurationMsBoundedWhenProbeDisabled()
{
    ScopedEvidenceDir guard;
    rtl::Reference<kqoffice::ai::Provider> provider(new kqoffice::ai::Provider());

    css::ai::ProviderRequest req;
    req.capability = u"rewrite"_ustr;
    req.prompt = u"ping"_ustr;

    auto rsp = provider->call(req);
    CPPUNIT_ASSERT(rsp.durationMs >= 0);
    CPPUNIT_ASSERT(rsp.durationMs < 30000);
}

void ProviderTest::testStubRuntimeReturnsOkJsonEnvelope()
{
    ScopedEvidenceDir guard;
    ::setenv("KQOFFICE_AI_STUB_RUNTIME", "1", 1);
    // ScopedEvidenceDir already sets KQOFFICE_AI_DISABLE_PROBE=1.

    rtl::Reference<kqoffice::ai::Provider> provider(new kqoffice::ai::Provider());
    css::ai::ProviderRequest req;
    req.capability = u"rewrite"_ustr;
    req.prompt = u"after stub runtime"_ustr;
    req.context = u"swpara-2"_ustr;

    const css::ai::ProviderResponse rsp = provider->call(req);
    ::unsetenv("KQOFFICE_AI_STUB_RUNTIME");

    CPPUNIT_ASSERT_EQUAL(u"ok"_ustr, rsp.status);
    CPPUNIT_ASSERT(rsp.content.indexOf(u"\"schema_version\""_ustr) >= 0);
    CPPUNIT_ASSERT(rsp.content.indexOf(u"v2-w3-runtime-1"_ustr) >= 0);
    CPPUNIT_ASSERT(rsp.content.indexOf(u"paragraph-replace"_ustr) >= 0);
    CPPUNIT_ASSERT(rsp.content.indexOf(u"swpara-2"_ustr) >= 0);
    CPPUNIT_ASSERT(rsp.content.indexOf(u"after stub runtime"_ustr) >= 0);
    CPPUNIT_ASSERT(!rsp.evidenceId.isEmpty());
}

CPPUNIT_TEST_SUITE_REGISTRATION(ProviderTest);
} // namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
