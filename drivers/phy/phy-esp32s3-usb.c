// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

struct esp32s3_usb_phy {
	struct device *dev;
	void __iomem *reg0;
	void __iomem *reg1;
};

static int esp32s3_usb_phy_power_on(struct phy *phy)
{
	struct esp32s3_usb_phy *esp32_phy = phy_get_drvdata(phy);

	iowrite32(0x00180000, esp32_phy->reg0);
	return 0;
}

static int esp32s3_usb_phy_set_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	struct esp32s3_usb_phy *esp32_phy = phy_get_drvdata(phy);

	dev_dbg(esp32_phy->dev, "%s: mode = %d, submode = %d\n",
		__func__, mode, submode);
	switch (mode) {
	case PHY_MODE_USB_HOST:
		iowrite32(0x001d5000, esp32_phy->reg1);
		return 0;
	case PHY_MODE_USB_DEVICE:
		iowrite32(0x001c3000, esp32_phy->reg1);
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct phy_ops esp32s3_usb_phy_ops = {
	.power_on = esp32s3_usb_phy_power_on,
	.set_mode = esp32s3_usb_phy_set_mode,
	.owner = THIS_MODULE,
};

static int esp32s3_usb_phy_probe(struct platform_device *pdev)
{
	struct esp32s3_usb_phy *esp32_phy;
	struct phy_provider *provider;
	struct phy *phy;

	esp32_phy = devm_kzalloc(&pdev->dev, sizeof(*esp32_phy), GFP_KERNEL);
	if (!esp32_phy)
		return -ENOMEM;
	esp32_phy->dev = &pdev->dev;
	platform_set_drvdata(pdev, esp32_phy);

	phy = devm_phy_create(esp32_phy->dev, NULL, &esp32s3_usb_phy_ops);
	if (IS_ERR(phy)) {
		dev_err(esp32_phy->dev, "Failed to create PHY: %ld\n",
			PTR_ERR(phy));
		return PTR_ERR(phy);
	}
	phy_set_drvdata(phy, esp32_phy);

	esp32_phy->reg0 = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(esp32_phy->reg0))
		return PTR_ERR(esp32_phy->reg0);
	esp32_phy->reg1 = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(esp32_phy->reg1))
		return PTR_ERR(esp32_phy->reg1);

	provider = devm_of_phy_provider_register(esp32_phy->dev,
						 of_phy_simple_xlate);
	if (IS_ERR(provider)) {
		dev_err(esp32_phy->dev, "Failed to register PHY provider: %ld\n",
			PTR_ERR(provider));
		return PTR_ERR(provider);
	}

	return 0;
}

static const struct of_device_id esp32s3_usb_phy_of_match[] = {
	{ .compatible = "esp,esp32s3-usb-phy", },
	{ },
};
MODULE_DEVICE_TABLE(of, esp32s3_usb_phy_of_match);

static struct platform_driver esp32s3_usb_phy_driver = {
	.probe		= esp32s3_usb_phy_probe,
	.driver		= {
		.name	= "esp32s3-usb-phy",
		.of_match_table = esp32s3_usb_phy_of_match,
	},
};
module_platform_driver(esp32s3_usb_phy_driver);

MODULE_AUTHOR("Max Filippov <jcmvbkbc@gmail.com>");
MODULE_DESCRIPTION("ESP32S3 built-in USB2.0 PHY driver");
MODULE_LICENSE("GPL");
