/*
 * cmc623.c
 *
 * driver supporting CMC623 ImageConverter functions for Samsung P3 device
 *
 * COPYRIGHT(C) Samsung Electronics Co., Ltd. 2006-2010 All Right Reserved.
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

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/wakelock.h>
#include <linux/blkdev.h>
#include <linux/i2c.h>
#include <mach/gpio.h>
#include <mach/gpio-sec.h>

#include <linux/delay.h>
#include <linux/workqueue.h>

#include <linux/regulator/consumer.h>
#include <linux/earlysuspend.h>

#include <linux/time.h>
#include <linux/timer.h>
#include <linux/wakelock.h>



#include "cmc623.h"

#if defined(CONFIG_MACH_SAMSUNG_P4) || defined(CONFIG_MACH_SAMSUNG_P4WIFI) || defined(CONFIG_MACH_SAMSUNG_P4LTE)
#include "p4_cmc623_tune.h"
#elif defined(CONFIG_MACH_SAMSUNG_P5)
#include "p5_cmc623_tune.h"
#else
#error Undefined Tuning Value
#endif



#define ENABLE_CMC623_TUNING
//#define ENABLE_LCD_TYPE_ADC 

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define GPIO_LEVEL_LOW	0
#define GPIO_LEVEL_HIGH	1

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend	cmc623_early_suspend;
#endif

#define MAX_LEVEL	1600


#define CMC623_DEVICE_ADDR	    0x38

/* Each client has this additional data */
struct cmc623_data {
	struct i2c_client *client;
};

static struct cmc623_data *p_cmc623_data;

static struct i2c_client *g_client;
#define I2C_M_WR 0 /* for i2c */
#define I2c_M_RD 1 /* for i2c */

static int lcdonoff = FALSE;

#if defined(CONFIG_TARGET_LOCALE_KOR)
static int cmc623_current_region_enable = false; //region mode added
#endif

struct cmc623_gpio {
    unsigned int bl_reset;
    unsigned int ima_sleep;
    unsigned int ima_bypass;
    unsigned int ima_pwren;
    unsigned int lvds_n_shdn;
    unsigned int mlcd_on1;
    unsigned int mlcd_on;
    unsigned int ima_n_rst;  
};
struct cmc623_state_type{
	enum eCabc_Mode cabc_mode;
	unsigned int brightness;
	unsigned int suspended;
    enum eLcd_mDNIe_UI scenario;
    enum eBackground_Mode background;
//This value must reset to 0 (standard value) when change scenario
    enum eCurrent_Temp temperature;
    enum eOutdoor_Mode ove;
    struct str_sub_unit *sub_tune;
    struct str_main_unit *main_tune;
    struct cmc623_gpio gpio;
    unsigned int suspending;
    unsigned int resuming; 
};

static struct cmc623_state_type cmc623_state = {
	.cabc_mode = CABC_ON_MODE,
    .brightness = 784,
	.suspended = FALSE,
    .scenario = mDNIe_UI_MODE,
    .background = STANDARD_MODE,
    .temperature = TEMP_STANDARD,
    .ove = OUTDOOR_OFF_MODE,
    .sub_tune = NULL,
    .main_tune = NULL,
    .suspending = 0,
    .resuming = 0,
};


static DEFINE_MUTEX(tuning_mutex);
static int cmc623_I2cWrite16(unsigned char Addr, unsigned long Data);


static void bypass_onoff_ctrl(int value);
static int apply_main_tune_value(enum eLcd_mDNIe_UI ui, enum eBackground_Mode bg, enum eCabc_Mode cabc, int force);
static int apply_sub_tune_value(enum eCurrent_Temp temp, enum eOutdoor_Mode ove, enum eCabc_Mode cabc, int force);
int __cmc623_set_tune_value(struct Cmc623RegisterSet *value);
#if defined(CONFIG_TARGET_LOCALE_KOR)
static unsigned long last_cmc623_Bank = 0xffff;
#endif

unsigned long last_cmc623_Algorithm = 0xffff;

static int cmc623_current_type = 0;

typedef enum
{
	cmc623_type_none = 0,
	cmc623_type_lsi,
	cmc623_type_fujitsu,
}CMC623_type;
#if 0 
static int set_mdnie_scenario_mode(unsigned int mode); 
#endif


#ifdef ENABLE_CMC623_TUNING
#define CMC623_MAX_SETTINGS	 100
#define MAX_FILE_NAME 128
#define TUNING_FILE_PATH "/sdcard/tuning/"

static int tuning_enable = 0;
static struct Cmc623RegisterSet Cmc623_TuneSeq[CMC623_MAX_SETTINGS];
static char tuning_filename[MAX_FILE_NAME];

static int load_tuning_data(char *filename);
bool cmc623_tune(unsigned long num);
static int parse_text(char *src, int len);
#endif


#ifdef ENABLE_LCD_TYPE_ADC
extern int tps6586x_adc_read(u32 *mili_volt, u8 channel);
static struct wake_lock adc_wake_lock;
static struct timer_list adc_timer;

static struct workqueue_struct *lcd_adc_workqueue;
static void lcd_adc_work_handler(struct work_struct *);
static DECLARE_WORK(lcd_adc_work, lcd_adc_work_handler);

#endif 

static struct workqueue_struct *camera_workqueue = NULL;
static void camera_tuning_hander(struct work_struct *unsed);
static DECLARE_WORK(camera_tuning_work, camera_tuning_hander);
static struct timer_list camera_timer;
unsigned int camera_resume_flag = 0;
struct Cmc623RegisterSet *camera_value = NULL;

static struct workqueue_struct *lcd_bl_workqueue = NULL;
static void lcd_bl_handler(struct work_struct *unused);
static DECLARE_DELAYED_WORK(lcd_bl_work, lcd_bl_handler);
unsigned int lcd_bl_workqueue_statue = 0;


const struct str_sub_tuning sub_tune_value[MAX_TEMP_MODE][MAX_OUTDOOR_MODE] = {
    {   
        {   
            .value[CABC_OFF_MODE] = {.name = "STANDARD,OUTDOOR:OFF,CABC:OFF",  .value = NULL}, 
            .value[CABC_ON_MODE] = {.name = "STANDARD,OUTDOOR:OFF,CABC:ON",   .value = NULL} 
        },
        { 
            .value[CABC_OFF_MODE] = {.name = "STANDARD,OUTDOOR:ON,CABC:OFF",   .value = ove_cabcoff},
            .value[CABC_ON_MODE] = {.name = "STANDARD,OUTDOOR:ON,CABC:ON",    .value = ove_cabcon}
        } 
    },
    {   
        {
            .value[CABC_OFF_MODE] = {.name = "WARM,OUTDOOR:OFF,CABC:OFF",      .value = warm_cabcoff},      
            .value[CABC_ON_MODE] = {.name = "WARM,OUTDOOR:OFF,CABC:ON",       .value = warm_cabcon}
        },
        {
            .value[CABC_OFF_MODE] = {.name = "WARM,OUTDOOR:ON,CABC:OFF",       .value = warm_ove_cabcoff},     
            .value[CABC_ON_MODE] = {.name = "WARM,OUTDOOR:ON,CABC:ON",        .value = warm_ove_cabcon}
        }
    },
    {   
        {
            .value[CABC_OFF_MODE] = {.name = "COLD,OUTDOOR:OFF,CABC:OFF",      .value = cold_cabcoff},      
            .value[CABC_ON_MODE] = {.name = "COLD,OUTDOOR:OFF,CABC:ON",       .value = cold_cabcon}
    },
        {
            .value[CABC_OFF_MODE] = {.name = "COLD,OUTDOOR:ON,CABC:OFF",       .value = cold_ove_cabcoff},     
            .value[CABC_ON_MODE] = {.name = "COLD,OUTDOOR:ON,CABC:ON",        .value = cold_ove_cabcon}
        }
    },
};

const struct str_main_tuning tune_value[MAX_BACKGROUND_MODE][MAX_mDNIe_MODE] = {
    
    {{.value[CABC_OFF_MODE] = {.name = "DYN_UI_OFF",        .flag = 0, .tune = dynamic_ui_cabcoff, .plut = NULL},
      .value[CABC_ON_MODE]  = {.name = "DYN_UI_ON",         .flag = 0, .tune = dynamic_ui_cabcon, .plut = NULL}},
     {.value[CABC_OFF_MODE] = {.name = "DYN_VIDEO_OFF",     .flag = 0, .tune = dynamic_video_cabcoff, .plut = cmc623_video_plut},
      .value[CABC_ON_MODE]  = {.name = "DYN_VIDEO_ON",      .flag = 0, .tune = dynamic_video_cabcon, .plut = cmc623_video_plut}},
     {.value[CABC_OFF_MODE] = {.name = "DYN_VIDEO_W_OFF",   .flag = 0, .tune = dynamic_video_cabcoff, .plut = cmc623_video_plut},
      .value[CABC_ON_MODE]  = {.name = "DYN_VIDEO_W_ON",    .flag = 0, .tune = dynamic_video_cabcon, .plut = cmc623_video_plut}},
     {.value[CABC_OFF_MODE] = {.name = "DYN_VIDEO_C_OFF",   .flag = 0, .tune = dynamic_video_cabcoff, .plut = cmc623_video_plut},
      .value[CABC_ON_MODE]  = {.name = "DYN_VIDEO_C_ON",    .flag = 0, .tune = dynamic_video_cabcon, .plut = cmc623_video_plut}},
     {.value[CABC_OFF_MODE] = {.name = "DYN_CAMERA_OFF",    .flag = TUNE_FLAG_CABC_ALWAYS_OFF, .tune = camera, .plut = NULL},
      .value[CABC_ON_MODE]  = {.name = "DYN_CAMERA_ON",     .flag = TUNE_FLAG_CABC_ALWAYS_OFF, .tune = camera, .plut = NULL}},
     {.value[CABC_OFF_MODE] = {.name = "DYN_NAVI_OFF",      .flag = 0, .tune = NULL, .plut = NULL},
      .value[CABC_ON_MODE]  = {.name = "DYN_NAVI_ON",       .flag = 0, .tune = NULL, .plut = NULL}},
     {.value[CABC_OFF_MODE] = {.name = "DYN_GALLERY_OFF",   .flag = 0, .tune = dynamic_gallery_cabcoff, .plut = NULL},
      .value[CABC_ON_MODE]  = {.name = "DYN_GALLERY_ON",    .flag = 0, .tune = dynamic_gallery_cabcon, .plut = NULL}},
     {.value[CABC_OFF_MODE] = {.name = "DYN_DMB_OFF",     .flag = 0, .tune = dynamic_dmb_cabcoff, .plut = cmc623_video_plut},
      .value[CABC_ON_MODE]  = {.name = "DYN_DMB_ON",      .flag = 0, .tune = dynamic_dmb_cabcon, .plut = cmc623_video_plut}}},      
      
      
    {{.value[CABC_OFF_MODE] = {.name = "STD_UI_OFF",        .flag = 0, .tune = standard_ui_cabcoff, .plut = NULL},
      .value[CABC_ON_MODE]  = {.name = "STD_UI_ON",         .flag = 0, .tune = standard_ui_cabcon, .plut = NULL}},
     {.value[CABC_OFF_MODE] = {.name = "STD_VIDEO_OFF",     .flag = 0, .tune = standard_video_cabcoff, .plut = cmc623_video_plut},
      .value[CABC_ON_MODE]  = {.name = "STD_VIDEO_ON",      .flag = 0, .tune = standard_video_cabcon, .plut = cmc623_video_plut}},
     {.value[CABC_OFF_MODE] = {.name = "STD_VIDEO_W_OFF",   .flag = 0, .tune = standard_video_cabcoff, .plut = cmc623_video_plut},
      .value[CABC_ON_MODE]  = {.name = "STD_VIDEO_W_ON",    .flag = 0, .tune = standard_video_cabcon, .plut = cmc623_video_plut}},
     {.value[CABC_OFF_MODE] = {.name = "STD_VIDEO_C_OFF",   .flag = 0, .tune = standard_video_cabcoff, .plut = cmc623_video_plut},
      .value[CABC_ON_MODE]  = {.name = "STD_VIDEO_C_ON",    .flag = 0, .tune = standard_video_cabcon, .plut = cmc623_video_plut}},
     {.value[CABC_OFF_MODE] = {.name = "STD_CAMERA_OFF",    .flag = TUNE_FLAG_CABC_ALWAYS_OFF, .tune = camera, .plut = NULL},
      .value[CABC_ON_MODE]  = {.name = "STD_CAMERA_ON",     .flag = TUNE_FLAG_CABC_ALWAYS_OFF, .tune = camera, .plut = NULL}},
     {.value[CABC_OFF_MODE] = {.name = "STD_NAVI_OFF",      .flag = 0, .tune = NULL, .plut = NULL},
      .value[CABC_ON_MODE]  = {.name = "STD_NAVI_ON",       .flag = 0, .tune = NULL, .plut = NULL}},
     {.value[CABC_OFF_MODE] = {.name = "STD_GALLERY_OFF",   .flag = 0, .tune = standard_gallery_cabcoff, .plut = NULL},
      .value[CABC_ON_MODE]  = {.name = "STD_GALLERY_ON",    .flag = 0, .tune = standard_gallery_cabcon, .plut = NULL}},	
     {.value[CABC_OFF_MODE] = {.name = "DYN_DMB_OFF",     .flag = 0, .tune = dynamic_dmb_cabcoff, .plut = cmc623_video_plut},
      .value[CABC_ON_MODE]  = {.name = "DYN_DMB_ON",      .flag = 0, .tune = dynamic_dmb_cabcon, .plut = cmc623_video_plut}}},
     
    {{.value[CABC_OFF_MODE] = {.name = "MOV_UI_OFF",        .flag = 0, .tune = movie_ui_cabcoff, .plut = NULL},
      .value[CABC_ON_MODE]  = {.name = "MOV_UI_ON",         .flag = 0, .tune = movie_ui_cabcon, .plut = NULL}},
     {.value[CABC_OFF_MODE] = {.name = "MOV_VIDEO_OFF",     .flag = 0, .tune = movie_video_cabcoff, .plut = cmc623_video_plut},
      .value[CABC_ON_MODE]  = {.name = "MOV_VIDEO_ON",      .flag = 0, .tune = movie_video_cabcon, .plut = cmc623_video_plut}},
     {.value[CABC_OFF_MODE] = {.name = "MOV_VIDEO_W_OFF",   .flag = 0, .tune = movie_video_cabcoff, .plut = cmc623_video_plut},
      .value[CABC_ON_MODE]  = {.name = "MOV_VIDEO_W_ON",    .flag = 0, .tune = movie_video_cabcon, .plut = cmc623_video_plut}},
     {.value[CABC_OFF_MODE] = {.name = "MOV_VIDEO_C_OFF",   .flag = 0, .tune = movie_video_cabcoff, .plut = cmc623_video_plut},
      .value[CABC_ON_MODE]  = {.name = "MOV_VIDEO_C_ON",    .flag = 0, .tune = movie_video_cabcon, .plut = cmc623_video_plut}},
     {.value[CABC_OFF_MODE] = {.name = "MOV_CAMERA_OFF",    .flag = TUNE_FLAG_CABC_ALWAYS_OFF, .tune = camera, .plut = NULL},
      .value[CABC_ON_MODE]  = {.name = "MOV_CAMERA_ON",     .flag = TUNE_FLAG_CABC_ALWAYS_OFF, .tune = camera, .plut = NULL}},
     {.value[CABC_OFF_MODE] = {.name = "MOV_NAVI_OFF",      .flag = 0, .tune = NULL, .plut = NULL},
      .value[CABC_ON_MODE]  = {.name = "MOV_NAVI_ON",       .flag = 0, .tune = NULL, .plut = NULL}},
     {.value[CABC_OFF_MODE] = {.name = "MOV_GALLERY_OFF",   .flag = 0, .tune = movie_gallery_cabcoff, .plut = NULL},
      .value[CABC_ON_MODE]  = {.name = "MOV_GALLERY_ON",    .flag = 0, .tune = movie_gallery_cabcon, .plut = NULL}},
     {.value[CABC_OFF_MODE] = {.name = "DYN_DMB_OFF",     .flag = 0, .tune = dynamic_dmb_cabcoff, .plut = cmc623_video_plut},
      .value[CABC_ON_MODE]  = {.name = "DYN_DMB_ON",      .flag = 0, .tune = dynamic_dmb_cabcon, .plut = cmc623_video_plut}}},       
};


static int cmc623_I2cWrite(struct i2c_client *client, u8 reg,
				u8 *data, u8 length)
{
	int ret, i;
	u8 buf[length+1];
	struct i2c_msg msg[1];

	buf[0] = reg;
	for (i = 0; i < length; i++)
		buf[i+1] = *(data++);

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = length+1;
	msg[0].buf = buf;

	ret = i2c_transfer(client->adapter, msg, 1);
	if (ret != 1)
		return -EIO;

	return 0;
}


int cmc623_I2cWrite16(unsigned char Addr, unsigned long Data)
{
	int err = -1000;
	struct i2c_msg msg[1];
	unsigned char data[3];

	if (!p_cmc623_data) {
        pr_err("[CMC623:%s:ERROR] : p_cmc623_data is NULL\n",__func__);
        return -ENODEV;
	}
	g_client = p_cmc623_data->client;

	if (!g_client) {
        pr_err("[CMC623:%s:ERROR] :g_client is NULL\n",__func__);
        return -ENODEV;
	}

	if (!g_client->adapter) {
        pr_err("[CMC623:%s:ERROR] : gclient->adapter is NULL\n",__func__);
		return -ENODEV;
	}

	if (cmc623_state.suspended == TRUE) {
        pr_err("[CMC623:%s:ERROR] : Can't writing value while cmc623 suspend \n",__func__);
		return 0;
	}
#if defined(CONFIG_TARGET_LOCALE_KOR)
		if(Addr == 0x0000)
		{
		if(Data == last_cmc623_Bank)
			{
			return 0;
			}
		last_cmc623_Bank = Data;
		}
	else if(Addr == 0x0001 && last_cmc623_Bank==0)
		{
		last_cmc623_Algorithm = Data;
		}
#endif

	data[0] = Addr;
	data[1] = ((Data >> 8) & 0xFF);
	data[2] = (Data) & 0xFF;
	msg->addr = g_client->addr;
	msg->flags = I2C_M_WR;
	msg->len = 3;
	msg->buf = data;

	err = i2c_transfer(g_client->adapter, msg, 1);
    if (err != 1) {
        pr_err("[CMC623:%s:ERROR] : i2c_transfer failed(%d) for Addr : %x, Data : %lx\n",
            __func__, err, Addr, Data);
        return -EIO;
    }

    return 0;
    
}


char cmc623_I2cRead(u8 reg, u8 *val, unsigned int len)
{
	int	err;
	struct	i2c_msg msg[1];

	unsigned char data[1];
	if ((g_client == NULL) || (!g_client->adapter))
		return -ENODEV;

	msg->addr	= g_client->addr;
	msg->flags	= I2C_M_WR;
	msg->len	= 1;
	msg->buf	= data;
	*data		= reg;

	err = i2c_transfer(g_client->adapter, msg, 1);

	if (err >= 0) {
		msg->flags = I2C_M_RD;
		msg->len   = len;
		msg->buf   = val;
		err = i2c_transfer(g_client->adapter, msg, 1);
	}

	if (err >= 0)
		return 0;

	/* add by inter.park */
	pr_err("%s %d i2c transfer error\n", __func__, __LINE__);

	return err;

}


int cmc623_I2cRead16(u8 reg, u16 *val)
{
	int	err;
	struct	i2c_msg msg[2];
	u8 regaddr = reg;
	u8 data[2];

	if (!p_cmc623_data) {
		pr_err("%s p_cmc623_data is NULL\n", __func__);
		return -ENODEV;
	}
	g_client = p_cmc623_data->client;

	if ((g_client == NULL)) {
		pr_err("%s g_client is NULL\n", __func__);
		return -ENODEV;
	}

	if (!g_client->adapter) {
		pr_err("%s g_client->adapter is NULL\n", __func__);
		return -ENODEV;
	}

	if (regaddr == 0x0001) {
		*val = last_cmc623_Algorithm;
		return 0;
	}

	msg[0].addr   = g_client->addr;
	msg[0].flags  = I2C_M_WR;
	msg[0].len	  = 1;
	msg[0].buf	  = &regaddr;
	msg[1].addr   = g_client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len	 = 2;
	msg[1].buf	 = &data[0];
	err = i2c_transfer(g_client->adapter, &msg[0], 2);

	if (err >= 0) {
		*val = (data[0]<<8) | data[1];
		return 0;
	}
	/* add by inter.park */
	pr_err("%s %d i2c transfer error: %d\n",
			__func__, __LINE__, err);

	return err;
}

static bool __cmc623_set_init(void)
{
    int i;
	int ret = 0;
	int num = 0;
    mutex_lock(&tuning_mutex);    

	if (cmc623_current_type == cmc623_type_fujitsu){
		num =ARRAY_SIZE(cmc623f_init2);
		for (i = 0; i < num; i++) {
			if (cmc623_I2cWrite16(cmc623f_init2[i].RegAddr, cmc623f_init2[i].Data) != 0) {
				pr_err("why return false??!!!\n");
	            mutex_unlock(&tuning_mutex);    
				return FALSE;
			}
			if (cmc623f_init2[i].RegAddr == CMC623_REG_SWRESET && 
	                cmc623f_init2[i].Data == 0xffff)
				usleep_range(2000, 2100);
		}
	}
	else{
		num =ARRAY_SIZE(cmc623_init2);
		for (i = 0; i < num; i++) {
			if (cmc623_I2cWrite16(cmc623_init2[i].RegAddr, cmc623_init2[i].Data) != 0) {
				pr_err("why return false??!!!\n");
	            mutex_unlock(&tuning_mutex);    
				return FALSE;
			}
			if (cmc623_init2[i].RegAddr == CMC623_REG_SWRESET && 
	                cmc623_init2[i].Data == 0xffff)
				usleep_range(2000, 2100);
		}
	}
	
    mutex_unlock(&tuning_mutex);    
	return TRUE;
}

void cmc623_reg_unmask(void)
{
	if (!p_cmc623_data) {
		pr_err("%s cmc623 is not initialized\n", __func__);
		return;
	}
	cmc623_I2cWrite16(0x28, 0x0000);
}


void cabc_onoff_ctrl(int value)
{
    int i = 0;
    int num = 0;

    printk("[CMC623:INFO]:%s value : %d\n", __func__,value);

    if (apply_main_tune_value(cmc623_state.scenario, cmc623_state.background, value, 0) != 0){
        printk("[CMC623:ERROR]:%s: apply main tune value faile \n");
        return ;
    }
}

static void CMC623_Set_Mode(void)
{
	cabc_onoff_ctrl(cmc623_state.cabc_mode);
}


/* value: 0 ~ 1600*/
static void __cmc623_cabc_pwm_reg(int value)
{
	int reg;
	unsigned char *p_plut;
	u16 min_duty;

	if (!p_cmc623_data) {
		pr_err("%s cmc623 is not initialized\n", __func__);
		return;
	}

    p_plut = cmc623_state.main_tune->plut;
    if (p_plut == NULL) {
        p_plut = cmc623_default_plut;
    }

    //printk("[CMC623:INFO]:%s:plut info : %d\n",__func__,p_plut[0]);

	min_duty = p_plut[7] * value / 100;
	if (min_duty < 4) {
		reg = 0xc000 | (max(1, (value*p_plut[3]/100)));
	} 
    else {
		 /*PowerLUT*/
		cmc623_I2cWrite16(0x76, (p_plut[0] * value / 100) << 8
				| (p_plut[1] * value / 100));
		cmc623_I2cWrite16(0x77, (p_plut[2] * value / 100) << 8
				| (p_plut[3] * value / 100));
		cmc623_I2cWrite16(0x78, (p_plut[4] * value / 100) << 8
				| (p_plut[5] * value / 100));
		cmc623_I2cWrite16(0x79, (p_plut[6] * value / 100) << 8
				| (p_plut[7] * value / 100));
		cmc623_I2cWrite16(0x7a, (p_plut[8] * value / 100) << 8);
		reg = 0x5000 | (value<<4);
	}
	cmc623_I2cWrite16(0xB4, reg);
}

/*value: 0 ~ 100*/
static void __cmc623_cabc_pwm(int value)
{
    
    //printk("[CMC623:INFO]:%s:set intensity : %d\n",__func__,value);
    mutex_lock(&tuning_mutex);    
	cmc623_I2cWrite16(0x00, 0x0000);
	__cmc623_cabc_pwm_reg(value);
	cmc623_I2cWrite16(0x28, 0x0000);

    mutex_unlock(&tuning_mutex);
}

/*value: 0 ~ 100*/
static void __cmc623_bypass_pwm(int value)
{
	int reg;

    //printk("[CMC623:INFO]:%s:set intensity : %d\n",__func__,value);

    mutex_lock(&tuning_mutex);    
	//reg = 0x4000 | (value<<4);
    reg = 0xc000 | (value);
	cmc623_I2cWrite16(0x00, 0x0000);
	cmc623_I2cWrite16(0xB4, reg);
	cmc623_I2cWrite16(0x28, 0x0000);
    mutex_unlock(&tuning_mutex);    
}

/* value: 0 ~ 1600*/
void __pwm_brightness(int value)
{
	int data;

	if (value < 0)
		data = 0;
	else if (value > 1600)
		data = 1600;
	else
		data = value;

#if 0
	if (data == 1280 && current_autobrightness_enable) {/*outdoor mode on*/
		current_cmc623_OutDoor_OnOff = TRUE;
		cmc623_Set_Mode(current_cmc623_UI, current_cmc623_CABC_OnOff);
	} else if (current_cmc623_OutDoor_OnOff == TRUE && data < 1280) {
		/*outdoor mode off*/
		current_cmc623_OutDoor_OnOff = FALSE;
		cmc623_Set_Mode(current_cmc623_UI, current_cmc623_CABC_OnOff);
	}
#endif

	if (data < 16)
		data = 1; /*Range of data 0~1600, min value 0~15 is same as 0*/
	else
		data = data >> 4;
    
#ifdef __BYPASS_TEST_ENABLE
    __cmc623_bypass_pwm(data);
    return;
#endif

    if((cmc623_state.cabc_mode == CABC_OFF_MODE) || 
            (cmc623_state.main_tune->flag & TUNE_FLAG_CABC_ALWAYS_OFF)) {
        __cmc623_bypass_pwm(data);
    }
    else {
        __cmc623_cabc_pwm(data);
    }
}

void set_backlight_pwm(int level)
{
	if (lcdonoff == TRUE) {
		__pwm_brightness(level);
	}
	//current_gamma_level = level;
	cmc623_state.brightness = level;
}
EXPORT_SYMBOL(set_backlight_pwm);

static int cmc623_hw_rst()
{

	return 0;
}

int panel_gpio_init()
{
	int ret;
	pr_info("%s called\n", __func__);
	/* LVDS GPIO Initialize */
#if 0  
        if(system_rev > 0x0A){
            ret = gpio_request(GPIO_MLCD_ON, "GPIO_MLCD_ON");
            if (ret) {
        	pr_err("failed to request LVDS GPIO%d\n",
        			GPIO_MLCD_ON);
        		return ret;
               }
            }
        else{
            ret = gpio_request(GPIO_MLCD_ON_REV05, "GPIO_MLCD_ON");
            if (ret) {
        	pr_err("failed to request LVDS GPIO%d\n",
        			GPIO_MLCD_ON_REV05);
        		return ret;
               }
            }
#else
	ret = gpio_request(cmc623_state.gpio.mlcd_on, "GPIO_MLCD_ON");
	if (ret) {
		pr_err("failed to request LVDS GPIO%d\n",
				GPIO_MLCD_ON);
		return ret;
	}
#endif

	ret = gpio_request(cmc623_state.gpio.mlcd_on1, "GPIO_MLCD_ON1");
	if (ret) {
		pr_err("failed to request LVDS GPIO%d\n",cmc623_state.gpio.mlcd_on1);
		return ret;
	}

	ret = gpio_request(cmc623_state.gpio.lvds_n_shdn, "GPIO_LVDS_N_SHDN");
	if (ret) {
		pr_err("failed to request LVDS GPIO%d\n", cmc623_state.gpio.lvds_n_shdn);
		return ret;
	}

	ret = gpio_request(cmc623_state.gpio.bl_reset, "GPIO_BL_RESET");
	if (ret) {
		pr_err("failed to request LVDS GPIO%d\n",cmc623_state.gpio.bl_reset);
		return ret;
	}

#if 0  
        if(system_rev > 0x0A){
            ret = gpio_direction_output(GPIO_MLCD_ON, 1);
            if (ret < 0)
                goto cleanup;
            }
        else{
            ret = gpio_direction_output(GPIO_MLCD_ON_REV05, 1);
            if (ret < 0)
                goto cleanup;
            }
#else
	ret = gpio_direction_output(cmc623_state.gpio.mlcd_on, 1);
	if (ret < 0)
		goto cleanup;
#endif

	ret = gpio_direction_output(cmc623_state.gpio.mlcd_on1, 1);
	if (ret < 0)
		goto cleanup;

	ret = gpio_direction_output(cmc623_state.gpio.lvds_n_shdn, 1);
	if (ret < 0)
		goto cleanup;

	ret = gpio_direction_output(cmc623_state.gpio.bl_reset, 1);
	if (ret < 0)
		goto cleanup;

#if 0   
        if(system_rev > 0x0A)
            tegra_gpio_enable(GPIO_MLCD_ON);
        else
            tegra_gpio_enable(GPIO_MLCD_ON_REV05);
#else
	tegra_gpio_enable(cmc623_state.gpio.mlcd_on);
#endif
	tegra_gpio_enable(cmc623_state.gpio.mlcd_on1);
	tegra_gpio_enable(cmc623_state.gpio.lvds_n_shdn);
	tegra_gpio_enable(cmc623_state.gpio.bl_reset);

	return 0;

cleanup:
#if 0
        if(system_rev > 0x0A)
	    gpio_free(GPIO_MLCD_ON);
        else
	    gpio_free(GPIO_MLCD_ON_REV05);
#else            
	gpio_free(cmc623_state.gpio.mlcd_on);
#endif
	gpio_free(cmc623_state.gpio.mlcd_on1);
	gpio_free(cmc623_state.gpio.lvds_n_shdn);
	gpio_free(cmc623_state.gpio.bl_reset);
	return ret;
}

int cmc623_gpio_init()
{
	int ret;
	pr_info("%s called\n", __func__);

	/* LVDS GPIO Initialize */
	ret = gpio_request(cmc623_state.gpio.ima_pwren , "GPIO_IMA_PWREN");
	if (ret) {
		pr_err("failed to request CMC623 GPIO%d\n",cmc623_state.gpio.ima_pwren );
		return ret;
	}

	ret = gpio_request(cmc623_state.gpio.ima_n_rst, "GPIO_IMA_N_RST");
	if (ret) {
		pr_err("failed to request CMC623 GPIO%d\n",cmc623_state.gpio.ima_n_rst);
		return ret;
	}

	ret = gpio_request(cmc623_state.gpio.ima_bypass, "GPIO_IMA_BYPASS");
	if (ret) {
		pr_err("failed to request CMC623 GPIO%d\n",cmc623_state.gpio.ima_bypass);
		return ret;
	}

	ret = gpio_request(cmc623_state.gpio.ima_sleep, "GPIO_IMA_SLEEP");
	if (ret) {
		pr_err("failed to request CMC623 GPIO%d\n",cmc623_state.gpio.ima_sleep);
		return ret;
	}

	ret = gpio_direction_output(cmc623_state.gpio.ima_pwren , 1);
	if (ret < 0)
		goto cleanup;

	ret = gpio_direction_output(cmc623_state.gpio.ima_n_rst, 1);
	if (ret < 0)
		goto cleanup;

	ret = gpio_direction_output(cmc623_state.gpio.ima_bypass, 1);
	if (ret < 0)
		goto cleanup;

	ret = gpio_direction_output(cmc623_state.gpio.ima_sleep, 1);
	if (ret < 0)
		goto cleanup;

	tegra_gpio_enable(cmc623_state.gpio.ima_pwren );
	tegra_gpio_enable(cmc623_state.gpio.ima_n_rst);
	tegra_gpio_enable(cmc623_state.gpio.ima_bypass);
	tegra_gpio_enable(cmc623_state.gpio.ima_sleep);

	return 0;

cleanup:
	gpio_free(cmc623_state.gpio.ima_pwren );
	gpio_free(cmc623_state.gpio.ima_n_rst);
	gpio_free(cmc623_state.gpio.ima_bypass);
	gpio_free(cmc623_state.gpio.ima_sleep);
	return ret;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
void cmc623_suspend(struct early_suspend *h)
{
    if(cmc623_state.suspending == 1)
        return; 
    
	printk("+ %s\n",__func__);
    
    cmc623_state.suspending = 1;
	cmc623_state.suspended = TRUE;
	lcdonoff = FALSE;
    
    if(lcd_bl_workqueue_statue == 1) {
        printk("%s : delayed workqueue cancel reqeust\n",__func__);
        //cancel_delayed_work(&lcd_bl_workqueue);
        lcd_bl_workqueue_statue = 0;
    }
	if (!p_cmc623_data) {
		pr_err("%s cmc623 is not initialized\n", __func__);
	}

	/* 1.2V/1.8V/3.3V may be on

	CMC623[0x07] := 0x0004
	cmc623_I2cWrite16(0x07, 0x0004);*/

	/* lcd_backlight_reset low*/
	gpio_set_value(cmc623_state.gpio.bl_reset, 0);
#if defined(CONFIG_MACH_SAMSUNG_P5)    
    msleep(200);
#else
    msleep(100);
#endif

	/* CMC623 SLEEPB <= LOW*/
	gpio_set_value(cmc623_state.gpio.ima_sleep, 0);

	/*CMC623 BYPASSB <= LOW*/
	gpio_set_value(cmc623_state.gpio.ima_bypass, 0);

	/* wait 1ms*/
	usleep_range(1000, 2000);

	/* CMC623 FAILSAFEB <= LOW*/
	gpio_set_value(cmc623_state.gpio.ima_pwren , 0);

	/* LVDS_nSHDN low*/
	gpio_set_value(cmc623_state.gpio.lvds_n_shdn, 0);

	/* Disable LVDS Panel Power, 1.2, 1.8, display 3.3V */
	gpio_set_value(cmc623_state.gpio.mlcd_on1, 0);
	usleep_range(1000, 2000);
#if 0  
        if(system_rev > 0x0A)
            gpio_set_value(GPIO_MLCD_ON, 0);
        else
            gpio_set_value(GPIO_MLCD_ON_REV05, 0);            
#else            
	gpio_set_value(cmc623_state.gpio.mlcd_on, 0);
#endif

	msleep(200);
	/*cmc623_state.suspended = TRUE;*/
    cmc623_state.suspending = 0;
    printk("- %s\n",__func__);
}
EXPORT_SYMBOL(cmc623_suspend);


void cmc623_pre_resume()
{

#if 1     
	gpio_set_value(cmc623_state.gpio.ima_n_rst, GPIO_LEVEL_HIGH);
	gpio_set_value(cmc623_state.gpio.ima_pwren , GPIO_LEVEL_LOW);
	gpio_set_value(cmc623_state.gpio.ima_bypass, GPIO_LEVEL_LOW);
	gpio_set_value(cmc623_state.gpio.ima_sleep, GPIO_LEVEL_LOW);
    gpio_set_value(cmc623_state.gpio.lvds_n_shdn, GPIO_LEVEL_LOW);
    gpio_set_value(cmc623_state.gpio.mlcd_on, GPIO_LEVEL_LOW);
    gpio_set_value(cmc623_state.gpio.mlcd_on1, GPIO_LEVEL_LOW);
    gpio_set_value(cmc623_state.gpio.bl_reset, GPIO_LEVEL_LOW);
    msleep(200);
#endif
	/* Enable LVDS Panel Power, 1.2, 1.8, display 3.3V enable */
  #if 0
        if(system_rev > 0x0A)
            gpio_set_value(GPIO_MLCD_ON, GPIO_LEVEL_HIGH);
        else
    	    gpio_set_value(GPIO_MLCD_ON_REV05, GPIO_LEVEL_HIGH);            
  #else
	gpio_set_value(cmc623_state.gpio.mlcd_on, GPIO_LEVEL_HIGH);
  #endif
	usleep_range(30, 100);

	gpio_set_value(cmc623_state.gpio.mlcd_on1, GPIO_LEVEL_HIGH);

	/* LVDS_N_SHDN to high*/
	//usleep_range(1000, 2000);
    //usleep_range(20000, 25000);
    //gpio_set_value(cmc623_state.gpio.lvds_n_shdn, GPIO_LEVEL_HIGH);
}
EXPORT_SYMBOL(cmc623_pre_resume);

/*CAUTION : pre_resume function must be called before using this function*/
void __cmc623_resume(struct early_suspend *h)
{
    printk("+ %s\n",__func__);
	cmc623_pre_resume();

	if (cmc623_current_type == cmc623_type_fujitsu){
		usleep_range(1000, 2000);
		/* BYPASSB <= HIGH*/
		gpio_set_value(cmc623_state.gpio.ima_bypass, GPIO_LEVEL_HIGH);
		usleep_range(1000, 2000);
		
		/* SLEEPB <= HIGH*/
		gpio_set_value(cmc623_state.gpio.ima_sleep, GPIO_LEVEL_HIGH);
		usleep_range(5000, 6000);

		/* FAILSAFEB <= HIGH */
		gpio_set_value(cmc623_state.gpio.ima_pwren , GPIO_LEVEL_HIGH);
		usleep_range(5000, 6000);
	}else{
		usleep_range(1000, 2000);
		/* FAILSAFEB <= HIGH */
		gpio_set_value(cmc623_state.gpio.ima_pwren , GPIO_LEVEL_HIGH);
		usleep_range(1000, 2000);

		/* BYPASSB <= HIGH*/
		gpio_set_value(cmc623_state.gpio.ima_bypass, GPIO_LEVEL_HIGH);
		usleep_range(1000, 2000);

		/* SLEEPB <= HIGH*/
		gpio_set_value(cmc623_state.gpio.ima_sleep, GPIO_LEVEL_HIGH);
		usleep_range(1000, 2000);
	}

	/* RESETB <= LOW*/
	gpio_set_value(cmc623_state.gpio.ima_n_rst, GPIO_LEVEL_LOW);

	/* wait 4ms or above*/
	usleep_range(5000, 6000);

	/* RESETB(K6) <= HIGH*/
	gpio_set_value(cmc623_state.gpio.ima_n_rst, GPIO_LEVEL_HIGH);

	/* wait 0.3ms or above*/
	usleep_range(5000, 6000);

	cmc623_state.suspended = FALSE;

	if (!p_cmc623_data) {
		pr_err("%s cmc623 is not initialized\n", __func__);
	}
    
	__cmc623_set_init();
    
#ifdef ENABLE_CMC623_TUNING 
    if (tuning_enable)
    {
        load_tuning_data(tuning_filename);
        goto rest_resume;
    }
#endif 

#if 0 
    if(cmc623_state.scenario == mDNIe_CAMERA_MODE){ 
        if(camera_value != NULL) {
            if (__cmc623_set_tune_value(camera_value) != 0){
                printk("[CMC623:ERROR]:%s: set tune value falied \n",__func__);
            }
            __pwm_brightness(cmc623_state.brightness);
            camera_value = NULL;
        }
    }

    else {
        apply_main_tune_value(cmc623_state.scenario, cmc623_state.background, cmc623_state.cabc_mode, 1);
        __pwm_brightness(cmc623_state.brightness);
    }
    //apply_sub_tune_value(cmc623_state.);
#else
    apply_main_tune_value(cmc623_state.scenario, cmc623_state.background, cmc623_state.cabc_mode, 1);
    //__pwm_brightness(cmc623_state.brightness);

#endif
    goto rest_resume;

rest_resume :
	lcdonoff = TRUE;
	//msleep_interruptible(250);
	//gpio_set_value(cmc623_state.gpio.bl_reset, GPIO_LEVEL_HIGH);
    gpio_set_value(cmc623_state.gpio.lvds_n_shdn, GPIO_LEVEL_HIGH);
    queue_delayed_work(lcd_bl_workqueue,&lcd_bl_work,HZ/3);
    lcd_bl_workqueue_statue = 1;
    printk("- %s\n",__func__);
}


static void camera_timeout (unsigned long prt)
{
    camera_resume_flag = 0;
}

void cmc623_resume(struct early_suspend *h) 
{
#if 0     
    del_timer(&camera_timer);
    camera_timer.function = camera_timeout;
    camera_timer.data = NULL;
    camera_timer.expires = jiffies+(HZ*2);
    camera_resume_flag = 1;
    add_timer(&camera_timer);
#endif
    if(cmc623_state.resuming == 1)
        return; 

    cmc623_state.resuming = 1; 
    __cmc623_resume(h);
    cmc623_state.resuming = 0;
}
EXPORT_SYMBOL(cmc623_resume);
#endif

void cmc623_shutdown(struct i2c_client *client)
{
	pr_info("[CMC623:INFO] : + %s called\n",__func__);

	cmc623_state.suspended = TRUE;
	lcdonoff = FALSE;

	if (!p_cmc623_data) {
		pr_err("%s cmc623 is not initialized\n", __func__);
	}

	/* 1.2V/1.8V/3.3V may be on

	CMC623[0x07] := 0x0004
	cmc623_I2cWrite16(0x07, 0x0004);*/

	/* lcd_backlight_reset low*/
	gpio_set_value(cmc623_state.gpio.bl_reset, 0);

	mdelay(200); /* can't use sleep in shutdown path */

	/* CMC623 SLEEPB <= LOW*/
	gpio_set_value(cmc623_state.gpio.ima_sleep, 0);

	/* CMC623 BYPASSB <= LOW*/
	gpio_set_value(cmc623_state.gpio.ima_bypass, 0);

	mdelay(1); /* can't use sleep in shutdown path */

	/* CMC623 FAILSAFEB <= LOW*/
	gpio_set_value(cmc623_state.gpio.ima_pwren , 0);

	/* LVDS_nSHDN low*/
	gpio_set_value(cmc623_state.gpio.lvds_n_shdn, 0);

	/* Disable LVDS Panel Power, 1.2, 1.8, display 3.3V */
	gpio_set_value(cmc623_state.gpio.mlcd_on1, 0);
	mdelay(1); /* can't use sleep in shutdown path */
#if 0
        if(system_rev > 0x0A)
	        gpio_set_value(GPIO_MLCD_ON, 0);
        else
            gpio_set_value(GPIO_MLCD_ON_REV05, 0);
#else    
	gpio_set_value(cmc623_state.gpio.mlcd_on, 0);
#endif
	msleep(400);
	pr_info("[CMC623:INFO] : + %s called\n",__func__);

}


//static int current_cabc_onoff = 1;

static ssize_t mdnie_cabc_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", cmc623_state.cabc_mode);
}


static ssize_t mdnie_cabc_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int value;

#ifdef ENABLE_CMC623_TUNING
    if(tuning_enable) {
        printk("[CMC623:INFO]:%s:Tuning Mode Enabled\n",__func__);
        return size;
    }
#endif

	sscanf(buf, "%d", &value);

	printk("[CMC623:INFO]:set cabc on/off mode : %d\n",value);
    
    if (value < CABC_OFF_MODE || value >= MAX_CABC_MODE) {
        printk("[CMC623:ERROR] : wrong cabc mode value : %d\n",value);
        return size;
    }

    if (cmc623_state.suspended == TRUE) {
        pr_err("[CMC623:%s:ERROR] : Can't writing value while cmc623 suspend \n",__func__);
        cmc623_state.cabc_mode = value;
        return size;
    }

    cabc_onoff_ctrl(value);
    //set_backlight_pwm(cmc623_state.brightness);
    return size;
}

static DEVICE_ATTR(mdnie_cabc, 0664, mdnie_cabc_show,
			mdnie_cabc_store);




void lcd_power_on(void)
{
	cmc623_pre_resume();
	pr_info("-0- %s called -0-\n", __func__);

	msleep(1);
	/* FAILSAFEB <= HIGH */
	gpio_set_value(cmc623_state.gpio.ima_pwren , GPIO_LEVEL_HIGH);
	msleep(1);

	/* BYPASSB <= HIGH*/
	gpio_set_value(cmc623_state.gpio.ima_bypass, GPIO_LEVEL_HIGH);
	msleep(1);

	/* SLEEPB <= HIGH*/
	gpio_set_value(cmc623_state.gpio.ima_sleep, GPIO_LEVEL_HIGH);
	msleep(1);

	/* RESETB <= LOW*/
	gpio_set_value(cmc623_state.gpio.ima_n_rst, GPIO_LEVEL_LOW);

	/* wait 4ms or above*/
	msleep(5);

	/* RESETB(K6) <= HIGH*/
	gpio_set_value(cmc623_state.gpio.ima_n_rst, GPIO_LEVEL_HIGH);

	/* wait 0.3ms or above*/
	msleep(5);

	cmc623_state.suspended = FALSE;

	if (!p_cmc623_data) {
		pr_err("%s cmc623 is not initialized\n", __func__);
	}

	__cmc623_set_init();
	CMC623_Set_Mode();


    lcdonoff = TRUE;
	//set_backlight_pwm(cmc623_state.brightness);
    
	/* BL_EN HIGH*/
	msleep(130);
	gpio_set_value(cmc623_state.gpio.bl_reset, GPIO_LEVEL_HIGH);
	pr_info("-0- %s end -0-\n", __func__);
}

void lcd_power_off(void)
{
	pr_info("-0- %s called -0-\n", __func__);

	cmc623_state.suspended = TRUE;
	lcdonoff = FALSE;

	if (!p_cmc623_data) {
		pr_err("%s cmc623 is not initialized\n", __func__);
	}

	/* lcd_backlight_reset low*/
	gpio_set_value(cmc623_state.gpio.bl_reset, 0);
	msleep(100);
	msleep(100);

	/* CMC623 SLEEPB <= LOW*/
	gpio_set_value(cmc623_state.gpio.ima_sleep, 0);

	/* CMC623 BYPASSB <= LOW*/
	gpio_set_value(cmc623_state.gpio.ima_bypass, 0);

	/* wait 1ms*/
	msleep(1);

	/* CMC623 FAILSAFEB <= LOW*/
	gpio_set_value(cmc623_state.gpio.ima_pwren , 0);

	/* LVDS_nSHDN low*/
	gpio_set_value(cmc623_state.gpio.lvds_n_shdn, 0);

	/* Disable LVDS Panel Power, 1.2, 1.8, display 3.3V */
	gpio_set_value(cmc623_state.gpio.mlcd_on1, 0);
	msleep(1);

#if 0 
        if(system_rev > 0x0A)
            gpio_set_value(GPIO_MLCD_ON, 0);
        else
            gpio_set_value(GPIO_MLCD_ON_REV05, 0);
#else    
	gpio_set_value(cmc623_state.gpio.mlcd_on, 0);
#endif
}



static ssize_t lcd_power_file_cmd_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	pr_info("called %s \n", __func__);

	return sprintf(buf, "%u\n", lcdonoff);
}



static ssize_t lcd_power_file_cmd_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int value;

	sscanf(buf, "%d", &value);

	pr_info("[lcd_power] in lcd_power_file_cmd_store, input value = %d \n", value);

	if ((lcdonoff == 0) && (value == 1)) {
		//lcd_power_on();
		cmc623_resume(NULL);
		pr_info("[lcd_power on] <= value : %d \n", value);
	} else if ((lcdonoff == 1) && (value == 0)) {
		//lcd_power_off();
		cmc623_suspend(NULL);
		pr_info("[lcd_power off] <= value : %d \n", value);
	} else
		pr_info("[lcd_power] lcd is already = %d \n", lcdonoff);

	return size;
}



static DEVICE_ATTR(lcd_power, 0664, lcd_power_file_cmd_show, lcd_power_file_cmd_store);


#if 0 
static const char lcdtype_name[][64] = {
		"SEC_LTN101AL01",
		};
#endif

static ssize_t lcdtype_file_cmd_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	pr_info("type: %s\n",lcdtype_name[0]);
	
	return sprintf(buf,lcdtype_name[0]);
}

static ssize_t lcdtype_file_cmd_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	return size;
}

static DEVICE_ATTR(lcdtype, 0664, lcdtype_file_cmd_show, lcdtype_file_cmd_store);




int __cmc623_count_tune_value(struct Cmc623RegisterSet *value)
{
    int ret = 0;
    int count = 500;
    while(count) {
        if((value[ret].RegAddr == 0xffff) && (value[ret].Data) == 0xffff) {
            return ret;
        }
        count--;
        ret++;
    }

    printk("[CMC623:ERROR]:%s:wrong tune count \n");
    return 0;
}

int __cmc623_set_tune_value(struct Cmc623RegisterSet *value)
{
    int ret = 0; 
    unsigned int num;
	unsigned int i = 0;

    mutex_lock(&tuning_mutex);    

    num = __cmc623_count_tune_value(value);    

    //printk("[CMC623:Info] : %s called, num : %d\n",__func__,num);

	for (i = 0; i < num; i++) {
        //printk("RegAddr : %x, value : %x\n",value[i].RegAddr,value[i].Data);
        ret = cmc623_I2cWrite16(value[i].RegAddr,value[i].Data);
        if (ret != 0) {
            //printk("[CMC623:ERROR]:%s:cmc623_I2cWrite16 failed : %d\n",ret);
            goto set_error;
        }
    }
    
set_error:     
    mutex_unlock(&tuning_mutex);
    return ret;
}

//region mode added
#if defined(CONFIG_TARGET_LOCALE_KOR)
void cmc623_Set_Region_Ext(int enable, int hStart, int hEnd, int vStart, int vEnd)
{
	u16 data=0;

	mutex_lock(&tuning_mutex);  

	cmc623_I2cWrite16(0x0000,0x0000);
	cmc623_I2cRead16(0x0001, &data);

	data &= 0x00ff;

	if(enable)
	{
//		cmc623_I2cWrite16_Region(0x0001,0x0300 | data);
		cmc623_I2cWrite16(0x0001,0x0700 | data);
		
		cmc623_I2cWrite16(0x0002,hStart);
		cmc623_I2cWrite16(0x0003,hEnd);
		cmc623_I2cWrite16(0x0004,vStart);
		cmc623_I2cWrite16(0x0005,vEnd);
	}
	else
	{
		cmc623_I2cWrite16(0x0001,0x0000 | data);
	}
	cmc623_I2cWrite16(0x0028,0x0000);

	mutex_unlock(&tuning_mutex);
	
	cmc623_current_region_enable = enable;
}
EXPORT_SYMBOL(cmc623_Set_Region_Ext);

#endif


#if 0 
static int set_mdnie_scenario_mode(unsigned int mode)
{
    printk("[CMC623:Info] : %s : %d\n",__func__, mode);
    mutex_lock(&tuning_mutex);
    switch(mode) { 
        case mDNIe_UI_MODE: 
            cabc_onoff_ctrl(cmc623_state.cabc_mode);
            break;     
        case mDNIe_CAMERA_MODE : 
            __cmc623_set_tune_value(cmc623_camera_cabcoff, ARRAY_SIZE(cmc623_camera_cabcoff));
            printk("[CMC623:Info]:camera cabc-off mode\n");
            break;
        case mDNIe_VIDEO_MODE : 
            if (cmc623_state.cabc_mode == CABC_ON_MODE) {
                __cmc623_set_tune_value(cmc623_video_cabc_on, ARRAY_SIZE(cmc623_video_cabc_on));    
                printk("[CMC623:Info]:video cabc-on mode\n");
            }
            else {
                __cmc623_set_tune_value(cmc623_video_cabc_off, ARRAY_SIZE(cmc623_video_cabc_off));
                printk("[CMC623:Info]:video cabc-off mode\n");
            }
            break; 
#if 0             
        case mDNIe_VIDEO_WARM_MODE :
            __cmc623_set_tune_value(cmc623_video_warm_mode, ARRAY_SIZE(cmc623_warm_video_mode));
            set_backlight_pwm(cmc623_state.brightness);
            break; 
        case mDNIe_VIDEO_COLD_MODE :   
            __cmc623_set_tune_value(cmc623_video_cold_mode, ARRAY_SIZE(cmc623_video_cold_mode));
            set_backlight_pwm(cmc623_state.brightness);
            break; 
#endif             
        case mDNIe_GALLERY_MODE :
            if (cmc623_state.cabc_mode == CABC_ON_MODE) {
                __cmc623_set_tune_value(cmc623_gallery_cabc_on, ARRAY_SIZE(cmc623_gallery_cabc_on));
                printk("[CMC623:Info]:gallery cabc-on mode\n");
            }
            else {
                __cmc623_set_tune_value(cmc623_gallery_cabc_off, ARRAY_SIZE(cmc623_gallery_cabc_off));    
                printk("[CMC623:Info]:gallery cabc-off mode\n");
            }
            break;
        default : 
            printk("[CMC623:ERROR] : Undefined Scenario Value : %d\n",mode);
            mutex_unlock(&tuning_mutex);
            return -1; 
    }
    cmc623_state.scenario = mode; 
    set_backlight_pwm(cmc623_state.brightness);
    mutex_unlock(&tuning_mutex);
    return 0; 

    
}
#endif

static ssize_t mdnie_scenario_show(struct device *dev,
        struct device_attribute *attr, char *buf)

{
    printk("[CMC623:info] : %s called \n",__func__);
    return sprintf(buf,"Current Scenario Mode : %d\n",cmc623_state.scenario);
}


static ssize_t mdnie_scenario_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t size)
{
    int ret;
    int value;
   
	sscanf(buf, "%d", &value);
    //printk("[CMC623:INFO] set scenario mode : %d\n",value);

    if(value < mDNIe_UI_MODE || value >= MAX_mDNIe_MODE){
        printk("[CMC623:ERROR] : wrong scenario mode value : %d\n",value);
        return size;
    }

    if (lcdonoff == FALSE) {
        cmc623_state.scenario = value;
        return size;
    }

    ret = apply_main_tune_value(value, cmc623_state.background, cmc623_state.cabc_mode, 0);
    if (ret != 0) {
        printk("[CMC623:ERROR] ERROR : set main tune value faild \n");
    }

    return size;

#if 0 
    //requestValue = atoi(buf);
    sscanf(buf,"%d",&requestValue);
    printk("[CMC623:info] : %s called %s : Request Value : %d\n",__func__,buf, requestValue);

    if (lcdonoff == FALSE) {
        cmc623_state.scenario = requestValue;
    }
    else { 
#if 0        
        ret = set_mdnie_scenario_mode(requestValue);     
        if (ret != 0) {
            printk("[CMC623:Error]:set_mdnie_scenario_mode() failed \n");
            return size; 
        }
#endif        
    }
    return size;
#endif     
}

static DEVICE_ATTR(mdnie_scenario, 0664, mdnie_scenario_show, mdnie_scenario_store);


#ifdef ENABLE_CMC623_TUNING
static int parse_text(char *src, int len)
{
	int i,count, ret;
	int index=0;
	char *str_line[CMC623_MAX_SETTINGS];
	char *sstart;
	char *c;
	unsigned int data1, data2;

	c = src;
	count = 0;
	sstart = c;

	for(i=0; i<len; i++,c++){
		char a = *c;
		if(a=='\r' || a=='\n'){
			if(c > sstart){
				str_line[count] = sstart;
				count++;
			}
			*c='\0';
			sstart = c+1;
		}
	}

	if(c > sstart){
		str_line[count] = sstart;
		count++;
	}


	for(i=0; i<count; i++){
		//printk("line:%d, [start]%s[end]\n", i, str_line[i]);
		ret = sscanf(str_line[i], "0x%x,0x%x\n", &data1, &data2);
		//printk("Result => [0x%2x 0x%4x] %s\n", data1, data2, (ret==2)?"Ok":"Not available");
		if(ret == 2) {   
			Cmc623_TuneSeq[index].RegAddr = (unsigned char)data1;
			Cmc623_TuneSeq[index++].Data  = (unsigned long)data2;
		}
	}
	return index;
}


bool cmc623_tune(unsigned long num) 
{
	unsigned int i;

    printk("[CMC623:INFO] Start tuning CMC623\n");

	for (i=0; i<num; i++) {
		//printk("[%2d] Writing => reg: 0x%2x, data: 0x%4lx\n", i+1, Cmc623_TuneSeq[i].RegAddr, Cmc623_TuneSeq[i].Data);
        printk("[CMC623:Tuning][%2d] : reg : 0x%2x, data: 0x%4lx\n",i+1, Cmc623_TuneSeq[i].RegAddr, Cmc623_TuneSeq[i].Data);

		if (cmc623_I2cWrite16(Cmc623_TuneSeq[i].RegAddr, Cmc623_TuneSeq[i].Data) != 0) {
			printk("[CMC623:ERROR] : I2CWrite failed\n");
			return 0;
		}
        printk("[CMC623:Tunig] : Write Done\n");        

		if (Cmc623_TuneSeq[i].RegAddr == CMC623_REG_SWRESET && Cmc623_TuneSeq[i].Data == 0xffff ){
			mdelay(3);
		}
	}
	printk("[CMC623:INFO] End tuning CMC623\n");
	return 1;
}

static int load_tuning_data(char *filename)
{
    struct file *filp;
	char	*dp;
	long	l, i ;
	loff_t  pos;
	int     ret, num;
	mm_segment_t fs;

	printk("[CMC623:INFO]:%s called loading file name : %s\n",__func__,filename);

	fs = get_fs();
	set_fs(get_ds());

	filp = filp_open(filename, O_RDONLY, 0);
	if(IS_ERR(filp)) 
	{
		printk("[CMC623:ERROR]:File open failed\n");
		return -1;
	}

	l = filp->f_path.dentry->d_inode->i_size;
	printk("[CMC623:INFO]: Loading File Size : %ld(bytes)", l);

	dp = kmalloc(l+10, GFP_KERNEL);	
	if(dp == NULL){
		printk("[CMC623:ERROR]:Can't not alloc memory for tuning file load\n");
		filp_close(filp, current->files);
		return -1;
	}
	pos = 0;
	memset(dp, 0, l);
	printk("[CMC623:INFO] : before vfs_read()\n");
	ret = vfs_read(filp, (char __user *)dp, l, &pos);   
	printk("[CMC623:INFO] : after vfs_read()\n");

	if(ret != l) {
        printk("[CMC623:ERROR] : vfs_read() filed ret : %d\n",ret);
        kfree(dp);
		filp_close(filp, current->files);
		return -1;
	}

	filp_close(filp, current->files);

	set_fs(fs);
	num = parse_text(dp, l);

	if(!num) {
		printk("[CMC623:ERROR]:Nothing to parse\n");
		kfree(dp);
		return -1;
	}

	printk("[CMC623:INFO] : Loading Tuning Value's Count : %d", num);
	cmc623_tune(num);


	kfree(dp);
	return num;
}


static ssize_t tuning_show(struct device *dev,
        struct device_attribute *attr, char *buf)

{
    int ret = 0;
    ret = sprintf(buf,"Tunned File Name : %s\n",tuning_filename);

    return ret;
}


static ssize_t tuning_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t size)
{

    memset(tuning_filename,0,sizeof(tuning_filename));
    sprintf(tuning_filename,"%s%s",TUNING_FILE_PATH,buf);
    
    printk("[CMC623:INFO]:%s:%s\n",__func__,tuning_filename);

    if (load_tuning_data(tuning_filename) <= 0) {
        printk("[CMC623:ERROR]:load_tunig_data() failed\n");
        return size;
    }
    tuning_enable = 1;
    return size;
}

static DEVICE_ATTR(tuning, 0664, tuning_show, tuning_store);
#endif 


static ssize_t mdnie_ove_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    int ret = 0;

    return sprintf(buf,"Current OVE Value : %s\n",(cmc623_state.ove==0) ? "Disabled" : "Enabled");
}


static ssize_t mdnie_ove_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t size)
{
    int ret;
    int value;
    
	sscanf(buf, "%d", &value);
    printk("[CMC623:INFO] set ove : %d\n",value);

    if(value < OUTDOOR_OFF_MODE || value >= MAX_OUTDOOR_MODE) {
        printk("[CMC623:ERROR] : wrong ove mode value : %d\n",value);
        return size;
    }

    if (lcdonoff == FALSE) {
        cmc623_state.ove = value;
        return size;
    }
    
    ret = apply_sub_tune_value(cmc623_state.temperature, value, cmc623_state.cabc_mode, 0);
    if (ret != 0) {
        printk("[CMC623:ERROR] ERROR : set sub tune value faild \n");
    }
    //cmc623_state.ove = value;
    //printk("[CMC623:INFO] Current Sub tunning : %s\n",sub_tune_value[cmc623_state.temperature][cmc623_state.ove].value[cmc623_state.cabc_mode].name);
    
    return size;
}

static DEVICE_ATTR(mdnie_ove, 0664, mdnie_ove_show, mdnie_ove_store);


static ssize_t mdnie_temp_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    const char temp_name[MAX_TEMP_MODE][16] = {
        "STANDARD",
        "WARM",
        "COLD",
    };

    if(cmc623_state.temperature >= MAX_TEMP_MODE) {
        printk("[CMC623:ERROR] : wrong color temperature mode value : %d\n",cmc623_state.temperature);
        return 0; 
    }   

    return sprintf(buf,"Current Color Temperature Mode : %s\n",temp_name[cmc623_state.temperature]);
}


static ssize_t mdnie_temp_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t size)
{
    int ret;
    int value;
	
	sscanf(buf, "%d", &value);
    printk("[CMC623:INFO] set color temperature : %d\n",value);
    
    if(value < TEMP_STANDARD ||value >= MAX_TEMP_MODE){
        printk("[CMC623:ERROR] : wrong color temperature mode value : %d\n",value);
        return size;
    }

    if (lcdonoff == FALSE) {
        cmc623_state.temperature = value;
        return size;
    }

    ret = apply_sub_tune_value(value, cmc623_state.ove, cmc623_state.cabc_mode ,0);
    if (ret != 0) {
        printk("[CMC623:ERROR] ERROR : set sub tune value faild \n");
    }

#if 0     
    cmc623_state.temperature = value;

    printk("[CMC623:INFO] Current Sub tunning : %s\n",
        sub_tuning_value[cmc623_state.temperature][cmc623_state.ove].tuning[cmc623_state.cabc_mode].name);
#endif     
    return size;
}

static DEVICE_ATTR(mdnie_temp, 0664, mdnie_temp_show, mdnie_temp_store);


//region mode added
#if defined(CONFIG_TARGET_LOCALE_KOR)
static ssize_t mdnie_roi_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	printk("called %s \n",__func__);

	return sprintf(buf,"%u\n",0);
}

static ssize_t mdnie_roi_show_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t size)
{
	int value,x1,y1,x2,y2;

	printk(KERN_INFO "[mdnie] %s called\n", __func__);
	
 	sscanf(buf, "%d%d%d%d%d", &value,&x1,&x2,&y1,&y2);

	printk(KERN_INFO "[mdnie] region:%d  x:%d~%d y:%d~%d\n", value, x1, x2, y1, y2);

	cmc623_Set_Region_Ext(value,x1,x2,y1,y2);
			
	return size;
}

static DEVICE_ATTR(mdnie_roi,0664, mdnie_roi_show, mdnie_roi_show_store);

#endif

static ssize_t mdnie_bg_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    const char background_name[MAX_BACKGROUND_MODE][16] = {
        "STANDARD",
        "DYNAMIC",
        "MOVIE",
    };

    if(cmc623_state.background >= MAX_BACKGROUND_MODE) {
        printk("[CMC623:ERROR] : Undefined Background Mode : %d\n",cmc623_state.background);
        return 0; 
    }   

    return sprintf(buf,"Current Background Mode : %s\n",background_name[cmc623_state.background]);
}


static ssize_t mdnie_bg_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t size)
{
    int ret;
    int value;
	
	sscanf(buf, "%d", &value);
    printk("[CMC623:INFO] set background mode : %d\n",value);
    
    if(value < DYNAMIC_MODE || value >= MAX_BACKGROUND_MODE){
        printk("[CMC623:ERROR] : wrong backgound mode value : %d\n",value);
        return size;
    }

    if (lcdonoff == FALSE) {
        cmc623_state.background = value;
        return size;
    }

    ret = apply_main_tune_value(cmc623_state.scenario, value, cmc623_state.cabc_mode, 0);
    if (ret != 0) {
        printk("[CMC623:ERROR] ERROR : set main tune value faild \n");
    }

    return size;
}

static DEVICE_ATTR(mdnie_bg, 0664, mdnie_bg_show, mdnie_bg_store);


#ifdef ENABLE_LCD_TYPE_ADC

#define LCD_TYPE_ADC_CHANNEL    0


static ssize_t lcd_type_adc_show(struct device *dev,
        struct device_attribute *attr, char *buf)

{
    int ret = 0;
    unsigned int value;

    if (tps6586x_adc_read(&value,LCD_TYPE_ADC_CHANNEL) != 0){
        printk("[CMC623:ERROR]:tps6586x_adc_read() failed \n");
        return ret;
    }

    ret = sprintf(buf,"LCD TYPE ADC VALUE : %dmV\n",value);

    return ret;
}

#if 0 
static ssize_t lcd_type_adc_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t size)
{

    memset(tuning_filename,0,sizeof(tuning_filename));
    sprintf(tuning_filename,"%s%s",TUNING_FILE_PATH,buf);
    
    printk("[CMC623:INFO]:%s:%s\n",__func__,tuning_filename);

    if (load_tuning_data(tuning_filename) <= 0) {
        printk("[CMC623:ERROR]:load_tunig_data() failed\n");
        return size;
    }
    tuning_enable = 1;
    return size;
}

#endif

static DEVICE_ATTR(lcd_type_adc, 0444, lcd_type_adc_show, NULL);

static void lcd_adc_work_handler(struct work_struct *unused)
{
    unsigned int adc_value;
   
    if (tps6586x_adc_read(&adc_value,LCD_TYPE_ADC_CHANNEL) != 0){
        printk("[CMC623:ERROR]:tps6586x_adc_read() failed \n");
        return;
    }
    printk("[LCD TYPE ADC] adc value : %dmV\n",adc_value);
}



static void lcd_type_timeout(unsigned long ptr)
{
    queue_work(lcd_adc_workqueue,&lcd_adc_work);    
    adc_timer.expires = jiffies + HZ;
    add_timer(&adc_timer);
}

#endif


static void bypass_onoff_ctrl(int value)
{
    int i = 0;
    int num;
    
	if (value == 1) {
        num = ARRAY_SIZE(cmc623_bypass);
		for (i = 0; i < num; i++) {
			cmc623_I2cWrite16(cmc623_bypass[i].RegAddr, cmc623_bypass[i].Data);
		}
		set_backlight_pwm(cmc623_state.brightness);
        printk("[CMC623:INFO]: CMC623 Bypass mode Enabled\n");
	} 
    else {
		cabc_onoff_ctrl(cmc623_state.cabc_mode);
		set_backlight_pwm(cmc623_state.brightness);
		printk("[CMC623:INFO]: CMC623 Bypass mode Disabled\n");
	}
}

static void camera_tuning_hander(struct work_struct *unused)
{
    printk("[CMC623:INFO] : %s called \n",__func__);
    
    __cmc623_resume(NULL);
    return ;
}


static void lcd_bl_handler(struct work_struct *unused)
{
    printk("%s called\n",__func__);
    if (lcdonoff == TRUE)
        gpio_set_value(cmc623_state.gpio.bl_reset, GPIO_LEVEL_HIGH);
    else {
        printk("[CMC623:INFO] : bl_reset : HIGH ignored\n");
    }
    lcd_bl_workqueue_statue = 0;
}


static int apply_main_tune_value(enum eLcd_mDNIe_UI ui, enum eBackground_Mode bg, enum eCabc_Mode cabc, int force)
{
    enum eCurrent_Temp temp = 0;
    int brightness_update = 1;
    struct Cmc623RegisterSet *value = NULL;

#ifdef ENABLE_CMC623_TUNING
    if(tuning_enable){
        printk("[CMC623:INFO]:%s:Tuning mode Enabled\n",__func__);
        return 0;
    }
#endif
    if (force == 0) {
        if((cmc623_state.scenario == ui) && (cmc623_state.background == bg) && (cmc623_state.cabc_mode == cabc)) {
            printk("[CMC623:INFO]:%s:already setted ui : %d, bg : %d\n",__func__ ,ui ,bg);
            return 0; 
        }
    }
    printk("[CMC623:INFO]:%s:curr main tune : %s\n",__func__, tune_value[cmc623_state.background][cmc623_state.scenario].value[cmc623_state.cabc_mode].name);
    printk("[CMC623:INFO]:%s: main tune : %s\n",__func__,tune_value[bg][ui].value[cabc].name);

    if ((ui == mDNIe_VIDEO_WARM_MODE) || (ui == mDNIe_VIDEO_COLD_MODE)) {
        if ((cmc623_state.scenario == mDNIe_VIDEO_MODE) || (cmc623_state.scenario == mDNIe_DMB_MODE)) {
            if (ui == mDNIe_VIDEO_WARM_MODE) {
                temp = TEMP_WARM;
            }
            else {
                temp = TEMP_COLD;
            }
            if (apply_sub_tune_value(temp, cmc623_state.ove, cabc, 0) != 0){
                printk("[CMC623:ERROR]:%s:apply_sub_tune_value() faield \n");
            }
            goto rest_set;   
        }
    }

//#ifdef FEATURE_ANRD_KOR
    if ((ui == mDNIe_DMB_WARM_MODE) || (ui == mDNIe_DMB_COLD_MODE)) {
        if (cmc623_state.scenario == mDNIe_DMB_MODE) {
            if (ui == mDNIe_DMB_WARM_MODE) {
                temp = TEMP_WARM;
            }
            else {
                temp = TEMP_COLD;
            }
            if (apply_sub_tune_value(temp, cmc623_state.ove, cabc, 0) != 0){
                printk("[CMC623:ERROR]:%s:apply_sub_tune_value() faield \n");
            }
            goto rest_set;   
        }
    }
//#endif

    value = tune_value[bg][ui].value[cabc].tune;
    if(value == NULL) {
        printk("[CMC623:ERROR]:%s:can't found tuning value \n");
        return -1;
    }

#if 0 
    if(ui == mDNIe_CAMERA_MODE) {
        if((camera_workqueue != NULL) && (camera_resume_flag == 0)) {
            cmc623_suspend(NULL);
            queue_work(camera_workqueue, &camera_tuning_work);
            camera_value = value;
            goto rest_set;
        }
    }
    camera_value = NULL;
#endif    
    if (__cmc623_set_tune_value(value) != 0){
        printk("[CMC623:ERROR]:%s: set tune value falied \n");
        return -1; 
    }

#if 0 
    if ((cmc623_state.main_tune != NULL) && (cmc623_state.main_tune->plut != tune_value[bg][ui].value[cabc].plut)){
        brightness_update = 1; 
        goto rest_set;
    }

    if(cmc623_state.cabc_mode != cabc) {
        brightness_update = 1; 
        goto rest_set;
    }

     if ((cmc623_state.main_tune != NULL) && (cmc623_state.main_tune->flag != tune_value[bg][ui].value[cabc].flag)){
         brightness_update = 1; 
         goto rest_set;
     }
#endif
     

     if ((ui == mDNIe_VIDEO_WARM_MODE) || (ui == mDNIe_VIDEO_COLD_MODE) || (ui == mDNIe_VIDEO_MODE)|| (ui == mDNIe_DMB_MODE)) {
        
        if (ui == mDNIe_VIDEO_WARM_MODE) {
                temp = TEMP_WARM;
            }
        else if (ui == mDNIe_VIDEO_COLD_MODE){
            temp = TEMP_COLD;
        }
        else {
            temp = TEMP_STANDARD;
        }
        if (apply_sub_tune_value(temp, cmc623_state.ove, cabc, 0) != 0){
            printk("[CMC623:ERROR]:%s:apply_sub_tune_value() faield \n");
        }
     }       
        
//#ifdef FEATURE_ANRD_KOR
     if ((ui == mDNIe_DMB_WARM_MODE) || (ui == mDNIe_DMB_COLD_MODE)) {
        
        if (ui == mDNIe_DMB_WARM_MODE) {
                temp = TEMP_WARM;
            }
        else {
            temp = TEMP_COLD;
        }
        if (apply_sub_tune_value(temp, cmc623_state.ove, cabc, 0) != 0){
            printk("[CMC623:ERROR]:%s:apply_sub_tune_value() faield \n");
        }
     } 
//#endif
        
rest_set:    
    
    cmc623_state.scenario = ui;
    cmc623_state.background = bg;
    cmc623_state.cabc_mode = cabc;
    cmc623_state.main_tune = &tune_value[bg][ui].value[cabc];

    if(ui == mDNIe_UI_MODE) {
        cmc623_state.temperature = TEMP_STANDARD;
        cmc623_state.ove = OUTDOOR_OFF_MODE;
    }

    if (ui == mDNIe_VIDEO_WARM_MODE) 
        cmc623_state.temperature = TEMP_WARM;
    else if (ui == mDNIe_VIDEO_COLD_MODE)
        cmc623_state.temperature = TEMP_COLD;

//#ifdef FEATURE_ANRD_KOR
    if (ui == mDNIe_DMB_WARM_MODE) 
        cmc623_state.temperature = TEMP_WARM;
    else if (ui == mDNIe_DMB_COLD_MODE)
        cmc623_state.temperature = TEMP_COLD;
//#endif

#if 1 
    if (brightness_update){
        //set_backlight_pwm(cmc623_state.brightness);
        __pwm_brightness(cmc623_state.brightness);
    } 
#else
        __pwm_brightness(cmc623_state.brightness);
#endif
    return 0;
}
 

static int apply_sub_tune_value(enum eCurrent_Temp temp, enum eOutdoor_Mode ove, enum eCabc_Mode cabc, int force)
{

    //unsigned int num = 0;
    struct Cmc623RegisterSet *value = NULL;

#ifdef ENABLE_CMC623_TUNING
    if(tuning_enable){
        printk("[CMC623:INFO]:%s:Tuning mode Enabled\n",__func__);
        return 0;
    }
#endif
    if (force == 0) {
        if((cmc623_state.temperature == temp) && (cmc623_state.ove == ove)) {
            printk("[CMC623:INFO]:%s:already setted temp : %d, over : %d\n",__func__ ,temp ,ove);
            return 0; 
        }
    }
    printk("[CMC623:INFO]:%s:curr sub tune : %s\n",__func__,sub_tune_value[cmc623_state.temperature][cmc623_state.ove].value[cmc623_state.cabc_mode].name);
    printk("[CMC623:INFO]:%s: sub tune : %s\n",__func__,sub_tune_value[temp][ove].value[cabc].name);

    if((ove == OUTDOOR_OFF_MODE) || (temp == TEMP_STANDARD)) {
        printk("[CMC623:INFO]:%s:set default main tune\n",__func__);
        value = tune_value[cmc623_state.background][cmc623_state.scenario].value[cmc623_state.cabc_mode].tune;
        if (value == NULL) {
            printk("[CMC623:ERROR]:%s:can't found main tuning value \n",__func__);
            return -1; 
        }

        if (__cmc623_set_tune_value(value) != 0){
            printk("[CMC623:ERROR]:%s: set tune value falied \n");
            return -1; 
        }
        
        if((ove != OUTDOOR_OFF_MODE) || (temp != TEMP_STANDARD)) {
            goto set_sub_tune;
        }
   
    }
    else {
set_sub_tune :        
        value = sub_tune_value[temp][ove].value[cabc].value;
        if(value == NULL) {
            printk("[CMC623:ERROR]:%s:can't found sub tuning value \n",__func__);
            return -1;
        }
    
        if (__cmc623_set_tune_value(value) != 0){
            printk("[CMC623:ERROR]:%s: set tune value falied \n",__func__);
            return -1; 
        }
    }
    cmc623_state.temperature = temp; 
    cmc623_state.ove = ove;
    return 0;
}



#ifdef BYPASS_ONOFF_TEST
static void cmc623_reinit()
{
	/* FAILSAFEB <= HIGH*/            
	gpio_set_value(cmc623_state.gpio.ima_pwren , GPIO_LEVEL_HIGH);
	msleep(1);

	/*BYPASSB <= HIGH*/
	gpio_set_value(cmc623_state.gpio.ima_bypass, GPIO_LEVEL_HIGH);
	msleep(1);

	/* SLEEPB <= HIGH*/
	gpio_set_value(cmc623_state.gpio.ima_sleep, GPIO_LEVEL_HIGH);
	msleep(1);

	/* RESETB <= LOW*/
	gpio_set_value(cmc623_state.gpio.ima_n_rst, GPIO_LEVEL_LOW);

	/*wait 4ms or above*/
	msleep(5);

	/* RESETB <= HIGH*/
	gpio_set_value(cmc623_state.gpio.ima_n_rst, GPIO_LEVEL_HIGH);

	/* wait 0.3ms or above*/
	msleep(1);
	
	cmc623_state.suspended = FALSE;

	// set registers using I2C
	if(!p_cmc623_data)
		pr_err("%s cmc623 is not initialized\n", __func__);
	
	__cmc623_set_init();
	
	lcdonoff = TRUE;
}



static ssize_t bypass_onoff_file_cmd_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	pr_info("called %s \n",__func__);

	return sprintf(buf,"%u\n",current_bypass_onoff);
}

static ssize_t bypass_onoff_file_cmd_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t size)
{
	int value;
	
	sscanf(buf, "%d", &value);

	pr_info("[bypass set] in bypass_onoff_file_cmd_store, input value = %d \n",value);

	if((cmc623_state.suspended == FALSE) && (lcdonoff == TRUE)) {
		if((current_bypass_onoff==0) && (value == 1)) {
			bypass_onoff_ctrl(value);
			current_bypass_onoff = 1;
		} else if((current_bypass_onoff==1) &&(value == 0)) {
			bypass_onoff_ctrl(value);
			current_bypass_onoff = 0;
		} else
			pr_info("[bypass set] bypass is already = %d \n",current_bypass_onoff);
	} else
		pr_info("[bypass set] LCD is suspend = %d \n",cmc623_state.suspended);

	return size;
}

static DEVICE_ATTR(bypassonoff, 0664, bypass_onoff_file_cmd_show, bypass_onoff_file_cmd_store);
#endif

static int cmc623_i2c_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct cmc623_data *data;

    printk("[CMC623:INFO] : + %s called\n",__func__);
	data = kzalloc(sizeof(struct cmc623_data), GFP_KERNEL);
	if (!data) {
		pr_err("[CMC623:%s:ERROR] : kmalloc for cmc623_data error\n",__func__);
		return -ENOMEM;
	}

	data->client = client;
	i2c_set_clientdata(client, data);

	p_cmc623_data = data;

	panel_gpio_init();
	cmc623_gpio_init();

#ifdef CONFIG_HAS_EARLYSUSPEND

#ifndef ENABLE_LCD_TYPE_ADC
	cmc623_early_suspend.level =  EARLY_SUSPEND_LEVEL_DISABLE_FB - 1;
	cmc623_early_suspend.suspend = cmc623_suspend;
	cmc623_early_suspend.resume = cmc623_resume;
	register_early_suspend(&cmc623_early_suspend);
#endif
#endif	/* CONFIG_HAS_EARLYSUSPEND */

    printk("[CMC623:INFO] : + %s called\n",__func__);
	return 0;
}

static int __devexit cmc623_i2c_remove(struct i2c_client *client)
{
	struct cmc623_data *data = i2c_get_clientdata(client);

	p_cmc623_data = NULL;

	i2c_set_clientdata(client, NULL);

	kfree(data);

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&cmc623_early_suspend);
#endif	/* CONFIG_HAS_EARLYSUSPEND */

	dev_info(&client->dev, "cmc623 i2c remove success!!!\n");

	return 0;
}

static const struct i2c_device_id cmc623[] = {
	{ "image_convertor", 0 },
};
MODULE_DEVICE_TABLE(i2c, cmc623);

struct i2c_driver cmc623_i2c_driver = {
	.driver	= {
	.name	= "image_convertor",
	.owner = THIS_MODULE,
	},
	.probe		= cmc623_i2c_probe,
	.remove		= __devexit_p(cmc623_i2c_remove),
	.id_table	= cmc623,
#if !(defined CONFIG_HAS_EARLYSUSPEND)
	.suspend = NULL,
	.resume  = NULL,
#endif
	.shutdown = cmc623_shutdown,
};



static int alloc_cmc623_gpio(void)
{
    printk("[CMC623:INFO]:%s called\n",__func__);
    printk("[CMC623:INFO]:%s systemrev : %x\n",system_rev);

    cmc623_state.gpio.ima_sleep = 0;
    cmc623_state.gpio.ima_bypass = 0;
    cmc623_state.gpio.ima_pwren = 0;
    cmc623_state.gpio.lvds_n_shdn = 0;
    cmc623_state.gpio.mlcd_on1 = 0;
    cmc623_state.gpio.ima_n_rst = 0;
    cmc623_state.gpio.bl_reset = 0;
    cmc623_state.gpio.mlcd_on = 0;

    
    cmc623_state.gpio.ima_sleep = GPIO_IMA_SLEEP;
    cmc623_state.gpio.ima_bypass = GPIO_IMA_BYPASS;
    cmc623_state.gpio.ima_pwren = GPIO_IMA_PWREN;
    cmc623_state.gpio.lvds_n_shdn = GPIO_LVDS_N_SHDN;
    cmc623_state.gpio.mlcd_on1 = GPIO_MLCD_ON1;
    cmc623_state.gpio.ima_n_rst = GPIO_IMA_N_RST;
    
#if defined(CONFIG_MACH_SAMSUNG_P4) || defined(CONFIG_MACH_SAMSUNG_P4WIFI)
    cmc623_state.gpio.bl_reset = GPIO_BL_RESET;
    cmc623_state.gpio.mlcd_on = GPIO_MLCD_ON;
#elif defined(CONFIG_MACH_SAMSUNG_P4LTE)
    cmc623_state.gpio.bl_reset = GPIO_BL_RESET;
    if(system_rev > 0x0A)
        cmc623_state.gpio.mlcd_on = GPIO_MLCD_ON;
    else 
        cmc623_state.gpio.mlcd_on = GPIO_MLCD_ON_REV05;
#elif defined(CONFIG_MACH_SAMSUNG_P5)  
    cmc623_state.gpio.bl_reset = GPIO_BL_RESET;
    cmc623_state.gpio.mlcd_on = GPIO_MLCD_ON;
    if (system_rev < 6)
        cmc623_state.gpio.mlcd_on = TEGRA_GPIO_PI4;
    if (system_rev < 9)
        cmc623_state.gpio.bl_reset = TEGRA_GPIO_PK4;
#endif

    if ((!cmc623_state.gpio.ima_sleep) || (!cmc623_state.gpio.ima_bypass) || (!cmc623_state.gpio.ima_pwren) || 
        (!cmc623_state.gpio.lvds_n_shdn) || (!cmc623_state.gpio.mlcd_on1) || (!cmc623_state.gpio.ima_n_rst) ||
        (!cmc623_state.gpio.bl_reset) || (!cmc623_state.gpio.mlcd_on)) {
         BUG_ON(1);
    }
    return 0;
    
}

//extern struct class *sec_class;
static struct class *mdni_class;
struct device *tune_cmc623_dev;

static int __devinit cmc623_probe(struct platform_device *pdev)
{
	int ret = 0;
	u16 data=0;

	printk("[CMC623:INFO] : + %s \n",__func__);

    alloc_cmc623_gpio();

    mdni_class = class_create(THIS_MODULE, "mdnie");
	if (IS_ERR(mdni_class))
		pr_err("Failed to create class(mdnie_class)!\n");

	tune_cmc623_dev = device_create(mdni_class, NULL, 0, NULL, "cmc623");
	if (IS_ERR(tune_cmc623_dev)) {
		pr_err("Failed to create device!");
		ret = -1;
	}
    
#ifdef BYPASS_ONOFF_TEST
	if (device_create_file(tune_cmc623_dev, &dev_attr_bypassonoff) < 0) {
		printk("Failed to create device file!(%s)!\n", dev_attr_bypassonoff.attr.name);
		ret = -1;
	}
#endif
	if (device_create_file(tune_cmc623_dev, &dev_attr_mdnie_cabc) < 0) {
		pr_err("Failed to create device file!(%s)!\n",
			dev_attr_mdnie_cabc.attr.name);
		ret = -1;
	}

	if (device_create_file(tune_cmc623_dev, &dev_attr_lcd_power) < 0) {
		pr_err("Failed to create device file!(%s)!\n", dev_attr_lcd_power.attr.name);
		ret = -1;
	}

	if (device_create_file(tune_cmc623_dev, &dev_attr_lcdtype) < 0) {
		pr_err("Failed to create device file!(%s)!\n", dev_attr_lcdtype.attr.name);
		ret = -1;
	}

    if (device_create_file(tune_cmc623_dev, &dev_attr_mdnie_scenario) < 0) {
        pr_err("Failed to create device file!(%s)!\n", dev_attr_mdnie_scenario.attr.name);
		ret = -1;
    }

//region mode added
#if defined(CONFIG_TARGET_LOCALE_KOR)
		if (device_create_file(tune_cmc623_dev, &dev_attr_mdnie_roi) < 0){
		pr_err("Failed to create device file!(%s)!\n", dev_attr_mdnie_roi.attr.name);
		 ret = -1;
		}
#endif

#ifdef ENABLE_CMC623_TUNING    
    if (device_create_file(tune_cmc623_dev, &dev_attr_tuning) < 0) {
        pr_err("Failed to create device file(%s)!\n",dev_attr_tuning.attr.name);
        ret = -1;
    }
#endif

    if (device_create_file(tune_cmc623_dev, &dev_attr_mdnie_ove) < 0) {
        printk("[CMC623:ERROR] device_crate_filed(%s) \n", dev_attr_mdnie_ove.attr.name);
        ret = -1;
    }

    if(device_create_file(tune_cmc623_dev, &dev_attr_mdnie_temp) < 0) {
        printk("[CMC623:ERROR] device_crate_filed(%s) \n", dev_attr_mdnie_temp.attr.name);
        ret = -1;
    }

    if(device_create_file(tune_cmc623_dev, &dev_attr_mdnie_bg) < 0) {
        printk("[CMC623:ERROR] device_crate_filed(%s) \n", dev_attr_mdnie_bg.attr.name);
        ret = -1;
    }

#ifdef ENABLE_LCD_TYPE_ADC
    if(device_create_file(tune_cmc623_dev, &dev_attr_lcd_type_adc) < 0) {
        printk("[CMC623:ERROR] device_crate_filed(%s) \n", dev_attr_lcd_type_adc.attr.name);
        ret = -1;
    }
    wake_lock_init(&adc_wake_lock, WAKE_LOCK_SUSPEND, "lcd_type_adc");
    wake_lock(&adc_wake_lock);

    lcd_adc_workqueue = create_singlethread_workqueue("lcd_adc");
    if(lcd_adc_workqueue == NULL) {
        printk("[CMC623:ERROR]:Can't not create workqueue for lcd adc\n");

    }
    else {
        init_timer(&adc_timer);
        adc_timer.function = lcd_type_timeout;
        adc_timer.data = NULL;
        adc_timer.expires = jiffies + (HZ*10);
        add_timer(&adc_timer);
    }
#endif    

	ret = i2c_add_driver(&cmc623_i2c_driver);

	cmc623_I2cRead16(0x1A, &data);
	if( data == 0x623F)
		cmc623_current_type = cmc623_type_fujitsu;	
	else
		cmc623_current_type = cmc623_type_lsi;

	printk("[CMC623:LCD_TYEP] LCD_TYEP(%d) \n", cmc623_current_type);

#ifdef __BYPASS_TEST_ENABLE
    bypass_onoff_ctrl(TRUE);
    goto rest_init;
#endif 
#if 0
    camera_workqueue = create_singlethread_workqueue("camera_tuning");
    if (camera_workqueue == NULL){
        printk("[CMC623:ERROR]:Can't not create workqueue for camera tuning\n");
    }
    init_timer(&camera_timer);
#endif

    lcd_bl_workqueue = create_singlethread_workqueue("lcd_bl");
    if(lcd_bl_workqueue == NULL) {
        printk("[CNC623:ERROR]:Can't not create workqueue for lcd_bl\n");
    }
    apply_main_tune_value(mDNIe_UI_MODE, STANDARD_MODE, CABC_ON_MODE, 1);
    //set_backlight_pwm(cmc623_state.brightness);

    //apply_sub_tune_value();

rest_init:
	cmc623_state.suspended = FALSE;
	lcdonoff = TRUE;

	printk("[CMC623:INFO] : - %s \n",__func__);

	return ret;
}


static int __devexit cmc623_remove(struct platform_device *pdev)
{
	i2c_del_driver(&cmc623_i2c_driver);

	return 0;
}

struct platform_driver sec_cmc623 =  {
	.driver = {
		.name = "sec_cmc623",
		.owner  = THIS_MODULE,
	},
	.probe  = cmc623_probe,
	.remove = cmc623_remove,
};

static int __init cmc623_init(void)
{
	//pr_info("**** < cmc623_init  > *****\n");

	return platform_driver_register(&sec_cmc623);
}

static void __exit cmc623_exit(void)
{
	platform_driver_unregister(&sec_cmc623);
}

module_init(cmc623_init);
module_exit(cmc623_exit);

/* Module information */
MODULE_AUTHOR("Samsung");
MODULE_DESCRIPTION("Tuning CMC623 image converter");
MODULE_LICENSE("GPL");


