/*
 * leds-tps6586x.h - platform data structure for tps6586x driven LEDs.
 *
 * Copyright (C) 2010 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __LINUX_LEDS_TPS6586X_H
#define __LINUX_LEDS_TPS6586X_H

#include <linux/leds.h>

#define LED_TPS6586X_RED	0x01
#define LED_TPS6586X_GREEN	0x02
#define LED_TPS6586X_BLUE	0x04

enum led_tps6586x_isink {
	LED_TPS6586X_ISINK_NONE = 0,  /* Off */
	LED_TPS6586X_ISINK1,  /* 3.7mA */
	LED_TPS6586X_ISINK2,  /* 7.4mA */
	LED_TPS6586X_ISINK3,  /* 11.1mA */
	LED_TPS6586X_ISINK4,  /* 14.9mA */
	LED_TPS6586X_ISINK5,  /* 18.6mA */
	LED_TPS6586X_ISINK6,  /* 23.2mA */
	LED_TPS6586X_ISINK7,  /* 27.3mA */
};

struct led_tps6586x_pdata {
	char *name;                     /* LED name as expected by LED class */
	enum led_tps6586x_isink isink; /* initial brightness value */
	u8 color;
};

#endif /* __LINUX_LEDS_TPS6586X_H */
