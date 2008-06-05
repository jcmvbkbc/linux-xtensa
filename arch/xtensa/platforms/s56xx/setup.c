/*
 *
 * arch/xtensa/platforms/s56xx/setup.c
 *
 * Platform specific initialization.
 *
 * Authors: Chris Zankel <chris@zankel.net>
 *          Joe Taylor <joe@tensilica.com>
 *
 * Copyright 2001 - 2005 Tensilica Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/stringify.h>
#include <linux/notifier.h>

#include <asm/platform.h>
#include <asm/bootparam.h>


void __init platform_init(bp_tag_t* bootparam)
{

}

void platform_halt(void)
{
	printk (" ** Called platform_halt(), looping forever! **\n");
	while (1);
}

void platform_power_off(void)
{
	printk (" ** Called platform_power_off(), looping forever! **\n");
	while (1);
}
void platform_restart(void)
{
	/* Flush and reset the mmu, simulate a processor reset, and
	 * jump to the reset vector. */

	__asm__ __volatile__("movi	a2, 15\n\t"
			     "wsr	a2, " __stringify(ICOUNTLEVEL) "\n\t"
			     "movi	a2, 0\n\t"
			     "wsr	a2, " __stringify(ICOUNT) "\n\t"
			     "wsr	a2, " __stringify(IBREAKENABLE) "\n\t"
			     "wsr	a2, " __stringify(LCOUNT) "\n\t"
			     "movi	a2, 0x1f\n\t"
			     "wsr	a2, " __stringify(PS) "\n\t"
			     "isync\n\t"
			     "jx	%0\n\t"
			     :
			     : "a" (XCHAL_RESET_VECTOR_VADDR)
			     : "a2");

	/* control never gets here */
}

void platform_heartbeat(void)
{
}

#define __STRETCH_S5000__
#include <asm/variant/s5000/s5000.h>
#include <asm/variant/s5000/intcntl.h>
#include <asm/variant/interrupt.h>

void platform_init_irq(void)
{
	sx_intcntl_reg *int_ctrl = (sx_intcntl_reg *)SXP1_INTCNTL_BASE;

	/* Route interrupts */

	int_ctrl->config_dmac_tc[0] =
		SX_INTC_MK_CONFIG(1, S5000_INTNUM_DMAC0);
	int_ctrl->config_dmac_tc[4] =
		SX_INTC_MK_CONFIG(1, S5000_INTNUM_DMAC0);

	int_ctrl->config_uart[0] =
		SX_INTC_MK_CONFIG(1, S5000_INTNUM_UART0);
	int_ctrl->config_uart[1] =
		SX_INTC_MK_CONFIG(1, S5000_INTNUM_UART1);

	int_ctrl->config_pci_inta =
		SX_INTC_MK_CONFIG(1, S5000_INTNUM_PCIA);
	int_ctrl->config_pci_intb =
		SX_INTC_MK_CONFIG(1, S5000_INTNUM_PCIB);
	int_ctrl->config_pci_intc =
		SX_INTC_MK_CONFIG(1, S5000_INTNUM_PCIC);
	int_ctrl->config_pci_intd =
		SX_INTC_MK_CONFIG(1, S5000_INTNUM_PCID);


}

static int
s56xx_panic_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	return NOTIFY_DONE;
}

static struct notifier_block s56xx_panic_block = {
	s56xx_panic_event,
	NULL,
	0
};

void __init platform_setup(char **p_cmdline)
{
	atomic_notifier_chain_register(&panic_notifier_list, &s56xx_panic_block);
}
