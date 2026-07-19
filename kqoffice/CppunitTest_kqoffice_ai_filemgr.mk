# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the 可圈office project (V6: AI File Manager).
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_CppunitTest_CppunitTest,kqoffice_ai_filemgr))

$(eval $(call gb_CppunitTest_set_include,kqoffice_ai_filemgr,\
    -I$(SRCDIR)/kqoffice/source/ai/filemgr \
    -I$(SRCDIR)/kqoffice/source/ai/control \
    $$(INCLUDE) \
))

$(eval $(call gb_CppunitTest_add_exception_objects,kqoffice_ai_filemgr, \
    kqoffice/qa/cppunit/test_ai_filemgr \
))

$(eval $(call gb_CppunitTest_use_libraries,kqoffice_ai_filemgr, \
    cppu \
    cppuhelper \
    kqoffice_ai \
    sal \
))

$(eval $(call gb_CppunitTest_use_sdk_api,kqoffice_ai_filemgr))

# vim: set noet sw=4 ts=4:
