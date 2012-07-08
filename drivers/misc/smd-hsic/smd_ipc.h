/*
 * driver/misc/smd-hsic/smd_ipc.h
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

#ifndef _SMD_IPC_H_
#define _SMD_IPC_H_

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/completion.h>
#include <linux/wakelock.h>
#include <linux/skbuff.h>
#include "smd_hsic.h"
#include "smd.h"

#define IPC_READ_BUF_SIZE       (16*1024)
#define IPC_PM_STATUS_DISCONNECT   2
#define IPC_PM_STATUS_SUSPEND   1
#define IPC_PM_STATUS_RESUME    0

struct str_smdipc {
	struct class *class;
	struct device *dev;
	struct cdev cdev;
	dev_t devid;

	atomic_t opened;
	int open_fail_cnt;

	struct str_ring_buf read_buf;
	char *tpbuf;

	wait_queue_head_t poll_wait;
	struct str_hsic hsic;
	struct str_err_stat stat;
	struct wake_lock rxguide_lock;
	struct delayed_work rx_work;
	unsigned int pm_stat;

	struct wake_lock wakelock;
	/* to block re-entering suspend routine */
	unsigned int suspended;

	struct sk_buff_head rxq;
};

struct fmt_hdr {
	u16 len;
	u8 control;
} __attribute__ ((packed));

void *init_smdipc(void);
void *connect_smdipc(void *smd_device, struct str_hsic *hsic);
void disconnect_smdipc(void *smd_device);
void exit_smdipc(void *);

int smdipc_suspend(struct str_smdipc *smdipc);
int smdipc_resume(struct str_smdipc *smdipc);

#endif				/* _SMD_IPC_H_ */
