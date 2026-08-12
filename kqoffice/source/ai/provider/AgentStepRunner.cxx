/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 */

#include "AgentStepRunner.hxx"

#include "Provider.hxx"

#include <DocumentAIFormulaDryRun.hxx>

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

OUString buildCeilingPlanPrompt(const OUString& goal, const OUString& context,
                                const OUString& surface, const OUString& docTools)
{
    OUStringBuffer b;
    b.append(u"你是可圈办公 Agent Mode 规划器（plan 槽）。\n"_ustr);
    if (surface == u"calc"_ustr)
    {
        b.append(u"【Calc Agent 四阶段】请严格按下列编号输出计划（可增删细节，但阶段齐全）：\n"_ustr);
        b.append(u"1. 探查：列类型/空值/异常（只读描述）\n"_ustr);
        b.append(u"2. 清洗：旁列公式或清洗写回块\n"_ustr);
        b.append(u"3. 汇总：合计/均值等公式写回块\n"_ustr);
        b.append(u"4. 图表建议：推荐图表类型与数据范围（不自动插入）\n"_ustr);
        b.append(u"5. 等待用户批准写回（禁止声称已改表）\n"_ustr);
    }
    else
    {
        b.append(u"为用户目标输出 3–7 步可执行计划（中文编号列表）。\n"_ustr);
        b.append(u"每步一行：动词开头、可验证。禁止修改主文档。\n"_ustr);
        b.append(u"若涉及写回，最后一步必须是「生成可批准写回块并等待用户批准」。\n"_ustr);
    }
    if (!surface.isEmpty() && surface != u"none"_ustr)
    {
        b.append(u"当前表面："_ustr);
        b.append(surface);
        b.append(u"\n"_ustr);
    }
    b.append(u"目标：\n"_ustr);
    b.append(goal);
    if (!context.isEmpty())
    {
        b.append(u"\n用户上下文：\n"_ustr);
        b.append(context);
    }
    if (!docTools.isEmpty())
    {
        b.append(u"\n文档工具摘要（skeleton/块）：\n"_ustr);
        OUString dt = docTools;
        if (dt.getLength() > 6000)
            dt = dt.copy(0, 6000) + u"…"_ustr;
        b.append(dt);
    }
    return b.makeStringAndClear();
}

OUString buildCeilingActPrompt(const OUString& goal, const OUString& plan,
                               const OUString& context, const OUString& surface,
                               const OUString& docTools)
{
    OUStringBuffer b;
    b.append(u"你是可圈办公 Agent Mode 执行器（agent 槽）。\n"_ustr);
    b.append(u"根据计划产出完整交付物。禁止声称已修改主文档。\n"_ustr);
    b.append(u"若需要写回主文档，必须附机器可解析块（用户批准后才写）：\n"_ustr);
    if (surface == u"calc"_ustr)
    {
        b.append(u"【Calc Agent 交付结构 — 按章节输出】\n"_ustr);
        b.append(u"## 1. 探查\n列类型、空值、异常（中文要点）\n"_ustr);
        b.append(u"## 2. 清洗\n===可圈清洗写回=== 下 cell:旁列|=公式|说明\n"_ustr);
        b.append(u"## 3. 汇总\n===可圈公式写回=== 下 cell:格|=公式|说明\n"_ustr);
        b.append(u"## 4. 图表建议\n推荐图表类型 + 数据范围（如 range:A1:B10）；不声称已插入\n"_ustr);
        b.append(u"- 公式：===可圈公式写回=== 下 cell:格|=公式|说明\n"_ustr);
        b.append(u"- 清洗旁列：===可圈清洗写回=== 下 cell:旁列|=公式|说明\n"_ustr);
    }
    else if (surface == u"impress"_ustr)
    {
        b.append(u"- 大纲成片：## N. 标题 / - 要点 / 讲稿：…\n"_ustr);
        b.append(u"- 仅讲稿：===可圈讲稿写回=== 下 slide:N|讲稿|口播\n"_ustr);
    }
    else
    {
        b.append(u"- 大纲样式：===可圈大纲写回=== 下 para:N|H1|标题\n"_ustr);
        b.append(u"- 审阅修复：===可圈审阅修复=== 下 FIX|旧|新\n"_ustr);
        b.append(u"- 或选区改写：给出完整改写正文\n"_ustr);
    }
    b.append(u"目标：\n"_ustr);
    b.append(goal);
    b.append(u"\n计划：\n"_ustr);
    b.append(plan);
    if (!context.isEmpty())
    {
        b.append(u"\n上下文：\n"_ustr);
        b.append(context);
    }
    if (!docTools.isEmpty())
    {
        b.append(u"\n文档工具材料：\n"_ustr);
        OUString dt = docTools;
        if (dt.getLength() > 8000)
            dt = dt.copy(0, 8000) + u"…"_ustr;
        b.append(dt);
    }
    return b.makeStringAndClear();
}

