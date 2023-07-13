#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/types.h>
#include <asm/processor.h>
#include <asm/trax.h>

#if XCHAL_HAVE_TRAX

#define TRAX_ERI_BASE	0x00100000

#define TRAXID		0x00
#define TRAXID_PRODNO_MASK	0xf0000000
#define TRAXID_PRODNO_SHIFT	28
#define TRAXID_MAJVER_MASK	0x00f00000
#define TRAXID_MAJVER_SHIFT	20
#define TRAXID_STDCFG_MASK	0x00010000
#define TRAXCTRL	0x04
#define TRAXCTRL_TREN_MASK	0x00000001
#define TRAXCTRL_TRSTP_MASK	0x00000002
#define TRAXCTRL_PCMEN_MASK	0x00000004
#define TRAXCTRL_PTIEN_MASK	0x00000010
#define TRAXCTRL_CTIEN_MASK	0x00000020
#define TRAXCTRL_TMEN_MASK	0x00000080
#define TRAXCTRL_CNTU_MASK	0x00000200
#define TRAXCTRL_TSEN_MASK	0x00000800
#define TRAXCTRL_SMPER_MASK	0x00007000
#define TRAXCTRL_SMPER_SHIFT	12
#define TRAXCTRL_PTOWT_MASK	0x00010000
#define TRAXCTRL_PTOWS_MASK	0x00020000
#define TRAXCTRL_CTOWT_MASK	0x00100000
#define TRAXCTRL_CTOWS_MASK	0x00200000
#define TRAXSTAT	0x08
#define TRAXSTAT_TRACT_MASK	0x00000001
#define TRAXSTAT_TRIG_MASK	0x00000002
#define TRAXSTAT_PCMTG_MASK	0x00000004
#define TRAXSTAT_MEMSZ_MASK	0x00001f00
#define TRAXSTAT_MEMSZ_SHIFT	8
#define TRAXDATA	0x0c
#define TRAXADDR	0x10
#define TRAXADDR_TADDR_MASK	0x001fffff
#define TRAXADDR_TWRAP_MASK	0x7fe00000
#define TRAXADDR_TWSAT_MASK	0x80000000
#define TRIGGERPC	0x14
#define PCMATCHCTRL	0x18
#define PCMATCHCTRL_PCML_MASK	0x0000001f
#define PCMATCHCTRL_PCMS_MASK	0x80000000
#define DELAYCOUNT	0x1c
#define MEMSTARTADDR	0x20
#define MEMENDADDR	0x24

struct trax_state {
	u32 memstart;		/* copy of the MEMSTARTADDR */
	u32 memend;		/* copy of the MEMENDADDR */
	u32 memsz;		/* size of TRAX memory in bytes, derived from TRAXSTAT.MEMSZ */
	u32 addr;		/* address of the first trace word */
	u32 size;		/* trace size in words */
};

static struct trax_state trax_state;

static void trax_wer(u32 v, u32 reg)
{
	set_er(v, TRAX_ERI_BASE + reg);
}

static u32 trax_rer(u32 reg)
{
	return get_er(TRAX_ERI_BASE + reg);
}

static int trax_start_flags(u32 flags)
{
	u32 ctrl = trax_rer(TRAXCTRL);
	u32 stat = trax_rer(TRAXSTAT);

	if (stat & TRAXSTAT_TRACT_MASK) {
		pr_err("%s: trace is running\n", __func__);
		return -EBUSY;
	}
	trax_wer(0, DELAYCOUNT);

	trax_wer((ctrl & ~(TRAXCTRL_TRSTP_MASK |
			   TRAXCTRL_PCMEN_MASK)) |
		 (TRAXCTRL_TREN_MASK |
		  TRAXCTRL_TMEN_MASK) | flags,
		 TRAXCTRL);

	return 0;
}

int trax_start(void)
{
	return trax_start_flags(0);
}

int trax_stop(void)
{
	u32 stat = trax_rer(TRAXSTAT);
	u32 ctrl = trax_rer(TRAXCTRL);
	u32 addr;

	if (stat & TRAXSTAT_TRACT_MASK) {
		int i;

		trax_wer(ctrl | TRAXCTRL_TRSTP_MASK, TRAXCTRL);
		trax_wer(0, DELAYCOUNT);
		for (i = 100; i --> 0; ) {
			stat = trax_rer(TRAXSTAT);
			if (!(stat & TRAXSTAT_TRACT_MASK))
				break;
		}
		if (!i)
			return -EBUSY;
	}
	if (ctrl & TRAXCTRL_TREN_MASK) {
		addr = trax_rer(TRAXADDR);
		if (addr & (TRAXADDR_TWRAP_MASK | TRAXADDR_TWSAT_MASK)) {
			/*
			 * memend may be less than memstart in which case TRAX buffer
			 * wraps around the end of available TRAX memory
			 */
			trax_state.size = ((trax_state.memend - trax_state.memstart) &
					   (trax_state.memsz - 1)) + 1;
			trax_state.addr = addr & TRAXADDR_TADDR_MASK;
		} else {
			trax_state.size = addr - trax_state.memstart;
			trax_state.addr = trax_state.memstart;
		}
		trax_wer(ctrl & ~TRAXCTRL_TREN_MASK, TRAXCTRL);
	}
	return 0;
}

static u32 trax_addr_next(u32 addr)
{
	if (addr == trax_state.memend)
		addr = trax_state.memstart;
	else
		++addr;
	return addr & (trax_state.memsz - 1);
}

static int trax_setup(void)
{
	u32 id = trax_rer(TRAXID);
	u32 ctrl = trax_rer(TRAXCTRL);
	u32 stat = trax_rer(TRAXSTAT);

	pr_debug("%s: traxid = 0x%08x, traxctrl = 0x%08x, traxstat = 0x%08x\n",
		 __func__, id, ctrl, stat);
	pr_debug("%s: memstartaddr = 0x%08x, memendaddr = 0x%08x\n",
		 __func__, trax_rer(MEMSTARTADDR), trax_rer(MEMENDADDR));
	if ((id & (TRAXID_PRODNO_MASK |
		   TRAXID_MAJVER_MASK |
		   TRAXID_STDCFG_MASK)) !=
	    ((0 << TRAXID_PRODNO_SHIFT) |
	     (3 << TRAXID_MAJVER_SHIFT) |
	     TRAXID_STDCFG_MASK)) {
		pr_warn("%s: unsupported TRAX hardware configuration, TRAXID = 0x%08x\n",
			__func__, id);
		return -ENODEV;
	}
	trax_state.memsz =
		1 << (((trax_rer(TRAXSTAT) & TRAXSTAT_MEMSZ_MASK) >>
		       TRAXSTAT_MEMSZ_SHIFT) - 2);
	trax_state.memstart = trax_rer(MEMSTARTADDR);
	trax_state.memend = trax_rer(MEMENDADDR);
	trax_wer(ctrl & ~(TRAXCTRL_CTOWS_MASK |
			  TRAXCTRL_CTOWT_MASK |
			  TRAXCTRL_PTOWS_MASK |
			  TRAXCTRL_PTOWT_MASK), TRAXCTRL);
	trax_stop();
	return 0;
}

static int __init trax_init(void)
{
	int rv = trax_setup();

	return rv ? rv : trax_start();
}
core_initcall(trax_init);

#ifdef CONFIG_TRAX_DECODE

static const char * const branch_type_str[] = {
	"?",
	"normal",
	"exception",
	"loopback",
	"OCD return",
	"OCD exception",
};

struct trax_context {
	u32 addr;
	bool valid;
};

struct trax_msg {
	enum {
		TRAX_MSG_TYPE_IGNORE,
		TRAX_MSG_TYPE_UNDEF,
		TRAX_MSG_TYPE_IBM,
		TRAX_MSG_TYPE_FIRST = TRAX_MSG_TYPE_IBM,
		TRAX_MSG_TYPE_IBMS,
		TRAX_MSG_TYPE_SYNC,
		TRAX_MSG_TYPE_CORR,
		TRAX_MSG_TYPE_LAST = TRAX_MSG_TYPE_CORR,
	} type;
	enum {
		TRAX_MSG_BRANCH_UNDEF,
		TRAX_MSG_BRANCH_NORMAL,
		TRAX_MSG_BRANCH_NON_OCD_EXCEPTION,
		TRAX_MSG_BRANCH_LOOPBACK,
		TRAX_MSG_BRANCH_OCD_RETURN,
		TRAX_MSG_BRANCH_OCD_EXCEPTION,
	} branch_type;
	u16 icnt;
	u8 dcont;
	u32 addr;
	u64 timestamp;
};

struct trax_stream_context {
	u8 packet_buffer[15];
	u8 packet_size;		/* bits in the packet_buffer */
	struct trax_msg msg;
	u8 msg_packet;		/* current packet in the message */

	u8 scratch_type;
	u8 scratch_branch_type;
};

struct trax_field {
	u8 src_bits;
	u8 dst_bytes;
	u8 dst_offset;
	bool (*fixup)(struct trax_stream_context *ctx);
};

static bool trax_msg_type_fixup(struct trax_stream_context *ctx)
{
	switch (ctx->scratch_type) {
	case 4:
		ctx->msg.type = TRAX_MSG_TYPE_IBM;
		return true;
	case 12:
		ctx->msg.type = TRAX_MSG_TYPE_IBMS;
		return true;
	case 9:
		ctx->msg.type = TRAX_MSG_TYPE_SYNC;
		return true;
	case 33:
		ctx->msg.type = TRAX_MSG_TYPE_CORR;
		return true;
	default:
		return false;
	}
}

#if 0
static bool trax_msg_branch_type_fixup_v4(struct trax_stream_context *ctx)
{
	switch (ctx->scratch_branch_type) {
	case 0:
		ctx->msg.branch_type = TRAX_MSG_BRANCH_NORMAL;
		return true;
	case 1:
		ctx->msg.branch_type = TRAX_MSG_BRANCH_NON_OCD_EXCEPTION;
		return true;
	case 2:
		ctx->msg.branch_type = TRAX_MSG_BRANCH_LOOPBACK;
		return true;
	case 4:
		ctx->msg.branch_type = TRAX_MSG_BRANCH_OCD_RETURN;
		return true;
	case 6:
		ctx->msg.branch_type = TRAX_MSG_BRANCH_OCD_EXCEPTION;
		return true;
	default:
		return false;
	}
}
#endif

static bool trax_msg_branch_type_fixup(struct trax_stream_context *ctx)
{
	switch (ctx->scratch_branch_type) {
	case 0:
		ctx->msg.branch_type = TRAX_MSG_BRANCH_NORMAL;
		return true;
	case 1:
		ctx->msg.branch_type = TRAX_MSG_BRANCH_NON_OCD_EXCEPTION;
		return true;
	default:
		return false;
	}
}
static bool trax_msg_evcode_fixup(struct trax_stream_context *ctx)
{
	switch (ctx->scratch_branch_type) {
	case 0xa:
		return true;
	default:
		return false;
	}
}

static const struct trax_field trax_ibm_packet0[] = {
	{
		.src_bits = 6,
		.dst_bytes = 1,
		.dst_offset = offsetof(struct trax_stream_context, scratch_type),
		.fixup = trax_msg_type_fixup,
	}, {
		.src_bits = 1,
		.dst_bytes = 1,
		.dst_offset = offsetof(struct trax_stream_context, scratch_branch_type),
		.fixup = trax_msg_branch_type_fixup,
	}, {
		.dst_bytes = 2,
		.dst_offset = offsetof(struct trax_stream_context, msg.icnt),
	},
};

static const struct trax_field trax_ibms_packet0[] = {
	{
		.src_bits = 6,
		.dst_bytes = 1,
		.dst_offset = offsetof(struct trax_stream_context, scratch_type),
		.fixup = trax_msg_type_fixup,
	}, {
		.src_bits = 1,
		.dst_bytes = 1,
		.dst_offset = offsetof(struct trax_stream_context, msg.dcont),
	}, {
		.src_bits = 1,
		.dst_bytes = 1,
		.dst_offset = offsetof(struct trax_stream_context, scratch_branch_type),
		.fixup = trax_msg_branch_type_fixup,
	}, {
		.dst_bytes = 2,
		.dst_offset = offsetof(struct trax_stream_context, msg.icnt),
	},
};

static const struct trax_field trax_sync_packet0[] = {
	{
		.src_bits = 6,
		.dst_bytes = 1,
		.dst_offset = offsetof(struct trax_stream_context, scratch_type),
		.fixup = trax_msg_type_fixup,
	}, {
		.src_bits = 1,
		.dst_bytes = 1,
		.dst_offset = offsetof(struct trax_stream_context, msg.dcont),
	}, {
		.dst_bytes = 2,
		.dst_offset = offsetof(struct trax_stream_context, msg.icnt),
	},
};

static const struct trax_field trax_corr_packet0[] = {
	{
		.src_bits = 6,
		.dst_bytes = 1,
		.dst_offset = offsetof(struct trax_stream_context, scratch_type),
		.fixup = trax_msg_type_fixup,
	}, {
		.src_bits = 6,
		.dst_bytes = 1,
		.dst_offset = offsetof(struct trax_stream_context, scratch_branch_type),
		.fixup = trax_msg_evcode_fixup,
	}, {
		.dst_bytes = 2,
		.dst_offset = offsetof(struct trax_stream_context, msg.icnt),
	},
};

static const struct trax_field trax_addr_packet[] = {
	{
		.dst_bytes = 4,
		.dst_offset = offsetof(struct trax_stream_context, msg.addr),
	},
};

static const struct trax_field trax_timestamp_packet[] = {
	{
		.dst_bytes = 8,
		.dst_offset = offsetof(struct trax_stream_context, msg.timestamp),
	},
};

static const struct {
	u8 max_packets;
	const struct trax_field *packet[3];
} trax_decode[] = {
	[TRAX_MSG_TYPE_IBM] = {
		.max_packets = 3,
		.packet = {
			trax_ibm_packet0,
			trax_addr_packet,
			trax_timestamp_packet,
		},
	},
	[TRAX_MSG_TYPE_IBMS] = {
		.max_packets = 3,
		.packet = {
			trax_ibms_packet0,
			trax_addr_packet,
			trax_timestamp_packet,
		},
	},
	[TRAX_MSG_TYPE_SYNC] = {
		.max_packets = 3,
		.packet = {
			trax_sync_packet0,
			trax_addr_packet,
			trax_timestamp_packet,
		},
	},
	[TRAX_MSG_TYPE_CORR] = {
		.max_packets = 2,
		.packet = {
			trax_corr_packet0,
			trax_timestamp_packet,
		},
	},
};

static bool trax_decode_field(struct trax_stream_context *ctx,
			      const struct trax_field *field,
			      u32 start_bit)
{
	u32 i, n;

	memset((char *)ctx + field->dst_offset, 0, field->dst_bytes);
	if (ctx->packet_size - start_bit < field->src_bits) {
		pr_debug("%s: wanted %d bits, got %d - %d\n",
			 __func__, field->src_bits, ctx->packet_size, start_bit);
		return false;
	}

	n = field->src_bits ? field->src_bits : ctx->packet_size - start_bit;
	if (n > field->dst_bytes * 8)
		n = field->dst_bytes * 8;

	for (i = 0; i < n; i += 8) {
		u32 j = start_bit + i;
		u32 b = ctx->packet_buffer[j / 8] >> (j % 8);

		if (j % 8 && n - i > 8 - j % 8) {
			b |= ctx->packet_buffer[j / 8 + 1] << (8 - j % 8);
		}
		if (n - i < 8)
			b &= (1 << (n - i)) - 1;
		*((u8 *)ctx + field->dst_offset + i / 8) = b;
	}
#ifdef DEBUG
	print_hex_dump(KERN_DEBUG, " ", DUMP_PREFIX_NONE,
		       32, 1, ((u8 *)ctx + field->dst_offset), field->dst_bytes, false);
#endif

	if (field->fixup)
		return field->fixup(ctx);
	else
		return true;
}

static bool trax_decode_fields(struct trax_stream_context *ctx,
			       const struct trax_field *field)
{
	u32 i = 0;

	for (;;) {
		if (!trax_decode_field(ctx, field, i))
			return false;
		if (field->src_bits)
			i += field->src_bits;
		else
			return true;
		++field;
	}
}

static bool trax_decode_packet(struct trax_stream_context *ctx)
{
	pr_debug("%s: packet_size = %d\n", __func__, ctx->packet_size);
#ifdef DEBUG
	print_hex_dump(KERN_DEBUG, " ", DUMP_PREFIX_NONE,
		       32, 1, ctx->packet_buffer, (ctx->packet_size + 7) / 8, false);
#endif

	if (ctx->msg.type == TRAX_MSG_TYPE_UNDEF) {
		u32 i;

		for (i = TRAX_MSG_TYPE_FIRST; i <= TRAX_MSG_TYPE_LAST; ++i)
			if (trax_decode_fields(ctx, trax_decode[i].packet[0])) {
				ctx->msg_packet = 1;
				return true;
			}
		return false;
	} else {
	    if (ctx->msg_packet < trax_decode[ctx->msg.type].max_packets &&
		trax_decode_fields(ctx, trax_decode[ctx->msg.type].packet[ctx->msg_packet])) {
		    ++ctx->msg_packet;
		    return true;
	    } else {
		    ctx->msg.type = TRAX_MSG_TYPE_UNDEF;
		    ctx->msg_packet = 0;
		    return false;
	    }
	}
}

static bool trax_decode_message(struct trax_context *trax_ctx,
				const struct trax_msg *msg)
{
	struct trax_context next = *trax_ctx;
	bool ret;

	switch (msg->type) {
	case TRAX_MSG_TYPE_IBM:
		pr_debug("%s:  IBM: type: %13s, icnt = %4d, u-addr = 0x%08x, tstamp = %lld\n",
			 __func__, branch_type_str[msg->branch_type],
			 msg->icnt, msg->addr, msg->timestamp);
		next.addr ^= msg->addr;
		trax_ctx->addr += msg->icnt;
		break;
	case TRAX_MSG_TYPE_IBMS:
		pr_debug("%s: IBMS: type: %13s, icnt = %4d, f-addr = 0x%08x, tstamp = %lld, dcont = %d\n",
			 __func__, branch_type_str[msg->branch_type],
			 msg->icnt, msg->addr, msg->timestamp, msg->dcont);
		next.addr = msg->addr;
		next.valid = true;
		trax_ctx->addr += msg->icnt;
		break;
	case TRAX_MSG_TYPE_SYNC:
		pr_debug("%s: SYNC: type: %13s, icnt = %4d, f-addr = 0x%08x, tstamp = %lld, dcont = %d\n",
			 __func__, "-",
			 msg->icnt, msg->addr, msg->timestamp, msg->dcont);
		next.addr = msg->addr;
		next.valid = true;
		break;
	case TRAX_MSG_TYPE_CORR:
		pr_debug("%s: CORR: type: %13s, icnt = %4d, ------ = 0x%08x, tstamp = %lld\n",
			 __func__, "-----",
			 msg->icnt, 0, msg->timestamp);
		trax_ctx->addr += msg->icnt;
		break;
	default:
		return true;
	}

	switch (msg->type) {
	case TRAX_MSG_TYPE_IBM:
		if (!next.valid)
			break;
		fallthrough;
	case TRAX_MSG_TYPE_IBMS:
		pr_info("%s: 0x%08x -> 0x%08x (%s) @ %lld\n",
			__func__, trax_ctx->addr, next.addr,
			branch_type_str[msg->branch_type], msg->timestamp);
		break;
	case TRAX_MSG_TYPE_SYNC:
		pr_info("%s: 0x%08x @ %lld\n",
			__func__, next.addr, msg->timestamp);
		break;
	default:
		break;
	}
	ret = true;

	*trax_ctx = next;
	return ret;
}

static bool trax_decode_stream(struct trax_context *trax_ctx,
			       struct trax_stream_context *stm_ctx, u32 data)
{
	u32 i;

	for (i = 0; i < sizeof(data); ++i) {
		u8 b = data & 0xff;
		u8 v = b >> 2;
		u8 packet_size = stm_ctx->packet_size;

		data >>= 8;
		if (stm_ctx->msg.type == TRAX_MSG_TYPE_IGNORE) {
			if ((b & 3) == 3) {
				pr_debug("%s: type = TRAX_MSG_TYPE_UNDEF\n", __func__);
				stm_ctx->msg.type = TRAX_MSG_TYPE_UNDEF;
				stm_ctx->packet_size = 0;
			}
		} else if (packet_size + 6 < sizeof(stm_ctx->packet_buffer) * 8) {
			if (!(packet_size % 8))
				stm_ctx->packet_buffer[packet_size / 8] = v;
			else
				stm_ctx->packet_buffer[packet_size / 8] |= v << (packet_size % 8);
			if (packet_size % 8 > 2)
				stm_ctx->packet_buffer[packet_size / 8 + 1] = v >> (8 - packet_size % 8);
			stm_ctx->packet_size += 6;

			if (b & 1) {
				bool ret = trax_decode_packet(stm_ctx);

				stm_ctx->packet_size = 0;
				if (!ret) {
					if (b & 2)
						stm_ctx->msg.type = TRAX_MSG_TYPE_UNDEF;
					else
						stm_ctx->msg.type = TRAX_MSG_TYPE_IGNORE;
				} else if (b & 2) {
					if (!trax_decode_message(trax_ctx, &stm_ctx->msg))
						return false;
					stm_ctx->msg.type = TRAX_MSG_TYPE_UNDEF;
				}
			}
		} else {
			pr_debug("%s: type = TRAX_MSG_TYPE_IGNORE, stm_ctx->packet_size = %d\n",
				 __func__, stm_ctx->packet_size);
			stm_ctx->msg.type = TRAX_MSG_TYPE_IGNORE;
		}
	}
	return true;
}

void trax_dump(void)
{
	struct trax_context trax_ctx = {0};
	struct trax_stream_context stm_ctx = {0};
	u32 i, addr;
	int ret;

	ret = trax_stop();
	if (ret < 0) {
		pr_err("%s: strax_stop didn't succeed, not dumping anything\n", __func__);
		return;
	}
	addr = trax_state.addr;
	pr_debug("%s: dumping 0x%x words from 0x%x\n", __func__, trax_state.size, addr);
	for (i = 0; i < trax_state.size; ++i) {
		u32 data;

		trax_wer(addr, TRAXADDR);
		data = trax_rer(TRAXDATA);
		pr_debug("%s: addr = 0x%08x, data = 0x%08x\n", __func__, addr, data);
		if (!trax_decode_stream(&trax_ctx, &stm_ctx, data))
			return;
		addr = trax_addr_next(addr);
	}
}

#else

void trax_dump(void)
{
	char buf[16 * 9 + 1];
	u32 i, j, addr;
	int ret;

	ret = trax_stop();
	if (ret < 0) {
		pr_err("%s: strax_stop didn't succeed, not dumping anything\n", __func__);
		return;
	}
	addr = trax_state.addr;
	pr_debug("%s: dumping 0x%x words from 0x%x\n", __func__, trax_state.size, addr);
	for (i = 0; i < trax_state.size; i += 16) {
		for (j = 0; j < 16 && i + j < trax_state.size; ++j) {
			u32 data;

			trax_wer(addr, TRAXADDR);
			data = trax_rer(TRAXDATA);
			sprintf(buf + j * 9, " %08x", data);
			addr = trax_addr_next(addr);
		}
		pr_info("%s: %04x: %s\n", __func__, i, buf);
	}
}

#endif

#ifdef CONFIG_PROC_FS

static int trax_proc_open(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t trax_proc_write(struct file *file, const char __user *buffer,
			       size_t count, loff_t *pos)
{
	if (*pos != 0)
		return -EINVAL;

	if (count > 0) {
		char buf[32];
		unsigned long sz = min(count, sizeof(buf) - 1);
		unsigned long rd = copy_from_user(buf, buffer, sz);
		int rv = 0;
		char *p;

		pr_debug("%s: count = %zd, rd = %ld\n", __func__, count, rd);
		if (rd)
			return -EFAULT;

		p = memchr(buf, '\n', sz);
		if (p)
			*p = 0;
		else
			buf[sz] = 0;

		if (strcmp(buf, "dump") == 0) {
			trax_dump();
			rv = 0;
		} else if (strcmp(buf, "stop") == 0) {
			rv = trax_stop();
		} else if (strcmp(buf, "start") == 0) {
			rv = trax_start();
		} else {
			char *p = buf;
			u32 matchctrl = 0;
			u32 addr;
			u32 mask;
			int n;

			if (*p == '!') {
				matchctrl = PCMATCHCTRL_PCMS_MASK;
				++p;
			}
			n = sscanf(p, "%i/%i", &addr, &mask);
			if (n == 1) {
				mask = 0;
				++n;
			}
			if (n == 2 && mask < 32) {
				trax_wer(addr, TRIGGERPC);
				trax_wer(matchctrl | mask, PCMATCHCTRL);
				rv = trax_start_flags(TRAXCTRL_PCMEN_MASK);
			} else {
				rv = -EINVAL;
			}
		}
		if (rv < 0)
			count = rv;
		else
			*pos += count;
	}
	return count;
}

static ssize_t trax_proc_read(struct file *file, char __user *buffer,
			      size_t size, loff_t *pos)
{
	u32 addr = trax_state.addr + *pos / 4;
	size_t off = 0;
	size_t soff = *pos % 4;

	if (trax_rer(TRAXSTAT) & TRAXSTAT_TRACT_MASK) {
		pr_debug("%s: TRAXSTAT.TRACT is enabled, not dumping\n",
			 __func__);
		return -EINVAL;
	}
	if (trax_rer(TRAXCTRL) & TRAXCTRL_TREN_MASK) {
		int rv = trax_stop();

		if (rv)
			return rv;
	}

	if (*pos >= trax_state.size * 4)
		return 0;

	if (addr > trax_state.memend)
		addr -= trax_state.size;

	if (trax_state.size * 4 - *pos < size)
		size = trax_state.size * 4 - *pos;

	if (!access_ok(buffer, size))
		return -EFAULT;

	while (off < size) {
		u32 data;
		u32 sz = min(size - off, sizeof(data) - soff);

		trax_wer(addr, TRAXADDR);
		data = trax_rer(TRAXDATA);
		if (__copy_to_user(buffer + off, (char *)&data + soff, sz))
			return -EFAULT;
		soff = 0;
		off += sz;
		addr = trax_addr_next(addr);
	}
	*pos += off;
	return off;
}

static const struct proc_ops trax_proc_ops = {
	.proc_flags	= PROC_ENTRY_PERMANENT,
	.proc_open	= trax_proc_open,
	.proc_write	= trax_proc_write,
	.proc_read	= trax_proc_read,
	.proc_lseek	= default_llseek,
};

static int __init proc_trax_init(void)
{
	struct proc_dir_entry *pde;

	pde = proc_create("trax", 0, NULL, &trax_proc_ops);
	if (pde) {
		proc_set_size(pde, trax_state.memsz * 4);
	}
	return 0;
}
fs_initcall(proc_trax_init);

#endif

#endif
