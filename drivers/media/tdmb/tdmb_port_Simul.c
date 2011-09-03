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
#include <linux/io.h>

#include <linux/fs.h>
#include <linux/uaccess.h>

#include <linux/time.h>
#include <linux/timer.h>

#include <plat/gpio-cfg.h>
#include <mach/regs-clock.h>
#include <linux/vmalloc.h>

#include "tdmb.h"

static bool g_bOnAir;
static bool g_bPowerOn;

#define TDMB_FROM_FILE_BIN "/data/test.ts"
/* 0.1sec */
#define TIME_STEP (10*HZ/100)

struct file *fp_tff;
struct timer_list tff_timer;

static void __timeover(void)
{
	tdmb_irq_handler(0, NULL);
}

static int __read_from_file_to_dest(char *dest, int size)
{
	int ret = 0;
	int temp_size;
	int data_received = 0;
	mm_segment_t oldfs;

	temp_size = size;

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	do {
		temp_size -= ret;
		ret = fp_tff->f_op->read(fp_tff, dest, temp_size, &fp_tff->f_pos);
		DPRINTK("---> file read [ret:%d] [f_pos:%d]\n", ret, fp_tff->f_pos);
		if (ret < temp_size) {
			DPRINTK(" file from the first\n");
			fp_tff->f_op->llseek(fp_tff, 0, SEEK_SET);
		}
		data_received += ret;
	} while (ret < temp_size);

	set_fs(oldfs);

	return ret;
}

static bool __get_ensemble_info(EnsembleInfoType *ensembleInfo, unsigned long freqHz)
{
	int i;
	int j;
	int nSubChIdx = 0;
	int nCnt;
	const char *ensembleName = NULL;

	DPRINTK("_get_ensemble_info - freq(%d)\n", freqHz);

	if (freqHz == 205280000) {
		ensembleInfo->TotalSubChNumber = 1;
		strncpy((char *)ensembleInfo->EnsembleLabelCharField, (char *)"Samsung", ENSEMBLE_LABEL_SIZE_MAX);

		ensembleInfo->EnsembleFrequency = freqHz;

		ensembleInfo->SubChInfo[nSubChIdx].SubChID		= 0;
		ensembleInfo->SubChInfo[nSubChIdx].StartAddress = 0;
		ensembleInfo->SubChInfo[nSubChIdx].TMId 		= 1;
		ensembleInfo->SubChInfo[nSubChIdx].Type 		= 0x18;
		ensembleInfo->SubChInfo[nSubChIdx].ServiceID	= 100;
		memcpy(ensembleInfo->SubChInfo[nSubChIdx].ServiceLabel, "TFF", SERVICE_LABEL_SIZE_MAX);

		return true;
	} else {
		return false;
	}
}

static void __register_timer(struct timer_list *timer, unsigned long timeover)
{
	init_timer(timer);
	timer->expires = get_jiffies_64() + timeover;
	timer->function = __timeover;
	add_timer(timer);
}

static void TFF_power_off()
{
	DPRINTK("call TDMB_PowerOff !\n");

	if (g_bPowerOn) {
		g_bOnAir = false;
		del_timer(&tff_timer);
		filp_close(fp_tff, NULL);
		g_bPowerOn = false;
	}
}

static bool TFF_power_on()
{
	DPRINTK("__tdmb_drv_power_on - OK\n");

	if (g_bPowerOn) {
		return true;
	} else {
		int ret = 0;
		mm_segment_t oldfs;
		
		DPRINTK("%s\n", __func__);
		DPRINTK("##############################\n");
		
		fp_tff = filp_open(TDMB_FROM_FILE_BIN, O_RDONLY, 0);
		if (IS_ERR(fp_tff)) {
			return false;
		} else {
			__register_timer(&tff_timer, TIME_STEP);

			g_bPowerOn = true;
			return true;
		}
	}
}

static void TFF_get_dm(tdmb_dm *info)
{
	info->rssi = 100;
	info->BER = 2000;
	info->PER = 0;
	info->antenna = 0;
}

static bool TFF_set_ch(unsigned long freqHz, unsigned char subhcid, bool factory_test)
{
	__register_timer(&tff_timer, TIME_STEP);
	return true;
}

static bool TFF_scan_ch(EnsembleInfoType *ensembleInfo, unsigned long freqHz)
{
	if (g_bPowerOn == false || ensembleInfo == NULL)
		return false;
	else
		return __get_ensemble_info(ensembleInfo, freqHz);
}

static void TFF_pull_data(void)
{
	int ret = 0;
	mm_segment_t oldfs;
	unsigned int size;
	unsigned int head;
	unsigned int tail;
	unsigned int dist;
	unsigned int temp_size;

	extern unsigned int *tdmb_ts_head;
	extern unsigned int *tdmb_ts_tail;
	extern char *tdmb_ts_buffer;
	extern unsigned int tdmb_ts_size;

	DPRINTK("%s\n", __func__);

	size = DMB_TS_SIZE*40;
	head = *tdmb_ts_head;
	tail = *tdmb_ts_tail;

	DPRINTK("entering head:%d,tail:%d size:%d,ps_size:%d\n", head, tail, size, tdmb_ts_size);

	if (size > tdmb_ts_size) {
		DPRINTK("Error - size too large\n");
	} else {
		if (head >= tail)
			dist = head-tail;
		else
			dist = tdmb_ts_size+head-tail;

		DPRINTK("dist: %x\n", dist);

		if ((tdmb_ts_size-dist) < size) {
			DPRINTK("too small space is left in Ring Buffer!!\n");
		} else {
			if (head+size <= tdmb_ts_size) {
				ret = __read_from_file_to_dest((tdmb_ts_buffer+head), size);

				head += ret;
				if (head == tdmb_ts_size)
					head = 0;
			} else {
				temp_size = tdmb_ts_size-head;
				temp_size = (temp_size/DMB_TS_SIZE)*DMB_TS_SIZE;

				if (temp_size > 0) {
					ret = __read_from_file_to_dest((tdmb_ts_buffer+head), temp_size);
					temp_size = ret;
				}

				ret = __read_from_file_to_dest(tdmb_ts_buffer, size-temp_size);

				head = size-temp_size;
			}

			DPRINTK("< data > %x, %x, %x, %x\n",
						*(tdmb_ts_buffer + *tdmb_ts_head),
						*(tdmb_ts_buffer + *tdmb_ts_head + 1),
						*(tdmb_ts_buffer + *tdmb_ts_head + 2),
						*(tdmb_ts_buffer + *tdmb_ts_head + 3));

			DPRINTK("exiting - head : %d\n", head);

			*tdmb_ts_head = head;
		}
	}

	__register_timer(&tff_timer, TIME_STEP);
}

unsigned long TFF_int_size(void)
{
	return 188*20;
}

static const TDMBDrvFunc TFFDrvFunc = {
	.power_on = TFF_power_on,
	.power_off = TFF_power_off,
	.scan_ch = TFF_scan_ch,
	.get_dm = TFF_get_dm,
	.set_ch = TFF_set_ch,
	.pull_data = TFF_pull_data,
	.get_int_size = TFF_int_size,
};

TDMBDrvFunc * tdmb_get_drv_func(struct tdmb_platform_data * gpio)
{
	DPRINTK("tdmb_get_drv_func : Simul\n");
	return &TFFDrvFunc;
}
