/*
 * arch/xtensa/kernel/time.c
 *
 * Timer and clock support.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005 - 2009 Tensilica Inc.
 *
 * Chris Zankel <chris@zankel.net>
 */

#include <linux/errno.h>
#include <linux/time.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/profile.h>
#include <linux/delay.h>

#include <asm/timex.h>
#include <asm/platform.h>

#ifdef CONFIG_XTENSA_CALIBRATE_CCOUNT
unsigned long ccount_per_jiffy;					/* per 1/HZ */
#endif
unsigned long nsec_per_ccount = DEFAULT_NSEC_PER_CCOUNT;	/* nsec per ccount increment */

#ifdef CONFIG_GENERIC_TIME
static cycle_t ccount_read(void)
{
	return (cycle_t)get_ccount();
}

static struct clocksource ccount_clocksource = {
	.name = "ccount",
	.rating = 200,
	.read = ccount_read,
	.mask = CLOCKSOURCE_MASK(32),
	/*
	 * With a shift of 22 the lower limit of the cpu clock is
	 * 1MHz, where NSEC_PER_CCOUNT is 1000 or a bit less than
	 * 2^10: Since we have 32 bits and the multiplicator can
	 * already take up as much as 10 bits, this leaves us with
	 * remaining upper 22 bits.
	 */
	.shift = 22,
};
#endif

static irqreturn_t timer_interrupt(int irq, void *dev_id);
static struct irqaction timer_irqaction = {
	.handler =	timer_interrupt,
	.flags =	IRQF_PERCPU | IRQF_DISABLED,
	.name =		"timer",
	.mask = 	CPU_MASK_ALL,
};

void __init time_init(void)
{
	xtime.tv_nsec = 0;
	xtime.tv_sec = read_persistent_clock();

	set_normalized_timespec(&wall_to_monotonic,
		-xtime.tv_sec, -xtime.tv_nsec);
#ifdef CONFIG_XTENSA_CALIBRATE_CCOUNT
	printk("time_init: Platform Calibrating CPU frequency\n");
	platform_calibrate_ccount();
#endif
	printk("%s: ccount_per_jiffy:%lu [%d.%02d MHz], nsec_per_ccount:%lu\n", __func__,
		(unsigned long) CCOUNT_PER_JIFFY,
		(int)CCOUNT_PER_JIFFY/(1000000/HZ),
		(int)(CCOUNT_PER_JIFFY/(10000/HZ))%100, 
		(unsigned long) nsec_per_ccount);

#ifdef CONFIG_GENERIC_TIME
	ccount_clocksource.mult =
		clocksource_hz2mult(CCOUNT_PER_JIFFY * HZ,
				ccount_clocksource.shift);
	clocksource_register(&ccount_clocksource);
#endif /* CONFIG_GENERIC_CLOCK */
	/* Initialize the linux timer interrupt. */

	set_linux_timer(get_ccount() + CCOUNT_PER_JIFFY);
	setup_irq(LINUX_TIMER_INT, &timer_irqaction);
}

#ifdef CONFIG_SMP
extern void secondary_irq_enable(int);
void __init secondary_time_init(void)
{
printk("secondary_time_init()\n");
	set_linux_timer(get_ccount() + CCOUNT_PER_JIFFY);
	secondary_irq_enable(LINUX_TIMER_INT);
}
#endif

#ifndef CONFIG_GENERIC_TIME
int do_settimeofday(struct timespec *tv)
{
	time_t wtm_sec, sec = tv->tv_sec;
	long wtm_nsec, nsec = tv->tv_nsec;
	unsigned long delta;

	if ((unsigned long)tv->tv_nsec >= NSEC_PER_SEC)
		return -EINVAL;

	write_seqlock_irq(&xtime_lock);

	/* This is revolting. We need to set "xtime" correctly. However, the
	 * value in this location is the value at the most recent update of
	 * wall time.  Discover what correction gettimeofday() would have
	 * made, and then undo it!
	 */

	delta = CCOUNT_PER_JIFFY;
	delta += get_ccount() - get_linux_timer();
	nsec -= delta * NSEC_PER_CCOUNT;

	wtm_sec  = wall_to_monotonic.tv_sec + (xtime.tv_sec - sec);
	wtm_nsec = wall_to_monotonic.tv_nsec + (xtime.tv_nsec - nsec);

	set_normalized_timespec(&xtime, sec, nsec);
	set_normalized_timespec(&wall_to_monotonic, wtm_sec, wtm_nsec);

	ntp_clear();
	write_sequnlock_irq(&xtime_lock);
	return 0;
}

EXPORT_SYMBOL(do_settimeofday);


void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;
	unsigned long volatile sec, usec, delta, seq;

	do {
		seq = read_seqbegin_irqsave(&xtime_lock, flags);

		sec = xtime.tv_sec;
		usec = (xtime.tv_nsec / NSEC_PER_USEC);

		delta = get_linux_timer() - get_ccount();

	} while (read_seqretry_irqrestore(&xtime_lock, seq, flags));

	usec += (((unsigned long) CCOUNT_PER_JIFFY - delta)
		 * (unsigned long) NSEC_PER_CCOUNT) / NSEC_PER_USEC;

	for (; usec >= 1000000; sec++, usec -= 1000000)
		;

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

EXPORT_SYMBOL(do_gettimeofday);
#endif


/*
 * The timer interrupt is called HZ times per second.
 */

irqreturn_t timer_interrupt (int irq, void *dev_id)
{
	unsigned int cpu = smp_processor_id();
	unsigned long next;
	unsigned long ticks;

	next = get_linux_timer();

again:

	ticks = 0;
	while ((signed long)(get_ccount() - next) > 0) {
		ticks++;
		next += CCOUNT_PER_JIFFY;
	}

	/* Note that writing CCOMPARE clears the interrupt. */

	set_linux_timer(next);


	profile_tick(CPU_PROFILING);
	update_process_times(user_mode(get_irq_regs()));

	if (cpu == 0) {
		write_seqlock(&xtime_lock);

		do_timer(ticks); /* Linux handler in kernel/timer.c */
		write_sequnlock(&xtime_lock);

		/* Allow platform to do something useful (Wdog). */

		platform_heartbeat();
	}

	/* Make sure we didn't miss any tick... */

	if ((signed long)(get_ccount() - next) > 0)
		goto again;

	return IRQ_HANDLED;
}

#ifndef CONFIG_GENERIC_CALIBRATE_DELAY
/* Called from start_kernel() and secondary_start_kernel() */
void __cpuinit calibrate_delay(void)
{
	loops_per_jiffy = CCOUNT_PER_JIFFY;

	printk("%s: Calibrating delay loop (skipped)... "
	       "%lu.%02lu BogoMIPS preset\n", __func__,
	       loops_per_jiffy/(1000000/HZ),
	       (loops_per_jiffy/(10000/HZ)) % 100);
}
#endif
