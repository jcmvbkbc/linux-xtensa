// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/spi/spi.h>

#define ESP32_SPI_DRIVER_NAME	"esp32_spi"

#define SPI_CMD_REG		0x00
#define SPI_UPDATE			BIT(23)
#define SPI_USR				BIT(24)

#define SPI_CTRL_REG		0x08
#define SPI_Q_POL			BIT(18)
#define SPI_D_POL			BIT(19)
#define SPI_HOLD_POL			BIT(20)
#define SPI_WP_POL			BIT(21)
#define SPI_RD_BIT_ORDER		GENMASK(24, 23)
#define SPI_WR_BIT_ORDER		GENMASK(26, 25)

#define SPI_CLOCK_REG		0x0c
#define SPI_CLKCNT_L			GENMASK(5, 0)
#define SPI_CLKCNT_H			GENMASK(11, 6)
#define SPI_CLKCNT_N			GENMASK(17, 12)
#define SPI_CLKDIV_PRE			GENMASK(21, 18)
#define SPI_CLK_EQU_SYSCLK		BIT(31)

#define SPI_USER_REG		0x10
#define SPI_DOUTDIN			BIT(0)
#define SPI_QPI_MODE			BIT(3)
#define SPI_OPI_MODE			BIT(4)
#define SPI_CS_HOLD			BIT(6)
#define SPI_CS_SETUP			BIT(7)
#define SPI_CK_OUT_EDGE			BIT(9)
#define SPI_FWRITE_DUAL			BIT(12)
#define SPI_FWRITE_QUAD			BIT(13)
#define SPI_FWRITE_OCT			BIT(14)
#define SPI_USR_MOSI			BIT(27)
#define SPI_USR_MISO			BIT(28)
#define SPI_USR_DUMMY			BIT(29)
#define SPI_USR_ADDR			BIT(30)
#define SPI_USR_COMMAND			BIT(31)

#define SPI_USER1_REG		0x14
#define SPI_CS_HOLD_TIME		GENMASK(26, 22)
#define SPI_CS_SETUP_TIME		GENMASK(21, 17)

#define SPI_USER2_REG		0x18

#define SPI_MS_DLEN_REG		0x1c
#define SPI_MS_DATA_BITLEN		GENMASK(17, 0)

#define SPI_MISC_REG		0x20
#define SPI_CS_DIS			GENMASK(5, 0)
#define SPI_CK_DIS			BIT(6)
#define SPI_MASTER_CS_POL		GENMASK(12, 7)
#define SPI_CK_IDLE_EDGE		BIT(29)
#define SPI_CS_KEEP_ACTIVE		BIT(30)

#define SPI_DIN_MODE_REG	0x24
#define SPI_DIN_NUM_REG		0x28
#define SPI_DOUT_MODE_REG	0x2c
#define SPI_DMA_CONF_REG	0x30
#define SPI_DMA_RX_ENA			BIT(27)
#define SPI_DMA_TX_ENA			BIT(28)
#define SPI_RX_AFIFO_RST		BIT(29)
#define SPI_BUF_AFIFO_RST		BIT(30)
#define SPI_DMA_AFIFO_RST		BIT(31)

#define SPI_DMA_INT_ENA_REG	0x34
#define SPI_DMA_INT_CLR_REG	0x38
#define SPI_DMA_INT_RAW_REG	0x3c
#define SPI_DMA_INT_ST_REG	0x40
#define SPI_TRANS_DONE_INT		BIT(12)

#define ESP32_SPI_FIFO_SIZE	64
#define SPI_W0_REG		0x98

#define SPI_SLAVE_REG		0xe0
#define SPI_SLAVE_MODE			BIT(26)
#define SPI_SOFT_RESET			BIT(27)

#define SPI_CLK_GATE_REG	0xe8
#define SPI_CLK_EN			BIT(0)
#define SPI_MST_CLK_ACTIVE		BIT(1)
#define SPI_MST_CLK_SEL			BIT(2)


struct esp32_spi {
	void __iomem *regs;		/* virt. address of control registers */
	struct clk *clk;		/* bus clock */
	u32 speed_hz;			/* bus speed configured in SPI_CLOCK_REG */
	struct completion done;		/* wake-up from interrupt */
};

static void esp32_spi_write(struct esp32_spi *spi, int offset, u32 value)
{
	iowrite32(value, spi->regs + offset);
}

static u32 esp32_spi_read(struct esp32_spi *spi, int offset)
{
	return ioread32(spi->regs + offset);
}

static void esp32_spi_init(struct esp32_spi *spi)
{
	esp32_spi_write(spi, SPI_CLK_GATE_REG,
			SPI_CLK_EN |
			SPI_MST_CLK_ACTIVE |
			SPI_MST_CLK_SEL);
	esp32_spi_write(spi, SPI_SLAVE_REG, SPI_SOFT_RESET);
	esp32_spi_write(spi, SPI_SLAVE_REG, 0);
	esp32_spi_write(spi, SPI_DMA_INT_ENA_REG, 0);
	esp32_spi_write(spi, SPI_DMA_INT_CLR_REG, SPI_TRANS_DONE_INT);
	esp32_spi_write(spi, SPI_DIN_MODE_REG, 0);
	esp32_spi_write(spi, SPI_DOUT_MODE_REG, 0);
	esp32_spi_write(spi, SPI_USER2_REG, 0);
	esp32_spi_write(spi, SPI_DMA_CONF_REG,
			SPI_RX_AFIFO_RST |
			SPI_BUF_AFIFO_RST);
}

static int
esp32_spi_prepare_message(struct spi_controller *host, struct spi_message *msg)
{
	struct esp32_spi *spi = spi_controller_get_devdata(host);
	struct spi_device *device = msg->spi;
	u8 cs = spi_get_chipselect(device, 0);
	u32 cr;

	/* SPI_USER1_REG */
	cr = FIELD_PREP(SPI_CS_HOLD_TIME, 1) | FIELD_PREP(SPI_CS_SETUP_TIME, 1);
	esp32_spi_write(spi, SPI_USER1_REG, cr);

	/* SPI_CTRL_REG */
	cr = 0;

	if (device->mode & SPI_LSB_FIRST)
		cr |= SPI_RD_BIT_ORDER | SPI_WR_BIT_ORDER;

	esp32_spi_write(spi, SPI_CTRL_REG, cr);

	/* SPI_MISC_REG */
	cr = //SPI_CS_KEEP_ACTIVE |
		//FIELD_PREP(SPI_MASTER_CS_POL, BIT(cs)) |
		(SPI_CS_DIS ^ BIT(cs));

	if (device->mode & SPI_CPOL)
		cr |= SPI_CK_IDLE_EDGE;

	esp32_spi_write(spi, SPI_MISC_REG, cr);
	return 0;
}

static void esp32_spi_set_cs(struct spi_device *device, bool is_high)
{
	struct esp32_spi *spi = spi_controller_get_devdata(device->controller);
	u8 cs = spi_get_chipselect(device, 0);
	u32 cr = is_high ? 0 : SPI_CS_KEEP_ACTIVE;

	/* SPI_MISC_REG */
	cr |= (SPI_CS_DIS ^ BIT(cs));

	if (device->mode & SPI_CS_HIGH)
		cr |= FIELD_PREP(SPI_MASTER_CS_POL, BIT(cs));

	if (device->mode & SPI_CPOL)
		cr |= SPI_CK_IDLE_EDGE;

	esp32_spi_write(spi, SPI_MISC_REG, cr);
}

static void esp32_spi_wait_bit(struct esp32_spi *spi, u32 bit)
{
	unsigned long timeout = jiffies + HZ;

	while (esp32_spi_read(spi, SPI_CMD_REG) & bit) {
		if (time_after(jiffies, timeout)) {
			pr_warn("%s: timeout waiting for 0x%08x\n", __func__, bit);
			return;
		}
		cpu_relax();
	}
}

static void esp32_spi_prepare_transfer(struct esp32_spi *spi,
				       struct spi_device *device,
				       struct spi_transfer *t,
				       unsigned int n_words)
{
	u32 cr;
	unsigned int mode;

	/* SPI_CLOCK_REG */
	if (t->speed_hz != spi->speed_hz) {
		u32 spi_clock = clk_get_rate(spi->clk);
		u32 clkcnt = DIV_ROUND_UP(spi_clock, t->speed_hz) - 1;
		u32 clkdiv = 1;

		if (clkcnt && !FIELD_FIT(SPI_CLKCNT_N, clkcnt)) {
			clkdiv = t->speed_hz / FIELD_MAX(SPI_CLKCNT_N);
			clkcnt = DIV_ROUND_UP(spi_clock, t->speed_hz * clkdiv) - 1;
		}
		if (!clkcnt)
			clkcnt = 1;

		cr = FIELD_PREP(SPI_CLKDIV_PRE, clkdiv - 1) |
			FIELD_PREP(SPI_CLKCNT_N, clkcnt) |
			FIELD_PREP(SPI_CLKCNT_L, clkcnt) |
			FIELD_PREP(SPI_CLKCNT_H, (clkcnt + 1) / 2 - 1);

		esp32_spi_write(spi, SPI_CLOCK_REG, cr);
		spi->speed_hz = t->speed_hz;
	}

	/* SPI_MS_DLEN_REG */
	esp32_spi_write(spi, SPI_MS_DLEN_REG,
			FIELD_PREP(SPI_MS_DATA_BITLEN, n_words * 8 - 1));

	/* SPI_USER_REG */
	cr = SPI_CS_HOLD | SPI_CS_SETUP | SPI_DOUTDIN;

	if ((device->mode & (SPI_CPOL | SPI_CPHA)) == SPI_CPOL ||
	    (device->mode & (SPI_CPOL | SPI_CPHA)) == SPI_CPHA)
		cr |= SPI_CK_OUT_EDGE;

	if (t->tx_buf)
		cr |= SPI_USR_MOSI;
	if (t->rx_buf)
		cr |= SPI_USR_MISO;

	mode = max_t(unsigned int, t->rx_nbits, t->tx_nbits);
	WARN_ON(mode != SPI_NBITS_SINGLE);

	esp32_spi_write(spi, SPI_USER_REG, cr);
}

