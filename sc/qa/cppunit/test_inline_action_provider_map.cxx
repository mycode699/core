/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-B: Select-to-Act Calc).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Day-3 W4 action token → W1 offline capability map (pure logic).
 */

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <rtl/ustring.hxx>

#include "InlineActionProviderDispatch.hxx"

using sc::inline_actions::CellAction;
using sc::inline_actions::offlineCapabilityForCellAction;

namespace
{

class InlineActionProviderMapCalc : public CppUnit::TestFixture
{
public:
    void testCellActionCapabilityMap();

    CPPUNIT_TEST_SUITE(InlineActionProviderMapCalc);
    CPPUNIT_TEST(testCellActionCapabilityMap);
    CPPUNIT_TEST_SUITE_END();
};

void InlineActionProviderMapCalc::testCellActionCapabilityMap()
{
    CPPUNIT_ASSERT_EQUAL(u"summarize"_ustr,
                         offlineCapabilityForCellAction(CellAction::ExplainData));
    CPPUNIT_ASSERT_EQUAL(u"intent-to-uno"_ustr,
                         offlineCapabilityForCellAction(CellAction::SuggestChart));
    CPPUNIT_ASSERT_EQUAL(u"intent-to-uno"_ustr,
                         offlineCapabilityForCellAction(CellAction::GenerateFormula));
    CPPUNIT_ASSERT_EQUAL(u"format-fix"_ustr,
                         offlineCapabilityForCellAction(CellAction::FormatClean));
    CPPUNIT_ASSERT_EQUAL(u"format-fix"_ustr,
                         offlineCapabilityForCellAction(CellAction::FormatChange));
}

CPPUNIT_TEST_SUITE_REGISTRATION(InlineActionProviderMapCalc);

} // namespace

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */