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

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/smp.h>
#include <linux/security.h>
#include <linux/signal.h>

#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/uaccess.h>
#include <asm/ptrace.h>
#include <asm/elf.h>
#include <asm/coprocessor.h>


int ptrace_getregs(struct task_struct *child, void __user *uregs)
{
	struct pt_regs *regs = task_pt_regs(child);
	xtensa_gregset_t __user *gregset = uregs;
	unsigned long wb = regs->windowbase;
	int i;

	if (!access_ok(VERIFY_WRITE, uregs, sizeof(xtensa_gregset_t)))
		return -EIO;

	__put_user(regs->pc, &gregset->pc);
	__put_user(regs->ps & ~(1 << PS_EXCM_BIT), &gregset->ps);
	__put_user(regs->lbeg, &gregset->lbeg);
	__put_user(regs->lend, &gregset->lend);
	__put_user(regs->lcount, &gregset->lcount);
	__put_user(regs->windowstart, &gregset->windowstart);
	__put_user(regs->windowbase, &gregset->windowbase);
	__put_user(regs->threadptr, &gregset->threadptr);

	for (i = 0; i < XCHAL_NUM_AREGS; i++)
		__put_user(regs->areg[i],
				gregset->a + ((wb * 4 + i) % XCHAL_NUM_AREGS));

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
	__get_user(regs->threadptr, &gregset->threadptr);

	regs->ps = (regs->ps & ~ps_mask) | (ps & ps_mask) | (1 << PS_EXCM_BIT);

	if (wb >= XCHAL_NUM_AREGS / 4)
		return -EFAULT;

	if (wb != regs->windowbase || ws != regs->windowstart) {
		unsigned long rotws, wmask;

		rotws = (((ws | (ws << WSBITS)) >> wb) &
				((1 << WSBITS) - 1)) & ~1;
		wmask = ((rotws ? WSBITS + 1 - ffs(rotws) : 0) << 4) |
			(rotws & 0xF) | 1;
		regs->windowbase = wb;
		regs->windowstart = ws;
		regs->wmask = wmask;
	}

	if (wb != 0 &&  __copy_from_user(regs->areg + XCHAL_NUM_AREGS - wb * 4,
				gregset->a, wb * 16))
		return -EFAULT;

	if (__copy_from_user(regs->areg, gregset->a + wb * 4,
				(WSBITS - wb) * 16))
		return -EFAULT;

	return 0;
}

int ptrace_peekusr(struct task_struct *child, long regno, long __user *ret)
{
	struct pt_regs *regs;
	unsigned long tmp;

	regs = task_pt_regs(child);
	tmp = 0;  /* Default return value. */

	switch(regno) {
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
		}
		break;

	default:
		return ptrace_peekusr_common(child, regno, ret);
	}
	return put_user(tmp, ret);
}
