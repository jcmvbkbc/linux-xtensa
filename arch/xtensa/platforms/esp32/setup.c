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

#ifdef DEBUG
#if 1
void dbg_echo(const char *s)
{
	//volatile void __iomem *base = (volatile void __iomem *)0x3ff40000;
	volatile void __iomem *base = (volatile void __iomem *)0x60000000;

	while ((readl(base + UART_STATUS_REG) & UART_TXFIFO_CNT_MASK) != 0)
		;

	while (*s) {
		if (*s == '\n')
			writel('\r', base + UART_FIFO_REG);
		writel(*s, base + UART_FIFO_REG);
		++s;
	}
}
#else
void dbg_echo(const char *s)
{
	volatile void __iomem *base = (volatile void __iomem *)0x60038000;

	while (!(readl(base + USB_SERIAL_JTAG_EP1_CONF_REG) &
		 USB_SERIAL_JTAG_SERIAL_IN_EP_DATA_FREE_MASK))
		;

	while (*s) {
		if (*s == '\n')
			writel('\r', base + USB_SERIAL_JTAG_EP1_REG);
		writel(*s, base + USB_SERIAL_JTAG_EP1_REG);
		++s;
	}
}
#endif

void dbg_printf(const char *fmt, ...)
{
	va_list ap;
	char buf[256];

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	dbg_echo(buf);
}
#endif

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
