/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V2 W5: Async Cowork OS Notification Bridge).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_COWORK_TASKOSNOTIFICATIONBRIDGE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_COWORK_TASKOSNOTIFICATIONBRIDGE_HXX

#include "TaskReviewBridge.hxx"
#include "TaskRunner.hxx"
#include "TaskStore.hxx"

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <mutex>
#include <vector>

namespace kqoffice::ai::cowork
{

SAL_DLLPUBLIC_EXPORT OUString taskOsNotificationPostedToken();
SAL_DLLPUBLIC_EXPORT OUString taskOsNotificationClickToken();

struct SAL_DLLPUBLIC_EXPORT TaskOsNotificationRequest
{
    bool valid = false;
    OUString actionToken;
    OUString clickToken;
    OUString title;
    OUString body;
    TaskReviewRequest reviewRequest;
};

class SAL_DLLPUBLIC_EXPORT TaskOsNotificationSink
{
public:
    virtual ~TaskOsNotificationSink();

    virtual void postNotification(const TaskOsNotificationRequest& request) = 0;
};

class SAL_DLLPUBLIC_EXPORT InMemoryTaskOsNotificationSink final
    : public TaskOsNotificationSink
{
public:
    void postNotification(const TaskOsNotificationRequest& request) override;

    std::vector<TaskOsNotificationRequest> snapshot() const;

private:
    mutable std::mutex m_mutex;
    std::vector<TaskOsNotificationRequest> m_requests;
};

class SAL_DLLPUBLIC_EXPORT OsNotificationTaskNotificationSink final
    : public TaskNotificationSink
{
public:
    OsNotificationTaskNotificationSink(TaskNotificationSink& notificationSink,
                                       TaskOsNotificationSink& osSink);

    void notify(const TaskNotification& notification) override;

    sal_Int32 postedCount() const;
    TaskOsNotificationRequest lastPostedRequest() const;

private:
    TaskNotificationSink& m_notificationSink;
    TaskOsNotificationSink& m_osSink;
    mutable std::mutex m_mutex;
    sal_Int32 m_postedCount = 0;
    TaskOsNotificationRequest m_lastPostedRequest;
};

SAL_DLLPUBLIC_EXPORT bool buildOsNotificationFromTaskNotification(
    const TaskNotification& notification,
    TaskOsNotificationRequest& out);

SAL_DLLPUBLIC_EXPORT bool openReviewFromOsNotificationClick(
    const TaskOsNotificationRequest& request,
    TaskStore& store,
    TaskReviewOpenSink& sink,
    TaskReviewOpenResult* out = nullptr);

} // namespace kqoffice::ai::cowork

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
