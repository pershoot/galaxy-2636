#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/fcntl.h>

#include <linux/workqueue.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

#include <mach/gpio.h>
#include <mach/gpio-p5.h>

#include <linux/io.h>

#include <linux/fs.h>
#include <linux/uaccess.h>

#include <linux/time.h>
#include <linux/timer.h>

#include <linux/vmalloc.h>

#include "tdmb.h"
#include "INC_INCLUDES.h"

static ST_SUBCH_INFO *g_pStChInfo;
static bool g_bOnAir;
static bool g_bPowerOn;
INC_UINT8  g_acStreamBuf[INC_INTERRUPT_SIZE+188];
struct tdmb_platform_data gpio_cfg;

#ifndef GPIO_LEVEL_HIGH
#define GPIO_LEVEL_HIGH 1
#endif
#ifndef GPIO_LEVEL_LOW
#define GPIO_LEVEL_LOW 0
#endif

static bool __control_irq(bool set)
{
	bool ret = true;
	int irq_ret;
	if (set) {
		set_irq_type(gpio_cfg.irq, IRQ_TYPE_EDGE_FALLING);
		irq_ret = request_irq(gpio_cfg.irq, tdmb_irq_handler, IRQF_DISABLED, TDMB_DEV_NAME, NULL);
		if (irq_ret < 0) {
			DPRINTK("request_irq failed !! \r\n");
			ret = false;
		}
	} else {
		free_irq(gpio_cfg.irq, NULL);
	}

	return ret;
}

static void __control_gpio(bool poweron)
{
	if (poweron) {
		gpio_cfg.gpio_on();
	} else {
		gpio_cfg.gpio_off();
	}
}

static bool __get_ensemble_info(EnsembleInfoType *ensembleInfo, unsigned long freqHz)
{
	int i;
	int j;
	int nSubChIdx = 0;
	int nCnt;
	const char *ensembleName = NULL;
	INC_CHANNEL_INFO *pINC_SubChInfo;

	DPRINTK("T3900_get_ensemble_info - freq(%ld)\n", freqHz);

	ensembleInfo->TotalSubChNumber = INTERFACE_GETDMB_CNT() + INTERFACE_GETDAB_CNT();
	DPRINTK("total subchannel number : %d\n", ensembleInfo->TotalSubChNumber);

	if (ensembleInfo->TotalSubChNumber > 0) {
		ensembleName = (char *)INTERFACE_GETENSEMBLE_LABEL(TDMB_I2C_ID80);
		DPRINTK("ensembleName : %s\n", ensembleName);

		if (ensembleName) {
			strncpy((char *)ensembleInfo->EnsembleLabelCharField, (char *)ensembleName, ENSEMBLE_LABEL_SIZE_MAX);
		}

		ensembleInfo->EnsembleFrequency = freqHz;

		for (i = 0; i < 2; i++) {
			nCnt = (i == 0) ? INTERFACE_GETDMB_CNT() : INTERFACE_GETDAB_CNT();

			for (j = 0; j < nCnt; j++, nSubChIdx++) {
				pINC_SubChInfo = (i == 0) ? INTERFACE_GETDB_DMB(j) : INTERFACE_GETDB_DAB(j);
				ensembleInfo->SubChInfo[nSubChIdx].SubChID		= pINC_SubChInfo->ucSubChID;
				ensembleInfo->SubChInfo[nSubChIdx].StartAddress = pINC_SubChInfo->uiStarAddr;
				ensembleInfo->SubChInfo[nSubChIdx].TMId 		= pINC_SubChInfo->uiTmID;
				ensembleInfo->SubChInfo[nSubChIdx].Type 		= pINC_SubChInfo->ucServiceType;
				ensembleInfo->SubChInfo[nSubChIdx].ServiceID	= pINC_SubChInfo->ulServiceID;
				memcpy(ensembleInfo->SubChInfo[nSubChIdx].ServiceLabel, pINC_SubChInfo->aucLabel, SERVICE_LABEL_SIZE_MAX);

				DPRINTK("%s(subchid:%d,TMID:%d)\n"
					, ensembleInfo->SubChInfo[nSubChIdx].ServiceLabel
					, ensembleInfo->SubChInfo[nSubChIdx].SubChID
					, ensembleInfo->SubChInfo[nSubChIdx].TMId);
			}
		}
		return true;
	} else {
		return false;
	}
}

static void T3900_power_off(void)
{
	DPRINTK("T3900_power_off\n");

	if (g_bPowerOn) {
		g_bOnAir = false;

		INC_STOP(TDMB_I2C_ID80);

		__control_irq(false);
		__control_gpio(false);

		vfree(g_pStChInfo);
		g_pStChInfo = NULL;

		g_bPowerOn = false;
	}
}

static bool T3900_power_on(void)
{
	DPRINTK("T3900_power_on\n");

	if (g_bPowerOn) {
		return true;
	} else {
		g_pStChInfo = vmalloc(sizeof(ST_SUBCH_INFO));
		if (g_pStChInfo == NULL) {
			return false;
		} else {
			__control_gpio(true);

			if (INTERFACE_INIT(TDMB_I2C_ID80) != INC_SUCCESS) {
				__control_gpio(false);

				vfree(g_pStChInfo);
				g_pStChInfo = NULL;

				return false;
			} else {
				__control_irq(true);
				g_bPowerOn = true;
				return true;
			}
		}
	}
}

static void T3900_get_dm(tdmb_dm *info)
{
	if (g_bPowerOn == true && g_bOnAir == true) {
		INC_STATUS_CHECK(TDMB_I2C_ID80);
		info->rssi = INC_GET_RSSI(TDMB_I2C_ID80);
		info->BER = INC_GET_SAMSUNG_BER(TDMB_I2C_ID80);
		info->antenna = INC_GET_SAMSUNG_ANT_LEVEL(TDMB_I2C_ID80);
		info->PER = 0;
	} else {
		info->rssi = 100;
		info->BER = 2000;
		info->PER = 0;
		info->antenna = 0;
	}
}

static bool T3900_set_ch(unsigned long freqHz, unsigned char subhcid, bool factory_test)
{
	INC_UINT8 reErr;
	bool ret = false;

	if (g_bPowerOn) {
		g_pStChInfo->nSetCnt = 1;
		g_pStChInfo->astSubChInfo[0].ulRFFreq = freqHz / 1000;
		g_pStChInfo->astSubChInfo[0].ucSubChID = subhcid;
		g_pStChInfo->astSubChInfo[0].ucServiceType = 0x0;
		if (g_pStChInfo->astSubChInfo[0].ucSubChID >= 64) {
			g_pStChInfo->astSubChInfo[0].ucSubChID -= 64;
			g_pStChInfo->astSubChInfo[0].ucServiceType = 0x18;
		}

		g_bOnAir = false;

		if (factory_test == false)
			reErr = INTERFACE_START(TDMB_I2C_ID80, g_pStChInfo);
		else
			reErr = INTERFACE_START_TEST(TDMB_I2C_ID80, g_pStChInfo);

		if (reErr == INC_SUCCESS) {
			/* TODO Ensemble  good code .... */
			g_bOnAir = true;
			ret = true;
		} else if (reErr == INC_RETRY) {
			DPRINTK("IOCTL_TDMB_ASSIGN_CH retry\n");

			T3900_power_off();
			T3900_power_on();

			if (factory_test == false)
				reErr = INTERFACE_START(TDMB_I2C_ID80, g_pStChInfo);
			else
				reErr = INTERFACE_START_TEST(TDMB_I2C_ID80, g_pStChInfo);

			if (reErr == INC_SUCCESS) {
				/* TODO Ensemble  good code .... */
				g_bOnAir = true;
				ret = true;
			}
		}
	}

	return ret;
}

static bool T3900_scan_ch(EnsembleInfoType *ensembleInfo, unsigned long freqHz)
{
	if (g_bPowerOn == false || ensembleInfo == NULL)
		return false;
	else if(INTERFACE_SCAN(TDMB_I2C_ID80, (freqHz / 1000)) == INC_SUCCESS)
		return __get_ensemble_info(ensembleInfo, freqHz);
	else
		return false;
}

static void T3900_pull_data(void)
{
	INC_UINT16 ulRemainLength = INC_INTERRUPT_SIZE;
#if !(INC_INTERRUPT_SIZE <= 0xFFF)
	INC_UINT16 unIndex = 0;
	INC_UINT16 unSPISize = 0xFFF;
#endif

	memset(g_acStreamBuf, 0, sizeof(g_acStreamBuf));

#if (INC_INTERRUPT_SIZE <= 0xFFF)
	INC_CMD_READ_BURST(TDMB_I2C_ID80, APB_STREAM_BASE, g_acStreamBuf, ulRemainLength);
#else
	while (ulRemainLength) {
		if (ulRemainLength >= unSPISize) {
			INC_CMD_READ_BURST(TDMB_I2C_ID80, APB_STREAM_BASE, &g_acStreamBuf[unIndex*unSPISize], unSPISize);
			unIndex++;
			ulRemainLength -= unSPISize;
		} else {
			INC_CMD_READ_BURST(TDMB_I2C_ID80, APB_STREAM_BASE, &g_acStreamBuf[unIndex*unSPISize], ulRemainLength);
			ulRemainLength = 0;
		}
	}
#endif

	tdmb_store_data(g_acStreamBuf, INC_INTERRUPT_SIZE);
}

static unsigned long T3900_int_size(void)
{
	return INC_INTERRUPT_SIZE;
}

static TDMBDrvFunc T3900DrvFunc = {
	.power_on = T3900_power_on,
	.power_off = T3900_power_off,
	.scan_ch = T3900_scan_ch,
	.get_dm = T3900_get_dm,
	.set_ch = T3900_set_ch,
	.pull_data = T3900_pull_data,
	.get_int_size = T3900_int_size,
};

TDMBDrvFunc * tdmb_get_drv_func(struct tdmb_platform_data * gpio)
{
	DPRINTK("tdmb_get_drv_func : T3900\n");
	memcpy(&gpio_cfg, gpio, sizeof(struct tdmb_platform_data));
	return &T3900DrvFunc;
}
