#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/processor.h>
#include <asm/coprocessor.h>
#include <asm/thread_info.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/ptrace.h>
#include <asm/current.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/signal.h>
#include <asm/tlbflush.h>
#include <variant/tie-asm.h>

// TODO TODO TODO
#define POST_INTERRUPT_SOFTIRQ 7

ENTRY(do_post_IRQ)

	entry	a1, 32

	rsr	a3, ps
	movi	a4, 0x700
	movi	a5, 0x00122000 + 4 * POST_INTERRUPT_SOFTIRQ
	wer	a4, a5
	extui	a4, a3, PS_STACK_SHIFT, PS_STACK_LEN
	beqi	a4, PS_STACK_CROSS, .Lkernel_post_irq
	beqi	a4, PS_STACK_FIRST_INT, .Luser_post_irq
post_irq_panic:

	.section ".rodata", "a"
.Lpost_irq_panic_msg:
	.asciz "do_post_IRQ with wrong PS: 0x%08x\n"
	.previous

	mov	a11, a3
	movi	a10, .Lpost_irq_panic_msg
	movi	a8, panic
	callx8	a8
1:	j	1b

.Lkernel_post_irq:
	movsp	a1, a1
	rotw	-1
	mov	a12, a1
	rotw	1

	movi	a5, (PS_STACK_KERNEL << PS_STACK_SHIFT) | PS_DI_MASK
	j	.Lcommon_post_irq

.Luser_post_irq:
	rsr	a4, ksb
	movi	a5, (PS_STACK_FIRST_KER << PS_STACK_SHIFT) | PS_DI_MASK

.Lcommon_post_irq:
#if PT_SIZE > 128
	addi	a4, a4, -((PT_SIZE + 0xf) & 0xfffffff0)
	addi	a2, a4, ((-PT_SIZE) & 0x0000000f)
#elif PT_SIZE <= 128
	addi	a4, a4, -128
	addi	a2, a4, 128 - PT_SIZE
#endif
	movsp	a1, a4

	movi	a6, ~(PS_STACK_MASK)
	and	a3, a3, a6
	or	a3, a3, a5
	wsr	a3, ps

	rsr	a4, windowbase
	s32i	a4, a2, PT_WINDOWBASE
	j	common_exception_return

ENDPROC(do_post_IRQ)

ENTRY(common_exception)

	entry	a1, 0

	/* debug debug */
	rsr	a3, ksb
	addi	a3, a3, -1
	xor	a3, a3, a1
	srli	a3, a3, 13
	beqz	a3, 2f
	rsr	a3, isb
	addi	a3, a3, -1
	xor	a3, a3, a1
	srli	a3, a3, 13
	beqz	a3, 2f
common_exception_bad_stack:
	j	common_exception_bad_stack
2:
	/* debug debug */
	rsr	a3, windowbase
	s32i	a3, a2, PT_WINDOWBASE
	l32i	a3, a2, PT_PS
	extui	a8, a3, PS_CAUSE_SHIFT, 12
	s32i	a8, a2, PT_EXCCAUSE
	bbsi.l	a3, PS_DI_SHIFT, 1f
	rsil	a4, 0
1:
	movi	a4, -1
	s32i	a4, a2, PT_SYSCALL
	rsr	a4, excsave1
	extui	a8, a3, PS_CAUSE_SHIFT, 8
	addx4	a4, a8, a4
	l32i	a8, a4, 0
	mov	a10, a2
	callx8	a8

	.global common_exception_return
common_exception_return:

	rsil	a3, LOCKLEVEL

	GET_THREAD_INFO(a4, a1)
	extui	a3, a3, PS_STACK_SHIFT, PS_STACK_LEN
	l32i	a5, a4, TI_FLAGS
	beqi	a3, PS_STACK_FIRST_KER, .Luser
	beqi	a3, PS_STACK_KERNEL, .Lkernel

	retw

.Lkernel:
#ifdef CONFIG_PREEMPT
	bbci.l	a5, TIF_NEED_RESCHED, 1f

	/* Check current_thread_info->preempt_count */

	l32i	a5, a4, TI_PRE_COUNT
	bnez	a5, 1f
#ifdef CONFIG_TRACE_IRQFLAGS
	movi	a8, trace_hardirqs_off
	callx8	a8
#endif
	movi	a8, preempt_schedule_irq
	callx8	a8
#ifdef CONFIG_TRACE_IRQFLAGS
	movi	a8, trace_hardirqs_on
	callx8	a8
#endif
	j	common_exception_return
#endif
1:
	retw

.Luser:
	bbsi.l	a5, TIF_NEED_RESCHED, 1f
	bbsi.l	a5, TIF_NOTIFY_RESUME, 2f
	bbsi.l	a5, TIF_SIGPENDING, 2f

user_return:
	rsr	a3, windowbase
	l32i	a4, a2, PT_WINDOWBASE
	movi	a5, 0xf0ffffff
	xor	a3, a3, a4
	and	a3, a3, a5
	xor	a3, a3, a4
	wsr	a3, windowbase
	rsync
	retw
1:
	rsil	a3, 0
	movi	a8, schedule
	callx8	a8
	j	common_exception_return
2:
	rsil	a3, 0
	mov	a10, a2
	movi	a8, do_notify_resume
	callx8	a8
	j	common_exception_return

ENDPROC(common_exception)

/*
 * Task switch.
 *
 * struct task*  _switch_to (struct task* prev, struct task* next)
 *         a2                              a2                 a3
 *
 * Register usage:
 *  a2: prev
 *  a3: next
 *  a4: prev->thread_info
 *  a5: next->thread_info
 */

ENTRY(_switch_to)

	entry	a1, 32

	l32i	a4, a2, TASK_THREAD_INFO
	l32i	a5, a3, TASK_THREAD_INFO

	/* debug debug */
       	/* thread_info structs are at the bottom of thread stacks */
	extui	a6, a4, 0, 13
1:	bnez	a6, 1b
	extui	a6, a5, 0, 13
1:	bnez	a6, 1b
	/* prev -- at the bottom of current stack... */
	srli	a6, a1, 13
	srli	a7, a4, 13
1:	bne	a6, a7, 1b
	/* thread_info's point back to their tasks */
	l32i	a6, a4, TI_TASK
1:	bne	a6, a2, 1b
	l32i	a6, a5, TI_TASK
1:	bne	a6, a3, 1b
	/* debug debug */

	save_xtregs_user a4 a6 a8 a9 a10 a11 THREAD_XTREGS_USER

#if THREAD_RA > 1020 || THREAD_SP > 1020
	addi	a6, a2, TASK_THREAD
	s32i	a0, a6, THREAD_RA - TASK_THREAD	# save return address
	s32i	a1, a6, THREAD_SP - TASK_THREAD	# save stack pointer
#else
	s32i	a0, a2, THREAD_RA	# save return address
	s32i	a1, a2, THREAD_SP	# save stack pointer
#endif

	/* Disable ints while we manipulate the stack pointer. */

	rsil	a14, LOCKLEVEL

#if THREAD_RA > 1020 || THREAD_SP > 1020
	s32i	a14, a6, THREAD_PS - TASK_THREAD
#else
	s32i	a14, a2, THREAD_PS
#endif

	/* Switch CPENABLE */

#if (XTENSA_HAVE_COPROCESSORS || XTENSA_HAVE_IO_PORTS)
	l32i	a6, a5, THREAD_CPENABLE
	xsr	a6, cpenable
	s32i	a6, a4, THREAD_CPENABLE
#endif

	/* Flush register file. */

	ssai	0
	spillw

	/* Set kernel stack (and leave critical section)
	 * Note: It's save to set it here. The stack will not be overwritten
	 *       because the kernel stack will only be loaded again after
	 *       we return from kernel space.
	 */

	addi	a6, a5, (PT_REGS_OFFSET + PT_SIZE)
	wsr	a6, ksb

	/* restore context of the task 'next' */

	l32i	a0, a3, THREAD_RA	# restore return address
	l32i	a1, a3, THREAD_SP	# restore stack pointer

	/* debug debug */
	/* next -- at the bottom of new  stack... */
	srli	a6, a6, 13
	srli	a7, a1, 13
1:	bne	a6, a7, 1b
	/* debug debug */

	load_xtregs_user a5 a6 a8 a9 a10 a11 THREAD_XTREGS_USER

#if THREAD_RA > 1020 || THREAD_SP > 1020
	addi	a6, a3, TASK_THREAD
	l32i	a7, a6, THREAD_PS - TASK_THREAD
#else
	l32i	a7, a3, THREAD_PS
#endif
	movi	a6, ~PS_STACK_MASK
	xor	a14, a14, a7
	and	a14, a14, a6
	xor	a14, a14, a7

	wsr	a14, ps
	rsync

	retw

ENDPROC(_switch_to)

ENTRY(ret_from_fork)

	/* void schedule_tail (struct task_struct *prev)
	 * Note: prev is still in a6 (return value from fake call4 frame)
	 */
	movi	a8, schedule_tail
	callx8	a8

	movi	a8, do_syscall_trace_leave
	mov	a10, a1
	callx8	a8

ret_to_userspace:
	movi	a3, (~PS_STACK_MASK)
	movi	a4, (PS_STACK_FIRST_KER << PS_STACK_SHIFT)
	rsil	a2, LOCKLEVEL
	and	a2, a2, a3
	or	a2, a2, a4
	wsr	a2, ps
	rsync

#if PT_SIZE > 128
	addi	a2, a1, ((-PT_SIZE) & 0x0000000f)
#elif PT_SIZE <= 128
	addi	a2, a1, 128 - PT_SIZE
#endif

	j	user_return

ENDPROC(ret_from_fork)

/*
 * Kernel thread creation helper
 * On entry, set up by copy_thread: a2 = thread_fn, a3 = thread_fn arg
 *           left from _switch_to: a6 = prev
 */
ENTRY(ret_from_kernel_thread)

	call8	schedule_tail
	mov	a10, a3
	callx8	a2
	j	ret_to_userspace

ENDPROC(ret_from_kernel_thread)

#ifdef CONFIG_MMU

ENTRY(fast_second_level_miss)

	entry	a1, 32

	/* We need to map the page of PTEs for the user task.  Find
	 * the pointer to that page.  Also, it's possible for tsk->mm
	 * to be NULL while tsk->active_mm is nonzero if we faulted on
	 * a vmalloc address.  In that rare case, we must use
	 * active_mm instead to avoid a fault in this handler.  See
	 *
	 * http://mail.nl.linux.org/linux-mm/2002-08/msg00258.html
	 *   (or search Internet on "mm vs. active_mm")
	 *
	 *	if (!mm)
	 *		mm = tsk->active_mm;
	 *	pgd = pgd_offset (mm, regs->excvaddr);
	 *	pmd = pmd_offset (pgd, regs->excvaddr);
	 *	pmdval = *pmd;
	 */

	GET_CURRENT(a4,a1)
	l32i	a3, a4, TASK_MM		# tsk->mm
	beqz	a3, 9f

8:	l32i	a6, a2, PT_EXCVADDR	# fault address
	_PGD_OFFSET(a3, a6, a4)
	l32i	a3, a3, 0		# read pmdval
	beqz	a3, 2f

	/* Read ptevaddr and convert to top of page-table page.
	 *
	 * 	vpnval = read_ptevaddr_register() & PAGE_MASK;
	 * 	vpnval += DTLB_WAY_PGTABLE;
	 *	pteval = mk_pte (virt_to_page(pmd_val(pmdval)), PAGE_KERNEL);
	 *	write_dtlb_entry (pteval, vpnval);
	 *
	 * The messy computation for 'pteval' above really simplifies
	 * into the following:
	 *
	 * pteval = ((pmdval - PAGE_OFFSET) & PAGE_MASK) | PAGE_DIRECTORY
	 */

	movi	a4, (-PAGE_OFFSET) & 0xffffffff
	add	a3, a3, a4		# pmdval - PAGE_OFFSET
	extui	a4, a3, 0, PAGE_SHIFT	# ... & PAGE_MASK
	xor	a3, a3, a4

	movi	a4, _PAGE_DIRECTORY
	or	a3, a3, a4		# ... | PAGE_DIRECTORY

	/*
	 * We utilize all three wired-ways (7-9) to hold pmd translations.
	 * Memory regions are mapped to the DTLBs according to bits 28 and 29.
	 * This allows to map the three most common regions to three different
	 * DTLBs:
	 *  0,1 -> way 7	program (0040.0000) and virtual (c000.0000)
	 *  2   -> way 8	shared libaries (2000.0000)
	 *  3   -> way 0	stack (3000.0000)
	 */

	extui	a6, a6, 28, 2		# addr. bit 28 and 29	0,1,2,3
	rsr	a4, ptevaddr
	addx2	a6, a6, a6		# ->			0,3,6,9
	srli	a4, a4, PAGE_SHIFT
	extui	a6, a6, 2, 2		# ->			0,0,1,2
	slli	a4, a4, PAGE_SHIFT	# ptevaddr & PAGE_MASK
	addi	a6, a6, DTLB_WAY_PGD
	add	a4, a4, a6		# ... + way_number

3:	wdtlb	a3, a4
	dsync

	retw

9:	l32i	a3, a4, TASK_ACTIVE_MM	# unlikely case mm == 0
	j	8b

#if (DCACHE_WAY_SIZE > PAGE_SIZE)
#warning TODO
2:	/* Special case for cache aliasing.
	 * We (should) only get here if a clear_user_page, copy_user_page
	 * or the aliased cache flush functions got preemptively interrupted 
	 * by another task. Re-establish temporary mapping to the 
	 * TLBTEMP_BASE areas.
	 */

	/* We shouldn't be in a double exception */

	l32i	a0, a2, PT_DEPC
	bgeui	a0, VALID_DOUBLE_EXCEPTION_ADDRESS, 2f

	/* Make sure the exception originated in the special functions */

	movi	a0, __tlbtemp_mapping_start
	rsr	a3, epc1
	bltu	a3, a0, 2f
	movi	a0, __tlbtemp_mapping_end
	bgeu	a3, a0, 2f

	/* Check if excvaddr was in one of the TLBTEMP_BASE areas. */

	movi	a3, TLBTEMP_BASE_1
	rsr	a0, excvaddr
	bltu	a0, a3, 2f

	addi	a1, a0, -TLBTEMP_SIZE
	bgeu	a1, a3, 2f

	/* Check if we have to restore an ITLB mapping. */

	movi	a1, __tlbtemp_mapping_itlb
	rsr	a3, epc1
	sub	a3, a3, a1

	/* Calculate VPN */

	movi	a1, PAGE_MASK
	and	a1, a1, a0

	/* Jump for ITLB entry */

	bgez	a3, 1f

	/* We can use up to two TLBTEMP areas, one for src and one for dst. */

	extui	a3, a0, PAGE_SHIFT + DCACHE_ALIAS_ORDER, 1
	add	a1, a3, a1

	/* PPN is in a6 for the first TLBTEMP area and in a7 for the second. */

	mov	a0, a6
	movnez	a0, a7, a3
	j	3b

	/* ITLB entry. We only use dst in a6. */

1:	witlb	a6, a1
	isync
	j	4b


#endif	// DCACHE_WAY_SIZE > PAGE_SIZE


2:	/* Invalid PGD, default exception handling */

	mov	a10, a2
	call8	do_page_fault
	retw

ENDPROC(fast_second_level_miss)

ENTRY(fast_store_prohibited)

	entry	a1, 32

	l32i	a3, a2, PT_EXCCAUSE
	movi	a4, EXCCAUSE_STORE_CACHE_ATTRIBUTE
	bne	a3, a4, 2f

	GET_CURRENT(a4,a1)
	l32i	a3, a4, TASK_MM		# tsk->mm
	beqz	a3, 9f

8:	l32i	a4, a2, PT_EXCVADDR	# fault address
	_PGD_OFFSET(a3, a4, a6)
	l32i	a3, a3, 0
	beqz	a3, 2f

	/*
	 * Note that we test _PAGE_WRITABLE_BIT only if PTE is present
	 * and is not PAGE_NONE. See pgtable.h for possible PTE layouts.
	 */

	_PTE_OFFSET(a3, a4, a6)
	l32i	a6, a3, 0		# read pteval
	movi	a4, _PAGE_CA_INVALID
	ball	a6, a4, 2f
	bbci.l	a6, _PAGE_WRITABLE_BIT, 2f

	movi	a4, _PAGE_ACCESSED | _PAGE_DIRTY | _PAGE_HW_WRITE
	or	a6, a6, a4
	l32i	a4, a2, PT_EXCVADDR
	s32i	a6, a3, 0

	/* We need to flush the cache if we have page coloring. */
#if (DCACHE_WAY_SIZE > PAGE_SIZE) && XCHAL_DCACHE_IS_WRITEBACK
	dhwb	a3, 0
#endif
	pdtlb	a3, a4
	wdtlb	a6, a3

	retw

9:	l32i	a3, a4, TASK_ACTIVE_MM	# unlikely case mm == 0
	j	8b

2:	/* If there was a problem, handle fault in C */

	mov	a10, a2
	call8	do_page_fault
	retw

ENDPROC(fast_store_prohibited)

#endif /* CONFIG_MMU */
