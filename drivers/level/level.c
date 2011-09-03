#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/kernel_sec_common.h>

#include <asm/uaccess.h>

#define LEVEL_DEV_NAME	"level"

static ssize_t show_control(struct device *d,
		struct device_attribute *attr, char *buf);
static ssize_t store_control(struct device *d,
		struct device_attribute *attr, const char *buf, size_t count);
		
static DEVICE_ATTR(control, S_IRUGO | S_IWUGO, show_control, store_control);

static struct attribute *levelctl_attributes[] = {
	&dev_attr_control.attr,
	NULL
};

static const struct attribute_group levelctl_group = {
	.attrs = levelctl_attributes,
};

static unsigned int convert_debug_level_str(const char *str) {
	if (strncmp(str, "0xA0A0", 6) == 0 || strncmp(str, "0xa0a0", 6) == 0)
		return KERNEL_SEC_DEBUG_LEVEL_LOW;

	if (strncmp(str, "0xB0B0", 6) == 0 || strncmp(str, "0xb0b0", 6) == 0)
		return KERNEL_SEC_DEBUG_LEVEL_MID;

	if (strncmp(str, "0xC0C0", 6) == 0 || strncmp(str, "0xc0c0", 6) == 0)
		return KERNEL_SEC_DEBUG_LEVEL_HIGH;

	return 0;
}

static void convert_debug_level_int(unsigned int val, char *str) {
	if (val == KERNEL_SEC_DEBUG_LEVEL_LOW) {
		strcpy(str, "0xA0A0");
		return;
	}

	if (val == KERNEL_SEC_DEBUG_LEVEL_MID) {
		strcpy(str, "0xB0B0");
		return;
	}

	if (val == KERNEL_SEC_DEBUG_LEVEL_HIGH) {
		strcpy(str, "0xC0C0");
		return;
	}
}


static ssize_t show_control(struct device *d,
		struct device_attribute *attr, char *buf)
{
	char buffer[7];
	convert_debug_level_int(kernel_sec_get_debug_level(), buffer);

	return sprintf(buf, "%s\n", buffer);
}

static ssize_t store_control(struct device *d,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int sec_debug_level = convert_debug_level_str(buf);

	if (sec_debug_level == 0)
		return -EINVAL;
	
	kernel_sec_set_debug_level(sec_debug_level); 

	return count;
}

static int level_open(struct inode *inode, struct file *filp)
{
	printk("level Device open\n");

	return 0;
}

static struct file_operations level_fops = 
{
	.owner = THIS_MODULE,
	.open = level_open,
};

static struct miscdevice level_device = {
	.minor  = MISC_DYNAMIC_MINOR,
	.name   = LEVEL_DEV_NAME,
	.fops   = &level_fops,
};

/* init & cleanup. */
static int __init level_init(void)
{
	int result;

	printk("level device init\n");

	result = misc_register(&level_device);

	if (result < 0) 
		goto init_exit;
		
	result = sysfs_create_group(&level_device.this_device->kobj, &levelctl_group);

	if (result < 0)
		goto init_exit;

	return 0;

init_exit:
	printk("failed to create sysfs files\n");
	return result;
}

static void __exit level_exit(void)
{
	printk("level device exit\n");
	misc_deregister(&level_device);
}

module_init(level_init);
module_exit(level_exit);

