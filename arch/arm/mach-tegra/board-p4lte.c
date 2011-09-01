/*
 * arch/arm/mach-tegra/board-p4.c
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
#include "board-p4lte.h"
#include "devices.h"
#include "fuse.h"
#include "wakeups-t2.h"
#include <media/s5k5bbgx.h>
#include <media/s5k5ccgx.h>
#include "../../../drivers/samsung/lte_bootloader/lte_modem_bootloader.h"

#ifdef CONFIG_SAMSUNG_LPM_MODE
#include <linux/moduleparam.h>
#endif

#ifdef CONFIG_KERNEL_DEBUG_SEC
#include <linux/kernel_sec_common.h>
#endif

#if defined(CONFIG_SEC_KEYBOARD_DOCK)
#include <linux/sec_keyboard_struct.h>
#endif

#ifdef CONFIG_USB_ANDROID_ACCESSORY
#include <linux/usb/f_accessory.h>
#endif

struct class *sec_class;
EXPORT_SYMBOL(sec_class);

struct board_revision {
	unsigned int value;
	unsigned int gpio_value;
	char string[20];
};

/* We'll enumerate board revision from 10
 * to avoid a confliction with revision numbers of P3
*/
struct board_revision p4_board_rev[] = {
	{ 10, 0x0C, ".Rev00" },
	{ 10, 0x0D, ".Rev01" },
	{ 10, 0x0E, ".EMUL Rev00" },
	{ 10, 0x0F, ".Rev02" },
	{ 10, 0x10, ".Rev03" },
	{ 10, 0x11, ".Rev04" },
	{ 10, 0x12, ".Rev05" },
	{ 10, 0x13, ".Rev06" },
	{ 10, 0x14, ".Rev07" },
	{ 10, 0x15, ".Rev08" },
	{ 10, 0x16, "Rev04" },
	{ 10, 0x17, "Rev05" },
	{ 11, 0x18, "Rev06" }, /*P4 rev01*/
	{ 12, 0x19, "Rev07" },
	{ 13, 0x1A, "Rev08" },
	{ 14, 0x1B, "Rev09" },
	{ 15, 0x1C, "Rev10" },
	{ 16, 0x1D, "Rev11" },
	{ 17, 0x1E, "Rev12" },
};

struct board_usb_data {
	struct mutex ldo_en_lock;
	int usb_regulator_on[3];
};

static struct board_usb_data usb_data;
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
static struct mxt_callbacks *charger_callbacks;

/*Check ADC value to select headset type*/
extern s16 adc_get_value(u8 channel);
extern s16 stmpe811_adc_get_value(u8 channel);
extern void p3_set_usb_path(usb_path_type usb_path);
extern usb_path_type usb_sel_status;
extern int __init register_smd_resource(void);

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
		kernel_sec_clear_upload_magic_number();
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
//#if 0
	to_io = ioremap(BOOT_MODE_P_ADDR, 4);
	writel((unsigned long)boot_mode, to_io);
	iounmap(to_io);
//#endif
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

        if (system_rev > 0x0A)
            value = gpio_get_value(GPIO_TA_nCONNECTED);
        else
            value = gpio_get_value(GPIO_TA_nCONNECTED_REV05);

	if (code == SYS_RESTART) {
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

	tegra_gpio_enable(GPIO_BT_HOST_WAKE);
	platform_device_register(&p3_bcm4330_rfkill_device);

	return 0;
}

/* UART Interface for Bluetooth */

static struct tegra_uart_platform_data bt_uart_pdata = {
	.wake_peer = p3_bt_uart_wake_peer,
};

static struct resource tegra_uartc_resources[] = {
	[0] = {
		.start	= TEGRA_UARTC_BASE,
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
	{ "pwm",	"clk_32k",	32768,		false},
	{ "pll_c",	"clk_m",	586000000,	true},
	{ "pll_a",	NULL,		11289600,	true},
	{ "pll_a_out0",	NULL,		11289600,	true},
	{ "clk_dev1",   "pll_a_out0",   0,              true},
	{ "i2s1",	"pll_a_out0",	11289600,	true},
	{ "i2s2",	"pll_a_out0",	11289600,	true},
	{ "audio",	"pll_a_out0",	11289600,	true},
	{ "audio_2x",	"audio",	22579200,	true},
	{ "spdif_out",	"pll_a_out0",	5644800,	false},
	{ "vde",	"pll_m",	240000000,	false},
	{ NULL,		NULL,		0,		0},
};

#ifndef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
static char *usb_functions[] = { "mtp", "diag" };
static char *usb_functions_adb[] = { "mtp", "adb", "diag" };
static char *usb_functions_rndis[] = { "rndis","diag" };
static char *usb_functions_rndis_adb[] = { "rndis","adb","diag" };

static char *usb_functions_all[] = { "rndis", "mtp", "adb", "diag" };


static struct android_usb_product usb_products[] = {
	{
		.product_id     = 0x6860,
		.num_functions  = ARRAY_SIZE(usb_functions),
		.functions      = usb_functions,
	},
	{
		.product_id     = 0x6860,
		.num_functions  = ARRAY_SIZE(usb_functions_adb),
		.functions      = usb_functions_adb,
	},
	{
		.product_id     = 0x6862, //0x68C4,//0x6863,
		.num_functions  = ARRAY_SIZE(usb_functions_rndis),
		.functions      = usb_functions_rndis,
	},
	{
		.product_id     = 0x6862, //0x6864,
		.num_functions  = ARRAY_SIZE(usb_functions_rndis_adb),
		.functions      = usb_functions_rndis_adb,
	},
};

/* standard android USB platform data */
static struct android_usb_platform_data andusb_plat = {
	.vendor_id              = 0x04e8,
	.product_id             = 0x6860,//0x68C4,//0x6860,
	.manufacturer_name      = "SAMSUNG",
	.product_name           = "SCH-I905",
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

static char *usb_functions_rndis_diag[] = {
	"rndis",
	"diag",
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
static char *usb_functions_diag_adb[] = {
//	"mtp",
	"diag",
	"adb",
};
static char *usb_functions_mtp_diag[] = {
	"mtp",
	"diag",
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
	//"usb_mass_storage",
	//"acm",
#    ifndef CONFIG_USB_ANDROID_SAMSUNG_KIES_UMS
	"mtp",
#    endif
	"diag",
	"rndis",
	"adb",
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
		.product_id	= SAMSUNG_P4LTE_DEBUG_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_diag_adb),
		.functions	= usb_functions_diag_adb,
		.bDeviceClass	= 0xEF,
		.bDeviceSubClass= 0x02,
		.bDeviceProtocol= 0x01,
		.s		= ANDROID_P4LTE_DEBUG_CONFIG_STRING,
		.mode		= USBSTATUS_ADB,
	},
	{
		.product_id	= SAMSUNG_P4LTE_KIES_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_mtp_diag),
		.functions	= usb_functions_mtp_diag,
		.bDeviceClass	= 0xEF,
		.bDeviceSubClass= 0x02,
		.bDeviceProtocol= 0x01,
		.s		= ANDROID_P4LTE_KIES_CONFIG_STRING,
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
		.product_id	= SAMSUNG_P4LTE_RNDIS_DIAG_PRODUCT_ID,
		.num_functions	= ARRAY_SIZE(usb_functions_rndis_diag),
		.functions	= usb_functions_rndis_diag,
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
	.vendorID       = S3C_VENDOR_ID,
	.vendorDescr    = "Samsung",
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
		"Board P3 : Enterring sec_jack_set_micbias_state = %d\n", on);
	if (system_rev < 0x3)
		gpio_set_value(TEGRA_GPIO_PH3, on);
	else
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
	.bus_clk_rate	= { 100000, 10000 },
	.bus_mux	= { &i2c2_ddc, &i2c2_gen2 },
	.bus_mux_len	= { 1, 1 },
};

static struct tegra_i2c_platform_data p3_i2c3_platform_data = {
	.adapter_nr	= 3,
	.bus_count	= 1,
	.bus_clk_rate	= { 400000, 0 },
};

static struct tegra_i2c_platform_data p3_dvc_platform_data = {
	.adapter_nr	= 4,
	.bus_count	= 1,
	.bus_clk_rate	= { 100000, 0 },
	.is_dvc		= true,
};

static struct tegra_audio_platform_data tegra_audio_pdata[] = {
	/* For I2S1 */
	[0] = {
		.i2s_master	= true,
		.dma_on		= true,  /* use dma by default */
		.i2s_master_clk = 44100,
		.i2s_clk_rate	= 2822400,
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
				[0] = {tegra_das_port_i2s1,
				       tegra_das_port_dap1, true},
				[1] = {tegra_das_port_dap1,
				       tegra_das_port_i2s1, false},
				[2] = {tegra_das_port_i2s2,
				       tegra_das_port_dap4, true},
				[3] = {tegra_das_port_dap4,
				       tegra_das_port_i2s2, false},
			},
		},
	}
};

static void p3_i2c_init(void)
{
         if(system_rev > 0xA){
		tegra_i2c_device1.dev.platform_data = &p3_i2c1_platform_data;
         }
	tegra_i2c_device2.dev.platform_data = &p3_i2c2_platform_data;
	tegra_i2c_device3.dev.platform_data = &p3_i2c3_platform_data;
	tegra_i2c_device4.dev.platform_data = &p3_dvc_platform_data;

         if(system_rev > 0xA){
		platform_device_register(&tegra_i2c_device1);
         }
	platform_device_register(&tegra_i2c_device2);
	platform_device_register(&tegra_i2c_device3);
	platform_device_register(&tegra_i2c_device4);
}

#define LTE_MODEM_SPI_BUS_NUM	2
#define LTE_MODEM_SPI_CS		1
#define LTE_MODEM_SPI_MAX_HZ	10000000 //20000000

struct lte_modem_bootloader_platform_data lte_modem_bootloader_pdata = {
	.name = LTE_MODEM_BOOTLOADER_DRIVER_NAME,
	.gpio_lte2ap_status = GPIO_LTE2AP_STATUS,
};

static struct spi_board_info p3_lte_modem[] __initdata = {
	{
		.modalias = LTE_MODEM_BOOTLOADER_DRIVER_NAME,
		.platform_data = &lte_modem_bootloader_pdata,
		.max_speed_hz = LTE_MODEM_SPI_MAX_HZ,
		.bus_num = LTE_MODEM_SPI_BUS_NUM,
		.chip_select = LTE_MODEM_SPI_CS,
		.mode = SPI_MODE_0,
	},
};

static void p3_lte_modem_bootloader_init()
{
	spi_register_board_info(p3_lte_modem, ARRAY_SIZE(p3_lte_modem));
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

	return status & TEGRA_WAKE_GPIO_PS4 ? KEY_POWER : KEY_RESERVED;
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

	pr_info("Board P4 : %s=%d instance=%d present regulator=%d\n",
		 __func__, active, instance, usb_data.usb_regulator_on[instance]);
	mutex_lock(&usb_data.ldo_en_lock);

	if (active) {
		if (!usb_data.usb_regulator_on[instance]) {
			do {
				ret = regulator_enable(reg);
				if (ret == 0)
					break;
				msleep(3);
			} while(try_times--);
			if (ret == 0)
				usb_data.usb_regulator_on[instance] = 1;
			else
				pr_err("%s: failed to turn on \
					vdd_ldo6 regulator\n", __func__);
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
	pr_info("Board P4 : %s = %d\n", __func__, active);
}

void tegra_acc_power(u8 token, bool active)
{
	int gpio_acc_en;
	int gpio_acc_5v;
	int try_cnt = 0;
	static bool enable = false;
	static u8 acc_en_token = 0;

	gpio_acc_en = GPIO_ACCESSORY_EN;

	if(system_rev > 0x0A)
		gpio_acc_5v = GPIO_V_ACCESSORY_5V;
	else
		gpio_acc_5v = GPIO_V_ACCESSORY_5V_REV05;

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
	[0] = KEY_VOLUMEUP,
	[1] = KEY_VOLUMEDOWN,
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

	if (system_rev < 0x02) {
		for (count = 0; count < 2; count++)
			sum += adc_get_value(1);

		vol_1 = sum / 2;
		pr_debug("%s: samsung_charger_adc = %d !!\n", __func__, vol_1);

		if ((vol_1 > 900) && (vol_1 < 1300))
			result = true;
		else
			result = false;
	} else {
		for (count = 0; count < 2; count++)
			sum += stmpe811_adc_get_value(6);

		vol_1 = sum / 2;
		pr_debug("%s: samsung_charger_adc = %d !!\n", __func__, vol_1);

		if ((vol_1 > 800) && (vol_1 < 1800))
			result = true;
		else
			result = false;
	}

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

        if(system_rev > 0x0A){
            gpio_request(GPIO_TA_nCONNECTED, "GPIO_TA_nCONNECTED");
            gpio_direction_input(GPIO_TA_nCONNECTED);
            tegra_gpio_enable(GPIO_TA_nCONNECTED);
        }
        else{
            gpio_request(GPIO_TA_nCONNECTED_REV05, "GPIO_TA_nCONNECTED");
            gpio_direction_input(GPIO_TA_nCONNECTED_REV05);
            tegra_gpio_enable(GPIO_TA_nCONNECTED_REV05);
        }

	gpio_request(GPIO_TA_nCHG, "GPIO_TA_nCHG");
	gpio_direction_input(GPIO_TA_nCHG);
	tegra_gpio_enable(GPIO_TA_nCHG);

	gpio_request(GPIO_CURR_ADJ, "GPIO_CURR_ADJ");
	if (check_samsung_charger() == 1)
		gpio_direction_output(GPIO_CURR_ADJ, 1);
	else
		gpio_direction_output(GPIO_CURR_ADJ, 0);
	tegra_gpio_enable(GPIO_CURR_ADJ);

	gpio_request(GPIO_FUEL_ALRT, "GPIO_FUEL_ALRT");
	gpio_direction_input(GPIO_FUEL_ALRT);
	tegra_gpio_enable(GPIO_FUEL_ALRT);

	pr_info("Battery GPIO initialized.\n");

}

static void  p3_inform_charger_connection(int mode)
{
	if (charger_callbacks && charger_callbacks->inform_charger)
		charger_callbacks->inform_charger(charger_callbacks, mode);
};

static struct p3_battery_platform_data p3_battery_platform = {
	.charger = {
		.enable_line = GPIO_TA_EN,
		.connect_line = GPIO_TA_nCONNECTED,
		.fullcharge_line = GPIO_TA_nCHG,
		.currentset_line = GPIO_CURR_ADJ,
	},
	.check_dedicated_charger = check_samsung_charger,
	.init_charger_gpio = p3_bat_gpio_init,
	.inform_charger_connection = p3_inform_charger_connection,
	.temp_high_threshold = 48000,	/* 48c */
	.temp_high_recovery = 44000,	/* 44c */
	.temp_low_recovery = 1000,		/* 1c */
	.temp_low_threshold = -3000,		/* -3c */
#ifdef CONFIG_MACH_SAMSUNG_P4LTE
	.temp_high_threshold_lpm = 48000,	/* 48c */
	.temp_high_recovery_lpm = 44000,	/* 44c */
	.temp_low_recovery_lpm = 0,		/* 0c */
	.temp_low_threshold_lpm = -4000,	/* -4c */
#endif
	.charge_duration = 10*60*60,	/* 10 hour */
	.recharge_duration = 1.5*60*60,	/* 1.5 hour */
	.recharge_voltage = 4150,	/*4.15V */
};

static struct platform_device p3_battery_device = {
	.name = "p3-battery",
	.id = -1,
	.dev = {
		.platform_data = &p3_battery_platform,
	},
};

static struct p3_battery_platform_data p3_battery_platform_rev05 = {
	.charger = {
		.enable_line = GPIO_TA_EN,
		.connect_line = GPIO_TA_nCONNECTED_REV05,
		.fullcharge_line = GPIO_TA_nCHG,
		.currentset_line = GPIO_CURR_ADJ,
	},
	.check_dedicated_charger = check_samsung_charger,
	.init_charger_gpio = p3_bat_gpio_init,
	.inform_charger_connection = p3_inform_charger_connection,
	.temp_high_threshold = 48000,	/* 48c */
	.temp_high_recovery = 44000,	/* 44c */
	.temp_low_recovery = 1000,		/* 1c */
	.temp_low_threshold = -3000,		/* -3c */
#ifdef CONFIG_MACH_SAMSUNG_P4LTE
	.temp_high_threshold_lpm = 48000,	/* 48c */
	.temp_high_recovery_lpm = 44000,	/* 44c */
	.temp_low_recovery_lpm = 0,		/* 0c */
	.temp_low_threshold_lpm = -4000,	/* -4c */
#endif
	.charge_duration = 10*60*60,	/* 10 hour */
	.recharge_duration = 1.5*60*60,	/* 1.5 hour */
	.recharge_voltage = 4150,	/*4.15V */
};

static struct platform_device p3_battery_device_rev05 = {
	.name = "p3-battery",
	.id = -1,
	.dev = {
		.platform_data = &p3_battery_platform_rev05,
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

static int dock_wakeup(void)
{
	unsigned long status =
		readl(IO_ADDRESS(TEGRA_PMC_BASE) + PMC_WAKE_STATUS);

	if (status & TEGRA_WAKE_GPIO_PI5) {
		writel(TEGRA_WAKE_GPIO_PI5,
			IO_ADDRESS(TEGRA_PMC_BASE) + PMC_WAKE_STATUS);
	}

	return status & TEGRA_WAKE_GPIO_PI5 ? KEY_WAKEUP : KEY_RESERVED;
}

static struct dock_keyboard_platform_data kbd_pdata = {
	.acc_power = tegra_acc_power,
	.wakeup_key = dock_wakeup,
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
		/* adc == 0, 3 pole */
		.adc_high = 0,
		.delay_ms = 0,
		.check_count = 0,
		.jack_type = SEC_HEADSET_3POLE,
	},
	{
		/* 0 < adc <= 600, unstable zone, default to 3pole if it stays
		 * in this range for a 200ms (20ms delays, 10 samples)
		 */
		.adc_high = 600,
		.delay_ms = 20,
		.check_count = 10,
		.jack_type = SEC_HEADSET_3POLE,
	},
	{
		/* 600 < adc <= 2000, unstable zone, default to 4pole if it
		 * stays in this range for 200ms (20ms delays, 10 samples)
		 */
		.adc_high = 2000,
		.delay_ms = 20,
		.check_count = 10,
		.jack_type = SEC_HEADSET_4POLE,
	},
	{
		/* 2000 < adc <= 3800, 4 pole zone */
		// as following new schemetics, margin is gone to 3800 instead of 3700
		// 2011.05.26 by Rami Jung
		.adc_high = 3800,
		.delay_ms = 0,
		.check_count = 0,
		.jack_type = SEC_HEADSET_4POLE,
	},
	{
		/* adc > 3800, unstable zone, default to 3pole if it stays
		 * in this range for a second (10ms delays, 100 samples)
		 */
		.adc_high = 0x7fffffff,
		.delay_ms = 10,
		.check_count = 100,
		.jack_type = SEC_HEADSET_3POLE,
	},
};

/* To support 3-buttons earjack */
static struct sec_jack_buttons_zone sec_jack_buttons_zones[] = {
	{
		/* 0 <= adc <= 150, stable zone */
		.code		= KEY_MEDIA,
		.adc_low	= 0,
		.adc_high	= 150,
	},
	{
		/* 180 <= adc <= 340, stable zone */
		.code		= KEY_VOLUMEUP,
		.adc_low	= 180,
		.adc_high	= 340,
	},
	{
		/* 400 <= adc <= 720, stable zone */
		.code		= KEY_VOLUMEDOWN,
		.adc_low	= 400,
		.adc_high	= 720,
	},
};

static int sec_jack_get_adc_value(void)
{
	s16 ret;
	if (system_rev < 0x2)
		ret = adc_get_value(0);
	else
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

#ifdef CONFIG_SAMSUNG_PHONE_TTY
static struct platform_device sec_cdma_dpram = {
        .name = "dpram-device",
        .id = -1,
};
#endif

static struct platform_device *p3_devices[] __initdata = {
	&androidusb_device,
#ifndef CONFIG_USB_ANDROID_RNDIS
	&p3_rndis_device,
#else
	&s3c_device_rndis,
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
#if defined(CONFIG_SEC_KEYBOARD_DOCK)
	&sec_keyboard,
#endif
#ifdef CONFIG_30PIN_CONN
	&sec_device_connector,
#endif
	&tegra_das_device,
	&ram_console_device,
#ifdef CONFIG_SAMSUNG_PHONE_TTY
    &sec_cdma_dpram,
#endif
    &tegra_spi_device3,
};

static struct platform_device *p3_devices_rev05[] __initdata = {
	&androidusb_device,
#ifndef CONFIG_USB_ANDROID_RNDIS
	&p3_rndis_device,
#else
	&s3c_device_rndis,
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
	&p3_battery_device_rev05,
	&tegra_i2s_device1,
	&tegra_spdif_device,
	&tegra_avp_device,
	&tegra_camera,
	&sec_device_jack,
#if defined(CONFIG_SEC_KEYBOARD_DOCK)
	&sec_keyboard,
#endif
#ifdef CONFIG_30PIN_CONN
	&sec_device_connector,
#endif
	&tegra_das_device,
	&ram_console_device,
#ifdef CONFIG_SAMSUNG_PHONE_TTY
    &sec_cdma_dpram,
#endif
    &tegra_spi_device3,
};


static void p3_keys_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(p3_keys); i++)
		tegra_gpio_enable(p3_keys[i].gpio);
}

/*p3 touch : atmel_mxt1386*/
static void p3_touch_init_hw(void)
{
	pr_info("p3_touch_init_hw\n");
    if(system_rev < 0x0B)
  	  gpio_request(GPIO_TOUCH_EN_REV05, "TOUCH_EN");
	gpio_request(GPIO_TOUCH_RST, "TOUCH_RST");
	gpio_request(GPIO_TOUCH_INT, "TOUCH_INT");

    if(system_rev < 0x0B)
	  gpio_direction_output(GPIO_TOUCH_EN_REV05, 1);
	gpio_direction_output(GPIO_TOUCH_RST, 1);
	gpio_direction_input(GPIO_TOUCH_INT);

    if(system_rev < 0x0B)
	  tegra_gpio_enable(GPIO_TOUCH_EN_REV05);
	tegra_gpio_enable(GPIO_TOUCH_RST);
	tegra_gpio_enable(GPIO_TOUCH_INT);
}

static void sec_s5k5ccgx_init(void)
{
	printk("%s,, \n",__func__);

	tegra_gpio_enable(GPIO_CAM_PMIC_EN1);	//3M_CORE_1.2V
	tegra_gpio_enable(GPIO_CAM_PMIC_EN2);	//CAM_AVDD2.8V
	tegra_gpio_enable(GPIO_CAM_PMIC_EN3);	//2M_DVDD_1.8V
	tegra_gpio_enable(GPIO_CAM_PMIC_EN4);	//CAM_IO_1.8V
	tegra_gpio_enable(GPIO_CAM_R_nRST);		//3M nRST
	tegra_gpio_enable(GPIO_CAM_R_nSTBY);		//3M STBY
//temp	tegra_gpio_enable(GPIO_CAM_MOVIE_EN);	//flash ??
	tegra_gpio_enable(GPIO_CAM_FLASH_SET);	//flash ??

	gpio_request(GPIO_CAM_PMIC_EN1, "CAMERA_PMIC_EN1");
	gpio_request(GPIO_CAM_PMIC_EN2, "CAMERA_PMIC_EN2");
	gpio_request(GPIO_CAM_PMIC_EN3, "CAMERA_PMIC_EN3");
	gpio_request(GPIO_CAM_PMIC_EN4, "CAMERA_PMIC_EN4");
	gpio_request(GPIO_CAM_R_nRST, "CAMERA_CAM_Left_nRST");
	gpio_request(GPIO_CAM_R_nSTBY, "CAMERA_CAM_nSTBY");
        if(system_rev > 0x0A)
            gpio_request(GPIO_CAM_FLASH_EN, "CAM_FLASH_EN");
        else
            gpio_request(GPIO_CAM_FLASH_EN_REV05, "CAM_FLASH_EN");
	gpio_request(GPIO_CAM_FLASH_SET, "CAM_FLASH_SET");

	gpio_direction_output(GPIO_CAM_PMIC_EN1, 0);
	gpio_direction_output(GPIO_CAM_PMIC_EN2, 0);
	gpio_direction_output(GPIO_CAM_PMIC_EN3, 0);
	gpio_direction_output(GPIO_CAM_PMIC_EN4, 0);
	gpio_direction_output(GPIO_CAM_R_nRST, 0);
	gpio_direction_output(GPIO_CAM_R_nSTBY, 0);
        if(system_rev > 0x0A)
            gpio_direction_output(GPIO_CAM_FLASH_EN, 0);
        else
            gpio_direction_output(GPIO_CAM_FLASH_EN_REV05, 0);
	gpio_direction_output(GPIO_CAM_FLASH_SET, 0);

#if 0
	printk("<=PCAM=> LOW 1: %d\n",  gpio_get_value(GPIO_CAM_PMIC_EN1));
	printk("<=PCAM=> LOW 2: %d\n",  gpio_get_value(GPIO_CAM_PMIC_EN2));
	printk("<=PCAM=> LOW 4: %d\n",  gpio_get_value(GPIO_CAM_PMIC_EN4));

	gpio_direction_output(GPIO_CAM_PMIC_EN1, 1);
	gpio_direction_output(GPIO_CAM_PMIC_EN2, 1);
	gpio_direction_output(GPIO_CAM_PMIC_EN4, 1);
	mdelay(10);

	printk("<=PCAM=> HIGH 1: %d\n",  gpio_get_value(GPIO_CAM_PMIC_EN1));
	printk("<=PCAM=> HIGH 2: %d\n",	gpio_get_value(GPIO_CAM_PMIC_EN2));
	printk("<=PCAM=> HIGH 4: %d\n",	gpio_get_value(GPIO_CAM_PMIC_EN4));

	gpio_direction_output(GPIO_CAM_PMIC_EN1, 0);
	gpio_direction_output(GPIO_CAM_PMIC_EN2, 0);
	gpio_direction_output(GPIO_CAM_PMIC_EN4, 0);
	mdelay(10);

	printk("<=PCAM=> LOW 1: %d\n",	gpio_get_value(GPIO_CAM_PMIC_EN1));
	printk("<=PCAM=> LOW 2: %d\n",	gpio_get_value(GPIO_CAM_PMIC_EN2));
	printk("<=PCAM=> LOW 4: %d\n",	gpio_get_value(GPIO_CAM_PMIC_EN4));
#endif
}

struct tegra_pingroup_config mclk = {
	TEGRA_PINGROUP_CSUS,
	TEGRA_MUX_VI_SENSOR_CLK,
	TEGRA_PUPD_PULL_DOWN,
	TEGRA_TRI_TRISTATE
};

/*static void P3_s5k5ccgx_flash_on()
{
	//gpio_set_value(GPIO_CAM_FLASH_SET, 1);
	gpio_set_value(GPIO_CAM_FLASH_SET, 1);

}

static void P3_s5k5ccgx_flash_off()
{
	//gpio_set_value(GPIO_CAM_FLASH_SET, 0);
	gpio_set_value(GPIO_CAM_FLASH_SET, 0);
}*/

static void p3_s5k5ccgx_power_on()
{
    printk("%s,, \n",__func__);
    gpio_set_value(GPIO_CAM_R_nSTBY, 0); //3M STBY low
    gpio_set_value(GPIO_CAM_R_nRST, 0); //3M nRST low
    gpio_set_value(GPIO_CAM_F_nSTBY, 0); // 2M STBY low
    gpio_set_value(GPIO_CAM_F_nRST, 0); // 2M nRST low
    gpio_set_value(GPIO_CAM_PMIC_EN1, 0);
    gpio_set_value(GPIO_CAM_PMIC_EN2, 0);
    gpio_set_value(GPIO_CAM_PMIC_EN3, 0);
    gpio_set_value(GPIO_CAM_PMIC_EN4, 0);
    udelay(100);
    gpio_set_value(GPIO_CAM_PMIC_EN1, 1); // 3M_CORE_1.2V, 3M_AF_2.8V
    udelay(100);
    gpio_set_value(GPIO_CAM_PMIC_EN2, 1); // CAM_AVDD2.8V
    udelay(100);
    gpio_set_value(GPIO_CAM_PMIC_EN3, 1); // 2M_DVDD_1.8V
    udelay(100);
    gpio_set_value(GPIO_CAM_PMIC_EN4, 1); // CAM_IO_1.8V
    udelay(100);

    tegra_pinmux_set_func(&mclk);
    tegra_pinmux_set_tristate(TEGRA_PINGROUP_CSUS, TEGRA_TRI_NORMAL);
    udelay(100);

    gpio_set_value(GPIO_CAM_R_nSTBY, 1); //3M STBY high
    udelay(200);

    gpio_set_value(GPIO_CAM_R_nRST, 1); //3M nRST high
    msleep(10);
}


static void p3_s5k5ccgx_power_off()
{
    printk("%s,, \n",__func__);
    msleep(3);
    gpio_set_value(GPIO_CAM_R_nRST, 0); //3M nRST Low
    udelay(100);

    tegra_pinmux_set_tristate(TEGRA_PINGROUP_CSUS, TEGRA_TRI_TRISTATE);
    udelay(100);

    gpio_set_value(GPIO_CAM_R_nSTBY, 0); //3M STBY Low
    udelay(100);

    gpio_set_value(GPIO_CAM_PMIC_EN4, 0);// CAM_IO_1.8V
    udelay(100);
    gpio_set_value(GPIO_CAM_PMIC_EN3, 0); // 2M_DVDD_1.8V
    udelay(100);
    gpio_set_value(GPIO_CAM_PMIC_EN2, 0);// CAM_AVDD2.8V
    udelay(100);
    gpio_set_value(GPIO_CAM_PMIC_EN1, 0);// 3M_CORE_1.2V, 3M_AF_2.8V
    msleep(800);
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
		gpio_set_value(GPIO_CAM_FLASH_SET, 0);
		udelay(FLASH_TIME_EN_SET_US);
		gpio_set_value(GPIO_CAM_FLASH_SET, 1);
		udelay(FLASH_TIME_EN_SET_US);
	}
	udelay(FLASH_TIME_LATCH_US);
	/* At this point, the LED will be on */
}

static int P3_s5k5ccgx_flash(int enable)
{
	/* Turn main flash on or off by asserting a value on the EN line. */
	printk("========== flash enable = %d \n", enable);
        if(system_rev > 0x0A)
            gpio_set_value(GPIO_CAM_FLASH_EN, enable);
        else
            gpio_set_value(GPIO_CAM_FLASH_EN_REV05, enable);
	return 0;
}

static int P3_s5k5ccgx_af_assist(int enable)
{
        /* Turn assist light on or off by asserting a value on the EN_SET
        * line. The default illumination level of 1/7.3 at 100% is used */
        printk("========== flash af_assist =========== %d \n", enable);

        if(system_rev > 0x0A)
                gpio_set_value(GPIO_CAM_FLASH_EN, 0);
        else
                gpio_set_value(GPIO_CAM_FLASH_EN_REV05, 0);

        if (enable){
                aat1274_write(FLASH_MOVIE_MODE_CURRENT_100_PERCENT);
        }else{
                gpio_set_value(GPIO_CAM_FLASH_SET, 0);
        }

	return 0;
}

static int P3_s5k5ccgx_torch(int enable)
{
	/* Turn torch mode on or off by writing to the EN_SET line. A level
	 * of 1/7.3 and 50% is used (half AF assist brightness). */
        if(system_rev > 0x0A)
                gpio_set_value(GPIO_CAM_FLASH_EN, 0);
        else
                gpio_set_value(GPIO_CAM_FLASH_EN_REV05, 0);

        if (enable)
                aat1274_write(FLASH_MOVIE_MODE_CURRENT_79_PERCENT);
         else
                gpio_set_value(GPIO_CAM_FLASH_SET, 0);

	return 0;
}

struct s5k5ccgx_platform_data p3_s5k5ccgx_data = {
	.power_on = p3_s5k5ccgx_power_on,
	.power_off = p3_s5k5ccgx_power_off,
	.flash_onoff = P3_s5k5ccgx_flash,
	.af_assist_onoff = P3_s5k5ccgx_af_assist,
	.torch_onoff = P3_s5k5ccgx_torch,
	//.isp_int_read = p3_s5k5ccgx_isp_int_read
};

static const struct i2c_board_info sec_s5k5ccgx_camera[] = {
	{
		//I2C_BOARD_INFO("imx073", 0x3E>>1),
		I2C_BOARD_INFO("s5k5ccgx", 0x78>>1), // 0xAC
		.platform_data = &p3_s5k5ccgx_data,
	},
};

struct tegra_pingroup_config s5k5bbgx_mclk = {
	TEGRA_PINGROUP_CSUS, TEGRA_MUX_VI_SENSOR_CLK,
	TEGRA_PUPD_PULL_DOWN, TEGRA_TRI_TRISTATE
};

void p3_s5k5bbgx_power_on(void)
{
    printk("%s,, \n",__func__);
    gpio_set_value(GPIO_CAM_R_nSTBY, 0); //3M STBY low
    gpio_set_value(GPIO_CAM_R_nRST, 0); //3M nRST low
    gpio_set_value(GPIO_CAM_F_nSTBY, 0); // 2M STBY low
    gpio_set_value(GPIO_CAM_F_nRST, 0); // 2M nRST low
    gpio_set_value(GPIO_CAM_PMIC_EN1, 0);
    gpio_set_value(GPIO_CAM_PMIC_EN2, 0);
    gpio_set_value(GPIO_CAM_PMIC_EN3, 0);
    gpio_set_value(GPIO_CAM_PMIC_EN4, 0);
    udelay(100);
    gpio_set_value(GPIO_CAM_PMIC_EN1, 1); // 3M_CORE_1.2V, 3M_AF_2.8V
    udelay(100);
    gpio_set_value(GPIO_CAM_PMIC_EN2, 1); // CAM_AVDD2.8V
    udelay(100);
    gpio_set_value(GPIO_CAM_PMIC_EN3, 1); // 2M_DVDD_1.8V
    udelay(100);
    gpio_set_value(GPIO_CAM_PMIC_EN4, 1); // CAM_IO_1.8V
    udelay(100);

    tegra_pinmux_set_func(&mclk);
    tegra_pinmux_set_tristate(TEGRA_PINGROUP_CSUS, TEGRA_TRI_NORMAL);
    udelay(100);

    gpio_set_value(GPIO_CAM_F_nSTBY, 1); // 2M STBY High
    udelay(100);

    gpio_set_value(GPIO_CAM_F_nRST, 1); // 2M nRST High
    msleep(10); //udelay(200);
}


void p3_s5k5bbgx_power_off(void)
{
    msleep(3);
    gpio_set_value(GPIO_CAM_F_nRST, 0); // 2M nRST Low
    udelay(100);

    gpio_set_value(GPIO_CAM_F_nSTBY, 0); // 2M STBY Low
    udelay(100);

    tegra_pinmux_set_tristate(TEGRA_PINGROUP_CSUS, TEGRA_TRI_TRISTATE);
    udelay(100);

    gpio_set_value(GPIO_CAM_PMIC_EN4, 0); // CAM_IO_1.8V
    udelay(100);
    gpio_set_value(GPIO_CAM_PMIC_EN3, 0); // 2M_DVDD_1.8V
    udelay(100);
    gpio_set_value(GPIO_CAM_PMIC_EN2, 0); // CAM_AVDD2.8V
    udelay(100);
    gpio_set_value(GPIO_CAM_PMIC_EN1, 0); // 3M_CORE_1.2V, 3M_AF_2.8V
    msleep(800);
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
	sec_s5k5ccgx_init();
	status = i2c_register_board_info(3, sec_s5k5ccgx_camera,
				ARRAY_SIZE(sec_s5k5ccgx_camera));
	status = i2c_register_board_info(3, sec_s5k5bbgx_camera,
				ARRAY_SIZE(sec_s5k5bbgx_camera));

	return 0;
}

static void p3_touch_exit_hw(void)
{
	pr_info("p3_touch_exit_hw\n");
	gpio_free(GPIO_TOUCH_INT);
	gpio_free(GPIO_TOUCH_RST);
    if(system_rev < 0x0B)
      gpio_free(GPIO_TOUCH_EN_REV05);

	tegra_gpio_disable(GPIO_TOUCH_INT);
	tegra_gpio_disable(GPIO_TOUCH_RST);
    if(system_rev < 0x0B)
      tegra_gpio_disable(GPIO_TOUCH_EN_REV05);
}


static void p3_touch_suspend_hw(void)
{
	gpio_direction_output(GPIO_TOUCH_RST, 0);
	gpio_direction_output(GPIO_TOUCH_INT, 0);
    if(system_rev < 0x0B)
	  gpio_direction_output(GPIO_TOUCH_EN_REV05, 0);
}

static void p3_touch_resume_hw(void)
{
	gpio_direction_output(GPIO_TOUCH_RST, 1);
    if(system_rev < 0x0B)
      gpio_direction_output(GPIO_TOUCH_EN_REV05, 1);
	gpio_direction_input(GPIO_TOUCH_INT);
	msleep(120);
}

static void p3_register_touch_callbacks(struct mxt_callbacks *cb)
{
	charger_callbacks = cb;
}

static struct mxt_platform_data p3_touch_platform_data = {
	.numtouch = 10,
	.max_x  = 1279,
	.max_y  = 799,
	.init_platform_hw  = p3_touch_init_hw,
	.exit_platform_hw  = p3_touch_exit_hw,
	.suspend_platform_hw = p3_touch_suspend_hw,
	.resume_platform_hw = p3_touch_resume_hw,
	.register_cb = p3_register_touch_callbacks,
	/*mxt_power_config*/
	/* Set Idle Acquisition Interval to 32 ms. */
	.power_config.idleacqint = 32,
	.power_config.actvacqint = 255,
	/* Set Active to Idle Timeout to 4 s (one unit = 200ms). */
	.power_config.actv2idleto = 50,
	/*acquisition_config*/
	/* Atmel: 8 -> 10*/
	.acquisition_config.chrgtime = 10,
	.acquisition_config.reserved = 0,
	.acquisition_config.tchdrift = 5,
	/* Atmel: 0 -> 10*/
	.acquisition_config.driftst = 10,
	/* infinite*/
	.acquisition_config.tchautocal = 0,
	/* disabled*/
	.acquisition_config.sync = 0,
#ifdef MXT_CALIBRATE_WORKAROUND
	/*autocal config at wakeup status*/
	.acquisition_config.atchcalst = 9,
	.acquisition_config.atchcalsthr = 48,
	/* Atmel: 50 => 10 : avoid wakeup lockup : 2 or 3 finger*/
	.acquisition_config.atchcalfrcthr = 10,
	.acquisition_config.atchcalfrcratio = 215,
#else
	/* Atmel: 5 -> 0 -> 9  (to avoid ghost touch problem)*/
	.acquisition_config.atchcalst = 9,
	/* Atmel: 50 -> 55 -> 48 ->10 (to avoid ghost touch problem)*/
	.acquisition_config.atchcalsthr = 10,
	/* 50-> 20 (To avoid  wakeup touch lockup)  */
	.acquisition_config.atchcalfrcthr = 20,
	/* 25-> 0  (To avoid  wakeup touch lockup */
	.acquisition_config.atchcalfrcratio = 0,
#endif
	/*multitouch_config*/
	/* enable + message-enable*/
	.touchscreen_config.ctrl = 0x8b,
	.touchscreen_config.xorigin = 0,
	.touchscreen_config.yorigin = 0,
	.touchscreen_config.xsize = 27,
	.touchscreen_config.ysize = 42,
	.touchscreen_config.akscfg = 0,
	/* Atmel: 0x11 -> 0x21 -> 0x11*/
	.touchscreen_config.blen = 0x11,
	/* Atmel: 50 -> 55 -> 48,*/
	.touchscreen_config.tchthr = 48,
	.touchscreen_config.tchdi = 2,
	/* orient : Horizontal flip */
	.touchscreen_config.orient = 1,
	.touchscreen_config.mrgtimeout = 0,
	.touchscreen_config.movhysti = 10,
	.touchscreen_config.movhystn = 1,
	 /* Atmel  0x20 ->0x21 -> 0x2e(-2)*/
	.touchscreen_config.movfilter = 0x2f,
	.touchscreen_config.numtouch = MXT_MAX_NUM_TOUCHES,
	.touchscreen_config.mrghyst = 5, /*Atmel 10 -> 5*/
	 /* Atmel 20 -> 5 -> 50 (To avoid One finger Pinch Zoom) */
	.touchscreen_config.mrgthr = 50,
	.touchscreen_config.amphyst = 10,
	.touchscreen_config.xrange = 799,
	.touchscreen_config.yrange = 1279,
	.touchscreen_config.xloclip = 0,
	.touchscreen_config.xhiclip = 0,
	.touchscreen_config.yloclip = 0,
	.touchscreen_config.yhiclip = 0,
	.touchscreen_config.xedgectrl = 0,
	.touchscreen_config.xedgedist = 0,
	.touchscreen_config.yedgectrl = 0,
	.touchscreen_config.yedgedist = 0,
	.touchscreen_config.jumplimit = 18,
	.touchscreen_config.tchhyst = 10,
	.touchscreen_config.xpitch = 1,
	.touchscreen_config.ypitch = 3,
	/*noise_suppression_config*/
	.noise_suppression_config.ctrl = 5,
	.noise_suppression_config.reserved = 0,
	.noise_suppression_config.reserved1 = 0,
	.noise_suppression_config.reserved2 = 0,
	.noise_suppression_config.reserved3 = 0,
	.noise_suppression_config.reserved4 = 0,
	.noise_suppression_config.reserved5 = 0,
	.noise_suppression_config.reserved6 = 0,
	.noise_suppression_config.noisethr = 40,
	.noise_suppression_config.reserved7 = 0,/*1;*/
	.noise_suppression_config.freq[0] = 10,
	.noise_suppression_config.freq[1] = 18,
	.noise_suppression_config.freq[2] = 23,
	.noise_suppression_config.freq[3] = 30,
	.noise_suppression_config.freq[4] = 36,
	.noise_suppression_config.reserved8 = 0, /* 3 -> 0*/
	/*cte_config*/
	.cte_config.ctrl = 0,
	.cte_config.cmd = 0,
	.cte_config.mode = 0,
	/*16 -> 4 -> 8*/
	.cte_config.idlegcafdepth = 8,
	/*63 -> 16 -> 54(16ms sampling)*/
	.cte_config.actvgcafdepth = 54,
	.cte_config.voltage = 0x3c,
	/* (enable + non-locking mode)*/
	.gripsupression_config.ctrl = 0,
	.gripsupression_config.xlogrip = 0, /*10 -> 0*/
	.gripsupression_config.xhigrip = 0, /*10 -> 0*/
	.gripsupression_config.ylogrip = 0, /*10 -> 15*/
	.gripsupression_config.yhigrip = 0,/*10 -> 15*/
	.palmsupression_config.ctrl = 1,
	.palmsupression_config.reserved1 = 0,
	.palmsupression_config.reserved2 = 0,
	/* 40 -> 20(For PalmSuppression detect) */
	.palmsupression_config.largeobjthr = 20,
	/* 5 -> 50(For PalmSuppression detect) */
	.palmsupression_config.distancethr = 50,
	.palmsupression_config.supextto = 5,
	/*config change for ta connected*/
	.tchthr_for_ta_connect = 80,
	.tchdi_for_ta_connect = 2,
	.noisethr_for_ta_connect = 55,
	.idlegcafdepth_ta_connect = 32,
	.actvgcafdepth_ta_connect = 63,
#ifdef MXT_CALIBRATE_WORKAROUND
	/*autocal config at idle status*/
	.atchcalst_idle = 9,
	.atchcalsthr_idle = 10,
	.atchcalfrcthr_idle = 50,
	/* Atmel: 25 => 55 : avoid idle palm on lockup*/
	.atchcalfrcratio_idle = 55,
#endif
};

static const struct i2c_board_info sec_i2c_touch_info[] = {
	{
		I2C_BOARD_INFO("sec_touch", 0x4c),
		.irq		= TEGRA_GPIO_TO_IRQ(GPIO_TOUCH_INT),
		.platform_data = &p3_touch_platform_data,

	},
};

static int __init p3_touch_init(void)
{
	p3_touch_init_hw();
	i2c_register_board_info(1, sec_i2c_touch_info,
					ARRAY_SIZE(sec_i2c_touch_info));

	return 0;
}

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
		.power_down_on_bus_suspend = 0,
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
	.currentlimit_irq = TEGRA_GPIO_TO_IRQ(GPIO_V_ACCESSORY_5V),
};

static struct tegra_otg_platform_data tegra_otg_pdata_rev05 = {
	.host_register = &tegra_usb_otg_host_register,
	.host_unregister = &tegra_usb_otg_host_unregister,
	.otg_en = tegra_otg_en,
	.currentlimit_irq = TEGRA_GPIO_TO_IRQ(GPIO_V_ACCESSORY_5V_REV05),
};

#define AHB_ARBITRATION_DISABLE		0x0
#define   USB_ENB			(1 << 6)
#define   USB2_ENB	`		(1 << 18)
#define   USB3_ENB			(1 << 17)

#define AHB_ARBITRATION_PRIORITY_CTRL   0x4
#define   AHB_PRIORITY_WEIGHT(x)	(((x) & 0x7) << 29)
#define   PRIORITY_SELEECT_USB		(1 << 6)
#define   PRIORITY_SELEECT_USB2		(1 << 18)
#define   PRIORITY_SELEECT_USB3		(1 << 17)

#define AHB_GIZMO_AHB_MEM		0xc
#define   ENB_FAST_REARBITRATE		(1 << 2)

#define AHB_GIZMO_APB_DMA		0x10

#define AHB_GIZMO_USB			0x1c
#define AHB_GIZMO_USB2			0x78
#define AHB_GIZMO_USB3			0x7c
#define   IMMEDIATE			(1 << 18)
#define   MAX_AHB_BURSTSIZE(x)		(((x) & 0x3) << 16)
#define	  DMA_BURST_1WORDS		MAX_AHB_BURSTSIZE(0)
#define	  DMA_BURST_4WORDS		MAX_AHB_BURSTSIZE(1)
#define	  DMA_BURST_8WORDS		MAX_AHB_BURSTSIZE(2)
#define	  DMA_BURST_16WORDS		MAX_AHB_BURSTSIZE(3)

#define AHB_MEM_PREFETCH_CFG3		0xe0
#define AHB_MEM_PREFETCH_CFG4		0xe4
#define AHB_MEM_PREFETCH_CFG1		0xec
#define AHB_MEM_PREFETCH_CFG2		0xf0
#define   PREFETCH_ENB			(1 << 31)
#define   MST_ID(x)			(((x) & 0x1f) << 26)
#define   USB_MST_ID			MST_ID(6)
#define   USB2_MST_ID			MST_ID(18)
#define   USB3_MST_ID			MST_ID(17)
#define   ADDR_BNDRY(x)			(((x) & 0x1f) << 21)
#define		SPEC_THROTTLE(x)		(((x) & 0x1f) << 16)
#define   INACTIVITY_TIMEOUT(x)		(((x) & 0xffff) << 0)

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
	int gpio_otg_en;
	int ret;
	unsigned long val;

	mutex_init(&usb_data.ldo_en_lock);
	usb_data.usb_regulator_on[0] = 0;
	usb_data.usb_regulator_on[1] = 0;
	usb_data.usb_regulator_on[2] = 0;

        if(system_rev > 0x0A){
            tegra_gpio_enable(GPIO_V_ACCESSORY_5V);
            ret = gpio_request(GPIO_V_ACCESSORY_5V, "GPIO_V_ACCESSORY_5V");
            if (ret) {
            	pr_err("%s: gpio_request() for V_ACCESSORY_5V failed\n",
            		__func__);
            	return;
            }
            gpio_direction_input(GPIO_V_ACCESSORY_5V);
        }
        else{
            tegra_gpio_enable(GPIO_V_ACCESSORY_5V_REV05);
            ret = gpio_request(GPIO_V_ACCESSORY_5V_REV05, "GPIO_V_ACCESSORY_5V");
            if (ret) {
            	pr_err("%s: gpio_request() for V_ACCESSORY_5V failed\n",
            		__func__);
            	return;
            }
            gpio_direction_input(GPIO_V_ACCESSORY_5V_REV05);
        }

	gpio_otg_en = GPIO_OTG_EN;

	tegra_gpio_enable(gpio_otg_en);
	ret = gpio_request(gpio_otg_en, "GPIO_OTG_EN");
	if (ret) {
		pr_err("%s: gpio_request() for OTG_EN failed\n",
			__func__);
		return;
	}
	gpio_direction_output(gpio_otg_en, 0);

	tegra_gpio_enable(GPIO_ACCESSORY_EN);
	gpio_request(GPIO_ACCESSORY_EN, "GPIO_ACCESSORY_EN");
	if (ret) {
		pr_err("%s: gpio_request() for ACCESSORY_EN failed\n",
			__func__);
		return;
	}
	gpio_direction_output(GPIO_ACCESSORY_EN, 0);

	/* boost USB1 performance */
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
	val |= PRIORITY_SELEECT_USB | AHB_PRIORITY_WEIGHT(7) | PRIORITY_SELEECT_USB3;
	gizmo_writel(val, AHB_ARBITRATION_PRIORITY_CTRL);

	val = gizmo_readl(AHB_MEM_PREFETCH_CFG1);
	val &= ~MST_ID(~0);
	val |= PREFETCH_ENB | MST_ID(0x5) | ADDR_BNDRY(0xC) | SPEC_THROTTLE(0) | INACTIVITY_TIMEOUT(0x1000);
	gizmo_writel(val, AHB_MEM_PREFETCH_CFG1);

	val = gizmo_readl(AHB_MEM_PREFETCH_CFG2);
	val &= ~MST_ID(~0);
	val |= PREFETCH_ENB | USB_MST_ID | ADDR_BNDRY(0xC) | SPEC_THROTTLE(0) | INACTIVITY_TIMEOUT(0x1000);
	gizmo_writel(val, AHB_MEM_PREFETCH_CFG2);

	val = gizmo_readl(AHB_MEM_PREFETCH_CFG3);
	val &= ~MST_ID(~0);
	val |= PREFETCH_ENB | USB3_MST_ID| ADDR_BNDRY(0xC) | SPEC_THROTTLE(0) | INACTIVITY_TIMEOUT(0x1000);
	gizmo_writel(val, AHB_MEM_PREFETCH_CFG3);
	/* end of boost USB1 performance */

	tegra_usb_phy_init(tegra_usb_phy_pdata, ARRAY_SIZE(tegra_usb_phy_pdata));

	if(system_rev > 0x0A)
		tegra_otg_device.dev.platform_data = &tegra_otg_pdata;
	else
		tegra_otg_device.dev.platform_data = &tegra_otg_pdata_rev05;

	platform_device_register(&tegra_otg_device);

#ifdef CONFIG_SAMSUNG_LPM_MODE
	if (!charging_mode_from_boot) {
		tegra_ehci3_device.dev.platform_data=&tegra_ehci_pdata[2];
		platform_device_register(&tegra_ehci3_device);
	}
#else
	tegra_ehci3_device.dev.platform_data=&tegra_ehci_pdata[2];
	platform_device_register(&tegra_ehci3_device);
#endif

#if 0 /* TODO: USB team must verify */
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

static void p3_wlan_gpio_unconfig(void)
{
	printk(KERN_DEBUG "### p3_wlan_gpio_unconfig  ###\n");

}

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

	if (system_rev < 0x3)
		ear_micbias = TEGRA_GPIO_PH3;
	else
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

	tegra_gpio_enable(GPIO_MICBIAS_EN);
	tegra_gpio_enable(ear_micbias);
	tegra_gpio_enable(GPIO_DET_3_5);
	tegra_gpio_enable(GPIO_EAR_SEND_END);

cleanup:
	gpio_free(GPIO_MICBIAS_EN);
	gpio_free(ear_micbias);

	return ret;
}

#ifdef CONFIG_BCM4751_POWER
static struct bcm4751_rfkill_platform_data p3_gps_rfkill_pdata = {
	.gpio_nrst = GPIO_GPS_N_RST,
	.gpio_pwr_en	= GPIO_GPS_PWR_EN,
};

static struct platform_device p3_gps_rfkill_device = {
	.name = "bcm4751_rfkill",
	.id	= -1,
	.dev	= {
		.platform_data = &p3_gps_rfkill_pdata,
	},
};

static int __init p3_gps_init(void)
{
	struct clk *clk32 = clk_get_sys(NULL, "blink");
	if (!IS_ERR(clk32)) {
		clk_set_rate(clk32, clk32->parent->rate);
		clk_enable(clk32);
	}

	tegra_gpio_enable(GPIO_GPS_N_RST);
	tegra_gpio_enable(GPIO_GPS_PWR_EN);

	return 0;
}
#endif

static void p3_power_off(void)
{
	int ret;
	u32 value;

/*    
	gpio_set_value(GPIO_220_PMIC_PWRON, 0);
	msleep(300);

	gpio_set_value(GPIO_220_PMIC_PWRHOLD_OFF, 0);
	msleep(100);
*/
	value = gpio_get_value(GPIO_TA_nCONNECTED);
	if(!value) {
		tps6586x_soft_rst();
	}

	ret = tps6586x_power_off();
	if (ret)
		pr_err("p3: failed to power off\n");

	while (1);
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

static void p4_check_hwrev(void)
{
	unsigned int value, rev_no, i;
	struct board_revision *board_rev;

	board_rev = p4_board_rev;
	rev_no = ARRAY_SIZE(p4_board_rev);

	gpio_request(GPIO_HW_REV0, "GPIO_HW_REV0");
	gpio_request(GPIO_HW_REV1, "GPIO_HW_REV1");
	gpio_request(GPIO_HW_REV2, "GPIO_HW_REV2");
	gpio_request(GPIO_HW_REV3, "GPIO_HW_REV3");
	gpio_request(GPIO_HW_REV4, "GPIO_HW_REV4");

	tegra_gpio_enable(GPIO_HW_REV0);
	tegra_gpio_enable(GPIO_HW_REV1);
	tegra_gpio_enable(GPIO_HW_REV2);
	tegra_gpio_enable(GPIO_HW_REV3);
	tegra_gpio_enable(GPIO_HW_REV4);

	gpio_direction_input(GPIO_HW_REV0);
	gpio_direction_input(GPIO_HW_REV1);
	gpio_direction_input(GPIO_HW_REV2);
	gpio_direction_input(GPIO_HW_REV3);
	gpio_direction_input(GPIO_HW_REV4);

	printk("p4_check_hwrev may needs time for REV GPIO setting stable\n");
	msleep(100);
	printk("Check time here : p4_check_hwrev may needs time for REV GPIO setting stable\n");

	value = gpio_get_value(GPIO_HW_REV0) |
			(gpio_get_value(GPIO_HW_REV1)<<1) |
			(gpio_get_value(GPIO_HW_REV2)<<2) |
			(gpio_get_value(GPIO_HW_REV3)<<3) |
			(gpio_get_value(GPIO_HW_REV4)<<4);

	for (i = 0; i < rev_no; i++) {
		if (board_rev[i].gpio_value == value)
			break;
	}

	system_rev = (i == rev_no) ? board_rev[rev_no-1].value : board_rev[i].value;

	if (i == rev_no)
		pr_warn("%s: Valid revision NOT found! Latest one will be assigned!\n", __func__);

	pr_info("%s: system_rev = %d(0x%02x) (gpio value = 0x%02x)\n", __func__, system_rev, system_rev, value);
}

#ifdef CONFIG_KERNEL_DEBUG_SEC

static ssize_t store_sec_debug_upload(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,
				  size_t count)

{
	int sec_debug_level = kernel_sec_get_debug_level();

	if ( sec_debug_level == KERNEL_SEC_DEBUG_LEVEL_MID || sec_debug_level == KERNEL_SEC_DEBUG_LEVEL_HIGH ) {
		if (strncmp(buf, "RILPANIC", 8) == 0) {
			printk("=======================================\n");
			printk("This is RIL PANIC call not Kernel Panic\n");
			printk("=======================================\n");
			kernel_sec_set_upload_cause(UPLOAD_CAUSE_KERNEL_PANIC);
			kernel_sec_hw_reset(false);
			emergency_restart();
		}
	}
}

static DEVICE_ATTR(sec_debug_upload, 0220, NULL, store_sec_debug_upload);


/* Debug level control */
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
/* -- Debug level control */
#endif

static void __init tegra_p3_init(void)
{
	char serial[20];
	int ret = 0;

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

	tegra_i2s_device1.dev.platform_data = &tegra_audio_pdata[0];
	tegra_i2s_device2.dev.platform_data = &tegra_audio_pdata[1];
	tegra_spdif_device.dev.platform_data = &tegra_spdif_pdata;
	tegra_das_device.dev.platform_data = &tegra_das_pdata;

        if(system_rev > 0x0A)
            platform_add_devices(p3_devices, ARRAY_SIZE(p3_devices));
        else
            platform_add_devices(p3_devices_rev05, ARRAY_SIZE(p3_devices_rev05));

	sec_class = class_create(THIS_MODULE, "sec");
	if (IS_ERR(sec_class))
		pr_err("Failed to create sec class!\n");

	p4_check_hwrev();

	p3_jack_init();
	p3_sdhci_init();
	p3_lte_modem_bootloader_init();
	p3_gpio_i2c_init();
	p3_camera_init();
	p3_regulator_init();
#ifdef CONFIG_SAMSUNG_LPM_MODE
	if (!charging_mode_from_boot) {
		p3_touch_init();
	} else {
		p3_touch_init_hw();
		p3_touch_suspend_hw();
	}
#else
	p3_touch_init();
#endif
	p3_keys_init();

#ifdef CONFIG_SEC_MODEM
	register_smd_resource();
#endif

	p3_usb_init();
#ifdef CONFIG_BCM4751_POWER
	p3_gps_init();
#endif
	p3_panel_init();
	p3_sensors_init();
	p3_power_off_init();
	p3_emc_init();
	p3_rfkill_init();
	p3_bt_lpm_init();

#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
/* soonyong.cho : This is for setting unique serial number */
	s3c_usb_set_serial();
/* Changes value of nluns in order to use external storage */
	usb_device_init();
#endif
	register_reboot_notifier(&p3_reboot_notifier);

	/* don't allow console to suspend so we get extra logging
	 * during power management activities
	 */
	console_suspend_enabled = 0;

#ifdef CONFIG_KERNEL_DEBUG_SEC
	/* Add debug level node */
	struct device *platform = p3_devices[0]->dev.parent;
	ret = device_create_file(platform, &dev_attr_sec_debug_level);
	if (ret)
		printk("Fail to create sec_debug_level file\n");

	ret = device_create_file(platform, &dev_attr_sec_debug_upload);
	if (ret)
		printk("Fail to create sec_debug_upload file\n");
#endif
}

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
