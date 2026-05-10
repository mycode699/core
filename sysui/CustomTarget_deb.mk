# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

include $(SRCDIR)/sysui/productlist.mk

deb_WORKDIR := $(gb_CustomTarget_workdir)/sysui/deb
deb_SRCDIR := $(SRCDIR)/sysui/desktop/debian

$(eval $(call gb_CustomTarget_CustomTarget,sysui/deb))

$(eval $(call gb_CustomTarget_register_targets,sysui/deb,\
$(foreach product,$(PRODUCTLIST),\
	$(product)-desktop-integration.tar.gz \
	$(product)/DEBIAN/control \
	$(product)/DEBIAN/postinst \
	$(product)/DEBIAN/postrm \
	$(product)/DEBIAN/prerm \
	$(UNIXFILENAME.$(product))-debian-menus_$(PKGVERSION)-$(LIBO_VERSION_PATCH)_all.deb) \
))

define deb_register_target
$(deb_WORKDIR)/$(1)-desktop-integration.tar.gz: $(deb_WORKDIR)/$(UNIXFILENAME.$(1))-debian-menus_$(PKGVERSION)-$(LIBO_VERSION_PATCH)_all.deb
	fakeroot $(GNUTAR) -C $(deb_WORKDIR) -cf - $$(notdir $$<) | gzip > $$@

$(deb_WORKDIR)/$(UNIXFILENAME.$(1))-debian-menus_$(PKGVERSION)-$(LIBO_VERSION_PATCH)_all.deb: $(deb_WORKDIR)/$(1)/DEBIAN/postrm $(deb_WORKDIR)/$(1)/DEBIAN/postinst $(deb_WORKDIR)/$(1)/DEBIAN/prerm $(deb_WORKDIR)/$(1)/DEBIAN/control
	chmod -R g-w $(deb_WORKDIR)/$(1)
	chmod a+rx $(deb_WORKDIR)/$(1)/DEBIAN \
		$(deb_WORKDIR)/$(1)/DEBIAN/pre* $(deb_WORKDIR)/$(1)/DEBIAN/post*
	chmod g-s $(deb_WORKDIR)/$(1)/DEBIAN
	fakeroot dpkg-deb --build $(deb_WORKDIR)/$(1) $$@
endef

$(foreach product,$(PRODUCTLIST),\
$(eval $(call deb_register_target,$(product))))

$(deb_WORKDIR)/%/DEBIAN/postrm: $(deb_SRCDIR)/postrm
	cat $< | tr -d "\015" | \
		sed 's/%PREFIX/$(UNIXFILENAME.$*)/g' >> $@

$(deb_WORKDIR)/%/DEBIAN/postinst: $(deb_SRCDIR)/postinst
	cat $< | tr -d "\015" | \
		sed 's/%PREFIX/$(UNIXFILENAME.$*)/g' >> $@

$(deb_WORKDIR)/%/DEBIAN/prerm: $(deb_SRCDIR)/prerm
	cat $< | tr -d "\015" | \
		sed 's/%PREFIX/$(UNIXFILENAME.$*)/g' >> $@

$(deb_WORKDIR)/%/DEBIAN/control: $(deb_SRCDIR)/control $(gb_CustomTarget_workdir)/sysui/share/%/create_tree.sh
	mkdir -p $(deb_WORKDIR)/$*/usr/lib/menu
	cd $(gb_CustomTarget_workdir)/sysui/share/$* \
		&& DESTDIR=$(deb_WORKDIR)/$* \
		ICON_PREFIX=$(UNIXFILENAME.$*) \
		KDEMAINDIR=/usr \
		PREFIXDIR=/usr \
		./create_tree.sh
	sed $(deb_SRCDIR)/openoffice.org-debian-menus \
		-e 's/%PRODUCTNAME/$(PRODUCTNAME.$*) $(PRODUCTVERSION)/' \
		-e 's/%PREFIX/$(UNIXFILENAME.$*)/' \
		-e 's/%ICONPREFIX/$(UNIXFILENAME.$*)/' \
		> $(deb_WORKDIR)/$*/usr/lib/menu/$(UNIXFILENAME.$*)
	echo "Package: $(UNIXFILENAME.$*)-debian-menus" >$@
	cat $< | tr -d "\015" | \
		sed 's/%productname/$(PRODUCTNAME.$*) $(PRODUCTVERSION)/' \
		>> $@
	echo "Version: $(PKGVERSION)-$(LIBO_VERSION_PATCH)" >>$@
	du -k -s $(deb_WORKDIR)/$* | $(gb_AWK) -F ' ' '{ printf "Installed-Size: %s\n", $$1 ; }' >>$@

# vim: set noet sw=4 ts=4:
