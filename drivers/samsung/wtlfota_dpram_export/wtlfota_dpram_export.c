/****************************************************************************
 **
 ** COPYRIGHT(C) : Samsung Electronics Co.Ltd, 2006-2010 ALL RIGHTS RESERVED
 **
 ** AUTHOR       : Song Wei  			@LDK@
 **                WTLFOTA_DPRAM Device Driver for Via6410
 **			Reference: Via6419 DPRAM driver (dpram.c/.h)
 ****************************************************************************/
#ifndef _HSDPA_WTLFOTA_DPRAM
#define _HSDPA_WTLFOTA_DPRAM
#endif

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/irq.h>
#include <linux/poll.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <asm/irq.h>
#include <mach/hardware.h>

#include <mach/hardware.h>
#include <mach/gpio.h>
#include <mach/gpio-sec.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/wakelock.h>
#include <linux/kernel_sec_common.h>

#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/time.h>
#include <linux/if_arp.h>

void tegra_gpio_enable_GPIO_VIA_PS_HOLD_OFF(void)
{
  tegra_gpio_enable(GPIO_VIA_PS_HOLD_OFF);
}

EXPORT_SYMBOL(tegra_gpio_enable_GPIO_VIA_PS_HOLD_OFF);


void tegra_gpio_enable_GPIO_DP_INT_AP(void)
{
  tegra_gpio_enable(GPIO_DP_INT_AP);
}

EXPORT_SYMBOL(tegra_gpio_enable_GPIO_DP_INT_AP);


void tegra_gpio_enable_GPIO_PHONE_ACTIVE(void)
{
  tegra_gpio_enable(GPIO_PHONE_ACTIVE);
}

EXPORT_SYMBOL(tegra_gpio_enable_GPIO_PHONE_ACTIVE);


/* init & cleanup. */
static int __init wtlfota_dpram_export_init(void)
{
  printk("wtlfota_dpram_export_init\n");
  return 0;
}

static void __exit wtlfota_dpram_export_exit(void)
{
  printk("wtlfota_dpram_export_exit\n");
}

module_init(wtlfota_dpram_export_init);
module_exit(wtlfota_dpram_export_exit);

MODULE_AUTHOR("SAMSUNG ELECTRONICS CO., LTD");

MODULE_DESCRIPTION("export symbols needed by WTLFOTA_DPRAM Device");
