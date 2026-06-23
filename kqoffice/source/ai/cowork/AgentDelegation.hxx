/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W5: Multi-Agent Task Delegation).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Multi-agent coordination primitives:
 *  - AgentTaskDelegation: decompose a task into sub-agent tasks by kind.
 *  - AgentResultMerge: merge sub-agent results into a parent result.
 *  - AgentProgressAggregateBuilder: build progress aggregates and format
 *    for UI display.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_COWORK_AGENTDELEGATION_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_COWORK_AGENTDELEGATION_HXX

#include "AsyncTask.hxx"

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::cowork
{

class AgentTaskDelegation
{
public:
    /// Decompose a task into sub-agent tasks based on its kind.
    /// The envelope's kind and taskId drive the decomposition;
    /// subAgentTasks on the envelope may carry pre-existing assignments.
    static std::vector<SubAgentTask> decompose(
        const AsyncTaskEnvelope& env,
        const OUString& parentTaskId);

    /// Expand a task based on its scenario:
    ///   WeeklyReport    -> data-collector, writer, reviewer
    ///   OutlineToSlides -> outline-generator, slide-designer
    ///   ContractReview  -> clause-analyzer, risk-checker, legal-reviewer
    ///   DataCleanup     -> data-validator, format-optimizer
    static std::vector<SubAgentTask> scenarioDecompose(
        TaskKind kind, const OUString& parentId);
};

class AgentResultMerge
{
public:
    /// Combine multiple sub-agent results into one merged result.
    static AgentTaskResult merge(
        const std::vector<AgentTaskResult>& results,
        const OUString& parentTaskId);

    /// Check if all sub-tasks for a parent are complete (every result
    /// is in a terminal state: Applied, Failed, or Cancelled).
    static bool isAllComplete(
        const std::vector<AgentTaskResult>& results,
        sal_Int32 expectedTotal);
};

class AgentProgressAggregateBuilder
{
public:
    /// Build progress from a list of sub-tasks and their results.
    static AgentProgressAggregate build(
        const OUString& rootTaskId,
        const std::vector<SubAgentTask>& subTasks,
        const std::vector<AgentTaskResult>& results);

    /// Format for UI display, e.g. "3/5 completed, 1 running, 1 pending".
    static OUString formatForUI(
        const AgentProgressAggregate& agg);
};

} // namespace kqoffice::ai::cowork

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
