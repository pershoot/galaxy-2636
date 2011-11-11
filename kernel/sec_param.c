/*
 *  linux/kernel/sec_param.c
 *
 * Copyright (c) 2011 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/kernel_sec_common.h>

#define MISC_RD	0
#define MISC_WR	1

#define SEC_MISC_FILE_NAME	"/dev/block/mmcblk0p6"	/* MISC */
#define SEC_MISC_FILE_SIZE	0x200000		/* 2MB */
#define SEC_MISC_FILE_OFFSET	(0x200000 - 4096)	/* 2MB */

/* for indication of RECOVERY mode */
#define REBOOT_MODE_RECOVERY            4
int reboot_mode = NULL;

/* single global instance */
sec_param_data *param_data = NULL;

static bool misc_sec_operation(void *value, int offset, int size, int direction)
{
	printk("%s %x %x %d %d\n", __func__, value, offset, size, direction);
	/* Read from MSC(misc) partition  */
	struct file *filp;
	mm_segment_t fs;
	int ret = true;
	int flag = (direction == MISC_WR) ? (O_RDWR | O_SYNC) : O_RDONLY;

	filp = filp_open(SEC_MISC_FILE_NAME, flag, 0);

	if (IS_ERR(filp)) {
		printk("%s: filp_open failed. (%ld)\n", __func__, PTR_ERR(filp));
		return false;
	}

	fs = get_fs();
	set_fs(get_ds());

	ret = filp->f_op->llseek(filp, offset, SEEK_SET);
	if (ret < 0) {
		printk("%s FAIL LLSEEK \n", __func__);
		ret = false;
		goto misc_sec_debug_out;
	}

	if (direction == MISC_RD) {
		ret = filp->f_op->read(filp, (char __user *)value, size, &filp->f_pos);
	} else if (direction == MISC_WR) {
		ret = filp->f_op->write(filp, (char __user *)value, size, &filp->f_pos);
	}

misc_sec_debug_out:
	set_fs(fs);
	filp_close(filp, NULL);
	return ret;
}

bool sec_open_param(void)
{
	int ret = true;
	int offset = SEC_MISC_FILE_OFFSET;

	if (param_data != NULL)
		if (reboot_mode != REBOOT_MODE_RECOVERY)
			return true;

	param_data = kmalloc(sizeof(sec_param_data), GFP_KERNEL);
	
	ret = misc_sec_operation(param_data, offset, sizeof(sec_param_data), MISC_RD);

	if (!ret) {
		kfree(param_data);
		param_data = NULL;
		printk("%s PARAM OPEN FAIL\n", __func__);
		return false;
	}

#if 0
	printk("************* PARAM ***************\n");
	printk("0x%x\n", param_data->signature);
	printk("0x%x\n", param_data->size);
	printk("0x%x\n", param_data->oemlock);
	printk("0x%x\n", param_data->sud);
	printk("0x%x\n", param_data->secure);
	printk("0x%x\n", param_data->fusetrigger);
	printk("0x%x\n", param_data->debuglevel);
	printk("0x%x\n", param_data->uartsel);
	printk("0x%x\n", param_data->usbsel);
	printk("************* PARAM ***************\n");
#endif

	return ret;

}
EXPORT_SYMBOL(sec_open_param);

bool sec_get_param(sec_param_index index, void *value)
{
	int ret = true;
	ret = sec_open_param();
	if (!ret)
		return ret;
	
	switch (index) {
	case param_index_oemlock:
		memcpy(value, &(param_data->oemlock), sizeof(unsigned int));
		break;
	case param_index_sud:
		memcpy(value, &(param_data->sud), sizeof(unsigned int));
		break;
	case param_index_secure:
		memcpy(value, &(param_data->secure), sizeof(unsigned int));
		break;
	case param_index_fusetrigger:
		memcpy(value, &(param_data->fusetrigger), sizeof(unsigned int));
		break;
	case param_index_sbk:
		memcpy(value, &(param_data->sbk), sizeof(unsigned int));
		break;
#ifdef param_test
	case param_index_test:
		memcpy(value, &(param_data->test), sizeof(unsigned int));
		break;
#endif
	case param_index_debuglevel:
		memcpy(value, &(param_data->debuglevel), sizeof(unsigned int));
		break;
	case param_index_uartsel:
		memcpy(value, &(param_data->uartsel), sizeof(unsigned int));
		break;
	case param_index_usbsel:
		memcpy(value, &(param_data->usbsel), sizeof(unsigned int));
		break;
	default:
		return false;
	}

	return true;
}
EXPORT_SYMBOL(sec_get_param);

bool sec_set_param(sec_param_index index, void *value)
{
	int ret = true;
	int offset = SEC_MISC_FILE_OFFSET;

	ret = sec_open_param();
	if (!ret)
		return ret;
	
	switch (index) {
	case param_index_oemlock:
		memcpy(&(param_data->oemlock), value, sizeof(unsigned int));
		break;
	case param_index_sud:
		memcpy(&(param_data->sud), value, sizeof(unsigned int));
		break;
	case param_index_secure:
		memcpy(&(param_data->secure), value, sizeof(unsigned int));
		break;
	case param_index_fusetrigger:
		memcpy(&(param_data->fusetrigger), value, sizeof(unsigned int));
		break;
	case param_index_sbk:
		memcpy(&(param_data->sbk), value, sizeof(unsigned int));
		break;
#ifdef param_test
	case param_index_test:
		memcpy(&(param_data->test), value, sizeof(unsigned int));
		break;
#endif
	case param_index_debuglevel:
		memcpy(&(param_data->debuglevel), value, sizeof(unsigned int));
		break;
	case param_index_uartsel:
		memcpy(&(param_data->uartsel), value, sizeof(unsigned int));
		break;
	case param_index_usbsel:
		memcpy(&(param_data->usbsel), value, sizeof(unsigned int));
		break;
	default:
		return false;
	}

	return misc_sec_operation(param_data, offset, sizeof(sec_param_data), MISC_WR);
}
EXPORT_SYMBOL(sec_set_param);


