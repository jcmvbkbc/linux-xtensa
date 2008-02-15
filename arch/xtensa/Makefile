#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2001 - 2007  Tensilica Inc.
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture

# Core configuration.

VARIANT := $(strip $(subst ", ,$(CONFIG_XTENSA_VARIANT_NAME)))

ifeq ($(strip $(VARIANT)),)
ifeq ($(CONFIG_XTENSA_VARIANT_CUSTOM),y)
$(error Xtensa processor variant name cannot be empty.)
endif
#  We are being invoked without config info (e.g. during 'make menuconfig').
#  However we need config info to know which Xtensa toolchain to invoke.
#  Is a working CC important at this point?
#  (The top-level Makefile may invoke CC via cc-option -- does its
#   derivation persist?...)
#  We don't yet know the VARIANT name (e.g. when invoking 'make menuconfig').
#  In this case, just select any toolchain found on the PATH:
somegcc := $(firstword $(wildcard $(addsuffix /xtensa_*-linux*-gcc,$(subst :, ,$(PATH)))))
ifeq ($(somegcc),)
$(error No Xtensa toolchain found on PATH.)
else
VARIANT := $(subst xtensa_,,$(word 1,$(subst -, ,$(notdir $(somegcc)))))
# $(warning Picked variant .$(VARIANT).)
endif
endif

fullarch := xtensa_$(VARIANT)
UTS_MACHINE := $(fullarch)
# $(warning Selected arch $(fullarch).)

# Platform configuration

platform-$(CONFIG_XTENSA_PLATFORM_XT2000)	:= xt2000
platform-$(CONFIG_XTENSA_PLATFORM_ISS)		:= iss
platform-$(CONFIG_XTENSA_PLATFORM_LX60)		:= lx60

PLATFORM = $(platform-y)
export PLATFORM

# temporarily until string.h is fixed
KBUILD_CFLAGS += -ffreestanding

KBUILD_CFLAGS += -pipe -mlongcalls

KBUILD_DEFCONFIG := iss_defconfig

# ramdisk/initrd support
# You need a compressed ramdisk image, named ramdisk.gz in
# arch/xtensa/boot/ramdisk

core-$(CONFIG_EMBEDDED_RAMDISK)	+= arch/xtensa/boot/ramdisk/

# Test for cross compiling

ifneq ($(SUBARCH),$(ARCH))
  ifeq ($(CROSS_COMPILE),)
    CROSS_COMPILE := $(call cc-cross-prefix, $(fullarch)-linux-uclibc-  $(fullarch)-linux-gnu-)
  endif
endif

#
# Extra flags that neededed for sparse (i.e. when compiling with C=1)
ifeq ($CONFIG_XTENSA_CPU_LINUX_LE,y)
CHECKFLAGS      += -D__XTENSA_EL__=1
else
CHECKFLAGS      += -D__XTENSA_EB__=1
endif

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

archprepare: $(archinc)/.platform

# Update processor variant and platform symlinks if something which affects
# them changed.

$(archinc)/.platform: $(wildcard include/config/arch/*.h) include/config/auto.conf
	@echo '  SYMLINK $(archinc)/variant -> $(archinc)/variant-$(VARIANT)'
	$(Q)mkdir -p $(archinc)
	$(Q)ln -fsn $(srctree)/$(archinc)/variant-$(VARIANT) $(archinc)/variant
	@echo '  SYMLINK $(archinc)/platform -> $(archinc)/platform-$(PLATFORM)'
	$(Q)ln -fsn $(srctree)/$(archinc)/platform-$(PLATFORM) $(archinc)/platform
	@touch $@


all: zImage

bzImage : zImage

zImage zImage.initrd: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $@

CLEAN_FILES	+= arch/xtensa/vmlinux.lds                      \
		   $(archinc)/platform $(archinc)/variant	\
		   $(archinc)/.platform

define archhelp
  @echo '* zImage      - Compressed kernel image (arch/xtensa/boot/images/zImage.*)'
endef

