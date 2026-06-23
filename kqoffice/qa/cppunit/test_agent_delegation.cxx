/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W5: Multi-Agent Delegation Tests).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Unit tests for the multi-agent coordination primitives:
 *   1. scenarioDecompose for each TaskKind
 *   2. decompose with valid envelope
 *   3. merge with all-success results
 *   4. merge with mixed success/failure
 *   5. progress aggregate calculation
 *   6. progress formatting
 *
 * Pure-logic -- no URE / VCL bootstrap.
 */

#include <sal/types.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/plugin/TestPlugIn.h>

#include <rtl/ustring.hxx>

#include "AgentDelegation.hxx"
#include "AsyncTask.hxx"
#include "TaskStateMachine.hxx"

using namespace kqoffice::ai::cowork;

namespace
{

class AgentDelegationTest : public CppUnit::TestFixture
{
public:
    void testScenarioDecompose_WeeklyReport()
    {
        std::vector<SubAgentTask> subs
            = AgentTaskDelegation::scenarioDecompose(
                TaskKind::WeeklyReport, u"tk-20260623-001"_ustr);

        CPPUNIT_ASSERT_EQUAL(static_cast<std::size_t>(3), subs.size());

        // data-collector (order 0, no dependency)
        CPPUNIT_ASSERT_EQUAL(u"data-collector"_ustr, subs[0].agentRole);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(0), subs[0].stepOrder);
        CPPUNIT_ASSERT(subs[0].dependsOn.isEmpty());
        CPPUNIT_ASSERT_EQUAL(u"tk-20260623-001"_ustr, subs[0].parentTaskId);

