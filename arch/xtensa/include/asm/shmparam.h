/*
 * include/asm-xtensa/shmparam.h
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2001 - 2009 Tensilica Inc.
 */

#ifndef _XTENSA_SHMPARAM_H
#define _XTENSA_SHMPARAM_H

#include <asm/page.h>

/*
 * Xtensa can have variable size caches, and if
 * the size of single way is larger than the page size,
 * then we have to start worrying about cache aliasing
 * problems.
 */

#if defined(DCACHE_ALIASING_POSSIBLE)
# define SHMLBA	DCACHE_WAY_SIZE
#else
# define SHMLBA	PAGE_SIZE
#endif

#endif /* _XTENSA_SHMPARAM_H */
