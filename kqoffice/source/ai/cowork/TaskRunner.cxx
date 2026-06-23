/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V2 W5: Async Cowork Task Runner).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "TaskRunner.hxx"

#include <osl/thread.hxx>

#include <memory>
#include <utility>

namespace kqoffice::ai::cowork
{
namespace
{
void appendNotification(TaskNotificationSink& sink,
                        TaskNotificationKind kind,
                        const OUString& monthDir,
                        const TaskSchedulerRunResult& run)
{
    TaskNotification notification;
    notification.kind = kind;
    notification.monthDir = monthDir;
    notification.taskId = run.taskId;
    notification.state = run.finalState;
    notification.resultPlanId = run.resultPlanId;
    notification.evidenceId = run.evidenceId;
    notification.failureReason = run.failureReason;
    sink.notify(notification);
}

class NotifyingWorker final : public TaskWorker
{
public:
    NotifyingWorker(TaskWorker& worker, TaskNotificationSink& sink, OUString monthDir)
        : m_worker(worker)
        , m_sink(sink)
        , m_monthDir(std::move(monthDir))
    {
    }

    TaskWorkerResult run(const AsyncTaskEnvelope& runningTask) override
    {
        TaskNotification notification;
        notification.kind = TaskNotificationKind::TaskRunning;
        notification.monthDir = m_monthDir;
        notification.taskId = runningTask.taskId;
        notification.state = runningTask.state;
        notification.evidenceId = u"dispatched"_ustr;
        m_sink.notify(notification);
        return m_worker.run(runningTask);
    }

private:
    TaskWorker& m_worker;
    TaskNotificationSink& m_sink;
    OUString m_monthDir;
};

class RunOneThread final : public osl::Thread
{
public:
    RunOneThread(TaskScheduler& scheduler,
                 TaskNotificationSink& sink,
                 OUString monthDir,
                 TaskWorker& worker)
        : m_scheduler(scheduler)
        , m_sink(sink)
        , m_monthDir(std::move(monthDir))
        , m_worker(worker)
    {
    }

    void SAL_CALL run() override
    {
        osl_setThreadName("KQOfficeTaskRunner");

        TaskNotification start;
        start.kind = TaskNotificationKind::WorkerStarted;
        start.monthDir = m_monthDir;
        m_sink.notify(start);

        TaskSchedulerRunResult runResult;
        NotifyingWorker notifyingWorker(m_worker, m_sink, m_monthDir);
        m_ok = m_scheduler.runOne(m_monthDir, notifyingWorker, &runResult);
        m_runResult = runResult;

        if (!runResult.dispatched)
        {
            TaskNotification empty;
            empty.kind = TaskNotificationKind::WorkerEmpty;
            empty.monthDir = m_monthDir;
            m_sink.notify(empty);
            return;
        }

        if (runResult.finalState == TaskState::AwaitingReview)
            appendNotification(m_sink, TaskNotificationKind::AwaitingReview, m_monthDir, runResult);
        else if (runResult.finalState == TaskState::Failed)
            appendNotification(m_sink, TaskNotificationKind::TaskFailed, m_monthDir, runResult);

        appendNotification(m_sink, TaskNotificationKind::WorkerIdle, m_monthDir, runResult);
    }

    bool ok() const { return m_ok; }
    const TaskSchedulerRunResult& runResult() const { return m_runResult; }

private:
    TaskScheduler& m_scheduler;
    TaskNotificationSink& m_sink;
    OUString m_monthDir;
    TaskWorker& m_worker;
    bool m_ok = false;
    TaskSchedulerRunResult m_runResult;
};
}

OUString taskNotificationKindToken(TaskNotificationKind kind)
{
    switch (kind)
    {
        case TaskNotificationKind::WorkerStarted:
            return u"worker-started"_ustr;
        case TaskNotificationKind::TaskRunning:
            return u"task-running"_ustr;
        case TaskNotificationKind::AwaitingReview:
            return u"awaiting-review-notification"_ustr;
        case TaskNotificationKind::TaskFailed:
            return u"task-failed-notification"_ustr;
        case TaskNotificationKind::WorkerIdle:
            return u"worker-idle"_ustr;
        case TaskNotificationKind::WorkerEmpty:
            return u"worker-empty"_ustr;
    }
    return OUString();
}

TaskNotificationSink::~TaskNotificationSink() = default;

void InMemoryTaskNotificationSink::notify(const TaskNotification& notification)
{
    std::scoped_lock guard(m_mutex);
    m_notifications.push_back(notification);
}

std::vector<TaskNotification> InMemoryTaskNotificationSink::snapshot() const
{
    std::scoped_lock guard(m_mutex);
    return m_notifications;
}

std::vector<OUString> InMemoryTaskNotificationSink::tokens() const
{
    std::vector<OUString> result;
    std::scoped_lock guard(m_mutex);
    result.reserve(m_notifications.size());
    for (const auto& notification : m_notifications)
        result.push_back(taskNotificationKindToken(notification.kind));
    return result;
}

TaskRunner::TaskRunner(TaskScheduler& scheduler, TaskNotificationSink& sink)
    : m_scheduler(scheduler)
    , m_sink(sink)
{
}

bool TaskRunner::startOneAndJoinForTest(const OUString& monthDir,
                                        TaskWorker& worker,
                                        TaskRunnerResult* out)
{
    auto thread = std::make_unique<RunOneThread>(m_scheduler, m_sink, monthDir, worker);
    thread->create();
    thread->join();

    const TaskSchedulerRunResult& run = thread->runResult();
    TaskRunnerResult result;
    result.threadStarted = true;
    result.dispatched = run.dispatched;
    result.workerCalled = run.workerCalled;
    result.workerIdle = run.workerIdle;
    result.taskId = run.taskId;
    result.finalState = run.finalState;
    result.runningBefore = run.runningBefore;
    result.runningAfter = run.runningAfter;

    auto notifications = dynamic_cast<InMemoryTaskNotificationSink*>(&m_sink);
    if (notifications)
        result.notificationCount = static_cast<sal_Int32>(notifications->snapshot().size());

    if (out)
        *out = result;
    return thread->ok();
}

} // namespace kqoffice::ai::cowork

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
