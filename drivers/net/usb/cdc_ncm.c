/*
 * USB CDC NCM class device driver
 *
 * Copyright (C) 2009-2011	NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/mii.h>
#include <linux/crc32.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/usb/usbnet.h>
#include <linux/version.h>


/***************************************************
	NCM Class Definition
***************************************************/

/* NCM request code */
enum ncm_control_requests {
	GET_NTB_PARAMETERS 	= 0x80,	/* required */
	GET_NET_ADDRESS 	= 0x81,
	SET_NET_ADDRESS 	= 0x82,
	GET_NTB_FORMAT 		= 0x83,
	SET_NTB_FORMAT 		= 0x84,	/* 0: NTB-16; 1:NTB-32 */
	GET_NTB_INPUT_SIZE 	= 0x85,	/* required */
	SET_NTB_INPUT_SIZE 	= 0x86,	/* required */
	GET_MAX_DATAGRAM_SIZE 	= 0x87,
	SET_MAX_DATAGRAM_SIZE 	= 0x88,
	GET_CRC_MODE 		= 0x89,
	SET_CRC_MODE 		= 0x8A,
};

/* NCM GET_NTB_PARAMETERS Response */
struct ntb_params {
	__le16 wLength;		/* should be 0x1c */
	__le16 bmNtbFormatsSupported;	/* bit0: NTB-16 (must); bit1: NTB-32 */
	__le32 dwNtbInMaxSize;
	__le16 wNdpInDivisor;
	__le16 wNdpInPayloadRemainder;
	__le16 wNdpInAlignment;
	__le16 wReserved1;	/* zero padding */
	__le32 dwNtbOutMaxSize;
	__le16 wNdpOutDivisor;
	__le16 wNdpOutPayloadRemainder;
	__le16 wNdpOutAlignment;
	__le16 wReserved2;	/* zero padding */
} __attribute__ ((packed));

/***************************************************
	NCM Transfer Block Definition
***************************************************/

#define NTH16_SIGNATURE		0x484D434E
#define NTH32_SIGNATURE		0x686D636E
#define NDP16_SIGNATURE		0x304D434E
#define NDP16_SIG_CRC32		0x314D434E
#define NDP32_SIGNATURE		0x306D636E
#define NDP32_SIG_CRC32		0x316D636E

/* NTH16 NCM Transfer Header */
struct nth16_hdr {
	__le32 dwSignature;	/* signature: must be "NCMH" */
	__le16 wHeaderLength;	/* header length: 0x0C */
	__le16 wSequence;	/* sequence number */
	__le16 wBlockLength;	/* size of this NTB in bytes */
	__le16 wFpIndex;	/* offset of the first NDP16 from the byte zero
				   of the NTB: must be multiple of 4 */
} __attribute__ ((packed));

/* NDP16 NCM Datagram Pointer Header */
struct ndp16_hdr {
	__le32 dwSignature;	/* NDP16 signature */
	__le16 wLength;		/* size of this NDP16 in bytes, must be multiple
				   of 4 and must be at least 16 */
	__le16 wNextFpIndex;	/* byte index to the next NDP16 */
} __attribute__ ((packed));

/* NDP16 NCM Datagram Pointer Entry */
struct ndp16_ent {
	__le16 wDatagramIndex;	/* offset from byte 0 of the NTB */
	__le16 wDatagramLength;	/* length */
};

/* NTH32 NCM Transfer Header */
struct nth32_hdr {
	__le32 dwSignature;	/* signature: must be "ncmh" */
	__le16 wHeaderLength;	/* header length: 0x10 */
	__le16 wSequence;	/* sequence number */
	__le32 dwBlockLength;	/* size of this NTB in bytes */
	__le32 dwFpIndex;	/* offset of the first NDP32 from the byte zero
				   of the NTB: must be multiple of 4 */
} __attribute__ ((packed));

/* NDP32 NCM Datagram Pointer Header */
struct ndp32_hdr {
	__le32 dwSignature;
	__le16 wLength;
	__le16 wResvered6;
	__le32 dwNextNdpIndex;
	__le32 dwReserved12;
} __attribute__ ((packed));

/* NDP32 NCM Datagram Pointer Entry */
struct ndp32_ent {
	__le32 dwDatagramIndex;
	__le32 dwDatagramLength;
};

/* used to check in NCM frames */
#define MIN_NDP16_SIZE	(sizeof(struct ndp16_hdr) + sizeof(struct ndp16_ent)*2)
#define MIN_NDP32_SIZE	(sizeof(struct ndp32_hdr) + sizeof(struct ndp32_ent)*2)

/* used to creat out NCM frames */
#define MAX_NDP16_ENTRIES	5
#define MAX_NDP16_SIZE	(sizeof(struct ndp16_hdr) + sizeof(struct ndp16_ent)*6)

/* private driver data for each NCM device */
struct driver_params {
	struct ntb_params ntb_params;
	u8 ntb_format;
	u8 crc_mode;
	u16 max_datagram_size;
	u16 ndp_offset;
	u16 last_rx_seq;
	u16 tx_seq;
};

/***************************************************
	NCM Rx frame fixup
***************************************************/

static int ncm_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	struct nth16_hdr hdr;
	struct ndp16_hdr fph;
	struct ndp16_ent e;
	u8 *head;
	u8 *fp;
	struct driver_params *params = (struct driver_params *)dev->driver_priv;

	head = (u8 *) skb->data;
	memcpy(&hdr, head, sizeof(struct nth16_hdr));
	le32_to_cpus(&hdr.dwSignature);
	le16_to_cpus(&hdr.wHeaderLength);
	le16_to_cpus(&hdr.wSequence);
	le16_to_cpus(&hdr.wBlockLength);
	le16_to_cpus(&hdr.wFpIndex);

	/* check block header */
	if (hdr.dwSignature != NTH16_SIGNATURE) {
		netdev_err(dev->net, "Invalid NCM block signature!\n");
		return 0;
	}

	if (hdr.wHeaderLength != sizeof(struct nth16_hdr)) {
		netdev_err(dev->net, "Invalid NCM block header size!\n");
		return 0;
	}

	if (hdr.wBlockLength != skb->len) {
		netdev_err(dev->net,
		    "NCM block size doesn't match the input size (%u != %u)\n",
		    hdr.wBlockLength, skb->len);
		return 0;
	}

	if (!IS_ALIGNED(hdr.wFpIndex, 4)) {
		netdev_err(dev->net, "wFpIndex is not 4-byte aligned!\n");
		return 0;
	}

	/* missing frame? */
	if ((hdr.wSequence - params->last_rx_seq) > 1)
		netdev_warn(dev->net, "missing frame (seq# %u last seq# %u)\n",
			hdr.wSequence, params->last_rx_seq);
	params->last_rx_seq = hdr.wSequence;

	fp = head + hdr.wFpIndex;
	memcpy(&fph, fp, sizeof(struct ndp16_hdr));
	le32_to_cpus(&fph.dwSignature);
	le16_to_cpus(&fph.wLength);
	le16_to_cpus(&fph.wNextFpIndex);

	/* check frame pointer header */
	if ((params->crc_mode && fph.dwSignature != NDP16_SIG_CRC32) ||
	    (fph.dwSignature != NDP16_SIGNATURE) ||
	    (fph.wLength < MIN_NDP16_SIZE) || !IS_ALIGNED(fph.wLength, 4)) {
		netdev_err(dev->net, "Invalid NCM frame pointer header!\n");
		return 0;
	}

	fp += sizeof(struct ndp16_hdr);
	memcpy(&e, fp, sizeof(struct ndp16_ent));
	le16_to_cpus(&e.wDatagramIndex);
	le16_to_cpus(&e.wDatagramLength);

	while (e.wDatagramLength > 0) {
		unsigned char *frame;
		struct sk_buff *new_skb;

		if (e.wDatagramLength > params->max_datagram_size) {
			netdev_err(dev->net, "Bad frame length: %d\n",
				e.wDatagramLength);
			return 0;
		}

		frame = head + e.wDatagramIndex;

		new_skb = skb_clone(skb, GFP_ATOMIC);
		if (new_skb) {
			new_skb->len = e.wDatagramLength;
			new_skb->data = frame;
			skb_set_tail_pointer(new_skb, e.wDatagramLength);
			usbnet_skb_return(dev, new_skb);
		} else {
			return 0;
		}

		/* next frame */
		fp += sizeof(struct ndp16_ent);
		memcpy(&e, fp, sizeof(struct ndp16_ent));
		le16_to_cpus(&e.wDatagramIndex);
		le16_to_cpus(&e.wDatagramLength);

		/* check sequence end mark */
		if (e.wDatagramIndex == 0)
			break;
	}

	skb_pull(skb, skb->len);

	return 1;
}

/***************************************************
	NCM Tx frame fixup
***************************************************/

static struct sk_buff *ncm_tx_fixup(struct usbnet *dev, struct sk_buff *skb,
				    gfp_t flags)
{
	u32 blk_size;
	u32 padlen;
	u32 dgm_offset;
	u32 dgm_len;
	int headroom = skb_headroom(skb);
	int tailroom = skb_tailroom(skb);
	struct nth16_hdr hdr;
	struct ndp16_hdr fph;
	struct ndp16_ent e;
	struct driver_params *params = (struct driver_params *)dev->driver_priv;

	dgm_offset = params->ndp_offset + 32;
	dgm_len = ALIGN(skb->len, params->ntb_params.wNdpOutDivisor);

	blk_size = dgm_len + dgm_offset;
	if (blk_size > params->ntb_params.dwNtbOutMaxSize) {
		netdev_err(dev->net, "Tx blk size is too big: %d\n", blk_size);
		return NULL;
	}

	padlen = dgm_len - skb->len;

	/* create an NCM transfer block */
	hdr.dwSignature = cpu_to_le32(NTH16_SIGNATURE);
	hdr.wHeaderLength = cpu_to_le16(sizeof(struct nth16_hdr));
	hdr.wSequence = cpu_to_le16(params->tx_seq);
	if ((blk_size % dev->maxpacket) == 0)
		hdr.wBlockLength = cpu_to_le16(blk_size + 1);
	else
		hdr.wBlockLength = cpu_to_le16(blk_size);

	/* FP starts right after the block header */
	hdr.wFpIndex = cpu_to_le16(params->ndp_offset);
	params->tx_seq++;

	fph.dwSignature = cpu_to_le32(NDP16_SIGNATURE);
	/* send only one frame */
	fph.wLength = cpu_to_le16(sizeof(struct ndp16_hdr) +
				  sizeof(struct ndp16_ent) * 2);
	fph.wNextFpIndex = 0;

	/* first frame */
	e.wDatagramIndex = cpu_to_le16(dgm_offset);
	e.wDatagramLength = cpu_to_le16(skb->len);

	if ((!skb_cloned(skb))
	    && ((headroom + tailroom) >= (dgm_offset + padlen))) {
		if ((headroom < dgm_offset) || (tailroom < padlen)) {
			skb->data = memmove(skb->head + dgm_offset, skb->data,
					    skb->len);
			skb_set_tail_pointer(skb, skb->len);
		}
	} else {
		struct sk_buff *skb2;
		skb2 = skb_copy_expand(skb, dgm_offset, padlen, flags);
		dev_kfree_skb_any(skb);
		skb = skb2;
		if (!skb)
			return NULL;
	}

	skb_push(skb, dgm_offset);
	memset(skb->data, 0, dgm_offset);
	memcpy(skb->data, &hdr, sizeof(struct nth16_hdr));
	memcpy(skb->data + params->ndp_offset, &fph, sizeof(struct ndp16_hdr));
	memcpy(skb->data + params->ndp_offset + sizeof(struct ndp16_hdr), &e,
	       sizeof(struct ndp16_ent));
	skb_put(skb, padlen);

	if (skb->len != blk_size) {
		netdev_err(dev->net, "skb->len != blk_size (%d)\n", blk_size);
		return NULL;
	}

	return skb;
}

/***************************************************
	Send NCM control message
***************************************************/

static int send_ctrl_msg(struct usbnet *dev,
			 u8 dir,
			 u8 request, u16 value, u16 index, void *data, int size)
{
	int retval;

	retval = usb_control_msg(dev->udev, dir ?
				 usb_rcvctrlpipe(dev->udev, 0) :
				 usb_sndctrlpipe(dev->udev, 0),
				 request,
				 dir | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
				 value, index, data, size, 1000);

	if (retval != size)
		netdev_err(dev->net, "usb_control_msg error: %d\n", retval);

	return (retval < 0) ? retval : 0;
}

/***************************************************
	Change MTU
***************************************************/

static int ncm_change_mtu(struct net_device *net, int new_mtu)
{
	struct usbnet *dev = netdev_priv(net);
	struct cdc_state *info = (void *)dev->data;
	struct driver_params *params = (struct driver_params *)dev->driver_priv;
	u16 max_datagram_size;
	int hard_mtu = new_mtu + net->hard_header_len;

	netdev_dbg(dev->net, "new_mtu=%d\n", new_mtu);

	if (new_mtu <= 0 || hard_mtu > 16384)
		return -EINVAL;

	if ((hard_mtu % dev->maxpacket) == 0)
		return -EDOM;

	max_datagram_size = cpu_to_le16(hard_mtu);

	/* assuming the device won't allow changing the max datagram size
	 * larger than max NTB in/out size
	 */
	if (send_ctrl_msg(dev, USB_DIR_OUT, SET_MAX_DATAGRAM_SIZE, 0,
			  info->u->bMasterInterface0, &max_datagram_size, 2)) {
		netdev_warn(dev->net,
			"SET_MAX_DATAGRAM_SIZE request failed!\n");
	}

	if (send_ctrl_msg(dev, USB_DIR_IN, GET_MAX_DATAGRAM_SIZE, 0,
			  info->u->bMasterInterface0, &max_datagram_size, 2)) {
		netdev_warn(dev->net,
			"GET_MAX_DATAGRAM_SIZE request failed!\n");
	} else {
		le16_to_cpus(&max_datagram_size);
		netdev_dbg(dev->net,
			"max datagram size: %d\n", max_datagram_size);
	}

	if (max_datagram_size < hard_mtu)
		return -EINVAL;

	/* update the max datagram size in the driver parameters */
	if (params->max_datagram_size < max_datagram_size)
		params->max_datagram_size = max_datagram_size;

	net->mtu = new_mtu;
	dev->hard_mtu = hard_mtu;

	return 0;
}

static const struct net_device_ops ncm_netdev_ops = {
	.ndo_open		= usbnet_open,
	.ndo_stop		= usbnet_stop,
	.ndo_start_xmit		= usbnet_start_xmit,
	.ndo_tx_timeout		= usbnet_tx_timeout,
	.ndo_change_mtu		= ncm_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

/***************************************************
	CDC NCM Binding
***************************************************/

static int cdc_ncm_bind(struct usbnet *dev, struct usb_interface *intf)
{
	struct cdc_state *info = (void *)&dev->data;
	int retval;
	struct usb_driver *driver = driver_of(intf);
	struct driver_params *params;
	u32 max_ntb_in_size = 0;
	u16 max_datagram_size = 0;
	u16 ntb_format = 0;
	u16 crc_mode = 0;

	retval = usbnet_generic_cdc_bind(dev, intf);
	if (retval < 0)
		return retval;

	/* create private driver data */
	dev->driver_priv = kmalloc(sizeof(struct driver_params), GFP_KERNEL);
	if (dev->driver_priv == NULL) {
		usb_set_intfdata(info->data, NULL);
		usb_driver_release_interface(driver, info->data);
		return -ENOMEM;
	}

	params = (struct driver_params *)dev->driver_priv;
	memset(params, 0, sizeof(struct driver_params));

	/* get NTB parameters */
	if (send_ctrl_msg(dev, USB_DIR_IN, GET_NTB_PARAMETERS, 0,
			  info->u->bMasterInterface0,
			  &params->ntb_params, sizeof(struct ntb_params))) {
		dev_dbg(&intf->dev, "GET_NTB_PARAMETERS request failed!\n");
		goto bad_param;
	} else {
		le16_to_cpus(&params->ntb_params.wLength);
		le16_to_cpus(&params->ntb_params.bmNtbFormatsSupported);
		le16_to_cpus(&params->ntb_params.dwNtbInMaxSize);
		le16_to_cpus(&params->ntb_params.wNdpInDivisor);
		le16_to_cpus(&params->ntb_params.wNdpInPayloadRemainder);
		le16_to_cpus(&params->ntb_params.wNdpInAlignment);
		le16_to_cpus(&params->ntb_params.dwNtbOutMaxSize);
		le16_to_cpus(&params->ntb_params.wNdpOutDivisor);
		le16_to_cpus(&params->ntb_params.wNdpOutPayloadRemainder);
		le16_to_cpus(&params->ntb_params.wNdpOutAlignment);

		dev_dbg(&intf->dev, "bmNtbFormatsSupported: %u\n",
			params->ntb_params.bmNtbFormatsSupported);
		dev_dbg(&intf->dev, "ndwNtbInMaxSize: %u\n",
			params->ntb_params.dwNtbInMaxSize);
		dev_dbg(&intf->dev, "nwNdpInDivisor: %u\n",
			params->ntb_params.wNdpInDivisor);
		dev_dbg(&intf->dev, "nwNdpInPayloadRemainder: %u\n",
			params->ntb_params.wNdpInPayloadRemainder);
		dev_dbg(&intf->dev, "nwNdpInAlignment: %u\n",
			params->ntb_params.wNdpInAlignment);
		dev_dbg(&intf->dev, "ndwNtbOutMaxSize: %u\n",
			params->ntb_params.dwNtbOutMaxSize);
		dev_dbg(&intf->dev, "nwNdpOutDivisor: %u\n",
			params->ntb_params.wNdpOutDivisor);
		dev_dbg(&intf->dev, "nwNdpOutPayloadRemainder: %u\n",
			params->ntb_params.wNdpOutPayloadRemainder);
		dev_dbg(&intf->dev, "nwNdpOutAlignment: %u\n",
			params->ntb_params.wNdpOutAlignment);
	}

	/* NCM spec 6.2.7 */
	if (params->ntb_params.dwNtbInMaxSize < 2048) {
		dev_dbg(&intf->dev, "invalid NtbInMaxSize\n");
		goto bad_param;
	}

	/* get max NTB input size */
	if (send_ctrl_msg(dev, USB_DIR_IN, GET_NTB_INPUT_SIZE, 0,
			  info->u->bMasterInterface0, &max_ntb_in_size, 4)) {
		dev_dbg(&intf->dev, "GET_NTB_INPUT_SIZE request failed!\n");
		goto bad_param;
	} else {
		le32_to_cpus(&max_ntb_in_size);
		dev_dbg(&intf->dev, "max ntb input size: %u\n",
			max_ntb_in_size);
	}

	dev->rx_urb_size = max_ntb_in_size;

	/* --------- the following control requests are optional --------- */

	/* get net address */
	if (send_ctrl_msg(dev, USB_DIR_IN, GET_NET_ADDRESS, 0,
			  info->u->bMasterInterface0, dev->net->dev_addr,
			  ETH_ALEN)) {
		dev_dbg(&intf->dev, "GET_NET_ADDRESS request failed!\n");
	} else {
		dev_dbg(&intf->dev, "HW addr: "MAC_FMT"\n",
			dev->net->dev_addr[0],
			dev->net->dev_addr[1],
			dev->net->dev_addr[2],
			dev->net->dev_addr[3],
			dev->net->dev_addr[4],
			dev->net->dev_addr[5]);
	}

	/* get NTB format */
	if (send_ctrl_msg(dev, USB_DIR_IN, GET_NTB_FORMAT, 0,
			  info->u->bMasterInterface0, &ntb_format, 2)) {
		dev_dbg(&intf->dev, "GET_NTB_FORMAT request failed!\n");
	} else {
		le16_to_cpus(&ntb_format);
		dev_dbg(&intf->dev, "ntb_format: %s\n", (ntb_format) ?
			"NTB32" : "NTB16");
	}

	/* TODO: only support ntb16 for now - will support ntb32 for usb 3.0 */
	if (ntb_format != 0) {
		dev_dbg(&intf->dev, "The ntb32 is not supported!\n");
		goto bad_param;
	}

	/* get max datagram size */
	if (send_ctrl_msg(dev, USB_DIR_IN, GET_MAX_DATAGRAM_SIZE, 0,
			  info->u->bMasterInterface0, &max_datagram_size, 2)) {
		dev_dbg(&intf->dev, "GET_MAX_DATAGRAM_SIZE request failed!\n");
	} else {
		le16_to_cpus(&max_datagram_size);
		dev_dbg(&intf->dev, "max datagram size: %d\n",
			max_datagram_size);
	}

	if (max_datagram_size < ETH_FRAME_LEN)
		goto bad_param;

	le16_to_cpus(&max_datagram_size);
	params->max_datagram_size = max_datagram_size;

	/* get CRC mode */
	if (send_ctrl_msg(dev, USB_DIR_IN, GET_CRC_MODE, 0,
			  info->u->bMasterInterface0, &crc_mode, 2)) {
		dev_dbg(&intf->dev, "GET_CRC_MODE request failed!\n");
	} else {
		le16_to_cpus(&crc_mode);
		dev_dbg(&intf->dev, "crc mode: %d\n", crc_mode);
	}

	/* disable crc mode in case it is enabled */
	if (crc_mode) {
		crc_mode = 0;
		if (send_ctrl_msg(dev, USB_DIR_OUT, SET_CRC_MODE, 0,
				  info->u->bMasterInterface0, &crc_mode, 2)) {
			dev_dbg(&intf->dev, "SET_CRC_MODE request failed!\n");
			goto bad_param;
		}
	}

	params->ntb_format = ntb_format;
	params->crc_mode = crc_mode;
	params->ndp_offset = ALIGN(sizeof(struct nth16_hdr),
				   params->ntb_params.wNdpOutAlignment);

	/* reserve headroom space for NTH and NDP to avoid memmove */
	dev->net->needed_headroom = params->ndp_offset + 32;
	dev->net->needed_tailroom = params->ntb_params.wNdpOutDivisor;
	dev->net->netdev_ops = &ncm_netdev_ops;

	strcpy(dev->net->name, "ncm%d");

	return 0;

bad_param:
	usb_set_intfdata(info->data, NULL);
	usb_driver_release_interface(driver, info->data);
	kfree(dev->driver_priv);
	return -ENODEV;
}

/***************************************************
	CDC NCM Unbind
***************************************************/

static void cdc_ncm_unbind(struct usbnet *dev, struct usb_interface *intf)
{
	usbnet_cdc_unbind(dev, intf);

	/* free private driver data */
	kfree(dev->driver_priv);
}

static int cdc_manage_power(struct usbnet *dev, int on)
{
	dev->intf->needs_remote_wakeup = on;
	return 0;
}

static const struct driver_info ncm_info = {
	.description = "NCM device",
	.flags = FLAG_ETHER,
	.tx_fixup = ncm_tx_fixup,
	.rx_fixup = ncm_rx_fixup,
	.bind = cdc_ncm_bind,
	.unbind = cdc_ncm_unbind,
	.status = usbnet_cdc_status,
	.manage_power =	cdc_manage_power,
};

static const struct driver_info mbm_info = {
	.description = "MBM device",
	.flags = FLAG_WWAN,
	.tx_fixup = ncm_tx_fixup,
	.rx_fixup = ncm_rx_fixup,
	.bind = cdc_ncm_bind,
	.unbind = cdc_ncm_unbind,
	.status = usbnet_cdc_status,
	.manage_power =	cdc_manage_power,
};

static const struct usb_device_id products[] = {
	{
	 /* Ericsson f5521gw */
	 .match_flags = USB_DEVICE_ID_MATCH_INT_INFO
		| USB_DEVICE_ID_MATCH_DEVICE,
	 USB_DEVICE(0x0bdb,0x190d),
	 .driver_info = (unsigned long)&mbm_info,
	 },
	{
	 /* standard NCM class device */
	 USB_INTERFACE_INFO(USB_CLASS_COMM, USB_CDC_SUBCLASS_NCM,
			    USB_CDC_PROTO_NONE),
	 .driver_info = (unsigned long)&ncm_info,
	 },
	{},
};

MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver ncm_driver = {
	.name = "CDC NCM device",
	.id_table = products,
	.probe = usbnet_probe,
	.disconnect = usbnet_disconnect,
	.suspend = usbnet_suspend,
	.resume = usbnet_resume,
	.reset_resume =	usbnet_resume,
	.supports_autosuspend = 1,
};

static int __init ncm_init(void)
{
	return usb_register(&ncm_driver);
}

module_init(ncm_init);

static void __exit ncm_exit(void)
{
	usb_deregister(&ncm_driver);
}

module_exit(ncm_exit);

MODULE_AUTHOR("Steve Lin");
MODULE_DESCRIPTION("USB NCM devices");
MODULE_LICENSE("GPL");
