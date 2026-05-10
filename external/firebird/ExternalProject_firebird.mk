# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_ExternalProject_ExternalProject,firebird))

$(eval $(call gb_ExternalProject_use_autoconf,firebird,build))

$(eval $(call gb_ExternalProject_use_externals,firebird,\
	boost_headers \
	icu \
	libatomic_ops \
	libtommath \
))

$(eval $(call gb_ExternalProject_register_targets,firebird,\
	build \
))

firebird_ASCII_ROOT := /private/tmp/kdoffice-firebird-ascii
firebird_ASCII_WORKDIR := $(firebird_ASCII_ROOT)/firebird
firebird_ASCII_UNPACKED_WORKDIR := $(firebird_ASCII_ROOT)
firebird_BUILDDIR = $(EXTERNAL_WORKDIR)/gen/$(if $(ENABLE_DEBUG),Debug,Release)/firebird
firebird_VERSION := 3.0.13

$(call gb_ExternalProject_get_target,firebird) : EXTERNAL_WORKDIR := $(firebird_ASCII_WORKDIR)
$(call gb_ExternalProject_get_state_target,firebird,build) : EXTERNAL_WORKDIR := $(firebird_ASCII_WORKDIR)

$(call gb_ExternalProject_get_state_target,firebird,build):
	$(call gb_Trace_StartRange,firebird,EXTERNAL)
	$(call gb_Helper_abbreviate_dirs,rm -rf $(firebird_ASCII_ROOT) && mkdir -p $(firebird_ASCII_ROOT) && cp -R $(gb_UnpackedTarball_workdir)/firebird $(firebird_ASCII_ROOT) && ln -s $(gb_UnpackedTarball_workdir)/icu $(firebird_ASCII_ROOT)/icu && ln -s $(gb_UnpackedTarball_workdir)/libatomic_ops $(firebird_ASCII_ROOT)/libatomic_ops && ln -s $(gb_UnpackedTarball_workdir)/libtommath $(firebird_ASCII_ROOT)/libtommath)
	$(call gb_ExternalProject_run,build,\
		export PKG_CONFIG="" \
		&& export CPPFLAGS=" \
			$(BOOST_CPPFLAGS) \
			$(if $(SYSTEM_LIBATOMIC_OPS),$(LIBATOMIC_OPS_CFLAGS), \
				-I$(firebird_ASCII_UNPACKED_WORKDIR)/libatomic_ops/src \
			) \
			$(if $(SYSTEM_LIBTOMMATH),$(LIBTOMMATH_CFLAGS), \
				-I$(firebird_ASCII_UNPACKED_WORKDIR)/libtommath \
			) \
			$(if $(SYSTEM_ICU),$(ICU_CPPFLAGS), \
				-I$(firebird_ASCII_UNPACKED_WORKDIR)/icu/source \
				-I$(firebird_ASCII_UNPACKED_WORKDIR)/icu/source/i18n \
				-I$(firebird_ASCII_UNPACKED_WORKDIR)/icu/source/common \
			) \
			$(if $(filter GCC-INTEL,$(COM)-$(CPUNAME)),-Di386=1) \
			" \
		&& export CFLAGS=" \
			$(if $(filter MSC,$(COM)),$(if $(MSVC_USE_DEBUG_RUNTIME),-DMSVC_USE_DEBUG_RUNTIME)) \
			$(if $(filter MSC-TRUE-X86_64,$(COM)-$(COM_IS_CLANG)-$(CPUNAME)),-march=x86-64-v2) \
			$(if $(HAVE_GCC_FNO_SIZED_DEALLOCATION),-fno-sized-deallocation -fno-delete-null-pointer-checks) \
			$(call gb_ExternalProject_get_build_flags,firebird) \
			$(if $(ENABLE_DEBUG),$(if $(filter MSC,$(COM)),-Od -Z7)) \
		" \
		&& export CXXFLAGS=" \
			$(BOOST_CXXFLAGS) \
			$(if $(filter MSC,$(COM)),$(if $(MSVC_USE_DEBUG_RUNTIME),-DMSVC_USE_DEBUG_RUNTIME)) \
			$(if $(filter MSC-TRUE-X86_64,$(COM)-$(COM_IS_CLANG)-$(CPUNAME)),-march=x86-64-v2) \
			$(if $(HAVE_GCC_FNO_SIZED_DEALLOCATION),-fno-sized-deallocation -fno-delete-null-pointer-checks) \
			$(CXXFLAGS_CXX11) \
			$(if $(filter TRUE,$(COM_IS_CLANG)), -Wno-c++11-narrowing) \
			$(call gb_ExternalProject_get_build_flags,firebird) \
			$(if $(ENABLE_DEBUG),$(if $(filter MSC,$(COM)),-Od -Z7)) \
		" \
		&& export LDFLAGS=" \
			$(call gb_ExternalProject_get_link_flags,firebird) \
			$(if $(SYSTEM_LIBATOMIC_OPS),$(LIBATOMIC_OPS_LIBS), \
				-L$(firebird_ASCII_UNPACKED_WORKDIR)/libatomic_ops/src \
			) \
			$(if $(SYSTEM_LIBTOMMATH),$(LIBTOMMATH_LIBS), \
				-L$(firebird_ASCII_UNPACKED_WORKDIR)/libtommath \
			) \
			$(if $(SYSTEM_ICU),$(ICU_LIBS), \
				-L$(firebird_ASCII_UNPACKED_WORKDIR)/icu/source/lib \
			) \
		" \
		&& export LIBREOFFICE_ICU_LIB="$(firebird_ASCII_UNPACKED_WORKDIR)/icu/source/lib" \
		&& export MSVC_USE_INDIVIDUAL_PDBS=TRUE \
		&& MAKE=$(MAKE) $(gb_RUN_CONFIGURE) ./configure \
			--without-editline \
			--with-wire-compress=no \
			$(gb_CONFIGURE_PLATFORMS) \
			$(if $(DISABLE_DYNLOADING), \
				--enable-static --disable-shared \
			, \
				--enable-shared --disable-static \
			) \
			$(if $(HAVE_LIBCPP),CXX='$(CXX) -D_LIBCPP_ENABLE_CXX17_REMOVED_AUTO_PTR') \
		&& LC_ALL=C $(MAKE) \
			ROOT='$(firebird_ASCII_WORKDIR)' \
			$(if $(ENABLE_DEBUG),Debug) SHELL='$(SHELL)' $(if $(filter LINUX,$(OS)),CXXFLAGS="$$CXXFLAGS") \
			MATHLIB="$(if $(SYSTEM_LIBTOMMATH),$(LIBTOMMATH_LIBS),-L$(firebird_ASCII_UNPACKED_WORKDIR)/libtommath -ltommath)" \
			LIBO_TUNNEL_LIBRARY_PATH='$(subst ','\'',$(subst $$,$$$$,$(call gb_Helper_extend_ld_path,$(firebird_ASCII_UNPACKED_WORKDIR)/icu/source/lib:$(firebird_BUILDDIR)/lib)))' \
		$(if $(filter MACOSX,$(OS)), \
			&& install_name_tool -id @__________________________________________________OOO/libfbclient.dylib.$(firebird_VERSION) \
				-delete_rpath @loader_path/.. \
				$(firebird_BUILDDIR)/lib/libfbclient.dylib.$(firebird_VERSION) \
			&& install_name_tool -id @__________________________________________________OOO/libEngine12.dylib \
				-delete_rpath @loader_path/.. \
				$(firebird_BUILDDIR)/plugins/libEngine12.dylib \
			&& install_name_tool -change @rpath/lib/libfbclient.dylib \
				@loader_path/libfbclient.dylib.$(firebird_VERSION) $(firebird_BUILDDIR)/plugins/libEngine12.dylib \
			&& $(PERL) $(SRCDIR)/solenv/bin/macosx-change-install-names.pl shl OOO \
				$(firebird_BUILDDIR)/lib/libfbclient.dylib.$(firebird_VERSION) \
				$(firebird_BUILDDIR)/plugins/libEngine12.dylib \
			) \
		&& rm -rf $(gb_UnpackedTarball_workdir)/firebird/gen \
		&& cp -R $(firebird_ASCII_WORKDIR)/gen $(gb_UnpackedTarball_workdir)/firebird/ \
	)
	$(call gb_Trace_EndRange,firebird,EXTERNAL)

# vim: set noet sw=4 ts=4:
