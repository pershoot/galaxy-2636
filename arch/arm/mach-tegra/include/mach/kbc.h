/*
 * arch/arm/mach-tegra/include/mach/kbc.h
 *
 * Platform definitions for tegra-kbc keyboard input driver
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef ASMARM_ARCH_TEGRA_KBC_H
#define ASMARM_ARCH_TEGRA_KBC_H

#include <linux/types.h>

#define KBC_MAX_ROW		16
#define KBC_MAX_COL		8
#define KBC_MAX_GPIO		(KBC_MAX_ROW + KBC_MAX_COL)
#define KBC_MAX_KPRESS_EVENT	8
#define KBC_MAX_KEY		(KBC_MAX_ROW * KBC_MAX_COL)

enum {
	kbc_pin_unused = 0,
	kbc_pin_row,
	kbc_pin_col,
};

struct tegra_kbc_wake_key {
	u8 row:4;
	u8 col:4;
};

struct tegra_kbc_pin_cfg {
	int pin_type;
	unsigned char num;
};
/**
 * struct tegra_kbc_platform_data - Tegra kbc specific platform data for kbc
 *                                  driver.
 * @debounce_cnt: Debaunce count in terms of clock ticks of 32KHz
 * @repeat_cnt: The time to start next scan after completing the current scan
 *              in terms of clock ticks of 32KHz clock
 * @scan_timeout_cnt: Number of clock count (32KHz) to keep scanning of keys
 *              after any key is pressed.
 * @plain_keycode: The key code array for keys in normal mode.
 * @fn_keycode:  The key code array for keys with function key pressed.
 * @is_filter_keys: Tells whether filter algorithms applied or not.
 * @kbc_pin_type: The type of kbc pin whether unused or column or row.
 * @is_wake_on_any_key: System whouls wakeup on any key or the key list from
 *                      wake_cfg.
 * @wake_key_cnt: Number of key count in wakeup list.
 */
struct tegra_kbc_platform_data {
	unsigned int debounce_cnt;
	unsigned int repeat_cnt;
	unsigned int scan_timeout_cnt;
	int *plain_keycode;
	int *fn_keycode;
	bool is_filter_keys;
	struct tegra_kbc_pin_cfg pin_cfg[KBC_MAX_GPIO];
	bool is_wake_on_any_key;
	struct tegra_kbc_wake_key *wake_cfg;
	int wake_key_cnt;
};
#endif
