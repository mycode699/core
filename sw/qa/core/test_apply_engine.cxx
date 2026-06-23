/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

// W3 Day-1b D1 — IntelligentWriterApplyEngine skeleton tests.
// Pure-logic / enum-stability cppunit; no SwDoc backing required for
// the kind/status round-trip cases. Doc-backing apply tests land in
// W3 Day-1c/d/e/f alongside per-kind SwUndoApplyPatch concrete impls.

#include <cppunit/TestAssert.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/plugin/TestPlugIn.h>

#include <IntelligentWriterApplyEngine.hxx>

namespace
{
class ApplyEngineTest : public CppUnit::TestFixture
{
public:
    void test01_PatchKindToken_paragraphReplace_roundtrip();
    void test02_PatchKindToken_paragraphInsertAfter_roundtrip();
    void test03_PatchKindToken_paragraphDelete_roundtrip();
    void test04_PatchKindToken_paragraphFormat_roundtrip();
    void test05_PatchKindToken_paragraphReformat_roundtrip();
    void test06_PatchKindToken_textRangeReplace_roundtrip();
    void test07_PatchKindToken_textFormat_roundtrip();
    void test08_PatchKindFromToken_unknown_returnsNullopt();
    void test09_ApplyStatusToken_ok();
    void test10_ApplyStatusToken_validationFailed();
    void test11_ApplyStatusToken_staleSnapshot();
    void test12_ApplyStatusToken_patchFailed();
    void test13_ApplyStatusToken_undoException();
    void test14_AllSevenKinds_distinctTokens();
    void test15_ComputeParagraphTextHash_empty_knownDigest();
    void test16_ComputeParagraphTextHash_nonEmpty_differsFromEmpty();
    void test17_ParseParagraphId_validAndInvalid();
    void test18_ComputeParagraphTextHash_empty_isDeleteAfterState();
    void test19_ComputeParagraphTextHash_insertAfterFixtureText();
    void test20_ParseParagraphId_insertAfterNextParagraphIndex();
    void test21_ValidateTextRangeReplacePatch_ok_fixtureRange();
    void test22_ValidateTextRangeReplacePatch_outOfRangeAndIdempotent();
    void test23_ParseFormatChangesStyle_fixtureHeading2();
    void test24_ParseFormatChangesStyle_missingOrMalformed();
    void test25_ParseFormatChangesReformat_fixtureP5();
    void test26_ParseFormatChangesReformat_missingFirstLineIndent();
    void test27_ParseFormatChangesBold_fixtureP7();
    void test28_ValidateTextFormatPatch_rangeBounds();
    void test29_ValidateApplyPlanShape_minimalReplaceAndDelete();
    void test30_ApplyStatusToken_roundtripAllFiveModes();
    void test31_ValidateApplyPlanShape_singleParagraphReplace_inlineContract();
    void test32_ValidateApplyPlanShape_singleParagraphReplace_missingPatchIdFails();
    void test33_ParseApplyPlanRuntimeJson_twoPatchReplaceAndDelete();
    void test34_ParseApplyPlanRuntimeJson_invalidKindReturnsNullopt();
    void test35_ParseApplyPlanRuntimeJson_writerRuntimeFixture();

