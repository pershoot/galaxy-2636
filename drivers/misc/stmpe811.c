#include <linux/types.h>
#include <linux/irq.h>
#include <linux/pm.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
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


#define STMPE811_CHIP_ID	0x00
#define STMPE811_ID_VER	0x02
#define STMPE811_SYS_CTRL1	0x03
#define STMPE811_SYS_CTRL2	0x04
#define STMPE811_INT_CTRL		0x09
#define STMPE811_INT_EN		0x0A
#define STMPE811_INT_STA		0x0B
#define STMPE811_ADC_INT_EN	0x0E
#define STMPE811_ADC_INT_STA	0x0F
#define STMPE811_ADC_CTRL1		0x20
#define STMPE811_ADC_CTRL2		0x21
#define STMPE811_ADC_CAPT		0x22
#define STMPE811_ADC_DATA_CH0	0x30
#define STMPE811_ADC_DATA_CH1	0x32
#define STMPE811_ADC_DATA_CH2	0x34
#define STMPE811_ADC_DATA_CH3	0x36
#define STMPE811_ADC_DATA_CH4	0x38
#define STMPE811_ADC_DATA_CH5	0x3A
#define STMPE811_ADC_DATA_CH6	0x3C
#define STMPE811_ADC_DATA_CH7	0x3E
#define STMPE811_GPIO_AF 		0x17
#define STMPE811_TSC_CTRL		0x40

#define klogi(fmt, arg...)  printk(KERN_INFO "%s: " fmt "\n" , __func__, ## arg)
#define kloge(fmt, arg...)  printk(KERN_ERR "%s(%d): " fmt "\n" , __func__, __LINE__, ## arg)

static struct device *sec_adc_dev;
extern struct class *sec_class;

static struct file_operations stmpe811_fops =
{
	.owner = THIS_MODULE,
};

static struct miscdevice stmpe811_adc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "sec_adc",
	.fops = &stmpe811_fops,
};

static struct i2c_driver stmpe811_adc_i2c_driver;
static struct i2c_client *stmpe811_adc_i2c_client = NULL;


struct stmpe811_adc_state{
	struct i2c_client	*client;	
	struct mutex	adc_lock;
};
struct stmpe811_adc_state *stmpe811_adc_state;


static int stmpe811_i2c_read(struct i2c_client *client, u8 reg, u8 *data, u8 length)
{
	int value;

	value = i2c_smbus_read_i2c_block_data(client, (u8)reg, length, data);
	if (value < 0)
		printk("%s: Failed to stmpe811_i2c_read, value: %d\n", __func__, value);
	
	return 0;
}

static int stmpe811_i2c_write(struct i2c_client *client, u8 reg, u8 *data, u8 length)
{
	u16 value;
	int ret_value;
	value=(*(data+1)) | (*(data)<< 8) ;
		
	ret_value = i2c_smbus_write_word_data(client, (u8)reg, swab16(value));
	if (ret_value < 0) {
		//printk("%s: Failed to stmpe811_i2c_write, ret_value: %d\n", __func__, ret_value);
		return ret_value;
	}

	return 0;
}

int stmpe811_write_register(u8 addr, u16 w_data)
{
	struct i2c_client *client = stmpe811_adc_i2c_client;
	u8 data[2];
	int value;

	data[0] = w_data & 0xFF;
	data[1] = (w_data >> 8);

	value = stmpe811_i2c_write(client, addr, data, (u8)2) ;
	if (value < 0) {
		printk("%s: stmpe811_write_register addr(0x%x), value: %d\n", __func__, addr, value);
		return value;
	}

	return 0;
}
#ifdef CONFIG_MACH_SAMSUNG_P5
s16 stmpe811_adc_get_value(u8 channel)
{
	struct i2c_client *client = stmpe811_adc_i2c_client;
	struct stmpe811_adc_state *adc = i2c_get_clientdata(client);
	s16 ret;
	u8 data[2];
	u16 w_data = 0;
	int data_channel_addr = 0;
	int count = 0;

	mutex_lock(&adc->adc_lock);

	ret = stmpe811_write_register(STMPE811_ADC_CAPT, (1 << channel)) ;

	if (ret < 0) {
		if (ret == -ENXIO || gpio_get_value(TEGRA_GPIO_PX3) == 0 ) {
			gpio_direction_output(TEGRA_GPIO_PX2, 0); // scl
			udelay(2);
			gpio_direction_output(TEGRA_GPIO_PX3, 0); // sda
			udelay(2);
			gpio_direction_output(TEGRA_GPIO_PX2, 1);
			udelay(2);
			gpio_direction_output(TEGRA_GPIO_PX3, 1);
			udelay(2);
			stmpe811_write_register(STMPE811_ADC_CAPT, (1 << channel));
		}
	}

	while(count < 10)
	{
		stmpe811_i2c_read(client, STMPE811_ADC_CAPT, data, (u8)1);

		//printk("%s: try count (%d)\n", __func__, count);
		if(data[0] & (1 << channel))
		{
			printk("%s: Confirmed new data in channel(%d) \n", __func__, channel);
			break;
		}
		
		msleep(1);
		count++;
	}
	
	data_channel_addr = STMPE811_ADC_DATA_CH0 + (channel * 2);
	msleep(10);

	/* read value from ADC */
	if (stmpe811_i2c_read(client, data_channel_addr, data, (u8)2) < 0) {
		printk("%s: Failed to read ADC_DATA_CH(%d).\n", __func__,channel);
		return -1;
	}

	w_data = ((data[0]<<8) | data[1]) & 0x0FFF;	
	printk("%s: ADC_DATA_CH(%d) = 0x%x, %d. \n", __func__,channel, w_data,w_data );

	stmpe811_write_register(STMPE811_ADC_CAPT, (1 << channel));
	
	ret = w_data;
	mutex_unlock(&adc->adc_lock);

	return ret;
}
#else //P3, P4, P4 LTE
s16 stmpe811_adc_get_value(u8 channel)
{
	struct i2c_client *client = stmpe811_adc_i2c_client;
	s16 ret;
	u8 data[2];
	u16 w_data;
	int data_channel_addr = 0;
	
	msleep(10);

	stmpe811_write_register(STMPE811_ADC_CAPT, (1 << channel));

	stmpe811_i2c_read(client, STMPE811_ADC_CAPT, data, (u8)1);
//	printk("STMPE811_ADC_CAPT = 0x%x..\n", data[0]);

	msleep(10);

	stmpe811_i2c_read(client, STMPE811_ADC_CAPT, data, (u8)1);
//	printk("STMPE811_ADC_CAPT = 0x%x..\n", data[0]);

	data_channel_addr = STMPE811_ADC_DATA_CH0 + (channel * 2);

//	printk("%s: data_channel_addr = 0x%x, channel = 0x%x\n", __func__,data_channel_addr, (1 << channel));

	/* read value from ADC */
	if (stmpe811_i2c_read(client, data_channel_addr, data, (u8)2) < 0) {
		printk("%s: Failed to read ADC_DATA_CH(%d).\n", __func__,channel);
		return -1;
	}

	w_data = ((data[0]<<8) | data[1]) & 0x0FFF;
	
	printk("%s: ADC_DATA_CH(%d) = 0x%x, %d. \n", __func__,channel, w_data,w_data );
	
	ret = w_data;

//	ret = ((data&0x0FFF)*2048)/4095;
	return ret;
}
#endif
EXPORT_SYMBOL(stmpe811_adc_get_value);

static ssize_t adc_test_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", "adc_test_show");
}

static ssize_t adc_test_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int mode;
	s16 val;
	sscanf(buf, "%d", &mode);

	if (mode <0 || mode >3) {
		kloge("invalid channel: %d", mode);
		return -EINVAL;
	}

	val = stmpe811_adc_get_value((u8)mode);
	klogi("value from ch%d: %d", mode, val);
	
	return count;		
}
static DEVICE_ATTR(adc_test, S_IRUGO | S_IWUSR | S_IWGRP,
		adc_test_show, adc_test_store);

#ifdef CONFIG_MACH_SAMSUNG_P5
static int stmpe811_suspend(struct i2c_client * client , pm_message_t mesg)
{
	stmpe811_adc_get_value(6);
	return 0;
}

static int stmpe811_resume(struct i2c_client * client )
{
	stmpe811_adc_get_value(6);
	return 0;
}
#endif /*CONFIG_MACH_SAMSUNG_P5*/

static int __init stmpe811_adc_init(void)
{
	int ret =0;

	klogi("start!");

	/*misc device registration*/
	ret = misc_register(&stmpe811_adc_device);
	if (ret < 0) {
		kloge("misc_register failed");
		return ret; 	  	
	}

	/* set sysfs for adc test mode*/
	sec_adc_dev = device_create(sec_class, NULL, 0, NULL, "sec_adc");
	if (IS_ERR(sec_adc_dev)) {
		kloge("failed to create device!\n");
		goto  DEREGISTER_MISC;
	}
	
	ret = device_create_file(sec_adc_dev, &dev_attr_adc_test);
	if (ret < 0) {
		kloge("failed to create device file(%s)!\n", dev_attr_adc_test.attr.name);
		goto DESTROY_DEVICE;
	}

	if (i2c_add_driver(&stmpe811_adc_i2c_driver))
		kloge("%s: Can't add fg i2c drv\n", __func__);
#if !defined(CONFIG_MACH_SAMSUNG_P5)
	gpio_request(GPIO_ADC_INT, "GPIO_ADC_INT");
	gpio_direction_input(GPIO_ADC_INT);
	tegra_gpio_enable(GPIO_ADC_INT);
#endif
	return 0;

DESTROY_DEVICE:
	device_destroy(sec_class, 0);
DEREGISTER_MISC:
	misc_deregister(&stmpe811_adc_device);

	return ret;
}

static void __init stmpe811_adc_exit(void)
{
	klogi("start!");

	i2c_del_driver(&stmpe811_adc_i2c_driver);

	device_remove_file(sec_adc_dev, &dev_attr_adc_test);
	device_destroy(sec_class, 0);

	misc_deregister(&stmpe811_adc_device);
}


static int stmpe811_adc_i2c_remove(struct i2c_client *client)
{
	struct stmpe811_adc_state *adc = i2c_get_clientdata(client);
	mutex_destroy(&adc->adc_lock);
	kfree(adc);
	return 0;
}

static int stmpe811_adc_i2c_probe(struct i2c_client *client,  const struct i2c_device_id *id)
{
	struct stmpe811_adc_state *adc;
	u8 data[2];
	u16 w_data;

	printk("stmpe811_adc_i2c_probe!!!\n");

	adc = kzalloc(sizeof(struct stmpe811_adc_state), GFP_KERNEL);
	if (adc == NULL) {		
		printk("failed to allocate memory \n");
		return -ENOMEM;
	}
	
	adc->client = client;
	i2c_set_clientdata(client, adc);

	stmpe811_adc_i2c_client = client;

	mutex_init(&adc->adc_lock);

	if (stmpe811_i2c_read(client, STMPE811_CHIP_ID, data, (u8)2) < 0) {
		printk("%s: Failed to read STMPE811_CHIP_ID.\n", __func__);
		return -1;
	}

	w_data = (data[0]<<8) | data[1];
	printk("%s: CHIP_ID = 0x%x. \n", __func__, w_data);

// init stmpe811 adc driver
	stmpe811_write_register(STMPE811_SYS_CTRL1, 0x02); // soft reset

	msleep(10);
	
#ifdef CONFIG_MACH_SAMSUNG_P5
	stmpe811_write_register(STMPE811_SYS_CTRL2, 0x0a); // enable adc & ts clock
	stmpe811_i2c_read(client, STMPE811_SYS_CTRL2, data, (u8)1);
	printk("STMPE811_SYS_CTRL2 = 0x%x..\n", data[0]);

	//in reference code, write 0x40 into STMPE811_INT_EN(0x0A) - enabling ADC interrupt
	stmpe811_write_register(STMPE811_INT_EN, 0x00); // disable interrupt
	stmpe811_i2c_read(client, STMPE811_INT_EN, data, (u8)1);
	printk("STMPE811_INT_EN = 0x%x..\n", data[0]);

	//in reference code, write 0x0 into STMPE811_ADC_CTRL1(0x20)
	stmpe811_write_register(STMPE811_ADC_CTRL1, 0x38); //64, 12bit, internal
	stmpe811_i2c_read(client, STMPE811_ADC_CTRL1, data, (u8)1);
	printk("STMPE811_ADC_CTRL1 = 0x%x..\n", data[0]);

	//in reference code, write 0x0 into STMPE811_ADC_CTRL2(0x21)
	stmpe811_write_register(STMPE811_ADC_CTRL2, 0x03); //clock speed 6.5MHz
	stmpe811_i2c_read(client, STMPE811_ADC_CTRL2, data, (u8)1);
	printk("STMPE811_ADC_CTRL2 = 0x%x..\n", data[0]);

//   It should be ADC settings. So the value should be 0x00 instead of 0xFF 
//   		2011.05.05 by Rami.Jung 
	stmpe811_write_register(STMPE811_GPIO_AF, 0x00); // gpio 0-3 -> ADC

	stmpe811_i2c_read(client, STMPE811_GPIO_AF, data, (u8)1);
	printk("STMPE811_GPIO_AF = 0x%x..\n", data[0]);

	//
	stmpe811_write_register(STMPE811_ADC_CAPT, 0xD0);

#else //P3, P4, P4 LTE
	stmpe811_write_register(STMPE811_SYS_CTRL2, 0x00); // enable adc & ts clock
	stmpe811_i2c_read(client, STMPE811_SYS_CTRL2, data, (u8)1);
	printk("STMPE811_SYS_CTRL2 = 0x%x..\n", data[0]);

	stmpe811_write_register(STMPE811_INT_EN, 0x00); // disable interrupt
	stmpe811_i2c_read(client, STMPE811_INT_EN, data, (u8)1);
	printk("STMPE811_INT_EN = 0x%x..\n", data[0]);
	
	stmpe811_write_register(STMPE811_ADC_CTRL1, 0x3C); //64, 12bit, internal
	stmpe811_i2c_read(client, STMPE811_ADC_CTRL1, data, (u8)1);
	printk("STMPE811_ADC_CTRL1 = 0x%x..\n", data[0]);

	stmpe811_write_register(STMPE811_ADC_CTRL2, 0x03); //clock speed 6.5MHz
	stmpe811_i2c_read(client, STMPE811_ADC_CTRL2, data, (u8)1);
	printk("STMPE811_ADC_CTRL2 = 0x%x..\n", data[0]);

//	stmpe811_write_register(STMPE811_GPIO_AF, 0xFF); // gpio 0-3 -> ADC

//   It should be ADC settings. So the value should be 0x00 instead of 0xFF 
//   		2011.05.05 by Rami.Jung 
	stmpe811_write_register(STMPE811_GPIO_AF, 0x00); // gpio 0-3 -> ADC

	stmpe811_i2c_read(client, STMPE811_GPIO_AF, data, (u8)1);
	printk("STMPE811_GPIO_AF = 0x%x..\n", data[0]);

	stmpe811_write_register(STMPE811_TSC_CTRL, 0x00);
	stmpe811_i2c_read(client, STMPE811_TSC_CTRL, data, (u8)1);
	printk("STMPE811_TSC_CTRL = 0x%x..\n", data[0]);
#endif

	printk("adc_i2c_probe success!!!\n");
	
	return 0;
}


static const struct i2c_device_id stmpe811_adc_device_id[] = {
	{"stmpe811", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, stmpe811_adc_device_id);


static struct i2c_driver stmpe811_adc_i2c_driver = {
	.driver = {
		.name = "stmpe811",
		.owner = THIS_MODULE,
	},
	.probe	= stmpe811_adc_i2c_probe,
	.remove	= stmpe811_adc_i2c_remove,
#ifdef CONFIG_MACH_SAMSUNG_P5
	.suspend		= stmpe811_suspend,
	.resume		= stmpe811_resume,
#endif /*CONFIG_MACH_SAMSUNG_P5*/
	.id_table	= stmpe811_adc_device_id,
};

module_init(stmpe811_adc_init);
module_exit(stmpe811_adc_exit);

MODULE_AUTHOR("Samsung");
MODULE_DESCRIPTION("Samsung STMPE811 ADC driver");
MODULE_LICENSE("GPL");
