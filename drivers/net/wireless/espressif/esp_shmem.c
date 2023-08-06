/*
 * SPDX-FileCopyrightText: 2015-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "esp.h"
#include "esp_if.h"
#include "esp_api.h"
#include "esp_bt_api.h"
#include "esp_kernel_port.h"
#include "esp_shmem.h"
#include "esp_stats.h"

#define SHMEM_BUF_SIZE			1600

#define ESP_SHMEM_IRQ_FROM_WIFI_REG	0
#define ESP_SHMEM_IRQ_TO_WIFI_REG	4

#define ESP_SHMEM_READ_HW_Q		0
#define ESP_SHMEM_WRITE_HW_Q		1

volatile u8 host_sleep;

struct esp_wifi_shmem
{
	void __iomem *regs;
	int irq;
	struct esp_wifi_shmem_queue *hw_q;
	u32 tx_done;
	struct sk_buff_head rx_q[MAX_PRIORITY_QUEUES];
	struct sk_buff_head tx_q;
	bool esp_reset_after_module_load;
	struct esp_adapter adapter;
};

static struct sk_buff *read_packet(struct esp_adapter *adapter)
{
	struct esp_wifi_shmem *hw = container_of(adapter, struct esp_wifi_shmem, adapter);
	struct sk_buff *skb = NULL;

	skb = skb_dequeue(&(hw->rx_q[PRIO_Q_HIGH]));
	if (!skb)
		skb = skb_dequeue(&(hw->rx_q[PRIO_Q_MID]));
	if (!skb)
		skb = skb_dequeue(&(hw->rx_q[PRIO_Q_LOW]));

	return skb;
}

static int write_packet(struct esp_adapter *adapter, struct sk_buff *skb)
{
	u32 max_pkt_size = SHMEM_BUF_SIZE - sizeof(struct esp_payload_header);
	struct esp_wifi_shmem *hw = container_of(adapter, struct esp_wifi_shmem, adapter);
	struct esp_skb_cb *cb = NULL;
	struct esp_wifi_shmem_queue *q = hw->hw_q + ESP_SHMEM_WRITE_HW_Q;
	void **data = (void *)hw->hw_q + q->offset;
	u32 r, w;

	if (!adapter || !skb || !skb->data || !skb->len) {
		pr_err("Invalid args\n");
		if (skb)
			dev_kfree_skb(skb);
		return -EINVAL;
	}

	if (skb->len > max_pkt_size) {
		pr_err("Drop pkt of len[%u] > max spi transport len[%u]\n",
		       skb->len, max_pkt_size);
		dev_kfree_skb(skb);
		return -EPERM;
	}

	r = READ_ONCE(q->read);
	w = READ_ONCE(q->write);
	rmb();
	pr_debug("%s: q->read = %d, q->write = %d\n", __func__, r, w);
	if (w - r == q->mask) {
		pr_debug("%s: write queue full\n", __func__);
		cb = (struct esp_skb_cb *)skb->cb;
		if (cb && cb->priv)
			esp_tx_pause(cb->priv);
		dev_kfree_skb(skb);
		return -EBUSY;
	}

	WRITE_ONCE(data[w & q->mask], skb->data);
	wmb();
	++w;
	WRITE_ONCE(q->write, w);
	wmb();
	writel(1, hw->regs + ESP_SHMEM_IRQ_TO_WIFI_REG);

	if (w - r == q->mask) {
		cb = (struct esp_skb_cb *)skb->cb;
		if (cb && cb->priv)
			esp_tx_pause(cb->priv);
	}
	skb_queue_tail(&hw->tx_q, skb);

	return 0;
}

static const struct esp_if_ops if_ops = {
	.read		= read_packet,
	.write		= write_packet,
};

void process_event_esp_bootup(struct esp_adapter *adapter, u8 *evt_buf, u8 len)
{
	struct esp_wifi_shmem *hw = container_of(adapter, struct esp_wifi_shmem, adapter);
	/* Bootup event will be received whenever ESP is booted.
	 * It is termed 'First bootup' when this event is received
	 * the first time module loaded. It is termed 'Second & onward bootup' when
	 * there is ESP reset (reason may be manual reset of ESP or any crash at ESP)
	 */
	u8 len_left = len, tag_len;
	u8 *pos;
	uint8_t iface_idx = 0;
	enum chipset_type_e hardware_type = ESP_FIRMWARE_CHIP_UNRECOGNIZED;

	if (!adapter)
		return;

	if (!evt_buf)
		return;

	/* Second & onward bootup, cleanup and re-init the driver */
	if (hw->esp_reset_after_module_load)
		set_bit(ESP_CLEANUP_IN_PROGRESS, &adapter->state_flags);

	pos = evt_buf;

	while (len_left) {

		tag_len = *(pos + 1);

		pr_debug("EVENT: %d\n", *pos);

		if (*pos == ESP_BOOTUP_CAPABILITY) {
			adapter->capabilities = *(pos + 2);
		} else if (*pos == ESP_BOOTUP_FW_DATA) {
			if (tag_len != sizeof(struct fw_data))
				pr_info("Length not matching to firmware data size\n");
			else
				if (process_fw_data((struct fw_data *)(pos + 2))) {
					esp_remove_card(adapter);
					return;
				}
		} else if (*pos == ESP_BOOTUP_FIRMWARE_CHIP_ID) {
			hardware_type = *(pos+2);
		} else if (*pos == ESP_BOOTUP_TEST_RAW_TP) {
			process_test_capabilities(*(pos + 2));
		} else {
			pr_warn("Unsupported tag in event\n");
		}

		pos += (tag_len+2);
		len_left -= (tag_len+2);
	}

	if ((hardware_type != ESP_FIRMWARE_CHIP_ESP32) &&
	    (hardware_type != ESP_FIRMWARE_CHIP_ESP32S2) &&
	    (hardware_type != ESP_FIRMWARE_CHIP_ESP32C3) &&
	    (hardware_type != ESP_FIRMWARE_CHIP_ESP32S3)) {
		pr_info("ESP chipset not recognized, ignoring [%d]\n", hardware_type);
		hardware_type = ESP_FIRMWARE_CHIP_UNRECOGNIZED;
	} else {
		pr_info("ESP chipset detected [%s]\n",
			hardware_type == ESP_FIRMWARE_CHIP_ESP32 ? "esp32" :
			hardware_type == ESP_FIRMWARE_CHIP_ESP32S2 ? "esp32-s2" :
			hardware_type == ESP_FIRMWARE_CHIP_ESP32C3 ? "esp32-c3" :
			hardware_type == ESP_FIRMWARE_CHIP_ESP32S3 ? "esp32-s3" :
			"unknown");
	}

	if (hw->esp_reset_after_module_load) {

		/* Second & onward bootup:
		 *
		 * SPI is software and not a hardware based module.
		 * When bootup event is received, we should discard all prior commands,
		 * old messages pending at network and re-initialize everything.
		 *
		 * Such handling is not required
		 * 1. for SDIO
		 *   as Removal of SDIO triggers complete Deinit and on SDIO insertion/
		 *   detection, i.e., after probing, initialization is triggered
		 *
		 * 2. On first bootup (if counterpart of this else)
		 *   First bootup event is received immediately after module insertion.
		 *   As all network or cmds are init and clean for the first time,
		 *   there is no need to re-init them
		 */

		skb_queue_purge(&hw->tx_q);

		for (iface_idx = 0; iface_idx < ESP_MAX_INTERFACE; iface_idx++) {

			struct esp_wifi_device *priv = adapter->priv[iface_idx];

			if (!priv)
				continue;

			if (priv->scan_in_progress)
				ESP_MARK_SCAN_DONE(priv, true);

			if (priv->ndev &&
			    wireless_dev_current_bss_exists(&priv->wdev)) {
				CFG80211_DISCONNECTED(priv->ndev,
						      0, NULL, 0, false, GFP_KERNEL);
			}
		}

		esp_remove_card(adapter);

		skb_queue_head_init(&hw->tx_q);
	}

	if (esp_add_card(adapter)) {
		pr_err("network iterface init failed\n");
	}

	process_capabilities(adapter);
	print_capabilities(adapter->capabilities);


	hw->esp_reset_after_module_load = true;
}

static int process_rx_buf(struct esp_wifi_shmem *hw, struct sk_buff *skb)
{
	struct esp_payload_header *header;
	u16 len = 0;
	u16 offset = 0;

	if (!skb)
		return -EINVAL;

	header = (struct esp_payload_header *) skb->data;

	if (header->if_type >= ESP_MAX_IF) {
		pr_err("%s: bad if_type %d\n", __func__, header->if_type);
		return -EINVAL;
	}

	offset = le16_to_cpu(header->offset);

	/* Validate received SKB. Check len and offset fields */
	if (offset != sizeof(struct esp_payload_header)) {
		pr_err("%s: bad offfset %d\n", __func__, offset);
		return -EINVAL;
	}

	len = le16_to_cpu(header->len);
	if (!len) {
		pr_err("%s: bad len %d\n", __func__, len);
		return -EINVAL;
	}

	len += sizeof(struct esp_payload_header);

	if (len > SHMEM_BUF_SIZE) {
		pr_err("%s: bad len %d\n", __func__, len);
		return -EINVAL;
	}

	/* Trim SKB to actual size */
	skb_trim(skb, len);

	/* enqueue skb for read_packet to pick it */
	if (header->if_type == ESP_INTERNAL_IF)
		skb_queue_tail(&hw->rx_q[PRIO_Q_HIGH], skb);
	else if (header->if_type == ESP_HCI_IF)
		skb_queue_tail(&hw->rx_q[PRIO_Q_MID], skb);
	else
		skb_queue_tail(&hw->rx_q[PRIO_Q_LOW], skb);

	return 0;
}

static bool esp_wifi_shmem_handle_rx(struct esp_wifi_shmem *hw, void *p)
{
	struct sk_buff *rx_skb = esp_alloc_skb(SHMEM_BUF_SIZE);
	u8 *rx_buf;
	int ret;

	if (!rx_skb)
		return false;

	rx_buf = skb_put(rx_skb, SHMEM_BUF_SIZE);
	memcpy(rx_buf, p, SHMEM_BUF_SIZE);

	ret = process_rx_buf(hw, rx_skb);
	if (ret)
		dev_kfree_skb(rx_skb);
	return ret == 0;
}

static void esp_wifi_shmem_handle_tx(struct esp_wifi_shmem *hw, u32 n)
{
	struct sk_buff *skb;
	bool resumed = false;
	u32 i;

	for (i = 0; i < n; ++i) {
		skb = skb_dequeue(&hw->tx_q);
		if (WARN_ON(!skb))
			break;
		if (!resumed && skb) {
			struct esp_skb_cb *cb = (struct esp_skb_cb *)skb->cb;

			if (cb->priv) {
				esp_tx_resume(cb->priv);
				resumed = true;
			}
		}
		dev_kfree_skb(skb);
	}
	hw->tx_done += n;
}

static irqreturn_t esp_wifi_shmem_irq_handler(int irq, void *dev)
{
	struct esp_wifi_shmem *hw = dev;

	if (hw->hw_q) {
		writel(0, hw->regs + ESP_SHMEM_IRQ_FROM_WIFI_REG);
		return IRQ_WAKE_THREAD;
	}
	return IRQ_NONE;
}

static irqreturn_t esp_wifi_shmem_thread_handler(int irq, void *dev)
{
	struct esp_wifi_shmem *hw = dev;
	struct esp_wifi_shmem_queue *q = hw->hw_q + ESP_SHMEM_READ_HW_Q;
	void **data = (void *)hw->hw_q + q->offset;
	u32 w, r;
	bool rx_done = false;

	for (;;) {
		w = READ_ONCE(q->write);
		r = READ_ONCE(q->read);
		rmb();
		if (r == w) {
			break;
		} else {
			void *p = READ_ONCE(data[r & q->mask]);

			pr_debug("%s: read queue: r = %d, w = %d\n", __func__, r, w);
			if (esp_wifi_shmem_handle_rx(hw, p))
				rx_done = true;
			++r;
			wmb();
			WRITE_ONCE(q->read, r);
		}
	}

	/* indicate reception of new packets */
	if (rx_done)
		esp_process_new_packet_intr(&hw->adapter);

	q = hw->hw_q + ESP_SHMEM_WRITE_HW_Q;
	w = READ_ONCE(q->write);
	r = READ_ONCE(q->read);
	rmb();
	pr_debug("%s: write queue: r = %d, w = %d\n", __func__, r, w);
	if (r != hw->tx_done)
		esp_wifi_shmem_handle_tx(hw, r - hw->tx_done);

	return IRQ_HANDLED;
}

