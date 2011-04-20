#
# Makefile for the linux ext4-filesystem routines.
#

ifdef CONFIG_USE_XTENSA_XCC_COMPILER
#
# super.c:
#	Signal: Floating point exception in Loop Nest Optimizer phase.
#	Error: Signal Floating point exception in phase Loop Nest Optimizer -- processing aborted
#
EXTRA_CFLAGS += -O2
endif

obj-$(CONFIG_EXT4_FS) += ext4.o

ext4-y	:= balloc.o bitmap.o dir.o file.o fsync.o ialloc.o inode.o \
		   ioctl.o namei.o super.o symlink.o hash.o resize.o extents.o \
		   ext4_jbd2.o migrate.o mballoc.o

ext4-$(CONFIG_EXT4_FS_XATTR)		+= xattr.o xattr_user.o xattr_trusted.o
ext4-$(CONFIG_EXT4_FS_POSIX_ACL)	+= acl.o
ext4-$(CONFIG_EXT4_FS_SECURITY)		+= xattr_security.o
