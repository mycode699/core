# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

gb_Rdb__get_install_target = $(INSTROOT)/$(LIBO_ETC_FOLDER)/services/$(1).rdb

define gb_Rdb__command
$(if $(COMPONENTS),,$(call gb_Output_error,Rdb without components: partial build of module postprocess is not possible currently - only top-level make works))
$(call gb_Helper_abbreviate_dirs,\
		RESPONSEFILE=$$(mktemp /tmp/gbuild-rdb-response.XXXXXX) && \
		COMPONENTWORKDIR=$$(mktemp -d /tmp/gbuild-rdb.XXXXXX) && \
		trap 'rm -rf "$$COMPONENTWORKDIR" "$$RESPONSEFILE"' 0 && \
		mkdir -p $(dir $@) && \
		printf '%s\n' '<list>' > "$$RESPONSEFILE" && \
		i=0; \
		for component in $(foreach component,$(COMPONENTS),"$(call gb_ComponentTarget_get_target,$(component))"); do \
			i=$$((i+1)); \
			dest="$$COMPONENTWORKDIR/$$i.component"; \
			cp "$$component" "$$dest" && printf '%s\n' "<filename>$$dest</filename>" >> "$$RESPONSEFILE" || exit 1; \
		done && \
		printf '%s\n' '</list>' >> "$$RESPONSEFILE" && \
		$(call gb_ExternalExecutable_get_command,xsltproc) --nonet -o "$(1)" \
			"$(SRCDIR)/solenv/bin/packcomponents.xslt" "$$RESPONSEFILE")
endef

$(call gb_Rdb_get_target,%) :| $(call gb_ExternalExecutable_get_dependencies,xsltproc)
		$(call gb_Output_announce,$*,$(true),RDB,1)
		$(call gb_Trace_StartRange,$*,RDB)
		$(call gb_Rdb__command,$@,$*,$?,$^)
		$(call gb_Trace_EndRange,$*,RDB)

.PHONY : $(call gb_Rdb_get_clean_target,%)
$(call gb_Rdb_get_clean_target,%) :
		$(call gb_Output_announce,$*,$(false),RDB,1)
		$(call gb_Helper_abbreviate_dirs,\
			rm -f $(call gb_Rdb__get_install_target,$*) $(call gb_Rdb_get_target,$*))

define gb_Rdb__Rdb_impl
# gb_Rdb_add_component, which adds to the target-specific COMPONENTS variable, can be called (from
# gb_ComponentTarget_ComponentTarget) before gb_Rdb__Rdb_impl is called, so using `COMPONENTS :=`
# here could lose content; but still use `COMPONENTS ?=` here to establish COMPONENTS as target-
# specific even in the corner case of an empty Rdb with no gb_Rdb_add_component calls, so that the
# use of $(COMPONENTS) in gb_Rdb__command would not accidentally pick a global COMPONENTS variable:
$(call gb_Rdb_get_target,$(1)) : COMPONENTS ?=
$(call gb_Rdb_get_target,$(1)) : $(gb_Module_CURRENTMAKEFILE)
$$(eval $$(call gb_Module_register_target,$(2),$(call gb_Rdb_get_clean_target,$(1))))
$(call gb_Helper_make_userfriendly_targets,$(1),Rdb,$(2))

endef

define gb_Rdb_Rdb
$(call gb_Rdb__Rdb_impl,$(1),$(call gb_Rdb_get_target,$(1)))

endef

# FIXME this needs some layer-like thing for the special case in URE
define gb_Rdb_Rdb_install
$(call gb_Rdb__Rdb_impl,$(1),$(if $(2),$(INSTROOT)/$(2),$(call gb_Rdb__get_install_target,$(1))))
$(call gb_Helper_install_final, \
		$(if $(2),$(INSTROOT)/$(2),$(call gb_Rdb__get_install_target,$(1))), \
		$(call gb_Rdb_get_target,$(1)))

endef

define gb_Rdb_add_component
$(call gb_Rdb_get_target,$(1)) : $(call gb_ComponentTarget_get_target,$(2))
$(call gb_Rdb_get_target,$(1)) : COMPONENTS += $(2)

endef

define gb_Rdb_add_components
$(foreach component,$(2),$(call gb_Rdb_add_component,$(1),$(component)))

endef

# vim: set noet sw=4 ts=4:
