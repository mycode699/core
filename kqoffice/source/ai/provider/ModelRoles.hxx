/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * Clavue-aligned model roles with a **5-slot user-facing routing surface**:
 *   primary  — 主/兜底模型 (mainLoop + fallback)
 *   light    — 轻量/并发 (summarize/extract/classify/background)
 *   agent    — Agent/子代理 (cowork, mesh, subagent work)
 *   plan     — 思考/规划 (planner, judge/guide)
 *   review   — 审核/审查 (reviewer)
 *
 * Internal role→slot mapping expands these five (like Clavue expands
 * Primary/Haiku/Sonnet/Opus into derived slots). Users never set 9 fields.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_PROVIDER_MODELROLES_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_PROVIDER_MODELROLES_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai
{

enum class ModelRole
{
    MainLoop,
    Summarize,
    Extract,
    Classify,
    SearchMemory,
    QuickSummary,
    Background,
    Planner,
    Reviewer,
    Judge,
    Verifier,
    Fallback,
    Agent, // cowork / mesh agent step
};

/// User-facing routing slots (exactly five).
enum class ModelRouteSlot
{
    Primary, // 主/兜底
    Light, // 轻量/并发
    Agent, // Agent
    Plan, // 思考/规划
    Review, // 审核/审查
};

/// Persisted + runtime snapshot. User edits only the five fields below;
/// expandUserSlots() fills legacy aliases for internal callers.
struct ModelRoutingSnapshot
{
    // —— 五槽用户面 ——
    OUString primaryModel; // 主/兜底
    OUString lightModel; // 轻量/并发
    OUString agentModel; // Agent
    OUString planModel; // 思考/规划
    OUString reviewModel; // 审核/审查

    OUString backend; // "ollama" | …
    OUString baseUrl;

    // —— 兼容别名（读写旧 JSON / 内部）——
    // 读入时 smallFast→light, subagent→agent；写出时双向同步。
    OUString smallFastModel; // alias of lightModel
    OUString subagentModel; // alias of agentModel
    OUString exploreModel; // derived → agent
    OUString generalModel; // derived → agent or light
    OUString teamModel; // derived → agent
    OUString guideModel; // derived → plan
};

struct ModelRoleResolution
{
    ModelRole role = ModelRole::MainLoop;
    ModelRouteSlot slot = ModelRouteSlot::Primary;
    OUString model;
    OUString roleName;
    OUString slotName;
};

SAL_DLLPUBLIC_EXPORT ModelRole modelRoleForCapability(const OUString& rCapability);
SAL_DLLPUBLIC_EXPORT ModelRouteSlot defaultSlotForRole(ModelRole eRole);
SAL_DLLPUBLIC_EXPORT OUString modelRoleName(ModelRole eRole);
SAL_DLLPUBLIC_EXPORT OUString modelRouteSlotName(ModelRouteSlot eSlot);

/// Normalize aliases: light↔smallFast, agent↔subagent, expand derived slots.
SAL_DLLPUBLIC_EXPORT void expandUserSlots(ModelRoutingSnapshot& rRouting);

SAL_DLLPUBLIC_EXPORT ModelRoleResolution resolveModelForRole(
    ModelRole eRole, const ModelRoutingSnapshot& rRouting,
    const std::vector<OUString>& rAvailableModels);

SAL_DLLPUBLIC_EXPORT ModelRoleResolution resolveModelForCapability(
    const OUString& rCapability, const ModelRoutingSnapshot& rRouting,
    const std::vector<OUString>& rAvailableModels);

SAL_DLLPUBLIC_EXPORT OUString pickAvailableModel(
    const OUString& rPreferred, const std::vector<OUString>& rAvailableModels);

/// Map scenario capabilityHint (rewrite/chat/plan/review/agent/summarize/extract…)
/// to Provider capability string used by resolveModelForCapability.
SAL_DLLPUBLIC_EXPORT OUString normalizeCapabilityHint(const OUString& rHint);

/// One-shot routing diagnostic (Ollama probe + five-slot resolution).
/// Used by Options probe and AI panel background health chip.
struct ModelRoutingDiagnostics
{
    bool ollamaReachable = false;
    sal_Int32 installedCount = 0;
    OUString primaryResolved;
    OUString lightResolved;
    OUString agentResolved;
    OUString planResolved;
    OUString reviewResolved;
    OUString summaryZh; ///< multi-line human summary
    bool lightReady = false; ///< light slot resolved (background/summarize path)
    bool reviewReady = false; ///< review slot resolved
};

/// Probe Ollama, load routing, resolve five user slots. Never throws.
SAL_DLLPUBLIC_EXPORT ModelRoutingDiagnostics diagnoseModelRouting();

} // namespace kqoffice::ai

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
