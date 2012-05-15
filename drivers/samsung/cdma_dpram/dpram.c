/* dpram.c
 *
 * SAMSUNG TTY modem driver for cdma
 *
 * Copyright (C) 2010 Samsung Electronics. All rights reserved.
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

#define _HSDPA_DPRAM
#define _ENABLE_ERROR_DEVICE

#define _ENABLE_DEBUG_PRINTS

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/irq.h>
#include <linux/poll.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <asm/irq.h>
#include <mach/hardware.h>

#include <mach/hardware.h>
#include <mach/gpio.h>
#include <mach/gpio-sec.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/wakelock.h>
#include <linux/kernel_sec_common.h>

#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/time.h>
#include <linux/if_arp.h>

#include "dpram.h"

#define SVNET_PDP_ETHER
#ifdef SVNET_PDP_ETHER
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#endif

#define DRIVER_NAME 		"DPRAM"
#define DRIVER_PROC_ENTRY	"driver/dpram"
#define DRIVER_MAJOR_NUM	232

#define GPIO_DPRAM_INT_N GPIO_DP_INT_AP
#define GPIO_PHONE_RST_N GPIO_CP_RST
#define GPIO_PHONE_ON GPIO_CP_ON

#define WRITE_TO_DPRAM(dest, src, size) \
	_memcpy((void *)(DPRAM_VBASE + dest), src, size)

#define READ_FROM_DPRAM(dest, src, size) \
	_memcpy(dest, (void *)(DPRAM_VBASE + src), size)

/*****************************************************************************/
/*                             MULTIPDP FEATURE                              */
/*****************************************************************************/
#include <linux/miscdevice.h>
#include <linux/netdevice.h>

/* Device node name for application interface */
#define APP_DEVNAME				"multipdp"
/* number of PDP context */
#define NUM_PDP_CONTEXT			4

/* Device types */
#define DEV_TYPE_NET			0 /* network device for IP data */
#define DEV_TYPE_SERIAL			1 /* serial device for CSD */

/* Device major & minor number */
#define CSD_MAJOR_NUM			248
#define CSD_MINOR_NUM			0

/* Maximum number of PDP context */
#define MAX_PDP_CONTEXT			12

/* Maximum PDP data length */
#define MAX_PDP_DATA_LEN		1500

/* Device flags */
#define DEV_FLAG_STICKY			0x1 /* Sticky */

/* Device Identification */
#define ONEDRAM_DEV 			0
#define DPRAM_DEV			1
#define UNKNOWN_DEV			2

/* Radio Tye Identification */
#define LTE			    0
#define CDMA			1

/* Data Status */
#define SUSPEND			0
#define ACTIVE			1

/* Maximum PDP packet length including header and start/stop bytes */
#define MAX_PDP_PACKET_LEN		(MAX_PDP_DATA_LEN + 4 + 2)

struct sk_buff_head txq;

typedef enum _interface_type
{
	IPADDR_TYPE_IPV4 = 1,
	IPADDR_TYPE_IPV6,
	IPADDR_TYPE_IPV4V6,
	IPV6_TYPE_MAX
}interface_type_t;

typedef struct pdp_arg {
	unsigned char	id;
	char		ifname[16];
	char 		int_iden[8];
	interface_type_t en_interface_type;
} __attribute__ ((packed)) pdp_arg_t;


/* PDP data packet header format */
struct pdp_hdr {
	u16	len;		/* Data length */
	u8	id;			/* Channel ID */
	u8	control;	/* Control field */
} __attribute__ ((packed));

/* Virtual network interface */
struct vnet_struct{
	struct net_device	*net;
	struct net_device_stats	stats;
	struct sk_buff* 	skb_ptr;
	u8	    netq_init;				//0:netif_start_queue called    1: netif_start_queue() not called
	u8	    netq_active;			//0: don't call netif_wake_queue    1: Call netif_wake_queue
	struct semaphore netq_sem;	// sem to protect netq_init and netq_active
};

static struct work_struct xmit_work_struct;

static struct work_struct cp_crash_work;

/* PDP information type */
struct pdp_info {
	/* PDP context ID */
	u8		id;
	/* onedram or dpram interface */
	atomic_t		intf_dev;

	/* Device type */
	unsigned		type;

	/* Device flags */
	unsigned		flags;

	/* Tx packet buffer */
	u8		*tx_buf;

	/* App device interface */
	union {
		/* Virtual network interface */
		struct vnet_struct vnet_u;

		/* Virtual serial interface */
		struct {
			struct tty_driver tty_driver;
			int         		refcount;
			struct tty_struct	*tty_table[1];
			struct ktermios		*termios[1];
			struct ktermios		*termios_locked[1];
			char			tty_name[16];
			struct tty_struct	*tty;
			struct semaphore	write_lock;
		} vs_u;
	} dev_u;
#define vn_dev		dev_u.vnet_u
#define vs_dev		dev_u.vs_u
};


/* PDP information table */
static struct pdp_info *pdp_table[MAX_PDP_CONTEXT];
static DECLARE_MUTEX(pdp_lock);

static int g_adjust = 9;
static int g_radio = CDMA;		// Temp change to support EHRPD  LTE;//MULTIRIL
static int g_datastatus = ACTIVE;//To Suspend or resume data
static int pdp_tx_flag = 0;

static DECLARE_MUTEX(mux_net_lock);
static DECLARE_MUTEX(mux_tty_lock);


static unsigned char pdp_net_rx_buf[MAX_PDP_DATA_LEN + 100];

static inline struct pdp_info *pdp_get_dev(u8 id);
static int dpram_init_and_report(void);
void TestIOCTLHandler(void);
void ClearPendingInterruptFromModem(void);

static int pdp_mux_tty(struct pdp_info *dev, const void *data, size_t len);
static int pdp_mux_net();
static inline struct pdp_info * pdp_get_serdev(const char *name);
static inline struct pdp_info *pdp_remove_dev(u8 id);
static int  vnet_recv_rx_data(u8 *buf, int size, struct pdp_info *dev);
void dpram_debug_dump_raw_read_buffer(const unsigned char  *buf, int len);
void multipdp_debug_dump_write_buffer(const unsigned char  *buf, int len);
static void net_wakeup_all_if(void);
static void net_stop_all_if (void);
void request_phone_reset(unsigned long data);

struct timer_list phone_active_timer;
struct timer_list req_ack_timer;

/*****************************************************************************/
#ifdef _ENABLE_DEBUG_PRINTS

#define PRINT_WRITE
#define PRINT_READ
#define PRINT_WRITE_SHORT
#define PRINT_READ_SHORT
#define PRINT_SEND_IRQ
#define PRINT_RECV_IRQ
#define PRINT_IPC_FORMATTED_MSG

#define DPRAM_PRINT_ERROR             0x0001
#define DPRAM_PRINT_WARNING           0x0002
#define DPRAM_PRINT_INFO              0x0004
#define DPRAM_PRINT_WRITE             0x0008
#define DPRAM_PRINT_WRITE_SHORT       0x0010
#define DPRAM_PRINT_READ              0x0020
#define DPRAM_PRINT_READ_SHORT        0x0040
#define DPRAM_PRINT_SEND_IRQ          0x0080
#define DPRAM_PRINT_RECV_IRQ          0x0100
#define DPRAM_PRINT_HEAD_TAIL         0x0400
#define DPRAM_PRINT_IPC_FORMATTED_MSG 0x0800
#define DPRAM_PRINT_CBUF 			0x1000

#define MULTIPDP_PRINT_ERROR		0x0001
#define MULTIPDP_PRINT_WARNING		0x0002
#define MULTIPDP_PRINT_INFO			0x0004
#define MULTIPDP_PRINT_WRITE		0x0008
#define MULTIPDP_PRINT_READ			0x0010

extern void tegra_gpio_enable(int gpio);

void dpram_debug_print(u8 print_prefix, u32 mask,  const char *fmt, ...);
void register_dpram_debug_control_attribute(void);
void deregister_dpram_debug_control_attribute(void);
static ssize_t dpram_show_debug_level(struct device_driver *ddp, char *buf);
static ssize_t dpram_store_debug_level(struct device_driver *ddp, const char *buf, size_t count);

#define DPRAM_LOG_ERR(s, arg...)           dpram_debug_print(1, DPRAM_PRINT_ERROR, s, ## arg)
#define DPRAM_LOG_WARN(s, arg...)          dpram_debug_print(1, DPRAM_PRINT_WARNING, s, ## arg)
#define DPRAM_LOG_INFO(s, arg...)          dpram_debug_print(1, DPRAM_PRINT_INFO, s, ## arg)
#define DPRAM_LOG_WRITE(s, arg...)        dpram_debug_print(0, DPRAM_PRINT_WRITE, s, ## arg)
#define DPRAM_LOG_WRITE_SHORT(s, arg...)  dpram_debug_print(1, DPRAM_PRINT_WRITE_SHORT, s, ## arg)
#define DPRAM_LOG_READ(s, arg...)         dpram_debug_print(0, DPRAM_PRINT_READ, s, ## arg)
#define DPRAM_LOG_READ_SHORT(s, arg...)   dpram_debug_print(1, DPRAM_PRINT_READ_SHORT, s, ## arg)
#define DPRAM_LOG_SEND_IRQ(s, arg...)     dpram_debug_print(1, DPRAM_PRINT_SEND_IRQ, s, ## arg)
#define DPRAM_LOG_RECV_IRQ(s, arg...)     dpram_debug_print(1, DPRAM_PRINT_RECV_IRQ, s, ## arg)
#define DPRAM_LOG_HEAD_TAIL(s, arg...)    dpram_debug_print(1, DPRAM_PRINT_HEAD_TAIL, s, ## arg)
#define DPRAM_LOG_FIPC_MSG(s, arg...)     dpram_debug_print(0, DPRAM_PRINT_IPC_FORMATTED_MSG, s, ## arg)

void multipdp_debug_print(u32 mask,  const char *fmt, ...);
void register_multipdp_debug_control_attribute(void);
void deregister_multipdp_debug_control_attribute(void);
#define MULTIPDP_LOG_ERR(s,arg...)			multipdp_debug_print(MULTIPDP_PRINT_ERROR, s, ## arg)
#define MULTIPDP_LOG_WARN(s,arg...)			multipdp_debug_print(MULTIPDP_PRINT_WARNING, s, ## arg)
#define MULTIPDP_LOG_INFO(s,arg...)			multipdp_debug_print(MULTIPDP_PRINT_INFO, s, ## arg)
#define MULTIPDP_LOG_READ(s,arg...)			multipdp_debug_print(MULTIPDP_PRINT_WRITE, s, ## arg)
#define MULTIPDP_LOG_WRITE(s,arg...)		multipdp_debug_print(MULTIPDP_PRINT_READ, s, ## arg)

u16 dpram_debug_mask = DPRAM_PRINT_ERROR | DPRAM_PRINT_WARNING;
//u16 dpram_debug_mask = DPRAM_PRINT_ERROR | DPRAM_PRINT_WARNING | DPRAM_PRINT_IPC_FORMATTED_MSG | DPRAM_PRINT_READ_SHORT | DPRAM_PRINT_WRITE_SHORT | DPRAM_PRINT_INFO;

u16 mulitpdp_debug_mask = MULTIPDP_PRINT_ERROR | MULTIPDP_PRINT_WARNING;
//u16 mulitpdp_debug_mask = MULTIPDP_PRINT_ERROR | MULTIPDP_PRINT_WARNING | MULTIPDP_PRINT_INFO | MULTIPDP_PRINT_WRITE | MULTIPDP_PRINT_READ;

#else

#define DPRAM_LOG_ERR(s, arg...)           do { } while (0)
#define DPRAM_LOG_WARN(s, arg...)          do { } while (0)
#define DPRAM_LOG_INFO(s, arg...)          do { } while (0)
#define DPRAM_LOG_WRITE(s, arg...)        do { } while (0)
#define DPRAM_LOG_WRITE_SHORT(s, arg...)  do { } while (0)
#define DPRAM_LOG_READ(s, arg...)         do { } while (0)
#define DPRAM_LOG_READ_SHORT(s, arg...)   do { } while (0)
#define DPRAM_LOG_SEND_IRQ(s, arg...)     do { } while (0)
#define DPRAM_LOG_RECV_IRQ(s, arg...)     do { } while (0)
#define DPRAM_LOG_HEAD_TAIL(s, arg...)    do { } while (0)
#define DPRAM_LOG_FIPC_MSG(s, arg...)     do { } while (0)

#define MULTIPDP_LOG_ERR(s,arg...)			do { } while (0)
#define MULTIPDP_LOG_WARN(s,arg...)			do { } while (0)
#define MULTIPDP_LOG_INFO(s,arg...)			do { } while (0)
#define MULTIPDP_LOG_READ(s,arg...)			do { } while (0)
#define MULTIPDP_LOG_WRITE(s,arg...)		do { } while (0)

#endif /*_ENABLE_DEBUG_PRINTS */

#ifdef _ENABLE_ERROR_DEVICE
#define DPRAM_ERR_MSG_LEN			128	
#define DPRAM_ERR_DEVICE			"dpramerr"
#endif	/* _ENABLE_ERROR_DEVICE */

/***************************************************************************/
/*                              GPIO SETTING                               */
/***************************************************************************/
#include <mach/gpio.h>

#define GPIO_LEVEL_LOW		0
#define GPIO_LEVEL_HIGH		1

#define IRQ_DPRAM_INT_N   gpio_to_irq(GPIO_DP_INT_AP)		
#define IRQ_PHONE_ACTIVE	  gpio_to_irq(GPIO_PHONE_ACTIVE)


static void send_interrupt_to_phone(u16 irq_mask);

static void __iomem *dpram_base;
static void dpram_drop_data(dpram_device_t *device);

static int phone_sync;
static int dump_on;
static int dpram_phone_getstatus(void);

#define DPRAM_VBASE dpram_base

static struct tty_driver *dpram_tty_driver;

static dpram_tasklet_data_t dpram_tasklet_data[MAX_INDEX];

static dpram_device_t dpram_table[MAX_INDEX] = {
	{
		.in_head_addr = DPRAM_PHONE2PDA_FORMATTED_HEAD_ADDRESS,
		.in_tail_addr = DPRAM_PHONE2PDA_FORMATTED_TAIL_ADDRESS,
		.in_buff_addr = DPRAM_PHONE2PDA_FORMATTED_BUFFER_ADDRESS,
		.in_buff_size = DPRAM_PHONE2PDA_FORMATTED_BUFFER_SIZE,
		.in_head_saved = 0,
		.in_tail_saved = 0,

		.out_head_addr = DPRAM_PDA2PHONE_FORMATTED_HEAD_ADDRESS,
		.out_tail_addr = DPRAM_PDA2PHONE_FORMATTED_TAIL_ADDRESS,
		.out_buff_addr = DPRAM_PDA2PHONE_FORMATTED_BUFFER_ADDRESS,
		.out_buff_size = DPRAM_PDA2PHONE_FORMATTED_BUFFER_SIZE,
		.out_head_saved = 0,
		.out_tail_saved = 0,

		.mask_req_ack = INT_MASK_REQ_ACK_F,
		.mask_res_ack = INT_MASK_RES_ACK_F,
		.mask_send = INT_MASK_SEND_F,
	},
	{
		.in_head_addr = DPRAM_PHONE2PDA_RAW_HEAD_ADDRESS,
		.in_tail_addr = DPRAM_PHONE2PDA_RAW_TAIL_ADDRESS,
		.in_buff_addr = DPRAM_PHONE2PDA_RAW_BUFFER_ADDRESS,
		.in_buff_size = DPRAM_PHONE2PDA_RAW_BUFFER_SIZE,
		.in_head_saved = 0,
		.in_tail_saved = 0,

		.out_head_addr = DPRAM_PDA2PHONE_RAW_HEAD_ADDRESS,
		.out_tail_addr = DPRAM_PDA2PHONE_RAW_TAIL_ADDRESS,
		.out_buff_addr = DPRAM_PDA2PHONE_RAW_BUFFER_ADDRESS,
		.out_buff_size = DPRAM_PDA2PHONE_RAW_BUFFER_SIZE,
		.out_head_saved = 0,
		.out_tail_saved = 0,

		.mask_req_ack = INT_MASK_REQ_ACK_R,
		.mask_res_ack = INT_MASK_RES_ACK_R,
		.mask_send = INT_MASK_SEND_R,
	},
};

static struct tty_struct *dpram_tty[MAX_INDEX];
static struct ktermios *dpram_termios[MAX_INDEX];
static struct ktermios *dpram_termios_locked[MAX_INDEX];

static void res_ack_tasklet_handler(unsigned long data);
static void fmt_rcv_tasklet_handler(unsigned long data);
static void raw_rcv_tasklet_handler(unsigned long data);

static int pdp_activate(pdp_arg_t *pdp_arg, unsigned type, unsigned flags );

static DECLARE_TASKLET(fmt_send_tasklet, fmt_rcv_tasklet_handler, 0);
static DECLARE_TASKLET(raw_send_tasklet, raw_rcv_tasklet_handler, 0);
static DECLARE_TASKLET(fmt_res_ack_tasklet, res_ack_tasklet_handler, (unsigned long)&dpram_table[FORMATTED_INDEX]);
static DECLARE_TASKLET(raw_res_ack_tasklet, res_ack_tasklet_handler, (unsigned long)&dpram_table[RAW_INDEX]);

#ifdef _ENABLE_ERROR_DEVICE
static unsigned int is_dpram_err= FALSE;
static char dpram_err_buf[DPRAM_ERR_MSG_LEN];

struct class *dpram_class;
static DECLARE_WAIT_QUEUE_HEAD(dpram_err_wait_q);
static struct fasync_struct *dpram_err_async_q;
extern void p3_set_usb_path(usb_path_type usb_path);
#endif	/* _ENABLE_ERROR_DEVICE */

/* added for waiting until the phone gets UP */
/* There is a big delay (freeze for 17S) during boot time. This delay create a race condition with the PIF_INIT Wait done delay
   as we start the wait loop and after the initial freeze, when the code gets running, the timeout happens. So in initial
   stage we need a larger delay */
//static int modem_pif_init_wait_time_ctrl = FALSE;
int modem_pif_init_wait_condition;
int dpram_init_cmd_wait_condition;
static wait_queue_head_t modem_pif_init_done_wait_q;
static wait_queue_head_t dpram_init_cmd_wait_q;
struct wake_lock dpram_wake_lock;

#define PIF_TIMEOUT		180 * HZ
#define DPRAM_INIT_TIMEOUT		15 * HZ

static atomic_t raw_txq_req_ack_rcvd;
static atomic_t fmt_txq_req_ack_rcvd;
static u8 is_net_stopped = 0;
// 2008.10.20.
//static DECLARE_MUTEX(write_mutex);

/* if you enable this, remember there will be multi path to report the error to upper layer
   1 from /dev/dpram1 to error device and 2nd one from multipdp to error device.
   you should sync these two path */
//#define _ENABLE_SELF_ERROR_CORRECTION

void dpram_initiate_self_error_correction(void)
{
#ifdef _ENABLE_SELF_ERROR_CORRECTION
	int ret;
	DPRAM_LOG_ERR ("[DPRAM] dpram_initiate_self_error_correction\n");
	ret = mod_timer(&phone_active_timer, jiffies + msecs_to_jiffies(1000));
	if(ret) 
		printk(KERN_ERR "error timer!!\n");
#endif
}

/* tty related functions. */
static inline void byte_align(unsigned long dest, unsigned long src)
{
	u16 *p_src;
	volatile u16 *p_dest;

	if (!(dest % 2) && !(src % 2)) {
		p_dest = (u16 *)dest;
		p_src = (u16 *)src;

		*p_dest = (*p_dest & 0xFF00) | (*p_src & 0x00FF);
	} else if ((dest % 2) && (src % 2)) {
		p_dest = (u16 *)(dest - 1);
		p_src = (u16 *)(src - 1);

		*p_dest = (*p_dest & 0x00FF) | (*p_src & 0xFF00);
	} else if (!(dest % 2) && (src % 2)) {
		p_dest = (u16 *)dest;
		p_src = (u16 *)(src - 1);

		*p_dest = (*p_dest & 0xFF00) | ((*p_src >> 8) & 0x00FF);
	} else if ((dest % 2) && !(src % 2)) {
		p_dest = (u16 *)(dest - 1);
		p_src = (u16 *)src;

		*p_dest = (*p_dest & 0x00FF) | ((*p_src << 8) & 0xFF00);
	} else {
		DPRAM_LOG_ERR("oops.~\n");
	}
}

static inline void _memcpy(void *p_dest, const void *p_src, int size)
{
	unsigned long dest = (unsigned long)p_dest;
	unsigned long src = (unsigned long)p_src;

	if (size <= 0)
		return;

	if (dest & 1) {
		byte_align(dest, src);
		dest++, src++;
		size--;
	}

	if (size & 1) {
		byte_align(dest + size - 1, src + size - 1);
		size--;
	}

	if (src & 1) {
		unsigned char *s = (unsigned char *)src;
		volatile u16 *d = (unsigned short *)dest;

		size >>= 1;

		while (size--) {
			*d++ = s[0] | (s[1] << 8);
			s += 2;
		}
	} else {
		u16 *s = (u16 *)src;
		volatile u16 *d = (unsigned short *)dest;
		size >>= 1;
		while (size--)
			*d++ = *s++;
	}
}

/* Note the use of non-standard return values (0=match, 1=no-match) */
static inline int _memcmp(u8 *dest, u8 *src, int size)
{
#if 1
	while (size--)
		if (*dest++ != *src++)
			return 1;
	return 0;
#else
	int i = 0;
	for (i = 0 ; i < size ; i++)
		if (*(dest + i) != *(src + i))
			return 1;
	return 0;
#endif
}

#if 0
void dpram_platform_init(void)
{

	/* LTE-STEALTH Related config. CS related settings are done in Machine Init*/
	unsigned int regVal;

	/* SINGALS
	   1) C110_DPRAM_nCS --> XM0CSN_3  (ie Xm0CSn[3] MP0_1[3])
	   2) C110_OE_N -->XM0OEN
	   3) C110_LB -> XM0BEN_0
	   4) C110_UB --> XM0BEN_1
	   5) C110_DPRAM_INT_N --> XEINT_8
	   6) C110_WE_N --> XM0WEN
	   7) DATA LINES --> XM0DATA_0 to XM0DATA_15
	   8) Address Lines -->XM0ADDR_0 to XM0ADDR_12 */

	//ADDR LINES //0xE0200340  and 0xE0200360
	regVal = 0x22222222;
	writel(regVal, S5PV210_GPA0_BASE + 0x0340);

	regVal = __raw_readl(S5PV210_GPA0_BASE + 0x0360);
	regVal |= 0x00022222;
	writel(regVal, S5PV210_GPA0_BASE + 0x0360);

	//DATA LINES MP06 and MP07 //0xE0200380 and 0xE02003A0
	regVal = 0x22222222;
	writel(regVal, S5PV210_GPA0_BASE + 0x0380);

	regVal = 0x22222222;
	writel(regVal, S5PV210_GPA0_BASE + 0x03A0);

}
#endif

static inline int WRITE_TO_DPRAM_VERIFY(u32 dest, void *src, int size)
{
	int cnt = 3;

	while (cnt--) {
		_memcpy((void *)(DPRAM_VBASE + dest), (void *)src, size);
		if (!_memcmp((u8 *)(DPRAM_VBASE + dest), (u8 *)src, size))
			return 0;
	}
	return -1;
}

static inline int READ_FROM_DPRAM_VERIFY(void *dest, u32 src, int size)
{
	int cnt = 3;
	while (cnt--) {
		_memcpy((void *)dest, (void *)(DPRAM_VBASE + src), size);
		if (!_memcmp((u8 *)dest, (u8 *)(DPRAM_VBASE + src), size))
			return 0;
	}
	return -1;
}

static void send_interrupt_to_phone(u16 irq_mask)
{
		u16 temp;
		READ_FROM_DPRAM(&temp, DPRAM_PDA2PHONE_INTERRUPT_ADDRESS,
				DPRAM_INTERRUPT_PORT_SIZE);

	if ((temp & INT_MASK_REQ_ACK_R) && is_net_stopped)
	{
		/* don't let the mailbox overwrite happen if we req for ACK_R */
		DPRAM_LOG_INFO ("<=== Setting the Interrupt Mask: %d\n", irq_mask);
		irq_mask |= INT_MASK_REQ_ACK_R;
	}

	WRITE_TO_DPRAM(DPRAM_PDA2PHONE_INTERRUPT_ADDRESS,
			&irq_mask, DPRAM_INTERRUPT_PORT_SIZE);
#ifdef PRINT_SEND_IRQ
	DPRAM_LOG_SEND_IRQ("=====> send IRQ: %x \n", irq_mask);
#endif
}

static int dpram_write_net(dpram_device_t *device, const unsigned char *buf, int len)
{
	int last_size = 0;
	u16 head, tail;

	READ_FROM_DPRAM_VERIFY(&head, device->out_head_addr, sizeof(head));
	READ_FROM_DPRAM_VERIFY(&tail, device->out_tail_addr, sizeof(tail));

	/* printk(KERN_ERR "%s, head: %d, tail: %d\n", __func__, head, tail); */
	if (head < tail) {
		/* +++++++++ head ---------- tail ++++++++++ */
		WRITE_TO_DPRAM(device->out_buff_addr + head, buf, len);
	} else {
		/* ------ tail +++++++++++ head ------------ */
		last_size = device->out_buff_size - head;
		WRITE_TO_DPRAM(device->out_buff_addr + head, buf, len > last_size ? last_size : len);
		if(len > last_size) {
			WRITE_TO_DPRAM(device->out_buff_addr, buf + last_size, len - last_size);
		}
	}

	/* update new head */
	head = (u16)((head + len) % device->out_buff_size);
	WRITE_TO_DPRAM_VERIFY(device->out_head_addr, &head, sizeof(head));

	device->out_head_saved = head;
	device->out_tail_saved = tail;

	return len;
}
static int dpram_write(dpram_device_t *device, const unsigned char *buf, int len)
{
	int last_size = 0;
	u16 head, tail;
	u16 irq_mask = 0;
	u16 magic, access;
	struct pdp_hdr *pdp_hdr_ptr = (struct pdp_hdr *)(buf + 1);
	int curr_pkt_len = 0, free_space = 0;

	/*  If the phone is down, let's reset everything and fail the write. */
	READ_FROM_DPRAM_VERIFY(&magic, DPRAM_MAGIC_CODE_ADDRESS, sizeof(magic));
	READ_FROM_DPRAM_VERIFY(&access, DPRAM_ACCESS_ENABLE_ADDRESS, sizeof(access));
	if (!dpram_phone_getstatus() || !access || magic != 0xAA) {
		/* We'll just call the dpram memory init again */
		dpram_initiate_self_error_correction();
		DPRAM_LOG_ERR("magic code is invalid, access %d, magic %d\n", access, magic);
		return -EINVAL;
	}

	//Do sanity tests here on the buf. 
	// Note: Formatted data don't have channel ID. As we are taking length from packet, this should be okie
	if (buf[0] != 0x7F) {
		DPRAM_LOG_ERR("%s: missing start of pkt 0x7F \n", __func__);
		return -EINVAL;
	}
	if (pdp_hdr_ptr->len > (len - 2)) {
		DPRAM_LOG_ERR("%s: Size mismatch. currPktLen=%d, len%d \n", __func__, pdp_hdr_ptr->len, len);
		return -EINVAL;
	}
	if (buf[pdp_hdr_ptr->len + 1] != 0x7E) {
		DPRAM_LOG_ERR("%s: missing end of pkt 0x7E \n", __func__);
		return -EINVAL;
	}

	READ_FROM_DPRAM_VERIFY(&head, device->out_head_addr, sizeof(head));
	READ_FROM_DPRAM_VERIFY(&tail, device->out_tail_addr, sizeof(tail));

	//Now try only writing the curr pkt
	curr_pkt_len = pdp_hdr_ptr->len + 2; // including SOF/EOF
	free_space = (head < tail) ? tail - head - 1 : device->out_buff_size + tail - head - 1;

	if (curr_pkt_len > free_space) {
		DPRAM_LOG_INFO( "WRITE: No space in Q, curr_pkt_len[%d] free_space[%d] head[%d] tail[%d]\n", curr_pkt_len, free_space, head, tail);
		return 0;
	}

	/* printk(KERN_ERR "%s, head: %d, tail: %d\n", __func__, head, tail); */
	if (head < tail) {
		/* +++++++++ head ---------- tail ++++++++++ */
		WRITE_TO_DPRAM(device->out_buff_addr + head, buf, len);
	} else {
		/* ------ tail +++++++++++ head ------------ */
		last_size = device->out_buff_size - head;
		WRITE_TO_DPRAM(device->out_buff_addr + head, buf, len > last_size ? last_size : len);
		if(len > last_size) {
			WRITE_TO_DPRAM(device->out_buff_addr, buf + last_size, len - last_size);
		}
	}

	/* update new head */
	head = (u16)((head + len) % device->out_buff_size);
	WRITE_TO_DPRAM_VERIFY(device->out_head_addr, &head, sizeof(head));

	device->out_head_saved = head;
	device->out_tail_saved = tail;

	/* @LDK@ send interrupt to the phone, if.. */
	irq_mask = INT_MASK_VALID;

	if (len > 0)
		irq_mask |= device->mask_send;

#if 0
	if (len > retval)
		irq_mask |= device->mask_req_ack;
#endif

	send_interrupt_to_phone(irq_mask);

	return len;
}

static inline int dpram_tty_insert_data(dpram_device_t *device, const u8 *psrc, u16 size)
{
#define CLUSTER_SEGMENT	1500
	u16 copied_size = 0;
	int retval = 0;
#ifdef PRINT_READ
	int i;
	DPRAM_LOG_READ("READ: %d\n", size);
	DPRAM_LOG_READ("WRITE: return: %d\n", retval);
	for (i = 0 ; i < size ; i++)
		DPRAM_LOG_READ("%02x ", *(psrc + i));
	DPRAM_LOG_READ("\n");
#endif
#ifdef PRINT_READ_SHORT
	DPRAM_LOG_READ_SHORT("READ: size:  %d\n", size);
#endif

	if (size > CLUSTER_SEGMENT && (device->serial.tty->index == 1)) {
		while (size) {
			copied_size = (size > CLUSTER_SEGMENT) ? CLUSTER_SEGMENT : size;
			tty_insert_flip_string(device->serial.tty, psrc + retval, copied_size);
			size -= copied_size;
			retval += copied_size;
		}
		return retval;
	}
	return tty_insert_flip_string(device->serial.tty, psrc, size);
}

static int dpram_read_fmt(dpram_device_t *device, const u16 non_cmd)
{
	int retval = 0;
	int retval_add = 0;
	int size = 0;
	u16 head, tail;

	READ_FROM_DPRAM_VERIFY(&head, device->in_head_addr, sizeof(head));
	READ_FROM_DPRAM_VERIFY(&tail, device->in_tail_addr, sizeof(tail));

	DPRAM_LOG_READ("[DPRAM] Reading formatted queue...\n");
	DPRAM_LOG_READ("=====> %s,  head: %d, tail: %d\n", __func__, head, tail);

	if (head != tail) {
		u16 up_tail = 0;

		// ------- tail ++++++++++++ head -------- //
		if (head > tail) {
			size = head - tail;
			retval = dpram_tty_insert_data(device, (u8 *)(DPRAM_VBASE + (device->in_buff_addr + tail)), size);
			if (size != retval)
				DPRAM_LOG_ERR("[DPRAM: size: %d, retval: %d\n", size, retval);
#ifdef PRINT_READ_SHORT
			else
				DPRAM_LOG_READ_SHORT("READ -return: %d\n", retval);
#endif
		} else {
			// +++++++ head ------------ tail ++++++++ //
			int tmp_size = 0;
			// Total Size.
			size = device->in_buff_size - tail + head;

			// 1. tail -> buffer end.
			tmp_size = device->in_buff_size - tail;
			retval = dpram_tty_insert_data(device, (u8 *)(DPRAM_VBASE + (device->in_buff_addr + tail)), tmp_size);
			if (tmp_size != retval)
				DPRAM_LOG_ERR("[DPRAM: size: %d, retval: %d\n", size, retval);
#ifdef PRINT_READ_SHORT
			else
				DPRAM_LOG_READ_SHORT("READ -return: %d\n", retval);
#endif

			// 2. buffer start -> head.
			if (size > tmp_size) {
				retval_add = dpram_tty_insert_data(device, (u8 *)(DPRAM_VBASE + device->in_buff_addr), size - tmp_size);
				retval += retval_add;
				if ((size - tmp_size) != retval_add)
					DPRAM_LOG_ERR("[DPRAM: size - tmp_size: %d, retval_add: %d\n", size - tmp_size, retval_add);
#ifdef PRINT_READ_SHORT
				else
					DPRAM_LOG_READ_SHORT("READ -return_add: %d\n", retval_add);
#endif
			}
		}
		/* update tail */
		up_tail = (u16)((tail + retval) % device->in_buff_size);
		WRITE_TO_DPRAM_VERIFY(device->in_tail_addr, &up_tail, sizeof(up_tail));
	}
	device->in_head_saved = head;
	device->in_tail_saved = tail;

	if(atomic_read(&fmt_txq_req_ack_rcvd) > 0) {
		// there is a situation where the q become full after we reached the tasklet.
		// so this logic will allow us to send the RES_ACK as soon as we read 1 packet and CP get a chance to
		// write another buffer.
		DPRAM_LOG_RECV_IRQ("sending INT_MASK_RESP_ACK_F\n");
		send_interrupt_to_phone(INT_NON_COMMAND(device->mask_res_ack));
		atomic_set(&fmt_txq_req_ack_rcvd, 0);
	} else {
		if (non_cmd & device->mask_req_ack)
			send_interrupt_to_phone(INT_NON_COMMAND(device->mask_res_ack));
	}
	return retval;
}

static int dpram_read_raw(dpram_device_t *device, const u16 non_cmd)
{
	int retval = 0;
	int size = 0;
	u16 head, tail;
	u16 up_tail = 0;
	int ret;
	size_t len;
	struct pdp_info *dev = NULL;
	struct pdp_hdr hdr;
	u16 read_offset = 0;
	u8 len_high, len_low, id, control;
	u16 pre_data_size; //pre_hdr_size,
	u8 ch;

	READ_FROM_DPRAM_VERIFY(&head, device->in_head_addr, sizeof(head));
	READ_FROM_DPRAM_VERIFY(&tail, device->in_tail_addr, sizeof(tail));

	if (head != tail) {
		up_tail = 0;

		if (head > tail)
			size = head - tail;				/* ----- (tail) 7f 00 00 7e (head) ----- */
		else
			size = device->in_buff_size - tail + head;	/* 00 7e (head) ----------- (tail) 7f 00 */

#ifdef PRINT_READ_SHORT
		DPRAM_LOG_READ_SHORT("RAW READ: head: %d, tail: %d, size: %d\n", head, tail, size);
#endif
		READ_FROM_DPRAM(&ch, device->in_buff_addr + ((u16)(tail + read_offset) % device->in_buff_size), sizeof(ch));

		if (ch == 0x7f) {
			read_offset++;
		} else {
			dpram_drop_data(device);
			return -1;
		}

		len_high = len_low = id = control = 0;
		READ_FROM_DPRAM(&len_low, device->in_buff_addr + ((u16)(tail + read_offset) % device->in_buff_size), sizeof(len_high));
		read_offset++;
		READ_FROM_DPRAM(&len_high, device->in_buff_addr + ((u16)(tail + read_offset) % device->in_buff_size), sizeof(len_low));
		read_offset++;
		READ_FROM_DPRAM(&id, device->in_buff_addr + ((u16)(tail + read_offset) % device->in_buff_size), sizeof(id));
		read_offset++;
		READ_FROM_DPRAM(&control, device->in_buff_addr + ((u16)(tail + read_offset) % device->in_buff_size), sizeof(control));
		read_offset++;

		hdr.len = len_high << 8 | len_low;
		hdr.id = id;
		hdr.control = control;

		len = hdr.len - sizeof(struct pdp_hdr);
		if (len <= 0) {
			DPRAM_LOG_ERR("READ RAW - wrong length, read_offset: %d, len: %d hdr.id: %d\n", read_offset, len, hdr.id);
			dpram_drop_data(device);
			return -1;
		}
		dev = pdp_get_dev(hdr.id);

#ifdef PRINT_READ_SHORT
		DPRAM_LOG_READ_SHORT("RAW READ: read_offset: %d, len: %d hdr.id: %d\n", read_offset, len, hdr.id);
#endif
		if (!dev) {
			DPRAM_LOG_ERR("onedram_raw_rx_data_callback : ch_id = %u, there is no existing device.\n", hdr.id);
			dpram_drop_data(device);
			return -1;
		}
		if (DEV_TYPE_SERIAL == dev->type) {
			if (dev->vs_dev.tty != NULL && dev->vs_dev.refcount) {
				if ((u16)(tail + read_offset) % device->in_buff_size + len < device->in_buff_size) {
					dpram_debug_dump_raw_read_buffer((u8 *)(DPRAM_VBASE + (device->in_buff_addr + (u16)(tail + read_offset) % device->in_buff_size)), len);
					ret = tty_insert_flip_string(dev->vs_dev.tty, (u8 *)(DPRAM_VBASE + (device->in_buff_addr + (u16)(tail + read_offset) % device->in_buff_size)), len);
					tty_flip_buffer_push(dev->vs_dev.tty);
				} else {
					pre_data_size = device->in_buff_size - (tail + read_offset);
					dpram_debug_dump_raw_read_buffer((u8 *)(DPRAM_VBASE + (device->in_buff_addr + tail + read_offset)), pre_data_size);
					ret = tty_insert_flip_string(dev->vs_dev.tty, (u8 *)(DPRAM_VBASE + (device->in_buff_addr + tail + read_offset)), pre_data_size);
					dpram_debug_dump_raw_read_buffer((u8 *)(DPRAM_VBASE + (device->in_buff_addr)), len - pre_data_size);
					ret += tty_insert_flip_string(dev->vs_dev.tty, (u8 *)(DPRAM_VBASE + (device->in_buff_addr)), len - pre_data_size);
					tty_flip_buffer_push(dev->vs_dev.tty);
					//LOGL(DL_DEBUG, "RAW pre_data_size: %d, len-pre_data_size: %d, ret: %d\n", pre_data_size, len- pre_data_size, ret);
				}
			} else {
				DPRAM_LOG_ERR("tty channel(id:%d) is not opened.\n", dev->id);
				ret = len;
			}

			if (!ret) {
				DPRAM_LOG_ERR("(tty_insert_flip_string) ch_id: %d, drop byte: %d\n buff addr: %x\n read addr: %x\n", hdr.id, size, (device->in_buff_addr), (device->in_buff_addr + ((u16)(tail + read_offset) % device->in_buff_size)));
				dpram_drop_data(device);
				return -1;
			}
		} else if (DEV_TYPE_NET == dev->type) {
			if ((u16)(tail + read_offset) % device->in_buff_size + len < device->in_buff_size) {
				dpram_debug_dump_raw_read_buffer((u8 *)(DPRAM_VBASE + (device->in_buff_addr + (u16)(tail + read_offset) % device->in_buff_size)), len);
				ret = vnet_recv_rx_data((u8 *)(DPRAM_VBASE + (device->in_buff_addr + (u16)(tail + read_offset) % device->in_buff_size)), len, dev);
			} else {
				/* data span in two area. copy to local buffer and push. */
				/* TODO - Opimization later - avoid local copy here */
				pre_data_size = device->in_buff_size - (tail + read_offset);
				memcpy(pdp_net_rx_buf, (u8 *)(DPRAM_VBASE + (device->in_buff_addr + tail + read_offset)), pre_data_size);
				memcpy(pdp_net_rx_buf + pre_data_size, (u8 *)(DPRAM_VBASE + (device->in_buff_addr)), len - pre_data_size);
				dpram_debug_dump_raw_read_buffer(pdp_net_rx_buf, len);
				ret = vnet_recv_rx_data(pdp_net_rx_buf, len, dev);
			}
			if (ret != len) {
				DPRAM_LOG_ERR("vnet_recv_rx_data dropping bytes \n");
				dpram_drop_data(device);
				return -1;
			}
		}

		read_offset += ret;
		//LOGL(DL_DEBUG,"read_offset: %d ret= %d\n", read_offset, ret);

		READ_FROM_DPRAM(&ch, (device->in_buff_addr + ((u16)(tail + read_offset) % device->in_buff_size)), sizeof(ch));
		if (ch == 0x7e) {
			read_offset++;
		} else {
			//LOGE("Last byte: %d, drop byte: %d\n buff addr: %x\n read addr: %x\n", ch, size, (device->in_buff_addr), (device->in_buff_addr + ((u16)(tail + read_offset) % device->in_buff_size)));
			dpram_drop_data(device);
			return -1;
		}

		size -= (ret + sizeof(struct pdp_hdr) + 2);
		retval += (ret + sizeof(struct pdp_hdr) + 2);

		//if (size < 0) {
		//LOGE("something wrong....\n");
		//	return -1;
		//}

		up_tail = (u16)((tail + read_offset) % device->in_buff_size);
		WRITE_TO_DPRAM_VERIFY(device->in_tail_addr, &up_tail, sizeof(up_tail));
	}

	device->in_head_saved = head;
	device->in_tail_saved = tail;

	if(atomic_read(&raw_txq_req_ack_rcvd) > 0) {
		// there is a situation where the q become full after we reached the tasklet.
		// so this logic will allow us to send the RES_ACK as soon as we read 1 packet and CP get a chance to
		// write another buffer.
		DPRAM_LOG_RECV_IRQ("sending INT_MASK_RESP_ACK_R\n");
		send_interrupt_to_phone(INT_NON_COMMAND(device->mask_res_ack));
		atomic_set(&raw_txq_req_ack_rcvd, 0);
	} else {
		if (non_cmd & device->mask_req_ack)
			send_interrupt_to_phone(INT_NON_COMMAND(device->mask_res_ack));
	}

	return retval;
}

#ifdef _ENABLE_ERROR_DEVICE
void request_phone_reset(unsigned long data)
{
	char buf[DPRAM_ERR_MSG_LEN];
	unsigned long flags;
	volatile int gpio_state;
	gpio_state = gpio_get_value(GPIO_PHONE_ACTIVE);

	if(!gpio_state) {
	memset((void *)buf, 0, sizeof(buf));

	memcpy(buf, "8 $PHONE-OFF", sizeof("8 $PHONE-OFF"));
	DPRAM_LOG_ERR("[PHONE ERROR] ->> %s\n", buf);

	local_irq_save(flags);
	memcpy(dpram_err_buf, buf, DPRAM_ERR_MSG_LEN);
	is_dpram_err = TRUE;
	local_irq_restore(flags);

	wake_up_interruptible(&dpram_err_wait_q);
	kill_fasync(&dpram_err_async_q, SIGIO, POLL_IN);
}
}
#endif

static int dpram_shared_bank_remap(void)
{
	dpram_base = ioremap_nocache(DPRAM_START_ADDRESS_PHYS + DPRAM_SHARED_BANK, DPRAM_SHARED_BANK_SIZE);
	if (dpram_base == NULL) {
		DPRAM_LOG_ERR("failed ioremap\n");
		return -ENOENT;
	}

	DPRAM_LOG_INFO("[DPRAM] ioremap returned %p\n", dpram_base);
	return 0;
}

static void dpram_clear(void)
{
	long i = 0, err = 0;
	unsigned long flags;

	DPRAM_LOG_INFO("[DPRAM] *** entering dpram_clear()\n");
	/* clear DPRAM except interrupt area */
	local_irq_save(flags);

	for (i = DPRAM_PDA2PHONE_FORMATTED_HEAD_ADDRESS;
			i < DPRAM_SIZE - (DPRAM_INTERRUPT_PORT_SIZE * 2);
			i += 2) {
		*((u16 *)(DPRAM_VBASE + i)) = i;
	}
	local_irq_restore(flags);

	for (i = DPRAM_PDA2PHONE_FORMATTED_HEAD_ADDRESS;
			i < DPRAM_SIZE - (DPRAM_INTERRUPT_PORT_SIZE * 2);
			i += 2) {
		if (*((u16 *)(DPRAM_VBASE + i)) != i) {
			DPRAM_LOG_ERR("[DPRAM] *** dpram_clear() verification failed at %8.8X\n", (((unsigned int)DPRAM_VBASE) + i));
			if (err++ > 128)
				break;
		}
	}

	local_irq_save(flags);
	for (i = DPRAM_PDA2PHONE_FORMATTED_HEAD_ADDRESS;
			i < DPRAM_SIZE - (DPRAM_INTERRUPT_PORT_SIZE * 2);
			i += 2) {
		*((u16 *)(DPRAM_VBASE + i)) = 0;
	}
	local_irq_restore(flags);

	DPRAM_LOG_INFO("[DPRAM] *** leaving dpram_clear()\n");
}

static int dpram_init_and_report(void)
{
	const u16 magic_code = 0x00aa;
	u16 ac_code = 0x0000;
	const u16 init_end = INT_COMMAND(INT_MASK_CMD_INIT_END);
	u16 magic, enable;

	/* @LDK@ write DPRAM disable code */
	WRITE_TO_DPRAM(DPRAM_ACCESS_ENABLE_ADDRESS, &ac_code, sizeof(ac_code));
	/* @LDK@ dpram clear */
	dpram_clear();
	/* @LDK@ write magic code */
	WRITE_TO_DPRAM(DPRAM_MAGIC_CODE_ADDRESS, &magic_code, sizeof(magic_code));
	/* @LDK@ write DPRAM enable code */
	ac_code = 0x0001;
	WRITE_TO_DPRAM(DPRAM_ACCESS_ENABLE_ADDRESS, &ac_code, sizeof(ac_code));

	/* @LDK@ send init end code to phone */
	send_interrupt_to_phone(init_end);
	READ_FROM_DPRAM((void *)&magic, DPRAM_MAGIC_CODE_ADDRESS, sizeof(magic));
	READ_FROM_DPRAM((void *)&enable, DPRAM_ACCESS_ENABLE_ADDRESS, sizeof(enable));
	printk(KERN_ERR "[DPRAM] magic code = %x, access enable = %x\n", magic, enable);
	printk(KERN_ERR "[DPRAM] Send 0x%x to MailboxBA  (Dpram init finish).\n", init_end);

	phone_sync = 1;
	return 0;
}

static inline int dpram_get_read_available(dpram_device_t *device)
{
	u16 head, tail;
	READ_FROM_DPRAM_VERIFY(&head, device->in_head_addr, sizeof(head));
	READ_FROM_DPRAM_VERIFY(&tail, device->in_tail_addr, sizeof(tail));
	/* printk(KERN_ERR "H: %d, T: %d, H-T: %d\n",head, tail, head-tail); */

	if (head < device->in_buff_size) {
		return head - tail;
	} else {
		dpram_drop_data(device);
		return 0;
	}
}


static void dpram_drop_data(dpram_device_t *device)
{
	u16 head, tail;
	READ_FROM_DPRAM_VERIFY(&head, device->in_head_addr, sizeof(head));
	READ_FROM_DPRAM_VERIFY(&tail, device->in_tail_addr, sizeof(tail));
	if (head >= device->in_buff_size || tail >= device->in_buff_size) {
		head = tail = 0;
		WRITE_TO_DPRAM_VERIFY(device->in_head_addr, &head, sizeof(head));
	}
	WRITE_TO_DPRAM_VERIFY(device->in_tail_addr, &head, sizeof(head));
	DPRAM_LOG_ERR("[DPRAM] %s, head: %d, tail: %d\n", __func__, head, tail);
}

static void dpram_phone_reset(void)
{
	gpio_set_value(GPIO_PHONE_RST_N, GPIO_LEVEL_LOW);
	mdelay(100);
	gpio_set_value(GPIO_PHONE_RST_N, GPIO_LEVEL_HIGH);
	DPRAM_LOG_INFO("dpram_phone_reset\n");  
}

static int dpram_phone_power_on(void)
{
    int RetVal = 0;
    int dpram_init_RetVal = 0;

    DPRAM_LOG_INFO("[DPRAM] dpram_phone_power_on using GPIO_PHONE_ON()\n");

    if(system_rev > 0x0A){
        gpio_set_value(GPIO_PHONE_RST_N, GPIO_LEVEL_LOW);
        gpio_set_value(GPIO_PHONE_ON, GPIO_LEVEL_HIGH);
        mdelay(300);
        gpio_set_value(GPIO_VIA_PS_HOLD_OFF, GPIO_LEVEL_HIGH);
        mdelay(300);
        gpio_set_value(GPIO_PHONE_ON, GPIO_LEVEL_LOW);
        gpio_set_value(GPIO_PHONE_RST_N, GPIO_LEVEL_HIGH);
    }
    else{
    gpio_set_value(GPIO_PHONE_RST_N, GPIO_LEVEL_LOW);
    gpio_set_value(GPIO_CP_ON_REV05, GPIO_LEVEL_HIGH);
    mdelay(300);
    gpio_set_value(GPIO_VIA_PS_HOLD_OFF, GPIO_LEVEL_HIGH);
    mdelay(300);
    gpio_set_value(GPIO_CP_ON_REV05, GPIO_LEVEL_LOW);
    gpio_set_value(GPIO_PHONE_RST_N, GPIO_LEVEL_HIGH);
    }


    /* We have a reset. Use that */

    //dpram_platform_init();

    /* Wait here until the PHONE is up. Waiting as the this called from IOCTL->UM thread */
    DPRAM_LOG_INFO("[DPRAM] power control waiting for INT_MASK_CMD_PIF_INIT_DONE\n");
    modem_pif_init_wait_condition = 0;
    dpram_init_cmd_wait_condition = 0;
    /* 1HZ = 1 clock tick, 100 default */
    ClearPendingInterruptFromModem();

    dpram_init_RetVal = wait_event_interruptible_timeout(dpram_init_cmd_wait_q, dpram_init_cmd_wait_condition, DPRAM_INIT_TIMEOUT);
    if (!dpram_init_RetVal) {
    /*RetVal will be 0 on timeout, non zero if interrupted */
    DPRAM_LOG_WARN("[DPRAM] INIT_START cmd was not arrived. dpram_init_cmd_wait_condition is 0 and wait timeout happend \n");
    return FALSE;
    }

    RetVal = wait_event_interruptible_timeout(modem_pif_init_done_wait_q, modem_pif_init_wait_condition, PIF_TIMEOUT);

    DPRAM_LOG_INFO("[DPRAM] wait_event_interruptible_timeout - done, modem_pif_init_wait_condition = %d\n", modem_pif_init_wait_condition);

    if (!RetVal) {
    /*RetVal will be 0 on timeout, non zero if interrupted */
    DPRAM_LOG_WARN("[DPRAM] PIF init failed, modem_pif_init_wait_condition is 0 and wait timeout happend \n");
    return FALSE;
    }
    return TRUE;
}

static void dpram_phone_power_off(void)
{
    gpio_set_value(GPIO_VIA_PS_HOLD_OFF, GPIO_LEVEL_LOW);
    mdelay(200);
    gpio_set_value(GPIO_VIA_PS_HOLD_OFF, GPIO_LEVEL_HIGH);

    DPRAM_LOG_ERR(" Phone power Off.\n");
}

static int dpram_phone_getstatus(void)
{
	return gpio_get_value(GPIO_PHONE_ACTIVE);
}

//Used only in debug mode
static void dpram_mem_rw(struct _mem_param *param)
{
	if (param->dir)
		WRITE_TO_DPRAM(param->addr, (void *)&param->data, sizeof(param->data));
	else
		READ_FROM_DPRAM((void *)&param->data, param->addr, sizeof(param->data));
}

static int dpram_phone_ramdump_on(void)
{
	const u16 rdump_flag1 = 0x554C;
	const u16 rdump_flag2 = 0x454D;

	DPRAM_LOG_ERR("[DPRAM] Ramdump ON.\n");
	WRITE_TO_DPRAM(DPRAM_MAGIC_CODE_ADDRESS,    &rdump_flag1, sizeof(rdump_flag1));
	WRITE_TO_DPRAM(DPRAM_ACCESS_ENABLE_ADDRESS, &rdump_flag2, sizeof(rdump_flag2));

	/* @LDK@ send init end code to phone */
	//usb_switch_mode(2);
	p3_set_usb_path(USB_SEL_CP_USB);
	dump_on = 1;

	dpram_phone_reset();

	return 0;
}

static int dpram_phone_ramdump_off(void)
{
	const u16 rdump_flag1 = 0x00aa;
	const u16 rdump_flag2 = 0x0001;
	//	const u16 temp1, temp2;

	DPRAM_LOG_ERR("[DPRAM] Ramdump OFF.\n");
	dump_on = 0;
	WRITE_TO_DPRAM(DPRAM_MAGIC_CODE_ADDRESS,    &rdump_flag1, sizeof(rdump_flag1));
	WRITE_TO_DPRAM(DPRAM_ACCESS_ENABLE_ADDRESS, &rdump_flag2, sizeof(rdump_flag2));

	/* @LDK@ send init end code to phone */
	//usb_switch_mode(1);
	p3_set_usb_path(USB_SEL_AP_USB);
	phone_sync = 0;
	dpram_phone_reset();

	return 0;
}

static void dpram_dump_state(void)
{
	u16 magic, enable;
	u16 fmt_in_head, fmt_in_tail, fmt_out_head, fmt_out_tail;
	u16 raw_in_head, raw_in_tail, raw_out_head, raw_out_tail;

	READ_FROM_DPRAM((void *)&magic, DPRAM_MAGIC_CODE_ADDRESS, sizeof(magic));
	READ_FROM_DPRAM((void *)&enable, DPRAM_ACCESS_ENABLE_ADDRESS, sizeof(enable));
	READ_FROM_DPRAM((void *)&fmt_in_head, DPRAM_PHONE2PDA_FORMATTED_HEAD_ADDRESS, sizeof(fmt_in_head));
	READ_FROM_DPRAM((void *)&fmt_in_tail, DPRAM_PHONE2PDA_FORMATTED_TAIL_ADDRESS, sizeof(fmt_in_tail));
	READ_FROM_DPRAM((void *)&fmt_out_head, DPRAM_PDA2PHONE_FORMATTED_HEAD_ADDRESS, sizeof(fmt_out_head));
	READ_FROM_DPRAM((void *)&fmt_out_tail, DPRAM_PDA2PHONE_FORMATTED_TAIL_ADDRESS, sizeof(fmt_out_tail));
	READ_FROM_DPRAM((void *)&raw_in_head, DPRAM_PHONE2PDA_RAW_HEAD_ADDRESS, sizeof(raw_in_head));
	READ_FROM_DPRAM((void *)&raw_in_tail, DPRAM_PHONE2PDA_RAW_TAIL_ADDRESS, sizeof(raw_in_tail));
	READ_FROM_DPRAM((void *)&raw_out_head, DPRAM_PDA2PHONE_RAW_HEAD_ADDRESS, sizeof(raw_out_head));
	READ_FROM_DPRAM((void *)&raw_out_tail, DPRAM_PDA2PHONE_RAW_TAIL_ADDRESS, sizeof(raw_out_tail));

	DPRAM_LOG_ERR("magic  = 0x%x\n", magic);
	DPRAM_LOG_ERR("enable  = 0x%x\n", enable);
	DPRAM_LOG_ERR("fmt_in_head  = 0x%x\n", fmt_in_head);
	DPRAM_LOG_ERR("fmt_in_tail  = 0x%x\n", fmt_in_tail);
	DPRAM_LOG_ERR("fmt_out_head  = 0x%x\n", fmt_out_head);
	DPRAM_LOG_ERR("fmt_out_tail  = 0x%x\n", fmt_out_tail);
	DPRAM_LOG_ERR("raw_in_head  = 0x%x\n", raw_in_head);
	DPRAM_LOG_ERR("raw_in_tail  = 0x%x\n", raw_in_tail);
	DPRAM_LOG_ERR("raw_out_head  = 0x%x\n", raw_out_head);
	DPRAM_LOG_ERR("raw_out_tail  = 0x%x\n", raw_out_tail);
}

static ssize_t show_debug(struct device *d, struct device_attribute *attr, char *buf)
{
	int inbuf, outbuf;
	char *p = buf;

	u16 magic, enable;
	u16 fmt_in_head, fmt_in_tail, fmt_out_head, fmt_out_tail;
	u16 raw_in_head, raw_in_tail, raw_out_head, raw_out_tail;

	READ_FROM_DPRAM((void *)&magic, DPRAM_MAGIC_CODE_ADDRESS, sizeof(magic));
	READ_FROM_DPRAM((void *)&enable, DPRAM_ACCESS_ENABLE_ADDRESS, sizeof(enable));
	READ_FROM_DPRAM((void *)&fmt_in_head, DPRAM_PHONE2PDA_FORMATTED_HEAD_ADDRESS, sizeof(fmt_in_head));
	READ_FROM_DPRAM((void *)&fmt_in_tail, DPRAM_PHONE2PDA_FORMATTED_TAIL_ADDRESS, sizeof(fmt_in_tail));
	READ_FROM_DPRAM((void *)&fmt_out_head, DPRAM_PDA2PHONE_FORMATTED_HEAD_ADDRESS, sizeof(fmt_out_head));
	READ_FROM_DPRAM((void *)&fmt_out_tail, DPRAM_PDA2PHONE_FORMATTED_TAIL_ADDRESS, sizeof(fmt_out_tail));
	READ_FROM_DPRAM((void *)&raw_in_head, DPRAM_PHONE2PDA_RAW_HEAD_ADDRESS, sizeof(raw_in_head));
	READ_FROM_DPRAM((void *)&raw_in_tail, DPRAM_PHONE2PDA_RAW_TAIL_ADDRESS, sizeof(raw_in_tail));
	READ_FROM_DPRAM((void *)&raw_out_head, DPRAM_PDA2PHONE_RAW_HEAD_ADDRESS, sizeof(raw_out_head));
	READ_FROM_DPRAM((void *)&raw_out_tail, DPRAM_PDA2PHONE_RAW_TAIL_ADDRESS, sizeof(raw_out_tail));

	inbuf = CIRC_CNT(fmt_in_head, fmt_in_tail, dpram_table[0].in_buff_size);
	outbuf = CIRC_CNT(fmt_out_head, fmt_out_tail, dpram_table[0].out_buff_size);
	p += sprintf(p, "%d\tSize\t%8u(in)\t%8u(out)\n"
			"\tIn\t%8u\t%8u\t%8u\n\tOut\t%8u\t%8u\t%8u\n",
			0, dpram_table[0].in_buff_size, dpram_table[0].out_buff_size,
			fmt_in_head, fmt_in_tail, inbuf,
			fmt_out_head, fmt_out_tail, outbuf);

	inbuf = CIRC_CNT(raw_in_head, raw_in_tail, dpram_table[1].in_buff_size);
	outbuf = CIRC_CNT(raw_out_head, raw_out_tail, dpram_table[1].out_buff_size);
	p += sprintf(p, "%d\tSize\t%8u(in)\t%8u(out)\n"
			"\tIn\t%8u\t%8u\t%8u\n\tOut\t%8u\t%8u\t%8u\n",
			1, dpram_table[1].in_buff_size, dpram_table[1].out_buff_size,
			raw_in_head, raw_in_tail, inbuf,
			raw_out_head, raw_out_tail, outbuf);

	return p - buf;

}
static DEVICE_ATTR(debug, S_IRUGO | S_IWUSR, show_debug, NULL);

#ifdef CONFIG_PROC_FS
static int dpram_read_proc(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	char *p = page;
	int len;
	u16 magic, enable;
	u16 fmt_in_head, fmt_in_tail, fmt_out_head, fmt_out_tail;
	u16 raw_in_head, raw_in_tail, raw_out_head, raw_out_tail;
	u16 in_interrupt = 0, out_interrupt = 0;
	int fih, fit, foh, fot;
	int rih, rit, roh, rot;

#ifdef _ENABLE_ERROR_DEVICE
	char buf[DPRAM_ERR_MSG_LEN];
	unsigned long flags;
#endif	/* _ENABLE_ERROR_DEVICE */

	READ_FROM_DPRAM((void *)&magic, DPRAM_MAGIC_CODE_ADDRESS, sizeof(magic));
	READ_FROM_DPRAM((void *)&enable, DPRAM_ACCESS_ENABLE_ADDRESS, sizeof(enable));
	READ_FROM_DPRAM((void *)&fmt_in_head, DPRAM_PHONE2PDA_FORMATTED_HEAD_ADDRESS, sizeof(fmt_in_head));
	READ_FROM_DPRAM((void *)&fmt_in_tail, DPRAM_PHONE2PDA_FORMATTED_TAIL_ADDRESS, sizeof(fmt_in_tail));
	READ_FROM_DPRAM((void *)&fmt_out_head, DPRAM_PDA2PHONE_FORMATTED_HEAD_ADDRESS, sizeof(fmt_out_head));
	READ_FROM_DPRAM((void *)&fmt_out_tail, DPRAM_PDA2PHONE_FORMATTED_TAIL_ADDRESS, sizeof(fmt_out_tail));
	READ_FROM_DPRAM((void *)&raw_in_head, DPRAM_PHONE2PDA_RAW_HEAD_ADDRESS, sizeof(raw_in_head));
	READ_FROM_DPRAM((void *)&raw_in_tail, DPRAM_PHONE2PDA_RAW_TAIL_ADDRESS, sizeof(raw_in_tail));
	READ_FROM_DPRAM((void *)&raw_out_head, DPRAM_PDA2PHONE_RAW_HEAD_ADDRESS, sizeof(raw_out_head));
	READ_FROM_DPRAM((void *)&raw_out_tail, DPRAM_PDA2PHONE_RAW_TAIL_ADDRESS, sizeof(raw_out_tail));


	fih = dpram_table[FORMATTED_INDEX].in_head_saved;
	fit = dpram_table[FORMATTED_INDEX].in_tail_saved;
	foh = dpram_table[FORMATTED_INDEX].out_head_saved;
	fot = dpram_table[FORMATTED_INDEX].out_tail_saved;
	rih = dpram_table[RAW_INDEX].in_head_saved;
	rit = dpram_table[RAW_INDEX].in_tail_saved;
	roh = dpram_table[RAW_INDEX].out_head_saved;
	rot = dpram_table[RAW_INDEX].out_tail_saved;

	READ_FROM_DPRAM((void *)&out_interrupt, DPRAM_PDA2PHONE_INTERRUPT_ADDRESS, sizeof(out_interrupt));
	READ_FROM_DPRAM((void *)&in_interrupt, DPRAM_PHONE2PDA_INTERRUPT_ADDRESS, sizeof(in_interrupt));

#ifdef _ENABLE_ERROR_DEVICE
	memset((void *)buf, '\0', DPRAM_ERR_MSG_LEN);
	local_irq_save(flags);
	memcpy(buf, dpram_err_buf, DPRAM_ERR_MSG_LEN - 1);
	local_irq_restore(flags);
#endif	/* _ENABLE_ERROR_DEVICE */
	p += sprintf(p,
			"-------------------------------------\n"
			"| NAME\t\t\t| VALUE\n"
			"-------------------------------------\n"
			"|R MAGIC CODE\t\t| 0x%04x\n"
			"|R ENABLE CODE\t\t| 0x%04x\n"
			"|R PHONE->PDA FMT HEAD\t| %u\n"
			"|R PHONE->PDA FMT TAIL\t| %u\n"
			"|R PDA->PHONE FMT HEAD\t| %u\n"
			"|R PDA->PHONE FMT TAIL\t| %u\n"
			"|R PHONE->PDA RAW HEAD\t| %u\n"
			"|R RPHONE->PDA RAW TAIL\t| %u\n"
			"|R PDA->PHONE RAW HEAD\t| %u\n"
			"|R PDA->PHONE RAW TAIL\t| %u\n"

			"| FMT PHONE->PDA HEAD\t| %d\n"
			"| FMT PHONE->PDA TAIL\t| %d\n"
			"| FMT PDA->PHONE HEAD\t| %d\n"
			"| FMT PDA->PHONE TAIL\t| %d\n"
			"-------------------------------------\n"

			"| RAW PHONE->PDA HEAD\t| %d\n"
			"| RAW PHONE->PDA TAIL\t| %d\n"
			"| RAW PDA->PHONE HEAD\t| %d\n"
			"| RAW PDA->PHONE TAIL\t| %d\n"

			"-------------------------------------\n"

			"| PHONE->PDA IRQREG\t| 0x%04x\n"
			"| PDA->PHONE IRQREG\t| 0x%04x\n"

			"-------------------------------------\n"

#ifdef _ENABLE_ERROR_DEVICE
			"| LAST PHONE ERR MSG\t| %s\n"
#endif	/* _ENABLE_ERROR_DEVICE */

			"| PHONE ACTIVE\t\t| %s\n"
			"| DPRAM INT Level\t| %d\n"
			"-------------------------------------\n",
		magic, enable,
		fmt_in_head, fmt_in_tail, fmt_out_head, fmt_out_tail,
		raw_in_head, raw_in_tail, raw_out_head, raw_out_tail,
		fih, fit, foh, fot,
		rih, rit, roh, rot,
		in_interrupt, out_interrupt,
#ifdef _ENABLE_ERROR_DEVICE
		(buf[0] != '\0' ? buf : "NONE"),
#endif	/* _ENABLE_ERROR_DEVICE */
		(dpram_phone_getstatus() ? "ACTIVE" : "INACTIVE"),
		gpio_get_value(GPIO_DPRAM_INT_N));
	len = (p - page) - off;
	if (len < 0)
		len = 0;

	*eof = (len <= count) ? 1 : 0;
	*start = page + off;

	return len;
}
#endif /* CONFIG_PROC_FS */

/* dpram tty file operations. */
static int dpram_tty_open(struct tty_struct *tty, struct file *file)
{
	dpram_device_t *device = &dpram_table[tty->index];
	device->serial.tty = tty;
	device->serial.open_count++;
	DPRAM_LOG_INFO("[%s] %s opend\n", __func__, tty->name);

	if (device->serial.open_count > 1) {
		device->serial.open_count--;
		return -EBUSY;
	}

	tty->driver_data = (void *)device;
	tty->low_latency = 1;

	return 0;
}

static void dpram_tty_close(struct tty_struct *tty, struct file *file)
{
	dpram_device_t *device = (dpram_device_t *)tty->driver_data;
	if (device && (device == &dpram_table[tty->index])) {
		down(&device->serial.sem);
		device->serial.open_count--;
		device->serial.tty = NULL;
		up(&device->serial.sem);
	}
}

static int dpram_tty_write(struct tty_struct *tty,
		const unsigned char *buffer, int count)
{
	dpram_device_t *device = (dpram_device_t *)tty->driver_data;
	if (!device)
		return 0;

	return dpram_write(device, buffer, count);
}

static int dpram_tty_write_room(struct tty_struct *tty)
{
	int avail;
	u16 head, tail;

	dpram_device_t *device = (dpram_device_t *)tty->driver_data;
	if (device != NULL) {
		head = device->out_head_saved;
		tail = device->out_tail_saved;
		avail = (head < tail) ? tail - head - 1 :
			device->out_buff_size + tail - head - 1;

		DPRAM_LOG_WRITE_SHORT("[DPRAM] %s: returns avail=%d\n", __func__, avail);

		return avail;
	}
	return 0;
}

static int dpram_tty_ioctl(struct tty_struct *tty, struct file *file,
		unsigned int cmd, unsigned long arg)
{
	unsigned int val;

	printk(KERN_ERR "[%s] 0x%x\n", __func__, cmd);

	switch (cmd) {
		case DPRAM_TEST:
			TestIOCTLHandler();
			return 0;

		case DPRAM_PHONE_ON:
			/* This IOCTL will block until the PHONE gets ready */
			phone_sync = 0;
			dump_on = 0;
			if (!dpram_phone_power_on()) {
				DPRAM_LOG_ERR("dpram_phone_power_on failed\n");
				return -EAGAIN;
			}
			return 0;

		case DPRAM_PHONE_GETSTATUS:
			val = dpram_phone_getstatus();
			return copy_to_user((unsigned int *)arg, &val, sizeof(val));

		case DPRAM_PHONE_RESET:
			/* After reset, we will get PHONE_ON from UM */
			phone_sync = 0;
			dpram_phone_reset();
			return 0;

		case DPRAM_PHONE_OFF:
			dpram_phone_power_off();
			return 0;

		case DPRAM_PHONE_RAMDUMP_ON:
			dpram_phone_ramdump_on();
			return 0;

		case DPRAM_PHONE_RAMDUMP_OFF:
			dpram_phone_ramdump_off();
			return 0;

		case DPRAM_EXTRA_MEM_RW:
			/* do nothing here. Just a dummy call in Via6410 */
			return 0;

		case DPRAM_MEM_RW: 
			{
				struct _mem_param param;
				copy_from_user((void *)&param, (void *)arg, sizeof(param));
				dpram_mem_rw(&param);
				if (!param.dir)
					return copy_to_user((unsigned long *)arg, &param, sizeof(param));
				return 0;
			}
		default:
			break;
	}
	return -ENOIOCTLCMD;
}

static int dpram_tty_chars_in_buffer(struct tty_struct *tty)
{
	int data;
	u16 head, tail;

	dpram_device_t *device = (dpram_device_t *)tty->driver_data;

	if (device != NULL) {
		head = device->out_head_saved;
		tail = device->out_tail_saved;
		data = (head > tail) ? head - tail - 1 :	device->out_buff_size - tail + head;

		DPRAM_LOG_WRITE_SHORT("[DPRAM] %s: returns data=%d\n", __func__, data);

		return data;
	}
	return 0;
}

#ifdef _ENABLE_ERROR_DEVICE
static int dpram_err_read(struct file *filp, char *buf, size_t count, loff_t *ppos)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	ssize_t ret;

	add_wait_queue(&dpram_err_wait_q, &wait);
	set_current_state(TASK_INTERRUPTIBLE);

	while (1) {
		local_irq_save(flags);

		if (is_dpram_err) {
			if (copy_to_user(buf, dpram_err_buf, count)) {
				ret = -EFAULT;
			} else {
				printk(KERN_ERR "[DPRAM] upload to RIL : %s\n", buf);
				ret = count;
			}
			is_dpram_err = FALSE;
			local_irq_restore(flags);
			break;
		}
		local_irq_restore(flags);
		if (filp->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			break;
		}

		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
		schedule();
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&dpram_err_wait_q, &wait);

	return ret;
}

static int dpram_err_fasync(int fd, struct file *filp, int mode)
{
	return fasync_helper(fd, filp, mode, &dpram_err_async_q);
}

static unsigned int dpram_err_poll(struct file *filp,
		struct poll_table_struct *wait)
{
	poll_wait(filp, &dpram_err_wait_q, wait);
	if(is_dpram_err) {
		printk(KERN_ERR "err poll success : %s\n", dpram_err_buf);
		return POLLIN | POLLRDNORM;
	}
	else {
		return 0;
	}
//	return ((is_dpram_err) ? (POLLIN | POLLRDNORM) : 0);
}
#endif	/* _ENABLE_ERROR_DEVICE */

/* handlers. */
static void res_ack_tasklet_handler(unsigned long data)
{
	dpram_device_t *device = (dpram_device_t *)data;

	/* TODO: check this case */
	u16 magic, access;
	READ_FROM_DPRAM_VERIFY(&magic, DPRAM_MAGIC_CODE_ADDRESS, sizeof(magic));
	READ_FROM_DPRAM_VERIFY(&access, DPRAM_ACCESS_ENABLE_ADDRESS, sizeof(access));
	if (!dpram_phone_getstatus() || !access || magic != 0xAA) {
		dpram_initiate_self_error_correction();
		return;
	}

	if (device && device->serial.tty) {
		struct tty_struct *tty = device->serial.tty;

		if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) && tty->ldisc->ops->write_wakeup)
			(tty->ldisc->ops->write_wakeup)(tty);

		wake_up_interruptible(&tty->write_wait);
	}
}

static void fmt_rcv_tasklet_handler(unsigned long data)
{
	dpram_tasklet_data_t *tasklet_data = (dpram_tasklet_data_t *)data;

	dpram_device_t *device = tasklet_data->device;
	u16 non_cmd = tasklet_data->non_cmd;

	int ret = 0;
	int cnt = 0;

	/* TODO: check this case */
	u16 magic, access;
	READ_FROM_DPRAM_VERIFY(&magic, DPRAM_MAGIC_CODE_ADDRESS, sizeof(magic));
	READ_FROM_DPRAM_VERIFY(&access, DPRAM_ACCESS_ENABLE_ADDRESS, sizeof(access));
	if (!dpram_phone_getstatus() || !access || magic != 0xAA) {
		dpram_initiate_self_error_correction();
		return;
	}

	if (device && device->serial.tty) {
		struct tty_struct *tty = device->serial.tty;

		while (dpram_get_read_available(device)) {
			ret = dpram_read_fmt(device, non_cmd);

			if (!ret)
				cnt++;

			if (cnt > 10) {
				dpram_drop_data(device);
				break;
			}
			if (ret < 0) {
				DPRAM_LOG_ERR("%s, dpram_read_fmt failed\n", __func__);
				/* TODO: ... wrong.. */
			}
			tty_flip_buffer_push(tty);
		}
	} else {
		DPRAM_LOG_ERR("[DPRAM] fmt_rcv_tasklet_handler(): Dropping data(2)...\n");
		dpram_drop_data(device);
	}

	DPRAM_LOG_READ_SHORT("[DPRAM] Leaving fmt_rcv_tasklet_handler()\n");
}

static void raw_rcv_tasklet_handler(unsigned long data)
{
	dpram_tasklet_data_t *tasklet_data = (dpram_tasklet_data_t *)data;

	dpram_device_t *device = tasklet_data->device;
	u16 non_cmd = tasklet_data->non_cmd;

	int ret = 0;

	//printk("[HANG]enter raw_rcv \n");
	/* TODO: check this case */
	u16 magic, access;
	READ_FROM_DPRAM_VERIFY(&magic, DPRAM_MAGIC_CODE_ADDRESS, sizeof(magic));
	READ_FROM_DPRAM_VERIFY(&access, DPRAM_ACCESS_ENABLE_ADDRESS, sizeof(access));
	if (!dpram_phone_getstatus() || !access || magic != 0xAA) {
		dpram_initiate_self_error_correction();
		return;
	}

	while (dpram_get_read_available(device)) {
		ret = dpram_read_raw(device, non_cmd);
		if (ret < 0) {
			//LOGE("RAW dpram_read_raw failed\n");
			/* TODO: ... wrong.. */
		}
	}

	//printk("[HANG]leave raw_rcv \n");
}

static void cmd_req_active_handler(void)
{
	send_interrupt_to_phone(INT_COMMAND(INT_MASK_CMD_RES_ACTIVE));
}

static unsigned char cpdump_debug_file_name[DPRAM_ERR_MSG_LEN] = "CDMA Crash";


static void cmd_error_display_handler_work(void)
{
#ifdef _ENABLE_ERROR_DEVICE
	char buf[DPRAM_ERR_MSG_LEN];
	unsigned long flags;

	memset((void *)buf, 0, sizeof(buf));

	if (dpram_phone_getstatus()) {
		READ_FROM_DPRAM(cpdump_debug_file_name, DPRAM_PHONE2PDA_FORMATTED_BUFFER_ADDRESS
				, sizeof(cpdump_debug_file_name));
		kernel_sec_set_cause_strptr(cpdump_debug_file_name, sizeof(cpdump_debug_file_name));
		if(kernel_sec_get_debug_level() != KERNEL_SEC_DEBUG_LEVEL_LOW) {
			t_kernel_sec_mmu_info mmu_info;

			memcpy(buf, "CDMA ", 5);
			READ_FROM_DPRAM((buf + 5), DPRAM_PHONE2PDA_FORMATTED_BUFFER_ADDRESS, sizeof (buf) - 6);

			local_irq_save(flags);

			printk("[kernel_sec_dump_cp_handle2] : Configure to restart AP and collect dump on restart...\n");
			printk(KERN_ERR "debug : %s\n", cpdump_debug_file_name);
			//kernel_sec_set_upload_magic_number();
			//kernel_sec_set_upload_cause(UPLOAD_CAUSE_CP_ERROR_FATAL);
			//kernel_sec_get_mmu_reg_dump(&mmu_info);
			//kernel_sec_hw_reset(false);

        		pr_err("[debug] CP crash upload mode!!!!\n");
        		dump_all_task_info();
        		dump_cpu_stat();
        		mdelay(1000);
        		dump_cpu_stat();		
        		panic("CP Crash");

			local_irq_restore(flags);
		}
		else {
			printk(KERN_ERR "debug : %s\n", cpdump_debug_file_name);
			memcpy((void *)buf, "9 $PHONE-RESET", sizeof("9 $PHONE-RESET"));
		}
	} else {
		// --- can't catch the CDMA watchdog reset!!
		memcpy((void *)buf, "8 $PHONE-OFF", sizeof("8 $PHONE-OFF"));
	}
	memcpy(dpram_err_buf, buf, DPRAM_ERR_MSG_LEN);
	is_dpram_err = TRUE;
	wake_up_interruptible(&dpram_err_wait_q);
	kill_fasync(&dpram_err_async_q, SIGIO, POLL_IN);
#endif	/* _ENABLE_ERROR_DEVICE */
}

static void cmd_error_display_handler(void) {
	if (!work_pending(&cp_crash_work))
	{
		PREPARE_WORK(&cp_crash_work, cmd_error_display_handler_work);
		schedule_work(&cp_crash_work);
	}
}


static void cmd_phone_start_handler(void)
{
	DPRAM_LOG_ERR("[DPRAM] Received 0xc8 from Phone (Phone Boot OK).\n");
	dpram_init_cmd_wait_condition = 1;
	wake_up_interruptible(&dpram_init_cmd_wait_q);
	dpram_init_and_report(); /* TODO init always now. otherwise starting/stopping modem code on debugger don't make this happen */
}

static void cmd_req_time_sync_handler(void)
{
	/* TODO: add your codes here.. */
}

static void cmd_phone_deep_sleep_handler(void)
{
	/* TODO: add your codes here.. */
}

static void cmd_nv_rebuilding_handler(void)
{
	memcpy(dpram_err_buf, "NV_REBUILDING", 14);
	is_dpram_err = TRUE;

	wake_up_interruptible(&dpram_err_wait_q);
	kill_fasync(&dpram_err_async_q, SIGIO, POLL_IN);
}

static void cmd_silent_nv_rebuilding_handler(void)
{
	memcpy(dpram_err_buf, "SILENT_NV_REBUILDING", 21);
	is_dpram_err = TRUE;

	wake_up_interruptible(&dpram_err_wait_q);
	kill_fasync(&dpram_err_async_q, SIGIO, POLL_IN);
}

static void cmd_emer_down_handler(void)
{
	/* TODO: add your codes here.. */
}

static void cmd_pif_init_done_handler(void)
{
	DPRAM_LOG_INFO("[DPRAM] cmd_pif_init_done_handler\n");
	if (&modem_pif_init_done_wait_q == NULL) {
		DPRAM_LOG_ERR("[DPRAM] Error - modem_pif_init_done_wait_q is NULL \n");
		return;
	}
	modem_pif_init_wait_condition = 1;
	wake_up_interruptible(&modem_pif_init_done_wait_q);
}

static void command_handler(u16 cmd)
{
	DPRAM_LOG_WARN("[DPRAM] Entering command_handler(0x%04X)\n", cmd);
	switch (cmd) {
		case INT_MASK_CMD_REQ_ACTIVE:
			cmd_req_active_handler();
			break;

		case INT_MASK_CMD_ERR_DISPLAY:
			//TODO: add debug:
			cmd_error_display_handler();
			break;

		case INT_MASK_CMD_PHONE_START:
			cmd_phone_start_handler();
			break;
			/* TODO: not implemented */
		case INT_MASK_CMD_REQ_TIME_SYNC:
			cmd_req_time_sync_handler();
			break;

		case INT_MASK_CMD_PHONE_DEEP_SLEEP:
			cmd_phone_deep_sleep_handler();
			break;

		case INT_MASK_CMD_NV_REBUILDING:
			cmd_nv_rebuilding_handler();
			break;

		case INT_MASK_CMD_EMER_DOWN:
			cmd_emer_down_handler();
			break;
			//
		case INT_MASK_CMD_PIF_INIT_DONE:
			cmd_pif_init_done_handler();
			break;

		case INT_MASK_CMD_SILENT_NV_REBUILDING:
			cmd_silent_nv_rebuilding_handler();
			break;

		default:
			DPRAM_LOG_ERR("Unknown command.. %x\n", cmd);
	}
	DPRAM_LOG_INFO("[DPRAM] Leaving command_handler(0x%04X)\n", cmd);
}

static void non_command_handler(u16 non_cmd)
{
	u16 head, tail;
	/* @LDK@ formatted check. */

	READ_FROM_DPRAM_VERIFY(&head, DPRAM_PHONE2PDA_FORMATTED_HEAD_ADDRESS, sizeof(head));
	READ_FROM_DPRAM_VERIFY(&tail, DPRAM_PHONE2PDA_FORMATTED_TAIL_ADDRESS, sizeof(tail));

	if (head != tail) {
		non_cmd |= INT_MASK_SEND_F;
	} else {
		if (non_cmd & INT_MASK_REQ_ACK_F)
			DPRAM_LOG_INFO("=====> FMT DATA EMPTY & REQ_ACK_F\n");
	}

	/* @LDK@ raw check. */
	READ_FROM_DPRAM_VERIFY(&head, DPRAM_PHONE2PDA_RAW_HEAD_ADDRESS, sizeof(head));
	READ_FROM_DPRAM_VERIFY(&tail, DPRAM_PHONE2PDA_RAW_TAIL_ADDRESS, sizeof(tail));

	if (head != tail) {
		non_cmd |= INT_MASK_SEND_R;
	} else {
		if (non_cmd & INT_MASK_REQ_ACK_R)
			DPRAM_LOG_INFO("=====> RAW DATA EMPTY & REQ_ACK_R\n");
	}

	/* @LDK@ +++ scheduling.. +++ */
	if (non_cmd & INT_MASK_SEND_F) {
		wake_lock_timeout(&dpram_wake_lock, HZ/2);
		dpram_tasklet_data[FORMATTED_INDEX].device = &dpram_table[FORMATTED_INDEX];
		dpram_tasklet_data[FORMATTED_INDEX].non_cmd = non_cmd;
		fmt_send_tasklet.data = (unsigned long)&dpram_tasklet_data[FORMATTED_INDEX];
		tasklet_schedule(&fmt_send_tasklet);
	}

	if (non_cmd & INT_MASK_SEND_R) {
		wake_lock_timeout(&dpram_wake_lock, HZ*2);
		dpram_tasklet_data[RAW_INDEX].device = &dpram_table[RAW_INDEX];
		dpram_tasklet_data[RAW_INDEX].non_cmd = non_cmd;
		raw_send_tasklet.data = (unsigned long)&dpram_tasklet_data[RAW_INDEX];
		//printk("[HANG] sch raw tasklet \n");
		tasklet_schedule(&raw_send_tasklet);
	}

	if (non_cmd & INT_MASK_REQ_ACK_R) {
		DPRAM_LOG_RECV_IRQ("received INT_MASK_REQ_ACK_R\n");
		atomic_inc(&raw_txq_req_ack_rcvd);
	}

	if (non_cmd & INT_MASK_REQ_ACK_F) {
		DPRAM_LOG_RECV_IRQ("received INT_MASK_REQ_ACK_F\n");
		atomic_inc(&fmt_txq_req_ack_rcvd);
	}

	if (non_cmd & INT_MASK_RES_ACK_F) {
		wake_lock_timeout(&dpram_wake_lock, HZ/2);
		tasklet_schedule(&fmt_res_ack_tasklet);
	}

	if (non_cmd & INT_MASK_RES_ACK_R) {
		wake_lock_timeout(&dpram_wake_lock, HZ*2);
		tasklet_schedule(&raw_res_ack_tasklet);

		MULTIPDP_LOG_INFO("NON_CMD RES_ACK_R Interrupt ==> schedule write work item \n");
		if (!work_pending(&xmit_work_struct)) {
		PREPARE_WORK(&xmit_work_struct, pdp_mux_net);
		schedule_work(&xmit_work_struct);
		}

	} else {
		if (is_net_stopped)
		{
		   READ_FROM_DPRAM_VERIFY(&head, DPRAM_PDA2PHONE_RAW_HEAD_ADDRESS, sizeof(head));
		   READ_FROM_DPRAM_VERIFY(&tail, DPRAM_PDA2PHONE_RAW_TAIL_ADDRESS, sizeof(tail));
	
		   if (head == tail)
		   {

		       if (!work_pending(&xmit_work_struct)) {
		           /* we have space in out Q. So trigger the net interfaces */
			       PREPARE_WORK(&xmit_work_struct, pdp_mux_net);
			       schedule_work(&xmit_work_struct);
		       }
		   }
		}
	}
}

/* @LDK@ interrupt handlers. */
static irqreturn_t dpram_irq_handler(int irq, void *dev_id)
{
	u16 irq_mask = 0;

	READ_FROM_DPRAM_VERIFY(&irq_mask, DPRAM_PHONE2PDA_INTERRUPT_ADDRESS, sizeof(irq_mask));
	DPRAM_LOG_RECV_IRQ("received mailboxAB = 0x%x\n", irq_mask);

#if 1	// TODO:debug: print head tail
	u16 fih, fit, foh, fot;
	u16 rih, rit, roh, rot;

	READ_FROM_DPRAM_VERIFY(&fih, DPRAM_PHONE2PDA_FORMATTED_HEAD_ADDRESS, sizeof(fih));
	READ_FROM_DPRAM_VERIFY(&fit, DPRAM_PHONE2PDA_FORMATTED_TAIL_ADDRESS, sizeof(fit));
	READ_FROM_DPRAM_VERIFY(&foh, DPRAM_PDA2PHONE_FORMATTED_HEAD_ADDRESS, sizeof(foh));
	READ_FROM_DPRAM_VERIFY(&fot, DPRAM_PDA2PHONE_FORMATTED_TAIL_ADDRESS, sizeof(fot));
	READ_FROM_DPRAM_VERIFY(&rih, DPRAM_PHONE2PDA_RAW_HEAD_ADDRESS, sizeof(rih));
	READ_FROM_DPRAM_VERIFY(&rit, DPRAM_PHONE2PDA_RAW_TAIL_ADDRESS, sizeof(rit));
	READ_FROM_DPRAM_VERIFY(&roh, DPRAM_PDA2PHONE_RAW_HEAD_ADDRESS, sizeof(roh));
	READ_FROM_DPRAM_VERIFY(&rot, DPRAM_PDA2PHONE_RAW_TAIL_ADDRESS, sizeof(rot));
	DPRAM_LOG_HEAD_TAIL("\n fmt_in  H:%4d, T:%4d, M:%4d\n fmt_out H:%4d, T:%4d, M:%4d\n raw_in  H:%4d, T:%4d, M:%4d\n raw out H:%4d, T:%4d, M:%4d\n", fih, fit, DPRAM_PHONE2PDA_FORMATTED_BUFFER_SIZE, foh, fot, DPRAM_PDA2PHONE_FORMATTED_BUFFER_SIZE, rih, rit, DPRAM_PHONE2PDA_RAW_BUFFER_SIZE, roh, rot, DPRAM_PDA2PHONE_RAW_BUFFER_SIZE);
#endif
	// valid bit verification. @LDK@
	if (!(irq_mask & INT_MASK_VALID)) {
		DPRAM_LOG_ERR("Invalid interrupt mask: 0x%04x\n", irq_mask);
		DPRAM_LOG_ERR("[DPRAM] Leaving dpram_irq_handler()\n");
		return IRQ_HANDLED;
	}

	// Say something about the phone being dead...
	if (irq_mask == INT_POWERSAFE_FAIL) {
		DPRAM_LOG_ERR("[DPRAM] *** MODEM image corrupt.  Rerun download. ***\n");
		return IRQ_HANDLED;
	}

	if (irq_mask & INT_MASK_COMMAND) {
		irq_mask &= ~(INT_MASK_VALID | INT_MASK_COMMAND);
		command_handler(irq_mask);
	} else {
		irq_mask &= ~INT_MASK_VALID;
		non_command_handler(irq_mask);
	}

	//DPRAM_LOG_INFO("[DPRAM] Leaving dpram_irq_handler()\n");
	return IRQ_HANDLED;
}

static irqreturn_t phone_active_irq_handler(int irq, void *dev_id)
{
	volatile int gpio_state;
	int ret;
	gpio_state = gpio_get_value(GPIO_PHONE_ACTIVE);
	DPRAM_LOG_ERR("[DPRAM] PHONE_ACTIVE level: %s, phone_sync: %d\n",
			((gpio_state) ? "HIGH" : "LOW "), phone_sync);

#ifdef _ENABLE_ERROR_DEVICE
	if((phone_sync) && (!gpio_state)) {
		/* after 2 sec, check PHONE_ACTIVE pin again */
		ret = mod_timer(&phone_active_timer, jiffies + msecs_to_jiffies(1000));
		if(ret) 
			printk(KERN_ERR "error timer!!\n");
	}
#endif
	return IRQ_HANDLED;
}

/* basic functions. */
#ifdef _ENABLE_ERROR_DEVICE
static const struct file_operations dpram_err_ops = {
	.owner = THIS_MODULE,
	.read = dpram_err_read,
	.fasync = dpram_err_fasync,
	.poll = dpram_err_poll,
	.llseek = no_llseek,
	/* TODO: add more operations */
};
#endif	/* _ENABLE_ERROR_DEVICE */

static struct tty_operations dpram_tty_ops = {
	.open 		= dpram_tty_open,
	.close 		= dpram_tty_close,
	.write 		= dpram_tty_write,
	.write_room = dpram_tty_write_room,
	.ioctl 		= dpram_tty_ioctl,
	.chars_in_buffer = dpram_tty_chars_in_buffer,
	/* TODO: add more operations */
};

#ifdef _ENABLE_ERROR_DEVICE
static void unregister_dpram_err_device(void)
{
	unregister_chrdev(DRIVER_MAJOR_NUM, DPRAM_ERR_DEVICE);
	class_destroy(dpram_class);
}

static int register_dpram_err_device(void)
{
	/* @LDK@ 1 = formatted, 2 = raw, so error device is '0' */
	struct device *dpram_err_dev_t;
	int ret = register_chrdev(DRIVER_MAJOR_NUM, DPRAM_ERR_DEVICE, &dpram_err_ops);
	if (ret < 0)
		return ret;

	dpram_class = class_create(THIS_MODULE, "dpramerr");
	if (IS_ERR(dpram_class)) {
		unregister_dpram_err_device();
		return -EFAULT;
	}

	dpram_err_dev_t = device_create(dpram_class, NULL,
			MKDEV(DRIVER_MAJOR_NUM, 0), NULL, DPRAM_ERR_DEVICE);

	if (IS_ERR(dpram_err_dev_t)) {
		unregister_dpram_err_device();
		return -EFAULT;
	}
	return 0;
}
#endif	/* _ENABLE_ERROR_DEVICE */

static int register_dpram_driver(void)
{
	int retval = 0;
	/* @LDK@ allocate tty driver */
	dpram_tty_driver = alloc_tty_driver(MAX_INDEX);
	if (!dpram_tty_driver)
		return -ENOMEM;

	/* @LDK@ initialize tty driver */
	dpram_tty_driver->owner = THIS_MODULE;
	dpram_tty_driver->magic = TTY_DRIVER_MAGIC;
	dpram_tty_driver->driver_name = DRIVER_NAME;
	dpram_tty_driver->name = "dpram";
	dpram_tty_driver->major = DRIVER_MAJOR_NUM;
	dpram_tty_driver->minor_start = 1;
	dpram_tty_driver->num = 2;
	dpram_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
	dpram_tty_driver->subtype = SERIAL_TYPE_NORMAL;
	dpram_tty_driver->flags = TTY_DRIVER_REAL_RAW;
	dpram_tty_driver->init_termios = tty_std_termios;
	dpram_tty_driver->init_termios.c_cflag =
		(B115200 | CS8 | CREAD | CLOCAL | HUPCL);

	tty_set_operations(dpram_tty_driver, &dpram_tty_ops);
	dpram_tty_driver->ttys = dpram_tty;
	dpram_tty_driver->termios = dpram_termios;
	dpram_tty_driver->termios_locked = dpram_termios_locked;

	/* @LDK@ register tty driver */
	retval = tty_register_driver(dpram_tty_driver);

	if (retval) {
		DPRAM_LOG_ERR("tty_register_driver error\n");
		put_tty_driver(dpram_tty_driver);
		return retval;
	}
	return 0;
}

static void unregister_dpram_driver(void)
{
	tty_unregister_driver(dpram_tty_driver);
}

