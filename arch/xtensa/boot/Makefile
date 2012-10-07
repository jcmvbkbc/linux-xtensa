#
# arch/xtensa/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
#


# KBUILD_CFLAGS used when building rest of boot (takes effect recursively)
KBUILD_CFLAGS	+= -fno-builtin -Iarch/$(ARCH)/boot/include
HOSTFLAGS	+= -Iarch/$(ARCH)/boot/include

BIG_ENDIAN	:= $(shell echo -e __XTENSA_EB__ | $(CC) -E - | grep -v "\#")

export ccflags-y
export BIG_ENDIAN

subdir-y	:= lib

# Subdirs for the boot loader(s)

bootdir-$(CONFIG_XTENSA_PLATFORM_ISS)	 += boot-elf
bootdir-$(CONFIG_XTENSA_PLATFORM_XT2000) += boot-redboot boot-elf boot-uboot
bootdir-$(CONFIG_XTENSA_PLATFORM_LX60)   += boot-redboot boot-elf boot-uboot

BUILTIN_DTB := $(patsubst "%",%,dc232b_lx60).dtb.o
obj-y += $(BUILTIN_DTB)

# Rule to build device tree blobs
$(obj)/%.dtb: $(src)/dts/%.dts FORCE
	$(call if_changed_dep,dtc)

clean-files := *.dtb.S

zImage Image: $(bootdir-y)

$(bootdir-y): $(addprefix $(obj)/,$(subdir-y)) \
	      $(addprefix $(obj)/,$(host-progs))
	$(Q)$(MAKE) $(build)=$(obj)/$@ $(MAKECMDGOALS)

