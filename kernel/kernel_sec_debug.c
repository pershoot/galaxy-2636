/*
 *  linux/kernel/kernel_sec_debug.c
 *
 * Copyright (c) 2010 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifdef CONFIG_KERNEL_DEBUG_SEC

#include <linux/kernel_sec_common.h>
#include <asm/cacheflush.h>           // cacheflush
#include <linux/errno.h>
#include <linux/ctype.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#include <linux/file.h>
#include <mach/hardware.h>
#include <linux/kernel_stat.h>
#include <linux/reboot.h>

#include <linux/kallsyms.h>
#include <linux/ptrace.h>
#include <linux/ratelimit.h>

#ifndef arch_irq_stat_cpu
#define arch_irq_stat_cpu(cpu) 0
#endif

#ifndef arch_irq_stat
#define arch_irq_stat() 0
#endif

#ifndef arch_idle_time
#define arch_idle_time(cpu) 0
#endif

void dump_all_task_info(void);
void dump_cpu_stat(void);


/*
 *  Variable
 */

const char* gkernel_sec_build_info_date_time[] =
{
	__DATE__,
	__TIME__
};

static int debuglevel;

// klaatu
sched_log_t gExcpTaskLog[SCHED_LOG_MAX];
unsigned int gExcpTaskLogIdx = 0;

typedef enum {	
	__SERIAL_SPEED,
	__LOAD_RAMDISK,
	__BOOT_DELAY,
	__LCD_LEVEL,
	__SWITCH_SEL,
	__PHONE_DEBUG_ON,
	__LCD_DIM_LEVEL,
	__MELODY_MODE,
	__REBOOT_MODE,
	__NATION_SEL,
	__SET_DEFAULT_PARAM,
	__PARAM_INT_11,
	__PARAM_INT_12,
	__PARAM_INT_13,
	__PARAM_INT_14,
	__VERSION,
	__CMDLINE,
	__PARAM_STR_2,
	__PARAM_STR_3,
	__PARAM_STR_4
} param_idx;

char gkernel_sec_build_info[100];
unsigned char  kernel_sec_cause_str[KERNEL_SEC_DEBUG_CAUSE_STR_LEN];

/*
 *  Function
 */

//void __iomem * kernel_sec_viraddr_wdt_reset_reg;
__used t_kernel_sec_arm_core_regsiters kernel_sec_core_reg_dump;
__used t_kernel_sec_mmu_info           kernel_sec_mmu_reg_dump;
__used kernel_sec_upload_cause_type     gkernel_sec_upload_cause;

/*
volatile void __iomem *dpram_base = 0;
volatile unsigned int *onedram_sem;
volatile unsigned int *onedram_mailboxAB;		//received mail
volatile unsigned int *onedram_mailboxBA;		//send mail
unsigned int received_cp_ack = 0;
*/

//extern void (*sec_set_param_value)(int idx, void *value);
//extern void (*sec_get_param_value)(int idx, void *value);

void kernel_sec_set_cp_upload(void)
{
	/*
	unsigned int send_mail, wait_count;

	send_mail = KERNEL_SEC_DUMP_AP_DEAD_INDICATOR;

	*onedram_sem = 0x0;
	*onedram_mailboxBA = send_mail;

	printk(KERN_EMERG"[kernel_sec_dump_set_cp_upload] set cp upload mode, MailboxBA 0x%8x\n", send_mail);

	wait_count = 0;
	received_cp_ack = 0;
	while(1)
	{
		if(received_cp_ack == 1)
		{
			printk(KERN_EMERG"  - Done.\n");
			break;
		}
		mdelay(10);
		if(++wait_count > 500)
		{
			printk(KERN_EMERG"  - Fail to set CP uploadmode.\n");
			break;
		}
	}
	printk(KERN_EMERG" modem_wait_count : %d \n", wait_count);
	*/
}
EXPORT_SYMBOL(kernel_sec_set_cp_upload);


void kernel_sec_set_cp_ack(void)   //is set by dpram - dpram_irq_handler
{
//	received_cp_ack = 1;
}
EXPORT_SYMBOL(kernel_sec_set_cp_ack);


#if defined(CONFIG_MACH_SAMSUNG_P5)
static void __iomem *kernel_sec_get_pointer_upload_magic_number(void)
{
	static void __iomem *upload_magic_number_addr = 0;

	if (upload_magic_number_addr)
		return upload_magic_number_addr;
	else {
		upload_magic_number_addr = ioremap(LOKE_BOOT_USB_DWNLD_P_ADDR, 4);
		return upload_magic_number_addr;
	}
}
#endif

void kernel_sec_set_upload_magic_number(void)
{
	void __iomem *to_io;
#if defined(CONFIG_MACH_SAMSUNG_P5)
	to_io = kernel_sec_get_pointer_upload_magic_number();
#else
	to_io = ioremap(LOKE_BOOT_USB_DWNLD_P_ADDR, 4);
#endif
	writel(0xFFFFFFFF, to_io);
//	iounmap(to_io);
}
EXPORT_SYMBOL(kernel_sec_set_upload_magic_number);


void kernel_sec_clear_upload_magic_number(void)
{
	void __iomem *to_io;
#if defined(CONFIG_MACH_SAMSUNG_P5)
	to_io = kernel_sec_get_pointer_upload_magic_number();
#else
	to_io = ioremap(LOKE_BOOT_USB_DWNLD_P_ADDR, 4);
#endif
	writel(0xFFFFFFFF, to_io);
}
EXPORT_SYMBOL(kernel_sec_clear_upload_magic_number);

void kernel_sec_map_wdog_reg(void)
{
	/* Virtual Mapping of Watchdog register */
	/*
	kernel_sec_viraddr_wdt_reset_reg = ioremap_nocache(S3C_PA_WDT,0x400);

	if (kernel_sec_viraddr_wdt_reset_reg == NULL)
	{
		printk(KERN_EMERG"Failed to ioremap() region in forced upload keystring\n");
	}
	*/
}
EXPORT_SYMBOL(kernel_sec_map_wdog_reg);

#if defined(CONFIG_MACH_SAMSUNG_P5)
static void __iomem *kernel_sec_get_pointer_upload_cause(void)
{
	static void __iomem *upload_cause_addr = 0;

	if (upload_cause_addr)
		return upload_cause_addr;
	else {
		upload_cause_addr = ioremap(KERNEL_SEC_UPLOAD_CAUSE_P_ADDR, 4);
		return upload_cause_addr;
	}
}
#endif

void kernel_sec_set_upload_cause(kernel_sec_upload_cause_type upload_type)
{
	unsigned int upload;
	void __iomem *to_io;

	gkernel_sec_upload_cause = upload_type;

	switch (upload_type) {
	case UPLOAD_CAUSE_INIT:
		pr_emerg("[DBG] upload cause : UPLOAD_CAUSE_INIT\n");
		break;
	case UPLOAD_CAUSE_KERNEL_PANIC:
		pr_emerg("[DBG] upload cause : UPLOAD_CAUSE_KERNEL_PANIC\n");
		break;
	case UPLOAD_CAUSE_FORCED_UPLOAD:
		pr_emerg("[DBG] upload cause : UPLOAD_CAUSE_FORCED_UPLOAD\n");
		break;
	case UPLOAD_CAUSE_CP_ERROR_FATAL:
		pr_emerg("[DBG] upload cause : UPLOAD_CAUSE_CP_ERROR_FATAL\n");
		break;
	case UPLOAD_CAUSE_LTE_ERROR_FATAL:
		pr_emerg("[DBG] upload cause : UPLOAD_CAUSE_LTE_ERROR_FATAL\n");
		break;        
	case UPLOAD_CAUSE_USER_FAULT:
		pr_emerg("[DBG] upload cause : UPLOAD_CAUSE_USER_FAULT\n");
		break;
	default:
		pr_emerg("[DBG] upload cause : UNKNOWN \n");
	}

#if defined(CONFIG_MACH_SAMSUNG_P5)
	to_io = kernel_sec_get_pointer_upload_cause();
#else
	to_io = ioremap(KERNEL_SEC_UPLOAD_CAUSE_P_ADDR, 4);
#endif

	upload = readl(to_io);
	upload &= ~KERNEL_SEC_UPLOAD_CAUSE_MASK;
	upload |= upload_type;
	writel(0xFFFFFFFF, to_io);

	pr_emerg("(kernel_sec_set_upload_cause) : upload_cause set %x\n",
			upload_type);
#if !defined(CONFIG_MACH_SAMSUNG_P5)
	iounmap(to_io);
#endif
}
EXPORT_SYMBOL(kernel_sec_set_upload_cause);

kernel_sec_upload_cause_type kernel_sec_get_upload_cause(void)
{
	unsigned long upload;
	void __iomem *to_io;

#if defined(CONFIG_MACH_SAMSUNG_P5)
	to_io = kernel_sec_get_pointer_upload_cause();
#else
	to_io = ioremap(KERNEL_SEC_UPLOAD_CAUSE_P_ADDR, 4);
#endif
	upload = readl(to_io);
	upload &= KERNEL_SEC_UPLOAD_CAUSE_MASK;
#if !defined(CONFIG_MACH_SAMSUNG_P5)
	iounmap(to_io);
#endif
	return (kernel_sec_upload_cause_type)upload;
}
EXPORT_SYMBOL(kernel_sec_get_upload_cause);

void kernel_sec_set_cause_strptr(unsigned char* str_ptr, int size)
{
	unsigned int temp;

	memset((void *)kernel_sec_cause_str, 0, sizeof(kernel_sec_cause_str));
	memcpy(kernel_sec_cause_str, str_ptr, size);

	temp = virt_to_phys(kernel_sec_cause_str);
	__raw_writel(temp, LOKE_BOOT_USB_DWNLD_V_ADDR+4); //loke read this ptr, display_aries_upload_image
}
EXPORT_SYMBOL(kernel_sec_set_cause_strptr);


void kernel_sec_set_autotest(void)
{
/*
	unsigned int temp;

	temp = __raw_readl(S5P_INFORM6);
	temp |= 1<<KERNEL_SEC_UPLOAD_AUTOTEST_BIT;
	__raw_writel( temp , S5P_INFORM6 );	
*/
}
EXPORT_SYMBOL(kernel_sec_set_autotest);

void kernel_sec_set_build_info(void)
{
	char * p = gkernel_sec_build_info;
	sprintf(p,"P3_BUILD_INFO: HWREV: %x",system_rev);
	strcat(p, " Date:");
	strcat(p, gkernel_sec_build_info_date_time[0]);
	strcat(p, " Time:");
	strcat(p, gkernel_sec_build_info_date_time[1]);
}
EXPORT_SYMBOL(kernel_sec_set_build_info);

void kernel_sec_init(void)
{
	/*
	// set the onedram mailbox virtual address
	dpram_base = ioremap_nocache(0x30000000 + 0x05000000 + 0xFFF800, 0x60); //DPRAM_START_ADDRESS_PHYS + DPRAM_SHARED_BANK + DPRAM_SMP 

	if (dpram_base == NULL) {
		printk(KERN_EMERG"failed ioremap\n");
	}
	onedram_sem = dpram_base ; 
	onedram_mailboxAB = dpram_base + 0x20;
	onedram_mailboxBA = dpram_base + 0x40;
	*/
	kernel_sec_set_upload_magic_number();
	kernel_sec_set_upload_cause(UPLOAD_CAUSE_INIT);	
	//kernel_sec_map_wdog_reg();
}
EXPORT_SYMBOL(kernel_sec_init);

/* core reg dump function*/
void kernel_sec_get_core_reg_dump(t_kernel_sec_arm_core_regsiters* regs)
{
	asm(
		// we will be in SVC mode when we enter this function. Collect SVC registers along with cmn registers.
		"str r0, [%0,#0] \n\t"		// R0
		"str r1, [%0,#4] \n\t"		// R1
		"str r2, [%0,#8] \n\t"		// R2
		"str r3, [%0,#12] \n\t"		// R3
		"str r4, [%0,#16] \n\t"		// R4
		"str r5, [%0,#20] \n\t"		// R5
		"str r6, [%0,#24] \n\t"		// R6
		"str r7, [%0,#28] \n\t"		// R7
		"str r8, [%0,#32] \n\t"		// R8
		"str r9, [%0,#36] \n\t"		// R9
		"str r10, [%0,#40] \n\t"	// R10
		"str r11, [%0,#44] \n\t"	// R11
		"str r12, [%0,#48] \n\t"	// R12

		/* SVC */
		"str r13, [%0,#52] \n\t"	// R13_SVC
		"str r14, [%0,#56] \n\t"	// R14_SVC
		"mrs r1, spsr \n\t"			// SPSR_SVC
		"str r1, [%0,#60] \n\t"

		/* PC and CPSR */
		"sub r1, r15, #0x4 \n\t"	// PC
		"str r1, [%0,#64] \n\t"	
		"mrs r1, cpsr \n\t"			// CPSR
		"str r1, [%0,#68] \n\t"

		/* SYS/USR */
		"mrs r1, cpsr \n\t"			// switch to SYS mode
		"and r1, r1, #0xFFFFFFE0\n\t"
		"orr r1, r1, #0x1f \n\t"
		"msr cpsr,r1 \n\t"

		"str r13, [%0,#72] \n\t"	// R13_USR
		"str r14, [%0,#76] \n\t"	// R13_USR

		/*FIQ*/
		"mrs r1, cpsr \n\t"			// switch to FIQ mode
		"and r1,r1,#0xFFFFFFE0\n\t"
		"orr r1,r1,#0x11\n\t"
		"msr cpsr,r1 \n\t"

		"str r8, [%0,#80] \n\t"		// R8_FIQ
		"str r9, [%0,#84] \n\t"		// R9_FIQ
		"str r10, [%0,#88] \n\t"	// R10_FIQ
		"str r11, [%0,#92] \n\t"	// R11_FIQ
		"str r12, [%0,#96] \n\t"	// R12_FIQ
		"str r13, [%0,#100] \n\t"	// R13_FIQ
		"str r14, [%0,#104] \n\t"	// R14_FIQ
		"mrs r1, spsr \n\t"			// SPSR_FIQ
		"str r1, [%0,#108] \n\t"

		/*IRQ*/
		"mrs r1, cpsr \n\t"			// switch to IRQ mode
		"and r1, r1, #0xFFFFFFE0\n\t"
		"orr r1, r1, #0x12\n\t"
		"msr cpsr,r1 \n\t"

		"str r13, [%0,#112] \n\t"	// R13_IRQ
		"str r14, [%0,#116] \n\t"	// R14_IRQ
		"mrs r1, spsr \n\t"			// SPSR_IRQ
		"str r1, [%0,#120] \n\t"

		/*MON*/
		"mrs r1, cpsr \n\t"			// switch to monitor mode
		"and r1, r1, #0xFFFFFFE0\n\t"
		"orr r1, r1, #0x16\n\t"
		"msr cpsr,r1 \n\t"

		"str r13, [%0,#124] \n\t"	// R13_MON
		"str r14, [%0,#128] \n\t"	// R14_MON
		"mrs r1, spsr \n\t"			// SPSR_MON
		"str r1, [%0,#132] \n\t"

		/*ABT*/
		"mrs r1, cpsr \n\t"			// switch to Abort mode
		"and r1, r1, #0xFFFFFFE0\n\t"
		"orr r1, r1, #0x17\n\t"
		"msr cpsr,r1 \n\t"

		"str r13, [%0,#136] \n\t"	// R13_ABT
		"str r14, [%0,#140] \n\t"	// R14_ABT
		"mrs r1, spsr \n\t"			// SPSR_ABT
		"str r1, [%0,#144] \n\t"

		/*UND*/
		"mrs r1, cpsr \n\t"			// switch to undef mode
		"and r1, r1, #0xFFFFFFE0\n\t"
		"orr r1, r1, #0x1B\n\t"
		"msr cpsr,r1 \n\t"

		"str r13, [%0,#148] \n\t"	// R13_UND
		"str r14, [%0,#152] \n\t"	// R14_UND
		"mrs r1, spsr \n\t"			// SPSR_UND
		"str r1, [%0,#156] \n\t"

		/* restore to SVC mode */
		"mrs r1, cpsr \n\t"			// switch to undef mode
		"and r1, r1, #0xFFFFFFE0\n\t"
		"orr r1, r1, #0x13\n\t"
		"msr cpsr,r1 \n\t"
		
		:				/* output */
        :"r"(regs)    	/* input */
        :"%r1"     		/* clobbered register */
        );

	return;	
}
EXPORT_SYMBOL(kernel_sec_get_core_reg_dump);

int kernel_sec_get_mmu_reg_dump(t_kernel_sec_mmu_info *mmu_info)
{
// CR DAC TTBR0 TTBR1 TTBCR
	asm("mrc    p15, 0, r1, c1, c0, 0 \n\t"	//SCTLR
		"str r1, [%0] \n\t"
		"mrc    p15, 0, r1, c2, c0, 0 \n\t"	//TTBR0
		"str r1, [%0,#4] \n\t"
		"mrc    p15, 0, r1, c2, c0,1 \n\t"	//TTBR1
		"str r1, [%0,#8] \n\t"
		"mrc    p15, 0, r1, c2, c0,2 \n\t"	//TTBCR
		"str r1, [%0,#12] \n\t"
		"mrc    p15, 0, r1, c3, c0,0 \n\t"	//DACR
		"str r1, [%0,#16] \n\t"
		
		"mrc    p15, 0, r1, c5, c0,0 \n\t"	//DFSR
		"str r1, [%0,#20] \n\t"
		"mrc    p15, 0, r1, c6, c0,0 \n\t"	//DFAR
		"str r1, [%0,#24] \n\t"
		"mrc    p15, 0, r1, c5, c0,1 \n\t"	//IFSR
		"str r1, [%0,#28] \n\t"
		"mrc    p15, 0, r1, c6, c0,2 \n\t"	//IFAR
		"str r1, [%0,#32] \n\t"
		/*Dont populate DAFSR and RAFSR*/
		"mrc    p15, 0, r1, c10, c2,0 \n\t"	//PMRRR
		"str r1, [%0,#44] \n\t"
		"mrc    p15, 0, r1, c10, c2,1 \n\t"	//NMRRR
		"str r1, [%0,#48] \n\t"
		"mrc    p15, 0, r1, c13, c0,0 \n\t"	//FCSEPID
		"str r1, [%0,#52] \n\t"
		"mrc    p15, 0, r1, c13, c0,1 \n\t"	//CONTEXT
		"str r1, [%0,#56] \n\t"
		"mrc    p15, 0, r1, c13, c0,2 \n\t"	//URWTPID
		"str r1, [%0,#60] \n\t"
		"mrc    p15, 0, r1, c13, c0,3 \n\t"	//UROTPID
		"str r1, [%0,#64] \n\t"
		"mrc    p15, 0, r1, c13, c0,4 \n\t"	//POTPIDR
		"str r1, [%0,#68] \n\t"
		
		:					/* output */
        :"r"(mmu_info)    /* input */
        :"%r1","memory"         /* clobbered register */
        ); 
	return 0;
}
EXPORT_SYMBOL(kernel_sec_get_mmu_reg_dump);

void kernel_sec_save_final_context(void)
{
	if(	kernel_sec_get_mmu_reg_dump(&kernel_sec_mmu_reg_dump) < 0)
	{
		printk(KERN_EMERG"(kernel_sec_save_final_context) kernel_sec_get_mmu_reg_dump faile.\n");
	}
	kernel_sec_get_core_reg_dump(&kernel_sec_core_reg_dump);

	printk(KERN_EMERG "(kernel_sec_save_final_context) Final context was saved before the system reset.\n");
}
EXPORT_SYMBOL(kernel_sec_save_final_context);


/*
 *  bSilentReset
 *    TRUE  : Silent reset - clear the magic code.
 *    FALSE : Reset to upload mode - not clear the magic code.
 *
 *  TODO : DebugLevel consideration should be added.
 */
//extern void Ap_Cp_Switch_Config(u16 ap_cp_mode);
void kernel_sec_hw_reset(bool bSilentReset)
{
//	Ap_Cp_Switch_Config(0);
#if defined(CONFIG_MACH_SAMSUNG_P5)
	/* After panic, kernel_sec_get_debug_level often disturb reset process */
	if (bSilentReset || (KERNEL_SEC_DEBUG_LEVEL_LOW == debuglevel))
#else
	if (bSilentReset || (KERNEL_SEC_DEBUG_LEVEL_LOW == kernel_sec_get_debug_level()))
#endif
	{
		kernel_sec_clear_upload_magic_number();
		printk(KERN_EMERG "(kernel_sec_hw_reset) Upload Magic Code is cleared for silet reset.\n");
	}

	printk(KERN_EMERG "(kernel_sec_hw_reset) %s\n", gkernel_sec_build_info);
	
	printk(KERN_EMERG "(kernel_sec_hw_reset) The forced reset was called. The system will be reset !!\n");

	flush_cache_all();
	/*
	// flush cache back to ram 
	flush_cache_all();

	__raw_writel(0x8000,kernel_sec_viraddr_wdt_reset_reg +0x4 );
	__raw_writel(0x1,   kernel_sec_viraddr_wdt_reset_reg +0x4 );
	__raw_writel(0x8,   kernel_sec_viraddr_wdt_reset_reg +0x8 );
	__raw_writel(0x8021,kernel_sec_viraddr_wdt_reset_reg);

	 // Never happened because the reset will occur before this. 
	while(1);
	*/
}
EXPORT_SYMBOL(kernel_sec_hw_reset);

bool kernel_sec_set_debug_level(int level)
{
	if (!(level == KERNEL_SEC_DEBUG_LEVEL_LOW 
			|| level == KERNEL_SEC_DEBUG_LEVEL_MID 
			|| level == KERNEL_SEC_DEBUG_LEVEL_HIGH)) {
		printk_ratelimited(KERN_NOTICE "(kernel_sec_set_debug_level) The debug value is \
				invalid(0x%x)!! Set default level(LOW)\n", level);
		debuglevel = KERNEL_SEC_DEBUG_LEVEL_LOW;
		return 0;
	}
		
	debuglevel = level;
	/* write to MISC */
	//misc_sec_debug_level(MISC_WR);
	sec_set_param(param_index_debuglevel, &debuglevel);

	/* write to regiter (magic code) */
	kernel_sec_set_upload_magic_number();

	printk(KERN_NOTICE "(kernel_sec_set_debug_level) The debug value is 0x%x !!\n", level);

	return 1;
}
EXPORT_SYMBOL(kernel_sec_set_debug_level);

int kernel_sec_get_debug_level(void)
{
	//misc_sec_debug_level(MISC_RD);
	sec_get_param(param_index_debuglevel, &debuglevel);

	if(!( debuglevel == KERNEL_SEC_DEBUG_LEVEL_LOW 
			|| debuglevel == KERNEL_SEC_DEBUG_LEVEL_MID 
			|| debuglevel == KERNEL_SEC_DEBUG_LEVEL_HIGH))
	{
		/*In case of invalid debug level, default (debug level low)*/
		printk(KERN_NOTICE "(%s) The debug value is \
				invalid(0x%x)!! Set default level(LOW)\n", __func__, debuglevel);	
		debuglevel = KERNEL_SEC_DEBUG_LEVEL_LOW;
	}
	return debuglevel;
}
EXPORT_SYMBOL(kernel_sec_get_debug_level);

int kernel_sec_check_debug_level_high(void)
{
	if (KERNEL_SEC_DEBUG_LEVEL_HIGH == kernel_sec_get_debug_level())
		return 1;
	return 0;
}
EXPORT_SYMBOL(kernel_sec_check_debug_level_high);

extern struct GAForensicINFO GAFINFO;

void dump_one_task_info( struct task_struct *tsk, bool isMain )
{
	char stat_array[3] = {'R', 'S', 'D'};
	char stat_ch;
	char *pThInf = tsk->stack;
	unsigned long wchan;
	unsigned long pc = 0;
	char symname[KSYM_NAME_LEN];
	int permitted;
	struct mm_struct *mm;

	permitted = ptrace_may_access(tsk, PTRACE_MODE_READ);
	mm = get_task_mm(tsk);
	if (mm) {
		if (permitted) {
			pc = KSTK_EIP(tsk);
		}
 	}
	
	wchan = get_wchan(tsk);
	if (lookup_symbol_name(wchan, symname) < 0) {
		if (!ptrace_may_access(tsk, PTRACE_MODE_READ)) 
			sprintf(symname,"_____");
		else  
			sprintf(symname, "%lu", wchan);
	}

	stat_ch = tsk->state <= TASK_UNINTERRUPTIBLE ? stat_array[tsk->state] : '?';
	printk( KERN_INFO "%8d %8d %8d %16lld %c(%d) %3d  %08x %08x  %08x %c %16s [%s]\n",
		tsk->pid, (int)(tsk->utime), (int)(tsk->stime), tsk->se.exec_start, stat_ch, (int)(tsk->state),
		*(int*)(pThInf + GAFINFO.thread_info_struct_cpu), (int)wchan, (int)pc, (int)tsk, isMain?'*':' ', tsk->comm, symname );

	if (tsk->state == TASK_RUNNING || tsk->state == TASK_UNINTERRUPTIBLE || tsk->mm == NULL)
		show_stack(tsk, NULL);
}

void dump_all_task_info(void)
{
	struct task_struct *frst_tsk;
	struct task_struct *curr_tsk;
	struct task_struct *frst_thr;
	struct task_struct *curr_thr;

	printk( KERN_INFO "\n" );
	printk( KERN_INFO " current proc : %d %s\n", current->pid, current->comm );
	printk( KERN_INFO " -------------------------------------------------------------------------------------------------------------\n" );
	printk( KERN_INFO "     pid      uTime    sTime      exec(ns)  stat  cpu   wchan   user_pc  task_struct          comm   sym_wchan\n" );
	printk( KERN_INFO " -------------------------------------------------------------------------------------------------------------\n" );

	//processes
	frst_tsk = &init_task;
	curr_tsk = frst_tsk;
	while( curr_tsk != NULL ) {
		dump_one_task_info( curr_tsk,  true );
		//threads
		if( curr_tsk->thread_group.next != NULL ) {
			frst_thr = container_of( curr_tsk->thread_group.next, struct task_struct, thread_group );
			curr_thr = frst_thr;
			if( frst_thr != curr_tsk ) {
				while( curr_thr != NULL ) {
					dump_one_task_info( curr_thr, false );
					curr_thr = container_of( curr_thr->thread_group.next, struct task_struct, thread_group );
					if( curr_thr == curr_tsk ) break;
				}
			}
		}
		curr_tsk = container_of( curr_tsk->tasks.next, struct task_struct, tasks );
		if( curr_tsk == frst_tsk ) break;
	}
	printk( KERN_INFO " -----------------------------------------------------------------------------------\n" );
}
EXPORT_SYMBOL(dump_all_task_info);

