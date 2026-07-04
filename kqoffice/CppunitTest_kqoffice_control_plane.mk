# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the 可圈office project (V4 M4: Control Plane).
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_CppunitTest_CppunitTest,kqoffice_control_plane))

$(eval $(call gb_CppunitTest_set_include,kqoffice_control_plane,\
    -I$(SRCDIR)/kqoffice/source/ai/control \
    $$(INCLUDE) \
))

# Day-1: link against Library_kqoffice_ai instead of duplicating the control
# plane sources into the test binary (gbuild fdo#47246 forbids the
# duplicate-object pattern even with hidden visibility on macOS).
$(eval $(call gb_CppunitTest_add_exception_objects,kqoffice_control_plane, \
    kqoffice/qa/cppunit/test_control_plane \
))

# Day-1: pure-logic tests only. No URE / VCL — those would set URE=true
# and pull in unobootstrap/vclbootstrap protectors that require a
# working services.rdb. See solenv/gbuild/CppunitTest.mk:114-129.
# use_sdk_api just adds API include paths (no URE), needed for IDL headers.
$(eval $(call gb_CppunitTest_use_libraries,kqoffice_control_plane, \
    cppu \
    cppuhelper \
    kqoffice_ai \
    sal \
))

$(eval $(call gb_CppunitTest_use_sdk_api,kqoffice_control_plane))

# vim: set noet sw=4 ts=4: