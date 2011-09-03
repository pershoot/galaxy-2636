/****************************************************************************
 **
 ** COPYRIGHT(C) : Samsung Electronics Co.Ltd, 2006-2010 ALL RIGHTS RESERVED
 **
 ** AUTHOR       : Song Wei  			@LDK@
 **                WTLFOTA_DPRAM Device Driver for Via6410
 **			Reference: Via6419 DPRAM driver (dpram.c/.h)
 ****************************************************************************/
#ifndef _HSDPA_WTLFOTA_DPRAM
#define _HSDPA_WTLFOTA_DPRAM
#endif
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/irq.h>
#include <linux/poll.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <asm/irq.h>
#include <mach/hardware.h>

#include <mach/hardware.h>
#include <mach/gpio.h>
#include <mach/gpio-sec.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/wakelock.h>
#include <linux/kernel_sec_common.h>

#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/time.h>
#include <linux/if_arp.h>

#include "dpram_uio_driver.h"
#include "wtlfota_dpram.h"



/***************************************************************************/
/*                              GPIO SETTING                               */
/***************************************************************************/
#include <mach/gpio.h>

#define GPIO_LEVEL_LOW				0
#define GPIO_LEVEL_HIGH				1

#define GPIO_DPRAM_INT_N GPIO_DP_INT_AP
#define GPIO_PHONE_RST_N GPIO_CP_RST
#define GPIO_PHONE_ON GPIO_CP_ON

#define IRQ_DPRAM_INT_N   gpio_to_irq(GPIO_DP_INT_AP)
#define IRQ_PHONE_ACTIVE	  gpio_to_irq(GPIO_PHONE_ACTIVE)


extern void tegra_gpio_enable_GPIO_VIA_PS_HOLD_OFF(void);
extern void tegra_gpio_enable_GPIO_DP_INT_AP(void);
extern void tegra_gpio_enable_GPIO_PHONE_ACTIVE(void);
extern void tegra_init_snor(void);

static void init_hw_setting(void)
{
  u32 reg;
  /* initial pin settings - dpram driver control */
  gpio_request(GPIO_DP_INT_AP,"dpram/IRQ_DPRAM_INT_N");
  gpio_direction_input(GPIO_DP_INT_AP);
  tegra_gpio_enable_GPIO_DP_INT_AP();
  set_irq_type(IRQ_DPRAM_INT_N, IRQ_TYPE_EDGE_FALLING);

  gpio_request(GPIO_PHONE_ACTIVE, "dpram/IRQ_PHONE_ACTIVE");
  gpio_direction_input(GPIO_PHONE_ACTIVE);
  tegra_gpio_enable_GPIO_PHONE_ACTIVE();

  if(system_rev > 0x0A){
    if (gpio_is_valid(GPIO_PHONE_ON)) {
      if (gpio_request(GPIO_PHONE_ON, "dpram/GPIO_PHONE_ON"))
	printk(KERN_ERR "request fail GPIO_PHONE_ON\n");
      gpio_direction_output(GPIO_PHONE_ON, GPIO_LEVEL_LOW);
    }
    gpio_set_value(GPIO_PHONE_ON, GPIO_LEVEL_LOW);
  }
  else{
    if (gpio_is_valid(GPIO_CP_ON_REV05)) {
      if (gpio_request(GPIO_CP_ON_REV05, "dpram/GPIO_PHONE_ON"))
	printk(KERN_ERR "request fail GPIO_PHONE_ON\n");
      gpio_direction_output(GPIO_CP_ON_REV05, GPIO_LEVEL_LOW);
    }
    gpio_set_value(GPIO_CP_ON_REV05, GPIO_LEVEL_LOW);
  }

  if (gpio_is_valid(GPIO_PHONE_RST_N)) {
    if (gpio_request(GPIO_PHONE_RST_N, "dpram/GPIO_PHONE_RST_N"))
      printk(KERN_ERR "request fail GPIO_PHONE_RST_N\n");
    reg = readl(IO_ADDRESS(0x6000d108));
    writel(reg | (0x01 << 6), IO_ADDRESS(0x6000d108)); //set phone_reset gpio config output
    gpio_direction_output(GPIO_PHONE_RST_N, GPIO_LEVEL_LOW);
  }

  if (gpio_is_valid(GPIO_VIA_PS_HOLD_OFF)) {
    if (gpio_request(GPIO_VIA_PS_HOLD_OFF, "dpram/GPIO_VIA_PS_HOLD_OFF"))
      printk(KERN_ERR "request fail GPIO_VIA_PS_HOLD_OFF\n");
    reg = readl(IO_ADDRESS(0x6000d184));
    writel(reg | (0x1 << 5), IO_ADDRESS(0x6000d184)); //set gpio via_ps_hold output
    gpio_direction_output(GPIO_VIA_PS_HOLD_OFF, GPIO_LEVEL_HIGH);
    tegra_gpio_enable_GPIO_VIA_PS_HOLD_OFF();
  }

  tegra_init_snor();
}


static void fini_hw_setting(void)
{

  if (gpio_is_valid(GPIO_PHONE_ON)) {
    gpio_free(GPIO_PHONE_ON);
  }
	
  if (gpio_is_valid(GPIO_PHONE_RST_N)) {
    gpio_free(GPIO_PHONE_RST_N);
  }

  if (gpio_is_valid(GPIO_DP_INT_AP)) {
    gpio_free(GPIO_DP_INT_AP);
  }
  if (gpio_is_valid(GPIO_PHONE_ACTIVE)) {
    gpio_free(GPIO_PHONE_ACTIVE);
  }
  if (gpio_is_valid(GPIO_VIA_PS_HOLD_OFF)) {
    gpio_free(GPIO_VIA_PS_HOLD_OFF);
  }
}




static int  wtlfota_dpram_probe(void)
{
  int retval = 0;

  /* @LDK@ H/W setting */
  //  wtlfota_dpram_platform_init();
  init_hw_setting();

  /* retval = wtlfota_dpram_shared_bank_remap(); */
  /* if(retval != 0){ */
  /*   return retval; */
  /* } */


  /* @LDK@ check out missing interrupt from the phone */
  //check_miss_interrupt();

  return retval;
}


/***************************************************************************/
/*                              IOCTL                                      */
/***************************************************************************/

typedef struct gpio_name_value_pair{
  char name[_WTLFOTA_GPIO_PARAM_NAME_LENGTH];
  int value;
}gpio_name_value_pair_t;

#define GPIO_TRANSLATION_LIST_LEN 6
static gpio_name_value_pair_t GPIO_TRANSLATION_LIST[GPIO_TRANSLATION_LIST_LEN]={
  {"GPIO_PHONE_ON", GPIO_PHONE_ON},
  {"GPIO_PHONE_RST_N", GPIO_PHONE_RST_N},
  {"GPIO_DPRAM_INT", GPIO_DPRAM_INT_N},
  {"GPIO_PHONE_ACTIVE", GPIO_PHONE_ACTIVE},
  {"GPIO_VIA_PS_HOLD_OFF", GPIO_VIA_PS_HOLD_OFF},
  {"GPIO_CP_ON_REV05", GPIO_CP_ON_REV05},
};

unsigned int gpio_name_to_value(const char *name){
  unsigned val = -1;
  int i;
  for(i=0; i<GPIO_TRANSLATION_LIST_LEN; i++){
    if(!strncmp(name, GPIO_TRANSLATION_LIST[i].name, _WTLFOTA_GPIO_PARAM_NAME_LENGTH)){
      val = GPIO_TRANSLATION_LIST[i].value;
      break;
    }
  }
  return val;
}

static void dpram_gpio_op(struct _gpio_param *param)
{
  unsigned int gpio;
  //handle fake gpio first
  if(!strncmp(param->name, "FAKE_READONLY_GPIO_system_rev", _WTLFOTA_GPIO_PARAM_NAME_LENGTH)){
    printk("FAKE_READONLY_GPIO_system_rev received!\n");
    if(param->op == _WTLFOTA_GPIO_OP_READ){
      param->data = system_rev;
    }
    return;
  }

  gpio = gpio_name_to_value(param->name);
  if(gpio < 0){
    printk("gpio name not recognized in dpram_gpio_op\n");
    return;
  }
  /* //debugging */
  /* printk("gpio name %s, gpio value:0x%x\n", param->name, gpio); */
  /* return; */

  switch(param->op){
  case _WTLFOTA_GPIO_OP_READ:
    param->data = gpio_get_value(gpio);
    break;
  case _WTLFOTA_GPIO_OP_WRITE:
    gpio_set_value(gpio, param->data);
    break;
  default:
    break;
  }
}


