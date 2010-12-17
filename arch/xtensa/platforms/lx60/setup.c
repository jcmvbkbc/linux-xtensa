/*
 *
 * arch/xtensa/platforms/xtavnet/setup.c
 *
 * ...
 *
 * Authors:	Chris Zankel <chris@zankel.net>
 *		Joe Taylor <joe@tensilica.com>
 *		Pete Delaney <piet@tensilica.com>
 * 
 * Copyright 2001 - 2010 Tensilica Inc.
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
#include <asm/vectors.h>

enum xtensa_board  platform_board  = AVNET_UNKNOWN;
char	  	  *platform_board_name = "<<UNKNOWN>>";
unsigned int       platform_mem_start = PLATFORM_DEFAULT_MEM_START;
unsigned int       platform_mem_size =  PLATFORM_DEFAULT_MEM_SIZE;


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

/*
 * This information is used to probe the memory
 * of the current board to find out which board
 * we are running on. We start from the largest
 * possability and work down to the smallest.
 */
struct board_info_s {
		unsigned int 	 himem;
		enum 		 xtensa_board board;
		char 		*board_name;
};
struct board_info_s  board_info[3] = {
	{ .himem = (unsigned int) ((96 * 1024 * 1024) - 1),
	  .board = AVNET_LX200,
	  .board_name = "AVNET_LX200"
	},
	{ .himem = (unsigned int) ((64 * 1024 * 1024) - 1),
	  .board = AVNET_LX60,
	  .board_name = "AVNET_LX60"
	},
	{ .himem = (unsigned int) ((48 * 1024 * 1024) - 1),
	  .board = AVNET_LX110,
	  .board_name = "AVNET_LX110"
	}
};

static int bus_errors = 0;

void __init
probe_exception_handler(struct pt_regs *regs, unsigned long exccause)
{
	regs->pc += 3;                  /* skip the l8ui instruction */
	bus_errors++;			/* Feels dangerious */
}

/* 
 * very early initialization, before secondary cpu's have been brought up,
 * but also called by secondaary processors but there is nothing we need 
 * to do for them.
 */

void platform_init(bp_tag_t *bootparams)
{
	extern void trap_init(void);
	extern void *trap_set_early_C_handler(int cause, void *handler);
	extern void *trap_initialize_early_exc_table(void);
	extern void trap_enable_early_exc_table(void);
	unsigned char *ptr;
	unsigned char saved_byte;
	struct board_info_s *bi;
	void *saved_bus_exception_hander_addr;
	void *saved_data_exception_hander_addr;
	void *saved_addr_exception_hander_addr;
	int board;
	int cpu = 0;

#ifdef CONFIG_SMP
	/* 
 	 * Only the primary CPU needs to determine which Avnet board we are running on.
 	 * We assume with SMP that the PRID register is supported.
 	 */
	cpu = get_sr(PRID);
	if (cpu != 0) 
		goto done; 
#endif

	/*
	 * We let the primary CPU Determine the Avnet Board 
	 * from it's memory size.  Access end of each boards memory, 
	 * starting with the largest. If we succeed we assume
	 * the current selection is our board.
	 *
	 * Hopefully soon this will be unnecessary and
	 * will be available from the boards FPGA registers.
	 *
	 * $excsave1 was set up pointing to the Trap Table
	 * just a bit earlier in arch_init(); so it's now
	 * save to take an excpetion.
	 *
	 * Using Early Exception Handler Table till per_cpu code knows how many
	 * CPU's are being brought on line and we can initialized the final tables.
	 */ 
	trap_initialize_early_exc_table();	/* Set default exception handlers; including C (Default) handeler */
	saved_bus_exception_hander_addr  = trap_set_early_C_handler(EXCCAUSE_LOAD_STORE_ERROR, probe_exception_handler);	/* 03 */
	saved_data_exception_hander_addr = trap_set_early_C_handler(EXCCAUSE_LOAD_STORE_DATA_ERROR, probe_exception_handler);	/* 13 */
	saved_addr_exception_hander_addr = trap_set_early_C_handler(EXCCAUSE_LOAD_STORE_ADDR_ERROR, probe_exception_handler);	/* 15 */
	trap_enable_early_exc_table();		/* Enable Exception Table usage by setting excsave1 register */

	for (board = 0; board < 3; board++) {
		bus_errors = 0;
		bi = &board_info[board];
		ptr = (char *) (bi->himem | 0xD8000000);	/* Uncached memory access */
		saved_byte = *ptr;
		if (bus_errors)					/* Set if we got an exception */
			continue;
		*ptr = (char) 0xbabecafe;
		if (*ptr != (char) 0xbabecafe) {
			*ptr = saved_byte;
			ptr = NULL;
			continue;
		}
		*ptr = (char ) 0xdeadbeef;
		if (*ptr != (char) 0xdeadbeef) {
			*ptr = saved_byte;
			ptr = NULL;
			continue;
		}
		if (bus_errors) 
			continue;

		break;						/* Memory seems to exist for this board */
	}
	trap_set_early_C_handler(EXCCAUSE_LOAD_STORE_ADDR_ERROR, saved_bus_exception_hander_addr);	
	trap_set_early_C_handler(EXCCAUSE_LOAD_STORE_ADDR_ERROR, saved_data_exception_hander_addr);
	trap_set_early_C_handler(EXCCAUSE_LOAD_STORE_ADDR_ERROR, saved_addr_exception_hander_addr);

	if (ptr) {
		platform_mem_size = bi->himem + 1;
		platform_board = bi->board;
		platform_board_name = bi->board_name;
	} else {
		/* 
  		 * System will likely come up if we use the smallest known memory.
  		 * Perhaps better to hang here or do a BREAK instruction.
 		 */
		platform_mem_size = (unsigned int) ((48 * 1024 * 1024) - 1);
		platform_board = AVNET_UNKNOWN;
		platform_board_name = "<<Unknown Avnet Board>> [FIXME]";
	}

	/*
	 * Initialize sysmem in our platform code.
	 */ 
	if (sysmem.nr_banks == 0) {
		sysmem.nr_banks = 1;
		sysmem.bank[0].start = PLATFORM_DEFAULT_MEM_START;
		sysmem.bank[0].end = PLATFORM_DEFAULT_MEM_START + platform_mem_size;
	}
	printk("%s: sysmem.bank[0].{start = 0x%lx, end = 0x%lx}\n", __func__, 
		    sysmem.bank[0].start, sysmem.bank[0].end);
	
	printk("%s(bootparams:%p): cpu:%d, platform_board:%d:'%s, platform_mem_size:0X%x:%d\n", __func__, 
		   bootparams,     cpu,    platform_board,        
				           platform_board_name,   platform_mem_size, platform_mem_size);
}

/* early initialization, after platform_init() */
void __init platform_setup(char **cmdline)
{
	if (cmdline) {
		if (cmdline[0])
			printk("%s(cmdline[0]:'%s'): platform_board:%d:'%s'\n", __func__, 
				   cmdline[0],       platform_board, platform_board_name);
		else
			printk("%s(void): platform_board:%d:'%s'\n", __func__, 
					  platform_board, platform_board_name);
	}
}


static int xtavnet_init(void)
{
	int ret;

	printk("%s(): Registering Platform Devices.\n", __func__);

	ret = platform_device_register(&lx60_oeth_platform_device);

	return(ret);
}

/*
 * Register to be done during do_initcalls(), basic system is 
 */
arch_initcall(xtavnet_init);

/* Heartbeat. */

long platform_heartbeats = 0;

void platform_heartbeat(void)
{
#if 0
	if ((platform_heartbeats++ & 0X3F) == 0) {
		if ((platform_heartbeats++ & 0X40) == 1)
			lcd_disp_at_pos("*", 12);
		else
			lcd_disp_at_pos(" ", 12);
	}
#endif
}


