#include <linux/io.h>
#include "tdmb.h"

#define TDMB_BASE_ADDR_PHYS 0x98000000
void *addr_TDMB_CS4_V;

int tdmb_init_bus(void)
{
	addr_TDMB_CS4_V = ioremap(TDMB_BASE_ADDR_PHYS, PAGE_SIZE);
	DPRINTK("TDMB EBI2 Init addr_TDMB_CS4_V(0x%x)\n", addr_TDMB_CS4_V);
	return 0;
}

void tdmb_exit_bus(void)
{
	addr_TDMB_CS4_V = NULL;
}
