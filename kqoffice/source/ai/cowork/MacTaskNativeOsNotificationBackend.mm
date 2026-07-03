/* -*- Mode: ObjC++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V2 W5: macOS Native OS Notification Backend).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "TaskNativeOsNotificationBackend.hxx"

#include <rtl/strbuf.hxx>
#include <rtl/textenc.h>

typedef TimeValue KQOfficeOslTimeValue;
#define TimeValue KQOfficeMacOSTimeValue
#import <Foundation/Foundation.h>
#undef TimeValue
typedef KQOfficeOslTimeValue TimeValue;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
@interface KQOfficeTaskNotificationDelegate : NSObject <NSUserNotificationCenterDelegate>
@end
#pragma clang diagnostic pop

namespace kqoffice::ai::cowork
{
namespace
{
NSString* toNSString(const OUString& text)
{
    OString utf8 = OUStringToOString(text, RTL_TEXTENCODING_UTF8);
    NSString* value = [NSString stringWithUTF8String:utf8.getStr()];
    return value ? value : @"";
}

OUString toOUString(id value)
{
    if (![value isKindOfClass:[NSString class]])
        return OUString();

    const char* utf8 = [static_cast<NSString*>(value) UTF8String];
    return utf8 ? OStringToOUString(OString(utf8), RTL_TEXTENCODING_UTF8) : OUString();
}

TaskNativeOsNotificationClickPayload payloadFromUserInfo(NSDictionary* userInfo)
{
    TaskNativeOsNotificationClickPayload payload;
    if (!userInfo)
        return payload;

    payload.actionToken = toOUString([userInfo objectForKey:@"actionToken"]);
    payload.clickToken = toOUString([userInfo objectForKey:@"clickToken"]);
    payload.monthDir = toOUString([userInfo objectForKey:@"monthDir"]);
    payload.taskId = toOUString([userInfo objectForKey:@"taskId"]);
    payload.resultPlanId = toOUString([userInfo objectForKey:@"resultPlanId"]);
    payload.evidenceId = toOUString([userInfo objectForKey:@"evidenceId"]);
    payload.valid = !payload.actionToken.isEmpty() && !payload.clickToken.isEmpty()
                    && !payload.monthDir.isEmpty() && !payload.taskId.isEmpty()
                    && !payload.resultPlanId.isEmpty();
    return payload;
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

void installNotificationDelegate()
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    static KQOfficeTaskNotificationDelegate* delegate
        = [[KQOfficeTaskNotificationDelegate alloc] init];
    [[NSUserNotificationCenter defaultUserNotificationCenter] setDelegate:delegate];
#pragma clang diagnostic pop
}

class MacTaskNativeOsNotificationBackend final
    : public TaskNativeOsNotificationBackend
{
public:
    TaskNativeOsNotificationPostResult postNativeNotification(
        const TaskOsNotificationRequest& request) override
    {
        TaskNativeOsNotificationPostResult result;
        result.request = request;
        result.backendToken = u"macos-nsusernotification"_ustr;

        if (!request.valid || request.title.isEmpty())
        {
            result.failureReason = u"invalid-native-os-notification-request"_ustr;
            return result;
        }

        result.attempted = true;

        @autoreleasepool
        {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            @try
            {
                installNotificationDelegate();

                NSUserNotification* notification = [[NSUserNotification alloc] init];
                notification.title = toNSString(request.title);
                notification.informativeText = toNSString(request.body);
                notification.identifier = toNSString(request.reviewRequest.taskId);
                notification.userInfo = @{
                    @"actionToken" : toNSString(request.actionToken),
                    @"clickToken" : toNSString(request.clickToken),
                    @"monthDir" : toNSString(request.reviewRequest.monthDir),
                    @"taskId" : toNSString(request.reviewRequest.taskId),
                    @"resultPlanId" : toNSString(request.reviewRequest.resultPlanId),
                    @"evidenceId" : toNSString(request.reviewRequest.evidenceId)
                };

                [[NSUserNotificationCenter defaultUserNotificationCenter]
                    deliverNotification:notification];
                result.submitted = true;
                result.failureReason.clear();
            }
            @catch (NSException*)
            {
                result.submitted = false;
                result.failureReason = taskNativeOsNotificationFailedToken();
            }
#pragma clang diagnostic pop
            }

            recordNativeOsNotificationSubmitEvidence(result);
            if (result.submitted && taskNativeOsNotificationSmokeClickEnabled())
            {
                const TaskNativeOsNotificationClickPayload payload
                    = payloadFromRequest(result.request);
                const bool dispatched = dispatchNativeOsNotificationClickPayload(payload);
                recordNativeOsNotificationClickDispatchEvidence(
                    payload, dispatched, result.backendToken);
            }
            return result;
        }
    };
}

} // namespace kqoffice::ai::cowork

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
@implementation KQOfficeTaskNotificationDelegate

- (BOOL)userNotificationCenter:(NSUserNotificationCenter*)center
       shouldPresentNotification:(NSUserNotification*)notification
{
    (void)center;
    (void)notification;
    return YES;
}

- (void)userNotificationCenter:(NSUserNotificationCenter*)center
       didActivateNotification:(NSUserNotification*)notification
{
    (void)center;
    kqoffice::ai::cowork::dispatchNativeOsNotificationClickPayload(
        kqoffice::ai::cowork::payloadFromUserInfo([notification userInfo]));
}

@end
#pragma clang diagnostic pop

namespace kqoffice::ai::cowork
{

std::unique_ptr<TaskNativeOsNotificationBackend>
createMacosTaskNativeOsNotificationBackend()
{
    return std::make_unique<MacTaskNativeOsNotificationBackend>();
}

} // namespace kqoffice::ai::cowork

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
