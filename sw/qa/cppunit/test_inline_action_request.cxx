/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-A: Select-to-Act Writer).
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

using sw::inline_actions::buildWriterParagraphRequest;

namespace
{
bool lcl_requestIdMatchesPattern(const OUString& rJson)
{
    const OUString aPrefix = u"\"request_id\":\"iar-"_ustr;
    const sal_Int32 nPos = rJson.indexOf(aPrefix);
    if (nPos < 0)
        return false;
    const sal_Int32 nHexStart = nPos + aPrefix.getLength();
    if (rJson.getLength() < nHexStart + 17)
        return false;
    for (sal_Int32 i = nHexStart; i < nHexStart + 16; ++i)
    {
        const sal_Unicode c = rJson[i];
        const bool bHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!bHex)
            return false;
    }
    return rJson[nHexStart + 16] == '"';
}

class InlineActionRequestWriter : public CppUnit::TestFixture
{
public:
    void testEnvelopeRequiredFields();
    void testRewriteTargetAndExpectedCapability();

    CPPUNIT_TEST_SUITE(InlineActionRequestWriter);
    CPPUNIT_TEST(testEnvelopeRequiredFields);
    CPPUNIT_TEST(testRewriteTargetAndExpectedCapability);
    CPPUNIT_TEST_SUITE_END();
};

void InlineActionRequestWriter::testEnvelopeRequiredFields()
{
    const OUString aJson
        = buildWriterParagraphRequest(u"explain"_ustr, u"swpara-1"_ustr, u"offline"_ustr);
    CPPUNIT_ASSERT(aJson.indexOf(u"\"schema_version\":\"v2-w4-1\""_ustr) >= 0);
    CPPUNIT_ASSERT(aJson.indexOf(u"\"surface\":\"writer-paragraph\""_ustr) >= 0);
    CPPUNIT_ASSERT(lcl_requestIdMatchesPattern(aJson));
    CPPUNIT_ASSERT(aJson.indexOf(u"\"created_at\":"_ustr) >= 0);
}

void InlineActionRequestWriter::testRewriteTargetAndExpectedCapability()
{
    const OUString aJson
        = buildWriterParagraphRequest(u"rewrite"_ustr, u"swpara-42"_ustr, u"offline"_ustr);
    CPPUNIT_ASSERT(aJson.indexOf(u"\"action\":\"rewrite\""_ustr) >= 0);
    CPPUNIT_ASSERT(aJson.indexOf(u"\"paragraph_id\":\"swpara-42\""_ustr) >= 0);
    CPPUNIT_ASSERT(aJson.indexOf(u"\"expected_capability\":\"rewrite\""_ustr) >= 0);
}

CPPUNIT_TEST_SUITE_REGISTRATION(InlineActionRequestWriter);

} // namespace

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */