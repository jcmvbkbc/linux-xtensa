/*
 * include/asm-xtensa/timex.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2009 Tensilica Inc.
 */

#ifndef _XTENSA_TIMEX_H
#define _XTENSA_TIMEX_H

#ifdef __KERNEL__

#include <asm/processor.h>
#include <linux/stringify.h>
#include <linux/autoconf.h>

#define _INTLEVEL(x)	XCHAL_INT ## x ## _LEVEL
#define INTLEVEL(x)	_INTLEVEL(x)

#if INTLEVEL(XCHAL_TIMER0_INTERRUPT) <= XCHAL_EXCM_LEVEL
# define LINUX_TIMER     0
# define LINUX_TIMER_INT XCHAL_TIMER0_INTERRUPT
#elif INTLEVEL(XCHAL_TIMER1_INTERRUPT) <= XCHAL_EXCM_LEVEL
# define LINUX_TIMER     1
# define LINUX_TIMER_INT XCHAL_TIMER1_INTERRUPT
#elif INTLEVEL(XCHAL_TIMER2_INTERRUPT) <= XCHAL_EXCM_LEVEL
# define LINUX_TIMER     2
# define LINUX_TIMER_INT XCHAL_TIMER2_INTERRUPT
#else
# error "No Xtensa timer at or below EXCM level on this core; need at least one!"
#endif

#define LINUX_TIMER_MASK        (1L << LINUX_TIMER_INT)

#define CLOCK_TICK_RATE 	1193180	/* (everyone is using this value) */
#define CLOCK_TICK_FACTOR       20 /* Factor of both 10^6 and CLOCK_TICK_RATE */

/* Defaults used of platform doesn't provide platform_calibrate_ccount() */
#define DEFAULT_CCOUNT_PER_JIFFY (CONFIG_XTENSA_CPU_CLOCK*(CONFIG_XTENSA_CPU_CLOCK_UNITS/HZ))
#define DEFAULT_NSEC_PER_CCOUNT  1000000000UL/(CONFIG_XTENSA_CPU_CLOCK * CONFIG_XTENSA_CPU_CLOCK_UNITS)

/* Set by platform_calibrate_ccount() */
#ifdef CONFIG_XTENSA_CALIBRATE_CCOUNT
extern unsigned long nsec_per_ccount;
extern unsigned long ccount_per_jiffy;
#define CCOUNT_PER_JIFFY ccount_per_jiffy
#define NSEC_PER_CCOUNT  nsec_per_ccount
#else
#define CCOUNT_PER_JIFFY DEFAULT_CCOUNT_PER_JIFFY
#define NSEC_PER_CCOUNT DEFAULT_NSEC_PER_CCOUNT
#endif


typedef unsigned long long cycles_t;

/*
 * Only used for SMP.
 */

extern cycles_t cacheflush_time;

#define get_cycles()	(0)


/*
 * Register access.
 */

#define WSR_CCOUNT(r)	  asm volatile ("wsr %0,"__stringify(CCOUNT) :: "a" (r))
#define RSR_CCOUNT(r)	  asm volatile ("rsr %0,"__stringify(CCOUNT) : "=a" (r))
#define WSR_CCOMPARE(x,r) asm volatile ("wsr %0,"__stringify(CCOMPARE)"+"__stringify(x) :: "a"(r))
#define RSR_CCOMPARE(x,r) asm volatile ("rsr %0,"__stringify(CCOMPARE)"+"__stringify(x) : "=a"(r))

static inline unsigned long get_ccount (void)
{
	unsigned long ccount;
	RSR_CCOUNT(ccount);
	return ccount;
}

static inline void set_ccount (unsigned long ccount)
{
	WSR_CCOUNT(ccount);
}

static inline unsigned long get_linux_timer (void)
{
	unsigned ccompare;
	RSR_CCOMPARE(LINUX_TIMER, ccompare);
	return ccompare;
}

static inline void set_linux_timer (unsigned long ccompare)
{
	WSR_CCOMPARE(LINUX_TIMER, ccompare);
}

#endif	/* __KERNEL__ */
#endif	/* _XTENSA_TIMEX_H */
