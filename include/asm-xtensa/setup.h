/*
 * include/asm-xtensa/setup.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_SETUP_H
#define _XTENSA_SETUP_H

#define COMMAND_LINE_SIZE	256

#define SYSMEM_BANKS_MAX	4

typedef struct sysmem_bank {
	unsigned long start;
	unsigned long end;
	int node;
} sysmem_bank_t;

typedef struct sysmem_info {
	int nr_banks;
	sysmem_bank_t bank[SYSMEM_BANKS_MAX];
} sysmem_info_t;

extern sysmem_info_t __initdata sysmem;

#endif
