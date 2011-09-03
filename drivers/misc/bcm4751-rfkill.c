/*
 * Broadcom's GPS BCM4751 rfkill power control via GPIO
 *
 * Copyright (C) 2010 Samsung Electronic Co., LTD.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/rfkill.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/regulator/machine.h>
#include <linux/platform_device.h>
#include <linux/bcm4751-rfkill.h>


struct bcm4751 {
	struct platform_device *pdev;
	struct rfkill *rfkill0;
	struct rfkill *rfkill1;

	spinlock_t gps_lock;
	int gpio_nrst;
	int gpio_pwr_en;

	struct mutex lock;
	struct regulator *lna_ldo;
};

/*
 * blocked = false : transmitter on / blocked = true : transmitter off
 */
static int bcm4751_rfkill0_set(void *data, bool blocked)
{
	struct bcm4751 *ddata = data;
	int gpio_pwr_en = ddata->gpio_pwr_en;
	int ret;

	mutex_lock(&ddata->lock);
	if (blocked) {  /* GPS Off */
		gpio_set_value(gpio_pwr_en, 0);
		printk("%s: GPS(bcm4751) turned off. (GPIO_GPS_PWR_EN: low) \n", __func__);

		if (regulator_is_enabled(ddata->lna_ldo) == true) {
			ret = regulator_disable(ddata->lna_ldo);
			if (ret != 0) {
				printk(KERN_ERR, "%s: Failed to disable GPS_LNA power(%d)\n", __func__, ret);
				goto out;
			}
			printk("%s: GPS_LNA LDO turned off.\n", __func__);
		}
	}
	else {  /* GPS On */
		if (regulator_is_enabled(ddata->lna_ldo) == false) {
			ret = regulator_enable(ddata->lna_ldo);
			if (ret != 0) {
				printk(KERN_ERR "%s: Failed to enable GPS_LNA power(%d)\n", __func__, ret);
				goto out;
			}
			printk("%s: GPS_LNA LDO turned on.\n", __func__);
			msleep(10);
		}

		gpio_set_value(gpio_pwr_en, 1);
		printk("%s: GPS(bcm4751) turned on. (GPIO_GPS_PWR_EN: high)\n", __func__);
	}
	ret = 0;

out:
	mutex_unlock(&ddata->lock);
	return ret;
}

static int bcm4751_rfkill1_set(void *data, bool blocked)
{
	struct bcm4751 *ddata = data;
	int gpio_nrst = ddata->gpio_nrst;

	if (blocked) {  /* gps_n_rst low*/
		gpio_set_value(gpio_nrst, 0);
		printk("%s: set GPIO_GPS_N_RST low.\n", __func__);
	}
	else {  /* gps_n_rst high */
		gpio_set_value(gpio_nrst, 1);
		printk("%s: set GPIO_GPS_N_RST high.\n", __func__);
	}

	return 0;
}

static struct rfkill_ops bcm4751_rfkill0_ops = {
	.set_block = bcm4751_rfkill0_set,
};

static struct rfkill_ops bcm4751_rfkill1_ops = {
	.set_block = bcm4751_rfkill1_set,
};

static int bcm4751_rfkill_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct bcm4751 *ddata;
	struct bcm4751_rfkill_platform_data *pdata = pdev->dev.platform_data;

	ddata = kzalloc(sizeof(*ddata), GFP_KERNEL);
	if (ddata == NULL) {
		return -ENOMEM;
	}

	ddata->lna_ldo = regulator_get(NULL, "vdd_ldo5");
	if (IS_ERR(ddata->lna_ldo)) {
		printk(KERN_ERR "%s: can't get GPS_LNA power rail\n", __func__);
		ret = PTR_ERR(ddata->lna_ldo);
		goto err_regulator_get;
	}

	ddata->pdev = pdev;
	ddata->gpio_nrst = pdata->gpio_nrst;
	ddata->gpio_pwr_en = pdata->gpio_pwr_en;

	ddata->rfkill0 = rfkill_alloc("bcm4751", &pdev->dev, RFKILL_TYPE_GPS, &bcm4751_rfkill0_ops, (void*)ddata);
	if (unlikely(!ddata->rfkill0)) {
		ret = -ENOMEM;
		goto err_rfkill0_alloc;
	}

	ddata->rfkill1 = rfkill_alloc("bcm4751", &pdev->dev, RFKILL_TYPE_GPS, &bcm4751_rfkill1_ops, (void*)ddata);
	if (unlikely(!ddata->rfkill1)) {
		ret = -ENOMEM;
		goto err_rfkill1_alloc;
	}

	rfkill_init_sw_state(ddata->rfkill0, (bool)true);
	rfkill_init_sw_state(ddata->rfkill1, (bool)false);

	ret = rfkill_register(ddata->rfkill0);
	if (unlikely(ret)) {
		ret = -ENODEV;
		goto err_rfkill0_register;
	}

	ret = rfkill_register(ddata->rfkill1);
	if (unlikely(ret)) {
		ret = -ENODEV;
		goto err_rfkill1_register;
	}

	mutex_init(&ddata->lock);
	platform_set_drvdata(pdev, ddata);

	return 0;

err_rfkill1_register:
	rfkill_unregister(ddata->rfkill0);
err_rfkill0_register:
	rfkill_destroy(ddata->rfkill1);	
err_rfkill1_alloc:
	rfkill_destroy(ddata->rfkill0);
err_rfkill0_alloc:
	regulator_put(ddata->lna_ldo);
err_regulator_get:
	kfree(ddata);
	ddata = NULL;

	return ret;
}

static int bcm4751_rfkill_remove(struct platform_device *pdev)
{
	struct bcm4751 *ddata = platform_get_drvdata(pdev);

	mutex_destroy(&ddata->lock);

	rfkill_unregister(ddata->rfkill1);
	rfkill_unregister(ddata->rfkill0);
	rfkill_destroy(ddata->rfkill1);	
	rfkill_destroy(ddata->rfkill0);

	regulator_put(ddata->lna_ldo);

	gpio_free(ddata->gpio_nrst);
	gpio_free(ddata->gpio_pwr_en);

	kfree(ddata);
	ddata = NULL;
	
	return 0;
}

static struct platform_driver bcm4751_rfkill_driver = {
	.probe = bcm4751_rfkill_probe,
	.remove = bcm4751_rfkill_remove,
	.driver = {
		   .name = "bcm4751_rfkill",
		   .owner = THIS_MODULE,
		   },
};

static int __init bcm4751_rfkill_init(void)
{
	return platform_driver_register(&bcm4751_rfkill_driver);
}

static void __exit bcm4751_rfkill_exit(void)
{
	platform_driver_unregister(&bcm4751_rfkill_driver);
}

module_init(bcm4751_rfkill_init);
module_exit(bcm4751_rfkill_exit);

MODULE_DESCRIPTION("bcm4751-rfkill");
MODULE_AUTHOR("Samsung");
MODULE_LICENSE("GPL");
