/*
 * include/asm-xtensa/platform-lx60/serial.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001, 2006 Tensilica Inc.
 */

#ifndef __ASM_XTENSA_LX60_SERIAL_H
#define __ASM_XTENSA_LX60_SERIAL_H

#include <platform/hardware.h>

#define BASE_BAUD ( DUART16552_XTAL_FREQ / 16 )

/* The 8250 driver checks IRQ is not 0 in order to enable the UART
   interrupt. This is not correct for the LX60 board where the UART
   interrupt is 0... */
#ifdef is_real_interrupt
#undef is_real_interrupt
#define is_real_interrupt(irq)	((irq) != NO_IRQ)
#endif

#ifdef __XTENSA_EL__
#define IO_BASE_1 (DUART16552_VADDR)
#elif defined(__XTENSA_EB__)
#define IO_BASE_1 (DUART16552_VADDR + 3)
#else
#error endianess not defined
#endif

#define SERIAL_PORT_DFNS					\
	{ .baud_base = BASE_BAUD, 				\
	  .irq = DUART16552_INTNUM,                             \
	  .flags = (ASYNC_BOOT_AUTOCONF|ASYNC_SKIP_TEST),       \
	  .iomem_base = (u8*) IO_BASE_1,			\
          .iomem_reg_shift = 2,					\
	  .io_type = SERIAL_IO_MEM, }

#endif /* __ASM_XTENSA_LX60_SERIAL_H */
