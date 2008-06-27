#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#

GZIP = gzip
GZIP_FLAGS = -v9fc
OBJCOPYFLAGS = --strip-all -R .comment -R .note.gnu.build-id -O binary
MKIMAGE = $(srctree)/scripts/mkuboot.sh

KERNEL_LOAD  = 0xd0001000
KERNEL_ENTRY = 0xd0001000

vmlinux.bin: vmlinux
	$(OBJCOPY) $(OBJCOPYFLAGS) $^ $@

vmlinux.bin.gz: vmlinux.bin
	$(GZIP) $(GZIP_FLAGS) $^ > $@

uImage: vmlinux.bin.gz
	$(MKIMAGE) -A xtensa -O linux -T kernel -C gzip \
		-a $(KERNEL_LOAD) -e $(KERNEL_ENTRY) \
		-n 'Linux-$(KERNELRELEASE)' -d $^ \
		arch/$(ARCH)/boot/$@
	rm -f vmlinux.bin vmlinux.bin.gz

zImage: uImage

