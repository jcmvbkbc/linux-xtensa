/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef ESP32_IPC_API_H
#define ESP32_IPC_API_H

#include <linux/types.h>

int esp32_ipc_register_rx(void *ipc, u32 addr, void *p,
			  int (*rx)(void *p, void *data),
			  void (*rx_batch_done)(void *p, u32 tag));
int esp32_ipc_tx(void *ipc, u32 addr, void *info, u32 *tag);

#endif
