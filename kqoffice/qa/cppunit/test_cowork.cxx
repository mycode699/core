/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W5 Day-0: Async Cowork Tests).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Day-0 unit tests for the async cowork skeleton:
 *   1. TaskKind / TaskState / TaskStepState token round-trip.
 *   2. Legal-transition matrix (canTransition + legalTransitions).
 *   3. awaiting-review -> failed is illegal (standard enforcement).
 *   4. applied / failed / cancelled are terminals (no successors, no self-loop).
 *   5. TaskStore write/read round-trip preserves every schema field.
 *   6. TaskStore listByState filters by state and matches ids on disk.
 *
 * Pure-logic — no URE / VCL bootstrap (see W1 ProviderTest).
 */

#include <sal/types.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/plugin/TestPlugIn.h>

#include <rtl/ustring.hxx>

#include <osl/thread.hxx>

#include <cstdlib>
#include <memory>
#include <unistd.h>

#include "AsyncTask.hxx"
#include "CoworkUiBridge.hxx"
#include "TaskNativeOsNotificationBackend.hxx"
#include "TaskOsNotificationBridge.hxx"
#include "TaskQueue.hxx"
#include "TaskReviewBridge.hxx"
#include "TaskRunner.hxx"
#include "TaskScheduler.hxx"
#include "TaskStateMachine.hxx"
#include "TaskStore.hxx"

using namespace kqoffice::ai::cowork;

namespace
{
// RAII: pin KQOFFICE_AI_TASKS_DIR to a fresh mkdtemp dir so each test
// isolates its on-disk state. Matches ScopedEvidenceDir in test_provider.cxx.
class ScopedTasksDir
{
public:
    ScopedTasksDir()
    {
        char templ[] = "/tmp/kqoffice-tk-XXXXXX";
        const char* dir = ::mkdtemp(templ);
        if (dir)
        {
            m_dir = OString(dir);
            ::setenv("KQOFFICE_AI_TASKS_DIR", dir, 1);
        }
    }
    ~ScopedTasksDir()
    {
        ::unsetenv("KQOFFICE_AI_TASKS_DIR");
    }

    const OString& path() const { return m_dir; }

private:
    OString m_dir;
};
} // namespace

namespace
{
class CowoekTest : public CppUnit::TestFixture
{
public:
    void testTaskKindRoundTrip();
    void testTaskStateRoundTrip();
    void testTaskStepStateRoundTrip();
    void testCanTransitionLegalMatrix();
    void testCanTransitionRejectsAwaitingReviewToFailed();
    void testTerminalStatesHaveNoSuccessors();
    void testLegalTransitionsMatchesCanTransition();
    void testStoreWriteReadRoundTrip();
    void testStoreListByStateFilters();
    void testStoreReadMissingReturnsFalse();
    void testQueueDispatchAwaitingReviewAppliedLifecycle();
    void testQueueCancelPendingAndRunning();
    void testQueueRefineFailedCreatesNewPendingTask();
    void testQueueLifecycleEvidenceContract();
    void testSchedulerRunOneSuccessReleasesWorker();
    void testSchedulerRunOneFailureReleasesWorker();
    void testSchedulerRecoverInterruptedRunningTasks();
    void testRunnerThreadSuccessNotifiesAwaitingReview();
    void testRunnerThreadFailureNotifiesFailed();
    void testRunnerThreadEmptyQueueNotifiesIdle();
    void testNotificationClickBuildsReviewRequest();
    void testNotificationClickRejectsNonReviewEvents();
    void testOsNotificationBuildsClickPayload();
    void testOsNotificationClickOpensStoredReview();
    void testOsNotificationSinkIgnoresNonReviewEvents();
    void testOsNotificationClickRejectsStaleTaskState();
    void testNativeOsNotificationFallbackRecordsUnavailable();
    void testPlatformNativeOsNotificationFactoryFallsBackWhenUnavailable();
    void testNativeOsNotificationSinkCountsSubmittedBackend();
    void testNativeOsNotificationClickPayloadBuildsRequest();
    void testNativeOsNotificationClickOpensStoredReview();
    void testNativeOsNotificationClickDispatchesRegisteredSink();
    void testRunnerAwaitingReviewNotificationCanOpenReview();
    void testReviewRequestOpensStoredAwaitingReviewTask();
    void testReviewRequestRejectsStaleTaskState();
    void testReviewRequestRejectsPlanMismatch();
    void testReviewAcceptResultMarksTaskApplied();
    void testReviewAcceptRejectsStaleTaskState();
    void testReviewAcceptRejectsPlanMismatch();
    void testRunnerNotificationAutoOpensStoredReview();
    void testRunnerFailureNotificationDoesNotAutoOpenReview();
    void testAutoOpenReviewNotificationRecordsPlanMismatch();
    void testCoworkUiBridgeRunsNewTaskToOpenedReview();
    void testCoworkUiBridgePostsOsNotificationRequest();
    void testCoworkUiAsyncBridgeExposesPendingRunningAndCompletes();

    CPPUNIT_TEST_SUITE(CowoekTest);
    CPPUNIT_TEST(testTaskKindRoundTrip);
    CPPUNIT_TEST(testTaskStateRoundTrip);
    CPPUNIT_TEST(testTaskStepStateRoundTrip);
    CPPUNIT_TEST(testCanTransitionLegalMatrix);
    CPPUNIT_TEST(testCanTransitionRejectsAwaitingReviewToFailed);
    CPPUNIT_TEST(testTerminalStatesHaveNoSuccessors);
    CPPUNIT_TEST(testLegalTransitionsMatchesCanTransition);
    CPPUNIT_TEST(testStoreWriteReadRoundTrip);
    CPPUNIT_TEST(testStoreListByStateFilters);
    CPPUNIT_TEST(testStoreReadMissingReturnsFalse);
    CPPUNIT_TEST(testQueueDispatchAwaitingReviewAppliedLifecycle);
    CPPUNIT_TEST(testQueueCancelPendingAndRunning);
    CPPUNIT_TEST(testQueueRefineFailedCreatesNewPendingTask);
    CPPUNIT_TEST(testQueueLifecycleEvidenceContract);
    CPPUNIT_TEST(testSchedulerRunOneSuccessReleasesWorker);
    CPPUNIT_TEST(testSchedulerRunOneFailureReleasesWorker);
    CPPUNIT_TEST(testSchedulerRecoverInterruptedRunningTasks);
    CPPUNIT_TEST(testRunnerThreadSuccessNotifiesAwaitingReview);
    CPPUNIT_TEST(testRunnerThreadFailureNotifiesFailed);
    CPPUNIT_TEST(testRunnerThreadEmptyQueueNotifiesIdle);
    CPPUNIT_TEST(testNotificationClickBuildsReviewRequest);
    CPPUNIT_TEST(testNotificationClickRejectsNonReviewEvents);
    CPPUNIT_TEST(testOsNotificationBuildsClickPayload);
    CPPUNIT_TEST(testOsNotificationClickOpensStoredReview);
    CPPUNIT_TEST(testOsNotificationSinkIgnoresNonReviewEvents);
    CPPUNIT_TEST(testOsNotificationClickRejectsStaleTaskState);
    CPPUNIT_TEST(testNativeOsNotificationFallbackRecordsUnavailable);
    CPPUNIT_TEST(testPlatformNativeOsNotificationFactoryFallsBackWhenUnavailable);
    CPPUNIT_TEST(testNativeOsNotificationSinkCountsSubmittedBackend);
    CPPUNIT_TEST(testNativeOsNotificationClickPayloadBuildsRequest);
    CPPUNIT_TEST(testNativeOsNotificationClickOpensStoredReview);
    CPPUNIT_TEST(testNativeOsNotificationClickDispatchesRegisteredSink);
    CPPUNIT_TEST(testRunnerAwaitingReviewNotificationCanOpenReview);
    CPPUNIT_TEST(testReviewRequestOpensStoredAwaitingReviewTask);
    CPPUNIT_TEST(testReviewRequestRejectsStaleTaskState);
    CPPUNIT_TEST(testReviewRequestRejectsPlanMismatch);
    CPPUNIT_TEST(testReviewAcceptResultMarksTaskApplied);
    CPPUNIT_TEST(testReviewAcceptRejectsStaleTaskState);
    CPPUNIT_TEST(testReviewAcceptRejectsPlanMismatch);
    CPPUNIT_TEST(testRunnerNotificationAutoOpensStoredReview);
    CPPUNIT_TEST(testRunnerFailureNotificationDoesNotAutoOpenReview);
    CPPUNIT_TEST(testAutoOpenReviewNotificationRecordsPlanMismatch);
    CPPUNIT_TEST(testCoworkUiBridgeRunsNewTaskToOpenedReview);
    CPPUNIT_TEST(testCoworkUiBridgePostsOsNotificationRequest);
    CPPUNIT_TEST(testCoworkUiAsyncBridgeExposesPendingRunningAndCompletes);
    CPPUNIT_TEST_SUITE_END();
};

class FixedResultWorker final : public TaskWorker
{
public:
    explicit FixedResultWorker(TaskWorkerResult result)
        : m_result(result)
    {
    }

    TaskWorkerResult run(const AsyncTaskEnvelope& runningTask) override
    {
        ++m_callCount;
        m_seenTaskId = runningTask.taskId;
        m_seenState = runningTask.state;
        return m_result;
    }

    sal_Int32 callCount() const { return m_callCount; }
    const OUString& seenTaskId() const { return m_seenTaskId; }
    TaskState seenState() const { return m_seenState; }

private:
    TaskWorkerResult m_result;
    sal_Int32 m_callCount = 0;
    OUString m_seenTaskId;
    TaskState m_seenState = TaskState::Pending;
};

AsyncTaskEnvelope makeSchedulerTask(const OUString& taskId)
{
    AsyncTaskEnvelope env;
    env.taskId = taskId;
    env.kind = TaskKind::WeeklyReport;
    env.title = u"Scheduler lifecycle"_ustr;
    env.createdAt = u"2026-05-11T14:00:00Z"_ustr;
    env.updatedAt = u"2026-05-11T14:00:00Z"_ustr;
    env.serviceMode = u"offline"_ustr;
    env.userPrompt = u"Run through scheduler"_ustr;
    return env;
}

void CowoekTest::testTaskKindRoundTrip()
{
    // Order and tokens locked by W5 spec §"Token lock" — keep in sync with
    // docs/schemas/async-task.schema.json kind enum.
    const TaskKind kinds[] = {
        TaskKind::WeeklyReport,
        TaskKind::OutlineToSlides,
        TaskKind::ContractReview,
        TaskKind::DataCleanup,
    };
    const OUString tokens[] = {
        u"weekly-report"_ustr,
        u"outline-to-slides"_ustr,
        u"contract-review"_ustr,
        u"data-cleanup"_ustr,
    };
    for (size_t i = 0; i < SAL_N_ELEMENTS(kinds); ++i)
    {
        CPPUNIT_ASSERT_EQUAL(tokens[i], taskKindToken(kinds[i]));
        TaskKind out = TaskKind::WeeklyReport;
        CPPUNIT_ASSERT(parseTaskKind(tokens[i], out));
        CPPUNIT_ASSERT(out == kinds[i]);
    }

    TaskKind out = TaskKind::WeeklyReport;
    CPPUNIT_ASSERT(!parseTaskKind(u"nope"_ustr, out));
    CPPUNIT_ASSERT(!parseTaskKind(OUString(), out));
}

void CowoekTest::testTaskStateRoundTrip()
{
    const TaskState states[] = {
        TaskState::Pending,
        TaskState::Running,
        TaskState::AwaitingReview,
        TaskState::Applied,
        TaskState::Failed,
        TaskState::Cancelled,
    };
    const OUString tokens[] = {
        u"pending"_ustr,
        u"running"_ustr,
        u"awaiting-review"_ustr,
        u"applied"_ustr,
        u"failed"_ustr,
        u"cancelled"_ustr,
    };
    for (size_t i = 0; i < SAL_N_ELEMENTS(states); ++i)
    {
        CPPUNIT_ASSERT_EQUAL(tokens[i], taskStateToken(states[i]));
        TaskState out = TaskState::Pending;
        CPPUNIT_ASSERT(parseTaskState(tokens[i], out));
        CPPUNIT_ASSERT(out == states[i]);
    }

    // Guard against the historical "needs-review" spelling drift.
    TaskState out = TaskState::Pending;
    CPPUNIT_ASSERT(!parseTaskState(u"needs-review"_ustr, out));
    CPPUNIT_ASSERT(!parseTaskState(OUString(), out));
}

void CowoekTest::testTaskStepStateRoundTrip()
{
    // Step states are narrower than TaskState: no awaiting-review.
    const TaskStepState states[] = {
        TaskStepState::Pending,
        TaskStepState::Running,
        TaskStepState::Completed,
        TaskStepState::Failed,
    };
    const OUString tokens[] = {
        u"pending"_ustr,
        u"running"_ustr,
        u"completed"_ustr,
        u"failed"_ustr,
    };
    for (size_t i = 0; i < SAL_N_ELEMENTS(states); ++i)
    {
        CPPUNIT_ASSERT_EQUAL(tokens[i], taskStepStateToken(states[i]));
        TaskStepState out = TaskStepState::Pending;
        CPPUNIT_ASSERT(parseTaskStepState(tokens[i], out));
        CPPUNIT_ASSERT(out == states[i]);
    }

    // Step level rejects the task-only "awaiting-review" token.
    TaskStepState out = TaskStepState::Pending;
    CPPUNIT_ASSERT(!parseTaskStepState(u"awaiting-review"_ustr, out));
    CPPUNIT_ASSERT(!parseTaskStepState(u"applied"_ustr, out));
    CPPUNIT_ASSERT(!parseTaskStepState(u"cancelled"_ustr, out));
}

void CowoekTest::testCanTransitionLegalMatrix()
{
    // pending -> running / cancelled
    CPPUNIT_ASSERT(canTransition(TaskState::Pending, TaskState::Running));
    CPPUNIT_ASSERT(canTransition(TaskState::Pending, TaskState::Cancelled));
    CPPUNIT_ASSERT(!canTransition(TaskState::Pending, TaskState::AwaitingReview));
    CPPUNIT_ASSERT(!canTransition(TaskState::Pending, TaskState::Applied));
    CPPUNIT_ASSERT(!canTransition(TaskState::Pending, TaskState::Failed));

    // running -> awaiting-review / failed / cancelled
    CPPUNIT_ASSERT(canTransition(TaskState::Running, TaskState::AwaitingReview));
    CPPUNIT_ASSERT(canTransition(TaskState::Running, TaskState::Failed));
    CPPUNIT_ASSERT(canTransition(TaskState::Running, TaskState::Cancelled));
    CPPUNIT_ASSERT(!canTransition(TaskState::Running, TaskState::Applied));
    CPPUNIT_ASSERT(!canTransition(TaskState::Running, TaskState::Pending));

    // awaiting-review -> running (refine) / applied / cancelled
    CPPUNIT_ASSERT(canTransition(TaskState::AwaitingReview, TaskState::Running));
    CPPUNIT_ASSERT(canTransition(TaskState::AwaitingReview, TaskState::Applied));
    CPPUNIT_ASSERT(canTransition(TaskState::AwaitingReview, TaskState::Cancelled));
    CPPUNIT_ASSERT(!canTransition(TaskState::AwaitingReview, TaskState::Pending));

    // Self-loops illegal for every state.
    CPPUNIT_ASSERT(!canTransition(TaskState::Pending, TaskState::Pending));
    CPPUNIT_ASSERT(!canTransition(TaskState::Running, TaskState::Running));
    CPPUNIT_ASSERT(!canTransition(TaskState::AwaitingReview, TaskState::AwaitingReview));
}

void CowoekTest::testCanTransitionRejectsAwaitingReviewToFailed()
{
    // Critical negative case per W5 spec: a task in awaiting-review cannot
    // directly fail; it must either roll back to running (refine) or be
    // cancelled. A failure discovered during review lands on running first.
    CPPUNIT_ASSERT(!canTransition(TaskState::AwaitingReview, TaskState::Failed));
}

void CowoekTest::testTerminalStatesHaveNoSuccessors()
{
    const TaskState terminals[] = {
        TaskState::Applied,
        TaskState::Failed,
        TaskState::Cancelled,
    };
    const TaskState all[] = {
        TaskState::Pending,
        TaskState::Running,
        TaskState::AwaitingReview,
        TaskState::Applied,
        TaskState::Failed,
        TaskState::Cancelled,
    };
    for (TaskState from : terminals)
    {
        CPPUNIT_ASSERT(isTerminalTaskState(from));
        CPPUNIT_ASSERT(legalTransitions(from).empty());
        for (TaskState to : all)
        {
            CPPUNIT_ASSERT(!canTransition(from, to));
        }
    }

    // Non-terminals report false.
    CPPUNIT_ASSERT(!isTerminalTaskState(TaskState::Pending));
    CPPUNIT_ASSERT(!isTerminalTaskState(TaskState::Running));
    CPPUNIT_ASSERT(!isTerminalTaskState(TaskState::AwaitingReview));
}

void CowoekTest::testLegalTransitionsMatchesCanTransition()
{
    const TaskState all[] = {
        TaskState::Pending,
        TaskState::Running,
        TaskState::AwaitingReview,
        TaskState::Applied,
        TaskState::Failed,
        TaskState::Cancelled,
    };
    for (TaskState from : all)
    {
        auto successors = legalTransitions(from);
        // Every successor reported here must pass canTransition, and every
        // other state must fail it — the two APIs cannot disagree.
        for (TaskState to : all)
        {
            bool listed = false;
            for (TaskState s : successors)
            {
                if (s == to) { listed = true; break; }
            }
            CPPUNIT_ASSERT_EQUAL(listed, canTransition(from, to));
        }
    }
}

void CowoekTest::testStoreWriteReadRoundTrip()
{
    ScopedTasksDir scope;
    TaskStore store;

    AsyncTaskEnvelope env;
    env.taskId = u"tk-20260511-001"_ustr;
    env.kind = TaskKind::WeeklyReport;
    env.state = TaskState::AwaitingReview;
    env.title = u"W19 Weekly Report"_ustr;
    env.createdAt = u"2026-05-11T08:30:00Z"_ustr;
    env.updatedAt = u"2026-05-11T08:45:12Z"_ustr;
    env.serviceMode = u"offline"_ustr;
    env.userPrompt = u"Summarize last week's incidents."_ustr;
    env.sourceDocs.push_back(u"file:///docs/a.odt"_ustr);
    env.sourceDocs.push_back(u"file:///docs/b.ods"_ustr);
    env.targetTemplate = u"tpl-weekly-v1"_ustr;

    TaskStep step1;
    step1.stepId = u"s1"_ustr;
    step1.title = u"Collect incidents"_ustr;
    step1.state = TaskStepState::Completed;
    step1.evidenceId = u"ev-20260511-aaaaa"_ustr;
    env.steps.push_back(step1);

    TaskStep step2;
    step2.stepId = u"s2"_ustr;
    step2.title = u"Draft summary"_ustr;
    step2.state = TaskStepState::Running;
    // Leave step2.evidenceId empty to exercise the optional-evidence path.
    env.steps.push_back(step2);

    env.resultPlanId = u"ap-0123456789abcdef"_ustr;
    env.evidenceIds.push_back(u"ev-20260511-aaaaa"_ustr);
    env.evidenceIds.push_back(u"ev-20260511-bbbbb"_ustr);

    CPPUNIT_ASSERT(store.write(env));

    AsyncTaskEnvelope out;
    CPPUNIT_ASSERT(store.read(u"2026-05"_ustr, env.taskId, out));

    CPPUNIT_ASSERT_EQUAL(env.taskId, out.taskId);
    CPPUNIT_ASSERT(env.kind == out.kind);
    CPPUNIT_ASSERT(env.state == out.state);
    CPPUNIT_ASSERT_EQUAL(env.title, out.title);
    CPPUNIT_ASSERT_EQUAL(env.createdAt, out.createdAt);
    CPPUNIT_ASSERT_EQUAL(env.updatedAt, out.updatedAt);
    CPPUNIT_ASSERT_EQUAL(env.serviceMode, out.serviceMode);
    CPPUNIT_ASSERT_EQUAL(env.userPrompt, out.userPrompt);
    CPPUNIT_ASSERT_EQUAL(env.targetTemplate, out.targetTemplate);
    CPPUNIT_ASSERT_EQUAL(env.resultPlanId, out.resultPlanId);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), out.schemaVersion);

    CPPUNIT_ASSERT_EQUAL(env.sourceDocs.size(), out.sourceDocs.size());
    for (size_t i = 0; i < env.sourceDocs.size(); ++i)
        CPPUNIT_ASSERT_EQUAL(env.sourceDocs[i], out.sourceDocs[i]);

    CPPUNIT_ASSERT_EQUAL(env.evidenceIds.size(), out.evidenceIds.size());
    for (size_t i = 0; i < env.evidenceIds.size(); ++i)
        CPPUNIT_ASSERT_EQUAL(env.evidenceIds[i], out.evidenceIds[i]);

    CPPUNIT_ASSERT_EQUAL(env.steps.size(), out.steps.size());
    for (size_t i = 0; i < env.steps.size(); ++i)
    {
        CPPUNIT_ASSERT_EQUAL(env.steps[i].stepId, out.steps[i].stepId);
        CPPUNIT_ASSERT_EQUAL(env.steps[i].title, out.steps[i].title);
        CPPUNIT_ASSERT(env.steps[i].state == out.steps[i].state);
        CPPUNIT_ASSERT_EQUAL(env.steps[i].evidenceId, out.steps[i].evidenceId);
    }
}

void CowoekTest::testStoreListByStateFilters()
{
    ScopedTasksDir scope;
    TaskStore store;

    auto mkEnv = [](const OUString& id, TaskState st) {
        AsyncTaskEnvelope env;
        env.taskId = id;
        env.kind = TaskKind::ContractReview;
        env.state = st;
        env.title = u"Contract review"_ustr;
        env.createdAt = u"2026-05-11T09:00:00Z"_ustr;
        env.updatedAt = u"2026-05-11T09:05:00Z"_ustr;
        env.serviceMode = u"offline"_ustr;
        // failureReason is required-iff-failed per schema; set only then.
        if (st == TaskState::Failed)
            env.failureReason = u"simulated"_ustr;
        return env;
    };

    CPPUNIT_ASSERT(store.write(mkEnv(u"tk-20260511-010"_ustr, TaskState::Running)));
    CPPUNIT_ASSERT(store.write(mkEnv(u"tk-20260511-011"_ustr, TaskState::AwaitingReview)));
    CPPUNIT_ASSERT(store.write(mkEnv(u"tk-20260511-012"_ustr, TaskState::Running)));
    CPPUNIT_ASSERT(store.write(mkEnv(u"tk-20260511-013"_ustr, TaskState::Failed)));

    auto running = store.listByState(u"2026-05"_ustr, TaskState::Running);
    CPPUNIT_ASSERT_EQUAL(size_t(2), running.size());

    auto awaiting = store.listByState(u"2026-05"_ustr, TaskState::AwaitingReview);
    CPPUNIT_ASSERT_EQUAL(size_t(1), awaiting.size());
    CPPUNIT_ASSERT_EQUAL(u"tk-20260511-011"_ustr, awaiting[0]);

    auto failed = store.listByState(u"2026-05"_ustr, TaskState::Failed);
    CPPUNIT_ASSERT_EQUAL(size_t(1), failed.size());
    CPPUNIT_ASSERT_EQUAL(u"tk-20260511-013"_ustr, failed[0]);

    auto applied = store.listByState(u"2026-05"_ustr, TaskState::Applied);
    CPPUNIT_ASSERT(applied.empty());
}

void CowoekTest::testStoreReadMissingReturnsFalse()
{
    ScopedTasksDir scope;
    TaskStore store;

    AsyncTaskEnvelope out;
    // Missing month dir.
    CPPUNIT_ASSERT(!store.read(u"2026-05"_ustr, u"tk-20260511-999"_ustr, out));
    CPPUNIT_ASSERT(store.listByState(u"2026-05"_ustr, TaskState::Running).empty());
}

void CowoekTest::testQueueDispatchAwaitingReviewAppliedLifecycle()
{
    ScopedTasksDir scope;
    TaskStore store;
    TaskQueue queue(store);

    AsyncTaskEnvelope env;
    env.taskId = u"tk-20260511-020"_ustr;
    env.kind = TaskKind::WeeklyReport;
    env.title = u"Weekly report"_ustr;
    env.createdAt = u"2026-05-11T10:00:00Z"_ustr;
    env.updatedAt = u"2026-05-11T10:00:00Z"_ustr;
    env.serviceMode = u"offline"_ustr;
    env.userPrompt = u"Summarize this week"_ustr;

    CPPUNIT_ASSERT(queue.enqueue(env));

    AsyncTaskEnvelope running;
    CPPUNIT_ASSERT(queue.dispatchNext(u"2026-05"_ustr, running));
    CPPUNIT_ASSERT(running.state == TaskState::Running);
    CPPUNIT_ASSERT_EQUAL(u"dispatched"_ustr, running.evidenceIds.back());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), queue.runningCount(u"2026-05"_ustr));

    AsyncTaskEnvelope review;
    CPPUNIT_ASSERT(queue.markAwaitingReview(u"2026-05"_ustr, env.taskId,
                                            u"ap-0123456789abcdef"_ustr,
                                            u"apply-plan-ready"_ustr, &review));
    CPPUNIT_ASSERT(review.state == TaskState::AwaitingReview);
    CPPUNIT_ASSERT_EQUAL(u"ap-0123456789abcdef"_ustr, review.resultPlanId);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), queue.runningCount(u"2026-05"_ustr));

    AsyncTaskEnvelope applied;
    CPPUNIT_ASSERT(queue.markApplied(u"2026-05"_ustr, env.taskId, &applied));
    CPPUNIT_ASSERT(applied.state == TaskState::Applied);
    CPPUNIT_ASSERT_EQUAL(u"user-accepted"_ustr, applied.evidenceIds.back());

    AsyncTaskEnvelope ignored;
    CPPUNIT_ASSERT(!queue.cancel(u"2026-05"_ustr, env.taskId, &ignored));
}

void CowoekTest::testQueueCancelPendingAndRunning()
{
    ScopedTasksDir scope;
    TaskStore store;
    TaskQueue queue(store);

    auto makeEnv = [](const OUString& id) {
        AsyncTaskEnvelope env;
        env.taskId = id;
        env.kind = TaskKind::DataCleanup;
        env.title = u"Cleanup"_ustr;
        env.createdAt = u"2026-05-11T11:00:00Z"_ustr;
        env.updatedAt = u"2026-05-11T11:00:00Z"_ustr;
        env.serviceMode = u"offline"_ustr;
        return env;
    };

    CPPUNIT_ASSERT(queue.enqueue(makeEnv(u"tk-20260511-021"_ustr)));
    AsyncTaskEnvelope cancelledPending;
    CPPUNIT_ASSERT(queue.cancel(u"2026-05"_ustr, u"tk-20260511-021"_ustr,
                                &cancelledPending));
    CPPUNIT_ASSERT(cancelledPending.state == TaskState::Cancelled);
    CPPUNIT_ASSERT_EQUAL(u"user-cancelled-before-dispatch"_ustr,
                         cancelledPending.evidenceIds.back());

    CPPUNIT_ASSERT(queue.enqueue(makeEnv(u"tk-20260511-022"_ustr)));
    AsyncTaskEnvelope running;
    CPPUNIT_ASSERT(queue.dispatchNext(u"2026-05"_ustr, running));
    CPPUNIT_ASSERT(running.state == TaskState::Running);

    AsyncTaskEnvelope cancelledRunning;
    CPPUNIT_ASSERT(queue.cancel(u"2026-05"_ustr, u"tk-20260511-022"_ustr,
                                &cancelledRunning));
    CPPUNIT_ASSERT(cancelledRunning.state == TaskState::Cancelled);
    CPPUNIT_ASSERT_EQUAL(u"user-cancelled-mid-run"_ustr,
                         cancelledRunning.evidenceIds.back());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), queue.runningCount(u"2026-05"_ustr));
}

void CowoekTest::testQueueRefineFailedCreatesNewPendingTask()
{
    ScopedTasksDir scope;
    TaskStore store;
    TaskQueue queue(store);

    AsyncTaskEnvelope env;
    env.taskId = u"tk-20260511-023"_ustr;
    env.kind = TaskKind::ContractReview;
    env.title = u"Review contract"_ustr;
    env.createdAt = u"2026-05-11T12:00:00Z"_ustr;
    env.updatedAt = u"2026-05-11T12:00:00Z"_ustr;
    env.serviceMode = u"offline"_ustr;
    env.userPrompt = u"Find risks"_ustr;

    CPPUNIT_ASSERT(queue.enqueue(env));
    AsyncTaskEnvelope running;
    CPPUNIT_ASSERT(queue.dispatchNext(u"2026-05"_ustr, running));

    AsyncTaskEnvelope failed;
    CPPUNIT_ASSERT(queue.markFailed(u"2026-05"_ustr, env.taskId,
                                    u"provider-error"_ustr, &failed));
    CPPUNIT_ASSERT(failed.state == TaskState::Failed);
    CPPUNIT_ASSERT_EQUAL(u"provider-error"_ustr, failed.failureReason);

    AsyncTaskEnvelope refined;
    CPPUNIT_ASSERT(queue.refineFailed(u"2026-05"_ustr, env.taskId,
                                      u"tk-20260511-024"_ustr,
                                      u"Find renewal risk only"_ustr, refined));
    CPPUNIT_ASSERT(refined.state == TaskState::Pending);
    CPPUNIT_ASSERT_EQUAL(u"tk-20260511-024"_ustr, refined.taskId);
    CPPUNIT_ASSERT_EQUAL(u"Find renewal risk only"_ustr, refined.userPrompt);
    CPPUNIT_ASSERT_EQUAL(u"refined-resubmit"_ustr,
                         refined.evidenceIds[refined.evidenceIds.size() - 2]);
    CPPUNIT_ASSERT_EQUAL(u"refined-from-tk-20260511-023"_ustr,
                         refined.evidenceIds.back());

    AsyncTaskEnvelope dispatched;
    CPPUNIT_ASSERT(queue.dispatchNext(u"2026-05"_ustr, dispatched));
    CPPUNIT_ASSERT_EQUAL(refined.taskId, dispatched.taskId);
}

