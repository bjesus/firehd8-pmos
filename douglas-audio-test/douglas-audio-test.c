// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/mfd/mt6323/registers.h>

#define ABB_AFE_CON0		0x4000
#define ABB_AFE_CON1		0x4002
#define ABB_AFE_CON10		0x4014
#define ABB_AFE_CON11		0x4016
#define AFE_PMIC_NEWIF_CFG0	0x4024

static void __iomem *afe_base;
static void __iomem *topckgen_base;
static struct regmap *pctl_regmap;
static struct regmap *pmic_regmap;
static struct clk *clk_infra, *clk_aud_sel, *clk_intbus, *clk_mux2, *clk_apll2;

static struct clk *get_dt_clk(const char *compat, u32 id)
{
	struct device_node *np = of_find_compatible_node(NULL, NULL, compat);
	struct of_phandle_args spec = { .np = np, .args_count = 1, .args = { id } };
	struct clk *c;

	if (!np)
		return ERR_PTR(-ENODEV);
	c = of_clk_get_from_provider(&spec);
	of_node_put(np);
	return c;
}

static void set_pin_mode(int pin, int mode)
{
	u32 reg = 0x0600 + ((pin / 5) << 4);
	u32 shift = 3 * (pin % 5);
	regmap_update_bits(pctl_regmap, reg, 0xf << shift, (mode & 0xf) << shift);
}

static void set_gpio_out(int pin, int val)
{
	u32 dir_reg = 0x0000 + ((pin / 16) << 4);
	u32 dout_reg = 0x0400 + ((pin / 16) << 4);
	u32 bit = BIT(pin % 16);

	set_pin_mode(pin, 0);
	regmap_update_bits(pctl_regmap, dir_reg, bit, bit);
	regmap_update_bits(pctl_regmap, dout_reg, bit, val ? bit : 0);
}

static void afe_update(u32 reg, u32 mask, u32 val)
{
	u32 v = readl(afe_base + reg);
	v = (v & ~mask) | (val & mask);
	writel(v, afe_base + reg);
}

