/*
 * arch/xtensa/include/asm/pgtable.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Copyright (C) 2001 - 2009 Tensilica Inc.
 */

#ifndef _XTENSA_PGTABLE_H
#define _XTENSA_PGTABLE_H

#include <asm-generic/pgtable-nopmd.h>
#include <asm/page.h>

/*
 * We only use two ring levels, user and kernel space.
 */

#define USER_RING		1	/* user ring level */
#define KERNEL_RING		0	/* kernel ring level */

/*
 * The Xtensa architecture port of Linux has a two-level page table system,
 * i.e. the logical three-level Linux page table layout is folded.
 * Each task has the following memory page tables:
 *
 *   PGD table (page directory), ie. 3rd-level page table:
 *	One page (4 kB) of 1024 (PTRS_PER_PGD) pointers to PTE tables
 *	(Architectures that don't have the PMD folded point to the PMD tables)
 *
 *	The pointer to the PGD table for a given task can be retrieved from
 *	the task structure (struct task_struct*) t, e.g. current():
 *	  (t->mm ? t->mm : t->active_mm)->pgd
 *
 *   PMD tables (page middle-directory), ie. 2nd-level page tables:
 *	Absent for the Xtensa architecture (folded, PTRS_PER_PMD == 1).
 *
 *   PTE tables (page table entry), ie. 1st-level page tables:
 *	One page (4 kB) of 1024 (PTRS_PER_PTE) PTEs with a special PTE
 *	invalid_pte_table for absent mappings.
 *
 * The individual pages are 4 kB big with special pages for the empty_zero_page.
 *  
 *  Details on the Xtensa ARCH are available in the Xtensa ISA
 *   http://www.tensilica.com/products/literature-docs/documentation/xtensa-isa-databook.htm
 *
 */

#define PGDIR_SHIFT	22
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * Entries per page directory level: we use two-level, so
 * we don't really have any PMD directory physically.
 */
#define PTRS_PER_PTE		1024
#define PTRS_PER_PTE_SHIFT	10
#define PTRS_PER_PGD		1024
#define PGD_ORDER		0
#define USER_PTRS_PER_PGD	(TASK_SIZE/PGDIR_SIZE)
#define FIRST_USER_ADDRESS	0
#define FIRST_USER_PGD_NR	(FIRST_USER_ADDRESS >> PGDIR_SHIFT)

/*
 * Kernel's Virtual memory area. We keep a distance to other memory regions to be
 * on the safe side. See also fixmap.h
 */

#define VMALLOC_START		0xC0000000
#define VMALLOC_END		0xC7FEFFFF

#if DCACHE_WAY_SIZE > ICACHE_WAY_SIZE
 #define MAX_CACHE_WAY_SIZE 	DCACHE_WAY_SIZE
 #define MAX_CACHE_WAY_SHIFT	DCACHE_WAY_SHIFT
#else
 #define MAX_CACHE_WAY_SIZE     ICACHE_WAY_SIZE
 #define MAX_CACHE_WAY_SHIFT    ICACHE_WAY_SHIFT
#endif

#if 0
#define TLBTEMP_BASE_1		0xC7FF0000
#define TLBTEMP_BASE_2		(TLBTEMP_BASE_1 + MAX_CACHE_WAY_SIZE)
#define TLBTEMP_BASE_END	(TLBTEMP_BASE_2 + MAX_CACHE_WAY_SIZE)

#else
#define TLBTEMP_BASE_1		0xC7FF0000
#define TLBTEMP_BASE_2		0xC7FF8000
#define TLBTEMP_BASE_END	0xC8000000
#endif



/*
 * For the Xtensa architecture, the PTE layout is as follows:
 *
 *              |<- PAGE ->|
 *              |<- MASK ->|
 *   PTE type    31------12  11---7   6   5-4  3 2   1 0   Cache  Note
 *               ----------------------------------------
 *              |           | Software  | Hard Ware Prot |
 *              |  PPN      | ??WAD | P | RI | CacheAttr |         *0
 *               ----------------------------------------
 *   present    |  PPN      | ??WAD | 1 | RI | 0 0 | w x | bypass  *1
 *   present    |  PPN      | ??WAD | 1 | RI | 0 1 | w x | wrback  *1
 *   present    |  PPN      | ??WAD | 1 | RI | 1 0 | w x | wrthru  *1
 *   present    |  PPN      | ??WAD | 1 | 01 | 1 1 | 0 0 | PROT_NONE (no rwx)
 *   swap       | index   | typ | 1 | 0 | 01 | 1 1 | 0 0 |
 *   pte_none   |       ???     | 0 | 0 | 01 | 1 1 | 0 0 |
 *   reserved   |                   | 0 | RI | 1 1 | 0 1 |
 *   file       |  file page offset | 0 | 01 | 1 1 | 1 0 |
 *   reserved   |                   | 0 | RI | 1 1 | 1 1 |         *2
 *               ----------------------------------------
 *
 *   NOTE *0:   MMU Cache Attributes are explained at Table 4-144 in 
 *              section 4.6.5.10 of the Xtensa ISA.
 *   Note *1:   MMU v1 has a "v" (valid) bit instead of an "x" (execute) bit.
 *   Note *2:   MMU v1 treats this also as "present", with similar format.
 *
 *   Legend:
 *   PPN	Physical Page Number
 *   RI		ring (0=privileged, 1-3=user)
 *   CacheAttr	CA field (cache attribute, a.k.a. AM = access mode)
 *   P		page with CA=1100 is present (disambiguating)
 *   ??WAD	software bits (?=unassigned, W=writable, A=accessed, D=dirty)
 *   w		page is writable
 *   x		page is executable
 *   index	swap offset / PAGE_SIZE (19 bits -> 2 GB)
 *   typ	swap type (5 bits -> 32 types) (identifies swap file!?!?)
 *   file page offset	26-bit offset into the file, in increments of PAGE_SIZE
 *			(for files up to 256 GB)
 *
 * A PTE must never have any of the values marked as reserved.
 * Thus a PTE is either:
 *	present		maps to a physical page in memory
 *	file		?? would map to that portion of a file if accessed ??
 *	swap		swapped out to disk at the indicated offset
 *	none		not mapped (access will cause a fault)
 *
 * We need the PAGE_PRESENT bit to distinguish present but not accessable, 
 * ie: rwx == 0, (PROT_NONE) from other non-readable PTEs (none, file, swap).
 *
 * Similar to the Alpha and MIPS ports, we need to keep track of the ref
 * and mod bits in software.  We have a software "you can read
 * from this page" bit, and a hardware one which actually lets the
 * process read from the page.  On the same token we have a software
 * writable bit and the real hardware one which actually lets the
 * process write to the page.
 */
#define _PAGE_HW_EXEC		(1<<0)	/* hardware: page is executable */
#define _PAGE_HW_WRITE		(1<<1)	/* hardware: page is writable */

#define _PAGE_RIGHTS_FILE	(1<<1)	/* non-linear mapping, if !present */
#define _PAGE_RIGHTS_NONE	(0<<0)	/* special case for VM_PROT_NONE */
#define _PAGE_RIGHTS_MASK	(3<<0)	/* Bits 1..0 */

/* None of these cache modes include MP coherency:  */
#define _PAGE_CA_BYPASS		(0<<2)	/* bypass, non-speculative */
#define _PAGE_CA_WB		(1<<2)	/* write-back */
#define _PAGE_CA_WT		(2<<2)	/* write-through */
#define _PAGE_CA_MASK		(3<<2)	/* Bits 3..2 */
#define _PAGE_CA_INVALID	(3<<2)

#define _PAGE_KERNEL		(0<<4)	/* user access (ring = 1) */
#define _PAGE_USER		(1<<4)	/* user access (ring = 1) */
#define _PAGE_RING_MASK		(3<<4)	/* access bits (ring = 0||1||2||3) */

/* Software - bits 6 7 .  8 9 10 11 */
#define _PAGE_PRESENT		(1<<6)	/* 0X40: software: page present */
#define _PAGE_DIRTY		(1<<7)	/* 0X80: software: page dirty */
#define _PAGE_SWAP		(1<<7)	/* 0X80: software: swap */
#define _PAGE_ACCESSED		(1<<8)	/* 0X100: software: page accessed (read) */
#define _PAGE_WRITABLE		(1<<9)	/* 0X200: software: page writable */
#define _PAGE_WRITABLE_BIT	9
#define _PAGE_SOFTWARE_MASK	(0x3F<<6)

/* 
 * On older HW revisions, MMU V1, we always have to 
 * set bit 0 to indicate a valid PTE 
 */
#if XCHAL_HW_VERSION_MAJOR < 2000
# define _PAGE_VALID	(1<<0)
# define MMU_V1 	1
#else
# define _PAGE_VALID	0
# define MMU_V1 	0
#endif

#define _PAGE_CHG_MASK	(PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY)
#define _PAGE_BASE	 (_PAGE_VALID | _PAGE_CA_WB | _PAGE_ACCESSED | _PAGE_PRESENT) 
#define _PAGE_FILE	(_PAGE_KERNEL | _PAGE_CA_INVALID | _PAGE_RIGHTS_FILE )
#define  PAGE_PROT_MASK (_PAGE_RING_MASK | _PAGE_CA_MASK | _PAGE_RIGHTS_MASK )	/* 0x3F: Bits 5...0 */

#ifdef CONFIG_MMU
#define WITH_PAGE_USER
#ifdef  WITH_PAGE_USER
/*
 * PROT_NONE means the page is protected and NONE of the access modes are allowed, ie: RWX == 0
 * NB: Other ARCH use PAGE_NOTE to imply this but it's use is local to pgtable.h.
 */
#define PAGE_PROT_NONE	   __pgprot(_PAGE_CA_INVALID | _PAGE_PRESENT | _PAGE_USER | _PAGE_RIGHTS_NONE)
#else
#define PAGE_PROT_NONE	   __pgprot(_PAGE_CA_INVALID | _PAGE_PRESENT | _PAGE_RIGHTS_NONE)
#endif
#define PAGE_COPY	   __pgprot(_PAGE_BASE | _PAGE_USER)
#define PAGE_COPY_EXEC	   __pgprot(_PAGE_BASE | _PAGE_USER | _PAGE_HW_EXEC)
#define PAGE_READONLY	   __pgprot(_PAGE_BASE | _PAGE_USER)
#define PAGE_READONLY_EXEC __pgprot(_PAGE_BASE | _PAGE_USER | _PAGE_HW_EXEC)
#define PAGE_SHARED	   __pgprot(_PAGE_BASE | _PAGE_USER | _PAGE_WRITABLE)
#define PAGE_SHARED_EXEC \
	__pgprot(_PAGE_BASE | _PAGE_USER | _PAGE_WRITABLE | _PAGE_HW_EXEC)
#define PAGE_KERNEL	   __pgprot(_PAGE_BASE | _PAGE_HW_WRITE)
#define PAGE_KERNEL_EXEC   __pgprot(_PAGE_BASE|_PAGE_HW_WRITE|_PAGE_HW_EXEC)

/*
 * Systems with Cache Aliasing have to worry about the pages
 * accessed by the pte existing in the cache and not being
 * flushed with the pte changed by the kernel.
 *
 * REMIND: Try to removed this for SMP systems without CACHE_ALIASING.
 */
#if defined(CACHE_ALIASING_POSSIBLE) || defined(CONFIG_SMP)
# define _PAGE_DIRECTORY (_PAGE_VALID | _PAGE_ACCESSED | _PAGE_CA_BYPASS)
#else
# define _PAGE_DIRECTORY (_PAGE_VALID | _PAGE_ACCESSED | _PAGE_CA_WB)
#endif

#else /* no mmu */

# define PAGE_PROT_NONE  __pgprot(0)
# define PAGE_SHARED     __pgprot(0)
# define PAGE_COPY       __pgprot(0)
# define PAGE_READONLY   __pgprot(0)
# define PAGE_KERNEL     __pgprot(0)

#endif

/*
 * On certain configurations of Xtensa MMUs (eg. the initial Linux config),
 * the MMU can't do page protection for execute, and considers that the same as
 * read.  Also, write permissions may imply read permissions.
 * What follows is the closest we can get by reasonable means..
 * See linux/mm/mmap.c for protection_map[] array that uses these definitions.
 */
#define __P000	PAGE_PROT_NONE		/* private --- */
#define __P001	PAGE_READONLY		/* private --r */
#define __P010	PAGE_COPY		/* private -w- */
#define __P011	PAGE_COPY		/* private -wr */
#define __P100	PAGE_READONLY_EXEC	/* private x-- */
#define __P101	PAGE_READONLY_EXEC	/* private x-r */
#define __P110	PAGE_COPY_EXEC		/* private xw- */
#define __P111	PAGE_COPY_EXEC		/* private xwr */

#define __S000	PAGE_PROT_NONE		/* shared  --- */
#define __S001	PAGE_READONLY		/* shared  --r */
#define __S010	PAGE_SHARED		/* shared  -w- */
#define __S011	PAGE_SHARED		/* shared  -wr */
#define __S100	PAGE_READONLY_EXEC	/* shared  x-- */
#define __S101	PAGE_READONLY_EXEC	/* shared  x-r */
#define __S110	PAGE_SHARED_EXEC	/* shared  xw- */
#define __S111	PAGE_SHARED_EXEC	/* shared  xwr */

#ifndef __ASSEMBLY__

#define pte_ERROR(e) \
	printk("%s:%d: bad pte %08lx.\n", __FILE__, __LINE__, pte_val(e))
#define pgd_ERROR(e) \
	printk("%s:%d: bad pgd entry %08lx.\n", __FILE__, __LINE__, pgd_val(e))

extern unsigned long empty_zero_page[1024];

#define ZERO_PAGE(vaddr) (virt_to_page(empty_zero_page))

#ifdef CONFIG_MMU
extern pgd_t swapper_pg_dir[PAGE_SIZE/sizeof(pgd_t)];
extern void paging_init(void);
extern void pgtable_cache_init(void);
#else
# define swapper_pg_dir NULL
static inline void paging_init(void) { }
static inline void pgtable_cache_init(void) { }
#endif


/*
 * The pmd contains the kernel virtual address of the pte page.
 */
#define pmd_page_vaddr(pmd) ((unsigned long)(pmd_val(pmd) & PAGE_MASK))
#define pmd_page(pmd) virt_to_page(pmd_val(pmd))

/*
 * pte_none() returns true if page has no mapping at all.
 * Makes sure the other fields of the pte are NULL:
 *	(pte & _PAGE_MASK) == 0			PPN == 0
 *	(pte & _RING_MASK) == 0
 *	(pte && _PAGE_SOFTWARE_MASK) == 0
 */
#ifdef WITH_PAGE_USER
#define pte_none(pte)	 ((pte_val(pte) & (PAGE_PROT_MASK|_PAGE_PRESENT)) == (_PAGE_CA_INVALID | _PAGE_USER) )
#else
#define pte_none(pte)	 ((pte_val(pte) & (PAGE_PROT_MASK|_PAGE_PRESENT)) == (_PAGE_CA_INVALID) )
#endif

/*
 * pte_present() returns true if the pte_pfn(pte) and protection bits are valid.
 */
#if MMU_V1
#define pte_present(pte) (((pte_val(pte) & _PAGE_CA_MASK) != _PAGE_CA_INVALID)	\
	               || ((pte_val(pte) & _PAGE_RIGHTS_MASK) == _PAGE_RIGHTS_MASK))
#else
#define pte_present(pte) ((pte_val(pte) & _PAGE_CA_MASK) != _PAGE_CA_INVALID \
			|| (pte_val(pte) & (_PAGE_PRESENT|_PAGE_RIGHTS_MASK)) == _PAGE_PRESENT)
#endif

/*
 * Set the pte to be pte_none; pte has no mapping
 */
#ifdef WITH_PAGE_USER
#define pte_clear(mm, addr, ptep)                                               \
	do {                                                                    \
		if (mm != NULL)							\
			update_pte(ptep, __pte(_PAGE_CA_INVALID | _PAGE_USER));    \
		else                                                            \
			update_pte(ptep, __pte(_PAGE_CA_INVALID | _PAGE_USER));    \
	} while(0)
#else
#define pte_clear(mm,addr,ptep)                                         \
        do { update_pte(ptep, __pte(_PAGE_CA_INVALID)); } while(0)
#endif
	


#define pmd_none(pmd)	 (!pmd_val(pmd))
#define pmd_present(pmd) (pmd_val(pmd) & PAGE_MASK)
#define pmd_bad(pmd)	 (pmd_val(pmd) & ~PAGE_MASK)
#define pmd_clear(pmdp)	 do { set_pmd(pmdp, __pmd(0)); } while (0)

/*
 * These functions are only valid if pte_present() IS true.
 */
static inline int pte_write(pte_t pte) { return pte_val(pte) & _PAGE_WRITABLE; }
static inline int pte_dirty(pte_t pte) { return pte_val(pte) & _PAGE_DIRTY; }
static inline int pte_young(pte_t pte) { return pte_val(pte) & _PAGE_ACCESSED; }
static inline int pte_special(pte_t pte) { return 0; }

/*
 * This macro is only valid if pte_present() is NOT true.
 */
static inline int pte_file(pte_t pte)  {
	int retval;

	retval =  ((pte_val(pte) & PAGE_PROT_MASK) == _PAGE_FILE);

	return(retval);
}

static inline pte_t pte_wrprotect(pte_t pte)	
	{ pte_val(pte) &= ~(_PAGE_WRITABLE | _PAGE_HW_WRITE); return pte; }
static inline pte_t pte_mkclean(pte_t pte)
	{ pte_val(pte) &= ~(_PAGE_DIRTY | _PAGE_HW_WRITE); return pte; }
static inline pte_t pte_mkold(pte_t pte)
	{ pte_val(pte) &= ~_PAGE_ACCESSED; return pte; }
static inline pte_t pte_mkdirty(pte_t pte)
	{ pte_val(pte) |= _PAGE_DIRTY; return pte; }
static inline pte_t pte_mkyoung(pte_t pte)
	{ pte_val(pte) |= _PAGE_ACCESSED; return pte; }
static inline pte_t pte_mkwrite(pte_t pte)
	{ pte_val(pte) |= _PAGE_WRITABLE; return pte; }
static inline pte_t pte_mkspecial(pte_t pte)
	{ return pte; }

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */

#define pte_pfn(pte)		(pte_val(pte) >> PAGE_SHIFT)
#define pte_same(a,b)		(pte_val(a) == pte_val(b))
#define pte_page(x)		pfn_to_page(pte_pfn(x))
#define pfn_pte(pfn, prot)	__pte(((pfn) << PAGE_SHIFT) | pgprot_val(prot))
#define mk_pte(page, prot)	pfn_pte(page_to_pfn(page), prot)

static inline pte_t pte_modify(pte_t pte, pgprot_t newprot)
{
	return __pte((pte_val(pte) & _PAGE_CHG_MASK) | pgprot_val(newprot));
}

static inline void update_pte_bp(void) {}
/*
 * Certain architectures need to do special things when pte's
 * within a page table are directly modified.  Thus, the following
 * hook is made available.
 */
static inline void update_pte(pte_t *ptep, pte_t pte)
{
	*ptep = pte;

	if ((pte_val(pte) & PAGE_PROT_MASK) == 0X1f) update_pte_bp();

#if defined(CACHE_ALIASING_POSSIBLE) || defined(CONFIG_SMP)
	/* 
	 * Use a "Data Cache Hit Writeback Invalidate" to force this pte to memory.
	 * Invalidating to make sure we always use what's in physical memory.
	 * This is done to avoid cache aliases in the Page Tables and should
	 * be kept in sync with the _PAGE_DIRECTORY definition above.
	 *
	 * REMIND:
	 *    This is in Chris's code that Joe checked in but NOT in
	 *    Chris's linux-next repo. Need to double check vmcode
	 *    against Christian's current stuff; a number of differences exist.
	 *
	 */
	__asm__ __volatile__ ("dhwbi %0, 0" :: "a" (ptep));
#endif

}

struct mm_struct;

static inline void
set_pte_at(struct mm_struct *mm, unsigned long addr, pte_t *ptep, pte_t pteval)
{
	update_pte(ptep, pteval);
}


static inline void
set_pmd(pmd_t *pmdp, pmd_t pmdval)
{
	*pmdp = pmdval;

#if defined(DCACHE_ALIASING_POSSIBLE) || defined(CONFIG_SMP)
	/* 
	 * Use a "Data Cache Hit Writeback Invalidate" to force this pmd to memory.
	 * Invalidating to make sure we always use what's in physical memory.
	 */
	__asm__ __volatile__ ("dhwbi %0, 0" :: "a" (pmdp));
#endif
}

struct vm_area_struct;

static inline int
ptep_test_and_clear_young(struct vm_area_struct *vma, unsigned long addr,
    			  pte_t *ptep)
{
	pte_t pte = *ptep;
	if (!pte_young(pte))
		return 0;
	update_pte(ptep, pte_mkold(pte));
	return 1;
}

static inline pte_t
ptep_get_and_clear(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	pte_t pte = *ptep;
	pte_clear(mm, addr, ptep);
	return pte;
}

static inline void
ptep_set_wrprotect(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
  	pte_t pte = *ptep;
  	update_pte(ptep, pte_wrprotect(pte));
}

/* to find an entry in a kernel page-table-directory */
#define pgd_offset_k(address)	pgd_offset(&init_mm, address)

/* to find an entry in a page-table-directory */
#define pgd_offset(mm,address)	((mm)->pgd + pgd_index(address))

#define pgd_index(address)	((address) >> PGDIR_SHIFT)

/* Find an entry in the second-level page table.. */
#define pmd_offset(dir,address) ((pmd_t*)(dir))

/* Find an entry in the third-level page table.. */
#define pte_index(address)	(((address) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pte_offset_kernel(dir,addr) 					\
	((pte_t*) pmd_page_vaddr(*(dir)) + pte_index(addr))
#define pte_offset_map(dir,addr)	pte_offset_kernel((dir),(addr))
#define pte_offset_map_nested(dir,addr)	pte_offset_kernel((dir),(addr))

#define pte_unmap(pte)		do { } while (0)
#define pte_unmap_nested(pte)	do { } while (0)


/*
 * Encode and decode a swap entry.
 *
 * Format of swap pte:
 *  bit	   0	   MBZ
 *  bit	   1	   page-file (must be zero)
 *  bits   2 -  3  page hw access mode (must be 11: _PAGE_CA_INVALID)
 *  bits   4 -  5  ring protection (must be 01: _PAGE_USER)
 *  bits   6       present == 0
 *  bits   7       swap == 1
 *  bits   8 - 10  swap type (3 bits -> 8 types)
 *  bits  11 - 31  swap offset / PAGE_SIZE (21 bits -> 8GB)
 
 * Format of file pte:
 *  bit	   0	   MBZ
 *  bit	   1	   page-file (must be one: _PAGE_RIGHTS_FILE)
 *  bits   2 -  3  page hw access mode (must be 11: _PAGE_CA_INVALID)
 *  bits   4 -  5  ring protection (must be 01: _PAGE_USER)
 *  bits   6       present = 0
 *  bits   7 - 31  file offset / PAGE_SIZE
 */

#define __swp_type(entry)	(((entry).val >> 8) & 0x1f)
#define __swp_offset(entry)	((entry).val >> 13)
#define __swp_entry(type,offs)	\
	((swp_entry_t) {((type) << 8) | ((offs) << 13) | _PAGE_SWAP | _PAGE_CA_INVALID})
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })

/*
 * The following file #defines only work if pte_present() is not true.
 */
#define PTE_FILE_MAX_BITS	25			/* 32 - 7 */
#define pte_to_pgoff(pte)	(pte_val(pte) >> 7)
#define pgoff_to_pte(off)	\
	((pte_t) { ((off) << 7) | _PAGE_FILE })

