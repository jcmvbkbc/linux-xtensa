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

VARIANT		:= $(strip $(subst ",,$(CONFIG_XTENSA_VARIANT_NAME)))#")))
PLATFORM	:= $(strip $(subst ",,$(CONFIG_XTENSA_PLATFORM_NAME)))#")))

UTS_MACHINE	:= xtensa_$(VARIANT)

# temporarily until string.h is fixed
KBUILD_CFLAGS += -ffreestanding

KBUILD_CFLAGS += -pipe -mlongcalls
KBUILD_DEFCONFIG := iss_defconfig

# Test for cross compiling. Allow xtensa-*-* and xtensa_<variant>-*-*

ifneq ($(SUBARCH),$(ARCH))
  ifeq ($(CROSS_COMPILE),)
    CROSS_COMPILE := $(call cc-cross-prefix, \
    		       xtensa-linux-uclibc-  xtensa_$(VARIANT)-linux-uclibc- \
    		       xtensa-linux-gnu-     xtensa_$(VARIANT)-linux-gnu-)
  endif
endif

# We need to have a 'variant' configured, so exclude this line for 
# make config/clean/etc.

ifneq ($(strip $(VARIANT)),)
LIBGCC := $(shell $(CC) $(KBUILD_CFLAGS) -print-libgcc-file-name)
endif

head-y		:= arch/xtensa/kernel/head.o
core-y		+= arch/xtensa/kernel/ arch/xtensa/mm/
ifneq ($(PLATFORM),)
core-y		+= arch/xtensa/platforms/$(PLATFORM)/
endif
libs-y		+= arch/xtensa/lib/ $(LIBGCC)

boot		:= arch/xtensa/boot
archinc		:= include/asm-xtensa

archprepare: checkbin $(archinc)/.configured

checkbin:
ifneq ($(strip $(VARIANT)),)
	@if test "$(call cc-option-yn,-mlongcalls)" = "n" ; then \
		echo '*** Default compiler does not target an Xtensa core'; \
		echo -n '*** Please put an appropriate Xtensa toolchain '; \
		echo '($(VARIANT) variant) on your PATH'; \
		false; \
	fi
else
	@echo '*** Xtensa core variant name cannot be empty (check config)' ; \
	false
endif

drivers-$(CONFIG_OPROFILE)	+= arch/xtensa/oprofile/

# Update processor variant and platform symlinks if something which affects them changed.

$(archinc)/.configured: $(wildcard include/config/arch/*.h) include/config/auto.conf
	@echo '  SYMLINK $(archinc)/variant -> $(archinc)/variant-$(VARIANT)'
	$(Q)mkdir -p $(archinc)
	$(Q)ln -fsn $(srctree)/$(archinc)/variant-$(VARIANT) $(archinc)/variant
	@echo '  SYMLINK $(archinc)/platform -> $(archinc)/platform-$(PLATFORM)'
	$(Q)ln -fsn $(srctree)/$(archinc)/platform-$(PLATFORM) $(archinc)/platform
	@touch $@

all: zImage

zImage zImage.initrd: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $@

CLEAN_FILES	+= arch/xtensa/vmlinux.lds                      \
		   $(archinc)/platform $(archinc)/variant	\
		   $(archinc)/.configured

define archhelp
  @echo '* zImage      - Compressed kernel image'
  @echo '* uImage      - Compressed kernel image for U-Boot'
endef

