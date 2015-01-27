/*
 * include/asm-xtensa/processor.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2008 Tensilica Inc.
 */

#ifndef _XTENSA_PROCESSOR_H
#define _XTENSA_PROCESSOR_H

#include <variant/core.h>
#include <platform/hardware.h>

#include <linux/compiler.h>
#include <asm/ptrace.h>
#include <asm/types.h>
#include <asm/regs.h>

#define ARCH_SLAB_MINALIGN	XCHAL_DATA_WIDTH

#define STACK_TOP	TASK_SIZE
#define STACK_TOP_MAX	STACK_TOP

#ifndef __ASSEMBLY__

typedef struct {
	unsigned long seg;
} mm_segment_t;

/* Forward declaration */
struct task_struct;

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr()  ({ __label__ _l; _l: &&_l;})

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE     (TASK_SIZE / 2)

/* Free all resources held by a thread. */
#define release_thread(thread) do { } while(0)

/* Copy and release all segment info associated with a VM */
#define copy_segments(p, mm)	do { } while(0)
#define release_segments(mm)	do { } while(0)
#define forget_segments()	do { } while (0)

#define thread_saved_pc(tsk)	(task_pt_regs(tsk)->pc)

extern unsigned long get_wchan(struct task_struct *p);

#define KSTK_EIP(tsk)		(task_pt_regs(tsk)->pc)
#define KSTK_ESP(tsk)		(pt_areg(task_pt_regs(tsk), 1))

#define cpu_relax()  barrier()
#define cpu_relax_lowlatency() cpu_relax()

/* Special register access. */

#define WSR(v,sr) __asm__ __volatile__ ("wsr %0,"__stringify(sr) :: "a"(v));
#define RSR(v,sr) __asm__ __volatile__ ("rsr %0,"__stringify(sr) : "=a"(v));

#define set_sr(x,sr) ({unsigned int v=(unsigned int)x; WSR(v,sr);})
#define get_sr(sr) ({unsigned int v; RSR(v,sr); v; })

#ifndef XCHAL_HAVE_EXTERN_REGS
#define XCHAL_HAVE_EXTERN_REGS 0
#endif

#if XCHAL_HAVE_EXTERN_REGS

static inline void set_er(unsigned long value, unsigned long addr)
{
	asm volatile ("wer %0, %1" : : "a" (value), "a" (addr) : "memory");
}

static inline unsigned long get_er(unsigned long addr)
{
	register unsigned long value;
	asm volatile ("rer %0, %1" : "=a" (value) : "a" (addr) : "memory");
	return value;
}

#endif /* XCHAL_HAVE_EXTERN_REGS */

#endif	/* __ASSEMBLY__ */

#if XCHAL_XEA_VERSION <= 2
#include <asm/processor-xea2.h>
#elif XCHAL_XEA_VERSION == 3
#include <asm/processor-xea3.h>
#else
#error Unsupported XEA version
#endif

#endif	/* _XTENSA_PROCESSOR_H */
