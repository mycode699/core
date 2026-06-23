/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4 Day-3: Writer → Provider map).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Pure map tests — no UNO / VCL.
 */

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <rtl/ustring.hxx>

#include "InlineActionProviderDispatch.hxx"

using sw::inline_actions::mapWriterActionToProviderCapability;
using sw::inline_actions::buildWriterRuntimeJsonPromptForProvider;

namespace
{

class ProviderCapabilityMapWriter : public CppUnit::TestFixture
{
public:
    void testRewriteMapsToRewrite();
    void testFormatCleanMapsToFormatFix();
    void testShortenMapsToSummarize();
    void testRuntimePromptLocksW3JsonContract();

    CPPUNIT_TEST_SUITE(ProviderCapabilityMapWriter);
    CPPUNIT_TEST(testRewriteMapsToRewrite);
    CPPUNIT_TEST(testFormatCleanMapsToFormatFix);
    CPPUNIT_TEST(testShortenMapsToSummarize);
    CPPUNIT_TEST(testRuntimePromptLocksW3JsonContract);
    CPPUNIT_TEST_SUITE_END();
};

void ProviderCapabilityMapWriter::testRewriteMapsToRewrite()
{
    const auto oCap = mapWriterActionToProviderCapability(u"rewrite"_ustr);
    CPPUNIT_ASSERT(oCap.has_value());
    CPPUNIT_ASSERT_EQUAL(u"rewrite"_ustr, *oCap);
}

void ProviderCapabilityMapWriter::testFormatCleanMapsToFormatFix()
{
    const auto oCap = mapWriterActionToProviderCapability(u"format-clean"_ustr);
    CPPUNIT_ASSERT(oCap.has_value());
    CPPUNIT_ASSERT_EQUAL(u"format-fix"_ustr, *oCap);
}

void ProviderCapabilityMapWriter::testShortenMapsToSummarize()
{
    const auto oCap = mapWriterActionToProviderCapability(u"shorten"_ustr);
    CPPUNIT_ASSERT(oCap.has_value());
    CPPUNIT_ASSERT_EQUAL(u"summarize"_ustr, *oCap);
}

void ProviderCapabilityMapWriter::testRuntimePromptLocksW3JsonContract()
{
    const OUString sPrompt = buildWriterRuntimeJsonPromptForProvider(
        u"rewrite"_ustr, u"Make the paragraph clearer."_ustr, u"swpara-7"_ustr,
        u"req-20260609-ollama"_ustr);

    CPPUNIT_ASSERT(sPrompt.indexOf(u"Return exactly one JSON object"_ustr) >= 0);
    CPPUNIT_ASSERT(sPrompt.indexOf(u"v2-w3-runtime-1"_ustr) >= 0);
    CPPUNIT_ASSERT(sPrompt.indexOf(u"ap-inline-req-20260609-ollama"_ustr) >= 0);
    CPPUNIT_ASSERT(sPrompt.indexOf(u"diag-inline-req-20260609-ollama"_ustr) >= 0);
    CPPUNIT_ASSERT(sPrompt.indexOf(u"sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"_ustr) >= 0);
    CPPUNIT_ASSERT(sPrompt.indexOf(u"swpara-7"_ustr) >= 0);
    CPPUNIT_ASSERT(sPrompt.indexOf(u"\"preview_only\":false"_ustr) >= 0);
    CPPUNIT_ASSERT(sPrompt.indexOf(u"\"target\":{\"paragraph_id\":\"swpara-7\"}"_ustr) >= 0);
    CPPUNIT_ASSERT(sPrompt.indexOf(u"paragraph-replace"_ustr) >= 0);
    CPPUNIT_ASSERT(sPrompt.indexOf(u"paragraph-insert-after"_ustr) >= 0);
    CPPUNIT_ASSERT(sPrompt.indexOf(u"text-range-replace"_ustr) >= 0);
    CPPUNIT_ASSERT(sPrompt.indexOf(u"text-format"_ustr) >= 0);
    CPPUNIT_ASSERT(sPrompt.indexOf(u"Never omit kind"_ustr) >= 0);
    CPPUNIT_ASSERT(sPrompt.indexOf(u"Make the paragraph clearer."_ustr) >= 0);
}

CPPUNIT_TEST_SUITE_REGISTRATION(ProviderCapabilityMapWriter);

} // namespace

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
