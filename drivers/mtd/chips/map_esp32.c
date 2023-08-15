// SPDX-License-Identifier: GPL-2.0

#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/esp32-ipc-api.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mtd/map.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/xip.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>

static int map_esp32_read(struct mtd_info *, loff_t, size_t, size_t *, u_char *);
static int map_esp32_write(struct mtd_info *, loff_t, size_t, size_t *, const u_char *);
static void map_esp32_nop(struct mtd_info *);
static struct mtd_info *map_esp32_probe(struct map_info *map);
static int map_esp32_erase(struct mtd_info *mtd, struct erase_info *info);
static int map_esp32_point(struct mtd_info *mtd, loff_t from, size_t len,
			 size_t *retlen, void **virt, resource_size_t *phys);
static int map_esp32_unpoint(struct mtd_info *mtd, loff_t from, size_t len);

enum {
	ESP32_IPC_FLASH_STATE_IDLE,
	ESP32_IPC_FLASH_STATE_READY,
	ESP32_IPC_FLASH_STATE_DONE,
};

enum {
	ESP_IPC_FLASH_CMD_ERASE,
	ESP_IPC_FLASH_CMD_READ,
	ESP_IPC_FLASH_CMD_WRITE,
};

struct esp32_ipc_flash_cmd {
	void *data;
	u32 addr;
	u32 size;
	u32 local_state;

	u32 remote_state;
	u32 result;
	u32 code;
};

struct esp32_ipc_flash {
	void *ipc;
	u32 ipc_addr;
	struct map_info *map;
	struct esp32_ipc_flash_cmd *cmd;
	struct completion *completion;
};

static int esp32_ipc_flash_erase(struct esp32_ipc_flash *hw, u32 off, u32 size);
static int esp32_ipc_flash_read(struct esp32_ipc_flash *hw, u32 off, u32 size,
				void *data);
static int esp32_ipc_flash_write(struct esp32_ipc_flash *hw, u32 off, u32 size,
				 const void *data);


#define ESP32_PARTITION_TABLE_DEFAULT_ADDRESS	0x8000
#define ESP32_PARTITION_RECORD_MAGIC0		0xaa
#define ESP32_PARTITION_RECORD_MAGIC1		0x50

struct esp32_partition_record {
	u8 magic[2];
	u8 type;
	u8 subtype;
	u32 offset;
	u32 size;
	char name[20];
};

static struct mtd_chip_driver map_esp32_chipdrv = {
	.probe	= map_esp32_probe,
	.name	= "map_rom",
	.module	= THIS_MODULE
};

static unsigned int default_erasesize(struct map_info *map)
{
	const __be32 *erase_size = NULL;

	erase_size = of_get_property(map->device_node, "erase-size", NULL);

	return !erase_size ? map->size : be32_to_cpu(*erase_size);
}

static struct mtd_info *map_esp32_probe(struct map_info *map)
{
	struct mtd_info *mtd;
	struct device_node *controller_node;
	struct platform_device *controller_pdev;
	struct esp32_ipc_flash *hw;

	controller_node = of_parse_phandle(map->device_node, "controller", 0);
	controller_pdev = of_find_device_by_node(controller_node);
	if (!controller_pdev)
		return NULL;
	hw = platform_get_drvdata(controller_pdev);
	hw->map = map;

	mtd = kzalloc(sizeof(*mtd), GFP_KERNEL);
	if (!mtd)
		return NULL;

	map->fldrv = &map_esp32_chipdrv;
	mtd->priv = hw;
	mtd->name = map->name;
	mtd->type = MTD_ROM;
	mtd->size = map->size;
	mtd->_point = map_esp32_point;
	mtd->_unpoint = map_esp32_unpoint;
	mtd->_read = map_esp32_read;
	mtd->_write = map_esp32_write;
	mtd->_sync = map_esp32_nop;
	mtd->_erase = map_esp32_erase;
	mtd->flags = MTD_WRITEABLE;
	mtd->erasesize = default_erasesize(map);
	mtd->writesize = 1;
	mtd->writebufsize = 1;

	__module_get(THIS_MODULE);
	return mtd;
}


static int map_esp32_point(struct mtd_info *mtd, loff_t from, size_t len,
			   size_t *retlen, void **virt, resource_size_t *phys)
{
	struct esp32_ipc_flash *hw = mtd->priv;
	struct map_info *map = hw->map;

	if (!map->virt)
		return -EINVAL;
	*virt = map->virt + from;
	if (phys)
		*phys = map->phys + from;
	*retlen = len;
	return 0;
}

static int map_esp32_unpoint(struct mtd_info *mtd, loff_t from, size_t len)
{
	return 0;
}

static int map_esp32_read(struct mtd_info *mtd, loff_t from, size_t len,
			  size_t *retlen, u_char *buf)
{
	struct esp32_ipc_flash *hw = mtd->priv;
	struct map_info *map = hw->map;

	map_copy_from(map, buf, from, len);
	*retlen = len;
	return 0;
}

static void map_esp32_nop(struct mtd_info *mtd)
{
	/* Nothing to see here */
}

static int map_esp32_write(struct mtd_info *mtd, loff_t to, size_t len,
			   size_t *retlen, const u_char *buf)
{
	struct esp32_ipc_flash *hw = mtd->priv;
	int ret;

	ret = esp32_ipc_flash_write(hw, to, len, buf);
	*retlen = len;
	return ret;
}

static int map_esp32_erase(struct mtd_info *mtd, struct erase_info *info)
{
	struct esp32_ipc_flash *hw = mtd->priv;

	return esp32_ipc_flash_erase(hw, info->addr, info->len);
}

static void __xipram esp32_ipc_flash_io_iram(struct esp32_ipc_flash_cmd *cmd)
{
	unsigned long flags;

	local_irq_save(flags);
	WRITE_ONCE(cmd->local_state, ESP32_IPC_FLASH_STATE_READY);
	wmb();
	while (READ_ONCE(cmd->remote_state) != ESP32_IPC_FLASH_STATE_DONE)
		rmb();
	local_irq_restore(flags);
}

static int esp32_ipc_flash_io(struct esp32_ipc_flash *hw,
			      struct esp32_ipc_flash_cmd *cmd)
{
	int ret;

	WRITE_ONCE(cmd->remote_state, ESP32_IPC_FLASH_STATE_IDLE);
	WRITE_ONCE(cmd->local_state, ESP32_IPC_FLASH_STATE_IDLE);
	wmb();

	ret = esp32_ipc_tx(hw->ipc, hw->ipc_addr, cmd, NULL);
	if (ret)
		return ret;

	while (READ_ONCE(cmd->remote_state) == ESP32_IPC_FLASH_STATE_IDLE)
		rmb();
	if (READ_ONCE(cmd->remote_state) != ESP32_IPC_FLASH_STATE_READY)
		return -EINVAL;
	esp32_ipc_flash_io_iram(cmd);
	return cmd->result ? -EINVAL : 0;
}

static int esp32_ipc_flash_erase(struct esp32_ipc_flash *hw, u32 off, u32 size)
{
	hw->cmd->code = ESP_IPC_FLASH_CMD_ERASE;
	hw->cmd->addr = off;
	hw->cmd->size = size;
	hw->cmd->data = NULL;
	return esp32_ipc_flash_io(hw, hw->cmd);
}

static int esp32_ipc_flash_read(struct esp32_ipc_flash *hw, u32 off, u32 size,
				void *data)
{
	hw->cmd->code = ESP_IPC_FLASH_CMD_READ;
	hw->cmd->addr = off;
	hw->cmd->size = size;
	hw->cmd->data = data;
	return esp32_ipc_flash_io(hw, hw->cmd);
}

static int esp32_ipc_flash_write(struct esp32_ipc_flash *hw, u32 off, u32 size,
				 const void *data)
{
	hw->cmd->code = ESP_IPC_FLASH_CMD_WRITE;
	hw->cmd->addr = off;
	hw->cmd->size = size;
	hw->cmd->data = (void *)data;
	return esp32_ipc_flash_io(hw, hw->cmd);
}

static int esp32_ipc_flash_rx(void *p, void *data)
{
	struct esp32_ipc_flash *hw = p;

	hw->cmd = data;
	complete(hw->completion);
	return 1;
}

static int esp32_ipc_flash_probe(struct platform_device *pdev)
{
	struct esp32_ipc_flash *hw =
		devm_kzalloc(&pdev->dev, sizeof(*hw), GFP_KERNEL);
	DECLARE_COMPLETION_ONSTACK(completion);
	int ret;

	if (!hw)
		return -ENOMEM;
	platform_set_drvdata(pdev, hw);

	init_completion(&completion);
	hw->completion = &completion;
	hw->ipc = platform_get_drvdata(to_platform_device(pdev->dev.parent));
	ret = of_property_read_u32(pdev->dev.of_node, "reg", &hw->ipc_addr);
	if (ret < 0)
		return ret;

	ret = esp32_ipc_register_rx(hw->ipc, hw->ipc_addr, hw,
				    esp32_ipc_flash_rx, NULL);
	if (ret < 0)
		return ret;

	while (esp32_ipc_tx(hw->ipc, hw->ipc_addr, NULL, NULL) != 0) {
	}
	wait_for_completion(&completion);
	if (!hw->cmd)
		return -ENOMEM;
	return 0;
}

static int esp32_ipc_flash_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id esp32_ipc_flash_match[] = {
	{
		.compatible = "esp,esp32-ipc-flash",
	}, {},
};
MODULE_DEVICE_TABLE(of, esp32_ipc_flash_match);

static struct platform_driver esp32_ipc_flash_driver = {
	.probe   = esp32_ipc_flash_probe,
	.remove  = esp32_ipc_flash_remove,
	.driver  = {
		.name = "esp32-ipc-flash",
		.of_match_table = of_match_ptr(esp32_ipc_flash_match),
	},
};

static const struct of_device_id mtd_parser_esp32_of_match_table[] = {
	{
		.compatible = "esp,esp32-partition-table",
	}, {},
};
MODULE_DEVICE_TABLE(of, mtd_parser_esp32_of_match_table);

static int esp32_flash_parse_partitions(struct mtd_info *mtd,
					const struct mtd_partition **pparts,
					struct mtd_part_parser_data *data)
{
	struct esp32_ipc_flash *hw = mtd->priv;
	struct esp32_partition_record pr[16];
	u32 addr = ESP32_PARTITION_TABLE_DEFAULT_ADDRESS;
	struct device_node *of_node;
	int ret;

	of_node = mtd_get_of_node(mtd);
	if (of_node) {
		struct device_node *part = of_get_child_by_name(of_node, "partitions");

		if (part) {
			of_property_read_u32(part, "esp32-partition-table-addr", &addr);
			of_node_put(part);
		}
	}

	ret = esp32_ipc_flash_read(hw, addr, sizeof(pr), pr);
	if (!ret) {
		int i;
		struct mtd_partition *mtd_part = kzalloc(sizeof(*mtd_part) * ARRAY_SIZE(pr),
							 GFP_KERNEL);

		if (!mtd_part)
			return -ENOMEM;

		*pparts = mtd_part;

		for (i = 0; i < ARRAY_SIZE(pr); ++i) {
			struct esp32_partition_record *p = pr + i;
			struct mtd_partition *mp = mtd_part + i;

			if (p->magic[0] != ESP32_PARTITION_RECORD_MAGIC0 ||
			    p->magic[1] != ESP32_PARTITION_RECORD_MAGIC1)
				break;
			mp->size = p->size;
			mp->offset = p->offset;
			mp->name = kstrdup(p->name, GFP_KERNEL);
		}
		ret = i;
	}
	return ret;
}

static void esp32_flash_cleanup(const struct mtd_partition *pparts, int nr_parts)
{
	int i;

	for (i = 0; i < nr_parts; ++i)
		kfree(pparts[i].name);
	kfree(pparts);
}

static struct mtd_part_parser esp32_flash_partition_parser = {
	.parse_fn = esp32_flash_parse_partitions,
	.cleanup = esp32_flash_cleanup,
	.name = "esp32",
	.of_match_table = mtd_parser_esp32_of_match_table,
};

static int __init map_esp32_init(void)
{
	int ret = platform_driver_register(&esp32_ipc_flash_driver);

	if (!ret) {
		register_mtd_chip_driver(&map_esp32_chipdrv);
		ret = register_mtd_parser(&esp32_flash_partition_parser);
	}
	return ret;
}

static void __exit map_esp32_exit(void)
{
	deregister_mtd_parser(&esp32_flash_partition_parser);
	unregister_mtd_chip_driver(&map_esp32_chipdrv);
	platform_driver_unregister(&esp32_ipc_flash_driver);
}

module_init(map_esp32_init);
module_exit(map_esp32_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Max Filippov <jcmvbkbc@gmail.com>");
MODULE_DESCRIPTION("MTD chip driver for ESP32 chips");
