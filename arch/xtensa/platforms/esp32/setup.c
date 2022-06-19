// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>

#include <asm/bootparam.h>
#include <asm/platform.h>
#include <asm/processor.h>
#include <asm/timex.h>
#include <asm/delay.h>

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

static void kick_watchdog(void)
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

void platform_heartbeat(void)
{
	kick_watchdog();
}

static void watchdog_cb(struct timer_list *timer)
{
	kick_watchdog();
	mod_timer(timer, jiffies + HZ);
}

static DEFINE_TIMER(watchdog_timer, watchdog_cb);

#ifdef CONFIG_XTENSA_CALIBRATE_CCOUNT

void __init platform_calibrate_ccount(void)
{
	ccount_freq = 0;
}

#endif

#define RTCIO_PAD_PDAC1_DRV_SHIFT 30
#define RTCIO_PAD_PDAC1_DAC_SHIFT 19
#define RTCIO_PAD_PDAC1_XPD_DAC_MASK BIT(18)
#define RTCIO_PAD_PDAC1_MUX_SEL_MASK BIT(17)
#define RTCIO_PAD_PDAC1_DAC_XPD_FORCE BIT(10)

#define RTCIO_PAD_PDAC1_FUNC_DAC_DEFAULT \
	((2 << RTCIO_PAD_PDAC1_DRV_SHIFT) | \
	 RTCIO_PAD_PDAC1_XPD_DAC_MASK | \
	 RTCIO_PAD_PDAC1_MUX_SEL_MASK | \
	 RTCIO_PAD_PDAC1_DAC_XPD_FORCE)

#define PERIOD (1000000000 / 22050)

static struct timer_data {
	struct hrtimer timer;
	u8 *data;
	u32 index;
	u32 size;
	ktime_t prev;
} timer_data;

static enum hrtimer_restart timer_cb(struct hrtimer *timer)
{
	struct timer_data *p = &timer_data;
	ktime_t now = ktime_get();

	hrtimer_forward(timer, now, PERIOD);
	if (p->prev != 0) {
		while (p->prev < now) {
			++p->index;
			p->prev += PERIOD;
		}
	} else {
		p->prev = now;
	}
	if (p->index < p->size) {
		volatile void __iomem *rtc_base;

		rtc_base = (volatile void __iomem *)0x3FF48400; /* RTC IO MUX */
		writel(RTCIO_PAD_PDAC1_FUNC_DAC_DEFAULT |
		       (p->data[p->index] << RTCIO_PAD_PDAC1_DAC_SHIFT),
		       rtc_base + 0x84);
		return HRTIMER_RESTART;
	}
	return HRTIMER_NORESTART;
}

static long prepare_file(struct timer_data *p, const char *name)
{
	struct file *file;
	loff_t pos;
	ssize_t rd;

	file = filp_open(name, O_RDONLY, 0);
	if (IS_ERR(file)) {
		pr_err("filp_open(%s): %ld\n", name, PTR_ERR(file));
		return PTR_ERR(file);
	}

	p->size = 100000;
	p->data = kzalloc(p->size, GFP_KERNEL);
	if (!p->data) {
		pr_err("no memory\n");
		fput(file);
		return -ENOMEM;
	}
	pos = 0;
	rd = kernel_read(file, p->data, p->size, &pos);
	pr_info("%s: rd %zd\n", __func__, rd);
	fput(file);
	return 0;
}

static void play(struct timer_list *timer)
{
	if (prepare_file(&timer_data, "/test.u8") != 0) {
		mod_timer(timer, jiffies + 10);
		return;
	}
	hrtimer_init(&timer_data.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_HARD);
	timer_data.timer.function = timer_cb;
	hrtimer_start(&timer_data.timer, PERIOD, HRTIMER_MODE_REL_HARD);
}

static DEFINE_TIMER(retry_timer, play);

static int __init machine_setup(void)
{
	volatile void __iomem *rtc_base;
	volatile void __iomem *sens_base;
	u32 v1, v2;

	watchdog_cb(&watchdog_timer);

	rtc_base = (volatile void __iomem *)0x3FF48400; /* RTC IO MUX */
	sens_base = (volatile void __iomem *)0x3FF48800; /* Sensors */

	v1 = readl(rtc_base + 0x00);
	v2 = readl(rtc_base + 0x0c);
	pr_info("RTCIO_RTC_GPIO_OUT_REG = 0x%08x, RTCIO_RTC_GPIO_ENABLE_REG = 0x%08x\n", v1, v2);
	v1 = readl(sens_base + 0x98);
	v2 = readl(sens_base + 0x9c);
	pr_info("SENS_SAR_DAC_CTRL1_REG = 0x%08x, SENS_SAR_DAC_CTRL2_REG = 0x%08x\n", v1, v2);

	writel(RTCIO_PAD_PDAC1_FUNC_DAC_DEFAULT, rtc_base + 0x84);
	writel(0, sens_base + 0x9c);

	play(&retry_timer);

	return 0;
}
late_initcall(machine_setup);
