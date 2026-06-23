/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V2 W5: Async Cowork Task Scheduler).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "TaskScheduler.hxx"

#include <algorithm>

namespace kqoffice::ai::cowork
{
namespace
{
OUString fallbackEvidence(const TaskWorkerResult& result)
{
    return result.evidenceId.isEmpty()
        ? u"worker-apply-plan-ready"_ustr
        : result.evidenceId;
}

OUString fallbackFailureReason(const TaskWorkerResult& result)
{
    return result.failureReason.isEmpty()
        ? u"worker-failed"_ustr
        : result.failureReason;
}
}

TaskWorker::~TaskWorker() = default;

TaskWorkerResult TaskWorkerResult::awaitingReview(const OUString& resultPlanId,
                                                   const OUString& evidenceId)
{
    TaskWorkerResult result;
    result.success = true;
    result.resultPlanId = resultPlanId;
    result.evidenceId = evidenceId;
    return result;
}

TaskWorkerResult TaskWorkerResult::failed(const OUString& reason)
{
    TaskWorkerResult result;
    result.success = false;
    result.failureReason = reason;
    return result;
}

TaskScheduler::TaskScheduler(TaskStore& store, TaskQueue& queue)
    : m_store(store)
    , m_queue(queue)
{
}

bool TaskScheduler::runOne(const OUString& monthDir,
                           TaskWorker& worker,
                           TaskSchedulerRunResult* out)
{
    TaskSchedulerRunResult result;
    result.runningBefore = m_queue.runningCount(monthDir);
    result.runningAfter = result.runningBefore;

    AsyncTaskEnvelope running;
    if (!m_queue.dispatchNext(monthDir, running))
    {
        if (out)
            *out = result;
        return false;
    }

    result.dispatched = true;
    result.workerCalled = true;
    result.workerIdle = false;
    result.taskId = running.taskId;

    TaskWorkerResult workerResult = worker.run(running);

    AsyncTaskEnvelope finished;
    bool ok = false;
    if (workerResult.success)
    {
        if (workerResult.resultPlanId.isEmpty())
        {
            ok = m_queue.markFailed(monthDir, running.taskId,
                                    u"worker-missing-result-plan"_ustr,
                                    &finished);
        }
        else
        {
            ok = m_queue.markAwaitingReview(monthDir, running.taskId,
                                            workerResult.resultPlanId,
                                            fallbackEvidence(workerResult),
                                            &finished);
        }
    }
    else
    {
        ok = m_queue.markFailed(monthDir, running.taskId,
                                fallbackFailureReason(workerResult),
                                &finished);
    }

    result.workerIdle = true;
    result.runningAfter = m_queue.runningCount(monthDir);
    result.finalState = ok ? finished.state : running.state;
    if (ok)
    {
        result.resultPlanId = finished.resultPlanId;
        result.failureReason = finished.failureReason;
        if (!finished.evidenceIds.empty())
            result.evidenceId = finished.evidenceIds.back();
    }

    if (out)
        *out = result;
    return ok;
}

sal_Int32 TaskScheduler::recoverInterruptedRunning(const OUString& monthDir)
{
    return recoverInterruptedRunning(monthDir, u"process-restart-during-run"_ustr);
}

sal_Int32 TaskScheduler::recoverInterruptedRunning(const OUString& monthDir,
                                                   const OUString& reason)
{
    auto running = m_store.listByState(monthDir, TaskState::Running);
    std::sort(running.begin(), running.end(),
              [](const OUString& a, const OUString& b) {
                  return a.compareTo(b) < 0;
              });

    sal_Int32 recovered = 0;
    OUString failureReason = reason.isEmpty()
        ? u"process-restart-during-run"_ustr
        : reason;
    for (const auto& taskId : running)
    {
        if (m_queue.markFailed(monthDir, taskId, failureReason))
            ++recovered;
    }
    return recovered;
}

} // namespace kqoffice::ai::cowork

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
