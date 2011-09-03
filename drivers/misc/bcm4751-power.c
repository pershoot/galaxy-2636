/*
 * driver/misc/bcm4751-power.c
 *
 * driver supporting bcm4751(GPS) power control
 *
 * COPYRIGHT(C) Samsung Electronics Co., Ltd. 2006-2010 All Right Reserved.  
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

#include <linux/module.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/regulator/machine.h>
#include <linux/blkdev.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <mach/gpio-sec.h>

static int s_nRstState, s_PwrEnState;
static struct regulator *gps_lna;
static DEFINE_MUTEX(gps_mutex);

static struct miscdevice sec_gps_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "sec_gps",
};

static ssize_t gpio_n_rst_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", s_nRstState);
}

static ssize_t gpio_n_rst_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t size)
{
	int stat;
	
	if (sscanf(buf, "%i", &stat) != 1 || (stat < 0 || stat > 1))
		return -EINVAL;

	s_nRstState = stat;
	gpio_set_value(GPIO_GPS_N_RST, s_nRstState);
	printk("Set GPIO_GPS_nRST to %s.\n", (s_nRstState)?"high":"low");

	return size;
}

static DEVICE_ATTR(nrst, S_IRUGO | S_IWUSR, gpio_n_rst_show, gpio_n_rst_store);

static ssize_t gpio_pwr_en_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", s_PwrEnState);
}

static ssize_t gpio_pwr_en_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t size)
{
	int stat;
	int ret;
	int lna_enabled;
	
	if (sscanf(buf, "%i", &stat) != 1 || (stat < 0 || stat > 1))
		return -EINVAL;

	if (stat != s_PwrEnState) {
		lna_enabled = regulator_is_enabled(gps_lna);
	}
	else
		return -EINVAL;

	mutex_lock(&gps_mutex);
	s_PwrEnState = stat;

	if (stat && !lna_enabled) {
		ret = regulator_enable(gps_lna);
		if (ret != 0) {
			printk(KERN_ERR "Failed to enable GPS_LNA_2.85V: %d\n", ret);
			goto end_of_function;
		}
		printk("GPS_LNA LDO turned on\n");
		msleep(10);
	}

	gpio_set_value(GPIO_GPS_PWR_EN, s_PwrEnState);
	printk("Set GPIO_GPS_PWR_EN to %s.\n", (s_PwrEnState)?"high":"low");

	if (!stat && lna_enabled) {
		ret = regulator_disable(gps_lna);
		if (ret != 0) {
			printk(KERN_ERR "Failed to disable GPS_LNA_2.85V: %d\n", ret);
			goto end_of_function;
		}
		printk("GPS_LNA LDO turned off\n");
	}

	ret = size;

end_of_function:
	mutex_unlock(&gps_mutex);
	return ret;
}

static DEVICE_ATTR(pwr_en, S_IRUGO | S_IWUSR, gpio_pwr_en_show, gpio_pwr_en_store);

static void sec_gps_hw_init(void)
{
	gpio_request(GPIO_GPS_N_RST, "GPS_N_RST");
	gpio_direction_output(GPIO_GPS_N_RST, 1);
	s_nRstState  = 1;

	gpio_request(GPIO_GPS_PWR_EN, "GPS_PWR_EN");
	gpio_direction_output(GPIO_GPS_PWR_EN, 0);
	s_PwrEnState = 0;
}

extern struct class *sec_class;
struct device *sec_gps_dev;
static int __init sec_gps_init(void)
{
	int ret=0;

	sec_gps_hw_init();

#if (defined(CONFIG_MACH_SAMSUNG_P5) || defined(CONFIG_MACH_SAMSUNG_P5WIFI))
	gps_lna = regulator_get(NULL, "vdd_ldo8");
#else	/* P4 series */
	gps_lna = regulator_get(NULL, "vdd_ldo5");
#endif

	if (IS_ERR(gps_lna)) {
		printk(KERN_ERR "%s: can't get GPS_LNA\n", __func__);
		return PTR_ERR(gps_lna);
	}

	ret = misc_register(&sec_gps_device);
	if (ret<0) {
		printk(KERN_ERR "misc_register failed!\n");
		goto fail_after_regulator_get;
	}

	sec_gps_dev = device_create(sec_class, NULL, 0, NULL, "gps");
	if (IS_ERR(sec_gps_dev)) {
		printk(KERN_ERR "failed to create device!\n");
		ret = -EINVAL;
		goto fail_after_misc_reg;
	}

	if (device_create_file(sec_gps_dev, &dev_attr_nrst) < 0) {
		printk(KERN_ERR "failed to create device file!(%s)!\n", dev_attr_nrst.attr.name);
		ret = -EINVAL;
		goto fail_after_device_create;
	}

	if (device_create_file(sec_gps_dev, &dev_attr_pwr_en) < 0) {
		printk(KERN_ERR "failed to create device file!(%s)!\n", dev_attr_pwr_en.attr.name);
		device_remove_file(sec_gps_dev, &dev_attr_nrst);
		ret = -EINVAL;
		goto fail_after_device_create;
	}
	
	return 0;

fail_after_device_create:
	device_destroy(sec_class, 0);	
fail_after_misc_reg:
	misc_deregister(&sec_gps_device);
fail_after_regulator_get:
	regulator_put(gps_lna);

	return ret;
}

static void __exit sec_gps_exit(void)
{
	device_remove_file(sec_gps_dev, &dev_attr_pwr_en);
	device_remove_file(sec_gps_dev, &dev_attr_nrst);

	device_destroy(sec_class, 0);

	misc_deregister(&sec_gps_device);
	regulator_put(gps_lna);
}

module_init(sec_gps_init);
module_exit(sec_gps_exit);

/* Module information */
MODULE_AUTHOR("Samsung");
MODULE_DESCRIPTION("GPS power control driver");
MODULE_LICENSE("GPL");

