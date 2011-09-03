/*
 * arch/arm/mach-tegra/include/mach/gpio.h
 *
 * Copyright (C) 2010 Google, Inc.
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

#ifndef __MACH_TEGRA_GPIO_H
#define __MACH_TEGRA_GPIO_H

#include <mach/irqs.h>

#define TEGRA_NR_GPIOS		INT_GPIO_NR
#define ARCH_NR_GPIOS		(TEGRA_NR_GPIOS + 128)

#include <asm-generic/gpio.h>

#ifdef CONFIG_GPIO_STMPE1801
static inline int gpio_get_value(unsigned gpio)
{
	if (gpio >= TEGRA_NR_GPIOS)
		return gpio_get_value_cansleep(gpio);
	else
		return __gpio_get_value(gpio);
}
static inline void gpio_set_value(unsigned gpio, int value)
{
	if (gpio >= TEGRA_NR_GPIOS)
		gpio_set_value_cansleep(gpio, value);
	else
		__gpio_set_value(gpio, value);
}
#else
#define gpio_get_value		__gpio_get_value
#define gpio_set_value		__gpio_set_value
#endif
#define gpio_cansleep		__gpio_cansleep

#define TEGRA_GPIO_TO_IRQ(gpio) (INT_GPIO_BASE + (gpio))
#define TEGRA_IRQ_TO_GPIO(irq) ((irq) - INT_GPIO_BASE)

static inline int gpio_to_irq(unsigned int gpio)
{
#ifdef CONFIG_GPIO_STMPE1801
	if (gpio < TEGRA_NR_GPIOS)
		return INT_GPIO_BASE + gpio;
	else
		return __gpio_to_irq(gpio);
#else
	if (gpio < TEGRA_NR_GPIOS)
		return INT_GPIO_BASE + gpio;
	return -EINVAL;
#endif
}

static inline int irq_to_gpio(unsigned int irq)
{
#ifdef CONFIG_GPIO_STMPE1801
/* for using gpio_expander */
	if ((irq >= INT_GPIO_BASE) && (irq < INT_GPIO_BASE + INT_GPIO_NR + NR_BOARD_IRQS))
		return irq - INT_GPIO_BASE;
	return -EINVAL;
#else
	if ((irq >= INT_GPIO_BASE) && (irq < INT_GPIO_BASE + INT_GPIO_NR))
		return irq - INT_GPIO_BASE;
	return -EINVAL;
#endif
}

void tegra_gpio_enable(int gpio);
void tegra_gpio_disable(int gpio);

#endif
