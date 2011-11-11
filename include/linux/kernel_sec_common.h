/*
 * include/linux/kernel_sec_common.h
 *
 * Copyright (c) 2010 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _KERNEL_SEC_COMMON_H_
#define _KERNEL_SEC_COMMON_H_

#include <asm/io.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/sched.h>

// MAGIC_CODE in LOKE
// you have to use this vitrual address with consideration
//#define LOKE_BOOT_USB_DWNLD_V_ADDR  0xC1000000
#define LOKE_BOOT_USB_DWNLD_V_ADDR	(0xE0000000 - 4)		/* Magic number physic start address 0x18BFFFFC */
#define LOKE_BOOT_USB_DWNLD_P_ADDR	(0x20000000 - 4)		/* Magic number physic start address 0x18BFFFFC */
#define LOKE_BOOT_USB_DWNLDMAGIC_NO	0x66262564
#define KERNEL_SEC_UPLOAD_CAUSE_V_ADDR	(0xE0000000 - 8)  /* Magic code virtual addr for upload cause */
#define KERNEL_SEC_UPLOAD_CAUSE_P_ADDR	(0x20000000 - 8)  /* Magic code physical addr for upload cause */



#define KERNEL_SEC_DUMP_AP_DEAD_INDICATOR      0xABCD00C9    // AP -> CP : AP Crash Ind
#define KERNEL_SEC_DUMP_AP_DEAD_ACK      0xCACAEDED   // CP -> AP : CP ready for uplaod mode. 

#define KERNEL_SEC_DEBUG_CAUSE_STR_LEN   65 //it's longer than DPRAM_ERR_MSG_LEN, in dpram.h
#define KERNEL_SEC_DEBUG_LEVEL_LOW	(0x574F4C44)
#define KERNEL_SEC_DEBUG_LEVEL_MID	(0x44494D44)
#define KERNEL_SEC_DEBUG_LEVEL_HIGH	(0x47494844)

//WDOG register
#define S3C_PA_WDT                  0xE2700000

// klaatu - schedule log
#define SCHED_LOG_MAX 2000

typedef struct {
    void * dummy;
    void * fn;
}irq_log_t;

typedef union {
    char task[TASK_COMM_LEN];
    irq_log_t irq;
}task_log_t;

typedef struct {
    unsigned long long time;
    task_log_t log;
}sched_log_t;

extern sched_log_t gExcpTaskLog[SCHED_LOG_MAX];
extern unsigned int gExcpTaskLogIdx;

typedef struct tag_mmu_info
{	
	int SCTLR;
	int TTBR0;
	int TTBR1;
	int TTBCR;
	int DACR;
	int DFSR;
	int DFAR;
	int IFSR;
	int IFAR;
	int DAFSR;
	int IAFSR;
	int PMRRR;
	int NMRRR;
	int FCSEPID;
	int CONTEXT;
	int URWTPID;
	int UROTPID;
	int POTPIDR;
}t_kernel_sec_mmu_info;

/*ARM CORE regs mapping structure*/
typedef struct
{
	/* COMMON */
	unsigned int r0;
	unsigned int r1;
	unsigned int r2;
	unsigned int r3;
	unsigned int r4;
	unsigned int r5;
	unsigned int r6;
	unsigned int r7;
	unsigned int r8;
	unsigned int r9;
	unsigned int r10;
	unsigned int r11;
	unsigned int r12;

	/* SVC */
	unsigned int r13_svc;
	unsigned int r14_svc;
	unsigned int spsr_svc;

	/* PC & CPSR */
	unsigned int pc;
	unsigned int cpsr;
	
	/* USR/SYS */
	unsigned int r13_usr;
	unsigned int r14_usr;

	/* FIQ */
	unsigned int r8_fiq;
	unsigned int r9_fiq;
	unsigned int r10_fiq;
	unsigned int r11_fiq;
	unsigned int r12_fiq;
	unsigned int r13_fiq;
	unsigned int r14_fiq;
	unsigned int spsr_fiq;

	/* IRQ */
	unsigned int r13_irq;
	unsigned int r14_irq;
	unsigned int spsr_irq;

	/* MON */
	unsigned int r13_mon;
	unsigned int r14_mon;
	unsigned int spsr_mon;

	/* ABT */
	unsigned int r13_abt;
	unsigned int r14_abt;
	unsigned int spsr_abt;

	/* UNDEF */
	unsigned int r13_und;
	unsigned int r14_und;
	unsigned int spsr_und;

}t_kernel_sec_arm_core_regsiters;

typedef enum
{
	UPLOAD_CAUSE_INIT		= 0x00,
	UPLOAD_CAUSE_KERNEL_PANIC	= 0xC8,
	UPLOAD_CAUSE_EXT4_ERROR		= 0xC9,
	UPLOAD_CAUSE_FORCED_UPLOAD	= 0x22,
	UPLOAD_CAUSE_CP_ERROR_FATAL	= 0xCC,
	UPLOAD_CAUSE_LTE_ERROR_FATAL    = 0xCF, //added for LTE
	UPLOAD_CAUSE_USER_FAULT		= 0x2F
} kernel_sec_upload_cause_type;

#ifdef CONFIG_TARGET_LOCALE_KOR// klaatu
#ifdef CONFIG_KERNEL_DEBUG_SEC
typedef struct {
	char Magic[4];
	char BuildRev[12];
	char BuildDate[12];
	char BuildTime[9];
	void *Excp_reserve1;
	void *Excp_reserve2;
	void *Excp_reserve3;
	void *Excp_reserve4;
}gExcpDebugInfo_t;

#endif /* CONFIG_KERNEL_DEBUG_SEC */
#endif /* CONFIG_TARGET_LOCALE_KOR */

#define KERNEL_SEC_UPLOAD_CAUSE_MASK     0x000000FF
#define KERNEL_SEC_UPLOAD_AUTOTEST_BIT   31
#define KERNEL_SEC_UPLOAD_AUTOTEST_MASK  (1<<KERNEL_SEC_UPLOAD_AUTOTEST_BIT)

#define KERNEL_SEC_DEBUG_LEVEL_BIT   29
#define KERNEL_SEC_DEBUG_LEVEL_MASK  (3<<KERNEL_SEC_DEBUG_LEVEL_BIT)

extern void __iomem * kernel_sec_viraddr_wdt_reset_reg;
extern void kernel_sec_map_wdog_reg(void);

extern void kernel_sec_set_cp_upload(void);
extern void kernel_sec_set_cp_ack(void);
extern void kernel_sec_set_upload_magic_number(void);
extern void kernel_sec_set_upload_cause(kernel_sec_upload_cause_type uploadType);
extern kernel_sec_upload_cause_type kernel_sec_get_upload_cause(void);
extern void kernel_sec_set_cause_strptr(unsigned char* str_ptr, int size);
extern void kernel_sec_set_autotest(void);
extern void kernel_sec_clear_upload_magic_number(void);
extern void kernel_sec_set_build_info(void);

extern void kernel_sec_hw_reset(bool bSilentReset);
extern void kernel_sec_init(void);

extern void kernel_sec_get_core_reg_dump(t_kernel_sec_arm_core_regsiters* regs);
extern int  kernel_sec_get_mmu_reg_dump(t_kernel_sec_mmu_info *mmu_info);
extern void kernel_sec_save_final_context(void);

typedef struct _sec_param_data {
	unsigned int signature;
	unsigned int size;
	unsigned int oemlock;
	unsigned int sud;
	unsigned int secure;
	unsigned int fusetrigger;
	unsigned int sbk[4];
#ifdef param_test
	unsigned int test;
#endif
	unsigned int debuglevel;
	unsigned int uartsel;
	unsigned int usbsel;
} sec_param_data;

typedef enum
{
	param_index_oemlock,
	param_index_sud,
	param_index_secure,
	param_index_fusetrigger,
	param_index_sbk,
#ifdef param_test
	param_index_test,
#endif
	param_index_debuglevel,
	param_index_uartsel,
	param_index_usbsel,
} sec_param_index;

extern bool sec_open_param(void);
extern bool sec_get_param(sec_param_index index, void *value);
extern bool sec_set_param(sec_param_index index, void *value);

extern bool kernel_sec_set_debug_level(int level);
extern int kernel_sec_get_debug_level(void);

extern void dump_all_task_info(void);
extern void dump_cpu_stat(void);

#define KERNEL_SEC_LEN_BUILD_TIME 16
#define KERNEL_SEC_LEN_BUILD_DATE 16

#endif /* _KERNEL_SEC_COMMON_H_ */
