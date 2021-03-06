/*
 * arch/arm/mach-tegra/include/mach/gpio-p4wifi.h
 *
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __GPIO_P4WIFI_H
#define __GPIO_P4WIFI_H

#include "../gpio-names.h"

/* 
 * standard: Main Rev0.3 
 * Note: If pin is not matched with Rev0.3, please use TEGRA_GPIO_PXX with system_rev variable.
 */
#define GPIO_USB_SEL2		TEGRA_GPIO_PO1
#define GPIO_MAG_I2C_SCL	TEGRA_GPIO_PO2
#define GPIO_CP_ON		TEGRA_GPIO_PO3
#define GPIO_MAG_I2C_SDA	TEGRA_GPIO_PO4
#define GPIO_DOCK_INT		TEGRA_GPIO_PO5
#define GPIO_IFCONSENSE		TEGRA_GPIO_PO6
#define GPIO_FUEL_I2C_SCL	TEGRA_GPIO_PO7
#define GPIO_FUEL_I2C_SDA	TEGRA_GPIO_PO0
#define GPIO_LIGHT_I2C_SCL	TEGRA_GPIO_PY0
#define GPIO_THERMAL_I2C_SCL	TEGRA_GPIO_PY1
#define GPIO_LIGHT_I2C_SDA	TEGRA_GPIO_PY2
#define GPIO_THERMAL_I2C_SDA	TEGRA_GPIO_PY3
#define GPIO_HSIC_EN		TEGRA_GPIO_PV0
#define GPIO_PDA_ACTIVE		TEGRA_GPIO_PV1
#define GPIO_REMOTE_SENSE_IRQ	TEGRA_GPIO_PV2
#define GPIO_EAR_SEND_END	TEGRA_GPIO_PV3
#define GPIO_LVDS_N_SHDN	TEGRA_GPIO_PC1
#define GPIO_IMAGE_I2C_SCL	TEGRA_GPIO_PC6
#define GPIO_GPS_PWR_EN		TEGRA_GPIO_PZ2
#define GPIO_GPS_N_RST		TEGRA_GPIO_PN5
#define GPIO_IMA_N_RST		TEGRA_GPIO_PN4
#define GPIO_IMA_BYPASS		TEGRA_GPIO_PZ4
#define GPIO_IMA_SLEEP		TEGRA_GPIO_PW0
#define GPIO_GPS_CNTL		TEGRA_GPIO_PV7
#define GPIO_IMAGE_I2C_SDA	TEGRA_GPIO_PZ3
#define GPIO_BT_nRST		TEGRA_GPIO_PW1
#define GPIO_HDMI_HPD		TEGRA_GPIO_PN7
#define GPIO_CAM_F_nRST		TEGRA_GPIO_PT4
#define GPIO_CAM_F_nSTBY	TEGRA_GPIO_PD5
#define GPIO_CAM_PMIC_EN1	TEGRA_GPIO_PT2
#define GPIO_CAM_PMIC_EN2	TEGRA_GPIO_PT3
#define GPIO_CAM_R_nRST		TEGRA_GPIO_PD2
#define GPIO_LIGHT_SENSOR_DVI	TEGRA_GPIO_PA0
#define GPIO_BT_EN		TEGRA_GPIO_PJ5
#define GPIO_HW_REV2		TEGRA_GPIO_PU4
#define GPIO_USB_SEL1		TEGRA_GPIO_PB2  /* P3_Rev07: TEGRA_GPIO_PU5 */
#define GPIO_nTHRM_IRQ		TEGRA_GPIO_PU6
#define GPIO_CAM_PMIC_EN3	TEGRA_GPIO_PBB1
#define GPIO_BL_RESET		TEGRA_GPIO_PR3
#define GPIO_IPC_SLAVE_WAKEUP	TEGRA_GPIO_PR4
#define GPIO_HDMI_EN1		TEGRA_GPIO_PR5
#define GPIO_MOTOR_EN2		TEGRA_GPIO_PR6
#define GPIO_OTG_EN		TEGRA_GPIO_PR7
#define GPIO_WLAN_HOST_WAKE	TEGRA_GPIO_PS0
#define GPIO_BT_WAKE		TEGRA_GPIO_PS1
#define GPIO_BT_HOST_WAKE	TEGRA_GPIO_PS2
#define GPIO_EXT_WAKEUP		TEGRA_GPIO_PS4
#define GPIO_PHONE_ACTIVE	TEGRA_GPIO_PS5
#define GPIO_CAM_R_nSTBY	TEGRA_GPIO_PS6
#define GPIO_CAM_FLASH_SET	TEGRA_GPIO_PS7  /* P3_Rev07: N/A */
#define GPIO_IPC_HOST_WAKEUP	TEGRA_GPIO_PQ6
#define GPIO_WLAN_EN		TEGRA_GPIO_PQ2
#define GPIO_RESET_REQ_N	TEGRA_GPIO_PQ3
#define GPIO_IMA_PWREN		TEGRA_GPIO_PQ4
#define GPIO_HSIC_ACTIVE_STATE	TEGRA_GPIO_PQ5
#define GPIO_HSIC_SUS_REQ	TEGRA_GPIO_PQ0
#define GPIO_FUEL_ALRT		TEGRA_GPIO_PQ7
#define GPIO_TA_nCHG		TEGRA_GPIO_PK5
#define GPIO_LCD_LDO_EN		TEGRA_GPIO_PX0
#define GPIO_CP_RST		TEGRA_GPIO_PX1
#define GPIO_CAM_PMIC_EN4	TEGRA_GPIO_PBB4
#define GPIO_CAM_FLASH_EN	TEGRA_GPIO_PBB5
#define GPIO_TA_EN		TEGRA_GPIO_PX4
#define GPIO_CODEC_LDO_EN	TEGRA_GPIO_PX5
#define GPIO_MICBIAS_EN		TEGRA_GPIO_PX6
#define GPIO_UART_SEL		TEGRA_GPIO_PX7
#define GPIO_CAM_PMIC_EN5	TEGRA_GPIO_PR2
#define GPIO_DET_3_5		TEGRA_GPIO_PW3
#define GPIO_ADC_I2C_SCL	TEGRA_GPIO_PI7  /* P3_Rev07: TEGRA_GPIO_PA6 */
#define GPIO_ADC_I2C_SDA	TEGRA_GPIO_PG2  /* P3_Rev07: TEGRA_GPIO_PA7 */
#define GPIO_HDMI_I2C_SCL	TEGRA_GPIO_PH2  /* P3_Rev07: TEGRA_GPIO_PB7 */
#define GPIO_TA_nCONNECTED  	TEGRA_GPIO_PW2  /* P3_Rev07: TEGRA_GPIO_PB6 */
#define GPIO_HDMI_I2C_SDA	TEGRA_GPIO_PH3  /* P3_Rev07: TEGRA_GPIO_PB5 */
#define GPIO_V_ACCESSORY_5V	TEGRA_GPIO_PD1  /* P3_Rev07: TEGRA_GPIO_PB4 */
#define GPIO_LCD_EN		TEGRA_GPIO_PD0
#define GPIO_TOUCH_RST		TEGRA_GPIO_PD3
#define GPIO_TOUCH_INT		TEGRA_GPIO_PD4
#define GPIO_CURR_ADJ		TEGRA_GPIO_PV4
#define GPIO_ADC_INT		TEGRA_GPIO_PV5
#define GPIO_MPU_INT		TEGRA_GPIO_PV6
#define GPIO_ACCESSORY_INT	TEGRA_GPIO_PI5
#define GPIO_HDMI_LOGIC_I2C_SCL	TEGRA_GPIO_PJ0
#define GPIO_TOUCH_EN		TEGRA_GPIO_PJ2
#define GPIO_HDMI_LOGIC_I2C_SDA	TEGRA_GPIO_PK3
#define GPIO_AK8975_INT		TEGRA_GPIO_PK4
#define GPIO_MOTOR_I2C_SCL	TEGRA_GPIO_PK2
#define GPIO_MOTOR_I2C_SDA	TEGRA_GPIO_PI3
#define GPIO_ACCESSORY_EN	TEGRA_GPIO_PI6
#define GPIO_HW_REV3		TEGRA_GPIO_PG0
#define GPIO_HW_REV4		TEGRA_GPIO_PG1
#define GPIO_CODEC_I2C_SDA	TEGRA_GPIO_PG3
#define GPIO_MHL_INT		TEGRA_GPIO_PH0
#define GPIO_MHL_RST		TEGRA_GPIO_PH1
#define GPIO_EAR_MICBIAS_EN	TEGRA_GPIO_PS3
#define GPIO_IPC_TXD		TEGRA_GPIO_PJ7
#define GPIO_IPC_RXD		TEGRA_GPIO_PB0
#define GPIO_HW_REV0		TEGRA_GPIO_PB1
#define GPIO_HW_REV1		TEGRA_GPIO_PK7
#define GPIO_CODEC_I2C_SCL	TEGRA_GPIO_PI0
#define GPIO_SIM_DETECT		TEGRA_GPIO_PC7

#define GPIO_GPS_UART_TXD	TEGRA_GPIO_PU0
#define GPIO_GPS_UART_RXD	TEGRA_GPIO_PU1
#define GPIO_GPS_UART_CTS	TEGRA_GPIO_PU2
#define GPIO_GPS_UART_RTS	TEGRA_GPIO_PU3

/* CONFIG_TDMB P4 KOR SKT / P4 KOR KT / P4 KOR WIFI */
#define GPIO_TDMB_EN            TEGRA_GPIO_PU5
#define GPIO_TDMB_RST           TEGRA_GPIO_PA7
#define GPIO_TDMB_INT           TEGRA_GPIO_PB4
#define GPIO_TDMB_SPI_CS        TEGRA_GPIO_PB5
#define GPIO_TDMB_SPI_CLK       TEGRA_GPIO_PA6
#define GPIO_TDMB_SPI_MOSI      TEGRA_GPIO_PB6
#define GPIO_TDMB_SPI_MISO      TEGRA_GPIO_PB7

/* Warning: deprecated definitions. These should be deleted later */
#if 1  
#define GPIO_MLCD_ON		TEGRA_GPIO_PD0
#define GPIO_MLCD_ON1		TEGRA_GPIO_PX0
#define GPIO_MOTOR_EN1		TEGRA_GPIO_PG2  /* warning: not used anymore*/
#endif

typedef enum
{
	USB_SEL_AP_USB = 0,
	USB_SEL_CP_USB,
	USB_SEL_ADC
} usb_path_type;

#endif  /* __GPIO_P4WIFI_H */
