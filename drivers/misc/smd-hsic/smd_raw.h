/*
 * driver/misc/smd-hsic/smd_raw.h
 *
 * driver supporting miscellaneous functions for Samsung P3 device
 *
 * COPYRIGHT(C) Samsung Electronics Co., Ltd. 2006-2011 All Right Reserved.
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

#ifndef _SMD_RAW_H_
#define _SMD_RAW_H_

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/completion.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <linux/wakelock.h>

#include "smd.h"

#define MAX_RAW_DEV		3
#define MAX_PDP_DEV		3
#define MAX_RAW_CHANNEL		(MAX_RAW_DEV+MAX_PDP_DEV)

#define CID_RAW_CSD		1
#define CID_RAW_ROUTER		25
#define CID_RAW_PDP0		10
#define CID_RAW_PDP1		11
#define CID_RAW_PDP2		12
#define CID_RAW_LB		31

#define NAME_RAW_CSD		"smdcsd"
#define NAME_RAW_ROUTER		"smdrouter"
#define NAME_RAW_PDP0		"pdp0"
#define NAME_RAW_PDP1		"pdp1"
#define NAME_RAW_PDP2		"pdp2"
#define NAME_RAW_LB		"smdloop"

#define MINOR_RAW_CSD		6
#define MINOR_RAW_ROUTER		7
#define MINOR_RAW_PDP0		8
#define MINOR_RAW_PDP1		9
#define MINOR_RAW_PDP2		10
#define MINOR_RAW_LB		11
#define RAW_PM_STATUS_DISCONNECT	2
#define RAW_PM_STATUS_SUSPEND	1
#define RAW_PM_STATUS_RESUME	0

struct str_raw_dev {
	struct device *dev;
	struct cdev cdev;
	dev_t devid;
	unsigned int cid;

	struct str_ring_buf read_buf;

	atomic_t opened;

	wait_queue_head_t poll_wait;

	unsigned int recvsize;
};

struct str_pdp_dev {
	struct net_device *pdp;
	struct net_device_stats *pdp_stats;
	unsigned int cid;
};

struct str_pdp_priv {
	unsigned int cid;
	struct net_device_stats pdp_stats;
	struct str_smdraw *smdraw;
	struct delayed_work work;
	struct workqueue_struct *workqueue;
	struct sk_buff_head txq;
	spinlock_t lock;
	struct net_device *dev;
};

struct str_smdraw {
	struct class *class;
	struct device *dev;
	struct cdev cdev;
	dev_t devid;
	char *urb_rx_buf[MAX_RX_URB_COUNT];
	struct str_raw_dev rawdev[MAX_RAW_DEV];
	struct str_pdp_dev pdpdev[MAX_PDP_DEV];
	struct str_hsic hsic;

	unsigned int rawdevcnt;
	unsigned int pdpdevcnt;
	unsigned int pm_stat;

	struct str_ring_buf read_buf;

	struct str_err_stat stat;
	struct wake_lock rxguide_lock;
	struct delayed_work rx_work;

	struct wake_lock wakelock;
	/* to block re-entering suspend routine */
	unsigned int suspended;
	bool tx_flow_control;

	spinlock_t lock;
	struct sk_buff_head pdp_txq;
	struct delayed_work pdp_work;
	struct workqueue_struct *pdp_wq;

	unsigned long wake_time;
};

struct str_dev_info {
	char *name;
	unsigned int cid;
	dev_t minor;
	unsigned int dev_type;

};

struct raw_hdr {
	u8 start;
	u32 len;
	u8 id;
	u8 control;
} __attribute__ ((packed));

void *init_smdraw(void);
void *connect_smdraw(void *smd_device, struct str_hsic *hsic);
void disconnect_smdraw(void *smd_device);
void exit_smdraw(void *param);
int smdraw_suspend(struct str_smdraw *smdraw);
int smdraw_resume(struct str_smdraw *smdraw);

#endif				/* _SMD_RAW_H_ */