void dump_cpu_stat(void)
{
	int i, j;
	unsigned long jif;
	cputime64_t user, nice, system, idle, iowait, irq, softirq, steal;
	cputime64_t guest, guest_nice;
	u64 sum = 0;
	u64 sum_softirq = 0;
	unsigned int per_softirq_sums[NR_SOFTIRQS] = {0};
	struct timespec boottime;
	unsigned int per_irq_sum;

	char *softirq_to_name[NR_SOFTIRQS] = {
	     "HI", "TIMER", "NET_TX", "NET_RX", "BLOCK", "BLOCK_IOPOLL",
	     "TASKLET", "SCHED", "HRTIMER",  "RCU"
	};

	user = nice = system = idle = iowait = irq = softirq = steal = cputime64_zero;
	guest = guest_nice = cputime64_zero;

	getboottime(&boottime);
	jif = boottime.tv_sec;
	for_each_possible_cpu(i) {
		user = cputime64_add(user, kstat_cpu(i).cpustat.user);
		nice = cputime64_add(nice, kstat_cpu(i).cpustat.nice);
		system = cputime64_add(system, kstat_cpu(i).cpustat.system);
		idle = cputime64_add(idle, kstat_cpu(i).cpustat.idle);
		idle = cputime64_add(idle, arch_idle_time(i));
		iowait = cputime64_add(iowait, kstat_cpu(i).cpustat.iowait);
		irq = cputime64_add(irq, kstat_cpu(i).cpustat.irq);
		softirq = cputime64_add(softirq, kstat_cpu(i).cpustat.softirq);

		for_each_irq_nr(j) {
			sum += kstat_irqs_cpu(j, i);
		}
		sum += arch_irq_stat_cpu(i);
		for (j = 0; j < NR_SOFTIRQS; j++) {
			unsigned int softirq_stat = kstat_softirqs_cpu(j, i);
			per_softirq_sums[j] += softirq_stat;
			sum_softirq += softirq_stat;
		}
	}
	sum += arch_irq_stat();
	printk(KERN_INFO "\n");
	printk(KERN_INFO " cpu     user:%llu  nice:%llu  system:%llu  idle:%llu  iowait:%llu  irq:%llu  softirq:%llu %llu %llu " "%llu\n",
		(unsigned long long)cputime64_to_clock_t(user), 
		(unsigned long long)cputime64_to_clock_t(nice),
		(unsigned long long)cputime64_to_clock_t(system),
		(unsigned long long)cputime64_to_clock_t(idle),
		(unsigned long long)cputime64_to_clock_t(iowait),
		(unsigned long long)cputime64_to_clock_t(irq),
		(unsigned long long)cputime64_to_clock_t(softirq),
		(unsigned long long)0, //cputime64_to_clock_t(steal),
		(unsigned long long)0, //cputime64_to_clock_t(guest),
		(unsigned long long)0);//cputime64_to_clock_t(guest_nice));
	printk(KERN_INFO " -----------------------------------------------------------------------------------\n" );
	for_each_online_cpu(i) {
		/* Copy values here to work around gcc-2.95.3, gcc-2.96 */
		user = kstat_cpu(i).cpustat.user;
		nice = kstat_cpu(i).cpustat.nice;
		system = kstat_cpu(i).cpustat.system;
		idle = kstat_cpu(i).cpustat.idle;
		idle = cputime64_add(idle, arch_idle_time(i));
		iowait = kstat_cpu(i).cpustat.iowait;
		irq = kstat_cpu(i).cpustat.irq;
		softirq = kstat_cpu(i).cpustat.softirq;
		//steal = kstat_cpu(i).cpustat.steal;
		//guest = kstat_cpu(i).cpustat.guest;
		//guest_nice = kstat_cpu(i).cpustat.guest_nice;
		printk(KERN_INFO " cpu %d   user:%llu  nice:%llu  system:%llu  idle:%llu  iowait:%llu  irq:%llu  softirq:%llu %llu %llu " "%llu\n",
			i,
			(unsigned long long)cputime64_to_clock_t(user),
			(unsigned long long)cputime64_to_clock_t(nice),
			(unsigned long long)cputime64_to_clock_t(system),
			(unsigned long long)cputime64_to_clock_t(idle),
			(unsigned long long)cputime64_to_clock_t(iowait),
			(unsigned long long)cputime64_to_clock_t(irq),
			(unsigned long long)cputime64_to_clock_t(softirq),
			(unsigned long long)0, //cputime64_to_clock_t(steal),
			(unsigned long long)0, //cputime64_to_clock_t(guest),
			(unsigned long long)0);//cputime64_to_clock_t(guest_nice));
	}
	printk(KERN_INFO " -----------------------------------------------------------------------------------\n" ); 
	printk(KERN_INFO "\n"); 
	printk(KERN_INFO " irq : %llu", (unsigned long long)sum); 
	printk(KERN_INFO " -----------------------------------------------------------------------------------\n" ); 
	/* sum again ? it could be updated? */ 
	for_each_irq_nr(j) { 
		per_irq_sum = 0; 
		for_each_possible_cpu(i) 
		per_irq_sum += kstat_irqs_cpu(j, i); 
		if(per_irq_sum) {
			printk(KERN_INFO " irq-%4d : %8u %s\n", j, per_irq_sum, irq_to_desc(j)->action ?
				irq_to_desc(j)->action->name ?: "???" : "???"); 
		}
	} 
	printk(KERN_INFO " -----------------------------------------------------------------------------------\n" ); 
	printk(KERN_INFO "\n"); 
	printk(KERN_INFO " softirq : %llu", (unsigned long long)sum_softirq); 
	printk(KERN_INFO " -----------------------------------------------------------------------------------\n" ); 
	for (i = 0; i < NR_SOFTIRQS; i++) 
		if(per_softirq_sums[i]) 
			printk(KERN_INFO " softirq-%d : %8u %s \n", i, per_softirq_sums[i], softirq_to_name[i]); 
	printk(KERN_INFO " -----------------------------------------------------------------------------------\n" ); 
}
EXPORT_SYMBOL(dump_cpu_stat);

#endif // CONFIG_KERNEL_DEBUG_SEC
