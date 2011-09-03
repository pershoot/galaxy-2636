/*
 *  wacom_i2c.c - Wacom G5 Digitizer Controller (I2C bus)
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/wacom_i2c.h>

#include "wacom_i2c_func.h"
#include "wacom_i2c_flash.h"

struct i2c_client *g_client;

int wacom_i2c_frequency(char buf)
{
	int ret;

	if (g_client == NULL) {
		printk(KERN_DEBUG "[WACOM] failed to modify the frequency\n");
		return 0;
	}
	ret = i2c_master_send(g_client, &buf, sizeof(buf));
	if (ret > 0) {
		printk(KERN_INFO "buf:%d, sent:%d\n", buf, ret);
	} else {
		printk(KERN_ERR "Digitizer is not active\n");
		return -1;
	}
	return 0;
}
EXPORT_SYMBOL(wacom_i2c_frequency);

static int wacom_get_pdct_level(int irq)
{
	return __gpio_get_value((unsigned)irq_to_gpio(irq));
}

static irqreturn_t wacom_interrupt(int irq, void *dev_id)
{
	struct wacom_i2c *wac_i2c = dev_id;
	wacom_i2c_coord(wac_i2c);
	return IRQ_HANDLED;
}

static irqreturn_t wacom_pdct_interrupt(int irq, void *dev_id)
{
	int pdct;
	struct wacom_i2c *wac_i2c = dev_id;

	pdct = wacom_get_pdct_level(wac_i2c->pdct_irq);
	pr_info("pdct: %d\n", __func__, pdct);
	
	return IRQ_HANDLED;
}

static void wacom_i2c_set_input_values(struct i2c_client *client,
				struct wacom_i2c *wac_i2c,
				struct input_dev *input_dev)
{
	/*Set input values before registering input device*/
	input_dev->name = "WACOM_I2C_DIGITIZER";
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;
	input_dev->evbit[0] |= BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);

	__set_bit(ABS_PRESSURE, input_dev->absbit);
	/*__set_bit(ABS_MISC, input_dev->absbit);*/
	__set_bit(BTN_TOUCH, input_dev->keybit);
	__set_bit(DIGITIZER_TOOL_PEN, input_dev->keybit);
	__set_bit(DIGITIZER_TOOL_RUBBER, input_dev->keybit);
	__set_bit(DIGITIZER_STYLUS, input_dev->keybit);
	/*__set_bit(BTN_STYLUS2, input_dev->keybit);*/
	__set_bit(KEY_LEFTALT, input_dev->keybit);
}

static int wacom_check_emr_prox(struct wacom_g5_callbacks *cb)
{
	struct wacom_i2c *wac = container_of(cb, struct wacom_i2c, callbacks);

	pr_debug("[WACOM] %s\n", __func__);

	return wac->pen_prox;
}

static int wacom_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct wacom_i2c *wac_i2c;
	int i, ret;
	i = ret = 0;

	printk(KERN_INFO "[WACOM] %s\n", __func__);

	/*Check I2C functionality*/
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		goto err3;

	/*Obtain kernel memory space for wacom i2c*/
	wac_i2c = kzalloc(sizeof(struct wacom_i2c), GFP_KERNEL);
	wac_i2c->wac_feature = &wacom_feature_EMR;

	/*Initializing for semaphor*/
	mutex_init(&wac_i2c->lock);

	/*Register platform data*/
	wac_i2c->wac_pdata = client->dev.platform_data;

	/*Register callbacks*/
	wac_i2c->callbacks.check_prox = wacom_check_emr_prox;
	if(wac_i2c->wac_pdata->register_cb)
		wac_i2c->wac_pdata->register_cb(&wac_i2c->callbacks);

	/*Register wacom i2c to input device*/
	wac_i2c->input_dev = input_allocate_device();
	if (wac_i2c == NULL || wac_i2c->input_dev == NULL)
		goto fail;
	wacom_i2c_set_input_values(client, wac_i2c, wac_i2c->input_dev);

	wac_i2c->client = client;
	wac_i2c->irq = client->irq;
	/*  wac_i2c->gpio = S5PV210_GPD0(2); */
	/*  gpio_request(wac_i2c->gpio, "WACOM_INT"); */
	/*  s3c_gpio_setpull(wac_i2c->gpio, S3C_GPIO_PULL_NONE); */
	/*  s3c_gpio_cfgpin(wac_i2c->gpio,  S3C_GPIO_SFN(0xF)); */

	/*  wac_i2c->irq = EMR_INT; */

	/*Change below if irq is needed*/
	wac_i2c->irq_flag = 1;

	/*Request IRQ*/
	if (wac_i2c->irq_flag) {
		ret = request_threaded_irq(wac_i2c->irq, NULL, wacom_interrupt,
		IRQF_DISABLED|IRQF_TRIGGER_RISING , wac_i2c->name, wac_i2c);
		if (ret < 0)
			goto err1;
	}

	wacom_i2c_query(wac_i2c);

	if (wac_i2c->wac_pdata->xy_switch) {
		input_set_abs_params(wac_i2c->input_dev, ABS_X, 0,
			wac_i2c->wac_feature->y_max, 4, 0);
		input_set_abs_params(wac_i2c->input_dev, ABS_Y, 0,
			wac_i2c->wac_feature->x_max, 4, 0);
	} else {
		input_set_abs_params(wac_i2c->input_dev, ABS_X, 0,
			wac_i2c->wac_feature->x_max, 4, 0);
		input_set_abs_params(wac_i2c->input_dev, ABS_Y, 0,
			wac_i2c->wac_feature->y_max, 4, 0);
	}	
	input_set_abs_params(wac_i2c->input_dev, ABS_PRESSURE, 0,
			wac_i2c->wac_feature->pressure_max, 0, 0);
	input_set_drvdata(wac_i2c->input_dev, wac_i2c);

	/*Set client data*/
	i2c_set_clientdata(client, wac_i2c);

	/*Before registering input device, data in each input_dev must be set*/
	if (input_register_device(wac_i2c->input_dev))
		goto err2;

	g_client = client;

	if (wac_i2c->wac_pdata->pdct_irq) {
		wac_i2c->pdct_irq =
			wac_i2c->wac_pdata->pdct_irq;
		ret = request_threaded_irq(wac_i2c->pdct_irq,
					NULL,
					wacom_pdct_interrupt,
					(IRQF_TRIGGER_FALLING |
						IRQF_TRIGGER_RISING),
					wac_i2c->name,
					wac_i2c);
		if (ret<0) {
			pr_err("Failed to register IRQ\n");
			goto err2;
		}
	}


	/*  if(wac_i2c->irq_flag) */
	/*   disable_irq(wac_i2c->irq); */

	return 0;

err3:
	printk(KERN_ERR "wacom_i2c:No I2C functionality found\n");
	return -ENODEV;

err2:
	printk(KERN_ERR "wacom_i2c:err2 occured\n");
	input_free_device(wac_i2c->input_dev);
	return -EIO;

err1:
	printk(KERN_ERR "wacom_i2c:err1 occured(num:%d)\n", ret);
	input_free_device(wac_i2c->input_dev);
	wac_i2c->input_dev = NULL;
	return -EIO;

fail:
	printk(KERN_ERR "wacom_i2c:fail occured\n");
	return -ENOMEM;
}

static int wacom_i2c_remove(struct i2c_client *client)
{
	struct wacom_i2c *wac_i2c = i2c_get_clientdata(client);

	free_irq(client->irq, wac_i2c);
	input_unregister_device(wac_i2c->input_dev);
	kfree(wac_i2c);

	return 0;
}

static int wacom_i2c_suspend(struct i2c_client *client, pm_message_t mesg)
{
/*    int ret;
//    struct wacom_i2c *wac_i2c = i2c_get_clientdata(client);
//    ret = wac_i2c->power(0);
//    gpio_request(GPIO_PEN_SLP, "PEN_SLP");
//    s3c_gpio_cfgpin(GPIO_PEN_SLP, S3C_GPIO_OUTPUT);
//    gpio_direction_output(GPIO_PEN_SLP, 1);
*/
	return 0;
}

static int wacom_i2c_resume(struct i2c_client *client)
{
/*    gpio_request(GPIO_PEN_SLP, "PEN_SLP");
//    s3c_gpio_cfgpin(GPIO_PEN_SLP, S3C_GPIO_OUTPUT);
//    gpio_direction_output(GPIO_PEN_SLP, 0);
*/
	return 0;
}

static const struct i2c_device_id wacom_i2c_id[] = {
	{"wacom_g5_i2c", 0},
	{},
};

/*Create handler for wacom_i2c_driver*/
static struct i2c_driver wacom_i2c_driver = {
	.driver = {
		.name = "wacom_g5_i2c",
	},

	.probe = wacom_i2c_probe,
	.remove = wacom_i2c_remove,
	.suspend = wacom_i2c_suspend,
	.resume = wacom_i2c_resume,
	.id_table = wacom_i2c_id,
};

static int __init wacom_i2c_init(void)
{
	return i2c_add_driver(&wacom_i2c_driver);
}

static void __exit wacom_i2c_exit(void)
{
	i2c_del_driver(&wacom_i2c_driver);
}


late_initcall(wacom_i2c_init);
module_exit(wacom_i2c_exit);

MODULE_AUTHOR("Samsung");
MODULE_DESCRIPTION("Driver for Wacom G5 Digitizer Controller");

MODULE_LICENSE("GPL");

