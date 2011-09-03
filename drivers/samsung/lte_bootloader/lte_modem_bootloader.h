#ifndef __LTE_MODEM_BOOTLOADER_H
#define __LTE_MODEM_BOOTLOADER_H

#define LTE_MODEM_BOOTLOADER_DRIVER_NAME	"lte_modem_bootloader"

#define IOCTL_LTE_MODEM_XMIT_BOOT		_IOW('o', 0x23, unsigned int)
#define IOCTL_LTE_MODEM_LTE2AP_STATUS	_IOR('o', 0x24, unsigned int)

#define AIRPLAIN_MODE_TEST
#define LTE_SILENT_RESET_TEST

#ifdef AIRPLAIN_MODE_TEST
#define IOCTL_LTE_MODEM_AIRPLAIN_ON	_IOWR('o', 0x25, unsigned int)
#define IOCTL_LTE_MODEM_AIRPLAIN_OFF	_IOWR('o', 0x26, unsigned int)
#endif

#ifdef LTE_SILENT_RESET_TEST
#define IOCTL_LTE_SILENT_RESET_ON	_IOWR('o', 0x27, unsigned int)
#define IOCTL_LTE_SILENT_RESET_OFF	_IOWR('o', 0x28, unsigned int)
#endif

struct lte_modem_bootloader_param {
	char __user *buf;
	int len;
};

struct lte_modem_bootloader_platform_data {
	const char *name;

	unsigned gpio_lte2ap_status;
};
#endif /* LTE_MODEM_BOOTLOADER_H */
