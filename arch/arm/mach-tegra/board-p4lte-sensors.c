/*
 * arch/arm/mach-tegra/board-p4lte-sensors.c
 *
 * Copyright (c) 2011, NVIDIA CORPORATION, All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * Neither the name of NVIDIA CORPORATION nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <linux/delay.h>
#include <linux/i2c.h>
#include <mach/gpio.h>
#include <mach/gpio-sec.h>
#include <linux/bh1721fvc.h>
#include "gpio-names.h"
#include <linux/mpu.h>

static void p3_ak8975_init(void)
{
	tegra_gpio_enable(GPIO_AK8975_INT);
	gpio_request(GPIO_AK8975_INT, "ak8975c_int");
	gpio_direction_input(GPIO_AK8975_INT);
}

static void p3_mpu3050_init(void)
{
	tegra_gpio_enable(GPIO_MPU_INT);
	gpio_request(GPIO_MPU_INT, "mpu3050_int");
	gpio_direction_input(GPIO_MPU_INT);
}

/* we use a skeleton to provide some information needed by MPL
 * but we don't use the suspend/resume/read functions so we
 * don't initialize them so that mldl_cfg.c doesn't try to
 * control it directly.  we have a separate mag driver instead.
 */
static struct ext_slave_descr simple_ak8975_descr = {
	/*.init             = */ NULL,
	/*.exit             = */ NULL,
	/*.suspend          = */ NULL,
	/*.resume           = */ NULL,
	/*.read             = */ NULL,
	/*.config           = */ NULL,
	/*.name             = */ "ak8975",
	/*.type             = */ EXT_SLAVE_TYPE_COMPASS,
	/*.id               = */ COMPASS_ID_AKM,
	/*.reg              = */ 0x03,
	/*.len              = */ 6,
	/*.endian           = */ EXT_SLAVE_LITTLE_ENDIAN,
	/*.range            = */ {9830, 4000}
};

static struct ext_slave_descr *ak8975_get_slave_descr(void)
{
	return &simple_ak8975_descr;
}

static struct mpu3050_platform_data p3_mpu3050_pdata_rev05 = {
	.int_config  = 0x10,
	/* Orientation for MPU.  Part is mounted rotated
	 * 90 degrees counter-clockwise from natural orientation.
	 * So X & Y are swapped and Y is negated.
	 */
	.orientation = {  0, -1,  0,
			  1,  0,  0,
			  0,  0,  1 },
	.level_shifter = 0,
	.accel = {
		.get_slave_descr = kxtf9_get_slave_descr,
		.irq         = 0,
		.adapt_num   = 0,
		.bus         = EXT_SLAVE_BUS_SECONDARY,
		.address     = 0x0F,
		/* Orientation for the Acc.  Part is mounted rotated
		 * 180 degrees from natural orientation.
		 * So X & Y are both negated.
		 */
		.orientation = { -1,  0,  0,
				  0, -1,  0,
				  0,  0,  1 },
	},
	.compass = {
		.get_slave_descr = ak8975_get_slave_descr,
		.irq	     = 0,
		.adapt_num   = 3,            /*bus number 3*/
		.bus         = EXT_SLAVE_BUS_PRIMARY,
		.address     = 0x0C,
		/* Orientation for the Mag.  Part is mounted rotated
		 * 90 degrees counter-clockwise from natural orientation.
		 * So X & Y are swapped and Y is negated.
		 */
		.orientation = {  0, -1,  0,
				  1,  0,  0,
				  0,  0,  1 },

	},
	.pressure = {
		.get_slave_descr = NULL,
		.irq	     = 0,
		.bus	     = EXT_SLAVE_BUS_INVALID,
	},
};


static struct mpu3050_platform_data p3_mpu3050_pdata = {
	.int_config  = 0x10,
	/* Orientation for MPU.  Part is mounted rotated
	 * 90 degrees counter-clockwise from natural orientation.
	 * So X & Y are swapped and Y is negated.
	 */
	.orientation = {  0, -1,  0,
			  -1,  0,  0,
			  0,  0,  -1 },
	.level_shifter = 0,
	.accel = {
		.get_slave_descr = kxtf9_get_slave_descr,
		.irq         = 0,
		.adapt_num   = 0,
		.bus         = EXT_SLAVE_BUS_SECONDARY,
		.address     = 0x0F,
		/* Orientation for the Acc.  Part is mounted rotated
		 * 180 degrees from natural orientation.
		 * So X & Y are both negated.
		 */
		.orientation = { -1,  0,  0,
				  0, 1,  0,
				  0,  0,  -1 },
	},
	.compass = {
		.get_slave_descr = ak8975_get_slave_descr,
		.irq	     = 0,
		.adapt_num   = 12,            /*bus number 3*/
		.bus         = EXT_SLAVE_BUS_PRIMARY,
		.address     = 0x0C,
		/* Orientation for the Mag.  Part is mounted rotated
		 * 90 degrees counter-clockwise from natural orientation.
		 * So X & Y are swapped and Y is negated.
		 */
		.orientation = {  0, 1,  0,
				 1,  0,  0,
				  0,  0,  -1 },
	},
	.pressure = {
		.get_slave_descr = NULL,
		.irq	     = 0,
		.bus	     = EXT_SLAVE_BUS_INVALID,
	},
};

static const struct i2c_board_info p3_i2c_mpu_sensor_board_info_rev05[] = {
	{
		I2C_BOARD_INFO("mpu3050", 0x68),
		.irq = TEGRA_GPIO_TO_IRQ(GPIO_MPU_INT),
		.platform_data = &p3_mpu3050_pdata_rev05,
	},
	{
		I2C_BOARD_INFO("kxtf9", 0x0F),
	},
};


static const struct i2c_board_info p3_i2c_mpu_sensor_board_info[] = {
	{
		I2C_BOARD_INFO("mpu3050", 0x68),
		.irq = TEGRA_GPIO_TO_IRQ(GPIO_MPU_INT),
		.platform_data = &p3_mpu3050_pdata,
	},
	{
		I2C_BOARD_INFO("kxtf9", 0x0F),
	},
};

static const struct i2c_board_info p3_i2c2_board_info[] = {
	{
		I2C_BOARD_INFO("bq20z75-battery", 0x0B),
	},
};

static int  bh1721fvc_light_sensor_reset(void)
{
	int err;

	err = gpio_direction_output(GPIO_LIGHT_SENSOR_DVI, 0);
	if (err) {
		printk(KERN_ERR "Failed to make the light sensor gpio(dvi)"
			" low (%d)\n", err);
		return err;
	}
	udelay(2);
	err = gpio_direction_output(GPIO_LIGHT_SENSOR_DVI, 1);
	if (err) {
		printk(KERN_ERR "Failed to make the light sensor gpio(dvi)"
			" high (%d)\n", err);
		return err;
	}
	return 0;
}

static struct bh1721fvc_platform_data bh1721fvc_pdata = {
	.reset = bh1721fvc_light_sensor_reset,
};

static struct i2c_board_info p3_i2c_light_sensor_board_info[] = {
	{
		I2C_BOARD_INFO("bh1721fvc", 0x23),
		.platform_data = &bh1721fvc_pdata,
	},
};

static struct i2c_board_info p3_i2c_compass_sensor_board_info[] = {
	{
		I2C_BOARD_INFO("ak8975c", 0x0C),
		.irq = TEGRA_GPIO_TO_IRQ(GPIO_AK8975_INT),
	},
};

static int p3_light_sensor_init(void)
{
	int err;

	tegra_gpio_enable(GPIO_LIGHT_SENSOR_DVI);
	err = gpio_request(GPIO_LIGHT_SENSOR_DVI, "LIGHT_SENSOR_DVI");
	if (err) {
		printk(KERN_ERR "Failed to request the light "
			" sensor gpio (%d)\n", err);
		return err;
	}
	err = gpio_direction_output(GPIO_LIGHT_SENSOR_DVI, 1);
	if (err) {
		printk(KERN_ERR "Failed to make the light sensor gpio(dvi)"
			" high (%d)\n", err);
		return err;
	}

	return 0;
}

int __init p3_sensors_init(void)
{
	p3_light_sensor_init();
	p3_ak8975_init();
	p3_mpu3050_init();

        if(system_rev > 0x0A){
        	i2c_register_board_info(0, p3_i2c_mpu_sensor_board_info,
        		ARRAY_SIZE(p3_i2c_mpu_sensor_board_info));
        }
        else{
            	i2c_register_board_info(0, p3_i2c_mpu_sensor_board_info_rev05,
        		ARRAY_SIZE(p3_i2c_mpu_sensor_board_info_rev05));

        }
	i2c_register_board_info(12, p3_i2c_compass_sensor_board_info,
		ARRAY_SIZE(p3_i2c_compass_sensor_board_info));
	i2c_register_board_info(5, p3_i2c_light_sensor_board_info,
		ARRAY_SIZE(p3_i2c_light_sensor_board_info));
#if 0

	if (system_rev >= 0x2) { /* rev0.2 */
		i2c_register_board_info(0, p3_i2c_mpu_sensor_board_info,
			ARRAY_SIZE(p3_i2c_mpu_sensor_board_info));
		i2c_register_board_info(12, p3_i2c_compass_sensor_board_info,
			ARRAY_SIZE(p3_i2c_compass_sensor_board_info));
		i2c_register_board_info(5, p3_i2c_light_sensor_board_info,
			ARRAY_SIZE(p3_i2c_light_sensor_board_info));
	} else if (system_rev >= 0x1) { /* rev0.1 */
		/* AK8975 and MPU3050 are on GEN1_I2C (bus 0) */
		/* BH1721FVC is on SW I2c (bus 5) */
		i2c_register_board_info(0, p3_i2c_mpu_sensor_board_info,
			ARRAY_SIZE(p3_i2c_mpu_sensor_board_info));
		i2c_register_board_info(0, p3_i2c_compass_sensor_board_info,
			ARRAY_SIZE(p3_i2c_compass_sensor_board_info));
		i2c_register_board_info(5, p3_i2c_light_sensor_board_info,
			ARRAY_SIZE(p3_i2c_light_sensor_board_info));
	} else if (system_rev >= 0x0) { /* rev0.0 */
		i2c_register_board_info(3, p3_i2c_compass_sensor_board_info,
			ARRAY_SIZE(p3_i2c_compass_sensor_board_info));
		i2c_register_board_info(5, p3_i2c_light_sensor_board_info,
			ARRAY_SIZE(p3_i2c_light_sensor_board_info));
	} else {
		/* on older lunchbox, AK8975 and BH1721FVC are both
		 * on CAM_I2C (name in schematic), which is i2c bus 3.
		 * MPU is on GEN1_I2C
		 */
		i2c_register_board_info(3, p3_i2c_light_sensor_board_info,
			ARRAY_SIZE(p3_i2c_light_sensor_board_info));
		i2c_register_board_info(3, p3_i2c_compass_sensor_board_info,
			ARRAY_SIZE(p3_i2c_compass_sensor_board_info));
	}
#endif

	return 0;
}
