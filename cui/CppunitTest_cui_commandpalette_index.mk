# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the 可圈office project (V2 W2: Cmd+K Command Palette).
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_CppunitTest_CppunitTest,cui_commandpalette_index))

$(eval $(call gb_CppunitTest_set_include,cui_commandpalette_index,\
    -I$(SRCDIR)/cui/source/inc \
    $$(INCLUDE) \
))

$(eval $(call gb_CppunitTest_add_exception_objects,cui_commandpalette_index, \
    cui/qa/unit/CommandIndexTest \
))

# Pure-logic parser test — header-only parseCommandsXcu means no libcui
# linkage required, mirroring the FuzzyMatcher test layout.
$(eval $(call gb_CppunitTest_use_libraries,cui_commandpalette_index, \
    cppu \
    cppuhelper \
    sal \
    test \
    unotest \
))

# vim: set noet sw=4 ts=4:
