/*
 * arch/xtensa/kernel/mxhead.S
 *
 * Xtensa Secondary Processors startup code.
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
 */

#include <linux/linkage.h>

#include <asm/initialize_mmu.h>
#include <asm/regs.h>
#include <asm/mxregs.h>
#include <asm/cacheasm.h>


	.section .SecondaryResetVector.text, "ax"


ENTRY(_SecondaryResetVector)
	_j _SetupOCD

	.begin  no-absolute-literals
	.literal_position

_SetupOCD:
	/* 
	 * Initialize WB, WS, and clear PS.EXCM (to allow loop instructions).
	 * Set Interrupt Level just below XCHAL_DEBUGLEVEL to allow 
	 * xt-gdb to single step via DEBUG exceptions received directly
	 * by ocd.
	 */
	movi	a0, 0
	movi	a1, 1

	wsr	a0, WINDOWBASE
	wsr	a1, WINDOWSTART

#if 1
	movi	a1, XCHAL_EXCM_LEVEL		# Typically 3
#else
	movi	a1, XCHAL_DEBUGLEVEL-1		# Typically 6 - 1
#endif
	wsr	a1, PS
	rsync


#if XCHAL_DCACHE_LINE_LOCKABLE
	___unlock_dcache_all a2 a3
#endif

#if XCHAL_ICACHE_LINE_LOCKABLE
	___unlock_icache_all a2 a3
#endif
	___invalidate_dcache_all a2 a3		# These use pc relative loop instructions
	___invalidate_icache_all a2 a3
	isync

	/* Add ourselves to the cache coherency controller. */

#ifdef CONFIG_HAVE_SMP
	movi	a2, CCON
	movi	a3, 1
	wer	a3, a2
#endif


_SetupMMU:
	Offset = _SetupMMU - _SecondaryResetVector


	initialize_mmu

	/*
	 * Start Secondary Processors with NULL pointer to boot params.
	 */
	movi	a2, 0				#  a2 == NULL
	movi	a3, _startup
	jx	a3

	 .end    no-absolute-literals	


	.section 	.SecondaryResetVector.remapped_text, "ax"
	.global         _RemappedSecondaryResetVector

	.org 0                                  # Need to do org before literals

_RemappedSecondaryResetVector:
	.begin  no-absolute-literals
	.literal_position
	
	_j      _RemappedSetupMMU
	. = _RemappedSecondaryResetVector + Offset       # Position Remapped code to same location as

_RemappedSetupMMU:

	initialize_mmu

	.end    no-absolute-literals