static void esp32_spi_start_transfer(struct esp32_spi *spi)
{
	/* SPI_CMD_REG */
	esp32_spi_write(spi, SPI_CMD_REG, SPI_UPDATE);
	esp32_spi_wait_bit(spi, SPI_UPDATE);
	esp32_spi_write(spi, SPI_CMD_REG, SPI_USR);
}

static irqreturn_t esp32_spi_irq(int irq, void *dev_id)
{
	struct esp32_spi *spi = dev_id;
	u32 status = esp32_spi_read(spi, SPI_DMA_INT_ST_REG);

	if (status) {
		esp32_spi_write(spi, SPI_DMA_INT_ENA_REG, 0);
		complete(&spi->done);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static void esp32_spi_wait(struct esp32_spi *spi)
{
	esp32_spi_wait_bit(spi, SPI_USR);
	if (esp32_spi_read(spi, SPI_DMA_INT_RAW_REG) & SPI_TRANS_DONE_INT) {
		esp32_spi_write(spi, SPI_DMA_INT_CLR_REG, SPI_TRANS_DONE_INT);
	} else {
		WARN_ONCE(1, "wait complete, but no SPI_TRANS_DONE_INT\n");
	}
#if 0
		reinit_completion(&spi->done);
		esp32_spi_write(spi, SPI_DMA_INT_ENA_REG, SPI_TRANS_DONE_INT);
		wait_for_completion(&spi->done);
		esp32_spi_write(spi, SPI_DMA_INT_CLR_REG, SPI_TRANS_DONE_INT);
#endif
}

static int esp32_spi_transfer_one(struct spi_controller *host,
				  struct spi_device *device,
				  struct spi_transfer *t)
{
	struct esp32_spi *spi = spi_controller_get_devdata(host);
	const u8 *tx_ptr = t->tx_buf;
	u8 *rx_ptr = t->rx_buf;
	unsigned int remaining_words = t->len;

	while (remaining_words) {
		unsigned int n_words = min(remaining_words, ESP32_SPI_FIFO_SIZE);
		u32 n32 = n_words & -4;
		u32 r32 = n_words - n32;

		esp32_spi_prepare_transfer(spi, device, t, n_words);

		if (n32)
			memcpy_toio(spi->regs + SPI_W0_REG, tx_ptr, n32);
		if (r32) {
			u32 w = 0;

			memcpy(&w, tx_ptr + n32, r32);
			memcpy_toio(spi->regs + SPI_W0_REG + n32, &w, 4);
		}
		tx_ptr += n_words;

		esp32_spi_start_transfer(spi);
		esp32_spi_wait(spi);

		if (rx_ptr) {
			if (n32)
				memcpy_fromio(rx_ptr, spi->regs + SPI_W0_REG, n32);
			if (r32) {
				u32 w;

				memcpy_fromio(&w, spi->regs + SPI_W0_REG + n32, 4);
				memcpy(rx_ptr + n32, &w, r32);
			}
			rx_ptr += n_words;
		}

		remaining_words -= n_words;
	}
	return 0;
}

static int esp32_spi_probe(struct platform_device *pdev)
{
	struct esp32_spi *spi;
	int ret, irq;
	struct spi_controller *host;
	struct reset_control *rst;
	struct device_node *np = pdev->dev.of_node;
	u32 cs_num;

	host = spi_alloc_host(&pdev->dev, sizeof(struct esp32_spi));
	if (!host) {
		dev_err(&pdev->dev, "out of memory\n");
		return -ENOMEM;
	}

	spi = spi_controller_get_devdata(host);
	init_completion(&spi->done);
	platform_set_drvdata(pdev, host);

	ret = of_property_read_u32(np, "spi-num-chipselects", &cs_num);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to get spi-num-chipselects\n");
		goto put_host;
	}
	host->num_chipselect = cs_num;

	spi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(spi->regs)) {
		ret = PTR_ERR(spi->regs);
		goto put_host;
	}

	spi->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(spi->clk)) {
		dev_err(&pdev->dev, "Unable to find bus clock\n");
		ret = PTR_ERR(spi->clk);
		goto put_host;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto put_host;
	}

	/* Spin up the bus clock before hitting registers */
	ret = clk_prepare_enable(spi->clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable bus clock\n");
		goto put_host;
	}
	rst = devm_reset_control_get(&pdev->dev, "reset");
	if (!IS_ERR(rst))
		reset_control_deassert(rst);

	/* Define our host */
	host->dev.of_node = np;
	host->max_speed_hz = clk_get_rate(spi->clk);
	host->bus_num = pdev->id;
	host->mode_bits = SPI_CPHA | SPI_CPOL |
		SPI_CS_HIGH | SPI_LSB_FIRST |
		SPI_TX_DUAL | SPI_TX_QUAD |
		SPI_RX_DUAL | SPI_RX_QUAD;
	host->bits_per_word_mask = SPI_BPW_MASK(8);
	host->prepare_message = esp32_spi_prepare_message;
	host->set_cs = esp32_spi_set_cs;
	host->transfer_one = esp32_spi_transfer_one;

	pdev->dev.dma_mask = NULL;
	/* Configure the SPI host hardware */
	esp32_spi_init(spi);

	/* Register for SPI Interrupt */
	ret = devm_request_irq(&pdev->dev, irq, esp32_spi_irq, 0,
			       dev_name(&pdev->dev), spi);
	if (ret) {
		dev_err(&pdev->dev, "Unable to bind to interrupt\n");
		goto disable_clk;
	}

	ret = devm_spi_register_controller(&pdev->dev, host);
	if (ret < 0) {
		dev_err(&pdev->dev, "spi_register_host failed\n");
		goto disable_clk;
	}

	return 0;

