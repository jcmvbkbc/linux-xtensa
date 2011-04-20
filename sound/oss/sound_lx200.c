/*
 * sound/oss/sound_lx200.c
 *
 * Audio driver for playing a PCM bitstream over a DAC.
 * Designed to work on the LX200 and new LX110 FPGA Avnet boards.
 * Currently works on the LX200 but is silent on the LX110.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007-2010 Tensilica Inc.
 *
 * Dan Nicolaescu <dann@tensilica.com>
 * Pete Delaney <piet@tensilica.com>
 * Rajat Dani <Rajat.Dani@tensilica.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/errno.h>
#include <linux/sound.h>
#include <linux/soundcard.h>
#include <linux/interrupt.h>
#include <linux/list.h>

#include <asm/uaccess.h>
#include <asm/hardware.h>
#include <asm/io.h>

#include <asm/hardware.h>
#include <asm/platform.h>
#include <platform/hardware.h>
#include <asm/vectors.h>         		/* Necessary to see if variant workaround is needed */

#include <linux/module.h>       		/* Specifically, a module */
#include <linux/kernel.h>       		/* We're doing kernel work */
#include <linux/proc_fs.h>      		/* Necessary because we use the proc fs */
#include <linux/autoconf.h>     		/* Necessary because we use CONFIG #defines */ 
#include <asm/uaccess.h>        		/* for copy_from_user */

#include <asm/mxregs.h>


/*
 * The LX200 and LX110 assign interrupts a bit differently.
 * The LX200 generates different interrupts for FIFO over runs 
 * and when the FIFO falls below the low water mark (FIFO onstantsEVEL).
 * The LX110 combinds them onto the same IRQ.
 *
 * The FIFO LEVEL interrupt is assert whenever the
 * number of FIFO entries is  less than of equal to
 * the FIFO level.
 *
 * See:
 * 	<p4root>/Xtensa/Hardware/Barcelona/FPGA/rtl/I2S/Audio_regs.v.tpp
 * 		assign FifoLevelInt = (NumFifoEntries <= intFifoLevel) && intMask[1];
 *
 * Further, for SMP linux we have to increase the IRQ numbers by three
 * due to the interrupt distributer.
 *
 * These global IRQ numbers are established once for the board during
 * initialization.
 */
volatile int i2s_output_fifo_underrun_irq = -1;	/* I2S Output Underrun Interrupt */
volatile int i2s_output_fifo_level_irq = -1;	/* I2S Output FIFO Level (Low Water Mark) Interrupt */
volatile int i2s_input_fifo_underrun_irq = -1;	/* I2S Input Underrun Interrupt */
volatile int i2s_input_fifo_level_irq = -1;	/* I2S Input FIFO Level (Low Water Mark) Interrupt */

#define AUDIO_REQUEST_IRQ_FLAG 0

/* 
 * Interrupt Types - used by interrupt handler 
 * to select action to take
 */ 
enum i2s_irq {
	OUTPUT_FIFO_UNDERRUN = 1,
	OUTPUT_FIFO_LEVEL = 2,
	INPUT_FIFO_UNDERRUN = 3,
	INPUT_FIFO_LEVEL = 4,
	TIMER_DETECTED_OUTPUT_FIFO_LEVEL = 5
};	

						/* SPI (Serial Three Wire) Registers */
volatile char *spi_start = NULL;		/* Starts serial transfer on the SPI interface */
volatile char *spi_busy = NULL; 		/* Indicates that the SPI interface is busy */
volatile int  *spi_data = NULL; 		/* Date to be written to the SPI interface */

/*
 * OpenCores.Org I2S Registers:
 * See:
 *     https://www.hq.tensilica.com/eweb/grp_fpga/IPBlocks/i2s.pdf
 */
				
						/* 		       I2S Transmitter Registers 				   */
						/* ------------------------------------------------------------------------------- */
volatile int  *i2s_TxVersion = NULL; 		/* Version Parameters used to Generate Transmitter (ADDR_WITDH, Master/Slave) */
volatile int  *i2s_TxConfig = NULL; 		/* Config Register Controls Operation of Transmitter (Sample Resolution, ... */
volatile int  *i2s_TxIntMask = NULL;		/* Interrupt Mask to enable Interrupt Source (Higher|Lower Buffer Empty) */
						/* Bit 0 when '0' masks the FIFO underrun interrupt ... */
						/* Bit 1 when '0' masks the FIFO level interrupt */

volatile int  *i2s_TxIntStat = NULL;		/* Interrupt Status: Bits set to one when Event Occures (Higher|Lower Buffer Empty) ... */
						/* ... On the LX200 only the Underrun bit is set, only the interrupt indicates the LEVEL */
volatile int  *i2s_out_data_0 = NULL; 		/* Data to be written out to the I2S interface on Channel 0 (Left and Right) */
volatile int  *i2s_out_data_1 = NULL; 		/* Data to be written out to the I2S interface on Channel 1   "         "    */
volatile int  *i2s_out_data_2 = NULL; 		/* Data to be written out to the I2S interface on Channel 2   "         "    */
volatile int  *i2s_out_data_3 = NULL; 		/* Data to be written out to the I2S interface on Channel 3   "         "    */
volatile char *i2s_TxStart = NULL; 		/* Begins serial transfer to the I2S inteface; On LX200, Done via i2s_TxConfig on LX110 */
volatile int  *int_reg = NULL; 			/* I2S interface FIFO Underrun Interrupt */
volatile int  *int_fifolevel = NULL; 		/* Level at which FIFO should trigger an Interrupt */
volatile int  *num_fifoentries = NULL;  	/* Indicates the number of entries occupied in the I2S FIFO */

						/* 		I2s receive registers                  */
						/* --------------------------------------------------- */
volatile int  *i2s_RxVersion = NULL;          	/* Version Register (RO) */
volatile int  *i2s_RxConfig = NULL;          	/* Config Register (RW) */
volatile int  *i2s_RxIntMask = NULL;          	/* I2S Mask Register to Enable Interrupt Source (RW) */
volatile int  *i2s_RxIntStat = NULL;          	/* Bits set to 1 when an Event Occures (RW) */
volatile int  *i2s_RxData = NULL;          	/* Data Read From the I2S Interface (RO) */ 

/* 
 * OpenCores I2C (Serial Two Wire) Registers:
 * See:
 *	https://www.hq.tensilica.com/eweb/grp_fpga/IPBlocks/i2c_specs.pdf
 *	http://en.wikipedia.org/wiki/I2c
 */
volatile unsigned *i2c_rPRERlo = NULL;		/* Clock PREscale Register lo-byte */	
volatile unsigned *i2c_rPRERhi = NULL;		/* Clock PREscale Register hi-byte*/	
volatile unsigned *i2c_rCTR = NULL;		/* Control Register */	
volatile unsigned *i2c_rTXR = NULL;		/* Tranmit Register */	
volatile unsigned *i2c_rRXR = NULL;		/* Receive register */	
volatile unsigned *i2c_rCR = NULL;		/* Command register */	
volatile unsigned *i2c_rSR = NULL;		/* Status register */	

/*
 * MACROS to dereference global pointers to registers.
 * These register pointers are assigned dynamicaly
 * during initialization based on the Avnet board
 * being used.
 *
 * Likely should be removed. Neithor Dan nor Piet
 * think this is likely a good idea, thought it is convenient
 * that the value of the register is easily displayed
 * with ddd by putting the mouse on top of the code.
 *
 * The parentheses are necessary for statements like this to work:
 * 	I2S_TX_INT_MASK &= ~0x2;
 */
#define SPI_START	(*spi_start)
#define SPI_BUSY	(*spi_busy)
#define SPI_DATA	(*spi_data)
#define I2S_TX_START	(*i2s_TxStart)

#define I2S_TX_VERSION	(*i2s_TxVersion)
#define I2S_TX_CONFIG	(*i2s_TxConfig)
#define I2S_TX_INT_MASK	(*i2s_TxIntMask)
#define I2S_TX_INT_STAT	(*i2s_TxIntStat)
#define I2S_OUT_DATA_0	(*i2s_out_data_0)
#define I2S_OUT_DATA_1	(*i2s_out_data_1)
#define I2S_OUT_DATA_2	(*i2s_out_data_2)
#define I2S_OUT_DATA_3	(*i2s_out_data_3)
#define INT_FIFOLEVEL	(*int_fifolevel)
#define NUM_FIFOENTRIES	(*num_fifoentries)

#define I2S_RX_VERSION	(*i2s_RxVersion)
#define I2S_RX_CONFIG	(*i2s_RxConfig)
#define I2S_RX_INT_MASK	(*i2s_RxIntMask)
#define I2S_RX_INT_STAT	(*i2s_RxIntStat)
#define I2S_RX_DATA	(*i2s_RxData)

#define rPRERlo		(*i2c_rPRERlo)
#define rPRERhi		(*i2c_rPRERhi)
#define rCTR		(*i2c_rCTR)
#define rTXR		(*i2c_rTXR)
#define rRXR		(*i2c_rRXR)
#define rCR 		(*i2c_rCR)
#define rSR		(*i2c_rSR)

/*
 * I2S Transmitter Config Register Shifts to Bit Positions
 * See section 5.2.3.
 */
#define TXCONFIG_CHAN_EN            28		/* Only documented in XT-AV110 Spec, ... */
#define TXCONFIG_FIFO_LEVEL_INT     24		/* ... not documented in i2s spec. */
#define TXCONFIG_RES                16
#define TXCONFIG_RATIO              8
#define TXCONFIG_TSWAP              2
#define TXCONFIG_TINTEN             1
#define TXCONFIG_TXEN               0


/* ------------- Clock synthesizer --------------- */
/* I2C address */
#define CLK_SYN_I2C_ADDR                    0xD2
/* Internal registers */
#define CLK_SYN_BASE_OSCILLATOR_M_REG       0x84
#define CLK_SYN_BASE_OSCILLATOR_N_L_REG     0x85
#define CLK_SYN_BASE_OSCILLATOR_N_H_REG     0x86

int slx200_initialized = 0;

/*
 * These two globals are set when an Underrun or LEVEL interrupt occure.
 * They are cleared and used by the write function to do flow control
 * witht the FIFO.
 */
int i2s_tx_below_level = 0;			/* Set when a LEVEL interrupt occurs */
int i2s_tx_active = 0;				/* Cleared when a UnderRun interrupt occurs */

void break_on(void) { 
	// asm("break 7,15");			/* Didn't seem to work */
}

#define BREAK_ON(event) {	\
	if (event)	    	\
		break_on();   	\
				\
	BUG_ON(event);		\
}

#define ENTRY_BYTE_SIZE			2	/* 16 bit Audio; NB: HDMI will be 32 bit data (4 byte) */
#define ENTRY_CHANNELS			2	/* Two Channels per i2s FIFO */


#define CODEC_NAME		 	"sound_lx200"

/* 
 * HiWater Mark at 50% of total FIFO entries, so the
 * largest Audio Buff we can process during an interrupt
 * without potentially overflowing is also 50% of the
 * size of the FIFO.
 *
 * Allocating lots of buffers but they are only allocated while
 * /dev/dsp is open and music is being played. Allocating
 * enough buffers so audio application can run at normal
 * priority and tempory priority pauses doesn't effect
 * the audio.
 *
 * The following constants are calculated calcuatated during
 * module initialization and based to the FIFO size; which
 * currently appears to be different for the LX110 and LX200.
 */
#define LX200_TOTAL_AUDIO_FIFO_ENTRIES 	(8192 * 4)
#define LX110_TOTAL_AUDIO_FIFO_ENTRIES 	(4096)						/* GOT Bus errors with 8192 */

int total_audio_fifo_entries = -1;	/* LX200_TOTAL_AUDIO_FIFO_ENTRIES or LX110_TOTAL_AUDIO_FIFO_ENTRIES */
int low_water_mark = -1;		/* Calc constant during initialization; currently 50% of total_audio_fifo_entries */
int entries_above_low_water_mark = -1;	/* total_audio_fifo_entries - low_water_mark */
int bytes_above_low_water_mark = -1;	/* entries_above_low_water_mark  * 2 */
int buffer_size = -1;			/* bytes_above_low_water_mark */

#define NUM_BUFFERS			30
#define AUDIO_RATE_DEFAULT       	44100
#define BIT_RESOLUTION			16

/* Codec parameters */
#define SAMPLERATE          		48000

struct audio_buff {
	struct list_head  list;
	char 		  *data;	/* Begining of unconsumed data in the Buffer */
	unsigned int 	   len;		/* Length of Data in the Buffer */
	unsigned int 	   max_len;	/* Length of the Buffer */
	int		   instance;	/* Debugging aid */
	int		   line;	/* Debugging aid */
};
LIST_HEAD(free_audio_buffs);		/* Head of Audio Buffs available for data */
LIST_HEAD(full_audio_buffs);		/* Head of buffer ready for interrupt handler ...
					   ... to try sending to FIFO */

static DECLARE_WAIT_QUEUE_HEAD(free_audio_bufs);/* Writers waiting for free audio buffers */

static spinlock_t lx200_lock;

static int in_use; 			/* Used for exclusive access to the device. */
static int hifi_workaround_enabled = 1;	/* Enables workaround for interrupt distributer configured correctly */
static int interrupt_expected;
static int device_major;
static int audio_channels = 2;		/* Linux considers Left and Right as seperate channels ... 
					   the i2s considers Left and Right combined into 1 queue 
					   which we (unfortunaley) are calling channels */

#ifdef CONFIG_DEBUG_KERNEL
#define dprintk printk
/*
 * Congestion Monitoring:
 *   Provides a console log displaying congestion problems
 *   while playing music. Eats about 45% of a CPU in a 
 *   kernel compiled -O0.
 *
 *   The Console Log Looks like this:
 *  
 *   **************+.......=*******+.......=*******+.......=*******+.......=*******+..
 *   .....=*******+.......=*******+.......=*******+.......=*******+.......=*******+...
 *   ....=*******+.......=*******+.......=*******+.......=*******+.......=*******+....
 *   ...=*******+.......=*******+.......=*******+.......=*******+.......=*******+.....
 *   ..=*******+.......=*******+.......=*******+.......=*******+.......=*******+......
 *   .=*******+.......=*******+.......=*******+.......=*******+.......=*******+.......
 *   =*******+.......=*******+.......=*******+.......=*******+.......=*******+.......=
 *
 * or more recienty with large buffers; Ex: dd if=*.wav of=/dev/dsp bs=50000
 * 
 * .!***..**.**.*+.=*.*+.=*.*+.=*.*+.=*.*+.=*+.=*+.=*.*+.=*.*+.=*.*+.=*.*+.=*+.=*+.=
 * *.*+.=*.*+.=*.*+.=*.*+.=*.*+.=*+.=*+.=*.*+.=*.*+.=*.*+.=*.*+.=*+.=*+.=*.*+.=*.*+.
 * =*.*+.=*.*+.=*.*+.=*+.=*+.=*.*+.=*.*+.=*.*+.=*.*+.=*+.=*+.=*.*+.=*.*+.=*.*+.=*.*+
 * .=*+.=*+.=*.*+.=*.*+.=*.*+.=*.*+.=*.*+.=*+.=*+.=*.*+.=*.*+.=*.*+.=*.*+.=*+.=*+.=*
 * .*+.=*.*+.=*.*+.=*.*+.=*+.=*+.=*.*+.=*.*+.=*.*+.=*.*+.=*.*+.=*+.=*+.=*.*+.=*.*+.=
 *
 * or with small buffers; Ex: cat *.wav > /dev/dsp
 * ........*........***************................***************........**********
 * *+........=****........************+........=***........*************+........=**
 * ........**************+........=*........***************........********+........
 * =*******........*********+........=******........**********+........=*****.......
 * .***********+........=****........************+........=***........*************+
 *
 *   Where:
 *       '+' Ran out of free audio buffers, ie: all of the full audio buffers are full; that's good.
 *       '=' After running out of free audio buffers, sleeping, and being woken, ...
 *           ... we are still above the low water mark; that's good.
 *       '*' writer got a audio buffer and is about to fill it.
 *       '.' interrupt handler put a audio buff on the FIFO.
 *       '^' Looks like we would have gone over the top of the FIFO on the LX110.
 *       '#' Got an FIFO Underrun; that's VERY BAD.
 */
#define CM_LINE_SIZE                80
#define CM_BUFFER_SIZE            2000
#define CM_BUFFER_SIZE_LOW_WATER   160	/* Print at this level if interruptable */
#define CM_BUFFER_SIZE_HIGH_WATER 1500	/* Print at this level in all cases */

int cm_enabled = 0;			/* Set non-zero to enable event trace */
int cm_immediate_flush_enabled = 0;
int cm_bytes_on_line = 0;
int cm_bytes_in_buffer = 0;
char cm_buffer[CM_BUFFER_SIZE];

#define CM_PUTC(c) cm_putc(c)
#define CM_FLUSH() cm_flush()

void static cm_flush(void) {
	if (cm_enabled) {
		cm_buffer[cm_bytes_in_buffer++] = ' ';
		cm_buffer[cm_bytes_in_buffer++] = '<';
		cm_buffer[cm_bytes_in_buffer++] = 'E';
		cm_buffer[cm_bytes_in_buffer++] = 'O';
		cm_buffer[cm_bytes_in_buffer++] = 'F';
		cm_buffer[cm_bytes_in_buffer++] = '>';
		cm_buffer[cm_bytes_in_buffer++] = '\n';
		cm_buffer[cm_bytes_in_buffer++] = '\000';
		printk("%s\n", &cm_buffer[0]);
		cm_bytes_in_buffer = 0;
		cm_bytes_on_line = 0;
	}
}	
	

void static cm_putc(char c) {
	if (cm_enabled) {
		BUG_ON(cm_bytes_in_buffer < 0);
		BUG_ON(cm_bytes_in_buffer > CM_BUFFER_SIZE);
		if (cm_immediate_flush_enabled) {
			printk("%c", c);
		} else {
			cm_buffer[cm_bytes_in_buffer++] = c; 
		}
		if (cm_bytes_on_line++ >= CM_LINE_SIZE) {
			cm_bytes_on_line = -1;
			cm_putc('\n');
			if (cm_bytes_in_buffer >= CM_BUFFER_SIZE_LOW_WATER) {
				if ((!in_interrupt() && !irqs_disabled()) || 
				     (cm_bytes_in_buffer >= CM_BUFFER_SIZE_HIGH_WATER)) {
					cm_buffer[cm_bytes_in_buffer++] = '\000';
					printk("%s\n", &cm_buffer[0]);
					cm_bytes_in_buffer = 0;
					cm_bytes_on_line = 0;
				}
			}
		}
			
	}
}
#else
#define dprintk(...)
#define CM_PUTC(...)
#define CM_FLUSH()
#endif

void slx200_set_board_specific_constants(void)
{
	switch(platform_board) {

	case AVNET_LX200:
		spi_start =       (volatile char *)(XSHAL_IOBLOCK_BYPASS_VADDR+0x0D070000);
		spi_busy =        (volatile char *)(XSHAL_IOBLOCK_BYPASS_VADDR+0x0D070004);
		spi_data =        (volatile int *) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0D070008);

		i2s_TxStart  =    (volatile char *)(XSHAL_IOBLOCK_BYPASS_VADDR+0x0D07000C);
		i2s_out_data_0 =  (volatile int *) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0D070010);
		i2s_TxIntStat =   (volatile int *) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0D070014);
		int_fifolevel =   (volatile int *) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0D070018);
		num_fifoentries = (volatile int *) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0D07001C);
		i2s_TxIntMask =   (volatile int *) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0D070020);

#if defined(CONFIG_ARCH_HAS_SMP)
		/* Interrupt Distributer shifts IRQ's by three */
		i2s_output_fifo_underrun_irq = XCHAL_EXTINT5_NUM;
		i2s_output_fifo_level_irq = XCHAL_EXTINT6_NUM;
		i2s_input_fifo_underrun_irq = XCHAL_EXTINT7_NUM;
		i2s_input_fifo_level_irq = XCHAL_EXTINT8_NUM;
#else
		i2s_output_fifo_underrun_irq = XCHAL_EXTINT2_NUM;
		i2s_output_fifo_level_irq = XCHAL_EXTINT3_NUM;
		i2s_input_fifo_underrun_irq = XCHAL_EXTINT4_NUM;
		i2s_input_fifo_level_irq = XCHAL_EXTINT5_NUM;
#endif
		total_audio_fifo_entries 	= LX200_TOTAL_AUDIO_FIFO_ENTRIES;
		low_water_mark 			= (total_audio_fifo_entries/2);
		INT_FIFOLEVEL                   = low_water_mark;					/* Set Low Water to 50% of FIFO */
		entries_above_low_water_mark    = (total_audio_fifo_entries - low_water_mark);
	        bytes_above_low_water_mark      = (entries_above_low_water_mark * 2);
		buffer_size                     = bytes_above_low_water_mark;

		printk("%s: XCHAL_CODE_ID: '%s'\n", __func__, XCHAL_CORE_ID);
		if (strcmp(XCHAL_CORE_ID, "test_mmuhifi_c3") == 0) {
			printk("%s: Enableing workaround for the test_mmuuhifi Variant\n", __func__);
			hifi_workaround_enabled = 1;
		}
		
		
		slx200_initialized++;
		break;

	case AVNET_LX110:
				    /* I2S Output Register Mappings */
		i2s_TxVersion =     (volatile int *) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0D080000);
		i2s_TxConfig =      (volatile int *) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0D080004);
		i2s_TxIntMask =     (volatile int *) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0D080008);
		i2s_TxIntStat =     (volatile int *) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0D08000C);
		i2s_out_data_0 =    (volatile int *) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0D088010);
		i2s_out_data_1 =    (volatile int *) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0D088014);
		i2s_out_data_2 =    (volatile int *) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0D088018);
		i2s_out_data_3 =    (volatile int *) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0D08801C);

				    /* I2S Input Register Mappings */
		i2s_RxVersion =     (volatile int *) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0D088000);
		i2s_RxConfig =      (volatile int *) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0D088004);
		i2s_RxIntMask =     (volatile int *) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0D088008);
		i2s_RxIntStat =     (volatile int *) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0D08800C);
		i2s_RxData =        (volatile int *) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0D088010);

				     /* I2C Master Register Mappings */
		i2c_rPRERlo =	    (volatile unsigned *)(XSHAL_IOBLOCK_BYPASS_VADDR+0x0D090000);
		i2c_rPRERhi =	    (volatile unsigned *)(XSHAL_IOBLOCK_BYPASS_VADDR+0x0D090004);
		i2c_rCTR =	    (volatile unsigned *)(XSHAL_IOBLOCK_BYPASS_VADDR+0x0D090008);
		i2c_rTXR =	    (volatile unsigned *)(XSHAL_IOBLOCK_BYPASS_VADDR+0x0D09000C);
		i2c_rRXR =	    (volatile unsigned *)(XSHAL_IOBLOCK_BYPASS_VADDR+0x0D09000C);
		i2c_rCR =	    (volatile unsigned *)(XSHAL_IOBLOCK_BYPASS_VADDR+0x0D090010);
		i2c_rSR =	    (volatile unsigned *)(XSHAL_IOBLOCK_BYPASS_VADDR+0x0D090010);

				    /* SPI Register Mappings */
		spi_start =         (volatile char *)(XSHAL_IOBLOCK_BYPASS_VADDR+0x0D0A0000);
		spi_busy =          (volatile char *)(XSHAL_IOBLOCK_BYPASS_VADDR+0x0D0A0004);
		spi_data =          (volatile int *) (XSHAL_IOBLOCK_BYPASS_VADDR+0x0D0A0008);

#if defined(CONFIG_ARCH_HAS_SMP)
		/* Interrupt Distributer shifts IRQ's by three */
		i2s_output_fifo_underrun_irq = XCHAL_EXTINT5_NUM;
		i2s_output_fifo_level_irq = XCHAL_EXTINT5_NUM;
		i2s_input_fifo_underrun_irq = XCHAL_EXTINT6_NUM;
		i2s_input_fifo_level_irq = XCHAL_EXTINT6_NUM;
#else
		i2s_output_fifo_underrun_irq = XCHAL_EXTINT2_NUM;
		i2s_output_fifo_level_irq = XCHAL_EXTINT2_NUM;
		i2s_input_fifo_underrun_irq = XCHAL_EXTINT3_NUM;
		i2s_input_fifo_level_irq = XCHAL_EXTINT3_NUM;
#endif

		total_audio_fifo_entries = LX110_TOTAL_AUDIO_FIFO_ENTRIES;
		low_water_mark 			= (total_audio_fifo_entries/2);
		entries_above_low_water_mark    = (total_audio_fifo_entries - low_water_mark);
		bytes_above_low_water_mark      = (entries_above_low_water_mark * 2);
		buffer_size                     = bytes_above_low_water_mark;

		slx200_initialized++;
		break;

	case AVNET_LX60:
	default:
		break;

	}
}

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

	if (regno != AIC23_RESET) {
		aic23_regs[regno] = data;
	}

	data = (regno<<9)|data;
	SPI_DATA = data;	/* write data */
	SPI_START = 1;

	for (i = 0; i < 50; i++)
	  asm ("");	/* wait for write to complete */

	while(SPI_BUSY)
		dummy++;

	SPI_START=0;
}

static void aic23_config_dac(void)
{
	SPI_START = 0;
	memset(aic23_regs, 0, sizeof(aic23_regs));

	/*
 	 * On the LX200 we have crystal clock on the board
 	 * whereas on the LX110 the FPGA RTL provides the
 	 * clock synthesizer.
 	 *
 	 * On the LX200 audio sampling had aliasing
 	 * issues with the data phase delays between the
 	 * the daughter board and the FPGA.
 	 */
	switch (platform_board) {

	case AVNET_LX200:
		aic23_write_reg(AIC23_RESET,      0x000); 	/* reset AIC23 */
		aic23_write_reg(AIC23_LEFTINVOL,  0x017); 	/* enabling left input */
		aic23_write_reg(AIC23_RIGHTINVOL, 0x017); 	/* enabling right input */
		aic23_write_reg(AIC23_ANAPATH,    0x014); 	/* analog audio path - enable DAC */
		aic23_write_reg(AIC23_DIGPATH,    0x004); 	/* digital audio path - default val */
		aic23_write_reg(AIC23_POWERDOWN,  0x000); 	/* power down control - enable clock */
		aic23_write_reg(AIC23_DIGIF,      0x062);	/* digital aud interface format - MASTER mode / LRSWAP */
		aic23_write_reg(AIC23_SAMPLERATE, 0x023);	/* sample rate control - USB clock mode */
		aic23_write_reg(AIC23_DIGACT,     0x001);
		break;

	case AVNET_LX110:
		aic23_write_reg(AIC23_RESET,      0x000); 	/* reset AIC23 */
		aic23_write_reg(AIC23_LEFTINVOL,  0x017); 	/* enabling left input */
		aic23_write_reg(AIC23_RIGHTINVOL, 0x017); 	/* enabling right input */
		aic23_write_reg(AIC23_ANAPATH,    0x012); 	/* analog audio path - enable DAC */
		aic23_write_reg(AIC23_DIGPATH,    0x004); 	/* digital audio path - default val */
		aic23_write_reg(AIC23_POWERDOWN,  0x007); 	/* power down control - enable clock */
		aic23_write_reg(AIC23_DIGIF,      0x022);	/* digital aud interface format - SLAVE mode / LRSWAP */
		aic23_write_reg(AIC23_SAMPLERATE, 0x000);	/* sample rate control - NORMAL clock mode */
		aic23_write_reg(AIC23_DIGACT,     0x001);
		break;

	default:
		BUG();
	}
}

static void aic23_set_vol(int vol)
{
	/* vol supported range 0-10 */
        /* actual value is 48 to 127 (79 steps) */
	aic23_write_reg(AIC23_LEFTHPVOL,	vol);
	aic23_write_reg(AIC23_RIGHTHPVOL,	vol);
}

static int aic23_get_vol(void)
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

	case 44100:	/* DEFAULT */
		aic23_write_reg(AIC23_SAMPLERATE,	0x023);
		break;

#if 0
		/* 
		 * The rest are Not Supported by the LX110 yet.
		 */
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
#endif

	default:
		retval = -1;
		break;
	}

	return retval;
}

/***************************************************************
 * Begin I2C functions:
 **************************************************************/

void i2c_init(unsigned int busfreq, unsigned int i2cfreq)
{
  unsigned int divisor;
  char upper, lower;

  /*
   * Prescale = (wb_clk_i / (5 * SCL) - 1; See Section 3.2.1.
   * The core uses 5*SCL intermally.
   */  
  divisor = (busfreq / (5 * i2cfreq)) - 1;
  lower = divisor & 0xff;
  upper = (divisor & 0xff00) >> 8;

  rPRERlo = lower;		/* Set Clock PREscale Register lo-byte */
  rPRERhi = upper;		/*                             hi-byte */

  /*
   * Set Control Reigster to enable EN Core but not Interrupt.
   *   Bit #6 IEN Core INTERRUPT Enable (0X40)
   *   Bit #7 EN  Core Enable (0X80)
   *   Section 3.2.2.
   */
  rCTR = 0x80;
}
    
void i2c_write_addr(char addr, unsigned int read) 
{
  addr = (addr & 0xfe) | (read);
  rTXR = addr;
  rCR = 0x90;			/* Command Register */

  while (rSR & 0x2) {
  	// Wait until transfer complete
  }
  while (rSR & 0x80){
  	// Wait for ACK
  }
}

void i2c_write_data(char data) 
{
  rTXR = data;
  rCR = 0x10;			/* Command Register = WR, write to slave */
 
  // Wait until TIP (Transfer In Progess) completes.
  while (rSR & 0x2){}

  // Wait for RxACK (Bit:7) to clear, 
  // this represents ackknowlege from the addresses slave.
  while (rSR & 0x80){}
}

char i2c_read_data(void)
{
  /*
   * Set  Command Register Bits:
   *     Bit 5: Read from slave:0X20
   *     Bot 3: Send NACK:0x8 
   *     Section 3.2.5.
   */
  rCR = 0x28;

  // Wait until transfer complete
  while (rSR & 0x2){}
  return (rRXR);
}

void i2c_stop(void)
{
  /*
   * Set bit 6 of Command Register:
   *    STO, generate stop condition.
   *    Cleared Automatically.
   */
  rCR = 0x40;

  // Wait until stopped
  // Wiit for Bit:6 I2C bus busy to clear
  //    1 after START signal detected.
  //    0 after STOP signal detected.
  while (rSR & 0x40){}  
}

/***************************************************************
 * End I2C functions 
 **************************************************************/



/****************************************************************
 * Begin Clock Synthesizer functions
 ****************************************************************/
void writeClkSynReg(char addr, char data) {

  i2c_write_addr (CLK_SYN_I2C_ADDR,0x0);
  i2c_write_data (addr);
  i2c_write_data (data);
  i2c_stop();  
}

int clk_syn_init(int aud_samp_freq)
{
    int ref_divider_M;
    int fb_divider_N_L;
    int fb_divider_N_H;
    int bus_freq = 50000000;
    int i2c_freq = 100000;

    /* Initialize */
    i2c_init (bus_freq, i2c_freq);  
    
    /* Audio clock setup */ 
    switch(aud_samp_freq) {
        case 48000: 			/* 48000 sampling freq => 12.288MHz Audio clock */
            ref_divider_M = 0xE1;
            fb_divider_N_L = 0x00;
            fb_divider_N_H = 0xE8;
            break;

        case 44100: 			/* 44100 sampling freq => 11.2896MHz Audio clock */
            ref_divider_M = 0x77;
            fb_divider_N_L = 0x20;
            fb_divider_N_H = 0xED;
            break;

        default:
            printk("%s: Audio sampling frequncy %d not supported!\n", __func__, aud_samp_freq);
            return -1;
    }    

    /* Set reference divider M */
    writeClkSynReg(CLK_SYN_BASE_OSCILLATOR_M_REG, ref_divider_M);

    /* Set feedback divider N lower bits */
    writeClkSynReg(CLK_SYN_BASE_OSCILLATOR_N_L_REG, fb_divider_N_L);

    /* Set feedback divider N higher bits */
    writeClkSynReg(CLK_SYN_BASE_OSCILLATOR_N_H_REG, fb_divider_N_H);

    return 0;
}
/**************************************************************************
 * End Clock Synthesizer functions 
 ***************************************************************************/


/************************************************************************** 
 * Begin I2S functions 
 **************************************************************************/

static void i2s_tx_init(unsigned char resolution, unsigned int sample_freq, unsigned int bus_freq, int fifoIntLevel)
{
  unsigned int ratio;
  unsigned int config;
  int intlevel = fifoIntLevel & 0xf;

  ratio = (bus_freq - (sample_freq * resolution * 8)) / (sample_freq * resolution * 4);
  config = I2S_TX_CONFIG;

  I2S_TX_CONFIG = 
    (config & 0xf0c000ff) |
    intlevel << TXCONFIG_FIFO_LEVEL_INT |
    resolution << TXCONFIG_RES |
    ratio << TXCONFIG_RATIO
#if 0
    | 1 << TXCONFIG_TINTEN	/* Enables Audio Interupts */
    | 1 << TXCONFIG_TXEN
#endif
    ;		
}

/*
 * channels_mask:
 * 	1: channel 0 (Left & Right)
 * 	2: channel 1   "       "
 * 	4: channel 3   "       "	
 * 	8: channel 4   "       "
 */
