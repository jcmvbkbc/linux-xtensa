/*
 * Ethernet driver for Open Ethernet Controller (www.opencores.org).
 *      Copyright (c) 2002 Simon Srot (simons@opencores.org)
 *      Copyright (c) 2006, 2007, 2009 Tensilica Inc.
 *      Copyright (c) Dan Nicolaescu <dann@tensilica.com>
 *      Copyright (c) Pete Delaney <piet@tensilica.com>
 *      Copyright (c) 2008 Florian Fainelli <florian.fainelli@openpattern.org>
 * 
 * Based on:
 *
 * Ethernet driver for Motorola MPC8xx.
 *      Copyright (c) 1997 Dan Malek (dmalek@jlc.net)
 *
 * mcen302.c: A Linux network driver for Mototrola 68EN302 MCU
 *
 *      Copyright (C) 1999 Aplio S.A. Written by Vadim Lebedev
 *
 * 
 *  The Open Ethernet Controller is just a MAC, it needs to be
 *  combined with a PHY and buffer memory in order to create an
 *  ethernet device. Thus some of the hardware parameters are device
 *  specific. They need to be defined in asm/hardware.h. Example:
 * 
 * The IRQ for the device:
 * #define OETH_IRQ                1
 *
 * The flag to be passed to request_irq:
 * #define OETH_REQUEST_IRQ_FLAG   0
 * 
 * The address where the MAC registers are mapped:
 * #define OETH_BASE_ADDR          0xFD030000
 *
 * The address where the MAC RX/TX buffers are mapped:
 * #define OETH_SRAM_BUFF_BASE     0xFD800000
 * 
 * Sizes for a RX or TX buffer:
 * #define OETH_RX_BUFF_SIZE       2048
 * #define OETH_TX_BUFF_SIZE       2048
 * The number of RX and TX buffers:
 * #define OETH_RXBD_NUM           16
 * #define OETH_TXBD_NUM           16
 * The PHY ID (needed if MII is enabled):
 * #define OETH_PHY_ID             0
 * 
 * Code to perform the device specific initialization (REGS is a
 *  struct oeth_regs*):
 * #define OETH_PLATFORM_SPECIFIC_INIT(REGS)
 * it should at least initialize the device MAC address in
 *  REGS->mac_addr1 and REGS->mac_addr2.
 * 
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/device.h>
#include <linux/skbuff.h>
#include <linux/module.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/platform_device.h>

#include <asm/hardware.h>
#include <asm/processor.h>
#include <platform/system.h>

#include <asm/mxregs.h>		/* REMIND: move to processor.h */

#include "open_eth.h"

MODULE_DESCRIPTION("Opencores ethernet driver.");
MODULE_LICENSE("GPL");

#define DRV_NAME "OpencoresEthernet"
#define DEV_NAME "oeth"


/* Debug helpers. */
#undef OETH_DEBUG_TRANSMIT 
#ifdef OETH_DEBUG_TRANSMIT
#define OEDTX(x) x
#else
#define OEDTX(x)
#endif

#undef OETH_DEBUG_RECEIVE
#ifdef OETH_DEBUG_RECEIVE
#define OEDRX(x) x
#else
#define OEDRX(x)
#endif

#define OETH_REGS_SIZE  0x1000	/* MAC registers + RX and TX descriptors */
#define OETH_BD_BASE    (OETH_BASE_ADDR + 0x400)
#define OETH_TOTAL_BD   128

/* The transmitter timeout FIXME: this needs to be handled */
#define OETH_TX_TIMEOUT	       (2*HZ)

/* The buffer descriptors track the ring buffers. */
struct oeth_private {
	struct oeth_regs *regs;	/* Address of controller registers. */
	struct oeth_bd *rx_bd_base;	/* Address of Rx BDs. */
	struct oeth_bd *tx_bd_base;	/* Address of Tx BDs. */
	u8 tx_next;		/* Next buffer to be sent */
	u8 tx_last;		/* Next buffer to be checked if packet sent */
	u8 tx_full;		/* Buffer ring full indicator */
	u8 rx_cur;		/* Next buffer to be checked if packet received */
	spinlock_t lock;
	spinlock_t rx_lock;
	spinlock_t napi_lock;
	struct napi_struct napi;
	unsigned int reschedule_in_poll;
	struct net_device_stats stats;
	struct mii_if_info mii_if;	/* MII lib hooks/info */
};

static void oeth_tx(struct net_device *dev);
static irqreturn_t oeth_interrupt(int irq, void *dev_id);

#if defined(OETH_DEBUG_RECEIVE) || defined(OETH_DEBUG_TRANSMIT)
static void oeth_print_packet(u32 * add, int len)
{
	int i;

	printk("ipacket: add = %p len = %d\n", add, len);
	for (i = 0; i < len; i++) {
		if (!(i % 16))
			printk("\n");
		else if (!(i % 8))
			printk(" ");
		printk(" %.2x", *(((unsigned char *)add) + i));
	}
	printk("\n");
}
#endif

#if 0
static 
#endif
int oeth_open(struct net_device *dev)
{
	int ret;
	struct oeth_private *cep = netdev_priv(dev);
	struct oeth_regs *regs = cep->regs;

	/*FIXME: just for debugging.... */
	memset((void *)OETH_SRAM_BUFF_BASE, 0, 0x4000);

	napi_enable(&cep->napi);

	/* Install our interrupt handler. */
	ret = request_irq(OETH_IRQ, oeth_interrupt, OETH_REQUEST_IRQ_FLAG,
			  dev->name, (void *)dev);
	if (ret) {
		dev_err(&dev->dev, "%s: request_irq(OETH_IRQ:%d, &oeth_interrupt(), OETH_REQUEST_IRQ_FLAG:%d, dev->name:'%s', dev:%p)" 
				    " Failed for the Opencore ethernet device\n", __func__, OETH_IRQ, OETH_REQUEST_IRQ_FLAG,  dev->name, dev);

		napi_disable(&cep->napi);
		return ret;
	}
	/* Enable the receiver and transmiter. */
	regs->moder |= OETH_MODER_RXEN | OETH_MODER_TXEN;

	/* Start the queue, we are ready to process packets now. */
	netif_start_queue(dev);
	printk("%s:  Ready to process packets now on dev->name:'%s', dev:%p; \n", __func__, dev->name, dev);

#if 0 && defined(CONFIG_DEBUG_KERNEL) && defined(CONFIG_ARCH_HAS_SMP)
	/* For MX case:
	 *	0: NMI
	 *	1: UART
	 *	2: OETH
	 */
	set_er(1<<(OETH_IRQ-2), MIASGSET);
	set_er(0, MIASGSET);
#endif
	return 0;
}

static int oeth_close(struct net_device *dev)
{
	struct oeth_private *cep = netdev_priv(dev);
	struct oeth_regs *regs = cep->regs;
	volatile struct oeth_bd *bdp;
	int i;

	netif_stop_queue(dev);
	napi_disable(&cep->napi);

	spin_lock_irq(&cep->lock);
	/* Disable the receiver and transmiter. */
	regs->moder &= ~(OETH_MODER_RXEN | OETH_MODER_TXEN);

	bdp = cep->rx_bd_base;
	for (i = 0; i < OETH_RXBD_NUM; i++) {
		bdp->len_status &= ~(OETH_TX_BD_STATS | OETH_TX_BD_READY);
		bdp++;
	}

	bdp = cep->tx_bd_base;
	for (i = 0; i < OETH_TXBD_NUM; i++) {
		bdp->len_status &= ~(OETH_RX_BD_STATS | OETH_RX_BD_EMPTY);
		bdp++;
	}

	spin_unlock_irq(&cep->lock);

	return 0;
}

static int oeth_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct oeth_private *cep = netdev_priv(dev);
	volatile struct oeth_bd *bdp;
	unsigned long flags;
	u32 len_status;

	spin_lock_irqsave(&cep->lock, flags);

	if (cep->tx_full) {
		/* All transmit buffers are full.  Bail out. */
		dev_warn(&dev->dev, "tx queue full!.\n");
		netif_stop_queue(dev);
		spin_unlock_irqrestore(&cep->lock, flags);
		return NETDEV_TX_BUSY;
	}

	/* Fill in a Tx ring entry. */
	bdp = cep->tx_bd_base + cep->tx_next;
	len_status = bdp->len_status;

	/* Clear all of the status flags. */
	len_status &= ~OETH_TX_BD_STATS;

	/* If the frame is short, tell MAC to pad it. */
	if (skb->len <= ETH_ZLEN)
		len_status |= OETH_TX_BD_PAD;
	else
		len_status &= ~OETH_TX_BD_PAD;

	OEDTX((printk("TX skb\n")));
	OEDTX((oeth_print_packet((u32 *) skb->data, skb->len)));
	OEDTX((printk("end TX skb print\n")));

	if (skb->len > OETH_TX_BUFF_SIZE) {
		if (net_ratelimit())
			dev_warn(&dev->dev, "tx frame too long!.\n");
		dev_kfree_skb_irq(skb);
		spin_unlock_irqrestore(&cep->lock, flags);
		return NETDEV_TX_OK;
	}

	/* Copy data to TX buffer. */
	memcpy((unsigned char *)bdp->addr, skb->data, skb->len);

	len_status = (len_status & 0x0000ffff) | (skb->len << 16);

	if ((bdp->addr + (len_status >> 16))
	    >= (OETH_SRAM_BUFF_BASE + OETH_TXBD_NUM * OETH_TX_BUFF_SIZE
		+ OETH_RXBD_NUM * OETH_RX_BUFF_SIZE))
		panic("MEMORY OVERWRITE at address: %x !!!\n",
		      (bdp->addr + (len_status >> 16)));

	OEDTX((printk("TX controller buff\n")));
	OEDTX((oeth_print_packet((u32 *) bdp->addr, bdp->len_status >> 16)));
	OEDTX(printk("end TX controller buff print\n"));

	dev_kfree_skb_irq(skb);

	cep->tx_next =
	    (cep->tx_next + 1 == OETH_TXBD_NUM) ? 0 : (cep->tx_next + 1);

	if (cep->tx_next == cep->tx_last) {
		cep->tx_full = 1;
		/* Do not transmit anymore if the TX queue is full. */
		netif_stop_queue(dev);
		cep->stats.tx_compressed++;
	}

	/* Send it on its way.  Tell controller its ready, interrupt when done,
	 * and to put the CRC on the end. */
	len_status |= (OETH_TX_BD_READY | OETH_TX_BD_IRQ | OETH_TX_BD_CRC);
	bdp->len_status = len_status;

	spin_unlock_irqrestore(&cep->lock, flags);

	dev->trans_start = jiffies;
	return 0;
}

/* The interrupt handler. */
static irqreturn_t oeth_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct oeth_private *cep = netdev_priv(dev);
	volatile struct oeth_regs *regs = cep->regs;
	u32 int_events;

	spin_lock(&cep->lock);

	/* Get the interrupt events that caused us to be here. */
	int_events = regs->int_src;
	/* Acknowledge interrupt. */
	regs->int_src = int_events;

	if (int_events & OETH_INT_BUSY)
		cep->stats.rx_compressed++;

	/* If RX or BUSY enable RX polling. On the RX path we need to
	   copy the packet from the internal buffer to an skb, doing
	   that in an interrupt handler increases interrupt
	   latency. */
	if (int_events & (OETH_INT_RXF | OETH_INT_RXE | OETH_INT_BUSY)
	    && !cep->reschedule_in_poll) {
		spin_lock(&cep->napi_lock);
		if (netif_rx_schedule_prep(&cep->napi)) {
			regs->int_mask &= ~(OETH_INT_MASK_RXF
					    | OETH_INT_MASK_RXE);
			__netif_rx_schedule(&cep->napi);
		} else {
			cep->reschedule_in_poll++;
		}
		spin_unlock(&cep->napi_lock);
	}

	/* Handle transmit event in its own function. */
	if (int_events & (OETH_INT_TXB | OETH_INT_TXE)) {
		oeth_tx(dev);
	}
#if 0
	/* Check for receive busy, i.e. packets coming but no place to
	 * put them. */
	if (int_events & OETH_INT_BUSY) {
		if (!(int_events & (OETH_INT_RXF | OETH_INT_RXE))) {
			oeth_rx(dev, 1);
		}
	}
#endif

	spin_unlock(&cep->lock);

	return IRQ_HANDLED;
}

static void oeth_tx(struct net_device *dev)
{
	struct oeth_private *cep = netdev_priv(dev);
	volatile struct oeth_bd *bdp;

	for (;;
	     cep->tx_last =
	     (cep->tx_last + 1 == OETH_TXBD_NUM) ? 0 : (cep->tx_last + 1)) {
		u32 len_status;

		bdp = cep->tx_bd_base + cep->tx_last;
		len_status = bdp->len_status;

		/* If the OETH_TX_BD_READY is set the transmission has
		 * not been done yet! */
		if ((len_status & OETH_TX_BD_READY)
		    || ((cep->tx_last == cep->tx_next) && !cep->tx_full))
			break;

		/* Check status for errors. */
		if (len_status & OETH_TX_BD_LATECOL)
			cep->stats.tx_window_errors++;
		if (len_status & OETH_TX_BD_RETLIM)
			cep->stats.tx_aborted_errors++;
		if (len_status & OETH_TX_BD_UNDERRUN)
			cep->stats.tx_fifo_errors++;
		if (len_status & OETH_TX_BD_CARRIER)
			cep->stats.tx_carrier_errors++;
		if (len_status &
		    (OETH_TX_BD_LATECOL | OETH_TX_BD_RETLIM
		     | OETH_TX_BD_UNDERRUN))
			cep->stats.tx_errors++;

		cep->stats.tx_packets++;
		cep->stats.tx_bytes += len_status >> 16;
		cep->stats.collisions += (len_status & OETH_TX_BD_RETRY) >> 4;

		if (cep->tx_full) {
			cep->tx_full = 0;
			/* We have freed an entry in the TX queue, we can
			 * start transmitting again. */
			if (netif_queue_stopped(dev))
				netif_wake_queue(dev);
		}
	}
}

static unsigned int oeth_rx(struct net_device *dev, int budget)
{
	struct oeth_private *cep = netdev_priv(dev);
	volatile struct oeth_bd *bdp;
	struct sk_buff *skb;
	int received;
	int pkt_len;
	int bad = 0;
	int count = 0;

	for (received = 0; received < budget;
	     cep->rx_cur =
	     (cep->rx_cur + 1 == OETH_RXBD_NUM) ? 0 : (cep->rx_cur + 1)) {
		u32 len_status;
		bdp = cep->rx_bd_base + cep->rx_cur;

		/* First, grab all of the stats for the incoming
		 * packet.  These get messed up if we get called due
		 * to a busy condition. */
		len_status = bdp->len_status;

		if (len_status & OETH_RX_BD_EMPTY)
			break;

		received++;

		/* Check status for errors. */
		if (len_status & (OETH_RX_BD_TOOLONG | OETH_RX_BD_SHORT)) {
			cep->stats.rx_length_errors++;
			printk("Length Error! l=%8x a=%8x s=%4x\n",
			       len_status >> 16, bdp->addr, len_status
			       && 0xFFFF);
			bad = 1;
		}
		if (len_status & OETH_RX_BD_DRIBBLE) {
			cep->stats.rx_frame_errors++;
			printk("Frame Error! l=%8x a=%8x s=%4x\n",
			       len_status >> 16, bdp->addr, len_status
			       && 0xFFFF);
			bad = 1;
		}
		if (len_status & OETH_RX_BD_CRCERR) {
			cep->stats.rx_crc_errors++;
			printk("CRC Error! l=%8x a=%8x s=%4x\n",
			       len_status >> 16, bdp->addr, len_status
			       && 0xFFFF);
			bad = 1;
		}
		if (len_status & OETH_RX_BD_OVERRUN) {
			cep->stats.rx_crc_errors++;
			printk("RECEIVER OVERRUN! l=%8x a=%8x s=%4x\n",
			       len_status >> 16, bdp->addr, len_status
			       && 0xFFFF);
			bad = 1;
		}
		if (len_status & OETH_RX_BD_TOOLONG) {
			cep->stats.rx_crc_errors++;
			printk("RECEIVER TOOLONG! l=%8x a=%8x s=%4x\n",
			       len_status >> 16, bdp->addr, len_status
			       && 0xFFFF);
			bad = 1;
		}
		if (len_status & OETH_RX_BD_MISS) {

		}
		if (len_status & OETH_RX_BD_LATECOL) {
			cep->stats.rx_frame_errors++;
			printk("LateCol Error! l=%8x a=%8x s=%4x\n",
			       len_status >> 16, bdp->addr, len_status
			       && 0xFFFF);
			bad = 1;
		}

		if (bad) {
			bdp->len_status = (len_status & ~OETH_RX_BD_STATS)
			    | OETH_RX_BD_EMPTY;
			continue;
		}

		/* Process the incoming frame. */
		pkt_len = len_status >> 16;

		skb = netdev_alloc_skb(dev, pkt_len + 2);
		skb_reserve(skb, NET_IP_ALIGN);

#if 0	// FIXME
		skb = dev_alloc_skb(pkt_len);	/*netdev_alloc_skb in newer kernels. */
#endif

		if (likely(skb)) {
			skb->dev = dev;
			OEDRX((printk("RX in ETH buf\n")));
			OEDRX((oeth_print_packet((u32 *) bdp->addr, pkt_len)));
// FIXME: do we need an skb_put here? 
			memcpy(skb_put(skb, pkt_len),
			       (unsigned char *)bdp->addr, pkt_len);
			OEDRX((printk("RX in memory\n")));
			OEDRX((oeth_print_packet((u32 *) skb->data, pkt_len)));
			skb->protocol = eth_type_trans(skb, dev);
			dev->last_rx = jiffies;
			cep->stats.rx_packets++;
			cep->stats.rx_bytes += pkt_len;
			netif_receive_skb(skb);
			count++;

			if (((u32) bdp->addr + pkt_len)
			    >= (OETH_SRAM_BUFF_BASE + OETH_TXBD_NUM
				* OETH_TX_BUFF_SIZE
				+ OETH_RXBD_NUM * OETH_RX_BUFF_SIZE))
				panic("address exceeds the buffer!\n");
		} else {
			dev_warn(&dev->dev,"Memory squeeze, dropping packet.\n");
			cep->stats.rx_dropped++;
		}

		bdp->len_status = (len_status & ~OETH_RX_BD_STATS)
		    | OETH_RX_BD_EMPTY;
	}
#if 1
	if (count == 0)
		cep->stats.rx_length_errors++;
	else if (count >= OETH_RXBD_NUM)
		cep->stats.rx_over_errors++;
	else
		cep->stats.rx_crc_errors++;
#endif

	return received;
}


static int oeth_poll(struct napi_struct *napi, int budget)
{
	struct oeth_private *cep = container_of(napi,struct oeth_private, napi);
	struct net_device *dev = cep->mii_if.dev;
	int work_done = 0;

rx_action:
#if 0
	oeth_tx(dev);
#endif
	spin_lock(&cep->rx_lock);
	work_done += oeth_rx(dev, (budget - work_done));
	spin_unlock(&cep->rx_lock);

	if (netif_running(dev) && (work_done < budget)) {
		unsigned long flags;
		int more;

		spin_lock_irqsave(&cep->napi_lock, flags);

		more = cep->reschedule_in_poll;
		if (!more) {
			/* Stop polling and reenable interrupts. */
			__netif_rx_complete(napi);
			cep->regs->int_mask |= 
				(OETH_INT_MASK_RXF | OETH_INT_MASK_RXE);
		} else {
			cep->reschedule_in_poll--;
		}

		spin_unlock_irqrestore(&cep->napi_lock, flags);

		if (more)
			goto rx_action;

	//	cep->stats.rx_fifo_errors++;
	}
	//	cep->stats.rx_missed_errors++;


	return work_done;
}

static struct net_device_stats *oeth_get_stats(struct net_device *dev)
{
	struct oeth_private *cep = netdev_priv(dev);
	return &cep->stats;
}

static int oeth_set_mac_address(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;
	struct oeth_private *cep = netdev_priv(dev);
	volatile struct oeth_regs *regs = cep->regs;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);

	regs->mac_addr1 = addr->sa_data[0] << 8 | addr->sa_data[1];
	regs->mac_addr0 = addr->sa_data[2] << 24
	    | addr->sa_data[3] << 16 | addr->sa_data[4] << 8 | addr->sa_data[5];
	return 0;
}

static int __init oeth_setup(struct net_device *dev, unsigned int base_addr,
			    unsigned int irq);

/*
 * Probe for an Opencores ethernet controller.
 */
#if 0
static 
#endif
int __devinit oeth_probe(struct platform_device *pdev)
{
	struct net_device *dev = alloc_etherdev(sizeof(struct oeth_private));
	int res = 0;

	printk("%s: {\n", __func__);

	if (!dev) {
		res = -ENOMEM;
		goto fail;
	}

	if (check_mem_region(OETH_BASE_ADDR, OETH_REGS_SIZE)) {
		res = -ENOMEM;
		goto fail;
	}

	if ((res = oeth_setup(dev, OETH_BASE_ADDR, OETH_IRQ)) != 0)
		goto fail;

	if (register_netdev(dev)) {
		printk(KERN_WARNING "%s: No card found.\n", __func__);
		res = -ENODEV;
		goto fail;
	}

	platform_set_drvdata(pdev, dev);

	printk("%s: }\n", __func__);
	return 0;

fail:
	printk("%s.fail: \n", __func__);
	free_netdev(dev);
	return res;
}

static void oeth_get_drvinfo(struct net_device *dev,
			     struct ethtool_drvinfo *info)
{
	strcpy(info->driver, DRV_NAME);
	strcpy(info->version, "0.0");
	strcpy(info->bus_info, "none");
}

static int oeth_get_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct oeth_private *cep = netdev_priv(dev);
	spin_lock_irq(&cep->lock);
	mii_ethtool_gset(&cep->mii_if, ecmd);
	spin_unlock_irq(&cep->lock);
	return 0;
}

static int oeth_set_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct oeth_private *cep = netdev_priv(dev);
	int res;
	spin_lock_irq(&cep->lock);
	res = mii_ethtool_sset(&cep->mii_if, ecmd);
	spin_unlock_irq(&cep->lock);
	return res;
}

static int oeth_nway_reset(struct net_device *dev)
{
	struct oeth_private *cep = netdev_priv(dev);
	return mii_nway_restart(&cep->mii_if);
}

static u32 oeth_get_link(struct net_device *dev)
{
	struct oeth_private *cep = netdev_priv(dev);
	return mii_link_ok(&cep->mii_if);
}

static struct ethtool_ops ethtool_ops = {
	.get_drvinfo = oeth_get_drvinfo,
	.get_link = oeth_get_link,
	.get_settings = oeth_get_settings,
	.set_settings = oeth_set_settings,
	.nway_reset = oeth_nway_reset,
	//.get_strings = ethtool_op_net_device_stats_get_strings,
	//.get_stats_count = ethtool_op_net_device_stats_get_stats_count,
	//.get_ethtool_stats = ethtool_op_net_device_get_ethtool_stats,
};

/* MII Data accesses. */
static int mdio_read(struct net_device *dev, int phy_id, int location)
{
	struct oeth_private *cep = netdev_priv(dev);
	struct oeth_regs *regs = cep->regs;
	int read_value, i;
	volatile int v;

	regs->miiaddress = (phy_id & OETH_MIIADDRESS_FIAD)
	    | ((location << 8) & OETH_MIIADDRESS_RGAD);
	regs->miicommand = OETH_MIICOMMAND_RSTAT;

	/* Check if the MII is done. */
	for (i = 10; i >= 0; i--) {
		v = regs->miistatus;
		if (!(v & OETH_MIISTATUS_BUSY)) {
			read_value = regs->miirx_data;
			/* Don't leave miicommand in read status, it
			 * seems to not be reset to 0 after completion. */
			regs->miicommand = 0;
			return read_value;
		}
		msleep(10);
	}
	dev_warn(&dev->dev, "mdio_read timeout\n");
	return -1;
}

static void mdio_write(struct net_device *dev, int phy_id, int location,
		       int value)
{
	struct oeth_private *cep = netdev_priv(dev);
	struct oeth_regs *regs = cep->regs;
	int i;
	volatile int v;
	regs->miiaddress = (phy_id & OETH_MIIADDRESS_FIAD)
	    | ((location << 8) & OETH_MIIADDRESS_RGAD);
	regs->miitx_data = value;
	regs->miicommand = OETH_MIICOMMAND_WCTRLDATA;
	/* Check if the MII is done. */
	for (i = 100; i >= 0; i--) {
		v = regs->miistatus;
		if (!(v & OETH_MIISTATUS_BUSY))
			return;
		msleep(10);
	}
	dev_warn(&dev->dev, "mdio_write timeout\n");
}

/* Initialize the Open Ethernet MAC. */
#if 0
static 
#endif
int oeth_setup(struct net_device *dev, unsigned int base_addr,
		     unsigned int irq)
{
	struct oeth_private *cep = netdev_priv(dev);
	volatile struct oeth_regs *regs;
	volatile struct oeth_bd *tx_bd, *rx_bd;
	int i, phy_id;
	unsigned long mem_addr = OETH_SRAM_BUFF_BASE;

	printk(KERN_INFO "%s: Open Ethernet Core Version 1.0.1\n", __func__);

	/* Initialize the locks. */
	spin_lock_init(&cep->lock);
	spin_lock_init(&cep->rx_lock);
	spin_lock_init(&cep->napi_lock);

	/* Memory regions for the controller registers and buffer space. */
	request_region(base_addr, OETH_REGS_SIZE, DRV_NAME);
	dev->base_addr = base_addr;
	request_region(OETH_SRAM_BUFF_BASE,
		       OETH_TXBD_NUM * OETH_TX_BUFF_SIZE
		       + OETH_RXBD_NUM * OETH_RX_BUFF_SIZE, DRV_NAME);
	/* Get pointer ethernet controller configuration registers. */
	regs = cep->regs = (struct oeth_regs *)(base_addr);

	/* Reset the controller. */
	regs->moder = OETH_MODER_RST;	/* Reset ON */
	regs->moder &= ~OETH_MODER_RST;	/* Reset OFF */

	/* Setting TXBD base to OETH_TXBD_NUM. */
	regs->tx_bd_num = OETH_TXBD_NUM;

	/* Initialize TXBD pointer. */
	cep->tx_bd_base = (struct oeth_bd *)OETH_BD_BASE;
	tx_bd = cep->tx_bd_base;

	/* Initialize RXBD pointer. */
	cep->rx_bd_base = cep->tx_bd_base + OETH_TXBD_NUM;
	rx_bd = cep->rx_bd_base;

	/* Initialize receive/transmit pointers. */
	cep->rx_cur = 0;
	cep->tx_next = 0;
	cep->tx_last = 0;
	cep->tx_full = 0;

	/* Set min (64) and max (1536) packet length. */
	regs->packet_len = (64 << 16) | 1536;

	/* Set IPGT, IPGR1, IPGR2 and COLLCONF registers to the
	 * recommended values. */
	regs->ipgt = 0x00000015;
	regs->ipgr1 = 0x0000000c;
	regs->ipgr2 = 0x00000012;
	regs->collconf = 0x000f003f;

	/* Set control module mode. Do not deal with PAUSE frames for now. */
	regs->ctrlmoder = 0;

#ifdef OETH_PHY_ID
	phy_id = OETH_PHY_ID;
#else
	/* Identify phy address. */
	for (phy_id = 0; phy_id < 0x1f; phy_id++) {
		int id1, id2;

		id1 = mdio_read(dev, phy_id, MII_PHYSID1);
		if (id1 < 0 || id1 == 0xffff)
			continue;
		id2 = mdio_read(dev, phy_id, MII_PHYSID2);
		if (id2 < 0 || id2 == 0xffff)
			continue;
		dev_info(&dev->dev, "%s: Found id1:%04x, id2:%04x at phy_id:%d.\n",
			 __func__, id1, id2, phy_id);
		break;
	}
#endif
		       
	/* Initialize MII. */
	cep->mii_if.dev = dev;
	cep->mii_if.mdio_read = mdio_read;
	cep->mii_if.mdio_write = mdio_write;
	cep->mii_if.phy_id = phy_id;
	cep->mii_if.phy_id_mask = OETH_MIIADDRESS_FIAD;
	cep->mii_if.reg_num_mask = 0x1f;
	SET_ETHTOOL_OPS(dev, &ethtool_ops);

	/* Platform specific initialization. This function should set
	   at least set regs->mac_addr1 and regs->mac_addr2. */
/* 	OETH_PLATFORM_SPECIFIC_INIT(regs); */
	do {
		/* Set the clock divider to 2 (50MHz / 2) */
		regs->miimoder = (OETH_MIIMODER_CLKDIV & 0x2);

		/* Reset the PHY. */
		{
			int j, res;
			mdio_write(dev, cep->mii_if.phy_id, MII_BMCR,
				   BMCR_RESET);
			/* Wait until the reset is complete. */
			for (j = 1000; j >= 0; j--) {
				res =
				    mdio_read(dev, cep->mii_if.phy_id,
					      MII_BMCR);
				if (!(res & BMCR_RESET))
					break;
			}
			if (res & BMCR_RESET) {
				dev_warn (&dev->dev,
					"PHY reset timeout BMCR:0x%08x!\n",res);
				return -1;
			}
		}

		/* Tell the PHY to turn on the activity LED. */
		mdio_write(dev, 0, MII_TPISTATUS, 0xce);

		{
			/* Test code to setup the networking parameters according to
			   the DIP switches. */
			u32 net_settings =
			    ((*(u32 *) DIP_SWITCHES_ADDR) & 0xc0) >> 6;
			if (net_settings) {
				/* Disable autonegotiation in order to disable full duplex. */
				u32 cword;
				if (net_settings & 1)
					/* half duplex requested */
					cword = 0x0000;
				else
					cword = BMCR_FULLDPLX;

				if (net_settings & 2)
					/* 10 Mbit requested */
					cword |= 0x0000;
				else
					cword |= BMCR_SPEED100;

				mdio_write(dev, 0, MII_BMCR, cword);
			}
		}

		/* Initialize the MAC address. */
		regs->mac_addr1 = OETH_MACADDR0 << 8 | OETH_MACADDR1;
		regs->mac_addr0 = OETH_MACADDR2 << 24 | OETH_MACADDR3 << 16
		    | OETH_MACADDR4 << 8 | ((*(u32 *) DIP_SWITCHES_ADDR) &
					    0x3f);

	} while (0);

	/* Initialize TXBDs. */
	for (i = 0; i < OETH_TXBD_NUM; i++) {
		tx_bd[i].len_status =
		    OETH_TX_BD_PAD | OETH_TX_BD_CRC | OETH_TX_BD_IRQ;
		tx_bd[i].addr = mem_addr;
		mem_addr += OETH_TX_BUFF_SIZE;
	}
	tx_bd[OETH_TXBD_NUM - 1].len_status |= OETH_TX_BD_WRAP;

	/* Initialize RXBDs. */
	for (i = 0; i < OETH_RXBD_NUM; i++) {
		rx_bd[i].len_status = OETH_RX_BD_EMPTY | OETH_RX_BD_IRQ;
		rx_bd[i].addr = mem_addr;
		mem_addr += OETH_RX_BUFF_SIZE;
	}
	rx_bd[OETH_RXBD_NUM - 1].len_status |= OETH_RX_BD_WRAP;

	/* Set default ethernet MAC address. */
	dev->dev_addr[0] = (regs->mac_addr1 >> 8) & 0xff;
	dev->dev_addr[1] = regs->mac_addr1 & 0xff;
	dev->dev_addr[2] = (regs->mac_addr0 >> 24) & 0xff;
	dev->dev_addr[3] = (regs->mac_addr0 >> 16) & 0xff;
	dev->dev_addr[4] = (regs->mac_addr0 >> 8) & 0xff;
	dev->dev_addr[5] = regs->mac_addr0 & 0xff;

	/* Clear all pending interrupts. */
	regs->int_src = 0xffffffff;

	/* Promisc, IFG, CRCEn  */
	regs->moder |= OETH_MODER_PAD | OETH_MODER_IFG | OETH_MODER_CRCEN;

	/* Enable interrupt sources. */
	regs->int_mask = OETH_INT_MASK_TXB | OETH_INT_MASK_TXE
	    | OETH_INT_MASK_RXF | OETH_INT_MASK_RXE
	    | OETH_INT_MASK_TXC | OETH_INT_MASK_RXC | OETH_INT_MASK_BUSY;

	/* The Open Ethernet specific entries in the device structure. */
	dev->open	     = oeth_open;
	dev->hard_start_xmit = oeth_start_xmit;
	dev->stop	     = oeth_close;
	dev->get_stats	     = oeth_get_stats;
	dev->set_mac_address = oeth_set_mac_address;

	netif_napi_add(dev, &cep->napi, oeth_poll, OETH_RXBD_NUM * 2 - 4);
	dev->irq	     = irq;
	/* FIXME: Something needs to be done with dev->tx_timeout and
	   dev->watchdog timeout here. */

	dev_info(&dev->dev,"Hardware MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
			 dev->dev_addr[0], dev->dev_addr[1],
			 dev->dev_addr[2], dev->dev_addr[3],
			 dev->dev_addr[4], dev->dev_addr[5]);

	return 0;
}

