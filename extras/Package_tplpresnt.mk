# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project / 可圈办公 product packaging.
#
# Presentation templates for Chinese office parity (WPS/Office CN):
# prefer CN business packs + calm professional themes. Decorative Lorem-heavy
# packs (Candy/DNA/Beehive/...) are omitted so the template browser does not
# look like a consumer collage.

$(eval $(call gb_Package_Package,extras_tplpresnt,$(gb_CustomTarget_workdir)/extras/source/templates/presnt))

ifneq ($(WITH_TEMPLATES),)

$(eval $(call gb_Package_add_files,extras_tplpresnt,$(LIBO_SHARE_FOLDER)/template/common/presnt,\
Business_Pitch_CN.otp \
Project_Report_CN.otp \
Teaching_Courseware_CN.otp \
Metropolis.otp \
Grey_Elegant.otp \
Focus.otp \
Progress.otp \
Midnightblue.otp \
Blueprint_Plans.otp \
Portfolio.otp \
Vintage.otp \
))

else

$(eval $(call gb_Package_add_empty_directory,extras_tplpresnt,$(LIBO_SHARE_FOLDER)/template/common/presnt))

endif

# vim: set noet sw=4 ts=4:
