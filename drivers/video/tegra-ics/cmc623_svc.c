/*
 * cmc623_svc.c
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
#include <asm/uaccess.h>
#include <linux/i2c.h>
#include <mach/gpio.h>
#include <linux/delay.h>
#include <linux/workqueue.h>

#include "cmc623.h"

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#if 0
#define dprintk(x...) printk(x)
#else
#define dprintk(x...) (0)
#endif

//#define CMC623_TUNING
//#define CMC623_TUNE_STATIC	// if you want bypass mode, don't enable this.

/* auto ove option is not maintained */
#undef __CMC623_AUTO_OVE_MODE__

#if defined(CMC623_TUNE_STATIC) && !defined(CMC623_TUNING)
#error "CMC623_TUNE_STATIC define is available in only tuning bunary"
#endif

#define END_SEQ		0xffff

#ifdef CMC623_TUNING
#define CMC623_PATH_TUNING_DATA3  "/sdcard/external_sd/p1/mdnie_tune"
#define CMC623_PATH_TUNING_DATA2  "/sdcard/external_sd/p1/1"
#define CMC623_PATH_TUNING_DATA   "/sdcard/cmc623_tune"

#define klogi(fmt, arg...)  printk(KERN_INFO "%s: " fmt "\n" , __func__, ## arg)
#define kloge(fmt, arg...)  printk(KERN_ERR "%s(%d): " fmt "\n" , __func__, __LINE__, ## arg)

#define CMC623_MAX_SETTINGS	 100
static struct Cmc623RegisterSet Cmc623_TuneSeq[CMC623_MAX_SETTINGS];
#endif

#ifdef CMC623_TUNE_STATIC
#define CMC623_PATH_STATIC_TUNING_DATA   "/sdcard/cmc623_tune_static"
static int brightness_calls = 0;
static int static_tune_loaded = 0;
#endif

#define DELIMITER 0xff

static const u8 all_regs_bank0[] = {
	0xb4, 0xb3, 0x10, 0x24, 0x0b, 0x12, 0x13, 0x14, 0x15, 
	0x16, 0x17, 0x18, 0x19, 0x0f, 0x0d, 0x0e, 0x22, 0x23, 0x49, 0x4a, 
	0x4b, 0x4d, 0xc8, 0xc9, 0x42, 0x6e, 0x6f, 0x70, 0x71, 
	0x76, 0x77, 0x78, 0x79, 0x7a, 0x28, 0x09, 0x26,
	DELIMITER,
	0x01,
	0x2c, 0x2d, 0x2e, 0x2f, 0x3a, 0x3b, 0x3c, 0x3f, 0x42, 
	DELIMITER,
	0x72, 0x73, 0x74, 0x75, 0x7c, 
	DELIMITER,
	0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3,
	DELIMITER,
};

static const u8 all_regs_bank1[] = {
	0x09, 0x0a, 0x0b, 0x0c, 0x01, 0x06, 0x07, 0x65, 0x68, 0x6c,
	0x6d, 0x6e, 
	DELIMITER,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31,
	0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
	DELIMITER,

};

#define I2C_M_WR 0 /* for i2c */
#define I2c_M_RD 1 /* for i2c */

#define SIG_MDNIE_UI_MODE		0
#define SIG_MDNIE_VIDEO_MODE		1
#define SIG_MDNIE_VIDEO_WARM_MODE	2
#define SIG_MDNIE_VIDEO_COLD_MODE	3
#define SIG_MDNIE_CAMERA_MODE		4
#define SIG_MDNIE_NAVI			5
#define SIG_MDNIE_DMB_MODE		6
#define SIG_MDNIE_VT_MODE		7
#define SIG_MDNIE_GALLERY_MODE		8

#define SIG_MDNIE_USERSELECT_DYNAMIC_MODE		0
#define SIG_MDNIE_USERSELECT_STANDARD_MODE		1
#define SIG_MDNIE_USERSELECT_MOVIE_MODE			2

/* Each client has this additional data */
struct cmc623_data {
	struct i2c_client *client;
};

static struct cmc623_data * p_cmc623_data = NULL;
#ifdef __CMC623_AUTO_OVE_MODE__
static struct workqueue_struct *ove_wq;
static struct work_struct work_ove;
#endif

struct cmc623_state_type{
	unsigned int cabc_enabled;
	unsigned int brightness;
	unsigned int suspended;
	int white;
	int black;
	int saturation;
	int power_lut_num;
	u16 saved_algo_sel;
};

struct cmc623_mode_desc{
	char * name;
	bool use_cabc;
	bool use_outdoor;
	bool use_coltemp;
	bool use_bgeffect;
	int power_lut_num;
};

struct cmc623_mode_info{
	int cabc;
	int outdoor;
	int color_temp;
	int bgeffect;
};

static struct cmc623_state_type cmc623_state = { 
#ifdef CMC623_TUNING
	.cabc_enabled = false,	//for tuning mode
#else
	.cabc_enabled = true,
#endif
	.brightness = 32,
	.suspended = false,
	.white = 0,
	.black = 0,
	.saturation = 0,
	.power_lut_num = 0,
};

struct mDNIe_data_type{
	u16 addr;
	u16 data;
};

typedef enum
{
	mDNIe_UI_MODE,
	mDNIe_VIDEO_MODE,
	mDNIe_VIDEO_WARM_MODE,
	mDNIe_VIDEO_COLD_MODE,
	mDNIe_CAMERA_MODE,
	mDNIe_NAVI,
	mDNIe_DMB_MODE,
	mDNIe_VT_MODE,
	mDNIe_GALLERY_MODE,
	mDNIe_DMB_WARM_MODE,
	mDNIe_DMB_COLD_MODE,
}Lcd_mDNIe_UI;

typedef enum
{
	mDNIe_USERSELECT_STANDARD_MODE = 0,
	mDNIe_USERSELECT_DYNAMIC_MODE = 1,
	mDNIe_USERSELECT_MOVIE_MODE = 2,
}Lcd_mDNIe_UserSelect;

typedef enum
{
	mode_type_CABC_none,
	mode_type_CABC_on,
	mode_type_CABC_off,
}mDNIe_mode_CABC_type;

#define COLOR_TEMP_NORMAL	0
#define COLOR_TEMP_WARM		1
#define COLOR_TEMP_COLD		2

static enum Lcd_CMC623_UI_mode current_cmc623_UI = CMC623_UI_MODE; // mDNIe Set Status Checking Value.
static enum Lcd_CMC623_USERSELECT_mode current_cmc623_Userselect = CMC623_USERSELECT_STANDARD_MODE; // mDNIe User Select mode Set Status Checking Value.
static int current_cmc623_OutDoor_OnOff = false;
static int current_cmc623_CABC_OnOff = false;

#define CMC_FLAG_SETTING_FIRST		0x00000001
#define CMC_FLAG_SETTING_BOOT		0x00000002
static int cmc623_bypass_mode = false;
static int current_autobrightness_enable = false;
static int cmc623_current_region_enable = false;

static DEFINE_MUTEX(cmc623_state_transaction_lock);

/*
static mDNIe_mode_CABC_type cmc623_cabc_mode[]=
{
	mode_type_CABC_none,		// UI
	mode_type_CABC_on,		// Video
	mode_type_CABC_on,		// Video warm
	mode_type_CABC_on,		// Video cold
	mode_type_CABC_off, 	// Camera
	mode_type_CABC_none,		// Navi
};*/

#include "cmc623_tune_value.h"	

#ifdef CMC623_TUNE_STATIC
static mDNIe_data_type cmc623_static_Tune[100];
#endif

#if 0
typedef enum
{
	LCD_TYPE_VA,
	LCD_TYPE_PLS,
	LCD_TYPE_VA50,
	LCD_TYPE_TN,
	LCD_TYPE_FFS,	
	LCD_TYPE_LCDPLS,	
	LCD_TYPE_T7,	
	LCD_TYPE_T8,	
	LCD_TYPE_MAX,
}Lcd_Type;
Lcd_Type lcd_type = 0;
#endif

#define NUM_ITEM_POWER_LUT	9
#define NUM_POWER_LUT	2

static unsigned char cmc623_Power_LUT[NUM_POWER_LUT][NUM_ITEM_POWER_LUT]={
	{ 0x42, 0x47, 0x3e, 0x52, 0x42, 0x3f, 0x3a, 0x37, 0x3f },
	{ 0x3a, 0x3f, 0x36, 0x4a, 0x3a, 0x37, 0x32, 0x2f, 0x37 },
};

static struct cmc623_mode_desc modes_desc[] = {
	/* name		cabc 	outdoor	temp 	bgeffect	power_lut_num */
	{ "UI",		true,	false,	false,	true,		0 },
	{ "Video",	true,	true,	true,	true,		1 },
	{ "Camera",	false,	true,	false,	false,		0 },
	{ "VT",		true,	false,	false,	true,		0 },
	{ "DMB",	true,	true,	true,	true,		0 },
	{ "Gallery",	true,	false,	false,	true,		0 },
};

static int cmc623_I2cRead16(u8 reg, u16 *val);
static bool cmc623_I2cWrite16(unsigned char Addr, unsigned long Data);
static void cmc623_cabc_pwm_brightness_reg(int value, int flag, int lut);
static void cmc623_manual_pwm_brightness_reg(int value);
static void cmc623_manual_pwm_brightness_reg_nosync(int value);

static unsigned long last_cmc623_Bank = 0xffff;
static unsigned long last_cmc623_Algorithm = 0xffff;

static void cmc623_reg_unmask(void)
{
	if(!p_cmc623_data)
	{
	printk(KERN_ERR "%s cmc623 is not initialized\n", __func__);
	return;
	}

	cmc623_I2cWrite16(0x28, 0x0000);
}

#if 0
static void cmc623_Color_White_Change(int value, int finalize)
{
	struct mDNIe_data_type *mode = cmc623_white_val_values[cmc623_state.cabc_enabled];
	mode += (value+4);
	cmc623_state.white = value;

#ifdef CMC623_TUNING
	printk(KERN_ERR "%s ignore for tuning mode\n", __func__);
	return;
#endif

	cmc623_I2cWrite16(0x0000, 0x0000);		//bank
	cmc623_I2cWrite16(mode->addr, mode->data);
	
	if(finalize == TRUE)
	{
		cmc623_reg_unmask();
	}
}

static void cmc623_Color_Black_Change(int value, int finalize)
{
	struct mDNIe_data_type *mode = (struct mDNIe_data_type*)cmc623_black_values[((value+4)*2)+cmc623_state.cabc_enabled];
	cmc623_state.black = value;

#ifdef CMC623_TUNING
	printk(KERN_ERR "%s ignore for tuning mode\n", __func__);
	return;
#endif

	while ( mode->addr != END_SEQ)
	{
		cmc623_I2cWrite16(mode->addr, mode->data);
		mode++;
	}
	
	if(finalize == TRUE)
	{
		cmc623_reg_unmask();
	}
}

static void cmc623_Color_Saturation_Change(int value, int finalize)
{
	struct mDNIe_data_type *mode = cmc623_saturation_val_values[cmc623_state.cabc_enabled];
	mode += (value+4);
	cmc623_state.saturation = value;

#ifdef CMC623_TUNING
	printk(KERN_ERR "%s ignore for tuning mode\n", __func__);
	return;
#endif

	cmc623_I2cWrite16(0x0000, 0x0000);		//bank
	cmc623_I2cWrite16(mode->addr, mode->data);
	
	if(finalize == TRUE)
	{
		cmc623_reg_unmask();
	}
}
#endif

#ifdef __CMC623_AUTO_OVE_MODE__
static int cmc623_OutDoor_Enable(int enable);
#endif

#if 0
static void cmc623_UserSelect_Mode_Change(struct mDNIe_data_type *mode)
{

	if(!p_cmc623_data)
		{
		printk(KERN_ERR "%s cmc623 is not initialized\n", __func__);
		return;
		}

	if(cmc623_bypass_mode)
		{
		printk(KERN_WARNING "%s ignore for bypass mode\n", __func__);
		return;
		}
#if 1 // yd.seo check this point		
	while ( mode->addr != END_SEQ)
	{
		cmc623_I2cWrite16(mode->addr, mode->data);
		//printk(KERN_INFO "[cmc623] a(0x%x),d(0x%x)\n",mode->addr, mode->data);	
		mode++;
	}
#endif

}

static void cmc623_Set_UserSelect_Mode(enum Lcd_CMC623_USERSELECT_mode mode, int cmc623_CABC_OnOff, int flag)
{
	int cabc_enable=0;
	int lut_num;

	if(!cmc623_CABC_OnOff)	
	{
		switch(mode)
		{
			case CMC623_USERSELECT_STANDARD_MODE:
				cmc623_UserSelect_Mode_Change((struct mDNIe_data_type*)cmc623_values[CMC_USERSELECT_STANDARD]);
			break;

			case CMC623_USERSELECT_MOVIE_MODE:
				cmc623_UserSelect_Mode_Change((struct mDNIe_data_type*)cmc623_values[CMC_USERSELECT_MOVIE]);
			break;
	
			case CMC623_USERSELECT_DYNAMIC_MODE:
				cmc623_UserSelect_Mode_Change((struct mDNIe_data_type*)cmc623_values[CMC_USERSELECT_DYNAMIC]);
			break;
		}
		printk("[cmc623] cmc623_Userselect_Mode: current_cmc623_Userselect(%d), current_cmc623_CABC_OnOff(%d)->(%d) / OVE : %d \n",current_cmc623_Userselect, current_cmc623_CABC_OnOff, cabc_enable,current_cmc623_OutDoor_OnOff);	
		//cmc623_flag_Userselect = TRUE;
		current_cmc623_Userselect = mode;
	}	
}
#endif

static void cmc623_Mode_Change(struct mDNIe_data_type *mode, int cabc_enable, int lut, int flag)
{
	int check;

//	if(mDNIe_Tuning_Mode == TRUE)
//	{
//		printk("mDNIe_Mode_Change [mDNIe_Tuning_Mode = TRUE, API is Return] \n");
//		return;
//	}
//	else
//	{
//		s3c_mdnie_mask();

#ifdef CMC623_TUNING
	printk(KERN_ERR "%s ignore for tuning mode\n", __func__);
	return;
#endif

	if(!p_cmc623_data)
		{
		printk(KERN_ERR "%s cmc623 is not initialized\n", __func__);
		return;
		}

	if(cmc623_bypass_mode)
		{
		printk(KERN_WARNING "%s ignore for bypass mode\n", __func__);
		return;
		}
	
	while ( mode->addr != END_SEQ)
	{
		cmc623_I2cWrite16(mode->addr, mode->data);
		//pr_debug("[cmc623] a(0x%x),d(0x%x)\n", mode->addr, mode->data);	
		mode++;
	}
	
	// brightness setting 
	check = ((flag&CMC_FLAG_SETTING_FIRST) || cabc_enable != cmc623_state.cabc_enabled);
	if(check || lut != cmc623_state.power_lut_num)
	{
		if(cabc_enable)
		{
			//CABC brightness setting
			cmc623_cabc_pwm_brightness_reg(cmc623_state.brightness, flag, lut);

			cmc623_state.cabc_enabled = TRUE;
		}
		else
		{
			//Manual brightness setting
			if((flag&CMC_FLAG_SETTING_FIRST)&&!(flag&CMC_FLAG_SETTING_BOOT))
				cmc623_manual_pwm_brightness_reg_nosync(cmc623_state.brightness);
			else
				cmc623_manual_pwm_brightness_reg(cmc623_state.brightness);

			cmc623_state.cabc_enabled = FALSE;
		}
		cmc623_state.power_lut_num = lut;
	}
#if 0
	if(check)
	{
		cmc623_Color_White_Change(cmc623_state.white,FALSE);
		cmc623_Color_Saturation_Change(cmc623_state.saturation,FALSE);
		cmc623_Color_Black_Change(cmc623_state.black,FALSE);	
	}
#endif	

#ifdef __CMC623_AUTO_OVE_MODE__
	if(!cmc623_OutDoor_Enable(current_cmc623_OutDoor_OnOff))
	{
		cmc623_reg_unmask();
	}
#else
	cmc623_reg_unmask();
#endif
}

static void cmc623_Outdoor_Change(enum Cmc623_Tune_Type mode, int enable)
{
	u16 data = 0;

	/* check outdoor is available */
	if (!modes_desc[mode].use_outdoor) {
		pr_debug("%s: mode(%d) doesn't support outdoor\n", __func__, mode);
		return;
	}
	
	cmc623_I2cWrite16(0x0000,0x0000);
	cmc623_I2cRead16(0x0001, &data);

	if (enable) {
		int idx;
		struct mDNIe_data_type* values;

		/* Because ACR ALGO_SEL field has "00" or "01" along using PCC algorithm,
		 * we save value before enable OVE algorithm("10").
		 */
		if ((data & CMC623_ACR_ALGO_SEL_MASK) != CMC623_ACR_ALGO_SEL_OVE)
			cmc623_state.saved_algo_sel = data & CMC623_ACR_ALGO_SEL_MASK;
		
		data &= ~CMC623_ACR_ALGO_SEL_MASK;
		data |= CMC623_ACR_ALGO_SEL_OVE;
		cmc623_I2cWrite16(0x0001, data);

		/* tuning value */
		idx = cmc623_get_outdoor_index(mode, 1);
		values = (struct mDNIe_data_type*)cmc623_Outdoor_values[idx];
		while (values->addr != END_SEQ)
		{
			cmc623_I2cWrite16(values->addr, values->data);
			values++;
		}
	} else {
		/* If ove is enabled outside of this function, this code should be re-written,
		 * because saved_algo_sel value could not be valid in that case.
		 */
		if ((data & CMC623_ACR_ALGO_SEL_MASK) == CMC623_ACR_ALGO_SEL_OVE) {
			data &= ~CMC623_ACR_ALGO_SEL_MASK;
			data |= (cmc623_state.saved_algo_sel & CMC623_ACR_ALGO_SEL_MASK);
		}
		cmc623_I2cWrite16(0x0001, data);
	}
	cmc623_I2cWrite16(0x0028,0x0000);

	printk("[cmc623] UImode(%d), outdoor %d.\n", mode, enable );
}

static void cmc623_Colortemp_Change(enum Cmc623_Tune_Type mode, int color_temp)
{
	u16 data;

	/* check color_temp is available */
	if (!modes_desc[mode].use_coltemp) {
		pr_debug("%s: mode(%d) doesn't support color temperature\n", __func__, mode);
		return;
	}
	
	cmc623_I2cWrite16(0x0000, 0x0000);
	cmc623_I2cRead16(0x0001, &data);

	if (color_temp) {
		int idx;
		struct mDNIe_data_type* values; 
		
		data |= CMC623_ACR_MCM_SEL;
		cmc623_I2cWrite16(0x0001, data);

		/* tuning value */
		idx = cmc623_get_colortemp_index(mode, color_temp);
		values = (struct mDNIe_data_type*)cmc623_Colortemp_values[idx];
		while ( values->addr != END_SEQ)
		{
			cmc623_I2cWrite16(values->addr, values->data);
			values++;
		}
	} else {
		data &= ~CMC623_ACR_MCM_SEL;
		cmc623_I2cWrite16(0x0001, data);
	}
	cmc623_I2cWrite16(0x0028,0x0000);

	printk("[cmc623] UImode(%d), colortemp %d.\n", mode, color_temp );
}

static void cmc623_set_mode_real(enum Cmc623_Tune_Type mode, struct cmc623_mode_info *mode_info, int flag)
{
	int real_lut = 0;
	int type_idx;
	int cabc = 0;
	int outdoor = 0;
	int coltemp = 0;
	int bgeffect = 0;

	cabc = modes_desc[mode].use_cabc ? mode_info->cabc : 0;
	outdoor = modes_desc[mode].use_outdoor ? mode_info->outdoor : 0;
	coltemp = modes_desc[mode].use_coltemp ? mode_info->color_temp : 0;
	bgeffect = modes_desc[mode].use_bgeffect ? mode_info->bgeffect : 0;
	real_lut = modes_desc[mode].power_lut_num;

	/* if you want to use outdoor_warm/outdoor_cold, remove this code */
	/* if (outdoor)
		coltemp = 0; */
	
	type_idx = cmc623_get_type_index(cabc, bgeffect);

	cmc623_Mode_Change((struct mDNIe_data_type*)(cmc623_values[mode]+type_idx)[0], cabc, real_lut, flag);

	printk("[cmc623] UImode(%d:%d), cabc %d->%d, bgeffect %d->%d.\n",
		mode, type_idx, mode_info->cabc, cabc, mode_info->bgeffect, bgeffect);	
}

static void cmc623_Set_Mode(	enum Lcd_CMC623_UI_mode mode, 
				int cabc_onoff,
				/*int outdoor_onoff,*/
				/*int color_temp,*/
				int bgeffect,
				int flag )
{
	enum Cmc623_Tune_Type tune_mode;
	struct cmc623_mode_info mode_info;
	bool change_color_temp;

	memset(&mode_info, 0, sizeof(mode_info));

	mode_info.cabc = cabc_onoff;
	//mode_info.outdoor = outdoor_onoff;
	//mode_info.color_temp = color_temp;
	mode_info.bgeffect = bgeffect;

	/* default setting */
	change_color_temp = false;
	
	switch(mode) {
		case CMC623_UI_MODE:
			tune_mode = CMC_TUNE_UI;
			mode_info.color_temp = COLOR_TEMP_NORMAL;
		break;

		case CMC623_VIDEO_MODE:
			tune_mode = CMC_TUNE_Video;
			mode_info.color_temp = COLOR_TEMP_NORMAL;
		break;

		case CMC623_VIDEO_WARM_MODE:
			tune_mode = CMC_TUNE_Video;
			mode_info.color_temp = COLOR_TEMP_WARM;
			change_color_temp = true;
		break;

		case CMC623_VIDEO_COLD_MODE:
			tune_mode = CMC_TUNE_Video;
			mode_info.color_temp = COLOR_TEMP_COLD;
			change_color_temp = true;
		break;
		
		case CMC623_CAMERA_MODE:
			tune_mode = CMC_TUNE_Camera;
			mode_info.color_temp = COLOR_TEMP_NORMAL;
		break;

		case CMC623_NAVI:
			tune_mode = CMC_TUNE_UI;
			mode_info.color_temp = COLOR_TEMP_NORMAL;
		break;

		case CMC623_DMB_MODE:
			tune_mode = CMC_TUNE_DMB;
			mode_info.color_temp = COLOR_TEMP_NORMAL;
		break;

		case CMC623_VT_MODE:
			tune_mode = CMC_TUNE_VT;
			mode_info.color_temp = COLOR_TEMP_NORMAL;
		break;

		case CMC623_GALLERY_MODE:
			tune_mode = CMC_TUNE_Gallery;
			mode_info.color_temp = COLOR_TEMP_NORMAL;
		break;

		case CMC623_DMB_WARM_MODE:
			tune_mode = CMC_TUNE_DMB;
			mode_info.color_temp = COLOR_TEMP_WARM;
			change_color_temp = true;
		break;

		case CMC623_DMB_COLD_MODE:
			tune_mode = CMC_TUNE_DMB;
			mode_info.color_temp = COLOR_TEMP_COLD;
			change_color_temp = true;
		break;

		default:
			tune_mode = CMC_TUNE_UI;
			mode_info.color_temp = COLOR_TEMP_NORMAL;
		break;
	}

	current_cmc623_UI = mode;
	current_cmc623_CABC_OnOff = cabc_onoff;
	/*current_cmc623_OutDoor_OnOff = outdoor_onoff;*/
	current_cmc623_Userselect = bgeffect;

	cmc623_set_mode_real(tune_mode, &mode_info, flag);
	if (change_color_temp)
		cmc623_Colortemp_Change(tune_mode, mode_info.color_temp);
}

#if 0
static void cmc623_Set_Mode(enum Lcd_CMC623_UI_mode mode, int cmc623_CABC_OnOff, int flag)
{
	int cabc_enable=0;
	int lut_num;
	current_cmc623_UI = mode;
	
	if(cmc623_CABC_OnOff)
	{
		cabc_enable = 1;
		
		switch(mode)
		{
			case CMC623_UI_MODE:
				lut_num = 0;
				cmc623_Mode_Change((struct mDNIe_data_type*)cmc623_values[CMC_UI_CABC], TRUE, flag, lut_num);
			break;

			case CMC623_VIDEO_MODE:
				lut_num = 0;
				cmc623_Mode_Change((struct mDNIe_data_type*)cmc623_values[CMC_Video_CABC], TRUE, flag, lut_num);
			break;

			case CMC623_VIDEO_WARM_MODE:
				lut_num = 0;
				cmc623_Mode_Change((struct mDNIe_data_type*)cmc623_values[CMC_Video_CABC], TRUE, flag, lut_num);
			break;

			case CMC623_VIDEO_COLD_MODE:
				lut_num = 0;
				cmc623_Mode_Change((struct mDNIe_data_type*)cmc623_values[CMC_Video_CABC], TRUE, flag, lut_num);
			break;
			
			case CMC623_CAMERA_MODE:
				lut_num = 0;
				cabc_enable = 0;
				cmc623_Mode_Change((struct mDNIe_data_type*)cmc623_values[CMC_Camera], FALSE, flag, lut_num);
			break;

			case CMC623_NAVI:
				lut_num = 0;
				cmc623_Mode_Change((struct mDNIe_data_type*)cmc623_values[CMC_UI_CABC], TRUE, flag, lut_num);
			break;

			case CMC623_DMB_MODE:
				lut_num = 0;
				cmc623_Mode_Change((struct mDNIe_data_type*)cmc623_values[CMC_DMB_CABC], TRUE, flag, lut_num);
			break;

			case CMC623_VT_MODE:
				lut_num = 0;
				cmc623_Mode_Change((struct mDNIe_data_type*)cmc623_values[CMC_VT_CABC], TRUE, flag, lut_num);
			break;

			case CMC623_GALLERY_MODE:
				lut_num = 0;
				cmc623_Mode_Change((struct mDNIe_data_type*)cmc623_values[CMC_GALLERY_CABC], TRUE, flag, lut_num);
			break;
		}

		//current_cmc623_UI = mode;
		current_cmc623_CABC_OnOff = TRUE;
	}
	else
	{
		cabc_enable = 0;
		
		switch(mode)
		{
			case CMC623_UI_MODE:
				lut_num = 0;
				cmc623_Mode_Change((struct mDNIe_data_type*)cmc623_values[CMC_UI], FALSE, flag, lut_num);
			break;

			case CMC623_VIDEO_MODE:
				lut_num = 0;
				cabc_enable = 0;
				cmc623_Mode_Change((struct mDNIe_data_type*)cmc623_values[CMC_Video], FALSE, flag, lut_num);
			break;

			case CMC623_VIDEO_WARM_MODE:
				lut_num = 0;
				cabc_enable = 0;
				cmc623_Mode_Change((struct mDNIe_data_type*)cmc623_values[CMC_Video_CABC], FALSE, flag, lut_num);
			break;

			case CMC623_VIDEO_COLD_MODE:
				lut_num = 0;
				cabc_enable = 0;
				cmc623_Mode_Change((struct mDNIe_data_type*)cmc623_values[CMC_Video_CABC], FALSE, flag, lut_num);
			break;
			
			case CMC623_CAMERA_MODE:
				lut_num = 0;
				cmc623_Mode_Change((struct mDNIe_data_type*)cmc623_values[CMC_Camera], FALSE, flag, lut_num);
			break;

			case CMC623_NAVI:
				lut_num = 0;
				cmc623_Mode_Change((struct mDNIe_data_type*)cmc623_values[CMC_UI], FALSE, flag, lut_num);
			break;

			case CMC623_DMB_MODE:
				lut_num = 0;
				cmc623_Mode_Change((struct mDNIe_data_type*)cmc623_values[CMC_DMB], FALSE, flag, lut_num);
			break;

			case CMC623_VT_MODE:
				lut_num = 0;
				cmc623_Mode_Change((struct mDNIe_data_type*)cmc623_values[CMC_VT], FALSE, flag, lut_num);
			break;

			case CMC623_GALLERY_MODE:
				lut_num = 0;
				cmc623_Mode_Change((struct mDNIe_data_type*)cmc623_values[CMC_GALLERY], FALSE, flag, lut_num);
			break;
		}
		
		//current_cmc623_UI = mode;
		current_cmc623_CABC_OnOff = FALSE;
	}	
	printk("[cmc623] cmc623_Set_Mode: current_cmc623_UI(%d), current_cmc623_CABC_OnOff(%d)->(%d) / OVE : %d \n",current_cmc623_UI, current_cmc623_CABC_OnOff, cabc_enable,current_cmc623_OutDoor_OnOff);	
}
//EXPORT_SYMBOL(cmc623_Set_Mode);
#endif

void cmc623_Set_Mode_Ext(enum Lcd_CMC623_UI_mode mode, u8 mDNIe_Outdoor_OnOff)
{
	enum Cmc623_Tune_Type current_tune_mode;
	enum Cmc623_Tune_Type tune_mode;
	bool color_temp_enable;
	int color_temp;

	mutex_lock(&cmc623_state_transaction_lock);

	switch (mode) {
		case CMC623_VIDEO_MODE:
			tune_mode = CMC_TUNE_Video;
			color_temp = COLOR_TEMP_NORMAL;
			color_temp_enable = true;
		break;

		case CMC623_VIDEO_WARM_MODE:
			tune_mode = CMC_TUNE_Video;
			color_temp = COLOR_TEMP_WARM;
			color_temp_enable = true;
		break;

		case CMC623_VIDEO_COLD_MODE:
			tune_mode = CMC_TUNE_Video;
			color_temp = COLOR_TEMP_COLD;
			color_temp_enable = true;
		break;

		case CMC623_DMB_MODE:
			tune_mode = CMC_TUNE_DMB;
			color_temp = COLOR_TEMP_NORMAL;
			color_temp_enable = true;
		break;

		case CMC623_DMB_WARM_MODE:
			tune_mode = CMC_TUNE_DMB;
			color_temp = COLOR_TEMP_WARM;
			color_temp_enable = true;
		break;

		case CMC623_DMB_COLD_MODE:
			tune_mode = CMC_TUNE_DMB;
			color_temp = COLOR_TEMP_COLD;
			color_temp_enable = true;
		break;

		default:
			tune_mode = CMC_TUNE_UI;
			color_temp = COLOR_TEMP_NORMAL;
			color_temp_enable = false;
		break;
	}

	switch (current_cmc623_UI) {
		case CMC623_VIDEO_MODE:
			current_tune_mode = CMC_TUNE_Video;
		break;

		case CMC623_VIDEO_WARM_MODE:
			current_tune_mode = CMC_TUNE_Video;
		break;

		case CMC623_VIDEO_COLD_MODE:
			current_tune_mode = CMC_TUNE_Video;
		break;

		case CMC623_DMB_MODE:
			current_tune_mode = CMC_TUNE_DMB;
		break;

		case CMC623_DMB_WARM_MODE:
			current_tune_mode = CMC_TUNE_DMB;
		break;

		case CMC623_DMB_COLD_MODE:
			current_tune_mode = CMC_TUNE_DMB;
		break;

		default:
			current_tune_mode = CMC_TUNE_UI;
		break;
	}

	if (color_temp_enable && (tune_mode == current_tune_mode)) {
		/* don't change mode if only color temperature is changed */
		cmc623_Colortemp_Change(tune_mode, color_temp);
	} else {
		cmc623_Set_Mode(mode, current_cmc623_CABC_OnOff,
				current_cmc623_Userselect, 0);
	}

	mutex_unlock(&cmc623_state_transaction_lock);
	
	dprintk("[cmc623] cmc623_Set_Mode_Ext: current_cmc623_UI(%d), current_cmc623_OutDoor_OnOff(%d)  \n",current_cmc623_UI, current_cmc623_OutDoor_OnOff);	
}
//EXPORT_SYMBOL(cmc623_Set_Mode_Ext);

void cmc623_Set_Userselect_Ext(int userselect)
{
	printk(KERN_INFO "%s(%d)\n", __func__, userselect);

	mutex_lock(&cmc623_state_transaction_lock);
	cmc623_Set_Mode(current_cmc623_UI, current_cmc623_CABC_OnOff,
				userselect, 0);
	mutex_unlock(&cmc623_state_transaction_lock);
}

void cmc623_Set_Outdoor_Ext(enum Lcd_CMC623_UI_mode mode, int enable)
{
	enum Cmc623_Tune_Type tune_mode;

	mutex_lock(&cmc623_state_transaction_lock);

	switch(mode) {
		case CMC623_UI_MODE:
			tune_mode = CMC_TUNE_UI;
		break;

		case CMC623_VIDEO_MODE:
		case CMC623_VIDEO_WARM_MODE:
		case CMC623_VIDEO_COLD_MODE:
			tune_mode = CMC_TUNE_Video;
		break;
		
		case CMC623_CAMERA_MODE:
			tune_mode = CMC_TUNE_Camera;
		break;

		case CMC623_NAVI:
			tune_mode = CMC_TUNE_UI;
		break;

		case CMC623_DMB_MODE:
			tune_mode = CMC_TUNE_DMB;
		break;

		case CMC623_VT_MODE:
			tune_mode = CMC_TUNE_VT;
		break;

		case CMC623_GALLERY_MODE:
			tune_mode = CMC_TUNE_Gallery;
		break;

		default:
			tune_mode = CMC_TUNE_UI;
		break;
	}

	cmc623_Outdoor_Change(tune_mode, enable);
	current_cmc623_OutDoor_OnOff = enable;
	mutex_unlock(&cmc623_state_transaction_lock);
}

void cmc623_Set_Region_Ext(int enable, int hStart, int hEnd, int vStart, int vEnd)
{
	u16 data=0;

	mutex_lock(&cmc623_state_transaction_lock);

	cmc623_I2cWrite16(0x0000,0x0000);
	cmc623_I2cRead16(0x0001, &data);

	data &= 0x00ff;

	if(enable)
	{
		cmc623_I2cWrite16(0x0001,0x0300 | data);
	
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
	mutex_unlock(&cmc623_state_transaction_lock);
	
	cmc623_current_region_enable = enable;
}
EXPORT_SYMBOL(cmc623_Set_Region_Ext);

static bool cmc623_I2cWrite16( unsigned char Addr, unsigned long Data)
{
	int err;
	struct i2c_msg msg[1];
	unsigned char data[3];
	struct i2c_client *p_client;

	if(!p_cmc623_data)
		{
		printk(KERN_ERR "p_cmc623_data is NULL\n");
		return -ENODEV;
		}
	p_client = p_cmc623_data->client;		

	if((p_client == NULL))  
		{
		printk("cmc623_I2cWrite16 p_client is NULL\n");
		return -ENODEV;
		}

	if (!p_client->adapter) 
		{
		printk("cmc623_I2cWrite16 p_client->adapter is NULL\n");
		return -ENODEV;
		}

	if(TRUE == cmc623_state.suspended)
		{
		printk("cmc623 don't need writing while LCD off(a:%x,d:%lx)\n", Addr, Data);
		return 0;
		}

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

	data[0] = Addr;
	data[1] = ((Data >>8)&0xFF);
	data[2] = (Data)&0xFF;
	msg->addr 	= p_client->addr;
	msg->flags 	= I2C_M_WR;
	msg->len 	= 3;
	msg->buf 	= data;

	err = i2c_transfer(p_client->adapter, msg, 1);

	if (err >= 0) 
		{
		pr_debug("%s i2c a:0x%x d:0x%lx\n", __func__, Addr, Data);
		return 0;
		}

	printk("%s i2c transfer error:%d(a:%x)\n", __func__, err, Addr);/* add by inter.park */
	return err;    
}

static int cmc623_I2cRead16_direct(u8 reg, u16 *val, int isDirect)
{
	int      err;
	struct   i2c_msg msg[2];
	u8 regaddr = reg;
	u8 data[2];
	struct i2c_client *p_client;

	if(!p_cmc623_data)
		{
		printk(KERN_ERR "%s p_cmc623_data is NULL\n", __func__);
		return -ENODEV;
		}
	p_client = p_cmc623_data->client;		

	if( (p_client == NULL))  
		{
		printk("%s p_client is NULL\n", __func__);
		return -ENODEV;
		}

	if (!p_client->adapter) 
		{
		printk("%s p_client->adapter is NULL\n", __func__);
		return -ENODEV;
		}

	if(!isDirect && regaddr == 0x0001 && last_cmc623_Algorithm!=0xffff && last_cmc623_Bank==0)
		{
		*val = last_cmc623_Algorithm;
		return 0;
		}

	msg[0].addr   = p_client->addr;
	msg[0].flags  = I2C_M_WR;
	msg[0].len    = 1;
	msg[0].buf    = &regaddr;
	msg[1].addr   = p_client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len   = 2;
	msg[1].buf   = &data[0];
	err = i2c_transfer(p_client->adapter, &msg[0], 2);

	if (err >= 0) 
		{
		*val = (data[0]<<8) | data[1];
		return 0;
		}
	printk(KERN_ERR "%s %d i2c transfer error: %d\n", __func__, __LINE__, err);/* add by inter.park */

	return err;
}

static inline int cmc623_I2cRead16(u8 reg, u16 *val)
{
	return cmc623_I2cRead16_direct(reg, val, 0);
}

#ifdef __CMC623_AUTO_OVE_MODE__
void cmc623_autobrightness_enable(int enable)
{
	mutex_lock(&cmc623_state_transaction_lock);
	if(current_cmc623_OutDoor_OnOff == TRUE && enable == FALSE)
	{//outdoor mode off
		current_cmc623_OutDoor_OnOff = FALSE;
		cmc623_Set_Mode(current_cmc623_UI, current_cmc623_CABC_OnOff,0);
	}

	current_autobrightness_enable = enable;
	mutex_unlock(&cmc623_state_transaction_lock);
}
EXPORT_SYMBOL(cmc623_autobrightness_enable);
#endif //__CMC623_AUTO_OVE_MODE__

static void cmc623_cabc_enable_flag(int enable, int flag)
{
	if(!p_cmc623_data)
	{
		printk(KERN_ERR "%s cmc623 is not initialized\n", __func__);
	}

	printk(KERN_INFO "%s(%d)\n", __func__, enable);

	cmc623_Set_Mode(current_cmc623_UI, enable, 
				current_cmc623_Userselect,
				flag);
}

void cmc623_cabc_enable(int enable)
{
	if(!p_cmc623_data)
	{
		printk(KERN_ERR "%s cmc623 is not initialized\n", __func__);
	}

	printk(KERN_INFO "%s(%d)\n", __func__, enable);

	mutex_lock(&cmc623_state_transaction_lock);
	cmc623_Set_Mode(current_cmc623_UI, enable,
				current_cmc623_Userselect,
				0);
	mutex_unlock(&cmc623_state_transaction_lock);
}
EXPORT_SYMBOL(cmc623_cabc_enable);

#ifdef __CMC623_AUTO_OVE_MODE__
static u16 ove_target_value=0;
static void ove_workqueue_func(struct work_struct *data)
{
	int i = 0;
	for(i=0; i<=8; ++i)
	{
		mutex_lock(&cmc623_state_transaction_lock);
		if(cmc623_state.suspended == TRUE)
		{
			mutex_unlock(&cmc623_state_transaction_lock);
			return;
		}

		cmc623_I2cWrite16(0x0054, (((ove_target_value >> 8) * i / 8) << 8) | ((ove_target_value & 0x00ff) * i / 8));
		cmc623_reg_unmask();
		mutex_unlock(&cmc623_state_transaction_lock);
		
		msleep(15);
	}
}

static int cmc623_OutDoor_Enable(int enable)
{
	u16 i2cdata=0;

	if(enable)
	{
		switch(current_cmc623_UI)
		{
			case CMC623_UI_MODE:
			ove_target_value = OVE_values[6 + cmc623_state.cabc_enabled];
			break;

			case CMC623_VIDEO_MODE:
			ove_target_value = OVE_values[2 + cmc623_state.cabc_enabled];
			break;
			
			case CMC623_CAMERA_MODE:
			ove_target_value = OVE_values[4 + cmc623_state.cabc_enabled];
			break;

			case CMC623_DMB_MODE:
			ove_target_value = OVE_values[10 + cmc623_state.cabc_enabled];
			break;

			case CMC623_VT_MODE:
			ove_target_value = OVE_values[8 + cmc623_state.cabc_enabled];
			break;

			case CMC623_GALLERY_MODE:
			ove_target_value = OVE_values[12 + cmc623_state.cabc_enabled];
			break;
			
			default:
			ove_target_value = OVE_values[0];
			break;
		}
		
		if(ove_target_value == 0x00)
		{
			return 0;
		}

		cmc623_I2cWrite16(0x0000, 0x0000);
		cmc623_I2cRead16(0x0001, &i2cdata);
		i2cdata |= 0x0002;
		cmc623_I2cWrite16(0x0001, i2cdata);

		if(current_cmc623_OutDoor_OnOff != enable)
		{
			queue_work(ove_wq, &work_ove);
		}
		else
		{
			cmc623_I2cWrite16(0x0054, ove_target_value);
			cmc623_reg_unmask();
		}
	}
	else 
	{//outdoor mode off
		cmc623_I2cWrite16(0x0000, 0x0000);
		cmc623_I2cRead16(0x0001, &i2cdata);
		i2cdata &= 0xfffc;
		cmc623_I2cWrite16(0x0001, i2cdata);

		cmc623_reg_unmask();
	}
	current_cmc623_OutDoor_OnOff = enable;

	return 1;
}
//EXPORT_SYMBOL(cmc623_OutDoor_Enable);
#endif

// value: 0 ~ 100
static void cmc623_cabc_pwm_brightness_reg(int value, int flag, int lut)
{
	u32 reg;
	unsigned char * p_plut;
	u16 min_duty;

	if(!p_cmc623_data)
		{
		printk(KERN_ERR "%s cmc623 is not initialized\n", __func__);
		return;
		}

	p_plut = cmc623_Power_LUT[lut];

	min_duty = p_plut[7] * value / 100;
	if(min_duty < 4)
		{
		if(value == 0)
			reg = 0xC000;
		else
			reg = 0xC000 | ((max(1,(value*p_plut[3]/100))));		
		}
	else
		{
		cmc623_I2cWrite16(0x76,(p_plut[0] * value / 100) << 8 | (p_plut[1] * value / 100));	//PowerLUT
		cmc623_I2cWrite16(0x77,(p_plut[2] * value / 100) << 8 | (p_plut[3] * value / 100));	//PowerLUT
		cmc623_I2cWrite16(0x78,(p_plut[4] * value / 100) << 8 | (p_plut[5] * value / 100));	//PowerLUT
		cmc623_I2cWrite16(0x79,(p_plut[6] * value / 100) << 8 | (p_plut[7] * value / 100));	//PowerLUT
		cmc623_I2cWrite16(0x7a,(p_plut[8] * value / 100) << 8);	//PowerLUT

		reg = 0x5000 | (value<<4);
		}
	
//	if(flag&CMC_FLAG_SETTING_FIRST)
//		{
//		reg |= 0x8000;
//		}

	cmc623_I2cWrite16(0xB4, reg);			//pwn duty
}

// value: 0 ~ 100
static void cmc623_cabc_pwm_brightness(int value)
{

	if(!p_cmc623_data)
		{
		printk(KERN_ERR "%s cmc623 is not initialized\n", __func__);
		return;
		}

	cmc623_I2cWrite16(0x00,0x0000);	//BANK 0

	cmc623_cabc_pwm_brightness_reg(value,0,cmc623_state.power_lut_num);

	cmc623_I2cWrite16(0x28,0x0000);
}

// value: 0 ~ 100
// This should be used only for special purpose as resume
static void cmc623_manual_pwm_brightness_reg_nosync(int value)
{
	u32 reg;

	if(!p_cmc623_data)
		{
		printk(KERN_ERR "%s cmc623 is not initialized\n", __func__);
		return;
		}

	reg = 0xC000 | (value);

	cmc623_I2cWrite16(0xB4, reg);			//pwn duty
}

// value: 0 ~ 100
static void cmc623_manual_pwm_brightness_reg(int value)
{
	u32 reg;

	if(!p_cmc623_data)
		{
		printk(KERN_ERR "%s cmc623 is not initialized\n", __func__);
		return;
		}

	/* Normally the value we set is not reflected immediatly on PWM line, because of cmc623's ABC block.
	 * In special case like wakeup, this shows abnormal brightness control on LCD.
	 * But, this workaround code need to be improved */
	if(value > 6)
		reg = 0x4000 | (value<<4);	/* normal */
	else
		reg = 0xC000 | (value);

	cmc623_I2cWrite16(0xB4, reg);			//pwn duty
}

// value: 0 ~ 100
static void cmc623_manual_pwm_brightness(int value)
{
	if(!p_cmc623_data)
		{
		printk(KERN_ERR "%s cmc623 is not initialized\n", __func__);
		return;
		}

	cmc623_I2cWrite16(0x00, 0x0000);		//bank0
	cmc623_manual_pwm_brightness_reg(value);
	cmc623_I2cWrite16(0x28, 0x0000);
}



// value: 0 ~ 1600
void tune_cmc623_pwm_brightness(int value)
{
	u32 data;


#ifdef CMC623_TUNE_STATIC
	if (brightness_calls == 0) {
		brightness_calls = 1;
		cmc623_load_tuning_static_data();
		if (static_tune_loaded) {
			struct mDNIe_data_type *static_tune_mode;
			static_tune_mode = &cmc623_static_Tune[0];
			while (static_tune_mode->addr != END_SEQ) {
				cmc623_I2cWrite16(static_tune_mode->addr, static_tune_mode->data);
				pr_notice("[cmc623] a(0x%x),d(0x%x)\n",static_tune_mode->addr, static_tune_mode->data);	
				static_tune_mode++;
			}
			cmc623_reg_unmask();
		}
	}
#endif	

	if(!p_cmc623_data)
		{
		printk(KERN_ERR "%s cmc623 is not initialized\n", __func__);
		}

	if(value<0)
		data = 0;
	else if(value>1600)
		data = 1600;
	else
		data = value;

	mutex_lock(&cmc623_state_transaction_lock);
		
#ifdef __CMC623_AUTO_OVE_MODE__
	if(data == 1280 && current_autobrightness_enable)
	{//outdoor mode on
		pr_info("automatic outdoor enable\n");
		cmc623_OutDoor_Enable(TRUE);
	}
	else if (current_cmc623_OutDoor_OnOff == TRUE && data < 1280)
	{//outdoor mode off
		pr_info("automatic outdoor disable\n");
		cmc623_OutDoor_Enable(FALSE);
	}
#endif

	data >>= 4;

	// data must not be zero unless value is zero
	if(value>0 && data==0)
		data = 1;

	cmc623_state.brightness = data;

	/* In bypass mode, cabc is off */
	if(cmc623_state.cabc_enabled == TRUE && !cmc623_bypass_mode)
	{
		cmc623_cabc_pwm_brightness(data);
	}
	else
	{
		cmc623_manual_pwm_brightness(data);
	}
	mutex_unlock(&cmc623_state_transaction_lock);
}
EXPORT_SYMBOL(tune_cmc623_pwm_brightness);

#ifdef CMC623_TUNING

//static bool cmc623_tune(unsigned long num)
bool cmc623_tune(unsigned long num) // P1_LSJ : DE08
{
	unsigned int i;

	printk("========== Start of tuning CMC623 Jun  ==========\n");

	for (i=0; i<num; i++) 
	{
		printk("[%2d] Writing => reg: 0x%2x, data: 0x%4lx\n", i+1, Cmc623_TuneSeq[i].RegAddr, Cmc623_TuneSeq[i].Data);

		if (0 > cmc623_I2cWrite16(Cmc623_TuneSeq[i].RegAddr, Cmc623_TuneSeq[i].Data)) 
		{
			printk("I2cWrite16 failed\n");
			return 0;
		}
		else 
		{
			printk("I2cWrite16 succeed\n");
		}

		if ( Cmc623_TuneSeq[i].RegAddr == CMC623_REG_SWRESET && Cmc623_TuneSeq[i].Data == 0xffff ) 
		{
			mdelay(3);
		}
	}
	printk("==========  End of tuning CMC623 Jun  ==========\n");
	return 1;
}

//static int parse_text(char * src, int len, unsigned short * output)
static int parse_text(char * src, int len)
{
	int i,count, ret;
	int index=0;
	char * str_line[CMC623_MAX_SETTINGS];
	char * sstart;
	char * c;
	unsigned int data1, data2;

	c = src;
	count = 0;
	sstart = c;

	for(i=0; i<len; i++,c++) 
	{
		char a = *c;
		if(a=='\r' || a=='\n') 
		{
			if(c > sstart) 
			{
				str_line[count] = sstart;
				count++;
			}
			*c='\0';
			sstart = c+1;
		}
	}

	if(c > sstart) 
	{
		str_line[count] = sstart;
		count++;
	}

	printk("----------------------------- Total number of lines:%d\n", count);

	for(i=0; i<count; i++) 
	{
		printk("line:%d, [start]%s[end]\n", i, str_line[i]);
		ret = sscanf(str_line[i], "0x%x,0x%x\n", &data1, &data2);
		printk("Result => [0x%2x 0x%4x] %s\n", data1, data2, (ret==2)?"Ok":"Not available");
		if(ret == 2) 
		{   
			Cmc623_TuneSeq[index].RegAddr = (unsigned char)data1;
			Cmc623_TuneSeq[index++].Data  = (unsigned long)data2;
		}
	}
	return index;
}

static int cmc623_load_data(void)
{
	struct file *filp;
	char	*dp;
	long	l, i ;
	loff_t  pos;
	int     ret, num;
	mm_segment_t fs;

	klogi("cmc623_load_data start!");

	fs = get_fs();
	set_fs(get_ds());

	filp = filp_open(CMC623_PATH_TUNING_DATA, O_RDONLY, 0);
	if(IS_ERR(filp)) 
	{
		kloge("file open error:%d", (s32)filp);

		return -1;

		/*		
		filp = filp_open(CMC623_PATH_TUNING_DATA2, O_RDONLY, 0);
		if(IS_ERR(filp)) 
		{
		kloge("file open error2");

		filp = filp_open(CMC623_PATH_TUNING_DATA3, O_RDONLY, 0);
		if(IS_ERR(filp)) 
		{
		kloge("file open error3");
		return -1;
		}
		}
		*/
	}

	l = filp->f_path.dentry->d_inode->i_size;
	klogi("Size of the file : %ld(bytes)", l);

	//dp = kmalloc(l, GFP_KERNEL);
	dp = kmalloc(l+10, GFP_KERNEL);		// add cushion
	if(dp == NULL) 
	{
		kloge("Out of Memory!");
		filp_close(filp, current->files);
		return -1;
	}
	pos = 0;
	memset(dp, 0, l);
	kloge("== Before vfs_read ======");
	ret = vfs_read(filp, (char __user *)dp, l, &pos);   // P1_LSJ : DE08 : ¿©±â¼­ Á×À½ 
	kloge("== After vfs_read ======");

	if(ret != l) 
	{
		kloge("<CMC623> Failed to read file (ret = %d)", ret);
		kfree(dp);
		filp_close(filp, current->files);
		return -1;
	}

	filp_close(filp, current->files);

	set_fs(fs);

	for(i=0; i<l; i++)
	{   
		printk("%x ", dp[i]);
	}
	printk("\n");

	num = parse_text(dp, l);

	if(!num) 
	{
		kloge("Nothing to parse!");
		kfree(dp);
		return -1;
	}

	printk("------ Jun Total number of parsed lines: %d\n", num);
	cmc623_tune(num);

	kfree(dp);
	return num;
}

int CMC623_tuning_load_from_file(void)
{
	return cmc623_load_data();
}
EXPORT_SYMBOL(CMC623_tuning_load_from_file);

#ifdef CMC623_TUNE_STATIC
static int parse_text2(char * src, int len)
{
	int i,count, ret, real_count;
	int index=0;
	char * str_line[1200];
	char * sstart;
	char * c;
	unsigned int data1, data2;
	struct mDNIe_data_type *mode;

	c = src;
	count = 0;
	sstart = c;
	real_count = 0;

	for(i=0; i<len; i++,c++) 
	{
		char a = *c;
		if(a=='\r' || a=='\n') 
		{
			if(c > sstart) 
			{
				str_line[count] = sstart;
				count++;
			}
		*c='\0';
		sstart = c+1;

		real_count++;

		printk("line:%d, [start]%s[end]\n", real_count, "-_-");
		}
	}

	if(c > sstart) 
	{
		str_line[count] = sstart;
		count++;
	}

	printk("----------------------------- Total number of lines:%d\n", count);

	mode = &cmc623_static_Tune[0];

	for(i=0; i<count; i++) 
	{
		while(*str_line[i] == '\t' || *str_line[i] == ' ')
		{
			str_line[i]++;
		}
		//printk("line:%d, [start]%s[end]\n", i, str_line[i]);
		ret = sscanf(str_line[i], "0x%x,0x%x\n", &data1, &data2);
		//printk("Result => [0x%2x 0x%4x] %s\n", data1, data2, (ret==2)?"Ok":"Not available");
		if(ret == 2 && mode != 0) 
		{
			mode->addr = (u16)data1;
			mode->data = (u16)data2;
			mode++;
		}
	}
	
	if(mode != 0)
	{
		printk("insert end mark\n");
		mode->addr = (u16)END_SEQ;
		mode->data = (u16)0x0000;
	}

	return real_count/2;
}

static void cmc623_load_tuning_static_data()
{
	struct file *filp;
	char	*dp;
	long	l, i ;
	loff_t  pos;
	int     ret, num;
	mm_segment_t fs;

	klogi("cmc623_load_tuning_static_data start!");

	fs = get_fs();
	set_fs(get_ds());

	filp = filp_open(CMC623_PATH_STATIC_TUNING_DATA, O_RDONLY, 0);
	if(IS_ERR(filp)) 
	{
		kloge("file open error:%d", (s32)filp);
		return -1;
	}

	l = filp->f_path.dentry->d_inode->i_size;
	klogi("Size of the file : %ld(bytes)", l);

	//dp = kmalloc(l, GFP_KERNEL);
	dp = kmalloc(l+10, GFP_KERNEL);		// add cushion
	if(dp == NULL) 
	{
		kloge("Out of Memory!");
		filp_close(filp, current->files);
		return -1;
	}
	pos = 0;
	memset(dp, 0, l);
	kloge("== Before vfs_read ======");
	ret = vfs_read(filp, (char __user *)dp, l, &pos);   // P1_LSJ : DE08 : ¿©±â¼­ Á×À½ 
	kloge("== After vfs_read ======");

	if(ret != l) 
	{
		kloge("<CMC623> Failed to read file (ret = %d)", ret);
		kfree(dp);
		filp_close(filp, current->files);
		return -1;
	}

	filp_close(filp, current->files);

	set_fs(fs);
/*
	for(i=0; i<l; i++)
    {   
		printk("%x ", dp[i]);
    }
	printk("\n");
*/

	num = parse_text2(dp, l);

	if(!num) 
	{
		kloge("Nothing to parse!");
		kfree(dp);
		return -1;
	}
		
	printk("------ Jun Total number of parsed lines: %d\n", num);

	static_tune_loaded = 1;
	
//	cmc623_tune(num);

	kfree(dp);
	return num;
}
#endif

#endif	//CMC623_TUNING

static ssize_t mdnie_scenario_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	int mdnie_ui = 0;

	printk("called %s \n",__func__);

	switch(current_cmc623_UI)
	{
		case mDNIe_UI_MODE:
		default:	
			mdnie_ui = 0;
			break;

		case mDNIe_VIDEO_MODE:
			mdnie_ui = 1;
			break;

		case mDNIe_VIDEO_WARM_MODE:
			mdnie_ui = 2;
			break;

		case mDNIe_VIDEO_COLD_MODE:
			mdnie_ui = 3;
			break;
		
		case mDNIe_CAMERA_MODE:
			mdnie_ui = 4;
			break;

		case mDNIe_NAVI:
			mdnie_ui = 5;
			break;

		case mDNIe_DMB_MODE:
			mdnie_ui = 6;
			break;

		case mDNIe_VT_MODE:
			mdnie_ui = 7;
			break;

		case mDNIe_GALLERY_MODE:
			mdnie_ui = 8;
			break;
	}
	return sprintf(buf,"%u\n",mdnie_ui);
}

static ssize_t mdnie_scenario_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t size)
{
	int value;
	int mdnie;
	
    	sscanf(buf, "%d", &value);

	printk(KERN_INFO "[mdnie set] in mdnieset_ui_file_cmd_store, input value = %d \n",value);

	mdnie = 0;
	
	switch(value)
	{
		case SIG_MDNIE_UI_MODE:
			mdnie = mDNIe_UI_MODE;
			break;

		case SIG_MDNIE_VIDEO_MODE:
			mdnie = mDNIe_VIDEO_MODE;
			break;

		case SIG_MDNIE_VIDEO_WARM_MODE:
			mdnie = mDNIe_VIDEO_WARM_MODE;
			break;

		case SIG_MDNIE_VIDEO_COLD_MODE:
			mdnie = mDNIe_VIDEO_COLD_MODE;
			break;
		
		case SIG_MDNIE_CAMERA_MODE:
			mdnie = mDNIe_CAMERA_MODE;
			break;

		case SIG_MDNIE_NAVI:
			mdnie = mDNIe_NAVI;
			break;

		case SIG_MDNIE_DMB_MODE:
			mdnie = mDNIe_DMB_MODE;
			break;

		case SIG_MDNIE_VT_MODE:
			mdnie = mDNIe_VT_MODE;
			break;
			
		case SIG_MDNIE_GALLERY_MODE:
			mdnie = mDNIe_GALLERY_MODE;
			break;
			
		default:
			printk("\nmdnieset_ui_file_cmd_store value is wrong : value(%d)\n",value);
			break;
	}

	current_cmc623_OutDoor_OnOff = false;

	cmc623_Set_Mode_Ext((enum Lcd_CMC623_UI_mode)mdnie, current_cmc623_OutDoor_OnOff);

	return size;
}

static DEVICE_ATTR(mdnie_scenario,0664, mdnie_scenario_show, mdnie_scenario_store);

