#ifndef _TDMB_PLATFORM_H_
#define _TDMB_PLATFORM_H_

struct tdmb_platform_data {
	void (*gpio_on) (void);
	void (*gpio_off)(void);
	int irq;
};

#endif /* _TDMB_PLATFORM_H_ */
