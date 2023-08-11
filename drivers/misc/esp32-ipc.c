// SPDX-License-Identifier: GPL-2.0-only

#include <linux/esp32-ipc-api.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include "esp32-ipc.h"

#define ESP32_IPC_IRQ_FROM_FW_REG	0
#define ESP32_IPC_IRQ_TO_FW_REG		4

#define ESP32_IPC_READ_HW_Q		0
#define ESP32_IPC_WRITE_HW_Q		1

#define ESP32_IPC_CLIENTS_MAX		4

struct esp32_ipc_client {
	void *p;
	int (*rx)(void *p, void *data);
	void (*rx_batch_done)(void *p, u32 tag);
};

struct esp32_ipc {
	void __iomem *regs;
	int irq;
	struct esp32_ipc_queue *hw_q;
	spinlock_t lock;
	struct esp32_ipc_client client[ESP32_IPC_CLIENTS_MAX];
};

int esp32_ipc_register_rx(void *ipc, u32 addr, void *p,
			int (*rx)(void *p, void *data),
			void (*rx_batch_done)(void *p, u32 tag))
{
	struct esp32_ipc *hw = ipc;

	if (addr < ESP32_IPC_CLIENTS_MAX) {
		hw->client[addr].p = p;
		hw->client[addr].rx = rx;
		hw->client[addr].rx_batch_done = rx_batch_done;
		return 0;
	}
	return -EINVAL;
}
EXPORT_SYMBOL(esp32_ipc_register_rx);

int esp32_ipc_tx(void *ipc, u32 addr, void *info, u32 *tag)
{
	struct esp32_ipc *hw = ipc;
	struct esp32_ipc_queue *q = hw->hw_q + ESP32_IPC_WRITE_HW_Q;
	struct esp32_ipc_queue_entry *data = (void *)hw->hw_q + q->offset;
	unsigned long flags;
	u32 r, w;

	spin_lock_irqsave(&hw->lock, flags);
	r = READ_ONCE(q->read);
	w = READ_ONCE(q->write);
	rmb();
	pr_debug("%s: q->read = %d, q->write = %d\n", __func__, r, w);
	if (w - r == q->mask) {
		spin_unlock_irqrestore(&hw->lock, flags);
		pr_debug("%s: write queue full\n", __func__);
		return -EBUSY;
	}

	WRITE_ONCE(data[w & q->mask].addr, addr);
	WRITE_ONCE(data[w & q->mask].info, info);
	wmb();
	if (tag)
		*tag = w;
	++w;
	WRITE_ONCE(q->write, w);
	spin_unlock_irqrestore(&hw->lock, flags);
	wmb();
	writel(1, hw->regs + ESP32_IPC_IRQ_TO_FW_REG);
	return 0;
}
EXPORT_SYMBOL(esp32_ipc_tx);

static irqreturn_t esp32_ipc_irq_handler(int irq, void *dev)
{
	struct esp32_ipc *hw = dev;

	if (hw->hw_q) {
		writel(0, hw->regs + ESP32_IPC_IRQ_FROM_FW_REG);
		return IRQ_WAKE_THREAD;
	}
	return IRQ_NONE;
}

static irqreturn_t esp32_ipc_thread_handler(int irq, void *dev)
{
	struct esp32_ipc *hw = dev;
	struct esp32_ipc_queue *q = hw->hw_q + ESP32_IPC_READ_HW_Q;
	struct esp32_ipc_queue_entry *data = (void *)hw->hw_q + q->offset;
	bool rx_done[ESP32_IPC_CLIENTS_MAX] = {0};
	u32 w, r;
	int i;

	for (;;) {
		w = READ_ONCE(q->write);
		r = READ_ONCE(q->read);
		rmb();
		if (r == w) {
			break;
		} else {
			u32 addr = READ_ONCE(data[r & q->mask].addr);
			void *info = READ_ONCE(data[r & q->mask].info);

			pr_debug("%s: read queue: r = %d, w = %d\n", __func__, r, w);
			if (addr < ESP32_IPC_CLIENTS_MAX && hw->client[addr].rx) {
				if (hw->client[addr].rx(hw->client[addr].p, info))
					rx_done[addr] = true;
			} else {
				pr_warn("%s: got message for addr = %d, no handler\n",
					__func__, addr);
			}
			++r;
			wmb();
			WRITE_ONCE(q->read, r);
		}
	}

	q = hw->hw_q + ESP32_IPC_WRITE_HW_Q;
	r = READ_ONCE(q->read);
	rmb();

	/* postprocessing calls */

	for (i = 0; i < ESP32_IPC_CLIENTS_MAX; ++i) {
		if (rx_done[i] && hw->client[i].rx_batch_done)
			hw->client[i].rx_batch_done(hw->client[i].p, r);
	}

	return IRQ_HANDLED;
}

static int init_hw(struct platform_device *pdev, struct esp32_ipc *hw)
{
	void __iomem *p;

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
	} else {
		return -ENODEV;
	}

	hw->irq = platform_get_irq(pdev, 0);
	if (hw->irq >= 0) {
		int ret;

		ret = devm_request_threaded_irq(&pdev->dev, hw->irq,
						esp32_ipc_irq_handler,
						esp32_ipc_thread_handler,
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

static int esp32_ipc_probe(struct platform_device *pdev)
{
	struct esp32_ipc *hw =
		devm_kzalloc(&pdev->dev, sizeof(*hw), GFP_KERNEL);
	int ret;

	if (!hw)
		return -ENOMEM;
	platform_set_drvdata(pdev, hw);

	spin_lock_init(&hw->lock);
	ret = init_hw(pdev, hw);
	if (ret < 0)
		return ret;

	hw->hw_q[ESP32_IPC_WRITE_HW_Q].read = 0;
	hw->hw_q[ESP32_IPC_WRITE_HW_Q].write = 0;
	wmb();
	writel(1, hw->regs + ESP32_IPC_IRQ_TO_FW_REG);

	ret = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	return ret;
}

static int esp32_ipc_remove(struct platform_device *pdev)
{
	struct esp32_ipc *hw = platform_get_drvdata(pdev);

	if (!hw)
		return 0;

	disable_irq(hw->irq);
	return 0;
}

static const struct of_device_id esp32_ipc_match[] = {
	{
		.compatible = "esp,esp32-ipc",
	}, {},
};
MODULE_DEVICE_TABLE(of, esp32_ipc_match);

static struct platform_driver esp32_ipc_driver = {
	.probe   = esp32_ipc_probe,
	.remove  = esp32_ipc_remove,
	.driver  = {
		.name = "esp32-ipc",
		.of_match_table = of_match_ptr(esp32_ipc_match),
	},
};

module_platform_driver(esp32_ipc_driver);
