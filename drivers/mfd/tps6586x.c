/*
 * Core driver for TI TPS6586x PMIC family
 *
 * Copyright (c) 2010 CompuLab Ltd.
 * Mike Rapoport <mike@compulab.co.il>
 *
 * Based on da903x.c.
 * Copyright (C) 2008 Compulab, Ltd.
 * Mike Rapoport <mike@compulab.co.il>
 * Copyright (C) 2006-2008 Marvell International Ltd.
 * Eric Miao <eric.miao@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/delay.h>

#include <linux/mfd/core.h>
#include <linux/mfd/tps6586x.h>

#define TPS6586X_SUPPLYENE  0x14
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
#define SOFT_RST_BIT        BIT(0) /* Soft reset control */

/* Control Registers */
#define TPS6586x_CHG1		0x49
#define TPS6586X_CHG2		0x4A

#define RETRY_CNT			5
#endif

#define EXITSLREQ_BIT       BIT(1) /* Exit sleep mode request */
#define SLEEP_MODE_BIT      BIT(3) /* Sleep mode */

/* GPIO control registers */
#define TPS6586X_GPIOSET1	0x5d
#define TPS6586X_GPIOSET2	0x5e


/* interrupt control registers */
#define TPS6586X_INT_ACK1	0xb5
#define TPS6586X_INT_ACK2	0xb6
#define TPS6586X_INT_ACK3	0xb7
#define TPS6586X_INT_ACK4	0xb8

/* interrupt mask registers */
#define TPS6586X_INT_MASK1	0xb0
#define TPS6586X_INT_MASK2	0xb1
#define TPS6586X_INT_MASK3	0xb2
#define TPS6586X_INT_MASK4	0xb3
#define TPS6586X_INT_MASK5	0xb4

/* device id */
#define TPS6586X_VERSIONCRC	0xcd

#ifdef CONFIG_TPS6586X_ADC
/* ADC0 Engine Data */
#define TPS6586x_ADC0_SET	0x61
#define TPS6586x_ADC0_WAIT	0x62
#define TPS6586x_ADC0_SUM2	0x94
#define TPS6586x_ADC0_SUM1	0x95
#define TPS6586x_ADC0_INT	0x9A

/* ADC0 Constant */
#define ADC_CONVERSION_DELAY_USEC			70
#define ADC_CONVERSION_TIMEOUT_USEC			500
#define ADC_CONVERSION_VOLTAGE_RANGE			2000
#define ADC_CONVERSION_DIVIDOR				3
#define ADC_CONVERSION_PRECISION			10
#define ADC_CONVERSION_SUB_OFFSET			2250
#define ADC_FULL_SCALE_READING_MV_BAT			4622
#define ADC_FULL_SCALE_READING_MV_TS			2600
#define ADC_FULL_SCALE_READING_MV_ANALOG_PIN1		2600
#define ADC_FULL_SCALE_READING_MV_ANALOG_PIN2		2600
#define ADC_CONVERSION_PREWAIT_MS			26
#endif /* CONFIG_TPS6586X_ADC */

struct tps6586x_irq_data {
	u8	mask_reg;
	u8	mask_mask;
};

#define TPS6586X_IRQ(_reg, _mask)				\
	{							\
		.mask_reg = (_reg) - TPS6586X_INT_MASK1,	\
		.mask_mask = (_mask),				\
	}

static const struct tps6586x_irq_data tps6586x_irqs[] = {
	[TPS6586X_INT_PLDO_0]	= TPS6586X_IRQ(TPS6586X_INT_MASK1, 1 << 0),
	[TPS6586X_INT_PLDO_1]	= TPS6586X_IRQ(TPS6586X_INT_MASK1, 1 << 1),
	[TPS6586X_INT_PLDO_2]	= TPS6586X_IRQ(TPS6586X_INT_MASK1, 1 << 2),
	[TPS6586X_INT_PLDO_3]	= TPS6586X_IRQ(TPS6586X_INT_MASK1, 1 << 3),
	[TPS6586X_INT_PLDO_4]	= TPS6586X_IRQ(TPS6586X_INT_MASK1, 1 << 4),
	[TPS6586X_INT_PLDO_5]	= TPS6586X_IRQ(TPS6586X_INT_MASK1, 1 << 5),
	[TPS6586X_INT_PLDO_6]	= TPS6586X_IRQ(TPS6586X_INT_MASK1, 1 << 6),
	[TPS6586X_INT_PLDO_7]	= TPS6586X_IRQ(TPS6586X_INT_MASK1, 1 << 7),
	[TPS6586X_INT_COMP_DET]	= TPS6586X_IRQ(TPS6586X_INT_MASK4, 1 << 0),
	[TPS6586X_INT_ADC]	= TPS6586X_IRQ(TPS6586X_INT_MASK2, 1 << 1),
	[TPS6586X_INT_PLDO_8]	= TPS6586X_IRQ(TPS6586X_INT_MASK2, 1 << 2),
	[TPS6586X_INT_PLDO_9]	= TPS6586X_IRQ(TPS6586X_INT_MASK2, 1 << 3),
	[TPS6586X_INT_PSM_0]	= TPS6586X_IRQ(TPS6586X_INT_MASK2, 1 << 4),
	[TPS6586X_INT_PSM_1]	= TPS6586X_IRQ(TPS6586X_INT_MASK2, 1 << 5),
	[TPS6586X_INT_PSM_2]	= TPS6586X_IRQ(TPS6586X_INT_MASK2, 1 << 6),
	[TPS6586X_INT_PSM_3]	= TPS6586X_IRQ(TPS6586X_INT_MASK2, 1 << 7),
	[TPS6586X_INT_RTC_ALM1]	= TPS6586X_IRQ(TPS6586X_INT_MASK5, 1 << 4),
	[TPS6586X_INT_ACUSB_OVP] = TPS6586X_IRQ(TPS6586X_INT_MASK5, 0x03),
	[TPS6586X_INT_USB_DET]	= TPS6586X_IRQ(TPS6586X_INT_MASK5, 1 << 2),
	[TPS6586X_INT_AC_DET]	= TPS6586X_IRQ(TPS6586X_INT_MASK5, 1 << 3),
	[TPS6586X_INT_BAT_DET]	= TPS6586X_IRQ(TPS6586X_INT_MASK3, 1 << 0),
	[TPS6586X_INT_CHG_STAT]	= TPS6586X_IRQ(TPS6586X_INT_MASK4, 0xfc),
	[TPS6586X_INT_CHG_TEMP]	= TPS6586X_IRQ(TPS6586X_INT_MASK3, 0x06),
	[TPS6586X_INT_PP]	= TPS6586X_IRQ(TPS6586X_INT_MASK3, 0xf0),
	[TPS6586X_INT_RESUME]	= TPS6586X_IRQ(TPS6586X_INT_MASK5, 1 << 5),
	[TPS6586X_INT_LOW_SYS]	= TPS6586X_IRQ(TPS6586X_INT_MASK5, 1 << 6),
	[TPS6586X_INT_RTC_ALM2] = TPS6586X_IRQ(TPS6586X_INT_MASK4, 1 << 1),
};

struct tps6586x {
	struct mutex		lock;
	struct device		*dev;
	struct i2c_client	*client;

	struct gpio_chip	gpio;
	struct irq_chip		irq_chip;
	struct mutex		irq_lock;
	int			irq_base;
	u32			irq_en;
	u8			mask_cache[5];
	u8			mask_reg[5];
};

static inline int __tps6586x_read(struct i2c_client *client,
				  int reg, uint8_t *val)
{
	int ret;
	int ret_cnt = 0;

	while(ret_cnt++ < RETRY_CNT) {
		ret = i2c_smbus_read_byte_data(client, reg);
		if (ret >= 0) 
			break;

		dev_err(&client->dev, "failed reading at 0x%02x(ret=%d)\n", 
			reg, ret_cnt);
	}

	if(ret < 0)
		return ret;

	*val = (uint8_t)ret;

	return 0;
}

static inline int __tps6586x_reads(struct i2c_client *client, int reg,
				   int len, uint8_t *val)
{
	int ret;

	ret = i2c_smbus_read_i2c_block_data(client, reg, len, val);
	if (ret < 0) {
		dev_err(&client->dev, "failed reading from 0x%02x\n", reg);
		return ret;
	}

	return 0;
}

static inline int __tps6586x_write(struct i2c_client *client,
				 int reg, uint8_t val)
{
	int ret;
	int ret_cnt = 0;

	while(ret_cnt++ < RETRY_CNT) {
		ret = i2c_smbus_write_byte_data(client, reg, val);
		if (ret >= 0) 
			break;

		dev_err(&client->dev, "failed writing 0x%02x to 0x%02x(ret=%d)\n",
				val, reg, ret_cnt);
	}

	if(ret < 0)
		return ret;

	return 0;
}

static inline int __tps6586x_writes(struct i2c_client *client, int reg,
				  int len, uint8_t *val)
{
	int ret, i;
	/*
	 * tps6586 does not support burst writes.
	 * i2c writes have to be    1 byte at a time.
	 */
	for (i = 0; i < len; i++) {
		ret = __tps6586x_write(client, reg + i, *(val + i));
		if (ret < 0)
			return ret;
	}

	return 0;
}

int tps6586x_write(struct device *dev, int reg, uint8_t val)
{
	return __tps6586x_write(to_i2c_client(dev), reg, val);
}
EXPORT_SYMBOL_GPL(tps6586x_write);

int tps6586x_writes(struct device *dev, int reg, int len, uint8_t *val)
{
	return __tps6586x_writes(to_i2c_client(dev), reg, len, val);
}
EXPORT_SYMBOL_GPL(tps6586x_writes);

int tps6586x_read(struct device *dev, int reg, uint8_t *val)
{
	return __tps6586x_read(to_i2c_client(dev), reg, val);
}
EXPORT_SYMBOL_GPL(tps6586x_read);

int tps6586x_reads(struct device *dev, int reg, int len, uint8_t *val)
{
	return __tps6586x_reads(to_i2c_client(dev), reg, len, val);
}
EXPORT_SYMBOL_GPL(tps6586x_reads);

int tps6586x_set_bits(struct device *dev, int reg, uint8_t bit_mask)
{
	struct tps6586x *tps6586x = dev_get_drvdata(dev);
	uint8_t reg_val;
	int ret = 0;

	mutex_lock(&tps6586x->lock);

	ret = __tps6586x_read(to_i2c_client(dev), reg, &reg_val);
	if (ret)
		goto out;

	if ((reg_val & bit_mask) == 0) {
		reg_val |= bit_mask;
		ret = __tps6586x_write(to_i2c_client(dev), reg, reg_val);
	}
out:
	mutex_unlock(&tps6586x->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(tps6586x_set_bits);

int tps6586x_clr_bits(struct device *dev, int reg, uint8_t bit_mask)
{
	struct tps6586x *tps6586x = dev_get_drvdata(dev);
	uint8_t reg_val;
	int ret = 0;

	mutex_lock(&tps6586x->lock);

	ret = __tps6586x_read(to_i2c_client(dev), reg, &reg_val);
	if (ret)
		goto out;

	if (reg_val & bit_mask) {
		reg_val &= ~bit_mask;
		ret = __tps6586x_write(to_i2c_client(dev), reg, reg_val);
	}
out:
	mutex_unlock(&tps6586x->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(tps6586x_clr_bits);

int tps6586x_update(struct device *dev, int reg, uint8_t val, uint8_t mask)
{
	struct tps6586x *tps6586x = dev_get_drvdata(dev);
	uint8_t reg_val;
	int ret = 0;

	mutex_lock(&tps6586x->lock);

	ret = __tps6586x_read(tps6586x->client, reg, &reg_val);
	if (ret)
		goto out;

	if ((reg_val & mask) != val) {
		reg_val = (reg_val & ~mask) | val;
		ret = __tps6586x_write(tps6586x->client, reg, reg_val);
	}
out:
	mutex_unlock(&tps6586x->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(tps6586x_update);

static struct i2c_client *tps6586x_i2c_client;
int tps6586x_power_off(void)
{
	struct device *dev = NULL;
	uint8_t data = 0;
	uint32_t count = 0;
	int ret;

	if (!tps6586x_i2c_client)
		return -EINVAL;

	dev = &tps6586x_i2c_client->dev;

	while (1) {
		/* SLEEP REQUEST EXIT CONTROL */
		tps6586x_clr_bits(dev, TPS6586X_SUPPLYENE, EXITSLREQ_BIT);
		ret = tps6586x_read(dev, TPS6586X_SUPPLYENE, &data);
		if (ret < 0) {
			pr_err("%s() failed to read with return : %d\n",
				__func__, ret);
			continue;
		}
		if (data & EXITSLREQ_BIT) {
			pr_warn("%s: EXITSLREQ_BIT is not set(0x%x)\n", __func__, data);
			continue;
		}

		/* Set TPS6586X in SLEEP MODE. The device will be powered off */
		tps6586x_set_bits(dev, TPS6586X_SUPPLYENE, SLEEP_MODE_BIT);
		mdelay(100);
		/* The below code should not excute in normal case */
		ret = tps6586x_read(dev, TPS6586X_SUPPLYENE, &data);
		if (ret < 0) {
			pr_err("%s() failed to read with return : %d\n",
				__func__, ret);
		} else if (data & SLEEP_MODE_BIT) {
			pr_info("%s: SLEEP_MODE_BIT is set\n", __func__);
			break;
		}

		mdelay(1000);
	}

	return 0;
}

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
int tps6586x_soft_rst(void)
{
	if (!tps6586x_i2c_client)
		return -EINVAL;

	return tps6586x_set_bits(&tps6586x_i2c_client->dev,
				TPS6586X_SUPPLYENE, SOFT_RST_BIT);
}
#endif

static int tps6586x_gpio_get(struct gpio_chip *gc, unsigned offset)
{
	struct tps6586x *tps6586x = container_of(gc, struct tps6586x, gpio);
	uint8_t val;
	int ret;

	ret = __tps6586x_read(tps6586x->client, TPS6586X_GPIOSET2, &val);
	if (ret)
		return ret;

	return !!(val & (1 << offset));
}


static void tps6586x_gpio_set(struct gpio_chip *chip, unsigned offset,
			      int value)
{
	struct tps6586x *tps6586x = container_of(chip, struct tps6586x, gpio);

	__tps6586x_write(tps6586x->client, TPS6586X_GPIOSET2,
			 value << offset);
}

static int tps6586x_gpio_input(struct gpio_chip *gc, unsigned offset)
{
	/* FIXME: add handling of GPIOs as dedicated inputs */
	return -ENOSYS;
}

static int tps6586x_gpio_output(struct gpio_chip *gc, unsigned offset,
				int value)
{
	struct tps6586x *tps6586x = container_of(gc, struct tps6586x, gpio);
	uint8_t val, mask;
	int ret;

	val = value << offset;
	mask = 0x1 << offset;
	ret = tps6586x_update(tps6586x->dev, TPS6586X_GPIOSET2, val, mask);
	if (ret)
		return ret;

	val = 0x1 << (offset * 2);
	mask = 0x3 << (offset * 2);

	return tps6586x_update(tps6586x->dev, TPS6586X_GPIOSET1, val, mask);
}

static void tps6586x_gpio_init(struct tps6586x *tps6586x, int gpio_base)
{
	int ret;

	if (!gpio_base)
		return;

	tps6586x->gpio.owner		= THIS_MODULE;
	tps6586x->gpio.label		= tps6586x->client->name;
	tps6586x->gpio.dev		= tps6586x->dev;
	tps6586x->gpio.base		= gpio_base;
	tps6586x->gpio.ngpio		= 4;
	tps6586x->gpio.can_sleep	= 1;

	tps6586x->gpio.direction_input	= tps6586x_gpio_input;
	tps6586x->gpio.direction_output	= tps6586x_gpio_output;
	tps6586x->gpio.set		= tps6586x_gpio_set;
	tps6586x->gpio.get		= tps6586x_gpio_get;

	ret = gpiochip_add(&tps6586x->gpio);
	if (ret)
		dev_warn(tps6586x->dev, "GPIO registration failed: %d\n", ret);
}

static int __remove_subdev(struct device *dev, void *unused)
{
	platform_device_unregister(to_platform_device(dev));
	return 0;
}

static int tps6586x_remove_subdevs(struct tps6586x *tps6586x)
{
	return device_for_each_child(tps6586x->dev, NULL, __remove_subdev);
}

static void tps6586x_irq_lock(unsigned int irq)
{
	struct tps6586x *tps6586x = get_irq_chip_data(irq);

	mutex_lock(&tps6586x->irq_lock);
}

static void tps6586x_irq_enable(unsigned int irq)
{
	struct tps6586x *tps6586x = get_irq_chip_data(irq);
	unsigned int __irq = irq - tps6586x->irq_base;
	const struct tps6586x_irq_data *data = &tps6586x_irqs[__irq];

	tps6586x->mask_reg[data->mask_reg] &= ~data->mask_mask;
	tps6586x->irq_en |= (1 << __irq);
}

static void tps6586x_irq_disable(unsigned int irq)
{
	struct tps6586x *tps6586x = get_irq_chip_data(irq);

	unsigned int __irq = irq - tps6586x->irq_base;
	const struct tps6586x_irq_data *data = &tps6586x_irqs[__irq];

	tps6586x->mask_reg[data->mask_reg] |= data->mask_mask;
	tps6586x->irq_en &= ~(1 << __irq);
}

static void tps6586x_irq_sync_unlock(unsigned int irq)
{
	struct tps6586x *tps6586x = get_irq_chip_data(irq);
	int i;

	for (i = 0; i < ARRAY_SIZE(tps6586x->mask_reg); i++) {
		if (tps6586x->mask_reg[i] != tps6586x->mask_cache[i]) {
			if (!WARN_ON(tps6586x_write(tps6586x->dev,
						    TPS6586X_INT_MASK1 + i,
						    tps6586x->mask_reg[i])))
				tps6586x->mask_cache[i] = tps6586x->mask_reg[i];
		}
	}

	mutex_unlock(&tps6586x->irq_lock);
}

static irqreturn_t tps6586x_irq(int irq, void *data)
{
	struct tps6586x *tps6586x = data;
	u32 acks;
	int ret = 0;

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	/* there's a race between the running of this threaded
	 * irq handler and the resume of the tegra i2c controller
	 * so sleep briefly to make sure the i2c controller has
	 * been resumed first.
	 */
	ret = tps6586x_reads(tps6586x->dev, TPS6586X_INT_ACK1,
			     sizeof(acks), (uint8_t *)&acks);
	if (ret < 0) {
		int i;
		for (i = 0; i < 5; i++) {
			pr_info("%s: failed reading INT_ACK1, sleep & retry\n",
				__func__);
			usleep_range(10000, 20000);
			ret = tps6586x_reads(tps6586x->dev, TPS6586X_INT_ACK1,
					sizeof(acks), (uint8_t *)&acks);
			if (!ret)
				break;
		}
	}
#else
	ret = tps6586x_reads(tps6586x->dev, TPS6586X_INT_ACK1,
			     sizeof(acks), (uint8_t *)&acks);
#endif

	if (ret < 0) {
		dev_err(tps6586x->dev, "failed to read interrupt status\n");
		return IRQ_NONE;
	}

	acks = le32_to_cpu(acks);

	pr_debug("%s: INT_ACK1 has value 0x%x\n", __func__, acks);

	while (acks) {
		int i = __ffs(acks);

		if (tps6586x->irq_en & (1 << i))
			handle_nested_irq(tps6586x->irq_base + i);

		acks &= ~(1 << i);
	}

	return IRQ_HANDLED;
}

static int __devinit tps6586x_irq_init(struct tps6586x *tps6586x, int irq,
				       int irq_base)
{
	int i, ret;
	u8 tmp[4];

	if (!irq_base) {
		dev_warn(tps6586x->dev, "No interrupt support on IRQ base\n");
		return -EINVAL;
	}

	mutex_init(&tps6586x->irq_lock);
	for (i = 0; i < 5; i++) {
		tps6586x->mask_cache[i] = 0xff;
		tps6586x->mask_reg[i] = 0xff;
		tps6586x_write(tps6586x->dev, TPS6586X_INT_MASK1 + i, 0xff);
	}

	tps6586x_reads(tps6586x->dev, TPS6586X_INT_ACK1, sizeof(tmp), tmp);

	tps6586x->irq_base = irq_base;

	tps6586x->irq_chip.name = "tps6586x";
	tps6586x->irq_chip.enable = tps6586x_irq_enable;
	tps6586x->irq_chip.disable = tps6586x_irq_disable;
	tps6586x->irq_chip.bus_lock = tps6586x_irq_lock;
	tps6586x->irq_chip.bus_sync_unlock = tps6586x_irq_sync_unlock;

	for (i = 0; i < ARRAY_SIZE(tps6586x_irqs); i++) {
		int __irq = i + tps6586x->irq_base;
		set_irq_chip_data(__irq, tps6586x);
		set_irq_chip_and_handler(__irq, &tps6586x->irq_chip,
					 handle_simple_irq);
		set_irq_nested_thread(__irq, 1);
#ifdef CONFIG_ARM
		set_irq_flags(__irq, IRQF_VALID);
#endif
	}

	ret = request_threaded_irq(irq, NULL, tps6586x_irq, IRQF_ONESHOT,
				   "tps6586x", tps6586x);

	if (!ret) {
		device_init_wakeup(tps6586x->dev, 1);
		enable_irq_wake(irq);
	}

	return ret;
}

#ifdef CONFIG_TPS6586X_ADC

static struct tps6586x *g_tps6586x;

/* read voltage from ADC of TPS6586X
 *	CH1(ACCESSORY_ID)
 *	CH2(REMOTE_SENSE)
 */
static int __tps6586x_adc_read(struct tps6586x *tps6586x, u32 *mili_volt,
			u8 channel)
{
	u32 timeout  = 0;
	uint8_t  dataS1  = 0;
	uint8_t  dataH   = 0;
	uint8_t  dataL   = 0;

	int ret;

	pr_info("%s(channel:%d)\n", __func__, channel);
	/* error check */
	if (WARN(tps6586x == NULL, "%s() tps6586x is null\n", __func__))
		return -1;
	if (WARN(mili_volt == NULL, "%s() mili_volt is null\n", __func__))
		return -2;
	if (WARN(channel > 2, "%s() channel error\n", __func__))
		return -3;

	*mili_volt = 0;    /* Default is 0V. */

	/* Configuring the adc conversion cycle
	 * ADC0_WAIT register(0x62)
	 * Reset all ADC engines and return them to the idle state;
	 * ADC0_RESET: 1
	*/
	ret = tps6586x_write(tps6586x->dev, TPS6586x_ADC0_WAIT, 0x80);
	if (ret < 0) {
		dev_err(tps6586x->dev, "%s() failed writing %d(%d)\n",
			__func__, TPS6586x_ADC0_WAIT, ret);
		return -4;
	}

	/* ADC0_SET register(0x61)
	 * ADC0_EN: 0(Don't start conversion);
	 * Number of Readings: 10; CHANNEL: CH1(Analog1)
	 */
	ret = tps6586x_write(tps6586x->dev, TPS6586x_ADC0_SET, 0x10 | channel);
	if (ret < 0) {
		dev_err(tps6586x->dev, "%s() failed writing %d(%d)\n",
			__func__, TPS6586x_ADC0_SET, ret);
		return -5;
	}

	/*  ADC0_WAIT register(0x62)
	 *  REF_EN: 0; AUTO_REF: 1; Wait time: 0.062ms
	*/
	ret = tps6586x_write(tps6586x->dev, TPS6586x_ADC0_WAIT, 0x21);
	if (ret < 0) {
		dev_err(tps6586x->dev, "%s() failed writing %d(%d)\n",
			__func__, TPS6586x_ADC0_WAIT, ret);
		return -6;
	}

	/* Start conversion!! */
	ret = tps6586x_write(tps6586x->dev, TPS6586x_ADC0_SET, 0x90 | channel);
	if (ret < 0) {
		dev_err(tps6586x->dev, "%s() failed writing %d(%d)\n",
			__func__, TPS6586x_ADC0_SET, ret);
		return -7;
	}

	/* Wait for conversion */
	msleep(ADC_CONVERSION_PREWAIT_MS);

	/* make sure the conversion is completed, or adc error. */
	while (1) {
		/* Read ADC status register */
		ret = tps6586x_read(tps6586x->dev, TPS6586x_ADC0_INT, &dataS1);
		if (ret < 0) {
			dev_err(tps6586x->dev,
				"%s() failed to read with return : %d\n",
				__func__, ret);
			return -8;
		}

		/* Conversion is done! */
		if (dataS1 & 0x80)
			break;

		/* ADC error! */
		if (dataS1 & 0x40) {
			dev_err(tps6586x->dev, "ADC conversion error.\n");
			return -9;
		}

		udelay(ADC_CONVERSION_DELAY_USEC);
		timeout += ADC_CONVERSION_DELAY_USEC;
		if (timeout >= ADC_CONVERSION_TIMEOUT_USEC)
			return -10;
	}

	/* Read the ADC conversion Average (SUM). */
	ret = tps6586x_read(tps6586x->dev, TPS6586x_ADC0_SUM2, &dataH);
	if (ret < 0) {
		dev_err(tps6586x->dev, "%s() failed reading %d(%d)\n",
			__func__, TPS6586x_ADC0_SUM2, ret);
		return -11;
	}

	ret = tps6586x_read(tps6586x->dev, TPS6586x_ADC0_SUM1, &dataL);
	if (ret < 0) {
		dev_err(tps6586x->dev, "%s() failed reading %d(%d)\n",
			__func__, TPS6586x_ADC0_SUM1, ret);
		return -12;
	}

	/* ADC0_WAIT register(0x62)
	 * REF_EN: 0; AUTO_REF: 0; Wait time: 0.062ms
	*/
	ret = tps6586x_write(tps6586x->dev, TPS6586x_ADC0_WAIT, 0x01);
	if (ret < 0) {
		dev_err(tps6586x->dev, "%s() failed writing %d(%d)\n",
			__func__, TPS6586x_ADC0_WAIT, ret);
		return -13;
	}

	/* Get a result value with mV. */
	*mili_volt = (((dataH << 8) | dataL) *
		ADC_FULL_SCALE_READING_MV_ANALOG_PIN1) / 1023 / 16;

	pr_info("[PM_ADC] ADC%d result : %dmV(0x%x)\n",
		channel, *mili_volt, ((dataH << 8) | dataL));

	return 0;
}

int tps6586x_adc_read(u32 *mili_volt, u8 channel)
{
	return __tps6586x_adc_read(g_tps6586x, mili_volt, channel);
}
EXPORT_SYMBOL_GPL(tps6586x_adc_read);

#endif /* CONFIG_TPS6586X_ADC */

static int __devinit tps6586x_add_subdevs(struct tps6586x *tps6586x,
					  struct tps6586x_platform_data *pdata)
{
	struct tps6586x_subdev_info *subdev;
	struct platform_device *pdev;
	int i, ret = 0;

	for (i = 0; i < pdata->num_subdevs; i++) {
		subdev = &pdata->subdevs[i];

		pdev = platform_device_alloc(subdev->name, subdev->id);

		pdev->dev.parent = tps6586x->dev;
		pdev->dev.platform_data = subdev->platform_data;

		ret = platform_device_add(pdev);
		if (ret)
			goto failed;
	}
	return 0;

failed:
	tps6586x_remove_subdevs(tps6586x);
	return ret;
}

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
static int tps6586x_print_reg(void)
{
	int i, ret = 0;
	uint8_t reg[256];

	memset(reg, 1, 256);
	for (i = 0; i < 255; i++) {
		mutex_lock(&g_tps6586x->lock);
		ret = __tps6586x_read(to_i2c_client(g_tps6586x->dev),
				i, &reg[i]);
		mutex_unlock(&g_tps6586x->lock);
	}
	pr_info("[PM] %s()-----------------\n", __func__);
	for (i = 0; i < 255; i += 8) {
		pr_info("0x%02x : 0x%02x 0x%02x 0x%02x"
			" 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
			i, reg[i], reg[i+1], reg[i+2], reg[i+3],
			reg[i+4], reg[i+5], reg[i+6], reg[i+7]);
	}
	pr_info("[PM]            -----------------\n");

	return ret;
}
#endif /* CONFIG_MACH_SAMSUNG_VARIATION_TEGRA */

static int __devinit tps6586x_i2c_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct tps6586x_platform_data *pdata = client->dev.platform_data;
	struct tps6586x *tps6586x;
	int ret;

	if (!pdata) {
		dev_err(&client->dev, "tps6586x requires platform data\n");
		return -ENOTSUPP;
	}

	ret = i2c_smbus_read_byte_data(client, TPS6586X_VERSIONCRC);
	if (ret < 0) {
		dev_err(&client->dev, "Chip ID read failed: %d\n", ret);
		return -EIO;
	}

	dev_info(&client->dev, "VERSIONCRC is %02x\n", ret);

	tps6586x = kzalloc(sizeof(struct tps6586x), GFP_KERNEL);
	if (tps6586x == NULL)
		return -ENOMEM;

	tps6586x->client = client;
	tps6586x->dev = &client->dev;
	i2c_set_clientdata(client, tps6586x);

	mutex_init(&tps6586x->lock);

	if (client->irq) {
		ret = tps6586x_irq_init(tps6586x, client->irq,
					pdata->irq_base);
		if (ret) {
			dev_err(&client->dev, "IRQ init failed: %d\n", ret);
			goto err_irq_init;
		}
	}

	ret = tps6586x_add_subdevs(tps6586x, pdata);
	if (ret) {
		dev_err(&client->dev, "add devices failed: %d\n", ret);
		goto err_add_devs;
	}

	tps6586x_gpio_init(tps6586x, pdata->gpio_base);

	tps6586x_i2c_client = client;

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	g_tps6586x = tps6586x;
	tps6586x_print_reg();

	/* Disable Charger LDO mode, Dynamic Timer Function */	
	tps6586x_write(tps6586x->dev, TPS6586X_CHG2, 0x00);
#endif

	return 0;

err_add_devs:
	if (client->irq)
		free_irq(client->irq, tps6586x);
err_irq_init:
	kfree(tps6586x);
	return ret;
}

static int __devexit tps6586x_i2c_remove(struct i2c_client *client)
{
	struct tps6586x *tps6586x = i2c_get_clientdata(client);

	if (client->irq)
		free_irq(client->irq, tps6586x);

	return 0;
}

static const struct i2c_device_id tps6586x_id_table[] = {
	{ "tps6586x", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, tps6586x_id_table);

static struct i2c_driver tps6586x_driver = {
	.driver	= {
		.name	= "tps6586x",
		.owner	= THIS_MODULE,
	},
	.probe		= tps6586x_i2c_probe,
	.remove		= __devexit_p(tps6586x_i2c_remove),
	.id_table	= tps6586x_id_table,
};

static int __init tps6586x_init(void)
{
	return i2c_add_driver(&tps6586x_driver);
}
subsys_initcall(tps6586x_init);

static void __exit tps6586x_exit(void)
{
	i2c_del_driver(&tps6586x_driver);
}
module_exit(tps6586x_exit);

MODULE_DESCRIPTION("TPS6586X core driver");
MODULE_AUTHOR("Mike Rapoport <mike@compulab.co.il>");
MODULE_LICENSE("GPL");
