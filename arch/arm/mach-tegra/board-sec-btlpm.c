/* linux/arch/arm/mach-tegra/board-p3-btlpm.c
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

#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/hrtimer.h>
#include <linux/serial_core.h>
#include <asm/mach-types.h>
#include <mach/gpio-sec.h>

static struct bt_lpm {
	struct hrtimer bt_lpm_timer;
	ktime_t bt_lpm_delay;
} bt_lpm;

static enum hrtimer_restart bt_enter_lpm(struct hrtimer *timer)
{
	gpio_set_value(GPIO_BT_WAKE, 0);

	return HRTIMER_NORESTART;
}

void p3_bt_uart_wake_peer(struct uart_port *unused)
{
	if (!bt_lpm.bt_lpm_timer.function)
		return;

	hrtimer_try_to_cancel(&bt_lpm.bt_lpm_timer);
	gpio_set_value(GPIO_BT_WAKE, 1);
	hrtimer_start(&bt_lpm.bt_lpm_timer, bt_lpm.bt_lpm_delay,
		HRTIMER_MODE_REL);
}

int p3_bt_lpm_init(void)
{
	int ret;

	tegra_gpio_enable(GPIO_BT_WAKE);
	ret = gpio_request(GPIO_BT_WAKE, "gpio_bt_wake");
	if (ret) {
		pr_err("Failed to request gpio_bt_wake control\n");
		return 0;
	}

	ret = gpio_direction_output(GPIO_BT_WAKE, 0);
	if (ret) {
		pr_err("Failed to set gpio_direction_output\n");
		return 0;
	}

	hrtimer_init(&bt_lpm.bt_lpm_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	bt_lpm.bt_lpm_delay = ktime_set(1, 0);	/* 1 sec */
	bt_lpm.bt_lpm_timer.function = bt_enter_lpm;
	return 0;
}
