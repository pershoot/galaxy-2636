/* linux/driver/input/misc/kxsd9.c
 * Copyright (C) 2010 Samsung Electronics. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
/* #define DEBUG */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/input.h>

#define READ_REPEAT_SHIFT	0
#define READ_REPEAT		(1 << READ_REPEAT_SHIFT)

/* kxsd9 register address */
#define kxsd9_XOUT_MSB           0x00
#define kxsd9_XOUT_LSB           0x01
#define kxsd9_YOUT_MSB           0x02
#define kxsd9_YOUT_LSB           0x03
#define kxsd9_ZOUT_MSB           0x04
#define kxsd9_ZOUT_LSB           0x05

#define kxsd9_AUXOUT_MSB         0x06
#define kxsd9_AUXOUT_LSB         0x07

#define kxsd9_RESET              0x0a /* write only */
#define kxsd9_CTRL_REGC          0x0c /* reset/default value 0xe1 */
#define kxsd9_CTRL_REGB          0x0d /* reset/default value 0x40 */
#define kxsd9_CTRL_REGA          0x0e /* read only */

/* full scale range values in ctrl_c reg */
#define kxsd9_FS_8		0x00  /* +/- 8G, 12-bit, 205 counts/g */
#define kxsd9_FS_6		0x01  /* +/- 6G, 12-bit, 273 counts/g */
#define kxsd9_FS_4		0x02  /* +/- 4G, 12-bit, 410 counts/g */
#define kxsd9_FS_2		0x03  /* +/- 2G, 12-bit, 819 counts/g */
#define kxsd9_FS_MASK		0x03

/* operational bandwidth values in ctrl_c reg */
#define kxsd9_LP_None		0x00
#define kxsd9_LP_2000_HZ	0x60 /* so is 0x20 and 0x40 */
#define kxsd9_LP_1000_HZ	0x80
#define kxsd9_LP_500_HZ		0xa0
#define kxsd9_LP_100_HZ		0xc0
#define kxsd9_LP_50_HZ		0xe0
#define kxsd9_LP_MASK		0xe0

/* write this value to reset reg to reload sensitivity and temperature
 * correction values
 */
#define kxsd9_Reset_Key_Value      0xca

/* kxsd9 register address info ends here*/

#define kxsd9_dbgmsg(str, args...) pr_debug("%s: " str, __func__, ##args)

struct kxsd9_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct hrtimer timer;
	struct work_struct work;
	struct workqueue_struct *wq;
	ktime_t poll_delay;
	struct mutex lock;
	bool enabled;
};

static int kxsd9_enable(struct kxsd9_data *kxsd9)
{
	int err;
	err = i2c_smbus_write_byte_data(kxsd9->client, kxsd9_CTRL_REGB, 0x40);
	pr_info("%s: enabling and starting poll timer, delay %lldns\n",
		__func__, ktime_to_ns(kxsd9->poll_delay));
	hrtimer_start(&kxsd9->timer, kxsd9->poll_delay, HRTIMER_MODE_REL);
	return err;
}

static int kxsd9_disable(struct kxsd9_data *kxsd9)
{
	pr_info("%s: disabling and cancelling poll timer\n", __func__);
	hrtimer_cancel(&kxsd9->timer);
	cancel_work_sync(&kxsd9->work);
	return i2c_smbus_write_byte_data(kxsd9->client, kxsd9_CTRL_REGB, 0);
}

static void kxsd9_work_func(struct work_struct *work)
{
	struct kxsd9_data *kxsd9 = container_of(work, struct kxsd9_data, work);
	int err;
	u8 acc_data[6];
	s16 x, y, z;

	err = i2c_smbus_read_i2c_block_data(kxsd9->client, kxsd9_XOUT_MSB,
					    sizeof(acc_data), acc_data);
	if (err != sizeof(acc_data)) {
		pr_err("%s : failed to read %d bytes for getting x/y/z\n",
			__func__, sizeof(acc_data));
		return;
	}

	/* the values read aren't 2's complement but
	 * 0-4095 with 2048 being really 0, so adjust
	 */
	x = ((acc_data[0] << 4) | (acc_data[1] >> 4)) - 2048;
	y = ((acc_data[2] << 4) | (acc_data[3] >> 4)) - 2048;
	z = ((acc_data[4] << 4) | (acc_data[5] >> 4)) - 2048;

	kxsd9_dbgmsg("%s: x = %d, y = %d, z = %d\n", __func__, x, y, z);

	input_report_rel(kxsd9->input_dev, REL_X, x);
	input_report_rel(kxsd9->input_dev, REL_Y, y);
	input_report_rel(kxsd9->input_dev, REL_Z, z);
	input_sync(kxsd9->input_dev);
}

/* This function is the timer function that runs on the configured poll_delay.
 * It just starts a thread to do the i2c read of the latest acc value
 * and delivers it via a input device.
 */
static enum hrtimer_restart kxsd9_timer_func(struct hrtimer *timer)
{
	struct kxsd9_data *kxsd9 = container_of(timer, struct kxsd9_data,
						timer);
	queue_work(kxsd9->wq, &kxsd9->work);
	hrtimer_forward_now(&kxsd9->timer, kxsd9->poll_delay);
	return HRTIMER_RESTART;
}

static ssize_t kxsd9_show_enable(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct kxsd9_data *kxsd9  = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", kxsd9->enabled);
}

static ssize_t kxsd9_set_enable(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct kxsd9_data *kxsd9  = dev_get_drvdata(dev);
	bool new_enable;

	if (sysfs_streq(buf, "1"))
		new_enable = true;
	else if (sysfs_streq(buf, "0"))
		new_enable = false;
	else {
		pr_err("%s: invalid value %d\n", __func__, *buf);
		return -EINVAL;
	}

	if (new_enable == kxsd9->enabled)
		return size;

	mutex_lock(&kxsd9->lock);
	if (new_enable)
		kxsd9_enable(kxsd9);
	else
		kxsd9_disable(kxsd9);
	kxsd9->enabled = new_enable;

	mutex_unlock(&kxsd9->lock);

	return size;
}

static ssize_t kxsd9_show_delay(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct kxsd9_data *kxsd9  = dev_get_drvdata(dev);
	return sprintf(buf, "%lld\n", ktime_to_ns(kxsd9->poll_delay));
}

static ssize_t kxsd9_set_delay(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct kxsd9_data *kxsd9  = dev_get_drvdata(dev);
	u64 delay_ns;
	int res;

	res = strict_strtoll(buf, 10, &delay_ns);
	if (res < 0)
		return res;

	mutex_lock(&kxsd9->lock);
	if (delay_ns != ktime_to_ns(kxsd9->poll_delay)) {
		kxsd9->poll_delay = ns_to_ktime(delay_ns);
		if (kxsd9->enabled) {
			kxsd9_disable(kxsd9);
			kxsd9_enable(kxsd9);
		}
	}
	mutex_unlock(&kxsd9->lock);

	return size;
}

static DEVICE_ATTR(enable, S_IRUGO | S_IWUSR | S_IWGRP,
			kxsd9_show_enable, kxsd9_set_enable);
static DEVICE_ATTR(poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
			kxsd9_show_delay, kxsd9_set_delay);

static int kxsd9_suspend(struct device *dev)
{
	int res = 0;
	struct kxsd9_data *kxsd9  = dev_get_drvdata(dev);

	if (kxsd9->enabled)
		res = kxsd9_disable(kxsd9);

	return res;
}

static int kxsd9_resume(struct device *dev)
{
	int res = 0;
	struct kxsd9_data *kxsd9 = dev_get_drvdata(dev);

	if (kxsd9->enabled)
		res = kxsd9_enable(kxsd9);

	return res;
}


static const struct dev_pm_ops kxsd9_pm_ops = {
	.suspend = kxsd9_suspend,
	.resume = kxsd9_resume,
};

static int kxsd9_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct kxsd9_data *kxsd9;
	struct input_dev *input_dev;
	int err;

	pr_info("%s: start\n", __func__);
	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_WRITE_BYTE_DATA |
				     I2C_FUNC_SMBUS_READ_I2C_BLOCK)) {
		pr_err("%s: i2c functionality check failed!\n", __func__);
		err = -ENODEV;
		goto exit;
	}

	kxsd9 = kzalloc(sizeof(*kxsd9), GFP_KERNEL);
	if (kxsd9 == NULL) {
		dev_err(&client->dev,
				"failed to allocate memory for module data\n");
		err = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(client, kxsd9);
	kxsd9->client = client;
	mutex_init(&kxsd9->lock);

	/* hrtimer settings.  we poll for acc values using a timer
	 * who's frequency can e set via sysfs
	 */
	hrtimer_init(&kxsd9->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	kxsd9->poll_delay = ns_to_ktime(200 * NSEC_PER_MSEC);
	kxsd9->timer.function = kxsd9_timer_func;

	/* the timer just fires off a work queue request.  we need a thread
	 * to read the data and provide it to the input_dev
	 */
	kxsd9->wq = create_singlethread_workqueue("kxsd9_wq");
	if (!kxsd9->wq) {
		err = -ENOMEM;
		pr_err("%s: could not create workqueue\n", __func__);
		goto exit_create_workqueue_failed;
	}
	/* this is the thread function we run on the work queue */
	INIT_WORK(&kxsd9->work, kxsd9_work_func);

	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		dev_err(&client->dev,
			"input device allocate failed\n");
		goto exit_input_dev_alloc_failed;
	}

	kxsd9->input_dev = input_dev;
	input_set_drvdata(input_dev, kxsd9);
	input_dev->name = "accelerometer";

	input_set_capability(input_dev, EV_REL, REL_X);
	input_set_abs_params(input_dev, REL_X, -2048, 2047, 0, 0);
	input_set_capability(input_dev, EV_REL, REL_Y);
	input_set_abs_params(input_dev, REL_Y, -2048, 2047, 0, 0);
	input_set_capability(input_dev, EV_REL, REL_Z);
	input_set_abs_params(input_dev, REL_Z, -2048, 2047, 0, 0);

	err = input_register_device(input_dev);
	if (err) {
		pr_err("%s: Unable to register input device: %s\n",
			__func__, input_dev->name);
		input_free_device(input_dev);
		goto exit_input_register_device_failed;
	}

	if (device_create_file(&input_dev->dev,
				&dev_attr_enable) < 0) {
		pr_err("Failed to create device file(%s)!\n",
				dev_attr_enable.attr.name);
		goto exit_device_create_file;
	}

	if (device_create_file(&input_dev->dev,
				&dev_attr_poll_delay) < 0) {
		pr_err("Failed to create device file(%s)!\n",
				dev_attr_poll_delay.attr.name);
		goto exit_device_create_file2;
	}

	/* set full scale range to +/- 2g, no filter for now */
	err = i2c_smbus_write_byte_data(client,
					kxsd9_CTRL_REGC,
					kxsd9_FS_2 | kxsd9_LP_100_HZ);
	if (err)
		pr_err("%s: set range failed\n", __func__);

	pr_info("%s: returning 0\n", __func__);
	return 0;

exit_device_create_file2:
	device_remove_file(&input_dev->dev, &dev_attr_enable);
exit_device_create_file:
	input_unregister_device(input_dev);
exit_input_register_device_failed:
exit_input_dev_alloc_failed:
	destroy_workqueue(kxsd9->wq);
exit_create_workqueue_failed:
	mutex_destroy(&kxsd9->lock);
	kfree(kxsd9);
exit:
	return err;
}

static int __devexit kxsd9_remove(struct i2c_client *client)
{
	struct kxsd9_data *kxsd9 = i2c_get_clientdata(client);

	if (kxsd9->enabled)
		kxsd9_disable(kxsd9);

	device_remove_file(&kxsd9->input_dev->dev, &dev_attr_enable);
	device_remove_file(&kxsd9->input_dev->dev, &dev_attr_poll_delay);
	input_unregister_device(kxsd9->input_dev);
	destroy_workqueue(kxsd9->wq);
	mutex_destroy(&kxsd9->lock);
	kfree(kxsd9);

	return 0;
}

static const struct i2c_device_id kxsd9_id[] = {
	{ "kxsd9", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, kxsd9_id);

static struct i2c_driver kxsd9_driver = {
	.probe = kxsd9_probe,
	.remove = __devexit_p(kxsd9_remove),
	.id_table = kxsd9_id,
	.driver = {
		.pm = &kxsd9_pm_ops,
		.owner = THIS_MODULE,
		.name = "kxsd9",
	},
};

static int __init kxsd9_init(void)
{
	return i2c_add_driver(&kxsd9_driver);
}

static void __exit kxsd9_exit(void)
{
	i2c_del_driver(&kxsd9_driver);
}

module_init(kxsd9_init);
module_exit(kxsd9_exit);

MODULE_DESCRIPTION("kxsd9 accelerometer driver");
MODULE_AUTHOR("Mike J. Chen Samsung Electronics <mjchen@sta.samsung.com>");
MODULE_LICENSE("GPL");
