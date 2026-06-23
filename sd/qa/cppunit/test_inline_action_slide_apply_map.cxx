/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-C: Select-to-Act Impress).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Day-5 W4-C: SlideElementAction → shouldApplyProviderContentToMarkedTextShape (pure logic).
 */

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include "InlineActionSlideApply.hxx"

using sd::inline_actions::SlideElementAction;
using sd::inline_actions::shouldApplyProviderContentToMarkedTextShape;

namespace
{

class InlineActionSlideApplyMapImpress : public CppUnit::TestFixture
{
public:
    void testSlideElementActionShouldApplyMap();

    CPPUNIT_TEST_SUITE(InlineActionSlideApplyMapImpress);
    CPPUNIT_TEST(testSlideElementActionShouldApplyMap);
    CPPUNIT_TEST_SUITE_END();
};

void InlineActionSlideApplyMapImpress::testSlideElementActionShouldApplyMap()
{
    CPPUNIT_ASSERT(shouldApplyProviderContentToMarkedTextShape(SlideElementAction::RewriteText));
    CPPUNIT_ASSERT(
        shouldApplyProviderContentToMarkedTextShape(SlideElementAction::TranslateText));
    CPPUNIT_ASSERT(!shouldApplyProviderContentToMarkedTextShape(SlideElementAction::AdjustColor));
    CPPUNIT_ASSERT(!shouldApplyProviderContentToMarkedTextShape(SlideElementAction::Relayout));
}

CPPUNIT_TEST_SUITE_REGISTRATION(InlineActionSlideApplyMapImpress);

} // namespace

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */