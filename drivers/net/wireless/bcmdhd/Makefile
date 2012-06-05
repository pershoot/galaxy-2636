# bcmdhd
DHDCFLAGS = -Wall -Wstrict-prototypes -Dlinux -DBCMDRIVER                     \
	-DBCMDONGLEHOST -DUNRELEASEDCHIP -DBCMDMA32 -DWLBTAMP -DBCMFILEIMAGE  \
	-DDHDTHREAD -DDHD_GPL -DDHD_SCHED -DDHD_DEBUG -DSDTEST -DBDC -DTOE    \
	-DDHD_BCMEVENTS -DSHOW_EVENTS -DDONGLEOVERLAYS -DBCMDBG               \
	-DMMC_SDIO_ABORT -DBCMSDIO -DBCMLXSDMMC -DBCMPLATFORM_BUS -DWLP2P     \
	-DNEW_COMPAT_WIRELESS -DWIFI_ACT_FRAME -DARP_OFFLOAD_SUPPORT          \
	-DKEEP_ALIVE -DCSCAN -DPKT_FILTER_SUPPORT -DEXAMPLE_TABLE             \
	-DEMBEDDED_PLATFORM -DPNO_SUPPORT -DSET_RANDOM_MAC_SOFTAP             \
	-DDHD_USE_EARLYSUSPEND     \
	-Idrivers/net/wireless/bcmdhd -Idrivers/net/wireless/bcmdhd/include

DHDOFILES = aiutils.o bcmsdh_sdmmc_linux.o dhd_linux.o siutils.o bcmutils.o   \
	dhd_linux_sched.o bcmwifi.o dhd_sdio.o bcmevent.o dhd_bta.o hndpmu.o  \
	bcmsdh.o dhd_cdc.o bcmsdh_linux.o dhd_common.o linux_osl.o            \
	bcmsdh_sdmmc.o dhd_custom_gpio.o sbutils.o wldev_common.o wl_android.o dhd_cfg80211.o

ifneq ($(CONFIG_MACH_SAMSUNG_VARIATION_TEGRA),y)
DHDCFLAGS += -DCUSTOM_OOB_GPIO_NUM=2 -DOOB_INTR_ONLY -DHW_OOB -DGET_CUSTOM_MAC_ENABLE
endif

ifeq ($(CONFIG_MACH_SAMSUNG_VARIATION_TEGRA),y)
bcmdhd-objs += wlan_gpio.o dhd_custom_sec.o
DHDCFLAGS += -DRDWR_MACADDR -DCUSTOMER_HW_SAMSUNG -DPOWER_ON_DELAY_4330 -DCONFIG_WIFI_CONTROL_FUNC
else
DHDCFLAGS += -DCUSTOMER_HW2 -DENABLE_INSMOD_NO_FW_LOAD
endif

obj-$(CONFIG_BCMDHD) += bcmdhd.o
bcmdhd-objs += $(DHDOFILES)
ifneq ($(CONFIG_WIRELESS_EXT),)
bcmdhd-objs += wl_iw.o
DHDCFLAGS += -DSOFTAP -DWL_WIRELESS_EXT
endif
ifneq ($(CONFIG_CFG80211),)
bcmdhd-objs += wl_cfg80211.o wl_cfgp2p.o wl_linux_mon.o
DHDCFLAGS += -DWL_CFG80211
endif
ifneq ($(CONFIG_DHD_USE_SCHED_SCAN),)
DHDCFLAGS += -DWL_SCHED_SCAN
endif
ifneq ($(CONFIG_DHD_ENABLE_P2P),)
DHDCFLAGS += -DWL_ENABLE_P2P_IF
endif
EXTRA_CFLAGS = $(DHDCFLAGS)
ifeq ($(CONFIG_BCMDHD),m)
EXTRA_LDFLAGS += --strip-debug
endif
