
#include <linux/linkage.h>

#include <asm/regs.h>
#include <asm/mxregs.h>
#include <asm/cacheasm.h>


	.section .SecondaryResetVector.text, "ax"


ENTRY(_SecondaryResetVector)

	/* Initialize WB, WS, and clear PS.EXCM (to allow 0-loops). */

	movi	a0, 0
	movi	a1, 1

	wsr	a0, WINDOWBASE
	wsr	a1, WINDOWSTART
	wsr	a1, PS
	rsync

#if XCHAL_DCACHE_LINE_LOCKABLE
	___unlock_dcache_all a2 a3
#endif

#if XCHAL_ICACHE_LINE_LOCKABLE
	___unlock_icache_all a2 a3
#endif
	___invalidate_dcache_all a2 a3
	___invalidate_icache_all a2 a3
	isync

	/* Add ourselves to the cache coherency controller. */

	movi	a2, CCON
	movi	a3, 1
	wer	a3, a2

	movi	a3, _startup
	jx	a3

