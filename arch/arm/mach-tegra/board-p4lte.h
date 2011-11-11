/*
 * arch/arm/mach-tegra/board-p4.h
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

#ifndef _MACH_TEGRA_BOARD_P4_H
#define _MACH_TEGRA_BOARD_P4_H

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

/* soonyong.cho : Define samsung product id and config string.
 *                Sources such as 'android.c' and 'devs.c' refered below define
 */
#ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
#  define SAMSUNG_VENDOR_ID		0x04e8

#  ifdef CONFIG_USB_ANDROID_SAMSUNG_ESCAPE
	/* USE DEVGURU HOST DRIVER */
	/* 0x6860 : MTP(0) + MS Composite (UMS) */
	/* 0x685E : UMS(0) + MS Composite (ADB) */
#  ifdef CONFIG_USB_ANDROID_SAMSUNG_KIES_UMS
#    define SAMSUNG_KIES_PRODUCT_ID	0x685e	/* ums(0) + acm(1,2) */
#  else
#    define SAMSUNG_KIES_PRODUCT_ID	0x6860	/* mtp(0) + acm(1,2) */
#    define SAMSUNG_P4LTE_KIES_PRODUCT_ID	 0x6860	/* mtp(0) + diag */
#  endif
#    define SAMSUNG_DEBUG_PRODUCT_ID 0x6860	/* ums(0) + acm(1,2) + adb(3) (with MS Composite) */
#    define SAMSUNG_P4LTE_DEBUG_PRODUCT_ID 0x6860 /* diag(1,2) + adb(3) (with MS Composite) */
#    define SAMSUNG_UMS_PRODUCT_ID	0x685B  /* UMS Only */
#    define SAMSUNG_MTP_PRODUCT_ID	0x685C  /* MTP Only */
#    ifdef CONFIG_USB_ANDROID_SAMSUNG_RNDIS_WITH_MS_COMPOSITE
#      define SAMSUNG_RNDIS_PRODUCT_ID	0x6861  /* RNDIS(0,1) + UMS (2) + MS Composite */
#    else
#      define SAMSUNG_RNDIS_PRODUCT_ID	0x6863  /* RNDIS only */
#      define SAMSUNG_P4LTE_RNDIS_DIAG_PRODUCT_ID	0x6862  /* RNDIS + DIAG */
#    endif
#    define ANDROID_DEBUG_CONFIG_STRING	 "MTP + ACM + ADB (Debugging mode)"
#    define ANDROID_P4LTE_DEBUG_CONFIG_STRING	 "MTP + DIAG + ADB (Debugging mode)"
#  ifdef CONFIG_USB_ANDROID_SAMSUNG_KIES_UMS
#    define ANDROID_KIES_CONFIG_STRING	 "UMS + ACM (SAMSUNG KIES mode)"
#  else
#    define ANDROID_KIES_CONFIG_STRING	 "MTP + ACM (SAMSUNG KIES mode)"
#    define ANDROID_P4LTE_KIES_CONFIG_STRING	 "MTP + DIAG"
#  endif
#  else /* USE MCCI HOST DRIVER */
#    define SAMSUNG_KIES_PRODUCT_ID	0x6877	/* Shrewbury ACM+MTP*/
#    define SAMSUNG_DEBUG_PRODUCT_ID	0x681C	/* Shrewbury ACM+UMS+ADB*/
#    define SAMSUNG_UMS_PRODUCT_ID	0x681D
#    define SAMSUNG_MTP_PRODUCT_ID	0x68A9
#    define SAMSUNG_RNDIS_PRODUCT_ID	0x6863
#    define ANDROID_DEBUG_CONFIG_STRING	 "ACM + UMS + ADB (Debugging mode)"
#    define ANDROID_KIES_CONFIG_STRING	 "ACM + MTP (SAMSUNG KIES mode)"
#  endif
#  define       ANDROID_UMS_CONFIG_STRING	 "UMS Only (Not debugging mode)"
#  define       ANDROID_MTP_CONFIG_STRING	 "MTP Only (Not debugging mode)"
#ifdef CONFIG_USB_ANDROID_ACCESSORY
#    define ANDROID_ACCESSORY_CONFIG_STRING		"ACCESSORY Only(ADK mode)"
#    define ANDROID_ACCESSORY_ADB_CONFIG_STRING	"ACCESSORY _ADB (ADK + ADB mode)"
#endif
#  ifdef CONFIG_USB_ANDROID_SAMSUNG_RNDIS_WITH_MS_COMPOSITE
#    define       ANDROID_RNDIS_CONFIG_STRING	 "RNDIS + UMS (Not debugging mode)"
#  else
#    define       ANDROID_RNDIS_CONFIG_STRING	 "RNDIS Only (Not debugging mode)"
#    define       ANDROID_P4LTE_RNDIS_DIAG_CONFIG_STRING	 "RNDIS + DIAG (Not debugging mode)"
#  endif
	/* Refered from S1, P1 */
#  define USBSTATUS_UMS				0x0
#  define USBSTATUS_SAMSUNG_KIES 		0x1
#  define USBSTATUS_MTPONLY			0x2
#  define USBSTATUS_ASKON			0x4
#  define USBSTATUS_VTP				0x8
#  define USBSTATUS_ADB				0x10
#  define USBSTATUS_DM				0x20
#  define USBSTATUS_ACM				0x40
#  define USBSTATUS_SAMSUNG_KIES_REAL		0x80
#ifdef CONFIG_USB_ANDROID_ACCESSORY
#  define USBSTATUS_ACCESSORY			0x100
#endif

/* soonyong.cho : This is for setting unique serial number */
//void __init s3c_usb_set_serial(void);
#endif /* CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE */


#endif
