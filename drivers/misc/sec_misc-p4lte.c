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
#include <mach/gpio-sec.h>
//#include <mach/gpio-p3.h>
#include <mach/gpio.h>
#include "sec_misc.h"
#include <linux/interrupt.h>
#include <linux/irq.h>

static struct wake_lock sec_misc_wake_lock;
static struct wake_lock	wake_lock_usb_modem;

static int use_jig_irq = 0;

unsigned char emmc_checksum_done;
unsigned char emmc_checksum_pass;

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

int check_jig_on(void);

void p3_usb_path_init(void)
{
    if(system_rev > 0x0A){
	gpio_request(GPIO_USB_SEL1, "GPIO_USB_SEL1");
	gpio_direction_output(GPIO_USB_SEL1, 0);
        tegra_gpio_enable(GPIO_USB_SEL1);
    }
    else{
    	gpio_request(GPIO_USB_SEL1_REV05, "GPIO_USB_SEL1");
	gpio_direction_output(GPIO_USB_SEL1_REV05, 0);
        tegra_gpio_enable(GPIO_USB_SEL1_REV05);
    }
        
    gpio_request(GPIO_USB_SEL2, "GPIO_USB_SEL2");
    gpio_direction_output(GPIO_USB_SEL2, 0);
    tegra_gpio_enable(GPIO_USB_SEL2);
}

void p3_set_usb_path(usb_path_type usb_path)
{
    if(usb_path == USB_SEL_AP_USB){
        if(system_rev > 0x0A)
            gpio_set_value(GPIO_USB_SEL1, 1);
        else
            gpio_set_value(GPIO_USB_SEL1_REV05, 1);
        //gpio_set_value(GPIO_USB_SEL2, 0);		//EUR
        gpio_set_value(GPIO_USB_SEL2, 1);
        usb_sel_status = USB_SEL_AP_USB;
    }
    else if(usb_path == USB_SEL_CP_USB){
        if(system_rev > 0x0A)
            gpio_set_value(GPIO_USB_SEL1, 0);
        else
            gpio_set_value(GPIO_USB_SEL1_REV05, 0);
        
        gpio_set_value(GPIO_USB_SEL2, 0);
        usb_sel_status = USB_SEL_CP_USB;
    }
    else if(usb_path == USB_SEL_ADC){
        if(system_rev > 0x0A)
            gpio_set_value(GPIO_USB_SEL1, 0);
        else
            gpio_set_value(GPIO_USB_SEL1_REV05, 0);
        
        gpio_set_value(GPIO_USB_SEL2, 1);
        usb_sel_status = USB_SEL_ADC;
    }
}

void p3_uart_path_init(void){
#if 0
	gpio_request(GPIO_UART_SEL, "GPIO_UART_SEL");

	gpio_direction_output(GPIO_UART_SEL, 1);
#if defined(CONFIG_SAMSUNG_KEEP_CONSOLE)
	gpio_set_value(GPIO_UART_SEL, 1);	// Set UART path to AP
#else
	gpio_set_value(GPIO_UART_SEL, 0);	// Set UART path to CP
#endif
	tegra_gpio_enable(GPIO_UART_SEL);
#else
	gpio_request(GPIO_UART_SEL, "GPIO_UART_SEL");
	if ( gpio_request(GPIO_UART_SEL2, "GPIO_UART_SEL2") < 0)
		printk(KERN_ALERT "GPIO_UART_SEL2 Request Failed *************************************************\n");

	//gpio_direction_output(GPIO_UART_SEL, 1);
	//gpio_direction_output(GPIO_UART_SEL2, 1);
	//gpio_set_value(GPIO_UART_SEL, 1); // Set UART path to AP
	//gpio_set_value(GPIO_UART_SEL, 0); // Set UART path to CP
	gpio_set_value(GPIO_UART_SEL2, 1); // Set UART_SEL2 always initiate 1
	tegra_gpio_enable(GPIO_UART_SEL);
	tegra_gpio_enable(GPIO_UART_SEL2);
#endif
}

static ssize_t uart_sel_show(struct device *dev, struct device_attribute *attr, char *buf)
{

	ssize_t	ret;
	int PinValue = 5;
	int PinValue2 = 5;

	PinValue = gpio_get_value(GPIO_UART_SEL);
	PinValue2 = gpio_get_value(GPIO_UART_SEL2);



	if(PinValue == 0)
		ret = sprintf(buf, "UART path => MODEM (VIA)\n");
	else if(PinValue == 1 && PinValue2 == 0)
		ret = sprintf(buf, "UART path => LTEMODEM (CMC220)\n");
	else if(PinValue == 1 && PinValue2 == 1)
		ret = sprintf(buf, "UART path => PDA (T20)\n");
	else
		ret = sprintf(buf, "uart_sel_show\n");

	return ret;
}

#if 0			//EUR
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
#endif

#if defined(CONFIG_SEC_KEYBOARD_DOCK)
extern bool g_keyboard;
#endif
static ssize_t uart_sel_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t size)
{
	static int first_modem_set_uart = 1;
	
	//wake_lock(&sec_misc_wake_lock);

#if defined(CONFIG_SEC_KEYBOARD_DOCK)
	if (g_keyboard) {
		pr_err("%s - the keyboard is connected.\n", __func__);
		return size;
	}
#endif

	if((0 == strncmp(buf,"PDA", 3)) || (0 == strncmp(buf, "pda", 3)))
	{
		gpio_set_value(GPIO_UART_SEL, 1);	// Set UART path to AP
		gpio_set_value(GPIO_UART_SEL2, 1);	// Set UART path to AP
		klogi("Path Set To UART PDA (AP)\n");
	}
	else if((0 == strncmp(buf, "LTEMODEM", 8)) || (0 == strncmp(buf, "ltemodem", 8)))
	{
		gpio_set_value(GPIO_UART_SEL, 1);	// Set UART path to LTE
		gpio_set_value(GPIO_UART_SEL2, 0);	// Set UART path to LTE
		klogi("Path Set To UART LTE\n");
	}
	else if((0 == strncmp(buf, "MODEM", 5)) || (0 == strncmp(buf, "modem", 5)))
	{
	    if (first_modem_set_uart == 0) {        // first setting at the booting time , Modem arleady set, No need and Secure Othe UART control
			gpio_set_value(GPIO_UART_SEL, 0);	// Set UART path to CP
	    }
		first_modem_set_uart = 0;
		klogi("Path Set To UART MODEM(CP)\n");
	}
	else
		printk("Enter PDA(AP uart) or LTE(LTE uart) or MODEM(CP uart)...\n");

	//wake_unlock(&sec_misc_wake_lock);
	return size;
}

#if 0		//EUR
static ssize_t uart_sel_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t size)
{
	int state;

	if (sscanf(buf, "%i", &state) != 1 || (state < 0 || state > 1))
		return -EINVAL;

	// prevents the system from entering suspend 
	wake_lock(&sec_misc_wake_lock);

	if(state == 1)
	{
		printk("[denis]Set UART path to AP state : %d\n" ,state);
		gpio_set_value(GPIO_UART_SEL, 1);	// Set UART path to AP
		klogi("Set UART path to AP\n");
	}
	else if(state == 0)
	{
		printk("[denis]Set UART path to CP state : %d\n",state);
		gpio_set_value(GPIO_UART_SEL, 0);	// Set UART path to CP
		klogi("Set UART path to CP\n");
	}
	else
		klogi("Enter 1(AP uart) or 0(CP uart)...\n");
	
	wake_unlock(&sec_misc_wake_lock);
	return size;
}
#endif

static DEVICE_ATTR(uartsel, S_IRUGO | S_IWUGO, uart_sel_show, uart_sel_store);

//static DEVICE_ATTR(uartsel, S_IRUGO | S_IWUSR | S_IWGRP, uart_sel_show, uart_sel_store);		//EUR


static ssize_t usb_sel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
#if 0		//EUR
	ssize_t	ret;

	if(usb_sel_status == USB_SEL_ADC)
		ret = sprintf(buf, "USB path => ADC\n");
	else if(usb_sel_status == USB_SEL_AP_USB)
		ret = sprintf(buf, "USB path => PDA\n");
		//ret = sprintf(buf, "USB path => AP\n");
	else if (usb_sel_status==USB_SEL_CP_USB)
		ret = sprintf(buf, "USB path => MODEM\n");
		//ret = sprintf(buf, "USB path => CP\n");
	else
		ret = sprintf(buf, "usb_sel_show\n");

	return ret;
#else
	ssize_t ret;
	int PinValue = 5;
	int PinValue2 = 5;

        if(system_rev > 0x0A)
            PinValue = gpio_get_value(GPIO_USB_SEL1);
        else
            PinValue = gpio_get_value(GPIO_USB_SEL1_REV05);
        
	PinValue2 = gpio_get_value(GPIO_USB_SEL2);

	if(PinValue == 0 && PinValue2 == 0)
		ret = sprintf(buf, "USB path => MODEM (VIA)\n");
	else if(PinValue == 0 && PinValue2 == 1)
		ret = sprintf(buf, "USB path => LTEMODEM (CMC220)\n");
	else if(PinValue == 1)
		ret = sprintf(buf, "USB path => PDA\n");
	else
		ret = sprintf(buf, "usb_sel_show\n");

	return ret;
#endif
}

static ssize_t usb_sel_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t size)
{
	// prevents the system from entering suspend 
	//wake_lock(&sec_misc_wake_lock);

	if((0 == strncmp(buf, "LTEMODEM", 8)) || (0 == strncmp(buf, "ltemodem", 8)))	{
		//p3_set_usb_path(USB_SEL_ADC);	// Set USB path to CP
		if(system_rev > 0x0A)
                    gpio_set_value(GPIO_USB_SEL1, 0);
                else
                    gpio_set_value(GPIO_USB_SEL1_REV05, 0);
                
		gpio_set_value(GPIO_USB_SEL2, 1);
		usb_sel_status = USB_SEL_ADC;
		klogi("Set USB path to LTEMODEM(LTE)\n");

		if (use_jig_irq)
			wake_lock_timeout(&wake_lock_usb_modem, HZ / 2);
	}
	else if((0 == strncmp(buf, "PDA", 3)) || (0 == strncmp(buf, "pda", 3)))	{
		//p3_set_usb_path(USB_SEL_AP_USB);	// Set USB path to AP
		if(system_rev > 0x0A)
                    gpio_set_value(GPIO_USB_SEL1, 1);
                else
                    gpio_set_value(GPIO_USB_SEL1_REV05, 1);
                
		gpio_set_value(GPIO_USB_SEL2, 1);
		usb_sel_status = USB_SEL_AP_USB;
		klogi("Set USB path to PDA(AP)\n");

		if (use_jig_irq)
			wake_lock_timeout(&wake_lock_usb_modem, HZ / 2);
	}
	else if((0 == strncmp(buf, "MODEM", 5)) || (0 == strncmp(buf, "modem", 5)))	{
		//p3_set_usb_path(USB_SEL_CP_USB);	// Set USB path to CP
		if(system_rev > 0x0A)
                    gpio_set_value(GPIO_USB_SEL1, 0);
                else
                    gpio_set_value(GPIO_USB_SEL1_REV05, 0);
                
	        gpio_set_value(GPIO_USB_SEL2, 0);
		usb_sel_status = USB_SEL_CP_USB;
		klogi("Set USB path to MODEM(CP)\n");

		if (use_jig_irq == 1 && check_jig_on() == 1)
			wake_lock(&wake_lock_usb_modem);
	}
	else {
		klogi("Enter LTEMODEM(LTE usb) or PDA(AP usb) or  MODEM(CP usb)...\n");
		if (use_jig_irq)
			wake_lock_timeout(&wake_lock_usb_modem, HZ / 2);
	}

	//wake_unlock(&sec_misc_wake_lock);

	return size;
}


#if 0		//EUR
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
	
	wake_unlock(&sec_misc_wake_lock);

	return size;
}
#endif

static DEVICE_ATTR(usbsel, S_IRUGO | S_IWUGO, usb_sel_show, usb_sel_store);
//static DEVICE_ATTR(usbsel, S_IRUGO | S_IWUSR | S_IWGRP, usb_sel_show, usb_sel_store);		//EUR

int check_usb_status = CHARGER_BATTERY;		//EUR
#if 1		//EUR
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

static DEVICE_ATTR(usb_state, S_IRUGO | S_IWUSR, usb_state_show,    usb_state_store);
#endif

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

static DEVICE_ATTR(emmc_checksum_pass, 0664, emmc_checksum_pass_show, emmc_checksum_pass_store);

#if 0	//EUR
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
#else
int check_jig_on(void)
{
	u32 value;
	value = gpio_get_value(GPIO_IFCONSENSE);
	return (value)?0:1;
}

static irqreturn_t jig_irq_handler(int irq, void *arg)
{
	int PinValue = 5;
	int PinValue2 = 5;

        if (system_rev > 0x0A)
            PinValue = gpio_get_value(GPIO_USB_SEL1);
        else
            PinValue = gpio_get_value(GPIO_USB_SEL1_REV05);
        
	PinValue2 = gpio_get_value(GPIO_USB_SEL2);

	if ((check_jig_on() == 1) && (PinValue == 0 && PinValue2 == 0)) {
		/* USB path => MODEM (VIA), JIG inserted : wake lock */
		wake_lock(&wake_lock_usb_modem);
	}
	else
		wake_lock_timeout(&wake_lock_usb_modem, HZ / 2);

	return IRQ_HANDLED;
}

int init_jig_on(void)
{
	int jig_irq;
	jig_irq = gpio_to_irq(GPIO_IFCONSENSE);

	if (request_irq(jig_irq, jig_irq_handler,
		IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING,
		"JIG intr", NULL)) {
		pr_err("JIG interrupt handler register failed!\n");

		gpio_request(GPIO_IFCONSENSE, "GPIO_IFCONSENSE");
		gpio_direction_input(GPIO_IFCONSENSE);
		tegra_gpio_enable(GPIO_IFCONSENSE);

		return 0;
	}

	use_jig_irq = 1;
	wake_lock_init(&wake_lock_usb_modem, WAKE_LOCK_SUSPEND, "usb_modem_wake_lock");
}
#endif

//struct class *sec_switch_class;
//EXPORT_SYMBOL(sec_switch_class);
extern struct class *sec_class;
struct device *sec_misc_dev;

static int __init sec_misc_init(void)
{
	int ret=0;
	klogi("started!");
	printk("[denis]sec_misc_init!\n");

	ret = misc_register(&sec_misc_device);
	if (ret<0) {
		kloge("misc_register failed!");
		return ret;
	}

	//sec_switch_class = class_create(THIS_MODULE, "sec");
	//if (IS_ERR(sec_switch_class))
		//pr_err("Failed to create class named \"sec\"!\n");
	//sec_misc_dev = device_create(sec_class, NULL, 0, NULL, "switch");
	sec_misc_dev = device_create(sec_class, NULL, 0, NULL, "sec_misc");		//EUR
	if (IS_ERR(sec_misc_dev)) {
		kloge("failed to create device!");
		return -1;
	}

	if (device_create_file(sec_misc_dev, &dev_attr_uartsel) < 0) {
		kloge("failed to create device file!(%s)!\n", dev_attr_uartsel.attr.name);
		return -1;
	}

	if (device_create_file(sec_misc_dev, &dev_attr_usbsel) < 0) {
		kloge("failed to create device file!(%s)!\n", dev_attr_usbsel.attr.name);
		return -1;
	}

	if (device_create_file(sec_misc_dev, &dev_attr_usb_state) < 0)		//EUR
		pr_err("Failed to create device file(%s)!\n", dev_attr_usb_state.attr.name);		//EUR
	
	if (device_create_file(sec_misc_dev, &dev_attr_emmc_checksum_done) < 0)
		pr_err("failed to create device file - %s\n", dev_attr_emmc_checksum_done.attr.name);

	if (device_create_file(sec_misc_dev, &dev_attr_emmc_checksum_pass) < 0)
		pr_err("failed to create device file - %s\n", dev_attr_emmc_checksum_pass.attr.name);

	wake_lock_init(&sec_misc_wake_lock, WAKE_LOCK_SUSPEND, "sec_misc");

	p3_uart_path_init();
	p3_usb_path_init();
	p3_set_usb_path(USB_SEL_AP_USB);
	init_jig_on();
	
	return 0;
}

static void __exit sec_misc_exit(void)
{
	wake_lock_destroy(&sec_misc_wake_lock);
	
	device_remove_file(sec_misc_dev, &dev_attr_uartsel);
	device_remove_file(sec_misc_dev, &dev_attr_usbsel);

	//device_destroy(sec_switch_class,0);

}

module_init(sec_misc_init);
module_exit(sec_misc_exit);

/* Module information */
MODULE_AUTHOR("Samsung");
MODULE_DESCRIPTION("Samsung P3 misc. driver");
MODULE_LICENSE("GPL");


