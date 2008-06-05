#
# arch/xtensa/boot/Makefile
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
#

BIG_ENDIAN	:= $(shell echo -e __XTENSA_EB__ | $(CC) -E -P -)

ifeq ($(BIG_ENDIAN),1)
OBJCOPY_OFORMAT := -O elf32-xtensa-be
else
OBJCOPY_OFORMAT := -O elf32-xtensa-le
endif

zobjs-y		= inffast.o inflate.o inftrees.o
targets		:= bootstrap.o zmem.o $(zobjs-y)

XLIB		:= arch/xtensa/lib/lib.a

# Files from the zlib library for compressed images

quiet_cmd_copy_zlib = COPY	$@
      cmd_copy_zlib = cat $< > $@

$(addprefix $(obj)/,$(zobjs-y:.o=.c)): $(obj)/%: $(srctree)/lib/zlib_inflate/%
	$(call if_changed,copy_zlib)

ZOBJS = $(addprefix $(obj)/,$(zobjs-y))
EXTRA_CFLAGS = -Ilib/zlib_inflate -fpic

# Binary vmlinux images

OBJCOPYFLAGS_vmlinux.bin :=						\
	--strip-all -R .comment -R .note.gnu.build-id -O binary

$(obj)/vmlinux.bin: vmlinux FORCE
	$(call if_changed,objcopy)

$(obj)/vmlinuz.bin: $(obj)/vmlinux.bin FORCE
	$(call if_changed,gzip)

# Compressed and uncompressed packaged images

quiet_cmd_gen_image = GEN	$@
      cmd_gen_image =							\
	echo -e '.section .image,"ax";.incbin "$<"'			\
		| $(CC) -c -x assembler -o $(<:.bin=.o) -;		\
	$(LD) -pie -T $(srctree)/$(obj)/boot.lds -o $@.tmp $(^:.bin=.o);	\
	$(OBJCOPY) -O binary $@.tmp $@				
	# rm $(<:.bin=.o) $@.tmp

# Uncompressed Image

$(obj)/Image: $(obj)/vmlinux.bin $(obj)/bootstrap.o
	$(call if_changed,gen_image)

# Compressed Image (for RedBoot, for example)

$(obj)/zImage: $(obj)/vmlinuz.bin $(obj)/bootstrap.o $(obj)/zmem.o $(ZOBJS) $(XLIB)
	$(call if_changed,gen_image)

# U-Boot Image

MKIMAGE	:= $(srctree)/scripts/mkuboot.sh

KERNEL_LOAD = 0xd0001000
KERNEL_ENTRY = 0xd0001000

quiet_cmd_uimage = UIMAGE	$@
      cmd_uimage = $(CONFIG_SHELL) $(MKIMAGE) -A xtensa -O linux -T kernel \
      		   -C gzip -a $(KERNEL_LOAD) -e $(KERNEL_ENTRY)		  \
		   -n 'Linux-$(KERNELRELEASE)' -d $< $@

$(obj)/uImage:	$(obj)/vmlinuz.bin FORCE
	$(call if_changed,uimage)
	@echo '   Image $@ is ready.'

# 'ELF' image including a compressed bootable zImage

RESET_VEC_CMD := '\#include <asm/variant/core.h>\nXCHAL_RESET_VECTOR_VADDR'
RESET_VEC := $(shell echo -e $(RESET_VEC_CMD)|$(CC) -Iinclude -E -|grep -v "\#")

LDFLAGS_zImage.elf = -T boot.lds -T text $(RESET_VEC)
$(obj)/zImage.elf: $(obj)/zImage $(obj)/reset.o
	$(call if_changed,ld)

