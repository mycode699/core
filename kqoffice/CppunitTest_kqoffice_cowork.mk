# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the 可圈office project (V2 W5: Async Cowork).
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_CppunitTest_CppunitTest,kqoffice_cowork))

$(eval $(call gb_CppunitTest_set_include,kqoffice_cowork,\
    -I$(SRCDIR)/kqoffice/source/ai/cowork \
    $$(INCLUDE) \
))

# Day-0: link against Library_kqoffice_ai instead of duplicating the cowork
# sources into the test binary (gbuild fdo#47246 forbids the duplicate-object
# pattern even with hidden visibility on macOS).
$(eval $(call gb_CppunitTest_add_exception_objects,kqoffice_cowork, \
    kqoffice/qa/cppunit/test_cowork \
))

# Day-0: pure-logic tests only. No URE / VCL — those would set URE=true
# and pull in unobootstrap/vclbootstrap protectors that require a
# working services.rdb. See solenv/gbuild/CppunitTest.mk:114-129.
# use_sdk_api just adds API include paths (no URE), needed for IDL headers.
$(eval $(call gb_CppunitTest_use_libraries,kqoffice_cowork, \
    cppu \
    cppuhelper \
    kqoffice_ai \
    sal \
))

$(eval $(call gb_CppunitTest_use_sdk_api,kqoffice_cowork))

# vim: set noet sw=4 ts=4:
