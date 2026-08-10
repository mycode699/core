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
    /// Optional vision/multimodal model (local Ollama tag). Empty → auto-pick.
    OUString visionModel;

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

/// Resolve local vision/multimodal model tag.
/// Order: rPreferred → routing.visionModel → env KQOFFICE_AI_VISION_MODEL →
/// first installed name matching vision-ish tags → light → primary → "llava".
SAL_DLLPUBLIC_EXPORT OUString resolveVisionModel(
    const OUString& rPreferred, const ModelRoutingSnapshot& rRouting,
    const std::vector<OUString>& rAvailableModels = {});

/// Map scenario capabilityHint (rewrite/chat/plan/review/agent/summarize/extract…)
/// to Provider capability string used by resolveModelForCapability.
SAL_DLLPUBLIC_EXPORT OUString normalizeCapabilityHint(const OUString& rHint);

/// One-shot routing diagnostic (Ollama and/or OpenAI-compatible gateway).
/// Used by Options probe and AI panel background health chip.
struct ModelRoutingDiagnostics
{
    bool ollamaReachable = false;
    bool gatewayReachable = false; ///< openai-compatible baseUrl probe
    OUString backend; ///< ollama | openai-compatible | …
    OUString baseUrl;
    bool apiKeyPresent = false;
    sal_Int32 installedCount = 0;
    OUString primaryResolved;
    OUString lightResolved;
    OUString agentResolved;
    OUString planResolved;
    OUString reviewResolved;
    OUString summaryZh; ///< multi-line human summary
    bool lightReady = false; ///< light slot resolved (background/summarize path)
    bool reviewReady = false; ///< review slot resolved
    bool healthy = false; ///< true when backend probe ok and primary slot usable
    /// Machine-stable issue token: ok | missing-key | gateway-offline | ollama-offline |
    /// no-primary | degraded
    OUString issueCode;
    /// Numbered recovery steps (zh-CN) for chat / options UI.
    OUString recoveryGuideZh;
    /// Operator paths (never contain secrets).
    OUString apiKeyPathHint; ///< e.g. ~/.config/kqoffice/api-key
    OUString routingConfigPathHint; ///< e.g. ~/.config/kqoffice/model-routing.json
    /// Membership (api.03122.com) — populated when baseUrl is the official gateway.
    bool membershipSessionOk = false;
    OUString membershipEmail; ///< empty if anonymous / missing
    sal_Int32 membershipDayFastRem = -1; ///< day fast remaining; -1 = unknown
    sal_Int32 membershipBoostPacks = -1; ///< 加油包 packs; -1 = unknown
    OUString membershipQuotaLineZh; ///< short chip: 「会员 · 今日剩 12 · 加油包 2」
};

/// Probe Ollama, load routing, resolve five user slots. Never throws.
SAL_DLLPUBLIC_EXPORT ModelRoutingDiagnostics diagnoseModelRouting();

/// Build a Markdown recovery card from a diagnostic snapshot (or re-probe if empty).
/// Used after Provider failures and when the user clicks「模型诊断」.
SAL_DLLPUBLIC_EXPORT OUString formatModelHealthRecoveryGuide(
    const ModelRoutingDiagnostics& rDiag, const OUString& rFailDetail = OUString());

/// Default local config directory (absolute when HOME is known).
SAL_DLLPUBLIC_EXPORT OUString kqofficeAiConfigDir();

} // namespace kqoffice::ai

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