        // writer (order 1, depends on data-collector)
        CPPUNIT_ASSERT_EQUAL(u"writer"_ustr, subs[1].agentRole);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), subs[1].stepOrder);
        CPPUNIT_ASSERT_EQUAL(subs[0].subTaskId, subs[1].dependsOn);

        // reviewer (order 2, depends on writer)
        CPPUNIT_ASSERT_EQUAL(u"reviewer"_ustr, subs[2].agentRole);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(2), subs[2].stepOrder);
        CPPUNIT_ASSERT_EQUAL(subs[1].subTaskId, subs[2].dependsOn);
    }

    void testScenarioDecompose_OutlineToSlides()
    {
        std::vector<SubAgentTask> subs
            = AgentTaskDelegation::scenarioDecompose(
                TaskKind::OutlineToSlides, u"tk-20260623-002"_ustr);

        CPPUNIT_ASSERT_EQUAL(static_cast<std::size_t>(2), subs.size());

        CPPUNIT_ASSERT_EQUAL(u"outline-generator"_ustr, subs[0].agentRole);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(0), subs[0].stepOrder);
        CPPUNIT_ASSERT(subs[0].dependsOn.isEmpty());

        CPPUNIT_ASSERT_EQUAL(u"slide-designer"_ustr, subs[1].agentRole);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), subs[1].stepOrder);
        CPPUNIT_ASSERT_EQUAL(subs[0].subTaskId, subs[1].dependsOn);
    }

    void testScenarioDecompose_ContractReview()
    {
        std::vector<SubAgentTask> subs
            = AgentTaskDelegation::scenarioDecompose(
                TaskKind::ContractReview, u"tk-20260623-003"_ustr);

        CPPUNIT_ASSERT_EQUAL(static_cast<std::size_t>(3), subs.size());

        CPPUNIT_ASSERT_EQUAL(u"clause-analyzer"_ustr, subs[0].agentRole);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(0), subs[0].stepOrder);

        CPPUNIT_ASSERT_EQUAL(u"risk-checker"_ustr, subs[1].agentRole);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), subs[1].stepOrder);
        CPPUNIT_ASSERT_EQUAL(subs[0].subTaskId, subs[1].dependsOn);

        CPPUNIT_ASSERT_EQUAL(u"legal-reviewer"_ustr, subs[2].agentRole);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(2), subs[2].stepOrder);
        CPPUNIT_ASSERT_EQUAL(subs[1].subTaskId, subs[2].dependsOn);
    }

    void testScenarioDecompose_DataCleanup()
    {
        std::vector<SubAgentTask> subs
            = AgentTaskDelegation::scenarioDecompose(
                TaskKind::DataCleanup, u"tk-20260623-004"_ustr);

        CPPUNIT_ASSERT_EQUAL(static_cast<std::size_t>(2), subs.size());

        CPPUNIT_ASSERT_EQUAL(u"data-validator"_ustr, subs[0].agentRole);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(0), subs[0].stepOrder);

        CPPUNIT_ASSERT_EQUAL(u"format-optimizer"_ustr, subs[1].agentRole);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), subs[1].stepOrder);
        CPPUNIT_ASSERT_EQUAL(subs[0].subTaskId, subs[1].dependsOn);
    }

    void testDecompose_WithEnvelope_ExistingSubs()
    {
        // When the envelope already has pre-assigned sub-agent tasks,
        // decompose returns them directly.
        AsyncTaskEnvelope env;
        env.taskId = u"tk-20260623-005"_ustr;
        env.kind = TaskKind::DataCleanup;

        SubAgentTask preBuilt;
        preBuilt.subTaskId = u"tk-20260623-005-sub-0"_ustr;
        preBuilt.parentTaskId = env.taskId;
        preBuilt.agentRole = u"custom-agent"_ustr;
        preBuilt.instruction = u"custom instruction"_ustr;
        preBuilt.stepOrder = 0;
        env.subAgentTasks.push_back(preBuilt);

        std::vector<SubAgentTask> subs
            = AgentTaskDelegation::decompose(env, env.taskId);

        CPPUNIT_ASSERT_EQUAL(static_cast<std::size_t>(1), subs.size());
        CPPUNIT_ASSERT_EQUAL(u"custom-agent"_ustr, subs[0].agentRole);
    }

    void testDecompose_WithEnvelope_NoExistingSubs()
    {
        // When the envelope has no pre-assigned subs, decompose falls back
        // to scenarioDecompose.
        AsyncTaskEnvelope env;
        env.taskId = u"tk-20260623-006"_ustr;
        env.kind = TaskKind::WeeklyReport;

        std::vector<SubAgentTask> subs
            = AgentTaskDelegation::decompose(env, env.taskId);

        // WeeklyReport should produce 3 sub-agents.
        CPPUNIT_ASSERT_EQUAL(static_cast<std::size_t>(3), subs.size());
        CPPUNIT_ASSERT_EQUAL(u"data-collector"_ustr, subs[0].agentRole);
    }

    void testMerge_AllSuccess()
    {
        std::vector<AgentTaskResult> results;

        AgentTaskResult r1;
        r1.subTaskId = u"task-sub-0"_ustr;
        r1.parentTaskId = u"task"_ustr;
        r1.state = TaskState::Applied;
        r1.summary = u"Collector finished"_ustr;
        r1.evidenceIds = { u"ev-001"_ustr };
        results.push_back(r1);

        AgentTaskResult r2;
        r2.subTaskId = u"task-sub-1"_ustr;
        r2.parentTaskId = u"task"_ustr;
        r2.state = TaskState::Applied;
        r2.summary = u"Writer finished"_ustr;
        r2.evidenceIds = { u"ev-002"_ustr };
        results.push_back(r2);

        AgentTaskResult merged = AgentResultMerge::merge(results, u"task"_ustr);

        CPPUNIT_ASSERT_EQUAL(TaskState::Applied, merged.state);
        CPPUNIT_ASSERT_EQUAL(u"task"_ustr, merged.parentTaskId);
        CPPUNIT_ASSERT_EQUAL(u"task"_ustr, merged.subTaskId);
        // Summary should contain both.
        CPPUNIT_ASSERT(merged.summary.indexOf("Collector") >= 0);
        CPPUNIT_ASSERT(merged.summary.indexOf("Writer") >= 0);
        CPPUNIT_ASSERT_EQUAL(static_cast<std::size_t>(2), merged.evidenceIds.size());
    }

    void testMerge_MixedSuccessFailure()
    {
        std::vector<AgentTaskResult> results;

        AgentTaskResult r1;
        r1.subTaskId = u"task-sub-0"_ustr;
        r1.parentTaskId = u"task"_ustr;
        r1.state = TaskState::Applied;
        r1.summary = u"OK"_ustr;
        results.push_back(r1);

        AgentTaskResult r2;
        r2.subTaskId = u"task-sub-1"_ustr;
        r2.parentTaskId = u"task"_ustr;
        r2.state = TaskState::Failed;
        r2.summary = u"FAIL"_ustr;
        results.push_back(r2);

        AgentTaskResult r3;
        r3.subTaskId = u"task-sub-2"_ustr;
        r3.parentTaskId = u"task"_ustr;
        r3.state = TaskState::Running;
        r3.summary = u"RUN"_ustr;
        results.push_back(r3);

        AgentTaskResult merged = AgentResultMerge::merge(results, u"task"_ustr);

        // Mixed success + failure should yield Failed.
        CPPUNIT_ASSERT_EQUAL(TaskState::Failed, merged.state);
        CPPUNIT_ASSERT_EQUAL(static_cast<std::size_t>(3), merged.evidenceIds.size());
    }

    void testMerge_WithNextSteps()
    {
        std::vector<AgentTaskResult> results;

        AgentTaskResult r1;
        r1.subTaskId = u"task-sub-0"_ustr;
        r1.parentTaskId = u"task"_ustr;
        r1.state = TaskState::Applied;

        SubAgentTask nextStep;
        nextStep.subTaskId = u"extra-step"_ustr;
        nextStep.parentTaskId = u"task"_ustr;
        nextStep.agentRole = u"extra-agent"_ustr;
        r1.nextSteps.push_back(nextStep);
        results.push_back(r1);

        AgentTaskResult merged = AgentResultMerge::merge(results, u"task"_ustr);

        CPPUNIT_ASSERT_EQUAL(TaskState::Applied, merged.state);
        CPPUNIT_ASSERT_EQUAL(static_cast<std::size_t>(1), merged.nextSteps.size());
        CPPUNIT_ASSERT_EQUAL(u"extra-agent"_ustr, merged.nextSteps[0].agentRole);
    }

    void testIsAllComplete()
    {
        std::vector<AgentTaskResult> results;

        AgentTaskResult r1;
        r1.subTaskId = u"task-sub-0"_ustr;
        r1.parentTaskId = u"task"_ustr;
        r1.state = TaskState::Applied;
        results.push_back(r1);

        AgentTaskResult r2;
        r2.subTaskId = u"task-sub-1"_ustr;
        r2.parentTaskId = u"task"_ustr;
        r2.state = TaskState::Applied;
        results.push_back(r2);

        // Both are terminal -> complete.
        CPPUNIT_ASSERT(AgentResultMerge::isAllComplete(results, 2));
    }

    void testIsAllComplete_Incomplete()
    {
        std::vector<AgentTaskResult> results;

        AgentTaskResult r1;
        r1.subTaskId = u"task-sub-0"_ustr;
        r1.parentTaskId = u"task"_ustr;
        r1.state = TaskState::Applied;
        results.push_back(r1);

        AgentTaskResult r2;
        r2.subTaskId = u"task-sub-1"_ustr;
        r2.parentTaskId = u"task"_ustr;
        r2.state = TaskState::Running; // non-terminal
        results.push_back(r2);

        CPPUNIT_ASSERT(!AgentResultMerge::isAllComplete(results, 2));
    }

    void testIsAllComplete_InsufficientCount()
    {
        std::vector<AgentTaskResult> results;

        AgentTaskResult r1;
        r1.subTaskId = u"task-sub-0"_ustr;
        r1.parentTaskId = u"task"_ustr;
        r1.state = TaskState::Applied;
        results.push_back(r1);

        // Expected 3 but only have 1.
        CPPUNIT_ASSERT(!AgentResultMerge::isAllComplete(results, 3));
    }

    void testProgressAggregate_Empty()
    {
        AgentProgressAggregate agg = AgentProgressAggregateBuilder::build(
            u"task"_ustr, {}, {});

        CPPUNIT_ASSERT_EQUAL(sal_Int32(0), agg.totalSteps);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(0), agg.completedSteps);
        CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, agg.completionPercent(), 0.01);
    }

    void testProgressAggregate_AllPending()
    {
        std::vector<SubAgentTask> subs;
        SubAgentTask s;
        s.subTaskId = u"task-sub-0"_ustr;
        s.agentRole = u"worker"_ustr;
        s.stepOrder = 0;
        subs.push_back(s);

        AgentProgressAggregate agg = AgentProgressAggregateBuilder::build(
            u"task"_ustr, subs, {});

        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), agg.totalSteps);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(0), agg.completedSteps);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), agg.pendingSteps);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(0), agg.failedSteps);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(0), agg.runningSteps);
        CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, agg.completionPercent(), 0.01);
    }

    void testProgressAggregate_Mixed()
    {
        std::vector<SubAgentTask> subs;
        for (sal_Int32 i = 0; i < 4; ++i)
        {
            SubAgentTask s;
            s.subTaskId = u"task-sub-"_ustr + OUString::number(i);
            s.stepOrder = i;
            subs.push_back(s);
        }

        std::vector<AgentTaskResult> results;

        AgentTaskResult r0;
        r0.subTaskId = u"task-sub-0"_ustr;
        r0.state = TaskState::Applied;
        results.push_back(r0);

        AgentTaskResult r1;
        r1.subTaskId = u"task-sub-1"_ustr;
        r1.state = TaskState::Failed;
        results.push_back(r1);

        AgentTaskResult r2;
        r2.subTaskId = u"task-sub-2"_ustr;
        r2.state = TaskState::Running;
        results.push_back(r2);

        // task-sub-3 has no result -> pending

        AgentProgressAggregate agg = AgentProgressAggregateBuilder::build(
            u"task"_ustr, subs, results);

        CPPUNIT_ASSERT_EQUAL(sal_Int32(4), agg.totalSteps);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), agg.completedSteps);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), agg.failedSteps);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), agg.runningSteps);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), agg.pendingSteps);
        CPPUNIT_ASSERT_DOUBLES_EQUAL(25.0, agg.completionPercent(), 0.01);
    }

    void testFormatForUI()
    {
        AgentProgressAggregate agg;
        agg.totalSteps = 5;
        agg.completedSteps = 3;
        agg.failedSteps = 0;
        agg.runningSteps = 1;
        agg.pendingSteps = 1;

        OUString formatted = AgentProgressAggregateBuilder::formatForUI(agg);

        CPPUNIT_ASSERT(formatted.indexOf("3/5") >= 0);
        CPPUNIT_ASSERT(formatted.indexOf("1 running") >= 0);
        CPPUNIT_ASSERT(formatted.indexOf("1 pending") >= 0);
    }

    void testFormatForUI_WithFailures()
    {
        AgentProgressAggregate agg;
        agg.totalSteps = 3;
        agg.completedSteps = 1;
        agg.failedSteps = 1;
        agg.runningSteps = 0;
        agg.pendingSteps = 1;

        OUString formatted = AgentProgressAggregateBuilder::formatForUI(agg);

        CPPUNIT_ASSERT(formatted.indexOf("1/3") >= 0);
        CPPUNIT_ASSERT(formatted.indexOf("1 failed") >= 0);
        CPPUNIT_ASSERT(formatted.indexOf("1 pending") >= 0);
    }

    void testFormatForUI_Empty()
    {
        AgentProgressAggregate agg;
        agg.totalSteps = 0;

        OUString formatted = AgentProgressAggregateBuilder::formatForUI(agg);
        CPPUNIT_ASSERT(formatted.isEmpty());
    }

    CPPUNIT_TEST_SUITE(AgentDelegationTest);
    CPPUNIT_TEST(testScenarioDecompose_WeeklyReport);
    CPPUNIT_TEST(testScenarioDecompose_OutlineToSlides);
    CPPUNIT_TEST(testScenarioDecompose_ContractReview);
    CPPUNIT_TEST(testScenarioDecompose_DataCleanup);
    CPPUNIT_TEST(testDecompose_WithEnvelope_ExistingSubs);
    CPPUNIT_TEST(testDecompose_WithEnvelope_NoExistingSubs);
    CPPUNIT_TEST(testMerge_AllSuccess);
    CPPUNIT_TEST(testMerge_MixedSuccessFailure);
    CPPUNIT_TEST(testMerge_WithNextSteps);
    CPPUNIT_TEST(testIsAllComplete);
    CPPUNIT_TEST(testIsAllComplete_Incomplete);
    CPPUNIT_TEST(testIsAllComplete_InsufficientCount);
    CPPUNIT_TEST(testProgressAggregate_Empty);
    CPPUNIT_TEST(testProgressAggregate_AllPending);
    CPPUNIT_TEST(testProgressAggregate_Mixed);
    CPPUNIT_TEST(testFormatForUI);
    CPPUNIT_TEST(testFormatForUI_WithFailures);
    CPPUNIT_TEST(testFormatForUI_Empty);
    CPPUNIT_TEST_SUITE_END();
};

} // namespace

CPPUNIT_TEST_SUITE_REGISTRATION(AgentDelegationTest);

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
