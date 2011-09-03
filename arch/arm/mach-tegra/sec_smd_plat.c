/* linux/arch/arm/mach-tegra/p3_smd_plat.c
 * Copyright (C) 2010 Samsung Electronics. All rights reserved.
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <mach/gpio.h>
#include "gpio-names.h"

#define MAIN_REV05	0x0B
#define REAL_REV00	0x0C
#define REAL_REV01	0x0D
#define REAL_REV02	0x0F
#define REAL_REV03	0x10

#define HW_REV	REAL_REV02

#if (HW_REV == MAIN_REV05)

#define P3_GPIO_CP_ACT              TEGRA_GPIO_PS5
#define P3_GPIO_CP_RST              TEGRA_GPIO_PK6 
#define P3_GPIO_CP_ON               TEGRA_GPIO_PO3
#define P3_GPIO_AP_ACT              TEGRA_GPIO_PV1
#define P3_GPIO_SIM_DETECT          TEGRA_GPIO_PC7
#define P3_GPIO_CP_REQ              TEGRA_GPIO_PQ3
#define P3_GPIO_HSIC_ACTIVE         TEGRA_GPIO_PQ5
#define P3_GPIO_HSIC_SUS_REQ        TEGRA_GPIO_PQ6
#define P3_GPIO_HSIC_EN             TEGRA_GPIO_PV0

#elif (HW_REV == REAL_REV00)

#define P3_GPIO_CP_ACT              TEGRA_GPIO_PS5
#define P3_GPIO_CP_RST              TEGRA_GPIO_PX1 
#define P3_GPIO_CP_ON               TEGRA_GPIO_PO3
#define P3_GPIO_AP_ACT            	TEGRA_GPIO_PV1
#define P3_GPIO_SIM_DETECT          TEGRA_GPIO_PC7
#define P3_GPIO_CP_REQ              TEGRA_GPIO_PQ3
#define P3_GPIO_HSIC_EN             TEGRA_GPIO_PV0
#define P3_GPIO_HSIC_ACTIVE         TEGRA_GPIO_PR4
#define P3_GPIO_HSIC_SUS_REQ        TEGRA_GPIO_PQ0
#define P3_GPIO_SLAVE_WAKEUP        TEGRA_GPIO_PQ5
#define P3_GPIO_HOST_WAKEUP         TEGRA_GPIO_PQ6

#elif ((HW_REV == REAL_REV01) || (HW_REV == REAL_REV02))

#if defined(CONFIG_MACH_SAMSUNG_P4)
#define P3_GPIO_CP_ACT              TEGRA_GPIO_PS5
#define P3_GPIO_CP_RST              TEGRA_GPIO_PX1 
#define P3_GPIO_CP_ON               TEGRA_GPIO_PO3
#define P3_GPIO_AP_ACT              TEGRA_GPIO_PV1
#define P3_GPIO_SIM_DETECT          TEGRA_GPIO_PC7
#define P3_GPIO_CP_REQ              TEGRA_GPIO_PQ3
#define P3_GPIO_HSIC_EN             TEGRA_GPIO_PV0
#define P3_GPIO_HSIC_ACTIVE         TEGRA_GPIO_PQ5
#define P3_GPIO_SLAVE_WAKEUP        TEGRA_GPIO_PR4
#define P3_GPIO_HSIC_SUS_REQ        TEGRA_GPIO_PQ0
#define P3_GPIO_HOST_WAKEUP         TEGRA_GPIO_PQ6
#elif defined(CONFIG_MACH_SAMSUNG_P5)
#define P3_GPIO_CP_ACT              TEGRA_GPIO_PO5
#define P3_GPIO_CP_RST              TEGRA_GPIO_PX1 
#define P3_GPIO_CP_ON               TEGRA_GPIO_PO3
#define P3_GPIO_AP_ACT              TEGRA_GPIO_PV1
#define P3_GPIO_SIM_DETECT          TEGRA_GPIO_PS0
#define P3_GPIO_CP_REQ              TEGRA_GPIO_PQ3
#define P3_GPIO_HSIC_EN             TEGRA_GPIO_PV0
#define P3_GPIO_HSIC_ACTIVE         TEGRA_GPIO_PQ5
#define P3_GPIO_SLAVE_WAKEUP        TEGRA_GPIO_PR4
#define P3_GPIO_HSIC_SUS_REQ        TEGRA_GPIO_PQ0
#define P3_GPIO_HOST_WAKEUP         TEGRA_GPIO_PW2
//#define P3_GPIO_HSIC_SUS_REQ        TEGRA_GPIO_PQ6		//wake source change rev0.2
//#define P3_GPIO_HOST_WAKEUP         TEGRA_GPIO_PQ0
#endif

#else
#define P3_GPIO_CP_ACT              TEGRA_GPIO_PS5
#define P3_GPIO_CP_RST              TEGRA_GPIO_PX1 
#define P3_GPIO_CP_ON               TEGRA_GPIO_PO3
#define P3_GPIO_AP_ACT              TEGRA_GPIO_PV1
#define P3_GPIO_SIM_DETECT          TEGRA_GPIO_PC7
#define P3_GPIO_CP_REQ              TEGRA_GPIO_PQ3
#define P3_GPIO_HSIC_EN             TEGRA_GPIO_PV0
#define P3_GPIO_HSIC_ACTIVE         TEGRA_GPIO_PQ5
#define P3_GPIO_HSIC_SUS_REQ        TEGRA_GPIO_PQ0
#define P3_GPIO_SLAVE_WAKEUP        TEGRA_GPIO_PR4
#define P3_GPIO_HOST_WAKEUP         TEGRA_GPIO_PQ6

#endif


static struct resource smd_res[] = {
    [0] = {
        .name = "cp_act",
        .start = P3_GPIO_CP_ACT,
        .end = P3_GPIO_CP_ACT,
        .flags = IORESOURCE_IO,
    },
    [1] = {
        .name = "cp_rst",
        .start = P3_GPIO_CP_RST,
        .end = P3_GPIO_CP_RST,
        .flags = IORESOURCE_IO,
    },    
    [2] = {
        .name = "cp_on",
        .start = P3_GPIO_CP_ON,
        .end = P3_GPIO_CP_ON,
        .flags = IORESOURCE_IO,
    },        
    [3] = {
        .name = "ap_act",
        .start = P3_GPIO_AP_ACT,
        .end = P3_GPIO_AP_ACT,
        .flags = IORESOURCE_IO,
    },
    [4] = {
        .name = "sim_det",
        .start = P3_GPIO_SIM_DETECT,
        .end = P3_GPIO_SIM_DETECT,
        .flags = IORESOURCE_IO,
    },
    [5] = {
        .name = "hsic_act",
        .start = P3_GPIO_HSIC_ACTIVE,
        .end = P3_GPIO_HSIC_ACTIVE,
        .flags = IORESOURCE_IO,
    },
    [6] = {
        .name = "hsic_sus",
        .start = P3_GPIO_HSIC_SUS_REQ,
        .end = P3_GPIO_HSIC_SUS_REQ,
        .flags = IORESOURCE_IO,
    },
    [7] = {
        .name = "hsic_en",
        .start = P3_GPIO_HSIC_EN,
        .end = P3_GPIO_HSIC_EN,
        .flags = IORESOURCE_IO,
    },
    [8] = {
        .name = "cp_req",
        .start = P3_GPIO_CP_REQ,
        .end = P3_GPIO_CP_REQ,
        .flags = IORESOURCE_IO,
    },
#if (HW_REV >= REAL_REV00)
    [9] = {
        .name = "slv_wkp",
        .start = P3_GPIO_SLAVE_WAKEUP,
        .end = P3_GPIO_SLAVE_WAKEUP,
        .flags = IORESOURCE_IO,
    },
    [10] = {
        .name = "hst_wkp",
        .start = P3_GPIO_HOST_WAKEUP,
        .end = P3_GPIO_HOST_WAKEUP,
        .flags = IORESOURCE_IO,
    },
#endif        
 };

static struct platform_device smd = {
    .name = "smd-ctl",
    .id = -1,
    .num_resources = ARRAY_SIZE(smd_res),
    .resource = smd_res,
};

int check_modem_alive(void)
{
	return gpio_get_value(P3_GPIO_CP_ACT);
}

int __init register_smd_resource (void)
{
    int i;
    for(i=0; i < ARRAY_SIZE(smd_res); i++) {
        tegra_gpio_enable(smd_res[i].start);
    }
    return platform_device_register(&smd);
}

//device_initcall(register_smd_resource);
