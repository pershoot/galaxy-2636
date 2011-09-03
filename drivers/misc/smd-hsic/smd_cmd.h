/*
 * driver/misc/smd-hsic/smd_cmd.h
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

#ifndef _SMD_CMD_H_
#define _SMD_CMD_H_

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/completion.h>
#include <linux/wakelock.h>

#include "smd_hsic.h"
#include "smd.h"

#define CMD_READ_BUF_SIZE       (64*1024)
#define CMD_PM_STATUS_DISCONNECT   2
#define CMD_PM_STATUS_SUSPEND   1
#define CMD_PM_STATUS_RESUME    0

struct str_smdcmd {
	struct class *class;
	struct device *dev;
	struct cdev cdev;
	dev_t devid;

	atomic_t opened;

	struct str_ring_buf read_buf;
	char *tpbuf;

	wait_queue_head_t poll_wait;
	struct str_hsic hsic;
	struct str_err_stat stat;

	struct wake_lock rxguide_lock;
	struct delayed_work rx_work;

	unsigned int pm_stat;

	struct wake_lock wakelock;
	unsigned int suspended;
};

void *init_smdcmd(void);
void *connect_smdcmd(void *smd_device, struct str_hsic *hsic);
void disconnect_smdcmd(void *smd_device);
void exit_smdcmd(void *);

int smdcmd_suspend(struct str_smdcmd *smdcmd);
int smdcmd_resume(struct str_smdcmd *smdcmd);

#endif				/* _SMD_CMD_H_ */
