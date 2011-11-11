/*
 * driver/misc/smd-hsic/smd_hsic.c
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

#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/suspend.h>

#include "smd_core.h"
#include "smd_ipc.h"
#include "smd_raw.h"
#include "smd_cmd.h"
#include "smd_down.h"
#include "../smdctl/smd_ctl.h"

static emu_fn emu_reg_func[MAX_DEV_ID] = {
	init_smdipc,
	init_smdraw,
	init_smdrfs,
	init_smdcmd,
	init_smd_down,
};

static con_fn emu_con_func[MAX_DEV_ID] = {
	connect_smdipc,
	connect_smdraw,
	connect_smdrfs,
	connect_smdcmd,
	connect_smd_down,
};

static discon_fn emu_discon_func[MAX_DEV_ID] = {
	disconnect_smdipc,
	disconnect_smdraw,
	disconnect_smdrfs,
	disconnect_smdcmd,
	disconnect_smd_down,
};

static demu_fn emu_dereg_func[MAX_DEV_ID] = {
	exit_smdipc,
	exit_smdraw,
	exit_smdrfs,
	exit_smdcmd,
	exit_smd_down,
};

struct smd_usbdev {
	struct usb_device *usbdev;
	unsigned int suspended;
	struct str_hsic *hsic;
	void *smd_device[MAX_DEV_ID];
	struct wake_lock tx_wlock;
	struct wake_lock boot_wlock;
};

static struct smd_usbdev g_usbdev;

void *get_smd_device(unsigned int id)
{
	if (g_usbdev.usbdev)
		return g_usbdev.smd_device[id];
	else
		return NULL;
}

static inline struct usb_driver *get_usb_driver(struct usb_interface *intf)
{
	return to_usb_driver(intf->dev.driver);
}

static int fill_usb_pipe(struct str_hsic *hsic, struct usb_device *usbdev,
		  const struct usb_host_interface *desc)
{
	if ((usb_pipein(desc->endpoint[0].desc.bEndpointAddress)) &&
	    (usb_pipeout(desc->endpoint[1].desc.bEndpointAddress))) {
		hsic->rx_pipe =
		    usb_rcvbulkpipe(usbdev,
				    desc->endpoint[0].desc.bEndpointAddress);
		hsic->tx_pipe =
		    usb_sndbulkpipe(usbdev,
				    desc->endpoint[1].desc.bEndpointAddress);

		pr_debug("1tx end addr : %d, rx end addr : %d\n",
		       desc->endpoint[0].desc.bEndpointAddress,
		       desc->endpoint[1].desc.bEndpointAddress);
	} else if ((usb_pipeout(desc->endpoint[0].desc.bEndpointAddress)) &&
		   (usb_pipein(desc->endpoint[1].desc.bEndpointAddress))) {
		hsic->rx_pipe =
		    usb_rcvbulkpipe(usbdev,
				    desc->endpoint[1].desc.bEndpointAddress);
		hsic->tx_pipe =
		    usb_sndbulkpipe(usbdev,
				    desc->endpoint[0].desc.bEndpointAddress);

		pr_debug("2tx end addr : %d, rx end addr : %d\n",
		       desc->endpoint[1].desc.bEndpointAddress,
		       desc->endpoint[0].desc.bEndpointAddress);
	} else {
		pr_err("%s:undefined endpoint\n", __func__);
		return -EINVAL;
	}

	return 0;
}

struct str_intf_priv *smd_create_dev(struct usb_interface *intf,
				     struct usb_device *usbdev,
				     const struct usb_host_interface *desc,
				     int devid)
{
	struct str_intf_priv *intfpriv = NULL;
	struct str_hsic *hsic = NULL;
	void *res;

	if (devid >= MAX_DEV_ID) {
		pr_err("%s Devid:%d Cannot Support\n", __func__,
		       devid);
		return NULL;
	}

	intfpriv = kzalloc(sizeof(*intfpriv), GFP_KERNEL);
	if (!intfpriv) {
		pr_err("%s:malloc for intfpriv failed\n", __func__);
		return NULL;
	}

	hsic = kzalloc(sizeof(*hsic), GFP_KERNEL);
	if (!hsic) {
		pr_err("%s:malloc for hsic failed\n", __func__);
		goto err_kzalloc_hsic;
	}

	hsic->intf = intf;
	hsic->usb = usb_get_dev(usbdev);

	if (fill_usb_pipe(hsic, usbdev, desc)) {
		pr_err("%s:fill_usb_pipe() failed\n", __func__);
		goto err_fill_usb_pipe;
	}

	if (emu_con_func[devid]) {
		res = emu_con_func[devid](g_usbdev.smd_device[devid], hsic);
		if (!res) {
			pr_err("%s:emufunc for devid: %d register failed\n",
			       __func__, devid);
			goto err_connect;
		}
		intfpriv->data = res;
		intfpriv->devid = devid;
		kfree(hsic);
	} else {
		intfpriv->data = hsic;
		intfpriv->devid = devid | ID_PRI;
	}
	return intfpriv;

err_connect:
err_fill_usb_pipe:
	kfree(hsic);
err_kzalloc_hsic:
	kfree(intfpriv);
	return NULL;
}

static void smdhsic_pm_runtime_start(struct work_struct *work)
{
	if (g_usbdev.usbdev) {
		pr_info("%s(udev:0x%p)\n", __func__, g_usbdev.usbdev);
		pm_runtime_allow(&g_usbdev.usbdev->dev);
	}
	wake_unlock(&g_usbdev.boot_wlock);
}

static int smdhsic_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	int devid = -1;
	int err;
	const struct usb_cdc_union_desc *union_header = NULL;
	const struct usb_host_interface *data_desc;
	struct usb_interface *data_intf;
	struct usb_device *usbdev;
	struct str_intf_priv *intfpriv = NULL;
	struct usb_driver *driver;
	struct str_smdipc *smdipc;
	struct str_hsic *hsic;
	u8 *data;
	int len;

	pr_info("%s: Enter\n", __func__);

	usbdev = interface_to_usbdev(intf);
	g_usbdev.usbdev = usbdev;
	driver = get_usb_driver(intf);
	data = intf->altsetting->extra;
	len = intf->altsetting->extralen;

	if (!len) {
		if (intf->cur_altsetting->endpoint->extralen &&
		    intf->cur_altsetting->endpoint->extra) {
			pr_debug(
			       "%s: Seeking extra descriptors on endpoint\n",
			       __func__);
			len = intf->cur_altsetting->endpoint->extralen;
			data = intf->cur_altsetting->endpoint->extra;
		} else {
			pr_err(
			       "%s: Zero length descriptor reference\n",
			       __func__);
			return -EINVAL;
		}
	}

	if (!len) {
		pr_err("%s: Zero length descriptor reference\n",
		       __func__);
		return -EINVAL;
	}

	while (len > 0) {
		if (data[1] == USB_DT_CS_INTERFACE) {
			switch (data[2]) {
			case USB_CDC_UNION_TYPE:
				if (union_header)
					break;
				union_header =
				    (struct usb_cdc_union_desc *)data;
				break;
			default:
				break;
			}
		}
		data += data[0];
		len -= data[0];
	}

	if (!union_header) {
		pr_err("%s:USB CDC is not union type\n", __func__);
		return -EINVAL;
	}

	data_intf = usb_ifnum_to_if(usbdev, union_header->bSlaveInterface0);
	if (!data_intf) {
		pr_err("%s:data_inferface is NULL\n", __func__);
		return -ENODEV;
	}

	data_desc = data_intf->altsetting;
	if (!data_desc) {
		pr_err("%s:data_desc is NULL\n", __func__);
		return -ENODEV;
	}

	switch (id->driver_info) {
	case XMM6260_PSI_DOWN:
		pr_warn("%s:XMM6260_PSI_DOWN\n", __func__);
		wake_lock(&g_usbdev.boot_wlock);
		intfpriv = smd_create_dev(data_intf, usbdev,
					data_desc, DOWN_DEV_ID);
		break;
	case XMM6260_BIN_DOWN:
		intfpriv = smd_create_dev(data_intf, usbdev,
					data_desc, DOWN_DEV_ID);
		break;
	case XMM6260_CHANNEL:
		devid = intf->altsetting->desc.bInterfaceNumber / 2;
		intfpriv = smd_create_dev(data_intf, usbdev, data_desc, devid);
		break;
	default:
		pr_err("%s: Undefined driver_info: %lu\n",
			__func__, id->driver_info);
		break;
	}

	if (!intfpriv) {
		pr_err("%s:smd_create_dev() failed\n", __func__);
		return -EINVAL;
	}

	err = usb_driver_claim_interface(driver, data_intf, intfpriv);
	if (err < 0) {
		pr_err("%s:usb_driver_claim() failed\n", __func__);
		return err;
	}

	/* to start runtime pm with AP initiated L2 */
	if (usb_runtime_pm_ap_initiated_L2) {
		usbdev->autosuspend_delay = msecs_to_jiffies(200);
		if (devid == FMT_DEV_ID) {
			smdipc = (struct str_smdipc *)intfpriv->data;
			hsic = &smdipc->hsic;
			g_usbdev.hsic = hsic;
			g_usbdev.hsic->dpm_suspending = false;
			g_usbdev.suspended = 0;
			INIT_DELAYED_WORK(&hsic->pm_runtime_work,
					smdhsic_pm_runtime_start);
			schedule_delayed_work(&hsic->pm_runtime_work,
					msecs_to_jiffies(10000));
		}
	} else
		usbdev->autosuspend_delay = 0;

	intfpriv->devid |= ID_BIND;
	usb_set_intfdata(intf, intfpriv);
	pm_suspend_ignore_children(&usbdev->dev, true);

	return 0;
}

static struct usb_interface *get_usb_intf(struct str_intf_priv *intfpriv)
{
	unsigned int devid = intfpriv->devid;
	struct str_smdipc *smdipc;
	struct str_smd_down *smddown;
	struct str_smdrfs *smdrfs;
	struct str_smdcmd *smdcmd;
	struct str_smdraw *smdraw;

	if (devid & ID_PRI) {
		struct str_hsic *hsic = intfpriv->data;
		return hsic->intf;
	}

	switch (GET_DEVID(devid)) {
	case FMT_DEV_ID:
		smdipc = (struct str_smdipc *)intfpriv->data;
		return smdipc->hsic.intf;
	case RAW_DEV_ID:
		smdraw = (struct str_smdraw *)intfpriv->data;
		return smdraw->hsic.intf;
	case RFS_DEV_ID:
		smdrfs = (struct str_smdrfs *)intfpriv->data;
		return smdrfs->hsic.intf;
	case CMD_DEV_ID:
		smdcmd = (struct str_smdcmd *)intfpriv->data;
		return smdcmd->hsic.intf;
	case DOWN_DEV_ID:
		smddown = (struct str_smd_down *)intfpriv->data;
		return smddown->hsic.intf;
	default:
		pr_err("%s:Undefined DEVID: %d\n", __func__,
		       GET_DEVID(devid));
	}

	return NULL;
}

static struct usb_device *get_usb_device(struct str_intf_priv *intfpriv)
{
	unsigned int devid = intfpriv->devid;
	struct str_smdipc *smdipc;
	struct str_smd_down *smddown;
	struct str_smdrfs *smdrfs;
	struct str_smdcmd *smdcmd;
	struct str_smdraw *smdraw;

	if (devid & ID_PRI) {
		struct str_hsic *hsic = intfpriv->data;
		return hsic->usb;
	}

	switch (GET_DEVID(devid)) {
	case FMT_DEV_ID:
		smdipc = (struct str_smdipc *)intfpriv->data;
		return smdipc->hsic.usb;
	case RAW_DEV_ID:
		smdraw = (struct str_smdraw *)intfpriv->data;
		return smdraw->hsic.usb;
	case RFS_DEV_ID:
		smdrfs = (struct str_smdrfs *)intfpriv->data;
		return smdrfs->hsic.usb;
	case CMD_DEV_ID:
		smdcmd = (struct str_smdcmd *)intfpriv->data;
		return smdcmd->hsic.usb;
	case DOWN_DEV_ID:
		smddown = (struct str_smd_down *)intfpriv->data;
		return smddown->hsic.usb;
	default:
		pr_err("%s:Undefined DEVID: %d\n", __func__,
		       GET_DEVID(devid));
	}
	return NULL;
}

static void smdhsic_disconnect(struct usb_interface *intf)
{
	int devid;
	struct usb_interface *smd_intf;
	struct str_intf_priv *intfpriv;
	struct usb_device *device = NULL;

	pr_info("%s: Called\n", __func__);

	intfpriv = usb_get_intfdata(intf);
	if (!intfpriv) {
		pr_err("%s: intfpriv is NULL\n", __func__);
		goto err_get_intfdata;
	}
	device = get_usb_device(intfpriv);
	devid = GET_DEVID(intfpriv->devid);
	pr_debug("%s : devid : %d\n", __func__, devid);

	smd_intf = get_usb_intf(intfpriv);
	if (!smd_intf) {
		pr_err("smd_intf is NULL\n");
		goto err_get_usb_intf;
	}

	if (smd_intf != intf) {
		pr_err("smd_intf is not same intf\n");
		goto err_mismatched_intf;
	}

	usb_driver_release_interface(get_usb_driver(intf), smd_intf);

	if (!device)
		usb_put_dev(device);

	switch (devid) {
	case FMT_DEV_ID:
		pm_runtime_disable(&device->dev);
		if (g_usbdev.hsic)
			cancel_delayed_work(&g_usbdev.hsic->pm_runtime_work);

		smdctl_request_connection_recover(true);
	case RAW_DEV_ID:
	case RFS_DEV_ID:
	case CMD_DEV_ID:
	case DOWN_DEV_ID:
		if (emu_discon_func[devid])
			emu_discon_func[devid](g_usbdev.smd_device[devid]);
		else
			kfree(intfpriv->data);
		break;
	default:
		pr_warn("%s:Undefined Callback Function\n",
		       __func__);
	}

	/* Power on/off kernel-panic workaround,
	 * if USB suspend cmd was queued in power.work before disconnect,
	 * reset the runtime PM request value to PM_REQ_NONE
	 */
	device->dev.power.request = RPM_REQ_NONE;

	kfree(intfpriv);
	usb_set_intfdata(intf, NULL);
	g_usbdev.usbdev = NULL;
	g_usbdev.suspended = 0;
	g_usbdev.hsic = NULL;
	return;

err_mismatched_intf:
err_get_usb_intf:
	if (device)
		usb_put_dev(device);
err_get_intfdata:
	pr_err("release(2) : %p\n", intf);
	usb_driver_release_interface(get_usb_driver(intf), intf);
	return;
}

#ifdef CONFIG_PM
static int smdhsic_suspend(struct usb_interface *interface,
			   pm_message_t message)
{
	int r = 0;
	unsigned int devid;
	struct str_smdipc *smdipc;
	struct str_smdrfs *smdrfs;
	struct str_smdcmd *smdcmd;
	struct str_smdraw *smdraw;
	struct str_intf_priv *intfpriv;

	intfpriv = usb_get_intfdata(interface);
	devid = GET_DEVID(intfpriv->devid);

	switch (devid) {
	case FMT_DEV_ID:
		smdipc = intfpriv->data;
		if (smdipc->suspended)
			return 0;
		r = smdipc_suspend(smdipc);
		pr_debug("SMDHSIC SUS FMT_DEV(%d)\n", g_usbdev.suspended);
		wake_unlock(&smdipc->wakelock);
		smdipc->suspended = 1;
		g_usbdev.suspended++;
		smdctl_set_pm_status(PM_STATUS_L2);
		break;
	case RAW_DEV_ID:
		smdraw = intfpriv->data;
		if (smdraw->suspended)
			return 0;
		r = smdraw_suspend(smdraw);
		pr_debug("SMDHSIC SUS RAW_DEV(%d)\n", g_usbdev.suspended);
		wake_unlock(&smdraw->wakelock);
		smdraw->suspended = 1;
		g_usbdev.suspended++;
		break;
	case RFS_DEV_ID:
		smdrfs = intfpriv->data;
		if (smdrfs->suspended)
			return 0;
		r = smdrfs_suspend(smdrfs);
		pr_debug("SMDHSIC SUS RFS_DEV(%d)\n", g_usbdev.suspended);
		wake_unlock(&smdrfs->wakelock);
		smdrfs->suspended = 1;
		g_usbdev.suspended++;
		break;
	case CMD_DEV_ID:
		smdcmd = intfpriv->data;
		if (smdcmd->suspended)
			return 0;
		r = smdcmd_suspend(smdcmd);
		pr_debug("SMDHSIC SUS CMD_DEV(%d)\n", g_usbdev.suspended);
		wake_unlock(&smdcmd->wakelock);
		smdcmd->suspended = 1;
		g_usbdev.suspended++;
	default:
		/* fall through */
		break;
	}

	return r;
}

static int smdhsic_resume(struct usb_interface *interface)
{
	int r = 0;
	unsigned int devid;
	struct str_smdipc *smdipc;
	struct str_smdrfs *smdrfs;
	struct str_smdcmd *smdcmd;
	struct str_smdraw *smdraw;
	struct str_intf_priv *intfpriv;

	intfpriv = usb_get_intfdata(interface);
	devid = GET_DEVID(intfpriv->devid);

	switch (devid) {
	case FMT_DEV_ID:
		smdipc = intfpriv->data;
		if (!smdipc->suspended)
			return 0;
		wake_lock(&smdipc->wakelock);
		g_usbdev.suspended--;
		smdipc->suspended = 0;
		r = smdipc_resume(smdipc);
		pr_debug("SMDHSIC RES FMT_DEV(%d)\n", g_usbdev.suspended);
		break;
	case RAW_DEV_ID:
		smdraw = intfpriv->data;
		if (!smdraw->suspended)
			return 0;
		wake_lock(&smdraw->wakelock);
		smdraw->suspended = 0;
		g_usbdev.suspended--;
		r = smdraw_resume(smdraw);
		pr_debug("SMDHSIC RES RAW_DEV(%d)\n", g_usbdev.suspended);
		break;
	case RFS_DEV_ID:
		smdrfs = intfpriv->data;
		if (!smdrfs->suspended)
			return 0;
		wake_lock(&smdrfs->wakelock);
		smdrfs->suspended = 0;
		g_usbdev.suspended--;
		r = smdrfs_resume(smdrfs);
		pr_debug("SMDHSIC RES RFS_DEV(%d)\n", g_usbdev.suspended);
		break;
	case CMD_DEV_ID:
		smdcmd = intfpriv->data;
		if (!smdcmd->suspended)
			return 0;
		wake_lock(&smdcmd->wakelock);
		smdcmd->suspended = 0;
		g_usbdev.suspended--;
		r = smdcmd_resume(smdcmd);
		pr_debug("SMDHSIC RES CMD_DEV(%d)\n", g_usbdev.suspended);
		smdctl_set_pm_status(PM_STATUS_L0);
	default:
		break;
	}
	return r;
}
#endif

int smdhsic_pm_suspend(void)
{
	int r = 0;
	struct device *dev;

	pr_debug("%s\n", __func__);

	if (!g_usbdev.usbdev) {
		pr_err("%s(NOT INIT DEVICE)\n", __func__);
		return -EFAULT;
	}

	if (g_usbdev.suspended) {
		pr_info("[%d] %s\n", __LINE__, __func__);
		return 0;
	}

	dev = &g_usbdev.usbdev->dev;

	if (usb_runtime_pm_ap_initiated_L2) {
		pm_runtime_suspend(dev);
		return 0;
	}

	pr_debug("%s(%d)\n", __func__, dev->power.runtime_status);
	pr_debug("%s(pwr.usg_cnt:%d)\n", __func__,
		atomic_read(&dev->power.usage_count));
	if (atomic_read(&dev->power.usage_count))
		r = pm_runtime_put_sync(dev);
	pr_debug("%s done %d \t %d\n",
		__func__, r, dev->power.usage_count.counter);
	return r;
}
EXPORT_SYMBOL_GPL(smdhsic_pm_suspend);

int smdhsic_pm_resume(void)
{
	int r = 0;
	int spin = 20;
	struct device *dev;

	pr_debug("%s\n", __func__);

	if (!g_usbdev.usbdev) {
		smdctl_request_connection_recover(true);
		return -EFAULT;
	}

	if (g_usbdev.hsic && g_usbdev.hsic->dpm_suspending) {
		pr_debug("%s : dpm is suspending just return\n", __func__);
		return 0;
	}

	dev = &g_usbdev.usbdev->dev;

	if (usb_runtime_pm_ap_initiated_L2) {
wait_active:
		if (g_usbdev.hsic && g_usbdev.hsic->dpm_suspending) {
			pr_debug("%s : dpm is suspending just return\n",
				__func__);
			return 0;
		}
		switch (dev->power.runtime_status) {
		case RPM_SUSPENDED:
			r = pm_runtime_resume(dev);
			if (!r && dev->power.timer_expires == 0
			 		&& dev->power.request_pending == false) {
	                			pr_err("%s:run time idle\n", __func__);
					pm_runtime_idle(dev);
			} else if (r < 0) {
				pr_err("%s : pm_runtime_resume failed : %d\n", __func__, r);
				smdctl_request_connection_recover(true);
				return r;
			}
			msleep(20);
			goto wait_active;
			break;
		case RPM_SUSPENDING:
		case RPM_RESUMING:
			if (spin-- < 0) {
				if (g_usbdev.hsic &&
					g_usbdev.hsic->resume_failcnt++ > 5) {
					g_usbdev.hsic->resume_failcnt = 0;
					smdctl_request_connection_recover(true);
				}
				return -ETIMEDOUT;
			}
			msleep(20);
			goto wait_active;
		case RPM_ACTIVE:
			if (g_usbdev.hsic)
				g_usbdev.hsic->resume_failcnt = 0;
			break;
		default:
			break;
		}
		return 0;
	}

	pr_debug("%s (%d)\n", __func__, dev->power.runtime_status);

	pr_debug("%s(pwr.usg_cnt:%d)\n",
		__func__, atomic_read(&dev->power.usage_count));
	if (!(atomic_read(&dev->power.usage_count)))
		r = pm_runtime_get_sync(dev);

	pr_debug("%s done %d \t %d\n",
		__func__, r, dev->power.usage_count.counter);
	return r;
}
EXPORT_SYMBOL_GPL(smdhsic_pm_resume);

int smdhsic_pm_resume_AP(void)
{
	int r;
	int expire = 500;
	int pending_spin = 20;
	int suspended_spin = 20;
	struct completion done;
	struct device *dev;

	if (!g_usbdev.usbdev) {
		smdctl_request_connection_recover(true);
		return -ENODEV;
	}

	dev = &g_usbdev.usbdev->dev;

retry:
	/* Hold Wake lock, when TX...*/
	wake_lock_timeout(&g_usbdev.tx_wlock, msecs_to_jiffies(500));
	/* dpm_suspending can be set during RPM STATUS changing */
	if (g_usbdev.hsic && g_usbdev.hsic->dpm_suspending)
		return -EAGAIN;

	switch (dev->power.runtime_status) {
	case RPM_SUSPENDED:
		pr_debug("%s: HSIC suspended\n", __func__);
		init_completion(&done);
		r = smdctl_request_slave_wakeup(&done);
		if (r <= 0 &&
			!wait_for_completion_timeout(&done,
						msecs_to_jiffies(expire))) {
			pr_err("%s: HSIC Resume timeout %d\n",
			       __func__, expire);
			r = smdctl_request_slave_wakeup(NULL);
			if (r <= 0) {
				if (g_usbdev.hsic &&
					g_usbdev.hsic->resume_failcnt++ > 5) {
					g_usbdev.hsic->resume_failcnt = 0;
					smdctl_request_connection_recover(true);
				}
				return -ETIMEDOUT;
			}
		}

		if (suspended_spin-- <= 0) {
			if (g_usbdev.hsic &&
				g_usbdev.hsic->resume_failcnt++ > 5) {
				g_usbdev.hsic->resume_failcnt = 0;
				smdctl_request_connection_recover(true);
			}
			smdctl_request_slave_wakeup(NULL);
			return -ETIMEDOUT;
		}
		smdctl_request_slave_wakeup(NULL);
		msleep(100);
		goto retry;
	case RPM_SUSPENDING:
	case RPM_RESUMING:
		pr_debug("%s: HSIC status : %d spin: %d\n", __func__,
			dev->power.runtime_status,
			pending_spin);
		if (pending_spin == 0) {
			pr_err("%s: Modem runtime pm timeout\n",
			       __func__);
			if (g_usbdev.hsic &&
				g_usbdev.hsic->resume_failcnt++ > 5) {
				g_usbdev.hsic->resume_failcnt = 0;
				smdctl_request_connection_recover(true);
			}
			smdctl_reenumeration_control();
			return -ETIMEDOUT;
		}
		pending_spin--;
		usleep_range(5000, 10000);
		goto retry;
	case RPM_ACTIVE:
		if (g_usbdev.hsic)
			g_usbdev.hsic->resume_failcnt = 0;

		/* For under autosuspend timer */
		wake_lock_timeout(&g_usbdev.tx_wlock, msecs_to_jiffies(100));
		break;
	default:
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(smdhsic_pm_resume_AP);

bool smdhsic_pm_active(void)
{
	struct device *dev;

	if (!g_usbdev.usbdev)
		return false;

	dev = &g_usbdev.usbdev->dev;
	return (dev->power.runtime_status == RPM_ACTIVE);
}
EXPORT_SYMBOL_GPL(smdhsic_pm_active);

int smdhsic_reset_resume(struct usb_interface *intf)
{
	unsigned int devid;
	struct str_intf_priv *intfpriv;
	struct device *dev = &g_usbdev.usbdev->dev;

	pr_debug("%s: Called\n", __func__);

	if (!g_usbdev.suspended)
		return 0;

	intfpriv = usb_get_intfdata(intf);
	devid = GET_DEVID(intfpriv->devid);

	if (!usb_runtime_pm_ap_initiated_L2 &&
		atomic_read(&dev->power.usage_count) == 1)
		pm_runtime_get_noresume(dev);

	smdhsic_resume(intf);

	if (devid == CMD_DEV_ID) {
		g_usbdev.hsic->dpm_suspending = false;
		pr_debug("%s : dpm suspending set to false\n", __func__);
	}

	return 0;
}

static struct usb_device_id smdhsic_ids[] = {
	{USB_DEVICE(BOOT_VEN_ID, BOOT_PRO_ID),
	 .driver_info = XMM6260_PSI_DOWN,},
	{USB_DEVICE(PSI_VEN_ID, PSI_PRO_ID),
	 .driver_info = XMM6260_BIN_DOWN,},
	{USB_DEVICE(MAIN_VEN_ID, MAIN_PRO_ID),
	 .driver_info = XMM6260_CHANNEL},
	{}
};

static int smdhsic_notifier_event(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	struct str_hsic *hsic  = g_usbdev.hsic;

#ifdef CONFIG_SAMSUNG_LPM_MODE
	extern int lpm_mode_flag;

	/* In case of LPM state, HSIC device should not be created */
	if (lpm_mode_flag)
		return NOTIFY_DONE;
#endif

	if (!hsic)
		return NOTIFY_DONE;

	switch (event) {
	case PM_SUSPEND_PREPARE:
		hsic->dpm_suspending = true;
		pr_debug("%s : dpm suspending set to true\n", __func__);
		return NOTIFY_OK;
	case PM_POST_SUSPEND:
		hsic->dpm_suspending = false;
		pr_debug("%s : dpm suspending set to false\n", __func__);
		return NOTIFY_OK;
	}
	return NOTIFY_DONE;
}

static struct notifier_block smdhsic_pm_notifier = {
	.notifier_call = smdhsic_notifier_event,
};

static struct usb_driver smdhsic_driver = {
	.name = "cdc_smd",
	.probe = smdhsic_probe,
	.disconnect = smdhsic_disconnect,
#ifdef CONFIG_PM
	.suspend = smdhsic_suspend,
	.resume = smdhsic_resume,
	.reset_resume = smdhsic_reset_resume,
#endif
	.id_table = smdhsic_ids,
	.supports_autosuspend = 1,
};

static int __init smd_hsic_init(void)
{
	int i;
	for (i = 0 ; i < MAX_DEV_ID ; i++)
		if (emu_reg_func[i])
			g_usbdev.smd_device[i] = emu_reg_func[i]();

	wake_lock_init(&g_usbdev.tx_wlock, WAKE_LOCK_SUSPEND, "smd_txlock");
	wake_lock_init(&g_usbdev.boot_wlock, WAKE_LOCK_SUSPEND, "smd_bootlock");

	register_pm_notifier(&smdhsic_pm_notifier);
	return usb_register(&smdhsic_driver);
}

static void __exit smd_hsic_exit(void)
{
	int i;
	for (i = 0 ; i < MAX_DEV_ID ; i++)
		if (emu_dereg_func[i])
			emu_dereg_func[i](g_usbdev.smd_device[i]);
	usb_deregister(&smdhsic_driver);
	wake_lock_destroy(&g_usbdev.tx_wlock);
	wake_lock_destroy(&g_usbdev.boot_wlock);
}

module_init(smd_hsic_init);
module_exit(smd_hsic_exit);

MODULE_DESCRIPTION("SAMSUNG MODEM DRIVER for HSIC");
MODULE_AUTHOR("Minwoo KIM <minwoo7945.kim@samsung.com>");
MODULE_LICENSE("GPL");
