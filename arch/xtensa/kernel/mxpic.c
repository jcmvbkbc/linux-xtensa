/*
 * linux/arch/xtensa/kernel/mxpic.c
 *
 * Xtensa built-in interrupt controller.
 *
 * Copyright (C) 2002 - 2009 Tensilica, Inc.
 * Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 *
 * Chris Zankel <chris@zankel.net>
 * Joe Taylor <joe@tensilica.com>
 * Pete Delaney <piet@tensilica.com>
 */

#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel_stat.h>

#include <asm/uaccess.h>
#include <asm/platform.h>
#include <asm/mxregs.h>

#ifdef CONFIG_SMP

extern DEFINE_PER_CPU(unsigned int , cached_irq_mask);

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
	printk("%s: cpu:%d, mask:%x\n", __func__, cpu, get_sr(INTENABLE));

}
#endif