void CowoekTest::testQueueLifecycleEvidenceContract()
{
    ScopedTasksDir scope;
    TaskStore store;
    TaskQueue queue(store);

    auto makeEnv = [](const OUString& id) {
        AsyncTaskEnvelope env;
        env.taskId = id;
        env.kind = TaskKind::WeeklyReport;
        env.title = u"Worker lifecycle"_ustr;
        env.createdAt = u"2026-05-11T13:00:00Z"_ustr;
        env.updatedAt = u"2026-05-11T13:00:00Z"_ustr;
        env.serviceMode = u"offline"_ustr;
        env.userPrompt = u"Build dynamic lifecycle proof"_ustr;
        return env;
    };

    CPPUNIT_ASSERT(queue.enqueue(makeEnv(u"tk-20260511-030"_ustr)));

    AsyncTaskEnvelope enqueued;
    CPPUNIT_ASSERT(store.read(u"2026-05"_ustr, u"tk-20260511-030"_ustr,
                              enqueued));
    CPPUNIT_ASSERT(enqueued.state == TaskState::Pending);
    CPPUNIT_ASSERT_EQUAL(u"enqueued"_ustr, enqueued.evidenceIds.back());

    AsyncTaskEnvelope running;
    CPPUNIT_ASSERT(queue.dispatchNext(u"2026-05"_ustr, running));
    CPPUNIT_ASSERT(running.state == TaskState::Running);
    CPPUNIT_ASSERT_EQUAL(u"dispatched"_ustr, running.evidenceIds.back());

    AsyncTaskEnvelope review;
    CPPUNIT_ASSERT(queue.markAwaitingReview(u"2026-05"_ustr, running.taskId,
                                            u"ap-fedcba9876543210"_ustr,
                                            u"apply-plan-ready"_ustr, &review));
    CPPUNIT_ASSERT(review.state == TaskState::AwaitingReview);
    CPPUNIT_ASSERT_EQUAL(u"ap-fedcba9876543210"_ustr, review.resultPlanId);
    CPPUNIT_ASSERT_EQUAL(u"apply-plan-ready"_ustr, review.evidenceIds.back());

    AsyncTaskEnvelope applied;
    CPPUNIT_ASSERT(queue.markApplied(u"2026-05"_ustr, running.taskId,
                                     &applied));
    CPPUNIT_ASSERT(applied.state == TaskState::Applied);
    CPPUNIT_ASSERT_EQUAL(u"user-accepted"_ustr, applied.evidenceIds.back());

    CPPUNIT_ASSERT(queue.enqueue(makeEnv(u"tk-20260511-031"_ustr)));
    AsyncTaskEnvelope cancelledPending;
    CPPUNIT_ASSERT(queue.cancel(u"2026-05"_ustr, u"tk-20260511-031"_ustr,
                                &cancelledPending));
    CPPUNIT_ASSERT(cancelledPending.state == TaskState::Cancelled);
    CPPUNIT_ASSERT_EQUAL(u"user-cancelled-before-dispatch"_ustr,
                         cancelledPending.evidenceIds.back());

    CPPUNIT_ASSERT(queue.enqueue(makeEnv(u"tk-20260511-032"_ustr)));
    AsyncTaskEnvelope runningCancel;
    CPPUNIT_ASSERT(queue.dispatchNext(u"2026-05"_ustr, runningCancel));
    AsyncTaskEnvelope cancelledRunning;
    CPPUNIT_ASSERT(queue.cancel(u"2026-05"_ustr, runningCancel.taskId,
                                &cancelledRunning));
    CPPUNIT_ASSERT(cancelledRunning.state == TaskState::Cancelled);
    CPPUNIT_ASSERT_EQUAL(u"user-cancelled-mid-run"_ustr,
                         cancelledRunning.evidenceIds.back());

    CPPUNIT_ASSERT(queue.enqueue(makeEnv(u"tk-20260511-033"_ustr)));
    AsyncTaskEnvelope runningFailed;
    CPPUNIT_ASSERT(queue.dispatchNext(u"2026-05"_ustr, runningFailed));
    AsyncTaskEnvelope failed;
    CPPUNIT_ASSERT(queue.markFailed(u"2026-05"_ustr, runningFailed.taskId,
                                    u"provider-timeout"_ustr, &failed));
    CPPUNIT_ASSERT(failed.state == TaskState::Failed);
    CPPUNIT_ASSERT_EQUAL(u"provider-timeout"_ustr, failed.failureReason);
    CPPUNIT_ASSERT_EQUAL(u"provider-timeout"_ustr, failed.evidenceIds.back());

    AsyncTaskEnvelope refined;
    CPPUNIT_ASSERT(queue.refineFailed(u"2026-05"_ustr, failed.taskId,
                                      u"tk-20260511-034"_ustr,
                                      u"Try a smaller scope"_ustr, refined));
    CPPUNIT_ASSERT(refined.state == TaskState::Pending);
    CPPUNIT_ASSERT_EQUAL(u"refined-resubmit"_ustr,
                         refined.evidenceIds[refined.evidenceIds.size() - 2]);
    CPPUNIT_ASSERT_EQUAL(u"refined-from-tk-20260511-033"_ustr,
                         refined.evidenceIds.back());
}

void CowoekTest::testSchedulerRunOneSuccessReleasesWorker()
{
    ScopedTasksDir scope;
    TaskStore store;
    TaskQueue queue(store);
    TaskScheduler scheduler(store, queue);

    CPPUNIT_ASSERT(queue.enqueue(makeSchedulerTask(u"tk-20260511-040"_ustr)));

    FixedResultWorker worker(TaskWorkerResult::awaitingReview(
        u"ap-1111111111111111"_ustr, u"worker-apply-plan-ready"_ustr));
    TaskSchedulerRunResult run;
    CPPUNIT_ASSERT(scheduler.runOne(u"2026-05"_ustr, worker, &run));

    CPPUNIT_ASSERT(run.dispatched);
    CPPUNIT_ASSERT(run.workerCalled);
    CPPUNIT_ASSERT(run.workerIdle);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), worker.callCount());
    CPPUNIT_ASSERT_EQUAL(u"tk-20260511-040"_ustr, worker.seenTaskId());
    CPPUNIT_ASSERT(worker.seenState() == TaskState::Running);
    CPPUNIT_ASSERT(run.finalState == TaskState::AwaitingReview);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), run.runningBefore);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), run.runningAfter);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), queue.runningCount(u"2026-05"_ustr));

    AsyncTaskEnvelope stored;
    CPPUNIT_ASSERT(store.read(u"2026-05"_ustr, u"tk-20260511-040"_ustr, stored));
    CPPUNIT_ASSERT(stored.state == TaskState::AwaitingReview);
    CPPUNIT_ASSERT_EQUAL(u"ap-1111111111111111"_ustr, stored.resultPlanId);
    CPPUNIT_ASSERT_EQUAL(u"worker-apply-plan-ready"_ustr, stored.evidenceIds.back());
}

void CowoekTest::testSchedulerRunOneFailureReleasesWorker()
{
    ScopedTasksDir scope;
    TaskStore store;
    TaskQueue queue(store);
    TaskScheduler scheduler(store, queue);

    CPPUNIT_ASSERT(queue.enqueue(makeSchedulerTask(u"tk-20260511-041"_ustr)));

    FixedResultWorker worker(TaskWorkerResult::failed(u"worker-provider-error"_ustr));
    TaskSchedulerRunResult run;
    CPPUNIT_ASSERT(scheduler.runOne(u"2026-05"_ustr, worker, &run));

    CPPUNIT_ASSERT(run.dispatched);
    CPPUNIT_ASSERT(run.workerCalled);
    CPPUNIT_ASSERT(run.workerIdle);
    CPPUNIT_ASSERT(run.finalState == TaskState::Failed);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), run.runningAfter);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), queue.runningCount(u"2026-05"_ustr));

    AsyncTaskEnvelope stored;
    CPPUNIT_ASSERT(store.read(u"2026-05"_ustr, u"tk-20260511-041"_ustr, stored));
    CPPUNIT_ASSERT(stored.state == TaskState::Failed);
    CPPUNIT_ASSERT_EQUAL(u"worker-provider-error"_ustr, stored.failureReason);
    CPPUNIT_ASSERT_EQUAL(u"worker-provider-error"_ustr, stored.evidenceIds.back());
}

void CowoekTest::testSchedulerRecoverInterruptedRunningTasks()
{
    ScopedTasksDir scope;
    TaskStore store;
    TaskQueue queue(store, 2);
    TaskScheduler scheduler(store, queue);

    CPPUNIT_ASSERT(queue.enqueue(makeSchedulerTask(u"tk-20260511-042"_ustr)));
    CPPUNIT_ASSERT(queue.enqueue(makeSchedulerTask(u"tk-20260511-043"_ustr)));

    AsyncTaskEnvelope runningA;
    AsyncTaskEnvelope runningB;
    CPPUNIT_ASSERT(queue.dispatchNext(u"2026-05"_ustr, runningA));
    CPPUNIT_ASSERT(queue.dispatchNext(u"2026-05"_ustr, runningB));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), queue.runningCount(u"2026-05"_ustr));

    CPPUNIT_ASSERT_EQUAL(sal_Int32(2),
                         scheduler.recoverInterruptedRunning(u"2026-05"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), queue.runningCount(u"2026-05"_ustr));

    AsyncTaskEnvelope recoveredA;
    AsyncTaskEnvelope recoveredB;
    CPPUNIT_ASSERT(store.read(u"2026-05"_ustr, runningA.taskId, recoveredA));
    CPPUNIT_ASSERT(store.read(u"2026-05"_ustr, runningB.taskId, recoveredB));
    CPPUNIT_ASSERT(recoveredA.state == TaskState::Failed);
    CPPUNIT_ASSERT(recoveredB.state == TaskState::Failed);
    CPPUNIT_ASSERT_EQUAL(u"process-restart-during-run"_ustr,
                         recoveredA.failureReason);
    CPPUNIT_ASSERT_EQUAL(u"process-restart-during-run"_ustr,
                         recoveredB.failureReason);
    CPPUNIT_ASSERT_EQUAL(u"process-restart-during-run"_ustr,
                         recoveredA.evidenceIds.back());
    CPPUNIT_ASSERT_EQUAL(u"process-restart-during-run"_ustr,
                         recoveredB.evidenceIds.back());
}

void CowoekTest::testRunnerThreadSuccessNotifiesAwaitingReview()
{
    ScopedTasksDir scope;
    TaskStore store;
    TaskQueue queue(store);
    TaskScheduler scheduler(store, queue);
    InMemoryTaskNotificationSink sink;
    TaskRunner runner(scheduler, sink);

    CPPUNIT_ASSERT(queue.enqueue(makeSchedulerTask(u"tk-20260511-050"_ustr)));

    FixedResultWorker worker(TaskWorkerResult::awaitingReview(
        u"ap-2222222222222222"_ustr, u"worker-apply-plan-ready"_ustr));
    TaskRunnerResult result;
    CPPUNIT_ASSERT(runner.startOneAndJoinForTest(u"2026-05"_ustr, worker, &result));

    CPPUNIT_ASSERT(result.threadStarted);
    CPPUNIT_ASSERT(result.dispatched);
    CPPUNIT_ASSERT(result.workerCalled);
    CPPUNIT_ASSERT(result.workerIdle);
    CPPUNIT_ASSERT(result.finalState == TaskState::AwaitingReview);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), result.runningAfter);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(4), result.notificationCount);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), worker.callCount());

    const auto tokens = sink.tokens();
    CPPUNIT_ASSERT_EQUAL(size_t(4), tokens.size());
    CPPUNIT_ASSERT_EQUAL(u"worker-started"_ustr, tokens[0]);
    CPPUNIT_ASSERT_EQUAL(u"task-running"_ustr, tokens[1]);
    CPPUNIT_ASSERT_EQUAL(u"awaiting-review-notification"_ustr, tokens[2]);
    CPPUNIT_ASSERT_EQUAL(u"worker-idle"_ustr, tokens[3]);

    const auto notifications = sink.snapshot();
    CPPUNIT_ASSERT_EQUAL(u"2026-05"_ustr, notifications[2].monthDir);
    CPPUNIT_ASSERT_EQUAL(u"tk-20260511-050"_ustr, notifications[2].taskId);
    CPPUNIT_ASSERT(notifications[2].state == TaskState::AwaitingReview);
    CPPUNIT_ASSERT_EQUAL(u"ap-2222222222222222"_ustr,
                         notifications[2].resultPlanId);
    CPPUNIT_ASSERT_EQUAL(u"worker-apply-plan-ready"_ustr,
                         notifications[2].evidenceId);
}

