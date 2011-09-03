/*
 * arch/arm/mach-tegra/gpio.c
 *
 * Copyright (c) 2010 Google, Inc
 *
 * Author:
 *	Erik Gilling <konkers@google.com>
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

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

#include <linux/io.h>
#include <linux/gpio.h>

#include <mach/iomap.h>
#include <mach/suspend.h>

#define GPIO_BANK(x)		((x) >> 5)
#define GPIO_PORT(x)		(((x) >> 3) & 0x3)
#define GPIO_BIT(x)		((x) & 0x7)

#define GPIO_REG(x)		(IO_TO_VIRT(TEGRA_GPIO_BASE) +	\
				 GPIO_BANK(x) * 0x80 +		\
				 GPIO_PORT(x) * 4)

#define GPIO_CNF(x)		(GPIO_REG(x) + 0x00)
#define GPIO_OE(x)		(GPIO_REG(x) + 0x10)
#define GPIO_OUT(x)		(GPIO_REG(x) + 0X20)
#define GPIO_IN(x)		(GPIO_REG(x) + 0x30)
#define GPIO_INT_STA(x)		(GPIO_REG(x) + 0x40)
#define GPIO_INT_ENB(x)		(GPIO_REG(x) + 0x50)
#define GPIO_INT_LVL(x)		(GPIO_REG(x) + 0x60)
#define GPIO_INT_CLR(x)		(GPIO_REG(x) + 0x70)

#define GPIO_MSK_CNF(x)		(GPIO_REG(x) + 0x800)
#define GPIO_MSK_OE(x)		(GPIO_REG(x) + 0x810)
#define GPIO_MSK_OUT(x)		(GPIO_REG(x) + 0X820)
#define GPIO_MSK_INT_STA(x)	(GPIO_REG(x) + 0x840)
#define GPIO_MSK_INT_ENB(x)	(GPIO_REG(x) + 0x850)
#define GPIO_MSK_INT_LVL(x)	(GPIO_REG(x) + 0x860)

#define GPIO_INT_LVL_MASK		0x010101
#define GPIO_INT_LVL_EDGE_RISING	0x000101
#define GPIO_INT_LVL_EDGE_FALLING	0x000100
#define GPIO_INT_LVL_EDGE_BOTH		0x010100
#define GPIO_INT_LVL_LEVEL_HIGH		0x000001
#define GPIO_INT_LVL_LEVEL_LOW		0x000000

struct tegra_gpio_bank {
	int bank;
	int irq;
	spinlock_t lvl_lock[4];
#ifdef CONFIG_PM
	u32 cnf[4];
	u32 out[4];
	u32 oe[4];
	u32 int_enb[4];
	u32 int_lvl[4];
#endif
};


static struct tegra_gpio_bank tegra_gpio_banks[] = {
	{.bank = 0, .irq = INT_GPIO1},
	{.bank = 1, .irq = INT_GPIO2},
	{.bank = 2, .irq = INT_GPIO3},
	{.bank = 3, .irq = INT_GPIO4},
	{.bank = 4, .irq = INT_GPIO5},
	{.bank = 5, .irq = INT_GPIO6},
	{.bank = 6, .irq = INT_GPIO7},
};

static int tegra_gpio_compose(int bank, int port, int bit)
{
	return (bank << 5) | ((port & 0x3) << 3) | (bit & 0x7);
}

static void tegra_gpio_mask_write(u32 reg, int gpio, int value)
{
	u32 val;

	val = 0x100 << GPIO_BIT(gpio);
	if (value)
		val |= 1 << GPIO_BIT(gpio);
	__raw_writel(val, reg);
}

void tegra_gpio_enable(int gpio)
{
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	/* non-tegra gpio, eg. gpio expander */
	if (gpio >= TEGRA_NR_GPIOS)
		return;
#endif
	tegra_gpio_mask_write(GPIO_MSK_CNF(gpio), gpio, 1);
}

void tegra_gpio_disable(int gpio)
{
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	/* non-tegra gpio, eg. gpio expander */
	if (gpio >= TEGRA_NR_GPIOS)
		return;
#endif
	tegra_gpio_mask_write(GPIO_MSK_CNF(gpio), gpio, 0);
}

static void tegra_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	tegra_gpio_mask_write(GPIO_MSK_OUT(offset), offset, value);
}

static int tegra_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	return (__raw_readl(GPIO_IN(offset)) >> GPIO_BIT(offset)) & 0x1;
}

static int tegra_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	tegra_gpio_mask_write(GPIO_MSK_OE(offset), offset, 0);
	return 0;
}

static int tegra_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
					int value)
{
	tegra_gpio_set(chip, offset, value);
	tegra_gpio_mask_write(GPIO_MSK_OE(offset), offset, 1);
	return 0;
}



static struct gpio_chip tegra_gpio_chip = {
	.label			= "tegra-gpio",
	.direction_input	= tegra_gpio_direction_input,
	.get			= tegra_gpio_get,
	.direction_output	= tegra_gpio_direction_output,
	.set			= tegra_gpio_set,
	.base			= 0,
	.ngpio			= TEGRA_NR_GPIOS,
};

static void tegra_gpio_irq_ack(unsigned int irq)
{
	int gpio = irq - INT_GPIO_BASE;

	__raw_writel(1 << GPIO_BIT(gpio), GPIO_INT_CLR(gpio));
}

static void tegra_gpio_irq_mask(unsigned int irq)
{
	int gpio = irq - INT_GPIO_BASE;

	tegra_gpio_mask_write(GPIO_MSK_INT_ENB(gpio), gpio, 0);
}

static void tegra_gpio_irq_unmask(unsigned int irq)
{
	int gpio = irq - INT_GPIO_BASE;

	tegra_gpio_mask_write(GPIO_MSK_INT_ENB(gpio), gpio, 1);
}

static int tegra_gpio_irq_set_type(unsigned int irq, unsigned int type)
{
	int gpio = irq - INT_GPIO_BASE;
	struct tegra_gpio_bank *bank = get_irq_chip_data(irq);
	int port = GPIO_PORT(gpio);
	int lvl_type;
	int val;
	unsigned long flags;

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
		lvl_type = GPIO_INT_LVL_EDGE_RISING;
		break;

	case IRQ_TYPE_EDGE_FALLING:
		lvl_type = GPIO_INT_LVL_EDGE_FALLING;
		break;

	case IRQ_TYPE_EDGE_BOTH:
		lvl_type = GPIO_INT_LVL_EDGE_BOTH;
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		lvl_type = GPIO_INT_LVL_LEVEL_HIGH;
		break;

	case IRQ_TYPE_LEVEL_LOW:
		lvl_type = GPIO_INT_LVL_LEVEL_LOW;
		break;

	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&bank->lvl_lock[port], flags);

	val = __raw_readl(GPIO_INT_LVL(gpio));
	val &= ~(GPIO_INT_LVL_MASK << GPIO_BIT(gpio));
	val |= lvl_type << GPIO_BIT(gpio);
	__raw_writel(val, GPIO_INT_LVL(gpio));

	spin_unlock_irqrestore(&bank->lvl_lock[port], flags);

	if (type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH))
		__set_irq_handler_unlocked(irq, handle_level_irq);
	else if (type & (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING))
		__set_irq_handler_unlocked(irq, handle_edge_irq);

	if (tegra_get_suspend_mode() == TEGRA_SUSPEND_LP0)
		tegra_set_lp0_wake_type(irq, type);

	return 0;
}

static void tegra_gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	struct tegra_gpio_bank *bank;
	int port;
	int pin;
	int unmasked = 0;

	desc->chip->ack(irq);

	bank = get_irq_data(irq);

	for (port = 0; port < 4; port++) {
		int gpio = tegra_gpio_compose(bank->bank, port, 0);
		unsigned long sta = __raw_readl(GPIO_INT_STA(gpio)) &
			__raw_readl(GPIO_INT_ENB(gpio));
		u32 lvl = __raw_readl(GPIO_INT_LVL(gpio));

		for_each_set_bit(pin, &sta, 8) {
			__raw_writel(1 << pin, GPIO_INT_CLR(gpio));

			/* if gpio is edge triggered, clear condition
			 * before executing the hander so that we don't
			 * miss edges
			 */
			if (lvl & (0x100 << pin)) {
				unmasked = 1;
				desc->chip->unmask(irq);
			}

			generic_handle_irq(gpio_to_irq(gpio + pin));
		}
	}

	if (!unmasked)
		desc->chip->unmask(irq);

}

#ifdef CONFIG_PM
#include <mach/gpio-sec.h>

#define NO		0
#define YES		1

#define GPIO_OUTPUT	1
#define GPIO_INPUT	0

#define GPIO_LEVEL_HIGH		1
#define GPIO_LEVEL_LOW		0
#define GPIO_LEVEL_NONE		(-1)

#define GPIO_HWREV_NONE		((unsigned int)0)
#define GPIO_HWREV_06		((unsigned int)6)
#define GPIO_HWREV_08		((unsigned int)8)
#define GPIO_HWREV_09		((unsigned int)9)

#define GPIO_SLP_HOLD_PREVIOUS_LEVEL		GPIO_LEVEL_NONE

#define GPIO_I2C_SCL_GROUP_SLEEP_LEVEL		GPIO_LEVEL_HIGH
#define GPIO_I2C_SDA_GROUP_SLEEP_LEVEL		GPIO_LEVEL_HIGH

#define GPIO_EXT_PU_GROUP_SLEEP_LEVEL		GPIO_LEVEL_HIGH
#define GPIO_EXT_PD_GROUP_SLEEP_LEVEL		GPIO_LEVEL_LOW

#define GPIO_LDO_ENABLE_GROUP_SLEEP_LEVEL	GPIO_LEVEL_LOW

#define GPIO_CP_ON_PIN_GROUP_SLP_LEVEL		GPIO_LEVEL_HIGH
struct sec_slp_gpio_cfg_st {
	int slp_ctrl;
	unsigned int gpio;
	int dir;
	int val;
	unsigned int hwrev;
};

static struct sec_slp_gpio_cfg_st p3_sleep_gpio_table_rev04[] = {
	{NO,	TEGRA_GPIO_PN7,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,				GPIO_HWREV_NONE  },	/* GPIO_BT_HOST_WAKE */
	{YES,	TEGRA_GPIO_PI4,		GPIO_OUTPUT,	GPIO_LDO_ENABLE_GROUP_SLEEP_LEVEL,	GPIO_HWREV_NONE  },	/* GPIO_MLCD_ON */
};

static struct sec_slp_gpio_cfg_st p3_sleep_gpio_table_rev06[] = {
	{YES,	TEGRA_GPIO_PH1,		GPIO_OUTPUT,	GPIO_I2C_SCL_GROUP_SLEEP_LEVEL,	GPIO_HWREV_06  },	/* GPIO_HDMI_I2C_SCL */
	{NO,	TEGRA_GPIO_PH2,		GPIO_OUTPUT,	GPIO_I2C_SDA_GROUP_SLEEP_LEVEL,	GPIO_HWREV_06  },	/* GPIO_HDMI_I2C_SDA */
};

static struct sec_slp_gpio_cfg_st p3_sleep_gpio_table_rev08[] = {
	{NO,	TEGRA_GPIO_PK4,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },	/* GPIO_BL_RESET */
};

static struct sec_slp_gpio_cfg_st p3_sleep_gpio_table[] = {
/* dedicated to P3 Main Rev02 */

	{YES,	GPIO_MLCD_ON,			GPIO_OUTPUT,	GPIO_LDO_ENABLE_GROUP_SLEEP_LEVEL,	GPIO_HWREV_06  },
	{YES,	GPIO_MLCD_ON1,			GPIO_OUTPUT,	GPIO_LDO_ENABLE_GROUP_SLEEP_LEVEL,	GPIO_HWREV_NONE  },
	{NO,	GPIO_BL_RESET,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_09  },

	{YES,	GPIO_IMA_PWREN,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },
	{YES,	GPIO_IMA_N_RST,			GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  },
	{NO,	GPIO_IMA_BYPASS,		GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  },
	{NO,	GPIO_IMA_SLEEP,			GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  },
	{YES,	GPIO_IMAGE_I2C_SCL,		GPIO_OUTPUT,	GPIO_I2C_SCL_GROUP_SLEEP_LEVEL, GPIO_HWREV_NONE  },
	{NO,	GPIO_IMAGE_I2C_SDA,		GPIO_OUTPUT,	GPIO_I2C_SDA_GROUP_SLEEP_LEVEL, GPIO_HWREV_NONE  },

	{NO,	GPIO_CAM_L_nRST,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },
	{YES,	GPIO_CAM_F_nRST,		GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  },
	{NO,	GPIO_CAM_F_STANDBY,		GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  },
	{NO,	GPIO_CAM_PMIC_SDA,		GPIO_OUTPUT,	GPIO_I2C_SDA_GROUP_SLEEP_LEVEL, GPIO_HWREV_NONE  },	//ykh
	{YES,	GPIO_CAM_PMIC_SCL,		GPIO_OUTPUT,	GPIO_I2C_SCL_GROUP_SLEEP_LEVEL, GPIO_HWREV_NONE  },	//ykh
/*	{YES,	GPIO_CAM_PMIC_EN1,		GPIO_OUTPUT,	GPIO_LDO_ENABLE_GROUP_SLEEP_LEVEL, 	GPIO_HWREV_NONE  },	*/
/*	{YES,	GPIO_CAM_PMIC_EN2,		GPIO_OUTPUT,	GPIO_LDO_ENABLE_GROUP_SLEEP_LEVEL, 	GPIO_HWREV_NONE  },	*/
/*	{YES,	GPIO_CAM_PMIC_EN3,		GPIO_OUTPUT,	GPIO_LDO_ENABLE_GROUP_SLEEP_LEVEL, GPIO_HWREV_NONE	},	*/
/*	{YES,	GPIO_CAM_PMIC_EN4,		GPIO_OUTPUT,	GPIO_LDO_ENABLE_GROUP_SLEEP_LEVEL,	GPIO_HWREV_NONE  }, */
/*	{YES,	GPIO_CAM_PMIC_EN5,		GPIO_OUTPUT,	GPIO_LDO_ENABLE_GROUP_SLEEP_LEVEL,	GPIO_HWREV_NONE  }, */

	{YES,	GPIO_WLAN_EN,			GPIO_OUTPUT,	GPIO_SLP_HOLD_PREVIOUS_LEVEL,	GPIO_HWREV_NONE  },
	{NO,	GPIO_WLAN_HOST_WAKE,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },

	{YES,	GPIO_BT_EN,			GPIO_OUTPUT,	GPIO_SLP_HOLD_PREVIOUS_LEVEL,	GPIO_HWREV_NONE },
	{YES,	GPIO_BT_nRST,			GPIO_OUTPUT,	GPIO_SLP_HOLD_PREVIOUS_LEVEL,	GPIO_HWREV_NONE  },
	{NO,	GPIO_BT_WAKE,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },
	{NO,	GPIO_BT_HOST_WAKE,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_06  },

	{NO,	GPIO_TA_EN,			GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  },
	{NO,	GPIO_TA_nCONNECTED,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },
	{NO,	GPIO_TA_nCHG,			GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  },
	{NO,	GPIO_CURR_ADJ,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_06  },

	{YES,	GPIO_ACCESSORY_EN,		GPIO_OUTPUT,	GPIO_LDO_ENABLE_GROUP_SLEEP_LEVEL,	GPIO_HWREV_06  },
	{NO,	GPIO_ACCESSORY_INT,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },
	{NO,	GPIO_V_ACCESSORY_5V,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },
	{NO,	GPIO_IFCONSENSE,		GPIO_INPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },
	{NO,	GPIO_DOCK_INT,			GPIO_INPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },
	{NO,	GPIO_EXT_WAKEUP,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },
	{NO,	GPIO_DET_3_5,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },
	{NO,	GPIO_REMOTE_SENSE_IRQ,		GPIO_INPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  },
	{NO,	GPIO_EAR_SEND_END,		GPIO_INPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  },

	{YES,	GPIO_UART_SEL,			GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  },
	{YES,	GPIO_USB_SEL1,			GPIO_OUTPUT,	GPIO_LEVEL_HIGH,		GPIO_HWREV_NONE },
	{YES,	GPIO_USB_SEL2,			GPIO_OUTPUT,	GPIO_LEVEL_HIGH,		GPIO_HWREV_NONE  },

	{YES,	GPIO_TOUCH_EN,			GPIO_OUTPUT,	GPIO_LDO_ENABLE_GROUP_SLEEP_LEVEL,	GPIO_HWREV_NONE  },
/*	{NO,	GPIO_TOUCH_RST, 		GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  }, */
	{NO,	GPIO_TOUCH_INT, 		GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  },

	{NO,	GPIO_MHL_INT,			GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  },
	{NO,	GPIO_HDMI_HPD,			GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_06  },
	{YES,	GPIO_HDMI_LOGIC_I2C_SCL,	GPIO_OUTPUT,	GPIO_I2C_SCL_GROUP_SLEEP_LEVEL, GPIO_HWREV_NONE  },
	{NO,	GPIO_HDMI_LOGIC_I2C_SDA,	GPIO_OUTPUT,	GPIO_I2C_SDA_GROUP_SLEEP_LEVEL, GPIO_HWREV_NONE  },
	{NO,	GPIO_LVDS_N_SHDN,		GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  },

	{YES,	GPIO_CODEC_LDO_EN,		GPIO_OUTPUT,	GPIO_LDO_ENABLE_GROUP_SLEEP_LEVEL,	GPIO_HWREV_NONE  },
	{YES,	GPIO_CODEC_I2C_SCL,		GPIO_OUTPUT,	GPIO_I2C_SCL_GROUP_SLEEP_LEVEL,	GPIO_HWREV_NONE  },
	{NO,	GPIO_CODEC_I2C_SDA,		GPIO_OUTPUT,	GPIO_I2C_SDA_GROUP_SLEEP_LEVEL,	GPIO_HWREV_NONE  },

	{YES,	GPIO_MOTOR_EN,			GPIO_OUTPUT,	GPIO_LDO_ENABLE_GROUP_SLEEP_LEVEL, 	GPIO_HWREV_NONE  },
	{YES,	GPIO_MOTOR_I2C_SCL,		GPIO_OUTPUT,	GPIO_I2C_SCL_GROUP_SLEEP_LEVEL,	GPIO_HWREV_NONE  },
	{NO,	GPIO_MOTOR_I2C_SDA,		GPIO_OUTPUT,	GPIO_I2C_SDA_GROUP_SLEEP_LEVEL,	GPIO_HWREV_NONE  },

/*	{NO,	GPIO_ADC_INT,			GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  }, */
	{YES,	GPIO_ADC_I2C_SCL,		GPIO_OUTPUT,	GPIO_I2C_SCL_GROUP_SLEEP_LEVEL,	GPIO_HWREV_NONE  },
	{NO,	GPIO_ADC_I2C_SDA,		GPIO_OUTPUT,	GPIO_I2C_SDA_GROUP_SLEEP_LEVEL,	GPIO_HWREV_NONE  },

	{YES,	GPIO_ACC_EN,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_09 },

	{NO,	GPIO_nTHRM_IRQ, 		GPIO_OUTPUT,	GPIO_LEVEL_HIGH,		GPIO_HWREV_06  },

	{NO,	GPIO_MPU_INT,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },
	{NO,	GPIO_AK8975_INT,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },
	{YES,	GPIO_MAG_I2C_SCL,		GPIO_OUTPUT,	GPIO_I2C_SCL_GROUP_SLEEP_LEVEL, GPIO_HWREV_NONE  },
	{NO,	GPIO_MAG_I2C_SDA,		GPIO_OUTPUT,	GPIO_I2C_SDA_GROUP_SLEEP_LEVEL, GPIO_HWREV_NONE  },

	{NO,	GPIO_FUEL_ALRT,			GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_06  },
	{YES,	GPIO_FUEL_I2C_SCL,		GPIO_OUTPUT,	GPIO_I2C_SCL_GROUP_SLEEP_LEVEL, GPIO_HWREV_NONE  },
	{NO,	GPIO_FUEL_I2C_SDA,		GPIO_OUTPUT,	GPIO_I2C_SDA_GROUP_SLEEP_LEVEL, GPIO_HWREV_NONE  },

	{YES,	GPIO_LIGHT_I2C_SCL,		GPIO_OUTPUT,	GPIO_I2C_SCL_GROUP_SLEEP_LEVEL, GPIO_HWREV_NONE  },
	{NO,	GPIO_LIGHT_I2C_SDA,		GPIO_OUTPUT,	GPIO_I2C_SDA_GROUP_SLEEP_LEVEL, GPIO_HWREV_NONE  },

	{YES,	GPIO_GPS_UART_TXD,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE },
	{YES,	GPIO_GPS_UART_RXD,		GPIO_INPUT,	GPIO_LEVEL_NONE,		GPIO_HWREV_NONE },
	{YES,	GPIO_GPS_UART_CTS,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE },
	{YES,	GPIO_GPS_UART_RTS,		GPIO_INPUT,	GPIO_LEVEL_NONE,		GPIO_HWREV_NONE },

/*	{NO,	GPIO_HW_REV0,			GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  }, 	*/
	{NO,	GPIO_HW_REV1,			GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  },
	{NO,	GPIO_HW_REV2,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },
	{NO,	GPIO_HW_REV3,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },
	{NO,	GPIO_HW_REV4,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },

	{YES,	TEGRA_GPIO_PW6,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE },	/* BT_UART_TXD */
	{YES,	TEGRA_GPIO_PW7,			GPIO_INPUT,	GPIO_LEVEL_NONE,		GPIO_HWREV_NONE },	/* BT_UART_RXD */
	{NO,	TEGRA_GPIO_PA1,			GPIO_INPUT,	GPIO_LEVEL_NONE,		GPIO_HWREV_NONE },	/* BT_UART_CTS */
	{NO,	TEGRA_GPIO_PC0,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE },	/* BT_UART_RTS */

	{YES,	TEGRA_GPIO_PN0,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE },	/* I2S_SYNC */
	{YES,	TEGRA_GPIO_PA2,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE },	/* VOICE_SYNC */
	{YES,	TEGRA_GPIO_PK0,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE },	/* JTAG_SET0 */
	{YES,	TEGRA_GPIO_PK1,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE },	/* JTAG_SET1 */
#ifndef CONFIG_SEC_KEYBOARD_DOCK
	{YES,	TEGRA_GPIO_PC2,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE },	/* AP_TXD */
	{YES,	TEGRA_GPIO_PC3,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE },	/* AP_RXD */
#endif

#if 1
	/* for T250S NC (inner pins, not exposed) */
	{YES,	TEGRA_GPIO_PS3, 		GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE },
	{YES,	TEGRA_GPIO_PS4, 		GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE },
	{YES,	TEGRA_GPIO_PS5, 		GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE },
	{YES,	TEGRA_GPIO_PS6, 		GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE },
	{YES,	TEGRA_GPIO_PS7, 		GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE },

	{YES,	TEGRA_GPIO_PQ6, 		GPIO_OUTPUT,	GPIO_LEVEL_HIGH, 		GPIO_HWREV_NONE },
	{YES,	TEGRA_GPIO_PQ7, 		GPIO_OUTPUT,	GPIO_LEVEL_HIGH, 		GPIO_HWREV_NONE },

	{YES,	TEGRA_GPIO_PV4,			GPIO_OUTPUT,	GPIO_LEVEL_HIGH,		GPIO_HWREV_NONE },
	{YES,	TEGRA_GPIO_PV5,			GPIO_OUTPUT,	GPIO_LEVEL_HIGH,		GPIO_HWREV_NONE },
#endif

#if defined(CONFIG_MACH_SAMSUNG_P5WIFI)
	{YES,	GPIO_CP_ON, 		GPIO_OUTPUT,	GPIO_LEVEL_LOW, GPIO_HWREV_NONE  },
	{YES,	GPIO_CP_RST,			GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE },
	{YES,	GPIO_RESET_REQ_N,		GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE },
	{YES,	GPIO_PHONE_ACTIVE,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },
	{YES,	GPIO_PDA_ACTIVE,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },
	{YES,	GPIO_SIM_DETECT,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },

	{YES,	GPIO_HSIC_EN,			GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  },
	{YES,	GPIO_HSIC_SUS_REQ,		GPIO_OUTPUT,	GPIO_LEVEL_HIGH, 		GPIO_HWREV_NONE },
	{YES,	GPIO_HSIC_ACTIVE_STATE,		GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  },
	{YES,	GPIO_IPC_HOST_WAKEUP,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,		GPIO_HWREV_NONE  },
	{YES,	GPIO_IPC_SLAVE_WAKEUP,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },

	{YES,	GPIO_GPS_CNTL,			GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  },
#else
	{YES,	GPIO_CP_ON, 		GPIO_OUTPUT,	GPIO_CP_ON_PIN_GROUP_SLP_LEVEL, GPIO_HWREV_NONE  },
	{YES,	GPIO_CP_RST,			GPIO_OUTPUT,	GPIO_CP_ON_PIN_GROUP_SLP_LEVEL, GPIO_HWREV_NONE },
	{YES,	GPIO_RESET_REQ_N,		GPIO_OUTPUT,	GPIO_CP_ON_PIN_GROUP_SLP_LEVEL, GPIO_HWREV_NONE },
	{NO,	GPIO_PHONE_ACTIVE,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },
	{YES,	GPIO_PDA_ACTIVE,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },
	{NO,	GPIO_SIM_DETECT,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },

	{YES,	GPIO_HSIC_EN,			GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  },
	{NO,	GPIO_HSIC_SUS_REQ,		GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE },
	{NO,	GPIO_HSIC_ACTIVE_STATE,		GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  },
	{YES,	GPIO_IPC_HOST_WAKEUP,		GPIO_INPUT,	GPIO_LEVEL_NONE,		GPIO_HWREV_NONE  },
	{NO,	GPIO_IPC_SLAVE_WAKEUP,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },

	{YES,	GPIO_GPS_CNTL,			GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  },
#endif

#if !defined(CONFIG_MACH_SAMSUNG_P5WIFI)
	{YES,	GPIO_IPC_TXD,			GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE },
	{YES,	GPIO_IPC_RXD,			GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE },
#endif

#if defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
	{YES,	GPIO_TDMB_EN,			GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE },
	{YES,	GPIO_TDMB_RST,			GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE },
	{YES,	GPIO_TDMB_SPI_CS,		GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE },
	{YES,	GPIO_TDMB_SPI_CLK,		GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE },
	{YES,	GPIO_TDMB_SPI_MOSI,		GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE },
	{YES,	GPIO_TDMB_SPI_MISO,		GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE },
#endif

/* exp	{YES,	GPIO_CAM_MOVIE_EN,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },*/
/* exp	{YES,	GPIO_CAM_FLASH_EN,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },*/
/* exp	{YES,	GPIO_GPS_PWR_EN,		GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  },*/
/* exp	{YES,	GPIO_GPS_N_RST,			GPIO_OUTPUT,	GPIO_LEVEL_LOW, 		GPIO_HWREV_NONE  },*/
/* exp	{YES,	GPIO_LIGHT_SENSOR_DVI,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },*/
/* exp	{YES,	GPIO_HDMI_EN1,			GPIO_OUTPUT,	GPIO_LDO_ENABLE_GROUP_SLEEP_LEVEL, 	GPIO_HWREV_NONE  },*/
/* exp	{YES,	GPIO_OTG_EN,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },*/
/* exp	{NO,	GPIO_ISP_INT,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },*/
/* exp	{YES,	GPIO_MICBIAS_EN,		GPIO_OUTPUT,	GPIO_LDO_ENABLE_GROUP_SLEEP_LEVEL,	GPIO_HWREV_NONE  },*/
/* exp	{YES,	GPIO_MHL_RST,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },*/
/* exp	{YES,	GPIO_EAR_MICBIAS_EN,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,			GPIO_HWREV_NONE  },*/
};

static void tegra_set_sleep_gpio_cfg(struct sec_slp_gpio_cfg_st *slp_gpio_cfg)
{
	tegra_gpio_enable(slp_gpio_cfg[0].gpio);

	if (slp_gpio_cfg[0].dir == GPIO_OUTPUT) {
		tegra_gpio_mask_write(GPIO_MSK_OE(slp_gpio_cfg[0].gpio), slp_gpio_cfg[0].gpio, 1);
		if (slp_gpio_cfg[0].val != GPIO_LEVEL_NONE) {
			tegra_gpio_mask_write(GPIO_MSK_OUT(slp_gpio_cfg[0].gpio),
							slp_gpio_cfg[0].gpio, slp_gpio_cfg[0].val);
		}
	} else if (slp_gpio_cfg[0].dir == GPIO_INPUT) {
		tegra_gpio_mask_write(GPIO_MSK_OE(slp_gpio_cfg[0].gpio), slp_gpio_cfg[0].gpio, 0);
	}
}

static void tegra_set_sleep_gpio_table_checkrev(struct sec_slp_gpio_cfg_st *slp_gpio_cfg_table, int table_size)
{
	int cnt;
	for (cnt = 0; cnt < table_size; cnt++) {
		if (slp_gpio_cfg_table[cnt].slp_ctrl == YES
				&& system_rev >= slp_gpio_cfg_table[cnt].hwrev)
			tegra_set_sleep_gpio_cfg(&slp_gpio_cfg_table[cnt]);
	}
}

static void tegra_set_sleep_gpio_table(void)
{
	tegra_set_sleep_gpio_table_checkrev(p3_sleep_gpio_table, ARRAY_SIZE(p3_sleep_gpio_table));

	if (system_rev < 9)
		tegra_set_sleep_gpio_table_checkrev(p3_sleep_gpio_table_rev08, ARRAY_SIZE(p3_sleep_gpio_table_rev08));		

	if (system_rev < 8)
		tegra_set_sleep_gpio_table_checkrev(p3_sleep_gpio_table_rev06, ARRAY_SIZE(p3_sleep_gpio_table_rev06));		

	if (system_rev < 6)
		tegra_set_sleep_gpio_table_checkrev(p3_sleep_gpio_table_rev04, ARRAY_SIZE(p3_sleep_gpio_table_rev04));		
}

struct sec_gpio_cfg_st {
	int attr;
	unsigned int gpio;
	int dir;
	int val;
	unsigned int hwrev;
};

#define SPIO 0
#define GPIO 1
#define GPIO_INIT_LEVEL_IPC_BACK_POWERING	GPIO_LEVEL_LOW

static struct sec_gpio_cfg_st p3_gpio_table_rev04[] = {
	{GPIO,	TEGRA_GPIO_PN7,		GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },	/* GPIO_BT_HOST_WAKE */
	{GPIO,	TEGRA_GPIO_PI4,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },	/* GPIO_MLCD_ON */
};

static struct sec_gpio_cfg_st p3_gpio_table_rev06[] = {
	{GPIO,	TEGRA_GPIO_PH1,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_06 },	/* GPIO_HDMI_I2C_SCL */
	{GPIO,	TEGRA_GPIO_PH2,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_06 },	/* GPIO_HDMI_I2C_SDA */
};

static struct sec_gpio_cfg_st p3_gpio_table_rev08[] = {
	{GPIO,	TEGRA_GPIO_PK4,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },	/* GPIO_BL_RESET */
};

static struct sec_gpio_cfg_st p3_gpio_table_rev09[] = {
	{GPIO,	TEGRA_GPIO_PH2,		GPIO_INPUT,		GPIO_LEVEL_NONE,	GPIO_HWREV_09 },	/* GPIO_TOUCH_ID */
};

static struct sec_gpio_cfg_st p3_gpio_table[] = {
	
/* dedicated to P3 Main Rev02 */

	{GPIO,	GPIO_MLCD_ON,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_06 },
/*	{GPIO,	GPIO_LCD_LDO_EN,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },	*/
	{GPIO,	GPIO_BL_RESET,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_09 },

	{GPIO,	GPIO_IMA_PWREN,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_IMA_N_RST,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_IMA_BYPASS,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_IMA_SLEEP,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_IMAGE_I2C_SCL,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_IMAGE_I2C_SDA,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },

	{GPIO,	GPIO_CAM_L_nRST,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_CAM_F_nRST,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_CAM_F_STANDBY,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_CAM_PMIC_SDA,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_CAM_PMIC_SCL,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
/*	{GPIO,	GPIO_CAM_PMIC_EN1,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE }, */
/*	{GPIO,	GPIO_CAM_PMIC_EN2,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },	*/
/*	{GPIO,	GPIO_CAM_PMIC_EN3,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },	*/
/*	{GPIO,	GPIO_CAM_PMIC_EN4,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },	*/
/*	{GPIO,	GPIO_CAM_PMIC_EN5,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },	*/

	{GPIO,	GPIO_WLAN_EN,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_WLAN_HOST_WAKE,		GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },

	{GPIO,	GPIO_BT_EN,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_BT_nRST,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_BT_WAKE,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_BT_HOST_WAKE,		GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_06 },

	{GPIO,	GPIO_TA_EN,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_TA_nCONNECTED,		GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_TA_nCHG,			GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },

	{GPIO,	GPIO_ACCESSORY_INT,		GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_V_ACCESSORY_5V,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_IFCONSENSE,		GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_DOCK_INT,			GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_EXT_WAKEUP,		GPIO_INPUT, GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_DET_3_5,			GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_REMOTE_SENSE_IRQ,		GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_EAR_SEND_END,		GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },

	{GPIO,	GPIO_UART_SEL,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_USB_SEL1,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_USB_SEL2,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_USB_RECOVERY,		GPIO_INPUT, GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },

	{GPIO,	GPIO_TOUCH_EN,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
/*	{GPIO,	GPIO_TOUCH_RST, 		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },	*/
	{GPIO,	GPIO_TOUCH_INT, 		GPIO_INPUT, GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_TOUCH_ID, 		GPIO_INPUT, GPIO_LEVEL_NONE,	GPIO_HWREV_09 },

	{GPIO,	GPIO_MHL_INT,			GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_HDMI_LOGIC_I2C_SCL,	GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_HDMI_LOGIC_I2C_SDA,	GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_LVDS_N_SHDN,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },

	{GPIO,	GPIO_CODEC_LDO_EN,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_CODEC_I2C_SDA,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_CODEC_I2C_SCL,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },

	{GPIO,	GPIO_MOTOR_EN,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_MOTOR_I2C_SCL,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_MOTOR_I2C_SDA,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },

/*	{GPIO,	GPIO_ADC_INT,			GPIO_INPUT, GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },	*/
	{GPIO,	GPIO_ADC_I2C_SCL,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_ADC_I2C_SDA,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },

	{GPIO,	GPIO_ACC_EN,			GPIO_OUTPUT,	GPIO_LEVEL_HIGH,	GPIO_HWREV_09 },
/*	{GPIO,	GPIO_ACC_INT,			GPIO_INPUT, GPIO_LEVEL_NONE,	GPIO_HWREV_NONE }, */

	{GPIO,	GPIO_MPU_INT,			GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },

	{GPIO,	GPIO_AK8975_INT,		GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_MAG_I2C_SCL,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_MAG_I2C_SDA,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },

	{GPIO,	GPIO_FUEL_ALRT,			GPIO_OUTPUT,	GPIO_LEVEL_LOW, 	GPIO_HWREV_06  },
	{GPIO,	GPIO_FUEL_I2C_SCL,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_FUEL_I2C_SDA,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },

	{GPIO,	GPIO_LIGHT_I2C_SCL,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_LIGHT_I2C_SDA,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },

	{GPIO,	GPIO_GPIO_I2C_SCL,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_GPIO_I2C_SDA,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },

	{SPIO,	GPIO_GPS_UART_TXD,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{SPIO,	GPIO_GPS_UART_RXD,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{SPIO,	GPIO_GPS_UART_CTS,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{SPIO,	GPIO_GPS_UART_RTS,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },

/*	{GPIO,	GPIO_HW_REV0,			GPIO_INPUT, GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },	*/
	{GPIO,	GPIO_HW_REV1,			GPIO_INPUT, GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_HW_REV2,			GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_HW_REV3,			GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_HW_REV4,			GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },

	{GPIO,	TEGRA_GPIO_PB2,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,		GPIO_HWREV_NONE  },
	{GPIO,	TEGRA_GPIO_PN6,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,		GPIO_HWREV_NONE  },
	{GPIO,	TEGRA_GPIO_PBB1,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,		GPIO_HWREV_NONE  },
#if defined(CONFIG_TDMB) && !defined(CONFIG_TDMB_MODULE)
	{GPIO,  GPIO_TDMB_EN,			 GPIO_OUTPUT,	 GPIO_LEVEL_LOW,	GPIO_HWREV_NONE },
	{GPIO,  GPIO_TDMB_RST,			 GPIO_OUTPUT,	 GPIO_LEVEL_LOW,	GPIO_HWREV_NONE  },
	{GPIO,  GPIO_TDMB_SPI_CS,		 GPIO_OUTPUT,	 GPIO_LEVEL_LOW,	GPIO_HWREV_NONE  },
	{GPIO,  GPIO_TDMB_SPI_CLK,		 GPIO_OUTPUT,	 GPIO_LEVEL_LOW,	GPIO_HWREV_NONE  },
	{GPIO,  GPIO_TDMB_SPI_MOSI,		 GPIO_OUTPUT,	 GPIO_LEVEL_LOW,	GPIO_HWREV_NONE  },
	{GPIO,  GPIO_TDMB_SPI_MISO,		 GPIO_OUTPUT,	 GPIO_LEVEL_LOW,	GPIO_HWREV_NONE  },
#else
	{GPIO,	TEGRA_GPIO_PBB4,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,		GPIO_HWREV_NONE  },
	{GPIO,	TEGRA_GPIO_PBB5,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,		GPIO_HWREV_NONE  },
#endif	
	{GPIO,	TEGRA_GPIO_PJ6,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,		GPIO_HWREV_NONE  },
	{GPIO,	TEGRA_GPIO_PS3,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,		GPIO_HWREV_NONE  },
	{GPIO,	TEGRA_GPIO_PS7,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,		GPIO_HWREV_NONE  },
/*	{GPIO,	TEGRA_GPIO_PZ5,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,		GPIO_HWREV_NONE  }, */
	{GPIO,	TEGRA_GPIO_PK6,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,		GPIO_HWREV_NONE  },
	{GPIO,	TEGRA_GPIO_PX2,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,		GPIO_HWREV_NONE  },
	{GPIO,	TEGRA_GPIO_PI7,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,		GPIO_HWREV_NONE  },
	{GPIO,	TEGRA_GPIO_PH1,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,		GPIO_HWREV_NONE },
	{GPIO,	TEGRA_GPIO_PH2,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,		GPIO_HWREV_NONE },

	{GPIO,	TEGRA_GPIO_PD1,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,	GPIO_HWREV_NONE },
	{GPIO,	TEGRA_GPIO_PD3,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,	GPIO_HWREV_NONE },
	{GPIO,	TEGRA_GPIO_PD4,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,	GPIO_HWREV_NONE },
	{GPIO,	TEGRA_GPIO_PG4,			GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	TEGRA_GPIO_PG5,			GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	TEGRA_GPIO_PG6,			GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	TEGRA_GPIO_PG7,			GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	TEGRA_GPIO_PH4,			GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	TEGRA_GPIO_PH5,			GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	TEGRA_GPIO_PH6,			GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	TEGRA_GPIO_PH7,			GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },

	{GPIO,	TEGRA_GPIO_PA6,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,	GPIO_HWREV_09 },
	{GPIO,	TEGRA_GPIO_PA7,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,	GPIO_HWREV_09 },
	{GPIO,	TEGRA_GPIO_PB7,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,	GPIO_HWREV_NONE },
	{GPIO,	TEGRA_GPIO_PB4,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,	GPIO_HWREV_09 },
	{GPIO,	TEGRA_GPIO_PK4,			GPIO_OUTPUT,	GPIO_LEVEL_HIGH,	GPIO_HWREV_09 },
#if defined(CONFIG_MACH_SAMSUNG_P5KORWIFI)
	{GPIO,	TEGRA_GPIO_PI4,			GPIO_INPUT,	GPIO_LEVEL_NONE,		GPIO_HWREV_NONE  },
#else
	{GPIO,	TEGRA_GPIO_PI4,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,		GPIO_HWREV_NONE  },
#endif

#if defined(CONFIG_MACH_SAMSUNG_P5WIFI)
	{GPIO,	GPIO_CP_ON,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_CP_RST,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_RESET_REQ_N,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_PHONE_ACTIVE,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_PDA_ACTIVE,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_SIM_DETECT,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,	GPIO_HWREV_NONE },

	{GPIO,	GPIO_HSIC_EN,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_HSIC_SUS_REQ,		GPIO_OUTPUT,	GPIO_LEVEL_HIGH,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_HSIC_ACTIVE_STATE,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_IPC_HOST_WAKEUP,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_IPC_SLAVE_WAKEUP,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,	GPIO_HWREV_NONE },

	{GPIO,	GPIO_GPS_CNTL,			GPIO_OUTPUT,	GPIO_LEVEL_LOW,	GPIO_HWREV_NONE },

	{GPIO,	TEGRA_GPIO_PP0, 		GPIO_OUTPUT,	GPIO_LEVEL_LOW,	GPIO_HWREV_NONE },
	{GPIO,	TEGRA_GPIO_PP1, 		GPIO_OUTPUT,	GPIO_LEVEL_LOW,	GPIO_HWREV_NONE },
	{GPIO,	TEGRA_GPIO_PP2, 		GPIO_OUTPUT,	GPIO_LEVEL_LOW,	GPIO_HWREV_NONE },
	{GPIO,	TEGRA_GPIO_PP3, 		GPIO_OUTPUT,	GPIO_LEVEL_LOW,	GPIO_HWREV_NONE },
#else
	{GPIO,	GPIO_CP_ON,			GPIO_OUTPUT,	GPIO_INIT_LEVEL_IPC_BACK_POWERING,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_CP_RST,			GPIO_OUTPUT,	GPIO_INIT_LEVEL_IPC_BACK_POWERING,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_RESET_REQ_N,		GPIO_OUTPUT,	GPIO_INIT_LEVEL_IPC_BACK_POWERING,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_PHONE_ACTIVE,		GPIO_OUTPUT,	GPIO_INIT_LEVEL_IPC_BACK_POWERING,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_PDA_ACTIVE,		GPIO_OUTPUT,	GPIO_INIT_LEVEL_IPC_BACK_POWERING,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_SIM_DETECT,		GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },

	{GPIO,	GPIO_HSIC_EN,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_HSIC_SUS_REQ,		GPIO_OUTPUT,	GPIO_INIT_LEVEL_IPC_BACK_POWERING,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_HSIC_ACTIVE_STATE,		GPIO_OUTPUT,	GPIO_INIT_LEVEL_IPC_BACK_POWERING,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_IPC_HOST_WAKEUP,		GPIO_OUTPUT,	GPIO_INIT_LEVEL_IPC_BACK_POWERING,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_IPC_SLAVE_WAKEUP,		GPIO_OUTPUT,	GPIO_INIT_LEVEL_IPC_BACK_POWERING,	GPIO_HWREV_NONE },

	{GPIO,	GPIO_GPS_CNTL,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },
#endif

#if !defined(CONFIG_MACH_SAMSUNG_P5WIFI)
	{GPIO,	GPIO_IPC_TXD,			GPIO_OUTPUT,	GPIO_INIT_LEVEL_IPC_BACK_POWERING,	GPIO_HWREV_NONE },
	{GPIO,	GPIO_IPC_RXD,			GPIO_OUTPUT,	GPIO_INIT_LEVEL_IPC_BACK_POWERING,	GPIO_HWREV_NONE },
#endif

/* exp	{GPIO,	GPIO_ACCESSORY_EN,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },*/
/* exp	{GPIO,	GPIO_ISP_INT,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },*/
/* exp	{GPIO,	GPIO_EAR_MICBIAS_EN,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },*/
/* exp	{GPIO,	GPIO_OTG_EN,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },*/
/* exp	{GPIO,	GPIO_GPS_PWR_EN,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },*/
/* exp	{GPIO,	GPIO_GPS_N_RST,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },*/
/* exp	{GPIO,	GPIO_MHL_RST,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },*/
/* exp	{GPIO,	GPIO_HDMI_EN1,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },*/
/* exp	{GPIO,	GPIO_CAM_MOVIE_EN,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,		GPIO_HWREV_NONE },*/
/* exp	{GPIO,	GPIO_CAM_FLASH_EN,		GPIO_OUTPUT,	GPIO_LEVEL_LOW,		GPIO_HWREV_NONE },*/
/* exp	{GPIO,	GPIO_MICBIAS_EN,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },*/
/* exp	{GPIO,	GPIO_LIGHT_SENSOR_DVI,		GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },*/

/* exp	{SPIO,	GPIO_HDMI_HPD,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },*/
/* exp	{GPIO,	GPIO_nTHRM_IRQ,			GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE }, */
/* exp	{GPIO,	GPIO_FUEL_ALRT,			GPIO_INPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },*/
/* exp	{GPIO,	GPIO_CURR_ADJ,			GPIO_OUTPUT,	GPIO_LEVEL_NONE,	GPIO_HWREV_NONE },*/
};

static void tegra_set_gpio_init_config(struct sec_gpio_cfg_st *gpio_cfg)
{
	if (gpio_cfg[0].attr == GPIO) {
		tegra_gpio_enable(gpio_cfg[0].gpio);

		if (gpio_cfg[0].dir == GPIO_OUTPUT) {
			tegra_gpio_mask_write(GPIO_MSK_OE(gpio_cfg[0].gpio), gpio_cfg[0].gpio, 1);
			if (gpio_cfg[0].val != GPIO_LEVEL_NONE) {
				tegra_gpio_mask_write(GPIO_MSK_OUT(gpio_cfg[0].gpio), gpio_cfg[0].gpio, gpio_cfg[0].val);
			}
		} else if (gpio_cfg[0].dir == GPIO_INPUT) {
			tegra_gpio_mask_write(GPIO_MSK_OE(gpio_cfg[0].gpio), gpio_cfg[0].gpio, 0);
		} else {
			tegra_gpio_disable(gpio_cfg[0].gpio);
		}
	} else {
		tegra_gpio_disable(gpio_cfg[0].gpio);
	}
}

static void tegra_set_gpio_init_table_checkrev(struct sec_gpio_cfg_st *gpio_cfg_table, int table_size)
{
	int cnt;
	for (cnt = 0; cnt < table_size; cnt++) {
		if (system_rev >= gpio_cfg_table[cnt].hwrev)
			tegra_set_gpio_init_config(&gpio_cfg_table[cnt]);
	}
}

static void tegra_set_gpio_init_table(void)
{
	tegra_set_gpio_init_table_checkrev(p3_gpio_table, ARRAY_SIZE(p3_gpio_table));

	if (system_rev < 9)
		tegra_set_gpio_init_table_checkrev(p3_gpio_table_rev08, ARRAY_SIZE(p3_gpio_table_rev08));

	if (system_rev < 8)
		tegra_set_gpio_init_table_checkrev(p3_gpio_table_rev06, ARRAY_SIZE(p3_gpio_table_rev06));

	if (system_rev < 6)
		tegra_set_gpio_init_table_checkrev(p3_gpio_table_rev04, ARRAY_SIZE(p3_gpio_table_rev04));

	if (system_rev > 8)
		tegra_set_gpio_init_table_checkrev(p3_gpio_table_rev09, ARRAY_SIZE(p3_gpio_table_rev09));
}
#endif

void tegra_gpio_resume(void)
{
	unsigned long flags;
	int b, p, i;

	local_irq_save(flags);

	for (b = 0; b < ARRAY_SIZE(tegra_gpio_banks); b++) {
		struct tegra_gpio_bank *bank = &tegra_gpio_banks[b];

		for (p = 0; p < ARRAY_SIZE(bank->oe); p++) {
			unsigned int gpio = (b<<5) | (p<<3);
			__raw_writel(bank->cnf[p], GPIO_CNF(gpio));
			__raw_writel(bank->out[p], GPIO_OUT(gpio));
			__raw_writel(bank->oe[p], GPIO_OE(gpio));
			__raw_writel(bank->int_lvl[p], GPIO_INT_LVL(gpio));
			__raw_writel(bank->int_enb[p], GPIO_INT_ENB(gpio));
		}
	}

	local_irq_restore(flags);

	for (i = INT_GPIO_BASE; i < (INT_GPIO_BASE + TEGRA_NR_GPIOS); i++) {
		struct irq_desc *desc = irq_to_desc(i);
		if (!desc || (desc->status & IRQ_WAKEUP))
			continue;
		enable_irq(i);
	}
}

void tegra_gpio_suspend(void)
{
	unsigned long flags;
	int b, p, i;

	for (i = INT_GPIO_BASE; i < (INT_GPIO_BASE + TEGRA_NR_GPIOS); i++) {
		struct irq_desc *desc = irq_to_desc(i);
		if (!desc)
			continue;
		if (desc->status & IRQ_WAKEUP) {
			int gpio = i - INT_GPIO_BASE;
			pr_debug("gpio %d.%d is wakeup\n", gpio/8, gpio&7);
			continue;
		}
		disable_irq(i);
	}

	local_irq_save(flags);
	for (b = 0; b < ARRAY_SIZE(tegra_gpio_banks); b++) {
		struct tegra_gpio_bank *bank = &tegra_gpio_banks[b];

		for (p = 0; p < ARRAY_SIZE(bank->oe); p++) {
			unsigned int gpio = (b<<5) | (p<<3);
			bank->cnf[p] = __raw_readl(GPIO_CNF(gpio));
			bank->out[p] = __raw_readl(GPIO_OUT(gpio));
			bank->oe[p] = __raw_readl(GPIO_OE(gpio));
			bank->int_enb[p] = __raw_readl(GPIO_INT_ENB(gpio));
			bank->int_lvl[p] = __raw_readl(GPIO_INT_LVL(gpio));
		}
	}
	local_irq_restore(flags);

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	tegra_set_sleep_gpio_table();
#endif
}

static int tegra_gpio_wake_enable(unsigned int irq, unsigned int enable)
{
	int ret;
	struct tegra_gpio_bank *bank = get_irq_chip_data(irq);

	ret = tegra_set_lp1_wake(bank->irq, enable);
	if (ret)
		return ret;

	if (tegra_get_suspend_mode() == TEGRA_SUSPEND_LP0)
		return tegra_set_lp0_wake(irq, enable);

	return 0;
}


static struct irq_chip tegra_gpio_irq_chip = {
	.name		= "GPIO",
	.ack		= tegra_gpio_irq_ack,
	.mask		= tegra_gpio_irq_mask,
	.unmask		= tegra_gpio_irq_unmask,
	.set_type	= tegra_gpio_irq_set_type,
#ifdef CONFIG_PM
	.set_wake	= tegra_gpio_wake_enable,
#endif
};


/* This lock class tells lockdep that GPIO irqs are in a different
 * category than their parents, so it won't report false recursion.
 */
static struct lock_class_key gpio_lock_class;
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
static void use_sys_clk_req_gpio(void)

{

    unsigned long pmc_base_reg = IO_APB_VIRT + 0xE400;

    unsigned long offset = 0x1c;

    unsigned long mask = (1 << 21);

    volatile unsigned long *p = (volatile unsigned long *)(pmc_base_reg + offset);

    *p = *p & ~mask;

}
#endif


static int __init tegra_gpio_init(void)
{
	struct tegra_gpio_bank *bank;
	int i;
	int j;

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	use_sys_clk_req_gpio();
	/* init the gpio when kernel booting. */
	pr_info("%s()\n", __func__);
	tegra_set_gpio_init_table();
#endif

	for (i = 0; i < 7; i++) {
		for (j = 0; j < 4; j++) {
			int gpio = tegra_gpio_compose(i, j, 0);
			__raw_writel(0x00, GPIO_INT_ENB(gpio));
		}
	}

	gpiochip_add(&tegra_gpio_chip);

	for (i = INT_GPIO_BASE; i < (INT_GPIO_BASE + TEGRA_NR_GPIOS); i++) {
		bank = &tegra_gpio_banks[GPIO_BANK(irq_to_gpio(i))];

		lockdep_set_class(&irq_desc[i].lock, &gpio_lock_class);
		set_irq_chip_data(i, bank);
		set_irq_chip(i, &tegra_gpio_irq_chip);
		set_irq_handler(i, handle_simple_irq);
		set_irq_flags(i, IRQF_VALID);
	}

	for (i = 0; i < ARRAY_SIZE(tegra_gpio_banks); i++) {
		bank = &tegra_gpio_banks[i];

		set_irq_chained_handler(bank->irq, tegra_gpio_irq_handler);
		set_irq_data(bank->irq, bank);

		for (j = 0; j < 4; j++)
			spin_lock_init(&bank->lvl_lock[j]);
	}

	return 0;
}

postcore_initcall(tegra_gpio_init);

#ifdef	CONFIG_DEBUG_FS

#include <linux/debugfs.h>
#include <linux/seq_file.h>

static int dbg_gpio_show(struct seq_file *s, void *unused)
{
	int i;
	int j;

	for (i = 0; i < 7; i++) {
		for (j = 0; j < 4; j++) {
			int gpio = tegra_gpio_compose(i, j, 0);
			seq_printf(s,
				"%d:%d %02x %02x %02x %02x %02x %02x %06x\n",
				i, j,
				__raw_readl(GPIO_CNF(gpio)),
				__raw_readl(GPIO_OE(gpio)),
				__raw_readl(GPIO_OUT(gpio)),
				__raw_readl(GPIO_IN(gpio)),
				__raw_readl(GPIO_INT_STA(gpio)),
				__raw_readl(GPIO_INT_ENB(gpio)),
				__raw_readl(GPIO_INT_LVL(gpio)));
		}
	}
	return 0;
}

static int dbg_gpio_open(struct inode *inode, struct file *file)
{
	return single_open(file, dbg_gpio_show, &inode->i_private);
}

static const struct file_operations debug_fops = {
	.open		= dbg_gpio_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init tegra_gpio_debuginit(void)
{
	(void) debugfs_create_file("tegra_gpio", S_IRUGO,
					NULL, NULL, &debug_fops);
	return 0;
}
late_initcall(tegra_gpio_debuginit);
#endif
