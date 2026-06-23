/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V2 W5: Async Cowork UI Bridge).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "CoworkUiBridge.hxx"

#include "TaskNativeOsNotificationBackend.hxx"
#include "TaskQueue.hxx"
#include "TaskRunner.hxx"
#include "TaskScheduler.hxx"

#include <osl/thread.hxx>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <utility>

namespace kqoffice::ai::cowork
{
namespace
{
OUString digest16(const OUString& seed)
{
    sal_uInt64 hash = 1469598103934665603ULL;
    for (sal_Int32 i = 0; i < seed.getLength(); ++i)
    {
        hash ^= static_cast<sal_uInt64>(seed[i]);
        hash *= 1099511628211ULL;
    }

    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(hash));
    return OUString::createFromAscii(buf);
}

OUString planIdForTask(const OUString& taskId)
{
    return u"ap-"_ustr + digest16(taskId + u":plan"_ustr);
}

OUString evidenceIdForTask(const OUString& taskId)
{
    return u"ev-"_ustr + digest16(taskId + u":evidence"_ustr);
}

class CoworkUiBridgeWorker final : public TaskWorker
{
public:
    explicit CoworkUiBridgeWorker(sal_uInt64 nMinimumRunningMs = 0)
        : m_nMinimumRunningMs(nMinimumRunningMs)
    {
    }

    TaskWorkerResult run(const AsyncTaskEnvelope& runningTask) override
    {
        if (m_nMinimumRunningMs > 0)
        {
            TimeValue aDelay;
            aDelay.Seconds = static_cast<sal_Int64>(m_nMinimumRunningMs / 1000);
            aDelay.Nanosec = static_cast<sal_Int64>((m_nMinimumRunningMs % 1000) * 1000000);
            osl::Thread::wait(aDelay);
        }
        return TaskWorkerResult::awaitingReview(
            planIdForTask(runningTask.taskId),
            evidenceIdForTask(runningTask.taskId));
    }

private:
    sal_uInt64 m_nMinimumRunningMs;
};
}

class CoworkUiTaskBridgeJob::Impl final : public osl::Thread
{
public:
    Impl(OUString aMonthDir, AsyncTaskEnvelope aTask, TaskReviewOpenSink& rOpenSink,
         bool bEnableNativeNotifications)
        : m_aMonthDir(std::move(aMonthDir))
        , m_aTask(std::move(aTask))
        , m_rOpenSink(rOpenSink)
        , m_bEnableNativeNotifications(bEnableNativeNotifications)
    {
        m_aResult.taskId = m_aTask.taskId;
    }

    ~Impl() override { joinJob(); }

    bool prepare()
    {
        if (m_bStarted || m_bPrepared || m_aMonthDir.isEmpty() || m_aTask.taskId.isEmpty())
            return false;

        TaskQueue aQueue(m_aStore);
        if (!aQueue.enqueue(m_aTask))
            return false;

        {
            std::scoped_lock aGuard(m_aResultMutex);
            m_aResult.enqueued = true;
            m_aResult.finalState = TaskState::Pending;
        }
        m_bPrepared = true;
        return true;
    }

    bool startJob()
    {
        if (!m_bPrepared || m_bStarted)
            return false;
        if (!create())
        {
            m_bDone = true;
            return false;
        }
        m_bStarted = true;
        return true;
    }

    void joinJob()
    {
        if (m_bStarted && !m_bJoined.exchange(true))
            join();
    }

    bool isStarted() const { return m_bStarted; }
    bool isDone() const { return m_bDone; }

    CoworkUiBridgeResult result() const
    {
        std::scoped_lock aGuard(m_aResultMutex);
        return m_aResult;
    }

private:
    void SAL_CALL run() override
    {
        TaskQueue aQueue(m_aStore);
        TaskScheduler aScheduler(m_aStore, aQueue);
        AutoOpenReviewNotificationSink aAutoSink(m_aNotificationSink, m_aStore, m_rOpenSink);
        std::unique_ptr<TaskNativeOsNotificationBackend> xNativeBackend
            = m_bEnableNativeNotifications
                  ? createPlatformTaskNativeOsNotificationBackend()
                  : std::make_unique<FallbackTaskNativeOsNotificationBackend>();
        NativeTaskOsNotificationSink aNativeSink(*xNativeBackend);
        OsNotificationTaskNotificationSink aRunnerSink(aAutoSink, aNativeSink);
        TaskRunner aRunner(aScheduler, aRunnerSink);

        // The first UI bridge worker is deterministic and otherwise completes
        // immediately. Keep the real running state visible long enough for the
        // main-thread dialog poller to render it without blocking the UI.
        CoworkUiBridgeWorker aWorker(600);
        TaskRunnerResult aRunnerResult;
        const bool bRunnerOk
            = aRunner.startOneAndJoinForTest(m_aMonthDir, aWorker, &aRunnerResult);

        CoworkUiBridgeResult aResult;
        aResult.enqueued = true;
        aResult.threadStarted = aRunnerResult.threadStarted;
        aResult.dispatched = aRunnerResult.dispatched;
        aResult.workerCalled = aRunnerResult.workerCalled;
        aResult.finalState = aRunnerResult.finalState;
        aResult.taskId = m_aTask.taskId;
        aResult.notificationCount
            = static_cast<sal_Int32>(m_aNotificationSink.snapshot().size());
        aResult.autoOpenAttemptCount = aAutoSink.autoOpenAttemptCount();
        aResult.autoOpenSuccessCount = aAutoSink.autoOpenSuccessCount();
        aResult.osNotificationPostedCount = aRunnerSink.postedCount();
        aResult.lastReviewOpenResult = aAutoSink.lastAutoOpenResult();
        aResult.resultPlanId = aResult.lastReviewOpenResult.resultPlanId;
        aResult.evidenceId = aResult.lastReviewOpenResult.evidenceId;

        {
            std::scoped_lock aGuard(m_aResultMutex);
            m_aResult = std::move(aResult);
            (void)bRunnerOk;
        }
        m_bDone = true;
    }

    OUString m_aMonthDir;
    AsyncTaskEnvelope m_aTask;
    TaskReviewOpenSink& m_rOpenSink;
    bool m_bEnableNativeNotifications;
    TaskStore m_aStore;
    InMemoryTaskNotificationSink m_aNotificationSink;
    mutable std::mutex m_aResultMutex;
    CoworkUiBridgeResult m_aResult;
    std::atomic<bool> m_bPrepared = false;
    std::atomic<bool> m_bStarted = false;
    std::atomic<bool> m_bDone = false;
    std::atomic<bool> m_bJoined = false;
};

CoworkUiTaskBridgeJob::CoworkUiTaskBridgeJob(const OUString& rMonthDir,
                                             const AsyncTaskEnvelope& rTask,
                                             TaskReviewOpenSink& rOpenSink,
                                             bool bEnableNativeNotifications)
    : m_xImpl(std::make_unique<Impl>(rMonthDir, rTask, rOpenSink,
                                     bEnableNativeNotifications))
{
}

CoworkUiTaskBridgeJob::~CoworkUiTaskBridgeJob() = default;

bool CoworkUiTaskBridgeJob::prepare() { return m_xImpl->prepare(); }

bool CoworkUiTaskBridgeJob::start() { return m_xImpl->startJob(); }

void CoworkUiTaskBridgeJob::join() { m_xImpl->joinJob(); }

bool CoworkUiTaskBridgeJob::isStarted() const { return m_xImpl->isStarted(); }

bool CoworkUiTaskBridgeJob::isDone() const { return m_xImpl->isDone(); }

CoworkUiBridgeResult CoworkUiTaskBridgeJob::result() const { return m_xImpl->result(); }

namespace
{
bool runCoworkUiTaskBridgeImpl(TaskStore& store,
                               const OUString& monthDir,
                               const AsyncTaskEnvelope& task,
                               TaskNotificationSink& runnerSink,
                               AutoOpenReviewNotificationSink& autoSink,
                               OsNotificationTaskNotificationSink* osSink,
                               CoworkUiBridgeResult* out)
{
    CoworkUiBridgeResult result;
    result.taskId = task.taskId;

    auto finish = [&](bool ok) {
        if (out)
            *out = result;
        return ok;
    };

    if (monthDir.isEmpty() || task.taskId.isEmpty())
        return finish(false);

    TaskQueue queue(store);
    if (!queue.enqueue(task))
        return finish(false);
    result.enqueued = true;

    TaskScheduler scheduler(store, queue);
    TaskRunner runner(scheduler, runnerSink);
    CoworkUiBridgeWorker worker;

    TaskRunnerResult runnerResult;
    const bool runnerOk = runner.startOneAndJoinForTest(monthDir, worker, &runnerResult);

    result.threadStarted = runnerResult.threadStarted;
    result.dispatched = runnerResult.dispatched;
    result.workerCalled = runnerResult.workerCalled;
    result.finalState = runnerResult.finalState;
    result.notificationCount = runnerResult.notificationCount;
    result.autoOpenAttemptCount = autoSink.autoOpenAttemptCount();
    result.autoOpenSuccessCount = autoSink.autoOpenSuccessCount();
    result.osNotificationPostedCount = osSink ? osSink->postedCount() : 0;
    result.lastReviewOpenResult = autoSink.lastAutoOpenResult();
    result.resultPlanId = result.lastReviewOpenResult.resultPlanId;
    result.evidenceId = result.lastReviewOpenResult.evidenceId;

    return finish(runnerOk && result.lastReviewOpenResult.opened);
}
}

bool runCoworkUiTaskBridge(TaskStore& store,
                           const OUString& monthDir,
                           const AsyncTaskEnvelope& task,
                           TaskNotificationSink& notificationSink,
                           TaskReviewOpenSink& openSink,
                           CoworkUiBridgeResult* out)
{
    AutoOpenReviewNotificationSink autoSink(notificationSink, store, openSink);
    return runCoworkUiTaskBridgeImpl(store, monthDir, task, autoSink, autoSink,
                                    nullptr, out);
}

bool runCoworkUiTaskBridge(TaskStore& store,
                           const OUString& monthDir,
                           const AsyncTaskEnvelope& task,
                           TaskNotificationSink& notificationSink,
                           TaskReviewOpenSink& openSink,
                           TaskOsNotificationSink& osNotificationSink,
                           CoworkUiBridgeResult* out)
{
    AutoOpenReviewNotificationSink autoSink(notificationSink, store, openSink);
    OsNotificationTaskNotificationSink osSink(autoSink, osNotificationSink);
    return runCoworkUiTaskBridgeImpl(store, monthDir, task, osSink, autoSink,
                                    &osSink, out);
}

} // namespace kqoffice::ai::cowork

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
