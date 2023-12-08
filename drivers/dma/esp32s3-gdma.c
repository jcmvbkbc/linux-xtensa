// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include "virt-dma.h"

#define ESP32_GDMA_MAX_SIZE	32768
#define ESP32_GDMA_CHANNELS	10

#define GDMA_CONF0_CH_REG		0x00
#define GDMA_RST_CH				BIT(0)
#define GDMA_IN_DSCR_BURST_EN_CH		BIT(2)
#define GDMA_OUT_AUTO_WRBACK_CH			BIT(2)
#define GDMA_IN_DATA_BURST_EN_CH		BIT(3)
#define GDMA_OUT_EOF_MODE_CH			BIT(3)
#define GDMA_MEM_TRANS_EN_CH			BIT(4)
#define GDMA_OUT_DSCR_BURST_EN_CH		BIT(4)
#define GDMA_OUT_DATA_BURST_EN_CH		BIT(5)
#define GDMA_CONF1_CH_REG		0x04
#define GDMA_DMA_IN_FIFO_FULL_THRS_CH		GENMASK(11, 0)
#define GDMA_CHECK_OWNER_CH			BIT(12)
#define GDMA_EXT_MEM_BK_SIZE_CH			GENMASK(14, 13)
#define GDMA_INT_RAW_CH_REG		0x08
#define GDMA_INT_ST_CH_REG		0x0c
#define GDMA_INT_ENA_CH_REG		0x10
#define GDMA_INT_CLR_CH_REG		0x14
#define GDMA_IN_DONE_CH_INT			BIT(0)
#define GDMA_OUT_DONE_CH_INT			BIT(0)
#define GDMA_IN_SUC_EOF_CH_INT			BIT(1)
#define GDMA_OUT_EOF_CH_INT			BIT(1)
#define GDMA_IN_ERR_EOF_CH_INT			BIT(2)
#define GDMA_OUT_DSCR_ERR_CH_INT		BIT(2)
#define GDMA_IN_DSCR_ERR_CH_INT			BIT(3)
#define GDMA_OUT_TOTAL_EOF_CH_INT		BIT(2)
#define GDMA_IN_DSCR_EMPTY_CH_INT		BIT(4)
#define GDMA_IN_FIFO_FULL_WM_CH_INT		BIT(5)
#define GDMA_FIFO_STATUS_CH_REG		0x18
#define GDMA_LINK_CH_REG		0x20
#define GDMA_LINK_ADDR_CH			GENMASK(19, 0)
#define GDMA_IN_LINK_AUTO_RET_CH		BIT(20)
#define GDMA_OUT_LINK_STOP_CH			BIT(20)
#define GDMA_IN_LINK_STOP_CH			BIT(21)
#define GDMA_OUT_LINK_START_CH			BIT(21)
#define GDMA_IN_LINK_START_CH			BIT(22)
#define GDMA_OUT_LINK_RESTART_CH		BIT(22)
#define GDMA_IN_LINK_RESTART_CH			BIT(23)
#define GDMA_OUT_LINK_PARK_CH			BIT(23)
#define GDMA_IN_LINK_PARK_CH			BIT(24)
#define GDMA_STATE_CH_REG		0x24
#define GDMA_SUC_EOF_DES_ADDR_CH_REG	0x28
#define GDMA_ERR_EOF_DES_ADDR_CH_REG	0x2c
#define GDMA_DSCR_CH_REG		0x30
#define GDMA_DSCR_BF0_CH_REG		0x34
#define GDMA_DSCR_BF1_CH_REG		0x38
#define GDMA_PRI_CH_REG			0x44
#define GDMA_PRI_CH				GENMASK(3, 0)
#define GDMA_PERI_SEL_CH_REG		0x48
#define GDMA_PERI_SEL_CH			GENMASK(5, 0)

#define GDMA_CH_REG_SIZE		0x60

#define GDMA_MISC_CONF_REG		0x3c8
#define GDMA_AHBM_RST_INTER			BIT(0)
#define GDMA_AHBM_RST_EXTER			BIT(1)
#define GDMA_ARB_PRI_DIS			BIT(2)
#define GDMA_CLK_EN				BIT(4)

#define GDMA_EXTMEM_REJECT_ADDR_REG	0x3f4
#define GDMA_EXTMEM_REJECT_ST_REG	0x3f8
#define GDMA_EXTMEM_REJECT_ATRR			GENMASK(1, 0)
#define GDMA_EXTMEM_REJECT_CHANNEL_NUM		GENMASK(5, 2)
#define GDMA_EXTMEM_REJECT_PERI_NUM		GENMASK(11, 6)

#define GDMA_EXTMEM_REJECT_INT_RAW_REG	0x3fc
#define GDMA_EXTMEM_REJECT_INT_ST_REG	0x400
#define GDMA_EXTMEM_REJECT_INT_ENA_REG	0x404
#define GDMA_EXTMEM_REJECT_INT_CLR_REG	0x408
#define GDMA_EXTMEM_REJECT_INT			BIT(0)

#define GDMA_DATE_REG			0x40c


struct esp32_dma_cfg {
	u32 channel_id;
	u32 target_id;
};

struct esp32_dma_chan {
	struct virt_dma_chan vc;
	u32 id;
};

struct esp32_dma_device {
	struct dma_device ddev;
	void __iomem *base;
	struct clk *clk;
	struct esp32_dma_chan chan[ESP32_GDMA_CHANNELS];
};

static struct dma_chan *esp32_dma_of_xlate(struct of_phandle_args *dma_spec,
					   struct of_dma *ofdma)
{
	struct esp32_dma_device *dmadev = ofdma->of_dma_data;
	struct device *dev = dmadev->ddev.dev;
	struct esp32_dma_cfg cfg;
	struct esp32_dma_chan *chan;
	struct dma_chan *c;

	if (dma_spec->args_count < 2) {
		dev_err(dev, "Bad number of cells\n");
		return NULL;
	}

	cfg.channel_id = dma_spec->args[0];
	cfg.target_id = dma_spec->args[1];

	if (cfg.channel_id >= ESP32_GDMA_CHANNELS) {
		dev_err(dev, "Bad channel and/or request id\n");
		return NULL;
	}

	chan = &dmadev->chan[cfg.channel_id];

	c = dma_get_slave_channel(&chan->vc.chan);
	if (!c) {
		dev_err(dev, "No more channels available\n");
		return NULL;
	}

	//esp32_dma_set_config(chan, &cfg);

	return c;
}

static const struct of_device_id esp32_dma_of_match[] = {
	{ .compatible = "esp,esp32s3-gdma", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, esp32_dma_of_match);

static int esp32_dma_probe(struct platform_device *pdev)
{
	struct esp32_dma_chan *chan;
	struct esp32_dma_device *dmadev;
	struct dma_device *dd;
	struct resource *res;
	struct reset_control *rst;
	int i, ret;

	dmadev = devm_kzalloc(&pdev->dev, sizeof(*dmadev), GFP_KERNEL);
	if (!dmadev)
		return -ENOMEM;

	dd = &dmadev->ddev;

	dmadev->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(dmadev->base))
		return PTR_ERR(dmadev->base);

	dmadev->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dmadev->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(dmadev->clk), "Can't get clock\n");

	ret = clk_prepare_enable(dmadev->clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "clk_prep_enable error: %d\n", ret);
		return ret;
	}

	rst = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(rst)) {
		ret = PTR_ERR(rst);
		if (ret == -EPROBE_DEFER)
			goto clk_free;
	} else {
		reset_control_deassert(rst);
	}

	dma_set_max_seg_size(&pdev->dev, ESP32_GDMA_MAX_SIZE);

	dd->dev = &pdev->dev;

	dma_cap_set(DMA_SLAVE, dd->cap_mask);
	dma_cap_set(DMA_PRIVATE, dd->cap_mask);
	dma_cap_set(DMA_CYCLIC, dd->cap_mask);
	dd->directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
#if 0
	dd->device_alloc_chan_resources = esp32_dma_alloc_chan_resources;
	dd->device_free_chan_resources = esp32_dma_free_chan_resources;
	dd->device_tx_status = esp32_dma_tx_status;
	dd->device_issue_pending = esp32_dma_issue_pending;
	dd->device_prep_slave_sg = esp32_dma_prep_slave_sg;
	dd->device_prep_dma_cyclic = esp32_dma_prep_dma_cyclic;
	dd->device_config = esp32_dma_slave_config;
	dd->device_pause = esp32_dma_pause;
	dd->device_resume = esp32_dma_resume;
	dd->device_terminate_all = esp32_dma_terminate_all;
	dd->device_synchronize = esp32_dma_synchronize;
	dd->src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
		BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
		BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	dd->dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
		BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
		BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	dd->residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;
	dd->copy_align = DMAENGINE_ALIGN_32_BYTES;
	dd->max_burst = STM32_DMA_MAX_BURST;
	dd->max_sg_burst = STM32_DMA_ALIGNED_MAX_DATA_ITEMS;
	dd->descriptor_reuse = true;
#endif
	INIT_LIST_HEAD(&dd->channels);

	for (i = 0; i < ESP32_GDMA_CHANNELS; ++i) {
		chan = &dmadev->chan[i];
		chan->id = i;
		//chan->vc.desc_free = esp32_dma_desc_free;
		vchan_init(&chan->vc, dd);
#if 0
		chan->mdma_config.ifcr = res->start;
		chan->mdma_config.ifcr += STM32_DMA_IFCR(chan->id);

		chan->mdma_config.tcf = STM32_DMA_TCI;
		chan->mdma_config.tcf <<= STM32_DMA_FLAGS_SHIFT(chan->id);
#endif
	}

	ret = dma_async_device_register(dd);
	if (ret)
		goto clk_free;

#if 0
	for (i = 0; i < STM32_DMA_MAX_CHANNELS; i++) {
		chan = &dmadev->chan[i];
		ret = platform_get_irq(pdev, i);
		if (ret < 0)
			goto err_unregister;
		chan->irq = ret;

		ret = devm_request_irq(&pdev->dev, chan->irq,
				       esp32_dma_chan_irq, 0,
				       dev_name(chan2dev(chan)), chan);
		if (ret) {
			dev_err(&pdev->dev,
				"request_irq failed with err %d channel %d\n",
				ret, i);
			goto err_unregister;
		}
	}
#endif

	ret = of_dma_controller_register(pdev->dev.of_node,
					 esp32_dma_of_xlate, dmadev);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"ESP32S# GDMA OF registration failed %d\n", ret);
		goto err_unregister;
	}

	platform_set_drvdata(pdev, dmadev);

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_put(&pdev->dev);

	dev_info(&pdev->dev, "ESP32S3 GDMA driver registered\n");

	return 0;

err_unregister:
	dma_async_device_unregister(dd);
clk_free:
	clk_disable_unprepare(dmadev->clk);

	return ret;
}

#ifdef CONFIG_PM
static int esp32_dma_runtime_suspend(struct device *dev)
{
	struct esp32_dma_device *dmadev = dev_get_drvdata(dev);

	clk_disable_unprepare(dmadev->clk);

	return 0;
}

static int esp32_dma_runtime_resume(struct device *dev)
{
	struct esp32_dma_device *dmadev = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(dmadev->clk);
	if (ret) {
		dev_err(dev, "failed to prepare_enable clock\n");
		return ret;
	}

	return 0;
}
#endif

#if 0 && defined CONFIG_PM_SLEEP
static int esp32_dma_pm_suspend(struct device *dev)
{
	struct esp32_dma_device *dmadev = dev_get_drvdata(dev);
	int id, ret, scr;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	for (id = 0; id < STM32_DMA_MAX_CHANNELS; id++) {
		scr = esp32_dma_read(dmadev, STM32_DMA_SCR(id));
		if (scr & STM32_DMA_SCR_EN) {
			dev_warn(dev, "Suspend is prevented by Chan %i\n", id);
			return -EBUSY;
		}
	}

	pm_runtime_put_sync(dev);

	pm_runtime_force_suspend(dev);

	return 0;
}

static int esp32_dma_pm_resume(struct device *dev)
{
	return pm_runtime_force_resume(dev);
}
#endif

#if 0
static const struct dev_pm_ops esp32_dma_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(esp32_dma_pm_suspend, esp32_dma_pm_resume)
	SET_RUNTIME_PM_OPS(esp32_dma_runtime_suspend,
			   esp32_dma_runtime_resume, NULL)
};
#endif

static struct platform_driver esp32_dma_driver = {
	.driver = {
		.name = "esp32-gdma",
		.of_match_table = esp32_dma_of_match,
		//.pm = &esp32_dma_pm_ops,
	},
	.probe = esp32_dma_probe,
};

static int __init esp32_dma_init(void)
{
	return platform_driver_register(&esp32_dma_driver);
}
subsys_initcall(esp32_dma_init);

