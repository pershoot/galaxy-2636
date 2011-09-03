/*
 * Modem control driver
 *
 * Copyright (C) 2010 Samsung Electronics Co.Ltd
 * Author: Suchang Woo <suchang.woo@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __MODEM_CONTROL_H__
#define __MODEM_CONTROL_H__

#define MC_SUCCESS 0
#define MC_HOST_HIGH 1
#define MC_HOST_TIMEOUT 2

struct modemctl;
struct modemctl_ops {
	void (*modem_on)(struct modemctl *);
	void (*modem_off)(struct modemctl *);
	void (*modem_reset)(struct modemctl *);
	void (*modem_boot)(struct modemctl *);
	void (*modem_suspend)(struct modemctl *);
	void (*modem_resume)(struct modemctl *);
	void (*modem_cfg_gpio)(void);
};

struct modemctl_platform_data {
	const char *name;
	unsigned gpio_phone_on;
	unsigned gpio_phone_off;    
	unsigned gpio_cp_reset;
	unsigned gpio_slave_wakeup;
	unsigned gpio_host_wakeup;
	unsigned gpio_host_active;
	unsigned gpio_phone_active;
//	unsigned gpio_pda_active;    
	int wakeup;
	struct modemctl_ops ops;
};

struct modemctl {
	int irq[3];

	unsigned gpio_phone_on;
	unsigned gpio_phone_off;    
	unsigned gpio_cp_reset;
	unsigned gpio_slave_wakeup;
	unsigned gpio_host_wakeup;
	unsigned gpio_host_active;
	unsigned gpio_phone_active;    
    
	struct modemctl_ops *ops;
	struct regulator *vcc;

	struct device *dev;
	const struct attribute_group *group;

	struct delayed_work work;
	struct work_struct resume_work;
	int wakeup_flag; /*flag for CP boot GPIO sync flag*/
	int cpcrash_flag;
	struct completion *l2_done;
};

extern struct platform_device modemctl;

extern int usbsvn_request_resume(void);

int mc_is_modem_on(void);
int mc_is_host_wakeup(void);
int mc_is_modem_active(void);
//int mc_is_suspend_request(void); //temp_inchul
int mc_prepare_resume(int);
int  mc_reconnect_gpio(void);
void crash_event(int type);
/* WJ 0413 */
int mc_is_slave_wakeup(void);
void mc_phone_active_irq_enable(int on);
#endif /* __MODEM_CONTROL_H__ */
