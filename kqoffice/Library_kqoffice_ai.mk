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
    kqoffice/source/ai/provider/ServiceModePolicy \
))

# vim: set noet sw=4 ts=4:
