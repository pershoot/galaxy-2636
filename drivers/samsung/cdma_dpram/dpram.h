/* dpram.h
 *
 * SAMSUNG TTY modem driver for VIA modem
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

#ifndef __DPRAM_H__
#define __DPRAM_H__

/* 16KB Size */
#define DPRAM_SIZE					0x4000
/* Memory Address */
#define DPRAM_START_ADDRESS 				0x0000
#define DPRAM_MAGIC_CODE_ADDRESS			(DPRAM_START_ADDRESS)
#define DPRAM_ACCESS_ENABLE_ADDRESS			(DPRAM_START_ADDRESS + 0x0002)

#define DPRAM_PDA2PHONE_FORMATTED_START_ADDRESS		(DPRAM_START_ADDRESS + 0x0004)
#define DPRAM_PDA2PHONE_FORMATTED_HEAD_ADDRESS		(DPRAM_PDA2PHONE_FORMATTED_START_ADDRESS)
#define DPRAM_PDA2PHONE_FORMATTED_TAIL_ADDRESS		(DPRAM_PDA2PHONE_FORMATTED_HEAD_ADDRESS + 0x0002)
#define DPRAM_PDA2PHONE_FORMATTED_BUFFER_ADDRESS	(DPRAM_PDA2PHONE_FORMATTED_HEAD_ADDRESS + 0x0004)
#define DPRAM_PDA2PHONE_FORMATTED_BUFFER_SIZE		(1020+1024)	/* 0x03FC */

#define DPRAM_PDA2PHONE_RAW_START_ADDRESS		(DPRAM_START_ADDRESS + 0x0404 + 1024)
#define DPRAM_PDA2PHONE_RAW_HEAD_ADDRESS		(DPRAM_PDA2PHONE_RAW_START_ADDRESS)
#define DPRAM_PDA2PHONE_RAW_TAIL_ADDRESS		(DPRAM_PDA2PHONE_RAW_HEAD_ADDRESS + 0x0002)
#define DPRAM_PDA2PHONE_RAW_BUFFER_ADDRESS		(DPRAM_PDA2PHONE_RAW_HEAD_ADDRESS + 0x0004)
#define DPRAM_PDA2PHONE_RAW_BUFFER_SIZE			(7152-1024)	/* 1BF0 */

#define DPRAM_PHONE2PDA_FORMATTED_START_ADDRESS		(DPRAM_START_ADDRESS + 0x1FF8)
#define DPRAM_PHONE2PDA_FORMATTED_HEAD_ADDRESS		(DPRAM_PHONE2PDA_FORMATTED_START_ADDRESS)
#define DPRAM_PHONE2PDA_FORMATTED_TAIL_ADDRESS		(DPRAM_PHONE2PDA_FORMATTED_HEAD_ADDRESS + 0x0002)
#define DPRAM_PHONE2PDA_FORMATTED_BUFFER_ADDRESS	(DPRAM_PHONE2PDA_FORMATTED_HEAD_ADDRESS + 0x0004)
#define DPRAM_PHONE2PDA_FORMATTED_BUFFER_SIZE		(1020+1024)	/* 0x03FC */

#define DPRAM_PHONE2PDA_RAW_START_ADDRESS		(DPRAM_START_ADDRESS + 0x23F8 + 1024)
#define DPRAM_PHONE2PDA_RAW_HEAD_ADDRESS		(DPRAM_PHONE2PDA_RAW_START_ADDRESS)
#define DPRAM_PHONE2PDA_RAW_TAIL_ADDRESS		(DPRAM_PHONE2PDA_RAW_HEAD_ADDRESS + 0x0002)
#define DPRAM_PHONE2PDA_RAW_BUFFER_ADDRESS		(DPRAM_PHONE2PDA_RAW_HEAD_ADDRESS + 0x0004)
#define DPRAM_PHONE2PDA_RAW_BUFFER_SIZE			(7152-1024)	/* 1BF0 */

#define DPRAM_PDA2PHONE_INTERRUPT_ADDRESS		(DPRAM_START_ADDRESS + 0x3FFE)
#define DPRAM_PHONE2PDA_INTERRUPT_ADDRESS		(DPRAM_START_ADDRESS + 0x3FFC)

#define DPRAM_INTERRUPT_PORT_SIZE			2

#define DPRAM_START_ADDRESS_PHYS 	 		0xD0000000
#define DPRAM_SHARED_BANK				0x00000000

#define DPRAM_SHARED_BANK_SIZE				0x4000	/* 16*1024 bytes */

#define TRUE	1
#define FALSE	0

/* interrupt masks.*/
#define INT_MASK_VALID			0x0080
#define INT_MASK_COMMAND		0x0040
#define INT_MASK_REQ_ACK_F		0x0020
#define INT_MASK_REQ_ACK_R		0x0010
#define INT_MASK_RES_ACK_F		0x0008
#define INT_MASK_RES_ACK_R		0x0004
#define INT_MASK_SEND_F			0x0002
#define INT_MASK_SEND_R			0x0001

#define INT_MASK_CMD_INIT_START		0x0001
#define INT_MASK_CMD_INIT_END		0x0002
#define INT_MASK_CMD_REQ_ACTIVE		0x0003
#define INT_MASK_CMD_RES_ACTIVE		0x0004
#define INT_MASK_CMD_REQ_TIME_SYNC	0x0005
#define INT_MASK_CMD_PHONE_START 	0x0008
#define INT_MASK_CMD_ERR_DISPLAY	0x0009
#define INT_MASK_CMD_PHONE_DEEP_SLEEP	0x000A
#define INT_MASK_CMD_NV_REBUILDING	0x000B
#define INT_MASK_CMD_EMER_DOWN		0x000C
#define INT_MASK_CMD_PIF_INIT_DONE     	0x000D
#define INT_MASK_CMD_SILENT_NV_REBUILDING	0x000E

#define INT_COMMAND(x)			(INT_MASK_VALID | INT_MASK_COMMAND | x)
#define INT_NON_COMMAND(x)		(INT_MASK_VALID | x)

/* special interrupt cmd indicating modem boot failure. */
#define INT_POWERSAFE_FAIL              0xDEAD

#define FORMATTED_INDEX			0
#define RAW_INDEX			1
#define MAX_INDEX			2

/* ioctl command definitions. */
#define IOC_MZ_MAGIC		('o')
#define DPRAM_PHONE_POWON	_IO(IOC_MZ_MAGIC, 0xd0)
#define DPRAM_PHONEIMG_LOAD	_IO(IOC_MZ_MAGIC, 0xd1)
#define DPRAM_NVDATA_LOAD	_IO(IOC_MZ_MAGIC, 0xd2)
#define DPRAM_PHONE_BOOTSTART	_IO(IOC_MZ_MAGIC, 0xd3)

struct _param_nv {
	unsigned char *addr;
	unsigned int size;
};

struct _param_em {
	unsigned int offset;
	unsigned char *addr;
	unsigned int size;
	int rw;
};

#define IOC_SEC_MAGIC		(0xf0)
#define DPRAM_PHONE_ON		_IO(IOC_SEC_MAGIC, 0xc0)
#define DPRAM_PHONE_OFF		_IO(IOC_SEC_MAGIC, 0xc1)
#define DPRAM_PHONE_GETSTATUS	_IOR(IOC_SEC_MAGIC, 0xc2, unsigned int)
#define DPRAM_PHONE_RESET	_IO(IOC_SEC_MAGIC, 0xc5)
#define DPRAM_PHONE_RAMDUMP_ON	_IO(IOC_SEC_MAGIC, 0xc6)
#define DPRAM_PHONE_RAMDUMP_OFF	_IO(IOC_SEC_MAGIC, 0xc7)
#define DPRAM_EXTRA_MEM_RW	_IOWR(IOC_SEC_MAGIC, 0xc8, unsigned long)
#define DPRAM_MEM_RW		_IOWR(IOC_SEC_MAGIC, 0xc9, unsigned long)
#define DPRAM_TEST		_IO(IOC_SEC_MAGIC, 0xcf) /* TODO: reimplement. Dummy for now */


#define IOC_MZ2_MAGIC		(0xC1)
#define HN_PDP_ACTIVATE		_IOWR(IOC_MZ2_MAGIC, 0xe0, pdp_arg_t)
#define HN_PDP_DEACTIVATE	_IOW(IOC_MZ2_MAGIC, 0xe1, pdp_arg_t)
#define HN_PDP_ADJUST		_IOW(IOC_MZ2_MAGIC, 0xe2, int)
#define HN_PDP_TXSTART		_IO(IOC_MZ2_MAGIC, 0xe3)
#define HN_PDP_TXSTOP		_IO(IOC_MZ2_MAGIC, 0xe4)
#define HN_PDP_SETRADIO		_IOW(IOC_MZ2_MAGIC, 0xe5,int)
#define HN_PDP_DATASTATUS	_IOW(IOC_MZ2_MAGIC, 0xe6,int)
#define HN_PDP_FLUSH_WORK	_IO(IOC_MZ2_MAGIC, 0xe7)

/* structure definitions. */
typedef struct dpram_serial {
	/* pointer to the tty for this device */
	struct tty_struct *tty;
	/* number of times this port has been opened */
	int open_count;
	/* locks this structure */
	struct semaphore sem;
} dpram_serial_t;

typedef struct dpram_device {
	/* DPRAM memory addresses */
	unsigned long in_head_addr;
	unsigned long in_tail_addr;
	unsigned long in_buff_addr;
	unsigned long in_buff_size;

	unsigned long out_head_addr;
	unsigned long out_tail_addr;
	unsigned long out_buff_addr;
	unsigned long out_buff_size;

	unsigned int in_head_saved;
	unsigned int in_tail_saved;
	unsigned int out_head_saved;
	unsigned int out_tail_saved;

	u_int16_t mask_req_ack;
	u_int16_t mask_res_ack;
	u_int16_t mask_send;

	dpram_serial_t serial;
} dpram_device_t;

typedef struct dpram_tasklet_data {
	dpram_device_t *device;
	u_int16_t non_cmd;
} dpram_tasklet_data_t;

struct _mem_param {
	unsigned short addr;
	unsigned long data;
	int dir;
};

#define BIT0	0x00000001
#define BIT1	0x00000002
#define BIT2	0x00000004
#define BIT3	0x00000008
#define BIT4	0x00000010
#define BIT5	0x00000020
#define BIT6	0x00000040
#define BIT7	0x00000080
#define BIT8	0x00000100
#define BIT9	0x00000200
#define BIT10	0x00000400
#define BIT11	0x00000800
#define BIT12	0x00001000
#define BIT13	0x00002000
#define BIT14	0x00004000
#define BIT15	0x00008000
#define BIT16	0x00010000
#define BIT17	0x00020000
#define BIT18	0x00040000
#define BIT19	0x00080000
#define BIT20	0x00100000
#define BIT21	0x00200000
#define BIT22	0x00400000
#define BIT23	0x00800000
#define BIT24	0x01000000
#define BIT25	0x02000000
#define BIT26	0x04000000
#define BIT27	0x08000000
#define BIT28	0x10000000
#define BIT29	0x20000000
#define BIT30	0x40000000
#define BIT31	0x80000000

#define CIRC_CNT(head,tail,size) ((head) < (tail) ? \
		        (head) + (size) - (tail) : (head) - (tail))
#define CIRC_SPACE(head,tail,size) CIRC_CNT((tail),((head)+1),(size))
#define CIRC_CNT_TO_END(head,tail,size) ((head) < (tail) ? \
		        (size) - (tail) : (head) - (tail))
#define CIRC_SPACE_TO_END(head,tail,size) ((head) < (tail) ? \
		        (tail) - (head) - 1 : (size) - (head) - !(tail))

void tegra_init_snor();

#endif	/* __DPRAM_H__ */
