# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the 可圈office project (V2 W1: Provider Runtime).
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_Library_Library,kqoffice_ai))

$(eval $(call gb_Library_set_include,kqoffice_ai,\
    $$(INCLUDE) \
))

$(eval $(call gb_Library_use_sdk_api,kqoffice_ai))

$(eval $(call gb_Library_use_libraries,kqoffice_ai,\
    cppu \
    cppuhelper \
    sal \
    salhelper \
))

$(eval $(call gb_Library_set_componentfile,kqoffice_ai,kqoffice/util/kqoffice_ai,services))

$(eval $(call gb_Library_add_exception_objects,kqoffice_ai,\
    kqoffice/source/ai/provider/EvidenceRecorder \
    kqoffice/source/ai/provider/OllamaAdapter \
    kqoffice/source/ai/provider/Provider \
    kqoffice/source/ai/provider/RuntimePlanStub \
    kqoffice/source/ai/provider/ServiceModePolicy \
    kqoffice/source/ai/cowork/CoworkUiBridge \
    kqoffice/source/ai/cowork/TaskNativeOsNotificationBackend \
    kqoffice/source/ai/cowork/TaskQueue \
    kqoffice/source/ai/cowork/TaskOsNotificationBridge \
    kqoffice/source/ai/cowork/TaskReviewBridge \
    kqoffice/source/ai/cowork/TaskRunner \
    kqoffice/source/ai/cowork/TaskScheduler \
    kqoffice/source/ai/cowork/TaskStore \
))

ifeq ($(OS),MACOSX)
$(eval $(call gb_Library_add_cxxflags,kqoffice_ai,\
    $(gb_OBJCXXFLAGS) \
))
$(eval $(call gb_Library_add_objcxxobjects,kqoffice_ai,\
    kqoffice/source/ai/cowork/MacTaskNativeOsNotificationBackend \
))
$(eval $(call gb_Library_use_system_darwin_frameworks,kqoffice_ai,\
    Foundation \
))
endif

ifeq ($(OS),WNT)
$(eval $(call gb_Library_add_exception_objects,kqoffice_ai,\
    kqoffice/source/ai/cowork/WindowsTaskNativeOsNotificationBackend \
))
$(eval $(call gb_Library_use_system_win32_libs,kqoffice_ai,\
    shell32 \
))
endif

# vim: set noet sw=4 ts=4:
