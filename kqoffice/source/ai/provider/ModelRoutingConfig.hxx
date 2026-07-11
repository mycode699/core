/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * Load/save five-slot model routing for 可圈office (Clavue-aligned policy).
 * User slots: primary / light / agent / plan / review.
 * Source of truth order (high → low):
 *   1. env KQOFFICE_AI_*_MODEL overrides
 *   2. JSON config file (KQOFFICE_AI_ROUTING path or user profile)
 *   3. empty snapshot → first available Ollama model at call time
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_PROVIDER_MODELROUTINGCONFIG_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_PROVIDER_MODELROUTINGCONFIG_HXX

#include "ModelRoles.hxx"

#include <rtl/ustring.hxx>

namespace kqoffice::ai
{

/// Resolve routing path: env KQOFFICE_AI_ROUTING, else ~/.config/kqoffice/model-routing.json
SAL_DLLPUBLIC_EXPORT OUString defaultModelRoutingConfigPath();

/// Parse a minimal JSON object for routing keys (no full JSON library).
SAL_DLLPUBLIC_EXPORT ModelRoutingSnapshot parseModelRoutingJson(const OUString& rJson);

/// Load file + apply env overrides. Never throws.
SAL_DLLPUBLIC_EXPORT ModelRoutingSnapshot loadModelRoutingSnapshot();

/// Serialize snapshot to JSON (for diagnostics / settings export).
SAL_DLLPUBLIC_EXPORT OUString serializeModelRoutingJson(const ModelRoutingSnapshot& rRouting);

/// Ensure a default template file exists (does not overwrite).
SAL_DLLPUBLIC_EXPORT bool ensureDefaultModelRoutingTemplate();

/// Persist routing snapshot to the default (or KQOFFICE_AI_ROUTING) path.
/// Returns false if the path cannot be written.
SAL_DLLPUBLIC_EXPORT bool saveModelRoutingSnapshot(const ModelRoutingSnapshot& rRouting);

/// Human-readable config path for the Options UI.
SAL_DLLPUBLIC_EXPORT OUString modelRoutingConfigPathForDisplay();

} // namespace kqoffice::ai

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
