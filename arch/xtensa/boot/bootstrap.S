#include <asm/variant/core.h>
#include <asm/regs.h>
#include <asm/asmmacro.h>
#include <asm/cacheasm.h>

#define KERNEL_IMAGE_LOAD_ADDR	0xd0001000
#define KERNEL_IMAGE_SIZE	0x10000000

/*
 *
 * Assumptions:
 *  ROM images
 *   - The bootstrap software runs at the linked address.
 *   - ..
 *
 *  RAM
 *
 *   - The bootstrap software can run at any RAM address.
 *   - There is enough space for the uncompressed kernel image
 *     at the kernel start address. It doesn't overlap with either the
 *     bootloader (including the image) or the boot parameters.

 * The following conditions:
 *
 *  - Must be called with callX with PS.WOE=1 and PS.EXCM=0. 
 *  - PS.INTLEVEL should be set to 15 to disable interrupts.
 *  - Although not used, a1 should be a valid stack pointer.
 *  - a2 points to the boot-paramter list.
 *
 */

/* The Linux-Kernel image including the loader must be loaded
 * to a position so that the kernel and the boot parameters
 * can fit in the space before the load address.
 */

/* Make sure we have enough space for the 'uncompressor' */

#define STACK_SIZE 32768
#define HEAP_SIZE (131072*4)
#define R_XTENSA_RELATIVE	5

/* This must be at the start of the image, so we give it its own section. */

	# a2: Parameter list
	# a3: Size of parameter list

	.section .start, "ax"

	.globl __start

__start:
	entry	sp, 32		# we do not intend to return
	_call0	_start
__start_a0:

	.section .text, "ax"
	.begin literal_prefix .text

	.globl _start
	.align 4

_start:	/* Get the 'loadaddr'. Keep it in a0... */

	movi	a4, __start_a0
	sub	a0, a0, a4

	/* 'reset' WINDOWSTART */

	movi	a4, 1
	rsr	a5, WINDOWBASE
	ssl	a5
	sll	a4, a4
	wsr	a4, WINDOWSTART
	rsync

	/* Apply relocations if not running in ROM */

	movi	a4, __rela_dyn_start
	movi	a5, __rela_dyn_end

	beq	a4, a5, 3f

	add	a4, a4, a0	# + loadaddr
	add	a5, a5, a0	# + loadaddr

	/* We currently only know about R_XTENSA_RELATIVE, skip others. */

1:	l32i	a6, a4, 4	# r_info
	extui	a6, a6, 0, 8
	bnei	a6, R_XTENSA_RELATIVE, 2f

	l32i	a6, a4, 0	# r_offset
	add	a6, a6, a0	# + loadaddr
	l32i	a7, a6, 0
	add	a7, a7, a0
	s32i	a7, a6, 0

2:	addi	a4, a4, 12
	bne	a4, a5, 1b

3:	/* Clear the BSS section. */

	movi	a4, __bss_start
	movi	a5, __bss_end
	#add	a4, a4, a0
	#add	a5, a5, a0
	movi.n	a6, 0

1:	s32i	a6, a4, 0
	addi	a4, a4, 4
	blt	a4, a5, 1b

	/* Synchronize the caches. */

#if XCHAL_DCACHE_IS_WRITEBACK
	___flush_dcache_all a5 a6
#endif
	___invalidate_icache_all a5 a6

	/* Set the stack pointer. */

	//  FIXME: movi	a1, 

	/* Uncompress the kernel */

	/*
	 * a0: load address
	 * a1: stack
	 * a2: boot parameter
	 */

	movi	a3, __image_start
	movi	a4, __image_end
#	add	a8, a0, a3		# load address for the image
	mov	a8, a3
	sub	a11, a4, a3		# size of the image

	movi	a6, KERNEL_IMAGE_LOAD_ADDR
	movi	a7, KERNEL_IMAGE_SIZE

	movi	a9, complen
#	add	a9, a9, a0		# complen + loadaddr
	s32i	a11, a9, 0

	movi	a0, 0

	/*
	 *  a6 (a2)	destination
	 *  a7 (a3)	maximum size of destination
	 *  a8 (a4)	source address
	 *  a9 (a5)	ptr to length
	 * a10 (a6)
	 * a11 (a7)	size of image
	 */

	.extern gunzip
	movi	a4, gunzip
	callx4	a4
	j	3f


3:	/* Synchronize caches. */

#if XCHAL_DCACHE_IS_WRITEBACK
	___flush_dcache_all a4 a5
#endif
	___invalidate_icache_all a4 a5
	isync

	/*
	 * a1: stack
	 * a2: boot parameter
	 */

	movi	a4, KERNEL_IMAGE_LOAD_ADDR
	jx	a4

	.globl gunzip
	.weak gunzip
	.align 4
