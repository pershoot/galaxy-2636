/*
 * Copyright (c) 2010 Trusted Logic S.A.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <asm/div64.h>
#include <asm/system.h>
#include <linux/version.h>
#include <asm/cputype.h>
#include <linux/interrupt.h>
#include <linux/page-flags.h>
#include <linux/pagemap.h>
#include <linux/vmalloc.h>
#include <linux/jiffies.h>

#include "scxlnx_defs.h"
#include "scxlnx_comm.h"
#include "scx_protocol.h"
#include "scxlnx_util.h"
#include "scxlnx_conn.h"

/*
 * Structure common to all SMC operations
 */
struct SCXLNX_GENERIC_SMC {
	u32 reg0;
	u32 reg1;
	u32 reg2;
	u32 reg3;
	u32 reg4;
};

/*----------------------------------------------------------------------------
 * SMC operations
 *----------------------------------------------------------------------------*/

static inline void SCXLNXCommCallGenericSMC(
	struct SCXLNX_GENERIC_SMC *pGenericSMC)
{
#ifdef CONFIG_SMP
	long ret;
	cpumask_t saved_cpu_mask;
	cpumask_t local_cpu_mask = CPU_MASK_NONE;

	cpu_set(0, local_cpu_mask);
	sched_getaffinity(0, &saved_cpu_mask);
	ret = sched_setaffinity(0, &local_cpu_mask);
	if (ret != 0)
	{
		dprintk(KERN_ERR "sched_setaffinity #1 -> 0x%lX", ret);
	}
#endif

	__asm__ volatile(
		"mov r0, %2\n"
		"mov r1, %3\n"
		"mov r2, %4\n"
		"mov r3, %5\n"
		"mov r4, %6\n"
		".word    0xe1600070              @ SMC 0\n"
		"mov %0, r0\n"
		"mov %1, r1\n"
		: "=r" (pGenericSMC->reg0), "=r" (pGenericSMC->reg1)
		: "r" (pGenericSMC->reg0), "r" (pGenericSMC->reg1),
		  "r" (pGenericSMC->reg2), "r" (pGenericSMC->reg3),
		  "r" (pGenericSMC->reg4)
		: "r0", "r1", "r2", "r3", "r4");

#ifdef CONFIG_SMP
		ret = sched_setaffinity(0, &saved_cpu_mask);
		if (ret != 0)
		{
			dprintk(KERN_ERR "sched_setaffinity #2 -> 0x%lX", ret);
		}
#endif
}

/*
 * Calls the get protocol version SMC.
 * Fills the parameter pProtocolVersion with the version number returned by the
 * SMC
 */
static inline void SCXLNXCommCallGetProtocolVersionSMC(u32 *pProcotolVersion)
{
	struct SCXLNX_GENERIC_SMC sGenericSMC;

	sGenericSMC.reg0 = SCX_SMC_GET_PROTOCOL_VERSION;
	sGenericSMC.reg1 = 0;
	sGenericSMC.reg2 = 0;
	sGenericSMC.reg3 = 0;
	sGenericSMC.reg4 = 0;

	SCXLNXCommCallGenericSMC(&sGenericSMC);
	*pProcotolVersion = sGenericSMC.reg1;
}


/*
 * Calls the init SMC with the specified parameters.
 * Returns zero upon successful completion, or an appropriate error code upon
 * failure.
 */
static inline int SCXLNXCommCallInitSMC(u32 nSharedPageDescriptor)
{
	struct SCXLNX_GENERIC_SMC sGenericSMC;

	sGenericSMC.reg0 = SCX_SMC_INIT;
	/* Descriptor for the layer 1 shared buffer */
	sGenericSMC.reg1 = nSharedPageDescriptor;
	sGenericSMC.reg2 = 0;
	sGenericSMC.reg3 = 0;
	sGenericSMC.reg4 = 0;

	SCXLNXCommCallGenericSMC(&sGenericSMC);
	if (sGenericSMC.reg0 != S_SUCCESS)
		printk(KERN_ERR "SCXLNXCommCallInitSMC:"
			" r0=0x%08X upon return (expected 0x%08X)!\n",
			sGenericSMC.reg0,
			S_SUCCESS);

	return sGenericSMC.reg0;
}


/*
 * Calls the reset irq SMC.
 */
static inline void SCXLNXCommCallResetIrqSMC(void)
{
	struct SCXLNX_GENERIC_SMC sGenericSMC;

	sGenericSMC.reg0 = SCX_SMC_RESET_IRQ;
	sGenericSMC.reg1 = 0;
	sGenericSMC.reg2 = 0;
	sGenericSMC.reg3 = 0;
	sGenericSMC.reg4 = 0;

	SCXLNXCommCallGenericSMC(&sGenericSMC);
}


/*
 * Calls the WAKE_UP SMC.
 * Returns zero upon successful completion, or an appropriate error code upon
 * failure.
 */
static inline int SCXLNXCommCallWakeUpSMC(u32 nL1SharedBufferDescriptor,
	u32 nSharedMemStartOffset,
	u32 nSharedMemSize)
{
	struct SCXLNX_GENERIC_SMC sGenericSMC;

	sGenericSMC.reg0 = SCX_SMC_WAKE_UP;
	sGenericSMC.reg1 = nSharedMemStartOffset;
	/* long form command */
	sGenericSMC.reg2 = nSharedMemSize | 0x80000000;
	sGenericSMC.reg3 = nL1SharedBufferDescriptor;
	sGenericSMC.reg4 = 0;

	SCXLNXCommCallGenericSMC(&sGenericSMC);

	if (sGenericSMC.reg0 != S_SUCCESS)
		printk(KERN_ERR "SCXLNXCommCallWakeUpSMC:"
			" r0=0x%08X upon return (expected 0x%08X)!\n",
			sGenericSMC.reg0,
			S_SUCCESS);

	return sGenericSMC.reg0;
}

/*
 * Calls the N-Yield SMC.
 */
static inline void SCXLNXCommCallNYieldSMC(void)
{
	struct SCXLNX_GENERIC_SMC sGenericSMC;

	sGenericSMC.reg0 = SCX_SMC_N_YIELD;
	sGenericSMC.reg1 = 0;
	sGenericSMC.reg2 = 0;
	sGenericSMC.reg3 = 0;
	sGenericSMC.reg4 = 0;

	SCXLNXCommCallGenericSMC(&sGenericSMC);
}

/* Yields the Secure World */
int tf_schedule_secure_world(struct SCXLNX_COMM *pComm, bool prepare_exit)
{
	SCXLNXCommSetCurrentTime(pComm);

	/* yield to the Secure World */
	SCXLNXCommCallNYieldSMC();

	return 0;
}

/*
 * Returns the L2 descriptor for the specified user page.
 */

#define L2_INIT_DESCRIPTOR_BASE           (0x00000003)
#define L2_INIT_DESCRIPTOR_V13_12_SHIFT   (4)

static u32 SCXLNXCommGetL2InitDescriptor(void *pVirtAddr)
{
	struct page *pPage;
	u32 nVirtAddr;
	u32 nPhysAddr;
	u32 nDescriptor;

	nDescriptor = L2_INIT_DESCRIPTOR_BASE;
	nVirtAddr = (u32) pVirtAddr;

	/* get physical address and add to nDescriptor */
	pPage = virt_to_page(pVirtAddr);
	nPhysAddr = page_to_phys(pPage);
	nDescriptor |= (nPhysAddr & L2_DESCRIPTOR_ADDR_MASK);

	/* Add virtual address v[13:12] bits to nDescriptor */
	nDescriptor |= (DESCRIPTOR_V13_12_GET(nVirtAddr)
		<< L2_INIT_DESCRIPTOR_V13_12_SHIFT);

	nDescriptor |= SCXLNXCommGetL2DescriptorCommon(nVirtAddr, &init_mm);


	return nDescriptor;
}


/*----------------------------------------------------------------------------
 * Power management
 *----------------------------------------------------------------------------*/

/*
 * Free the memory used by the W3B buffer for the specified comm.
 * This function does nothing if no W3B buffer is allocated for the device.
 */
static inline void SCXLNXCommFreeW3B(struct SCXLNX_COMM *pComm)
{
	SCXLNXCommReleaseSharedMemory(
		&(pComm->sW3BAllocationContext),
		&(pComm->sW3BShmemDesc),
		0);

	SCXLNXReleaseCoarsePageTableAllocator(&(pComm->sW3BAllocationContext));

	internal_vfree((void *)pComm->nW3BShmemVAddr);
	pComm->nW3BShmemVAddr = 0;
	pComm->nW3BShmemSize = 0;
	clear_bit(SCXLNX_COMM_FLAG_W3B_ALLOCATED, &(pComm->nFlags));
}


/*
 * Allocates the W3B buffer for the specified comm.
 * Returns zero upon successful completion, or an appropriate error code upon
 * failure.
 */
static inline int SCXLNXCommAllocateW3B(struct SCXLNX_COMM *pComm)
{
	int nError;
	u32 nFlags;
	u32 nConfigFlags_S;
	u32 *pW3BDescriptors;
	u32 nW3BDescriptorCount;
	u32 nW3BCurrentSize;

	nConfigFlags_S = SCXLNXCommReadReg32(&pComm->pBuffer->nConfigFlags_S);

retry:
	if ((test_bit(SCXLNX_COMM_FLAG_W3B_ALLOCATED, &(pComm->nFlags))) == 0) {
		/*
		 * Initialize the shared memory for the W3B
		 */
		SCXLNXInitializeCoarsePageTableAllocator(
			&pComm->sW3BAllocationContext);
	} else {
		/*
		 * The W3B is allocated but do we have to reallocate a bigger
		 * one?
		 */
		/* Check H bit */
		if ((nConfigFlags_S & (1<<4)) != 0) {
			/* The size of the W3B may change after SMC_INIT */
			/* Read the current value */
			nW3BCurrentSize = SCXLNXCommReadReg32(
				&pComm->pBuffer->nW3BSizeCurrent_S);
			if (pComm->nW3BShmemSize > nW3BCurrentSize)
				return 0;

			SCXLNXCommFreeW3B(pComm);
			goto retry;
		} else {
			return 0;
		}
	}

	/* check H bit */
	if ((nConfigFlags_S & (1<<4)) != 0)
		/* The size of the W3B may change after SMC_INIT */
		/* Read the current value */
		pComm->nW3BShmemSize = SCXLNXCommReadReg32(
			&pComm->pBuffer->nW3BSizeCurrent_S);
	else
		pComm->nW3BShmemSize = SCXLNXCommReadReg32(
			&pComm->pBuffer->nW3BSizeMax_S);

	pComm->nW3BShmemVAddr = (u32) internal_vmalloc(pComm->nW3BShmemSize);
	if (pComm->nW3BShmemVAddr == 0) {
		printk(KERN_ERR "SCXLNXCommAllocateW3B():"
			" Out of memory for W3B buffer (%u bytes)!\n",
			(unsigned int)(pComm->nW3BShmemSize));
		nError = -ENOMEM;
		goto error;
	}

	/* initialize the sW3BShmemDesc structure */
	pComm->sW3BShmemDesc.nType = SCXLNX_SHMEM_TYPE_PM_HIBERNATE;
	INIT_LIST_HEAD(&(pComm->sW3BShmemDesc.list));

	nFlags = (SCX_SHMEM_TYPE_READ | SCX_SHMEM_TYPE_WRITE);

	/* directly point to the L1 shared buffer W3B descriptors */
	pW3BDescriptors = pComm->pBuffer->nW3BDescriptors;

	/*
	 * SCXLNXCommFillDescriptorTable uses the following parameter as an
	 * IN/OUT
	 */

	nError = SCXLNXCommFillDescriptorTable(
		&(pComm->sW3BAllocationContext),
		&(pComm->sW3BShmemDesc),
		pComm->nW3BShmemVAddr,
		NULL,
		pW3BDescriptors,
		&(pComm->nW3BShmemSize),
		&(pComm->nW3BShmemOffset),
		false,
		nFlags,
		&nW3BDescriptorCount);
	if (nError != 0) {
		printk(KERN_ERR "SCXLNXCommAllocateW3B():"
			" SCXLNXCommFillDescriptorTable failed with "
			"error code 0x%08x!\n",
			nError);
		goto error;
	}

	set_bit(SCXLNX_COMM_FLAG_W3B_ALLOCATED, &(pComm->nFlags));

	/* successful completion */
	return 0;

error:
	SCXLNXCommFreeW3B(pComm);

	return nError;
}

/*
 * Perform a Secure World shutdown operation.
 * The routine does not return if the operation succeeds.
 * the routine returns an appropriate error code if
 * the operation fails.
 */
int SCXLNXCommShutdown(struct SCXLNX_COMM *pComm)
{
#ifdef CONFIG_TFN
	/* this function is useless for the TEGRA product */
	return 0;
#else
	int nError;
	union SCX_COMMAND_MESSAGE sMessage;
	union SCX_ANSWER_MESSAGE sAnswer;

	dprintk(KERN_INFO "SCXLNXCommShutdown()\n");

	memset(&sMessage, 0, sizeof(sMessage));

	sMessage.sHeader.nMessageType = SCX_MESSAGE_TYPE_MANAGEMENT;
	sMessage.sHeader.nMessageSize =
			(sizeof(struct SCX_COMMAND_MANAGEMENT) -
				sizeof(struct SCX_COMMAND_HEADER))/sizeof(u32);

	sMessage.sManagementMessage.nCommand = SCX_MANAGEMENT_SHUTDOWN;

	nError = SCXLNXCommSendReceive(
		pComm,
		&sMessage,
		&sAnswer,
		NULL,
		false);

	if (nError != 0) {
		dprintk(KERN_ERR "SCXLNXCommShutdown(): "
			"SCXLNXCommSendReceive failed (error %d)!\n",
			nError);
		return nError;
	}

#ifdef CONFIG_TF_DRIVER_DEBUG_SUPPORT
	if (sAnswer.sHeader.nErrorCode != 0)
		dprintk(KERN_ERR "tf_driver: shutdown failed.\n");
	else
		dprintk(KERN_INFO "tf_driver: shutdown succeeded.\n");
#endif

	return sAnswer.sHeader.nErrorCode;
#endif
}


/*
 * Perform a Secure World hibernate operation.
 * The routine does not return if the operation succeeds.
 * the routine returns an appropriate error code if
 * the operation fails.
 */
int SCXLNXCommHibernate(struct SCXLNX_COMM *pComm)
{
#ifdef CONFIG_TFN
	/* this function is useless for the TEGRA product */
	return 0;
#else
	int nError;
	union SCX_COMMAND_MESSAGE sMessage;
	union SCX_ANSWER_MESSAGE sAnswer;
	u32 nFirstCommand;
	u32 nFirstFreeCommand;

	dprintk(KERN_INFO "SCXLNXCommHibernate()\n");

	nError = SCXLNXCommAllocateW3B(pComm);
	if (nError != 0) {
		dprintk(KERN_ERR "SCXLNXCommHibernate(): "
			"SCXLNXCommAllocateW3B failed (error %d)!\n",
			nError);
		return nError;
	}

	/*
	 * As the polling thread is already hibernating, we
	 * should send the message and receive the answer ourself
	 */

	/* build the "prepare to hibernate" message */
	sMessage.sHeader.nMessageType = SCX_MESSAGE_TYPE_MANAGEMENT;
	sMessage.sManagementMessage.nCommand = SCX_MANAGEMENT_HIBERNATE;
	/* Long Form Command */
	sMessage.sManagementMessage.nSharedMemDescriptors[0] = 0;
	sMessage.sManagementMessage.nSharedMemDescriptors[1] = 0;
	sMessage.sManagementMessage.nW3BSize =
		pComm->nW3BShmemSize | 0x80000000;
	sMessage.sManagementMessage.nW3BStartOffset =
		pComm->nW3BShmemOffset;
	sMessage.sHeader.nOperationID = (u32) &sAnswer;

	SCXLNXDumpMessage(&sMessage);

	/* find a slot to send the message in */

	/* AFY: why not use the function SCXLNXCommSendReceive?? We are
	 * duplicating a lot of subtle code here. And it's not going to be
	 * tested because power management is currently not supported by the
	 * secure world. */
	for (;;) {
		int nQueueWordsCount, nCommandSize;

		spin_lock(&(pComm->lock));

		nFirstCommand = SCXLNXCommReadReg32(
			&pComm->pBuffer->nFirstCommand);
		nFirstFreeCommand = SCXLNXCommReadReg32(
			&pComm->pBuffer->nFirstFreeCommand);

		nQueueWordsCount = nFirstFreeCommand - nFirstCommand;
		nCommandSize     = sMessage.sHeader.nMessageSize
			+ sizeof(struct SCX_COMMAND_HEADER);
		if ((nQueueWordsCount + nCommandSize) <
				SCX_N_MESSAGE_QUEUE_CAPACITY) {
			/* Command queue is not full */
			memcpy(&pComm->pBuffer->sCommandQueue[
				nFirstFreeCommand %
					SCX_N_MESSAGE_QUEUE_CAPACITY],
				&sMessage,
				nCommandSize * sizeof(u32));

			SCXLNXCommWriteReg32(&pComm->pBuffer->nFirstFreeCommand,
				nFirstFreeCommand + nCommandSize);

			spin_unlock(&(pComm->lock));
			break;
		}

		spin_unlock(&(pComm->lock));
		(void)tf_schedule_secure_world(pComm, false);
	}

	/* now wait for the answer, dispatching other answers */
	while (1) {
		u32 nFirstAnswer;
		u32 nFirstFreeAnswer;

		/* check all the answers */
		nFirstFreeAnswer = SCXLNXCommReadReg32(
			&pComm->pBuffer->nFirstFreeAnswer);
		nFirstAnswer = SCXLNXCommReadReg32(
			&pComm->pBuffer->nFirstAnswer);

		if (nFirstAnswer != nFirstFreeAnswer) {
			int bFoundAnswer = 0;

			do {
				/* answer queue not empty */
				union SCX_ANSWER_MESSAGE sComAnswer;
				struct SCX_ANSWER_HEADER sHeader;
				/* size of the command in words of 32bit */
				int nCommandSize;

				/* get the nMessageSize */
				memcpy(&sHeader,
					&pComm->pBuffer->sAnswerQueue[
						nFirstAnswer %
						SCX_S_ANSWER_QUEUE_CAPACITY],
					sizeof(struct SCX_ANSWER_HEADER));
				nCommandSize = sHeader.nMessageSize +
					sizeof(struct SCX_ANSWER_HEADER);

				/*
				 * NOTE: nMessageSize is the number of words
				 * following the first word
				 */
				memcpy(&sComAnswer,
					&pComm->pBuffer->sAnswerQueue[
						nFirstAnswer %
						SCX_S_ANSWER_QUEUE_CAPACITY],
					nCommandSize * sizeof(u32));

				SCXLNXDumpAnswer(&sComAnswer);

				if (sComAnswer.sHeader.nOperationID ==
						(u32) &sAnswer) {
					/*
					 * this is the answer to the "prepare to
					 * hibernate" message
					 */
					memcpy(&sAnswer,
						&sComAnswer,
						nCommandSize * sizeof(u32));

					bFoundAnswer = 1;
					SCXLNXCommWriteReg32(
						&pComm->pBuffer->nFirstAnswer,
						nFirstAnswer + nCommandSize);
					break;
				} else {
					/*
					 * this is a standard message answer,
					 * dispatch it
					 */
					struct SCXLNX_ANSWER_STRUCT
						*pAnswerStructure;

					pAnswerStructure =
						(struct SCXLNX_ANSWER_STRUCT *)
						sComAnswer.sHeader.nOperationID;

					memcpy(pAnswerStructure->pAnswer,
						&sComAnswer,
						nCommandSize * sizeof(u32));

					pAnswerStructure->bAnswerCopied = true;
				}

				SCXLNXCommWriteReg32(
					&pComm->pBuffer->nFirstAnswer,
					nFirstAnswer + nCommandSize);
			} while (nFirstAnswer != nFirstFreeAnswer);

			if (bFoundAnswer)
				break;
		}

		/*
		 * since the Secure World is at least running the "prepare to
		 * hibernate" message, its timeout must be immediate So there is
		 * no need to check its timeout and schedule() the current
		 * thread
		 */
		(void)tf_schedule_secure_world(pComm, false);
	} /* while (1) */

	printk(KERN_INFO "tf_driver: hibernate.\n");
	return 0;
#endif
}