disable_clk:
	clk_disable_unprepare(spi->clk);
put_host:
	spi_controller_put(host);

	return ret;
}

static void esp32_spi_remove(struct platform_device *pdev)
{
	struct spi_controller *host = platform_get_drvdata(pdev);
	struct esp32_spi *spi = spi_controller_get_devdata(host);

	esp32_spi_write(spi, SPI_DMA_INT_ENA_REG, 0);
	clk_disable_unprepare(spi->clk);
}

static int esp32_spi_suspend(struct device *dev)
{
	struct spi_controller *host = dev_get_drvdata(dev);
	struct esp32_spi *spi = spi_controller_get_devdata(host);
	int ret;

	ret = spi_controller_suspend(host);
	if (ret)
		return ret;

	esp32_spi_write(spi, SPI_DMA_INT_ENA_REG, 0);
	clk_disable_unprepare(spi->clk);

	return ret;
}

static int esp32_spi_resume(struct device *dev)
{
	struct spi_controller *host = dev_get_drvdata(dev);
	struct esp32_spi *spi = spi_controller_get_devdata(host);
	int ret;

	ret = clk_prepare_enable(spi->clk);
	if (ret)
		return ret;
	ret = spi_controller_resume(host);
	if (ret)
		clk_disable_unprepare(spi->clk);

	return ret;
}

static DEFINE_SIMPLE_DEV_PM_OPS(esp32_spi_pm_ops,
				esp32_spi_suspend, esp32_spi_resume);


static const struct of_device_id esp32_spi_of_match[] = {
	{ .compatible = "esp,esp32s3-spi", },
	{}
};
MODULE_DEVICE_TABLE(of, esp32_spi_of_match);

static struct platform_driver esp32_spi_driver = {
	.probe = esp32_spi_probe,
	.remove_new = esp32_spi_remove,
	.driver = {
		.name = ESP32_SPI_DRIVER_NAME,
		.pm = &esp32_spi_pm_ops,
		.of_match_table = esp32_spi_of_match,
	},
};
module_platform_driver(esp32_spi_driver);

MODULE_AUTHOR("Max Filippov <jcmvbkbc@gmail.com>");
MODULE_DESCRIPTION("ESP32 SPI driver");
MODULE_LICENSE("GPL");
