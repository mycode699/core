# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the 可圈office project (V2 W1: Provider Runtime).
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_CppunitTest_CppunitTest,kqoffice_provider))

$(eval $(call gb_CppunitTest_set_include,kqoffice_provider,\
    -I$(SRCDIR)/kqoffice/source/ai/provider \
    $$(INCLUDE) \
))

# Day-0 test compiles Provider/ServiceModePolicy objects directly into the
# test binary (same pattern as cui_commandpalette_fuzzy + FuzzyMatcher).
# The library symbols are hidden by default on macOS, and Day-0 tests
# never bring up UNO so the library is not loaded — no collision.
$(eval $(call gb_CppunitTest_add_exception_objects,kqoffice_provider, \
    kqoffice/qa/cppunit/test_provider \
    kqoffice/source/ai/provider/Provider \
    kqoffice/source/ai/provider/ServiceModePolicy \
))

# Day-0: pure-logic tests only. No URE / VCL — those would set URE=true
# and pull in unobootstrap/vclbootstrap protectors that require a
# working services.rdb. See solenv/gbuild/CppunitTest.mk:114-129.
# use_sdk_api just adds API include paths (no URE), needed for IDL headers.
$(eval $(call gb_CppunitTest_use_libraries,kqoffice_provider, \
    cppu \
    cppuhelper \
    sal \
))

$(eval $(call gb_CppunitTest_use_sdk_api,kqoffice_provider))

# vim: set noet sw=4 ts=4:
