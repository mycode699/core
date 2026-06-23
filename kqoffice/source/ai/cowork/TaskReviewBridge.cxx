/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V2 W5: Async Cowork Review Bridge).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "TaskReviewBridge.hxx"

#include "TaskQueue.hxx"

namespace kqoffice::ai::cowork
{

OUString taskReviewRequestToken()
{
    return u"open-review-request"_ustr;
}

OUString taskReviewOpenedToken()
{
    return u"diff-review-opened"_ustr;
}

OUString taskReviewAcceptedToken()
{
    return u"diff-review-accepted"_ustr;
}

namespace
{
TaskReviewOpenResult failedOpenResult(const TaskReviewRequest& request,
                                      const OUString& reason)
{
    TaskReviewOpenResult result;
    result.monthDir = request.monthDir;
    result.taskId = request.taskId;
    result.resultPlanId = request.resultPlanId;
    result.evidenceId = request.evidenceId;
    result.failureReason = reason;
    return result;
}

TaskReviewAcceptResult acceptResultFromOpenResult(
    const TaskReviewOpenResult& openResult)
{
    TaskReviewAcceptResult result;
    result.sourceToken = openResult.actionToken;
    result.monthDir = openResult.monthDir;
    result.taskId = openResult.taskId;
    result.resultPlanId = openResult.resultPlanId;
    result.evidenceId = openResult.evidenceId;
    return result;
}
}

TaskReviewRequestSink::~TaskReviewRequestSink() = default;

void InMemoryTaskReviewRequestSink::openReview(const TaskReviewRequest& request)
{
    std::scoped_lock guard(m_mutex);
    m_requests.push_back(request);
}

std::vector<TaskReviewRequest> InMemoryTaskReviewRequestSink::snapshot() const
{
    std::scoped_lock guard(m_mutex);
    return m_requests;
}

TaskReviewOpenSink::~TaskReviewOpenSink() = default;

void InMemoryTaskReviewOpenSink::openDiffReview(const TaskReviewOpenResult& result)
{
    std::scoped_lock guard(m_mutex);
    m_results.push_back(result);
}

std::vector<TaskReviewOpenResult> InMemoryTaskReviewOpenSink::snapshot() const
{
    std::scoped_lock guard(m_mutex);
    return m_results;
}

AutoOpenReviewNotificationSink::AutoOpenReviewNotificationSink(
    TaskNotificationSink& notificationSink,
    TaskStore& store,
    TaskReviewOpenSink& openSink)
    : m_notificationSink(notificationSink)
    , m_store(store)
    , m_openSink(openSink)
{
}

void AutoOpenReviewNotificationSink::notify(const TaskNotification& notification)
{
    m_notificationSink.notify(notification);

    if (notification.kind != TaskNotificationKind::AwaitingReview)
        return;

    TaskReviewOpenResult result;
    const bool opened = openReviewFromNotification(notification, m_store, m_openSink, &result);

    std::scoped_lock guard(m_mutex);
    ++m_autoOpenAttempts;
    if (opened)
        ++m_autoOpenSuccesses;
    m_lastAutoOpenResult = result;
}

sal_Int32 AutoOpenReviewNotificationSink::autoOpenAttemptCount() const
{
    std::scoped_lock guard(m_mutex);
    return m_autoOpenAttempts;
}

sal_Int32 AutoOpenReviewNotificationSink::autoOpenSuccessCount() const
{
    std::scoped_lock guard(m_mutex);
    return m_autoOpenSuccesses;
}

TaskReviewOpenResult AutoOpenReviewNotificationSink::lastAutoOpenResult() const
{
    std::scoped_lock guard(m_mutex);
    return m_lastAutoOpenResult;
}

bool buildReviewRequestFromNotification(const TaskNotification& notification,
                                        TaskReviewRequest& out)
{
    out = TaskReviewRequest();

    if (notification.kind != TaskNotificationKind::AwaitingReview)
        return false;
    if (notification.monthDir.isEmpty() || notification.taskId.isEmpty()
        || notification.resultPlanId.isEmpty())
        return false;

    out.valid = true;
    out.actionToken = taskReviewRequestToken();
    out.monthDir = notification.monthDir;
    out.taskId = notification.taskId;
    out.resultPlanId = notification.resultPlanId;
    out.evidenceId = notification.evidenceId;
    return true;
}

bool openReviewFromNotification(const TaskNotification& notification,
                                TaskReviewRequestSink& sink)
{
    TaskReviewRequest request;
    if (!buildReviewRequestFromNotification(notification, request))
        return false;

    sink.openReview(request);
    return true;
}

bool openReviewRequest(const TaskReviewRequest& request,
                       TaskStore& store,
                       TaskReviewOpenSink& sink,
                       TaskReviewOpenResult* out)
{
    auto finishFailed = [&](const OUString& reason) {
        if (out)
            *out = failedOpenResult(request, reason);
        return false;
    };

    if (!request.valid || request.actionToken != taskReviewRequestToken()
        || request.monthDir.isEmpty() || request.taskId.isEmpty()
        || request.resultPlanId.isEmpty())
    {
        return finishFailed(u"invalid-open-review-request"_ustr);
    }

    AsyncTaskEnvelope task;
    if (!store.read(request.monthDir, request.taskId, task))
        return finishFailed(u"review-task-not-found"_ustr);

    if (task.state != TaskState::AwaitingReview)
        return finishFailed(u"review-task-not-awaiting-review"_ustr);

    if (task.resultPlanId != request.resultPlanId)
        return finishFailed(u"review-plan-mismatch"_ustr);

    TaskReviewOpenResult result;
    result.opened = true;
    result.actionToken = taskReviewOpenedToken();
    result.sourceToken = u"notification-click-review-opened"_ustr;
    result.monthDir = request.monthDir;
    result.taskId = request.taskId;
    result.resultPlanId = request.resultPlanId;
    result.evidenceId = request.evidenceId.isEmpty()
                            ? (task.evidenceIds.empty() ? OUString() : task.evidenceIds.back())
                            : request.evidenceId;
    sink.openDiffReview(result);
    if (out)
        *out = result;
    return true;
}

bool openReviewFromNotification(const TaskNotification& notification,
                                TaskStore& store,
                                TaskReviewOpenSink& sink,
                                TaskReviewOpenResult* out)
{
    TaskReviewRequest request;
    if (!buildReviewRequestFromNotification(notification, request))
    {
        if (out)
            *out = failedOpenResult(request, u"invalid-open-review-notification"_ustr);
        return false;
    }
    return openReviewRequest(request, store, sink, out);
}

bool acceptReviewResult(const TaskReviewOpenResult& openResult,
                        TaskStore& store,
                        TaskReviewAcceptResult* out)
{
    TaskReviewAcceptResult result = acceptResultFromOpenResult(openResult);

    auto finish = [&](bool ok) {
        if (out)
            *out = result;
        return ok;
    };

    if (!openResult.opened || openResult.actionToken != taskReviewOpenedToken()
        || openResult.monthDir.isEmpty() || openResult.taskId.isEmpty()
        || openResult.resultPlanId.isEmpty())
    {
        result.failureReason = u"invalid-review-accept-request"_ustr;
        return finish(false);
    }

    AsyncTaskEnvelope task;
    if (!store.read(openResult.monthDir, openResult.taskId, task))
    {
        result.failureReason = u"review-task-not-found"_ustr;
        return finish(false);
    }
    result.finalState = task.state;

    if (task.state != TaskState::AwaitingReview)
    {
        result.failureReason = u"review-task-not-awaiting-review"_ustr;
        return finish(false);
    }

    if (task.resultPlanId != openResult.resultPlanId)
    {
        result.failureReason = u"review-plan-mismatch"_ustr;
        return finish(false);
    }

    TaskQueue queue(store);
    AsyncTaskEnvelope applied;
    if (!queue.markApplied(openResult.monthDir, openResult.taskId, &applied))
    {
        result.failureReason = u"review-accept-transition-failed"_ustr;
        return finish(false);
    }

    result.applied = true;
    result.actionToken = taskReviewAcceptedToken();
    result.finalState = applied.state;
    result.resultPlanId = applied.resultPlanId;
    result.evidenceId = applied.evidenceIds.empty() ? OUString() : applied.evidenceIds.back();
    return finish(true);
}

} // namespace kqoffice::ai::cowork

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
