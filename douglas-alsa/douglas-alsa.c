// SPDX-License-Identifier: GPL-2.0-only
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/mfd/mt6323/registers.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_domain.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#define PCTL_BASE_PHYS		0x10005000
#define AFE_BASE_PHYS		0x11220000
#define AFE_SRAM_PHYS		0x11221000
#define AFE_SRAM_SIZE		0x6000		/* playback (DL1) region */
#define AFE_PCM_ALIGN		64

/*
 * Capture (VUL / UL1) gets its own slice of AFE SRAM so it can run at the
 * same time as playback. The vendor DTB maps 0x9000 of SRAM for
 * mt_soc_dl1_pcm (reg = <0 0x11220000 0 0x1000>, <0 0x11221000 0 0x9000>),
 * so 0x6000 playback + 0x2000 capture fits with room to spare.
 */
#define AFE_UL_SRAM_OFF		AFE_SRAM_SIZE
#define AFE_UL_SRAM_SIZE	0x2000
#define AFE_UL_SRAM_PHYS	(AFE_SRAM_PHYS + AFE_UL_SRAM_OFF)
#define AFE_SRAM_MAP_SIZE	(AFE_SRAM_SIZE + AFE_UL_SRAM_SIZE)

#define AUDIO_TOP_CON0		0x0000
#define AFE_DAC_CON0		0x0010
#define AFE_DAC_CON1		0x0014
#define AFE_CONN1		0x0024
#define AFE_CONN2		0x0028
#define AFE_CONN3		0x002c
#define AFE_I2S_CON1		0x0034
#define AFE_DL1_BASE		0x0040
#define AFE_DL1_CUR		0x0044
#define AFE_DL1_END		0x0048
#define AFE_CONN_24BIT		0x006c
#define AFE_VUL_BASE		0x0080
#define AFE_VUL_END		0x0088
#define AFE_VUL_CUR		0x008c
#define AFE_ADDA_DL_SRC2_CON0	0x0108
#define AFE_ADDA_DL_SRC2_CON1	0x010c
#define AFE_ADDA_UL_SRC_CON0	0x0114
#define AFE_ADDA_NEWIF_CFG0	0x0138
#define AFE_ADDA_NEWIF_CFG1	0x013c
#define AFE_ADDA_UL_DL_CON0	0x0124
#define AFE_SGEN_CON0		0x01f0
#define AFE_ADDA_PREDIS_CON0	0x0260
#define AFE_ADDA_PREDIS_CON1	0x0264
#define AFE_MEMIF_PBUF_SIZE	0x03d8
#define FPGA_CFG1		0x04c4

#define ABB_AFE_CON0		0x4000
#define ABB_AFE_CON1		0x4002
#define ABB_AFE_CON10		0x4014
#define ABB_AFE_CON11		0x4016
#define ABB_AFE_CON9		0x4012
#define AFE_PMIC_NEWIF_CFG0	0x4024
#define AFE_PMIC_NEWIF_CFG2	0x4028

/*
 * Uplink mic mode. On MT8163 the vendor's TurnOnADcPowerDCC() is a stub that
 * logs "not support", so despite the codec advertising five analog mic modes
 * only two are real here: ACC (analog, AC-coupled) and DMIC (digital mic).
 */
#define DOUGLAS_MIC_MODE_ACC	0
#define DOUGLAS_MIC_MODE_DMIC	1

struct douglas_alsa {
	struct snd_soc_card card;
	struct device *dev;
	void __iomem *pctl_base;
	void __iomem *afe_base;
	void __iomem *sram;
	struct regmap *scpsys;
	struct regmap *pmic_regmap;
	struct clk *clk_infra;
	struct clk *clk_aud_sel;
	struct clk *clk_intbus;
	struct clk *clk_mux2;
	struct clk *clk_apll2;
	struct pinctrl *pctl;
	struct pinctrl_state *pins_aud;
	struct pinctrl_state *pins_gpio;
	struct snd_pcm_substream *substream;
	struct snd_pcm_substream *cap_substream;
	struct mutex lock;
	struct hrtimer timer;
	struct work_struct period_work;
	struct work_struct cap_period_work;
	u32 last_period;
	u32 cap_last_period;
	bool running;
	bool cap_running;
	bool clocks_on;
	bool dac_enabled;
	bool adc_enabled;
	unsigned int volume;
	/*
	 * Runtime-selectable so the working combination can be found by
	 * sweeping from userspace instead of rebuilding the module. The board's
	 * real choice is not in the kernel tree - the Android HAL picks it at
	 * runtime from a parameter store.
	 */
	unsigned int mic_mode;	/* DOUGLAS_MIC_MODE_* */
	unsigned int mic_mux;	/* raw AUDTOP_CON0[3:0]; vendor uses 0, 1, 4 */
	unsigned int mic_gain;	/* 0..5 = {-6, 0, 6, 12, 18, 24} dB */
};

static struct platform_device *g_pdev;
static struct douglas_alsa *g_da;

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

static void set_pin_mode(struct douglas_alsa *da, int pin, int mode)
{
	u32 reg = 0x0600 + ((pin / 5) << 4);
	u32 shift = 3 * (pin % 5);
	u32 v = readl(da->pctl_base + reg);

	v &= ~(0x7U << shift);
	v |= ((mode & 0x7U) << shift);
	writel(v, da->pctl_base + reg);
}

/*
 * Pins 7/8/9 are AUD_CLK_MOSI / AUD_DAT_MISO / AUD_DAT_MOSI (vendor DTB
 * audclk/audmiso/audmosi-gpio). Function 1 is the PMIC audio link; both
 * playback and capture need it, so only drop back to GPIO when neither
 * stream is open.
 */
static void douglas_set_aud_pins(struct douglas_alsa *da, bool func)
{
	if (da->pins_aud && da->pins_gpio) {
		pinctrl_select_state(da->pctl, func ? da->pins_aud : da->pins_gpio);
		return;
	}
	set_pin_mode(da, 7, func ? 1 : 0);
	set_pin_mode(da, 8, func ? 1 : 0);
	set_pin_mode(da, 9, func ? 1 : 0);
}

static void set_gpio_out(struct douglas_alsa *da, int pin, int val)
{
	u32 dir_reg = 0x0000 + ((pin / 16) << 4);
	u32 dout_reg = 0x0400 + ((pin / 16) << 4);
	u32 bit = BIT(pin % 16);
	u32 v;

	set_pin_mode(da, pin, 0);
	v = readl(da->pctl_base + dir_reg) | bit;
	writel(v, da->pctl_base + dir_reg);

	v = readl(da->pctl_base + dout_reg);
	if (val)
		v |= bit;
	else
		v &= ~bit;
	writel(v, da->pctl_base + dout_reg);
}

static void douglas_init_hp_detect_gpio(struct douglas_alsa *da)
{
	u32 bit = BIT(26 % 16); /* GPIO26 = bit 10 in bank 1 */

	set_pin_mode(da, 26, 0);
	/* Input */
	writel(readl(da->pctl_base + 0x0010) & ~bit, da->pctl_base + 0x0010);
	/* Pull-down (PULLSEL=0, PULLEN=1) per vendor DTB eint4 */
	writel(readl(da->pctl_base + 0x0210) & ~bit, da->pctl_base + 0x0210);
	writel(readl(da->pctl_base + 0x0110) | bit,  da->pctl_base + 0x0110);
}

static void douglas_update_amp_route(struct douglas_alsa *da, bool active)
{
	bool hp;

	if (!active) {
		set_gpio_out(da, 28, 0);
		set_gpio_out(da, 44, 0);
		return;
	}
	hp = !!(readl(da->pctl_base + 0x0510) & BIT(10));
	set_gpio_out(da, 28, hp ? 0 : 1);
	set_gpio_out(da, 44, hp ? 1 : 0);
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
	unsigned int regval = idx;

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

/*
 * Analog + digital uplink path on the MT6323, ported from Amazon's
 * mt_soc_codec_63xx.c TurnOnADcPowerACC() (ADC1 / main mic, mic-source mux 0):
 *
 *	Topck_Enable(true);
 *	Ana_Set_Reg(AFE_PMIC_NEWIF_CFG2, 0x302F | (GetULNewIFFrequency(sr) << 10), 0xffff);
 *	Ana_Set_Reg(ABB_AFE_CON1, GetULFrequency(sr) << 4, 0x0010);
 *	TopCtlChangeTrigger();
 *	Ana_Set_Reg(ABB_AFE_CON0, 0x0002, 0x0002);	// turn on UL
 *	Ana_Set_Reg(AUDTOP_CON0, 0x7800, 0x7f80);	// Enable LCH 1.4v, 2.4V
 *	Ana_Set_Reg(AUDTOP_CON2, 0x00F0, 0x00ff);	// Enable RCH 1.4V, 2.4V
 *	Ana_Set_Reg(AUDTOP_CON3, 0x0000, 0x00000100);
 *	Ana_Set_Reg(AUDTOP_CON0, 0, 0x0000000f);	// main_mic
 *	Ana_Set_Reg(AUDTOP_CON8, 0x0008, 0x0008);	// MICBIAS
 *	Ana_Set_Reg(AUDTOP_CON0, 0x0180, 0x0180);	// Enable LCH ADC, PGA
 *	Ana_Set_Reg(AUDTOP_CON2, 0x0003, 0x0003);	// Enable RCH ADC, PGA
 *
 * At 48 kHz GetULNewIFFrequency() = 3 and GetULFrequency() = 1.
 */
static void douglas_pmic_adc_enable(struct douglas_alsa *da, bool enable)
{
	u32 val = 0;

	if (da->adc_enabled == enable)
		return;

	if (enable) {
		/* Topck_Enable(true) - same sequence the DAC path uses. */
		regmap_update_bits(da->pmic_regmap, MT6323_TOP_CKPDN0, BIT(0), BIT(0));
		regmap_write(da->pmic_regmap, MT6323_TOP_CKPDN1_CLR, 0x0100);
		udelay(250);

		/* config UL up8x_rxif adc voice mode, 48 kHz */
		regmap_write(da->pmic_regmap, AFE_PMIC_NEWIF_CFG2,
			     0x302f | (3 << 10));
		regmap_update_bits(da->pmic_regmap, ABB_AFE_CON1,
				   0x0010, 1 << 4);
		/* TopCtlChangeTrigger() */
		regmap_read(da->pmic_regmap, ABB_AFE_CON11, &val);
		regmap_update_bits(da->pmic_regmap, ABB_AFE_CON11,
				   0x0100, ((val & 1) ? 0 : 1) << 8);

		/* SetMicPGAGain(): LCH in AUDTOP_CON0[6:4], RCH in CON1[10:8] */
		regmap_update_bits(da->pmic_regmap, MT6323_AUDTOP_CON0,
				   0x0070, (da->mic_gain & 0x7) << 4);
		regmap_update_bits(da->pmic_regmap, MT6323_AUDTOP_CON1,
				   0x0700, (da->mic_gain & 0x7) << 8);

		if (da->mic_mode == DOUGLAS_MIC_MODE_DMIC) {
			/* TurnOnADcPowerDmic(): no analog ADC/PGA at all */
			regmap_update_bits(da->pmic_regmap, ABB_AFE_CON9,
					   0x0011, 0x0011);
			regmap_update_bits(da->pmic_regmap, ABB_AFE_CON0,
					   0x0002, 0x0002);
			regmap_update_bits(da->pmic_regmap, MT6323_AUDTOP_CON8,
					   0x03ff, 0x020c);
		} else {
			/* TurnOnADcPowerACC(), ADC1 branch */
			regmap_update_bits(da->pmic_regmap, ABB_AFE_CON0,
					   0x0002, 0x0002);
			regmap_update_bits(da->pmic_regmap, MT6323_AUDTOP_CON0, 0x7f80, 0x7800);
			regmap_update_bits(da->pmic_regmap, MT6323_AUDTOP_CON2, 0x00ff, 0x00f0);
			regmap_update_bits(da->pmic_regmap, MT6323_AUDTOP_CON3, 0x0100, 0x0000);
			regmap_update_bits(da->pmic_regmap, MT6323_AUDTOP_CON0,
					   0x000f, da->mic_mux & 0x000f);
			regmap_update_bits(da->pmic_regmap, MT6323_AUDTOP_CON8, 0x0008, 0x0008);
			regmap_update_bits(da->pmic_regmap, MT6323_AUDTOP_CON0, 0x0180, 0x0180);
			regmap_update_bits(da->pmic_regmap, MT6323_AUDTOP_CON2, 0x0003, 0x0003);
		}
	} else {
		regmap_update_bits(da->pmic_regmap, ABB_AFE_CON9, 0x0010, 0x0000);
		regmap_update_bits(da->pmic_regmap, MT6323_AUDTOP_CON8, 0x000c, 0x0000);
		regmap_update_bits(da->pmic_regmap, MT6323_AUDTOP_CON0, 0x0180, 0x0000);
		regmap_update_bits(da->pmic_regmap, MT6323_AUDTOP_CON2, 0x0003, 0x0000);
		/*
		 * Only drop the 1.4V/2.4V common mode if playback is not using
		 * it; TurnOnADcPowerACC() keeps 1.4V when GetDLStatus() is true.
		 */
		regmap_update_bits(da->pmic_regmap, MT6323_AUDTOP_CON0, 0x7f80,
				   da->dac_enabled ? 0x7000 : 0x6000);
		regmap_update_bits(da->pmic_regmap, MT6323_AUDTOP_CON2, 0x00ff, 0x00c0);
		regmap_update_bits(da->pmic_regmap, MT6323_AUDTOP_CON3, 0x0100, 0x0000);
		regmap_update_bits(da->pmic_regmap, MT6323_AUDTOP_CON8, 0x0008, 0x0000);
		regmap_update_bits(da->pmic_regmap, ABB_AFE_CON0, 0x0002, 0x0000);
		if (!da->dac_enabled) {
			regmap_write(da->pmic_regmap, MT6323_TOP_CKPDN1_SET, 0x0100);
			regmap_update_bits(da->pmic_regmap, MT6323_TOP_CKPDN0, BIT(0), 0);
		}
	}
	da->adc_enabled = enable;
}

static void douglas_cap_period_work(struct work_struct *work)
{
	struct douglas_alsa *da = g_da;
	struct snd_pcm_substream *substream;

	if (!da)
		return;
	substream = READ_ONCE(da->cap_substream);
	if (READ_ONCE(da->cap_running) && substream)
		snd_pcm_period_elapsed(substream);
}

static void douglas_period_work(struct work_struct *work)
{
	struct douglas_alsa *da = g_da;
	struct snd_pcm_substream *substream;

	if (!da)
		return;
	substream = READ_ONCE(da->substream);
	if (READ_ONCE(da->running) && substream)
		snd_pcm_period_elapsed(substream);
}

static enum hrtimer_restart douglas_timer_cb(struct hrtimer *timer)
{
	struct douglas_alsa *da = g_da;
	struct snd_pcm_substream *substream;
	bool active = false;

	if (!da)
		return HRTIMER_NORESTART;

	substream = READ_ONCE(da->substream);
	if (READ_ONCE(da->running) && substream && substream->runtime) {
		u32 cur = readl(da->afe_base + AFE_DL1_CUR);
		u32 period_bytes = frames_to_bytes(substream->runtime,
						   substream->runtime->period_size);
		u32 off = (cur >= AFE_SRAM_PHYS && cur < AFE_SRAM_PHYS + substream->runtime->dma_bytes)
			  ? (cur - AFE_SRAM_PHYS) : 0;
		u32 period = period_bytes ? (off / period_bytes) : 0;

		if (period != da->last_period) {
			da->last_period = period;
			douglas_update_amp_route(da, true);
			schedule_work(&da->period_work);
		}
		active = true;
	}

	substream = READ_ONCE(da->cap_substream);
	if (READ_ONCE(da->cap_running) && substream && substream->runtime) {
		u32 cur = readl(da->afe_base + AFE_VUL_CUR);
		u32 period_bytes = frames_to_bytes(substream->runtime,
						   substream->runtime->period_size);
		u32 off = (cur >= AFE_UL_SRAM_PHYS &&
			   cur < AFE_UL_SRAM_PHYS + substream->runtime->dma_bytes)
			  ? (cur - AFE_UL_SRAM_PHYS) : 0;
		u32 period = period_bytes ? (off / period_bytes) : 0;

		if (period != da->cap_last_period) {
			da->cap_last_period = period;
			schedule_work(&da->cap_period_work);
		}
		active = true;
	}

	if (!active)
		return HRTIMER_NORESTART;

	hrtimer_forward_now(timer, ms_to_ktime(5));
	return HRTIMER_RESTART;
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

/*
 * Capture runs the ADDA uplink at a fixed 48 kHz, 16-bit: the vendor's
 * mt_soc_pcm_capture.c programs the VUL memif with AFE_WLEN_16_BIT and
 * SetoutputConnectionFormat(OUTPUT_DATA_FORMAT_16BIT, O09/O10).
 */
static const struct snd_pcm_hardware douglas_capture_hw = {
	.info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 1,
	.channels_max = 2,
	.buffer_bytes_max = AFE_UL_SRAM_SIZE,
	.period_bytes_min = AFE_PCM_ALIGN,
	.period_bytes_max = AFE_UL_SRAM_SIZE / 2,
	.periods_min = 2,
	.periods_max = AFE_UL_SRAM_SIZE / AFE_PCM_ALIGN,
};

static int douglas_pcm_open(struct snd_soc_component *comp,
			    struct snd_pcm_substream *substream)
{
	struct douglas_alsa *da = g_da;
	struct snd_pcm_runtime *runtime = substream->runtime;
	bool capture = substream->stream == SNDRV_PCM_STREAM_CAPTURE;
	int ret;

	if (!da)
		return -EINVAL;

	mutex_lock(&da->lock);
	if (capture ? (da->cap_substream != NULL) : (da->substream != NULL)) {
		mutex_unlock(&da->lock);
		return -EBUSY;
	}

	if (!da->clocks_on) {
		if (!da->dev->pm_domain)
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
		da->clocks_on = true;
	}

	runtime->hw = capture ? douglas_capture_hw : douglas_pcm_hw;
	runtime->dma_area = (void __force *)(capture ? da->sram + AFE_UL_SRAM_OFF
						     : da->sram);
	runtime->dma_addr = capture ? AFE_UL_SRAM_PHYS : AFE_SRAM_PHYS;
	runtime->dma_bytes = capture ? AFE_UL_SRAM_SIZE : AFE_SRAM_SIZE;
	ret = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, AFE_PCM_ALIGN);
	if (!ret)
		ret = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, AFE_PCM_ALIGN);
	if (!ret) {
		if (capture)
			WRITE_ONCE(da->cap_substream, substream);
		else
			WRITE_ONCE(da->substream, substream);
	}
	mutex_unlock(&da->lock);
	return ret;
}

static int douglas_pcm_close(struct snd_soc_component *comp,
			     struct snd_pcm_substream *substream)
{
	struct douglas_alsa *da = g_da;

	if (!da)
		return 0;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		WRITE_ONCE(da->cap_running, false);
		WRITE_ONCE(da->cap_substream, NULL);
		mutex_lock(&da->lock);
		afe_update(da, AFE_DAC_CON0,
			   READ_ONCE(da->running) ? BIT(3) : (BIT(3) | BIT(0)), 0);
		afe_update(da, AFE_ADDA_UL_SRC_CON0, BIT(0), 0);
		afe_update(da, AFE_CONN3, BIT(0) | BIT(3), 0);
		if (!da->dac_enabled)
			afe_update(da, AFE_ADDA_UL_DL_CON0, BIT(0), 0);
		douglas_pmic_adc_enable(da, false);
		if (!READ_ONCE(da->substream))
			douglas_set_aud_pins(da, false);
		mutex_unlock(&da->lock);
		return 0;
	}

	WRITE_ONCE(da->running, false);
	WRITE_ONCE(da->substream, NULL);
	if (!READ_ONCE(da->cap_running))
		hrtimer_cancel(&da->timer);

	mutex_lock(&da->lock);
	douglas_update_amp_route(da, false);
	afe_update(da, AFE_DAC_CON0, BIT(1) | BIT(0), 0);
	afe_update(da, AFE_ADDA_DL_SRC2_CON0, BIT(0), 0);
	afe_update(da, AFE_I2S_CON1, BIT(0), 0);
	if (!da->adc_enabled)
		afe_update(da, AFE_ADDA_UL_DL_CON0, BIT(0), 0);
	douglas_pmic_dac_enable(da, false);
	if (!READ_ONCE(da->cap_substream))
		douglas_set_aud_pins(da, false);
	mutex_unlock(&da->lock);
	return 0;
}

static int douglas_pcm_hw_params(struct snd_soc_component *comp,
				 struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct douglas_alsa *da = g_da;
	size_t bytes = params_buffer_bytes(params);

	if (!da || !IS_ALIGNED(bytes, AFE_PCM_ALIGN))
		return -EINVAL;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		if (bytes > AFE_UL_SRAM_SIZE)
			return -EINVAL;
		substream->runtime->dma_area = (void __force *)(da->sram + AFE_UL_SRAM_OFF);
		substream->runtime->dma_addr = AFE_UL_SRAM_PHYS;
		substream->runtime->dma_bytes = bytes;
		writel(AFE_UL_SRAM_PHYS, da->afe_base + AFE_VUL_BASE);
		writel(AFE_UL_SRAM_PHYS + bytes - 1, da->afe_base + AFE_VUL_END);
		return 0;
	}

	if (bytes > AFE_SRAM_SIZE)
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
	struct douglas_alsa *da = g_da;
	bool mono = substream->runtime->channels == 1;
	snd_pcm_format_t fmt = substream->runtime->format;
	bool wide = (fmt == SNDRV_PCM_FORMAT_S24_LE || fmt == SNDRV_PCM_FORMAT_S32_LE);
	u32 fetch = (fmt == SNDRV_PCM_FORMAT_S32_LE) ? (3 << 16) :
		    (fmt == SNDRV_PCM_FORMAT_S24_LE) ? (1 << 16) : 0;

	if (!da)
		return -ENODEV;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		mutex_lock(&da->lock);
		memset_io(da->sram + AFE_UL_SRAM_OFF, 0,
			  substream->runtime->dma_bytes);

		/*
		 * VUL memif: rate code 10 = 48 kHz in AFE_DAC_CON1[19:16]
		 * (same encoding DL1 uses in [3:0]), mono flag in bit 27.
		 */
		afe_update(da, AFE_DAC_CON1, GENMASK(19, 16) | BIT(27),
			   (10 << 16) | (mono ? BIT(27) : 0));

		/*
		 * ADDA uplink SRC: voice mode 3 = 48 kHz, written to both
		 * field copies as the vendor does -
		 *   Afe_Set_Reg(AFE_ADDA_UL_SRC_CON0,
		 *               (mode << 19) | (mode << 17), 0x001E0000);
		 */
		afe_update(da, AFE_ADDA_UL_SRC_CON0, 0x001e0000,
			   (3 << 19) | (3 << 17));
		writel(0x03f87201, da->afe_base + AFE_ADDA_NEWIF_CFG0);
		afe_update(da, AFE_ADDA_NEWIF_CFG1, GENMASK(11, 10), 3 << 10);

		/* VUL fetch format = 16 bit (AFE_MEMIF_PBUF_SIZE[23:22]) */
		afe_update(da, AFE_MEMIF_PBUF_SIZE, GENMASK(23, 22), 0);
		/* O09/O10 connection format = 16 bit (AFE_CONN_24BIT bit == output) */
		afe_update(da, AFE_CONN_24BIT, BIT(9) | BIT(10), 0);

		/* ADDA UL output -> VUL memif: I03->O09, I04->O10 (AFE_CONN3) */
		afe_update(da, AFE_CONN3, BIT(0) | BIT(3), BIT(0) | BIT(3));

		douglas_set_aud_pins(da, true);
		douglas_pmic_adc_enable(da, true);
		da->cap_last_period = 0;
		mutex_unlock(&da->lock);
		return 0;
	}

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

	afe_update(da, AFE_CONN1, BIT(21), BIT(21));
	afe_update(da, AFE_CONN2, BIT(6), BIT(6));
	writel(0xf0000000, da->afe_base + AFE_SGEN_CON0);

	douglas_set_aud_pins(da, true);
	douglas_pmic_dac_enable(da, true);
	da->last_period = 0;
	mutex_unlock(&da->lock);
	return 0;
}

