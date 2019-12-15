/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _GPIO_ESP32_H
#define _GPIO_ESP32_H

/* peri is Signal No. for desired Input signal
 * from table 6-2 Peripheral Signals via GPIO Matrix from esp32s3 TRM
 */
#define GPIO_FUNC_IN_SEL(peri)	((peri) * 4)
#define GPIO_FUNC_OUT_SEL(pin)	((pin) * 4)

#define PIN(pin)		((pin) * 4)

#define FUN_WPD			0x0080
#define FUN_WPU			0x0100
#define FUN_IE			0x0200
#define FUN_DRV_5MA		0x0000
#define FUN_DRV_10MA		0x0400
#define FUN_DRV_20MA		0x0800
#define FUN_DRV_40MA		0x0c00
#define FUN_SEL(fn)		(((fn) & 0x7) << 12)
#define FILTER_EN		0x8000

#define FUN0_20MA		(FUN_SEL(0) | FUN_DRV_20MA)
#define FUN0_20MA_IE		(FUN_SEL(0) | FUN_DRV_20MA | FUN_IE)
#define FUN0_20MA_IE_WPU	(FUN_SEL(0) | FUN_DRV_20MA | FUN_IE | FUN_WPU)

/* esp32 IO MUX indices */

#define IOMUX_GPIO36		0
#define IOMUX_GPIO37		1
#define IOMUX_GPIO38		2
#define IOMUX_GPIO39		3
#define IOMUX_GPIO34		4
#define IOMUX_GPIO35		5
#define IOMUX_GPIO32		6
#define IOMUX_GPIO33		7
#define IOMUX_GPIO25		8
#define IOMUX_GPIO26		9
#define IOMUX_GPIO27		10
#define IOMUX_MTMS		11
#define IOMUX_MTDI		12
#define IOMUX_MTCK		13
#define IOMUX_MTDO		14
#define IOMUX_GPIO2		15
#define IOMUX_GPIO0		16
#define IOMUX_GPIO4		17
#define IOMUX_GPIO16		18
#define IOMUX_GPIO17		19
#define IOMUX_SD_DATA2		20
#define IOMUX_SD_DATA3		21
#define IOMUX_SD_CMD		22
#define IOMUX_SD_CLK		23
#define IOMUX_SD_DATA0		24
#define IOMUX_SD_DATA1		25
#define IOMUX_GPIO5		26
#define IOMUX_GPIO18		27
#define IOMUX_GPIO19		28
#define IOMUX_GPIO20		29
#define IOMUX_GPIO21		30
#define IOMUX_GPIO22		31
#define IOMUX_U0RXD		32
#define IOMUX_U0TXD		33
#define IOMUX_GPIO23		34
#define IOMUX_GPIO24		35

#endif
