#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2001 - 2009  Tensilica Inc.
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies. Remember to do have actions
# for "archclean" and "archdep" for cleaning up and making dependencies for
# this architecture

# Core and platform configurations

#
# Remove Quotes from Variant and Platform Names;
# as well as Extra CFLAGS if Optimized for Debugging.
#
VARIANT_HASCODE	:= $(strip $(subst ",,$(CONFIG_XTENSA_VARIANT_HASCODE)))#")))
VARIANT		:= $(strip $(subst ",,$(CONFIG_XTENSA_VARIANT_NAME)))#")))
PLATFORM	:= $(strip $(subst ",,$(CONFIG_XTENSA_PLATFORM_NAME)))#")))
DEBUG_CFLAGS	:= $(strip $(subst ",,$(CONFIG_CC_DEBUGGING_CFLAGS)))#")))

UTS_MACHINE	:= xtensa_$(VARIANT)

# temporarily until string.h is fixed
KBUILD_CFLAGS += -ffreestanding

KBUILD_CFLAGS += -mlongcalls $(DEBUG_CFLAGS) -pipe
KBUILD_AFLAGS += -mlongcalls $(DEBUG_CFLAGS)

# KBUILD_DEFCONFIG := iss_defconfig

VARIANT_DIR := $(patsubst %,arch/xtensa/variants/%/,$(VARIANT))
PLATFORM_DIR := $(patsubst %,arch/xtensa/platforms/%/,$(PLATFORM))

ifeq ($(KBUILD_SRC),)
KBUILD_CPPFLAGS += $(patsubst %,-I%include,$(VARIANT_DIR) $(PLATFORM_DIR))
else
KBUILD_CPPFLAGS += $(patsubst %,-I$(srctree)/%include,$(VARIANT_DIR) $(PLATFORM_DIR))
endif


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
ifneq ($(VARIANT_HASCODE),)
core-y		+= arch/xtensa/variants/$(VARIANT)/
endif
ifneq ($(PLATFORM),)
core-y		+= arch/xtensa/platforms/$(PLATFORM)/
endif
libs-y		+= arch/xtensa/lib/ $(LIBGCC)

boot		:= arch/xtensa/boot

# archprepare: checkbin $(archinc)/.configured

archprepare: checkbin 

#
# Test for a Xtnesa Compiler and print Significant Env Variables
# if make started with V=1
#
checkbin:
ifneq ($(strip $(VARIANT)),)
	@if test "$(call cc-option-yn,-mlongcalls)" = "n" ; then \
		echo '*** Default compiler does not appear to target an Xtensa core.'; \
		echo '*** It didn't understand the '-mlongcalls' GCC option; \
		echo '*** Please put an appropriate Xtensa toolchain on your PATH'; \
		echo '';\
		echo 'CROSS_COMPILE:                    $(CROSS_COMPILE) '; \
		echo 'PLATFORM:                         $(PLATFORM) '; \
		echo 'VARIANT:                          $(VARIANT) '; \
		echo 'KBUILD_CFLAGS:                    $(KBUILD_CFLAGS) '; \
		echo 'VARIANT_DIR:                      $(VARIANT_DIR) '; \
		echo 'PLATFORM_DIR:                     $(PLATFORM_DIR) '; \
		echo ''; \
		false; \
	fi
	@if test "$(KBUILD_VERBOSE)" != 0 ; then  \
		echo 'CROSS_COMPILE:                    $(CROSS_COMPILE) '; \
		echo 'CONFIG_XTENSA_PLATFORM_NAME:      $(CONFIG_XTENSA_PLATFORM_NAME) '; \
		echo 'PLATFORM:                         $(PLATFORM) '; \
		echo 'CONFIG_XTENSA_VARIANT_NAME:       $(CONFIG_XTENSA_VARIANT_NAME) '; \
		echo 'VARIANT_HASCODE:                  $(VARIANT_HASCODE) '; \
		echo 'VARIANT:                          $(VARIANT) '; \
		echo 'CONFIG_CC_DEBUGGING_CFLAGS:       $(CONFIG_CC_DEBUGGING_CFLAGS) '; \
		echo 'DEBUG_CFLAGS:                     $(DEBUG_CFLAGS) '; \
		echo 'KBUILD_CFLAGS:                    $(KBUILD_CFLAGS) '; \
		echo 'VARIANT_DIR:                      $(VARIANT_DIR) '; \
		echo 'PLATFORM_DIR:                     $(PLATFORM_DIR) '; \
		echo ''; \
		true; \
	fi
else
	@echo '*** Xtensa core variant name cannot be empty (check config)' ; \
	false
endif

drivers-$(CONFIG_OPROFILE)	+= arch/xtensa/oprofile/

all: zImage

zImage zImage.initrd: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) $@

# CLEAN_FILES	+= arch/xtensa/vmlinux.lds                      \
#		   $(archinc)/platform $(archinc)/variant	\
# 		   $(archinc)/.configured

define archhelp
  @echo '* zImage      - Compressed kernel image'
  @echo '* uImage      - Compressed kernel image for U-Boot'
endef

