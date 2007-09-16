/*
 *
 * arch/xtensa/platform-lx60/setup.c
 *
 * ...
 *
 * Authors:	Chris Zankel <chris@zankel.net>
 *		Joe Taylor <joe@tensilica.com>
 * 
 * Copyright 2001 - 2006 Tensilica Inc.
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
#include <linux/kdev_t.h>
#include <linux/types.h>
#include <linux/major.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/stringify.h>
/* #include <linux/platform_device.h> */


#include <asm/processor.h>
#include <asm/platform.h>
#include <asm/bootparam.h>
//#include <asm/hardware.h>
#include <asm/platform/lcd.h>

void platform_halt(void)
{
	lcd_disp_at_pos(" HALT ", 0);
	local_irq_disable();
	while (1);
}

void platform_power_off(void)
{
        lcd_disp_at_pos ("POWEROFF", 0);
	local_irq_disable();
	while (1);
}

void platform_restart(void)
{
	/* Flush and reset the mmu, simulate a processor reset, and
	 * jump to the reset vector. */
  
  
	__asm__ __volatile__ ("movi	a2, 15\n\t"
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
			      : "a2"
			      );

	/* control never gets here */
}

void __init platform_setup(char** cmdline)
{
       //lcd_init();
	//lcd_disp_at_pos("Xtensa Linux!!                           You know you want it!", 0);

}

/* static struct resource open_eth_resource[] = { */
/* 	{ */
/* 		.start	= OETH_BASE_ADDR, */
/* 		.end	= OETH_BASE_ADDR + 0x1000, */
/* 		.flags	= IORESOURCE_MEM, */
/* 	}, { */
/* 		.start	= OETH_SRAM_BUFF_BASE, */
/* 		.end	= OETH_SRAM_BUFF_BASE + 0xFFFFFF, */
/* 		.flags	= IORESOURCE_MEM, */
/* 	}, */
/* 	, { */
/* 		.start	= OETH_IRQ, */
/* 		.end	= OETH_IRQ, */
/* 		.flags	= IORESOURCE_IRQ */
/* 	}, { */
/* 		.start	= OETH_RX_BUFF_SIZE, */
/* 		.end	= OETH_SRAM_BUFF_BASE + 0xFFFFFF, */
/* 		.flags	= IORESOURCE_MEM, */
/* 	} */
/* }; */


/* early initialization */

void platform_init(bp_tag_t* first)
{
}

/* Heartbeat. */

void platform_heartbeat(void)
{
#if 0
	static int i=0, t = 0;
	if (--t < 0)
	{
		t = 59;
		if (1) {
			extern int dann_int_count,  dann_rx_count, dann_tx_count;
			static char locbuff[256];
			static int c = 0;
			sprintf (locbuff, "c=%5d I=%5x                         T=%5x R=%5x", c, dann_int_count, dann_tx_count, dann_rx_count);
			lcd_disp_at_pos (locbuff, 0);
			c++;
		}
/* 		lcd_shiftleft(); */
		i ^= 1;
	}
#endif
}
