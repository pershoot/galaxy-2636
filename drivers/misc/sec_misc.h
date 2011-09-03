#ifndef SEC_MISC_H
#define SEC_MISC_H

#define klogi(fmt, arg...)  printk(KERN_INFO "%s: " fmt "\n" , __func__, ## arg)
#define kloge(fmt, arg...)  printk(KERN_ERR "%s(%d): " fmt "\n" , __func__, __LINE__, ## arg)

/*
 * touch related
 */

#define TOUCH_BOOTLOADER_ADDR (0x26 << 1)

char touch_firmware[] = {
	#include "../input/touchscreen/mxt1386_fw_ver09.h"
};

/* Bootloader states */
#define WAITING_BOOTLOAD_COMMAND   0xC0
#define WAITING_FRAME_DATA	0x80
#define FRAME_CRC_CHECK	0x02
#define FRAME_CRC_PASS	0x04
#define FRAME_CRC_FAIL	0x03
#define APP_CRC_FAIL	0x40
#define BOOTLOAD_STATUS_MASK	0x3f  // 0011 1111

#define MXT_MAX_FRAME_SIZE	532/*276*/

enum charger_type {
	CHARGER_BATTERY = 0,
	CHARGER_USB,
	CHARGER_AC,
	CHARGER_DISCHARGE
};

#endif  //SEC_MISC_H

