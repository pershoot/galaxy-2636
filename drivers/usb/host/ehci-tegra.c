/*
 * EHCI-compliant USB host controller driver for NVIDIA Tegra SoCs
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (C) 2009 - 2011 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/platform_data/tegra_usb.h>
#include <linux/irq.h>
#include <linux/usb/otg.h>
#include <mach/usb_phy.h>
#include <mach/suspend.h>
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
#ifdef CONFIG_USB_HOST_NOTIFY
#include <linux/host_notify.h>
#endif
#endif

#define TEGRA_USB_USBCMD_REG_OFFSET		0x140
#define TEGRA_USB_USBCMD_RESET			(1 << 1)
#define TEGRA_USB_USBMODE_REG_OFFSET		0x1a8
#define TEGRA_USB_USBMODE_HOST			(3 << 0)
#define TEGRA_USB_PORTSC1_PTC(x)		(((x) & 0xf) << 16)
#define TEGRA_USB_TXFILLTUNING			0x164

#define TEGRA_USB_DMA_ALIGN 32


struct tegra_ehci_context {
	bool valid;
	u32 command;
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	u32 intr_enable;
#endif
	u32 frame_list;
	u32 async_next;
	u32 txfilltunning;
	u32 otgsc;
	enum tegra_usb_phy_port_speed port_speed;
};

struct tegra_ehci_hcd {
	struct ehci_hcd *ehci;
	struct tegra_usb_phy *phy;
	struct clk *clk;
	struct clk *clk_min;	
	struct clk *emc_clk;
	struct clk *sclk_clk;
	struct otg_transceiver *transceiver;
	int hub_suspend_req;	
	int host_resumed;
	int bus_is_power_down;
	int port_resuming;
	struct tegra_ehci_context context;
	int require_power_down_on_bus_suspend;
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA	
	bool device_ready_for_reset;
#endif
	struct delayed_work work;
};

#ifdef CONFIG_SAMSUNG_LTE_MODEMCTL
extern int mc_is_modem_on(void);
extern int mc_is_host_wakeup(void);
extern int mc_control_slave_wakeup(int val);
extern int mc_control_active_state(int val);
#endif

#if defined CONFIG_USB_EHCI_ONOFF_FEATURE
/*
 * with core dvfs phy clock turn off at suspend
 * when silent reset from modem has called during suspend with phy clock off
 * it needs time to set up clock before hsic phy power on
 */
bool  ehci_power_up_by_sysfs;
#endif

static void tegra_ehci_power_up(struct usb_hcd *hcd)
{
	struct tegra_ehci_hcd *tegra = dev_get_drvdata(hcd->self.controller);

	clk_enable(tegra->emc_clk);
	clk_enable(tegra->sclk_clk);
	clk_enable(tegra->clk);
#if defined CONFIG_USB_EHCI_ONOFF_FEATURE
	if (ehci_power_up_by_sysfs && tegra->phy->instance == 1) {
		ehci_power_up_by_sysfs = false;
		mdelay(50);
	}
#endif
	tegra_usb_phy_power_on(tegra->phy);
	tegra->host_resumed = 1;
	tegra->bus_is_power_down = 0;	
}

static void tegra_ehci_power_down(struct usb_hcd *hcd)
{
	struct tegra_ehci_hcd *tegra = dev_get_drvdata(hcd->self.controller);

	tegra->host_resumed = 0;
	tegra_usb_phy_power_off(tegra->phy);
	if (tegra->bus_is_power_down) {
		clk_disable(tegra->clk_min);
		return;
	}	
	clk_disable(tegra->clk);
	clk_disable(tegra->sclk_clk);
	clk_disable(tegra->emc_clk);
}

static int tegra_ehci_hub_control(
	struct usb_hcd	*hcd,
	u16		typeReq,
	u16		wValue,
	u16		wIndex,
	char		*buf,
	u16		wLength
)
{
	struct ehci_hcd	*ehci = hcd_to_ehci(hcd);
	int		ports = HCS_N_PORTS(ehci->hcs_params);
	u32 __iomem	*status_reg = &ehci->regs->port_status[ (wIndex & 0xff) - 1];
	
	u32		temp, status;
	unsigned long	flags;
	int		retval = 0;
	unsigned	selector;
	struct		tegra_ehci_hcd *tegra = dev_get_drvdata(hcd->self.controller);
	bool		hsic = false;
	unsigned	retry_connect = 20;

	if (tegra->phy->instance == 1) {
		struct tegra_ulpi_config *config = tegra->phy->config;
		hsic = (config->inf_type == TEGRA_USB_UHSIC);
	}

	status_reg = &ehci->regs->port_status[(wIndex & 0xff) - 1];

	spin_lock_irqsave(&ehci->lock, flags);

	/*
	 * In ehci_hub_control() for USB_PORT_FEAT_ENABLE clears the other bits
	 * that are write on clear, by writing back the register read value, so
	 * USB_PORT_FEAT_ENABLE is handled by masking the set on clear bits
	 */
	if (typeReq == ClearPortFeature && wValue == USB_PORT_FEAT_ENABLE) {
		temp = ehci_readl(ehci, status_reg);
		ehci_writel(ehci, (temp & ~PORT_RWC_BITS) & ~PORT_PE, status_reg);
		goto done;
	} else if (typeReq == GetPortStatus) {
		temp = ehci_readl(ehci, status_reg);
		if (tegra->port_resuming && !(temp & PORT_SUSPEND)) {
			/* resume completed */
			tegra->port_resuming = 0;
			tegra_usb_phy_postresume(tegra->phy);
		}
	}

	else if (typeReq == SetPortFeature && wValue == USB_PORT_FEAT_SUSPEND) {
		temp = ehci_readl(ehci, status_reg);
		if ((temp & PORT_PE) == 0 || (temp & PORT_RESET) != 0) {
			retval = -EPIPE;
			goto done;
		}

		/* After above check the port must be connected.
		* Set appropriate bit thus could put phy into low power
		* mode if we have hostpc feature
		*/
		temp &= ~PORT_WKCONN_E;
		temp |= PORT_WKDISC_E | PORT_WKOC_E;
		ehci_writel(ehci, temp | PORT_SUSPEND, status_reg);
		/* Need a 4ms delay before the controller goes to suspend */
		mdelay(4);

		/*
		 * If a transaction is in progress, there may be a delay in
		 * suspending the port. Poll until the port is suspended.
		 */
		if (handshake(ehci, status_reg, PORT_SUSPEND,
						PORT_SUSPEND, 5000))
			pr_err("%s: timeout waiting for PORT_SUSPEND\n", __func__);
		set_bit((wIndex & 0xff) - 1, &ehci->suspended_ports);
		
		tegra->hub_suspend_req = 1;		

		goto done;
	}

	/*
	* Tegra host controller will time the resume operation to clear the bit
	* when the port control state switches to HS or FS Idle. This behavior
	* is different from EHCI where the host controller driver is required
	* to set this bit to a zero after the resume duration is timed in the
	* driver.
	*/

	else if (typeReq == ClearPortFeature && wValue == USB_PORT_FEAT_SUSPEND) {
		pr_debug("non-HSIC ClearPortFeature: USB_PORT_FEAT_SUSPEND\n");
		temp = ehci_readl(ehci, status_reg);
		if ((temp & PORT_RESET) || !(temp & PORT_PE)) {
			retval = -EPIPE;
			goto done;
		}

		if (!(temp & PORT_SUSPEND))
			goto done;

		tegra_usb_phy_preresume(tegra->phy);

		ehci->reset_done[wIndex-1] = jiffies + msecs_to_jiffies(25);

		temp &= ~(PORT_RWC_BITS | PORT_WAKE_BITS);
		/* start resume signalling */
		ehci_writel(ehci, temp | PORT_RESUME, status_reg);

		spin_unlock_irqrestore(&ehci->lock, flags);
		msleep(20);
		spin_lock_irqsave(&ehci->lock, flags);

		/* polling PORT_RESUME until the controller clear this bit */
		if (handshake(ehci, status_reg, PORT_RESUME, 0, 2000))
			pr_err("%s: timeout waiting for PORT_RESUME\n", __func__);

		/* polling PORT_SUSPEND until the controller clear this bit */
		if (handshake(ehci, status_reg, PORT_SUSPEND, 0, 2000))
			pr_err("%s: timeout waiting for PORT_SUSPEND\n", __func__);

		ehci->reset_done[wIndex-1] = 0;

		tegra->port_resuming = 1;
		goto done;
	}

	/* Handle port reset here */
	if ((hsic) && (typeReq == SetPortFeature) &&
		((wValue == USB_PORT_FEAT_RESET) || (wValue == USB_PORT_FEAT_POWER))) {
		selector = wIndex >> 8;
		wIndex &= 0xff;
		if (!wIndex || wIndex > ports) {
			retval = -EPIPE;
			goto done;
		}
		wIndex--;
		status = 0;
		temp = ehci_readl(ehci, status_reg);
		if (temp & PORT_OWNER)
			goto done;
		temp &= ~PORT_RWC_BITS;

		switch (wValue) {
		case USB_PORT_FEAT_RESET:
		{
			if (temp & PORT_RESUME) {
				retval = -EPIPE;
				goto done;
			}
			/* line status bits may report this as low speed,
			* which can be fine if this root hub has a
			* transaction translator built in.
			*/
			if ((temp & (PORT_PE|PORT_CONNECT)) == PORT_CONNECT
					&& !ehci_is_TDI(ehci) && PORT_USB11 (temp)) {
				ehci_dbg (ehci, "port %d low speed --> companion\n", wIndex + 1);
				temp |= PORT_OWNER;
				ehci_writel(ehci, temp, status_reg);
			} else {
				ehci_vdbg(ehci, "port %d reset\n", wIndex + 1);
				temp &= ~PORT_PE;
				/*
				* caller must wait, then call GetPortStatus
				* usb 2.0 spec says 50 ms resets on root
				*/
				ehci->reset_done[wIndex] = jiffies + msecs_to_jiffies(50);
				ehci_writel(ehci, temp, status_reg);
				if (hsic && (wIndex == 0)) {
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
					if(!tegra->device_ready_for_reset) {
						tegra_usb_phy_bus_idle(tegra->phy);
						while(retry_connect) {
							if (tegra_usb_phy_is_device_connected(tegra->phy)) {
								tegra->device_ready_for_reset = true;
								break;
							}
							spin_unlock_irqrestore(&ehci->lock, flags);
							msleep(50);
							spin_lock_irqsave(&ehci->lock, flags);
							retry_connect--;
						}
					}
					if(tegra->device_ready_for_reset) {
						retval = tegra_usb_phy_bus_reset(tegra->phy);
						tegra->device_ready_for_reset = false;
					}
#else
					tegra_usb_phy_bus_reset(tegra->phy);
#endif
				}
			}

			break;
		}
		case USB_PORT_FEAT_POWER:
		{
			if (HCS_PPC(ehci->hcs_params))
				ehci_writel(ehci, temp | PORT_POWER, status_reg);
			if (hsic && (wIndex == 0)) {
				tegra_usb_phy_bus_connect(tegra->phy);
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
				tegra->device_ready_for_reset = true;
#endif
			}
			break;
		}
		}
		goto done;
	}

	spin_unlock_irqrestore(&ehci->lock, flags);

	/* Handle the hub control events here */
	return ehci_hub_control(hcd, typeReq, wValue, wIndex, buf, wLength);
done:
	spin_unlock_irqrestore(&ehci->lock, flags);
	return retval;
}

static void tegra_ehci_restart(struct usb_hcd *hcd)
{
	unsigned int temp;
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);

	/* Set to Host mode by setting bit 0-1 of USB device mode register */
	temp = readl(hcd->regs + TEGRA_USB_USBMODE_REG_OFFSET);
	writel((temp | TEGRA_USB_USBMODE_HOST),
		(hcd->regs + TEGRA_USB_USBMODE_REG_OFFSET));

	/* reset the ehci controller */
	ehci->controller_resets_phy = 0;
	ehci_reset(ehci);
	ehci->controller_resets_phy = 1;

	/* setup the frame list and Async q heads */
	ehci_writel(ehci, ehci->periodic_dma, &ehci->regs->frame_list);
	ehci_writel(ehci, (u32)ehci->async->qh_dma, &ehci->regs->async_next);
	/* setup the command register and set the controller in RUN mode */
	ehci->command &= ~(CMD_LRESET|CMD_IAAD|CMD_PSE|CMD_ASE|CMD_RESET);
	ehci->command |= CMD_RUN;
	ehci_writel(ehci, ehci->command, &ehci->regs->command);

	/* Enable the root Port Power */
	if (HCS_PPC(ehci->hcs_params)) {
		temp = ehci_readl(ehci, &ehci->regs->port_status[0]);
		ehci_writel(ehci, temp | PORT_POWER, &ehci->regs->port_status[0]);
	}

	down_write(&ehci_cf_port_reset_rwsem);
	hcd->state = HC_STATE_RUNNING;
	ehci_writel(ehci, FLAG_CF, &ehci->regs->configured_flag);
	/* flush posted writes */
	ehci_readl(ehci, &ehci->regs->command);
	up_write(&ehci_cf_port_reset_rwsem);

	/* Turn On Interrupts */
	ehci_writel(ehci, INTR_MASK, &ehci->regs->intr_enable);
}

static int tegra_usb_suspend(struct usb_hcd *hcd)
{
	struct tegra_ehci_hcd *tegra = dev_get_drvdata(hcd->self.controller);
	struct ehci_regs __iomem *hw = tegra->ehci->regs;
	struct tegra_ehci_context *context = &tegra->context;
	unsigned long flags;
	int hsic = 0;
	struct tegra_ulpi_config *config;

	if (tegra->phy->instance == 1) {
		config = tegra->phy->config;
		hsic = (config->inf_type == TEGRA_USB_UHSIC);
	}

	spin_lock_irqsave(&tegra->ehci->lock, flags);

#ifdef CONFIG_SAMSUNG_PHONE_SVNET
#if 0
	if (tegra->phy->instance == 2) {
	  struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	  ehci_writel(ehci, 0, &ehci->regs->intr_enable);
	  (void)ehci_readl(ehci, &ehci->regs->intr_enable);
	}
#endif
#endif
	context->port_speed = (readl(&hw->port_status[0]) >> 26) & 0x3;

	if ((context->port_speed > TEGRA_USB_PHY_PORT_HIGH) || hsic) {
		/* If no device connection or invalid speeds,
		 * don't save the context */
		context->valid = false;
	} else {
		context->command	= readl(&hw->command);
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
		context->intr_enable	= readl(&hw->intr_enable);
#endif
		context->frame_list	= readl(&hw->frame_list);
		context->async_next	= readl(&hw->async_next);
		context->txfilltunning	= readl(&hw->reserved[2]);
		context->otgsc		= readl(&hw->reserved[18]);
		context->valid = true;
	}

	ehci_halt(tegra->ehci);
	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	if (hsic)
		tegra->device_ready_for_reset = false;
#endif
	spin_unlock_irqrestore(&tegra->ehci->lock, flags);


#ifdef CONFIG_SAMSUNG_LTE_MODEMCTL
	if (tegra->phy->instance == 2)
		mc_control_active_state(0); /*HSIC IPC control the ACTIVE_STATE*/
#endif

	tegra_ehci_power_down(ehci_to_hcd(tegra->ehci));
	return 0;
}

static int tegra_usb_resume(struct usb_hcd *hcd)
{
	struct tegra_ehci_hcd *tegra = dev_get_drvdata(hcd->self.controller);
	struct tegra_ehci_context *context = &tegra->context;
	struct usb_device *udev = hcd->self.root_hub;
	struct ehci_regs __iomem *hw = tegra->ehci->regs;
	unsigned long val;
	int lp0_resume = 0;
	int hsic = 0;
	struct tegra_ulpi_config *config;

	if (tegra->phy->instance == 1) {
		config = tegra->phy->config;
		hsic = (config->inf_type == TEGRA_USB_UHSIC);
	}
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	/* for hsic, set accessible bit before power up */
	/* it prevent infinite report_bad_irq */
	if (hsic) {
		set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
		tegra_ehci_power_up(ehci_to_hcd(tegra->ehci));
	} 
#ifdef CONFIG_MACH_SAMSUNG_P4LTE
	else if (tegra->phy->instance == 2) {
		pr_err("%s: set_bit HCD_FLAG_HW_ACCESSIBLE first to prevent 'spurious bad irq' making lockup\n", __func__);
		set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
		tegra_ehci_power_up(ehci_to_hcd(tegra->ehci));
	}	
#endif
	else {
		tegra_ehci_power_up(ehci_to_hcd(tegra->ehci));
		set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	}
#else
	tegra_ehci_power_up(ehci_to_hcd(tegra->ehci));
	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
#endif

	if (!context->valid) {
		/* Wait for the phy to detect new devices
		 * before we restart the controller */
		if (!hsic)
			msleep(10);
		goto restart;
	}

	tegra_ehci_phy_restore_start(tegra->phy, context->port_speed);

	/* Check if the phy resume from LP0. When the phy resume from LP0
	 * USB register will be reset. */
	if (!readl(&hw->async_next))
		lp0_resume = 1;

	/* Restore register context */
	writel(TEGRA_USB_USBMODE_HOST, &hw->reserved[19]);
	writel(context->otgsc,         &hw->reserved[18]);
	writel(context->txfilltunning, &hw->reserved[2]);
	writel(context->async_next,    &hw->async_next);
	writel(context->frame_list,    &hw->frame_list);
	writel(context->command,       &hw->command);

	/* Enable Port Power */
	val = readl(&hw->port_status[0]);
	val |= PORT_POWER;
	writel(val, &hw->port_status[0]);
	udelay(10);

	if (lp0_resume) {
		/* Program the field PTC in PORTSC based on the saved speed mode */
		val = readl(&hw->port_status[0]);
		val &= ~(TEGRA_USB_PORTSC1_PTC(~0));
		if (context->port_speed == TEGRA_USB_PHY_PORT_HIGH)
			val |= TEGRA_USB_PORTSC1_PTC(5);
		else if (context->port_speed == TEGRA_USB_PHY_PORT_SPEED_FULL)
			val |= TEGRA_USB_PORTSC1_PTC(6);
		else if (context->port_speed == TEGRA_USB_PHY_PORT_SPEED_LOW)
			val |= TEGRA_USB_PORTSC1_PTC(7);
		writel(val, &hw->port_status[0]);
		udelay(10);
	}

	/* Disable test mode by setting PTC field to NORMAL_OP */
	val = readl(&hw->port_status[0]);
	val &= ~(TEGRA_USB_PORTSC1_PTC(~0));
	writel(val, &hw->port_status[0]);
	udelay(10);

	/* Poll until CCS is enabled */
	if (handshake(tegra->ehci, &hw->port_status[0], PORT_CONNECT,
							PORT_CONNECT, 2000)) {
		pr_err("%s: timeout waiting for PORT_CONNECT\n", __func__);
		goto restart;
	}

	/* Poll until PE is enabled */
	if (handshake(tegra->ehci, &hw->port_status[0], PORT_PE,
							PORT_PE, 2000)) {
		pr_err("%s: timeout waiting for USB_PORTSC1_PE\n", __func__);
		goto restart;
	}

	/* Clear the PCI status, to avoid an interrupt taken upon resume */
	val = readl(&hw->status);
	val |= STS_PCD;
	writel(val, &hw->status);

	/* Put controller in suspend mode by writing 1 to SUSP bit of PORTSC */
	val = readl(&hw->port_status[0]);
	if ((val & PORT_POWER) && (val & PORT_PE)) {
		val |= PORT_SUSPEND;
		writel(val, &hw->port_status[0]);
		/* Need a 4ms delay before the controller goes to suspend */
		mdelay(4);

		/* Wait until port suspend completes */
		if (handshake(tegra->ehci, &hw->port_status[0], PORT_SUSPEND,
							PORT_SUSPEND, 1000)) {
			pr_err("%s: timeout waiting for PORT_SUSPEND\n",
								__func__);
			goto restart;
		}
	}

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	/* Restore interrupt register */
	writel(context->intr_enable, &hw->intr_enable);
	udelay(10);
#endif

	tegra_ehci_phy_restore_end(tegra->phy);

#ifdef CONFIG_SAMSUNG_PHONE_SVNET
	if (tegra->phy->instance == 2)
		usb_root_hub_lost_power(hcd->self.root_hub);
#endif

	return 0;

restart:
	if (context->valid)
		tegra_ehci_phy_restore_end(tegra->phy);
	if (hsic) {

		struct ehci_hcd *ehci = hcd_to_ehci(hcd);

		if (!ehci || !ehci->regs || !ehci->async)
			return 0;
		val = readl(&hw->port_status[0]);
		if (!((val & PORT_POWER) && (val & PORT_PE))) {
			tegra_ehci_restart(hcd);
			usb_set_device_state(udev, USB_STATE_CONFIGURED);
		}
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
		{
			struct ehci_hcd *ehci = hcd_to_ehci(hcd);
			pr_info("portsc1 A %x \n",
				readl(&ehci->regs->port_status[0]));
		}
#endif
		tegra_usb_phy_bus_idle(tegra->phy);

		if (!tegra_usb_phy_is_device_connected(tegra->phy))
			schedule_delayed_work(&tegra->work, 50);
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
		else
			tegra->device_ready_for_reset = true;
#endif
	} else {
		tegra_ehci_restart(hcd);
#ifdef CONFIG_MACH_SAMSUNG_P4LTE
	if (tegra->phy->instance == 2) {
		usb_set_device_state(udev, USB_STATE_CONFIGURED);
	}
#endif
	}

#ifdef CONFIG_SAMSUNG_PHONE_SVNET
	if (tegra->phy->instance == 2)
		usb_root_hub_lost_power(hcd->self.root_hub);
#endif

	return 0;
}

static int tegra_ehci_reset(struct usb_hcd *hcd)
{
	unsigned long temp;
	int usec = 250*1000; /* see ehci_reset */

	temp = readl(hcd->regs + TEGRA_USB_USBCMD_REG_OFFSET);
	temp |= TEGRA_USB_USBCMD_RESET;
	writel(temp, hcd->regs + TEGRA_USB_USBCMD_REG_OFFSET);

	do {
		temp = readl(hcd->regs + TEGRA_USB_USBCMD_REG_OFFSET);
		if (!(temp & TEGRA_USB_USBCMD_RESET))
			break;
		udelay(1);
		usec--;
	} while (usec);

	if (!usec)
		return -ETIMEDOUT;

	/* Set to Host mode by setting bit 0-1 of USB device mode register */
	temp = readl(hcd->regs + TEGRA_USB_USBMODE_REG_OFFSET);
	writel((temp | TEGRA_USB_USBMODE_HOST),
			(hcd->regs + TEGRA_USB_USBMODE_REG_OFFSET));

	return 0;
}


#ifdef CONFIG_MACH_SAMSUNG_P4LTE
static int shutdown = 0;
#endif

static void tegra_ehci_shutdown(struct usb_hcd *hcd)
{
	struct tegra_ehci_hcd *tegra = dev_get_drvdata(hcd->self.controller);
	/* ehci_shutdown touches the USB controller registers, make sure
	 * controller has clocks to it */

#ifdef CONFIG_MACH_SAMSUNG_P4LTE
	printk(KERN_INFO "%s %d\n", __func__, __LINE__);

	if (tegra->phy->instance == 2)
		shutdown = 1;
#endif
    
	if (!tegra->host_resumed)
		tegra_ehci_power_up(hcd);

	/* call ehci shut down */
	ehci_shutdown(hcd);

#if 0	
	/* Turn Off Interrupts */
	ehci_writel(tegra->ehci, 0, &tegra->ehci->regs->intr_enable);
	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	usb_remove_hcd(hcd);
#ifdef CONFIG_SAMSUNG_LTE_MODEMCTL
	if (tegra->phy->instance == 2)
		mc_control_active_state(0); /*HSIC IPC control the ACTIVE_STATE*/
#endif    
	usb_put_hcd(hcd);
	cancel_delayed_work(&tegra->work);
	tegra_usb_phy_power_off(tegra->phy);         
	tegra_usb_phy_close(tegra->phy);
	iounmap(hcd->regs);

	tegra->host_resumed = 0;
	if (tegra->bus_is_power_down) {
		clk_disable(tegra->clk_min);
	}
	else {
		clk_disable(tegra->clk);
		clk_disable(tegra->sclk_clk);
		clk_disable(tegra->emc_clk);
	}

	clk_put(tegra->clk_min);
	clk_put(tegra->clk);
	clk_put(tegra->sclk_clk);
	clk_put(tegra->emc_clk);

	kfree(tegra);    

	printk(KERN_INFO "%s %d\n", __func__, __LINE__);

	/* we are ready to shut down, powerdown the phy */
//	tegra_ehci_power_down(hcd);
#else
	/* we are ready to shut down, powerdown the phy */
	tegra_ehci_power_down(hcd);
#endif
#ifdef CONFIG_MACH_SAMSUNG_P4LTE
	printk(KERN_ERR "%s %d\n", __func__, __LINE__);
#endif
}

