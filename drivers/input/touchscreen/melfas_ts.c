/* drivers/input/touchscreen/melfas_ts.c
 *
 * Copyright (C) 2010 Melfas, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/melfas_ts.h>
#include <mach/gpio.h>
#include <mach/gpio-sec.h>

#define TS_READ_START_ADDR 0x10
#define TS_READ_REGS_LEN 5
#define TS_WRITE_REGS_LEN 16
#define P5_MAX_TOUCH	10
#define P5_MAX_I2C_FAIL	50
#define P5_THRESHOLD 0x70

#ifndef I2C_M_WR
#define I2C_M_WR 0
#endif

#define DEBUG_MODE
#define SET_DOWNLOAD_BY_GPIO
#define COOD_ROTATE_270
#define TSP_FACTORY_TEST
#define ENABLE_NOISE_TEST_MODE

#define REPORT_MT(touch_number, x, y, amplitude) \
do {     \
	input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, touch_number);\
	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x);             \
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y);             \
	input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, amplitude);         \
	input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, amplitude); \
	input_mt_sync(ts->input_dev);                                      \
} while (0)

#ifdef SET_DOWNLOAD_BY_GPIO
#include "mcs8000_download.h"
#endif

struct melfas_ts_data {
	uint16_t addr;
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct melfas_tsi_platform_data *pdata;
	struct early_suspend early_suspend;
	struct tsp_callbacks callbacks;
	uint32_t flags;
	bool charging_status;
	bool tsp_status;
#ifdef SET_DOWNLOAD_BY_GPIO
	int gpio_scl;
	int gpio_sda;
#endif
	int touch_id;
	int (*power)(int on);
	void (*power_enable)(int en);
};

#ifdef CONFIG_SAMSUNG_INPUT
extern struct class *sec_class;
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void melfas_ts_early_suspend(struct early_suspend *h);
static void melfas_ts_late_resume(struct early_suspend *h);
#endif

static struct muti_touch_info g_Mtouch_info[P5_MAX_TOUCH];
static bool debug_print = false;

#ifdef DEBUG_MODE
static bool debug_on = false;
static tCommandInfo_t tCommandInfo[] = {
	{ '?', "Help" },
	{ 'T', "Go to LOGGING mode" },
	{ 'M', "Go to MTSI_1_2_0 mode" },
	{ 'R', "Toggle LOG ([R]awdata)" },
	{ 'F', "Toggle LOG (Re[f]erence)" },
	{ 'I', "Toggle LOG ([I]ntensity)" },
	{ 'G', "Toggle LOG ([G]roup Image)" },
	{ 'D', "Toggle LOG ([D]elay Image)" },
	{ 'P', "Toggle LOG ([P]osition)" },
	{ 'B', "Toggle LOG (De[b]ug)" },
	{ 'V', "Toggle LOG (Debug2)" },
	{ 'L', "Toggle LOG (Profi[l]ing)" },
	{ 'O', "[O]ptimize Delay" },
	{ 'N', "[N]ormalize Intensity" }
};

static bool vbLogType[LT_LIMIT] = {0, };
static const char mcLogTypeName[LT_LIMIT][20] = {
	"LT_DIAGNOSIS_IMG",
	"LT_RAW_IMG",
	"LT_REF_IMG",
	"LT_INTENSITY_IMG",
	"LT_GROUP_IMG",
	"LT_DELAY_IMG",
	"LT_POS",
	"LT_DEBUG",
	"LT_DEBUG2",
	"LT_PROFILING",
};

static void toggle_log(struct melfas_ts_data *ts, eLogType_t _eLogType);
static void print_command_list(void);
static int melfas_i2c_read(struct i2c_client *client, u16 addr, u16 length, u8 *value);

static void debug_i2c_read(struct i2c_client *client, u16 addr, u8 *value, u16 length)
{
	melfas_i2c_read(client, addr, length, value);
}

static int debug_i2c_write(struct i2c_client *client, u8 *value, u16 length)
{
	return i2c_master_send(client, value, length);
}

static void key_handler(struct melfas_ts_data *ts, char key_val)
{
	u8 write_buf[2];
//	u8 read_buf[2];
//	int try_cnt = 0;
	pr_info("[TSP] %s - %c\n", __func__, key_val);
	switch (key_val) {
	case '?':
	case '/':
		print_command_list();
		break;
	case 'T':
	case 't':
		write_buf[0] = ADDR_ENTER_LOGGING;
		write_buf[1] = 1;
		debug_i2c_write(ts->client, write_buf, 2);
		debug_on = true;
#if 0
		for ( try_cnt = 1; try_cnt - 1 < 10; try_cnt++) {
			msleep(100);
			/* verify the register was written */
			i2c_master_recv(ts->client, read_buf, 1);
			if (read_buf[0] == 72) {
				pr_info("[TSP] success - %c \n", key_val);
				break;
			} else {
				pr_info("[TSP] try again : val : %d , cnt : %d\n", read_buf[0], try_cnt);
				debug_i2c_write(ts->client, write_buf, 2);
			}
		}
#endif
		break;
	case 'M':
	case 'm':
		write_buf[0] = ADDR_CHANGE_PROTOCOL;
		write_buf[1] = 7;//PTC_STSI_1_0_0;
		debug_i2c_write(ts->client, write_buf, 2);
		debug_on = false;
		msleep(200);
		ts->power_enable(0);
		msleep(200);
		ts->power_enable(1);
		break;
	case 'R':
	case 'r':
		toggle_log(ts, LT_RAW_IMG);
		break;
	case 'F':
	case 'f':
		toggle_log(ts, LT_REF_IMG);
		break;
	case 'I':
	case 'i':
		toggle_log(ts, LT_INTENSITY_IMG);
		break;
	case 'G':
	case 'g':
		toggle_log(ts, LT_GROUP_IMG);
		break;
	case 'D':
	case 'd':
		toggle_log(ts, LT_DELAY_IMG);
		break;
	case 'P':
	case 'p':
		toggle_log(ts, LT_POS);
		break;
	case 'B':
	case 'b':
		toggle_log(ts, LT_DEBUG);
		break;
	case 'V':
	case 'v':
		toggle_log(ts, LT_DEBUG2);
		break;
	case 'L':
	case 'l':
		toggle_log(ts, LT_PROFILING);
		break;
	case 'O':
	case 'o':
		pr_info("Enter 'Optimize Delay' mode!!!\n");
		write_buf[0] = ADDR_CHANGE_OPMODE;
		write_buf[1] = OM_OPTIMIZE_DELAY;
		if (!debug_i2c_write(ts->client, write_buf, 2))
			goto ERROR_HANDLE;
		break;
	case 'N':
	case 'n':
		pr_info("Enter 'Normalize Intensity' mode!!!\n");
		write_buf[0] = ADDR_CHANGE_OPMODE;
		write_buf[1] = OM_NORMALIZE_INTENSITY;
		if (!debug_i2c_write(ts->client, write_buf, 2))
			goto ERROR_HANDLE;
		break;
	default:
		;
	}
	return;
ERROR_HANDLE:
	pr_info("ERROR!!! \n");
}

static void print_command_list(void)
{
	int i;
	pr_info("######################################################\n");
	for (i = 0; i < sizeof(tCommandInfo) / sizeof(tCommandInfo_t); i++) {
		pr_info("[%c]: %s\n", tCommandInfo[i].cCommand, tCommandInfo[i].sDescription);
	}
	pr_info("######################################################\n");
}

static void toggle_log(struct melfas_ts_data *ts, eLogType_t _eLogType)
{
	u8 write_buf[2];
	vbLogType[_eLogType] ^= 1;
	if (vbLogType[_eLogType]) {
		write_buf[0] = ADDR_LOGTYPE_ON;
		pr_info("%s ON\n", mcLogTypeName[_eLogType]);
	} else {
		write_buf[0] = ADDR_LOGTYPE_OFF;
		pr_info("%s OFF\n", mcLogTypeName[_eLogType]);
	}
	write_buf[1] = _eLogType;
	debug_i2c_write(ts->client, write_buf, 2);
}

static void logging_function(struct melfas_ts_data *ts)
{
	u8 read_buf[100];
	u8 read_mode, read_num;
	int FingerX, FingerY, FingerID;
	int i;
	static int past_read_mode = HEADER_NONE;
	static char *ps;
	static char s[500];

	debug_i2c_read(ts->client, LOG_READ_ADDR, read_buf, 2);

	read_mode = read_buf[0];
	read_num = read_buf[1];

	//pr_info("[TSP] read_mode : %d,  read_num : %d\n", read_mode, read_num);

	switch (read_mode) {
	case HEADER_U08://Unsigned character
	{
		unsigned char* p = (unsigned char*) &read_buf[2];
		i2c_master_recv(ts->client, read_buf, read_num + 2);
		ps = s;
		s[0] = '\0';

		for (i = 0; i < read_num - 1; i++)
		{
			sprintf(ps, "%4d,", p[i]);
			ps = s + strlen(s);
		}
		sprintf(ps, "%4d\n", p[i]);
		ps = s + strlen(s);
		printk(KERN_DEBUG "%s", s);
		break;
	}
	case HEADER_S08://Unsigned character
	{
		signed char* p = (signed char*) &read_buf[2];
		i2c_master_recv(ts->client, read_buf, read_num + 2);
		ps = s;
		s[0] = '\0';

		for (i = 0; i < read_num - 1; i++)
		{
			sprintf(ps, "%4d,", p[i]);
			ps = s + strlen(s);
		}
		sprintf(ps, "%4d\n", p[i]);
		ps = s + strlen(s);
		printk(KERN_DEBUG "%s", s);
		break;
	}
	case HEADER_U16://Unsigned short
	{
		unsigned short* p = (unsigned short*) &read_buf[2];
		i2c_master_recv(ts->client, read_buf, read_num * 2 + 2);
		if (past_read_mode != HEADER_U16_NOCR)
		{
			ps = s;
			s[0] = '\0';
		}

		for (i = 0; i < read_num - 1; i++)
		{
			sprintf(ps, "%5d,", p[i]);
			ps = s + strlen(s);
		}
		sprintf(ps, "%5d\n", p[i]);
		ps = s + strlen(s);
		printk(KERN_DEBUG "%s", s);
		break;
	}
	case HEADER_U16_NOCR:
	{
		unsigned short* p = (unsigned short*) &read_buf[2];
		i2c_master_recv(ts->client, read_buf, read_num * 2 + 2);

		if (past_read_mode != HEADER_U16_NOCR)
		{
			ps = s;
			s[0] = '\0';
		}
		for (i = 0; i < read_num; i++)
		{
			sprintf(ps, "%5d,", p[i]);
			ps = s + strlen(s);
		}
		break;
	}
	case HEADER_S16://Unsigned short
	{
		signed short* p = (signed short*) &read_buf[2];
		i2c_master_recv(ts->client, read_buf, read_num * 2 + 2);

		if (past_read_mode != HEADER_S16_NOCR)
		{
			ps = s;
			s[0] = '\0';
		}

		for (i = 0; i < read_num - 1; i++)
		{
			sprintf(ps, "%5d,", p[i]);
			ps = s + strlen(s);
		}
		sprintf(ps, "%5d\n", p[i]);
		ps = s + strlen(s);
		printk(KERN_DEBUG "%s", s);
		break;
	}
	case HEADER_S16_NOCR:
	{
		signed short* p = (signed short*) &read_buf[2];
		i2c_master_recv(ts->client, read_buf, read_num * 2 + 2);

		if (past_read_mode != HEADER_S16_NOCR)
		{
			ps = s;
			s[0] = '\0';
		}
		for (i = 0; i < read_num; i++)
		{
			sprintf(ps, "%5d,", p[i]);
			ps = s + strlen(s);
		}
		break;
	}
	case HEADER_U32://Unsigned short
	{
		unsigned long* p = (unsigned long*) &read_buf[2];
		i2c_master_recv(ts->client, read_buf, read_num * 4 + 4);

		if (past_read_mode != HEADER_U32_NOCR)
		{
			ps = s;
			s[0] = '\0';
		}

		for (i = 0; i < read_num - 1; i++)
		{
			sprintf(ps, "%10ld,", p[i]);
			ps = s + strlen(s);
		}
		sprintf(ps, "%10ld\n", p[i]);
		ps = s + strlen(s);
		printk(KERN_DEBUG "%s", s);
		break;
	}
	case HEADER_U32_NOCR://Unsigned short
	{
		unsigned long* p = (unsigned long*) &read_buf[2];
		i2c_master_recv(ts->client, read_buf, read_num * 4 + 4);

		if (past_read_mode != HEADER_U32_NOCR)
		{
			ps = s;
			s[0] = '\0';
		}
		for (i = 0; i < read_num; i++)
		{
			sprintf(ps, "%10ld,", p[i]);
			ps = s + strlen(s);
		}
		break;
	}
	case HEADER_S32://Unsigned short
	{
		signed long* p = (signed long*) &read_buf[2];
		i2c_master_recv(ts->client, read_buf, read_num * 4 + 4);

		if (past_read_mode != HEADER_S32_NOCR)
		{
			ps = s;
			s[0] = '\0';
		}

		for (i = 0; i < read_num - 1; i++)
		{
			sprintf(ps, "%10ld,", p[i]);
			ps = s + strlen(s);
		}
		sprintf(ps, "%10ld\n", p[i]);
		ps = s + strlen(s);
		printk(KERN_DEBUG "%s", s);
		break;
	}
	case HEADER_S32_NOCR://Unsigned short
	{
		signed long* p = (signed long*) &read_buf[2];
		i2c_master_recv(ts->client, read_buf, read_num * 4 + 4);

		if (past_read_mode != HEADER_S32_NOCR)
		{
			ps = s;
			s[0] = '\0';
		}
		for (i = 0; i < read_num; i++)
		{
			sprintf(ps, "%10ld,", p[i]);
			ps = s + strlen(s);
		}
		break;
	}
	case HEADER_TEXT://Text
	{
		i2c_master_recv(ts->client, read_buf, read_num + 2);

		ps = s;
		s[0] = '\0';

		for (i = 2; i < read_num + 2; i++)
		{
			sprintf(ps, "%c", read_buf[i]);
			ps = s + strlen(s);
		}
		printk(KERN_DEBUG "%s\n", s);
		break;
	}
	case HEADER_FINGER:
	{
		i2c_master_recv(ts->client, read_buf, read_num * 4 + 2);

		ps = s;
		s[0] = '\0';
		for (i = 2; i < read_num * 4 + 2; i = i + 4)
		{
			//log_printf( device_idx, " %5ld", read_buf[i]  , 0,0);
			FingerX = (read_buf[i + 1] & 0x07) << 8 | read_buf[i];
			FingerY = (read_buf[i + 3] & 0x07) << 8 | read_buf[i + 2];

			FingerID = (read_buf[i + 1] & 0xF8) >> 3;
			sprintf(ps, "%2d (%4d,%4d) | ", FingerID, FingerX, FingerY);
			ps = s + strlen(s);
		}
		printk(KERN_DEBUG "%s\n", s);
		break;
	}
	case HEADER_S12://Unsigned short
	{
		signed short* p = (signed short*) &read_buf[2];
		i2c_master_recv(ts->client, read_buf, read_num * 2 + 2);

		if (past_read_mode != HEADER_S12_NOCR)
		{
			ps = s;
			s[0] = '\0';
		}
		for (i = 0; i < read_num; i++)
		{
			if (p[i] > 4096 / 2)
			p[i] -= 4096;
		}

		for (i = 0; i < read_num - 1; i++)
		{
			sprintf(ps, "%5d,", p[i]);
			ps = s + strlen(s);
		}
		sprintf(ps, "%5d\n", p[i]);
		ps = s + strlen(s);
		printk(KERN_DEBUG "%s", s);
		break;
	}
	case HEADER_S12_NOCR:
	{
		signed short* p = (signed short*) &read_buf[2];
		i2c_master_recv(ts->client, read_buf, read_num * 2 + 2);

		if (past_read_mode != HEADER_S12_NOCR)
		{
			ps = s;
			s[0] = '\0';
		}
		for (i = 0; i < read_num; i++)
		{
			if (p[i] > 4096 / 2)
			p[i] -= 4096;
		}
		for (i = 0; i < read_num; i++)
		{
			sprintf(ps, "%5d,", p[i]);
			ps = s + strlen(s);
		}
		break;
	}
	case HEADER_PRIVATE://Unsigned character
	{
		unsigned char* p = (unsigned char*) &read_buf[2];
		i2c_master_recv(ts->client, read_buf, read_num + 2 + read_num % 2);

		ps = s;
		s[0] = '\0';
		sprintf(ps, "################## CUSTOM_PRIVATE LOG: ");
		ps = s + strlen(s);
		for (i = 0; i < read_num - 1; i++)
		{
			sprintf(ps, "%5d,", p[i]);
			ps = s + strlen(s);
		}
		sprintf(ps, "%5d\n", p[i]);
		ps = s + strlen(s);
		printk(KERN_DEBUG "%s", s);
		break;
	}
	default:
		break;
	}

	past_read_mode = read_mode;
}
#endif /* DEBUG_MODE */

static int melfas_i2c_read(struct i2c_client *client, u16 addr, u16 length, u8 *value)
{
	struct i2c_adapter *adapter = client->adapter;
	struct i2c_msg msg[2];

	msg[0].addr  = client->addr;
	msg[0].flags = 0x00;
	msg[0].len   = 2;
	msg[0].buf   = (u8 *) &addr;

	msg[1].addr  = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len   = length;
	msg[1].buf   = (u8 *) value;

	if  (i2c_transfer(adapter, msg, 2) == 2)
		return 0;
	else
		return -EIO;

}

static int melfas_i2c_write(struct i2c_client *client, char *buf, int length)
{
	int i;
	char data[TS_WRITE_REGS_LEN];

	if (length > TS_WRITE_REGS_LEN) {
		pr_err("[TSP] size error - %s\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < length; i++)
		data[i] = *buf++;

	i = i2c_master_send(client, (char *)data, length);

	if (i == length)
		return length;
	else{
		pr_err("[TSP] melfas_i2c_write error : [%d]",i);
		return -EIO;
	}
}

static void release_all_fingers(struct melfas_ts_data *ts)
{
	int i;
	for(i=0; i<P5_MAX_TOUCH; i++) {
		if(-1 == g_Mtouch_info[i].strength) {
			g_Mtouch_info[i].posX = 0;
			g_Mtouch_info[i].posY = 0;
			continue;
		}

		g_Mtouch_info[i].strength = 0;

		REPORT_MT(i, g_Mtouch_info[i].posX,
				g_Mtouch_info[i].posY, g_Mtouch_info[i].strength);

		g_Mtouch_info[i].posX = 0;
		g_Mtouch_info[i].posY = 0;

		if(0 == g_Mtouch_info[i].strength)
			g_Mtouch_info[i].strength = -1;
	}
	input_sync(ts->input_dev);
}

static void reset_tsp(struct melfas_ts_data *ts)
{
	int		ret = 0;
	char	buf[2];
	
	buf[0] = 0x60;
	buf[1] = ts->charging_status;
	
	release_all_fingers(ts);
	
	ts->power_enable(0);
	msleep(700);
	ts->power_enable(1);
	msleep(500);

	pr_info("[TSP] TSP reset & TA/USB %sconnect\n",ts->charging_status?"":"dis");
	melfas_i2c_write(ts->client, (char *)buf, 2);
}

static int read_input_info(struct melfas_ts_data *ts, u8 *val)
{
	return melfas_i2c_read(ts->client, TS_READ_START_ADDR, TS_READ_REGS_LEN, val);
}

static int check_firmware_master(struct melfas_ts_data *ts, u8 *val)
{
	return melfas_i2c_read(ts->client, MCSTS_FIRMWARE_VER_REG_MASTER, 1, val);
}

static int check_firmware_slave(struct melfas_ts_data *ts, u8 *val)
{
	return melfas_i2c_read(ts->client, MCSTS_FIRMWARE_VER_REG_SLAVE, 1, val);
}
static int check_firmware_core(struct melfas_ts_data *ts, u8 *val)
{
	return melfas_i2c_read(ts->client, MCSTS_FIRMWARE_VER_REG_CORE, 1, val);
}

static int check_firmware_private(struct melfas_ts_data *ts, u8 *val)
{
	return melfas_i2c_read(ts->client, MCSTS_FIRMWARE_VER_REG_PRIVATE_CUSTOM, 1, val);
}

static int check_firmware_public(struct melfas_ts_data *ts, u8 *val)
{
	return melfas_i2c_read(ts->client, MCSTS_FIRMWARE_VER_REG_PUBLIC_CUSTOM, 1, val);
}

static int check_slave_boot(struct melfas_ts_data *ts, u8 *val)
{
	return melfas_i2c_read(ts->client, 0xb1, 1, val);
}

static int firmware_update(struct melfas_ts_data *ts)
{
	int i=0;
	u8 	val = 0;
	u8 	val_slv = 0;
	u8 	fw_ver = 0;
	int ret = 0;
	int ret_slv = 0;
	bool empty_chip = true;
	INT8 dl_enable_bit = 0x00;
	u8 version_info = 0;

#if !MELFAS_ISP_DOWNLOAD
	ret = check_firmware_master(ts, &val);
	ret_slv = check_firmware_slave(ts, &val_slv);

	for(i=0 ; i <5 ; i++)
	{
		if (ret || ret_slv) {
			pr_err("[TSP] ISC mode : Failed to check firmware version(ISP) : %d , %d : %0x0 , %0x0 \n",ret, ret_slv,val,val_slv);
			ret = check_firmware_master(ts, &val);
			ret_slv = check_firmware_slave(ts, &val_slv);			
		}else{
			empty_chip = false;
			break;
		}
	}

	if(MELFAS_CORE_FIRWMARE_UPDATE_ENABLE) 
	{
		check_firmware_core(ts,&version_info);
//		printk("<TSP> CORE_VERSION : 0x%2X\n",version_info);
		if(version_info < CORE_FW_VER || version_info==0xFF)
			dl_enable_bit |= 0x01;
	}
	if(MELFAS_PRIVATE_CONFIGURATION_UPDATE_ENABLE)
	{
		check_firmware_private(ts,&version_info);
//		printk("<TSP> PRIVATE_CUSTOM_VERSION : 0x%2X\n",version_info);
		if(version_info < PRIVATE_FW_VER || version_info==0xFF)
			dl_enable_bit |= 0x02;
	}
	if(MELFAS_PUBLIC_CONFIGURATION_UPDATE_ENABLE)
	{
		check_firmware_public(ts,&version_info);
//		printk("<TSP> PUBLIC_CUSTOM_VERSION : 0x%2X\n",version_info);
		if(version_info < PUBLIC_FW_VER || version_info==0xFF
			|| (ts->touch_id == 0 && (val < MELFAS_FW_VER || val > 0x50 ))
			|| (ts->touch_id == 1 && val < ILJIN_FW_VER ))
			dl_enable_bit |= 0x04;
	}
	pr_info("dl_enable_bit  = [0x%X],%s pennel",dl_enable_bit, ts->touch_id ? "ILJIN" : "MELFAS" );
//	dl_enable_bit = 0x07;
	
#endif

#ifdef SET_DOWNLOAD_BY_GPIO
	disable_irq(ts->client->irq);
	/* enable gpio */
#ifdef CONFIG_ARCH_TEGRA
	tegra_gpio_enable(ts->gpio_sda);
	tegra_gpio_enable(ts->gpio_scl);
#endif
	gpio_request(ts->gpio_sda, "TSP_SDA");
	gpio_request(ts->gpio_scl, "TSP_SCL");


#if MELFAS_ISP_DOWNLOAD
	ret = mcsdl_download_binary_data(ts->touch_id);
	if (ret)
		pr_err("[TSP] SET Download ISP Fail - error code [%d]\n", ret);
#else

	/* (current version  < binary verion) */
	if (empty_chip || val != val_slv
			|| val < MELFAS_BASE_FW_VER
			|| (val > 0x50  && val < ILJIN_BASE_FW_VER) )
	{
		pr_info("[TSP] ISC mode enter but ISP mode download start : 0x%X, 0x%X\n",val,val_slv);
		mcsdl_download_binary_data(ts->touch_id); 	//ISP mode download
	}
	else
	{
		ret = mms100_ISC_download_binary_data(ts->touch_id, dl_enable_bit);
		if (ret)
		{
			pr_err("[TSP] SET Download ISC Fail - error code [%d]\n", ret);
			ret = mcsdl_download_binary_data(ts->touch_id); 	//ISP mode download 
			if (ret)
				pr_err("[TSP] SET Download ISC & ISP ALL Fail - error code [%d]\n", ret);
		}
	}
#endif

	gpio_free(ts->gpio_sda);
	gpio_free(ts->gpio_scl);

	/* disable gpio */
#ifdef CONFIG_ARCH_TEGRA
	tegra_gpio_disable(ts->gpio_sda);
	tegra_gpio_disable(ts->gpio_scl);
#endif
#endif /* SET_DOWNLOAD_BY_GPIO */
	msleep(100);

	/* reset chip */
	ts->power_enable(0);
	msleep(200);

	ts->power_enable(1);
	msleep(100);

	enable_irq(ts->client->irq);

	return 0;

}

static void inform_charger_connection(struct tsp_callbacks *cb, int mode)
{
	struct melfas_ts_data *ts = container_of(cb,
			struct melfas_ts_data, callbacks);
	char buf[2];

	buf[0] = 0x60;
	buf[1] = !!mode;
	ts->charging_status = !!mode;

	if(ts->tsp_status){
		pr_info("[TSP] TA/USB is %sconnected\n", !!mode ? "" : "dis");
		melfas_i2c_write(ts->client, (char *)buf, 2);
	}
}

static void melfas_ts_read_input(struct melfas_ts_data *ts)
{
	int ret = 0, i;
	u8 buf[TS_READ_REGS_LEN];
	int touchType=0, touchState =0, touchID=0, posX=0, posY=0, strength=0, keyID = 0, reportID = 0;

#ifdef DEBUG_MODE
	if (debug_on) {
		logging_function(ts);
		return ;
	}
#endif

	ret = read_input_info(ts, buf);
	if (ret < 0) {
		pr_err("[TSP] Failed to read the touch info\n");

		for(i=0; i<P5_MAX_I2C_FAIL; i++ )
			read_input_info(ts, buf);

		if(i == P5_MAX_I2C_FAIL){	// ESD Detection - I2c Fail
			pr_err("[TSP] Melfas_ESD I2C FAIL \n");
			reset_tsp(ts);
		}

		return ;
	}
	else{
		touchType  = (buf[0]>>6)&0x03;
		touchState = (buf[0]>>4)&0x01;
		reportID = (buf[0]&0x0f);
#if defined(COOD_ROTATE_90)
		posY = ((buf[1]& 0x0F) << 8) | buf[2];
		posX  = ((buf[1]& 0xF0) << 4) | buf[3];
		posX = ts->pdata->max_y - posX;
#elif defined(COOD_ROTATE_270)
		posY = ((buf[1]& 0x0F) << 8) | buf[2];
		posX  = ((buf[1]& 0xF0) << 4) | buf[3];
		posY = ts->pdata->max_x - posY;
#else
		posX = ((buf[1]& 0x0F) << 8) | buf[2];
		posY = ((buf[1]& 0xF0) << 4) | buf[3];
#endif
		keyID = strength = buf[4];

		if(reportID == 0x0F) { // ESD Detection
			pr_info("[TSP] MELFAS_ESD Detection");
			reset_tsp(ts);
			return ;
		}
		else if(reportID == 0x0E)// abnormal Touch Detection
		{
			pr_info("[TSP] MELFAS abnormal Touch Detection");
			reset_tsp(ts);
			return ;
		}
		else if(reportID == 0x0D)// 	clear reference
		{
			pr_info("[TSP] MELFAS Clear Reference Detection");
			return ;
		}
		else if(reportID == 0x0C)//Impulse Noise TA
		{
			pr_info("[TSP] MELFAS Impulse Noise TA Detection");
			return ;
		}

		touchID = reportID-1;

		if (debug_print)
			pr_info("[TSP] reportID: %d\n", reportID);

		if(reportID > P5_MAX_TOUCH || reportID < 1) {
			pr_err("[TSP] Invalid touch id.\n");
			release_all_fingers(ts);
			return ;
		}

		if(touchType == TOUCH_SCREEN) {
			g_Mtouch_info[touchID].posX= posX;
			g_Mtouch_info[touchID].posY= posY;

			if(touchState) {
#if 0//def CONFIG_KERNEL_DEBUG_SEC
				if (0 >= g_Mtouch_info[touchID].strength)
					pr_info("[TSP] Press    - ID : %d  [%d,%d] WIDTH : %d",
						touchID,
						g_Mtouch_info[touchID].posX,
						g_Mtouch_info[touchID].posY,
						strength);
#else
				if (0 >= g_Mtouch_info[touchID].strength)
					pr_info("[TSP] P : %d\n", touchID);
#endif
				g_Mtouch_info[touchID].strength= (strength+1)/2;
			} else {
#if 0//def CONFIG_KERNEL_DEBUG_SEC
				if (g_Mtouch_info[touchID].strength)
					pr_info("[TSP] Release - ID : %d [%d,%d]",
						touchID,
						g_Mtouch_info[touchID].posX,
						g_Mtouch_info[touchID].posY);
#else
				if (g_Mtouch_info[touchID].strength)
					pr_info("[TSP] R : %d\n", touchID);

#endif
				g_Mtouch_info[touchID].strength = 0;
			}

			for(i=0; i<P5_MAX_TOUCH; i++) {
				if(g_Mtouch_info[i].strength== -1)
					continue;

				REPORT_MT(i, g_Mtouch_info[i].posX,
						g_Mtouch_info[i].posY, g_Mtouch_info[i].strength);

				if (debug_print)
					pr_info("[TSP] Touch ID: %d, State : %d, x: %d, y: %d, z: %d\n",
						i, touchState, g_Mtouch_info[i].posX,
						g_Mtouch_info[i].posY, g_Mtouch_info[i].strength);

				if(g_Mtouch_info[i].strength == 0)
					g_Mtouch_info[i].strength = -1;
			}
		}
		input_sync(ts->input_dev);
	}
}

static irqreturn_t melfas_ts_irq_handler(int irq, void *handle)
{
	struct melfas_ts_data *ts = (struct melfas_ts_data *)handle;
	if (debug_print)
		pr_info("[TSP] %s\n", __func__);

	melfas_ts_read_input(ts);
	return IRQ_HANDLED;
}

static ssize_t show_firmware_dev(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct melfas_ts_data *ts = dev_get_drvdata(dev);
	u8 ver = 0;

	check_firmware_master(ts, &ver);

	if (!ts->touch_id)
		return snprintf(buf, PAGE_SIZE,	"MEL_%Xx%d\n", ver, 0x0);
	else
		return snprintf(buf, PAGE_SIZE,	"ILJ_%Xx%d\n", ver, 0x0);
}

static ssize_t store_firmware(struct device *dev,
						struct device_attribute *attr,
						const char *buf,
						size_t count)
{
	struct melfas_ts_data *ts = dev_get_drvdata(dev);
	int i ;

	if (sscanf(buf, "%d", &i) != 1)
		return -EINVAL;

#ifdef SET_DOWNLOAD_BY_GPIO
	disable_irq(ts->client->irq);
	/* enable gpio */
#ifdef CONFIG_ARCH_TEGRA
	tegra_gpio_enable(ts->gpio_sda);
	tegra_gpio_enable(ts->gpio_scl);
#endif
	gpio_request(ts->gpio_sda, "TSP_SDA");
	gpio_request(ts->gpio_scl, "TSP_SCL");
	

	mcsdl_download_binary_file();

	gpio_free(ts->gpio_sda);
	gpio_free(ts->gpio_scl);

	/* disable gpio */
#ifdef CONFIG_ARCH_TEGRA
	tegra_gpio_disable(ts->gpio_sda);
	tegra_gpio_disable(ts->gpio_scl);
#endif
#endif /* SET_DOWNLOAD_BY_GPIO */

	msleep(100);
	/* reset chip */
	ts->power_enable(0);
	msleep(200);
	ts->power_enable(1);
	msleep(100);

	enable_irq(ts->client->irq);

	return count;
}

static ssize_t show_firmware_bin(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct melfas_ts_data *ts = dev_get_drvdata(dev);

	if (!ts->touch_id)
		return snprintf(buf, PAGE_SIZE,	"MEL_%Xx%d\n",
					MELFAS_FW_VER, 0x0);
	else
		return snprintf(buf, PAGE_SIZE,	"ILJ_%Xx%d\n",
					ILJIN_FW_VER, 0x0);
}
static ssize_t show_firmware_bin_slv(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct melfas_ts_data *ts = dev_get_drvdata(dev);
	u8 val_slv = 0;
	int ret_slv = 0;
	ret_slv = check_firmware_slave(ts, &val_slv);
	if (ret_slv) {
		pr_err("[TSP] Failed to check firmware slave version : %d \n",ret_slv);
	}
	if (!ts->touch_id)
		return snprintf(buf, PAGE_SIZE,	"MEL_%Xx%d\n",
					val_slv, 0x0);
	else
		return snprintf(buf, PAGE_SIZE,	"ILJ_%Xx%d\n",
					val_slv, 0x0);
}
static ssize_t show_firmware_bin_detail(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct melfas_ts_data *ts = dev_get_drvdata(dev);

	u8 	val_core 	= 0;
	u8 	val_private = 0;
	u8 	val_public 	= 0;
	int ret_core 	= 0;
	int ret_private = 0;
	int ret_public 	= 0;

	ret_core 	= check_firmware_core(ts, &val_core);
	ret_private = check_firmware_private(ts, &val_private);
	ret_public 	= check_firmware_public(ts, &val_public);


	if (!ts->touch_id)
		return snprintf(buf, PAGE_SIZE,	"MEL_%Xx%Xx%X\n",
					val_core, val_private, val_public);
	else
		return snprintf(buf, PAGE_SIZE,	"ILJ_%Xx%Xx%X\n",
					val_core, val_private, val_public);
}

static ssize_t store_debug_mode(struct device *dev,
						struct device_attribute *attr,
						const char *buf,
						size_t count)
{
	struct melfas_ts_data *ts = dev_get_drvdata(dev);
	char ch;

	if (sscanf(buf, "%c", &ch) != 1)
		return -EINVAL;

	key_handler(ts, ch);

	return count;
}

static ssize_t show_debug_mode(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	return sprintf(buf, debug_on ? "ON\n" : "OFF\n");
}

static ssize_t store_debug_log(struct device *dev,
						struct device_attribute *attr,
						const char *buf,
						size_t count)
{
	int i;

	if (sscanf(buf, "%d", &i) != 1)
		return -EINVAL;

	if (i)
		debug_print = true;
	else
		debug_print = false;

	return count;
}

static ssize_t show_threshold(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	struct melfas_ts_data *ts = dev_get_drvdata(dev);
	u8 threshold;
	melfas_i2c_read(ts->client, P5_THRESHOLD, 1, &threshold);

	return sprintf(buf, "%d\n", threshold);
}

static DEVICE_ATTR(fw_dev, S_IWUSR|S_IRUGO, show_firmware_dev, store_firmware);
static DEVICE_ATTR(fw_bin, S_IRUGO, show_firmware_bin, NULL);
static DEVICE_ATTR(fw_bin_slv, S_IRUGO, show_firmware_bin_slv, NULL);
static DEVICE_ATTR(fw_bin_detail, S_IRUGO, show_firmware_bin_detail, NULL);
static DEVICE_ATTR(debug_mode, S_IWUSR|S_IRUGO, show_debug_mode, store_debug_mode);
static DEVICE_ATTR(debug_log, S_IWUSR|S_IRUGO, NULL, store_debug_log);
#ifdef ENABLE_NOISE_TEST_MODE
static DEVICE_ATTR(set_threshould, S_IRUGO, show_threshold, NULL);
#else
static DEVICE_ATTR(threshold, S_IRUGO, show_threshold, NULL);
#endif

#if defined(TSP_FACTORY_TEST)
static u16 inspection_data[1134] = { 0, };
#ifdef DEBUG_LOW_DATA
static u16 low_data[1134] = { 0, };
#endif
static u16 lntensity_data[1134] = { 0, };
static void check_debug_data(struct melfas_ts_data *ts)
{
	u8 write_buffer[6];
	u8 read_buffer[2];
	int sensing_line, exciting_line;
	int gpio = irq_to_gpio(ts->client->irq);

	disable_irq(ts->client->irq);

	/* enter the debug mode */
	write_buffer[0] = 0xA0;
	write_buffer[1] = 0x1A;
	write_buffer[2] = 0x0;
	write_buffer[3] = 0x0;
	write_buffer[4] = 0x0;
	write_buffer[5] = 0x01;
	melfas_i2c_write(ts->client, (char *)write_buffer, 6);

	/* wating for the interrupt*/
	while (gpio_get_value(gpio)) {
		printk(".");
		udelay(100);
	}

	if (debug_print)
		pr_info("[TSP] read dummy\n");

	/* read the dummy data */
	melfas_i2c_read(ts->client, 0xA8, 2, read_buffer);

	if (debug_print)
		pr_info("[TSP] read inspenction data\n");
	write_buffer[5] = 0x02;
	for (sensing_line = 0; sensing_line < 27; sensing_line++) {
		for (exciting_line =0; exciting_line < 42; exciting_line++) {
			write_buffer[2] = exciting_line;
			write_buffer[3] = sensing_line;
			melfas_i2c_write(ts->client, (char *)write_buffer, 6);
			melfas_i2c_read(ts->client, 0xA8, 2, read_buffer);
			inspection_data[exciting_line + sensing_line * 42] =
				(read_buffer[1] & 0xf) << 8 | read_buffer[0];
		}
	}
#ifdef DEBUG_LOW_DATA
	if (debug_print)
		pr_info("[TSP] read low data\n");
	write_buffer[5] = 0x03;
	for (sensing_line = 0; sensing_line < 27; sensing_line++) {
		for (exciting_line =0; exciting_line < 42; exciting_line++) {
			write_buffer[2] = exciting_line;
			write_buffer[3] = sensing_line;
			melfas_i2c_write(ts->client, (char *)write_buffer, 6);
			melfas_i2c_read(ts->client, 0xA8, 2, read_buffer);
			low_data[exciting_line + sensing_line * 42] =
				(read_buffer[1] & 0xf) << 8 | read_buffer[0];
		}
	}
#endif
	release_all_fingers(ts);
	ts->power_enable(0);

	msleep(200);
	ts->power_enable(1);
	enable_irq(ts->client->irq);
}

static ssize_t all_refer_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int status = 0;
	int i;
	struct melfas_ts_data *ts  = dev_get_drvdata(dev);

	check_debug_data(ts);

	if (debug_print)
		pr_info("[TSP] inspection data\n");
	for (i = 0; i < 1134; i++) {
		/* out of range */
		if (inspection_data[i] < 30) {
			status = 1;
			break;
		}

		if (debug_print) {
			if (0 == i % 27)
				printk("\n");
			printk("%5u  ", inspection_data[i]);
		}
	}

#if DEBUG_LOW_DATA
	pr_info("[TSP] low data\n");
	for (i = 0; i < 1134; i++) {
		if (0 == i % 27)
			printk("\n");
		printk("%5u  ", low_data[i]);
	}
#endif
	return sprintf(buf, "%u\n", status);
}

static void check_intesity_data(struct melfas_ts_data *ts)
{

	u8 write_buffer[6];
	u8 read_buffer[2];
	int sensing_line, exciting_line;
	int gpio = irq_to_gpio(ts->client->irq);

	if (0 == inspection_data[0]) {
		/* enter the debug mode */
		write_buffer[0] = 0xA0;
		write_buffer[1] = 0x1A;
		write_buffer[2] = 0x0;
		write_buffer[3] = 0x0;
		write_buffer[4] = 0x0;
		write_buffer[5] = 0x01;
		melfas_i2c_write(ts->client, (char *)write_buffer, 6);

		/* wating for the interrupt*/
		while (gpio_get_value(gpio)) {
			printk(".");
			udelay(100);
		}

		/* read the dummy data */
		melfas_i2c_read(ts->client, 0xA8, 2, read_buffer);

		write_buffer[5] = 0x02;
		for (sensing_line = 0; sensing_line < 27; sensing_line++) {
			for (exciting_line =0; exciting_line < 42; exciting_line++) {
				write_buffer[2] = exciting_line;
				write_buffer[3] = sensing_line;
				melfas_i2c_write(ts->client, (char *)write_buffer, 6);
				melfas_i2c_read(ts->client, 0xA8, 2, read_buffer);
				inspection_data[exciting_line + sensing_line * 42] =
					(read_buffer[1] & 0xf) << 8 | read_buffer[0];
			}
		}

		release_all_fingers(ts);
		ts->power_enable(0);
		msleep(700);
		ts->power_enable(1);
	}

	write_buffer[0] = 0xA0;
	write_buffer[1] = 0x1A;
	write_buffer[4] = 0x0;
	write_buffer[5] = 0x04;
	for (sensing_line = 0; sensing_line < 27; sensing_line++) {
		for (exciting_line =0; exciting_line < 42; exciting_line++) {
			write_buffer[2] = exciting_line;
			write_buffer[3] = sensing_line;
			melfas_i2c_write(ts->client, (char *)write_buffer, 6);
			melfas_i2c_read(ts->client, 0xA8, 2, read_buffer);
			lntensity_data[exciting_line + sensing_line * 42] =
				(read_buffer[1] & 0xf) << 8 | read_buffer[0];
		}
	}
#if 1
	pr_info("[TSP] lntensity data");
	int i;
	for (i = 0; i < 1134; i++) {
		if (0 == i % 27)
			printk("\n");
		printk("%5u  ", lntensity_data[i]);
	}
#endif

}

static ssize_t set_refer0_mode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	u16 refrence = 0;
	struct melfas_ts_data *ts = dev_get_drvdata(dev);

	check_intesity_data(ts);

	refrence = inspection_data[927];
	return sprintf(buf, "%u\n", refrence);
}

static ssize_t set_refer1_mode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	u16 refrence = 0;
	refrence = inspection_data[172];
	return sprintf(buf, "%u\n", refrence);
}

static ssize_t set_refer2_mode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	u16 refrence = 0;
	refrence = inspection_data[608];
	return sprintf(buf, "%u\n", refrence);
}

static ssize_t set_refer3_mode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	u16 refrence = 0;
	refrence = inspection_data[1003];
	return sprintf(buf, "%u\n", refrence);
}

static ssize_t set_refer4_mode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	u16 refrence = 0;
	refrence = inspection_data[205];
	return sprintf(buf, "%u\n", refrence);
}

static ssize_t set_intensity0_mode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	u16 intensity = 0;
	intensity = lntensity_data[927];
	return sprintf(buf, "%u\n", intensity);
}

static ssize_t set_intensity1_mode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	u16 intensity = 0;
	intensity = lntensity_data[172];
	return sprintf(buf, "%u\n", intensity);
}

static ssize_t set_intensity2_mode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	u16 intensity = 0;
	intensity = lntensity_data[608];
	return sprintf(buf, "%u\n", intensity);
}

static ssize_t set_intensity3_mode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	u16 intensity = 0;
	intensity = lntensity_data[1003];
	return sprintf(buf, "%u\n", intensity);
}

static ssize_t set_intensity4_mode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	u16 intensity = 0;
	intensity = lntensity_data[205];
	return sprintf(buf, "%u\n", intensity);
}

/* noise test */
static DEVICE_ATTR(set_all_refer, S_IRUGO, all_refer_show, NULL);
static DEVICE_ATTR(set_refer0, S_IRUGO, set_refer0_mode_show, NULL);
static DEVICE_ATTR(set_delta0, S_IRUGO, set_intensity0_mode_show, NULL);
static DEVICE_ATTR(set_refer1, S_IRUGO, set_refer1_mode_show, NULL);
static DEVICE_ATTR(set_delta1, S_IRUGO, set_intensity1_mode_show, NULL);
static DEVICE_ATTR(set_refer2, S_IRUGO, set_refer2_mode_show, NULL);
static DEVICE_ATTR(set_delta2, S_IRUGO, set_intensity2_mode_show, NULL);
static DEVICE_ATTR(set_refer3, S_IRUGO, set_refer3_mode_show, NULL);
static DEVICE_ATTR(set_delta3, S_IRUGO, set_intensity3_mode_show, NULL);
static DEVICE_ATTR(set_refer4, S_IRUGO, set_refer4_mode_show, NULL);
static DEVICE_ATTR(set_delta4, S_IRUGO, set_intensity4_mode_show, NULL);
#endif

