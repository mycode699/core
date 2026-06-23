/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-C: Select-to-Act Impress).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Day-0 enum-stability cppunit. Pure logic — no VCL, no URE, no Provider.
 *
 * Locks the W4-C token table at docs/product/v2/w4-select-to-act-spec.md
 * L269-L276. All four W4-C tokens go through Diff Review.
 */

#include <sal/types.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/plugin/TestPlugIn.h>

#include <rtl/ustring.hxx>

#include "SlideElementActions.hxx"

using sd::inline_actions::SlideElementAction;
using sd::inline_actions::toToken;
using sd::inline_actions::fromToken;

namespace
{
class SlideElementActionEnumStable : public CppUnit::TestFixture
{
public:
    void testTokenSpelling();
    void testRoundTrip();
    void testUnknownFallsBackToRewriteText();

    CPPUNIT_TEST_SUITE(SlideElementActionEnumStable);
    CPPUNIT_TEST(testTokenSpelling);
    CPPUNIT_TEST(testRoundTrip);
    CPPUNIT_TEST(testUnknownFallsBackToRewriteText);
    CPPUNIT_TEST_SUITE_END();
};

void SlideElementActionEnumStable::testTokenSpelling()
{
    CPPUNIT_ASSERT_EQUAL(u"rewrite-text"_ustr,   toToken(SlideElementAction::RewriteText));
    CPPUNIT_ASSERT_EQUAL(u"adjust-color"_ustr,   toToken(SlideElementAction::AdjustColor));
    CPPUNIT_ASSERT_EQUAL(u"relayout"_ustr,       toToken(SlideElementAction::Relayout));
    CPPUNIT_ASSERT_EQUAL(u"translate-text"_ustr, toToken(SlideElementAction::TranslateText));
}

void SlideElementActionEnumStable::testRoundTrip()
{
    const SlideElementAction aAll[] = {
        SlideElementAction::RewriteText,
        SlideElementAction::AdjustColor,
        SlideElementAction::Relayout,
        SlideElementAction::TranslateText,
    };
    for (SlideElementAction e : aAll)
        CPPUNIT_ASSERT_EQUAL(e, fromToken(toToken(e)));
}

void SlideElementActionEnumStable::testUnknownFallsBackToRewriteText()
{
    // Unknown tokens MUST land in RewriteText (the most conservative
    // Diff-bound default). Diff Review still gates the apply step, so a
    // bad route is still recoverable.
    CPPUNIT_ASSERT_EQUAL(SlideElementAction::RewriteText, fromToken(rtl::OUString()));
    CPPUNIT_ASSERT_EQUAL(SlideElementAction::RewriteText, fromToken(u"REWRITE-TEXT"_ustr));   // case-sensitive
    CPPUNIT_ASSERT_EQUAL(SlideElementAction::RewriteText, fromToken(u"rewrite"_ustr));        // W4-A token
    CPPUNIT_ASSERT_EQUAL(SlideElementAction::RewriteText, fromToken(u"translate-en"_ustr));   // W4-A token
    CPPUNIT_ASSERT_EQUAL(SlideElementAction::RewriteText, fromToken(u"  relayout  "_ustr));   // no trim
    CPPUNIT_ASSERT_EQUAL(SlideElementAction::RewriteText, fromToken(u"\u4e2d\u6587"_ustr));   // non-ASCII
}

CPPUNIT_TEST_SUITE_REGISTRATION(SlideElementActionEnumStable);

} // namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
