# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the 可圈office project (V2 W3: Writer Apply Runtime).
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_CppunitTest_CppunitTest,sw_apply_engine))

$(eval $(call gb_CppunitTest_set_include,sw_apply_engine,\
    -I$(SRCDIR)/sw/inc \
    $$(INCLUDE) \
))

# W3 Day-1b: patch-kind / status token round-trip (pure logic).
$(eval $(call gb_CppunitTest_add_exception_objects,sw_apply_engine, \
    sw/qa/core/test_apply_engine \
))

$(eval $(call gb_CppunitTest_use_libraries,sw_apply_engine, \
    cppu \
    cppuhelper \
    sal \
    sw \
))

$(eval $(call gb_CppunitTest_use_sdk_api,sw_apply_engine))

# vim: set noet sw=4 ts=4: