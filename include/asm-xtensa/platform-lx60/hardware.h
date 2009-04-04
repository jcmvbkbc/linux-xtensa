/*
 * include/asm-xtensa/platform-lx60/hardware.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2009 Tensilica Inc.
 */

/*
 * This file contains the hardware configuration of the LX60 board.
 *
 * REMIND FIXME:
 *	While modifying open_eth.c to import it's platform variables
 *	from platform data structures declared in 
 *		arch/xtensa/platforma/lx60/setup.c
 *	it seemed to be necessary to #undef __XTENSA_LX60_HARDWARE_H below
 *	for parameters here to be accessable. Will resolve this when I update
 *	open_eth.c to use new platform data structures (Soon).
 *						-piet
 */
#undef __XTENSA_LX60_HARDWARE_H	
#ifndef __XTENSA_LX60_HARDWARE_H
#define __XTENSA_LX60_HARDWARE_H
#include <asm/processor.h>
#include <platform/system.h>

/* By default NO_IRQ is defined to 0 in Linux, but we use the
   interrupt 0 for UART... */
#define NO_IRQ                 -1

/* Memory configuration. */

#define PLATFORM_DEFAULT_MEM_START 0x00000000
#define PLATFORM_DEFAULT_MEM_SIZE  0x04000000

/* Interrupt configuration. */

#define PLATFORM_NR_IRQS	2

/* 
 * Default assignment of LX60 devices to external interrupts. 
 *
 *   CONFIG_ARCH_HAS_SMP means the Hardware supports SMP, ie: a MX
 *
 *   CONFIG_SMP means the OS is configured for SMP. 
 *
 *   CONFIG_ARCH_HAS_SMP without CONFIG_SMP means to run
 *   without SMP on hardware that supports it.
 */

/*  UART interrupt: */
#ifdef CONFIG_ARCH_HAS_SMP
#define DUART16552_INTNUM	3
#else
#define DUART16552_INTNUM	0
#endif

/*  Ethernet interrupt:  */
#ifdef CONFIG_ARCH_HAS_SMP
#define OETH_IRQ                4
#else
#define OETH_IRQ                1
#endif
#define OETH_REQUEST_IRQ_FLAG   0
/*
 *  Device addresses and parameters.
 */

/* UART crystal frequency in Hz */
#define DUART16552_XTAL_FREQ	(CONFIG_XTENSA_CPU_CLOCK * 1000)

/* UART */
#define DUART16552_VADDR	(XSHAL_IOBLOCK_BYPASS_VADDR+0xD050020)
/* LCD instruction and data addresses. */
#define LCD_INSTR_ADDR		(char*)(XSHAL_IOBLOCK_BYPASS_VADDR + 0xD040000)
#define LCD_DATA_ADDR		(char*)(XSHAL_IOBLOCK_BYPASS_VADDR + 0xD040004)
#define DIP_SWITCHES_ADDR	(XSHAL_IOBLOCK_BYPASS_VADDR+0xD02000C)

/*  Opencores Ethernet controller:  */
#define OETH_BASE_ADDR		(XSHAL_IOBLOCK_BYPASS_VADDR+0xD030000)
#define OETH_SRAM_BUFF_BASE	(XSHAL_IOBLOCK_BYPASS_VADDR+0xD800000)
#define OETH_BASE_IO_ADDR	IOADDR(0xD030000)

/* MAC registers + RX and TX descriptors */
#define OETH_REGS_SIZE  	0x1000  

/* The rest of the parameters for the Opencores Ethernet Controller. */
#define OETH_RXBD_NUM           5
#define OETH_TXBD_NUM	        5

#define OETH_RX_BUFF_SIZE	0x600
#define OETH_TX_BUFF_SIZE	0x600

/* The MAC address of the controller. The one here corresponds to:
   00:50:c2:13:6f:xx. The last byte is read from the DIP switches on
   the board. */
#define OETH_MACADDR0           0x00
#define OETH_MACADDR1           0x50
#define OETH_MACADDR2           0xc2
#define OETH_MACADDR3           0x13
#define OETH_MACADDR4           0x6f

/* The ID for the PHY device. Use auto-detection if not defined. */
#if 0
#ifdef CONFIG_ARCH_HAS_SMP
#define OETH_PHY_ID             3
#else
#define OETH_PHY_ID             0
#endif /* CONFIG_ARCH_HAS_SMP */
#endif

#define OETH_PLATFORM_SPECIFIC_INIT(regs)				\
do {									\
    	/* Set the clock divider to 2 (50MHz / 2) */			\
	regs->miimoder = (OETH_MIIMODER_CLKDIV & 0x2);			\
									\
	/* Tell the PHY to turn on the activity LED. */			\
	regs->miiaddress = 0x1b;					\
	regs->miitx_data = 0xce;					\
	regs->miicommand = OETH_MIICOMMAND_WCTRLDATA;			\
	{								\
	  int i;							\
	  volatile int v;						\
	  for (i = 256; i >= 0; i--){					\
	    v = regs->miistatus;					\
	    if (!(v & 2))						\
	      break;							\
	  }								\
	  printk("dann: write control data i=%d\n", i);			\
	}								\
									\
	regs->mac_addr1 = OETH_MACADDR0 << 8 | OETH_MACADDR1;		\
	regs->mac_addr0 = OETH_MACADDR2 << 24 | OETH_MACADDR3 << 16	\
		| OETH_MACADDR4 << 8 | *(u32*)DIP_SWITCHES_ADDR;	\
									\
} while (0)

#endif /* __XTENSA_LX60_HARDWARE_H */
