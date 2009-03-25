/*
 * include/asm-xtensa/cacheflush.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * (C) 2001 - 2007 Tensilica Inc.
 */

#ifndef _XTENSA_CACHEFLUSH_H
#define _XTENSA_CACHEFLUSH_H

#ifdef __KERNEL__

#include <linux/mm.h>
#include <asm/processor.h>
#include <asm/page.h>

/*
 * Lo-level routines for cache flushing.
 *
 * invalidate data or instruction cache:
 *
 * __invalidate_icache_all()
 * __invalidate_icache_page(adr)
 * __invalidate_dcache_page(adr)
 * __invalidate_icache_range(from,size)
 * __invalidate_dcache_range(from,size)
 *
 * flush data cache:
 *
 * __flush_dcache_page(adr)
 *
 * flush and invalidate data cache:
 *
 * __flush_invalidate_dcache_all()
 * __flush_invalidate_dcache_page(adr)
 * __flush_invalidate_dcache_range(from,size)
 *
 * specials for cache aliasing:
 *
 * __flush_invalidate_dcache_page_alias(vaddr,paddr)
 * __invalidate_icache_page_alias(vaddr,paddr)
 */

extern void __invalidate_dcache_all(void);
extern void __invalidate_icache_all(void);
extern void __invalidate_dcache_page(unsigned long);
extern void __invalidate_icache_page(unsigned long);
extern void __invalidate_icache_range(unsigned long, unsigned long);
extern void __invalidate_dcache_range(unsigned long, unsigned long);

#if XCHAL_DCACHE_IS_WRITEBACK
extern void __flush_invalidate_dcache_all(void);
extern void __flush_dcache_page(unsigned long);
extern void __flush_dcache_range(unsigned long, unsigned long);
extern void __flush_invalidate_dcache_page(unsigned long);
extern void __flush_invalidate_dcache_range(unsigned long, unsigned long);
#else
# define __flush_dcache_range(p,s)		do { } while(0)
# define __flush_dcache_page(p)			do { } while(0)
# define __flush_invalidate_dcache_page(p) 	__invalidate_dcache_page(p)
# define __flush_invalidate_dcache_range(p,s)	__invalidate_dcache_range(p,s)
#endif

#if defined(DCACHE_ALIASING_POSSIBLE)
extern void __flush_invalidate_dcache_page_alias(unsigned long, unsigned long);
#else
static inline void __flush_invalidate_dcache_page_alias(unsigned long v, unsigned long p) 
{
	/* 
	 * Using static inline instead of #define to avoid unused
	 * variable compile warnings in calling functions.
	 */
}
#endif

#if defined(ICACHE_ALIASING_POSSIBLE)
extern void __invalidate_icache_page_alias(unsigned long, unsigned long);
#else
static inline void __invalidate_icache_page_alias(unsigned long v, unsigned long p)
{
	/* 
	 * Using static inline instead of #define to avoid unused
	 * variable compile warnings in calling functions.
	 */
}
#endif

/*
 * We have physically tagged caches - nothing to do here -
 * unless we have cache aliasing.
 *
 * Pages can get remapped. Because this might change the 'color' of that page,
 * we have to flush the cache before the PTE is changed.
 * (see also Documentation/cachetlb.txt)
 */

#define cache_is_vivt()                  0
#define cache_is_vipt()                  1

#if defined(DCACHE_ALIASING_POSSIBLE) || defined(CONFIG_SMP)
# define cache_is_vipt_aliasing()	 1
# define cache_is_vipt_nonaliasing()	 0
#else
# define cache_is_vipt_aliasing()        0
# define cache_is_vipt_nonaliasing()     1
#endif

#if defined(DCACHE_ALIASING_POSSIBLE) || defined(CONFIG_SMP)

extern void flush_cache_all(void);

#define local_flush_cache_all()						\
	do {								\
		__flush_invalidate_dcache_all();			\
		__invalidate_icache_all();				\
	} while (0)

