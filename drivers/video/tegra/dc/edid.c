/*
 * drivers/video/tegra/dc/edid.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Erik Gilling <konkers@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define DEBUG

#include <linux/debugfs.h>
#include <linux/fb.h>
#include <linux/i2c.h>
#include <linux/seq_file.h>
#include <linux/vmalloc.h>

#include "edid.h"

#ifdef	CONFIG_MACH_SAMSUNG_HDMI_EDID_FORCE_PASS
u8 default_edid[] = { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x52, 0x62, 0x06, 0x02, 0x01, 0x01, 0x01, 0x01, 0xff, 0x12, 0x01, 0x03, 0x80, 0x69, 0x3b, 0x78, 0x0a, 0x0d, 0xc9, 0xa0, 0x57 ,0x47 ,0x98, 0x27, 0x12, 0x48, 0x4c, 0x2d, 0xce, 0x00, 0x81, 0x80, 0x8b, 0xc0, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x3a, 0x80, 0x18, 0x71, 0x38, 0x2d, 0x40, 0x58, 0x2c, 0x45, 0x00, 0xc4, 0x8e, 0x21, 0x00, 0x00, 0x1e, 0x8c, 0x0a, 0xd0, 0x8a, 0x20, 0xe0, 0x2d, 0x10, 0x10, 0x3e, 0x96, 0x00, 0xc4, 0x8e, 0x21, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x54, 0x53, 0x42, 0x2d, 0x54, 0x56, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xfd, 0x00, 0x17, 0x4c, 0x0f, 0x44, 0x0f, 0x00, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x01, 0xcc, 0x02, 0x03, 0x20, 0x71, 0x4a, 0x90, 0x05, 0x04, 0x03, 0x07, 0x02, 0x06, 0x01, 0x20, 0x22, 0x23, 0x09, 0x07, 0x07, 0x6c, 0x03, 0x0c, 0x00, 0x10, 0x00, 0x00, 0x2d, 0xc0, 0x22, 0x22, 0x2b, 0x2b, 0x01, 0x1d, 0x80, 0x18, 0x71, 0x1c, 0x16, 0x20, 0x58, 0x2c, 0x25, 0x00, 0xc4, 0x8e, 0x21, 0x00, 0x00, 0x9e, 0x01, 0x1d, 0x00, 0x72, 0x51, 0xd0, 0x1e, 0x20, 0x6e, 0x28, 0x55, 0x00, 0xc4, 0x8e, 0x21, 0x00, 0x00, 0x1e, 0x8c, 0x0a, 0xd0, 0x8a, 0x20, 0xe0, 0x2d, 0x10, 0x10, 0x3e, 0x96, 0x00, 0x13, 0x8e, 0x21, 0x00, 0x00, 0x18, 0x8c, 0x0a, 0xa0, 0x14, 0x51, 0xf0, 0x16, 0x00, 0x26, 0x7c, 0x43, 0x00, 0x13, 0x8e, 0x21, 0x00, 0x00, 0x98, 0x8c, 0x0a, 0xa0, 0x14, 0x51, 0xf0, 0x16, 0x00, 0x26, 0x7c, 0x43, 0x00, 0xc4, 0x8e, 0x21, 0x00, 0x00, 0x98, 0x00, 0x00, 0x00, 0x00, 0x00, 0xec };

enum {
	HDMI_EDID_CHECK,
	HDMI_EDID_FORCE_PASS,
};
static int	b_edid_force_pass = HDMI_EDID_CHECK;

void set_edid_force_pass(int set)
{
	printk("[HDMI] %s() set = %d\n", __func__, set);
	b_edid_force_pass = set;
}
EXPORT_SYMBOL(set_edid_force_pass);

int get_edid_force_pass()
{
	return b_edid_force_pass;
}
EXPORT_SYMBOL(get_edid_force_pass);
#endif


struct tegra_edid {
	struct i2c_client	*client;
	struct i2c_board_info	info;
	int			bus;

	u8			*data;
	unsigned		len;
	u8			support_stereo;
};

#if defined(DEBUG) || defined(CONFIG_DEBUG_FS)
static int tegra_edid_show(struct seq_file *s, void *unused)
{
	struct tegra_edid *edid = s->private;
	int i;

	for (i = 0; i < edid->len; i++) {
		if (i % 16 == 0)
			seq_printf(s, "edid[%03x] =", i);

		seq_printf(s, " %02x", edid->data[i]);

		if (i % 16 == 15)
			seq_printf(s, "\n");
	}

	return 0;
}
#endif

#ifdef CONFIG_DEBUG_FS
static int tegra_edid_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, tegra_edid_show, inode->i_private);
}

static const struct file_operations tegra_edid_debug_fops = {
	.open		= tegra_edid_debug_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void tegra_edid_debug_add(struct tegra_edid *edid)
{
	char name[] = "edidX";

	snprintf(name, sizeof(name), "edid%1d", edid->bus);
	debugfs_create_file(name, S_IRUGO, NULL, edid, &tegra_edid_debug_fops);
}
#else
void tegra_edid_debug_add(struct tegra_edid *edid)
{
}
#endif

#ifdef DEBUG
static char tegra_edid_dump_buff[16 * 1024];

static void tegra_edid_dump(struct tegra_edid *edid)
{
	struct seq_file s;
	int i;
	char c;

	memset(&s, 0x0, sizeof(s));

	s.buf = tegra_edid_dump_buff;
	s.size = sizeof(tegra_edid_dump_buff);
	s.private = edid;

	tegra_edid_show(&s, NULL);

	i = 0;
	while (i < s.count ) {
		if ((s.count - i) > 256) {
			c = s.buf[i + 256];
			s.buf[i + 256] = 0;
			printk("%s", s.buf + i);
			s.buf[i + 256] = c;
		} else {
			printk("%s", s.buf + i);
		}
		i += 256;
	}
}
#else
static void tegra_edid_dump(struct tegra_edid *edid)
{
}
#endif

int tegra_edid_read_block(struct tegra_edid *edid, int block, u8 *data)
{
	u8 block_buf[] = {block >> 1};
	u8 cmd_buf[] = {(block & 0x1) * 128};
	int status;
	struct i2c_msg msg[] = {
		{
			.addr = 0x30,
			.flags = 0,
			.len = 1,
			.buf = block_buf,
		},
		{
			.addr = 0x50,
			.flags = 0,
			.len = 1,
			.buf = cmd_buf,
		},
		{
			.addr = 0x50,
			.flags = I2C_M_RD,
			.len = 128,
			.buf = data,
		}};
	struct i2c_msg *m;
	int msg_len;

	if (block > 1) {
		msg_len = 3;
		m = msg;
	} else {
		msg_len = 2;
		m = &msg[1];
	}

	status = i2c_transfer(edid->client->adapter, m, msg_len);

	if (status < 0)
		return status;

	if (status != msg_len)
		return -EIO;

	return 0;
}

int tegra_edid_parse_ext_block(u8 *raw, int idx, struct tegra_edid *edid)
{
	u8 *ptr;
	u8 tmp;
	u8 code;
	int len;

	ptr = &raw[4];

	while (ptr < &raw[idx]) {
		tmp = *ptr;
		len = tmp & 0x1f;

		/* HDMI Specification v1.4a, section 8.3.2:
		 * see Table 8-16 for HDMI VSDB format.
		 * data blocks have tags in top 3 bits:
		 * tag code 2: video data block
		 * tag code 3: vendor specific data block
		 */
		code = (tmp >> 5) & 0x3;
		switch (code) {
		/* case 2 is commented out for now */
		case 3:
		{
			int j = 0;

			if ((len >= 8) &&
				(ptr[1] == 0x03) &&
				(ptr[2] == 0x0c) &&
				(ptr[3] == 0)) {
				j = 8;
				tmp = ptr[j++];
				/* HDMI_Video_present? */
				if (tmp & 0x20) {
					/* Latency_Fields_present? */
					if (tmp & 0x80)
						j += 2;
					/* I_Latency_Fields_present? */
					if (tmp & 0x40)
						j += 2;
					/* 3D_present? */
					if (j <= len && (ptr[j] & 0x80))
						edid->support_stereo = 1;
				}
			}

			len++;
			ptr += len; /* adding the header */
			break;
		}
		default:
			len++; /* len does not include header */
			ptr += len;
			break;
		}
	}

	return 0;
}

int tegra_edid_mode_support_stereo(struct fb_videomode *mode)
{
	if (!mode)
		return 0;

	if (mode->xres == 1280 && mode->yres == 720 && mode->refresh == 60)
		return 1;

	if (mode->xres == 1280 && mode->yres == 720 && mode->refresh == 50)
		return 1;

	return 0;
}

#ifdef	CONFIG_MACH_SAMSUNG_HDMI_EDID_FORCE_PASS
int tegra_edid_get_monspecs_hdmi_checker(struct tegra_edid *edid, struct fb_monspecs *specs)
{
	int i;
	int j;
	int ret;
	int extension_blocks;
	int use_default = 0;

	printk(KERN_INFO "[HDMI] %s()\n", __func__);
	
	edid->support_stereo = 0;

	ret = tegra_edid_read_block(edid, 0, edid->data);
	if (ret)
		printk(KERN_WARNING "[HDMI][WARNING] edid read block fail but factory mode! \n");

	memset(specs, 0x0, sizeof(struct fb_monspecs));
	fb_edid_to_monspecs(edid->data, specs);	
	if (specs->modedb == NULL)
	{
		printk(KERN_WARNING "[HDMI][WARNING] write default edid!\n");
		for(i = 1; i <= ARRAY_SIZE(default_edid); i++)
			edid->data[i] = default_edid[i];

		fb_edid_to_monspecs(edid->data, specs);
		use_default = 1;
	}

	extension_blocks = edid->data[0x7e];

	for (i = 1; i <= extension_blocks; i++) {
		if (!use_default) {
			ret = tegra_edid_read_block(edid, i, edid->data + i * 128);
			if (ret < 0)
				break;
		}

		if (edid->data[i * 128] == 0x2) {
			fb_edid_add_monspecs(edid->data + i * 128, specs);

			tegra_edid_parse_ext_block(edid->data + i * 128,
					edid->data[i * 128 + 2], edid);

			if (edid->support_stereo) {
				for (j = 0; j < specs->modedb_len; j++) {
					if (tegra_edid_mode_support_stereo(
						&specs->modedb[j]))
						specs->modedb[j].vmode |=
						FB_VMODE_STEREO_FRAME_PACK;
				}
			}
		}
	}

	edid->len = i * 128;

	tegra_edid_dump(edid);

	return 0;
}
EXPORT_SYMBOL(tegra_edid_get_monspecs_hdmi_checker);
#endif


int tegra_edid_get_monspecs(struct tegra_edid *edid, struct fb_monspecs *specs)
{
	int i;
	int j;
	int ret;
	int extension_blocks;

	printk(KERN_INFO "[HDMI] %s()\n", __func__);
	
	edid->support_stereo = 0;

	ret = tegra_edid_read_block(edid, 0, edid->data);
	if (ret) {
		printk(KERN_ERR "[HDMI][ERROR] edid read first block fail\n");
		return ret;
	}

	memset(specs, 0x0, sizeof(struct fb_monspecs));
	fb_edid_to_monspecs(edid->data, specs);
	if (specs->modedb == NULL) {
		printk(KERN_ERR "[HDMI][ERROR] modedb is NULL\n");
		return -EINVAL;
	}

	extension_blocks = edid->data[0x7e];
	printk(KERN_INFO "[HDMI] %s() EXTENSIION_BLOCKS = %d\n", __func__, extension_blocks);

	for (i = 1; i <= extension_blocks; i++) {
		ret = tegra_edid_read_block(edid, i, edid->data + i * 128);
		if (ret < 0) {
			printk(KERN_ERR "[HDMI][ERROR] edid read 2nd block fail i=%d\n", i);
			return ret;
		}

		if (edid->data[i * 128] == 0x2) {
			fb_edid_add_monspecs(edid->data + i * 128, specs);

			tegra_edid_parse_ext_block(edid->data + i * 128,
					edid->data[i * 128 + 2], edid);

			if (edid->support_stereo) {
				for (j = 0; j < specs->modedb_len; j++) {
					if (tegra_edid_mode_support_stereo(
						&specs->modedb[j]))
						specs->modedb[j].vmode |=
						FB_VMODE_STEREO_FRAME_PACK;
				}
			}
		}
	}

	edid->len = i * 128;

	tegra_edid_dump(edid);

	return 0;
}

struct tegra_edid *tegra_edid_create(int bus)
{
	struct tegra_edid *edid;
	struct i2c_adapter *adapter;
	int err;

	edid = kzalloc(sizeof(struct tegra_edid), GFP_KERNEL);
	if (!edid)
		return ERR_PTR(-ENOMEM);

	edid->data = vmalloc(SZ_32K);
	if (!edid->data) {
		err = -ENOMEM;
		goto free_edid;
	}
	strlcpy(edid->info.type, "tegra_edid", sizeof(edid->info.type));
	edid->bus = bus;
	edid->info.addr = 0x50;
	edid->info.platform_data = edid;

	adapter = i2c_get_adapter(bus);
	if (!adapter) {
		pr_err("can't get adpater for bus %d\n", bus);
		err = -EBUSY;
		goto free_edid;
	}

	edid->client = i2c_new_device(adapter, &edid->info);
	i2c_put_adapter(adapter);

	if (!edid->client) {
		pr_err("can't create new device\n");
		err = -EBUSY;
		goto free_edid;
	}

	tegra_edid_debug_add(edid);

	return edid;

free_edid:
	vfree(edid->data);
	kfree(edid);

	return ERR_PTR(err);
}

void tegra_edid_destroy(struct tegra_edid *edid)
{
	i2c_release_client(edid->client);
	vfree(edid->data);
	kfree(edid);
}

static const struct i2c_device_id tegra_edid_id[] = {
        { "tegra_edid", 0 },
        { }
};

MODULE_DEVICE_TABLE(i2c, tegra_edid_id);

static struct i2c_driver tegra_edid_driver = {
        .id_table = tegra_edid_id,
        .driver = {
                .name = "tegra_edid",
        },
};

static int __init tegra_edid_init(void)
{
        return i2c_add_driver(&tegra_edid_driver);
}

static void __exit tegra_edid_exit(void)
{
        i2c_del_driver(&tegra_edid_driver);
}

module_init(tegra_edid_init);
module_exit(tegra_edid_exit);
