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
  /* XTBOARD_SWRST_REG = XTBOARD_SWRST_RESETVALUE; */
  *(volatile unsigned *)0xfd020010 = 0xdead;
}

/* SMP */

#ifdef CONFIG_SMP
static __init void smp_init_cpus(void)
{
	unsigned int i;
	unsigned int ncpus = 1;

	for (i = 0; i < ncpus; i++) {
		cpu_set(i, cpu_present_map);
		cpu_set(i, cpu_possible_map);
	}
}

__init int platform_boot_secondary(unsigned int cpu, struct task_struct *ts)
{
	printk("start secondary\n");
	return -1;
}
#endif
void __init platform_setup(char** cmdline)
{
#ifdef CONFIG_SMP
	smp_init_cpus();
#endif
       //lcd_init();
	//lcd_disp_at_pos("Xtensa Linux!!                           You know you want it!", 0);

}

/* early initialization */

void platform_init(bp_tag_t* first)
{
}

/* Heartbeat. */

void platform_heartbeat(void)
{
}


