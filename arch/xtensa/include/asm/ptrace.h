/*
 * include/asm-xtensa/ptrace.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */
#ifndef _XTENSA_PTRACE_H
#define _XTENSA_PTRACE_H

#include <linux/compiler.h>
#include <uapi/asm/ptrace.h>
#include <variant/core.h>

#ifndef __ASSEMBLY__

struct task_struct;

int ptrace_getregs(struct task_struct *child, void __user *uregs);
int ptrace_setregs(struct task_struct *child, void __user *uregs);
int ptrace_peekusr(struct task_struct *child, long regno, long __user *ret);
int ptrace_peekusr_common(struct task_struct *child, long regno,
			  long __user *ret);

# ifndef CONFIG_SMP
#  define profile_pc(regs) instruction_pointer(regs)
# else
#  define profile_pc(regs)						\
	({								\
		in_lock_functions(instruction_pointer(regs)) ?		\
		return_pointer(regs) : instruction_pointer(regs);	\
	})
# endif

#define user_stack_pointer(regs) (pt_areg(regs, 1))

#else	/* __ASSEMBLY__ */

# include <asm/asm-offsets.h>

#endif	/* !__ASSEMBLY__ */

#if XCHAL_XEA_VERSION <= 2
#include <asm/ptrace-xea2.h>
#elif XCHAL_XEA_VERSION == 3
#include <asm/ptrace-xea3.h>
#else
#error Unsupported XEA version
#endif

#endif	/* _XTENSA_PTRACE_H */
