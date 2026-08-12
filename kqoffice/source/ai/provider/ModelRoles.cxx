/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 */

#include "ModelRoles.hxx"
#include "MembershipClient.hxx"
#include "ModelRoutingConfig.hxx"
#include "OllamaAdapter.hxx"
#include "OpenAICompatibleAdapter.hxx"

#include <AiPaths.hxx>
#include <AiResourceEnvelope.hxx>

#include <osl/time.h>
#include <rtl/strbuf.hxx>
#include <rtl/ustrbuf.hxx>
#include <rtl/ustring.hxx>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>

namespace kqoffice::ai
{
namespace
{
OUString slotModel(const ModelRoutingSnapshot& r, ModelRouteSlot eSlot)
{
    switch (eSlot)
    {
        case ModelRouteSlot::Primary:
            return r.primaryModel;
        case ModelRouteSlot::Light:
            return !r.lightModel.isEmpty() ? r.lightModel : r.smallFastModel;
        case ModelRouteSlot::Agent:
            return !r.agentModel.isEmpty() ? r.agentModel : r.subagentModel;
        case ModelRouteSlot::Plan:
            return r.planModel;
        case ModelRouteSlot::Review:
            return r.reviewModel;
    }
    return r.primaryModel;
}

void fillIfEmpty(OUString& field, const OUString& value)
{
    if (field.isEmpty() && !value.isEmpty())
        field = value;
}
} // namespace

void expandUserSlots(ModelRoutingSnapshot& r)
{
    // Prefer canonical five-slot names; accept legacy aliases on load.
    if (r.lightModel.isEmpty() && !r.smallFastModel.isEmpty())
        r.lightModel = r.smallFastModel;
    if (r.agentModel.isEmpty() && !r.subagentModel.isEmpty())
        r.agentModel = r.subagentModel;
    if (r.smallFastModel.isEmpty())
        r.smallFastModel = r.lightModel;
    if (r.subagentModel.isEmpty())
        r.subagentModel = r.agentModel;

    // Derived internal slots (Clavue-style expansion from a few user slots).
    fillIfEmpty(r.guideModel, r.planModel);
    fillIfEmpty(r.exploreModel, r.agentModel);
    fillIfEmpty(r.teamModel, r.agentModel);
    fillIfEmpty(r.generalModel, !r.agentModel.isEmpty() ? r.agentModel : r.lightModel);

    // Empty specialized slots fall back toward primary (never invent names).
    fillIfEmpty(r.lightModel, r.primaryModel);
    fillIfEmpty(r.agentModel, r.primaryModel);
    fillIfEmpty(r.planModel, r.primaryModel);
    fillIfEmpty(r.reviewModel, r.primaryModel);
    fillIfEmpty(r.smallFastModel, r.lightModel);
    fillIfEmpty(r.subagentModel, r.agentModel);
    fillIfEmpty(r.guideModel, r.planModel);
    fillIfEmpty(r.exploreModel, r.agentModel);
    fillIfEmpty(r.teamModel, r.agentModel);
    fillIfEmpty(r.generalModel, r.agentModel);
}

ModelRole modelRoleForCapability(const OUString& rCapability)
{
    const OUString c = rCapability.toAsciiLowerCase();
    if (c == u"summarize"_ustr || c == u"quick-summary"_ustr || c == u"quicksummary"_ustr
        || c == u"shorten"_ustr || c == u"condense"_ustr)
        return ModelRole::Summarize;
    // Short selection rewrite/formal/polish → light slot (faster when light ≠ primary).
    if (c == u"quick-edit"_ustr || c == u"rewrite-light"_ustr)
        return ModelRole::QuickSummary;
    if (c == u"extract"_ustr || c == u"session-memory"_ustr)
        return ModelRole::Extract;
    if (c == u"classify"_ustr || c == u"intent-to-uno"_ustr)
        return ModelRole::Classify;
    if (c == u"search-memory"_ustr || c == u"searchmemory"_ustr || c == u"knowledge-query"_ustr)
        return ModelRole::SearchMemory;
    if (c == u"background"_ustr || c == u"hook"_ustr)
        return ModelRole::Background;
    if (c == u"plan"_ustr || c == u"planner"_ustr || c == u"outline"_ustr
        || c == u"agent-plan"_ustr)
        return ModelRole::Planner;
    if (c == u"review"_ustr || c == u"reviewer"_ustr || c == u"critique"_ustr
        || c == u"proofread"_ustr)
        return ModelRole::Reviewer;
    if (c == u"judge"_ustr || c == u"arbiter"_ustr)
        return ModelRole::Judge;
    if (c == u"verify"_ustr || c == u"verifier"_ustr)
        return ModelRole::Verifier;
    if (c == u"agent"_ustr || c == u"cowork"_ustr || c == u"subagent"_ustr
        || c == u"mesh"_ustr || c == u"fallback"_ustr)
        return ModelRole::Agent;
    // Product edit verbs → primary/mainLoop (rewrite surface).
    if (c == u"chat"_ustr || c == u"rewrite"_ustr || c == u"polish"_ustr || c == u"edit"_ustr
        || c == u"expand"_ustr || c == u"expand-write"_ustr || c == u"translate"_ustr
        || c == u"translation"_ustr || c == u"paraphrase"_ustr || c == u"formula"_ustr)
        return ModelRole::MainLoop;
    return ModelRole::MainLoop;
}

ModelRouteSlot defaultSlotForRole(ModelRole eRole)
{
    switch (eRole)
    {
        case ModelRole::MainLoop:
        case ModelRole::Fallback:
            return ModelRouteSlot::Primary;
        case ModelRole::Summarize:
        case ModelRole::Extract:
        case ModelRole::Classify:
        case ModelRole::SearchMemory:
        case ModelRole::QuickSummary:
        case ModelRole::Background:
        case ModelRole::Verifier:
            return ModelRouteSlot::Light;
        case ModelRole::Agent:
            return ModelRouteSlot::Agent;
        case ModelRole::Planner:
        case ModelRole::Judge:
            return ModelRouteSlot::Plan;
        case ModelRole::Reviewer:
            return ModelRouteSlot::Review;
    }
    return ModelRouteSlot::Primary;
}

OUString modelRoleName(ModelRole eRole)
{
    switch (eRole)
    {
        case ModelRole::MainLoop:
            return u"mainLoop"_ustr;
        case ModelRole::Summarize:
            return u"summarize"_ustr;
        case ModelRole::Extract:
            return u"extract"_ustr;
        case ModelRole::Classify:
            return u"classify"_ustr;
        case ModelRole::SearchMemory:
            return u"searchMemory"_ustr;
        case ModelRole::QuickSummary:
            return u"quickSummary"_ustr;
        case ModelRole::Background:
            return u"background"_ustr;
        case ModelRole::Planner:
            return u"planner"_ustr;
        case ModelRole::Reviewer:
            return u"reviewer"_ustr;
        case ModelRole::Judge:
            return u"judge"_ustr;
        case ModelRole::Verifier:
            return u"verifier"_ustr;
        case ModelRole::Fallback:
            return u"fallback"_ustr;
        case ModelRole::Agent:
            return u"agent"_ustr;
    }
    return u"mainLoop"_ustr;
}

OUString modelRouteSlotName(ModelRouteSlot eSlot)
{
    switch (eSlot)
    {
        case ModelRouteSlot::Primary:
            return u"primary"_ustr;
        case ModelRouteSlot::Light:
            return u"light"_ustr;
        case ModelRouteSlot::Agent:
            return u"agent"_ustr;
        case ModelRouteSlot::Plan:
            return u"plan"_ustr;
        case ModelRouteSlot::Review:
            return u"review"_ustr;
    }
    return u"primary"_ustr;
}

OUString pickAvailableModel(const OUString& rPreferred,
                            const std::vector<OUString>& rAvailableModels)
{
    if (!rPreferred.isEmpty())
    {
        for (const auto& m : rAvailableModels)
        {
            if (m.equalsIgnoreAsciiCase(rPreferred))
                return m;
        }
        for (const auto& m : rAvailableModels)
        {
            if (m.startsWith(rPreferred) || rPreferred.startsWith(m))
                return m;
        }
        return rPreferred;
    }
    if (!rAvailableModels.empty())
        return rAvailableModels.front();
    return OUString();
}

ModelRoleResolution resolveModelForRole(ModelRole eRole, const ModelRoutingSnapshot& rRouting,
                                        const std::vector<OUString>& rAvailableModels)
{
    ModelRoutingSnapshot routing = rRouting;
    expandUserSlots(routing);

    ModelRoleResolution aOut;
    aOut.role = eRole;
    aOut.roleName = modelRoleName(eRole);
    aOut.slot = defaultSlotForRole(eRole);
    aOut.slotName = modelRouteSlotName(aOut.slot);

    OUString preferred = slotModel(routing, aOut.slot);
    // Fallback chain toward primary (Clavue discipline: never invent family).
    if (preferred.isEmpty() && aOut.slot != ModelRouteSlot::Primary)
    {
        if (aOut.slot == ModelRouteSlot::Light)
            preferred = routing.agentModel;
        if (preferred.isEmpty() && aOut.slot == ModelRouteSlot::Agent)
            preferred = routing.lightModel;
        if (preferred.isEmpty() && aOut.slot == ModelRouteSlot::Plan)
            preferred = routing.primaryModel;
        if (preferred.isEmpty() && aOut.slot == ModelRouteSlot::Review)
            preferred = routing.planModel.isEmpty() ? routing.primaryModel : routing.planModel;
        if (preferred.isEmpty())
            preferred = routing.primaryModel;
    }

    aOut.model = pickAvailableModel(preferred, rAvailableModels);
    return aOut;
}

ModelRoleResolution resolveModelForCapability(const OUString& rCapability,
                                              const ModelRoutingSnapshot& rRouting,
                                              const std::vector<OUString>& rAvailableModels)
{
    return resolveModelForRole(modelRoleForCapability(rCapability), rRouting, rAvailableModels);
}

namespace
{
bool looksLikeVisionModelName(const OUString& name)
{
    const OUString n = name.toAsciiLowerCase();
    return n.indexOf(u"llava"_ustr) >= 0 || n.indexOf(u"bakllava"_ustr) >= 0
           || n.indexOf(u"moondream"_ustr) >= 0 || n.indexOf(u"minicpm-v"_ustr) >= 0
           || n.indexOf(u"minicpm_v"_ustr) >= 0 || n.indexOf(u"qwen2-vl"_ustr) >= 0
           || n.indexOf(u"qwen2.5-vl"_ustr) >= 0 || n.indexOf(u"qwen-vl"_ustr) >= 0
           || n.indexOf(u"vision"_ustr) >= 0 || n.indexOf(u"gemma3"_ustr) >= 0
           || n.indexOf(u"pixtral"_ustr) >= 0 || n.indexOf(u"llama3.2-vision"_ustr) >= 0
           || n.indexOf(u"llama-vision"_ustr) >= 0;
}
} // namespace

OUString resolveVisionModel(const OUString& rPreferred, const ModelRoutingSnapshot& rRouting,
                            const std::vector<OUString>& rAvailableModels)
{
    auto firstVisionInstalled = [&]() -> OUString {
        for (const auto& m : rAvailableModels)
        {
            if (looksLikeVisionModelName(m))
                return m;
        }
        return OUString();
    };

    if (!rPreferred.isEmpty())
        return pickAvailableModel(rPreferred, rAvailableModels);

    if (!rRouting.visionModel.isEmpty())
        return pickAvailableModel(rRouting.visionModel, rAvailableModels);

    const char* env = std::getenv("KQOFFICE_AI_VISION_MODEL");
    if (env && *env)
        return pickAvailableModel(OUString::fromUtf8(env), rAvailableModels);

    const OUString vis = firstVisionInstalled();
    if (!vis.isEmpty())
        return vis;

    // Soft fallbacks: light → primary → hard default llava (may 404 if not installed)
    if (!rRouting.lightModel.isEmpty())
        return pickAvailableModel(rRouting.lightModel, rAvailableModels);
    if (!rRouting.primaryModel.isEmpty())
        return pickAvailableModel(rRouting.primaryModel, rAvailableModels);
    if (!rAvailableModels.empty())
        return rAvailableModels.front();
    return u"llava"_ustr;
}

OUString normalizeCapabilityHint(const OUString& rHint)
{
    const OUString c = rHint.toAsciiLowerCase().trim();
    if (c.isEmpty())
        return u"chat"_ustr;
    if (c == u"quick-edit"_ustr || c == u"rewrite-light"_ustr)
        return u"quick-edit"_ustr;
    if (c == u"rewrite"_ustr || c == u"polish"_ustr || c == u"edit"_ustr || c == u"formal"_ustr
        || c == u"expand"_ustr || c == u"expand-write"_ustr || c == u"paraphrase"_ustr
        || c == u"translate"_ustr || c == u"translation"_ustr)
        return u"rewrite"_ustr;
    if (c == u"summarize"_ustr || c == u"summary"_ustr || c == u"shorten"_ustr
        || c == u"condense"_ustr)
        return u"summarize"_ustr;
    if (c == u"extract"_ustr || c == u"data-clean"_ustr || c == u"clean"_ustr)
        return u"extract"_ustr;
    if (c == u"plan"_ustr || c == u"planner"_ustr || c == u"outline"_ustr)
        return u"plan"_ustr;
    if (c == u"review"_ustr || c == u"reviewer"_ustr || c == u"proofread"_ustr
        || c == u"critique"_ustr || c == u"checklist"_ustr)
        return u"review"_ustr;
    if (c == u"agent"_ustr || c == u"cowork"_ustr || c == u"mesh"_ustr || c == u"multi-step"_ustr)
        return u"agent"_ustr;
    if (c == u"chat"_ustr || c == u"formula"_ustr || c == u"help"_ustr)
        return u"chat"_ustr;
    // pass-through known provider capabilities
    if (c == u"classify"_ustr || c == u"background"_ustr || c == u"judge"_ustr
        || c == u"verify"_ustr)
        return c;
    return c;
}

namespace
{
OUString homeConfigDir()
{
    // mac: ~/.config/kqoffice · Win: %APPDATA%/kqoffice
    const OUString d = kqofficeAiConfigDir();
    return d.isEmpty() ? u"~/.config/kqoffice"_ustr : d;
}

/// Best-effort membership quota via cached MembershipClient (no duplicate curl).
void enrichMembershipQuota(ModelRoutingDiagnostics& d)
{
    d.membershipSessionOk = false;
    d.membershipEmail.clear();
    d.membershipDayFastRem = -1;
    d.membershipBoostPacks = -1;
    d.membershipQuotaLineZh.clear();

    if (!OpenAICompatibleAdapter::isOpenAICompatibleBackend(d.backend))
        return;
    if (d.baseUrl.indexOf(u"api.03122.com") < 0 && d.baseUrl.indexOf(u"03122.com") < 0)
        return;
    if (!d.apiKeyPresent)
        return;

    // Reuses process-level status cache (AiResourceEnvelope TTL) — no second HTTP.
    const MembershipBoostResult br = membershipBoostAction(u"status"_ustr);
    d.membershipEmail = br.email;
    d.membershipDayFastRem = br.dayFastRem;
    d.membershipBoostPacks = br.packs;
    d.membershipSessionOk = !br.email.isEmpty() || br.dayFastRem >= 0 || br.ok;

    OUStringBuffer line;
    line.append(u"会员"_ustr);
    if (!d.membershipEmail.isEmpty())
    {
        line.append(u" · "_ustr);
        OUString em = d.membershipEmail;
        if (em.getLength() > 22)
            em = em.copy(0, 20) + u"…"_ustr;
        line.append(em);
    }
    if (d.membershipDayFastRem >= 0)
    {
        line.append(u" · 今日剩 "_ustr);
        line.append(d.membershipDayFastRem);
    }
    if (d.membershipBoostPacks >= 0)
    {
        line.append(u" · 加油包 "_ustr);
        line.append(d.membershipBoostPacks);
    }
    if (d.membershipDayFastRem == 0)
        line.append(u" · 额度不足可签到/升级"_ustr);
    d.membershipQuotaLineZh = line.makeStringAndClear();
}

void fillRecoveryGuide(ModelRoutingDiagnostics& d)
{
    d.apiKeyPathHint = homeConfigDir() + u"/api-key"_ustr;
    d.routingConfigPathHint = homeConfigDir() + u"/model-routing.json"_ustr;

    OUStringBuffer g;
    g.append(u"### 模型健康与修复\n\n"_ustr);
    g.append(d.summaryZh);
    g.append(u"\n\n"_ustr);

    if (d.issueCode == u"missing-key"_ustr)
    {
        g.append(u"**问题：未登录会员或 session 无效（api.03122.com）**\n\n"_ustr);
        g.append(u"1. 推荐 Device 登录（写入会员 sessionToken，不是上游 sk-）：\n\n"_ustr);
        g.append(u"```bash\nbin/kqoffice-cloud-login.sh\n```\n\n"_ustr);
        g.append(u"2. 或网页注册后 Device 授权：https://www.03122.com/zh-CN/account/device.html\n\n"_ustr);
        g.append(u"3. `model-routing.json` 的 baseUrl 须为 `https://api.03122.com`\n\n"_ustr);
        g.append(u"4. 回到侧栏点 **「模型诊断」** 验证（无需改文档）\n"_ustr);
    }
    else if (d.issueCode == u"gateway-offline"_ustr)
    {
        g.append(u"**问题：OpenAI 兼容网关不可达**\n\n"_ustr);
        g.append(u"1. 检查 `baseUrl`（当前：`"_ustr);
        g.append(d.baseUrl.isEmpty() ? u"?"_ustr : d.baseUrl);
        g.append(u"`）\n\n"_ustr);
        g.append(u"2. 编辑 `"_ustr);
        g.append(d.routingConfigPathHint);
        g.append(u"` 中的 `baseUrl` / `backend`\n\n"_ustr);
        g.append(u"3. 本机可临时改用 Ollama：`backend` 设为 `ollama` 并启动 `ollama serve`\n\n"_ustr);
        g.append(u"4. 点 **「模型诊断」** 复查连通性\n"_ustr);
    }
    else if (d.issueCode == u"ollama-offline"_ustr)
    {
        g.append(u"**问题：本地 Ollama 不可达（127.0.0.1:11434）**\n\n"_ustr);
        g.append(u"1. 启动：`ollama serve`（或打开 Ollama 应用）\n\n"_ustr);
        g.append(u"2. 安装模型：`ollama pull <模型名>`\n\n"_ustr);
        g.append(u"3. 在 `"_ustr);
        g.append(d.routingConfigPathHint);
        g.append(u"` 设置 `primaryModel` / `lightModel`\n\n"_ustr);
        g.append(u"4. 点 **「模型诊断」** 确认「Ollama 可达」\n"_ustr);
    }
    else if (d.issueCode == u"no-primary"_ustr)
    {
        g.append(u"**问题：主模型槽未解析**\n\n"_ustr);
        g.append(u"1. 打开 `"_ustr);
        g.append(d.routingConfigPathHint);
        g.append(u"`，设置 `primaryModel`（或 `auto`）\n\n"_ustr);
        g.append(u"2. Ollama：确认 `ollama list` 有对应模型\n\n"_ustr);
        g.append(u"3. 网关：确认 Key 有效且 `/v1/models` 可访问\n\n"_ustr);
        g.append(u"4. 点 **「模型诊断」** 复查\n"_ustr);
    }
    else if (d.issueCode == u"degraded"_ustr)
    {
        g.append(u"**状态：部分槽位可用（降级）**\n\n"_ustr);
        g.append(u"1. 主槽可用时可先改写/对话\n\n"_ustr);
        g.append(u"2. 配置 light / review 槽以启用总结与审查\n\n"_ustr);
        g.append(u"3. 点 **「模型诊断」** 查看五槽解析\n"_ustr);
    }
    else
    {
        g.append(u"**状态：就绪**\n\n"_ustr);
        g.append(u"- 后端：`"_ustr);
        g.append(d.backend.isEmpty() ? u"?"_ustr : d.backend);
        g.append(u"`\n"_ustr);
        g.append(u"- 主模型：`"_ustr);
        g.append(d.primaryResolved.isEmpty() ? u"?"_ustr : d.primaryResolved);
        g.append(u"`\n"_ustr);
        g.append(u"- 可直接选区改写 / 对话；写回仍须批准\n"_ustr);
    }

    g.append(u"\n---\n"_ustr);
    g.append(u"**路径**\n"_ustr);
    g.append(u"- Key：`"_ustr);
    g.append(d.apiKeyPathHint);
    g.append(u"`\n"_ustr);
    g.append(u"- 路由：`"_ustr);
    g.append(d.routingConfigPathHint);
    g.append(u"`\n"_ustr);
    g.append(u"- 菜单：工具 → 选项 → 可圈 AI\n"_ustr);
    g.append(u"- 纪律：修复过程 **不改主文档**；诊断仅探测连通性\n"_ustr);
    d.recoveryGuideZh = g.makeStringAndClear();
}
} // namespace

// Defined in AiPaths.cxx — keep export from this TU for link stability with older call sites.
// (Primary implementation: kqoffice::ai::kqofficeAiConfigDir in AiPaths.)

OUString formatModelHealthRecoveryGuide(const ModelRoutingDiagnostics& rDiag,
                                        const OUString& rFailDetail)
{
    ModelRoutingDiagnostics d = rDiag;
    if (d.summaryZh.isEmpty() && d.issueCode.isEmpty())
        d = diagnoseModelRouting();
    if (d.recoveryGuideZh.isEmpty())
        fillRecoveryGuide(d);

    if (rFailDetail.isEmpty())
        return d.recoveryGuideZh;

    OUStringBuffer out;
    out.append(u"### 本次请求失败\n\n"_ustr);
    out.append(rFailDetail);
    out.append(u"\n\n主文档未改。\n\n"_ustr);
    out.append(d.recoveryGuideZh);
    return out.makeStringAndClear();
}

ModelRoutingDiagnostics diagnoseModelRouting()
{
    // Soft process cache: warm-open often probes twice within one frame budget.
    static std::mutex s_diagMu;
    static ModelRoutingDiagnostics s_diagCache;
    static sal_Int64 s_diagMs = 0;
    static bool s_diagValid = false;
    {
        std::lock_guard<std::mutex> g(s_diagMu);
        if (s_diagValid)
        {
            TimeValue tv{};
            osl_getSystemTime(&tv);
            const sal_Int64 now = static_cast<sal_Int64>(tv.Seconds) * 1000
                                  + static_cast<sal_Int64>(tv.Nanosec) / 1000000;
            const sal_Int64 ttl
                = kqoffice::ai::control::AiResourceEnvelope::routingDiagnoseCacheTtlMs();
            if ((now - s_diagMs) <= ttl)
                return s_diagCache;
        }
    }

    ModelRoutingDiagnostics d;
    ensureDefaultModelRoutingTemplate();
    const ModelRoutingSnapshot routing = loadModelRoutingSnapshot();
    d.backend = routing.backend.isEmpty() ? u"ollama"_ustr : routing.backend.toAsciiLowerCase();
    d.baseUrl = routing.baseUrl;
    d.apiKeyPresent = !OpenAICompatibleAdapter::apiKeyFromEnv().isEmpty();
    d.apiKeyPathHint = homeConfigDir() + u"/api-key"_ustr;
    d.routingConfigPathHint = homeConfigDir() + u"/model-routing.json"_ustr;

    std::vector<OUString> models;

    if (OpenAICompatibleAdapter::isOpenAICompatibleBackend(d.backend))
    {
        const OUString baseUrl
            = routing.baseUrl.isEmpty() ? u"http://127.0.0.1:8080"_ustr : routing.baseUrl;
        d.baseUrl = baseUrl;
        OpenAICompatibleAdapter gw(baseUrl, OpenAICompatibleAdapter::apiKeyFromEnv());
        d.gatewayReachable = (gw.probe() == u"reachable"_ustr);
        if (d.gatewayReachable)
            models = gw.listModels();
        d.installedCount = static_cast<sal_Int32>(models.size());
        // M21: do not probe Ollama on the openai-compatible hot path — offline
        // Ollama adds hundreds of ms of cold-open latency. Hybrid probe is opt-in.
        const char* hybrid = std::getenv("KQOFFICE_AI_PROBE_OLLAMA");
        if (hybrid && *hybrid && hybrid[0] != '0')
        {
            OllamaAdapter ollama;
            d.ollamaReachable = (ollama.probe() == u"reachable"_ustr);
        }
        else
            d.ollamaReachable = false;
    }
    else
    {
        OllamaAdapter adapter;
        const OUString probe = adapter.probe();
        d.ollamaReachable = (probe == u"reachable"_ustr);
        if (d.ollamaReachable)
            models = adapter.listModels();
        d.installedCount = static_cast<sal_Int32>(models.size());
    }

    const auto rPrimary
        = resolveModelForRole(ModelRole::MainLoop, routing, models);
    const auto rLight = resolveModelForRole(ModelRole::Background, routing, models);
    const auto rAgent = resolveModelForRole(ModelRole::Agent, routing, models);
    const auto rPlan = resolveModelForRole(ModelRole::Planner, routing, models);
    const auto rReview = resolveModelForRole(ModelRole::Reviewer, routing, models);

    d.primaryResolved = rPrimary.model;
    d.lightResolved = rLight.model;
    d.agentResolved = rAgent.model;
    d.planResolved = rPlan.model;
    d.reviewResolved = rReview.model;
    // When models list empty but primaryModel is set (e.g. "auto"), still usable.
    if (d.primaryResolved.isEmpty() && !routing.primaryModel.isEmpty())
        d.primaryResolved = routing.primaryModel;
    if (d.lightResolved.isEmpty() && !d.primaryResolved.isEmpty())
        d.lightResolved = d.primaryResolved;
    if (d.reviewResolved.isEmpty() && !d.primaryResolved.isEmpty())
        d.reviewResolved = d.primaryResolved;
    d.lightReady = !d.lightResolved.isEmpty();
    d.reviewReady = !d.reviewResolved.isEmpty();

    OUStringBuffer b;
    if (OpenAICompatibleAdapter::isOpenAICompatibleBackend(d.backend))
    {
        if (!d.gatewayReachable)
        {
            b.append(u"诊断：OpenAI 兼容网关不可达（"_ustr);
            b.append(d.baseUrl.isEmpty() ? u"?"_ustr : d.baseUrl);
            b.append(u"）。请检查网络与 baseUrl。"_ustr);
            d.healthy = false;
            d.issueCode = u"gateway-offline"_ustr;
        }
        else if (!d.apiKeyPresent)
        {
            b.append(u"诊断：网关可达，但未配置 API Key。\n"
                     u"请写入 ~/.config/kqoffice/api-key 或设置 KQOFFICE_AI_API_KEY。"_ustr);
            d.healthy = false;
            d.issueCode = u"missing-key"_ustr;
        }
        else
        {
            b.append(u"诊断：会员网关可达 · openai-compatible · 主="_ustr);
            b.append(d.primaryResolved.isEmpty() ? u"?"_ustr : d.primaryResolved);
            b.append(u" · 会话已配置"_ustr);
            if (d.installedCount > 0)
            {
                b.append(u" · /v1/models="_ustr);
                b.append(d.installedCount);
            }
            else
                b.append(u" · 模型列表空（可用 primaryModel=auto）"_ustr);
            // Membership quota chip (api.03122.com only; best-effort)
            enrichMembershipQuota(d);
            if (!d.membershipQuotaLineZh.isEmpty())
            {
                b.append(u"\n"_ustr);
                b.append(d.membershipQuotaLineZh);
            }
            else if (d.baseUrl.indexOf(u"api.03122.com") >= 0)
            {
                b.append(u"\n会员：会话可能无效 — 请 Device 登录 bin/kqoffice-cloud-login.sh"_ustr);
            }
            d.healthy = !d.primaryResolved.isEmpty();
            d.issueCode = d.healthy ? u"ok"_ustr : u"no-primary"_ustr;
            if (d.healthy && d.membershipDayFastRem == 0)
            {
                // still "healthy" gateway-wise, but surface quota on summary
                b.append(u"\n提示：今日免费额度已用尽，可签到加油包或升级套餐。"_ustr);
            }
        }
    }
    else if (!d.ollamaReachable)
    {
        b.append(u"诊断：Ollama 不可达（127.0.0.1:11434）。后台轻量槽与审查槽暂不可用。"_ustr);
        d.healthy = false;
        d.issueCode = u"ollama-offline"_ustr;
    }
    else
    {
        b.append(u"诊断：Ollama 可达 · 已装模型 "_ustr);
        b.append(d.installedCount);
        b.append(u"\n主="_ustr);
        b.append(d.primaryResolved.isEmpty() ? u"?"_ustr : d.primaryResolved);
        b.append(u"  轻量="_ustr);
        b.append(d.lightResolved.isEmpty() ? u"?"_ustr : d.lightResolved);
        b.append(u"  Agent="_ustr);
        b.append(d.agentResolved.isEmpty() ? u"?"_ustr : d.agentResolved);
        b.append(u"  规划="_ustr);
        b.append(d.planResolved.isEmpty() ? u"?"_ustr : d.planResolved);
        b.append(u"  审查="_ustr);
        b.append(d.reviewResolved.isEmpty() ? u"?"_ustr : d.reviewResolved);
        if (d.lightReady)
            b.append(u"\n轻量槽就绪（总结/抽取/后台）"_ustr);
        else
            b.append(u"\n轻量槽未解析 — 请配置 light 或 primary"_ustr);
        if (d.reviewReady)
            b.append(u" · 审查槽就绪"_ustr);
        else
            b.append(u" · 审查槽未解析"_ustr);
        d.healthy = d.lightReady || !d.primaryResolved.isEmpty();
        if (!d.healthy)
            d.issueCode = u"no-primary"_ustr;
        else if (!d.lightReady || !d.reviewReady)
            d.issueCode = u"degraded"_ustr;
        else
            d.issueCode = u"ok"_ustr;
    }
    d.summaryZh = b.makeStringAndClear();
    fillRecoveryGuide(d);
    {
        std::lock_guard<std::mutex> g(s_diagMu);
        s_diagCache = d;
        TimeValue tv{};
        osl_getSystemTime(&tv);
        s_diagMs = static_cast<sal_Int64>(tv.Seconds) * 1000
                   + static_cast<sal_Int64>(tv.Nanosec) / 1000000;
        s_diagValid = true;
    }
    return d;
}

} // namespace kqoffice::ai

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
