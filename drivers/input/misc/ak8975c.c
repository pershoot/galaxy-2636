/* drivers/input/misc/ak8975c.c - ak8975 magnetometer driver
 *
 * Copyright (C) 2007-2008 HTC Corporation.
 * Copyright (C) 2010 Samsung Electronics Corporation.
 * Author: Hou-Kun Chen <houkun.chen@gmail.com>
 * Author: Mike J. Chen <mjchen@sta.samsung.com>
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

/*
 * Revised by AKM 2009/04/02
 * Revised by Motorola 2010/05/27
 * Revised by Samsung 2010/12/13 - uses a timer that runs at user specified
 *    poll interval.  timer triggers a single threaded workqueue.  the thread
 *    starts ADC sample and waits for completion.  interrupt triggers the
 *    completion to let the thread continue.  thread then reports data via
 *    input framework.
 *
 */
/*#define DEBUG*/
#define FACTORY_TEST
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/input.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif


#define AK8975_MODE_MEASURE		0x00	/* Starts measurement. */
#define AK8975_MODE_E2P_READ		0x02	/* E2P access mode (read). */
#define AK8975_MODE_POWERDOWN		0x03	/* Power down mode */

#define MAX_CONVERSION_TRIAL            5
#define MAX_CONVERION_TIMEOUT           500
#define CONVERSION_DONE_POLL_TIME       10

#define AK8975_REG_WIA			0x00
#define MM_AK8975_DEVICE_ID		0x48
#define AK8975_REG_INFO			0x01

#define AK8975_REG_ST1			0x02
#define REG_ST1_DRDY_SHIFT              0
#define REG_ST1_DRDY_MASK               (1 << REG_ST1_DRDY_SHIFT)

#define AK8975_REG_HXL			0x03
#define AK8975_REG_HXH			0x04
#define AK8975_REG_HYL			0x05
#define AK8975_REG_HYH			0x06
#define AK8975_REG_HZL			0x07
#define AK8975_REG_HZH			0x08
#define AK8975_REG_ST2			0x09
#define REG_ST2_DERR_SHIFT              2
#define REG_ST2_DERR_MASK               (1 << REG_ST2_DERR_SHIFT)

#define REG_ST2_HOFL_SHIFT              3
#define REG_ST2_HOFL_MASK               (1 << REG_ST2_HOFL_SHIFT)

#define AK8975_REG_CNTL			0x0A
#define REG_CNTL_MODE_SHIFT             0
#define REG_CNTL_MODE_MASK              (0xF << REG_CNTL_MODE_SHIFT)
#define REG_CNTL_MODE_POWER_DOWN        0
#define REG_CNTL_MODE_ONCE		0x01
#define REG_CNTL_MODE_SELF_TEST         0x08
#define REG_CNTL_MODE_FUSE_ROM          0x0F

#define AK8975_REG_RSVC			0x0B
#define AK8975_REG_ASTC			0x0C
#define AK8975_REG_TS1			0x0D
#define AK8975_REG_TS2			0x0E
#define AK8975_REG_I2CDIS		0x0F
#define AK8975_REG_ASAX			0x10
#define AK8975_REG_ASAY			0x11
#define AK8975_REG_ASAZ			0x12


struct ak8975c_data {
	struct i2c_client *this_client;
	struct input_dev *input_dev;
	struct work_struct work;
	struct hrtimer timer;
	struct completion data_ready;
	struct mutex lock;
	struct workqueue_struct *wq;
	u8 asa[3];
	ktime_t poll_delay;
	bool enabled;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
};

static void ak8975c_disable_irq(struct ak8975c_data *ak_data)
{
	disable_irq(ak_data->this_client->irq);
	if (try_wait_for_completion(&ak_data->data_ready)) {
		/* we actually got the interrupt before we could disable it
		 * so we need to enable again to undo our disable since the
		 * irq_handler already disabled it
		 */
		enable_irq(ak_data->this_client->irq);
	}
}

static irqreturn_t ak8975c_irq_handler(int irq, void *ak8975c_data_p)
{
	struct ak8975c_data *ak_data = ak8975c_data_p;
	disable_irq_nosync(irq);
	complete(&ak_data->data_ready);
	return IRQ_HANDLED;
}

static int ak8975c_wait_for_data_ready(struct ak8975c_data *ak_data)
{
	int err;

	enable_irq(ak_data->this_client->irq);

	err = wait_for_completion_timeout(&ak_data->data_ready, 5*HZ);
	if (err > 0)
		return 0;

	ak8975c_disable_irq(ak_data);

	if (err == 0) {
		pr_err("%s: wait timed out\n", __func__);
		return -ETIMEDOUT;
	}

	pr_err("%s: wait restart\n", __func__);
	return err;
}


static void ak8975c_enable(struct ak8975c_data *ak_data)
{
	pr_debug("%s: starting poll timer, delay %lldns\n",
		__func__, ktime_to_ns(ak_data->poll_delay));
	hrtimer_start(&ak_data->timer, ak_data->poll_delay, HRTIMER_MODE_REL);
}

static void ak8975c_disable(struct ak8975c_data *ak_data)
{
	pr_debug("%s: cancelling poll timer\n", __func__);
	hrtimer_cancel(&ak_data->timer);
	cancel_work_sync(&ak_data->work);
}

static enum hrtimer_restart ak8975c_timer_func(struct hrtimer *timer)
{
	struct ak8975c_data *ak_data;

	ak_data = container_of(timer, struct ak8975c_data, timer);
	queue_work(ak_data->wq, &ak_data->work);
	hrtimer_forward_now(&ak_data->timer, ak_data->poll_delay);
	return HRTIMER_RESTART;
}

static ssize_t ak8975c_show_enable(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct ak8975c_data *ak_data  = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", ak_data->enabled);
}

static ssize_t ak8975c_set_enable(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct ak8975c_data *ak_data  = dev_get_drvdata(dev);
	bool new_enable;

	if (sysfs_streq(buf, "1"))
		new_enable = true;
	else if (sysfs_streq(buf, "0"))
		new_enable = false;
	else {
		pr_debug("%s: invalid value %d\n", __func__, *buf);
		return -EINVAL;
	}

	if (new_enable == ak_data->enabled)
		return size;

	mutex_lock(&ak_data->lock);
	if (new_enable)
		ak8975c_enable(ak_data);
	else
		ak8975c_disable(ak_data);
	ak_data->enabled = new_enable;

	mutex_unlock(&ak_data->lock);

	return size;
}

static ssize_t ak8975c_show_delay(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct ak8975c_data *ak_data  = dev_get_drvdata(dev);
	return sprintf(buf, "%lld\n", ktime_to_ns(ak_data->poll_delay));
}

static ssize_t ak8975c_set_delay(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct ak8975c_data *ak_data  = dev_get_drvdata(dev);
	u64 delay_ns;
	int res;

	res = strict_strtoll(buf, 10, &delay_ns);
	if (res < 0)
		return res;

	mutex_lock(&ak_data->lock);
	if (delay_ns != ktime_to_ns(ak_data->poll_delay)) {
		ak_data->poll_delay = ns_to_ktime(delay_ns);
		if (ak_data->enabled) {
			ak8975c_disable(ak_data);
			ak8975c_enable(ak_data);
		}
	}
	mutex_unlock(&ak_data->lock);

	return size;
}

static DEVICE_ATTR(enable, S_IRUGO | S_IWUSR | S_IWGRP,
			ak8975c_show_enable, ak8975c_set_enable);
static DEVICE_ATTR(poll_delay, S_IRUGO | S_IWUSR | S_IWGRP,
			ak8975c_show_delay, ak8975c_set_delay);

#if (defined DEBUG) || (defined FACTORY_TEST)
#ifdef FACTORY_TEST
static bool ak8975_selftest_passed;
static s16 sf_x, sf_y, sf_z;
#endif

static void ak8975c_selftest(struct ak8975c_data *ak_data)
{
	int err;
	u8 buf[6];
	s16 x, y, z;

	/* read device info */
	err = i2c_smbus_read_i2c_block_data(ak_data->this_client,
					AK8975_REG_WIA, 2, buf);
	pr_info("%s: device id = 0x%x, info = 0x%x\n",
		__func__, buf[0], buf[1]);

	/* set ATSC self test bit to 1 */
	err = i2c_smbus_write_byte_data(ak_data->this_client,
					AK8975_REG_ASTC, 0x40);

	/* start self test */
	err = i2c_smbus_write_byte_data(ak_data->this_client,
					AK8975_REG_CNTL,
					REG_CNTL_MODE_SELF_TEST);

	/* wait for data ready */
	while (1) {
		msleep(20);
		if (i2c_smbus_read_byte_data(ak_data->this_client,
						AK8975_REG_ST1) == 1) {
			break;
		}
	}

	err = i2c_smbus_read_i2c_block_data(ak_data->this_client,
					AK8975_REG_HXL, sizeof(buf), buf);

	/* set ATSC self test bit to 0 */
	err = i2c_smbus_write_byte_data(ak_data->this_client,
					AK8975_REG_ASTC, 0x00);

	x = buf[0] | (buf[1] << 8);
	y = buf[2] | (buf[3] << 8);
	z = buf[4] | (buf[5] << 8);

	/* Hadj = (H*(Asa+128))/256 */
	x = (x*(ak_data->asa[0] + 128)) >> 8;
	y = (y*(ak_data->asa[1] + 128)) >> 8;
	z = (z*(ak_data->asa[2] + 128)) >> 8;

	pr_info("%s: self test x = %d, y = %d, z = %d\n",
		__func__, x, y, z);
	if ((x >= -100) && (x <= 100))
		pr_info("%s: x passed self test, expect -100<=x<=100\n",
			__func__);
	else
		pr_info("%s: x failed self test, expect -100<=x<=100\n",
			__func__);
	if ((y >= -100) && (y <= 100))
		pr_info("%s: y passed self test, expect -100<=y<=100\n",
			__func__);
	else
		pr_info("%s: y failed self test, expect -100<=y<=100\n",
			__func__);
	if ((z >= -1000) && (z <= -300))
		pr_info("%s: z passed self test, expect -1000<=z<=-300\n",
			__func__);
	else
		pr_info("%s: z failed self test, expect -1000<=z<=-300\n",
			__func__);

#ifdef FACTORY_TEST
	if (((x >= -100) && (x <= 100)) && ((y >= -100) && (y <= 100)) &&
	    ((z >= -1000) && (z <= -300)))
		ak8975_selftest_passed = 1;

	sf_x = x;
	sf_y = y;
	sf_z = z;
#endif
}
#endif

static void ak8975c_work_func(struct work_struct *data)
{
	struct ak8975c_data *ak_data;
	u8 buf[8];
	int err;
	s16 x, y, z;

	ak_data = container_of(data, struct ak8975c_data, work);

	/* start ADC conversion */
	err = i2c_smbus_write_byte_data(ak_data->this_client,
					AK8975_REG_CNTL, REG_CNTL_MODE_ONCE);

	/* wait for ADC conversion to complete */
	err = ak8975c_wait_for_data_ready(ak_data);
	if (err) {
		pr_err("%s: wait for data ready failed\n", __func__);
		return;
	}

	/* get the value and report it */
	err = i2c_smbus_read_i2c_block_data(ak_data->this_client,
					AK8975_REG_ST1, sizeof(buf), buf);
	if (err != sizeof(buf)) {
		pr_err("%s: read data over i2c failed\n", __func__);
		return;
	}

	/* buf[0] is status1, buf[7] is status2 */
	if (buf[0] == 0) {
		pr_err("%s: status not ready, unexpected\n", __func__);
		return;
	}
	if (buf[7]) {
		pr_err("%s: status error 0x%x, unexpected\n", __func__, buf[7]);
		return;
	}
	x = buf[1] | (buf[2] << 8);
	y = buf[3] | (buf[4] << 8);
	z = buf[5] | (buf[6] << 8);

	pr_debug("%s: raw x = %d, y = %d, z = %d\n", __func__, x, y, z);
#if 1
	pr_debug("%s: buf[0], ST1 = 0x%x, buf[7], ST2 = 0x%x\n",
		__func__, buf[0], buf[7]);
	pr_debug("%s: buf[2,1], X = 0x%x,0x%x "
		"buf[4,3], Y = 0x%x,0x%x, buf[6,5], Z = 0x%x,0x%x\n",
		__func__, buf[2], buf[1], buf[4],
		buf[3], buf[6], buf[5]);
#else
	pr_debug("%s: buf[0] = 0x%x, buf[2,1] = 0x%x,0x%x "
		"buf[4,3] = 0x%x,0x%x, buf[6,5] = 0x%x,0x%x, buf[7] = 0x%x\n",
		__func__, buf[0], buf[2], buf[1], buf[4],
		buf[3], buf[6], buf[5], buf[7]);
#endif

	/* Hadj = (H*(Asa+128))/256 */
	x = (x*(ak_data->asa[0] + 128)) >> 8;
	y = (y*(ak_data->asa[1] + 128)) >> 8;
	z = (z*(ak_data->asa[2] + 128)) >> 8;

	pr_debug("%s: adjusted x = %d, y = %d, z = %d\n", __func__, x, y, z);

	input_report_rel(ak_data->input_dev, REL_RX, x);
	input_report_rel(ak_data->input_dev, REL_RY, y);
	input_report_rel(ak_data->input_dev, REL_RZ, z);
	/* use SYN_CONFIG instead of input_sync() so that a EV_SYN event
	 * is still sent to userspace even if all REL data has 0 value,
	 * otherwise the 0 values aren't sent and it's as if there
	 * was a missing event
	 */
	input_event(ak_data->input_dev, EV_SYN, SYN_CONFIG, 0);
}

#ifdef FACTORY_TEST
extern struct class *sec_class;
static struct device *sec_ak8975_dev;

static ssize_t ak8975c_get_asa(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ak8975c_data *ak_data  = dev_get_drvdata(dev);

	return sprintf(buf, "%d, %d, %d\n", ak_data->asa[0], ak_data->asa[1], ak_data->asa[2]);
}

static ssize_t ak8975c_get_selftest(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ak8975c_selftest(dev_get_drvdata(dev));
	return sprintf(buf, "%d, %d, %d, %d\n", ak8975_selftest_passed, sf_x, sf_y, sf_z);
}

static ssize_t ak8975c_check_registers(struct device *dev,
		struct device_attribute *attr, char *strbuf)
{
	struct ak8975c_data *ak_data  = dev_get_drvdata(dev);
	u8 buf[13];
	int err;

	/* power down */
	err = i2c_smbus_write_byte_data(ak_data->this_client,
					AK8975_REG_CNTL, REG_CNTL_MODE_POWER_DOWN);

	/* get the value */
	err = i2c_smbus_read_i2c_block_data(ak_data->this_client,
					AK8975_REG_WIA, 11, buf);

	buf[11] = i2c_smbus_read_byte_data(ak_data->this_client,
					AK8975_REG_ASTC);
	buf[12] = i2c_smbus_read_byte_data(ak_data->this_client,
					AK8975_REG_I2CDIS);


	return sprintf(strbuf, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
			buf[0], buf[1], buf[2], buf[3], buf[4], buf[5],
			buf[6], buf[7], buf[8], buf[9], buf[10], buf[11],
			buf[12]);
}

static ssize_t ak8975c_check_cntl(struct device *dev,
		struct device_attribute *attr, char *strbuf)
{
	struct ak8975c_data *ak_data  = dev_get_drvdata(dev);
	u8 buf;
	int err;

	/* power down */
	err = i2c_smbus_write_byte_data(ak_data->this_client,
					AK8975_REG_CNTL, REG_CNTL_MODE_POWER_DOWN);

	buf = i2c_smbus_read_byte_data(ak_data->this_client,
					AK8975_REG_CNTL);


	return sprintf(strbuf, "%s\n", (!buf ? "OK" : "NG"));
}

static ssize_t ak8975c_get_status(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ak8975c_data *ak_data  = dev_get_drvdata(dev);
	int success;

	if((ak_data->asa[0] == 0) | (ak_data->asa[0] == 0xff) |
		(ak_data->asa[1] == 0) | (ak_data->asa[1] == 0xff) |
			(ak_data->asa[2] == 0) | (ak_data->asa[2] == 0xff)) success = 0;
	else success = 1;
				
	return sprintf(buf, "%s\n", (success ? "OK" : "NG"));
}

static ssize_t ak8975_adc(struct device *dev,
		struct device_attribute *attr, char *strbuf)
{
	struct ak8975c_data *ak_data  = dev_get_drvdata(dev);
	u8 buf[8];
	s16 x, y, z;
	int err, success;

	/* start ADC conversion */
	err = i2c_smbus_write_byte_data(ak_data->this_client,
					AK8975_REG_CNTL, REG_CNTL_MODE_ONCE);

	/* wait for ADC conversion to complete */
	err = ak8975c_wait_for_data_ready(ak_data);
	if (err) {
		pr_err("%s: wait for data ready failed\n", __func__);
		return;
	}

	/* get the value and report it */
	err = i2c_smbus_read_i2c_block_data(ak_data->this_client,
					AK8975_REG_ST1, sizeof(buf), buf);
	if (err != sizeof(buf)) {
		pr_err("%s: read data over i2c failed\n", __func__);
		return;
	}

	/* buf[0] is status1, buf[7] is status2 */
	if ((buf[0] == 0) | (buf[7] == 1)) success = 0;
	else success = 1;

	x = buf[1] | (buf[2] << 8);
	y = buf[3] | (buf[4] << 8);
	z = buf[5] | (buf[6] << 8);

	pr_debug("%s: raw x = %d, y = %d, z = %d\n", __func__, x, y, z);
	
	return sprintf(strbuf, "%s, %d, %d, %d\n", (success ? "OK" : "NG"), x, y, z);
}

static DEVICE_ATTR(ak8975_asa, S_IRUGO,
		ak8975c_get_asa, NULL);
static DEVICE_ATTR(ak8975_selftest, S_IRUGO,
		ak8975c_get_selftest, NULL);
static DEVICE_ATTR(ak8975_chk_registers, S_IRUGO,
		ak8975c_check_registers, NULL);
static DEVICE_ATTR(ak8975_chk_cntl, S_IRUGO,
		ak8975c_check_cntl, NULL);
static DEVICE_ATTR(status, S_IRUGO,
		ak8975c_get_status, NULL);
static DEVICE_ATTR(adc, S_IRUGO,
		ak8975_adc, NULL);

static struct device_attribute *magnetic_sensor_attrs[] = {
	&dev_attr_adc,
	&dev_attr_status,
	NULL,
};

extern struct class *sensors_class;
static struct device *magnetic_sensor_device;
extern int sensors_register(struct device *dev, void * drvdata, struct device_attribute *attributes[], char *name);
#endif


static int ak8975c_internal_suspend(struct ak8975c_data *ak_data)
{
	mutex_lock(&ak_data->lock);
	if (ak_data->enabled)
		ak8975c_disable(ak_data);
	mutex_unlock(&ak_data->lock);

	return 0;
}

static int ak8975c_internal_resume(struct ak8975c_data *ak_data)
{
	mutex_lock(&ak_data->lock);
	if (ak_data->enabled)
		ak8975c_enable(ak_data);
	mutex_unlock(&ak_data->lock);

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
#define ak8975c_suspend	NULL
#define ak8975c_resume		NULL

static void ak8975c_early_suspend(struct early_suspend *h)
{
	struct ak8975c_data *ak_data = container_of(h, struct ak8975c_data,
							early_suspend);
	ak8975c_internal_suspend(ak_data);
}

static void ak8975c_late_resume(struct early_suspend *h)
{
	struct ak8975c_data *ak_data = container_of(h, struct ak8975c_data,
							early_suspend);
	ak8975c_internal_resume(ak_data);
}
#else
static void ak8975c_suspend(struct device *dev)
{
	struct ak8975c_data *ak_data = dev_get_drvdata(dev);
	ak8975c_internal_suspend(ak_data);
}

static void ak8975c_resume(struct device *dev)
{
	struct ak8975c_data *ak_data = dev_get_drvdata(dev);
	ak8975c_internal_resume(ak_data);
}
#endif

int ak8975c_probe(struct i2c_client *client,
		  const struct i2c_device_id *devid)
{
	struct ak8975c_data *ak_data;
	struct input_dev *input_dev;
	int err;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "i2c_check_functionality failed\n");
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}

	ak_data = kzalloc(sizeof(*ak_data), GFP_KERNEL);
	if (!ak_data) {
		dev_err(&client->dev,
			"failed to allocate memory for module data\n");
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}

	i2c_set_clientdata(client, ak_data);

	ak_data->this_client = client;
	mutex_init(&ak_data->lock);
	init_completion(&ak_data->data_ready);

	/* hrtimer settings.  we poll for magnetometer readings
	 * using a fixed frequency timer (user adjustable). */
	hrtimer_init(&ak_data->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ak_data->poll_delay = ns_to_ktime(100 * NSEC_PER_MSEC);
	ak_data->timer.function = ak8975c_timer_func;

	/* the timer just fires off a work queue request.  we need a thread
	 * to write the i2c command to start the ADC sample conversion */
	ak_data->wq = create_singlethread_workqueue("ak8975c_wq");
	if (!ak_data->wq) {
		err = -ENOMEM;
		pr_err("%s: could not create workqueue\n", __func__);
		goto exit_create_workqueue_failed;
	}
	/* this is the thread function we run on the work queue */
	INIT_WORK(&ak_data->work, ak8975c_work_func);

	err = sensors_register(magnetic_sensor_device, ak_data, magnetic_sensor_attrs, "magnetic_sensor");
	if(err) {
		printk(KERN_ERR "%s: cound not register magnetic sensor device(%d).\n", __func__, err);
	}
	
	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		dev_err(&client->dev,
			"input device allocate failed\n");
		goto exit_input_dev_alloc_failed;
	}

	ak_data->input_dev = input_dev;
	input_set_drvdata(input_dev, ak_data);
	input_dev->name = "magnetometer_sensor";

	/* x-axis of raw magnetic vector */
	input_set_capability(input_dev, EV_REL, REL_RX);
	input_set_abs_params(input_dev, REL_RX, -20480, 20479, 0, 0);
	/* y-axis of raw magnetic vector */
	input_set_capability(input_dev, EV_REL, REL_RY);
	input_set_abs_params(input_dev, REL_RY, -20480, 20479, 0, 0);
	/* z-axis of raw magnetic vector */
	input_set_capability(input_dev, EV_REL, REL_RZ);
	input_set_abs_params(input_dev, REL_RZ, -20480, 20479, 0, 0);

	err = input_register_device(input_dev);
	if (err) {
		pr_err("%s: Unable to register input device: %s\n",
			__func__, input_dev->name);
		input_free_device(input_dev);
		goto exit_input_register_device_failed;
	}

	err = request_irq(client->irq, ak8975c_irq_handler,
			IRQF_TRIGGER_HIGH, "ak8975c_int", ak_data);
	if (err < 0) {
		pr_err("%s: can't allocate irq.\n", __func__);
		goto exit_request_irq_failed;
	}
	/* start with interrupt disabled until the driver is enabled */
	disable_irq(client->irq);

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

	/* put into fuse access mode to read asa data */
	err = i2c_smbus_write_byte_data(client, AK8975_REG_CNTL,
					REG_CNTL_MODE_FUSE_ROM);
	if (err)
		pr_err("%s: unable to enter fuse rom mode\n", __func__);

	err = i2c_smbus_read_i2c_block_data(client, AK8975_REG_ASAX,
					sizeof(ak_data->asa), ak_data->asa);
	if (err != sizeof(ak_data->asa))
		pr_err("%s: unable to load factory sensitivity adjust values\n",
			__func__);
	else
		pr_debug("%s: asa_x = %d, asa_y = %d, asa_z = %d\n", __func__,
			ak_data->asa[0], ak_data->asa[1], ak_data->asa[2]);

	err = i2c_smbus_write_byte_data(client, AK8975_REG_CNTL,
					REG_CNTL_MODE_POWER_DOWN);
	if (err) {
		dev_err(&client->dev, "Error in setting power down mode\n");
		goto exit_device_create_file2;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	ak_data->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ak_data->early_suspend.suspend = ak8975c_early_suspend;
	ak_data->early_suspend.resume = ak8975c_late_resume;
	register_early_suspend(&ak_data->early_suspend);
#endif

#ifdef FACTORY_TEST
	sec_ak8975_dev = device_create(sec_class, NULL, 0, ak_data,
			"sec_ak8975");
	if (IS_ERR(sec_ak8975_dev))
		printk("Failed to create device!");

	if (device_create_file(sec_ak8975_dev, &dev_attr_ak8975_asa) < 0) {
		printk("Failed to create device file(%s)! \n",
			dev_attr_ak8975_asa.attr.name);
		goto exit_device_create_file2;
	}
	if (device_create_file(sec_ak8975_dev, &dev_attr_ak8975_selftest) < 0) {
		printk("Failed to create device file(%s)! \n",
			dev_attr_ak8975_selftest.attr.name);
		device_remove_file(&input_dev->dev, &dev_attr_ak8975_asa);
		goto exit_device_create_file2;
	}
	if (device_create_file(sec_ak8975_dev, &dev_attr_ak8975_chk_registers) < 0) {
		printk("Failed to create device file(%s)! \n",
			dev_attr_ak8975_chk_registers.attr.name);
		device_remove_file(&input_dev->dev, &dev_attr_ak8975_asa);
		device_remove_file(&input_dev->dev, &dev_attr_ak8975_selftest);
		goto exit_device_create_file2;
	}
	if (device_create_file(sec_ak8975_dev, &dev_attr_ak8975_chk_cntl) < 0) {
		printk("Failed to create device file(%s)! \n",
			dev_attr_ak8975_chk_cntl.attr.name);
		device_remove_file(&input_dev->dev, &dev_attr_ak8975_asa);
		device_remove_file(&input_dev->dev, &dev_attr_ak8975_selftest);
		device_remove_file(&input_dev->dev, &dev_attr_ak8975_chk_registers);
		goto exit_device_create_file2;
	}
#endif

	return 0;

exit_device_create_file2:
	device_remove_file(&input_dev->dev, &dev_attr_enable);
exit_device_create_file:
	free_irq(client->irq, ak_data);
exit_request_irq_failed:
	input_unregister_device(input_dev);
exit_input_register_device_failed:
	destroy_workqueue(ak_data->wq);
exit_create_workqueue_failed:
	mutex_destroy(&ak_data->lock);
exit_input_dev_alloc_failed:
	kfree(ak_data);
exit_alloc_data_failed:
exit_check_functionality_failed:
	return err;
}

static int __devexit ak8975c_remove(struct i2c_client *client)
{
	struct ak8975c_data *ak_data = i2c_get_clientdata(client);
	device_remove_file(&ak_data->input_dev->dev, &dev_attr_enable);
	device_remove_file(&ak_data->input_dev->dev, &dev_attr_poll_delay);

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ak_data->early_suspend);
#endif
	/* enable irq before free if needed */
	if (!ak_data->enabled)
		enable_irq(client->irq);
	free_irq(client->irq, NULL);
	input_unregister_device(ak_data->input_dev);
	destroy_workqueue(ak_data->wq);
	mutex_destroy(&ak_data->lock);
	kfree(ak_data);
	return 0;
}

static const struct i2c_device_id ak8975c_id[] = {
	{ "ak8975c", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, ak8975c_id);

static const struct dev_pm_ops ak8975c_pm_ops = {
	.suspend = ak8975c_suspend,
	.resume  = ak8975c_resume,
};

static struct i2c_driver ak8975c_driver = {
	.probe = ak8975c_probe,
	.remove = ak8975c_remove,
	.id_table = ak8975c_id,
	.driver = {
		.name = "ak8975c",
		.pm = &ak8975c_pm_ops,
	},
};

static int __init ak8975c_init(void)
{
	return i2c_add_driver(&ak8975c_driver);
}

static void __exit ak8975c_exit(void)
{
	i2c_del_driver(&ak8975c_driver);
}

module_init(ak8975c_init);
module_exit(ak8975c_exit);

MODULE_AUTHOR("Mike J. Chen <mchen@sta.samsung.com>");
MODULE_DESCRIPTION("AK8975 magnetometer driver");
MODULE_LICENSE("GPL");