    CPPUNIT_TEST_SUITE(ApplyEngineTest);
    CPPUNIT_TEST(test01_PatchKindToken_paragraphReplace_roundtrip);
    CPPUNIT_TEST(test02_PatchKindToken_paragraphInsertAfter_roundtrip);
    CPPUNIT_TEST(test03_PatchKindToken_paragraphDelete_roundtrip);
    CPPUNIT_TEST(test04_PatchKindToken_paragraphFormat_roundtrip);
    CPPUNIT_TEST(test05_PatchKindToken_paragraphReformat_roundtrip);
    CPPUNIT_TEST(test06_PatchKindToken_textRangeReplace_roundtrip);
    CPPUNIT_TEST(test07_PatchKindToken_textFormat_roundtrip);
    CPPUNIT_TEST(test08_PatchKindFromToken_unknown_returnsNullopt);
    CPPUNIT_TEST(test09_ApplyStatusToken_ok);
    CPPUNIT_TEST(test10_ApplyStatusToken_validationFailed);
    CPPUNIT_TEST(test11_ApplyStatusToken_staleSnapshot);
    CPPUNIT_TEST(test12_ApplyStatusToken_patchFailed);
    CPPUNIT_TEST(test13_ApplyStatusToken_undoException);
    CPPUNIT_TEST(test14_AllSevenKinds_distinctTokens);
    CPPUNIT_TEST(test15_ComputeParagraphTextHash_empty_knownDigest);
    CPPUNIT_TEST(test16_ComputeParagraphTextHash_nonEmpty_differsFromEmpty);
    CPPUNIT_TEST(test17_ParseParagraphId_validAndInvalid);
    CPPUNIT_TEST(test18_ComputeParagraphTextHash_empty_isDeleteAfterState);
    CPPUNIT_TEST(test19_ComputeParagraphTextHash_insertAfterFixtureText);
    CPPUNIT_TEST(test20_ParseParagraphId_insertAfterNextParagraphIndex);
    CPPUNIT_TEST(test21_ValidateTextRangeReplacePatch_ok_fixtureRange);
    CPPUNIT_TEST(test22_ValidateTextRangeReplacePatch_outOfRangeAndIdempotent);
    CPPUNIT_TEST(test23_ParseFormatChangesStyle_fixtureHeading2);
    CPPUNIT_TEST(test24_ParseFormatChangesStyle_missingOrMalformed);
    CPPUNIT_TEST(test25_ParseFormatChangesReformat_fixtureP5);
    CPPUNIT_TEST(test26_ParseFormatChangesReformat_missingFirstLineIndent);
    CPPUNIT_TEST(test27_ParseFormatChangesBold_fixtureP7);
    CPPUNIT_TEST(test28_ValidateTextFormatPatch_rangeBounds);
    CPPUNIT_TEST(test29_ValidateApplyPlanShape_minimalReplaceAndDelete);
    CPPUNIT_TEST(test30_ApplyStatusToken_roundtripAllFiveModes);
    CPPUNIT_TEST(test31_ValidateApplyPlanShape_singleParagraphReplace_inlineContract);
    CPPUNIT_TEST(test32_ValidateApplyPlanShape_singleParagraphReplace_missingPatchIdFails);
    CPPUNIT_TEST(test33_ParseApplyPlanRuntimeJson_twoPatchReplaceAndDelete);
    CPPUNIT_TEST(test34_ParseApplyPlanRuntimeJson_invalidKindReturnsNullopt);
    CPPUNIT_TEST(test35_ParseApplyPlanRuntimeJson_writerRuntimeFixture);
    CPPUNIT_TEST_SUITE_END();
};

void ApplyEngineTest::test01_PatchKindToken_paragraphReplace_roundtrip()
{
    using namespace sw::intelligent;
    OString tok = PatchKindToken(PatchKind::ParagraphReplace);
    CPPUNIT_ASSERT_EQUAL(OString("paragraph-replace"), tok);
    auto back = PatchKindFromToken("paragraph-replace");
    CPPUNIT_ASSERT(back.has_value());
    CPPUNIT_ASSERT(*back == PatchKind::ParagraphReplace);
}

void ApplyEngineTest::test02_PatchKindToken_paragraphInsertAfter_roundtrip()
{
    using namespace sw::intelligent;
    OString tok = PatchKindToken(PatchKind::ParagraphInsertAfter);
    CPPUNIT_ASSERT_EQUAL(OString("paragraph-insert-after"), tok);
    auto back = PatchKindFromToken("paragraph-insert-after");
    CPPUNIT_ASSERT(back.has_value());
    CPPUNIT_ASSERT(*back == PatchKind::ParagraphInsertAfter);
}

void ApplyEngineTest::test03_PatchKindToken_paragraphDelete_roundtrip()
{
    using namespace sw::intelligent;
    OString tok = PatchKindToken(PatchKind::ParagraphDelete);
    CPPUNIT_ASSERT_EQUAL(OString("paragraph-delete"), tok);
    auto back = PatchKindFromToken("paragraph-delete");
    CPPUNIT_ASSERT(back.has_value());
    CPPUNIT_ASSERT(*back == PatchKind::ParagraphDelete);
}

void ApplyEngineTest::test04_PatchKindToken_paragraphFormat_roundtrip()
{
    using namespace sw::intelligent;
    OString tok = PatchKindToken(PatchKind::ParagraphFormat);
    CPPUNIT_ASSERT_EQUAL(OString("paragraph-format"), tok);
    auto back = PatchKindFromToken("paragraph-format");
    CPPUNIT_ASSERT(back.has_value());
    CPPUNIT_ASSERT(*back == PatchKind::ParagraphFormat);
}

void ApplyEngineTest::test05_PatchKindToken_paragraphReformat_roundtrip()
{
    using namespace sw::intelligent;
    OString tok = PatchKindToken(PatchKind::ParagraphReformat);
    CPPUNIT_ASSERT_EQUAL(OString("paragraph-reformat"), tok);
    auto back = PatchKindFromToken("paragraph-reformat");
    CPPUNIT_ASSERT(back.has_value());
    CPPUNIT_ASSERT(*back == PatchKind::ParagraphReformat);
}

void ApplyEngineTest::test06_PatchKindToken_textRangeReplace_roundtrip()
{
    using namespace sw::intelligent;
    OString tok = PatchKindToken(PatchKind::TextRangeReplace);
    CPPUNIT_ASSERT_EQUAL(OString("text-range-replace"), tok);
    auto back = PatchKindFromToken("text-range-replace");
    CPPUNIT_ASSERT(back.has_value());
    CPPUNIT_ASSERT(*back == PatchKind::TextRangeReplace);
}

void ApplyEngineTest::test07_PatchKindToken_textFormat_roundtrip()
{
    using namespace sw::intelligent;
    OString tok = PatchKindToken(PatchKind::TextFormat);
    CPPUNIT_ASSERT_EQUAL(OString("text-format"), tok);
    auto back = PatchKindFromToken("text-format");
    CPPUNIT_ASSERT(back.has_value());
    CPPUNIT_ASSERT(*back == PatchKind::TextFormat);
}

void ApplyEngineTest::test08_PatchKindFromToken_unknown_returnsNullopt()
{
    using namespace sw::intelligent;
    CPPUNIT_ASSERT(!PatchKindFromToken("not-a-real-kind").has_value());
    CPPUNIT_ASSERT(!PatchKindFromToken("").has_value());
    CPPUNIT_ASSERT(!PatchKindFromToken("table-cell-replace").has_value());
}

void ApplyEngineTest::test09_ApplyStatusToken_ok()
{
    using namespace sw::intelligent;
    CPPUNIT_ASSERT_EQUAL(OString("ok"), ApplyStatusToken(ApplyStatus::Ok));
}

void ApplyEngineTest::test10_ApplyStatusToken_validationFailed()
{
    using namespace sw::intelligent;
    CPPUNIT_ASSERT_EQUAL(OString("validation-failed"),
                         ApplyStatusToken(ApplyStatus::ValidationFailed));
}

void ApplyEngineTest::test11_ApplyStatusToken_staleSnapshot()
{
    using namespace sw::intelligent;
    CPPUNIT_ASSERT_EQUAL(OString("stale-snapshot"),
                         ApplyStatusToken(ApplyStatus::StaleSnapshot));
}

void ApplyEngineTest::test12_ApplyStatusToken_patchFailed()
{
    using namespace sw::intelligent;
    CPPUNIT_ASSERT_EQUAL(OString("patch-failed"),
                         ApplyStatusToken(ApplyStatus::PatchFailed));
}

void ApplyEngineTest::test13_ApplyStatusToken_undoException()
{
    using namespace sw::intelligent;
    CPPUNIT_ASSERT_EQUAL(OString("undo-exception"),
                         ApplyStatusToken(ApplyStatus::UndoException));
}

void ApplyEngineTest::test14_AllSevenKinds_distinctTokens()
{
    using namespace sw::intelligent;
    PatchKind kinds[] = {
        PatchKind::ParagraphReplace,     PatchKind::ParagraphInsertAfter,
        PatchKind::ParagraphDelete,      PatchKind::ParagraphFormat,
        PatchKind::ParagraphReformat,    PatchKind::TextRangeReplace,
        PatchKind::TextFormat,
    };
    OString tokens[7];
    for (int i = 0; i < 7; ++i)
        tokens[i] = PatchKindToken(kinds[i]);
    for (int i = 0; i < 7; ++i)
    {
        CPPUNIT_ASSERT(!tokens[i].isEmpty());
        for (int j = i + 1; j < 7; ++j)
            CPPUNIT_ASSERT_MESSAGE("kind tokens must be pairwise distinct",
                                   tokens[i] != tokens[j]);
    }
}

void ApplyEngineTest::test15_ComputeParagraphTextHash_empty_knownDigest()
{
    using namespace sw::intelligent;
    const OUString aHash = ComputeParagraphTextHash(u""_ustr);
    CPPUNIT_ASSERT(aHash.startsWith("sha256:"));
    CPPUNIT_ASSERT_EQUAL(
        OUString(u"sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"_ustr),
        aHash);
}

void ApplyEngineTest::test16_ComputeParagraphTextHash_nonEmpty_differsFromEmpty()
{
    using namespace sw::intelligent;
    const OUString aEmpty = ComputeParagraphTextHash(u""_ustr);
    const OUString aHello = ComputeParagraphTextHash(u"hello"_ustr);
    CPPUNIT_ASSERT(aHello.startsWith("sha256:"));
    CPPUNIT_ASSERT(aHello != aEmpty);
}

void ApplyEngineTest::test17_ParseParagraphId_validAndInvalid()
{
    using namespace sw::intelligent;
    sal_uInt32 nParagraph = 0;
    CPPUNIT_ASSERT(ParseParagraphId(u"swpara-1"_ustr, nParagraph));
    CPPUNIT_ASSERT_EQUAL(sal_uInt32(1), nParagraph);
    CPPUNIT_ASSERT(ParseParagraphId(u"swpara-42"_ustr, nParagraph));
    CPPUNIT_ASSERT_EQUAL(sal_uInt32(42), nParagraph);
    CPPUNIT_ASSERT(!ParseParagraphId(u"para-1"_ustr, nParagraph));
    CPPUNIT_ASSERT(!ParseParagraphId(u"swpara-0"_ustr, nParagraph));
    CPPUNIT_ASSERT(!ParseParagraphId(u"swpara-"_ustr, nParagraph));
    CPPUNIT_ASSERT(!ParseParagraphId(OUString(), nParagraph));
}

void ApplyEngineTest::test18_ComputeParagraphTextHash_empty_isDeleteAfterState()
{
    using namespace sw::intelligent;
    const OUString aEmpty = ComputeParagraphTextHash(u""_ustr);
    const OUString aDeletedAfter = ComputeParagraphTextHash(u""_ustr);
    CPPUNIT_ASSERT_EQUAL(aEmpty, aDeletedAfter);
    CPPUNIT_ASSERT(aEmpty.startsWith("sha256:"));
}

void ApplyEngineTest::test19_ComputeParagraphTextHash_insertAfterFixtureText()
{
    using namespace sw::intelligent;
    // apply-plan-runtime.writer-runtime.json p2 "after" payload
    const OUString aInsertText = u"下文将进一步分解此论点。"_ustr;
    const OUString aHash = ComputeParagraphTextHash(aInsertText);
    CPPUNIT_ASSERT(aHash.startsWith("sha256:"));
    CPPUNIT_ASSERT(aHash != ComputeParagraphTextHash(u""_ustr));
    CPPUNIT_ASSERT(aHash == ComputeParagraphTextHash(aInsertText));
}

void ApplyEngineTest::test20_ParseParagraphId_insertAfterNextParagraphIndex()
{
    using namespace sw::intelligent;
    sal_uInt32 nAnchor = 0;
    CPPUNIT_ASSERT(ParseParagraphId(u"swpara-42"_ustr, nAnchor));
    CPPUNIT_ASSERT_EQUAL(sal_uInt32(42), nAnchor);
    sal_uInt32 nInserted = nAnchor + 1;
    CPPUNIT_ASSERT_EQUAL(sal_uInt32(43), nInserted);
    CPPUNIT_ASSERT(ParseParagraphId(u"swpara-43"_ustr, nInserted));
    CPPUNIT_ASSERT_EQUAL(sal_uInt32(43), nInserted);
}

void ApplyEngineTest::test21_ValidateTextRangeReplacePatch_ok_fixtureRange()
{
    using namespace sw::intelligent;
    // writer-runtime p6 intent: typo 因该 → 应该 inside a longer paragraph (UTF-16 range)
    const OUString aPrefix = u"012345678901"_ustr; // 12 code units
    const OUString aParagraph = aPrefix + u"因该"_ustr + u"后文"_ustr;
    PatchRange aRange;
    aRange.mnStart = 12;
    aRange.mnLength = 2;
    CPPUNIT_ASSERT_EQUAL(OString("ok"),
                         ValidateTextRangeReplacePatch(aParagraph, aRange, u"因该"_ustr,
                                                       u"应该"_ustr));
}

void ApplyEngineTest::test22_ValidateTextRangeReplacePatch_outOfRangeAndIdempotent()
{
    using namespace sw::intelligent;
    const OUString aParagraph = u"hello"_ustr;
    PatchRange aRange;
    aRange.mnStart = 0;
    aRange.mnLength = 5;
    CPPUNIT_ASSERT_EQUAL(OString("failed"),
                         ValidateTextRangeReplacePatch(aParagraph, aRange, u"world"_ustr,
                                                       u"bye"_ustr));
    aRange.mnStart = 10;
    aRange.mnLength = 1;
    CPPUNIT_ASSERT_EQUAL(OString("failed"),
                         ValidateTextRangeReplacePatch(aParagraph, aRange, u"x"_ustr, u"y"_ustr));
    aRange.mnStart = 0;
    aRange.mnLength = 5;
    CPPUNIT_ASSERT_EQUAL(OString("skipped-idempotent"),
                         ValidateTextRangeReplacePatch(aParagraph, aRange, u"hello"_ustr,
                                                       u"hello"_ustr));
}

void ApplyEngineTest::test23_ParseFormatChangesStyle_fixtureHeading2()
{
    using namespace sw::intelligent;
    // apply-plan-runtime.writer-runtime.json p4 format_changes
    const std::optional<OUString> oStyle
        = ParseFormatChangesStyle(u"{\"style\": \"Heading 2\"}"_ustr);
    CPPUNIT_ASSERT(oStyle.has_value());
    CPPUNIT_ASSERT_EQUAL(u"Heading 2"_ustr, *oStyle);
    CPPUNIT_ASSERT_EQUAL(u"Heading 2"_ustr,
                         *ParseFormatChangesStyle(u"{\"style\":\"Heading 2\"}"_ustr));
}

void ApplyEngineTest::test24_ParseFormatChangesStyle_missingOrMalformed()
{
    using namespace sw::intelligent;
    CPPUNIT_ASSERT(!ParseFormatChangesStyle(u"{}"_ustr).has_value());
    CPPUNIT_ASSERT(!ParseFormatChangesStyle(u"{\"bold\":true}"_ustr).has_value());
    CPPUNIT_ASSERT(!ParseFormatChangesStyle(OUString()).has_value());
}

void ApplyEngineTest::test25_ParseFormatChangesReformat_fixtureP5()
{
    using namespace sw::intelligent;
    // apply-plan-runtime.writer-runtime.json p5 format_changes (twips + multiplier)
    const std::optional<ParagraphReformatAttrs> oAttrs = ParseFormatChangesReformat(
        u"{\"first_line_indent\": 24, \"line_spacing\": 1.5}"_ustr);
    CPPUNIT_ASSERT(oAttrs.has_value());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(24), oAttrs->mnFirstLineIndentTwips);
    CPPUNIT_ASSERT(oAttrs->mfLineSpacing.has_value());
    CPPUNIT_ASSERT_DOUBLES_EQUAL(1.5, *oAttrs->mfLineSpacing, 1e-9);

