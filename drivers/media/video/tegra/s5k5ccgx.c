/* drivers/media/video/tegra/s5k5ccgx.c
 *
 * Driver for s5k5ccgx (3MP Camera) from SEC
 *
 * Copyright (C) 2010, SAMSUNG ELECTRONICS
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/completion.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/file.h>
#include <linux/syscalls.h>
#include <media/tegra_camera.h>

#ifndef CONFIG_MACH_SAMSUNG_P5W_KT
#ifdef CONFIG_MACH_SAMSUNG_P5
#include "s5k5ccgx_regs_p5.h"
#else
#include "s5k5ccgx_regs.h"
#endif
#else	//homepad
#include "s5k5ccgx_regs_p5_kt.h"
#endif

#include <media/s5k5ccgx.h>


//#define CONFIG_LOAD_FILE	/*For tunning binary*/
#define S5K5CCGX_BURST_WRITE_LIST(A)	s5k5ccgx_sensor_burst_write_list(A,(sizeof(A) / sizeof(A[0])),#A);
#ifdef FACTORY_TEST
static int dtpTest = DTP_OFF;
#endif
#define FACTORY_CHECK


//modify to TouchAF by Teddy
#define INNER_WINDOW_WIDTH_1024_768             230         //INNER_WINDOW_WIDTH_ON_PREVIEW       
#define INNER_WINDOW_HEIGHT_1024_768            230
#define OUTER_WINDOW_WIDTH_1024_768              512
#define OUTER_WINDOW_HEIGHT_1024_768             426
#define INNER_WINDOW_WIDTH_720P                 287
#define INNER_WINDOW_HEIGHT_720P                215
#define OUTER_WINDOW_WIDTH_720P                 640
#define OUTER_WINDOW_HEIGHT_720P                399         //OUTER_WINDOW_HEIGHT_ON_CAMREC


#define FORMAT_FLAGS_COMPRESSED		0x3
#define SENSOR_JPEG_SNAPSHOT_MEMSIZE	0x410580

#define POLL_TIME_MS		10
#define CAPTURE_POLL_TIME_MS    1000

/* maximum time for one frame at minimum fps (15fps) in normal mode */
#define NORMAL_MODE_MAX_ONE_FRAME_DELAY_MS     67
/* maximum time for one frame at minimum fps (4fps) in night mode */
#define NIGHT_MODE_MAX_ONE_FRAME_DELAY_MS     250

/* time to move lens to target position before last af mode register write */
#define LENS_MOVE_TIME_MS       100

/* level at or below which we need to enable flash when in auto mode */
#define LOW_LIGHT_LEVEL		0x20

/* level at or below which we need to use low light capture mode */
#define HIGH_LIGHT_LEVEL	0xFFFE

#define FIRST_AF_SEARCH_COUNT   80
#define SECOND_AF_SEARCH_COUNT  80
#define AE_STABLE_SEARCH_COUNT  6

#define FIRST_SETTING_FOCUS_MODE_DELAY_MS	100
#define SECOND_SETTING_FOCUS_MODE_DELAY_MS	200



#ifdef CONFIG_VIDEO_S5K5CCGX_DEBUG
enum {
	S5K5CCGX_DEBUG_I2C		= 1U << 0,
	S5K5CCGX_DEBUG_I2C_BURSTS	= 1U << 1,
};
static uint32_t s5k5ccgx_debug_mask = S5K5CCGX_DEBUG_I2C_BURSTS;
module_param_named(debug_mask, s5k5ccgx_debug_mask, uint, S_IWUSR | S_IRUGO);

#define s5k5ccgx_debug(mask, x...) \
	do { \
		if (s5k5ccgx_debug_mask & mask) \
			pr_info(x);	\
	} while (0)
#else

#define s5k5ccgx_debug(mask, x...)
#endif



#define S5K5CCGX_VERSION_1_1	0x11

enum  s5k5ccgx_af_status{
	AUTO_FOCUS_PROGRESS = 5,
	AUTO_FOCUS_DONE = 2,
	AUTO_FOCUS_FAILED = 0,
	AUTO_FOCUS_CANCELLED = 7,
};


enum s5k5ccgx_cammode {
	CAM_CAMEREA = 0,
	CAM_CAMCORDER = 1,
};

enum s5k5ccgx_preview_frame_size {
	S5K5CCGX_PREVIEW_QCIF = 0,	/* 176x144 */
	S5K5CCGX_PREVIEW_320x240,	/* 320x240 */
	S5K5CCGX_PREVIEW_CIF,		/* 352x288 */
	S5K5CCGX_PREVIEW_528x432,	/* 528x432 */	
	S5K5CCGX_PREVIEW_VGA,		/* 640x480 */
#ifdef CONFIG_MACH_SAMSUNG_P5W_KT	// homepad
	S5K5CCGX_PREVIEW_4CIF,		/* 704x576 */
#endif
	S5K5CCGX_PREVIEW_D1,		/* 720x480 */
	S5K5CCGX_PREVIEW_SVGA,		/* 800x600 */
	S5K5CCGX_PREVIEW_XGA,		/* 1024x768*/
	S5K5CCGX_PREVIEW_PVGA,		/* 1280*720*/
	S5K5CCGX_PREVIEW_SXGA,		/* 1280x1024*/		
	S5K5CCGX_PREVIEW_MAX,
};

enum s5k5ccgx_capture_frame_size {
#ifdef CONFIG_MACH_SAMSUNG_P5W_KT	// homepad
	S5K5CCGX_CAPTURE_VGA = 0,	/* 640x480 */
	S5K5CCGX_CAPTURE_1MP,		/* 1280x960 */
	S5K5CCGX_CAPTURE_2MP,		/* UXGA  - 1600x1200 */
	S5K5CCGX_CAPTURE_3MP,		/* QXGA  - 2048x1536 */
	S5K5CCGX_CAPTURE_MAX,
#else
	S5K5CCGX_CAPTURE_VGA = 0,	/* 640x480 */
	S5K5CCGX_CAPTURE_WVGA,		/* 800x480 */
	S5K5CCGX_CAPTURE_SVGA,		/* 800x600 */
	S5K5CCGX_CAPTURE_WSVGA,		/* 1024x600 */
	S5K5CCGX_CAPTURE_1MP,		/* 1280x960 */
	S5K5CCGX_CAPTURE_W1MP,		/* 1600x960 */
	S5K5CCGX_CAPTURE_2MP,		/* UXGA  - 1600x1200 */
	S5K5CCGX_CAPTURE_W2MP,		/* 35mm Academy Offset Standard 1.66 */
	S5K5CCGX_CAPTURE_3MP,		/* QXGA  - 2048x1536 */
	S5K5CCGX_CAPTURE_MAX,
#endif
};



struct s5k5ccgx_framesize {
	u32 index;
	u32 width;
	u32 height;
};

static const struct s5k5ccgx_framesize s5k5ccgx_preview_framesize_list[] = {
	{ S5K5CCGX_PREVIEW_QCIF,	176,  144 },
	{ S5K5CCGX_PREVIEW_320x240,	320,  240 },
	{ S5K5CCGX_PREVIEW_CIF,		352,  288 },
	{ S5K5CCGX_PREVIEW_528x432,	528,  432 },	
	{ S5K5CCGX_PREVIEW_VGA,		640,  480 },
#ifdef CONFIG_MACH_SAMSUNG_P5W_KT	// homepad
	{ S5K5CCGX_PREVIEW_4CIF,	704,  576 },
#endif
	{ S5K5CCGX_PREVIEW_D1,		720,  480 },
	{ S5K5CCGX_PREVIEW_SVGA,	800,  600 },
	{ S5K5CCGX_PREVIEW_XGA,		1024, 768 },
	{ S5K5CCGX_PREVIEW_PVGA,	1280, 720 },
	{ S5K5CCGX_PREVIEW_SXGA,	1280, 1024 },
};

static const struct s5k5ccgx_framesize s5k5ccgx_capture_framesize_list[] = {
	{ S5K5CCGX_CAPTURE_VGA,		640,  480 },
	{ S5K5CCGX_CAPTURE_1MP,		1280, 960 },
	{ S5K5CCGX_CAPTURE_2MP,		1600, 1200 },
	{ S5K5CCGX_CAPTURE_3MP,		2048, 1536 },
};


struct s5k5ccgx_position {
	int x;
	int y;
};


struct s5k5ccgx_regset {
	u32 size;
	u8 *data;
};

#ifdef CONFIG_LOAD_FILE
struct s5k5ccgx_regset_table {
	const u32	*reg;
	int		array_size;
	char		*name;
};

#define S5K5CCGX_REGSET(x, y)		\
	[(x)] = {					\
		.reg		= (y),			\
		.array_size	= ARRAY_SIZE((y)),	\
		.name		= #y,			\
}

#define S5K5CCGX_REGSET_TABLE(y)		\
	{					\
		.reg		= (y),			\
		.array_size	= ARRAY_SIZE((y)),	\
		.name		= #y,			\
}
#else
struct s5k5ccgx_regset_table {
	const u32	*reg;
	int		array_size;
};

#define S5K5CCGX_REGSET(x, y)		\
	[(x)] = {					\
		.reg		= (y),			\
		.array_size	= ARRAY_SIZE((y)),	\
}

#define S5K5CCGX_REGSET_TABLE(y)		\
	{					\
		.reg		= (y),			\
		.array_size	= ARRAY_SIZE((y)),	\
}
#endif
struct s5k5ccgx_regs {
	struct s5k5ccgx_regset_table ev[EXPOSURE_MODE_MAX];
	struct s5k5ccgx_regset_table metering[METERING_MAX];/*added*/
	struct s5k5ccgx_regset_table iso[ISO_MAX];/*added*/
	struct s5k5ccgx_regset_table effect[EFFECT_MODE_MAX];
	struct s5k5ccgx_regset_table white_balance[WB_MODE_MAX];
#ifdef CONFIG_MACH_SAMSUNG_P5
	struct s5k5ccgx_regset_table HD_white_balance[WB_MODE_MAX];
#endif
	struct s5k5ccgx_regset_table preview_size[S5K5CCGX_PREVIEW_MAX];
	struct s5k5ccgx_regset_table capture_size[S5K5CCGX_CAPTURE_MAX];
	struct s5k5ccgx_regset_table scene_mode[SCENE_MODE_MAX];
	struct s5k5ccgx_regset_table saturation[SATURATION_MAX];
	struct s5k5ccgx_regset_table contrast[CONTRAST_MAX];
	struct s5k5ccgx_regset_table sharpness[SHARPNESS_MAX];
	struct s5k5ccgx_regset_table fps[FRAME_RATE_MAX];
	struct s5k5ccgx_regset_table preview_return;
	struct s5k5ccgx_regset_table flash_start;
	struct s5k5ccgx_regset_table flash_end;
	struct s5k5ccgx_regset_table af_assist_flash_start;
	struct s5k5ccgx_regset_table af_assist_flash_end;
	struct s5k5ccgx_regset_table flash_ae_set;
	struct s5k5ccgx_regset_table flash_ae_clear;
	struct s5k5ccgx_regset_table af_low_light_normal_mode_1;
	struct s5k5ccgx_regset_table af_low_light_normal_mode_2;
	struct s5k5ccgx_regset_table af_low_light_normal_mode_3;
	struct s5k5ccgx_regset_table af_low_light_macro_mode_1;
	struct s5k5ccgx_regset_table af_low_light_macro_mode_2;
	struct s5k5ccgx_regset_table af_low_light_macro_mode_3;
	struct s5k5ccgx_regset_table ae_lock_on;
	struct s5k5ccgx_regset_table ae_lock_off;
	struct s5k5ccgx_regset_table awb_lock_on;
	struct s5k5ccgx_regset_table awb_lock_off;
	struct s5k5ccgx_regset_table highlight_cap;
	struct s5k5ccgx_regset_table normal_cap;
	struct s5k5ccgx_regset_table lowlight_cap;
	struct s5k5ccgx_regset_table nightshot_cap;
	struct s5k5ccgx_regset_table wdr_on;
	struct s5k5ccgx_regset_table wdr_off;
	struct s5k5ccgx_regset_table face_detection_on;
	struct s5k5ccgx_regset_table face_detection_off;
	struct s5k5ccgx_regset_table capture_start;
	struct s5k5ccgx_regset_table capture_start_pvga;	
	struct s5k5ccgx_regset_table af_macro_mode;
	struct s5k5ccgx_regset_table af_normal_mode;
	struct s5k5ccgx_regset_table af_return_macro_position;
	struct s5k5ccgx_regset_table hd_first_af_start;
	struct s5k5ccgx_regset_table single_af_start;
	struct s5k5ccgx_regset_table single_af_off;
	struct s5k5ccgx_regset_table dtp_start;
	struct s5k5ccgx_regset_table dtp_stop;
	struct s5k5ccgx_regset_table init_reg_1;
	struct s5k5ccgx_regset_table init_reg_2;
	struct s5k5ccgx_regset_table init_reg_DTP;
	struct s5k5ccgx_regset_table flash_init;
	struct s5k5ccgx_regset_table reset_crop;
	struct s5k5ccgx_regset_table get_ae_stable_status;
	struct s5k5ccgx_regset_table get_light_level;
	struct s5k5ccgx_regset_table get_1st_af_search_status;
	struct s5k5ccgx_regset_table get_2nd_af_search_status;
	struct s5k5ccgx_regset_table get_capture_status;
	struct s5k5ccgx_regset_table get_esd_status;
	struct s5k5ccgx_regset_table get_iso;
	struct s5k5ccgx_regset_table get_ae_stable;
	struct s5k5ccgx_regset_table get_shutterspeed;
	struct s5k5ccgx_regset_table auto_contrast_off;
	struct s5k5ccgx_regset_table auto_contrast_on;
	struct s5k5ccgx_regset_table update_preview_setting;
};

static const struct s5k5ccgx_regs regs_for_fw_version_1_1 = {
	.ev = {
		S5K5CCGX_REGSET(EXPOSURE_M4, s5k5ccgx_brightness_m_4),
		S5K5CCGX_REGSET(EXPOSURE_M3, s5k5ccgx_brightness_m_3),
		S5K5CCGX_REGSET(EXPOSURE_M2, s5k5ccgx_brightness_m_2),
		S5K5CCGX_REGSET(EXPOSURE_M1, s5k5ccgx_brightness_m_1),
		S5K5CCGX_REGSET(EXPOSURE_ZERO, s5k5ccgx_brightness_0),
		S5K5CCGX_REGSET(EXPOSURE_P1, s5k5ccgx_brightness_p_1),
		S5K5CCGX_REGSET(EXPOSURE_P2, s5k5ccgx_brightness_p_2),
		S5K5CCGX_REGSET(EXPOSURE_P3, s5k5ccgx_brightness_p_3),
		S5K5CCGX_REGSET(EXPOSURE_P4, s5k5ccgx_brightness_p_4),		
	},
	.metering = {
		S5K5CCGX_REGSET(METERING_MATRIX, s5k5ccgx_metering_normal),
		S5K5CCGX_REGSET(METERING_CENTER, s5k5ccgx_metering_center),
		S5K5CCGX_REGSET(METERING_SPOT, s5k5ccgx_metering_spot),
	},
	.iso = {
		S5K5CCGX_REGSET(ISO_AUTO, s5k5ccgx_iso_auto),
		S5K5CCGX_REGSET(ISO_100, s5k5ccgx_iso_100),
		S5K5CCGX_REGSET(ISO_200, s5k5ccgx_iso_200),
		S5K5CCGX_REGSET(ISO_400, s5k5ccgx_iso_400),
	},

	.effect = {
		S5K5CCGX_REGSET(EFFECT_NONE, s5k5ccgx_effect_off),
		S5K5CCGX_REGSET(EFFECT_MONO, s5k5ccgx_effect_mono),
		S5K5CCGX_REGSET(EFFECT_SEPIA, s5k5ccgx_effect_sepia),
		S5K5CCGX_REGSET(EFFECT_NEGATIVE, s5k5ccgx_effect_negative),
	},
	.white_balance = {
		S5K5CCGX_REGSET(WB_AUTO, s5k5ccgx_wb_auto),
		S5K5CCGX_REGSET(WB_DAYLIGHT, s5k5ccgx_wb_daylight),
		S5K5CCGX_REGSET(WB_CLOUDY, s5k5ccgx_wb_cloudy),
		S5K5CCGX_REGSET(WB_INCANDESCENT, s5k5ccgx_wb_incandescent),
		S5K5CCGX_REGSET(WB_FLUORESCENT, s5k5ccgx_wb_fluorescent),
	},
#ifdef CONFIG_MACH_SAMSUNG_P5
	.HD_white_balance = {
		S5K5CCGX_REGSET(WB_AUTO, s5k5ccgx_wb_auto_HD_Camera),
		S5K5CCGX_REGSET(WB_DAYLIGHT, s5k5ccgx_wb_daylight_HD_Camera),
		S5K5CCGX_REGSET(WB_CLOUDY, s5k5ccgx_wb_cloudy_HD_Camera),
		S5K5CCGX_REGSET(WB_INCANDESCENT, s5k5ccgx_wb_incandescent_HD_Camera),
		S5K5CCGX_REGSET(WB_FLUORESCENT, s5k5ccgx_wb_fluorescent_HD_Camera),
	},
#endif
	.scene_mode = {
		S5K5CCGX_REGSET(SCENE_AUTO, s5k5ccgx_scene_off),
		S5K5CCGX_REGSET(SCENE_PORTRAIT, s5k5ccgx_scene_portrait),
		S5K5CCGX_REGSET(SCENE_NIGHT, s5k5ccgx_scene_nightshot),
		S5K5CCGX_REGSET(SCENE_LANDSCAPE, s5k5ccgx_scene_landscape),
		S5K5CCGX_REGSET(SCENE_SPORTS, s5k5ccgx_scene_sports),
		S5K5CCGX_REGSET(SCENE_PARTY, s5k5ccgx_scene_party),
		S5K5CCGX_REGSET(SCENE_BEACH, s5k5ccgx_scene_beach),
		S5K5CCGX_REGSET(SCENE_SUNSET, s5k5ccgx_scene_sunset),
		S5K5CCGX_REGSET(SCENE_FIRE_WORK, s5k5ccgx_scene_firework),
		S5K5CCGX_REGSET(SCENE_CANDLE_LIGHT, s5k5ccgx_scene_candle),
		S5K5CCGX_REGSET(SCENE_DUSKDAWN, s5k5ccgx_scene_dawn),
		S5K5CCGX_REGSET(SCENE_TEXT, s5k5ccgx_scene_text),
	},

	.saturation = {
		S5K5CCGX_REGSET(SATURATION_MINUS_2, s5k5ccgx_saturation_m_2),
		S5K5CCGX_REGSET(SATURATION_MINUS_1, s5k5ccgx_saturation_m_1),
		S5K5CCGX_REGSET(SATURATION_DEFAULT, s5k5ccgx_saturation_0),
		S5K5CCGX_REGSET(SATURATION_PLUS_1, s5k5ccgx_saturation_p_1),
		S5K5CCGX_REGSET(SATURATION_PLUS_2, s5k5ccgx_saturation_p_2),
	},
	.contrast = {
		S5K5CCGX_REGSET(CONTRAST_MINUS_2, s5k5ccgx_contrast_m_2),
		S5K5CCGX_REGSET(CONTRAST_MINUS_1, s5k5ccgx_contrast_m_1),
		S5K5CCGX_REGSET(CONTRAST_DEFAULT, s5k5ccgx_contrast_0),
		S5K5CCGX_REGSET(CONTRAST_PLUS_1, s5k5ccgx_contrast_p_1),
		S5K5CCGX_REGSET(CONTRAST_PLUS_2, s5k5ccgx_contrast_p_2),
	},
	.sharpness = {
		S5K5CCGX_REGSET(SHARPNESS_MINUS_2, s5k5ccgx_sharpness_m_2),
		S5K5CCGX_REGSET(SHARPNESS_MINUS_1, s5k5ccgx_sharpness_m_1),
		S5K5CCGX_REGSET(SHARPNESS_DEFAULT, s5k5ccgx_sharpness_0),
		S5K5CCGX_REGSET(SHARPNESS_PLUS_1, s5k5ccgx_sharpness_p_1),
		S5K5CCGX_REGSET(SHARPNESS_PLUS_2, s5k5ccgx_sharpness_p_2),
	},
	.fps = {
		S5K5CCGX_REGSET(FRAME_RATE_AUTO, s5k5ccgx_fps_auto),
		S5K5CCGX_REGSET(FRAME_RATE_15, s5k5ccgx_fps_15fix),
		S5K5CCGX_REGSET(FRAME_RATE_30, s5k5ccgx_fps_30fix),
	},

	.preview_return = S5K5CCGX_REGSET_TABLE(s5k5ccgx_preview),

	.flash_start = S5K5CCGX_REGSET_TABLE(s5k5ccgx_mainflash_start),
	.flash_end = S5K5CCGX_REGSET_TABLE(s5k5ccgx_mainflash_end),
	.af_assist_flash_start =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_preflash_start),
	.af_assist_flash_end =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_preflash_end),
	.flash_ae_set =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_flash_ae_set),
	.flash_ae_clear =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_flash_ae_clear),
#if 0
	.af_low_light_normal_mode_1 =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_AF_Low_Light_normal_mode_1_EVT1),
	.af_low_light_normal_mode_2 =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_AF_Low_Light_normal_mode_2_EVT1),
	.af_low_light_normal_mode_3 =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_AF_Low_Light_normal_mode_3_EVT1),
	.af_low_light_macro_mode_1 =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_AF_Low_Light_Macro_mode_1_EVT1),
	.af_low_light_macro_mode_2 =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_AF_Low_Light_Macro_mode_2_EVT1),
	.af_low_light_macro_mode_3 =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_AF_Low_Light_Macro_mode_3_EVT1),
	/*.af_low_light_mode_off =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_AF_Low_Light_Mode_Off_EVT1),*/
#endif
	.ae_lock_on =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_ae_lock),
	.ae_lock_off =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_ae_unlock),
	.awb_lock_on =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_awb_lock),
	.awb_lock_off =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_awb_unlock),

	.highlight_cap = S5K5CCGX_REGSET_TABLE(s5k5ccgx_highlight_snapshot),
	.lowlight_cap = S5K5CCGX_REGSET_TABLE(s5k5ccgx_lowlight_snapshot),
	.nightshot_cap = S5K5CCGX_REGSET_TABLE(s5k5ccgx_night_snapshot),
#if 0
	.wdr_on = S5K5CCGX_REGSET_TABLE(s5k5ccgx_WDR_on_EVT1),
	.wdr_off = S5K5CCGX_REGSET_TABLE(s5k5ccgx_WDR_off_EVT1),
	.face_detection_on = S5K5CCGX_REGSET_TABLE(s5k5ccgx_Face_Detection_On_EVT1),
	.face_detection_off =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_Face_Detection_Off_EVT1),
#endif
	.af_macro_mode = S5K5CCGX_REGSET_TABLE(s5k5ccgx_af_macro_on),
	.af_normal_mode = S5K5CCGX_REGSET_TABLE(s5k5ccgx_af_normal_on),
#if 0
	.af_return_macro_position =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_AF_Return_Macro_pos_EVT1),
#endif
	.hd_first_af_start = S5K5CCGX_REGSET_TABLE(s5k5ccgx_1st_720P_af_do),
	.single_af_start = S5K5CCGX_REGSET_TABLE(s5k5ccgx_af_do),
	.single_af_off = S5K5CCGX_REGSET_TABLE(s5k5ccgx_af_off),
	.dtp_start = S5K5CCGX_REGSET_TABLE(s5k5ccgx_dtp_on),
	.dtp_stop = S5K5CCGX_REGSET_TABLE(s5k5ccgx_dtp_off),

	.preview_size = {
		S5K5CCGX_REGSET(S5K5CCGX_PREVIEW_QCIF, s5k5ccgx_176_144_Preview),
		S5K5CCGX_REGSET(S5K5CCGX_PREVIEW_320x240, s5k5ccgx_320_240_Preview),
		S5K5CCGX_REGSET(S5K5CCGX_PREVIEW_CIF, s5k5ccgx_352_288_Preview),
		S5K5CCGX_REGSET(S5K5CCGX_PREVIEW_528x432, s5k5ccgx_528_432_Preview),		
		S5K5CCGX_REGSET(S5K5CCGX_PREVIEW_VGA, s5k5ccgx_640_480_Preview),
#ifdef CONFIG_MACH_SAMSUNG_P5W_KT	// homepad
		S5K5CCGX_REGSET(S5K5CCGX_PREVIEW_4CIF, s5k5ccgx_704_576_Preview),
#endif
		S5K5CCGX_REGSET(S5K5CCGX_PREVIEW_D1, s5k5ccgx_720_480_Preview),
		S5K5CCGX_REGSET(S5K5CCGX_PREVIEW_SVGA, s5k5ccgx_800_600_Preview),
		S5K5CCGX_REGSET(S5K5CCGX_PREVIEW_XGA, s5k5ccgx_1024_768_Preview),
		S5K5CCGX_REGSET(S5K5CCGX_PREVIEW_SXGA, s5k5ccgx_1280_1024_Preview),		
		S5K5CCGX_REGSET(S5K5CCGX_PREVIEW_PVGA, s5k5ccgx_1280_720_Preview),
	},
	.capture_start = S5K5CCGX_REGSET_TABLE(s5k5ccgx_snapshot),
	.capture_start_pvga = S5K5CCGX_REGSET_TABLE(s5k5ccgx_snapshot_pvga),	
	.init_reg_1 = S5K5CCGX_REGSET_TABLE(s5k5ccgx_pre_init0),
	.init_reg_2 = S5K5CCGX_REGSET_TABLE(s5k5ccgx_init0),
	.init_reg_DTP = S5K5CCGX_REGSET_TABLE(s5k5ccgx_DTP_init0),
	.get_light_level = S5K5CCGX_REGSET_TABLE(s5k5ccgx_get_light_status),
#if 0
	/*.get_ae_stable_status =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_Get_AE_Stable_Status_EVT1),*/
	.get_light_level = S5K5CCGX_REGSET_TABLE(s5k5ccgx_get_light_status),
	/*.get_1st_af_search_status =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_get_1st_af_search_status_EVT1),
	.get_2nd_af_search_status =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_get_2nd_af_search_status_EVT1),*/
	/*.get_capture_status =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_get_capture_status_EVT1),*/
	/*.get_esd_status = S5K5CCGX_REGSET_TABLE(s5k5ccgx_get_esd_status_EVT1),
	.get_iso = S5K5CCGX_REGSET_TABLE(s5k5ccgx_get_iso_reg_EVT1),
	.get_shutterspeed =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_get_shutterspeed_reg_EVT1),*/
	.auto_contrast_off =  S5K5CCGX_REGSET_TABLE(s5k5ccgx_Auto_Contrast_OFF_EVT1),
	.auto_contrast_on =  S5K5CCGX_REGSET_TABLE(s5k5ccgx_Auto_Contrast_ON_EVT1),
#endif
	.get_iso = S5K5CCGX_REGSET_TABLE(s5k5ccgx_get_iso_reg),
	.get_ae_stable = S5K5CCGX_REGSET_TABLE(s5k5ccgx_get_ae_stable_reg),
	.get_shutterspeed =
		S5K5CCGX_REGSET_TABLE(s5k5ccgx_get_shutterspeed_reg),
	.update_preview_setting = S5K5CCGX_REGSET_TABLE(s5k5ccgx_update_preview_setting),
};

/*share with hal*/
struct camera_cur_parm {
	/*enum s5k5ccgx_dtp_test			mode_dtp;*/
	enum s5k5ccgx_scene_mode		mode_scene;
	enum s5k5ccgx_focus_mode		mode_focus;
	enum s5k5ccgx_color_effect		mode_effect;
	enum s5k5ccgx_white_balance		mode_wb;
	enum s5k5ccgx_flash_mode			mode_flash;
	enum s5k5ccgx_exposure			mode_exposure;
	enum s5k5ccgx_metering_mode		mode_metering;
	enum s5k5ccgx_autocontrast		mode_auto_contrast;
	enum s5k5ccgx_iso_mode			mode_iso;
};

/*in just driver*/
struct s5k5ccgx_state {
	struct s5k5ccgx_platform_data *pdata;
	struct s5k5ccgx_position position;
	struct mutex ctrl_lock;
	struct completion af_complete;
	struct i2c_client *i2c_client;
	enum s5k5ccgx_cammode;
	enum s5k5ccgx_autofocus_control;
	enum s5k5ccgx_af_status;
	enum s5k5ccgx_mode_info lastmode_info;
	int preview_framesize_index;
	int lastPreview_framesize_index;
	int capture_framesize_index;
	bool sensor_af_in_low_light_mode;
	bool initialized;
	int one_frame_delay_ms;
	int af_status;
	bool flash_on;
	bool torch_on;
	const struct s5k5ccgx_regs *regs;
	struct camera_cur_parm parms;
	bool power_status;
	struct s5k5ccgx_exif_info exif_info;
	struct s5k5ccgx_mode state_mode;
	bool esd_status;	
	//modify to TouchAF by Teddy 
	bool touchaf_enable;    
	bool bHD_enable;           
	int bCammode;
	bool isAFCancel;
#ifdef CONFIG_MACH_SAMSUNG_P5W_KT	//devide internal and market app : goggles, QRcode, etc..
	int bAppmode;
#endif
};

static struct s5k5ccgx_state *state;
#ifdef CONFIG_MACH_SAMSUNG_P5
extern struct i2c_client *i2c_client_pmic;
#endif

/**
 * s5k5ccgx_i2c_read_twobyte: Read 2 bytes from sensor
 */
static int s5k5ccgx_i2c_read_twobyte(struct i2c_client *client,
				  u16 subaddr, u16 *data)
{
	int err;
	unsigned char buf[2];
	struct i2c_msg msg[2];

	cpu_to_be16s(&subaddr);

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = (u8 *)&subaddr;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 2;
	msg[1].buf = buf;

	err = i2c_transfer(client->adapter, msg, 2);
	if (unlikely(err != 2)) {
		dev_err(&state->i2c_client->dev, "%s: register read fail\n", __func__);
		printk("%s: register read fail\n", __func__);
		return -EIO;
	}

	*data = ((buf[0] << 8) | buf[1]);

	return 0;
}

/**
 * s5k5ccgx_i2c_write_twobyte: Write (I2C) multiple bytes to the camera sensor
 * @client: pointer to i2c_client
 * @cmd: command register
 * @w_data: data to be written
 * @w_len: length of data to be written
 *
 * Returns 0 on success, <0 on error
 */
static int s5k5ccgx_i2c_write_twobyte(struct i2c_client *client,
					 u16 addr, u16 w_data)
{
	int retry_count = 5;
	unsigned char buf[4];
	struct i2c_msg msg = {client->addr, 0, 4, buf};
	int ret = 0;
	if (addr == 0xFFFF) {
		pr_info("s5k5ccgx_i2c_write_twobyte give delay: %d\n", w_data);
		msleep(w_data);
	} else {
		buf[0] = addr >> 8;
		buf[1] = addr;
		buf[2] = w_data >> 8;
		buf[3] = w_data & 0xff;
		do {
			ret = i2c_transfer(client->adapter, &msg, 1);
			if (likely(ret == 1))
				break;
			msleep(POLL_TIME_MS);
			dev_err(&state->i2c_client->dev, "%s: I2C err %d, retry %d.\n",
				__func__, ret, retry_count);
		} while (retry_count-- > 0);
		if (ret != 1) {
			dev_err(&state->i2c_client->dev, "%s: I2C is not working\n", __func__);
			return -EIO;
		}
	}

	return 0;
}

#ifdef CONFIG_MACH_SAMSUNG_P5
int s5k5ccgx_write_reg8(struct i2c_client *client, u8 addr, u8 val)
{
	int err;
	struct i2c_msg msg;
	unsigned char data[2];
	//struct i2c_msg msg = {client->addr, 0, 2, data};
	int retry = 0;
	int retry_count = 5;

	if (!client->adapter) {
		return -ENODEV;
	}

	data[0] = addr;
	data[1] = val;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = 2;
	msg.buf = data;

	do {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (likely(err == 1))
			break;//return 0;
		retry++;
		msleep(POLL_TIME_MS);
		pr_err("i2c transfer failed, retrying %x %x err = %d \n",
				addr, val, err);
		msleep(3);
	} while (retry <= retry_count);

	if (err != 1) {
		pr_err("%s: I2C is not working.\n", __func__);
		return -EIO;
	}

	return 0;
}
extern int s5k5ccgx_write_regs_pm(struct i2c_client *client, const u16 regs[],
			     int size)
{
	int i, err;

	for (i = 0; i < size; i++) {
		
		err = s5k5ccgx_write_reg8(client,
			((regs[i] & 0xFF00) >> 8), regs[i] & 0x00FF);
		if (unlikely(err != 0)) {
			pr_err("%s: register write failed\n", __func__);
			return err;
		}
	}

	return 0;
}
#endif

#ifdef CONFIG_LOAD_FILE
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
/*#include <asm/uaccess.h>*/

static char *s5k5ccgx_regs_table;

static int s5k5ccgx_regs_table_size;

int s5k5ccgx_regs_table_init(int rev)
{
	struct file *filp;
	char *dp;
	long l;
	loff_t pos;
	int ret;
	mm_segment_t fs = get_fs();

	set_fs(get_ds());

	filp = filp_open("/mnt/sdcard/s5k5ccgx_regs_p5.h", O_RDONLY, 0);

	if (IS_ERR_OR_NULL(filp)) {
		printk(KERN_DEBUG "file open error\n");
		return PTR_ERR(filp);
	}

	l = filp->f_path.dentry->d_inode->i_size;
	printk(KERN_DEBUG "l = %ld\n", l);
	dp = kmalloc(l, GFP_KERNEL);
	/*dp = vmalloc(l);	*/
	if (dp == NULL) {
		printk(KERN_DEBUG "Out of Memory\n");
		filp_close(filp, current->files);
	}

	pos = 0;
	memset(dp, 0, l);
	ret = vfs_read(filp, (char __user *)dp, l, &pos);

	if (ret != l) {
		printk(KERN_DEBUG "Failed to read file ret = %d\n", ret);
		/*kfree(dp);*/
		vfree(dp);
		filp_close(filp, current->files);
		return -EINVAL;
	}

	filp_close(filp, current->files);

	set_fs(fs);

	s5k5ccgx_regs_table = dp;

	s5k5ccgx_regs_table_size = l;

	*((s5k5ccgx_regs_table + s5k5ccgx_regs_table_size) - 1) = '\0';

	printk(KERN_DEBUG "s5k5ccgx_regs_table 0x%p, %ld\n", dp, l);

	return 0;
}

void s5k5ccgx_regs_table_exit(void)
{
	if (s5k5ccgx_regs_table) {
		s5k5ccgx_regs_table = NULL;
		kfree(s5k5ccgx_regs_table);
	}
}
static int s5k5ccgx_regs_table_write(struct i2c_client *client, char *name)
{
	char *start, *end, *reg;
	unsigned int value;
	char reg_buf[11];

	*(reg_buf + 10) = '\0';

	start = strstr(s5k5ccgx_regs_table, name);
	end = strstr(start, "};");

	while (1) {
		/* Find Address */
		reg = strstr(start, "0x");
		if (reg)
			start = strstr(reg, "\n");

		if ((reg == NULL) || (reg > end))
			break;
		/* Write Value to Address */
		if (reg != NULL)  {
			memcpy(reg_buf, (reg), 10);
			value =
				(unsigned int)simple_strtoul(reg_buf,
						NULL, 16);

			/*	printk("==== value 0x%08x=======\n", value); */

			s5k5ccgx_i2c_write_twobyte(client,
					((value >> 16) & 0xFFFF), (value & 0xFFFF));
		}
	}

	return 0;
}

static int s5k5ccgx_write_regs(struct i2c_client *client, const u32 regs[],
			     int size, char *name)
{
	int err;

	err = s5k5ccgx_regs_table_write(state->i2c_client, name);
	if (unlikely(err < 0)) {
		pr_err("%s: s5k5ccgx_regs_table_write failed\n", __func__);
		return err;
	}
	return 0;
}
#else
static int s5k5ccgx_write_regs(struct i2c_client *client, const u32 regs[],
			     int size)
{
	int i, err;
	for (i = 0; i < size; i++) {
		err = s5k5ccgx_i2c_write_twobyte(client,
			(regs[i] >> 16), regs[i]);

		if (unlikely(err != 0)) {
			dev_err(&state->i2c_client->dev, "%s: register write failed\n", __func__);
			return err;
		}
	}

	return 0;
}
#endif

static int s5k5ccgx_set_from_table(struct i2c_client *client,
				const char *setting_name,
				const struct s5k5ccgx_regset_table *table,
				int table_size, int index)
{
	pr_info("set : %s   index : %d....\n", setting_name, index);

	if ((index < 0) || (index >= table_size)) {
		dev_err(&state->i2c_client->dev, "%s: index(%d) out of range[0:%d] for table for %s\n",
			__func__, index, table_size, setting_name);
		return -EINVAL;
	}

	table += index;
	if (table->reg == NULL)
		return -EINVAL;
#ifdef CONFIG_LOAD_FILE
	return s5k5ccgx_write_regs(client, table->reg, table->array_size, table->name);
#else
	return s5k5ccgx_write_regs(client, table->reg, table->array_size);
#endif
}

static u16 s5k5ccgx_get_ae_stable()
{
	int err;
	u16 read_value = 0;

	err = s5k5ccgx_set_from_table(state->i2c_client, "get ae stable", &state->regs->get_ae_stable, 1, 0);
	if (err) {
		dev_err(&state->i2c_client->dev, "%s: write cmd failed, returning 0\n", __func__);
		goto out;
	}
	err = s5k5ccgx_i2c_read_twobyte(state->i2c_client, 0x0F12, &read_value);
	if (err) {
		dev_err(&state->i2c_client->dev, "%s: read cmd failed, returning 0\n", __func__);
		goto out;
	}

	PCAM_DEBUG("read_value =  0x%X", read_value);
	return read_value;

out:
	/* restore write mode */
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x0028, 0x7000);
	return read_value;
}

static u16 s5k5ccgx_get_iso()
{
	int err;
	u16 read_value = 0;

	err = s5k5ccgx_set_from_table(state->i2c_client, "get iso", &state->regs->get_iso, 1, 0);
	if (err) {
		dev_err(&state->i2c_client->dev, "%s: write cmd failed, returning 0\n", __func__);
		goto out;
	}
	err = s5k5ccgx_i2c_read_twobyte(state->i2c_client, 0x0F12, &read_value);
	if (err) {
		dev_err(&state->i2c_client->dev, "%s: read cmd failed, returning 0\n", __func__);
		goto out;
	}

	PCAM_DEBUG("read_value =  0x%X", read_value);
	return read_value;

out:
	/* restore write mode */
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x0028, 0x7000);
	return read_value;
}

static u32 s5k5ccgx_get_shutter_speed()
{
	int err;
	u16 read_value_lsb = 0;
	u16 read_value_msb = 0;

	err = s5k5ccgx_set_from_table(state->i2c_client, "get shutter speed", &state->regs->get_shutterspeed, 1, 0);
	if (err) {
		dev_err(&state->i2c_client->dev, "%s: write cmd failed, returning 0\n", __func__);
		goto out;
	}
	err = s5k5ccgx_i2c_read_twobyte(state->i2c_client, 0x0F12, &read_value_lsb);
	err = s5k5ccgx_i2c_read_twobyte(state->i2c_client, 0x0F12, &read_value_msb);
	if (err) {
		dev_err(&state->i2c_client->dev, "%s: read cmd failed, returning 0\n", __func__);
		goto out;
	}

	PCAM_DEBUG("read_value =  0x%X", read_value_lsb+(read_value_msb<<16));
	return read_value_lsb + (read_value_msb<<16);

out:
	/* restore write mode */
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x0028, 0x7000);
	return read_value_lsb + (read_value_msb<<16);
}

static u32 s5k5ccgx_get_light_level()
{
	int err;
	u16 read_value_lsb = 0;
	u16 read_value_msb = 0;


	err = s5k5ccgx_set_from_table(state->i2c_client, "get light level", &state->regs->get_light_level, 1, 0);
	if (err) {
		dev_err(&state->i2c_client->dev, "%s: write cmd failed, returning 0\n", __func__);
		goto out;
	}
	err = s5k5ccgx_i2c_read_twobyte(state->i2c_client, 0x0F12, &read_value_lsb);
	err = s5k5ccgx_i2c_read_twobyte(state->i2c_client, 0x0F12, &read_value_msb);
	if (err) {
		dev_err(&state->i2c_client->dev, "%s: read cmd failed, returning 0\n", __func__);
		goto out;
	}

	PCAM_DEBUG("read_value =  0x%X", read_value_lsb+(read_value_msb<<16));
	return read_value_lsb + (read_value_msb<<16);

out:
	/* restore write mode */
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x0028, 0x7000);
	return read_value_lsb + (read_value_msb<<16);
}

static int s5k5ccgx_set_capture_size()
{
	int err;

	dev_dbg(&state->i2c_client->dev, "%s: index:%d\n", __func__,
		state->capture_framesize_index);

	err = s5k5ccgx_set_from_table(state->i2c_client, "capture_size",
				state->regs->capture_size,
				ARRAY_SIZE(state->regs->capture_size),
				state->capture_framesize_index);
	if (err < 0) {
		dev_err(&state->i2c_client->dev,
			"%s: failed: i2c_write for capture_size index %d\n",
			__func__, state->capture_framesize_index);
	}

	return err;
}

//modify to TouchAF by Teddy
static int s5k5ccgx_reset_AF_region()
{
	u16 mapped_x = 512;
	u16 mapped_y = 384;
	u16 inner_window_start_x = 0;
	u16 inner_window_start_y = 0;
	u16 outer_window_start_x = 0;
	u16 outer_window_start_y = 0;

	state->touchaf_enable = false;
	// mapping the touch position on the sensor display
	mapped_x = (mapped_x * 1024) / 1066;
	mapped_y = (mapped_y * 768) / 800;
	printk("\n\n%s : mapped xPos = %d, mapped yPos = %d\n\n", __func__, mapped_x, mapped_y);

	inner_window_start_x    = mapped_x - (INNER_WINDOW_WIDTH_1024_768 / 2);
	outer_window_start_x    = mapped_x - (OUTER_WINDOW_WIDTH_1024_768 / 2);
	printk("\n\n%s : boxes are in the sensor window. in_Sx = %d, out_Sx= %d\n\n", __func__, inner_window_start_x, outer_window_start_x);

	inner_window_start_y    = mapped_y - (INNER_WINDOW_HEIGHT_1024_768 / 2);
	outer_window_start_y    = mapped_y - (OUTER_WINDOW_HEIGHT_1024_768 / 2);
	printk("\n\n%s : boxes are in the sensor window. in_Sy = %d, out_Sy= %d\n\n", __func__, inner_window_start_y, outer_window_start_y);

	//calculate the start position value
	inner_window_start_x = inner_window_start_x * 1024 /1024;
	outer_window_start_x = outer_window_start_x * 1024 / 1024;
	inner_window_start_y = inner_window_start_y * 1024 / 768;
	outer_window_start_y = outer_window_start_y * 1024 / 768;
	printk("\n\n%s : calculated value inner_window_start_x = %d\n\n", __func__, inner_window_start_x);
	printk("\n\n%s : calculated value inner_window_start_y = %d\n\n", __func__, inner_window_start_y);
	printk("\n\n%s : calculated value outer_window_start_x = %d\n\n", __func__, outer_window_start_x);
	printk("\n\n%s : calculated value outer_window_start_y = %d\n\n", __func__, outer_window_start_y);

	//Write register
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x0028, 0x7000);

	// inner_window_start_x
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x002A, 0x0234);
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x0F12, inner_window_start_x);

	// outer_window_start_x
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x002A, 0x022C);
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x0F12, outer_window_start_x);

	// inner_window_start_y
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x002A, 0x0236);
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x0F12, inner_window_start_y);

	// outer_window_start_y
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x002A, 0x022E);
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x0F12, outer_window_start_y);

	// Update AF window
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x002A, 0x023C);
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x0F12, 0x0001);

	return 0;
}

static int s5k5ccgx_start_capture(struct s5k5ccgx_state *state, struct s5k5ccgx_mode *mode)
{
	int err;
	u32 light_level;
	dev_dbg(&state->i2c_client->dev, "%s:start\n", __func__);
	printk("\n\n%s : %d\n\n", __func__, state->parms.mode_flash);

	light_level = s5k5ccgx_get_light_level();
	PCAM_DEBUG("light_level = 0x%x", light_level);

	s5k5ccgx_set_from_table(state->i2c_client, "ae lock off",
				&state->regs->ae_lock_off, 1, 0);
	s5k5ccgx_set_from_table(state->i2c_client, "awb lock off",
				&state->regs->awb_lock_off, 1, 0);

	state->exif_info.info_flash = 0;

	if ((state->parms.mode_scene != SCENE_NIGHT)&&(state->parms.mode_scene != SCENE_FIRE_WORK)) {
		switch (state->parms.mode_flash) {
		case FLASH_AUTO:
			if (light_level > LOW_LIGHT_LEVEL) {
				/* light level bright enough
				 * that we don't need flash
				 */
				break;
			}
			/* fall through to flash start */
		case FLASH_ON:
			/*if (state->parms.mode_focus == FOCUS_INFINITY) {
				s5k5ccgx_set_from_table(state->i2c_client,
					"AF assist flash start",
					&state->regs->af_assist_flash_start,
					1, 0);
				s5k5ccgx_set_from_table(state->i2c_client,
					"AF assist flash end",
					&state->regs->af_assist_flash_end,
					1, 0);
				msleep(10);
			}*/
			s5k5ccgx_set_from_table(state->i2c_client, "flash start",
					&state->regs->flash_start, 1, 0);
			state->exif_info.info_flash = 1;
			state->flash_on = true;
			state->pdata->flash_onoff(1);
			/*msleep(200);*/
			break;
		default:
			break;
		}
	}
#if 0 /*always 2048x1536*/
	err = s5k5ccgx_set_capture_size();
	if (err < 0) {
		dev_err(&state->i2c_client->dev,
			"%s: failed: i2c_write for capture_resolution\n",
			__func__);
		return -EIO;
	}
#endif
	if (mode->xres == 2048 && mode->yres == 1536){
		s5k5ccgx_set_from_table(state->i2c_client, "capture start",
			&state->regs->capture_start, 1, 0);
		if (state->parms.mode_scene == SCENE_NIGHT){
			msleep(140);
			printk("msleep -------------- 140ms \n");
		}
		else if(state->parms.mode_scene == SCENE_FIRE_WORK){
			if (light_level > LOW_LIGHT_LEVEL){
				msleep(650);
				printk("msleep -------------- fire work --650ms \n");				
			}
			else{
				msleep(460);
				printk("msleep -------------- fire work --  460ms \n");
			}
		}
	}
	else if (mode->xres == 1024 && mode->yres == 768){
		s5k5ccgx_set_from_table(state->i2c_client, "capture start pvga",
			&state->regs->capture_start_pvga, 1, 0);
		if (state->parms.mode_scene == SCENE_NIGHT){
			msleep(140);
			printk("msleep -------------- 140ms \n");
		}
		else if(state->parms.mode_scene == SCENE_FIRE_WORK){
			if (light_level > LOW_LIGHT_LEVEL){
				msleep(650);
				printk("msleep -------------- fire work --650ms \n");				
			}
			else{
				msleep(460);
				printk("msleep -------------- fire work --  460ms \n");
			}
		}
	}	
	else {
		err = s5k5ccgx_set_from_table(state->i2c_client, "preview_size",
			&state->regs->preview_size, ARRAY_SIZE(state->regs->preview_size), state->preview_framesize_index);
		if (err < 0) {
			dev_err(&state->i2c_client->dev, "%s: failed: Could not set preview size\n", __func__);
			return -EIO;
		}
		err = s5k5ccgx_set_from_table(state->i2c_client, "update preview setting",
			&state->regs->update_preview_setting, 1, 0);
		if (err < 0) {
			dev_err(&state->i2c_client->dev, "%s: failed: Could not set preview update\n", __func__);
			return -EIO;
		}
	}

	/* a shot takes takes at least 50ms so sleep that amount first
	* and then start polling for completion.
	*/
	/*
	if (light_level > LOW_LIGHT_LEVEL)
		msleep(50);
	else
		msleep(150);
	*/
		//msleep(200); //ykh

/*msleep(NORMAL_MODE_MAX_ONE_FRAME_DELAY_MS*2);*/
#if 0 /*move to ioctl*/
	if ((state->parms.mode_scene != SCENE_NIGHT) && (state->flash_on)) {
		state->flash_on = false;
		state->pdata->flash_onoff(0);
		s5k5ccgx_set_from_table(state->i2c_client, "flash end",
					&state->regs->flash_end, 1, 0);
	}
#endif
	return 0;

}

/* wide dynamic range support */
static int s5k5ccgx_set_wdr(struct i2c_client *client, int value)
{
	if (value == WDR_ON)
		return s5k5ccgx_set_from_table(state->i2c_client, "wdr on",
					&state->regs->wdr_on, 1, 0);
	return s5k5ccgx_set_from_table(state->i2c_client, "wdr off",
				&state->regs->wdr_off, 1, 0);
}

static int s5k5ccgx_set_face_detection(struct i2c_client *client, int value)
{

	if (value == FACE_DETECTION_ON)
		return s5k5ccgx_set_from_table(state->i2c_client, "face detection on",
				&state->regs->face_detection_on, 1, 0);
	return s5k5ccgx_set_from_table(state->i2c_client, "face detection off",
				&state->regs->face_detection_off, 1, 0);
}

static int s5k5ccgx_return_focus(void)
{
	int err;
#if 0
	err = s5k5ccgx_set_from_table(state->i2c_client,
		"af normal mode 1",
		&state->regs->af_normal_mode_1, 1, 0);
	if (err < 0)
		goto fail;
	msleep(FIRST_SETTING_FOCUS_MODE_DELAY_MS);
	err = s5k5ccgx_set_from_table(state->i2c_client,
		"af normal mode 2",
		&state->regs->af_normal_mode_2, 1, 0);
	if (err < 0)
		goto fail;
	msleep(SECOND_SETTING_FOCUS_MODE_DELAY_MS);
	err = s5k5ccgx_set_from_table(state->i2c_client,
		"af normal mode 3",
		&state->regs->af_normal_mode_3, 1, 0);
	if (err < 0)
		goto fail;
#else
	err = s5k5ccgx_set_from_table(state->i2c_client,
		"af normal mode",
		&state->regs->af_normal_mode, 1, 0);
	if (err < 0)
		goto fail;
#endif

	return 0;
fail:
	dev_err(&state->i2c_client->dev,
		"%s: i2c_write failed\n", __func__);
	return -EIO;
}

static int s5k5ccgx_set_af_low_light()
{
	int err;
	if (state->parms.mode_focus == FOCUS_MACRO) {
		dev_dbg(&state->i2c_client->dev,
				"%s: FOCUS_MODE_MACRO\n", __func__);
		err = s5k5ccgx_set_from_table(state->i2c_client, "af low light macro mode 1",
				&state->regs->af_low_light_macro_mode_1, 1, 0);
		if (err < 0)
			goto fail;
		msleep(FIRST_SETTING_FOCUS_MODE_DELAY_MS);
		err = s5k5ccgx_set_from_table(state->i2c_client, "af low light macro mode 2",
				&state->regs->af_low_light_macro_mode_2, 1, 0);
		if (err < 0)
			goto fail;
		msleep(SECOND_SETTING_FOCUS_MODE_DELAY_MS);
		err = s5k5ccgx_set_from_table(state->i2c_client, "af low light macro mode 3",
				&state->regs->af_low_light_macro_mode_3, 1, 0);
		if (err < 0)
			goto fail;
	} else {
		err = s5k5ccgx_set_from_table(state->i2c_client,
			"af low light normal mode 1",
			&state->regs->af_low_light_normal_mode_1, 1, 0);
		if (err < 0)
			goto fail;
		msleep(FIRST_SETTING_FOCUS_MODE_DELAY_MS);
		err = s5k5ccgx_set_from_table(state->i2c_client,
			"af low light normal mode 2",
			&state->regs->af_low_light_normal_mode_2, 1, 0);
		if (err < 0)
			goto fail;
		msleep(SECOND_SETTING_FOCUS_MODE_DELAY_MS);
		err = s5k5ccgx_set_from_table(state->i2c_client,
			"af low light normal mode 3",
			&state->regs->af_low_light_normal_mode_3, 1, 0);
		if (err < 0)
			goto fail;
	}
	return 0;
fail:
	dev_err(&state->i2c_client->dev,
		"%s: i2c_write failed\n", __func__);
	return -EIO;
}

static int s5k5ccgx_set_focus_mode(struct i2c_client *client, int value)
{
	int err;

	if((state->parms.mode_focus == value)||(state->bCammode == CAMMODE_CAMCORDER))
		return 0;

	dev_dbg(&client->dev, "%s value(%d)\n", __func__, value);

	if (value<1 || value>7)
		return -EINVAL;

	switch (value) {
	case FOCUS_MACRO:
	#if 0
		dev_dbg(&state->i2c_client->dev,
				"%s: FOCUS_MODE_MACRO\n", __func__);
		err = s5k5ccgx_set_from_table(state->i2c_client, "af macro mode 1",
				&state->regs->af_macro_mode_1, 1, 0);
		if (err < 0)
			goto fail;
		msleep(FIRST_SETTING_FOCUS_MODE_DELAY_MS);
		err = s5k5ccgx_set_from_table(state->i2c_client, "af macro mode 2",
				&state->regs->af_macro_mode_2, 1, 0);
		if (err < 0)
			goto fail;
		msleep(SECOND_SETTING_FOCUS_MODE_DELAY_MS);
		err = s5k5ccgx_set_from_table(state->i2c_client, "af macro mode 3",
				&state->regs->af_macro_mode_3, 1, 0);
		if (err < 0)
			goto fail;
	#else
		err = s5k5ccgx_set_from_table(state->i2c_client, "af macro mode",
				&state->regs->af_macro_mode, 1, 0);
		if (err < 0)
			goto fail;
	#endif
		state->parms.mode_focus = FOCUS_MACRO;
		break;

	case FOCUS_INFINITY:
	case FOCUS_AUTO:
	case FOCUS_FIXED:		
	#if 0
		err = s5k5ccgx_set_from_table(state->i2c_client,
			"af normal mode 1",
			&state->regs->af_normal_mode_1, 1, 0);
		if (err < 0)
			goto fail;
		msleep(FIRST_SETTING_FOCUS_MODE_DELAY_MS);
		err = s5k5ccgx_set_from_table(state->i2c_client,
			"af normal mode 2",
			&state->regs->af_normal_mode_2, 1, 0);
		if (err < 0)
			goto fail;
		msleep(SECOND_SETTING_FOCUS_MODE_DELAY_MS);
		err = s5k5ccgx_set_from_table(state->i2c_client,
			"af normal mode 3",
			&state->regs->af_normal_mode_3, 1, 0);
		if (err < 0)
			goto fail;
	#else
		err = s5k5ccgx_set_from_table(state->i2c_client,
			"af normal mode",
			&state->regs->af_normal_mode, 1, 0);
		if (err < 0)
			goto fail;
	#endif
		state->parms.mode_focus = value;
		break;
	default:
		return -EINVAL;
		break;
	}

	return 0;
fail:
	dev_err(&state->i2c_client->dev,
		"%s: i2c_write failed\n", __func__);
	return -EIO;
}

static void s5k5ccgx_auto_focus_flash_start(struct i2c_client *client)
{
	int count;
	u16 read_value;


	/* delay 200ms (SLSI value) and then poll to see if AE is stable.
	 * once it is stable, lock it and then return to do AF
	 */
	/*msleep(200);*/

	/* enter read mode */
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x002C, 0x7000);
	for (count = 0; count < AE_STABLE_SEARCH_COUNT; count++) {
		if (state->af_status == AF_CANCEL)
			break;
		/*check AE STABLE state*/
		s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x002E, 0x1E3C);
		s5k5ccgx_i2c_read_twobyte(state->i2c_client, 0x0F12, &read_value);
		dev_dbg(&state->i2c_client->dev, "%s: ae stable status = %#x\n",
			__func__, read_value);
		if (read_value == 0x1)
			break;
		msleep(state->one_frame_delay_ms);
	}

	/* restore write mode */
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x0028, 0x7000);

	/* if we were cancelled, turn off flash */
#if 0 /* move to ioctl]*/
	if (state->af_status == AF_CANCEL) {
		dev_dbg(&state->i2c_client->dev,
			"%s: AF cancelled\n", __func__);
		s5k5ccgx_set_from_table(state->i2c_client, "AF assist flash end",
				&state->regs->af_assist_flash_end, 1, 0);
		state->flash_on = false;
		state->pdata->af_assist_onoff(0);
	}
#endif
}

static int ae_check_need;
static int frame_ignore;

static int s5k5ccgx_start_auto_focus(struct i2c_client *client)
{
	u32 light_level;

	dev_dbg(&state->i2c_client->dev, "%s: start SINGLE AF operation, flash mode %d\n",
		__func__, state->parms.mode_flash);
	/* in case user calls auto_focus repeatedly without a cancel
	 * or a capture, we need to cancel here to allow ae_awb
	 * to work again, or else we could be locked forever while
	 * that app is running, which is not the expected behavior.
	 */

	s5k5ccgx_set_from_table(state->i2c_client, "ae lock off",
				&state->regs->ae_lock_off, 1, 0);

	s5k5ccgx_set_from_table(state->i2c_client, "awb lock off",
				&state->regs->awb_lock_off, 1, 0);

	light_level = s5k5ccgx_get_light_level();

	ae_check_need = 0;

	if(!(state->preview_framesize_index == S5K5CCGX_PREVIEW_PVGA)){
		if (light_level <= LOW_LIGHT_LEVEL) {
			switch(state->parms.mode_flash) {
			case FLASH_AUTO:
			case FLASH_ON:
				s5k5ccgx_set_from_table(state->i2c_client, "AF assist flash start",
					&state->regs->af_assist_flash_start, 1, 0);
				s5k5ccgx_set_from_table(state->i2c_client, "Flash AE set",
					&state->regs->flash_ae_set, 1, 0);
				state->pdata->af_assist_onoff(1);
				state->flash_on = true;
				ae_check_need++;
				msleep(100);
				//PCAM_DEBUG("ae_check_need = 0x%X   msleep ------ 100ms  ", ae_check_need);			
			case FLASH_OFF:
			default:
				break;
			}
		} else {
			switch(state->parms.mode_flash) {
			case FLASH_ON:
				s5k5ccgx_set_from_table(state->i2c_client, "AF assist flash start",
					&state->regs->af_assist_flash_start, 1, 0);
				s5k5ccgx_set_from_table(state->i2c_client, "Flash AE set",
					&state->regs->flash_ae_set, 1, 0);
				state->pdata->af_assist_onoff(1);
				state->flash_on = true;
				ae_check_need++;
				msleep(100);	
				//PCAM_DEBUG("ae_check_need = 0x%X   msleep ------ 100ms  ", ae_check_need);			
			case FLASH_AUTO:
			case FLASH_OFF:
			default:
				break;
			}
		}
	}
	state->isAFCancel = true;

	return 0;
}

static int s5k5ccgx_stop_auto_focus(struct i2c_client *client)
{
	int focus_mode = state->parms.mode_focus;
	dev_dbg(&state->i2c_client->dev, "%s: single AF Off command Setting\n", __func__);

	state->touchaf_enable = false;
	/* always cancel ae_awb, in case AF already finished before
	 * we got called.
	 */
	/* restore write mode */
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x0028, 0x7000);

	s5k5ccgx_set_from_table(state->i2c_client, "ae lock off",
				&state->regs->ae_lock_off, 1, 0);

	s5k5ccgx_set_from_table(state->i2c_client, "awb lock off",
				&state->regs->awb_lock_off, 1, 0);

	if(!(state->preview_framesize_index == S5K5CCGX_PREVIEW_PVGA)){
		if (state->flash_on) {
			s5k5ccgx_set_from_table(state->i2c_client, "AF assist flash end",
						&state->regs->af_assist_flash_end, 1, 0);
			s5k5ccgx_set_from_table(state->i2c_client, "Flash AE clear",
						&state->regs->flash_ae_clear, 1, 0);
			state->pdata->af_assist_onoff(0);
			state->flash_on = false;
			msleep(100);//added temporary
		}
	}

	if (state->af_status != AF_START) {
		dev_dbg(&state->i2c_client->dev,
			"%s: sending Single_AF_Off commands to sensor\n", __func__);

		s5k5ccgx_set_from_table(state->i2c_client, "single af off",
					&state->regs->single_af_off, 1, 0);
		return 0;
	}

	state->af_status = AF_CANCEL;

	if (state->isAFCancel) {
		/* we weren't in the middle auto focus operation, we're done */
		dev_dbg(&state->i2c_client->dev,
			"%s: auto focus not in progress, done\n", __func__);
		if (focus_mode == FOCUS_MACRO) {
			/* for change focus mode forcely */
			state->parms.mode_focus = -1;
			s5k5ccgx_set_focus_mode(state->i2c_client, FOCUS_MACRO);
		} else if (focus_mode == FOCUS_AUTO) {
			/* for change focus mode forcely */
			state->parms.mode_focus = -1;
			s5k5ccgx_set_focus_mode(state->i2c_client, FOCUS_AUTO);
		}

		return 0;
	}	
	/* auto focus was in progress.  the other thread
	 * is either in the middle of get_auto_focus_result()
	 * or will call it shortly.  set a flag to have
	 * it abort it's polling.  that thread will
	 * also do cleanup like restore focus position.
	 *
	 * it might be enough to just send sensor commands
	 * to abort auto focus and the other thread would get
	 * that state from it's polling calls, but I'm not sure.
	 */


	state->touchaf_enable = false;
	return 0;
}

/* called by HAL after auto focus was started to get the result.
 * it might be aborted asynchronously by a call to set_auto_focus
 */


static int s5k5ccgx_get_auto_focus_check_second_search(struct i2c_client *client)
{
	int err, count;
	u16 read_value;

	PCAM_DEBUG("START");

	/*2nd search*/
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x002C, 0x7000);
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x002E, 0x1F2F);
	s5k5ccgx_i2c_read_twobyte(state->i2c_client, 0x0F12, &read_value);

	return read_value;
}


static int s5k5ccgx_get_auto_focus_pre_check(struct i2c_client *client)
{
	if (!(state->touchaf_enable)) {
		if (ae_check_need) {
			if (s5k5ccgx_get_ae_stable() != 0x0001) {
				msleep(70);
				//PCAM_DEBUG("msleep ------ 70ms  ");			
				if (ae_check_need++ < 10)
				return 0;
			}
		}
#ifdef CONFIG_MACH_SAMSUNG_P5W_KT	//devide internal and market app : goggles, QRcode, etc..
		if(state->bAppmode == APPMODE_SEC_APP)
#endif
		{
			s5k5ccgx_set_from_table(state->i2c_client, "ae lock on",
			&state->regs->ae_lock_on, 1, 0);	
			if ((state->parms.mode_flash != FLASH_ON || !ae_check_need) && state->parms.mode_wb == WB_AUTO) {
				s5k5ccgx_set_from_table(state->i2c_client, "awb lock on",
				&state->regs->awb_lock_on, 1, 0);
			}
		}
	}

	if (state->bCammode == CAMMODE_CAMCORDER) {
		// set AF operation value for 720P
		s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x0028, 0x7000);
		s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x002A, 0x0226);
		s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x0F12, 0x0010); 

		// 720P 1frame delay
		msleep(50);

		// set AF start cmd value for 720P
		s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x002A, 0x0224);
		s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x0F12, 0x0006);
		printk("\n\n%s : 720P Auto Focus Operation \n\n", __func__);
	} else {     
		if ( (state->parms.mode_scene == SCENE_FIRE_WORK) || (state->parms.mode_scene == SCENE_NIGHT))
			msleep(250);
		else
			msleep(100);
		s5k5ccgx_set_from_table(state->i2c_client, "single af start", &state->regs->single_af_start, 1, 0);
	}
	state->af_status = AF_START;
	dev_dbg(&state->i2c_client->dev, "%s: af_status set to start\n", __func__);

	ae_check_need = 0;

	return 1;
}

static int s5k5ccgx_get_auto_focus_check_first_search(struct i2c_client *client)
{
	int err, count;
	u16 read_value;

	/*1st search*/
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x002C, 0x7000);
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x002E, 0x2D12);
	s5k5ccgx_i2c_read_twobyte(state->i2c_client, 0x0F12, &read_value);

	PCAM_DEBUG("START  %04X\n", read_value);

	return read_value;
}

static void s5k5ccgx_init_parameters()
{
	/*state->parms.contrast = CONTRAST_DEFAULT;*/
	state->parms.mode_effect = EFFECT_NONE;
	state->parms.mode_exposure = EXPOSURE_ZERO;
	state->parms.mode_flash = FLASH_ON;
	state->parms.mode_focus = FOCUS_AUTO;
	state->parms.mode_iso = ISO_AUTO;
	state->parms.mode_metering = METERING_CENTER;
	state->parms.mode_scene = SCENE_AUTO;
	state->parms.mode_wb = WB_AUTO;
	state->one_frame_delay_ms = NORMAL_MODE_MAX_ONE_FRAME_DELAY_MS;
}

static int s5k5ccgx_set_flash_mode(struct i2c_client *client, int value)
{
	if (state->parms.mode_flash == value)
		return 0;
	if ((value >= FLASH_AUTO) && (value <= FLASH_TORCH)) {
		pr_debug("%s: setting flash mode to %d\n",
			__func__, value);
		if (value == FLASH_TORCH) {
			state->pdata->torch_onoff(1);
			state->torch_on = true;
		} else if (value != FLASH_TORCH && state->torch_on == true) {
			state->pdata->torch_onoff(0);
			state->torch_on = false;
		}
		state->parms.mode_flash = value;
	}
	pr_debug("%s: trying to set invalid flash mode %d\n",
		__func__, value);
	return 0;
}




/* This function is called from the g_ctrl api
 *
 * This function should be called only after the s_fmt call,
 * which sets the required width/height value.
 *
 * It checks a list of available frame sizes and sets the
 * most appropriate frame size.
 *
 * The list is stored in an increasing order (as far as possible).
 * Hence the first entry (searching from the beginning) where both the
 * width and height is more than the required value is returned.
 * In case of no perfect match, we set the last entry (which is supposed
 * to be the largest resolution supported.)
 */

#if 0

/*static void s5k5ccgx_get_esd_int(struct i2c_client *client,
				struct v4l2_control *ctrl)
{
	struct s5k5ccgx_state *state = i2c_get_clientdata(client);
	u16 read_value;
	int err;

	if ((S5K5CCGX_RUNMODE_RUNNING == state->runmode) &&
		(state->af_status != AF_START)) {
		err = s5k5ccgx_set_from_table(sd, "get esd status",
					&state->regs->get_esd_status,
					1, 0);
		err |= s5k5ccgx_i2c_read_twobyte(client, 0x0F12, &read_value);
		dev_dbg(&client->dev,
			"%s: read_value == 0x%x\n", __func__, read_value);
		// return to write mode
		err |= s5k5ccgx_i2c_write_twobyte(client, 0x0028, 0x7000);

		if (err < 0) {
			v4l_info(client,
				"Failed I2C for getting ESD information\n");
			ctrl->value = 0x01;
		} else {
			if (read_value != 0x0000) {
				v4l_info(client, "ESD interrupt happened!!\n");
				ctrl->value = 0x01;
			} else {
				dev_dbg(&client->dev,
					"%s: No ESD interrupt!!\n", __func__);
				ctrl->value = 0x00;
			}
		}
	} else
		ctrl->value = 0x00;
}*/

/* returns the real iso currently used by sensor due to lighting
 * conditions, not the requested iso we sent using s_ctrl.
 */
/*static int s5k5ccgx_get_iso(struct i2c_client *client, struct v4l2_control *ctrl)
{
	int err;
	struct s5k5ccgx_state *state = i2c_get_clientdata(client);
	u16 read_value1 = 0;
	u16 read_value2 = 0;
	int read_value;

	err = s5k5ccgx_set_from_table(sd, "get iso",
				&state->regs->get_iso, 1, 0);
	err |= s5k5ccgx_i2c_read_twobyte(client, 0x0F12, &read_value1);
	err |= s5k5ccgx_i2c_read_twobyte(client, 0x0F12, &read_value2);

	// restore write mode
	s5k5ccgx_i2c_write_twobyte(client, 0x0028, 0x7000);

	read_value = read_value1 * read_value2 / 384;

	if (read_value > 0x400)
		ctrl->value = ISO_400;
	else if (read_value > 0x200)
		ctrl->value = ISO_200;
	else if (read_value > 0x100)
		ctrl->value = ISO_100;
	else
		ctrl->value = ISO_50;

	dev_dbg(&client->dev, "%s: get iso == %d (0x%x, 0x%x)\n",
		__func__, ctrl->value, read_value1, read_value2);

	return err;
}
*/
/*static int s5k5ccgx_get_shutterspeed(struct i2c_client *client,
	struct v4l2_control *ctrl)
{
	int err;
	struct s5k5ccgx_state *state = i2c_get_clientdata(client);

	u16 read_value_1;
	u16 read_value_2;
	u32 read_value;

	err = s5k5ccgx_set_from_table(sd, "get shutterspeed",
				&state->regs->get_shutterspeed, 1, 0);
	err |= s5k5ccgx_i2c_read_twobyte(client, 0x0F12, &read_value_1);
	err |= s5k5ccgx_i2c_read_twobyte(client, 0x0F12, &read_value_2);

	read_value = (read_value_2 << 16) | (read_value_1 & 0xffff);
	// restore write mode
	s5k5ccgx_i2c_write_twobyte(client, 0x0028, 0x7000);

	ctrl->value = read_value * 1000 / 400;
	dev_dbg(&client->dev,
			"%s: get shutterspeed == %d\n", __func__, ctrl->value);

	return err;
}*/
#endif

static int s5k5ccgx_init_regs(struct i2c_client *client)
{
	u16 read_value;
	int err;	
	PCAM_DEBUG("start");

	/* we'd prefer to do this in probe, but the framework hasn't
	 * turned on the camera yet so our i2c operations would fail
	 * if we tried to do it in probe, so we have to do it here
	 * and keep track if we succeeded or not.
	 */

	/* enter read mode */
	err = s5k5ccgx_i2c_write_twobyte(client, 0x002C, 0x7000);
	if(err !=0)
		return err;
	err = s5k5ccgx_i2c_write_twobyte(client, 0x002E, 0x0150);
	err = s5k5ccgx_i2c_read_twobyte(client, 0x0F12, &read_value);
	PCAM_DEBUG("%s : FW ChipID  revision : %04X\n", __func__, read_value);

	err = s5k5ccgx_i2c_write_twobyte(client, 0x002C, 0x7000);
	err = s5k5ccgx_i2c_write_twobyte(client, 0x002E, 0x0152);
	err = s5k5ccgx_i2c_read_twobyte(client, 0x0F12, &read_value);
	PCAM_DEBUG("%s :FW EVT revision : %04X\n", __func__, read_value);

	/* restore write mode */
	err = s5k5ccgx_i2c_write_twobyte(client, 0x0028, 0x7000);

	state->initialized = true;
	state->regs = &regs_for_fw_version_1_1;
	dtpTest = 0;

	return err;
}

#define BURST_MODE_BUFFER_MAX_SIZE 2700
unsigned char s5k5ccgx_buf_for_burstmode[BURST_MODE_BUFFER_MAX_SIZE];

static int s5k5ccgx_sensor_burst_write_list(const u32 list[], int size, char *name)	
{
	int err = -EINVAL;
	int i = 0;
	int idx = 0;

	u16 subaddr = 0, next_subaddr = 0;
	u16 value = 0;

	struct i2c_msg msg = {  state->i2c_client->addr, 0, 0, s5k5ccgx_buf_for_burstmode};
	


	for (i = 0; i < size; i++)
	{

		if(idx > (BURST_MODE_BUFFER_MAX_SIZE - 10))
		{
			printk("<=PCAM=> BURST MODE buffer overflow!!!\n");
			 return err;
		}



		subaddr = (list[i] & 0xFFFF0000) >> 16;

		if(subaddr == 0x0F12)
			next_subaddr = (list[i+1] & 0xFFFF0000) >> 16;

		value = list[i] & 0x0000FFFF;
		
		switch(subaddr)
		{

			case 0x0F12:
			{
				// make and fill buffer for burst mode write
				if(idx == 0) 
				{
					s5k5ccgx_buf_for_burstmode[idx++] = 0x0F;
					s5k5ccgx_buf_for_burstmode[idx++] = 0x12;
				}
				s5k5ccgx_buf_for_burstmode[idx++] = value >> 8;
				s5k5ccgx_buf_for_burstmode[idx++] = value & 0xFF;

			
			 	//write in burstmode
				if(next_subaddr != 0x0F12)
				{
					msg.len = idx;
					err = i2c_transfer(state->i2c_client->adapter, &msg, 1) == 1 ? 0 : -EIO;
					//printk("s5k5ccgx_sensor_burst_write, idx = %d\n",idx);
					idx=0;
				}
				
			}
			break;

			case 0xFFFF:
			{
				pr_info("burst_mode --- s5k5ccgx_i2c_write_twobyte give delay: %d\n", value);				
				msleep(value);
			}
			break;
		
			default:
			{
			    idx = 0;
			    err = s5k5ccgx_i2c_write_twobyte(state->i2c_client, subaddr, value);
			}
			break;
			
		}

		
	}

	if (unlikely(err < 0))
	{
		printk("<=PCAM=>%s: register set failed\n",__func__);
		return err;
	}
	return 0;

}

int s5k5ccgx_set_exif_info(struct s5k5ccgx_state *state)
{
	u16 iso_value;

	state->exif_info.info_exptime_numer = s5k5ccgx_get_shutter_speed() / 4;
	state->exif_info.info_exptime_denumer = 100000;

	iso_value = s5k5ccgx_get_iso() * 100 / 256;

	if (iso_value < 150)
		state->exif_info.info_iso = 50;
	else if (iso_value < 250)
		state->exif_info.info_iso = 100;
	else if (iso_value < 350)
		state->exif_info.info_iso = 200;
	else
		state->exif_info.info_iso = 400;

	return 0;
}

static int s5k5ccgx_set_mode(struct s5k5ccgx_state *state, struct s5k5ccgx_mode *mode)
{
	int sensor_mode;
	int err = 0;

	pr_info(" s5k5ccgx_set_mode 3M !!!! x = %d, y = %d, mode_info = %d\n", mode->xres, mode->yres, mode->mode_info);

	if (mode->xres == 2048 && mode->yres == 1536)
		state->capture_framesize_index = S5K5CCGX_CAPTURE_3MP;
		// 3M size only support capture mode
	else if (mode->xres == 1280 && mode->yres == 1024)
		state->preview_framesize_index = S5K5CCGX_PREVIEW_SXGA;		
	else if (mode->xres == 1280 && mode->yres == 720)
		state->preview_framesize_index = S5K5CCGX_PREVIEW_PVGA;
	else if (mode->xres == 1024 && mode->yres == 768)
		state->preview_framesize_index = S5K5CCGX_PREVIEW_XGA;
	else if (mode->xres == 640 && mode->yres == 480)
		state->preview_framesize_index = S5K5CCGX_PREVIEW_VGA;
	else if (mode->xres == 528 && mode->yres == 432)
		state->preview_framesize_index = S5K5CCGX_PREVIEW_528x432;
#ifdef CONFIG_MACH_SAMSUNG_P5W_KT	// homepad
	else if (mode->xres == 352 && mode->yres == 288)
		state->preview_framesize_index = S5K5CCGX_PREVIEW_CIF;
	else if (mode->xres == 320 && mode->yres == 240)
		state->preview_framesize_index = S5K5CCGX_PREVIEW_320x240;
	else if (mode->xres == 704 && mode->yres == 576)
		state->preview_framesize_index = S5K5CCGX_PREVIEW_4CIF;
	else if (mode->xres == 176 && mode->yres == 144)
		state->preview_framesize_index = S5K5CCGX_PREVIEW_QCIF;
#else
	else if (mode->xres == 352 && mode->yres == 288)
		state->preview_framesize_index = S5K5CCGX_PREVIEW_528x432;
	else if (mode->xres == 320 && mode->yres == 240)
		state->preview_framesize_index = S5K5CCGX_PREVIEW_320x240;
#endif
	else {
		pr_err("%s : invalid resolution supplied to set mode %dx%d\n",
				__func__, mode->xres, mode->yres);
		return -EINVAL;
	}

	if (state->lastPreview_framesize_index == state->preview_framesize_index && 
		state->esd_status == false && mode->mode_info != MODE_INFO_STILL) 
		return 0;

	if (mode->mode_info == MODE_INFO_STILL) {
		//TODO : make STILL mode

		s5k5ccgx_start_capture(state, mode);
		s5k5ccgx_set_exif_info(state);		
		state->preview_framesize_index = S5K5CCGX_PREVIEW_MAX;
		if(state->touchaf_enable)
			s5k5ccgx_reset_AF_region();
	} else if (mode->mode_info == MODE_INFO_PREVIEW || mode->mode_info == MODE_INFO_VIDEO) {
		//TODO : make continuous frame setting - preview or video
		state->isAFCancel = false;
		if ((!state->power_status && mode->mode_info == MODE_INFO_PREVIEW) || state->esd_status == true) {
			//TODO : turn on the power with preview
			if (mode->xres == 1280 && mode->yres == 720) {
                state->bHD_enable = true;
				//TODO : using HD init setting
				printk("1280 x 720 setting ~~~~~~~~~~~~~~~~\n");
#ifdef CONFIG_LOAD_FILE				
				err = s5k5ccgx_set_from_table(state->i2c_client, "preview_size",
						&state->regs->preview_size, ARRAY_SIZE(state->regs->preview_size), state->preview_framesize_index);                    
				if (err < 0) {
					dev_err(&state->i2c_client->dev, "%s: failed: Could not set 1M preview size\n",
						__func__);
					return -EIO;
				}
#else
				S5K5CCGX_BURST_WRITE_LIST(s5k5ccgx_1280_720_Preview);				
#endif
#ifndef CONFIG_MACH_SAMSUNG_P5
                        err = s5k5ccgx_set_from_table(state->i2c_client, "hd first af start",
                              &state->regs->hd_first_af_start , 1, 0);
				if (err < 0) {
					dev_err(&state->i2c_client->dev, "%s: failed: 720P first AF CMD fail\n",
						__func__);
					return -EIO;
				}
#endif

			}
		else if (mode->xres == 1280 && mode->yres == 1024){
                state->bHD_enable = false;
				//TODO : using HD init setting
				printk("1280 x 1024 setting ~~~~~~~~~~~~~~~~\n");
#ifdef CONFIG_LOAD_FILE				
				err = s5k5ccgx_set_from_table(state->i2c_client, "preview_size",
						&state->regs->preview_size, ARRAY_SIZE(state->regs->preview_size), state->preview_framesize_index);                    
				if (err < 0) {
					dev_err(&state->i2c_client->dev, "%s: failed: Could not set 1M preview size\n",
						__func__);
					return -EIO;
				}
#else
				S5K5CCGX_BURST_WRITE_LIST(s5k5ccgx_1280_1024_Preview);				
#endif				
                        
			}else {
                state->bHD_enable = false;
				if (s5k5ccgx_set_from_table(state->i2c_client, "init reg 1",
					&state->regs->init_reg_1, 1, 0) < 0) {
					pr_err("%s, init regs 1 failed\n", __func__);
					return -EIO;
				}

				/* delay 10ms after wakeup of SOC processor */
				msleep(10);
#ifdef CONFIG_LOAD_FILE
				//S5K5CCGX_BURST_WRITE_LIST(s5k5ccgx_init0);

				if (s5k5ccgx_set_from_table(state->i2c_client, "init reg 2",
					&state->regs->init_reg_2, 1, 0) < 0)
					return -EIO;
#else
				if (dtpTest) {
					printk("DTP TEST init\n");
					S5K5CCGX_BURST_WRITE_LIST(s5k5ccgx_DTP_init0);
					/*if (s5k5ccgx_set_from_table(state->i2c_client, "init reg DTP",
						&state->regs->init_reg_DTP, 1, 0) < 0)
						return -EIO;*/
				}
				else {
					S5K5CCGX_BURST_WRITE_LIST(s5k5ccgx_init0);
					/*if (s5k5ccgx_set_from_table(state->i2c_client, "init reg 2",
						&state->regs->init_reg_2, 1, 0) < 0) {
							pr_err("%s, init regs 2 failed\n", __func__);
							return -EIO;
					}*/
				}
#endif
				if (!dtpTest) {
					err = s5k5ccgx_set_from_table(state->i2c_client, "preview_size",
						&state->regs->preview_size, ARRAY_SIZE(state->regs->preview_size), state->preview_framesize_index);
					if (err < 0) {
						dev_err(&state->i2c_client->dev, "%s: failed: Could not set preview size\n", __func__);
						return -EIO;
					}
					err = s5k5ccgx_set_from_table(state->i2c_client, "update preview setting",
						&state->regs->update_preview_setting, 1, 0);
					if (err < 0) {
						dev_err(&state->i2c_client->dev, "%s: failed: Could not set preview update\n", __func__);
						return -EIO;
					}
				}

			}
			state->power_status = true;

		} else if (state->power_status && (mode->mode_info == MODE_INFO_VIDEO || mode->mode_info == MODE_INFO_PREVIEW)) {
			//TODO : change preview or video size
			if (state->lastmode_info == MODE_INFO_STILL) {
				//TODO : preview return
				/*state->pdata->flash_onoff(0);
				state->flash_on = false;*/
				err = s5k5ccgx_set_from_table(state->i2c_client, "preview return",
					&state->regs->preview_return, 1, 0);
				if (err < 0) {
					dev_err(&state->i2c_client,
						"%s: failed: s5k5ccgx_Preview_Return\n",
						__func__);
					return -EIO;
				}
				if (state->parms.mode_scene == SCENE_NIGHT){
					msleep(500);
					printk("msleep -------------- 500ms \n");
				} else if(state->parms.mode_scene == SCENE_FIRE_WORK){
					msleep(800);
					printk("msleep -------------- 800ms \n");
				} else {
					msleep(150);
					printk("msleep -------------- 150ms \n");
				}
			} else if (state->lastmode_info == MODE_INFO_PREVIEW || state->lastmode_info == MODE_INFO_VIDEO) {
					//TODO : change preview resolution
					err = s5k5ccgx_set_from_table(state->i2c_client, "preview_size",
						&state->regs->preview_size, ARRAY_SIZE(state->regs->preview_size), state->preview_framesize_index);
					if (err < 0) {
						dev_err(&state->i2c_client->dev, "%s: failed: Could not set preview size\n", __func__);
						return -EIO;
					}
					err = s5k5ccgx_set_from_table(state->i2c_client, "update preview setting",
						&state->regs->update_preview_setting, 1, 0);
					if (err < 0) {
						dev_err(&state->i2c_client->dev, "%s: failed: Could not set preview update\n", __func__);
						return -EIO;
					}
			} else {
				//TODO : error
				pr_err("%s : unknown error with sensor mode\n",
						__func__);
				return -EINVAL;
			}
		} else if (!state->power_status && mode->mode_info == MODE_INFO_VIDEO) {
			//TODO : error - start recording can't launch with power on
			pr_err("%s: StartRecording can't launching before power on\n", __func__);
			return -EINVAL;
		} else {
			//TOTO : unknown error
			pr_err("%s: Unknown Error in set_mode\n", __func__);
			return -EFAULT;
		}
	}
	state->lastmode_info = mode->mode_info;
	if (state->lastmode_info == MODE_INFO_STILL)
		state->isAFCancel = false;
	state->lastPreview_framesize_index = state->preview_framesize_index;
	state->esd_status = false;
	state->touchaf_enable = false;
            
	return 0;
}

#if 1
static void s5k5ccgx_esd_reset(struct i2c_client *client, int arg)
{
	if (arg == ESD_DETECTED) {
			state->power_status = false;
			state->esd_status = true;			
			state->pdata->power_off();
			state->pdata->power_on();
	}


	if (s5k5ccgx_init_regs(client) < 0) {
		pr_err("%s, init regs failed\n", __func__);
		return -ENODEV;
	}

#ifdef CONFIG_LOAD_FILE
	if (s5k5ccgx_regs_table_init(1)) {
		printk("%s: config file read fail\n", __func__);
		return -EIO;
	}
	msleep(100);
#endif

//	s5k5ccgx_set_mode(state, &state->state_mode);
	
	return 0;
}
#endif

static int s5k5ccgx_autofocus_getresult(struct i2c_client *client, struct s5k5ccgx_af_result *af_mode)
{
	u16	result;

	PCAM_DEBUG("START %d", af_mode->mode);

	switch (af_mode->mode) {
	case 0: /*presetting*/
	{
		result = s5k5ccgx_get_auto_focus_pre_check(state->i2c_client);
		af_mode->param_2 = result;
		frame_ignore = 0;
	}
	break;
	case 1: /*1st search*/
	{
		frame_ignore++;
		if (frame_ignore < 3) {
			msleep(67);
			af_mode->param_2 = 1;
			break;
		}
		result = s5k5ccgx_get_auto_focus_check_first_search(state->i2c_client);
		af_mode->param_2 = result;
	}
	break;

	case 2: /*2nd search*/
	{
		result = s5k5ccgx_get_auto_focus_check_second_search(state->i2c_client);
		af_mode->param_2 = 0;/*init*/
		af_mode->param_2 = result;
	}
	break;

	default:
	{
		dev_dbg(&state->i2c_client->dev, "unexpected mode is comming from hal\n");
	}
	break;
	}

	return 0;
}

static int s5k5ccgx_set_whitebalance(unsigned long value)
{
	int err = 0;
//for LSI sensor debug	u16 read_value;
#ifdef CONFIG_MACH_SAMSUNG_P5
	if (state->bHD_enable) {
		err = s5k5ccgx_set_from_table(state->i2c_client, "HD_white_balance" , &state->regs->HD_white_balance,
			ARRAY_SIZE(state->regs->HD_white_balance), value);
	} else
#endif
	if(state->parms.mode_scene == SCENE_AUTO){
		err = s5k5ccgx_set_from_table(state->i2c_client, "white_balance" , &state->regs->white_balance,
			ARRAY_SIZE(state->regs->white_balance), value);
	}

	return err;
}

static int s5k5ccgx_set_iso(unsigned long value)
{
	int err = 0;
#if 0
	err = s5k5ccgx_set_from_table(state->i2c_client, "iso" , &state->regs->iso,
		ARRAY_SIZE(state->regs->iso), value);
#endif
	return err;
}

static int s5k5ccgx_set_cam_mode(unsigned long value)
{
	int err = 0;

	printk("%s : value = %d\n\n",__func__, value);

	if (value == CAMMODE_CAMCORDER)
	{
		//TODO : do fix frame control
		err = s5k5ccgx_set_from_table(state->i2c_client, "fps" , &state->regs->fps,
				ARRAY_SIZE(state->regs->fps), FRAME_RATE_30);
		printk("30 fps fix frame!!\n\n\n");
	}
	else if(value == CAMMODE_MMS_CAMCORDER)
	{
		err = s5k5ccgx_set_from_table(state->i2c_client, "fps" , &state->regs->fps,
				ARRAY_SIZE(state->regs->fps), FRAME_RATE_15);
		printk(" 15fps fix frame!!\n\n\n");	
	}
	else
	{
		//TODO : do variable frame control
		err = s5k5ccgx_set_from_table(state->i2c_client, "fps" , &state->regs->fps,
				ARRAY_SIZE(state->regs->fps), FRAME_RATE_AUTO);		
		printk("variable frame!!\n\n\n");
	}

	state->bCammode = value;     

	return err;
}

static int s5k5ccgx_set_torch(unsigned long value)
{
	int err = 0;
	printk("\n\n%s : %d\n\n", __func__, state->parms.mode_flash);

	if (value == CAF_START) {
		switch (state->parms.mode_flash) {
		case FLASH_ON:
			state->flash_on = true;
			state->pdata->torch_onoff(1);
			break;
		default:
			break;
		}
	} else if (value == CAF_STOP) {
		switch (state->parms.mode_flash) {
		case FLASH_ON:
			state->flash_on = false;
			state->pdata->torch_onoff(0);
			break;
		default:
			break;
		}
	}

	return err;
}


static int s5k5ccgx_set_scene(unsigned long value)
{
	int err = 0;

	if (value != SCENE_AUTO) {
		err = s5k5ccgx_set_from_table(state->i2c_client, "scene_mode" , &state->regs->scene_mode,
			ARRAY_SIZE(state->regs->scene_mode), SCENE_AUTO);
		if (err < 0) {
			return -EINVAL;
		}
	}

	err = s5k5ccgx_set_from_table(state->i2c_client, "scene_mode" , &state->regs->scene_mode,
		ARRAY_SIZE(state->regs->scene_mode), value);

	return err;
}
static int s5k5ccgx_check_dataline_stop(struct i2c_client *client, int arg)
{
	PCAM_DEBUG("START \n");
	u16 width, height;
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x002C, 0x7000);
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x002E, 0x023E);
	s5k5ccgx_i2c_read_twobyte(state->i2c_client, 0x0F12, &width);
	
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x002C, 0x7000);
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x002E, 0x0240);
	s5k5ccgx_i2c_read_twobyte(state->i2c_client, 0x0F12, &height);

	 printk("333333333333333333333 %s width = %d,, %04X,, height =	%d,, %04X\n", __func__, width, width, height, height);
	 

	if (arg == 0) {		
		state->pdata->power_off();
		state->pdata->power_on();
	}


	if (s5k5ccgx_init_regs(client) < 0) {
		pr_err("%s, init regs failed\n", __func__);
		return -ENODEV;
	}

#ifdef CONFIG_LOAD_FILE
	if (s5k5ccgx_regs_table_init(1)) {
		printk("%s: config file read fail\n", __func__);
		return -EIO;
	}
	msleep(100);
#endif

//	s5k5ccgx_set_mode(state, &state->state_mode);
	
	return 0;
}

//modify to TouchAF by Teddy
static int s5k5ccgx_set_touchaf(struct s5k5ccgx_touchaf_pos *tpos)
{
	u16 mapped_x = 0;
	u16 mapped_y = 0;
	u16 inner_window_start_x = 0;
	u16 inner_window_start_y = 0;
	u16 outer_window_start_x = 0;
	u16 outer_window_start_y = 0;

	u16 sensor_width = 0;
	u16 sensor_height = 0;

	u16 inner_window_width = 0;
	u16 inner_window_height = 0;
	u16 outer_window_width = 0;
	u16 outer_window_height = 0;

	u16 touch_width = 0;
	u16 touch_height = 0;

	if (state->bHD_enable) {
		sensor_width    = 1280;
		sensor_height   = 720;
		inner_window_width = INNER_WINDOW_WIDTH_720P;
		inner_window_height= INNER_WINDOW_HEIGHT_720P;
		outer_window_width= OUTER_WINDOW_WIDTH_720P;
		outer_window_height= OUTER_WINDOW_HEIGHT_720P;
		touch_width= 1280;
		touch_height= 720;
	} else {
		sensor_width    = 1024;
		sensor_height   = 768;
		inner_window_width = INNER_WINDOW_WIDTH_1024_768;
		inner_window_height= INNER_WINDOW_HEIGHT_1024_768;
		outer_window_width= OUTER_WINDOW_WIDTH_1024_768;
		outer_window_height= OUTER_WINDOW_HEIGHT_1024_768;
		//touch_width= 1066;
		//touch_height= 800;
		touch_width= 1000;
		touch_height= 750;          
	}

	state->touchaf_enable = true;
	printk("\n\n%s : xPos = %d, yPos = %d \n\n", __func__, tpos->xpos, tpos->ypos);

	// mapping the touch position on the sensor display
	mapped_x = (tpos->xpos * sensor_width) / touch_width;
	mapped_y = (tpos->ypos * sensor_height) / touch_height;
	printk("\n\n%s : mapped xPos = %d, mapped yPos = %d\n\n", __func__, mapped_x, mapped_y);

	// set X axis
	if ( mapped_x  <=  (inner_window_width / 2) ) {
		inner_window_start_x    = 0;
		outer_window_start_x    = 0;
		printk("\n\n%s : inbox over the left side. boxes are left side align in_Sx = %d, out_Sx= %d\n\n", __func__, inner_window_start_x, outer_window_start_x);
	} else if ( mapped_x  <=  (outer_window_width / 2) ) {
		inner_window_start_x    = mapped_x - (inner_window_width / 2);
		outer_window_start_x    = 0;
		printk("\n\n%s : outbox only over the left side. outbox is only left side align in_Sx = %d, out_Sx= %d\n\n", __func__, inner_window_start_x, outer_window_start_x);
	} else if ( mapped_x  >=  ( (sensor_width - 1) - (inner_window_width / 2) ) ) {
		inner_window_start_x    = (sensor_width - 1) - inner_window_width;
		outer_window_start_x    = (sensor_width - 1) - outer_window_width;
		printk("\n\n%s : inbox over the right side. boxes are rightside align in_Sx = %d, out_Sx= %d\n\n", __func__, inner_window_start_x, outer_window_start_x);
	} else if ( mapped_x  >=  ( (sensor_width - 1) - (outer_window_width / 2) ) ) {
		inner_window_start_x    = mapped_x - (inner_window_width / 2);
		outer_window_start_x    = (sensor_width - 1) - outer_window_width;
		printk("\n\n%s : outbox only over the right side. out box is only right side align in_Sx = %d, out_Sx= %d\n\n", __func__, inner_window_start_x, outer_window_start_x);
	} else {
		inner_window_start_x    = mapped_x - (inner_window_width / 2);
		outer_window_start_x    = mapped_x - (outer_window_width / 2);
		printk("\n\n%s : boxes are in the sensor window. in_Sx = %d, out_Sx= %d\n\n", __func__, inner_window_start_x, outer_window_start_x);
	}

	// set Y axis
	if ( mapped_y  <=  (inner_window_height / 2) ) {
		inner_window_start_y    = 0;
		outer_window_start_y    = 0;
		printk("\n\n%s : inbox over the top side. boxes are top side align in_Sy = %d, out_Sy= %d\n\n", __func__, inner_window_start_y, outer_window_start_y);
	} else if ( mapped_y  <=  (outer_window_height / 2) ) {
		inner_window_start_y    = mapped_y - (inner_window_height / 2);
		outer_window_start_y    = 0;
		printk("\n\n%s : outbox only over the top side. outbox is only top side align in_Sy = %d, out_Sy= %d\n\n", __func__, inner_window_start_y, outer_window_start_y);
	} else if ( mapped_y  >=  ( (sensor_height - 1) - (inner_window_height / 2) ) ) {
		inner_window_start_y    = (sensor_height - 1) - inner_window_height;
		outer_window_start_y    = (sensor_height - 1) - outer_window_height;
		printk("\n\n%s : inbox over the bottom side. boxes are bottom side align in_Sy = %d, out_Sy= %d\n\n", __func__, inner_window_start_y, outer_window_start_y);
	} else if ( mapped_y  >=  ( (sensor_height - 1) - (outer_window_height / 2) ) ) {
		inner_window_start_y    = mapped_y - (inner_window_height / 2);
		outer_window_start_y    = (sensor_height - 1) - outer_window_height;
		printk("\n\n%s : outbox only over the bottom side. out box is only bottom side align in_Sy = %d, out_Sy= %d\n\n", __func__, inner_window_start_y, outer_window_start_y);
	} else {
		inner_window_start_y    = mapped_y - (inner_window_height / 2);
		outer_window_start_y    = mapped_y - (outer_window_height / 2);
		printk("\n\n%s : boxes are in the sensor window. in_Sy = %d, out_Sy= %d\n\n", __func__, inner_window_start_y, outer_window_start_y);
	}

	//calculate the start position value
	inner_window_start_x = inner_window_start_x * 1024 /sensor_width;
	outer_window_start_x = outer_window_start_x * 1024 / sensor_width;
	inner_window_start_y = inner_window_start_y * 1024 / sensor_height;
	outer_window_start_y = outer_window_start_y * 1024 / sensor_height;
	printk("\n\n%s : calculated value inner_window_start_x = %d\n\n", __func__, inner_window_start_x);
	printk("\n\n%s : calculated value inner_window_start_y = %d\n\n", __func__, inner_window_start_y);
	printk("\n\n%s : calculated value outer_window_start_x = %d\n\n", __func__, outer_window_start_x);
	printk("\n\n%s : calculated value outer_window_start_y = %d\n\n", __func__, outer_window_start_y);

	//Write register
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x0028, 0x7000);

	// inner_window_start_x
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x002A, 0x0234);
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x0F12, inner_window_start_x);

	// outer_window_start_x
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x002A, 0x022C);
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x0F12, outer_window_start_x);

	// inner_window_start_y
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x002A, 0x0236);
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x0F12, inner_window_start_y);

	// outer_window_start_y
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x002A, 0x022E);
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x0F12, outer_window_start_y);

	// Update AF window
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x002A, 0x023C);
	s5k5ccgx_i2c_write_twobyte(state->i2c_client, 0x0F12, 0x0001);

	return 0;
}

#ifdef CONFIG_MACH_SAMSUNG_P5W_KT	//devide internal and market app : goggles, QRcode, etc..
static int s5k5ccgx_set_app_mode(unsigned long value)
{
	int err = 0;
	printk("%s : value = %d\n\n",__func__, value);

	if(value)
		state->bAppmode = APPMODE_SEC_APP;
	else
		state->bAppmode = APPMODE_3RD_APP;

	return err;	
}
#endif

static long s5k5ccgx_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct s5k5ccgx_mode mode;
	struct s5k5ccgx_af_result af_mode;

	struct i2c_client *client = state->i2c_client;
	int err;

	switch (cmd) {
	case S5K5CCGX_IOCTL_CAMMODE:
	{
		return s5k5ccgx_set_cam_mode(arg);
	}
	case S5K5CCGX_IOCTL_SET_MODE:
	{
		if (copy_from_user(&mode, (const void __user *)arg, sizeof(struct s5k5ccgx_mode))) {
			pr_info("%s %d\n", __func__, __LINE__);
			return -EFAULT;
		}
		state->state_mode = mode;
		return s5k5ccgx_set_mode(state, &mode);
	}

	case S5K5CCGX_IOCTL_SCENE_MODE:
	{
		PCAM_DEBUG("S5K5CCGX_IOCTL_SCENE_MODE ");
		err = s5k5ccgx_set_scene(arg);
		if (err < 0) {
			dev_err(&state->i2c_client->dev,
				"%s: failed to set scene-mode default value\n",
				__func__);
			return -EFAULT;
		}

		state->parms.mode_scene = arg;

		if (state->parms.mode_scene == SCENE_NIGHT) {
			state->one_frame_delay_ms =
				NIGHT_MODE_MAX_ONE_FRAME_DELAY_MS;
		} else {
			state->one_frame_delay_ms =
				NORMAL_MODE_MAX_ONE_FRAME_DELAY_MS;
		}
		break;
	}

	case S5K5CCGX_IOCTL_FOCUS_MODE:
	{
		PCAM_DEBUG("S5K5CCGX_IOCTL_FOCUS_MODE value = %d\n", arg);
		err = s5k5ccgx_set_focus_mode(state->i2c_client, arg);
		if (err < 0) {
			dev_err(&state->i2c_client->dev,
				"%s: failed to set focus-mode default value\n",
				__func__);
			return -EFAULT;
		}
		state->parms.mode_focus = arg;
		break;
	}
	case S5K5CCGX_IOCTL_COLOR_EFFECT:
	{
		PCAM_DEBUG("S5K5CCGX_IOCTL_COLOR_EFFECT");
		err = s5k5ccgx_set_from_table(state->i2c_client, "effect" , &state->regs->effect,
					ARRAY_SIZE(state->regs->effect), arg);
		if (err < 0) {
			dev_err(&state->i2c_client->dev,
				"%s: failed to set effect-mode default value\n",
				__func__);
			return -EFAULT;
		}
		state->parms.mode_effect = arg;
		break;
	}
	case S5K5CCGX_IOCTL_WHITE_BALANCE:
	{
		PCAM_DEBUG("S5K5CCGX_IOCTL_WHITE_BALANCE");
		err = s5k5ccgx_set_whitebalance(arg);
		if (err < 0) {
			dev_err(&state->i2c_client->dev,
				"%s: failed to set wb-mode default value\n",
				__func__);
			return -EFAULT;
		}
		state->parms.mode_wb = arg;
		break;
	}
	case S5K5CCGX_IOCTL_FLASH_MODE:
	{
		PCAM_DEBUG("S5K5CCGX_IOCTL_FLASH_MODE");

		err = s5k5ccgx_set_flash_mode(state->i2c_client, arg);
		if (err < 0) {
			dev_err(&state->i2c_client->dev,
				"%s: failed to set flash-mode default value\n",
				__func__);
			return -EFAULT;
		}
		printk("\n\n%s : %d\n\n", __func__, state->parms.mode_flash);
		break;
	}
	case S5K5CCGX_IOCTL_EXPOSURE:
	{
		PCAM_DEBUG("S5K5CCGX_IOCTL_EXPOSURE");
		if(state->parms.mode_scene == SCENE_AUTO){		
			err = s5k5ccgx_set_from_table(state->i2c_client, "ev" , &state->regs->ev,
						ARRAY_SIZE(state->regs->ev), arg);
			if (err < 0) {
				dev_err(&state->i2c_client->dev,
					"%s: failed to set exposure-mode default value\n",
					__func__);
				return -EFAULT;
			}
		}
		state->parms.mode_exposure = arg;
		break;
	}
	case S5K5CCGX_IOCTL_AF_CONTROL:
	{
		PCAM_DEBUG("S5K5CCGX_IOCTL_AF_CONTROL : %d ", arg);
		
		switch (arg) {
		case AF_STOP:
			s5k5ccgx_stop_auto_focus(state->i2c_client);
			break;
		case AF_START:
			s5k5ccgx_start_auto_focus(state->i2c_client);
			break;
		case AF_CANCEL:
			break;
		case CAF_START:
			s5k5ccgx_set_torch(arg);
			break;
		case CAF_STOP:
			s5k5ccgx_set_torch(arg);
			break;
		default:
			break;
		}
		break;
	}
	case S5K5CCGX_IOCTL_ESD_RESET:
	{
		if(!dtpTest){
			PCAM_DEBUG("S5K5CCGX_IOCTL_ESD_RESET");
			printk("============  ESD ESD called \n");
			s5k5ccgx_esd_reset(state->i2c_client, (enum s5k5ccgx_esd_reset) arg);
		}
		break;
	}
	case S5K5CCGX_IOCTL_EXPOSURE_METER:
	{
		PCAM_DEBUG("S5K5CCGX_IOCTL_EXPOSURE_METER");
		if(state->parms.mode_scene == SCENE_AUTO){
			err = s5k5ccgx_set_from_table(state->i2c_client, "metering" , &state->regs->metering,
						ARRAY_SIZE(state->regs->metering), arg);
			if (err < 0) {
				dev_err(&state->i2c_client->dev,
					"%s: failed to set metering-mode default value\n",
					__func__);
				return -EFAULT;
			}
		}
		state->parms.mode_metering = arg;
		break;
	}
	case S5K5CCGX_IOCTL_ISO:
	{
		PCAM_DEBUG("S5K5CCGX_IOCTL_ISO");
		err = s5k5ccgx_set_iso(arg);
		if (err < 0) {
			dev_err(&state->i2c_client->dev,
				"%s: failed to set iso-mode default value\n",
				__func__);
			return -EFAULT;
		}
		state->parms.mode_iso = arg;
		break;
	}
	case S5K5CCGX_IOCTL_GET_AF:
	{
		PCAM_DEBUG("S5K5CCGX_IOCTL_GET_AF");

		if (copy_from_user(&af_mode, (const void __user *)arg, sizeof(struct s5k5ccgx_af_result))) {
			pr_info("%s %d\n", __func__, __LINE__);
			return -EFAULT;
		}
		s5k5ccgx_autofocus_getresult(state->i2c_client, &af_mode);
		if (copy_to_user((void __user *)arg, &af_mode, sizeof(struct s5k5ccgx_af_result))) {
			pr_info("%s %d\n", __func__, __LINE__);
			return -EFAULT;
		}
		break;
	}

	case S5K5CCGX_IOCTL_EXT_CONTROL:
	{
		PCAM_DEBUG("S5K5CCGX_IOCTL_FLASH_CONTROL");
		if(!(state->preview_framesize_index == S5K5CCGX_PREVIEW_PVGA)){
			switch (arg) {
			case FLASH_EXT_OFF:
				state->flash_on = false;
				state->pdata->flash_onoff(0);
				break;
			case FLASH_EXT_PRE_OFF:
			if (state->flash_on) {
				s5k5ccgx_set_from_table(state->i2c_client, "AF assist flash end",
							&state->regs->af_assist_flash_end, 1, 0);
				s5k5ccgx_set_from_table(state->i2c_client, "Flash AE clear",
							&state->regs->flash_ae_clear, 1, 0);
				state->flash_on = false;
				state->pdata->af_assist_onoff(0);
				msleep(100);//added temporary
				}
				break;
			default:
				break;
			}
		}
		break;
	}
	case S5K5CCGX_IOCTL_EXIF_INFO:
	{
		if (copy_to_user((void __user *)arg, &state->exif_info,
					sizeof(state->exif_info))) {
			return -EFAULT;
		}
		break;
	}
	//modify to TouchAF by Teddy
	case S5K5CCGX_IOCTL_TOUCHAF:
	{
		struct s5k5ccgx_touchaf_pos tpos;
		if (copy_from_user(&tpos, (const void __user *)arg,sizeof(struct s5k5ccgx_touchaf_pos))) {
		    pr_info("%s %d\n", __func__, __LINE__);
		    return -EFAULT;
		}
		return s5k5ccgx_set_touchaf(&tpos);
	}
#ifdef FACTORY_TEST
	case S5K5CCGX_IOCTL_DTP_TEST:
	{
		int status = 0;
		PCAM_DEBUG( "%s: S5K5CCGX_IOCTL_DTP_TEST Entered!!! dtpTest = %d\n", __func__, (int)arg);
		if (dtpTest == 1 && arg == 0)
			status = s5k5ccgx_check_dataline_stop(client, arg);
		dtpTest = arg;
		return status;
	}
#endif
#ifdef CONFIG_MACH_SAMSUNG_P5W_KT	//devide internal and market app : goggles, QRcode, etc..
	case S5K5CCGX_IOCTL_APPMODE:
	{
		PCAM_DEBUG("S5K5CCGX_IOCTL_APPMODE : %d", arg);
		return s5k5ccgx_set_app_mode(arg);
	}
#endif
	}

	dev_dbg(&state->i2c_client->dev, "%send\n", __func__);

	return 0;
}

#ifdef CONFIG_VIDEO_S5K5CCGX_DEBUG
static void s5k5ccgx_dump_regset(struct s5k5ccgx_regset *regset)
{
	if ((regset->data[0] == 0x00) && (regset->data[1] == 0x2A)) {
		if (regset->size <= 6)
			pr_err("odd regset size %d\n", regset->size);
		pr_info("regset: addr = 0x%02X%02X, data[0,1] = 0x%02X%02X,"
			" total data size = %d\n",
			regset->data[2], regset->data[3],
			regset->data[6], regset->data[7],
			regset->size-6);
	} else {
		pr_info("regset: 0x%02X%02X%02X%02X\n",
			regset->data[0], regset->data[1],
			regset->data[2], regset->data[3]);
		if (regset->size != 4)
			pr_err("odd regset size %d\n", regset->size);
	}
}
#endif

static int s5k5ccgx_i2c_write_block(struct i2c_client *client, u8 *buf, int size)
{
	int retry_count = 5;
	int ret;
	struct i2c_msg msg = {client->addr, 0, size, buf};

#ifdef CONFIG_VIDEO_S5K5CCGX_DEBUG
	if (s5k5ccgx_debug_mask & S5K5CCGX_DEBUG_I2C_BURSTS) {
		if ((buf[0] == 0x0F) && (buf[1] == 0x12))
			pr_info("%s : data[0,1] = 0x%02X%02X,"
				" total data size = %d\n",
				__func__, buf[2], buf[3], size-2);
		else
			pr_info("%s : 0x%02X%02X%02X%02X\n",
				__func__, buf[0], buf[1], buf[2], buf[3]);
	}
#endif

	do {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (likely(ret == 1))
			break;
		msleep(POLL_TIME_MS);
	} while (retry_count-- > 0);
	if (ret != 1) {
		dev_err(&state->i2c_client->dev, "%s: I2C is not working.\n", __func__);
		return -EIO;
	}

	return 0;
}

/*
 * Parse the init_reg2 array into a number of register sets that
 * we can send over as i2c burst writes instead of writing each
 * entry of init_reg2 as a single 4 byte write.  Write the
 * new data structures and then free them.
 */
static int s5k5ccgx_write_init_reg2_burst(struct i2c_client *client)
{
	struct s5k5ccgx_regset *regset_table;
	struct s5k5ccgx_regset *regset;
	struct s5k5ccgx_regset *end_regset;
	u8 *regset_data;
	u8 *dst_ptr;
	const u32 *end_src_ptr;
	bool flag_copied;
	int init_reg_2_array_size = state->regs->init_reg_2.array_size;
	int init_reg_2_size = init_reg_2_array_size * sizeof(u32);
	const u32 *src_ptr = state->regs->init_reg_2.reg;
	u32 src_value;
	int err;

	pr_debug("%s : start\n", __func__);

	regset_data = vmalloc(init_reg_2_size);
	if (regset_data == NULL)
		return -ENOMEM;
	regset_table = vmalloc(sizeof(struct s5k5ccgx_regset) * init_reg_2_size);
	if (regset_table == NULL) {
		kfree(regset_data);
		return -ENOMEM;
	}

	dst_ptr = regset_data;
	regset = regset_table;
	end_src_ptr = &state->regs->init_reg_2.reg[init_reg_2_array_size];

	src_value = *src_ptr++;
	while (src_ptr <= end_src_ptr) {
		/* initial value for a regset */
		regset->data = dst_ptr;
		flag_copied = false;
		*dst_ptr++ = src_value >> 24;
		*dst_ptr++ = src_value >> 16;
		*dst_ptr++ = src_value >> 8;
		*dst_ptr++ = src_value;

		/* check subsequent values for a data flag (starts with
		   0x0F12) or something else */
		do {
			src_value = *src_ptr++;
			if ((src_value & 0xFFFF0000) != 0x0F120000) {
				/* src_value is start of next regset */
				regset->size = dst_ptr - regset->data;
				regset++;
				break;
			}
			/* copy the 0x0F12 flag if not done already */
			if (!flag_copied) {
				*dst_ptr++ = src_value >> 24;
				*dst_ptr++ = src_value >> 16;
				flag_copied = true;
			}
			/* copy the data part */
			*dst_ptr++ = src_value >> 8;
			*dst_ptr++ = src_value;
		} while (src_ptr < end_src_ptr);
	}
	pr_debug("%s : finished creating table\n", __func__);

	end_regset = regset;
	pr_debug("%s : first regset = %p, last regset = %p, count = %d\n",
		__func__, regset_table, regset, end_regset - regset_table);
	pr_debug("%s : regset_data = %p, end = %p, dst_ptr = %p\n", __func__,
		regset_data, regset_data + (init_reg_2_size * sizeof(u32)),
		dst_ptr);

#ifdef CONFIG_VIDEO_S5K5CCGX_DEBUG
	if (s5k5ccgx_debug_mask & S5K5CCGX_DEBUG_I2C_BURSTS) {
		int last_regset_end_addr = 0;
		regset = regset_table;
		do {
			s5k5ccgx_dump_regset(regset);
			if (regset->size > 4) {
				int regset_addr = (regset->data[2] << 8 |
						regset->data[3]);
				if (last_regset_end_addr == regset_addr)
					pr_info("%s : this regset can be"
						" combined with previous\n",
						__func__);
				last_regset_end_addr = (regset_addr +
							regset->size - 6);
			}
			regset++;
		} while (regset < end_regset);
	}
#endif
	regset = regset_table;
	pr_debug("%s : start writing init reg 2 bursts\n", __func__);
	do {
		if (regset->size > 4) {
			/* write the address packet */
			err = s5k5ccgx_i2c_write_block(client, regset->data, 4);
			if (err)
				break;
			/* write the data in a burst */
			err = s5k5ccgx_i2c_write_block(client, regset->data+4,
						regset->size-4);

		} else
			err = s5k5ccgx_i2c_write_block(client, regset->data,
						regset->size);
		if (err)
			break;
		regset++;
	} while (regset < end_regset);

	pr_debug("%s : finished writing init reg 2 bursts\n", __func__);

	vfree(regset_data);
	vfree(regset_table);

	return err;
}

#ifdef FACTORY_CHECK
static ssize_t cameraflash_file_cmd_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
		/*Reserved*/
	return 0;
}

static ssize_t cameraflash_file_cmd_store(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t size)
{
	int value;

	sscanf(buf, "%d", &value);

	if (value == 0) {
		printk(KERN_INFO "[Factory flash]OFF\n");
		state->pdata->torch_onoff(0);
	} else {
		printk(KERN_INFO "[Factory flash]ON\n");
		state->pdata->torch_onoff(1);
	}

	return size;
}

static DEVICE_ATTR(cameraflash, 0660, cameraflash_file_cmd_show, cameraflash_file_cmd_store);
extern struct class *sec_class;
struct device *sec_cam_dev = NULL;



ssize_t camtype_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	printk("%s \n", __func__);
	char * sensorname = "NG";
	sensorname = "SLSI_S5K5CCGX";
	return sprintf(buf,"%s\n", sensorname);
}


ssize_t camtype_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t size)
{
	printk(KERN_NOTICE "%s:%s\n", __func__, buf);

	return size;
}

static DEVICE_ATTR(camtype,0660, camtype_show, camtype_store);
#endif

static int s5k5ccgx_open(struct inode *inode, struct file *file)
{
	struct i2c_client *client = state->i2c_client;

	dev_dbg(&state->i2c_client->dev, "%s\n", __func__);

	file->private_data = state;
	state->lastmode_info = MODE_INFO_PREVIEW;
	state->preview_framesize_index = S5K5CCGX_PREVIEW_XGA;
	state->lastPreview_framesize_index = S5K5CCGX_PREVIEW_MAX;
	state->parms.mode_scene = SCENE_AUTO;	
	state->parms.mode_focus = FOCUS_AUTO;
	state->power_status = false;
	state->esd_status = false;	
	//modify to TouchAF by Teddy
	state->touchaf_enable = false;
	state->bHD_enable = false;
	state->isAFCancel = false;

	if (state->pdata && state->pdata->power_on)
		state->pdata->power_on();

	if (s5k5ccgx_init_regs(client) < 0) {
		pr_err("%s, init regs failed\n", __func__);
		state->pdata->power_off();		
		state->power_status = false;		
		return -ENODEV;
	}
#ifdef CONFIG_LOAD_FILE
	{
	int temp2 = 1;
	if (s5k5ccgx_regs_table_init(temp2)) {
		printk("%s: config file read fail\n", __func__);
		return -EIO;
	}
	msleep(100);
	}
#endif

#if 0

	if (s5k5ccgx_set_from_table(client, "init reg 1",
		&state->regs->init_reg_1, 1, 0) < 0) {
		pr_err("%s, init regs 1 failed\n", __func__);
		return -EIO;
	}

	/* delay 10ms after wakeup of SOC processor */
	msleep(10);
#ifdef CONFIG_LOAD_FILE
	if (s5k5ccgx_set_from_table(client, "init reg 2",
		&state->regs->init_reg_2, 1, 0) < 0)
		return -EIO;
	/*if (s5k5ccgx_write_init_reg2_burst(client) < 0) {
			dev_err(&state->i2c_client->dev, "%s, burst mode failed\n", __func__);
			return -EIO;
		}*/
#else
	if (s5k5ccgx_set_from_table(client, "init reg 2",
		&state->regs->init_reg_2, 1, 0) < 0) {
		pr_err("%s, init regs 2 failed\n", __func__);
		return -EIO;
	}
	/*if (s5k5ccgx_write_init_reg2_burst(client) < 0) {
		dev_err(&state->i2c_client->dev, "%s, burst mode failed\n", __func__);
		return -EIO;
	}*/
#endif
#endif
	return 0;
}


int s5k5ccgx_release(struct inode *inode, struct file *file)
{
	dev_dbg(&state->i2c_client->dev, "%s\n", __func__);
	/*if (state->torch_on == true) {
		state->pdata->torch_onoff(0);
		state->torch_on = false;
	}*/

	if (state->pdata && state->pdata->power_off)
		state->pdata->power_off();
	state->pdata->torch_onoff(0);
	state->power_status = false;
	state->esd_status = false;	
	return 0;
}

static const struct file_operations s5k5ccgx_fileops = {
	.owner = THIS_MODULE,
	.open = s5k5ccgx_open,
	.unlocked_ioctl = s5k5ccgx_ioctl,
	.compat_ioctl = s5k5ccgx_ioctl,
	.release = s5k5ccgx_release,
};

static struct miscdevice s5k5ccgx_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "s5k5ccgx",
	.fops = &s5k5ccgx_fileops,
};

/*
 * s5k5ccgx_probe
 * Fetching platform data is being done with s_config subdev call.
 * In probe routine, we just register subdev device
 */
static int s5k5ccgx_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err;
	int dev;
	char s5k5ccgx_name[20] = "s5k5ccgx";
	char s5k5ccgx_pmic_name[20] = "s5k5ccgx_pmic";
	PCAM_DEBUG("START");
	
	if (strcmp(s5k5ccgx_name, id->name) == 0)
		dev = 0;
	if (strcmp(s5k5ccgx_pmic_name, id->name) == 0)
		dev = 1;

	switch (dev) {
		case 0:
			state = kzalloc(sizeof(struct s5k5ccgx_state), GFP_KERNEL);
			if (state == NULL) {
				pr_err("%s,, state allocation failed\n",__func__);
				return -ENOMEM;
			}

			state->i2c_client = client;
			if (!state->i2c_client) {
				pr_err("s5k5ccgx: Unknown I2C client!\n");
				kfree(state);
				return -ENODEV;
			}

			state->pdata = client->dev.platform_data;
			if (!state->pdata) {
				pr_err("s5k5ccgx: Unknown platform data!\n");
				kfree(state);
				return -ENODEV;
			}

			err = misc_register(&s5k5ccgx_device);
			if (err) {
				pr_err("s5k5ccgx: Unable to register misc device!\n");
				kfree(state);
				return err;
			}
#ifdef FACTORY_CHECK
			{
				static bool  camtype_init = false;
				if (sec_cam_dev == NULL)
				{
					sec_cam_dev = device_create(sec_class, NULL, 0, NULL, "sec_s5k5ccgx");
					if (IS_ERR(sec_cam_dev))
						pr_err("Failed to create device(sec_lcd_dev)!\n");
				}
			
				if (sec_cam_dev != NULL && camtype_init == false)
				{
					camtype_init = true;
					if (device_create_file(sec_cam_dev, &dev_attr_camtype) < 0)
						pr_err("Failed to create device file(%s)!\n", dev_attr_camtype.attr.name);
				}
			}
			{
				static bool  camflash_init = false;
				if (sec_cam_dev != NULL && camflash_init == false)
				{
					camflash_init = true;
					if (device_create_file(sec_cam_dev, &dev_attr_cameraflash) < 0)
						pr_err("Failed to create device file(%s)!\n", dev_attr_cameraflash.attr.name);
				}
			}

#endif

			PCAM_DEBUG("5MP camera S5K5CCGX loaded.");
			break;
#ifdef CONFIG_MACH_SAMSUNG_P5
		case 1:
			i2c_client_pmic = client;
			if (!i2c_client_pmic) {
				pr_err("s5k5ccgx: Unknown i2c_client_pmic client!\n");
				return -ENODEV;
			}
			PCAM_DEBUG("5MP camera S5K5CCGX i2c_client_pmic loaded.");
			break;
#endif
	}
	i2c_set_clientdata(client, state);

	return 0;
}

static int s5k5ccgx_remove(struct i2c_client *client)
{
	dev_dbg(&state->i2c_client->dev, "%s\n", __func__);

	mutex_destroy(&state->ctrl_lock);

	misc_deregister(&s5k5ccgx_device);
	kfree(state);

	return 0;
}

static const struct i2c_device_id s5k5ccgx_id[] = {
	{ "s5k5ccgx", 0 },
	{}
};


MODULE_DEVICE_TABLE(i2c, s5k5ccgx_id);

static struct i2c_driver s5k5ccgx_i2c_driver = {
	.driver = {
		.name = "s5k5ccgx",
		.owner = THIS_MODULE,
	},
	.probe = s5k5ccgx_probe,
	.remove = s5k5ccgx_remove,
	.id_table = s5k5ccgx_id,
};

#ifdef CONFIG_MACH_SAMSUNG_P5
static const struct i2c_device_id s5k5ccgx_pmic_id[] = {
	{ "s5k5ccgx_pmic", 0 },
	{}
};


MODULE_DEVICE_TABLE(i2c, s5k5ccgx_pmic_id);

static struct i2c_driver s5k5ccgx_i2c_pmic_driver = {
	.driver = {
		.name = "s5k5ccgx_pmic",
		.owner = THIS_MODULE,
	},
	.probe = s5k5ccgx_probe,
	.remove = s5k5ccgx_remove,
	.id_table = s5k5ccgx_pmic_id,
};
#endif
	
static int __init s5k5ccgx_init(void)
{
	int status;
	dev_dbg(&state->i2c_client->dev, "%s\n", __func__);

	status = i2c_add_driver(&s5k5ccgx_i2c_driver);
	if (status) {
		printk(" s5k5ccgx add driver error\n");
		return status;
	}
#ifdef CONFIG_MACH_SAMSUNG_P5
	status = i2c_add_driver(&s5k5ccgx_i2c_pmic_driver);
	if (status) {
		printk(" s5k5ccgx_pmic add driver error\n");
		return status;
	}
#endif
	return 0;
}

static void __exit s5k5ccgx_exit(void)
{
	i2c_del_driver(&s5k5ccgx_i2c_driver);
}

module_init(s5k5ccgx_init);
module_exit(s5k5ccgx_exit);

MODULE_DESCRIPTION("LSI S5K5CCGX 5MP SOC camera driver");
MODULE_LICENSE("GPL");



