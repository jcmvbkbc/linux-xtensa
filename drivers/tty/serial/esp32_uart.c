// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/console.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/serial_core.h>
#include <linux/slab.h>
#include <linux/tty_flip.h>
#include <linux/delay.h>

#define DRIVER_NAME	"esp32-uart"
#define DEV_NAME	"ttyS"
#define UART_NR		3

#define ESP32_UART_TX_FIFO_SIZE	127
#define ESP32_UART_RX_FIFO_SIZE	127

#define UART_FIFO_REG		0x00
#define UART_INT_RAW_REG	0x04
#define UART_INT_ST_REG		0x08
#define UART_INT_ENA_REG	0x0c
#define UART_INT_CLR_REG	0x10
#define UART_RXFIFO_FULL_INT_MASK	0x00000001
#define UART_TXFIFO_EMPTY_INT_MASK	0x00000002
#define UART_BRK_DET_INT_MASK		0x00000080
#define UART_STATUS_REG		0x1c
#define UART_RXFIFO_CNT_MASK		0x000000ff
#define UART_RXFIFO_CNT_SHIFT		0
#define UART_TXFIFO_CNT_MASK		0x00ff0000
#define UART_TXFIFO_CNT_SHIFT		16
#define UART_ST_UTX_OUT_MASK		0x0f000000
#define UART_ST_UTX_OUT_IDLE		0x00000000
#define UART_ST_UTX_OUT_SHIFT		24
#define UART_CONF0_REG		0x20
#define UART_PARITY_MASK		0x00000001
#define UART_PARITY_EN_MASK		0x00000002
#define UART_BIT_NUM_MASK		0x0000000c
#define UART_BIT_NUM_5			0x00000000
#define UART_BIT_NUM_6			0x00000004
#define UART_BIT_NUM_7			0x00000008
#define UART_BIT_NUM_8			0x0000000c
#define UART_STOP_BIT_NUM_MASK		0x00000030
#define UART_STOP_BIT_NUM_1		0x00000010
#define UART_STOP_BIT_NUM_2		0x00000030
#define UART_TICK_REF_ALWAYS_ON_MASK	0x08000000
#define UART_CONF1_REG		0x24
#define UART_RXFIFO_FULL_THRHD_MASK	0x0000007f
#define UART_RXFIFO_FULL_THRHD_SHIFT	0
#define UART_TXFIFO_EMPTY_THRHD_MASK	0x00007f00
#define UART_TXFIFO_EMPTY_THRHD_SHIFT	8


static const struct of_device_id esp32_uart_dt_ids[] = {
	{
		.compatible = "esp,esp32-uart",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, esp32_uart_dt_ids);

static struct uart_port *esp32_uart_ports[UART_NR];

#ifdef DEBUG
void dbg_echo(const char *s)
{
	volatile void __iomem *base = (volatile void __iomem *)0x3ff40000;

	while ((readl(base + UART_STATUS_REG) & UART_TXFIFO_CNT_MASK) != 0)
		;

	while (*s) {
		if (*s == '\n')
			writel('\r', base + UART_FIFO_REG);
		writel(*s, base + UART_FIFO_REG);
		++s;
	}
}

void dbg_printf(const char *fmt, ...)
{
	va_list ap;
	char buf[256];

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	dbg_echo(buf);
}
#endif

static void esp32_uart_write(struct uart_port *port, unsigned long reg, u32 v)
{
	writel(v, port->membase + reg);
}

static u32 esp32_uart_read(struct uart_port *port, unsigned long reg)
{
	return readl(port->membase + reg);
}

static u32 esp32_uart_tx_fifo_cnt(struct uart_port *port)
{
	return (esp32_uart_read(port, UART_STATUS_REG) &
		UART_TXFIFO_CNT_MASK) >> UART_TXFIFO_CNT_SHIFT;
}

static u32 esp32_uart_rx_fifo_cnt(struct uart_port *port)
{
	return (esp32_uart_read(port, UART_STATUS_REG) &
		UART_RXFIFO_CNT_MASK) >> UART_RXFIFO_CNT_SHIFT;
}

/* return TIOCSER_TEMT when transmitter is not busy */
static unsigned int esp32_uart_tx_empty(struct uart_port *port)
{
	u32 status = esp32_uart_read(port, UART_STATUS_REG) &
		(UART_TXFIFO_CNT_MASK | UART_ST_UTX_OUT_MASK);

	pr_debug("%s: %08x\n", __func__, status);
	return status == UART_ST_UTX_OUT_IDLE ? TIOCSER_TEMT : 0;
}

static void esp32_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

static unsigned int esp32_uart_get_mctrl(struct uart_port *port)
{
	return 0;
}

static void esp32_uart_stop_tx(struct uart_port *port)
{
	u32 int_ena;

	int_ena = esp32_uart_read(port, UART_INT_ENA_REG);
	esp32_uart_write(port, UART_INT_ENA_REG,
			 int_ena & ~UART_TXFIFO_EMPTY_INT_MASK);
}

static void esp32_uart_rxint(struct uart_port *port)
{
	struct tty_port *tty_port = &port->state->port;
	u32 rx_fifo_cnt = esp32_uart_rx_fifo_cnt(port);
	unsigned long flags;
	u32 i;

	if (!rx_fifo_cnt)
		return;

	spin_lock_irqsave(&port->lock, flags);

	for (i = 0; i < rx_fifo_cnt; ++i) {
		u32 rx = esp32_uart_read(port, UART_FIFO_REG);
		u32 brk = 0;

		++port->icount.rx;

		if (!rx) {
			brk = esp32_uart_read(port, UART_INT_ST_REG) &
				UART_BRK_DET_INT_MASK;
		}
		if (brk) {
			esp32_uart_write(port, UART_INT_CLR_REG, brk);
			++port->icount.brk;
			uart_handle_break(port);
		} else {
			if (uart_handle_sysrq_char(port, (unsigned char)rx))
				continue;
			tty_insert_flip_char(tty_port, rx, TTY_NORMAL);
		}
	}
	spin_unlock_irqrestore(&port->lock, flags);

	tty_flip_buffer_push(tty_port);
}

static void esp32_uart_put_char(struct uart_port *port, unsigned char c)
{
	esp32_uart_write(port, UART_FIFO_REG, c);
}

static void esp32_uart_put_char_sync(struct uart_port *port, unsigned char c)
{
	while (esp32_uart_tx_fifo_cnt(port) >= ESP32_UART_TX_FIFO_SIZE)
		cpu_relax();
	esp32_uart_put_char(port, c);
}

static void esp32_uart_transmit_buffer(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;
	u32 tx_fifo_used = esp32_uart_tx_fifo_cnt(port);

	while (!uart_circ_empty(xmit) && tx_fifo_used < ESP32_UART_TX_FIFO_SIZE) {
		esp32_uart_put_char(port, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		++tx_fifo_used;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit)) {
		esp32_uart_stop_tx(port);
	} else {
		u32 int_ena;

		int_ena = esp32_uart_read(port, UART_INT_ENA_REG);
		esp32_uart_write(port, UART_INT_ENA_REG,
				 int_ena | UART_TXFIFO_EMPTY_INT_MASK);
	}
}

static void esp32_uart_txint(struct uart_port *port)
{
	esp32_uart_transmit_buffer(port);
}

static irqreturn_t esp32_uart_int(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	u32 status;

	status = esp32_uart_read(port, UART_INT_ST_REG);

	if (status & (UART_RXFIFO_FULL_INT_MASK | UART_BRK_DET_INT_MASK))
		esp32_uart_rxint(port);
	if (status & UART_TXFIFO_EMPTY_INT_MASK)
		esp32_uart_txint(port);

	esp32_uart_write(port, UART_INT_CLR_REG, status);
	return IRQ_HANDLED;
}

static void esp32_uart_start_tx(struct uart_port *port)
{
	esp32_uart_transmit_buffer(port);
}

static void esp32_uart_stop_rx(struct uart_port *port)
{
	u32 int_ena;

	int_ena = esp32_uart_read(port, UART_INT_ENA_REG);
	esp32_uart_write(port, UART_INT_ENA_REG,
			 int_ena & ~UART_RXFIFO_FULL_INT_MASK);
}

static void esp32_uart_break_ctl(struct uart_port *port, int break_state)
{
}

static int esp32_uart_startup(struct uart_port *port)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	esp32_uart_write(port, UART_CONF1_REG,
			 (1 << UART_RXFIFO_FULL_THRHD_SHIFT) |
			 (1 << UART_TXFIFO_EMPTY_THRHD_SHIFT));
	esp32_uart_write(port, UART_INT_CLR_REG,
			 UART_RXFIFO_FULL_INT_MASK |
			 UART_BRK_DET_INT_MASK);
	esp32_uart_write(port, UART_INT_ENA_REG,
			 UART_RXFIFO_FULL_INT_MASK |
			 UART_BRK_DET_INT_MASK);
	spin_unlock_irqrestore(&port->lock, flags);

	ret = devm_request_irq(port->dev, port->irq, esp32_uart_int, 0,
			       DRIVER_NAME, port);

	pr_debug("%s, request_irq: %d, conf1 = %08x, int_st = %08x, status = %08x\n",
		 __func__, ret,
		 esp32_uart_read(port, UART_CONF1_REG),
		 esp32_uart_read(port, UART_INT_ST_REG),
		 esp32_uart_read(port, UART_STATUS_REG));
	return ret;
}

static void esp32_uart_shutdown(struct uart_port *port)
{
	esp32_uart_write(port, UART_INT_ENA_REG, 0);
	devm_free_irq(port->dev, port->irq, port);
}

static void esp32_uart_set_termios(struct uart_port *port,
				   struct ktermios *termios,
				   const struct ktermios *old)
{
	u32 conf0 = UART_TICK_REF_ALWAYS_ON_MASK;

	if (termios->c_cflag & PARENB) {
		conf0 |= UART_PARITY_EN_MASK;
		if (termios->c_cflag & PARODD)
			conf0 |= UART_PARITY_MASK;
	}

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		conf0 |= UART_BIT_NUM_5;
		break;
	case CS6:
		conf0 |= UART_BIT_NUM_6;
		break;
	case CS7:
		conf0 |= UART_BIT_NUM_7;
		break;
	case CS8:
		conf0 |= UART_BIT_NUM_8;
		break;
	}

	if (termios->c_cflag & CSTOPB)
		conf0 |= UART_STOP_BIT_NUM_2;
	else
		conf0 |= UART_STOP_BIT_NUM_1;

	esp32_uart_write(port, UART_CONF0_REG, conf0);
}

static const char *esp32_uart_type(struct uart_port *port)
{
	return "ESP32 UART";
}

static void esp32_uart_release_port(struct uart_port *port)
{
}

static int esp32_uart_request_port(struct uart_port *port)
{
	return 0;
}

/* configure/auto-configure the port */
static void esp32_uart_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_ESP32UART;
}

#ifdef CONFIG_CONSOLE_POLL
static int esp32_uart_poll_init(struct uart_port *port)
{
	return 0;
}

static void esp32_uart_poll_put_char(struct uart_port *port, unsigned char c)
{
	esp32_uart_put_char_sync(port, c);
}

static int esp32_uart_poll_get_char(struct uart_port *port)
{
	if (esp32_uart_rx_fifo_cnt(port))
		return esp32_uart_read(port, UART_FIFO_REG);
	else
		return NO_POLL_CHAR;

}
#endif

static const struct uart_ops esp32_uart_pops = {
	.tx_empty	= esp32_uart_tx_empty,
	.set_mctrl	= esp32_uart_set_mctrl,
	.get_mctrl	= esp32_uart_get_mctrl,
	.stop_tx	= esp32_uart_stop_tx,
	.start_tx	= esp32_uart_start_tx,
	.stop_rx	= esp32_uart_stop_rx,
	.break_ctl	= esp32_uart_break_ctl,
	.startup	= esp32_uart_startup,
	.shutdown	= esp32_uart_shutdown,
	.set_termios	= esp32_uart_set_termios,
	.type		= esp32_uart_type,
	.release_port	= esp32_uart_release_port,
	.request_port	= esp32_uart_request_port,
	.config_port	= esp32_uart_config_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_init	= esp32_uart_poll_init,
	.poll_put_char	= esp32_uart_poll_put_char,
	.poll_get_char	= esp32_uart_poll_get_char,
#endif
};

static void esp32_uart_console_putchar(struct uart_port *port, unsigned char c)
{
	esp32_uart_put_char_sync(port, c);
}

static void esp32_uart_string_write(struct uart_port *port, const char *s,
				    unsigned int count)
{
	uart_console_write(port, s, count, esp32_uart_console_putchar);
}

static void
esp32_uart_console_write(struct console *co, const char *s, unsigned int count)
{
	struct uart_port *port = esp32_uart_ports[co->index];
	unsigned long flags;
	int locked = 1;

	if (port->sysrq)
		locked = 0;
	else if (oops_in_progress)
		locked = spin_trylock_irqsave(&port->lock, flags);
	else
		spin_lock_irqsave(&port->lock, flags);

	esp32_uart_string_write(port, s, count);

	if (locked)
		spin_unlock_irqrestore(&port->lock, flags);
}

static int __init esp32_uart_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
	int ret;
	/*
	 * check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index == -1 || co->index >= ARRAY_SIZE(esp32_uart_ports))
		co->index = 0;

	port = esp32_uart_ports[co->index];
	if (!port)
		return -ENODEV;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	ret = uart_set_options(port, co, baud, parity, bits, flow);

	return ret;
}

static struct uart_driver esp32_uart_reg;
static struct console esp32_uart_console = {
	.name		= DEV_NAME,
	.write		= esp32_uart_console_write,
	.device		= uart_console_device,
	.setup		= esp32_uart_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &esp32_uart_reg,
};

static void esp32_uart_earlycon_putchar(struct uart_port *port, unsigned char c)
{
	esp32_uart_put_char_sync(port, c);
}

static void esp32_uart_earlycon_write(struct console *con, const char *s,
				      unsigned int n)
{
	struct earlycon_device *dev = con->data;

	uart_console_write(&dev->port, s, n, esp32_uart_earlycon_putchar);
}

#ifdef CONFIG_CONSOLE_POLL
static int esp32_uart_earlycon_read(struct console *con, char *s, unsigned int n)
{
	struct earlycon_device *dev = con->data;
	int num_read = 0;

	while (num_read < n) {
		int c = esp32_uart_poll_get_char(&dev->port);

		if (c == NO_POLL_CHAR)
			break;
		s[num_read++] = c;
	}
	return num_read;
}
#endif

static int __init esp32_uart_early_console_setup(struct earlycon_device *device,
						 const char *options)
{
	if (!device->port.membase)
		return -ENODEV;

	device->con->write = esp32_uart_earlycon_write;
#ifdef CONFIG_CONSOLE_POLL
	device->con->read = esp32_uart_earlycon_read;
#endif

	return 0;
}

OF_EARLYCON_DECLARE(esp32uart, "esp,esp32-uart",
		    esp32_uart_early_console_setup);

static struct uart_driver esp32_uart_reg = {
	.owner		= THIS_MODULE,
	.driver_name	= DRIVER_NAME,
	.dev_name	= DEV_NAME,
	.nr		= ARRAY_SIZE(esp32_uart_ports),
	.cons		= &esp32_uart_console,
};

static int esp32_uart_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct uart_port *port;
	struct resource *res;
	int ret;

	port = devm_kzalloc(&pdev->dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	ret = of_alias_get_id(np, "serial");
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get alias id, errno %d\n", ret);
		return ret;
	}
	if (ret >= UART_NR) {
		dev_err(&pdev->dev, "driver limited to %d serial ports\n",
			UART_NR);
		return -ENOMEM;
	}

	port->line = ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		return -ENODEV;
	}

	port->mapbase = res->start;
	port->membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(port->membase)) {
		return PTR_ERR(port->membase);
	}

	port->dev = &pdev->dev;
	port->type = PORT_ESP32UART;
	port->iotype = UPIO_MEM;
	port->irq = platform_get_irq(pdev, 0);
	port->ops = &esp32_uart_pops;
	port->flags = UPF_BOOT_AUTOCONF;
	port->has_sysrq = 1;
	port->fifosize = ESP32_UART_TX_FIFO_SIZE;

	esp32_uart_ports[port->line] = port;

	platform_set_drvdata(pdev, port);

	ret = uart_add_one_port(&esp32_uart_reg, port);
	if (ret) {
		return ret;
	}

	return 0;
}

static int esp32_uart_remove(struct platform_device *pdev)
{
	struct uart_port *port = platform_get_drvdata(pdev);

	uart_remove_one_port(&esp32_uart_reg, port);

	return 0;
}


static struct platform_driver esp32_uart_driver = {
	.probe		= esp32_uart_probe,
	.remove		= esp32_uart_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.of_match_table	= esp32_uart_dt_ids,
		//.pm	= &esp32_uart_pm_ops,
	},
};

static int __init esp32_uart_init(void)
{
	int ret;

	ret = uart_register_driver(&esp32_uart_reg);
	if (ret)
		return ret;

	ret = platform_driver_register(&esp32_uart_driver);
	if (ret)
		uart_unregister_driver(&esp32_uart_reg);

	return ret;
}

static void __exit esp32_uart_exit(void)
{
	platform_driver_unregister(&esp32_uart_driver);
	uart_unregister_driver(&esp32_uart_reg);
}

module_init(esp32_uart_init);
module_exit(esp32_uart_exit);

MODULE_DESCRIPTION("Espressif ESP32 UART driver.");
MODULE_LICENSE("GPL v2");
