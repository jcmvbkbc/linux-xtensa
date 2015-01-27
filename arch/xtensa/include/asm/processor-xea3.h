/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2015 Cadence Design Systems Inc.
 */

#ifndef _XTENSA_PROCESSOR_XEA3_H
#define _XTENSA_PROCESSOR_XEA3_H

#include <variant/core.h>
#include <platform/hardware.h>

#include <linux/compiler.h>
#include <asm/ptrace.h>
#include <asm/types.h>
#include <asm/regs.h>

#ifdef CONFIG_MMU
#define TASK_SIZE	__XTENSA_UL_CONST(0x80000000)
#else
#define TASK_SIZE	(PLATFORM_DEFAULT_MEM_START + PLATFORM_DEFAULT_MEM_SIZE)
#endif

/*
 * General exception cause assigned to debug exceptions. Debug exceptions go
 * to their own vector, rather than the general exception vectors (user,
 * kernel, double); and their specific causes are reported via DEBUGCAUSE
 * rather than EXCCAUSE.  However it is sometimes convenient to redirect debug
 * exceptions to the general exception mechanism.  To do this, an otherwise
 * unused EXCCAUSE value was assigned to debug exceptions for this purpose.
 */

#define EXCCAUSE_MAPPED_DEBUG	63

/*
 * We use DEPC also as a flag to distinguish between double and regular
 * exceptions. For performance reasons, DEPC might contain the value of
 * EXCCAUSE for regular exceptions, so we use this definition to mark a
 * valid double exception address.
 * (Note: We use it in bgeui, so it should be 64, 128, or 256)
 */

#define VALID_DOUBLE_EXCEPTION_ADDRESS	64

/* LOCKLEVEL defines the interrupt level that masks all
 * general-purpose interrupts.
 */
#define LOCKLEVEL 0x8

#ifndef __ASSEMBLY__

/* Build a valid return address for the specified call winsize.
 * winsize must be 1 (call4), 2 (call8), or 3 (call12)
 */
#define MAKE_RA_FOR_CALL(ra,ws)   (ra)

/* Convert return address to a valid pc
 * Note: We assume that the stack pointer is in the same 1GB ranges as the ra
 */
#define MAKE_PC_FROM_RA(ra,sp)    (ra)

#define SPILLED_REG(sp, reg) (*(((unsigned long *)(sp)) - 8 + (reg)))

struct thread_struct {

	/* kernel's return address and stack pointer for context switching */
	unsigned long ra; /* kernel's a0: return address and window call size */
	unsigned long sp; /* kernel's a1: stack pointer */
	unsigned long ps; /* kernel's ps: Stk, mainly */

	mm_segment_t current_ds;    /* see uaccess.h for example uses */

	/* struct xtensa_cpuinfo info; */

	unsigned long bad_vaddr; /* last user fault */
	unsigned long bad_uaddr; /* last kernel fault accessing user space */
	unsigned long error_code;

	unsigned long ibreak[XCHAL_NUM_IBREAK];
	unsigned long dbreaka[XCHAL_NUM_DBREAK];
	unsigned long dbreakc[XCHAL_NUM_DBREAK];

	/* Make structure 16 bytes aligned. */
	int align[0] __attribute__ ((aligned(16)));
};

#define INIT_THREAD  \
{									\
	.ra =		0,						\
	.sp =		sizeof(init_stack) + (long) &init_stack,	\
	.ps =		(PS_STACK_KERNEL << PS_STACK_SHIFT),		\
	.current_ds =	{0},						\
	.bad_vaddr =	0,						\
	.bad_uaddr =	0,						\
	.error_code =	0,						\
}


/*
 * Do necessary setup to start up a newly executed thread.
 * Note: We set-up ps as if we did a call4 to the new pc.
 *       set_thread_state in signal.c depends on it.
 */
#define USER_PS_VALUE ((PS_STACK_PAGE << PS_STACK_SHIFT) |		\
		       (USER_RING << PS_RING_SHIFT))

/* Clearing a0 terminates the backtrace. */
#define start_thread(regs, new_pc, new_sp) \
do { \
	memset(regs, 0, sizeof(*regs)); \
	regs->pc = new_pc; \
	regs->ps = USER_PS_VALUE; \
	pt_areg(regs, 1) = new_sp; \
	pt_areg(regs, 0) = 0; \
	regs->windowbase = 0x80000000; \
} while (0)

#endif	/* __ASSEMBLY__ */
#endif	/* _XTENSA_PROCESSOR_XEA3_H */
