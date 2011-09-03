/*
 * driver/misc/smd-hsic/smd_core.h
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

#ifndef _SMD_CORE_H_
#define _SMD_CORE_H_

#include <linux/kobject.h>
#include <linux/platform_device.h>

#include "smd_ipc.h"
#include "smd_rfs.h"
#include "smd_hsic.h"

#define ROW_PEP_CHAR    16
#define MAX_DUMP_SIZE	512

struct str_smd {
	struct str_smdipc smdipc;
	struct str_smdrfs smdrfs;
};

unsigned int get_request_size(struct str_ring_buf *buf);
unsigned int get_vacant_size(struct str_ring_buf *buf);
unsigned int get_remained_size(struct str_ring_buf *buf);
int alloc_buf(struct str_ring_buf *buf, unsigned int size);
void flush_buf(struct str_ring_buf *buf);
void free_buf(struct str_ring_buf *buf);
int memcpy_to_ringbuf(struct str_ring_buf *rbuf, const char *data,
		      unsigned int len);
int memcpy_from_ringbuf(struct str_ring_buf *rbuf, char *buf, unsigned int len);
int memcpy_from_ringbuf_user(struct str_ring_buf *rbuf, char *buf,
			     unsigned int len);
struct str_smd *get_smd_addr(struct device *dev);

int smd_alloc_urb(struct str_hsic *hsic);
void smd_free_urb(struct str_hsic *hsic);
void dump_buffer(char *startAddr, int size);
void init_err_stat(struct str_err_stat *stat);
void smd_kill_urb(struct str_hsic *hsic);

#endif	/* _SMD_CORE_H */
