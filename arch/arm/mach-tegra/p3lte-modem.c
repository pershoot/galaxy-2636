#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <mach/gpio.h>
#include <mach/gpio-p4lte.h>
#include <mach/irqs.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/regulator/consumer.h>
#include <linux/modemctl.h>

static struct modemctl_platform_data mdmctl_data;

static void modemctl_cfg_gpio(void)
{
	unsigned gpio_phone_on = mdmctl_data.gpio_phone_on;
	unsigned gpio_phone_off = mdmctl_data.gpio_phone_off;
	unsigned gpio_cp_rst = mdmctl_data.gpio_cp_reset;
	unsigned gpio_slave_wakeup = mdmctl_data.gpio_slave_wakeup;    
	unsigned gpio_host_wakeup = mdmctl_data.gpio_host_wakeup;
	unsigned gpio_host_active = mdmctl_data.gpio_host_active;
//	unsigned gpio_pda_active = mdmctl_data.gpio_pda_active;    
	unsigned gpio_phone_active;

         if(system_rev > 0xA){
		gpio_phone_active = GPIO_LTE_ACTIVE;            
         }
         else{
		gpio_phone_active = GPIO_LTE2AP_STATUS;            
         }            

	printk(KERN_INFO "%s IN!\n", __func__);

	if (gpio_is_valid(gpio_phone_on)) {
		if (gpio_request(gpio_phone_on, "220_PMIC_PWRON"))
			printk(KERN_ERR "request fail 220_PMIC_PWRON\n");
		gpio_direction_output(gpio_phone_on, 0);
	}
	tegra_gpio_enable(gpio_phone_on);    
   
	if (gpio_is_valid(gpio_phone_off)) {
		if (gpio_request(gpio_phone_off, "220_PMIC_PWRHOLD_OFF"))
			printk(KERN_ERR "request fail 220_PMIC_PWRHOLD_OFF\n");
		gpio_direction_output(gpio_phone_off, 0);
	}
	tegra_gpio_enable(gpio_phone_off);    

	if (gpio_is_valid(gpio_cp_rst)) {
		if (gpio_request(gpio_cp_rst, "GPIO_CMC_RST"))
			printk(KERN_ERR "request fail GPIO_CMC_RST\n");
		gpio_direction_output(gpio_cp_rst, 0);
	}
	tegra_gpio_enable(gpio_cp_rst);    

	if (gpio_is_valid(gpio_slave_wakeup)) {
		if (gpio_request(gpio_slave_wakeup, "GPIO_AP2LTE_WAKEUP"))
			printk(KERN_ERR "request fail GPIO_AP2LTE_WAKEUP\n");
		gpio_direction_output(gpio_slave_wakeup, 0);
	}
	tegra_gpio_enable(gpio_slave_wakeup); 
 
	if (gpio_is_valid(gpio_host_wakeup)) {
		if (gpio_request(gpio_host_wakeup, "GPIO_LTE2AP_WAKEUP"))
			printk(KERN_ERR "request fail GPIO_LTE2AP_WAKEUP\n");
		gpio_direction_input(gpio_host_wakeup);
	}
	tegra_gpio_enable(gpio_host_wakeup);    

	if (gpio_is_valid(gpio_host_active)) {
		if (gpio_request(gpio_host_active, "GPIO_AP2LTE_STATUS"))
			printk(KERN_ERR "request fail GPIO_AP2LTE_STATUS\n");
		gpio_direction_output(gpio_host_active, 0);
	}
	tegra_gpio_enable(gpio_host_active); 

	if (gpio_is_valid(gpio_phone_active)) {
		if (gpio_request(gpio_phone_active, "GPIO_LTE_ACTIVE"))
			printk(KERN_ERR "request fail GPIO_LTE_ACTIVE\n");
		gpio_direction_input(gpio_phone_active);
	}
	tegra_gpio_enable(gpio_phone_active);    

#if 0   
	gpio_set_value(gpio_phone_on, 1);
	msleep(300);

   	gpio_set_value(gpio_cp_rst, 1);
	gpio_set_value(gpio_phone_off, 1);
	msleep(300);
	
	printk(KERN_ERR "%s gpio_phone_on = %d, gpio_phone_off = %d, gpio_cp_rst = %d\n", __func__, gpio_get_value(gpio_phone_on), gpio_get_value(gpio_phone_off), gpio_get_value(gpio_cp_rst));
#endif    

}

static void lte_on(struct modemctl *mc)
{
	printk(KERN_INFO "%s\n", __func__);
    
	if(!mc->gpio_phone_off ||!mc->gpio_phone_on ||!mc->gpio_cp_reset)
		return;

	gpio_set_value(mc->gpio_phone_on, 1);
	msleep(300);

   	gpio_set_value(mc->gpio_cp_reset, 1);
	gpio_set_value(mc->gpio_phone_off, 1);
	msleep(300);	 
}

static void lte_off(struct modemctl *mc)
{
	printk(KERN_INFO "%s\n", __func__);

	if(!mc->gpio_phone_off ||!mc->gpio_phone_on ||!mc->gpio_cp_reset)
		return;

	gpio_set_value(mc->gpio_phone_on, 0);
         	gpio_set_value(mc->gpio_cp_reset, 0); 
	mdelay(300);

	gpio_set_value(mc->gpio_phone_off, 0);
         mdelay(300);

}

static void lte_reset(struct modemctl *mc)
{
	printk(KERN_INFO "%s\n", __func__);

	if(!mc->gpio_cp_reset)
		return;    

	gpio_set_value(mc->gpio_cp_reset, 0); 
	msleep(100);  

	gpio_set_value(mc->gpio_cp_reset, 1);
	msleep(100);     
}

/* move the PDA_ACTIVE Pin control to sleep_gpio_table */
static void lte_suspend(struct modemctl *mc)
{
	//xmm6260_vcc_off(mc);
}

static void lte_resume(struct modemctl *mc)
{
#ifndef CONFIG_SAMSUNG_PHONE_SVNET
	printk(KERN_INFO "%s\n", __func__);
	gpio_set_value(mc->gpio_host_active, 1);
#endif
}

static struct modemctl_platform_data mdmctl_data = {
	.name = "lte",
	.gpio_phone_on = GPIO_220_PMIC_PWRON,
	.gpio_phone_off = GPIO_220_PMIC_PWRHOLD_OFF,
	.gpio_cp_reset = GPIO_CMC_RST,	
	.gpio_slave_wakeup = GPIO_AP2LTE_WAKEUP,
	.gpio_host_wakeup = GPIO_LTE2AP_WAKEUP,
	.gpio_host_active = GPIO_AP2LTE_STATUS,
//	.gpio_pda_active = GPIO_PDA_ACTIVE,
	.ops = {
		.modem_on = lte_on,
		.modem_off = lte_off,
		.modem_reset = lte_reset,
		.modem_suspend = lte_suspend,
		.modem_resume = lte_resume,
		.modem_cfg_gpio = modemctl_cfg_gpio,
	}
};

/* TODO: check the IRQs..... */
static struct resource mdmctl_res[] = {
	[0] = {
		.start = TEGRA_GPIO_TO_IRQ(GPIO_LTE2AP_WAKEUP),
		.end = TEGRA_GPIO_TO_IRQ(GPIO_LTE2AP_WAKEUP),
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device modemctl = {
	.name = "modemctl",
	.id = -1,
	.num_resources = ARRAY_SIZE(mdmctl_res),
	.resource = mdmctl_res,
	.dev = {
		.platform_data = &mdmctl_data,
	},
};