static int __devexit oeth_remove (struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);

	unregister_netdev(dev);
	release_region(dev->base_addr, OETH_REGS_SIZE);
	release_region(OETH_SRAM_BUFF_BASE,
		       OETH_TXBD_NUM * OETH_TX_BUFF_SIZE
		       + OETH_RXBD_NUM * OETH_RX_BUFF_SIZE);
	free_netdev(dev);

	return 0;
}


static struct platform_driver oeth_driver = {
	.driver.name = "oeth",
	.probe = oeth_probe,
};

static struct platform_device *oeth_device;

static int __init oeth_init(void)
{
	int res = 0;

	res = platform_driver_register(&oeth_driver);
	if (res)
		goto out;
	
	oeth_device = platform_device_alloc(DEV_NAME, 0);
	if (oeth_device == NULL) {
		res = -ENOMEM;
		goto out_unregister;
	}

	if (platform_device_add(oeth_device)) {
		platform_device_put(oeth_device);
		oeth_device = NULL;
	}

	return res;

out_unregister:
	platform_driver_unregister(&oeth_driver);
out:
	return res;
}

static void __exit oeth_exit(void)
{
	platform_driver_unregister(&oeth_driver);

	if (oeth_device) {
		platform_device_unregister(oeth_device);
		oeth_device = NULL;
	}
}

module_init(oeth_init);
module_exit(oeth_exit);

