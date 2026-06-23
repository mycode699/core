/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-B: Select-to-Act Calc).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Day-2 inline-action-request envelope shape (pure logic, no VCL).
 */

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <rtl/ustring.hxx>

#include "InlineActionRequest.hxx"

using sc::inline_actions::buildCalcCellRequest;

namespace
{
class InlineActionRequestCalc : public CppUnit::TestFixture
{
public:
    void testCalcCellEnvelope();

    CPPUNIT_TEST_SUITE(InlineActionRequestCalc);
    CPPUNIT_TEST(testCalcCellEnvelope);
    CPPUNIT_TEST_SUITE_END();
};

void InlineActionRequestCalc::testCalcCellEnvelope()
{
    const OUString aJson = buildCalcCellRequest(u"suggest-chart"_ustr, u"Q2-销售"_ustr,
                                                u"B2:E15"_ustr, u"private"_ustr);
    CPPUNIT_ASSERT(aJson.indexOf(u"\"schema_version\":\"v2-w4-1\""_ustr) >= 0);
    CPPUNIT_ASSERT(aJson.indexOf(u"\"surface\":\"calc-cell\""_ustr) >= 0);
    CPPUNIT_ASSERT(aJson.indexOf(u"\"request_id\":\"iar-"_ustr) >= 0);
    CPPUNIT_ASSERT(aJson.indexOf(u"\"sheet\":\"Q2-销售\""_ustr) >= 0);
    CPPUNIT_ASSERT(aJson.indexOf(u"\"range\":\"B2:E15\""_ustr) >= 0);
    CPPUNIT_ASSERT(aJson.indexOf(u"\"expected_capability\":\"suggest-chart\""_ustr) >= 0);
}

CPPUNIT_TEST_SUITE_REGISTRATION(InlineActionRequestCalc);

} // namespace

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */