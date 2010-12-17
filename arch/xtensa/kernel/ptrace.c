
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2008  Tensilica Inc.
 *
 * Joe Taylor	<joe@tensilica.com>
 * Chris Zankel <chris@zankel.net>
 * Scott Foehner<sfoehner@yahoo.com>,
 * Kevin Chea
 * Marc Gauthier<marc@tensilica.com> <marc@alumni.uwaterloo.ca>
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/smp.h>
#include <linux/security.h>
#include <linux/signal.h>
#include <linux/hardirq.h>
#include <linux/autoconf.h>

#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/ptrace.h>
#include <asm/elf.h>
#include <asm/coprocessor.h>

/*
 * Called by kernel/ptrace.c when detaching to disable single stepping.
 */

void ptrace_disable(struct task_struct *child)
{
	/* Nothing to do.. */
}

int ptrace_getregs(struct task_struct *child, void __user *uregs)
{
	struct pt_regs *regs = task_pt_regs(child);
	xtensa_gregset_t __user *gregset = uregs;
	/* unsigned long wm = regs->wmask; */
	unsigned long wb = regs->windowbase;
	int /* live, */ i;

	if (!access_ok(VERIFY_WRITE, uregs, sizeof(xtensa_gregset_t)))
		return -EIO;

	__put_user(regs->pc, &gregset->pc);
	__put_user(regs->ps & ~(1 << PS_EXCM_BIT), &gregset->ps);
	__put_user(regs->lbeg, &gregset->lbeg);
	__put_user(regs->lend, &gregset->lend);
	__put_user(regs->lcount, &gregset->lcount);
	__put_user(regs->windowstart, &gregset->windowstart);
	__put_user(regs->windowbase, &gregset->windowbase);

	for (i = 0; i < XCHAL_NUM_AREGS; i++)
		__put_user(regs->areg[i],gregset->a+((wb*4+i)%XCHAL_NUM_AREGS));
#if 0
	live = (wm & 2) ? 4 : (wm & 4) ? 8 : (wm & 8) ? 12 : 16;

	for (i = 0; i < live; i++)
		__put_user(regs->areg[i],gregset->a+((wb*4+i)%XCHAL_NUM_AREGS));
	for (i = XCHAL_NUM_AREGS - (wm >> 4) * 4; i < XCHAL_NUM_AREGS; i++)
		__put_user(regs->areg[i],gregset->a+((wb*4+i)%XCHAL_NUM_AREGS));
#endif

	return 0;
}

int ptrace_setregs(struct task_struct *child, void __user *uregs)
{
	struct pt_regs *regs = task_pt_regs(child);
	xtensa_gregset_t *gregset = uregs;
	const unsigned long ps_mask = PS_CALLINC_MASK | PS_OWB_MASK;
	unsigned long ps;
	unsigned long wb, ws;

	if (!access_ok(VERIFY_WRITE, uregs, sizeof(xtensa_gregset_t)))
		return -EIO;

	__get_user(regs->pc, &gregset->pc);
	__get_user(ps, &gregset->ps);
	__get_user(regs->lbeg, &gregset->lbeg);
	__get_user(regs->lend, &gregset->lend);
	__get_user(regs->lcount, &gregset->lcount);
	__get_user(ws, &gregset->windowstart);
	__get_user(wb, &gregset->windowbase);

	regs->ps = (regs->ps & ~ps_mask) | (ps & ps_mask) | (1 << PS_EXCM_BIT);

	if (wb >= XCHAL_NUM_AREGS / 4)
		return -EFAULT;

	if (wb != regs->windowbase || ws != regs->windowstart) {
		unsigned long rotws, wmask;

		rotws = (((ws | (ws << WSBITS)) >> wb) & ((1 << WSBITS) - 1)) & ~1;
		wmask = ((rotws ? WSBITS + 1 - ffs(rotws) : 0) << 4) | (rotws & 0xF) | 1;
		/*printk("** Changed wb %lu->%lu, ws 0x%lx->0x%lx, wmask 0x%lx->0x%lx (rotws=0x%x)\n",
			regs->windowbase, wb, regs->windowstart, ws, regs->wmask, wmask, rotws);*/
		regs->windowbase = wb;
		regs->windowstart = ws;
		regs->wmask = wmask;
	}

	if (wb != 0 &&  __copy_from_user(regs->areg + XCHAL_NUM_AREGS - wb * 4,
					 gregset->a, wb * 16))
		return -EFAULT;

	if (__copy_from_user(regs->areg, gregset->a + wb*4, (WSBITS-wb) * 16))
		return -EFAULT;

	return 0;
}


int ptrace_getxregs(struct task_struct *child, void __user *uregs)
{
	struct pt_regs *regs = task_pt_regs(child);
	struct thread_info *ti = task_thread_info(child);
	elf_xtregs_t __user *xtregs = uregs;
	int ret = 0;

	if (!access_ok(VERIFY_WRITE, uregs, sizeof(elf_xtregs_t)))
		return -EIO;

#if XTENSA_HAVE_COPROCESSORS
	manage_coprocessors(child, CP_FLUSH_ALL);

	/*                          to              from  */
	ret |= __copy_to_user(&xtregs->cp0, &ti->xtregs_cp,
			      sizeof(xtregs_coprocessor_t));
#endif	/* XTENSA_HAVE_COPROCESSORS */

	ret |= __copy_to_user(&xtregs->opt, &regs->xtregs_opt,
			      sizeof(xtregs->opt));
	ret |= __copy_to_user(&xtregs->user,&ti->xtregs_user,
			      sizeof(xtregs->user));

	return ret ? -EFAULT : 0;
}

int ptrace_setxregs(struct task_struct *child, void __user *uregs)
{
	struct thread_info *ti = task_thread_info(child);
	struct pt_regs *regs = task_pt_regs(child);
	elf_xtregs_t *xtregs = uregs;
	int ret = 0;

#if XTENSA_HAVE_COPROCESSORS
	/* Flush and release all coprocessors before we overwrite CP registers. */
	manage_coprocessors(child, CP_FLUSH_AND_RELEASE_ALL);

	/*                             to            from   */
	ret |= __copy_from_user(&ti->xtregs_cp, &xtregs->cp0, 
				sizeof(xtregs_coprocessor_t));
#endif
	ret |= __copy_from_user(&regs->xtregs_opt, &xtregs->opt,
				sizeof(xtregs->opt));
				
	ret |= __copy_from_user(&ti->xtregs_user, &xtregs->user,
				sizeof(xtregs->user));

	return ret ? -EFAULT : 0;
}

int ptrace_peekusr(struct task_struct *child, long regno, long __user *ret)
{
	struct pt_regs *regs;
	unsigned long tmp;

	regs = task_pt_regs(child);
	tmp = 0;  /* Default return value. */

	switch(regno) {

		case REG_AR_BASE ... REG_AR_BASE + XCHAL_NUM_AREGS - 1:
			tmp = regs->areg[regno - REG_AR_BASE];
			break;

		case REG_A_BASE ... REG_A_BASE + 15:
			tmp = regs->areg[regno - REG_A_BASE];
			break;

		case REG_PC:
			tmp = regs->pc;
			break;

		case REG_PS:
			/* Note:  PS.EXCM is not set while user task is running;
			 * its being set in regs is for exception handling
			 * convenience.  */
			tmp = (regs->ps & ~(1 << PS_EXCM_BIT));
			break;

		case REG_WB:
			break;		/* tmp = 0 */

		case REG_WS:
		{
			unsigned long wb = regs->windowbase;
			unsigned long ws = regs->windowstart;
			tmp = ((ws>>wb) | (ws<<(WSBITS-wb))) & ((1<<WSBITS)-1);
			break;
		}
		case REG_LBEG:
			tmp = regs->lbeg;
			break;

		case REG_LEND:
			tmp = regs->lend;
			break;

		case REG_LCOUNT:
			tmp = regs->lcount;
			break;

		case REG_SAR:
			tmp = regs->sar;
			break;

		case SYSCALL_NR:
			tmp = regs->syscall;
			break;

		default:
			return -EIO;
	}
	return put_user(tmp, ret);
}

int ptrace_pokeusr(struct task_struct *child, long regno, long val)
{
	struct pt_regs *regs;
	regs = task_pt_regs(child);

	switch (regno) {
		case REG_AR_BASE ... REG_AR_BASE + XCHAL_NUM_AREGS - 1:
			regs->areg[regno - REG_AR_BASE] = val;
			break;

		case REG_A_BASE ... REG_A_BASE + 15:
			regs->areg[regno - REG_A_BASE] = val;
			break;

		case REG_PC:
			regs->pc = val;
			break;

		case SYSCALL_NR:
			regs->syscall = val;
			break;

		default:
			return -EIO;
	}
	return 0;
}

long arch_ptrace(struct task_struct *child, long request, long addr, long data)
{
	int ret = -EPERM;

	switch (request) {
	case PTRACE_PEEKTEXT:	/* read word at location addr. */
	case PTRACE_PEEKDATA:
		ret = generic_ptrace_peekdata(child, addr, data);
		break;

	case PTRACE_PEEKUSR:	/* read register specified by addr. */
		ret = ptrace_peekusr(child, addr, (void __user *) data);
		break;

	case PTRACE_POKETEXT:	/* write the word at location addr. */
	case PTRACE_POKEDATA:
		ret = generic_ptrace_pokedata(child, addr, data);
		break;

	case PTRACE_POKEUSR:	/* write register specified by addr. */
		ret = ptrace_pokeusr(child, addr, data);
		break;

	/* continue and stop at next (return from) syscall */

	case PTRACE_SYSCALL:
	case PTRACE_CONT: /* restart after signal. */
	{
		ret = -EIO;
		if (!valid_signal(data))
			break;
		if (request == PTRACE_SYSCALL)
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		else
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		child->exit_code = data;
		/* Make sure the single step bit is not set. */
		child->ptrace &= ~PT_SINGLESTEP;
		wake_up_process(child);
		ret = 0;
		break;
	}

	/*
	 * make the child exit.  Best I can do is send it a sigkill.
	 * perhaps it should be put in the status that it wants to
	 * exit.
	 */
	case PTRACE_KILL:
		ret = 0;
		if (child->exit_state == EXIT_ZOMBIE)	/* already dead */
			break;
		child->exit_code = SIGKILL;
		child->ptrace &= ~PT_SINGLESTEP;
		wake_up_process(child);
		break;

	case PTRACE_SINGLESTEP:
		ret = -EIO;
		if (!valid_signal(data))
			break;
		clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		child->ptrace |= PT_SINGLESTEP;
		child->exit_code = data;
		wake_up_process(child);
		ret = 0;
		break;

	case PTRACE_GETREGS:
		ret = ptrace_getregs(child, (void __user *) data);
		break;

	case PTRACE_SETREGS:
		ret = ptrace_setregs(child, (void __user *) data);
		break;

	case PTRACE_GETXTREGS:
		ret = ptrace_getxregs(child, (void __user *) data);
		break;

	case PTRACE_SETXTREGS:
		ret = ptrace_setxregs(child, (void __user *) data);
		break;

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

void do_syscall_trace(void)
{
	/*
	 * The 0x80 provides a way for the tracing parent to distinguish
	 * between a syscall stop and SIGTRAP delivery
	 */
	ptrace_notify(SIGTRAP|((current->ptrace & PT_TRACESYSGOOD) ? 0x80 : 0));

	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}

#if defined(CONFIG_DEBUG_KERNEL) && defined(CONFIG_PREEMPT)
/*
 * It use to be common with UNIX kernels to 
 * check for premption being enabled when returning
 * from a system call. We curretly only have
 * two cases of premption being disabled here;
 * both likely during early init.
 *
 * The kernel doesn't currenty detect a spinlock
 * being left locked on returning to user space.
 * Detecting it here would make it's correction easier.
 * It's possible when PREEMPTION is enabled.
 */ 
int do_syscall_trace_enter_prempt = 0;
int do_syscall_trace_enter_not_prempt = 0;

void do_syscall_trace_enter_not_prempt_bp(void) {}
void do_syscall_trace_enter_prempt_bp(void) {}
#endif

void do_syscall_trace_enter(struct pt_regs *regs)
{
#if defined(CONFIG_DEBUG_KERNEL) && defined(CONFIG_PREEMPT)
	UNUSED struct task_struct *tsk = current;
	int prempt = preemptible();

	if (prempt) {
		do_syscall_trace_enter_prempt_bp();
		do_syscall_trace_enter_prempt++;
	} else {
		do_syscall_trace_enter_not_prempt_bp();
		if (do_syscall_trace_enter_not_prempt++ >= 2) {
			printk(KERN_WARNING "%s: do_syscall_trace_enter_not_prempt:%d > 2\n", __func__,
			                         do_syscall_trace_enter_not_prempt);
		}
	}
#endif
	if (test_thread_flag(TIF_SYSCALL_TRACE)
			&& (current->ptrace & PT_PTRACED))
		do_syscall_trace();

#if 0
	if (unlikely(current->audit_context))
		audit_syscall_entry(current, AUDIT_ARCH_XTENSA..);
#endif
}

void do_syscall_trace_leave(struct pt_regs *regs)
{
	if ((test_thread_flag(TIF_SYSCALL_TRACE))
			&& (current->ptrace & PT_PTRACED))
		do_syscall_trace();
}

