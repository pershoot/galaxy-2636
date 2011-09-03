/*
 * Copyright (C) 2010 Motorola, Inc.
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

#ifndef __S5K5BBGX_H__
#define __S5K5BBGX_H__

#include <linux/ioctl.h>  /* For IOCTL macros */

#define FACTORY_TEST
#define S5K5BBGX_IOCTL_SET_MODE		_IOW('o', 1, struct s5k5bbgx_mode)
#define S5K5BBGX_IOCTL_TEST_PATTERN       _IOW('o', 7, enum s5k5bbgx_test_pattern)
#define S5K5BBGX_IOCTL_COLOR_EFFECT       _IOW('o', 11, enum s5k5bbgx_color_effect)
#define S5K5BBGX_IOCTL_WHITE_BALANCE      _IOW('o', 12, enum s5k5bbgx_white_balance)
#define S5K5BBGX_IOCTL_EXPOSURE	        _IOW('o', 14, enum s5k5bbgx_exposure)
#define S5K5BBGX_IOCTL_ESD_RESET	        _IOW('o', 15, enum s5k5bbgx_esd_reset)
#define S5K5BBGX_IOCTL_RECORDING_FRAME	        _IOW('o', 16, enum s5k5bbgx_recording_frame)
#define S5K5BBGX_IOCTL_EXIF_INFO	_IOR('o', 17, struct s5k5bbgx_exif_info)

#ifdef FACTORY_TEST
#define S5K5BBGX_IOCTL_DTP_TEST	        _IOW('o', 18, s5k5bbgx_dtp_test)

typedef enum {
	S5K5BBGX_DTP_TEST_OFF,
	S5K5BBGX_DTP_TEST_ON
} s5k5bbgx_dtp_test ;
#endif
#define S5K5BBGX_IOCTL_CAMMODE		_IOW('o', 19, enum s5k5bbgx_cam_mode)

struct s5k5bbgx_exif_info {
	unsigned int 	info_exptime_numer;
	unsigned int 	info_exptime_denumer;
	unsigned int	info_iso;
};

enum s5k5bbgx_test_pattern {
	S5K5BBGX_TEST_PATTERN_NONE,
	S5K5BBGX_TEST_PATTERN_COLORBARS,
	S5K5BBGX_TEST_PATTERN_CHECKERBOARD
};

enum s5k5bbgx_cam_mode {
	FRONT_CAMMODE_CAMERA = 1,
	FRONT_CAMMODE_CAMCORDER,
	FRONT_CAMMODE_MMS_CAMCORDER,
	FRONT_CAMMODE_MAX,
};

struct s5k5bbgx_otp_data {
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

struct s5k5bbgx_mode {
	int xres;
	int yres;
	__u32 frame_length;
	__u32 coarse_time;
	__u16 gain;
};

enum s5k5bbgx_esd_reset {
	FRONT_ESD_DETECTED = 0,
	FRONT_NOT_DETECTED
};

enum s5k5bbgx_color_effect {
	FRONT_EFFECT_NONE = 0,
	FRONT_EFFECT_MONO,
	FRONT_EFFECT_SEPIA,
	FRONT_EFFECT_NEGATIVE,
	FRONT_EFFECT_SOLARIZE,
	FRONT_EFFECT_POSTERIZE
};

enum s5k5bbgx_white_balance {
	FRONT_WB_AUTO = 0,
	FRONT_WB_DAYLIGHT,
	FRONT_WB_INCANDESCENT,
	FRONT_WB_FLUORESCENT,
	FRONT_WB_CLOUDY
};

enum s5k5bbgx_exposure {
	FRONT_EXPOSURE_M4 = 1,
	FRONT_EXPOSURE_M3,
	FRONT_EXPOSURE_M2,
	FRONT_EXPOSURE_M1,
	FRONT_EXPOSURE_ZERO,
	FRONT_EXPOSURE_P1,
	FRONT_EXPOSURE_P2,
	FRONT_EXPOSURE_P3,
	FRONT_EXPOSURE_P4	
};

enum s5k5bbgx_recording_frame {
	FIXED_FRAME = 0,
	VARIABLE_FRAME
};

#ifdef __KERNEL__

struct s5k5bbgx_platform_data {
	void (*power_on)(void);
	void (*power_off)(void);
};
#endif /* __KERNEL__ */

#endif  /* __S5K5BBGX_H__ */

