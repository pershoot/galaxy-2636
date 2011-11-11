/*
 * driver/misc/smdctl/smd_ctl.c
 *
 * driver supporting miscellaneous functions for Samsung P3 device
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/kobject.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/irq.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/mutex.h>
#include <linux/gfp.h>

#include <linux/file.h>
#include <linux/uaccess.h>
#include <linux/fcntl.h>
#include <linux/ioctl.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/reboot.h>
#include <linux/kthread.h>

#include "../smd-hsic/smd_hsic.h"
#include "smd_ctl.h"
#include <linux/kernel_sec_common.h>
#if defined(CONFIG_MACH_SAMSUNG_P5)
#include <linux/regulator/consumer.h>
#endif

#undef pr_debug
#define pr_debug pr_info

static struct completion *g_L2complete;
static int g_pm_status;
struct str_smdctl *gsmdctl;

static const struct str_smd_gpio g_gpio[SMD_GPIO_MAX] = {
	{"cp_act", GPIO_OUT, LOW, IRQF_TRIGGER_RISING},
	{"sim_det", GPIO_IN, LOW, IRQF_TRIG_BOTH_EDGE},
	{"cp_rst", GPIO_OUT, LOW, IRQF_TRIG_BOTH_EDGE},
	{"cp_on", GPIO_OUT, LOW, IRQF_TRIG_BOTH_EDGE},
	{"ap_act", GPIO_OUT, LOW, IRQF_TRIG_BOTH_EDGE},
	{"hsic_act", GPIO_OUT, LOW, IRQF_TRIG_BOTH_EDGE},
	{"hsic_sus", GPIO_OUT, LOW, IRQF_TRIG_BOTH_EDGE},
	{"hsic_en", GPIO_OUT, LOW, IRQF_TRIGGER_HIGH},
	{"cp_req", GPIO_OUT, LOW, IRQF_TRIGGER_HIGH},
	{"slv_wkp", GPIO_OUT, LOW, IRQF_TRIGGER_HIGH},
	{"hst_wkp", GPIO_OUT, LOW, IRQF_TRIGGER_HIGH},
};

int usb_runtime_pm_ap_initiated_L2 = 1;
EXPORT_SYMBOL(usb_runtime_pm_ap_initiated_L2);

int smdctl_set_pm_status(unsigned int status)
{
	/* this function should be protected by semaphore/atomic value */
	pr_debug("SMD CUR_PM -> %d\n", status);

	g_pm_status = status;
	return 0;
}
EXPORT_SYMBOL(smdctl_set_pm_status);

static unsigned int smdctl_get_pm_status(void)
{
	/* this function should be protected by semaphore/atomic value */
	return g_pm_status;
}

int smdctl_request_slave_wakeup(struct completion *done)
{
	unsigned long flags;
	struct str_ctl_gpio *gpio = gsmdctl->gpio;

	if (gpio_get_value(gpio[SMD_GPIO_CP_ACT].num) == LOW) {
		pr_err("%s : modem. not connected\n", __func__);
		spin_lock_irqsave(&gsmdctl->lock, flags);
		g_L2complete = NULL;
		spin_unlock_irqrestore(&gsmdctl->lock, flags);
		return -ENOTCONN;
	}

	if (gpio_get_value(gpio[SMD_GPIO_HST_WKP].num) == LOW) {
		pr_debug("%s: host wkp level LOW return 1\n", __func__);
		if (gsmdctl->pm_kthread > 0)
			wake_up_process(gsmdctl->pm_kthread);
		spin_lock_irqsave(&gsmdctl->lock, flags);
		g_L2complete = NULL;
		spin_unlock_irqrestore(&gsmdctl->lock, flags);
		return 1;
	}

	spin_lock_irqsave(&gsmdctl->lock, flags);
	g_L2complete = done;
	spin_unlock_irqrestore(&gsmdctl->lock, flags);

	pr_debug("%s: smdctl->host wake = %d, pm status = %d\n", __func__,
		gsmdctl->host_wake,
		smdctl_get_pm_status());

	if (done == NULL) {
		pr_debug("SMD SLV_WKP -> 0\n");
		gpio_set_value(gpio[SMD_GPIO_SLV_WKP].num, LOW);
		return 0;
	}

	if (gpio_get_value(gpio[SMD_GPIO_SLV_WKP].num) == HIGH) {
		pr_debug("SMD SLV_WKP -> 0\n");
		gpio_set_value(gpio[SMD_GPIO_SLV_WKP].num, LOW);
		usleep_range(10000, 20000);
	}
	pr_debug("SMD SLV_WKP -> 1\n");
	gpio_set_value(gpio[SMD_GPIO_SLV_WKP].num, HIGH);

	return 0;
}
EXPORT_SYMBOL_GPL(smdctl_request_slave_wakeup);

static void smd_sus_req_worker(struct work_struct *work)
{
	struct str_smdctl *smdctl =
	    container_of(work, struct str_smdctl, sus_req_work.work);
	struct str_ctl_gpio *gpio = smdctl->gpio;

	if (smdctl->cp_stat != CP_PWR_ON)
		return;

	if (gpio_get_value(gpio[SMD_GPIO_HSIC_SUS].num))
		smdhsic_pm_suspend();
}

static irqreturn_t irq_sus_request(int irq, void *dev_id)
{
	int int_val;

	struct str_smdctl *smdctl = (struct str_smdctl *)dev_id;
	struct str_ctl_gpio *gpio = smdctl->gpio;

	if (smdctl->cp_stat != CP_PWR_ON)
		return IRQ_HANDLED;

	int_val = gpio_get_value(gpio[SMD_GPIO_HSIC_SUS].num);
	pr_debug("%s: val:%d cur:%d\n", __func__, int_val,
		 smdctl_get_pm_status());

	if (int_val) {
		if (!work_pending(&smdctl->sus_req_work.work))
			schedule_delayed_work(&smdctl->sus_req_work,
					msecs_to_jiffies(20));
	}

	return IRQ_HANDLED;
}

static int smdctl_request_suspend_req(struct str_smdctl *smdctl)
{
	int ret;
	struct str_ctl_gpio *gpio = smdctl->gpio;

	ret = request_irq(gpio[SMD_GPIO_HSIC_SUS].irq,
			irq_sus_request,
			IRQF_TRIG_BOTH_EDGE,
			gpio[SMD_GPIO_HSIC_SUS].name,
			(void *)smdctl);
	if (ret) {
		pr_err("%s fail to request irq\n", __func__);
		return ret;
	}

	INIT_DELAYED_WORK(&smdctl->sus_req_work, smd_sus_req_worker);
	return ret;
}

static void smd_cp_active_worker(struct work_struct *work)
{
	struct str_smdctl *smdctl =
	    container_of(work, struct str_smdctl, cp_active_work.work);
	struct str_ctl_gpio *gpio = smdctl->gpio;

	if (smdctl->shutdown_called)
		return;

	if (gpio_get_value(gpio[SMD_GPIO_CP_ACT].num) == LOW) {
		smdctl->uevent_envs[0] =
			!gpio_get_value(gpio[SMD_GPIO_HSIC_SUS].num) ?
					"MAILBOX=cp_exit" : "MAILBOX=cp_reset";
		kobject_uevent_env(&smdctl->dev->kobj,
					KOBJ_OFFLINE, smdctl->uevent_envs);
		pr_err("smd: sent uevnet(%s)\n", smdctl->uevent_envs[0]);
		/* guide time to modem boot */
		wake_lock_timeout(&smdctl->ctl_wake_lock, HZ*20);
	}
}

static irqreturn_t irq_cp_act(int irq, void *dev_id)
{
	int cp_act;
	struct str_smdctl *smdctl = (struct str_smdctl *)dev_id;
	struct str_ctl_gpio *gpio = smdctl->gpio;

	cp_act = gpio_get_value(gpio[SMD_GPIO_CP_ACT].num);
	pr_info("%s: %d\n", __func__, cp_act);

	if (cp_act) {
		if (smdctl->cp_stat == CP_PWR_OFF) {
			pr_err("%s: CP Power On success!!!\n",
			       __func__);
			smdctl->cp_stat = CP_PWR_ON;
			smdctl->sim_reference_level =
				gpio_get_value(gpio[SMD_GPIO_SIM_DET].num);
		} else
			pr_err("%s : Invalid status :%d\n", __func__,
			       smdctl->cp_stat);
	} else {
		if (smdctl->cp_stat == CP_PWR_ON) {
			pr_err("%s: CP Power OFF success!!!\n",
			       __func__);
			smdctl->cp_stat = CP_PWR_OFF;
			/* active level debounce, check after 2 sec. */
			schedule_delayed_work(&smdctl->cp_active_work, HZ*2);
			/* guide time to modem boot */
			wake_lock_timeout(&smdctl->ctl_wake_lock, HZ*20);
		} else
			pr_err("%s : Invalid status : %d\n", __func__,
			       smdctl->cp_stat);
	}

	return IRQ_HANDLED;
}

static int smdctl_request_cp_active(struct str_smdctl *smdctl)
{
	int ret;
	struct str_ctl_gpio *gpio = smdctl->gpio;

	gpio_direction_input(gpio[SMD_GPIO_CP_ACT].num);
	ret = request_irq(gpio[SMD_GPIO_CP_ACT].irq,
			irq_cp_act,
			IRQF_TRIG_BOTH_EDGE,
			gpio[SMD_GPIO_CP_ACT].name,
			(void *)smdctl);
	if (ret) {
		pr_err("%s fail to request irq\n", __func__);
		return ret;
	}

	INIT_DELAYED_WORK(&smdctl->cp_active_work, smd_cp_active_worker);

	return ret;
}

void smdctl_request_connection_recover(bool reconnection)
{
	if (gsmdctl->shutdown_called)
		return;

	if (reconnection) {
		pr_info("%s:reconnection\n", __func__);
		if (gpio_get_value(gsmdctl->gpio[SMD_GPIO_CP_ACT].num)) {
			gsmdctl->uevent_envs[0] ="MAILBOX=cp_reset";
			kobject_uevent_env(&gsmdctl->dev->kobj, KOBJ_OFFLINE,
							gsmdctl->uevent_envs);
			pr_info("send uevent[%s]\n", gsmdctl->uevent_envs[0]);
			/* guide time to modem boot */
			wake_lock_timeout(&gsmdctl->ctl_wake_lock, HZ*20);
		} else
			pr_info("phone active low, let it handle at IRQ\n");
	} else {
		gsmdctl->modem_uevent_requested = false;

		if (gsmdctl->cp_stat == CP_PWR_ON &&
			gpio_get_value(gsmdctl->gpio[SMD_GPIO_CP_ACT].num)) {
				if (gsmdctl->modem_reset_remained) {
					pr_info("%s:delayed reset\n", __func__);
					schedule_delayed_work(&gsmdctl->modem_reset_work,
								msecs_to_jiffies(500));
					/* guide time to modem boot */
					wake_lock_timeout(&gsmdctl->ctl_wake_lock, HZ*20);
				}
				gsmdctl->modem_reset_remained = false;
		}
	}
}
EXPORT_SYMBOL(smdctl_request_connection_recover);

void smdctl_reenumeration_control(void)
{
	/* reenumeration gpio control */
	pr_debug("SMD HSIC ACT -> 0\n");
	gpio_set_value(gsmdctl->gpio[SMD_GPIO_HSIC_ACT].num, 0);
	pr_debug("SMD PDA ACT -> 0\n");
	gpio_set_value(gsmdctl->gpio[SMD_GPIO_AP_ACT].num, 0);
	/* wait modem idle */
	msleep(30);

	pr_debug("SMD PDA ACT -> 1\n");
	gpio_set_value(gsmdctl->gpio[SMD_GPIO_AP_ACT].num, 1);
	usleep_range(10000, 10000);
	pr_debug("SMD SLV WKP -> 1\n");
	gpio_set_value(gsmdctl->gpio[SMD_GPIO_SLV_WKP].num, 1);
	usleep_range(10000, 10000);
	pr_debug("SMD SLV WKP -> 0\n");
	gpio_set_value(gsmdctl->gpio[SMD_GPIO_SLV_WKP].num, 0);
	usleep_range(10000, 10000);

	pr_debug("SMD HSIC ACT -> 1\n");
	gpio_set_value(gsmdctl->gpio[SMD_GPIO_HSIC_ACT].num, 1);
}
EXPORT_SYMBOL(smdctl_reenumeration_control);

static void smd_modem_reset_worker(struct work_struct *work)
{
	int retval;
	struct str_smdctl *smdctl =
	    container_of(work, struct str_smdctl, modem_reset_work.work);

	if (smdctl->cp_stat == CP_PWR_OFF || smdctl->shutdown_called)
		return;

	pr_info("%s:uevent\n", __func__);
	retval = kobject_uevent_env(&smdctl->dev->kobj, KOBJ_OFFLINE,
							smdctl->uevent_envs);
	if (retval != 0)
		pr_err("Error: kobject_uevent %d\n", retval);

	/* guide time to modem boot */
	wake_lock_timeout(&smdctl->ctl_wake_lock, HZ*20);
}

static void smd_request_uevent(struct str_smdctl *smdctl)
{
	int retval;

	if (smdctl->shutdown_called)
		return;

	if (!smdctl->modem_uevent_requested) {
		smdctl->modem_uevent_requested = true;
		retval = kobject_uevent_env(&smdctl->dev->kobj, KOBJ_OFFLINE,
					smdctl->uevent_envs);
		if (retval != 0)
			pr_err("Error: kobject_uevent %d\n",
				retval);
		/* guide time to modem boot */
		wake_lock_timeout(&smdctl->ctl_wake_lock,
				HZ*20);
	} else {
		pr_info("%s:delayed\n", __func__);
		smdctl->modem_reset_remained = true;
	}
}

#ifndef CONFIG_NOT_SUPPORT_SIMDETECT
static void smd_sim_detect_worker(struct work_struct *work)
{
	struct str_smdctl *smdctl =
	    container_of(work, struct str_smdctl, sim_detect_work.work);
	struct str_ctl_gpio *gpio = &smdctl->gpio[SMD_GPIO_SIM_DET];

	pr_info("SIM DETECT : LEVEL(%d)\n",
		gpio_get_value(gpio->num));

	if (smdctl->cp_stat == CP_PWR_ON) {
		if (smdctl->sim_reference_level == LOW) {
			if (gpio_get_value(gpio->num) == HIGH) {
				pr_info("sim detach event\n");
				smdctl->sim_reference_level =
					gpio_get_value(gpio->num);
				smdctl->uevent_envs[0] = "MAILBOX=sim_detach";
				smd_request_uevent(smdctl);
			}
		} else {
			if (gpio_get_value(gpio->num) == LOW) {
				pr_info("sim attach event\n");
				smdctl->sim_reference_level =
					gpio_get_value(gpio->num);
				smdctl->uevent_envs[0] = "MAILBOX=sim_attach";
				smd_request_uevent(smdctl);
			}
		}
	} else {
		if (smdctl->sim_reference_level == LOW) {
			if (gpio_get_value(gpio->num) == HIGH) {
				pr_info("sim detach event unhandled\n");
				smdctl->sim_reference_level =
					gpio_get_value(gpio->num);
				smdctl->uevent_envs[0] = "MAILBOX=sim_detach";
				smdctl->modem_reset_remained = true;
			}
		} else {
			if (gpio_get_value(gpio->num) == LOW) {
				pr_info("sim attach event unhandled\n");
				smdctl->sim_reference_level =
					gpio_get_value(gpio->num);
				smdctl->uevent_envs[0] = "MAILBOX=sim_attach";
				smdctl->modem_reset_remained = true;
			}
		}
	}
}

static irqreturn_t irq_sim_detect(int irq, void *dev_id)
{
	struct str_smdctl *smdctl = (struct str_smdctl *)dev_id;

	/* sim detect pin debounce time for 500ms */
	wake_lock_timeout(&smdctl->ctl_wake_lock, HZ);
	schedule_delayed_work(&smdctl->sim_detect_work,	msecs_to_jiffies(500));

	return IRQ_HANDLED;
}

static int smdctl_request_sim_detect(struct str_smdctl *smdctl)
{
	int ret;
	struct str_ctl_gpio *gpio = smdctl->gpio;

	gpio_direction_input(gpio[SMD_GPIO_SIM_DET].num);
	ret = request_irq(gpio[SMD_GPIO_SIM_DET].irq,
			irq_sim_detect,
			IRQF_TRIG_BOTH_EDGE,
			gpio[SMD_GPIO_SIM_DET].name,
			(void *)smdctl);
	if (ret) {
		pr_err("%s fail to request irq\n", __func__);
		return ret;
	}

	INIT_DELAYED_WORK(&smdctl->sim_detect_work, smd_sim_detect_worker);

	return ret;
}
#endif

static irqreturn_t irq_host_wakeup(int irq, void *dev_id)
{
	int hst_wkp;
	unsigned long flags;
	struct str_smdctl *smdctl = (struct str_smdctl *)dev_id;
	struct str_ctl_gpio *gpio = smdctl->gpio;

	hst_wkp = gpio_get_value(gpio[SMD_GPIO_HST_WKP].num);
	pr_debug("SMD HST_WKP -> %d\n", hst_wkp);

	if (smdctl->cp_stat != CP_PWR_ON)
		return IRQ_HANDLED;

	if (!hst_wkp) {
		if (smdctl->host_wake == CP_SUS_UNDEFIND_STAT) {
			pr_info("HOST_WAKE_RESET detected\n");
			smdctl->host_wake = CP_SUS_RESET;
			smdctl_set_pm_status(PM_STATUS_L0);

		} else if (smdctl->host_wake == CP_SUS_RESET) {
			if (smdctl_get_pm_status() == PM_STATUS_L3) {
				pr_debug("L3 -> L0\n");
				gpio_set_value(gpio[SMD_GPIO_HSIC_ACT].num,
					HIGH);
				pr_debug("SMD HSIC_ACT -> 1\n");
				smdctl_set_pm_status(PM_STATUS_L3_L0);
			} else if (smdctl->dev->power.status == DPM_ON)
				if (smdctl->pm_kthread > 0)
					wake_up_process(smdctl->pm_kthread);
		} else
			smdctl->host_wake = CP_SUS_HIGH;

	} else {
		if (smdctl->host_wake == CP_SUS_UNDEFIND_STAT) {
			/* do not change wake state */
			pr_debug("%s:host wake high during undefined state\n",
				__func__);
		} else if (smdctl->host_wake == CP_SUS_RESET) {
			gpio = &smdctl->gpio[SMD_GPIO_SLV_WKP];
			spin_lock_irqsave(&gsmdctl->lock, flags);
			if (g_L2complete) {
				pr_debug("do complete at hst_wkp high\n");
				complete(g_L2complete);
				gpio_set_value(gpio->num, LOW);
				pr_debug("SMD SLV_WKP -> 0\n");
				g_L2complete = NULL;

			} else if (smdctl_get_pm_status() == PM_STATUS_L3_L0) {
				pr_debug("L3 -> L0#\n");
				if (gpio_get_value(gpio->num) == HIGH) {
					gpio_set_value(gpio->num, LOW);
					pr_debug("SMD SLV_WKP -> 0\n");
				}
				smdctl_set_pm_status(PM_STATUS_L0);
			}
			spin_unlock_irqrestore(&gsmdctl->lock, flags);
		} else
			smdctl->host_wake = CP_SUS_LOW;
	}

	return IRQ_HANDLED;
}

static int smdctl_request_host_wakeup(struct str_smdctl *smdctl)
{
	int ret;
	struct str_ctl_gpio *gpio = smdctl->gpio;

	gpio_direction_input(gpio[SMD_GPIO_HST_WKP].num);
	ret = request_irq(gpio[SMD_GPIO_HST_WKP].irq,
			irq_host_wakeup,
			IRQF_TRIG_BOTH_EDGE,
			gpio[SMD_GPIO_HST_WKP].name,
			(void *)smdctl);
	if (ret)
		pr_err("%s fail to request irq\n", __func__);

	return ret;
}

/* To protect reverse bias */
/* reverse bias makes modem go unstable power state during boot */
static int smdctl_rest_init(struct str_smdctl *smdctl)
{
	int ret;
	pr_debug("rest called\n");

	if (smdctl->rest_init_done)
		return 0;

	ret = smdctl_request_host_wakeup(smdctl);
	if (ret)
		goto err_ret;
	gpio_direction_input(smdctl->gpio[SMD_GPIO_HSIC_SUS].num);
	if (!usb_runtime_pm_ap_initiated_L2) {
		ret = smdctl_request_suspend_req(smdctl);
		if (ret)
			goto err1;
	}
	ret = smdctl_request_cp_active(smdctl);
	if (ret)
		goto err2;
#ifndef CONFIG_NOT_SUPPORT_SIMDETECT
	ret = smdctl_request_sim_detect(smdctl);
	if (ret)
		goto err3;
#endif
	smdctl->rest_init_done = true;

	INIT_DELAYED_WORK(&smdctl->modem_reset_work, smd_modem_reset_worker);

	return 0;
#ifndef CONFIG_NOT_SUPPORT_SIMDETECT
err3:
	free_irq(smdctl->gpio[SMD_GPIO_CP_ACT].irq, (void *)smdctl);
#endif
err2:
	if (!usb_runtime_pm_ap_initiated_L2)
		free_irq(smdctl->gpio[SMD_GPIO_HSIC_SUS].irq, (void *)smdctl);
err1:
	free_irq(smdctl->gpio[SMD_GPIO_HST_WKP].irq, (void *)smdctl);
err_ret:
	return ret;

}

static int smdctl_hsic_pm_resume_kthread(void *data)
{
	int val;
	struct str_smdctl *smdctl = (struct str_smdctl *)data;
	struct str_ctl_gpio *gpio = smdctl->gpio;

	pr_info("%s\n", __func__);

	while(!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
wait_suspend:
		if (kthread_should_stop()) {
			pr_debug("stop kthread pm\n");
			break;
		}
		val = gpio_get_value(gpio[SMD_GPIO_HST_WKP].num);
		if (!val && gpio_get_value(gpio[SMD_GPIO_CP_ACT].num)) {
			pr_debug("run kthread pm\n");
			if (smdctl->usb_ignore_suspending_hstwkp
				|| smdctl->dev->power.status == DPM_ON) {
				if (smdhsic_pm_resume() < 0)
					pr_err("%s: Resume failed\n", __func__);
			} else {
				msleep(100);
				goto wait_suspend;
			}
		}
		schedule();
	}

	set_current_state(TASK_RUNNING);
	pr_info("exit kthread pm\n");
	return 0;
}

static int xmm6260_off(struct str_smdctl *smdctl)
{
	struct str_ctl_gpio *gpio = smdctl->gpio;

	if (!gpio) {
		pr_err("%s:gpio not allocated\n", __func__);
		return -EINVAL;
	}
	pr_info("%s Start\n", __func__);
	smdctl->cp_stat = CP_PWR_OFF;
	gpio_set_value(gpio[SMD_GPIO_CP_REQ].num, LOW);
	usleep_range(300, 1000);
	gpio_set_value(gpio[SMD_GPIO_CP_RST].num, LOW);
	gpio_set_value(gpio[SMD_GPIO_CP_ON].num, LOW);
	msleep(50);
	gpio_set_value(gpio[SMD_GPIO_AP_ACT].num, LOW);
	gpio_set_value(gpio[SMD_GPIO_HSIC_ACT].num, LOW);
	gpio_set_value(gpio[SMD_GPIO_SLV_WKP].num, LOW);
	return 0;
}

static int xmm6260_on(struct str_smdctl *smdctl)
{
	int ret;
	struct str_ctl_gpio *gpio = smdctl->gpio;
	if (!gpio) {
		pr_err("%s:gpio not allocated\n", __func__);
		return -EINVAL;
	}
	pr_info("%s Start\n", __func__);

	/* ensure xmm6260 off before power on */
	xmm6260_off(smdctl);

	gpio_set_value(gpio[SMD_GPIO_CP_RST].num, HIGH);
	usleep_range(160, 1000);

	gpio_set_value(gpio[SMD_GPIO_CP_REQ].num, HIGH);
	msleep(100);

	gpio_set_value(gpio[SMD_GPIO_CP_ON].num, HIGH);
	msleep(150);

	ret = smdctl_rest_init(smdctl);
	if (ret)
		return ret;

	gpio_set_value(gpio[SMD_GPIO_HSIC_ACT].num, HIGH);
	pr_debug("SMD HSIC_ACT -> 1\n");

	gpio_set_value(gpio[SMD_GPIO_AP_ACT].num, HIGH);
	pr_debug("SMD AP_ACT -> 1\n");

	return 0;
}

static int xmm6260_reset(struct str_smdctl *smdctl)
{
	int ret;
	struct str_ctl_gpio *gpio = smdctl->gpio;
	if (!gpio) {
		pr_err("%s:gpio not allocated\n", __func__);
		return -EINVAL;
	}
	pr_info("%s Start\n", __func__);

	/* set low AP-CP GPIO befor warm reset */
	gpio_set_value(gpio[SMD_GPIO_CP_ACT].num, LOW);
	gpio_set_value(gpio[SMD_GPIO_HSIC_ACT].num, LOW);
	gpio_set_value(gpio[SMD_GPIO_HSIC_SUS].num, LOW);
	msleep(100);

	switch (smdctl->cp_reset_cnt) {
	case 0 ... 3:
		pr_info("cp warn reset\n");
		gpio_set_value(gpio[SMD_GPIO_CP_REQ].num, LOW);
		msleep(10);
		gpio_set_value(gpio[SMD_GPIO_CP_RST].num, HIGH);
		usleep_range(160, 1000);
		gpio_set_value(gpio[SMD_GPIO_CP_REQ].num, HIGH);
		break;

	case 4 ... 9:
		pr_info("cp PMU reset\n");
		gpio_set_value(gpio[SMD_GPIO_CP_REQ].num, LOW);
		msleep(10);
		gpio_set_value(gpio[SMD_GPIO_CP_RST].num, LOW);
		usleep_range(160, 1000);
		gpio_set_value(gpio[SMD_GPIO_CP_RST].num, HIGH);
		usleep_range(160, 1000);
		gpio_set_value(gpio[SMD_GPIO_CP_REQ].num, HIGH);
		break;

	default:
		pr_info("cp off-on\n");
		/* OFF */
		gpio_set_value(gpio[SMD_GPIO_CP_REQ].num, LOW);
		usleep_range(300, 1000);
		gpio_set_value(gpio[SMD_GPIO_CP_RST].num, LOW);
		gpio_set_value(gpio[SMD_GPIO_CP_ON].num, LOW);
		msleep(10 * smdctl->cp_reset_cnt);
		/* ON */
		gpio_set_value(gpio[SMD_GPIO_CP_RST].num, HIGH);
		usleep_range(160, 1000);
		gpio_set_value(gpio[SMD_GPIO_CP_REQ].num, HIGH);
		msleep(100);
		break;
	}
	pr_err("CP_RESET_CNT=%d\n", smdctl->cp_reset_cnt++);
	return 0;
}
#ifdef CONFIG_KERNEL_DEBUG_SEC
/*
 * HSIC CP uploas scenario -
 * 1. CP send Crash message
 * 2. Rild save the ram data to file via HSIC
 * 3. Rild call the kernel_upload() for AP ram dump
 */
static void kernel_upload(void)
{
	/*TODO: check the DEBUG LEVEL*/
	/* panic("CP Crash"); */
	kernel_sec_set_upload_cause(UPLOAD_CAUSE_CP_ERROR_FATAL);
#if defined(CONFIG_MACH_SAMSUNG_P5)
	struct regulator *reg;

	reg = regulator_get(NULL, "vdd_ldo4");
	if (IS_ERR_OR_NULL(reg))
		pr_err("%s: couldn't get regulator vdd_ldo4\n", __func__);
	else {
		regulator_enable(reg);
		pr_debug("%s: enabling regulator vdd_ldo4\n", __func__);
		regulator_put(reg);
	}
#endif
	kernel_sec_hw_reset(false);
	emergency_restart();
}
#endif


static int smdctl_open(struct inode *inode, struct file *file)
{
	int r = 0;
	struct cdev *cdev = inode->i_cdev;
	struct str_smdctl *smdctl = container_of(cdev, struct str_smdctl, cdev);

	if (smdctl->shutdown_called)
		return -EBUSY;

	/* guide time to modem boot */
	wake_lock_timeout(&smdctl->ctl_wake_lock, HZ*20);

	pr_info("%s\n", __func__);
	if (atomic_cmpxchg(&smdctl->refcount, 0, 1) != 0) {
		pr_err("%s : already opend..\n", __func__);
		return -EBUSY;
	}

	file->private_data = smdctl;

	return r;
}

static int smdctl_release(struct inode *inode, struct file *file)
{
	struct str_smdctl *smdctl = (struct str_smdctl *)file->private_data;

	pr_info("%s\n", __func__);

	atomic_dec(&smdctl->refcount);
	return 0;
}

static long smdctl_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int r = 0;
	unsigned long flags;
	struct str_smdctl *smdctl = (struct str_smdctl *)file->private_data;
	struct str_ctl_gpio *gpio = smdctl->gpio;

	if (smdctl->shutdown_called) {
		/* after shutdown ignore every ioctl request */
		pr_debug("%s:ioctl blocked\n", __func__);
		return 0;
	}

	printk(KERN_ERR "ioctl code = %x\n", cmd);

	switch (cmd) {
	case IOCTL_CP_ON:
		pr_info("IOCTL_CP_ON\n");
		smdctl->host_wake = CP_SUS_UNDEFIND_STAT;

		/* guide time to modem boot */
		wake_lock_timeout(&smdctl->ctl_wake_lock, HZ*20);

		/* start hsic resume kernel thread */
		smdctl->pm_kthread  = kthread_create(
					smdctl_hsic_pm_resume_kthread,
					(void *)smdctl,
					"smdctld");
		if (IS_ERR_OR_NULL(smdctl->pm_kthread))
			return PTR_ERR(smdctl->pm_kthread);
		else
			wake_up_process(smdctl->pm_kthread);

		r = xmm6260_on(smdctl);
		if (r) {
			pr_err("%s:_cp_on() failed(%d)\n", __func__,
			       r);
		}
		break;

	case IOCTL_CP_OFF:
		pr_info("IOCTL_CP_OFF\n");

		/* fix timeout recovery lockup */
		spin_lock_irqsave(&gsmdctl->lock, flags);
		g_L2complete = NULL;
		spin_unlock_irqrestore(&gsmdctl->lock, flags);

		/* stop hsic resume kernel thread */
		if (!IS_ERR_OR_NULL(smdctl->pm_kthread)) {
			kthread_stop(smdctl->pm_kthread);
			smdctl->pm_kthread = NULL;
		}

		r = xmm6260_off(smdctl);
		if (r) {
			pr_err("%s:_cp_on() failed(%d)\n", __func__,
			       r);
		}
		break;

	case IOCTL_CP_RESET:
		pr_info("IOCTL_CP_RESET\n");
		xmm6260_reset(smdctl);
		break;

	case IOCTL_ON_HSIC_ACT:
		pr_info("IOCTL_ON_HSIC_ACT\n");
		gpio_set_value(gpio[SMD_GPIO_HSIC_ACT].num, HIGH);
		break;

	case IOCTL_OFF_HSIC_ACT:
		pr_info("IOCTL_OFF_HSIC_ACT\n");
		gpio_set_value(gpio[SMD_GPIO_HSIC_ACT].num, LOW);
		break;

	case IOCTL_GET_HOST_WAKE:
		r = smdctl->host_wake;
		pr_info("%s current wake_stat : %d\n", __func__, r);
		break;

	case IOCTL_ENABLE_HSIC_EN:
		pr_info("IOCTL_ENABLE_HSIC_EN\n");
		gpio_set_value(gpio[SMD_GPIO_HSIC_EN].num, HIGH);
		break;

	case IOCTL_DISABLE_HSIC_EN:
		pr_info("IOCTL_DISABLE_HSIC_EN\n");
		gpio_set_value(gpio[SMD_GPIO_HSIC_EN].num, LOW);
		break;
#ifdef CONFIG_KERNEL_DEBUG_SEC
	case IOCTL_CP_UPLOAD:
		pr_info("IOCTL_CP_UPLOAD\n");
		kernel_upload();
		break;
#endif
	default:
		pr_err("%s : Invalid cmd : %x", __func__, cmd);
		return -EINVAL;
	}
	return r;
}

static const struct file_operations smdctl_fops = {
	.owner = THIS_MODULE,
	.open = smdctl_open,
	.release = smdctl_release,
	.unlocked_ioctl = smdctl_ioctl,
};

static int register_smdctl_dev(struct str_smdctl *smdctl)
{
	int r;
	dev_t devid;

	if (!smdctl)
		return -EINVAL;

	smdctl->class = class_create(THIS_MODULE, SMDCTL_DEVNAME);
	if (IS_ERR(smdctl->class)) {
		r = PTR_ERR(smdctl->class);
		smdctl->class = NULL;
		return r;
	}

	r = alloc_chrdev_region(&devid, SMDCTL_MINOR, 1, SMDCTL_DEVNAME);
	if (r) {
		class_destroy(smdctl->class);
		smdctl->class = NULL;
		return r;
	}

	cdev_init(&smdctl->cdev, &smdctl_fops);

	r = cdev_add(&smdctl->cdev, devid, 1);
	if (r) {
		unregister_chrdev_region(devid, 1);
		class_destroy(smdctl->class);
		smdctl->devid = 0;
		smdctl->class = NULL;
		return r;
	}
	smdctl->devid = devid;

	smdctl->dev =
	    device_create(smdctl->class, NULL, smdctl->devid, NULL,
			  SMDCTL_DEVNAME);
	if (IS_ERR(smdctl->dev)) {
		r = PTR_ERR(smdctl->dev);
		smdctl->dev = NULL;
		cdev_del(&smdctl->cdev);
		unregister_chrdev_region(smdctl->devid, 1);
		class_destroy(smdctl->class);
		smdctl->devid = 0;
		smdctl->class = NULL;
		return r;
	}

	atomic_set(&smdctl->refcount, 0);
	smdctl->cp_stat = CP_PWR_OFF;
	dev_set_drvdata(smdctl->dev, smdctl);


	return 0;
}

static void deregister_smdctl_dev(struct str_smdctl *smdctl)
{
	device_destroy(smdctl->class, smdctl->devid);
	smdctl->dev = NULL;
	cdev_del(&smdctl->cdev);
	unregister_chrdev_region(smdctl->devid, 1);
	class_destroy(smdctl->class);
	smdctl->devid = 0;
	smdctl->class = NULL;
}

static int init_gpio(struct str_smdctl *smdctl, struct platform_device *pdev)
{
	int i, j;
	int status;
	struct resource *res;

	for (i = 0; i < SMD_GPIO_MAX; i++) {
		res =
		    platform_get_resource_byname(pdev, IORESOURCE_IO,
						 g_gpio[i].name);
		if (!res) {
			pr_err("%s:get_resource failed(%s)", __func__,
			       g_gpio[i].name);
			status = -ENXIO;
			goto unregister;
		}
		strncpy(smdctl->gpio[i].name, g_gpio[i].name,
			strlen(g_gpio[i].name));

		smdctl->gpio[i].num = res->start;

		if (!smdctl->gpio[i].num)
			continue;

		status = gpio_request(smdctl->gpio[i].num, g_gpio[i].name);
		if (status) {
			pr_err("%s: gpio_request(%d,\"%s\") failed (%d)\n",
			       __func__, smdctl->gpio[i].num,
				g_gpio[i].name, status);
			goto unregister;
		}
		smdctl->gpio[i].num = res->start;

		if (g_gpio[i].dir == GPIO_OUT)
			gpio_direction_output(smdctl->gpio[i].num,
					      g_gpio[i].initval);
		else
			gpio_direction_input(smdctl->gpio[i].num);

		smdctl->gpio[i].irq = gpio_to_irq(smdctl->gpio[i].num);
	}
	return 0;

unregister:
	for (j = 0; j < i; j++)
		gpio_free(smdctl->gpio[j].num);
	return status;
}

static struct str_smdctl *get_smdctl_addr(struct device *dev)
{
	struct str_smdctl *smdctl;
	struct platform_device *pdev;

	pdev = container_of(dev, struct platform_device, dev);
	if (!pdev)
		return NULL;

	smdctl = (struct str_smdctl *)platform_get_drvdata(pdev);

	return smdctl;
}

static ssize_t smdctl_ref_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int len;
	struct str_smdctl *smdctl = get_smdctl_addr(dev);
	if (!smdctl) {
		pr_err("%s:_get_smd_addr() failed\n", __func__);
		return 0;
	}

	len = sprintf(buf, "smdctl reference count : %d\n",
		atomic_read(&smdctl->refcount));
	return len;
}

static ssize_t smdctl_gpio_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int len = 0;
	int i;
	struct str_smdctl *smdctl;

	smdctl = get_smdctl_addr(dev);
	if (!smdctl) {
		pr_err("%s:_get_smd_addr() failed\n", __func__);
		return 0;
	}

	for (i = 0; i < SMD_GPIO_MAX; i++) {
		if (smdctl->gpio[i].num)
			len += sprintf(buf + len, "name : %s, value : %d\n",
				       smdctl->gpio[i].name,
				       gpio_get_value(smdctl->gpio[i].num));
	}
	return len;
}

static ssize_t modem_stat_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int len = 0;
	struct str_smdctl *smdctl;
	unsigned int status;
	const char str_stat[CP_MAX_STATUS][16]
	= { "CP_ON", "CP_OFF", "CP_ABNORMAL" };

	smdctl = get_smdctl_addr(dev);
	if (!smdctl) {
		pr_err("%s:_get_smd_addr() failed\n", __func__);
		return 0;
	}

	status = smdctl->cp_stat;
	if (status >= CP_MAX_STATUS) {
		pr_err("%s:invalid cp status(%d)\n", __func__, status);
		return len;
	}

	len = sprintf(buf, "current cp status : %s\n", str_stat[status]);
	return len;
}

static struct device_attribute smdctl_ref =
__ATTR(ref, S_IRUGO, smdctl_ref_show, NULL);

static struct device_attribute smdctl_gpio =
__ATTR(gpio, S_IRUGO, smdctl_gpio_show, NULL);

static struct device_attribute modem_stat =
__ATTR(status, S_IRUGO, modem_stat_show, NULL);

static struct attribute *smdctl_sysfs_attrs[] = {
	&modem_stat.attr,
	&smdctl_gpio.attr,
	&smdctl_ref.attr,
	NULL,
};

static struct attribute_group smdctl_sysfs = {
	.name = "smdctl",
	.attrs = smdctl_sysfs_attrs,
};

static int smdctl_reboot_notify(struct notifier_block *this,
					unsigned long code, void *unused)
{
	pr_info("%s(0x%lx)\n", __func__, code);

	if (!gsmdctl)
		return NOTIFY_DONE;

	if (code == SYS_DOWN || code == SYS_HALT || code == SYS_POWER_OFF) {
		gsmdctl->shutdown_called = true;

		if (gsmdctl->rest_init_done) {
#ifndef CONFIG_NOT_SUPPORT_SIMDETECT
			free_irq(gsmdctl->gpio[SMD_GPIO_SIM_DET].irq,
							(void *)gsmdctl);
#endif
			free_irq(gsmdctl->gpio[SMD_GPIO_CP_ACT].irq,
							(void *)gsmdctl);
			if (!usb_runtime_pm_ap_initiated_L2)
				free_irq(gsmdctl->gpio[SMD_GPIO_HSIC_SUS].irq,
							(void *)gsmdctl);
			free_irq(gsmdctl->gpio[SMD_GPIO_HST_WKP].irq,
							(void *)gsmdctl);
		}

		/* power off hsic block */
		pr_info("hsic ldo off\n");
		gpio_set_value(gsmdctl->gpio[SMD_GPIO_HSIC_EN].num, 0);
		mdelay(20);

		/* control modem power off before pmic control */
		pr_info("modem power off\n");
		gpio_set_value(gsmdctl->gpio[SMD_GPIO_CP_REQ].num, 0);
		udelay(500);	/* min 300us */
		gpio_set_value(gsmdctl->gpio[SMD_GPIO_CP_RST].num, 0);
		gpio_set_value(gsmdctl->gpio[SMD_GPIO_CP_ON].num, 0);

		/* wait for disconnect hsic slave modem */
		mdelay(200);
	}

	return NOTIFY_DONE;
}

static struct notifier_block smdctl_reboot_notifier = {
	.notifier_call =	smdctl_reboot_notify,
};

static int __devinit smdctl_probe(struct platform_device *pdev)
{
	int i;
	int r;
	struct str_smdctl *smdctl;
	struct str_ctl_gpio *gpio;

	smdctl = kzalloc(sizeof(*smdctl), GFP_KERNEL);
	if (!smdctl) {
		pr_err("%s:malloc for smdctl failed\n", __func__);
		return -ENOMEM;
	}

	gsmdctl = smdctl;

	smdctl->usb_ignore_suspending_hstwkp = true;

	r = init_gpio(smdctl, pdev);
	if (r)
		goto err_init_gpio;

	gpio = smdctl->gpio;

	r = register_smdctl_dev(smdctl);
	if (r) {
		pr_err("%s: failed to register smdctl error = %d\n",
			__func__, r);
		goto err_register_smdctl_dev;
	}

	r = register_reboot_notifier(&smdctl_reboot_notifier);
	if (r) {
		pr_err("%s:cannot register reboot notifier (err=%d)\n", __func__, r);
		goto err_register_notifier;
	}

	r = sysfs_create_group(&pdev->dev.kobj, &smdctl_sysfs);
	if (r) {
		pr_err("%s: Failed to create sysfs group\n",
			__func__);
		goto err_sysfs_create_group;
	}

	wake_lock_init(&smdctl->ctl_wake_lock, WAKE_LOCK_SUSPEND, "hsic");
	smdctl_set_pm_status(PM_STATUS_UNDEFINE);

	spin_lock_init(&smdctl->lock);

	platform_set_drvdata(pdev, smdctl);

	return 0;

err_sysfs_create_group:
	unregister_reboot_notifier(&smdctl_reboot_notifier);
err_register_notifier:
	deregister_smdctl_dev(smdctl);
err_register_smdctl_dev:
	for (i = 0; i < SMD_GPIO_MAX; i++)
		gpio_free(gpio[i].num);
err_init_gpio:
	kfree(smdctl);
	gsmdctl = NULL;

	return r;
}

static int __devexit smdctl_remove(struct platform_device *pdev)
{
	int i;
	struct str_smdctl *smdctl = platform_get_drvdata(pdev);
	struct str_ctl_gpio *gpio = smdctl->gpio;

	kthread_stop(smdctl->pm_kthread);

	sysfs_remove_group(&pdev->dev.kobj, &smdctl_sysfs);

#ifndef CONFIG_NOT_SUPPORT_SIMDETECT
	free_irq(gpio[SMD_GPIO_SIM_DET].irq, (void *)smdctl);
#endif
	free_irq(smdctl->gpio[SMD_GPIO_CP_ACT].irq, (void *)smdctl);
	if (!usb_runtime_pm_ap_initiated_L2)
		free_irq(smdctl->gpio[SMD_GPIO_HSIC_SUS].irq, (void *)smdctl);
	free_irq(smdctl->gpio[SMD_GPIO_HST_WKP].irq, (void *)smdctl);

	wake_lock_destroy(&smdctl->ctl_wake_lock);

	deregister_smdctl_dev(smdctl);

	for (i = 0; i < SMD_GPIO_MAX; i++)
		gpio_free(gpio[i].num);

	kfree(smdctl);
	gsmdctl = NULL;

	return 0;
}

#ifdef CONFIG_PM

static void L3_resume_gpio_control(struct str_smdctl *smdctl)
{
	struct str_ctl_gpio *gpio = smdctl->gpio;

	pr_debug("%s: called\n", __func__);

	if (gpio_get_value(gpio[SMD_GPIO_HST_WKP].num) == LOW) {
		if (!smdctl->usb_ignore_suspending_hstwkp) {
			pr_err("CP L3 -> L0\n");
			if (smdctl->pm_kthread > 0)
				wake_up_process(smdctl->pm_kthread);
		}
		return;
	}

	gpio_set_value(gpio[SMD_GPIO_SLV_WKP].num, HIGH);
	pr_debug("SMD SLV_WKP -> 1\n");
	mdelay(10);	/* do not use msleep at resume function */
	gpio_set_value(gpio[SMD_GPIO_SLV_WKP].num, LOW);
	pr_debug("SMD SLV_WKP -> 0\n");

	return;
}

static void L3_suspend_gpio_control(struct str_smdctl *smdctl)
{
	pr_debug("%s: called\n", __func__);

	if (!usb_runtime_pm_ap_initiated_L2)
		smdhsic_pm_suspend();	/* run by usb runtime pm */

	smdctl_set_pm_status(PM_STATUS_L3);

	return;
}

static int smdctl_suspend(struct platform_device *pdev, pm_message_t message)
{
	struct str_smdctl *smdctl = platform_get_drvdata(pdev);
	struct str_ctl_gpio *gpio = smdctl->gpio;

	pr_debug("%s: called\n", __func__);

	if (!smdctl->usb_ignore_suspending_hstwkp &&
		gpio_get_value(gpio[SMD_GPIO_HST_WKP].num) == LOW)
		return -1;

	L3_suspend_gpio_control(smdctl);

	return 0;
}

static int smdctl_resume(struct platform_device *pdev)
{
	struct str_smdctl *smdctl = platform_get_drvdata(pdev);

	pr_debug("%s: Enter\n", __func__);

	L3_resume_gpio_control(smdctl);

	smdctl_set_pm_status(PM_STATUS_L3_L0);

	return 0;
}
#endif

static struct platform_driver smdctl_plat = {
	.probe = smdctl_probe,
	.remove = __devexit_p(smdctl_remove),
#ifdef CONFIG_PM
	.suspend = smdctl_suspend,
	.resume = smdctl_resume,
#endif
	.driver = {
		   .name = "smd-ctl",
		   },
};

static int __init smdctl_init(void)
{
	pr_debug("%s: Enter\n", __func__);
	return platform_driver_register(&smdctl_plat);
}

static void __exit smdctl_exit(void)
{
	pr_debug("%s: Enter\n", __func__);
	platform_driver_unregister(&smdctl_plat);
}

module_init(smdctl_init);
module_exit(smdctl_exit);

MODULE_DESCRIPTION("SAMSUNG MODEM DRIVER");
MODULE_AUTHOR("Minwoo KIM <minwoo7945.kim@samsung.com>");
MODULE_LICENSE("GPL");
