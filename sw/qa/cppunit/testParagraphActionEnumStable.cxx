/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-A: Select-to-Act Writer).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Day-0 enum-stability cppunit. Pure logic — no VCL, no URE, no Provider.
 *
 * Locks the W4-A token table at docs/product/v2/w4-select-to-act-spec.md
 * L249-L258. Any rename / addition / reorder must update this fixture
 * and the apply-plan-runtime schema enum simultaneously.
 */

#include <sal/types.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/plugin/TestPlugIn.h>

#include <rtl/ustring.hxx>

#include "ParagraphActions.hxx"

using sw::inline_actions::ParagraphAction;
using sw::inline_actions::toToken;
using sw::inline_actions::fromToken;

namespace
{
class ParagraphActionEnumStable : public CppUnit::TestFixture
{
public:
    void testTokenSpelling();
    void testRoundTrip();
    void testUnknownFallsBackToCustom();

    CPPUNIT_TEST_SUITE(ParagraphActionEnumStable);
    CPPUNIT_TEST(testTokenSpelling);
    CPPUNIT_TEST(testRoundTrip);
    CPPUNIT_TEST(testUnknownFallsBackToCustom);
    CPPUNIT_TEST_SUITE_END();
};

void ParagraphActionEnumStable::testTokenSpelling()
{
    // Lock literal spellings — these strings cross into the JSON schema
    // capability enum and must not drift.
    CPPUNIT_ASSERT_EQUAL(u"rewrite"_ustr,      toToken(ParagraphAction::Rewrite));
    CPPUNIT_ASSERT_EQUAL(u"expand"_ustr,       toToken(ParagraphAction::Expand));
    CPPUNIT_ASSERT_EQUAL(u"shorten"_ustr,      toToken(ParagraphAction::Shorten));
    CPPUNIT_ASSERT_EQUAL(u"translate-en"_ustr, toToken(ParagraphAction::TranslateEn));
    CPPUNIT_ASSERT_EQUAL(u"format-clean"_ustr, toToken(ParagraphAction::FormatClean));
    CPPUNIT_ASSERT_EQUAL(u"explain"_ustr,      toToken(ParagraphAction::Explain));
    CPPUNIT_ASSERT_EQUAL(u"custom"_ustr,       toToken(ParagraphAction::Custom));
}

void ParagraphActionEnumStable::testRoundTrip()
{
    const ParagraphAction aAll[] = {
        ParagraphAction::Rewrite,
        ParagraphAction::Expand,
        ParagraphAction::Shorten,
        ParagraphAction::TranslateEn,
        ParagraphAction::FormatClean,
        ParagraphAction::Explain,
        ParagraphAction::Custom,
    };
    for (ParagraphAction e : aAll)
        CPPUNIT_ASSERT_EQUAL(e, fromToken(toToken(e)));
}

void ParagraphActionEnumStable::testUnknownFallsBackToCustom()
{
    // Unknown tokens MUST land in Custom (free-form / user_prompt route)
    // so they cannot silently flip into a Diff-bound capability.
    CPPUNIT_ASSERT_EQUAL(ParagraphAction::Custom, fromToken(rtl::OUString()));
    CPPUNIT_ASSERT_EQUAL(ParagraphAction::Custom, fromToken(u"REWRITE"_ustr));         // case-sensitive
    CPPUNIT_ASSERT_EQUAL(ParagraphAction::Custom, fromToken(u"translate"_ustr));        // missing -en
    CPPUNIT_ASSERT_EQUAL(ParagraphAction::Custom, fromToken(u"explain-data"_ustr));     // W4-B token, not W4-A
    CPPUNIT_ASSERT_EQUAL(ParagraphAction::Custom, fromToken(u"  rewrite  "_ustr));      // no trim
    CPPUNIT_ASSERT_EQUAL(ParagraphAction::Custom, fromToken(u"\u4e2d\u6587"_ustr));     // non-ASCII
}

CPPUNIT_TEST_SUITE_REGISTRATION(ParagraphActionEnumStable);

} // namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
