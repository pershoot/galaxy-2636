/*
 * arch/arm/mach-tegra/include/mach/gpio-sec.h
 *
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __GPIO_SEC_H
#define __GPIO_SEC_H

#if defined(CONFIG_MACH_SAMSUNG_P3)
#include "gpio-p3.h"
#elif defined(CONFIG_MACH_SAMSUNG_P4)
#include "gpio-p4.h"
#elif defined(CONFIG_MACH_SAMSUNG_P4WIFI)
#include "gpio-p4wifi.h"
#elif defined(CONFIG_MACH_SAMSUNG_P4LTE)
#include "gpio-p4lte.h"
#elif defined(CONFIG_MACH_SAMSUNG_P5)
#include "gpio-p5.h"
#else
#error "Invalid machine type"
#endif

#if !defined(CONFIG_MACH_SAMSUNG_P5)
#define NO		0
#define YES		1

#define GPIO_OUTPUT	1
#define GPIO_INPUT	0

#define GPIO_LEVEL_HIGH		1
#define GPIO_LEVEL_LOW		0
#define GPIO_LEVEL_NONE		(-1)

#define GPIO_SLP_HOLD_PREVIOUS_LEVEL		GPIO_LEVEL_NONE

#define GPIO_I2C_SCL_GROUP_SLEEP_LEVEL		GPIO_LEVEL_HIGH
#define GPIO_I2C_SDA_GROUP_SLEEP_LEVEL		GPIO_LEVEL_HIGH

#define GPIO_EXT_PU_GROUP_SLEEP_LEVEL		GPIO_LEVEL_HIGH
#define GPIO_EXT_PD_GROUP_SLEEP_LEVEL		GPIO_LEVEL_LOW

#define GPIO_LDO_ENABLE_GROUP_SLEEP_LEVEL	GPIO_LEVEL_LOW

#define GPIO_CP_ON_PIN_GROUP_SLP_LEVEL		GPIO_LEVEL_HIGH

#define SPIO 0
#define GPIO 1
#define GPIO_INIT_LEVEL_IPC_BACK_POWERING	GPIO_LEVEL_LOW

struct sec_slp_gpio_cfg_st {
	int slp_ctrl;
	unsigned int gpio;
	int dir;
	int val;
};

struct sec_gpio_cfg_st {
	int attr;
	unsigned int gpio;
	int dir;
	int val;
};

struct sec_gpio_table_st {
	struct sec_gpio_cfg_st *init_gpio_table;
	int n_init_gpio_table;
	struct sec_slp_gpio_cfg_st *sleep_gpio_table;
	int n_sleep_gpio_table;
};

extern void tegra_gpio_register_table(struct sec_gpio_table_st *gpio_table);
#endif
#endif  /* __GPIO_SEC_H */
