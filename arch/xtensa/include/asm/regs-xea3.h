/*
 * Copyright (c) 2015 Cadence Design Systems Inc.  All Rights Reserved.
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

#ifndef _XTENSA_REGS_XEA3_H
#define _XTENSA_REGS_XEA3_H

/* EXCCAUSE register fields */

#define EXCCAUSE_CAUSE_SHIFT		0
#define EXCCAUSE_CAUSE_MASK		0x00f
#define EXCCAUSE_SUBCAUSE_SHIFT		4
#define EXCCAUSE_SUBCAUSE_MASK		0xff0
#define EXCCAUSE_SUBCAUSE_TYPE_SHIFT	4
#define EXCCAUSE_SUBCAUSE_TYPE_MASK	0x0f0
#define EXCCAUSE_SUBCAUSE_INFO_SHIFT	8
#define EXCCAUSE_SUBCAUSE_INFO_MASK	0xf00


/* Exception cause codes */

#define EXCCAUSE_NO_EXCEPTION		0
#define EXCCAUSE_INSTRUCTION_USAGE	1
#define EXCCAUSE_DATA_USAGE		2
#define EXCCAUSE_EXTERNAL		3
#define EXCCAUSE_DEBUG			4
#define EXCCAUSE_SYSCALL		5
/* cause codes 6..9 are reserved */
#define EXCCAUSE_COPROCESSOR		10
#define EXCCAUSE_HARDWARE_FAILURE	11
#define EXCCAUSE_TLB_MISS		12
#define EXCCAUSE_MEMORY_MGMT_ERROR	13
/* cause code 14 is reserved */
#define EXCCAUSE_CUSTOM			15


/* INSTRUCTION_USAGE subcause codes */

#define EXCCAUSE_INSTRUCTION_USAGE_ILLEGAL_OPCODE	0
#define EXCCAUSE_INSTRUCTION_USAGE_ILLEGAL_PC		1
#define EXCCAUSE_INSTRUCTION_USAGE_IFETCH_CROSS		2
#define EXCCAUSE_INSTRUCTION_USAGE_ACCESS_VIOLATION	3


/* DATA_USAGE subcause codes */

#define EXCCAUSE_DATA_USAGE_ALIGNMENT			0
#define EXCCAUSE_DATA_USAGE_COMPUTATIONAL_ERROR		1
#define EXCCAUSE_DATA_USAGE_ILLEGAL_ACCESS		2
#define EXCCAUSE_DATA_USAGE_ACCESS_VIOLATION		3


/* EXTERNAL subcause codes */

#define EXCCAUSE_EXTERNAL_BUS_ERROR			0
#define EXCCAUSE_EXTERNAL_BUS_TIMEOUT			1
#define EXCCAUSE_EXTERNAL_UNCORRECTABLE_ERROR		2


/* DEBUG subcause codes */

#define EXCCAUSE_DEBUG_BREAK				0
#define EXCCAUSE_DEBUG_IBREAK				2
#define EXCCAUSE_DEBUG_DBREAK				3
#define EXCCAUSE_DEBUG_SINGLE_STEP			4


/* XEA2-compatible exception cause mappings */

