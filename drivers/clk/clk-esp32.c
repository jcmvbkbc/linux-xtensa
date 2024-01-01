// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#define RTC_CNTL_SOC_CLK_SEL	GENMASK(28, 27)

#define DPORT_CPU_CPUPERIOD_SEL	GENMASK(1, 0)

#define SYSCON_PRE_DIV_CNT	GENMASK(9, 0)

#define ESP32_NUM_CORE_CLOCKS	2

static void __init esp32_core_clk_init(struct device_node *node)
{
	void __iomem *base;
	u32 rtc_cntl_clk_conf;
	u32 cpu_per;
	u32 sysclk_conf;

	u32 soc_clk_sel;
	u32 system_pre_div_cnt;
	u32 cpuperiod_sel;

	struct clk *clk = NULL;
	u32 cpu_clk_rate;
	u32 apb_clk_rate;

	struct clk_hw_onecell_data *esp32_clk_data;
	struct clk_hw *hw;

	base = of_iomap(node, 0);
	if (!base) {
		pr_err("%pOFn: failed to map rtc_cntl_clk_conf register\n", node);
		return;
	}
	rtc_cntl_clk_conf = ioread32(base);
	iounmap(base);

	base = of_iomap(node, 1);
	if (!base) {
		pr_err("%pOFn: failed to map cpu_per register\n", node);
		return;
	}
	cpu_per = ioread32(base);
	iounmap(base);

	base = of_iomap(node, 2);
	if (!base) {
		pr_err("%pOFn: failed to map sysclk_conf register\n", node);
		return;
	}
	sysclk_conf = ioread32(base);
	iounmap(base);

	soc_clk_sel = FIELD_GET(RTC_CNTL_SOC_CLK_SEL, rtc_cntl_clk_conf);
	cpuperiod_sel = FIELD_GET(DPORT_CPU_CPUPERIOD_SEL, cpu_per);
	system_pre_div_cnt = FIELD_GET(SYSCON_PRE_DIV_CNT, sysclk_conf);

	switch (soc_clk_sel) {
	case 0:
		clk = of_clk_get_by_name(node, "xtl");
		if (IS_ERR_OR_NULL(clk) || clk_prepare_enable(clk)) {
			pr_err("%pOFn: couldn't get, prepare or enable xtl clock\n", node);
			return;
		}
		cpu_clk_rate = clk_get_rate(clk) / (system_pre_div_cnt + 1);
		apb_clk_rate = cpu_clk_rate;
		break;
	case 1:
		cpu_clk_rate = 80000000 * (cpuperiod_sel + 1);
		apb_clk_rate = 80000000;
		break;
	case 2:
		clk = of_clk_get_by_name(node, "rc_fast");
		if (IS_ERR_OR_NULL(clk) || clk_prepare_enable(clk)) {
			pr_err("%pOFn: couldn't get, prepare or enable rc_fast clock\n", node);
			return;
		}
		cpu_clk_rate = clk_get_rate(clk) / (system_pre_div_cnt + 1);
		apb_clk_rate = cpu_clk_rate;
		break;
	case 3:
		clk = of_clk_get_by_name(node, "apll");
		if (IS_ERR_OR_NULL(clk) || clk_prepare_enable(clk)) {
			pr_err("%pOFn: couldn't get, prepare or enable apll clock\n", node);
			return;
		}
		cpu_clk_rate = clk_get_rate(clk) * (cpuperiod_sel + 1) / 4;
		apb_clk_rate = cpu_clk_rate / 2;
		break;
	}

	esp32_clk_data = kzalloc(struct_size(esp32_clk_data, hws,
					     ESP32_NUM_CORE_CLOCKS),
				 GFP_KERNEL);
	if (!esp32_clk_data) {
		if (clk) {
			clk_disable_unprepare(clk);
			clk_put(clk);
		}
		return;
	}

	hw = clk_hw_register_fixed_rate(NULL, "cpu_clk", NULL, 0, cpu_clk_rate);
	esp32_clk_data->hws[0] = hw;
	hw = clk_hw_register_fixed_rate(NULL, "apb_clk", NULL, 0, apb_clk_rate);
	esp32_clk_data->hws[1] = hw;
	esp32_clk_data->num = ESP32_NUM_CORE_CLOCKS;

	of_clk_add_hw_provider(node, of_clk_hw_onecell_get, esp32_clk_data);
}
CLK_OF_DECLARE_DRIVER(esp32_cc, "esp,esp32-core-clock", esp32_core_clk_init);
