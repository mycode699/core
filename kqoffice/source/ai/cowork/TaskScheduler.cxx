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
#include <thread>

#include <osl/time.h>
#include <sal/log.hxx>

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
TaskWorkerFactory::~TaskWorkerFactory() = default;

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
    , m_nMaxParallelism(std::max(1u, std::thread::hardware_concurrency()))
    , m_bShutdown(false)
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

// --- Parallel worker thread -----------------------------------------------

namespace
{
sal_Int64 currentTimeMs()
{
    TimeValue tv;
    osl_getSystemTime(&tv);
    return static_cast<sal_Int64>(tv.Seconds) * 1000
         + static_cast<sal_Int64>(tv.Nanosec) / 1000000;
}
} // namespace

class TaskScheduler::ParallelWorkerThread final : public osl::Thread
{
public:
    ParallelWorkerThread(TaskScheduler& scheduler,
                         OUString monthDir,
                         TaskWorker& worker,
                         TaskWorkerEvidence& ev)
        : m_scheduler(scheduler)
        , m_monthDir(std::move(monthDir))
        , m_worker(worker)
        , m_evidence(ev)
    {
    }

    void SAL_CALL run() override
    {
        if (m_scheduler.m_bShutdown)
        {
            m_evidence.event = u"worker-cancelled"_ustr;
            m_evidence.endTimeMs = currentTimeMs();
            return;
        }

        osl_setThreadName("KQOfficeParTask");
        oslThreadIdentifier tid = osl::Thread::getCurrentIdentifier();
        char tidBuf[32];
        std::snprintf(tidBuf, sizeof(tidBuf), "tid-%p",
                      reinterpret_cast<void*>(static_cast<sal_uIntPtr>(tid)));
        m_evidence.threadId = OUString::createFromAscii(tidBuf);
        m_evidence.startTimeMs = currentTimeMs();
        m_evidence.event = u"worker-started"_ustr;

        SAL_INFO("kqoffice.ai.cowork.scheduler",
                  "ParallelWorker " << tidBuf << " starting for task "
                  << m_evidence.taskId);

        TaskSchedulerRunResult runResult;
        bool ok = m_scheduler.runOne(m_monthDir, m_worker, &runResult);

        m_evidence.taskId = runResult.taskId;
        m_evidence.endTimeMs = currentTimeMs();
        if (ok)
        {
            m_evidence.event = u"worker-completed"_ustr;
            m_evidence.evidenceId = runResult.evidenceId.isEmpty()
                ? u"worker-apply-plan-ready"_ustr
                : runResult.evidenceId;
        }
        else
        {
            m_evidence.event = u"worker-failed"_ustr;
            m_evidence.evidenceId = runResult.failureReason.isEmpty()
                ? u"worker-failed"_ustr
                : runResult.failureReason;
        }

        SAL_INFO("kqoffice.ai.cowork.scheduler",
                  "ParallelWorker " << tidBuf << " finished: "
                  << (ok ? "ok" : "fail") << " task=" << m_evidence.taskId);
    }

private:
    TaskScheduler& m_scheduler;
    OUString m_monthDir;
    TaskWorker& m_worker;
    TaskWorkerEvidence& m_evidence;
};

TaskScheduler::~TaskScheduler() = default;

// --- Parallel dispatch methods -------------------------------------------

void TaskScheduler::sortByPriority(std::vector<AsyncTaskEnvelope>& tasks)
{
    std::sort(tasks.begin(), tasks.end(),
              [](const AsyncTaskEnvelope& a, const AsyncTaskEnvelope& b) {
                  // Higher priority value first (High=2 > Normal=1 > Low=0)
                  if (a.priority != b.priority)
                      return static_cast<int>(a.priority) > static_cast<int>(b.priority);
                  // Stable secondary sort by task ID
                  return a.taskId.compareTo(b.taskId) < 0;
              });
}

TaskSchedulerDispatchAllResult TaskScheduler::dispatchAll(
    const OUString& monthDir, TaskWorkerFactory& factory)
{
    TaskSchedulerDispatchAllResult result;
    if (m_bShutdown)
        return result;

    // Count how many pending tasks are available
    auto pendingIds = m_store.listByState(monthDir, TaskState::Pending);
    if (pendingIds.empty())
        return result;

    // Cap at max parallelism
    sal_Int32 runningNow = m_queue.runningCount(monthDir);
    sal_Int32 slots = m_nMaxParallelism - runningNow;
    if (slots <= 0)
        return result;

    sal_Int32 toDispatch = std::min(slots, static_cast<sal_Int32>(pendingIds.size()));

    // Pre-allocate evidence vector
    result.workerEvidence.resize(toDispatch);

    // Spin up worker threads. Each worker calls runOne() which internally
    // dispatches a pending task (with priority ordering via dispatchNext).
    {
        osl::MutexGuard poolGuard(m_aPoolMutex);
        for (sal_Int32 i = 0; i < toDispatch; ++i)
        {
            // Use a placeholder task — the actual task is determined by
            // runOne's internal dispatchNext call
            AsyncTaskEnvelope placeholder;
            TaskWorker& w = factory.workerFor(placeholder);
            result.workerEvidence[i].taskId = u"pending"_ustr;

            auto thread = std::make_unique<ParallelWorkerThread>(
                *this, monthDir, w, result.workerEvidence[i]);
            if (thread->create())
            {
                m_aWorkers.push_back(std::move(thread));
                ++result.dispatched;
            }
        }
    }

    // Join all worker threads
    for (auto& worker : m_aWorkers)
    {
        worker->join();
    }

    // Post-join: patch any evidence that still shows "pending" taskId
    // to reflect the actual dispatched taskId, and ensure event is set
    // to the proper outcome rather than the initial "worker-started".
    for (auto& ev : result.workerEvidence)
    {
        if (ev.taskId == u"pending"_ustr)
        {
            ev.taskId = u"no-task-dispatched"_ustr;
            ev.event = u"worker-empty"_ustr;
        }
    }

    // Count completions/failures from evidence
    result.completed = 0;
    result.failed = 0;
    for (const auto& ev : result.workerEvidence)
    {
        if (ev.event == u"worker-completed"_ustr)
            ++result.completed;
        else if (ev.event == u"worker-failed"_ustr)
            ++result.failed;
    }

    // Clear the completed workers
    {
        osl::MutexGuard poolGuard(m_aPoolMutex);
        m_aWorkers.clear();
    }

    return result;
}

TaskSchedulerDispatchAllResult TaskScheduler::dispatchAll(
    const OUString& monthDir, TaskWorker& worker)
{
    class SharedWorkerFactory final : public TaskWorkerFactory
    {
    public:
        explicit SharedWorkerFactory(TaskWorker& w) : m_w(w) {}
        TaskWorker& workerFor(const AsyncTaskEnvelope&) override { return m_w; }
    private:
        TaskWorker& m_w;
    };
    SharedWorkerFactory factory(worker);
    return dispatchAll(monthDir, factory);
}

sal_Int32 TaskScheduler::activeWorkers() const
{
    osl::MutexGuard guard(const_cast<osl::Mutex&>(m_aPoolMutex));
    return static_cast<sal_Int32>(m_aWorkers.size());
}

void TaskScheduler::setMaxParallelism(sal_Int32 n)
{
    if (n > 0)
        m_nMaxParallelism = n;
}

void TaskScheduler::shutdown()
{
    m_bShutdown = true;
    osl::MutexGuard guard(m_aPoolMutex);
    for (auto& worker : m_aWorkers)
        worker->join();
    m_aWorkers.clear();
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
