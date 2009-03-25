/*
 * include/asm-xtensa/ptrace.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2009 Tensilica Inc.
 */

#ifndef _XTENSA_PTRACE_H
#define _XTENSA_PTRACE_H

/*
 * Kernel stack:
 *
 *                 +-----------------------+  -------- STACK_SIZE
 *                 |     register file     |  |
 *                 +-----------------------+  |
 *                 |    struct pt_regs     |  |
 *                 +-----------------------+  | ------ PT_REGS_OFFSET
 * Double          :  16 bytes spill area  :  |  ^
 * Exception       :- - - - - - - - - - - -:  |  |
 * Frame           :    struct pt_regs     :  |  |
 *                 :- - - - - - - - - - - -:  |  |
 *                 |                       |  |  |
 *                 |     memory stack      |  |  |
 *                 |     ~(4k|8k|16k)      |  |  |
 *                 ~                       ~  ~  ~
 *                 ~                       ~  ~  ~
 *                 |                       |  |  |
 *                 |                       |  |  |
 *                 +-----------------------+  |  | --- STACK_BIAS
 *                 |  struct task_struct ? |  |  |  ^
 *  current -->    +-----------------------+  |  |  |
 *                 |  struct thread_info   |  |  |  |
 *                 +-----------------------+ --------
 */

#define KERNEL_STACK_SIZE (CONFIG_STACK_SIZE)

/*  Offsets for exception_handlers[] (3 x 64-entries x 4-byte tables). */

#define EXC_TABLE_KSTK		0x000	/* Kernel Stack */
#define EXC_TABLE_DOUBLE_SAVE	0x004	/* Double exception save area for a0 */
#define EXC_TABLE_FIXUP		0x008	/* Fixup handler */
#define EXC_TABLE_PARAM		0x00c	/* For passing a parameter to fixup */
#define EXC_TABLE_FAST_USER	0x100	/* Fast user exception handler */
#define EXC_TABLE_FAST_KERNEL	0x200	/* Fast kernel exception handler */
#define EXC_TABLE_DEFAULT	0x300	/* Default C-Handler */
#define EXC_TABLE_SIZE		0x400
#define EXC_TABLE_SIZE_LOG2	10

/* Registers used by strace */

#define REG_A_BASE	0x0000
#define REG_AR_BASE	0x0100
#define REG_PC		0x0020
#define REG_PS		0x02e6
#define REG_WB		0x0248
#define REG_WS		0x0249
#define REG_LBEG	0x0200
#define REG_LEND	0x0201
#define REG_LCOUNT	0x0202
#define REG_SAR		0x0203

#define SYSCALL_NR	0x00ff

/* Other PTRACE_ values defined in <linux/ptrace.h> using values 0-9,16,17,24 */

#define PTRACE_GETREGS		12
#define PTRACE_SETREGS		13
#define PTRACE_GETXTREGS	18
#define PTRACE_SETXTREGS	19

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

#if 1
/*
 * This struct defines the way the registers are stored on the
 * kernel stack during a system call or other kernel entry.
 */
struct pt_regs {
	unsigned long pc;		/*   4 */
	unsigned long ps;		/*   8 */
	unsigned long depc;		/*  12 */
	unsigned long exccause;		/*  16 */
	unsigned long excvaddr;		/*  20 */
	unsigned long debugcause;	/*  24 */
	unsigned long wmask;		/*  28 */
	unsigned long lbeg;		/*  32 */
	unsigned long lend;		/*  36 */
	unsigned long lcount;		/*  40 */
	unsigned long sar;		/*  44 */
	unsigned long windowbase;	/*  48 */
	unsigned long windowstart;	/*  52 */
	unsigned long syscall;		/*  56 */
	unsigned long icountlevel;	/*  60 */
	unsigned long scompare1;	/*  64 */

	/* Additional configurable registers that are used by the compiler. */
	xtregs_opt_t xtregs_opt;

	/* Make sure the areg field is 16 bytes aligned. */
	int align[0] __attribute__ ((aligned(16)));

	/* current register frame.
	 * Note: The ESF for kernel exceptions ends after 16 registers!
	 */
	unsigned long areg[16];		/* 128 (64) */
};
#else 

struct pt_regs {
	/* Additional configurable registers that are used by the compiler. */
	xtregs_opt_t xtregs_opt;


	/* fast-handler exception frame */

	/* Make sure the areg field is 16 bytes aligned. */
	int align[0] __attribute__ ((aligned(16)));

	unsigned long pc;		/*   4 */
	unsigned long ps;		/*   8 */
	unsigned long depc;		/*  12 */
	unsigned long exccause;		/*  16 */
	unsigned long excvaddr;		/*  20 */
	unsigned long debugcause;	/*  24 */
	unsigned long wmask;		/*  28 */
	unsigned long lbeg;		/*  32 */
	unsigned long lend;		/*  36 */
	unsigned long lcount;		/*  40 */
	unsigned long sar;		/*  44 */
	unsigned long windowbase;	/*  48 */
	unsigned long windowstart;	/*  52 */
	unsigned long syscall;		/*  56 */
	unsigned long icountlevel;	/*  60 */
	unsigned long scompare1;	/*  64 */

	/* current register frame. */
	unsigned long a[16];
};
#endif /* 1 */

/*
 * Used by kgdb_read_reg() for the 'p' command.
 */
#define REG_GDB__A_BASE     0xfc000000
#define REG_GDB_AR_BASE     0x00000100
#define REG_GDB_PC          0x00000020
#define REG_GDB_PS          0x000002e6
#define REG_GDB_WB          0x00000248
#define REG_GDB_WS          0x00000249
#define REG_GDB_LBEG        0x00000200
#define REG_GDB_LEND        0x00000201
#define REG_GDB_LCOUNT      0x00000202
#define REG_GDB_SAR         0x00000203
#define REG_GDB_DEPC        0x000002c0
#define REG_GDB_EXCCAUSE    0x000002e8
#define REG_GDB_EXCVADDR    0x000002ee
#define REG_GDB_ORIG_AREG2  0x1

#include <asm/variant/core.h>
#include <asm/core.h>

# define task_pt_regs(tsk) ((struct pt_regs*) \
  (task_stack_page(tsk) + KERNEL_STACK_SIZE - (XCHAL_NUM_AREGS-16)*4) - 1)
# define user_mode(regs) (((regs)->ps & 0x00000020)!=0)
# define instruction_pointer(regs) ((regs)->pc)
# define return_pointer(regs) (MAKE_PC_FROM_RA((regs)->areg[0],(regs)->areg[1]))
extern void show_regs(struct pt_regs *);

# ifndef CONFIG_SMP
#  define profile_pc(regs) instruction_pointer(regs)
# else
#  define profile_pc(regs)						     \
	({								     \
		in_lock_functions(instruction_pointer(regs)) ? 		     \
			return_pointer(regs) : instruction_pointer(regs);    \
	})
# endif

#else	/* __ASSEMBLY__ */

# include <asm/asm-offsets.h>
#define PT_REGS_OFFSET	  (KERNEL_STACK_SIZE - PT_USER_SIZE)

#endif	/* !__ASSEMBLY__ */

#endif  /* __KERNEL__ */

#endif	/* _XTENSA_PTRACE_H */
