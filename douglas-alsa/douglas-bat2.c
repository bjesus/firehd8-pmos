// SPDX-License-Identifier: GPL-2.0-only
/*
 * Amazon Fire HD 8 (2017, douglas) MT6323 PMIC AUXADC Battery Gauge driver.
 * Reads real battery voltage (BATSNS channel 7 -> AUXADC_ADC0) and queries
 * bq24190-charger for charging state, reporting 0-100% capacity to upowerd.
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
};

static struct douglas_bat *g_bat;

static int douglas_read_vbat_mv(struct douglas_bat *bat)
{
	u32 val = 0;
	int i, s, sum = 0;

	mutex_lock(&bat->lock);
	regmap_update_bits(bat->pmic, MT6323_AUXADC_CON11, BIT(4), BIT(4));
	for (s = 0; s < 4; s++) {
		regmap_update_bits(bat->pmic, MT6323_AUXADC_CON22, BIT(7), 0);
		regmap_update_bits(bat->pmic, MT6323_AUXADC_CON22, BIT(7), BIT(7));
		udelay(300);

		for (i = 0; i < 50; i++) {
			regmap_read(bat->pmic, MT6323_AUXADC_ADC0, &val);
			if (val & BIT(15))
				break;
			usleep_range(500, 1000);
		}
		sum += (val & 0x7fff);
	}
	mutex_unlock(&bat->lock);

	/* Vbat (mV) = (sum / 4) * 4 * 1800 / 32768 = sum * 1800 / 32768 */
	return (sum * 1800) / 32768;
}

static int douglas_vbat_to_soc(int mv, bool charging)
{
	static const struct { int mv; int soc; } tbl[] = {
		{ 4180, 100 },
		{ 4100,  90 },
		{ 4020,  80 },
		{ 3950,  70 },
		{ 3880,  60 },
		{ 3820,  50 },
		{ 3780,  40 },
		{ 3740,  30 },
		{ 3700,  20 },
		{ 3650,  10 },
		{ 3550,   5 },
		{ 3400,   0 },
	};
	int i;

	if (charging && mv < 4190)
		mv -= 40;

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

static void remove_existing_psy(const char *name)
{
	struct device *dev = class_find_device_by_name(power_supply_class, name);
	if (dev) {
		struct power_supply *psy = dev_get_drvdata(dev);
		put_device(dev);
		if (psy)
			power_supply_unregister(psy);
	}
}

static int __init douglas_bat_init(void)
{
	struct device *pmic_dev;
	struct mt6397_chip *chip;
	struct power_supply_config cfg = {};
	struct douglas_bat *bat;

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

	remove_existing_psy("bq24190-battery");
	remove_existing_psy("battery");

	cfg.drv_data = bat;
	bat->psy = power_supply_register(pmic_dev, &douglas_bat_desc, &cfg);
	if (IS_ERR(bat->psy)) {
		int err = PTR_ERR(bat->psy);
		kfree(bat);
		return err;
	}

	g_bat = bat;
	pr_info("douglas-battery: registered MT6323 AUXADC battery (vbat=%d mV)\n",
		douglas_read_vbat_mv(bat));
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