void CowoekTest::testRunnerThreadFailureNotifiesFailed()
{
    ScopedTasksDir scope;
    TaskStore store;
    TaskQueue queue(store);
    TaskScheduler scheduler(store, queue);
    InMemoryTaskNotificationSink sink;
    TaskRunner runner(scheduler, sink);

    CPPUNIT_ASSERT(queue.enqueue(makeSchedulerTask(u"tk-20260511-051"_ustr)));

    FixedResultWorker worker(TaskWorkerResult::failed(u"provider-timeout"_ustr));
    TaskRunnerResult result;
    CPPUNIT_ASSERT(runner.startOneAndJoinForTest(u"2026-05"_ustr, worker, &result));

    CPPUNIT_ASSERT(result.threadStarted);
    CPPUNIT_ASSERT(result.dispatched);
    CPPUNIT_ASSERT(result.workerCalled);
    CPPUNIT_ASSERT(result.workerIdle);
    CPPUNIT_ASSERT(result.finalState == TaskState::Failed);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), result.runningAfter);

    const auto tokens = sink.tokens();
    CPPUNIT_ASSERT_EQUAL(size_t(4), tokens.size());
    CPPUNIT_ASSERT_EQUAL(u"worker-started"_ustr, tokens[0]);
    CPPUNIT_ASSERT_EQUAL(u"task-running"_ustr, tokens[1]);
    CPPUNIT_ASSERT_EQUAL(u"task-failed-notification"_ustr, tokens[2]);
    CPPUNIT_ASSERT_EQUAL(u"worker-idle"_ustr, tokens[3]);

    const auto notifications = sink.snapshot();
    CPPUNIT_ASSERT_EQUAL(u"provider-timeout"_ustr,
                         notifications[2].failureReason);
    CPPUNIT_ASSERT_EQUAL(u"provider-timeout"_ustr,
                         notifications[2].evidenceId);
}

void CowoekTest::testRunnerThreadEmptyQueueNotifiesIdle()
{
    ScopedTasksDir scope;
    TaskStore store;
    TaskQueue queue(store);
    TaskScheduler scheduler(store, queue);
    InMemoryTaskNotificationSink sink;
    TaskRunner runner(scheduler, sink);

    FixedResultWorker worker(TaskWorkerResult::awaitingReview(
        u"ap-3333333333333333"_ustr, u"worker-apply-plan-ready"_ustr));
    TaskRunnerResult result;
    CPPUNIT_ASSERT(!runner.startOneAndJoinForTest(u"2026-05"_ustr, worker, &result));

    CPPUNIT_ASSERT(result.threadStarted);
    CPPUNIT_ASSERT(!result.dispatched);
    CPPUNIT_ASSERT(!result.workerCalled);
    CPPUNIT_ASSERT(result.workerIdle);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), worker.callCount());

    const auto tokens = sink.tokens();
    CPPUNIT_ASSERT_EQUAL(size_t(2), tokens.size());
    CPPUNIT_ASSERT_EQUAL(u"worker-started"_ustr, tokens[0]);
    CPPUNIT_ASSERT_EQUAL(u"worker-empty"_ustr, tokens[1]);
}

void CowoekTest::testNotificationClickBuildsReviewRequest()
{
    TaskNotification notification;
    notification.kind = TaskNotificationKind::AwaitingReview;
    notification.monthDir = u"2026-05"_ustr;
    notification.taskId = u"tk-20260511-060"_ustr;
    notification.state = TaskState::AwaitingReview;
    notification.resultPlanId = u"ap-4444444444444444"_ustr;
    notification.evidenceId = u"worker-apply-plan-ready"_ustr;

    TaskReviewRequest request;
    CPPUNIT_ASSERT(buildReviewRequestFromNotification(notification, request));
    CPPUNIT_ASSERT(request.valid);
    CPPUNIT_ASSERT_EQUAL(u"open-review-request"_ustr, request.actionToken);
    CPPUNIT_ASSERT_EQUAL(u"2026-05"_ustr, request.monthDir);
    CPPUNIT_ASSERT_EQUAL(u"tk-20260511-060"_ustr, request.taskId);
    CPPUNIT_ASSERT_EQUAL(u"ap-4444444444444444"_ustr,
                         request.resultPlanId);
    CPPUNIT_ASSERT_EQUAL(u"worker-apply-plan-ready"_ustr,
                         request.evidenceId);

    InMemoryTaskReviewRequestSink sink;
    CPPUNIT_ASSERT(openReviewFromNotification(notification, sink));
    const auto requests = sink.snapshot();
    CPPUNIT_ASSERT_EQUAL(size_t(1), requests.size());
    CPPUNIT_ASSERT_EQUAL(u"open-review-request"_ustr,
                         requests[0].actionToken);
    CPPUNIT_ASSERT_EQUAL(u"tk-20260511-060"_ustr, requests[0].taskId);
}

void CowoekTest::testNotificationClickRejectsNonReviewEvents()
{
    TaskNotification running;
    running.kind = TaskNotificationKind::TaskRunning;
    running.monthDir = u"2026-05"_ustr;
    running.taskId = u"tk-20260511-061"_ustr;
    running.resultPlanId = u"ap-5555555555555555"_ustr;

    TaskReviewRequest request;
    CPPUNIT_ASSERT(!buildReviewRequestFromNotification(running, request));
    CPPUNIT_ASSERT(!request.valid);

    TaskNotification missingPlan;
    missingPlan.kind = TaskNotificationKind::AwaitingReview;
    missingPlan.monthDir = u"2026-05"_ustr;
    missingPlan.taskId = u"tk-20260511-062"_ustr;
    CPPUNIT_ASSERT(!buildReviewRequestFromNotification(missingPlan, request));

    TaskNotification missingMonth;
    missingMonth.kind = TaskNotificationKind::AwaitingReview;
    missingMonth.taskId = u"tk-20260511-063"_ustr;
    missingMonth.resultPlanId = u"ap-6666666666666666"_ustr;
    CPPUNIT_ASSERT(!buildReviewRequestFromNotification(missingMonth, request));

    InMemoryTaskReviewRequestSink sink;
    CPPUNIT_ASSERT(!openReviewFromNotification(running, sink));
    CPPUNIT_ASSERT(sink.snapshot().empty());
}

void CowoekTest::testOsNotificationBuildsClickPayload()
{
    TaskNotification notification;
    notification.kind = TaskNotificationKind::AwaitingReview;
    notification.monthDir = u"2026-05"_ustr;
    notification.taskId = u"tk-20260511-075"_ustr;
    notification.state = TaskState::AwaitingReview;
    notification.resultPlanId = u"ap-1616161616161616"_ustr;
    notification.evidenceId = u"worker-apply-plan-ready"_ustr;

    TaskOsNotificationRequest request;
    CPPUNIT_ASSERT(buildOsNotificationFromTaskNotification(notification, request));
    CPPUNIT_ASSERT(request.valid);
    CPPUNIT_ASSERT_EQUAL(u"os-notification-posted"_ustr, request.actionToken);
    CPPUNIT_ASSERT_EQUAL(u"os-notification-click-review"_ustr, request.clickToken);
    CPPUNIT_ASSERT(!request.title.isEmpty());
    CPPUNIT_ASSERT(!request.body.isEmpty());
    CPPUNIT_ASSERT(request.reviewRequest.valid);
    CPPUNIT_ASSERT_EQUAL(u"open-review-request"_ustr,
                         request.reviewRequest.actionToken);
    CPPUNIT_ASSERT_EQUAL(u"tk-20260511-075"_ustr,
                         request.reviewRequest.taskId);
    CPPUNIT_ASSERT_EQUAL(u"ap-1616161616161616"_ustr,
                         request.reviewRequest.resultPlanId);
}

void CowoekTest::testOsNotificationClickOpensStoredReview()
{
    ScopedTasksDir scope;
    TaskStore store;
    TaskQueue queue(store);

    CPPUNIT_ASSERT(queue.enqueue(makeSchedulerTask(u"tk-20260511-076"_ustr)));

    AsyncTaskEnvelope running;
    CPPUNIT_ASSERT(queue.dispatchNext(u"2026-05"_ustr, running));
    CPPUNIT_ASSERT(queue.markAwaitingReview(u"2026-05"_ustr,
                                            running.taskId,
                                            u"ap-1717171717171717"_ustr,
                                            u"worker-apply-plan-ready"_ustr));

    TaskNotification notification;
    notification.kind = TaskNotificationKind::AwaitingReview;
    notification.monthDir = u"2026-05"_ustr;
    notification.taskId = running.taskId;
    notification.state = TaskState::AwaitingReview;
    notification.resultPlanId = u"ap-1717171717171717"_ustr;
    notification.evidenceId = u"worker-apply-plan-ready"_ustr;

    TaskOsNotificationRequest request;
    CPPUNIT_ASSERT(buildOsNotificationFromTaskNotification(notification, request));

    InMemoryTaskReviewOpenSink openSink;
    TaskReviewOpenResult openResult;
    CPPUNIT_ASSERT(openReviewFromOsNotificationClick(request, store, openSink,
                                                    &openResult));
    CPPUNIT_ASSERT(openResult.opened);
    CPPUNIT_ASSERT_EQUAL(u"diff-review-opened"_ustr, openResult.actionToken);
    CPPUNIT_ASSERT_EQUAL(u"notification-click-review-opened"_ustr,
                         openResult.sourceToken);
    CPPUNIT_ASSERT_EQUAL(running.taskId, openResult.taskId);
    CPPUNIT_ASSERT_EQUAL(u"ap-1717171717171717"_ustr,
                         openResult.resultPlanId);
    CPPUNIT_ASSERT_EQUAL(size_t(1), openSink.snapshot().size());
}

void CowoekTest::testOsNotificationSinkIgnoresNonReviewEvents()
{
    InMemoryTaskNotificationSink forwardedSink;
    InMemoryTaskOsNotificationSink osSink;
    OsNotificationTaskNotificationSink sink(forwardedSink, osSink);

    TaskNotification running;
    running.kind = TaskNotificationKind::TaskRunning;
    running.monthDir = u"2026-05"_ustr;
    running.taskId = u"tk-20260511-077"_ustr;
    running.state = TaskState::Running;
    running.evidenceId = u"dispatched"_ustr;

    sink.notify(running);

    CPPUNIT_ASSERT_EQUAL(size_t(1), forwardedSink.snapshot().size());
    CPPUNIT_ASSERT(osSink.snapshot().empty());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), sink.postedCount());
}

void CowoekTest::testOsNotificationClickRejectsStaleTaskState()
{
    ScopedTasksDir scope;
    TaskStore store;
    TaskQueue queue(store);

    CPPUNIT_ASSERT(queue.enqueue(makeSchedulerTask(u"tk-20260511-078"_ustr)));

    AsyncTaskEnvelope running;
    CPPUNIT_ASSERT(queue.dispatchNext(u"2026-05"_ustr, running));
    CPPUNIT_ASSERT(queue.markAwaitingReview(u"2026-05"_ustr,
                                            running.taskId,
                                            u"ap-1818181818181818"_ustr,
                                            u"worker-apply-plan-ready"_ustr));

    TaskNotification notification;
    notification.kind = TaskNotificationKind::AwaitingReview;
    notification.monthDir = u"2026-05"_ustr;
    notification.taskId = running.taskId;
    notification.state = TaskState::AwaitingReview;
    notification.resultPlanId = u"ap-1818181818181818"_ustr;
    notification.evidenceId = u"worker-apply-plan-ready"_ustr;

    TaskOsNotificationRequest request;
    CPPUNIT_ASSERT(buildOsNotificationFromTaskNotification(notification, request));
    CPPUNIT_ASSERT(queue.cancel(u"2026-05"_ustr, running.taskId));

    InMemoryTaskReviewOpenSink openSink;
    TaskReviewOpenResult openResult;
    CPPUNIT_ASSERT(!openReviewFromOsNotificationClick(request, store, openSink,
                                                     &openResult));
    CPPUNIT_ASSERT(!openResult.opened);
    CPPUNIT_ASSERT_EQUAL(u"review-task-not-awaiting-review"_ustr,
                         openResult.failureReason);
    CPPUNIT_ASSERT(openSink.snapshot().empty());
}

void CowoekTest::testNativeOsNotificationFallbackRecordsUnavailable()
{
    TaskNotification notification;
    notification.kind = TaskNotificationKind::AwaitingReview;
    notification.monthDir = u"2026-05"_ustr;
    notification.taskId = u"tk-20260511-080"_ustr;
    notification.state = TaskState::AwaitingReview;
    notification.resultPlanId = u"ap-1919191919191919"_ustr;
    notification.evidenceId = u"worker-apply-plan-ready"_ustr;

    TaskOsNotificationRequest request;
    CPPUNIT_ASSERT(buildOsNotificationFromTaskNotification(notification, request));

    FallbackTaskNativeOsNotificationBackend backend;
    const TaskNativeOsNotificationPostResult result
        = backend.postNativeNotification(request);

    CPPUNIT_ASSERT(!result.attempted);
    CPPUNIT_ASSERT(!result.submitted);
    CPPUNIT_ASSERT_EQUAL(u"native-os-notification-unavailable"_ustr,
                         result.backendToken);
    CPPUNIT_ASSERT_EQUAL(u"native-os-notification-unavailable"_ustr,
                         result.failureReason);
    CPPUNIT_ASSERT_EQUAL(request.reviewRequest.taskId,
                         result.request.reviewRequest.taskId);
}

void CowoekTest::testPlatformNativeOsNotificationFactoryFallsBackWhenUnavailable()
{
    std::unique_ptr<TaskNativeOsNotificationBackend> backend
        = createPlatformTaskNativeOsNotificationBackend();
    CPPUNIT_ASSERT(backend);

#if !defined MACOSX && !defined _WIN32
    TaskOsNotificationRequest request;
    request.valid = true;
    request.title = u"任务已完成，等待审批"_ustr;
    const TaskNativeOsNotificationPostResult result
        = backend->postNativeNotification(request);
    CPPUNIT_ASSERT(!result.submitted);
    CPPUNIT_ASSERT_EQUAL(u"native-os-notification-unavailable"_ustr,
                         result.backendToken);
#endif
}

namespace
{
class SubmittedNativeBackend final : public TaskNativeOsNotificationBackend
{
public:
    TaskNativeOsNotificationPostResult postNativeNotification(
        const TaskOsNotificationRequest& request) override
    {
        ++m_calls;
        TaskNativeOsNotificationPostResult result;
        result.attempted = true;
        result.submitted = true;
        result.backendToken = taskNativeOsNotificationSubmittedToken();
        result.request = request;
        return result;
    }

    sal_Int32 calls() const { return m_calls; }

private:
    sal_Int32 m_calls = 0;
};
}

void CowoekTest::testNativeOsNotificationSinkCountsSubmittedBackend()
{
    TaskNotification notification;
    notification.kind = TaskNotificationKind::AwaitingReview;
    notification.monthDir = u"2026-05"_ustr;
    notification.taskId = u"tk-20260511-081"_ustr;
    notification.state = TaskState::AwaitingReview;
    notification.resultPlanId = u"ap-2020202020202020"_ustr;
    notification.evidenceId = u"worker-apply-plan-ready"_ustr;

    TaskOsNotificationRequest request;
    CPPUNIT_ASSERT(buildOsNotificationFromTaskNotification(notification, request));

    SubmittedNativeBackend backend;
    NativeTaskOsNotificationSink sink(backend);
    sink.postNotification(request);

    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), backend.calls());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), sink.attemptedCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), sink.submittedCount());
    const TaskNativeOsNotificationPostResult result = sink.lastResult();
    CPPUNIT_ASSERT(result.attempted);
    CPPUNIT_ASSERT(result.submitted);
    CPPUNIT_ASSERT_EQUAL(u"native-os-notification-submitted"_ustr,
                         result.backendToken);
    CPPUNIT_ASSERT_EQUAL(request.reviewRequest.taskId,
                         result.request.reviewRequest.taskId);
}

void CowoekTest::testNativeOsNotificationClickPayloadBuildsRequest()
{
    TaskNativeOsNotificationClickPayload payload;
    payload.valid = true;
    payload.actionToken = u"os-notification-posted"_ustr;
    payload.clickToken = u"os-notification-click-review"_ustr;
    payload.monthDir = u"2026-05"_ustr;
    payload.taskId = u"tk-20260511-082"_ustr;
    payload.resultPlanId = u"ap-2121212121212121"_ustr;
    payload.evidenceId = u"worker-apply-plan-ready"_ustr;

    const TaskOsNotificationRequest request
        = buildOsNotificationRequestFromNativeClickPayload(payload);
    CPPUNIT_ASSERT(request.valid);
    CPPUNIT_ASSERT_EQUAL(payload.actionToken, request.actionToken);
    CPPUNIT_ASSERT_EQUAL(payload.clickToken, request.clickToken);
    CPPUNIT_ASSERT(request.reviewRequest.valid);
    CPPUNIT_ASSERT_EQUAL(u"open-review-request"_ustr,
                         request.reviewRequest.actionToken);
    CPPUNIT_ASSERT_EQUAL(payload.monthDir, request.reviewRequest.monthDir);
    CPPUNIT_ASSERT_EQUAL(payload.taskId, request.reviewRequest.taskId);
    CPPUNIT_ASSERT_EQUAL(payload.resultPlanId,
                         request.reviewRequest.resultPlanId);
    CPPUNIT_ASSERT_EQUAL(payload.evidenceId, request.reviewRequest.evidenceId);

    payload.clickToken = u"wrong-click-token"_ustr;
    CPPUNIT_ASSERT(!buildOsNotificationRequestFromNativeClickPayload(payload).valid);
}

void CowoekTest::testNativeOsNotificationClickOpensStoredReview()
{
    ScopedTasksDir scope;
    TaskStore store;
    TaskQueue queue(store);

    CPPUNIT_ASSERT(queue.enqueue(makeSchedulerTask(u"tk-20260511-083"_ustr)));

    AsyncTaskEnvelope running;
    CPPUNIT_ASSERT(queue.dispatchNext(u"2026-05"_ustr, running));
    CPPUNIT_ASSERT(queue.markAwaitingReview(u"2026-05"_ustr,
                                            running.taskId,
                                            u"ap-2222222222222222"_ustr,
                                            u"worker-apply-plan-ready"_ustr));

    TaskNativeOsNotificationClickPayload payload;
    payload.valid = true;
    payload.actionToken = u"os-notification-posted"_ustr;
    payload.clickToken = u"os-notification-click-review"_ustr;
    payload.monthDir = u"2026-05"_ustr;
    payload.taskId = running.taskId;
    payload.resultPlanId = u"ap-2222222222222222"_ustr;
    payload.evidenceId = u"worker-apply-plan-ready"_ustr;

    InMemoryTaskReviewOpenSink openSink;
    TaskReviewOpenResult openResult;
    CPPUNIT_ASSERT(openReviewFromNativeOsNotificationClick(payload, store,
                                                          openSink,
                                                          &openResult));
    CPPUNIT_ASSERT(openResult.opened);
    CPPUNIT_ASSERT_EQUAL(u"diff-review-opened"_ustr, openResult.actionToken);
    CPPUNIT_ASSERT_EQUAL(u"notification-click-review-opened"_ustr,
                         openResult.sourceToken);
    CPPUNIT_ASSERT_EQUAL(running.taskId, openResult.taskId);
    CPPUNIT_ASSERT_EQUAL(size_t(1), openSink.snapshot().size());
}

namespace
{
class RecordingNativeClickSink final : public TaskNativeOsNotificationClickSink
{
public:
    void handleNativeNotificationClick(
        const TaskNativeOsNotificationClickPayload& payload) override
    {
        ++m_calls;
        m_lastPayload = payload;
    }

    sal_Int32 calls() const { return m_calls; }
    const TaskNativeOsNotificationClickPayload& lastPayload() const
    {
        return m_lastPayload;
    }

private:
    sal_Int32 m_calls = 0;
    TaskNativeOsNotificationClickPayload m_lastPayload;
};
}

void CowoekTest::testNativeOsNotificationClickDispatchesRegisteredSink()
{
    clearTaskNativeOsNotificationClickSink();

    TaskNativeOsNotificationClickPayload payload;
    payload.valid = true;
    payload.actionToken = u"os-notification-posted"_ustr;
    payload.clickToken = u"os-notification-click-review"_ustr;
    payload.monthDir = u"2026-05"_ustr;
    payload.taskId = u"tk-20260511-084"_ustr;
    payload.resultPlanId = u"ap-2323232323232323"_ustr;
    payload.evidenceId = u"worker-apply-plan-ready"_ustr;

    CPPUNIT_ASSERT(!dispatchNativeOsNotificationClickPayload(payload));

    auto sink = std::make_shared<RecordingNativeClickSink>();
    setTaskNativeOsNotificationClickSink(sink);
    CPPUNIT_ASSERT(dispatchNativeOsNotificationClickPayload(payload));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), sink->calls());
    CPPUNIT_ASSERT_EQUAL(payload.taskId, sink->lastPayload().taskId);

    clearTaskNativeOsNotificationClickSink(sink.get());
    CPPUNIT_ASSERT(!dispatchNativeOsNotificationClickPayload(payload));
}

void CowoekTest::testRunnerAwaitingReviewNotificationCanOpenReview()
{
    ScopedTasksDir scope;
    TaskStore store;
    TaskQueue queue(store);
    TaskScheduler scheduler(store, queue);
    InMemoryTaskNotificationSink notificationSink;
    TaskRunner runner(scheduler, notificationSink);

    CPPUNIT_ASSERT(queue.enqueue(makeSchedulerTask(u"tk-20260511-064"_ustr)));

    FixedResultWorker worker(TaskWorkerResult::awaitingReview(
        u"ap-7777777777777777"_ustr, u"worker-apply-plan-ready"_ustr));
    CPPUNIT_ASSERT(runner.startOneAndJoinForTest(u"2026-05"_ustr, worker));

    const auto notifications = notificationSink.snapshot();
    CPPUNIT_ASSERT_EQUAL(size_t(4), notifications.size());
    CPPUNIT_ASSERT(notifications[2].kind == TaskNotificationKind::AwaitingReview);

    InMemoryTaskReviewRequestSink reviewSink;
    CPPUNIT_ASSERT(openReviewFromNotification(notifications[2], reviewSink));

    const auto requests = reviewSink.snapshot();
    CPPUNIT_ASSERT_EQUAL(size_t(1), requests.size());
    CPPUNIT_ASSERT(requests[0].valid);
    CPPUNIT_ASSERT_EQUAL(u"open-review-request"_ustr,
                         requests[0].actionToken);
    CPPUNIT_ASSERT_EQUAL(u"2026-05"_ustr, requests[0].monthDir);
    CPPUNIT_ASSERT_EQUAL(u"tk-20260511-064"_ustr, requests[0].taskId);
    CPPUNIT_ASSERT_EQUAL(u"ap-7777777777777777"_ustr,
                         requests[0].resultPlanId);
    CPPUNIT_ASSERT_EQUAL(u"worker-apply-plan-ready"_ustr,
                         requests[0].evidenceId);

    InMemoryTaskReviewOpenSink openSink;
    TaskReviewOpenResult openResult;
    CPPUNIT_ASSERT(openReviewRequest(requests[0], store, openSink, &openResult));
    CPPUNIT_ASSERT(openResult.opened);
    CPPUNIT_ASSERT_EQUAL(u"diff-review-opened"_ustr, openResult.actionToken);
    CPPUNIT_ASSERT_EQUAL(u"notification-click-review-opened"_ustr,
                         openResult.sourceToken);
    CPPUNIT_ASSERT_EQUAL(u"tk-20260511-064"_ustr, openResult.taskId);
    CPPUNIT_ASSERT_EQUAL(u"ap-7777777777777777"_ustr,
                         openResult.resultPlanId);

    const auto opened = openSink.snapshot();
    CPPUNIT_ASSERT_EQUAL(size_t(1), opened.size());
    CPPUNIT_ASSERT(opened[0].opened);
}

