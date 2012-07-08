/*
 * driver/misc/smd-hsic/smd_down.c
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
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/mutex.h>
#include <linux/gfp.h>

#include "smd_ipc.h"
#include "smd_core.h"
#include "smd_down.h"

#undef DISABLE_COMP

#define DOWN_RX_URB_CNT		1

#define DOWN_TEMP_BUFFER_SIZE	4096
#define COMP_REQUEST		 1

static void smd_down_rx_comp(struct urb *urb);
static void smd_down_tx_comp(struct urb *urb);

static int smd_down_rx_usb(struct str_smd_down *smddown)
{
	int r;
	char *buf = smddown->tpbuf;
	struct str_hsic *hsic = &smddown->hsic;
	struct urb *urb;

	if (!hsic || !hsic->usb)
		return -ENODEV;

	if (!hsic->rx_urb[0].urb) {
		pr_err("%s: hsic->rx_urb[0].urb is NULL\n", __func__);
		return -ENODEV;
	}
	urb = hsic->rx_urb[0].urb;

	usb_fill_bulk_urb(urb, hsic->usb, hsic->rx_pipe,
			  buf, DOWN_TEMP_BUFFER_SIZE, smd_down_rx_comp,
			  (void *)smddown);

	urb->transfer_flags = 0;

	r = usb_submit_urb(hsic->rx_urb[0].urb, GFP_ATOMIC);
	if (r)
		pr_err("%s:usb_submit_urb() failed\n (%d)\n", __func__, r);

	return r;
}

static void smd_down_rx_work(struct work_struct *work)
{
	int r;
	struct str_smd_down *smddown =
		container_of(work, struct str_smd_down, rx_work.work);
	struct urb *urb = smddown->delayed_urb;
	struct completion *comp = &smddown->rdcomp;

	if (urb->actual_length) {
		r = memcpy_to_ringbuf(&smddown->read_buf,
					  smddown->tpbuf,
					  urb->actual_length);
		if (r == -ENOSPC) {
			schedule_delayed_work(&smddown->rx_work,
					msecs_to_jiffies(500));
			pr_debug("%s:ring buffer full-- delayed work\n",
				__func__);
			return;
		}
		if (r < 0) {
			pr_err("%s:memcpy_to_ringbuf() failed! r:%d\n",
				__func__, r);
			goto rx_usb;
		}

		if (smddown->rdcomp_req) {
			complete(comp);
			smddown->rdcomp_req = 0;
		}
#if 0
		pr_info("================= RECEIVE DOWN =================\n");
		dump_buffer(smddown->tpbuf, urb->actual_length);
		pr_info("================================================\n");
#endif
	}
rx_usb:
	smd_down_rx_usb(smddown);
}

static void smd_down_rx_comp(struct urb *urb)
{
	struct str_smd_down *smddown = (struct str_smd_down *)urb->context;

	switch (urb->status) {
	case 0:
		smddown->delayed_urb = urb;
		schedule_delayed_work(&smddown->rx_work, 0);
		/*resubmit will be called at the work*/
		return;
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		pr_err("%s:LINK ERROR:%d\n", __func__, urb->status);
		return;

	case -EOVERFLOW:
		smddown->stat.rx_over_err++;
		break;

	case -EILSEQ:
		smddown->stat.rx_crc_err++;
		break;

	case -ETIMEDOUT:
	case -EPROTO:
		smddown->stat.rx_pro_err++;
		break;
	}
	smddown->stat.rx_err++;

	smd_down_rx_usb(smddown);
}

static void smd_down_tx_comp(struct urb *urb)
{
	pr_debug("%s:status:%d\n", __func__, urb->status);

	if (urb->status != 0)
		pr_err("%s:Wrong Status:%d\n", __func__, urb->status);

	kfree(urb->transfer_buffer);
	usb_free_urb(urb);

	return;
}

static int smd_down_open(struct inode *inode, struct file *file)
{
	int r;
	struct cdev *cdev = inode->i_cdev;
	struct str_smd_down *smddown =
		container_of(cdev, struct str_smd_down, cdev);

	pr_info("smddown_open\n");

	if (atomic_cmpxchg(&smddown->opened, 0, 1)) {
		pr_err("%s : already opened..\n", __func__);
		return -EBUSY;
	}

	init_err_stat(&smddown->stat);

	r = smd_down_rx_usb(smddown);
	if (r) {
		pr_err("%s:smd_down is not ready(%d)\n",
			__func__, r);
		atomic_set(&smddown->opened, 0);
		if (smddown->open_fail_cnt++ > 10) {
			r = -ENOTCONN;
			smddown->open_fail_cnt = 0;
		}
		return r;
	}

	smddown->open_fail_cnt = 0;
	file->private_data = smddown;
	return 0;
}

static int smd_down_release(struct inode *inode, struct file *file)
{
	struct str_smd_down *smddown = file->private_data;

	pr_info("smddown_release()\n");

	cancel_delayed_work_sync(&smddown->rx_work);
	smd_kill_urb(&smddown->hsic);
	atomic_set(&smddown->opened, 0);

	return 0;
}

#define WRITE_TIME_OUT	    (HZ*1)
#define READ_TIME_OUT	    HZ

static ssize_t smd_down_write(struct file *file, const char __user *buf,
				  size_t count, loff_t *ppos)
{
	struct str_smd_down *smddown = file->private_data;
	struct str_hsic *hsic = &smddown->hsic;
	int r;
	char *drvbuf;
	struct urb *urb;

	if (!hsic)
		return -ENODEV;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		pr_err("%s: tx urb is NULL\n", __func__);
		return -ENODEV;
	}

	drvbuf = kmalloc(count, GFP_KERNEL);
	if (!drvbuf) {
		pr_err("%s:kmalloc for drvbuf failed\n", __func__);
		r = -EAGAIN;
		goto exit;
	}

	r = copy_from_user(drvbuf, buf, count);
	if (r < 0) {
		pr_err("%s:copy_form_user failed(ret : %d)..\n",
			__func__, r);
		goto exit;
	}
	/* don't need resume USB?? */
	usb_fill_bulk_urb(urb, hsic->usb, hsic->tx_pipe,
			  drvbuf, count, smd_down_tx_comp, smddown);
	urb->transfer_flags = URB_ZERO_PACKET;

	r = usb_submit_urb(urb, GFP_KERNEL);
	if (r) {
		pr_err("%s:usb_submit_urb() failed (%d)\n", __func__, r);
		goto exit;
	}
	return count;
exit:
	usb_free_urb(urb);
	kfree(drvbuf);
	return r;
}

static ssize_t smd_down_read(struct file *file, char *buf, size_t count,
				 loff_t *f_pos)
{
	ssize_t ret = 0;
	int remaining_bytes = 0;
	int retry = 5;
	struct str_smd_down *smddown = file->private_data;
	struct str_ring_buf *readbuf = &smddown->read_buf;

	smddown->rdcomp_req = 0;

	if (count) {
		int i;
		for (i = 0; i < retry; i++) {
			remaining_bytes = get_remained_size(readbuf);
			if (remaining_bytes < count) {
				init_completion(&smddown->rdcomp);
				smddown->rdcomp_req = COMP_REQUEST;
				ret = wait_for_completion_timeout(&smddown->rdcomp,
							READ_TIME_OUT);
				if (!ret)
					pr_debug("timed out for read comp\n");
				continue;
			} else
				break;
		}
		if (i == retry) {
			pr_warn("%s:comp timeout req: %d data: %d\n",
				__func__, count, remaining_bytes);
			count = remaining_bytes;

			if (remaining_bytes == 0)
#if defined (CONFIG_MACH_BOSE_ATT)
				atomic_set(&smddown->opened, 0);
#endif
				return -EAGAIN;
		}
	}
	/* if count == 0, memcpy from ring buffer returns all remaining bytes */
	ret = memcpy_from_ringbuf_user(readbuf, buf, count);
	if (ret < 0) {
		pr_err("%s:memcpy_from_ringbuf_user() failed(%d)\n",
			   __func__, ret);
		return ret;
	}
	pr_debug("read done : %d\n", ret);
	return ret;
}

static const struct file_operations smd_down_fops = {
	.owner = THIS_MODULE,
	.open = smd_down_open,
	.release = smd_down_release,
	.write = smd_down_write,
	.read = smd_down_read,
};

static int register_smddown_dev(struct str_smd_down *smddown)
{
	int r;
	dev_t devid;

	if (!smddown) {
		pr_err("%s: smddown is NULL\n", __func__);
		return -EINVAL;
	}

	smddown->class = class_create(THIS_MODULE, SMDDOWN_DEVNAME);
	if (IS_ERR_OR_NULL(smddown->class)) {
		r = PTR_ERR(smddown->class);
		smddown->class = NULL;
		pr_err("%s: class_create() failed, r = %d\n", __func__, r);
		return r;
	}

	r = alloc_chrdev_region(&devid, SMDDOWN_MINOR, 1, SMDDOWN_DEVNAME);
	if (r) {
		pr_err("%s: alloc_chrdev_region() failed, r = %d\n",
			__func__, r);
		goto err_alloc_chrdev_region;
	}

	cdev_init(&smddown->cdev, &smd_down_fops);

	r = cdev_add(&smddown->cdev, devid, 1);
	if (r) {
		pr_err("%s: cdev_add() failed, r = %d\n", __func__, r);
		goto err_cdev_add;
	}
	smddown->devid = devid;

	smddown->dev = device_create(smddown->class, NULL, smddown->devid,
				NULL, SMDDOWN_DEVNAME);
	if (IS_ERR_OR_NULL(smddown->dev)) {
		r = PTR_ERR(smddown->dev);
		smddown->dev = NULL;
		pr_err("%s: device_create() failed, r = %d\n", __func__, r);
		goto err_device_create;
	}

	atomic_set(&smddown->opened, 0);
	init_waitqueue_head(&smddown->poll_wait);
	dev_set_drvdata(smddown->dev, smddown);

	return 0;

err_device_create:
	cdev_del(&smddown->cdev);
err_cdev_add:
	unregister_chrdev_region(devid, 1);
err_alloc_chrdev_region:
	class_destroy(smddown->class);
	return r;
}

static void dereg_smddown_dev(struct str_smd_down *smddown)
{
	dev_set_drvdata(smddown->dev, NULL);
	device_destroy(smddown->class, smddown->devid);
	cdev_del(&smddown->cdev);
	unregister_chrdev_region(smddown->devid, 1);
	class_destroy(smddown->class);
}

void *init_smd_down(void)
{
	int r;
	struct str_smd_down *smddown = kzalloc(sizeof(*smddown), GFP_KERNEL);
	if (!smddown) {
		pr_err("%s:malloc for smdipc failed\n", __func__);
		return NULL;
	}

	r = register_smddown_dev(smddown);
	if (r) {
		pr_err("%s:register_smdipc_dev() failed (%d)\n", __func__, r);
		goto err_register_smddown_dev;
	}

	smddown->tpbuf = kmalloc(DOWN_TEMP_BUFFER_SIZE, GFP_DMA);
	if (!smddown->tpbuf) {
		pr_err("%s:malloc ipc tp buf failed\n", __func__);
		goto err_kmalloc_tpbuf;
	}

	r = alloc_buf(&smddown->read_buf, DOWN_READ_BUF_SIZE);
	if (r) {
		pr_err("%s : alloc_buf() failed\n", __func__);
		goto err_alloc_buf;
	}

	smddown->hsic.rx_urbcnt = DOWN_RX_URB_CNT;

	r = smd_alloc_urb(&smddown->hsic);
	if (r) {
		pr_err("%s:smd_alloc_urb() failed\n", __func__);
		goto err_alloc_urb;
	}

	INIT_DELAYED_WORK(&smddown->rx_work, smd_down_rx_work);
	return smddown;

err_alloc_urb:
	free_buf(&smddown->read_buf);
err_alloc_buf:
	kfree(smddown->tpbuf);
err_kmalloc_tpbuf:
	dereg_smddown_dev(smddown);
err_register_smddown_dev:
	kfree(smddown);
	return NULL;
}

void *connect_smd_down(void *smd_device, struct str_hsic *hsic)
{
	struct str_smd_down *smddown = smd_device;

	smddown->hsic.intf = hsic->intf;
	smddown->hsic.usb = hsic->usb;
	smddown->hsic.rx_pipe = hsic->rx_pipe;
	smddown->hsic.tx_pipe = hsic->tx_pipe;

	return smd_device;
}

void disconnect_smd_down(void *smd_device)
{
	struct str_smd_down *smddown = smd_device;
	smd_kill_urb(&smddown->hsic);
}

void exit_smd_down(void *param)
{
	struct str_smd_down *smddown = param;

	cancel_delayed_work_sync(&smddown->rx_work);
	smd_free_urb(&smddown->hsic);
	free_buf(&smddown->read_buf);
	kfree(smddown->tpbuf);
	dereg_smddown_dev(smddown);
	kfree(smddown);
}
