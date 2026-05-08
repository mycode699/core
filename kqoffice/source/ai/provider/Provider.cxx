/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W1: Provider Runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "Provider.hxx"

#include <com/sun/star/lang/IllegalArgumentException.hpp>
#include <cppuhelper/supportsservice.hxx>

namespace kqoffice::ai
{
namespace
{
constexpr OUStringLiteral kImplName = u"com.kqoffice.ai.Provider";
constexpr OUStringLiteral kServiceName = u"com.sun.star.ai.Provider";
} // namespace

Provider::Provider() = default;
Provider::~Provider() = default;

css::ai::ProviderResponse SAL_CALL
Provider::call(const css::ai::ProviderRequest& req)
{
    if (req.capability.isEmpty())
    {
        throw css::lang::IllegalArgumentException(
            "ProviderRequest.capability must not be empty",
            static_cast<cppu::OWeakObject*>(this), 0);
    }

    css::ai::ProviderResponse rsp;
    rsp.durationMs = 0;

    if (!m_policy.allows(req.capability))
    {
        rsp.status = "policy-denied";
        rsp.content = "service mode " + m_policy.modeName()
                    + " denies capability " + req.capability;
        rsp.evidenceId = OUString();
        return rsp;
    }

    // Day-0 stub: no backend wired. Future W1 Day-1 will dispatch
    // through ProviderRegistry → OllamaAdapter.
    rsp.status = "provider-error";
    rsp.content = "no provider backend registered (W1 Day-0 stub)";
    rsp.evidenceId = OUString();
    return rsp;
}

css::uno::Sequence<OUString> SAL_CALL Provider::listCapabilities()
{
    // Empty until W1 Day-1 wires OllamaAdapter capability discovery.
    return {};
}

OUString SAL_CALL Provider::getServiceMode()
{
    return m_policy.modeName();
}

OUString SAL_CALL Provider::getImplementationName()
{
    return kImplName;
}

sal_Bool SAL_CALL Provider::supportsService(const OUString& name)
{
    return cppu::supportsService(this, name);
}

css::uno::Sequence<OUString> SAL_CALL Provider::getSupportedServiceNames()
{
    return { kServiceName };
}

} // namespace kqoffice::ai

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
