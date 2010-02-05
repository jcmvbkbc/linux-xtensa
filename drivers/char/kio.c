/*
 * drivers/char/kio.c
 *
 * Xtensa KIO Driver - Provides for the mapping of KIO segment.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008-2010 Tensilica Inc.
 *
 * Pete Delaney <piet.delaney@tensilica.com
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include <linux/skbuff.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>


int kio_open( struct inode *inode, struct file *filp )
{
    return 0;
}

int kio_read( struct file *filp, char *buff, size_t size, loff_t *offset)
{
	return 0;	
}

int kio_close( struct inode *inode, struct file *filp )
{
    return 0;
}

int kio_mmap_bypass_enabled = 1;	/* DEBUG switch to enable with xt-gdb */

int kio_mmap( struct file *filp, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long pfn;
	unsigned long paddr;
	unsigned long paddr0;

	if (size > XCHAL_KIO_SIZE)
		return -ENXIO;

	if ((vma->vm_flags & VM_WRITE) && !(vma->vm_flags & VM_SHARED))
		return(-EINVAL);

	/* 
	 * remap will work here as we marked all pages as RESERVED in 
	 * buddy_area_init()
	 */
	vma->vm_flags |= VM_RESERVED;

	/* 
 	 * REMIND: /home/marc/work/bo/p4root/Xtensa/OS/boards/xtensa/xtav60/xtensa/xtav60.h 
	 *         where did it go?
 	 */
#	define XTBOARD_CLKFRQ_OFS 	0x04   /* clock frequency Hz (read-only) */
#	define XTBOARD_SYSLED_OFS      	0x08    /* LEDs */

#	define XSHAL_IOBLOCK_BYPASS_PADDR  XCHAL_KIO_PADDR				/* 0xf0000000 */
#if 0
#	define XTBOARD_FPGAREGS_PADDR 	   (XSHAL_IOBLOCK_BYPASS_PADDR+0x0D020000)	/* 0xfd020000 */
#endif
#	define BOOT_SRAM_PADDR            (XSHAL_IOBLOCK_BYPASS_PADDR+0x0D400000) 	/* 0xfd040000: Boot 128K Sram address */
#	define UART16550_PADDR            (XSHAL_IOBLOCK_BYPASS_PADDR+0x0D050000)
# 	define ETHERNET_BUFFER_PADDR      (XSHAL_IOBLOCK_BYPASS_PADDR+0x0D800000)
#	define ETHERNET_BUFFER_SIZE	  (16*1024)					/* From LX200 User Guide */
# 	define AUDIO_PADDR                (XSHAL_IOBLOCK_BYPASS_PADDR+0x0D070000)
# 	define XTBOARD_FLASH_CACHED_PADDR (XSHAL_IOBLOCK_BYPASS_PADDR+0x08000000)
#	define XTBOARD_SYSLED_PADDR    	  (XTBOARD_FPGAREGS_PADDR+XTBOARD_SYSLED_OFS)	/* Didn't seem to be writable */

	/* The rest of the parameters for the Opencores Ethernet Controller. */
#	define OETH_RXBD_NUM           5
#	define OETH_TXBD_NUM           5

#	define OETH_RX_BUFF_SIZE       0x600
#	define OETH_TX_BUFF_SIZE       0x600

#	define OETH_BUFFERS_SIZE	((OETH_RXBD_NUM * OETH_RX_BUFF_SIZE) + (OETH_TXBD_NUM * OETH_TX_BUFF_SIZE))
#	define MIN(x,y) ((x) < (y) ? (x) : (y))



	/*
	 * Test with memory at the end of the Buffer space not currently
	 * being used by the OpenEthernet driver. See:
	 *	include/asm-xtensa/platform-lx60/hardware
	 *	drivers/net/open_eth.c
	 */
	paddr0 = ETHERNET_BUFFER_PADDR + OETH_BUFFERS_SIZE;
        pfn = paddr0 >> PAGE_SHIFT;
	paddr = pfn << PAGE_SHIFT;
	size = MIN(size, ((paddr0 - paddr) + (ETHERNET_BUFFER_SIZE - OETH_BUFFERS_SIZE)));

	printk("%s: paddr:0x%lx; pfn:0x%lx, size:0x%lx:%ld\n", __func__, paddr, pfn, size, size);

	if (kio_mmap_bypass_enabled) {
		/* Change vma to be uncached rwx; attribute:0x3 */
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	}
	printk("%s: vma->vm_page_prot.pgprot:%lx\n", __func__, vma->vm_page_prot.pgprot);

        if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot))
		return -EAGAIN;
					
	return(0);
}

/* Referenced by mem.c */
struct file_operations kio_fops =
{
    mmap:       kio_mmap,
    open:       kio_open,
    release:    kio_close,
    read:	kio_read,
};

#if 0
/* 
 * This Doesn't seem to work any londer; letting mem.c register our minor dev num.
 */
void kio_init(void)
{
	int major;

	/* Use mknod to make /dev/kio; TODO Make node automatically */
	major = register_chrdev(101,"kio", &kio_fops);
	if (major < 0) {
	    printk(KERN_ERR "kio: can't get major 101 error = %d\n", major);
	}
	else {
		printk("KIO: Successfully initialized kio device\n");
	}
}
#endif


