/*
 * arch/xtensa/include/asm/xchal_vaddr_remap.h
 *
 * Xtensa macros for MMU V3 Support. Deals with re-mapping the Virtual
 * Memory Addresses from "Virtual == Physical" to their prevvious V2 MMU
 * mappings (KSEG at 0xD0000000 and KIO at 0XF0000000). 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 - 2010 Tensilica Inc.
 *
 * Pete Delaney <piet@tensilica.com>
 * Marc Gauthier <marc@tensilica.com
 */

#ifndef _XTENSA_VECTORS_H
#define _XTENSA_VECTORS_H

#include <variant/core.h>

#define VIRTUAL_MEMORY_ADDRESS  	0xD0000000    /* Will Become VECBASE */
#define KERNELOFFSET            	0xD0002000    /* Image Virtual Start Address */

#if defined(XCHAL_HAVE_PTP_MMU) && XCHAL_HAVE_PTP_MMU && XCHAL_HAVE_SPANNING_WAY  
  /* MMU v3 */
  #define PHYSICAL_MEMORY_ADDRESS 	0x00000000
  #define LOAD_MEMORY_ADDRESS  	  	0x00002000
#else
  #define KERNELOFFSET            	0xD0002000
  #define LOAD_MEMORY_ADDRESS     	0xD0002000
#endif

#define XC_VADDR(offset)	(VIRTUAL_MEMORY_ADDRESS  + offset)
#define XC_PADDR(offset)	(PHYSICAL_MEMORY_ADDRESS + offset)

#define VECBASE_RESET_VADDR	VIRTUAL_MEMORY_ADDRESS	/* Used to set VECBASE register */

#define RESET_VECTOR_VECOFS	(XCHAL_RESET_VECTOR_VADDR - VECBASE_RESET_VADDR)
#define RESET_VECTOR_VADDR		XC_VADDR(RESET_VECTOR_VECOFS)

#define USER_VECTOR_VADDR		XC_VADDR(XCHAL_USER_VECOFS)
#define KERNEL_VECTOR_VADDR		XC_VADDR(XCHAL_KERNEL_VECOFS)
#define DOUBLEEXC_VECTOR_VADDR		XC_VADDR(XCHAL_DOUBLEEXC_VECOFS)
#define WINDOW_VECTORS_VADDR		XC_VADDR(XCHAL_WINDOW_OF4_VECOFS)
#define INTLEVEL2_VECTOR_VADDR		XC_VADDR(XCHAL_INTLEVEL2_VECOFS)
#define INTLEVEL3_VECTOR_VADDR		XC_VADDR(XCHAL_INTLEVEL3_VECOFS)
#define INTLEVEL4_VECTOR_VADDR		XC_VADDR(XCHAL_INTLEVEL4_VECOFS)
#define INTLEVEL5_VECTOR_VADDR		XC_VADDR(XCHAL_INTLEVEL5_VECOFS)
#define INTLEVEL6_VECTOR_VADDR		XC_VADDR(XCHAL_INTLEVEL6_VECOFS)

#define DEBUG_VECTOR_VADDR	XC_VADDR(XCHAL_DEBUG_VECOFS)

#undef  XCHAL_NMI_VECTOR_VADDR
#define XCHAL_NMI_VECTOR_VADDR		XC_VADDR(XCHAL_NMI_VECOFS)

#undef  XCHAL_INTLEVEL7_VECTOR_VADDR
#define XCHAL_INTLEVEL7_VECTOR_VADDR	XC_VADDR(XCHAL_INTLEVEL7_VECOFS)

/*
 * These XCHAL_* #defines from varian/core.h
 * are not valid to use with V3 MMU. Non-XCHAR
 * constants are defined above and should be used.
 */
#undef  XCHAL_VECBASE_RESET_VADDR
#undef  XCHAL_RESET_VECTOR0_VADDR
#undef  XCHAL_RESET_VECTOR1_VADDR
#undef  XCHAL_USER_VECTOR_VADDR
#undef  XCHAL_KERNEL_VECTOR_VADDR
#undef  XCHAL_DOUBLEEXC_VECTOR_VADDR
#undef  XCHAL_WINDOW_VECTORS_VADDR
#undef  XCHAL_INTLEVEL2_VECTOR_VADDR
#undef  XCHAL_INTLEVEL3_VECTOR_VADDR
#undef  XCHAL_INTLEVEL4_VECTOR_VADDR
#undef  XCHAL_INTLEVEL5_VECTOR_VADDR
#undef  XCHAL_INTLEVEL6_VECTOR_VADDR
#undef  XCHAL_DEBUG_VECTOR_VADDR
#undef  XCHAL_NMI_VECTOR_VADDR
#undef  XCHAL_INTLEVEL7_VECTOR_VADDR


#endif /* _XTENSA_VECTORS_H */