OUString buildCeilingReviewPrompt(const OUString& goal, const OUString& actOutput,
                                  const OUString& surface)
{
    OUStringBuffer b;
    b.append(u"你是可圈办公 Agent Mode 审核器（review 槽）。\n"_ustr);
    b.append(u"审查执行结果：事实风险、是否误改范围、写回块是否可解析。\n"_ustr);
    b.append(u"结论首行：通过 或 需修改。不要改主文档。\n"_ustr);
    if (!surface.isEmpty())
    {
        b.append(u"表面："_ustr);
        b.append(surface);
        b.append(u"\n"_ustr);
    }
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

AgentStepResult makeLocalVerifyStep(const OUString& actBody)
{
    AgentStepResult verify;
    verify.stepKind = u"verify"_ustr;
    verify.capability = u"local-verify"_ustr;
    const bool hasMarker = actBody.indexOf(u"===可圈"_ustr) >= 0 || actBody.indexOf(u"```json"_ustr) >= 0
                           || actBody.indexOf(u"## "_ustr) >= 0 || actBody.indexOf(u"FIX|"_ustr) >= 0
                           || actBody.indexOf(u"cell:"_ustr) >= 0 || actBody.indexOf(u"slide:"_ustr) >= 0
                           || actBody.trim().startsWith(u"="_ustr) || actBody.getLength() > 40;
    OUStringBuffer content;
    if (hasMarker)
        content.append(u"写回候选：可尝试分期 ApplyPlan（仍须用户批准）"_ustr);
    else
        content.append(u"写回候选较弱：可仅作咨询正文，用户可改指令重跑"_ustr);

    // Soft formula dry-run when act looks formula-ish (selection snapshot if Calc).
    if (actBody.indexOf(u"="_ustr) >= 0
        || actBody.indexOf(u"===可圈公式"_ustr) >= 0
        || actBody.indexOf(u"===可圈清洗"_ustr) >= 0)
    {
        const auto snap
            = kqoffice::ai::chat::DocumentAIFormulaDryRun::captureSelectionSnapshot(256);
        const auto dry = kqoffice::ai::chat::DocumentAIFormulaDryRun::checkText(
            actBody, snap.empty() ? nullptr : &snap);
        if (dry.hasWork())
        {
            content.append(u"\n"_ustr);
            content.append(dry.summaryZh);
            if (dry.badCount > 0)
                content.append(u" · 建议修正公式后再点继续/批准"_ustr);
        }
    }
    verify.status = u"ok"_ustr;
    verify.content = content.makeStringAndClear();
    return verify;
}

OUString normalizeSurface(const OUString& rSurface)
{
    return rSurface.isEmpty() ? u"none"_ustr : rSurface.toAsciiLowerCase();
}

AgentStepResult makeBindStep(const OUString& surface, const OUString& rDocToolsContext,
                             const OUString& rContext)
{
    AgentStepResult bind;
    bind.stepKind = u"bind"_ustr;
    bind.capability = u"document-tools"_ustr;
    if (!rDocToolsContext.isEmpty() || !rContext.isEmpty())
    {
        bind.status = u"ok"_ustr;
        OUStringBuffer bc;
        bc.append(u"表面="_ustr);
        bc.append(surface);
        bc.append(u" · 已绑定文档上下文"_ustr);
        if (!rDocToolsContext.isEmpty())
        {
            bc.append(u" · tools="_ustr);
            bc.append(OUString::number(rDocToolsContext.getLength()));
            bc.append(u" 字"_ustr);
        }
        bind.content = bc.makeStringAndClear();
    }
    else
    {
        bind.status = u"ok"_ustr;
        bind.content = u"无额外文档工具材料（将仅用用户目标）"_ustr;
    }
    return bind;
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

AgentPipelineResult AgentStepRunner::runCeilingPlanPhase(const OUString& rGoal,
                                                         const OUString& rContext,
                                                         const OUString& rSurface,
                                                         const OUString& rDocToolsContext)
{
    AgentPipelineResult pipe;
    pipe.goal = rGoal;
    if (rGoal.isEmpty())
    {
        pipe.failureReason = u"empty-goal"_ustr;
        return pipe;
    }

    const OUString surface = normalizeSurface(rSurface);
    OUStringBuffer transcript;

    AgentStepResult bind = makeBindStep(surface, rDocToolsContext, rContext);
    pipe.steps.push_back(bind);
    appendStepTranscript(transcript, bind);

    AgentStepResult plan = runOne(
        u"plan"_ustr, buildCeilingPlanPrompt(rGoal, rContext, surface, rDocToolsContext), 45000);
    plan.stepKind = u"plan"_ustr;
    pipe.steps.push_back(plan);
    appendStepTranscript(transcript, plan);
    pipe.combinedContent = transcript.makeStringAndClear();
    pipe.finalEvidenceId = plan.evidenceId;
    if (plan.status != u"ok"_ustr)
    {
        pipe.failureReason = u"plan-step-failed status="_ustr + plan.status;
        return pipe;
    }
    // Gate marker for UI: plan ready, waiting for human Continue before act.
    pipe.failureReason = u"awaiting-continue"_ustr;
    pipe.success = true;
    return pipe;
}

AgentPipelineResult AgentStepRunner::runCeilingExecutePhase(
    const OUString& rGoal, const OUString& rPlanContent, const OUString& rContext,
    const OUString& rSurface, const OUString& rDocToolsContext)
{
    AgentPipelineResult pipe;
    pipe.goal = rGoal;
    if (rGoal.isEmpty())
    {
        pipe.failureReason = u"empty-goal"_ustr;
        return pipe;
    }
    if (rPlanContent.isEmpty())
    {
        pipe.failureReason = u"empty-plan"_ustr;
        return pipe;
    }

    const OUString surface = normalizeSurface(rSurface);
    OUStringBuffer transcript;

    AgentStepResult act = runOne(
        u"agent"_ustr,
        buildCeilingActPrompt(rGoal, rPlanContent, rContext, surface, rDocToolsContext), 90000);
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

    AgentStepResult review
        = runOne(u"review"_ustr, buildCeilingReviewPrompt(rGoal, act.content, surface), 45000);
    review.stepKind = u"review"_ustr;
    pipe.steps.push_back(review);
    appendStepTranscript(transcript, review);

    AgentStepResult verify = makeLocalVerifyStep(act.content);
    pipe.steps.push_back(verify);
    appendStepTranscript(transcript, verify);

    pipe.combinedContent = transcript.makeStringAndClear();
    pipe.applyCandidateContent = act.content;
    pipe.finalEvidenceId = !review.evidenceId.isEmpty() ? review.evidenceId : act.evidenceId;
    if (review.status != u"ok"_ustr)
        pipe.failureReason = u"review-step-soft-fail status="_ustr + review.status;
    pipe.success = true;
    return pipe;
}

AgentPipelineResult AgentStepRunner::runCeilingMode(const OUString& rGoal, const OUString& rContext,
                                                    const OUString& rSurface,
                                                    const OUString& rDocToolsContext)
{
    AgentPipelineResult planPipe
        = runCeilingPlanPhase(rGoal, rContext, rSurface, rDocToolsContext);
    if (!planPipe.success)
        return planPipe;

    OUString planContent;
    for (const auto& st : planPipe.steps)
    {
        if (st.stepKind == u"plan"_ustr)
        {
            planContent = st.content;
            break;
        }
    }

    AgentPipelineResult execPipe = runCeilingExecutePhase(rGoal, planContent, rContext, rSurface,
                                                          rDocToolsContext);

    AgentPipelineResult pipe;
    pipe.goal = rGoal;
    pipe.steps = planPipe.steps;
    for (const auto& st : execPipe.steps)
        pipe.steps.push_back(st);
    OUStringBuffer combined;
    combined.append(planPipe.combinedContent);
    combined.append(execPipe.combinedContent);
    pipe.combinedContent = combined.makeStringAndClear();
    pipe.applyCandidateContent = execPipe.applyCandidateContent;
    pipe.finalEvidenceId = !execPipe.finalEvidenceId.isEmpty() ? execPipe.finalEvidenceId
                                                               : planPipe.finalEvidenceId;
    pipe.failureReason = execPipe.failureReason;
    pipe.success = execPipe.success;
    if (!pipe.success && pipe.failureReason.isEmpty())
        pipe.failureReason = u"ceiling-execute-failed"_ustr;
    return pipe;
}

AgentPipelineResult AgentStepRunner::runCalcAgentMode(const OUString& rGoal,
                                                      const OUString& rContext,
                                                      const OUString& rDocToolsContext)
{
    // Force calc surface prompts (probe → clean → aggregate → chart).
    AgentPipelineResult pipe = runCeilingMode(rGoal, rContext, u"calc"_ustr, rDocToolsContext);
    if (!pipe.success)
        return pipe;

    // Extra sandbox step (detailed) after pipeline for UI visibility.
    AgentStepResult sandbox;
    sandbox.stepKind = u"sandbox"_ustr;
    sandbox.capability = u"formula-sandbox"_ustr;
    const OUString body
        = !pipe.applyCandidateContent.isEmpty() ? pipe.applyCandidateContent : pipe.combinedContent;
    const auto dry = kqoffice::ai::chat::DocumentAIFormulaDryRun::checkText(body);
    sandbox.status = u"ok"_ustr;
    if (!dry.hasWork())
        sandbox.content = u"公式沙箱：未检出公式候选 · 仍可作咨询正文"_ustr;
    else
    {
        OUStringBuffer b;
        b.append(dry.summaryZh);
        sal_Int32 shown = 0;
        for (const auto& it : dry.items)
        {
            if (it.sandboxStatus.isEmpty() || it.sandboxStatus == u"none"_ustr)
                continue;
            if (++shown > 4)
                break;
            b.append(u"\n· "_ustr);
            b.append(it.formula);
            b.append(u" → "_ustr);
            b.append(it.sandboxNote.isEmpty() ? it.sandboxStatus : it.sandboxNote);
        }
        if (dry.badCount > 0)
            b.append(u"\n建议：修正失败公式后再批准写回"_ustr);
        sandbox.content = b.makeStringAndClear();
    }
    pipe.steps.push_back(sandbox);
    OUStringBuffer combined;
    combined.append(pipe.combinedContent);
    combined.append(u"### [sandbox] formula-sandbox\n"_ustr);
    combined.append(sandbox.content);
    combined.append(u"\n"_ustr);
    pipe.combinedContent = combined.makeStringAndClear();
    return pipe;
}

std::vector<OUString> AgentStepRunner::parsePlanStepTitles(const OUString& rPlanMarkdown)
{
    std::vector<OUString> out;
    if (rPlanMarkdown.isEmpty())
        return out;
    sal_Int32 pos = 0;
    while (pos < rPlanMarkdown.getLength() && out.size() < 8)
    {
        sal_Int32 nl = rPlanMarkdown.indexOf(u'\n', pos);
        if (nl < 0)
            nl = rPlanMarkdown.getLength();
        OUString line = rPlanMarkdown.copy(pos, nl - pos).trim();
        pos = nl + 1;
        if (line.isEmpty())
            continue;
        // 1. xxx  or 1、xxx  or - xxx
        sal_Int32 i = 0;
        while (i < line.getLength() && line[i] >= u'0' && line[i] <= u'9')
            ++i;
        if (i > 0 && i < line.getLength()
            && (line[i] == u'.' || line[i] == u'、' || line[i] == u')'))
        {
            ++i;
            while (i < line.getLength() && line[i] == u' ')
                ++i;
            OUString title = line.copy(i).trim();
            if (!title.isEmpty())
                out.push_back(title);
            continue;
        }
        if (line.startsWith(u"-"_ustr) || line.startsWith(u"*"_ustr))
        {
            sal_Int32 j = 0;
            while (j < line.getLength()
                   && (line[j] == u'-' || line[j] == u'*' || line[j] == u' '))
                ++j;
            OUString title = line.copy(j).trim();
            if (!title.isEmpty())
                out.push_back(title);
        }
    }
    return out;
}

} // namespace kqoffice::ai

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
