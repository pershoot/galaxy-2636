/*
 * Integrity check code for crypto module.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#include <crypto/hash.h>
#include <crypto/sha.h>
#include <linux/err.h>
#include <linux/scatterlist.h>
#include <asm-generic/sections.h>

#include "internal.h"

#define ZIMAGE_ADDR (_stext + 0x20000000)

static bool integrity_checked = false;

void do_integrity_check(void) {
	u8 *rbuf = (u8 *) ZIMAGE_ADDR;
	u32 len;
	u8 hmac[SHA256_DIGEST_SIZE];
	struct hash_desc desc;
	struct scatterlist sg;
	u8* key="12345678";

	printk(KERN_INFO "FIPS: do kernel integrity check\n");

	if (unlikely(integrity_checked || in_fips_err())) return;

	if ( *((u32*) &rbuf[36]) != 0x016F2818) {
		printk(KERN_ERR "FIPS: invalid zImage magic number.");
		set_in_fips_err();
		goto err;
	}

	printk(KERN_INFO "FIPS: read zImage magic number\n");

	if (*(u32*)&rbuf[44] < *(u32*)&rbuf[40]) {
                printk(KERN_ERR "FIPS: invalid zImage calculated len");
                set_in_fips_err();
                goto err;
	}
	len = *(u32*)&rbuf[44] - *(u32*)&rbuf[40];
        printk(KERN_INFO "FIPS: read zImage len %d\n",len);

	desc.tfm = crypto_alloc_hash("hmac(sha256)",0,0);

	if (IS_ERR(desc.tfm)) {
		printk(KERN_ERR "FIPS: integ failed to allocate tfm %ld\n",PTR_ERR(desc.tfm));
		set_in_fips_err();
		goto err;	
	}

        printk(KERN_INFO "FIPS: alloc hash ok\n");
	sg_init_one(&sg, rbuf, len);
        printk(KERN_INFO "FIPS: sg init \n");
	crypto_hash_setkey(desc.tfm,key,strlen(key));
        printk(KERN_INFO "FIPS: crypto set key \n");
	crypto_hash_digest(&desc,&sg,len,hmac);	

	if (!strncmp(hmac,&rbuf[len],SHA256_DIGEST_SIZE)) {
		printk(KERN_INFO "FIPS: integrity check passed\n");
	} else {
		printk(KERN_ERR "FIPS: integrity check failed\n");
		set_in_fips_err();
	}

err:
	integrity_checked=true;
	crypto_free_hash(desc.tfm);

	return;
}

