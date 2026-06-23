/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V2 W5: Async Cowork OS Notification Bridge).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "TaskOsNotificationBridge.hxx"

namespace kqoffice::ai::cowork
{

OUString taskOsNotificationPostedToken()
{
    return u"os-notification-posted"_ustr;
}

OUString taskOsNotificationClickToken()
{
    return u"os-notification-click-review"_ustr;
}

namespace
{
TaskReviewOpenResult failedClickResult(const TaskOsNotificationRequest& request,
                                       const OUString& reason)
{
    TaskReviewOpenResult result;
    result.monthDir = request.reviewRequest.monthDir;
    result.taskId = request.reviewRequest.taskId;
    result.resultPlanId = request.reviewRequest.resultPlanId;
    result.evidenceId = request.reviewRequest.evidenceId;
    result.failureReason = reason;
    return result;
}
}

TaskOsNotificationSink::~TaskOsNotificationSink() = default;

void InMemoryTaskOsNotificationSink::postNotification(
    const TaskOsNotificationRequest& request)
{
    std::scoped_lock guard(m_mutex);
    m_requests.push_back(request);
}

std::vector<TaskOsNotificationRequest> InMemoryTaskOsNotificationSink::snapshot() const
{
    std::scoped_lock guard(m_mutex);
    return m_requests;
}

OsNotificationTaskNotificationSink::OsNotificationTaskNotificationSink(
    TaskNotificationSink& notificationSink,
    TaskOsNotificationSink& osSink)
    : m_notificationSink(notificationSink)
    , m_osSink(osSink)
{
}

void OsNotificationTaskNotificationSink::notify(const TaskNotification& notification)
{
    m_notificationSink.notify(notification);

    TaskOsNotificationRequest request;
    if (!buildOsNotificationFromTaskNotification(notification, request))
        return;

    m_osSink.postNotification(request);

    std::scoped_lock guard(m_mutex);
    ++m_postedCount;
    m_lastPostedRequest = request;
}

sal_Int32 OsNotificationTaskNotificationSink::postedCount() const
{
    std::scoped_lock guard(m_mutex);
    return m_postedCount;
}

TaskOsNotificationRequest
OsNotificationTaskNotificationSink::lastPostedRequest() const
{
    std::scoped_lock guard(m_mutex);
    return m_lastPostedRequest;
}

bool buildOsNotificationFromTaskNotification(
    const TaskNotification& notification,
    TaskOsNotificationRequest& out)
{
    out = TaskOsNotificationRequest();

    TaskReviewRequest reviewRequest;
    if (!buildReviewRequestFromNotification(notification, reviewRequest))
        return false;

    out.valid = true;
    out.actionToken = taskOsNotificationPostedToken();
    out.clickToken = taskOsNotificationClickToken();
    out.title = u"任务已完成，等待审批"_ustr;
    out.body = notification.taskId + u" 已准备好打开 review"_ustr;
    out.reviewRequest = reviewRequest;
    return true;
}

bool openReviewFromOsNotificationClick(
    const TaskOsNotificationRequest& request,
    TaskStore& store,
    TaskReviewOpenSink& sink,
    TaskReviewOpenResult* out)
{
    if (!request.valid || request.actionToken != taskOsNotificationPostedToken()
        || request.clickToken != taskOsNotificationClickToken()
        || !request.reviewRequest.valid)
    {
        if (out)
            *out = failedClickResult(request, u"invalid-os-notification-click"_ustr);
        return false;
    }

    return openReviewRequest(request.reviewRequest, store, sink, out);
}

} // namespace kqoffice::ai::cowork

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
