/*
 * driver/misc/sec_misc.c
 *
 * driver supporting miscellaneous functions for Samsung P3 device
 *
 * COPYRIGHT(C) Samsung Electronics Co., Ltd. 2006-2010 All Right Reserved.  
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
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/wakelock.h>
#include <linux/blkdev.h>
#include <linux/kernel_sec_common.h>
#include <mach/gpio-sec.h>
#include <mach/gpio.h>

#include "sec_misc.h"

static struct wake_lock sec_misc_wake_lock;

static struct file_operations sec_misc_fops =
{
	.owner = THIS_MODULE,
	//.read = sec_misc_read,
	//.ioctl = sec_misc_ioctl,
	//.open = sec_misc_open,
	//.release = sec_misc_release,
};

static struct miscdevice sec_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "sec_misc",
	.fops = &sec_misc_fops,
};

usb_path_type usb_sel_status = USB_SEL_AP_USB;

unsigned char emmc_checksum_done;
unsigned char emmc_checksum_pass;

void p3_set_usb_path(usb_path_type usb_path);

void p3_usb_path_init(void){
	int usbsel1, usbsel2;

	gpio_request(GPIO_USB_SEL1, "GPIO_USB_SEL1");
	gpio_direction_output(GPIO_USB_SEL1, 0);
	tegra_gpio_enable(GPIO_USB_SEL1);

	gpio_request(GPIO_USB_SEL2, "GPIO_USB_SEL2");
	gpio_direction_input(GPIO_USB_SEL2);
	usbsel2 = gpio_get_value(GPIO_USB_SEL2);
	gpio_direction_output(GPIO_USB_SEL2, 0);
	tegra_gpio_enable(GPIO_USB_SEL2);

	if (usbsel2 == 1) {
		p3_set_usb_path(USB_SEL_AP_USB);
	} else if (usbsel2 == 0) {
		p3_set_usb_path(USB_SEL_CP_USB);
	}
}

void p3_set_usb_path(usb_path_type usb_path)
{
	if(usb_path == USB_SEL_AP_USB){
	      gpio_set_value(GPIO_USB_SEL1, 1);
	      gpio_set_value(GPIO_USB_SEL2, 1);
		usb_sel_status = USB_SEL_AP_USB;
		}
	else if(usb_path == USB_SEL_CP_USB){
	      gpio_set_value(GPIO_USB_SEL1, 0);
	      gpio_set_value(GPIO_USB_SEL2, 0);
		usb_sel_status = USB_SEL_CP_USB;
		}
	else if(usb_path == USB_SEL_ADC){
	      gpio_set_value(GPIO_USB_SEL1, 0);
	      gpio_set_value(GPIO_USB_SEL2, 1);
		usb_sel_status = USB_SEL_ADC;
	}
}

void p3_uart_path_init(void){
    int uartsel;

    gpio_request(GPIO_UART_SEL, "GPIO_UART_SEL");
    gpio_direction_input(GPIO_UART_SEL);
    uartsel = gpio_get_value(GPIO_UART_SEL);

    gpio_direction_output(GPIO_UART_SEL, uartsel);
    gpio_set_value(GPIO_UART_SEL, uartsel);
    /*
#if defined(CONFIG_SAMSUNG_KEEP_CONSOLE)
    gpio_set_value(GPIO_UART_SEL, 1);   // Set UART path to AP
#else
#if defined(CONFIG_MACH_SAMSUNG_P5WIFI)
    if(system_rev > 6)
        gpio_set_value(GPIO_UART_SEL, 0);   // Set UART path to CP
    else
        gpio_set_value(GPIO_UART_SEL, 1);   // Set UART path to AP for temp support testmode
#else
    gpio_set_value(GPIO_UART_SEL, 0);   // Set UART path to CP
#endif
#endif
*/
    tegra_gpio_enable(GPIO_UART_SEL);

}

static ssize_t uart_sel_show(struct device *dev, struct device_attribute *attr, char *buf)
{

	ssize_t	ret;
	int PinValue=5;

	PinValue = gpio_get_value(GPIO_UART_SEL);

	if(PinValue == 0)
		ret = sprintf(buf, "UART path => CP\n");
	else if(PinValue == 1)
		ret = sprintf(buf, "UART path => AP\n");
	else
		ret = sprintf(buf, "uart_sel_show\n");
	return ret;
}

#if defined(CONFIG_SEC_KEYBOARD_DOCK)
extern bool g_keyboard;
#endif
static ssize_t uart_sel_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t size)
{
	int state;

	if (sscanf(buf, "%i", &state) != 1 || (state < 0 || state > 1))
		return -EINVAL;

#if defined(CONFIG_SEC_KEYBOARD_DOCK)
	if (g_keyboard) {
		pr_err("%s - the keyboard is connected.\n", __func__);
		return size;
	}
#endif

	// prevents the system from entering suspend
	wake_lock(&sec_misc_wake_lock);

	if(state == 1)
	{
		printk("[denis]Set UART path to AP state : %d\n" ,state);
		gpio_set_value(GPIO_UART_SEL, 1);	// Set UART path to AP
		klogi("Set UART path to AP\n");
		sec_set_param(param_index_uartsel, &state);
	}
	else if(state == 0)
	{
		printk("[denis]Set UART path to CP state : %d\n",state);
		gpio_set_value(GPIO_UART_SEL, 0);	// Set UART path to CP
		klogi("Set UART path to CP\n");
		sec_set_param(param_index_uartsel, &state);
	}
	else
		klogi("Enter 1(AP uart) or 0(CP uart)...\n");
	
	wake_unlock(&sec_misc_wake_lock);
	return size;
}

static DEVICE_ATTR(uartsel, S_IRUGO | S_IWUSR | S_IWGRP,
		uart_sel_show, uart_sel_store);


static ssize_t usb_sel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t	ret;

	if(usb_sel_status == USB_SEL_ADC)
		ret = sprintf(buf, "USB path => ADC\n");
	else if(usb_sel_status == USB_SEL_AP_USB)
		ret = sprintf(buf, "USB path => PDA\n");
	else if (usb_sel_status==USB_SEL_CP_USB)
		ret = sprintf(buf, "USB path => MODEM\n");
	else
		ret = sprintf(buf, "usb_sel_show\n");

	return ret;

}

static ssize_t usb_sel_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t size)
{

	int state;

	if (sscanf(buf, "%i", &state) != 1 || (state < 0 || state > 2))
		return -EINVAL;

	// prevents the system from entering suspend 
	wake_lock(&sec_misc_wake_lock);

	if(state == 2)	{
		p3_set_usb_path(USB_SEL_ADC);	// Set USB path to CP
		klogi("Set USB path to ADC\n");
	}
	else if(state == 1)	{
		p3_set_usb_path(USB_SEL_AP_USB);	// Set USB path to AP
		klogi("Set USB path to AP\n");
	}
	else if(state == 0)	{
		p3_set_usb_path(USB_SEL_CP_USB);	// Set USB path to CP
		klogi("Set USB path to CP\n");
	}
	else
		klogi("Enter 2(ADC usb) or 1(AP usb) or 0(CP usb)...\n");

	if (state >= 0 && state <= 2)
		sec_set_param(param_index_usbsel, &state);
	
	wake_unlock(&sec_misc_wake_lock);

	return size;
}

static DEVICE_ATTR(usbsel, S_IRUGO | S_IWUSR | S_IWGRP,
		usb_sel_show, usb_sel_store);

int check_usb_status = CHARGER_BATTERY;
static ssize_t usb_state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", (check_usb_status == CHARGER_USB) ?
			"USB_STATE_CONFIGURED" : "USB_STATE_NOTCONFIGURED");
}

static ssize_t usb_state_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t size)
{
	return 0;
}

static DEVICE_ATTR(usb_state, S_IRUGO | S_IWUSR, usb_state_show, usb_state_store);

static ssize_t emmc_checksum_done_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", emmc_checksum_done);
}

static ssize_t emmc_checksum_done_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int state;

	if (sscanf(buf, "%i", &state) != 1 || (state < 0 || state > 1))
		return -EINVAL;

	emmc_checksum_done = (unsigned char)state;
	return size;
}
/* /sys/class/sec/sec_misc/emmc_checksum_done
 * This node should be created with 0644 to avoid CTS failure 
 */
static DEVICE_ATTR(emmc_checksum_done, 0664, emmc_checksum_done_show, emmc_checksum_done_store);

static ssize_t emmc_checksum_pass_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", emmc_checksum_pass);
}

static ssize_t emmc_checksum_pass_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	int state;

	if (sscanf(buf, "%i", &state) != 1 || (state < 0 || state > 1))
		return -EINVAL;

	emmc_checksum_pass = (unsigned char)state;
	return size;
}
/* /sys/class/sec/sec_misc/emmc_checksum_pass
 * This node should be created with 0644 to avoid CTS failure 
 */
static DEVICE_ATTR(emmc_checksum_pass, 0664, emmc_checksum_pass_show, emmc_checksum_pass_store);

int check_jig_on(void)
{
	u32 value = gpio_get_value(GPIO_IFCONSENSE);
	return !value;
}

static void init_jig_on(void)
{
	gpio_request(GPIO_IFCONSENSE, "GPIO_IFCONSENSE");
	gpio_direction_input(GPIO_IFCONSENSE);
	tegra_gpio_enable(GPIO_IFCONSENSE);
}

extern struct class *sec_class;
struct device *sec_misc_dev;

static int __init sec_misc_init(void)
{
	int ret=0;
	pr_info("%s\n", __func__);

	ret = misc_register(&sec_misc_device);
	if (ret<0) {
		pr_err("misc_register failed!\n");
		return ret;
	}

	sec_misc_dev = device_create(sec_class, NULL, 0, NULL, "sec_misc");
	if (IS_ERR(sec_misc_dev)) {
		pr_err("failed to create device!\n");
		return -ENODEV;
	}

	if (device_create_file(sec_misc_dev, &dev_attr_uartsel) < 0)
		pr_err("failed to create device file!(%s)!\n", dev_attr_uartsel.attr.name);

	if (device_create_file(sec_misc_dev, &dev_attr_usbsel) < 0)
		pr_err("failed to create device file!(%s)!\n", dev_attr_usbsel.attr.name);

	if (device_create_file(sec_misc_dev, &dev_attr_usb_state) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_usb_state.attr.name);
	
	if (device_create_file(sec_misc_dev, &dev_attr_emmc_checksum_done) < 0)
		pr_err("failed to create device file - %s\n", dev_attr_emmc_checksum_done.attr.name);

	if (device_create_file(sec_misc_dev, &dev_attr_emmc_checksum_pass) < 0)
		pr_err("failed to create device file - %s\n", dev_attr_emmc_checksum_pass.attr.name);

	wake_lock_init(&sec_misc_wake_lock, WAKE_LOCK_SUSPEND, "sec_misc");

	p3_uart_path_init();
	p3_usb_path_init();
	//p3_set_usb_path(USB_SEL_AP_USB);
	init_jig_on();
	
	return 0;
}

static void __exit sec_misc_exit(void)
{
	wake_lock_destroy(&sec_misc_wake_lock);
	
	device_remove_file(sec_misc_dev, &dev_attr_uartsel);
	device_remove_file(sec_misc_dev, &dev_attr_usbsel);
	device_remove_file(sec_misc_dev, &dev_attr_usb_state);
	device_remove_file(sec_misc_dev, &dev_attr_emmc_checksum_done);
	device_remove_file(sec_misc_dev, &dev_attr_emmc_checksum_pass);
}

module_init(sec_misc_init);
module_exit(sec_misc_exit);

/* Module information */
MODULE_AUTHOR("Samsung");
MODULE_DESCRIPTION("Samsung P3 misc. driver");
MODULE_LICENSE("GPL");

