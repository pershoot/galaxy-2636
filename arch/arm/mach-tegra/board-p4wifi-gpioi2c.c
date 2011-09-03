
/* linux/arch/arm/mach-tegra/board-p4wifi-gpioi2c.c
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

#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/nct1008.h>
#include <linux/akm8975.h>
#include <linux/sii9234.h>
#include <linux/mfd/wm8994/wm8994_pdata.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/power/max17042_battery.h>
#include <mach/gpio.h>
#include <mach/gpio-sec.h>
#include <mach/pinmux.h>
#include <mach/iomap.h>
#include <mach/io.h>
#include <asm/io.h>
#ifdef CONFIG_VIBTONZ
#include "clock.h"
#include <linux/isa1200_vibrator.h>
#endif

/* light sensor */
static struct i2c_gpio_platform_data tegra_gpio_i2c5_pdata = {
	.sda_pin = GPIO_LIGHT_I2C_SDA,
	.scl_pin = GPIO_LIGHT_I2C_SCL,
	.udelay = 1, /* 200 kHz */
	.timeout = 0, /* jiffies */
};

static struct platform_device tegra_gpio_i2c5_device = {
	.name = "i2c-gpio",
	.id = 5,
	.dev = {
		.platform_data = &tegra_gpio_i2c5_pdata,
	}
};

/* fuel guage */
static struct i2c_gpio_platform_data tegra_gpio_i2c6_pdata = {
	.sda_pin = GPIO_FUEL_I2C_SDA,
	.scl_pin = GPIO_FUEL_I2C_SCL,
	.udelay = 1, /* 200 kHz */
	.timeout = 0, /* jiffies */
};

static struct platform_device tegra_gpio_i2c6_device = {
	.name = "i2c-gpio",
	.id = 6,
	.dev = {
		.platform_data = &tegra_gpio_i2c6_pdata,
	}
};

static struct i2c_gpio_platform_data tegra_gpio_i2c7_pdata = {
	.sda_pin = TEGRA_GPIO_PW2,  /*CAM_PMIC_I2C_SDA*/
	.scl_pin = TEGRA_GPIO_PX3,  /*CAM_PMIC_I2C_SCL*/
	.udelay = 1, /* 200 kHz */
	.timeout = 0, /* jiffies */
};

static struct platform_device tegra_gpio_i2c7_device = {
	.name = "i2c-gpio",
	.id = 7,
	.dev = {
		.platform_data = &tegra_gpio_i2c7_pdata,
	}
};

/* audio codec */
static struct i2c_gpio_platform_data tegra_gpio_i2c8_pdata = {
	.sda_pin = GPIO_CODEC_I2C_SDA,
	.scl_pin = GPIO_CODEC_I2C_SCL,
	.udelay = 1, /* 200 kHz */
	.timeout = 0, /* jiffies */
};

static struct platform_device tegra_gpio_i2c8_device = {
	.name = "i2c-gpio",
	.id = 8,
	.dev = {
		.platform_data = &tegra_gpio_i2c8_pdata,
	}
};

/* thermal monitor */
static struct i2c_gpio_platform_data tegra_gpio_i2c9_pdata = {
	.sda_pin = GPIO_THERMAL_I2C_SDA,
	.scl_pin = GPIO_THERMAL_I2C_SCL,
	.udelay = 1, /* 200 kHz */
	.timeout = 0, /* jiffies */
};

static struct platform_device tegra_gpio_i2c9_device = {
	.name = "i2c-gpio",
	.id = 9,
	.dev = {
		.platform_data = &tegra_gpio_i2c9_pdata,
	}
};

/* image converter */
static struct i2c_gpio_platform_data tegra_gpio_i2c10_pdata = {
	.sda_pin = GPIO_IMAGE_I2C_SDA,
	.scl_pin = GPIO_IMAGE_I2C_SCL,
	.udelay = 1, /* 200 kHz */
	.timeout = 0, /* jiffies */
};

static struct platform_device tegra_gpio_i2c10_device = {
	.name = "i2c-gpio",
	.id = 10,
	.dev = {
		.platform_data = &tegra_gpio_i2c10_pdata,
	}
};

/* AD converter */
static struct i2c_gpio_platform_data tegra_gpio_i2c11_pdata = {
	.sda_pin = GPIO_ADC_I2C_SDA,
	.scl_pin = GPIO_ADC_I2C_SCL,
	.udelay = 3, /* 200 kHz */
	.timeout = 0, /* jiffies */
};

static struct platform_device tegra_gpio_i2c11_device = {
	.name = "i2c-gpio",
	.id = 11,
	.dev = {
		.platform_data = &tegra_gpio_i2c11_pdata,
	}
};

/* magnetic sensor */
static struct i2c_gpio_platform_data tegra_gpio_i2c12_pdata = {
	.sda_pin = GPIO_MAG_I2C_SDA,
	.scl_pin = GPIO_MAG_I2C_SCL,
	.udelay = 1, /* 200 kHz */
	.timeout = 0,
};

static struct platform_device tegra_gpio_i2c12_device = {
	.name = "i2c-gpio",
	.id = 12,
	.dev = {
		.platform_data = &tegra_gpio_i2c12_pdata,
	}
};

/* HDMI */
static struct i2c_gpio_platform_data tegra_gpio_i2c13_pdata = {
	.sda_pin = GPIO_HDMI_I2C_SDA,
	.scl_pin = GPIO_HDMI_I2C_SCL,
	.udelay = 3,
	.timeout = 0,
};

static struct platform_device tegra_gpio_i2c13_device = {
	.name = "i2c-gpio",
	.id = 13,
	.dev = {
		.platform_data = &tegra_gpio_i2c13_pdata,
	}
};

/* HDMI logic IF */
static struct i2c_gpio_platform_data tegra_gpio_i2c14_pdata = {
	.sda_pin = GPIO_HDMI_LOGIC_I2C_SDA,
	.scl_pin = GPIO_HDMI_LOGIC_I2C_SCL,
	.udelay = 3,
	.timeout = 0,
};

static struct platform_device tegra_gpio_i2c14_device = {
	.name = "i2c-gpio",
	.id = 14,
	.dev = {
		.platform_data = &tegra_gpio_i2c14_pdata,
	}
};

#ifdef CONFIG_VIBTONZ
static struct i2c_gpio_platform_data tegra_gpio_i2c15_pdata = {
	.sda_pin = GPIO_MOTOR_I2C_SDA,
	.scl_pin = GPIO_MOTOR_I2C_SCL,
	.udelay = 1, /* 200 kHz */
	.timeout = 0,
};

static struct platform_device tegra_gpio_i2c15_device = {
	.name = "i2c-gpio",
	.id = 15,
	.dev = {
		.platform_data = &tegra_gpio_i2c15_pdata,
	}
};
#endif

static struct max17042_platform_data max17042_pdata = {
	.sdi_capacity = 0x340A,
	.sdi_vfcapacity = 0x478A,
	.atl_capacity = 0x349A,
	.atl_vfcapacity = 0x4630,
	.fuel_alert_line = GPIO_FUEL_ALRT,
};

static struct i2c_board_info sec_gpio_i2c6_info[] = {
	{
		I2C_BOARD_INFO("fuelgauge", 0x36),
		.platform_data = &max17042_pdata,
	},
};

static struct i2c_board_info sec_gpio_i2c7_info[] = {
	{
		I2C_BOARD_INFO("imx072_pmic", 0xF4 >> 1),
	},
};

static void wm8994_set_mic_bias(bool on)
{
	pr_info("Board P3 : Enterring wm8994_set_mic_bias\n");
	gpio_set_value(GPIO_MICBIAS_EN, on);
}


static void *das_base = IO_ADDRESS(TEGRA_APB_MISC_BASE);

static inline unsigned long das_readl(unsigned long offset)
{
        return readl(das_base + offset);
}

static inline void das_writel(unsigned long value, unsigned long offset)
{
        writel(value, das_base + offset);
}

static void tegra_set_dap_connection(bool on)
{
	int reg_val;

	pr_info("Board P4 : %s : %d\n", __func__, on);
	if(on) {
#if 0
		das_writel(DAP_CTRL_SEL_DAP3, APB_MISC_DAS_DAP_CTRL_SEL_1);
		das_writel(DAP_CTRL_SEL_DAP3, APB_MISC_DAS_DAP_CTRL_SEL_3);
		das_writel((DAP_MS_SEL_MASTER /*| DAP_CTRL_SEL_DAP2*/ | DAP_CTRL_SEL_DAP4),
			APB_MISC_DAS_DAP_CTRL_SEL_2);
#else

		//DAP1
		reg_val = das_readl(APB_MISC_DAS_DAP_CTRL_SEL_0);

		reg_val &= ~(DAP_MS_SEL_DEFAULT_MASK << DAP_MS_SEL_SHIFT);
		reg_val |= (1 << DAP_MS_SEL_SHIFT); //DAP1 master

		reg_val &= ~(DAP_CTRL_SEL_DEFAULT_MASK << DAP_CTRL_SEL_SHIFT);
		reg_val |= (DAP_CTRL_SEL_DAC1 << DAP_CTRL_SEL_SHIFT); //DAP1<-DAC1

		das_writel(reg_val, APB_MISC_DAS_DAP_CTRL_SEL_0);

		//DAP2
		reg_val = das_readl(APB_MISC_DAS_DAP_CTRL_SEL_1);

		reg_val &= ~(DAP_MS_SEL_DEFAULT_MASK << DAP_MS_SEL_SHIFT);
		reg_val |= (1 << DAP_MS_SEL_SHIFT);//DAP2 master

		reg_val &= ~(DAP_CTRL_SEL_DEFAULT_MASK << DAP_CTRL_SEL_SHIFT);
		reg_val |= (DAP_CTRL_SEL_DAP4 << DAP_CTRL_SEL_SHIFT);//DAP2<-DAP4

		das_writel(reg_val, APB_MISC_DAS_DAP_CTRL_SEL_1);

		//DAP3
		reg_val = das_readl(APB_MISC_DAS_DAP_CTRL_SEL_2);

		reg_val &= ~(DAP_MS_SEL_DEFAULT_MASK << DAP_MS_SEL_SHIFT);
		reg_val |= (1 << DAP_MS_SEL_SHIFT);//DAP3 master

		reg_val &= ~(DAP_CTRL_SEL_DEFAULT_MASK << DAP_CTRL_SEL_SHIFT);
		reg_val |= (DAP_CTRL_SEL_DAP4 << DAP_CTRL_SEL_SHIFT);//DAP3<-DAP4

		das_writel(reg_val, APB_MISC_DAS_DAP_CTRL_SEL_2);

		//DAP4
		reg_val = das_readl(APB_MISC_DAS_DAP_CTRL_SEL_3);

		reg_val &= ~(DAP_MS_SEL_DEFAULT_MASK << DAP_MS_SEL_SHIFT);
		reg_val |= (0 << DAP_MS_SEL_SHIFT);//DAP4 slave

		reg_val &= ~(DAP_CTRL_SEL_DEFAULT_MASK << DAP_CTRL_SEL_SHIFT);
		reg_val |= (DAP_CTRL_SEL_DAP2 << DAP_CTRL_SEL_SHIFT);//DAP4<-DAP2

		das_writel(reg_val, APB_MISC_DAS_DAP_CTRL_SEL_3);

		//DAC1
		reg_val = das_readl(APB_MISC_DAS_DAC_INPUT_DATA_CLK_SEL_0);

		reg_val &= ~(DAC_SDATA2_SEL_DEFAULT_MASK << DAC_SDATA2_SEL_SHIFT);
		reg_val |= ((DAP_CTRL_SEL_DAP1 - DAP_CTRL_SEL_DAP1) << DAC_SDATA2_SEL_SHIFT);//DAC1 <- DAP1

		reg_val &= ~(DAC_SDATA1_SEL_DEFAULT_MASK << DAC_SDATA1_SEL_SHIFT);
		reg_val |= ((DAP_CTRL_SEL_DAP1 - DAP_CTRL_SEL_DAP1) << DAC_SDATA1_SEL_SHIFT);//DAC1 <- DAP1

		reg_val &= ~(DAC_CLK_SEL_DEFAULT_MASK << DAC_CLK_SEL_SHIFT);
		reg_val |= (DAP_CTRL_SEL_DAP1 << DAC_CLK_SEL_SHIFT);// DAC1 <- DAP1

		das_writel(reg_val, APB_MISC_DAS_DAC_INPUT_DATA_CLK_SEL_0);
#endif
	} else {
		das_writel(DAP_CTRL_SEL_DAP3, APB_MISC_DAS_DAP_CTRL_SEL_1);
		das_writel((DAP_MS_SEL_MASTER | DAP_CTRL_SEL_DAP2),
			APB_MISC_DAS_DAP_CTRL_SEL_2);
	}
}


static struct wm8994_platform_data wm8994_pdata = {
	.ldo = GPIO_CODEC_LDO_EN,
	.set_mic_bias = wm8994_set_mic_bias,
	.set_dap_connection = tegra_set_dap_connection,
};

static struct i2c_board_info sec_gpio_i2c8_info[] = {
	{
		I2C_BOARD_INFO("wm8994", 0x36 >> 1),
		.platform_data = &wm8994_pdata,
	},
};

static void nct1008_init(void)
{
	tegra_gpio_enable(GPIO_nTHRM_IRQ);
	gpio_request(GPIO_nTHRM_IRQ, "temp_alert");
	gpio_direction_input(GPIO_nTHRM_IRQ);
}

extern void tegra_throttling_enable(bool enable);
static struct nct1008_platform_data p3_nct1008_pdata = {
	.supported_hwrev = true,
	.ext_range = false,
	.conv_rate = 0x08,
	.offset = 0,
	.hysteresis = 0,
	.shutdown_ext_limit = 115,
	.shutdown_local_limit = 120,
	.throttling_ext_limit = 90,
	.alarm_fn = tegra_throttling_enable,
};

static struct i2c_board_info sec_gpio_i2c9_info[] = {
	{
		I2C_BOARD_INFO("nct1008", 0x4C),
		.platform_data = &p3_nct1008_pdata,
		.irq = TEGRA_GPIO_TO_IRQ(GPIO_nTHRM_IRQ),
	},
};

static struct i2c_board_info sec_gpio_i2c10_info[] = {
	{
		I2C_BOARD_INFO("image_convertor", 0x38),
	},
};

static struct i2c_board_info sec_gpio_i2c11_info[] = {
	{
		I2C_BOARD_INFO("max1237", 0x34),
	},
	{
		I2C_BOARD_INFO("stmpe811", 0x82>>1),
	},
};

static void sii9234_init(void)
{
	int ret = gpio_request(GPIO_HDMI_EN1, "hdmi_en1");
	if (ret) {
		pr_err("%s: gpio_request() for HDMI_EN1 failed\n", __func__);
		return;
	}
	gpio_direction_output(GPIO_HDMI_EN1, 0);
	if (ret) {
		pr_err("%s: gpio_direction_output() for HDMI_EN1 failed\n",
			__func__);
		return;
	}
	tegra_gpio_enable(GPIO_HDMI_EN1);

	ret = gpio_request(GPIO_MHL_RST, "mhl_rst");
	if (ret) {
		pr_err("%s: gpio_request() for MHL_RST failed\n", __func__);
		return;
	}
	ret = gpio_direction_output(GPIO_MHL_RST, 1);
	if (ret) {
		pr_err("%s: gpio_direction_output() for MHL_RST failed\n",
			__func__);
		return;
	}
	tegra_gpio_enable(GPIO_MHL_RST);
}

static void sii9234_hw_reset(void)
{
	struct regulator *reg;

	gpio_set_value(GPIO_MHL_RST, 1);
	reg = regulator_get(NULL, "vdd_ldo7");
	if (IS_ERR_OR_NULL(reg)) {
		pr_err("%s: failed to get vdd_ldo7 regulator\n", __func__);
		return;
	}
	regulator_enable(reg);
	regulator_put(reg);

	reg = regulator_get(NULL, "vdd_ldo8");
	if (IS_ERR_OR_NULL(reg)) {
		pr_err("%s: failed to get vdd_ldo8 regulator\n", __func__);
		return;
	}
	regulator_enable(reg);
	regulator_put(reg);

	usleep_range(10000, 20000);
	gpio_set_value(GPIO_HDMI_EN1, 1);

	usleep_range(5000, 10000);
	gpio_set_value(GPIO_MHL_RST, 0);

	usleep_range(10000, 20000);
	gpio_set_value(GPIO_MHL_RST, 1);
	msleep(30);
}

