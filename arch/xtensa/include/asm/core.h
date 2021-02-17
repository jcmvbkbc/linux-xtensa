/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019 Cadence Design Systems Inc. */

#ifndef _ASM_XTENSA_CORE_H
#define _ASM_XTENSA_CORE_H

#include <variant/core.h>

#ifndef XCHAL_HAVE_EXCLUSIVE
#define XCHAL_HAVE_EXCLUSIVE 0
#endif

#ifndef XCHAL_HAVE_EXTERN_REGS
#define XCHAL_HAVE_EXTERN_REGS 0
#endif

#ifndef XCHAL_HAVE_MPU
#define XCHAL_HAVE_MPU 0
#endif

#ifndef XCHAL_HAVE_VECBASE
#define XCHAL_HAVE_VECBASE 0
#endif

#define XCHAL_MMU_VERSION_NOMMU 0
#define XCHAL_MMU_VERSION_MMUV2 2
#define XCHAL_MMU_VERSION_MMUV3 3
#define XCHAL_MMU_VERSION_ROCHESTER 4

#ifndef XCHAL_MMU_VERSION
#if XCHAL_HAVE_PTP_MMU
#if XCHAL_HAVE_SPANNING_WAY
#define XCHAL_MMU_VERSION XCHAL_MMU_VERSION_MMUV3
#else
#define XCHAL_MMU_VERSION XCHAL_MMU_VERSION_MMUV2
#endif
#else
#define XCHAL_MMU_VERSION XCHAL_MMU_VERSION_NOMMU
#endif
#endif

#ifndef XCHAL_SPANNING_WAY
#define XCHAL_SPANNING_WAY 0
#endif

#if XCHAL_HAVE_WINDOWED
#if defined(CONFIG_USER_ABI_DEFAULT) || defined(CONFIG_USER_ABI_CALL0_PROBE)
/* Whether windowed ABI is supported in userspace. */
#define USER_SUPPORT_WINDOWED
#endif
#if defined(__XTENSA_WINDOWED_ABI__) || defined(USER_SUPPORT_WINDOWED)
/* Whether windowed ABI is supported either in userspace or in the kernel. */
#define SUPPORT_WINDOWED
#endif
#endif

/* Xtensa ABI requires stack alignment to be at least 16 */
#if XCHAL_DATA_WIDTH > 16
#define XTENSA_STACK_ALIGNMENT	XCHAL_DATA_WIDTH
#else
#define XTENSA_STACK_ALIGNMENT	16
#endif

#endif
