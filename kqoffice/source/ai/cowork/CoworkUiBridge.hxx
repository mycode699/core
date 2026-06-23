/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V2 W5: Async Cowork UI Bridge).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_COWORK_COWORKUIBRIDGE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_COWORK_COWORKUIBRIDGE_HXX

#include "AsyncTask.hxx"
#include "TaskOsNotificationBridge.hxx"
#include "TaskReviewBridge.hxx"
#include "TaskStore.hxx"

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <memory>

namespace kqoffice::ai::cowork
{

struct SAL_DLLPUBLIC_EXPORT CoworkUiBridgeResult
{
    bool enqueued = false;
    bool threadStarted = false;
    bool dispatched = false;
    bool workerCalled = false;
    TaskState finalState = TaskState::Pending;
    OUString taskId;
    OUString resultPlanId;
    OUString evidenceId;
    sal_Int32 notificationCount = 0;
    sal_Int32 autoOpenAttemptCount = 0;
    sal_Int32 autoOpenSuccessCount = 0;
    sal_Int32 osNotificationPostedCount = 0;
    TaskReviewOpenResult lastReviewOpenResult;
};

// Owns one real background Cowork UI task. prepare() persists the pending
// envelope synchronously so the UI can render it before start() dispatches
// the worker. The owner must join before destroying the review-open sink.
class SAL_DLLPUBLIC_EXPORT CoworkUiTaskBridgeJob
{
public:
    CoworkUiTaskBridgeJob(const OUString& monthDir,
                          const AsyncTaskEnvelope& task,
                          TaskReviewOpenSink& openSink,
                          bool enableNativeNotifications = true);
    ~CoworkUiTaskBridgeJob();

    CoworkUiTaskBridgeJob(const CoworkUiTaskBridgeJob&) = delete;
    CoworkUiTaskBridgeJob& operator=(const CoworkUiTaskBridgeJob&) = delete;

    bool prepare();
    bool start();
    void join();
    bool isStarted() const;
    bool isDone() const;
    CoworkUiBridgeResult result() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_xImpl;
};

SAL_DLLPUBLIC_EXPORT bool runCoworkUiTaskBridge(
    TaskStore& store,
    const OUString& monthDir,
    const AsyncTaskEnvelope& task,
    TaskNotificationSink& notificationSink,
    TaskReviewOpenSink& openSink,
    CoworkUiBridgeResult* out = nullptr);

SAL_DLLPUBLIC_EXPORT bool runCoworkUiTaskBridge(
    TaskStore& store,
    const OUString& monthDir,
    const AsyncTaskEnvelope& task,
    TaskNotificationSink& notificationSink,
    TaskReviewOpenSink& openSink,
    TaskOsNotificationSink& osNotificationSink,
    CoworkUiBridgeResult* out = nullptr);

} // namespace kqoffice::ai::cowork

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
