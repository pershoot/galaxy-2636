obj-y                                   += common.o
obj-y                                   += apbio.o
obj-y                                   += io.o
obj-y                                   += irq.o legacy_irq.o
obj-y                                   += syncpt.o
obj-y                                   += clock.o
obj-y                                   += dvfs.o
obj-y                                   += timer.o
obj-y                                   += pinmux.o
obj-y                                   += devices.o
obj-y                                   += delay.o
obj-y                                   += suspend.o
ifneq ($(CONFIG_ICS),y)
obj-y                                   += powergate.o
obj-y                                   += fuse.o
else
obj-y                                   += powergate-ics.o
obj-y                                   += fuse-ics.o
endif
obj-y                                   += kfuse.o
ifeq ($(CONFIG_ICS),y)
obj-y                                   += csi.o
endif
obj-y                                   += i2c_error_recovery.o
ifeq ($(CONFIG_TEGRA_ALSA),y)
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += tegra2_i2s.o
else
obj-y                                   += tegra_i2s_audio.o
endif
obj-y                                   += tegra_spdif_audio.o
obj-y                                   += tegra_das.o
obj-y                                   += mc.o
obj-$(CONFIG_TEGRA_STAT_MON)			+= tegra2_statmon.o
obj-$(CONFIG_USB_SUPPORT)               += usb_phy.o
obj-$(CONFIG_FIQ)                       += fiq.o
obj-$(CONFIG_TEGRA_FIQ_DEBUGGER)        += tegra_fiq_debugger.o
obj-$(CONFIG_TEGRA_PWM)                 += pwm.o
obj-$(CONFIG_TEGRA_ARB_SEMAPHORE)	+= arb_sema.o

obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += clock.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += tegra2_clocks.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += tegra2_dvfs.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += tegra2_fuse.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += tegra2_speedo.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += suspend-t2.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)		+= tegra2_save.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)		+= tegra2_emc.o
obj-$(CONFIG_CPU_V7)			+= cortex-a9.o

obj-$(CONFIG_SMP)                       += localtimer.o
obj-$(CONFIG_SMP)                       += platsmp.o
obj-y                                   += headsmp.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += headsmp-t2.o
obj-$(CONFIG_TEGRA_SYSTEM_DMA)          += dma.o
obj-$(CONFIG_CPU_FREQ)                  += cpu-tegra.o
obj-$(CONFIG_CPU_IDLE)                  += cpuidle.o
ifneq ($(CONFIG_ICS),y)
obj-$(CONFIG_TEGRA_IOVMM)               += iovmm.o
else
obj-$(CONFIG_TEGRA_IOVMM)               += iovmm-ics.o
endif
obj-$(CONFIG_TEGRA_IOVMM_GART)          += iovmm-gart.o
obj-$(CONFIG_TEGRA_MC_PROFILE)          += tegra2_mc.o

obj-y                                   += nv/

obj-${CONFIG_TEGRA_SPI_SLAVE}		+= spi_tegra_slave.o

obj-${CONFIG_MACH_HARMONY}              += board-harmony.o
obj-${CONFIG_MACH_HARMONY}              += board-harmony-pinmux.o
obj-${CONFIG_MACH_HARMONY}              += board-harmony-panel.o
obj-${CONFIG_MACH_HARMONY}              += board-harmony-sdhci.o

obj-${CONFIG_MACH_VENTANA}              += board-ventana.o
obj-${CONFIG_MACH_VENTANA}              += board-ventana-pinmux.o
obj-${CONFIG_MACH_VENTANA}              += board-ventana-sdhci.o
obj-${CONFIG_MACH_VENTANA}              += board-ventana-power.o
obj-${CONFIG_MACH_VENTANA}              += board-ventana-panel.o
obj-${CONFIG_MACH_VENTANA}              += board-ventana-jack.o
obj-${CONFIG_MACH_VENTANA}              += board-ventana-sensors.o
obj-${CONFIG_MACH_VENTANA}              += board-ventana-kbc.o
obj-${CONFIG_MACH_VENTANA}              += board-ventana-memory.o

obj-${CONFIG_MACH_WHISTLER}             += board-whistler.o
obj-${CONFIG_MACH_WHISTLER}             += board-whistler-pinmux.o
obj-${CONFIG_MACH_WHISTLER}             += board-whistler-sdhci.o
obj-${CONFIG_MACH_WHISTLER}             += board-whistler-power.o
obj-${CONFIG_MACH_WHISTLER}             += board-whistler-panel.o
obj-${CONFIG_MACH_WHISTLER}             += board-whistler-sensors.o
obj-${CONFIG_MACH_WHISTLER}             += board-whistler-kbc.o
obj-${CONFIG_MACH_WHISTLER}             += board-whistler-baseband.o
obj-${CONFIG_MACH_WHISTLER}             += board-whistler-memory.o

obj-${CONFIG_MACH_SAMSUNG_P3}           += board-p3.o
obj-${CONFIG_MACH_SAMSUNG_P3}           += board-p3-pinmux.o
obj-${CONFIG_MACH_SAMSUNG_P3}           += board-p3-sdhci.o
obj-${CONFIG_MACH_SAMSUNG_P3}           += board-p3-power.o
obj-${CONFIG_MACH_SAMSUNG_P3}           += board-p3-panel.o
obj-${CONFIG_MACH_SAMSUNG_P3}           += board-p3-gpioi2c.o
obj-${CONFIG_MACH_SAMSUNG_P3}           += board-p3-sensors.o
obj-${CONFIG_MACH_SAMSUNG_P3}           += board-p3-memory.o
obj-${CONFIG_MACH_SAMSUNG_P3}           += gpio.o
obj-${CONFIG_MACH_SAMSUNG_P3}           += board-p3-gpio.o
obj-$(CONFIG_MACH_SAMSUNG_P3)		+= pinmux-t2-tables.o