/*
 * Perform a Secure World resume operation.
 * The routine returns once the Secure World is active again
 * or if an error occurs during the "resume" process
 */
int SCXLNXCommResume(struct SCXLNX_COMM *pComm)
{
#ifdef CONFIG_TFN
	/* this function is useless for the TEGRA product */
	return 0;
#else
	int nError;
	u32 nStatus;

	dprintk(KERN_INFO "SCXLNXCommResume()\n");

	nError = SCXLNXCommCallWakeUpSMC(
		SCXLNXCommGetL2InitDescriptor(pComm->pBuffer),
		pComm->nW3BShmemOffset,
		pComm->nW3BShmemSize);

	if (nError != 0) {
		dprintk(KERN_ERR "SCXLNXCommResume(): "
			"SCXLNXCommCallWakeUpSMC failed (error %d)!\n",
			nError);
		return nError;
	}

	nStatus = ((SCXLNXCommReadReg32(&(pComm->pBuffer->nStatus_S))
		& SCX_STATUS_POWER_STATE_MASK)
		>> SCX_STATUS_POWER_STATE_SHIFT);

	while ((nStatus != SCX_POWER_MODE_ACTIVE)
			&& (nStatus != SCX_POWER_MODE_PANIC)) {
		SCXLNXCommCallNYieldSMC();

		nStatus = ((SCXLNXCommReadReg32(&(pComm->pBuffer->nStatus_S))
			& SCX_STATUS_POWER_STATE_MASK)
			>> SCX_STATUS_POWER_STATE_SHIFT);

		/*
		 * As this may last quite a while, call the kernel scheduler to
		 * hand over CPU for other operations
		 */
		schedule();
	}

	switch (nStatus) {
	case SCX_POWER_MODE_ACTIVE:
		break;

	case SCX_POWER_MODE_PANIC:
		dprintk(KERN_ERR "SCXLNXCommResume(): "
			"Secure World POWER_MODE_PANIC!\n");
		return -EINVAL;

	default:
		dprintk(KERN_ERR "SCXLNXCommResume(): "
			"unexpected Secure World POWER_MODE (%d)!\n", nStatus);
		return -EINVAL;
	}

	dprintk(KERN_INFO "SCXLNXCommResume() succeeded\n");
	return 0;
#endif
}

