/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-C: Select-to-Act Impress).
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

using sd::inline_actions::SlideElementAction;
using sd::inline_actions::offlineCapabilityForSlideElementAction;

namespace
{

class InlineActionProviderMapImpress : public CppUnit::TestFixture
{
public:
    void testSlideElementActionCapabilityMap();

    CPPUNIT_TEST_SUITE(InlineActionProviderMapImpress);
    CPPUNIT_TEST(testSlideElementActionCapabilityMap);
    CPPUNIT_TEST_SUITE_END();
};

void InlineActionProviderMapImpress::testSlideElementActionCapabilityMap()
{
    CPPUNIT_ASSERT_EQUAL(u"rewrite"_ustr,
                         offlineCapabilityForSlideElementAction(SlideElementAction::RewriteText));
    CPPUNIT_ASSERT_EQUAL(u"format-fix"_ustr,
                         offlineCapabilityForSlideElementAction(SlideElementAction::AdjustColor));
    CPPUNIT_ASSERT_EQUAL(u"format-fix"_ustr,
                         offlineCapabilityForSlideElementAction(SlideElementAction::Relayout));
    CPPUNIT_ASSERT_EQUAL(u"rewrite"_ustr,
                         offlineCapabilityForSlideElementAction(SlideElementAction::TranslateText));
}

CPPUNIT_TEST_SUITE_REGISTRATION(InlineActionProviderMapImpress);

} // namespace

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */