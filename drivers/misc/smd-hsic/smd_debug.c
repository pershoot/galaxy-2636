/*
 * driver/misc/smd-hsic/smd_debug.c
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
#include <linux/platform_device.h>
#include <linux/gpio.h>

#include "smd_debug.h"
#include "smd_core.h"
#include "smd_ipc.h"

static ssize_t smdipc_readbuf_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int len;
	struct str_smd *smd;
	struct str_ring_buf *rbuf;

	smd = get_smd_addr(dev);
	if (!smd) {
		pr_err("%s:_get_smd_addr() failed\n", __func__);
		return 0;
	}

	rbuf = &smd->smdipc.read_buf;
	if (rbuf->buf == NULL)
		len = sprintf(buf, "Ring Buffer for IPC was not allocated\n");
	else
		len = sprintf(buf, "Remained Size : %d\n \
			   Vacant   Size : %d\n \
			   Buffer   Size : %d\n",
			get_remained_size(rbuf),
			get_vacant_size(rbuf),
			rbuf->size);

	return len;
}

static ssize_t smdipc_total_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int len;
	struct str_smd *smd;

	smd = get_smd_addr(dev);
	if (!smd) {
		pr_err("%s:_get_smd_addr() failed\n", __func__);
		return 0;
	}
	len = sprintf(buf, "Total Read Count : 0\n \
			   Total Write Count : 0\n");
	return len;
}

static struct device_attribute readbuf =
__ATTR(bufstat, S_IRUGO, smdipc_readbuf_show, NULL);

static struct device_attribute totalcount =
__ATTR(total, S_IRUGO, smdipc_total_show, NULL);

static struct attribute *smdipc_sysfs_attrs[] = {
	&readbuf.attr,
	&totalcount.attr,
	NULL,
};

static struct attribute_group smdipc_sysfs = {
	.name = "smdipc",
	.attrs = smdipc_sysfs_attrs,
};

int init_smdipc_debuf(struct kobject *kobj)
{
	int r;

	r = sysfs_create_group(kobj, &smdipc_sysfs);
	if (r)
		pr_err("%s:create sysfs for smd-ipc failed\n",
			   __func__);

	return r;
}

void exit_smdipcdebug(struct kobject *kobj)
{

}
