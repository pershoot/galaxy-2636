/*
 * arch/arm/mach-tegra/fuse.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@android.com>
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

#if defined(CONFIG_ICS)
#include <mach/hardware.h>

#define INVALID_PROCESS_ID      99 /* don't expect to have 100 process id's */

unsigned long long tegra_chip_uid(void);
unsigned int tegra_spare_fuse(int bit);
#else

enum tegra_revision {
	TEGRA_REVISION_UNKNOWN = 0,
	TEGRA_REVISION_A02,
	TEGRA_REVISION_A03,
	TEGRA_REVISION_A03p,
	TEGRA_REVISION_A04,
	TEGRA_REVISION_MAX,
};
#endif

unsigned long long tegra_chip_uid(void);
unsigned int tegra_spare_fuse(int bit);
int tegra_sku_id(void);
int tegra_cpu_process_id(void);
int tegra_core_process_id(void);
#if defined(CONFIG_ICS)
int tegra_cpu_speedo_id(void);
int tegra_soc_speedo_id(void);
#endif
int tegra_soc_speedo_id(void);
void tegra_init_fuse(void);
void tegra_init_speedo_data(void);
u32 tegra_fuse_readl(unsigned long offset);
void tegra_fuse_writel(u32 value, unsigned long offset);
#if !defined(CONFIG_ICS)
enum tegra_revision tegra_get_revision(void);
#else
const char *tegra_get_revision_name(void);

static inline int tegra_package_id(void) { return -1; }
#endif
