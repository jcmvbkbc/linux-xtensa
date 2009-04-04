/*
 * arch/xtensa/include/asm/mxregs.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 - 2009 Tensilica Inc.
 */

#ifndef _XTENSA_MXREGS_H
#define _XTENSA_MXREGS_H

#ifdef CONFIG_ARCH_HAS_SMP

/*
 * RER/WER at, as	Read/write external register
 *	at: value
 *	as: address
 *
 * Address	Value
 * 00nn		0...0p..p	Interrupt Routing, route IRQ n to processor p
 * 01pp		0...0d..d	16 bits (d) 'ored' as single IPI to processor p
 * 0180		0...0m..m	Clear enable specified by mask (m)
 * 0184		0...0m..m	Set enable specified by mask (m)
 * 0190		0...0x..x	8-bit IPI partition register
 * 				VVVVVVVVPPPPUUUUUUUUUUUUUUUUU
 * 				V (10-bit) Release/Version
 * 				P ( 4-bit) Number of cores - 1
 * 				U (18-bit) ID
 * 01a0		i.......i	32-bit ConfigID
 * 0200		0...0m..m	RunStall core 'n'
 * 0220		c		Cache coherency enabled
 */

#define MIROUT(irq)	(0x000 + (irq))
#define MIPICAUSE(cpu)	(0x100 + (cpu))
#define MIPISET(cause)	(0x140 + (cause))
#define MIENG		0x180
#define MIENGSET	0x184
#define MIASG		0x188	/* Read Global Assert Register */
#define MIASGSET	0x18c	/* Set Global Addert Regiter */
#define MIPIPART	0x190
#define SYSCFGID	0x1a0
#define MPSCORE		0x200
#define CCON		0x220

#ifndef __ASSEMBLY__


static inline void set_er (unsigned long value, unsigned long addr)
{
        asm volatile ("wer %0, %1" : : "a" (value), "a" (addr) : "memory");
}

static inline unsigned long get_er (unsigned long addr)
{
        register unsigned long value;
        asm volatile ("rer %0, %1" : "=a" (value) : "a" (addr) : "memory");
        return value;
}

#endif /* __ASSEMBLY__ */

#endif /* CONFIG_ARCH_HAS_SMP */

#endif /* _XTENSA_MSREGS_H */

