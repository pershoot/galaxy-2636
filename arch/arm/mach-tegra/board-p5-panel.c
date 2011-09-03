/*
 * arch/arm/mach-tegra/board-p3-panel.c
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

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/resource.h>
#include <asm/mach-types.h>
#include <linux/platform_device.h>
#include <linux/earlysuspend.h>
#if 1 // Governor Test
#include <linux/kernel.h>
#endif
#include <linux/pwm_backlight.h>
#include <mach/nvhost.h>
#include <mach/nvmap.h>
#include <mach/irqs.h>
#include <mach/iomap.h>
#include <mach/dc.h>
#include <mach/fb.h>
#include <mach/gpio-sec.h>

#include "devices.h"
#include "gpio-names.h"
#include "board.h"
#include <linux/mfd/tps6586x.h>


#define HDMI_HPD_GPIO	GPIO_HDMI_HPD
//#define HDMI_ENB_GPIO	TEGRA_GPIO_PH2

static struct regulator *p3_hdmi_reg = NULL;
static struct regulator *p3_hdmi_pll = NULL;

static struct platform_device p3_backlight_device = {
	.name	= "cmc623_pwm_bl",
	.id	= -1,
};

static int p3_hdmi_enable(void)
{
#if 0
	gpio_set_value(HDMI_ENB_GPIO, 1);
	if (!p3_hdmi_reg) {
		p3_hdmi_reg = regulator_get(NULL, "avdd_hdmi"); /* LD07 */
			if (IS_ERR_OR_NULL(p3_hdmi_reg)) {
				pr_err("hdmi: couldn't get regulator avdd_hdmi\n");
				p3_hdmi_reg = NULL;
				gpio_set_value(HDMI_ENB_GPIO, 0);
				return PTR_ERR(p3_hdmi_reg);
			}
	}
	regulator_enable(p3_hdmi_reg);

	if (!p3_hdmi_pll) {
		p3_hdmi_pll = regulator_get(NULL, "avdd_hdmi_pll"); /* LD08 */
		if (IS_ERR_OR_NULL(p3_hdmi_pll)) {
			pr_err("hdmi: couldn't get regulator avdd_hdmi_pll\n");
			p3_hdmi_pll = NULL;
			regulator_disable(p3_hdmi_reg);
			p3_hdmi_reg = NULL;
			gpio_set_value(HDMI_ENB_GPIO, 0);
			return PTR_ERR(p3_hdmi_pll);
		}
	}
	regulator_enable(p3_hdmi_pll);
#endif
	return 0;
}

static int p3_hdmi_disable(void)
{
#if 0
	gpio_set_value(HDMI_ENB_GPIO, 0);

	if (p3_hdmi_reg)
		regulator_disable(p3_hdmi_reg);

	if (p3_hdmi_pll)
		regulator_disable(p3_hdmi_pll);
#endif
	return 0;
}

static struct resource p3_disp1_resources[] = {
	{
		.name	= "irq",
		.start	= INT_DISPLAY_GENERAL,
		.end	= INT_DISPLAY_GENERAL,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "regs",
		.start	= TEGRA_DISPLAY_BASE,
		.end	= TEGRA_DISPLAY_BASE + TEGRA_DISPLAY_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "fbmem",
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource p3_disp2_resources[] = {
	{
		.name	= "irq",
		.start	= INT_DISPLAY_B_GENERAL,
		.end	= INT_DISPLAY_B_GENERAL,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "regs",
		.start	= TEGRA_DISPLAY2_BASE,
		.end	= TEGRA_DISPLAY2_BASE + TEGRA_DISPLAY2_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "fbmem",
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "hdmi_regs",
		.start	= TEGRA_HDMI_BASE,
		.end	= TEGRA_HDMI_BASE + TEGRA_HDMI_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct tegra_dc_mode p3_panel_modes[] = {
	{	// SAMSULG PLS LCD panel
		.pclk = 74666667,
		.flags = TEGRA_DC_MODE_FLAG_NEG_V_SYNC|TEGRA_DC_MODE_FLAG_NEG_H_SYNC,
		.h_ref_to_sync = 1,
		.v_ref_to_sync = 1,
		.h_sync_width = 16, //32,
		.v_sync_width = 3, //10,
		.h_back_porch = 90, //80,
		.v_back_porch = 12, //24,
		.h_active = 800,
		.v_active = 1280,
		.h_front_porch = 48, //48,
		.v_front_porch = 4, //3,
	},
};

static struct tegra_fb_data p3_fb_data = {
	.win		= 0,
	.xres		= 800,
	.yres		= 1280,
	.bits_per_pixel	= 32, //16,
};

static struct tegra_fb_data p3_hdmi_fb_data = {
	.win		= 0,
	.xres		= 1366,
	.yres		= 768,
	.bits_per_pixel	= 16,
};

#ifdef CONFIG_KERNEL_DEBUG_SEC
struct struct_frame_buf_mark {
	u32 special_mark_1;
	u32 special_mark_2;
	u32 special_mark_3;
	u32 special_mark_4;
	void *p_fb;/*it must be physical address*/
	u32 resX;
	u32 resY;
	u32 bpp; /*color depth : 16 or 24*/
	u32 frames; /*frame buffer count : 2*/
};
static struct struct_frame_buf_mark frame_buf_mark = {
	.special_mark_1 = (('*' << 24) | ('^' << 16) | ('^' << 8) | ('*' << 0)),
	.special_mark_2 = (('I' << 24) | ('n' << 16) | ('f' << 8) | ('o' << 0)),
	.special_mark_3 = (('H' << 24) | ('e' << 16) | ('r' << 8) | ('e' << 0)),
	.special_mark_4 = (('f' << 24) | ('b' << 16) | ('u' << 8) | ('f' << 0)),
	.p_fb = 0,
	.resX = 1280, /* it has dependency on h/w */
	.resY = 800, /* it has dependency on h/w */
	.bpp = 32, /* it has dependency on h/w */
	.frames = 2
};
#endif

static struct tegra_dc_out p3_disp1_out = {
	.type		= TEGRA_DC_OUT_RGB,

	.align		= TEGRA_DC_ALIGN_MSB,
	.order		= TEGRA_DC_ORDER_RED_BLUE,

	.modes		= p3_panel_modes,
	.n_modes 	= ARRAY_SIZE(p3_panel_modes),

	/* P5 */
	.width		= 120,	/* in mm */
	.height		= 192,	/* in mm */
#if 0
	.enable		= p3_panel_enable,
	.disable	= p3_panel_disable,
#endif
};

static struct tegra_dc_out p3_disp2_out = {
	.type		= TEGRA_DC_OUT_HDMI,
	.flags		= TEGRA_DC_OUT_HOTPLUG_HIGH,

	.dcc_bus	= 1,

	.max_pixclock	= KHZ2PICOS(74250),

	.align		= TEGRA_DC_ALIGN_MSB,
	.order		= TEGRA_DC_ORDER_RED_BLUE,

	.hotplug_gpio	= HDMI_HPD_GPIO,
	.enable		= p3_hdmi_enable,
	.disable	= p3_hdmi_disable,
};

static struct tegra_dc_platform_data p3_disp1_pdata = {
	.flags		= TEGRA_DC_FLAG_ENABLED,
	.default_out	= &p3_disp1_out,
	.fb		= &p3_fb_data,
};

static struct tegra_dc_platform_data p3_disp2_pdata = {
	.flags		= 0,
	.default_out	= &p3_disp2_out,
	.fb		= &p3_hdmi_fb_data,
};

static struct nvhost_device p3_disp1_device = {
	.name		= "tegradc",
	.id		= 0,
	.resource	= p3_disp1_resources,
	.num_resources	= ARRAY_SIZE(p3_disp1_resources),
	.dev = {
		.platform_data = &p3_disp1_pdata,
	},
};

static struct nvhost_device p3_disp2_device = {
	.name		= "tegradc",
	.id		= 1,
	.resource	= p3_disp2_resources,
	.num_resources	= ARRAY_SIZE(p3_disp2_resources),
	.dev = {
		.platform_data = &p3_disp2_pdata,
	},
};

static struct nvmap_platform_carveout p3_carveouts[] = {
	[0] = {
		.name		= "iram",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_IRAM,
		.base		= TEGRA_IRAM_BASE,
		.size		= TEGRA_IRAM_SIZE,
		.buddy_size	= 0, /* no buddy allocation for IRAM */
	},
	[1] = {
		.name		= "generic-0",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_GENERIC,
		.buddy_size	= SZ_32K,
	},
};

static struct nvmap_platform_data p3_nvmap_data = {
	.carveouts	= p3_carveouts,
	.nr_carveouts	= ARRAY_SIZE(p3_carveouts),
};

static struct platform_device p3_nvmap_device = {
	.name	= "tegra-nvmap",
	.id	= -1,
	.dev	= {
		.platform_data = &p3_nvmap_data,
	},
};

static struct platform_device p3_device_cmc623 = {
		.name			= "sec_cmc623",
		.id			= -1,
};

/*
static const struct i2c_board_info p3_i2c0_board_info[] = { // NvOdmIoModule_I2c, 0	// GEN1_I2C ??
	{
		I2C_BOARD_INFO("sec_tune_cmc623_i2c", 0x38),
	},
};
*/

static struct platform_device *p3_gfx_devices[] __initdata = {
	&p3_nvmap_device,
	&tegra_grhost_device,
	&tegra_pwfm2_device,
	&p3_backlight_device,
	&p3_device_cmc623,
};

#ifdef CONFIG_HAS_EARLYSUSPEND

#if 1 // Governor Test
static char *cpufreq_gov_conservative = "conservative";
static char *cpufreq_gov_interactive = "interactive";
static char *cpufreq_sysfs_place_holder="/sys/devices/system/cpu/cpu%i/cpufreq/scaling_governor";

static void p3_panel_set_cpufreq_governor(char *governor)
{
	struct file *scaling_gov = NULL;
	char    buf[128];
	int i;
	loff_t offset = 0;

	if (governor == NULL)
		return;

	for_each_cpu(i, cpu_present_mask) {
		sprintf(buf, cpufreq_sysfs_place_holder,i);
		scaling_gov = filp_open(buf, O_RDWR, 0);
		if (scaling_gov != NULL) {
			if (scaling_gov->f_op != NULL &&
				scaling_gov->f_op->write != NULL)
				scaling_gov->f_op->write(scaling_gov,
						governor,
						strlen(governor),
						&offset);
			else pr_err("f_op might be null\n");

			filp_close(scaling_gov, NULL);
		} else {
			pr_err("%s. Can't open %s\n", __func__, buf);
		}
	}
}
#endif

/* put early_suspend/late_resume handlers here for the display in order
 * to keep the code out of the display driver, keeping it closer to upstream
 */
struct early_suspend p3_panel_early_suspender;

static void p3_panel_early_suspend(struct early_suspend *h)
{
	if (num_registered_fb > 0)
		fb_blank(registered_fb[0], FB_BLANK_POWERDOWN);
#if 1 // Governor Test
	p3_panel_set_cpufreq_governor(cpufreq_gov_conservative);
#endif
}

static void p3_panel_late_resume(struct early_suspend *h)
{
	if (num_registered_fb > 0)
		fb_blank(registered_fb[0], FB_BLANK_UNBLANK);
#if 1 // Governor Test
	p3_panel_set_cpufreq_governor(cpufreq_gov_interactive);
#endif
}
#endif

extern	void sii9234_init(void);

void p3_panel_gpio_init(void)
{
	if (system_rev >= 6) {
		tegra_gpio_enable(HDMI_HPD_GPIO);
		gpio_request(HDMI_HPD_GPIO, "hdmi_hpd");
		gpio_direction_input(HDMI_HPD_GPIO);
		gpio_free(HDMI_HPD_GPIO);
	} else {
		gpio_request(GPEX_GPIO_P1, "hdmi_hpd");
		gpio_direction_input(GPEX_GPIO_P1);
		gpio_free(GPEX_GPIO_P1);
	}

/* just for MHL i2c level shifter */
	gpio_request(GPIO_HDMI_EN1, "hdmi_en1");
	gpio_direction_output(GPIO_HDMI_EN1, 1);
	gpio_free(GPIO_HDMI_EN1);

	sii9234_init();
}

int __init p3_panel_init(void)
{
	int err;
	struct resource *res;

	//tegra_gpio_enable(HDMI_ENB_GPIO);
	//gpio_request(HDMI_ENB_GPIO, "hdmi_5v_en");
	//gpio_direction_output(HDMI_ENB_GPIO, 1);

	//p3_panel_gpio_init();

#ifdef CONFIG_HAS_EARLYSUSPEND
	p3_panel_early_suspender.suspend = p3_panel_early_suspend;
	p3_panel_early_suspender.resume = p3_panel_late_resume;
	p3_panel_early_suspender.level = EARLY_SUSPEND_LEVEL_DISABLE_FB;
	register_early_suspend(&p3_panel_early_suspender);
#endif

	p3_carveouts[1].base = tegra_carveout_start;
	p3_carveouts[1].size = tegra_carveout_size;

	err = platform_add_devices(p3_gfx_devices,
				   ARRAY_SIZE(p3_gfx_devices));

	res = nvhost_get_resource_byname(&p3_disp1_device,
					IORESOURCE_MEM, "fbmem");
	res->start = tegra_fb_start;
	res->end = tegra_fb_start + tegra_fb_size - 1;

	if (system_rev < 6) {
		p3_disp2_out.hotplug_gpio = GPEX_GPIO_P1;
		p3_disp2_out.dcc_bus = 1;
	} else if (system_rev < 8) {
		p3_disp2_out.dcc_bus = 13;
	}

	res = nvhost_get_resource_byname(&p3_disp2_device,
		IORESOURCE_MEM, "fbmem");
	res->start = tegra_fb2_start;
	res->end = tegra_fb2_start + tegra_fb2_size - 1;

	if (!err)
		err = nvhost_device_register(&p3_disp1_device);

	/* HDMI FB */
	if (system_rev >= 6) {	// temporary
		if (!err)
			err = nvhost_device_register(&p3_disp2_device);
	}

	//i2c_register_board_info(0, p3_i2c0_board_info,
	//ARRAY_SIZE(p3_i2c0_board_info));

#ifdef CONFIG_KERNEL_DEBUG_SEC
	frame_buf_mark.bpp = p3_fb_data.bits_per_pixel;
	/*it has dependency on h/w*/
	frame_buf_mark.p_fb = (void*)p3_disp1_resources[2].start;
	/*it has dependency on project*/
#endif

	return err;
}

