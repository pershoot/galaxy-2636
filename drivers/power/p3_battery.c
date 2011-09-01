/*
 *  p3_battery.c
 *  charger systems for lithium-ion (Li+) batteries
 *
 *  Copyright (C) 2010 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/irq.h>
#include <linux/wakelock.h>
#include <linux/android_alarm.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <linux/earlysuspend.h>
#include <linux/io.h>
#include <mach/gpio.h>
#include <linux/power/p3_battery.h>
#include <linux/power/max17042_battery.h>

#ifdef CONFIG_KERNEL_DEBUG_SEC
#include <linux/kernel_sec_common.h>
#endif

#define FAST_POLL	40	/* 40 sec */
#define SLOW_POLL	(30*60)	/* 30 min */

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

enum charger_type {
	CHARGER_BATTERY = 0,
	CHARGER_USB,
	CHARGER_AC,
	CHARGER_DISCHARGE
};

struct battery_info {
	u32 batt_id;		/* Battery ID from ADC */
	s32 batt_vol;		/* Battery voltage from ADC */
	s32 batt_temp;		/* Battery Temperature (C) from ADC */
	s32 batt_current;	/* Battery current from ADC */
	u32 level;		/* formula */
	u32 charging_source;	/* 0: no cable, 1:usb, 2:AC */
	u32 charging_enabled;	/* 0: Disable, 1: Enable */
	u32 batt_health;	/* Battery Health (Authority) */
	u32 batt_is_full;       /* 0 : Not full 1: Full */
	u32 batt_is_recharging; /* 0 : Not recharging 1: Recharging */
	u32 batt_improper_ta; /* 1: improper ta */
};

struct battery_data {
	struct device		*dev;
	struct p3_battery_platform_data *pdata;
	struct battery_info	info;
	struct power_supply	psy_battery;
	struct power_supply	psy_usb;
	struct power_supply	psy_ac;
	struct workqueue_struct	*p3_TA_workqueue;
#ifdef CONFIG_TARGET_LOCALE_KOR
	struct workqueue_struct	*low_bat_comp_workqueue;
#endif
	struct work_struct	battery_work;
	struct work_struct	cable_work;
	struct delayed_work	TA_work;
	struct delayed_work	fuelgauge_work;
	struct delayed_work	fullcharging_work;
	struct delayed_work	full_comp_work;
#ifdef CONFIG_TARGET_LOCALE_KOR
	struct delayed_work	low_comp_work;
#endif
	struct alarm		alarm;
	struct mutex		work_lock;
	struct wake_lock	vbus_wake_lock;
	struct wake_lock	cable_wake_lock;
	struct wake_lock	work_wake_lock;
	struct wake_lock	fullcharge_wake_lock;
#ifdef CONFIG_TARGET_LOCALE_KOR
	struct wake_lock	low_comp_wake_lock;
#endif
#ifdef __TEST_DEVICE_DRIVER__ 
	struct wake_lock	wake_lock_for_dev;
#endif /* __TEST_DEVICE_DRIVER__ */
	enum charger_type	current_cable_status;
	enum charger_type	previous_cable_status;
	int		present;
	unsigned int	device_state;
	unsigned int	charging_mode_booting;
	int		p3_battery_initial;
	int		low_batt_boot_flag;
	u32		full_charge_comp_recharge_info;
	unsigned long	charging_start_time;
	int		connect_irq;
	bool		slow_poll;
	ktime_t		last_poll;
	int fg_skip;
	int fg_skip_cnt;
	int fg_chk_cnt;
	int recharging_cnt;
	int previous_charging_status;
#if !defined(CONFIG_MACH_SAMSUNG_P4) || !defined(CONFIG_MACH_SAMSUNG_P4WIFI) || !defined(CONFIG_MACH_SAMSUNG_P4LTE)
	int full_check_flag;
	bool is_first_check;
#endif
};

struct battery_data *test_batterydata;

#ifdef CONFIG_SAMSUNG_LPM_MODE
/* to notify lpm state to other modules */
int lpm_mode_flag;
EXPORT_SYMBOL(lpm_mode_flag);
#endif

static void p3_set_chg_en(struct battery_data *battery, int enable);
static void p3_cable_changed(struct battery_data *battery);
static ssize_t p3_bat_show_property(struct device *dev,
			struct device_attribute *attr,
			char *buf);
static ssize_t p3_bat_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count);

#ifdef __TEST_DEVICE_DRIVER__
static ssize_t sec_batt_test_show_property(struct device *dev,
                                      struct device_attribute *attr,
                                      char *buf);
static ssize_t sec_batt_test_store(struct device *dev, 
			     struct device_attribute *attr,
			     const char *buf, size_t count);
static int bat_temp_force_state = 0;
#endif /* __TEST_DEVICE_DRIVER__ */

static bool check_UV_charging_case(void);
static void p3_bat_status_update(struct power_supply *bat_ps);
extern int p3_low_batt_compensation(int fg_soc, int fg_vcell, int fg_current);
extern void reset_low_batt_comp_cnt(void);
extern int check_jig_on(void);
extern int get_fuelgauge_value(int data);
extern struct max17042_chip *max17042_chip_data;

static int check_ta_conn(struct battery_data *battery)
{
	u32 value;
	value = gpio_get_value(battery->pdata->charger.connect_line);
	pr_debug("%s.", value ? "TA NOT connected" : "TA connected");
	return !value;
}

#ifdef CONFIG_SAMSUNG_LPM_MODE
static void lpm_mode_check(struct battery_data *battery)
{
	extern int charging_mode_from_boot;

	pr_info("%s : charging_mode_from_boot(%d), ta_connected(%d)\n",
		__func__, charging_mode_from_boot, check_ta_conn(battery));

	if (!charging_mode_from_boot)
		return;

	if (check_ta_conn(battery)) {
		battery->charging_mode_booting = 1;
		lpm_mode_flag = 1;
		pr_info("%s : charging_mode_booting(%d)\n", __func__,
			battery->charging_mode_booting);
	} else {
		pr_info("%s: ta no longer connected, powering off\n", __func__);
		if (pm_power_off)
		{
#ifdef CONFIG_KERNEL_DEBUG_SEC
            kernel_sec_upload_cause_type upload_cause = kernel_sec_get_upload_cause();
            if (upload_cause == UPLOAD_CAUSE_INIT)
                /* Clear the magic number because it's normal reboot */
                kernel_sec_clear_upload_magic_number();
#endif
			pm_power_off();
		}
	}
}
#endif

static void set_chargcurr_high(struct battery_data *battery, int on)
{
	gpio_set_value(battery->pdata->charger.currentset_line, on);
	pr_debug("%s: Setting charing current to %s.",
		__func__, (on) ? "HIGH(2A)" : "LOW(500mA)");
}

static void p3_set_charging(struct battery_data *battery, int charger_type)
{
	switch (charger_type) {
	case 1:
		set_chargcurr_high(battery, 1);
		break;
	case 2:
		set_chargcurr_high(battery, 0);
	case 3: // maintain previous setting value when stop charging
	default:
		break;
	}
}

static void p3_battery_alarm(struct alarm *alarm)
{
	struct battery_data *battery =
			container_of(alarm, struct battery_data, alarm);

	pr_debug("%s : p3_battery_alarm.....\n", __func__);
	wake_lock(&battery->work_wake_lock);
	schedule_work(&battery->battery_work);
}

static void p3_program_alarm(struct battery_data *battery, int seconds)
{
	ktime_t low_interval = ktime_set(seconds - 10, 0);
	ktime_t slack = ktime_set(20, 0);
	ktime_t next;

	pr_debug("%s : p3_program_alarm.....\n", __func__);
	next = ktime_add(battery->last_poll, low_interval);
	alarm_start_range(&battery->alarm, next, ktime_add(next, slack));
}

static void p3_get_cable_status(struct battery_data *battery)
{
	if (check_ta_conn(battery)) {
		if (battery->pdata->check_dedicated_charger())
			battery->current_cable_status = CHARGER_AC;
		else
			battery->current_cable_status = CHARGER_USB;
		set_irq_type(gpio_to_irq(battery->pdata->charger.connect_line),
			IRQ_TYPE_LEVEL_HIGH);
		if (battery->pdata->inform_charger_connection)
			battery->pdata->inform_charger_connection(true);
	} else {
		battery->current_cable_status = CHARGER_BATTERY;
		set_irq_type(gpio_to_irq(battery->pdata->charger.connect_line),
			IRQ_TYPE_LEVEL_LOW);
		if (battery->pdata->inform_charger_connection)
			battery->pdata->inform_charger_connection(false);
		
		battery->info.batt_improper_ta = 0;  // clear flag
	}

	pr_info("%s: current_status : %d\n", __func__, battery->current_cable_status);
}

/* Processes when the interrupt line is asserted. */
static void p3_TA_work_handler(struct work_struct *work)
{
	struct battery_data *battery =
		container_of(work, struct battery_data, TA_work.work);

	p3_get_cable_status(battery);

	if (battery->previous_cable_status != battery->current_cable_status) {
		battery->previous_cable_status = battery->current_cable_status;
		p3_cable_changed(battery);
	}

	pr_info("p3_TA_work_handler : %d.\n", battery->current_cable_status);

	enable_irq(battery->connect_irq);
}

static irqreturn_t p3_TA_interrupt_handler(int irq, void *arg)
{
	struct battery_data *battery = (struct battery_data *)arg;

	pr_debug("p3_TA_interrupt_handler");
	disable_irq_nosync(irq);
	cancel_delayed_work(&battery->TA_work);
	queue_delayed_work(battery->p3_TA_workqueue, &battery->TA_work, 30);
	wake_lock_timeout(&battery->cable_wake_lock, HZ * 2);
	return IRQ_HANDLED;
}

static u32 get_charger_status(struct battery_data *battery)
{
	if (check_ta_conn(battery)) {
		if (battery->current_cable_status == CHARGER_USB)
			pr_info("Charger Status : USB\n");
		else
			pr_info("Charger Status : TA\n");
		return 1;
	} else {
		pr_info("Charger Status : Battery.\n");
		return 0;
	}
}

static int is_over_abs_time(struct battery_data *battery)
{
	unsigned int total_time;
	ktime_t ktime;
	struct timespec cur_time;

	if (!battery->charging_start_time)
		return 0;

	ktime = alarm_get_elapsed_realtime();
	cur_time = ktime_to_timespec(ktime);

	if (battery->info.batt_is_recharging)
		total_time = battery->pdata->recharge_duration;
	else
		total_time = battery->pdata->charge_duration;

	if (battery->charging_start_time + total_time < cur_time.tv_sec) {
		pr_info("Charging time out");
		return 1;
	} else
		return 0;
}

static int p3_get_bat_level(struct power_supply *bat_ps)
{
	struct battery_data *battery = container_of(bat_ps,
				struct battery_data, psy_battery);
	int fg_soc;
	int fg_vfsoc;
	int fg_vcell;
	int fg_current;
	int avg_current;
	int recover_flag = 0;

	recover_flag = fg_check_cap_corruption();

	/* check VFcapacity every five minutes */
	if (!(battery->fg_chk_cnt++ % 10)) {
		fg_check_vf_fullcap_range();
		battery->fg_chk_cnt = 1;
	}

	fg_soc = get_fuelgauge_value(FG_LEVEL);
	if (fg_soc < 0) {
		pr_info("Can't read soc!!!");
		fg_soc = battery->info.level;
	}

	if (!check_jig_on() && !max17042_chip_data->info.low_batt_comp_flag) {
		if (((fg_soc+5) < max17042_chip_data->info.previous_repsoc) ||
			(fg_soc > (max17042_chip_data->info.previous_repsoc+5)))
			battery->fg_skip = 1;
	}

	/* skip one time (maximum 30 seconds) because of corruption. */
	if (battery->fg_skip) {
		pr_info("%s: skip update until corruption check "
			"is done (fg_skip_cnt:%d)\n",
			__func__, ++battery->fg_skip_cnt);
		fg_soc = battery->info.level;
		if (recover_flag || battery->fg_skip_cnt > 10) {
			battery->fg_skip = 0;
			battery->fg_skip_cnt = 0;
		}
	}

	if (battery->low_batt_boot_flag) {
		fg_soc = 0;

		if (check_ta_conn(battery) && !check_UV_charging_case()) {
			fg_adjust_capacity();
			battery->low_batt_boot_flag = 0;
		}

		if (!check_ta_conn(battery))
			battery->low_batt_boot_flag = 0;
	}

	fg_vcell = get_fuelgauge_value(FG_VOLTAGE);
	if (fg_vcell < 0) {
		pr_info("Can't read vcell!!!");
		fg_vcell = battery->info.batt_vol;
	} else
		battery->info.batt_vol = fg_vcell;

	fg_current = get_fuelgauge_value(FG_CURRENT);
#if !defined(CONFIG_MACH_SAMSUNG_P4) || !defined(CONFIG_MACH_SAMSUNG_P4WIFI) || !defined(CONFIG_MACH_SAMSUNG_P4LTE)
	avg_current = get_fuelgauge_value(FG_CURRENT_AVG);
	fg_vfsoc = get_fuelgauge_value(FG_VF_SOC);

	// Algorithm for reducing time to fully charged (from MAXIM)
	if(battery->info.charging_enabled &&  // Charging is enabled
		!battery->info.batt_is_recharging &&  // Not Recharging
		battery->info.charging_source == CHARGER_AC &&  // Only AC (Not USB cable)
		!battery->is_first_check &&  // Skip when first check after boot up
		(fg_vfsoc>70 && (fg_current>20 && fg_current<250) &&
		(avg_current>20 && avg_current<260))) {

		if(battery->full_check_flag == 2) {
			pr_info("%s: force fully charged SOC !! (%d)", __func__, battery->full_check_flag);
			fg_set_full_charged();
			fg_soc = get_fuelgauge_value(FG_LEVEL);
		}
		else if(battery->full_check_flag < 2)
			pr_info("%s: full_check_flag (%d)", __func__, battery->full_check_flag);

		if(battery->full_check_flag++ > 10000)  // prevent overflow
			battery->full_check_flag = 3;
	}
	else
		battery->full_check_flag = 0;
#endif

	if (battery->info.charging_source == CHARGER_AC &&
		battery->info.batt_improper_ta == 0) {
		if (is_over_abs_time(battery)) {
			fg_soc = 100;
			battery->info.batt_is_full = 1;
			pr_info("%s: charging time is over", __func__);
			pr_info("%s: fg_vcell = %d, fg_soc = %d,"
				" is_full = %d\n",
				__func__, fg_vcell, fg_soc,
				battery->info.batt_is_full);
			p3_set_chg_en(battery, 0);
			goto __end__;
		}
	}

	if (fg_vcell <= battery->pdata->recharge_voltage) {
		if (battery->info.batt_is_full &&
			!battery->info.charging_enabled) {
			if (++battery->recharging_cnt > 1) {
				pr_info("recharging(under full)");
				battery->info.batt_is_recharging = 1;
				p3_set_chg_en(battery, 1);
				battery->recharging_cnt = 0;
				pr_info("%s: fg_vcell = %d, fg_soc = %d,"
					" is_recharging = %d\n",
					__func__, fg_vcell, fg_soc,
					battery->info.batt_is_recharging);
			}
		} else
			battery->recharging_cnt = 0;
	} else
		battery->recharging_cnt = 0;

	if (fg_soc > 100)
		fg_soc = 100;

	/*  Checks vcell level and tries to compensate SOC if needed.*/
	/*  If jig cable is connected, then skip low batt compensation check. */
#ifdef CONFIG_TARGET_LOCALE_KOR
	pr_info("%s : chek point 1, cond: %d!\n",__func__, max17042_chip_data->low_comp_pre_cond);
	if (!check_jig_on() && !battery->info.charging_enabled 
		&& max17042_chip_data->low_comp_pre_cond == 0) {
		if (p3_low_batt_compensation(fg_soc, fg_vcell, fg_current) == 1)
			pr_info("error ");
	}

	 if(max17042_chip_data->pre_cond_ok == 1) {
		max17042_chip_data->low_comp_pre_cond = 1;
		pr_info("%s : schedule low batt compensation! pre_count(%d), pre_condition(%d)\n",
			__func__, max17042_chip_data->pre_cond_ok, max17042_chip_data->low_comp_pre_cond);
		wake_lock(&battery->low_comp_wake_lock);
		queue_delayed_work(battery->low_bat_comp_workqueue, &battery->low_comp_work, 0);
	}
#else
	if (!check_jig_on() && !battery->info.charging_enabled)
		fg_soc = p3_low_batt_compensation(fg_soc, fg_vcell, fg_current);
#endif

__end__:
	pr_debug("fg_vcell = %d, fg_soc = %d, is_full = %d",
		fg_vcell, fg_soc, battery->info.batt_is_full);

#if !defined(CONFIG_MACH_SAMSUNG_P4) || !defined(CONFIG_MACH_SAMSUNG_P4WIFI) || !defined(CONFIG_MACH_SAMSUNG_P4LTE)
	if(battery->is_first_check)
		battery->is_first_check = false;
#endif

	if (battery->info.batt_is_full &&
		(battery->info.charging_source != CHARGER_USB))
		fg_soc = 100;
#if 0 // not used
	else {
		if (fg_soc >= 100)
			fg_soc = 99;
	}
#endif
	return fg_soc;
}

static int p3_get_bat_vol(struct power_supply *bat_ps)
{
	struct battery_data *battery = container_of(bat_ps,
				struct battery_data, psy_battery);
	return battery->info.batt_vol;
}

static bool check_UV_charging_case(void)
{
	int fg_vcell = get_fuelgauge_value(FG_VOLTAGE);
	int fg_current = get_fuelgauge_value(FG_CURRENT);
	int threshold = 0;

	if (get_fuelgauge_value(FG_BATTERY_TYPE) == SDI_BATTERY_TYPE)
		threshold = 3300 + ((fg_current * 17) / 100);
	else if (get_fuelgauge_value(FG_BATTERY_TYPE) == ATL_BATTERY_TYPE)
		threshold = 3300 + ((fg_current * 13) / 100);

	if (fg_vcell <= threshold)
		return 1;
	else
		return 0;
}

static void p3_set_time_for_charging(struct battery_data *battery, int mode)
{
	if (mode) {
		ktime_t ktime;
		struct timespec cur_time;

		ktime = alarm_get_elapsed_realtime();
		cur_time = ktime_to_timespec(ktime);

		/* record start time for abs timer */
		battery->charging_start_time = cur_time.tv_sec;
	} else {
		/* initialize start time for abs timer */
		battery->charging_start_time = 0;
	}
}

static void p3_set_chg_en(struct battery_data *battery, int enable)
{
	int chg_en_val = get_charger_status(battery);
	int charger_enable_line = battery->pdata->charger.enable_line;
	if (enable) {
		if (chg_en_val) {
			if (battery->current_cable_status == CHARGER_AC) {
				if (battery->pdata->check_dedicated_charger()
					== 1) {
					pr_info("%s: samsung charger!!\n",
						__func__);
					p3_set_charging(battery, 1);
				} else {
					pr_info("%s: improper charger!!\n",
						__func__);
					battery->info.batt_improper_ta = 1;
					p3_set_charging(battery, 2);
				}
				gpio_set_value(charger_enable_line, 0);
			} else if (battery->current_cable_status ==
				CHARGER_USB) {
				pr_info("USB charger!!");
				p3_set_charging(battery, 2);
				gpio_set_value(charger_enable_line, 0);
			} else {
				pr_info("else type charger!!");
				p3_set_charging(battery, 2);
				gpio_set_value(charger_enable_line, 0);
			}
			pr_info("%s: Enabling the external charger ", __func__);
			p3_set_time_for_charging(battery, 1);
		}
	} else {
		gpio_set_value(charger_enable_line, 1);
		if (battery->current_cable_status == CHARGER_BATTERY)  { // charging stopped by cable disconnected
			p3_set_charging(battery, 2);
		} else { // charging stopped with cable connected
			p3_set_charging(battery, 3);
		}
		udelay(10);
		pr_info("%s: Disabling the external charger ", __func__);
		p3_set_time_for_charging(battery, 0);
		battery->info.batt_is_recharging = 0;
	}

	battery->info.charging_enabled = enable;
}

static void p3_temp_control(struct battery_data *battery, int mode)
{
	switch (mode) {
	case POWER_SUPPLY_HEALTH_GOOD:
		pr_debug("GOOD");
		battery->info.batt_health = mode;
		break;
	case POWER_SUPPLY_HEALTH_OVERHEAT:
		pr_debug("OVERHEAT");
		battery->info.batt_health = mode;
		break;
	case POWER_SUPPLY_HEALTH_COLD:
		pr_debug("FREEZE");
		battery->info.batt_health = mode;
		break;
	default:
		break;
	}
	schedule_work(&battery->cable_work);
}

static int p3_get_bat_temp(struct power_supply *bat_ps)
{
	struct battery_data *battery = container_of(bat_ps,
				struct battery_data, psy_battery);
	int health = battery->info.batt_health;
	int update = 0;
	int battery_temp;
	int temp_high_threshold;
	int temp_high_recovery;
	int temp_low_threshold;
	int temp_low_recovery;

#ifdef CONFIG_MACH_SAMSUNG_P4LTE
	extern int charging_mode_from_boot;
#endif

	battery_temp = get_fuelgauge_value(FG_TEMPERATURE);

	temp_high_threshold = battery->pdata->temp_high_threshold;
	temp_high_recovery = battery->pdata->temp_high_recovery;
	temp_low_threshold = battery->pdata->temp_low_threshold;
	temp_low_recovery = battery->pdata->temp_low_recovery;

#ifdef CONFIG_MACH_SAMSUNG_P4LTE
	if (charging_mode_from_boot) {
		temp_high_threshold = battery->pdata->temp_high_threshold_lpm;
		temp_high_recovery = battery->pdata->temp_high_recovery_lpm;
		temp_low_threshold = battery->pdata->temp_low_threshold_lpm;
		temp_low_recovery = battery->pdata->temp_low_recovery_lpm;
	}
#endif

#ifdef __TEST_DEVICE_DRIVER__
	switch (bat_temp_force_state) {
	case 0:
		break;
	case 1:
		battery_temp = temp_high_threshold + 1;
		break;
	case 2:
		battery_temp = temp_low_threshold - 1;
		break;
	default:
		break;
	}
#endif /* __TEST_DEVICE_DRIVER__ */

	if (battery_temp >= temp_high_threshold) {
		if (health != POWER_SUPPLY_HEALTH_OVERHEAT &&
				health != POWER_SUPPLY_HEALTH_UNSPEC_FAILURE) {
			p3_temp_control(battery, POWER_SUPPLY_HEALTH_OVERHEAT);
			update = 1;
		}
	} else if (battery_temp <= temp_high_recovery &&
			battery_temp >= temp_low_recovery) {
		if (health == POWER_SUPPLY_HEALTH_OVERHEAT ||
				health == POWER_SUPPLY_HEALTH_COLD) {
			p3_temp_control(battery, POWER_SUPPLY_HEALTH_GOOD);
			update = 1;
		}
	} else if (battery_temp <= temp_low_threshold) {
		if (health != POWER_SUPPLY_HEALTH_COLD &&
				health != POWER_SUPPLY_HEALTH_UNSPEC_FAILURE) {
			p3_temp_control(battery, POWER_SUPPLY_HEALTH_COLD);
			update = 1;
		}
	}

	if (update)
		pr_info("p3_get_bat_temp = %d.", battery_temp);

	return battery_temp/100;
}

static int p3_bat_get_charging_status(struct battery_data *battery)
{
#ifdef CONFIG_MACH_SAMSUNG_P3
	switch (battery->info.charging_source) {
	case CHARGER_BATTERY:
	case CHARGER_USB:
		return POWER_SUPPLY_STATUS_NOT_CHARGING;
	case CHARGER_AC:
		if (battery->info.batt_is_full)
			return POWER_SUPPLY_STATUS_FULL;
		else if(battery->info.batt_improper_ta)
			return POWER_SUPPLY_STATUS_NOT_CHARGING;
		else
			return POWER_SUPPLY_STATUS_CHARGING;
	case CHARGER_DISCHARGE:
		return POWER_SUPPLY_STATUS_DISCHARGING;
	default:
		return POWER_SUPPLY_STATUS_UNKNOWN;
	}
#else // P4
	switch (battery->info.charging_source) {
	case CHARGER_BATTERY:
	case CHARGER_USB:
		return POWER_SUPPLY_STATUS_DISCHARGING;
	case CHARGER_AC:
		if (battery->info.batt_is_full ||
			battery->info.level == 100)
			return POWER_SUPPLY_STATUS_FULL;
		else if(battery->info.batt_improper_ta)
			return POWER_SUPPLY_STATUS_DISCHARGING;
		else
			return POWER_SUPPLY_STATUS_CHARGING;
	case CHARGER_DISCHARGE:
		return POWER_SUPPLY_STATUS_NOT_CHARGING;
	default:
		return POWER_SUPPLY_STATUS_UNKNOWN;
	}
#endif
}

static int p3_bat_get_property(struct power_supply *bat_ps,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct battery_data *battery = container_of(bat_ps,
				struct battery_data, psy_battery);

	pr_debug("psp = %d", psp);
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = p3_bat_get_charging_status(battery);
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = battery->info.batt_health;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = battery->present;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = battery->info.level;
		pr_debug("level = %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = battery->info.batt_temp;
		pr_debug("temp = %d\n", val->intval);
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

	enum charger_type charger = battery->info.charging_source;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
#ifdef CONFIG_MACH_SAMSUNG_P3
		val->intval = 0;
#else // P4
		val->intval = (charger == CHARGER_USB ? 1 : 0);
#endif
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

	enum charger_type charger = battery->info.charging_source;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = (charger == CHARGER_AC ? 1 : 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

#define SEC_BATTERY_ATTR(_name)\
{\
	.attr = { .name = #_name, .mode = S_IRUGO | (S_IWUSR | S_IWGRP) },\
	.show = p3_bat_show_property,\
	.store = p3_bat_store,\
}

static struct device_attribute p3_battery_attrs[] = {
	SEC_BATTERY_ATTR(batt_vol),
	SEC_BATTERY_ATTR(batt_temp),
#ifdef CONFIG_MACH_SAMSUNG_P5
	SEC_BATTERY_ATTR(batt_temp_cels),
#endif
	SEC_BATTERY_ATTR(batt_current),
	SEC_BATTERY_ATTR(charging_source),
	SEC_BATTERY_ATTR(fg_soc),
	SEC_BATTERY_ATTR(reset_soc),
	SEC_BATTERY_ATTR(reset_cap),
	SEC_BATTERY_ATTR(fg_reg),
	SEC_BATTERY_ATTR(batt_type),
	SEC_BATTERY_ATTR(batt_temp_check),
	SEC_BATTERY_ATTR(batt_full_check),
#ifdef CONFIG_SAMSUNG_LPM_MODE
	SEC_BATTERY_ATTR(charging_mode_booting),
	SEC_BATTERY_ATTR(voltage_now),
#endif
};

enum {
	BATT_VOL = 0,
	BATT_TEMP,
#ifdef CONFIG_MACH_SAMSUNG_P5
	BATT_TEMP_CELS,
#endif
	BATT_CURRENT,
	BATT_CHARGING_SOURCE,
	BATT_FG_SOC,
	BATT_RESET_SOC,
	BATT_RESET_CAP,
	BATT_FG_REG,
	BATT_BATT_TYPE,
	BATT_TEMP_CHECK,
	BATT_FULL_CHECK,
#ifdef CONFIG_SAMSUNG_LPM_MODE
	CHARGING_MODE_BOOTING,
	VOLTAGE_NOW,
#endif
};

static int p3_bat_create_attrs(struct device *dev)
{
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(p3_battery_attrs); i++) {
		rc = device_create_file(dev, &p3_battery_attrs[i]);
		if (rc)
			goto p3_attrs_failed;
	}
	return 0;

p3_attrs_failed:
	while (i--)
		device_remove_file(dev, &p3_battery_attrs[i]);
	return rc;
}

static ssize_t p3_bat_show_property(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	int i = 0;
#ifdef CONFIG_MACH_SAMSUNG_P5	
	s32 temp = 0;
#endif	
	u8 batt_str[5];
	const ptrdiff_t off = attr - p3_battery_attrs;

	switch (off) {
	case BATT_VOL:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
		get_fuelgauge_value(FG_VOLTAGE));
		break;
	case BATT_TEMP:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
		test_batterydata->info.batt_temp);
		break;
#ifdef CONFIG_MACH_SAMSUNG_P5		
	case BATT_TEMP_CELS:
			temp = ((test_batterydata->info.batt_temp) / 10);
			i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			temp);
			break;
#endif			
	case BATT_CURRENT:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
		get_fuelgauge_value(FG_CURRENT_AVG));
		break;
	case BATT_CHARGING_SOURCE:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
		test_batterydata->info.charging_source);
		break;
	case BATT_FG_SOC:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
		get_fuelgauge_value(FG_LEVEL));
		break;
	case BATT_BATT_TYPE:
		if (get_fuelgauge_value(FG_BATTERY_TYPE) == SDI_BATTERY_TYPE)
			sprintf(batt_str, "SDI");
		else if (get_fuelgauge_value(FG_BATTERY_TYPE) ==
			ATL_BATTERY_TYPE)
			sprintf(batt_str, "ATL");
		i += scnprintf(buf + i, PAGE_SIZE - i, "%s_%s\n",
		batt_str, batt_str);
		break;
	case BATT_TEMP_CHECK:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
		test_batterydata->info.batt_health);
		break;
	case BATT_FULL_CHECK:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
		test_batterydata->info.batt_is_full);
		break;
#ifdef CONFIG_SAMSUNG_LPM_MODE
	case CHARGING_MODE_BOOTING:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			test_batterydata->charging_mode_booting);
		break;
	case VOLTAGE_NOW:
		i += scnprintf(buf + i, PAGE_SIZE - i, "%d\n",
			get_fuelgauge_value(FG_VOLTAGE));
		break;
#endif
	default:
		i = -EINVAL;
	}

	return i;
}

static ssize_t p3_bat_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	int x = 0;
	int ret = 0;
	const ptrdiff_t off = attr - p3_battery_attrs;

	switch (off) {
	case BATT_RESET_SOC:
		if (sscanf(buf, "%d\n", &x) == 1) {
			if (x == 1) {
				if (!check_ta_conn(test_batterydata))
					fg_reset_soc();
			}
			ret = count;
		}
		p3_bat_status_update(&test_batterydata->psy_battery);
		pr_debug("Reset SOC:%d ", x);
		break;
	case BATT_RESET_CAP:
		if (sscanf(buf, "%d\n", &x) == 1 ||
			x == 2 || x == 3 || x == 4) {
			if (x == 1 || x == 2 || x == 3 || x == 4)
				fg_reset_capacity();
			ret = count;
		}
		pr_debug("%s: Reset CAP:%d\n", __func__, x);
		break;
	case BATT_FG_REG:
		if (sscanf(buf, "%d\n", &x) == 1) {
			if (x == 1)
				fg_periodic_read();
			ret = count;
		}
		pr_debug("%s: FG Register:%d\n", __func__, x);
		break;
#ifdef CONFIG_SAMSUNG_LPM_MODE
	case CHARGING_MODE_BOOTING:
		if (sscanf(buf, "%d\n", &x) == 1) {
			test_batterydata->charging_mode_booting = x;
			ret = count;
		}
		break;
#endif
	default:
		ret = -EINVAL;
	}
	return ret;
}

