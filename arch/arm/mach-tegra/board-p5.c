/*
 * arch/arm/mach-tegra/board-p5.c
 *
 * Copyright (c) 2010, NVIDIA Corporation.
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/serial_8250.h>
#include <linux/i2c.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/i2c-tegra.h>
#include <linux/nct1008.h>
#include <linux/i2c-gpio.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/platform_data/tegra_usb.h>
#include <linux/usb/android_composite.h>
#include <linux/mfd/tps6586x.h>
#include <linux/regulator/consumer.h>
#include <linux/sec_jack.h>
#include <linux/atmel_mxt1386.h>
#if defined(CONFIG_TOUCHSCREEN_WACOM_G5)
#include <linux/wacom_i2c.h>
#endif
#include <linux/bcm4751-rfkill.h>
#include <linux/30pin_con.h>
#include <linux/mutex.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include <linux/syscalls.h>
#include <linux/vfs.h>
#include <linux/file.h>
#include <linux/memblock.h>
#include <linux/tegra_uart.h>
#include <linux/console.h>
#include <asm/segment.h>
#include <linux/uaccess.h>
#include <linux/spi/spi.h>

#include <mach/clk.h>
#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/pinmux.h>
#include <mach/iomap.h>
#include <mach/io.h>
#include <mach/i2s.h>
#include <mach/spdif.h>
#include <mach/audio.h>
#include <mach/kbc.h>
#include <linux/power/p3_battery.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/usb_phy.h>
#include <mach/tegra_das.h>

#include <mach/gpio-sec.h>

#include "gpio-names.h"
#include "board.h"
#include "clock.h"
#include "board-p5.h"
#include "devices.h"
#include "fuse.h"
#include "wakeups-t2.h"
#include <media/s5k5bbgx.h>
#include <media/s5k5ccgx.h>
#if defined(CONFIG_TOUCHSCREEN_MELFAS)
#include <linux/melfas_ts.h>
#endif
#if defined(CONFIG_SEC_KEYBOARD_DOCK)
#include <linux/sec_keyboard_struct.h>
#endif

#if defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
#include <media/tdmb_plat.h>
#endif

#ifdef CONFIG_USB_ANDROID_ACCESSORY
#include <linux/usb/f_accessory.h>
#endif

#ifdef CONFIG_SAMSUNG_LPM_MODE
#include <linux/moduleparam.h>
#endif

#ifdef CONFIG_KERNEL_DEBUG_SEC
#include <linux/kernel_sec_common.h>
#endif

struct class *sec_class;
EXPORT_SYMBOL(sec_class);

struct board_usb_data {
	struct mutex ldo_en_lock;
	int usb_regulator_on[3];
};

#ifdef CONFIG_SAMSUNG_LPM_MODE
int charging_mode_from_boot;

/* Get charging_mode status from kernel CMDLINE parameter. */
__module_param_call("", charging_mode,  &param_ops_int,
		&charging_mode_from_boot, 0, 0644);
MODULE_PARM_DESC(charging_mode_from_boot, "Charging mode parameter value.");
#endif

struct bootloader_message {
	char command[32];
	char status[32];
};

static struct clk *wifi_32k_clk;
#if defined(CONFIG_TOUCHSCREEN_WACOM_G5)
static struct wacom_g5_callbacks *wacom_callbacks;
#endif
/*static unsigned int *sec_batt_level;*/
static struct board_usb_data usb_data;

/*Check ADC value to select headset type*/
extern s16 adc_get_value(u8 channel);
extern s16 stmpe811_adc_get_value(u8 channel);
extern void p3_set_usb_path(usb_path_type usb_path);
extern usb_path_type usb_sel_status;
#if defined(CONFIG_SEC_MODEM)
extern int __init register_smd_resource (void);
#endif


/* REBOOT_MODE
 *
 * These defines must be kept in sync with the bootloader.
 */
#define REBOOT_MODE_NONE                0
#define REBOOT_MODE_DOWNLOAD            1
#define REBOOT_MODE_NORMAL              2
#define REBOOT_MODE_UPDATE              3
#define REBOOT_MODE_RECOVERY            4
#define REBOOT_MODE_FOTA                5
#define REBOOT_MODE_FASTBOOT            7
#define REBOOT_MODE_DOWNLOAD_FAILED     8
#define REBOOT_MODE_DOWNLOAD_SUCCESS    9

#define MISC_DEVICE "/dev/block/mmcblk0p6"

static int write_bootloader_message(char *cmd, int mode)
{
	struct file *filp;
	mm_segment_t oldfs;
	int ret = 0;
	loff_t pos = 2048L;  /* bootloader message offset in MISC.*/

	struct bootloader_message  bootmsg;

	memset(&bootmsg, 0, sizeof(struct bootloader_message));

	if (mode == REBOOT_MODE_RECOVERY) {
		strcpy(bootmsg.command, "boot-recovery");
#ifdef CONFIG_KERNEL_DEBUG_SEC
		kernel_sec_set_debug_level(KERNEL_SEC_DEBUG_LEVEL_LOW);
#endif
	}
	else if (mode == REBOOT_MODE_FASTBOOT)
		strcpy(bootmsg.command, "boot-fastboot");
	else if (mode == REBOOT_MODE_NORMAL)
		strcpy(bootmsg.command, "boot-reboot");
	else if (mode == REBOOT_MODE_FOTA)
		strcpy(bootmsg.command, "boot-fota");
	else if (mode == REBOOT_MODE_NONE)
		strcpy(bootmsg.command, "boot-normal");
	else
		strcpy(bootmsg.command, cmd);

	bootmsg.status[0] = (char) mode;


	filp = filp_open(MISC_DEVICE, O_WRONLY, 0);

	if (IS_ERR(filp)) {
		pr_info("failed to open MISC : '%s'.\n", MISC_DEVICE);
		return 0;
	}

	oldfs = get_fs();
	set_fs(KERNEL_DS);

	ret = vfs_write(filp, (const char *)&bootmsg,
			sizeof(struct bootloader_message), &pos);

	set_fs(oldfs);

	if (ret < 0)
		pr_info("failed to write on MISC\n");
	else
		pr_info("command : %s written on MISC\n", bootmsg.command);

	fput(filp);
	filp_close(filp, NULL);

	return ret;
}

/* Boot Mode Physical Addresses and Magic Token */
#define BOOT_MODE_P_ADDR	(0x20000000 - 0x0C)
#define BOOT_MAGIC_P_ADDR	(0x20000000 - 0x10)
#define BOOT_MAGIC_TOKEN	0x626F6F74

static void write_bootloader_mode(char boot_mode)
{
	void __iomem *to_io;
#if 0
	to_io = ioremap(BOOT_MODE_P_ADDR, 4);
	writel((unsigned long)boot_mode, to_io);
	iounmap(to_io);
#endif
	/* Write a magic value to a 2nd memory location to distinguish between a
	 * cold boot and a reboot.
	 */
	to_io = ioremap(BOOT_MAGIC_P_ADDR, 4);
	writel(BOOT_MAGIC_TOKEN, to_io);
	iounmap(to_io);
}

static int p3_notifier_call(struct notifier_block *this,
					unsigned long code, void *_cmd)
{
	int mode;
	u32 value;
	struct regulator *reg;

	value = gpio_get_value(GPIO_TA_nCONNECTED);

	if (code == SYS_RESTART) {
		reg = regulator_get(NULL, "vdd_ldo4");
		if (IS_ERR_OR_NULL(reg))
			pr_err("%s: couldn't get regulator vdd_ldo4\n", __func__);
		else {
			regulator_enable(reg);
			pr_debug("%s: enabling regulator vdd_ldo4\n", __func__);
			regulator_put(reg);
		}

		mode = REBOOT_MODE_NORMAL;
		if (_cmd) {
			if (!strcmp((char *)_cmd, "recovery"))
				mode = REBOOT_MODE_RECOVERY;
			else if (!strcmp((char *)_cmd, "bootloader"))
				mode = REBOOT_MODE_FASTBOOT;
			else if (!strcmp((char *)_cmd, "fota"))
				mode = REBOOT_MODE_FOTA;
			else if (!strcmp((char *)_cmd, "download"))
				mode = REBOOT_MODE_DOWNLOAD;
		}
	} else if (code == SYS_POWER_OFF && charging_mode_from_boot == true && !value)
		mode = REBOOT_MODE_NORMAL;
	else
		mode = REBOOT_MODE_NONE;

	pr_debug("%s, Reboot Mode : %d\n", __func__, mode);

	write_bootloader_mode(mode);

	write_bootloader_message(_cmd, mode);

	return NOTIFY_DONE;
}

static struct notifier_block p3_reboot_notifier = {
	.notifier_call = p3_notifier_call,
};

int p5_panic_notifier_call(void)
{
	struct regulator *reg;
	pr_debug("%s\n", __func__);
	
	reg = regulator_get(NULL, "vdd_ldo4");
	if (IS_ERR_OR_NULL(reg))
		pr_err("%s: couldn't get regulator vdd_ldo4\n", __func__);
	else {
		regulator_enable(reg);
		pr_debug("%s: enabling regulator vdd_ldo4\n", __func__);
		regulator_put(reg);
	}
	
	return NOTIFY_DONE;
}

static struct plat_serial8250_port debug_uart_platform_data[] = {
	{
		.membase	= IO_ADDRESS(TEGRA_UARTB_BASE),
		.mapbase	= TEGRA_UARTB_BASE,
		.irq		= INT_UARTB,
		.flags		= UPF_BOOT_AUTOCONF | UPF_FIXED_TYPE,
		.type		= PORT_TEGRA,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= 216000000,
	}, {
		.flags		= 0,
	}
};

static struct platform_device debug_uart = {
	.name = "serial8250",
	.id = PLAT8250_DEV_PLATFORM,
	.dev = {
		.platform_data = debug_uart_platform_data,
	},
};

static struct tegra_audio_platform_data tegra_spdif_pdata = {
	.dma_on = true,  /* use dma by default */
	.i2s_clk_rate = 5644800,
	.mode = SPDIF_BIT_MODE_MODE16BIT,
	.fifo_fmt = 0,
};


static struct tegra_utmip_config utmi_phy_config[] = {
	[0] = {
			.hssync_start_delay = 0,
			.idle_wait_delay = 17,
			.elastic_limit = 16,
			.term_range_adj = 6,
			.xcvr_setup = 15,
			.xcvr_lsfslew = 2,
			.xcvr_lsrslew = 2,
	},
	[1] = {
			.hssync_start_delay = 0,
			.idle_wait_delay = 17,
			.elastic_limit = 16,
			.term_range_adj = 6,
			.xcvr_setup = 8,
			.xcvr_lsfslew = 2,
			.xcvr_lsrslew = 2,
	},
};
static struct tegra_ulpi_config hsic_phy_config = {
	.reset_gpio = TEGRA_GPIO_PG2,
	.clk = "clk_dev2",
	.inf_type = TEGRA_USB_UHSIC,
};

static struct resource ram_console_resource[] = {
	{
		.flags = IORESOURCE_MEM,
	}
};

static struct platform_device ram_console_device = {
	.name = "ram_console",
	.id = -1,
	.num_resources = ARRAY_SIZE(ram_console_resource),
	.resource = ram_console_resource,
};

/* Bluetooth(BCM4330) rfkill */
static struct resource p3_bcm4330_rfkill_resources[] = {
	{
		.name   = "bcm4330_nreset_gpio",
		.start  = GPIO_BT_nRST,
		.end    = GPIO_BT_nRST,
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "bcm4330_nshutdown_gpio",
		.start  = GPIO_BT_EN,
		.end    = GPIO_BT_EN,
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "bcm4330_hostwake_gpio",
		.start  = TEGRA_GPIO_TO_IRQ(GPIO_BT_HOST_WAKE),
		.end    = TEGRA_GPIO_TO_IRQ(GPIO_BT_HOST_WAKE),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device p3_bcm4330_rfkill_device = {
	.name = "bcm4330_rfkill",
	.id             = -1,
	.num_resources  = ARRAY_SIZE(p3_bcm4330_rfkill_resources),
	.resource       = p3_bcm4330_rfkill_resources,
};

int __init p3_rfkill_init(void)
{
	/*Add Clock Resource*/
	clk_add_alias("bcm4330_32k_clk", p3_bcm4330_rfkill_device.name, \
				"blink", NULL);

	if (system_rev < 6)
		tegra_gpio_enable(TEGRA_GPIO_PN7);
	else
		tegra_gpio_enable(GPIO_BT_HOST_WAKE);
	if (system_rev < 6) {
		p3_bcm4330_rfkill_resources[2].start = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PN7);
		p3_bcm4330_rfkill_resources[2].end = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PN7);
	}
	platform_device_register(&p3_bcm4330_rfkill_device);

	return 0;
}

/* UART Interface for Bluetooth */

static struct tegra_uart_platform_data bt_uart_pdata = {
	.wake_peer = p3_bt_uart_wake_peer,
};

static struct resource tegra_uartc_resources[] = {
	[0] = {
		.start 	= TEGRA_UARTC_BASE,
		.end	= TEGRA_UARTC_BASE + TEGRA_UARTC_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= INT_UARTC,
		.end	= INT_UARTC,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device tegra_btuart_device = {
	.name	= "tegra_uart",
	.id	= 2,
	.num_resources	= ARRAY_SIZE(tegra_uartc_resources),
	.resource	= tegra_uartc_resources,
	.dev	= {
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data = &bt_uart_pdata,
	},
};

static __initdata struct tegra_clk_init_table p3_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "uartb",	"pll_p",	216000000,	true},
	{ "uartc",      "pll_m",        600000000,      false},
	{ "blink",      "clk_32k",      32768,          false},
	{ "pll_p_out4",	"pll_p",	24000000,	true },
	{ "pwm",	"pll_c",	560000000,	true},
	{ "pll_c",	"clk_m",	560000000,	true},
	{ "pll_a",	NULL,		11289600,	true},
	{ "pll_a_out0",	NULL,		11289600,	true},
	{ "i2s1",	"pll_a_out0",	11289600,	true},
	{ "i2s2",	"pll_a_out0",	11289600,	true},
	{ "audio",	"pll_a_out0",	11289600,	true},
	{ "audio_2x",	"audio",	22579200,	true},
	{ "spdif_out",	"pll_a_out0",	5644800,	false},
	{ "vde",	"pll_m",	240000000,	false},
	{ "sclk", NULL, 240000000,  true},
	{ "hclk", "sclk", 240000000,  true},
	{ NULL,		NULL,		0,		0},
};

#ifndef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
static char *usb_functions[] = { "mtp" };
static char *usb_functions_adb[] = { "mtp", "adb" };
static char *usb_functions_rndis[] = { "rndis" };
static char *usb_functions_rndis_adb[] = { "rndis", "adb" };
static char *usb_functions_all[] = { "rndis", "mtp", "adb"};


static struct android_usb_product usb_products[] = {
	{
		.product_id     = 0x685c,
		.num_functions  = ARRAY_SIZE(usb_functions),
		.functions      = usb_functions,
	},
	{
		.product_id     = 0x6860,
		.num_functions  = ARRAY_SIZE(usb_functions_adb),
		.functions      = usb_functions_adb,
	},
	{
		.product_id     = 0x6863,
		.num_functions  = ARRAY_SIZE(usb_functions_rndis),
		.functions      = usb_functions_rndis,
	},
	{
		.product_id     = 0x6864,
		.num_functions  = ARRAY_SIZE(usb_functions_rndis_adb),
		.functions      = usb_functions_rndis_adb,
	},
};

/* standard android USB platform data */
static struct android_usb_platform_data andusb_plat = {
	.vendor_id              = 0x04e8,
	.product_id             = 0x6860,
	.manufacturer_name      = "Samsung",
	.product_name           = "GT-P7100",
	.serial_number          = NULL,
	.num_products = ARRAY_SIZE(usb_products),
	.products = usb_products,
	.num_functions = ARRAY_SIZE(usb_functions_all),
	.functions = usb_functions_all,
};


static struct usb_ether_platform_data p3_rndis_pdata = {
	.vendorID	= 0x04e8,
	.vendorDescr	= "Samsung",
};

struct platform_device p3_rndis_device = {
	.name	= "rndis",
	.id	= -1,
	.dev	= {
		.platform_data = &p3_rndis_pdata,
	},
};

#else

#include <linux/usb/android_composite.h>
#define S3C_VENDOR_ID		0x18d1
#define S3C_PRODUCT_ID		0x0001
#define S3C_ADB_PRODUCT_ID	0x0005
#define MAX_USB_SERIAL_NUM	17

static char *usb_functions_ums[] = {
	"usb_mass_storage",
};

static char *usb_functions_rndis[] = {
	"rndis",
};

/* soonyong.cho : Variables for samsung composite such as kies, mtp, ums, etc... */
#ifdef CONFIG_USB_ANDROID_SAMSUNG_ESCAPE /* USE DEVGURU HOST DRIVER */
/* kies mode : using MS Composite*/
#ifdef CONFIG_USB_ANDROID_SAMSUNG_KIES_UMS
static char *usb_functions_ums_acm[] = {
	"usb_mass_storage",
	"acm",
};
#else
static char *usb_functions_mtp_acm[] = {
	"mtp",
	"acm",
};
#endif
/* debug mode : using MS Composite*/
static char *usb_functions_ums_acm_adb[] = {
	"usb_mass_storage",
	"acm",
	"adb",
};
#else /* USE MCCI HOST DRIVER */
/* kies mode */
static char *usb_functions_acm_mtp[] = {
	"acm",
	"mtp",
};
/* debug mode */
static char *usb_functions_acm_ums_adb[] = {
	"acm",
	"usb_mass_storage",
	"adb",
};
#endif
/* mtp only mode */
static char *usb_functions_mtp[] = {
	"mtp",
};
#ifdef CONFIG_USB_ANDROID_ACCESSORY
/* accessory mode */
static char *usb_functions_accessory[] = {
	"accessory",
};
static char *usb_functions_accessory_adb[] = {
	"accessory",
	"adb",
};
#endif
static char *usb_functions_all[] = {
#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
/* soonyong.cho : Every function driver for samsung composite.
 *  		  Number of to enable function features have to be same as below.
 */
#  ifdef CONFIG_USB_ANDROID_SAMSUNG_ESCAPE /* USE DEVGURU HOST DRIVER */
#ifdef CONFIG_USB_ANDROID_ACCESSORY
	"accessory",
#endif	
	"usb_mass_storage",
	"acm",
	"adb",
	"rndis",
#    ifndef CONFIG_USB_ANDROID_SAMSUNG_KIES_UMS
	"mtp",
#    endif
#  else /* USE MCCI HOST DRIVER */
	"acm",
	"usb_mass_storage",
	"adb",
	"rndis",
	"mtp",
#  endif
#else /* original */
#  ifdef CONFIG_USB_ANDROID_RNDIS
	"rndis",
#  endif
	"usb_mass_storage",
	"adb",
#  ifdef CONFIG_USB_ANDROID_ACM
	"acm",
#  endif
#endif /* CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE */
};

static struct android_usb_product usb_products[] = {
#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
/* soonyong.cho : Please modify below value correctly if you customize composite */
#  ifdef CONFIG_USB_ANDROID_SAMSUNG_ESCAPE /* USE DEVGURU HOST DRIVER */
#    ifdef CONFIG_USB_ANDROID_SAMSUNG_KIES_UMS
	{
		.product_id	= SAMSUNG_DEBUG_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_ums_acm_adb),
		.functions	= usb_functions_ums_acm_adb,
		.bDeviceClass	= 0xEF,
		.bDeviceSubClass= 0x02,
		.bDeviceProtocol= 0x01,
		.s		= ANDROID_DEBUG_CONFIG_STRING,
		.mode		= USBSTATUS_ADB,
	},
	{
		.product_id	= SAMSUNG_KIES_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_ums_acm),
		.functions	= usb_functions_ums_acm,
		.bDeviceClass	= 0xEF,
		.bDeviceSubClass= 0x02,
		.bDeviceProtocol= 0x01,
		.s		= ANDROID_KIES_CONFIG_STRING,
		.mode		= USBSTATUS_SAMSUNG_KIES,
	},
	{
		.product_id	= SAMSUNG_UMS_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_ums),
		.functions	= usb_functions_ums,
		.bDeviceClass	= USB_CLASS_PER_INTERFACE,
		.bDeviceSubClass= 0,
		.bDeviceProtocol= 0,
		.s		= ANDROID_UMS_CONFIG_STRING,
		.mode		= USBSTATUS_UMS,
	},
	{
		.product_id	= SAMSUNG_RNDIS_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_rndis),
		.functions	= usb_functions_rndis,
#      ifdef CONFIG_USB_ANDROID_SAMSUNG_RNDIS_WITH_MS_COMPOSITE
		.bDeviceClass	= 0xEF,
		.bDeviceSubClass= 0x02,
		.bDeviceProtocol= 0x01,
#      else
#        ifdef CONFIG_USB_ANDROID_RNDIS_WCEIS
		.bDeviceClass	= USB_CLASS_WIRELESS_CONTROLLER,
#        else
		.bDeviceClass	= USB_CLASS_COMM,
#        endif
		.bDeviceSubClass= 0,
		.bDeviceProtocol= 0,
#      endif
		.s		= ANDROID_RNDIS_CONFIG_STRING,
		.mode		= USBSTATUS_VTP,
	},
#    else /* Not used KIES_UMS */
	{
		.product_id	= SAMSUNG_DEBUG_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_ums_acm_adb),
		.functions	= usb_functions_ums_acm_adb,
		.bDeviceClass	= 0xEF,
		.bDeviceSubClass= 0x02,
		.bDeviceProtocol= 0x01,
		.s		= ANDROID_DEBUG_CONFIG_STRING,
		.mode		= USBSTATUS_ADB,
	},
	{
		.product_id	= SAMSUNG_KIES_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_mtp_acm),
		.functions	= usb_functions_mtp_acm,
		.bDeviceClass	= 0xEF,
		.bDeviceSubClass= 0x02,
		.bDeviceProtocol= 0x01,
		.s		= ANDROID_KIES_CONFIG_STRING,
		.mode		= USBSTATUS_SAMSUNG_KIES,
		.multi_conf_functions[0] = usb_functions_mtp,
		.multi_conf_functions[1] = usb_functions_mtp_acm,		
	},
	{
		.product_id	= SAMSUNG_UMS_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_ums),
		.functions	= usb_functions_ums,
		.bDeviceClass	= USB_CLASS_PER_INTERFACE,
		.bDeviceSubClass= 0,
		.bDeviceProtocol= 0,
		.s		= ANDROID_UMS_CONFIG_STRING,
		.mode		= USBSTATUS_UMS,
	},
#ifdef CONFIG_USB_ANDROID_ACCESSORY	
	{
		.vendor_id	= USB_ACCESSORY_VENDOR_ID,
		.product_id	= USB_ACCESSORY_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_accessory),
		.functions	= usb_functions_accessory,
		.bDeviceClass	= USB_CLASS_PER_INTERFACE,
		.bDeviceSubClass= 0,
		.bDeviceProtocol= 0,
		.s		= ANDROID_ACCESSORY_CONFIG_STRING,
		.mode		= USBSTATUS_ACCESSORY,
	},	
	{
		.vendor_id	= USB_ACCESSORY_VENDOR_ID,
		.product_id	= USB_ACCESSORY_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_accessory_adb),
		.functions	= usb_functions_accessory_adb,
		.bDeviceClass	= USB_CLASS_PER_INTERFACE,
		.bDeviceSubClass= 0,
		.bDeviceProtocol= 0,
		.s		= ANDROID_ACCESSORY_ADB_CONFIG_STRING,
		.mode		= USBSTATUS_ACCESSORY,
	},		
#endif	
	{
		.product_id	= SAMSUNG_RNDIS_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_rndis),
		.functions	= usb_functions_rndis,
#      ifdef CONFIG_USB_ANDROID_SAMSUNG_RNDIS_WITH_MS_COMPOSITE
		.bDeviceClass	= 0xEF,
		.bDeviceSubClass= 0x02,
		.bDeviceProtocol= 0x01,
#      else
#        ifdef CONFIG_USB_ANDROID_RNDIS_WCEIS
		.bDeviceClass	= USB_CLASS_WIRELESS_CONTROLLER,
#        else
		.bDeviceClass	= USB_CLASS_COMM,
#        endif
		.bDeviceSubClass= 0,
		.bDeviceProtocol= 0,
#      endif
		.s		= ANDROID_RNDIS_CONFIG_STRING,
		.mode		= USBSTATUS_VTP,
	},
	{
		.product_id	= SAMSUNG_MTP_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_mtp),
		.functions	= usb_functions_mtp,
		.bDeviceClass	= USB_CLASS_PER_INTERFACE,
		.bDeviceSubClass= 0,
		.bDeviceProtocol= 0x01,
		.s		= ANDROID_MTP_CONFIG_STRING,
		.mode		= USBSTATUS_MTPONLY,
	},
#    endif
#  else  /* USE MCCI HOST DRIVER */
	{
		.product_id	= SAMSUNG_DEBUG_PRODUCT_ID, /* change sequence */
		.num_functions	= ARRAY_SIZE(usb_functions_acm_ums_adb),
		.functions	= usb_functions_acm_ums_adb,
		.bDeviceClass	= USB_CLASS_COMM,
		.bDeviceSubClass= 0,
		.bDeviceProtocol= 0,
		.s		= ANDROID_DEBUG_CONFIG_STRING,
		.mode		= USBSTATUS_ADB,
	},
	{
		.product_id	= SAMSUNG_KIES_PRODUCT_ID, /* change sequence */
		.num_functions	= ARRAY_SIZE(usb_functions_acm_mtp),
		.functions	= usb_functions_acm_mtp,
		.bDeviceClass	= USB_CLASS_COMM,
		.bDeviceSubClass= 0,
		.bDeviceProtocol= 0,
		.s		= ANDROID_KIES_CONFIG_STRING,
		.mode		= USBSTATUS_SAMSUNG_KIES,

	},
	{
		.product_id	= SAMSUNG_UMS_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_ums),
		.functions	= usb_functions_ums,
		.bDeviceClass	= USB_CLASS_PER_INTERFACE,
		.bDeviceSubClass= 0,
		.bDeviceProtocol= 0,
		.s		= ANDROID_UMS_CONFIG_STRING,
		.mode		= USBSTATUS_UMS,
	},
	{
		.product_id	= SAMSUNG_RNDIS_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_rndis),
		.functions	= usb_functions_rndis,
#    ifdef CONFIG_USB_ANDROID_RNDIS_WCEIS
		.bDeviceClass	= USB_CLASS_WIRELESS_CONTROLLER,
#    else
		.bDeviceClass	= USB_CLASS_COMM,
#    endif
		.bDeviceSubClass= 0,
		.bDeviceProtocol= 0,
		.s		= ANDROID_RNDIS_CONFIG_STRING,
		.mode		= USBSTATUS_VTP,
	},
	{
		.product_id	= SAMSUNG_MTP_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_mtp),
		.functions	= usb_functions_mtp,
		.bDeviceClass	= USB_CLASS_PER_INTERFACE,
		.bDeviceSubClass= 0,
		.bDeviceProtocol= 0x01,
		.s		= ANDROID_MTP_CONFIG_STRING,
		.mode		= USBSTATUS_MTPONLY,
	},
#  endif
#else /* original */
	{
		.product_id	= S3C_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_ums),
		.functions	= usb_functions_ums,
	},
	{
		.product_id	= S3C_ADB_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_ums_adb),
		.functions	= usb_functions_ums_adb,
	},
#endif
};

static struct usb_mass_storage_platform_data ums_pdata = {
	.vendor			= "Android   ",//"Samsung",
	.product		= "UMS Composite",//"SMDKV210",
	.release		= 1,
	.nluns			= 1,
};
struct platform_device s3c_device_usb_mass_storage= {
	.name	= "usb_mass_storage",
	.id	= -1,
	.dev	= {
		.platform_data = &ums_pdata,
	},
};
EXPORT_SYMBOL(s3c_device_usb_mass_storage);

#ifdef CONFIG_USB_ANDROID_RNDIS
static struct usb_ether_platform_data rndis_pdata = {
/* ethaddr is filled by board_serialno_setup */
	.vendorID       = SAMSUNG_VENDOR_ID,
	.vendorDescr    = "Samsung",
	.ethaddr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
};
struct platform_device s3c_device_rndis= {
	.name   = "rndis",
	.id     = -1,
	.dev    = {
		.platform_data = &rndis_pdata,
	},
};
EXPORT_SYMBOL(s3c_device_rndis);
#endif



// serial number should be changed as real device for commercial release
static char device_serial[MAX_USB_SERIAL_NUM]="0123456789ABCDEF";
/* standard android USB platform data */

// Information should be changed as real product for commercial release
static struct android_usb_platform_data android_usb_pdata = {
#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
/* soonyong.cho : refered from S1 */
	.vendor_id		= SAMSUNG_VENDOR_ID,
	.product_id		= SAMSUNG_KIES_PRODUCT_ID,
	.manufacturer_name	= "SAMSUNG",
	.product_name		= "SAMSUNG_Android",
#else
	.vendor_id		= S3C_VENDOR_ID,
	.product_id		= S3C_PRODUCT_ID,
	.manufacturer_name	= "Android",//"Samsung",
	.product_name		= "Android",//"Samsung SMDKV210",
#endif
	.serial_number		= device_serial,
	.num_products 		= ARRAY_SIZE(usb_products),
	.products 		= usb_products,
	.num_functions 		= ARRAY_SIZE(usb_functions_all),
	.functions 		= usb_functions_all,
};

/* Changes value of nluns in order to use external storage */
static void __init usb_device_init(void)
{
	struct usb_mass_storage_platform_data *ums_pdata = s3c_device_usb_mass_storage.dev.platform_data;
	if(ums_pdata) {
		printk(KERN_DEBUG "%s: default luns=%d, system_rev=%d\n", __func__, ums_pdata->nluns, system_rev);
		ums_pdata->nluns = 1;
	}
	else {
		printk(KERN_DEBUG "I can't find s3c_device_usb_mass_storage\n");
	}
}

void __init s3c_usb_set_serial(void)
{
#ifdef CONFIG_USB_ANDROID_RNDIS
	int i;
	char *src;
#endif
	sprintf(device_serial, "%08X%08X", system_serial_high, system_serial_low);
#ifdef CONFIG_USB_ANDROID_RNDIS
	/* create a fake MAC address from our serial number.
	 * first byte is 0x02 to signify locally administered.
	 */
	rndis_pdata.ethaddr[0] = 0x02;
	src = device_serial;
	for (i = 0; *src; i++) {
		/* XOR the USB serial across the remaining bytes */
		rndis_pdata.ethaddr[i % (ETH_ALEN - 1) + 1] ^= *src++;
	}
#endif
}

#endif

static void sec_jack_set_micbias_state(bool on)
{
	printk(KERN_DEBUG
		"Board P5 : Enterring sec_jack_set_micbias_state = %d\n", on);
	gpio_set_value(GPIO_EAR_MICBIAS_EN, on);
}


#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
static struct platform_device androidusb_device = {
	.name   = "android_usb",
	.id     = -1,
	.dev    = {
		.platform_data  = &android_usb_pdata,
	},
};
#else
static struct platform_device androidusb_device = {
	.name   = "android_usb",
	.id     = -1,
	.dev    = {
		.platform_data  = &andusb_plat,
	},
};
#endif

static struct tegra_i2c_platform_data p3_i2c1_platform_data = {
	.adapter_nr	= 0,
	.bus_count	= 1,
	.bus_clk_rate	= { 400000, 0 },
	.slave_addr = 0x00FC,
};

static const struct tegra_pingroup_config i2c2_ddc = {
	.pingroup	= TEGRA_PINGROUP_DDC,
	.func		= TEGRA_MUX_I2C2,
};

static const struct tegra_pingroup_config i2c2_gen2 = {
	.pingroup	= TEGRA_PINGROUP_PTA,
	.func		= TEGRA_MUX_I2C2,
};

static struct tegra_i2c_platform_data p3_i2c2_platform_data = {
	.adapter_nr	= 1,
	.bus_count	= 2,
	.bus_clk_rate	= { 400000, 0 },
	.bus_mux	= { &i2c2_ddc, &i2c2_gen2 },
	.bus_mux_len	= { 1, 1 },
	.slave_addr = 0x00FC,
};

static struct tegra_i2c_platform_data p3_i2c3_platform_data = {
	.adapter_nr	= 3,
	.bus_count	= 1,
	.bus_clk_rate	= { 400000, 0 },
	.slave_addr = 0x00FC,
};

static struct tegra_i2c_platform_data p3_dvc_platform_data = {
	.adapter_nr	= 4,
	.bus_count	= 1,
	.bus_clk_rate	= { 400000, 0 },
	.is_dvc		= true,
};

static struct tegra_audio_platform_data tegra_audio_pdata[] = {
	/* For I2S1 */
	[0] = {
		.i2s_master	= true,
		.dma_on		= true,  /* use dma by default */
		.i2s_master_clk = 44100,
		.i2s_clk_rate	= 240000000,
		.dap_clk	= "clk_dev1",
		.audio_sync_clk = "audio_2x",
		.mode		= I2S_BIT_FORMAT_I2S,
		.fifo_fmt	= I2S_FIFO_PACKED,
		.bit_size	= I2S_BIT_SIZE_16,
		.i2s_bus_width = 32,
		.dsp_bus_width = 16,
		.en_dmic = false, /* by default analog mic is used */
	},
	/* For I2S2 */
	[1] = {
		.i2s_master	= true,
		.dma_on		= true,  /* use dma by default */
		.i2s_master_clk = 8000,
		.dsp_master_clk = 8000,
		.i2s_clk_rate	= 2000000,
		.dap_clk	= "clk_dev1",
		.audio_sync_clk = "audio_2x",
		.mode		= I2S_BIT_FORMAT_DSP,
		.fifo_fmt	= I2S_FIFO_16_LSB,
		.bit_size	= I2S_BIT_SIZE_16,
		.i2s_bus_width = 32,
		.dsp_bus_width = 16,
	}
};

static struct tegra_das_platform_data tegra_das_pdata = {
	.tegra_dap_port_info_table = {
		[0] = {
			.dac_port = tegra_das_port_none,
			.codec_type = tegra_audio_codec_type_none,
			.device_property = {
				.num_channels = 0,
				.bits_per_sample = 0,
				.rate = 0,
				.dac_dap_data_comm_format = 0,
			},
		},
		/* I2S1 <--> DAC1 <--> DAP1 <--> Hifi Codec */
		[1] = {
			.dac_port = tegra_das_port_i2s1,
			.codec_type = tegra_audio_codec_type_hifi,
			.device_property = {
				.num_channels = 2,
				.bits_per_sample = 16,
				.rate = 44100,
				.dac_dap_data_comm_format =
					dac_dap_data_format_i2s,
			},
		},
		[2] = {
			.dac_port = tegra_das_port_none,
			.codec_type = tegra_audio_codec_type_none,
			.device_property = {
				.num_channels = 0,
				.bits_per_sample = 0,
				.rate = 0,
				.dac_dap_data_comm_format = 0,
			},
		},
		[3] = {
			.dac_port = tegra_das_port_none,
			.codec_type = tegra_audio_codec_type_none,
			.device_property = {
				.num_channels = 0,
				.bits_per_sample = 0,
				.rate = 0,
				.dac_dap_data_comm_format = 0,
			},
		},
		[4] = {
			.dac_port = tegra_das_port_none,
			.codec_type = tegra_audio_codec_type_none,
			.device_property = {
				.num_channels = 0,
				.bits_per_sample = 0,
				.rate = 0,
				.dac_dap_data_comm_format = 0,
			},
		},
	},

	.tegra_das_con_table = {
		[0] = {
			.con_id = tegra_das_port_con_id_hifi,
			.num_entries = 4,
			.con_line = {
				[0] = {tegra_das_port_i2s1, tegra_das_port_dap1, true},
				[1] = {tegra_das_port_dap1, tegra_das_port_i2s1, false},
				[2] = {tegra_das_port_i2s2, tegra_das_port_dap4, true},
				[3] = {tegra_das_port_dap4, tegra_das_port_i2s2, false},
			},
		},
	}
};

static void p3_i2c_init(void)
{
	tegra_i2c_device1.dev.platform_data = &p3_i2c1_platform_data;
	tegra_i2c_device2.dev.platform_data = &p3_i2c2_platform_data;
	tegra_i2c_device3.dev.platform_data = &p3_i2c3_platform_data;
	tegra_i2c_device4.dev.platform_data = &p3_dvc_platform_data;

	platform_device_register(&tegra_i2c_device1);
	platform_device_register(&tegra_i2c_device2);
	platform_device_register(&tegra_i2c_device3);
	platform_device_register(&tegra_i2c_device4);
}

#define GPIO_KEY(_id, _gpio, _iswake)		\
	{					\
		.code = _id,			\
		.gpio = GPIO_##_gpio,	\
		.active_low = 0,		\
		.desc = #_id,			\
		.type = EV_KEY,			\
		.wakeup = _iswake,		\
		.debounce_interval = 10,	\
	}

static struct gpio_keys_button p3_keys[] = {
	[0] = GPIO_KEY(KEY_POWER, EXT_WAKEUP, 1), /* EXT_WAKEUP */
};

#define PMC_WAKE_STATUS 0x14

static int p3_wakeup_key(void)
{
	unsigned long status =
		readl(IO_ADDRESS(TEGRA_PMC_BASE) + PMC_WAKE_STATUS);
		
	if (status & TEGRA_WAKE_GPIO_PA0) {
		writel(TEGRA_WAKE_GPIO_PA0,
			IO_ADDRESS(TEGRA_PMC_BASE) + PMC_WAKE_STATUS);
	}		

	return status & TEGRA_WAKE_GPIO_PA0 ? KEY_POWER : KEY_RESERVED;
}

static bool p3_ckech_lpm(void)
{
	return charging_mode_from_boot ? true : false;
}

static struct gpio_keys_platform_data p3_keys_platform_data = {
	.buttons	= p3_keys,
	.nbuttons	= ARRAY_SIZE(p3_keys),
	.wakeup_key	= p3_wakeup_key,
#ifdef CONFIG_SAMSUNG_LPM_MODE
	.check_lpm = p3_ckech_lpm,
#endif
};

static struct platform_device p3_keys_device = {
	.name	= "gpio-keys",
	.id	= 0,
	.dev	= {
		.platform_data	= &p3_keys_platform_data,
	},
};

int __init tegra_p3_protected_aperture_init(void)
{
	       tegra_protected_aperture_init(tegra_grhost_aperture);
	       return 0;
	}
late_initcall(tegra_p3_protected_aperture_init);

static struct platform_device tegra_camera = {
	.name = "tegra_camera",
	.id = -1,
};

static void tegra_usb_ldo_en(int active, int instance)
{
	struct regulator *reg = regulator_get(NULL, "vdd_ldo6");
	int ret = 0;
	int try_times = 5;

	if (IS_ERR_OR_NULL(reg)) {
		pr_err("%s: failed to get vdd_ldo6 regulator\n", __func__);
		return;
	}

	pr_info("Board P5 : %s=%d instance=%d present regulator=%d\n",
		 __func__, active, instance, usb_data.usb_regulator_on[instance]);
	mutex_lock(&usb_data.ldo_en_lock);

	if (active) {
		if (!usb_data.usb_regulator_on[instance]) {
			do {
				ret = regulator_enable(reg);
				if (ret == 0)
					break;
				msleep(3);
			} while (try_times--);
			if (ret == 0)
				usb_data.usb_regulator_on[instance] = 1;
			else
				pr_err("%s: failed to turn on "
					"vdd_ldo6 regulator\n", __func__);
		}
	} else {
		regulator_disable(reg);
		usb_data.usb_regulator_on[instance] = 0;
	}
	regulator_put(reg);

	mutex_unlock(&usb_data.ldo_en_lock);
}

static void tegra_otg_en(int active)
{
	int gpio_otg_en;

	gpio_otg_en = GPIO_OTG_EN;

	active = !!active;
	gpio_direction_output(gpio_otg_en, active);
	pr_info("Board P5 : %s = %d\n", __func__, active);
}

void tegra_acc_power(u8 token, bool active)
{
	int gpio_acc_en;
	int gpio_acc_5v;
	int try_cnt = 0;
	static bool enable = false;
	static u8 acc_en_token = 0;

	if (system_rev < 0x06)
		gpio_acc_en = GPEX_GPIO_P8;
	else
		gpio_acc_en = GPIO_ACCESSORY_EN;
	gpio_acc_5v = GPIO_V_ACCESSORY_5V;

	/*	token info
		0 : force power off,
		1 : usb otg
		2 : ear jack
		3 : keyboard
	*/

	if (active) {
		acc_en_token |= (1 << token);
		enable = true;
		gpio_direction_output(gpio_acc_en, 1);
		msleep(1);
		while(!gpio_get_value(gpio_acc_5v)) {
			gpio_direction_output(gpio_acc_en, 0);
			msleep(10);
			gpio_direction_output(gpio_acc_en, 1);
			if (try_cnt > 30) {
				pr_err("[acc] failed to enable the accessory_en");
				break;
			} else
				try_cnt++;
		}
	} else {
		if (0 == token) {
			gpio_direction_output(gpio_acc_en, 0);
			enable = false;
		} else {
			acc_en_token &= ~(1 << token);
			if (0 == acc_en_token) {
				gpio_direction_output(gpio_acc_en, 0);
				enable = false;
			}
		}
	}
	pr_info("Board P4 : %s token : (%d,%d) %s\n", __func__,
		token, active, enable ? "on" : "off");
}

static int p3_kbc_keycode[] = {
	[0] = KEY_VOLUMEDOWN,
	[1] = KEY_VOLUMEUP,
};

static struct tegra_kbc_platform_data p3_kbc_platform = {
	.debounce_cnt = 10,
	.repeat_cnt = 1024,
	.scan_timeout_cnt = 3000 * 32,
	.pin_cfg = {
		[0] = {
			.num = 0,
			.pin_type = kbc_pin_row,
		},
		[1] = {
			.num = 1,
			.pin_type = kbc_pin_row,
		},
		[17] = {
			.num = 1,
			.pin_type = kbc_pin_col,
		},
	},
	.plain_keycode = p3_kbc_keycode,
};

static struct resource p3_kbc_resources[] = {
	[0] = {
		.start = TEGRA_KBC_BASE,
		.end   = TEGRA_KBC_BASE + TEGRA_KBC_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = INT_KBC,
		.end   = INT_KBC,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device p3_kbc_device = {
	.name = "tegra-kbc",
	.id = -1,
	.dev = {
		.platform_data = &p3_kbc_platform,
	},
	.resource = p3_kbc_resources,
	.num_resources = ARRAY_SIZE(p3_kbc_resources),
};

static bool check_samsung_charger(void)
{
	bool result;
	int sum = 0;
	int count;
	int vol_1;
	usb_path_type old_usb_sel_status;
	struct regulator *reg = regulator_get(NULL, "vdd_ldo6");

	/* when device wakes from suspend due to charger being plugged
	 * in, this code runs before nct1008.c resume enables ldo6
	 * so we need to enable it here.
	 */
	regulator_enable(reg);
	udelay(10);

	old_usb_sel_status = usb_sel_status;
	p3_set_usb_path(USB_SEL_ADC);

	mdelay(100);

	for (count = 0; count < 2; count++)
		sum += stmpe811_adc_get_value(6);

	vol_1 = sum / 2;
		pr_debug("%s : samsung_charger_adc = %d  !!\n", __func__, vol_1);

	if ((vol_1 > 800) && (vol_1 < 1800))
			result = true;
	else
		result = false;







	mdelay(50);

	p3_set_usb_path(old_usb_sel_status);

	regulator_disable(reg);
	regulator_put(reg);

	pr_debug("%s: returning %d\n", __func__, result);
	return result;
}

void p3_bat_gpio_init(void)
{
	gpio_request(GPIO_TA_EN, "GPIO_TA_EN");
	gpio_direction_output(GPIO_TA_EN, 0);
	tegra_gpio_enable(GPIO_TA_EN);

	gpio_request(GPIO_TA_nCONNECTED, "GPIO_TA_nCONNECTED");
	gpio_direction_input(GPIO_TA_nCONNECTED);
	tegra_gpio_enable(GPIO_TA_nCONNECTED);

	gpio_request(GPIO_TA_nCHG, "GPIO_TA_nCHG");
	gpio_direction_input(GPIO_TA_nCHG);
	tegra_gpio_enable(GPIO_TA_nCHG);

	if (system_rev >= 6) {
		gpio_request(GPIO_CURR_ADJ, "GPIO_CURR_ADJ");
		if (check_samsung_charger() == 1)
			gpio_direction_output(GPIO_CURR_ADJ, 1);
		else
			gpio_direction_output(GPIO_CURR_ADJ, 0);
		tegra_gpio_enable(GPIO_CURR_ADJ);
	} else {
		gpio_request(GPEX_GPIO_P5, "GPIO_CURR_ADJ");
		if (check_samsung_charger() == 1)
			gpio_direction_output(GPEX_GPIO_P5, 1);
		else
			gpio_direction_output(GPEX_GPIO_P5, 0);
	}

	if (system_rev >= 6) {
		gpio_request(GPIO_FUEL_ALRT, "GPIO_FUEL_ALRT");
		gpio_direction_input(GPIO_FUEL_ALRT);
		tegra_gpio_enable(GPIO_FUEL_ALRT);
	} else {
		gpio_request(GPEX_GPIO_P2, "GPIO_FUEL_ALRT");
		gpio_direction_input(GPEX_GPIO_P2);
	}

	pr_info("Battery GPIO initialized.\n");

}

//static void sec_bat_get_level(unsigned int *batt_level)
//{
//	sec_batt_level = batt_level;
//}

#if defined(CONFIG_TOUCHSCREEN_MELFAS)
static struct tsp_callbacks * charger_callbacks;
static void tsp_inform_charger_connection(int mode)
{
	if (charger_callbacks && charger_callbacks->inform_charger)
		charger_callbacks->inform_charger(charger_callbacks, mode);
}
#endif

static struct p3_battery_platform_data p3_battery_platform = {
	.charger = {
		.enable_line = GPIO_TA_EN,
		.connect_line = GPIO_TA_nCONNECTED,
		.fullcharge_line = GPIO_TA_nCHG,
		.currentset_line = GPIO_CURR_ADJ,
	},
	.check_dedicated_charger = check_samsung_charger,
	.init_charger_gpio = p3_bat_gpio_init,
//	.get_batt_level = sec_bat_get_level,
#if defined(CONFIG_TOUCHSCREEN_MELFAS)
	.inform_charger_connection = tsp_inform_charger_connection,
#endif

#ifdef CONFIG_TARGET_LOCALE_KOR
	.temp_high_threshold = 63000,	/* 580 (spec) + 35 (dT) */
	.temp_high_recovery = 45500,	/* 417 */
	.temp_low_recovery = 2300,		/* -10 */
	.temp_low_threshold = -2000,		/* -40 */
	.charge_duration = 10*60*60,	/* 10 hour */
	.recharge_duration = 2*60*60,	/* 2 hour */
	.recharge_voltage = 4150,	/*4.15V */
#else
	.temp_high_threshold = 50000,	/* 50c */
	.temp_high_recovery = 42000,	/* 42c */
	.temp_low_recovery = 2000,		/* 2c */
	.temp_low_threshold = 0,		/* 0c */
	.charge_duration = 10*60*60,	/* 10 hour */
	.recharge_duration = 1.5*60*60,	/* 1.5 hour */
	.recharge_voltage = 4150,	/*4.15V */
#endif
};

static struct platform_device p3_battery_device = {
	.name = "p3-battery",
	.id = -1,
	.dev = {
		.platform_data = &p3_battery_platform,
	},
};

#if defined(CONFIG_SEC_KEYBOARD_DOCK)
#if 0
struct uart_platform_data {
	void(*send_to_keyboard)(unsigned int val);
};

struct kbd_callbacks {
	void (*get_data)(struct kbd_callbacks *, unsigned int val);
#if 0
	int (*check_keyboard_dock)(struct kbd_callbacks *, int val);
#endif
};

static struct kbd_callbacks sec_kdb_cb;

static void uart_to_keyboard(unsigned int val)
{
	if (sec_kdb_cb && sec_kdb_cb->get_data)
		sec_kdb_cb->get_data(sec_kdb_cb, val);
}

static int check_keyboard(struct kbd_callbacks *, int val)
{
	if (sec_kdb_cb && sec_kdb_cb->check_keyboard_dock)
		return sec_kdb_cb->check_keyboard_dock(sec_kdb_cb, val);
	return 0;
}

static void sec_keyboard_register_callbacks(struct kbd_callbacks *cb)
{
	sec_kdb_cb = cb;
}

static struct dock_keyboard_platform_data kbd_pdata {
	.enable= ,
	.disable= ,
	.register_cb =sec_keyboard_register_callbacks,
};

static struct platform_device sec_keyboard = {
	.name	= "sec_keyboard",
	.id	= -1,
	.dev = {
		.platform_data = &kbd_pdata,
	}
};

static struct uart_platform_data uart_pdata {
	.send_to_keyboard =uart_to_keyboard,
};

#else

static struct dock_keyboard_platform_data kbd_pdata = {
	.acc_power = tegra_acc_power,
	.accessory_irq_gpio = GPIO_ACCESSORY_INT,
};

static struct platform_device sec_keyboard = {
	.name	= "sec_keyboard",
	.id	= -1,
	.dev = {
		.platform_data = &kbd_pdata,
	}
};
#endif
#endif

static struct sec_jack_zone sec_jack_zones[] = {
	{
		/* adc == 0, default to 3pole if it stays
		 * in this range for 40ms (20ms delays, 2 samples)
		 */
		.adc_high = 0,
		.delay_ms = 0, /* delay 20ms in stmpe811 driver */
		.check_count = 2,
		.jack_type = SEC_HEADSET_3POLE,
	},
	{
		/* 0 < adc <= 1199, unstable zone, default to 3pole if it stays
		 * in this range for a 400ms (20ms delays, 20 samples)
		 */
		.adc_high = 1199,
		.delay_ms = 0,
		.check_count = 20,
		.jack_type = SEC_HEADSET_3POLE,
	},
	{
		/* 1199 < adc <= 2000, unstable zone, default to 4pole if it
		 * stays in this range for 400ms (20ms delays, 20 samples)
		 */
		.adc_high = 2000,
		.delay_ms = 0,
		.check_count = 20,
		.jack_type = SEC_HEADSET_4POLE,
	},
	{
		/* 2000 < adc <= 3800, default to 4 pole if it stays */
		/* in this range for 40ms (20ms delays, 2 samples)
		 */
		.adc_high = 3800,
		.delay_ms = 0,
		.check_count = 2,
		.jack_type = SEC_HEADSET_4POLE,
	},
	{
		/* adc > 3800, unstable zone, default to 3pole if it stays
		 * in this range for a second (10ms delays, 100 samples)
		 */
		.adc_high = 0x7fffffff,
		.delay_ms = 0,
		.check_count = 100,
		.jack_type = SEC_HEADSET_3POLE,
	},
};

/* To support 3-buttons earjack */
static struct sec_jack_buttons_zone sec_jack_buttons_zones[] = {
	{
		/* 0 <= adc <=180, stable zone */
		.code		= KEY_MEDIA,
		.adc_low	= 0,
		.adc_high	= 180,
	},
	{
		/* 181 <= adc <= 410, stable zone */
		.code		= KEY_VOLUMEUP,
		.adc_low	= 181,
		.adc_high	= 410,
	},
	{
		/* 411 <= adc <= 1000, stable zone */
		.code		= KEY_VOLUMEDOWN,
		.adc_low	= 411,
		.adc_high	= 1000,
	},
};

static int sec_jack_get_adc_value(void)
{
	s16 ret;
	ret = stmpe811_adc_get_value(4);
	printk(KERN_DEBUG
		"Board P3 : Enterring sec_jack_get_adc_value = %d\n", ret);
	return  ret;
}

struct sec_jack_platform_data sec_jack_pdata = {
	.set_micbias_state = sec_jack_set_micbias_state,
	.get_adc_value = sec_jack_get_adc_value,
	.zones = sec_jack_zones,
	.num_zones = ARRAY_SIZE(sec_jack_zones),
	.buttons_zones = sec_jack_buttons_zones,
	.num_buttons_zones = ARRAY_SIZE(sec_jack_buttons_zones),
	.det_gpio = GPIO_DET_3_5,
	.send_end_gpio = GPIO_EAR_SEND_END,
};

static struct platform_device sec_device_jack = {
	.name			= "sec_jack",
	.id			= 1, /* will be used also for gpio_event id */
	.dev.platform_data	= &sec_jack_pdata,
};

#ifdef CONFIG_30PIN_CONN
struct acc_con_platform_data acc_con_pdata = {
	.otg_en = tegra_otg_en,
	.acc_power = tegra_acc_power,
	.usb_ldo_en = tegra_usb_ldo_en,
	.accessory_irq_gpio = GPIO_ACCESSORY_INT,
	.dock_irq_gpio = GPIO_DOCK_INT,
	.mhl_irq_gpio = GPIO_MHL_INT,
	.hdmi_hpd_gpio = GPIO_HDMI_HPD,
};
struct platform_device sec_device_connector = {
		.name = "acc_con",
		.id = -1,
		.dev.platform_data = &acc_con_pdata,
};
#endif

static struct platform_device p5_regulator_consumer = {
	.name = "p5-regulator-consumer",
	.id = -1,
};

#if defined(CONFIG_MACH_SAMSUNG_P5WIFI)
static struct platform_device *uart_wifi_devices[] __initdata = {
		&tegra_uartd_device, /* uartd for wifi test mode support */
};
#endif

static struct platform_device *p3_devices[] __initdata = {
	&androidusb_device,
#ifdef CONFIG_USB_ANDROID_RNDIS
	&s3c_device_rndis,
#else
	&p3_rndis_device,
#endif
	&debug_uart,
	&tegra_uarta_device,
	&tegra_btuart_device,
	&pmu_device,
	&tegra_udc_device,
	&tegra_gart_device,
	&tegra_aes_device,
	&p3_keys_device,
	&p3_kbc_device,
	&tegra_wdt_device,
	&p3_battery_device,
	&tegra_i2s_device1,
	&tegra_spdif_device,
	&tegra_avp_device,
	&tegra_camera,
	&sec_device_jack,
#ifdef CONFIG_30PIN_CONN
	&sec_device_connector,
#endif
	&tegra_das_device,
	&ram_console_device,
#if defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
	&tegra_spi_device1,
#endif
	&p5_regulator_consumer,
};

static void p3_keys_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(p3_keys); i++)
		tegra_gpio_enable(p3_keys[i].gpio);
}

static void sec_s5k5ccgx_init(void)
{
	printk("%s,, \n",__func__);

	tegra_gpio_enable(GPIO_CAM_L_nRST);
	tegra_gpio_enable(GPIO_CAM_F_nRST);
	tegra_gpio_enable(GPIO_CAM_F_STANDBY);
	//tegra_gpio_enable(GPIO_ISP_INT);		/* CAM_ISP_INT is not a tegra gpio */
	//tegra_gpio_enable(GPIO_CAM_EN);
	//tegra_gpio_enable(GPIO_CAM_MOVIE_EN);	/* CAM_MOVIE_EN is not a tegra gpio */
	//tegra_gpio_enable(GPIO_CAM_FLASH_EN);	/* CAM_FLASH_EN is not a tegra gpio */

	gpio_request(GPIO_CAM_L_nRST, "CAMERA_CAM_Left_nRST");
	gpio_request(GPIO_CAM_F_nRST, "CAMERA_CAM_Front_nRST");
	gpio_request(GPIO_CAM_F_STANDBY, "CAMERA_CAM_Front_STANDBY");
	gpio_request(GPIO_ISP_INT, "ISP_INT");
	gpio_request(GPIO_CAM_EN, "GPIO_CAM_EN");
	gpio_request(GPIO_CAM_MOVIE_EN, "CAM_MOVIE_EN");
	gpio_request(GPIO_CAM_FLASH_EN, "CAM_FLASH_EN");

	gpio_direction_output(GPIO_CAM_L_nRST, 0);
	gpio_direction_output(GPIO_CAM_F_nRST, 0);
	gpio_direction_output(GPIO_CAM_F_STANDBY, 0);

	//gpio_direction_input(GPIO_ISP_INT);
	gpio_direction_output(GPIO_ISP_INT, 0);
	gpio_direction_output(GPIO_CAM_EN, 0);
	gpio_direction_output(GPIO_CAM_MOVIE_EN, 0);
	gpio_direction_output(GPIO_CAM_FLASH_EN, 0);

}

struct tegra_pingroup_config mclk = {
	TEGRA_PINGROUP_CSUS,
	TEGRA_MUX_VI_SENSOR_CLK,
	TEGRA_PUPD_PULL_DOWN,
	TEGRA_TRI_TRISTATE
};

static const u16 s5k5ccgx_power_on_1[] = {
	0x0259, /*BUCK 1.2V 000 00100 CAM_ISP_CORE_1.2V*/
	0x0491, /*ldo4 1.2V 00000100 CAM_SENSOR_CORE_1.2V*/
	0x0709, /*Enable CAM_ISP_CORE_1.2V, CAM_SENSOR_CORE_1.2V*/
	0x08BE,
};

struct i2c_client *i2c_client_pmic;

static void p3_s5k5ccgx_power_on(void)
{
	s5k5ccgx_write_regs_pm(i2c_client_pmic, s5k5ccgx_power_on_1, 4);
	udelay(100);//mdelay(10);
	gpio_set_value(GPIO_CAM_EN, 1);
	udelay(100);//mdelay(10);
	gpio_set_value(GPIO_CAM_F_STANDBY, 0);
	udelay(100);//udelay(500);
	gpio_set_value(GPIO_CAM_F_nRST, 0);
	udelay(100);//udelay(500);

	tegra_pinmux_set_func(&mclk);
	tegra_pinmux_set_tristate(TEGRA_PINGROUP_CSUS, TEGRA_TRI_NORMAL);

	udelay(100);//mdelay(10);
	gpio_set_value(GPIO_ISP_INT, 1);//STBY
	udelay(100);//mdelay(10);
	gpio_set_value(GPIO_CAM_L_nRST, 1);
	udelay(100);//mdelay(10);
}

static void p3_s5k5ccgx_power_off(void)
{
	printk("===%s,, \n",__func__);

	msleep(3);
	gpio_set_value(GPIO_CAM_L_nRST, 0);
	udelay(100);
	tegra_pinmux_set_tristate(TEGRA_PINGROUP_CSUS, TEGRA_TRI_TRISTATE);
	udelay(500);
	gpio_set_value(GPIO_ISP_INT, 0);
	mdelay(10);
	gpio_set_value(GPIO_CAM_EN, 0);
	udelay(100);
}

static unsigned int p3_s5k5ccgx_isp_int_read(void)
{
	return gpio_get_value(GPIO_ISP_INT);
}

#define FLASH_MOVIE_MODE_CURRENT_100_PERCENT	1
#define FLASH_MOVIE_MODE_CURRENT_79_PERCENT	3
#define FLASH_MOVIE_MODE_CURRENT_50_PERCENT	7
#define FLASH_MOVIE_MODE_CURRENT_32_PERCENT	11
#define FLASH_MOVIE_MODE_CURRENT_28_PERCENT	12

#define FLASH_TIME_LATCH_US			500
#define FLASH_TIME_EN_SET_US			1

/* The AAT1274 uses a single wire interface to write data to its
 * control registers. An incoming value is written by sending a number
 * of rising edges to EN_SET. Data is 4 bits, or 1-16 pulses, and
 * addresses are 17 pulses or more. Data written without an address
 * controls the current to the LED via the default address 17. */
static void aat1274_write(int value)
{
	while (value--) {
		gpio_set_value(GPIO_CAM_MOVIE_EN, 0);
		udelay(FLASH_TIME_EN_SET_US);
		gpio_set_value(GPIO_CAM_MOVIE_EN, 1);
		udelay(FLASH_TIME_EN_SET_US);
	}
	udelay(FLASH_TIME_LATCH_US);
	/* At this point, the LED will be on */
}

static int P3_s5k5ccgx_flash(int enable)
{
	/* Turn main flash on or off by asserting a value on the EN line. */
	printk("========== flash enable = %d \n", enable);
	gpio_set_value(GPIO_CAM_FLASH_EN, enable);

	return 0;
}

static int P3_s5k5ccgx_af_assist(int enable)
{
	/* Turn assist light on or off by asserting a value on the EN_SET
	 * line. The default illumination level of 1/7.3 at 100% is used */

	printk("==========  P3_s5k5ccgx_af_assist = %d \n", enable);
	/*gpio_set_value(GPIO_CAM_MOVIE_EN, !!enable);
	if (!enable)
		gpio_set_value(GPIO_CAM_FLASH_EN, 0);*/
	gpio_set_value(GPIO_CAM_FLASH_EN, 0);
	if (enable)
		aat1274_write(FLASH_MOVIE_MODE_CURRENT_100_PERCENT);
	else
		gpio_set_value(GPIO_CAM_MOVIE_EN, 0);

	return 0;
}

static int P3_s5k5ccgx_torch(int enable)
{
	/* Turn torch mode on or off by writing to the EN_SET line. A level
	 * of 1/7.3 and 50% is used (half AF assist brightness). */

	printk("==========  P3_s5k5ccgx_torch = %d \n", enable);
#if 0
	if (enable) {
		aat1274_write(FLASH_MOVIE_MODE_CURRENT_50_PERCENT);
	} else {
		gpio_set_value(GPIO_CAM_MOVIE_EN, 0);
		gpio_set_value(GPIO_CAM_FLASH_EN, 0);
	}
#endif
	gpio_set_value(GPIO_CAM_FLASH_EN, 0);
	if (enable)
		aat1274_write(FLASH_MOVIE_MODE_CURRENT_79_PERCENT);
	else
		gpio_set_value(GPIO_CAM_MOVIE_EN, 0);
	return 0;
}

struct s5k5ccgx_platform_data p3_s5k5ccgx_data = {
	.power_on = p3_s5k5ccgx_power_on,
	.power_off = p3_s5k5ccgx_power_off,
	.flash_onoff = P3_s5k5ccgx_flash,
	.af_assist_onoff = P3_s5k5ccgx_af_assist,
	.torch_onoff = P3_s5k5ccgx_torch,
	.isp_int_read = p3_s5k5ccgx_isp_int_read
};

static const struct i2c_board_info sec_s5k5ccgx_camera[] = {
	{
		I2C_BOARD_INFO("s5k5ccgx", 0x78>>1),
		.platform_data = &p3_s5k5ccgx_data,
	},
};

static const struct i2c_board_info sec_pmic_camera[] = {
	{
		I2C_BOARD_INFO("s5k5ccgx_pmic", 0xFA>>1),
	},
};
struct tegra_pingroup_config s5k5bbgx_mclk = {
	TEGRA_PINGROUP_CSUS, TEGRA_MUX_VI_SENSOR_CLK, TEGRA_PUPD_PULL_DOWN, TEGRA_TRI_TRISTATE
};

void p3_s5k5bbgx_power_on(void)
{
	printk("%s,, \n",__func__);

	gpio_set_value(GPIO_ISP_INT, 0);//STBY
	mdelay(30);
	gpio_set_value(GPIO_CAM_L_nRST, 0);
	mdelay(30);
	printk("%s,, 222\n",__func__);

	s5k5ccgx_write_regs_pm(i2c_client_pmic, s5k5ccgx_power_on_1, 4);
	mdelay(10);
	gpio_set_value(GPIO_CAM_EN, 1);
	mdelay(10);
	printk("%s,, 111\n",__func__);

	tegra_pinmux_set_func(&s5k5bbgx_mclk);
	tegra_pinmux_set_tristate(TEGRA_PINGROUP_CSUS, TEGRA_TRI_NORMAL);
	mdelay(10);

	/*gpio_set_value(GPIO_ISP_INT, 0);//STBY
	mdelay(10);
	gpio_set_value(GPIO_CAM_L_nRST, 0);
	mdelay(10);
	printk("%s,, 222\n",__func__);*/

	gpio_set_value(GPIO_CAM_F_STANDBY, 1);
	udelay(500);
	gpio_set_value(GPIO_CAM_F_nRST, 1);
	udelay(500);

	/*gpio_set_value(GPIO_ISP_INT, 0);//STBY
	mdelay(10);
	gpio_set_value(GPIO_CAM_L_nRST, 0);
	mdelay(10);*/

	printk("%s,, 333\n",__func__);
}

void p3_s5k5bbgx_power_off(void)
{
	msleep(3);
	gpio_set_value(GPIO_CAM_F_nRST, 0);
	udelay(500);
	gpio_set_value(GPIO_CAM_F_STANDBY, 0);
	udelay(500);

	tegra_pinmux_set_tristate(TEGRA_PINGROUP_CSUS, TEGRA_TRI_TRISTATE);
	mdelay(10);

	gpio_set_value(GPIO_CAM_EN, 0);
	mdelay(10);
}

struct s5k5bbgx_platform_data p3_s5k5bbgx_data = {
	.power_on = p3_s5k5bbgx_power_on,
	.power_off = p3_s5k5bbgx_power_off
};

static const struct i2c_board_info sec_s5k5bbgx_camera[] = {
	{
		I2C_BOARD_INFO("s5k5bbgx", 0x5a>>1),
		.platform_data = &p3_s5k5bbgx_data,
	},
};

static int __init p3_camera_init(void)
{
	int status;
	status = i2c_register_board_info(7, sec_pmic_camera,
					ARRAY_SIZE(sec_pmic_camera));
	status = i2c_register_board_info(3, sec_s5k5ccgx_camera,
				ARRAY_SIZE(sec_s5k5ccgx_camera));

	status = i2c_register_board_info(3, sec_s5k5bbgx_camera,
				ARRAY_SIZE(sec_s5k5bbgx_camera));

	return 0;
}

#if defined(CONFIG_TOUCHSCREEN_WACOM_G5)
static int p3_check_touch_silence(void)
{
	pr_debug("p3_check_touch_silence\n");

	if(wacom_callbacks && wacom_callbacks->check_prox)
		return wacom_callbacks->check_prox(wacom_callbacks);
	else
		return 0;
};
#endif
#if defined(CONFIG_TOUCHSCREEN_MELFAS)
static void register_tsp_callbacks(struct tsp_callbacks *cb)
{
	charger_callbacks = cb;
}

/*
static void touch_exit_hw(void)
{
	pr_info("%s\n", __func__);
	gpio_free(GPIO_TOUCH_INT);
	gpio_free(GPIO_TOUCH_EN);

	tegra_gpio_disable(GPIO_TOUCH_INT);
	tegra_gpio_disable(GPIO_TOUCH_EN);
}
*/

static void melfas_touch_power_enable(int en)
{
	pr_info("%s %s\n", __func__, en ? "on" : "off");
	gpio_direction_output(GPIO_TOUCH_EN, en ? 1 : 0);
}

static struct melfas_tsi_platform_data melfas_touch_platform_data = {
	.max_x = 799,
	.max_y = 1279,
	.max_pressure = 255,
	.max_width = 255,
	.gpio_scl = GPIO_TSP_SCL,
	.gpio_sda = GPIO_TSP_SDA,
	.power_enable = melfas_touch_power_enable,
	.register_cb = register_tsp_callbacks,
};

static const struct i2c_board_info sec_i2c_touch_info[] = {
	{
		I2C_BOARD_INFO("melfas-ts", 0x48),
		.irq		= TEGRA_GPIO_TO_IRQ(GPIO_TOUCH_INT),
		.platform_data = &melfas_touch_platform_data,
	},
};

static void touch_init_hw(void)
{
	pr_info("%s\n", __func__);
	gpio_request(GPIO_TOUCH_EN, "TOUCH_EN");
	gpio_request(GPIO_TOUCH_INT, "TOUCH_INT");

	gpio_direction_output(GPIO_TOUCH_EN, 1);
	gpio_direction_input(GPIO_TOUCH_INT);

	tegra_gpio_enable(GPIO_TOUCH_EN);
	tegra_gpio_enable(GPIO_TOUCH_INT);

	if (system_rev < 0x9)
#if defined(CONFIG_MACH_SAMSUNG_P5KORWIFI)
		melfas_touch_platform_data.gpio_touch_id  = 1;
#else
		melfas_touch_platform_data.gpio_touch_id  = 0;
#endif
	else {
		gpio_request(GPIO_TOUCH_ID, "TOUCH_ID");
		gpio_direction_input(GPIO_TOUCH_ID);
		tegra_gpio_enable(GPIO_TOUCH_ID);
		melfas_touch_platform_data.gpio_touch_id  =
			gpio_get_value(GPIO_TOUCH_ID);
	}
}

static int __init touch_init(void)
{
	touch_init_hw();

	i2c_register_board_info(0, sec_i2c_touch_info,
					ARRAY_SIZE(sec_i2c_touch_info));

	return 0;
}
#endif  /*CONFIG_TOUCHSCREEN_MELFAS*/

static struct usb_phy_plat_data tegra_usb_phy_pdata[] = {
	[0] = {
			.instance = 0,
//			.vbus_irq = TPS6586X_INT_BASE + TPS6586X_INT_USB_DET,
			.vbus_gpio = -1,
			.usb_ldo_en = tegra_usb_ldo_en,
	},
	[1] = {
			.instance = 1,
			.vbus_gpio = -1,
	},
	[2] = {
			.instance = 2,
			.vbus_gpio = -1,
			.usb_ldo_en = tegra_usb_ldo_en,
	},
};

static struct tegra_ehci_platform_data tegra_ehci_pdata[] = {
	[0] = {
		.phy_config = &utmi_phy_config[0],
		.operating_mode = TEGRA_USB_OTG,
		.power_down_on_bus_suspend = 0,
		.host_notify = 1,
		.sec_whlist_table_num = 1,
/* Don't merge with P3
		.currentlimit_irq = TEGRA_GPIO_TO_IRQ(GPIO_V_ACCESSORY_5V),
*/
	},
	[1] = {
		.phy_config = &hsic_phy_config,
		.operating_mode = TEGRA_USB_HOST,
		.power_down_on_bus_suspend = 1,
	},
	[2] = {
		.phy_config = &utmi_phy_config[1],
		.operating_mode = TEGRA_USB_HOST,
		.power_down_on_bus_suspend = 1,
	},
};

static struct platform_device *tegra_usb_otg_host_register(void)
{
	struct platform_device *pdev;
	void *platform_data;
	int val;

	pdev = platform_device_alloc(tegra_ehci1_device.name,
				tegra_ehci1_device.id);
	if (!pdev)
		return NULL;

	val = platform_device_add_resources(pdev, tegra_ehci1_device.resource,
		tegra_ehci1_device.num_resources);
	if (val)
		goto error;

	pdev->dev.dma_mask =  tegra_ehci1_device.dev.dma_mask;
	pdev->dev.coherent_dma_mask = tegra_ehci1_device.dev.coherent_dma_mask;

       platform_data = kmalloc(sizeof(struct tegra_ehci_platform_data),
                                GFP_KERNEL);
       if (!platform_data)
               goto error;

       memcpy(platform_data, &tegra_ehci_pdata[0],
                               sizeof(struct tegra_ehci_platform_data));
       pdev->dev.platform_data = platform_data;

	val = platform_device_add(pdev);
	if (val)
		goto error_add;

	return pdev;

error_add:
	kfree(platform_data);
error:
	pr_err("%s: failed to add the host contoller device\n", __func__);
	platform_device_put(pdev);
	return NULL;
}

static void tegra_usb_otg_host_unregister(struct platform_device *pdev)
{
	kfree(pdev->dev.platform_data);
	pdev->dev.platform_data = NULL;
	platform_device_unregister(pdev);
}

static struct tegra_otg_platform_data tegra_otg_pdata = {
	.host_register = &tegra_usb_otg_host_register,
	.host_unregister = &tegra_usb_otg_host_unregister,
	.otg_en = tegra_otg_en,
//	.batt_level = &sec_batt_level,
	.currentlimit_irq = TEGRA_GPIO_TO_IRQ(GPIO_V_ACCESSORY_5V),
};

static void p3_usb_gpio_init(void)
{
	int gpio_otg_en;
	int ret;

	gpio_otg_en = GPIO_OTG_EN;

	/* Because OTG_EN is gpio expandar */
	//tegra_gpio_enable(gpio_otg_en);
	ret = gpio_request(gpio_otg_en, "GPIO_OTG_EN");
	if (ret) {
		pr_err("%s: gpio_request() for OTG_EN failed\n",
			__func__);
		return;
	}
	gpio_direction_output(gpio_otg_en, 0);

	if (system_rev >= 6) {
		/* Because ACCESSORY_EN is gpio expandar */
		//tegra_gpio_enable(GPIO_ACCESSORY_EN);
		ret = gpio_request(GPIO_ACCESSORY_EN, "GPIO_ACCESSORY_EN");
		if (ret) {
			pr_err("%s: gpio_request() for ACCESSORY_EN failed\n",
				__func__);
			return;
		}
		gpio_direction_output(GPIO_ACCESSORY_EN, 0);
	}

	pr_info("Board P5 : %s\n", __func__);
}

#define AHB_ARBITRATION_DISABLE                0x0
#define   USB_ENB                      (1 << 6)
#define   USB2_ENB                     (1 << 18)
#define   USB3_ENB                     (1 << 17)

#define AHB_ARBITRATION_PRIORITY_CTRL   0x4
#define   AHB_PRIORITY_WEIGHT(x)       (((x) & 0x7) << 29)
#define   PRIORITY_SELEECT_USB         (1 << 6)
#define   PRIORITY_SELEECT_USB2                (1 << 18)
#define   PRIORITY_SELEECT_USB3                (1 << 17)

#define AHB_GIZMO_AHB_MEM              0xc
#define   ENB_FAST_REARBITRATE         (1 << 2)

#define AHB_GIZMO_APB_DMA              0x10

#define AHB_GIZMO_USB                  0x1c
#define AHB_GIZMO_USB2                 0x78
#define AHB_GIZMO_USB3                 0x7c
#define   IMMEDIATE                    (1 << 18)
#define   MAX_AHB_BURSTSIZE(x)         (((x) & 0x3) << 16)
#define          DMA_BURST_1WORDS              MAX_AHB_BURSTSIZE(0)
#define          DMA_BURST_4WORDS              MAX_AHB_BURSTSIZE(1)
#define          DMA_BURST_8WORDS              MAX_AHB_BURSTSIZE(2)
#define          DMA_BURST_16WORDS             MAX_AHB_BURSTSIZE(3)

#define AHB_MEM_PREFETCH_CFG3          0xe0
#define AHB_MEM_PREFETCH_CFG4          0xe4
#define AHB_MEM_PREFETCH_CFG1          0xec
#define AHB_MEM_PREFETCH_CFG2          0xf0
#define   PREFETCH_ENB                 (1 << 31)
#define   MST_ID(x)                    (((x) & 0x1f) << 26)
#define   USB_MST_ID                   MST_ID(6)
#define   USB2_MST_ID                  MST_ID(18)
#define   USB3_MST_ID                  MST_ID(17)
#define   ADDR_BNDRY(x)                        (((x) & 0xf) << 21)
#define   INACTIVITY_TIMEOUT(x)                (((x) & 0xffff) << 0)

static inline unsigned long gizmo_readl(unsigned long offset)
{
       return readl(IO_TO_VIRT(TEGRA_AHB_GIZMO_BASE + offset));
}

static inline void gizmo_writel(unsigned long value, unsigned long offset)
{
       writel(value, IO_TO_VIRT(TEGRA_AHB_GIZMO_BASE + offset));
}

static void p3_usb_init(void)
{
	int gpio_v_accessory_5v;
	int ret;
  unsigned long val;

	mutex_init(&usb_data.ldo_en_lock);
	usb_data.usb_regulator_on[0] = 0;
	usb_data.usb_regulator_on[1] = 0;
	usb_data.usb_regulator_on[2] = 0;

	gpio_v_accessory_5v = GPIO_V_ACCESSORY_5V;

	tegra_gpio_enable(gpio_v_accessory_5v);
	ret = gpio_request(gpio_v_accessory_5v, "GPIO_V_ACCESSORY_5V");
	if (ret) {
		pr_err("%s: gpio_request() for V_ACCESSORY_5V failed\n",
			__func__);
		return;
	}
	gpio_direction_input(gpio_v_accessory_5v);

//	p3_usb_gpio_init(); initialize in stmpe1801

	tegra_usb_phy_init(tegra_usb_phy_pdata, ARRAY_SIZE(tegra_usb_phy_pdata));

  	/*boost USB1 performance*/
  	val = gizmo_readl(AHB_GIZMO_AHB_MEM);
  	val |= ENB_FAST_REARBITRATE;
  	gizmo_writel(val, AHB_GIZMO_AHB_MEM);

  	val = gizmo_readl(AHB_GIZMO_USB);
  	val |= IMMEDIATE;
  	gizmo_writel(val, AHB_GIZMO_USB);

  	val = gizmo_readl(AHB_GIZMO_USB3);
  	val |= IMMEDIATE;
  	gizmo_writel(val, AHB_GIZMO_USB3);

  	val = gizmo_readl(AHB_ARBITRATION_PRIORITY_CTRL);
  	val |= PRIORITY_SELEECT_USB | PRIORITY_SELEECT_USB3 | AHB_PRIORITY_WEIGHT(7);
  	gizmo_writel(val, AHB_ARBITRATION_PRIORITY_CTRL);

  	val = gizmo_readl(AHB_MEM_PREFETCH_CFG1);
  	val &= ~MST_ID(~0);
  	val |= PREFETCH_ENB | MST_ID(5) | ADDR_BNDRY(0xc) | INACTIVITY_TIMEOUT(0x1000);
  	gizmo_writel(val, AHB_MEM_PREFETCH_CFG1);

  	val = gizmo_readl(AHB_MEM_PREFETCH_CFG2);
  	val &= ~MST_ID(~0);
  	val |= PREFETCH_ENB | USB_MST_ID | ADDR_BNDRY(0xc) | INACTIVITY_TIMEOUT(0x1000);
  	gizmo_writel(val, AHB_MEM_PREFETCH_CFG2);

  	val = gizmo_readl(AHB_MEM_PREFETCH_CFG3);
  	val &= ~MST_ID(~0);
  	val |= PREFETCH_ENB | USB3_MST_ID | ADDR_BNDRY(0xc) | INACTIVITY_TIMEOUT(0x1000);
  	gizmo_writel(val, AHB_MEM_PREFETCH_CFG3);

	tegra_otg_device.dev.platform_data = &tegra_otg_pdata;
	platform_device_register(&tegra_otg_device);
#if defined(CONFIG_SEC_MODEM)
#ifdef CONFIG_SAMSUNG_LPM_MODE
	if (!charging_mode_from_boot) {
		tegra_ehci2_device.dev.platform_data = &tegra_ehci_pdata[1];
		platform_device_register(&tegra_ehci2_device);
	}
#else
	tegra_ehci2_device.dev.platform_data = &tegra_ehci_pdata[1];
	platform_device_register(&tegra_ehci2_device);
#endif
#endif
#if 0 /*USB team must verify*/
	/* create a fake MAC address from our serial number.
	 * first byte is 0x02 to signify locally administered.
	 */
	src = andusb_plat.serial_number;
	p3_rndis_pdata.ethaddr[0] = 0x02;
	for (i = 0; *src; i++) {
		/* XOR the USB serial across the remaining bytes */
		p3_rndis_pdata.ethaddr[i % (ETH_ALEN - 1) + 1] ^= *src++;
	}
#endif
}

static void p3_wlan_gpio_config(void)
{
	printk(KERN_DEBUG "&&&  p3_wlan_gpio_config   &&&\n");
	gpio_request(GPIO_WLAN_EN, "GPIO_WLAN_EN");
	gpio_direction_output(GPIO_WLAN_EN, 0);
	tegra_gpio_enable(GPIO_WLAN_EN);
}

/*
static void p3_wlan_gpio_unconfig(void)
{
	printk(KERN_DEBUG "### p3_wlan_gpio_unconfig  ###\n");

}
*/

void p3_wlan_gpio_enable(void)
{
	wifi_32k_clk = clk_get_sys(NULL, "blink");
	if (IS_ERR(wifi_32k_clk)) {
		printk(KERN_DEBUG "%s: unable to get blink clock\n",
				__func__);
		return;
	}

	printk(KERN_DEBUG "--------------------------------------\n");

	printk(KERN_DEBUG "wlan power on OK\n");
	gpio_set_value(GPIO_WLAN_EN, 1);
	mdelay(100);
	clk_enable(wifi_32k_clk);
	printk(KERN_DEBUG "wlan get value  (%d)\n",
					gpio_get_value(GPIO_WLAN_EN));
}
EXPORT_SYMBOL(p3_wlan_gpio_enable);

void p3_wlan_gpio_disable(void)
{
	printk(KERN_DEBUG "-------------------------------------\n");
	printk(KERN_DEBUG "wlan power off OK\n");
	gpio_set_value(GPIO_WLAN_EN, 0);
	mdelay(100);
	clk_disable(wifi_32k_clk);
	printk(KERN_DEBUG "wlan get value  (%d)\n",
				gpio_get_value(GPIO_WLAN_EN));

}
EXPORT_SYMBOL(p3_wlan_gpio_disable);


int	is_JIG_ON_high()
{
	return !gpio_get_value(GPIO_IFCONSENSE);
}
EXPORT_SYMBOL(is_JIG_ON_high);

static int p3_jack_init(void)
{
	int ret = 0;
	int ear_micbias = 0;

	ear_micbias = GPIO_EAR_MICBIAS_EN;

	ret = gpio_request(GPIO_MICBIAS_EN, "micbias_enable");
	if (ret < 0)
		return ret;

	ret = gpio_request(ear_micbias, "ear_micbias_enable");
	if (ret < 0) {
		gpio_free(ear_micbias);
		return ret;
	}

	ret = gpio_direction_output(GPIO_MICBIAS_EN, 0);
	if (ret < 0)
		goto cleanup;

	ret = gpio_direction_output(ear_micbias, 0);
	if (ret < 0)
		goto cleanup;

	//tegra_gpio_enable(GPIO_MICBIAS_EN);	/* MICBIAS_EN is not a tegra gpio */
	//tegra_gpio_enable(ear_micbias);		/* EAR_MICBIAS_EN is not tegra gpio */
	tegra_gpio_enable(GPIO_DET_3_5);
	tegra_gpio_enable(GPIO_EAR_SEND_END);

cleanup:
	gpio_free(GPIO_MICBIAS_EN);
	gpio_free(ear_micbias);

	return ret;
}

/*
static struct bcm4751_rfkill_platform_data p3_gps_rfkill_pdata = {
	.gpio_nrst = GPIO_GPS_N_RST,
	.gpio_pwr_en	= GPIO_GPS_PWR_EN,
};
*/

/*
static struct platform_device p3_gps_rfkill_device = {
	.name = "bcm4751_rfkill",
	.id	= -1,
	.dev	= {
		.platform_data = &p3_gps_rfkill_pdata,
	},
};
*/

static int __init p3_gps_init(void)
{
	struct clk *clk32 = clk_get_sys(NULL, "blink");
	if (!IS_ERR(clk32)) {
		clk_set_rate(clk32, clk32->parent->rate);
		clk_enable(clk32);
	}

	//tegra_gpio_enable(GPIO_GPS_N_RST);	/* GPS_N_RST is not a tegra gpio */
	//tegra_gpio_enable(GPIO_GPS_PWR_EN);	/* GPS_PWR_EN is not a tegra gpio */

	return 0;
}

#if defined(CONFIG_MACH_SAMSUNG_P5LTE)
static int __init p3_lte_pmic_init(void)
{
	printk("p3_lte_pmic_init\n");

	gpio_request(GPIO_220_PMIC_PWR_ON, "220_PMIC_PWR_ON");
	gpio_request(GPIO_CMC_RST, "CMC_RST");

	gpio_direction_output(GPIO_220_PMIC_PWR_ON, 1);
	gpio_direction_output(GPIO_CMC_RST, 1);

	tegra_gpio_enable(GPIO_220_PMIC_PWR_ON);
	tegra_gpio_enable(GPIO_CMC_RST);
}
#endif
#if defined(CONFIG_TOUCHSCREEN_WACOM_G5)
static void p3_register_wacom_callbacks(struct wacom_g5_callbacks *cb)
{
	printk(KERN_INFO "p3_register_wacom_callbacks\n");
	wacom_callbacks = cb;
};

static void p3_digitizer_init_hw(void)
{
	printk("p3_digitizer_init_hw\n");

	if (system_rev >= 6)
		gpio_request(GPIO_PEN_SLP, "PEN_SLP");
	gpio_request(GPIO_PEN_PDCT, "PEN_PDCT");
	gpio_request(GPIO_PEN_IRQ, "PEN_IRQ");

	if (system_rev >= 6)
		gpio_direction_output(GPIO_PEN_SLP, 0);
	gpio_direction_input(GPIO_PEN_PDCT);
	gpio_direction_input(GPIO_PEN_IRQ);

	if (system_rev >= 6)
		tegra_gpio_enable(GPIO_PEN_SLP);
	tegra_gpio_enable(GPIO_PEN_PDCT);
	tegra_gpio_enable(GPIO_PEN_IRQ);
}

#include "p5_wacom_firmware.c"
static struct wacom_g5_firmware_data p3_digitizer_firmware_data;

static void p3_digitizer_init_firmware()
{
	p3_digitizer_firmware_data.binary_length = Binary_nLength;
	p3_digitizer_firmware_data.mpu_type = Mpu_type;
	p3_digitizer_firmware_data.version = Firmware_version_of_file;
	p3_digitizer_firmware_data.binary = Binary;
}

static struct wacom_g5_platform_data p3_digitizer_platform_data = {
	.x_invert = 1,
	.y_invert = 0,
	.xy_switch = 1,
	.pdct_irq = TEGRA_GPIO_TO_IRQ(GPIO_PEN_PDCT),
	.init_platform_hw 	= p3_digitizer_init_hw,
	.firm_data = &p3_digitizer_firmware_data,
	/* .exit_platform_hw = p3_touch_exit_hw, */
	/* .suspend_platform_hw = p3_touch_suspend_hw, */
	/* .resume_platform_hw = p3_touch_resume_hw, */
	.register_cb = p3_register_wacom_callbacks,
};

static const struct i2c_board_info sec_i2c_digitizer_info[] = {
	{
		I2C_BOARD_INFO("wacom_g5_i2c",  0x56),
		.irq		= TEGRA_GPIO_TO_IRQ(GPIO_PEN_IRQ),
		.platform_data = &p3_digitizer_platform_data,
	},
};

static int __init p3_digitizer_init(void)
{
	p3_digitizer_init_hw();
	p3_digitizer_init_firmware();
	i2c_register_board_info(1, sec_i2c_digitizer_info, ARRAY_SIZE(sec_i2c_digitizer_info));

	return 0;
}
#endif /*CONFIG_TOUCHSCREEN_WACOM_G5*/

#if defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
static void tdmb_gpio_on(void)
{
	printk("tdmb_gpio_on\n");

        tegra_gpio_disable(GPIO_TDMB_SPI_CS);
        tegra_gpio_disable(GPIO_TDMB_SPI_CLK);
        tegra_gpio_disable(GPIO_TDMB_SPI_MOSI);
        tegra_gpio_disable(GPIO_TDMB_SPI_MISO);

	gpio_set_value(GPIO_TDMB_EN, 1);
	msleep(10);
	gpio_set_value(GPIO_TDMB_RST, 0);
	msleep(2);
	gpio_set_value(GPIO_TDMB_RST, 1);
	msleep(10);
}

static void tdmb_gpio_off(void)
{
	printk("tdmb_gpio_off\n");

	gpio_set_value(GPIO_TDMB_RST, 0);
	msleep(1);
	gpio_set_value(GPIO_TDMB_EN, 0);

	tegra_gpio_enable(GPIO_TDMB_SPI_CS);
	tegra_gpio_enable(GPIO_TDMB_SPI_CLK);
	tegra_gpio_enable(GPIO_TDMB_SPI_MOSI);
	tegra_gpio_enable(GPIO_TDMB_SPI_MISO);
	gpio_set_value(GPIO_TDMB_SPI_CS, 0);
	gpio_set_value(GPIO_TDMB_SPI_CLK, 0);
	gpio_set_value(GPIO_TDMB_SPI_MOSI, 0);
	gpio_set_value(GPIO_TDMB_SPI_MISO, 0);
}

static struct tdmb_platform_data tdmb_pdata = {
	.gpio_on = tdmb_gpio_on,
	.gpio_off = tdmb_gpio_off,
	.irq = TEGRA_GPIO_TO_IRQ(GPIO_TDMB_INT),
};

static struct platform_device tdmb_device = {
	.name			= "tdmb",
	.id 			= -1,
	.dev			= {
		.platform_data = &tdmb_pdata,
	},
};

static struct spi_board_info tegra_spi_tdmb_devices[] __initdata = {
	{
		.modalias = "tdmbspi",
		.platform_data = NULL,
		.max_speed_hz = 5000000,//20000000,
		.bus_num = 0,
		.chip_select = 0,
		.mode = SPI_MODE_0,
	},
};

static void use_sys_clk_req_gpio(void)
{

    unsigned long pmc_base_reg = IO_APB_VIRT + 0xE400;
    unsigned long offset = 0x1c;
    unsigned long mask = (1 << 21);
    volatile unsigned long *p = (volatile unsigned long *)(pmc_base_reg + offset);
    *p = *p & ~mask;

}

static int __init p5_tdmb_init(void)
{
	printk(KERN_DEBUG "p5_tdmb_init\n");

	//use_sys_clk_req_gpio();

	tegra_gpio_enable(GPIO_TDMB_RST);
	tegra_gpio_enable(GPIO_TDMB_EN);
	tegra_gpio_enable(GPIO_TDMB_INT);

	gpio_request(GPIO_TDMB_EN, "TDMB_EN");
	gpio_direction_output(GPIO_TDMB_EN, 0);
	gpio_request(GPIO_TDMB_RST, "TDMB_RST");
	gpio_direction_output(GPIO_TDMB_RST, 0);
	gpio_request(GPIO_TDMB_INT, "TDMB_INT");
	gpio_direction_input(GPIO_TDMB_INT);

	platform_device_register(&tdmb_device);

	if (spi_register_board_info(tegra_spi_tdmb_devices, ARRAY_SIZE(tegra_spi_tdmb_devices)) != 0) {
		pr_err("%s: spi_register_board_info returned error\n", __func__);
	}
	return 0;
}
#endif

void p3_stmpe1801_gpio_setup_board(void)
{
	p3_jack_init();
	sec_s5k5ccgx_init();
	p3_usb_gpio_init();
}

static void p3_power_off(void)
{
	int ret;
	u32 value;

	/* control modem power off before pmic control */
	gpio_set_value(GPIO_RESET_REQ_N, 0);
	udelay(500);	/* min 300us */
	gpio_set_value(GPIO_CP_RST, 0);
	gpio_set_value(GPIO_CP_ON, 0);
	mdelay(50);

	/* prevent leakage current after power off */
	if (system_rev >= 9)
		gpio_set_value(GPIO_ACC_EN, 0);
	mdelay(50);

	value = gpio_get_value(GPIO_TA_nCONNECTED);
	if(!value) {
		pr_debug("%s: TA_nCONNECTED! Reset!\n", __func__);
		ret = tps6586x_soft_rst();
		if (ret)
			pr_err("p3: failed to tps6586x_soft_rst(ret:%d)\n", ret);
	} else {
		ret = tps6586x_power_off();
		if (ret)
			pr_err("p3: failed to power off(ret:%d)\n", ret);
	}

	mdelay(1000);
	pr_alert("p3: system halted.\n");
	while (1)
		;

}

static void __init p3_power_off_init(void)
{
	pm_power_off = p3_power_off;
}

static void __init p3_reserve(void)
{
	u64 ram_console_start;
	int ret;

	if (memblock_reserve(0x0, 4096) < 0)
		pr_warn("Cannot reserve first 4k of memory for safety\n");

	/* Reserve memory for the ram console at the end of memory. */
	ram_console_start = memblock_end_of_DRAM() - SZ_1M;

	ret = memblock_remove(ram_console_start, SZ_1M);

	if (ret < 0) {
		pr_err("Failed to reserve 0x%x bytes for ram_console at "
			"0x%llx, err = %d.\n",
			SZ_1M, ram_console_start, ret);
		return;
	}

	ram_console_resource[0].start = ram_console_start;
	ram_console_resource[0].end = ram_console_start + SZ_1M - 1;

	tegra_reserve(SZ_256M, SZ_8M, SZ_16M);

}

#ifdef CONFIG_KERNEL_DEBUG_SEC
static ssize_t show_sec_debug_level(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	int sec_debug_level = kernel_sec_get_debug_level();
	char buffer[5];
	memcpy(buffer, &sec_debug_level, 4);
	buffer[4] = '\0';
	return sprintf(buf, "%s\n", buffer);
}

static ssize_t store_sec_debug_level(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,
				  size_t count)
{
	int sec_debug_level = 0;
	memcpy(&sec_debug_level, buf, 4);

	printk("%s %x\n", __func__, sec_debug_level);

	if (!(sec_debug_level == KERNEL_SEC_DEBUG_LEVEL_LOW
			|| sec_debug_level == KERNEL_SEC_DEBUG_LEVEL_MID
			|| sec_debug_level == KERNEL_SEC_DEBUG_LEVEL_HIGH))
		return -EINVAL;

	kernel_sec_set_debug_level(sec_debug_level);

	return count;
}

static DEVICE_ATTR(sec_debug_level, 0644, show_sec_debug_level, store_sec_debug_level);
#endif /*CONFIG_KERNEL_DEBUG_SEC*/

static void __init tegra_p3_init(void)
{
	char serial[20];
	int ret = 0;

	pr_info("P5 board revision = %d(0x%02x)\n", system_rev, system_rev);
	tegra_common_init();
	tegra_clk_init_from_table(p3_clk_init_table);
	p3_pinmux_init();
	p3_i2c_init();

	snprintf(serial, sizeof(serial), "%llX", tegra_chip_uid());
#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
	android_usb_pdata.serial_number = kstrdup(serial, GFP_KERNEL);
#else
	andusb_plat.serial_number = kstrdup(serial, GFP_KERNEL);
#endif

	if (system_rev < 6)
		acc_con_pdata.hdmi_hpd_gpio = GPEX_GPIO_P1;
	if (system_rev < 6)
		p3_battery_platform.charger.currentset_line = GPEX_GPIO_P5;
#if defined(CONFIG_SEC_KEYBOARD_DOCK)
	if (system_rev < 6)
		kbd_pdata.gpio_accessory_enable = GPEX_GPIO_P8;
#endif
	tegra_i2s_device1.dev.platform_data = &tegra_audio_pdata[0];
	tegra_i2s_device2.dev.platform_data = &tegra_audio_pdata[1];
	tegra_spdif_device.dev.platform_data = &tegra_spdif_pdata;
	tegra_das_device.dev.platform_data = &tegra_das_pdata;
	platform_add_devices(p3_devices, ARRAY_SIZE(p3_devices));

#if defined(CONFIG_SEC_KEYBOARD_DOCK)
#if defined(CONFIG_MACH_SAMSUNG_P5WIFI)
	if (system_rev != 0x6)
		platform_device_register(&sec_keyboard);
#else
	platform_device_register(&sec_keyboard);
#endif	/* CONFIG_MACH_SAMSUNG_P5WIFI */
#endif	/* CONFIG_SEC_KEYBOARD_DOCK */

#if defined(CONFIG_MACH_SAMSUNG_P5WIFI)
	if (system_rev > 6){
		pr_err("this is  testmode support uart for wifi version\n");
		pr_err("check your harware\n");
		pr_err("if your hardware is wifi rev07, that's okay\n");
		platform_add_devices(uart_wifi_devices, ARRAY_SIZE(uart_wifi_devices));
	}
#endif

	sec_class = class_create(THIS_MODULE, "sec");
	if (IS_ERR(sec_class))
		pr_err("Failed to create sec class!\n");

//	p3_jack_init();
	p3_sdhci_init();
	p3_gpio_i2c_init();
	p3_camera_init();
	p3_regulator_init();

#if defined(CONFIG_TOUCHSCREEN_MELFAS)
#ifdef CONFIG_SAMSUNG_LPM_MODE
       /* touch not operate in LPM mode (low power mode charging)*/
	if (!charging_mode_from_boot) {
		touch_init();
	} else {
		touch_init_hw();
	}
#else
	touch_init();
#endif
#endif

	p3_keys_init();
#if defined(CONFIG_SEC_MODEM)
#ifdef CONFIG_SAMSUNG_LPM_MODE
	if (!charging_mode_from_boot)
		register_smd_resource();
#else
	register_smd_resource();
#endif
#endif
	p3_usb_init();
	p3_gps_init();
	p3_panel_init();
	p3_sensors_init();
	p3_power_off_init();
	p3_emc_init();
	p3_rfkill_init();
	p3_bt_lpm_init();
	p3_wlan_gpio_config();
#if defined(CONFIG_TOUCHSCREEN_WACOM_G5)
	p3_digitizer_init();
#endif



#if defined(CONFIG_MACH_SAMSUNG_P5LTE)
	p3_lte_pmic_init();
#endif
#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
/* soonyong.cho : This is for setting unique serial number */
	s3c_usb_set_serial();
/* Changes value of nluns in order to use external storage */
	usb_device_init();
#endif
#if defined(CONFIG_TDMB) || defined(CONFIG_TDMB_MODULE)
  	p5_tdmb_init();
#endif

	register_reboot_notifier(&p3_reboot_notifier);

#if 0   /* disable for now, HQ says it destablilizes the runtime */
	/* don't allow console to suspend so we get extra logging
	 * during power management activities
	 */
	console_suspend_enabled = 0;
#endif

#ifdef CONFIG_KERNEL_DEBUG_SEC
	{
		/* Add debug level node */
		struct device *platform = p3_devices[0]->dev.parent;
		ret = device_create_file(platform, &dev_attr_sec_debug_level);
		if (ret)
			printk("Fail to create sec_debug_level file\n");
	}
#endif /*CONFIG_KERNEL_DEBUG_SEC*/

}

#ifdef CONFIG_TARGET_LOCALE_KOR
MACHINE_START(SAMSUNG_P3, MODELNAME)
    .boot_params    = 0x00000100,
    .phys_io        = IO_APB_PHYS,
    .io_pg_offst    = ((IO_APB_VIRT) >> 18) & 0xfffc,
    .init_irq       = tegra_init_irq,
    .init_machine   = tegra_p3_init,
    .map_io         = tegra_map_common_io,
    .reserve        = p3_reserve,
    .timer          = &tegra_timer,
MACHINE_END
#else
MACHINE_START(SAMSUNG_P3, "p3")
	.boot_params    = 0x00000100,
	.phys_io        = IO_APB_PHYS,
	.io_pg_offst    = ((IO_APB_VIRT) >> 18) & 0xfffc,
	.init_irq       = tegra_init_irq,
	.init_machine   = tegra_p3_init,
	.map_io         = tegra_map_common_io,
	.reserve        = p3_reserve,
	.timer          = &tegra_timer,
MACHINE_END
#endif
