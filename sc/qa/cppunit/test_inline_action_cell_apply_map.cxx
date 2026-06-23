/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-B: Select-to-Act Calc).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Day-5 W4-B: CellAction → shouldApplyProviderContentToMarkedCell (pure logic).
 */

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include "InlineActionCellApply.hxx"

using sc::inline_actions::CellAction;
using sc::inline_actions::shouldApplyProviderContentToMarkedCell;

namespace
{

class InlineActionCellApplyMapCalc : public CppUnit::TestFixture
{
public:
    void testCellActionShouldApplyMap();

    CPPUNIT_TEST_SUITE(InlineActionCellApplyMapCalc);
    CPPUNIT_TEST(testCellActionShouldApplyMap);
    CPPUNIT_TEST_SUITE_END();
};

void InlineActionCellApplyMapCalc::testCellActionShouldApplyMap()
{
    CPPUNIT_ASSERT(shouldApplyProviderContentToMarkedCell(CellAction::GenerateFormula));
    CPPUNIT_ASSERT(!shouldApplyProviderContentToMarkedCell(CellAction::ExplainData));
    CPPUNIT_ASSERT(!shouldApplyProviderContentToMarkedCell(CellAction::SuggestChart));
    CPPUNIT_ASSERT(!shouldApplyProviderContentToMarkedCell(CellAction::FormatClean));
    CPPUNIT_ASSERT(!shouldApplyProviderContentToMarkedCell(CellAction::FormatChange));
}

CPPUNIT_TEST_SUITE_REGISTRATION(InlineActionCellApplyMapCalc);

} // namespace

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */