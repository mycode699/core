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

#include <rtl/ustring.hxx>
#include <sal/types.h>

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

class SAL_DLLPUBLIC_EXPORT TaskScheduler
{
public:
    TaskScheduler(TaskStore& store, TaskQueue& queue);

    bool runOne(const OUString& monthDir,
                TaskWorker& worker,
                TaskSchedulerRunResult* out = nullptr);

    sal_Int32 recoverInterruptedRunning(const OUString& monthDir);
    sal_Int32 recoverInterruptedRunning(const OUString& monthDir,
                                        const OUString& reason);

private:
    TaskStore& m_store;
    TaskQueue& m_queue;
};

} // namespace kqoffice::ai::cowork

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
