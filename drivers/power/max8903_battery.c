/*
 * linux/drivers/power/max8903_battery.c
 *
 * Battery management driver for MAX8903 charger chip.
 *
 * based on palmtx_battery.c
 *
 * Copyright (C) 2010 Samsung Electronics.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/irq.h>
#include <linux/wakelock.h>
#include <linux/slab.h>
#include <linux/ktime.h>
#include <linux/max8903.h>
#include <linux/android_alarm.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <linux/io.h>
#include <mach/gpio.h>
#include <mach/gpio-sec.h>

#define FAST_POLL			(1 * 60)
#define SLOW_POLL			(10 * 60)

#define BATTERY_UNKNOWN		0
#define BATTERY_CHARGING	1
#define BATTERY_DISCHARGING	2
#define BATTERY_RECHARGE	3
#define BATTERY_OVERHEATED	4

static char *supply_list[] = {
	"battery",
};
static enum power_supply_property p3_battery_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
};

static enum power_supply_property p3_power_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

typedef enum {
	CHARGER_BATTERY = 0,
	CHARGER_USB,
	CHARGER_AC,
	CHARGER_DISCHARGE
} charger_type_t;

struct battery_info {
	u32 batt_id;		/* Battery ID from ADC */
	s32 batt_vol;		/* Battery voltage from ADC */
	s32 batt_vol_adc;	/* Battery ADC value */
	s32 batt_vol_adc_cal;	/* Battery ADC value (calibrated)*/
	s32 temperature;
	s32 batt_current;	/* Battery current from ADC */
	u32 level;		/* formula */
	u32 charging_source;	/* 0: no cable, 1:usb, 2:AC */
	u32 batt_health;	/* Battery Health (Authority) */
	u32 batt_is_full;	/* 0 : Not full 1: Full */
	u32 charger_state;
};

struct battery_data {
	struct device		*dev;
	struct max8903_battery_platform_data *pdata;
	struct battery_info	info;
	struct power_supply	psy_battery;
	struct power_supply	psy_usb;
	struct power_supply	psy_ac;
	struct delayed_work	battery_work;
	struct workqueue_struct *monitor_wqueue;
	struct alarm		alarm;
	struct mutex		work_lock;
	struct wake_lock	work_wake_lock;
	struct max8903_charger_callbacks cable_callbacks;
	enum cable_type_t	cable_status;
	u32			slow_poll : 1;
	u32			charging : 1;
	u32			reserved : 30;
	int			timestamp;
	ktime_t			last_poll;
	int			charge_start_time;
	int			recharge_start_time;
	int			discharge_start_time;
	int			present;
	int			charger_state;
	int			chg_irq;
};

static int p3_enable_charger(struct battery_data *battery);
static void p3_disable_charger(struct battery_data *battery);

static int p3_battery_get_property(struct power_supply *bat_ps,
		enum power_supply_property psp,
		union power_supply_propval *val);

static int p3_usb_get_property(struct power_supply *usb_ps,
		enum power_supply_property psp,
		union power_supply_propval *val);

static int p3_ac_get_property(struct power_supply *ac_ps,
		enum power_supply_property psp,
		union power_supply_propval *val);

static int max8903_is_charging(struct battery_data *battery);
static int max8903_is_voltage_ok(struct battery_data *battery);
static void max8903_enable_charger(struct battery_data *battery,
		int fast_charging_enable);
static void max8903_disable_charger(struct battery_data *battery);

static void max8903_dump_state(struct battery_data *battery)
{
	pr_info("MAX8903 Charger State\n");
	pr_info("\tCharger Enabled %u CEN  %d\n",
		!gpio_get_value(battery->pdata->charger.enable_line),
		gpio_get_value(battery->pdata->charger.enable_line));
	pr_info("\tVoltage OK      %u DOK  %d\n",
		max8903_is_voltage_ok(battery),
		gpio_get_value(battery->pdata->charger.dok_line));
	pr_info("\tCharging        %u CHG  %d\n",
		max8903_is_charging(battery),
		gpio_get_value(battery->pdata->charger.chg_line));
	pr_info("\tFast Charging   %u ISET %d\n",
		gpio_get_value(battery->pdata->charger.iset_line),
		gpio_get_value(battery->pdata->charger.iset_line));
}

#ifdef DUMP_FUEL_GAUGE
static void fuel_gauge_dump_state(struct battery_data *battery)
{
	int ret;
	union power_supply_propval value;

	value.intval = 0;

	pr_info("Fuel Gauge State\n");

	if (!battery->pdata->psy_fuelgauge)
		pr_info("\tNo Fuel Gauge Present\n");

	if (!battery->pdata->psy_fuelgauge->get_property)
		pr_info("\tNo get_property API\n");

	ret = battery->pdata->psy_fuelgauge->get_property(
			battery->pdata->psy_fuelgauge,
			POWER_SUPPLY_PROP_CURRENT_NOW, &value);

	if (ret)
		pr_info("\tcurrent : error = %d\n", ret);
	else
		pr_info("\tcurrent : %d mA\n", value.intval);

	ret = battery->pdata->psy_fuelgauge->get_property(
			battery->pdata->psy_fuelgauge,
			POWER_SUPPLY_PROP_VOLTAGE_NOW, &value);

	if (ret)
		pr_info("\tvoltage : error = %d\n", ret);
	else
		pr_info("\tvoltage : %d mV\n", value.intval);

	ret = battery->pdata->psy_fuelgauge->get_property(
			battery->pdata->psy_fuelgauge,
			POWER_SUPPLY_PROP_CAPACITY, &value);

	if (ret)
		pr_info("\tSOC : error = %d\n", ret);
	else
		pr_info("\tSOC : %d%%\n", value.intval);

	ret = battery->pdata->psy_fuelgauge->get_property(
			battery->pdata->psy_fuelgauge,
			POWER_SUPPLY_PROP_CURRENT_AVG, &value);

	if (ret)
		pr_info("\tAvg Current : error = %d\n", ret);
	else
		pr_info("\tAvg Current : %d mA\n", value.intval);

	ret = battery->pdata->psy_fuelgauge->get_property(
			battery->pdata->psy_fuelgauge,
			POWER_SUPPLY_PROP_TEMP, &value);

	if (ret)
		pr_info("\tTemperature : error = %d\n", ret);
	else
		pr_info("\tTemperature : %d C\n", value.intval);
}
#endif

static int p3_enable_charger(struct battery_data *battery)
{
	switch (battery->cable_status) {
	case CABLE_TYPE_AC_FAST:
		max8903_enable_charger(battery, 1);
		break;
	case CABLE_TYPE_AC:
	case CABLE_TYPE_USB:
		max8903_enable_charger(battery, 0);
		break;
	default:
		max8903_disable_charger(battery);
		return -EINVAL;
	}

	return 0;
}

static void p3_disable_charger(struct battery_data *battery)
{

	max8903_disable_charger(battery);

	udelay(10); // TODO, is this necessary?
}

static int p3_bat_get_charging_status(struct battery_data *battery)
{
	charger_type_t charger = CHARGER_BATTERY;
	int ret = 0;

	charger = battery->info.charging_source;

	switch (charger) {
	case CHARGER_BATTERY:
	case CHARGER_USB:
		ret = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case CHARGER_AC:
		if (battery->info.batt_is_full)
			ret = POWER_SUPPLY_STATUS_FULL;
		else
			ret = POWER_SUPPLY_STATUS_CHARGING;
		break;
	case CHARGER_DISCHARGE:
		ret = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	default:
		ret = POWER_SUPPLY_STATUS_UNKNOWN;
	}

	return ret;
}

/* TODO POWER SUPPLY
 * We need to call through to the fuel gauge driver through an interface that
 * we get through the platform data.
 */
static int p3_battery_get_property(struct power_supply *bat_ps,
		enum power_supply_property psp,
		union power_supply_propval *value)
{
	struct battery_data *battery = container_of(bat_ps,
				struct battery_data, psy_battery);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		value->intval = p3_bat_get_charging_status(battery);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		value->intval = battery->info.batt_health;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		value->intval = battery->present;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		value->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		value->intval = battery->info.level;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		value->intval = battery->info.temperature;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int p3_usb_get_property(struct power_supply *usb_ps,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct battery_data *battery = container_of(usb_ps,
				struct battery_data, psy_usb);

	charger_type_t charger = battery->info.charging_source;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = (charger == CHARGER_USB ? 1 : 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int p3_ac_get_property(struct power_supply *ac_ps,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct battery_data *battery = container_of(ac_ps,
				struct battery_data, psy_ac);

	charger_type_t charger = battery->info.charging_source;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = (charger == CHARGER_AC ? 1 : 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void max8903_program_alarm(struct battery_data *battery, int seconds)
{
	ktime_t low_interval = ktime_set(seconds - 10, 0);
	ktime_t slack = ktime_set(20, 0);
	ktime_t next;
	next = ktime_add(battery->last_poll, low_interval);
	alarm_start_range(&battery->alarm, next, ktime_add(next, slack));
}

static void update_battery_health(struct battery_data *battery, int temp,
		int *health)
{
	if (temp >= battery->pdata->temp_high_threshold) {
		if (*health != POWER_SUPPLY_HEALTH_OVERHEAT &&
				*health !=  POWER_SUPPLY_HEALTH_UNSPEC_FAILURE)
			*health = POWER_SUPPLY_HEALTH_OVERHEAT;
	} else if (temp <= battery->pdata->temp_high_recovery &&
				 temp >= battery->pdata->temp_low_recovery) {
		if (*health == POWER_SUPPLY_HEALTH_OVERHEAT ||
				*health == POWER_SUPPLY_HEALTH_COLD)
			*health = POWER_SUPPLY_HEALTH_GOOD;
	} else if (temp <= battery->pdata->temp_low_threshold) {
		if (*health != POWER_SUPPLY_HEALTH_COLD &&
				*health != POWER_SUPPLY_HEALTH_UNSPEC_FAILURE)
			*health = POWER_SUPPLY_HEALTH_COLD;
	}
}

static void p3_battery_work(struct work_struct *work)
{
	struct battery_data *battery =
		container_of(work, struct battery_data, battery_work.work);
	struct timespec cur_time;
	int ret;
	struct timespec ts;
	unsigned long flags;
	union power_supply_propval value;
	ktime_t current_ktime;

	max8903_dump_state(battery);

	mutex_lock(&battery->work_lock);

	/* First we need to gather information. */
	current_ktime = alarm_get_elapsed_realtime();
	cur_time = ktime_to_timespec(current_ktime);

	if (battery->pdata->psy_fuelgauge &&
			battery->pdata->psy_fuelgauge->get_property) {

		/* Update the Battery Temperature */
		ret = battery->pdata->psy_fuelgauge->get_property(
				battery->pdata->psy_fuelgauge,
				POWER_SUPPLY_PROP_TEMP, &value);

		if (!ret) {
			battery->info.temperature = value.intval;
			
			/* Use the battery temperature to determine the health
			 * of the battery.
			 */
			update_battery_health(battery, value.intval,
					&battery->info.batt_health);
		} else
			battery->info.temperature = -1;

		/* Update the Battery Voltage */
		ret = battery->pdata->psy_fuelgauge->get_property(
				battery->pdata->psy_fuelgauge,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, &value);

		if (!ret)
			battery->info.batt_vol = value.intval;
		else
			battery->info.batt_vol = -1;
	}

	switch (battery->charger_state) {
	case BATTERY_UNKNOWN:
		/* First make sure the battery is not overheated. */
		if (battery->info.temperature > battery->pdata->temp_high_threshold) {
			pr_info("BATTERY : UNKNOWN->OVERHEAT on high temp\n");
			p3_disable_charger(battery);
			battery->charger_state = BATTERY_OVERHEATED;
			battery->discharge_start_time = cur_time.tv_sec;
			break;
		}

		if (!max8903_is_voltage_ok(battery)) {
			pr_info("BATTERY : UNKNOWN->DISCHARGING on disconnect\n");
			p3_disable_charger(battery);
			battery->charger_state = BATTERY_DISCHARGING;
			battery->info.charger_state = POWER_SUPPLY_STATUS_DISCHARGING;
			battery->discharge_start_time = cur_time.tv_sec;
			break;
		}

		ret = p3_enable_charger(battery);
		if (!ret) {
			pr_info("BATTERY : UNKNOWN->CHARGING, charge source detected\n");
			battery->charger_state = BATTERY_CHARGING;
		}
		break;

	case BATTERY_CHARGING:
		/* First make sure the battery is not overheated. */
		if (battery->info.temperature > battery->pdata->temp_high_threshold) {
			pr_info("BATTERY : CHARGING->OVERHEAT on high temp\n");
			p3_disable_charger(battery);
			battery->charger_state = BATTERY_OVERHEATED;
			battery->discharge_start_time = cur_time.tv_sec;
			break;
		}

		/* If our charge source has disappeared then we need to
		 * disable the charger.
		 */
		if (!max8903_is_voltage_ok(battery)) {
			pr_info("BATTERY : CHARGING->DISCHARGING, no charge source\n");
			p3_disable_charger(battery);
			battery->charger_state = BATTERY_DISCHARGING;
			battery->info.charger_state = POWER_SUPPLY_STATUS_DISCHARGING;
			battery->discharge_start_time = cur_time.tv_sec;
			break;
		}

		/* If the charger has stopped charging because it has completed
		 * its charge cycle then we can disable the charger.
		 */
		if (!max8903_is_charging(battery)) {
			pr_info("BATTERY : CHARGING->DISCHARGING on completion\n");
			p3_disable_charger(battery);
			battery->charger_state = BATTERY_DISCHARGING;
			battery->info.charger_state = POWER_SUPPLY_STATUS_DISCHARGING;
			battery->discharge_start_time = cur_time.tv_sec;
			break;
		}

		/* If the charger has been working for longer than our maximum duration then
		 * shut it off.
		 */
		if (cur_time.tv_sec - battery->charge_start_time >=
				battery->pdata->charge_duration) {
			pr_info("BATTERY : CHARGING->DISCHARGING on timeout\n");
			p3_disable_charger(battery);
			battery->charger_state = BATTERY_DISCHARGING;
			battery->info.charger_state = POWER_SUPPLY_STATUS_DISCHARGING;
			battery->discharge_start_time = cur_time.tv_sec;
		}
		break;

	case BATTERY_DISCHARGING:
		/* First make sure the battery is not overheated. */
		if (battery->info.temperature > battery->pdata->temp_high_threshold) {
			pr_info("BATTERY : DISCHARGING->OVERHEAT on high temp\n");
			battery->charger_state = BATTERY_OVERHEATED;
			break;
		}

		if (!max8903_is_voltage_ok(battery)) {
			pr_info("BATTERY : DISCHARGING, no charge source\n");
			break;
		}

		/* If the battery is below a certain threshold then we should
		 * start charging.
		 */

		/* If the battery is above a certain threshold then we should
		 * check the amount of time since the last the charge cycle and
		 * start a top off session.
		 */
		if (cur_time.tv_sec - battery->discharge_start_time >=
				battery->pdata->recharge_delay) {
			pr_info("BATTERY : DISCHARGING->RECHARGE, charge source detected\n");
			p3_enable_charger(battery);
			battery->charger_state = BATTERY_RECHARGE;
			battery->recharge_start_time = cur_time.tv_sec;
		}

		/* We need to have some sort of check to see if we need to start
		 * a main charge cycle.
		 */
		ret = p3_enable_charger(battery);
		if (!ret) {
			pr_info("BATTERY : DISCHARGING->CHARGING, charge source detected\n");
			battery->charger_state = BATTERY_CHARGING;
		}
		 break;

	case BATTERY_RECHARGE:
		if (battery->info.temperature > battery->pdata->temp_high_threshold) {
			pr_info("BATTERY : RECHARGING->OVERHEAT on high temp\n");
			battery->charger_state = BATTERY_OVERHEATED;
			break;
		}

		if (!max8903_is_voltage_ok(battery)) {
			pr_info("BATTERY : RECHARGING->DISCHARGING, no charge source\n");
			p3_disable_charger(battery);
			battery->charger_state = BATTERY_DISCHARGING;
			battery->info.charger_state = POWER_SUPPLY_STATUS_DISCHARGING;
			battery->discharge_start_time = cur_time.tv_sec;
		}

		if (!max8903_is_charging(battery)) {
			pr_info("BATTERY : RECHARGING->DISCHARGING on completion\n");
			p3_disable_charger(battery);
			battery->charger_state = BATTERY_DISCHARGING;
			battery->info.charger_state = POWER_SUPPLY_STATUS_DISCHARGING;
			battery->discharge_start_time = cur_time.tv_sec;
			break;
		}

		/* The recharging cycle will be for a specified duration. */
		if (cur_time.tv_sec - battery->recharge_start_time >=
				battery->pdata->recharge_duration) {
			p3_disable_charger(battery);
			battery->charger_state = BATTERY_DISCHARGING;
			battery->info.charger_state = POWER_SUPPLY_STATUS_DISCHARGING;
			battery->discharge_start_time = cur_time.tv_sec;
		}
		break;

	case BATTERY_OVERHEATED:
		pr_info("p3_battery_work : BATTERY_OVERHEAT\n");
		break;
	}

	mutex_unlock(&battery->work_lock);

	power_supply_changed(&battery->psy_battery);

	battery->last_poll = alarm_get_elapsed_realtime();
	ts = ktime_to_timespec(battery->last_poll);
	battery->timestamp = ts.tv_sec;

	/* prevent suspend before starting the alarm */
	local_irq_save(flags);
	wake_unlock(&battery->work_wake_lock);
	max8903_program_alarm(battery, FAST_POLL);
	local_irq_restore(flags);

	max8903_dump_state(battery);
}

static int max8903_battery_suspend(struct device *dev)
{
	struct battery_data *battery = dev_get_drvdata(dev);

	if (!battery->charging) {
		max8903_program_alarm(battery, SLOW_POLL);
		battery->slow_poll = 1;
	}

	return 0;
}

static int max8903_battery_resume(struct device *dev)
{
	struct battery_data *battery = dev_get_drvdata(dev);

	/* We might be on a slow sample cycle.  If we're
	 * resuming we should resample the battery state
	 * if it's been over a minute since we last did
	 * so, and move back to sampling every minute until
	 * we suspend again.
	 */
	if (battery->slow_poll) {
		max8903_program_alarm(battery, FAST_POLL);
		battery->slow_poll = 0;
	}

	return 0;
}

static void max8903_battery_cable_changed(struct max8903_charger_callbacks *arg,
	enum cable_type_t status)
{
	struct battery_data *battery = container_of(arg, struct battery_data,
			cable_callbacks);
	battery->cable_status = status;

	wake_lock(&battery->work_wake_lock);
	/* TODO, figure out what an appropriate delay would be. */
	queue_delayed_work(battery->monitor_wqueue,
		&battery->battery_work, msecs_to_jiffies(200));

}

static irqreturn_t max8903_charge_stop_irq(int irq, void *arg)
{
	struct battery_data *battery = (struct battery_data*)arg;

	/* TODO, do we need to set a flag indicating that charge complete
	 * interrupt has occured?
	 *
	 * We don't know if the charge stopped because the cycle completed
	 * or the cable was disconnected?
	 *
	 * It would be best to handle all this in the battery work queue so it
	 * is centralized.
	 */

	wake_lock(&battery->work_wake_lock);
	/* TODO, figure out what an appropriate delay would be. */
	queue_delayed_work(battery->monitor_wqueue,
		&battery->battery_work, msecs_to_jiffies(200));

	return IRQ_HANDLED;
}

static void p3_battery_alarm(struct alarm *alarm)
{
	struct battery_data *battery =
			container_of(alarm, struct battery_data, alarm);
	wake_lock(&battery->work_wake_lock);
	queue_work(battery->monitor_wqueue, &battery->battery_work.work);
}

static int max8903_is_voltage_ok(struct battery_data *battery)
{
	return !gpio_get_value(battery->pdata->charger.dok_line);
}

static int max8903_is_charging(struct battery_data *battery)
{
	return !gpio_get_value(battery->pdata->charger.chg_line);
}

static void max8903_enable_charger(struct battery_data *battery,
			int fast_charging_enable)
{
	gpio_set_value(battery->pdata->charger.iset_line, fast_charging_enable);
	gpio_set_value(battery->pdata->charger.enable_line, 0);
}

static void max8903_disable_charger(struct battery_data *battery)
{
	gpio_set_value(battery->pdata->charger.iset_line, 0);
	gpio_set_value(battery->pdata->charger.enable_line, 1);
}

static void max8903_init(struct battery_data *battery)
{
	battery->pdata->charger.init();

	gpio_request(battery->pdata->charger.enable_line, "MAX8903 CEN GPIO");
	gpio_direction_output(battery->pdata->charger.enable_line, 0);

	gpio_request(battery->pdata->charger.dok_line, "MAX8903 DOK GPIO");
	gpio_direction_input(battery->pdata->charger.dok_line);

	gpio_request(battery->pdata->charger.chg_line, "MAX8903 CHG GPIO");
	gpio_direction_input(battery->pdata->charger.chg_line);

	gpio_request(battery->pdata->charger.iset_line, "MAX8903 ISET GPIO");
	gpio_direction_output(battery->pdata->charger.iset_line, 0);
}

static int __devinit max8903_battery_probe(struct platform_device *pdev)
{
	struct max8903_battery_platform_data *pdata =
			dev_get_platdata(&pdev->dev);
	struct battery_data *battery;
	int ret = 0;

	pr_info("%s : MAX8903 Charger Driver Loading\n", __func__);

	battery = kzalloc(sizeof(*battery), GFP_KERNEL);
	if (!battery)
		return -ENOMEM;

	battery->pdata = pdata;

	max8903_init(battery);

	platform_set_drvdata(pdev, battery);

	battery->present = 1;
	battery->info.level = 100;
	battery->info.charging_source = CHARGER_BATTERY;
	battery->charger_state = BATTERY_UNKNOWN;
	battery->info.batt_health = POWER_SUPPLY_HEALTH_GOOD;

	battery->psy_battery.name = "battery";
	battery->psy_battery.type = POWER_SUPPLY_TYPE_BATTERY;
	battery->psy_battery.properties = p3_battery_properties;
	battery->psy_battery.num_properties = ARRAY_SIZE(p3_battery_properties);
	battery->psy_battery.get_property = p3_battery_get_property;

	battery->psy_usb.name = "usb";
	battery->psy_usb.type = POWER_SUPPLY_TYPE_USB;
	battery->psy_usb.supplied_to = supply_list;
	battery->psy_usb.num_supplicants = ARRAY_SIZE(supply_list);
	battery->psy_usb.properties = p3_power_properties;
	battery->psy_usb.num_properties = ARRAY_SIZE(p3_power_properties);
	battery->psy_usb.get_property = p3_usb_get_property;

	battery->psy_ac.name = "ac";
	battery->psy_ac.type = POWER_SUPPLY_TYPE_MAINS;
	battery->psy_ac.supplied_to = supply_list;
	battery->psy_ac.num_supplicants = ARRAY_SIZE(supply_list);
	battery->psy_ac.properties = p3_power_properties;
	battery->psy_ac.num_properties = ARRAY_SIZE(p3_power_properties);
	battery->psy_ac.get_property = p3_ac_get_property;

	mutex_init(&battery->work_lock);

	wake_lock_init(&battery->work_wake_lock, WAKE_LOCK_SUSPEND,
			"battery wake lock");

	INIT_DELAYED_WORK(&battery->battery_work, p3_battery_work);

	battery->monitor_wqueue =
		create_singlethread_workqueue(dev_name(&pdev->dev));
	if (!battery->monitor_wqueue) {
		pr_err("Failed to create single workqueue\n");
		ret = -ENOMEM;
		goto err_workqueue_init;
	}

	battery->last_poll = alarm_get_elapsed_realtime();

	alarm_init(&battery->alarm, ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP,
		p3_battery_alarm);

	ret = power_supply_register(&pdev->dev, &battery->psy_battery);
	if (ret) {
		pr_err("Failed to register battery power supply.\n");
		goto err_battery_psy_register;
	}

	ret = power_supply_register(&pdev->dev, &battery->psy_usb);
	if (ret) {
		pr_err("Failed to register USB power supply.\n");
		goto err_usb_psy_register;
	}

	ret = power_supply_register(&pdev->dev, &battery->psy_ac);
	if (ret) {
		pr_err("Failed to register AC power supply.\n");
		goto err_ac_psy_register;
	}

	battery->chg_irq = gpio_to_irq(battery->pdata->charger.chg_line);

	ret = set_irq_type(battery->chg_irq, IRQ_TYPE_EDGE_RISING);
	if (ret) {
		pr_err("Unable to configure MAX8903 Charger IRQ.\n");
		goto err_charger_irq_configure;
	}

	ret = request_irq(battery->chg_irq, max8903_charge_stop_irq, 0,
			"MAX8903 CHG IRQ", battery);
	if (ret) {
		pr_err("Unable to register MAX8903 Charger IRQ.\n");
		goto err_charger_irq;
	}

	battery->cable_callbacks.cable_changed = max8903_battery_cable_changed;
	if (battery->pdata->register_cable_callbacks)
		battery->pdata->register_cable_callbacks(&battery->cable_callbacks);

	queue_work(battery->monitor_wqueue, &battery->battery_work.work);

	return 0;

err_charger_irq:
err_charger_irq_configure:
	power_supply_unregister(&battery->psy_ac);
err_ac_psy_register:
	power_supply_unregister(&battery->psy_usb);
err_usb_psy_register:
	power_supply_unregister(&battery->psy_battery);
err_battery_psy_register:
	destroy_workqueue(battery->monitor_wqueue);
	cancel_delayed_work(&battery->battery_work);
	alarm_cancel(&battery->alarm);
err_workqueue_init:
	wake_lock_destroy(&battery->work_wake_lock);
	mutex_destroy(&battery->work_lock);
	kfree(battery);
	return ret;
}

static int __devexit max8903_battery_remove(struct platform_device *pdev)
{
	struct battery_data *battery = platform_get_drvdata(pdev);

	free_irq(gpio_to_irq(battery->pdata->charger.chg_line), NULL);

	power_supply_unregister(&battery->psy_ac);
	power_supply_unregister(&battery->psy_usb);
	power_supply_unregister(&battery->psy_battery);

	destroy_workqueue(battery->monitor_wqueue);
	cancel_delayed_work(&battery->battery_work);
	alarm_cancel(&battery->alarm);
	wake_lock_destroy(&battery->work_wake_lock);
	mutex_destroy(&battery->work_lock);
	kfree(battery);

	return 0;
}

static const struct dev_pm_ops max8903_battery_pm_ops = {
	.suspend	= max8903_battery_suspend,
	.resume		= max8903_battery_resume,
};

static struct platform_driver max8903_battery_driver = {
	.driver = {
		.name = "max8903-battery",
		.owner = THIS_MODULE,
		.pm = &max8903_battery_pm_ops,
	},
	.probe		= max8903_battery_probe,
	.remove		= __devexit_p(max8903_battery_remove),
};

static int __init max8903_battery_init(void)
{
	return platform_driver_register(&max8903_battery_driver);
}

static void __exit max8903_battery_exit(void)
{
	platform_driver_unregister(&max8903_battery_driver);
}

late_initcall(max8903_battery_init);
module_exit(max8903_battery_exit);

MODULE_DESCRIPTION("battery driver");
MODULE_LICENSE("GPL");

