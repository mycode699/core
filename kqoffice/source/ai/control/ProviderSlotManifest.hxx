/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI harness: provider Slot as data).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Engines are JSON files — code holds no vendor enum. AutoHarness BUILD 00.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_PROVIDERSLOTMANIFEST_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_PROVIDERSLOTMANIFEST_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::control
{

struct ProviderSlotManifest
{
    OUString id; ///< [a-z0-9.-]{1,64}
    OUString displayNameZh;
    OUString backend; ///< ollama | openai-compatible | membership | custom
    OUString baseUrl;
    OUString healthPath; ///< optional path fragment for ping
    bool allowNetwork = true;
    bool enabled = true;
    std::vector<OUString> aliases;
};

class SAL_DLLPUBLIC_EXPORT ProviderSlotRegistry
{
public:
    /// Load bundled defaults + override dir. Never throws.
    static std::vector<ProviderSlotManifest> loadAll();

    /// Parse one JSON object (minimal key extract).
    static bool parseOne(const OUString& json, ProviderSlotManifest& out);

    /// Validate id charset; empty or path segments rejected.
    static bool isValidId(const OUString& id);

    /// Override directory: KQOFFICE_AI_PROVIDER_SLOTS_DIR or ~/.config/kqoffice/provider-slots
    static OUString overrideDir();

    /// Built-in slots (always present if not overridden by same id).
    static std::vector<ProviderSlotManifest> builtinDefaults();

    /// Find by id or alias (case-insensitive id).
    static const ProviderSlotManifest* find(const std::vector<ProviderSlotManifest>& slots,
                                            const OUString& idOrAlias);

    /// One-line summary for diagnostics.
    static OUString summaryLineZh(const std::vector<ProviderSlotManifest>& slots);

    /// Ensure override dir has a README template (does not overwrite manifests).
    static bool ensureOverrideTemplate();
};

} // namespace kqoffice::ai::control

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
