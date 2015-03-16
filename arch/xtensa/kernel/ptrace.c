// TODO some minor issues
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2007  Tensilica Inc.
 *
 * Joe Taylor	<joe@tensilica.com, joetylr@yahoo.com>
 * Chris Zankel <chris@zankel.net>
 * Scott Foehner<sfoehner@yahoo.com>,
 * Kevin Chea
 * Marc Gauthier<marc@tensilica.com> <marc@alumni.uwaterloo.ca>
 */

#include <linux/audit.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/smp.h>
#include <linux/security.h>
#include <linux/signal.h>
#include <linux/tracehook.h>

#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/uaccess.h>
#include <asm/ptrace.h>
#include <asm/elf.h>
#include <asm/coprocessor.h>


void user_enable_single_step(struct task_struct *child)
{
	child->ptrace |= PT_SINGLESTEP;
}

void user_disable_single_step(struct task_struct *child)
{
	child->ptrace &= ~PT_SINGLESTEP;
}

/*
 * Called by kernel/ptrace.c when detaching to disable single stepping.
 */

void ptrace_disable(struct task_struct *child)
{
	/* Nothing to do.. */
}

static int ptrace_getxregs(struct task_struct *child, void __user *uregs)
{
	struct pt_regs *regs = task_pt_regs(child);
	struct thread_info *ti = task_thread_info(child);
	elf_xtregs_t __user *xtregs = uregs;
	int ret = 0;

	if (!access_ok(VERIFY_WRITE, uregs, sizeof(elf_xtregs_t)))
		return -EIO;

#if XTENSA_HAVE_COPROCESSORS
	/* Flush all coprocessor registers to memory. */
	coprocessor_flush_all(ti);
	ret |= __copy_to_user(&xtregs->cp0, &ti->xtregs_cp,
			      sizeof(xtregs_coprocessor_t));
#endif
	ret |= __copy_to_user(&xtregs->opt, &regs->xtregs_opt,
			      sizeof(xtregs->opt));
	ret |= __copy_to_user(&xtregs->user,&ti->xtregs_user,
			      sizeof(xtregs->user));

	return ret ? -EFAULT : 0;
}

static int ptrace_setxregs(struct task_struct *child, void __user *uregs)
{
	struct thread_info *ti = task_thread_info(child);
	struct pt_regs *regs = task_pt_regs(child);
	elf_xtregs_t *xtregs = uregs;
	int ret = 0;

	if (!access_ok(VERIFY_READ, uregs, sizeof(elf_xtregs_t)))
		return -EFAULT;

#if XTENSA_HAVE_COPROCESSORS
	/* Flush all coprocessors before we overwrite them. */
	coprocessor_flush_all(ti);
	coprocessor_release_all(ti);

	ret |= __copy_from_user(&ti->xtregs_cp, &xtregs->cp0,
				sizeof(xtregs_coprocessor_t));
#endif
	ret |= __copy_from_user(&regs->xtregs_opt, &xtregs->opt,
				sizeof(xtregs->opt));
	ret |= __copy_from_user(&ti->xtregs_user, &xtregs->user,
				sizeof(xtregs->user));

	return ret ? -EFAULT : 0;
}

int ptrace_peekusr_common(struct task_struct *child, long regno,
			  long __user *ret)
{
	struct pt_regs *regs;
	unsigned long tmp;

	regs = task_pt_regs(child);
	tmp = 0;  /* Default return value. */

	switch(regno) {

		case REG_AR_BASE ... REG_AR_BASE + XCHAL_NUM_AREGS - 1:
			tmp = pt_areg(regs, regno - REG_AR_BASE);
			break;

		case REG_A_BASE ... REG_A_BASE + 15:
			tmp = pt_areg(regs, regno - REG_A_BASE);
			break;

		case REG_PC:
			tmp = regs->pc;
			break;

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

static int ptrace_pokeusr(struct task_struct *child, long regno, long val)
{
	struct pt_regs *regs;
	regs = task_pt_regs(child);

	switch (regno) {
		case REG_AR_BASE ... REG_AR_BASE + XCHAL_NUM_AREGS - 1:
			pt_areg(regs, regno - REG_AR_BASE) = val;
			break;

		case REG_A_BASE ... REG_A_BASE + 15:
			pt_areg(regs, regno - REG_A_BASE) = val;
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

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	int ret = -EPERM;
	void __user *datap = (void __user *) data;

	switch (request) {
	case PTRACE_PEEKTEXT:	/* read word at location addr. */
	case PTRACE_PEEKDATA:
		ret = generic_ptrace_peekdata(child, addr, data);
		break;

	case PTRACE_PEEKUSR:	/* read register specified by addr. */
		ret = ptrace_peekusr(child, addr, datap);
		break;

	case PTRACE_POKETEXT:	/* write the word at location addr. */
	case PTRACE_POKEDATA:
		ret = generic_ptrace_pokedata(child, addr, data);
		break;

	case PTRACE_POKEUSR:	/* write register specified by addr. */
		ret = ptrace_pokeusr(child, addr, data);
		break;

	case PTRACE_GETREGS:
		ret = ptrace_getregs(child, datap);
		break;

	case PTRACE_SETREGS:
		ret = ptrace_setregs(child, datap);
		break;

	case PTRACE_GETXTREGS:
		ret = ptrace_getxregs(child, datap);
		break;

	case PTRACE_SETXTREGS:
		ret = ptrace_setxregs(child, datap);
		break;

	default:
		ret = ptrace_request(child, request, addr, data);
		break;
	}

	return ret;
}

unsigned long do_syscall_trace_enter(struct pt_regs *regs)
{
	unsigned long ret = 0;

	if (test_thread_flag(TIF_SYSCALL_TRACE) &&
	    tracehook_report_syscall_entry(regs))
		ret = -1;

	audit_syscall_entry(pt_areg(regs, 2), pt_areg(regs, 6),
			    pt_areg(regs, 3), pt_areg(regs, 4),
			    pt_areg(regs, 5));

	return ret ? -1 : pt_areg(regs, 2);
}

void do_syscall_trace_leave(struct pt_regs *regs)
{
	int step;

	audit_syscall_exit(regs);

	step = test_thread_flag(TIF_SINGLESTEP);
	if (step || test_thread_flag(TIF_SYSCALL_TRACE))
		tracehook_report_syscall_exit(regs, step);
}