obj-${CONFIG_MACH_SAMSUNG_P4}           += board-p4.o
obj-${CONFIG_MACH_SAMSUNG_P4}           += board-p4-pinmux.o
obj-${CONFIG_MACH_SAMSUNG_P4}           += board-p4-sdhci.o
obj-${CONFIG_MACH_SAMSUNG_P4}           += board-p4-power.o
obj-${CONFIG_MACH_SAMSUNG_P4}           += board-p4-panel.o
obj-${CONFIG_MACH_SAMSUNG_P4}           += board-p4-gpioi2c.o
obj-${CONFIG_MACH_SAMSUNG_P4}           += board-p4-sensors.o
obj-${CONFIG_MACH_SAMSUNG_P4}           += board-p4-memory.o
obj-${CONFIG_MACH_SAMSUNG_P4}           += gpio.o
obj-${CONFIG_MACH_SAMSUNG_P4}           += board-p4-gpio.o
obj-$(CONFIG_MACH_SAMSUNG_P4)		+= pinmux-t2-tables.o

obj-${CONFIG_MACH_SAMSUNG_P4WIFI}           += board-p4wifi.o
obj-${CONFIG_MACH_SAMSUNG_P4WIFI}           += board-p4wifi-pinmux.o
obj-${CONFIG_MACH_SAMSUNG_P4WIFI}           += board-p4wifi-sdhci.o
obj-${CONFIG_MACH_SAMSUNG_P4WIFI}           += board-p4wifi-power.o
obj-${CONFIG_MACH_SAMSUNG_P4WIFI}           += board-p4wifi-panel.o
obj-${CONFIG_MACH_SAMSUNG_P4WIFI}           += board-p4wifi-gpioi2c.o
obj-${CONFIG_MACH_SAMSUNG_P4WIFI}           += board-p4wifi-sensors.o
obj-${CONFIG_MACH_SAMSUNG_P4WIFI}           += board-p4wifi-memory.o
obj-${CONFIG_MACH_SAMSUNG_P4WIFI}           += gpio.o
obj-${CONFIG_MACH_SAMSUNG_P4WIFI}           += board-p4-gpio.o
obj-$(CONFIG_MACH_SAMSUNG_P4WIFI)	    += pinmux-t2-tables.o

obj-${CONFIG_MACH_SAMSUNG_P4LTE}           += board-p4lte.o
obj-${CONFIG_MACH_SAMSUNG_P4LTE}           += board-p4lte-pinmux.o
obj-${CONFIG_MACH_SAMSUNG_P4LTE}           += board-p4lte-sdhci.o
obj-${CONFIG_MACH_SAMSUNG_P4LTE}           += board-p4lte-power.o
obj-${CONFIG_MACH_SAMSUNG_P4LTE}           += board-p4lte-panel.o
obj-${CONFIG_MACH_SAMSUNG_P4LTE}           += board-p4lte-gpioi2c.o
obj-${CONFIG_MACH_SAMSUNG_P4LTE}           += board-p4lte-sensors.o
obj-${CONFIG_MACH_SAMSUNG_P4LTE}           += board-p4lte-memory.o
obj-${CONFIG_MACH_SAMSUNG_P4LTE}           += gpio.o
obj-${CONFIG_MACH_SAMSUNG_P4LTE}           += board-p4lte-gpio.o
obj-${CONFIG_MACH_SAMSUNG_P4LTE}           += pinmux-t2-tables.o


obj-${CONFIG_MACH_SAMSUNG_P5}           += board-p5.o
obj-${CONFIG_MACH_SAMSUNG_P5}           += board-p5-pinmux.o
obj-${CONFIG_MACH_SAMSUNG_P5}           += board-p5-sdhci.o
obj-${CONFIG_MACH_SAMSUNG_P5}           += board-p5-power.o
obj-${CONFIG_MACH_SAMSUNG_P5}           += board-p5-panel.o
obj-${CONFIG_MACH_SAMSUNG_P5}           += board-p5-gpioi2c.o
obj-${CONFIG_MACH_SAMSUNG_P5}           += board-p5-sensors.o
obj-${CONFIG_MACH_SAMSUNG_P5}           += board-p5-memory.o
obj-${CONFIG_MACH_SAMSUNG_P5}           += p5-pinmux-t2-tables.o
obj-${CONFIG_MACH_SAMSUNG_P5}		+= p5-gpio.o

obj-${CONFIG_MACH_SAMSUNG_VARIATION_TEGRA}           += board-sec-btlpm.o
obj-${CONFIG_MACH_SAMSUNG_VARIATION_TEGRA}           += board-sec-watchdog.o
obj-${CONFIG_VIBTONZ}           += isa1200_vibrator.o
obj-$(CONFIG_SEC_MODEM)					+= sec_smd_plat.o
obj-$(CONFIG_SAMSUNG_LTE_MODEMCTL)      += p3lte-modem.o

ifeq ($(CONFIG_TARGET_LOCALE_KOR),y)
EXTRA_CFLAGS =-DMODELNAME=\"$(SEC_PRODUCTNAME)\"
else
endif
