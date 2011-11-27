/*
 * f_diag.c - generic USB serial function driver (modified from f_serial.c)
 * ttygs2
*
 * Copyright (C) 2003 Al Borchers (alborchers@steinerpoint.com)
 * Copyright (C) 2008 by David Brownell
 * Copyright (C) 2008 by Nokia Corporation
 *
 * This software is distributed under the terms of the GNU General
 * Public License ("GPL") as published by the Free Software Foundation,
 * either version 2 of that License or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/device.h>

#include "u_serial.h"
#include "gadget_chips.h"

#include "f_diag.h"  // by NN
#include <linux/usb/android_composite.h>


//#define diag_EP_TEST

/*
 * This function packages a simple "generic serial" port with no real
 * control mechanisms, just raw data transfer over two bulk endpoints.
 *
 * Because it's not standardized, this isn't as interoperable as the
 * CDC ACM driver.  However, for many purposes it's just as functional
 * if you can arrange appropriate host side drivers.
 */

struct diag_descs {
	struct usb_endpoint_descriptor	*in;
	struct usb_endpoint_descriptor	*out;
};

struct f_diag {
	struct gserial			port;
	u8				data_id;
	u8				port_num;

	struct diag_descs		fs;
	struct diag_descs		hs;
};


static struct f_diag *_f_diag;

static inline struct f_diag *func_to_diag(struct usb_function *f)
{
	return container_of(f, struct f_diag, port.func);
}

/*-------------------------------------------------------------------------*/

/* interface descriptor: */

static struct usb_interface_descriptor diag_interface_desc  = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	/* .bInterfaceNumber = DYNAMIC */
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass =	0x10, //0,
	.bInterfaceProtocol =	0x01, //0,
	/* .iInterface = DYNAMIC */
};

/* full speed support: */

static struct usb_endpoint_descriptor diag_fs_in_desc  = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor diag_fs_out_desc  = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *diag_fs_function[]  = {
	(struct usb_descriptor_header *) &diag_interface_desc,
	(struct usb_descriptor_header *) &diag_fs_in_desc,
	(struct usb_descriptor_header *) &diag_fs_out_desc,
	NULL,
};

/* high speed support: */

static struct usb_endpoint_descriptor diag_hs_in_desc  = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	__constant_cpu_to_le16(512),
};

static struct usb_endpoint_descriptor diag_hs_out_desc  = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	__constant_cpu_to_le16(512),
};

static struct usb_descriptor_header *diag_hs_function[]  = {
	(struct usb_descriptor_header *) &diag_interface_desc,
	(struct usb_descriptor_header *) &diag_hs_in_desc,
	(struct usb_descriptor_header *) &diag_hs_out_desc,
	NULL,
};

/* string descriptors: */
#define F_diag_IDX	0
static struct usb_string diag_string_defs[] = {
//	[0].s = "Generic Serial",
	[F_diag_IDX].s = "Samsung Android diag",
	{  /* ZEROES END LIST */ },
};

static struct usb_gadget_strings diag_string_table = {
	.language =		0x0409,	/* en-us */
	.strings =		diag_string_defs,
};

static struct usb_gadget_strings *diag_strings[] = {
	&diag_string_table,
	NULL,
};

/*-------------------------------------------------------------------------*/

static int diag_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct f_diag		*diag = func_to_diag(f);
	struct usb_composite_dev *cdev = f->config->cdev;

	/* we know alt == 0, so this is an activation or a reset */
    
	if (diag->port.in->driver_data) {
		DBG(cdev, "reset generic ttyGS%d\n", diag->port_num);
		gserial_disconnect(&diag->port);
		diag->port.in_desc = ep_choose(cdev->gadget,
				diag->hs.in, diag->fs.in);
		diag->port.out_desc = ep_choose(cdev->gadget,
				diag->hs.out, diag->fs.out);
	} else {
		DBG(cdev, "activate generic ttyGS%d\n", diag->port_num);
		diag->port.in_desc = ep_choose(cdev->gadget,
				diag->hs.in, diag->fs.in);
		diag->port.out_desc = ep_choose(cdev->gadget,
				diag->hs.out, diag->fs.out);
	}	
	gserial_connect(&diag->port, diag->port_num);
	return 0;
}

static void diag_disable(struct usb_function *f)
{
	struct f_diag	*diag = func_to_diag(f);
	struct usb_composite_dev *cdev = f->config->cdev;

	DBG(cdev, "generic ttyGS%d deactivated\n", diag->port_num);
	gserial_disconnect(&diag->port);
}

/*-------------------------------------------------------------------------*/

/* serial function driver setup/binding */

static int __init
diag_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_diag		*diag = func_to_diag(f);
	int			status;
	struct usb_ep		*ep;

	/* allocate instance-specific interface IDs */
	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	diag->data_id = status;
	diag_interface_desc.bInterfaceNumber = status;

	status = -ENODEV;

	/* allocate instance-specific endpoints */
	ep = usb_ep_autoconfig(cdev->gadget, &diag_fs_in_desc);
	if (!ep)
		goto fail;
	diag->port.in = ep;
	ep->driver_data = cdev;	/* claim */

	ep = usb_ep_autoconfig(cdev->gadget, &diag_fs_out_desc);
	if (!ep)
		goto fail;
	diag->port.out = ep;
	ep->driver_data = cdev;	/* claim */
//	printk("[%s] in =0x%x , out =0x%x \n", __func__,diag->port.in ,diag->port.out );

	/* copy descriptors, and track endpoint copies */
	f->descriptors = usb_copy_descriptors(diag_fs_function);

	diag->fs.in = usb_find_endpoint(diag_fs_function,
			f->descriptors, &diag_fs_in_desc);
	diag->fs.out = usb_find_endpoint(diag_fs_function,
			f->descriptors, &diag_fs_out_desc);


	/* support all relevant hardware speeds... we expect that when
	 * hardware is dual speed, all bulk-capable endpoints work at
	 * both speeds
	 */
	if (gadget_is_dualspeed(c->cdev->gadget)) {
		diag_hs_in_desc.bEndpointAddress =
				diag_fs_in_desc.bEndpointAddress;
		diag_hs_out_desc.bEndpointAddress =
				diag_fs_out_desc.bEndpointAddress;

		/* copy descriptors, and track endpoint copies */
		f->hs_descriptors = usb_copy_descriptors(diag_hs_function);

		diag->hs.in = usb_find_endpoint(diag_hs_function,
				f->hs_descriptors, &diag_hs_in_desc);
		diag->hs.out = usb_find_endpoint(diag_hs_function,
				f->hs_descriptors, &diag_hs_out_desc);
	}

	DBG(cdev, "generic ttyGS%d: %s speed IN/%s OUT/%s\n",
			diag->port_num,
			gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full",
			diag->port.in->name, diag->port.out->name);
	return 0;

fail:
	/* we might as well release our claims on endpoints */
	if (diag->port.out)
		diag->port.out->driver_data = NULL;
	if (diag->port.in)
		diag->port.in->driver_data = NULL;

	ERROR(cdev, "%s: can't bind, err %d\n", f->name, status);

	return status;
}

static void
diag_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct f_diag		*diag = func_to_diag(f);

	if (gadget_is_dualspeed(c->cdev->gadget))
		usb_free_descriptors(f->hs_descriptors);
	usb_free_descriptors(f->descriptors);
#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
/* refered from S1 */
	gserial_disconnect(&diag->port);
	gserial_cleanup();
#endif
	kfree(diag);
}

#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
static int diag_set_interface_id(struct usb_function *f,
	int intf_num,
	int index_num)
{
	int ret = 0;
	struct f_diag		*diag = func_to_diag(f);

	if (gadget_is_dualspeed(f->config->cdev->gadget)) {
		if (index_num == 0) {
			if (usb_change_interface_num(diag_hs_function,
				f->hs_descriptors, &diag_interface_desc,
				intf_num)) {
				diag->data_id = intf_num;
			}
			ret = 1;			
		} else {
			printk(KERN_DEBUG "usb diag has only 1 interface. please check it\n");
		}
	} else {
		if (index_num == 0) {
			if (usb_change_interface_num(diag_fs_function,
				f->descriptors, &diag_interface_desc,
				intf_num)) {
				diag->data_id = intf_num;
			}
			ret = 1;			
		} else {
			printk(KERN_DEBUG "usb diag has only 1 interface. please check it\n");
		}
	}
	return ret;
}
#endif

/**
 * diag_bind_config - add a generic serial function to a configuration
 * @c: the configuration to support the serial instance
 * @port_num: /dev/ttyGS* port this interface will use
 * Context: single threaded during gadget setup
 *
 * Returns zero on success, else negative errno.
 *
 * Caller must have called @gserial_setup() with enough ports to
 * handle all the ones it binds.  Caller is also responsible
 * for calling @gserial_cleanup() before module unload.
 */
 int diag_bind_config(struct usb_configuration *c, u8 port_num)
{
	struct f_diag	*diag;
	int		status;

	/* REVISIT might want instance-specific strings to help
	 * distinguish instances ...
	 */

	/* maybe allocate device-global string ID */
	if (diag_string_defs[F_diag_IDX].id == 0) {
		status = usb_string_id(c->cdev);
		if (status < 0)
			return status;
		diag_string_defs[F_diag_IDX].id = status;
	}

	/* allocate and initialize one new instance */
	diag = kzalloc(sizeof *diag, GFP_KERNEL);
	if (!diag)
		return -ENOMEM;

	diag->port_num = port_num;

	diag->port.func.name = "diag";
	diag->port.func.strings = diag_strings;
	diag->port.func.bind = diag_bind;
	diag->port.func.unbind = diag_unbind;
	diag->port.func.set_alt = diag_set_alt;
	diag->port.func.disable = diag_disable;
  
#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
	diag->port.func.set_intf_num = diag_set_interface_id;
#endif

	_f_diag=diag; //by NN 08.23

	status = usb_add_function(c, &diag->port.func);
	if (status)
		kfree(diag);
	return status;
}

//changed by NN for adding DM 08.23 +
 int diag_function_add(struct usb_configuration *c)
{
	int ret;
	
	printk(KERN_INFO "diag_function_add\n");
	printk("LnT:=================diag_function_add============\n");
          ret = gserial_setup(c->cdev->gadget, 3);
	printk("LnT:=================gserial_setup:0x%x============\n",ret);

        if (ret)
                return ret;

	ret = diag_bind_config(c, 1);  //Changed by NN
	if (ret) {
		printk("[%s] Fail to diag_bind_config()\n", __func__);
		gserial_cleanup();
		return ret;
	}

	return ret;
}
static struct android_usb_function diag_function = {
	.name = "diag",
	.bind_config = diag_function_add,
};

static int __init init(void)
{
	printk(KERN_INFO "f_diag init\n");
	android_register_function(&diag_function);
	return 0;
}
module_init(init);







