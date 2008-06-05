/*
 * linux/arch/xtensa/kernel/xtpic.c
 *
 * Xtensa built-in interrupt controller.
 *
 * Copyright (C) 2002 - 2006 Tensilica, Inc.
 * Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 *
 * Chris Zankel <chris@zankel.net>
 * Kevin Chea
 *
 */

#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel_stat.h>

#include <asm/uaccess.h>
#include <asm/platform.h>

DEFINE_PER_CPU(unsigned int, cached_irq_mask);

static void xtensa_irq_mask(unsigned int irq)
{
	int cpu = smp_processor_id();
	per_cpu(cached_irq_mask, cpu) &= ~(1 << irq);
	set_sr (per_cpu(cached_irq_mask, cpu), INTENABLE);
}

static void xtensa_irq_unmask(unsigned int irq)
{
	int cpu = smp_processor_id();
	per_cpu(cached_irq_mask, cpu) |= 1 << irq;
	set_sr (per_cpu(cached_irq_mask, cpu), INTENABLE);
}

static void xtensa_irq_ack(unsigned int irq)
{
	set_sr(1 << irq, INTCLEAR);
}

static int xtensa_irq_retrigger(unsigned int irq)
{
	set_sr (1 << irq, INTSET);
	return 1;
}


static struct irq_chip xtensa_irq_chip = {
	.name		= "xtensa",
	.mask		= xtensa_irq_mask,
	.unmask		= xtensa_irq_unmask,
	.ack		= xtensa_irq_ack,
	.retrigger	= xtensa_irq_retrigger,
};

void __init init_IRQ(void)
{
	int index;

	for (index = 0; index < XTENSA_NR_IRQS; index++) {
		int mask = 1 << index;

#ifdef CONFIG_MX1
		if (index == 0)
			set_irq_chip_and_handler(index, &xtensa_irq_chip,
						 handle_percpu_irq);
#endif
		else if (mask & XCHAL_INTTYPE_MASK_SOFTWARE)
			set_irq_chip_and_handler(index, &xtensa_irq_chip,
						 handle_simple_irq);

		else if (mask & XCHAL_INTTYPE_MASK_EXTERN_EDGE)
			set_irq_chip_and_handler(index, &xtensa_irq_chip,
						 handle_edge_irq);

		else if (mask & XCHAL_INTTYPE_MASK_EXTERN_LEVEL)
			set_irq_chip_and_handler(index, &xtensa_irq_chip,
						 handle_level_irq);

		else if (mask & XCHAL_INTTYPE_MASK_TIMER)
			set_irq_chip_and_handler(index, &xtensa_irq_chip,
#ifdef CONFIG_SMP
						 handle_percpu_irq);
#else
						 handle_level_irq);
#endif

		else	/* XCHAL_INTTYPE_MASK_WRITE_ERROR */
			/* XCHAL_INTTYPE_MASK_NMI */

			set_irq_chip_and_handler(index, &xtensa_irq_chip,
						 handle_level_irq);
	}
	platform_init_irq();
}

#ifdef CONFIG_SMP
void enable_local_irq(int intrnum)
{
	int cpu = smp_processor_id();
	xtensa_irq_unmask(intrnum);
	printk("cpu %d mask %x\n", cpu, get_sr(INTENABLE));

}
#endif


