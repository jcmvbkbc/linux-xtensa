/*
 * arch/xtensa/kernel/vectors.S
 *
 * This file contains all exception vectors (user, kernel, and double),
 * as well as the window vectors (overflow and underflow), and the debug
 * vector. These are the primary vectors executed by the processor if an
 * exception occurs.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2005 - 2009 Tensilica, Inc.
 *
 * Chris Zankel <chris@zankel.net>
 * Marc Gauthier <marc@tensilica.com>
 * Pete Delaney <piet@@tensilica.com>
 */

/*
 * We use a two-level table approach. The user and kernel exception vectors
 * use a first-level dispatch table to dispatch the exception to a registered
 * fast handler or the default handler, if no fast handler was registered.
 * The default handler sets up a C-stack and dispatches the exception to a
 * registerd C handler in the second-level dispatch table.
 *
 * Fast handler entry condition:
 *
 *   a0:	trashed, original value saved on stack (PT_AREG0)
 *   a1:	a1
 *   a2:	new stack pointer, original value in depc
 *   a3:	a3
 *   depc:	a2, original value saved on stack (PT_DEPC)
 *   excsave_1:	dispatch table
 *
 * The value for PT_DEPC saved to stack also functions as a boolean to
 * indicate that the exception is either a double or a regular exception:
 *
 *   PT_DEPC	>= VALID_DOUBLE_EXCEPTION_ADDRESS: double exception
 *		<  VALID_DOUBLE_EXCEPTION_ADDRESS: regular exception
 *
 * Note:  Neither the kernel nor the user exception handler generate literals.
 *
 */

#include <linux/linkage.h>
#include <asm/ptrace.h>
#include <asm/ptrace.h>
#include <asm/current.h>
#include <asm/asm-offsets.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/thread_info.h>
#include <asm/processor.h>
#include <asm/cacheasm.h>

#define WINDOW_VECTORS_SIZE   0x180


/*
 * User exception vector. (Exceptions with PS.UM == 1, PS.EXCM == 0)
 *
 * We get here when an exception occurred while we were in userland.
 * We switch to the kernel stack and jump to the first level handler
 * associated to the exception cause.
 *
 * Note: the saved kernel stack pointer (EXC_TABLE_KSTK) is already
 *       decremented by PT_USER_SIZE.
 */

	.section .UserExceptionVector.text, "ax"

ENTRY(_UserExceptionVector)

	xsr	a3, EXCSAVE_1		# save a3 and get dispatch table
	wsr	a2, DEPC		# save a2
	l32i	a2, a3, EXC_TABLE_KSTK	# load kernel stack to a2
	s32i	a0, a2, PT_AREG0	# save a0 to ESF
	rsr	a0, EXCCAUSE		# retrieve exception cause
	s32i	a0, a2, PT_DEPC		# mark it as a regular exception
	addx4	a0, a0, a3		# find entry in table
	l32i	a0, a0, EXC_TABLE_FAST_USER	# load handler
	xsr	a3, EXCSAVE_1		# restore a3 and dispatch table
	jx	a0

/*
 * Kernel exception vector. (Exceptions with PS.UM == 0, PS.EXCM == 0)
 *
 * We get this exception when we were already in kernel space.
 * We decrement the current stack pointer (kernel) by PT_SIZE plus
 * maximum space reserved register save area for calls we might use:
 *	call4	16		Save 4 registers
 *	call8	32		Save 8 registers
 *	call12	48		Save 8 registers
 *
 * To show the backtrace correcly it's necessary to do a call4
 * followed by call12. With the call12 we can stash most of the
 * registers on the stack where backtrace and gdb can find them.
 *
 * Jump to the first-level handler associated with the exception cause.
 *
 * Note: we need to preserve space for the spill region.
 */

	.section .KernelExceptionVector.text, "ax"

ENTRY(_KernelExceptionVector)

	xsr	a3, EXCSAVE_1		# save a3, and get dispatch table
	wsr	a2, DEPC		# save a2
	addi	a2, a1, -48-PT_SIZE	# adjust stack pointer for a future call12
	s32i	a0, a2, PT_AREG0	# save a0 to ESF
	rsr	a0, EXCCAUSE		# retrieve exception cause
	s32i	a0, a2, PT_DEPC		# mark it as a regular exception
	addx4	a0, a0, a3		# find entry in table
	l32i	a0, a0, EXC_TABLE_FAST_KERNEL	# load handler address
	xsr	a3, EXCSAVE_1		# restore a3 and excsave1
	jx	a0


/*
 * Double exception vector (Exceptions with PS.EXCM == 1)
 * We get this exception when another exception occurs while were are
 * already in an exception, such as window overflow/underflow exception,
 * or 'expected' exceptions, for example memory exception when we were trying
 * to read data from an invalid address in user space.
 *
 * Note that this vector is never invoked for level-1 interrupts, because such
 * interrupts are disabled (masked) when PS.EXCM is set.
 *
 * We decode the exception and take the appropriate action.  However, the
 * double exception vector is much more careful, because a lot more error
 * cases go through the double exception vector than through the user and
 * kernel exception vectors.
 *
 * Occasionally, the kernel expects a double exception to occur.  This usually
 * happens when accessing user-space memory with the user's permissions
 * (l32e/s32e instructions).  The kernel state, though, is not always suitable
 * for immediate transfer of control to handle_double, where "normal" exception
 * processing occurs. Also in kernel mode, TLB misses can occur if accessing
 * vmalloc memory, possibly requiring repair in a double exception handler.
 *
 * The variable at TABLE_FIXUP offset from the pointer in EXCSAVE_1 doubles as
 * a boolean variable and a pointer to a fixup routine. If the variable
 * EXC_TABLE_FIXUP is non-zero, this handler jumps to that address. A value of
 * zero indicates to use the default kernel/user exception handler.
 * There is only one exception, when the value is identical to the exc_table
 * label, the kernel is in trouble. This mechanism is used to protect critical
 * sections, mainly when the handler writes to the stack to assert the stack
 * pointer is valid. Once the fixup/default handler leaves that area, the
 * EXC_TABLE_FIXUP variable is reset to the fixup handler or zero.
 *
 * Procedures wishing to use this mechanism should set EXC_TABLE_FIXUP to the
 * nonzero address of a fixup routine before it could cause a double exception
 * and reset it before it returns.
 *
 * Some other things to take care of when a fast exception handler doesn't
 * specify a particular fixup handler but wants to use the default handlers:
 *
 *  - The original stack pointer (in a1) must not be modified. The fast
 *    exception handler should only use a2 as the stack pointer.
 *
 *  - If the fast handler manipulates the stack pointer (in a2), it has to
 *    register a valid fixup handler and cannot use the default handlers.
 *
 *  - The handler can use any other generic register from a3 to a15, but it
 *    must save the content of these registers to stack (PT_AREG3...PT_AREGx)
 *
 *  - These registers must be saved before a double exception can occur.
 *
 *  - If we ever implement handling signals while in double exceptions, the
 *    number of registers a fast handler has saved (excluding a0 and a1) must
 *    be written to  PT_AREG1. (1 if only a3 is used, 2 for a3 and a4, etc. )
 *
 * The fixup handlers are special handlers:
 *
 *  - Fixup entry conditions differ from regular exceptions:
 *
 *	a0:	   DEPC
 *	a1: 	   a1
 *	a2:	   trashed, original value in EXC_TABLE_DOUBLE_A2
 *	a3:	   exctable
 *	depc:	   a0
 *	excsave_1: a3
 *
 *  - When the kernel enters the fixup handler, it still assumes it is in a
 *    critical section, so EXC_TABLE_FIXUP variable is set to exc_table.
 *    The fixup handler, therefore, has to re-register itself as the fixup
 *    handler before it returns from the double exception.
 *
 *  - Fixup handler can share the same exception frame with the fast handler.
 *    The kernel stack pointer is not changed when entering the fixup handler.
 *
 *  - Fixup handlers can jump to the default kernel and user exception
 *    handlers. Before it jumps, though, it has to setup a exception frame
 *    on stack. Because the default handler resets the register fixup handler
 *    the fixup handler must make sure that the default handler returns to
 *    it instead of the exception address, so it can re-register itself as
 *    the fixup handler.
 *
 * In case of a critical condition where the kernel cannot recover, we jump
 * to unrecoverable_exception with the following entry conditions.
 * All registers a0...a15 are unchanged from the last exception, except:
 *
 *	a0:	   last address before we jumped to the unrecoverable_exception.
 *	excsave_1: a0
 *
 *
 * See the handle_alloca_user and spill_registers routines for example clients.
 *
 * FIXME: Note: we currently don't allow signal handling coming from a double
 *        exception, so the item markt with (*) is not required.
 */

	.section .DoubleExceptionVector.text, "ax"
	.begin literal_prefix .DoubleExceptionVector
	.global _DoubleExceptionVector_Hang		# Available for Breakpoint
	.global _DoubleExceptionVector_WindowSpill	# Available for Breakpoint
	.global _DoubleExceptionVector_WindowOverflow	# Available for Breakpoint
	.global _DoubleExceptionVector_WindowUnderflow	# Available for Breakpoint

ENTRY(_DoubleExceptionVector)

	/* Get a register we can work with. */

	xsr	a3, EXCSAVE_1
	s32i	a2, a3, EXC_TABLE_DOUBLE_SAVE

	/* Check for kernel double exception (usually fatal). */

	rsr	a2, PS
	_bbsi.l	a2, PS_UM_BIT, 2f	# NOTE: .Lksp is now to far away for a branch

_DoubleExceptionVector_Hang:
	j _DoubleExceptionVector_Hang	# FIXME: Place a breakpoint here for now, ...
	j .Lksp				#        ... some day we should try to reach panic.


	/* 
	 * Check if we are currently handling a window exception.
	 * Note: We don't need to indicate that we enter a critical section. 
	 * The fixup code has to be near by to be in range of the branch instructions;
	 * so it's placed between the Window Underflow and Overflow handlers.
	 */
2:	xsr	a0, DEPC		# get DEPC, save a0

	movi	a2, XCHAL_WINDOW_VECTORS_VADDR
	_bltu	a0, a2, .Lfixup
	addi	a2, a2, WINDOW_VECTORS_SIZE
	_bgeu	a0, a2, .Lfixup


	/* Window overflow/underflow exception.
	 *
	 * Here we restart the window exception, and handle the
	 * exception as if a user exception occurred directly in
	 * user code.  We do this so that a user's saved state
	 * doesn't have to reflect any protected state, and thus
	 * complicate not only context-switch, but also the ptrace
	 * interface unnecessarily (and thus GDB, etc) and signal
	 * handleing.
	 *
	 * The underflow case is a bit simpler, because underflow
	 * loads a number of registers, and given we're restarting
	 * that process, all these registers are available (scratch).
	 * The overflow case is a bit trickier.
	 */
_DoubleExceptionVector_WindowSpill:	# Available for Breakpoint
	l32i	a2, a3, EXC_TABLE_KSTK	# get kernel base stack pointer

	/* Check for overflow/underflow exception, jump if overflow. */

	_bbci.l	a0, 6, _DoubleExceptionVector_WindowOverflow

	/* 
	 * Restart window UNDERFLOW exception.
	 * Currently:
	 * 	depc = orig a0, 
	 * 	a0 = orig DEPC,
	 * 	a2 = new sp based on KSTK from exc_table
	 * 	a3 = EXCSAVE_1
	 * 	excsave_1 = orig a3
	 *
	 * We return to the instruction in user space that caused the window
	 * underflow exception. Therefore, we change window base to the value
	 * before we entered the window underflow exception and prepare the
	 * registers to return as if we were coming from a regular exception
	 * by changing depc (in a0).
	 *
	 * NOTE: We can trash the current window frame (a0...a3) and depc!
	 */
_DoubleExceptionVector_WindowUnderflow:
	xsr	a3, EXCSAVE_1		# restore EXCSAVE_1 and a3
	wsr	a2, DEPC		# save stack pointer temporarily
	rsr	a0, PS
	extui	a0, a0, PS_OWB_SHIFT, 4
	wsr	a0, WINDOWBASE
	rsync

	/* We are now in the previous window frame. Save registers again. */

	xsr	a2, DEPC		# save a2 and get stack pointer
	s32i	a0, a2, PT_AREG0
	xsr	a3, EXCSAVE_1		# save a3, retrieve exc_table[cpu]
	rsr	a0, EXCCAUSE
	s32i	a0, a2, PT_DEPC		# mark it as a regular exception
	addx4	a0, a0, a3
	xsr	a3, EXCSAVE_1
	l32i	a0, a0, EXC_TABLE_FAST_USER
	jx	a0


.Lfixup:/* Check for a fixup handler or if we were in a critical section. */

	/* a0: depc, a1: a1, a2: kstk, a3: exc_table, depc: a0, excsave1: a3 */

	/* Enter critical section. */

	l32i	a2, a3, EXC_TABLE_FIXUP
	s32i	a3, a3, EXC_TABLE_FIXUP
	beq	a2, a3, .Lunrecoverable_fixup	# critical!
	beqz	a2, .Lfixup_default		# no handler was registered

	/* a0: depc, a1: a1, a2: trash, a3: exctable, depc: a0, excsave: a3 */

	jx	a2

.Lfixup_default:	/* Get stack pointer. */

	/* a0: depc, a1: a1, a2: kstk, a3: exc_table, depc: a0, excsave1: a3 */

	l32i	a2, a3, EXC_TABLE_DOUBLE_SAVE
	addi	a2, a2, -PT_USER_SIZE

	/*OLD a0: depc, a1: a1, a2: kstk, a3: a2, depc: a0, excsave: a3 */
	/* a0: depc, a1: a1, a2: kstk, a3: exc_table, depc: a0, excsave1: a3 */

	//xsr	a3, DEPC
	s32i	a0, a2, PT_DEPC
	l32i	a0, a3, EXC_TABLE_DOUBLE_SAVE	
	xsr	a0, DEPC
	s32i	a0, a2, PT_AREG0

	/* a0: avail, a1: a1, a2: kstk, a3: exc_table, depc: a2, excsave: a3 */

	// movi	a3, exc_table

	rsr	a0, EXCCAUSE
	addx4	a0, a0, a3
	xsr	a3, EXCSAVE_1
	l32i	a0, a0, EXC_TABLE_FAST_USER
	jx	a0



	/* 
	 * Restart window OVERFLOW exception.
	 *   Currently:
	 * 	depc = orig a0, 
	 * 	a0 = orig DEPC,
	 * 	a2 = new sp based on KSTK from exc_table
	 * 	a3 = EXCSAVE_1
	 * 	excsave_1 = orig a3
	 *
	 * We return to the instruction in user space that caused the window
	 * overflow exception. Therefore, we change window base to the value
	 * before we entered the window overflow exception and prepare the
	 * registers to return as if we were coming from a regular exception
	 * by changing DEPC (in a0).
	 *
	 * NOTE: We CANNOT trash the current window frame (a0...a3), but we
	 * can clobber depc.
	 *
	 * The tricky part here is that overflow8 and overflow12 handlers
	 * save a0, then clobber a0.  To restart the handler, we have to restore
	 * a0 if the double exception was past the point where a0 was clobbered.
	 *
	 * To keep things simple, we take advantage of the fact all overflow
	 * handlers save a0 in their very first instruction.  If DEPC was past
	 * that instruction, we can safely restore a0 from where it was saved
	 * on the stack.
	 *
	 * OLD a0: depc, a1: a1, a2: kstk, a3: a2, depc: a0, excsave: a3
	 * a0: depc, a1: a1, a2: kstk, a3: exc_table, depc: a0, excsave1: a3 
	 */
_DoubleExceptionVector_WindowOverflow:
#if 0
	/*
	 * We will do this after further checking of the fixup code
	 * and hopefully removing the check on DEPC to decide on
	 * doing a return from double exception (rfde).
	 */
	s32i	a0, a2, PT_DEPC		# save DEPC of error, for debugging
#endif

	/*  a0, a2 are available  */

	/*  
	 * Check whether to restore a0 possibly clobbered by the window handler.
	 * Checking if DEPC == _WindowOverflow8 or DEPC == _WindowOverflow12.
	 * This algorithm avoids using literals, which we are short of here
	 * in vectors.S.
	 */
	extui	a2, a0, 0, 6		# get offset into 64-byte vector handler
	beqz	a2, 1f			# if at start of vector, don't restore

	addi	a0, a0, -128		# for simpler branching...
	bbsi	a0, 8, 1f		# don't restore except for overflow 8 and 12

	bbsi	a0, 7, 2f		# branch if _WindowOverflow12

	/*  Restore a0 as saved by _WindowOverflow8().
	 *
	 *  FIXME:  we really need a fixup handler for this L32E,
	 *  for the extremely unlikely case where the overflow handler's
	 *  reference thru a0 gets a hardware TLB refill that bumps out
	 *  the (distinct, aliasing) TLB entry that mapped its prior
	 *  references thru a9, and where our reference now thru a9
	 *  gets a 2nd-level miss exception (not hardware TLB refill).
	 */

	l32e	a2, a9, -16
	wsr	a2, DEPC		# replace the saved a0
	j	1f

2:
	/*  Restore a0 as saved by _WindowOverflow12().
	 *
	 *  FIXME:  we really need a fixup handler for this L32E,
	 *  for the extremely unlikely case where the overflow handler's
	 *  reference thru a0 gets a hardware TLB refill that bumps out
	 *  the (distinct, aliasing) TLB entry that mapped its prior
	 *  references thru a13, and where our reference now thru a13
	 *  gets a 2nd-level miss exception (not hardware TLB refill).
	 */

	l32e	a2, a13, -16
	wsr	a2, DEPC		# replace the saved a0

1:
	/*  
	 * Restore WindowBase while leaving all address registers restored.
	 *  We have to use ROTW for this, because WSR.WINDOWBASE requires
	 *  an address register (which would prevent restore).
	 *
	 * Window Base goes from 0 ... 7 (Module 8)
	 * Window Start is 8 bits; Ex: (0b1010 1010):0x55 from series of call4s
	 */
	rsr	a0, PS
	extui	a0, a0, PS_OWB_SHIFT, 4
	rsr	a2, WINDOWBASE
	sub	a0, a2, a0		# a0 = 1, 2, or 3
	extui	a0, a0, 0, 3		# extract lower 3 bits

	l32i	a2, a3, EXC_TABLE_DOUBLE_SAVE	# restore a2
	xsr	a3, EXCSAVE_1		# restore EXCSAVE_1 and a3

	beqi	a0, 1, .L1pane		# branch if a0 == 1
	beqi	a0, 3, .L3pane		# branch if a0 == 3

	/*  Most common case, overflow off by 2 panes from user code.  */
	rsr	a0, DEPC		# restore a0
	rotw	-2			# WindowBase += -2

	/*
	 *  We are now in the user code's original window frame.
	 *  Process the exception as a user exception as if it was
	 *  taken by the user code.
	 *
	 *  This is similar to the user exception vector,
	 *  except that PT_DEPC isn't set to EXCCAUSE.
	 */
1:	xsr	a3, EXCSAVE_1		# save a3 and get dispatch table
	wsr	a2, DEPC		# save a2
	l32i	a2, a3, EXC_TABLE_KSTK	# load kernel stack to a2
	s32i	a0, a2, PT_AREG0	# save a0 to ESF
	rsr	a0, EXCCAUSE		# retrieve exception cause
#if 1
	/* 
	 * To be removed once fixup involvement is totaly understood.
	 * Staying with current convention of indicating a regular
	 * exception by placing the EXCCAUSE in regs->pt_depc.
	 *
	 * Indicating a double exception would cause the return
	 * to user space to do a return from doube excpetion.
	 * We have left the return pc in EXCSAVE_1 so the return
	 * from exception will work.
	 */
	s32i	a0, a2, PT_DEPC		# mark it as a regular exception by storing...
					# ... EXCCAUSE in exc_table[cpu]->pt_depc
#endif
	addx4	a0, a0, a3		# find entry in table
	l32i	a0, a0, EXC_TABLE_FAST_USER	# load handler
	xsr	a3, EXCSAVE_1		# restore a3 and dispatch table
	jx	a0			# call exc_table[cpu]->fast_user[exccause]
					# ... likely fast_second_level_miss().

.L1pane:
	rsr	a0, DEPC		# restore a0
	rotw	-1
	j	1b

.L3pane:
	rsr	a0, DEPC		# restore a0
	rotw	-3
	j	1b

	/*
	 * We only allow the ITLB miss exception if we are in kernel space.
	 * All other exceptions are unexpected and thus unrecoverable!
	 *
	 * Might want to switch to a reserved kernel stack for the current
	 * CPU here as the current stack might be blown.
	 */
	.extern fast_second_level_miss_double_kernel

.Lksp:	/* OLDa0: a0, a1: a1, a2: a2, a3: trashed, depc: depc, excsave: a3 */
	/* a0: depc, a1: a1, a2: kstk, a3: exc_table, depc: a0, excsave1: a3 */
	rsr	a3, EXCCAUSE
	beqi	a3, EXCCAUSE_ITLB_MISS, 1f
	addi	a3, a3, -EXCCAUSE_DTLB_MISS
	bnez	a3, .Lunrecoverable
1:	movi	a3, fast_second_level_miss_double_kernel
	jx	a3

	/* Critical! We can't handle this situation. PANIC! */

	.extern unrecoverable_exception

.Lunrecoverable_fixup:
1: j 1b
	l32i	a2, a3, EXC_TABLE_DOUBLE_SAVE
	xsr	a0, DEPC

.Lunrecoverable:
	rsr	a3, EXCSAVE_1
	wsr	a0, EXCSAVE_1
	movi	a0, unrecoverable_exception
	callx0	a0

	.end literal_prefix


/*
 * Debug interrupt vector
 *
 * There is not much space here, so simply jump to another handler.
 * EXCSAVE[DEBUGLEVEL] has been set to that handler.
 */

	.section .DebugInterruptVector.text, "ax"

ENTRY(_DebugInterruptVector)
	xsr	a0, EXCSAVE + XCHAL_DEBUGLEVEL
	jx	a0



/*
 * Medium priority level interrupt vectors
 *
 * Each takes less than 28 (0x1c) bytes, no literals, by placing
 * the extra 8 bytes that would otherwise be required in the window
 * vectors area where there is space.  With relocatable vectors,
 * all vectors are within ~ 4 kB range of each other, so we can
 * simply jump (J) to another vector without having to use JX.
 *
 * TODO:  This is a quick implementation that is functional,
 * but gives up prioritization of interrupts.
 * Needs to be cleaned up to pass the interrupt level to the
 * common_exception code to set the correct interrupt level.
 */

#if XCHAL_EXCM_LEVEL >= 2
	.section .Level2InterruptVector.text, "ax"
ENTRY(_Level2InterruptVector)
	#  Simulate level-one interrupt ...
	xsr	a0, EPC2
	wsr	a0, EPC1
	movi	a0, 4				# LEVEL1_INTERRUPT cause
	wsr	a0, EXCCAUSE
	rsr	a0, EPS2
	addi	a0, a0, (1 << PS_EXCM_BIT)	# set PS.EXCM
	wsr	a0, PS				# leave PS.INTLEVEL intact
	j	_SimulateUserKernelVectorException	# branch to user or kernel vector
#endif

#if XCHAL_EXCM_LEVEL >= 3
	.section .Level3InterruptVector.text, "ax"
ENTRY(_Level3InterruptVector)
	#  Simulate level-one interrupt ...
	wsr	a0, EPC2
	rsr	a0, EPC3
	wsr	a0, EPC1
	movi	a0, 4				# LEVEL1_INTERRUPT cause
	wsr	a0, EXCCAUSE
	rsr	a0, EPS3
	addi	a0, a0, (1 << PS_EXCM_BIT)	# set PS.EXCM
	wsr	a0, PS				# leave PS.INTLEVEL intact
	j	_SimulateUserKernelVectorException
#endif

#if XCHAL_EXCM_LEVEL >= 4
	.section .Level4InterruptVector.text, "ax"
ENTRY(_Level4InterruptVector)
	#  Simulate level-one interrupt ...
	wsr	a0, EPC2
	rsr	a0, EPC4
	wsr	a0, EPC1
	movi	a0, 4				# LEVEL1_INTERRUPT cause
	wsr	a0, EXCCAUSE
	rsr	a0, EPS4
	addi	a0, a0, (1 << PS_EXCM_BIT)	# set PS.EXCM
	wsr	a0, PS				# leave PS.INTLEVEL intact
	j	_SimulateUserKernelVectorException
#endif

#if XCHAL_EXCM_LEVEL >= 5
	.section .Level5InterruptVector.text, "ax"
ENTRY(_Level5InterruptVector)
	#  Simulate level-one interrupt ...
	wsr	a0, EPC2
	rsr	a0, EPC5
	wsr	a0, EPC1
	movi	a0, 4				# LEVEL1_INTERRUPT cause
	wsr	a0, EXCCAUSE
	rsr	a0, EPS5
	addi	a0, a0, (1 << PS_EXCM_BIT)	# set PS.EXCM
	wsr	a0, PS				# leave PS.INTLEVEL intact
	j	_SimulateUserKernelVectorException
#endif

#if XCHAL_EXCM_LEVEL >= 6
	.section .Level6InterruptVector.text, "ax"
ENTRY(_Level6InterruptVector)
	#  Simulate level-one interrupt ...
	wsr	a0, EPC2
	rsr	a0, EPC6
	wsr	a0, EPC1
	movi	a0, 4				# LEVEL1_INTERRUPT cause
	wsr	a0, EXCCAUSE
	rsr	a0, EPS6
	addi	a0, a0, (1 << PS_EXCM_BIT)	# set PS.EXCM
	wsr	a0, PS				# leave PS.INTLEVEL intact
	j	_SimulateUserKernelVectorException
#endif



/* Window overflow and underflow handlers.
 * The handlers must be 64 bytes apart, first starting with the underflow
 * handlers underflow-4 to underflow-12, then the overflow handlers
 * overflow-4 to overflow-12.
 *
 * Note: We rerun the underflow handlers if we hit an exception, so
 *	 we try to access any page that would cause a page fault early.
 */

	.section .WindowVectors.text, "ax"


/* 4-Register Window Overflow Vector (Handler) */

	.align 64
.global _WindowOverflow4
_WindowOverflow4:
	s32e	a0, a5, -16
	s32e	a1, a5, -12
	s32e	a2, a5,  -8
	s32e	a3, a5,  -4
	rfwo


#if XCHAL_EXCM_LEVEL >= 2
	/*  Not a window vector - but a convenient location
	 *  (where we know there's space) for continuation of
	 *  medium priority interrupt dispatch code.
	 *  On entry here, a0 contains PS, and EPC2 contains saved a0:
	 */
	.align 4
_SimulateUserKernelVectorException:
	bbsi.l	a0, PS_UM_BIT, 1f		# branch if user mode
	rsr	a0, EPC2			# restore a0
	j	_KernelExceptionVector		# simulate kernel vector exception
1:	rsr	a0, EPC2			# restore a0
	j	_UserExceptionVector		# simulate user vector exception
#endif


/* 4-Register Window Underflow Vector (Handler) */

	.align 64
.global _WindowUnderflow4
_WindowUnderflow4:
	l32e	a0, a5, -16
	l32e	a1, a5, -12
	l32e	a2, a5,  -8
	l32e	a3, a5,  -4
	rfwu


/* 8-Register Window Overflow Vector (Handler)
 *
 * NOTE: double exception handler expects first instruction to save a0.
 */

	.align 64
.global _WindowOverflow8
_WindowOverflow8:
	s32e	a0, a9, -16	/* MUST be first instruction of vector */
	l32e	a0, a1, -12
	s32e	a1, a9, -12
	s32e	a2, a9,  -8
	s32e	a3, a9,  -4
	s32e	a4, a0, -32
	s32e	a5, a0, -28
	s32e	a6, a0, -24
	s32e	a7, a0, -20
	rfwo

/* 8-Register Window Underflow Vector (Handler) */

	.align 64
.global _WindowUnderflow8
_WindowUnderflow8:
	l32e	a1, a9, -12
	l32e	a0, a9, -16
	l32e	a7, a1, -12
	l32e	a2, a9,  -8
	l32e	a3, a9,  -4
	l32e	a4, a7, -32
	l32e	a5, a7, -28
	l32e	a6, a7, -24
	l32e	a7, a7, -20
	rfwu


/* 12-Register Window Overflow Vector (Handler)
 *
 * NOTE: double exception handler expects first instruction to save a0.
 */

	.align 64
.global _WindowOverflow12
_WindowOverflow12:
	s32e	a0,  a13, -16	/* MUST be first instruction of vector */
	l32e	a0,  a1,  -12
	s32e	a1,  a13, -12
	s32e	a2,  a13,  -8
	s32e	a3,  a13,  -4
	s32e	a4,  a0,  -48
	s32e	a5,  a0,  -44
	s32e	a6,  a0,  -40
	s32e	a7,  a0,  -36
	s32e	a8,  a0,  -32
	s32e	a9,  a0,  -28
	s32e	a10, a0,  -24
	s32e	a11, a0,  -20
	rfwo

/* 12-Register Window Underflow Vector (Handler) */

	.align 64
.global _WindowUnderflow12
_WindowUnderflow12:
	l32e	a1,  a13, -12
	l32e	a0,  a13, -16
	l32e	a11, a1,  -12
	l32e	a2,  a13,  -8
	l32e	a4,  a11, -48
	l32e	a8,  a11, -32
	l32e	a3,  a13,  -4
	l32e	a5,  a11, -44
	l32e	a6,  a11, -40
	l32e	a7,  a11, -36
	l32e	a9,  a11, -28
	l32e	a10, a11, -24
	l32e	a11, a11, -20
	rfwu

	.text


