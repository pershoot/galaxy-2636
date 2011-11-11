/*
 * driver/misc/smd-hsic/smd_raw.c
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

#include <linux/module.h>
#include <linux/platform_device.h>

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
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>

#include <linux/mutex.h>
#include <linux/gfp.h>

#include "smd_core.h"
#include "smd_raw.h"

#include "../smdctl/smd_ctl.h"

#define RAW_RX_URB_CNT		4
#define RAW_TX_URB_CNT		32

#define RAW_TYPE_NET		1
#define RAW_TYPE_NOR		0

#define RAW_TEMP_BUFFER_SIZE	(4*1024)
#define RAW_READ_BUF_SIZE	(64*1024)

#define DEFAULT_WAKE_TIME	(HZ*6)

static void raw_urb_rx_comp(struct urb *urb);
static void raw_urb_tx_comp(struct urb *urb);

static ssize_t smdraw_waketime_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	unsigned int msec;
	char *p = buf;
	struct str_smdraw *smdraw = dev_get_drvdata(dev);

	msec = jiffies_to_msecs(smdraw->wake_time);

	p += sprintf(buf, "raw wake_time : %ums\n", msec);

	return p - buf;
}

static ssize_t smdraw_waketime_store(struct device *dev,
				     struct device_attribute *attr,
				     char *buf, size_t count )
{
	unsigned long msec;
	int r;
	struct str_smdraw *smdraw = dev_get_drvdata(dev);

	r = strict_strtoul(buf, 10, &msec);
	if (r) {
		return count;
	}
	smdraw->wake_time = msecs_to_jiffies(msec);

	return count;
}

static struct device_attribute smdraw_waketime =
__ATTR(waketime, S_IRUGO | S_IWUSR, smdraw_waketime_show, smdraw_waketime_store);

static struct attribute *smdraw_sysfs_attrs[] = {
	&smdraw_waketime.attr,
	NULL,
};

static struct attribute_group smdraw_sysfs = {
	.name = "smdraw",
	.attrs = smdraw_sysfs_attrs,
};


/* return free rx urb index */
/*this function should be proteced by mutex*/
static int get_rx_free_urb(struct str_hsic *hsic)
{
	unsigned int i;
	unsigned int max_urb = hsic->rx_urbcnt;

	for (i = 0; i < max_urb; i++) {
		if (hsic->rx_urb[i].urb_status == URB_STAT_ALLOC)
			return i;
	}
	pr_err("%s:can't find free urb\n", __func__);
	return -ENOSPC;
}

static int clear_rx_submit(struct urb *urb, struct str_hsic *hsic)
{
	unsigned int i;
	unsigned int max_urb = hsic->rx_urbcnt;

	for (i = 0; i < max_urb; i++) {
		if (hsic->rx_urb[i].urb == urb) {
			hsic->rx_urb[i].urb_status = URB_STAT_ALLOC;
			return 0;
		}
	}
	pr_err("%s:Can't find urb\n", __func__);
	return -ENXIO;
}

static int get_raw_header(struct str_ring_buf *rbuf, struct raw_hdr *hd)
{
	if (get_remained_size(rbuf) < sizeof(*hd)) {
		pr_debug("ring buffer data length less than header size\n");
		return -ENOSPC;
	}

	if (memcpy_from_ringbuf(rbuf, (char *)hd, sizeof(*hd)) != sizeof(*hd)) {
		pr_err("read header error from ringbuffer\n");
		return -EIO;
	}

	return 0;
}

static void drop_hdlc_packet(struct str_ring_buf *rbuf)
{
	char hdlc_stop;

	while (memcpy_from_ringbuf(rbuf, &hdlc_stop, sizeof(hdlc_stop)))
		if (hdlc_stop == HDLC_END)
			break;
}

/* returns 0 if not enough data, otherwise returns the hdlc_packet_len */
static unsigned int packet_has_enough_data(struct str_ring_buf *rbuf,
					struct raw_hdr *hd)
{
	int data_available;
	unsigned int hdlc_packet_len;

	data_available = get_remained_size(rbuf);
	hdlc_packet_len = hd->len + 2; /* hd->len + hdlc start/stop */

	if (data_available + sizeof(*hd) < hdlc_packet_len) {
		/* rewind_tail pointer */
		rbuf->tail -= sizeof(*hd);
		if (rbuf->tail < rbuf->buf)
			rbuf->tail += rbuf->size - 1;
		/* return and process it next completion*/
		return 0;
	}
	return hdlc_packet_len;
}

static void process_pdp_packet(struct str_ring_buf *rbuf,
			struct raw_hdr *hd, struct net_device *dev)
{
	int ret;
	char hdlc_stop;
	unsigned int packet_len;
	struct sk_buff *skb;

	packet_len = hd->len - (sizeof(*hd) - 1);
	skb = alloc_skb(packet_len, GFP_ATOMIC);
	if (!skb) {
		pr_err("%s:alloc_skb() error\n", __func__);
		drop_hdlc_packet(rbuf);
		return;
	}
	ret = memcpy_from_ringbuf(rbuf, skb->data, packet_len);
	if (ret != packet_len) {
		dev_kfree_skb_any(skb);
		drop_hdlc_packet(rbuf);
		return;
	}
	skb_put(skb, packet_len);
	skb->dev = dev;
	skb->protocol = __constant_htons(ETH_P_IP);
	skb->dev->stats.rx_packets++;
	skb->dev->stats.rx_bytes += skb->len;
	netif_rx(skb);

	/* consume hdlc stop byte */
	memcpy_from_ringbuf(rbuf, &hdlc_stop, sizeof(hdlc_stop));
}

static void process_raw_packet(struct str_ring_buf *rbuf,
			struct raw_hdr *hd, struct str_raw_dev *rawdev)
{
	int ret;
	char *raw_hdlc;
	unsigned int cpsize;
	unsigned int hdlc_packet_len;

	hdlc_packet_len = packet_has_enough_data(rbuf, hd);
	if (!hdlc_packet_len)
		return;

	if (!atomic_read(&rawdev->opened)) {
		drop_hdlc_packet(rbuf);
		return;
	}

	raw_hdlc = kmalloc(hdlc_packet_len, GFP_ATOMIC);
	if (!raw_hdlc)
		goto err_kmalloc;

	memcpy(raw_hdlc, hd, sizeof(*hd));
	cpsize = sizeof(*hd);

	ret = memcpy_from_ringbuf(rbuf, raw_hdlc + cpsize,
				hdlc_packet_len - cpsize);
	if (ret != (hdlc_packet_len - cpsize))
		goto err_memcpy1;

	ret = memcpy_to_ringbuf(&rawdev->read_buf, raw_hdlc, hdlc_packet_len);
	if (ret != hdlc_packet_len)
		goto err_memcpy2;

	kfree(raw_hdlc);
	wake_up(&rawdev->poll_wait);
	return;

err_memcpy2:
err_memcpy1:
	kfree(raw_hdlc);
err_kmalloc:
	drop_hdlc_packet(rbuf);
}

static int process_pdp(struct str_smdraw *smdraw, struct str_ring_buf *rbuf,
		struct raw_hdr *hd)
{
	int i;
	struct str_pdp_dev *pdpdev;

	for (i = 0; i < MAX_PDP_DEV; i++) {
		pdpdev = &smdraw->pdpdev[i];
		if (hd->id == pdpdev->cid) {
			process_pdp_packet(rbuf, hd, pdpdev->pdp);
			return 0;
		}
	}
	return -ENODEV;
}

static int process_raw(struct str_smdraw *smdraw, struct str_ring_buf *rbuf,
		struct raw_hdr *hd)
{
	int i;
	struct str_raw_dev *rawdev;

	for (i = 0; i < MAX_RAW_DEV; i++) {
		rawdev = &smdraw->rawdev[i];
		if (hd->id == rawdev->cid) {
			process_raw_packet(rbuf, hd, rawdev);
			return 0;
		}
	}
	return -ENODEV;
}

static void demux_raw(struct urb *urb, struct str_smdraw *smdraw)
{
	int ret;
	bool check_memcpy;
	struct raw_hdr hd;
	struct str_ring_buf *rbuf = &smdraw->read_buf;

	ret = memcpy_to_ringbuf(rbuf, urb->transfer_buffer, urb->actual_length);
	if (ret != urb->actual_length) {
		pr_err("%s: NO memory for ring buffer\n", __func__);
		check_memcpy = true;
		/* do not return here, proceed next to flush buffer */
	} else
		check_memcpy = false;

process_data:
	while (!get_raw_header(rbuf, &hd)) {
		/* check hdlc start byte */
		if (hd.start != HDLC_START) {
			pr_debug("%s:Wrong HD: %x\n", __func__, hd.start);
			drop_hdlc_packet(rbuf);
			continue;
		}

		if (hd.len > 1550) {
			/* this packet exceed max packet length */
			drop_hdlc_packet(rbuf);
			continue;
		}

		if (!packet_has_enough_data(rbuf, &hd))
			break;

		/* find device with channel ID */
		if (!process_pdp(smdraw, rbuf, &hd))
			continue;

		if (!process_raw(smdraw, rbuf, &hd))
			continue;

		/* unknown cid , drop this packet */
		drop_hdlc_packet(rbuf);
	}

	if (check_memcpy) {
		ret = memcpy_to_ringbuf(rbuf, urb->transfer_buffer, urb->actual_length);
		if (ret != urb->actual_length) {
			pr_err("%s: NO memory after proces data, flush buf\n", __func__);
			flush_buf(&smdraw->read_buf);
		} else {
			check_memcpy = false;
			goto process_data;
		}
	}
}

static int smdraw_rx_submit(struct str_smdraw *smdraw)
{
	int r = 0;
	char *buf;
	struct str_hsic *hsic = &smdraw->hsic;
	struct str_smd_urb *rx_urb;

	if (!hsic || !hsic->usb || !hsic->rx_urb[0].urb)
		return -ENODEV;

	tegra_ehci_txfilltuning();

#if 1
	r = get_rx_free_urb(hsic);
	if (r < 0) {
		pr_err("%s:get_rx_free_urb(): all urbs are already in submit\n",
			__func__);
		goto err;
	}
	rx_urb = &hsic->rx_urb[r];
	buf = smdraw->urb_rx_buf[r];
	if (!buf) {
		pr_err("%s:buf : NULL index : %d\n", __func__, r);
		goto err;
	}
#else
	rx_urb = &hsic->rx_urb[0];
	buf = smdraw->urb_rx_buf[0];
#endif
	usb_fill_bulk_urb(rx_urb->urb, hsic->usb, hsic->rx_pipe,
			  buf, RAW_TEMP_BUFFER_SIZE, raw_urb_rx_comp,
			  (void *)smdraw);

	rx_urb->urb->transfer_flags = 0;

	usb_mark_last_busy(hsic->usb);
	r = usb_submit_urb(rx_urb->urb, GFP_ATOMIC);
	if (r) {
		pr_err("%s:usb_submit_urb() failed (%d)\n", __func__, r);
		goto err;
	}
	rx_urb->urb_status = URB_STAT_SUBMIT;

err:
	usb_mark_last_busy(hsic->usb);
	return r;
}

static void raw_urb_rx_comp(struct urb *urb)
{
	struct str_smdraw *smdraw = (struct str_smdraw *)urb->context;
	struct str_hsic *hsic = &smdraw->hsic;

	if (smdraw->pm_stat == RAW_PM_STATUS_SUSPEND) {
		clear_rx_submit(urb, hsic);
		return;
	}

	usb_mark_last_busy(hsic->usb);

	switch (urb->status) {
	case 0:
		if (urb->actual_length) {
#if 0
			pr_info("============== RECEIVE RAW ===============\n");
			dump_buffer(urb->transfer_buffer, urb->actual_length);
			pr_info("==========================================\n");
#endif
			demux_raw(urb, smdraw);
			wake_lock_timeout(&smdraw->rxguide_lock, smdraw->wake_time);			
		}
		goto resubmit;

	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		pr_err("%s:LINK ERROR:%d\n", __func__, urb->status);
		usb_mark_last_busy(hsic->usb);
		clear_rx_submit(urb, hsic);
		return;

	case -EOVERFLOW:
		smdraw->stat.rx_over_err++;
		break;

	case -EILSEQ:
		smdraw->stat.rx_crc_err++;
		break;

	case -ETIMEDOUT:
	case -EPROTO:
		smdraw->stat.rx_pro_err++;
		break;
	default:
		/* fall through to other error */
		break;
	}
	smdraw->stat.rx_err++;
resubmit:
	clear_rx_submit(urb, hsic);
	smdraw_rx_submit(smdraw);
	usb_mark_last_busy(hsic->usb);
	return;
}

static void raw_urb_tx_comp(struct urb *urb)
{
	struct str_smdraw *smdraw = urb->context;

	switch (urb->status) {
	case 0:
		break;

	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		pr_err("%s:LINK ERROR:%d\n", __func__, urb->status);
		break;

	default:
		pr_err("%s:status: %d\n", __func__, urb->status);
		break;
	}

	kfree(urb->transfer_buffer);
	usb_free_urb(urb);

	usb_mark_last_busy(smdraw->hsic.usb);

	return;
}

static void raw_urb_pdp_tx_comp(struct urb *urb)
{
	struct sk_buff *skb = urb->context;
	struct str_pdp_priv *pdppriv = netdev_priv(skb->dev);

	switch (urb->status) {
	case 0:
		break;

	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		pr_err("%s:LINK ERROR:%d\n", __func__, urb->status);
		break;

	default:
		pr_err("%s:status: %d\n", __func__, urb->status);
		break;
	}

	/* queue work for pdp dev */
	if (!pdppriv->smdraw->tx_flow_control)
		queue_delayed_work(pdppriv->smdraw->pdp_wq, &pdppriv->smdraw->pdp_work, 0);

	usb_mark_last_busy(pdppriv->smdraw->hsic.usb);

	dev_kfree_skb_any(skb);
	return;
}

static int smdraw_open(struct inode *inode, struct file *file)
{
	int r;
	struct cdev *cdev = inode->i_cdev;
	struct str_raw_dev *rawdev = container_of(cdev, struct str_raw_dev,
						cdev);
	struct str_smdraw *smdraw = dev_get_drvdata(rawdev->dev);

	pr_info("smdraw_open() for cid : %d\n", rawdev->cid);

	if (atomic_cmpxchg(&rawdev->opened, 0, 1)) {
		pr_err("%s : Already opened\n", __func__);
		return -EBUSY;
	}
	r = alloc_buf(&rawdev->read_buf, RAW_READ_BUF_SIZE);
	if (r) {
		pr_err("%s:alloc_buf() failed\n", __func__);
		atomic_set(&rawdev->opened, 0);
		return r;
	}

	file->private_data = rawdev;

	if (smdraw_rx_submit(smdraw) < 0) {
		pr_err("%s:rx_submit() failed\n", __func__);
		atomic_set(&rawdev->opened, 0);
		return r;
	}

	return 0;
}

static int smdraw_release(struct inode *inode, struct file *file)
{
	struct str_raw_dev *rawdev = file->private_data;
	struct str_smdraw *smdraw = dev_get_drvdata(rawdev->dev);

	if (!smdraw) {
		pr_err("%s:smdraw:NULL\n", __func__);
		return -ENODEV;
	}
	pr_info("smdraw_release() for cid : %d\n", rawdev->cid);

	free_buf(&rawdev->read_buf);

	atomic_set(&rawdev->opened, 0);
	return 0;
}

static int raw_hdlc(const char __user *buf, char *dest_buf, ssize_t len,
		    unsigned int cid)
{
	int r;
	int hdsize;
	struct raw_hdr hd;
	unsigned int offset = 0;
	unsigned int hdlcen = HDLC_END;

	hdsize = sizeof(struct raw_hdr);
	hd.start = HDLC_START;
	hd.len = len + (hdsize - 1);
	hd.control = 0;
	hd.id = cid;
	memcpy(dest_buf, &hd, hdsize);
	offset += hdsize;
	r = copy_from_user(dest_buf + offset, buf, len);
	if (r < 0) {
		pr_err("%s:copy_from_user failed (%d)", __func__, r);
		return -EFAULT;
	}
	offset += len;
	memcpy(dest_buf + offset, &hdlcen, 1);
	offset += 1;

	return offset;
}

static int smdraw_tx_submit(const char __user *data, struct str_smdraw *smdraw,
			    unsigned int cid, ssize_t count)
{
	int r;
	int pktsz;
	char *urbbuf;
	struct str_hsic *hsic = &smdraw->hsic;
	struct urb *urb;

	if (!hsic || !hsic->usb)
		return -ENODEV;

	usb_mark_last_busy(smdraw->hsic.usb);

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return -EAGAIN;

	urbbuf = kzalloc(count + sizeof(struct raw_hdr) + 1, GFP_KERNEL);
	if (!urbbuf) {
		pr_err("%s:kmalloc for drvbuf failed\n", __func__);
		goto err;
	}

	pktsz = raw_hdlc(data, urbbuf, count, cid);
	if (pktsz < 0) {
		pr_err("%s Total pkt : %d, Req : %d\n", __func__, pktsz,
		       count);
		goto err;
	}

#if 0
	pr_info("================= TRANSMIT RAW =================\n");
	dump_buffer(urbbuf, pktsz);
	pr_info("================================================\n");
#endif
	usb_fill_bulk_urb(urb, hsic->usb, hsic->tx_pipe,
			  urbbuf, pktsz, raw_urb_tx_comp, (void *)smdraw);
	urb->transfer_flags = URB_ZERO_PACKET;

	usb_mark_last_busy(smdraw->hsic.usb);

	r = smdhsic_pm_resume_AP();
	if (r < 0) {
		pr_err("%s: HSIC Resume Failed(%d)\n", __func__, r);
		goto err;
	}

	usb_mark_last_busy(smdraw->hsic.usb);

	r = usb_submit_urb(urb, GFP_KERNEL);
	if (r) {
		pr_err("%s:usb_submit_urb() failed (%d)\n", __func__, r);
		goto err;
	}

	usb_mark_last_busy(smdraw->hsic.usb);
	return r;

err:
	if (urb)
		usb_free_urb(urb);
	if (urbbuf)
		kfree(urbbuf);
	return -EAGAIN;
}

static struct sk_buff *smdraw_pdp_skb_fill_hdlc(struct sk_buff *skb,
						unsigned int cid)
{
	char tail = HDLC_END;
	struct raw_hdr hd;
	struct sk_buff *skb_new;

	hd.start = HDLC_START;
	hd.len = skb->len + (sizeof(hd) - 1); /* exclude hdlc start bit len */
	hd.control = 0;
	hd.id = cid;

	skb_new = skb_copy_expand(skb, sizeof(hd), sizeof(tail), GFP_ATOMIC);
	if (!skb_new)
		return NULL;

	dev_kfree_skb_any(skb);

	memcpy(skb_push(skb_new, sizeof(hd)), &hd, sizeof(hd));
	memcpy(skb_put(skb_new, sizeof(tail)), &tail, sizeof(tail));

	return skb_new;
}

static int smdraw_pdp_tx_submit(struct sk_buff *skb)
{
	int r;
	struct urb *urb;
	struct str_pdp_priv *pdppriv = netdev_priv(skb->dev);
	struct str_smdraw *smdraw = pdppriv->smdraw;
	struct str_hsic *hsic = &smdraw->hsic;

	if (!hsic || !hsic->usb)
		return -ENODEV;

	usb_mark_last_busy(hsic->usb);

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb)
		return -ENOMEM;

#if 0
	pr_info("================= TRANSMIT RAW =================\n");
	dump_buffer(skb->data, skb->len);
	pr_info("================================================\n");
#endif
	usb_fill_bulk_urb(urb, hsic->usb, hsic->tx_pipe, skb->data,
			skb->len, raw_urb_pdp_tx_comp, (void *)skb);
	urb->transfer_flags = URB_ZERO_PACKET;

	usb_mark_last_busy(hsic->usb);

	r = usb_submit_urb(urb, GFP_ATOMIC);
	if (r) {
		pr_err("%s:usb_submit_urb() failed (%d) Drop packet\n",
			__func__, r);
		usb_kill_urb(urb);
		usb_free_urb(urb);
		return r;
	}
	skb->dev->stats.tx_packets++;
	skb->dev->stats.tx_bytes += skb->len;
	usb_mark_last_busy(hsic->usb);
	return r;
}

static ssize_t smdraw_write(struct file *file, const char __user * buf,
			    size_t count, loff_t *ppos)
{
	int r;
	int send_byte, byte_total;
	int byte_sent = 0;
	struct str_raw_dev *rawdev = file->private_data;
	struct str_smdraw *smdraw = dev_get_drvdata(rawdev->dev);

	pr_debug("%s: Enter\n", __func__);

	if (!smdraw) {
		pr_err("%s:smdraw:NULL\n", __func__);
		return -ENODEV;
	}

	if (smdraw->pm_stat == RAW_PM_STATUS_DISCONNECT)
		return -ENODEV;

	/* max packet size : 1500 byte */
	while (count > byte_sent) {
		send_byte = (count - byte_sent > 1500) ? 1500 : count - byte_sent;
	
		r = smdraw_tx_submit(buf + byte_sent , smdraw, rawdev->cid, send_byte);
		if (r) {
			pr_err("%s:smdraw_tx_submit() failed\n", __func__);
			return r;
		}
		byte_sent += send_byte;
	}
	return count;
}

static ssize_t smdraw_read(struct file *file, char *buf, size_t count,
			   loff_t *f_pos)
{
	ssize_t ret;
	int pktsize = 0;
	struct str_raw_dev *rawdev = file->private_data;
	struct str_ring_buf *readbuf = &rawdev->read_buf;
	struct raw_hdr hdr;
	char temp;

	if (!readbuf->buf)
		return -EFAULT;

	if (!get_remained_size(readbuf))
		goto done;

find_hdlc_start:
	ret = memcpy_from_ringbuf(readbuf, &hdr.start, 1);
	if ((!ret) && (hdr.start != HDLC_START)) {
		pr_warn("%s:HDLC START Fail: %d, ret: %d \n",
			       __func__, hdr.start, ret);
		goto find_hdlc_start;
	}

	ret = memcpy_from_ringbuf(readbuf, &hdr.len, 4);
	if (!ret) {
		pr_warn("%s:read raw hdr.len failed: %d\n", __func__, ret);
			ret = -1;
	}

	ret = memcpy_from_ringbuf(readbuf, &hdr.id, 1);
	if (!ret) {
		pr_warn("%s:read raw hdr.id failed: %d\n", __func__, ret);
			ret = -1;
	}

	ret = memcpy_from_ringbuf(readbuf, &hdr.control, 1);
	if (!ret) {
		pr_warn("%s:read raw hdr.control failed: %d\n", __func__, ret);
			ret = -1;
	}

	pktsize = hdr.len - sizeof(struct raw_hdr) + 1;
	ret = memcpy_from_ringbuf_user(readbuf, buf, pktsize);
	if (!ret) {
		pr_warn("%s:read ipc data failed: %d\n", __func__, ret);
			ret = -1;
	}

	ret = memcpy_from_ringbuf(readbuf, &temp, 1);
	if ((!ret) && (temp != HDLC_END)) {
		pr_warn("%s:HDLC END: %d, ret: %d\n", __func__, temp, ret);
			ret = -1;
	}

#ifdef DEBUG_LOG
	pr_info("=================== READ RAW ===================\n");
	dump_buffer(buf, pktsize);
	pr_info("================================================\n");
#endif

	if (ret < 0)
		return ret;

done:
	pr_debug("%s##: pktsize: %d \n", __func__, pktsize);

	return pktsize;
}

static unsigned int smdraw_poll(struct file *file,
				struct poll_table_struct *wait)
{
	struct str_raw_dev *rawdev = file->private_data;

	if (wait)
		poll_wait(file, &rawdev->poll_wait, wait);

	if (get_remained_size(&rawdev->read_buf))
		return POLLIN | POLLRDNORM;

	return 0;
}

static const struct file_operations smdraw_fops = {
	.owner = THIS_MODULE,
	.open = smdraw_open,
	.release = smdraw_release,
	.write = smdraw_write,
	.read = smdraw_read,
	.poll = smdraw_poll,
};

static int register_raw_dev(struct str_dev_info *info,
			    struct str_smdraw *smdraw)
{
	int r;
	dev_t devid = 0;
	struct str_raw_dev *rawdev;

	if (smdraw->rawdevcnt >= MAX_RAW_DEV) {
		pr_err("%s:Exceed MAX RAW DEV Count\n", __func__);
		return -ENOSPC;
	}
	rawdev = &smdraw->rawdev[smdraw->rawdevcnt];

	r = alloc_chrdev_region(&devid, info->minor, 1, info->name);
	if (r) {
		pr_err("%s:alloc_chrdev_region()failed\n", __func__);
		return r;
	}

	cdev_init(&rawdev->cdev, &smdraw_fops);

	r = cdev_add(&rawdev->cdev, devid, 1);
	if (r) {
		pr_err("%s:cdev_add() error\n", __func__);
		goto err_cdev_add;
	}
	rawdev->devid = devid;

	rawdev->dev = device_create(smdraw->class, NULL, devid,
				NULL, info->name);
	if (IS_ERR_OR_NULL(rawdev->dev)) {
		r = PTR_ERR(rawdev->dev);
		rawdev->dev = NULL;
		pr_err("%s: device_create() failed\n", __func__);
		goto err_device_create;
	}

	atomic_set(&rawdev->opened, 0);
	rawdev->cid = info->cid;
	init_waitqueue_head(&rawdev->poll_wait);
	dev_set_drvdata(rawdev->dev, smdraw);

	smdraw->rawdevcnt++;
	return 0;

err_device_create:
	cdev_del(&rawdev->cdev);
err_cdev_add:
	unregister_chrdev_region(devid, 1);
	return r;
}

static int smd_vnet_open(struct net_device *net)
{
	struct str_pdp_priv *netpriv = netdev_priv(net);
	if (netpriv == NULL) {
		pr_err("%s: netpriv is NULL\n", __func__);
		return -1;
	}

	pr_info("%s():cid:%d\n", __func__, netpriv->cid);
	smdraw_rx_submit(netpriv->smdraw);
	netif_start_queue(net);

	return 0;
}

static int smd_vnet_stop(struct net_device *net)
{
	struct str_pdp_priv *netpriv = netdev_priv(net);
	if (netpriv == NULL) {
		pr_err("%s: netpriv is NULL\n", __func__);
		return -1;
	}
	pr_info("%s():cid:%d\n\n", __func__, netpriv->cid);
	netif_stop_queue(net);

	return 0;
}

netdev_tx_t smd_vnet_xmit(struct sk_buff *skb, struct net_device *net)
{
	int r;
	struct str_smdraw *smdraw;
	struct str_pdp_priv *netpriv = netdev_priv(net);

	if (netpriv == NULL) {
		pr_err("%s:netpriv is NULL\n", __func__);
		return -1;
	}
	smdraw = netpriv->smdraw;

	skb_queue_tail(&smdraw->pdp_txq, skb);
	if (smdraw->tx_flow_control) {
		netif_stop_queue(net);
		return 0;
	}
	r = queue_delayed_work(smdraw->pdp_wq, &smdraw->pdp_work, 0);
	if (r < 0)
		pr_err("%s: queue_work() Failed\n", __func__);

	return 0;
}

#define PDP_MTU_SIZE 1500
static const struct net_device_ops pdp_net_ops = {
	.ndo_open = smd_vnet_open,
	.ndo_stop = smd_vnet_stop,
	.ndo_start_xmit = smd_vnet_xmit,
};

static void smd_vnet_setup(struct net_device *dev)
{
	dev->netdev_ops = &pdp_net_ops;
	dev->type = ARPHRD_PPP;
	dev->hard_header_len = 0;
	dev->mtu = PDP_MTU_SIZE;
	dev->tx_queue_len = 1000;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
	dev->watchdog_timeo = 30 * HZ;
}

static void pdp_workqueue_handler(struct work_struct *work)
{
	int r;
	unsigned long flags;
	struct sk_buff *skb, *tx_skb;
	struct str_pdp_priv *pdp_priv;
	struct str_smdraw *smdraw = container_of(work, struct str_smdraw, pdp_work.work);

	if (smdraw->pm_stat == RAW_PM_STATUS_DISCONNECT) {
		cancel_delayed_work(&smdraw->pdp_work);
		skb_queue_purge(&smdraw->pdp_txq);
		return;
	}

	if (smdraw->tx_flow_control) {
		cancel_delayed_work(&smdraw->pdp_work);
		return;
	}

	if (!skb_queue_len(&smdraw->pdp_txq))
		return;

	r = smdhsic_pm_resume_AP();
	if (r < 0) {
		if (r == -EAGAIN || r == -ETIMEDOUT) {
			pr_debug("%s:kernel pm resume, delayed(%d)\n",
							__func__, r);
		queue_delayed_work(smdraw->pdp_wq, &smdraw->pdp_work,
					msecs_to_jiffies(50));
		} else {
			pr_err("%s: HSIC Resume Failed(%d)\n", __func__, r);
			skb_queue_purge(&smdraw->pdp_txq);
		}
		return;
	}

	spin_lock_irqsave(&smdraw->lock, flags);
	skb = skb_dequeue(&smdraw->pdp_txq);
	while (skb) {
		if (!smdhsic_pm_active()) {
			pr_err("%s: rpm is not active\n", __func__);
			skb_queue_head(&smdraw->pdp_txq, skb);
			queue_delayed_work(smdraw->pdp_wq, &smdraw->pdp_work,
					msecs_to_jiffies(10));
			spin_unlock_irqrestore(&smdraw->lock, flags);
			return;
		}

		if (smdraw->pm_stat == RAW_PM_STATUS_DISCONNECT) {
			dev_kfree_skb_any(skb);
			cancel_delayed_work(&smdraw->pdp_work);
			skb_queue_purge(&smdraw->pdp_txq);
			spin_unlock_irqrestore(&smdraw->lock, flags);
			return;
		} else
			usb_mark_last_busy(smdraw->hsic.usb);

		pdp_priv = netdev_priv(skb->dev);
		/* skb destroy done by next function call */
		tx_skb = smdraw_pdp_skb_fill_hdlc(skb, pdp_priv->cid);
		if (!tx_skb) {
			pr_err("%s: fill hdlc skb no memory\n", __func__);
			skb_queue_head(&smdraw->pdp_txq, skb);
			queue_delayed_work(smdraw->pdp_wq, &smdraw->pdp_work,
					msecs_to_jiffies(10));
			spin_unlock_irqrestore(&smdraw->lock, flags);
			return;
		}
		/* tx_skb destroy done by tx complete function */
		if (smdraw_pdp_tx_submit(tx_skb)) {
			dev_kfree_skb_any(tx_skb);
			pr_err("%s: smdraw_pdp_tx_submit() failed, drop skb\n",
			     __func__);
			spin_unlock_irqrestore(&smdraw->lock, flags);
			return;
		}
		skb = skb_dequeue(&smdraw->pdp_txq);
	}
	spin_unlock_irqrestore(&smdraw->lock, flags);
}

static int register_raw_net(struct str_dev_info *info,
			    struct str_smdraw *smdraw, unsigned int index)
{
	int ret = 0;
	struct net_device *netdev = NULL;
	struct str_pdp_dev *smdnet = NULL;
	struct str_pdp_priv *netpriv = NULL;
	char devname[IFNAMSIZ];

	pr_info("%s:call\n", __func__);
	if (index >= MAX_PDP_DEV) {
		pr_err("ERROR:%s:Exceed MAX PDP DEV Count \n", __func__);
		return -ENOSPC;
	}

	smdnet = &smdraw->pdpdev[index];
	sprintf(devname, "rmnet%d", index);
	netdev =
	    alloc_netdev(sizeof(struct str_pdp_priv), devname, smd_vnet_setup);
	if (netdev == NULL) {
		pr_err("alloc_netdev() failed\n");
		goto err;
	}

	ret = register_netdev(netdev);
	if (ret != 0) {
		pr_err("register_netdev() failed\n");
		goto err;
	}
	netpriv = (struct str_pdp_priv *)netdev_priv(netdev);
	memset(netpriv, 0, sizeof(struct str_pdp_priv));
	netpriv->cid = info->cid;
	netpriv->smdraw = smdraw;
	netpriv->dev = netdev;

	memset(smdnet, 0, sizeof(struct str_pdp_dev));
	smdnet->pdp = netdev;
	smdnet->cid = info->cid;

	return 0;

 err:
	return -1;
}

static int register_smdraw_dev(struct str_smdraw *smdraw)
{
	int i;
	int r;
	dev_t devid;

	static struct str_dev_info dev_info[MAX_RAW_CHANNEL] = {
		{NAME_RAW_CSD, CID_RAW_CSD, MINOR_RAW_CSD, RAW_TYPE_NOR},
		{NAME_RAW_ROUTER, CID_RAW_ROUTER, MINOR_RAW_ROUTER,
		 RAW_TYPE_NOR},
		{NAME_RAW_LB, CID_RAW_LB, MINOR_RAW_LB, RAW_TYPE_NOR},
		{NAME_RAW_PDP0, CID_RAW_PDP0, MINOR_RAW_PDP0, RAW_TYPE_NET},
		{NAME_RAW_PDP1, CID_RAW_PDP1, MINOR_RAW_PDP1, RAW_TYPE_NET},
		{NAME_RAW_PDP2, CID_RAW_PDP2, MINOR_RAW_PDP2, RAW_TYPE_NET},
	};

	smdraw->class = class_create(THIS_MODULE, "smdraw");
	if (IS_ERR_OR_NULL(smdraw->class)) {
		pr_err("%s:class_create() failed\n", __func__);
		return -EINVAL;
	}

	if (alloc_buf(&smdraw->read_buf, RAW_READ_BUF_SIZE) < 0)
		return -ENOMEM;

	r = alloc_chrdev_region(&devid, SMDRAW_MINOR, 1, SMDRAW_DEVNAME);
	if (r) {
		pr_err("%s: alloc_chrdev_region() failed, r = %d\n",
			__func__, r);
		goto err_alloc_chrdev_region;
	}

	cdev_init(&smdraw->cdev, &smdraw_fops);

	r = cdev_add(&smdraw->cdev, devid, 1);
	if (r) {
		pr_err("%s: cdev_add() failed, r = %d\n", __func__, r);
		goto err_cdev_add;
	}
	smdraw->devid = devid;

	smdraw->dev = device_create(smdraw->class, NULL, smdraw->devid,
				NULL, SMDRAW_DEVNAME);
	if (IS_ERR_OR_NULL(smdraw->dev)) {
		r = PTR_ERR(smdraw->dev);
		smdraw->dev = NULL;
		pr_err("%s: device_create() failed, r = %d\n", __func__, r);
		goto err_device_create;
	}

	for (i = 0; i < MAX_RAW_CHANNEL; i++) {
		if (dev_info[i].dev_type == RAW_TYPE_NOR) {
			r = register_raw_dev(&dev_info[i], smdraw);
			if (r) {
				pr_err("%s:register_raw_dev() failed\n",
				       __func__);
				return r;
			}
		} else if (dev_info[i].dev_type == RAW_TYPE_NET) {
			r = register_raw_net(&dev_info[i], smdraw, smdraw->pdpdevcnt);
			if (r) {
				pr_err("%s:register_raw_net() failed\n",
				       __func__);
				return r;
			}
			smdraw->pdpdevcnt++;
		} else {
			pr_err("%s:undefined dev type : %d\n", __func__,
			       dev_info[i].dev_type);
			return -EINVAL;
		}
	}

	dev_set_drvdata(smdraw->dev, smdraw);
	
	return 0;


err_device_create:
	cdev_del(&smdraw->cdev);
err_cdev_add:
	unregister_chrdev_region(devid, 1);
err_alloc_chrdev_region:
	class_destroy(smdraw->class);
	
	return 0;
	
}

void dereg_smdraw_dev(struct str_smdraw *smdraw)
{
	int i;
	struct str_raw_dev *rawdev;

	for (i = 0; i < smdraw->rawdevcnt; i++) {
		rawdev = &smdraw->rawdev[i];
		dev_set_drvdata(rawdev->dev, NULL);
		device_destroy(smdraw->class, rawdev->devid);
		cdev_del(&rawdev->cdev);
		unregister_chrdev_region(rawdev->devid, 1);
	}

	free_buf(&smdraw->read_buf);
	for (i = 0; i < smdraw->pdpdevcnt; i++) {
		if (smdraw->pdpdev[i].pdp) {
			unregister_netdev(smdraw->pdpdev[i].pdp);
			smdraw->pdpdev[i].pdp = NULL;
		}
	}

	class_destroy(smdraw->class);
}

static void smdraw_free_rx_buf(struct str_smdraw *smdraw)
{
	int i;

	for (i = 0; i < MAX_RX_URB_COUNT; i++)
		kfree(smdraw->urb_rx_buf[i]);

	return;
}

static int smdraw_alloc_rx_buf(struct str_smdraw *smdraw)
{
	int i;

	for (i = 0; i < MAX_RX_URB_COUNT; i++) {
		smdraw->urb_rx_buf[i] = kzalloc(RAW_TEMP_BUFFER_SIZE,
						GFP_KERNEL);
		if (!smdraw->urb_rx_buf[i]) {
			pr_err("%s:kmalloc for urb_rx_buf failed\n",
			       __func__);
			/* unwind */
			smdraw_free_rx_buf(smdraw);
			return -ENOMEM;
		}
	}
	return 0;
}

void *init_smdraw(void)
{
	int r;
	struct str_smdraw *smdraw;

	pr_info("%s: Enter\n", __func__);

	smdraw = kzalloc(sizeof(*smdraw), GFP_KERNEL);
	if (!smdraw) {
		pr_err("%s:malloc for smdraw failed\n", __func__);
		return NULL;
	}

	r = register_smdraw_dev(smdraw);
	if (r) {
		pr_err("%s:register_smdraw_dev() failed (%d)\n", __func__, r);
		goto err_register_smdraw_dev;
	}

	r = smdraw_alloc_rx_buf(smdraw);
	if (r) {
		pr_err("%s:smdraw_alloc_rx_buf()", __func__);
		goto err_alloc_rx_buf;
	}

	smdraw->hsic.rx_urbcnt = RAW_RX_URB_CNT;

	r = smd_alloc_urb(&smdraw->hsic);
	if (r) {
		pr_err("%s:smd_alloc_urb() failed\n", __func__);
		goto err_alloc_urb;
	}

	init_MUTEX(&smdraw->hsic.hsic_mutex);

	smdraw->pm_stat = RAW_PM_STATUS_SUSPEND;

	wake_lock_init(&smdraw->wakelock, WAKE_LOCK_SUSPEND, "smdraw");
	wake_lock_init(&smdraw->rxguide_lock, WAKE_LOCK_SUSPEND, "rxraw");

	/* move it to smdraw common struct */
	smdraw->pdp_wq = create_workqueue("smdpdpd");
	if (smdraw->pdp_wq == NULL) {
		pr_err("%s:create_workqueue() failed\n", __func__);
		goto err_create_workqueue;
	}

	smdraw->wake_time = DEFAULT_WAKE_TIME;
	r = sysfs_create_group(&smdraw->dev->kobj, &smdraw_sysfs);
	if (r) {
		pr_err("%s: Failed to create sysfs group\n",
				__func__);
		goto err_sysfs_create_group;
	}


	INIT_DELAYED_WORK(&smdraw->pdp_work, pdp_workqueue_handler);
	skb_queue_head_init(&smdraw->pdp_txq);
	spin_lock_init(&smdraw->lock);
	pr_info("%s: End\n", __func__);

	return smdraw;

err_sysfs_create_group:
	destroy_workqueue(smdraw->pdp_wq);
err_create_workqueue:
	wake_lock_destroy(&smdraw->wakelock);
	wake_lock_destroy(&smdraw->rxguide_lock);
	smd_free_urb(&smdraw->hsic);
err_alloc_urb:
	smdraw_free_rx_buf(smdraw);
err_alloc_rx_buf:
	dereg_smdraw_dev(smdraw);
err_register_smdraw_dev:
	kfree(smdraw);
	return NULL;
}

void *connect_smdraw(void *smd_device, struct str_hsic *hsic)
{
	struct str_smdraw *smdraw = smd_device;
	struct net_device *netdev;
	int i ;

	smdraw->hsic.intf = hsic->intf;
	smdraw->hsic.usb = hsic->usb;
	smdraw->hsic.rx_pipe = hsic->rx_pipe;
	smdraw->hsic.tx_pipe = hsic->tx_pipe;

	smdraw->pm_stat = RAW_PM_STATUS_RESUME;

	cancel_delayed_work(&smdraw->pdp_work);
	skb_queue_purge(&smdraw->pdp_txq);

	smdraw_rx_submit(smdraw);

	for (i = 0; i < MAX_PDP_DEV; i++) {
		netdev = smdraw->pdpdev[i].pdp;
		if (netdev)
			netif_wake_queue(netdev);
	}
	
	return smd_device;
	}

void disconnect_smdraw(void *smd_device)
{
	struct str_smdraw *smdraw = smd_device;
	struct str_pdp_priv *pdppriv = netdev_priv(smdraw->pdpdev[0].pdp);
	int i;

	for (i=0;i < MAX_PDP_DEV;i++)
		if (smdraw->pdpdev[i].pdp)
			smd_vnet_stop(smdraw->pdpdev[i].pdp);

	cancel_delayed_work(&smdraw->pdp_work);
	skb_queue_purge(&smdraw->pdp_txq);

	smdraw->pm_stat = RAW_PM_STATUS_DISCONNECT;
	smdraw->suspended = 0;
	smdraw->tx_flow_control = false;

	flush_buf(&smdraw->read_buf);
	smd_kill_urb(&smdraw->hsic);
	wake_unlock(&smdraw->wakelock);
	wake_unlock(&smdraw->rxguide_lock);

	smdraw->hsic.usb = NULL;
}

void exit_smdraw(void *param)
{
	struct str_smdraw *smdraw = param;

	pr_info("%s: Enter\n", __func__);

	wake_lock_destroy(&smdraw->wakelock);
	wake_lock_destroy(&smdraw->rxguide_lock);

	smd_free_urb(&smdraw->hsic);
	smdraw_free_rx_buf(smdraw);
	dereg_smdraw_dev(smdraw);
	kfree(smdraw);
}

int smdraw_suspend(struct str_smdraw *smdraw)
{
	if (smdraw->pm_stat == RAW_PM_STATUS_RESUME) {
		smdraw->pm_stat = RAW_PM_STATUS_SUSPEND;
		smd_kill_urb(&smdraw->hsic);
	}
	return 0;
}

int smdraw_resume(struct str_smdraw *smdraw)
{
	int r;
	int retrycnt;

	if (smdraw->pm_stat != RAW_PM_STATUS_SUSPEND)
		return 0;

	for (retrycnt = 0; retrycnt < 50; retrycnt++) {
		smdraw->pm_stat = RAW_PM_STATUS_RESUME;
		r = smdraw_rx_submit(smdraw);
		if (!r)
			return 0;
		usleep_range(1000, 10000);
	}
	pr_err("%s:smdrfs_rx_usb() failed : %d\n", __func__, r);
	return r;
}
