/*
 * Copyright (C) 2010 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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

#ifndef _TEGRA_USB_H_
#define _TEGRA_USB_H_

enum tegra_usb_operating_modes {
	TEGRA_USB_DEVICE,
	TEGRA_USB_HOST,
	TEGRA_USB_OTG,
};

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
enum tegra_host_state {
	TEGRA_USB_POWEROFF,
	TEGRA_USB_POWERON,
	TEGRA_USB_OVERCURRENT,
};
#endif

struct tegra_ehci_platform_data {
	enum tegra_usb_operating_modes operating_mode;
	/* power down the phy on bus suspend */
	int power_down_on_bus_suspend;
	void *phy_config;
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
/*	Don't merge with P3
        int currentlimit_irq;
*/
	int host_notify;
	int sec_whlist_table_num;
#endif
};

struct tegra_otg_platform_data {
	struct platform_device* (*host_register)(void);
	void (*host_unregister)(struct platform_device*);
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	void (*otg_en)(int);
	unsigned int **batt_level;
	int currentlimit_irq;
#endif
};

#endif /* _TEGRA_USB_H_ */
