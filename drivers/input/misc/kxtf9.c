/* linux/driver/input/misc/kxtf9.c
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

/* kxtf9 register address */
#define kxtf9_XOUT_HPF_LSB       0x00
#define kxtf9_XOUT_HPF_MSB       0x01
#define kxtf9_YOUT_HPF_LSB       0x02
#define kxtf9_YOUT_HPF_MSB       0x03
#define kxtf9_ZOUT_HPF_LSB       0x04
#define kxtf9_ZOUT_HPF_MSB       0x05

#define kxtf9_XOUT_LSB           0x06
#define kxtf9_XOUT_MSB           0x07
#define kxtf9_YOUT_LSB           0x08
#define kxtf9_YOUT_MSB           0x09
#define kxtf9_ZOUT_LSB           0x0A
#define kxtf9_ZOUT_MSB           0x0B

#define kxtf9_DCST_RESP          0x0C
#define kxtf9_WHO_AM_I           0x0F
#define kxtf9_TILT_POS_CUR       0x10
#define kxtf9_TILT_POS_PRE       0x11

#define kxtf9_INT_SRC_REG1       0x15
#define kxtf9_INT_SRC_REG2       0x16

#define kxtf9_STATUS_REG         0x18

#define kxtf9_INT_REL            0x1A

#define kxtf9_CTRL_REG1          0x1B
#define kxtf9_CTRL_REG2          0x1C
#define kxtf9_CTRL_REG3          0x1D
#define kxtf9_INT_CTRL_REG1      0x1E
#define kxtf9_INT_CTRL_REG2      0x1F
#define kxtf9_INT_CTRL_REG3      0x20
#define kxtf9_DATA_CTRL_REG      0x21

#define kxtf9_TILT_TIMER         0x28
#define kxtf9_WUF_TIMER          0x29
#define kxtf9_TDT_TIMER          0x2B

/* Acceleration scale range values in CTRL_REG1 reg */
#define kxtf9_GSE_8		(2<<3)  /* +/- 8G */
#define kxtf9_GSE_4		(1<<3)  /* +/- 4G */
#define kxtf9_GSE_2		(0<<3)  /* +/- 2G */
#define kxtf9_GSE_MASK		(3<<3)

/* Output data rate values in CTRL_REG3 reg */
#define kxtf9_OWUF_25_HZ	0x00   /* 25Hz */
#define kxtf9_OWUF_50_HZ	0x01   /* 50Hz */
#define kxtf9_OWUF_100_HZ	0x02   /* 100Hz */
#define kxtf9_OWUF_200_HZ	0x03   /* 200Hz */
#define kxtf9_OWUF_MASK		0x03

/* kxtf9 register address info ends here*/

#define kxtf9_dbgmsg(str, args...) pr_debug("%s: " str, __func__, ##args)

struct kxtf9_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct hrtimer timer;
	struct work_struct work;
	struct workqueue_struct *wq;
	ktime_t poll_delay;
	struct mutex lock;
	bool enabled;
};

static int kxtf9_enable(struct kxtf9_data *kxtf9)
{
	int err;
	err = i2c_smbus_write_byte_data(kxtf9->client, kxtf9_CTRL_REG1, 0xC2);
	pr_info("%s: enabling and starting poll timer, delay %lldns\n",
		__func__, ktime_to_ns(kxtf9->poll_delay));
	hrtimer_start(&kxtf9->timer, kxtf9->poll_delay, HRTIMER_MODE_REL);
	return err;
}

static int kxtf9_disable(struct kxtf9_data *kxtf9)
{
	pr_info("%s: disabling and cancelling poll timer\n", __func__);
	hrtimer_cancel(&kxtf9->timer);
	cancel_work_sync(&kxtf9->work);
	return i2c_smbus_write_byte_data(kxtf9->client, kxtf9_CTRL_REG1, 0);
}

static void kxtf9_work_func(struct work_struct *work)
{
	struct kxtf9_data *kxtf9 = container_of(work, struct kxtf9_data, work);
	int err;
	u8 acc_data[6];
	s16 x, y, z;
	s32 temp;

	err = i2c_smbus_read_i2c_block_data(kxtf9->client, kxtf9_XOUT_LSB,
					    sizeof(acc_data), acc_data);
	if (err != sizeof(acc_data)) {
		pr_err("%s : failed to read %d bytes for getting x/y/z\n",
			__func__, sizeof(acc_data));
		return;
	}

	temp = ((acc_data[1] << 4) | (acc_data[0] >> 4));
	if (temp < 2048)
		x = (s16)(2000 * temp / 2048);
	else 
		x = (s16)(-2000 * (4096 - temp) / 2048);

	temp = ((acc_data[3] << 4) | (acc_data[2] >> 4));
	if (temp < 2048)
		y = (s16)(2000 * temp / 2048);
	else 
		y = (s16)(-2000 * (4096 - temp) / 2048);

	temp = ((acc_data[5] << 4) | (acc_data[4] >> 4));
	if (temp < 2048)
		z = (s16)(2000 * temp / 2048);
	else 
		z = (s16)(-2000 * (4096 - temp) / 2048);

	kxtf9_dbgmsg("%s: x = %d, y = %d, z = %d\n", __func__, x, y, z);

	input_report_rel(kxtf9->input_dev, REL_X, x);
	input_report_rel(kxtf9->input_dev, REL_Y, y);
	input_report_rel(kxtf9->input_dev, REL_Z, z);
	input_sync(kxtf9->input_dev);
}

/* This function is the timer function that runs on the configured poll_delay.
 * It just starts a thread to do the i2c read of the latest acc value
 * and delivers it via a input device.
 */
static enum hrtimer_restart kxtf9_timer_func(struct hrtimer *timer)
{
	struct kxtf9_data *kxtf9 = container_of(timer, struct kxtf9_data,
						timer);
	queue_work(kxtf9->wq, &kxtf9->work);
	hrtimer_forward_now(&kxtf9->timer, kxtf9->poll_delay);
	return HRTIMER_RESTART;
}

static ssize_t kxtf9_show_enable(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct kxtf9_data *kxtf9 = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", kxtf9->enabled);
}

static ssize_t kxtf9_set_enable(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct kxtf9_data *kxtf9 = dev_get_drvdata(dev);
	bool new_enable;

	if (sysfs_streq(buf, "1"))
		new_enable = true;
	else if (sysfs_streq(buf, "0"))
		new_enable = false;
	else {
		pr_err("%s: invalid value %d\n", __func__, *buf);
		return -EINVAL;
	}

	if (new_enable == kxtf9->enabled)
		return size;

	mutex_lock(&kxtf9->lock);
	if (new_enable)
		kxtf9_enable(kxtf9);
	else
		kxtf9_disable(kxtf9);
	kxtf9->enabled = new_enable;

	mutex_unlock(&kxtf9->lock);

	return size;
}

static ssize_t kxtf9_show_delay(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct kxtf9_data *kxtf9 = dev_get_drvdata(dev);
	return sprintf(buf, "%lld\n", ktime_to_ns(kxtf9->poll_delay));
}

static ssize_t kxtf9_set_delay(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct kxtf9_data *kxtf9 = dev_get_drvdata(dev);
	u64 delay_ns;
	int res;

	res = strict_strtoll(buf, 10, &delay_ns);
	if (res < 0)
		return res;

	mutex_lock(&kxtf9->lock);
	if (delay_ns != ktime_to_ns(kxtf9->poll_delay)) {
		kxtf9->poll_delay = ns_to_ktime(delay_ns);
		if (kxtf9->enabled) {
			kxtf9_disable(kxtf9);
			kxtf9_enable(kxtf9);
		}
	}
	mutex_unlock(&kxtf9->lock);

	return size;
}

static DEVICE_ATTR(enable, S_IRUGO | S_IWUSR | S_IWGRP,
			kxtf9_show_enable, kxtf9_set_enable);
static DEVICE_ATTR(poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
			kxtf9_show_delay, kxtf9_set_delay);

static int kxtf9_suspend(struct device *dev)
{
	int res = 0;
	struct kxtf9_data *kxtf9 = dev_get_drvdata(dev);

	if (kxtf9->enabled)
		res = kxtf9_disable(kxtf9);

	return res;
}

static int kxtf9_resume(struct device *dev)
{
	int res = 0;
	struct kxtf9_data *kxtf9 = dev_get_drvdata(dev);

	if (kxtf9->enabled)
		res = kxtf9_enable(kxtf9);

	return res;
}


static const struct dev_pm_ops kxtf9_pm_ops = {
	.suspend = kxtf9_suspend,
	.resume = kxtf9_resume,
};

static int kxtf9_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct kxtf9_data *kxtf9;
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

	kxtf9 = kzalloc(sizeof(*kxtf9), GFP_KERNEL);
	if (kxtf9 == NULL) {
		dev_err(&client->dev,
				"failed to allocate memory for module data\n");
		err = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(client, kxtf9);
	kxtf9->client = client;
	mutex_init(&kxtf9->lock);

	/* hrtimer settings.  we poll for acc values using a timer
	 * who's frequency can e set via sysfs
	 */
	hrtimer_init(&kxtf9->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	kxtf9->poll_delay = ns_to_ktime(200 * NSEC_PER_MSEC);
	kxtf9->timer.function = kxtf9_timer_func;

	/* the timer just fires off a work queue request.  we need a thread
	 * to read the data and provide it to the input_dev
	 */
	kxtf9->wq = create_singlethread_workqueue("kxtf9_wq");
	if (!kxtf9->wq) {
		err = -ENOMEM;
		pr_err("%s: could not create workqueue\n", __func__);
		goto exit_create_workqueue_failed;
	}
	/* this is the thread function we run on the work queue */
	INIT_WORK(&kxtf9->work, kxtf9_work_func);

	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		dev_err(&client->dev,
			"input device allocate failed\n");
		goto exit_input_dev_alloc_failed;
	}

	kxtf9->input_dev = input_dev;
	input_set_drvdata(input_dev, kxtf9);
	input_dev->name = "accelerometer_sensor";

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

	/* The sensor must be first disabled to configure it. */
	err = i2c_smbus_write_byte_data(client, kxtf9_CTRL_REG1, 0);
	if (err)
		pr_err("%s: set range failed\n", __func__);

	err = i2c_smbus_write_byte_data(client, kxtf9_CTRL_REG2, 0);
	if (err)
		pr_err("%s: set range failed\n", __func__);

	err = i2c_smbus_write_byte_data(client, kxtf9_CTRL_REG3,
			kxtf9_OWUF_100_HZ);
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
	destroy_workqueue(kxtf9->wq);
exit_create_workqueue_failed:
	mutex_destroy(&kxtf9->lock);
	kfree(kxtf9);
exit:
	return err;
}

static int __devexit kxtf9_remove(struct i2c_client *client)
{
	struct kxtf9_data *kxtf9 = i2c_get_clientdata(client);

	if (kxtf9->enabled)
		kxtf9_disable(kxtf9);

	device_remove_file(&kxtf9->input_dev->dev, &dev_attr_enable);
	device_remove_file(&kxtf9->input_dev->dev, &dev_attr_poll_delay);
	input_unregister_device(kxtf9->input_dev);
	destroy_workqueue(kxtf9->wq);
	mutex_destroy(&kxtf9->lock);
	kfree(kxtf9);

	return 0;
}

static const struct i2c_device_id kxtf9_id[] = {
	{ "kxtf9", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, kxtf9_id);

static struct i2c_driver kxtf9_driver = {
	.probe = kxtf9_probe,
	.remove = __devexit_p(kxtf9_remove),
	.id_table = kxtf9_id,
	.driver = {
		.pm = &kxtf9_pm_ops,
		.owner = THIS_MODULE,
		.name = "kxtf9",
	},
};

static int __init kxtf9_init(void)
{
	return i2c_add_driver(&kxtf9_driver);
}

static void __exit kxtf9_exit(void)
{
	i2c_del_driver(&kxtf9_driver);
}

module_init(kxtf9_init);
module_exit(kxtf9_exit);

MODULE_DESCRIPTION("kxtf9 accelerometer driver");
MODULE_AUTHOR("Adam Hampson Samsung Electronics <ahampson@sta.samsung.com>");
MODULE_LICENSE("GPL");