static int douglas_pcm_trigger(struct snd_soc_component *comp,
			       struct snd_pcm_substream *substream, int cmd)
{
	struct douglas_alsa *da = g_da;

	if (!da)
		return -ENODEV;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		switch (cmd) {
		case SNDRV_PCM_TRIGGER_START:
		case SNDRV_PCM_TRIGGER_RESUME:
			afe_update(da, AFE_ADDA_UL_DL_CON0, BIT(0), BIT(0));
			afe_update(da, AFE_ADDA_UL_SRC_CON0, BIT(0), BIT(0));
			/*
			 * bit 0 = AFE_ON (global, the vendor's EnableAfe()),
			 * bit 3 = VUL_ON (the capture memif). Both are needed.
			 */
			afe_update(da, AFE_DAC_CON0, BIT(3) | BIT(0),
				   BIT(3) | BIT(0));
			da->cap_last_period = 0;
			WRITE_ONCE(da->cap_running, true);
			hrtimer_start(&da->timer, ms_to_ktime(5), HRTIMER_MODE_REL);
			return 0;
		case SNDRV_PCM_TRIGGER_STOP:
		case SNDRV_PCM_TRIGGER_SUSPEND:
			WRITE_ONCE(da->cap_running, false);
			if (!READ_ONCE(da->running))
				hrtimer_try_to_cancel(&da->timer);
			afe_update(da, AFE_DAC_CON0,
				   READ_ONCE(da->running) ? BIT(3) : (BIT(3) | BIT(0)),
				   0);
			afe_update(da, AFE_ADDA_UL_SRC_CON0, BIT(0), 0);
			return 0;
		default:
			return -EINVAL;
		}
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		afe_update(da, AFE_ADDA_DL_SRC2_CON0, BIT(0), BIT(0));
		afe_update(da, AFE_I2S_CON1, BIT(0), BIT(0));
		afe_update(da, AFE_ADDA_UL_DL_CON0, BIT(0), BIT(0));
		afe_update(da, FPGA_CFG1, BIT(4), 0);
		afe_update(da, AFE_DAC_CON0, BIT(1) | BIT(0), BIT(1) | BIT(0));
		douglas_update_amp_route(da, true);
		da->last_period = 0;
		WRITE_ONCE(da->running, true);
		hrtimer_start(&da->timer, ms_to_ktime(5), HRTIMER_MODE_REL);
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		WRITE_ONCE(da->running, false);
		if (!READ_ONCE(da->cap_running))
			hrtimer_try_to_cancel(&da->timer);
		douglas_update_amp_route(da, false);
		afe_update(da, AFE_DAC_CON0, BIT(1) | BIT(0), 0);
		afe_update(da, AFE_ADDA_DL_SRC2_CON0, BIT(0), 0);
		afe_update(da, AFE_I2S_CON1, BIT(0), 0);
		if (!READ_ONCE(da->cap_running))
			afe_update(da, AFE_ADDA_UL_DL_CON0, BIT(0), 0);
		return 0;
	default:
		return -EINVAL;
	}
}