static int init_hw(struct platform_device *pdev, struct esp_wifi_shmem *hw)
{
	void __user *p;

	p = devm_of_iomap(&pdev->dev, pdev->dev.of_node, 0, NULL);
	if (IS_ERR(p))
		return PTR_ERR(p);
	hw->hw_q = (void *)readl(p);
	devm_iounmap(&pdev->dev, p);

	hw->regs = devm_of_iomap(&pdev->dev, pdev->dev.of_node, 1, NULL);
	if (IS_ERR(hw->regs))
		return PTR_ERR(hw->regs);

	dev_dbg(&pdev->dev, "%s: regs = %p, queues = %p\n",
		__func__, hw->regs, hw->hw_q);
	if (hw->hw_q) {
		u32 i;

		for (i = 0; i < 2; ++i) {
			dev_dbg(&pdev->dev, "%s: queue %d: offset = %d, mask = %x\n",
				__func__, i,
				hw->hw_q[i].offset,
				hw->hw_q[i].mask);
		}
		hw->tx_done = hw->hw_q[ESP_SHMEM_WRITE_HW_Q].write;
	} else {
		return -ENODEV;
	}

	hw->irq = platform_get_irq(pdev, 0);
	if (hw->irq >= 0) {
		int ret;

		ret = devm_request_threaded_irq(&pdev->dev, hw->irq,
						esp_wifi_shmem_irq_handler,
						esp_wifi_shmem_thread_handler,
						IRQF_SHARED, pdev->name, hw);
		if (ret < 0) {
			dev_err(&pdev->dev, "request_irq %d failed\n", hw->irq);
			return ret;
		}
	} else {
		dev_err(&pdev->dev, "missing IRQ property\n");
		return -ENODEV;
	}

	return 0;
}

static int esp_wifi_shmem_probe(struct platform_device *pdev)
{
	struct esp_wifi_shmem *hw =
		devm_kzalloc(&pdev->dev, sizeof(*hw), GFP_KERNEL);
	int ret;
	int i;

	if (!hw)
		return -ENOMEM;
	platform_set_drvdata(pdev, hw);
	for (i = 0; i < MAX_PRIORITY_QUEUES; ++i)
		skb_queue_head_init(&hw->rx_q[i]);
	skb_queue_head_init(&hw->tx_q);

	ret = init_hw(pdev, hw);
	if (ret < 0)
		return ret;
	ret = esp_wifi_init(&hw->adapter, &if_ops);
	if (ret < 0)
		return ret;

	hw->hw_q[ESP_SHMEM_WRITE_HW_Q].read = 0;
	hw->hw_q[ESP_SHMEM_WRITE_HW_Q].write = 0;
	wmb();
	writel(1, hw->regs + ESP_SHMEM_IRQ_TO_WIFI_REG);

	esp_wifi_shmem_thread_handler(0, hw);

	return ret;
}

static int esp_wifi_shmem_remove(struct platform_device *pdev)
{
	struct esp_wifi_shmem *hw = platform_get_drvdata(pdev);
	int i;

	if (!hw)
		return 0;

	disable_irq(hw->irq);
	esp_wifi_deinit(&hw->adapter);
	for (i = 0; i < MAX_PRIORITY_QUEUES; ++i)
		skb_queue_purge(&hw->rx_q[i]);
	skb_queue_purge(&hw->tx_q);
	return 0;
}

static const struct of_device_id esp_wifi_shmem_match[] = {
	{
		.compatible = "esp,esp-wifi-shmem",
	}, {},
};
MODULE_DEVICE_TABLE(of, esp_wifi_shmem_match);

static struct platform_driver esp_wifi_shmem_driver = {
	.probe   = esp_wifi_shmem_probe,
	.remove  = esp_wifi_shmem_remove,
	.driver  = {
		.name = "esp-wifi-shmem",
		.of_match_table = of_match_ptr(esp_wifi_shmem_match),
	},
};

module_platform_driver(esp_wifi_shmem_driver);