    const OUString aSerialized = SerializeFormatChangesReformat(*oAttrs);
    CPPUNIT_ASSERT(aSerialized.indexOf(u"\"first_line_indent\": 24"_ustr) >= 0);
    CPPUNIT_ASSERT(aSerialized.indexOf(u"\"line_spacing\": 1.5"_ustr) >= 0);
}

void ApplyEngineTest::test26_ParseFormatChangesReformat_missingFirstLineIndent()
{
    using namespace sw::intelligent;
    CPPUNIT_ASSERT(!ParseFormatChangesReformat(u"{}"_ustr).has_value());
    CPPUNIT_ASSERT(!ParseFormatChangesReformat(u"{\"line_spacing\": 1.5}"_ustr).has_value());
    CPPUNIT_ASSERT(!ParseFormatChangesReformat(OUString()).has_value());
}

void ApplyEngineTest::test27_ParseFormatChangesBold_fixtureP7()
{
    using namespace sw::intelligent;
    // apply-plan-runtime.writer-runtime.json p7 format_changes
    const std::optional<bool> oBold = ParseFormatChangesBold(u"{\"bold\": true}"_ustr);
    CPPUNIT_ASSERT(oBold.has_value());
    CPPUNIT_ASSERT(*oBold);
    CPPUNIT_ASSERT(*ParseFormatChangesBold(u"{\"bold\":true}"_ustr));
}

