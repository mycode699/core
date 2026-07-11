/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 */

#include "AgentStepRunner.hxx"

#include "Provider.hxx"

#include <com/sun/star/ai/ProviderRequest.hpp>
#include <com/sun/star/ai/ProviderResponse.hpp>
#include <rtl/ustrbuf.hxx>

namespace kqoffice::ai
{
namespace
{
OUString lower(const OUString& s) { return s.toAsciiLowerCase(); }

OUString buildPlanPrompt(const OUString& goal, const OUString& context)
{
    OUStringBuffer b;
    b.append(u"你是可圈office 的规划模型（plan 槽位）。\n"_ustr);
    b.append(u"请为下列目标制定简洁、可执行的步骤计划（中文，编号列表）。\n"_ustr);
    b.append(u"不要修改文档，只输出计划。\n"_ustr);
    b.append(u"目标：\n"_ustr);
    b.append(goal);
    if (!context.isEmpty())
    {
        b.append(u"\n上下文：\n"_ustr);
        b.append(context);
    }
    return b.makeStringAndClear();
}

OUString buildActPrompt(const OUString& goal, const OUString& plan, const OUString& context)
{
    OUStringBuffer b;
    b.append(u"你是可圈office 的执行 Agent（agent 槽位）。\n"_ustr);
    b.append(u"根据计划产出可交付的正文结果。若适合改写文档，可附带结构化 apply-plan JSON；"_ustr);
    b.append(u"否则输出完整草稿。禁止声称已修改用户主文档。\n"_ustr);
    b.append(u"目标：\n"_ustr);
    b.append(goal);
    b.append(u"\n计划：\n"_ustr);
    b.append(plan);
    if (!context.isEmpty())
    {
        b.append(u"\n上下文：\n"_ustr);
        b.append(context);
    }
    return b.makeStringAndClear();
}

OUString buildReviewPrompt(const OUString& goal, const OUString& actOutput)
{
    OUStringBuffer b;
    b.append(u"你是可圈office 的审核模型（review 槽位）。\n"_ustr);
    b.append(u"请审查下列执行结果：指出风险、遗漏与可改进点；给出简短结论"_ustr);
    b.append(u"（通过 / 需修改）。不要修改主文档。\n"_ustr);
    b.append(u"目标：\n"_ustr);
    b.append(goal);
    b.append(u"\n执行结果：\n"_ustr);
    b.append(actOutput);
    return b.makeStringAndClear();
}

OUString buildRolePrompt(const OUString& goal, const OUString& role, const OUString& instruction,
                         const OUString& prior)
{
    OUStringBuffer b;
    b.append(u"你是可圈office 多代理流程中的角色："_ustr);
    b.append(role);
    b.append(u"。\n"_ustr);
    if (!instruction.isEmpty())
    {
        b.append(u"指令：\n"_ustr);
        b.append(instruction);
        b.append(u"\n"_ustr);
    }
    b.append(u"目标：\n"_ustr);
    b.append(goal);
    if (!prior.isEmpty())
    {
        b.append(u"\n上游输出：\n"_ustr);
        b.append(prior);
    }
    b.append(u"\n请给出本角色的完整输出。禁止修改主文档。\n"_ustr);
    return b.makeStringAndClear();
}

void appendStepTranscript(OUStringBuffer& b, const AgentStepResult& step)
{
    b.append(u"### ["_ustr);
    b.append(step.stepKind);
    b.append(u"] capability="_ustr);
    b.append(step.capability);
    b.append(u" status="_ustr);
    b.append(step.status);
    if (!step.evidenceId.isEmpty())
    {
        b.append(u" evidence="_ustr);
        b.append(step.evidenceId);
    }
    b.append(u"\n"_ustr);
    b.append(step.content);
    b.append(u"\n\n"_ustr);
}
} // namespace

OUString AgentStepRunner::capabilityForAgentRole(const OUString& rAgentRole)
{
    const OUString r = lower(rAgentRole);
    if (r.isEmpty())
        return u"agent"_ustr;
    if (r.indexOf(u"plan"_ustr) >= 0 || r.indexOf(u"outline"_ustr) >= 0
        || r == u"planner"_ustr || r.indexOf(u"judge"_ustr) >= 0
        || r.indexOf(u"guide"_ustr) >= 0)
        return u"plan"_ustr;
    if (r.indexOf(u"review"_ustr) >= 0 || r.indexOf(u"critique"_ustr) >= 0
        || r.indexOf(u"legal"_ustr) >= 0 || r.indexOf(u"risk"_ustr) >= 0
        || r.indexOf(u"audit"_ustr) >= 0)
        return u"review"_ustr;
    if (r.indexOf(u"summar"_ustr) >= 0 || r.indexOf(u"extract"_ustr) >= 0
        || r.indexOf(u"classif"_ustr) >= 0 || r.indexOf(u"collect"_ustr) >= 0
        || r.indexOf(u"validat"_ustr) >= 0)
        return u"summarize"_ustr; // light slot
    // writer / actor / designer / agent / mesh / default
    return u"agent"_ustr;
}

AgentStepResult AgentStepRunner::runOne(const OUString& rCapability, const OUString& rPrompt,
                                        sal_Int32 nTimeoutMs)
{
    AgentStepResult out;
    out.capability = rCapability;
    out.stepKind = rCapability;

    if (rCapability.isEmpty() || rPrompt.isEmpty())
    {
        out.status = u"provider-error"_ustr;
        out.content = u"empty capability or prompt"_ustr;
        return out;
    }

    // Reuse full Provider path (ServiceMode + five-slot routing + Ollama + evidence).
    Provider provider;
    css::ai::ProviderRequest req;
    req.capability = rCapability;
    req.prompt = rPrompt;
    req.context = OUString();
    req.timeoutMs = nTimeoutMs > 0 ? nTimeoutMs : 60000;

    try
    {
        const css::ai::ProviderResponse rsp = provider.call(req);
        out.status = rsp.status;
        out.content = rsp.content;
        out.evidenceId = rsp.evidenceId;
        out.durationMs = rsp.durationMs;
    }
    catch (const css::uno::Exception& e)
    {
        out.status = u"provider-error"_ustr;
        out.content = e.Message;
    }
    catch (...)
    {
        out.status = u"provider-error"_ustr;
        out.content = u"unknown provider exception"_ustr;
    }
    return out;
}

AgentPipelineResult AgentStepRunner::runPlanActReview(const OUString& rGoal,
                                                      const OUString& rContext)
{
    AgentPipelineResult pipe;
    pipe.goal = rGoal;
    if (rGoal.isEmpty())
    {
        pipe.failureReason = u"empty-goal"_ustr;
        return pipe;
    }

    OUStringBuffer transcript;

    AgentStepResult plan = runOne(u"plan"_ustr, buildPlanPrompt(rGoal, rContext));
    plan.stepKind = u"plan"_ustr;
    pipe.steps.push_back(plan);
    appendStepTranscript(transcript, plan);
    if (plan.status != u"ok"_ustr)
    {
        pipe.failureReason = u"plan-step-failed status="_ustr + plan.status;
        pipe.combinedContent = transcript.makeStringAndClear();
        pipe.finalEvidenceId = plan.evidenceId;
        return pipe;
    }

    AgentStepResult act
        = runOne(u"agent"_ustr, buildActPrompt(rGoal, plan.content, rContext));
    act.stepKind = u"act"_ustr;
    pipe.steps.push_back(act);
    appendStepTranscript(transcript, act);
    if (act.status != u"ok"_ustr)
    {
        pipe.failureReason = u"act-step-failed status="_ustr + act.status;
        pipe.combinedContent = transcript.makeStringAndClear();
        pipe.finalEvidenceId = act.evidenceId;
        return pipe;
    }

    AgentStepResult review = runOne(u"review"_ustr, buildReviewPrompt(rGoal, act.content));
    review.stepKind = u"review"_ustr;
    pipe.steps.push_back(review);
    appendStepTranscript(transcript, review);

    pipe.combinedContent = transcript.makeStringAndClear();
    pipe.applyCandidateContent = act.content;
    pipe.finalEvidenceId = !review.evidenceId.isEmpty() ? review.evidenceId : act.evidenceId;
    // Pipeline succeeds if plan+act ok; review failure is soft (still return act for staging).
    if (review.status != u"ok"_ustr)
        pipe.failureReason = u"review-step-soft-fail status="_ustr + review.status;
    pipe.success = true;
    return pipe;
}

AgentPipelineResult AgentStepRunner::runRoleSequence(
    const OUString& rGoal, const std::vector<OUString>& rAgentRoles,
    const std::vector<OUString>& rInstructions, bool bContinueOnError)
{
    AgentPipelineResult pipe;
    pipe.goal = rGoal;
    if (rGoal.isEmpty() || rAgentRoles.empty())
    {
        pipe.failureReason = u"empty-goal-or-roles"_ustr;
        return pipe;
    }

    OUStringBuffer transcript;
    OUString prior;
    const sal_Int32 n = static_cast<sal_Int32>(rAgentRoles.size());
    for (sal_Int32 i = 0; i < n; ++i)
    {
        const OUString& role = rAgentRoles[static_cast<size_t>(i)];
        const OUString instruction
            = (static_cast<size_t>(i) < rInstructions.size())
                  ? rInstructions[static_cast<size_t>(i)]
                  : OUString();
        const OUString cap = capabilityForAgentRole(role);
        AgentStepResult step
            = runOne(cap, buildRolePrompt(rGoal, role, instruction, prior));
        step.stepKind = role;
        pipe.steps.push_back(step);
        appendStepTranscript(transcript, step);
        if (step.status != u"ok"_ustr)
        {
            pipe.finalEvidenceId = step.evidenceId;
            if (!bContinueOnError)
            {
                pipe.failureReason
                    = u"role-step-failed role="_ustr + role + u" status="_ustr + step.status;
                pipe.combinedContent = transcript.makeStringAndClear();
                return pipe;
            }
            continue;
        }
        prior = step.content;
        pipe.applyCandidateContent = step.content;
        pipe.finalEvidenceId = step.evidenceId;
    }

    pipe.combinedContent = transcript.makeStringAndClear();
    pipe.success = !pipe.steps.empty()
                   && pipe.steps.back().status == u"ok"_ustr;
    if (!pipe.success && pipe.failureReason.isEmpty())
        pipe.failureReason = u"role-sequence-incomplete"_ustr;
    return pipe;
}

} // namespace kqoffice::ai

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
