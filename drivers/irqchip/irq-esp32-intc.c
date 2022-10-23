// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/of.h>
#include <linux/of_platform.h>

#define ESP32_INTC_DISCONNECTED 6

struct esp32_intc {
	void __iomem *base;
	int n_irq;
};

static void esp32_intc_write(struct esp32_intc *priv, int i, int p)
{
	pr_debug("%s: 0x%08x -> 0x%08lx\n",
		 __func__, p, (unsigned long)(priv->base + i * 4));
	writel(p, priv->base + i * 4);
}

static void esp32_intc_disconnect_all(struct esp32_intc *priv)
{
	int i;

	for (i = 0; i < priv->n_irq; ++i)
		esp32_intc_write(priv, i, ESP32_INTC_DISCONNECTED);
}

static struct irq_chip esp32_intc_chip = {
	.name			= "esp32-intc",
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.irq_set_type		= irq_chip_set_type_parent,
	.flags			= IRQCHIP_MASK_ON_SUSPEND |
				  IRQCHIP_SKIP_SET_WAKE,
#ifdef CONFIG_SMP
	.irq_set_affinity	= irq_chip_set_affinity_parent,
#endif
};

static int esp32_intc_domain_alloc(struct irq_domain *domain, unsigned int virq,
				   unsigned int nr_irqs, void *data)
{
	struct irq_fwspec *fwspec = data;
	struct esp32_intc *priv = domain->host_data;
	unsigned int i;

	pr_debug("%s virq = %d, nr = %d\n", __func__, virq, nr_irqs);
	for (i = 0; i < nr_irqs; ++i) {
		struct irq_fwspec pfwspec;
		irq_hw_number_t hwirq;
		int rc;

		pr_debug("  %d: param_count = %d, param[0] = %d, param[1] = %d\n",
			 i, fwspec[i].param_count,
			 fwspec[i].param[0], fwspec[i].param[1]);

		if (fwspec[i].param_count != 2 ||
		    fwspec[i].param[0] >= priv->n_irq)
		    return -EINVAL;

		hwirq = fwspec[i].param[1];

		pfwspec.fwnode = domain->parent->fwnode;
		pfwspec.param_count = 2;
		pfwspec.param[0] = hwirq;
		pfwspec.param[1] = 0;

		rc = irq_domain_alloc_irqs_parent(domain, virq + i, 1,
						  &pfwspec);
		if (rc) {
			pr_err("%s: irq_domain_alloc_irqs_parent returned %d\n",
			       __func__, rc);
			return rc;
		}

		esp32_intc_write(priv, fwspec[i].param[0], hwirq);
		rc = irq_domain_set_hwirq_and_chip(domain, virq + i, hwirq,
						   &esp32_intc_chip, NULL);
		if (rc) {
			pr_err("%s: irq_domain_set_hwirq_and_chip returned %d\n",
			       __func__, rc);
			return rc;
		}
	}
	return 0;
}

static void esp32_intc_domain_free(struct irq_domain *domain, unsigned int virq,
				   unsigned int nr_irqs)
{
	pr_debug("%s\n", __func__);
}

static const struct irq_domain_ops esp32_domain_ops = {
	.alloc = esp32_intc_domain_alloc,
	.free = esp32_intc_domain_free,
};

static int __init esp32_intc_hw_init(struct device_node *node,
				     struct esp32_intc **hw)
{
	struct platform_device *pdev;
	struct esp32_intc *priv;
	resource_size_t size;

	pdev = of_find_device_by_node(node);
	if (!pdev)
		return -ENODEV;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_of_iomap(&pdev->dev, node, 0, &size);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->n_irq = size / 4;

	*hw = priv;

	esp32_intc_disconnect_all(priv);

	return 0;
}

static int __init esp32_intc_init(struct device_node *node,
				  struct device_node *parent)
{
	struct irq_domain *parent_domain, *domain;
	struct esp32_intc *priv;
	int rc;

	pr_debug("%pOF %s\n", node, __func__);

	if (!parent) {
		pr_err("%pOF: no parent, giving up\n", node);
		return -ENODEV;
	}

	parent_domain = irq_find_host(parent);
	if (!parent_domain) {
		pr_err("%pOF: unable to obtain parent domain\n", node);
		return -ENXIO;
	}

	rc = esp32_intc_hw_init(node, &priv);
	if (rc < 0) {
		pr_err("%pOF: couldn't init hw\n", node);
		return rc;
	}

	domain = irq_domain_add_hierarchy(parent_domain, 0,
					  priv->n_irq,
					  node, &esp32_domain_ops,
					  priv);
	if (!domain) {
		pr_err("%pOF: failed to allocated domain\n", node);
		return -ENOMEM;
	}
	return 0;
}

IRQCHIP_PLATFORM_DRIVER_BEGIN(esp32_intc)
IRQCHIP_MATCH("esp,esp32-intc", esp32_intc_init)
IRQCHIP_PLATFORM_DRIVER_END(esp32_intc)
