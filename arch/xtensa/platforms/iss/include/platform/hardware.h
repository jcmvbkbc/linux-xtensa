/*
 * include/asm-xtensa/platform-iss/hardware.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 Tensilica Inc.
 */

/*
 * This file contains the default configuration of ISS.
 */

#ifndef _XTENSA_PLATFORM_ISS_HARDWARE_H
#define _XTENSA_PLATFORM_ISS_HARDWARE_H

/*
 * Memory configuration:
 *   Memory Limited by Kernel being mapped 
 * 	Cached: 0XD000,0000 to 0XD7FF,FFFF
 * 	Bypass: 0XD800,0000 to 0XDFFF,FFFF
 */
#define PLATFORM_DEFAULT_MEM_START	0x00000000
#define PLATFORM_DEFAULT_MEM_SIZE	0x08000000	/* 13,4217,728 128M */

/*
 * Interrupt configuration.
 */

#endif /* _XTENSA_PLATFORM_ISS_HARDWARE_H */
