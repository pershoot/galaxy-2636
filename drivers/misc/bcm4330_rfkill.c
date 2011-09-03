/*
 * drivers/misc/bcm4330_rfkill.c
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

#include <linux/err.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/rfkill.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/wakelock.h>

struct bcm4330_rfkill_data {
	int gpio_reset;
	int gpio_shutdown;
	int irq;
	int delay;
	struct clk *bt_32k_clk;
};

static struct bcm4330_rfkill_data *bcm4330_rfkill;
static struct wake_lock rfkill_wake_lock;

static int bcm4330_bt_rfkill_set_power(void *data, bool blocked)
{
	int ret = 0;
	if (blocked) {
		pr_info("[BT] Bluetooth Power off\n");

		ret = disable_irq_wake(bcm4330_rfkill->irq);
		if (ret < 0)
			pr_err("[BT] unset wakeup src failed\n");

		disable_irq(bcm4330_rfkill->irq);

		wake_unlock(&rfkill_wake_lock);

		gpio_direction_output(bcm4330_rfkill->gpio_reset, 0);
		msleep(10);
		gpio_direction_output(bcm4330_rfkill->gpio_shutdown, 0);
		if (bcm4330_rfkill->bt_32k_clk)
			clk_disable(bcm4330_rfkill->bt_32k_clk);
	} else {
		pr_info("[BT] Bluetooth Power on\n");
		if (bcm4330_rfkill->bt_32k_clk)
			clk_enable(bcm4330_rfkill->bt_32k_clk);
		gpio_direction_output(bcm4330_rfkill->gpio_shutdown, 1);
		gpio_direction_output(bcm4330_rfkill->gpio_reset, 1);

		ret = enable_irq_wake(bcm4330_rfkill->irq);
		if (ret < 0)
			pr_err("[BT] set wakeup src failed\n");

		enable_irq(bcm4330_rfkill->irq);
	}

	return 0;
}

static const struct rfkill_ops bcm4330_bt_rfkill_ops = {
	.set_block = bcm4330_bt_rfkill_set_power,
};

irqreturn_t bt_host_wake_irq_handler(int irq, void *dev_id)
{
	wake_lock_timeout(&rfkill_wake_lock, 5*HZ);
	return IRQ_HANDLED;
}

static int bcm4330_rfkill_probe(struct platform_device *pdev)
{
	struct rfkill *bt_rfkill;
	struct resource *res;
	int ret;
	bool enable = false;  /* off */
	bool default_sw_block_state;

	wake_lock_init(&rfkill_wake_lock, WAKE_LOCK_SUSPEND, "bt_host_wake");

	bcm4330_rfkill = kzalloc(sizeof(*bcm4330_rfkill), GFP_KERNEL);
	if (!bcm4330_rfkill)
		return -ENOMEM;

	bcm4330_rfkill->bt_32k_clk = clk_get(&pdev->dev, "bcm4330_32k_clk");
	if (IS_ERR(bcm4330_rfkill->bt_32k_clk)) {
		pr_warn("can't find bcm4330_32k_clk. assuming clock to chip\n");
		bcm4330_rfkill->bt_32k_clk = NULL;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_IO,
						"bcm4330_nreset_gpio");
	if (!res) {
		pr_err("couldn't find reset gpio\n");
		goto free_bcm_32k_clk;
	}
	bcm4330_rfkill->gpio_reset = res->start;
	tegra_gpio_enable(bcm4330_rfkill->gpio_reset);
	ret = gpio_request(bcm4330_rfkill->gpio_reset,
					"bcm4330_nreset_gpio");
	if (unlikely(ret))
		goto free_bcm_32k_clk;

	res = platform_get_resource_byname(pdev, IORESOURCE_IO,
						"bcm4330_nshutdown_gpio");
	if (!res) {
		pr_err("couldn't find shutdown gpio\n");
		gpio_free(bcm4330_rfkill->gpio_reset);
		goto free_bcm_32k_clk;
	}

	bcm4330_rfkill->gpio_shutdown = res->start;
	tegra_gpio_enable(bcm4330_rfkill->gpio_shutdown);
	ret = gpio_request(bcm4330_rfkill->gpio_shutdown,
					"bcm4330_nshutdown_gpio");
	if (unlikely(ret)) {
		gpio_free(bcm4330_rfkill->gpio_reset);
		goto free_bcm_32k_clk;
	}

	bcm4330_rfkill->irq = platform_get_irq(pdev, 0);
	if (!bcm4330_rfkill->irq) {
		pr_err("couldn't find hostwake irq\n");
		goto free_bcm_res;
	}

	ret = request_irq(bcm4330_rfkill->irq, bt_host_wake_irq_handler,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			"bt_host_wake_irq_handler", NULL);

	if (ret < 0) {
		pr_err("[BT] Request_irq failed\n");
		goto free_bcm_res;
	}

	disable_irq(bcm4330_rfkill->irq);

	if (bcm4330_rfkill->bt_32k_clk && enable)
		clk_enable(bcm4330_rfkill->bt_32k_clk);
	gpio_direction_output(bcm4330_rfkill->gpio_shutdown, enable);
	gpio_direction_output(bcm4330_rfkill->gpio_reset, enable);

	bt_rfkill = rfkill_alloc("bcm4330 Bluetooth", &pdev->dev,
				RFKILL_TYPE_BLUETOOTH, &bcm4330_bt_rfkill_ops,
				NULL);

	if (unlikely(!bt_rfkill))
		goto free_bcm_irq;

	default_sw_block_state = !enable;
	rfkill_set_states(bt_rfkill, default_sw_block_state, false);

	ret = rfkill_register(bt_rfkill);

	if (unlikely(ret)) {
		rfkill_destroy(bt_rfkill);
		goto free_bcm_irq;
	}

	return 0;

free_bcm_irq:
	free_irq(bcm4330_rfkill->irq, NULL);
free_bcm_res:
	gpio_free(bcm4330_rfkill->gpio_shutdown);
	gpio_free(bcm4330_rfkill->gpio_reset);
free_bcm_32k_clk:
	if (bcm4330_rfkill->bt_32k_clk && enable)
		clk_disable(bcm4330_rfkill->bt_32k_clk);
	if (bcm4330_rfkill->bt_32k_clk)
		clk_put(bcm4330_rfkill->bt_32k_clk);
	kfree(bcm4330_rfkill);
	return -ENODEV;
}

static int bcm4330_rfkill_remove(struct platform_device *pdev)
{
	struct rfkill *bt_rfkill = platform_get_drvdata(pdev);

	if (bcm4330_rfkill->bt_32k_clk)
		clk_put(bcm4330_rfkill->bt_32k_clk);
	rfkill_unregister(bt_rfkill);
	rfkill_destroy(bt_rfkill);
	free_irq(bcm4330_rfkill->irq, NULL);
	gpio_free(bcm4330_rfkill->gpio_shutdown);
	gpio_free(bcm4330_rfkill->gpio_reset);
	kfree(bcm4330_rfkill);

	return 0;
}

static struct platform_driver bcm4330_rfkill_driver = {
	.probe = bcm4330_rfkill_probe,
	.remove = bcm4330_rfkill_remove,
	.driver = {
		   .name = "bcm4330_rfkill",
		   .owner = THIS_MODULE,
	},
};

static int __init bcm4330_rfkill_init(void)
{
	return platform_driver_register(&bcm4330_rfkill_driver);
}

static void __exit bcm4330_rfkill_exit(void)
{
	platform_driver_unregister(&bcm4330_rfkill_driver);
}

module_init(bcm4330_rfkill_init);
module_exit(bcm4330_rfkill_exit);

MODULE_DESCRIPTION("bcm4330 rfkill");
MODULE_AUTHOR("NVIDIA");
MODULE_LICENSE("GPL");
