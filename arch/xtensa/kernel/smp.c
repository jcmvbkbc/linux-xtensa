/*
 * arch/xtensa/kernel/smp.c
 *
 * Xtensa SMP support functions.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 - 2009 Tensilica Inc.
 *
 * Chris Zankel <chris@zankel.net>
 * Joe Taylor <joe@tensilica.com>
 * Pete Delaney <piet@tensilica.com
 */

#include <linux/init.h>
#include <linux/smp.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/thread_info.h>
#include <linux/kdebug.h>
#include <linux/cpumask.h>

#include <asm/tlbflush.h>
#include <asm/platform.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>
#include <asm/mxregs.h>
#include <asm/kdebug.h>

#ifdef CONFIG_SMP
# if XCHAL_HAVE_S32C1I == 0
#  error "The S32C1I option is required for SMP."
# endif
#endif


/* Per-processor data. */

extern DEFINE_PER_CPU(unsigned long, asid_cache);

/* Map of cores in the system and currently online. */
#if 0
const cpumask_t cpu_possible_map;
const cpumask_t cpu_online_map;
#endif

EXPORT_SYMBOL(cpu_possible_map);
EXPORT_SYMBOL(cpu_online_map);

/* IPI (Inter Process Interrupt) */

#define IPI_IRQ	0

static irqreturn_t ipi_interrupt(int irq, void *dev_id);
static struct irqaction ipi_irqaction = {
	.handler = 	ipi_interrupt,
	.flags = 	IRQF_PERCPU | IRQF_DISABLED,
	.name =		"ipi",
	.mask = 	CPU_MASK_ALL,
};

static inline unsigned int get_core_count(void)
{
        /* Bits 18..21 of SYSCFGID contain the core count minus 1. */
        unsigned int syscfgid = get_er(SYSCFGID);
        return ((syscfgid >> 18) & 0xf) + 1;
}

int get_core_id(void)
{
        /* Bits 0...18 of SYSCFGID contain the core id  */
        unsigned int core_id = get_er(SYSCFGID);

        return (core_id & 0x3fff);
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
}

void __init smp_init_cpus(void)
{
	unsigned i;
	unsigned int ncpus = get_core_count();
	unsigned int core_id = get_core_id();

	printk("%s: Core Count = %d\n", __func__, ncpus);
	printk("%s: Core Id = %d\n", __func__, core_id);

	for (i = 0; i < ncpus; i++) {
		cpu_set(i, cpu_present_map);
		cpu_set(i, cpu_possible_map);
	}
}

extern void secondary_time_init(void);
extern void secondary_trap_init(void);

void __devinit smp_prepare_boot_cpu(void)
{
	unsigned int cpu = smp_processor_id();
	BUG_ON(cpu != 0);
	cpu_asid_cache(cpu) = ASID_USER_FIRST;

	printk("%s:\n", __func__);
}

void __init smp_cpus_done(unsigned int max_cpus)
{
	printk("%s:\n", __func__);
}

extern void __init secondary_irq_init(void);
extern void secondary_irq_enable(int);

volatile int boot_secondary_processors = 1;	/* Set with xt-gdb via .xt-gdb */

void __init secondary_start_kernel(void)
{
	struct mm_struct *mm = &init_mm;
	unsigned int cpu = smp_processor_id();

#ifdef CONFIG_DEBUG_KERNEL
	if (boot_secondary_processors == 0) {
		printk("%s: boot_secondary_processors:%d; Hanging cpu:%d\n", 
			__func__, boot_secondary_processors, cpu);
		for(;;)
			;
	}

	printk("%s: boot_secondary_processors:%d; Booting cpu:%d\n", 
		__func__, cpu, boot_secondary_processors);
#endif

	/* Init EXCSAVE1 */

	secondary_trap_init();

	/* All kernel threads share the same mm context. */

	atomic_inc(&mm->mm_users);
	atomic_inc(&mm->mm_count);
	current->active_mm = mm;
	cpu_set(cpu, mm->cpu_vm_mask);
	enter_lazy_tlb(mm, current);

	preempt_disable();

	calibrate_delay();

	secondary_irq_init();
	secondary_time_init();
	secondary_irq_enable(IPI_IRQ);

	cpu_set(cpu, cpu_online_map);

	cpu_idle();
}

void __init smp_init_irq(void)
{
	setup_irq(IPI_IRQ, &ipi_irqaction);
}

extern struct {
	unsigned long stack;
	void* start;
} start_info;

extern int __devinit wakeup_secondary_cpu(unsigned int, struct task_struct*);

int __cpuinit __cpu_up(unsigned int cpu)
{
	struct task_struct *idle;
	int ret;

	idle = fork_idle(cpu);
	if (IS_ERR(idle)) {
		printk(KERN_ERR "CPU%u: fork() failed\n", cpu);
		return PTR_ERR(idle);
	}
	cpu_asid_cache(cpu) = ASID_USER_FIRST;

	start_info.stack = (unsigned long) idle->thread.sp;
	start_info.start = secondary_start_kernel;

	pr_debug("%s: Calling wakeup_secondary(cpu:%d, idle:%p)\n", __func__,
					       cpu,    idle);

	ret = wakeup_secondary_cpu(cpu, idle);

	if (ret == 0) {
		unsigned long timeout;

		timeout = jiffies + HZ * 20;
		while (time_before(jiffies, timeout)) {
			if (cpu_online(cpu))
				break;
			
			udelay(10);
			barrier();
		}

		if (!cpu_online(cpu)) {
			ret = -EIO;
			pr_debug("%s: ret = -EIO:%d);\n", __func__, ret);
		}
	}
	udelay(100000);
	pr_debug("%s: return(ret:%d);\n", __func__, ret);
	return ret;
}
extern void send_ipi_message(cpumask_t, int);
void smp_send_reschedule(int cpu)
{
	cpumask_t callmask = cpu_online_map;

	cpu_clear(smp_processor_id(), callmask);

	send_ipi_message(callmask, IPI_RESCHEDULE);
}

void smp_send_stop(void)
{
	/* REMIND-FIXME: remove this infinite loop. */
	printk("smp_send_stop()\n");
	while(1);
		// smp_call_function(stop_this_cpu, 0, 1, 0); ?? SH
}

struct smp_call_data {
	void (*func) (void *info);
	void *info;
	long wait;
	atomic_t pending;
	atomic_t running;
};


//extern void send_ipi_message(cpumask_t, int);

static DEFINE_SPINLOCK(smp_call_function_lock);
static struct smp_call_data * volatile smp_call_function_data;

int smp_call_function_on_cpu(void (*func)(void *info), void *info, 
			      int wait, cpumask_t callmask)
{
	struct smp_call_data data;
	long nrcpus;
	unsigned long timeout;

	// pr_debug("**********\nsmp_call_function %d -> %d%d %p(%p) w:%d\n",
	//	 smp_processor_id(), cpu_isset(0, cpu_online_map),
	//	 cpu_isset(1, cpu_online_map), func, info, wait);

	/* Exclude this CPU from the mask */
	cpu_clear(smp_processor_id(), callmask);
	nrcpus = cpus_weight(callmask);

	if (nrcpus == 0)
		return 0;

	/* Can deadlock when called with interrupts disabled */
	WARN_ON(irqs_disabled());

	data.func = func;
	data.info = info;
	data.wait = wait;

	atomic_set(&data.pending, nrcpus);
	atomic_set(&data.running, wait ? nrcpus : 0);

	spin_lock(&smp_call_function_lock);
	smp_call_function_data = &data;

	send_ipi_message(callmask, IPI_CALL_FUNC);

	timeout = jiffies + HZ * 20;
	while (atomic_read(&data.pending) != 0 && time_before(jiffies, timeout))
		barrier();

	if (atomic_read(&data.pending) != 0) {
		printk("%s: Cross Call Timed Out; data.pending:%d; wait:%d\n", 
			__func__,    atomic_read(&data.pending),   wait);
	}

	smp_call_function_data = NULL;
	spin_unlock(&smp_call_function_lock);

	if (atomic_read(&data.pending) != 0)
		return -ETIMEDOUT;

	while (wait && atomic_read(&data.running))
		barrier();

	return 0;
}


int smp_call_function(void (*func)(void *), void *info, int wait)
{
	return smp_call_function_on_cpu (func, info, wait, cpu_online_map);
}
EXPORT_SYMBOL(smp_call_function);

void ipi_call_function(void)
{
	struct smp_call_data *data;

	/* Get data structure before we decrement the 'pending' counter. */

	data = smp_call_function_data;
	atomic_sub(1, &data->pending);

	/* Execute function. */

#if 0
	printk("%s: data:%p->func:%p(data->info:%p)\n",
	  __func__, data, data->func, data->info);
#endif
	data->func(data->info);

	/* If caller is waiting on us, decrement running counter. */

	if (data->wait)
		atomic_sub(1, &data->running);
}

void ipi_reschedule(void)
{
	set_need_resched();
}

extern int recv_ipi_messages(void);

/*
 * REMIND: Why does a breakpoint here cause
 *         multihits and do_IRQ() recursion?
 *         Getting a kernel_exception and call
 *         to do_debug().	
 */
irqreturn_t ipi_interrupt(int irq, void *dev_id)
{
	int msg;
	
	msg = recv_ipi_messages();
#if 0
	{
		int cpu = smp_processor_id();

		printk("---------\nipi_interrupt msg %x on %d!\n", msg, cpu);
	}
#endif

	if (msg & (1 << IPI_RESCHEDULE)) {
		ipi_reschedule();
	} 
	if (msg & (1 << IPI_CALL_FUNC)) {
		ipi_call_function();
	}

#ifdef CONFIG_KGDB
	/* REMIND: Needs work! */
	if (msg & (1 << IPI_NMI_DIE)) {
		struct pt_regs *regs = NULL;

		printk("%s: notify_die(DIE_NMI_IPI, 'nmi_ipi', "
			"regs:%p, msg:0X%x, 2, SIGINT);\n", __func__, 
                         regs,    msg);

		// notify_die(DIE_NMI_IPI, "nmi_ipi", regs, msg, 2, SIGINT);
	}
#endif
	// send_ipi(cpu, SMP_MSG_RESCHEDULE)

	return IRQ_HANDLED;
}

// FIXME move somewhere else...
int setup_profiling_timer(unsigned int multiplier)
{
	pr_debug("setup_profiling_timer %d\n", multiplier);
	return 0;
}

/* ------------------------------------------------------------------------- */

#if defined(CONFIG_SMP)
/*
 * It's not clear yet how many of these cache and TLB flushes 
 * have to be implemented with Cross Calls.
 */

void ipi_flush_cache_page(const void *arg[])
{
	local_flush_cache_page((struct vm_area_struct *) arg[0], 
			     (unsigned long) arg[1], (unsigned long) arg[2]);
}

void ipi_flush_cache_range(const void *arg[])
{
	/* 
	 * NOTE: on_each_cpu() disables interrupts while doing
	 * local_flush_cache_range() on the local processor.
	 */
	local_flush_cache_range((struct vm_area_struct *) arg[0], 
			     (unsigned long) arg[1], (unsigned long) arg[2]);
}


void ipi_flush_cache_all(void)
{
	local_flush_cache_all();
}
#endif /* CONFIG_SMP */



void ipi_flush_icache_range(const void *arg[])
{
	local_flush_icache_range((unsigned long) arg[0], (unsigned long) arg[1]);
}

void ipi_flush_tlb_page(const void *arg[])
{
	local_flush_tlb_page((struct vm_area_struct *) arg[0], 
			     (unsigned long) arg[1]);
}

void ipi_flush_tlb_range(const void *arg[])
{
	local_flush_tlb_range((struct vm_area_struct *) arg[0], 
			      (unsigned long) arg[1], (unsigned long) arg[2]);
}

void flush_tlb_all(void)
{
	on_each_cpu((void(*)(void*))local_flush_tlb_all, NULL, 1);
}

void flush_tlb_mm(struct mm_struct* mm)
{
	on_each_cpu((void(*)(void*))local_flush_tlb_mm, mm, 1);
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long addr)
{
	unsigned long args[] = { (unsigned long )vma, addr};

	on_each_cpu((void(*)(void*))ipi_flush_tlb_page, args, 1);
}

void flush_tlb_range(struct vm_area_struct *vma, 
		     unsigned long start, unsigned long end)
{
	unsigned long args[] = { (unsigned long)vma, start, end };

	on_each_cpu((void(*)(void*))ipi_flush_tlb_range, args, 1);
}

void flush_icache_range(unsigned long start, unsigned long end)
{
	unsigned long args[] = {start, end};

	on_each_cpu((void(*)(void*))ipi_flush_icache_range, args, 1);
}

#ifdef CONFIG_SMP
void flush_cache_page(struct vm_area_struct *vma, 
		     unsigned long address, unsigned long pfn)
{
	unsigned long args[] = { (unsigned long)vma, address, pfn};

	on_each_cpu((void(*)(void*))ipi_flush_cache_page, args, 1);
}

void flush_cache_range(struct vm_area_struct *vma, 
		     unsigned long start, unsigned long end)
{
	unsigned long args[] = { (unsigned long)vma, start, end};

	on_each_cpu((void(*)(void*))ipi_flush_cache_range, args, 1);
}

void flush_cache_all()
{
	unsigned long args[] = { 0UL, 0UL, 0UL };

	on_each_cpu((void(*)(void*))ipi_flush_cache_all, args, 1);
}
#endif /* CONFIG_SMP */

/* ------------------------------------------------------------------------- */

void ipi_invalidate_dcache_range(const void *arg[])
{
	__invalidate_dcache_range((unsigned long)arg[0], (unsigned long) arg[1]);
}

void ipi_flush_invalidate_dcache_range(const void *arg[])
{
	__flush_invalidate_dcache_range((unsigned long)arg[0], (unsigned long) arg[1]);
}

void system_invalidate_dcache_range(unsigned long start, unsigned long size)
{
	unsigned long args[] = { start, size };
	on_each_cpu((void(*)(void*))ipi_invalidate_dcache_range, args, 1);
}

void system_flush_invalidate_dcache_range(unsigned long start, unsigned long size)
{
	unsigned long args[] = { start, size };
	on_each_cpu((void(*)(void*))ipi_flush_invalidate_dcache_range, args, 1);
}


