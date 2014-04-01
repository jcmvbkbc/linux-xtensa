/*
 * Xtensa compile-time HAL parameters.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2014 Cadence Design Systems Inc.
 */

#include <variant/core.h>

const unsigned long xchal_have_be = XCHAL_HAVE_BE;

const unsigned long xchal_icache_linesize = XCHAL_ICACHE_LINESIZE;
const unsigned long xchal_dcache_linesize = XCHAL_DCACHE_LINESIZE;

const unsigned long xchal_icache_size = XCHAL_ICACHE_SIZE;
const unsigned long xchal_dcache_size = XCHAL_DCACHE_SIZE;

const unsigned long xchal_dcache_is_writeback = XCHAL_DCACHE_IS_WRITEBACK;

const unsigned long xchal_icache_ways = XCHAL_ICACHE_WAYS;
const unsigned long xchal_dcache_ways = XCHAL_DCACHE_WAYS;

const unsigned long xchal_icache_line_lockable = XCHAL_ICACHE_LINE_LOCKABLE;
const unsigned long xchal_dcache_line_lockable = XCHAL_DCACHE_LINE_LOCKABLE;

const unsigned long xchal_have_spanning_way = XCHAL_HAVE_SPANNING_WAY;
const unsigned long xchal_have_ptp_mmu = XCHAL_HAVE_PTP_MMU;

#ifdef CONFIG_MMU
const unsigned long xchal_itlb_arf_entries_log2 = XCHAL_ITLB_ARF_ENTRIES_LOG2;
const unsigned long xchal_dtlb_arf_entries_log2 = XCHAL_DTLB_ARF_ENTRIES_LOG2;
#endif
