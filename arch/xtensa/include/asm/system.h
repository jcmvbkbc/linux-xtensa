/*
 * arch/xtensa/include/asm/system.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2009 Tensilica Inc.
 */

#ifndef _XTENSA_SYSTEM_H
#define _XTENSA_SYSTEM_H

#include <linux/stringify.h>

#include <asm/processor.h>

/* interrupt control */

#define local_save_flags(x)						\
	__asm__ __volatile__ ("rsr %0,"__stringify(PS) : "=a" (x));
#define local_irq_restore(x)	do {					\
	__asm__ __volatile__ ("wsr %0, "__stringify(PS)" ; rsync" 	\
	    		      :: "a" (x) : "memory"); } while(0);
#define local_irq_save(x)	do {					\
	__asm__ __volatile__ ("rsil %0, "__stringify(LOCKLEVEL) 	\
	    		      : "=a" (x) :: "memory");} while(0);

static inline void local_irq_disable(void)
{
	unsigned long flags;
	__asm__ __volatile__ ("rsil %0, "__stringify(LOCKLEVEL)
	    		      : "=a" (flags) :: "memory");
}
static inline void local_irq_enable(void)
{
	unsigned long flags;
	__asm__ __volatile__ ("rsil %0, 0" : "=a" (flags) :: "memory");

}

static inline int irqs_disabled(void)
{
	unsigned long flags;
	local_save_flags(flags);
	return flags & 0xf;
}

#if 1
/*
 * REMIND:
 *	On LTP Without Workaround: 
 *		PASS: 2057 FAIL: 28
 */	
#define smp_read_barrier_depends() barrier()
#define read_barrier_depends() barrier()
#else
#define smp_read_barrier_depends() do { } while(0)
#define read_barrier_depends() do { } while(0)
#endif

#define mb()  barrier()
#define rmb() mb()
#define wmb() mb()

#ifdef CONFIG_SMP
// FIXME: do we need anything special?
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#else
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#endif

#define set_mb(var, value)	do { var = value; mb(); } while (0)

#if !defined (__ASSEMBLY__)

/* * switch_to(n) should switch tasks to task nr n, first
 * checking that n isn't the current task, in which case it does nothing.
 */
extern void *_switch_to(void *last, void *next);

#endif	/* __ASSEMBLY__ */

/*
 * TIF_CURRENTLY_RUNNING is being used by gdb ps/btt macros
 * to know if a task is currenty running a it's CPU.
 */
#define prepare_arch_switch(next)                                \
do {                                                             \
	clear_tsk_thread_flag(next, TIF_CURRENTLY_RUNNING);      \
} while(0)

#define finish_arch_switch(prev)                                 \
do {                                                             \
	set_tsk_thread_flag(prev, TIF_CURRENTLY_RUNNING);        \
} while(0)

#if defined(CONFIG_SMP) && XTENSA_HAVE_COPROCESSORS
/* 
 * For SMP systems with coprocessors we either flush to coprocessor state or bind the
 * task to the CPU while it ownes the CP. If being debuged (ptrace) we always flush. 
 */
#define switch_to(prev,next,last)						\
do {										\
	manage_coprocessors((void *)prev, CP_SWITCH);				\
	(last) = _switch_to(prev, next);					\
} while(0)

#else

/* 
 * For non-SMP systems all Lazy Coprocessor Flushing is done in coprocessor.S
 * No flushing or process binding has to be done in manage_coprocessors();
 * especially if it doesn't have any coprocessors.
 */
#define switch_to(prev,next,last)						\
do {										\
	(last) = _switch_to(prev, next);					\
} while(0)
#endif

/*
 * Atomic compare and exchange.  Compare OLD with MEM, if identical,
 * store NEW in MEM.  Return the initial value in MEM.  Success is
 * indicated by comparing RETURN with OLD.
 */

#define __HAVE_ARCH_CMPXCHG     1

static inline unsigned long
cmpxchg_u32(volatile int *p, int old, int new)
{
#if XCHAL_HAVE_S32C1I
        __asm__ __volatile__(
"       l32i    %0, %1, 0       \n"
"       bne     %0, %2, 2f      \n"
"       wsr     %0, SCOMPARE1   \n"
"       mov     %0, %3          \n"
"       s32c1i  %0, %1, 0       \n"
"2:                             \n"
        : "=&a" (old)
        : "a" (p), "a" (old), "a" (new)
        : "memory"
        );

        return old;
#else
  __asm__ __volatile__("rsil    a15, "__stringify(LOCKLEVEL)"\n\t"
		       "l32i    %0, %1, 0              \n\t"
		       "bne	%0, %2, 1f             \n\t"
		       "s32i    %3, %1, 0              \n\t"
		       "1:                             \n\t"
		       "wsr     a15, "__stringify(PS)" \n\t"
		       "rsync                          \n\t"
		       : "=&a" (old)
		       : "a" (p), "a" (old), "r" (new)
		       : "a15", "memory");
  return old;
#endif
}

#ifdef CONFIG_CC_OPTIMIZE_FOR_DEBUGGING
static __inline__ void __cmpxchg_called_with_bad_pointer(void)
{
	extern void panic(const char *fmt, ...);

	panic(__func__);
} 
#else
/* 
 * This function doesn't exist in highly optimized kernels, so you'll 
 * get a linker error if something tries to do an invalid cmpxchg(). 
 */
extern void __cmpxchg_called_with_bad_pointer(void);
#endif

static __inline__ unsigned long
__cmpxchg(volatile void *ptr, unsigned long old, unsigned long new, int size)
{
	switch (size) {
	case 4:  return cmpxchg_u32(ptr, old, new);
	default: __cmpxchg_called_with_bad_pointer();
		 return old;
	}
}

#define cmpxchg(ptr,o,n)						      \
	({ __typeof__(*(ptr)) _o_ = (o);				      \
	   __typeof__(*(ptr)) _n_ = (n);				      \
	   (__typeof__(*(ptr))) __cmpxchg((ptr), (unsigned long)_o_,	      \
	   			        (unsigned long)_n_, sizeof (*(ptr))); \
	})




/*
 * xchg_u32
 *
 * Note that a15 is used here because the register allocation
 * done by the compiler is not guaranteed and a window overflow
 * may not occur between the rsil and wsr instructions. By using
 * a15 in the rsil, the machine is guaranteed to be in a state
 * where no register reference will cause an overflow.
 */

static inline unsigned long
xchg_u32(volatile int *p, unsigned long val)
{
#if XCHAL_HAVE_S32C1I
        unsigned long tmp, result;
        __asm__ __volatile__(
"1:     l32i    %0, %2, 0               \n"
"       beq     %0, %3, 2f              \n"
"       wsr     %0, SCOMPARE1           \n"
"       mov     %1, %0                  \n"
"       mov     %0, %3                  \n"
"       s32c1i  %0, %2, 0               \n"
"       bne     %0, %1, 1b              \n"
"2:                                     \n"
        : "=&a" (result), "=&a" (tmp)
        : "a" (p), "a" (val)
        : "memory"
        );
        return result;
#else
  unsigned long tmp;
  __asm__ __volatile__("rsil    a15, "__stringify(LOCKLEVEL)"\n\t"
		       "l32i    %0, %1, 0              \n\t"
		       "s32i    %2, %1, 0              \n\t"
		       "wsr     a15, "__stringify(PS)" \n\t"
		       "rsync                          \n\t"
		       : "=&a" (tmp)
		       : "a" (m), "a" (val)
		       : "a15", "memory");
  return tmp;
#endif
}


#ifdef CONFIG_CC_OPTIMIZE_FOR_DEBUGGING
static __inline__ void __xchg_called_with_bad_pointer(void)
{
	extern void panic(const char *fmt, ...);

	panic(__func__);
}
#else
/*
 * This only works if the compiler isn't horribly bad at optimizing.
 * gcc-2.5.8 reportedly can't handle this, but I define that one to
 * be dead anyway.
 */
extern void __xchg_called_with_bad_pointer(void);
#endif

static __inline__ unsigned long
__xchg(unsigned long x, volatile void * ptr, int size)
{
	switch (size) {
		case 4:
			return xchg_u32(ptr, x);
	}
	__xchg_called_with_bad_pointer();
	return x;
}

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

extern void set_except_vector(int n, void *addr);

static inline void spill_registers(void)
{
	unsigned int a0, ps;

	__asm__ __volatile__ (
		"movi	a14," __stringify (PS_EXCM_BIT) " | " __stringify(LOCKLEVEL) "\n\t"
		"mov	a12, a0\n\t"
		"rsr	a13," __stringify(SAR) "\n\t"
		"xsr	a14," __stringify(PS) "\n\t"
		"movi	a0, _spill_registers\n\t"
		"rsync\n\t"
		"callx0 a0\n\t"
		"mov	a0, a12\n\t"
		"wsr	a13," __stringify(SAR) "\n\t"
		"wsr	a14," __stringify(PS) "\n\t"
		:: "a" (&a0), "a" (&ps)
#ifdef CONFIG_FRAME_POINTER
		: "a2", "a3", "a4",       "a11", "a12", "a13", "a14", "a15", "memory");
#else
		: "a2", "a3", "a4", "a7", "a11", "a12", "a13", "a14", "a15", "memory");
#endif
}

#define arch_align_stack(x) (x)

#define UNUSED __attribute__((__unused__))

#endif	/* _XTENSA_SYSTEM_H */