/*----------------------------------------------------------------------------
 * Communication initialization and termination
 *----------------------------------------------------------------------------*/

/*
 * Handles the software interrupts issued by the Secure World.
 */
static irqreturn_t SCXLNXCommSoftIntHandler(int irq, void *dev_id)
{
	struct SCXLNX_COMM *pComm = (struct SCXLNX_COMM *) dev_id;

	if (pComm->pBuffer == NULL)
		return IRQ_NONE;

	if ((SCXLNXCommReadReg32(&pComm->pBuffer->nStatus_S) &
			SCX_STATUS_P_MASK) == 0)
		/* interrupt not issued by the Trusted Foundations Software */
		return IRQ_NONE;

	SCXLNXCommCallResetIrqSMC();

	/* signal N_SM_EVENT */
	wake_up(&pComm->waitQueue);

	return IRQ_HANDLED;
}

/*
 * Initializes the communication with the Secure World.
 * The L1 shared buffer is allocated and the Secure World
 * is yielded for the first time.
 * returns successfuly once the communication with
 * the Secure World is up and running
 *
 * Returns 0 upon success or appropriate error code
 * upon failure
 */
int SCXLNXCommInit(struct SCXLNX_COMM *pComm)
{
	int nError;
	struct page *pBufferPage;
	u32 nProtocolVersion;

	dprintk(KERN_INFO "SCXLNXCommInit()\n");

	spin_lock_init(&(pComm->lock));
	pComm->nFlags = 0;
	pComm->pBuffer = NULL;
	init_waitqueue_head(&(pComm->waitQueue));

	/*
	 * Check the Secure World protocol version is the expected one.
	 */
	SCXLNXCommCallGetProtocolVersionSMC(&nProtocolVersion);

	if ((GET_PROTOCOL_MAJOR_VERSION(nProtocolVersion))
			!= SCX_S_PROTOCOL_MAJOR_VERSION) {
		printk(KERN_ERR "SCXLNXCommInit():"
			" Unsupported Secure World Major Version "
			"(0x%02X, expected 0x%02X)!\n",
			GET_PROTOCOL_MAJOR_VERSION(nProtocolVersion),
			SCX_S_PROTOCOL_MAJOR_VERSION);
		nError = -EIO;
		goto error;
	}

	/*
	 * Register the software interrupt handler if required to.
	 */
	if (pComm->nSoftIntIrq != -1) {
		dprintk(KERN_INFO "SCXLNXCommInit(): "
			"Registering software interrupt handler (IRQ %d)\n",
			pComm->nSoftIntIrq);

		nError = request_irq(pComm->nSoftIntIrq,
			SCXLNXCommSoftIntHandler,
			IRQF_SHARED,
			SCXLNX_DEVICE_BASE_NAME,
			pComm);
		if (nError != 0) {
			dprintk(KERN_ERR "SCXLNXCommInit(): "
				"request_irq failed for irq %d (error %d)\n",
			pComm->nSoftIntIrq, nError);
			goto error;
		}
		set_bit(SCXLNX_COMM_FLAG_IRQ_REQUESTED, &(pComm->nFlags));
	}

	/*
	 * Allocate and initialize the L1 shared buffer.
	 */
	pComm->pBuffer = (void *) internal_get_zeroed_page(GFP_KERNEL);
	if (pComm->pBuffer == NULL) {
		printk(KERN_ERR "SCXLNXCommInit():"
			" get_zeroed_page failed for L1 shared buffer!\n");
		nError = -ENOMEM;
		goto error;
	}

	/*
	 * Ensure the page storing the L1 shared buffer is mapped.
	 */
	pBufferPage = virt_to_page(pComm->pBuffer);
	trylock_page(pBufferPage);

	dprintk(KERN_INFO "SCXLNXCommInit(): "
		"L1 shared buffer allocated at virtual:%p, "
		"physical:%p (page:%p)\n",
		pComm->pBuffer,
		(void *)virt_to_phys(pComm->pBuffer),
		pBufferPage);

	set_bit(SCXLNX_COMM_FLAG_L1_SHARED_ALLOCATED, &(pComm->nFlags));

	/*
	 * Init SMC
	 */
	nError = SCXLNXCommCallInitSMC(
		SCXLNXCommGetL2InitDescriptor(pComm->pBuffer));
	if (nError != S_SUCCESS) {
		dprintk(KERN_ERR "SCXLNXCommInit(): "
			"SCXLNXCommCallInitSMC failed (error 0x%08X)!\n",
			nError);
		goto error;
	}

	/*
	 * check whether the interrupts are actually enabled
	 * If not, remove irq handler
	 */
	if ((SCXLNXCommReadReg32(&pComm->pBuffer->nConfigFlags_S) &
			SCX_CONFIG_FLAG_S) == 0) {
		if (test_and_clear_bit(SCXLNX_COMM_FLAG_IRQ_REQUESTED,
				&(pComm->nFlags)) != 0) {
			dprintk(KERN_INFO "SCXLNXCommInit(): "
				"Interrupts not used, unregistering "
				"softint (IRQ %d)\n",
				pComm->nSoftIntIrq);

			free_irq(pComm->nSoftIntIrq, pComm);
		}
	} else {
		if (test_bit(SCXLNX_COMM_FLAG_IRQ_REQUESTED,
				&(pComm->nFlags)) == 0) {
			/*
			 * Interrupts are enabled in the Secure World, but not
			 * handled by driver
			 */
			dprintk(KERN_ERR "SCXLNXCommInit(): "
				"soft_interrupt argument not provided\n");
			nError = -EINVAL;
			goto error;
		}
	}

	/*
	 * Successful completion.
	 */

	/* yield for the first time */
	(void)tf_schedule_secure_world(pComm, false);

	dprintk(KERN_INFO "SCXLNXCommInit(): Success\n");
	return S_SUCCESS;

error:
	/*
	 * Error handling.
	 */
	dprintk(KERN_INFO "SCXLNXCommInit(): Failure (error %d)\n",
		nError);
	SCXLNXCommTerminate(pComm);
	return nError;
}


