/* drivers/media/tdmb/tdmb.c
 *
 *  TDMB Driver for Linux
 *
 *  klaatu, Copyright (c) 2009 Samsung Electronics
 *      http://www.samsung.com/
 *
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>

#include <linux/types.h>
#include <linux/fcntl.h>

/* for delay(sleep) */
#include <linux/delay.h>

/* for mutex */
#include <linux/mutex.h>

/*using copy to user */
#include <linux/uaccess.h>
#include <linux/clk.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include <linux/workqueue.h>
#include <linux/irq.h>
#include <asm/mach/irq.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>

#include <linux/io.h>
#include <mach/gpio.h>

#include "tdmb.h"

#define TS_PACKET_SIZE 188
#define MSC_BUF_SIZE 1024
#if 1
static unsigned char *MSCBuff;
static unsigned char *TSBuff;
#else
static unsigned char MSCBuff[1024];
static unsigned char TSBuff[INTERRUPT_SIZE * 2];
#endif
static int gTSBuffSize = 0;
static int bfirst = 1;
static int TSBuffpos;
static int MSCBuffpos;
static int mp2len;
static const int bitRateTable[2][16] = {
	/* MPEG1 for id=1*/
	{0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0},
	/* MPEG2 for id=0 */
	{0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0}
};

static struct workqueue_struct *tdmb_workqueue;
DECLARE_WORK(tdmb_work, tdmb_pull_data);

irqreturn_t tdmb_irq_handler(int irq, void *dev_id)
{
	int ret = 0;

	if (tdmb_workqueue) {
		ret = queue_work(tdmb_workqueue, &tdmb_work);
		if (ret == 0)
			DPRINTK("failed in queue_work\n");
	}

	return IRQ_HANDLED;
}

bool tdmb_create_databuffer(unsigned long int_size)
{
	gTSBuffSize = int_size * 2;

	MSCBuff = vmalloc(MSC_BUF_SIZE);
	TSBuff = vmalloc(gTSBuffSize);

	if (MSCBuff && TSBuff) {
		return true;
	} else {
		if (MSCBuff)
			vfree(MSCBuff);
		if (TSBuff)
			vfree(TSBuff);

		return false;
	}
}

void tdmb_destroy_databuffer(void)
{
	vfree(MSCBuff);
	vfree(TSBuff);
}

bool tdmb_create_workqueue(void)
{
	tdmb_workqueue = create_singlethread_workqueue("ktdmbd");
	if (tdmb_workqueue)
		return true;
	else
		return false;
}

bool tdmb_destroy_workqueue(void)
{
	if (tdmb_workqueue) {
		flush_workqueue(tdmb_workqueue);
		destroy_workqueue(tdmb_workqueue);
		tdmb_workqueue = NULL;
	}
	return true;
}

void tdmb_init_data(void)
{
    bfirst = 1;
    TSBuffpos = 0;
    MSCBuffpos = 0;
    mp2len = 0;    
}

static int __add_to_ringbuffer(unsigned char *pData, unsigned long dwDataSize)
{
	int ret = 0;
	unsigned int size;
	unsigned int head;
	unsigned int tail;
	unsigned int dist;
	unsigned int temp_size;
	extern unsigned int *tdmb_ts_head;
	extern unsigned int *tdmb_ts_tail;
	extern char *tdmb_ts_buffer;
	extern unsigned int tdmb_ts_size;

	if (tdmb_ts_size == 0)
		return 0;

	size = dwDataSize;
	head = *tdmb_ts_head;
	tail = *tdmb_ts_tail;

	if (size > tdmb_ts_size) {
		DPRINTK("Error - size too large\n");
	} else {
		if (head >= tail)
			dist = head-tail;
		else
			dist = tdmb_ts_size+head-tail;

		/* DPRINTK("dist: %x\n", dist); */

		if ((tdmb_ts_size-dist) < size) {
			DPRINTK("too small space is left in Ring Buffer(ts len:%d/free:%d)\n", size, (tdmb_ts_size-dist));
			DPRINTK("tdmb_ts_head:0x%x,tdmb_ts_tail:0x%x/head:%d,tail:%d \n", tdmb_ts_head, tdmb_ts_tail, head, tail);
		} else {
			if (head+size <= tdmb_ts_size) {
				memcpy((tdmb_ts_buffer+head), (char *)pData, size);

				head += size;
				if (head == tdmb_ts_size)
					head = 0;
			} else {
				temp_size = tdmb_ts_size-head;
				temp_size = (temp_size/DMB_TS_SIZE)*DMB_TS_SIZE;

				if (temp_size > 0)
					memcpy((tdmb_ts_buffer+head), (char *)pData, temp_size);

				memcpy(tdmb_ts_buffer, (char *)(pData+temp_size), size-temp_size);
				head = size-temp_size;
			}

			/*
			 *      DPRINTK("< data > %x, %x, %x, %x\n",
			 *            *(tdmb_ts_buffer+ *tdmb_ts_head),
			 *            *(tdmb_ts_buffer+ *tdmb_ts_head +1),
			 *            *(tdmb_ts_buffer+ *tdmb_ts_head +2),
			 *            *(tdmb_ts_buffer+ *tdmb_ts_head +3) );

			 *      DPRINTK("exiting - head : %d\n",head);
			 */

			*tdmb_ts_head = head;
		}
	}

	return ret;
}


static int __add_ts_data(unsigned char *pData, unsigned long dwDataSize)
{
	int j = 0;
	int maxi = 0;
	if (bfirst) {
		DPRINTK("!!!!! first sync dwDataSize = %ld !!!!!\n", dwDataSize);

		for (j = 0; j < dwDataSize; j++) {
			if (pData[j] == 0x47) {
				DPRINTK("!!!!! first sync j = %d !!!!!\n", j);
				tdmb_make_result(DMB_TS_PACKET_RESYNC, sizeof(int), (unsigned char *)&j);
				maxi = (dwDataSize - j) / TS_PACKET_SIZE;
				TSBuffpos = (dwDataSize - j) % TS_PACKET_SIZE;
				__add_to_ringbuffer(&pData[j], maxi * TS_PACKET_SIZE);
				if (TSBuffpos > 0)
					memcpy(TSBuff, &pData[j + maxi * TS_PACKET_SIZE], TSBuffpos);
				bfirst = 0;
				return 0;
			}
		}
	} else {
		maxi = (dwDataSize) / TS_PACKET_SIZE;

		if (TSBuffpos > 0) {
			if (pData[TS_PACKET_SIZE - TSBuffpos] != 0x47) {
				DPRINTK("!!!!!!!!!!!!! error 0x%x,0x%x!!!!!!!!!!!!\n",
							pData[TS_PACKET_SIZE - TSBuffpos],
							pData[TS_PACKET_SIZE - TSBuffpos + 1]);

				memset(TSBuff, 0, gTSBuffSize);
				TSBuffpos = 0;
				bfirst = 1;
				return -1;
			}

			memcpy(&TSBuff[TSBuffpos], pData, TS_PACKET_SIZE-TSBuffpos);
			__add_to_ringbuffer(TSBuff, TS_PACKET_SIZE);
			__add_to_ringbuffer(&pData[TS_PACKET_SIZE - TSBuffpos], dwDataSize - TS_PACKET_SIZE);
			memcpy(TSBuff, &pData[dwDataSize-TSBuffpos], TSBuffpos);
		} else {
			if (pData[0] != 0x47) {
				DPRINTK("!!!!!!!!!!!!! error 0x%x,0x%x!!!!!!!!!!!!\n",
								pData[0],
								pData[1]);

				memset(TSBuff, 0, gTSBuffSize);
				TSBuffpos = 0;
				bfirst = 1;
				return -1;
			}

			__add_to_ringbuffer(pData, dwDataSize);
		}
	}
	return 0;
}

static int __get_mp2_len(unsigned char *pkt)
{
	int id;
	int layer_index;
	int bitrate_index;
	int fs_index;
	int samplerate;
	int protection;
	int bitrate;
	int length;

	id = (pkt[1]>>3)&0x01; /* 1: ISO/IEC 11172-3, 0:ISO/IEC 13818-3 */
	layer_index = (pkt[1]>>1)&0x03; /* 2 */
	protection = pkt[1]&0x1;
/*
	if (protection != 0) {
		;
	}
*/
	bitrate_index = (pkt[2]>>4);
	fs_index = (pkt[2]>>2)&0x3; /* 1 */

	/* sync word check */
	if (pkt[0] == 0xff && (pkt[1]>>4) == 0xf) {
		if ((bitrate_index > 0 && bitrate_index < 15)
				&& (layer_index == 2) && (fs_index == 1)) {

			if (id == 1 && layer_index == 2) { /* Fs==48 KHz*/
				bitrate = 1000*bitRateTable[0][bitrate_index];
				samplerate = 48000;
			} else if (id == 0 && layer_index == 2) { /* Fs=24 KHz */
				bitrate = 1000*bitRateTable[1][bitrate_index];
				samplerate = 24000;
			} else
				return -1;

		} else
			return -1;
	} else
		return -1;

	if ((pkt[2]&0x02) != 0) { /* padding bit */
		return -1;
	}

	length = (144*bitrate)/(samplerate);

	return length;
}