static void sii9234_hw_off(void)
{
	struct regulator *reg;

	gpio_set_value(GPIO_HDMI_EN1, 0);

	reg = regulator_get(NULL, "vdd_ldo7");
	if (IS_ERR_OR_NULL(reg)) {
		pr_err("%s: failed to get vdd_ldo7 regulator\n", __func__);
		return;
	}
	regulator_disable(reg);
	regulator_put(reg);

	reg = regulator_get(NULL, "vdd_ldo8");
	if (IS_ERR_OR_NULL(reg)) {
		pr_err("%s: failed to get vdd_ldo8 regulator\n", __func__);
		return;
	}
	regulator_disable(reg);
	regulator_put(reg);

	gpio_set_value(GPIO_MHL_RST, 0);
}

struct sii9234_platform_data p3_sii9234_pdata = {
	.hw_reset = sii9234_hw_reset,
	.hw_off = sii9234_hw_off
};

static struct i2c_board_info sec_gpio_i2c14_info[] = {
	{
		I2C_BOARD_INFO("SII9234", 0x72>>1),
		.platform_data = &p3_sii9234_pdata,
	},
	{
		I2C_BOARD_INFO("SII9234A", 0x7A>>1),
	},
	{
		I2C_BOARD_INFO("SII9234B", 0x92>>1),
	},
	{
		I2C_BOARD_INFO("SII9234C", 0xC8>>1),
	},
};

#ifdef CONFIG_VIBTONZ
static struct clk *isa1200_vibrator_get_clk(void)
{
	return tegra_get_clock_by_name("clk_dev2");
}
static struct isa1200_vibrator_platform_data isa1200_vibrator_pdata = {
	.gpio_en = GPIO_MOTOR_EN2,
	.max_timeout = 10000,
	.ctrl0 = CTL0_DIVIDER256 | CTL0_PWM_GEN,
	.ctrl1 = CTL1_DEFAULT |CTL1_EXT_CLOCK,
	.ctrl2 = 0,
	.ctrl4 = 0,
	. pll = 0x23,
	.duty = 0x71,
	.period = 0x74,
	.get_clk = isa1200_vibrator_get_clk,
};
static struct i2c_board_info sec_gpio_i2c15_info[] = {
	{
		I2C_BOARD_INFO("isa1200_vibrator",  0x48),
		.platform_data = &isa1200_vibrator_pdata,
	},
};
#endif

int __init p3_gpio_i2c_init(void)
{
	platform_device_register(&tegra_gpio_i2c5_device);
	platform_device_register(&tegra_gpio_i2c6_device);
	if (system_rev < 0x2)
		platform_device_register(&tegra_gpio_i2c7_device);
	platform_device_register(&tegra_gpio_i2c8_device);
	platform_device_register(&tegra_gpio_i2c9_device);
	platform_device_register(&tegra_gpio_i2c10_device);
	platform_device_register(&tegra_gpio_i2c11_device);
	if (system_rev >= 0x2) {  /* greater than Rev0.2 or same */
		platform_device_register(&tegra_gpio_i2c12_device);
		platform_device_register(&tegra_gpio_i2c13_device);
		platform_device_register(&tegra_gpio_i2c14_device);
		platform_device_register(&tegra_gpio_i2c15_device);
	}
	i2c_register_board_info(6, sec_gpio_i2c6_info,
				ARRAY_SIZE(sec_gpio_i2c6_info));
	if (system_rev < 0x2)
		i2c_register_board_info(7, sec_gpio_i2c7_info,
					ARRAY_SIZE(sec_gpio_i2c7_info));
	i2c_register_board_info(8, sec_gpio_i2c8_info,
				ARRAY_SIZE(sec_gpio_i2c8_info));
	nct1008_init();
	i2c_register_board_info(9, sec_gpio_i2c9_info,
				ARRAY_SIZE(sec_gpio_i2c9_info));
	i2c_register_board_info(10, sec_gpio_i2c10_info,
				ARRAY_SIZE(sec_gpio_i2c10_info));
	i2c_register_board_info(11, sec_gpio_i2c11_info,
				ARRAY_SIZE(sec_gpio_i2c11_info));
	if (system_rev >= 0x2) {
		sii9234_init();
		i2c_register_board_info(14, sec_gpio_i2c14_info,
					ARRAY_SIZE(sec_gpio_i2c14_info));
		i2c_register_board_info(15, sec_gpio_i2c15_info,
					ARRAY_SIZE(sec_gpio_i2c15_info));
	}
	return 0;
}