gunzip:

	entry	a1, 16

	/* don't uncompress image, simply copy it */

2:	l32i	a5, a4, 0
	l32i	a6, a4, 4
	s32i	a5, a2, 0
	s32i	a6, a2, 4
	addi	a4, a4, 8
	addi	a2, a2, 8
	addi	a7, a7, -8
	bgez	a7, 2b

	retw

	.align 16
	.data
	.globl avail_ram
avail_ram:
	.long	_heap
	.globl end_avail
end_avail:
	.long	_heap + HEAP_SIZE

	.comm _stack, STACK_SIZE
	.comm _heap, HEAP_SIZE

	.globl end_avail
	.comm complen, 4

	.end	literal_prefix




/* ===================================================================== */


#if 0


#if 0


	movi	a4, __start
	movi	a5, __reloc_end

	# a0: address where this code has been loaded
	# a4: compiled address of __start
	# a5: compiled end address

	mov.n	a7, a0
	mov.n	a8, a4

1:
	l32i	a10, a7, 0
	l32i	a11, a7, 4
	s32i	a10, a8, 0
	s32i	a11, a8, 4
	l32i	a10, a7, 8
	l32i	a11, a7, 12
	s32i	a10, a8, 8
	s32i	a11, a8, 12
	addi	a8, a8, 16
	addi	a7, a7, 16
	blt	a8, a5, 1b


	/* We have to flush and invalidate the caches here before we jump. */

#if XCHAL_DCACHE_IS_WRITEBACK

	___flush_dcache_all a5 a6

#endif

	___invalidate_icache_all a5 a6
	isync

	movi	a11, _reloc
	jx	a11

	.globl _reloc
_reloc:
#endif
	/* RedBoot is now at the end of the memory, so we don't have
	 * to copy the parameter list. Keep the code around; in case
	 * we need it again. */
#if 0
	# a0: load address
	# a2: start address of parameter list
	# a3: length of parameter list
	# a4: __start

	/* copy the parameter list out of the way */

	movi	a6, _param_start
	add	a3, a2, a3
2:
	l32i	a8, a2, 0
	s32i	a8, a6, 0
	addi	a2, a2, 4
	addi	a6, a6, 4
	blt	a2, a3, 2b
#endif

	/* clear BSS section */

	movi	a6, __bss_start
	movi	a7, __bss_end
	movi.n	a5, 0
3:
	s32i	a5, a6, 0
	addi	a6, a6, 4
	blt	a6, a7, 3b

	movi	a5, -16
	movi	a1, _stack + STACK_SIZE
	and	a1, a1, a5

	/* Uncompress the kernel */

	# a0: load address
	# a2: boot parameter
	# a4: __start

	movi	a3, __image_load
	sub	a4, a3, a4
	add	a8, a0, a4

	# a1  Stack
	# a8(a4)  Load address of the image

	movi	a6, _image_start
	movi	a10, _image_end
	movi	a7, 0x1000000
	sub	a11, a10, a6
	movi	a9, complen
	s32i	a11, a9, 0

	movi	a0, 0

	# a6 destination
	# a7 maximum size of destination
	# a8 source
	# a9 ptr to length

	.extern gunzip
	movi	a4, gunzip
	beqz	a4, 1f

	callx4	a4

	j	2f


	# a6 destination start
	# a7 maximum size of destination
	# a8 source start
	# a9 ptr to length
	# a10 destination end

1:
        l32i    a9, a8, 0
        l32i    a11, a8, 4
        s32i    a9, a6, 0
        s32i    a11, a6, 4
        l32i    a9, a8, 8
        l32i    a11, a8, 12
        s32i    a9, a6, 8
        s32i    a11, a6, 12
        addi    a6, a6, 16
        addi    a8, a8, 16
        blt     a6, a10, 1b


	/* jump to the kernel */
2:
#if XCHAL_DCACHE_IS_WRITEBACK

	___flush_dcache_all a5 a6

#endif

	___invalidate_icache_all a5 a6

	isync

	movi	a5, __start
	#movi	a3, boot_initrd_start
	#movi	a4, boot_initrd_end
	sub	a3, a3, a5
	sub	a4, a4, a5
	add	a3, a0, a3
	add	a4, a0, a4

	# a2  Boot parameter list
	# a3  initrd_start (virtual load address)
	# a4  initrd_end   (virtual load address)

	movi	a0, _image_start
	jx	a0

	.align 16
	.data
	.globl avail_ram
avail_ram:
	.long	_heap
	.globl end_avail
end_avail:
	.long	_heap + HEAP_SIZE


	.comm _stack, STACK_SIZE
	.comm _heap, HEAP_SIZE

	.globl end_avail
	.comm complen, 4

	.end	literal_prefix


#endif
