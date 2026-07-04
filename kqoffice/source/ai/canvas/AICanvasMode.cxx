/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V5: AI Canvas Mode).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Implementation of AICanvasMode — guided document creation workflow.
 */

#include "AICanvasMode.hxx"

#include <AgentChatDiffApplier.hxx>
#include <AgentChatDiffExtractor.hxx>

#include <com/sun/star/ai/ProviderRequest.hpp>
#include <com/sun/star/ai/XProvider.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <comphelper/processfactory.hxx>
#include <osl/time.h>
#include <sal/log.hxx>

namespace kqoffice::ai::canvas
{

namespace
{
sal_Int64 currentTimeMs()
{
    TimeValue tv;
    osl_getSystemTime(&tv);
    return static_cast<sal_Int64>(tv.Seconds) * 1000
         + static_cast<sal_Int64>(tv.Nanosec) / 1000000;
}
}

AICanvasMode::AICanvasMode()
{
}

AICanvasMode::~AICanvasMode()
{
}

// ── Session management ──────────────────────────────────────────────────

CanvasSession AICanvasMode::startSession(CanvasDocType docType,
                                          const OUString& overallGoal)
{
    m_session = CanvasSession{};
    m_session.sessionId = u"canvas-"_ustr + OUString::number(currentTimeMs());
    m_session.docType = docType;
    m_session.overallGoal = overallGoal;
    m_session.estimatedSteps = estimateSteps(overallGoal);
    m_session.state = CanvasState::Describing;
    m_session.currentStep = 0;
    m_session.documentTitle = overallGoal;

    SAL_INFO("kqoffice.ai.canvas",
             "startSession: id=" << m_session.sessionId
                 << " type=" << static_cast<int>(docType)
                 << " goal=\"" << overallGoal << "\""
                 << " estimatedSteps=" << m_session.estimatedSteps);
    return m_session;
}

void AICanvasMode::cancelSession()
{
    m_session.state = CanvasState::Cancelled;
    SAL_INFO("kqoffice.ai.canvas",
             "cancelSession: id=" << m_session.sessionId);
}

// ── Step workflow ───────────────────────────────────────────────────────

CanvasSubmitResult AICanvasMode::submitRequirement(const OUString& requirement)
{
    CanvasSubmitResult result;

    if (!isActive())
    {
        result.error = u"No active canvas session"_ustr;
        return result;
    }

    m_session.state = CanvasState::Generating;

    CanvasStep step;
    step.stepNumber = m_session.steps.size() + 1;
    step.userRequirement = requirement;
    step.state = CanvasState::Generating;
    step.description = u"Step "_ustr + OUString::number(step.stepNumber)
        + u"/" + OUString::number(m_session.estimatedSteps);

    // Build context from prior confirmed steps
    OUString context = buildContext();

    // Generate content via LLM
    step.generatedContent = generateContent(requirement, context);

    // Extract diff plan from generated content
    auto plan = kqoffice::ai::chat::AgentChatDiffExtractor::extract(
        step.generatedContent);
    if (kqoffice::ai::chat::AgentChatDiffExtractor::validate(plan))
    {
        step.diffPlanId = plan.planId;
    }

    m_session.steps.push_back(step);
    m_session.state = CanvasState::Reviewing;

    result.success = true;
    result.step = step;
    result.generatedContent = step.generatedContent;

    SAL_INFO("kqoffice.ai.canvas",
             "submitRequirement: step=" << step.stepNumber
                 << " planId=" << step.diffPlanId);
    return result;
}

CanvasConfirmResult AICanvasMode::confirmStep()
{
    CanvasConfirmResult result;

    if (!isActive() || m_session.steps.empty())
    {
        result.error = u"No step to confirm"_ustr;
        return result;
    }

    if (m_session.currentStep >= static_cast<sal_Int32>(m_session.steps.size()))
    {
        result.error = u"No active step"_ustr;
        return result;
    }

    auto& step = m_session.steps[m_session.currentStep];
    step.state = CanvasState::Confirmed;
    step.confirmed = true;
    m_session.currentStep++;

    SAL_INFO("kqoffice.ai.canvas",
             "confirmStep: step=" << step.stepNumber
                 << " confirmedSteps=" << m_session.currentStep
                 << " estimatedSteps=" << m_session.estimatedSteps);

    // Apply the confirmed step's diff plan to the document
    if (!step.diffPlanId.isEmpty())
    {
        auto plan = kqoffice::ai::chat::AgentChatDiffExtractor::extract(
            step.generatedContent);
        if (kqoffice::ai::chat::AgentChatDiffExtractor::validate(plan))
        {
            kqoffice::ai::chat::AgentChatDiffApplier::apply(plan);
        }
    }

    // Check if we've reached the estimated step count
    if (m_session.currentStep >= m_session.estimatedSteps)
    {
        m_session.state = CanvasState::Completed;
        result.isComplete = true;
        SAL_INFO("kqoffice.ai.canvas", "confirmStep: all steps complete!");
    }
    else
    {
        // Prepare next step
        CanvasStep nextStep;
        nextStep.stepNumber = m_session.steps.size() + 1;
        nextStep.description = u"Step "_ustr + OUString::number(nextStep.stepNumber)
            + u"/" + OUString::number(m_session.estimatedSteps);
        nextStep.state = CanvasState::Describing;
        m_session.state = CanvasState::Describing;
        result.nextStep = nextStep;
        result.isComplete = false;
    }

    result.success = true;
    return result;
}

CanvasSubmitResult AICanvasMode::reviseStep(const OUString& feedback)
{
    CanvasSubmitResult result;

    if (!isActive() || m_session.steps.empty())
    {
        result.error = u"No step to revise"_ustr;
        return result;
    }

    sal_Int32 idx = m_session.currentStep - 1;
    if (idx < 0)
        idx = static_cast<sal_Int32>(m_session.steps.size()) - 1;
    if (idx >= static_cast<sal_Int32>(m_session.steps.size()))
    {
        result.error = u"Invalid step index for revision"_ustr;
        return result;
    }

    m_session.state = CanvasState::Revising;

    auto& step = m_session.steps[idx];
    step.state = CanvasState::Revising;
    step.userRequirement = step.userRequirement + u"\n[修改反馈]: "_ustr + feedback;

    OUString context = buildContext();
    step.generatedContent = generateContent(step.userRequirement, context);

    // Re-extract diff plan
    auto plan = kqoffice::ai::chat::AgentChatDiffExtractor::extract(
        step.generatedContent);
    if (kqoffice::ai::chat::AgentChatDiffExtractor::validate(plan))
    {
        step.diffPlanId = plan.planId;
    }

    m_session.state = CanvasState::Reviewing;

    result.success = true;
    result.step = step;
    result.generatedContent = step.generatedContent;

    SAL_INFO("kqoffice.ai.canvas",
             "reviseStep: step=" << step.stepNumber << " feedback=\"" << feedback << "\"");
    return result;
}

CanvasConfirmResult AICanvasMode::skipStep()
{
    CanvasConfirmResult result;

    if (!isActive() || m_session.steps.empty())
    {
        result.error = u"No step to skip"_ustr;
        return result;
    }

    if (m_session.currentStep >= static_cast<sal_Int32>(m_session.steps.size()))
    {
        result.error = u"No active step"_ustr;
        return result;
    }

    auto& step = m_session.steps[m_session.currentStep];
    step.state = CanvasState::Confirmed;
    step.confirmed = true;
    m_session.currentStep++;

    // Check completion
    if (m_session.currentStep >= m_session.estimatedSteps)
    {
        m_session.state = CanvasState::Completed;
        result.isComplete = true;
    }
    else
    {
        CanvasStep nextStep;
        nextStep.stepNumber = m_session.steps.size() + 1;
        nextStep.description = u"Step "_ustr + OUString::number(nextStep.stepNumber)
            + u"/" + OUString::number(m_session.estimatedSteps);
        nextStep.state = CanvasState::Describing;
        m_session.state = CanvasState::Describing;
        result.nextStep = nextStep;
    }

    result.success = true;
    SAL_INFO("kqoffice.ai.canvas", "skipStep: step=" << step.stepNumber);
    return result;
}

// ── Content application ─────────────────────────────────────────────────

bool AICanvasMode::applyAllSteps()
{
    if (!isActive())
        return false;

    bool allOk = true;
    for (sal_Int32 i = 0; i < m_session.currentStep; i++)
    {
        if (!applyStep(i))
            allOk = false;
    }
    return allOk;
}

bool AICanvasMode::applyStep(sal_Int32 stepIndex)
{
    if (stepIndex < 0 || stepIndex >= static_cast<sal_Int32>(m_session.steps.size()))
        return false;

    auto& step = m_session.steps[stepIndex];
    if (!step.confirmed || step.diffPlanId.isEmpty())
        return false;

    auto plan = kqoffice::ai::chat::AgentChatDiffExtractor::extract(
        step.generatedContent);
    if (!kqoffice::ai::chat::AgentChatDiffExtractor::validate(plan))
        return false;

    auto result = kqoffice::ai::chat::AgentChatDiffApplier::apply(plan);
    SAL_INFO("kqoffice.ai.canvas",
             "applyStep: step=" << step.stepNumber
                 << " success=" << (result.success ? "true" : "false"));
    return result.success;
}

// ── Progress ────────────────────────────────────────────────────────────

double AICanvasMode::progress() const
{
    if (m_session.estimatedSteps <= 0)
        return 0.0;
    return static_cast<double>(m_session.currentStep)
        / static_cast<double>(m_session.estimatedSteps);
}

OUString AICanvasMode::statusString() const
{
    return stateLabel(m_session.state)
        + u" (" + OUString::number(m_session.currentStep) + u"/"
        + OUString::number(m_session.estimatedSteps) + u")";
}

OUString AICanvasMode::stateLabel(CanvasState state)
{
    switch (state)
    {
        case CanvasState::Idle:       return u"等待中"_ustr;
        case CanvasState::Describing: return u"描述需求"_ustr;
        case CanvasState::Generating: return u"AI 生成中"_ustr;
        case CanvasState::Reviewing:  return u"请确认"_ustr;
        case CanvasState::Revising:   return u"修改中"_ustr;
        case CanvasState::Confirmed:  return u"已确认"_ustr;
        case CanvasState::Completed:  return u"完成"_ustr;
        case CanvasState::Cancelled:  return u"已取消"_ustr;
    }
    return u""_ustr;
}

// ── Undo ─────────────────────────────────────────────────────────────────

bool AICanvasMode::undoLastStep()
{
    return kqoffice::ai::chat::AgentChatDiffApplier::undo().success;
}

bool AICanvasMode::canUndo() const
{
    return kqoffice::ai::chat::AgentChatDiffApplier::canUndo();
}

// ── Private ──────────────────────────────────────────────────────────────

OUString AICanvasMode::generateContent(const OUString& requirement,
                                        const OUString& context)
{
    OUString result;
    try
    {
        auto xContext = comphelper::getProcessComponentContext();
        auto xProvider = css::uno::Reference<css::ai::XProvider>(
            xContext->getServiceManager()->createInstanceWithContext(
                u"org.kqoffice.ai.Provider"_ustr, xContext),
            css::uno::UNO_QUERY);

        if (!xProvider.is())
        {
            SAL_WARN("kqoffice.ai.canvas", "generateContent: no provider available");
            return u"[无法连接到 AI 服务]"_ustr;
        }

        css::ai::ProviderRequest req;
        req.capability = docTypeToSurface(m_session.docType) + u"-canvas"_ustr;

        // Build the full prompt with system instructions
        OUString prompt;
        prompt += u"你是一个文档创建助手。用户正在使用画布模式创建文档。\n"_ustr;
        prompt += u"文档类型: "_ustr + docTypeToSurface(m_session.docType) + u"\n"_ustr;
        prompt += u"总体目标: "_ustr + m_session.overallGoal + u"\n"_ustr;
        prompt += u"当前步骤: "_ustr + OUString::number(m_session.currentStep + 1)
            + u"/" + OUString::number(m_session.estimatedSteps) + u"\n"_ustr;

        if (!context.isEmpty())
        {
            prompt += u"\n已完成的内容:\n" + context + u"\n"_ustr;
        }

        prompt += u"\n用户需求: " + requirement + u"\n"_ustr;
        prompt += u"\n请生成当前步骤的文档内容，并用 ```json 格式输出变更操作计划。"_ustr;

        req.prompt = prompt;
        req.timeoutMs = 30000;

        auto response = xProvider->call(req);
        if (response.status == u"ok"_ustr)
        {
            result = response.content;
        }
        else
        {
            SAL_WARN("kqoffice.ai.canvas",
                     "generateContent: provider error status=" << response.status);
            result = u"[AI 服务返回错误: "_ustr + response.status + u"]"_ustr;
        }
    }
    catch (const css::uno::Exception& e)
    {
        SAL_WARN("kqoffice.ai.canvas",
                 "generateContent: exception " << e.Message);
        result = u"[AI 服务异常: "_ustr + e.Message + u"]"_ustr;
    }

    return result;
}

OUString AICanvasMode::buildContext() const
{
    OUString ctx;
    for (const auto& step : m_session.steps)
    {
        if (step.confirmed || step.state == CanvasState::Confirmed)
        {
            if (!ctx.isEmpty())
                ctx += u"\n---\n";
            ctx += u"步骤 " + OUString::number(step.stepNumber) + u":\n"
                + step.generatedContent;
        }
    }
    return ctx;
}

sal_Int32 AICanvasMode::estimateSteps(const OUString& goal)
{
    // Simple heuristic: count characters and map to 3-10 range
    sal_Int32 len = goal.getLength();
    if (len <= 20)
        return 3;
    if (len <= 60)
        return 5;
    if (len <= 120)
        return 7;
    if (len <= 200)
        return 8;
    return 10; // very detailed goal → max steps
}

OUString AICanvasMode::docTypeToSurface(CanvasDocType t)
{
    switch (t)
    {
        case CanvasDocType::Writer:  return u"writer"_ustr;
        case CanvasDocType::Calc:    return u"calc"_ustr;
        case CanvasDocType::Impress: return u"impress"_ustr;
    }
    return u"writer"_ustr;
}

} // namespace kqoffice::ai::canvas

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
