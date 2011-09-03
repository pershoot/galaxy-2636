/*
 * imx073.c - imx073 sensor driver
 *
 * Copyright (C) 2010 SAMSUNG ELECTRONICS.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/file.h>
#include <linux/fcntl.h>
#include <linux/clk.h>

#include <media/imx073.h>

#include <mach/pinmux.h>

#include <media/tegra_camera.h>

#include <linux/file.h>
#include <linux/vmalloc.h>

#include <linux/syscalls.h>

#define I2C_WRITE_SIZE 2048
struct imx073_reg {
	u16 addr;
	u16 val;
};

struct imx073_reg_isp {
	u16 command;
	u32 numbytes;
	u8 data[9];
};

struct imx073_info {
	int mode;
	struct i2c_client *i2c_client_isp;
	struct imx073_platform_data *pdata;
	struct m5mo_exif_info exif_info;
	enum imx073_scene_mode scenemode;
	enum imx073_mode_info lastmode_info;
	bool power_status;
};

extern struct class *sec_class;
struct device *imx073_dev;

#define CAMERA_FW_FILE_EXTERNAL_PATH	"/sdcard/RS_M5LS.bin"
#define CAMERA_FW_DUMP_FILE_PATH		"/sdcard/m5mo_dump.bin"
#define MISC_PARTITION_PATH	"/dev/block/mmcblk0p6"
#define START_POSITION_OF_VERSION_STRING		0x16ff00
#define LENGTH_OF_VERSION_STRING		21
#define FIRMWARE_ADDRESS_START  0x10000000
#define FIRMWARE_ADDRESS_END    0x101F7FFF

#define IMX073_TABLE_STOP 0xFFFF
#define IMX073_TABLE_END 0xFFFE
#define IMX073_TABLE_WAIT_MS 0xFFFD
#define IMX073_TABLE_LOAD 0xFFFC
#define IMX073_TABLE_DIRECT_WRITE_I2C 0xFFFB
#define IMX073_TABLE_WRITE 0xFFFA
#define IMX073_TABLE_WRITE_MEMORY 0xFFF9
#define IMX073_TABLE_READ 0xFFF8

#define IMX073_MAX_RETRIES 4
#define IMX073_FIRMWARE_READ_MAX_COUNT	50
#define IMX073_FIRMEWARE_READ_MAX_BUFFER_SIZE	2051

#define DEBUG_PRINTS 0
#if DEBUG_PRINTS
	#define FUNC_ENTR	\
		printk(KERN_INFO "[imx073] %s Entered!!!\n", __func__)
	#define I2C_DEBUG 1
#else
	#define FUNC_ENTR
	#define I2C_DEBUG 0
#endif

static const struct imx073_reg_isp mode_scene_auto[] = {
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x0A, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x0B, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x01, 0x03} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x38, 0x04} },
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x02, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x10, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x0F, 0x03} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x12, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x11, 0x05} },
	{0x0502,		5,	{0x05, 0x02, 0x09, 0x00, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x1D, 0x01} },
	{IMX073_TABLE_END,	0,	{0x00} }
};

static const struct imx073_reg_isp mode_scene_portrait[] = {
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x0A, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x0B, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x01, 0x03} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x38, 0x04} },
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x02, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x10, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x0F, 0x03} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x12, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x11, 0x04} },
	{0x0502,		5,	{0x05, 0x02, 0x09, 0x00, 0x11} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x1D, 0x00} },
	{IMX073_TABLE_END,	0,	{0x00} }
};

static const struct imx073_reg_isp mode_scene_landscape[] = {
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x0A, 0x02} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x0B, 0x02} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x01, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x38, 0x04} },
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x02, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x10, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x0F, 0x04} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x12, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x11, 0x06} },
	{0x0502,		5,	{0x05, 0x02, 0x09, 0x00, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x1D, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x40, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x41, 0x00} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_scene_sports[] = {
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x0A, 0x03} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x0B, 0x03} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x01, 0x03} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x38, 0x04} },
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x02, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x10, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x0F, 0x03} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x12, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x11, 0x05} },
	{0x0502,		5,	{0x05, 0x02, 0x09, 0x00, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x1D, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x40, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x41, 0x00} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static struct imx073_reg_isp mode_scene_party[] = {
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x0A, 0x04} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x0B, 0x04} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x01, 0x03} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x38, 0x04} },
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x02, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x10, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x0F, 0x04} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x12, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x11, 0x05} },
	{0x0502,		5,	{0x05, 0x02, 0x09, 0x00, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x1D, 0x00} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_scene_beach[] = {
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x0A, 0x05} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x0B, 0x05} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x01, 0x03} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x38, 0x06} },
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x02, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x10, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x0F, 0x04} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x12, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x11, 0x05} },
	{0x0502,		5,	{0x05, 0x02, 0x09, 0x00, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x1D, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x40, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x41, 0x00} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_scene_sunset[] = {
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x0A, 0x06} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x0B, 0x06} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x01, 0x03} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x38, 0x04} },
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x02, 0x02} },
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x03, 0x04} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x10, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x0F, 0x03} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x12, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x11, 0x05} },
	{0x0502,		5,	{0x05, 0x02, 0x09, 0x00, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x1D, 0x00} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_scene_night[] = {
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x0A, 0x09} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x0B, 0x09} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x01, 0x03} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x38, 0x04} },
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x02, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x10, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x0F, 0x03} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x12, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x11, 0x05} },
	{0x0502,		5,	{0x05, 0x02, 0x09, 0x00, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x1D, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x40, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x41, 0x00} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_scene_fire[] = {
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x0A, 0x0B} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x0B, 0x0B} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x01, 0x03} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x38, 0x04} },
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x02, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x10, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x0F, 0x03} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x12, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x11, 0x05} },
	{0x0502,		5,	{0x05, 0x02, 0x09, 0x00, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x1D, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x40, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x41, 0x00} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_scene_candle[] = {
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x0A, 0x0D} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x0B, 0x0D} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x01, 0x03} },
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x38, 0x04} },
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x02, 0x02} },
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x03, 0x04} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x10, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x0F, 0x03} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x12, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x11, 0x05} },
	{0x0502,		5,	{0x05, 0x02, 0x09, 0x00, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x1D, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x40, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x41, 0x00} },
	{IMX073_TABLE_END,	0,	{0x00} }
};

static const struct imx073_reg_isp mode_flash_torch[] = {
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x41, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x40, 0x00} }, /*All flash register Off*/
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x40, 0x03} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_flash_auto[] = {
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x41, 0x02} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x40, 0x02} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_flash_on[] = {
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x41, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x40, 0x01} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_flash_off[] = {
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x41, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x40, 0x00} },
	{IMX073_TABLE_END,	0,	{0x00} }
};

static const struct imx073_reg_isp mode_exposure_p2[] = {
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x38, 0x08} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_exposure_p1[] = {
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x38, 0x06} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_exposure_0[] = {
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x38, 0x04} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_exposure_m1[] = {
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x38, 0x02} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_exposure_m2[] = {
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x38, 0x00} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_af_stop[] = {
	{0x0502,		5,	{0x05, 0x02, 0x0A, 0x02, 0x00} }, /* Stop  AF */
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_af_start[] = {
	{0x0502,		5,	{0x05, 0x02, 0x0A, 0x02, 0x01} },/* Start AF */
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_caf_start[] = {
	{0x0502,		5,	{0x05, 0x02, 0x0A, 0x02, 0x02} },/* Start CAF */
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_af_result[] = {
	{IMX073_TABLE_READ,	5,	{0x05, 0x01, 0x0A, 0x03, 0x01} },/* AF Result */
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_focus_auto[] = {
	{0x0502,		5,	{0x05, 0x02, 0x0A, 0x01, 0x00} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_focus_infinity[] = {
	{0x0502,		5,	{0x05, 0x02, 0x0A, 0x01, 0x06} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_focus_macro[] = {
	{0x0502,		5,	{0x05, 0x02, 0x0A, 0x01, 0x01} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_lens_soft_landing[] = {
	{0x0502,		5,	{0x05, 0x02, 0x0A, 0x06, 0x80} },
	{0x0502,		5,	{0x05, 0x02, 0x0A, 0x07, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x0A, 0x02, 0x12} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_isp_parameter[] = {
	{0x0502,		5,	{0x05, 0x02, 0x00, 0x0B, 0x01} },
	{IMX073_TABLE_END,	0,	{0x00} }
};

static const struct imx073_reg_isp mode_isp_monitor[] = {
	{0x0502,		5,	{0x05, 0x02, 0x00, 0x0B, 0x02} },
	{IMX073_TABLE_STOP,	5,	{0x05, 0x01, 0x00, 0x10, 0x01} }, /*clear interrupt7*/
	{IMX073_TABLE_END,	0,	{0x00} }
};

static const struct imx073_reg_isp mode_isp_moderead[] = {
	{IMX073_TABLE_READ,	5,	{0x05, 0x01, 0x00, 0x0B, 0x01} },
	{IMX073_TABLE_END,	0,	{0x00} }
};


static const struct imx073_reg_isp mode_fwver_read[] = {
	{IMX073_TABLE_READ,	5,	{0x05, 0x01, 0x00, 0x0a, 1} },
	{IMX073_TABLE_END,	0,	{0x00} }
};

static const struct imx073_reg_isp mode_afcal_read[] = {
	{IMX073_TABLE_READ,	5,	{0x05, 0x01, 0x0A, 0x1d, 0x01} },
	{IMX073_TABLE_END,	0,	{0x00} }
};

static const struct imx073_reg_isp mode_awbcal_RGread_H[] = {
	{IMX073_TABLE_READ,	5,	{0x05, 0x01, 0x0E, 0x3C, 0x01} },
	{IMX073_TABLE_END,	0,	{0x00} }
};

static const struct imx073_reg_isp mode_awbcal_RGread_L[] = {
	{IMX073_TABLE_READ,	5,	{0x05, 0x01, 0x0E, 0x3D, 0x01} },
	{IMX073_TABLE_END,	0,	{0x00} }
};

static const struct imx073_reg_isp mode_awbcal_GBread_H[] = {
	{IMX073_TABLE_READ,	5,	{0x05, 0x01, 0x0E, 0x3E, 0x01} },
	{IMX073_TABLE_END,	0,	{0x00} }
};

static const struct imx073_reg_isp mode_awbcal_GBread_L[] = {
	{IMX073_TABLE_READ,	5,	{0x05, 0x01, 0x0E, 0x3F, 0x01} },
	{IMX073_TABLE_END,	0,	{0x00} }
};

static const struct imx073_reg_isp mode_isp_start[] = {
	{0x0502,		5,      {0x05, 0x02, 0x0F, 0x12, 0x01} },/* Starts Camera program */
	{IMX073_TABLE_STOP,	5,	{0x05, 0x01, 0x00, 0x10, 0x01} },/* clear interrupt7 */
	{IMX073_TABLE_END,	0,	{0x00} }
};

static const struct imx073_reg_isp mode_gammaeffect_off[] = {
	{0x0502,		5,	{0x05, 0x02, 0x01, 0x0B, 0x00} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_read_gammaeffect[] = {
	{IMX073_TABLE_READ,		5,	{0x05, 0x01, 0x01, 0x0B, 0x01} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_coloreffect_off[] = {
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x0B, 0x00} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_coloreffect_none[] = {
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x0B, 0x00} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_coloreffect_mono[] = {
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x0B, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x09, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x0A, 0x00} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_coloreffect_sepia[] = {
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x0B, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x09, 0xD8} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x0A, 0x18} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_coloreffect_posterize[] = { /* antique*/
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x0B, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x09, 0xD0} },
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x0A, 0x30} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_coloreffect_negative[] = {
	{0x0502,		5,	{0x05, 0x02, 0x01, 0x0B, 0x01} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_coloreffect_solarize[] = {
	{0x0502,		5,	{0x05, 0x02, 0x01, 0x0B, 0x04} },
	{IMX073_TABLE_END,	0,	{0x00} }
};

static const struct imx073_reg_isp mode_WB_auto[] = {
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x02, 0x01} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_WB_incandescent[] = {
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x02, 0x02} },
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x03, 0x01} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_WB_daylight[] = {
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x02, 0x02} },
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x03, 0x04} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_WB_fluorescent[] = {
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x02, 0x02} },
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x03, 0x02} },
	{IMX073_TABLE_END,	0,	{0x00} }
};

static const struct imx073_reg_isp mode_WB_cloudy[] = {
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x02, 0x02} },
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x03, 0x05} },
	{IMX073_TABLE_END,	0,	{0x00} }
};

static const struct imx073_reg_isp aeawb_lock[] = {
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x00, 0x01} },
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x00, 0x01} },
	{IMX073_TABLE_END,	0,	{0x00} }
};

static const struct imx073_reg_isp aeawb_unlock[] = {
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x00, 0x00} },
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x00, 0x00} },
	{IMX073_TABLE_END,	0,	{0x00} }
};

static const struct imx073_reg_isp mode_firmware_write[] = {
	{0x0902,		9,	{0x00, 0x04, 0x50, 0x00, 0x03, 0x08, 0x00, 0x01, 0x7E} },
	{0x0502,		5,	{0x05, 0x02, 0x0F, 0x13, 0x01} },
	{IMX073_TABLE_WRITE_MEMORY,	380928,	{0x68, 0x00, 0x00, 0x00} },
	{IMX073_TABLE_END,	0,	{0x00} }
};

static const struct imx073_reg_isp mode_recording_caf[] = {
	{0x0502,		5,	{0x05, 0x02, 0x0A, 0x01, 0x02} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static const struct imx073_reg_isp mode_recording_preview[] = {
	{0x0502,		5,	{0x05, 0x02, 0x0A, 0x01, 0x00} },
	{IMX073_TABLE_END,	0,	{0x00} }
};

/* exif information*/
static struct imx073_reg_isp mode_exif_exptime_numer[] = {
	{IMX073_TABLE_READ,	5,	{0x05, 0x01, 0x07, 0x00, 0x04} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static struct imx073_reg_isp mode_exif_exptime_denumer[] = {
	{IMX073_TABLE_READ,	5,	{0x05, 0x01, 0x07, 0x04, 0x04} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static struct imx073_reg_isp mode_exif_tv_numer[] = {
	{IMX073_TABLE_READ,	5,	{0x05, 0x01, 0x07, 0x08, 0x04} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static struct imx073_reg_isp mode_exif_tv_denumer[] = {
	{IMX073_TABLE_READ,	5,	{0x05, 0x01, 0x07, 0x0C, 0x04} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static struct imx073_reg_isp mode_exif_av_numer[] = {
	{IMX073_TABLE_READ,	5,	{0x05, 0x01, 0x07, 0x10, 0x04} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static struct imx073_reg_isp mode_exif_av_denumer[] = {
	{IMX073_TABLE_READ,	5,	{0x05, 0x01, 0x07, 0x14, 0x04} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static struct imx073_reg_isp mode_exif_bv_numer[] = {
	{IMX073_TABLE_READ,	5,	{0x05, 0x01, 0x07, 0x18, 0x04} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static struct imx073_reg_isp mode_exif_bv_denumer[] = {
	{IMX073_TABLE_READ,	5,	{0x05, 0x01, 0x07, 0x1C, 0x04} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static struct imx073_reg_isp mode_exif_iso_info[] = {
	{IMX073_TABLE_READ,	5,	{0x05, 0x01, 0x07, 0x28, 0x02} },
	{IMX073_TABLE_END,	0,	{0x00} }
};
static struct imx073_reg_isp mode_exif_flash_info[] = {
	{IMX073_TABLE_READ,	5,	{0x05, 0x01, 0x07, 0x2A, 0x02} },
	{IMX073_TABLE_END,	0,	{0x00} }
};

static const struct imx073_reg_isp SetModeSequence_ISP_Recording_setting[] = {
	{0x0502,		5,      {0x05, 0x02, 0x01, 0x32, 0x01} },/* 00 Monitor 01 MOVIE */
	{0x0502,		5,	{0x05, 0x02, 0x0A, 0x01, 0x02} },/* Continuous AF mode*/
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x10, 0x01} },/* Chroma Saturation ENABLE*/
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x0F, 0x03} },/* Chroma Saturation LVL*/
	{0x0502,		5,	{0x05, 0x02, 0x09, 0x00, 0x00} },/* Face Detection */
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x0B, 0x00} },/* EVP MODE CAP*/
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x38, 0x04} },/* AE_INDEX Default -> Add cuz request of Techwin*/
	{IMX073_TABLE_END,	0,      {0x00} }
};
static const struct imx073_reg_isp SetModeSequence_ISP_Preview_setting[] = {
	{0x0502,		5,      {0x05, 0x02, 0x01, 0x32, 0x00} },/* 00 Monitor 01 MOVIE */
	{0x0502,		5,	{0x05, 0x02, 0x0A, 0x01, 0x00} },/* Normal AF mode*/
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x10, 0x01} },/* Chroma Saturation ENABLE*/
	{0x0502,		5,	{0x05, 0x02, 0x02, 0x0F, 0x03} },/* Chroma Saturation LVL*/
	{0x0502,		5,	{0x05, 0x02, 0x09, 0x00, 0x00} },/* Face Detection */
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x0B, 0x00} },/* EVP MODE CAP*/
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x38, 0x04} },/* AE_INDEX Default -> Add cuz request of Techwin*/
	{IMX073_TABLE_END,	0,      {0x00} }
};

static const struct imx073_reg_isp SetModeSequence_ISP_Preview_Start_Camera[] = {
	{0x0502,		5,      {0x05, 0x02, 0x0F, 0x12, 0x01} },/* Starts Camera program */
	{IMX073_TABLE_STOP,	5,      {0x05, 0x01, 0x00, 0x10, 0x01} },/* clear interrupt7 */
	{0x0502,		5,      {0x05, 0x02, 0x01, 0x00, 0x02} },/* Select output interface to MIPI */
	{0x0502,		5,      {0x05, 0x02, 0x01, 0x01, 0x17} },/*  Preview size to YUV 640 x 480 */
	{0x0502,		5,      {0x05, 0x02, 0x0B, 0x01, 0x25} },/*  Capture size to YUV 3264 x 2448 */
	{0x0502,		5,      {0x05, 0x02, 0x01, 0x32, 0x00} },/* 00 Monitor 01 MOVIE */
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x00, 0x00} },/* select main image format */
	{IMX073_TABLE_END,	0,      {0x00} }
};

static const struct imx073_reg_isp SetModeSequence_ISP_Preview_Size_VGA[] = {
	{0x0502,		5,	{0x05, 0x02, 0x01, 0x02, 0x01} },/* Framerate : auto */
	{0x0502,		5,	{0x05, 0x02, 0x01, 0x01, 0x17} },/*  Preview size to YUV 640*480 */
	{IMX073_TABLE_END,	0,      {0x00} }
};
static const struct imx073_reg_isp SetModeSequence_ISP_Preview_Size_WVGA[] = {
	{0x0502,		5,	{0x05, 0x02, 0x01, 0x02, 0x01} },/* Framerate : auto */
	{0x0502,		5,	{0x05, 0x02, 0x01, 0x01, 0x1A} },/*  Preview size to YUV 800*480 */
	{IMX073_TABLE_END,	0,      {0x00} }
};
static const struct imx073_reg_isp SetModeSequence_ISP_Preview_Size_SVGA[] = {
	{0x0502,		5,	{0x05, 0x02, 0x01, 0x02, 0x01} },/* Framerate : auto */
	{0x0502,		5,	{0x05, 0x02, 0x01, 0x01, 0x1F} },/*  Preview size to YUV 800*600 */
	{IMX073_TABLE_END,	0,      {0x00} }
};
static const struct imx073_reg_isp SetModeSequence_ISP_Preview_Size_HD[] = {
	{0x0502,		5,	{0x05, 0x02, 0x01, 0x02, 0x01} },/* Framerate : auto */
	{0x0502,		5,	{0x05, 0x02, 0x01, 0x01, 0x21} },/*  Preview size to YUV 1280*720 */
	{IMX073_TABLE_END,	0,      {0x00} }
};
static const struct imx073_reg_isp SetModeSequence_ISP_Preview_Size_SXGA[] = {
	{0x0502,		5,	{0x05, 0x02, 0x01, 0x02, 0x01} },/* Framerate : auto */
	{0x0502,		5,	{0x05, 0x02, 0x01, 0x01, 0x24} },/*  Preview size to YUV 1280*960 */
	{IMX073_TABLE_END,	0,      {0x00} }
};
static const struct imx073_reg_isp SetModeSequence_ISP_Preview_Size_FHD[] = {
	{0x0502,		5,	{0x05, 0x02, 0x01, 0x31, 0x18} },/* Framerate : 24fps fixed */
	{0x0502,		5,	{0x05, 0x02, 0x01, 0x01, 0x25} },/*  Preview size to YUV 1920*1080 */
	{IMX073_TABLE_END,	0,      {0x00} }
};

static const struct imx073_reg_isp SetModeSequence_ISP_State_Monitor[] = {
	{0x0502,		5,	{0x05, 0x02, 0x00, 0x11, 0x01} },/* Enable Interrupt factor */
	{0x0502,		5,      {0x05, 0x02, 0x00, 0x0B, 0x02} },/* Go to monitor mode */
	{IMX073_TABLE_STOP,	5,      {0x05, 0x01, 0x00, 0x10, 0x01} },/* clear interrupt */
	{0x0502,		5,	{0x05, 0x02, 0x03, 0x00, 0x00} },/* unlock AE */
	{0x0502,		5,	{0x05, 0x02, 0x06, 0x00, 0x00} },/* unlock AWB */
	{IMX073_TABLE_END,	0,      {0x00} }
};
static const struct imx073_reg_isp SetModeSequence_ISP_State_Capture[] = {
	{0x0502,        	5, 	{0x05, 0x02, 0x0C, 0x0A, 0x64} },/* Delay for 100ms */
	{0x0502,		5,	{0x05, 0x02, 0x00, 0x11, 0x08} },/* Enable Interrupt factor */
	{0x0502,		5,	{0x05, 0x02, 0x00, 0x0B, 0x03} },/* Capture mode */
	{IMX073_TABLE_STOP,	5,      {0x05, 0x01, 0x00, 0x10, 0x01} },/* clear interrupt */
	{0x0502,		5,      {0x05, 0x02, 0x0C, 0x06, 0x01} },/* Select image number */
	{IMX073_TABLE_END,	0,      {0x00} }
};
static struct imx073_reg_isp SetModeSequence_ISP_Clear_Interrupt[] = {
	{IMX073_TABLE_STOP,	5,      {0x05, 0x01, 0x00, 0x10, 0x01} },/* clear interrupt */
	{IMX073_TABLE_END,	0,      {0x00} }
};

static const struct imx073_reg_isp SetModeSequence_ISP_Capture_Size_VGA[] = {
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x01, 0x09} },/*  Capture size to YUV 640*480 */
	{IMX073_TABLE_END,	0,      {0x00} }
};
static const struct imx073_reg_isp SetModeSequence_ISP_Capture_Size_WVGA[] = {
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x01, 0x0A} },/*  Capture size to YUV 800*480 */
	{IMX073_TABLE_END,	0,      {0x00} }
};
static const struct imx073_reg_isp SetModeSequence_ISP_Capture_Size_SVGA[] = {
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x01, 0x25} },/*  Capture size to YUV 800*600 */
	{IMX073_TABLE_END,	0,      {0x00} }
};
static const struct imx073_reg_isp SetModeSequence_ISP_Capture_Size_HD[] = {
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x01, 0x10} },/*  Capture size to YUV 1280*720 */
	{IMX073_TABLE_END,	0,      {0x00} }
};
static const struct imx073_reg_isp SetModeSequence_ISP_Capture_Size_SXGA[] = {
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x01, 0x14} },/*  Capture size to YUV 1280*960 */
	{IMX073_TABLE_END,	0,      {0x00} }
};
static const struct imx073_reg_isp SetModeSequence_ISP_Capture_Size_UXGA[] = {
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x01, 0x17} },/*  Capture size to YUV 1600*1200 */
	{IMX073_TABLE_END,	0,      {0x00} }
};
static const struct imx073_reg_isp SetModeSequence_ISP_Capture_Size_FHD[] = {
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x01, 0x19} },/*  Capture size to YUV 1920*1080 */
	{IMX073_TABLE_END,	0,      {0x00} }
};
static const struct imx073_reg_isp SetModeSequence_ISP_Capture_Size_8M[] = {
	{0x0502,		5,	{0x05, 0x02, 0x0B, 0x01, 0x25} },/*  Capture size to YUV 3264*2448 */
	{IMX073_TABLE_END,	0,      {0x00} }
};

static struct imx073_reg_isp SetModeSequence_ISP_Capture_transfer[] = {
	{0x0502,		5,	{0x05, 0x02, 0x0C, 0x09, 0x01} },
	{IMX073_TABLE_END,	0,      {0x00} }
};

enum {
	imx073_MODE_640x480,
	imx073_MODE_800x480,
	imx073_MODE_800x600,
	imx073_MODE_1280x720,
	imx073_MODE_1280x960,
	imx073_MODE_1600x1200,
	imx073_MODE_1920x1080,
	imx073_MODE_3264x2448,
};

static const struct imx073_reg_isp *preview_table[] = {
	[imx073_MODE_640x480] = SetModeSequence_ISP_Preview_Size_VGA,
	[imx073_MODE_800x480] = SetModeSequence_ISP_Preview_Size_WVGA,
	[imx073_MODE_800x600] = SetModeSequence_ISP_Preview_Size_SVGA,
	[imx073_MODE_1280x720] = SetModeSequence_ISP_Preview_Size_HD,
	[imx073_MODE_1280x960] = SetModeSequence_ISP_Preview_Size_SXGA,
	[imx073_MODE_1600x1200] = NULL,					/* ISP do not support this preview size */
	[imx073_MODE_1920x1080] = SetModeSequence_ISP_Preview_Size_FHD,
	[imx073_MODE_3264x2448] = NULL					/* ISP do not support this preview size */
};

static const struct imx073_reg_isp *capture_table[] = {
	[imx073_MODE_640x480] = SetModeSequence_ISP_Capture_Size_VGA,
	[imx073_MODE_800x480] = SetModeSequence_ISP_Capture_Size_WVGA,
	[imx073_MODE_800x600] = SetModeSequence_ISP_Capture_Size_8M,	/* ISP do not support this still size */
	[imx073_MODE_1280x720] = SetModeSequence_ISP_Capture_Size_HD,
	[imx073_MODE_1280x960] = SetModeSequence_ISP_Capture_Size_SXGA,
	[imx073_MODE_1600x1200] = SetModeSequence_ISP_Capture_Size_UXGA,
	[imx073_MODE_1920x1080] = SetModeSequence_ISP_Capture_Size_FHD,
	[imx073_MODE_3264x2448] = SetModeSequence_ISP_Capture_Size_8M
};

static const struct imx073_reg_isp *scene_table[] = {
	[SCENE_AUTO] = mode_scene_auto,
	[SCENE_ACTION] = mode_scene_sports,
	[SCENE_PORTRAIT] = mode_scene_portrait,
	[SCENE_LANDSCAPE] = mode_scene_landscape,
	[SCENE_NIGHT] = mode_scene_night,
	[SCENE_NIGHT_PORTRAIT] = mode_scene_night,
	[SCENE_THEATER] = mode_scene_auto,
	[SCENE_BEACH] = mode_scene_beach,
	[SCENE_SNOW] = mode_scene_beach,
	[SCENE_SUNSET] = mode_scene_sunset,
	[SCENE_STEADY_PHOTO] = mode_scene_auto,
	[SCENE_FIRE_WORK] = mode_scene_fire,
	[SCENE_PARTY] = mode_scene_party,
	[SCENE_CANDLE_LIGHT] = mode_scene_candle
};

static const struct imx073_reg_isp *focus_table[] = {
	[FOCUS_AUTO] = mode_focus_auto,
	[FOCUS_INFINITY] = mode_focus_infinity,
	[FOCUS_MACRO] = mode_focus_macro,
	[FOCUS_HYPER_FOCAL] = mode_focus_infinity
};

static const struct imx073_reg_isp *wb_table[] = {
	[WB_AUTO] = mode_WB_auto,
	[WB_DAYLIGHT] = mode_WB_daylight,
	[WB_INCANDESCENT] = mode_WB_incandescent,
	[WB_FLUORESCENT] = mode_WB_fluorescent,
	[WB_CLOUDY] = mode_WB_cloudy
};

static const struct imx073_reg_isp *flash_table[] = {
	[FLASH_AUTO] = mode_flash_auto,
	[FLASH_ON] = mode_flash_on,
	[FLASH_OFF] = mode_flash_off,
	[FLASH_TORCH] = mode_flash_torch
};

static const struct imx073_reg_isp *exposure_table[] = {
	[EXPOSURE_P2] = mode_exposure_p2,
	[EXPOSURE_P1] = mode_exposure_p1,
	[EXPOSURE_ZERO] mode_exposure_0,
	[EXPOSURE_M1] = mode_exposure_m1,
	[EXPOSURE_M2] = mode_exposure_m2
};


static int imx073_write_i2c(struct i2c_client *client, u8 *pdata, u16 flags, u16 len)
{
	int err;
	int retry;

	struct i2c_msg msg = {
		.addr = client->addr,
		.buf = pdata,
		.flags = flags,
		.len = len
	};

	FUNC_ENTR;
	retry = 0;
	do {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			break;
		pr_err("imx073: i2c transfer failed, \
				retrying(client : %d, msg : %d) - \
				open(%d)\n",
				client->addr, msg.addr, msg.flags);
		msleep(3);
	} while (++retry <= IMX073_MAX_RETRIES);
#if I2C_DEBUG
	{
		int i;
		pr_debug("%d size write : \n", msg.len);
		for (i = 0; i < msg.len; i++)
			pr_info("%x ", msg.buf[i]);
		pr_info("\n");
	}
#endif

	return err;
}

static int check_exist_file(unsigned char *filename)
{
	struct file *ftestexist = NULL;
	int ret;

	FUNC_ENTR;

	ftestexist = filp_open(filename, O_RDONLY, 0);
	pr_debug("%s : (%p)\n", __func__, ftestexist);

	if (IS_ERR_OR_NULL(ftestexist))
		ret = -1;
	else {
		ret = 0;
		filp_close(ftestexist, current->files);
	}
	pr_debug("%s : (%d)\n", __func__, ret);
	return ret;
}

static unsigned int get_file_size(unsigned char *filename)
{
	unsigned int file_size;
	struct file *filep;
	mm_segment_t old_fs;

	FUNC_ENTR;

	filep = filp_open(filename, O_RDONLY, 0) ;
	if (IS_ERR_OR_NULL(filep))
		file_size = 0;
	else {
		old_fs = get_fs();
		set_fs(KERNEL_DS);

		file_size = filep->f_op->llseek(filep, 0, SEEK_END);

		filp_close(filep, current->files);

		set_fs(old_fs);
	}
	pr_debug("%s: File size is %d\n", __func__, file_size);

	return file_size;
}

static unsigned int read_fw_data
	(unsigned char *buf, unsigned char *filename, unsigned int size)
{
	struct file *filep;
	mm_segment_t old_fs;

	FUNC_ENTR;

	filep = filp_open(filename, O_RDONLY, 0) ;

	if (IS_ERR_OR_NULL(filep))
		return 0;

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	filep->f_op->read(filep, buf, size, &filep->f_pos);

	filp_close(filep, current->files);

	set_fs(old_fs);

	return 1;
}

static int imx073_write_table_Isp(struct imx073_info *info,
		const struct imx073_reg_isp table[],
		u8 *rdata)
{
	const struct imx073_reg_isp *next;
	int i;
	u8 readbuffer[32];
	u8 pdata[32];
	unsigned int intstate;
	int ret = 1;
	struct i2c_client *client = info->i2c_client_isp;

	mm_segment_t oldfs;
	oldfs = get_fs();
	set_fs(get_ds());

	FUNC_ENTR;

	for (next = table; next->command != IMX073_TABLE_END; next++) {
		if (next->command == IMX073_TABLE_WAIT_MS) {
			msleep(next->numbytes);
			continue;
		} else if (next->command == IMX073_TABLE_STOP) {
			/*waiting for I2C*/
			i = 0;
			pr_debug("%s waiting ISP INT... (line : %d)\n", __func__, __LINE__);
			do {
				intstate = info->pdata->isp_int_read();
				if (i == 12000)	{
					i = 0;
					pr_debug("%s No Interrupt, \
							cancel waiting loop... (line : %d)\n", __func__, __LINE__);
					break;
				}
				i++;
				msleep(10);
			} while (intstate != 1);

			for (i = 0; i < next->numbytes; i++)
				pdata[i] = next->data[i];

			ret = imx073_write_i2c(client, pdata, 0, next->numbytes);
			if (ret != 1)
				return -1;

			pr_debug("INT clear..\n");

			ret = imx073_write_i2c(client, readbuffer, I2C_M_RD, 2);
			if (ret != 1)
				return -1;

			pr_debug("INT cleared!! %x%x\n", readbuffer[0], readbuffer[1]);

			i = 0;

			do {
				intstate = info->pdata->isp_int_read();
				if (i == 12000) {
					i = 0;
					pr_debug("%s No Interrupt, \
							cancel waiting loop... (line : %d)\n", \
							__func__, __LINE__);
					break;
				}
				i++;
				msleep(10);
			} while (intstate);
		} else if (next->command == IMX073_TABLE_WRITE_MEMORY) {
			unsigned char *bdata;
			unsigned char *fw_code = NULL;
			unsigned int address;
			unsigned int flash_addr = 0x10000000;
			unsigned int flash_addr2 = 0x101F0000;
			unsigned int fw_filesize;
			unsigned int fw_size = 0;
			unsigned int k;
			unsigned int addr = 0;
			unsigned int fw_addr = 0;
			unsigned int false = 0;
			unsigned int count = 0;
			unsigned int retry = 0;
			unsigned char fw[30] = {CAMERA_FW_FILE_EXTERNAL_PATH};
			ret = 0;

			pr_info("--FIRMWARE WRITE--\n");

			if (check_exist_file(fw) < 0) {
				pr_err("Fw file %s does not exist\n", fw);
				return false;
			}

			fw_filesize = get_file_size(fw);

			/* To store the firmware code */
			fw_size += ((fw_filesize-1) & ~0x3) + 4;
			pr_debug("1.filename = %s, fw_size = %d\n", fw, fw_size);
			pr_debug("2.filename = %s, filesize = %d\n", fw, fw_filesize);

			if (fw_size >= 0) {
				fw_code = (unsigned char *)vmalloc(fw_size);
				if (NULL == fw_code) {
					pr_debug("fw_code is NULL!!!\n");
					return false;
				} else {
					pr_debug("fw_code address = %p, %p\n", fw_code, &fw_code);
				}
			} else {
				pr_debug("Invalid input %d\n", fw_size);
				return false;
			}
			if (!read_fw_data(fw_code, fw, fw_filesize)) {
				pr_debug("Error reading firmware file.\n");
				goto write_fw_err;
			}
			addr = (unsigned int) fw_code;
			for (count = 0; count < 31; count++) {
				pr_debug("count: %d", count);
				/* Set FLASH ROM memory address */
				pdata[0] = 0x08;
				pdata[1] = 0x02;
				pdata[2] = 0x0F;
				pdata[3] = 0x00;
				pdata[4] = (unsigned char) (flash_addr >> 24);
				pdata[5] = (unsigned char) ((flash_addr >> 16) & 0xff);
				pdata[6] = (unsigned char) ((flash_addr >> 8) & 0xff);
				pdata[7] = (unsigned char) (flash_addr & 0xff);

				ret = imx073_write_i2c(client, pdata, 0, 8);
				if (ret != 1)
					goto write_fw_err;

				/* Erase FLASH ROM entire memory */
				pdata[0] = 0x05;
				pdata[1] = 0x02;
				pdata[2] = 0x0F;
				pdata[3] = 0x06;
				pdata[4] = 0x01;

				ret = imx073_write_i2c(client, pdata, 0, 5);
				if (ret != 1)
					goto write_fw_err;

				retry = 0;
				do {
					/* Response while sector-erase is operating. */
					pdata[0] = 0x05;
					pdata[1] = 0x01;
					pdata[2] = 0x0F;
					pdata[3] = 0x06;
					pdata[4] = 0x01;

					ret = imx073_write_i2c(client, pdata, 0, 5);
					if (ret != 1)
						goto write_fw_err;

					ret = imx073_write_i2c(client, readbuffer, I2C_M_RD, 2);
					if (ret != 1)
						goto write_fw_err;

					if (readbuffer[1] == 0x00)
						break;

					msleep(20);
				} while (++retry < IMX073_FIRMWARE_READ_MAX_COUNT);

				/* Set FLASH ROM programming size to 64kB */
				pdata[0] = 0x06;
				pdata[1] = 0x02;
				pdata[2] = 0x0F;
				pdata[3] = 0x04;
				pdata[4] = 0x00;
				pdata[5] = 0x00;

				ret = imx073_write_i2c(client, pdata, 0, 6);
				if (ret != 1)
					goto write_fw_err;

				/* clear M-5MoLS internal ram */
				pdata[0] = 0x05;
				pdata[1] = 0x02;
				pdata[2] = 0x0F;
				pdata[3] = 0x08;
				pdata[4] = 0x01;

				ret = imx073_write_i2c(client, pdata, 0, 5);
				if (ret != 1)
					goto write_fw_err;

				msleep(10);

				/* Set FLASH ROM memory address */
				pdata[0] = 0x08;
				pdata[1] = 0x02;
				pdata[2] = 0x0F;
				pdata[3] = 0x00;
				pdata[4] = (unsigned char) (flash_addr >> 24);
				pdata[5] = (unsigned char) ((flash_addr >> 16) & 0xff);
				pdata[6] = (unsigned char) ((flash_addr >> 8) & 0xff);
				pdata[7] = (unsigned char) (flash_addr & 0xff);

				ret = imx073_write_i2c(client, pdata, 0, 8);
				if (ret != 1)
					goto write_fw_err;

				address = 0x68000000;

				bdata = vmalloc(I2C_WRITE_SIZE + 8);
				if (!bdata)
					goto write_fw_err;

				for (k = 0; k < 0x10000; k += I2C_WRITE_SIZE) {

					bdata[0] = 0x00;
					bdata[1] = 0x04;
					bdata[2] = (address & 0xFF000000) >> 24;
					bdata[3] = (address & 0x00FF0000) >> 16;
					bdata[4] = (address & 0x0000FF00) >> 8;
					bdata[5] = (address & 0x000000FF);
					bdata[6] = (0xFF00 & I2C_WRITE_SIZE) >> 8;
					bdata[7] = (0x00FF & I2C_WRITE_SIZE);

					fw_addr = addr + (count * 0x10000) + k;

					memcpy(bdata+8 , (char *)fw_addr,
						I2C_WRITE_SIZE);

					address += I2C_WRITE_SIZE;
					ret = imx073_write_i2c(client, bdata, 0, I2C_WRITE_SIZE + 8);
					if (ret != 1) {
						vfree(bdata);
						goto write_fw_err;
					}
				}
				vfree(bdata);

				/* Start programming */
				pdata[0] = 0x05;
				pdata[1] = 0x02;
				pdata[2] = 0x0F;
				pdata[3] = 0x07;
				pdata[4] = 0x01;

				ret = imx073_write_i2c(client, pdata, 0, 5);
				if (ret != 1)
					goto write_fw_err;

				retry = 0;
				do {
					/* Response while sector-erase is operating */
					pdata[0] = 0x05;
					pdata[1] = 0x01;
					pdata[2] = 0x0F;
					pdata[3] = 0x07;
					pdata[4] = 0x01;

					ret = imx073_write_i2c(client, pdata, 0, 5);
					if (ret != 1)
						goto write_fw_err;

					ret = imx073_write_i2c(client, readbuffer, I2C_M_RD, 2);
					if (ret != 1)
						goto write_fw_err;

					if (readbuffer[1] == 0x00)
						break;
					msleep(20);
				} while (++retry < IMX073_FIRMWARE_READ_MAX_COUNT);

				msleep(20);

				flash_addr += 0x10000;
			}
			for (count = 0; count < 4; count++) {
				pr_debug("count: %d", count);
				/* Set FLASH ROM memory address */
				pdata[0] = 0x08;
				pdata[1] = 0x02;
				pdata[2] = 0x0F;
				pdata[3] = 0x00;
				pdata[4] = (unsigned char) (flash_addr2 >> 24);
				pdata[5] = (unsigned char) ((flash_addr2 >> 16) & 0xff);
				pdata[6] = (unsigned char) ((flash_addr2 >> 8) & 0xff);
				pdata[7] = (unsigned char) (flash_addr2 & 0xff);

				ret = imx073_write_i2c(client, pdata, 0, 8);
				if (ret != 1)
					goto write_fw_err;

				/* Erase FLASH ROM entire memory */
				pdata[0] = 0x05;
				pdata[1] = 0x02;
				pdata[2] = 0x0F;
				pdata[3] = 0x06;
				pdata[4] = 0x01;

				ret = imx073_write_i2c(client, pdata, 0, 5);
				if (ret != 1)
					goto write_fw_err;

				retry = 0;
				do {
					/* Response while sector-erase is operating. */
					pdata[0] = 0x05;
					pdata[1] = 0x01;
					pdata[2] = 0x0F;
					pdata[3] = 0x06;
					pdata[4] = 0x01;

					ret = imx073_write_i2c(client, pdata, 0, 5);
					if (ret != 1)
						goto write_fw_err;

					ret = imx073_write_i2c(client, readbuffer, I2C_M_RD, 2);
					if (ret != 1)
						goto write_fw_err;

					if (readbuffer[1] == 0x00)
						break;
					msleep(20);
				} while (++retry < IMX073_FIRMWARE_READ_MAX_COUNT);

				/* Set FLASH ROM programming size to 64kB */
				pdata[0] = 0x06;
				pdata[1] = 0x02;
				pdata[2] = 0x0F;
				pdata[3] = 0x04;
				pdata[4] = 0x20;
				pdata[5] = 0x00;

				ret = imx073_write_i2c(client, pdata, 0, 6);
				if (ret != 1)
					goto write_fw_err;

				/* clear M-5MoLS internal ram */
				pdata[0] = 0x05;
				pdata[1] = 0x02;
				pdata[2] = 0x0F;
				pdata[3] = 0x08;
				pdata[4] = 0x01;

				ret = imx073_write_i2c(client, pdata, 0, 5);
				if (ret != 1)
					goto write_fw_err;

				msleep(10);

				/* Set FLASH ROM memory address */
				pdata[0] = 0x08;
				pdata[1] = 0x02;
				pdata[2] = 0x0F;
				pdata[3] = 0x00;
				pdata[4] = (unsigned char) (flash_addr2 >> 24);
				pdata[5] = (unsigned char) ((flash_addr2 >> 16) & 0xff);
				pdata[6] = (unsigned char) ((flash_addr2 >> 8) & 0xff);
				pdata[7] = (unsigned char) (flash_addr2 & 0xff);

				ret = imx073_write_i2c(client, pdata, 0, 8);
				if (ret != 1)
					goto write_fw_err;

				address = 0x68000000;

				bdata = vmalloc(I2C_WRITE_SIZE + 8);
				if (!bdata)
					goto write_fw_err;

				for (k = 0; k < 0x2000; k += I2C_WRITE_SIZE) {
					bdata[0] = 0x00;
					bdata[1] = 0x04;
					bdata[2] = (address & 0xFF000000) >> 24;
					bdata[3] = (address & 0x00FF0000) >> 16;
					bdata[4] = (address & 0x0000FF00) >> 8;
					bdata[5] = (address & 0x000000FF);
					bdata[6] = (0xFF00 & I2C_WRITE_SIZE) >> 8;
					bdata[7] = (0x00FF & I2C_WRITE_SIZE);

					fw_addr = addr + (31 * 0x10000) + (count * 0x2000) + k;

					memcpy(bdata + 8, (char *)fw_addr,
						I2C_WRITE_SIZE);
					address += I2C_WRITE_SIZE;
					ret = imx073_write_i2c(client, bdata, 0, I2C_WRITE_SIZE + 8);
					if (ret != 1) {
						vfree(bdata);
						goto write_fw_err;
					}
				}
				vfree(bdata);

				/* Start programming */
				pdata[0] = 0x05;
				pdata[1] = 0x02;
				pdata[2] = 0x0F;
				pdata[3] = 0x07;
				pdata[4] = 0x01;

				ret = imx073_write_i2c(client, pdata, 0, 5);
				if (ret != 1)
					goto write_fw_err;

				retry = 0;
				do {
					/* Response while sector-erase is operating. */
					pdata[0] = 0x05;
					pdata[1] = 0x01;
					pdata[2] = 0x0F;
					pdata[3] = 0x07;
					pdata[4] = 0x01;

					ret = imx073_write_i2c(client, pdata, 0, 5);
					if (ret != 1)
						goto write_fw_err;

					ret = imx073_write_i2c(client, readbuffer, I2C_M_RD, 2);
					if (ret != 1)
						goto write_fw_err;

					if (readbuffer[1] == 0x00)
						break;

					msleep(20);
				} while (++retry < IMX073_FIRMWARE_READ_MAX_COUNT);
				msleep(20);

				flash_addr2 += 0x2000;
			}
			vfree(fw_code);

			pr_info("--FIRMWARE WRITE FINISH!!!--\n");
			return 0;
write_fw_err:
			pr_info("--FIRMWARE WRITE ABORTED DUE TO ERROR!!!--\n");
			vfree(fw_code);
			return -1;
		} else if (next->command == IMX073_TABLE_READ) {
			for (i = 0; i < next->numbytes; i++)
				pdata[i] = next->data[i];

			ret = imx073_write_i2c(client, pdata, 0, next->numbytes);
			if (ret != 1)
				return -1;

			ret = imx073_write_i2c(client, rdata, I2C_M_RD, pdata[4] + 1);
			if (ret != 1)
				return -1;
		} else {
			/*Write I2C Data*/
			for (i = 0; i < next->numbytes; i++)
				pdata[i] = next->data[i];

			ret = imx073_write_i2c(client, pdata, 0, next->numbytes);
			if (ret != 1)
				return -1;
		}
	}
	return 0;
}

static int imx073_firmware_write(struct imx073_info *info)
{
	int status = 0;

	status = imx073_write_table_Isp(info, mode_firmware_write, NULL);
	mdelay(100);
	return status;
}

static int m5mo_get_exif_info(struct imx073_info *info)
{
	int status;
	u8 val[5] = {0};
	struct m5mo_exif_info *exif_info = &info->exif_info;

	status = imx073_write_table_Isp(info, mode_exif_exptime_numer, val);
	if (status < 0)
		goto exif_err;
	exif_info->info_exptime_numer = val[4] +
		(val[3] << 8) + (val[2] << 16) + (val[1] << 24);
	pr_debug("m5mo_get_exif_info: info_exptime_numer = %d, %d, %d\n",
		exif_info->info_exptime_numer, val[1], val[2]);

	status = imx073_write_table_Isp(info, mode_exif_exptime_denumer, val);
	if (status < 0)
		goto exif_err;
	exif_info->info_exptime_denumer = val[4] +
		(val[3] << 8) + (val[2] << 16) + (val[1] << 24);
	pr_debug("m5mo_get_exif_info: info_exptime_denumer = %d\n",
		exif_info->info_exptime_denumer);

	status = imx073_write_table_Isp(info, mode_exif_tv_numer, val);
	if (status < 0)
		goto exif_err;
	exif_info->info_tv_numer = val[4] +
		(val[3] << 8) + (val[2] << 16) + (val[1] << 24);
	pr_debug("m5mo_get_exif_info: info_tv_numer = %d\n",
		exif_info->info_tv_numer);

	status = imx073_write_table_Isp(info, mode_exif_tv_denumer, val);
	if (status < 0)
		goto exif_err;
	exif_info->info_tv_denumer = val[4] +
		(val[3] << 8) + (val[2] << 16) + (val[1] << 24);
	pr_debug("m5mo_get_exif_info: info_tv_denumer = %d\n",
		exif_info->info_tv_denumer);

	status = imx073_write_table_Isp(info, mode_exif_av_numer, val);
	if (status < 0)
		goto exif_err;
	exif_info->info_av_numer = val[4] +
		(val[3] << 8) + (val[2] << 16) + (val[1] << 24);
	pr_debug("m5mo_get_exif_info: info_av_numer = %d\n",
		exif_info->info_av_numer);

	status = imx073_write_table_Isp(info, mode_exif_av_denumer, val);
	if (status < 0)
		goto exif_err;
	exif_info->info_av_denumer = val[4] +
		(val[3] << 8) + (val[2] << 16) + (val[1] << 24);
	pr_debug("m5mo_get_exif_info: info_av_denumer = %d\n",
		exif_info->info_av_denumer);

	status = imx073_write_table_Isp(info, mode_exif_bv_numer, val);
	if (status < 0)
		goto exif_err;
	exif_info->info_bv_numer = val[4] +
		(val[3] << 8) + (val[2] << 16) + (val[1] << 24);
	pr_debug("m5mo_get_exif_info: info_bv_numer = %d\n",
		exif_info->info_bv_numer);

	status = imx073_write_table_Isp(info, mode_exif_bv_denumer, val);
	if (status < 0)
		goto exif_err;
	exif_info->info_bv_denumer = val[4] +
		(val[3] << 8) + (val[2] << 16) + (val[1] << 24);
	pr_debug("m5mo_get_exif_info: info_bv_denumer = %d\n",
		exif_info->info_bv_denumer);

	status = imx073_write_table_Isp(info, mode_exif_iso_info, val);
	if (status < 0)
		goto exif_err;
	exif_info->info_iso = (u16)(val[2] + (val[1] << 8));
	pr_debug("m5mo_get_exif_info: info_iso = %u\n", exif_info->info_iso);

	status = imx073_write_table_Isp(info, mode_exif_flash_info, val);
	if (status < 0)
		goto exif_err;
	if ((val[2] + (val[1] << 8)) == 0x18 ||
		(val[2] + (val[1] << 8)) == 0x10) /*flash is not fire*/
		exif_info->info_flash = 0;
	else
		exif_info->info_flash = 1;
	pr_debug("m5mo_get_exif_info: read_val = 0x%xm, info_flash = %d\n",
		val[2] + (val[1] << 8), exif_info->info_flash);

	return 0;
exif_err:
	pr_err("m5mo_get_exif_info: error\n");
	return status;
}

static int imx073_verify_isp_mode(struct imx073_info *info, enum m5mo_isp_mode isp_mode)
{
	int i,  err;
	u8 status[2];
	FUNC_ENTR;
	for (i = 0; i < 100; i++) {
		err = imx073_write_table_Isp(info, mode_isp_moderead, status);

		pr_debug("%s: Isp mode status = 0x%x, trial = %d\n ", __func__, status[1], i);

		if (isp_mode == MODE_PARAMETER_SETTING) {
			if (status[1] == MODE_PARAMETER_SETTING)
				return 0;
		} else if (isp_mode == MODE_MONITOR) {
			if (status[1] == MODE_MONITOR)
				return 0;
		}
		msleep(20);
	}

	return -EBUSY;
}

static int imx073_set_mode
	(struct imx073_info *info, struct imx073_mode *mode)
{
	int sensor_mode;
	int err = 0;
	FUNC_ENTR;

/*
	pr_info("%s: xres %u yres %u mode_info %u \n",
			__func__, mode->xres, mode->yres, mode->mode_info);
*/
	if (mode->xres == 3264 && mode->yres == 2448)
		sensor_mode = imx073_MODE_3264x2448;
	else if (mode->xres == 1920 && mode->yres == 1080)
		sensor_mode = imx073_MODE_1920x1080;
	else if (mode->xres == 1600 && mode->yres == 1200)
		sensor_mode = imx073_MODE_1600x1200;
	else if (mode->xres == 1280 && mode->yres == 960)
		sensor_mode = imx073_MODE_1280x960;
	else if (mode->xres == 1280 && mode->yres == 720)
		sensor_mode = imx073_MODE_1280x720;
	else if (mode->xres == 800 && mode->yres == 600)
		sensor_mode = imx073_MODE_800x600;
	else if (mode->xres == 800 && mode->yres == 480)
		sensor_mode = imx073_MODE_800x480;
	else if (mode->xres == 640 && mode->yres == 480)
		sensor_mode = imx073_MODE_640x480;
	else {
		pr_err("%s: invalid resolution supplied to set mode %d %d\n",
				__func__, mode->xres, mode->yres);
		return -EINVAL;
	}

	/*imx073_firmware_write(info);*/

	if (mode->mode_info == MODE_INFO_STILL) {
		err = imx073_write_table_Isp(info, capture_table[sensor_mode], NULL);
		if (err)
			goto setmode_err;
		err = imx073_write_table_Isp(info, SetModeSequence_ISP_State_Capture, NULL);
		if (err)
			goto setmode_err;
		err = m5mo_get_exif_info(info);
		if (err)
			goto setmode_err;
		err = imx073_write_table_Isp(info, SetModeSequence_ISP_Capture_transfer, NULL);
		if (err)
			goto setmode_err;
	} else if (mode->mode_info == MODE_INFO_PREVIEW || mode->mode_info == MODE_INFO_VIDEO) {
		if (!info->power_status && mode->mode_info == MODE_INFO_PREVIEW) {
			if (!preview_table[sensor_mode]) {
				pr_err("%s: preview size %d %d is not supported in this sensor\n",
					__func__, mode->xres, mode->yres);
				return -EINVAL;
			}
			err = imx073_write_table_Isp(info, SetModeSequence_ISP_Preview_Start_Camera, NULL);
			if (err)
				goto setmode_err;
			err = imx073_write_table_Isp(info, preview_table[sensor_mode], NULL);
			if (err)
				goto setmode_err;
			err = imx073_write_table_Isp(info, SetModeSequence_ISP_State_Monitor, NULL);
			if (err)
				goto setmode_err;
			info->power_status = true;
		} else if (info->power_status &&
				(mode->mode_info == MODE_INFO_VIDEO || mode->mode_info == MODE_INFO_PREVIEW)) {
			if (info->lastmode_info == MODE_INFO_STILL) {
				err = imx073_write_table_Isp(info, SetModeSequence_ISP_Clear_Interrupt, NULL);
				if (err)
					goto setmode_err;
				err = imx073_write_table_Isp(info, SetModeSequence_ISP_State_Monitor, NULL);
				if (err)
					goto setmode_err;
			} else if (info->lastmode_info == MODE_INFO_PREVIEW ||
					info->lastmode_info == MODE_INFO_VIDEO) {
				err = imx073_write_table_Isp(info, mode_isp_parameter, NULL);
				if (err)
					goto setmode_err;
				err = imx073_verify_isp_mode(info, MODE_PARAMETER_SETTING);
				if (err)
					goto setmode_err;
				if (mode->mode_info == MODE_INFO_VIDEO) {
					err = imx073_write_table_Isp(info, SetModeSequence_ISP_Recording_setting, NULL);
					if (err)
						goto setmode_err;
				}
				if (info->lastmode_info == MODE_INFO_VIDEO) {
					err = imx073_write_table_Isp(info, SetModeSequence_ISP_Preview_setting, NULL);
					if (err)
						goto setmode_err;
				}

				if (info->mode != sensor_mode) {
					if (!preview_table[sensor_mode]) {
						pr_err("%s: preview size %d %d is not supported in this sensor\n",
							__func__, mode->xres, mode->yres);
						return -EINVAL;
					}
					err = imx073_write_table_Isp(info, preview_table[sensor_mode], NULL);
					if (err)
						goto setmode_err;
				}
				err = imx073_write_table_Isp(info, SetModeSequence_ISP_State_Monitor, NULL);
				if (err)
					goto setmode_err;
				if (mode->mode_info == MODE_INFO_VIDEO) {
					imx073_write_table_Isp(info, mode_caf_start, NULL);
				}
			}
		} else if (!info->power_status && mode->mode_info == MODE_INFO_VIDEO) {
			pr_err("%s: StartRecording can't launching before power on\n", __func__);
			return -EINVAL;
		} else {
			pr_err("%s: Unknown Error in set_mode\n", __func__);
			return -EFAULT;
		}
	}


	info->lastmode_info = mode->mode_info;
	info->mode = sensor_mode;
	return 0;
setmode_err:
	info->power_status = false;
	return err;
}

static int imx073_set_scene_mode
	(struct imx073_info *info, enum imx073_scene_mode arg)
{
	pr_debug("%s : %d\n", __func__, arg);
	if (arg < SCENE_MODE_MAX && arg >= 0)
		info->scenemode = arg;
	else
		return -EINVAL;
	return imx073_write_table_Isp(info, scene_table[arg], NULL);
}
static int imx073_set_focus_mode
	(struct imx073_info *info, enum imx073_focus_mode arg)
{
	pr_debug("%s : %d\n", __func__, arg);
	if (arg < FOCUS_MODE_MAX && arg >= 0)
		return imx073_write_table_Isp(info, focus_table[arg], NULL);
	else
		return -EINVAL;
}

static int imx073_set_color_effect
	(struct imx073_info *info, enum imx073_color_effect arg)
{
	int ret;
	pr_debug("%s : %d\n", __func__, arg);

	if (arg > EFFECT_MODE_MAX || arg < 0)
		return -EINVAL;

	if (arg == EFFECT_NEGATIVE || arg == EFFECT_SOLARIZE) {
		ret = imx073_write_table_Isp(info, mode_coloreffect_off, NULL);
		if (ret)
			return ret;
		ret = imx073_write_table_Isp(info, mode_isp_parameter, NULL);
		if (ret)
			return ret;
		ret = imx073_verify_isp_mode(info, MODE_PARAMETER_SETTING);
		if (ret)
			return ret;
	} else {
		u8 geffect[2];
		ret = imx073_write_table_Isp(info, mode_read_gammaeffect, geffect);
		if (ret)
			return ret;
		if (geffect[1]) {
			ret = imx073_write_table_Isp(info, mode_isp_parameter, NULL);
			if (ret)
				return ret;
			ret = imx073_verify_isp_mode(info, MODE_PARAMETER_SETTING);
			if (ret)
				return ret;
			ret = imx073_write_table_Isp(info, mode_gammaeffect_off, NULL);
			if (ret)
				return ret;
			ret = imx073_write_table_Isp(info, mode_isp_monitor, NULL);
			if (ret)
				return ret;
		}
	}

	switch (arg) {
	case EFFECT_NONE:
		ret = imx073_write_table_Isp(info, mode_coloreffect_off, NULL);
		if (ret)
			return ret;
		break;
	case EFFECT_MONO:
		ret = imx073_write_table_Isp(info, mode_coloreffect_mono, NULL);
		if (ret)
			return ret;
		break;
	case EFFECT_SEPIA:
		ret = imx073_write_table_Isp(info, mode_coloreffect_sepia, NULL);
		if (ret)
			return ret;
		break;
	case EFFECT_POSTERIZE:
		ret = imx073_write_table_Isp(info, mode_coloreffect_posterize, NULL);
		if (ret)
			return ret;
		break;
	case EFFECT_NEGATIVE:
		ret = imx073_write_table_Isp(info, mode_coloreffect_negative, NULL);
		if (ret)
			return ret;
		break;
	case EFFECT_SOLARIZE:
		ret = imx073_write_table_Isp(info, mode_coloreffect_solarize, NULL);
		if (ret)
			return ret;
		break;
	default:
		/* can't happen due to checks above but to quiet compiler
		 * warning
		 */
		break;
	}

	if (arg == EFFECT_NEGATIVE || arg == EFFECT_SOLARIZE) {
		ret = imx073_write_table_Isp(info, mode_isp_monitor, NULL);
		if (ret)
			return ret;
	}
	return 0;
}
static int imx073_set_white_balance
	(struct imx073_info *info, enum imx073_white_balance arg)
{
	pr_debug("%s : %d\n", __func__, arg);
	if (info->scenemode == SCENE_SUNSET || info->scenemode == SCENE_CANDLE_LIGHT)
		return 0;
	if (arg < WB_MODE_MAX && arg >= 0)
		return imx073_write_table_Isp(info, wb_table[arg], NULL);
	else
		return -EINVAL;
}
static int imx073_set_flash_mode
	(struct imx073_info *info, enum imx073_flash_mode arg)
{
	pr_debug("%s : %d\n", __func__, arg);
	if (arg < FLASH_MODE_MAX && arg >= 0)
		return  imx073_write_table_Isp(info, flash_table[arg], NULL);
	else
		return -EINVAL;
}
static int imx073_set_exposure
	(struct imx073_info *info, enum imx073_exposure arg)
{
	pr_debug("%s : %d\n", __func__, arg);
	if (arg < EXPOSURE_MODE_MAX && arg >= 0)
		return imx073_write_table_Isp(info, exposure_table[arg], NULL);
	else
		return -EINVAL;
}

static int imx073_set_autofocus
	(struct imx073_info *info, enum imx073_autofocus_control arg)
{
	int err;
	pr_debug("%s : %d\n", __func__, arg);
	switch (arg) {
	case AF_START:
		return imx073_write_table_Isp(info, mode_af_start, NULL);
	case AF_STOP:
		return imx073_write_table_Isp(info, mode_af_stop, NULL);
		break;
	case CAF_START:
		return imx073_write_table_Isp(info, mode_caf_start, NULL);
	case CAF_STOP:
		return imx073_write_table_Isp(info, mode_af_stop, NULL);
	}
	return 0;
}

static int imx073_set_lens_soft_landing (struct imx073_info *info)
{
	pr_debug("%s\n", __func__);
	return imx073_write_table_Isp(info, mode_lens_soft_landing, NULL);
}

static int imx073_get_af_result
	(struct imx073_info *info, u8 *status)
{
	int err;

	FUNC_ENTR;

	err = imx073_write_table_Isp(info, mode_af_result, status);
	pr_debug("af status = 0x%x", status[1]);
	return err;
}

static int imx073_esd_camera_reset
	(struct imx073_info *info, enum imx073_esd_reset arg)
{
	FUNC_ENTR;
	if (arg == ESD_DETECTED) {
		info->pdata->power_off();
		info->power_status = false;
		info->pdata->power_on();
	}
	return 0;
}

static int imx073_set_recording_frame(struct imx073_info *info, enum imx073_recording_frame arg)
{
	int err;

	pr_debug("test recording frame!!!\n");

	switch (arg) {
	case RECORDING_CAF:
		pr_debug("test recording frame - CAF!!!\n");
		err = imx073_write_table_Isp(info, mode_recording_caf, NULL);
		break;
	case RECORDING_PREVIEW:
		pr_debug("test recording frame - PREVIEW!!!\n");
		err = imx073_write_table_Isp(info, mode_recording_preview,
					NULL);
		break;
	default:
		pr_err("%s: Invalid recording frame Value, %d\n",
			__func__, arg);
		return 0;
		break;
	}

	if (err < 0)
		pr_err("%s: imx073_write_table() returned error, %d, %d\n",
			__func__, arg, err);

	return err;
}

static int imx073_set_aeawb_lockunlock(struct imx073_info *info, enum imx073_aeawb_lockunlock arg)
{
	int err;

	pr_debug("imx073_set_aeawb_lockunlock!!\n");

	switch (arg) {
	case AE_AWB_LOCK:
		err = imx073_write_table_Isp(info, aeawb_lock, NULL);
		break;
	case AE_AWB_UNLOCK:
		err = imx073_write_table_Isp(info, aeawb_unlock, NULL);
		break;
	default:
		pr_err("%s: Invalid aeawb_lockunlock Value, %d\n",
			__func__, arg);
		return 0;
		break;
	}

	if (err < 0)
		pr_err("%s: imx073_write_table() returned error, %d, %d\n",
			__func__, arg, err);

	return err;
}

/*
static int imx073_ioctl(struct inode *inode, struct file *file,
unsigned int cmd, unsigned long arg)
*/
static long imx073_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	struct imx073_info *info = file->private_data;
	FUNC_ENTR;
/*     pr_debug(KERN_INFO "\nimx073_ioctl : cmd = %d\n", cmd); */

	switch (cmd) {
	case IMX073_IOCTL_SET_MODE:
		{
			struct imx073_mode mode;
			if (copy_from_user(&mode, (const void __user *)arg,
				sizeof(struct imx073_mode))) {
				pr_info("%s %d\n", __func__, __LINE__);
				return -EFAULT;
			}

			return imx073_set_mode(info, &mode);
		}
	case IMX073_IOCTL_SCENE_MODE:
		return imx073_set_scene_mode(info, (enum imx073_scene_mode) arg);
	case IMX073_IOCTL_FOCUS_MODE:
		return imx073_set_focus_mode(info, (enum imx073_focus_mode) arg);
	case IMX073_IOCTL_COLOR_EFFECT:
		return imx073_set_color_effect(info, (enum imx073_color_effect) arg);
	case IMX073_IOCTL_WHITE_BALANCE:
		return imx073_set_white_balance(info, (enum imx073_white_balance) arg);
	case IMX073_IOCTL_FLASH_MODE:
		return imx073_set_flash_mode(info, (enum imx073_flash_mode) arg);
	case IMX073_IOCTL_EXPOSURE:
		return imx073_set_exposure(info, (enum imx073_exposure) arg);
	case IMX073_IOCTL_AF_CONTROL:
		return imx073_set_autofocus(info, (enum imx073_autofocus_control) arg);
	case IMX073_IOCTL_LENS_SOFT_LANDING:
		return imx073_set_lens_soft_landing(info);
	case IMX073_IOCTL_AF_RESULT:
	{
		int err;
		u8 status[2];
		err = imx073_get_af_result(info, status);
		if (err)
			return err;
		if (copy_to_user((void __user *)arg, &status[1], 1)) {
			pr_info("%s %d\n", __func__, __LINE__);
			return -EFAULT;
		}
		return 0;
	}
	case IMX073_IOCTL_ESD_RESET:
		return imx073_esd_camera_reset(info, (enum imx073_esd_reset) arg);
	case IMX073_IOCTL_EXIF_INFO:
		if (copy_to_user((void __user *)arg, &info->exif_info,
					sizeof(info->exif_info)))
			return -EFAULT;
		break;
	case IMX073_IOCTL_RECORDING_FRAME:
		return imx073_set_recording_frame(info, (enum imx073_recording_frame) arg);
	case IMX073_IOCTL_AEAWB_LOCKUNLOCK:
		return imx073_set_aeawb_lockunlock(info, (enum imx073_aeawb_lockunlock) arg);
	default:
		return -EINVAL;
	}
	return 0;
}

static struct imx073_info *info;

static int firmware_read(void)
{
	int err;
	u8 pdata[8];
	u8 *readbuffer;
	unsigned int address = FIRMWARE_ADDRESS_START;
	struct file *filp;
	mm_segment_t oldfs;
	struct i2c_client *client = info->i2c_client_isp;

	readbuffer = (u8 *)vmalloc(IMX073_FIRMEWARE_READ_MAX_BUFFER_SIZE);
	if (!readbuffer) {
		return -ENOMEM;
	}

	filp = filp_open(CAMERA_FW_DUMP_FILE_PATH, O_RDWR | O_CREAT, 0666);
	if (IS_ERR_OR_NULL(filp)) {
		pr_err("firmware_read: File open error\n");
		vfree(readbuffer);
		return -1;
	}

	pdata[0] = 0x00;
	pdata[1] = 0x03;

	filp->f_pos = 0;
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	while (address < FIRMWARE_ADDRESS_END) {
		pdata[2] = (address & 0xFF000000) >> 24;
		pdata[3] = (address & 0x00FF0000) >> 16;
		pdata[4] = (address & 0x0000FF00) >> 8;
		pdata[5] = (address & 0x000000FF);

		if (address + I2C_WRITE_SIZE <= FIRMWARE_ADDRESS_END) {
			pdata[6] = 0x08;
			pdata[7] = 0x00;
			err = imx073_write_i2c(client, pdata, 0, 8);
			if (err != 1)
				goto firmwarereaderr;
			err = imx073_write_i2c(client, readbuffer,
					1, 3 + I2C_WRITE_SIZE);
			if (err != 1)
				goto firmwarereaderr;
			filp->f_op->write(filp, &readbuffer[3],
					I2C_WRITE_SIZE, &filp->f_pos);
		} else {
			pdata[6] = ((FIRMWARE_ADDRESS_END - address + 1) &
				0xFF00) >> 8;
			pdata[7] = ((FIRMWARE_ADDRESS_END - address + 1) &
				0x00FF);
			err = imx073_write_i2c(client, pdata, 0, 8);
			if (err != 1)
				goto firmwarereaderr;
			err = imx073_write_i2c(client, readbuffer, 1,
					FIRMWARE_ADDRESS_END - address + 4);
			if (err != 1)
				goto firmwarereaderr;
			err = filp->f_op->write(filp, &readbuffer[3],
				FIRMWARE_ADDRESS_END - address + 1,
				&filp->f_pos);
			if (err < 0)
				goto firmwarereaderr;
		}
		address += I2C_WRITE_SIZE;
	}
	fput(filp);
	filp_close(filp, NULL);

	pr_info("dump end\n");

	set_fs(oldfs);
	vfree(readbuffer);
	return 0;
firmwarereaderr:
	set_fs(oldfs);
	filp_close(filp, NULL);
	vfree(readbuffer);
	return err;
}

static int m5mo_get_FW_info(char *fw_info)
{
	int err, i, w_count = 0;
	int m_pos = 0;
	char t_buf[35] = {0};
	struct file *filp;
	mm_segment_t oldfs;
	/*CAM FW*/
	for (i = 0; i < 30; i++) {
		err = imx073_write_table_Isp(info, mode_fwver_read, t_buf);
		if (t_buf[1] == 0x00)
			break;
		fw_info[m_pos] = t_buf[1];
		m_pos++;
	}
	/* Make blank*/
	fw_info[m_pos++] = ' ';

	/* PHONE FW */
	filp = filp_open(CAMERA_FW_FILE_EXTERNAL_PATH, O_RDONLY, 0);
	if (IS_ERR_OR_NULL(filp)) {
		pr_err("Error with open MISC(filp)\n");
		/* Just display dummy*/
		for (i = 0; i < LENGTH_OF_VERSION_STRING; i++) {
			if (i == 6 || i == 14)
				t_buf[i] = ' ';
			else
				t_buf[i] = 'F';
		}
	} else {
		filp->f_pos = START_POSITION_OF_VERSION_STRING;

		oldfs = get_fs();
		set_fs(KERNEL_DS);
		filp->f_op->read(filp, t_buf, LENGTH_OF_VERSION_STRING, &filp->f_pos);
		set_fs(oldfs);

		fput(filp);
		filp_close(filp, NULL);
	}

	for (i = 0; i < LENGTH_OF_VERSION_STRING; i++) {
		fw_info[m_pos] = t_buf[i];
		m_pos++;
	}

	/* Make blank*/
	fw_info[m_pos++] = ' ';

	w_count = 0; /*always 0*/
	sprintf(fw_info + m_pos, "%d", w_count);
	do {
		m_pos++;
	} while (fw_info[m_pos] != 0);

	/* Make blank*/
	fw_info[m_pos++] = ' ';

	/* AF CAL */
	err = imx073_write_table_Isp(info, mode_afcal_read, t_buf);
	if (t_buf[1] == 0xA0 || t_buf[1] == 0xA1) {
		sprintf(fw_info + m_pos, "%X", t_buf[1]);
		do {
			m_pos++;
		} while (fw_info[m_pos] != 0);

		/* Make blank*/
		fw_info[m_pos++] = ' ';

		sprintf(fw_info + m_pos, "%X", t_buf[1]);
		do {
			m_pos++;
		} while (fw_info[m_pos] != 0);
	} else {
		fw_info[m_pos++] = 'F';
		fw_info[m_pos++] = 'F';

		/* Make blank*/
		fw_info[m_pos++] = ' ';

		fw_info[m_pos++] = 'F';
		fw_info[m_pos++] = 'F';
	}

	/* Make blank*/
	fw_info[m_pos++] = ' ';

	/* AWB CAL RG High*/
	err = imx073_write_table_Isp(info, mode_awbcal_RGread_H, t_buf);
	sprintf(fw_info + m_pos, "%X", t_buf[1]);
	do {
		m_pos++;
	} while (fw_info[m_pos] != 0);

	/* Make blank*/
	fw_info[m_pos++] = ' ';

	/* AWB CAL RG Low*/
	err = imx073_write_table_Isp(info, mode_awbcal_RGread_L, t_buf);
	sprintf(fw_info + m_pos, "%X", t_buf[1]);
	do {
		m_pos++;
	} while (fw_info[m_pos] != 0);

	/* Make blank*/
	fw_info[m_pos++] = ' ';

	/* AWB CAL GB*/
	err = imx073_write_table_Isp(info, mode_awbcal_GBread_H, t_buf);
	sprintf(fw_info + m_pos, "%X", t_buf[1]);
	do {
		m_pos++;
	} while (fw_info[m_pos] != 0);

	/* Make blank*/
	fw_info[m_pos++] = ' ';

	/* AWB CAL GB*/
	err = imx073_write_table_Isp(info, mode_awbcal_GBread_L, t_buf);
	sprintf(fw_info + m_pos, "%X", t_buf[1]);
	do {
		m_pos++;
	} while (fw_info[m_pos] != 0);

	/* Make blank*/
	fw_info[m_pos++] = ' ';

	/* Make end */
	fw_info[m_pos] = '\0';

	pr_err(" \nfirmware information: %s\n", fw_info);

	return err;
}

static ssize_t camerafw_file_cmd_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int status;
	struct tegra_camera_clk_info info_clk;
	char fw_info[100] = {0};

	pr_info("called %s \n", __func__);

	tegra_camera_enable_vi();

	info_clk.id = TEGRA_CAMERA_MODULE_VI;
	info_clk.clk_id = TEGRA_CAMERA_VI_SENSOR_CLK;
	info_clk.rate = 24000000;
	tegra_camera_clk_set_rate(&info_clk);
	tegra_camera_enable_csi();
	info->pdata->power_on();

	status = imx073_write_table_Isp(info, mode_isp_start, NULL);
	if (status != 0)
		return status;
	status = m5mo_get_FW_info(fw_info);
	if (status != 0)
		return status;
	info->pdata->power_off();
	tegra_camera_disable_vi();
	tegra_camera_disable_csi();

	return sprintf(buf, "%s\n", fw_info);
}

static ssize_t camerafw_file_cmd_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int value;
	int status;
	struct tegra_camera_clk_info info_clk;

	sscanf(buf, "%d", &value);

	tegra_camera_enable_vi();

	info_clk.id = TEGRA_CAMERA_MODULE_VI;
	info_clk.clk_id = TEGRA_CAMERA_VI_SENSOR_CLK;
	info_clk.rate = 24000000;
	tegra_camera_clk_set_rate(&info_clk);

	tegra_camera_enable_csi();
	info->pdata->power_on();

	if (value == FWUPDATE) {
		pr_err("[fwupdate set]imx073_firmware_write start\n");
		status = imx073_firmware_write(info);
		if (status != 0)
			return -1;
		pr_err("[fwupdate set]imx073_firmware_write end\n");

	} else if (value == FWDUMP) {
		status = firmware_read();
		if (status != 0)
			return -1;
		pr_info("FWDUMP is done!!\n");
	}

	info->pdata->power_off();
	tegra_camera_disable_vi();
	tegra_camera_disable_csi();
	return size;
}
static DEVICE_ATTR(camerafw, 0660, camerafw_file_cmd_show, camerafw_file_cmd_store);

static int imx073_open(struct inode *inode, struct file *file)
{
	int status;
	u8 sysmode[2];

	FUNC_ENTR;

	file->private_data = info;

	info->scenemode = SCENE_AUTO;
	info->lastmode_info = MODE_INFO_PREVIEW;
	info->power_status = false;

	if (info->pdata && info->pdata->power_on)
		info->pdata->power_on();

	status = imx073_write_table_Isp(info, mode_isp_moderead, sysmode);

	if (status < 0) {
		info->pdata->power_off();
		info->power_status = false;
	}

	return status;
}

int imx073_release(struct inode *inode, struct file *file)
{
	FUNC_ENTR;
	if (info->pdata && info->pdata->power_off) {
		info->pdata->power_off();
		info->power_status = false;
	}
	file->private_data = NULL;
	return 0;
}

static const struct file_operations imx073_fileops = {
	.owner = THIS_MODULE,
	.open = imx073_open,
	.unlocked_ioctl = imx073_ioctl,
	.compat_ioctl = imx073_ioctl,
	.release = imx073_release,
};

static struct miscdevice imx073_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "imx073",
	.fops = &imx073_fileops,
};

static int imx073_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int err;
	FUNC_ENTR;

	pr_debug("%s , %x probing i2c(%lu)", id->name, client->addr, id->driver_data);

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		pr_err("imx073: Unable to allocate memory!\n");
		return -ENOMEM;
	}
	info->i2c_client_isp = client;
	if (!info->i2c_client_isp) {
		pr_err("imx073: Unknown I2C client!\n");
		err = -ENODEV;
		goto probeerr;
	}
	info->pdata = client->dev.platform_data;
	if (!info->pdata) {
		pr_err("imx073: Unknown platform data!\n");
		err = -ENODEV;
		goto probeerr;
	}
	err = misc_register(&imx073_device);
	if (err) {
		pr_err("imx073: Unable to register misc device!\n");
		goto probeerr;
	}
	i2c_set_clientdata(client, info);

	imx073_dev = device_create(sec_class, NULL, 0, NULL, "sec_imx073");
	if (IS_ERR(imx073_dev)) {
		pr_err("imx073_probe: Failed to create device!");
		misc_deregister(&imx073_device);
		goto probeerr;
	}
	if (device_create_file(imx073_dev, &dev_attr_camerafw) < 0) {
		pr_err("imx073_probe: Failed to create device file!(%s)!\n", dev_attr_camerafw.attr.name);
		device_destroy(sec_class, 0);
		goto probeerr;
	}
	return 0;

probeerr:
	kfree(info);
	return err;
}

static int imx073_remove(struct i2c_client *client)
{
	struct imx073_info *info;
	FUNC_ENTR;
	info = i2c_get_clientdata(client);
	misc_deregister(&imx073_device);
	kfree(info);
	device_remove_file(imx073_dev, &dev_attr_camerafw);
	return 0;
}

/*i2c_imx073*/
static const struct i2c_device_id imx073_id[] = {
	{ "imx073", 0 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, imx073_id);

static struct i2c_driver imx073_i2c_driver = {
	.driver = {
		.name = "imx073",
		.owner = THIS_MODULE,
	},
	.probe = imx073_probe,
	.remove = imx073_remove,
	.id_table = imx073_id,
};

static int __init imx073_init(void)
{
	int status;
	FUNC_ENTR;
	pr_info("imx073 sensor driver loading\n");
	status = i2c_add_driver(&imx073_i2c_driver);
	if (status) {
		pr_err("imx073 error\n");
		return status;
	}
	return 0;
}

static void __exit imx073_exit(void)
{
	FUNC_ENTR;
	i2c_del_driver(&imx073_i2c_driver);
}

module_init(imx073_init);
module_exit(imx073_exit);
