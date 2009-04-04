/*
 * arch/xtensa/boot/boot-elf/bootstrap.S
 *
 * Low-level exception handling
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004 - 2009 by Tensilica Inc.
 *
 * Chris Zankel <chris@zankel.net>
 * Marc Gauthier <marc@tensilica.com>
 * Pete Delaney <piet@tensilica.com>
 */


// Derived a bit from from:
//     $Id: //depot/dev/Boreal/Xtensa/OS/xtos/reset-vector.S#11 $

// Copyright (c) 1999-2009 by Tensilica Inc.  ALL RIGHTS RESERVED.
// These coded instructions, statements, and computer programs are the
// copyrighted works and confidential proprietary information of Tensilica Inc.
// They may not be modified, copied, reproduced, distributed, or disclosed to
// third parties in any manner, medium, or form, in whole or in part, without
// the prior written consent of Tensilica Inc.

#include <asm/bootparam.h>
#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/cacheasm.h>
#include <linux/linkage.h>


//#include <xtensa/coreasm.h>
//#include <xtensa/config/specreg.h>


	.section	.ResetVector.text, "ax"
	.global         _ResetVector
	.global         reset			# Place 1st Breakpoint here

_ResetVector:
	_j _SetupMMU

	/*
	 *  Even if the processor supports the non-PC-relative L32R option,
	 *  it will always start up in PC-relative mode.  We take advantage of
	 *  this, and use PC-relative mode at least until we're sure the .lit4
	 *  section is in place (which is sometimes only after unpacking).
	 */
	.begin  no-absolute-literals
	.literal_position

	.align 4
RomInitAddr:
	.word 0xd0001000
RomBootParam:
	.word _bootparam

	.align  4
_SetupMMU:
	Offset = _SetupMMU - _ResetVector

/* 2nd Copy Linked at 0x4600000 for ddd and xt-gdb */
#include <asm/initialize_mmu.h>			

	.end    no-absolute-literals

	rsil    a0, XCHAL_DEBUGLEVEL-1		# Enable xt-gdb Breakpoints
	rsync

# 
# Place 1st Breakpoint Here. Until we get here the MMU is mapped virtual == physical
# for V3 MMU, so it's not possible in this case to set normal kernel breakpoints as
# the code wasn't mapped there yet; now it is.
#
reset:						
	l32r    a0, RomInitAddr
	l32r	a2, RomBootParam
	movi	a3, 0
	movi	a4, 0

	jx      a0				# Typically calls _startup; a2 = &BootParams

	.align 4
//	.section .bootstrap.data, "aw"

	.globl _bootparam			# See RomBootParam in bootstrap.S
_bootparam:
	.short	BP_TAG_FIRST
	.short	4
	.long	BP_VERSION
	.short	BP_TAG_LAST
	.short	0
	.long	0

	.section	.ResetVector.remapped_text, "x"
	.global         _RemappedResetVector

	.org 0					# Need to do org before literals

_RemappedResetVector:
	.begin  no-absolute-literals
	.literal_position


	_j	_RemappedSetupMMU
        . = _RemappedResetVector + Offset	# Position Remapped code to same location orig code.

_RemappedSetupMMU:
#include <asm/initialize_mmu.h>                  /* Include code to re-map MMU V3 if needed */

	.end    no-absolute-literals