static int __init douglas_audio_test_init(void)
{
	struct regmap *scpsys;
	struct device *pmic_dev;
	struct mt6397_chip *chip;
	u32 val;
	int ret;

	scpsys = syscon_regmap_lookup_by_compatible("mediatek,mt8163-scpsys");
	pctl_regmap = syscon_regmap_lookup_by_compatible("mediatek,mt8163-pctl-a-syscfg");
	if (IS_ERR(scpsys) || IS_ERR(pctl_regmap)) {
		pr_err("douglas-audio-test: syscon lookup failed\n");
		return -ENODEV;
	}

	pmic_dev = bus_find_device_by_name(&platform_bus_type, NULL, "1000d000.pwrap:mt6323");
	if (!pmic_dev)
		return -ENODEV;
	chip = dev_get_drvdata(pmic_dev);
	pmic_regmap = chip ? chip->regmap : dev_get_regmap(pmic_dev, NULL);
	put_device(pmic_dev);
	if (!pmic_regmap)
		return -ENODEV;

	/* 1. Power on AUDIO domain */
	regmap_write(scpsys, 0x029c, 0x0000000d);

	/* 2. Map TOPCKGEN & AFE */
	topckgen_base = ioremap(0x10000000, 0x1000);
	afe_base = ioremap(0x11220000, 0x1000);
	if (!topckgen_base || !afe_base)
		return -ENOMEM;

	/* 3. Enable clocks */
	clk_infra = get_dt_clk("mediatek,mt8163-infracfg", 48);
	clk_aud_sel = get_dt_clk("mediatek,mt8163-topckgen", 79);
	clk_intbus = get_dt_clk("mediatek,mt8163-topckgen", 80);
	clk_mux2 = get_dt_clk("mediatek,mt8163-topckgen", 88);
	clk_apll2 = get_dt_clk("mediatek,mt8163-topckgen", 11);

	if (!IS_ERR(clk_infra)) clk_prepare_enable(clk_infra);
	if (!IS_ERR(clk_intbus)) clk_prepare_enable(clk_intbus);
	if (!IS_ERR(clk_aud_sel)) clk_prepare_enable(clk_aud_sel);
	if (!IS_ERR(clk_mux2) && !IS_ERR(clk_apll2)) {
		clk_set_parent(clk_mux2, clk_apll2);
		clk_prepare_enable(clk_mux2);
	}

	/* 4. Configure AFE digital + ADDA + SGEN (I05/I06 -> O03/O04) */
	afe_update(0x0000, BIT(14), BIT(14)); /* AFE_BUS_INIT */
	afe_update(0x0000, BIT(2) | BIT(6) | BIT(9) | BIT(25) | BIT(26), 0); /* ungate AFE + 24M + DAC */
	writel(0, afe_base + 0x0260); /* AFE_ADDA_PREDIS_CON0 */
	writel(0, afe_base + 0x0264); /* AFE_ADDA_PREDIS_CON1 */
	writel(0x83001802, afe_base + 0x0108); /* AFE_ADDA_DL_SRC2_CON0 48K */
	writel(0xf74f0000, afe_base + 0x010c); /* AFE_ADDA_DL_SRC2_CON1 */
	writel((10 << 8) | BIT(3), afe_base + 0x0034); /* AFE_I2S_CON1 */

	afe_update(0x0024, BIT(21), BIT(21)); /* AFE_CONN1: I05 -> O03 */
	afe_update(0x0028, BIT(6), BIT(6));   /* AFE_CONN2: I06 -> O04 */
	writel(0x348C28C2, afe_base + 0x01f0); /* AFE_SGEN_CON0: I05/I06 sine wave */

	afe_update(0x0108, BIT(0), BIT(0)); /* DL_SRC2 on */
	afe_update(0x0034, BIT(0), BIT(0)); /* I2S_CON1 on */
	afe_update(0x0124, BIT(0), BIT(0)); /* UL_DL_CON0 on */
	afe_update(0x04c4, BIT(4), 0);      /* FPGA_CFG1 */
	afe_update(0x0010, BIT(0), BIT(0)); /* AFE_DAC_CON0 global enable */

	/* 5. Switch PMIC audio pins (GPIO7,8,9 -> Mode 1) */
	set_pin_mode(7, 1);
	set_pin_mode(8, 1);
	set_pin_mode(9, 1);

	/* 6. Power on MT6323 PMIC DAC + Audio_Amp_Change */
	regmap_update_bits(pmic_regmap, MT6323_TOP_CKPDN0, BIT(0), BIT(0));
	regmap_write(pmic_regmap, MT6323_TOP_CKPDN1_CLR, 0x0100);
	udelay(250);

	regmap_write(pmic_regmap, AFE_PMIC_NEWIF_CFG0, 0x8330);
	regmap_update_bits(pmic_regmap, ABB_AFE_CON1, 0x000f, 0x000a);
	regmap_read(pmic_regmap, ABB_AFE_CON11, &val);
	regmap_update_bits(pmic_regmap, ABB_AFE_CON11, 0x0100, ((val & 1) ? 0 : 1) << 8);
	regmap_update_bits(pmic_regmap, ABB_AFE_CON0, 0x0001, 0x0001);

	/* DC comp + Analog Amp */
	regmap_update_bits(pmic_regmap, ABB_AFE_CON10, 0x0001, 0x0001);
	regmap_read(pmic_regmap, ABB_AFE_CON11, &val);
	regmap_update_bits(pmic_regmap, ABB_AFE_CON11, 0x0200, ((val & 2) ? 0 : 1) << 9);

	regmap_write(pmic_regmap, MT6323_AUDTOP_CON6, 0xF7F2);
	regmap_update_bits(pmic_regmap, MT6323_AUDTOP_CON0, 0xf000, 0x7000);
	regmap_write(pmic_regmap, MT6323_AUDTOP_CON5, 0x0014);
	regmap_write(pmic_regmap, MT6323_AUDTOP_CON4, 0x007C);
	mdelay(10);
	regmap_write(pmic_regmap, MT6323_AUDTOP_CON6, 0xF5BA);
	regmap_write(pmic_regmap, MT6323_AUDTOP_CON5, 0x2214);

	/* 7. Enable External Speaker Amp (GPIO28 -> HIGH) */
	set_gpio_out(28, 1);
	msleep(30);

	pr_info("douglas-audio-test: SGEN + MT6323 DAC + ExtAmp enabled! (ret=%d)\n", ret = 0);
	return 0;
}

static void __exit douglas_audio_test_exit(void)
{
	u32 val;

	set_gpio_out(28, 0);
	if (pmic_regmap) {
		regmap_write(pmic_regmap, MT6323_AUDTOP_CON5, 0x0014);
		regmap_write(pmic_regmap, MT6323_AUDTOP_CON6, 0xF7F2);
		regmap_write(pmic_regmap, MT6323_AUDTOP_CON4, 0x0000);
		regmap_update_bits(pmic_regmap, MT6323_AUDTOP_CON0, 0x1000, 0x0000);
		regmap_write(pmic_regmap, MT6323_AUDTOP_CON6, 0x37E2);
		regmap_update_bits(pmic_regmap, ABB_AFE_CON0, 0x0001, 0x0000);
		regmap_read(pmic_regmap, ABB_AFE_CON11, &val);
		regmap_update_bits(pmic_regmap, ABB_AFE_CON11, 0x0100, ((val & 1) ? 0 : 1) << 8);
		regmap_write(pmic_regmap, MT6323_TOP_CKPDN1_SET, 0x0100);
		regmap_update_bits(pmic_regmap, MT6323_TOP_CKPDN0, BIT(0), 0);
	}
	if (afe_base) {
		writel(0xf0000000, afe_base + 0x01f0);
		afe_update(0x0010, BIT(0), 0);
		iounmap(afe_base);
	}
	if (topckgen_base)
		iounmap(topckgen_base);
	set_pin_mode(7, 0);
	set_pin_mode(8, 0);
	set_pin_mode(9, 0);
	pr_info("douglas-audio-test: disabled\n");
}

module_init(douglas_audio_test_init);
module_exit(douglas_audio_test_exit);
MODULE_LICENSE("GPL");
