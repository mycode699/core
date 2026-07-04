# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the 可圈office project (V4 M2: AgentChat Core).
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_CppunitTest_CppunitTest,kqoffice_agent_chat))

$(eval $(call gb_CppunitTest_set_include,kqoffice_agent_chat,\
    -I$(SRCDIR)/kqoffice/source/ai/chat \
    -I$(SRCDIR)/kqoffice/source/ai/i18n \
    $$(INCLUDE) \
))

$(eval $(call gb_CppunitTest_add_exception_objects,kqoffice_agent_chat, \
    kqoffice/qa/cppunit/test_agent_chat \
))

$(eval $(call gb_CppunitTest_use_libraries,kqoffice_agent_chat, \
    cppu \
    cppuhelper \
    kqoffice_ai \
    sal \
))

$(eval $(call gb_CppunitTest_use_sdk_api,kqoffice_agent_chat))

# vim: set noet sw=4 ts=4: