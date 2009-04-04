/*
 * arch/xtensa/include/asm/kgdb.h
 *
 * KGDB Support for Xtensa
 *
 * Copyright (C) 2004-2005 MontaVista Software Inc.
 * Author: Manish Lachwani <mlachwani@mvista.com>
 *
 * Copyright (C) 2008 - 2009 Tensilica Inc.
 * Author: Pete Delaney <piet@tensilica.com>
 */

#ifndef _ASM_KGDB_H_
#define _ASM_KGDB_H_

#include <asm/gdb-regs.h>

#ifndef __ASSEMBLY__
#define BUFMAX			2048
#define NUMREGBYTES		XTENSA_GDB_REGISTERS_SIZE
#define NUMCRITREGBYTES		XTENSA_GDB_REGISTERS_SIZE

/*
 * Length of breakpoint istruction in arch_kgdb_breakpoint().
 * It's located in arch/xtensa/kernel/kgdb.c so it's address
 * can be compared with when using an illegal instruction to
 * emulate a breakpoint.
 */
#if XCHAL_HAVE_DENSITY
# define BREAK_INSTR_SIZE       2
#else
# define BREAK_INSTR_SIZE       3
#endif

extern void arch_kgdb_breakpoint(void);
extern void break_inst(void);
extern void break_return(void);

#define CHECK_EXCEPTION_STACK()	1
#define CACHE_FLUSH_IS_SAFE	1

extern int kgdb_early_setup;

#endif				/* !__ASSEMBLY__ */
#endif				/* _ASM_KGDB_H_ */
