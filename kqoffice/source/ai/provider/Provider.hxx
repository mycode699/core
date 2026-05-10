/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W1: Provider Runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * V2 W1 Day-0 skeleton — see docs/product/v2/w1-provider-runtime-spec.md.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_PROVIDER_PROVIDER_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_PROVIDER_PROVIDER_HXX

#include <com/sun/star/ai/XProvider.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <cppuhelper/implbase.hxx>
#include <rtl/ustring.hxx>
#include <sal/types.h>

#include "EvidenceRecorder.hxx"
#include "ServiceModePolicy.hxx"

namespace kqoffice::ai
{
/// Day-0 reference Provider implementation.
///
/// Behavior contract for V2 W1 Day-0:
///   1. ServiceModePolicy gates every call.
///   2. No backend wired yet — `call()` returns status="policy-denied"
///      for any non-empty capability so callers can begin integrating
///      against a stable interface without needing Ollama installed.
///   3. listCapabilities() is empty until OllamaAdapter lands (W1 Day-1).
///   4. getServiceMode() returns whatever ServiceModePolicy reports.
class SAL_DLLPUBLIC_EXPORT Provider final
    : public ::cppu::WeakImplHelper<
          css::ai::XProvider,
          css::lang::XServiceInfo>
{
public:
    Provider();
    ~Provider() override;

    // XProvider
    css::ai::ProviderResponse SAL_CALL call(
        const css::ai::ProviderRequest& req) override;
    css::uno::Sequence<OUString> SAL_CALL listCapabilities() override;
    OUString SAL_CALL getServiceMode() override;

    // XServiceInfo
    OUString SAL_CALL getImplementationName() override;
    sal_Bool SAL_CALL supportsService(const OUString& name) override;
    css::uno::Sequence<OUString> SAL_CALL getSupportedServiceNames() override;

private:
    ServiceModePolicy m_policy;
    EvidenceRecorder m_evidence;
};

} // namespace kqoffice::ai

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
