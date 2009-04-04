/*
 * arch/xtensa/include/asm/initialize_mmu.h
 *
 * Initializes MMU:
 *
 *	For the new V3 MMU we remap the TLB from virtual == physical
 *	to the standard Linux mapping used in earlier MMU's.
 *
 *	The the MMU we also support a new configuration register that
 *	specifies how the  S32C1I instruction operates with the cache
 *	controller.
 *
 *	This file is typically used with a #include to insert this code
 *	inline. We tried using and ASM macro but the assembler can't
 *	pass back dwarf information to allow single stepping to be 
 *	easily followed. Using a simple #include of the code is more
 *	useful.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2008 - 2009 Tensilica, Inc.
 *
 *   Marc Gauthier <marc@tensilica.com>
 *   Pete Delaney <piet@tensilica.com>
 */

#ifdef __ASSEMBLY__

/*
 * Using V3-MMU temporarily as an indicator of support for the new ATOMCTL register 
 */
#if  XCHAL_HAVE_S32C1I && XCHAL_HAVE_PTP_MMU && XCHAL_HAVE_SPANNING_WAY
/* XCHAL_HAVE_S32C1I && (XCHAL_HW_MIN_VERSION >= XTENSA_HWVERSION_RC_2008_0) */


/* 
 * We Have Atomic Operation Control (ATOMCTL) Register; Initialize it.
 * This register determines the effect of using a S32C1I instruction
 * with various combinations of:
 *
 *	1. With and without an Coherent Cache Controller which
 *	   can do Atomic Transactions to the memory internally.
 *
 *	2. With and without An Intelligent Memory Controller which 
 *	   can do Atomic Transactions itself.
 *	
 * The Core comes up with a default value of for the three types of cache ops:
 *
 *	 0x28: (WB: Internal, WT: Internal, BY:Exception)
 *
 * On the FPGA Cards we typically simulate an Intelligent Memory controller
 * which can implement  RCW transactions. For FPGA cards with an External
 * Memory controller we let it to the atomic operations internally while
 * doing a Cached (WB) transaction and use the Memory RCW for un-cached
 * operations. 
 *
 * For systems without an coherent cache controller, non-MX, we always
 * use the memory controllers RCW, thought non-MX controlers likely
 * support the Internal Operation.
 *
 * CUSTOMER-WARNING:
 *    Virtually all customers buy their memory controllers from vendors that
 *    don't support atomic RCW memory transactions and will likely want to
 *    configure this register to not use RCW.
 *
 * Developers might find using RCW in Bypass mode convenient when testing
 * with the cache being bypassed; for example studying cache alias problems.
 *
 * See Section 4.3.13.4 of ISA; Bits:
 *
 *        			WB     WT      BY
 *                            5   4 | 3   2 | 1   0
 *   2 Bit
 *   Field
 *   Values	WB - Write Back    	WT - Write Thru	  	BY - Bypass
 * --------- 	---------------    	-----------------     ----------------
 *     0 	Exception 	  	Exception		Exception
 *     1  	RCW Transaction	  	RCW Transaction 	RCW Transaction
 *     2  	Internal Operation	Exception		Reserved
 *     3  	Reserved		Reserved		Reserved
 */
# if XCHAL_DCACHE_IS_COHERENT
	movi	a3, 0x25		/* MX -- internal for writeback, RCW otherwise */
# else
	movi	a3, 0x15		/* non-MX -- always RCW */
# endif
	wsr	a3, ATOMCTL
#endif	/* XCHAL_HAVE_S32C1I && (XCHAL_HW_MIN_VERSION >= XTENSA_HWVERSION_RC_2008_0) */


#if XCHAL_HAVE_PTP_MMU && XCHAL_HAVE_SPANNING_WAY  
/* 
 * Have MMU v3  
 */

#if !XCHAL_HAVE_VECBASE
# error "MMU v3 requires reloc vectors"
#endif

	//  FIXME:  this code needs to be assembled -mtext-section-literals,
	//	and contain an appropriate .literal_position that we jump over;
	//	the standard reset vector usually does this already, so it's
	//	not repeated here.
	//
	//  This code must execute *before* LITBASE gets initialized.

	//  ASSUMPTIONS:
	//
	//  This code fragment is run only on an MMU v3.
	//  TLBs are in their reset state.
	//  ITLBCFG and DTLBCFG are zero (reset state).
	//  RASID is 0x04030201 (reset state).
	//  PS.RING is zero (reset state).
	//  LITBASE is zero (reset state, PC-relative literals); required to be PIC.
	//  This code is located in one of the following address ranges:
	//
	//
	//	0xF0000000..0xFFFFFFFF	(will keep same address in MMU v2 layout;
	//				 typically ROM)
	//	0x00000000..0x07FFFFFF  (system RAM; this code is actually linked
	//				 at 0xD0000000..0xD7FFFFFF [cached]
	//				 or 0xD8000000..0xDFFFFFFF [uncached];
	//				 does it have to be the latter?
	//				 in any case, initially runs elsewhere
	//				 than linked, so have to be careful)
	//	(local instram/instrom)	(will move to ??? or not move?)
	//
	//
	//  TLB setup proceeds along the following steps.  Legend:
	//
	//	VA = virtual address (two upper nibbles of it);
	//	PA = physical address (two upper nibbles of it);
	//	pc = physical range that contains this code;
	//	LM = physical range that contains local memories.
	//
	//  After step 2, we jump to virtual address in 0x40000000..0x5fffffff
	//  that corresponds to next instruction to execute in this code.
	//  After step 4, we jump to intended (linked) address of this code.
	//
	//
	//      Step 0     Step1     Step 2     Step3     Step 4     Step5
	//   ============  =====  ============  =====  ============  =====
	//     VA      PA     PA    VA      PA     PA    VA      PA     PA
	//   ------    --     --  ------    --     --  ------    --     --
	//   E0..FF -> E0  -> E0  E0..FF -> E0         F0..FF -> F0  -> F0
	//   C0..DF -> C0  -> C0  C0..DF -> C0         E0..EF -> F0  -> F0
	//   A0..BF -> A0  -> A0  A0..BF -> A0         D8..DF -> 00  -> 00
	//   80..9F -> 80  -> 80  80..9F -> 80         D0..D7 -> 00  -> 00
	//   60..7F -> 60  -> 60  60..7F -> 60         ??..?? -> LM  -> LM
	//   40..5F -> 40         40..47 -> pc  -> pc  40..5F -> pc
	//   20..3F -> 20  -> 20  20..3F -> 20
	//   00..1F -> 00  -> 00  00..1F -> 00
	//
	// Initial way 6 mappings:
	//	vaddr=0x00000000 asid=0x01  paddr=0x00000000  ca=3  ITLB way 6 (512 MB)
	//	vaddr=0x20000000 asid=0x01  paddr=0x20000000  ca=3  ITLB way 6 (512 MB)
	//	vaddr=0x40000000 asid=0x01  paddr=0x40000000  ca=3  ITLB way 6 (512 MB)
	//	vaddr=0x60000000 asid=0x01  paddr=0x60000000  ca=3  ITLB way 6 (512 MB)
	//	vaddr=0x80000000 asid=0x01  paddr=0x80000000  ca=3  ITLB way 6 (512 MB)
	//	vaddr=0xa0000000 asid=0x01  paddr=0xa0000000  ca=3  ITLB way 6 (512 MB)
	//	vaddr=0xc0000000 asid=0x01  paddr=0xc0000000  ca=3  ITLB way 6 (512 MB)
	//	vaddr=0xe0000000 asid=0x01  paddr=0xe0000000  ca=4  ITLB way 6 (512 MB)
	//
	//
	//  Before we begin: double check our PC is in proper range.
	//  No need to do this of course, if you know it's an expected range;
	//  but you *do* need the first two instructions below (and first label),
	//  for later steps.
	//
	// Need to use a '_' prefix to prevent asm from doing a 'l32r' followed by a callx0.
	// Added a '-no-transform' to assembler options to prevent any transformations like this.
	//
	// WARNING:
	//	Code below in step 2b will use this address for a computed branch.
	//
	_call0	1f		// get PC in a PIC manner (don't rely on literal constants)
0:	j	2f		// a0 = pc; NOTE: we get here AGAIN in Step 2b after remapping to 0X46000000
				//                REMIND: likely easier to understand if we don't do this
				//                        and just branch to the right address in Step 2b.

	.align	4
1:	movi	a2, 0x10000000
	movi	a3, 0x18000000
	add	a2, a2, a0	// a2 = 0x10000000 + original_pc
	bltu	a2, a3, 1f	// is PC >= 0xF0000000, or PC < 0x08000000 ?

				//  Panic!  bad PC, something wasn't linked or loaded right.
				//  REMIND: consider do something better than an infinite loop here?  
				//          BREAK? SIMCALL to debugger?
9:	j	9b		// Something's wrong, PC out of expected range

1:	//  PC seems okay, proceed.

	//  Step 1:  invalidate mapping at 0x40000000..0x5FFFFFFF.

	movi	a2, 0x40000006	// 512MB region at vaddr 0x40000000, way 6
	idtlb	a2		// kick it out...
	iitlb	a2
	isync

	//  Step 2:  map 0x40000000..0x47FFFFFF to paddr containing this code.
	//           Pages TLB Cache Attribute Should be R/W Exec without Caching.
	//
#define CA_BYPASS 	(_PAGE_CA_BYPASS | _PAGE_HW_WRITE | _PAGE_HW_EXEC)
#define CA_WRITEBACK 	(_PAGE_CA_WB     | _PAGE_HW_WRITE | _PAGE_HW_EXEC)

	srli	a3, a0, 27		// get 128MB area containing PC ...
	slli	a3, a3, 27		// ...
	addi	a3, a3, CA_BYPASS	// bypass-cache access R/W Executable
	addi	a7, a2, -1		// 128MB region at vaddr 0x40000000, way 5
	wdtlb	a3, a7			// setup mapping...
	witlb	a3, a7			// ...
	isync				// ...

	//  Step 2b:  jump to self, using new mapping above
	//            we jump back to the begining at 0: above

	slli	a4, a0, 5	// clear upper 5 bits of PC (get 128MB relative offset)
	srli	a4, a4, 5	// ...
	addi	a5, a2, -6	// a5 = 0x40000000
	add	a4, a4, a5	// address of above "j 2f" in 128MB page at vaddr 0x40000000
	jx	a4		// Note: jumps to 0x46000043; xt-gdb switches to remapped text section
				// Like doing a j 0b

	//  Step 3:  unmap everything other than current area.
	//	     We start at 0x60000000, wrap around, and end with 0x20000000
	//  NOTE:
	//	     You can't have any breakpoint set in the kernel during this period.
	//	     xt-gdb won't be able to remove the breakpoints and you will lose
	//	     control after going back to the V2 MMU mappings.
	//
2:	movi	a4, 0x20000000
	add	a5, a2, a4	// start at 0x60000000 (+6 for way 6)
