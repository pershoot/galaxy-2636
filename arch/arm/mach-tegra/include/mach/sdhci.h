/*
 * include/asm-arm/arch-tegra/sdhci.h
 *
 * Copyright (C) 2009 Palm, Inc.
 * Author: Yvonne Yip <y@palm.com>
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
#ifndef __ASM_ARM_ARCH_TEGRA_SDHCI_H
#define __ASM_ARM_ARCH_TEGRA_SDHCI_H

#include <linux/mmc/host.h>
#include <linux/mmc/card.h>

struct tegra_sdhci_platform_data {
	const char *clk_id;
	int force_hs;
	int rt_disable;
	int cd_gpio;
	int cd_gpio_polarity;
	int wp_gpio;
	int wp_gpio_polarity;
	int power_gpio;
	bool is_voltage_switch_supported;
	bool is_8bit_supported;
	const char *vdd_rail_name;
	const char *slot_rail_name;
	int vdd_max_uv;
	int vdd_min_uv;
	unsigned int max_clk;
	unsigned int clk_limit;

	void (*board_probe)(int id, struct mmc_host *);
	void (*board_remove)(int id, struct mmc_host *);

	/* embedded sdio data */
	struct sdio_cis cis;
	struct sdio_cccr cccr;
	struct sdio_embedded_func *funcs;
	int num_funcs;

	/* card detect callback registration function */
	int (*register_status_notify)(void (*callback)(int card_present,
				void *dev_id), void *dev_id);
};

#endif
