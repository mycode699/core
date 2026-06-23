/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V2 W5: Native OS Notification Backend).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "TaskNativeOsNotificationBackend.hxx"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include <rtl/textenc.h>
#include <rtl/ustrbuf.hxx>

namespace kqoffice::ai::cowork
{
namespace
{
std::mutex& nativeClickSinkMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::shared_ptr<TaskNativeOsNotificationClickSink>& nativeClickSink()
{
    static std::shared_ptr<TaskNativeOsNotificationClickSink> sink;
    return sink;
}

OUString envVar(const char* name)
{
    const char* value = std::getenv(name);
    if (!value || !*value)
        return OUString();
    return OUString(value, std::strlen(value), RTL_TEXTENCODING_UTF8);
}

void appendEscaped(OUStringBuffer& buf, const OUString& value)
{
    buf.append('"');
    for (sal_Int32 i = 0; i < value.getLength(); ++i)
    {
        sal_Unicode c = value[i];
        switch (c)
        {
            case '"': buf.append("\\\""); break;
            case '\\': buf.append("\\\\"); break;
            case '\n': buf.append("\\n"); break;
            case '\r': buf.append("\\r"); break;
            case '\t': buf.append("\\t"); break;
            default:
                if (c < 0x20)
                {
                    char tmp[8];
                    std::snprintf(tmp, sizeof(tmp), "\\u%04x", static_cast<unsigned>(c));
                    buf.appendAscii(tmp);
                }
                else
                {
                    buf.append(c);
                }
        }
    }
    buf.append('"');
}

void appendJsonField(OUStringBuffer& buf, const char* key, const OUString& value)
{
    buf.append(",\"");
    buf.appendAscii(key);
    buf.append("\":");
    appendEscaped(buf, value);
}

void appendJsonBoolField(OUStringBuffer& buf, const char* key, bool value)
{
    buf.append(",\"");
    buf.appendAscii(key);
    buf.append("\":");
    buf.appendAscii(value ? "true" : "false");
}

void appendPayloadFields(OUStringBuffer& buf,
                         const TaskNativeOsNotificationClickPayload& payload)
{
    appendJsonBoolField(buf, "valid", payload.valid);
    appendJsonField(buf, "action_token", payload.actionToken);
    appendJsonField(buf, "click_token", payload.clickToken);
    appendJsonField(buf, "month_dir", payload.monthDir);
    appendJsonField(buf, "task_id", payload.taskId);
    appendJsonField(buf, "result_plan_id", payload.resultPlanId);
    appendJsonField(buf, "evidence_id", payload.evidenceId);
}

TaskNativeOsNotificationClickPayload payloadFromRequest(
    const TaskOsNotificationRequest& request)
{
    TaskNativeOsNotificationClickPayload payload;
    payload.actionToken = request.actionToken;
    payload.clickToken = request.clickToken;
    payload.monthDir = request.reviewRequest.monthDir;
    payload.taskId = request.reviewRequest.taskId;
    payload.resultPlanId = request.reviewRequest.resultPlanId;
    payload.evidenceId = request.reviewRequest.evidenceId;
    payload.valid = request.valid && !payload.actionToken.isEmpty()
                    && !payload.clickToken.isEmpty() && !payload.monthDir.isEmpty()
                    && !payload.taskId.isEmpty() && !payload.resultPlanId.isEmpty();
    return payload;
}

void appendEvidenceLine(const OUString& event, OUStringBuffer& fields)
{
    const OUString path = envVar("KQOFFICE_AI_NATIVE_NOTIFICATION_EVIDENCE_LOG");
    if (path.isEmpty())
        return;

    OUStringBuffer line(256);
    line.append("{\"event\":");
    appendEscaped(line, event);
    line.append(fields.makeStringAndClear());
    line.append("}\n");

    OString utf8 = OUStringToOString(line.makeStringAndClear(), RTL_TEXTENCODING_UTF8);
    OString sysPath = OUStringToOString(path, RTL_TEXTENCODING_UTF8);
    std::FILE* fp = std::fopen(sysPath.getStr(), "ab");
    if (!fp)
        return;
    std::fwrite(utf8.getStr(), 1, utf8.getLength(), fp);
    std::fclose(fp);
}
}

OUString taskNativeOsNotificationUnavailableToken()
{
    return u"native-os-notification-unavailable"_ustr;
}

OUString taskNativeOsNotificationSubmittedToken()
{
    return u"native-os-notification-submitted"_ustr;
}

OUString taskNativeOsNotificationFailedToken()
{
    return u"native-os-notification-failed"_ustr;
}

bool taskNativeOsNotificationSmokeClickEnabled()
{
    return std::getenv("KQOFFICE_AI_NATIVE_NOTIFICATION_SMOKE_CLICK") != nullptr;
}

TaskNativeOsNotificationBackend::~TaskNativeOsNotificationBackend() = default;
TaskNativeOsNotificationClickSink::~TaskNativeOsNotificationClickSink() = default;

TaskNativeOsNotificationPostResult
FallbackTaskNativeOsNotificationBackend::postNativeNotification(
    const TaskOsNotificationRequest& request)
{
    TaskNativeOsNotificationPostResult result;
    result.request = request;
    result.backendToken = taskNativeOsNotificationUnavailableToken();
    result.failureReason = taskNativeOsNotificationUnavailableToken();
    return result;
}

NativeTaskOsNotificationSink::NativeTaskOsNotificationSink(
    TaskNativeOsNotificationBackend& backend)
    : m_backend(backend)
{
}

void NativeTaskOsNotificationSink::postNotification(
    const TaskOsNotificationRequest& request)
{
    TaskNativeOsNotificationPostResult result
        = m_backend.postNativeNotification(request);

    std::scoped_lock guard(m_mutex);
    if (result.attempted)
        ++m_attemptedCount;
    if (result.submitted)
        ++m_submittedCount;
    m_lastResult = result;
}

sal_Int32 NativeTaskOsNotificationSink::attemptedCount() const
{
    std::scoped_lock guard(m_mutex);
    return m_attemptedCount;
}

sal_Int32 NativeTaskOsNotificationSink::submittedCount() const
{
    std::scoped_lock guard(m_mutex);
    return m_submittedCount;
}

TaskNativeOsNotificationPostResult NativeTaskOsNotificationSink::lastResult() const
{
    std::scoped_lock guard(m_mutex);
    return m_lastResult;
}

std::unique_ptr<TaskNativeOsNotificationBackend>
createPlatformTaskNativeOsNotificationBackend()
{
#if defined MACOSX
    return createMacosTaskNativeOsNotificationBackend();
#elif defined _WIN32
    return createWindowsTaskNativeOsNotificationBackend();
#else
    return std::make_unique<FallbackTaskNativeOsNotificationBackend>();
#endif
}

TaskOsNotificationRequest buildOsNotificationRequestFromNativeClickPayload(
    const TaskNativeOsNotificationClickPayload& payload)
{
    TaskOsNotificationRequest request;
    if (!payload.valid || payload.actionToken != taskOsNotificationPostedToken()
        || payload.clickToken != taskOsNotificationClickToken()
        || payload.monthDir.isEmpty() || payload.taskId.isEmpty()
        || payload.resultPlanId.isEmpty())
        return request;

    request.valid = true;
    request.actionToken = payload.actionToken;
    request.clickToken = payload.clickToken;
    request.reviewRequest.valid = true;
    request.reviewRequest.actionToken = u"open-review-request"_ustr;
    request.reviewRequest.monthDir = payload.monthDir;
    request.reviewRequest.taskId = payload.taskId;
    request.reviewRequest.resultPlanId = payload.resultPlanId;
    request.reviewRequest.evidenceId = payload.evidenceId;
    return request;
}

bool openReviewFromNativeOsNotificationClick(
    const TaskNativeOsNotificationClickPayload& payload,
    TaskStore& store,
    TaskReviewOpenSink& sink,
    TaskReviewOpenResult* out)
{
    const TaskOsNotificationRequest request
        = buildOsNotificationRequestFromNativeClickPayload(payload);
    return openReviewFromOsNotificationClick(request, store, sink, out);
}

void setTaskNativeOsNotificationClickSink(
    std::shared_ptr<TaskNativeOsNotificationClickSink> sink)
{
    std::scoped_lock guard(nativeClickSinkMutex());
    nativeClickSink() = std::move(sink);
}

void clearTaskNativeOsNotificationClickSink(
    const TaskNativeOsNotificationClickSink* expectedSink)
{
    std::scoped_lock guard(nativeClickSinkMutex());
    if (!expectedSink || nativeClickSink().get() == expectedSink)
        nativeClickSink().reset();
}

bool dispatchNativeOsNotificationClickPayload(
    const TaskNativeOsNotificationClickPayload& payload)
{
    if (!payload.valid)
        return false;

    std::shared_ptr<TaskNativeOsNotificationClickSink> sink;
    {
        std::scoped_lock guard(nativeClickSinkMutex());
        sink = nativeClickSink();
    }

    if (!sink)
        return false;

    sink->handleNativeNotificationClick(payload);
    return true;
}

void recordNativeOsNotificationSubmitEvidence(
    const TaskNativeOsNotificationPostResult& result)
{
    TaskNativeOsNotificationClickPayload payload = payloadFromRequest(result.request);
    OUStringBuffer fields;
    appendJsonField(fields, "backend_token", result.backendToken);
    appendJsonBoolField(fields, "attempted", result.attempted);
    appendJsonBoolField(fields, "submitted", result.submitted);
    appendJsonField(fields, "failure_reason", result.failureReason);
    appendPayloadFields(fields, payload);
    appendEvidenceLine(u"native-os-notification-submit"_ustr, fields);
}

void recordNativeOsNotificationClickDispatchEvidence(
    const TaskNativeOsNotificationClickPayload& payload,
    bool dispatched,
    const OUString& backendToken)
{
    OUStringBuffer fields;
    appendJsonField(fields, "backend_token", backendToken);
    appendJsonBoolField(fields, "dispatched", dispatched);
    appendPayloadFields(fields, payload);
    appendEvidenceLine(u"native-os-notification-click-dispatch"_ustr, fields);
}

void recordNativeOsNotificationReviewOpenEvidence(
    const TaskNativeOsNotificationClickPayload& payload,
    const TaskReviewOpenResult& result,
    bool opened)
{
    OUStringBuffer fields;
    appendJsonBoolField(fields, "opened", opened);
    appendJsonField(fields, "failure_reason", result.failureReason);
    appendPayloadFields(fields, payload);
    appendJsonField(fields, "opened_task_id", result.taskId);
    appendJsonField(fields, "opened_result_plan_id", result.resultPlanId);
    appendJsonField(fields, "opened_evidence_id", result.evidenceId);
    appendEvidenceLine(u"native-os-notification-review-open"_ustr, fields);
}

} // namespace kqoffice::ai::cowork

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
