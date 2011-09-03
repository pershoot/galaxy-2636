
#include <asm/io.h>
#include <linux/wacom_i2c.h>
#include <linux/kernel_sec_common.h>
#include <mach/io.h>
#include <mach/system.h>

#include "wacom_i2c_flash.h"

extern unsigned char onEmrProx;//GWAN

int wacom_i2c_test(struct wacom_i2c *wac_i2c)
{
	int ret, i;
	char buf, test[10];
	buf = COM_QUERY;

	ret = i2c_master_send(wac_i2c->client, &buf, sizeof(buf));
	if (ret > 0)
		printk(KERN_INFO "buf:%d, sent:%d\n", buf, ret);
	else {
		printk(KERN_ERR "Digitizer is not active\n");
		return -1;
	}

	ret = i2c_master_recv(wac_i2c->client, test, sizeof(test));
	if (ret >= 0) {
		for (i = 0; i < 8; i++)
			printk(KERN_INFO "%d\n", test[i]);
	} else {
		printk(KERN_ERR "Digitizer does not reply\n");
		return -1;
	}

	return 0;
}

int wacom_i2c_master_send(struct i2c_client *client,
			const char *buf, int count,
			unsigned short addr)
{
	int ret;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg;

	msg.addr = addr;
	msg.flags = client->flags & I2C_M_TEN;
	msg.len = count;
	msg.buf = (char *)buf;

	ret = i2c_transfer(adap, &msg, 1);

	/* If everything went ok (i.e. 1 msg transmitted), return #bytes
	transmitted, else error code. */
	return (ret == 1) ? count : ret;
}

int wacom_i2c_master_recv(struct i2c_client *client, char *buf,
			int count, unsigned short addr)
{
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg;
	int ret;

	msg.addr = addr;
	msg.flags = client->flags & I2C_M_TEN;
	msg.flags |= I2C_M_RD;
	msg.len = count;
	msg.buf = buf;

	ret = i2c_transfer(adap, &msg, 1);

	/* If everything went ok (i.e. 1 msg transmitted), return #bytes
	transmitted, else error code. */
	return (ret == 1) ? count : ret;
}

static int wacom_i2c_read_feature(struct wacom_i2c *wac_i2c, char *data)
{
	struct wacom_features *wac_feature = wac_i2c->wac_feature;
	struct i2c_msg msg[2];
	int ret;
	char buf;

	buf = COM_QUERY;

	msg[0].addr  = wac_i2c->client->addr;
	msg[0].flags = 0x00;
	msg[0].len   = 2;
	msg[0].buf   = (u8 *) &buf;

	msg[1].addr  = wac_i2c->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len   = COM_COORD_NUM;
	msg[1].buf   = (u8 *) data;
	
	ret = i2c_transfer(wac_i2c->client->adapter, msg, 2);
	if  (ret == 2) {
		wac_feature->x_max = ((u16)data[1]<<8)+(u16)data[2];
		wac_feature->y_max = ((u16)data[3]<<8)+(u16)data[4];
		wac_feature->pressure_max = (u16)data[6]+((u16)data[5]<<8);
		if ((u8)data[8] == 0xff)
			wac_feature->fw_version = (u8)data[7];
		else
			wac_feature->fw_version = (u8)data[8];

		printk(KERN_NOTICE
			"[WACOM] x_max:%d\n", wac_feature->x_max);
		printk(KERN_NOTICE
			"[WACOM] y_max:%d\n", wac_feature->y_max);
		printk(KERN_NOTICE
			"[WACOM] pressure_max:%d\n", wac_feature->pressure_max);
		printk(KERN_NOTICE
			"[WACOM] fw_version:%d\n", wac_feature->fw_version);

		return 0;
	}
	else {
		printk(KERN_ERR "Digitizer is not active:r(%d)\n", ret);
		return -1;
	}
	
}

int wacom_i2c_query(struct wacom_i2c *wac_i2c)
{
	struct wacom_features *wac_feature = wac_i2c->wac_feature;
	struct i2c_msg msg[2];
	int ret;
	char buf;
	u8 *data;
	int i2c_ok=0;

	buf = COM_QUERY;
	data = wac_feature->data;

	msg[0].addr  = wac_i2c->client->addr;
	msg[0].flags = 0x00;
	msg[0].len   = 2;
	msg[0].buf   = (u8 *) &buf;

	msg[1].addr  = wac_i2c->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len   = COM_COORD_NUM;
	msg[1].buf   = (u8 *) data;
	
	ret = i2c_transfer(wac_i2c->client->adapter, msg, 2);
	if  (ret == 2) {
		i2c_ok = 1;
	}
	else {
		printk(KERN_ERR "Digitizer is not active:r(%d)\n", ret);
		i2c_ok = 0;
	}

	if (i2c_ok) {
		wac_feature->x_max = ((u16)data[1]<<8)+(u16)data[2];
		wac_feature->y_max = ((u16)data[3]<<8)+(u16)data[4];
		wac_feature->pressure_max = (u16)data[6]+((u16)data[5]<<8);
		if ((u8)data[8] == 0xff)
			wac_feature->fw_version = (u8)data[7];
		else
			wac_feature->fw_version = (u8)data[8];

		printk(KERN_NOTICE
			"[WACOM] x_max:%d\n", wac_feature->x_max);
		printk(KERN_NOTICE
			"[WACOM] y_max:%d\n", wac_feature->y_max);
		printk(KERN_NOTICE
			"[WACOM] pressure_max:%d\n", wac_feature->pressure_max);
		printk(KERN_NOTICE
			"[WACOM] fw_version:%d\n", wac_feature->fw_version);

		if (wac_i2c->wac_pdata->firm_data)
			if (wac_feature->fw_version !=  wac_i2c->wac_pdata->firm_data->version) {
				pr_warning("Need to flash wacom digitizer; entering flashing\n");
				pr_warning("New version:%d\n", wac_i2c->wac_pdata->firm_data->version);
				ret = wacom_i2c_flash(wac_i2c);
				if (ret < 0) {
					pr_err("Flashing wacom digitizer failed\n");
					return -1;
				} 
				if (ret == 0) {
					msleep(1000);

					ret = wacom_i2c_read_feature(wac_i2c, data);
					if (ret < 0) {
						printk(KERN_ERR "failed to get wacom features\n");
						return -1;
					}
				}
			}
	} else {
		if (wac_i2c->wac_pdata->firm_data) {
			pr_warning("New version:%d\n", wac_i2c->wac_pdata->firm_data->version);
			ret = wacom_i2c_flash(wac_i2c);
			if (ret < 0) {
				pr_err("Flashing wacom digitizer failed\n");
				return -1;
			}
			msleep(1000);

			ret = wacom_i2c_read_feature(wac_i2c, data);
			if (ret < 0) {
				pr_err("failed to get wacom features\n");
				return -1;
			}
		}
	}


	return 0;
}

int wacom_i2c_coord(struct wacom_i2c *wac)
{
	bool prox = false;
	int ret = 0;
	u8 *data;
	int eraser;
	int stylus;
	u16 x, y, pressure;
	u16 tmp;

	data = wac->wac_feature->data;

	ret = i2c_master_recv(wac->client, data, COM_COORD_NUM);
	if (ret >= 0) {
		pr_debug("[WACOM] %x, %x, %x, %x, %x, %x, %x\n",
		data[0], data[1], data[2], data[3], data[4], data[5], data[6]);
		if (data[0]&0x80) {
			/* enable emr device */
			if (!wac->pen_prox) {
				if(data[0] & 0x40)
					wac->tool=DIGITIZER_TOOL_RUBBER;
				else
					wac->tool=DIGITIZER_TOOL_PEN;
				pr_debug("[WACOM] is in(%d)\n", wac->tool);
			}
			wac->pen_prox = 1;			

			prox = !!(data[0]&0x10);
			x = ((u16)data[1]<<8)+(u16)data[2];
			y = ((u16)data[3]<<8)+(u16)data[4];
			pressure = (u16)data[6]+((u16)data[5]<<8);
			eraser = !!(data[0]&0x40);
			stylus = !!(data[0]&0x20);
			if (wac->wac_pdata->x_invert)
				x = wac->wac_feature->x_max - x;
			if (wac->wac_pdata->y_invert)
				y = wac->wac_feature->y_max - y;
			
			if (wac->wac_pdata->xy_switch) {
				tmp = x;
				x = y;
				y = tmp;
			}

			/*	input_report_key(wac_i2c->input_dev, BTN_STYLUS2, 0);	*/
			/*	input_report_abs(wac_i2c->input_dev, ABS_MISC, 0);	*/
			input_report_abs(wac->input_dev, ABS_X, x);
			input_report_abs(wac->input_dev, ABS_Y, y);
			input_report_abs(wac->input_dev, ABS_PRESSURE, pressure);
			input_report_key(wac->input_dev, DIGITIZER_STYLUS, stylus);
			input_report_key(wac->input_dev, BTN_TOUCH, prox);
			input_report_key(wac->input_dev, wac->tool, 1);
			input_sync(wac->input_dev);

			if(prox && !wac->pen_pressed)
				pr_info("[WACOM] is pressed(%d,%d)(%d)\n", x, y, wac->tool);
			if(!prox && wac->pen_pressed)
				pr_info("[WACOM] is released(%d,%d)(%d)\n", x, y, wac->tool);
			wac->pen_pressed = prox;

			if(stylus && !wac->side_pressed)
				pr_info("[WACOM] side on(%d,%d)(%d)\n", x, y, wac->tool);
			if(!stylus && wac->side_pressed)
				pr_info("[WACOM] side off(%d,%d)(%d)\n", x, y, wac->tool);
			wac->side_pressed = stylus;

			/*	printk(KERN_DEBUG "[WACOM] x:%d y:%d\n",x, y);		*/
			/*	printk(KERN_DEBUG "[WACOM] pressure:%d\n",pressure);	*/
		} else {
			/* disable emr device */
			if (wac->pen_prox) {
				/* input_report_abs(wac->input_dev, ABS_X, x); */
				/* input_report_abs(wac->input_dev, ABS_Y, y); */
				input_report_abs(wac->input_dev, ABS_PRESSURE, 0);
				input_report_key(wac->input_dev, DIGITIZER_STYLUS, 0);
				input_report_key(wac->input_dev, BTN_TOUCH, 0);
				input_report_key(wac->input_dev, wac->tool, 0);
				input_sync(wac->input_dev);
				if (wac->pen_pressed || wac->side_pressed)
					pr_info("[WACOM] is out(%d)\n", wac->tool);
				else
					pr_debug("[WACOM] is out(%d)\n", wac->tool);
				/* check_emr_device(false); */
				/* unset dvfs level */
			}
			wac->pen_prox = 0;			
			wac->pen_pressed = 0;
			wac->side_pressed = 0;
		}
	} else {
		printk(KERN_ERR "[WACOM] failed to read i2c\n");
		return -1;
	}

	return 0;
}