void CowoekTest::testReviewRequestOpensStoredAwaitingReviewTask()
{
    ScopedTasksDir scope;
    TaskStore store;
    TaskQueue queue(store);

    CPPUNIT_ASSERT(queue.enqueue(makeSchedulerTask(u"tk-20260511-065"_ustr)));

    AsyncTaskEnvelope running;
    CPPUNIT_ASSERT(queue.dispatchNext(u"2026-05"_ustr, running));

    AsyncTaskEnvelope review;
    CPPUNIT_ASSERT(queue.markAwaitingReview(u"2026-05"_ustr,
                                            running.taskId,
                                            u"ap-8888888888888888"_ustr,
                                            u"worker-apply-plan-ready"_ustr,
                                            &review));

    TaskReviewRequest request;
    request.valid = true;
    request.actionToken = taskReviewRequestToken();
    request.monthDir = u"2026-05"_ustr;
    request.taskId = review.taskId;
    request.resultPlanId = review.resultPlanId;

    InMemoryTaskReviewOpenSink sink;
    TaskReviewOpenResult result;
    CPPUNIT_ASSERT(openReviewRequest(request, store, sink, &result));
    CPPUNIT_ASSERT(result.opened);
    CPPUNIT_ASSERT_EQUAL(u"diff-review-opened"_ustr, result.actionToken);
    CPPUNIT_ASSERT_EQUAL(u"notification-click-review-opened"_ustr,
                         result.sourceToken);
    CPPUNIT_ASSERT_EQUAL(u"worker-apply-plan-ready"_ustr,
                         result.evidenceId);
    CPPUNIT_ASSERT_EQUAL(size_t(1), sink.snapshot().size());
}

void CowoekTest::testReviewRequestRejectsStaleTaskState()
{
    ScopedTasksDir scope;
    TaskStore store;
    TaskQueue queue(store);

    CPPUNIT_ASSERT(queue.enqueue(makeSchedulerTask(u"tk-20260511-066"_ustr)));

    TaskReviewRequest request;
    request.valid = true;
    request.actionToken = taskReviewRequestToken();
    request.monthDir = u"2026-05"_ustr;
    request.taskId = u"tk-20260511-066"_ustr;
    request.resultPlanId = u"ap-9999999999999999"_ustr;

    InMemoryTaskReviewOpenSink sink;
    TaskReviewOpenResult result;
    CPPUNIT_ASSERT(!openReviewRequest(request, store, sink, &result));
    CPPUNIT_ASSERT(!result.opened);
    CPPUNIT_ASSERT_EQUAL(u"review-task-not-awaiting-review"_ustr,
                         result.failureReason);
    CPPUNIT_ASSERT(sink.snapshot().empty());
}

void CowoekTest::testReviewRequestRejectsPlanMismatch()
{
    ScopedTasksDir scope;
    TaskStore store;
    TaskQueue queue(store);

    CPPUNIT_ASSERT(queue.enqueue(makeSchedulerTask(u"tk-20260511-067"_ustr)));

    AsyncTaskEnvelope running;
    CPPUNIT_ASSERT(queue.dispatchNext(u"2026-05"_ustr, running));
    CPPUNIT_ASSERT(queue.markAwaitingReview(u"2026-05"_ustr,
                                            running.taskId,
                                            u"ap-aaaaaaaaaaaaaaaa"_ustr,
                                            u"worker-apply-plan-ready"_ustr));

    TaskReviewRequest request;
    request.valid = true;
    request.actionToken = taskReviewRequestToken();
    request.monthDir = u"2026-05"_ustr;
    request.taskId = running.taskId;
    request.resultPlanId = u"ap-bbbbbbbbbbbbbbbb"_ustr;

    InMemoryTaskReviewOpenSink sink;
    TaskReviewOpenResult result;
    CPPUNIT_ASSERT(!openReviewRequest(request, store, sink, &result));
    CPPUNIT_ASSERT(!result.opened);
    CPPUNIT_ASSERT_EQUAL(u"review-plan-mismatch"_ustr,
                         result.failureReason);
    CPPUNIT_ASSERT(sink.snapshot().empty());
}

void CowoekTest::testReviewAcceptResultMarksTaskApplied()
{
    ScopedTasksDir scope;
    TaskStore store;
    TaskQueue queue(store);

    CPPUNIT_ASSERT(queue.enqueue(makeSchedulerTask(u"tk-20260511-072"_ustr)));

    AsyncTaskEnvelope running;
    CPPUNIT_ASSERT(queue.dispatchNext(u"2026-05"_ustr, running));
    CPPUNIT_ASSERT(queue.markAwaitingReview(u"2026-05"_ustr,
                                            running.taskId,
                                            u"ap-1212121212121212"_ustr,
                                            u"worker-apply-plan-ready"_ustr));

    TaskReviewRequest request;
    request.valid = true;
    request.actionToken = taskReviewRequestToken();
    request.monthDir = u"2026-05"_ustr;
    request.taskId = running.taskId;
    request.resultPlanId = u"ap-1212121212121212"_ustr;

    InMemoryTaskReviewOpenSink sink;
    TaskReviewOpenResult openResult;
    CPPUNIT_ASSERT(openReviewRequest(request, store, sink, &openResult));

    TaskReviewAcceptResult acceptResult;
    CPPUNIT_ASSERT(acceptReviewResult(openResult, store, &acceptResult));
    CPPUNIT_ASSERT(acceptResult.applied);
    CPPUNIT_ASSERT_EQUAL(u"diff-review-accepted"_ustr, acceptResult.actionToken);
    CPPUNIT_ASSERT_EQUAL(u"diff-review-opened"_ustr, acceptResult.sourceToken);
    CPPUNIT_ASSERT_EQUAL(TaskState::Applied, acceptResult.finalState);
    CPPUNIT_ASSERT_EQUAL(u"user-accepted"_ustr, acceptResult.evidenceId);

    AsyncTaskEnvelope stored;
    CPPUNIT_ASSERT(store.read(u"2026-05"_ustr, running.taskId, stored));
    CPPUNIT_ASSERT(stored.state == TaskState::Applied);
    CPPUNIT_ASSERT_EQUAL(u"ap-1212121212121212"_ustr, stored.resultPlanId);
    CPPUNIT_ASSERT_EQUAL(u"user-accepted"_ustr, stored.evidenceIds.back());
}

void CowoekTest::testReviewAcceptRejectsStaleTaskState()
{
    ScopedTasksDir scope;
    TaskStore store;
    TaskQueue queue(store);

    CPPUNIT_ASSERT(queue.enqueue(makeSchedulerTask(u"tk-20260511-073"_ustr)));

    AsyncTaskEnvelope running;
    CPPUNIT_ASSERT(queue.dispatchNext(u"2026-05"_ustr, running));
    CPPUNIT_ASSERT(queue.markAwaitingReview(u"2026-05"_ustr,
                                            running.taskId,
                                            u"ap-1313131313131313"_ustr,
                                            u"worker-apply-plan-ready"_ustr));

    TaskReviewOpenResult openResult;
    openResult.opened = true;
    openResult.actionToken = taskReviewOpenedToken();
    openResult.monthDir = u"2026-05"_ustr;
    openResult.taskId = running.taskId;
    openResult.resultPlanId = u"ap-1313131313131313"_ustr;

    CPPUNIT_ASSERT(queue.cancel(u"2026-05"_ustr, running.taskId));

    TaskReviewAcceptResult acceptResult;
    CPPUNIT_ASSERT(!acceptReviewResult(openResult, store, &acceptResult));
    CPPUNIT_ASSERT(!acceptResult.applied);
    CPPUNIT_ASSERT_EQUAL(u"review-task-not-awaiting-review"_ustr,
                         acceptResult.failureReason);
    CPPUNIT_ASSERT_EQUAL(TaskState::Cancelled, acceptResult.finalState);
}

void CowoekTest::testReviewAcceptRejectsPlanMismatch()
{
    ScopedTasksDir scope;
    TaskStore store;
    TaskQueue queue(store);

    CPPUNIT_ASSERT(queue.enqueue(makeSchedulerTask(u"tk-20260511-074"_ustr)));

    AsyncTaskEnvelope running;
    CPPUNIT_ASSERT(queue.dispatchNext(u"2026-05"_ustr, running));
    CPPUNIT_ASSERT(queue.markAwaitingReview(u"2026-05"_ustr,
                                            running.taskId,
                                            u"ap-1414141414141414"_ustr,
                                            u"worker-apply-plan-ready"_ustr));

    TaskReviewOpenResult openResult;
    openResult.opened = true;
    openResult.actionToken = taskReviewOpenedToken();
    openResult.monthDir = u"2026-05"_ustr;
    openResult.taskId = running.taskId;
    openResult.resultPlanId = u"ap-1515151515151515"_ustr;

    TaskReviewAcceptResult acceptResult;
    CPPUNIT_ASSERT(!acceptReviewResult(openResult, store, &acceptResult));
    CPPUNIT_ASSERT(!acceptResult.applied);
    CPPUNIT_ASSERT_EQUAL(u"review-plan-mismatch"_ustr,
                         acceptResult.failureReason);

    AsyncTaskEnvelope stored;
    CPPUNIT_ASSERT(store.read(u"2026-05"_ustr, running.taskId, stored));
    CPPUNIT_ASSERT(stored.state == TaskState::AwaitingReview);
    CPPUNIT_ASSERT_EQUAL(u"ap-1414141414141414"_ustr, stored.resultPlanId);
}

