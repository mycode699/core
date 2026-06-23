/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-B: Select-to-Act Calc).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Day-0 enum-stability cppunit. Pure logic — no VCL, no URE, no Provider.
 *
 * Locks the W4-B token table at docs/product/v2/w4-select-to-act-spec.md
 * L259-L267. format-clean is intentionally shared with W4-A (same literal).
 */

#include <sal/types.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/plugin/TestPlugIn.h>

#include <rtl/ustring.hxx>

#include "CellActions.hxx"

using sc::inline_actions::CellAction;
using sc::inline_actions::toToken;
using sc::inline_actions::fromToken;

namespace
{
class CellActionEnumStable : public CppUnit::TestFixture
{
public:
    void testTokenSpelling();
    void testRoundTrip();
    void testUnknownFallsBackToExplainData();

    CPPUNIT_TEST_SUITE(CellActionEnumStable);
    CPPUNIT_TEST(testTokenSpelling);
    CPPUNIT_TEST(testRoundTrip);
    CPPUNIT_TEST(testUnknownFallsBackToExplainData);
    CPPUNIT_TEST_SUITE_END();
};

void CellActionEnumStable::testTokenSpelling()
{
    CPPUNIT_ASSERT_EQUAL(u"explain-data"_ustr,     toToken(CellAction::ExplainData));
    CPPUNIT_ASSERT_EQUAL(u"suggest-chart"_ustr,    toToken(CellAction::SuggestChart));
    CPPUNIT_ASSERT_EQUAL(u"generate-formula"_ustr, toToken(CellAction::GenerateFormula));
    CPPUNIT_ASSERT_EQUAL(u"format-clean"_ustr,     toToken(CellAction::FormatClean));
    CPPUNIT_ASSERT_EQUAL(u"format-change"_ustr,    toToken(CellAction::FormatChange));
}

void CellActionEnumStable::testRoundTrip()
{
    const CellAction aAll[] = {
        CellAction::ExplainData,
        CellAction::SuggestChart,
        CellAction::GenerateFormula,
        CellAction::FormatClean,
        CellAction::FormatChange,
    };
    for (CellAction e : aAll)
        CPPUNIT_ASSERT_EQUAL(e, fromToken(toToken(e)));
}

void CellActionEnumStable::testUnknownFallsBackToExplainData()
{
    // Unknown tokens MUST land in ExplainData (popup-only, no ApplyPlan)
    // so they cannot silently route through Diff Review.
    CPPUNIT_ASSERT_EQUAL(CellAction::ExplainData, fromToken(rtl::OUString()));
    CPPUNIT_ASSERT_EQUAL(CellAction::ExplainData, fromToken(u"EXPLAIN-DATA"_ustr));     // case-sensitive
    CPPUNIT_ASSERT_EQUAL(CellAction::ExplainData, fromToken(u"explain"_ustr));          // W4-A token, not W4-B
    CPPUNIT_ASSERT_EQUAL(CellAction::ExplainData, fromToken(u"rewrite-text"_ustr));     // W4-C token, not W4-B
    CPPUNIT_ASSERT_EQUAL(CellAction::ExplainData, fromToken(u"  format-clean  "_ustr)); // no trim
    CPPUNIT_ASSERT_EQUAL(CellAction::ExplainData, fromToken(u"\u4e2d\u6587"_ustr));     // non-ASCII
}

CPPUNIT_TEST_SUITE_REGISTRATION(CellActionEnumStable);

} // namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