void ApplyEngineTest::test28_ValidateTextFormatPatch_rangeBounds()
{
    using namespace sw::intelligent;
    const OUString aParagraph = u"012345678901234567"_ustr; // 18 code units
    PatchRange aRange;
    aRange.mnStart = 5;
    aRange.mnLength = 6;
    CPPUNIT_ASSERT_EQUAL(OString("ok"),
                         ValidateTextFormatPatch(aParagraph, aRange, u"{\"bold\": true}"_ustr));
    aRange.mnStart = 15;
    aRange.mnLength = 5;
    CPPUNIT_ASSERT_EQUAL(OString("failed"),
                         ValidateTextFormatPatch(aParagraph, aRange, u"{\"bold\": true}"_ustr));
    CPPUNIT_ASSERT_EQUAL(OString("failed"),
                         ValidateTextFormatPatch(aParagraph, aRange, u"{\"bold\": false}"_ustr));
    CPPUNIT_ASSERT_EQUAL(OString("failed"),
                         ValidateTextFormatPatch(aParagraph, aRange, u"{}"_ustr));
}

void ApplyEngineTest::test29_ValidateApplyPlanShape_minimalReplaceAndDelete()
{
    using namespace sw::intelligent;
    ApplyPlan aPlan;
    aPlan.maSchemaVersion = u"v2-w3-runtime-1"_ustr;
    aPlan.maPlanId = u"ap-test-minimal-001"_ustr;
    aPlan.maSourceDiagnosticId = u"diag-test-001"_ustr;
    aPlan.maDocSnapshotHash
        = u"sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"_ustr;
    aPlan.mbPreviewOnly = false;

    Patch aReplace;
    aReplace.maPatchId = u"p1"_ustr;
    aReplace.meKind = PatchKind::ParagraphReplace;
    aReplace.maTarget.maParagraphId = u"swpara-1"_ustr;
    aReplace.maSeverity = u"minor"_ustr;
    aReplace.maRationale = u"replace token"_ustr;
    aReplace.maBefore = u"old"_ustr;
    aReplace.maAfter = u"new"_ustr;

    Patch aDelete;
    aDelete.maPatchId = u"p2"_ustr;
    aDelete.meKind = PatchKind::ParagraphDelete;
    aDelete.maTarget.maParagraphId = u"swpara-2"_ustr;
    aDelete.maSeverity = u"normal"_ustr;
    aDelete.maRationale = u"delete token"_ustr;

    aPlan.maPatches.push_back(aReplace);
    aPlan.maPatches.push_back(aDelete);

    OUString aError;
    CPPUNIT_ASSERT(ValidateApplyPlanShape(aPlan, aError));
    CPPUNIT_ASSERT(aError.isEmpty());

    ApplyPlan aBad = aPlan;
    aBad.maPlanId.clear();
    CPPUNIT_ASSERT(!ValidateApplyPlanShape(aBad, aError));
    CPPUNIT_ASSERT(!aError.isEmpty());

    aBad = aPlan;
    aBad.maSchemaVersion = u"v1-wrong"_ustr;
    CPPUNIT_ASSERT(!ValidateApplyPlanShape(aBad, aError));
}

void ApplyEngineTest::test30_ApplyStatusToken_roundtripAllFiveModes()
{
    using namespace sw::intelligent;
    const ApplyStatus aStatuses[] = {
        ApplyStatus::Ok,
        ApplyStatus::ValidationFailed,
        ApplyStatus::StaleSnapshot,
        ApplyStatus::PatchFailed,
        ApplyStatus::UndoException,
    };
    for (ApplyStatus eStatus : aStatuses)
    {
        const OString aTok = ApplyStatusToken(eStatus);
        CPPUNIT_ASSERT(!aTok.isEmpty());
        const std::optional<ApplyStatus> oBack = ApplyStatusFromToken(aTok);
        CPPUNIT_ASSERT(oBack.has_value());
        CPPUNIT_ASSERT_EQUAL(eStatus, *oBack);
    }
    CPPUNIT_ASSERT(!ApplyStatusFromToken("not-a-status").has_value());
    CPPUNIT_ASSERT(!ApplyStatusFromToken("").has_value());
    CPPUNIT_ASSERT(!ApplyStatusFromToken("patch-failed-extra").has_value());
}

void ApplyEngineTest::test31_ValidateApplyPlanShape_singleParagraphReplace_inlineContract()
{
    using namespace sw::intelligent;
    // Shape contract for BuildSingleParagraphReplacePlan (mock snapshot hash, no SwDoc).
    ApplyPlan aPlan;
    aPlan.maSchemaVersion = u"v2-w3-runtime-1"_ustr;
    aPlan.maPlanId = u"ap-inline-iar-00000001"_ustr;
    aPlan.maSourceDiagnosticId = u"diag-inline-iar-00000001"_ustr;
    aPlan.maDocSnapshotHash
        = u"sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"_ustr;
    aPlan.mbPreviewOnly = false;

    Patch aPatch;
    aPatch.maPatchId = u"p1"_ustr;
    aPatch.meKind = PatchKind::ParagraphReplace;
    aPatch.maTarget.maParagraphId = u"swpara-1"_ustr;
    aPatch.maTarget.maTextHash
        = u"sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"_ustr;
    aPatch.maSeverity = u"minor"_ustr;
    aPatch.maRationale = u"inline provider paragraph-replace"_ustr;
    aPatch.maBefore = u"before text"_ustr;
    aPatch.maAfter = u"after text"_ustr;
    aPlan.maPatches.push_back(aPatch);

    OUString aError;
    CPPUNIT_ASSERT(ValidateApplyPlanShape(aPlan, aError));
    CPPUNIT_ASSERT(aError.isEmpty());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), sal_Int32(aPlan.maPatches.size()));
    CPPUNIT_ASSERT(aPlan.maPatches[0].maBefore.has_value());
    CPPUNIT_ASSERT(aPlan.maPatches[0].maAfter.has_value());
    CPPUNIT_ASSERT_EQUAL(u"after text"_ustr, *aPlan.maPatches[0].maAfter);
}

