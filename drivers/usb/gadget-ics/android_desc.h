 #ifdef CONFIG_USB_ANDROID_SAMSUNG_COMPOSITE
/* soonyong.cho : Define samsung product id and config string.
 *                Sources such as 'android.c' and 'devs.c' refered below define
 */
#  define SAMSUNG_VENDOR_ID				0x04e8

#  ifdef CONFIG_USB_ANDROID_SAMSUNG_ESCAPE
	/* USE DEVGURU HOST DRIVER */
	/* 0x6860 : MTP(0) + MS Composite (UMS) */
	/* 0x685E : UMS(0) + MS Composite (ADB) */
#  ifdef CONFIG_USB_ANDROID_SAMSUNG_KIES_UMS
#    define SAMSUNG_KIES_PRODUCT_ID			0x685e	/* ums(0) + acm(1,2) */
#  else
#    define SAMSUNG_KIES_PRODUCT_ID			0x6860	/* mtp(0) + acm(1,2) */
#    define SAMSUNG_P4LTE_KIES_PRODUCT_ID		0x6860	/* mtp(0) + diag */
#  endif
#    define SAMSUNG_DEBUG_PRODUCT_ID	0x6860	/* mtp(0) + acm(1,2) + adb(3) (with MS Composite) */
#    define SAMSUNG_P4LTE_DEBUG_PRODUCT_ID		0x6860 /* mtp(0) + diag(1,2) + adb(3) (with MS Composite) */
#    define SAMSUNG_UMS_PRODUCT_ID			0x685B  /* UMS Only */
#    define SAMSUNG_MTP_PRODUCT_ID			0x685C  /* MTP Only */
#    ifdef CONFIG_USB_ANDROID_SAMSUNG_RNDIS_WITH_MS_COMPOSITE
#      define SAMSUNG_RNDIS_PRODUCT_ID			0x6861  /* RNDIS(0,1) + UMS (2) + MS Composite */
#    else
#      define SAMSUNG_RNDIS_PRODUCT_ID			0x6863  /* RNDIS only */
#      define SAMSUNG_P4LTE_RNDIS_DIAG_PRODUCT_ID	0x6862  /* RNDIS + DIAG */
#    endif
#    define ANDROID_DEBUG_CONFIG_STRING	 "MTP + ACM + ADB (Debugging mode)"
#    define ANDROID_P4LTE_DEBUG_CONFIG_STRING	 	"MTP + DIAG + ADB (Debugging mode)"
#  ifdef CONFIG_USB_ANDROID_SAMSUNG_KIES_UMS
#    define ANDROID_KIES_CONFIG_STRING			"UMS + ACM (SAMSUNG KIES mode)"
#  else
#    define ANDROID_KIES_CONFIG_STRING			"MTP + ACM (SAMSUNG KIES mode)"
#    define ANDROID_P4LTE_KIES_CONFIG_STRING		"MTP + DIAG"
#  endif
#  else /* USE MCCI HOST DRIVER */
#    define SAMSUNG_KIES_PRODUCT_ID			0x6877	/* Shrewbury ACM+MTP*/
#    define SAMSUNG_DEBUG_PRODUCT_ID			0x681C	/* Shrewbury ACM+UMS+ADB*/
#    define SAMSUNG_UMS_PRODUCT_ID			0x681D
#    define SAMSUNG_MTP_PRODUCT_ID			0x68A9
#    define SAMSUNG_RNDIS_PRODUCT_ID			0x6863
#    define ANDROID_DEBUG_CONFIG_STRING			"ACM + UMS + ADB (Debugging mode)"
#    define ANDROID_KIES_CONFIG_STRING			"ACM + MTP (SAMSUNG KIES mode)"
#  endif
#  define ANDROID_UMS_CONFIG_STRING			"UMS Only (Not debugging mode)"
#  define ANDROID_MTP_CONFIG_STRING			"MTP Only (Not debugging mode)"
#  ifdef CONFIG_USB_ANDROID_SAMSUNG_RNDIS_WITH_MS_COMPOSITE
#    define ANDROID_RNDIS_CONFIG_STRING			"RNDIS + UMS (Not debugging mode)"
#  else
#    define ANDROID_RNDIS_CONFIG_STRING	 		"RNDIS Only (Not debugging mode)"
#    define ANDROID_P4LTE_RNDIS_DIAG_CONFIG_STRING	"RNDIS + DIAG (Not debugging mode)"
#  endif
	/* Refered from S1, P1 */
#  define USBSTATUS_UMS					0x0
#  define USBSTATUS_SAMSUNG_KIES 			0x1
#  define USBSTATUS_MTPONLY				0x2
#  define USBSTATUS_ASKON				0x4
#  define USBSTATUS_VTP					0x8
#  define USBSTATUS_ADB					0x10
#  define USBSTATUS_DM					0x20
#  define USBSTATUS_ACM					0x40
#  define USBSTATUS_SAMSUNG_KIES_REAL			0x80
#ifdef CONFIG_USB_ANDROID_ACCESSORY
#  define USBSTATUS_ACCESSORY			0x100		
#endif
#endif