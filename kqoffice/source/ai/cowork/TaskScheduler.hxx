/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V2 W5: Async Cowork Task Scheduler).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_COWORK_TASKSCHEDULER_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_COWORK_TASKSCHEDULER_HXX

#include "AsyncTask.hxx"
#include "TaskQueue.hxx"
#include "TaskStore.hxx"

#include <osl/mutex.hxx>
#include <osl/thread.hxx>
#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <atomic>
#include <memory>
#include <vector>

namespace kqoffice::ai::cowork
{

struct SAL_DLLPUBLIC_EXPORT TaskWorkerResult
{
    bool success = false;
    OUString resultPlanId;
    OUString evidenceId;
    OUString failureReason;

    static TaskWorkerResult awaitingReview(const OUString& resultPlanId,
                                           const OUString& evidenceId);
    static TaskWorkerResult failed(const OUString& reason);
};

class SAL_DLLPUBLIC_EXPORT TaskWorker
{
public:
    virtual ~TaskWorker();

    // Runs synchronously for this first scheduler slice. The production
    // threaded worker can implement the same contract later.
    virtual TaskWorkerResult run(const AsyncTaskEnvelope& runningTask) = 0;
};

struct TaskSchedulerRunResult
{
    bool dispatched = false;
    bool workerCalled = false;
    bool workerIdle = true;
    OUString taskId;
    TaskState finalState = TaskState::Pending;
    OUString resultPlanId;
    OUString evidenceId;
    OUString failureReason;
    sal_Int32 runningBefore = 0;
    sal_Int32 runningAfter = 0;
};

/// Evidence record for parallel worker thread lifecycle.
struct TaskWorkerEvidence
{
    OUString evidenceId;
    OUString taskId;
    OUString threadId;
    OUString event; // "worker-started", "worker-completed", "worker-failed"
    sal_Int64 startTimeMs = 0;
    sal_Int64 endTimeMs = 0;
};

/// Factory interface for creating per-task worker instances.
class SAL_DLLPUBLIC_EXPORT TaskWorkerFactory
{
public:
    virtual ~TaskWorkerFactory();
    virtual TaskWorker& workerFor(const AsyncTaskEnvelope& task) = 0;
};

/// Result of a parallel dispatchAll call.
struct TaskSchedulerDispatchAllResult
{
    sal_Int32 dispatched = 0;
    sal_Int32 completed = 0;
    sal_Int32 failed = 0;
    std::vector<TaskWorkerEvidence> workerEvidence;
};

class SAL_DLLPUBLIC_EXPORT TaskScheduler
{
public:
    TaskScheduler(TaskStore& store, TaskQueue& queue);

    /// Single-task dispatch (backward compat). Runs one task synchronously
    /// in the calling thread.
    bool runOne(const OUString& monthDir,
                TaskWorker& worker,
                TaskSchedulerRunResult* out = nullptr);

    /// Parallel dispatch: dispatches all pending tasks up to
    /// maxParallelism, each in its own osl::Thread. Returns after all
    /// workers complete (join).
    TaskSchedulerDispatchAllResult dispatchAll(const OUString& monthDir,
                                               TaskWorkerFactory& factory);

    /// Parallel dispatch with a fixed worker instance shared across all
    /// tasks (for simple cases).
    TaskSchedulerDispatchAllResult dispatchAll(const OUString& monthDir,
                                               TaskWorker& worker);

    sal_Int32 recoverInterruptedRunning(const OUString& monthDir);
    sal_Int32 recoverInterruptedRunning(const OUString& monthDir,
                                        const OUString& reason);

    /// Configure maximum concurrent workers (default: CPU cores).
    void setMaxParallelism(sal_Int32 n);

    /// Current number of active worker threads.
    sal_Int32 activeWorkers() const;

    /// Graceful shutdown: signals workers to cancel and joins all threads.
    void shutdown();

private:
    /// Sort a vector of envelopes descending by priority (High first).
    static void sortByPriority(std::vector<AsyncTaskEnvelope>& tasks);

    TaskStore& m_store;
    TaskQueue& m_queue;

    class ParallelWorkerThread;
    osl::Mutex m_aPoolMutex;
    std::vector<std::unique_ptr<ParallelWorkerThread>> m_aWorkers;
    sal_Int32 m_nMaxParallelism;
    std::atomic<bool> m_bShutdown;
};

} // namespace kqoffice::ai::cowork

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
