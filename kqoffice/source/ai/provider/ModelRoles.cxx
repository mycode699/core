/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 */

#include "ModelRoles.hxx"
#include "ModelRoutingConfig.hxx"
#include "OllamaAdapter.hxx"

#include <rtl/ustrbuf.hxx>

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

OUString normalizeCapabilityHint(const OUString& rHint)
{
    const OUString c = rHint.toAsciiLowerCase().trim();
    if (c.isEmpty())
        return u"chat"_ustr;
    if (c == u"rewrite"_ustr || c == u"polish"_ustr || c == u"edit"_ustr
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

ModelRoutingDiagnostics diagnoseModelRouting()
{
    ModelRoutingDiagnostics d;
    ensureDefaultModelRoutingTemplate();
    const ModelRoutingSnapshot routing = loadModelRoutingSnapshot();

    OllamaAdapter adapter;
    const OUString probe = adapter.probe();
    d.ollamaReachable = (probe == u"reachable"_ustr);
    std::vector<OUString> models;
    if (d.ollamaReachable)
        models = adapter.listModels();
    d.installedCount = static_cast<sal_Int32>(models.size());

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
    d.lightReady = !d.lightResolved.isEmpty();
    d.reviewReady = !d.reviewResolved.isEmpty();

    OUStringBuffer b;
    if (!d.ollamaReachable)
    {
        b.append(u"诊断：Ollama 不可达（127.0.0.1:11434）。后台轻量槽与审查槽暂不可用。"_ustr);
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
    }
    d.summaryZh = b.makeStringAndClear();
    return d;
}

} // namespace kqoffice::ai

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
