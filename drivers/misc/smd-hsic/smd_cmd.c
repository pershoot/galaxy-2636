/*
 * driver/misc/smd-hsic/smd_cmd.c
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

#include "smd_cmd.h"
#include "smd_core.h"
#include "smd_raw.h"
#include "../smdctl/smd_ctl.h"

#define CMD_COMP_REQ		1
#define WRITE_TIME_OUT		(HZ*5)

#define CMD_TEMP_BUFFER_SIZE	4096

#define CMD_RX_URB_CNT		1

#undef DEBUG_LOG

/* define if flow control only control by driver and modem */
#define FLOW_CTL_DRV_ONLY

static void cmd_rx_comp(struct urb *urb);
static void cmd_tx_comp(struct urb *urb);

static int smdcmd_rx_usb(struct str_smdcmd *smdcmd)
{
	int ret;
	char *buf = smdcmd->tpbuf;
	struct str_hsic *hsic = &smdcmd->hsic;

	usb_fill_bulk_urb(hsic->rx_urb[0].urb, hsic->usb, hsic->rx_pipe,
			  buf, CMD_TEMP_BUFFER_SIZE, cmd_rx_comp,
			  (void *)smdcmd);

	hsic->rx_urb[0].urb->transfer_flags = 0;

	ret = usb_submit_urb(hsic->rx_urb[0].urb, GFP_ATOMIC);
	if (ret < 0)
		pr_err("%s:usb_submit_urb() failed:(%d)\n", __func__, ret);

	return ret;
}

static void process_cmd(unsigned short cmd)
{
	struct str_smdraw *smdraw = get_smd_device(RAW_DEV_ID);
	struct net_device *netdev;
	int i;

	if (!smdraw)
		return;

	switch(cmd) {
	case SMDCMD_FLOWCTL_SUSPEND:
		if (!smdraw->tx_flow_control) {
			pr_info("%s:control to suspend\n", __func__);
			smdraw->tx_flow_control = true;
			for (i = 0; i < MAX_PDP_DEV; i++) {
				netdev = smdraw->pdpdev[i].pdp;
				if (netdev)
					netif_stop_queue(netdev);
			}
		}
		break;
	case SMDCMD_FLOWCTL_RESUME:
		if (smdraw->tx_flow_control) {
			pr_info("%s:control to resume\n", __func__);
			smdraw->tx_flow_control = false;
			for (i = 0; i < MAX_PDP_DEV; i++) {
				netdev = smdraw->pdpdev[i].pdp;
				if (netdev)
					netif_wake_queue(netdev);
			}
			queue_delayed_work(smdraw->pdp_wq, &smdraw->pdp_work, 0);
		}
		break;
	}
}

static void cmd_rx_comp(struct urb *urb)
{
#ifndef FLOW_CTL_DRV_ONLY
	int ret;
#endif
	int status;
	int cmd_offset = 0;
	struct str_smdcmd *smdcmd;

	pr_debug("%s: Enter read size:%d\n", __func__, urb->actual_length);

	smdcmd = urb->context;
	status = urb->status;

	if (smdcmd->pm_stat == CMD_PM_STATUS_SUSPEND)
		return;

/* do not check open count when flow control is done by driver only */
#ifndef FLOW_CTL_DRV_ONLY
	if (!atomic_read(&smdcmd->opened)) {
		pr_err("%s Not Initialised\n", __func__);
		return;
	}
#endif

	usb_mark_last_busy(smdcmd->hsic.usb);

	switch (status) {
	case -ENOENT:
	case 0:
		if (urb->actual_length) {
			while (cmd_offset < urb->actual_length) {
				process_cmd(*((unsigned short *)
						(smdcmd->tpbuf + cmd_offset)));
				cmd_offset += sizeof(unsigned short);
			}

#ifndef FLOW_CTL_DRV_ONLY
			ret = memcpy_to_ringbuf(&smdcmd->read_buf,
						smdcmd->tpbuf,
						urb->actual_length);
			if (ret < 0)
				pr_err("%s:memcpy_to_ringbuf failed :%d\n",
				       __func__, ret);
			wake_up(&smdcmd->poll_wait);
			wake_lock_timeout(&smdcmd->rxguide_lock, HZ/2);
#endif

#ifdef DEBUG_LOG
			pr_info("=============== RECEIVE CMD ==============\n");
			dump_buffer(smdcmd->tpbuf, urb->actual_length);
			pr_info("==========================================\n");
#endif
		}
		if (!urb->status)
			goto resubmit;
		break;

	case -ECONNRESET:
	case -ESHUTDOWN:
		pr_err("%s:LINK ERROR:%d\n", __func__, status);
		break;

	case -EOVERFLOW:
		smdcmd->stat.rx_over_err++;
		break;

	case -EILSEQ:
		smdcmd->stat.rx_crc_err++;
		break;

	case -ETIMEDOUT:
	case -EPROTO:
		smdcmd->stat.rx_pro_err++;
		break;

	}
	smdcmd->stat.rx_err++;

resubmit:
	smdcmd_rx_usb(smdcmd);
	return;
}

static void cmd_tx_comp(struct urb *urb)
{
	pr_debug("%s: Enter write size:%d\n", __func__, urb->actual_length);

	if (urb->status != 0)
		pr_warn("%s:Wrong Status:%d\n", __func__, urb->status);

	kfree(urb->transfer_buffer);
	usb_free_urb(urb);

	return;
}

static int smdcmd_open(struct inode *inode, struct file *file)
{
	int r;
	struct cdev *cdev = inode->i_cdev;
	struct str_hsic *hsic;
	struct str_smdcmd *smdcmd;

	pr_info("%s: Enter\n", __func__);
	smdcmd = container_of(cdev, struct str_smdcmd, cdev);
	hsic = &smdcmd->hsic;
	if (atomic_cmpxchg(&smdcmd->opened, 0, 1)) {
		pr_err("%s : already opened..\n", __func__);
		return -EBUSY;
	}

	r = smdcmd_rx_usb(smdcmd);
	if (r) {
		pr_err("%s:smdcmd_rx_usb() failed\n (%d)\n", __func__, r);
		atomic_set(&smdcmd->opened, 0);
		return r;
	}
	init_err_stat(&smdcmd->stat);

	file->private_data = smdcmd;
	return 0;
}

static int smdcmd_release(struct inode *inode, struct file *file)
{
	struct str_smdcmd *smdcmd = file->private_data;
	pr_info("%s: Enter\n", __func__);

	smd_kill_urb(&smdcmd->hsic);
	atomic_set(&smdcmd->opened, 0);

	return 0;
}

static ssize_t smdcmd_write(struct file *file, const char __user * buf,
			    size_t count, loff_t *ppos)
{
	int r;
	char *drvbuf;
	struct str_smdcmd *smdcmd;
	struct str_hsic *hsic;
	struct urb *urb;

	pr_debug("%s: Enter write size`:%d\n", __func__, count);

	smdcmd = file->private_data;
	hsic = &smdcmd->hsic;

	if (!smdcmd || smdcmd->pm_stat == CMD_PM_STATUS_DISCONNECT)
		return -ENODEV;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		pr_err("%s: tx urb is NULL\n", __func__);
		return -ENODEV;
	}

	drvbuf = kzalloc(count, GFP_KERNEL);
	if (!drvbuf) {
		pr_err("%s:kmalloc for drvbuf failed\n", __func__);
		r = -EAGAIN;
		goto exit;
	}

	if (copy_from_user(drvbuf, buf, count) != count) {
		pr_err("%s:copy from user to drvbuf failed\n", __func__);
		r = -EINVAL;
		goto exit;
	}

#ifdef DEBUG_LOG
	pr_info("================= TRANSMIT CMD =================\n");
	dump_buffer(drvbuf, count);
	pr_info("================================================\n");
#endif

	r = smdhsic_pm_resume_AP();
	if (r < 0) {
		pr_err("%s: HSIC Resume Failed %d\n", __func__, r);
		goto exit;
	}

	usb_fill_bulk_urb(urb, hsic->usb, hsic->tx_pipe,
			  drvbuf, count, cmd_tx_comp, smdcmd);

	urb->transfer_flags = URB_ZERO_PACKET;

	usb_mark_last_busy(hsic->usb);
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

static ssize_t smdcmd_read(struct file *file, char *buf, size_t count,
			   loff_t *f_pos)
{
	int ret;
	int pktsize;
	struct str_smdcmd *smdcmd;
	struct str_ring_buf *readbuf;

	pr_debug("%s: Enter\n", __func__);

	smdcmd = (struct str_smdcmd *)file->private_data;
	readbuf = &smdcmd->read_buf;

	if (!readbuf->buf)
		return -EFAULT;

	pktsize = get_remained_size(readbuf);
	if (!pktsize)
		goto done;

	ret = memcpy_from_ringbuf_user(readbuf, buf, pktsize);
	if (!ret) {
		pr_warn("%s:read cmd data failed: %d\n", __func__, ret);
		return -1;
	}
#ifdef DEBUG_LOG
	pr_info("=================== READ CMD ===================\n");
	dump_buffer(buf, pktsize);
	pr_info("================================================\n");
#endif

	if (ret < 0)
		return ret;

done:
	pr_debug("%s##: pktsize: %d\n", __func__, pktsize);

	return pktsize;
}

static unsigned int smdcmd_poll(struct file *file,
				struct poll_table_struct *wait)
{
	struct str_smdcmd *smdcmd = file->private_data;

	if (get_remained_size(&smdcmd->read_buf))
		return POLLIN | POLLRDNORM;

	if (wait)
		poll_wait(file, &smdcmd->poll_wait, wait);
	else
		usleep_range(2000, 10000);

	if (get_remained_size(&smdcmd->read_buf))
		return POLLIN | POLLRDNORM;
	else
		return 0;
}

static const struct file_operations smdcmd_fops = {
	.owner = THIS_MODULE,
	.open = smdcmd_open,
	.release = smdcmd_release,
	.write = smdcmd_write,
	.read = smdcmd_read,
	.poll = smdcmd_poll,
};

static int register_smdcmd_dev(struct str_smdcmd *smdcmd)
{
	int r;
	dev_t devid;

	if (!smdcmd) {
		pr_err("%s: smdcmd is NULL\n", __func__);
		return -EINVAL;
	}

	smdcmd->class = class_create(THIS_MODULE, SMDCMD_DEVNAME);
	if (IS_ERR_OR_NULL(smdcmd->class)) {
		r = PTR_ERR(smdcmd->class);
		smdcmd->class = NULL;
		pr_err("%s: class_create() failed, r = %d\n", __func__, r);
		return r;
	}

	r = alloc_chrdev_region(&devid, SMDCMD_MINOR, 1, SMDCMD_DEVNAME);
	if (r) {
		pr_err("%s: alloc_chrdev_region() failed, r = %d\n",
			__func__, r);
		goto err_alloc_chrdev_region;
	}

	cdev_init(&smdcmd->cdev, &smdcmd_fops);

	r = cdev_add(&smdcmd->cdev, devid, 1);
	if (r) {
		pr_err("%s: cdev_add() failed, r = %d\n", __func__, r);
		goto err_cdev_add;
	}
	smdcmd->devid = devid;

	smdcmd->dev = device_create(smdcmd->class, NULL, smdcmd->devid,
				NULL, SMDCMD_DEVNAME);
	if (IS_ERR_OR_NULL(smdcmd->dev)) {
		r = PTR_ERR(smdcmd->dev);
		smdcmd->dev = NULL;
		pr_err("%s: device_create() failed, r = %d\n", __func__, r);
		goto err_device_create;
	}

	atomic_set(&smdcmd->opened, 0);
	init_waitqueue_head(&smdcmd->poll_wait);
	dev_set_drvdata(smdcmd->dev, smdcmd);

	return 0;

err_device_create:
	cdev_del(&smdcmd->cdev);
err_cdev_add:
	unregister_chrdev_region(devid, 1);
err_alloc_chrdev_region:
	class_destroy(smdcmd->class);
	return r;
}

static void dereg_smdcmd_dev(struct str_smdcmd *smdcmd)
{
	dev_set_drvdata(smdcmd->dev, NULL);
	device_destroy(smdcmd->class, smdcmd->devid);
	cdev_del(&smdcmd->cdev);
	unregister_chrdev_region(smdcmd->devid, 1);
	class_destroy(smdcmd->class);
}

void *init_smdcmd(void)
{
	int r;
	struct str_smdcmd *smdcmd;

	pr_info("%s: Enter\n", __func__);
	smdcmd = kzalloc(sizeof(*smdcmd), GFP_KERNEL);
	if (!smdcmd) {
		pr_err("%s:malloc for smdcmd failed\n", __func__);
		return NULL;
	}

	r = register_smdcmd_dev(smdcmd);
	if (r) {
		pr_err("%s:register_smdcmd_dev() failed (%d)\n", __func__, r);
		goto err_register_smdcmd_dev;
	}

	smdcmd->tpbuf = kzalloc(CMD_TEMP_BUFFER_SIZE, GFP_DMA);
	if (!smdcmd->tpbuf) {
		pr_err("%s:malloc cmd tp buf failed\n", __func__);
		r = -ENOMEM;
		goto err_kzalloc_tpbuf;
	}

	r = alloc_buf(&smdcmd->read_buf, CMD_READ_BUF_SIZE);
	if (r) {
		pr_err("%s:alloc_buf() failed\n", __func__);
		goto err_alloc_buf;
	}

	smdcmd->hsic.rx_urbcnt = CMD_RX_URB_CNT;

	r = smd_alloc_urb(&smdcmd->hsic);
	if (r) {
		pr_err("%s:smd_alloc_urb() failed\n", __func__);
		goto err_smd_alloc_urb;
	}

	smdcmd->pm_stat = CMD_PM_STATUS_SUSPEND;

	wake_lock_init(&smdcmd->wakelock, WAKE_LOCK_SUSPEND, "smdcmd");
	wake_lock_init(&smdcmd->rxguide_lock, WAKE_LOCK_SUSPEND, "rxcmd");

	pr_info("%s: End\n", __func__);
	return smdcmd;

err_smd_alloc_urb:
	free_buf(&smdcmd->read_buf);
err_alloc_buf:
	kfree(smdcmd->tpbuf);
err_kzalloc_tpbuf:
	dereg_smdcmd_dev(smdcmd);
err_register_smdcmd_dev:
	kfree(smdcmd);
	return NULL;
}

void *connect_smdcmd(void *smd_device, struct str_hsic *hsic)
{
	struct str_smdcmd *smdcmd = smd_device;

	smdcmd->hsic.intf = hsic->intf;
	smdcmd->hsic.usb = hsic->usb;
	smdcmd->hsic.rx_pipe = hsic->rx_pipe;
	smdcmd->hsic.tx_pipe = hsic->tx_pipe;

	smdcmd->pm_stat = CMD_PM_STATUS_RESUME;

	init_err_stat(&smdcmd->stat);

	smd_kill_urb(&smdcmd->hsic);
	smdcmd_rx_usb(smdcmd);

	return smd_device;
}

void disconnect_smdcmd(void *smd_device)
{
	struct str_smdcmd *smdcmd = smd_device;

	pr_info("%s: Enter\n", __func__);

	smdcmd->pm_stat = CMD_PM_STATUS_DISCONNECT;
	smdcmd->suspended = 0;

	flush_buf(&smdcmd->read_buf);
	smd_kill_urb(&smdcmd->hsic);
	wake_unlock(&smdcmd->wakelock);
	wake_unlock(&smdcmd->rxguide_lock);

	smdcmd->hsic.usb = NULL;
}

void exit_smdcmd(void *param)
{
	struct str_smdcmd *smdcmd = param;

	pr_info("%s: Enter\n", __func__);

	wake_lock_destroy(&smdcmd->wakelock);
	wake_lock_destroy(&smdcmd->rxguide_lock);

	smd_free_urb(&smdcmd->hsic);
	free_buf(&smdcmd->read_buf);
	kfree(smdcmd->tpbuf);
	dereg_smdcmd_dev(smdcmd);
	kfree(smdcmd);
}

int smdcmd_suspend(struct str_smdcmd *smdcmd)
{
	pr_debug("%s: Enter\n", __func__);
	if (smdcmd->pm_stat == CMD_PM_STATUS_RESUME) {
		smdcmd->pm_stat = CMD_PM_STATUS_SUSPEND;
		smd_kill_urb(&smdcmd->hsic);
	}
	return 0;
}

int smdcmd_resume(struct str_smdcmd *smdcmd)
{
	int r = 0;
	int retrycnt;
	pr_debug("%s: Enter\n", __func__);

	if (smdcmd->pm_stat != CMD_PM_STATUS_SUSPEND)
		return 0;

	for (retrycnt = 0; retrycnt < 50; retrycnt++) {
		smdcmd->pm_stat = CMD_PM_STATUS_RESUME;
		r = smdcmd_rx_usb(smdcmd);
		if (!r)
			return 0;
		usleep_range(1000, 10000);
	}
	pr_err("%s:smdcmd_rx_usb() failed : %d\n", __func__, r);
	return r;
}