void CowoekTest::testRunnerNotificationAutoOpensStoredReview()
{
    ScopedTasksDir scope;
    TaskStore store;
    TaskQueue queue(store);
    TaskScheduler scheduler(store, queue);
    InMemoryTaskNotificationSink notificationSink;
    InMemoryTaskReviewOpenSink openSink;
    AutoOpenReviewNotificationSink autoSink(notificationSink, store, openSink);
    TaskRunner runner(scheduler, autoSink);

    CPPUNIT_ASSERT(queue.enqueue(makeSchedulerTask(u"tk-20260511-068"_ustr)));

    FixedResultWorker worker(TaskWorkerResult::awaitingReview(
        u"ap-cccccccccccccccc"_ustr, u"worker-apply-plan-ready"_ustr));
    CPPUNIT_ASSERT(runner.startOneAndJoinForTest(u"2026-05"_ustr, worker));

    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), autoSink.autoOpenAttemptCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), autoSink.autoOpenSuccessCount());

    const auto notifications = notificationSink.snapshot();
    CPPUNIT_ASSERT_EQUAL(size_t(4), notifications.size());
    CPPUNIT_ASSERT(notifications[2].kind == TaskNotificationKind::AwaitingReview);

    const auto opened = openSink.snapshot();
    CPPUNIT_ASSERT_EQUAL(size_t(1), opened.size());
    CPPUNIT_ASSERT(opened[0].opened);
    CPPUNIT_ASSERT_EQUAL(u"diff-review-opened"_ustr, opened[0].actionToken);
    CPPUNIT_ASSERT_EQUAL(u"notification-click-review-opened"_ustr,
                         opened[0].sourceToken);
    CPPUNIT_ASSERT_EQUAL(u"tk-20260511-068"_ustr, opened[0].taskId);
    CPPUNIT_ASSERT_EQUAL(u"ap-cccccccccccccccc"_ustr,
                         opened[0].resultPlanId);
}

void CowoekTest::testRunnerFailureNotificationDoesNotAutoOpenReview()
{
    ScopedTasksDir scope;
    TaskStore store;
    TaskQueue queue(store);
    TaskScheduler scheduler(store, queue);
    InMemoryTaskNotificationSink notificationSink;
    InMemoryTaskReviewOpenSink openSink;
    AutoOpenReviewNotificationSink autoSink(notificationSink, store, openSink);
    TaskRunner runner(scheduler, autoSink);

    CPPUNIT_ASSERT(queue.enqueue(makeSchedulerTask(u"tk-20260511-069"_ustr)));

    FixedResultWorker worker(TaskWorkerResult::failed(u"provider-timeout"_ustr));
    CPPUNIT_ASSERT(runner.startOneAndJoinForTest(u"2026-05"_ustr, worker));

    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), autoSink.autoOpenAttemptCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), autoSink.autoOpenSuccessCount());
    CPPUNIT_ASSERT(openSink.snapshot().empty());

    const auto tokens = notificationSink.tokens();
    CPPUNIT_ASSERT_EQUAL(u"task-failed-notification"_ustr, tokens[2]);
}

void CowoekTest::testAutoOpenReviewNotificationRecordsPlanMismatch()
{
    ScopedTasksDir scope;
    TaskStore store;
    InMemoryTaskNotificationSink notificationSink;
    InMemoryTaskReviewOpenSink openSink;
    AutoOpenReviewNotificationSink autoSink(notificationSink, store, openSink);

    AsyncTaskEnvelope task = makeSchedulerTask(u"tk-20260511-070"_ustr);
    task.state = TaskState::AwaitingReview;
    task.resultPlanId = u"ap-dddddddddddddddd"_ustr;
    task.evidenceIds.push_back(u"worker-apply-plan-ready"_ustr);
    CPPUNIT_ASSERT(store.write(task));

    TaskNotification notification;
    notification.kind = TaskNotificationKind::AwaitingReview;
    notification.monthDir = u"2026-05"_ustr;
    notification.taskId = task.taskId;
    notification.state = TaskState::AwaitingReview;
    notification.resultPlanId = u"ap-eeeeeeeeeeeeeeee"_ustr;
    notification.evidenceId = u"worker-apply-plan-ready"_ustr;

    autoSink.notify(notification);

    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), autoSink.autoOpenAttemptCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), autoSink.autoOpenSuccessCount());
    CPPUNIT_ASSERT(openSink.snapshot().empty());

    const TaskReviewOpenResult result = autoSink.lastAutoOpenResult();
    CPPUNIT_ASSERT(!result.opened);
    CPPUNIT_ASSERT_EQUAL(u"review-plan-mismatch"_ustr,
                         result.failureReason);
    CPPUNIT_ASSERT_EQUAL(size_t(1), notificationSink.snapshot().size());
}

void CowoekTest::testCoworkUiBridgeRunsNewTaskToOpenedReview()
{
    ScopedTasksDir scope;
    TaskStore store;
    InMemoryTaskNotificationSink notificationSink;
    InMemoryTaskReviewOpenSink openSink;

    AsyncTaskEnvelope task = makeSchedulerTask(u"tk-20260511-071"_ustr);
    task.title = u"Cowork UI bridge"_ustr;
    task.userPrompt = u"Start from CoworkDialog"_ustr;

    CoworkUiBridgeResult result;
    CPPUNIT_ASSERT(runCoworkUiTaskBridge(store, u"2026-05"_ustr, task,
                                         notificationSink, openSink, &result));

    CPPUNIT_ASSERT(result.enqueued);
    CPPUNIT_ASSERT(result.threadStarted);
    CPPUNIT_ASSERT(result.dispatched);
    CPPUNIT_ASSERT(result.workerCalled);
    CPPUNIT_ASSERT(result.finalState == TaskState::AwaitingReview);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), result.autoOpenAttemptCount);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), result.autoOpenSuccessCount);
    CPPUNIT_ASSERT_EQUAL(u"diff-review-opened"_ustr,
                         result.lastReviewOpenResult.actionToken);
    CPPUNIT_ASSERT_EQUAL(u"notification-click-review-opened"_ustr,
                         result.lastReviewOpenResult.sourceToken);
    CPPUNIT_ASSERT(result.resultPlanId.startsWith(u"ap-"_ustr));
    CPPUNIT_ASSERT(result.evidenceId.startsWith(u"ev-"_ustr));

    AsyncTaskEnvelope stored;
    CPPUNIT_ASSERT(store.read(u"2026-05"_ustr, task.taskId, stored));
    CPPUNIT_ASSERT(stored.state == TaskState::AwaitingReview);
    CPPUNIT_ASSERT_EQUAL(result.resultPlanId, stored.resultPlanId);
    CPPUNIT_ASSERT(!stored.evidenceIds.empty());
    CPPUNIT_ASSERT_EQUAL(result.evidenceId, stored.evidenceIds.back());

    const auto notifications = notificationSink.tokens();
    CPPUNIT_ASSERT_EQUAL(size_t(4), notifications.size());
    CPPUNIT_ASSERT_EQUAL(u"awaiting-review-notification"_ustr, notifications[2]);

    const auto opened = openSink.snapshot();
    CPPUNIT_ASSERT_EQUAL(size_t(1), opened.size());
    CPPUNIT_ASSERT(opened[0].opened);
    CPPUNIT_ASSERT_EQUAL(task.taskId, opened[0].taskId);
    CPPUNIT_ASSERT_EQUAL(result.resultPlanId, opened[0].resultPlanId);
}

void CowoekTest::testCoworkUiBridgePostsOsNotificationRequest()
{
    ScopedTasksDir scope;
    TaskStore store;
    InMemoryTaskNotificationSink notificationSink;
    InMemoryTaskReviewOpenSink openSink;
    InMemoryTaskOsNotificationSink osSink;

    AsyncTaskEnvelope task = makeSchedulerTask(u"tk-20260511-079"_ustr);
    task.title = u"Cowork UI OS notification bridge"_ustr;
    task.userPrompt = u"Start from CoworkDialog with OS sink"_ustr;

    CoworkUiBridgeResult result;
    CPPUNIT_ASSERT(runCoworkUiTaskBridge(store, u"2026-05"_ustr, task,
                                         notificationSink, openSink, osSink,
                                         &result));

    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), result.autoOpenAttemptCount);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), result.autoOpenSuccessCount);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), result.osNotificationPostedCount);

    const auto requests = osSink.snapshot();
    CPPUNIT_ASSERT_EQUAL(size_t(1), requests.size());
    CPPUNIT_ASSERT(requests[0].valid);
    CPPUNIT_ASSERT_EQUAL(u"os-notification-posted"_ustr,
                         requests[0].actionToken);
    CPPUNIT_ASSERT_EQUAL(u"os-notification-click-review"_ustr,
                         requests[0].clickToken);
    CPPUNIT_ASSERT_EQUAL(task.taskId, requests[0].reviewRequest.taskId);
    CPPUNIT_ASSERT_EQUAL(result.resultPlanId,
                         requests[0].reviewRequest.resultPlanId);
}

void CowoekTest::testCoworkUiAsyncBridgeExposesPendingRunningAndCompletes()
{
    ScopedTasksDir scope;
    TaskStore store;
    InMemoryTaskReviewOpenSink openSink;

    AsyncTaskEnvelope task = makeSchedulerTask(u"tk-20260511-080"_ustr);
    task.title = u"Cowork async UI bridge"_ustr;
    task.userPrompt = u"Run without blocking CoworkDialog"_ustr;

    CoworkUiTaskBridgeJob job(u"2026-05"_ustr, task, openSink, false);
    CPPUNIT_ASSERT(job.prepare());
    CPPUNIT_ASSERT(!job.isStarted());
    CPPUNIT_ASSERT(!job.isDone());

    AsyncTaskEnvelope stored;
    CPPUNIT_ASSERT(store.read(u"2026-05"_ustr, task.taskId, stored));
    CPPUNIT_ASSERT(stored.state == TaskState::Pending);

    CPPUNIT_ASSERT(job.start());
    CPPUNIT_ASSERT(job.isStarted());

    bool sawRunning = false;
    TimeValue delay;
    delay.Seconds = 0;
    delay.Nanosec = 20000000;
    for (sal_Int32 i = 0; i < 50 && !sawRunning; ++i)
    {
        if (store.read(u"2026-05"_ustr, task.taskId, stored)
            && stored.state == TaskState::Running)
            sawRunning = true;
        else
            osl::Thread::wait(delay);
    }
    CPPUNIT_ASSERT(sawRunning);

    job.join();
    CPPUNIT_ASSERT(job.isDone());
    const CoworkUiBridgeResult result = job.result();
    CPPUNIT_ASSERT(result.enqueued);
    CPPUNIT_ASSERT(result.threadStarted);
    CPPUNIT_ASSERT(result.dispatched);
    CPPUNIT_ASSERT(result.workerCalled);
    CPPUNIT_ASSERT(result.finalState == TaskState::AwaitingReview);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(4), result.notificationCount);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), result.autoOpenSuccessCount);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), result.osNotificationPostedCount);

    CPPUNIT_ASSERT(store.read(u"2026-05"_ustr, task.taskId, stored));
    CPPUNIT_ASSERT(stored.state == TaskState::AwaitingReview);
    CPPUNIT_ASSERT_EQUAL(size_t(1), openSink.snapshot().size());
}

} // namespace

CPPUNIT_TEST_SUITE_REGISTRATION(CowoekTest);

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
