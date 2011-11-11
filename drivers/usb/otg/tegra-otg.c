/*
 * drivers/usb/otg/tegra-otg.c
 *
 * OTG transceiver driver for Tegra UTMI phy
 *
 * Copyright (C) 2010 NVIDIA Corp.
 * Copyright (C) 2010 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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

#include <linux/usb.h>
#include <linux/usb/otg.h>
#include <linux/usb/gadget.h>
#include <linux/usb/hcd.h>
#include <linux/platform_device.h>
#include <linux/platform_data/tegra_usb.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
#ifdef CONFIG_USB_HOST_NOTIFY
#include <linux/host_notify.h>
#endif
#include <linux/gpio.h>
#include <linux/wakelock.h>
#endif

#if defined(CONFIG_TARGET_LOCALE_KOR)
#define _SEC_DM_
#endif

#ifdef _SEC_DM_
struct device *usb_lock;
extern struct class *sec_class;
int usb_access_lock = 0;
//EXPORT_SYMBOL(usb_access_lock);	
#endif


#define USB_PHY_WAKEUP		0x408
#define  USB_ID_INT_EN		(1 << 0)
#define  USB_ID_INT_STATUS	(1 << 1)
#define  USB_ID_STATUS		(1 << 2)
#define  USB_ID_PIN_WAKEUP_EN	(1 << 6)
#define  USB_VBUS_WAKEUP_EN	(1 << 30)
#define  USB_VBUS_INT_EN	(1 << 8)
#define  USB_VBUS_INT_STATUS	(1 << 9)
#define  USB_VBUS_STATUS	(1 << 10)
#define  USB_INTS		(USB_VBUS_INT_STATUS | USB_ID_INT_STATUS)

struct tegra_otg_data {
	struct otg_transceiver otg;
	unsigned long int_status;
	spinlock_t lock;
	void __iomem *regs;
	struct clk *clk;
	int irq;
	struct platform_device *host;
	struct platform_device *pdev;
	struct work_struct work;
	unsigned int intr_reg_data;
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
#ifdef CONFIG_USB_HOST_NOTIFY
	struct host_notify_dev ndev;
#endif
	int currentlimit_irq;
	struct wake_lock wake_lock;
#endif
};


#ifdef _SEC_DM_
/* for sysfs control (/sys/class/sec/sec_usb_lock/enable) */
static ssize_t usb_lock_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (usb_access_lock)
		return snprintf(buf, PAGE_SIZE, "USB_LOCK");
	else
		return snprintf(buf, PAGE_SIZE, "USB_UNLOCK");
}

static ssize_t usb_lock_enable_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t size)
{
	
	struct tegra_otg_data *tegra_otg = dev_get_drvdata(dev);
	struct otg_transceiver *otg = &tegra_otg->otg;
//	struct sec_switch_struct *secsw = dev_get_drvdata(dev);
//	int cable_state;
	unsigned long status;
	int value;
	
	if (sscanf(buf, "%d", &value) != 1) {
		pr_err("%s : Invalid value\n", __func__);
		return -EINVAL;
	}

	if((value < 0) || (value > 1)) {
		pr_err("%s : Invalid value\n", __func__);
		return -EINVAL;
	}

//	if (IS_ERR_OR_NULL(secsw->pdata) ||
//		IS_ERR_OR_NULL(secsw->pdata->set_usb_gadget_vbus) ||
//	 	IS_ERR_OR_NULL(secsw->pdata->get_cable_status))
//		return size;

//		cable_state = secsw->pdata->get_cable_status();
	status = tegra_otg->int_status;
	
	if(value != usb_access_lock) {
		usb_access_lock = value;
		
		if(value == 1) {
			pr_err("%s : Set USB Block!!\n", __func__);
			usb_gadget_vbus_disconnect(otg->gadget);
			//secsw->pdata->set_usb_gadget_vbus(false);
		} else {
			pr_err("%s : Release USB Block!!\n", __func__);
//			if (cable_state)
				//secsw->pdata->set_usb_gadget_vbus(true);
			if (status & USB_VBUS_STATUS)
			{
				pr_err("%s : status: 0x%x\n", __func__, status);
				usb_gadget_vbus_connect(otg->gadget);
			}
		}
	}

	return size;
}
static DEVICE_ATTR(enable, 0664, usb_lock_enable_show, usb_lock_enable_store);
#endif

static inline unsigned long otg_readl(struct tegra_otg_data *tegra,
				      unsigned int offset)
{
	return readl(tegra->regs + offset);
}

static inline void otg_writel(struct tegra_otg_data *tegra, unsigned long val,
			      unsigned int offset)
{
	writel(val, tegra->regs + offset);
}

static const char *tegra_state_name(enum usb_otg_state state)
{
	if (state == OTG_STATE_A_HOST)
		return "HOST";
	if (state == OTG_STATE_B_PERIPHERAL)
		return "PERIPHERAL";
	if (state == OTG_STATE_A_SUSPEND)
		return "SUSPEND";
	return "INVALID";
}

void tegra_start_host(struct tegra_otg_data *tegra)
{
	struct tegra_otg_platform_data *pdata = tegra->otg.dev->platform_data;
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	unsigned int batt_level = 0;

	dev_info(tegra->otg.dev, "tegra_start_host+\n");
	wake_lock(&tegra->wake_lock);
#if 0
	if (*pdata->batt_level) {
		batt_level = **pdata->batt_level;
		if (batt_level < 15) {
#ifdef CONFIG_USB_HOST_NOTIFY
			host_state_notify(&tegra->ndev, NOTIFY_HOST_LOWBATT);
#endif
			dev_info(tegra->otg.dev, "LOW Battery=%d\n",
					**pdata->batt_level);
			return;
		}
	}
#endif
#endif
	if (!tegra->pdev) {
		tegra->pdev = pdata->host_register();
	}
	dev_info(tegra->otg.dev, "tegra_start_host-\n");
}

void tegra_stop_host(struct tegra_otg_data *tegra)
{
	struct tegra_otg_platform_data *pdata = tegra->otg.dev->platform_data;
	dev_info(tegra->otg.dev, "tegra_stop_host+\n");
	if (tegra->pdev) {
		pdata->host_unregister(tegra->pdev);
		tegra->pdev = NULL;
	}
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	wake_unlock(&tegra->wake_lock);
#endif
	dev_info(tegra->otg.dev, "tegra_stop_host-\n");
}

static void irq_work(struct work_struct *work)
{
	struct tegra_otg_data *tegra =
		container_of(work, struct tegra_otg_data, work);
	struct otg_transceiver *otg = &tegra->otg;
	enum usb_otg_state from = otg->state;
	enum usb_otg_state to = OTG_STATE_UNDEFINED;
	unsigned long flags;
	unsigned long status;

	clk_enable(tegra->clk);

	spin_lock_irqsave(&tegra->lock, flags);

	status = tegra->int_status;

	if (tegra->int_status & USB_ID_INT_STATUS) {
		if (status & USB_ID_STATUS) {
			if ((status & USB_VBUS_STATUS) && (from != OTG_STATE_A_HOST))
				to = OTG_STATE_B_PERIPHERAL;
			else
				to = OTG_STATE_A_SUSPEND;
		}
		else
			to = OTG_STATE_A_HOST;
	}
	if (from != OTG_STATE_A_HOST) {
		if (tegra->int_status & USB_VBUS_INT_STATUS) {
			if (status & USB_VBUS_STATUS)
				to = OTG_STATE_B_PERIPHERAL;
			else
				to = OTG_STATE_A_SUSPEND;
		}
	}
	spin_unlock_irqrestore(&tegra->lock, flags);

	if (to != OTG_STATE_UNDEFINED) {
		otg->state = to;

		dev_info(tegra->otg.dev, "%s --> %s\n", tegra_state_name(from),
					      tegra_state_name(to));

		if (to == OTG_STATE_A_SUSPEND) {
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
#ifdef CONFIG_USB_HOST_NOTIFY
			tegra->ndev.mode = NOTIFY_NONE_MODE;
#endif
#endif
			if (from == OTG_STATE_A_HOST) {
				tegra_stop_host(tegra);
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
#ifdef CONFIG_USB_HOST_NOTIFY
				host_state_notify(&tegra->ndev,
					 NOTIFY_HOST_REMOVE);
#endif
#endif
			} else if (from == OTG_STATE_B_PERIPHERAL && otg->gadget)
#ifdef _SEC_DM_
				if (!usb_access_lock)
#endif
				usb_gadget_vbus_disconnect(otg->gadget);
		} else if (to == OTG_STATE_B_PERIPHERAL && otg->gadget) {
			if (from == OTG_STATE_A_SUSPEND || from == OTG_STATE_UNDEFINED)
#ifdef _SEC_DM_
				if (!usb_access_lock)
#endif
				usb_gadget_vbus_connect(otg->gadget);
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
#ifdef CONFIG_USB_HOST_NOTIFY
			tegra->ndev.mode = NOTIFY_PERIPHERAL_MODE;
#endif
#endif
		} else if (to == OTG_STATE_A_HOST) {
			if (from == OTG_STATE_A_SUSPEND)
			tegra_start_host(tegra);
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
			else if (from == OTG_STATE_B_PERIPHERAL) {
#ifdef _SEC_DM_
				if (!usb_access_lock)
#endif
				usb_gadget_vbus_disconnect(otg->gadget);
				tegra_start_host(tegra);
			}
#ifdef CONFIG_USB_HOST_NOTIFY
			tegra->ndev.mode = NOTIFY_HOST_MODE;
			host_state_notify(&tegra->ndev, NOTIFY_HOST_ADD);
#endif
#endif
		}
	}
	clk_disable(tegra->clk);
}

static irqreturn_t tegra_otg_irq(int irq, void *data)
{
	struct tegra_otg_data *tegra = data;
	unsigned long flags;
	unsigned long val;

	spin_lock_irqsave(&tegra->lock, flags);

	val = otg_readl(tegra, USB_PHY_WAKEUP);
	otg_writel(tegra, val, USB_PHY_WAKEUP);

	if ((val & USB_ID_INT_STATUS) || (val & USB_VBUS_INT_STATUS)) {
		tegra->int_status = val;
		schedule_work(&tegra->work);
	}

	spin_unlock_irqrestore(&tegra->lock, flags);

	return IRQ_HANDLED;
}

static int tegra_otg_set_peripheral(struct otg_transceiver *otg,
				struct usb_gadget *gadget)
{
	struct tegra_otg_data *tegra;
	unsigned long val;

	tegra = container_of(otg, struct tegra_otg_data, otg);
	otg->gadget = gadget;

	clk_enable(tegra->clk);
	val = otg_readl(tegra, USB_PHY_WAKEUP);
	val |= (USB_VBUS_INT_EN | USB_VBUS_WAKEUP_EN);
	val |= (USB_ID_INT_EN | USB_ID_PIN_WAKEUP_EN);
	otg_writel(tegra, val, USB_PHY_WAKEUP);
	/* Add delay to make sure register is updated */
	udelay(1);
	clk_disable(tegra->clk);

	if ((val & USB_ID_STATUS) && (val & USB_VBUS_STATUS)) {
		val |= USB_VBUS_INT_STATUS;
	} else if (!(val & USB_ID_STATUS)) {
		val |= USB_ID_INT_STATUS;
	} else {
		val &= ~(USB_ID_INT_STATUS | USB_VBUS_INT_STATUS);
	}

	if ((val & USB_ID_INT_STATUS) || (val & USB_VBUS_INT_STATUS)) {
		tegra->int_status = val;
		schedule_work (&tegra->work);
	}

	return 0;
}

static int tegra_otg_set_host(struct otg_transceiver *otg,
				struct usb_bus *host)
{
	struct tegra_otg_data *tegra;
	unsigned long val;

	tegra = container_of(otg, struct tegra_otg_data, otg);
	otg->host = host;

	clk_enable(tegra->clk);
	val = otg_readl(tegra, USB_PHY_WAKEUP);
	val &= ~(USB_VBUS_INT_STATUS | USB_ID_INT_STATUS);

	val |= (USB_ID_INT_EN | USB_ID_PIN_WAKEUP_EN);
	otg_writel(tegra, val, USB_PHY_WAKEUP);
	clk_disable(tegra->clk);

	return 0;
}

static int tegra_otg_set_power(struct otg_transceiver *otg, unsigned mA)
{
	return 0;
}

static int tegra_otg_set_suspend(struct otg_transceiver *otg, int suspend)
{
	return 0;
}

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
static int tegra_get_accpower_level(int irq)
{
	int gpio_ret;
	gpio_ret = irq_to_gpio(irq);
	if (gpio_ret < 0)
		printk(KERN_ERR "%s get gpio error\n", __func__);
	return __gpio_get_value((unsigned)gpio_ret);
}

static irqreturn_t tegra_currentlimit_irq_thread(int irq, void *data)
{
	struct tegra_otg_data *tegra = data;

	if (tegra_get_accpower_level(tegra->currentlimit_irq)) {
#ifdef CONFIG_USB_HOST_NOTIFY
		tegra->ndev.booster = NOTIFY_POWER_ON;
#endif
		dev_info(tegra->otg.dev, "Acc power on detect\n");
	} else {
#ifdef CONFIG_USB_HOST_NOTIFY
		if (tegra->ndev.mode == NOTIFY_HOST_MODE) {
			host_state_notify(&tegra->ndev,
				NOTIFY_HOST_OVERCURRENT);
			dev_err(tegra->otg.dev, "OTG overcurrent!!!!!!\n");
		}
		tegra->ndev.booster = NOTIFY_POWER_OFF;
#endif
	}
	return IRQ_HANDLED;
}
#endif

static int tegra_otg_probe(struct platform_device *pdev)
{
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	struct tegra_otg_platform_data *pdata;
#endif
	struct tegra_otg_data *tegra;
	struct resource *res;
	int err;

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "Platform data missing\n");
		return -EINVAL;
	}
#endif
	tegra = kzalloc(sizeof(struct tegra_otg_data), GFP_KERNEL);
	if (!tegra)
		return -ENOMEM;

	tegra->otg.dev = &pdev->dev;
	tegra->otg.label = "tegra-otg";
	tegra->otg.state = OTG_STATE_UNDEFINED;
	tegra->otg.set_host = tegra_otg_set_host;
	tegra->otg.set_peripheral = tegra_otg_set_peripheral;
	tegra->otg.set_suspend = tegra_otg_set_suspend;
	tegra->otg.set_power = tegra_otg_set_power;
	spin_lock_init(&tegra->lock);

	platform_set_drvdata(pdev, tegra);

#ifdef _SEC_DM_
/* for sysfs control (/sys/class/sec/sec_usb_lock/) */
	usb_lock = device_create(sec_class, NULL, 0, NULL, "sec_usb_lock");

	if (IS_ERR(usb_lock)) {
		pr_err("Failed to create device (usb_lock)!\n");
		return PTR_ERR(usb_lock);
	}

	dev_set_drvdata(usb_lock, tegra);

	if (device_create_file(usb_lock, &dev_attr_enable) < 0)	{
		pr_err("Failed to create device file(%s)!\n", dev_attr_enable.attr.name);
		device_destroy((struct class *)usb_lock, 0);
	}
#endif

	tegra->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(tegra->clk)) {
		dev_err(&pdev->dev, "Can't get otg clock\n");
		err = PTR_ERR(tegra->clk);
		goto err_clk;
	}

	err = clk_enable(tegra->clk);
	if (err)
		goto err_clken;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get I/O memory\n");
		err = -ENXIO;
		goto err_io;
	}
	tegra->regs = ioremap(res->start, resource_size(res));
	if (!tegra->regs) {
		err = -ENOMEM;
		goto err_io;
	}

	tegra->otg.state = OTG_STATE_A_SUSPEND;

	err = otg_set_transceiver(&tegra->otg);
	if (err) {
		dev_err(&pdev->dev, "can't register transceiver (%d)\n", err);
		goto err_otg;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get IRQ\n");
		err = -ENXIO;
		goto err_irq;
	}
	tegra->irq = res->start;
	err = request_threaded_irq(tegra->irq, tegra_otg_irq,
				   NULL,
				   IRQF_SHARED, "tegra-otg", tegra);
	if (err) {
		dev_err(&pdev->dev, "Failed to register IRQ\n");
		goto err_irq;
	}
	INIT_WORK (&tegra->work, irq_work);

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	if (pdata->currentlimit_irq) {
		tegra->currentlimit_irq =
			pdata->currentlimit_irq;
		err = request_threaded_irq(tegra->currentlimit_irq,
					NULL,
					tegra_currentlimit_irq_thread,
					(IRQF_TRIGGER_FALLING |
						IRQF_TRIGGER_RISING),
					dev_name(&pdev->dev),
					tegra);
		if (err) {
			dev_err(&pdev->dev, "Failed to register IRQ\n");
			goto err_irq;
		}
	}
#ifdef CONFIG_USB_HOST_NOTIFY
#define NOTIFY_DRIVER_NAME "usb_otg"
	tegra->ndev.name = NOTIFY_DRIVER_NAME;
	if (pdata->otg_en)
		tegra->ndev.set_booster = pdata->otg_en;
	err = host_notify_dev_register(&tegra->ndev);
	if (err) {
		dev_err(&pdev->dev, "Failed to host_notify_dev_register\n");
		goto err_irq;
	}
#endif
	wake_lock_init(&tegra->wake_lock, WAKE_LOCK_SUSPEND, "tegra-otg");
#endif
	dev_info(&pdev->dev, "otg transceiver registered\n");
	return 0;

err_irq:
	otg_set_transceiver(NULL);
err_otg:
	iounmap(tegra->regs);
err_io:
	clk_disable(tegra->clk);
err_clken:
	clk_put(tegra->clk);
err_clk:
	platform_set_drvdata(pdev, NULL);
	kfree(tegra);
	return err;
}

static int __exit tegra_otg_remove(struct platform_device *pdev)
{
	struct tegra_otg_data *tegra = platform_get_drvdata(pdev);

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
#ifdef CONFIG_USB_HOST_NOTIFY
	host_notify_dev_unregister(&tegra->ndev);
#endif
	if (tegra->currentlimit_irq)
		free_irq(tegra->currentlimit_irq, tegra);
#endif
	free_irq(tegra->irq, tegra);
	otg_set_transceiver(NULL);
	iounmap(tegra->regs);
	clk_disable(tegra->clk);
	clk_put(tegra->clk);
	platform_set_drvdata(pdev, NULL);
	kfree(tegra);

	return 0;
}

#ifdef CONFIG_PM
static int tegra_otg_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct tegra_otg_data *tegra_otg = platform_get_drvdata(pdev);

	/* store the interupt enable for cable ID and VBUS */
	tegra_otg->intr_reg_data = readl(tegra_otg->regs + USB_PHY_WAKEUP);

	return 0;
}

static int tegra_otg_resume(struct platform_device * pdev)
{
	struct tegra_otg_data *tegra_otg = platform_get_drvdata(pdev);

	/* restore the interupt enable for cable ID and VBUS */
	writel(tegra_otg->intr_reg_data, (tegra_otg->regs + USB_PHY_WAKEUP));

	return 0;
}
#endif

static struct platform_driver tegra_otg_driver = {
	.driver = {
		.name  = "tegra-otg",
	},
	.remove  = __exit_p(tegra_otg_remove),
	.probe   = tegra_otg_probe,
#ifdef CONFIG_PM
	.suspend = tegra_otg_suspend,
	.resume = tegra_otg_resume,
#endif
};

static int __init tegra_otg_init(void)
{
	return platform_driver_register(&tegra_otg_driver);
}
subsys_initcall(tegra_otg_init);

static void __exit tegra_otg_exit(void)
{
	platform_driver_unregister(&tegra_otg_driver);
}
module_exit(tegra_otg_exit);
