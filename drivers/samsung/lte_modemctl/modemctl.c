/*
 * Modem control driver
 *
 * Copyright (C) 2010 Samsung Electronics Co.Ltd
 * Author: Suchang Woo <suchang.woo@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#undef DEBUG
#define CONFIG_SEC_DEBUG


#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/kdev_t.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/modemctl.h>
#include <mach/gpio-p4lte.h>
#ifdef CONFIG_KERNEL_DEBUG_SEC
#include <linux/kernel_sec_common.h>
#endif
#include <linux/reboot.h>

#define HOST_WUP_LEVEL 1

enum {
	HOST_WAKEUP_LOW = 1,
	HOST_WAKEUP_WAIT_RESET,
} HOST_WAKEUP_STATE;

enum {
	SVNET_ERROR_RESET,
	SVNET_ERROR_CRASH,
} SVNET_ERROR_TYPE;

/* FIXME: Don't use this except pm */
static struct modemctl *global_mc;
extern struct usbsvn *share_svn;

static int ignore_irq_count = 1;
extern int modemctl_shutdown_flag;

int mc_is_phone_on_disable(void)
{
	if (!global_mc)
		return -EFAULT;

	gpio_set_value(global_mc->gpio_phone_on, 0);

	return 0;    
}
EXPORT_SYMBOL_GPL(mc_is_phone_on_disable);

int mc_is_modem_on(void)
{
	return 1;
}
EXPORT_SYMBOL_GPL(mc_is_modem_on);

int mc_is_modem_active(void)
{
	if (!global_mc)
		return 0;

	return gpio_get_value(global_mc->gpio_phone_active);
}
EXPORT_SYMBOL_GPL(mc_is_modem_active);

int mc_control_active_state(int val)
{
	if (!global_mc)
		return -EFAULT;

	if (mc_is_modem_on()) {
		gpio_set_value(global_mc->gpio_host_active, val ? 1 : 0);
		printk("APtoLTE Active:%d\n", val ? 1 : 0);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(mc_control_active_state);

int mc_control_slave_wakeup(int val)
{
	if (!global_mc)
		return -EFAULT;

	gpio_set_value(global_mc->gpio_slave_wakeup, val ? 1 : 0);
	if (val == 1)
		printk(">>> S- WUP %d\n", gpio_get_value(global_mc->gpio_slave_wakeup));
	else
		printk("> S- WUP %d\n", gpio_get_value(global_mc->gpio_slave_wakeup));
	
	return 0;
}
EXPORT_SYMBOL_GPL(mc_control_slave_wakeup);

int mc_is_host_wakeup(void)
{
	if (!global_mc)
		return 0;

	return (gpio_get_value(global_mc->gpio_host_wakeup)
		== HOST_WUP_LEVEL) ? 1 : 0;
}
EXPORT_SYMBOL_GPL(mc_is_host_wakeup);

/* WJ 0413 */
int mc_is_slave_wakeup(void)
{
	if (!global_mc)
		return 0;

	return (gpio_get_value(global_mc->gpio_slave_wakeup)
		== 1) ? 1 : 0;
}
EXPORT_SYMBOL_GPL(mc_is_slave_wakeup);

void mc_phone_active_irq_enable(int on)
{
	if (!global_mc)
		return 0;
	if(on)
		enable_irq(global_mc->irq[0]);
	else
		disable_irq(global_mc->irq[0]);

	printk(KERN_ERR "[%s]%d\n",__func__,__LINE__);
}
EXPORT_SYMBOL_GPL(mc_phone_active_irq_enable);

#if 0 //temp_inchul
int mc_is_suspend_request(void)
{
	if (!global_mc)
		return 0;

	printk(KERN_DEBUG "%s:suspend requset val=%d\n", __func__,
		gpio_get_value(global_mc->gpio_suspend_request));
	return gpio_get_value(global_mc->gpio_suspend_request);
}
#endif

int mc_prepare_resume(int ms_time)
{
	int val;
	struct completion done;
	struct modemctl *mc = global_mc;

	if (!mc)
		return -EFAULT;

	val = gpio_get_value(mc->gpio_host_wakeup);
	if (val == HOST_WUP_LEVEL) {
		dev_dbg(mc->dev, "svn HOST_WUP:high!\n");
		return MC_HOST_HIGH;
	}

	val = gpio_get_value(mc->gpio_slave_wakeup);
	if (val) {
		gpio_set_value(mc->gpio_slave_wakeup, 0);
		dev_err(mc->dev, "svn SLAV_WUP:reset\n");
	}

	init_completion(&done);
	mc->l2_done = &done;
	gpio_set_value(mc->gpio_slave_wakeup, 1);
	printk(KERN_INFO ">> S- WUP 1\n");

	if (!wait_for_completion_timeout(&done, ms_time)) {
		dev_err(mc->dev, "Modem wakeup timeout %ld\n", ms_time);
		gpio_set_value(mc->gpio_slave_wakeup, 0);
		dev_err(mc->dev, "svn >SLAV_WUP:1,%d\n",
			gpio_get_value(mc->gpio_slave_wakeup));
		mc->l2_done = NULL;
		return MC_HOST_TIMEOUT;
	}
	return MC_SUCCESS;
}

int mc_reconnect_gpio(void)
{
	struct modemctl *mc = global_mc;

	if (!mc)
		return -EFAULT;

	dev_err(mc->dev, "TRY Reconnection...\n");

	gpio_set_value(mc->gpio_host_active, 0);
	dev_err(mc->dev, "TRY Reconnection >> H- act 0\n");
	msleep(10);
	gpio_set_value(mc->gpio_slave_wakeup, 1);
	dev_err(mc->dev, "TRY Reconnection >> S- WUP 1\n");
	msleep(10);
	gpio_set_value(mc->gpio_slave_wakeup, 0);
	dev_err(mc->dev, "TRY Reconnection >> S- WUP 0\n");
	msleep(10);
	gpio_set_value(mc->gpio_host_active, 1);
	dev_err(mc->dev, "TRY Reconnection >> H- act 1\n");

	return 0;
}

#ifdef CONFIG_SEC_DEBUG
/*
 * HSIC CP uploas scenario -
 * 1. CP send Crash message
 * 2. Rild save the ram data to file via HSIC
 * 3. Rild call the kernel_upload() for AP ram dump
 */
static void kernel_upload(struct modemctl *mc)
{
#if 0
	/*TODO: check the DEBUG LEVEL*/
	if (mc->cpcrash_flag)
		panic("LTE Crash");		
	else
		panic("HSIC Disconnected");
#else
	/*TODO: check the DEBUG LEVEL*/
	if (mc->cpcrash_flag){
                 kernel_sec_set_upload_cause(UPLOAD_CAUSE_LTE_ERROR_FATAL);
         }
	else{
                 kernel_sec_set_upload_cause(0xFF); //0xff : unkonwn case
         }

         kernel_sec_hw_reset(false);
         emergency_restart();    
#endif
}
#else
static void kernel_upload(struct modemctl *mc) {}
#endif

static int modem_on(struct modemctl *mc)
{
	dev_dbg(mc->dev, "%s\n", __func__);
	if (!mc->ops || !mc->ops->modem_on)
		return -ENXIO;

	mc->ops->modem_on(mc);

	return 0;
}

static int modem_off(struct modemctl *mc)
{
	dev_dbg(mc->dev, "%s\n", __func__);
	if (!mc->ops || !mc->ops->modem_off)
		return -ENXIO;

	mc->ops->modem_off(mc);

	return 0;
}

static int modem_reset(struct modemctl *mc)
{
	dev_dbg(mc->dev, "%s\n", __func__);
	if (!mc->ops || !mc->ops->modem_reset)
		return -ENXIO;

	mc->ops->modem_reset(mc);

	return 0;
}

static int modem_boot(struct modemctl *mc)
{
	dev_dbg(mc->dev, "%s\n", __func__);
	if (!mc->ops || !mc->ops->modem_boot)
		return -ENXIO;

	mc->ops->modem_boot(mc);

	return 0;
}

static int modem_get_active(struct modemctl *mc)
{
	dev_dbg(mc->dev, "%s\n", __func__);
	if (!mc->gpio_phone_active || !mc->gpio_cp_reset)
		return -ENXIO;

	dev_dbg(mc->dev, "cp %d phone %d\n",
			gpio_get_value(mc->gpio_cp_reset),
			gpio_get_value(mc->gpio_phone_active));

	if (gpio_get_value(mc->gpio_cp_reset))
		return gpio_get_value(mc->gpio_phone_active);

	return 0;
}

static ssize_t show_control(struct device *d,
		struct device_attribute *attr, char *buf)
{
	char *p = buf;
	struct modemctl *mc = dev_get_drvdata(d);
	struct modemctl_ops *ops = mc->ops;

	if (ops) {
		if (ops->modem_on)
			p += sprintf(p, "on ");
		if (ops->modem_off)
			p += sprintf(p, "off ");
		if (ops->modem_reset)
			p += sprintf(p, "reset ");
		if (ops->modem_boot)
			p += sprintf(p, "boot ");
	} else {
		p += sprintf(p, "(No ops)");
	}

	p += sprintf(p, "\n");
	return p - buf;
}

static ssize_t store_control(struct device *d,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct modemctl *mc = dev_get_drvdata(d);

	if (!strncmp(buf, "on", 2)) {
		modem_on(mc);
		return count;
	}

	if (!strncmp(buf, "off", 3)) {
		modem_off(mc);
		return count;
	}

	if (!strncmp(buf, "reset", 5)) {
		modem_reset(mc);
		return count;
	}

	if (!strncmp(buf, "boot", 4)) {
		//modem_boot(mc);
		ignore_irq_count = 1;		
		enable_irq(mc->irq[0]);           
		return count;
	}

	if (!strncmp(buf, "daolpu", 6)) {
		kernel_upload(mc);
		return count;
	}

	if (!strncmp(buf, "silent", 6)) {
		printk(KERN_ERR "%s -LTE Silent Reset!!!\n", __func__);        
		crash_event(1);
		return count;
	}    

	return count;
}

static ssize_t show_status(struct device *d,
		struct device_attribute *attr, char *buf)
{
	char *p = buf;
	struct modemctl *mc = dev_get_drvdata(d);

	p += sprintf(p, "%d\n", modem_get_active(mc));

	return p - buf;
}

static ssize_t show_wakeup(struct device *d,
		struct device_attribute *attr, char *buf)
{
	struct modemctl *mc = dev_get_drvdata(d);
	int count = 0;

	if (!mc->gpio_host_wakeup)
		return -ENXIO;

	count += sprintf(buf + count, "%d\n",
			mc->wakeup_flag);

	return count;
}

static ssize_t store_wakeup(struct device *d,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct modemctl *mc = dev_get_drvdata(d);

	if (!strncmp(buf, "reset", 5)) {
		mc->wakeup_flag = HOST_WAKEUP_WAIT_RESET;
		dev_dbg(mc->dev, "%s: wakup_flag %d\n",
			__func__, mc->wakeup_flag);
		return count;
	}
	return 0;

}

static ssize_t show_debug(struct device *d,
		struct device_attribute *attr, char *buf)
{
	char *p = buf;
	int i;
	struct modemctl *mc = dev_get_drvdata(d);

	for (i = 0; i < ARRAY_SIZE(mc->irq); i++) {
		if (mc->irq[i])
			p += sprintf(p, "Irq %d: %d\n", i, mc->irq[i]);
	}

	p += sprintf(p, "GPIO ----\n");

	if (mc->gpio_phone_on)
		p += sprintf(p, "\t%3d %d : phone on\n", mc->gpio_phone_on,
				gpio_get_value(mc->gpio_phone_on));
	if (mc->gpio_phone_off)
		p += sprintf(p, "\t%3d %d : phone off\n", mc->gpio_phone_off,
				gpio_get_value(mc->gpio_phone_off));   
	if (mc->gpio_phone_active)
		p += sprintf(p, "\t%3d %d : phone active\n",
				mc->gpio_phone_active,
				gpio_get_value(mc->gpio_phone_active));
#if 0 //temp_pfe     
	if (mc->gpio_pda_active)
		p += sprintf(p, "\t%3d %d : pda active\n", mc->gpio_pda_active,
				gpio_get_value(mc->gpio_pda_active));
#endif    
	if (mc->gpio_cp_reset)
		p += sprintf(p, "\t%3d %d : CP reset\n", mc->gpio_cp_reset,
				gpio_get_value(mc->gpio_cp_reset));

	p += sprintf(p, "Support types ---\n");

	return p - buf;
}
static DEVICE_ATTR(control, S_IRUGO | S_IWUSR| S_IWGRP, show_control, store_control);
static DEVICE_ATTR(status, S_IRUGO, show_status, NULL);
static DEVICE_ATTR(wakeup, S_IRUGO | S_IWUSR| S_IWGRP, show_wakeup, store_wakeup);
static DEVICE_ATTR(debug, S_IRUGO, show_debug, NULL);

static struct attribute *modemctl_attributes[] = {
	&dev_attr_control.attr,
	&dev_attr_status.attr,
	&dev_attr_wakeup.attr,
	&dev_attr_debug.attr,
	NULL
};

static const struct attribute_group modemctl_group = {
	.attrs = modemctl_attributes,
};

void crash_event(int type)
{
	char *envs[2] = { NULL, NULL };

	if (!global_mc)
		return;
#if 0
	envs[0] = (type == SVNET_ERROR_CRASH) ? "MAILBOX=cp_exit"
		: "MAILBOX=cp_reset";
#else
	envs[0] = "MAILBOX=lte_reset";
#endif

	printk(KERN_ERR "%s\n", __func__);	
	kobject_uevent_env(&global_mc->dev->kobj, KOBJ_CHANGE, envs);	
}

/*
 * mc_work - PHONE_ACTIVE irq
 *  After
 *	CP Crash : PHONE_ACTIVE(L) + CP_DUMP_INT(H) (0xC9)
 *	CP Reset : PHONE_ACTIVE(L) + CP_DUMP_INT(L) (0xC7)
 *  Before
 *	CP Crash : PHONE_ACTIVE(L) + SUSPEND_REQUEST(H) (0xC9)
 *	CP Reset : PHONE_ACTIVE(L) + SUSPEND_REQUEST(L) (0xC7)
 */
static void mc_work(struct work_struct *work_arg)
{
#if 0 //it will be removed!
	struct modemctl *mc = container_of(work_arg, struct modemctl,
		work.work);
	int error;
	int susp_req;
	char *envs[2] = { NULL, NULL };

	error = modem_get_active(mc);
	if (error < 0) {
		dev_err(mc->dev, "Not initialized\n");
		return;
	}
	susp_req = gpio_get_value(mc->gpio_suspend_request);
	dev_dbg(mc->dev, "PHONE ACTIVE: %d SUSPEND_REQUEST: %d\n",
		error, susp_req);

	envs[0] = susp_req ? "MAILBOX=cp_exit" : "MAILBOX=cp_reset";

	if (error && gpio_get_value(global_mc->gpio_phone_on)) {
		mc->cpcrash_flag = 0;
		kobject_uevent(&mc->dev->kobj, KOBJ_ONLINE);
	} else {
		msleep(300);
		if (!modem_get_active(mc)) {
			mc->cpcrash_flag = 1;
			kobject_uevent_env(&mc->dev->kobj, KOBJ_OFFLINE, envs);
		}
	}
#else
	struct modemctl *mc = container_of(work_arg, struct modemctl,
		work.work);
	int error;
	char *envs[2] = { NULL, NULL };

	error = modem_get_active(mc);
	if (error < 0) {
		dev_err(mc->dev, "Not initialized\n");
		return;
	}
	dev_dbg(mc->dev, "PHONE ACTIVE: %d \n", error);

	envs[0] = error ? "MAILBOX=dump_end" : "MAILBOX=dump_start";

	msleep(300);

	mc->cpcrash_flag = 1;

	//kobject_uevent_env(&mc->dev->kobj, KOBJ_OFFLINE, envs);
	kobject_uevent(&mc->dev->kobj, KOBJ_OFFLINE);    

	if(error)
		printk(KERN_ERR"[%s]%d, lte crash!dump end !!!\n",__func__,__LINE__);
	else
		printk(KERN_ERR"[%s]%d, lte crash!dump start !!!\n",__func__,__LINE__);       

#endif
}

static void mc_resume_worker(struct work_struct *work)
{
	struct modemctl *mc = container_of(work, struct modemctl, resume_work);
	int val = gpio_get_value(mc->gpio_host_wakeup);
	int err;

	dev_dbg(mc->dev, "svn qHOST_WUP:%d\n", val);

	if ((share_svn !=NULL) && (val == HOST_WUP_LEVEL)) {
		err = usbsvn_request_resume();
		if (err < 0)
			dev_err(mc->dev, "request resume failed: %d\n", err);
	}
}

static irqreturn_t modemctl_resume_irq(int irq, void *dev_id)
{
	struct modemctl *mc = (struct modemctl *)dev_id;
	int val = gpio_get_value(mc->gpio_host_wakeup);

	printk(KERN_INFO "< H- WUP %d (S %d)\n", val, mc_is_slave_wakeup());
		
	if (val != HOST_WUP_LEVEL) {
		if (mc->l2_done) {
			complete(mc->l2_done);
			mc->l2_done = NULL;
		}

		if (gpio_get_value(mc->gpio_host_active) != 0) {
			gpio_set_value(mc->gpio_slave_wakeup, 1);
			printk(KERN_INFO "> S- WUP %d\n", gpio_get_value(mc->gpio_slave_wakeup));
		} 

/* WJ 0413 delete old senario */
#if 0
		gpio_set_value(mc->gpio_slave_wakeup, 0);
		printk(KERN_INFO "> S- WUP %d\n", gpio_get_value(mc->gpio_slave_wakeup));
#endif
		return IRQ_HANDLED;
	}
	/* WJ 0413 */
#if 0
	else {
		if(gpio_get_value(mc->gpio_slave_wakeup) == 0) {
			gpio_set_value(mc->gpio_slave_wakeup, 1);
			dev_dbg(mc->dev, "> S- WUP %d\n", gpio_get_value(mc->gpio_slave_wakeup));
		}	
	}
#endif

	if (!work_pending(&mc->resume_work))
		schedule_work(&mc->resume_work);

	return IRQ_HANDLED;
}

static irqreturn_t modemctl_irq_handler(int irq, void *dev_id)
{
	struct modemctl *mc = (struct modemctl *)dev_id;
#if 0
	if(system_rev <= 0xA){         
	    if (ignore_irq_count){
			printk(KERN_ERR "[%s]%d, LTE2AP_STATUS irq On!But, it will be ignored in booting time!----ignore_irq_count = %d\n",
                                           __func__,__LINE__, ignore_irq_count);                   
		ignore_irq_count --;
		return IRQ_HANDLED;
              }
	}
         else if(system_rev > 0xA){
	    if (ignore_irq_count_06){
			printk(KERN_ERR "[%s]%d, LTE_ACTIVE irq On!But, it will be ignored in booting time!----ignore_irq_count_06 = %d\n",
                                           __func__,__LINE__, ignore_irq_count_06);                   
		ignore_irq_count_06 --;
		return IRQ_HANDLED;
              }            
         }
#else
         if (ignore_irq_count){
		printk(KERN_ERR "[%s]%d, LTE_ACTIVE irq On!But, it will be ignored in booting time!----ignore_irq_count = %d\n",
                                           __func__,__LINE__, ignore_irq_count);                   
		ignore_irq_count --;
		return IRQ_HANDLED;
         }
#endif

         printk(KERN_INFO "[%s]%d, system_rev = %x\n",__func__, __LINE__, system_rev);

	if (!work_pending(&mc->work.work))
		schedule_delayed_work(&mc->work, 20); /*1s*/
		/*schedule_work(&mc->work);*/

	return IRQ_HANDLED;
}

static void _free_all(struct modemctl *mc)
{
	int i;

	if (mc) {
		if (mc->ops)
			mc->ops = NULL;

		if (mc->group)
			sysfs_remove_group(&mc->dev->kobj, mc->group);

		for (i = 0; i < ARRAY_SIZE(mc->irq); i++) {
			if (mc->irq[i])
				free_irq(mc->irq[i], mc);
		}

		kfree(mc);
	}
}

static int __devinit modemctl_probe(struct platform_device *pdev)
{
	struct modemctl_platform_data *pdata = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct modemctl *mc;
	int irq;
	int error;

	if (!pdata) {
		dev_err(dev, "No platform data\n");
		return -EINVAL;
	}

	mc = kzalloc(sizeof(struct modemctl), GFP_KERNEL);
	if (!mc) {
		dev_err(dev, "Failed to allocate device\n");
		return -ENOMEM;
	}

	mc->gpio_phone_on = pdata->gpio_phone_on;
	mc->gpio_phone_off = pdata->gpio_phone_off;
	mc->gpio_cp_reset = pdata->gpio_cp_reset;
	mc->gpio_slave_wakeup = pdata->gpio_slave_wakeup;
	mc->gpio_host_wakeup = pdata->gpio_host_wakeup;
	mc->gpio_host_active = pdata->gpio_host_active;
//	mc->gpio_pda_active = pdata->gpio_pda_active;

	if( !pdata->gpio_phone_active ){
                if( system_rev > 0xA ){
		     mc->gpio_phone_active = GPIO_LTE_ACTIVE;        
		}
                  else{
                        printk("[%s], %d LTE_ACTIVE gpio is replaced as  LTE2AP_STATUS in this HW rev(system_rev = %x)\n",__func__, __LINE__, system_rev);
		     mc->gpio_phone_active = GPIO_LTE2AP_STATUS;
                  }

                  pdata->gpio_phone_active = mc->gpio_phone_active;
	}

	mc->ops = &pdata->ops;
	mc->dev = dev;
	dev_set_drvdata(mc->dev, mc);

	error = sysfs_create_group(&mc->dev->kobj, &modemctl_group);
	if (error) {
		dev_err(dev, "Failed to create sysfs files\n");
		goto fail;
	}
	mc->group = &modemctl_group;

	INIT_DELAYED_WORK(&mc->work, mc_work); 
	INIT_WORK(&mc->resume_work, mc_resume_worker);

	mc->ops->modem_cfg_gpio();

	irq = gpio_to_irq(pdata->gpio_phone_active);
	error = request_irq(irq, modemctl_irq_handler,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			"phone_active", mc);
	if (error) {
		dev_err(dev, "Failed to allocate an interrupt(%d)\n", irq);
		goto fail;
	}
	mc->irq[0] = irq; 
	//enable_irq_wake(irq);
	disable_irq(irq);

	irq = gpio_to_irq(pdata->gpio_host_wakeup);
	error = request_irq(irq, modemctl_resume_irq,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			"IPC_HOST_WAKEUP", mc);

	if (error) {
		dev_err(dev, "Failed to allocate an interrupt(%d)\n", irq);
		goto fail;
	}
	mc->irq[1] = irq;
	enable_irq_wake(irq);

	device_init_wakeup(&pdev->dev, pdata->wakeup);
	platform_set_drvdata(pdev, mc);
	global_mc = mc;
	return 0;

fail:
	_free_all(mc);
	return error;
}

static int __devexit modemctl_remove(struct platform_device *pdev)
{
	struct modemctl *mc = platform_get_drvdata(pdev);

	flush_work(&mc->work.work);
	flush_work(&mc->resume_work);
	platform_set_drvdata(pdev, NULL);
	_free_all(mc);
	return 0;
}

static int modemctl_shutdown(struct platform_device *pdev)
{
	struct modemctl *mc = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	if (mc_is_modem_on()) {
		printk(KERN_ERR "%s %d\n", __func__, __LINE__);
		disable_irq(mc->irq[0]);
		disable_irq(mc->irq[1]);
	}	

         modemctl_shutdown_flag = 1;

	if (mc->ops && mc->ops->modem_off)
		mc->ops->modem_off(mc);
}

#ifdef CONFIG_PM
static int modemctl_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct modemctl *mc = platform_get_drvdata(pdev);

printk(KERN_INFO "%s IN\n", __func__);

	if (mc->ops && mc->ops->modem_suspend)
		mc->ops->modem_suspend(mc);

	if (device_may_wakeup(dev) && mc_is_modem_on())
		enable_irq_wake(mc->irq[1]);

	return 0;
}

static int modemctl_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct modemctl *mc = platform_get_drvdata(pdev);

	if (device_may_wakeup(dev) && mc_is_modem_on())
		disable_irq_wake(mc->irq[1]);

	if (mc->ops && mc->ops->modem_resume)
		mc->ops->modem_resume(mc);

	return 0;
}

static const struct dev_pm_ops modemctl_pm_ops = {
	.suspend	= modemctl_suspend,
	.resume		= modemctl_resume,
};
#endif

static struct platform_driver modemctl_driver = {
	.probe		= modemctl_probe,
	.remove		= __devexit_p(modemctl_remove),
	.shutdown   = modemctl_shutdown,
	.driver		= {
		.name	= "modemctl",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &modemctl_pm_ops,
#endif
	},
};

static int __init modemctl_init(void)
{
	int retval;
	retval = platform_device_register(&modemctl);
	if (retval < 0)
		return retval;
	return platform_driver_register(&modemctl_driver);
}

static void __exit modemctl_exit(void)
{
	platform_driver_unregister(&modemctl_driver);
}

module_init(modemctl_init);
module_exit(modemctl_exit);

MODULE_DESCRIPTION("Modem control driver");
MODULE_AUTHOR("Suchang Woo <suchang.woo@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:modemctl");
