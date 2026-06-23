/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V2 W5: Async Cowork Review Bridge).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_COWORK_TASKREVIEWBRIDGE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_COWORK_TASKREVIEWBRIDGE_HXX

#include "TaskRunner.hxx"
#include "TaskStore.hxx"

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <mutex>
#include <vector>

namespace kqoffice::ai::cowork
{

SAL_DLLPUBLIC_EXPORT OUString taskReviewRequestToken();
SAL_DLLPUBLIC_EXPORT OUString taskReviewOpenedToken();
SAL_DLLPUBLIC_EXPORT OUString taskReviewAcceptedToken();

struct SAL_DLLPUBLIC_EXPORT TaskReviewRequest
{
    bool valid = false;
    OUString actionToken;
    OUString monthDir;
    OUString taskId;
    OUString resultPlanId;
    OUString evidenceId;
};

class SAL_DLLPUBLIC_EXPORT TaskReviewRequestSink
{
public:
    virtual ~TaskReviewRequestSink();

    virtual void openReview(const TaskReviewRequest& request) = 0;
};

class SAL_DLLPUBLIC_EXPORT InMemoryTaskReviewRequestSink final
    : public TaskReviewRequestSink
{
public:
    void openReview(const TaskReviewRequest& request) override;

    std::vector<TaskReviewRequest> snapshot() const;

private:
    mutable std::mutex m_mutex;
    std::vector<TaskReviewRequest> m_requests;
};

struct SAL_DLLPUBLIC_EXPORT TaskReviewOpenResult
{
    bool opened = false;
    OUString actionToken;
    OUString sourceToken;
    OUString monthDir;
    OUString taskId;
    OUString resultPlanId;
    OUString evidenceId;
    OUString failureReason;
};

struct SAL_DLLPUBLIC_EXPORT TaskReviewAcceptResult
{
    bool applied = false;
    OUString actionToken;
    OUString sourceToken;
    OUString monthDir;
    OUString taskId;
    OUString resultPlanId;
    OUString evidenceId;
    OUString failureReason;
    TaskState finalState = TaskState::Pending;
};

class SAL_DLLPUBLIC_EXPORT TaskReviewOpenSink
{
public:
    virtual ~TaskReviewOpenSink();

    virtual void openDiffReview(const TaskReviewOpenResult& result) = 0;
};

class SAL_DLLPUBLIC_EXPORT InMemoryTaskReviewOpenSink final
    : public TaskReviewOpenSink
{
public:
    void openDiffReview(const TaskReviewOpenResult& result) override;

    std::vector<TaskReviewOpenResult> snapshot() const;

private:
    mutable std::mutex m_mutex;
    std::vector<TaskReviewOpenResult> m_results;
};

class SAL_DLLPUBLIC_EXPORT AutoOpenReviewNotificationSink final
    : public TaskNotificationSink
{
public:
    AutoOpenReviewNotificationSink(TaskNotificationSink& notificationSink,
                                   TaskStore& store,
                                   TaskReviewOpenSink& openSink);

    void notify(const TaskNotification& notification) override;

    sal_Int32 autoOpenAttemptCount() const;
    sal_Int32 autoOpenSuccessCount() const;
    TaskReviewOpenResult lastAutoOpenResult() const;

private:
    TaskNotificationSink& m_notificationSink;
    TaskStore& m_store;
    TaskReviewOpenSink& m_openSink;
    mutable std::mutex m_mutex;
    sal_Int32 m_autoOpenAttempts = 0;
    sal_Int32 m_autoOpenSuccesses = 0;
    TaskReviewOpenResult m_lastAutoOpenResult;
};

SAL_DLLPUBLIC_EXPORT bool buildReviewRequestFromNotification(
    const TaskNotification& notification,
    TaskReviewRequest& out);

SAL_DLLPUBLIC_EXPORT bool openReviewFromNotification(
    const TaskNotification& notification,
    TaskReviewRequestSink& sink);

SAL_DLLPUBLIC_EXPORT bool openReviewRequest(
    const TaskReviewRequest& request,
    TaskStore& store,
    TaskReviewOpenSink& sink,
    TaskReviewOpenResult* out = nullptr);

SAL_DLLPUBLIC_EXPORT bool openReviewFromNotification(
    const TaskNotification& notification,
    TaskStore& store,
    TaskReviewOpenSink& sink,
    TaskReviewOpenResult* out = nullptr);

SAL_DLLPUBLIC_EXPORT bool acceptReviewResult(
    const TaskReviewOpenResult& openResult,
    TaskStore& store,
    TaskReviewAcceptResult* out = nullptr);

} // namespace kqoffice::ai::cowork

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
