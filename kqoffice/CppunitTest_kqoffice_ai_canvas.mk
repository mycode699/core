# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the 可圈office project (V5: AI Canvas Mode).
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_CppunitTest_CppunitTest,kqoffice_ai_canvas))

$(eval $(call gb_CppunitTest_set_include,kqoffice_ai_canvas,\
    -I$(SRCDIR)/kqoffice/source/ai/canvas \
    -I$(SRCDIR)/kqoffice/source/ai/chat \
    -I$(SRCDIR)/kqoffice/source/ai/provider \
    $$(INCLUDE) \
))

$(eval $(call gb_CppunitTest_add_exception_objects,kqoffice_ai_canvas, \
    kqoffice/qa/cppunit/test_ai_canvas \
))

$(eval $(call gb_CppunitTest_use_libraries,kqoffice_ai_canvas, \
    cppu \
    cppuhelper \
    kqoffice_ai \
    sal \
))

$(eval $(call gb_CppunitTest_use_sdk_api,kqoffice_ai_canvas))

# vim: set noet sw=4 ts=4:
