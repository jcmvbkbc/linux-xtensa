// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

struct esp32_trng {
	void __iomem *mem;
	struct hwrng rng;
};

static int esp32_trng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct esp32_trng *trng = container_of(rng, struct esp32_trng, rng);
	int ret = 0;

	while (max >= sizeof(u32)) {
		*(u32 *)buf = readl(trng->mem);
		ret += sizeof(u32);
		buf += sizeof(u32);
		max -= sizeof(u32);
		/* esp32 TRM recommends reading RNG_DATA_REG at a maximum rate of 5MHz */
		ndelay(200);
	}
	return ret;
}

static int esp32_trng_probe(struct platform_device *pdev)
{
	int ret;
	struct esp32_trng *trng;
	struct device *dev = &pdev->dev;

	trng = devm_kzalloc(dev, sizeof(*trng), GFP_KERNEL);
	if (!trng)
		return -ENOMEM;

	trng->mem = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(trng->mem))
		return PTR_ERR(trng->mem);

	trng->rng.name = pdev->name;
	trng->rng.read = esp32_trng_read;
	trng->rng.quality = 900;

	ret = devm_hwrng_register(dev, &trng->rng);
	if (ret) {
		dev_err(dev, "failed to register rng device: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, trng);

	return 0;
}

static const struct of_device_id esp32_trng_of_match[] = {
	{ .compatible = "esp,esp32-trng", },
	{},
};
MODULE_DEVICE_TABLE(of, esp32_trng_of_match);

static struct platform_driver esp32_trng_driver = {
	.driver = {
		.name = "esp32-trng",
		.of_match_table	= esp32_trng_of_match,
	},
	.probe = esp32_trng_probe,
};

module_platform_driver(esp32_trng_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Max Filippov <jcmvbkbc@gmail.com>");
MODULE_DESCRIPTION("ESP32 true random number generator driver");
