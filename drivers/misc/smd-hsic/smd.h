/*
 * driver/misc/smd-hsic/smd.h
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

#ifndef _SMD_H_
#define _SMD_H_

struct str_ring_buf {
	char *head;
	char *tail;
	char *buf;
	unsigned int size;
	spinlock_t lock;
};

#define SMDIPC_DEVNAME	"smdipc"
#define SMDIPC_MINOR	1

#define SMDRAW_DEVNAME	"smdraw"
#define SMDRAW_MINOR	2

#define SMDRFS_DEVNAME	"smdrfs"
#define SMDRFS_MINOR	3

#define SMDCMD_DEVNAME	"smdcmd"
#define SMDCMD_MINOR	4

#define HDLC_START  0x7f
#define HDLC_END    0x7E

#define SMDCMD_FLOWCTL_SUSPEND 0xca
#define SMDCMD_FLOWCTL_RESUME 0xcb

#endif
