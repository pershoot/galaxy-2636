/*
 * driver/misc/smd-hsic/smd_core.c
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

#include <linux/kobject.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/mutex.h>
#include <linux/gfp.h>
#include <linux/vmalloc.h>
#include "smd_core.h"

#define GET_ABS_HEAD(a)   (int)((a)->head-(a)->buf)
#define GET_ABS_TAIL(a)   (int)((a)->tail-(a)->buf)

unsigned int get_request_size(struct str_ring_buf *buf)
{
	unsigned int head = GET_ABS_HEAD(buf);

	return buf->size - head;
}

static unsigned int get_vacant_size_locked(struct str_ring_buf *buf)
{
	unsigned int head = GET_ABS_HEAD(buf);
	unsigned int tail = GET_ABS_TAIL(buf);

	if (head == tail)
		return buf->size - 1;
	else if (head > tail)
		return buf->size - (head - tail) - 1;
	else
		return tail - head - 1;
}

unsigned int get_vacant_size(struct str_ring_buf *buf)
{
	unsigned long flags;
	unsigned int value;
	spin_lock_irqsave(&buf->lock, flags);
	value = get_vacant_size_locked(buf);
	spin_unlock_irqrestore(&buf->lock, flags);
	return value;
}

static unsigned int get_remained_size_locked(struct str_ring_buf *buf)
{
	return buf->size - get_vacant_size_locked(buf) - 1;
}


unsigned int get_remained_size(struct str_ring_buf *buf)
{
	unsigned long flags;
	unsigned int value;
	spin_lock_irqsave(&buf->lock, flags);
	value = get_remained_size_locked(buf);
	spin_unlock_irqrestore(&buf->lock, flags);
	return value;
}

int alloc_buf(struct str_ring_buf *rbuf, unsigned int size)
{
	rbuf->buf = (char *)vmalloc(size);
	if (!rbuf->buf) {
		pr_err("%s:vmalloc() faild \n", __func__);
		return -ENOMEM;
	}
	rbuf->head = rbuf->buf;
	rbuf->tail = rbuf->buf;
	rbuf->size = size;
	spin_lock_init(&rbuf->lock);

	return 0;
}

void flush_buf(struct str_ring_buf *rbuf)
{
	unsigned long flags;

	if (!rbuf->buf)
		return;
	spin_lock_irqsave(&rbuf->lock, flags);
	rbuf->head = rbuf->buf;
	rbuf->tail = rbuf->buf;
	spin_unlock_irqrestore(&rbuf->lock, flags);
}

void free_buf(struct str_ring_buf *rbuf)
{
	if (rbuf->buf)
		vfree(rbuf->buf);
	rbuf->buf = NULL;
	rbuf->head = NULL;
	rbuf->tail = NULL;
	rbuf->size = 0;
}

int memcpy_to_ringbuf(struct str_ring_buf *rbuf, const char *data,
		      unsigned int len)
{
	unsigned long flags;
	unsigned int rbuf_vacant;
	unsigned int fst_size;
	unsigned int snd_size;

	if (!rbuf->buf) {
		pr_err("%s:buffer is not allocated\n", __func__);
		return -EIO;
	}

	spin_lock_irqsave(&rbuf->lock, flags);

	rbuf_vacant = get_vacant_size_locked(rbuf);

	if (rbuf_vacant <= len) {
		spin_unlock_irqrestore(&rbuf->lock, flags);
		pr_err("%s:not enough buffer, rbuf_vacant : %d, len : %d,\
				head : %d, tail : %d\n", __func__, rbuf_vacant, len,
				GET_ABS_HEAD(rbuf), GET_ABS_TAIL(rbuf));
		return -ENOSPC;
	}

	fst_size = (rbuf->size) - GET_ABS_HEAD(rbuf);

	if (len < fst_size)
		fst_size = len;
	memcpy(rbuf->head, data, fst_size);
	rbuf->head += fst_size;

	/* handle case for first size is same as write size */
	if (rbuf->head == rbuf->buf + rbuf->size)
		rbuf->head = rbuf->buf;

	if (fst_size < len) {
		snd_size = len - fst_size;
		memcpy(rbuf->buf, data + fst_size, snd_size);
		rbuf->head = rbuf->buf + snd_size;
	}

	spin_unlock_irqrestore(&rbuf->lock, flags);
	return len;
}

int memcpy_from_ringbuf_user(struct str_ring_buf *rbuf, char *buf,
			     unsigned int len)
{
	unsigned long flags;
	unsigned int rbuf_rmd;
	unsigned int fst_size;
	unsigned int snd_size;
	unsigned int rd_size;

	spin_lock_irqsave(&rbuf->lock, flags);

	rbuf_rmd = get_remained_size_locked(rbuf);
	if (!rbuf_rmd) {
		spin_unlock_irqrestore(&rbuf->lock, flags);
		return 0;
	}

	if (!len)
		rd_size = rbuf_rmd;
	else
		rd_size = (len > rbuf_rmd) ? rbuf_rmd : len;

	fst_size = (rbuf->size) - GET_ABS_TAIL(rbuf);
	fst_size = (rd_size > fst_size) ? fst_size : rd_size;

	if (copy_to_user(buf, rbuf->tail, fst_size)) {
		spin_unlock_irqrestore(&rbuf->lock, flags);
		pr_err("%s:copy_to_user failed(fst_size : %d)\n",
			__func__, fst_size);
		return -EFAULT;
	}

	rbuf->tail += fst_size;

	/* handle case for first size is same as read size */
	if (rbuf->tail == rbuf->buf + rbuf->size)
		rbuf->tail = rbuf->buf;

	if (fst_size < rd_size) {
		snd_size = rd_size - fst_size;
		if (copy_to_user(buf + fst_size, rbuf->buf, snd_size)) {
			spin_unlock_irqrestore(&rbuf->lock, flags);
			pr_err("%s:copy_to_user failed(snd_size : %d)\n",
			      __func__, snd_size);
			return -EFAULT;
		}
		rbuf->tail = rbuf->buf + snd_size;
	}

	spin_unlock_irqrestore(&rbuf->lock, flags);
	return rd_size;
}

int memcpy_from_ringbuf(struct str_ring_buf *rbuf, char *buf, unsigned int len)
{
	unsigned long flags;
	unsigned int rbuf_rmd;
	unsigned int fst_size;
	unsigned int snd_size;
	unsigned int rd_size;

	spin_lock_irqsave(&rbuf->lock, flags);

	rbuf_rmd = get_remained_size_locked(rbuf);
	if (!rbuf_rmd) {
		spin_unlock_irqrestore(&rbuf->lock, flags);
		return 0;
	}

	if (!len)
		rd_size = rbuf_rmd;
	else
		rd_size = (len > rbuf_rmd) ? rbuf_rmd : len;

	fst_size = (rbuf->size) - GET_ABS_TAIL(rbuf);
	fst_size = (rd_size > fst_size) ? fst_size : rd_size;

	memcpy(buf, rbuf->tail, fst_size);

	rbuf->tail += fst_size;

	/* handle case for first size is same as read size */
	if (rbuf->tail == rbuf->buf + rbuf->size)
		rbuf->tail = rbuf->buf;

	if (fst_size < rd_size) {
		snd_size = rd_size - fst_size;
		memcpy(buf + fst_size, rbuf->buf, snd_size);
		rbuf->tail = rbuf->buf + snd_size;
	}

	spin_unlock_irqrestore(&rbuf->lock, flags);
	return rd_size;
}

struct str_smd *get_smd_addr(struct device *dev)
{
	struct platform_device *pdev;

	pdev = container_of(dev, struct platform_device, dev);
	if (!pdev)
		return NULL;

	return platform_get_drvdata(pdev);
}

void smd_free_urb(struct str_hsic *hsic)
{
	unsigned int i;

	for (i = 0; i < hsic->rx_urbcnt; i++) {
		if (hsic->rx_urb[i].urb) {
			usb_kill_urb(hsic->rx_urb[i].urb);
			usb_free_urb(hsic->rx_urb[i].urb);
		}
	}
}

int smd_alloc_urb(struct str_hsic *hsic)
{
	unsigned int i;

	memset(hsic->rx_urb, 0, sizeof(hsic->rx_urb));

	for (i = 0; i < hsic->rx_urbcnt; i++) {
		hsic->rx_urb[i].urb = usb_alloc_urb(0, GFP_KERNEL);
		if (hsic->rx_urb[i].urb == NULL) {
			pr_err("%s:usb_alloc_urb failed\n", __func__);
			goto err;
		}
		hsic->rx_urb[i].urb_status = URB_STAT_ALLOC;
	}
	return 0;

err:
	/* clean up any partly allocated urbs */
	smd_free_urb(hsic);
	return -ENOMEM;
}

void smd_kill_urb(struct str_hsic *hsic)
{
	unsigned int i;

	for (i = 0; i < hsic->rx_urbcnt; i++) {
		if (hsic->rx_urb[i].urb) {
			usb_kill_urb(hsic->rx_urb[i].urb);
			hsic->rx_urb[i].urb_status = URB_STAT_ALLOC;
		}
	}
}

void dump_buffer(char *startAddr, int size)
{
	int i;

	if (size > MAX_DUMP_SIZE)
		size = MAX_DUMP_SIZE;

	for (i = 0; i < size; i++) {
		if ((i >= ROW_PEP_CHAR) && (!(i % ROW_PEP_CHAR)))
			pr_info("\n");

		printk("%02x ", *(startAddr + i));
	}

	pr_info("\n");
}

void init_err_stat(struct str_err_stat *stat)
{
	memset(stat, 0, sizeof(struct str_err_stat));
}
