/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W1: Provider Runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * V2 W1 Day-0 — Service mode gate.
 * Spec: docs/product/v2/w1-provider-runtime-spec.md §"Service Mode Policy".
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_PROVIDER_SERVICEMODEPOLICY_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_PROVIDER_SERVICEMODEPOLICY_HXX

#include <com/sun/star/uno/Sequence.hxx>
#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai
{
/// Three-tier service mode contract:
///   "offline" — default; localhost only; no data leaves the device
///   "private" — admin-configured private endpoint
///   "cloud"   — explicit user opt-in for public cloud providers
///
/// Day-0 implementation: only "offline" mode is wired; "private"/"cloud"
/// are recognized as values but no allow-list is yet enforced.
class SAL_DLLPUBLIC_EXPORT ServiceModePolicy
{
public:
    enum class Mode
    {
        Offline,
        Private,
        Cloud,
    };

    /// Default-constructs in Offline mode (Day-0 invariant).
    ServiceModePolicy();

    /// True iff the active mode permits the named capability.
    /// Offline rule (Clavue-aligned multi-role):
    ///   rewrite, summarize, format-fix, intent-to-uno,
    ///   plan, review, extract, classify, verify, chat
    /// Private/cloud: deny until wired.
    bool allows(const OUString& capability) const;

    /// Stringified mode for ProviderResponse / evidence.
    OUString modeName() const;

    /// Capability tokens permitted in the active mode (W1.A honesty).
    css::uno::Sequence<OUString> currentAllowlist() const;

    Mode mode() const { return m_mode; }

private:
    Mode m_mode;
};

} // namespace kqoffice::ai

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
