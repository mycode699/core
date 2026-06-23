/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V2 W5: Native OS Notification Backend).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_COWORK_TASKNATIVEOSNOTIFICATIONBACKEND_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_COWORK_TASKNATIVEOSNOTIFICATIONBACKEND_HXX

#include "TaskOsNotificationBridge.hxx"

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <memory>
#include <mutex>

namespace kqoffice::ai::cowork
{

SAL_DLLPUBLIC_EXPORT OUString taskNativeOsNotificationUnavailableToken();
SAL_DLLPUBLIC_EXPORT OUString taskNativeOsNotificationSubmittedToken();
SAL_DLLPUBLIC_EXPORT OUString taskNativeOsNotificationFailedToken();

SAL_DLLPUBLIC_EXPORT bool taskNativeOsNotificationSmokeClickEnabled();

struct SAL_DLLPUBLIC_EXPORT TaskNativeOsNotificationPostResult
{
    bool attempted = false;
    bool submitted = false;
    OUString backendToken;
    OUString failureReason;
    TaskOsNotificationRequest request;
};

struct SAL_DLLPUBLIC_EXPORT TaskNativeOsNotificationClickPayload
{
    bool valid = false;
    OUString actionToken;
    OUString clickToken;
    OUString monthDir;
    OUString taskId;
    OUString resultPlanId;
    OUString evidenceId;
};

class SAL_DLLPUBLIC_EXPORT TaskNativeOsNotificationClickSink
{
public:
    virtual ~TaskNativeOsNotificationClickSink();

    virtual void handleNativeNotificationClick(
        const TaskNativeOsNotificationClickPayload& payload) = 0;
};

class SAL_DLLPUBLIC_EXPORT TaskNativeOsNotificationBackend
{
public:
    virtual ~TaskNativeOsNotificationBackend();

    virtual TaskNativeOsNotificationPostResult postNativeNotification(
        const TaskOsNotificationRequest& request) = 0;
};

class SAL_DLLPUBLIC_EXPORT FallbackTaskNativeOsNotificationBackend final
    : public TaskNativeOsNotificationBackend
{
public:
    TaskNativeOsNotificationPostResult postNativeNotification(
        const TaskOsNotificationRequest& request) override;
};

class SAL_DLLPUBLIC_EXPORT NativeTaskOsNotificationSink final
    : public TaskOsNotificationSink
{
public:
    explicit NativeTaskOsNotificationSink(TaskNativeOsNotificationBackend& backend);

    void postNotification(const TaskOsNotificationRequest& request) override;

    sal_Int32 attemptedCount() const;
    sal_Int32 submittedCount() const;
    TaskNativeOsNotificationPostResult lastResult() const;

private:
    TaskNativeOsNotificationBackend& m_backend;
    mutable std::mutex m_mutex;
    sal_Int32 m_attemptedCount = 0;
    sal_Int32 m_submittedCount = 0;
    TaskNativeOsNotificationPostResult m_lastResult;
};

SAL_DLLPUBLIC_EXPORT std::unique_ptr<TaskNativeOsNotificationBackend>
createPlatformTaskNativeOsNotificationBackend();

SAL_DLLPUBLIC_EXPORT TaskOsNotificationRequest
buildOsNotificationRequestFromNativeClickPayload(
    const TaskNativeOsNotificationClickPayload& payload);

SAL_DLLPUBLIC_EXPORT bool openReviewFromNativeOsNotificationClick(
    const TaskNativeOsNotificationClickPayload& payload,
    TaskStore& store,
    TaskReviewOpenSink& sink,
    TaskReviewOpenResult* out = nullptr);

SAL_DLLPUBLIC_EXPORT void setTaskNativeOsNotificationClickSink(
    std::shared_ptr<TaskNativeOsNotificationClickSink> sink);

SAL_DLLPUBLIC_EXPORT void clearTaskNativeOsNotificationClickSink(
    const TaskNativeOsNotificationClickSink* expectedSink = nullptr);

SAL_DLLPUBLIC_EXPORT bool dispatchNativeOsNotificationClickPayload(
    const TaskNativeOsNotificationClickPayload& payload);

SAL_DLLPUBLIC_EXPORT void recordNativeOsNotificationSubmitEvidence(
    const TaskNativeOsNotificationPostResult& result);

SAL_DLLPUBLIC_EXPORT void recordNativeOsNotificationClickDispatchEvidence(
    const TaskNativeOsNotificationClickPayload& payload,
    bool dispatched,
    const OUString& backendToken);

SAL_DLLPUBLIC_EXPORT void recordNativeOsNotificationReviewOpenEvidence(
    const TaskNativeOsNotificationClickPayload& payload,
    const TaskReviewOpenResult& result,
    bool opened);

#if defined MACOSX
SAL_DLLPUBLIC_EXPORT std::unique_ptr<TaskNativeOsNotificationBackend>
createMacosTaskNativeOsNotificationBackend();
#endif

#if defined _WIN32
SAL_DLLPUBLIC_EXPORT std::unique_ptr<TaskNativeOsNotificationBackend>
createWindowsTaskNativeOsNotificationBackend();
#endif

} // namespace kqoffice::ai::cowork

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
