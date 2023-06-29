#ifndef ESP_SHMEM_H
#define ESP_SHMEM_H

struct esp_wifi_shmem_queue
{
	u32 write;
	u32 read;
	u32 offset;
	u32 mask;
};

#endif
