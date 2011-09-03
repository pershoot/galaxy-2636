/******************** (C) COPYRIGHT 2010 STMicroelectronics ********************
*
* File Name		: stmpe1801-gpio.c
* Authors		: Sensor & MicroActuators BU - Application Team
*				: Bela Somaiah
* Version		: V 1.5 
* Date			: 12/05/2011
* Description	: STMPE1801-GPIO
*
********************************************************************************
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* THE PRESENT SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES
* OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, FOR THE SOLE
* PURPOSE TO SUPPORT YOUR APPLICATION DEVELOPMENT.
* AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY DIRECT,
* INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING FROM THE
* CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE BY CUSTOMERS OF THE CODING
* INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
*
* THIS SOFTWARE IS SPECIFICALLY DESIGNED FOR EXCLUSIVE USE WITH ST PARTS.
*
********************************************************************************
* REVISON HISTORY
* VERSION | DATE 	   | AUTHORS	     | DESCRIPTION
* 1.5     | 12/05/2011 | Bela Somaiah    | 6th Release, reduced/eliminated delays  
* 1.4     | 28/04/2011 | Bela Somaiah    | 5th Release, fixed stmpe1801_gpio_irq_set_type() 
* 1.3     | 20/04/2011 | Bela Somaiah    | 4th Release, includes setclr function 
* 1.2     | 11/04/2011 | Bela Somaiah    | 3rd Release GPIO Interrupt 
* 1.1     | 07/04/2011 | Bela Somaiah    | 2nd Release Polling and Interrupt 
* 1.0     | 05/04/2011 | Bela Somaiah    | 1st Release Polling Method
*******************************************************************************/
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/hrtimer.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/serio.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/i2c/stmpe1801.h>
#include <linux/irq.h>


/*
 * Definitions & global arrays.
 */
#define DRIVER_DESC             "stmpe1801 i2c gpio driver"
#define DRV_NAME                "stmpe1801"

#define MAXGPIO                 18

#define STMPE_INT_STA	        0x08
#define STMPE_INT_STA_GPIO      0x0D
#define STMPE_INT_EN_GPIO_MASK  0x0A

#define STMPE_GPIO_SET_yyy      0x10
#define STMPE_GPIO_CLR_yyy      0x13
#define STMPE_GPIO_MP_yyy       0x16
#define STMPE_GPIO_SET_DIR_yyy  0x19
#define STMPE_GPIO_RE_yyy       0x1C
#define STMPE_GPIO_FE_yyy       0x1F
#define STMPE_GPIO_PULL_UP_yyy  0x22


/*
 * These registers are modified under the irq bus lock and cached to avoid
 * unnecessary writes in bus_sync_unlock.
 */
enum { REG_RE, REG_FE, REG_IE };

#define CACHE_NR_REGS	3
#define CACHE_NR_BANKS	3

struct stmpe1801_gpio {
	struct i2c_client *client;
	struct gpio_chip gpio_chip;
	unsigned gpio_start;
	struct mutex lock;
	struct mutex irq_lock;
	int irq_base;
	int irq;

	/* Caches of interrupt control registers for bus_lock */
	u8 regs[CACHE_NR_REGS][CACHE_NR_BANKS];
	u8 oldregs[CACHE_NR_REGS][CACHE_NR_BANKS];
};

static inline struct stmpe1801_gpio *to_stmpe_gpio(struct gpio_chip *chip)
{
	return container_of(chip, struct stmpe1801_gpio, gpio_chip);
}

static int stmpe1801_read_reg(struct i2c_client *client, unsigned char reg[], int cnum, u8 *buf, int num)
{
	struct i2c_msg xfer_msg[2];

	xfer_msg[0].addr = client->addr;
	xfer_msg[0].len = cnum;
	xfer_msg[0].flags = 0;
	xfer_msg[0].buf = reg;

	xfer_msg[1].addr = client->addr;
	xfer_msg[1].len = num;
	xfer_msg[1].flags = I2C_M_RD;
	xfer_msg[1].buf = buf;

	return i2c_transfer(client->adapter, xfer_msg, 2);
}

static int stmpe1801_write_reg(struct stmpe1801_gpio *stmpe1801_gpio, unsigned char reg[], u8 num_com)
{
	int rc;

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	if(num_com > 1)
		pr_debug( "stmpe1801_write_reg wr:0x%x 0x%x(c:%d)\n", reg[0], reg[1], num_com);
#endif

	rc = i2c_master_send(stmpe1801_gpio->client, reg, num_com);
	if(rc < 0) {
		printk("stmpe1801_write_reg: i2c_master_send failed\n");
		return rc;
	}
	return 0;
}

static int get_stmpe1801_gpio(struct stmpe1801_gpio *stmpe1801_gpio, u8 regAdd, int gpioNum, u8 *setreset)
{
	int ret = 0;
	u8 val[4];
	int iVal = 0;
	int compare = 0;
	u8 registerAdd[1];
	registerAdd[0] = regAdd;

	if((gpioNum >= 0) && (gpioNum < 18)) {
		ret = stmpe1801_read_reg(stmpe1801_gpio->client, registerAdd, 1, val, 3);

		iVal = (val[2] << 16) | (val[1] << 8) | val[0];

		compare = (1 << gpioNum);
		compare &= iVal;

		if(compare == 0)
			*setreset = 0;
		else
			*setreset = 1;

		return ret;
	}
	else
		return -1;
}

static int set_stmpe1801_gpio(struct stmpe1801_gpio *stmpe1801_gpio, u8 regAdd, int gpioNum, int setreset)
{
	int ret = 0;
	u8 val[4];
	int iVal = 0;
	u8 registerAdd[1];
	registerAdd[0] = regAdd;

	if((gpioNum >= 0) && (gpioNum < 18)) {
		mutex_lock(&stmpe1801_gpio->lock);
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
		printk("set gpio %d to %d at reg add %x\n", gpioNum, setreset, registerAdd[0]);		
#endif

		ret = stmpe1801_read_reg(stmpe1801_gpio->client, registerAdd, 1, val, 3);

		iVal = (val[2] << 16) |( val[1] << 8) | val[0];
		if(setreset == 1) iVal = iVal | (1 << gpioNum);
		else iVal = iVal & ~(1 << gpioNum);

		val[0] = regAdd;
		val[1] = iVal;
		val[2] = (iVal >> 8);
		val[3] = (iVal >> 16);

		ret = stmpe1801_write_reg(stmpe1801_gpio, &val[0], 4);

		mutex_unlock(&stmpe1801_gpio->lock);

		return ret; 
	}
	else
		return -1;
}

static void setclr_stmpe1801_gpio(struct stmpe1801_gpio *stmpe1801_gpio, u8 regAdd, int gpioNum)
{
	u8 val[4] = {0, 0, 0, 0};
	u32 iVal = 0;
	u8 registerAdd[1];
	registerAdd[0] = regAdd;

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	printk("setclr gpio %d at reg add %x\n", gpioNum, registerAdd[0]);		
#endif
	iVal = (val[2] << 16) |( val[1] << 8) | val[0];
	iVal = iVal | (1 << gpioNum);

	val[0] = regAdd;
	val[1] = iVal;
	val[2] = (iVal >> 8);
	val[3] = (iVal >> 16);

	mutex_lock(&stmpe1801_gpio->lock);
	stmpe1801_write_reg(stmpe1801_gpio, &val[0], 4);
	mutex_unlock(&stmpe1801_gpio->lock);
}
 
static void stmpe1801_gpio_set(struct gpio_chip *chip, unsigned offset, int val)
{
	struct stmpe1801_gpio *stmpe1801_gpio = to_stmpe_gpio(chip);

	if(val == 1) {
		setclr_stmpe1801_gpio(stmpe1801_gpio, STMPE_GPIO_SET_yyy, offset);
	}
	else {
		setclr_stmpe1801_gpio(stmpe1801_gpio, STMPE_GPIO_CLR_yyy, offset);  
	}
}

static int stmpe1801_gpio_direction_output(struct gpio_chip *chip,unsigned offset, int val)
{
	struct stmpe1801_gpio *stmpe1801_gpio = to_stmpe_gpio(chip);

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	/* set val */
	stmpe1801_gpio_set(chip, offset, val);
#endif
	return set_stmpe1801_gpio(stmpe1801_gpio, STMPE_GPIO_SET_DIR_yyy, offset, 1); 
}

static int stmpe1801_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	u8 bitStat;
	int compare;
	int ret;

	struct stmpe1801_gpio *stmpe1801_gpio = to_stmpe_gpio(chip);

	compare = (1 << offset);

	ret = get_stmpe1801_gpio(stmpe1801_gpio, STMPE_GPIO_MP_yyy, offset, &bitStat); 

	if(ret < 0)
		return ret;

	if(bitStat == 1)
		return compare;
	else
		return 0;
}

static int stmpe1801_gpio_direction_input(struct gpio_chip *chip,unsigned offset)
{
	struct stmpe1801_gpio *stmpe1801_gpio = to_stmpe_gpio(chip);

	return set_stmpe1801_gpio(stmpe1801_gpio,  STMPE_GPIO_SET_DIR_yyy, offset, 0);
}