void ApplyEngineTest::test32_ValidateApplyPlanShape_singleParagraphReplace_missingPatchIdFails()
{
    using namespace sw::intelligent;
    ApplyPlan aPlan;
    aPlan.maSchemaVersion = u"v2-w3-runtime-1"_ustr;
    aPlan.maPlanId = u"ap-inline-bad"_ustr;
    aPlan.maSourceDiagnosticId = u"diag-inline-bad"_ustr;
    aPlan.maDocSnapshotHash
        = u"sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"_ustr;

    Patch aPatch;
    aPatch.meKind = PatchKind::ParagraphReplace;
    aPatch.maTarget.maParagraphId = u"swpara-1"_ustr;
    aPatch.maSeverity = u"minor"_ustr;
    aPatch.maBefore = u"old"_ustr;
    aPatch.maAfter = u"new"_ustr;
    aPlan.maPatches.push_back(aPatch);

    OUString aError;
    CPPUNIT_ASSERT(!ValidateApplyPlanShape(aPlan, aError));
    CPPUNIT_ASSERT(aError.indexOf(u"patch_id"_ustr) >= 0);
}

void ApplyEngineTest::test33_ParseApplyPlanRuntimeJson_twoPatchReplaceAndDelete()
{
    using namespace sw::intelligent;
    const OUString aJson = uR"({
  "schema_version": "v2-w3-runtime-1",
  "plan_id": "ap-test-parse-001",
  "source_diagnostic_id": "diag-test-parse-001",
  "doc_snapshot_hash": "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
  "preview_only": false,
  "patches": [
    {
      "patch_id": "p1",
      "kind": "paragraph-replace",
      "target": {"paragraph_id": "swpara-1"},
      "severity": "minor",
      "rationale": "replace token",
      "before": "old",
      "after": "new"
    },
    {
      "patch_id": "p2",
      "kind": "paragraph-delete",
      "target": {"paragraph_id": "swpara-2"},
      "severity": "normal",
      "rationale": "delete token"
    }
  ]
})"_ustr;

    const std::optional<ApplyPlan> oPlan = ParseApplyPlanRuntimeJson(aJson);
    CPPUNIT_ASSERT_MESSAGE("two-patch envelope should parse", oPlan.has_value());
    CPPUNIT_ASSERT_EQUAL(u"ap-test-parse-001"_ustr, oPlan->maPlanId);
    CPPUNIT_ASSERT_EQUAL(u"diag-test-parse-001"_ustr, oPlan->maSourceDiagnosticId);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), sal_Int32(oPlan->maPatches.size()));
    CPPUNIT_ASSERT_EQUAL(u"p1"_ustr, oPlan->maPatches[0].maPatchId);
    CPPUNIT_ASSERT(oPlan->maPatches[0].meKind == PatchKind::ParagraphReplace);
    CPPUNIT_ASSERT_EQUAL(u"swpara-1"_ustr, oPlan->maPatches[0].maTarget.maParagraphId);
    CPPUNIT_ASSERT(oPlan->maPatches[0].maBefore.has_value());
    CPPUNIT_ASSERT(oPlan->maPatches[0].maAfter.has_value());
    CPPUNIT_ASSERT_EQUAL(u"p2"_ustr, oPlan->maPatches[1].maPatchId);
    CPPUNIT_ASSERT(oPlan->maPatches[1].meKind == PatchKind::ParagraphDelete);
    CPPUNIT_ASSERT_EQUAL(u"swpara-2"_ustr, oPlan->maPatches[1].maTarget.maParagraphId);

    OUString aError;
    CPPUNIT_ASSERT(ValidateApplyPlanShape(*oPlan, aError));
}

