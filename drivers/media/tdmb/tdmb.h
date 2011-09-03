#ifndef _TDMB_H_
#define _TDMB_H_
/*
 * tdmb.h
 *
 * - klaatu
 *
 *
*/
#include <linux/types.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <mach/gpio.h>
#include <media/tdmb_plat.h>

typedef struct{
	int b_isTDMB_Enable;
} tdmb_type;

#define TDMB_DEBUG

#ifdef TDMB_DEBUG
#define DPRINTK(x...) printk(KERN_DEBUG "TDMB " x)
#else
#define DPRINTK(x...) /* null */
#endif

#define TDMB_DEV_NAME	"tdmb"
#define TDMB_DEV_MAJOR	225
#define TDMB_DEV_MINOR	0


#define DMB_TS_COUNT		40
#define DMB_TS_SIZE			188

#define GDM_TS_BUF_MAX		(DMB_TS_SIZE*DMB_TS_COUNT)

#define GDM_MSC_BUF_MAX		(188*40)
#define GDM_DM_BUF_MAX		(512)
#define GDM_FIC_BUF_MAX		(384)


#define TS_BUFFER_SIZE		GDM_TS_BUF_MAX

#define TDMB_RING_BUFFER_SIZE			(188 * 100 + 4 + 4)
#define TDMB_RING_BUFFER_MAPPING_SIZE	\
		(((TDMB_RING_BUFFER_SIZE - 1) / PAGE_SIZE + 1) * PAGE_SIZE)

/* commands */
#define IOCTL_MAGIC	't'
#define IOCTL_MAXNR			32

#define IOCTL_TDMB_GET_DATA_BUFFSIZE            _IO( IOCTL_MAGIC, 0 )
#define IOCTL_TDMB_GET_CMD_BUFFSIZE             _IO( IOCTL_MAGIC, 1 )
#define IOCTL_TDMB_POWER_ON                      _IO( IOCTL_MAGIC, 2 )
#define IOCTL_TDMB_POWER_OFF                     _IO( IOCTL_MAGIC, 3 )
#define IOCTL_TDMB_SCAN_FREQ_ASYNC              _IO( IOCTL_MAGIC, 4 )
#define IOCTL_TDMB_SCAN_FREQ_SYNC               _IO( IOCTL_MAGIC, 5 )
#define IOCTL_TDMB_SCANSTOP                      _IO( IOCTL_MAGIC, 6 )
#define IOCTL_TDMB_ASSIGN_CH                     _IO( IOCTL_MAGIC, 7 )
#define IOCTL_TDMB_GET_DM                        _IO( IOCTL_MAGIC, 8 )
#define IOCTL_TDMB_ASSIGN_CH_TEST               _IO( IOCTL_MAGIC, 9 )

typedef struct{
    unsigned int    rssi;
    unsigned int    BER;
    unsigned int    PER;
    unsigned int    antenna;
} tdmb_dm;

#define MAX_ENSEMBLE_NUM             21
#define SUB_CH_NUM_MAX               64

#define ENSEMBLE_LABEL_SIZE_MAX      16
#define SERVICE_LABEL_SIZE_MAX       16
#define USER_APPL_DATA_SIZE_MAX      24
#define USER_APPL_NUM_MAX            12

typedef enum TMID_type{
    TMID_MSC_STREAM_AUDIO       = 0x00,
    TMID_MSC_STREAM_DATA        = 0x01,
    TMID_FIDC                   = 0x02,
    TMID_MSC_PACKET_DATA        = 0x03
} TMID_TYPE;

typedef enum{ 
    DSCTy_TDMB                  = 0x18,
    DSCTy_UNSPECIFIED           = 0x00 //Used for All-Zero Test
} DSCTy_TYPE;

typedef struct tag_SubChInfoType {
	/* Sub Channel Information */
	unsigned char SubChID; /* 6 bits */
	unsigned short StartAddress; /* 10 bits */
	/* 1 bit, 7/15 bits (Form,Size,protection level) */
	unsigned short FormSizeProtectionlevel;

	/* FIG 0/2  */
	unsigned char TMId; /* 2 bits */
	unsigned char Type; /* 6 bits */
	unsigned long ServiceID; /* 16/32 bits */
	unsigned char ServiceLabel[SERVICE_LABEL_SIZE_MAX]; /* 16*8 bits */
#if 0
	unsigned char PrimarySecondary;
#endif

	/* FIG 0/3 */
#if 0
	 unsigned short ServiceComponentID; /* /12 bits */
	 useless unsigned short PacketAddress; /* FIG 0/8 */
	 useless unsigned char SCIds;
#endif

	/* FIG 0/13 */
#if 0
	unsigned char NumberofUserAppl; /* MAX 12 */
	unsigned short UserApplType[USER_APPL_NUM_MAX];
	unsigned char UserApplLength[USER_APPL_NUM_MAX];
	/* max size 24 bytes */
	unsigned char UserApplData[USER_APPL_NUM_MAX][USER_APPL_DATA_SIZE_MAX];
#endif

#if 0
	 unsigned char bVisualRadio; /* 1 bits */
#endif
} SubChInfoType;

typedef struct tag_EnsembleInfoType {
	unsigned long EnsembleFrequency;	/* 4 bytes */
	unsigned char TotalSubChNumber;	/* MAX: 64 */

	unsigned short EnsembleID;
	unsigned char EnsembleLabelCharField[ENSEMBLE_LABEL_SIZE_MAX+1];
	SubChInfoType SubChInfo[SUB_CH_NUM_MAX];
} EnsembleInfoType;


#define TDMB_CMD_START_FLAG		0x7F
#define TDMB_CMD_END_FLAG		0x7E
#define TDMB_CMD_SIZE			30

/* Result Value */
#define DMB_FIC_RESULT_FAIL	    0x00
#define DMB_FIC_RESULT_DONE	    0x01
#define DMB_TS_PACKET_RESYNC    0x02

int tdmb_init_bus(void);
void tdmb_exit_bus(void);
irqreturn_t tdmb_irq_handler(int irq, void *dev_id);
unsigned long tdmb_get_chinfo(void);
void tdmb_pull_data(void);
bool tdmb_create_workqueue(void);
bool tdmb_destroy_workqueue(void);
bool tdmb_create_databuffer(unsigned long int_size);
void tdmb_destroy_databuffer(void);
void tdmb_init_data(void);
unsigned char tdmb_make_result
(
	unsigned char byCmd,
	unsigned short byDataLength,
	unsigned char  *pbyData
);
bool tdmb_store_data(unsigned char *pData, unsigned long len);

typedef struct {
	bool (*power_on) (void);
	void (*power_off) (void);
	bool (*scan_ch) (EnsembleInfoType *ensembleInfo, unsigned long freqHz);
	void (*get_dm) (tdmb_dm *info);
	bool (*set_ch) (unsigned long freqHz, unsigned char subchid, bool factory_test);
	void (*pull_data) (void);
	unsigned long (*get_int_size) (void);
} TDMBDrvFunc;

TDMBDrvFunc * tdmb_get_drv_func(struct tdmb_platform_data * gpio);

#endif
