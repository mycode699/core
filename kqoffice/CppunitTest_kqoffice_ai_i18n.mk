# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the 可圈office project (V2 AI i18n: string provider test).
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_CppunitTest_CppunitTest,kqoffice_ai_i18n))

$(eval $(call gb_CppunitTest_set_include,kqoffice_ai_i18n,\
    -I$(SRCDIR)/kqoffice/source/ai/i18n \
    $$(INCLUDE) \
))

# Header-only test — only the test source needs compiling. The i18n
# provider is header-only (AiI18nStrings.hxx) and does not require
# linking against Library_kqoffice_ai.
$(eval $(call gb_CppunitTest_add_exception_objects,kqoffice_ai_i18n, \
    kqoffice/qa/cppunit/test_ai_i18n \
))

$(eval $(call gb_CppunitTest_use_libraries,kqoffice_ai_i18n, \
    cppu \
    cppuhelper \
    sal \
))

$(eval $(call gb_CppunitTest_use_sdk_api,kqoffice_ai_i18n))

# vim: set noet sw=4 ts=4:
