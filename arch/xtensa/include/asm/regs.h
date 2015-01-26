/*
 * Copyright (c) 2006 Tensilica, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2.1 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307,
 * USA.
 */

#ifndef _XTENSA_REGS_H
#define _XTENSA_REGS_H

#include <variant/core.h>

/*  Special registers.  */

#define SREG_MR			32
#define SREG_IBREAKA		128
#define SREG_DBREAKA		144
#define SREG_DBREAKC		160
#define SREG_EPC		176
#define SREG_EPS		192
#define SREG_EXCSAVE		208
#define SREG_CCOMPARE		240
#define SREG_MISC		244

#if XCHAL_XEA_VERSION <= 2
#include <asm/regs-xea2.h>
#elif XCHAL_XEA_VERSION == 3
#include <asm/regs-xea3.h>
#else
#error Unsupported XEA version
#endif

#endif /* _XTENSA_REGS_H */
