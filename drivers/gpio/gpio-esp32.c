// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/bits.h>
#include <linux/gpio/driver.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define DRIVER_NAME	"esp32s3-gpio"

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

#define GPIO_PIN0_REG		0x74
#define GPIO_PIN_PAD_DRIVER		BIT(2)
#define GPIO_PIN_INT_TYPE		GENMASK(9, 7)
#define GPIO_PIN_INT_TYPE_NONE		0
#define GPIO_PIN_INT_TYPE_RISING_EDGE	1
#define GPIO_PIN_INT_TYPE_FALLING_EDGE	2
#define GPIO_PIN_INT_TYPE_ANY_EDGE	3
#define GPIO_PIN_INT_TYPE_LOW_LEVEL	4
#define GPIO_PIN_INT_TYPE_HIGH_LEVEL	5
#define GPIO_PIN_INT_ENA		GENMASK(17, 13)
#define GPIO_PIN_INT_ENA_INT		BIT(13)
#define GPIO_PIN_INT_ENA_NMI		BIT(14)

#define ESP32S3_GPIOS		49

struct esp32_gpio {
	void __iomem *base;
	struct gpio_chip gc;
	int irq;
};

static int esp32_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	struct esp32_gpio *chip = gpiochip_get_data(gc);

	if (offset >= ESP32S3_GPIOS)
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

	if (offset >= ESP32S3_GPIOS)
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

	if (offset >= ESP32S3_GPIOS)
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

	if (offset >= ESP32S3_GPIOS)
		return -EINVAL;

	esp32_gpio_set_output(chip, offset, value);
	esp32_gpio_set_direction(chip, offset, true);
	return 0;
}

static int esp32_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct esp32_gpio *chip = gpiochip_get_data(gc);

	if (offset >= ESP32S3_GPIOS)
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

static int esp32_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct esp32_gpio *chip;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(chip->base))
		return PTR_ERR(chip->base);

	chip->irq = platform_get_irq(pdev, 0);
	if (chip->irq < 0)
		return -ENODEV;

	chip->gc = (struct gpio_chip){
		.label = "esp32s3-gpio",
		.parent = dev,
		.owner = THIS_MODULE,
		.base = -1,
		.ngpio = ESP32S3_GPIOS,
		.get_direction = esp32_gpio_get_direction,
		.direction_input = esp32_gpio_direction_input,
		.direction_output = esp32_gpio_direction_output,
		.get = esp32_gpio_get,
		.get_multiple = esp32_gpio_get_multiple,
		.set = esp32_gpio_set,
		.set_multiple = esp32_gpio_set_multiple,
	};

	return gpiochip_add_data(&chip->gc, chip);
}

static const struct of_device_id esp32_gpio_dt_ids[] = {
	{ .compatible = "esp,esp32s3-gpio" },
	{ },
};
MODULE_DEVICE_TABLE(of, esp32_gpio_dt_ids);

static struct platform_driver esp32_gpio_driver = {
	.probe		= esp32_gpio_probe,
	.driver = {
		.name	= DRIVER_NAME,
		.of_match_table = esp32_gpio_dt_ids,
	},
};
module_platform_driver(esp32_gpio_driver)

MODULE_AUTHOR("Max Filippov <jcmvbkbc@gmail.com>");
MODULE_DESCRIPTION("ESP32 GPIO driver");
MODULE_LICENSE("GPL");
