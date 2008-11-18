/*
 * include/asm-xtensa/highmem.h
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2003 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_HIGHMEM_H
#define _XTENSA_HIGHMEM_H

#ifdef __KERNEL__

#include <asm/fixmap.h>
#include <asm/kmap_types.h>
#include <asm/pgtable.h>

extern void *kmap_high(struct page *page);
extern void kunmap_high(struct page *page);

static inline void *kmap(struct page *page)
{
	BUG_ON(in_interrupt());
	if (!PageHighMem(page))
		return page_address(page);
	return kmap_high(page);
}

static inline void kunmap(struct page *page)
{
	BUG_ON(in_interrupt());
	if (!PageHighMem(page))
		return;
	kunmap_high(page);
}

extern void *kmap_atomic(struct page *page, enum km_type type);
extern void kunmap_atomi(void *kvaddr, enum km_type type);
extern struct page *kmap_atomic_to_page(void *vaddr);

#define flush_cache_kmaps()	flush_cache_all()

#endif /* __KERNEL__ */
#endif /* _XTENSA_HIGHMEM_H */
