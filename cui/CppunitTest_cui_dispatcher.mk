# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# V2 W2 Day-1b — CommandPalette dispatcher harness (pure path, no URE).
#

$(eval $(call gb_CppunitTest_CppunitTest,cui_dispatcher))

$(eval $(call gb_CppunitTest_set_include,cui_dispatcher,\
    -I$(SRCDIR)/cui/source/inc \
    -I$(SRCDIR)/sfx2/inc \
    $$(INCLUDE) \
))

$(eval $(call gb_CppunitTest_add_defs,cui_dispatcher,\
    -DSRCDIR=\"$(SRCDIR)\" \
))

$(eval $(call gb_CppunitTest_add_exception_objects,cui_dispatcher, \
    cui/qa/unit/CommandPaletteDispatcherTest \
))

# Pure-logic: no use_ure / use_vcl / use_configuration — avoids broken
# services.rdb paths on non-ASCII BUILDDIR (B2). Links merged for exported
# sfx2::CommandPaletteDispatcher symbols (ENABLE_MERGELIBS).
$(eval $(call gb_CppunitTest_use_libraries,cui_dispatcher, \
    comphelper \
    cppu \
    cppuhelper \
    merged \
    sal \
    test \
))

# vim: set noet sw=4 ts=4: