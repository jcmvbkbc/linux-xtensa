// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#define SYSTEM_CPUPERIOD_SEL	GENMASK(1, 0)
#define SYSTEM_PLL_FREQ_SEL	BIT(2)

#define SYSTEM_PRE_DIV_CNT	GENMASK(9, 0)
#define SYSTEM_SOC_CLK_SEL	GENMASK(11, 10)
#define SYSTEM_CLK_XTAL_FREQ	GENMASK(18, 12)

#define ESP32S3_NUM_CORE_CLOCKS	3

static void __init esp32s3_core_clk_init(struct device_node *node)
{
	void __iomem *base;
	u32 cpu_per;
	u32 sysclk_conf;

	u32 soc_clk_sel;
	u32 system_pre_div_cnt;
	u32 cpuperiod_sel;

	u32 xtal_clk_rate;
	u32 cpu_clk_rate;
	u32 apb_clk_rate;

	struct clk_hw_onecell_data *esp32s3_clk_data;
	struct clk_hw *hw;

	base = of_iomap(node, 0);
	if (!base) {
		pr_err("%pOFn: failed to map cpu_per register\n", node);
		return;
	}
	cpu_per = ioread32(base);
	iounmap(base);

	base = of_iomap(node, 1);
	if (!base) {
		pr_err("%pOFn: failed to map sysclk_conf register\n", node);
		return;
	}
	sysclk_conf = ioread32(base);
	iounmap(base);

	esp32s3_clk_data = kzalloc(struct_size(esp32s3_clk_data, hws,
					       ESP32S3_NUM_CORE_CLOCKS),
				   GFP_KERNEL);

	xtal_clk_rate = FIELD_GET(SYSTEM_CLK_XTAL_FREQ, sysclk_conf) * 1000000;
	soc_clk_sel = FIELD_GET(SYSTEM_SOC_CLK_SEL, sysclk_conf);
	system_pre_div_cnt = FIELD_GET(SYSTEM_PRE_DIV_CNT, sysclk_conf);

	switch (soc_clk_sel) {
	case 0:
		cpu_clk_rate = xtal_clk_rate / (system_pre_div_cnt + 1);
		apb_clk_rate = cpu_clk_rate;
		break;
	case 1:
		//pll_clk_rate = (cpu_per & SYSTEM_PLL_FREQ_SEL) ? 480000000 : 320000000;
		cpuperiod_sel = FIELD_GET(SYSTEM_CPUPERIOD_SEL, cpu_per);
		cpu_clk_rate = 80000000 * (cpuperiod_sel + 1);
		apb_clk_rate = 80000000;
		break;
	case 2:
		cpu_clk_rate = 17500000 / (system_pre_div_cnt + 1);
		apb_clk_rate = cpu_clk_rate;
		break;
	}

	hw = clk_hw_register_fixed_rate(NULL, "xtal_clk", NULL, 0, xtal_clk_rate);
	esp32s3_clk_data->hws[0] = hw;
	hw = clk_hw_register_fixed_rate(NULL, "cpu_clk", NULL, 0, cpu_clk_rate);
	esp32s3_clk_data->hws[1] = hw;
	hw = clk_hw_register_fixed_rate(NULL, "apb_clk", NULL, 0, apb_clk_rate);
	esp32s3_clk_data->hws[2] = hw;
	esp32s3_clk_data->num = ESP32S3_NUM_CORE_CLOCKS;

	of_clk_add_hw_provider(node, of_clk_hw_onecell_get, esp32s3_clk_data);
}
CLK_OF_DECLARE_DRIVER(esp32s3_cc, "esp,esp32s3-core-clock", esp32s3_core_clk_init);
