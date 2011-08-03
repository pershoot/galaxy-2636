/*
 * $License:
 *    Copyright (C) 2010 InvenSense Corporation, All Rights Reserved.
 * $
 */
/**
 * @defgroup
 * @brief
 *
 * @{
 * @file     mlos-kernel.c
 * @brief
 *
 *
 */

#include "mlos.h"
#include <linux/delay.h>
#include <linux/slab.h>
#if !defined(CONFIG_MACH_SAMSUNG_P3_P7100)
#include <linux/time.h>
#endif

void *MLOSMalloc(unsigned int numBytes)
{
	return kmalloc(numBytes, GFP_KERNEL);
}

tMLError MLOSFree(void *ptr)
{
	kfree(ptr);
	return ML_SUCCESS;
}

tMLError MLOSCreateMutex(HANDLE *mutex)
{
	/* @todo implement if needed */
	return ML_ERROR_FEATURE_NOT_IMPLEMENTED;
}

tMLError MLOSLockMutex(HANDLE mutex)
{
	/* @todo implement if needed */
	return ML_ERROR_FEATURE_NOT_IMPLEMENTED;
}

tMLError MLOSUnlockMutex(HANDLE mutex)
{
	/* @todo implement if needed */
	return ML_ERROR_FEATURE_NOT_IMPLEMENTED;
}

tMLError MLOSDestroyMutex(HANDLE handle)
{
	/* @todo implement if needed */
	return ML_ERROR_FEATURE_NOT_IMPLEMENTED;
}

FILE *MLOSFOpen(char *filename)
{
	/* @todo implement if needed */
	return NULL;
}

void MLOSFClose(FILE *fp)
{
	/* @todo implement if needed */
}

void MLOSSleep(int mSecs)
{
	msleep(mSecs);
}

unsigned long MLOSGetTickCount(void)
{
#if !defined(CONFIG_MACH_SAMSUNG_P3_P7100)
	struct timespec now;

	getnstimeofday(&now);

	return (long)(now.tv_sec * 1000L + now.tv_nsec / 1000000L);
#else
	/* @todo implement if needed */
	return ML_ERROR_FEATURE_NOT_IMPLEMENTED;
#endif
}