#ifdef __TEST_DEVICE_DRIVER__
#define SEC_TEST_ATTR(_name)\
{\
	.attr = { .name = #_name, .mode = S_IRUGO | (S_IWUSR | S_IWGRP) },\
	.show = sec_batt_test_show_property,\
	.store = sec_batt_test_store,\
}

static struct device_attribute sec_batt_test_attrs[] = {
	SEC_TEST_ATTR(suspend_lock),
	SEC_TEST_ATTR(control_temp),
};

enum {
	SUSPEND_LOCK = 0,
	CTRL_TEMP,
};

static int sec_batt_test_create_attrs(struct device * dev)
{
        int i, ret;
        
        for (i = 0; i < ARRAY_SIZE(sec_batt_test_attrs); i++) {
                ret = device_create_file(dev, &sec_batt_test_attrs[i]);
                if (ret)
                        goto sec_attrs_failed;
        }
        goto succeed;
        
sec_attrs_failed:
        while (i--)
                device_remove_file(dev, &sec_batt_test_attrs[i]);
succeed:        
        return ret;
}

static ssize_t sec_batt_test_show_property(struct device *dev,
                                      struct device_attribute *attr,
                                      char *buf)
{
	int i = 0;
	const ptrdiff_t off = attr - sec_batt_test_attrs;

	switch (off) {
	default:
		i = -EINVAL;
	}       

	return i;
}

static ssize_t sec_batt_test_store(struct device *dev, 
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	int mode = 0;
	int ret = 0;
	const ptrdiff_t off = attr - sec_batt_test_attrs;

	switch (off) {
	case SUSPEND_LOCK:
		if (sscanf(buf, "%d\n", &mode) == 1) {
			dev_dbg(dev, "%s: suspend lock (%d)\n", __func__, mode);
			if (mode) 
				wake_lock(&test_batterydata->wake_lock_for_dev);
			else
				wake_lock_timeout(&test_batterydata->wake_lock_for_dev, HZ / 2);
			ret = count;
		}
		break;

	case CTRL_TEMP:
		if (sscanf(buf, "%d\n", &mode) == 1) {
			dev_info(dev, "%s: control temp (%d)\n", __func__, mode);
			bat_temp_force_state = mode;
			ret = count;
		}
		break;

	default:
		ret = -EINVAL;
	}       

	return ret;
}
#endif /* __TEST_DEVICE_DRIVER__ */

extern int check_usb_status;
static int p3_cable_status_update(struct battery_data *battery, int status)
{
	int ret = 0;
	enum charger_type source = CHARGER_BATTERY;

	pr_debug("Update cable status ");

	if (!battery->p3_battery_initial)
		return -EPERM;

	switch (status) {
	case CHARGER_BATTERY:
		pr_info("cable NOT PRESENT ");
		battery->info.charging_source = CHARGER_BATTERY;
		break;
	case CHARGER_USB:
		pr_info("cable USB ");
		battery->info.charging_source = CHARGER_USB;
		break;
	case CHARGER_AC:
		pr_info("cable AC ");
		battery->info.charging_source = CHARGER_AC;
		break;
	case CHARGER_DISCHARGE:
		pr_info("Discharge");
		battery->info.charging_source = CHARGER_DISCHARGE;
		break;
	default:
		pr_info("Not supported status");
		ret = -EINVAL;
	}
	check_usb_status = source = battery->info.charging_source;

	if (source == CHARGER_USB) {
		wake_lock(&battery->vbus_wake_lock);
	} else {
		/* give userspace some time to see the uevent and update
		* LED state or whatnot...
		*/
		if (!get_charger_status(battery)) {
			if (battery->charging_mode_booting)
				wake_lock_timeout(&battery->vbus_wake_lock,
						5 * HZ);
			else
				wake_lock_timeout(&battery->vbus_wake_lock,
						HZ / 2);
		}
	}

	wake_lock(&battery->work_wake_lock);
	schedule_work(&battery->battery_work);

	pr_debug("call power_supply_changed ");

	return ret;
}

static void p3_bat_status_update(struct power_supply *bat_ps)
{
	struct battery_data *battery = container_of(bat_ps,
				struct battery_data, psy_battery);

	int old_level, old_temp, old_health, old_is_full;
	int current_charging_status;

	if (!battery->p3_battery_initial)
		return;

	mutex_lock(&battery->work_lock);
	old_temp = battery->info.batt_temp;
	old_health = battery->info.batt_health;
	old_level = battery->info.level;
	old_is_full = battery->info.batt_is_full;
	battery->info.batt_temp = p3_get_bat_temp(bat_ps);

	battery->info.level = p3_get_bat_level(bat_ps);
	if (!battery->info.charging_enabled &&
			!battery->info.batt_is_full &&
			!check_jig_on()) {
		if (battery->info.level > old_level)
			battery->info.level = old_level;
	}
	battery->info.batt_vol = p3_get_bat_vol(bat_ps);

	current_charging_status =
		gpio_get_value(battery->pdata->charger.fullcharge_line);
	if (current_charging_status >= 0) {
		if (check_ta_conn(battery) &&
			battery->previous_charging_status == 0 &&
			current_charging_status == 1) {
			pr_debug("BATTERY FULL");
			cancel_delayed_work(&battery->fullcharging_work);
			schedule_delayed_work(&battery->fullcharging_work,
					msecs_to_jiffies(300));
			wake_lock_timeout(&battery->fullcharge_wake_lock,
					HZ * 30);
		}
		battery->previous_charging_status = current_charging_status;
	}
	power_supply_changed(bat_ps);
	pr_debug("call power_supply_changed");

	mutex_unlock(&battery->work_lock);
}

static void p3_cable_check_status(struct battery_data *battery)
{
	enum charger_type status = 0;

	mutex_lock(&battery->work_lock);

	if (get_charger_status(battery)) {
		if (battery->info.batt_health != POWER_SUPPLY_HEALTH_GOOD) {
			pr_info("Unhealth battery state! ");
			status = CHARGER_DISCHARGE;
			p3_set_chg_en(battery, 0);
			goto __end__;
		}

		status = battery->current_cable_status;

		p3_set_chg_en(battery, 1);

		max17042_chip_data->info.low_batt_comp_flag = 0;
		reset_low_batt_comp_cnt();

	} else {
		status = CHARGER_BATTERY;
		p3_set_chg_en(battery, 0);
	}
__end__:
	p3_cable_status_update(battery, status);
	mutex_unlock(&battery->work_lock);
}

static void p3_bat_work(struct work_struct *work)
{
	struct battery_data *battery =
		container_of(work, struct battery_data, battery_work);
	unsigned long flags;

	pr_debug("bat work ");
	p3_bat_status_update(&battery->psy_battery);
	battery->last_poll = alarm_get_elapsed_realtime();

	/* prevent suspend before starting the alarm */
	local_irq_save(flags);
	wake_unlock(&battery->work_wake_lock);
	p3_program_alarm(battery, FAST_POLL);
	local_irq_restore(flags);
}

static void p3_cable_work(struct work_struct *work)
{
	struct battery_data *battery =
		container_of(work, struct battery_data, cable_work);
	pr_debug("cable work ");
	p3_cable_check_status(battery);
}

static int p3_bat_suspend(struct device *dev)
{
	struct battery_data *battery = dev_get_drvdata(dev);
	pr_info("bat suspend");

	if (!get_charger_status(battery)) {
		p3_program_alarm(battery, SLOW_POLL);
		battery->slow_poll = 1;
	}

	return 0;
}

static void p3_bat_resume(struct device *dev)
{
	struct battery_data *battery = dev_get_drvdata(dev);
	pr_info("bat resume start\n");

	if (battery->slow_poll) {
		p3_program_alarm(battery, FAST_POLL);
		battery->slow_poll = 0;
	}

	wake_lock(&battery->work_wake_lock);
	schedule_work(&battery->battery_work);
	pr_info("bat resume end\n");
}

static void p3_cable_changed(struct battery_data *battery)
{
	pr_debug("charger changed ");

	if (!battery->p3_battery_initial)
		return;

	if (!battery->charging_mode_booting)
		battery->info.batt_is_full = 0;

	battery->info.batt_health = POWER_SUPPLY_HEALTH_GOOD;

	schedule_work(&battery->cable_work);

	/*
	 * Wait a bit before reading ac/usb line status and setting charger,
	 * because ac/usb status readings may lag from irq.
	 */

	battery->last_poll = alarm_get_elapsed_realtime();
	p3_program_alarm(battery, FAST_POLL);
}

void p3_cable_charging(struct battery_data *battery)
{
	battery->full_charge_comp_recharge_info =
		battery->info.batt_is_recharging;

	if (!battery->p3_battery_initial)
		return;

	if (battery->info.charging_enabled &&
		battery->info.batt_health == POWER_SUPPLY_HEALTH_GOOD) {
		p3_set_chg_en(battery, 0);
		battery->info.batt_is_full = 1;
		/* full charge compensation algorithm by MAXIM */
		fg_fullcharged_compensation(
			battery->full_charge_comp_recharge_info, 1);

		cancel_delayed_work(&battery->full_comp_work);
		schedule_delayed_work(&battery->full_comp_work, 100);

		pr_debug("battery is full charged ");
	}

	wake_lock(&battery->work_wake_lock);
	schedule_work(&battery->battery_work);

}

static irqreturn_t low_battery_isr(int irq, void *arg)
{
	struct battery_data *battery = (struct battery_data *)arg;
	pr_debug("low battery isr");
	cancel_delayed_work(&battery->fuelgauge_work);
	schedule_delayed_work(&battery->fuelgauge_work, 0);

	return IRQ_HANDLED;
}

int _low_battery_alarm_(struct battery_data *battery)
{
	if (!get_charger_status(battery)) {
		pr_info("soc = 0! Power Off!! ");
		battery->info.level = 0;
		wake_lock_timeout(&battery->vbus_wake_lock, HZ);
		power_supply_changed(&battery->psy_battery);
	}

	return 0;
}

static void fuelgauge_work_handler(struct work_struct *work)
{
	struct battery_data *battery =
		container_of(work, struct battery_data, fuelgauge_work.work);
	pr_info("low battery alert!");
	if (get_fuelgauge_value(FG_CHECK_STATUS))
		_low_battery_alarm_(battery);
}

static void full_comp_work_handler(struct work_struct *work)
{
	struct battery_data *battery =
		container_of(work, struct battery_data, full_comp_work.work);
	int avg_current = get_fuelgauge_value(FG_CURRENT_AVG);

	if (avg_current >= 25) {
		cancel_delayed_work(&battery->full_comp_work);
		schedule_delayed_work(&battery->full_comp_work, 100);
	} else {
		pr_info("%s: full charge compensation start (avg_current %d)\n",
			__func__, avg_current);
		fg_fullcharged_compensation(
			battery->full_charge_comp_recharge_info, 0);
	}
}

static void fullcharging_work_handler(struct work_struct *work)
{
	struct battery_data *battery =
		container_of(work, struct battery_data, fullcharging_work.work);
	pr_info("%s : nCHG rising intr!!", __func__);

	if (gpio_get_value(battery->pdata->charger.fullcharge_line) == 1
		&& get_charger_status(battery) != 0 && battery->info.level >= 70
		&& battery->current_cable_status != CHARGER_BATTERY)
		p3_cable_charging(battery);

}

#ifdef CONFIG_TARGET_LOCALE_KOR
static void low_comp_work_handler(struct work_struct *work)
{
	struct battery_data *battery =
		container_of(work, struct battery_data, low_comp_work.work);
	int fg_soc;
	int fg_vcell;
	int fg_current;	
	int i, comp_result;

	fg_soc = battery->info.level;

	for (i = 0 ; i<10 ; i++) {
		fg_vcell = get_fuelgauge_value(FG_VOLTAGE);
		fg_current = get_fuelgauge_value(FG_CURRENT);
		pr_info("%s : running", __func__);

		comp_result = p3_low_batt_compensation(fg_soc, fg_vcell, fg_current);
		if (comp_result == 2) {
			pr_info("%s : soc(%d), vcell(%d), current(%d), count(%d), pre_count(%d), pre_condition(%d)\n",
				__func__, fg_soc, fg_vcell, fg_current, i,
				max17042_chip_data->pre_cond_ok, max17042_chip_data->low_comp_pre_cond);
			break;
		}
		else if (comp_result == 1) {
			pr_info("%s : low compensation occurred!, vcell(%d), current(%d)\n",
				__func__, fg_vcell, fg_current);
			wake_lock(&battery->work_wake_lock);
			schedule_work(&battery->battery_work);
			break;
		}
		msleep(2000);
	}
	wake_unlock(&battery->low_comp_wake_lock);
}
#endif

static int __devinit p3_bat_probe(struct platform_device *pdev)
{
	struct p3_battery_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct battery_data *battery;
	int ret;
	unsigned long trigger;
	int irq_num;

	if (!pdata) {
		pr_err("%s : No platform data\n", __func__);
		return -EINVAL;
	}
	
	battery = kzalloc(sizeof(*battery), GFP_KERNEL);
	if (!battery)
		return -ENOMEM;

	battery->pdata = pdata;
	
	battery->pdata->init_charger_gpio();

	platform_set_drvdata(pdev, battery);
	test_batterydata = battery;

	battery->present = 1;
	battery->info.level = 100;
	battery->info.charging_source = CHARGER_BATTERY;
	battery->info.batt_health = POWER_SUPPLY_HEALTH_GOOD;
#if !defined(CONFIG_MACH_SAMSUNG_P4) || !defined(CONFIG_MACH_SAMSUNG_P4WIFI) || !defined(CONFIG_MACH_SAMSUNG_P4LTE)
	battery->is_first_check = true;
#endif

	battery->psy_battery.name = "battery";
	battery->psy_battery.type = POWER_SUPPLY_TYPE_BATTERY;
	battery->psy_battery.properties = p3_battery_properties;
	battery->psy_battery.num_properties = ARRAY_SIZE(p3_battery_properties);
	battery->psy_battery.get_property = p3_bat_get_property;

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

	wake_lock_init(&battery->vbus_wake_lock, WAKE_LOCK_SUSPEND,
		"vbus wake lock");
	wake_lock_init(&battery->work_wake_lock, WAKE_LOCK_SUSPEND,
		"batt_work wake lock");
	wake_lock_init(&battery->cable_wake_lock, WAKE_LOCK_SUSPEND,
		"temp wake lock");
	wake_lock_init(&battery->fullcharge_wake_lock, WAKE_LOCK_SUSPEND,
		"fullcharge wake lock");
#ifdef CONFIG_TARGET_LOCALE_KOR
	wake_lock_init(&battery->low_comp_wake_lock, WAKE_LOCK_SUSPEND,
		"low comp wake lock");
#endif
#ifdef __TEST_DEVICE_DRIVER__
	wake_lock_init(&battery->wake_lock_for_dev, WAKE_LOCK_SUSPEND,
		"test mode wake lock");
#endif /* __TEST_DEVICE_DRIVER__ */

	INIT_WORK(&battery->battery_work, p3_bat_work);
	INIT_WORK(&battery->cable_work, p3_cable_work);
	INIT_DELAYED_WORK(&battery->fuelgauge_work, fuelgauge_work_handler);
	INIT_DELAYED_WORK(&battery->fullcharging_work,
			fullcharging_work_handler);
	INIT_DELAYED_WORK(&battery->full_comp_work, full_comp_work_handler);
	INIT_DELAYED_WORK(&battery->TA_work, p3_TA_work_handler);
#ifdef CONFIG_TARGET_LOCALE_KOR
	INIT_DELAYED_WORK(&battery->low_comp_work, low_comp_work_handler);
#endif
	battery->p3_TA_workqueue = create_singlethread_workqueue(
		"p3_TA_workqueue");
#ifdef CONFIG_TARGET_LOCALE_KOR
	battery->low_bat_comp_workqueue = create_singlethread_workqueue(
		"low_bat_comp_workqueue");
#endif
	if (!battery->p3_TA_workqueue) {
		pr_err("Failed to create single workqueue\n");
		ret = -ENOMEM;
		goto err_workqueue_init;
	}
#ifdef CONFIG_TARGET_LOCALE_KOR
	if (!battery->low_bat_comp_workqueue) {
		pr_err("Failed to create low_bat_comp_workqueue workqueue\n");
		ret = -ENOMEM;
		goto err_workqueue_init;
	}
#endif

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

	/* create sec detail attributes */
	p3_bat_create_attrs(battery->psy_battery.dev);

#ifdef __TEST_DEVICE_DRIVER__
	sec_batt_test_create_attrs(battery->psy_ac.dev);
#endif /* __TEST_DEVICE_DRIVER__ */

	battery->p3_battery_initial = 1;
	battery->low_batt_boot_flag = 0;

	battery->connect_irq = gpio_to_irq(pdata->charger.connect_line);
	if (check_ta_conn(battery))
		trigger = IRQF_TRIGGER_HIGH;
	else
		trigger = IRQF_TRIGGER_LOW;

	if (request_irq(battery->connect_irq, p3_TA_interrupt_handler, trigger,
			"TA_CON intr", battery)) {
		pr_err("p3_TA_interrupt_handler register failed!\n");
		goto err_charger_irq;
	}
	disable_irq(battery->connect_irq);

	// Get initial cable status and enable connection irq.
	p3_get_cable_status(battery);
	battery->previous_cable_status = battery->current_cable_status;
	enable_irq(battery->connect_irq);

	if (check_ta_conn(battery) && check_UV_charging_case())
		battery->low_batt_boot_flag = 1;

	mutex_lock(&battery->work_lock);
	fg_alert_init();
	mutex_unlock(&battery->work_lock);

	p3_bat_status_update(&battery->psy_battery);

	p3_cable_check_status(battery);

	/* before enable fullcharge interrupt, check fullcharge */
	if (battery->info.charging_source == CHARGER_AC
		&& battery->info.charging_enabled
		&& gpio_get_value(pdata->charger.fullcharge_line) == 1)
		p3_cable_charging(battery);

	/* Request IRQ */
	irq_num = gpio_to_irq(max17042_chip_data->pdata->fuel_alert_line);
	if (request_irq(irq_num, low_battery_isr,
			IRQF_TRIGGER_FALLING, "FUEL_ALRT irq", battery))
		pr_err("Can NOT request irq 'IRQ_FUEL_ALERT' %d ", irq_num);

#ifdef CONFIG_SAMSUNG_LPM_MODE
	lpm_mode_check(battery);
#endif

	return 0;

err_charger_irq:
	alarm_cancel(&battery->alarm);
	power_supply_unregister(&battery->psy_ac);
err_ac_psy_register:
	power_supply_unregister(&battery->psy_usb);
err_usb_psy_register:
	power_supply_unregister(&battery->psy_battery);
err_battery_psy_register:
	destroy_workqueue(battery->p3_TA_workqueue);
#ifdef CONFIG_TARGET_LOCALE_KOR
	destroy_workqueue(battery->low_bat_comp_workqueue);
#endif
err_workqueue_init:
	wake_lock_destroy(&battery->vbus_wake_lock);
	wake_lock_destroy(&battery->work_wake_lock);
	wake_lock_destroy(&battery->cable_wake_lock);
	wake_lock_destroy(&battery->fullcharge_wake_lock);
#ifdef CONFIG_TARGET_LOCALE_KOR
	wake_lock_destroy(&battery->low_comp_wake_lock);
#endif
	mutex_destroy(&battery->work_lock);
	kfree(battery);

	return ret;
}

static int __devexit p3_bat_remove(struct platform_device *pdev)
{
	struct battery_data *battery = platform_get_drvdata(pdev);

	free_irq(gpio_to_irq(max17042_chip_data->pdata->fuel_alert_line), NULL);
	free_irq(gpio_to_irq(battery->pdata->charger.connect_line), NULL);

	alarm_cancel(&battery->alarm);
	power_supply_unregister(&battery->psy_ac);
	power_supply_unregister(&battery->psy_usb);
	power_supply_unregister(&battery->psy_battery);

	destroy_workqueue(battery->p3_TA_workqueue);
#ifdef CONFIG_TARGET_LOCALE_KOR
	destroy_workqueue(battery->low_bat_comp_workqueue);
#endif
	cancel_delayed_work(&battery->fuelgauge_work);
	cancel_delayed_work(&battery->fullcharging_work);
	cancel_delayed_work(&battery->full_comp_work);

	wake_lock_destroy(&battery->vbus_wake_lock);
	wake_lock_destroy(&battery->work_wake_lock);
	wake_lock_destroy(&battery->cable_wake_lock);
	wake_lock_destroy(&battery->fullcharge_wake_lock);
#ifdef CONFIG_TARGET_LOCALE_KOR
	wake_lock_destroy(&battery->low_comp_wake_lock);
#endif
	mutex_destroy(&battery->work_lock);

	kfree(battery);

	return 0;
}

static const struct dev_pm_ops p3_battery_pm_ops = {
	.prepare = p3_bat_suspend,
	.complete = p3_bat_resume,
};

static struct platform_driver p3_bat_driver = {
	.driver = {
		.name = "p3-battery",
		.owner  = THIS_MODULE,
		.pm = &p3_battery_pm_ops,
	},
	.probe		= p3_bat_probe,
	.remove		= __devexit_p(p3_bat_remove),
};

static int __init p3_bat_init(void)
{
	return platform_driver_register(&p3_bat_driver);
}

static void __exit p3_bat_exit(void)
{
	platform_driver_unregister(&p3_bat_driver);
}

late_initcall(p3_bat_init);
module_exit(p3_bat_exit);

MODULE_DESCRIPTION("battery driver");
MODULE_LICENSE("GPL");

