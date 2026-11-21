// SPDX-License-Identifier: GPL-2.0-only
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/mfd/mt6323/registers.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#define AFE_BASE_PHYS		0x11220000
#define AFE_SRAM_PHYS		0x11221000
#define AFE_SRAM_SIZE		0x6000
#define AFE_PCM_ALIGN		64

#define AUDIO_TOP_CON0		0x0000
#define AFE_DAC_CON0		0x0010
#define AFE_DAC_CON1		0x0014
#define AFE_CONN1		0x0024
#define AFE_CONN2		0x0028
#define AFE_I2S_CON1		0x0034
#define AFE_DL1_BASE		0x0040
#define AFE_DL1_CUR		0x0044
#define AFE_DL1_END		0x0048
#define AFE_CONN_24BIT		0x006c
#define AFE_ADDA_DL_SRC2_CON0	0x0108
#define AFE_ADDA_DL_SRC2_CON1	0x010c
#define AFE_ADDA_UL_DL_CON0	0x0124
#define AFE_SGEN_CON0		0x01f0
#define AFE_ADDA_PREDIS_CON0	0x0260
#define AFE_ADDA_PREDIS_CON1	0x0264
#define AFE_IRQ_MCU_CON		0x03a0
#define AFE_IRQ_STATUS		0x03a4
#define AFE_IRQ_CLR		0x03a8
#define AFE_IRQ_CNT1		0x03ac
#define AFE_MEMIF_PBUF_SIZE	0x03d8
#define FPGA_CFG1		0x04c4

#define ABB_AFE_CON0		0x4000
#define ABB_AFE_CON1		0x4002
#define ABB_AFE_CON10		0x4014
#define ABB_AFE_CON11		0x4016
#define AFE_PMIC_NEWIF_CFG0	0x4024

struct douglas_alsa {
	struct device *dev;
	void __iomem *afe_base;
	void __iomem *sram;
	struct regmap *scpsys;
	struct regmap *pctl_regmap;
	struct regmap *pmic_regmap;
	struct clk *clk_infra;
	struct clk *clk_aud_sel;
	struct clk *clk_intbus;
	struct clk *clk_mux2;
	struct clk *clk_apll2;
	struct snd_pcm_substream *substream;
	struct snd_soc_card card;
	struct mutex lock;
	spinlock_t irq_lock;
	int irq;
	bool running;
	bool dac_enabled;
	unsigned int volume;
};

static struct platform_device *g_pdev;

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

static int get_gic_spi_irq(u32 spi, u32 type)
{
	struct device_node *gic_np;
	struct irq_fwspec fwspec;
	int virq;

	gic_np = of_find_compatible_node(NULL, NULL, "mediatek,mt6577-sysirq");
	if (!gic_np)
		gic_np = of_find_node_by_name(NULL, "intpol-controller");
	if (!gic_np)
		return -ENODEV;

	fwspec.fwnode = of_node_to_fwnode(gic_np);
	fwspec.param_count = 3;
	fwspec.param[0] = 0; /* GIC_SPI */
	fwspec.param[1] = spi;
	fwspec.param[2] = type;
	virq = irq_create_fwspec_mapping(&fwspec);
	of_node_put(gic_np);
	return virq;
}

static void set_pin_mode(struct douglas_alsa *da, int pin, int mode)
{
	u32 reg = 0x0600 + ((pin / 5) << 4);
	u32 shift = 3 * (pin % 5);

	regmap_update_bits(da->pctl_regmap, reg, 0xf << shift, (mode & 0xf) << shift);
}

static void set_gpio_out(struct douglas_alsa *da, int pin, int val)
{
	u32 dir_reg = 0x0000 + ((pin / 16) << 4);
	u32 dout_reg = 0x0400 + ((pin / 16) << 4);
	u32 bit = BIT(pin % 16);

	set_pin_mode(da, pin, 0);
	regmap_update_bits(da->pctl_regmap, dir_reg, bit, bit);
	regmap_update_bits(da->pctl_regmap, dout_reg, bit, val ? bit : 0);
}

static inline void afe_update(struct douglas_alsa *da, u32 reg, u32 mask, u32 val)
{
	u32 v = readl(da->afe_base + reg);
	v = (v & ~mask) | (val & mask);
	writel(v, da->afe_base + reg);
}

static void douglas_apply_volume(struct douglas_alsa *da)
{
	unsigned int idx = min(da->volume, 7U);
	unsigned int regval = 7 - idx;

	regmap_update_bits(da->pmic_regmap, MT6323_AUDTOP_CON5,
			   0x7700, (regval << 12) | (regval << 8));
}

static void douglas_pmic_dac_enable(struct douglas_alsa *da, bool enable)
{
	u32 val = 0;

	if (da->dac_enabled == enable)
		return;

	if (enable) {
		regmap_update_bits(da->pmic_regmap, MT6323_TOP_CKPDN0, BIT(0), BIT(0));
		regmap_write(da->pmic_regmap, MT6323_TOP_CKPDN1_CLR, 0x0100);
		udelay(250);

		regmap_write(da->pmic_regmap, AFE_PMIC_NEWIF_CFG0, 0x8330);
		regmap_update_bits(da->pmic_regmap, ABB_AFE_CON1, 0x000f, 0x000a);
		regmap_read(da->pmic_regmap, ABB_AFE_CON11, &val);
		regmap_update_bits(da->pmic_regmap, ABB_AFE_CON11,
				   0x0100, ((val & 1) ? 0 : 1) << 8);
		regmap_update_bits(da->pmic_regmap, ABB_AFE_CON0, 0x0001, 0x0001);

		regmap_update_bits(da->pmic_regmap, ABB_AFE_CON10, 0x0001, 0x0001);
		regmap_read(da->pmic_regmap, ABB_AFE_CON11, &val);
		regmap_update_bits(da->pmic_regmap, ABB_AFE_CON11,
				   0x0200, ((val & 2) ? 0 : 1) << 9);

		regmap_write(da->pmic_regmap, MT6323_AUDTOP_CON6, 0xF7F2);
		regmap_update_bits(da->pmic_regmap, MT6323_AUDTOP_CON0, 0xf000, 0x7000);
		regmap_write(da->pmic_regmap, MT6323_AUDTOP_CON5, 0x0014);
		regmap_write(da->pmic_regmap, MT6323_AUDTOP_CON4, 0x007C);
		mdelay(10);
		regmap_write(da->pmic_regmap, MT6323_AUDTOP_CON6, 0xF5BA);
		regmap_write(da->pmic_regmap, MT6323_AUDTOP_CON5, 0x2214);
		douglas_apply_volume(da);
	} else {
		regmap_write(da->pmic_regmap, MT6323_AUDTOP_CON5, 0x0014);
		regmap_write(da->pmic_regmap, MT6323_AUDTOP_CON6, 0xF7F2);
		regmap_write(da->pmic_regmap, MT6323_AUDTOP_CON4, 0x0000);
		regmap_update_bits(da->pmic_regmap, MT6323_AUDTOP_CON0, 0x1000, 0x0000);
		regmap_write(da->pmic_regmap, MT6323_AUDTOP_CON6, 0x37E2);

		regmap_update_bits(da->pmic_regmap, ABB_AFE_CON0, 0x0001, 0x0000);
		regmap_read(da->pmic_regmap, ABB_AFE_CON11, &val);
		regmap_update_bits(da->pmic_regmap, ABB_AFE_CON11,
				   0x0100, ((val & 1) ? 0 : 1) << 8);
		regmap_write(da->pmic_regmap, MT6323_TOP_CKPDN1_SET, 0x0100);
		regmap_update_bits(da->pmic_regmap, MT6323_TOP_CKPDN0, BIT(0), 0);
	}
	da->dac_enabled = enable;
}

static irqreturn_t douglas_afe_irq(int irq, void *data)
{
	struct douglas_alsa *da = data;
	u32 status = readl(da->afe_base + AFE_IRQ_STATUS);

	if (!(status & BIT(0)))
		return IRQ_NONE;

	writel(BIT(0), da->afe_base + AFE_IRQ_CLR);
	return IRQ_WAKE_THREAD;
}

static irqreturn_t douglas_afe_irq_thread(int irq, void *data)
{
	struct douglas_alsa *da = data;
	struct snd_pcm_substream *substream = NULL;
	unsigned long flags;

	spin_lock_irqsave(&da->irq_lock, flags);
	if (da->running)
		substream = da->substream;
	spin_unlock_irqrestore(&da->irq_lock, flags);

	if (substream)
		snd_pcm_period_elapsed(substream);
	return IRQ_HANDLED;
}

static const struct snd_pcm_hardware douglas_pcm_hw = {
	.info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE,
	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 1,
	.channels_max = 2,
	.buffer_bytes_max = AFE_SRAM_SIZE,
	.period_bytes_min = AFE_PCM_ALIGN,
	.period_bytes_max = AFE_SRAM_SIZE / 2,
	.periods_min = 2,
	.periods_max = AFE_SRAM_SIZE / AFE_PCM_ALIGN,
};

static int douglas_pcm_open(struct snd_soc_component *comp,
			    struct snd_pcm_substream *substream)
{
	struct douglas_alsa *da = snd_soc_component_get_drvdata(comp);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret;

	if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK)
		return -EINVAL;

	mutex_lock(&da->lock);
	if (da->substream) {
		mutex_unlock(&da->lock);
		return -EBUSY;
	}

	regmap_write(da->scpsys, 0x029c, 0x0000000d);
	if (!IS_ERR(da->clk_infra)) clk_prepare_enable(da->clk_infra);
	if (!IS_ERR(da->clk_intbus)) clk_prepare_enable(da->clk_intbus);
	if (!IS_ERR(da->clk_aud_sel)) clk_prepare_enable(da->clk_aud_sel);
	if (!IS_ERR(da->clk_mux2) && !IS_ERR(da->clk_apll2)) {
		clk_set_parent(da->clk_mux2, da->clk_apll2);
		clk_prepare_enable(da->clk_mux2);
	}
	afe_update(da, AUDIO_TOP_CON0, BIT(14), BIT(14));
	afe_update(da, AUDIO_TOP_CON0, BIT(2) | BIT(6) | BIT(9) | BIT(25) | BIT(26), 0);

	runtime->hw = douglas_pcm_hw;
	runtime->dma_area = (void __force *)da->sram;
	runtime->dma_addr = AFE_SRAM_PHYS;
	runtime->dma_bytes = AFE_SRAM_SIZE;
	ret = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, AFE_PCM_ALIGN);
	if (!ret)
		ret = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, AFE_PCM_ALIGN);
	if (!ret)
		da->substream = substream;
	mutex_unlock(&da->lock);
	return ret;
}

static int douglas_pcm_close(struct snd_soc_component *comp,
			     struct snd_pcm_substream *substream)
{
	struct douglas_alsa *da = snd_soc_component_get_drvdata(comp);
	unsigned long flags;

	mutex_lock(&da->lock);
	set_gpio_out(da, 28, 0);
	afe_update(da, AFE_IRQ_MCU_CON, BIT(0), 0);
	writel(BIT(0), da->afe_base + AFE_IRQ_CLR);
	afe_update(da, AFE_DAC_CON0, BIT(1) | BIT(0), 0);
	afe_update(da, AFE_ADDA_DL_SRC2_CON0, BIT(0), 0);
	afe_update(da, AFE_I2S_CON1, BIT(0), 0);
	afe_update(da, AFE_ADDA_UL_DL_CON0, BIT(0), 0);
	douglas_pmic_dac_enable(da, false);
	set_pin_mode(da, 7, 0);
	set_pin_mode(da, 8, 0);
	set_pin_mode(da, 9, 0);

	spin_lock_irqsave(&da->irq_lock, flags);
	da->running = false;
	da->substream = NULL;
	spin_unlock_irqrestore(&da->irq_lock, flags);
	mutex_unlock(&da->lock);
	return 0;
}

static int douglas_pcm_hw_params(struct snd_soc_component *comp,
				 struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct douglas_alsa *da = snd_soc_component_get_drvdata(comp);
	size_t bytes = params_buffer_bytes(params);

	if (bytes > AFE_SRAM_SIZE || !IS_ALIGNED(bytes, AFE_PCM_ALIGN))
		return -EINVAL;

	substream->runtime->dma_area = (void __force *)da->sram;
	substream->runtime->dma_addr = AFE_SRAM_PHYS;
	substream->runtime->dma_bytes = bytes;
	writel(AFE_SRAM_PHYS, da->afe_base + AFE_DL1_BASE);
	writel(AFE_SRAM_PHYS + bytes - 1, da->afe_base + AFE_DL1_END);
	return 0;
}

static int douglas_pcm_prepare(struct snd_soc_component *comp,
			       struct snd_pcm_substream *substream)
{
	struct douglas_alsa *da = snd_soc_component_get_drvdata(comp);
	bool mono = substream->runtime->channels == 1;
	snd_pcm_format_t fmt = substream->runtime->format;
	bool wide = (fmt == SNDRV_PCM_FORMAT_S24_LE || fmt == SNDRV_PCM_FORMAT_S32_LE);
	u32 fetch = (fmt == SNDRV_PCM_FORMAT_S32_LE) ? (3 << 16) :
		    (fmt == SNDRV_PCM_FORMAT_S24_LE) ? (1 << 16) : 0;

	mutex_lock(&da->lock);
	memset_io(da->sram, 0, substream->runtime->dma_bytes);

	afe_update(da, AFE_MEMIF_PBUF_SIZE, GENMASK(17, 16), fetch);
	afe_update(da, AFE_DAC_CON1, GENMASK(3, 0) | GENMASK(11, 8) | BIT(21),
		   10 | (10 << 8) | (mono ? BIT(21) : 0));
	afe_update(da, AFE_CONN_24BIT, BIT(0) | BIT(1) | BIT(3) | BIT(4),
		   wide ? (BIT(0) | BIT(1) | BIT(3) | BIT(4)) : 0);

	writel(0, da->afe_base + AFE_ADDA_PREDIS_CON0);
	writel(0, da->afe_base + AFE_ADDA_PREDIS_CON1);
	writel(0x83001802, da->afe_base + AFE_ADDA_DL_SRC2_CON0);
	writel(0xf74f0000, da->afe_base + AFE_ADDA_DL_SRC2_CON1);
	writel((10 << 8) | BIT(3) | (wide ? BIT(1) : 0), da->afe_base + AFE_I2S_CON1);

	writel((u32)substream->runtime->period_size, da->afe_base + AFE_IRQ_CNT1);
	afe_update(da, AFE_IRQ_MCU_CON, GENMASK(7, 4), 10 << 4);
	afe_update(da, AFE_CONN1, BIT(21), BIT(21));
	afe_update(da, AFE_CONN2, BIT(6), BIT(6));
	writel(0xf0000000, da->afe_base + AFE_SGEN_CON0);

	set_pin_mode(da, 7, 1);
	set_pin_mode(da, 8, 1);
	set_pin_mode(da, 9, 1);
	douglas_pmic_dac_enable(da, true);
	mutex_unlock(&da->lock);
	return 0;
}

static int douglas_pcm_trigger(struct snd_soc_component *comp,
			       struct snd_pcm_substream *substream, int cmd)
{
	struct douglas_alsa *da = snd_soc_component_get_drvdata(comp);
	unsigned long flags;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		writel(BIT(0), da->afe_base + AFE_IRQ_CLR);
		afe_update(da, AFE_ADDA_DL_SRC2_CON0, BIT(0), BIT(0));
		afe_update(da, AFE_I2S_CON1, BIT(0), BIT(0));
		afe_update(da, AFE_ADDA_UL_DL_CON0, BIT(0), BIT(0));
		afe_update(da, FPGA_CFG1, BIT(4), 0);
		afe_update(da, AFE_DAC_CON0, BIT(1) | BIT(0), BIT(1) | BIT(0));
		afe_update(da, AFE_IRQ_MCU_CON, BIT(0), BIT(0));
		set_gpio_out(da, 28, 1);
		spin_lock_irqsave(&da->irq_lock, flags);
		da->running = true;
		spin_unlock_irqrestore(&da->irq_lock, flags);
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		spin_lock_irqsave(&da->irq_lock, flags);
		da->running = false;
		spin_unlock_irqrestore(&da->irq_lock, flags);
		set_gpio_out(da, 28, 0);
		afe_update(da, AFE_IRQ_MCU_CON, BIT(0), 0);
		writel(BIT(0), da->afe_base + AFE_IRQ_CLR);
		afe_update(da, AFE_DAC_CON0, BIT(1) | BIT(0), 0);
		afe_update(da, AFE_ADDA_DL_SRC2_CON0, BIT(0), 0);
		afe_update(da, AFE_I2S_CON1, BIT(0), 0);
		afe_update(da, AFE_ADDA_UL_DL_CON0, BIT(0), 0);
		return 0;
	default:
		return -EINVAL;
	}
}

static snd_pcm_uframes_t douglas_pcm_pointer(struct snd_soc_component *comp,
					     struct snd_pcm_substream *substream)
{
	struct douglas_alsa *da = snd_soc_component_get_drvdata(comp);
	u32 cur = readl(da->afe_base + AFE_DL1_CUR);
	u32 off;

	if (cur < AFE_SRAM_PHYS || cur >= AFE_SRAM_PHYS + substream->runtime->dma_bytes)
		off = 0;
	else
		off = cur - AFE_SRAM_PHYS;
	return bytes_to_frames(substream->runtime, off);
}

static int douglas_vol_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 7;
	return 0;
}

static int douglas_vol_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct douglas_alsa *da = snd_soc_card_get_drvdata(card);
	ucontrol->value.integer.value[0] = da->volume;
	return 0;
}

static int douglas_vol_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_card *card = snd_kcontrol_chip(kcontrol);
	struct douglas_alsa *da = snd_soc_card_get_drvdata(card);
	unsigned int val = min_t(unsigned int, ucontrol->value.integer.value[0], 7);

	mutex_lock(&da->lock);
	da->volume = val;
	if (da->dac_enabled)
		douglas_apply_volume(da);
	mutex_unlock(&da->lock);
	return 1;
}

static const struct snd_kcontrol_new douglas_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Volume",
		.info = douglas_vol_info,
		.get = douglas_vol_get,
		.put = douglas_vol_put,
	},
};

static const struct snd_soc_component_driver douglas_afe_component = {
	.name = "douglas-snd3",
	.open = douglas_pcm_open,
	.close = douglas_pcm_close,
	.hw_params = douglas_pcm_hw_params,
	.prepare = douglas_pcm_prepare,
	.trigger = douglas_pcm_trigger,
	.pointer = douglas_pcm_pointer,
};

static struct snd_soc_dai_driver douglas_afe_dai = {
	.name = "douglas-dl1-dai",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE,
	},
};

SND_SOC_DAILINK_DEFS(douglas_playback,
	DAILINK_COMP_ARRAY(COMP_CPU("douglas-dl1-dai")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("douglas-snd3")));

static struct snd_soc_dai_link douglas_dai_links[] = {
	{
		.name = "Douglas Playback",
		.stream_name = "Douglas Playback",
		.playback_only = 1,
		.nonatomic = true,
		SND_SOC_DAILINK_REG(douglas_playback),
	},
};

