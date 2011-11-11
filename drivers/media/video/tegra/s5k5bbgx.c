/*
 * s5k5bbgx.c - s5k5bbgx sensor driver
 *
 * Copyright (C) 2010 Google Inc.
 *
 * Contributors:
 *      Rebecca Schultz Zavin <rebecca@android.com>
 *
 * Leverage OV9640.c
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <media/s5k5bbgx.h>
#include <media/tegra_camera.h>

#ifndef CONFIG_MACH_SAMSUNG_P5W_KT
#ifdef CONFIG_MACH_SAMSUNG_P5
#include "s5k5bbgx_regs_p5.h"
#else
#include "s5k5bbgx_regs.h"
#endif
#else //homepad
#include "s5k5bbgx_regs_p5_kt.h"
#endif

#define DEBUG_PRINTS 0
#if DEBUG_PRINTS
	#define FUNC_ENTR	\
		printk(KERN_INFO "[S5K5BBGX] %s Entered!!!\n", __func__)
	//#define I2C_DEBUG 1
#else
	#define FUNC_ENTR
	//#define I2C_DEBUG 0
#endif

/* FOR S5K5BBGX TUNING */
//#define CONFIG_LOAD_FILE
#ifdef CONFIG_LOAD_FILE
#include <linux/vmalloc.h>
#include <linux/mm.h>
//#define max_size 200000

struct test
{
	char data;
	struct test *nextBuf;
};

struct test *testBuf;
#endif

#define S5K5BBGX_BURST_WRITE_LIST(A)	s5k5bbgx_sensor_burst_write_list(A,(sizeof(A) / sizeof(A[0])),#A);
struct s5k5bbgx_reg_8 {
	u8 addr;
	u8 val;
};

struct s5k5bbgx_info {
	int mode;
	struct i2c_client *i2c_client;
	struct s5k5bbgx_platform_data *pdata;
	struct s5k5bbgx_exif_info exif_info;
};


extern struct class *sec_class;
struct device *s5k5bbgx_dev;
#ifdef FACTORY_TEST
static s5k5bbgx_dtp_test dtpTest = S5K5BBGX_DTP_TEST_OFF;
#endif

#define S5K5BBGX_TABLE_WAIT_MS 0xFFFE
#define S5K5BBGX_TABLE_WAIT_MS_8 0xFE
#define S5K5BBGX_TABLE_END 0xFFFF
#define S5K5BBGX_TABLE_END_8 0xFF

#define S5K5BBGX_MAX_RETRIES 3
#define S5K5BBGX_READ_STATUS_RETRIES 50

enum {
#ifdef CONFIG_MACH_SAMSUNG_P5W_KT	// homepad
	S5K5BBGX_MODE_SENSOR_INIT,
	S5K5BBGX_MODE_PREVIEW_176x144,
	S5K5BBGX_MODE_PREVIEW_352x288,
	S5K5BBGX_MODE_PREVIEW_640x480,
	S5K5BBGX_MODE_PREVIEW_704x576,
	S5K5BBGX_MODE_PREVIEW_800x600,
	S5K5BBGX_MODE_CAPTURE_1600x1200,
#else
	S5K5BBGX_MODE_SENSOR_INIT,
	S5K5BBGX_MODE_PREVIEW_640x480,
	S5K5BBGX_MODE_PREVIEW_800x600,
	S5K5BBGX_MODE_CAPTURE_1600x1200,
#endif
#ifdef FACTORY_TEST
	S5K5BBGX_MODE_TEST_PATTERN,
	S5K5BBGX_MODE_TEST_PATTERN_OFF,
#endif
};

static const u32 *mode_table[] = {
#ifdef CONFIG_MACH_SAMSUNG_P5W_KT	// homepad
	[S5K5BBGX_MODE_SENSOR_INIT] = mode_sensor_init,
	[S5K5BBGX_MODE_PREVIEW_176x144] = mode_preview_176x144,
	[S5K5BBGX_MODE_PREVIEW_352x288] = mode_preview_352x288,
	[S5K5BBGX_MODE_PREVIEW_640x480] = mode_preview_640x480,
	[S5K5BBGX_MODE_PREVIEW_704x576] = mode_preview_704x576,
	[S5K5BBGX_MODE_PREVIEW_800x600] = mode_preview_800x600,
	[S5K5BBGX_MODE_CAPTURE_1600x1200] = mode_capture_1600x1200,
#else
	[S5K5BBGX_MODE_SENSOR_INIT] = mode_sensor_init,
	[S5K5BBGX_MODE_PREVIEW_640x480] = mode_preview_640x480,
	[S5K5BBGX_MODE_PREVIEW_800x600] = mode_preview_800x600,
	[S5K5BBGX_MODE_CAPTURE_1600x1200] = mode_capture_1600x1200,
#endif
#ifdef FACTORY_TEST
	[S5K5BBGX_MODE_TEST_PATTERN] = mode_test_pattern,
	[S5K5BBGX_MODE_TEST_PATTERN_OFF] = mode_test_pattern_off,
#endif
};

static int s5k5bbgx_read_reg(struct i2c_client *client, u16 addr, u8 *val, u16 length)
{
	int err;
	struct i2c_msg msg[2];
	unsigned char data[2];

	if (!client->adapter)
		return -ENODEV;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = data;

	/* high byte goes out first */
	data[0] = (u8) (addr >> 8);
	data[1] = (u8) (addr & 0xff);

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = length;
	msg[1].buf = val;
	err = i2c_transfer(client->adapter, msg, 2);

	if (err != 2)
		return -EINVAL;

	return 0;
}
static int s5k5bbgx_write_reg(struct i2c_client *client, u16 addr, u16 val)
{
#if 1
	int err;
	struct i2c_msg msg;
	unsigned char data[4];
	int retry = 0;

	if (!client->adapter)
		return -ENODEV;

	data[0] = (u8) (addr >> 8);
	data[1] = (u8) (addr & 0xff);
	data[2] = (u8) (val >> 8);
	data[3] = (u8) (val & 0xff);

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = 4;
	msg.buf = data;

	do {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			return 0;
		retry++;
		pr_err("s5k5bbgx: i2c transfer failed, retrying %x %x\n",
		       addr, val);
		msleep(3);
	} while (retry <= S5K5BBGX_MAX_RETRIES);
	if (err != 1) {
		pr_err("%s: I2C is not working\n", __func__);
		return -EIO;
	}

	return 0;
#endif
}

static int s5k5bbgx_write_table(struct i2c_client *client, const u32 table[])
{
	int err;
	int i;
	const u32 *next = table;
	u16 val;
	for (next = table; ((*next >> 16) & 0xFFFF) != S5K5BBGX_TABLE_END; next++) {
		if (((*next >> 16) & 0xFFFF) == S5K5BBGX_TABLE_WAIT_MS) {
			msleep(*next  & 0xFFFF);
			continue;
		}

		val = (u16)(*next  & 0xFFFF);

		err = s5k5bbgx_write_reg(client, (*next >> 16) & 0xFFFF, val);
		if (err < 0)
			return err;
	}

	return 0;
}

#ifdef CONFIG_LOAD_FILE
static inline int s5k5bbgx_write(struct i2c_client *client,
		u16 addr_reg, u16 data_reg)
{
	struct i2c_msg msg[1];
	unsigned char buf[4];
	int ret = 1;

	buf[0] = ((addr_reg >>8)&0xFF);
	buf[1] = (addr_reg)&0xFF;
	buf[2] = ((data_reg >>8)&0xFF);
	buf[3] = (data_reg)&0xFF;
	
	msg->addr = client->addr;
	msg->flags = 0;
	msg->len = 4;
	msg->buf = buf;


	ret = i2c_transfer(client->adapter, &msg, 1);

	if (unlikely(ret < 0)) {
		dev_err(&client->dev, "%s: (a:%x,d:%x) write failed\n", __func__, addr_reg,data_reg);
		return ret;
	}
	
	return (ret != 1) ? -1 : 0;
}

static int s5k5bbgx_write_tuningmode(struct i2c_client *client, char s_name[])
{
	int ret = -EAGAIN;
	u32 temp;
	//unsigned long temp;
	char delay = 0;
	char data[11];
	int searched = 0;
	int size = strlen(s_name);
	int i;
	unsigned int addr_reg;
	unsigned int data_reg;

	struct test *tempData, *checkData;

	printk("size = %d, string = %s\n", size, s_name);
	tempData = &testBuf[0];
	while(!searched)
	{
		searched = 1;
		for (i = 0; i < size; i++)
		{
			if (tempData->data != s_name[i])
			{
				searched = 0;
				break;
			}
			tempData = tempData->nextBuf;
		}
		tempData = tempData->nextBuf;
	}
	//structure is get..

	while(1)
	{
		if (tempData->data == '{')
			break;
		else
			tempData = tempData->nextBuf;
	}


	while (1)
	{
		searched = 0;
		while (1)
		{
			if (tempData->data == 'x')
			{
				//get 10 strings
				data[0] = '0';
				for (i = 1; i < 11; i++)
				{
					data[i] = tempData->data;
					tempData = tempData->nextBuf;
				}
				//printk("%s\n", data);
				temp = simple_strtoul(data, NULL, 16);
				break;
			}
			else if (tempData->data == '}')
			{
				searched = 1;
				break;
			}
			else
				tempData = tempData->nextBuf;
			if (tempData->nextBuf == NULL)
				return -1;
		}

		if (searched)
			break;

		//let search...
		if ((temp & 0xFFFE0000) == 0xFFFE0000) {                                                    
			delay = temp & 0xFFFF;                                                                              
			printk("[kidggang]:func(%s):line(%d):delay(0x%x):delay(%d)\n",__func__,__LINE__,delay,delay);       
			msleep(delay);                                                                                      
			continue;                                                                                           
		}
		else if ((temp & 0xFFFFFFFF) == 0xFFFFFFFF) { 
			printk("[kidggang]:FFFFFFFFFFFFFFFFFFFFFFFFFF\n");       
			continue;
		}
//		ret = s5k5bbgx_write(client, temp);

		addr_reg = (temp >> 16)&0xffff;
		data_reg = (temp & 0xffff);

		ret = s5k5bbgx_write(client, addr_reg, data_reg);
		
		//printk("addr = %x    data = %x\n", addr_reg, data_reg);
		//printk("data = %x\n", data_reg);

		/* In error circumstances */
		/* Give second shot */
		if (unlikely(ret)) {
			dev_info(&client->dev,
					"s5k5bbgx i2c retry one more time\n");
			ret = s5k5bbgx_write(client, addr_reg, data_reg);

			/* Give it one more shot */
			if (unlikely(ret)) {
				dev_info(&client->dev,
						"s5k5bbgx i2c retry twice\n");
				ret = s5k5bbgx_write(client, addr_reg, data_reg);
			}
		}
	}
	return ret;
}

static int loadFile(void){

	struct file *fp;
	char *nBuf;
	unsigned int max_size;
	unsigned int l;
	struct test *nextBuf = testBuf;
	int i = 0;

	int starCheck = 0;
	int check = 0;
	int ret = 0;
	loff_t pos;

	mm_segment_t fs = get_fs();
	set_fs(get_ds());

	fp = filp_open("/mnt/sdcard/s5k5bbgx_regs_p5.h", O_RDONLY, 0);

	if (IS_ERR(fp)) {
		printk("%s : file open error\n", __func__);
		return PTR_ERR(fp);
	}

	l = (int) fp->f_path.dentry->d_inode->i_size;

	max_size = l;
	
	printk("l = %d\n", l);
	nBuf = kmalloc(l, GFP_KERNEL);
	testBuf = (struct test*)kmalloc(sizeof(struct test) * l, GFP_KERNEL);

	if (nBuf == NULL) {
		printk( "Out of Memory\n");
		filp_close(fp, current->files);
	}
	pos = 0;
	memset(nBuf, 0, l);
	memset(testBuf, 0, l * sizeof(struct test));

	ret = vfs_read(fp, (char __user *)nBuf, l, &pos);

	if (ret != l) {
		printk("failed to read file ret = %d\n", ret);
		kfree(nBuf);
		kfree(testBuf);
		filp_close(fp, current->files);
		return -1;
	}

	filp_close(fp, current->files);

	set_fs(fs);

	i = max_size;

	printk("i = %d\n", i);

	while (i){
		testBuf[max_size - i].data = *nBuf;
		if (i != 1)
		{
			testBuf[max_size - i].nextBuf = &testBuf[max_size - i + 1];
		}
		else
		{
			testBuf[max_size - i].nextBuf = NULL;
			break;
		}
		i--;
		nBuf++;
	}

	i = max_size;
	nextBuf = &testBuf[0];
#if 1
	while (i - 1){
		if (!check && !starCheck){
			if (testBuf[max_size - i].data == '/')
			{
				if(testBuf[max_size-i].nextBuf != NULL)
				{
					if (testBuf[max_size-i].nextBuf->data == '/')
					{
						check = 1;// when find '//'
						i--;
					}
					else if (testBuf[max_size-i].nextBuf->data == '*')
					{
						starCheck = 1;// when find '/*'
						i--;
					}
				}	
				else
					break;
			}
			if (!check && !starCheck)
				if (testBuf[max_size - i].data != '\t')//ignore '\t'
				{
					nextBuf->nextBuf = &testBuf[max_size-i];
					nextBuf = &testBuf[max_size - i];
				}

		}
		else if (check && !starCheck)
		{
			if (testBuf[max_size - i].data == '/')
			{
				if(testBuf[max_size-i].nextBuf != NULL)
				{
					if (testBuf[max_size-i].nextBuf->data == '*')
					{
						starCheck = 1;// when find '/*'
						check = 0;
						i--;
					}
				}	
				else 
					break;
			}

			if(testBuf[max_size - i].data == '\n' && check) // when find '\n'
			{
				check = 0;
				nextBuf->nextBuf = &testBuf[max_size - i];
				nextBuf = &testBuf[max_size - i];
			}

		}
		else if (!check && starCheck)
		{
			if (testBuf[max_size - i].data == '*')
			{
				if(testBuf[max_size-i].nextBuf != NULL)
				{
					if (testBuf[max_size-i].nextBuf->data == '/')
					{
						starCheck = 0;// when find '*/'
						i--;
					}
				}	
				else
					break;
			}

		}
		i--;
		if (i < 2) {
			nextBuf = NULL;
			break;
		}
		if (testBuf[max_size - i].nextBuf == NULL)
		{
			nextBuf = NULL;
			break;
		}
	}
#endif
#if 0 // for print
	printk("i = %d\n", i);
	nextBuf = &testBuf[0];
	while (1)
	{
		//printk("sdfdsf\n");
		if (nextBuf->nextBuf == NULL)
			break;
		printk("%c", nextBuf->data);
		nextBuf = nextBuf->nextBuf;
	}
#endif
	return 0;
}
#endif

int s5k5bbgx_set_exif_info(struct s5k5bbgx_info *info)
{
	u8 r_value[2] = {0};
	u16 t_value;
	u16 iso_value;
	int err;

	err = s5k5bbgx_write_table(info->i2c_client, mode_exif_shutterspeed);
	err =  s5k5bbgx_read_reg(info->i2c_client, 0x0F12, r_value, 2);
	t_value = r_value[1] + (r_value[0] << 8);
	pr_debug("Shutterspeed = %d, r_value[1] = %d, r_value[0] = %d\n", t_value, r_value[1], r_value[0]);
	info->exif_info.info_exptime_numer = t_value*100 / 400;
	info->exif_info.info_exptime_denumer = 1000*100;

	err = s5k5bbgx_write_table(info->i2c_client, mode_exif_iso);
	err =  s5k5bbgx_read_reg(info->i2c_client, 0x0F12, r_value, 2);
	t_value = r_value[1] + (r_value[0] << 8);
	pr_debug("ISO = %d, r_value[1] = %d, r_value[0] =%d\n", t_value, r_value[1], r_value[0]);

	iso_value = t_value * 100 / 256;

	if (iso_value < 150)
		info->exif_info.info_iso = 50;
	else if (iso_value < 250)
		info->exif_info.info_iso = 100;
	else if (iso_value < 350)
		info->exif_info.info_iso = 200;
	else
		info->exif_info.info_iso = 400;

	return err;
}

static int s5k5bbgx_set_esd_reset(struct s5k5bbgx_info *info, enum s5k5bbgx_esd_reset arg)
{
	if (arg == FRONT_ESD_DETECTED) {
		int err;
		info->pdata->power_off();
		info->pdata->power_on();
		err = s5k5bbgx_write_table(info->i2c_client, mode_preview_800x600);
		if (err < 0)
			return err;
	}
	return 0;
}


static int s5k5bbgx_set_mode(struct s5k5bbgx_info *info, struct s5k5bbgx_mode *mode)
{
	int sensor_mode;
	int err;

	pr_info("%s: xres %u yres %u\n", __func__, mode->xres, mode->yres);

	if (mode->xres == 800 && mode->yres == 600) {
	#ifdef FACTORY_TEST
		if (dtpTest) {
			sensor_mode = S5K5BBGX_MODE_TEST_PATTERN;
			printk("DTP ON DTP ON !!!\n");
		}
		else
	#endif
		sensor_mode = S5K5BBGX_MODE_PREVIEW_800x600;
        } else if (mode->xres == 640 && mode->yres == 480) {
	#ifdef FACTORY_TEST
		if (dtpTest) {
			sensor_mode = S5K5BBGX_MODE_TEST_PATTERN;	
			printk("DTP ON DTP ON 222 !!!\n");
		}
		else
	#endif
		sensor_mode = S5K5BBGX_MODE_PREVIEW_640x480;
	} 
#ifdef CONFIG_MACH_SAMSUNG_P5W_KT	// homepad
	else if (mode->xres == 176 && mode->yres == 144)
		sensor_mode = S5K5BBGX_MODE_PREVIEW_176x144;
	else if (mode->xres == 352 && mode->yres == 288)
		sensor_mode = S5K5BBGX_MODE_PREVIEW_352x288;
	else if (mode->xres == 704 && mode->yres == 576)
		sensor_mode = S5K5BBGX_MODE_PREVIEW_704x576;
#endif
	else if (mode->xres == 1600 && mode->yres == 1200)
		sensor_mode = S5K5BBGX_MODE_CAPTURE_1600x1200;
	else {
		pr_err("%s: invalid resolution supplied to set mode %d %d\n",
		       __func__, mode->xres, mode->yres);
		return -EINVAL;
	}

#ifdef CONFIG_LOAD_FILE
	if(sensor_mode == S5K5BBGX_MODE_PREVIEW_800x600)
		err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_preview_800x600");
	else if(sensor_mode == S5K5BBGX_MODE_PREVIEW_640x480)
		err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_preview_640x480");
	else if(sensor_mode == S5K5BBGX_MODE_CAPTURE_1600x1200)
		err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_capture_1600x1200");
#else
	err = s5k5bbgx_write_table(info->i2c_client, mode_table[sensor_mode]);
	msleep(150);//added by ykh
#endif
	if (sensor_mode == S5K5BBGX_MODE_CAPTURE_1600x1200) {
		u8 val[2] = {0};
		u8 retry = 0;
		pr_debug("%s: sensor_ mode is S5K5BBGX_MODE_CAPTURE_1600x1200---!!\n", __func__);
#ifndef CONFIG_LOAD_FILE
		s5k5bbgx_set_exif_info(info);
#endif
		do {
			msleep(20);
#ifndef CONFIG_LOAD_FILE
			err = s5k5bbgx_write_table(info->i2c_client, mode_check_capture_staus);
#else
			err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_check_capture_staus");
#endif
			if (err < 0)
				return err;
			err = s5k5bbgx_read_reg(info->i2c_client, 0x0F12, val, 2);
			if (err < 0)
				return err;
			if ((val[1]+(val[0]<<8)) == 0)
				break;
			retry++;
		} while (retry <= S5K5BBGX_READ_STATUS_RETRIES);

	}
	if (err < 0)
		return err;

	info->mode = sensor_mode;
	return 0;
}


static int s5k5bbgx_set_color_effect(struct s5k5bbgx_info *info, enum s5k5bbgx_color_effect arg)
{
	FUNC_ENTR;

	int err = 0;
#ifndef CONFIG_LOAD_FILE
	switch (arg) {
	case FRONT_EFFECT_NONE:
		err = s5k5bbgx_write_table(info->i2c_client, mode_coloreffect_none);
		break;

	case FRONT_EFFECT_MONO:
		err = s5k5bbgx_write_table(info->i2c_client, mode_coloreffect_mono);
		break;

	case FRONT_EFFECT_SEPIA:
		err = s5k5bbgx_write_table(info->i2c_client, mode_coloreffect_sepia);
		break;

	case FRONT_EFFECT_NEGATIVE:
		err = s5k5bbgx_write_table(info->i2c_client, mode_coloreffect_negative);
		break;

	default:
		pr_err("%s: Invalid Color Effect, %d\n", __func__, arg);
		return 0;
		break;
	}

	if (err < 0)
		pr_err("%s: s5k5bbgx_write_table() returned error, %d, %d\n", __func__, arg, err);
#endif
	return err;
}


static int s5k5bbgx_set_white_balance(struct s5k5bbgx_info *info, enum s5k5bbgx_white_balance arg)
{
	FUNC_ENTR;

	int err;
#ifdef CONFIG_LOAD_FILE
	switch (arg) {
	case FRONT_WB_AUTO:
		err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_WB_auto");
		pr_info("%s: mode_wb_auto_tuning, %d\n", __func__, arg);
		break;

	case FRONT_WB_DAYLIGHT:
		err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_WB_daylight");
		pr_info("%s: mode_wb_daylight_tuning, %d\n", __func__, arg);
		break;

	case FRONT_WB_INCANDESCENT:
		err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_WB_incandescent");
		pr_info("%s: mode_wb_incandescent_tuning, %d\n", __func__, arg);
		break;

	case FRONT_WB_FLUORESCENT:
		err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_WB_fluorescent");
		pr_info("%s: mode_wb_fluorescent_tuning, %d\n", __func__, arg);
		break;

	case FRONT_WB_CLOUDY:
		err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_WB_cloudy");
		pr_info("%s: mode_wb_cloudy_tuning, %d\n", __func__, arg);
		break;

	default:
		pr_err("%s: Invalid White Balance, %d\n", __func__, arg);
		return 0;
		break;
	}
#else
	switch (arg) {
	case FRONT_WB_AUTO:
		err = s5k5bbgx_write_table(info->i2c_client, mode_WB_auto);
		break;

	case FRONT_WB_DAYLIGHT:
		err = s5k5bbgx_write_table(info->i2c_client, mode_WB_daylight);
		break;

	case FRONT_WB_INCANDESCENT:
		err = s5k5bbgx_write_table(info->i2c_client, mode_WB_incandescent);
		break;

	case FRONT_WB_FLUORESCENT:
		err = s5k5bbgx_write_table(info->i2c_client, mode_WB_fluorescent);
		break;

	case FRONT_WB_CLOUDY:
		err = s5k5bbgx_write_table(info->i2c_client, mode_WB_cloudy);
		break;

	default:
		pr_err("%s: Invalid White Balance, %d\n", __func__, arg);
		return 0;
		break;
	}
#endif	

	if (err < 0)
		pr_err("%s: s5k5bbgx_write_table() returned error, %d, %d\n", __func__, arg, err);

	return err;
}


static int s5k5bbgx_set_exposure(struct s5k5bbgx_info *info, enum s5k5bbgx_exposure arg)
{
	FUNC_ENTR;

	int err = 0;
#ifndef CONFIG_LOAD_FILE
	switch (arg) {
	case FRONT_EXPOSURE_P4:
		err = s5k5bbgx_write_table(info->i2c_client, mode_exposure_p4);
		break;

	case FRONT_EXPOSURE_P3:
		err = s5k5bbgx_write_table(info->i2c_client, mode_exposure_p3);
		break;
		
	case FRONT_EXPOSURE_P2:
		err = s5k5bbgx_write_table(info->i2c_client, mode_exposure_p2);
		break;

	case FRONT_EXPOSURE_P1:
		err = s5k5bbgx_write_table(info->i2c_client, mode_exposure_p1);
		break;

	case FRONT_EXPOSURE_ZERO:
		err = s5k5bbgx_write_table(info->i2c_client, mode_exposure_0);
		break;

	case FRONT_EXPOSURE_M1:
		err = s5k5bbgx_write_table(info->i2c_client, mode_exposure_m1);
		break;

	case FRONT_EXPOSURE_M2:
		err = s5k5bbgx_write_table(info->i2c_client, mode_exposure_m2);
		break;

	case FRONT_EXPOSURE_M3:
		err = s5k5bbgx_write_table(info->i2c_client, mode_exposure_m3);
		break;

	case FRONT_EXPOSURE_M4:
		err = s5k5bbgx_write_table(info->i2c_client, mode_exposure_m4);
		break;		

	default:
		pr_err("%s: Invalid Exposure Value, %d\n", __func__, arg);
		return 0;
		break;
	}

	if (err < 0)
		pr_err("%s: s5k5bbgx_write_table() returned error, %d, %d\n", __func__, arg, err);
#else
	switch (arg) {
		case FRONT_EXPOSURE_P4:
			err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_exposure_p4");
			break;
	
		case FRONT_EXPOSURE_P3:
			err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_exposure_p3");
			break;
			
		case FRONT_EXPOSURE_P2:
			err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_exposure_p2");
			break;
	
		case FRONT_EXPOSURE_P1:
			err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_exposure_p1");
			break;
	
		case FRONT_EXPOSURE_ZERO:
			err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_exposure_0");
			break;
	
		case FRONT_EXPOSURE_M1:
			err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_exposure_m1");
			break;
	
		case FRONT_EXPOSURE_M2:
			err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_exposure_m2");
			break;
	
		case FRONT_EXPOSURE_M3:
			err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_exposure_m3");
			break;
	
		case FRONT_EXPOSURE_M4:
			err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_exposure_m4");
			break;		
	
		default:
			pr_err("%s: Invalid Exposure Value, %d\n", __func__, arg);
			return 0;
			break;
		}
	
		if (err < 0)
			pr_err("%s: s5k5bbgx_write_table() returned error, %d, %d\n", __func__, arg, err);
#endif
	return err;
}

static int s5k5bbgx_set_recording_frame(struct s5k5bbgx_info *info, enum s5k5bbgx_recording_frame arg)
{
	FUNC_ENTR;

	int err;

	pr_debug("test recording frame!!!\n");

	switch (arg) {
	case FIXED_FRAME:
	pr_debug("test recording frame - fix!!!\n");
#ifdef CONFIG_LOAD_FILE
		err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_preview_800x600_fixframe");
		pr_info("s5k5bbgx_set_recording_frame - fix(tuning)   err(%d)!!!\n", err);
#else
#ifdef CONFIG_MACH_SAMSUNG_P5W_KT	// homepad
		err = s5k5bbgx_write_table(info->i2c_client, mode_preview_fixframe_30fps);
#else
		err = s5k5bbgx_write_table(info->i2c_client, mode_preview_800x600_fixframe);
#endif
#endif
		break;

	case VARIABLE_FRAME:
	pr_debug("test recording frame - variable!!!\n");
#ifdef CONFIG_LOAD_FILE
		err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_return_camera_preview");
		pr_info("s5k5bbgx_set_recording_frame - variable(tuning)    err(%d)!!!\n", err);
#else	
		err = s5k5bbgx_write_table(info->i2c_client, mode_return_camera_preview);
#endif
		break;

	default:
		pr_err("%s: Invalid recording frame Value, %d\n", __func__, arg);
		return 0;
		break;
	}

	if (err < 0)
		pr_err("%s: s5k5bbgx_write_table() returned error, %d, %d\n", __func__, arg, err);

	return err;
}

static int s5k5bbgx_set_frame_rate(struct s5k5bbgx_info *info, enum s5k5bbgx_cam_mode arg)
{
	FUNC_ENTR;

	int err;

	pr_debug("test recording frame!!!\n");
	switch (arg) {
	case FRONT_CAMMODE_CAMCORDER:
	{
	pr_debug("test recording frame - fix!!!\n");
#ifdef CONFIG_LOAD_FILE
		err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_preview_800x600_fixframe");
		pr_info("s5k5bbgx_set_recording_frame - fix(tuning)   err(%d)!!!\n", err);
#else
#ifdef CONFIG_MACH_SAMSUNG_P5W_KT	// homepad
		err = s5k5bbgx_write_table(info->i2c_client, mode_preview_fixframe_30fps);
#else
		err = s5k5bbgx_write_table(info->i2c_client, mode_preview_800x600_fixframe);
#endif
#endif
		break;
	}
	case FRONT_CAMMODE_CAMERA:
	{
	pr_debug("test recording frame - variable!!!\n");	
#ifdef CONFIG_LOAD_FILE
		err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_return_camera_preview");
		pr_info("s5k5bbgx_set_recording_frame - variable(tuning)    err(%d)!!!\n", err);
#else	
		err = s5k5bbgx_write_table(info->i2c_client, mode_return_camera_preview);
#endif
		break;
	}
	case FRONT_CAMMODE_MMS_CAMCORDER:
   	{
#ifdef CONFIG_LOAD_FILE
		err = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_preview_800x600_fixframe_15fps");
		pr_info("s5k5bbgx_set_recording_frame - fix(tuning)   err(%d)!!!\n", err);
#else
#ifdef CONFIG_MACH_SAMSUNG_P5W_KT	// homepad
		err = s5k5bbgx_write_table(info->i2c_client, mode_preview_fixframe_15fps);
#else
		err = s5k5bbgx_write_table(info->i2c_client, mode_preview_800x600_fixframe_15fps);
#endif
#endif
		break;
   	}
	default:
	{
		pr_info("==================================test recording frame - DEFAULT DEFAULT DEFAULT !!!\n");	
		pr_err("%s: Invalid recording frame Value, %d\n", __func__, arg);
		return 0;
		break;
	}
	}

	if (err < 0)
		pr_err("%s: s5k5bbgx_write_table() returned error, %d, %d\n", __func__, arg, err);

	return err;
}


#ifdef FACTORY_TEST
static int s5k5bbgx_return_normal_preview(struct s5k5bbgx_info *info)
{
	int err;
	printk("DTP OFF DTP OFF !!!\n");
	err = s5k5bbgx_write_table(info->i2c_client, mode_test_pattern_off);
	if (err < 0)
		pr_err("%s: s5k5bbgx_write_table() returned error, %d\n", __func__, err);

	return 0;
}
#endif
static long s5k5bbgx_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	FUNC_ENTR;

	struct s5k5bbgx_mode mode;
	struct s5k5bbgx_info *info = file->private_data;

	switch (cmd) {
	case S5K5BBGX_IOCTL_SET_MODE:
		if (copy_from_user(&mode, (const void __user *)arg, sizeof(struct s5k5bbgx_mode))) {
			pr_info("%s %d\n", __func__, __LINE__);
			return -EFAULT;
		}
		return s5k5bbgx_set_mode(info, &mode);

	case S5K5BBGX_IOCTL_COLOR_EFFECT:
		return s5k5bbgx_set_color_effect(info, (enum s5k5bbgx_color_effect) arg);

	case S5K5BBGX_IOCTL_WHITE_BALANCE:
		return s5k5bbgx_set_white_balance(info, (enum s5k5bbgx_white_balance) arg);

	case S5K5BBGX_IOCTL_EXPOSURE:
		return s5k5bbgx_set_exposure(info, (enum s5k5bbgx_exposure) arg);
#ifdef FACTORY_TEST
	case S5K5BBGX_IOCTL_DTP_TEST:
	{
		int status = 0;
		pr_err( "[S5K5BBGX]%s: S5K5BBGX_IOCTL_DTP_TEST Entered!!! dtpTest = %d\n", __func__, (s5k5bbgx_dtp_test) arg);
		if (dtpTest == 1 && (s5k5bbgx_dtp_test) arg == 0)
			status = s5k5bbgx_return_normal_preview(info);
		dtpTest = (s5k5bbgx_dtp_test) arg;
		return status;
	}
#endif

	case S5K5BBGX_IOCTL_ESD_RESET:
		return s5k5bbgx_set_esd_reset(info, (enum s5k5bbgx_esd_reset) arg);

	case S5K5BBGX_IOCTL_RECORDING_FRAME:
	{
		return s5k5bbgx_set_recording_frame(info, (enum s5k5bbgx_recording_frame) arg);
	}

	case S5K5BBGX_IOCTL_EXIF_INFO:
	{
		if (copy_to_user((void __user *)arg, &info->exif_info,
					sizeof(info->exif_info))) {
			return -EFAULT;
		}
		break;
	}
       case S5K5BBGX_IOCTL_CAMMODE:
	{
	   	return s5k5bbgx_set_frame_rate(info, (enum s5k5bbgx_cam_mode) arg);
   	}
	default:
		return -EINVAL;
	}

	return 0;
}

static ssize_t cameratype_file_cmd_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	char sensor_info[30] = "SLSI_S5K5BBGX";
	return sprintf(buf, "%s\n", sensor_info);
}

static ssize_t cameratype_file_cmd_store(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t size)
{
		/*Reserved*/
}

static DEVICE_ATTR(camtype, 0660, cameratype_file_cmd_show, cameratype_file_cmd_store);

static struct s5k5bbgx_info *info;

#define BURST_MODE_BUFFER_MAX_SIZE 2700
unsigned char s5k5bbgx_buf_for_burstmode[BURST_MODE_BUFFER_MAX_SIZE];

static int s5k5bbgx_sensor_burst_write_list(const u32 list[], int size, char *name)	
{
	int err = -EINVAL;
	int i = 0;
	int idx = 0;

	u16 subaddr = 0, next_subaddr = 0;
	u16 value = 0;

	struct i2c_msg msg = {  info->i2c_client->addr, 0, 0, s5k5bbgx_buf_for_burstmode};
	


	for (i = 0; i < size; i++)
	{

		if(idx > (BURST_MODE_BUFFER_MAX_SIZE - 10))
		{
			printk("BURST MODE buffer overflow!!!\n");
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
					s5k5bbgx_buf_for_burstmode[idx++] = 0x0F;
					s5k5bbgx_buf_for_burstmode[idx++] = 0x12;
				}
				s5k5bbgx_buf_for_burstmode[idx++] = value >> 8;
				s5k5bbgx_buf_for_burstmode[idx++] = value & 0xFF;

			
			 	//write in burstmode
				if(next_subaddr != 0x0F12)
				{
					msg.len = idx;
					err = i2c_transfer(info->i2c_client->adapter, &msg, 1) == 1 ? 0 : -EIO;
					//printk("s5k5ccgx_sensor_burst_write, idx = %d\n",idx);
					idx=0;
				}
				
			}
			break;

			case 0xFFFF:
			{
				pr_info("burst_mode --- end of REGISTER \n");
				err = 0;
				idx = 0;
			}
			break;

			case 0xFFFE:
			{
				pr_info("burst_mode --- s5k5ccgx_i2c_write_twobyte give delay: %d\n", value);				
				msleep(value);
			}
			break;

			default:
			{
			    idx = 0;
			    err = s5k5bbgx_write_reg(info->i2c_client, subaddr, value);
			}
			break;
			
		}

		
	}

	if (unlikely(err < 0))
	{
		printk("%s: register set failed\n",__func__);
		return err;
	}
	return 0;

}

static int s5k5bbgx_open(struct inode *inode, struct file *file)
{
	FUNC_ENTR;

	int status = 0;
	int err;
	//struct s5k5bbgx_info *pinfo;

	file->private_data = info;
	//pinfo = (struct s5k5bbgx_info *)file->private_data;

	if (info->pdata && info->pdata->power_on)
		info->pdata->power_on();
	msleep(10);//ykh
	err = s5k5bbgx_write_reg(info->i2c_client, 0x0028, 0x7000);
	if(err !=0)
		return err;
#ifdef CONFIG_LOAD_FILE
	err = loadFile();
	if (unlikely(err)) {
		printk("%s: failed to init\n", __func__);
		return err;
	}
	status = s5k5bbgx_write_tuningmode(info->i2c_client, "mode_sensor_init");
#else	
	//status = s5k5bbgx_write_table(pinfo->i2c_client, mode_table[0]);
	status = S5K5BBGX_BURST_WRITE_LIST(mode_sensor_init);
#endif
	if (status < 0)
		info->pdata->power_off();
	return status;
}

int s5k5bbgx_release(struct inode *inode, struct file *file)
{
	FUNC_ENTR;

	struct s5k5bbgx_info * pinfo = (struct s5k5bbgx_info *)file->private_data;

	if (pinfo->pdata && pinfo->pdata->power_off)
		pinfo->pdata->power_off();
	return 0;
}


static const struct file_operations s5k5bbgx_fileops = {
	.owner = THIS_MODULE,
	.open = s5k5bbgx_open,
	.unlocked_ioctl = s5k5bbgx_ioctl,
	.compat_ioctl = s5k5bbgx_ioctl,
	.release = s5k5bbgx_release,
};

static struct miscdevice s5k5bbgx_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "s5k5bbgx",
	.fops = &s5k5bbgx_fileops,
};

static int s5k5bbgx_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	FUNC_ENTR;

	int err;

	info = kzalloc(sizeof(struct s5k5bbgx_info), GFP_KERNEL);
	if (!info) {
		pr_err("s5k5bbgx: Unable to allocate memory!\n");
		return -ENOMEM;
	}
	err = misc_register(&s5k5bbgx_device);
	if (err) {
		pr_err("s5k5bbgx: Unable to register misc device!\n");
		kfree(info);
		return err;
	}

	info->i2c_client = client;

	if (client->dev.platform_data == NULL) {
		pr_err("s5k5bbgx probe: client->dev.platform_data is NULL!\n");
		return -ENXIO;
	}

	info->pdata = client->dev.platform_data;

	i2c_set_clientdata(client, info);

	s5k5bbgx_dev = device_create(sec_class, NULL, 0, NULL, "sec_s5k5bbgx");

	if (IS_ERR(s5k5bbgx_dev)) {
		printk("Failed to create s5k5bbgx_dev device!");
		return -1;
	}

	if (device_create_file(s5k5bbgx_dev, &dev_attr_camtype) < 0) {
		printk("Failed to create device file!(%s)!\n", dev_attr_camtype.attr.name);
		return -1;
	}	

	return 0;
}

static int s5k5bbgx_remove(struct i2c_client *client)
{
	FUNC_ENTR;

	struct s5k5bbgx_info *info;
	info = i2c_get_clientdata(client);
	misc_deregister(&s5k5bbgx_device);
	kfree(info);
	return 0;
}

static const struct i2c_device_id s5k5bbgx_id[] = {
	{ "s5k5bbgx", 0 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, s5k5bbgx_id);

static struct i2c_driver s5k5bbgx_i2c_driver = {
	.driver = {
		.name = "s5k5bbgx",
		.owner = THIS_MODULE,
	},
	.probe = s5k5bbgx_probe,
	.remove = s5k5bbgx_remove,
	.id_table = s5k5bbgx_id,
};

static int __init s5k5bbgx_init(void)
{
	FUNC_ENTR;

	int status;
	pr_info("s5k5bbgx sensor driver loading\n");

	status = i2c_add_driver(&s5k5bbgx_i2c_driver);
	if (status) {
		printk(KERN_ERR "s5k5bbgx error\n");
		return status;
	}
	return 0;
}

static void __exit s5k5bbgx_exit(void)
{
	FUNC_ENTR;

	i2c_del_driver(&s5k5bbgx_i2c_driver);
}

module_init(s5k5bbgx_init);
module_exit(s5k5bbgx_exit);


