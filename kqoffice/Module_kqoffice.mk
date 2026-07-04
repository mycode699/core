# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the 可圈office project (V2 W1: Provider Runtime).
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_Module_Module,kqoffice))

# Day-1: Library_kqoffice_ai now ships Provider + OllamaAdapter as a real
# UNO component (factory + .component descriptor). Pure-logic cppunit
# continues to build the same TUs into the test binary — see
# CppunitTest_kqoffice_provider.mk — and stays decoupled from the Library.

$(eval $(call gb_Module_add_targets,kqoffice,\
    Library_kqoffice_ai \
))

$(eval $(call gb_Module_add_check_targets,kqoffice,\
    CppunitTest_kqoffice_provider \
    CppunitTest_kqoffice_cowork \
    CppunitTest_kqoffice_agent_delegation \
    CppunitTest_kqoffice_ai_i18n \
    CppunitTest_kqoffice_agent_chat \
    CppunitTest_kqoffice_agent_mesh \
    CppunitTest_kqoffice_control_plane \
    CppunitTest_kqoffice_ai_canvas \
    CppunitTest_kqoffice_ai_filemgr \
))

# vim: set noet sw=4 ts=4:
