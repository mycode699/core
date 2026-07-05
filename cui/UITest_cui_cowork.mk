# This file is part of the 可圈office project (V2 W5 Cowork UITest).

$(eval $(call gb_UITest_UITest,cui_cowork))

$(eval $(call gb_UITest_add_modules,cui_cowork,$(SRCDIR)/cui/qa/uitest,\
	cowork/ \
))

$(eval $(call gb_UITest_avoid_oneprocess,cui_cowork))

# vim: set noet sw=4 ts=4: