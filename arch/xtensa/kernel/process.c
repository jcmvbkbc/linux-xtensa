/*
 * arch/xtensa/kernel/process.c
 *
 * Xtensa Processor version.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2009 Tensilica Inc.
 *
 * Joe Taylor <joe@tensilica.com>
 * Chris Zankel <chris@zankel.net>
 * Marc Gauthier <marc@tensilica.com, marc@alumni.uwaterloo.ca>
 * Pete Delaney <piet@tensilica.com>
 * Kevin Chea
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/elf.h>
#include <linux/init.h>
#include <linux/prctl.h>
#include <linux/init_task.h>
#include <linux/module.h>
#include <linux/mqueue.h>
#include <linux/fs.h>
#include <linux/autoconf.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/coprocessor.h>
#include <asm/platform.h>
#include <asm/mmu.h>
#include <asm/irq.h>
#include <asm/atomic.h>
#include <asm/asm-offsets.h>
#include <asm/regs.h>

extern void ret_from_fork(void);

void (*pm_power_off)(void) = NULL;
EXPORT_SYMBOL(pm_power_off);

#if XTENSA_HAVE_COPROCESSORS
extern DEFINE_PER_CPU(coprocessor_owner_t, coprocessor_owner);

#ifdef CONFIG_LAZY_SMP_COPROCESSOR_REGISTER_FLUSHING
volatile int lazy_smp_coprocessor_flushing_enabled = 1;
#else
volatile int lazy_smp_coprocessor_flushing_enabled = 0;
#endif /* CONFIG_LAZY_SMP_COPROCESSOR_REGISTER_FLUSHING */

#ifdef CONFIG_DEBUG_KERNEL
int process_debug = 0;

#define dprintk(args...) \
	do { \
		if (process_debug) printk(KERN_DEBUG args); \
	} while (0)
#else
#define dprintk(args...)
#endif

#if defined(CONFIG_DEBUG_KERNEL) && defined(XCHAL_HAVE_HIFI)
volatile int check_hifitest_registers = 0;

/*
 * This function is only used with a hifi test programs that puts the pid into a 
 * hifi2 TIE Register, incremnets other TIE registers, and checks to make sure they 
 * are in sync with a integer counterpart. It was helpfull in knowing ASAP when the
 * coprocessor registers were out of sync.
 *
 * REMIND: Will likely remove on push upstream. Might come in handy if another ARCH
 *         has a mechanizm for lazy saves that I missed and we want to switch to it.         
 */
static inline void hifitest_register_check(struct thread_info *owner_ti, int cp_num)
{
	if (check_hifitest_registers) {
		struct task_struct *owner_tsk = owner_ti->task;
		struct thread_info tmp_ti;
		int owner_pid = owner_tsk->pid;
		long long *aep0_ptr;
		long long long_aep0;
		int aep0;

		tmp_ti = *owner_ti;
		coprocessor_flush(&tmp_ti, cp_num);
	
		aep0_ptr = (long long *) &tmp_ti.xtregs_cp.cp1.aep0;
	
		long_aep0 = *aep0_ptr;
		aep0 = long_aep0 & 0XFFFFFF;
	
		if (aep0 != owner_pid) {
			dprintk("%s: owner_pid:%d\n", __func__, owner_pid);
			dprintk("%s: aep0:%d\n", __func__, aep0);
		}
	}
	return;
}
#else  /* !(CONFIG_DEBUG_KERNEL && XCHAL_HAVE_HIFI) */
#define check_hifitest_registers 0
#define hifitest_register_check(ti, cp_num)
#endif /* (CONFIG_DEBUG_KERNEL && XCHAL_HAVE_HIFI) */

/*
 * The task owning a coprocessor (CP) and it's associated registers
 * has it's task info ti_cpenable indicating which coprocessors it ownes.
 *
 * Prior to running the __switch_to() asm code has set the cpenable
 * register bits allowing it to continue using the CP. When a CP
 * owner switches out we bind the task to the current CPU and release
 * it's bind once it gives up ownership of the CP. This makes it possible
 * to use the Lazy saveing of the CP registers in an SMP environment.
 *
 * The CP owner is typically changed by a CP exception which saves
 * the current CP registers and it's cpenable register in the current task->ti,
 * restores the CP registers for the current task. and it's copy of the
 * cpenable register. The current threads cpenable register is always
 * in sync with the cpenable register.
 *
 * Here we manages the coprocessor registers, cpenable register,
 * and the list of pointers to owners of the coprocessros.
 *					-piet
 */
void manage_coprocessors(void *prev, int cmd) {
	struct task_struct *task = (struct task_struct *) prev;
	struct thread_info *ti = task_thread_info(task);
	struct thread_info *current_ti = current_thread_info();
	unsigned long ti_cpenable;
	unsigned long cpu_cpenable;
	int cpu = smp_processor_id();

	preempt_disable();

	/*
	 * While a thread is running the cpenable register
	 * should be identical to the copy in it's thread info.
 	 */
	cpu_cpenable = coprocessor_get_cpenable();
	ti_cpenable = ti->cpenable;
	BUG_ON((ti == current_ti) && ((cpu_cpenable != ti_cpenable) || current_ti->cpu != cpu));

#ifdef CONFIG_SMP
	if ( unlikely(task->ptrace) || unlikely(!lazy_smp_coprocessor_flushing_enabled) ) {
		/*
		 * Better to just flush the CP registers for proceses
		 * being debuged via ptrace. Otherwise we would have to cross
		 * call and disable interrupts instead of just preventing
		 * preemption.
		 */
		if (task->ptrace) {
			dprintk("%s: task:%p->ptrace:0x%x; cmd = CP_FLUSH_AND_RELEASE_ALL;\n", __func__, task, task->ptrace);
		}
		if (likely(cmd == CP_SWITCH)) {
			cmd = CP_FLUSH_AND_RELEASE_ALL;
		}
	}
#endif
	if (ti_cpenable) {
		struct coprocessor_owner *owners = &per_cpu(coprocessor_owner, cpu);
		int i;

		for (i = 0; i < XCHAL_CP_MAX; i++) {
			struct thread_info *owner_ti = owners->coprocessor_ti[i];
			unsigned long cpenable_bit = (1 << i);

			if (ti == owner_ti) {
				BUG_ON((ti_cpenable & cpenable_bit) == 0);
				switch(cmd) {
				case CP_FLUSH_ALL:
					/*
		 			 * Note: Temporally sets CPENABLE while saving registers.
		 			 */
					BUG_ON(cpu != ti->cpu);
					coprocessor_flush(ti, i);
					break;

				case CP_FLUSH_AND_RELEASE_ALL:
					BUG_ON(cpu != ti->cpu);
					coprocessor_flush(ti, i);
					/* fall through */

				case CP_RELEASE_ALL:
					owners->coprocessor_ti[i] = NULL;
					ti_cpenable &= ~cpenable_bit;
					ti->cpenable = ti_cpenable;

					if (ti == current_ti) {
						/* 
						 * We are the current task, so we keep the cpenable
						 * in sync with us.
						 */
						BUG_ON(cpu != ti->cpu);
					 	coprocessor_set_cpenable(ti_cpenable);
					}
					break;
#ifdef CONFIG_SMP
				case CP_SWITCH:
					BUG_ON(ti != current_ti);
					if (unlikely(((task->flags & PF_THREAD_BOUND) == 0))) {
						const cpumask_t current_mask = cpumask_of_cpu(cpu);

						dprintk("%s: (prev->flags & PF_THREAD_BOUND) == 0\n", __func__);

						if ((current_ti->flags & _TIF_COPRORESSOR_BOUND)) {
							panic("%s: !PF_THREAD_BOUND but CP owner and _TIF_COPRORESSOR_BOUND\n", __func__);
						} else {
							dprintk("%s: !PF_THREAD_BOUND && !TIF_COPRORESSOR_BOUND\n", __func__);
							BUG_ON(current_ti->flags & _TIF_COPRORESSOR_BOUND); 

							current_ti->saved_cpus_allowed = task->cpus_allowed;
							current_ti->saved_nr_cpus_allowed = task->rt.nr_cpus_allowed;
							current_ti->bound_cpus_allowed = current_mask;
							task->cpus_allowed = current_mask;
							task->flags |= PF_THREAD_BOUND;
							current_ti->flags |= _TIF_COPRORESSOR_BOUND;
						}
					}
					if (check_hifitest_registers) {
						hifitest_register_check(owner_ti, i);	/* Optimized out in performance kernels */
					}
					break;
#endif
				default:
					BUG();

				} /* switch(cmd) */
			} /* ti == owner_t1 */
		} /* for XCHAL_CP_MAX */
	} else {
		/*
		 * Task no longer owns any co-processors;
		 * if I previously bound myself to the
		 * current cpu it's now fine to un-bind.
		 */ 
#ifdef CONFIG_SMP
		if (unlikely((cmd == CP_SWITCH) && (task->flags & PF_THREAD_BOUND) != 0)) {
			struct thread_info *current_ti = current_thread_info();

			/* 
			 * We could be a bound task like one of the migration threads;
			 * Make sure we bound the thread.
			 */
			if (unlikely((current_ti->flags & _TIF_COPRORESSOR_BOUND) != 0)) {

				dprintk("%s: PF_THREAD_BOUND && _TIF_COPRORESSOR_BOUND; UN-BINDING Thread\n", __func__);

				if (unlikely(!cpus_equal(task->cpus_allowed, current_ti->bound_cpus_allowed))) {
					panic("%s: task->cpus_allowed != current_ti->bound_cpus_allowed", __func__);
				}
				task->cpus_allowed = current_ti->saved_cpus_allowed;
				task->rt.nr_cpus_allowed = current_ti->saved_nr_cpus_allowed;
				current_ti->saved_cpus_allowed = cpumask_of_cpu(0);
				current_ti->bound_cpus_allowed = cpumask_of_cpu(0);
				task->flags &= ~PF_THREAD_BOUND;
				current_ti->flags &= ~_TIF_COPRORESSOR_BOUND;
			}
		}
#endif
	}
	preempt_enable();
}
#endif /* XTENSA_HAVE_COPROCESSORS */

#if defined(CONFIG_DEBUG_KERNEL) && defined(CONFIG_SMP)
unsigned long idle_jiffies[NR_CPUS];
unsigned long idle_count[NR_CPUS];

void cpu_idle_monitor(int sched)
{
	int cpu = smp_processor_id();
	int dt = jiffies - idle_jiffies[cpu];
	int prid;
	unsigned long ccount = get_ccount();

	asm volatile ("rsr %0, "__stringify(PRID)"\n" : "=a" (prid));

	if (prid != cpu) panic("cpu");

	if (dt < 0) dt = - dt;

	if ((cpu >= 0) && (cpu <= NR_CPUS)) {
		idle_count[cpu]++;
		if (dt > (60 * HZ) ) {
			dprintk(KERN_DEBUG "%s: cpu:%d, ccount:%08lx, dt:%d, idle_count:[%lu, %lu]\n", __func__,
					        cpu,    ccount,       dt,    idle_count[0],  idle_count[1]);

			idle_jiffies[cpu] = jiffies;	
		}
	} else 
		printk("%s: cpu:%d is Bizzare!\n", __func__, cpu);
}
# define CPU_IDLE_MONITOR(sched) cpu_idle_monitor(sched)
#else
# define CPU_IDLE_MONITOR(sched)
#endif

/*
 * Powermanagement idle function, if any is provided by the platform.
 */
void cpu_idle(void)
{
  	local_irq_enable();

	/* endless idle loop with no priority at all */
	while (1) {
		while (!need_resched()) {
			platform_idle();
			CPU_IDLE_MONITOR(0);
		}
		preempt_enable_no_resched();
		CPU_IDLE_MONITOR(1);
		schedule();
		preempt_disable();
	}
}

/*
 * This is called when the thread calls exit().
 */
void exit_thread(void)
{
#if XTENSA_HAVE_COPROCESSORS
	manage_coprocessors(current, CP_RELEASE_ALL);
#endif
}

void flush_thread(void)
{
#if XTENSA_HAVE_COPROCESSORS
	manage_coprocessors(current, CP_FLUSH_AND_RELEASE_ALL);
#endif
}

/*
 * This is called before the thread is copied. 
 */
void prepare_to_copy(struct task_struct *tsk)
{
#if XTENSA_HAVE_COPROCESSORS
	manage_coprocessors(tsk, CP_FLUSH_ALL);
#endif
}

/*
 * Copy thread.
 *
 * The stack layout for the new thread looks like this:
 *
 *	+------------------------+ <- sp in childregs (= tos)
 *	|       childregs        |
 *	+------------------------+ <- thread.sp = sp in dummy-frame
 *	|      dummy-frame       |    (saved in dummy-frame spill-area)
 *	+------------------------+
 *
 * We create a dummy frame to return to ret_from_fork:
 *   a0 points to ret_from_fork (simulating a call4)
 *   sp points to itself (thread.sp)
 *   a2, a3 are unused.
 *
 * Note: This is a pristine frame, so we don't need any spill region on top of
 *       childregs.
 *
 * The fun part:  if we're keeping the same VM (i.e. cloning a thread,
 * not an entire process), we're normally given a new usp, and we CANNOT share
 * any live address register windows.  If we just copy those live frames over,
 * the two threads (parent and child) will overflow the same frames onto the
 * parent stack at different times, likely corrupting the parent stack (esp.
 * if the parent returns from functions that called clone() and calls new
 * ones, before the child overflows its now old copies of its parent windows).
 * One solution is to spill windows to the parent stack, but that's fairly
 * involved.  Much simpler to just not copy those live frames across.
 */

int copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
		unsigned long unused,
                struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs *childregs;
	struct thread_info *ti;
	unsigned long tos;
	int user_mode = user_mode(regs);

	/* Set up new TSS. */
	tos = (unsigned long)task_stack_page(p) + THREAD_SIZE;
	if (user_mode)
		childregs = (struct pt_regs*)(tos - PT_USER_SIZE);
	else
		childregs = (struct pt_regs*)tos - 1;

	/* This does not copy all the regs.  In a bout of brilliance or madness,
	   ARs beyond a0-a15 exist past the end of the struct. */
	*childregs = *regs;

	/* Create a call4 dummy-frame: a0 = 0, a1 = childregs. */
	*((int*)childregs - 3) = (unsigned long)childregs;
	*((int*)childregs - 4) = 0;

	childregs->areg[2] = 0;
	p->set_child_tid = p->clear_child_tid = NULL;
	p->thread.ra = MAKE_RA_FOR_CALL((unsigned long)ret_from_fork, 0x1);
	p->thread.sp = (unsigned long)childregs;

	if (user_mode(regs)) {
		int len = childregs->wmask & ~0xf;

		if (clone_flags & CLONE_VM) {
			/* If keeping the same stack, child might do a call8
			   (or call12) following the clone() or vfork() call,
			   and require a caller stack frame for the call8
			   overflow to work.  Ensure we spill at least the
			   caller's sp to enable this.  This normally only
			   happens for vfork() calls.  */
			if (regs->areg[1] == usp  /* same stack */
			    && len != 0  /* caller window is live */ ) {
				int windowsize = (regs->areg[0] >> 30) & 3;
				int caller_ars = (0 - windowsize*4) & (XCHAL_NUM_AREGS - 1);
				put_user(regs->areg[caller_ars+1], (unsigned __user*)(usp - 12));
			}

			/* Otherwise, can't share live windows */
			childregs->wmask = 1;
			childregs->windowbase = 0;
			childregs->windowstart = 1;
		} else {
			memcpy(&childregs->areg[XCHAL_NUM_AREGS - len/4],
			       &regs->areg[XCHAL_NUM_AREGS - len/4], len);
		}
		childregs->areg[1] = usp;

// FIXME: we need to set THREADPTR in thread_info...
//		if (clone_flags & CLONE_SETTLS)
//			childregs->areg[2] = childregs->areg[6];

	} else {
		/* In kernel space, we start a new thread with a new stack. */
		childregs->wmask = 1;
		childregs->windowbase = 0;
		childregs->windowstart = 1;
		childregs->areg[1] = tos;
	}

#if (XTENSA_HAVE_COPROCESSORS || XTENSA_HAVE_IO_PORTS)
	ti = task_thread_info(p);
	ti->cpenable = 0;
#endif

	return 0;
}


/*
 * These bracket the sleeping functions..
 */

unsigned long get_wchan(struct task_struct *p)
{
	unsigned long sp, pc;
	unsigned long stack_page = (unsigned long) task_stack_page(p);
	int count = 0;

	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	sp = p->thread.sp;
	pc = MAKE_PC_FROM_RA(p->thread.ra, p->thread.sp);

	do {
		if (sp < stack_page + sizeof(struct task_struct) ||
		    sp >= (stack_page + THREAD_SIZE) ||
		    pc == 0)
			return 0;
		if (!in_sched_functions(pc))
			return pc;

		/* Stack layout: sp-4: ra, sp-3: sp' */

		pc = MAKE_PC_FROM_RA(*(unsigned long*)sp - 4, sp);
		sp = *(unsigned long *)sp - 3;
	} while (count++ < 16);
	return 0;
}

/*
 * xtensa_gregset_t and 'struct pt_regs' are vastly different formats
 * of processor registers.  Besides different ordering,
 * xtensa_gregset_t contains non-live register information that
 * 'struct pt_regs' does not.  Exception handling (primarily) uses
 * 'struct pt_regs'.  Core files and ptrace use xtensa_gregset_t.
 *
 */

void xtensa_elf_core_copy_regs (xtensa_gregset_t *elfregs, struct pt_regs *regs)
{
	unsigned long wb, ws, wm;
	int live, last;

	wb = regs->windowbase;
	ws = regs->windowstart;
	wm = regs->wmask;
	ws = ((ws >> wb) | (ws << (WSBITS - wb))) & ((1 << WSBITS) - 1);

	/* Don't leak any random bits. */

	memset(elfregs, 0, sizeof (elfregs));

	/* Note:  PS.EXCM is not set while user task is running; its
	 * being set in regs->ps is for exception handling convenience.
	 */

	elfregs->pc		= regs->pc;
	elfregs->ps		= (regs->ps & ~(1 << PS_EXCM_BIT));
	elfregs->lbeg		= regs->lbeg;
	elfregs->lend		= regs->lend;
	elfregs->lcount		= regs->lcount;
	elfregs->sar		= regs->sar;
	elfregs->windowstart	= ws;

	live = (wm & 2) ? 4 : (wm & 4) ? 8 : (wm & 8) ? 12 : 16;
	last = XCHAL_NUM_AREGS - (wm >> 4) * 4;
	memcpy(elfregs->a, regs->areg, live * 4);
	memcpy(elfregs->a + last, regs->areg + last, (wm >> 4) * 16);
}

int dump_fpu(void)
{
	return 0;
}

asmlinkage
long xtensa_clone(unsigned long clone_flags, unsigned long newsp,
                  void __user *parent_tid, void *child_tls,
                  void __user *child_tid, long a5,
                  struct pt_regs *regs)
{
        if (!newsp)
                newsp = regs->areg[1];
        return do_fork(clone_flags, newsp, regs, 0, parent_tid, child_tid);
}

/*
 * xtensa_execve() executes a new program.
 */

asmlinkage
long xtensa_execve(char __user *name, char __user * __user *argv,
                   char __user * __user *envp,
                   long a3, long a4, long a5,
                   struct pt_regs *regs)
{
	long error;
	char *filename;
	struct task_struct *tsk = current;

	filename = getname(name);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
		
	/*
	 * coprocessors are released by do_execve() via
	 * flush_thread() called by flush_old_exec().
	 */
	error = do_execve(filename, argv, envp, regs);
	if (error == 0) {
		task_lock(current);
		tsk->ptrace &= ~PT_DTRACE;
		task_unlock(current);
	}
	putname(filename);
out:
	return error;
}

