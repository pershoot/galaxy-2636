/*
 * driver/misc/smd-hsic/smd_down.h
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

#ifndef _SMD_DOWN_H_
#define _SMD_DOWN_H_

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/completion.h>

#include "smd_hsic.h"
#include "smd_ipc.h"

#ifdef CONFIG_KERNEL_DEBUG_SEC
#define DOWN_READ_BUF_SIZE	(128*1024)
#else
#define DOWN_READ_BUF_SIZE	(32*1024)
#endif

#define SMDDOWN_DEVNAME		"smddown"
#define SMDDOWN_MINOR		10

struct str_smd_down {
	struct class *class;
	struct device *dev;
	struct cdev cdev;
	dev_t devid;

	atomic_t opened;
	int open_fail_cnt;

	struct str_ring_buf read_buf;
	char *tpbuf;

	struct completion rdcomp;
	unsigned int rdcomp_req;

	wait_queue_head_t poll_wait;
	struct str_hsic hsic;
	struct str_err_stat stat;

	struct delayed_work rx_work;
	struct urb *delayed_urb;
};

void *init_smd_down(void);
void *connect_smd_down(void *smd_device, struct str_hsic *hsic);
void disconnect_smd_down(void *smd_device);
void exit_smd_down(void *);

#endif				/* _SMD_DOWN_H_ */
