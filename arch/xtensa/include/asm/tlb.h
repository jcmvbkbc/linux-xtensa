/*
 * include/asm-xtensa/tlb.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2009 Tensilica Inc.
 */

#ifndef _XTENSA_TLB_H
#define _XTENSA_TLB_H

#include <asm/cache.h>
#include <asm/page.h>

#if !defined(DCACHE_ALIASING_POSSIBLE) && !defined(CONFIG_SMP)

/* 
 * Note, read http://lkml.org/lkml/2004/1/15/6 
 *
 * For SMP using HyperSPARC/srmmu/sparc32 form;
 * like Hypersparc we can't save cpu in page flags.
 *				-piet
 */

# define tlb_start_vma(tlb,vma)			do { } while (0)
# define tlb_end_vma(tlb,vma)			do { } while (0)

#else

/*
 * It appears that we currently may not be able to trust tlb->fullmm;
 * similar to Sparc64.
 *
 * I had strange panics after taking it out. Keeping it, at least
 * till I'm sure it's not needed. LTP test are failing without
 * mandating this flush:
 *
 *	 do_unaligned_user()
 *       Division by zero [ExcCause:6) in load_balance()]
 *
 * When you exit tlb->fullmm will be set so the vma cache won't be flushed.
 * Need to check mapped file vma's to see if they get flushed. Being
 * conservative for now.
 *
 * REMIND-FIXME:
 *
 *    This Division by zero may have been the race condition that I fixed in
 *    cpu_avg_load_per_task(); I'll try to remove this. The "strange" panics
 *    may have "just" been the variables in the exception frame not being
 *    presented correctly by xt-gdb (fixed in entry.S).
 *
 *					-piet		
 */
#if defined(CONFIG_SMP)
# define tlb_start_vma(tlb, vma)					      \
	do {								      \
		flush_cache_range(vma, vma->vm_start, vma->vm_end);  	      \
	} while(0)

# define tlb_end_vma(tlb, vma)						      \
	do {								      \
		flush_tlb_range(vma, vma->vm_start, vma->vm_end);     	      \
	} while(0)

#else /* !defined(CONFIG_SMP) */

# define tlb_start_vma(tlb, vma)					      \
	do {								      \
		if (!tlb->fullmm)					      \
			flush_cache_range(vma, vma->vm_start, vma->vm_end);   \
	} while(0)

# define tlb_end_vma(tlb, vma)						      \
	do {								      \
		if (!tlb->fullmm)					      \
			flush_tlb_range(vma, vma->vm_start, vma->vm_end);     \
	} while(0)
#endif
#endif

#define __tlb_remove_tlb_entry(tlb,pte,addr)	do { } while (0)
#define tlb_flush(tlb)				flush_tlb_mm((tlb)->mm)

#include <asm-generic/tlb.h>

#define __pte_free_tlb(tlb, pte)		pte_free((tlb)->mm, pte)

#endif	/* _XTENSA_TLB_H */
