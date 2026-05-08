/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W1: Provider Runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Day-0 unit tests — covers the three behaviors that must hold even before
 * any backend (Ollama / private / cloud) is wired:
 *   1. ServiceModePolicy default mode is "offline".
 *   2. Offline mode allows the four Day-0 capabilities and denies everything else.
 *   3. Provider.call() rejects empty capability via IllegalArgumentException.
 *   4. Provider.call() returns status="provider-error" for allowed capability
 *      (no backend yet) and status="policy-denied" for disallowed capability.
 */

#include <sal/types.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/plugin/TestPlugIn.h>

#include <com/sun/star/lang/IllegalArgumentException.hpp>
#include <rtl/ref.hxx>
#include <rtl/ustring.hxx>

#include "Provider.hxx"
#include "ServiceModePolicy.hxx"

namespace
{
// Day-0 contract test — pure C++ logic, no UNO bootstrap. Avoids
// BootstrapFixture so the binary does not need a working services.rdb,
// matching the W2 FuzzyMatcher fast-test approach.
class ProviderTest : public CppUnit::TestFixture
{
public:
    void testDefaultModeIsOffline();
    void testOfflineAllowsRewrite();
    void testOfflineDeniesUnknownCapability();
    void testEmptyCapabilityThrows();
    void testAllowedCapabilityReturnsStubError();
    void testDeniedCapabilityReturnsPolicyDenied();
    void testServiceModeAccessor();

    CPPUNIT_TEST_SUITE(ProviderTest);
    CPPUNIT_TEST(testDefaultModeIsOffline);
    CPPUNIT_TEST(testOfflineAllowsRewrite);
    CPPUNIT_TEST(testOfflineDeniesUnknownCapability);
    CPPUNIT_TEST(testEmptyCapabilityThrows);
    CPPUNIT_TEST(testAllowedCapabilityReturnsStubError);
    CPPUNIT_TEST(testDeniedCapabilityReturnsPolicyDenied);
    CPPUNIT_TEST(testServiceModeAccessor);
    CPPUNIT_TEST_SUITE_END();
};

void ProviderTest::testDefaultModeIsOffline()
{
    kqoffice::ai::ServiceModePolicy p;
    CPPUNIT_ASSERT_EQUAL(u"offline"_ustr, p.modeName());
    CPPUNIT_ASSERT_EQUAL(kqoffice::ai::ServiceModePolicy::Mode::Offline, p.mode());
}

void ProviderTest::testOfflineAllowsRewrite()
{
    kqoffice::ai::ServiceModePolicy p;
    CPPUNIT_ASSERT(p.allows(u"rewrite"_ustr));
    CPPUNIT_ASSERT(p.allows(u"summarize"_ustr));
    CPPUNIT_ASSERT(p.allows(u"format-fix"_ustr));
    CPPUNIT_ASSERT(p.allows(u"intent-to-uno"_ustr));
}

void ProviderTest::testOfflineDeniesUnknownCapability()
{
    kqoffice::ai::ServiceModePolicy p;
    CPPUNIT_ASSERT(!p.allows(u"steal-keys"_ustr));
    CPPUNIT_ASSERT(!p.allows(u""_ustr));
    CPPUNIT_ASSERT(!p.allows(u"summarize-and-upload"_ustr));
}

void ProviderTest::testEmptyCapabilityThrows()
{
    rtl::Reference<kqoffice::ai::Provider> provider = new kqoffice::ai::Provider;
    css::ai::ProviderRequest req;
    req.capability = u""_ustr;
    req.prompt = u"hi"_ustr;
    req.timeoutMs = 5000;
    CPPUNIT_ASSERT_THROW(provider->call(req),
                         css::lang::IllegalArgumentException);
}

void ProviderTest::testAllowedCapabilityReturnsStubError()
{
    rtl::Reference<kqoffice::ai::Provider> provider = new kqoffice::ai::Provider;
    css::ai::ProviderRequest req;
    req.capability = u"rewrite"_ustr;
    req.prompt = u"make this better"_ustr;
    req.timeoutMs = 5000;
    auto rsp = provider->call(req);
    CPPUNIT_ASSERT_EQUAL(u"provider-error"_ustr, rsp.status);
    CPPUNIT_ASSERT(rsp.evidenceId.isEmpty());
}

void ProviderTest::testDeniedCapabilityReturnsPolicyDenied()
{
    rtl::Reference<kqoffice::ai::Provider> provider = new kqoffice::ai::Provider;
    css::ai::ProviderRequest req;
    req.capability = u"unauthorized-capability"_ustr;
    req.prompt = u"x"_ustr;
    req.timeoutMs = 1000;
    auto rsp = provider->call(req);
    CPPUNIT_ASSERT_EQUAL(u"policy-denied"_ustr, rsp.status);
    CPPUNIT_ASSERT(rsp.evidenceId.isEmpty());
    // Content must mention the mode name so callers can surface it.
    CPPUNIT_ASSERT(rsp.content.indexOf("offline") >= 0);
}

void ProviderTest::testServiceModeAccessor()
{
    rtl::Reference<kqoffice::ai::Provider> provider = new kqoffice::ai::Provider;
    CPPUNIT_ASSERT_EQUAL(u"offline"_ustr, provider->getServiceMode());
}

CPPUNIT_TEST_SUITE_REGISTRATION(ProviderTest);
} // namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
