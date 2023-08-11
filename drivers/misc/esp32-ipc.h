/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef ESP32_IPC_H
#define ESP32_IPC_H

struct esp32_ipc_queue {
	u32 write;
	u32 read;
	u32 offset;
	u32 mask;
};

struct esp32_ipc_queue_entry {
	u32 addr;
	void *info;
};

#endif