static int stmpe1801_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct stmpe1801_gpio *stmpe1801_gpio = to_stmpe_gpio(chip);

	return stmpe1801_gpio->irq_base + offset;
}

static irqreturn_t gpio_interrupt(int irq, void *handle)
{
	u8 val[8];
	u8 regAdd[7];
	int rc, i;
	int iVal = 0;
	int compare = 0;

	struct stmpe1801_gpio *stmpe1801_gpio = handle;	

	regAdd[0]=STMPE_INT_STA;
	rc=stmpe1801_read_reg(stmpe1801_gpio->client, regAdd, 1, val, 2);

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	printk( "stmpe1801 sta:0x%x 0x%x\n", val[0], val[1]);
#endif

	if(rc < 0)
		return IRQ_NONE;

	if((val[0] & 0x08) == 0x08) {//GPIO controller int received
		regAdd[0]=STMPE_INT_STA_GPIO; 
		rc=stmpe1801_read_reg(stmpe1801_gpio->client, regAdd, 1, val, 3);

		iVal = (val[2] << 16) | (val[1] << 8) | val[0];

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
		printk( "stmpe1801 sta_gpio:0x%x\n", iVal);
#endif
		for (i = 0; i < MAXGPIO; i++) {
			compare = 0; 
			compare = (1 << i);
			compare &= iVal;

			if(compare) {
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
				printk( "stmpe1801 irq:0x%x\n", i);
#endif
				handle_nested_irq(stmpe1801_gpio->irq_base + i);
			}
		}
	}

	return IRQ_HANDLED;
}

static int stmpe1801_gpio_irq_set_type(unsigned int irq, unsigned int type)
{
	struct stmpe1801_gpio *stmpe1801_gpio = get_irq_chip_data(irq);
	int offset = irq - stmpe1801_gpio->irq_base;
	int regoffset = offset / 8;
	u8 mask = 1 << (offset % 8);

	if (type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH))
		return -EINVAL;

	if (type & IRQ_TYPE_EDGE_RISING)
		stmpe1801_gpio->regs[REG_RE][regoffset] |= mask;
	else
		stmpe1801_gpio->regs[REG_RE][regoffset] &= ~mask;

	if (type & IRQ_TYPE_EDGE_FALLING)
		stmpe1801_gpio->regs[REG_FE][regoffset] |= mask;
	else
		stmpe1801_gpio->regs[REG_FE][regoffset] &= ~mask;

	return 0;
}

static void stmpe1801_gpio_irq_lock(unsigned int irq)
{
	struct stmpe1801_gpio *stmpe1801_gpio = get_irq_chip_data(irq);

	mutex_lock(&stmpe1801_gpio->irq_lock);
}

static void stmpe1801_gpio_irq_sync_unlock(unsigned int irq)
{
	int i, j;
	struct stmpe1801_gpio *stmpe1801_gpio = get_irq_chip_data(irq);
	int num_banks = DIV_ROUND_UP(MAXGPIO, 8);
	static const u8 regmap[] = {
		[REG_RE]	= STMPE_GPIO_RE_yyy,
		[REG_FE]	= STMPE_GPIO_FE_yyy,
		[REG_IE]	= STMPE_INT_EN_GPIO_MASK,
	};

	for (i = 0; i < CACHE_NR_REGS; i++) {
		for (j = 0; j < num_banks; j++) {
			u8 old = stmpe1801_gpio->oldregs[i][j];
			u8 new = stmpe1801_gpio->regs[i][j];
			u8 val[2];

			if (new == old)
				continue;

			stmpe1801_gpio->oldregs[i][j] = new;
			val[0] = regmap[i]+j;
			val[1] = new;
			
			mutex_lock(&stmpe1801_gpio->lock);
			stmpe1801_write_reg(stmpe1801_gpio, val, 2);
			mutex_unlock(&stmpe1801_gpio->lock);
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
			printk( "stmpe1801 wr:0x%x 0x%x\n", val[0], val[1]);
#endif
		}
	}
	
	mutex_unlock(&stmpe1801_gpio->irq_lock); 
}

static void stmpe1801_gpio_irq_mask(unsigned int irq)
{
	struct stmpe1801_gpio *stmpe1801_gpio = get_irq_chip_data(irq);
	int offset = irq - stmpe1801_gpio->irq_base;
	int regoffset = offset / 8;
	u8 mask = 1 << (offset % 8);

	stmpe1801_gpio->regs[REG_IE][regoffset] &= ~mask;
	//set_stmpe1801_gpio(stmpe1801_gpio, STMPE_INT_EN_GPIO_MASK, offset, 0);
}

static void stmpe1801_gpio_irq_unmask(unsigned int irq)
{
	struct stmpe1801_gpio *stmpe1801_gpio = get_irq_chip_data(irq);
	int offset = irq - stmpe1801_gpio->irq_base;
	int regoffset = offset / 8;
	u8 mask = 1 << (offset % 8);

	stmpe1801_gpio->regs[REG_IE][regoffset] |= mask;
	//set_stmpe1801_gpio(stmpe1801_gpio, STMPE_INT_EN_GPIO_MASK, offset, 1);
}

static struct irq_chip stmpe1801_gpio_irq_chip = {
	.name                   = "stmpe1801_gpio",
	.bus_lock               = stmpe1801_gpio_irq_lock,
	.bus_sync_unlock        = stmpe1801_gpio_irq_sync_unlock,
	.mask                   = stmpe1801_gpio_irq_mask,
	.unmask                 = stmpe1801_gpio_irq_unmask,
	.set_type               = stmpe1801_gpio_irq_set_type,
};

static int stmpe1801_gpio_irq_init(struct stmpe1801_gpio *stmpe1801_gpio)
{
	int base = stmpe1801_gpio->irq_base;
	int irq;

	for (irq = base; irq < base + stmpe1801_gpio->gpio_chip.ngpio; irq++) {
		set_irq_chip_data(irq, stmpe1801_gpio);
		set_irq_chip_and_handler(irq, &stmpe1801_gpio_irq_chip, handle_simple_irq);
		set_irq_nested_thread(irq, 1);
#ifdef CONFIG_ARM
		set_irq_flags(irq, IRQF_VALID);
#else
		set_irq_noprobe(irq);
#endif
	}

	return 0;
}
static int stmpe1801_gpio_irq_remove(struct stmpe1801_gpio *stmpe1801_gpio)
{
	int base = stmpe1801_gpio->irq_base;
	int irq;

	for (irq = base; irq < base + stmpe1801_gpio->gpio_chip.ngpio; irq++) {
#ifdef CONFIG_ARM
		set_irq_flags(irq, 0);
#endif
		set_irq_chip_and_handler(irq, NULL, NULL);
		set_irq_chip_data(irq, NULL);
	}

	return 0;
}

static int init_stmpe1801_gpio(struct stmpe1801_gpio *stmpe1801_gpio)
{
	u8 regAdd[7];

	regAdd[0]=0x02; //SYS_CTRL
	regAdd[1]=0x80; //soft reset, 30us debounce time 
	stmpe1801_write_reg(stmpe1801_gpio, &regAdd[0],2);
	udelay(100);	// wait for reset
	
	regAdd[0]=0x06; // INT_ENABLE_MASK_LOW
	regAdd[1]=0x08; // GPIO Controller INT Mask only 
	stmpe1801_write_reg(stmpe1801_gpio, &regAdd[0],2);
	
#if !defined CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	regAdd[0]=0x0A; // INT_EN_GPIO_MASK_LOW
	regAdd[1]=0x0F; // IO 0, IO 1, IO 2, IO 3 can generate an interrupt
	stmpe1801_write_reg(stmpe1801_gpio, &regAdd[0],2);
#endif
	
	regAdd[0]=0x04; // INT_CTRL_LOW
	regAdd[1]=0x01; // Active Low, Falling Edge, Level interrupt, Global interrupt set 
	stmpe1801_write_reg(stmpe1801_gpio, &regAdd[0],2);
	
	regAdd[0]=0x19; // GPIO_SET_DIR_LOW
	regAdd[1]=0xF0; //IO 0-3 are input, IO 4-7 are output
	stmpe1801_write_reg(stmpe1801_gpio, &regAdd[0],2);
	
	regAdd[0]=0x1A; // GPIO_SET_DIR_MID
	regAdd[1]=0xFF; //IO 8-15 are output
	stmpe1801_write_reg(stmpe1801_gpio, &regAdd[0],2);
	
	regAdd[0]=0x1B; // GPIO_SET_DIR_HIGH
	regAdd[1]=0x03; //IO 16-17 are output
	stmpe1801_write_reg(stmpe1801_gpio, &regAdd[0],2);
	
#if !defined CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	regAdd[0]=0x1C; // GPIO_RE_LOW
	regAdd[1]=0x02; //IO 1 set to Active High 
	stmpe1801_write_reg(stmpe1801_gpio, &regAdd[0],2);
	
	regAdd[0]=0x1F; // GPIO_FE_LOW
	regAdd[1]=0x0D; //IO 0,2,3 set to Active Low
	stmpe1801_write_reg(stmpe1801_gpio, &regAdd[0],2);
#endif
	
	return 0;
}

static int stm_gpio_probe(struct i2c_client *client, const struct i2c_device_id *idp)
{
	struct generic_gpio_platform_data *pdata = client->dev.platform_data;
	struct stmpe1801_gpio *dev;
	struct gpio_chip *gc;
	int ret;

	printk( "++stmpe1801_gpio_probe\n");

	if (pdata == NULL) {
		dev_err(&client->dev, "missing platform data\n");
		return -ENODEV;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "SMBUS Byte Data not Supported\n");
		return -EIO;
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		dev_err(&client->dev, "failed to alloc memory\n");
		return -ENOMEM;
	}

	dev->client = client;

	gc = &(dev->gpio_chip);
	gc->direction_input = stmpe1801_gpio_direction_input; 
	gc->direction_output = stmpe1801_gpio_direction_output; 
	gc->get = stmpe1801_gpio_get;
	gc->set = stmpe1801_gpio_set;
	gc->can_sleep = 1;

	gc->base = pdata->gpio_start;
	gc->ngpio = MAXGPIO;
	gc->label = client->name;
	gc->owner = THIS_MODULE;

	mutex_init(&dev->lock);
	mutex_init(&dev->irq_lock);

	//GPIO Interrupt using irq_chip
	if (client->irq) {
		gc->to_irq = stmpe1801_gpio_to_irq;
		
		dev->irq = client->irq;
		dev->irq_base = pdata->irq_base + STMPE_INT_GPIO(0);
		
		ret = stmpe1801_gpio_irq_init(dev);
		if (ret) {
			dev_err(&client->dev, "failed to int irqs for gpio: %d\n", ret);
			goto err;
		}
		
		ret = request_threaded_irq(dev->irq, NULL, gpio_interrupt, IRQF_TRIGGER_LOW|IRQF_ONESHOT,
                                   "stmpe1801_gpio", dev);
		if (ret) {
			dev_err(&client->dev, "unable to get irq: %d\n", ret);
			goto out_removeirq;
		}
	}

	ret = gpiochip_add(&dev->gpio_chip);
	if (ret)
		goto out_freeirq;

	dev_info(&client->dev, "gpios %d..%d on a %s Rev. %d\n",
		gc->base, gc->base + gc->ngpio - 1,
		client->name, 0);

	init_stmpe1801_gpio(dev);
		
	if (pdata->setup) {
		ret = pdata->setup(client, gc->base, gc->ngpio, pdata->context);
		if (ret < 0)
			dev_warn(&client->dev, "setup failed, %d\n", ret);
	}

	i2c_set_clientdata(client, dev);

	return 0;
out_freeirq:
	if (client->irq)
		free_irq(client->irq, dev);
out_removeirq:
	if (client->irq)
		stmpe1801_gpio_irq_remove(dev);
err:
	kfree(dev);
	return ret;
}

static int stm_gpio_remove(struct i2c_client *client)
{
	struct generic_gpio_platform_data *pdata = client->dev.platform_data;
	struct stmpe1801_gpio *dev = i2c_get_clientdata(client);
	int ret;

	if (pdata->teardown) {
		ret = pdata->teardown(client,
				dev->gpio_chip.base, dev->gpio_chip.ngpio,
				pdata->context);
		if (ret < 0) {
			dev_err(&client->dev, "teardown failed %d\n", ret);
			return ret;
		}
	}

	ret = gpiochip_remove(&dev->gpio_chip);

	if (ret) {
		dev_err(&client->dev, "gpiochip_remove failed %d\n", ret);
		return ret;
	}

	free_irq(dev->irq, dev);

	kfree(dev);
	return 0;
}

static const struct i2c_device_id stm_gpio_id[] = {
	{ DRV_NAME, 0 },
	{ }
};

static struct i2c_driver stm_gpio_driver = {
	.driver = {
		.name = DRV_NAME,
	},
	.probe = stm_gpio_probe,
	.remove = stm_gpio_remove,
	.id_table = stm_gpio_id,
};

static int __init stm_gpio_init(void)
{
	return i2c_add_driver(&stm_gpio_driver);
}

static void __exit stm_gpio_exit(void)
{
	i2c_del_driver(&stm_gpio_driver);
}

MODULE_DESCRIPTION("STM GPIO IC Driver");
MODULE_AUTHOR("Bela Somaiah <bela.somaiah@st.com>");
MODULE_LICENSE("GPL");

subsys_initcall(stm_gpio_init);
module_exit(stm_gpio_exit);
