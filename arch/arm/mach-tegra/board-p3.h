/*
 * arch/arm/mach-tegra/board-p3.h
 *
 * Copyright (C) 2010 Google, Inc.
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

#ifndef _MACH_TEGRA_BOARD_P3_H
#define _MACH_TEGRA_BOARD_P3_H

int p3_regulator_init(void);
int p3_sdhci_init(void);
int p3_pinmux_init(void);
int p3_panel_init(void);
int p3_rfkill_init(void);
int p3_gpio_i2c_init(void);
int p3_sensors_init(void);
int p3_emc_init(void);
int p3_bt_lpm_init(void);
void p3_bt_uart_wake_peer(struct uart_port *);

/* MJF: This is the wrong place for the TPS defines, but that's what
 * Nvidia did for Ventana, so, to stick close, we'll do the same for now.
 */

/* TPS6586X gpios */
#define TPS6586X_GPIO_BASE	TEGRA_NR_GPIOS
#define AVDD_DSI_CSI_ENB_GPIO	(TPS6586X_GPIO_BASE + 1) /* gpio2 */

/* Interrupt numbers from external peripherals */
#define TPS6586X_INT_BASE	TEGRA_NR_IRQS
#define TPS6586X_INT_END	(TPS6586X_INT_BASE + 32)

#endif
