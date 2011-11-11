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

#ifndef _SMD_CTL_H_
#define _SMD_CTL_H_

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/wakelock.h>
#include <linux/timer.h>
#include <linux/time.h>

#define LOW			0
#define HIGH			1
#define GPIO_IN			0
#define GPIO_OUT			1

#define CP_PWR_ON		0
#define CP_PWR_OFF		1
#define CP_ABNOR			2
#define CP_MAX_STATUS		3

#define SMDCTL_DEVNAME		"smdctl"
#define SMDCTL_MINOR		0

#define IOCTL_CP_ON		_IO('o', 0x20)
#define IOCTL_CP_OFF		_IO('o', 0x21)
#define IOCTL_CP_START		_IO('o', 0x22)
#define IOCTL_OFF_HSIC_ACT	_IO('o', 0x23)
#define IOCTL_ON_HSIC_ACT		_IO('o', 0x24)
#define IOCTL_GET_HOST_WAKE	_IO('o', 0x25)
#define IOCTL_ENABLE_HSIC_EN	_IO('o', 0x26)
#define IOCTL_DISABLE_HSIC_EN	_IO('o', 0x27)

#ifdef CONFIG_KERNEL_DEBUG_SEC
#define IOCTL_CP_UPLOAD		_IO('o', 0x28)
#endif

#define IOCTL_SET_CP_USB_STAT 	_IO('o', 0x29)
#define IOCTL_CP_RESET 		_IO('o', 0x30)

#define CP_SUS_LOW		0
#define CP_SUS_RESET		1
#define CP_SUS_HIGH		2
#define CP_SUS_UNDEFIND_STAT	3

#define IRQF_TRIG_BOTH_EDGE	(IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING)

enum smd_gpio{
	SMD_GPIO_CP_ACT = 0,
	SMD_GPIO_SIM_DET,
	SMD_GPIO_CP_RST,
	SMD_GPIO_CP_ON,
	SMD_GPIO_AP_ACT,
	SMD_GPIO_HSIC_ACT,
	SMD_GPIO_HSIC_SUS,
	SMD_GPIO_HSIC_EN,
	SMD_GPIO_CP_REQ,
	SMD_GPIO_SLV_WKP,
	SMD_GPIO_HST_WKP,
	SMD_GPIO_MAX,
};

struct str_smd_gpio {
	char name[16];
	int dir;
	int initval;
	int intsrc;
};

struct str_ctl_gpio {
	char name[16];
	int num;
	int irq;
};

struct str_smdctl {
	struct class *class;
	struct device *dev;
	struct cdev cdev;
	dev_t devid;

	atomic_t refcount;
	unsigned int cp_stat;
	unsigned int host_wake;

	unsigned int cur_pm_status;

	struct wake_lock ctl_wake_lock;
	struct str_ctl_gpio gpio[SMD_GPIO_MAX];

	struct delayed_work sus_req_work;
	struct completion sus_req_comp;
	unsigned int sus_req_comp_req;

	struct completion host_wake_comp;
	unsigned int host_wake_comp_req;

	char *uevent_envs[2];
	struct delayed_work cp_active_work;
	struct delayed_work modem_reset_work;
	struct delayed_work sim_detect_work;
	unsigned int sim_reference_level;

	struct task_struct *pm_kthread;

	bool rest_init_done;
	bool usb_ignore_suspending_hstwkp;
	bool modem_uevent_requested;
	bool modem_reset_remained;
	bool shutdown_called;

	unsigned cp_reset_cnt;
};

#define PM_STATUS_UNDEFINE	0
#define PM_STATUS_INIT		1
#define PM_STATUS_L0		2
#define PM_STATUS_L0_L2	3
#define PM_STATUS_L2_L0_AP	4
#define PM_STATUS_L2_L0_CP	5
#define PM_STATUS_L2		6
#define PM_STATUS_L2_L3	7
#define PM_STATUS_L3_L0	8
#define PM_STATUS_L3		9

#define PM_REL_L0		0
#define PM_REQ_L0		1

int init_smdctl(struct str_smdctl *smdctl, struct platform_device *pdev);
int exit_smdctl(void);
int smd_check_pm_status(void);
int smdhsic_pm_resume_AP(void);
int smdctl_request_slave_wakeup(struct completion *done);

#endif	/* _SMD_CTL_H_ */