3:	idtlb	a5		// invalidate entry...
	iitlb	a5
	add	a5, a5, a4
	bne	a5, a2, 3b	// loop until we get to 0x40000000

	//  Step 4:  Setup MMU with the old V2 mappings.
	//
	//	Step 4a:
	//           This changes the size of all of pre-initialized TLB
	//	     entries in way 6 from 512MB to 256MB and changes the 
	//	     vaddrs by dividing them in half. Only Way 6 is effected
	//	     because curently it's the only way with variable size pages.	
	//
	movi	a6, 0x01000000	// way 6 page size = 256 MB (index 1)
	wsr	a6, ITLBCFG	// apply way 6 page size (Inst)
	wsr	a6, DTLBCFG	// apply way 6 page size (Data)
	isync

	//
	// TLB Way 6 is now set up for setting the old V2 mappings.
	// The TLB Currently should look like this:
	//
	// Showing way 5
	// vaddr=0x40000000 asid=0x01  paddr=0xf8000000  ca=3  ITLB way 5 (128 MB)	[ACTIVE]
	// vaddr=0x08000000 asid=0x00  paddr=0x00000000  ca=0  ITLB way 5 (128 MB)
	// vaddr=0x10000000 asid=0x00  paddr=0x00000000  ca=0  ITLB way 5 (128 MB)
	// vaddr=0x18000000 asid=0x00  paddr=0x00000000  ca=0  ITLB way 5 (128 MB)
	//
	// Showing way 6
	// vaddr=0x00000000 asid=0x00  paddr=0x00000000  ca=3  ITLB way 6 (256 MB)
	// vaddr=0x10000000 asid=0x00  paddr=0x20000000  ca=3  ITLB way 6 (256 MB)
	// vaddr=0x20000000 asid=0x00  paddr=0x40000000  ca=3  ITLB way 6 (256 MB)
	// vaddr=0x30000000 asid=0x00  paddr=0x60000000  ca=3  ITLB way 6 (256 MB)
	// vaddr=0x40000000 asid=0x00  paddr=0x80000000  ca=3  ITLB way 6 (256 MB)
	// vaddr=0x50000000 asid=0x00  paddr=0xa0000000  ca=3  ITLB way 6 (256 MB)
	// vaddr=0x60000000 asid=0x00  paddr=0xc0000000  ca=3  ITLB way 6 (256 MB)
	// vaddr=0x70000000 asid=0x00  paddr=0xe0000000  ca=3  ITLB way 6 (256 MB)

	// Step 4b:
	// Set up Way 5 TLB Entries:
	// 	vaddr=0xd0000000 asid=0x01  paddr=0x00000000  ca=7  ITLB way 5 (128 MB)
	// 	vaddr=0xd8000000 asid=0x01  paddr=0x00000000  ca=3  ITLB way 5 (128 MB)
	//
	movi	a5, 0xd0000005			// 128MB page at 0xd0000000 (way 5)
	movi	a4, CA_WRITEBACK		// paddr 0x00000000, writeback
	wdtlb	a4, a5
	witlb	a4, a5

	movi	a5, 0xd8000005			// 128MB page at 0xd8000000 (way 5)
	movi	a4, CA_BYPASS			// paddr 0x00000000, bypass
	wdtlb	a4, a5
	witlb	a4, a5

	
	// Step 4c:
	// Set up Way 6 TLB Entries:
	// 	vaddr=0xe0000000 asid=0x01  paddr=0x00000000  ca=7  ITLB way 5 (128 MB)
	// 	vaddr=0xf0000000 asid=0x01  paddr=0x00000000  ca=3  ITLB way 5 (128 MB)
	//
	movi	a5, 0xe0000006			// 256MB page at 0xe0000000 (way 6)
	movi	a4, 0xf0000000 + CA_WRITEBACK	// paddr 0xf0000000, writeback
	wdtlb	a4, a5
	witlb	a4, a5

	movi	a5, 0xf0000006			// 256MB page at 0xf0000000 (way 6)
	movi	a4, 0xf0000000 + CA_BYPASS	// paddr 0xf0000000, bypass
	wdtlb	a4, a5
	witlb	a4, a5

	// TODO:  local memory mapping

	isync

	//  Step 4b:  jump to self, using MMU v2 mappings.
	//  Well, just jump to where we've been linked to.

	movi	a4, 1f		// using a constant -- absolute jump
	jx	a4

1:
	//  Assuming VECBASE points to system RAM,
	//  bump it up to where system RAM can now be accessed (cached).
	//
	movi	a2, 0xd0000000
	rsr	a3, vecbase
	add	a2, a2, a3
	wsr	a2, vecbase

	//  Step 5:  remove temporary mapping.
	//           a7 = 0x40000005 (Way 5)
	idtlb	a7
	iitlb	a7
	isync		 	// If you lose control here while single ...
				// ... stepping it's likely  could be because ...
				// ... you had a linux break point enabled.

	//  In case it wasn't done yet, initialize PTEVADDR.
	//  Though in reality, its reset state probably should already be zero.
	//
	movi	a0, 0
	wsr	a0, PTEVADDR
	rsync

	// It's safe to enable kernel breakpoint now.
	//
	nop 					//  Done!

#else /* !(XCHAL_HAVE_PTP_MMU && XCHAL_HAVE_SPANNING_WAY) */

	nop
	nop
	nop
	nop
	nop

#endif /* XCHAL_HAVE_PTP_MMU && XCHAL_HAVE_SPANNING_WAY */
#endif /* __ASSEMBLY__ */
