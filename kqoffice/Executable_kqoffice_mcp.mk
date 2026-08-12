# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# kqoffice-mcp: local MCP JSON-RPC stdio host
#

$(eval $(call gb_Executable_Executable,kqoffice_mcp))

$(eval $(call gb_Executable_set_include,kqoffice_mcp,\
    -I$(SRCDIR)/kqoffice/source/ai/chat \
    $$(INCLUDE) \
))

$(eval $(call gb_Executable_use_libraries,kqoffice_mcp,\
    kqoffice_ai \
    comphelper \
    cppu \
    cppuhelper \
    sal \
    salhelper \
))

$(eval $(call gb_Executable_add_exception_objects,kqoffice_mcp,\
    kqoffice/source/ai/mcp/kqoffice_mcp_main \
))

# vim: set noet sw=4 ts=4:
