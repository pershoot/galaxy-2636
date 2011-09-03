/*
 * arch/arm/mach-tegra/tegra_adc.c
 *
 * Copyright (c) 2009, Samsung Electronics., Ltd.
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

#include <linux/types.h>
#include <linux/irq.h>
#include <linux/pm.h>
#include <linux/delay.h>
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
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <mach/gpio.h>
#include <mach/gpio-sec.h>

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define ADC_REG_SETUP 0x1
#define ADC_REG_CONFIG 0x0

/* select Ref. Voltage and state of AIN_/REF */
#define ADC_SB_SEL 0x7  // internal reference 2.048V
/* select clock type */
#define ADC_SB_CLK_INT 0x0  // internal
#define ADC_SB_CLK_EXT 0x1  // external
/* select bipolar or unipolar */
#define ADC_SB_POL_UNI 0x0  // unipolar
#define ADC_SB_POL_BI 0x1  // bipolar
/* select whether to reset config. register */
#define ADC_SB_RST 0x0  // reset
#define ADC_SB_RST_NO 0x1  // no action

/* select scan mode */
#define ADC_CB_SCAN_CONVERT 0x3
/* select single-ended or differential */
#define ADC_CB_SCAN_TYPE_SE 0x1  // single-ended
#define ADC_CB_SCAN_TYPE_DF 0x0  // differential

#define MAX1237_I2C_SLAVE_ADDR (0x34 << 1)
#define MAX1237_I2C_SPEED_KHZ  400

#define klogi(fmt, arg...)  printk(KERN_INFO "%s: " fmt "\n" , __func__, ## arg)
#define kloge(fmt, arg...)  printk(KERN_ERR "%s(%d): " fmt "\n" , __func__, __LINE__, ## arg)


extern unsigned int system_rev;


struct i2c_driver max1237_i2c_driver;
static struct i2c_client *max1237_i2c_client = NULL;

struct max1237_state {
	struct i2c_client *client;
};

static struct i2c_device_id max1237_id[] = {
	{"max1237", 0},
	{}
};

static int max1237_i2c_read(u16 *val)
{
	int 	 err;
	struct	 i2c_msg msg[1];
	u8 data[2];

	if( (max1237_i2c_client == NULL))  
	{
		printk("%s max1237_i2c_client is NULL\n", __func__);
		return -ENODEV;
	}

	if (!max1237_i2c_client->adapter) 
	{
		printk("%s max1237_i2c_client is NULL\n", __func__);
		return -ENODEV;
	}
	
	msg[0].addr   = max1237_i2c_client->addr;
	msg[0].flags 	= 1;
	msg[0].len	 = 2;
	msg[0].buf	 = &data[0];
	err = i2c_transfer(max1237_i2c_client->adapter, &msg[0], 1);

	if (err >= 0) 
	{
		*val = (data[0]<<8) | data[1];
		return 0;
	}
	printk(KERN_ERR "%s %d i2c transfer error: %d\n", __func__, __LINE__, err);

	return err;
}

static int max1237_i2c_write(u8 data)
{
	struct i2c_client *client = max1237_i2c_client;
	if(client == NULL)
		return -ENODEV;

	return i2c_smbus_write_byte(client, data);
}


s16 adc_get_value(u8 channel)
{
	u8 byte_config=0;
	u16 data=0;
	s16 ret;

	msleep(5);

	/* write config byte */
	byte_config = (ADC_REG_CONFIG<<7)|(ADC_CB_SCAN_CONVERT<<5)|(channel<<1)|(ADC_CB_SCAN_TYPE_SE);
	if (max1237_i2c_write(byte_config)<0) {
		return -1;
       }

	/* read value from ADC */
	if (max1237_i2c_read(&data)<0) {
		return -1;
       }

	ret = ((data&0x0FFF)*2048)/4095;

//	klogi(" : channel = %d, ret = %d.", channel, ret);
	
	return ret;
}
EXPORT_SYMBOL(adc_get_value);

static int __init max1237_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct max1237_state *state;
	struct device *dev = &client->dev;

	klogi("%s\n", __func__);

	 state = kzalloc(sizeof(struct max1237_state), GFP_KERNEL);
	 if(!state) {
		 dev_err(dev, "%s: failed to create max1237_state\n", __func__);
		 return -ENOMEM;
	 }

	state->client = client;
	max1237_i2c_client = client;

	i2c_set_clientdata(client, state);
	if(!max1237_i2c_client)
	{
		dev_err(dev, "%s: failed to create max1237_i2c_client\n", __func__);
		return -ENODEV;
	}

	return 0;
}


static int __devexit max1237_remove(struct i2c_client *client)
{
	struct fsa9480_state *state = i2c_get_clientdata(client);
	kfree(state);
	return 0;
}

struct i2c_driver max1237_i2c_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "max1237",
	},
	.id_table	= max1237_id,
	.probe	= max1237_probe,
	.remove	= __devexit_p(max1237_remove),
	.command = NULL,
};

static int __init max1237_init(void)
{
 	int ret;

	//if(get_hwversion()<HW_EMUL_REVISION00){
	if(system_rev<0x2/*emul00*/){

		klogi("%s\n", __func__);

		if((ret = i2c_add_driver(&max1237_i2c_driver)))
			kloge("Can't add max1237_init i2c driver\n");

		return ret;
	}
	else{
		return 0;
	}
}

static void __init max1237_exit(void)
{
	klogi("%s\n", __func__);
	i2c_del_driver(&max1237_i2c_driver);
}


module_init(max1237_init);
module_exit(max1237_exit);

MODULE_AUTHOR("Samsung");
MODULE_DESCRIPTION("Samsung Tegra ADC driver");
MODULE_LICENSE("GPL");
