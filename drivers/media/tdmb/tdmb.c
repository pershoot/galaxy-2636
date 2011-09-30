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
#define TDMB_PRE_MALLOC	1

static struct class *tdmb_class;

/* ring buffer */
char *TS_RING;
unsigned int *tdmb_ts_head;
unsigned int *tdmb_ts_tail;
char *tdmb_ts_buffer;
unsigned int tdmb_ts_size;

unsigned int *cmd_head;
unsigned int *cmd_tail;
static char *cmd_buffer;
static unsigned int cmd_size;

static unsigned long g_last_ch_info;

static TDMBDrvFunc *tdmbdrv_func;

static int tdmb_open(struct inode *inode, struct file *filp)
{
	DPRINTK("tdmb_open!\n");
	return 0;
}

static int tdmb_read(struct file *file, unsigned int cmd, unsigned long data)
{
	DPRINTK("tdmb_read!\n");

	return 0;
}

static int tdmb_release(struct inode *inode, struct file *filp)
{
	DPRINTK("tdmb_release! \r\n");

	/* For tdmb_release() without TDMB POWER OFF (App abnormal -> kernal panic) */
	tdmbdrv_func->power_off();

#if TDMB_PRE_MALLOC
	tdmb_ts_size = 0;
	cmd_size = 0;
#else
	if (TS_RING != 0) {
		kfree(TS_RING);
		TS_RING = 0;
		tdmb_ts_size = 0;
		cmd_size = 0;
	}
#endif

	return 0;
}

#if TDMB_PRE_MALLOC
static void tdmb_make_ring_buffer(void)
{
	size_t size = TDMB_RING_BUFFER_MAPPING_SIZE;

	/* size should aligned in PAGE_SIZE */
	if (size % PAGE_SIZE) /* klaatu hard coding */
		size = size + size % PAGE_SIZE;

	TS_RING = kmalloc(size, GFP_KERNEL);
	DPRINTK("RING Buff Create OK\n");
}

#endif

static int tdmb_mmap(struct file *filp, struct vm_area_struct *vma)
{
	size_t size;
	unsigned long pfn;

	DPRINTK("%s\n", __func__);

	vma->vm_flags |= VM_RESERVED;
	size = vma->vm_end - vma->vm_start;
	DPRINTK("size given : %x\n", size);

#if TDMB_PRE_MALLOC
	size = TDMB_RING_BUFFER_MAPPING_SIZE;
	if (!TS_RING) {
		DPRINTK("RING Buff ReAlloc(%d)!!\n", size);
#endif
		/* size should aligned in PAGE_SIZE */
		if (size % PAGE_SIZE) /* klaatu hard coding */
			size = size + size % PAGE_SIZE;

		TS_RING = kmalloc(size, GFP_KERNEL);
#if TDMB_PRE_MALLOC
	}
#endif

	pfn = virt_to_phys(TS_RING) >> PAGE_SHIFT;

	DPRINTK("vm_start:%lx,TS_RING:%s,size:%x,prot:%lx,pfn:%lx\n",
			vma->vm_start, TS_RING, size, vma->vm_page_prot, pfn);

	if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot))
		return -EAGAIN;

	DPRINTK("succeeded\n");

	tdmb_ts_head = TS_RING;
	tdmb_ts_tail = TS_RING + 4;
	tdmb_ts_buffer = TS_RING + 8;

	*tdmb_ts_head = 0;
	*tdmb_ts_tail = 0;

	tdmb_ts_size = size-8; /* klaatu hard coding */
	tdmb_ts_size = ((tdmb_ts_size / DMB_TS_SIZE) * DMB_TS_SIZE) - (30 * DMB_TS_SIZE);

	DPRINTK("tdmb_ts_head : %x, tdmb_ts_tail : %x, tdmb_ts_buffer : %x,tdmb_ts_size : %x\n",
				(unsigned int)tdmb_ts_head, (unsigned int)tdmb_ts_tail, (unsigned int)tdmb_ts_buffer, tdmb_ts_size);

	cmd_buffer = tdmb_ts_buffer + tdmb_ts_size + 8;
	cmd_head = cmd_buffer - 8;
	cmd_tail = cmd_buffer - 4;

	*cmd_head = 0;
	*cmd_tail = 0;

	cmd_size = 30 * DMB_TS_SIZE - 8; /* klaatu hard coding */

	DPRINTK("cmd_head : %x, cmd_tail : %x, cmd_buffer : %x, cmd_size : %x\n",
				(unsigned int)cmd_head, (unsigned int)cmd_tail, (unsigned int)cmd_buffer, cmd_size);

	return 0;
}


static int _tdmb_cmd_update(
	unsigned char *byCmdsHeader,
	unsigned char byCmdsHeaderSize,
	unsigned char *byCmds,
	unsigned short bySize)
{
	unsigned int size;
	unsigned int head;
	unsigned int tail;
	unsigned int dist;
	unsigned int temp_size;
	unsigned int dataSize;

	if (bySize > cmd_size) {
		DPRINTK(" Error - cmd size too large\n");
		return false;
	}

	head = *cmd_head;
	tail = *cmd_tail;
	size = cmd_size;
	dataSize = bySize + byCmdsHeaderSize;

	if (head >= tail)
		dist = head-tail;
	else
		dist = size + head-tail;

	if (size - dist <= dataSize) {
		DPRINTK("_tdmb_cmd_update too small space is left in Command Ring Buffer!!\n");
		return false;
	}

	DPRINTK("Error - %x head %d tail %d\n", (unsigned int)cmd_buffer, head, tail);

	if (head+dataSize <= size) {
		memcpy((cmd_buffer + head), (char *)byCmdsHeader, byCmdsHeaderSize);
		memcpy((cmd_buffer + head + byCmdsHeaderSize), (char *)byCmds, size);
		head += dataSize;
		if (head == size)
			head = 0;
	} else {
		temp_size = size - head;
		if (temp_size < byCmdsHeaderSize) {
			memcpy((cmd_buffer+head), (char *)byCmdsHeader, temp_size);
			memcpy((cmd_buffer), (char *)byCmdsHeader+temp_size, (byCmdsHeaderSize - temp_size));
			head = byCmdsHeaderSize - temp_size;
		} else {
			memcpy((cmd_buffer+head), (char *)byCmdsHeader, byCmdsHeaderSize);
			head += byCmdsHeaderSize;
			if (head == size)
				head = 0;
		}

		temp_size = size - head;
		memcpy((cmd_buffer + head), (char *)byCmds, temp_size);
		head = dataSize - temp_size;
		memcpy(cmd_buffer, (char *)(byCmds + temp_size), head);
	}

	*cmd_head = head;

	return true;
}

unsigned char tdmb_make_result(
	unsigned char byCmd,
	unsigned short byDataLength,
	unsigned char  *pbyData)
{
	unsigned char byCmds[256] = {0,};

	byCmds[0] = TDMB_CMD_START_FLAG;
	byCmds[1] = byCmd;
	byCmds[2] = (byDataLength>>8)&0xff;
	byCmds[3] = byDataLength&0xff;

#if 0
	if (byDataLength > 0) {
		if (pbyData == NULL) {
			/* to error  */
			return false;
		}
		memcpy(byCmds + 8, pbyData, byDataLength) ;
	}
#endif
	_tdmb_cmd_update(byCmds, 4 , pbyData,  byDataLength);

	return true;
}

unsigned long tdmb_get_chinfo(void)
{
	return g_last_ch_info;
}

void tdmb_pull_data(void)
{
	tdmbdrv_func->pull_data();
}

static int tdmb_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	//int copy_to_user_return = 0;
	//unsigned long ulFreq = 0;
	//unsigned char subChID = 0;
	//unsigned char svcType = 0;
	unsigned long FIG_Frequency;
	EnsembleInfoType *pEnsembleInfo;
	tdmb_dm dmBuff;

	DPRINTK("call tdmb_ioctl : 0x%x\n", cmd);

	if(_IOC_TYPE(cmd) != IOCTL_MAGIC) {
		DPRINTK("tdmb_ioctl : _IOC_TYPE error\n");
		return -EINVAL;
	}		
	if(_IOC_NR(cmd) >= IOCTL_MAXNR) {
		DPRINTK("tdmb_ioctl : _IOC_NR(cmd) 0x%x\n", _IOC_NR(cmd));	
		return -EINVAL;
	}

	switch (cmd) {
	case IOCTL_TDMB_GET_DATA_BUFFSIZE:
		DPRINTK("IOCTL_TDMB_GET_DATA_BUFFSIZE %d\n", tdmb_ts_size);
		ret = copy_to_user((unsigned int *)arg, &tdmb_ts_size, sizeof(unsigned int));
		break;

	case IOCTL_TDMB_GET_CMD_BUFFSIZE:
		DPRINTK("IOCTL_TDMB_GET_CMD_BUFFSIZE %d\n", cmd_size);
		ret = copy_to_user((unsigned int *)arg, &cmd_size, sizeof(unsigned int));
		break;

	case IOCTL_TDMB_POWER_ON:
		DPRINTK("IOCTL_TDMB_POWER_ON\n");
		if (tdmb_create_databuffer(tdmbdrv_func->get_int_size()) == false)
			ret = false;
		else if (tdmb_create_workqueue() == true)
			ret = tdmbdrv_func->power_on();
		else
			ret = false;
		break;

	case IOCTL_TDMB_POWER_OFF:
		DPRINTK("IOCTL_TDMB_POWER_OFF\n");
		tdmbdrv_func->power_off();
		tdmb_destroy_workqueue();
		tdmb_destroy_databuffer();
		g_last_ch_info = 0;
		ret = true;
		break;

	case IOCTL_TDMB_SCAN_FREQ_ASYNC:
		DPRINTK("IOCTL_TDMB_SCAN_FREQ_ASYNC\n");

		FIG_Frequency = arg;

		pEnsembleInfo = vmalloc(sizeof(EnsembleInfoType));
		memset((char *)pEnsembleInfo, 0x00, sizeof(EnsembleInfoType));

		ret = tdmbdrv_func->scan_ch(pEnsembleInfo, FIG_Frequency);
		if (ret == true) {
			tdmb_make_result(DMB_FIC_RESULT_DONE, sizeof(EnsembleInfoType), (unsigned char  *)pEnsembleInfo);
		} else {
			tdmb_make_result(DMB_FIC_RESULT_FAIL, sizeof(unsigned long), (unsigned char  *)&FIG_Frequency);
		}

		vfree(pEnsembleInfo);
		g_last_ch_info = 0;
		break;

	case IOCTL_TDMB_SCAN_FREQ_SYNC:
		DPRINTK("IOCTL_TDMB_SCAN_FREQ_SYNC %ld\n", FIG_Frequency);

		FIG_Frequency = ((EnsembleInfoType *)arg)->EnsembleFrequency;

		pEnsembleInfo = vmalloc(sizeof(EnsembleInfoType));
		memset((char *)pEnsembleInfo, 0x00, sizeof(EnsembleInfoType));

		ret = tdmbdrv_func->scan_ch(pEnsembleInfo, FIG_Frequency);
		if (ret == true) {
			if(copy_to_user((EnsembleInfoType *)arg, pEnsembleInfo, sizeof(EnsembleInfoType)))
				DPRINTK("IOCTL_TDMB_SCAN_FREQ_SYNC : copy_to_user failed\n");
		}

		vfree(pEnsembleInfo);
		g_last_ch_info = 0;
		break;

	case IOCTL_TDMB_SCANSTOP:
		DPRINTK("IOCTL_TDMB_SCANSTOP\n");
		ret = false;
		break;

	case IOCTL_TDMB_ASSIGN_CH:
		DPRINTK("IOCTL_TDMB_ASSIGN_CH %ld\n", arg);
		tdmb_init_data();
		ret = tdmbdrv_func->set_ch(arg, (arg % 1000), false);
		if (ret == true)
			g_last_ch_info = arg;
		else
			g_last_ch_info = 0;
		break;

	case IOCTL_TDMB_ASSIGN_CH_TEST:
		DPRINTK("IOCTL_TDMB_ASSIGN_CH_TEST %ld\n", arg);
		tdmb_init_data();
		ret = tdmbdrv_func->set_ch(arg, (arg % 1000), true);
		if (ret == true)
			g_last_ch_info = arg;
		else
			g_last_ch_info = 0;
		break;

	case IOCTL_TDMB_GET_DM:
		tdmbdrv_func->get_dm(&dmBuff);
		if(copy_to_user((tdmb_dm *)arg, &dmBuff, sizeof(tdmb_dm)))
			DPRINTK("IOCTL_TDMB_GET_DM : copy_to_user failed\n");
		ret = true;
		DPRINTK("rssi %d, ber %d, ANT %d\n", dmBuff.rssi, dmBuff.BER, dmBuff.antenna);
		break;
	}

	return ret;
}

static const struct file_operations tdmb_ctl_fops = {
	owner:		THIS_MODULE,
	open :		tdmb_open,
	read :		tdmb_read,
	unlocked_ioctl : tdmb_ioctl,
	mmap :		tdmb_mmap,
	release :	tdmb_release,
	llseek :	no_llseek,
};

static int tdmb_probe(struct platform_device *pdev)
{
	int ret;
	struct device *tdmb_dev;
	struct resource *res;
	int tdmb_irq;
	struct tdmb_platform_data *p = pdev->dev.platform_data;

	DPRINTK("call tdmb_probe\n");

	ret = register_chrdev(TDMB_DEV_MAJOR, TDMB_DEV_NAME, &tdmb_ctl_fops);
	if (ret < 0)
		DPRINTK("register_chrdev(TDMB_DEV) failed!\n");

	tdmb_class = class_create(THIS_MODULE, TDMB_DEV_NAME);
	if (IS_ERR(tdmb_class)) {
		unregister_chrdev(TDMB_DEV_MAJOR, TDMB_DEV_NAME);
		class_destroy(tdmb_class);
		DPRINTK("class_create failed!\n");

		return -EFAULT;
	}

	tdmb_dev = device_create(tdmb_class, NULL, MKDEV(TDMB_DEV_MAJOR, TDMB_DEV_MINOR), NULL, TDMB_DEV_NAME);
	if (IS_ERR(tdmb_dev)) {
		DPRINTK("device_create failed!\n");

		unregister_chrdev(TDMB_DEV_MAJOR, TDMB_DEV_NAME);
		class_destroy(tdmb_class);

		return -EFAULT;
	}

	tdmb_init_bus();
	tdmbdrv_func = tdmb_get_drv_func(p);

#if TDMB_PRE_MALLOC
	tdmb_make_ring_buffer();
#endif

	return 0;
}

static int tdmb_remove(struct platform_device *pdev)
{
	return 0;
}

static int tdmb_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	return 0;
}

static int tdmb_resume(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver tdmb_driver = {
	.probe  = tdmb_probe,
	.remove = tdmb_remove,
	.suspend  = tdmb_suspend,
	.resume = tdmb_resume,
	.driver = {
		.owner  = THIS_MODULE,
		.name = "tdmb"
	},
};

static int __init tdmb_init(void)
{
	int ret;

	DPRINTK("<klaatu TDMB> module init\n");
	ret = platform_driver_register(&tdmb_driver);
	if (ret)
		return ret;

	return 0;
}

static void __exit tdmb_exit(void)
{
	DPRINTK("<klaatu TDMB> module exit\n");
#if TDMB_PRE_MALLOC
	if (TS_RING != 0) {
		kfree(TS_RING);
		TS_RING = 0;
	}
#endif
	unregister_chrdev(TDMB_DEV_MAJOR, "tdmb");
	device_destroy(tdmb_class, MKDEV(TDMB_DEV_MAJOR, TDMB_DEV_MINOR));
	class_destroy(tdmb_class);

	platform_driver_unregister(&tdmb_driver);

	tdmb_exit_bus();
}

module_init(tdmb_init);
module_exit(tdmb_exit);

MODULE_AUTHOR("Samsung");
MODULE_DESCRIPTION("TDMB Driver");
MODULE_LICENSE("GPL v2");
