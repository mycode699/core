# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

PRODUCTLIST := libreoffice libreofficedev
PKGVERSION := $(LIBO_VERSION_MAJOR).$(LIBO_VERSION_MINOR).$(LIBO_VERSION_MICRO)
PKGVERSIONSHORT := $(LIBO_VERSION_MAJOR).$(LIBO_VERSION_MINOR)
# User-facing brand: 可圈办公 / CoC Office (cocoffice). Internal product key remains libreoffice.
PRODUCTNAME.libreoffice := 可圈办公
PRODUCTNAME.libreofficedev := CoCOfficeDev
UNIXFILENAME.libreoffice := cocoffice$(PKGVERSIONSHORT)
UNIXFILENAME.libreofficedev := cocofficedev$(PKGVERSIONSHORT)

# vim: set noet sw=4 ts=4:
