#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2001 - 2008  Tensilica Inc.
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture

# Core and platform configurations

variant-y	:= $(strip $(subst ",,$(CONFIG_XTENSA_VARIANT_NAME)))#")))
platform-y	:= $(strip $(subst ",,$(CONFIG_XTENSA_PLATFORM_NAME)))#")))

UTS_MACHINE	:= xtensa_$(variant-y)

# temporarily until string.h is fixed
KBUILD_CFLAGS += -ffreestanding

KBUILD_CFLAGS += -pipe -mlongcalls

KBUILD_DEFCONFIG := iss_defconfig

# Test for cross compiling. Allow xtensa-*-* and xtensa_<variant>-*-*

ifneq ($(SUBARCH),$(ARCH))
  ifeq ($(CROSS_COMPILE),)
    CROSS_COMPILE := $(call cc-cross-prefix, \
    		       xtensa-linux-uclibc-  xtensa_$(variant-y)-linux-uclibc- \
    		       xtensa-linux-gnu-     xtensa_$(variant-y)-linux-gnu-)
  endif
endif

# We need to have a 'variant' configured, so exclude this line for 
# make config/clean/etc.

ifneq ($(variant-y),)
LIBGCC := $(shell $(CC) $(KBUILD_CFLAGS) -print-libgcc-file-name)
endif

head-y		:= arch/xtensa/kernel/head.o
core-y		+= arch/xtensa/kernel/ arch/xtensa/mm/
ifneq ($(platform-y),)
core-y		+= arch/xtensa/platforms/$(platform-y)/
endif
libs-y		+= arch/xtensa/lib/ $(LIBGCC)

boot		:= arch/xtensa/boot
archinc		:= include/asm-xtensa

archprepare: $(archinc)/.configured

# Update processor variant and platform symlinks if something which affects them changed.

$(archinc)/.configured: $(wildcard include/config/arch/*.h) include/config/auto.conf
	@echo '  SYMLINK $(archinc)/variant -> $(archinc)/variant-$(variant-y)'
	$(Q)mkdir -p $(archinc)
	$(Q)ln -fsn $(srctree)/$(archinc)/variant-$(variant-y) $(archinc)/variant
	@echo '  SYMLINK $(archinc)/platform -> $(archinc)/platform-$(platform-y)'
	$(Q)ln -fsn $(srctree)/$(archinc)/platform-$(platform-y) $(archinc)/platform
	@touch $@

all: zImage

Image zImage uImage: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

CLEAN_FILES	+= arch/xtensa/vmlinux.lds                      \
		   $(archinc)/platform $(archinc)/variant	\
		   $(archinc)/.configured

define archhelp
  @echo '* zImage      - Compressed kernel image'
  @echo '* uImage      - Compressed kernel image for U-Boot'
endef