static void i2s_tx_start(unsigned char channels_mask)
{
  unsigned int config;

  switch(platform_board) {

 case AVNET_LX200:
	I2S_TX_START = 1;	/* Only can start channel 0 (L&R) */
	break;

  case AVNET_LX110:
  	config = (I2S_TX_CONFIG & 0x0fffffff) | channels_mask << TXCONFIG_CHAN_EN;
  	I2S_TX_CONFIG = config | 0x1;
	break;

  default:
	BUG();
  }
}

static void i2s_tx_stop(void)
{
  unsigned int config;
 
  switch(platform_board) {

  case AVNET_LX200:
	I2S_TX_START = 0;       /* Only can stop channel 0 (L&R) */ 
	break;

  case AVNET_LX110:
  	config = I2S_TX_CONFIG;
  	I2S_TX_CONFIG = config & 0xfffffffe;
	break;

  default:
	BUG();
  }
}


#if 0
/* Not currently used */
static void init_i2s_fifo(void)
{
	int i;

	// stop i2s logic
	i2s_tx_stop();

	//start i2s logic
	i2s_tx_start(0x1);

	for(i = 0; i < total_audio_fifo_entries; i++) {
		I2S_OUT_DATA_0 = 0;				/* Right */
		I2S_OUT_DATA_0 = 0;				/* Left  */
#if 0		
		if (platform_board == AVNET_LX110) {
			I2S_OUT_DATA_1 = 0;			/* Right */
			I2S_OUT_DATA_1 = 0;			/* Left  */

			I2S_OUT_DATA_2 = 0;			/* Right */
			I2S_OUT_DATA_2 = 0;			/* Left  */

			I2S_OUT_DATA_3 = 0;			/* Right */
			I2S_OUT_DATA_3 = 0;			/* Left  */
		}
#endif
	}
}
#endif


static void slx200_fifo_logic(void)
{
	i2s_tx_stop();
	i2s_tx_start(0x1);	// channels_mask:0x1 [Just_Channel_0]
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

int show_extern_irq_enabled = 0;

void slx200_audio_show_extern_irq(const char *func, const char *context)
{
#ifdef CONFIG_ARCH_HAS_SMP
	if (show_extern_irq_enabled) {
		int i;
		int interrupt = get_sr(INTERRUPT);
		int intenable = get_sr(INTENABLE);
		int mieng = get_er(MIENG);
		int miasg = get_er(MIASG);
		int mirout;
	
		printk("%s%s: interrupt: 0x%x, intenable: 0x%x\n", func, context, interrupt, intenable);
		printk("%s%s: MIENG: 0x%x, MSASG: 0x%x\n", func, context, mieng, miasg);
		printk("%s%s: MIROUT: [ ", func, context);
		for(i=0; i < XCHAL_NUM_INTERRUPTS; i++) {
			mirout = get_er(MIROUT(i));
			printk("0x%x ", mirout);
		}
		printk("]\n");
	}
#endif
}

/* 
 * Write the array of 16 bit audio data to the audio FIFO. 
 * For Mono we push the same two bytes to the FIFO twice,
 * once for each stereo channel.  For Stereo we push a total
 * of four bytes to two FIFO entries (two bytes to each channel).
 */
static int slx200_fifo_write(signed char *data, int byte_count)
{
	int i;
	short int *play_buf;

	play_buf = (short *) data;

	/*
	 * 'audio_channels' is the Linux std, of stero being 2 channels.
	 */ 
	if (audio_channels == 1) {
		for (i = 0; i < byte_count; i += 2) {	
			short int audio_data = *play_buf++;
			/* For Mono, write the same thing on both
			 * channels. */
			I2S_OUT_DATA_0 = audio_data;
			I2S_OUT_DATA_0 = audio_data;
		}
	}
	else {
		for (i = 0; i < byte_count; i += 4) {
			I2S_OUT_DATA_0 = *play_buf++;
			I2S_OUT_DATA_0 = *play_buf++;
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
	aic23_set_vol(vol_level);

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
		unsigned long flags;
		audio_buf_info abinfo;
		int available_entries = 0;

		spin_lock_irqsave(&lx200_lock, flags);
		if (!list_empty(&free_audio_buffs)) {
			struct audio_buff *buf;

			list_for_each_entry(buf, &free_audio_buffs, list) {
				available_entries += (buf->len/2);
			}
		}
		spin_unlock_irqrestore(&lx200_lock, flags);

		switch(platform_board) {
		case AVNET_LX200:
			available_entries += (total_audio_fifo_entries - NUM_FIFOENTRIES);
			break;

		case AVNET_LX110:
			if (i2s_tx_below_level)
				available_entries += (total_audio_fifo_entries/2);
			break;

		default:
			break;
		}
		abinfo.fragsize = 4;
		abinfo.fragstotal = abinfo.fragments = available_entries / 2;
		abinfo.bytes = available_entries * 2;
#if 0
		if (abinfo.bytes > total_audio_fifo_entries * 2) {
			abinfo.bytes = total_audio_fifo_entries * 2;
			abinfo.fragstotal = abinfo.fragments = total_audio_fifo_entries / 2;
		}
#endif
		return copy_to_user((long *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;
	}

	case SNDCTL_DSP_GETODELAY:
	{
		int available_entries = 0;
		unsigned long flags;
		int bytes;	

		spin_lock_irqsave(&lx200_lock, flags);
		if (!list_empty(&full_audio_buffs)) {
			 struct audio_buff *buf;
	
			list_for_each_entry(buf, &full_audio_buffs, list) {
				 available_entries += (buf->len/2);
			}
		}
		switch(platform_board) {
		case AVNET_LX200:
			available_entries += NUM_FIFOENTRIES;
			break;

		case AVNET_LX110:
			if (!i2s_tx_below_level)
				available_entries += (total_audio_fifo_entries/2);
			break;

		default:
			BUG();
		}
		spin_unlock_irqrestore(&lx200_lock, flags);

		bytes = (available_entries) * 2;
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

static struct timer_list audio_timer;
static irqreturn_t i2s_output_interrupt(int, void *);

void slx200_audio_timer(unsigned long data)
{
	struct timer_list *tmr = &audio_timer;
	unsigned long flags;

	spin_lock_irqsave(&lx200_lock, flags);

	if (in_use ) {
		int delay;

		if (NUM_FIFOENTRIES <= INT_FIFOLEVEL) {
			if (!list_empty(&full_audio_buffs)) {
				CM_PUTC('T');
				delay = 2;
			} else {
				CM_PUTC('?');
				delay = 1;
			}
			i2s_output_interrupt(i2s_output_fifo_level_irq, (void *)TIMER_DETECTED_OUTPUT_FIFO_LEVEL);
		} else {
			CM_PUTC('t');
			delay = 4;
		}	
		tmr->expires = jiffies + delay;
		tmr->data = (unsigned long) 0;
		tmr->function = slx200_audio_timer;
		add_timer(tmr);
	} else {
		CM_PUTC('Z');
	}

	spin_unlock_irqrestore(&lx200_lock, flags);
}

/*
 * cat audio_file.pcm > /dev/dsp will send down 4k blocks,
 * but mplayer will send down 50k blocks. 
 */
int slx200_audio_write_printk_enabled = 0;
static ssize_t slx200_audio_write(struct file *file, const char __user *initial_user_buf,
			       size_t initial_count, loff_t * ppos)
{
#ifdef CONFIG_DEBUG_KERNEL
	int fifo_entries, fifo_level, free_fifo_in_bytes;
#endif
	char __user *user_buf = (char *) initial_user_buf;
	size_t bytes_left = initial_count;
	size_t copied = 0;
	size_t written = 0;
	unsigned long flags;
	struct audio_buff *buf;
	int err = 0;

	CM_PUTC('W');


	if (bytes_left < 0 && bytes_left > buffer_size) {
		printk("count too big\n");
		err = -EINVAL;
		goto err_out;
	}

	/*
	 * Copy users buffer data to audio buffs.
	 * mplayer, the most frequent user of this
	 * driver, writes with very large buffers;
	 * 40kB.
	 */
	while (copied == 0 || bytes_left >= 4) {
		int copy = 0;

		/*
		 * Wait for an audio buff to become available.
		 */ 
		while (list_empty(&free_audio_buffs)) {
			if (slx200_audio_write_printk_enabled) 
				printk("%s: interruptible_sleep_on_timeout() ", __func__);
			else
				CM_PUTC('+');
	
			/*
		 	 * Do an UNINTERRUPTIBLE sleep on a wait
		 	 * condition variable with a 1s timeout.
		 	 * Woken up by interrupt hander when it
		 	 * moves a audio buff to the free list.
		 	 */
			sleep_on_timeout(&free_audio_bufs, HZ);
			if (i2s_tx_below_level && !i2s_tx_active) {
				/* 
	  			 * We should have been woken up much earlier,
	  			 * we are already below the low water mark!
	  			 * get things moving again!
	  			 */
				spin_lock_irqsave(&lx200_lock, flags);
				if (list_empty(&full_audio_buffs)) {
					CM_PUTC('?');
				} else {
					buf = list_first_entry(&full_audio_buffs, struct audio_buff, list);
					BREAK_ON(buf == NULL);
					BREAK_ON(buf->max_len != buffer_size);
					BREAK_ON(buf->data == NULL);
					I2S_TX_INT_MASK |= 0x3;
#if 0
                                	i2s_tx_start(0x1);
#endif
					slx200_fifo_write(buf->data, buf->len);
					written += buf->len;
					buf->len = 0;
					BREAK_ON(buf->max_len != buffer_size);
					buf->line = __LINE__;
	
					list_del_init(&buf->list);
					list_add_tail(&buf->list, &free_audio_buffs);
	
					i2s_tx_active = 1;
					i2s_tx_below_level = 0;
					CM_PUTC('!');
				}
				spin_unlock_irqrestore(&lx200_lock, flags);
			} else {
				CM_PUTC('=');
			}
			if (signal_pending(current)) {
				goto out;
			}
		}
		buf = list_first_entry(&free_audio_buffs, struct audio_buff, list);
		BREAK_ON(buf == NULL);
	
		/*
		 * Attach a buffer to the Audio Buff Header if
		 * it doesn't have one; will only occure just
		 * after opens that failed to attach all of the
		 * buffers. 
		 */
		if (buf->data == NULL) {
			buf->data = (char *) kmalloc(buffer_size, GFP_KERNEL);
			if (buf->data == NULL) {
				goto out;
			}
			buf->max_len = buffer_size;
		}
		BREAK_ON(buf->max_len != buffer_size);
		copy = min(bytes_left, buf->max_len);
		BREAK_ON(copy <= 0);	
		
	
		/* Make sure count is multiple of 4. */
		copy &= ~0x3;
		BREAK_ON(copy <= 0);
		
		if (copy_from_user(buf->data, (void const *)user_buf, copy)) {
			err = -EFAULT;
			goto err_out;
		}
		buf->len = copy;
		user_buf += copy;
	
		spin_lock_irqsave(&lx200_lock, flags);
		slx200_audio_show_extern_irq(__func__, ".Begin");
#ifdef CONFIG_DEBUG_KERNEL
		/*
		 * These might come in handy while debugging on the LX200.
		 */ 
		switch(platform_board) {
		case AVNET_LX200:
			fifo_entries = NUM_FIFOENTRIES;						 /* REMIND: How to do this on LX110 */
			fifo_level = INT_FIFOLEVEL;
			free_fifo_in_bytes = (total_audio_fifo_entries - fifo_entries) * 2;
			break;
		
		case AVNET_LX110:
			break;

		default:
			BUG();
		}
#endif

		/*
 		 * Check global updated by interript hander to see if
 		 * we are currently below the low water mark (LEVEL).
 		 */
		if (i2s_tx_below_level) {
			/*
			 * Copy Data to FIFO. If buf->len is less
			 * then fifo_level we will get another LEVEL
			 * interrupt and set i2s_tx_below_level.
			 */
			I2S_TX_INT_MASK |= 0X0;
			slx200_fifo_write(buf->data, buf->len);
			CM_PUTC('F');
			written += buf->len;
			buf->len = 0;					/* Leaveing buf on free list */
			i2s_tx_below_level = 0;                         /* Let LEVEL interrupt tell us if we are below low water mark */	
		} else {
			/* 
			 * Let LEVEL interrupt handler copy data to FIFO when it's safe 
			 */
			BREAK_ON(buf->max_len != buffer_size);
			buf->line = __LINE__;
	
			list_del_init(&buf->list);
			list_add_tail(&buf->list, &full_audio_buffs);
			CM_PUTC('B');
		}
		copied += copy;
		bytes_left -= copy;
		slx200_audio_show_extern_irq(__func__, ".End");
		spin_unlock_irqrestore(&lx200_lock, flags);
	}

out:
	if (copied) {
		if (written) {
			I2S_TX_INT_MASK |= 0x2;
			if (platform_board == AVNET_LX110) {
#if 0
				i2s_tx_start(0x1);
#endif
			}
			i2s_tx_active = 1;
		}
	}
	BUG_ON(copied > initial_count);
	return copied;

	/*
	 * Return error if no bytes were copied.
	 */ 
err_out:
	if (copied == 0)
		copied = err;

	goto out;
}

int i2s_output_interrupt_printf_enable = 0;

/* The Audio Output interrupt handler. */
static irqreturn_t i2s_output_interrupt(int irq, void *dev_id)
{
	unsigned long flags;
	int fifo_entries;
	struct audio_buff *buf;
	int tx_int_stat = I2S_TX_INT_STAT;
	enum i2s_irq irq_type = (enum i2s_irq) dev_id;
	int bytes_written = 0;
	int grab_lock = 1;

	if ((irq_type == TIMER_DETECTED_OUTPUT_FIFO_LEVEL) && (platform_board == AVNET_LX200)) {
		/* Marc was on drugs */
		tx_int_stat |= 0x2;
		grab_lock = 0;
	} else if ((irq_type == OUTPUT_FIFO_LEVEL) && (platform_board == AVNET_LX200)) {
		/* Steve was on drugs */
		tx_int_stat |= 0x2;
	}

	if (grab_lock)
		spin_lock_irqsave(&lx200_lock, flags);

	slx200_audio_show_extern_irq(__func__, ".Entry");

	if (platform_board ==  AVNET_LX200) 
		fifo_entries = NUM_FIFOENTRIES;
	else
		fifo_entries = -1;

	if (tx_int_stat & 0x2) {
		if (i2s_output_interrupt_printf_enable) {
		printk("%s(irq:%d, dev_id:%p): I2S_TX_INT_STAT:0x%x, I2S_TX_INT_MASK:0x%x, fifo_entries:%d, i2s_tx_below_level:%d, i2s_tx_active:%d\n", __func__,
		   	   irq,    dev_id,     I2S_TX_INT_STAT,      I2S_TX_INT_MASK,      fifo_entries,    i2s_tx_below_level,    i2s_tx_active);
		}

		for(;;) {
			/* LEVEL/Low-Water-Mark Interrupt */
			if (list_empty(&full_audio_buffs)) {
				i2s_tx_below_level = 1;
				I2S_TX_INT_STAT |=  0x2;	/* Clear   Level Interrupt by writeing a 1 to the status bit */
				I2S_TX_INT_MASK &= ~0x2;	/* Disable Level Interrupts */
				break;
			} else {
				buf = list_first_entry(&full_audio_buffs, struct audio_buff, list);
				BREAK_ON(buf == NULL);
				BREAK_ON(buf->max_len != buffer_size);
				BREAK_ON(buf->data == NULL);

				bytes_written += buf->len;
				if ( bytes_written > bytes_above_low_water_mark) {
					/* Not safe to write more */
					break;
				}
				/*
				 * On the LX200 we can double check the test above
				 * and make sure we didn't try to go above the
				 * top of the FIFO.
				 */
				if (platform_board ==  AVNET_LX200) {
					fifo_entries = NUM_FIFOENTRIES;	
					if ( buf->len > ((total_audio_fifo_entries - fifo_entries)*2)) {
						CM_PUTC('^');	/* Too High; LX110 won't know */
						break;
					}
				}
				/* Copy buffer data to the FIFO */
				slx200_fifo_write(buf->data, buf->len);
				CM_PUTC('.');	
	
				/* Move Buffer to the Free List */
				BREAK_ON(buf->max_len != buffer_size);
				buf->line = __LINE__;
	
				list_del_init(&buf->list);
				list_add_tail(&buf->list, &free_audio_buffs);
			};

			/* Wakeup writing task if it's blocked */
			wake_up(&free_audio_bufs);
		}
	}
	if (tx_int_stat & 0x1) {
		extern void slx200_audio_test(void);

		/* UnderRun - FIFO Ran out of data! */
		i2s_tx_below_level = 1;
		i2s_tx_active = 0;
#if 0
		i2s_tx_stop();
		init_i2s_fifo();
		slx200_audio_test();
#endif
		I2S_TX_INT_STAT |=  0x1;	/* Clear   UnderRun and Level Interrupt by writing a 1 to the status bits */
		I2S_TX_INT_MASK &= ~0x1;	/* Disable UnderRun and Level  Interrupts */
		CM_PUTC('#');
	}
	slx200_audio_show_extern_irq(__func__, ".Exit");

	if (grab_lock)
		spin_unlock_irqrestore(&lx200_lock, flags);

	return IRQ_HANDLED;
}

/* The Audio Input interrupt handler. */
static irqreturn_t i2s_input_interrupt(int irq, void *dev_id)
{
	unsigned long flags;


	spin_lock_irqsave(&lx200_lock, flags);

	printk("%s(irq:%d, dev_id:%p): I2S_RX_INT_STAT:0x%x, I2S_RX_INT_MASK:0x%x\n", __func__,
		   irq,    dev_id,     I2S_RX_INT_STAT,      I2S_RX_INT_MASK);

	interrupt_expected = 0;

	I2S_RX_INT_STAT = 0x0;

	spin_unlock_irqrestore(&lx200_lock, flags);

	return IRQ_HANDLED;
}	

int buffer_trim_enabled = 1;
/*
 * Moves Full Audio Buffers to Free List and, if enabled, trim buffers.
 * Assumes lx200_lock is held by caller.
 */
void static move_full_audio_buffs_to_free_list(void) 
{
	struct audio_buff *buf;
	struct audio_buff *tmp_buf;
	int count = 0;


	if (!list_empty(&full_audio_buffs)) {
		/*
 		 * We need to use the list *_safe() version because we
 		 * are actively removing items from the list. Macro
 		 * stores data intp tmp_buf to avoid getting confused.
 		 */
		list_for_each_entry_safe(buf, tmp_buf, &full_audio_buffs, list) {
			count++;
			BREAK_ON(buf->max_len != buffer_size);
			if (buf->max_len == buffer_size) {
				buf->line = __LINE__;

				list_del_init(&buf->list);
                        	list_add_tail(&buf->list, &free_audio_buffs);
			}
		}
	}
	INIT_LIST_HEAD(&full_audio_buffs);


	/*
	 * Release the Buffers, just keep the Audio Buff headers
	 */
	if (buffer_trim_enabled) {
		count = 0;
		if (!list_empty(&free_audio_buffs)) {
			list_for_each_entry(buf, &free_audio_buffs, list) {
				count++;
				if (buf->data) {
					kfree(buf->data);
					buf->data = NULL;
				}
				buf->max_len = 0;
			}
		}
		if (count != NUM_BUFFERS) {
			printk("%s: count:%d != NUM_BUFFERS:%d\n", __func__,
				    count,      NUM_BUFFERS);
		}
	}

}

/*
 * Frees Buffer Headers and Data Buffers
 */
void static release_audio_buffs(void) {
	struct audio_buff *buf, *tmp_buf;
	unsigned long flags;


	while(i2s_tx_active) {
		printk("%s: waiting for FIFO to drain\n", __func__);
	}
	spin_lock_irqsave(&lx200_lock, flags);	

	if (!list_empty(&free_audio_buffs)) {
		list_for_each_entry_safe(buf, tmp_buf, &free_audio_buffs, list) {
			BREAK_ON(buf->max_len != buffer_size);
			list_del_init(&buf->list);
			if (buf->data) {
		 		kfree((void *) buf->data);
				buf->data = NULL;
			}
			buf->line = __LINE__;
		 	kfree((void *) buf);
		}
	}
	INIT_LIST_HEAD(&free_audio_buffs);


	if (!list_empty(&free_audio_buffs)) {
		list_for_each_entry_safe(buf, tmp_buf, &full_audio_buffs, list) {
			BREAK_ON(buf->max_len != buffer_size);
			list_del_init(&buf->list);
			if (buf->data) {
				kfree((void *) buf->data);
				buf->data = NULL;
			}
			buf->line = __LINE__;
			kfree((void *) buf);
		}
	}
	INIT_LIST_HEAD(&full_audio_buffs);

	spin_unlock_irqrestore(&lx200_lock, flags);
}

static int slx200_audio_free_irqs(void)
{
	switch (platform_board) {
	case AVNET_LX200:
		free_irq(i2s_output_fifo_level_irq, (void *) OUTPUT_FIFO_LEVEL);
		free_irq(i2s_input_fifo_level_irq, (void *) INPUT_FIFO_LEVEL);
		/* FALLTHROUGH */
	case AVNET_LX110:
		free_irq(i2s_output_fifo_underrun_irq, (void *) OUTPUT_FIFO_UNDERRUN);
		free_irq(i2s_input_fifo_underrun_irq, (void *) INPUT_FIFO_UNDERRUN);
		break;
	
	default:
		BUG();
	}
	return 0;
}

static int slx200_audio_open(struct inode *inode, struct file *file)
{
	int retval = 0;
	int retval1 = 0;
	int retval2 = 0;
	int retval3 = 0;
	int retval4 = 0;
	struct audio_buff *buf;


	if (file->f_mode & FMODE_READ)
		return -ENODEV;

	if (in_use)
		return -EBUSY;

	slx200_audio_show_extern_irq(__func__, ".before_request_irq_call");
	
	switch (platform_board) {
	case AVNET_LX200:
		retval1 = request_irq(i2s_output_fifo_level_irq, i2s_output_interrupt, AUDIO_REQUEST_IRQ_FLAG, "slx200", (void *) OUTPUT_FIFO_LEVEL);
		if (retval1 != 0) {
			printk("%s: retval1 = %d = request_irq(i2s_output_fifo_level_irq:%d, ...);\n", __func__,
				    retval1,                   i2s_output_fifo_level_irq);

			retval = retval1;
		} else {
			dprintk("%s: Enabled i2s_output_fifo_level_irq:%d\n", __func__, i2s_output_fifo_level_irq);
		} 

		retval2 = request_irq(i2s_input_fifo_level_irq,  i2s_input_interrupt,  AUDIO_REQUEST_IRQ_FLAG, "slx200", (void *) INPUT_FIFO_LEVEL);
		if (retval2 != 0) {
			printk("%s: retval2 = %d = request_irq(i2s_input_fifo_level_irq:%d, ...);\n", __func__,
				    retval2,                   i2s_input_fifo_level_irq);

			retval = retval2;
		} else {
			dprintk("%s: Enabled i2s_input_fifo_level_irq:%d\n", __func__, i2s_input_fifo_level_irq);
		}
		/* FALLTHROUGH */

	case AVNET_LX110:
		retval3 = request_irq(i2s_output_fifo_underrun_irq, i2s_output_interrupt, AUDIO_REQUEST_IRQ_FLAG, "slx200", (void *) OUTPUT_FIFO_UNDERRUN);
		if (retval3 != 0) {
			printk("%s: retval3 = %d = request_irq(i2s_output_fifo_underrun_irq:%d, ...);\n", __func__,
				    retval3,                   i2s_output_fifo_underrun_irq);

			retval = retval3;
		} else {
			dprintk("%s: Enabled i2s_output_fifo_underrun_irq:%d\n", __func__, i2s_output_fifo_underrun_irq);
		}

		retval4 = request_irq(i2s_input_fifo_underrun_irq,  i2s_input_interrupt,  AUDIO_REQUEST_IRQ_FLAG, "slx200", (void *) INPUT_FIFO_UNDERRUN);
		if (retval4 != 0) {
			printk("%s: retval4 = %d = request_irq(i2s_input_fifo_underrun_irq:%d, ...);\n", __func__,
				    retval4,                   i2s_input_fifo_underrun_irq);

			retval = retval4;
		} else {
			dprintk("%s: Enabled i2s_input_fifo_underrun_irq:%d\n", __func__, i2s_input_fifo_underrun_irq);
		}
		break;
	default:
		BUG();
	}
	if ((retval1 == 0) && (retval1 == 0) && (retval1 == 0) && (retval1 == 0)) {
		slx200_audio_show_extern_irq(__func__, ".after_request_irq_call");

		/*
		 * Try to attach buffers to the Audio Buffs now.
		 * If we fail to allocate, it's ok, we will try
		 * again when we write if the buffer data is still
		 * still not attached to the header.
		 */
		if (!list_empty(&free_audio_buffs)) {
			list_for_each_entry(buf, &free_audio_buffs, list) {
				buf->data =  (char *) kmalloc(buffer_size, GFP_KERNEL);
				if (buf->data) 
					buf->max_len = buffer_size;
			}
		}
		in_use = 1;
		if(hifi_workaround_enabled) {
			init_timer(&audio_timer);
			slx200_audio_timer((unsigned long) 0);		/* Keeps time alive while in_use */
		}
		i2s_tx_below_level = 1;
		i2s_tx_active = 0;
	} else {
		slx200_audio_free_irqs();	
	}
	return retval;
}

static int slx200_audio_release(struct inode *inode, struct file *file)
{
	unsigned long flags;


	I2S_TX_INT_STAT = 0x0;
	I2S_TX_INT_MASK = 0x0;

	CM_FLUSH();
	
	spin_lock_irqsave(&lx200_lock, flags);

	del_timer_sync(&audio_timer);

	slx200_audio_free_irqs();

	move_full_audio_buffs_to_free_list();
	in_use = 0;
	i2s_tx_below_level = 1;
	i2s_tx_active = 0;
	
	spin_unlock_irqrestore(&lx200_lock, flags);
	return 0;
}

static int hifi_audio_aic_init(int freq, int channels)
{
    int fifo_int_level_shift = 1;   
    int bit_res = BIT_RESOLUTION;

    audio_channels = channels;
    
    if (slx200_set_sample_rate(AUDIO_RATE_DEFAULT) == -1)
        return -1;

    if (platform_board == AVNET_LX110) {
    	clk_syn_init(freq);
    	i2s_tx_init(bit_res, freq, freq*256, fifo_int_level_shift);
    }
   
    slx200_audio_mute(1); 
    slx200_fifo_logic();
    slx200_audio_mute(0);

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

static int upsample(int *out_buf, int *inp_buf, int len_32bits)
{
    int i;

    for (i=0; i<len_32bits; i++)
    {
        out_buf[2*i] = inp_buf[i];
        out_buf[(2*i)+1] = inp_buf[i];
    }
    return len_32bits*2;
}

static int mono_to_stereo(int *out_buf, int *inp_buf, int len_32bits)
{
    int i;
    short *mono_buf = (short*)inp_buf;
    short *stero_buf = (short*)out_buf;

    for (i=0; i<len_32bits*2; i++)
    {
        stero_buf[2*i] = mono_buf[i];   //fill only Left channel
        stero_buf[(2*i)+1] = 0;
    }

    return len_32bits*2;
}

static int sw_upsample = 0;
static int upsample_buf[16*1024];
static int stereo_buf[16*1024];
static short sinebuf[] = {              // One sine curve
    0x0000, 0x0000, 0xb410, 0xb410, 0x2021, 0x2021, 0xfb30, 0xfb30,
    0xff3f, 0xff3f, 0xeb4d, 0xeb4d, 0x815a, 0x815a, 0x8b65, 0x8b65,
    0xd96e, 0xd96e, 0x4076, 0x4076, 0xa27b, 0xa27b, 0xe67e, 0xe67e,
    0xff7f, 0xff7f, 0xe67e, 0xe67e, 0xa27b, 0xa27b, 0x4076, 0x4076,
    0xd96e, 0xd96e, 0x8b65, 0x8b65, 0x815a, 0x815a, 0xeb4d, 0xeb4d,
    0xff3f, 0xff3f, 0xfb30, 0xfb30, 0x2021, 0x2021, 0xb410, 0xb410,
    0x0000, 0x0000, 0x4cef, 0x4cef, 0xe0de, 0xe0de, 0x05cf, 0x05cf,
    0x01c0, 0x01c0, 0x15b2, 0x15b2, 0x7fa5, 0x7fa5, 0x759a, 0x759a,
    0x2791, 0x2791, 0xc089, 0xc089, 0x5e84, 0x5e84, 0x1a81, 0x1a81,
    0x0180, 0x0180, 0x1a81, 0x1a81, 0x5e84, 0x5e84, 0xc089, 0xc089,
    0x2791, 0x2791, 0x759a, 0x759a, 0x7fa5, 0x7fa5, 0x15b2, 0x15b2,
    0x01c0, 0x01c0, 0x05cf, 0x05cf, 0xe0de, 0xe0de, 0x4cef, 0x4cef
};

static int hifi_audio_aic_write(char *data, int size, int len_32bits)
{
    int *out_buf = (int *)(data);
    int out_buf_len = len_32bits / sizeof (int);
    int i;
    short int *play_buf;

    if (sw_upsample == 1)
    {
        out_buf_len = upsample(upsample_buf, out_buf, out_buf_len);
        out_buf = upsample_buf;
    }

    if (audio_channels == 1)
    {
        //fill only Left channel
        out_buf_len = mono_to_stereo(stereo_buf, out_buf, out_buf_len);
        out_buf = stereo_buf;
    }

    play_buf = (short int *) out_buf;
    for (i = 0; i < out_buf_len; i += 1)
    {
        I2S_OUT_DATA_0 = *play_buf++;
        I2S_OUT_DATA_0 = *play_buf++;
    }

    return 0;
}

void slx200_audio_test(void)
{
	static short buf[SAMPLERATE];
	int i;
	int outsz =  sizeof (buf) /  2;

	printk("%s: Begin 2s of sinewave: ", __func__);
	/* Fill the buffer buf with sine waves. */
	for (i = 0; i < outsz; i++)
		buf[i] = sinebuf[i % (sizeof (sinebuf) / 2)];

	for (i = 0; i < 2; i++) {
		hifi_audio_aic_write ((char*) buf, 0, outsz * 2);
		if ((i%64) == 63) 
			printk("\n");
		printk(".");
	}
	printk("\n");
}


static int __init slx200_init(void)
{
	int i;
	int channels = 2;
	int saved_num_fifoentries;
	int saved_int_fifolevel;
	int rc;

	/* Initialize the lock. */
	spin_lock_init(&lx200_lock);

	in_use = 0;
	i2s_tx_below_level = 1;
	i2s_tx_active = 0;

	/* 
 	 * Initialize pointers and Interrupt Levels 
 	 * based on the Avnet Board we are running on
 	 * and if we are running SMP.
 	 */
	slx200_set_board_specific_constants();

	if (!slx200_initialized)
		return(-1);
	
        /* config DAC for playout */
	aic23_config_dac();

	I2S_TX_INT_MASK = 0x0;
	switch(platform_board) {
	case AVNET_LX200:
	case AVNET_LX110:
		rc = hifi_audio_aic_init(SAMPLERATE, channels);
		if (rc == -1)
			return(-1);
		break;

	default:
		break;
	}
	slx200_set_volume(5);

	if ((device_major = register_sound_dsp(&dac_audio_fops, -1)) < 0) {
		printk(KERN_ERR "Cannot register dsp device");
		return device_major;
	}

	INIT_LIST_HEAD(&free_audio_buffs);	
	INIT_LIST_HEAD(&full_audio_buffs);	
	for (i = 0; i < NUM_BUFFERS; i++) {
		struct audio_buff *new_buf;

		new_buf = (struct audio_buff *) kmalloc(sizeof(struct audio_buff), GFP_KERNEL);
		if (new_buf == NULL) {
			release_audio_buffs();	/* Release headers */
			return -ENOMEM;
		}

		/* 
  		 * Allocate data buffer during open to 
  		 * save memory when not in use. 
  		 */
		new_buf->max_len = 0;
		new_buf->data = NULL;		
		new_buf->len = 0;
		new_buf->instance = i;
		new_buf->line = __LINE__;

		INIT_LIST_HEAD(&new_buf->list);
		list_add_tail(&new_buf->list, &free_audio_buffs);
	}
	I2S_TX_INT_MASK = 0x0;

	slx200_audio_test();

	switch(platform_board) {
	case AVNET_LX200:
		saved_num_fifoentries = NUM_FIFOENTRIES;
		saved_int_fifolevel = INT_FIFOLEVEL;
		printk("%s: saved_num_fifoentries:%d, saved_int_fifolevel:%d\n", __func__,
			    saved_num_fifoentries,    saved_int_fifolevel);
		break;

	case AVNET_LX110:
	default:
		break;
	}
	return 0;
}

static void __exit slx200_exit(void)
{
	if (slx200_initialized) {
                i2s_tx_stop(); 				//stop i2s logic
		release_audio_buffs();
		unregister_sound_dsp(device_major);

	}
}

module_init(slx200_init);
module_exit(slx200_exit);

MODULE_AUTHOR("Tensilica");
MODULE_DESCRIPTION("Audio driver for the LX200 board.");
MODULE_LICENSE("GPL");
