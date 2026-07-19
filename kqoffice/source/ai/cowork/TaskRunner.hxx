/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V2 W5: Async Cowork Task Runner).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_COWORK_TASKRUNNER_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_COWORK_TASKRUNNER_HXX

#include "AsyncTask.hxx"
#include "TaskScheduler.hxx"
#include "TaskStore.hxx"

#include <atomic>
#include <osl/thread.h>
#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <mutex>
#include <vector>

namespace kqoffice::ai::cowork
{

enum class TaskNotificationKind
{
    WorkerStarted,
    TaskRunning,
    AwaitingReview,
    TaskFailed,
    WorkerIdle,
    WorkerEmpty,
};

SAL_DLLPUBLIC_EXPORT OUString taskNotificationKindToken(TaskNotificationKind kind);

struct SAL_DLLPUBLIC_EXPORT TaskNotification
{
    TaskNotificationKind kind = TaskNotificationKind::WorkerEmpty;
    OUString monthDir;
    OUString taskId;
    TaskState state = TaskState::Pending;
    OUString resultPlanId;
    OUString evidenceId;
    OUString failureReason;
};

class SAL_DLLPUBLIC_EXPORT TaskNotificationSink
{
public:
    virtual ~TaskNotificationSink();

    virtual void notify(const TaskNotification& notification) = 0;
};

class SAL_DLLPUBLIC_EXPORT InMemoryTaskNotificationSink final
    : public TaskNotificationSink
{
public:
    void notify(const TaskNotification& notification) override;

    std::vector<TaskNotification> snapshot() const;
    std::vector<OUString> tokens() const;

private:
    mutable std::mutex m_mutex;
    std::vector<TaskNotification> m_notifications;
};

struct SAL_DLLPUBLIC_EXPORT TaskRunnerResult
{
    bool threadStarted = false;
    bool dispatched = false;
    bool workerCalled = false;
    bool workerIdle = true;
    OUString taskId;
    TaskState finalState = TaskState::Pending;
    sal_Int32 runningBefore = 0;
    sal_Int32 runningAfter = 0;
    sal_Int32 notificationCount = 0;
};

class SAL_DLLPUBLIC_EXPORT TaskRunner
{
public:
    TaskRunner(TaskScheduler& scheduler, TaskNotificationSink& sink);

    // Starts a real worker thread for one pending task and joins it before
    // returning. This deterministic entry point is used by cppunit and by
    // the first UI bridge before the long-lived worker loop lands.
    bool startOneAndJoinForTest(const OUString& monthDir,
                                TaskWorker& worker,
                                TaskRunnerResult* out = nullptr);

    /// Cancellation token for cooperative shutdown.
    void cancel() { m_bCancelled = true; }
    bool isCancelled() const { return m_bCancelled; }

    /// Thread ID of the last worker that ran, or 0.
    oslThreadIdentifier lastThreadId() const { return m_lastThreadId; }

    /// Each runner owns a dedicated TaskStore for thread-local I/O.
    TaskStore& store() { return m_store; }

    /// Direct access for internal thread classes.
    std::atomic<sal_uIntPtr> m_lastThreadId;

private:
    TaskScheduler& m_scheduler;
    TaskNotificationSink& m_sink;
    TaskStore m_store;
    std::atomic<bool> m_bCancelled;
};

/// Cancel the TaskRunner currently bound for UI (if any). Safe no-op when idle.
SAL_DLLPUBLIC_EXPORT void cancelActiveTaskRunner();
/// True while startOneAndJoinForTest (or UI bridge) holds an active runner.
SAL_DLLPUBLIC_EXPORT bool hasActiveTaskRunner();

} // namespace kqoffice::ai::cowork

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
