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

#define raw_smp_processor_id() (current_thread_info()->cpu)

struct xtensa_cpuinfo {
	unsigned long	*pgd_cache;
	unsigned long	*pte_cache;
	unsigned long	pgtable_cache_sz;
};

#define cpu_logical_map(cpu)	(cpu)

enum ipi_msg_type {
	IPI_RESCHEDULE = 0,
	IPI_CALL_FUNC,
	IPI_NMI_DIE
};

extern void arch_send_call_function_ipi_mask(const struct cpumask *mask);
extern void arch_send_call_function_single_ipi(int cpu);

#endif	/* _XTENSA_SMP_H */
