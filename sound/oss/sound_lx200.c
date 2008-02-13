#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/errno.h>
#include <linux/sound.h>
#include <linux/soundcard.h>
#include <linux/interrupt.h>

#include <asm/uaccess.h>
#include <asm/hardware.h>
#include <asm/io.h>

#include <asm/hardware.h>
#include <asm/platform/system.h>

#include <linux/module.h>       /* Specifically, a module */
#include <linux/kernel.h>       /* We're doing kernel work */
#include <linux/proc_fs.h>      /* Necessary because we use the proc fs */
#include <asm/uaccess.h>        /* for copy_from_user */

#define SPI_START       *(volatile char*)(XSHAL_IOBLOCK_BYPASS_VADDR+0x0d070000)
#define SPI_BUSY        *(volatile char*)(XSHAL_IOBLOCK_BYPASS_VADDR+0x0d070004)
#define SPI_DATA        *(volatile int*) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0d070008)
#define I2S_START       *(volatile char*)(XSHAL_IOBLOCK_BYPASS_VADDR+0x0d07000c)
#define I2S_DATA        *(volatile int*) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0d070010)
#define INT_REG         *(volatile int*) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0d070014)
#define INT_FIFOLEVEL   *(volatile int*) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0d070018)
#define NUM_FIFOENTRIES *(volatile int*) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0d07001c)
#define INT_MASK        *(volatile int*) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0d070020)

#define TOTAL_AUDIO_FIFO_ENTRIES 8192 * 4


#define CODEC_NAME		 "sound_lx200"

#define BUFFER_SIZE 48000
#define AUDIO_RATE_DEFAULT	      44100


static spinlock_t lx200_lock;
static char *data_buffer;
static int in_use; /* Used for exclusive access to the device. */
static int device_major;
static int audio_channels = 2;
/* To Store the default sample rate */
static long audio_samplerate = AUDIO_RATE_DEFAULT;

/* DAC register definitions, the DAC is an aic23 */
enum {
	AIC23_LEFTINVOL = 0,
	AIC23_RIGHTINVOL,
	AIC23_LEFTHPVOL,
	AIC23_RIGHTHPVOL,
	AIC23_ANAPATH,
	AIC23_DIGPATH,
	AIC23_POWERDOWN,
	AIC23_DIGIF,
	AIC23_SAMPLERATE,
	AIC23_DIGACT,
	AIC23_NUMREGS,
};

#define AIC23_RESET           15

static int aic23_regs[AIC23_NUMREGS];

static void aic23_write_reg(int regno, int data)
{
	int dummy = 0, i;

	while(SPI_BUSY)
		dummy++;

	if (regno != AIC23_RESET)
	{
		aic23_regs[regno] = data;
	}

	data = (regno<<9)|data;
	SPI_DATA = data;	/* write data */
	SPI_START=1;

	for (i=0; i<50; i++)
	  asm ("");	/* wait for write to complete */

	while(SPI_BUSY)
		dummy++;

	SPI_START=0;
}

static void aic23_config_dac(void)
{
	SPI_START=0;
	memset(aic23_regs, 0, sizeof(aic23_regs));
	aic23_write_reg(AIC23_RESET,      0x000); /* reset AIC23 */
	aic23_write_reg(AIC23_LEFTINVOL,  0x017); /* enabling left input */
	aic23_write_reg(AIC23_RIGHTINVOL, 0x017); /* enabling right input */
	aic23_write_reg(AIC23_ANAPATH,    0x014); /* analog audio path - enable DAC */
	aic23_write_reg(AIC23_DIGPATH,    0x004); /* digital audio path - default val */
	aic23_write_reg(AIC23_POWERDOWN,  0x000); /* power down control - enable clock */
	aic23_write_reg(AIC23_DIGIF,      0x062); /* digital aud interface format - master mode / LRSWAP */
	aic23_write_reg(AIC23_SAMPLERATE, 0x023); /* sample rate control - USB clock mode */
	aic23_write_reg(AIC23_DIGACT,     0x001); /* digital interface activation */
}

static void aic23_set_vol (int vol)
{
	/* vol supported range 0-10 */
        /* actual value is 48 to 127 (79 steps) */
	aic23_write_reg(AIC23_LEFTHPVOL,	vol);
	aic23_write_reg(AIC23_RIGHTHPVOL,	vol);
}

static int aic23_get_vol (void)
{
	int vol = aic23_regs[AIC23_LEFTHPVOL];
	return vol;
}

/* Registers setting for differnet sampling rates

 Freq    CKIN    SR3 SR2 SR1 SR0 BOSR    USBM    TOT

 48      0       0   0   0   0   0       1       0x001
 44.1    0       1   0   0   0   1       1       0x023
 32      0       0   1   1   0   0       1       0x019
 8       0       0   0   1   1   0       1       0x00D

 24      1       0   0   0   0   0       1       0x041
 22.05   1       1   0   0   0   1       1       0x063
 16      1       0   1   1   0   0       1       0x059

 Refer to the TLV320AIC23 part manual for details.
*/

static int aic23_set_freq(int freq)
{
	int retval = 0;

	switch (freq)
	{
		case 48000:
		aic23_write_reg(AIC23_SAMPLERATE,	0x001);
		break;

		case 44100:
		aic23_write_reg(AIC23_SAMPLERATE,	0x023);
		break;

		case 32000:
		aic23_write_reg(AIC23_SAMPLERATE,	0x019);
		break;

		case 8000:
		aic23_write_reg(AIC23_SAMPLERATE,	0x00D);
		break;

		//i/p clk div by 2
		case 24000:
		aic23_write_reg(AIC23_SAMPLERATE,	0x041);
		break;

		//i/p clk div by 2
		case 22050:
		aic23_write_reg(AIC23_SAMPLERATE,	0x063);
		break;

		//i/p clk div by 2
		case 16000:
		aic23_write_reg(AIC23_SAMPLERATE,	0x059);
		break;

		case 12000:
		aic23_write_reg(AIC23_SAMPLERATE,	0x041);
		break;

		case 11025:
		aic23_write_reg(AIC23_SAMPLERATE,	0x063);
		break;

		default:
		retval = -1;
		break;
	}

	return retval;
}

static void slx200_fifo_logic(void)
{
	I2S_START=0; //stop i2s logic
	I2S_START=1; //start i2s logic
}

static int slx200_audio_mute(int mute)
{
	static int mute_status = 0;
	static int prev_vol;
	if (mute_status == mute)
		return 0;
	mute_status = mute;
	if (mute) {
		prev_vol = aic23_get_vol();
		aic23_set_vol(0);
	}
	else 
		aic23_set_vol(prev_vol);
	return 0;
}


/* Write the audio data to the audio FIFO. */
static int slx200_fifo_write(signed char *data, int count)
{
	int i;
	short int *play_buf;
	play_buf = (short*)data;

	if (audio_channels == 1) {
		for (i = 0; i < count; i += 2) {
			short int audio_data = *play_buf++;
			/* For Mono, write the same thing on both
			 * channels. */
			I2S_DATA = audio_data;
			I2S_DATA = audio_data;
		}
	}
	else {
		for (i = 0; i < count; i += 4) {
			I2S_DATA = *play_buf++;
			I2S_DATA = *play_buf++;
		}
	}
	return 0;
}

static int slx200_set_sample_rate (long sample_rate)
{
	int retval;
	if ((retval = aic23_set_freq(sample_rate)) < 0) {
                printk(KERN_ERR "Invalid Sample Rate %d requested\n",
                       (int)sample_rate);
		return -EINVAL;
	}
        audio_samplerate = sample_rate;
        return retval;
}


/* Set the volume level. Level is from 0 to 10 */
static int slx200_set_volume (int vol_level)	
{
	static int volume_lookup_table [11] = {
		48, 66, 80, 92, 102, 110, 116, 120, 123, 125, 127 };

	if (vol_level > 10)
		vol_level = 10;
	else if (vol_level < 0)
		vol_level = 0;

	vol_level = volume_lookup_table [vol_level];
	aic23_set_vol (vol_level);

	return 0;
}

static int slx200_set_channels (int channels)
{
	if ((channels != 2) && (channels != 1))
		return -1;
	audio_channels = channels;
	return 0;
}

