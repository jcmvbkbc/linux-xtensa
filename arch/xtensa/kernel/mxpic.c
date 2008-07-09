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
#include <asm/mxpic.h>



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

static void set_er (unsigned long value, unsigned long addr)
{
	//printk("seter %x -> %x\n", value, addr);
	asm volatile ("wer %0, %1" : : "a" (value), "a" (addr) : "memory");
}

static inline unsigned long get_er (unsigned long addr)
{
	register unsigned long value;
	asm volatile ("rer %0, %1" : "=a" (value) : "a" (addr) : "memory");
	return value;
}
	
void write_er(unsigned long v, unsigned long a)
{
	set_er(v, a);
}
unsigned long read_er(unsigned long a)
{
	return get_er(a);
}

static void xtensa_mx_irq_mask(unsigned int irq)
{
	set_er(1 << (irq-2), MIENG);
}

static void xtensa_mx_irq_unmask(unsigned int irq)
{
	set_er(1 << (irq-2), MIENGSET);
}

static void xtensa_mx_irq_ack(unsigned int irq)
{
	set_er(1 << (irq-2), MIASG);
	set_sr(1 << irq, INTCLEAR);
}

static int xtensa_mx_irq_retrigger(unsigned int irq)
{
	set_er(1 << (irq-2), MIASGSET);
	set_sr(1 << irq, INTSET);
	return 1;
}
	

static struct irq_chip xtensa_mx_irq_chip = {
	.name		= "xtensa-mx",
	.mask		= xtensa_mx_irq_mask,
	.unmask		= xtensa_mx_irq_unmask,
	.ack		= xtensa_mx_irq_ack,
	.retrigger	= xtensa_mx_irq_retrigger,
};

static struct irq_chip xtensa_irq_chip = {
	.name		= "xtensa",
	.mask		= xtensa_irq_mask,
	.unmask		= xtensa_irq_unmask,
	.ack		= xtensa_irq_ack,
	.retrigger	= xtensa_irq_retrigger,
};

extern __init void smp_init_irq(void);

void __init init_IRQ(void)
{
	int index;

	for (index = 0; index < XTENSA_NR_IRQS; index++) {
		int mask = 1 << index;

		if (index <= 1)
			set_irq_chip_and_handler(index, &xtensa_irq_chip,
						 handle_percpu_irq);

		else if (index <= 5)
			set_irq_chip_and_handler(index, &xtensa_mx_irq_chip,
						 handle_level_irq);

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

	/* Enable all interrupts that are controlled by the external PIC */
	per_cpu(cached_irq_mask, smp_processor_id()) |= 0x3c;
	set_sr(0x3c, INTENABLE);

	smp_init_irq();

	/* Route all external interrupts to the first processor */
	for (index = 0; index < 4; index++)
		set_er(1, MIROUT(index));
}

void __init secondary_irq_init(void)
{
	printk("secondary_irq_init))\n");
	per_cpu(cached_irq_mask, smp_processor_id()) = 0x3c;
	set_sr(0x3c, INTENABLE);
}

int __init wakeup_secondary_cpu(unsigned int cpu, struct task_struct *ts)
{
	set_er(get_er(MPSCORE) & ~ (1 << cpu), MPSCORE);
	printk("cpu %d %lx\n", cpu, get_er(0x200));
	return 0;
}

void send_ipi_message(cpumask_t callmask, int msg_id)
{
	int index;
	unsigned long mask = 0;

	for_each_cpu_mask(index, callmask)
		if (index != smp_processor_id())
			mask |= 1 << index;

	//printk("send to %lx message id %d\n", mask, msg_id);

	set_er(mask, MIPISET(msg_id));
}

int recv_ipi_messages(void)
{
	int messages;

	messages = get_er(MIPICAUSE(smp_processor_id()));
#if 0
	set_er(messages, MIPICAUSE(smp_processor_id()));
#else
	{
	int i;
	for (i = 0 ; i < 3; i++)
		if (messages & (1 << i))
			set_er(1 << i, MIPICAUSE(smp_processor_id()));
	}
#endif

	//printk("cpu %d message bitfield %x\n", smp_processor_id(), messages);

	return messages;
}

void secondary_irq_enable(int intrnum)
{
	int cpu = smp_processor_id();
	xtensa_irq_unmask(intrnum);
	printk("cpu %d mask %x\n", cpu, get_sr(INTENABLE));

}


