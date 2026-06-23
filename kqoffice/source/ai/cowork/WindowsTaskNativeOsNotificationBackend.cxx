/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V2 W5: Windows Native OS Notification Backend).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "TaskNativeOsNotificationBackend.hxx"

#include <o3tl/char16_t2wchar_t.hxx>

#include <prewin.h>
#include <shellapi.h>
#include <windows.h>
#include <postwin.h>

#include <algorithm>
#include <cwchar>
#include <iterator>
#include <memory>
#include <utility>

namespace kqoffice::ai::cowork
{
namespace
{
constexpr UINT KQOFFICE_TASK_NOTIFICATION_MESSAGE = WM_APP + 0x451;
constexpr UINT KQOFFICE_TASK_NOTIFICATION_ID = 0x5157;

void copyToNotifyBuffer(wchar_t* dest, size_t destCount, const OUString& text)
{
    if (!dest || destCount == 0)
        return;

    const size_t count = std::min<size_t>(static_cast<size_t>(text.getLength()),
                                         destCount - 1);
    std::wmemcpy(dest, o3tl::toW(text.getStr()), count);
    dest[count] = L'\0';
}

TaskNativeOsNotificationClickPayload clickPayloadFromRequest(
    const TaskOsNotificationRequest& request)
{
    TaskNativeOsNotificationClickPayload payload;
    payload.valid = request.valid && request.actionToken == taskOsNotificationPostedToken()
                    && request.clickToken == taskOsNotificationClickToken()
                    && request.reviewRequest.valid
                    && !request.reviewRequest.monthDir.isEmpty()
                    && !request.reviewRequest.taskId.isEmpty()
                    && !request.reviewRequest.resultPlanId.isEmpty();
    payload.actionToken = request.actionToken;
    payload.clickToken = request.clickToken;
    payload.monthDir = request.reviewRequest.monthDir;
    payload.taskId = request.reviewRequest.taskId;
    payload.resultPlanId = request.reviewRequest.resultPlanId;
    payload.evidenceId = request.reviewRequest.evidenceId;
    return payload;
}

class WindowsNotificationWindow
{
public:
    explicit WindowsNotificationWindow(TaskNativeOsNotificationClickPayload payload)
        : m_payload(std::move(payload))
    {
    }

    bool create()
    {
        HINSTANCE hInstance = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &WindowsNotificationWindow::windowProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = L"KQOfficeTaskNativeNotificationWindow";
        RegisterClassExW(&wc);

        m_hWnd = CreateWindowExW(0, wc.lpszClassName, L"KQOfficeTaskNativeNotification",
                                0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                hInstance, this);
        return m_hWnd != nullptr;
    }

    bool showBalloon(const TaskOsNotificationRequest& request)
    {
        if (!m_hWnd)
            return false;

        NOTIFYICONDATAW data = {};
        data.cbSize = sizeof(data);
        data.hWnd = m_hWnd;
        data.uID = KQOFFICE_TASK_NOTIFICATION_ID;
        data.uCallbackMessage = KQOFFICE_TASK_NOTIFICATION_MESSAGE;
        data.uFlags = NIF_MESSAGE | NIF_INFO;
        copyToNotifyBuffer(data.szInfoTitle, std::size(data.szInfoTitle), request.title);
        copyToNotifyBuffer(data.szInfo, std::size(data.szInfo), request.body);
        data.dwInfoFlags = NIIF_INFO;

        if (!Shell_NotifyIconW(NIM_ADD, &data))
            return false;

        m_iconAdded = true;
        return Shell_NotifyIconW(NIM_MODIFY, &data);
    }

    void destroy()
    {
        delete this;
    }

    ~WindowsNotificationWindow()
    {
        HWND hWnd = m_hWnd;
        m_hWnd = nullptr;

        if (m_iconAdded && hWnd)
        {
            NOTIFYICONDATAW data = {};
            data.cbSize = sizeof(data);
            data.hWnd = hWnd;
            data.uID = KQOFFICE_TASK_NOTIFICATION_ID;
            Shell_NotifyIconW(NIM_DELETE, &data);
            m_iconAdded = false;
        }
        if (hWnd && !m_inWindowDestroy)
        {
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
            DestroyWindow(hWnd);
        }
    }

    static LRESULT CALLBACK windowProc(HWND hWnd, UINT message, WPARAM wParam,
                                       LPARAM lParam)
    {
        WindowsNotificationWindow* self = nullptr;
        if (message == WM_NCCREATE)
        {
            CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<WindowsNotificationWindow*>(create->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            return TRUE;
        }

        self = reinterpret_cast<WindowsNotificationWindow*>(
            GetWindowLongPtrW(hWnd, GWLP_USERDATA));

        if (message == KQOFFICE_TASK_NOTIFICATION_MESSAGE
            && wParam == KQOFFICE_TASK_NOTIFICATION_ID
            && (lParam == NIN_BALLOONUSERCLICK || lParam == NIN_BALLOONTIMEOUT
                || lParam == WM_LBUTTONUP))
        {
            if (self)
            {
                if (lParam == NIN_BALLOONUSERCLICK || lParam == WM_LBUTTONUP)
                    dispatchNativeOsNotificationClickPayload(self->m_payload);
                self->destroy();
            }
            return 0;
        }

        if (message == WM_DESTROY && self)
        {
            self->m_inWindowDestroy = true;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
            delete self;
            return 0;
        }

        return DefWindowProcW(hWnd, message, wParam, lParam);
    }

    HWND m_hWnd = nullptr;
    bool m_iconAdded = false;
    bool m_inWindowDestroy = false;
    TaskNativeOsNotificationClickPayload m_payload;
};

class WindowsTaskNativeOsNotificationBackend final
    : public TaskNativeOsNotificationBackend
{
public:
    TaskNativeOsNotificationPostResult postNativeNotification(
        const TaskOsNotificationRequest& request) override
    {
        TaskNativeOsNotificationPostResult result;
        result.request = request;
        result.backendToken = u"windows-shell-notifyicon"_ustr;

        if (!request.valid || request.title.isEmpty())
        {
            result.failureReason = u"invalid-native-os-notification-request"_ustr;
            return result;
        }

        result.attempted = true;
        TaskNativeOsNotificationClickPayload payload = clickPayloadFromRequest(request);
        if (!payload.valid)
        {
            result.failureReason = u"invalid-native-os-notification-click-payload"_ustr;
            return result;
        }

        WindowsNotificationWindow* window = new WindowsNotificationWindow(payload);
        if (!window->create() || !window->showBalloon(request))
        {
            window->destroy();
            result.failureReason = taskNativeOsNotificationFailedToken();
            return result;
        }

        result.submitted = true;
        result.failureReason.clear();
        return result;
    }
};
}

std::unique_ptr<TaskNativeOsNotificationBackend>
createWindowsTaskNativeOsNotificationBackend()
{
    return std::make_unique<WindowsTaskNativeOsNotificationBackend>();
}

} // namespace kqoffice::ai::cowork

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
