/*
 * arch/xtensa/platforms/s56xx/pci.c
 *
 * PCI specifics for the S56XX boards.
 *
 * Copyright (C) 2005 - 2006 Tensilica Inc.
 *
 * Chris Zankel <chris@zankel.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/pci.h>

#include <asm/irq.h>
#include <asm/system.h>

#include <asm/processor.h>
#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/pci.h>

#include <asm/platform/hardware.h>
#include <asm/variant/interrupt.h>

extern struct pci_ops s5_pci_ops;
extern void s5_pci_init(void);

/* ------------------------------------------------------------------------- */

/*
 * Map slot and pin number to an interrupt number.
 * SLOT		S56xx	SLOT 0	SLOT 1	SLOT 2
 * DEV		0	1	2	3
 * IDSEL	AD[16]	AD[17]	AD[18]	AD[19]
 * INTA#	INTA#	INTA#	INTB#	INTC#
 * INTB#	INTB#	INTB#	INTC#	INTD#
 * INTC#	INTC#	INTC#	INTD#	INTA#
 * INTD#	INTD#	INTD#	INTA#	INTB#
 */

int __init platform_pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	if (pin-- == 0)
		return -1;
	if (slot > 0)
		slot--;

	return S5000_INTNUM_PCIA + pin + slot;
}

/*
 * This function initializes the PCI bridge and enumerates the PCI bus.
 */

/*
 * S5 Memory Map
 *
 * F300.0000 -	128 MB PCI-Aperture Space for internal bus to master accesses
 * FAFF.FFFF
 *  F300.0000	64 MB MEM
 *  F700.0000	64 MB IO
 *
 * FB00.0000 -	PCI-X configuration registers
 * FB00.0FFF
 *
 * FB00.1000 -	PCI Configuration Space for internal bus to PCI master accesses
 * FB3F.FFFF
 */

#define PCI_IO_SPACE_CPU_BASE	0xf7000000UL
#define PCI_IO_SPACE_PCI_BASE	0x00000000UL
#define PCI_IO_SPACE_SIZE	0x04000000UL
#define PCI_MEM_SPACE_CPU_BASE	0xf3000000UL
#define PCI_MEM_SPACE_PCI_BASE	0xf3000000UL
#define PCI_MEM_SPACE_SIZE	0x04000000UL

int __init s5_pcibios_init(void)
{
	struct pci_controller *pci_ctrl;

	/* Initialize the pci controller. */

	s5_pci_init();

	/* Setup pci_controller structure */

	pci_ctrl = kzalloc(sizeof(*pci_ctrl), GFP_KERNEL);

	if (!pci_ctrl) {
		printk("PCI: cannot allocate space for the pci controller\n");
		return -ENOMEM;
	}

	pci_ctrl->first_busno = 0;
	pci_ctrl->last_busno = 0xff;
	pci_ctrl->ops = &s5_pci_ops;

#if 0
	pci_ctrl->io_space.start = PCI_IO_SPACE_PCI_BASE;
	pci_ctrl->io_space.end = PCI_IO_SPACE_PCI_BASE + PCI_IO_SPACE_SIZE;
	pci_ctrl->mem_space.start = PCI_MEM_SPACE_PCI_BASE;
	pci_ctrl->mem_space.end = PCI_MEM_SPACE_PCI_BASE + PCI_MEM_SPACE_SIZE;
#endif

	pci_ctrl->io_base = PCI_IO_SPACE_CPU_BASE;
	pci_ctrl->mem_base = 0;

	pcibios_init_resource(&pci_ctrl->io_resource,
			PCI_IO_SPACE_PCI_BASE, 
			PCI_IO_SPACE_PCI_BASE + PCI_IO_SPACE_SIZE,
			IORESOURCE_IO, "PCI host bridge");
	pcibios_init_resource(&pci_ctrl->mem_resource,
			PCI_MEM_SPACE_CPU_BASE, 
			PCI_MEM_SPACE_CPU_BASE + PCI_MEM_SPACE_SIZE,
			IORESOURCE_MEM, "PCI host bridge");

	pcibios_register_controller(pci_ctrl);

	return 0;
}
arch_initcall(s5_pcibios_init);
