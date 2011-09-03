/* dpraminit.c
 *
 * SAMSUNG TTY modem driver for cdma dpram
 *
 * Copyright (C) 2010 Samsung Electronics. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <asm/io.h>
#include <mach/io.h>
#include <mach/iomap.h>
#include <linux/delay.h>

#define RST_DEVICES_H 0x8
#define SWR_SNOR_RST (0x1 << 10)
#define CLK_OUT_ENB_H 0x14
#define CLK_ENB_SNOR (0x1 << 10)

#define TRISTATE_REG_A 0x14
#define TRISTATE_REG_B 0x18
#define Z_ATC (0x1 << 2)
#define Z_ATD (0x1 << 3)
#define Z_ATE (0x1 << 25)

#define PIN_MUX_CTL_A 0x80
#define ATC_SEL (0x2 << 22)
#define ATD_SEL (0x2 << 20)
#define ATE_SEL (0x2 << 12)

#define SNOR_CONFIG 0
#define SNOR_TIMING0 0x10
#define SNOR_TIMING1 0x14
#define SNOR_SEL_CS4 (0x4 << 4)
#define RDY_POLARITY_HIGH (0x1 << 23)
#define MUXMODE_GMI_ADMUX (0x1 << 28)
#define ADV_WIDTH (0x3 << 4)
#define CE_WIDTH (0x0 << 0)
#define HOLD_WIDTH (0x2 << 8)
#define WE_WIDTH (0x3 << 16)
#define OE_WIDTH (0x3 << 8)

#define TEGRA_DPRAM_BASE 0xD0000000 

static void __iomem *car_base = IO_ADDRESS(TEGRA_CLK_RESET_BASE);
static void __iomem *snor_reg_base = IO_ADDRESS(TEGRA_SNOR_BASE);
static void __iomem *apbmisc_base = IO_ADDRESS(TEGRA_APB_MISC_BASE);
static void __iomem *dpram = IO_ADDRESS(TEGRA_DPRAM_BASE);
static void __iomem *gpio_reg1 = IO_ADDRESS(0x6000D088);
static void __iomem *gpio_reg2 = IO_ADDRESS(0x6000D08c);

static void tegra_snor_clock_init()
{
    u32 reg = 0;
    u32  addr = 0;

    addr = car_base + CLK_OUT_ENB_H;
    reg = readl(addr);
    if(reg & CLK_ENB_SNOR)
    {
      writel((reg & ~CLK_ENB_SNOR), addr);
    }
    msleep(1);

    addr = car_base + RST_DEVICES_H;
    reg = readl(addr);
    writel((reg|SWR_SNOR_RST), addr);
    msleep(1);

    addr = car_base + CLK_OUT_ENB_H;
    reg = readl(addr);
    writel((reg|CLK_ENB_SNOR), addr);
    msleep(1);

    addr = car_base + RST_DEVICES_H;
    reg = readl(addr);
    writel((reg & ~SWR_SNOR_RST), addr);
    msleep(1);

    reg = readl(car_base + CLK_OUT_ENB_H);

    reg = readl(car_base + RST_DEVICES_H);
}

static void tegra_snor_pinmux_init()
{
    u32 reg, addr;

    addr = apbmisc_base + PIN_MUX_CTL_A;
    reg = readl(addr);
    reg |= (ATC_SEL | ATD_SEL | ATE_SEL);
    writel(reg, addr);

    addr = apbmisc_base + TRISTATE_REG_A;
    reg = readl(addr);
    reg &= ~Z_ATC;
    reg &= ~Z_ATD;
    writel(reg, addr);

    addr = apbmisc_base + TRISTATE_REG_B;
    reg = readl(addr);
    reg &= ~Z_ATE;
    writel(reg, addr);
}

void tegra_init_snor()
{
    u32 reg, addr;

    reg = readl(gpio_reg1);
    writel(0x00000000, gpio_reg1);
    writel(0x00000000, gpio_reg2);

    tegra_snor_clock_init();
    tegra_snor_pinmux_init();

    addr = snor_reg_base + SNOR_CONFIG;
    reg = readl(addr);
    writel(reg | (MUXMODE_GMI_ADMUX | RDY_POLARITY_HIGH | SNOR_SEL_CS4), addr);

    // 26Mhz Oscillator -> ~38nS.
    addr = snor_reg_base + SNOR_TIMING0;
    reg = readl(addr);
    writel(reg | (HOLD_WIDTH | ADV_WIDTH | CE_WIDTH), addr);

    addr = snor_reg_base + SNOR_TIMING1;
    reg = readl(addr);
    writel(reg | (WE_WIDTH | OE_WIDTH), addr);
}