static struct attribute *sec_touch_attributes[] = {
	&dev_attr_fw_dev.attr,
	&dev_attr_fw_bin.attr,
	&dev_attr_fw_bin_slv.attr,
	&dev_attr_fw_bin_detail.attr,
	&dev_attr_debug_mode.attr,
	&dev_attr_debug_log.attr,
#ifndef ENABLE_NOISE_TEST_MODE
	&dev_attr_threshold.attr,
	&dev_attr_set_all_refer.attr,
	&dev_attr_set_refer0.attr,
	&dev_attr_set_delta0.attr,
	&dev_attr_set_refer1.attr,
	&dev_attr_set_delta1.attr,
	&dev_attr_set_refer2.attr,
	&dev_attr_set_delta2.attr,
	&dev_attr_set_refer3.attr,
	&dev_attr_set_delta3.attr,
	&dev_attr_set_refer4.attr,
	&dev_attr_set_delta4.attr,
#endif
	NULL,
};

static struct attribute_group sec_touch_attr_group = {
	.attrs = sec_touch_attributes,
};

#ifdef ENABLE_NOISE_TEST_MODE
static struct attribute *sec_touch_facotry_attributes[] = {
	&dev_attr_set_all_refer.attr,
	&dev_attr_set_refer0.attr,
	&dev_attr_set_delta0.attr,
	&dev_attr_set_refer1.attr,
	&dev_attr_set_delta1.attr,
	&dev_attr_set_refer2.attr,
	&dev_attr_set_delta2.attr,
	&dev_attr_set_refer3.attr,
	&dev_attr_set_delta3.attr,
	&dev_attr_set_refer4.attr,
	&dev_attr_set_delta4.attr,
	&dev_attr_set_threshould.attr,
	NULL,
};

static struct attribute_group sec_touch_factory_attr_group = {
	.attrs = sec_touch_facotry_attributes,
};
#endif

static int melfas_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct melfas_ts_data *ts;
	struct device *tsp_dev;
#ifdef ENABLE_NOISE_TEST_MODE
	struct device *test_dev;
#endif
	struct input_dev *input;
	bool empty_chip = false;
	u8 val_mst = 0;
	u8 val_slv = 0;
	u8 fw_ver = 0;
	int ret = 0;
	int ret_slv = 0;

	u8 	val_core 	= 0;
	u8 	val_private = 0;
	u8 	val_public 	= 0;
	int ret_core 	= 0;
	int ret_private = 0;
	int ret_public 	= 0;

	int i	= 0;
	int irq = 0;
	int val_firm = -1;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("[TSP] melfas_ts_probe: need I2C_FUNC_I2C\n");
		ret = -ENODEV;
		goto err_check_functionality_failed;
	}

	ts = kmalloc(sizeof(struct melfas_ts_data), GFP_KERNEL);
	if (ts == NULL) {
		pr_err("[TSP] %s: failed to create a state of melfas-ts\n", __func__);
		ret = -ENOMEM;
		goto err_alloc_failed;
	}

	ts->pdata = client->dev.platform_data;
	if (ts->pdata->power_enable)
		ts->power_enable = ts->pdata->power_enable;

	ts->callbacks.inform_charger = inform_charger_connection;
	if (ts->pdata->register_cb)
		ts->pdata->register_cb(&ts->callbacks);
	ts->tsp_status = true;

#ifdef SET_DOWNLOAD_BY_GPIO
	ts->gpio_scl = ts->pdata->gpio_scl;
	ts->gpio_sda = ts->pdata->gpio_sda;
#endif
	ts->touch_id = ts->pdata->gpio_touch_id;
	ts->client = client;
	i2c_set_clientdata(client, ts);

#ifndef FW_FROM_FILE

	ret = check_firmware_master(ts, &val_mst);
	ret_slv = check_firmware_slave(ts, &val_slv);

#if MELFAS_ISP_DOWNLOAD
// ISP mode start
	if (ret || ret_slv) {
		empty_chip = true;
		pr_err("[TSP] Failed to check firmware version : %d , %d",ret, ret_slv);
	}

	fw_ver = ts->touch_id ? ILJIN_FW_VER : MELFAS_FW_VER;

	if(val_mst > 0x0 && val_mst < 0x50 )
		val_firm = 0;
	else if (val_mst > 0x50 )
		val_firm = 1;
	else
		val_firm = -1;

	/* (current version  < binary verion) */
	if (val_mst != val_slv || val_firm != ts->touch_id || val_mst < fw_ver || empty_chip )
		firmware_update(ts);

	ret = check_firmware_master(ts, &val_mst);
	ret_slv = check_firmware_slave(ts, &val_slv);

	pr_info("[TSP] %s panel - current firmware version : 0x%x , 0x%x\n",
		ts->touch_id ? "Iljin" : "Melfas", val_mst, val_slv);

	ret = check_slave_boot(ts, &val_mst);
	if (ret)
		pr_err("[TSP] Failed to check slave boot : %d", ret);