#define flush_cache_mm(mm)		flush_cache_all()
#define flush_cache_dup_mm(mm)		flush_cache_mm(mm)

#define flush_cache_vmap(start,end)	flush_cache_all()
#define flush_cache_vunmap(start,end)	flush_cache_all()

#if defined(DCACHE_ALIASING_POSSIBLE) || defined(CONFIG_SMP)
extern void flush_dcache_page(struct page*);
#else
#define flush_dcache_page(page)		do { } while (0)
#endif

extern void flush_cache_range(struct vm_area_struct*, ulong, ulong);
extern void local_flush_cache_range(struct vm_area_struct*, ulong, ulong);
extern void flush_cache_page(struct vm_area_struct*, unsigned long, unsigned long);
extern void local_flush_cache_page(struct vm_area_struct*, unsigned long, unsigned long);

/*
 * For Our VIPT cache flush_anon_page() likely is redundant 
 * with flush_dcache_page().
 */
#define ARCH_HAS_FLUSH_ANON_PAGE
static inline void flush_anon_page(struct vm_area_struct *vma,
			 struct page *page, unsigned long vmaddr)
{
	extern void __flush_anon_page(struct vm_area_struct *vma,
				struct page *, unsigned long);
	if (PageAnon(page))
		__flush_anon_page(vma, page, vmaddr);
}

#else /* DCACHE_ALIASING_POSSIBLE && !DEFINED_SMP */


#define local_flush_cache_all()				do { } while (0)
#define flush_cache_all()				do { } while (0)
#define flush_cache_mm(mm)				do { } while (0)
#define flush_cache_dup_mm(mm)				do { } while (0)

#define flush_cache_vmap(start,end)			do { } while (0)
#define flush_cache_vunmap(start,end)			do { } while (0)

#define flush_dcache_page(page)				do { } while (0)
#define flush_flush_anon_page(vma, page, vmaddr)	do { } while (0)

#define local_flush_cache_page(vma, addr, pfn)		do { } while (0)
#define local_flush_cache_range(vma, start, end)	do { } while (0)
#define flush_cache_page(vma, addr, pfn)		do { } while (0)
#define flush_cache_range(vma, start, end)		do { } while (0)

#endif  /* DCACHE_ALIASING_POSSIBLE && !DEFINED_SMP */

/* Ensure consistency between data and instruction cache. */
#define local_flush_icache_range(start, end) 				\
	do {								\
		__flush_dcache_range(start, (end) - (start));		\
		__invalidate_icache_range(start,(end) - (start));	\
	} while (0)

#ifdef CONFIG_SMP
extern void flush_icache_range(unsigned long start, unsigned long end);
#else
/* REMIND-FIXME: Needed for KGDB */
static inline void flush_icache_range(unsigned long start, unsigned long end) {
}
#endif

/* This is not required, see Documentation/cachetlb.txt */
#define	flush_icache_page(vma,page)			do { } while (0)

#define flush_dcache_mmap_lock(mapping)	\
	spin_lock_irq(&(mapping)->tree_lock)

#define flush_dcache_mmap_unlock(mapping) \
	spin_unlock_irq(&(mapping)->tree_lock)

#if defined(DCACHE_ALIASING_POSSIBLE) || defined(CONFIG_SMP)
extern void copy_to_user_page(struct vm_area_struct*, struct page*,
		unsigned long, void*, const void*, unsigned long);
extern void copy_from_user_page(struct vm_area_struct*, struct page*,
		unsigned long, void*, const void*, unsigned long);

#else

#define copy_to_user_page(vma, page, vaddr, dst, src, len)		\
	do {								\
		memcpy(dst, src, len);					\
		__flush_dcache_range((unsigned long) dst, len);		\
		__invalidate_icache_range((unsigned long) dst, len);	\
	} while (0)

#define copy_from_user_page(vma, page, vaddr, dst, src, len)            \
	memcpy(dst, src, len)

#endif

#endif /* __KERNEL__ */
#endif /* _XTENSA_CACHEFLUSH_H */
