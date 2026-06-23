/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-A: Select-to-Act Writer).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Pure cppunit for swpara-N formatting (no UNO / VCL).
 */

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <rtl/ustring.hxx>

#include "WriterBodyTextParagraphId.hxx"

using sw::inline_actions::formatBodyTextParagraphId;

namespace
{
class WriterParagraphIdFormat : public CppUnit::TestFixture
{
public:
    void testFormatBodyTextParagraphId();

    CPPUNIT_TEST_SUITE(WriterParagraphIdFormat);
    CPPUNIT_TEST(testFormatBodyTextParagraphId);
    CPPUNIT_TEST_SUITE_END();
};

void WriterParagraphIdFormat::testFormatBodyTextParagraphId()
{
    CPPUNIT_ASSERT_EQUAL(u"swpara-1"_ustr, formatBodyTextParagraphId(1));
    CPPUNIT_ASSERT_EQUAL(u"swpara-42"_ustr, formatBodyTextParagraphId(42));
}

CPPUNIT_TEST_SUITE_REGISTRATION(WriterParagraphIdFormat);

} // namespace

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */