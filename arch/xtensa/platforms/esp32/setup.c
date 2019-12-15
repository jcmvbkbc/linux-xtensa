// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/printk.h>

#include <asm/bootparam.h>
#include <asm/platform.h>
#include <asm/processor.h>
#include <asm/timex.h>

void platform_halt(void)
{
	local_irq_disable();
	while (1)
		cpu_relax();
}

void platform_power_off(void)
{
	local_irq_disable();
	while (1)
		cpu_relax();
}

void platform_restart(void)
{
	/* Flush and reset the mmu, simulate a processor reset, and
	 * jump to the reset vector. */
	cpu_reset();
	/* control never gets here */
}

void platform_heartbeat(void)
{
	volatile void __iomem *base;

	base = (volatile void __iomem *)0x3FF48000;
	writel(0x050D83AA1, base + 0xa4);
	writel(1, base + 0xa0);
	writel(0, base + 0xa4);

	base = (volatile void __iomem *)0x3FF5F000;
	writel(0x050D83AA1, base + 0x64);
	writel(1, base + 0x60);
	writel(0, base + 0x64);

	base = (volatile void __iomem *)0x3FF60000;
	writel(0x050D83AA1, base + 0x64);
	writel(1, base + 0x60);
	writel(0, base + 0x64);
}

#ifdef CONFIG_XTENSA_CALIBRATE_CCOUNT

void __init platform_calibrate_ccount(void)
{
	ccount_freq = 0;
}

#endif
