/*
 * Copyright (C) 2008 Samsung Electronics, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __T20_WM8994_H
#define __T20_WM8994_H

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
#include <mach/tegra_das.h>
#endif

struct wm8994_platform_data {
	int ldo;
	void (*set_mic_bias)(bool on);
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	void (*set_dap_connection)(bool on);
#endif
};

#endif