#define EXCCAUSE_ILLEGAL_INSTRUCTION		0x001
#define EXCCAUSE_SYSTEM_CALL			0x005
#if 0
#define EXCCAUSE_INSTRUCTION_FETCH_ERROR	2
#define EXCCAUSE_LOAD_STORE_ERROR		3
#define EXCCAUSE_LEVEL1_INTERRUPT		4
#define EXCCAUSE_ALLOCA				5
#define EXCCAUSE_INTEGER_DIVIDE_BY_ZERO		6
#define EXCCAUSE_SPECULATION			7
#endif
#define EXCCAUSE_PRIVILEGED			0x031
#define EXCCAUSE_UNALIGNED			0x002
#if 0
//#define EXCCAUSE_INSTR_DATA_ERROR		12
//#define EXCCAUSE_LOAD_STORE_DATA_ERROR		13
//#define EXCCAUSE_INSTR_ADDR_ERROR		14
//#define EXCCAUSE_LOAD_STORE_ADDR_ERROR		15
#endif
#define EXCCAUSE_ITLB_MISS			0x00c
#define EXCCAUSE_ITLB_MULTIHIT			0x00d
#define EXCCAUSE_ITLB_PRIVILEGE			0x01d
#if 0
//#define EXCCAUSE_ITLB_SIZE_RESTRICTION		19
#endif
#define EXCCAUSE_FETCH_CACHE_ATTRIBUTE		0x131
#define EXCCAUSE_DTLB_MISS			0x01c
#define EXCCAUSE_DTLB_MULTIHIT			0x08d
#define EXCCAUSE_DTLB_PRIVILEGE			0x09d
#if 0
//#define EXCCAUSE_DTLB_SIZE_RESTRICTION		27
#endif
#define EXCCAUSE_LOAD_CACHE_ATTRIBUTE		0x132
#define EXCCAUSE_STORE_CACHE_ATTRIBUTE		0x232
#if 0
//#define EXCCAUSE_COPROCESSOR0_DISABLED		32
//#define EXCCAUSE_COPROCESSOR1_DISABLED		33
//#define EXCCAUSE_COPROCESSOR2_DISABLED		34
//#define EXCCAUSE_COPROCESSOR3_DISABLED		35
//#define EXCCAUSE_COPROCESSOR4_DISABLED		36
//#define EXCCAUSE_COPROCESSOR5_DISABLED		37
//#define EXCCAUSE_COPROCESSOR6_DISABLED		38
//#define EXCCAUSE_COPROCESSOR7_DISABLED		39
#endif

#define EXCCAUSE_DEBUG_BI			0x204
#define EXCCAUSE_DEBUG_BN			0x104
#define EXCCAUSE_DEBUG_IB			0x024
#define EXCCAUSE_DEBUG_DB			0x034
#define EXCCAUSE_DEBUG_SS			0x044

/*  PS register fields.  */

#define PS_SAR_SHIFT		24
#define PS_SAR_MASK		0x1f000000
#define PS_SS_SHIFT		20
#define PS_SS_MASK		0x00300000
#define PS_CAUSE_SHIFT		8
#define PS_STACK_LEN		3
#define PS_STACK_SHIFT		5
#define PS_STACK_MASK		0x000000e0
#define PS_RING_SHIFT		4
#define PS_RING_MASK		0x00000010
#define PS_DI_SHIFT		3
#define PS_DI_MASK		0x00000008

#define PS_STACK_INTERRUPT	0
#define PS_STACK_CROSS		1
#define PS_STACK_IDLE		2
#define PS_STACK_KERNEL		3
#define PS_STACK_FIRST_INT	4
#define PS_STACK_FIRST_KER	5
#define PS_STACK_PAGE		7

#define PS_SS_CONTINUE		0
#define PS_SS_ONE_INSTRUCTION	2
#define PS_SS_SS_EXCEPTION	3

/*  DBREAKCn register fields.  */

#define DBREAKC_MASK_BIT		0
#define DBREAKC_MASK_MASK		0x0000003F
#define DBREAKC_LOAD_BIT		30
#define DBREAKC_LOAD_MASK		0x40000000
#define DBREAKC_STOR_BIT		31
#define DBREAKC_STOR_MASK		0x80000000


/*  DEBUGCAUSE register fields.  */

#define DEBUGCAUSE_DEBUGINT_BIT		5	/* External debug interrupt */
#define DEBUGCAUSE_BREAKN_BIT		4	/* BREAK.N instruction */
#define DEBUGCAUSE_BREAK_BIT		3	/* BREAK instruction */
#define DEBUGCAUSE_DBREAK_BIT		2	/* DBREAK match */
#define DEBUGCAUSE_IBREAK_BIT		1	/* IBREAK match */
#define DEBUGCAUSE_ICOUNT_BIT		0	/* ICOUNT would incr. to zero */

#endif /* _XTENSA_REGS_XEA3_H */
