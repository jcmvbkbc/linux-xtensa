/*
 * This file contains all exception vectors (user, kernel, and double),
 * as well as the window vectors (overflow and underflow), and the debug
 * vector. These are the primary vectors executed by the processor if an
 * exception occurs.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2015 Cadence Design Systems Inc.
 */

#include <linux/linkage.h>
#include <asm/ptrace.h>
#include <asm/current.h>
#include <asm/asm-offsets.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/thread_info.h>
#include <asm/vectors.h>

	.section .KernelExceptionVector.text, "ax"

ENTRY(_KernelExceptionVector)

	j	_SaveSegment

ENDPROC(_KernelExceptionVector)

	.section .DoubleExceptionVector.text, "ax"

_DoubleExceptionVector:
1:	j	1b

_TailChainSegment:
	rsil	a15, 0
	//rsil	a15, LOCKLEVEL
	l32dis	a10, a1, 1
	l32dis	a11, a1, 3
_TailChainSegmentException:
	rsil	a15, LOCKLEVEL
	s32dis	a12, a1, 0 /* 128 bytes stack frame */
#if PT_SIZE > 128
	addi	a9, a9, -((PT_SIZE - 128 + 0xf) & 0xfffffff0)
	addi	a10, a9, ((-PT_SIZE) & 0x0000000f)
#elif PT_SIZE < 128
	addi	a10, a9, 128 - PT_SIZE
#endif
	s32dis	a8, a1, 4

_ExitSegment:
	l32dis	a13, a1, 13
#if XCHAL_HAVE_S32C1I
	l32e	a11, a1, -32 /* SCOMPARE1 */
	wsr	a11, scompare1
#endif
	l32e	a10, a1, -28 /* LBEG */
	l32e	a11, a1, -24 /* LEND */
	l32e	a15, a1, -20 /* LCOUNT */
	l32dis	a12, a1, 12

_RestoreSegment:
_UnderflowSegment:
	l32e	a8, a1, -64
	l32e	a9, a1, -64
	l32e	a10, a1, -44
	l32e	a11, a1, -40
	l32e	a12, a1, -48
	l32e	a13, a1, -60
	l32e	a14, a1, -64
	l32e	a15, a1, -36

_SaveSegment:
_OverflowSegment:
	s32e	a8, a1, -60
	s32e	a9, a1, -64
	s32e	a10, a1, -44
	s32e	a11, a1, -40
	s32e	a12, a1, -48
	s32e	a13, a1, -56
	s32e	a14, a1, -52
	s32e	a15, a1, -36

_EntrySegment:
	s32e	a10, a1, -28 /* LBEG */
	s32e	a11, a1, -24 /* LEND */
	s32e	a15, a1, -20 /* LCOUNT */
	s32e	a14, a1, -8  /* EXCVADDR */
#if XCHAL_HAVE_S32C1I
	rsr	a11, scompare1
	s32e	a11, a1, -32 /* SCOMPARE1 */
#endif
	l32dis	a10, a1, 1
	l32dis	a11, a1, 3
	rsil	a15, LOCKLEVEL
	s32dis	a13, a1, 8 /* 128 bytes stack frame */
#if PT_SIZE > 128
	addi	a9, a9, -((PT_SIZE - 128 + 0xf) & 0xfffffff0)
	addi	a10, a9, ((-PT_SIZE) & 0x0000000f)
#elif PT_SIZE < 128
	addi	a10, a9, 128 - PT_SIZE
#endif
	s32dis	a12, a1, 12

//Exception

	addi	a0, a0, 3
	j	common_exception


/* Window overflow and underflow handlers.
 * The handlers must be 64 bytes apart, first starting with the underflow
 * handlers underflow-4 to underflow-12, then the overflow handlers
 * overflow-4 to overflow-12.
 *
 * Note: We rerun the underflow handlers if we hit an exception, so
 *	 we try to access any page that would cause a page fault early.
 */

#define ENTRY_ALIGN64(name)	\
	.globl name;		\
	.align 64;		\
	name:

	.section		.WindowVectors.text, "ax"


/* 4-Register Window Overflow Vector (Handler) */

ENTRY_ALIGN64(_WindowOverflow4)

	j	_TailChainSegment
	j	_TailChainSegmentException

ENDPROC(_WindowOverflow4)

/* 4-Register Window Underflow Vector (Handler) */

ENTRY_ALIGN64(_WindowUnderflow4)

0:	j	0b

ENDPROC(_WindowUnderflow4)

/* 8-Register Window Overflow Vector (Handler) */

ENTRY_ALIGN64(_WindowOverflow8)

	j	_OverflowSegment

ENDPROC(_WindowOverflow8)

/* 8-Register Window Underflow Vector (Handler) */

ENTRY_ALIGN64(_WindowUnderflow8)

	j	_UnderflowSegment

ENDPROC(_WindowUnderflow8)

	.text