static int __add_msc_data(unsigned char *pData, unsigned long dwDataSize, int SubChID)
{
	int j;
	int readpos = 0;
	unsigned char pOutAddr[TS_PACKET_SIZE];
	int remainbyte = 0;
	static int first = 1;

	if (bfirst) {
		for (j = 0; j < dwDataSize-4; j++) {
			if (pData[j] == 0xFF && ((pData[j+1]>>4) == 0xF)) {
				mp2len = __get_mp2_len(&pData[j]);
				DPRINTK("!!!! first sync mp2len= %d !!!!\n", mp2len);
				if (mp2len <= 0 || mp2len > MSC_BUF_SIZE)
					return -1;

				memcpy(MSCBuff, &pData[j], dwDataSize-j);
				MSCBuffpos = dwDataSize-j;
				bfirst = 0;
				first = 1;
				return 0;
			}
		}
	} else {
		if (mp2len <= 0 || mp2len > MSC_BUF_SIZE) {
			MSCBuffpos = 0;
			bfirst = 1;
			return -1;
		}

		remainbyte = dwDataSize;
		if ((mp2len-MSCBuffpos) >= dwDataSize) {
			memcpy(MSCBuff+MSCBuffpos, pData, dwDataSize);
			MSCBuffpos += dwDataSize;
			remainbyte = 0;
		} else if (mp2len-MSCBuffpos > 0) {
			memcpy(MSCBuff+MSCBuffpos, pData, (mp2len - MSCBuffpos));
			remainbyte = dwDataSize - (mp2len - MSCBuffpos);
			MSCBuffpos = mp2len;
		}

		if (MSCBuffpos == mp2len) {
			while (MSCBuffpos > readpos) {
				if (first) {
					pOutAddr[0] = 0xDF;
					pOutAddr[1] = 0xDF;
					pOutAddr[2] = (SubChID<<2);
					pOutAddr[2] |= (((MSCBuffpos>>3)>>8)&0x03);
					pOutAddr[3] = (MSCBuffpos>>3)&0xFF;

					if (!(MSCBuff[0] == 0xFF && ((MSCBuff[1]>>4) == 0xF))) {
						DPRINTK("!!!! error 0x%x,0x%x!!!!\n", MSCBuff[0], MSCBuff[1]);
						memset(MSCBuff, 0, MSC_BUF_SIZE);
						MSCBuffpos = 0;
						bfirst = 1;
						return -1;
					}

					memcpy(pOutAddr+4, MSCBuff, 184);
					readpos = 184;
					first = 0;
				} else {
					pOutAddr[0] = 0xDF;
					pOutAddr[1] = 0xD0;
					if (MSCBuffpos-readpos >= 184) {
						memcpy(pOutAddr+4, MSCBuff+readpos, 184);
						readpos += 184;
					} else {
						memcpy(pOutAddr+4, MSCBuff+readpos, MSCBuffpos-readpos);
						readpos += (MSCBuffpos-readpos);
					}
				}
				__add_to_ringbuffer(pOutAddr, TS_PACKET_SIZE);
			}

			first = 1;
			MSCBuffpos = 0;
			if (remainbyte > 0) {
				memcpy(MSCBuff, pData+dwDataSize-remainbyte, remainbyte);
				MSCBuffpos = remainbyte;
			}
		} else if (MSCBuffpos > mp2len) {
			DPRINTK("!!!! Error MSCBuffpos=%d, mp2len =%d!!!!\n", MSCBuffpos, mp2len);
			memset(MSCBuff, 0, MSC_BUF_SIZE);
			MSCBuffpos = 0;
			bfirst = 1;
			return -1;
		}
	}

	return 0;
}

bool tdmb_store_data(unsigned char *pData, unsigned long len)
{
	unsigned long i;
	unsigned long maxi;
	unsigned long subch_id = tdmb_get_chinfo();

	if (subch_id == 0) {
		return false;
	} else {
		subch_id = subch_id % 1000;

		if (subch_id >= 64) {
			__add_ts_data(pData, len);
		} else {
			maxi = len/TS_PACKET_SIZE;
			for (i = 0 ; i < maxi ; i++) {
				__add_msc_data(pData, TS_PACKET_SIZE, subch_id);
				pData += TS_PACKET_SIZE;
			}
		}
		return true;
	}
}