static ssize_t mdnie_bg_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	int mdnie_userselect = 0;

	printk("called %s \n",__func__);

	switch(current_cmc623_Userselect)
	{
		case mDNIe_USERSELECT_STANDARD_MODE:
		default:	
			mdnie_userselect = SIG_MDNIE_USERSELECT_STANDARD_MODE;
			break;

		case mDNIe_USERSELECT_MOVIE_MODE:
			mdnie_userselect = SIG_MDNIE_USERSELECT_MOVIE_MODE;
			break;

		case mDNIe_USERSELECT_DYNAMIC_MODE:
			mdnie_userselect = SIG_MDNIE_USERSELECT_DYNAMIC_MODE;
			break;
	}
	return sprintf(buf,"%u\n",mdnie_userselect);
}

static ssize_t mdnie_bg_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t size)
{
	int value;
	int mdnie_userselect;
	
    	sscanf(buf, "%d", &value);

	printk(KERN_INFO "[mdnie set] in mdnieset_ui_file_cmd_store, input value = %d \n",value);

	mdnie_userselect = 0;
	
	switch(value)
	{
		case SIG_MDNIE_USERSELECT_STANDARD_MODE:
			mdnie_userselect = mDNIe_USERSELECT_STANDARD_MODE;
			break;

		case SIG_MDNIE_USERSELECT_MOVIE_MODE:
			mdnie_userselect = mDNIe_USERSELECT_MOVIE_MODE;
			break;

		case SIG_MDNIE_USERSELECT_DYNAMIC_MODE:
			mdnie_userselect = mDNIe_USERSELECT_DYNAMIC_MODE;
			break;

		default:
			printk("mdnieset_user_select_file_cmd_store value is wrong : value(%d)\n",value);
			break;
	}

	cmc623_Set_Userselect_Ext(mdnie_userselect);
	//cmc623_Set_UserSelect_Mode((enum Lcd_CMC623_USERSELECT_mode)mdnie_userselect,current_cmc623_CABC_OnOff,0);

	return size;
}

static DEVICE_ATTR(mdnie_bg,0664, mdnie_bg_show, mdnie_bg_store);


static ssize_t mdnie_ove_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	printk("called %s \n",__func__);

	return sprintf(buf,"%u\n",current_cmc623_OutDoor_OnOff);
}

static ssize_t mdnie_ove_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t size)
{
	int value;
	int outdoor;
	
    	sscanf(buf, "%d", &value);

	printk(KERN_INFO "[mdnie set] in mdnie_ove_store, input value = %d \n",value);

	if(value)
	{
		outdoor = TRUE;
	}
	else
	{
		outdoor = FALSE;
	}

	cmc623_Set_Outdoor_Ext(current_cmc623_UI, outdoor);
			
	return size;
}

static DEVICE_ATTR(mdnie_ove,0664, mdnie_ove_show, mdnie_ove_store);

static ssize_t mdnie_cabc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	printk(KERN_INFO "%s \n", __func__);

	return sprintf(buf,"%u\n", current_cmc623_CABC_OnOff);
}

static ssize_t mdnie_cabc_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	char *endp;
	int enable;

	enable  =  simple_strtoul(buf, &endp, 0);
	printk(KERN_NOTICE "%s:%d\n", __func__, enable);

	//printk(KERN_INFO "[acl set] in aclset_file_cmd_store, input value = %d \n", value);

	/* In bypass mode, cabc is off */
	if (!cmc623_bypass_mode)
		cmc623_cabc_enable(enable);
	else
		pr_notice("%s:ignore cabc control because bypass mode is on\n", __func__);

	return size;
}

static DEVICE_ATTR(mdnie_cabc,0664, mdnie_cabc_show, mdnie_cabc_store);

static ssize_t mdnie_auto_ove_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	printk(KERN_INFO "%s \n", __func__);

	return sprintf(buf,"%u\n", current_autobrightness_enable);
}

static ssize_t mdnie_auto_ove_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t size)
{
	char *endp;
	int enable = simple_strtoul(buf, &endp, 0);
	printk(KERN_NOTICE "%s:%d\n", __func__, enable);
	
#ifdef __CMC623_AUTO_OVE_MODE__
	cmc623_autobrightness_enable(enable);
#else
	current_autobrightness_enable = enable;
#endif

	return size;
}

static DEVICE_ATTR(mdnie_auto_ove,0664, mdnie_auto_ove_show, mdnie_auto_ove_store);


static ssize_t tune_cmc623_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret=0;

#ifdef CMC623_TUNING
	klogi("");
	ret = cmc623_load_data();
#endif

	if(ret<0)
	{   
		return sprintf(buf, "FAIL\n");
	}
	else
	{   
		return sprintf(buf, "OK\n");
	}
}

static ssize_t tune_cmc623_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t size)
{
	return size;
}

static DEVICE_ATTR(tune, S_IRUGO | S_IWUSR, tune_cmc623_show, tune_cmc623_store);

static ssize_t set_reg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "<addr> <data> ex)0x00, 0x0000\n");
}

static ssize_t set_reg_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t size)
{
	int ret;
	u32 data1, data2;
	
	printk("[cmc623] %s : %s\n", __func__, buf);
	ret = sscanf(buf, "0x%x 0x%x\n", &data1, &data2);
	if(ret == 2)
		{
		printk("addr:0x%04x, data:0x%04x\n", data1, data2);
		cmc623_I2cWrite16(data1, data2);
		}
	else
		{
		printk("parse error num:%d, data:0x%04x, data:0x%04x\n", ret, data1, data2);
		}

	return size;
}

static DEVICE_ATTR(set_reg, 0664, set_reg_show, set_reg_store);

static u32 read_reg_address=0;

static ssize_t read_reg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret=0;
	u16 data2;
	
	printk("[cmc623] %s\n", __func__);
	printk("addr:0x%04x\n", read_reg_address);
	if(read_reg_address >= 0x100)
		ret = cmc623_I2cWrite16(0x00, 0x0001);
	else if(read_reg_address > 0x0)
		ret = cmc623_I2cWrite16(0x00, 0x0000);
	ret = cmc623_I2cRead16_direct(read_reg_address, &data2, TRUE);
	printk("data:0x%04x\n", data2);

    return sprintf(buf, "addr:0x%04x, data:0x%04x\n", read_reg_address, data2);
}

static ssize_t read_reg_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t size)
{
	int ret;
	u32 data1;
	u16 data2;
	
	printk("[cmc623] %s : %s\n", __func__, buf);
	ret = sscanf(buf, "0x%x\n", &data1);
	if(ret == 1)
		{
		read_reg_address = data1;
		printk("addr:0x%04x\n", data1);
		if(read_reg_address >= 0x100)
			ret = cmc623_I2cWrite16(0x00, 0x0001);
		else if(read_reg_address > 0x0)
			ret = cmc623_I2cWrite16(0x00, 0x0000);
		ret = cmc623_I2cRead16_direct(read_reg_address, &data2, TRUE);
		printk("data:0x%04x\n", data2);
		}
	else
		{
		printk("parse error num:%d, data:0x%04x\n", ret, data1);
		}

	return size;
}

static DEVICE_ATTR(read_reg, 0664, read_reg_show, read_reg_store);

static ssize_t show_regs_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "write 1 for reading all regs\n");
}

static ssize_t show_regs_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t size)
{
	int i;
	int ret;
	u32 data1;
	u16 data2;
	
	printk("[cmc623] %s : %s\n", __func__, buf);
	ret = sscanf(buf, "%d\n", &data1);
	if(ret == 1 && data1 == 1)
		{
		ret = cmc623_I2cWrite16(0x00, 0x0000);
		printk("BANK0\n");
		for(i=0;i<ARRAY_SIZE(all_regs_bank0);i++)
			{
			if(all_regs_bank0[i] == DELIMITER)
				{
				printk("------------------------\n");
				}
			else
				{
				ret = cmc623_I2cRead16_direct(all_regs_bank0[i], &data2, TRUE);
				printk("addr:0x%04x, data:0x%04x\n", all_regs_bank0[i], data2);
				}
			}
		ret = cmc623_I2cWrite16(0x00, 0x0001);
		printk("BANK1\n");
		for(i=0;i<ARRAY_SIZE(all_regs_bank1);i++)
			{
			if(all_regs_bank1[i] == DELIMITER)
				{
				printk("------------------------\n");
				}
			else
				{
				ret = cmc623_I2cRead16_direct(all_regs_bank1[i], &data2, TRUE);
				printk("addr:0x%04x, data:0x%04x\n", all_regs_bank1[i], data2);
				}
			}
		}
	else
		{
		printk("parse error num:%d, data:0x%04x\n", ret, data1);
		}

	printk("end %s\n", __func__);

	return size;
}

static DEVICE_ATTR(show_regs, 0664, show_regs_show, show_regs_store);

static ssize_t set_bypass_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	pr_info("called %s \n",__func__);

	return sprintf(buf,"%d\n",cmc623_bypass_mode);
}

static ssize_t set_bypass_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t size)
{
	//int i;
	int ret;
	u32 data1;
	//u16 data2;
	struct mDNIe_data_type *mode;
	
	printk("[cmc623] %s : %s\n", __func__, buf);
	ret = sscanf(buf, "%d\n", &data1);

	/* In bypass mode, cabc is off */
	mode = (struct mDNIe_data_type *)cmc623_Bypass;

	mutex_lock(&cmc623_state_transaction_lock);
	if(data1)
		{
		cmc623_bypass_mode = TRUE;
		while ( mode->addr != END_SEQ)
			{
			cmc623_I2cWrite16(mode->addr, mode->data);
			printk(KERN_INFO "[cmc623] a(0x%x),d(0x%x)\n",mode->addr, mode->data);	
			mode++;
			}
		cmc623_reg_unmask();
		
		/* In bypass mode, cabc is off */
		cmc623_manual_pwm_brightness(cmc623_state.brightness);
	
		}
	else
		{
		cmc623_bypass_mode = FALSE;

		cmc623_cabc_enable_flag(cmc623_state.cabc_enabled, 0/*CMC_FLAG_SETTING_FIRST*/);
		if(cmc623_state.cabc_enabled)
			cmc623_cabc_pwm_brightness(cmc623_state.brightness);
		else
			cmc623_manual_pwm_brightness(cmc623_state.brightness);
		}
	mutex_unlock(&cmc623_state_transaction_lock);
	
	printk("end %s\n", __func__);

	return size;
}

static DEVICE_ATTR(set_bypass, 0664, set_bypass_show, set_bypass_store);

#if 0
static ssize_t color_white_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf,"%d\n",cmc623_state.white);
}

static ssize_t color_white_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t size)
{
	int white;

	sscanf(buf, "%d", &white);
	printk(KERN_NOTICE "%s:%d\n", __func__, white);

	mutex_lock(&cmc623_state_transaction_lock);
	cmc623_state.white = white;
#if 0
	cmc623_Color_White_Change(cmc623_state.white,true);
#endif
	mutex_unlock(&cmc623_state_transaction_lock);

	return size;
}

static DEVICE_ATTR(color_white, 0664, color_white_show, color_white_store);

static ssize_t color_black_show(struct device *dev, struct device_attribute *attr, char *buf)
{
#if defined(CONFIG_TARGET_LOCALE_NTT)
	return sprintf(buf,"%d\n",-cmc623_state.black);
#else
	return sprintf(buf,"%d\n",cmc623_state.black);
#endif
}

static ssize_t color_black_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t size)
{
	int black;

	sscanf(buf, "%d", &black);
	printk(KERN_NOTICE "%s:%d\n", __func__, black);
	
	mutex_lock(&cmc623_state_transaction_lock);
	cmc623_state.black = black;

#if defined(CONFIG_TARGET_LOCALE_NTT)
	cmc623_state.black = -(cmc623_state.black);
#endif

#if 0
	cmc623_Color_Black_Change(cmc623_state.black,true);
#endif
	mutex_unlock(&cmc623_state_transaction_lock);

	return size;
}

static DEVICE_ATTR(color_black, 0664, color_black_show, color_black_store);

static ssize_t color_saturation_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf,"%d\n",cmc623_state.saturation);
}

static ssize_t color_saturation_store(struct device *dev, struct device_attribute *attr,const char *buf, size_t size)
{
	int saturation;

	sscanf(buf, "%d", &saturation);
	printk(KERN_NOTICE "%s:%d\n", __func__, saturation);
	
	mutex_lock(&cmc623_state_transaction_lock);
	cmc623_state.saturation = saturation;
#if 0
	cmc623_Color_Saturation_Change(cmc623_state.saturation,true);
#endif
	mutex_unlock(&cmc623_state_transaction_lock);

	return size;
}

static DEVICE_ATTR(color_saturation, 0666, color_saturation_show, color_saturation_store);
#endif

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


//static struct class *mdnieset_class;
static struct device *mdnie_dev;
//static struct device *cmc623_dev;

static void init_mdnie_class(struct device *mdnie_dev)
{
#if 0     
	mdnieset_class = class_create(THIS_MODULE, "mdnieset");
	if (IS_ERR(mdnieset_class))
		pr_err("Failed to create class(mdnie_class)!\n");

	mdnie_dev = device_create(mdnieset_class, NULL, 0, NULL, "mdnie");
#endif 
	if (IS_ERR(mdnie_dev))
		pr_err("Failed to create device(mdnieset_dev)!\n");

	if (device_create_file(mdnie_dev, &dev_attr_mdnie_scenario) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_mdnie_scenario.attr.name);

	if (device_create_file(mdnie_dev, &dev_attr_mdnie_bg) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_mdnie_bg.attr.name);

	if (device_create_file(mdnie_dev, &dev_attr_mdnie_ove) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_mdnie_ove.attr.name);

	if (device_create_file(mdnie_dev, &dev_attr_mdnie_cabc) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_mdnie_cabc.attr.name);

	if (device_create_file(mdnie_dev, &dev_attr_mdnie_auto_ove) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_mdnie_auto_ove.attr.name);
#if 0
	if (device_create_file(mdnie_dev, &dev_attr_color_white) < 0)
		pr_err("Failed to create device file!(%s)!\n", dev_attr_color_white.attr.name);

	if (device_create_file(mdnie_dev, &dev_attr_color_black) < 0)
		pr_err("Failed to create device file!(%s)!\n", dev_attr_color_black.attr.name);

	if (device_create_file(mdnie_dev, &dev_attr_color_saturation) < 0)
		pr_err("Failed to create device file!(%s)!\n", dev_attr_color_saturation.attr.name);
#endif
	if (device_create_file(mdnie_dev, &dev_attr_mdnie_roi) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_mdnie_roi.attr.name);
#if 0 
	cmc623_dev = device_create(mdnieset_class, NULL, 0, NULL, "cmc623-ext");
	if (IS_ERR(cmc623_dev))
		pr_err("Failed to create device(cmc623_dev)!\n");
#endif 
	if (device_create_file(mdnie_dev, &dev_attr_tune) < 0)
		printk("Failed to create device file!(%s)!\n", dev_attr_tune.attr.name);

	if (device_create_file(mdnie_dev, &dev_attr_set_reg) < 0)
		printk("Failed to create device file!(%s)!\n", dev_attr_set_reg.attr.name);

	if (device_create_file(mdnie_dev, &dev_attr_read_reg) < 0)
		printk("Failed to create device file!(%s)!\n", dev_attr_read_reg.attr.name);

	if (device_create_file(mdnie_dev, &dev_attr_show_regs) < 0)
		printk("Failed to create device file!(%s)!\n", dev_attr_show_regs.attr.name);

	if (device_create_file(mdnie_dev, &dev_attr_set_bypass) < 0)
		printk("Failed to create device file!(%s)!\n", dev_attr_set_bypass.attr.name);
}

int cmc623_service_suspend(void)
{
	printk(KERN_INFO "%s called\n", __func__);

	mutex_lock(&cmc623_state_transaction_lock);
	cmc623_state.suspended = TRUE;
	mutex_unlock(&cmc623_state_transaction_lock);

	return 0;
}

int cmc623_service_resume(void)
{
	printk(KERN_INFO "%s called\n", __func__);
		
	mutex_lock(&cmc623_state_transaction_lock);
	last_cmc623_Algorithm = 0xffff;
	last_cmc623_Bank = 0xffff;
	cmc623_state.suspended = FALSE;

	// restore mode & cabc status
	/*cmc623_state.brightness = 0;*/
	cmc623_cabc_enable_flag(cmc623_state.cabc_enabled, 0/*CMC_FLAG_SETTING_FIRST*/);
	if(cmc623_state.cabc_enabled)
		cmc623_cabc_pwm_brightness(cmc623_state.brightness);
	else
		cmc623_manual_pwm_brightness(cmc623_state.brightness);

	/* In bypass mode, cabc is off */
	if (cmc623_bypass_mode) {
		struct mDNIe_data_type *bypass_mode = &cmc623_Bypass[0];
		while (bypass_mode->addr != END_SEQ) {
			cmc623_I2cWrite16(bypass_mode->addr, bypass_mode->data);
			pr_notice("[cmc623] a(0x%x),d(0x%x)\n",bypass_mode->addr, bypass_mode->data);	
			bypass_mode++;
		}
		cmc623_reg_unmask();
	}	

#ifdef CMC623_TUNING
#ifdef CMC623_TUNE_STATIC
	if (static_tune_loaded)
	{
		struct mDNIe_data_type *static_tune_mode;
		static_tune_mode = &cmc623_static_Tune[0];
		while (static_tune_mode->addr != END_SEQ) {
			cmc623_I2cWrite16(static_tune_mode->addr, static_tune_mode->data);
			pr_notice("[cmc623] a(0x%x),d(0x%x)\n",static_tune_mode->addr, static_tune_mode->data);	
			static_tune_mode++;
		}
		cmc623_reg_unmask();
	}
	else
#endif
	{
		struct mDNIe_data_type *bypass_mode;
		if (cmc623_state.cabc_enabled)
			bypass_mode = &cmc623_Bypass_CABC[0];
		else
			bypass_mode = &cmc623_Bypass[0];
		while (bypass_mode->addr != END_SEQ) {
			cmc623_I2cWrite16(bypass_mode->addr, bypass_mode->data);
			pr_notice("[cmc623] a(0x%x),d(0x%x)\n",bypass_mode->addr, bypass_mode->data);	
			bypass_mode++;
		}
		cmc623_reg_unmask();
	}
#endif

	mutex_unlock(&cmc623_state_transaction_lock);
	return 0;
}

void init_cmc623_service(struct cmc623_data *pdata, struct device *mdnie_dev)
{
	printk("**** < %s >      ***********\n", __func__);

	p_cmc623_data = pdata;

	init_mdnie_class(mdnie_dev);

	cmc623_cabc_enable_flag(cmc623_state.cabc_enabled, 0/*CMC_FLAG_SETTING_FIRST|CMC_FLAG_SETTING_BOOT*/);

#ifdef CMC623_TUNING
	{
		struct mDNIe_data_type *bypass_mode;
		if (cmc623_state.cabc_enabled)
			bypass_mode = &cmc623_Bypass_CABC[0];
		else
			bypass_mode = &cmc623_Bypass[0];
		while (bypass_mode->addr != END_SEQ) {
			cmc623_I2cWrite16(bypass_mode->addr, bypass_mode->data);
			pr_notice("[cmc623] a(0x%x),d(0x%x)\n",bypass_mode->addr, bypass_mode->data);	
			bypass_mode++;
		}
		cmc623_reg_unmask();
	}
#endif

#ifdef __CMC623_AUTO_OVE_MODE__
	ove_wq = create_singlethread_workqueue("ove_wq");
	INIT_WORK(&work_ove, ove_workqueue_func);
#endif

}
