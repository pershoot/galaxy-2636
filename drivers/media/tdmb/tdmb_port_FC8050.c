#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/fcntl.h>

#include <linux/workqueue.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include <linux/fs.h>
#include <linux/uaccess.h>

#include <linux/time.h>
#include <linux/timer.h>

#include <linux/vmalloc.h>

#include "tdmb.h"
#include "DMBDrv_wrap_FC8050.h"

static bool g_bOnAir;
static bool g_bPowerOn;
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

	DPRINTK("__get_ensemble_info - freq(%d)\n", freqHz);

	SubChInfoTypeDB *pFCI_SubChInfo;

	ensembleInfo->TotalSubChNumber = DMBDrv_GetDMBSubChCnt() + DMBDrv_GetDABSubChCnt();
	DPRINTK("total subchannel number : %d\n", ensembleInfo->TotalSubChNumber);

	if (ensembleInfo->TotalSubChNumber > 0) {
		ensembleName = (char *)DMBDrv_GetEnsembleLabel();
		DPRINTK("ensembleName : %s\n", ensembleName);

		if (ensembleName) {
			strncpy((char *)ensembleInfo->EnsembleLabelCharField, (char *)ensembleName, ENSEMBLE_LABEL_SIZE_MAX);
		}

		ensembleInfo->EnsembleFrequency = freqHz;

		DPRINTK("UpdateEnsembleInfo - ensembleName(%s)\n", ensembleName);

		for (i = 0; i < 2; i++) {
			nCnt = (i == 0) ? DMBDrv_GetDMBSubChCnt() : DMBDrv_GetDABSubChCnt();

			for (j = 0; j < nCnt; j++, nSubChIdx++) {
				pFCI_SubChInfo = (i == 0) ? DMBDrv_GetFICDMB(j) : DMBDrv_GetFICDAB(j);

				ensembleInfo->EnsembleID						= pFCI_SubChInfo->uiEnsembleID;
				ensembleInfo->SubChInfo[nSubChIdx].SubChID		= pFCI_SubChInfo->ucSubchID;
				ensembleInfo->SubChInfo[nSubChIdx].StartAddress = pFCI_SubChInfo->uiStartAddress;
				ensembleInfo->SubChInfo[nSubChIdx].TMId 		= pFCI_SubChInfo->ucTMId;
				ensembleInfo->SubChInfo[nSubChIdx].Type 		= pFCI_SubChInfo->ucServiceType;
				ensembleInfo->SubChInfo[nSubChIdx].ServiceID	= pFCI_SubChInfo->ulServiceID;
				if (i == 0)
					memcpy(ensembleInfo->SubChInfo[nSubChIdx].ServiceLabel, (char *)DMBDrv_GetSubChDMBLabel(j), SERVICE_LABEL_SIZE_MAX);
				else
					memcpy(ensembleInfo->SubChInfo[nSubChIdx].ServiceLabel, (char *)DMBDrv_GetSubChDABLabel(j), SERVICE_LABEL_SIZE_MAX);

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

static void FC8050_power_off()
{
	DPRINTK("call TDMB_PowerOff !\n");

	if (g_bPowerOn) {
		g_bOnAir = false;

		DMBDrv_DeInit();

		__control_irq(false);
		__control_gpio(false);

		g_bPowerOn = false;
	}
}

static bool FC8050_power_on()
{
	DPRINTK("__tdmb_drv_power_on - OK\n");

	if (g_bPowerOn) {
		return true;
	} else {
		__control_gpio(true);

		if (DMBDrv_init() == TDMB_FAIL) {
			__control_gpio(false);
			return false;
		} else {
			__control_irq(true);
			g_bPowerOn = true;
			return true;
		}
	}
}

static void FC8050_get_dm(tdmb_dm *info)
{
	if (g_bPowerOn == true && g_bOnAir == true) {
		info->rssi = DMBDrv_GetRSSI();
		info->BER = DMBDrv_GetBER();
		info->antenna = DMBDrv_GetAntLevel();
		info->PER = 0;
	} else {
		info->rssi = 100;
		info->BER = 2000;
		info->PER = 0;
		info->antenna = 0;
	}
}

static bool FC8050_set_ch(unsigned long freqHz, unsigned char subhcid, bool factory_test)
{
	unsigned long ulFreq = freqHz / 1000;
	unsigned char subChID = subhcid % 1000;
	unsigned char svcType = 0x0;

	if (subChID >= 64) {
		subChID -= 64;
		svcType  = 0x18;
	}

	DPRINTK("IOCTL_TDMB_ASSIGN_CH ulFreq:%d, subChID:%d, svcType:%d\n", ulFreq, subChID, svcType);

	g_bOnAir = false;

	if (DMBDrv_SetCh(ulFreq, subChID, svcType) == 1) {
		DPRINTK("DMBDrv_SetCh Success\n");
		g_bOnAir = true;
		return true;
	} else {
		DPRINTK("DMBDrv_SetCh Fail\n");
		return false;
	}
}

static bool FC8050_scan_ch(EnsembleInfoType *ensembleInfo, unsigned long freqHz)
{
	if (g_bPowerOn == false || ensembleInfo == NULL)
		return false;
	else if (DMBDrv_ScanCh((freqHz / 1000)) == TDMB_SUCCESS)
		return __get_ensemble_info(ensembleInfo, freqHz);
	else
		return false;
}

static unsigned long FC8050_int_size(void)
{
	return 188*20;
}

static TDMBDrvFunc FC8050DrvFunc = {
	.power_on = FC8050_power_on,
	.power_off = FC8050_power_off,
	.scan_ch = FC8050_scan_ch,
	.get_dm = FC8050_get_dm,
	.set_ch = FC8050_set_ch,
	.pull_data = DMBDrv_ISR,
	.get_int_size = FC8050_int_size,
};

TDMBDrvFunc * tdmb_get_drv_func(struct tdmb_platform_data * gpio)
{
	DPRINTK("tdmb_get_drv_func : FC8050\n");
	memcpy(&gpio_cfg, gpio, sizeof(struct tdmb_platform_data));
	return &FC8050DrvFunc;
}
