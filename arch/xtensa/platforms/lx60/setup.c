/*
 *
 * arch/xtensa/platform-lx60/setup.c
 *
 * ...
 *
 * Authors:	Chris Zankel <chris@zankel.net>
 *		Joe Taylor <joe@tensilica.com>
 *		Pete Delaney <piet@tensilica.com>
 * 
 * Copyright 2001 - 2009 Tensilica Inc.
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
#include <linux/platform_device.h>
#include <asm//timex.h>

#include <../drivers/net/open_eth.h>

#include <asm/processor.h>
#include <asm/platform.h>
#include <asm/bootparam.h>
#include <platform/lcd.h>
#include <platform/hardware.h>
#include <variant/core.h>

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

#ifdef CONFIG_XTENSA_CALIBRATE_CCOUNT
/* 
 * Called from time_init(); more that just calibrating, 
 * completly establishes the clock rate from the Board
 * specific FPGA registers. See section 4.2.5 of Avnet
 * LX200 Users guide.
 */
void platform_calibrate_ccount(void)
{
  long *clk_freq_p = (long *)(XTBOARD_FPGAREGS_PADDR + XTBOARD_CLKFRQ_OFS);
  long  clk_freq = *clk_freq_p;

  ccount_per_jiffy = clk_freq/HZ;
  nsec_per_ccount = 1000000000UL/clk_freq;
}
#endif

void platform_restart(void)
{
  /* XTBOARD_SWRST_REG = XTBOARD_SWRST_RESETVALUE; */
  *(volatile unsigned *)0xfd020010 = 0xdead;
}

struct oeth_platform_data oeth_platform_data[] = {
	{
		.tx_bd_num = OETH_TXBD_NUM,
		.rx_bd_num = OETH_RXBD_NUM,
		.tx_buf_size = OETH_TX_BUFF_SIZE,
		.rx_buf_size = OETH_RX_BUFF_SIZE,
#ifdef OETH_PHY_ID
		.phy_id = OETH_PHY_ID,
#else
		.phy_id = 3,
#endif
	},
};
struct resource lx60_oeth_resource[] = {
	{
		.start = OETH_BASE_IO_ADDR,
		.end   = OETH_BASE_IO_ADDR + OETH_REGS_SIZE,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = OETH_IRQ,
		.end = OETH_IRQ,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device lx60_oeth_platform_device = {
	.name           = "oeth-mac",
	.num_resources  = ARRAY_SIZE(lx60_oeth_resource),
	.resource	= lx60_oeth_resource,
	.id             = -1,
	.dev = {
		.platform_data = oeth_platform_data,
	},
};

/* very early init */
void __init platform_setup(char **cmdline)
{
	if (cmdline) {
		if (cmdline[0])
			printk("lx60 %s(cmdline[0]:'%s')\n", __func__, cmdline[0]);
		else
			printk("lx60 %s(void)\n", __func__);
	}
}

/* early initialization, before secondary cpu's have been brought up */

void platform_init(bp_tag_t *bootparams)
{
	if( bootparams )
		printk("lx60 %s(bootparams:%p)\n", __func__, bootparams);
}

static int lx60_init(void)
{
	int ret;

	printk("%s()\n", __func__);

	ret = platform_device_register(&lx60_oeth_platform_device);

	return(ret);
}

/*
 * Register to be done during do_initcalls(), basic system is 
 */
arch_initcall(lx60_init);

/* Heartbeat. */

void platform_heartbeat(void)
{
}


