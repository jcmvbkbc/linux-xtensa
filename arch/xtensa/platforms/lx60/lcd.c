/*
 * Driver for the LCD display on the Tensilica LX60 Board.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2010 Tensilica Inc.
 */

/*
 *
 * FIXME: this code is from the examples from the LX60 user guide.
 *
 * See:
 *     http://www.mytechcorp.com/cfdata/productFile/File1/MOC-16216B-B-A0A04.pdf
 *
 * The lcd_pause function does busy waiting, which is probably not
 * great. Maybe the code could be changed to use kernel timers, or
 * change the hardware to not need to wait.
 */

#include <linux/init.h>

#include <platform/hardware.h>
#include <asm/processor.h>
#include <asm/platform.h>
#include <platform/system.h>
#include <platform/lcd.h>
#include <linux/delay.h>

#define LCD_PAUSE_ITERATIONS	4000 

/*
 * LCD Commands:
 */
#define LCD_CLEAR		0x01

#define LCD_HOME_POSITION	0x02

#define LCD_SET_ENTRY_MODE	0x04	/* Combine Entry Mode */
#define LCD_SET_ENTRY_MODE_CI	0x06		/* SetEntryMode:1, CursorInc:1, DisplayShift:0 */

#define LCD_COMBINED_ENABLE	0X08	/* Enable/Disable Display, Cursor, Blink */
#define LCD_DISPLAY_OFF		0x0c		/* Display:0, Cursor:0, Blink:0 */
#define LCD_DISPLAY_ON		0x0c		/* Display:1, Cursor:0, Blink:0 */

#define LCD_SHIF_COMMAND	0X10	/* Shift Commands */
#define LCD_SHIFT_LEFT		0x18
#define LCD_SHIFT_RIGHT		0x1c

#define LCD_FUNC		0x20	/* Combined Function flags */
#define LCD_DISPLAY_MODE_4BIT	0x28		/* DataLenght:0, DisplayLines:1, FontType:0 */
#define LCD_DISPLAY_MODE_8BIT	0x38		/* DataLenght:1, DisplayLines:1, FontType:0 */

#define LCD_CHAR_GEN		0x40	/* Char Generator Address */

#define LCD_DISPLAY_POS		0x80

int lcd_initialized = 0;
unsigned char *lcd_instr_addr = NULL;
unsigned char *lcd_data_addr = NULL;

/*
 * This is used to stuff bytes once the LCD has been put in 4/8 bit mode
 */
static void lcd_put_byte(unsigned char *lcd_register_addr, unsigned char instr)
{
	switch(platform_board) {
	case AVNET_LX110:
		*lcd_register_addr =  (instr & 0XF0);		/* High Nibble First */
		*lcd_register_addr =  (instr << 4) & 0XF0;
		break;

	case AVNET_LX60:
		*lcd_register_addr =  instr;
		break;

	default:
		BUG();
	}

#if 0
	/*
 	 * Controller requires a software delay after writing to the control
	 * or data registers. For the data register it is 38us. For the control
	 * register it is 38us for most bit fields, with the following exceptions:
    	 *	LCD_FUNC_                        100us.
    	 * 	LCD_CLEAR, LCD_HOME_POSITION     1520us.
    	 *
	 * For more details and reset timing, see the SUNPLUS SPLC780D data sheet.
	 */
	if (lcd_register_addr == lcd_data_addr)
		udelay(38);
	else {
		if ((instr & LCD_FUNC) && (instr & (LCD_CHAR_GEN | LCD_DISPLAY_POS)) == 0)
			udelay(100);
		else if ((instr == LCD_CLEAR) || (instr == LCD_HOME_POSITION)) 
			udelay(152);
		else udelay(38);
	}
#endif

}

static int __init lcd_init(void)
{
	if (lcd_initialized)
		goto done;

	udelay(200);

	/*
	 * Set the instruction and data register pointers 
	 * based on the current board.
	 */
	switch(platform_board) {
	case AVNET_LX60:
		lcd_instr_addr = (char *) LX60_LCD_INSTR_ADDR;
		lcd_data_addr =  (char *) LX60_LCD_DATA_ADDR;
		break;
		
	case AVNET_LX110:
		lcd_instr_addr = (char *) LX110_LCD_INSTR_ADDR;
		lcd_data_addr =  (char *) LX110_LCD_DATA_ADDR;
		break;
		
	case AVNET_LX200:
		goto done;

	default:
		BUG();
	}

	/*
	 * Both the LX60 and LX110 SYNC-UP with
	 * the same 3 initial instructions,
	 */
	*lcd_instr_addr = LCD_DISPLAY_MODE_8BIT;
	mdelay(20);
	*lcd_instr_addr = LCD_DISPLAY_MODE_8BIT;
	mdelay(20);
	*lcd_instr_addr = LCD_DISPLAY_MODE_8BIT;
	mdelay(20);

	switch(platform_board) {
	case AVNET_LX110:
		*lcd_instr_addr = LCD_DISPLAY_MODE_4BIT;		/* Only looks at bits 4...7 */
		mdelay(20);
		lcd_put_byte(lcd_instr_addr, LCD_DISPLAY_MODE_4BIT);	/* Sees all 8 bits via two writes */
		mdelay(20);
		break;

	case AVNET_LX60:
#if 0
		lcd_put_byte(lcd_instr_addr, LCD_DISPLAY_MODE_8BIT);	/* Sees all 8 bits via one write */
#endif
		break;

	default:
		break;
	}
	lcd_put_byte(lcd_instr_addr, LCD_DISPLAY_ON);
	mdelay(20);
	lcd_put_byte(lcd_instr_addr, LCD_CLEAR);
	mdelay(20);
#if 0
	lcd_put_byte(lcd_instr_addr, LCD_SET_ENTRY_MODE_CI);
	udelay(200);
#endif

	lcd_initialized++;
	
	lcd_disp_at_pos("XTENSA SMP LINUX", 0);

done:
	return 0;
}

void lcd_clear()
{
	if (lcd_initialized)
		lcd_put_byte(lcd_instr_addr, LCD_CLEAR);
}

void lcd_disp_at_pos (char *str, unsigned char pos)
{
	if (lcd_initialized) {
		udelay(100);
		lcd_put_byte(lcd_instr_addr, (LCD_DISPLAY_POS | pos));	
		udelay(100);
		while (*str != 0) {
			lcd_put_byte(lcd_data_addr, *str++);
			udelay(200);
		}
	}
}

void lcd_shiftleft(void)
{
	if (lcd_initialized) {
		lcd_put_byte(lcd_instr_addr, LCD_SHIFT_LEFT);
		mdelay(20);
	}
}

void lcd_shiftright(void)
{
	if (lcd_initialized) {
		lcd_put_byte(lcd_instr_addr, LCD_SHIFT_RIGHT);
		mdelay(20);	
	}
}

arch_initcall(lcd_init);