static int tegra_ehci_setup(struct usb_hcd *hcd)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int retval;

	/* EHCI registers start at offset 0x100 */
	ehci->caps = hcd->regs + 0x100;
	ehci->regs = hcd->regs + 0x100 +
		HC_LENGTH(readl(&ehci->caps->hc_capbase));

	dbg_hcs_params(ehci, "reset");
	dbg_hcc_params(ehci, "reset");

	/* cache this readonly data; minimize chip reads */
	ehci->hcs_params = readl(&ehci->caps->hcs_params);

	retval = ehci_halt(ehci);
	if (retval)
		return retval;

	/* data structure init */
	retval = ehci_init(hcd);
	if (retval)
		return retval;

	hcd->has_tt = 1;
	ehci->sbrn = 0x20;

	ehci_reset(ehci);

	/*
	 * Resetting the controller has the side effect of resetting the PHY.
	 * So, never reset the controller after the calling
	 * tegra_ehci_reinit API.
	 */
	ehci->controller_resets_phy = 1;
	ehci->port_reset_no_wait = 1;

	ehci_port_power(ehci, 1);
	return retval;
}

#ifdef CONFIG_SAMSUNG_PHONE_SVNET
#define CP_PORT		 1  /* HSIC0 in S5PC210 */
#define RETRY_CNT_LIMIT 30  /* Max 300ms wait for cp resume*/

static void tegra_wait_for_cp_resume(struct usb_hcd *hcd)
{
	struct tegra_ehci_hcd *tegra = dev_get_drvdata(hcd->self.controller);
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	u32 __iomem	*portsc ;
	u32 val32, retry_cnt = 0;

	u32 re_wakeup = 0;

	if (tegra->phy->instance != 2)
		return;
	
	portsc = &ehci->regs->port_status[CP_PORT-1];

	mc_control_active_state(1); /* CP USB Power On */
	do {
		msleep(10);

		val32 = ehci_readl(ehci, portsc);

		if ((mc_is_host_wakeup()==0) && (val32 & PORT_CONNECT))
			break;
	} while (++retry_cnt < RETRY_CNT_LIMIT);

	printk(KERN_INFO "usb %s, retry_cnt=%d, portsc=0x%x, mc_is_host_wakeup()=%d\n", 
		__func__, retry_cnt, val32, mc_is_host_wakeup());
}

static int tegra_ehci_hcd_update_device(struct usb_hcd *hcd, struct usb_device *udev)
{
	struct ehci_hcd *ehci = hcd_to_ehci(hcd);
	int rc = 0;

	/* No working yet -jaewang- */
	return rc;

	if (!udev->parent) /* udev is root hub itself, impossible */
		rc = -1;
	/* we only support lpm device connected to root hub yet */
	if (ehci->has_lpm && !udev->parent->parent) {
		rc = ehci_lpm_set_da(ehci, udev->devnum, udev->portnum);
		if (!rc)
			rc = ehci_lpm_check(ehci, udev->portnum);
	}
	return rc;
}
#endif

#ifdef CONFIG_PM
static int tegra_ehci_bus_suspend(struct usb_hcd *hcd)
{
	int ret = 0;
	struct tegra_ehci_hcd *tegra = dev_get_drvdata(hcd->self.controller);

#ifdef CONFIG_MACH_SAMSUNG_P4LTE
	printk(KERN_INFO "%s %d\n", __func__, __LINE__);

	if (tegra->phy->instance == 2 && shutdown == 1) {
		printk(KERN_INFO "%s %d, return by shutdown\n", __func__, __LINE__);
		return ret;
	}
#endif

	if (0 != (ret = ehci_bus_suspend(hcd)))
		return ret;

	if (tegra->require_power_down_on_bus_suspend &&
	    tegra->hub_suspend_req) {
		tegra_usb_set_phy_clock(tegra->phy, 0);

		if (0 != (ret = clk_enable(tegra->clk_min))) {
			pr_err("HSIC USB core clk enable failed\n");
			return ret;
	}

		clk_disable(tegra->clk);

		clk_disable(tegra->emc_clk);

		tegra->hub_suspend_req = 0;
		tegra->bus_is_power_down = 1;
	}
	return ret;

}

static int tegra_ehci_bus_resume(struct usb_hcd *hcd)
{
	struct tegra_ehci_hcd *tegra = dev_get_drvdata(hcd->self.controller);
	int ret;
	
	if (tegra->require_power_down_on_bus_suspend &&
	    tegra->bus_is_power_down) {
		if (0 != (ret = clk_enable(tegra->clk)))
			pr_err("HSIC usb PHY clk enable failed\n");

		if (0 != (ret = clk_enable(tegra->emc_clk)))
			pr_err("HSIC USB EMC clk enable failed\n");

		clk_disable(tegra->clk_min);

		tegra_usb_set_phy_clock(tegra->phy, 1);

		msleep(50);

		tegra->bus_is_power_down = 0;
	}

	tegra_usb_phy_preresume(tegra->phy);
	tegra->port_resuming = 1;
	return ehci_bus_resume(hcd);
}
#endif

struct temp_buffer {
	void *kmalloc_ptr;
	void *old_xfer_buffer;
	u8 data[0];
};

static void free_temp_buffer(struct urb *urb)
{
	enum dma_data_direction dir;
	struct temp_buffer *temp;

	if (!(urb->transfer_flags & URB_ALIGNED_TEMP_BUFFER))
		return;

	dir = usb_urb_dir_in(urb) ? DMA_FROM_DEVICE : DMA_TO_DEVICE;

	temp = container_of(urb->transfer_buffer, struct temp_buffer,
								data);

	if (dir == DMA_FROM_DEVICE)
		memcpy(temp->old_xfer_buffer, temp->data,
			urb->transfer_buffer_length);
	urb->transfer_buffer = temp->old_xfer_buffer;
	kfree(temp->kmalloc_ptr);

	urb->transfer_flags &= ~URB_ALIGNED_TEMP_BUFFER;
}

static int alloc_temp_buffer(struct urb *urb, gfp_t mem_flags)
{
	enum dma_data_direction dir;
	struct temp_buffer *temp, *kmalloc_ptr;
	size_t kmalloc_size;

	if (urb->num_sgs || urb->sg ||
		urb->transfer_buffer_length == 0 ||
		!((uintptr_t)urb->transfer_buffer & (TEGRA_USB_DMA_ALIGN - 1)))
	return 0;

	dir = usb_urb_dir_in(urb) ? DMA_FROM_DEVICE : DMA_TO_DEVICE;

	/* Allocate a buffer with enough padding for alignment */
	kmalloc_size = urb->transfer_buffer_length +
		sizeof(struct temp_buffer) + TEGRA_USB_DMA_ALIGN - 1;

	kmalloc_ptr = kmalloc(kmalloc_size, mem_flags);
	if (!kmalloc_ptr)
		return -ENOMEM;

	/* Position our struct temp_buffer such that data is aligned */
	temp = PTR_ALIGN(kmalloc_ptr + 1, TEGRA_USB_DMA_ALIGN) - 1;

	temp->kmalloc_ptr = kmalloc_ptr;
	temp->old_xfer_buffer = urb->transfer_buffer;
	if (dir == DMA_TO_DEVICE)
		memcpy(temp->data, urb->transfer_buffer,
	urb->transfer_buffer_length);
	urb->transfer_buffer = temp->data;

	urb->transfer_flags |= URB_ALIGNED_TEMP_BUFFER;

	return 0;
}

static int tegra_ehci_map_urb_for_dma(struct usb_hcd *hcd, struct urb *urb,
					gfp_t mem_flags)
{
	int ret;

	ret = alloc_temp_buffer(urb, mem_flags);
	if (ret)
		return ret;

	ret = usb_hcd_map_urb_for_dma(hcd, urb, mem_flags);
	if (ret)
		free_temp_buffer(urb);

	return ret;
}

static void tegra_ehci_unmap_urb_for_dma(struct usb_hcd *hcd, struct urb *urb)
{
	usb_hcd_unmap_urb_for_dma(hcd, urb);
	free_temp_buffer(urb);
}

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
static int work_count = 0;
#endif
static void tegra_hsic_connection_work(struct work_struct *work)
{
	struct tegra_ehci_hcd *tegra =
		container_of(work, struct tegra_ehci_hcd, work.work);
	if (tegra_usb_phy_is_device_connected(tegra->phy)) {
		cancel_delayed_work(&tegra->work);
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
		tegra->device_ready_for_reset = true;	
		work_count = 0;
#endif		
		return;
	}
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	if (work_count++ > 40) {
		work_count = 0;
		return;
	}
#endif
	schedule_delayed_work(&tegra->work, msecs_to_jiffies(50));
	return;
}

#ifdef CONFIG_USB_EHCI_ONOFF_FEATURE
/* Stored ehci handle for hsic instance */
struct usb_hcd *ehci_handle;
int ehci_tegra_irq;

static ssize_t show_ehci_power(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return sprintf(buf, "EHCI Power %s\n", (ehci_handle) ? "on" : "off");
}

static ssize_t store_ehci_power(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int power_on;
	int retval;
	struct tegra_ehci_hcd *tegra = dev_get_drvdata(dev);
	struct usb_hcd *hcd = ehci_to_hcd(tegra->ehci);

	if (sscanf(buf, "%d", &power_on) != 1)
		return -EINVAL;

	if (unlikely(!device_trylock(dev)))
		return -EBUSY;

	if (power_on == 0 && ehci_handle != NULL) {
		usb_remove_hcd(hcd);
		tegra_ehci_power_down(hcd);
		ehci_handle = NULL;
	} else if (power_on == 1) {
		if (ehci_handle)
#ifdef CONFIG_MACH_SAMSUNG_P4LTE
			goto out;
#else
			usb_remove_hcd(hcd);
#endif
		ehci_power_up_by_sysfs = true;
		tegra_ehci_power_up(hcd);
		retval = usb_add_hcd(hcd, ehci_tegra_irq,
					IRQF_DISABLED | IRQF_SHARED);
		if (retval < 0) {
			printk(KERN_ERR "power_on error\n");
			count = retval;
			tegra_ehci_power_down(hcd);
			ehci_handle = NULL;
		} else
			ehci_handle = hcd;
	}
#ifdef CONFIG_MACH_SAMSUNG_P4LTE
out:
#endif
	device_unlock(dev);

	return count;
}

static DEVICE_ATTR(ehci_power, S_IRUSR | S_IRGRP | S_IWUSR | S_IWGRP,
					show_ehci_power, store_ehci_power);

static inline int create_ehci_sys_file(struct ehci_hcd *ehci)
{
	return device_create_file(ehci_to_hcd(ehci)->self.controller,
							&dev_attr_ehci_power);
}

static inline void remove_ehci_sys_file(struct ehci_hcd *ehci)
{
	device_remove_file(ehci_to_hcd(ehci)->self.controller,
						&dev_attr_ehci_power);
}

void tegra_ehci_txfilltuning(void)
{
	unsigned long val;
	struct usb_hcd *hcd = ehci_handle;

	if (hcd) {
		val = readl(hcd->regs + TEGRA_USB_TXFILLTUNING);
		if ((val & 0x00ff0000) != 0x00100000) {
			pr_debug(" ************ set TXFILLTUNING to 01\n");
			val = 0x00100000;
		}
		writel(val, (hcd->regs + TEGRA_USB_TXFILLTUNING));
	}

}

EXPORT_SYMBOL(tegra_ehci_txfilltuning);
#endif

static const struct hc_driver tegra_ehci_hc_driver = {
	.description		= hcd_name,
	.product_desc		= "Tegra EHCI Host Controller",
	.hcd_priv_size		= sizeof(struct ehci_hcd),

	.flags			= HCD_USB2 | HCD_MEMORY,

	.reset			= tegra_ehci_setup,
	.irq			= ehci_irq,

	.start			= ehci_run,
	.stop			= ehci_stop,
	.shutdown		= tegra_ehci_shutdown,
	.urb_enqueue		= ehci_urb_enqueue,
	.urb_dequeue		= ehci_urb_dequeue,
	.map_urb_for_dma	= tegra_ehci_map_urb_for_dma,
	.unmap_urb_for_dma	= tegra_ehci_unmap_urb_for_dma,
	.endpoint_disable	= ehci_endpoint_disable,
	.endpoint_reset		= ehci_endpoint_reset,
	.get_frame_number	= ehci_get_frame,
	.hub_status_data	= ehci_hub_status_data,
	.hub_control		= tegra_ehci_hub_control,
	.clear_tt_buffer_complete = ehci_clear_tt_buffer_complete,
#ifdef CONFIG_PM
	.bus_suspend		= tegra_ehci_bus_suspend,
	.bus_resume		= tegra_ehci_bus_resume,
#endif
	.relinquish_port	= ehci_relinquish_port,
	.port_handed_over	= ehci_port_handed_over,
#ifdef CONFIG_SAMSUNG_PHONE_SVNET
	.wait_for_device	= tegra_wait_for_cp_resume,
	.update_device		= tegra_ehci_hcd_update_device,
#endif
};

static int tegra_ehci_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct usb_hcd *hcd;
	struct ehci_hcd *ehci;
	struct tegra_ehci_hcd *tegra;
	struct tegra_ehci_platform_data *pdata;
	struct tegra_utmip_config *config;
	int err = 0;
	int irq;
	int instance = pdev->id;
	char *usb_name;
	char *usb_min_name;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "Platform data missing\n");
		return -EINVAL;
	}

	tegra = kzalloc(sizeof(struct tegra_ehci_hcd), GFP_KERNEL);
	if (!tegra)
		return -ENOMEM;

	hcd = usb_create_hcd(&tegra_ehci_hc_driver, &pdev->dev,
					dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Unable to create HCD\n");
		err = -ENOMEM;
		goto fail_hcd;
	}

	platform_set_drvdata(pdev, tegra);

	switch (instance) {
		case 0:
			usb_name = "usb1";
			usb_min_name = "usb1min";
			break;
		case 1:
			usb_name = "usb2";
			usb_min_name = "usb2min";
			break;
		case 2:
			usb_name = "usb3";
			usb_min_name = "usb3min";
			break;
	}

	tegra->clk = clk_get(&pdev->dev, usb_name);
	if (IS_ERR(tegra->clk)) {
		dev_err(&pdev->dev, "Can't get ehci clock\n");
		err = PTR_ERR(tegra->clk);
		goto fail_clk;
	}

	err = clk_enable(tegra->clk);
	if (err)
		goto fail_clken;

	tegra->clk_min = clk_get(&pdev->dev, usb_min_name);
	if (IS_ERR(tegra->clk_min)) {
		dev_err(&pdev->dev, "Can't get ehci clock fast\n");
		err = PTR_ERR(tegra->clk_min);
		goto fail_clk_min;
	}
	/* only need enable usbXmin at the USB suspend time to ensure the
	 * VDD core at least is 1.1v
	 */	 


	tegra->sclk_clk = clk_get(&pdev->dev, "sclk");
	if (IS_ERR(tegra->sclk_clk)) {
		dev_err(&pdev->dev, "Can't get sclk clock\n");
		err = PTR_ERR(tegra->sclk_clk);
		goto fail_sclk_clk;
	}
	if (instance == 0)
		clk_set_rate(tegra->sclk_clk, 240000000);
	else
		clk_set_rate(tegra->sclk_clk, 80000000);
		
	clk_enable(tegra->sclk_clk);

	tegra->emc_clk = clk_get(&pdev->dev, "emc");
	if (IS_ERR(tegra->emc_clk)) {
		dev_err(&pdev->dev, "Can't get emc clock\n");
		err = PTR_ERR(tegra->emc_clk);
		goto fail_emc_clk;
	}

	clk_enable(tegra->emc_clk);
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	clk_set_rate(tegra->emc_clk, 150000000);
#else
	clk_set_rate(tegra->emc_clk, 400000000);
#endif

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get I/O memory\n");
		err = -ENXIO;
		goto fail_io;
	}
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);
	hcd->regs = ioremap(res->start, resource_size(res));
	if (!hcd->regs) {
		dev_err(&pdev->dev, "Failed to remap I/O memory\n");
		err = -ENOMEM;
		goto fail_io;
	}

	config = pdata->phy_config;

	INIT_DELAYED_WORK(&tegra->work, tegra_hsic_connection_work);

	tegra->phy = tegra_usb_phy_open(instance, hcd->regs, config,
						TEGRA_USB_PHY_MODE_HOST);
	if (IS_ERR(tegra->phy)) {
		dev_err(&pdev->dev, "Failed to open USB phy\n");
		err = -ENXIO;
		goto fail_phy;
	}

	err = tegra_ehci_reset(hcd);
	if (err) {
		dev_err(&pdev->dev, "Failed to reset controller\n");
		goto fail;
	}

	tegra_usb_phy_power_on(tegra->phy);
	tegra->host_resumed = 1;
	tegra->require_power_down_on_bus_suspend = 
		pdata->power_down_on_bus_suspend;

	irq = platform_get_irq(pdev, 0);
	if (!irq) {
		dev_err(&pdev->dev, "Failed to get IRQ\n");
		err = -ENODEV;
		goto fail;
	}

	set_irq_flags(irq, IRQF_VALID);

	ehci = hcd_to_ehci(hcd);
	tegra->ehci = ehci;
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	tegra->device_ready_for_reset = false;
#endif

#ifdef CONFIG_USB_EHCI_ONOFF_FEATURE
	if (instance == 1) {
		ehci_tegra_irq = irq;
#ifdef CONFIG_MACH_SAMSUNG_P4LTE
	}
	else if(instance == 2)
	{
                ehci_tegra_irq = irq;
#endif
		create_ehci_sys_file(ehci);
	}
#endif
#ifdef CONFIG_USB_OTG_UTILS
	if (pdata->operating_mode == TEGRA_USB_OTG) {
		tegra->transceiver = otg_get_transceiver();
		if (tegra->transceiver)
			otg_set_host(tegra->transceiver, &hcd->self);
	}
#endif

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
#ifdef CONFIG_USB_HOST_NOTIFY
	if (pdata->host_notify) {
		hcd->host_notify = pdata->host_notify;
		hcd->ndev.name = dev_name(&pdev->dev);
		err = host_notify_dev_register(&hcd->ndev);
		if (err) {
			dev_err(&pdev->dev, "Failed to host_notify_dev_register\n");
			goto fail;
		}
	}
#endif
#ifdef CONFIG_USB_SEC_WHITELIST
	if (pdata->sec_whlist_table_num)
		hcd->sec_whlist_table_num = pdata->sec_whlist_table_num;
#endif
#endif

	err = usb_add_hcd(hcd, irq, IRQF_DISABLED | IRQF_SHARED);
	if (err != 0) {
		dev_err(&pdev->dev, "Failed to add USB HCD\n");
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
		goto fail_notify;
#else
		goto fail;
#endif
	}

#ifdef CONFIG_USB_EHCI_ONOFF_FEATURE
	if (instance == 1)
		ehci_handle = hcd;
#ifdef CONFIG_MACH_SAMSUNG_P4LTE
	if (instance == 2)
		ehci_handle = hcd;
#endif
#endif
/* Don't merge with P3
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	if (pdata->currentlimit_irq) {
		tegra->currentlimit_irq = pdata->currentlimit_irq;
		err = request_threaded_irq(tegra->currentlimit_irq,
					tegra_currentlimit_irq,
					tegra_currentlimit_irq_thread,
					IRQF_TRIGGER_FALLING,
					dev_name(&pdev->dev),
					tegra);
		if (err) {
			dev_err(&pdev->dev, "Failed to register IRQ\n");
			goto fail;
		}
		disable_irq(tegra->currentlimit_irq);
	}
#endif
*/
#if 0
#ifdef CONFIG_MACH_SAMSUNG_P4LTE
#ifdef CONFIG_USB_EHCI_ONOFF_FEATURE
	if (instance == 2) {
		usb_remove_hcd(hcd);
		tegra_ehci_power_down(hcd);
		ehci_handle = NULL;
	}	
#endif
#endif
#endif
	return err;

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
fail_notify:
#ifdef CONFIG_USB_HOST_NOTIFY
	host_notify_dev_unregister(&hcd->ndev);
#endif
#endif
fail:
#ifdef CONFIG_USB_OTG_UTILS
	if (tegra->transceiver) {
		otg_set_host(tegra->transceiver, NULL);
		otg_put_transceiver(tegra->transceiver);
	}
#endif
	tegra_usb_phy_close(tegra->phy);
fail_phy:
	iounmap(hcd->regs);
fail_io:
	clk_disable(tegra->emc_clk);
	clk_put(tegra->emc_clk);
fail_emc_clk:
	clk_disable(tegra->sclk_clk);
	clk_put(tegra->sclk_clk);
fail_sclk_clk:
	clk_disable(tegra->clk);
fail_clken:
	clk_put(tegra->clk_min);
fail_clk_min:	
	clk_put(tegra->clk);
fail_clk:
	usb_put_hcd(hcd);
fail_hcd:
	kfree(tegra);
	return err;
}

#ifdef CONFIG_PM
static int tegra_ehci_resume(struct platform_device *pdev)
{
	struct tegra_ehci_hcd *tegra = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = ehci_to_hcd(tegra->ehci);

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	if (tegra->phy->instance == 0)
		return 0;
#endif
#ifdef CONFIG_SAMSUNG_LTE_MODEMCTL
	if (tegra->phy->instance == 2)
	  if (mc_is_modem_on() && !mc_is_host_wakeup()) {
		  mc_control_slave_wakeup(1);
		  msleep(10);
	  }
#endif

	return tegra_usb_resume(hcd);
}

static int tegra_ehci_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct tegra_ehci_hcd *tegra = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = ehci_to_hcd(tegra->ehci);

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	if (tegra->phy->instance == 0)
		return 0;
#endif
	if (time_before(jiffies, tegra->ehci->next_statechange))
		msleep(10);

	return tegra_usb_suspend(hcd);
}
#endif

static int tegra_ehci_remove(struct platform_device *pdev)
{
	struct tegra_ehci_hcd *tegra = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = ehci_to_hcd(tegra->ehci);

	if (tegra == NULL || hcd == NULL)
		return -EINVAL;

#ifdef CONFIG_USB_OTG_UTILS
	if (tegra->transceiver) {
		otg_set_host(tegra->transceiver, NULL);
		otg_put_transceiver(tegra->transceiver);
	}
#endif

#ifdef CONFIG_USB_EHCI_ONOFF_FEATURE
	if (tegra->phy->instance == 1) {
		remove_ehci_sys_file(hcd_to_ehci(hcd));
		ehci_handle = NULL;
	}
#ifdef CONFIG_MACH_SAMSUNG_P4LTE
	else if( tegra->phy->instance == 2 )
	{
		remove_ehci_sys_file(hcd_to_ehci(hcd));
		ehci_handle = NULL;
	}
#endif
#endif

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
/* Don't merge with P3
	if (tegra->currentlimit_irq)
		free_irq(tegra->currentlimit_irq, tegra);
*/
#ifdef CONFIG_USB_HOST_NOTIFY
	host_notify_dev_unregister(&hcd->ndev);
#endif
#endif

	/* Turn Off Interrupts */
	ehci_writel(tegra->ehci, 0, &tegra->ehci->regs->intr_enable);
	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	usb_remove_hcd(hcd);
#ifdef CONFIG_SAMSUNG_LTE_MODEMCTL
	if (tegra->phy->instance == 2)
		mc_control_active_state(0); /*HSIC IPC control the ACTIVE_STATE*/
#endif    
	usb_put_hcd(hcd);
	cancel_delayed_work(&tegra->work);
	tegra_usb_phy_power_off(tegra->phy);
	tegra_usb_phy_close(tegra->phy);
	iounmap(hcd->regs);

	clk_disable(tegra->clk);
	clk_put(tegra->clk);

	clk_disable(tegra->sclk_clk);
	clk_put(tegra->sclk_clk);

	clk_disable(tegra->emc_clk);
	clk_put(tegra->emc_clk);

	kfree(tegra);
	return 0;
}

static void tegra_ehci_hcd_shutdown(struct platform_device *pdev)
{
	struct tegra_ehci_hcd *tegra = platform_get_drvdata(pdev);
	struct usb_hcd *hcd = ehci_to_hcd(tegra->ehci);

	if (hcd->driver->shutdown)
		hcd->driver->shutdown(hcd);
}

static struct platform_driver tegra_ehci_driver = {
	.probe		= tegra_ehci_probe,
	.remove		= tegra_ehci_remove,
#ifdef CONFIG_PM
	.suspend	= tegra_ehci_suspend,
	.resume		= tegra_ehci_resume,
#endif
	.shutdown	= tegra_ehci_hcd_shutdown,
	.driver		= {
		.name	= "tegra-ehci",
	}
};