#else
// ISC mode start
	pr_info("[TSP] ISC mode Before update %s panel - current firmware version : 0x%x , 0x%x\n",
		ts->touch_id ? "Iljin" : "Melfas", val_mst, val_slv);

	firmware_update(ts);

	ret_core 	= check_firmware_core(ts, &val_core);
	ret_private = check_firmware_private(ts, &val_private);
	ret_public 	= check_firmware_public(ts, &val_public);

	if (ret_core || ret_private || ret_public) {
		pr_err("[TSP] Failed to check firmware version : %d , %d, %d\n",
			ret_core, ret_private , ret_public);
	}
	pr_info("[TSP] %s panel - current firmware version(ISC mode) : 0x%x , 0x%x, 0x%x\n",
		ts->touch_id ? "Iljin" : "Melfas", val_core, val_private , val_public);
#endif

#endif

	input = input_allocate_device();
	if (!input) {
		pr_err("[TSP] %s: failed to allocate input device\n", __func__);
		ret = -ENOMEM;
		goto err_alloc_failed;
	}

	ts->input_dev = input;

#ifdef CONFIG_SAMSUNG_INPUT
	input->name = "sec_touchscreen";
#else
	input->name = client->name;
#endif

	set_bit(EV_ABS,  input->evbit);
	set_bit(EV_SYN, input->evbit);
	set_bit(EV_KEY, input->evbit);
	set_bit(BTN_TOUCH, input->keybit);

#if defined(COOD_ROTATE_90) || defined(COOD_ROTATE_270)
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, ts->pdata->max_y, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, ts->pdata->max_x, 0, 0);
#else
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, ts->pdata->max_x, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, ts->pdata->max_y, 0, 0);
#endif
	input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, ts->pdata->max_pressure, 0, 0);
	input_set_abs_params(input, ABS_MT_WIDTH_MAJOR, 0, ts->pdata->max_width, 0, 0);
	input_set_abs_params(input, ABS_MT_TRACKING_ID, 0, P5_MAX_TOUCH-1, 0, 0);

	ret = input_register_device(input);
	if (ret) {
		pr_err("[TSP] %s: failed to register input device\n", __func__);
		ret = -ENOMEM;
		goto err_input_register_device_failed;
	}

	if (client->irq) {
		irq = client->irq;

		ret = request_threaded_irq(irq, NULL, melfas_ts_irq_handler,
					IRQF_ONESHOT | IRQF_TRIGGER_LOW,
					ts->client->name, ts);
		if (ret) {
			pr_err("[TSP] %s: Can't allocate irq %d, ret %d\n", __func__, irq, ret);
			ret = -EBUSY;
			goto err_request_irq;
		}
	}

	for (i = 0; i < P5_MAX_TOUCH ; i++)  /* _SUPPORT_MULTITOUCH_ */
		g_Mtouch_info[i].strength = -1;

#ifdef CONFIG_SAMSUNG_INPUT
	tsp_dev  = device_create(sec_class, NULL, 0, ts, "sec_touch");
	if (IS_ERR(tsp_dev))
		pr_err("[TSP] Failed to create device for the sysfs\n");

	ret = sysfs_create_group(&tsp_dev->kobj, &sec_touch_attr_group);
	if (ret)
		pr_err("[TSP] Failed to create sysfs group\n");
#endif

#ifdef ENABLE_NOISE_TEST_MODE
	test_dev = device_create(sec_class, NULL, 0, ts, "qt602240_noise_test");
	if (IS_ERR(test_dev)) {
		pr_err("Failed to create device for the factory test\n");
		ret = -ENODEV;
	}

	ret = sysfs_create_group(&test_dev->kobj, &sec_touch_factory_attr_group);
	if (ret) {
		pr_err("Failed to create sysfs group for the factory test\n");
	}
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = melfas_ts_early_suspend;
	ts->early_suspend.resume = melfas_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif

	return 0;

err_request_irq:
	free_irq(client->irq, ts);
err_input_register_device_failed:
	input_free_device(input);
err_alloc_failed:
	kfree(ts);
err_check_functionality_failed:
	return ret;
}

static int melfas_ts_remove(struct i2c_client *client)
{
	struct melfas_ts_data *ts = i2c_get_clientdata(client);

	pr_warning("[TSP] %s\n", __func__);

	unregister_early_suspend(&ts->early_suspend);
	free_irq(client->irq, ts);
	input_unregister_device(ts->input_dev);
	kfree(ts);
	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void melfas_ts_early_suspend(struct early_suspend *h)
{
	struct melfas_ts_data *ts = container_of(h,
		struct melfas_ts_data, early_suspend);
#else
static int melfas_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int ret;
	struct melfas_ts_data *ts = i2c_get_clientdata(client);
#endif

	pr_info("[TSP] %s\n", __func__);

	release_all_fingers(ts);

	disable_irq(ts->client->irq);

	if (ts->power_enable)
		ts->power_enable(0);
	ts->tsp_status = false;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void melfas_ts_late_resume(struct early_suspend *h)
{
	struct melfas_ts_data *ts = container_of(h,
		struct melfas_ts_data, early_suspend);
#else
static int melfas_ts_resume(struct i2c_client *client)
{
	struct melfas_ts_data *ts = i2c_get_clientdata(client);
#endif

	char buf[2];

	pr_info("[TSP] %s : TA/USB %sconnect\n", __func__,ts->charging_status?"":"dis");

	if (ts->power_enable)
		ts->power_enable(1);
	ts->tsp_status = true;

	buf[0] = 0x60;
	buf[1] = ts->charging_status;
	msleep(500);
	melfas_i2c_write(ts->client, (char *)buf, 2);

	enable_irq(ts->client->irq);
}

static void melfas_ts_shutdown(struct i2c_client *client)
{
	struct melfas_ts_data *ts = i2c_get_clientdata(client);

	free_irq(client->irq, ts);
	ts->power_enable(0);
#ifdef CONFIG_ARCH_TEGRA
	gpio_direction_output(GPIO_TOUCH_INT, 0);
#endif
}

static const struct i2c_device_id melfas_ts_id[] = {
	{ MELFAS_TS_NAME, 0 },
	{ }
};


static struct i2c_driver melfas_ts_driver = {
	.driver		= {
		.name	= MELFAS_TS_NAME,
	},
	.id_table		= melfas_ts_id,
	.probe		= melfas_ts_probe,
	.remove		= __devexit_p (melfas_ts_remove),
	.shutdown	= melfas_ts_shutdown,
#ifndef CONFIG_HAS_EARLYSUSPEND
	.suspend		= melfas_ts_suspend,
	.resume		= melfas_ts_resume,
#endif
};

static int __devinit melfas_ts_init(void)
{
	return i2c_add_driver(&melfas_ts_driver);
}

static void __exit melfas_ts_exit(void)
{
	i2c_del_driver(&melfas_ts_driver);
}

MODULE_DESCRIPTION("Driver for Melfas MTSI Touchscreen Controller");
MODULE_AUTHOR("MinSang, Kim <kimms@melfas.com>");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");

module_init(melfas_ts_init);
module_exit(melfas_ts_exit);
