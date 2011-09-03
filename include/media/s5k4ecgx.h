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

#ifndef __S5K4ECGX_H__
#define __S5K4ECGX_H__

#include <linux/ioctl.h>  /* For IOCTL macros */



#define S5K4ECGX_IOCTL_SET_MODE		        _IOW('o', 1, struct s5k4ecgx_mode)
#define S5K4ECGX_IOCTL_TEST_PATTERN       _IOW('o', 2, enum s5k4ecgx_test_pattern)
#define S5K4ECGX_IOCTL_SCENE_MODE	        _IOW('o', 3, enum s5k4ecgx_scene_mode)
#define S5K4ECGX_IOCTL_FOCUS_MODE	        _IOW('o', 4, enum s5k4ecgx_focus_mode)
#define S5K4ECGX_IOCTL_COLOR_EFFECT       _IOW('o', 5, enum s5k4ecgx_color_effect)
#define S5K4ECGX_IOCTL_WHITE_BALANCE      _IOW('o', 6, enum s5k4ecgx_white_balance)
#define S5K4ECGX_IOCTL_FLASH_MODE	        _IOW('o', 7, enum s5k4ecgx_flash_mode)
#define S5K4ECGX_IOCTL_EXPOSURE	          _IOW('o', 8, enum s5k4ecgx_exposure)
#define S5K4ECGX_IOCTL_AF_CONTROL	        _IOW('o', 9, enum s5k4ecgx_autofocus_control)
#define S5K4ECGX_IOCTL_AF_RESULT	        _IOR('o', 10, struct s5k4ecgx_autofocus_result)
#define S5K4ECGX_IOCTL_ESD_RESET	        _IOR('o', 11, enum s5k4ecgx_esd_reset)
#define S5K4ECGX_IOCTL_LENS_SOFT_LANDING  _IOW('o', 12, unsigned int)
#define S5K4ECGX_IOCTL_RECORDING_FRAME    _IOW('o', 13, enum s5k4ecgx_recording_frame)
#define S5K4ECGX_IOCTL_EXIF_INFO          _IOW('o', 14, struct s5k4ecgx_exif_info)
#define S5K4ECGX_IOCTL_EXPOSURE_METER       _IOW('o', 15, enum s5k4ecgx_metering_mode)
#define S5K4ECGX_IOCTL_ISO      _IOW('o', 16, enum s5k4ecgx_iso_mode)
#define S5K4ECGX_IOCTL_ANTISHAKE	        _IOW('o', 17, enum s5k4ecgx_antishake)
#define S5K4ECGX_IOCTL_AUTOCONTRAST	          _IOW('o', 18, enum s5k4ecgx_autocontrast)
#define S5K4ECGX_IOCTL_TOUCHAF	          _IOW('o', 19, struct s5k4ecgx_touchaf_pos)
#define S5K4ECGX_IOCTL_GET_AF		_IOWR('o', 20, struct s5k4ecgx_af_result)
#define S5K4ECGX_IOCTL_EXT_CONTROL	_IOW('o', 21, struct s5k4ecgx_pcam_ex_struct)
#define S5K4ECGX_IOCTL_DTP_TEST     _IOW('o', 22, struct s5k4ecgx_pcam_ex_struct)

struct s5k4ecgx_exif_info {
	unsigned int info_exptime_numer;
	unsigned int info_exptime_denumer;
	unsigned int info_tv_numer;
	unsigned int info_tv_denumer;
	unsigned int info_av_numer;
	unsigned int info_av_denumer;
	unsigned int info_bv_numer;
	int info_bv_denumer;
	unsigned int info_ebv_numer;
	int info_ebv_denumer;
	__u16 info_iso;
	unsigned int info_flash;
};
enum s5k4ecgx_esd_reset{
	ESD_DETECTED = 0,
	ESD_NOT_DETECTED
};


struct s5k4ecgx_autofocus_result {
	__u32 value;
};

struct s5k4ecgx_touchaf_pos {
	__u32 xpos;
	__u32 ypos;
};


struct s5k4ecgx_pcam_ex_struct {
	int mode;
	int param_1;
	int param_2;	
};

enum s5k4ecgx_ex_ctrl{
	EX_CTRL_FLASH = 0,
	EX_CTRL_FRAME_FIXED,
	EX_CTRL_FRAME_NFIXED,
	EX_CTRL_ASSIST_FLASH,
};

enum s5k4ecgx_ex_flash_state{
	EX_FLASH_OFF = 0,
	EX_FLASH_ON,
	EX_ASSIST_FLASH_OFF,
	EX_ASSIST_FLASH_ON,
};

struct s5k4ecgx_af_result {
	int mode;
	int param_1;
	int param_2;	
};

enum s5k4ecgx_autofocus_control {
	AF_STOP = 0,
	AF_START,
	//AF_STOP,
	AF_CANCEL,
	CAF_START,
	CAF_STOP
};

enum  s5k4ecgx_scene_mode {
	SCENE_AUTO = 0,
	SCENE_ACTION,
	SCENE_PORTRAIT,
	SCENE_LANDSCAPE,
	SCENE_NIGHT,
	SCENE_NIGHT_PORTRAIT,
	SCENE_THEATER,
	SCENE_BEACH,
	SCENE_SNOW,
	SCENE_SUNSET,
	SCENE_STEADY_PHOTO,
	SCENE_FIRE_WORK,
	SCENE_PARTY,
	SCENE_CANDLE_LIGHT,
	SCENE_DAWN,
	SCENE_MODE_MAX
};

enum s5k4ecgx_focus_mode{
	FOCUS_AUTO = 0,
	FOCUS_INFINITY,
	FOCUS_MACRO,
	FOCUS_FACE_DETECT,
	FOCUS_HYPER_FOCAL,
	FOCUS_MODE_MAX
};

enum s5k4ecgx_recording_frame{
	RECORDING_CAF = 0,
	RECORDING_PREVIEW
};

enum s5k4ecgx_color_effect {
	EFFECT_NONE = 0,
	EFFECT_MONO,
	EFFECT_SEPIA,
	EFFECT_NEGATIVE,
	EFFECT_SOLARIZE,
	EFFECT_POSTERIZE,
	EFFECT_MODE_MAX
};

enum s5k4ecgx_white_balance {
	WB_AUTO = 0,
	WB_DAYLIGHT,
	WB_INCANDESCENT,
	WB_FLUORESCENT,
	WB_CLOUDY,
	WB_MODE_MAX
};

enum s5k4ecgx_flash_mode {
	FLASH_AUTO = 0,
	FLASH_ON,
	FLASH_OFF,
	FLASH_TORCH,
	FLASH_MODE_MAX
};

enum s5k4ecgx_exposure {
	EXPOSURE_P2 = 0,
	EXPOSURE_P1,
	EXPOSURE_ZERO,
	EXPOSURE_M1,
	EXPOSURE_M2,
	EXPOSURE_MODE_MAX
};

enum s5k4ecgx_iso_mode {
	ISO_AUTO = 0,
	ISO_50,
	ISO_100,
	ISO_200,
	ISO_400,
	/*ISO_800,
	ISO_1600,
	ISO_SPORTS,
	ISO_NIGHT,
	ISO_MOVIE,*/
	ISO_MAX,
};

enum s5k4ecgx_metering_mode {
	METERING_BASE = 0,
	METERING_MATRIX,
	METERING_CENTER,
	METERING_SPOT,
	METERING_MAX,
};

enum s5k4ecgx_face_detection {
	FACE_DETECTION_OFF = 0,
	FACE_DETECTION_ON,
	FACE_DETECTION_NOLINE,
	FACE_DETECTION_ON_BEAUTY,
	FACE_DETECTION_MAX,
};

enum s5k4ecgx_test_pattern {
	TEST_PATTERN_NONE,
	TEST_PATTERN_COLORBARS,
	TEST_PATTERN_CHECKERBOARD
};

enum s5k4ecgx_saturation_mode {
	SATURATION_MINUS_2 = 0,
	SATURATION_MINUS_1,
	SATURATION_DEFAULT,
	SATURATION_PLUS_1,
	SATURATION_PLUS_2,
	SATURATION_MAX,
};

enum s5k4ecgx_contrast_mode {
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

enum s5k4ecgx_sharpness_mode {
	SHARPNESS_MINUS_3 = 0,
	SHARPNESS_MINUS_2,
	SHARPNESS_MINUS_1,
	SHARPNESS_DEFAULT,
	SHARPNESS_PLUS_1,
	SHARPNESS_PLUS_2,
	SHARPNESS_PLUS_3,
	SHARPNESS_MAX,
};

enum s5k4ecgx_frame_rate {
	FRAME_RATE_AUTO = 0,
	FRAME_RATE_5	= 5,
	FRAME_RATE_7	= 7,
	FRAME_RATE_10	= 10,
	FRAME_RATE_15	= 15,
	FRAME_RATE_20	= 20,
	FRAME_RATE_30	= 30,
	FRAME_RATE_MAX
};

enum s5k4ecgx_antishake {
	ANTISHAKE_OFF = 0,
	ANTISHAKE_ON,
};

enum s5k4ecgx_autocontrast {
	AUTOCONTRAST_OFF = 0,
	AUTOCONTRAST_ON,
};

enum s5k4ecgx_dtp_test {
	DTP_OFF = 0,
	DTP_ON,
};

enum s5k4ecgx_WDR {
	WDR_ON = 0,
	WDR_OFF,
};

struct s5k4ecgx_mode {
	int xres;
	int yres;
	__u32 frame_length;
	__u32 coarse_time;
	__u16 gain;
};


#ifdef __KERNEL__
struct s5k4ecgx_platform_data {
	void (*power_on)(void);
	void (*power_off)(void);
	int (*flash_onoff)(int);
	int (*af_assist_onoff)(int);
	int (*torch_onoff)(int);
	unsigned int (*isp_int_read)(void);
};
#endif /* __KERNEL__ */

#endif  /* __S5K4ECGX_H__ */
