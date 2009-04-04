/*
 * include/asm-xtensa/smp.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_SMP_H
#define _XTENSA_SMP_H

#include <linux/cpumask.h>

#define raw_smp_processor_id() (current_thread_info()->cpu)


//#define cpu_data (&boot_cpu_data)
//#define current_cpu_data boot_cpu_data


#if 0
struct xtensa_cpuinfo {
	unsigned long asid_cache;
//	unsigned long	*pgd_cache;
//	unsigned long	*pte_cache;
//	unsigned long	pgtable_cache_sz;
};
//extern struct xtensa_cpuinfo boot_cpu_data;
extern struct xtensa_cpuinfo cpu_data[NR_CPUS];
#endif

#define cpu_logical_map(cpu)	(cpu)

enum ipi_msg_type {
	IPI_RESCHEDULE = 0,
	IPI_CALL_FUNC,
	IPI_NMI_DIE
};

#endif	/* _XTENSA_SMP_H */
