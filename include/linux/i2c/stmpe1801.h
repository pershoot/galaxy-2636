/******************** (C) COPYRIGHT 2010 STMicroelectronics ********************
*
* File Name		: stmpe1801.h
* Authors		: Sensor & MicroActuators BU - Application Team
*			    : Bela Somaiah 
* Version		: V 1.1 
* Date			: 07/04/2011
* Description	: STMPE1801 GPIO header file 
*
********************************************************************************
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* THE PRESENT SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES
* OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, FOR THE SOLE
* PURPOSE TO SUPPORT YOUR APPLICATION DEVELOPMENT.
* AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY DIRECT,
* INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING FROM THE
* CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE BY CUSTOMERS OF THE CODING
* INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
*
* THIS SOFTWARE IS SPECIFICALLY DESIGNED FOR EXCLUSIVE USE WITH ST PARTS.
*
********************************************************************************
* REVISON HISTORY
*
* VERSION | DATE 	| AUTHORS	     | DESCRIPTION
* 1.1	  | 07/04/2011 | Bela Somaiah    | Second Release Polling and Interrupt 
* 1.0	  | 05/04/2011 | Bela Somaiah    | First Release
*
*******************************************************************************/
#ifndef __LINUX_INPUT_STMPE1801_H
#define __LINUX_INPUT_STMPE1801_H

#include <linux/device.h>

struct generic_gpio_platform_data {
        unsigned gpio_start;            /* GPIO Chip base # */
        unsigned pullup_dis_mask;       /* Pull-Up Disable Mask */
        int     (*setup)(struct i2c_client *client,
                                int gpio, unsigned ngpio,
                                void *context);
        int     (*teardown)(struct i2c_client *client,
                                int gpio, unsigned ngpio,
                                void *context);
        void    *context;
	int		irq_base;
};

/**
 * struct stmpe_keypad_platform_data - STMPE keypad platform data
 * @keymap_data: key map table and size
 * @debounce_ms: debounce interval, in ms.  Maximum is
 *               %STMPE_KEYPAD_MAX_DEBOUNCE.
 * @scan_count: number of key scanning cycles to confirm key data.
 *              Maximum is %STMPE_KEYPAD_MAX_SCAN_COUNT.
 * @no_autorepeat: disable key autorepeat
 */
struct stmpe_keypad_platform_data {
        struct matrix_keymap_data *keymap_data;
        unsigned int debounce_ms;
        unsigned int scan_count;
        bool no_autorepeat;
};

/* remove the compile warning */
#if !defined CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
/**
 * struct stmpe_gpio_platform_data - STMPE GPIO platform data
 * @gpio_base: first gpio number assigned.  A maximum of
 *             %STMPE_NR_GPIOS GPIOs will be allocated.
 */
struct stmpe_gpio_platform_data {
        int gpio_base;
        void (*setup)(struct stmpe *stmpe, unsigned gpio_base);
        void (*remove)(struct stmpe *stmpe, unsigned gpio_base);
};
#endif

#define STMPE_NR_INTERNAL_IRQS	5
#define STMPE_INT_GPIO(x)	(STMPE_NR_INTERNAL_IRQS + (x))


#endif
