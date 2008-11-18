#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>

//#include <asm/processor/sbios/sx-errors.h>
//#include <asm/processor/sbios/sx-ppi.h>
//#include <asm/processor/sbios/sx-interrupts.h>
//#include <asm/processor/sbios/sx-misc.h>
//#include <asm/processor/sbios/sx-eerom.h>

typedef unsigned int sx_uint32;
typedef unsigned char sx_uint8;

#define __STRETCH_S5000__ 1

#include <asm/variant/s5000/s5000.h>
#include <asm/variant/s5000/global_reg.h>
#include <asm/variant/s5000/gmac.h>
#include <asm/variant/s5000/dmac.h>
#include <asm/variant/s5000/intcntl.h>
#include <asm/variant/interrupt.h>


#define S5_NUM_GMACS		1
#define S5_NUM_RX_DESC		32
#define S5_NUM_TX_DESC		32
#define S5_MAX_RX_BUFF_SZ	1536
#define S5_RX_THRESHOLD		3
#define S5_TX_THRESHOLD		3

#define S5_DMA_DESC_SZ							\
	((S5_NUM_RX_DESC + S5_NUM_TX_DESC) * sizeof(sx_dmac_descriptor))
#define S5_DMAC_TIMEOUT_MS	10 // FIXME


/* The TBI module in the MAC is accessed via the MII interface for some
 *  * bizarre reason.  This is the PHY number assigned to that device
 *   */
#define SXP1_GMAC_TBI_PHYNUM 0x1E



static char s5_gmac_string[] = "s5-gmac";

/* The number of GMACs is fixed */

struct gmac_dev_info {
	struct platform_device *device;
	unsigned long base_addr;
	unsigned char eth_addr[6];
	int dmac_index;
} s5_gmac_dev_info[S5_NUM_GMACS];

struct s5_gmac_priv {

	/* Chip addresses */

	volatile sx_global_registers*		sx_gregs;
	volatile sx_dmac_channel_registers	*rx_chan;
	volatile sx_dmac_channel_registers	*tx_chan;

	struct gmac_dev_info		*gmac_info;
	sx_gmac_dev			*gmac;

	int				phy_addr;
	
	/* RX */
	sx_dmac_descriptor		*rx_ring;
	dma_addr_t			rx_ring_laddr;
	struct sk_buff*			rx_skb[S5_NUM_RX_DESC];
	int				rx_pend;
	//int				rx_done;

	/* TX */
	spinlock_t			tx_lock;
	sx_dmac_descriptor		*tx_ring;
	dma_addr_t			tx_ring_laddr;
	struct sk_buff*			tx_skb[S5_NUM_TX_DESC];
	int				tx_len[S5_NUM_TX_DESC];
	int				tx_pend;
	int				tx_done;

	struct device			*device;
	struct net_device_stats		stats;

};


#define S5_TX_DESC_AVAIL(p)						\
	(((p)->tx_done - (p)->tx_pend - 1) & (S5_NUM_TX_DESC - 1))
#define S5_TX_DESC_NEXT(d)						\
	(((d) + 1) & (S5_NUM_TX_DESC - 1))

#define S5_RX_DESC_NEXT(d)						\
	(((d) + 1) & (S5_NUM_RX_DESC - 1))
int dbg_rx=0;

static irqreturn_t s5_gmac_int_handler (int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct s5_gmac_priv *priv = netdev_priv(dev);
	sx_dmac_registers *dmac = (sx_dmac_registers*)SXP1_DMAC_BASE;
	uint32_t source = dmac->glob_reg.terminal_count_int_status;
	int index = priv->gmac_info->dmac_index;
	irqreturn_t ret = IRQ_NONE;

	/* TX interrupt */

	if (source & (1 << index)) {

		int idx = priv->tx_done;
		int cnt;

		/* Clear interrupt */
		dmac->glob_reg.terminal_count_int_status = 1 << index;
		cnt = priv->tx_chan->terminal_count_status;

		/* Free sent skbs */

		while (cnt--) {

			sx_dmac_descriptor *desc = &priv->tx_ring[idx];

			dev_kfree_skb_irq(priv->tx_skb[idx]);
			dma_unmap_single(priv->device, desc->src_start_addr,
					 priv->tx_len[idx], DMA_TO_DEVICE);
			priv->tx_skb[idx] = NULL;
			desc->control = 0;
			desc->src_start_addr = 0;

			idx = S5_TX_DESC_NEXT(idx);
		}

		priv->tx_done = idx;
		ret = IRQ_HANDLED;
	}

	/* RX interrupt */

	if (source & (1 << (index + 4))) {

		int idx = priv->rx_pend;
		int cnt;

		/* Clear interrupt */

		dmac->glob_reg.terminal_count_int_status = 1 << (index + 4);
		cnt = priv->rx_chan->terminal_count_status;

		while (cnt--) {
			sx_dmac_descriptor *desc = &priv->rx_ring[idx];
			dma_addr_t desc_laddr = priv->rx_ring_laddr + sizeof(sx_dmac_descriptor) * idx;
			struct sk_buff *new_skb, *old_skb;
			dma_addr_t new_laddr, old_laddr;
			int old_len;

			/* Retrieve descriptor information */

			old_len = desc->control & 0xfffff;
			old_laddr = desc->dst_start_addr;

			/* Allocate new SKB buffer */

			new_skb = dev_alloc_skb(S5_MAX_RX_BUFF_SZ + 2);

			if (new_skb == NULL) {
				printk(KERN_ERR "%s: out of memory, "
				       "dropping packet!\n", dev->name);
				break;
			}

			skb_reserve(new_skb, 2);
			new_skb->dev = dev;

			new_laddr = dma_map_single(priv->device,
					skb_put(new_skb, S5_MAX_RX_BUFF_SZ),
					S5_MAX_RX_BUFF_SZ, DMA_FROM_DEVICE);
			if (new_laddr == 0) {
				dev_kfree_skb_irq(new_skb);
				printk(KERN_ERR "%s: failed to map RX buffer, "
				       "dropping packet!\n", dev->name);
				break;
			}

			/* Pass the old one up the stack */

			dma_unmap_single(priv->device, old_laddr,
					 S5_MAX_RX_BUFF_SZ,DMA_FROM_DEVICE);

			old_skb = priv->rx_skb[idx];
if (dbg_rx)
{
	int *data = (int*)(((long)old_skb->data) & ~0x3);
	unsigned char *c = (unsigned char*)(old_skb->data);
	printk("%02x:%02x:%02x:%02x:%02x:%02x -> %02x:%02x:%02x:%02x:%02x:%02x %02x%02x\n", c[6], c[7], c[8], c[9], c[10], c[11], c[0], c[1], c[2], c[3], c[4], c[5], c[12], c[13]);
	printk("%02x %02x %d %s (%d) %d.%d.%d.%d -> %d.%d.%d.%d\n",
			c[14],c[15],(c[16]<<8) + c[17],
			c[23] == 1 ? "icmp" : c[23] == 6 ? "tcp" : c[23] == 17 ?"udp" : "??", c[23],
			c[26],c[27],c[28],c[29],
			c[30],c[31],c[32],c[33]);
	printk("%08x %08x %08x %08x\n",
			data[8], data[9], data[10], data[11]);

}

			skb_trim(old_skb, old_len);
			old_skb->protocol = eth_type_trans(old_skb, dev);
			
			netif_rx(old_skb);

			dev->last_rx = jiffies;

			/* Insert new SKB */

			priv->rx_skb[idx] = new_skb;
			desc->control = 0;
			desc->dst_start_addr = new_laddr;

			/* Update tail pointers */

			idx = S5_RX_DESC_NEXT(idx);
			priv->rx_chan->stored_head_ptr = 
				virt_to_bus(&priv->rx_ring[idx]);
			priv->rx_chan->pending_list_tail = virt_to_bus(desc_laddr);
		}

		priv->rx_pend = idx;
		ret = IRQ_HANDLED;
	}

	return ret;
}
int dbg_tx = 0;
static int s5_gmac_send(struct sk_buff* skb, struct net_device *dev)
{
	struct s5_gmac_priv *priv = netdev_priv(dev);
	int length = skb->len;
	dma_addr_t laddr;

#if 0
	if (!spin_lock(&priv->tx_lock)) {
		printk("dev locked!\n");
		return NETDEV_TX_LOCKED;
	}
#endif

	/* Ensure that the packet has a certain minimum size. */
	if (length < ETH_ZLEN) {
		if (skb_padto(skb, ETH_ZLEN)) {
			printk("skb == NULL??\n");
			//spin_unlock(&priv->tx_lock);
			return 0;
		}
		length = ETH_ZLEN;
	}
if (dbg_tx)
{
	int *data = (int*)(((long)skb->data) & ~0x3);
	unsigned char *c = (unsigned char*)(skb->data);
	printk("-- tx --\n");
	printk("%02x:%02x:%02x:%02x:%02x:%02x -> %02x:%02x:%02x:%02x:%02x:%02x %02x%02x\n", c[6], c[7], c[8], c[9], c[10], c[11], c[0], c[1], c[2], c[3], c[4], c[5], c[12], c[13]);
	printk("%02x %02x %d %s (%d) %d.%d.%d.%d -> %d.%d.%d.%d\n",
			c[14],c[15],(c[16]<<8) + c[17],
			c[23] == 1 ? "icmp" : c[23] == 6 ? "tcp" : c[23] == 17 ?"udp" : "??", c[23],
			c[26],c[27],c[28],c[29],
			c[30],c[31],c[32],c[33]);
	printk("%08x %08x %08x %08x\n",
			data[8], data[9], data[10], data[11]);

}

	/* Map the packet data into the logical DMA address space */

	laddr = dma_map_single(priv->device, skb->data, length, DMA_TO_DEVICE);

	if (!laddr) {
		printk(KERN_ERR "%s: failed to map tx DMA buffer.\n",dev->name);
		dev_kfree_skb(skb);
		//spin_unlock(&priv->tx_lock);
		return 1;
	}

	/* ... */

	if (S5_TX_DESC_AVAIL(priv)) {
		int num_pend = priv->tx_pend;
		sx_dmac_descriptor *desc = &priv->tx_ring[num_pend];
		dma_addr_t desc_laddr = priv->tx_ring_laddr + sizeof (sx_dmac_descriptor) * num_pend;

		/* Setup descriptor */


		desc->src_start_addr = laddr;
		desc->control =
			SX_DMACDESC_MK_CONTROL(length,
					      SX_GMAC_PFIX_READ_BITS(0,1,1,0), 
					      1, 1);
		/* Update pointers */

		priv->tx_skb[priv->tx_pend] = skb;
		priv->tx_len[priv->tx_pend] = length;

		/* Add descriptor to pending list */

		priv->tx_chan->pending_list_tail = virt_to_bus(desc_laddr);
		priv->tx_pend = S5_TX_DESC_NEXT(priv->tx_pend);

	} else {
		printk("out of send desc\n");
		// FIXME need to drop it or return something else than 0???
	}
	//spin_unlock(&priv->tx_lock);
	return 0;
}

#define S5_RX_MAX_PKT_LEN	2048	// FIXME

static int mii_read(struct s5_gmac_priv *priv, int phy, int reg, uint32_t *rval)
{
	sx_gmac_dev *gmac = (sx_gmac_dev*)priv->gmac_info->base_addr;

	gmac->mac.mii_config = 7;	/* Set clock divider */
	gmac->mac.mii_addr = (phy << 8) | reg;
	gmac->mac.mii_command = 1;
	gmac->mac.mii_command = 0;

	/* Wait for the value */

	if (gmac->mac.mii_indicators != 0)
		mdelay(1);

	if (gmac->mac.mii_indicators != 0)
		return -EAGAIN;

	if (rval)
		*rval = gmac->mac.mii_status;

	return 0;
}

static int mii_write(struct s5_gmac_priv *priv, int phy, int reg, uint32_t val)
{
	sx_gmac_dev *gmac = (sx_gmac_dev*)priv->gmac_info->base_addr;

	gmac->mac.mii_config = 7;	/* Set clock divider */
	gmac->mac.mii_addr = (phy << 8) | reg;
	gmac->mac.mii_cntl = val;

	/* Wait for the value */

	if (gmac->mac.mii_indicators != 0)
		mdelay(1);

	return gmac->mac.mii_indicators != 0 ?  -EAGAIN : 0;
}	


static int s5_mii_link_state(struct s5_gmac_priv *priv)
{
	uint32_t status_reg;
	int ret;

	ret = mii_read(priv, priv->phy_addr, 1, &status_reg);

	printk("link up? %d\n", status_reg & 4);

	return ret == 0 ? (status_reg & 4) : ret;
}

static int s5_mii_link_speed(struct s5_gmac_priv *priv)
{
	uint32_t rval;
	uint32_t ctrl_reg;
	uint32_t stat_reg;
	int cnt;
	const char *speed[] = { "10", "100", "1000", "illegal" };

	/* initialize phy */

	mii_write(priv, priv->phy_addr, 4, 0x1e1);

	/* Restart Autoneg. */

	mii_read(priv, priv->phy_addr, 0, &ctrl_reg);
	mii_write(priv, priv->phy_addr, 0, ctrl_reg | 0x0200);

	for (cnt = 0; cnt < 50; cnt++) {
		mii_read(priv, priv->phy_addr, 1, &stat_reg);
		if (stat_reg & 4)
			break;
		mdelay(100);
	}

	/* Assume S56DB board, which uses the Marvell 88E1020 PHY:
	 * bit 10	-> link status:		0 -> link down, 1 -> link good,
	 * bit 13	-> duplex status:	0 -> half,	1 -> full,
	 * bit 14/15	-> 00 :   10 mbps,	01:  100 mbps,	10: 1000 mbps
	 */

	mii_read(priv, priv->phy_addr, 0x11, &rval);

	printk("rval %x\n", rval);
	printk("link %s: %s %cD\n", 
			(rval & (1 << 10)) ? "up" : "down",
			speed[(rval >> 14) & 3],
			(rval & (1 << 13)) ? 'F' : 'H');


	
	return 0;
}

static void s5_gmac_init(struct s5_gmac_priv *priv)
{
	int loopback = 0;
	//int mtu = 0;
	int mode = 0; //FIXME SX_PPI_MODE_GMII;
	//int mode = SX_PPI_MODE_GMII;
	uint32_t rval;
	sx_gmac_dev *gmac = (sx_gmac_dev*)priv->gmac_info->base_addr;

	/* Set TX/RX burst size to 16 bytes, enable stats. */

	gmac->host.port_block_control = 
		SX_GMAC_PORT_BLOCK_CNTL(1, /* enable tx */
					1, /* enable rx */
					0, /* mac bypass */
					0, /* mac bypass fifo width */
					loopback, /* mac loopback enable */
					0, /* mac bypass gen crc enable */
					0, /* mac bypass TBI enable */
					0, /* mac bypass PL-3 mode enable */
					SX_GMAC_BURST_SIZE_128, /* tx */
					SX_GMAC_BURST_SIZE_128, /* rx */
					1, /* enable stat block */
					0, /* stat block 0 */
					0); /* stat block 1 */
	gmac->mac.config_1 = 
		SX_GMAC_CONFIG_1(	0, /* turn off reset */
				 	0, /* no simulation reset */
				 	0, /* no rx mac control reset */
				 	0, /* no tx mac control reset */
				 	0, /* no rx mac functional reset */
				 	0, /* no tx mac functional reset */
				 	loopback,
				 	0, /* turn on rx flow control */
				 	0, /* turn on tx flow control */
				 	1, /* rx enable */
				 	1); /* tx enable */

	/* MII or GMII mode */

	gmac->mac.max_frame_length = 1536; // mtu ? mtu : S5_RX_MAX_PKT_LEN;

	if (mode == 1) { // FIXME mode GMAC, ...
		/* GMII: byte_mode, preamble = 7, len_check, FD */
		gmac->mac.config_2 = 0x00007211;
	} else {
		/* nibble_mode, preamble = 7, len_check, FD */
		gmac->mac.config_2 = 0x00007111;
		gmac->fifo.config_5 &= ~0x00080000;
	}

	/* Set MII into non-TBI mode */

	mii_write(priv, SXP1_GMAC_TBI_PHYNUM, 0x11, 0x30);

	gmac->mac.station_address_0 = 0;
	gmac->mac.station_address_1 = 0;

	/* enable alcatel fifo block */

	rval = gmac->fifo.config_0;
	gmac->fifo.config_0 = (rval & ~0x1f) | 0x1f00;
	gmac->fifo.config_3 = (128 << 16) | 128;

	/* Accept any frame (if error or not) */

	rval = 0x3ffff | (8 << 20) | (1 << 18);
	rval = rval & ~ (1 | (1 << 10) | (1 << 16) | (1 << 4) | (1 << 7) | (1 << 11) | (1 << 12) | (1 << 13));
	gmac->fifo.config_5 = rval;
	rval = (~(rval & 0x3ffff)) & (~(1 << 7));
	gmac->fifo.config_4 = rval;

	/* Half-duplex... */


	/* Set the MAC address */

	gmac->mac.station_address_0 =
		(priv->gmac_info->eth_addr[0] << 24) |
		(priv->gmac_info->eth_addr[1] << 16) |
		(priv->gmac_info->eth_addr[2] << 8)  |
		(priv->gmac_info->eth_addr[3] << 0);
	gmac->mac.station_address_1 = 
		(priv->gmac_info->eth_addr[4] << 24) |
		(priv->gmac_info->eth_addr[5] << 16);

	/* Set ethernet address filter */



	gmac->host.dest_addr0_lo		= 0xffffffff;
	gmac->host.dest_addr0_hi		= 0x0000ffff;
	gmac->host.dest_addr_mask0_lo	= 0xffffffff;
	gmac->host.dest_addr_mask0_hi	= 0x0000ffff;

	gmac->host.dest_addr1_hi =
		(priv->gmac_info->eth_addr[0] << 8) |
		(priv->gmac_info->eth_addr[1] << 0);
	gmac->host.dest_addr1_lo =
		(priv->gmac_info->eth_addr[2] << 24) |
		(priv->gmac_info->eth_addr[3] << 16) |
		(priv->gmac_info->eth_addr[4] << 8)  |
		(priv->gmac_info->eth_addr[5] << 0);

	gmac->host.dest_addr_mask1_lo	= 0xffffffff;
	gmac->host.dest_addr_mask1_hi	= 0x0000ffff;

	gmac->host.dest_addr2_lo	= 0;
	gmac->host.dest_addr2_hi	= 0x1000;
	gmac->host.dest_addr_mask2_lo	= 0;
	gmac->host.dest_addr_mask2_hi	= 0x1000;

	gmac->host.dest_addr3_lo	= 0;
	gmac->host.dest_addr3_hi	= 0;
	gmac->host.dest_addr_mask3_lo	= 0;
	gmac->host.dest_addr_mask3_hi	= 0;

	/* Generate FCS */
	gmac->mac.config_2 |= 1 << 1;

}

static int s5_gmac_open(struct net_device *dev)
{
	struct s5_gmac_priv *priv = netdev_priv(dev);
	sx_gmac_dev *gmac = (sx_gmac_dev*)priv->gmac_info->base_addr;
	sx_global_registers *greg;
	//sx_dmac_registers *dmac;
	dma_addr_t laddr;
	dma_addr_t desc_laddr;
	int ret = 0;
	int i;
	unsigned int timeout;
	volatile int tmp;

	priv->rx_ring = dma_alloc_coherent(priv->device, S5_DMA_DESC_SZ,
					   &priv->rx_ring_laddr, GFP_KERNEL);
printk("rxring %p\n",priv->rx_ring);
	/* Initialize transmit descriptors */

	priv->tx_ring = priv->rx_ring + S5_NUM_RX_DESC;
	priv->tx_ring_laddr = priv->rx_ring_laddr + S5_NUM_RX_DESC * sizeof(sx_dmac_descriptor);

	for (i = 0; i < S5_NUM_TX_DESC; i++) {
		priv->tx_ring[i].control = 0;
		priv->tx_ring[i].src_start_addr = 0;
		priv->tx_ring[i].dst_start_addr =
			(uint32_t)&gmac->host.transmit_packet_data[0];
		desc_laddr = priv->tx_ring_laddr + sizeof (sx_dmac_descriptor) * ((i + 1) & (S5_NUM_TX_DESC - 1));
		priv->tx_ring[i].next = virt_to_bus(desc_laddr);
	}

	timeout = 1;

	/* Allocate receive buffers */

	for (i = 0; i < S5_NUM_RX_DESC; i++) {

		struct sk_buff *skb = dev_alloc_skb(S5_MAX_RX_BUFF_SZ + 2);

		if (skb == NULL) {
			printk(KERN_ERR "%s: couldn't allocate receive "
					"buffers\n", dev->name);
			ret = -ENOMEM;
			goto out;
		}

		skb_reserve(skb, 2);

		/* Map buffer to dma-space. */

		laddr = dma_map_single(priv->device,
				skb_put(skb, S5_MAX_RX_BUFF_SZ),
				S5_MAX_RX_BUFF_SZ, DMA_FROM_DEVICE);

		if (laddr == 0) {
			dev_kfree_skb(priv->rx_skb[i]);
			printk(KERN_ERR "%s: couldn't map rx DMA buffers\n",
			       dev->name);
			ret = -ENOMEM;
			goto out;
		}

		/* Assign the buffer to the GMAC */

		priv->rx_ring[i].control = 0;
		priv->rx_ring[i].src_start_addr =
			(uint32_t)&gmac->host.receive_packet_data[0];
		priv->rx_ring[i].dst_start_addr = laddr;

		desc_laddr = priv->rx_ring_laddr + sizeof (sx_dmac_descriptor) * ((i + 1) & (S5_NUM_RX_DESC - 1));
		priv->rx_ring[i].next = virt_to_bus(desc_laddr);

		skb->dev = dev;
		priv->rx_skb[i] = skb;
		//priv->rx_laddr[i] = laddr;
	}

	// FIXME
	{
		/* Clear any pending interrupts */

		sx_intcntl_reg *int_ctrl = (sx_intcntl_reg *)SXP1_INTCNTL_BASE;
		volatile sx_dmac_registers *dmac;
		int i = priv->gmac_info->dmac_index;

		dmac = (sx_dmac_registers*)SXP1_DMAC_BASE;
		dmac->glob_reg.terminal_count_int_status = (1<<i) | (1<<(i+4));

		/* Route interrupts */

		int_ctrl->config_dmac_tc[i] = 
			SX_INTC_MK_CONFIG(1, S5000_INTNUM_DMAC0);
		int_ctrl->config_dmac_tc[i+4] = 
			SX_INTC_MK_CONFIG(1, S5000_INTNUM_DMAC0);
	}

	/* Request interrupt. */

	if ((request_irq(S5000_INTNUM_DMAC0, s5_gmac_int_handler, 
			 IRQF_SHARED, "gmac", dev))) {
		printk(KERN_ERR "\n%s: unable to get IRQ %d.\n",
		       dev->name, dev->irq);
		// FIXME exit gracefully
		return -EAGAIN;
	}

	/* Take MAC block out of reset */

	greg = (sx_global_registers*) SXP1_GLOBAL_REG_BASE;
	// FIXME?? this is not what's in the include file
	greg->block_enable |= (1 << 2);	

	// FIXME check for mac...

	/* Start the RX DMA channel */

	priv->rx_chan->src_stride = 0;
	priv->rx_chan->src_skip = 0;
	priv->rx_chan->src_wrap_addr = 0;
	priv->rx_chan->dst_stride = 0;
	priv->rx_chan->dst_skip = 0;
	priv->rx_chan->dst_wrap_addr = 0;

#if 0
	//timeout = S5_DMAC_TIMEOUT_MS * sx_clock_freq() / 1000;
	if (timeout > 0xffff)
		timeout = 0xffff;
#endif
	timeout = 1;

	priv->rx_chan->terminal_count_number = S5_RX_THRESHOLD;
	priv->rx_chan->terminal_count_timeout = timeout;

	/* Clear count status */

	tmp = priv->rx_chan->terminal_count_status;

	priv->rx_chan->src_cntl = 
		SX_DMAC_CHANNEL_SRC_CNTL_MK(0, /* addr incr */
					    0, /* addr decr */
					    0, /* wrap incr */
					    0, /* wrap decr */
					    1, /* postfix read enable */
					    SX_DMAC_MASTER_AHB,
					    SX_DMAC_TRANSFER_WIDTH_QWORD,
					    SX_DMAC_BURST_SIZE_128);
	priv->rx_chan->dst_cntl =
		SX_DMAC_CHANNEL_DST_CNTL_MK(1, /* addr incr */
					    0, /* addr decr */
					    0, /* wrap incr */
					    0, /* wrap decr */
					    0, /* prefix write enable */
					    SX_DMAC_MASTER_DDR,
					    SX_DMAC_TRANSFER_WIDTH_QWORD,
					    SX_DMAC_BURST_SIZE_256);

	priv->rx_chan->postfix_read_addr =
		(uint32_t) &gmac->host.postfix_read_data;

	/* Clear the pending list by setting bit 11 */

	priv->rx_chan->channel_cntl = 1 << 11;

	priv->rx_chan->channel_cntl = 
		SX_DMAC_CHANNEL_CNTL_MK(1, /* enable DMA */
					0, /* don't halt */
					SX_DMAC_MASTER_DDR,
					0, /* priority */
					SX_DMAC_PERIPHERAL_TO_MEMORY_PERIPH_LEN,
					SX_DMAC_CHUNK_SIZE_UNLIMITED,
					0, /* AHB access mode (unused) */
					SX_DMAC_TRANSFER_WIDTH_QWORD);

	priv->rx_chan->stored_head_ptr = virt_to_bus(priv->rx_ring_laddr);
	priv->rx_chan->pending_list_tail = virt_to_bus(priv->tx_ring_laddr);

	/* Setup TX DMAC channel */

	priv->tx_chan->src_stride = 0;
	priv->tx_chan->src_skip = 0;
	priv->tx_chan->src_wrap_addr = 0;
	priv->tx_chan->dst_stride = 0;
	priv->tx_chan->dst_skip = 0;
	priv->tx_chan->dst_wrap_addr = 0;

	priv->tx_chan->terminal_count_number = S5_TX_THRESHOLD;
	priv->tx_chan->terminal_count_timeout = timeout;
	tmp = priv->tx_chan->terminal_count_status; /* clear status */

	priv->tx_chan->src_cntl = 
		SX_DMAC_CHANNEL_SRC_CNTL_MK(1, /* addr incr */
					    0, /* addr decr */
					    0, /* wrap incr */
					    0, /* wrap desc */
					    0, /* postfix read enable */
					    SX_DMAC_MASTER_DDR,
					    SX_DMAC_TRANSFER_WIDTH_QWORD,
					    SX_DMAC_BURST_SIZE_256);
	priv->tx_chan->prefix_write_addr = 
		(uint32_t)&gmac->host.prefix_write_data;

	priv->tx_chan->dst_cntl = 
		SX_DMAC_CHANNEL_DST_CNTL_MK(0, /* addr incr */
					    0, /* addr desr */
					    0, /* wrap incr */
					    0, /* wrap desr */
					    1, /* prefix write enable */
					    SX_DMAC_MASTER_AHB,
					    SX_DMAC_TRANSFER_WIDTH_QWORD,
					    SX_DMAC_BURST_SIZE_128);

	/* clear pending list by setting bit 11 and enable channel */

	priv->tx_chan->channel_cntl = 1 << 11;
	priv->tx_chan->channel_cntl =
		SX_DMAC_CHANNEL_CNTL_MK(1, /* enable dma */
					0, /* don't halt */
					SX_DMAC_MASTER_DDR,
					0, /* priority */
					SX_DMAC_MEMORY_TO_PERIPHERAL_DMAC_LEN,
					SX_DMAC_CHUNK_SIZE_UNLIMITED,
					0, /* AHB access mode (unused) */
					SX_DMAC_TRANSFER_WIDTH_QWORD);

	/* Initialize the GMAC */

	s5_gmac_init(priv);
	netif_start_queue(dev);

	s5_mii_link_state(priv);
	s5_mii_link_speed(priv);


	return 0;

out:
	/* Free any skb buffers that were allocated sucessfully. */

	while (--i > 0) {
		dev_kfree_skb(priv->rx_skb[i]);
		priv->rx_skb[i] = NULL;
	}
	return ret;
}

static int s5_gmac_close (struct net_device *dev)
{
	struct s5_gmac_priv *priv = netdev_priv(dev);
	sx_gmac_dev *gmac = (sx_gmac_dev*) priv->gmac_info->base_addr;

	printk("s5_gmac_close()\n");

	// FIXME: wait till tx descriptors have been sent?

	/* Disable PPI */

	gmac->mac.config_1 = 
		SX_GMAC_CONFIG_1(1, /* soft reset */
				 0, /* simulation reset */
				 1, /* reset rx control logic */
				 1, /* reset tx control logic */
				 1, /* rsset rx functional block */
				 1, /* reset tx functional block */
				 0, /* loopback */
				 0, /* rx flow control off */
				 0, /* tx flow control off */
				 0, /* disable tx */
				 0); /* disable rx */

	/* Disable DMAC channels */

	priv->rx_chan->channel_cntl = 0;
	priv->tx_chan->channel_cntl = 0;

	/* Free receive buffers */

	/* Free descriptor buffers */
// FIXME
	return 0;
}


static int __devinit
s5_gmac_probe1(struct platform_device *pdev)
{
	struct net_device *dev;
	struct s5_gmac_priv *priv;
	//struct gmac_device *gmac;
	volatile sx_global_registers *greg;
	volatile sx_dmac_registers *dmac;
	int err = 0;

	if ((dev = alloc_etherdev(sizeof (struct s5_gmac_priv))) == NULL)
		return -ENOMEM;

	priv = netdev_priv(dev);
	priv->device = &pdev->dev;
	SET_NETDEV_DEV(dev, &pdev->dev);
	//SET_MODULE_OWNER(dev);

	dev->open		= s5_gmac_open;
	dev->stop		= s5_gmac_close;
	dev->hard_start_xmit	= s5_gmac_send;
	//dev->get_stats		= s5_gmac_get_stats;
	//dev->set_multicast_list = s5_gmac_set_multicast_list;
	//dev->weight		= 16;

	/* ... */

	greg = (sx_global_registers*)SXP1_GLOBAL_REG_BASE;
	priv->sx_gregs = greg;

	//spin_lock_init(&priv->tx_lock);

	priv->gmac_info = (struct gmac_dev_info*)(pdev->dev.platform_data);

	memcpy(dev->dev_addr, priv->gmac_info->eth_addr, 6);

	printk("S5 GMAC @%p IRQ:%d MAC:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x\n",
			greg, S5000_INTNUM_DMAC0,
			dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
			dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);

	dmac = (sx_dmac_registers*)SXP1_DMAC_BASE;
	priv->tx_chan = &dmac->chan_reg[priv->gmac_info->dmac_index];
	priv->rx_chan = &dmac->chan_reg[priv->gmac_info->dmac_index + 4];
	dev->irq = priv->gmac_info->dmac_index;

	/* For S55DB, port 2 -> phy 0 and port 3 -> phy 2. */
#ifdef __STRETCH_S5530
	priv->phy_addr = priv->gmac_info->dmac_index == 3 ? 2 : 0;
#else
	priv->phy_addr = priv->gmac_info->dmac_index;
#endif

	// FIXME: we probably want to 'get' grec from another module


#if 0
	greg = (sx_global_registers*) SXP1_GLOBAL_REG_BASE;
	if (!request_mem_region(dev->base_addr,S5_GMAC_MEM_SIZE,s5_gmac_string))
		return -EBUSY;

	if (err)
		goto out;
#endif

	if ((err = register_netdev(dev)) != 0)
		goto out1;

	return 0;

out1:
#if 0
	release_region(dev->base_addr, S5_GMAC_MEM_SIZE);
out:
#endif
	free_netdev(dev);

	return err;
}


static struct platform_driver s5_gmac_driver = {
	.probe = s5_gmac_probe1,
	.remove = __devexit_p(s5_gmac_device_remove),
	.driver = {
		.name = s5_gmac_string,
                .owner  = THIS_MODULE,
	},
};

static int 
s5_gmac_init_module(void)
{
	//sx_eerom_stretch_board_info_t bi;
	int err;
	int i;

#if 0
	/* Attempt to read the board's EEROM */

	if ((err = stretch_board_info_read(&bi))) {
		printk(KERN_ERR "EEROM read failed!\n");
		return -ENODEV;
	}
#endif

	/* Register drivers */

	if ((err = platform_driver_register(&s5_gmac_driver))) {
		printk(KERN_ERR "Driver registration failed\n");
		return err;
	}

	/* FIXME: we should check if there is a device! */

	for (i = 0; i < S5_NUM_GMACS; i++) {
		struct platform_device *pldev;

		/* First, initialize gmac info structure */

		s5_gmac_dev_info[i].dmac_index = i;
		s5_gmac_dev_info[i].base_addr = SXP1_GMAC_BASE
						+ SX_GMAC1_OFFSET * i;
#if 0		
		memcpy(s5_gmac_dev_info[i].eth_addr, &bi.eth_addr0[i], 6);
#endif
		{
			unsigned char *e = s5_gmac_dev_info[i].eth_addr;
			e[0] = 0x00;
			e[1] = 0x0f;
			e[2] = 0x45;
			e[3] = 0x00;
			e[4] = 0x02;
			e[5] = 0xf0 + i;
		}

		/* Initialize platform device */

		pldev = platform_device_alloc(s5_gmac_string, i);
		if (pldev == NULL)
			continue;

		pldev->dev.platform_data = &s5_gmac_dev_info[i];
		s5_gmac_dev_info[i].device = pldev;

		if (platform_device_add(pldev)) {
			platform_device_put(pldev);
			s5_gmac_dev_info[i].device = NULL;
			continue;
		}

		if (!pldev->dev.driver) {
			s5_gmac_dev_info[i].device = NULL;
		}
	}

	return 0;
}

static void __exit
s5_gmac_exit_module(void)
{
	int i;

	platform_driver_unregister(&s5_gmac_driver);

	for (i = 0; i < S5_NUM_GMACS; i++) {
		if (s5_gmac_dev_info[i].device != NULL) {
			platform_device_unregister(s5_gmac_dev_info[i].device);
			s5_gmac_dev_info[i].device = NULL;
		}
	}
}

module_init(s5_gmac_init_module);
module_exit(s5_gmac_exit_module);