void ApplyEngineTest::test34_ParseApplyPlanRuntimeJson_invalidKindReturnsNullopt()
{
    using namespace sw::intelligent;
    const OUString aJson = uR"({
  "schema_version": "v2-w3-runtime-1",
  "plan_id": "ap-test-bad-kind",
  "source_diagnostic_id": "diag-test-bad-kind",
  "doc_snapshot_hash": "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
  "preview_only": false,
  "patches": [
    {
      "patch_id": "p1",
      "kind": "table-cell-replace",
      "target": {"paragraph_id": "swpara-1"},
      "severity": "minor",
      "rationale": "deferred kind"
    }
  ]
})"_ustr;

    CPPUNIT_ASSERT(!ParseApplyPlanRuntimeJson(aJson).has_value());
}

void ApplyEngineTest::test35_ParseApplyPlanRuntimeJson_writerRuntimeFixture()
{
    using namespace sw::intelligent;
    // W3 E2E parse gate: 3-patch excerpt from
    // docs/schemas/fixtures/apply-plan-runtime.writer-runtime.json (p1–p3).
    const OUString aJson = uR"({
  "schema_version": "v2-w3-runtime-1",
  "plan_id": "ap-20260512-w3day1b-d1-runtime-001",
  "source_diagnostic_id": "diag-20260512-w3day1b-runtime-007",
  "doc_snapshot_hash": "sha256:abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",
  "preview_only": false,
  "patches": [
    {
      "patch_id": "p1",
      "kind": "paragraph-replace",
      "target": {"paragraph_id": "swpara-42", "text_hash": "sha256:1111111111111111111111111111111111111111111111111111111111111111"},
      "severity": "minor",
      "rationale": "本段冗余，去除重复表述",
      "before": "本段表述太啰嗦，重复说了好几遍。",
      "after": "本段表述简洁，无重复。"
    },
    {
      "patch_id": "p2",
      "kind": "paragraph-insert-after",
      "target": {"paragraph_id": "swpara-42"},
      "severity": "minor",
      "rationale": "前段后补充承接句",
      "after": "下文将进一步分解此论点。"
    },
    {
      "patch_id": "p3",
      "kind": "paragraph-delete",
      "target": {"paragraph_id": "swpara-43"},
      "severity": "normal",
      "rationale": "重复段落，删除以避免冗余"
    }
  ]
})"_ustr;

    const std::optional<ApplyPlan> oPlan = ParseApplyPlanRuntimeJson(aJson);
    CPPUNIT_ASSERT_MESSAGE("writer-runtime 3-patch excerpt should parse", oPlan.has_value());
    CPPUNIT_ASSERT_EQUAL(u"v2-w3-runtime-1"_ustr, oPlan->maSchemaVersion);
    CPPUNIT_ASSERT_EQUAL(u"ap-20260512-w3day1b-d1-runtime-001"_ustr, oPlan->maPlanId);
    CPPUNIT_ASSERT_EQUAL(u"diag-20260512-w3day1b-runtime-007"_ustr, oPlan->maSourceDiagnosticId);
    CPPUNIT_ASSERT_EQUAL(
        u"sha256:abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789"_ustr,
        oPlan->maDocSnapshotHash);
    CPPUNIT_ASSERT(!oPlan->mbPreviewOnly);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(3), sal_Int32(oPlan->maPatches.size()));

    CPPUNIT_ASSERT_EQUAL(u"p1"_ustr, oPlan->maPatches[0].maPatchId);
    CPPUNIT_ASSERT(oPlan->maPatches[0].meKind == PatchKind::ParagraphReplace);
    CPPUNIT_ASSERT_EQUAL(u"swpara-42"_ustr, oPlan->maPatches[0].maTarget.maParagraphId);
    CPPUNIT_ASSERT(oPlan->maPatches[0].maTarget.maTextHash.has_value());
    CPPUNIT_ASSERT(oPlan->maPatches[0].maBefore.has_value());
    CPPUNIT_ASSERT(oPlan->maPatches[0].maAfter.has_value());
    CPPUNIT_ASSERT_EQUAL(u"本段表述太啰嗦，重复说了好几遍。"_ustr, *oPlan->maPatches[0].maBefore);
    CPPUNIT_ASSERT_EQUAL(u"本段表述简洁，无重复。"_ustr, *oPlan->maPatches[0].maAfter);

    CPPUNIT_ASSERT_EQUAL(u"p2"_ustr, oPlan->maPatches[1].maPatchId);
    CPPUNIT_ASSERT(oPlan->maPatches[1].meKind == PatchKind::ParagraphInsertAfter);
    CPPUNIT_ASSERT_EQUAL(u"swpara-42"_ustr, oPlan->maPatches[1].maTarget.maParagraphId);
    CPPUNIT_ASSERT(!oPlan->maPatches[1].maBefore.has_value());
    CPPUNIT_ASSERT(oPlan->maPatches[1].maAfter.has_value());
    CPPUNIT_ASSERT_EQUAL(u"下文将进一步分解此论点。"_ustr, *oPlan->maPatches[1].maAfter);

    CPPUNIT_ASSERT_EQUAL(u"p3"_ustr, oPlan->maPatches[2].maPatchId);
    CPPUNIT_ASSERT(oPlan->maPatches[2].meKind == PatchKind::ParagraphDelete);
    CPPUNIT_ASSERT_EQUAL(u"swpara-43"_ustr, oPlan->maPatches[2].maTarget.maParagraphId);
    CPPUNIT_ASSERT(!oPlan->maPatches[2].maBefore.has_value());
    CPPUNIT_ASSERT(!oPlan->maPatches[2].maAfter.has_value());

    OUString aError;
    CPPUNIT_ASSERT(ValidateApplyPlanShape(*oPlan, aError));
    CPPUNIT_ASSERT(aError.isEmpty());
}

CPPUNIT_TEST_SUITE_REGISTRATION(ApplyEngineTest);
} // namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