static int
slx200_audio_ioctl(struct inode *inode, struct file *file, uint cmd, ulong arg)
{
        long val;
        int ret = 0;

        switch (cmd) {
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, (int *)arg);
	case SNDCTL_DSP_CHANNELS:
                if (get_user(val, (long *)arg))
                        return -EFAULT;
                ret = slx200_set_channels(val);
                if (ret)
                        break;
		/* Fall through. */
        case SOUND_PCM_READ_CHANNELS:
                return put_user(audio_channels, (long *)arg);
	case SNDCTL_DSP_STEREO:
		if (get_user(val, (long *)arg))
                        return -EFAULT;
		/* val == 0 means mono*/
		if (val == 0)
			ret = slx200_set_channels(1);
		else if (val == 1) /* val == 1 means stereo */
			ret = slx200_set_channels(2);
		else
			/* http://www.4front-tech.com/pguide/audio.html
			 * says that anything else than 0 and 1 is
			 * undefined. */
			ret = -EFAULT;
		return ret;
	case SNDCTL_DSP_GETOSPACE:
	{
		audio_buf_info abinfo;
		int available_entries = (TOTAL_AUDIO_FIFO_ENTRIES - NUM_FIFOENTRIES);
		abinfo.fragsize = 4;
		abinfo.fragstotal = abinfo.fragments = available_entries / 2;
		abinfo.bytes = available_entries * 2;

		if (abinfo.bytes > TOTAL_AUDIO_FIFO_ENTRIES * 2)
		{
			abinfo.bytes = TOTAL_AUDIO_FIFO_ENTRIES * 2;
			abinfo.fragstotal = abinfo.fragments = TOTAL_AUDIO_FIFO_ENTRIES / 2;
		}
		return copy_to_user((long *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;
	}
	case SNDCTL_DSP_GETODELAY:
	{
		int entries = NUM_FIFOENTRIES;
		int bytes;
		bytes = entries * 2; /* To get the si*/
		return put_user(bytes, (long *)arg);
	}
        case SNDCTL_DSP_SPEED:
                if (get_user(val, (long *)arg))
                        return -EFAULT;
                ret = slx200_set_sample_rate(val);
		if (ret)
			break;
		/* fall through */
	case SOUND_PCM_READ_RATE:
		return put_user(audio_samplerate, (long *)arg);
	case SOUND_PCM_READ_BITS:
	case SNDCTL_DSP_SETFMT:
	case SNDCTL_DSP_GETFMTS:
		/* we can do 16-bit only */
		return put_user(AFMT_S16_LE, (long *)arg);

	case SNDCTL_DSP_RESET:
		return 0;
	default:
		;
	}
	return ret;
}

static ssize_t slx200_audio_write(struct file *file, const char __user *buf,
			       size_t count, loff_t * ppos)
{
	int fifo_entries, free_fifo_in_bytes;
	unsigned long flags;

	spin_lock_irqsave(&lx200_lock, flags);

	
	if (count < 0 && count > BUFFER_SIZE)
	{
		printk("count too big\n");
		count = -EINVAL;
		goto out;
	}

	fifo_entries = NUM_FIFOENTRIES;
	free_fifo_in_bytes = (TOTAL_AUDIO_FIFO_ENTRIES - fifo_entries) * 2;
	
	if (count > free_fifo_in_bytes) {
		/* Make sure count is multiple of 4. */
		count = free_fifo_in_bytes & ~0x3;
	}
	
	if (copy_from_user(data_buffer, (void const*)buf, count)) {
		count = -EFAULT;
		goto out;
	}
	
	slx200_fifo_write(data_buffer, count);

out:	
	spin_unlock_irqrestore(&lx200_lock, flags);
	return count;
}

static int slx200_audio_open(struct inode *inode, struct file *file)
{
	if (file->f_mode & FMODE_READ)
		return -ENODEV;
	if (in_use)
		return -EBUSY;

	in_use = 1;

	return 0;
}

static int slx200_audio_release(struct inode *inode, struct file *file)
{
	INT_REG = 0x0;
	INT_MASK = 0x0;
	in_use = 0;
	return 0;
}

static struct file_operations dac_audio_fops = {
	.owner =        THIS_MODULE,
	.llseek =       no_llseek,
	.write =	slx200_audio_write,
	.ioctl =	slx200_audio_ioctl,
	.open =		slx200_audio_open,
	.release =	slx200_audio_release,
};

static int __init slx200_init(void)
{

	in_use = 0;

	/* Initialize the lock. */
	spin_lock_init(&lx200_lock);
        /* config DAC for playout */
	aic23_config_dac();	

	INT_MASK = 0x0;
	
	if (slx200_set_channels (2))
		return -1;
	
	if (slx200_set_sample_rate (AUDIO_RATE_DEFAULT) == -1)
		return -1;

	slx200_audio_mute (1);
	slx200_fifo_logic();
	slx200_audio_mute (0);
	slx200_set_volume (4);

	if ((device_major = register_sound_dsp(&dac_audio_fops, -1)) < 0) {
		printk(KERN_ERR "Cannot register dsp device");
		return device_major;
	}

	data_buffer = (char *)kmalloc(BUFFER_SIZE, GFP_KERNEL);

	if (data_buffer == NULL)
		return -ENOMEM;

	INT_MASK = 0x0;

	return 0;
}

static void __exit slx200_exit(void)
{
	I2S_START = 0;
	unregister_sound_dsp(device_major);
	kfree((void *)data_buffer);
}

module_init(slx200_init);
module_exit(slx200_exit);

MODULE_AUTHOR("Tensilica");
MODULE_DESCRIPTION("Audio driver for the LX200 board.");
MODULE_LICENSE("GPL");
