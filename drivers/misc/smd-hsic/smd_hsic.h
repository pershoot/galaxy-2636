/*
 * driver/misc/smd-hsic/smd_hsic.h
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

#ifndef __SMD_HSIC_H__
#define __SMD_HSIC_H__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>

#define BOOT_VEN_ID	    0x058b
#define BOOT_PRO_ID	    0x0041

#define PSI_VEN_ID	    0x058b
#define PSI_PRO_ID	    0x0015

#define MAIN_VEN_ID	    0x1519
#define MAIN_PRO_ID	    0x0020

#define ID_PRI		    0x80000000
#define ID_BIND		    0x40000000
#define ID_REL		    0x20000000

#define GET_DEVID(x)	    (x & 0x000000ff)
#define IS_BIND_ID(x)	    (x & ID_BIND)

#define MAX_RX_URB_COUNT	32
#define MAX_TX_URB_COUNT	32

enum hsic_dev_id {
	FMT_DEV_ID = 0,
	RAW_DEV_ID,
	RFS_DEV_ID,
	CMD_DEV_ID,
	DOWN_DEV_ID,
	MAX_DEV_ID,
};

enum {
	XMM6260_PSI_DOWN = 0,
	XMM6260_BIN_DOWN,
	XMM6260_CHANNEL
};

enum {
	URB_STAT_FREE = 0,
	URB_STAT_ALLOC,
	URB_STAT_SUBMIT,
};

struct str_smd_urb {
	struct urb *urb;
	unsigned int urb_status;
};

struct str_hsic {
	struct usb_interface *intf;
	struct usb_device *usb;

	struct semaphore hsic_mutex;

	unsigned int rx_pipe;
	unsigned int tx_pipe;

	unsigned int rx_urbcnt;

	struct str_smd_urb rx_urb[MAX_RX_URB_COUNT];

	struct delayed_work pm_runtime_work;

	struct list_head *txq;

	bool dpm_suspending;
	unsigned int resume_failcnt;
};

struct str_intf_priv {
	unsigned int devid;
	void *data;
};

struct str_err_stat {
	unsigned int rx_err;
	unsigned int rx_crc_err;
	unsigned int rx_pro_err;
	unsigned int rx_over_err;
};

typedef void *(*emu_fn) (void);
typedef void *(*con_fn) (void *, struct str_hsic *);
typedef void (*discon_fn) (void *);
typedef void (*demu_fn) (void *);

int smdhsic_pm_suspend(void);
int smdhsic_pm_resume(void);
void *get_smd_device(unsigned int id);

extern int usb_runtime_pm_ap_initiated_L2;
extern int smdctl_set_pm_status(unsigned int);
extern void smdctl_request_connection_recover(bool);

/* work around for Tegre 2 hsic phy problem */
extern void tegra_ehci_txfilltuning(void);

void add_tail_txurb(struct list_head *list, struct urb *urb);
void add_head_txurb(struct list_head *list, struct urb *urb);
void queue_tx_work(void);
void flush_txurb(struct list_head *list);
#endif				/*__SMD_HSIC_H__ */