static long dpramctl_ioctl(struct file *file,
			   unsigned int cmd, unsigned long l)
{

  long ret;
  unsigned char *arg = (unsigned char *)l;
  switch (cmd) {
  case DPRAM_GPIO_OP:
    {
      struct _gpio_param param;
      ret = copy_from_user((void *)&param, (void *)arg, sizeof(param));
      if(ret != 0){
	printk("copy_from_user in dpramctl_ioctl failed!\n");
	return -EINVAL;
      }
      dpram_gpio_op(&param);
      if (param.op == _WTLFOTA_GPIO_OP_READ){
	return copy_to_user((unsigned long *)arg, &param, sizeof(param));
      }
      return 0;
    }
  default:
    break;
  }
  return -EINVAL;
}

static struct file_operations dpramctl_fops = {
  .owner =	THIS_MODULE,
  .unlocked_ioctl =	dpramctl_ioctl,
  .llseek =	no_llseek,
};

//use the minor number of dpramctl_dev in dev
static struct miscdevice dpramctl_dev = {
  .minor =	132, //MISC_DYNAMIC_MINOR,
  .name =		IOCTL_DEVICE_NAME,
  .fops =		&dpramctl_fops,
};

/**ioctl ends**/

static struct uio_info uinfo={
  .name = "wtlfota_dpram",
  .version = "0.0.1",
};

static irqreturn_t IRQ_WTLFOTA_DPRAM_INT_N_handler(int irq, struct uio_info *dev_info){
  return IRQ_HANDLED;
}
static int uio_register(struct uio_info * info ){
  int retval;
  info->mem[0].addr = WTLFOTA_DPRAM_START_ADDRESS_PHYS + WTLFOTA_DPRAM_SHARED_BANK;
  info->mem[0].size = WTLFOTA_DPRAM_SHARED_BANK_SIZE;
  info->mem[0].memtype = UIO_MEM_PHYS;

  info->irq = IRQ_DPRAM_INT_N;
  info->irq_flags=IRQF_DISABLED;
  //info->irq = UIO_IRQ_NONE;
  info->handler = IRQ_WTLFOTA_DPRAM_INT_N_handler;
  //todo: i should use global encapsulation here. just for debugging
  retval = dpram_uio_register_device(dpramctl_dev.this_device, info);
  return retval;
}

static void uio_unregister(struct uio_info * info ){
  dpram_uio_unregister_device(info);
}


/* init & cleanup. */
static int __init wtlfota_dpram_init(void)
{
  int ret;
  ret =  wtlfota_dpram_probe();
  if(ret != 0){
    printk("wtlfota_dpram_probe fail!\n");
    return -1;
  }
  ret = misc_register(&dpramctl_dev);
  if (ret < 0) {
    printk("misc_register() failed\n");
    return -1;
  }
  ret = uio_register(&uinfo);
  if (ret != 0) {
    printk("uio_register() failed\n");
    return -1;
  }
  printk("system_rev  is 0x%x\n", system_rev  );
  printk("wtlfota_dpram_init returning %d\n", ret);
  return ret;
}
static void __exit wtlfota_dpram_exit(void)
{
  uio_unregister(&uinfo);
  misc_deregister(&dpramctl_dev);
  fini_hw_setting();
  printk("wtlfota_dpram_exit returning\n");
}

module_init(wtlfota_dpram_init);
module_exit(wtlfota_dpram_exit);

MODULE_AUTHOR("SAMSUNG ELECTRONICS CO., LTD");

MODULE_DESCRIPTION("WTLFOTA_DPRAM Device Driver.");

MODULE_LICENSE("GPL v2");
