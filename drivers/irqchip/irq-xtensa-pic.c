/*
 * Xtensa built-in interrupt controller
 *
 * Copyright (C) 2002 - 2013 Tensilica, Inc.
 * Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Chris Zankel <chris@zankel.net>
 * Kevin Chea
 */

#include <linux/bits.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/xtensa-pic.h>
#include <linux/of.h>

static void xtensa_irq_mask(struct irq_data *d)
{
	u32 irq_mask;

	irq_mask = xtensa_get_sr(intenable);
	irq_mask &= ~BIT(d->hwirq);
	xtensa_set_sr(irq_mask, intenable);
}

static void xtensa_irq_unmask(struct irq_data *d)
{
	u32 irq_mask;

	irq_mask = xtensa_get_sr(intenable);
	irq_mask |= BIT(d->hwirq);
	xtensa_set_sr(irq_mask, intenable);
}

static void xtensa_irq_ack(struct irq_data *d)
{
	xtensa_set_sr(BIT(d->hwirq), intclear);
}

static void xtensa_irq_eoi(struct irq_data *d)
{
}

static int xtensa_irq_retrigger(struct irq_data *d)
{
	unsigned int mask = BIT(d->hwirq);

	if (WARN_ON(mask & ~XCHAL_INTTYPE_MASK_SOFTWARE))
		return 0;
	xtensa_set_sr(mask, intset);
	return 1;
}

static struct irq_chip xtensa_irq_chip = {
	.name		= "xtensa",
	.irq_mask	= xtensa_irq_mask,
	.irq_unmask	= xtensa_irq_unmask,
	.irq_ack	= xtensa_irq_ack,
	.irq_eoi	= xtensa_irq_eoi,
	.irq_retrigger	= xtensa_irq_retrigger,
};

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

#ifdef CONFIG_IRQ_DOMAIN_HIERARCHY
static int xtensa_pic_irq_domain_alloc(struct irq_domain *domain,
				       unsigned int virq,
				       unsigned int nr_irqs, void *args)
{
	struct irq_fwspec *fwspec = args;
	irq_hw_number_t hwirq;
	int rc;

	if (WARN_ON(nr_irqs != 1))
		return -EINVAL;

	switch (fwspec->param_count) {
	case 1:
		hwirq = fwspec->param[0];
		break;
	case 2:
		if (fwspec->param[1] == 0)
			hwirq = fwspec->param[0];
		else if (fwspec->param[1] == 1)
			hwirq = xtensa_map_ext_irq(fwspec->param[0]);
		else
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	pr_debug("%s: virq = %d, hwirq = %ld\n", __func__, virq, hwirq);
	rc = irq_domain_set_hwirq_and_chip(domain, virq, hwirq,
					   &xtensa_irq_chip, NULL);
	if (rc)
		return rc;

	return xtensa_irq_map(domain, virq, hwirq);
}

static void xtensa_pic_irq_domain_free(struct irq_domain *domain, unsigned int virq,
				       unsigned int nr_irqs)
{
	unsigned int i;

	for (i = 0; i < nr_irqs; i++) {
		struct irq_data *d = irq_domain_get_irq_data(domain, virq + i);
		irq_domain_reset_irq_data(d);
	}
}
#endif

static const struct irq_domain_ops xtensa_irq_domain_ops = {
	.xlate = xtensa_pic_irq_domain_xlate,
	.map = xtensa_irq_map,
#ifdef CONFIG_IRQ_DOMAIN_HIERARCHY
	.alloc = xtensa_pic_irq_domain_alloc,
	.free = xtensa_pic_irq_domain_free,
#endif
};

int __init xtensa_pic_init_legacy(struct device_node *interrupt_parent)
{
	struct irq_domain *root_domain =
		irq_domain_add_legacy(NULL, NR_IRQS - 1, 1, 0,
				&xtensa_irq_domain_ops, &xtensa_irq_chip);
	irq_set_default_host(root_domain);
	return 0;
}

static int __init xtensa_pic_init(struct device_node *np,
		struct device_node *interrupt_parent)
{
	struct irq_domain *root_domain =
		irq_domain_add_linear(np, NR_IRQS, &xtensa_irq_domain_ops,
				&xtensa_irq_chip);
	irq_set_default_host(root_domain);
	return 0;
}
IRQCHIP_DECLARE(xtensa_irq_chip, "cdns,xtensa-pic", xtensa_pic_init);
