/*
 * linux/arch/xtensa/kernel/irq.c
 *
 * Xtensa built-in interrupt controller and some generic functions copied
 * from i386.
 *
 * Copyright (C) 2002 - 2009 Tensilica, Inc.
 * Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 *
 * Chris Zankel <chris@zankel.net>
 * Joe Taylor <joe@tensilica.com>
 * Pete Delaney <piet@tensilica.com>
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
#include <asm/mxregs.h>

#ifdef CONFIG_KGDB
int kgdb_early_setup;
#endif

#ifdef CONFIG_SMP
extern __init void smp_init_irq(void);
#endif

DEFINE_PER_CPU(unsigned int, cached_irq_mask);

atomic_t irq_err_count;

/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves.
 */
void ack_bad_irq(unsigned int irq)
{
          printk("unexpected IRQ trap at vector %02x\n", irq);
}

/*
 * do_IRQ handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 */

asmlinkage void do_IRQ(int irq, struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	struct irq_desc *desc = irq_desc + irq;

	if (irq >= NR_IRQS) {
		printk(KERN_EMERG "%s: cannot handle IRQ %d\n",
				__func__, irq);
	}

	irq_enter();			/* Disable Preemption */

#ifdef CONFIG_DEBUG_STACKOVERFLOW
	/* Debugging check for stack overflow: is there less than 1KB free? */
	{
		unsigned long sp;

		__asm__ __volatile__ ("mov %0, a1\n" : "=a" (sp));
		sp &= THREAD_SIZE - 1;

		if (unlikely(sp < (sizeof(struct thread_info) + 1024)))
			printk("Stack overflow in do_IRQ: %ld\n",
			       sp - sizeof(struct thread_info));
	}
#endif
	desc->handle_irq(irq, desc);

	irq_exit();			/* Enable Preemption */
	set_irq_regs(old_regs);
}

/*
 * Generic, controller-independent functions:
 */

int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v, j;
	struct irqaction * action;
	unsigned long flags;

	if (i == 0) {
		seq_printf(p, "           ");
		for_each_online_cpu(j)
			seq_printf(p, "CPU%d       ",j);
		seq_putc(p, '\n');
	}

	if (i < NR_IRQS) {
		spin_lock_irqsave(&irq_desc[i].lock, flags);
		action = irq_desc[i].action;
		if (!action)
			goto skip;
		seq_printf(p, "%3d: ",i);
#ifndef CONFIG_SMP
		seq_printf(p, "%10u ", kstat_irqs(i));
#else
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", kstat_cpu(j).irqs[i]);
#endif
		seq_printf(p, " %14s", irq_desc[i].chip->typename);
		seq_printf(p, "  %s", action->name);

		for (action=action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);

		seq_putc(p, '\n');
skip:
		spin_unlock_irqrestore(&irq_desc[i].lock, flags);
	} else if (i == NR_IRQS) {
		seq_printf(p, "NMI: ");
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", nmi_count(j));
		seq_putc(p, '\n');
		seq_printf(p, "ERR: %10u\n", atomic_read(&irq_err_count));
	}
	return 0;
}

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

__attribute__((weak)) 
void variant_irq_enable(unsigned int irq) {
}

static void xtensa_irq_enable(unsigned int irq)
{
	variant_irq_enable(irq);
	xtensa_irq_unmask(irq);
}


__attribute__((weak)) 
void variant_irq_disable(unsigned int irq) { 
}

static void xtensa_irq_disable(unsigned int irq)
{
	xtensa_irq_mask(irq);
	variant_irq_disable(irq);
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
#if 1
	.enable		= xtensa_irq_enable,
	.disable	= xtensa_irq_disable,
#endif
	.mask		= xtensa_irq_mask,
	.unmask		= xtensa_irq_unmask,
	.ack		= xtensa_irq_ack,
	.retrigger	= xtensa_irq_retrigger,
};


#ifdef CONFIG_ARCH_HAS_SMP
/* Block Interrupts at Interrupt Distributer */
static void xtensa_mx_irq_mask(unsigned int irq)
{
	set_er(1 << (irq-2), MIENG);
}

/* Enable Interrupts at  Interrupt Distributer */
static void xtensa_mx_irq_unmask(unsigned int irq)
{
	set_er(1 << (irq-2), MIENGSET);
}

/* Assert Interrupt at Interrupt Distributer and then clear it on the core */
static void xtensa_mx_irq_ack(unsigned int irq)
{
	set_er(1 << (irq-2), MIASG);
	set_sr(1 << irq, INTCLEAR);
}

/* Assert Interrupt at Interrupt Distributer and then assert it on the core */
static int xtensa_mx_irq_retrigger(unsigned int irq)
{
	set_er(1 << (irq-2), MIASGSET);
	set_sr(1 << irq, INTSET);
	return 1;
}
	
/*
 * Used for IRQs 2, 3, 4, and 5 for SMP Systems
 */
static struct irq_chip xtensa_mx_irq_chip = {
	.name		= "xtensa-mx",
	.mask		= xtensa_mx_irq_mask,
	.unmask		= xtensa_mx_irq_unmask,
	.ack		= xtensa_mx_irq_ack,
	.retrigger	= xtensa_mx_irq_retrigger,
};

#endif

void __init init_IRQ(void)
{
	int index;

#ifdef CONFIG_KGDB
        if (kgdb_early_setup)
                return;
#endif
	for (index = 0; index < XTENSA_NR_IRQS; index++) {
		int mask = 1 << index;

#ifdef CONFIG_ARCH_HAS_SMP
		if (index <= 1)
			/* On CORE IPI Interrupts */
			set_irq_chip_and_handler(index, &xtensa_irq_chip,
						 handle_percpu_irq);

		else if (index <= 5)
			/* IRQs: [2, 3:UART, 4:OETH, 5] External Interrupts */
			set_irq_chip_and_handler(index, &xtensa_mx_irq_chip,	/* MX */
						 handle_level_irq);
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

#ifdef CONFIG_ARCH_HAS_SMP
		else if (mask & XCHAL_INTTYPE_MASK_TIMER)
			set_irq_chip_and_handler(index, &xtensa_irq_chip,
						 handle_percpu_irq);
#else
		else if (mask & XCHAL_INTTYPE_MASK_TIMER)
			set_irq_chip_and_handler(index, &xtensa_irq_chip,
						 handle_edge_irq);
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

#ifdef 	CONFIG_ARCH_HAS_SMP

#ifdef 	CONFIG_SMP
	smp_init_irq();
#endif

	/* 
	 * Route all external interrupts to the first processor, perhaps only.
	 * 
	 * REMIND:
	 *   Why do we want to do this? Puts more CPU load on 1st processor.
	 */
	for (index = 0; index < 4; index++)
		set_er(1, MIROUT(index));
#endif

#ifdef CONFIG_KGDB
        if (!kgdb_early_setup)
                kgdb_early_setup = 1;
#endif

}

#ifdef CONFIG_SMP
/*
 * REMIND: Move to seperate file?
 */
void __init secondary_irq_init(void)
{
	printk("secondary_irq_init: set cached_irq_mask and enable interrupts))\n");
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
	printk("%s(intrnum:%d): cpu:%d, INTENABLE:%x\n", __func__, intrnum, cpu, get_sr(INTENABLE));
}
#endif

