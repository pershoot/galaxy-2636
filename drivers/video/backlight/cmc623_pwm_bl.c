/*
 *  cmc623_pwm Backlight Driver based on SWI Driver.
 *
 *  Copyright (c) 2009 Samsung Electronics
 *  InKi Dae <inki.dae@samsung.com>
 *
 *  Based on Sharp's Corgi Backlight Driver
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>

#include <mach/hardware.h>
#include <mach/gpio.h>


#if defined(CONFIG_MACH_SAMSUNG_P4)  || defined(CONFIG_MACH_SAMSUNG_P4WIFI) || defined(CONFIG_MACH_SAMSUNG_P4LTE)
#define CMC623_PWM_MAX_INTENSITY		255
#define CMC623_PWM_DEFAULT_INTENSITY	150
#define MAX_LEVEL			1600

/*brightness tuning*/
#define MAX_BRIGHTNESS_LEVEL 255
#define MID_BRIGHTNESS_LEVEL 150
#define LOW_BRIGHTNESS_LEVEL 30
#define DIM_BRIGHTNESS_LEVEL 20
#elif defined(CONFIG_MACH_SAMSUNG_P5)
#define CMC623_PWM_MAX_INTENSITY		255
#define CMC623_PWM_DEFAULT_INTENSITY	150
#define MAX_LEVEL			1600

/*brightness tuning*/
#define MAX_BRIGHTNESS_LEVEL 255
#define MID_BRIGHTNESS_LEVEL 150
#define LOW_BRIGHTNESS_LEVEL 30
#define DIM_BRIGHTNESS_LEVEL 20


#endif
#if defined(CONFIG_MACH_SAMSUNG_P4)  || defined(CONFIG_MACH_SAMSUNG_P4WIFI) || defined(CONFIG_MACH_SAMSUNG_P4LTE)
#define MAX_BACKLIGHT_VALUE 1600 	/* 100%*/
#define MID_BACKLIGHT_VALUE 400  	/*36.5%*/
#define LOW_BACKLIGHT_VALUE 60 	/*2%*/
#define DIM_BACKLIGHT_VALUE 60 	/*2%*/
#elif defined(CONFIG_MACH_SAMSUNG_P5)
#define MAX_BACKLIGHT_VALUE 1600 	/* 100%*/
#define MID_BACKLIGHT_VALUE 784  	/*32% : 125nits */
#define LOW_BACKLIGHT_VALUE 60 	/*2%*/
#define DIM_BACKLIGHT_VALUE 60 	/*2%*/
#endif




extern void set_backlight_pwm(int value);

static struct early_suspend	st_early_suspend;
static struct platform_device *bl_pdev;

static int current_backlight_level = MID_BACKLIGHT_VALUE;

static int cmc623_pwm_suspended;
static int current_intensity;

static DEFINE_MUTEX(cmc623_pwm_mutex);


static void cmc623_pwm_apply_brightness(struct platform_device *pdev, int level)
{
	set_backlight_pwm(level);
	current_backlight_level = level;
}


static void cmc623_pwm_backlight_ctl(struct platform_device *pdev, int intensity)
{
	int tune_level;

		/* brightness tuning*/
		if (intensity >= MID_BRIGHTNESS_LEVEL)
			tune_level = (intensity - MID_BRIGHTNESS_LEVEL) * (MAX_BACKLIGHT_VALUE-MID_BACKLIGHT_VALUE) / (MAX_BRIGHTNESS_LEVEL-MID_BRIGHTNESS_LEVEL) + MID_BACKLIGHT_VALUE;
		else if (intensity >= LOW_BRIGHTNESS_LEVEL)
			tune_level = (intensity - LOW_BRIGHTNESS_LEVEL) * (MID_BACKLIGHT_VALUE-LOW_BACKLIGHT_VALUE) / (MID_BRIGHTNESS_LEVEL-LOW_BRIGHTNESS_LEVEL) + LOW_BACKLIGHT_VALUE;
		else if (intensity >= DIM_BRIGHTNESS_LEVEL)
			tune_level = (intensity - DIM_BRIGHTNESS_LEVEL) * (LOW_BACKLIGHT_VALUE-DIM_BACKLIGHT_VALUE) / (LOW_BRIGHTNESS_LEVEL-DIM_BRIGHTNESS_LEVEL) + DIM_BACKLIGHT_VALUE;
		else if (intensity > 0)
			tune_level = DIM_BACKLIGHT_VALUE;
		else
			tune_level = intensity;

	/*printk("--- [cmc]%d(%d)---\n", intensity, tune_level);*/
   	//printk("[CMC623:INFO] Intensity : %d, Tuned Intensity : %d\n",intensity, tune_level);

	cmc623_pwm_apply_brightness(pdev, tune_level);
}


static void cmc623_pwm_send_intensity(struct backlight_device *bd)
{
	/*unsigned long flags;*/
	int intensity = bd->props.brightness;
	struct platform_device *pdev = NULL;

	pdev = dev_get_drvdata(&bd->dev);
	if (pdev == NULL) {
		printk(KERN_ERR "%s:failed to get platform device.\n", __func__);
		return;
	}
#if 0
	if (bd->props.power != FB_BLANK_UNBLANK ||
		bd->props.fb_blank != FB_BLANK_UNBLANK ||
		cmc623_pwm_suspended) {
		printk("[cmc]i:%d(c:%d)\n", intensity, current_intensity);
		if (!current_intensity)
			return;
		msleep(1);
		intensity = 0;
	}
#endif

	mutex_lock(&cmc623_pwm_mutex);

	cmc623_pwm_backlight_ctl(pdev, intensity);

	mutex_unlock(&cmc623_pwm_mutex);

	current_intensity = intensity;
}

#ifdef CONFIG_PM
static int cmc623_pwm_suspend(struct platform_device *swi_dev, pm_message_t state)
{
	struct backlight_device *bd = platform_get_drvdata(swi_dev);

	cmc623_pwm_suspended = 1;
	cmc623_pwm_send_intensity(bd);
	return 0;
}

static int cmc623_pwm_resume(struct platform_device *swi_dev)
{
	struct backlight_device *bd = platform_get_drvdata(swi_dev);

	bd->props.brightness = CMC623_PWM_DEFAULT_INTENSITY;
	cmc623_pwm_suspended = 0;
	cmc623_pwm_send_intensity(bd);

	return 0;
}
#else
#define cmc623_pwm_suspend		NULL
#define cmc623_pwm_resume		NULL
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void cmc623_pwm_early_suspend(struct early_suspend *h)
{
	struct backlight_device *bd = platform_get_drvdata(bl_pdev);

	cmc623_pwm_suspended = 1;
	cmc623_pwm_send_intensity(bd);
}

static void cmc623_pwm_early_resume(struct early_suspend *h)
{
	struct backlight_device *bd = platform_get_drvdata(bl_pdev);

	/*bd->props.brightness = cmc623_pwm_DEFAULT_INTENSITY;*/
	cmc623_pwm_suspended = 0;

	cmc623_pwm_send_intensity(bd);

}
#endif

static int cmc623_pwm_set_intensity(struct backlight_device *bd)
{
	/*printk("BD->PROPS.BRIGHTNESS = %d\n", bd->props.brightness);*/

	cmc623_pwm_send_intensity(bd);

	return 0;
}


static int cmc623_pwm_get_intensity(struct backlight_device *bd)
{
	return current_intensity;
}


static struct backlight_ops cmc623_pwm_ops = {
	.get_brightness = cmc623_pwm_get_intensity,
	.update_status  = cmc623_pwm_set_intensity,
};

/*for measuring luminance*/
void cmc623_pwm_set_brightness(int brightness)
{
	/*unsigned long flags;*/

	printk("%s: value=%d\n", __func__, brightness);

	mutex_lock(&cmc623_pwm_mutex);

	cmc623_pwm_apply_brightness(bl_pdev, brightness);

	mutex_unlock(&cmc623_pwm_mutex);
}
EXPORT_SYMBOL(cmc623_pwm_set_brightness);

static int cmc623_pwm_probe(struct platform_device *pdev)
{
	struct backlight_properties props;
	struct backlight_device *bd;

	printk("cmc623_pwm Probe START!!!\n");

	bd = backlight_device_register("pwm-backlight", &pdev->dev, pdev, &cmc623_pwm_ops, &props);

	if (IS_ERR(bd))
		return PTR_ERR(bd);

	platform_set_drvdata(pdev, bd);

	bd->props.max_brightness = CMC623_PWM_MAX_INTENSITY;
	bd->props.brightness = CMC623_PWM_DEFAULT_INTENSITY;

	dev_info(&pdev->dev, "cmc623_pwm backlight driver is enabled.\n");

	bl_pdev = pdev;

#ifdef CONFIG_HAS_EARLYSUSPEND
	st_early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	st_early_suspend.suspend = cmc623_pwm_early_suspend;
	st_early_suspend.resume = cmc623_pwm_early_resume;
	register_early_suspend(&st_early_suspend);
#endif	/* CONFIG_HAS_EARLYSUSPEND */

	printk("cmc623_pwm Probe END!!!\n");
	return 0;

}

static int cmc623_pwm_remove(struct platform_device *pdev)
{
	struct backlight_device *bd = platform_get_drvdata(pdev);

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&st_early_suspend);
#endif	/* CONFIG_HAS_EARLYSUSPEND */

	bd->props.brightness = 0;
	bd->props.power = 0;
	cmc623_pwm_send_intensity(bd);

	backlight_device_unregister(bd);

	return 0;
}

static struct platform_driver cmc623_pwm_driver = {
	.driver		= {
		.name	= "cmc623_pwm_bl",
		.owner	= THIS_MODULE,
	},
	.probe		= cmc623_pwm_probe,
	.remove		= cmc623_pwm_remove,
#if !(defined CONFIG_HAS_EARLYSUSPEND)
	.suspend	= cmc623_pwm_suspend,
	.resume		= cmc623_pwm_resume,
#endif
};

static int __init cmc623_pwm_init(void)
{
	return platform_driver_register(&cmc623_pwm_driver);
}

static void __exit cmc623_pwm_exit(void)
{
	platform_driver_unregister(&cmc623_pwm_driver);
}

module_init(cmc623_pwm_init);
module_exit(cmc623_pwm_exit);
