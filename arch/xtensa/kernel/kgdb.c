/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

/*
 * Copyright (C) 2004 Amit S. Kale <amitkale@linsyssoft.com>
 * Copyright (C) 2000-2001 VERITAS Software Corporation.
 * Copyright (C) 2001-2002  Scott Foehner, Tensilica Inc.
 * Copyright (C) 2002 Andi Kleen, SuSE Labs
 * Copyright (C) 2004 LinSysSoft Technologies Pvt. Ltd.
 * Copyright (C) 2004-2005, 2007 MontaVista Software Inc.
 * Copyright (C) 2007-2008 Jason Wessel, Wind River Systems, Inc.
 * Copyright (C) 2008 Pete Delaney, Tensilica Inc.
 */

/****************************************************************************
 *  Contributor:     Lake Stevens Instrument Division$
 *  Written by:      Glenn Engel $
 *  Written by:	     Scott Foehner <sfoehner@yahoo.com>
 *  Written by:      Manish Lachwani, mlachwani@mvista.com
 *  Written by:	     Pete Delaney <piet@tensilica.com>
 *  Updates by:	     Amit Kale<akale@veritas.com>
 *  Updates by:	     Tom Rini <trini@kernel.crashing.org>
 *  Updates by:	     Jason Wessel <jason.wessel@windriver.com>
 *  Updates by:	     Jim Kingdon, Cygnus Support (i386).
 *  Origial kgdb:    2.1.xx Kernel compatibility by David Grothe <dave@gcom.com>
 *  Integrated into 2.2.5 kernel by Tigran Aivazian <tigran@sco.com>
 *  X86_64 changes from Andi Kleen's patch merged by Jim Houston
 */
#include <linux/spinlock.h>
#include <linux/kdebug.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/kgdb.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/nmi.h>

#include <asm/system.h>

#include <asm/processor.h>
#include <asm/regs.h>

#if 0
#include <mach_ipi.h>
#endif

/*
 * Put the error code here just in case the user cares:
 */
static int gdb_xtensa_errcode;

/*
 * Likewise, the exception cause (vector) number here (since GDB only gets 
 * the signal number through the usual means, and that's not very specific):
 */
static int gdb_xtensa_exc_cause = -1;

/*
 * local functions
 */

/* Use EPS[DEBUGLEVEL]. */

static inline unsigned long get_ps(void)
{
	unsigned long ps;
	__asm__ __volatile__ (" rsr %0, "__stringify(EPS)
			      "+"__stringify(XCHAL_DEBUGLEVEL)
			      : "=a" (ps));
	return ps;
}

static inline void set_ps(unsigned long ps)
{
	__asm__ __volatile__ (" wsr %0, "__stringify(EPS)
			      "+"__stringify(XCHAL_DEBUGLEVEL)
			      : : "a" (ps));
}

static inline unsigned long get_excsave(void)
{
	unsigned long excsave;
	__asm__ __volatile__ (" rsr %0, "__stringify(EXCSAVE)
			      "+"__stringify(XCHAL_DEBUGLEVEL)
			      : "=a" (excsave));
	return excsave;
}
static inline void set_excsave(unsigned long excsave)
{
	__asm__ __volatile__ (" wsr %0, "__stringify(EXCSAVE)
			      "+"__stringify(XCHAL_DEBUGLEVEL)
			      : : "a" (excsave));
}

/**
 *	pt_regs_to_gdb_regs - Convert ptrace regs to GDB regs
 *	@gdb_regs: A pointer to hold the registers in the order GDB wants.
 *	@regs: The &struct pt_regs of the current process.
 *
 *	Convert the pt_regs in @regs into the format for registers that
 *	GDB expects, stored in @gdb_regs.
 */
void pt_regs_to_gdb_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
	struct xtensa_gdb_registers *gregs;
	int reg;

	gregs = (struct xtensa_gdb_registers*)gdb_regs;

	/* Clear gdb register structure. */

	memset(gregs, sizeof (gregs), 0);

	/* Copy address register values. */

	if (user_mode(regs)) {

		unsigned long *areg = &gregs->ar0;
		unsigned long wb = regs->windowbase;
		const int frames = regs->wmask >> 4;

		/* Copy the current 16 registers */
		
		for (reg = 0; reg < 16; reg++) {
			int idx = (reg + wb * 4) & (XCHAL_NUM_AREGS - 1);
			areg[idx] = regs->areg[reg];
		}

		/* Copy the remaining registers. */

		for (reg = 1; reg <= frames * 4; reg++) {
			int idx = (wb * 4 - reg) & (XCHAL_NUM_AREGS - 1);
			areg[idx] = regs->areg[XCHAL_NUM_AREGS - reg];
		}

		gregs->windowbase = regs->windowbase;
		gregs->windowstart = regs->windowstart;

	} else {
		/*
		 * All register windows have been flushd, 
		 * so we only have to copy 16 regs.
		 * Windowsbase and windowstart weren't saved.
		 */

	 	unsigned long *areg = &gregs->ar0;

		for (reg = 0; reg < 16; reg++)
			areg[reg] = regs->areg[reg];
		gregs->windowbase = 0;
		gregs->windowstart = 1;
	}

	gregs->lbeg = regs->lbeg;
	gregs->lend = regs->lend;
	gregs->lcount = regs->lcount;

	gregs->exccause = get_excsave();
	gregs->depc = regs->depc;
	gregs->excvaddr = regs->excvaddr;
	gregs->sar = regs->sar;

	gregs->pc = regs->pc;
	gregs->ps = get_ps();

	return;
}

/**
 *	sleeping_thread_to_gdb_regs - Convert ptrace regs to GDB regs
 *	@gdb_regs: A pointer to hold the registers in the order GDB wants.
 *	@p: The &struct task_struct of the desired process.
 *
 *	Convert the register values of the sleeping process in @p to
 *	the format that GDB expects.
 *	This function is called when kgdb does not have access to the
 *	&struct pt_regs and therefore it should fill the gdb registers
 *	@gdb_regs with what has	been saved in &struct thread_struct
 *	thread field during switch_to.
 *
 *      REMIND: Does it help to copy regs from pt_regs like mips and arm?
 */
void 
sleeping_thread_to_gdb_regs(unsigned long *gdb_regs, struct task_struct *p)
{
	struct thread_struct *thread = &p->thread;
	struct xtensa_gdb_registers *gregs;

	/* Clear gdb register structure. */
	gregs = (struct xtensa_gdb_registers *)gdb_regs;
	memset(gregs, sizeof (gregs), 0);

	gregs->pc = thread->ra;
	gregs->sp = thread->sp;
	gregs->ibreaka0 = thread->ibreak[0];
	gregs->ibreaka1 = thread->ibreak[1];
	gregs->dbreaka0 = thread->dbreaka[0];
	gregs->dbreaka1 = thread->dbreaka[1];
	gregs->dbreakc0 = thread->dbreakc[0];
	gregs->dbreakc1 = thread->dbreakc[1];
}

/**
 *	gdb_regs_to_pt_regs - Convert GDB regs to ptrace regs.
 *	@gdb_regs: A pointer to hold the registers we've received from GDB.
 *	@regs: A pointer to a &struct pt_regs to hold these values in.
 *
 *	Convert the GDB regs in @gdb_regs into the pt_regs, and store them
 *	in @regs.
 */
void gdb_regs_to_pt_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
	struct xtensa_gdb_registers *gregs;
	int reg;

	gregs = (struct xtensa_gdb_registers*)gdb_regs;

	/* Copy address register values. */
	
	if (user_mode(regs)) {

		unsigned long *areg = &gregs->ar0;
		unsigned long wb = gregs->windowbase;

		/* Copy all registers */
		
		for (reg = 0; reg < XCHAL_NUM_AREGS; reg++) {
			int idx = (reg + wb *4) & (XCHAL_NUM_AREGS - 1);
			regs->areg[reg] = areg[idx];
		}

		regs->windowbase = gregs->windowbase;
		regs->windowstart = gregs->windowstart;

	} else {

		/*
		 * All register windows have been flushd, 
		 * so we only have to copy 16 regs.
		 * Windowsbase and windowstart weren't saved.
		 */

	 	unsigned long *areg = &gregs->ar0;

		for (reg = 0; reg < 16; reg++)
			regs->areg[reg] = areg[reg];
	}

	regs->lbeg = gregs->lbeg;
	regs->lend = gregs->lend;
	regs->lcount = gregs->lcount;

	regs->exccause = gregs->exccause;
	regs->depc = gregs->depc;
	regs->excvaddr = gregs->excvaddr;
	regs->sar = gregs->sar;

	regs->pc = gregs->pc;
	set_ps(gregs->ps);

	return;


#if 0
	unsigned long *ptr = gdb_regs;
	int reg = 0;

	for (reg = 0; reg <= 15 ; reg++)
                regs->areg[reg] = *(ptr++);

	for (reg = 16; reg <= 63; reg++)
		ptr++;

	regs->lbeg = *(ptr++);
	regs->lend = *(ptr++);
	regs->lcount = *(ptr++);

	for (reg = 0; reg <= 21; reg++)
		ptr++;

	regs->exccause = *(ptr++);
	regs->depc = *(ptr++);
	regs->excvaddr = *(ptr++);
	regs->windowbase = *(ptr++);
	regs->windowstart = *(ptr++);
	regs->sar = *(ptr++);

	ptr++;

	regs->ps = *(ptr++);

	for (reg = 0; reg <= 9; reg++)
		ptr++;

	regs->icountlevel = *(ptr++);
	regs->debugcause = *(ptr++);

	ptr++;
	ptr++;
	ptr++;

	regs->pc = *(ptr++);

	for (reg = 0; reg <= 31; reg++)
		ptr++;

	return;
#endif
}

/*
 * REG_GDB_* are defined in ptrace.h
 */
int 
kgdb_read_reg(unsigned long regno, char *output_buffer, struct pt_regs *regs)
{
	int ar_reg;
	extern int find_first_pane(int, int);

	switch(regno) {
		case REG_GDB_AR_BASE ... REG_GDB_AR_BASE + XCHAL_NUM_AREGS - 1:
			ar_reg = ( (regno - regs->windowbase * 4 - 
				    REG_GDB_AR_BASE) & (XCHAL_NUM_AREGS - 1));

			kgdb_mem2hex((char *)&(regs->areg[ar_reg]), 
							output_buffer, 4);
			break;

		case REG_GDB_PC:
			kgdb_mem2hex((char *)&(regs->pc), output_buffer, 4);
			break;
                                                                                      
                case REG_GDB_PS:
			/* Re-create PS, set WOE and keep PS.CALLING */
			{
				unsigned long ps = get_ps();
				kgdb_mem2hex((char *)&ps, output_buffer, 4);
			}
			break;
                                                                                      
                case REG_GDB_WB:
			kgdb_mem2hex((char *)&(regs->windowbase), 
							output_buffer, 4);
			break;
                                                                                      
                case REG_GDB_WS:
			kgdb_mem2hex((char *)&(regs->windowstart), 
							output_buffer, 4);
			break;
                                                                                      
                case REG_GDB_LBEG:
			kgdb_mem2hex((char *)&(regs->lbeg), output_buffer, 4);
			break;
                                                                                      
                case REG_GDB_LEND:
			kgdb_mem2hex((char *)&(regs->lend), output_buffer, 4);
			break;
                                                                                      
		case REG_GDB_LCOUNT:
			kgdb_mem2hex((char *)&(regs->lcount), output_buffer, 4);
			break;
                                                                                      
		case REG_GDB_SAR:
			kgdb_mem2hex((char *)&(regs->sar), output_buffer, 4);
			break;

		case REG_GDB_DEPC:
			kgdb_mem2hex((char *)&(regs->depc), output_buffer, 4);
			break;
                                                                                      
		case REG_GDB_EXCCAUSE:
			kgdb_mem2hex((char *)&(regs->exccause), 
							output_buffer, 4);
			break;
                                                                                      
		case REG_GDB_EXCVADDR:
			kgdb_mem2hex((char *)&(regs->excvaddr), 
							output_buffer, 4);
			break;
                                                                                      
		default:
			strcpy(output_buffer,"ffffffff");
			break;
        }
	output_buffer[0] = 0;
	return -1;
}


/*
 * Instruction Breakoints and Data Watchpoints
 */
static struct hw_ibreakpoint {
	unsigned		enabled;
	unsigned long		addr;
} ibreakinfo[XCHAL_NUM_IBREAK];

static struct hw_dbreakpoint {
	unsigned		enabled;
	unsigned long		ctrl;
	unsigned long		addr;
} dbreakinfo[XCHAL_NUM_IBREAK];

/*
 * Update Instruction Breakpoint Registers:
 *        -----------
 */
static void kgdb_correct_hw_ibreak(void)
{
	unsigned long ibreakenable;
	int correctit = 0;
	int breakbit;
	int breakno;

	ibreakenable = get_sr(IBREAKENABLE);
	for (breakno = 0; breakno < XCHAL_NUM_IBREAK; breakno++) {
		breakbit = 1 << breakno;
		if (!(ibreakenable & breakbit) && ibreakinfo[breakno].enabled) {
			correctit = 1;
			ibreakenable |= breakbit;
			set_debugreg(ibreakinfo[breakno].addr, 
							IBREAKA + breakno);

		} else {
			if ((ibreakenable & breakbit) && 
			    !ibreakinfo[breakno].enabled) {
				correctit = 1;
				ibreakenable &= ~breakbit;
			}
		}
	}
	if (correctit) {
		set_sr(ibreakenable, IBREAKENABLE);
		asm("isync");
	}
}

/*
 * Update Data Hardware Breakpoint Registers:
 *        ----
 */
static void kgdb_correct_hw_dbreak(void)
{
	unsigned long cur_dbreakc;
	unsigned long cur_dbreaka;
	unsigned long new_dbreakc;
	unsigned long new_dbreaka;
	int updates = 0;
	int enabled;
	int breakno;

	for (breakno = 0; breakno < XCHAL_NUM_DBREAK; breakno++) {
		cur_dbreaka = get_debugreg(DBREAKA + breakno);
		cur_dbreakc = get_debugreg(DBREAKC + breakno);
		if (dbreakinfo[breakno].enabled) {
			enabled = 1;
			new_dbreakc = dbreakinfo[breakno].ctrl;
			new_dbreaka = dbreakinfo[breakno].addr;

		} else {
			enabled = 0;
			new_dbreakc = 0X00000000;
			new_dbreaka = 0X00000000;
		}
		if (!enabled && (new_dbreakc != cur_dbreakc)) {
			set_debugreg(new_dbreakc, DBREAKC + breakno);
			updates++;
		}
		if (new_dbreaka != cur_dbreaka) {
			set_debugreg(new_dbreaka, DBREAKA + breakno);
			updates++;
		}
		if (enabled && (new_dbreakc != cur_dbreakc)) {
			set_debugreg(new_dbreakc, DBREAKC + breakno);
			updates++;
		}
	}
	if (updates) {
		asm("isync");
	}
}

static void kgdb_correct_hw_breaks(void)
{
	kgdb_correct_hw_ibreak();
	kgdb_correct_hw_dbreak();
}

static int
kgdb_remove_hw_break(unsigned long addr, int len, enum kgdb_bptype bptype)
{
	int i;

	switch(bptype) {
	case BP_HARDWARE_BREAKPOINT:
		for (i = 0; i < XCHAL_NUM_IBREAK; i++)
			if (ibreakinfo[i].addr == addr && 
						ibreakinfo[i].enabled) {
				ibreakinfo[i].enabled = 0;
				return(0);
		}
		break;
	
	case BP_WRITE_WATCHPOINT:
	case BP_READ_WATCHPOINT:
	case BP_ACCESS_WATCHPOINT:
		for (i = 0; i < XCHAL_NUM_DBREAK; i++)
			if (dbreakinfo[i].addr == addr && 
						dbreakinfo[i].enabled) {
				dbreakinfo[i].enabled = 0;
				return(0);
		}
		break;

	case BP_BREAKPOINT:	/* Software Breakpoint */
	default:
		panic(__func__);
	}
	return -1;
}

static void kgdb_remove_all_hw_break(void)
{
	int i;

	for (i = 0; i < XCHAL_NUM_IBREAK; i++)
		memset(&ibreakinfo[i], 0, sizeof(struct hw_ibreakpoint));

	for (i = 0; i < XCHAL_NUM_DBREAK; i++)
		memset(&dbreakinfo[i], 0, sizeof(struct hw_dbreakpoint));
}

/*
 * Called via arch_kgdb_ops.set_hw_breakpoint.
 */
static int
kgdb_set_hw_break(unsigned long addr, int len, enum kgdb_bptype bptype)
{
	unsigned long mask = 0;
	unsigned long ctrl = 0;
	int i;

	switch(bptype) {
	case BP_HARDWARE_BREAKPOINT:
		for (i = 0; i < XCHAL_NUM_IBREAK; i++) {
			if (!ibreakinfo[i].enabled)
				break;
		}
		if (i == XCHAL_NUM_IBREAK) 
			return -1;

		ibreakinfo[i].enabled = 1;
		ibreakinfo[i].addr = addr;
		break;
			
	case BP_WRITE_WATCHPOINT:	ctrl += 0x80000000;
	case BP_ACCESS_WATCHPOINT:	ctrl += 0x40000000;
		for (i = 0; i < XCHAL_NUM_DBREAK; i++) {
			if (!dbreakinfo[i].enabled)
				break;
		}
		if (i == XCHAL_NUM_DBREAK)
			return -1;

		switch(len) {
		case 1:  mask = 0x3F; break;
		case 2:  mask = 0X3E; break;
		case 4:  mask = 0X3C; break;
		case 8:  mask = 0X38; break;
		case 16: mask = 0X30; break;
		case 32: mask = 0X20; break;
		case 64: mask = 0x00; break;
		default: panic(__func__);
		}
		dbreakinfo[i].enabled = 1;
		dbreakinfo[i].addr = addr;
		dbreakinfo[i].ctrl = ctrl | mask;
		break;

	default:
		panic(__func__);
	}
	return 0;
}

/**
 *	kgdb_disable_hw_debug - Disable hardware debugging while we in kgdb.
 *	@regs: Current &struct pt_regs.
 *
 *	This function will be called if the particular architecture must
 *	disable hardware debugging while it is processing gdb packets or
 *	handling exception.
 */
void kgdb_disable_hw_debug(struct pt_regs *regs)
{
	/* Disable hardware debugging while we are in kgdb: */
	set_sr(0UL, IBREAKENABLE);
	set_sr(0UL, DBREAKC);
	set_sr(0UL, DBREAKC + 1);
}

/**
 *	kgdb_post_primary_code - Save error vector/code numbers.
 *	@regs: Original pt_regs.
 *	@exc_cause: Original error exception cause directed to vector.S.
 *		    Ex: breakpoint - EXCCAUSE_MAPPED_DEBUG.
 *	@err_code: Original error code; Ex: Debug Cause Register.
 *
 *	This is needed on architectures which support SMP and KGDB.
 *	This function is called after all the slave cpus have been put
 *	to a know spin state and the primary CPU has control over KGDB.
 */
void kgdb_post_primary_code(struct pt_regs *regs, int exc_cause, int err_code)
{
	/* primary processor is completely in the debugger */
	gdb_xtensa_exc_cause = exc_cause;
	gdb_xtensa_errcode = err_code;
}

#ifdef CONFIG_SMP

/*
 * Hook to call generic function kgdb_nmicallback().
 */
static void kgdb_call_nmi_hook(void *ignored)
{
	struct pt_regs *regs = NULL;

        kgdb_nmicallback(raw_smp_processor_id(), regs);
}

/**
 *	kgdb_roundup_cpus - Get other CPUs into a holding pattern
 *	@flags: Current IRQ state
 *
 *	On SMP systems, we need to get the attention of the other CPUs
 *	and get them be in a known state.  This should do what is needed
 *	to get the other CPUs to call kgdb_wait(). Note that on some arches,
 *	the NMI approach is not used for rounding up all the CPUs. For example,
 *	in case of MIPS, smp_call_function() is used to roundup CPUs. In
 *	this case, we have to make sure that interrupts are enabled before
 *	calling smp_call_function(). The argument to this function is
 *	the flags that will be used when restoring the interrupts. There is
 *	local_irq_save() call before kgdb_roundup_cpus().
 *
 *	On non-SMP systems, this is not called.
 */
void kgdb_roundup_cpus(unsigned long flags)
{
	local_irq_enable();
	smp_call_function(kgdb_call_nmi_hook, NULL, 0);
	local_irq_disable();
}
#endif

/**
 *	kgdb_arch_handle_exception - Handle architecture specific GDB packets.
 *	@vector: The error vector of the exception that happened.
 *	@signo: The signal number of the exception that happened.
 *	@err_code: The error code of the exception that happened.
 *	@remcom_in_buffer: The buffer of the packet we have read.
 *	@remcom_out_buffer: The buffer of %BUFMAX bytes to write a packet into.
 *	@regs: The &struct pt_regs of the current process.
 *
 *	This function MUST handle the 'c' and 's' command packets,
 *	as well packets to set / remove a hardware breakpoint, if used.
 *	If there are additional packets which the hardware needs to handle,
 *	they are handled here.  The code should return -1 if it wants to
 *	process more packets, and a %0 or %1 if it wants to exit from the
 *	kgdb callback.
 */
int kgdb_arch_handle_exception(int e_vector, int signo, int err_code,
			       char *remcomInBuffer, char *remcomOutBuffer,
			       struct pt_regs *linux_regs)
{
	unsigned long addr;
	char *ptr;
	int newPC;
	int ret = 0;

	switch (remcomInBuffer[0]) {
	case 'c':	/* cAA..AA    Continue at address AA..AA(optional) */
	case 's':	/* Step to next instruction */
		/* try to read optional parameter, pc unchanged if no parm */
		ptr = &remcomInBuffer[1];
		if (kgdb_hex2long(&ptr, &addr))
			linux_regs->pc = addr;

	case 'D': 	/* kill or detach; treat this like a continue */
	case 'k':	/* remove_all_break */
		newPC = linux_regs->pc;

		/* 
		 * Turn off single stepping by default for now.
		 */
		linux_regs->icountlevel = 0;
		atomic_set(&kgdb_cpu_doing_single_step, -1);

		/* 
		 * Set the instruction count register if we're stepping;
		 * hopefully won't interfear with xt-ocd and it's kgdb.
		 * Need a level between the kernel exception handleing
		 * and NLEVEL.
		 */
		if (remcomInBuffer[0] == 's') {
#if 1
			/*
			 * For now don't enable kgdb debugging
			 * exceptions and interrupts. Hopefully xt-ocd
			 * is preserving our value of icountlevel when
			 * it's using it.
			 */
			linux_regs->icountlevel = 1;
#else
			linux_regs->icountlevel = (linux_regs->ps & 0xf) + 1;
#endif
			kgdb_single_step = 1;
			if (kgdb_contthread) {
				/* 
				 * Keep track of the CPU that's single steping.
				 */
				atomic_set(&kgdb_cpu_doing_single_step,
					   raw_smp_processor_id());
			}
		}
		set_sr(0UL, IBREAKENABLE);
		kgdb_correct_hw_breaks();
		break;

	case 'p':
		ptr = &remcomInBuffer[1];
		kgdb_hex2long(&ptr, &addr);
		ret = kgdb_read_reg(addr, remcomOutBuffer, linux_regs);
		break;

	default:
		/* This means that we do not want to exit from the handler. */
		 ret = -1;
	}
	return(ret);
}

static inline int
single_step_cont(struct pt_regs *regs, struct die_args *args)
{
	/*
	 * Single step exception from kernel space to user space so
	 * eat the exception and continue the process:
	 */
	printk(KERN_ERR "KGDB: trap/step from kernel to user space, "
			"resuming...\n");

	kgdb_arch_handle_exception(args->trapnr, args->signr,
				   args->err, "c", "", regs);

	return NOTIFY_STOP;
}

static int was_in_debug_nmi[NR_CPUS];

/*
 * Calls linux_debug_hook before the kernel dies. If KGDB is enabled,
 * then try to fall into the debugger,
 */
static int __kgdb_xtensa_notify(struct die_args *args, unsigned long cmd)
{
	struct pt_regs *regs = args->regs;

	switch (cmd) {
	case DIE_NMI:
		if (atomic_read(&kgdb_active) != -1) {
			/* KGDB CPU roundup */
			kgdb_nmicallback(raw_smp_processor_id(), regs);
			was_in_debug_nmi[raw_smp_processor_id()] = 1;
			touch_nmi_watchdog();
			return NOTIFY_STOP;
		}
		return NOTIFY_DONE;

	case DIE_NMI_IPI:
		if (atomic_read(&kgdb_active) != -1) {
			/* KGDB CPU roundup */
			kgdb_nmicallback(raw_smp_processor_id(), regs);
			was_in_debug_nmi[raw_smp_processor_id()] = 1;
			touch_nmi_watchdog();
		}
		return NOTIFY_DONE;

	case DIE_NMIUNKNOWN:
		if (was_in_debug_nmi[raw_smp_processor_id()]) {
			was_in_debug_nmi[raw_smp_processor_id()] = 0;
			return NOTIFY_STOP;
		}
		return NOTIFY_DONE;

	case DIE_NMIWATCHDOG:
		if (atomic_read(&kgdb_active) != -1) {
			/* KGDB CPU roundup: */
			kgdb_nmicallback(raw_smp_processor_id(), regs);
			return NOTIFY_STOP;
		}
		/* Enter debugger: */
		break;

	case DIE_DEBUG:
		if (atomic_read(&kgdb_cpu_doing_single_step) ==
			raw_smp_processor_id() &&
			user_mode(regs))
			return single_step_cont(regs, args);
		/* fall through */
	default:
		if (user_mode(regs))
			return NOTIFY_DONE;
	}

	if (kgdb_handle_exception(args->trapnr, args->signr, args->err, regs))
		return NOTIFY_DONE;

	/* Must touch watchdog before return to normal operation. */
	touch_nmi_watchdog();
	return NOTIFY_STOP;
}

/*
 * NOTE: 
 *	die_args.err contains the contents of the debugcause register
 */
static int
kgdb_xtensa_notify(struct notifier_block *self, unsigned long cmd, void *args)
{
	unsigned long flags;
	int ret;

	local_irq_save(flags);
	ret = __kgdb_xtensa_notify(args, cmd);
	local_irq_restore(flags);

	return ret;
}

static struct notifier_block kgdb_notifier = {
	.notifier_call	= kgdb_xtensa_notify,

	/*
	 * Lowest-prio notifier priority, we want to be notified last:
	 */
	.priority	= -INT_MAX,
};

/**
 *	kgdb_arch_init - Perform any architecture specific initalization.
 *
 *	This function will handle the initalization of any architecture
 *	specific callbacks.
 */
int kgdb_arch_init(void)
{
	return register_die_notifier(&kgdb_notifier);
}

/**
 *	kgdb_arch_exit - Perform any architecture specific uninitalization.
 *
 *	This function will handle the uninitalization of any architecture
 *	specific callbacks, for dynamic registration and unregistration.
 */
void kgdb_arch_exit(void)
{
	unregister_die_notifier(&kgdb_notifier);
}

/**
 *
 *	kgdb_skipexception - Bail out of KGDB when we've been triggered.
 *	@exception: Exception vector number
 *	@regs: Current &struct pt_regs.
 *
 *	On some architectures we need to skip a breakpoint exception when
 *	it occurs after a breakpoint has been removed.
 *
 * Skip an int3 exception when it occurs after a breakpoint has been
 * removed. Backtrack eip by 1 since the int3 would have caused it to
 * increment by 1.
 *
 * REMIND: Update for xtensa and consider type of instruction being
 *         used for the breakpoint (BREAK vs ILL).
 *
 *         Put DEBUGCAUSE in pt_regs?
 */
int kgdb_skipexception(int exception_cause, struct pt_regs *regs)
{
	if ((exception_cause == EXCCAUSE_MAPPED_DEBUG) && 
					kgdb_isremovedbreak(regs->pc - 1)) {
		regs->pc -= 1;
		return 1;
	}
	return 0;
}

unsigned long kgdb_arch_pc(int exception_cause, struct pt_regs *regs)
{
	if (exception_cause == EXCCAUSE_MAPPED_DEBUG)
		return instruction_pointer(regs) - 1;
	return instruction_pointer(regs);
}


#ifdef  CONFIG_KGDB_BREAKS_WITH_ILLEGAL_INSTRUCTION
/*
 * Using Illegal Instruction 'ILL' while debugging
 * kgdb with xt-gdb via xt-ocd.
 */
# if XCHAL_HAVE_DENSITY
#  define BREAK_INSTR_SIZE       2
void arch_kgdb_breakpoint(void)
{
	__asm__ __volatile__ (                   \
		".globl break_inst\n\t"          \
		"break_inst:\till.n\n\t"         \
		".globl break_return\n\t"        \
		"break_return:\n\t");
}
#  ifdef __XTENSA_EB__
#   define BPT_INSTRUCTION {0x0F,0x56}    /* Big Endian */	
# else
#   define BPT_INSTRUCTION {0xF0,0x65}    /* Little Endian */
# endif

# else /*!XCHAL_HAVE_DENSITY */

# define BREAK_INSTR_SIZE       3
void arch_kgdb_breakpoint(void)
{
	__asm__ __volatile__ (                   \
		".globl break_inst\n\t"          \
		"break_inst:\till\n\t"           \
		".globl break_return\n\t"        \
		"break_return:\n\t");
}
#  ifdef __XTENSA_EB__
#   define BPT_INSTRUCTION  {0x00, 0x00, 0x00}   /* Big Endian */
#  else
   define BPT_INSTRUCTION   {0x00, 0x00, 0x00}  /* Little Endian */
#  endif 
# endif

#else /* !CONFIG_KGDB_BREAKS_WITH_ILLEGAL_INSTRUCTION */

/*
 * Using Standard Break Instruction
 */
# if XCHAL_HAVE_DENSITY
#  define BREAK_INSTR_SIZE       2
void arch_kgdb_breakpoint(void)
{
	__asm__ __volatile__ (                \
		".globl break_inst\n\t"       \
		"break_inst:\tbreak.n 0\n\t");
}
#  ifdef __XTENSA_EB__
#    define BPT_INSTRUCTION {0xd2,0x0f}  /* Big Endian */
#  else
#   define BPT_INSTRUCTION {0x2d,0xf0}  /* Little Endian */
#  endif

# else /*!XCHAL_HAVE_DENSITY */

#  define BREAK_INSTR_SIZE       3
void arch_kgdb_breakpoint(void)
{
	__asm__ __volatile__ (                 \
		".globl break_inst\n\t"        \
		"break_inst:\tbreak 0, 0\n\t");
}
#  ifdef __XTENSA_EB__
#   define BPT_INSTRUCTION  {0x00, 0x04, 0x00}   /* Big Endian */
#  else
#   define BPT_INSTRUCTION   {0x00, 0x40, 0x00}  /* Little Endian */
#  endif

# endif /* XCHAL_HAVE_DENSITY */ 
#endif /* CONFIG_KGDB_BREAKS_WITH_ILLEGAL_INSTRUCTION */

struct kgdb_arch arch_kgdb_ops = {
	/* Breakpoint instruction: */
	.gdb_bpt_instr		= BPT_INSTRUCTION,
	.flags			= KGDB_HW_BREAKPOINT,
	.set_hw_breakpoint	= kgdb_set_hw_break,
	.remove_hw_breakpoint	= kgdb_remove_hw_break,
	.remove_all_hw_break	= kgdb_remove_all_hw_break,
	.correct_hw_break	= kgdb_correct_hw_breaks,
};
