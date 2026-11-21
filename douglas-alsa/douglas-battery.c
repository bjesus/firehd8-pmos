// SPDX-License-Identifier: GPL-2.0-only
/*
 * Amazon Fire HD 8 (2017, douglas) MT6323 PMIC AUXADC Battery Gauge driver.
 * Uses vendor DTB vbat_channel_number = 6 (AUXADC_ADC1) + EFUSE chip trim
 * and exponential moving average filter so battery % is accurate and steady.
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/mfd/mt6323/registers.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>

struct douglas_bat {
	struct regmap *pmic;
	struct power_supply *psy;
	struct mutex lock;
	int trim_permille;
	int avg_mv;
};

static struct douglas_bat *g_bat;

static int douglas_read_trim(struct regmap *pmic)
{
	u32 r1 = 0, r2 = 0, code;

	regmap_read(pmic, 0x01c4, &r1);
	regmap_read(pmic, 0x01c6, &r2);
	code = ((r1 & 0xe000) >> 13) | ((r2 & 0x0003) << 3);

	if (code >= 1 && code <= 15)
		return 1000 + code * 5;
	if (code >= 17 && code <= 31)
		return 1000 - (32 - code) * 5;
	return 1000;
}

static int douglas_read_ch_mv(struct douglas_bat *bat, int ch, u32 adc_reg)
{
	u32 val = 0;
	int i, s, sum = 0;

	mutex_lock(&bat->lock);
	regmap_update_bits(bat->pmic, MT6323_AUXADC_CON11, BIT(4), BIT(4));
	for (s = 0; s < 8; s++) {
		regmap_update_bits(bat->pmic, MT6323_AUXADC_CON22, BIT(ch), 0);
		regmap_update_bits(bat->pmic, MT6323_AUXADC_CON22, BIT(ch), BIT(ch));
		udelay(300);

		for (i = 0; i < 50; i++) {
			regmap_read(bat->pmic, adc_reg, &val);
			if (val & BIT(15))
				break;
			usleep_range(500, 1000);
		}
		sum += (val & 0x7fff);
	}
	mutex_unlock(&bat->lock);

	/* V (mV) = (sum / 8) * 4 * 1800 / 32768 = sum * 900 / 32768 */
	val = (sum * 900) / 32768;
	return (val * bat->trim_permille) / 1000;
}

static int douglas_read_vbat_mv(struct douglas_bat *bat)
{
	/* Vendor DTB: vbat_channel_number = 6 (AUXADC_ADC1), isense = 7 (AUXADC_ADC0) */
	int ch6 = douglas_read_ch_mv(bat, 6, MT6323_AUXADC_ADC1);
	int ch7 = douglas_read_ch_mv(bat, 7, MT6323_AUXADC_ADC0);
	int mv = (ch6 > 3000) ? ch6 : ch7;

	if (bat->avg_mv == 0)
		bat->avg_mv = mv;
	else
		bat->avg_mv = (bat->avg_mv * 3 + mv) / 4;
	return bat->avg_mv;
}

/* Official Douglas 25C battery_profile_t2 curve from vendor DTB */
static int douglas_vbat_to_soc(int mv, bool charging)
{
	static const struct { int mv; int soc; } tbl[] = {
		{ 4167, 100 },
		{ 4061,  90 },
		{ 3972,  80 },
		{ 3904,  70 },
		{ 3839,  60 },
		{ 3798,  50 },
		{ 3779,  40 },
		{ 3761,  30 },
		{ 3726,  20 },
		{ 3674,  10 },
		{ 3585,   5 },
		{ 3439,   0 },
	};
	int i;

	if (charging && mv < 4160)
		mv -= 25;

	if (mv >= tbl[0].mv)
		return 100;
	if (mv <= tbl[ARRAY_SIZE(tbl) - 1].mv)
		return 0;

	for (i = 0; i < ARRAY_SIZE(tbl) - 1; i++) {
		if (mv >= tbl[i + 1].mv) {
			int dv = tbl[i].mv - tbl[i + 1].mv;
			int ds = tbl[i].soc - tbl[i + 1].soc;
			return tbl[i + 1].soc + ((mv - tbl[i + 1].mv) * ds) / dv;
		}
	}
	return 50;
}

static int douglas_get_charger_status(bool *charging)
{
	struct power_supply *chg;
	union power_supply_propval val = {};
	int status = POWER_SUPPLY_STATUS_DISCHARGING;

	*charging = false;
	chg = power_supply_get_by_name("bq24190-charger");
	if (!chg)
		return status;

	if (!power_supply_get_property(chg, POWER_SUPPLY_PROP_STATUS, &val))
		status = val.intval;
	if (!power_supply_get_property(chg, POWER_SUPPLY_PROP_ONLINE, &val) && val.intval) {
		*charging = (status == POWER_SUPPLY_STATUS_CHARGING);
		if (status == POWER_SUPPLY_STATUS_UNKNOWN ||
		    status == POWER_SUPPLY_STATUS_NOT_CHARGING) {
			status = POWER_SUPPLY_STATUS_CHARGING;
			*charging = true;
		}
	}
	power_supply_put(chg);
	return status;
}

static enum power_supply_property douglas_bat_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_CAPACITY,
};

static int douglas_bat_get_prop(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct douglas_bat *bat = power_supply_get_drvdata(psy);
	bool charging = false;
	int status, mv, soc;

	status = douglas_get_charger_status(&charging);
	mv = douglas_read_vbat_mv(bat);
	soc = douglas_vbat_to_soc(mv, charging);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = (charging && soc >= 100) ? POWER_SUPPLY_STATUS_FULL : status;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = mv * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = 4200000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = 3400000;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = soc;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct power_supply_desc douglas_bat_desc = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = douglas_bat_props,
	.num_properties = ARRAY_SIZE(douglas_bat_props),
	.get_property = douglas_bat_get_prop,
};

static int __init douglas_bat_init(void)
{
	struct device *pmic_dev;
	struct mt6397_chip *chip;
	struct power_supply_config cfg = {};
	struct douglas_bat *bat;
	int ch6, ch7;

	pmic_dev = bus_find_device_by_name(&platform_bus_type, NULL, "1000d000.pwrap:mt6323");
	if (!pmic_dev)
		return -ENODEV;
	chip = dev_get_drvdata(pmic_dev);

	bat = kzalloc(sizeof(*bat), GFP_KERNEL);
	if (!bat) {
		put_device(pmic_dev);
		return -ENOMEM;
	}
	mutex_init(&bat->lock);
	bat->pmic = chip ? chip->regmap : dev_get_regmap(pmic_dev, NULL);
	put_device(pmic_dev);
	if (!bat->pmic) {
		kfree(bat);
		return -ENODEV;
	}

	bat->trim_permille = douglas_read_trim(bat->pmic);
	ch6 = douglas_read_ch_mv(bat, 6, MT6323_AUXADC_ADC1);
	ch7 = douglas_read_ch_mv(bat, 7, MT6323_AUXADC_ADC0);

	/*
	 * No need to displace anyone: the charger's DT node sets
	 * "omit-battery-class", so bq24190 does not register a battery psy.
	 * Never power_supply_unregister() another driver's supply - bq24190
	 * keeps the pointer in bdi->battery and dereferences it on resume.
	 */
	cfg.drv_data = bat;
	bat->psy = power_supply_register(pmic_dev, &douglas_bat_desc, &cfg);
	if (IS_ERR(bat->psy)) {
		int err = PTR_ERR(bat->psy);
		kfree(bat);
		return err;
	}

	g_bat = bat;
	pr_info("douglas-battery: trim=%d ch6(vbat)=%d mV ch7(isense)=%d mV\n",
		bat->trim_permille, ch6, ch7);
	return 0;
}

static void __exit douglas_bat_exit(void)
{
	if (!g_bat)
		return;
	power_supply_unregister(g_bat->psy);
	kfree(g_bat);
	g_bat = NULL;
}

module_init(douglas_bat_init);
module_exit(douglas_bat_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Amazon Fire HD 8 (douglas) MT6323 AUXADC Battery driver");
