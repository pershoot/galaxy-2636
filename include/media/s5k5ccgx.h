/*
 * Copyright (C) 2010 SAMSUNG ELECTRONICS.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#ifndef __S5K5CCGX_H__
#define __S5K5CCGX_H__
#define FACTORY_TEST

#include <linux/ioctl.h>  /* For IOCTL macros */

#define S5K5CCGX_IOCTL_SET_MODE		        _IOW('o', 1, struct s5k5ccgx_mode)
#define S5K5CCGX_IOCTL_TEST_PATTERN       _IOW('o', 2, enum s5k5ccgx_test_pattern)
#define S5K5CCGX_IOCTL_SCENE_MODE	        _IOW('o', 3, enum s5k5ccgx_scene_mode)
#define S5K5CCGX_IOCTL_FOCUS_MODE	        _IOW('o', 4, enum s5k5ccgx_focus_mode)
#define S5K5CCGX_IOCTL_COLOR_EFFECT       _IOW('o', 5, enum s5k5ccgx_color_effect)
#define S5K5CCGX_IOCTL_WHITE_BALANCE      _IOW('o', 6, enum s5k5ccgx_white_balance)
#define S5K5CCGX_IOCTL_FLASH_MODE	        _IOW('o', 7, enum s5k5ccgx_flash_mode)
#define S5K5CCGX_IOCTL_EXPOSURE	          _IOW('o', 8, enum s5k5ccgx_exposure)
#define S5K5CCGX_IOCTL_AF_CONTROL	        _IOW('o', 9, enum s5k5ccgx_autofocus_control)
#define S5K5CCGX_IOCTL_ESD_RESET	        _IOR('o', 11, enum s5k5ccgx_esd_reset)
#define S5K5CCGX_IOCTL_EXIF_INFO          _IOW('o', 14, struct s5k5ccgx_exif_info)
#define S5K5CCGX_IOCTL_EXPOSURE_METER       _IOW('o', 15, enum s5k5ccgx_metering_mode)
#define S5K5CCGX_IOCTL_ISO      _IOW('o', 16, enum s5k5ccgx_iso_mode)
#define S5K5CCGX_IOCTL_ANTISHAKE	        _IOW('o', 17, enum s5k5ccgx_antishake)
#define S5K5CCGX_IOCTL_AUTOCONTRAST	          _IOW('o', 18, enum s5k5ccgx_autocontrast)
#define S5K5CCGX_IOCTL_TOUCHAF	          _IOW('o', 19, struct s5k5ccgx_touchaf_pos)
#define S5K5CCGX_IOCTL_GET_AF		_IOWR('o', 20, struct s5k5ccgx_af_result)
#define S5K5CCGX_IOCTL_EXT_CONTROL	_IOW('o', 21, enum s5k5ccgx_flash_ext_control)
#define S5K5CCGX_IOCTL_CAMMODE		_IOW('o', 22, enum s5k5ccgx_cam_mode)
#ifdef FACTORY_TEST
#define S5K5CCGX_IOCTL_DTP_TEST	        _IOW('o', 23, enum s5k5ccgx_dtp_test)
enum s5k5ccgx_dtp_test {
	DTP_OFF = 0,
	DTP_ON,
};
#endif
#ifdef CONFIG_MACH_SAMSUNG_P5W_KT	//devide internal and market app : goggles, QRcode, etc..
#define S5K5CCGX_IOCTL_APPMODE        _IOW('o', 24, enum s5k5ccgx_app_mode)
#endif

struct s5k5ccgx_exif_info {
	unsigned int info_exptime_numer;
	unsigned int info_exptime_denumer;
	unsigned int info_iso;
	unsigned int info_flash;
};
enum s5k5ccgx_esd_reset{
	ESD_DETECTED = 0,
	ESD_NOT_DETECTED
};
enum s5k5ccgx_flash_ext_control{
	FLASH_EXT_OFF = 0,
	FLASH_EXT_PRE_OFF,				
	FLASH_EXT_ON,
};

struct s5k5ccgx_touchaf_pos {
	__u32 xpos;
	__u32 ypos;
};

struct s5k5ccgx_af_result {
	int mode;
	int param_1;
	int param_2;	
};

enum s5k5ccgx_autofocus_control {
	AF_STOP = 0,
	AF_START,
	//AF_STOP,
	AF_CANCEL,
	CAF_START,
	CAF_STOP
};

enum  s5k5ccgx_scene_mode {
	SCENE_AUTO = 1,
	SCENE_ACTION,
	SCENE_PORTRAIT,
	SCENE_LANDSCAPE,
	SCENE_BEACH,
	SCENE_CANDLE_LIGHT,
	SCENE_FIRE_WORK,
	SCENE_NIGHT,
	SCENE_NIGHT_PORTRAIT,
	SCENE_PARTY,
	SCENE_SNOW,
	SCENE_SPORTS,
	SCENE_STEADY_PHOTO,
	SCENE_SUNSET,
	SCENE_DUSKDAWN,
	SCENE_FALLCOLOR,
	SCENE_THEATER,
	SCENE_BARCODE,
	SCENE_TEXT,
	SCENE_BACKLIGHT,
	SCENE_MODE_MAX
};

enum s5k5ccgx_focus_mode{
	FOCUS_AUTO = 1,
	FOCUS_INFINITY,
	FOCUS_MACRO,
	FOCUS_HYPER_FOCAL,
	FOCUS_FIXED,
	FOCUS_FACE_DETECT,
	FOCUS_MODE_MAX
};

enum s5k5ccgx_color_effect {
	EFFECT_AQUA = 1,
	EFFECT_BLACKBOARD,
	EFFECT_MONO,
	EFFECT_NEGATIVE,
	EFFECT_NONE,
	EFFECT_POSTERIZE,
	EFFECT_SEPIA,
	EFFECT_SOLARIZE,
	EFFECT_WHITEBOARD,
	EFFECT_MODE_MAX
};

enum s5k5ccgx_white_balance {
	WB_AUTO = 1,
	WB_INCANDESCENT,
	WB_FLUORESCENT,
	WB_WARMFLUORESCENT,
	WB_DAYLIGHT,
	WB_CLOUDY,
	WB_SHADE,
	WB_TWILIGHT,
	WB_MODE_MAX
};

enum s5k5ccgx_flash_mode {
	FLASH_AUTO = 1,
	FLASH_ON,
	FLASH_OFF,
	FLASH_TORCH,
	FLASH_MODE_MAX
};

enum s5k5ccgx_exposure {
	EXPOSURE_M4 = 1,
	EXPOSURE_M3,
	EXPOSURE_M2,
	EXPOSURE_M1,
	EXPOSURE_ZERO,
	EXPOSURE_P1,
	EXPOSURE_P2,
	EXPOSURE_P3,
	EXPOSURE_P4,	
	EXPOSURE_MODE_MAX
};

enum s5k5ccgx_iso_mode {
	ISO_AUTO = 1,
	ISO_100,
	ISO_200,
	ISO_400,
	ISO_MAX,
};

enum s5k5ccgx_cam_mode {
	CAMMODE_CAMERA = 1,
	CAMMODE_CAMCORDER,
	CAMMODE_MMS_CAMCORDER,
	CAMMODE_MAX,
};

enum s5k5ccgx_metering_mode {
	METERING_BASE = 0,
	METERING_CENTER,
	METERING_SPOT,
	METERING_MATRIX,
	METERING_MAX,
};

enum s5k5ccgx_face_detection {
	FACE_DETECTION_OFF = 0,
	FACE_DETECTION_ON,
	FACE_DETECTION_NOLINE,
	FACE_DETECTION_ON_BEAUTY,
	FACE_DETECTION_MAX,
};

enum s5k5ccgx_test_pattern {
	TEST_PATTERN_NONE,
	TEST_PATTERN_COLORBARS,
	TEST_PATTERN_CHECKERBOARD
};

enum s5k5ccgx_saturation_mode {
	SATURATION_MINUS_2 = 0,
	SATURATION_MINUS_1,
	SATURATION_DEFAULT,
	SATURATION_PLUS_1,
	SATURATION_PLUS_2,
	SATURATION_MAX,
};

enum s5k5ccgx_contrast_mode {
	CONTRAST_MINUS_4 = 0,
	CONTRAST_MINUS_3,
	CONTRAST_MINUS_2,
	CONTRAST_MINUS_1,
	CONTRAST_DEFAULT,
	CONTRAST_PLUS_1,
	CONTRAST_PLUS_2,
	CONTRAST_PLUS_3,
	CONTRAST_PLUS_4,
	CONTRAST_MAX,
};

enum s5k5ccgx_sharpness_mode {
	SHARPNESS_MINUS_3 = 0,
	SHARPNESS_MINUS_2,
	SHARPNESS_MINUS_1,
	SHARPNESS_DEFAULT,
	SHARPNESS_PLUS_1,
	SHARPNESS_PLUS_2,
	SHARPNESS_PLUS_3,
	SHARPNESS_MAX,
};

enum s5k5ccgx_frame_rate {
	FRAME_RATE_AUTO = 0,
	FRAME_RATE_5	= 5,
	FRAME_RATE_7	= 7,
	FRAME_RATE_10	= 10,
	FRAME_RATE_15	= 15,
	FRAME_RATE_20	= 20,
	FRAME_RATE_30	= 30,
	FRAME_RATE_MAX
};

enum s5k5ccgx_antishake {
	ANTISHAKE_OFF = 0,
	ANTISHAKE_ON,
};

enum s5k5ccgx_autocontrast {
	AUTOCONTRAST_OFF = 0,
	AUTOCONTRAST_ON,
};

enum s5k5ccgx_WDR {
	WDR_ON = 0,
	WDR_OFF,
};

enum s5k5ccgx_mode_info {
	MODE_INFO_PREVIEW = 0,
	MODE_INFO_STILL,
	MODE_INFO_VIDEO,
	MODE_INFO_MAX
};

#ifdef CONFIG_MACH_SAMSUNG_P5W_KT	//devide internal and market app : goggles, QRcode, etc..
enum s5k5ccgx_app_mode {
	APPMODE_SEC_APP = 0,
	APPMODE_3RD_APP,
	APPMODE_MAX,
};
#endif


struct s5k5ccgx_mode {
	int xres;
	int yres;
	enum s5k5ccgx_mode_info mode_info;
};

extern int s5k5ccgx_write_regs_pm(struct i2c_client *client, const u16 regs[], int size);

#ifdef __KERNEL__
struct s5k5ccgx_platform_data {
	void (*power_on)(void);
	void (*power_off)(void);
	int (*flash_onoff)(int);
	int (*af_assist_onoff)(int);
	int (*torch_onoff)(int);
	unsigned int (*isp_int_read)(void);
};
#endif /* __KERNEL__ */

#endif  /* __S5K5CCGX_H__ */

