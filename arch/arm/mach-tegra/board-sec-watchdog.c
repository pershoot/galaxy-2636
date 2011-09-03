/* board-p3--watchdog.c
 *
 * Copyright (C) 2011 Samsung Electronics, Inc.
 *
 * This is a merge of the Crespo (herring-watchdog.c) and tegra_wdt.c.
 * The tegra_wdt.c partly implements the Linux /dev/watchdog device,
 * but Android doesn't use it so it's not that useful.  Also, it seems
 * to not implement the actual KEEPALIVE ioctl anyway, and resets the
 * watchdog timer using an interrupt.
 *
 * We'll implement it instead like Crespo, using a workqueue, so
 * we can catch some types of kernel issues.
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/err.h>
#include <linux/workqueue.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <mach/iomap.h>

/* minimum and maximum watchdog trigger periods, in seconds */
#define MIN_WDT_PERIOD	5
#define MAX_WDT_PERIOD	1000

#define TIMER_PTV	0x0
#define TIMER_EN	(1 << 31)
#define TIMER_PERIODIC	(1 << 30)

#define TIMER_PCR	0x4
#define TIMER_PCR_INTR	(1 << 30)

#define WDT_EN		(1 << 5)
#define WDT_SEL_TMR1	(0 << 4)
#define WDT_SYS_RST	(1 << 2)

/* must be greater than MIN_WDT_PERIOD and lower than MAX_WDT_PERIOD.
 * we want a 60 second watchdog timeout (i.e. do a reset if we
 * haven't reset the watchdog within 60 seconds).  due to how the
 * watchdog hardware works, we actually need to set the timer to
 * half of that because the reset happens after two intervals (i.e.
 * when the watchdog timer interrupt fires when it is still pending,
 * then the device reset occurs).
 *
 * we'll pet the watchdog every 20 seconds
 */
#define WDT_TIMEOUT 60  /* in seconds */
#define WDT_PET_INTERVAL (20 * HZ) /* in jiffies */

struct watchdog_data {
	void __iomem		*wdt_source;
	void __iomem		*wdt_timer;
	unsigned int		timeout;
	int			pet_interval;
	struct delayed_work	work;
	struct workqueue_struct *wq;
};

static void watchdog_start(struct watchdog_data *wd)
{
	unsigned int val;

	/* since the watchdog reset occurs when a second interrupt
	 * is asserted before the first is processed, program the
	 * timer period to one-half of the watchdog period */
	pr_debug("%s: Arming watchdog with %d sec timeout\n",
		__func__, wd->timeout);
	val = wd->timeout * USEC_PER_SEC / 2;
	val |= (TIMER_EN | TIMER_PERIODIC);
	writel(val, wd->wdt_timer + TIMER_PTV);

	/* Enable watchdog timer, choose timer1 as source, reset entire
	 * system if it expires (instead of just COP or CPU
	 */
	val = WDT_EN | WDT_SEL_TMR1 | WDT_SYS_RST;
	writel(val, wd->wdt_source);

	/* make sure we're ready to pet the dog */
	queue_delayed_work(wd->wq, &wd->work, wd->pet_interval);
}

static void watchdog_stop(struct watchdog_data *wd)
{
	pr_debug("%s\n", __func__);
	writel(0, wd->wdt_source);
	writel(0, wd->wdt_timer + TIMER_PTV);
	/* clear the interrupt in case it was pending */
	writel(TIMER_PCR_INTR, wd->wdt_timer + TIMER_PCR);
}

static void watchdog_workfunc(struct work_struct *work)
{
	struct watchdog_data *wd;

	wd = container_of(work, struct watchdog_data, work.work);

	pr_debug("%s: pet watchdog\n", __func__);

	/* stop watchdog and restart it */
	watchdog_stop(wd);
	watchdog_start(wd);

	/* reschedule to clear it again in the future */
	queue_delayed_work(wd->wq, &wd->work, wd->pet_interval);
}

static int watchdog_probe(struct platform_device *pdev)
{
	struct resource *res_src, *res_tmr;
	struct watchdog_data *wd;
	u32 src_value;
	int ret = 0;

	if (pdev->id != -1) {
		dev_err(&pdev->dev, "only id -1 supported\n");
		return -ENODEV;
	}

	wd = kzalloc(sizeof(*wd), GFP_KERNEL);
	if (!wd) {
		dev_err(&pdev->dev, "out of memory\n");
		return -ENOMEM;
	}

	res_src = request_mem_region(TEGRA_CLK_RESET_BASE, 4, pdev->name);
	if (!res_src) {
		dev_err(&pdev->dev,
			"unable to request mem region TEGRA_CLK_RESET_BASE\n");
		ret = -ENOMEM;
		goto err_request_mem_region_reset_base;
	}
	res_tmr = request_mem_region(TEGRA_TMR1_BASE, TEGRA_TMR1_SIZE,
				pdev->name);

	if (!res_tmr) {
		dev_err(&pdev->dev,
			"unable to request mem region TEGRA_TMR1_BASE\n");
		ret = -ENOMEM;
		goto err_request_mem_region_tmr1_base;
	}

	wd->wdt_source = ioremap(res_src->start, resource_size(res_src));
	if (!wd->wdt_source) {
		dev_err(&pdev->dev, "unable to map clk_reset registers\n");
		ret = -ENOMEM;
		goto err_ioremap_reset_base;
	}
	wd->wdt_timer = ioremap(res_tmr->start, resource_size(res_tmr));
	if (!wd->wdt_timer) {
		dev_err(&pdev->dev, "unable to map tmr1 registers\n");
		ret = -ENOMEM;
		goto err_ioremap_tmr1_base;
	}

	src_value = readl(wd->wdt_source);
	dev_info(&pdev->dev, "reset source register 0x%x\n", src_value);
	if (src_value & BIT(12))
		dev_info(&pdev->dev, "last reset due to watchdog timeout\n");

	/* the watchdog shouldn't be running, but call stop to also clear
	 * some state that might have persisted between boots
	 */
	watchdog_stop(wd);
	wd->timeout = WDT_TIMEOUT;
	wd->pet_interval = WDT_PET_INTERVAL;
	wd->wq = create_workqueue("pet_watchdog");
	if (!wd->wq) {
		dev_err(&pdev->dev, "unable to map tmr1 registers\n");
		ret = -ENOMEM;
		goto err_create_workqueue;
	}
	INIT_DELAYED_WORK(&wd->work, watchdog_workfunc);

	platform_set_drvdata(pdev, wd);

	dev_info(&pdev->dev, "Starting watchdog timer\n");
	watchdog_start(wd);
	return 0;
err_create_workqueue:
	iounmap(wd->wdt_source);
err_ioremap_tmr1_base:
	iounmap(wd->wdt_timer);
err_ioremap_reset_base:
	release_mem_region(res_src->start, resource_size(res_src));
err_request_mem_region_tmr1_base:
	release_mem_region(res_tmr->start, resource_size(res_tmr));
err_request_mem_region_reset_base:
	kfree(wd);
	return ret;
}

static int watchdog_suspend(struct device *dev)
{
	struct watchdog_data *wd = dev_get_drvdata(dev);
	watchdog_stop(wd);
	return 0;
}

static int watchdog_resume(struct device *dev)
{
	struct watchdog_data *wd = dev_get_drvdata(dev);
	watchdog_start(wd);
	return 0;
}

static const struct dev_pm_ops watchdog_pm_ops = {
	.suspend_noirq =	watchdog_suspend,
	.resume_noirq =		watchdog_resume,
};

static struct platform_driver watchdog_driver = {
	.probe =	watchdog_probe,
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"watchdog",
		.pm =		&watchdog_pm_ops,
	},
};

static int __init watchdog_init(void)
{
	return platform_driver_register(&watchdog_driver);
}

module_init(watchdog_init);
