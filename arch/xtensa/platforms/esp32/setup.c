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

void __init platform_setup(char **p)
{
	volatile void __iomem *base;
	u32 v;

#if 0
	v = readl(base + 0xbc);
	pr_info("RTC_CNTL_RTC_SW_CPU_STALL_REG = %08x\n", v);
	writel((v & ~0x03f00000) | 0x02100000, base);

	v = readl(base);
	pr_info("RTC_CNTL_RTC_OPTIONS0_REG = %08x\n", v);
	writel((v & ~3) | 2, base);

	base = (volatile void __iomem *)0x600c0000;
	v = readl(base);
	pr_info("SYSTEM_CORE_1_CONTROL_0_REG = %08x\n", v);
	writel(v | 1, base);
#endif
}
