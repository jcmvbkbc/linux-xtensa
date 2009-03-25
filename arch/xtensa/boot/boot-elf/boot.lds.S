/*
 *  linux/arch/xtensa/boot/boot-elf/"boot.lds.S
 *
 *  Copyright (C) 2008 - 2009 by Tensilica Inc.
 *
 *  Chris Zankel <chris@zankel.net>
 *  Marc Gauthier <marc@tensilica.com
 *  Pete Delaney <piet@tensilica.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This demonstrates how to append code to the start of the kernel
 *  image, and boot the kernel after possibly remapping the MMU.  The
 *  new Xtensa V3 MMU which runs initially with idenity mapping, ie:
 *   			virtual == physical.
 */
#include <asm/variant/core.h>
#include <asm/core.h>			/* Remap Core Virtual Addresses */
OUTPUT_ARCH(xtensa)
ENTRY(_ResetVector)

/*
 * This code runs at the Reset Vector (0XFE000000) and includes
 * within it a section called 'image' make by the kernel 
 * makefiles that's linked to run at 0xD0000000 but starts running
 * at 0x00001000; just after the exception vectors. This can be viewed 
 * with objdump by looking at the Program Headers and Sections with:
 *
 *   xt-objdump -wph Image.elf
 *
 *	Program Header:
 *	    LOAD off    0x00001000 vaddr 0xd0001000 paddr 0x00001000 align 2**12
 *	         filesz 0x0019d29c memsz 0x0019d29c flags rw-
 *	    LOAD off    0x0019f000 vaddr 0xfe000000 paddr 0xfe000000 align 2**12
 *	         filesz 0x0000010e memsz 0x0000010e flags r-x
 *	
 *	Sections:
 *	Idx Name              Size      VMA       LMA       File off  Algn  Flags
 *	  0 .ResetVector.text 0000010e  fe000000  fe000000  0019f000  2**2  CONTENTS, ALLOC, LOAD, READONLY, CODE
 *	  1 .image            0019d29c  d0001000  00001000  00001000  2**0  CONTENTS, ALLOC, LOAD, DATA
 */	

SECTIONS
{

	.ResetVector.text XCHAL_RESET_VECTOR_VADDR :
	{
		*(.ResetVector.text)
	}


	/*      0xD0001000          0x00001000							                 */
	.image KERNELOFFSET: AT (LOAD_MEMORY_ADDRESS)	/* KERNELOFFSET: - defined in core.h; default 0xD0001000 */
	{
		_image_start = .;
		*(image)
		. = (. + 3) & ~ 3;
		_image_end = .	;
	}


	.bss ((LOADADDR(.image) + SIZEOF(.image) + 3) & ~ 3):
	{
		__bss_start = .;
		*(.sbss)
		*(.scommon)
		*(.dynbss)
		*(.bss)
		__bss_end = .;
	}

	/*
	 * This is a remapped copy of the Reset Vector Code.
	 * It keeps gdb in sync with the PC after switching
	 * to the temporary mapping used while setting up 
	 * the V2 MMU mappings for Linux.
	 */
	.ResetVector.remapped_text 0X46000000 (INFO):
	{
		*(.ResetVector.remapped_text)
	}
}
