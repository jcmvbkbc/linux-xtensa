// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/gpio/driver.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define DRIVER_NAME	"esp32xx-gpio"

#define GPIO_OUT_REG		0x04
#define GPIO_OUT_W1TS_REG	0x08
#define GPIO_OUT_W1TC_REG	0x0c
#define GPIO_OUT1_REG		0x10
#define GPIO_OUT1_W1TS_REG	0x14
#define GPIO_OUT1_W1TC_REG	0x18
#define GPIO_ENABLE_REG		0x20
#define GPIO_ENABLE_W1TS_REG	0x24
#define GPIO_ENABLE_W1TC_REG	0x28
#define GPIO_ENABLE1_REG	0x2c
#define GPIO_ENABLE1_W1TS_REG	0x30
#define GPIO_ENABLE1_W1TC_REG	0x34
#define GPIO_IN_REG		0x3c
#define GPIO_IN1_REG		0x40

#define GPIO_STATUS_REG		0x44
#define GPIO_STATUS_W1TS_REG	0x48
#define GPIO_STATUS_W1TC_REG	0x4c
#define GPIO_STATUS1_REG	0x50
#define GPIO_STATUS1_W1TS_REG	0x54
#define GPIO_STATUS1_W1TC_REG	0x58

#define GPIO_CPU_INT_REG	0x5c
#define GPIO_CPU_NMI_INT_REG	0x60
#define GPIO_CPU_INT1_REG	0x68
#define GPIO_CPU_NMI_INT1_REG	0x6c

#define GPIO_ACPU_INT_REG	0x60
#define GPIO_ACPU_NMI_INT_REG	0x64
#define GPIO_PCPU_INT_REG	0x68
#define GPIO_PCPU_NMI_INT_REG	0x6c
#define GPIO_ACPU_INT1_REG	0x74
#define GPIO_ACPU_NMI_INT1_REG	0x78
#define GPIO_PCPU_INT1_REG	0x7c
#define GPIO_PCPU_NMI_INT1_REG	0x80

#define ESP32_GPIO_PIN0_REG	0x88
#define ESP32S3_GPIO_PIN0_REG	0x74

#define GPIO_PIN_PAD_DRIVER		BIT(2)
#define GPIO_PIN_INT_TYPE		GENMASK(9, 7)
#define GPIO_PIN_INT_TYPE_NONE		0
#define GPIO_PIN_INT_TYPE_RISING_EDGE	1
#define GPIO_PIN_INT_TYPE_FALLING_EDGE	2
#define GPIO_PIN_INT_TYPE_ANY_EDGE	3
#define GPIO_PIN_INT_TYPE_LOW_LEVEL	4
#define GPIO_PIN_INT_TYPE_HIGH_LEVEL	5
#define GPIO_PIN_INT_ENA		GENMASK(17, 13)
#define ESP32S3_GPIO_PIN_INT_ENA_INT	BIT(13)
#define ESP32S3_GPIO_PIN_INT_ENA_NMI	BIT(14)
#define ESP32_GPIO_PIN_INT_ENA_INT	(BIT(13) | BIT(15))
#define ESP32_GPIO_PIN_INT_ENA_NMI	(BIT(14) | BIT(16))

#define ESP32_GPIOS		40
#define ESP32S3_GPIOS		49

struct esp32_gpio_variant {
	u32 ngpio;
	u32 pin0_reg;
	u32 int_ena;
	u32 int_base[2];
	const char *label;
};

static const struct esp32_gpio_variant esp32_gpio_variant = {
	.ngpio = ESP32_GPIOS,
	.pin0_reg = ESP32_GPIO_PIN0_REG,
	.int_ena = ESP32_GPIO_PIN_INT_ENA_INT,
	.int_base = { GPIO_ACPU_INT_REG, GPIO_ACPU_INT1_REG },
	.label = "esp32-gpio",
};

static const struct esp32_gpio_variant esp32s3_gpio_variant = {
	.ngpio = ESP32S3_GPIOS,
	.pin0_reg = ESP32S3_GPIO_PIN0_REG,
	.int_ena = ESP32S3_GPIO_PIN_INT_ENA_INT,
	.int_base = { GPIO_CPU_INT_REG, GPIO_CPU_INT1_REG },
	.label = "esp32s3-gpio",
};

struct esp32_gpio {
	void __iomem *base;
	void __iomem *pin_base;
	void __iomem *int_base[2];
	struct device_node *of_node;
	int irq;
	unsigned int ngpio;
	u32 int_ena;
	struct irq_domain *domain;
	struct gpio_chip gc;
};

static const struct irq_chip esp32_gpio_level_irqchip;
static const struct irq_chip esp32_gpio_edge_irqchip;

static int esp32_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct esp32_gpio *chip = gpiochip_get_data(gc);

	if (offset >= chip->ngpio)
		return -EINVAL;

	if (offset < 32)
		return !(readl(chip->base + GPIO_ENABLE_REG) & BIT(offset));
	else
		return !(readl(chip->base + GPIO_ENABLE1_REG) & BIT(offset - 32));
}

static int esp32_gpio_set_direction(struct esp32_gpio *chip,
				    unsigned int offset, bool output)
{
	unsigned long bit;

	if (offset >= chip->ngpio)
		return -EINVAL;

	if (offset < 32) {
		bit = BIT(offset);
		if (output)
			writel(bit, chip->base + GPIO_ENABLE_W1TS_REG);
		else
			writel(bit, chip->base + GPIO_ENABLE_W1TC_REG);
	} else {
		bit = BIT(offset - 32);
		if (output)
			writel(bit, chip->base + GPIO_ENABLE1_W1TS_REG);
		else
			writel(bit, chip->base + GPIO_ENABLE1_W1TC_REG);
	}
	return 0;
}

static int esp32_gpio_set_output(struct esp32_gpio *chip,
				 unsigned int offset, bool value)
{
	unsigned long bit;

	if (offset >= chip->ngpio)
		return -EINVAL;

	if (offset < 32) {
		bit = BIT(offset);
		if (value)
			writel(bit, chip->base + GPIO_OUT_W1TS_REG);
		else
			writel(bit, chip->base + GPIO_OUT_W1TC_REG);
	} else {
		bit = BIT(offset - 32);
		if (value)
			writel(bit, chip->base + GPIO_OUT1_W1TS_REG);
		else
			writel(bit, chip->base + GPIO_OUT1_W1TC_REG);
	}
	return 0;
}

static int esp32_gpio_direction_input(struct gpio_chip *gc, unsigned int offset)
{
	return esp32_gpio_set_direction(gpiochip_get_data(gc), offset, false);
}

static int esp32_gpio_direction_output(struct gpio_chip *gc,
				       unsigned int offset, int value)
{
	struct esp32_gpio *chip = gpiochip_get_data(gc);

	if (offset >= chip->ngpio)
		return -EINVAL;

	esp32_gpio_set_output(chip, offset, value);
	esp32_gpio_set_direction(chip, offset, true);
	return 0;
}

static int esp32_gpio_set_config(struct gpio_chip *gc,
				 unsigned int offset, unsigned long config)
{
	enum pin_config_param param = pinconf_to_config_param(config);
	struct esp32_gpio *chip = gpiochip_get_data(gc);
	u32 v;

	if (offset >= chip->ngpio)
		return -EINVAL;

	v = readl(chip->pin_base + offset * 4);

	switch (param) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		v |= GPIO_PIN_PAD_DRIVER;
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		v &= ~GPIO_PIN_PAD_DRIVER;
		break;
	default:
		return -ENOTSUPP;
	}

	writel(v, chip->pin_base + offset * 4);

	return 0;
}


static int esp32_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct esp32_gpio *chip = gpiochip_get_data(gc);

	if (offset >= chip->ngpio)
		return -EINVAL;

	if (offset < 32)
		return !!(readl(chip->base + GPIO_IN_REG) & BIT(offset));
	else
		return !!(readl(chip->base + GPIO_IN1_REG) & BIT(offset - 32));
}

static int esp32_gpio_get_multiple(struct gpio_chip *gc,
				   unsigned long *mask, unsigned long *bits)
{
	struct esp32_gpio *chip = gpiochip_get_data(gc);

	bits[0] = readl(chip->base + GPIO_IN_REG) & mask[0];
	bits[1] = readl(chip->base + GPIO_IN1_REG) & mask[1];
	return 0;
}

static void esp32_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	esp32_gpio_set_output(gpiochip_get_data(gc), offset, value);
}

static void esp32_gpio_set_multiple(struct gpio_chip *gc,
				    unsigned long *mask, unsigned long *bits)
{
	struct esp32_gpio *chip = gpiochip_get_data(gc);
	unsigned long v;

	v = bits[0] & mask[0];
	if (v)
		writel(v, chip->base + GPIO_OUT_W1TS_REG);
	v = ~bits[0] & mask[0];
	if (v)
		writel(v, chip->base + GPIO_OUT_W1TC_REG);

	v = bits[1] & mask[1];
	if (v)
		writel(v, chip->base + GPIO_OUT1_W1TS_REG);
	v = ~bits[1] & mask[1];
	if (v)
		writel(v, chip->base + GPIO_OUT1_W1TC_REG);
}

static void esp32_gpio_irq_mask(struct irq_data *irq_data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irq_data);
	struct esp32_gpio *chip = gpiochip_get_data(gc);
	unsigned int gpio_num = irq_data->hwirq;
	u32 v;

	gpiochip_disable_irq(gc, gpio_num);
	v = readl(chip->pin_base + gpio_num * 4);
	v &= ~chip->int_ena;
	writel(v, chip->pin_base + gpio_num * 4);
}

static void esp32_gpio_irq_unmask(struct irq_data *irq_data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irq_data);
	struct esp32_gpio *chip = gpiochip_get_data(gc);
	unsigned int gpio_num = irq_data->hwirq;
	u32 v;

	gpiochip_enable_irq(gc, gpio_num);
	v = readl(chip->pin_base + gpio_num * 4);
	v |= chip->int_ena;
	writel(v, chip->pin_base + gpio_num * 4);
}

static void esp32_gpio_irq_ack(struct irq_data *irq_data)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irq_data);
	struct esp32_gpio *chip = gpiochip_get_data(gc);
	unsigned int gpio_num = irq_data->hwirq;

	if (gpio_num < 32)
		writel(BIT(gpio_num), chip->base + GPIO_STATUS_W1TC_REG);
	else
		writel(BIT(gpio_num - 32), chip->base + GPIO_STATUS1_W1TC_REG);
}

static void esp32_gpio_irq_enable(struct irq_data *irq_data)
{
	esp32_gpio_irq_ack(irq_data);
	esp32_gpio_irq_unmask(irq_data);
}

static int esp32_gpio_set_irq_type(struct irq_data *irq_data, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irq_data);
	struct esp32_gpio *chip = gpiochip_get_data(gc);
	unsigned int gpio_num = irq_data->hwirq;
	u32 v;

	gpiochip_enable_irq(gc, gpio_num);
	v = readl(chip->pin_base + gpio_num * 4);
	v &= ~GPIO_PIN_INT_TYPE;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		v |= FIELD_PREP(GPIO_PIN_INT_TYPE, GPIO_PIN_INT_TYPE_RISING_EDGE);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		v |= FIELD_PREP(GPIO_PIN_INT_TYPE, GPIO_PIN_INT_TYPE_FALLING_EDGE);
		break;
	case IRQ_TYPE_EDGE_BOTH:
		v |= FIELD_PREP(GPIO_PIN_INT_TYPE, GPIO_PIN_INT_TYPE_ANY_EDGE);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		v |= FIELD_PREP(GPIO_PIN_INT_TYPE, GPIO_PIN_INT_TYPE_HIGH_LEVEL);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		v |= FIELD_PREP(GPIO_PIN_INT_TYPE, GPIO_PIN_INT_TYPE_LOW_LEVEL);
		break;
	default:
		return -EINVAL;
	}

	writel(v, chip->pin_base + gpio_num * 4);

	if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_chip_handler_name_locked(irq_data,
						 &esp32_gpio_level_irqchip,
						 handle_fasteoi_irq, NULL);
	else
		irq_set_chip_handler_name_locked(irq_data,
						 &esp32_gpio_edge_irqchip,
						 handle_level_irq, NULL);

	return 0;
}

static const struct irq_chip esp32_gpio_level_irqchip = {
	.name		= DRIVER_NAME,
	.irq_enable	= esp32_gpio_irq_enable,
	.irq_eoi	= esp32_gpio_irq_ack,
	.irq_mask	= esp32_gpio_irq_mask,
	.irq_unmask	= esp32_gpio_irq_unmask,
	.irq_set_type	= esp32_gpio_set_irq_type,
	.flags		= IRQCHIP_EOI_THREADED | IRQCHIP_EOI_IF_HANDLED |
			  IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static const struct irq_chip esp32_gpio_edge_irqchip = {
	.name		= DRIVER_NAME,
	.irq_enable	= esp32_gpio_irq_enable,
	.irq_ack	= esp32_gpio_irq_ack,
	.irq_mask	= esp32_gpio_irq_mask,
	.irq_unmask	= esp32_gpio_irq_unmask,
	.irq_set_type	= esp32_gpio_set_irq_type,
	.flags		= IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static void esp32_gpio_irqhandler(struct irq_desc *desc)
{
	struct esp32_gpio *chip =
		gpiochip_get_data(irq_desc_get_handler_data(desc));
	struct irq_chip *irqchip = irq_desc_get_chip(desc);
	struct irq_domain *irqdomain = chip->gc.irq.domain;
	unsigned long status;
	int offset;

	chained_irq_enter(irqchip, desc);

	status = readl(chip->int_base[0]);
	for_each_set_bit(offset, &status, 32)
		generic_handle_domain_irq(irqdomain, offset);

	status = readl(chip->int_base[1]);
	for_each_set_bit(offset, &status, chip->ngpio - 32)
		generic_handle_domain_irq(irqdomain, offset + 32);

	chained_irq_exit(irqchip, desc);
}

static int esp32_gpio_irq_register(struct esp32_gpio *chip, struct device *dev)
{
	struct gpio_irq_chip *girq = &chip->gc.irq;;

	gpio_irq_chip_set_chip(girq, &esp32_gpio_edge_irqchip);
	girq->parent_handler = esp32_gpio_irqhandler;
	girq->num_parents = 1;
	girq->parents = devm_kcalloc(dev, 1,
				     sizeof(*girq->parents),
				     GFP_KERNEL);
	if (!girq->parents)
		return -ENOMEM;

	girq->parents[0] = chip->irq;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_level_irq;

	return 0;
}

static const struct of_device_id esp32_gpio_dt_ids[] = {
	{ .compatible = "esp,esp32-gpio", .data = &esp32_gpio_variant },
	{ .compatible = "esp,esp32s3-gpio", .data = &esp32s3_gpio_variant },
	{ },
};
MODULE_DEVICE_TABLE(of, esp32_gpio_dt_ids);

static int esp32_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	static const struct of_device_id *match;
	const struct esp32_gpio_variant *variant;
	struct esp32_gpio *chip;
	int ret;

	match = of_match_device(esp32_gpio_dt_ids, &pdev->dev);
	if (!match)
		return -ENODEV;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(chip->base))
		return PTR_ERR(chip->base);

	variant = match->data;
	chip->pin_base = chip->base + variant->pin0_reg;
	chip->int_base[0] = chip->base + variant->int_base[0];
	chip->int_base[1] = chip->base + variant->int_base[1];
	chip->ngpio = variant->ngpio;
	chip->int_ena = variant->int_ena;

	chip->of_node = dev->of_node;
	chip->irq = platform_get_irq(pdev, 0);
	if (chip->irq < 0)
		return -ENODEV;

	chip->gc = (struct gpio_chip){
		.label = variant->label,
		.parent = dev,
		.owner = THIS_MODULE,
		.base = -1,
		.ngpio = variant->ngpio,
		.get_direction = esp32_gpio_get_direction,
		.direction_input = esp32_gpio_direction_input,
		.direction_output = esp32_gpio_direction_output,
		.set_config = esp32_gpio_set_config,
		.get = esp32_gpio_get,
		.get_multiple = esp32_gpio_get_multiple,
		.set = esp32_gpio_set,
		.set_multiple = esp32_gpio_set_multiple,
		.irq = {
			.fwnode = dev_fwnode(dev),
		},
	};

	ret = esp32_gpio_irq_register(chip, dev);
	if (ret < 0)
		return ret;

	return gpiochip_add_data(&chip->gc, chip);
}

static struct platform_driver esp32_gpio_driver = {
	.probe = esp32_gpio_probe,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = esp32_gpio_dt_ids,
	},
};
module_platform_driver(esp32_gpio_driver)

MODULE_AUTHOR("Max Filippov <jcmvbkbc@gmail.com>");
MODULE_DESCRIPTION("ESP32 GPIO driver");
MODULE_LICENSE("GPL");
