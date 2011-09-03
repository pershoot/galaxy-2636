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

#ifndef __imx073_H__
#define __imx073_H__

#include <linux/ioctl.h>  /* For IOCTL macros */

#define IMX073_IOCTL_SET_MODE           _IOW('o', 1, struct imx073_mode)
#define IMX073_IOCTL_TEST_PATTERN       _IOW('o', 2, enum imx073_test_pattern)
#define IMX073_IOCTL_SCENE_MODE         _IOW('o', 3, enum imx073_scene_mode)
#define IMX073_IOCTL_FOCUS_MODE         _IOW('o', 4, enum imx073_focus_mode)
#define IMX073_IOCTL_COLOR_EFFECT       _IOW('o', 5, enum imx073_color_effect)
#define IMX073_IOCTL_WHITE_BALANCE      _IOW('o', 6, enum imx073_white_balance)
#define IMX073_IOCTL_FLASH_MODE         _IOW('o', 7, enum imx073_flash_mode)
#define IMX073_IOCTL_EXPOSURE           _IOW('o', 8, enum imx073_exposure)
#define IMX073_IOCTL_AF_CONTROL         _IOW('o', 9, enum imx073_autofocus_control)
#define IMX073_IOCTL_AF_RESULT          _IOR('o', 10, struct imx073_autofocus_result)
#define IMX073_IOCTL_ESD_RESET          _IOR('o', 11, enum imx073_esd_reset)
#define IMX073_IOCTL_LENS_SOFT_LANDING  _IOW('o', 12, unsigned int)
#define IMX073_IOCTL_RECORDING_FRAME    _IOW('o', 13, enum imx073_recording_frame)
#define IMX073_IOCTL_EXIF_INFO          _IOW('o', 14, struct m5mo_exif_info)
#define IMX073_IOCTL_AEAWB_LOCKUNLOCK          _IOW('o', 15, enum imx073_aeawb_lockunlock)

enum imx073_aeawb_lockunlock {
	AE_AWB_LOCK = 0,
	AE_AWB_UNLOCK
};

struct m5mo_exif_info {
	unsigned int info_exptime_numer;
	unsigned int info_exptime_denumer;
	unsigned int info_tv_numer;
	unsigned int info_tv_denumer;
	unsigned int info_av_numer;
	unsigned int info_av_denumer;
	unsigned int info_bv_numer;
	int info_bv_denumer;
	__u16 info_iso;
	unsigned int info_flash;
};

enum m5mo_isp_mode {
	MODE_SYSTEM_INIT = 0,
	MODE_PARAMETER_SETTING,
	MODE_MONITOR,
	MODE_STILL_CAPTURE
};

enum imx073_esd_reset {
	ESD_DETECTED = 0,
	ESD_NOT_DETECTED
};

struct imx073_autofocus_result {
	__u32 value;
};

enum imx073_autofocus_control {
	AF_START = 0,
	AF_STOP,
	CAF_START,
	CAF_STOP
};

enum imx073_scene_mode {
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
	SCENE_MODE_MAX
};

enum imx073_focus_mode {
	FOCUS_AUTO = 0,
	FOCUS_INFINITY,
	FOCUS_MACRO,
	FOCUS_HYPER_FOCAL,
	FOCUS_MODE_MAX
};

enum imx073_color_effect {
	EFFECT_NONE = 0,
	EFFECT_MONO,
	EFFECT_SEPIA,
	EFFECT_NEGATIVE,
	EFFECT_SOLARIZE,
	EFFECT_POSTERIZE,
	EFFECT_MODE_MAX
};

enum imx073_white_balance {
	WB_AUTO = 0,
	WB_DAYLIGHT,
	WB_INCANDESCENT,
	WB_FLUORESCENT,
	WB_CLOUDY,
	WB_MODE_MAX
};

enum imx073_flash_mode {
	FLASH_AUTO = 0,
	FLASH_ON,
	FLASH_OFF,
	FLASH_TORCH,
	FLASH_MODE_MAX
};

enum imx073_exposure {
	EXPOSURE_P2 = 0,
	EXPOSURE_P1,
	EXPOSURE_ZERO,
	EXPOSURE_M1,
	EXPOSURE_M2,
	EXPOSURE_MODE_MAX
};


enum imx073_test_pattern {
	TEST_PATTERN_NONE,
	TEST_PATTERN_COLORBARS,
	TEST_PATTERN_CHECKERBOARD
};

/*firmware standard*/
enum imx073_firmware_standard {
	FWUPDATE = 1,
	FWDUMP
};

struct imx073_otp_data {
	/* Only the first 5 bytes are actually used. */
	__u8 sensor_serial_num[6];
	__u8 part_num[8];
	__u8 lens_id[1];
	__u8 manufacture_id[2];
	__u8 factory_id[2];
	__u8 manufacture_date[9];
	__u8 manufacture_line[2];

	__u32 module_serial_num;
	__u8 focuser_liftoff[2];
	__u8 focuser_macro[2];
	__u8 reserved1[12];
	__u8 shutter_cal[16];
	__u8 reserved2[183];

	/* Big-endian. CRC16 over 0x00-0x41 (inclusive) */
	__u16 crc;
	__u8 reserved3[3];
	__u8 auto_load[2];
} __attribute__ ((packed));

enum imx073_mode_info {
	MODE_INFO_PREVIEW = 0,
	MODE_INFO_STILL,
	MODE_INFO_VIDEO,
	MODE_INFO_MAX
};

struct imx073_mode {
	int xres;
	int yres;
	enum imx073_mode_info mode_info;
};

enum imx073_recording_frame {
	RECORDING_CAF = 0,
	RECORDING_PREVIEW
};

#ifdef __KERNEL__
struct imx073_platform_data {
	void (*power_on)(void);
	void (*power_off)(void);
	unsigned int (*isp_int_read)(void);
};
#endif /* __KERNEL__ */

#endif  /* __imx073_H__ */