#endif /*  !defined (__ASSEMBLY__) */


#ifdef __ASSEMBLY__

/* Assembly macro _PGD_INDEX is the same as C pgd_index(unsigned long),
 *                _PGD_OFFSET as C pgd_offset(struct mm_struct*, unsigned long),
 *                _PMD_OFFSET as C pmd_offset(pgd_t*, unsigned long)
 *                _PTE_OFFSET as C pte_offset(pmd_t*, unsigned long)
 *
 * Note: We require an additional temporary register which can be the same as
 *       the register that holds the address.
 *
 * ((pte_t*) ((unsigned long)(pmd_val(*pmd) & PAGE_MASK)) + pte_index(addr))
 *
 */
#define _PGD_INDEX(rt,rs)	extui	rt, rs, PGDIR_SHIFT, 32-PGDIR_SHIFT
#define _PTE_INDEX(rt,rs)	extui	rt, rs, PAGE_SHIFT, PTRS_PER_PTE_SHIFT

/*
 * Returns &pgd_entry in mm
 */
#define _PGD_OFFSET(mm,adr,tmp)		l32i	mm, mm, MM_PGD;		\
					_PGD_INDEX(tmp, adr);		\
					addx4	mm, tmp, mm

#define _PTE_OFFSET(pmd,adr,tmp)	_PTE_INDEX(tmp, adr);		\
					srli	pmd, pmd, PAGE_SHIFT;	\
					slli	pmd, pmd, PAGE_SHIFT;	\
					addx4	pmd, tmp, pmd

#else

#define kern_addr_valid(addr)	(1)

extern  void update_mmu_cache(struct vm_area_struct * vma,
			      unsigned long address, pte_t pte);

/*
 * Mark the prot value as bypass with Write and Exec attributes/permissions.
 *
 * Added to mmap() KIO region which also maps it for the kernel
 * via static way 6.
 */
#define pgprot_noncached(prot)  __pgprot( (pgprot_val(prot) & ~(_PAGE_CA_WB)) |            \
					  (_PAGE_CA_BYPASS|_PAGE_HW_WRITE|_PAGE_HW_EXEC))

/*
 * remap a physical page `pfn' of size `size' with page protection `prot'
 * into virtual address `from'
 */

#define io_remap_pfn_range(vma,from,pfn,size,prot) \
                remap_pfn_range(vma, from, pfn, size, prot)

typedef pte_t *pte_addr_t;

/*
 * Like ARM, we provide our own arch_get_unmapped_area to cope with VIPT caches.
 */
#define HAVE_ARCH_UNMAPPED_AREA


/*  
 * __HAVE_ARCH_ENTER_LAZY_MMU_MODE:
 * These virtualization hooks might be handy for TLB/CACHE testing.
 */
#if 0
#define arch_enter_lazy_mmu_mode()      {local_flush_tlb_all(); __flush_invalidate_dcache_all(); __invalidate_icache_all(); invalidate_page_directory(); }
#define arch_leave_lazy_mmu_mode()      {local_flush_tlb_all(); __flush_invalidate_dcache_all(); __invalidate_icache_all(); invalidate_page_directory(); }
#else
#define arch_enter_lazy_mmu_mode()	do {} while (0)
#define arch_leave_lazy_mmu_mode()	do {} while (0)
#define arch_flush_lazy_mmu_mode()      do {} while (0)
#endif /* 0 */

#endif /* !defined (__ASSEMBLY__) */

#define __HAVE_ARCH_ENTER_LAZY_MMU_MODE			/* Have arch_*_lazy_mmu_mode() */
#define __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
#define __HAVE_ARCH_PTEP_GET_AND_CLEAR
#define __HAVE_ARCH_PTEP_SET_WRPROTECT
#define __HAVE_ARCH_PTEP_MKDIRTY
#define __HAVE_ARCH_PTE_SAME

#include <asm-generic/pgtable.h>

#endif /* _XTENSA_PGTABLE_H */
