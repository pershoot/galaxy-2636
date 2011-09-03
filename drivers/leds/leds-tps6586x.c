/*
 * leds-tps6586x.c - LED class driver for TPS6586x PMU driven LEDs.
 *
 * Copyright (C) 2010 Samsung Electronics Co., Ltd.
 *
 * Inspired by leds-regulator driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/earlysuspend.h>
#include <linux/leds.h>
#include <linux/mfd/tps6586x.h>
#include <linux/platform_device.h>
#include <linux/leds-tps6586x.h>

#define TPS6586x_R54_RGB2RED 0x54
#define TPS6586x_R55_RGB2GREEN 0x55
#define TPS6586x_R56_RGB2BLUE 0x56

#define LED_TPS6586X_PHASE 1
#define LED_TPS6586X_INTENSITY 0x1F

#define to_tps6586x_led(led_cdev) \
	container_of(led_cdev, struct tps6586x_led, cdev)

struct tps6586x_led {
	struct led_classdev cdev;
	struct device *tps_dev;
	enum led_tps6586x_isink isink_default;
	enum led_tps6586x_isink isink;
	u8 color;
	u8 on_time_phase;
	u8 intensity;
	bool enabled;
	struct mutex mutex;
	struct work_struct work;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
};

static inline struct device *to_tps6586x_dev(struct device *dev)
{
	return dev->parent;
}

static void tps6586x_led_enable(struct tps6586x_led *led)
{
	if (led->enabled)
		return;

	/* On control */
	tps6586x_set_bits(led->tps_dev, TPS6586x_R55_RGB2GREEN, (1<<7));

	led->enabled = true;
}

static void tps6586x_led_disable(struct tps6586x_led *led)
{
	if (!led->enabled)
		return;

	/* Off control */
	tps6586x_clr_bits(led->tps_dev, TPS6586x_R55_RGB2GREEN, (1<<7));

	led->enabled = false;
}

static void tps6586x_led_set_value(struct tps6586x_led *led)
{
	u8 rgb2red;
	u8 rgb2green;
	u8 rgb2blue;

	mutex_lock(&led->mutex);

	tps6586x_led_disable(led);

	/* R/G/B intensity control */
	rgb2red = (led->color & LED_TPS6586X_RED) ? led->intensity : 0;
	rgb2green = (led->color & LED_TPS6586X_GREEN) ? led->intensity : 0;
	rgb2blue = (led->color & LED_TPS6586X_BLUE) ? led->intensity : 0;

	/* On time phase control */
	rgb2green |= (u8)(led->on_time_phase<<6);

	tps6586x_write(led->tps_dev, TPS6586x_R54_RGB2RED, rgb2red);
	tps6586x_write(led->tps_dev, TPS6586x_R55_RGB2GREEN, rgb2green);
	tps6586x_write(led->tps_dev, TPS6586x_R56_RGB2BLUE, rgb2blue);

	tps6586x_update(led->tps_dev, TPS6586x_R54_RGB2RED,
		      (u8)(led->isink<<5), 0xE0);

	if (led->isink != LED_TPS6586X_ISINK_NONE)
		tps6586x_led_enable(led);

	mutex_unlock(&led->mutex);
}

static void led_work(struct work_struct *work)
{
	struct tps6586x_led *led;

	led = container_of(work, struct tps6586x_led, work);
	tps6586x_led_set_value(led);
}

static void tps6586x_led_brightness_set(struct led_classdev *led_cdev,
			   enum led_brightness brightness)
{
	struct tps6586x_led *led = to_tps6586x_led(led_cdev);

	if (brightness == LED_HALF)
		led->isink = LED_TPS6586X_ISINK1;
	else if (brightness == LED_FULL)
		led->isink = LED_TPS6586X_ISINK7;

	schedule_work(&led->work);
}

#if defined(CONFIG_HAS_EARLYSUSPEND)
static void tps6586x_led_suspend(struct early_suspend *h)
{
	struct tps6586x_led *led =
		container_of(h, struct tps6586x_led, early_suspend);

	led->isink = LED_TPS6586X_ISINK_NONE;
	tps6586x_led_set_value(led);
}

static void tps6586x_led_resume(struct early_suspend *h)
{
	struct tps6586x_led *led =
		container_of(h, struct tps6586x_led, early_suspend);

	led->isink = led->isink_default;
	tps6586x_led_set_value(led);
}
#endif

static ssize_t tps6586x_led_color_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct tps6586x_led *led = to_tps6586x_led(led_cdev);

	return sprintf(buf, "Current LED color: %d\n", led->color);
}

static ssize_t tps6586x_led_color_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct tps6586x_led *led = to_tps6586x_led(led_cdev);

	int val;

	if (sscanf(buf, "%i", &val) != 1 || (val < 0 || val > 7))
		return -EINVAL;

	led->color = (u8)val;
	if (val == 0)
		tps6586x_led_disable(led);
	else
		tps6586x_led_set_value(led);

	return size;
}

static DEVICE_ATTR(color, S_IRUGO | S_IWUSR,
		tps6586x_led_color_show, tps6586x_led_color_store);

static int __devinit tps6586x_led_probe(struct platform_device *pdev)
{
	struct led_tps6586x_pdata *pdata = pdev->dev.platform_data;
	struct tps6586x_led *led;
	int ret = 0;

	if (pdata == NULL) {
		dev_err(&pdev->dev, "no platform data\n");
		return -ENODEV;
	}

	led = kzalloc(sizeof(*led), GFP_KERNEL);
	if (led == NULL)
		return -ENOMEM;

	/* maximum sink current (27.3mA) */
	led->cdev.max_brightness = LED_TPS6586X_ISINK7;
	if (pdata->isink > led->cdev.max_brightness) {
		dev_err(&pdev->dev, "Invalid default brightness %d\n",
				pdata->isink);
		ret = -EINVAL;
		goto err_led;
	}
	led->isink_default = led->isink = pdata->isink;
	led->color = pdata->color;
	led->on_time_phase = LED_TPS6586X_PHASE;
	led->intensity = LED_TPS6586X_INTENSITY;
	led->enabled = true;

	led->tps_dev = to_tps6586x_dev(&pdev->dev);

	led->cdev.brightness_set = tps6586x_led_brightness_set;
	led->cdev.name = pdata->name;
	led->cdev.flags |= LED_CORE_SUSPENDRESUME;

	mutex_init(&led->mutex);
	INIT_WORK(&led->work, led_work);

	platform_set_drvdata(pdev, led);

	ret = led_classdev_register(&pdev->dev, &led->cdev);
	if (ret < 0)
		goto err_led;

	/* to expose the default value to userspace */
	led->cdev.brightness = led->isink;

	/* Set the default led status */
	tps6586x_led_set_value(led);

#ifdef CONFIG_HAS_EARLYSUSPEND
	led->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	led->early_suspend.suspend = tps6586x_led_suspend;
	led->early_suspend.resume = tps6586x_led_resume;
	register_early_suspend(&led->early_suspend);
#endif

	ret = device_create_file(led->cdev.dev, &dev_attr_color);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to create device file!(%s)!\n",
				dev_attr_color.attr.name);
		ret = -EINVAL;
		goto err_device_create_file;
	}

	return 0;

err_device_create_file:
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&led->early_suspend);
#endif

err_led:
	kfree(led);
	return ret;
}

static int __devexit tps6586x_led_remove(struct platform_device *pdev)
{
	struct tps6586x_led *led = platform_get_drvdata(pdev);

	device_remove_file(led->cdev.dev, &dev_attr_color);
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&led->early_suspend);
#endif
	led_classdev_unregister(&led->cdev);
	cancel_work_sync(&led->work);
	tps6586x_led_disable(led);
	kfree(led);
	return 0;
}

static struct platform_driver tps6586x_led_driver = {
	.driver = {
		   .name  = "tps6586x-leds",
		   .owner = THIS_MODULE,
		   },
	.probe  = tps6586x_led_probe,
	.remove = __devexit_p(tps6586x_led_remove),
};

static int __init tps6586x_led_init(void)
{
	return platform_driver_register(&tps6586x_led_driver);
}
module_init(tps6586x_led_init);

static void __exit tps6586x_led_exit(void)
{
	platform_driver_unregister(&tps6586x_led_driver);
}
module_exit(tps6586x_led_exit);

MODULE_AUTHOR("Samsung Electronics");
MODULE_DESCRIPTION("TPS6586x driven LED driver");
MODULE_LICENSE("GPL");
