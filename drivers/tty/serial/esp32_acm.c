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
#include <asm/serial.h>

#define DRIVER_NAME	"esp32s3-acm"
#define DEV_NAME	"ttyACM"
#define UART_NR		4

#define ESP32S3_ACM_TX_FIFO_SIZE	64

#define USB_SERIAL_JTAG_EP1_REG		0x00
#define USB_SERIAL_JTAG_EP1_CONF_REG	0x04
#define USB_SERIAL_JTAG_WR_DONE_MASK				0x00000001
#define USB_SERIAL_JTAG_SERIAL_IN_EP_DATA_FREE_MASK		0x00000002
#define USB_SERIAL_JTAG_SERIAL_OUT_EP_DATA_AVAIL_MASK		0x00000008
#define USB_SERIAL_JTAG_INT_ST_REG	0x0c
#define USB_SERIAL_JTAG_SERIAL_OUT_RECV_PKT_INT_ST_MASK		0x00000004
#define USB_SERIAL_JTAG_SERIAL_IN_EMPTY_INT_ST_MASK		0x00000008
#define USB_SERIAL_JTAG_INT_ENA_REG	0x10
#define USB_SERIAL_JTAG_SERIAL_OUT_RECV_PKT_INT_ENA_MASK	0x00000004
#define USB_SERIAL_JTAG_SERIAL_IN_EMPTY_INT_ENA_MASK		0x00000008
#define USB_SERIAL_JTAG_INT_CLR_REG	0x14
#define USB_SERIAL_JTAG_IN_EP1_ST_REG	0x2c
#define USB_SERIAL_JTAG_IN_EP1_WR_ADDR_MASK			0x000001fc
#define USB_SERIAL_JTAG_IN_EP1_WR_ADDR_SHIFT			2
#define USB_SERIAL_JTAG_OUT_EP1_ST_REG	0x3c
#define USB_SERIAL_JTAG_OUT_EP1_REC_DATA_CNT_MASK		0x007f0000
#define USB_SERIAL_JTAG_OUT_EP1_REC_DATA_CNT_SHIFT		16

static const struct of_device_id esp32s3_acm_dt_ids[] = {
	{
		.compatible = "esp,esp32s3-acm",
	}, { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, esp32s3_acm_dt_ids);

static struct uart_port *esp32s3_acm_ports[UART_NR];

#ifdef DEBUG
void dbg_echo(const char *s)
{
	volatile void __iomem *base = (volatile void __iomem *)0x60038000;

	while (!(readl(base + USB_SERIAL_JTAG_EP1_CONF_REG) &
		 USB_SERIAL_JTAG_SERIAL_IN_EP_DATA_FREE_MASK))
		;

	while (*s) {
		if (*s == '\n')
			writel('\r', base + USB_SERIAL_JTAG_EP1_REG);
		writel(*s, base + USB_SERIAL_JTAG_EP1_REG);
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

static void esp32s3_acm_write(struct uart_port *port, unsigned long reg, u32 v)
{
	writel(v, port->membase + reg);
}

static u32 esp32s3_acm_read(struct uart_port *port, unsigned long reg)
{
	return readl(port->membase + reg);
}

static u32 esp32s3_acm_tx_fifo_free(struct uart_port *port)
{
	return esp32s3_acm_read(port, USB_SERIAL_JTAG_EP1_CONF_REG) &
		USB_SERIAL_JTAG_SERIAL_IN_EP_DATA_FREE_MASK;
}

static u32 esp32s3_acm_tx_fifo_cnt(struct uart_port *port)
{
	u32 status = esp32s3_acm_read(port, USB_SERIAL_JTAG_IN_EP1_ST_REG);

	return (status & USB_SERIAL_JTAG_IN_EP1_WR_ADDR_MASK) >>
		USB_SERIAL_JTAG_IN_EP1_WR_ADDR_SHIFT;
}

static u32 esp32s3_acm_rx_fifo_cnt(struct uart_port *port)
{
	u32 status = esp32s3_acm_read(port, USB_SERIAL_JTAG_OUT_EP1_ST_REG);

	return (status & USB_SERIAL_JTAG_OUT_EP1_REC_DATA_CNT_MASK) >>
		USB_SERIAL_JTAG_OUT_EP1_REC_DATA_CNT_SHIFT;
}

/* return TIOCSER_TEMT when transmitter is not busy */
static unsigned int esp32s3_acm_tx_empty(struct uart_port *port)
{
	return esp32s3_acm_tx_fifo_cnt(port) == 0 ? TIOCSER_TEMT : 0;
}

static void esp32s3_acm_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

static unsigned int esp32s3_acm_get_mctrl(struct uart_port *port)
{
	return TIOCM_CAR;
}

static void esp32s3_acm_stop_tx(struct uart_port *port)
{
	u32 int_ena;

	int_ena = esp32s3_acm_read(port, USB_SERIAL_JTAG_INT_ENA_REG);
	esp32s3_acm_write(port, USB_SERIAL_JTAG_INT_ENA_REG,
			  int_ena & ~USB_SERIAL_JTAG_SERIAL_IN_EMPTY_INT_ENA_MASK);
}

static void esp32s3_acm_rxint(struct uart_port *port)
{
	struct tty_port *tty_port = &port->state->port;
	u32 rx_fifo_cnt = esp32s3_acm_rx_fifo_cnt(port);
	unsigned long flags;
	u32 i;

	if (!rx_fifo_cnt)
		return;

	spin_lock_irqsave(&port->lock, flags);

	for (i = 0; i < rx_fifo_cnt; ++i) {
		u32 rx = esp32s3_acm_read(port, USB_SERIAL_JTAG_EP1_REG);

		++port->icount.rx;
		tty_insert_flip_char(tty_port, rx, TTY_NORMAL);
	}
	spin_unlock_irqrestore(&port->lock, flags);

	tty_flip_buffer_push(tty_port);
}

static void esp32s3_acm_push(struct uart_port *port)
{
	if (esp32s3_acm_tx_fifo_free(port))
		esp32s3_acm_write(port, USB_SERIAL_JTAG_EP1_CONF_REG,
				  USB_SERIAL_JTAG_WR_DONE_MASK);
}

static void esp32s3_acm_put_char(struct uart_port *port, unsigned char c)
{
	esp32s3_acm_write(port, USB_SERIAL_JTAG_EP1_REG, c);
}

static void esp32s3_acm_put_char_sync(struct uart_port *port, unsigned char c)
{
	while (!esp32s3_acm_tx_fifo_free(port))
		cpu_relax();
	esp32s3_acm_put_char(port, c);
	esp32s3_acm_push(port);
}

static void esp32s3_acm_transmit_buffer(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;
	u32 tx_fifo_used = esp32s3_acm_tx_fifo_cnt(port);

	if (esp32s3_acm_tx_fifo_free(port)) {
		while (!uart_circ_empty(xmit) && tx_fifo_used < ESP32S3_ACM_TX_FIFO_SIZE) {
			esp32s3_acm_put_char(port, xmit->buf[xmit->tail]);
			xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
			port->icount.tx++;
			++tx_fifo_used;
		}
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit)) {
		esp32s3_acm_stop_tx(port);
	} else {
		u32 int_ena;

		int_ena = esp32s3_acm_read(port, USB_SERIAL_JTAG_INT_ENA_REG);
		esp32s3_acm_write(port, USB_SERIAL_JTAG_INT_ENA_REG,
				  int_ena | USB_SERIAL_JTAG_SERIAL_IN_EMPTY_INT_ENA_MASK);
	}

	if (tx_fifo_used > 0 && tx_fifo_used < ESP32S3_ACM_TX_FIFO_SIZE)
		esp32s3_acm_write(port, USB_SERIAL_JTAG_EP1_CONF_REG,
				  USB_SERIAL_JTAG_WR_DONE_MASK);
}

static void esp32s3_acm_txint(struct uart_port *port)
{
	esp32s3_acm_transmit_buffer(port);
}

static irqreturn_t esp32s3_acm_int(int irq, void *dev_id)
{
	struct uart_port *port = dev_id;
	u32 status;

	status = esp32s3_acm_read(port, USB_SERIAL_JTAG_INT_ST_REG);
	esp32s3_acm_write(port, USB_SERIAL_JTAG_INT_CLR_REG, status);

	if (status & USB_SERIAL_JTAG_SERIAL_OUT_RECV_PKT_INT_ST_MASK)
		esp32s3_acm_rxint(port);
	if (status & USB_SERIAL_JTAG_SERIAL_IN_EMPTY_INT_ST_MASK)
		esp32s3_acm_txint(port);

	return IRQ_HANDLED;
}

static void esp32s3_acm_start_tx(struct uart_port *port)
{
	esp32s3_acm_transmit_buffer(port);
}

static void esp32s3_acm_stop_rx(struct uart_port *port)
{
	u32 int_ena;

	int_ena = esp32s3_acm_read(port, USB_SERIAL_JTAG_INT_ENA_REG);
	esp32s3_acm_write(port, USB_SERIAL_JTAG_INT_ENA_REG,
			  int_ena & ~USB_SERIAL_JTAG_SERIAL_OUT_RECV_PKT_INT_ENA_MASK);
}

static void esp32s3_acm_break_ctl(struct uart_port *port, int break_state)
{
}

static int esp32s3_acm_startup(struct uart_port *port)
{
	int ret = 0;

	esp32s3_acm_write(port, USB_SERIAL_JTAG_INT_ENA_REG,
			  USB_SERIAL_JTAG_SERIAL_OUT_RECV_PKT_INT_ENA_MASK);
	ret = devm_request_irq(port->dev, port->irq, esp32s3_acm_int, 0,
			       DRIVER_NAME, port);
	return ret;
}

static void esp32s3_acm_shutdown(struct uart_port *port)
{
	esp32s3_acm_write(port, USB_SERIAL_JTAG_INT_ENA_REG, 0);
	devm_free_irq(port->dev, port->irq, port);
}

static void esp32s3_acm_set_termios(struct uart_port *port,
				    struct ktermios *termios,
				    const struct ktermios *old)
{
}

static const char *esp32s3_acm_type(struct uart_port *port)
{
	return "ESP32S3 ACM";
}

static void esp32s3_acm_release_port(struct uart_port *port)
{
}

static int esp32s3_acm_request_port(struct uart_port *port)
{
	return 0;
}

/* configure/auto-configure the port */
static void esp32s3_acm_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_ESP32ACM;
}

#ifdef CONFIG_CONSOLE_POLL
static void esp32s3_acm_poll_put_char(struct uart_port *port, unsigned char c)
{
	esp32s3_acm_put_char_sync(port, c);
}

static int esp32s3_acm_poll_get_char(struct uart_port *port)
{
	if (esp32s3_acm_rx_fifo_cnt(port))
		return esp32s3_acm_read(port, USB_SERIAL_JTAG_EP1_REG);
	else
		return NO_POLL_CHAR;

}
#endif

static const struct uart_ops esp32s3_acm_pops = {
	.tx_empty	= esp32s3_acm_tx_empty,
	.set_mctrl	= esp32s3_acm_set_mctrl,
	.get_mctrl	= esp32s3_acm_get_mctrl,
	.stop_tx	= esp32s3_acm_stop_tx,
	.start_tx	= esp32s3_acm_start_tx,
	.stop_rx	= esp32s3_acm_stop_rx,
	.break_ctl	= esp32s3_acm_break_ctl,
	.startup	= esp32s3_acm_startup,
	.shutdown	= esp32s3_acm_shutdown,
	.set_termios	= esp32s3_acm_set_termios,
	.type		= esp32s3_acm_type,
	.release_port	= esp32s3_acm_release_port,
	.request_port	= esp32s3_acm_request_port,
	.config_port	= esp32s3_acm_config_port,
#ifdef CONFIG_CONSOLE_POLL
	.poll_put_char	= esp32s3_acm_poll_put_char,
	.poll_get_char	= esp32s3_acm_poll_get_char,
#endif
};

static void esp32s3_acm_console_putchar(struct uart_port *port, unsigned char c)
{
	esp32s3_acm_put_char_sync(port, c);
}

static void esp32s3_acm_string_write(struct uart_port *port, const char *s,
				     unsigned int count)
{
	uart_console_write(port, s, count, esp32s3_acm_console_putchar);
}

static void
esp32s3_acm_console_write(struct console *co, const char *s, unsigned int count)
{
	struct uart_port *port = esp32s3_acm_ports[co->index];
	unsigned long flags;
	int locked = 1;

	if (port->sysrq)
		locked = 0;
	else if (oops_in_progress)
		locked = spin_trylock_irqsave(&port->lock, flags);
	else
		spin_lock_irqsave(&port->lock, flags);

	esp32s3_acm_string_write(port, s, count);

	if (locked)
		spin_unlock_irqrestore(&port->lock, flags);
}

static int __init esp32s3_acm_console_setup(struct console *co, char *options)
{
	if (!esp32s3_acm_ports[co->index])
		return -ENODEV;
	return 0;
}

static int esp32s3_acm_console_exit(struct console *co)
{
	return 0;
}

static struct uart_driver esp32s3_acm_reg;
static struct console esp32s3_acm_console = {
	.name		= DEV_NAME,
	.write		= esp32s3_acm_console_write,
	.device		= uart_console_device,
	.setup		= esp32s3_acm_console_setup,
	.exit		= esp32s3_acm_console_exit,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &esp32s3_acm_reg,
};

static void esp32s3_acm_earlycon_putchar(struct uart_port *port, unsigned char c)
{
	esp32s3_acm_put_char_sync(port, c);
}

static void esp32s3_acm_earlycon_write(struct console *con, const char *s,
				      unsigned int n)
{
	struct earlycon_device *dev = con->data;

	uart_console_write(&dev->port, s, n, esp32s3_acm_earlycon_putchar);
}

#ifdef CONFIG_CONSOLE_POLL
static int esp32s3_acm_earlycon_read(struct console *con, char *s, unsigned int n)
{
	struct earlycon_device *dev = con->data;
	int num_read = 0;

	while (num_read < n) {
		int c = esp32s3_acm_poll_get_char(&dev->port);

		if (c == NO_POLL_CHAR)
			break;
		s[num_read++] = c;
	}
	return num_read;
}
#endif

static int __init esp32s3_acm_early_console_setup(struct earlycon_device *device,
						   const char *options)
{
	if (!device->port.membase)
		return -ENODEV;

	device->con->write = esp32s3_acm_earlycon_write;
#ifdef CONFIG_CONSOLE_POLL
	device->con->read = esp32s3_acm_earlycon_read;
#endif
	return 0;
}

OF_EARLYCON_DECLARE(esp32s3acm, "esp,esp32s3-acm",
		    esp32s3_acm_early_console_setup);

static struct uart_driver esp32s3_acm_reg = {
	.owner		= THIS_MODULE,
	.driver_name	= DRIVER_NAME,
	.dev_name	= DEV_NAME,
	.nr		= ARRAY_SIZE(esp32s3_acm_ports),
	.cons		= &esp32s3_acm_console,
};

static int esp32s3_acm_probe(struct platform_device *pdev)
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
	port->type = PORT_ESP32ACM;
	port->iotype = UPIO_MEM;
	port->irq = platform_get_irq(pdev, 0);
	port->ops = &esp32s3_acm_pops;
	port->flags = UPF_BOOT_AUTOCONF;
	port->has_sysrq = 1;
	port->fifosize = ESP32S3_ACM_TX_FIFO_SIZE;

	esp32s3_acm_ports[port->line] = port;

	platform_set_drvdata(pdev, port);

	ret = uart_add_one_port(&esp32s3_acm_reg, port);
	return ret;
}

static int esp32s3_acm_remove(struct platform_device *pdev)
{
	struct uart_port *port = platform_get_drvdata(pdev);

	uart_remove_one_port(&esp32s3_acm_reg, port);
	return 0;
}


static struct platform_driver esp32s3_acm_driver = {
	.probe		= esp32s3_acm_probe,
	.remove		= esp32s3_acm_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.of_match_table	= esp32s3_acm_dt_ids,
	},
};

static int __init esp32s3_acm_init(void)
{
	int ret;

	ret = uart_register_driver(&esp32s3_acm_reg);
	if (ret)
		return ret;

	ret = platform_driver_register(&esp32s3_acm_driver);
	if (ret)
		uart_unregister_driver(&esp32s3_acm_reg);

	return ret;
}

static void __exit esp32s3_acm_exit(void)
{
	platform_driver_unregister(&esp32s3_acm_driver);
	uart_unregister_driver(&esp32s3_acm_reg);
}

module_init(esp32s3_acm_init);
module_exit(esp32s3_acm_exit);

MODULE_DESCRIPTION("Espressif ESP32S3 USB ACM driver.");
MODULE_LICENSE("GPL v2");
