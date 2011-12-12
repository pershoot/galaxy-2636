#ifndef CMC623_REG_HEADER
#define CMC623_REG_HEADER

#define CMC623_REG_SELBANK   0x00

/* A stage configuration */
#define CMC623_REG_DNRHDTROVE 0x01
#define CMC623_REG_DITHEROFF 0x06
#define CMC623_REG_CLKCONT 0x10
#define CMC623_REG_CLKGATINGOFF 0x0a
#define CMC623_REG_INPUTIFCON 0x24
#define CMC623_REG_CLKMONCONT   0x11
#define CMC623_REG_HDRTCEOFF 0x3a
#define CMC623_REG_I2C 0x0d
#define CMC623_REG_BSTAGE 0x0e
#define CMC623_REG_CABCCTRL 0x7c
#define CMC623_REG_PWMCTRL 0xb4
#define CMC623_REG_OVEMAX 0x54

/* A stage image size */
#define CMC623_REG_1280 0x22
#define CMC623_REG_800 0x23

/* B stage image size */
#define CMC623_REG_SCALERINPH 0x09
#define CMC623_REG_SCALERINPV 0x0a
#define CMC623_REG_SCALEROUTH 0x0b
#define CMC623_REG_SCALEROUTV 0x0c

/* EDRAM configuration */
#define CMC623_REG_EDRBFOUT40 0x01
#define CMC623_REG_EDRAUTOREF 0x06
#define CMC623_REG_EDRACPARAMTIM 0x07

/* Vsync Calibartion */
#define CMC623_REG_CALVAL10 0x65

/* tcon output polarity */
#define CMC623_REG_TCONOUTPOL 0x68

/* tcon RGB configuration */
#define CMC623_REG_TCONRGB1 0x6c
#define CMC623_REG_TCONRGB2 0x6d
#define CMC623_REG_TCONRGB3 0x6e

/* Reg update */
#define CMC623_REG_REGMASK 0x28
#define CMC623_REG_SWRESET 0x09
#define CMC623_REG_RGBIFEN 0x26

struct Cmc623RegisterSet{
	unsigned int RegAddr;
	unsigned int Data;
};

enum eLcd_mDNIe_UI{
	mDNIe_UI_MODE,
	mDNIe_VIDEO_MODE,
	mDNIe_VIDEO_WARM_MODE,
	mDNIe_VIDEO_COLD_MODE,
	mDNIe_CAMERA_MODE,
	mDNIe_NAVI,
	mDNIe_GALLERY_MODE,
//#ifdef FEATURE_ANRD_KOR	
	mDNIe_DMB_MODE,
	mDNIe_DMB_WARM_MODE,
	mDNIe_DMB_COLD_MODE,
//#endif
	MAX_mDNIe_MODE,
};

enum eCurrent_Temp {
    TEMP_STANDARD =0,
    TEMP_WARM,
    TEMP_COLD,
    MAX_TEMP_MODE,
};

enum eBackground_Mode {
    DYNAMIC_MODE = 0,
    STANDARD_MODE,
    MOVIE_MODE,
    MAX_BACKGROUND_MODE,
};


enum eCabc_Mode {
    CABC_OFF_MODE = 0,
    CABC_ON_MODE, 
    MAX_CABC_MODE,        
};

enum eOutdoor_Mode {
    OUTDOOR_OFF_MODE = 0,
    OUTDOOR_ON_MODE,
    MAX_OUTDOOR_MODE,
};

struct str_sub_unit {
    char *name;
    struct Cmc623RegisterSet *value;
};

struct str_sub_tuning {
/*
    Array Index 0 : cabc off tuning value
    Array Index 1 : cabc on tunning value 
*/    
    struct str_sub_unit value[MAX_CABC_MODE];
};

#define TUNE_FLAG_CABC_AUTO         0
#define TUNE_FLAG_CABC_ALWAYS_OFF   1
#define TUNE_FLAG_CABC_ALWAYS_ON    2 


struct str_main_unit {
    char *name;
    int flag;
    struct Cmc623RegisterSet *tune;
    unsigned char *plut;
};

struct str_main_tuning {
/*
    Array Index 0 : cabc off tuning value
    Array Index 1 : cabc on tunning value 
*/    
    struct str_main_unit value[MAX_CABC_MODE];
};

#define NUM_ITEM_POWER_LUT	9

extern void cmc623_suspend(struct early_suspend *h);
extern void cmc623_resume(struct early_suspend *h);


#endif  /*TUNE2CMC623_HEADER*/


