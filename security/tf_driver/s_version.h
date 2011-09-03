/*
 * Copyright (c) 2006-2010 Trusted Logic S.A.
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

#ifndef __S_VERSION_H__
#define __S_VERSION_H__

/*
 * Usage: define S_VERSION_BUILD on the compiler's command line.
 *
 * Then, you get:
 * - S_VERSION_MAIN "X.Y"
 * - S_VERSION_BUILD = 0 if S_VERSION_BUILD not defined or empty
 * - S_VERSION_STRING = "TFO[O][P] X.Y.N     " or "TFO[O][P] X.Y.N D   "
 * - S_VERSION_RESOURCE = X,Y,0,N
 */

#ifdef S_VERSION_BUILD
/* TRICK: detect if S_VERSION is defined but empty */
#if 0 == S_VERSION_BUILD-0
#undef  S_VERSION_BUILD
#define S_VERSION_BUILD 0
#endif
#else
/* S_VERSION_BUILD is not defined */
#define S_VERSION_BUILD 0
#endif

#define __STRINGIFY(X) #X
#define __STRINGIFY2(X) __STRINGIFY(X)

#if !defined(NDEBUG) || defined(_DEBUG)
#define S_VERSION_VARIANT_DEBUG "D"
#else
#define S_VERSION_VARIANT_DEBUG " "
#endif

#ifdef STANDARD
#define S_VERSION_VARIANT_STANDARD "S"
#else
#define S_VERSION_VARIANT_STANDARD " "
#endif

#define S_VERSION_VARIANT S_VERSION_VARIANT_STANDARD S_VERSION_VARIANT_DEBUG " "

/*
 * This version number must be updated for each new release
 */
#define S_VERSION_MAIN  "08.01"
#define S_VERSION_RESOURCE 8,1,0,S_VERSION_BUILD

/*
 * Products Versioning
 */
#if defined(WIN32)

/* Win32 Simulator and all Win32 Side Components */
#define PRODUCT_NAME "TFOWX"

#elif defined(__ANDROID32__)

#define PRODUCT_NAME "UNKWN"

#elif defined(LINUX)

#if defined(__ARM_EABI__)
/* arm architecture -> Cortex-A8 */
#define PRODUCT_NAME "TFOLB"
#else
/* ix86 architecture -> Linux Simulator and all Linux Side Components */
#define PRODUCT_NAME "TFOLX"
#endif

#else

/* Not OS specififc -> Cortex-A8 Secure Binary */
#define PRODUCT_NAME "TFOXB"

#endif

#define S_VERSION_STRING \
         PRODUCT_NAME S_VERSION_MAIN  "." \
         __STRINGIFY2(S_VERSION_BUILD) " " \
         S_VERSION_VARIANT

#endif /* __S_VERSION_H__ */
