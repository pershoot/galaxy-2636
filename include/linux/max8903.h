/*
 * include/linux/max8903.h
 *
 * Battery management driver for MAX8903 charger chip.
 *
 * Copyright (C) 2010 Samsung Electronics.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef __MAX8903_H__
#define __MAX8903_H__

#include <linux/power_supply.h>

enum cable_type_t {
	CABLE_TYPE_NONE = 0,
	CABLE_TYPE_USB,
	CABLE_TYPE_AC,
	CABLE_TYPE_AC_FAST,
	CABLE_TYPE_DEBUG
};

struct max8903_charger_data {
	int enable_line;
	int iset_line;
	int irq_line;
	int dok_line;
	int chg_line;
	void (*init)(void);
};

struct max8903_charger_callbacks {
	void (*cable_changed)(struct max8903_charger_callbacks *arg,
		enum cable_type_t status);
};

struct max8903_battery_platform_data {
	struct max8903_charger_data charger;
	struct max8903_charger_callbacks cable_callbacks;
	void (*register_cable_callbacks)(struct max8903_charger_callbacks *arg);
	struct power_supply *psy_fuelgauge;
	int temp_high_threshold;
	int temp_high_recovery;
	int temp_low_recovery;
	int temp_low_threshold;
	int recharge_duration;
	int recharge_delay;
	int charge_duration;
};

#endif /* __MAX8903_H__ */
