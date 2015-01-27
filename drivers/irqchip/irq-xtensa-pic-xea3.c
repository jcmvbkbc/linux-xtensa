/*
 * Xtensa XEA3 built-in interrupt controller
 *
 * Copyright (C) 2002 - 2013 Tensilica, Inc.
 * Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/of.h>

#include "irqchip.h"

/*
 * Device Tree IRQ specifier translation function which works with one or
 * two cell bindings. First cell value maps directly to the hwirq number.
 * Second cell if present specifies whether hwirq number is external (1) or
 * internal (0).
 */
static int xtensa_pic_irq_domain_xlate(struct irq_domain *d,
		struct device_node *ctrlr,
		const u32 *intspec, unsigned int intsize,
		unsigned long *out_hwirq, unsigned int *out_type)
{
	return xtensa_irq_domain_xlate(intspec, intsize,
			intspec[0], intspec[0],
			out_hwirq, out_type);
}

static const struct irq_domain_ops xtensa_xea3_irq_domain_ops = {
	.xlate = xtensa_pic_irq_domain_xlate,
	.map = xtensa_irq_map,
};

static void xtensa_xea3_irq_mask(struct irq_data *d)
{
	set_er(0x100, 0x00122000 + 4 * d->hwirq);
}

static void xtensa_xea3_irq_unmask(struct irq_data *d)
{
	set_er(0x101, 0x00122000 + 4 * d->hwirq);
}

static void xtensa_xea3_irq_enable(struct irq_data *d)
{
	variant_irq_enable(d->hwirq);
	xtensa_xea3_irq_unmask(d);
}

static void xtensa_xea3_irq_disable(struct irq_data *d)
{
	xtensa_xea3_irq_mask(d);
	variant_irq_disable(d->hwirq);
}

static void xtensa_xea3_irq_ack(struct irq_data *d)
{
	set_er(0x200, 0x00122000 + 4 * d->hwirq);
}

static int xtensa_xea3_irq_retrigger(struct irq_data *d)
{
	set_er(0x202, 0x00122000 + 4 * d->hwirq);
	return 1;
}

static struct irq_chip xtensa_xea3_irq_chip = {
	.name		= "xtensa-xea3",
	.irq_enable	= xtensa_xea3_irq_enable,
	.irq_disable	= xtensa_xea3_irq_disable,
	.irq_mask	= xtensa_xea3_irq_mask,
	.irq_unmask	= xtensa_xea3_irq_unmask,
	//.irq_ack	= xtensa_xea3_irq_ack,
	.irq_retrigger	= xtensa_xea3_irq_retrigger,
};

int __init xtensa_pic_xea3_init_legacy(struct device_node *interrupt_parent)
{
	struct irq_domain *root_domain =
		irq_domain_add_legacy(NULL, NR_IRQS, 0, 0,
				      &xtensa_xea3_irq_domain_ops,
				      &xtensa_xea3_irq_chip);
	irq_set_default_host(root_domain);
	return 0;
}

static int __init xtensa_pic_xea3_init(struct device_node *np,
				       struct device_node *interrupt_parent)
{
	struct irq_domain *root_domain =
		irq_domain_add_linear(np, NR_IRQS, &xtensa_xea3_irq_domain_ops,
				      &xtensa_xea3_irq_chip);
	irq_set_default_host(root_domain);
	return 0;
}
IRQCHIP_DECLARE(xtensa_xea3_irq_chip, "cdns,xtensa-pic-xea3", xtensa_pic_xea3_init);
