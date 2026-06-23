/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W5: Multi-Agent Task Delegation).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AgentDelegation.hxx"
#include "TaskStateMachine.hxx"

#include <sal/log.hxx>

#include <algorithm>
#include <map>

namespace kqoffice::ai::cowork
{

namespace
{

OUString makeSubTaskId(const OUString& parentId, sal_Int32 stepOrder)
{
    return parentId + "-sub-" + OUString::number(stepOrder);
}

} // namespace

/* static */
std::vector<SubAgentTask> AgentTaskDelegation::decompose(
    const AsyncTaskEnvelope& env, const OUString& parentTaskId)
{
    SAL_INFO("kqoffice.ai.cowork.agent",
             "AgentTaskDelegation::decompose parent="
                 << parentTaskId
                 << " kind=" << taskKindToken(env.kind)
                 << " existing_subs=" << env.subAgentTasks.size());

    // If the envelope already carries pre-assigned sub-agent tasks, return
    // them directly. Otherwise decompose from the scenario.
    if (!env.subAgentTasks.empty())
        return env.subAgentTasks;

    return scenarioDecompose(env.kind, parentTaskId);
}

/* static */
std::vector<SubAgentTask> AgentTaskDelegation::scenarioDecompose(
    TaskKind kind, const OUString& parentId)
{
    std::vector<SubAgentTask> subs;

    switch (kind)
    {
        case TaskKind::WeeklyReport:
        {
            // 3 sub-agents: data-collector, writer, reviewer
            SubAgentTask collector;
            collector.subTaskId = makeSubTaskId(parentId, 0);
            collector.parentTaskId = parentId;
            collector.agentRole = u"data-collector"_ustr;
            collector.instruction = u"从源文档中提取本周活动数据"_ustr;
            collector.stepOrder = 0;
            subs.push_back(collector);

            SubAgentTask writer;
            writer.subTaskId = makeSubTaskId(parentId, 1);
            writer.parentTaskId = parentId;
            writer.agentRole = u"writer"_ustr;
            writer.instruction = u"根据收集的数据撰写周报初稿"_ustr;
            writer.stepOrder = 1;
            writer.dependsOn = collector.subTaskId;
            subs.push_back(writer);

            SubAgentTask reviewer;
            reviewer.subTaskId = makeSubTaskId(parentId, 2);
            reviewer.parentTaskId = parentId;
            reviewer.agentRole = u"reviewer"_ustr;
            reviewer.instruction = u"审校周报内容，修正格式与措辞"_ustr;
            reviewer.stepOrder = 2;
            reviewer.dependsOn = writer.subTaskId;
            subs.push_back(reviewer);

            SAL_INFO("kqoffice.ai.cowork.agent",
                     "scenarioDecompose WeeklyReport -> 3 sub-agents parent="
                         << parentId);
            break;
        }
        case TaskKind::OutlineToSlides:
        {
            // 2 sub-agents: outline-generator, slide-designer
            SubAgentTask outline;
            outline.subTaskId = makeSubTaskId(parentId, 0);
            outline.parentTaskId = parentId;
            outline.agentRole = u"outline-generator"_ustr;
            outline.instruction = u"将文档大纲转换为幻灯片结构"_ustr;
            outline.stepOrder = 0;
            subs.push_back(outline);

            SubAgentTask designer;
            designer.subTaskId = makeSubTaskId(parentId, 1);
            designer.parentTaskId = parentId;
            designer.agentRole = u"slide-designer"_ustr;
            designer.instruction = u"为幻灯片结构配置版式和视觉元素"_ustr;
            designer.stepOrder = 1;
            designer.dependsOn = outline.subTaskId;
            subs.push_back(designer);

            SAL_INFO("kqoffice.ai.cowork.agent",
                     "scenarioDecompose OutlineToSlides -> 2 sub-agents parent="
                         << parentId);
            break;
        }
        case TaskKind::ContractReview:
        {
            // 3 sub-agents: clause-analyzer, risk-checker, legal-reviewer
            SubAgentTask analyzer;
            analyzer.subTaskId = makeSubTaskId(parentId, 0);
            analyzer.parentTaskId = parentId;
            analyzer.agentRole = u"clause-analyzer"_ustr;
            analyzer.instruction = u"解析合同条款，提取关键义务与权利"_ustr;
            analyzer.stepOrder = 0;
            subs.push_back(analyzer);

            SubAgentTask risk;
            risk.subTaskId = makeSubTaskId(parentId, 1);
            risk.parentTaskId = parentId;
            risk.agentRole = u"risk-checker"_ustr;
            risk.instruction = u"检查条款中的��险点与不合理之处"_ustr;
            risk.stepOrder = 1;
            risk.dependsOn = analyzer.subTaskId;
            subs.push_back(risk);

            SubAgentTask legal;
            legal.subTaskId = makeSubTaskId(parentId, 2);
            legal.parentTaskId = parentId;
            legal.agentRole = u"legal-reviewer"_ustr;
            legal.instruction = u"从法律合规角度审校并提出修改建议"_ustr;
            legal.stepOrder = 2;
            legal.dependsOn = risk.subTaskId;
            subs.push_back(legal);

            SAL_INFO("kqoffice.ai.cowork.agent",
                     "scenarioDecompose ContractReview -> 3 sub-agents parent="
                         << parentId);
            break;
        }
        case TaskKind::DataCleanup:
        {
            // 2 sub-agents: data-validator, format-optimizer
            SubAgentTask validator;
            validator.subTaskId = makeSubTaskId(parentId, 0);
            validator.parentTaskId = parentId;
            validator.agentRole = u"data-validator"_ustr;
            validator.instruction = u"校验数据一致性，标记异常值"_ustr;
            validator.stepOrder = 0;
            subs.push_back(validator);

            SubAgentTask formatter;
            formatter.subTaskId = makeSubTaskId(parentId, 1);
            formatter.parentTaskId = parentId;
            formatter.agentRole = u"format-optimizer"_ustr;
            formatter.instruction = u"优化数据格式并生成清理后的数据集"_ustr;
            formatter.stepOrder = 1;
            formatter.dependsOn = validator.subTaskId;
            subs.push_back(formatter);

            SAL_INFO("kqoffice.ai.cowork.agent",
                     "scenarioDecompose DataCleanup -> 2 sub-agents parent="
                         << parentId);
            break;
        }
    }

    return subs;
}

/* static */
AgentTaskResult AgentResultMerge::merge(
    const std::vector<AgentTaskResult>& results,
    const OUString& parentTaskId)
{
    AgentTaskResult merged;
    merged.parentTaskId = parentTaskId;
    merged.subTaskId = parentTaskId; // merged result carries parent id

    bool allApplied = true;
    bool anyFailed = false;
    std::vector<OUString> allEvidence;
    OUStringBuffer summaryBuf;

    for (const auto& r : results)
    {
        if (r.state == TaskState::Failed)
        {
            anyFailed = true;
            allApplied = false;
        }
        else if (r.state != TaskState::Applied)
        {
            allApplied = false;
        }

        if (!r.summary.isEmpty())
        {
            if (!summaryBuf.isEmpty())
                summaryBuf.append('\n');
            summaryBuf.append(r.summary);
        }

        for (const auto& eid : r.evidenceIds)
            allEvidence.push_back(eid);

        for (const auto& ns : r.nextSteps)
            merged.nextSteps.push_back(ns);
    }

    merged.state = allApplied ? TaskState::Applied
                   : (anyFailed ? TaskState::Failed
                                : TaskState::Running);
    merged.summary = summaryBuf.makeStringAndClear();
    merged.evidenceIds = std::move(allEvidence);

    SAL_INFO("kqoffice.ai.cowork.agent",
             "AgentResultMerge::merge parent="
                 << parentTaskId
                 << " count=" << results.size()
                 << " merged_state=" << taskStateToken(merged.state)
                 << " next_steps=" << merged.nextSteps.size());

    return merged;
}

/* static */
bool AgentResultMerge::isAllComplete(
    const std::vector<AgentTaskResult>& results,
    sal_Int32 expectedTotal)
{
    if (static_cast<sal_Int32>(results.size()) < expectedTotal)
        return false;

    for (const auto& r : results)
    {
        if (!isTerminalTaskState(r.state))
            return false;
    }

    SAL_INFO("kqoffice.ai.cowork.agent",
             "AgentResultMerge::isAllComplete total="
                 << results.size()
                 << " expected=" << expectedTotal
                 << " all_terminal=true");

    return true;
}

/* static */
AgentProgressAggregate AgentProgressAggregateBuilder::build(
    const OUString& rootTaskId,
    const std::vector<SubAgentTask>& subTasks,
    const std::vector<AgentTaskResult>& results)
{
    AgentProgressAggregate agg;
    agg.rootTaskId = rootTaskId;
    agg.totalSteps = static_cast<sal_Int32>(subTasks.size());
    agg.completedSteps = 0;
    agg.failedSteps = 0;
    agg.pendingSteps = 0;
    agg.runningSteps = 0;

    // Index results by subTaskId for fast lookup.
    std::map<OUString, TaskState> resultState;
    for (const auto& r : results)
        resultState[r.subTaskId] = r.state;

    for (const auto& sub : subTasks)
    {
        auto it = resultState.find(sub.subTaskId);
        if (it == resultState.end())
        {
            // No result yet -> pending.
            ++agg.pendingSteps;
        }
        else
        {
            switch (it->second)
            {
                case TaskState::Applied:
                    ++agg.completedSteps;
                    break;
                case TaskState::Failed:
                case TaskState::Cancelled:
                    ++agg.failedSteps;
                    break;
                case TaskState::Running:
                    ++agg.runningSteps;
                    agg.activeSubTasks.push_back(sub.subTaskId);
                    break;
                case TaskState::Pending:
                case TaskState::AwaitingReview:
                    ++agg.pendingSteps;
                    break;
            }
        }
    }

    SAL_INFO("kqoffice.ai.cowork.agent",
             "AgentProgressAggregateBuilder::build root="
                 << rootTaskId
                 << " total=" << agg.totalSteps
                 << " completed=" << agg.completedSteps
                 << " failed=" << agg.failedSteps
                 << " pending=" << agg.pendingSteps
                 << " running=" << agg.runningSteps);

    return agg;
}

/* static */
OUString AgentProgressAggregateBuilder::formatForUI(
    const AgentProgressAggregate& agg)
{
    if (agg.totalSteps == 0)
        return u""_ustr;

    OUStringBuffer buf;
    buf.append(OUString::number(agg.completedSteps));
    buf.append("/");
    buf.append(OUString::number(agg.totalSteps));

    if (agg.failedSteps > 0)
    {
        buf.append(" (" + OUString::number(agg.failedSteps) + " failed)");
    }

    if (agg.runningSteps > 0)
    {
        buf.append(", " + OUString::number(agg.runningSteps) + " running");
    }

    if (agg.pendingSteps > 0)
    {
        buf.append(", " + OUString::number(agg.pendingSteps) + " pending");
    }

    return buf.makeStringAndClear();
}

} // namespace kqoffice::ai::cowork

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
