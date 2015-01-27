/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2015 Cadence Design Systems Inc.
 */
#ifndef _XTENSA_PTRACE_XEA3_H
#define _XTENSA_PTRACE_XEA3_H

#include <uapi/asm/ptrace.h>

/*
 * Kernel stack
 *
 *		+-----------------------+  -------- STACK_SIZE
 *		|     register file     |  |
 *		+-----------------------+  |
 *		|    struct pt_regs     |  |
 *		+-----------------------+  | ------ PT_REGS_OFFSET
 *		|                       |  |  |
 *		|     memory stack      |  |  |
 *		|                       |  |  |
 *		~                       ~  ~  ~
 *		~                       ~  ~  ~
 *		|                       |  |  |
 *		|                       |  |  |
 *		+-----------------------+  |  |
 *		|  struct task_struct   |  |  |
 *  current --> +-----------------------+  |  |
 *		|  struct thread_info   |  |  |
 *		+-----------------------+ -----
 */

#define KERNEL_STACK_SIZE (2 * PAGE_SIZE)

#ifndef __ASSEMBLY__

#include <asm/coprocessor.h>

struct pt_regs {
	/* Additional configurable registers that are used by the compiler. */
	xtregs_opt_t xtregs_opt;

	unsigned long lbeg;		/*  32 */
	unsigned long lend;		/*  36 */
	unsigned long lcount;		/*  40 */
	unsigned long pc;		/*   4 */
	unsigned long ps;		/*   8 */
	unsigned long excvaddr;		/*  12 */
	union {
		unsigned long dummy;
		unsigned long depc;		/*  12 */
		unsigned long exccause;		/*  16 */
		unsigned long wmask;		/*  28 */
		unsigned long sar;		/*  44 */
		unsigned long windowbase;	/*  48 */
		unsigned long windowstart;	/*  52 */
		unsigned long syscall;		/*  56 */
		unsigned long icountlevel;	/*  60 */
		unsigned long scompare1;	/*  64 */
		unsigned long threadptr;	/*  68 */
	};

	unsigned long xreg[8];
	/* current register frame.
	 * Note: The ESF for kernel exceptions ends after 16 registers!
	 */
	unsigned long areg[8];
};

#include <asm/regs.h>
#include <variant/core.h>

# define arch_has_single_step()	(1)
# define task_pt_regs(tsk) ((struct pt_regs*) \
	(task_stack_page(tsk) + KERNEL_STACK_SIZE - (XCHAL_NUM_AREGS-16)*4) - 1)
# define instruction_pointer(regs) ((regs)->pc)

# define user_mode(regs) (((regs)->ps & PS_RING_MASK)!=0)
# define return_pointer(regs) ((regs)->areg[0])

#else	/* __ASSEMBLY__ */

#define PT_REGS_OFFSET	  (KERNEL_STACK_SIZE - PT_USER_SIZE)

#endif	/* !__ASSEMBLY__ */

#endif	/* _XTENSA_PTRACE_XEA3_H */
