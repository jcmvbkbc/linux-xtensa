/*
 * include/asm-xtensa/spinlock.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_SPINLOCK_H
#define _XTENSA_SPINLOCK_H

#define __raw_spin_is_locked(x)	((x)->slock != 0)
#define __raw_spin_unlock_wait(lock) \
	do { while (__raw_spin_is_locked(lock)) cpu_relax(); } while (0)

#define __raw_spin_lock_flags(lock, flags) __raw_spin_lock(lock)

static inline void __raw_spin_lock(raw_spinlock_t *lock)
{
	unsigned long tmp;

	asm volatile(
"	movi	%0, 0		\n"
"	wsr	%0, SCOMPARE1	\n"
"1:	movi	%0, 1		\n"
"	s32c1i	%0, %1, 0	\n"
"	bnez	%0, 1b		\n"
	: "=&a" (tmp) : "a" (&lock->slock) : "memory");
}

static inline int __raw_spin_trylock(raw_spinlock_t *lock)
{
	unsigned long tmp;

	asm volatile(
"	movi	%0, 0		\n"
"	wsr	%0, SCOMPARE1	\n"
"	movi	%0, 1		\n"
"	s32c1i	%0, %1, 0	\n"
	: "=&a" (tmp) : "a" (&lock->slock) : "memory");

	return tmp == 0 ? 1 : 0;
}

static inline void __raw_spin_unlock(raw_spinlock_t *lock)
{
	unsigned long tmp;

	asm volatile(
"	movi	%0, 0		\n"
"	s32i	%0, %1, 0	\n"
	: "=&a" (tmp) : "a" (&lock->slock) : "memory");
}

// FIXME from ARM:
/* write_can_lock - would write_trylock() succeed? */
#define __raw_write_can_lock(x)		((x)->lock == 0x80000000)

static inline void __raw_write_lock(raw_rwlock_t *rw)
{
	unsigned long tmp;

	asm volatile(
"	movi	%0, 0		\n"
"	wsr	%0, SCOMPARE1	\n"
"1:	movi	%0, 1		\n"
"	slli	%0, %0, 31	\n"
"	s32c1i	%0, %1, 0	\n"
"	bnez	%0, 1b		\n"
	: "=&a" (tmp) : "a" (&rw->lock) : "memory");
}

static inline int __raw_write_trylock(raw_rwlock_t *rw)
{
	unsigned long tmp;

	asm volatile(
"	movi	%0, 0		\n"
"	wsr	%0, SCOMPARE1	\n"
"	movi	%0, 1		\n"
"	slli	%0, %0, 31	\n"
"	s32c1i	%0, %1, 0	\n"
	: "=&a" (tmp) : "a" (&rw->lock) : "memory");

	return tmp == 0 ? 1 : 0;
}

static inline void __raw_write_unlock(raw_rwlock_t *rw)
{
	unsigned long tmp;

	asm volatile(
"	movi	%0, 0		\n"
"	s32i	%0, %1, 0	\n"
	: "=&a" (tmp) : "a" (&rw->lock) : "memory");
}

static inline void __raw_read_lock(raw_rwlock_t *rw)
{
	unsigned long tmp;
	unsigned long result;

	asm volatile(
"1:	l32i	%1, %2, 0	\n"
"	addi	%0, %1, 1	\n"
"	bltz	%0, 1b		\n"
"	wsr	%1, SCOMPARE1	\n"
"	s32c1i	%0, %2, 0	\n"
"	bne	%0, %1, 1b	\n"
	: "=&a" (result), "=&a" (tmp) : "a" (&rw->lock) : "memory");
}

static inline int __raw_read_trylock(raw_rwlock_t *rw)
{
	unsigned long result;
	unsigned long tmp;

	asm volatile(
"	l32i	%1, %2, 0	\n"
"	addi	%0, %1, 1	\n"
"	bltz	%0, 1f		\n"
"	wsr	%1, SCOMPARE1	\n"
"	s32c1i	%0, %2, 0	\n"
"	sub	%0, %0, %1	\n"
"1:				\n"
	: "=&a" (result), "=&a" (tmp) : "a" (&rw->lock) : "memory");

	return result == 0;
}

static inline void __raw_read_unlock(raw_rwlock_t *rw)
{
	unsigned long tmp1, tmp2;

	asm volatile(
"1:	l32i	%1, %2, 0	\n"
"	addi	%0, %1, -1	\n"
"	wsr	%1, SCOMPARE1	\n"
"	s32c1i	%0, %2, 0	\n"
"	bne	%0, %1, 1b	\n"
	: "=&a" (tmp1), "=&a" (tmp2) : "a" (&rw->lock) : "memory");
}

#endif	/* _XTENSA_SPINLOCK_H */