static int douglas_alsa_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *pmic_dev;
	struct mt6397_chip *chip;
	struct douglas_alsa *da;
	int ret;

	da = devm_kzalloc(dev, sizeof(*da), GFP_KERNEL);
	if (!da)
		return -ENOMEM;
	da->dev = dev;
	mutex_init(&da->lock);
	spin_lock_init(&da->irq_lock);
	da->volume = 5;

	da->scpsys = syscon_regmap_lookup_by_compatible("mediatek,mt8163-scpsys");
	da->pctl_regmap = syscon_regmap_lookup_by_compatible("mediatek,mt8163-pctl-a-syscfg");
	if (IS_ERR(da->scpsys) || IS_ERR(da->pctl_regmap))
		return -ENODEV;

	pmic_dev = bus_find_device_by_name(&platform_bus_type, NULL, "1000d000.pwrap:mt6323");
	if (!pmic_dev)
		return -ENODEV;
	chip = dev_get_drvdata(pmic_dev);
	da->pmic_regmap = chip ? chip->regmap : dev_get_regmap(pmic_dev, NULL);
	put_device(pmic_dev);
	if (!da->pmic_regmap)
		return -ENODEV;

	da->afe_base = devm_ioremap(dev, AFE_BASE_PHYS, 0x1000);
	da->sram = devm_ioremap(dev, AFE_SRAM_PHYS, AFE_SRAM_SIZE);
	if (!da->afe_base || !da->sram)
		return -ENOMEM;

	da->clk_infra = get_dt_clk("mediatek,mt8163-infracfg", 48);
	da->clk_aud_sel = get_dt_clk("mediatek,mt8163-topckgen", 79);
	da->clk_intbus = get_dt_clk("mediatek,mt8163-topckgen", 80);
	da->clk_mux2 = get_dt_clk("mediatek,mt8163-topckgen", 88);
	da->clk_apll2 = get_dt_clk("mediatek,mt8163-topckgen", 11);

	da->irq = get_gic_spi_irq(142, IRQ_TYPE_LEVEL_HIGH);
	if (da->irq <= 0)
		return da->irq ? da->irq : -ENODEV;

	ret = devm_request_threaded_irq(dev, da->irq, douglas_afe_irq,
					douglas_afe_irq_thread, IRQF_ONESHOT,
					"douglas-afe", da);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, da);

	ret = devm_snd_soc_register_component(dev, &douglas_afe_component,
					      &douglas_afe_dai, 1);
	if (ret)
		return ret;

	da->card.name = "mt8163-douglas";
	da->card.owner = THIS_MODULE;
	da->card.dev = dev;
	da->card.dai_link = douglas_dai_links;
	da->card.num_links = ARRAY_SIZE(douglas_dai_links);
	da->card.controls = douglas_controls;
	da->card.num_controls = ARRAY_SIZE(douglas_controls);
	snd_soc_card_set_drvdata(&da->card, da);

	ret = devm_snd_soc_register_card(dev, &da->card);
	if (ret)
		return ret;

	dev_info(dev, "Douglas ALSA sound card registered (irq=%d)\n", da->irq);
	return 0;
}

static struct platform_driver douglas_alsa_driver = {
	.probe = douglas_alsa_probe,
	.driver = {
		.name = "douglas-snd3",
	},
};

static int __init douglas_alsa_init(void)
{
	int ret = platform_driver_register(&douglas_alsa_driver);
	if (ret)
		return ret;
	g_pdev = platform_device_register_simple("douglas-snd3", -1, NULL, 0);
	if (IS_ERR(g_pdev)) {
		platform_driver_unregister(&douglas_alsa_driver);
		return PTR_ERR(g_pdev);
	}
	return 0;
}

static void __exit douglas_alsa_exit(void)
{
	platform_device_unregister(g_pdev);
	platform_driver_unregister(&douglas_alsa_driver);
}

module_init(douglas_alsa_init);
module_exit(douglas_alsa_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Amazon Fire HD 8 (douglas) loadable ALSA sound card");