/**** Virtual Network Interface functions */
static int  vnet_recv_rx_data(u8 *buf, int size, struct pdp_info *dev)
{
	int ret_val = 0;
	struct sk_buff *skb;
	unsigned char ip_hdr_byte1 = 0, ip_version = 0;

#ifdef SVNET_PDP_ETHER
	char *p;
	struct ethhdr *eth_hdr;
	char source[ETH_ALEN] = {18, 52, 86, 120, 154, 188};
	char dest[ETH_ALEN]= {18, 0,  0,  0,   0,   0};
#endif

	MULTIPDP_LOG_INFO("In vnet_recv_rx_data \n");

	if (!netif_running(dev->vn_dev.net))
	{
		MULTIPDP_LOG_ERR("%s(id: %u) is not running\n", dev->vn_dev.net->name, dev->id);
		ret_val = size; /* just say we cosumed the buffer */
		return ret_val;
	}

#ifdef SVNET_PDP_ETHER
	skb = alloc_skb(size+ sizeof(struct ethhdr), GFP_ATOMIC);
#else
	skb = alloc_skb(size, GFP_ATOMIC);
#endif
	if (skb == NULL)
	{
		MULTIPDP_LOG_ERR("vnet_recv_rx_data ==> alloc_skb() failed\n");
		return ret_val;
	}
	/* determine the ip version */
	ip_hdr_byte1 = (*buf);
	ip_version = ip_hdr_byte1 >> 4;

#ifdef SVNET_PDP_ETHER
	p = skb_put(skb, size + sizeof(struct ethhdr));
	eth_hdr = (struct ethhdr *)p;
	memcpy(eth_hdr->h_dest, dest, ETH_ALEN);
	memcpy(eth_hdr->h_source, source, ETH_ALEN);
	eth_hdr->h_proto = (ip_version == 0x06)? __constant_htons(ETH_P_IPV6): __constant_htons(ETH_P_IP);
	p = p + sizeof(struct ethhdr);	/* advance eth header size */
	memcpy((void *)p, (void *)buf, size); /* copy payload from NIC perspective */
#else
	p = skb_put(skb, size);
	memcpy((void *)p, (void *)buf, size); /*no header, copy payload from begining */
#endif

	/* initalize skb net device information */
	skb->dev = dev->vn_dev.net;
	/* fill the protocol type in skb */
	skb->protocol = (ip_version == 0x06)? __constant_htons(ETH_P_IPV6): __constant_htons(ETH_P_IP);

#ifdef SVNET_PDP_ETHER
	skb_reset_mac_header(skb);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb_pull(skb, sizeof(struct ethhdr));
#endif

	netif_rx(skb); /* push the allocated skb to higher level */
	dev->vn_dev.stats.rx_packets++;
	dev->vn_dev.stats.rx_bytes += skb->len;

	return size;
}

static void vnet_del_dev(struct net_device *net)
{
	unregister_netdev(net);
	kfree(net);
}

static int vnet_open(struct net_device *net)
{
	struct pdp_info *dev = (struct pdp_info *)net->ml_priv;

	down(&dev->vn_dev.netq_sem);
	netif_start_queue(net);
	if(dev->vn_dev.netq_active == SUSPEND)
		netif_stop_queue(net);
	dev->vn_dev.netq_init = 1;
	up(&dev->vn_dev.netq_sem);

	return 0;
}

static int vnet_stop(struct net_device *net)
{
	struct pdp_info *dev = (struct pdp_info *)net->ml_priv;

	down(&dev->vn_dev.netq_sem);
	netif_stop_queue(net);
	up(&dev->vn_dev.netq_sem);

	return 0;
}

static int vnet_start_xmit(struct sk_buff *skb, struct net_device *net)
{
	struct pdp_info *dev = (struct pdp_info *)net->ml_priv;

	MULTIPDP_LOG_INFO("In vnet_start_xmit\n");

	MULTIPDP_LOG_WRITE("WRITE vnet_start_xmit ==> Buff Len %d \n", skb->len);

#ifdef SVNET_PDP_ETHER
	skb_pull(skb,14);
#endif

	skb_queue_tail(&txq, skb);

	MULTIPDP_LOG_WRITE("WRITE vnet_start_xmit ==> schedule write work item \n");
	if (!work_pending(&xmit_work_struct))
	{
		PREPARE_WORK(&xmit_work_struct, pdp_mux_net);
		schedule_work(&xmit_work_struct);
	}

	return 0;
}

static struct net_device_stats *vnet_get_stats(struct net_device *net)
{
	struct pdp_info *dev = (struct pdp_info *)net->ml_priv;
	return &dev->vn_dev.stats;
}

static void vnet_tx_timeout(struct net_device *net)
{
	struct pdp_info *dev = (struct pdp_info *)net->ml_priv;

	MULTIPDP_LOG_ERR("vnet_tx_timeout ==>Tx timed out, device ID = %d %d\n", is_net_stopped, dev->id);

	net->trans_start = jiffies;
	dev->vn_dev.stats.tx_errors++;

	if (dev->vn_dev.netq_active == ACTIVE){
		net_wakeup_all_if();
		is_net_stopped = 0;
	}
}

static const struct net_device_ops pdp_netdev_ops = {
	.ndo_open		= vnet_open,
	.ndo_stop		= vnet_stop,
	.ndo_start_xmit	= vnet_start_xmit,
	.ndo_get_stats	= vnet_get_stats,
	.ndo_tx_timeout	= vnet_tx_timeout,
};

static void vnet_setup(struct net_device *dev)
{
	dev->netdev_ops = &pdp_netdev_ops;

#ifdef SVNET_PDP_ETHER
	dev->type = ARPHRD_ETHER;
#else
	dev->type = ARPHRD_PPP;
#endif

#ifdef SVNET_PDP_ETHER
	dev->addr_len = ETH_ALEN;
	dev->dev_addr[0] = 0x12;
#else
	dev->addr_len = 0;
#endif
	dev->hard_header_len 	= 0;
	dev->mtu		= MAX_PDP_DATA_LEN;
	dev->tx_queue_len	= 1000;
#ifdef SVNET_PDP_ETHER
	dev->flags		= IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST | IFF_SLAVE;
#else
	dev->flags		= IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
#endif
	dev->watchdog_timeo	=100 * HZ;
}

static struct net_device *vnet_add_dev(void *priv)
{
	int ret;
	struct net_device *dev;

	MULTIPDP_LOG_INFO(" %s\n",__FUNCTION__);
	dev = alloc_netdev(0, "hrpd%d", vnet_setup);
	if (dev == NULL) {
		MULTIPDP_LOG_ERR("vnet_add_dev ==> alloc_netdev failed\n");
		return NULL;
	}

	dev->ml_priv		= priv;
	ret = register_netdev(dev);

	if (ret != 0) {
		MULTIPDP_LOG_ERR("vnet_add_dev ==> register_netdevice failed: %d\n", ret);
		kfree(dev);
		return NULL;
	}
	return dev;
}

/******* Virtual Serial Interface functions *******/
static int vs_open(struct tty_struct *tty, struct file *filp)
{

	struct pdp_info *dev;

	dev = pdp_get_serdev(tty->driver->name); /* 2.6 kernel porting */
	if (dev == NULL) {
		int ret;

		pdp_arg_t pdp_arg = { .id = 7, .ifname = "ttyCDMA", };
		pdp_arg_t scrn_arg = { .id = 16, .ifname = "ttySCRN", };
		pdp_arg_t debug_arg = { .id = 28, .ifname = "ttyDEBUG", };
		pdp_arg_t atchnl_arg = { .id = 17, .ifname = "ttyCSD", };
		pdp_arg_t ets_arg = { .id = 26, .ifname = "ttyETS", };
		pdp_arg_t ptp_arg = { .id = 18, .ifname = "ttyPTP", };
		
		MULTIPDP_LOG_ERR("tty_driver retry routine!!\n");

		if(strcmp(tty->driver->name, "ttyCDMA") == 0) {
			ret = pdp_activate(&pdp_arg, DEV_TYPE_SERIAL,DEV_FLAG_STICKY);
			if (ret < 0) {
				MULTIPDP_LOG_ERR("failed to create a serial device for ttyCDMA\n");
				return -ENODEV;
			}
		} else if (strcmp(tty->driver->name, "ttySCRN") == 0) {
			ret = pdp_activate(&scrn_arg, DEV_TYPE_SERIAL,DEV_FLAG_STICKY);
			if (ret < 0) {
				MULTIPDP_LOG_ERR("failed to create a serial device for ttySCRN\n");
		return -ENODEV;
			}
		} else if (strcmp(tty->driver->name, "ttyDEBUG") == 0) {
			ret = pdp_activate(&debug_arg, DEV_TYPE_SERIAL,DEV_FLAG_STICKY);
			if (ret < 0) {
				MULTIPDP_LOG_ERR("failed to create a serial device for ttyDEBUG\n");
				return -ENODEV;
			}
		} else if (strcmp(tty->driver->name, "ttyCSD") == 0) {
			ret = pdp_activate(&atchnl_arg, DEV_TYPE_SERIAL,DEV_FLAG_STICKY);
			if (ret < 0) {
				MULTIPDP_LOG_ERR("failed to create a serial device for ttyCSD\n");
				return -ENODEV;
			}
		} else if (strcmp(tty->driver->name, "ttyETS") == 0) {
			ret = pdp_activate(&ets_arg, DEV_TYPE_SERIAL,DEV_FLAG_STICKY);
			if (ret < 0) {
				MULTIPDP_LOG_ERR("failed to create a serial device for ttyETS\n");
				return -ENODEV;
			}
		} else if (strcmp(tty->driver->name, "ttyPTP") == 0) {
			ret = pdp_activate(&ptp_arg, DEV_TYPE_SERIAL,DEV_FLAG_STICKY);
			if (ret < 0) {
				MULTIPDP_LOG_ERR("failed to create a serial device for ttyPTP\n");
				return -ENODEV;
			}
		} else {
			MULTIPDP_LOG_ERR("invalid argument! : %s\n", tty->driver->name);
			return -ENODEV;
		}
	}

	tty->driver_data = (void *)dev;
	tty->low_latency = 0;
	dev->vs_dev.tty = tty;
	dev->vs_dev.refcount++;
	DPRAM_LOG_INFO("[%s] %s, refcount: %d \n", __func__, tty->driver->name, dev->vs_dev.refcount);

	return 0;
}

static void vs_close(struct tty_struct *tty, struct file *filp)
{
	struct pdp_info *dev;

	dev = pdp_get_serdev(tty->driver->name);
	if (!dev)
		return;
	dev->vs_dev.refcount--;
	DPRAM_LOG_INFO("[%s] %s, refcount: %d \n", __func__, tty->driver->name, dev->vs_dev.refcount);

	return;
}

static int vs_write(struct tty_struct *tty,
		const unsigned char *buf, int count)
{
	int ret;
	struct pdp_info *dev = (struct pdp_info *)tty->driver_data;

	ret = pdp_mux_tty(dev, buf, count); // we should return only how much we wrote

	return ret;
}

static int vs_write_room(struct tty_struct *tty)
{
	return 8192*2;		/* TODO: No idea where is number came from?? */
}

static int vs_chars_in_buffer(struct tty_struct *tty)
{
	return 0;
}

static int vs_ioctl(struct tty_struct *tty, struct file *file,
		unsigned int cmd, unsigned long arg)
{
	return -ENOIOCTLCMD;
}

static struct tty_operations multipdp_tty_ops = {
	.open 		= vs_open,
	.close 		= vs_close,
	.write 		= vs_write,
	.write_room = vs_write_room,
	.ioctl 		= vs_ioctl,
	.chars_in_buffer = vs_chars_in_buffer,
};

static int vs_add_dev(struct pdp_info *dev)
{
	struct tty_driver *tty_driver;

	tty_driver = &dev->vs_dev.tty_driver;

	switch (dev->id) {
		case 7:
			tty_driver->minor_start = CSD_MINOR_NUM;
			break;

		case 8:
			tty_driver->minor_start = 1;
			break;

		case 5:
			tty_driver->minor_start = 2;
			break;

		case 6:
			tty_driver->minor_start = 3;
			break;

		case 25:
			tty_driver->minor_start = 4;
			break;

		case 30:
			tty_driver->minor_start = 5;
			break;
		case 26:
			tty_driver->minor_start = 6;
			break;

		case 16:
			tty_driver->minor_start = 7;
			break;

		case 28:
			tty_driver->minor_start = 8;
			break;

		case 17:
			tty_driver->minor_start = 9;
			break;

		case 18:
			tty_driver->minor_start = 10;
			break;

		default:
			tty_driver = NULL;
	}

	if (!tty_driver) {
		MULTIPDP_LOG_ERR("vs_add_dev ==> tty driver is NULL!\n");
		return -1;
	}

	kref_init(&tty_driver->kref);

	tty_driver->magic	= TTY_DRIVER_MAGIC;
	tty_driver->driver_name	= "multipdp";
	tty_driver->name	= dev->vs_dev.tty_name;
	tty_driver->major	= CSD_MAJOR_NUM;
	//	tty_driver->minor_start = CSD_MINOR_NUM;
	tty_driver->num		= 1;
	tty_driver->type	= TTY_DRIVER_TYPE_SERIAL;
	tty_driver->subtype	= SERIAL_TYPE_NORMAL;
	tty_driver->flags	= TTY_DRIVER_REAL_RAW;
	tty_driver->ttys	= dev->vs_dev.tty_table; // 2.6 kernel porting
	tty_driver->termios	= dev->vs_dev.termios;
	tty_driver->termios_locked	= dev->vs_dev.termios_locked;

	tty_set_operations(tty_driver, &multipdp_tty_ops);
	return tty_register_driver(tty_driver);
}

static void vs_del_dev(struct pdp_info *dev)
{
	struct tty_driver *tty_driver = NULL;
	tty_driver = &dev->vs_dev.tty_driver;
	tty_unregister_driver(tty_driver);
}
static inline struct pdp_info * pdp_get_dev(u8 id)
{
	int slot;

	for (slot = 0; slot < MAX_PDP_CONTEXT; slot++) {
		if (pdp_table[slot] && pdp_table[slot]->id == id) {
			return pdp_table[slot];
		}
	}
	return NULL;
}

static inline int pdp_add_dev(struct pdp_info *dev)
{
	int slot;

	if (pdp_get_dev(dev->id)) {
		return -EBUSY;
	}

	for (slot = 0; slot < MAX_PDP_CONTEXT; slot++) {
		if (pdp_table[slot] == NULL) {
			pdp_table[slot] = dev;
			return slot;
		}
	}
	return -ENOSPC;
}

static inline struct pdp_info * pdp_remove_slot(int slot)
{
	struct pdp_info *dev;

	dev = pdp_table[slot];
	pdp_table[slot] = NULL;
	return dev;
}

static int pdp_activate(pdp_arg_t *pdp_arg, unsigned type, unsigned flags )
{
	int ret = 0;
	struct pdp_info *dev = NULL;
	struct net_device *net = NULL;
	MULTIPDP_LOG_INFO("pdp_activate ==> id: %d\n", pdp_arg->id);

	dev = vmalloc(sizeof(struct pdp_info) + MAX_PDP_PACKET_LEN);
	if (dev == NULL) {
		MULTIPDP_LOG_ERR("pdp_activate ==> vmalloc failed, out of memory\n");
		return -ENOMEM;
	}
	memset(dev, 0, sizeof(struct pdp_info));

	/* @LDK@ added by gykim on 20070203 for adjusting IPC 3.0 spec. */
	if (type == DEV_TYPE_NET) {
		dev->id = pdp_arg->id + g_adjust;
		printk(KERN_ERR "[dpram] id : %d\n", pdp_arg->id);
	}
	else {
		dev->id = pdp_arg->id;
		dev->vs_dev.refcount = 0;
	}
	dev->type = type;
	dev->flags = flags;
	dev->tx_buf = (u8 *)(dev + 1);

	if (type == DEV_TYPE_NET) {
			net = vnet_add_dev((void *)dev);
		if (!net) {
			vfree(dev);
			return -ENOMEM;
		}
		dev->vn_dev.netq_init = 0;
		dev->vn_dev.netq_active = g_datastatus;
		init_MUTEX(&dev->vn_dev.netq_sem);

		dev->vn_dev.net = net;
		strcpy(pdp_arg->ifname, net->name);

		down(&pdp_lock);
		ret = pdp_add_dev(dev);
		if (ret < 0) {
			MULTIPDP_LOG_ERR("pdp_activate ==> pdp_add_dev() failed\n");
			up(&pdp_lock);
			vnet_del_dev(dev->vn_dev.net);
			vfree(dev);
			return ret;
		}
		up(&pdp_lock);
		MULTIPDP_LOG_INFO("%s(id: %u) network device created\n", net->name, dev->id);
	} else if (type == DEV_TYPE_SERIAL) {
		init_MUTEX(&dev->vs_dev.write_lock);
		strcpy(dev->vs_dev.tty_name, pdp_arg->ifname);

		ret = vs_add_dev(dev);
		if (ret < 0) {
			vfree(dev);
			return ret;
		}

		down(&pdp_lock);
		ret = pdp_add_dev(dev);
		if (ret < 0) {
			MULTIPDP_LOG_ERR("pdp_activate ==> pdp_add_dev() failed\n");
			up(&pdp_lock);
			vs_del_dev(dev);
			vfree(dev);
			return ret;
		}
		up(&pdp_lock);

		if (dev->id == 7) {
			MULTIPDP_LOG_INFO("%s(id: %u) serial device is created.\n", dev->vs_dev.tty_driver.name, dev->id);
		}
		else if (dev->id == 8) {
			MULTIPDP_LOG_INFO("%s(id: %u) serial device is created.\n", dev->vs_dev.tty_driver.name, dev->id);
		}
		//skumar@wtl for ttyETS (ETS over DPRAM)
		else if (dev->id == 26) {
			MULTIPDP_LOG_INFO("%s(id: %u) serial device is created.\n", dev->vs_dev.tty_driver.name, dev->id);
		}
		else if (dev->id == 16) {
			MULTIPDP_LOG_INFO("%s(id: %u) serial device is created.\n", dev->vs_dev.tty_driver.name, dev->id);
		}
		else if (dev->id == 28) {
			MULTIPDP_LOG_INFO("%s(id: %u) serial device is created.\n", dev->vs_dev.tty_driver.name, dev->id);
		}
		else if (dev->id == 17) {
			MULTIPDP_LOG_INFO("%s(id: %u) serial device is created.\n", dev->vs_dev.tty_driver.name, dev->id);
		}
		else if (dev->id == 18) {
			MULTIPDP_LOG_INFO("%s(id: %u) serial device is created.\n", dev->vs_dev.tty_driver.name, dev->id);
		}

	}
	return 0;
}

static int pdp_deactivate(pdp_arg_t *pdp_arg, int force)
{
	struct pdp_info *dev = NULL;

	MULTIPDP_LOG_INFO("pdp_deactivate ==> id: %d\n", pdp_arg->id);

	pdp_arg->id = pdp_arg->id + g_adjust;

	dev = pdp_get_dev(pdp_arg->id);
	if (dev == NULL) {
		MULTIPDP_LOG_ERR("pdp_deactivate ==> error not found id: %u\n", pdp_arg->id);
		return -EINVAL;
	}
	if (!force && dev->flags & DEV_FLAG_STICKY) {
		MULTIPDP_LOG_ERR("pdp_deactivate ==> sticky id: %u\n", pdp_arg->id);
		return -EACCES;
	}

	if (dev->type == DEV_TYPE_NET) {
		down(&pdp_lock);

		pdp_remove_dev(pdp_arg->id);
		up(&pdp_lock);

		MULTIPDP_LOG_WARN("%s(id: %u) network device removed\n",
				dev->vn_dev.net->name, dev->id);
		vnet_del_dev(dev->vn_dev.net);

		vfree(dev);
	}

	return 0;
}

static void pdp_cleanup(void)
{
	int slot;
	struct pdp_info *dev;

	down(&pdp_lock);
	for (slot = 0; slot < MAX_PDP_CONTEXT; slot++) {
		dev = pdp_remove_slot(slot);
		if (dev) {
			if (dev->type == DEV_TYPE_NET) {
				MULTIPDP_LOG_WARN("%s(id: %u) network device removed\n",
						dev->vn_dev.net->name, dev->id);
				vnet_del_dev(dev->vn_dev.net);
			} else if (dev->type == DEV_TYPE_SERIAL) {
				if (dev->id == 1) {
					MULTIPDP_LOG_WARN("%s(id: %u) serial device removed\n",
							dev->vs_dev.tty_driver.name, dev->id);
				}
				else if (dev->id == 8) {
					MULTIPDP_LOG_WARN("%s(id: %u) serial device removed\n",
							dev->vs_dev.tty_driver.name, dev->id);
				}
				else if (dev->id == 16) {
					MULTIPDP_LOG_WARN("%s(id: %u) serial device removed\n",
							dev->vs_dev.tty_driver.name, dev->id);
				}
				else if (dev->id == 28) {
					MULTIPDP_LOG_WARN("%s(id: %u) serial device removed\n",
							dev->vs_dev.tty_driver.name, dev->id);
				}
				else if (dev->id == 17) {
					MULTIPDP_LOG_WARN("%s(id: %u) serial device removed\n",
							dev->vs_dev.tty_driver.name, dev->id);
				}
				vs_del_dev(dev);
			}
			vfree(dev);
		}
	}
	up(&pdp_lock);
}
static int pdp_adjust(const int adjust)
{
	g_adjust = adjust;
	MULTIPDP_LOG_INFO("pdp_adjust ==> adjusting value: %d\n", adjust);
	return 0;
}

static int pdp_setradiotype(const int radio)
{
	struct pdp_info *dev;
	int loopcount;
	g_radio = radio;

	MULTIPDP_LOG_INFO("pdp_setradiotype  = %d \n ",g_radio);
	down(&pdp_lock);
	for (loopcount = 0; loopcount < MAX_PDP_CONTEXT; loopcount++) {
		dev = pdp_table[loopcount];
		if (dev!=NULL) {
			if(dev->type == DEV_TYPE_NET)
			{
				if (g_radio == LTE)
					atomic_set(&dev->intf_dev, ONEDRAM_DEV);
				else
					atomic_set(&dev->intf_dev, DPRAM_DEV);
			}
		}
	}
	up(&pdp_lock);
	return 0;
}

static int pdp_datastatus(const int datastatus)
{
	struct pdp_info *dev;
	int loopcount;

	down(&pdp_lock);
	for (loopcount = 0; loopcount < MAX_PDP_CONTEXT; loopcount++) {
		dev = pdp_table[loopcount];
		if ((dev!=NULL) && (dev->type == DEV_TYPE_NET)){
			down(&dev->vn_dev.netq_sem);
			if (SUSPEND == datastatus){
				if (dev->vn_dev.netq_init != 0){
					netif_stop_queue(dev->vn_dev.net);
				}
				dev->vn_dev.netq_active = SUSPEND;
			}
			else{
				if ((dev->vn_dev.netq_init != 0) && (SUSPEND == dev->vn_dev.netq_active)){
					netif_wake_queue(dev->vn_dev.net);
				}
				dev->vn_dev.netq_active = ACTIVE;
			}
			up(&dev->vn_dev.netq_sem);
		}
	}
	g_datastatus = datastatus;		//Not sure if we even need this global variable.
	MULTIPDP_LOG_INFO("pdp_datastatus = %d \n ",g_datastatus);
	up(&pdp_lock);

	return 0;
}

static void net_stop_all_if ()
{
	int loopcount = 0;
	struct pdp_info *dev;

	down(&pdp_lock);
	for (loopcount = 0; loopcount < MAX_PDP_CONTEXT; loopcount++) {
		dev = pdp_table[loopcount];
		if ((dev!=NULL) && (dev->type == DEV_TYPE_NET)){
			down(&dev->vn_dev.netq_sem);
			netif_stop_queue(dev->vn_dev.net);
			up(&dev->vn_dev.netq_sem);
		}
	}
	up(&pdp_lock);
}

static void net_wakeup_all_if()
{
	int loopcount = 0;
	struct pdp_info *dev;

	down(&pdp_lock);
	for (loopcount = 0; loopcount < MAX_PDP_CONTEXT; loopcount++) {
		dev = pdp_table[loopcount];
		if ((dev!=NULL) && (dev->type == DEV_TYPE_NET)){
			down(&dev->vn_dev.netq_sem);
			netif_wake_queue(dev->vn_dev.net);
			up(&dev->vn_dev.netq_sem);
		}
	}
	up(&pdp_lock);
}

static long multipdp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int adjust, radio, datastatus;
	long ret = 0;
	pdp_arg_t pdp_arg;
    void __user *argp = (void __user *)arg;

    lock_kernel();

    printk(KERN_ERR "[%s] cmd : 0x%x\n", __func__, cmd);

	switch (cmd) {
		case HN_PDP_ACTIVATE:
			{
				MULTIPDP_LOG_INFO ("HN_PDP_ACTIVATE \n");
				if (copy_from_user(&pdp_arg, (void *)argp, sizeof(pdp_arg))) {
					MULTIPDP_LOG_ERR("HN_PDP_ACTIVATE copy_from_user failed \n");
                    unlock_kernel();
					return -EFAULT;
				}

				ret = pdp_activate(&pdp_arg, DEV_TYPE_NET, 0);
				if (ret < 0) {
					MULTIPDP_LOG_ERR("HN_PDP_ACTIVATE pdp_activate failed\n");
                    unlock_kernel();
					return ret;
				}
			}
			return copy_to_user(argp, &pdp_arg, sizeof(pdp_arg));

		case HN_PDP_DEACTIVATE:
			if (copy_from_user(&pdp_arg, argp, sizeof(pdp_arg))){
                unlock_kernel();
				return -EFAULT;
            }
			return pdp_deactivate(&pdp_arg, 0);

		case HN_PDP_ADJUST:
			if (copy_from_user(&adjust, argp, sizeof (int))){
                unlock_kernel();
				return -EFAULT;
            }
			return pdp_adjust(adjust);

		case HN_PDP_SETRADIO:
			if (copy_from_user(&radio, argp, sizeof (int))){
                unlock_kernel();
				return -EFAULT;
            }
			return pdp_setradiotype(radio);
		case HN_PDP_DATASTATUS:
			if (copy_from_user(&datastatus, argp, sizeof (int))){
                unlock_kernel();
				return -EFAULT;
            }
			return pdp_datastatus(datastatus);
		case HN_PDP_TXSTART:
			pdp_tx_flag = 0;
            unlock_kernel();
			return 0;

		case HN_PDP_TXSTOP:
			pdp_tx_flag = 1;
            unlock_kernel();
			return 0;

		case HN_PDP_FLUSH_WORK:
			flush_scheduled_work(); /* flush any pending tx tasks */
            unlock_kernel();
			return 0;
	}
    unlock_kernel();

	MULTIPDP_LOG_ERR("invalid ioctl cmd : %x\n", cmd);
	return -EINVAL;
}

static struct file_operations multipdp_fops = {
	.owner =	THIS_MODULE,
	.unlocked_ioctl =	multipdp_ioctl,
	.llseek =	no_llseek,
};

static struct miscdevice multipdp_dev = {
	.minor =	132, //MISC_DYNAMIC_MINOR,
	.name =		APP_DEVNAME,
	.fops =		&multipdp_fops,
};

static inline struct pdp_info *pdp_get_serdev(const char *name)
{
	int slot;
	struct pdp_info *dev;

	for (slot = 0; slot < MAX_PDP_CONTEXT; slot++) {
		dev = pdp_table[slot];
		if (dev && dev->type == DEV_TYPE_SERIAL &&
				strcmp(name, dev->vs_dev.tty_name) == 0) {
			return dev;
		}
	}
	return NULL;
}

static inline struct pdp_info *pdp_remove_dev(u8 id)
{
	int slot;
	struct pdp_info *dev;
	for (slot = 0; slot < MAX_PDP_CONTEXT; slot++) {
		if (pdp_table[slot] && pdp_table[slot]->id == id) {
			dev = pdp_table[slot];
			pdp_table[slot] = NULL;
			return dev;
		}
	}
	return NULL;
}

static int pdp_mux_tty(struct pdp_info *dev, const void *data, size_t len)
{
	int ret, n = 0;
	size_t nbytes;
	u8 *tx_buf;
	struct pdp_hdr *hdr;
	const u8 *buf;
	unsigned long start_time = jiffies; 
	struct timeval elapsedtime_tv = {0};
	int count;

	down(&mux_tty_lock);

	tx_buf = dev->tx_buf;
	hdr = (struct pdp_hdr *)(tx_buf + 1);
	buf = data;

	hdr->id = dev->id; //file device id
	hdr->control = 0; //always zero for now

	//compute the max allowed length
	if (len > MAX_PDP_DATA_LEN)
		nbytes = MAX_PDP_DATA_LEN;
	else
		nbytes = len;

	hdr->len = nbytes + sizeof(struct pdp_hdr); //this is our packet lenth
	tx_buf[0] = 0x7f; //fill eof flag at the end
	memcpy(tx_buf + 1 + sizeof(struct pdp_hdr), buf,  nbytes); //copy payload to buffer
	tx_buf[1 + hdr->len] = 0x7e;

	//total count to send
	count = hdr->len + 2; //SOF and EOF

	multipdp_debug_dump_write_buffer(tx_buf, count);

	while (count) {
		/* printk(KERN_ERR "hdr->id: %d, hdr->len: %d\n", hdr->id, hdr->len); */
		ret = dpram_write(&dpram_table[RAW_INDEX], tx_buf + n, count);
		if (ret < 0) {
			MULTIPDP_LOG_ERR("write_to_dpram() failed: %d\n", ret);
			up(&mux_tty_lock);
			return -EAGAIN;
		}
		n += ret;
		count -= ret;

		jiffies_to_timeval( jiffies - start_time, &elapsedtime_tv );
		if (elapsedtime_tv.tv_sec > 1) {
			MULTIPDP_LOG_ERR("write_to_dpram() failed even after retrying: %d\n", ret);
			up(&mux_tty_lock);
			return -EAGAIN;
		}
	}
	up(&mux_tty_lock);
	return nbytes;
}

static const char hdlc_start[1] = { 0x7F };
static const char hdlc_end[1] = { 0x7E };

static int _write_raw_skb(dpram_device_t *device, struct pdp_info *dev, struct sk_buff *skb)
{
	char *b;
	struct pdp_hdr *hdr;

	b = skb_put(skb, sizeof(hdlc_end));
	memcpy(b, hdlc_end, sizeof(hdlc_end));

	b = skb_push(skb, sizeof(struct pdp_hdr) + sizeof(hdlc_start));
	memcpy(b, hdlc_start, sizeof(hdlc_start));

	b += sizeof(hdlc_start);

	hdr = (struct pdp_hdr*)b;
	hdr->id = dev->id;
	hdr->control = 0;
	hdr->len  = skb->len - 2;

	return dpram_write_net(device, skb->data, skb->len);
}

static int _write_raw_buf(dpram_device_t *device, struct pdp_info *dev, struct sk_buff *skb)
{
	int len;
	struct pdp_hdr h;

	len = skb->len + sizeof(struct pdp_hdr);
	h.id = dev->id;
	h.control = 0;
	h.len = len;

	len  = dpram_write_net(device, (u8*)hdlc_start, sizeof(hdlc_start));
	len += dpram_write_net(device, (u8*)&h, sizeof(h));
	len += dpram_write_net(device, skb->data, skb->len);
	len += dpram_write_net(device, (u8*)hdlc_end, sizeof(hdlc_end));

	return len;
}

static int pdp_mux_net(struct work_struct *data)
{
	int ret = 0;
	struct sk_buff *skb;
	u16 head, tail;
	int free_space = 0;
	dpram_device_t *device;
	u16 irq_mask = 0;
	struct net_device *net;
	struct pdp_info *dev;
	
	device = &dpram_table[RAW_INDEX];

	down(&mux_net_lock);

	MULTIPDP_LOG_INFO("In pdp_mux_net ()\n");

	skb = skb_dequeue(&txq);
	while (skb) {
		READ_FROM_DPRAM_VERIFY(&head, device->out_head_addr, sizeof(head));
		READ_FROM_DPRAM_VERIFY(&tail, device->out_tail_addr, sizeof(tail));
		free_space = (head < tail) ? tail - head - 1 : device->out_buff_size + tail - head - 1;
		if (free_space < skb->len + 4) {
			ret = -ENOSPC;
			break; 
		} 

		net = (struct net_device *)skb->dev;
		if (!net) {
			MULTIPDP_LOG_ERR("net_device is already unregistered!\n");
			up(&mux_net_lock);
			return -ENODEV;
		}
		dev = (struct pdp_info *)net->ml_priv;

		if (skb_headroom(skb) > (sizeof(struct pdp_hdr) + sizeof(hdlc_start)) 
				&& skb_tailroom(skb) > sizeof(hdlc_end)) { 
			ret = _write_raw_skb(device, dev, skb);
		} else {
			ret = _write_raw_buf(device, dev, skb);
		}

		if (ret < 0) {
			dev->vn_dev.stats.tx_dropped++;
		} else {
			net->trans_start = jiffies;
			dev->vn_dev.stats.tx_bytes += skb->len;
			dev->vn_dev.stats.tx_packets++;
		}

		dev_kfree_skb_any(skb);
		skb = skb_dequeue(&txq);
	}

	if (ret > 0) {
		if (is_net_stopped)
		{
			net_wakeup_all_if();
			is_net_stopped = 0;
		}

		irq_mask = INT_MASK_VALID | INT_MASK_SEND_R;
		send_interrupt_to_phone(irq_mask);
	} else if (ret < 0) {
		if (ret == -ENOSPC) {
                    int timer_ret = 0;
                    skb_queue_head(&txq, skb);
                    MULTIPDP_LOG_INFO("write nospc queue %p is_net: %d freespace: %d\n", skb, is_net_stopped, free_space);
                    send_interrupt_to_phone(INT_NON_COMMAND(dpram_table[RAW_INDEX].mask_req_ack));

                    timer_ret = mod_timer(&req_ack_timer, jiffies + msecs_to_jiffies(200));
                    if(timer_ret) 
    			printk(KERN_ERR "error timer!!\n");
                        
			if (!is_net_stopped)
			{
				net_stop_all_if ();
				is_net_stopped = 1;
			}
		} else {
			MULTIPDP_LOG_ERR("write err %d, drop %p\n", ret, skb);
			dev_kfree_skb_any(skb);
                }
	} else {
		MULTIPDP_LOG_INFO("txq was already processed\n");
		if (is_net_stopped)
		{
			net_wakeup_all_if();
			is_net_stopped = 0;
		}
	}

	up(&mux_net_lock);
	return ret;
}

static int multipdp_init(void)
{
	int ret;
	pdp_arg_t pdp_arg = { .id = 7, .ifname = "ttyCDMA", };
	//pdp_arg_t efs_arg = { .id = 8, .ifname = "ttyEFS", };
	//pdp_arg_t gps_arg = { .id = 5, .ifname = "ttyGPS", };
	//pdp_arg_t xtra_arg = { .id = 6, .ifname = "ttyXTRA", };
	//pdp_arg_t smd_arg = { .id = 25, .ifname = "ttySMD", };
	//pdp_arg_t pcm_arg = { .id = 30, .ifname = "ttyPCM", } ;
	//pdp_arg_t net_arg = { .id = 1, .ifname = "pdp", } ;	//for network interface
	pdp_arg_t scrn_arg = { .id = 16, .ifname = "ttySCRN", } ;	// for TRFB
	pdp_arg_t debug_arg = { .id = 28, .ifname = "ttyDEBUG", } ;
	pdp_arg_t atchnl_arg = { .id = 17, .ifname = "ttyCSD", } ; // sbaby@wtl - for AT command channel
	pdp_arg_t ets_arg = { .id = 26, .ifname = "ttyETS", };
	pdp_arg_t ptp_arg = { .id = 18, .ifname = "ttyPTP", };

	DPRAM_LOG_INFO("multipdp_init \n");

	ret = pdp_activate(&pdp_arg, DEV_TYPE_SERIAL,DEV_FLAG_STICKY);
	if (ret < 0) {
		MULTIPDP_LOG_ERR("failed to create a serial device for ttyCDMA\n");
		goto pdp_arg;
	}

	ret = pdp_activate(&ets_arg, DEV_TYPE_SERIAL,DEV_FLAG_STICKY);
	if (ret < 0) {
		MULTIPDP_LOG_ERR("failed to create a serial device for ttyETS\n");
		goto ets_arg;
	}

	ret = pdp_activate(&scrn_arg, DEV_TYPE_SERIAL, DEV_FLAG_STICKY);
	if (ret < 0) {
		MULTIPDP_LOG_ERR("failed to create a serial device for ttySCRN\n");
		goto scrn_arg;
	}
	ret = pdp_activate(&debug_arg, DEV_TYPE_SERIAL, DEV_FLAG_STICKY);
	if (ret < 0) {
		MULTIPDP_LOG_ERR("failed to create a serial device for ttyDEBUG\n");
		goto debug_arg;
	}
	ret = pdp_activate(&atchnl_arg, DEV_TYPE_SERIAL, DEV_FLAG_STICKY);
	if (ret < 0) {
		MULTIPDP_LOG_ERR("failed to create a serial device for ttyCSD\n");
		goto atchnl_arg;
	}

	ret = pdp_activate(&ptp_arg, DEV_TYPE_SERIAL, DEV_FLAG_STICKY);
	if (ret < 0) {
		MULTIPDP_LOG_ERR("failed to create a serial device for ttyptp\n");
		goto ptp_arg;
	}

	/* create app. interface device */
	ret = misc_register(&multipdp_dev);
	if (ret < 0) {
		MULTIPDP_LOG_ERR("misc_register() failed\n");
		goto atchnl_arg;
	}

	return 0;

ptp_arg:
	pdp_deactivate(&ptp_arg, 1);
atchnl_arg:
	pdp_deactivate(&atchnl_arg, 1);
debug_arg:
	pdp_deactivate(&debug_arg, 1);
scrn_arg:
	pdp_deactivate(&scrn_arg, 1);
ets_arg:
	pdp_deactivate(&ets_arg, 1);
pdp_arg:
	pdp_deactivate(&pdp_arg, 1);

	return ret;
}

static void multipdp_exit(void)
{
	/* remove app. interface device */
	misc_deregister(&multipdp_dev);

	/* clean up PDP context table */
	pdp_cleanup();
}

static void init_devices(void)
{
	int i;
	for (i = 0; i < MAX_INDEX; i++) {
		init_MUTEX(&dpram_table[i].serial.sem);
		dpram_table[i].serial.open_count = 0;
		dpram_table[i].serial.tty = NULL;
	}
}

static void init_hw_setting(void)
{
    u32 reg;
    /* initial pin settings - dpram driver control */
    gpio_request(GPIO_DP_INT_AP,"dpram/IRQ_DPRAM_INT_N");
    gpio_direction_input(GPIO_DP_INT_AP);
    tegra_gpio_enable(GPIO_DP_INT_AP);
    set_irq_type(IRQ_DPRAM_INT_N, IRQ_TYPE_EDGE_FALLING);

    gpio_request(GPIO_PHONE_ACTIVE, "dpram/IRQ_PHONE_ACTIVE");
    gpio_direction_input(GPIO_PHONE_ACTIVE);
    tegra_gpio_enable(GPIO_PHONE_ACTIVE);
    set_irq_type(IRQ_PHONE_ACTIVE, IRQ_TYPE_EDGE_BOTH);

    if(system_rev > 0x0A){
        if (gpio_is_valid(GPIO_PHONE_ON)) {
            if (gpio_request(GPIO_PHONE_ON, "dpram/GPIO_PHONE_ON"))
                printk(KERN_ERR "request fail GPIO_PHONE_ON\n");
            gpio_direction_output(GPIO_PHONE_ON, GPIO_LEVEL_LOW);
        }
        gpio_set_value(GPIO_PHONE_ON, GPIO_LEVEL_LOW);
    }
    else{
        if (gpio_is_valid(GPIO_CP_ON_REV05)) {
            if (gpio_request(GPIO_CP_ON_REV05, "dpram/GPIO_PHONE_ON"))
                printk(KERN_ERR "request fail GPIO_PHONE_ON\n");
            gpio_direction_output(GPIO_CP_ON_REV05, GPIO_LEVEL_LOW);
        }
        gpio_set_value(GPIO_CP_ON_REV05, GPIO_LEVEL_LOW);
    }

    if (gpio_is_valid(GPIO_PHONE_RST_N)) {
        if (gpio_request(GPIO_PHONE_RST_N, "dpram/GPIO_PHONE_RST_N"))
            printk(KERN_ERR "request fail GPIO_PHONE_RST_N\n");
        reg = readl(IO_ADDRESS(0x6000d108));
        writel(reg | (0x01 << 6), IO_ADDRESS(0x6000d108)); //set phone_reset gpio config output
        gpio_direction_output(GPIO_PHONE_RST_N, GPIO_LEVEL_LOW);
    }

    if (gpio_is_valid(GPIO_VIA_PS_HOLD_OFF)) {
        if (gpio_request(GPIO_VIA_PS_HOLD_OFF, "dpram/GPIO_VIA_PS_HOLD_OFF"))
            printk(KERN_ERR "request fail GPIO_VIA_PS_HOLD_OFF\n");
        reg = readl(IO_ADDRESS(0x6000d184));
        writel(reg | (0x1 << 5), IO_ADDRESS(0x6000d184)); //set gpio via_ps_hold output
        gpio_direction_output(GPIO_VIA_PS_HOLD_OFF, GPIO_LEVEL_HIGH);
        tegra_gpio_enable(GPIO_VIA_PS_HOLD_OFF);
    }

    tegra_init_snor();
}

static void kill_tasklets(void)
{
	tasklet_kill(&fmt_res_ack_tasklet);
	tasklet_kill(&raw_res_ack_tasklet);
	tasklet_kill(&fmt_send_tasklet);
	tasklet_kill(&raw_send_tasklet);
}

static int register_interrupt_handler(void)
{

	unsigned int dpram_irq, phone_active_irq;
	int retval = 0;

	dpram_irq = IRQ_DPRAM_INT_N;
	phone_active_irq = IRQ_PHONE_ACTIVE;

	/* @LDK@ interrupt area read - pin level will be driven high. */
	/* dpram_clear(); */

	/* @LDK@ dpram interrupt */
	retval = request_irq(dpram_irq, dpram_irq_handler, IRQF_DISABLED, "dpram irq", NULL);

	if (retval) {
		DPRAM_LOG_ERR("DPRAM interrupt handler failed.\n");
		unregister_dpram_driver();
		return -1;
	}

	/* @LDK@ phone active interrupt */
	retval = request_irq(phone_active_irq, phone_active_irq_handler, IRQF_DISABLED, "VIAActive", NULL);

	if (retval) {
		DPRAM_LOG_ERR("Phone active interrupt handler failed.\n");
		free_irq(phone_active_irq, NULL);
		unregister_dpram_driver();
		return -1;
	}
	return 0;
}

static void check_miss_interrupt(void)
{
	unsigned long flags;
	if (gpio_get_value(GPIO_PHONE_ACTIVE) &&
			(!gpio_get_value(GPIO_DPRAM_INT_N))) {
		DPRAM_LOG_ERR("there is a missed interrupt. try to read it!\n");

		local_irq_save(flags);
		dpram_irq_handler(IRQ_DPRAM_INT_N, NULL);
		local_irq_restore(flags);
	}
}

static int dpram_suspend(struct platform_device *dev, pm_message_t state)
{
    DPRAM_LOG_ERR("enter suspend.\n");    

    gpio_set_value(GPIO_PDA_ACTIVE, GPIO_LEVEL_LOW);
    return 0;
}

static int dpram_resume(struct platform_device *dev)
{
    u32 reg = 0;
    u32  addr = 0;

    DPRAM_LOG_ERR("enter resume\n");    

    addr = IO_ADDRESS(0x70009000);
    reg = readl(addr);
    writel(reg | (0x4 << 4), addr); //SNOR_SEL_CS4

    gpio_set_value(GPIO_PDA_ACTIVE, GPIO_LEVEL_HIGH);
    check_miss_interrupt();
    return 0;
}

static void init_phone_active_timer(void)
{
	setup_timer( &phone_active_timer, request_phone_reset, 0 );
}

static void init_req_ack_timer(void)
{
	setup_timer( &req_ack_timer, check_miss_interrupt, 0 );
}

static int __devinit dpram_probe(struct platform_device *dev)
{
	int retval;

	printk(KERN_ERR "[DPRAM] *** Entering dpram_probe()\n");

	/* @LDK@ register dpram (tty) driver */
	retval = register_dpram_driver();
	if (retval) {
		DPRAM_LOG_ERR("Failed to register dpram (tty) driver.\n");
		return -1;
	}

#ifdef _ENABLE_ERROR_DEVICE
	/* @LDK@ register dpram error device */
	retval = register_dpram_err_device();
	if (retval) {
		DPRAM_LOG_ERR("Failed to register dpram error device.\n");
		unregister_dpram_driver();
		return -1;
	}

	init_waitqueue_head(&modem_pif_init_done_wait_q);
	init_waitqueue_head(&dpram_init_cmd_wait_q);

	memset((void *)dpram_err_buf, '\0', sizeof dpram_err_buf);
#endif /* _ENABLE_ERROR_DEVICE */

	INIT_WORK(&xmit_work_struct, NULL);
	multipdp_init();

	INIT_WORK(&cp_crash_work, NULL);	

	/* @LDK@ H/W setting */
	init_hw_setting();
	dpram_shared_bank_remap();

	/* @LDK@ initialize device table */
	init_devices();

	atomic_set(&raw_txq_req_ack_rcvd, 0);
	atomic_set(&fmt_txq_req_ack_rcvd, 0);

	skb_queue_head_init(&txq);

	/* @LDK@ register interrupt handler */
	retval = register_interrupt_handler();
	if (retval < 0) {
		DPRAM_LOG_ERR("[DPRAM] *** dpram_probe() failed to register interrupt handler.\n");
		return -1;
	}

	//dpram_platform_init();

#ifdef CONFIG_PROC_FS
	create_proc_read_entry(DRIVER_PROC_ENTRY, 0, 0, dpram_read_proc, NULL);
#endif	/* CONFIG_PROC_FS */

	/* @LDK@ check out missing interrupt from the phone */
	/* check_miss_interrupt(); */

#ifdef _ENABLE_DEBUG_PRINTS
	register_dpram_debug_control_attribute();
	register_multipdp_debug_control_attribute();
#endif
	device_create_file(multipdp_dev.this_device, &dev_attr_debug);

	init_phone_active_timer();
    init_req_ack_timer();

	printk(KERN_ERR  "[DPRAM] *** Leaving dpram_probe()\n");
	return 0;
}

static int __devexit dpram_remove(struct platform_device *dev)
{
	int ret;

#ifdef _ENABLE_DEBUG_PRINTS
	deregister_dpram_debug_control_attribute();
	deregister_multipdp_debug_control_attribute();
#endif

	/* @LDK@ unregister dpram (tty) driver */
	unregister_dpram_driver();

	/* @LDK@ unregister dpram error device */
#ifdef _ENABLE_ERROR_DEVICE
	unregister_dpram_err_device();
#endif

	/* remove app. interface device */
	multipdp_exit();

	/* @LDK@ unregister irq handler */
	free_irq(IRQ_DPRAM_INT_N, NULL);
	free_irq(IRQ_PHONE_ACTIVE, NULL);

	kill_tasklets();

	ret = del_timer(&phone_active_timer);
	if(ret) {
		DPRAM_LOG_ERR("phone_active_timer is still in use.\n");
	}

        ret = del_timer(&req_ack_timer);
	if(ret) {
		DPRAM_LOG_ERR("req_ack_timer is still in use.\n");
	}
    
	return 0;
}

static int dpram_shutdown(struct platform_device *dev)
{
	int ret;

#ifdef _ENABLE_DEBUG_PRINTS
	deregister_dpram_debug_control_attribute();
	deregister_multipdp_debug_control_attribute();
#endif

	/* @LDK@ unregister dpram (tty) driver */
	unregister_dpram_driver();

	/* @LDK@ unregister dpram error device */
#ifdef _ENABLE_ERROR_DEVICE
	unregister_dpram_err_device();
#endif

	/* remove app. interface device */
#if !defined(CONFIG_ICS)
	multipdp_exit();
#endif

	/* @LDK@ unregister irq handler */
	free_irq(IRQ_DPRAM_INT_N, NULL);
	free_irq(IRQ_PHONE_ACTIVE, NULL);

	kill_tasklets();

	ret = del_timer(&phone_active_timer);
	if(ret) {
		DPRAM_LOG_ERR("phone_active_timer is still in use.\n");
	}

        ret = del_timer(&req_ack_timer);
	if(ret) {
		DPRAM_LOG_ERR("req_ack_timer is still in use.\n");
	}

        dpram_phone_power_off();
        
	return 0;
}


static struct platform_driver platform_dpram_driver = {
	.probe		= dpram_probe,
	.remove		= __devexit_p(dpram_remove),
	.suspend	= dpram_suspend,
	.resume 	= dpram_resume,
	.shutdown = dpram_shutdown,
	.driver	= {
		.name	= "dpram-device",
	},
};

/* init & cleanup. */
static int __init dpram_init(void)
{
	wake_lock_init(&dpram_wake_lock, WAKE_LOCK_SUSPEND, "DPRAM");
	return platform_driver_register(&platform_dpram_driver);
}
static void __exit dpram_exit(void)
{
	wake_lock_destroy(&dpram_wake_lock);
	platform_driver_unregister(&platform_dpram_driver);
}

void ClearPendingInterruptFromModem(void)
{
	u16 in_interrupt = 0;
	READ_FROM_DPRAM((void *)&in_interrupt, DPRAM_PHONE2PDA_INTERRUPT_ADDRESS, sizeof(in_interrupt));
}
void TestIOCTLHandler(void)
{
	u16 in_interrupt = 0, out_interrupt = 0;

	/* READ_FROM_DPRAM((void *)&out_interrupt, DPRAM_PDA2PHONE_INTERRUPT_ADDRESS, sizeof(out_interrupt)); */
	READ_FROM_DPRAM((void *)&in_interrupt, DPRAM_PHONE2PDA_INTERRUPT_ADDRESS, sizeof(in_interrupt));

	/* send_interrupt_to_phone(in_interrupt); */

	printk(KERN_ERR "in_interrupt = 0x%x\n", in_interrupt);
	printk(KERN_ERR "out_interrupt  = 0x%x\n", out_interrupt);

	printk(KERN_ERR "Interrupt GPIO state = %x\n", gpio_get_value(GPIO_DPRAM_INT_N));

	dpram_dump_state();
}

void dpram_debug_dump_raw_read_buffer(const unsigned char  *buf, int len)
{
#ifdef PRINT_READ
	int i = 0;

	DPRAM_LOG_READ("[DPRAM] RAW READ:\n");
	for (i = 0; i < len; i++)
		DPRAM_LOG_READ( "%02x ", *((unsigned char *)buf + i));

	DPRAM_LOG_READ("\n");
#endif
}
void multipdp_debug_dump_write_buffer(const unsigned char  *buf, int len)
{
#ifdef MULTIPDP_PRINT_WRITE
	int i = 0;

	MULTIPDP_LOG_WRITE("[MULTIPDP] WRITE:\n");
	for (i = 0; i < len; i++)
		MULTIPDP_LOG_WRITE( "%02x ", *((unsigned char *)buf + i));

	MULTIPDP_LOG_WRITE("\n");
#endif
}

#ifdef _ENABLE_DEBUG_PRINTS

#define CBUF_SIZE 1024
static char cbuf[CBUF_SIZE] = {'.'};
static char tcbuf[CBUF_SIZE] = {'.'};;
static int cbuf_idx;

void dpram_debug_print_cbuf(char *strptr)
{
	int len, len1, len2;
	len = strlen(strptr);

	if (len > CBUF_SIZE) {
		printk(KERN_ERR "[DPRAM] ILLEGAL STRING\n");
		return;
	}

	if ((cbuf_idx + len) < CBUF_SIZE) {
		memcpy(&cbuf[cbuf_idx], strptr, len);
		cbuf_idx += len;
		if (cbuf_idx >= CBUF_SIZE)
			cbuf_idx = 0;
	} else {
		len1 = CBUF_SIZE - cbuf_idx;
		len2 = len - len1;
		memcpy(&cbuf[cbuf_idx], strptr, len1);
		strptr += len1;
		memcpy(cbuf, strptr, len2);
		cbuf_idx = len2;
	}
}


void dpram_debug_cbuf_reinit(void)
{
	int len;
	len = CBUF_SIZE - cbuf_idx;
	memcpy(tcbuf, &cbuf[cbuf_idx], len);
	memcpy(&tcbuf[len], cbuf, (CBUF_SIZE - len));
	tcbuf[CBUF_SIZE - 1] = 0;
	printk(KERN_ERR "%s\nComplete print\n", tcbuf);
	memset(cbuf, '.', CBUF_SIZE);
	cbuf_idx = 0;
}

static ssize_t dpram_store_debug_level(struct device_driver *ddp, const char *buf, size_t count)
{
	u16 value;
	char *after;
	value = simple_strtoul(buf, &after, 10);
	printk(KERN_ERR "[%s] value = %x\n", __func__, value);
	dpram_debug_mask = value;
	dpram_debug_mask |= (DPRAM_PRINT_ERROR | DPRAM_PRINT_WARNING); /* ERROR/WARN are always enabled */
	dpram_debug_cbuf_reinit();

	/* dpram_test_set_head_tail(); */
	return 1;
}
static ssize_t dpram_show_debug_level(struct device_driver *ddp, char *buf)
{
	printk(KERN_ERR "%s, dpram_debug_mask %x\n", __func__, dpram_debug_mask);
	return snprintf(buf, PAGE_SIZE, "%d\n", dpram_debug_mask);
}

struct driver_attribute debug_ctrl_attr;


void register_dpram_debug_control_attribute(void)
{
	debug_ctrl_attr.attr.name = "debugctrl";
	//debug_ctrl_attr.attr.owner = platform_dpram_driver.driver.owner;
	debug_ctrl_attr.attr.mode = S_IRUSR | S_IWUSR;
	debug_ctrl_attr.show = dpram_show_debug_level;
	debug_ctrl_attr.store = dpram_store_debug_level;

	cbuf_idx = 0;
	memset(cbuf, '.', CBUF_SIZE);

	driver_create_file(&platform_dpram_driver.driver, &debug_ctrl_attr);
}

void deregister_dpram_debug_control_attribute(void)
{
	driver_remove_file(&platform_dpram_driver.driver, &debug_ctrl_attr);
}

static char s_buf[1024];

void  dpram_debug_print(u8 print_prefix, u32 mask,  const char *fmt, ...)
{
	va_list args;

	if (dpram_debug_mask & mask) {
		va_start(args, fmt);
		vsprintf(s_buf, fmt, args);
		va_end(args);

		if (dpram_debug_mask & DPRAM_PRINT_CBUF) {
			dpram_debug_print_cbuf(s_buf);
		} else {
			if (print_prefix)
				printk(KERN_ERR "[DPRAM]");
			printk(s_buf);
		}
	}
}

static ssize_t mulitpdp_store_debug_level(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t len)
{
	u16 value;
	char *after;
	value = simple_strtoul(buf, &after, 10);
	printk("mulitpdp_store_debug_level, value = %x\n", value);
	mulitpdp_debug_mask = value;
	mulitpdp_debug_mask |= (MULTIPDP_PRINT_ERROR | MULTIPDP_PRINT_WARNING); // ERROR/WARN are always enabled
	printk("mulitpdp_debug_mask = %x\n", mulitpdp_debug_mask);

	return 1;
}
static ssize_t mulitpdp_show_debug_level(struct device *dev, struct device_attribute *attr, char *buf)
{
	printk("mulitpdp_show_debug_level, mulitpdp_debug_mask %x\n", mulitpdp_debug_mask);

	return snprintf(buf, PAGE_SIZE, "%d\n", mulitpdp_debug_mask);
}

static DEVICE_ATTR(multipdp_debug, S_IRUGO | S_IWUSR, mulitpdp_show_debug_level, mulitpdp_store_debug_level);
void register_multipdp_debug_control_attribute(void)
{
	device_create_file(multipdp_dev.this_device, &dev_attr_multipdp_debug);
}

void deregister_multipdp_debug_control_attribute(void)
{
	device_remove_file(multipdp_dev.this_device, &dev_attr_multipdp_debug);
}
void multipdp_debug_print(u32 mask,  const char *fmt, ...)
{
	if (mulitpdp_debug_mask & mask) {
		static char s_buf[1024];
		va_list args;

		va_start(args, fmt);
		vsprintf(s_buf, fmt, args);
		va_end(args);

		//printk("[MULTIPDP]");
		printk(s_buf);
	}
}
#endif /*_ENABLE_DEBUG_PRINTS*/

module_init(dpram_init);
module_exit(dpram_exit);

MODULE_AUTHOR("SAMSUNG ELECTRONICS CO., LTD");

MODULE_DESCRIPTION("DPRAM Device Driver.");

MODULE_LICENSE("GPL");
