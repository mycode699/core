/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W1: Provider Runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "Provider.hxx"

#include "OllamaAdapter.hxx"
#include "RuntimePlanStub.hxx"

#include <com/sun/star/lang/IllegalArgumentException.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <cppuhelper/supportsservice.hxx>

#include <cstdlib>
#include <osl/time.h>

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

    TimeValue aStart{};
    osl_getSystemTime(&aStart);

    if (!m_policy.allows(req.capability))
    {
        rsp.status = "policy-denied";
        rsp.content = "service mode " + m_policy.modeName()
                    + " denies capability " + req.capability;
        rsp.evidenceId = OUString();
        TimeValue aEnd{};
        osl_getSystemTime(&aEnd);
        rsp.durationMs = static_cast<sal_Int32>(
            (aEnd.Seconds - aStart.Seconds) * 1000
            + static_cast<sal_Int64>(aEnd.Nanosec - aStart.Nanosec) / 1000000);
        return rsp;
    }

    // W4 E2E: v2-w3-runtime-1 JSON when Ollama is off. Requires both env vars so
    // legacy cppunit (DISABLE_PROBE only → provider-error) stays unchanged.
    if (std::getenv("KQOFFICE_AI_STUB_RUNTIME") != nullptr
        && std::getenv("KQOFFICE_AI_DISABLE_PROBE") != nullptr)
    {
        rsp.status = "ok";
        rsp.content = buildStubRuntimePlanJson(req);
        EvidenceRecord rec;
        rec.serviceMode = m_policy.modeName();
        rec.provider = u"stub-runtime"_ustr;
        rec.capability = req.capability;
        rec.status = rsp.status;
        rec.requestSizeBytes = req.prompt.getLength();
        rec.responseSizeBytes = rsp.content.getLength();
        TimeValue aEnd{};
        osl_getSystemTime(&aEnd);
        rsp.durationMs = static_cast<sal_Int32>(
            (aEnd.Seconds - aStart.Seconds) * 1000
            + static_cast<sal_Int64>(aEnd.Nanosec - aStart.Nanosec) / 1000000);
        rec.durationMs = rsp.durationMs;
        rsp.evidenceId = m_evidence.record(rec);
        return rsp;
    }

    // Day-1: dispatch through OllamaAdapter unless cppunit has set
    // KQOFFICE_AI_DISABLE_PROBE — pure-logic fixtures must not open
    // sockets to 127.0.0.1:11434 in the build sandbox.
    OUString providerLabel = u"stub"_ustr;
    if (std::getenv("KQOFFICE_AI_DISABLE_PROBE") != nullptr)
    {
        rsp.status = "provider-error";
        rsp.content = "no provider backend registered (W1 Day-0 stub)";
    }
    else
    {
        OllamaAdapter adapter;
        OUString p = adapter.probe();
        if (p == u"reachable"_ustr)
        {
            auto models = adapter.listModels();
            OUString head = models.empty() ? u"?"_ustr : models.front();
            providerLabel = u"ollama: " + head;
            if (models.empty())
            {
                // Daemon up but no models pulled — nothing to route the
                // prompt to. Evidence still records the attempt.
                rsp.status = "provider-error";
                rsp.content = "ollama reachable but no models installed";
            }
            else
            {
                OUString text = adapter.generate(head, req.prompt);
                if (text.isEmpty())
                {
                    rsp.status = "provider-error";
                    rsp.content = "ollama generate failed (timeout, "
                                  "non-2xx, or empty response)";
                }
                else
                {
                    rsp.status = "ok";
                    rsp.content = text;
                }
            }
        }
        else
        {
            rsp.status = "provider-error";
            rsp.content = "ollama unreachable at 127.0.0.1:11434";
        }
    }

    // Day-1: record evidence for any allowed call so operators can audit
    // why a capability returned ok / provider-error. Only the
    // policy-denied branch above keeps evidenceId empty.
    EvidenceRecord rec;
    rec.serviceMode = m_policy.modeName();
    rec.provider = providerLabel;
    rec.capability = req.capability;
    rec.status = rsp.status;
    rec.requestSizeBytes = req.prompt.getLength();
    rec.responseSizeBytes = rsp.content.getLength();
    TimeValue aEnd{};
    osl_getSystemTime(&aEnd);
    rsp.durationMs = static_cast<sal_Int32>(
        (aEnd.Seconds - aStart.Seconds) * 1000
        + static_cast<sal_Int64>(aEnd.Nanosec - aStart.Nanosec) / 1000000);
    rec.durationMs = rsp.durationMs;
    rsp.evidenceId = m_evidence.record(rec);
    return rsp;
}

css::uno::Sequence<OUString> SAL_CALL Provider::listCapabilities()
{
    return m_policy.currentAllowlist();
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

extern "C" SAL_DLLPUBLIC_EXPORT css::uno::XInterface*
com_kqoffice_ai_Provider_get_implementation(
    css::uno::XComponentContext* /*context*/,
    css::uno::Sequence<css::uno::Any> const& /*args*/)
{
    return ::cppu::acquire(new ::kqoffice::ai::Provider());
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
