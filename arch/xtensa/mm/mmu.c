/*
 * xtensa mmu stuff
 *
 * Extracted from init.c
 */
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/cache.h>

#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/page.h>

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

void __init paging_init(void)
{
	memset(swapper_pg_dir, 0, PAGE_SIZE);
}

/*
 * Flush the mmu and reset associated register to default values.
 */
void __init init_mmu(void)
{
#if XCHAL_HAVE_PTP_MMU && XCHAL_HAVE_SPANNING_WAY
	/* 
	 * We have a V3 MMU, the TLB was initialized with  virtual == physical 
	 * mappings on a hardware reset. This was done by the hardware by 
	 * presetting idenity mapping in Way 6:
	 *
         *  vaddr=0x00000000 asid=0x01  paddr=0x00000000  ca=3  ITLB way 6 (512 MB)
         *  vaddr=0x20000000 asid=0x01  paddr=0x20000000  ca=3  ITLB way 6 (512 MB)
         *  vaddr=0x40000000 asid=0x01  paddr=0x40000000  ca=3  ITLB way 6 (512 MB)
         *  vaddr=0x60000000 asid=0x01  paddr=0x60000000  ca=3  ITLB way 6 (512 MB)
         *  vaddr=0x80000000 asid=0x01  paddr=0x80000000  ca=3  ITLB way 6 (512 MB)
         *  vaddr=0xa0000000 asid=0x01  paddr=0xa0000000  ca=3  ITLB way 6 (512 MB)
         *  vaddr=0xc0000000 asid=0x01  paddr=0xc0000000  ca=3  ITLB way 6 (512 MB)
         *  vaddr=0xe0000000 asid=0x01  paddr=0xe0000000  ca=4  ITLB way 6 (512 MB)
	 * 
	 * The reset vector code remapped KSEG (0xD000000) to map physical memory 
	 * in way 5 and changed the page size to in way 6 to 256 MB by setting the 
	 * TLB config register, It removed the (virtual == physical) mappings
	 * by setting the ASID fields to zero in way 6 and set up the KIO mappings;
	 * Un-Cached at 0xF0000000 and Cached at 0xE000000.
	 *
	 * Way 5
	 *   vaddr=0x40000000 asid=0x00  paddr=0xf8000000  ca=3  ITLB way 5 (128 MB)
	 *   vaddr=0x08000000 asid=0x00  paddr=0x00000000  ca=0  ITLB way 5 (128 MB)
	 *   vaddr=0xd0000000 asid=0x01  paddr=0x00000000  ca=7  ITLB way 5 (128 MB)
	 *   vaddr=0xd8000000 asid=0x01  paddr=0x00000000  ca=3  ITLB way 5 (128 MB)
	 *
	 * Way 6
	 *   vaddr=0x00000000 asid=0x00  paddr=0x00000000  ca=3  ITLB way 6 (256 MB)
	 *   vaddr=0x10000000 asid=0x00  paddr=0x20000000  ca=3  ITLB way 6 (256 MB)
	 *   vaddr=0x20000000 asid=0x00  paddr=0x40000000  ca=3  ITLB way 6 (256 MB)
	 *   vaddr=0x30000000 asid=0x00  paddr=0x60000000  ca=3  ITLB way 6 (256 MB)
	 *   vaddr=0x40000000 asid=0x00  paddr=0x80000000  ca=3  ITLB way 6 (256 MB)
	 *   vaddr=0x50000000 asid=0x00  paddr=0xa0000000  ca=3  ITLB way 6 (256 MB)
	 *   vaddr=0xe0000000 asid=0x01  paddr=0xf0000000  ca=7  ITLB way 6 (256 MB)
	 *   vaddr=0xf0000000 asid=0x01  paddr=0xf0000000  ca=3  ITLB way 6 (256 MB)
	 * 
	 *   See arch/xtensa/boot/boot-elf/bootstrap for details.
	 */
#else
	/* 
	 * Writing zeros to the instruction and data TLBCFG special 
	 * registers ensure that valid values exist in the register.  
	 *
	 * For existing PGSZID<w> fields, zero selects the first element 
	 * of the page-size array.  For nonexistent PGSZID<w> fields, 
	 * zero is the best value to write.  Also, when changing PGSZID<w>
	 * fields, the corresponding TLB must be flushed.
	 */
	set_itlbcfg_register(0);
	set_dtlbcfg_register(0);
#endif

	flush_tlb_all();	/* Flush the Auto-Refill TLB Ways (0...3) */

	/* Set rasid register to a known value. */

	set_rasid_register(ASID_INSERT(ASID_USER_FIRST));

	/* Set PTEVADDR special register to the start of the page
	 * table, which is in kernel mappable space (ie. not
	 * statically mapped).  This register's value is undefined on
	 * reset.
	 */
	set_ptevaddr_register(PGTABLE_START);
}

struct kmem_cache *pgtable_cache __read_mostly;

static void pgd_ctor(void *addr)
{
	pte_t *ptep = (pte_t *)addr;
	int i;

	for (i = 0; i < 1024; i++, ptep++)
		pte_clear(NULL, 0, ptep);

}

void __init pgtable_cache_init(void)
{
	pgtable_cache = kmem_cache_create("pgd",
			PAGE_SIZE, PAGE_SIZE,
			SLAB_HWCACHE_ALIGN,
			pgd_ctor);
}
