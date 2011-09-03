/*
 * arch/arm/mach-tegra/board-p3.c
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
#include "board-p3.h"
#include "devices.h"
#include "fuse.h"
#include "wakeups-t2.h"
#include <media/s5k5bbgx.h>
#include <media/imx073.h>
#ifdef CONFIG_SAMSUNG_LPM_MODE
#include <linux/moduleparam.h>
#endif

struct class *sec_class;
EXPORT_SYMBOL(sec_class);

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

#if defined(CONFIG_EMBEDDED_MMC_START_OFFSET)
struct tegra_partition_info tegra_part_info = {
	.nr_parts = 0,
};

static int __init tegrapart_setup(char *options)
{
	char *str = options;

	if (!options || !*options)
		return 0;

	while (tegra_part_info.nr_parts < MAX_TEGRA_PART_NR) {
		struct tegra_partition *part;
		unsigned long long start, length, sector_sz;
		char *tmp = str;

		part = &tegra_part_info.parts[tegra_part_info.nr_parts];

		while (*tmp && !isspace(*tmp) && *tmp != ':')
			tmp++;

		if (tmp == str || *tmp != ':') {
			pr_err("%s: improperly formatted string %s\n",
				__func__, options);
			break;
		}

		part->name = str;
		*tmp = 0;

		str = tmp+1;
		start = simple_strtoull(str, &tmp, 16);
		if (*tmp != ':')
			break;
		str = tmp+1;
		length = simple_strtoull(str, &tmp, 16);
		if (*tmp != ':')
			break;
		str = tmp+1;
		sector_sz = simple_strtoull(str, &tmp, 16);

		start *= sector_sz;
		length *= sector_sz;
		part->offset = start;
		part->size = length;
		tegra_part_info.nr_parts++;
		str = tmp+1;

		if (*tmp != ',')
			break;
	}

	return 0;
}
__setup("tegrapart=", tegrapart_setup);
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

	if (mode == REBOOT_MODE_RECOVERY)
		strcpy(bootmsg.command, "boot-recovery");
	else if (mode == REBOOT_MODE_FASTBOOT)
		strcpy(bootmsg.command, "boot-fastboot");
	else if (mode == REBOOT_MODE_NORMAL)
		strcpy(bootmsg.command, "boot-reboot");
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

	if (code == SYS_RESTART) {
		mode = REBOOT_MODE_NORMAL;
		if (_cmd) {
			if (!strcmp((char *)_cmd, "recovery"))
				mode = REBOOT_MODE_RECOVERY;
			else if (!strcmp((char *)_cmd, "bootloader"))
				mode = REBOOT_MODE_FASTBOOT;
			else if (!strcmp((char *)_cmd, "download"))
				mode = REBOOT_MODE_DOWNLOAD;
		}
	} else if (code == SYS_POWER_OFF && charging_mode_from_boot == true)
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
	.spdif_clk_rate = 5644800,
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
	{ "i2s1",	"pll_a_out0",	11289600,	true},
	{ "i2s2",	"pll_a_out0",	11289600,	true},
	{ "audio",	"pll_a_out0",	11289600,	true},
	{ "audio_2x",	"audio",	22579200,	true},
	{ "spdif_out",	"pll_a_out0",	5644800,	false},
	{ "vde",	"pll_m",	240000000,	false},
	{ NULL,		NULL,		0,		0},
};

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

static void sec_jack_set_micbias_state(bool on)
{
	printk(KERN_DEBUG
		"Board P3 : Enterring sec_jack_set_micbias_state = %d\n", on);
	if (system_rev < 0x3)
		gpio_set_value(TEGRA_GPIO_PH3, on);
	else
		gpio_set_value(GPIO_EAR_MICBIAS_EN, on);
}

static struct platform_device androidusb_device = {
	.name   = "android_usb",
	.id     = -1,
	.dev    = {
		.platform_data  = &andusb_plat,
	},
};

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
	.bus_clk_rate	= { 400000, 10000 },
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
	.dap_clk = "clk_dev1",
	.tegra_dap_port_info_table = {
		/* I2S1 <--> DAC1 <--> DAP1 <--> Hifi Codec */
		[0] = {
			.dac_port = tegra_das_port_i2s1,
			.dap_port = tegra_das_port_dap1,
			.codec_type = tegra_audio_codec_type_hifi,
			.device_property = {
				.num_channels = 2,
				.bits_per_sample = 16,
				.rate = 44100,
				.dac_dap_data_comm_format =
						dac_dap_data_format_all,
			},
		},
		[1] = {
			.dac_port = tegra_das_port_none,
			.dap_port = tegra_das_port_none,
			.codec_type = tegra_audio_codec_type_none,
			.device_property = {
				.num_channels = 0,
				.bits_per_sample = 0,
				.rate = 0,
				.dac_dap_data_comm_format = 0,
			},
		},
		[2] = {
			.dac_port = tegra_das_port_none,
			.dap_port = tegra_das_port_none,
			.codec_type = tegra_audio_codec_type_none,
			.device_property = {
				.num_channels = 0,
				.bits_per_sample = 0,
				.rate = 0,
				.dac_dap_data_comm_format = 0,
			},
		},
		/* I2S2 <--> DAC2 <--> DAP4 <--> BT SCO Codec */
		[3] = {
			.dac_port = tegra_das_port_i2s2,
			.dap_port = tegra_das_port_dap4,
			.codec_type = tegra_audio_codec_type_bluetooth,
			.device_property = {
				.num_channels = 1,
				.bits_per_sample = 16,
				.rate = 8000,
				.dac_dap_data_comm_format =
					dac_dap_data_format_dsp,
			},
		},
		[4] = {
			.dac_port = tegra_das_port_none,
			.dap_port = tegra_das_port_none,
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
			.num_entries = 2,
			.con_line = {
				[0] = {tegra_das_port_i2s1, tegra_das_port_dap1, true},
				[1] = {tegra_das_port_dap1, tegra_das_port_i2s1, false},
			},
		},
		[1] = {
			.con_id = tegra_das_port_con_id_bt_codec,
			.num_entries = 4,
			.con_line = {
				[0] = {tegra_das_port_i2s2, tegra_das_port_dap4, true},
				[1] = {tegra_das_port_dap4, tegra_das_port_i2s2, false},
				[2] = {tegra_das_port_i2s1, tegra_das_port_dap1, true},
				[3] = {tegra_das_port_dap1, tegra_das_port_i2s1, false},
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

static void tegra_otg_en(int active)
{
	int gpio_otg_en;

	if (system_rev < 0x2)
		gpio_otg_en = TEGRA_GPIO_PW5;
	else
		gpio_otg_en = GPIO_OTG_EN;

	active = !!active;
	gpio_direction_output(gpio_otg_en, active);

	pr_debug("Board P3 : %s = %d\n", __func__, active);
}

void tegra_acc_power(int active)
{
	active = !!active;
	gpio_direction_output(GPIO_ACCESSORY_EN, active);
	pr_debug("Board P3 : %s = %d\n", __func__, active);
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

	gpio_request(GPIO_TA_nCONNECTED, "GPIO_TA_nCONNECTED");
	gpio_direction_input(GPIO_TA_nCONNECTED);
	tegra_gpio_enable(GPIO_TA_nCONNECTED);

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
	.temp_high_threshold = 50000,	/* 50c */
	.temp_high_recovery = 42000,	/* 42c */
	.temp_low_recovery = 2000,		/* 2c */
	.temp_low_threshold = 0,		/* 0c */
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

#if defined(CONFIG_KEYBOARD_P3)
static struct platform_device p3_keyboard = {
	.name	= "p3_keyboard",
	.id	= -1,
};
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
		/* 2000 < adc <= 3700, 4 pole zone */
		.adc_high = 3700,
		.delay_ms = 0,
		.check_count = 0,
		.jack_type = SEC_HEADSET_4POLE,
	},
	{
		/* adc > 3700, unstable zone, default to 3pole if it stays
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
		/* 130 <= adc <= 365, stable zone */
		.code		= KEY_MEDIA,
		.adc_low	= 0,
		.adc_high	= 2500,
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

static struct platform_device watchdog_device = {
	.name = "watchdog",
	.id = -1,
};

static struct platform_device *p3_devices[] __initdata = {
	&watchdog_device,
	&androidusb_device,
	&p3_rndis_device,
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
#if defined(CONFIG_KEYBOARD_P3)
	&p3_keyboard,
#endif
#ifdef CONFIG_30PIN_CONN
	&sec_device_connector,
#endif
	&tegra_das_device,
	&ram_console_device,
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
	gpio_request(GPIO_TOUCH_EN, "TOUCH_EN");
	gpio_request(GPIO_TOUCH_RST, "TOUCH_RST");
	gpio_request(GPIO_TOUCH_INT, "TOUCH_INT");

	gpio_direction_output(GPIO_TOUCH_EN, 1);
	gpio_direction_output(GPIO_TOUCH_RST, 1);
	gpio_direction_input(GPIO_TOUCH_INT);

	tegra_gpio_enable(GPIO_TOUCH_EN);
	tegra_gpio_enable(GPIO_TOUCH_RST);
	tegra_gpio_enable(GPIO_TOUCH_INT);
}

static void sec_imx073_init(void)
{
	tegra_gpio_enable(GPIO_CAM_PMIC_EN1);
	tegra_gpio_enable(GPIO_CAM_PMIC_EN2);
	tegra_gpio_enable(GPIO_CAM_PMIC_EN3);
	tegra_gpio_enable(GPIO_CAM_PMIC_EN4);
	tegra_gpio_enable(GPIO_CAM_PMIC_EN5);
	tegra_gpio_enable(GPIO_CAM_L_nRST);
	tegra_gpio_enable(GPIO_CAM_F_nRST);
	tegra_gpio_enable(GPIO_CAM_F_STANDBY);
	tegra_gpio_enable(GPIO_ISP_INT);

	gpio_request(GPIO_CAM_PMIC_EN1, "CAMERA_PMIC_EN1");
	gpio_request(GPIO_CAM_PMIC_EN2, "CAMERA_PMIC_EN2");
	gpio_request(GPIO_CAM_PMIC_EN3, "CAMERA_PMIC_EN3");
	gpio_request(GPIO_CAM_PMIC_EN4, "CAMERA_PMIC_EN4");
	gpio_request(GPIO_CAM_PMIC_EN5, "CAMERA_PMIC_EN5");
	gpio_request(GPIO_CAM_L_nRST, "CAMERA_CAM_Left_nRST");
	gpio_request(GPIO_CAM_F_nRST, "CAMERA_CAM_Front_nRST");
	gpio_request(GPIO_CAM_F_STANDBY, "CAMERA_CAM_Front_STANDBY");
	gpio_request(GPIO_ISP_INT, "ISP_INT");

	gpio_direction_output(GPIO_CAM_PMIC_EN1, 0);
	gpio_direction_output(GPIO_CAM_PMIC_EN2, 0);
	gpio_direction_output(GPIO_CAM_PMIC_EN3, 0);
	gpio_direction_output(GPIO_CAM_PMIC_EN4, 0);
	gpio_direction_output(GPIO_CAM_PMIC_EN5, 0);
	gpio_direction_output(GPIO_CAM_L_nRST, 0);
	gpio_direction_output(GPIO_CAM_F_nRST, 0);
	gpio_direction_output(GPIO_CAM_F_STANDBY, 0);
	gpio_direction_input(GPIO_ISP_INT);
}

struct tegra_pingroup_config mclk = {
	TEGRA_PINGROUP_CSUS,
	TEGRA_MUX_VI_SENSOR_CLK,
	TEGRA_PUPD_PULL_DOWN,
	TEGRA_TRI_TRISTATE
};

static void p3_imx073_power_on(void)
{
	gpio_set_value(GPIO_CAM_PMIC_EN1, 1);
	usleep_range(900, 1000);
	gpio_set_value(GPIO_CAM_PMIC_EN2, 1);
	usleep_range(900, 1000);
	if (system_rev >= 0x7) {
		gpio_set_value(GPIO_CAM_PMIC_EN4, 1);
		usleep_range(900, 1000);
		gpio_set_value(GPIO_CAM_PMIC_EN3, 1);
	} else {
		gpio_set_value(GPIO_CAM_PMIC_EN3, 1);
		usleep_range(900, 1000);
		gpio_set_value(GPIO_CAM_PMIC_EN4, 1);
	}
	usleep_range(900, 1000);
	gpio_set_value(GPIO_CAM_PMIC_EN5, 1);

	udelay(100);
	tegra_pinmux_set_func(&mclk);
	tegra_pinmux_set_tristate(TEGRA_PINGROUP_CSUS, TEGRA_TRI_NORMAL);
	udelay(10);
	gpio_set_value(GPIO_CAM_L_nRST, 1);
	usleep_range(3000, 10000);
}

static void p3_imx073_power_off(void)
{
	usleep_range(3000, 10000);
	gpio_set_value(GPIO_CAM_L_nRST, 0);
	udelay(10);
	tegra_pinmux_set_tristate(TEGRA_PINGROUP_CSUS, TEGRA_TRI_TRISTATE);
	udelay(50);
	gpio_set_value(GPIO_CAM_PMIC_EN5, 0);
	usleep_range(900, 1000);
	if (system_rev >= 0x7) {
		gpio_set_value(GPIO_CAM_PMIC_EN3, 0);
		msleep(40);
		gpio_set_value(GPIO_CAM_PMIC_EN4, 0);
	} else {
		gpio_set_value(GPIO_CAM_PMIC_EN4, 0);
		msleep(40);
		gpio_set_value(GPIO_CAM_PMIC_EN3, 0);
	}
	usleep_range(900, 1000);
	gpio_set_value(GPIO_CAM_PMIC_EN2, 0);
	msleep(40);
	gpio_set_value(GPIO_CAM_PMIC_EN1, 0);
}

static unsigned int p3_imx073_isp_int_read(void)
{
	return gpio_get_value(GPIO_ISP_INT);
}

struct imx073_platform_data p3_imx073_data = {
	.power_on = p3_imx073_power_on,
	.power_off = p3_imx073_power_off,
	.isp_int_read = p3_imx073_isp_int_read
};

static const struct i2c_board_info sec_imx073_camera[] = {
	{
		I2C_BOARD_INFO("imx073", 0x3E>>1),
		.platform_data = &p3_imx073_data,
	},
};

struct tegra_pingroup_config s5k5bbgx_mclk = {
	TEGRA_PINGROUP_CSUS, TEGRA_MUX_VI_SENSOR_CLK,
	TEGRA_PUPD_PULL_DOWN, TEGRA_TRI_TRISTATE
};

void p3_s5k5bbgx_power_on(void)
{
	gpio_set_value(GPIO_CAM_PMIC_EN1, 1);
	usleep_range(1000, 2000);
	gpio_set_value(GPIO_CAM_PMIC_EN2, 1);
	usleep_range(1000, 2000);
	if (system_rev >= 0x7) {
		gpio_set_value(GPIO_CAM_PMIC_EN4, 1);
		usleep_range(1000, 2000);
		gpio_set_value(GPIO_CAM_PMIC_EN3, 1);
	} else {
		gpio_set_value(GPIO_CAM_PMIC_EN3, 1);
		usleep_range(1000, 2000);
		gpio_set_value(GPIO_CAM_PMIC_EN4, 1);
	}
	usleep_range(1000, 2000);
	gpio_set_value(GPIO_CAM_PMIC_EN5, 1);

	udelay(100);

	tegra_pinmux_set_func(&s5k5bbgx_mclk);
	tegra_pinmux_set_tristate(TEGRA_PINGROUP_CSUS, TEGRA_TRI_NORMAL);

	udelay(10);

	gpio_set_value(GPIO_CAM_F_STANDBY, 1);
	udelay(20);

	gpio_set_value(GPIO_CAM_F_nRST, 1);
	udelay(10);

	gpio_set_value(GPIO_CAM_PMIC_EN2, 0);
	usleep_range(3000, 5000);

}

void p3_s5k5bbgx_power_off(void)
{
	usleep_range(3000, 5000);
	gpio_set_value(GPIO_CAM_F_nRST, 0);
	udelay(10);

	gpio_set_value(GPIO_CAM_F_STANDBY, 0);
	udelay(10);
	tegra_pinmux_set_tristate(TEGRA_PINGROUP_CSUS, TEGRA_TRI_TRISTATE);
	udelay(50);

	gpio_set_value(GPIO_CAM_PMIC_EN5, 0);
	usleep_range(1000, 2000);
	if (system_rev >= 0x7) {
		gpio_set_value(GPIO_CAM_PMIC_EN3, 0);
		msleep(500);
		gpio_set_value(GPIO_CAM_PMIC_EN4, 0);
	} else {
		gpio_set_value(GPIO_CAM_PMIC_EN4, 0);
		msleep(500);
		gpio_set_value(GPIO_CAM_PMIC_EN3, 0);
	}
	usleep_range(1000, 2000);
	gpio_set_value(GPIO_CAM_PMIC_EN2, 0);
	usleep_range(1000, 2000);
	gpio_set_value(GPIO_CAM_PMIC_EN1, 0);
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
	sec_imx073_init();
	status = i2c_register_board_info(3, sec_imx073_camera,
				ARRAY_SIZE(sec_imx073_camera));
	status = i2c_register_board_info(3, sec_s5k5bbgx_camera,
				ARRAY_SIZE(sec_s5k5bbgx_camera));

	return 0;
}

static void p3_touch_exit_hw(void)
{
	pr_info("p3_touch_exit_hw\n");
	gpio_free(GPIO_TOUCH_INT);
	gpio_free(GPIO_TOUCH_RST);
	gpio_free(GPIO_TOUCH_EN);

	tegra_gpio_disable(GPIO_TOUCH_INT);
	tegra_gpio_disable(GPIO_TOUCH_RST);
	tegra_gpio_disable(GPIO_TOUCH_EN);
}


static void p3_touch_suspend_hw(void)
{
	gpio_direction_output(GPIO_TOUCH_RST, 0);
	gpio_direction_output(GPIO_TOUCH_INT, 0);
	gpio_direction_output(GPIO_TOUCH_EN, 0);
}

static void p3_touch_resume_hw(void)
{
	gpio_direction_output(GPIO_TOUCH_RST, 1);
	gpio_direction_output(GPIO_TOUCH_EN, 1);
	gpio_direction_input(GPIO_TOUCH_INT);
	msleep(120);
}

static void p3_register_touch_callbacks(struct mxt_callbacks *cb)
{
	charger_callbacks = cb;
}

static struct mxt_platform_data p3_touch_platform_data = {
	.numtouch = 10,
	.max_x  = 1280,
	.max_y  = 800,
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
	.touchscreen_config.ctrl = 131,
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
	.touchscreen_config.movhysti = 3,
	.touchscreen_config.movhystn = 1,
	 /* Atmel  0x20 ->0x21 -> 0x2e(-2)*/
	.touchscreen_config.movfilter = 0x2e,
	.touchscreen_config.numtouch = MXT_MAX_NUM_TOUCHES,
	.touchscreen_config.mrghyst = 5, /*Atmel 10 -> 5*/
	 /* Atmel 20 -> 5 -> 50 (To avoid One finger Pinch Zoom) */
	.touchscreen_config.mrgthr = 50,
	.touchscreen_config.amphyst = 10,
	.touchscreen_config.xrange = 800,
	.touchscreen_config.yrange = 1280,
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
	.noise_suppression_config.freq[1] = 15,
	.noise_suppression_config.freq[2] = 20,
	.noise_suppression_config.freq[3] = 25,
	.noise_suppression_config.freq[4] = 30,
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
	.gripsupression_config.ctrl = 17,
	.gripsupression_config.xlogrip = 0, /*10 -> 0*/
	.gripsupression_config.xhigrip = 0, /*10 -> 0*/
	.gripsupression_config.ylogrip = 15, /*10 -> 15*/
	.gripsupression_config.yhigrip = 15,/*10 -> 15*/
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
	.noisethr_for_ta_connect = 60,
	.freq_for_ta_connect[0] = 8,
	.freq_for_ta_connect[1] = 10,
	.freq_for_ta_connect[2] = 15,
	.freq_for_ta_connect[3] = 22,
	.freq_for_ta_connect[4] = 25,
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
			.vbus_irq = TPS6586X_INT_BASE + TPS6586X_INT_USB_DET,
			.vbus_gpio = TEGRA_GPIO_PD0,
	},
	[1] = {
			.instance = 1,
			.vbus_gpio = -1,
	},
	[2] = {
			.instance = 2,
			.vbus_gpio = TEGRA_GPIO_PD3,
	},
};

static struct tegra_ehci_platform_data tegra_ehci_pdata[] = {
	[0] = {
		.phy_config = &utmi_phy_config[0],
		.operating_mode = TEGRA_USB_HOST,
		.power_down_on_bus_suspend = 1,
		.currentlimit_irq = TEGRA_GPIO_TO_IRQ(GPIO_V_ACCESSORY_5V),
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
};

static void p3_usb_init(void)
{
	int gpio_otg_en;
	int ret;
	char *src;
	int i;

	tegra_gpio_enable(GPIO_V_ACCESSORY_5V);
	ret = gpio_request(GPIO_V_ACCESSORY_5V, "GPIO_V_ACCESSORY_5V");
	if (ret) {
		pr_err("%s: gpio_request() for V_ACCESSORY_5V failed\n",
			__func__);
		return;
	}
	gpio_direction_input(GPIO_V_ACCESSORY_5V);

	if (system_rev < 0x2)
		gpio_otg_en = TEGRA_GPIO_PW5;
	else
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

	tegra_otg_device.dev.platform_data = &tegra_otg_pdata;
	platform_device_register(&tegra_otg_device);

	/* create a fake MAC address from our serial number.
	 * first byte is 0x02 to signify locally administered.
	 */
	src = andusb_plat.serial_number;
	p3_rndis_pdata.ethaddr[0] = 0x02;
	for (i = 0; *src; i++) {
		/* XOR the USB serial across the remaining bytes */
		p3_rndis_pdata.ethaddr[i % (ETH_ALEN - 1) + 1] ^= *src++;
	}
}

static int __init p3_hsic_init(void)
{
#ifdef CONFIG_SAMSUNG_LPM_MODE
	int ret = 0;
	if (!charging_mode_from_boot) {
		register_smd_resource();
		tegra_ehci2_device.dev.platform_data = &tegra_ehci_pdata[1];
		ret = platform_device_register(&tegra_ehci2_device);
	}
	return ret;
#else
	register_smd_resource();
	tegra_ehci2_device.dev.platform_data = &tegra_ehci_pdata[1];
	return platform_device_register(&tegra_ehci2_device);
#endif
}
late_initcall(p3_hsic_init);

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

static void p3_power_off(void)
{
	int ret;

	ret = tps6586x_power_off();
	if (ret)
		pr_err("p3: failed to power off\n");

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

static void __init tegra_p3_init(void)
{
	char serial[20];

	pr_info("P3 board revision = %d(0x%02x)\n", system_rev, system_rev);

	tegra_common_init();
	tegra_clk_init_from_table(p3_clk_init_table);
	p3_pinmux_init();
	p3_i2c_init();

	snprintf(serial, sizeof(serial), "%llX", tegra_chip_uid());
	andusb_plat.serial_number = kstrdup(serial, GFP_KERNEL);
	tegra_i2s_device1.dev.platform_data = &tegra_audio_pdata[0];
	tegra_i2s_device2.dev.platform_data = &tegra_audio_pdata[1];
	tegra_spdif_device.dev.platform_data = &tegra_spdif_pdata;
	tegra_das_device.dev.platform_data = &tegra_das_pdata;
	platform_add_devices(p3_devices, ARRAY_SIZE(p3_devices));

	sec_class = class_create(THIS_MODULE, "sec");
	if (IS_ERR(sec_class))
		pr_err("Failed to create sec class!\n");

	p3_jack_init();
	p3_sdhci_init();
	p3_gpio_i2c_init();
	p3_camera_init();
	p3_regulator_init();
#ifdef CONFIG_SAMSUNG_LPM_MODE
	if (!charging_mode_from_boot) {
		p3_touch_init();
	} else {
		p3_touch_init_hw();
	}
#else
	p3_touch_init();
#endif
	p3_keys_init();
	p3_usb_init();
	p3_gps_init();
	p3_panel_init();
	p3_sensors_init();
	p3_power_off_init();
	p3_emc_init();
	p3_rfkill_init();
	p3_bt_lpm_init();

	register_reboot_notifier(&p3_reboot_notifier);

	/* don't allow console to suspend so we get extra logging
	 * during power management activities
	 */
	console_suspend_enabled = 0;
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