/*
 * Attempt to terminate the communication with the Secure World.
 * The L1 shared buffer is freed.
 * Calling this routine terminates definitaly the communication
 * with the Secure World : there is no way to inform the Secure World of a new
 * L1 shared buffer to be used once it has been initialized.
 */
void SCXLNXCommTerminate(struct SCXLNX_COMM *pComm)
{
	dprintk(KERN_INFO "SCXLNXCommTerminate()\n");

	set_bit(SCXLNX_COMM_FLAG_TERMINATING, &(pComm->nFlags));

	if ((test_bit(SCXLNX_COMM_FLAG_W3B_ALLOCATED,
			&(pComm->nFlags))) != 0) {
		dprintk(KERN_INFO "SCXLNXCommTerminate(): "
			"Freeing the W3B buffer...\n");
		SCXLNXCommFreeW3B(pComm);
	}

	if ((test_bit(SCXLNX_COMM_FLAG_L1_SHARED_ALLOCATED,
			&(pComm->nFlags))) != 0) {
		__clear_page_locked(virt_to_page(pComm->pBuffer));
		internal_free_page((unsigned long) pComm->pBuffer);
	}

	if ((test_bit(SCXLNX_COMM_FLAG_IRQ_REQUESTED,
			&(pComm->nFlags))) != 0) {
		dprintk(KERN_INFO "SCXLNXCommTerminate(): "
			"Unregistering softint (IRQ %d)\n",
			pComm->nSoftIntIrq);
		free_irq(pComm->nSoftIntIrq, pComm);
	}
}
