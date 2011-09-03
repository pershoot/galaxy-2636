/*
 *  max17042_battery.h
 *  fuel-gauge systems for lithium-ion (Li+) batteries
 *
 *  Copyright (C) 2010 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _LINUX_MAX_17042_BATTERY_H
#define _LINUX_MAX_17042_BATTERY_H

/* Register address */
#define STATUS_REG				0x00
#define VALRT_THRESHOLD_REG	0x01
#define TALRT_THRESHOLD_REG	0x02
#define SALRT_THRESHOLD_REG	0x03
#define REMCAP_REP_REG			0x05
#define SOCREP_REG				0x06
#define TEMPERATURE_REG		0x08
#define VCELL_REG				0x09
#define CURRENT_REG				0x0A
#define AVG_CURRENT_REG		0x0B
#define SOCMIX_REG				0x0D
#define SOCAV_REG				0x0E
#define REMCAP_MIX_REG			0x0F
#define FULLCAP_REG				0x10
#define RFAST_REG				0x15
#define AVR_TEMPERATURE_REG	0x16
#define CYCLES_REG				0x17
#define DESIGNCAP_REG			0x18
#define AVR_VCELL_REG			0x19
#define CONFIG_REG				0x1D
#define REMCAP_AV_REG			0x1F
#define FULLCAP_NOM_REG		0x23
#define MISCCFG_REG				0x2B
#define RCOMP_REG				0x38
#define FSTAT_REG				0x3D
#define DQACC_REG				0x45
#define DPACC_REG				0x46
#define OCV_REG					0xEE
#define VFOCV_REG				0xFB
#define VFSOC_REG				0xFF

#define FG_LEVEL 0
#define FG_TEMPERATURE 1
#define FG_VOLTAGE 2
#define FG_CURRENT 3
#define FG_CURRENT_AVG 4
#define FG_BATTERY_TYPE 5
#define FG_CHECK_STATUS 6
#define FG_VF_SOC 7

#define LOW_BATT_COMP_RANGE_NUM	5
#define LOW_BATT_COMP_LEVEL_NUM	2
#ifdef CONFIG_TARGET_LOCALE_KOR
#define MAX_LOW_BATT_CHECK_CNT	12
#else
#define MAX_LOW_BATT_CHECK_CNT	2
#endif
#define MAX17042_CURRENT_UNIT	15625 / 100000

struct max17042_platform_data {
	int sdi_capacity;
	int sdi_vfcapacity;
	int atl_capacity;
	int atl_vfcapacity;
	int fuel_alert_line;
};

struct fuelgauge_info {
	/* test print count */
	int pr_cnt;
	/* battery type */
	int battery_type;
	/* full charge comp */
	u32 previous_fullcap;
	u32 previous_vffullcap;
	u32 full_charged_cap;
	/* capacity and vfcapacity */
	u16 capacity;
	u16 vfcapacity;
	int soc_restart_flag;
	/* cap corruption check */
	u32 previous_repsoc;
	u32 previous_vfsoc;
	u32 previous_remcap;
	u32 previous_mixcap;
	u32 previous_fullcapacity;
	u32 previous_vfcapacity;
	u32 previous_vfocv;
	/* low battery comp */
	int low_batt_comp_cnt[LOW_BATT_COMP_RANGE_NUM][LOW_BATT_COMP_LEVEL_NUM];
	int check_start_vol;
	int low_batt_comp_flag;
};

struct max17042_chip {
	struct i2c_client		*client;
	struct max17042_platform_data	*pdata;
	struct fuelgauge_info	info;
	struct mutex			fg_lock;
#ifdef CONFIG_TARGET_LOCALE_KOR
	int pre_cond_ok;
	int low_comp_pre_cond;
#endif
};

/* SDI type low battery compensation offset */
#ifdef CONFIG_MACH_SAMSUNG_P3
#define SDI_Range5_1_Offset		3369
#define SDI_Range5_3_Offset		3469
#define SDI_Range4_1_Offset		3369
#define SDI_Range4_3_Offset		3469
#define SDI_Range3_1_Offset		3453
#define SDI_Range3_3_Offset		3619
#define SDI_Range2_1_Offset		3447
#define SDI_Range2_3_Offset		3606
#define SDI_Range1_1_Offset		3438
#define SDI_Range1_3_Offset		3590

#define SDI_Range5_1_Slope		0
#define SDI_Range5_3_Slope		0
#define SDI_Range4_1_Slope		0
#define SDI_Range4_3_Slope		0
#define SDI_Range3_1_Slope		60
#define SDI_Range3_3_Slope		100
#define SDI_Range2_1_Slope		50
#define SDI_Range2_3_Slope		77
#define SDI_Range1_1_Slope		0
#define SDI_Range1_3_Slope		0

#elif defined(CONFIG_MACH_SAMSUNG_P4) || defined(CONFIG_MACH_SAMSUNG_P4WIFI) ||\
	defined(CONFIG_MACH_SAMSUNG_P4LTE)
#define SDI_Range5_1_Offset		3318
#define SDI_Range5_3_Offset		3383
#define SDI_Range4_1_Offset		3451
#define SDI_Range4_3_Offset		3618
#define SDI_Range3_1_Offset		3453
#define SDI_Range3_3_Offset		3615
#define SDI_Range2_1_Offset		3447
#define SDI_Range2_3_Offset		3606
#define SDI_Range1_1_Offset		3438
#define SDI_Range1_3_Offset		3591

#define SDI_Range5_1_Slope		0
#define SDI_Range5_3_Slope		0
#define SDI_Range4_1_Slope		53
#define SDI_Range4_3_Slope		94
#define SDI_Range3_1_Slope		54
#define SDI_Range3_3_Slope		92
#define SDI_Range2_1_Slope		45
#define SDI_Range2_3_Slope		78
#define SDI_Range1_1_Slope		0
#define SDI_Range1_3_Slope		0

#elif defined(CONFIG_MACH_SAMSUNG_P5)
/* SDI type low battery compensation offset */
//Range4 : current consumption is more than 1.5A
//Range3 : current consumption is between 0.6A ~ 1.5A
//Range2 : current consumption is between 0.2A ~ 0.6A
//Range1 : current consumption is less than 0.2A
//11.06. 10 update 
#define SDI_Range5_1_Offset		3308
#define SDI_Range5_3_Offset		3365
#define SDI_Range4_1_Offset		3440	//3361
#define SDI_Range4_3_Offset		3600	//3448
#define SDI_Range3_1_Offset		3439	//3438
#define SDI_Range3_3_Offset		3628	//3634
#define SDI_Range2_1_Offset		3459	//3459
#define SDI_Range2_3_Offset		3606	//3606
#define SDI_Range1_1_Offset		3441	//3442
#define SDI_Range1_3_Offset		3591	//3591

#define SDI_Range5_1_Slope		0
#define SDI_Range5_3_Slope		0
#define SDI_Range4_1_Slope		52	//0
#define SDI_Range4_3_Slope		92	//0
#define SDI_Range3_1_Slope		51	//50
#define SDI_Range3_3_Slope		114	//124
#define SDI_Range2_1_Slope		85	//90
#define SDI_Range2_3_Slope		78	//78
#define SDI_Range1_1_Slope		0	//0
#define SDI_Range1_3_Slope		0	//0
#endif

/* ATL type low battery compensation offset */
#define ATL_Range5_1_Offset		3298
#define ATL_Range5_3_Offset		3330
#define ATL_Range4_1_Offset		3298
#define ATL_Range4_3_Offset		3330
#define ATL_Range3_1_Offset		3375
#define ATL_Range3_3_Offset		3445
#define ATL_Range2_1_Offset		3371
#define ATL_Range2_3_Offset		3466
#define ATL_Range1_1_Offset		3362
#define ATL_Range1_3_Offset		3443

#define ATL_Range5_1_Slope		0
#define ATL_Range5_3_Slope		0
#define ATL_Range4_1_Slope		0
#define ATL_Range4_3_Slope		0
#define ATL_Range3_1_Slope		50
#define ATL_Range3_3_Slope		77
#define ATL_Range2_1_Slope		40
#define ATL_Range2_3_Slope		111
#define ATL_Range1_1_Slope		0
#define ATL_Range1_3_Slope		0

enum {
	POSITIVE = 0,
	NEGATIVE,
};

enum {
	UNKNOWN_TYPE = 0,
	SDI_BATTERY_TYPE,
	ATL_BATTERY_TYPE,
};

void fg_periodic_read(void);

extern int fg_reset_soc(void);
extern int fg_reset_capacity(void);
extern int fg_adjust_capacity(void);
extern void fg_low_batt_compensation(u32 level);
extern int fg_alert_init(void);
extern void fg_fullcharged_compensation(u32 is_recharging, u32 pre_update);
extern void fg_check_vf_fullcap_range(void);
extern int fg_check_cap_corruption(void);
extern void fg_set_full_charged(void);

#endif
