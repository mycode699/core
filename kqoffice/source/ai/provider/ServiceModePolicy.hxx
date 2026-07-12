/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W1: Provider Runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * V2 W1 — Service mode gate.
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
///   "offline" — default; local Ollama / localhost paths
///   "private" — admin-configured private gateway (openai-compatible etc.)
///   "cloud"   — explicit opt-in for public cloud (requires allow-cloud env)
///
/// Mode selection (first match wins):
///   1. KQOFFICE_AI_SERVICE_MODE=offline|private|cloud
///   2. else offline
///
/// Cloud additionally requires KQOFFICE_AI_ALLOW_CLOUD=1 (or true/yes).
class SAL_DLLPUBLIC_EXPORT ServiceModePolicy
{
public:
    enum class Mode
    {
        Offline,
        Private,
        Cloud,
    };

    /// Constructs from environment (default Offline).
    ServiceModePolicy();

    /// Explicit construct for tests / Options UI wiring.
    explicit ServiceModePolicy(Mode eMode);

    /// True iff the active mode permits the named capability.
    /// Offline + private share the local-safe allow-list.
    /// Cloud uses the same allow-list only when allow-cloud is set; otherwise deny-all.
    bool allows(const OUString& capability) const;

    /// Stringified mode for ProviderResponse / evidence.
    OUString modeName() const;

    /// Capability tokens permitted in the active mode.
    css::uno::Sequence<OUString> currentAllowlist() const;

    Mode mode() const { return m_mode; }

    /// Parse "offline"/"private"/"cloud" (case-insensitive). Unknown → Offline.
    static Mode parseModeName(const OUString& rName);

    /// Read mode from KQOFFICE_AI_SERVICE_MODE (default Offline).
    static Mode modeFromEnvironment();

    /// True when KQOFFICE_AI_ALLOW_CLOUD is 1/true/yes.
    static bool cloudExplicitlyAllowed();

private:
    Mode m_mode;
    bool m_cloudAllowed = false;
};

} // namespace kqoffice::ai

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
