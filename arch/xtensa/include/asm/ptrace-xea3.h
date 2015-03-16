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

	union {
		unsigned long dummy;
		unsigned long sar;		/*  44 */
	};
	unsigned long threadptr;	/*  68 */
	unsigned long syscall;		/*  56 */
	unsigned long windowbase;	/*  48 */
	/* Don't change order of the fields below */
	unsigned long scompare1;	/*  64 */
	unsigned long lbeg;		/*  32 */
	unsigned long lend;		/*  36 */
	unsigned long lcount;		/*  40 */
	unsigned long pc;		/*   4 */
	unsigned long ps;		/*   8 */
	unsigned long excvaddr;		/*  12 */
	unsigned long exccause;		/*  16 */

	/* current register frame.
	 * Note: The ESF for kernel exceptions ends after 16 registers!
	 */
	unsigned long areg_hi[8];
	unsigned long spill_areg[8];
};

#include <asm/regs.h>
#include <variant/core.h>

#define EXCEPTION_FRAME_SIZE (sizeof(struct pt_regs) < 128 ? \
	128 : ((sizeof(struct pt_regs) + 15) & 0xfffffff0))

unsigned long *__pt_areg(struct pt_regs *regs, unsigned i);
static inline unsigned long *_pt_areg(struct pt_regs *regs, unsigned i)
{
	if (i < 8) {
		unsigned long p = (unsigned long)(regs + 1) -
			EXCEPTION_FRAME_SIZE;
		return (unsigned long *)p - 8 + i;
	} else if (i < 16) {
		return regs->areg_hi + i - 8;
	} else if (i < XCHAL_NUM_AREGS) {
		return regs->spill_areg + i - 16;
	} else {
		return __pt_areg(regs, i);
	}
}

# define pt_areg(regs, i) (*_pt_areg(regs, i))
# define arch_has_single_step()	(1)
# define task_pt_regs(tsk) ((struct pt_regs*) \
	(task_stack_page(tsk) + KERNEL_STACK_SIZE - XCHAL_NUM_AREGS * 4) - 1)
# define instruction_pointer(regs) ((regs)->pc)

# define user_mode(regs) (((regs)->ps & PS_RING_MASK)!=0)
# define return_pointer(regs) pt_areg(regs, 0)

#else	/* __ASSEMBLY__ */

#define PT_USER_SIZE	  (XCHAL_NUM_AREGS * 4 + PT_SIZE)
#define PT_REGS_OFFSET	  (KERNEL_STACK_SIZE - PT_USER_SIZE)

#endif	/* !__ASSEMBLY__ */

#endif	/* _XTENSA_PTRACE_XEA3_H */
