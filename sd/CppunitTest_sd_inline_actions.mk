# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the 可圈office project (V2 W4: Select-to-Act).
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_CppunitTest_CppunitTest,sd_inline_actions))

$(eval $(call gb_CppunitTest_set_include,sd_inline_actions,\
    -I$(SRCDIR)/sd/source/ui/inline-actions \
    -I$(SRCDIR)/sd/inc \
    $$(INCLUDE) \
))

# W4 Day-0: pure enum-stability round-trip + unknown-token fallback.
# Links against Library_sd to reuse SlideElementActions translation unit
# (avoid duplicate-object pattern per fdo#47246).
$(eval $(call gb_CppunitTest_add_exception_objects,sd_inline_actions, \
    sd/qa/cppunit/test_inline_action_provider_map \
    sd/qa/cppunit/test_inline_action_slide_apply_map \
    sd/qa/cppunit/testSlideElementActionEnumStable \
))

# W4 Day-0: pure-logic tests only. No URE / VCL — those would pull in
# unobootstrap/vclbootstrap protectors. use_sdk_api just adds API
# include paths (no URE), needed for any IDL headers used downstream.
$(eval $(call gb_CppunitTest_use_libraries,sd_inline_actions, \
    cppu \
    cppuhelper \
    sal \
    sd \
))

$(eval $(call gb_CppunitTest_use_sdk_api,sd_inline_actions))

# vim: set noet sw=4 ts=4:
