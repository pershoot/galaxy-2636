/*
 * driver/misc/smd-hsic/smd_rfs.c
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
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/mutex.h>
#include <linux/gfp.h>

#include "smd_core.h"
#include "smd_rfs.h"

#include "../smdctl/smd_ctl.h"

#define RFS_COMP_REQ		1
#define WRITE_TIME_OUT		(HZ*5)

#define RFS_TEMP_BUFFER_SIZE	(512*1024)

#define RFS_TX_URB_CNT		1
#define RFS_RX_URB_CNT		1

#undef DEBUG_LOG

static void rfs_rx_comp(struct urb *urb);
static void rfs_tx_comp(struct urb *urb);

static int smdrfs_rx_usb(struct str_smdrfs *smdrfs)
{
	int r;
	char *buf = smdrfs->tpbuf;
	struct str_hsic *hsic = &smdrfs->hsic;
	struct urb *urb;

	if (smdrfs->pm_stat == RFS_PM_STATUS_SUSPEND ||
		smdrfs->pm_stat == RFS_PM_STATUS_DISCONNECT)
		return -ENODEV;

	if (!hsic || !hsic->usb || !hsic->rx_urb[0].urb)
		return -EFAULT;

	tegra_ehci_txfilltuning();

	urb = hsic->rx_urb[0].urb;

	usb_fill_bulk_urb(urb, hsic->usb, hsic->rx_pipe,
			  buf, RFS_TEMP_BUFFER_SIZE, rfs_rx_comp,
			  (void *)smdrfs);

	urb->transfer_flags = 0;

	usb_mark_last_busy(hsic->usb);
	r = usb_submit_urb(urb, GFP_ATOMIC);
	if (r)
		pr_err("%s:usb_submit_urb() failed (%d)\n", __func__, r);

	return r;
}

static int process_rfs_data(struct str_smdrfs *smdrfs, int length)
{
	int r;
	int rcvsize = length;
	int offset = 0;
	int cpsize;
	struct rfs_hdlc_hdr hdr;

	while (rcvsize) {
		pr_debug("%s:rcvsize : %d, remind : %d\n",
			__func__, rcvsize, smdrfs->remind_cnt);
		if (smdrfs->remind_cnt) {
			if (rcvsize < smdrfs->remind_cnt) {
				cpsize = rcvsize;
				smdrfs->remind_cnt -= cpsize;
			} else {
				cpsize = smdrfs->remind_cnt;
				smdrfs->remind_cnt = 0;
				smdrfs->frame_cnt++;
			}
		} else {
			memcpy(&hdr, smdrfs->tpbuf + offset,
			       sizeof(struct rfs_hdlc_hdr));
			if (hdr.start != HDLC_START) {
				pr_err("Wrong HDLC HEADER : %d\n",
				       hdr.start);
				pr_info("DROP Data : %d\n", rcvsize);
				return rcvsize;
			}

			if (hdr.len + 2 > rcvsize) {
				smdrfs->remind_cnt = (hdr.len + 2) - rcvsize;
				cpsize = rcvsize;
			} else {
				smdrfs->remind_cnt = 0;
				cpsize = hdr.len + 2;
				smdrfs->frame_cnt++;
			}
		}
		pr_debug("%s: copy size : %d, remind_cnt : %d\n",
			__func__, cpsize, smdrfs->remind_cnt);

		r = memcpy_to_ringbuf(&smdrfs->read_buf, smdrfs->tpbuf + offset,
				      cpsize);
		if (r < 0) {
			pr_err("WARNING:%s:No Space.. Drop\n", __func__);
			return rcvsize;
		}
		offset += cpsize;
		rcvsize -= cpsize;
		pr_debug("offset : %d, rcvsize : %d\n", offset, rcvsize);
	}

	if (smdrfs->frame_cnt)
		wake_up_interruptible(&smdrfs->poll_wait);

	pr_debug("curr frame cnt : %d\n", smdrfs->frame_cnt);
	return rcvsize;

}

static void rfs_rx_comp(struct urb *urb)
{
	int status = urb->status;
	struct str_smdrfs *smdrfs = urb->context;

	if (smdrfs->pm_stat == RFS_PM_STATUS_SUSPEND)
		return;

	usb_mark_last_busy(smdrfs->hsic.usb);

	switch (status) {
	case -ENOENT:
	case 0:
		if (urb->actual_length) {
			process_rfs_data(smdrfs, urb->actual_length);
			wake_lock_timeout(&smdrfs->rxguide_lock, HZ*3);
#if defined(DEBUG_LOG)
			pr_info("=============== RECEIVE RFS ==============\n");
			dump_buffer(smdrfs->tpbuf, urb->actual_length);
			pr_info("==========================================\n");
#endif
		}
		if (!urb->status)
			goto resubmit;
		break;

	case -ECONNRESET:
	case -ESHUTDOWN:
		pr_err("%s:LINK ERROR:%d\n", __func__, status);
		return;

	case -EOVERFLOW:
		smdrfs->stat.rx_over_err++;
		break;

	case -EILSEQ:
		smdrfs->stat.rx_crc_err++;
		break;

	case -ETIMEDOUT:
	case -EPROTO:
		smdrfs->stat.rx_pro_err++;
		break;
	}
	smdrfs->stat.rx_err++;

resubmit:
	smdrfs_rx_usb(smdrfs);
}

static void rfs_tx_comp(struct urb *urb)
{
	if (urb->status != 0)
		pr_warn("%s:Wrong Status:%d\n", __func__, urb->status);

	kfree(urb->transfer_buffer);
	usb_free_urb(urb);
}

static int smdrfs_open(struct inode *inode, struct file *file)
{
	int r;
	struct cdev *cdev = inode->i_cdev;
	struct str_smdrfs *smdrfs = container_of(cdev, struct str_smdrfs, cdev);

	pr_info("%s: Enter\n", __func__);

	if (smdrfs->pm_stat == RFS_PM_STATUS_DISCONNECT) {
		pr_err("%s : disconnected\n", __func__);
		return -ENODEV;
	}

	if (!smdrfs->hsic.usb) {
		pr_err("%s : no resources\n", __func__);
		return -ENODEV;
	}

	if (atomic_cmpxchg(&smdrfs->opened, 0, 1)) {
		pr_err("%s : already opened..\n", __func__);
		return -EBUSY;
	}

	init_err_stat(&smdrfs->stat);

	usb_mark_last_busy(smdrfs->hsic.usb);
	r = smdhsic_pm_resume_AP();
	if (r < 0) {
		pr_err("%s: HSIC Resume Failed(%d)\n", __func__, r);
		atomic_set(&smdrfs->opened, 0);
		return -EBUSY;
	}
	smd_kill_urb(&smdrfs->hsic);
	usb_mark_last_busy(smdrfs->hsic.usb);

	r = smdrfs_rx_usb(smdrfs);
	if (r) {
		pr_err("%s:smdrfs_rx_usb() failed\n (%d)\n",
			__func__, r);
		atomic_set(&smdrfs->opened, 0);
		return r;
	}

	smdrfs->remind_cnt = 0;
	file->private_data = smdrfs;

	return 0;
}

static int smdrfs_release(struct inode *inode, struct file *file)
{
	struct str_smdrfs *smdrfs = file->private_data;
	pr_info("%s: Enter\n", __func__);
	smd_kill_urb(&smdrfs->hsic);
	atomic_set(&smdrfs->opened, 0);
	return 0;
}

static ssize_t smdrfs_write(struct file *file, const char __user * buf,
			    size_t count, loff_t *ppos)
{
	int r;
	char *drvbuf;
	int pktsz = count + 2;
	struct str_smdrfs *smdrfs = file->private_data;
	struct str_hsic *hsic = &smdrfs->hsic;
	struct urb *urb;

	if (!hsic)
		return -ENODEV;

	if (smdrfs->pm_stat == RFS_PM_STATUS_DISCONNECT)
		return -ENODEV;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		pr_err("%s: tx urb is NULL\n", __func__);
		return -ENODEV;
	}

	drvbuf = kmalloc(pktsz, GFP_KERNEL);
	if (!drvbuf) {
		pr_err("%s:kmalloc for drvbuf failed\n", __func__);
		r = -EAGAIN;
		goto exit;
	}

	drvbuf[0] = HDLC_START;
	drvbuf[pktsz-1] = HDLC_END;
	r = copy_from_user(drvbuf + 1, buf, count);
	if (r < 0) {
		pr_err("%s:copy_form_user failed(ret : %d)..\n", __func__, r);
		r = -EFAULT;
		goto exit;
	}

	usb_fill_bulk_urb(urb, hsic->usb, hsic->tx_pipe,
			  drvbuf, pktsz, rfs_tx_comp, smdrfs);
	urb->transfer_flags = URB_ZERO_PACKET;

#if defined(DEBUG_LOG)
	pr_info("================= TRANSMIT RFS =================\n");
	dump_buffer(drvbuf, pktsz);
	pr_info("================================================\n");
#endif

	add_tail_txurb(hsic->txq, urb);
	queue_tx_work();

	return count;
exit:
	if (urb)
		usb_free_urb(urb);
	if (drvbuf)
		kfree(drvbuf);
	return r;
}

static ssize_t smdrfs_read(struct file *file, char *buf, size_t count,
			   loff_t *f_pos)
{
	ssize_t ret;
	char temp;
	int pktsize;
	int offset;
	struct rfs_hdr hdr;
	struct str_smdrfs *smdrfs = file->private_data;
	struct str_ring_buf *readbuf = &smdrfs->read_buf;

	if (!readbuf->buf)
		return -EFAULT;

	if (!get_remained_size(readbuf) || !smdrfs->frame_cnt)
		return 0;

find_hdlc_start:
	ret = memcpy_from_ringbuf(readbuf, &temp, 1);
	if ((!ret) && (temp != HDLC_START)) {
		pr_warn("%s:get hdlc start byte : %d, ret : %d\n",
			__func__, temp, ret);
		goto find_hdlc_start;
	}

	ret = memcpy_from_ringbuf(readbuf, (char *)&hdr, sizeof(hdr));
	if (!ret) {
		pr_warn("%s:read ipc hdr failed : %d\n", __func__, ret);
		return ret;
	}
	pktsize = hdr.len - sizeof(struct rfs_hdr);
	ret = copy_to_user(buf, (char *)&hdr, sizeof(struct rfs_hdr));
	if (ret < 0) {
		pr_err("%s:copy_to_user failed :%d\n", __func__, ret);
		return ret;
	}
	offset = sizeof(struct rfs_hdr);
	ret = memcpy_from_ringbuf_user(readbuf, buf + offset, pktsize);
	if (!ret) {
		pr_warn("%s:read ipc data failed : %d\n", __func__, ret);
		return ret;
	}
	offset += pktsize + 2;
	ret = memcpy_from_ringbuf(readbuf, &temp, 1);
	if ((!ret) && (temp != HDLC_END)) {
		pr_warn("%s:get hdlc end byte: %d, ret: %d\n",
			__func__, temp, ret);
		return ret;
	}
#if defined(DEBUG_LOG)
	pr_info("=================== READ RFS ===================\n");
	dump_buffer(buf, hdr.len);
	pr_info("================================================\n");
#endif

	smdrfs->frame_cnt--;
	return hdr.len;
}

static unsigned int smdrfs_poll(struct file *file,
				struct poll_table_struct *wait)
{
	struct str_smdrfs *smdrfs = file->private_data;

	if (smdrfs->frame_cnt)
		return POLLIN | POLLRDNORM;

	if (wait)
		poll_wait(file, &smdrfs->poll_wait, wait);
	else
		usleep_range(2000, 10000);

	if (smdrfs->frame_cnt)
		return POLLIN | POLLRDNORM;
	else
		return 0;
}

static const struct file_operations smdrfs_fops = {
	.owner = THIS_MODULE,
	.open = smdrfs_open,
	.release = smdrfs_release,
	.write = smdrfs_write,
	.read = smdrfs_read,
	.poll = smdrfs_poll,
};

static int register_smdrfs_dev(struct str_smdrfs *smdrfs)
{
	int r;
	dev_t devid;

	if (!smdrfs) {
		pr_err("%s: smdrfs is NULL\n", __func__);
		return -EINVAL;
	}

	smdrfs->class = class_create(THIS_MODULE, SMDRFS_DEVNAME);
	if (IS_ERR_OR_NULL(smdrfs->class)) {
		r = PTR_ERR(smdrfs->class);
		smdrfs->class = NULL;
		pr_err("%s: class_create() failed, r = %d\n", __func__, r);
		return r;
	}

	r = alloc_chrdev_region(&devid, SMDRFS_MINOR, 1, SMDRFS_DEVNAME);
	if (r) {
		pr_err("%s: alloc_chrdev_region() failed, r = %d\n",
			__func__, r);
		goto err_alloc_chrdev_region;
	}

	cdev_init(&smdrfs->cdev, &smdrfs_fops);
	r = cdev_add(&smdrfs->cdev, devid, 1);
	if (r) {
		pr_err("%s: cdev_add() failed, r = %d\n", __func__, r);
		goto err_cdev_add;
	}
	smdrfs->devid = devid;

	smdrfs->dev = device_create(smdrfs->class, NULL, smdrfs->devid,
				NULL, SMDRFS_DEVNAME);
	if (IS_ERR_OR_NULL(smdrfs->dev)) {
		r = PTR_ERR(smdrfs->dev);
		smdrfs->dev = NULL;
		pr_err("%s: device_create() failed, r = %d\n", __func__, r);
		goto err_device_create;
	}

	atomic_set(&smdrfs->opened, 0);
	init_waitqueue_head(&smdrfs->poll_wait);
	dev_set_drvdata(smdrfs->dev, smdrfs);

	return 0;
err_device_create:
	cdev_del(&smdrfs->cdev);
err_cdev_add:
	unregister_chrdev_region(devid, 1);
err_alloc_chrdev_region:
	class_destroy(smdrfs->class);
	return r;
}

static void dereg_smdrfs_dev(struct str_smdrfs *smdrfs)
{
	dev_set_drvdata(smdrfs->dev, NULL);
	device_destroy(smdrfs->class, smdrfs->devid);
	cdev_del(&smdrfs->cdev);
	unregister_chrdev_region(smdrfs->devid, 1);
	class_destroy(smdrfs->class);
}

void *init_smdrfs(void)
{
	int r;
	struct str_smdrfs *smdrfs;

	pr_info("%s: Enter\n", __func__);
	smdrfs = kzalloc(sizeof(*smdrfs), GFP_KERNEL);
	if (!smdrfs) {
		pr_err("%s:malloc for smdrfs failed\n", __func__);
		return NULL;
	}

	r = register_smdrfs_dev(smdrfs);
	if (r) {
		pr_err("%s:register_smdrfs_dev() failed (%d)\n", __func__, r);
		goto err_register_smdrfs_dev;
	}

	smdrfs->tpbuf = kmalloc(RFS_TEMP_BUFFER_SIZE, GFP_DMA);
	if (!smdrfs->tpbuf) {
		pr_err("%s:malloc ipc tp buf failed\n", __func__);
		goto err_kmalloc_tpbuf;
	}

	r = alloc_buf(&smdrfs->read_buf, RFS_READ_BUF_SIZE);
	if (r) {
		pr_err("%s: alloc_buf() failed\n", __func__);
		goto err_alloc_buf;
	}

	smdrfs->hsic.rx_urbcnt = RFS_RX_URB_CNT;

	r = smd_alloc_urb(&smdrfs->hsic);
	if (r) {
		pr_err("%s:smd_alloc_urb() failed\n", __func__);
		goto err_smd_alloc_urb;
	}

	smdrfs->pm_stat = RFS_PM_STATUS_SUSPEND;

	wake_lock_init(&smdrfs->wakelock, WAKE_LOCK_SUSPEND, "smdrfs");
	wake_lock_init(&smdrfs->rxguide_lock, WAKE_LOCK_SUSPEND, "rxrfs");

	pr_info("%s: End\n", __func__);
	return smdrfs;

err_smd_alloc_urb:
	free_buf(&smdrfs->read_buf);
err_alloc_buf:
	kfree(smdrfs->tpbuf);
err_kmalloc_tpbuf:
	dereg_smdrfs_dev(smdrfs);
err_register_smdrfs_dev:
	kfree(smdrfs);
	return NULL;
}

void *connect_smdrfs(void *smd_device, struct str_hsic *hsic)
{
	struct str_smdrfs *smdrfs = smd_device;

	smdrfs->hsic.intf = hsic->intf;
	smdrfs->hsic.usb = hsic->usb;
	smdrfs->hsic.rx_pipe = hsic->rx_pipe;
	smdrfs->hsic.tx_pipe = hsic->tx_pipe;
	smdrfs->hsic.txq = hsic->txq;

	smd_kill_urb(&smdrfs->hsic);

	smdrfs->pm_stat = RFS_PM_STATUS_RESUME;

	return smd_device;
}

void disconnect_smdrfs(void *smd_device)
{
	struct str_smdrfs *smdrfs = smd_device;

	smdrfs->pm_stat = RFS_PM_STATUS_DISCONNECT;

	flush_txurb(smdrfs->hsic.txq);
	smd_kill_urb(&smdrfs->hsic);
	flush_buf(&smdrfs->read_buf);
	smdrfs->frame_cnt = 0;
	smdrfs->suspended = 0;
	wake_unlock(&smdrfs->wakelock);
	wake_unlock(&smdrfs->rxguide_lock);

	smdrfs->hsic.usb = NULL;
}

void exit_smdrfs(void *param)
{
	struct str_smdrfs *smdrfs = param;

	pr_info("%s: Enter\n", __func__);

	wake_lock_destroy(&smdrfs->wakelock);
	wake_lock_destroy(&smdrfs->rxguide_lock);

	smd_free_urb(&smdrfs->hsic);
	free_buf(&smdrfs->read_buf);
	kfree(smdrfs->tpbuf);
	dereg_smdrfs_dev(smdrfs);
	kfree(smdrfs);

	return;
}

int smdrfs_suspend(struct str_smdrfs *smdrfs)
{
	if (smdrfs->pm_stat == RFS_PM_STATUS_RESUME) {
		smdrfs->pm_stat = RFS_PM_STATUS_SUSPEND;
		smd_kill_urb(&smdrfs->hsic);
	}
	return 0;
}

int smdrfs_resume(struct str_smdrfs *smdrfs)
{
	int r;
	int retrycnt;

	if (smdrfs->pm_stat != RFS_PM_STATUS_SUSPEND)
		return 0;

	for (retrycnt = 0; retrycnt < 50; retrycnt++) {
		smdrfs->pm_stat = RFS_PM_STATUS_RESUME;
		r = smdrfs_rx_usb(smdrfs);
		if (!r)
			return 0;
		usleep_range(1000, 10000);
	}
	pr_err("%s:smdrfs_rx_usb() failed : %d\n", __func__, r);
	return r;
}