static snd_pcm_uframes_t douglas_pcm_pointer(struct snd_soc_component *comp,
					     struct snd_pcm_substream *substream)
{
	struct douglas_alsa *da = g_da;
	u32 cur, off;

	if (!da)
		return 0;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		cur = readl(da->afe_base + AFE_VUL_CUR);
		if (cur < AFE_UL_SRAM_PHYS ||
		    cur >= AFE_UL_SRAM_PHYS + substream->runtime->dma_bytes)
			off = 0;
		else
			off = cur - AFE_UL_SRAM_PHYS;
		return bytes_to_frames(substream->runtime, off);
	}

	cur = readl(da->afe_base + AFE_DL1_CUR);
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
	struct douglas_alsa *da = g_da;
	ucontrol->value.integer.value[0] = da ? da->volume : 5;
	return 0;
}

static int douglas_vol_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct douglas_alsa *da = g_da;
	unsigned int val = min_t(unsigned int, ucontrol->value.integer.value[0], 7);

	if (!da)
		return 0;
	mutex_lock(&da->lock);
	da->volume = val;
	if (da->dac_enabled)
		douglas_apply_volume(da);
	mutex_unlock(&da->lock);
	return 1;
}

struct douglas_mic_ctl {
	unsigned int max;
	size_t offset;
};

static const struct douglas_mic_ctl douglas_mic_ctls[] = {
	{ 1,  offsetof(struct douglas_alsa, mic_mode) },	/* 0=ACC 1=DMIC */
	{ 15, offsetof(struct douglas_alsa, mic_mux) },	/* AUDTOP_CON0[3:0] */
	{ 5,  offsetof(struct douglas_alsa, mic_gain) },	/* 0..5 */
};

static unsigned int *douglas_mic_field(struct douglas_alsa *da,
				       struct snd_kcontrol *kcontrol)
{
	unsigned long idx = kcontrol->private_value;

	if (idx >= ARRAY_SIZE(douglas_mic_ctls))
		return NULL;
	return (unsigned int *)((char *)da + douglas_mic_ctls[idx].offset);
}

static int douglas_mic_info(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	unsigned long idx = kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = douglas_mic_ctls[idx].max;
	return 0;
}

static int douglas_mic_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct douglas_alsa *da = g_da;
	unsigned int *field = da ? douglas_mic_field(da, kcontrol) : NULL;

	ucontrol->value.integer.value[0] = field ? *field : 0;
	return 0;
}

static int douglas_mic_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct douglas_alsa *da = g_da;
	unsigned long idx = kcontrol->private_value;
	unsigned int *field = da ? douglas_mic_field(da, kcontrol) : NULL;
	unsigned int val;

	if (!field)
		return 0;
	val = min_t(unsigned int, ucontrol->value.integer.value[0],
		    douglas_mic_ctls[idx].max);
	if (*field == val)
		return 0;

	mutex_lock(&da->lock);
	*field = val;
	/*
	 * Re-apply immediately if capture is already up, so a sweep does not
	 * have to stop and restart arecord for every value.
	 */
	if (da->adc_enabled) {
		douglas_pmic_adc_enable(da, false);
		douglas_pmic_adc_enable(da, true);
	}
	mutex_unlock(&da->lock);
	return 1;
}

#define DOUGLAS_MIC_CTL(cname, cidx) {					\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,				\
	.name = cname,							\
	.info = douglas_mic_info,					\
	.get = douglas_mic_get,						\
	.put = douglas_mic_put,						\
	.private_value = (cidx),					\
}

static const struct snd_kcontrol_new douglas_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Volume",
		.info = douglas_vol_info,
		.get = douglas_vol_get,
		.put = douglas_vol_put,
	},
	DOUGLAS_MIC_CTL("Mic Mode", 0),
	DOUGLAS_MIC_CTL("Mic Mux", 1),
	DOUGLAS_MIC_CTL("Mic Gain", 2),
};

static const struct snd_soc_component_driver douglas_afe_component = {
	.name = "douglas-alsa",
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
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
};

SND_SOC_DAILINK_DEFS(douglas_playback,
	DAILINK_COMP_ARRAY(COMP_CPU("douglas-dl1-dai")),
	DAILINK_COMP_ARRAY(COMP_DUMMY()),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("douglas-alsa")));

static struct snd_soc_dai_link douglas_dai_links[] = {
	{
		.name = "Douglas Playback",
		.stream_name = "Douglas Playback",
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
	g_da = da;
	mutex_init(&da->lock);
	hrtimer_init(&da->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	da->timer.function = douglas_timer_cb;
	INIT_WORK(&da->period_work, douglas_period_work);
	INIT_WORK(&da->cap_period_work, douglas_cap_period_work);
	da->volume = 5;
	da->mic_mode = DOUGLAS_MIC_MODE_ACC;
	da->mic_mux = 0;
	da->mic_gain = 3;	/* 12 dB */

	da->scpsys = syscon_regmap_lookup_by_compatible("mediatek,mt8163-scpsys");
	if (IS_ERR(da->scpsys))
		return -ENODEV;

	pmic_dev = bus_find_device_by_name(&platform_bus_type, NULL, "1000d000.pwrap:mt6323");
	if (!pmic_dev)
		return -ENODEV;
	chip = dev_get_drvdata(pmic_dev);
	da->pmic_regmap = chip ? chip->regmap : dev_get_regmap(pmic_dev, NULL);
	put_device(pmic_dev);
	if (!da->pmic_regmap)
		return -ENODEV;

	da->pctl_base = devm_ioremap(dev, PCTL_BASE_PHYS, 0x1000);
	da->afe_base = devm_ioremap(dev, AFE_BASE_PHYS, 0x1000);
	da->sram = devm_ioremap_wc(dev, AFE_SRAM_PHYS, AFE_SRAM_MAP_SIZE);
	if (!da->pctl_base || !da->afe_base || !da->sram)
		return -ENOMEM;

	douglas_init_hp_detect_gpio(da);

	if (dev->of_node) {
		da->clk_infra = devm_clk_get(dev, "aud_infra_clk");
		da->clk_aud_sel = devm_clk_get(dev, "top_mux_audio");
		da->clk_intbus = devm_clk_get(dev, "top_mux_audio_intbus");
		da->clk_mux2 = devm_clk_get(dev, "aud_mux2_clk");
		da->clk_apll2 = devm_clk_get(dev, "apmixed_apll2_clk");
		da->pctl = devm_pinctrl_get(dev);
		if (!IS_ERR(da->pctl)) {
			da->pins_aud = pinctrl_lookup_state(da->pctl, "audpmicclk-speaker-mode1");
			da->pins_gpio = pinctrl_lookup_state(da->pctl, "audpmicclk-speaker-mode0");
			if (IS_ERR(da->pins_aud) || IS_ERR(da->pins_gpio))
				da->pins_aud = da->pins_gpio = NULL;
		} else {
			da->pctl = NULL;
		}
		dev_info(dev, "DT-bound: pm_domain=%s pinctrl=%s\n",
			 dev->pm_domain ? "yes" : "no", da->pins_aud ? "yes" : "raw");
	} else {
		da->clk_infra = get_dt_clk("mediatek,mt8163-infracfg", 48);
		da->clk_aud_sel = get_dt_clk("mediatek,mt8163-topckgen", 79);
		da->clk_intbus = get_dt_clk("mediatek,mt8163-topckgen", 80);
		da->clk_mux2 = get_dt_clk("mediatek,mt8163-topckgen", 88);
		da->clk_apll2 = get_dt_clk("mediatek,mt8163-topckgen", 11);
	}

	/* The DAI link finds the platform component by device name. */
	douglas_playback_platforms[0].name = dev_name(dev);
	if (da->pins_gpio)
		pinctrl_select_state(da->pctl, da->pins_gpio);

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

	dev_info(dev, "Douglas ALSA sound card registered (g_da=%p)\n", g_da);
	return 0;
}

static const struct of_device_id douglas_alsa_of_match[] = {
	{ .compatible = "mediatek,mt8163-soc-pcm-dl1" },
	{ }
};
MODULE_DEVICE_TABLE(of, douglas_alsa_of_match);

static struct platform_driver douglas_alsa_driver = {
	.probe = douglas_alsa_probe,
	.driver = {
		.name = "douglas-alsa",
		.of_match_table = douglas_alsa_of_match,
	},
};

static int __init douglas_alsa_init(void)
{
	int ret = platform_driver_register(&douglas_alsa_driver);
	if (ret)
		return ret;
	/* Fall back to a bare device only if the DT node did not bind us. */
	if (!g_da) {
		g_pdev = platform_device_register_simple("douglas-alsa", -1, NULL, 0);
		if (IS_ERR(g_pdev)) {
			platform_driver_unregister(&douglas_alsa_driver);
			return PTR_ERR(g_pdev);
		}
	}
	return 0;
}

static void __exit douglas_alsa_exit(void)
{
	if (g_pdev)
		platform_device_unregister(g_pdev);
	platform_driver_unregister(&douglas_alsa_driver);
}

module_init(douglas_alsa_init);
module_exit(douglas_alsa_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Amazon Fire HD 8 (douglas) loadable ALSA sound card");
