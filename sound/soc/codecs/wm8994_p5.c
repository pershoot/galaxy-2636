/*
 * wm8994_p5.c  --  WM8994 ALSA Soc Audio driver related Aries
 *
 *  Copyright (C) 2011 Samsung Electronics.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include "wm8994_samsung.h"

/*
 * Debug Feature
 */
#define SUBJECT "wm8994_p5.c"

/*
 * Definitions of tunning volumes for wm8994
 */
struct gain_info_t playback_gain_table[PLAYBACK_GAIN_NUM] = {
	{ /* COMMON */
		.mode = COMMON_SET_BIT,
		.reg  = WM8994_DAC1_LEFT_VOLUME,	/* 610h */
		.mask = WM8994_DAC1L_VOL_MASK,
		.gain = WM8994_DAC1_VU | 0xC0
	}, {
		.mode = COMMON_SET_BIT,
		.reg  = WM8994_DAC1_RIGHT_VOLUME,	/* 611h */
		.mask = WM8994_DAC1R_VOL_MASK,
		.gain = WM8994_DAC1_VU | 0xC0
	}, {
		.mode = COMMON_SET_BIT,
		.reg  = WM8994_AIF1_DAC1_LEFT_VOLUME,	/* 402h */
		.mask = WM8994_AIF1DAC1L_VOL_MASK,
		.gain = WM8994_AIF1DAC1_VU | 0xC0
	}, {
		.mode = COMMON_SET_BIT,
		.reg  = WM8994_AIF1_DAC1_RIGHT_VOLUME,	/* 403h */
		.mask = WM8994_AIF1DAC1R_VOL_MASK,
		.gain = WM8994_AIF1DAC1_VU | 0xC0
	}, { /* RCV */
		.mode = PLAYBACK_RCV,
		.reg  = WM8994_OUTPUT_MIXER_5,		/* 31h */
		.mask = WM8994_DACL_MIXOUTL_VOL_MASK,
		.gain = 0x0 << WM8994_DACL_MIXOUTL_VOL_SHIFT
	}, {
		.mode = PLAYBACK_RCV,
		.reg  = WM8994_OUTPUT_MIXER_6,		/* 32h */
		.mask = WM8994_DACR_MIXOUTR_VOL_MASK,
		.gain = 0x0 << WM8994_DACR_MIXOUTR_VOL_SHIFT
	}, {
		.mode = PLAYBACK_RCV,
		.reg  = WM8994_LEFT_OPGA_VOLUME,	/* 20h */
		.mask = WM8994_MIXOUTL_VOL_MASK,
		.gain = WM8994_MIXOUT_VU | 0x3D
	}, {
		.mode = PLAYBACK_RCV,
		.reg  = WM8994_RIGHT_OPGA_VOLUME,	/* 21h */
		.mask = WM8994_MIXOUTR_VOL_MASK,
		.gain = WM8994_MIXOUT_VU | 0x3D
	}, {
		.mode = PLAYBACK_RCV,
		.reg  = WM8994_HPOUT2_VOLUME,		/* 1Fh */
		.mask = WM8994_HPOUT2_VOL_MASK,
		.gain = 0x0 << WM8994_HPOUT2_VOL_SHIFT
	}, { /* SPK */
		.mode = PLAYBACK_SPK,
		.reg  = WM8994_SPKMIXL_ATTENUATION,	/* 22h */
		.mask = WM8994_SPKMIXL_VOL_MASK,
		.gain = 0x0
	}, {
		.mode = PLAYBACK_SPK,
		.reg  = WM8994_SPKMIXR_ATTENUATION,	/* 23h */
		.mask = WM8994_SPKMIXR_VOL_MASK,
		.gain = 0x0
	}, {
		.mode = PLAYBACK_SPK,
		.reg  = WM8994_SPEAKER_VOLUME_LEFT,	/* 26h */
		.mask = WM8994_SPKOUTL_VOL_MASK,
		.gain = WM8994_SPKOUT_VU | 0x3F         /* +6dB */
	}, {
		.mode = PLAYBACK_SPK,
		.reg  = WM8994_SPEAKER_VOLUME_RIGHT,	/* 27h */
		.mask = WM8994_SPKOUTR_VOL_MASK,
		.gain = WM8994_SPKOUT_VU | 0x3F         /* +6dB */
	}, {
		.mode = PLAYBACK_SPK,
		.reg  = WM8994_CLASSD,			/* 25h */
		.mask = WM8994_SPKOUTL_BOOST_MASK,
		.gain = 0x3 << WM8994_SPKOUTL_BOOST_SHIFT
	}, {
		.mode = PLAYBACK_SPK,
		.reg  = WM8994_CLASSD,			/* 25h */
		.mask = WM8994_SPKOUTR_BOOST_MASK,
		.gain = 0x3 << WM8994_SPKOUTR_BOOST_SHIFT
	}, {
		.mode = PLAYBACK_SPK,
		.reg  = WM8994_AIF1_DAC1_LEFT_VOLUME,	/* 402h */
		.mask = WM8994_AIF1DAC1L_VOL_MASK,
		.gain = WM8994_AIF1DAC1_VU | 0xC0       /* 0dB */
	}, {
		.mode = PLAYBACK_SPK,
		.reg  = WM8994_AIF1_DAC1_RIGHT_VOLUME,	/* 403h */
		.mask = WM8994_AIF1DAC1R_VOL_MASK,
		.gain = WM8994_AIF1DAC1_VU | 0xC0       /* 0dB */
	}, { /* HP */
		.mode = PLAYBACK_HP,
		.reg  = WM8994_LEFT_OUTPUT_VOLUME,	/* 1Ch */
		.mask = WM8994_HPOUT1L_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x2F
	}, {
		.mode = PLAYBACK_HP,
		.reg  = WM8994_RIGHT_OUTPUT_VOLUME,	/* 1Dh */
		.mask = WM8994_HPOUT1R_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x2F
	}, {
		.mode = PLAYBACK_HP,
		.reg  = WM8994_LEFT_OPGA_VOLUME,	/* 20h */
		.mask = WM8994_MIXOUTL_VOL_MASK,
		.gain = WM8994_MIXOUT_VU | 0x39
	}, {
		.mode = PLAYBACK_HP,
		.reg  = WM8994_RIGHT_OPGA_VOLUME,	/* 21h */
		.mask = WM8994_MIXOUTR_VOL_MASK,
		.gain = WM8994_MIXOUT_VU | 0x39
	}, { /* SPK_HP */
		.mode = PLAYBACK_SPK_HP,
		.reg  = WM8994_SPKMIXL_ATTENUATION,	/* 22h */
		.mask = WM8994_SPKMIXL_VOL_MASK,
		.gain = 0x0
	}, {
		.mode = PLAYBACK_SPK_HP,
		.reg  = WM8994_SPKMIXR_ATTENUATION,	/* 23h */
		.mask = WM8994_SPKMIXR_VOL_MASK,
		.gain = 0x0
	}, {
		.mode = PLAYBACK_SPK_HP,
		.reg  = WM8994_SPEAKER_VOLUME_LEFT,	/* 26h */
		.mask = WM8994_SPKOUTL_VOL_MASK,
		.gain = WM8994_SPKOUT_VU | 0x3F         /* +6dB */
	}, {
		.mode = PLAYBACK_SPK_HP,
		.reg  = WM8994_SPEAKER_VOLUME_RIGHT,	/* 27h */
		.mask = WM8994_SPKOUTR_VOL_MASK,
		.gain = WM8994_SPKOUT_VU | 0x3F         /* +6dB */
	}, {
		.mode = PLAYBACK_SPK_HP,
		.reg  = WM8994_CLASSD,			/* 25h */
		.mask = WM8994_SPKOUTL_BOOST_MASK,
		.gain = 0x6 << WM8994_SPKOUTL_BOOST_SHIFT /* +9dB */
	}, {
		.mode = PLAYBACK_SPK_HP,
		.reg  = WM8994_CLASSD,			/* 25h */
		.mask = WM8994_SPKOUTR_BOOST_MASK,
		.gain = 0x6 << WM8994_SPKOUTR_BOOST_SHIFT /* +9dB */
	}, {
		.mode = PLAYBACK_SPK_HP,
		.reg  = WM8994_LEFT_OUTPUT_VOLUME,	/* 1Ch */
		.mask = WM8994_HPOUT1L_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x1C
	}, {
		.mode = PLAYBACK_SPK_HP,
		.reg  = WM8994_RIGHT_OUTPUT_VOLUME,	/* 1Dh */
		.mask = WM8994_HPOUT1R_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x1C
	}, {
		.mode = PLAYBACK_SPK_HP,
		.reg  = WM8994_LEFT_OPGA_VOLUME,	/* 20h */
		.mask = WM8994_MIXOUTL_VOL_MASK,
		.gain = WM8994_MIXOUT_VU | 0x39
	}, {
		.mode = PLAYBACK_SPK_HP,
		.reg  = WM8994_RIGHT_OPGA_VOLUME,	/* 21h */
		.mask = WM8994_MIXOUTR_VOL_MASK,
		.gain = WM8994_MIXOUT_VU | 0x39
	}, { /* RING_SPK */
		.mode = PLAYBACK_RING_SPK,
		.reg  = WM8994_SPEAKER_VOLUME_LEFT,	/* 26h */
		.mask = WM8994_SPKOUTL_VOL_MASK,
		.gain = WM8994_SPKOUT_VU | 0x3E
	}, { /* RING_SPK */
		.mode = PLAYBACK_RING_SPK,
		.reg  = WM8994_SPEAKER_VOLUME_RIGHT, /* 27h */
		.mask = WM8994_SPKOUTR_VOL_MASK,
		.gain = WM8994_SPKOUT_VU | 0x3E
	}, {
		.mode = PLAYBACK_RING_SPK,
		.reg  = WM8994_CLASSD,			/* 25h */
		.mask = WM8994_SPKOUTL_BOOST_MASK,
		.gain = 0x6 << WM8994_SPKOUTL_BOOST_SHIFT /* +9dB */
	}, {
		.mode = PLAYBACK_RING_SPK,
		.reg  = WM8994_CLASSD,			/* 25h */
		.mask = WM8994_SPKOUTR_BOOST_MASK,
		.gain = 0x6 << WM8994_SPKOUTR_BOOST_SHIFT /* +9dB */
	}, { /* RING_HP */
		.mode = PLAYBACK_RING_HP,
		.reg  = WM8994_LEFT_OUTPUT_VOLUME,	/* 1Ch */
		.mask = WM8994_HPOUT1L_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x34
	}, {
		.mode = PLAYBACK_RING_HP,
		.reg  = WM8994_RIGHT_OUTPUT_VOLUME,	/* 1Dh */
		.mask = WM8994_HPOUT1R_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x34
	}, {
		.mode = PLAYBACK_RING_HP,
		.reg  = WM8994_LEFT_OPGA_VOLUME,	/* 20h */
		.mask = WM8994_MIXOUTL_VOL_MASK,
		.gain = WM8994_MIXOUT_VU | 0x39
	}, {
		.mode = PLAYBACK_RING_HP,
		.reg  = WM8994_RIGHT_OPGA_VOLUME,	/* 21h */
		.mask = WM8994_MIXOUTR_VOL_MASK,
		.gain = WM8994_MIXOUT_VU | 0x39
	}, { /* RING_SPK_HP */
		.mode = PLAYBACK_RING_SPK_HP,
		.reg  = WM8994_SPEAKER_VOLUME_LEFT,	/* 26h */
		.mask = WM8994_SPKOUTL_VOL_MASK,
		.gain = WM8994_SPKOUT_VU | 0x3E
	}, {
		.mode = PLAYBACK_RING_SPK_HP,
		.reg  = WM8994_SPEAKER_VOLUME_RIGHT, /* 26h */
		.mask = WM8994_SPKOUTR_VOL_MASK,
		.gain = WM8994_SPKOUT_VU | 0x3E
	}, {
		.mode = PLAYBACK_RING_SPK_HP,
		.reg  = WM8994_CLASSD,			/* 25h */
		.mask = WM8994_SPKOUTL_BOOST_MASK,
		.gain = 0x36 << WM8994_SPKOUTL_BOOST_SHIFT /* +6, +6dB */
	}, {
		.mode = PLAYBACK_RING_SPK_HP,
		.reg  = WM8994_CLASSD,			/* 25h */
		.mask = WM8994_SPKOUTR_BOOST_MASK,
		.gain = 0x36 << WM8994_SPKOUTR_BOOST_SHIFT /* +6, +6dB */
	}, {
		.mode = PLAYBACK_RING_SPK_HP,
		.reg  = WM8994_LEFT_OUTPUT_VOLUME,	/* 1Ch */
		.mask = WM8994_HPOUT1L_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x1C
	}, {
		.mode = PLAYBACK_RING_SPK_HP,
		.reg  = WM8994_RIGHT_OUTPUT_VOLUME,	/* 1Dh */
		.mask = WM8994_HPOUT1R_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x1C
	}, { /* HP_NO_MIC */
		.mode = PLAYBACK_HP_NO_MIC,
		.reg  = WM8994_LEFT_OUTPUT_VOLUME,  /* 1Ch */
		.mask = WM8994_HPOUT1L_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x2F
	}, {
		.mode = PLAYBACK_HP_NO_MIC,
		.reg  = WM8994_RIGHT_OUTPUT_VOLUME, /* 1Dh */
		.mask = WM8994_HPOUT1R_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x2F
	}, {
		.mode = PLAYBACK_HP_NO_MIC,
		.reg  = WM8994_LEFT_OPGA_VOLUME,	/* 20h */
		.mask = WM8994_MIXOUTL_VOL_MASK,
		.gain = WM8994_MIXOUT_VU | 0x39
	}, {
		.mode = PLAYBACK_HP_NO_MIC,
		.reg  = WM8994_RIGHT_OPGA_VOLUME,   /* 21h */
		.mask = WM8994_MIXOUTR_VOL_MASK,
		.gain = WM8994_MIXOUT_VU | 0x39
	}, { /* LINEOUT */
		.mode = PLAYBACK_LINEOUT,
		.reg  = WM8994_LEFT_OPGA_VOLUME,	/* 20h */
		.mask = WM8994_MIXOUTL_MUTE_N | WM8994_MIXOUTL_VOL_MASK,
		.gain = WM8994_MIXOUT_VU | WM8994_MIXOUTL_MUTE_N | 0x3E
	}, {
		.mode = PLAYBACK_LINEOUT,
		.reg  = WM8994_RIGHT_OPGA_VOLUME,	/* 21h */
		.mask = WM8994_MIXOUTR_MUTE_N | WM8994_MIXOUTR_VOL_MASK,
		.gain = WM8994_MIXOUT_VU | WM8994_MIXOUTR_MUTE_N | 0x3E
	}, {
		.mode = PLAYBACK_LINEOUT,
		.reg  = WM8994_OUTPUT_MIXER_5,		/* 31h */
		.mask = WM8994_DACL_MIXOUTL_VOL_MASK,
		.gain = 0x2 << WM8994_DACL_MIXOUTL_VOL_SHIFT
	}, {
		.mode = PLAYBACK_LINEOUT,
		.reg  = WM8994_OUTPUT_MIXER_6,		/* 32h */
		.mask = WM8994_DACR_MIXOUTR_VOL_MASK,
		.gain = 0x2 << WM8994_DACR_MIXOUTR_VOL_SHIFT
	}, {
		.mode = PLAYBACK_LINEOUT,
		.reg  = WM8994_LINE_OUTPUTS_VOLUME,	/* 1Eh */
		.mask = WM8994_LINEOUT2_VOL_MASK,
		.gain = 0x0 << WM8994_LINEOUT2_VOL_SHIFT
	}, {
		.mode = PLAYBACK_LINEOUT,
		.reg  = WM8994_DAC1_LEFT_VOLUME,	/* 610h */
		.mask = WM8994_DAC1L_MUTE_MASK | WM8994_DAC1L_VOL_MASK,
		.gain = WM8994_DAC1_VU | 0xC0
	}, {
		.mode = PLAYBACK_LINEOUT,
		.reg  = WM8994_DAC1_RIGHT_VOLUME,	/* 611h */
		.mask = WM8994_DAC1R_MUTE_MASK | WM8994_DAC1R_VOL_MASK,
		.gain = WM8994_DAC1_VU | 0xC0
	}, {
		.mode = PLAYBACK_LINEOUT,
		.reg  = WM8994_AIF1_DAC1_LEFT_VOLUME,	/* 402h */
		.mask = WM8994_AIF1DAC1L_VOL_MASK,
		.gain = WM8994_DAC1_VU | 0xC0
	}, {
		.mode = PLAYBACK_LINEOUT,
		.reg  = WM8994_AIF1_DAC1_RIGHT_VOLUME,	/* 403h */
		.mask = WM8994_AIF1DAC1R_VOL_MASK,
		.gain = WM8994_DAC1_VU | 0xC0
	},
};

struct gain_info_t voicecall_gain_table[VOICECALL_GAIN_NUM] = {
	{ /* COMMON */
		.mode = COMMON_SET_BIT,
		.reg  = WM8994_DAC1_LEFT_VOLUME,	/* 610h */
		.mask = WM8994_DAC1L_VOL_MASK,
		.gain = WM8994_DAC1_VU | 0xC0		/* 0dB */
	}, {
		.mode = COMMON_SET_BIT,
		.reg  = WM8994_DAC1_RIGHT_VOLUME,	/* 611h */
		.mask = WM8994_DAC1R_VOL_MASK,
		.gain = WM8994_DAC1_VU | 0xC0		/* 0dB */
	}, {
		.mode = COMMON_SET_BIT,
		.reg  = WM8994_AIF1_DAC1_LEFT_VOLUME,	/* 402h */
		.mask = WM8994_AIF1DAC1L_VOL_MASK,
		.gain = WM8994_AIF1DAC1_VU | 0xC0	/* 0dB */
	}, {
		.mode = COMMON_SET_BIT,
		.reg  = WM8994_AIF1_DAC1_RIGHT_VOLUME,	/* 403h */
		.mask = WM8994_AIF1DAC1R_VOL_MASK,
		.gain = WM8994_AIF1DAC1_VU | 0xC0	/* 0dB */
	}, {
		.mode = COMMON_SET_BIT,
		.reg  = WM8994_DAC2_LEFT_VOLUME,	/* 612h */
		.mask = WM8994_DAC2L_VOL_MASK,
		.gain = WM8994_DAC2_VU | 0xC0		/* 0dB */
	}, {
		.mode = COMMON_SET_BIT,
		.reg  = WM8994_DAC2_RIGHT_VOLUME,	/* 613h */
		.mask = WM8994_DAC2R_VOL_MASK,
		.gain = WM8994_DAC2_VU | 0xC0		/* 0dB */
	}, { /* LOOPBACK_RCV*/
		.mode = VOICECALL_RCV,
		.reg  = WM8994_INPUT_MIXER_4,		/* 29h */
		.mask = WM8994_IN2R_MIXINR_VOL_MASK | WM8994_MIXOUTR_MIXINR_VOL_MASK,
		.gain = 0x80				/* +30dB */
	}, {
		.mode = VOICECALL_RCV,
		.reg  = WM8994_RIGHT_LINE_INPUT_3_4_VOLUME,	/* 18h */
		.mask = WM8994_IN2R_VOL_MASK,
		.gain = WM8994_IN2R_VU | 0x17		/* +18dB */
	}, {
		.mode = VOICECALL_RCV,
		.reg  = WM8994_SPKMIXL_ATTENUATION,	/* 22h */
		.mask = WM8994_SPKMIXL_VOL_MASK,
		.gain = 0x0				/* +0dB */
	}, {
		.mode = VOICECALL_RCV,
		.reg  = WM8994_SPKMIXR_ATTENUATION,	/* 23h */
		.mask = WM8994_SPKMIXR_VOL_MASK,
		.gain = 0x0				/* +0dB */
	}, {
		.mode = VOICECALL_RCV,
		.reg  = WM8994_SPEAKER_VOLUME_LEFT,	/* 26h */
		.mask = WM8994_SPKOUTL_VOL_MASK,
		.gain = WM8994_SPKOUT_VU | 0x3E		/* +5dB */
	}, {
		.mode = VOICECALL_RCV,
		.reg  = WM8994_SPEAKER_VOLUME_RIGHT,	/* 27h */
		.mask = WM8994_SPKOUTR_VOL_MASK,
		.gain = WM8994_SPKOUT_VU | 0x3E		/* +5dB */
	}, {
		.mode = VOICECALL_RCV,
		.reg  = WM8994_CLASSD,			/* 25h */
		.mask = WM8994_SPKOUTL_BOOST_MASK,
		.gain = 0x4 << WM8994_SPKOUTL_BOOST_SHIFT	/* +6dB */
	}, {
		.mode = VOICECALL_RCV,
		.reg  = WM8994_CLASSD,			/* 25h */
		.mask = WM8994_SPKOUTR_BOOST_MASK,
		.gain = 0x4 << WM8994_SPKOUTR_BOOST_SHIFT	/* +6dB */
	}, { /* SPK */
		.mode = VOICECALL_SPK,
		.reg  = WM8994_INPUT_MIXER_3,		/* 29h */
		.mask = WM8994_IN1L_MIXINL_VOL_MASK | WM8994_MIXOUTL_MIXINL_VOL_MASK,
		.gain = 0x10				/* +30dB */
	}, {
		.mode = VOICECALL_SPK,
		.reg  = WM8994_LEFT_LINE_INPUT_1_2_VOLUME,	/* 18h */
		.mask = WM8994_IN1L_VOL_MASK,
		.gain = WM8994_IN1L_VU | 0x7		/* +8dB */
	}, {
		.mode = VOICECALL_SPK,
		.reg  = WM8994_SPKMIXL_ATTENUATION,	/* 22h */
		.mask = WM8994_SPKMIXL_VOL_MASK,
		.gain = 0x0				/* +0dB */
	}, {
		.mode = VOICECALL_SPK,
		.reg  = WM8994_SPKMIXR_ATTENUATION,	/* 23h */
		.mask = WM8994_SPKMIXR_VOL_MASK,
		.gain = 0x0				/* +0dB */
	}, {
		.mode = VOICECALL_SPK,
		.reg  = WM8994_SPEAKER_VOLUME_LEFT,	/* 26h */
		.mask = WM8994_SPKOUTL_VOL_MASK,
		.gain = WM8994_SPKOUT_VU | 0x3E		/* +5dB */
	}, {
		.mode = VOICECALL_SPK,
		.reg  = WM8994_SPEAKER_VOLUME_RIGHT,	/* 27h */
		.mask = WM8994_SPKOUTR_VOL_MASK,
		.gain = WM8994_SPKOUT_VU | 0x3E		/* +5dB */
	}, {
		.mode = VOICECALL_SPK,
		.reg  = WM8994_CLASSD,			/* 25h */
		.mask = WM8994_SPKOUTL_BOOST_MASK,
		.gain = 0x4 << WM8994_SPKOUTL_BOOST_SHIFT	/* +6dB */
	}, {
		.mode = VOICECALL_SPK,
		.reg  = WM8994_CLASSD,			/* 25h */
		.mask = WM8994_SPKOUTR_BOOST_MASK,
		.gain = 0x4 << WM8994_SPKOUTR_BOOST_SHIFT	/* +6dB */

	}, { /* HP */
		.mode = VOICECALL_HP,
		.reg  = WM8994_RIGHT_LINE_INPUT_1_2_VOLUME,	/* 1Ah */
		.mask = WM8994_IN1R_VOL_MASK,
		.gain = WM8994_IN1R_VU | 0x10		/* 15dB */
	}, {
		.mode = VOICECALL_HP,
		.reg  = WM8994_INPUT_MIXER_4,		/* 2Ah */
		.mask = WM8994_IN1R_MIXINR_VOL_MASK | WM8994_MIXOUTR_MIXINR_VOL_MASK,
		.gain = 0x30				/* +30dB*/
	}, {
		.mode = VOICECALL_HP,
		.reg  = WM8994_LEFT_OPGA_VOLUME,	/* 20h */
		.mask = WM8994_MIXOUTL_VOL_MASK,
		.gain = WM8994_MIXOUT_VU | 0x39
	}, {
		.mode = VOICECALL_HP,
		.reg  = WM8994_RIGHT_OPGA_VOLUME,	/* 21h */
		.mask = WM8994_MIXOUTR_VOL_MASK,
		.gain = WM8994_MIXOUT_VU | 0x39
	}, {
		.mode = VOICECALL_HP,
		.reg  = WM8994_LEFT_OUTPUT_VOLUME,	/* 1Ch */
		.mask = WM8994_HPOUT1L_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x33         /* -6dB */
	}, {
		.mode = VOICECALL_HP,
		.reg  = WM8994_RIGHT_OUTPUT_VOLUME,	/* 1Dh */
		.mask = WM8994_HPOUT1R_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x33         /* -6dB */
	}, { /* HP_NO_MIC */
		.mode = VOICECALL_HP_NO_MIC,
		.reg  = WM8994_LEFT_LINE_INPUT_1_2_VOLUME,	/* 18h */
		.mask = WM8994_IN1L_VOL_MASK,
		.gain = WM8994_IN1L_VU | 0x17		/* +18dB */
	}, {
		.mode = VOICECALL_HP_NO_MIC,
		.reg  = WM8994_INPUT_MIXER_3,		/* 29h */
		.mask = WM8994_IN1L_MIXINL_VOL_MASK | WM8994_MIXOUTL_MIXINL_VOL_MASK,
		.gain = 0x10                             /* +30dB */
	}, {
		.mode = VOICECALL_HP_NO_MIC,
		.reg  = WM8994_LEFT_OPGA_VOLUME,	/* 20h */
		.mask = WM8994_MIXOUTL_VOL_MASK,
		.gain = WM8994_MIXOUT_VU | 0x71
	}, {
		.mode = VOICECALL_HP_NO_MIC,
		.reg  = WM8994_RIGHT_OPGA_VOLUME,	/* 21h */
		.mask = WM8994_MIXOUTR_VOL_MASK,
		.gain = WM8994_MIXOUT_VU | 0x71
	}, {
		.mode = VOICECALL_HP_NO_MIC,
		.reg  = WM8994_LEFT_OUTPUT_VOLUME,	/* 1Ch */
		.mask = WM8994_HPOUT1L_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x39         /* 0dB */
	}, {
		.mode = VOICECALL_HP_NO_MIC,
		.reg  = WM8994_RIGHT_OUTPUT_VOLUME,	/* 1Dh */
		.mask = WM8994_HPOUT1R_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x39         /* 0dB */
	}

};

struct gain_info_t recording_gain_table[RECORDING_GAIN_NUM] = {
	{ /* MAIN */
		.mode = RECORDING_MAIN,
		.reg  = WM8994_LEFT_LINE_INPUT_1_2_VOLUME,	/* 18h */
		.mask = WM8994_IN1L_VOL_MASK,
		.gain = WM8994_IN1L_VU | 0x17
	}, {
		.mode = RECORDING_MAIN,
		.reg  = WM8994_INPUT_MIXER_3,		/* 29h */
		.mask = WM8994_IN1L_MIXINL_VOL_MASK | WM8994_MIXOUTL_MIXINL_VOL_MASK,
		.gain = 0x10				/* +30dB */
	}, {
		.mode = RECORDING_MAIN,
		.reg  = WM8994_AIF1_ADC1_LEFT_VOLUME,	/* 400h */
		.mask = WM8994_AIF1ADC1L_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xC0       /* 0dB */
	}, {
		.mode = RECORDING_MAIN,
		.reg  = WM8994_AIF1_ADC1_RIGHT_VOLUME,	/* 401h */
		.mask = WM8994_AIF1ADC1R_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xC0       /* 0dB */
	}, { /* HP */
		.mode = RECORDING_HP,
		.reg  = WM8994_RIGHT_LINE_INPUT_1_2_VOLUME,	/* 1Ah */
		.mask = WM8994_IN1R_VOL_MASK,
		.gain = WM8994_IN1R_VU | 0x15
	}, {
		.mode = RECORDING_HP,
		.reg  = WM8994_INPUT_MIXER_4,		/* 2Ah */
		.mask = WM8994_IN1R_MIXINR_VOL_MASK | WM8994_MIXOUTR_MIXINR_VOL_MASK,
		.gain = 0x10
	}, {
		.mode = RECORDING_HP,
		.reg  = WM8994_AIF1_ADC1_LEFT_VOLUME,	/* 400h */
		.mask = WM8994_AIF1ADC1L_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xC0
	}, {
		.mode = RECORDING_HP,
		.reg  = WM8994_AIF1_ADC1_RIGHT_VOLUME,	/* 401h */
		.mask = WM8994_AIF1ADC1R_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xC0
	}, { /* RECOGNITION_MAIN */
		.mode = RECORDING_REC_MAIN,
		.reg  = WM8994_LEFT_LINE_INPUT_1_2_VOLUME,	/* 18h */
		.mask = WM8994_IN1L_VOL_MASK,
		.gain = WM8994_IN1L_VU | 0x11
	}, {
		.mode = RECORDING_REC_MAIN,
		.reg  = WM8994_INPUT_MIXER_3,		/* 29h */
		.mask = WM8994_IN1L_MIXINL_VOL_MASK | WM8994_MIXOUTL_MIXINL_VOL_MASK,
		.gain = 0x10  /* 30dB */
	}, {
		.mode = RECORDING_REC_MAIN,
		.reg  = WM8994_AIF1_ADC1_LEFT_VOLUME,	/* 400h */
		.mask = WM8994_AIF1ADC1L_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xC0	/* +0dB */
	}, {
		.mode = RECORDING_REC_MAIN,
		.reg  = WM8994_AIF1_ADC1_RIGHT_VOLUME,	/* 401h */
		.mask = WM8994_AIF1ADC1R_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xC0	/* +0dB */
	}, { /* RECOGNITION_HP */
		.mode = RECORDING_REC_HP,
		.reg  = WM8994_RIGHT_LINE_INPUT_1_2_VOLUME,	/* 1Ah */
		.mask = WM8994_IN1R_VOL_MASK,
		.gain = WM8994_IN1R_VU | 0x15		/* +15dB */
	}, {
		.mode = RECORDING_REC_HP,
		.reg  = WM8994_INPUT_MIXER_4,		/* 2Ah */
		.mask = WM8994_IN1R_MIXINR_VOL_MASK | WM8994_MIXOUTR_MIXINR_VOL_MASK,
		.gain = 0x10				/* +30dB */
	}, {
		.mode = RECORDING_REC_HP,
		.reg  = WM8994_AIF1_ADC1_LEFT_VOLUME,	/* 400h */
		.mask = WM8994_AIF1ADC1L_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xC0
	}, {
		.mode = RECORDING_REC_HP,
		.reg  = WM8994_AIF1_ADC1_RIGHT_VOLUME,	/* 401h */
		.mask = WM8994_AIF1ADC1R_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xC0
	}, { /* CAMCORDER_MAIN */
		.mode = RECORDING_CAM_MAIN,
		.reg  = WM8994_LEFT_LINE_INPUT_1_2_VOLUME,	/* 18h */
		.mask = WM8994_IN1L_VOL_MASK,
		.gain = WM8994_IN1L_VU | 0x0B
	}, {
		.mode = RECORDING_CAM_MAIN,
		.reg  = WM8994_INPUT_MIXER_3,		/* 29h */
		.mask = WM8994_IN1L_MIXINL_VOL_MASK | WM8994_MIXOUTL_MIXINL_VOL_MASK,
		.gain = 0x10				/* 30dB */
	}, {
		.mode = RECORDING_CAM_MAIN,
		.reg  = WM8994_AIF1_ADC1_LEFT_VOLUME,	/* 400h */
		.mask = WM8994_AIF1ADC1L_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xC0	/* +0dB */
	}, {
		.mode = RECORDING_CAM_MAIN,
		.reg  = WM8994_AIF1_ADC1_RIGHT_VOLUME,	/* 401h */
		.mask = WM8994_AIF1ADC1R_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xC0	/* +0dB */
	}, { /* CAMCORDER_HP */
		.mode = RECORDING_CAM_HP,
		.reg  = WM8994_RIGHT_LINE_INPUT_1_2_VOLUME,	/* 1Ah */
		.mask = WM8994_IN1R_VOL_MASK,
		.gain = WM8994_IN1R_VU | 0x15		/* +15dB */
	}, {
		.mode = RECORDING_CAM_HP,
		.reg  = WM8994_INPUT_MIXER_4,		/* 2Ah */
		.mask = WM8994_IN1R_MIXINR_VOL_MASK | WM8994_MIXOUTR_MIXINR_VOL_MASK,
		.gain = 0x10				/* +30dB */
	}, {
		.mode = RECORDING_CAM_HP,
		.reg  = WM8994_AIF1_ADC1_LEFT_VOLUME,	/* 400h */
		.mask = WM8994_AIF1ADC1L_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xC0
	}, {
		.mode = RECORDING_CAM_HP,
		.reg  = WM8994_AIF1_ADC1_RIGHT_VOLUME,	/* 401h */
		.mask = WM8994_AIF1ADC1R_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xC0
	},
};
#if defined(CONFIG_TARGET_LOCALE_KOR)
struct gain_info_t voipcall_gain_table[VOIPCALL_GAIN_NUM] = {
	{ /* SPK */
		.mode = VOIPCALL_SPK,
		.reg  = WM8994_SPKMIXL_ATTENUATION,	/* 22h */
		.mask = WM8994_SPKMIXL_VOL_MASK,
		.gain = 0x0
	}, {
		.mode = VOIPCALL_SPK,
		.reg  = WM8994_SPKMIXR_ATTENUATION,	/* 23h */
		.mask = WM8994_SPKMIXR_VOL_MASK,
		.gain = 0x0
	}, {
		.mode = VOIPCALL_SPK,
		.reg  = WM8994_SPEAKER_VOLUME_LEFT,	/* 26h */
		.mask = WM8994_SPKOUTL_VOL_MASK,
		.gain = WM8994_SPKOUT_VU | 0x3D
	}, {
		.mode = VOIPCALL_SPK,
		.reg  = WM8994_SPEAKER_VOLUME_RIGHT,	/* 27h */
		.mask = WM8994_SPKOUTR_VOL_MASK,
		.gain = WM8994_SPKOUT_VU | 0x3D
	}, {
		.mode = VOIPCALL_SPK,
		.reg  = WM8994_CLASSD,			/* 25h */
		.mask = WM8994_SPKOUTL_BOOST_MASK,
		.gain = 0x06 << WM8994_SPKOUTL_BOOST_SHIFT
	}, {
		.mode = VOIPCALL_SPK,
		.reg  = WM8994_CLASSD,			/* 25h */
		.mask = WM8994_SPKOUTR_BOOST_MASK,
		.gain = 0x06 << WM8994_SPKOUTR_BOOST_SHIFT
	}, {
		.mode = VOIPCALL_SPK,
		.reg  = WM8994_AIF1_DAC1_LEFT_VOLUME,	/* 402h */
		.mask = WM8994_AIF1DAC1L_VOL_MASK,
		.gain = WM8994_AIF1DAC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_SPK,
		.reg  = WM8994_AIF1_DAC1_RIGHT_VOLUME,	/* 403h */
		.mask = WM8994_AIF1DAC1R_VOL_MASK,
		.gain = WM8994_AIF1DAC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_SPK,
		.reg  = WM8994_DAC1_LEFT_VOLUME,	/* 610h */
		.mask = WM8994_DAC1L_VOL_MASK,
		.gain = WM8994_DAC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_SPK,
		.reg  = WM8994_DAC1_RIGHT_VOLUME,	/* 611h */
		.mask = WM8994_DAC1R_VOL_MASK,
		.gain = WM8994_DAC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_SPK,
		.reg  = WM8994_AIF1_CONTROL_2,	/* 301h */
		.mask = WM8994_AIF1DAC_BOOST_MASK,
		.gain = 0x4000
	}, { /* HP */
		.mode = VOIPCALL_HP,
		.reg  = WM8994_DAC1_LEFT_VOLUME,	/* 610h */
		.mask = WM8994_DAC1L_VOL_MASK,
		.gain = WM8994_DAC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_HP,
		.reg  = WM8994_DAC1_RIGHT_VOLUME,	/* 611h */
		.mask = WM8994_DAC1R_VOL_MASK,
		.gain = WM8994_DAC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_HP,
		.reg  = WM8994_AIF1_CONTROL_2,	/* 301h */
		.mask = WM8994_AIF1DAC_BOOST_MASK,
		.gain = 0x4000
	}, {
		.mode = VOIPCALL_HP,
		.reg  = WM8994_AIF1_DAC1_LEFT_VOLUME,	/* 402h */
		.mask = WM8994_AIF1DAC1L_VOL_MASK,
		.gain = WM8994_AIF1DAC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_HP,
		.reg  = WM8994_AIF1_DAC1_RIGHT_VOLUME,	/* 403h */
		.mask = WM8994_AIF1DAC1R_VOL_MASK,
		.gain = WM8994_AIF1DAC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_HP,
		.reg  = WM8994_LEFT_OUTPUT_VOLUME,	/* 1Ch */
		.mask = WM8994_HPOUT1L_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x36
	}, {
		.mode = VOIPCALL_HP,
		.reg  = WM8994_RIGHT_OUTPUT_VOLUME,	/* 1Dh */
		.mask = WM8994_HPOUT1R_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x36
	}, {
		.mode = VOIPCALL_HP,
		.reg  = WM8994_LEFT_OPGA_VOLUME,	/* 20h */
		.mask = WM8994_MIXOUTL_VOL_MASK,
		.gain = WM8994_MIXOUT_VU | 0x39
	}, {
		.mode = VOIPCALL_HP,
		.reg  = WM8994_RIGHT_OPGA_VOLUME,	/* 21h */
		.mask = WM8994_MIXOUTR_VOL_MASK,
		.gain = WM8994_MIXOUT_VU | 0x39
	}, { /* MAIN_MIC*/
		.mode = VOIPCALL_MAINMIC,
		.reg  = WM8994_LEFT_LINE_INPUT_1_2_VOLUME,	/* 18h */
		.mask = WM8994_IN1L_VOL_MASK,
		.gain = WM8994_IN1L_VU | 0x13
	}, {
		.mode = VOIPCALL_MAINMIC,
		.reg  = WM8994_INPUT_MIXER_3,		/* 29h */
		.mask = WM8994_IN1L_MIXINL_VOL_MASK | WM8994_MIXOUTL_MIXINL_VOL_MASK,
		.gain = 0x20				/* +30dB */
	}, {
		.mode = VOIPCALL_MAINMIC,
		.reg  = WM8994_AIF1_ADC1_LEFT_VOLUME,	/* 400h */
		.mask = WM8994_AIF1ADC1L_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xef        /* 0dB */
	}, {
		.mode = VOIPCALL_MAINMIC,
		.reg  = WM8994_AIF1_ADC1_RIGHT_VOLUME,	/* 401h */
		.mask = WM8994_AIF1ADC1R_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xC0       /* 0dB */
	}, { /* HP_MIC */
		.mode = VOIPCALL_HPMIC,
		.reg  = WM8994_RIGHT_LINE_INPUT_1_2_VOLUME,	/* 1Ah */
		.mask = WM8994_IN1R_VOL_MASK,
		.gain = WM8994_IN1R_VU | 0x13
	}, {
		.mode = VOIPCALL_HPMIC,
		.reg  = WM8994_INPUT_MIXER_4,		/* 2Ah */
		.mask = WM8994_IN1R_MIXINR_VOL_MASK | WM8994_MIXOUTR_MIXINR_VOL_MASK,
		.gain = 0x20
	}, {
		.mode = VOIPCALL_HPMIC,
		.reg  = WM8994_AIF1_ADC1_LEFT_VOLUME,	/* 400h */
		.mask = WM8994_AIF1ADC1L_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_HPMIC,
		.reg  = WM8994_AIF1_ADC1_RIGHT_VOLUME,	/* 401h */
		.mask = WM8994_AIF1ADC1R_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xef
	}
};
#else /*CONFIG_TARGET_LOCALE_KOR */
struct gain_info_t voipcall_gain_table[VOIPCALL_GAIN_NUM] = {
	{ /* SPK */
		.mode = VOIPCALL_SPK,
		.reg  = WM8994_SPKMIXL_ATTENUATION,	/* 22h */
		.mask = WM8994_SPKMIXL_VOL_MASK,
		.gain = 0x0
	}, {
		.mode = VOIPCALL_SPK,
		.reg  = WM8994_SPKMIXR_ATTENUATION,	/* 23h */
		.mask = WM8994_SPKMIXR_VOL_MASK,
		.gain = 0x0
	}, {
		.mode = VOIPCALL_SPK,
		.reg  = WM8994_SPEAKER_VOLUME_LEFT,	/* 26h */
		.mask = WM8994_SPKOUTL_VOL_MASK,
		.gain = WM8994_SPKOUT_VU | 0x3D
	}, {
		.mode = VOIPCALL_SPK,
		.reg  = WM8994_SPEAKER_VOLUME_RIGHT,	/* 27h */
		.mask = WM8994_SPKOUTR_VOL_MASK,
		.gain = WM8994_SPKOUT_VU | 0x3D
	}, {
		.mode = VOIPCALL_SPK,
		.reg  = WM8994_CLASSD,			/* 25h */
		.mask = WM8994_SPKOUTL_BOOST_MASK,
		.gain = 0x06 << WM8994_SPKOUTL_BOOST_SHIFT
	}, {
		.mode = VOIPCALL_SPK,
		.reg  = WM8994_CLASSD,			/* 25h */
		.mask = WM8994_SPKOUTR_BOOST_MASK,
		.gain = 0x06 << WM8994_SPKOUTR_BOOST_SHIFT
	}, {
		.mode = VOIPCALL_SPK,
		.reg  = WM8994_AIF1_DAC1_LEFT_VOLUME,	/* 402h */
		.mask = WM8994_AIF1DAC1L_VOL_MASK,
		.gain = WM8994_AIF1DAC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_SPK,
		.reg  = WM8994_AIF1_DAC1_RIGHT_VOLUME,	/* 403h */
		.mask = WM8994_AIF1DAC1R_VOL_MASK,
		.gain = WM8994_AIF1DAC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_SPK,
		.reg  = WM8994_DAC1_LEFT_VOLUME,	/* 610h */
		.mask = WM8994_DAC1L_VOL_MASK,
		.gain = WM8994_DAC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_SPK,
		.reg  = WM8994_DAC1_RIGHT_VOLUME,	/* 611h */
		.mask = WM8994_DAC1R_VOL_MASK,
		.gain = WM8994_DAC1_VU | 0xC0
	}, { /* HP */
		.mode = VOIPCALL_HP,
		.reg  = WM8994_DAC1_LEFT_VOLUME,	/* 610h */
		.mask = WM8994_DAC1L_VOL_MASK,
		.gain = WM8994_DAC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_HP,
		.reg  = WM8994_DAC1_RIGHT_VOLUME,	/* 611h */
		.mask = WM8994_DAC1R_VOL_MASK,
		.gain = WM8994_DAC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_HP,
		.reg  = WM8994_AIF1_DAC1_LEFT_VOLUME,	/* 402h */
		.mask = WM8994_AIF1DAC1L_VOL_MASK,
		.gain = WM8994_AIF1DAC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_HP,
		.reg  = WM8994_AIF1_DAC1_RIGHT_VOLUME,	/* 403h */
		.mask = WM8994_AIF1DAC1R_VOL_MASK,
		.gain = WM8994_AIF1DAC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_HP,
		.reg  = WM8994_LEFT_OUTPUT_VOLUME,	/* 1Ch */
		.mask = WM8994_HPOUT1L_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x30
	}, {
		.mode = VOIPCALL_HP,
		.reg  = WM8994_RIGHT_OUTPUT_VOLUME,	/* 1Dh */
		.mask = WM8994_HPOUT1R_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x30
	}, {
		.mode = VOIPCALL_HP,
		.reg  = WM8994_LEFT_OPGA_VOLUME,	/* 20h */
		.mask = WM8994_MIXOUTL_VOL_MASK,
		.gain = WM8994_MIXOUT_VU | 0x39
	}, {
		.mode = VOIPCALL_HP,
		.reg  = WM8994_RIGHT_OPGA_VOLUME,	/* 21h */
		.mask = WM8994_MIXOUTR_VOL_MASK,
		.gain = WM8994_MIXOUT_VU | 0x39
	}, { /* MAIN_MIC*/
		.mode = VOIPCALL_MAINMIC,
		.reg  = WM8994_LEFT_LINE_INPUT_1_2_VOLUME,	/* 18h */
		.mask = WM8994_IN1L_VOL_MASK,
		.gain = WM8994_IN1L_VU | 0x13
	}, {
		.mode = VOIPCALL_MAINMIC,
		.reg  = WM8994_INPUT_MIXER_3,		/* 29h */
		.mask = WM8994_IN1L_MIXINL_VOL_MASK | WM8994_MIXOUTL_MIXINL_VOL_MASK,
		.gain = 0x20				/* +30dB */
	}, {
		.mode = VOIPCALL_MAINMIC,
		.reg  = WM8994_AIF1_ADC1_LEFT_VOLUME,	/* 400h */
		.mask = WM8994_AIF1ADC1L_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xEF       /* 0dB */
	}, {
		.mode = VOIPCALL_MAINMIC,
		.reg  = WM8994_AIF1_ADC1_RIGHT_VOLUME,	/* 401h */
		.mask = WM8994_AIF1ADC1R_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xC0       /* 0dB */
	}, { /* HP_MIC */
		.mode = VOIPCALL_HPMIC,
		.reg  = WM8994_RIGHT_LINE_INPUT_1_2_VOLUME,	/* 1Ah */
		.mask = WM8994_IN1R_VOL_MASK,
		.gain = WM8994_IN1R_VU | 0x1A
	}, {
		.mode = VOIPCALL_HPMIC,
		.reg  = WM8994_INPUT_MIXER_4,		/* 2Ah */
		.mask = WM8994_IN1R_MIXINR_VOL_MASK | WM8994_MIXOUTR_MIXINR_VOL_MASK,
		.gain = 0x10
	}, {
		.mode = VOIPCALL_HPMIC,
		.reg  = WM8994_AIF1_ADC1_LEFT_VOLUME,	/* 400h */
		.mask = WM8994_AIF1ADC1L_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_HPMIC,
		.reg  = WM8994_AIF1_ADC1_RIGHT_VOLUME,	/* 401h */
		.mask = WM8994_AIF1ADC1R_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xC0
	}, { /* HP OTHER */
		.mode = VOIPCALL_HP_OTHER,
		.reg  = WM8994_DAC1_LEFT_VOLUME,	/* 610h */
		.mask = WM8994_DAC1L_VOL_MASK,
		.gain = WM8994_DAC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_HP_OTHER,
		.reg  = WM8994_DAC1_RIGHT_VOLUME,	/* 611h */
		.mask = WM8994_DAC1R_VOL_MASK,
		.gain = WM8994_DAC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_HP_OTHER,
		.reg  = WM8994_AIF1_DAC1_LEFT_VOLUME,	/* 402h */
		.mask = WM8994_AIF1DAC1L_VOL_MASK,
		.gain = WM8994_AIF1DAC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_HP_OTHER,
		.reg  = WM8994_AIF1_DAC1_RIGHT_VOLUME,	/* 403h */
		.mask = WM8994_AIF1DAC1R_VOL_MASK,
		.gain = WM8994_AIF1DAC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_HP_OTHER,
		.reg  = WM8994_LEFT_OUTPUT_VOLUME,	/* 1Ch */
		.mask = WM8994_HPOUT1L_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x27
	}, {
		.mode = VOIPCALL_HP_OTHER,
		.reg  = WM8994_RIGHT_OUTPUT_VOLUME,	/* 1Dh */
		.mask = WM8994_HPOUT1R_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x27
	}, {
		.mode = VOIPCALL_HP_OTHER,
		.reg  = WM8994_LEFT_OPGA_VOLUME,	/* 20h */
		.mask = WM8994_MIXOUTL_VOL_MASK,
		.gain = WM8994_MIXOUT_VU | 0x39
	}, {
		.mode = VOIPCALL_HP_OTHER,
		.reg  = WM8994_RIGHT_OPGA_VOLUME,	/* 21h */
		.mask = WM8994_MIXOUTR_VOL_MASK,
		.gain = WM8994_MIXOUT_VU | 0x39
	}, { /* HP MIC OTHER */
		.mode = VOIPCALL_HPMIC_OTHER,
		.reg  = WM8994_RIGHT_LINE_INPUT_1_2_VOLUME,	/* 1Ah */
		.mask = WM8994_IN1R_VOL_MASK,
		.gain = WM8994_IN1R_VU | 0x18
	}, {
		.mode = VOIPCALL_HPMIC_OTHER,
		.reg  = WM8994_INPUT_MIXER_4,		/* 2Ah */
		.mask = WM8994_IN1R_MIXINR_VOL_MASK | WM8994_MIXOUTR_MIXINR_VOL_MASK,
		.gain = 0x10
	}, {
		.mode = VOIPCALL_HPMIC_OTHER,
		.reg  = WM8994_AIF1_ADC1_LEFT_VOLUME,	/* 400h */
		.mask = WM8994_AIF1ADC1L_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_HPMIC_OTHER,
		.reg  = WM8994_AIF1_ADC1_RIGHT_VOLUME,	/* 401h */
		.mask = WM8994_AIF1ADC1R_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xC0
	}, { /* SPK OTHER */
		.mode = VOIPCALL_SPK_OTHER,
		.reg  = WM8994_SPKMIXL_ATTENUATION,	/* 22h */
		.mask = WM8994_SPKMIXL_VOL_MASK,
		.gain = 0x0
	}, {
		.mode = VOIPCALL_SPK_OTHER,
		.reg  = WM8994_SPKMIXR_ATTENUATION,	/* 23h */
		.mask = WM8994_SPKMIXR_VOL_MASK,
		.gain = 0x0
	}, {
		.mode = VOIPCALL_SPK_OTHER,
		.reg  = WM8994_SPEAKER_VOLUME_LEFT,	/* 26h */
		.mask = WM8994_SPKOUTL_VOL_MASK,
		.gain = WM8994_SPKOUT_VU | 0x3D
	}, {
		.mode = VOIPCALL_SPK_OTHER,
		.reg  = WM8994_SPEAKER_VOLUME_RIGHT,	/* 27h */
		.mask = WM8994_SPKOUTR_VOL_MASK,
		.gain = WM8994_SPKOUT_VU | 0x3D
	}, {
		.mode = VOIPCALL_SPK_OTHER,
		.reg  = WM8994_CLASSD,			/* 25h */
		.mask = WM8994_SPKOUTL_BOOST_MASK,
		.gain = 0x06 << WM8994_SPKOUTL_BOOST_SHIFT
	}, {
		.mode = VOIPCALL_SPK_OTHER,
		.reg  = WM8994_CLASSD,			/* 25h */
		.mask = WM8994_SPKOUTR_BOOST_MASK,
		.gain = 0x06 << WM8994_SPKOUTR_BOOST_SHIFT
	}, {
		.mode = VOIPCALL_SPK_OTHER,
		.reg  = WM8994_AIF1_DAC1_LEFT_VOLUME,	/* 402h */
		.mask = WM8994_AIF1DAC1L_VOL_MASK,
		.gain = WM8994_AIF1DAC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_SPK_OTHER,
		.reg  = WM8994_AIF1_DAC1_RIGHT_VOLUME,	/* 403h */
		.mask = WM8994_AIF1DAC1R_VOL_MASK,
		.gain = WM8994_AIF1DAC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_SPK_OTHER,
		.reg  = WM8994_DAC1_LEFT_VOLUME,	/* 610h */
		.mask = WM8994_DAC1L_VOL_MASK,
		.gain = WM8994_DAC1_VU | 0xC0
	}, {
		.mode = VOIPCALL_SPK_OTHER,
		.reg  = WM8994_DAC1_RIGHT_VOLUME,	/* 611h */
		.mask = WM8994_DAC1R_VOL_MASK,
		.gain = WM8994_DAC1_VU | 0xC0
	}, { /* MAIN MIC OTHER */
		.mode = VOIPCALL_MAINMIC_OTHER,
		.reg  = WM8994_LEFT_LINE_INPUT_1_2_VOLUME,	/* 18h */
		.mask = WM8994_IN1L_VOL_MASK,
		.gain = WM8994_IN1L_VU | 0x0D
	}, {
		.mode = VOIPCALL_MAINMIC_OTHER,
		.reg  = WM8994_INPUT_MIXER_3,		/* 29h */
		.mask = WM8994_IN1L_MIXINL_VOL_MASK | WM8994_MIXOUTL_MIXINL_VOL_MASK,
		.gain = 0x20				/* +30dB */
	}, {
		.mode = VOIPCALL_MAINMIC_OTHER,
		.reg  = WM8994_AIF1_ADC1_LEFT_VOLUME,	/* 400h */
		.mask = WM8994_AIF1ADC1L_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xEF       /* 0dB */
	}, {
		.mode = VOIPCALL_MAINMIC_OTHER,
		.reg  = WM8994_AIF1_ADC1_RIGHT_VOLUME,	/* 401h */
		.mask = WM8994_AIF1ADC1R_VOL_MASK,
		.gain = WM8994_AIF1ADC1_VU | 0xC0       /* 0dB */
	}
};
#endif
struct gain_info_t gain_code_table[GAIN_CODE_NUM] = {
	/* Playback */
	{/* HP */
		.mode = PLAYBACK_HP | PLAYBACK_MODE | GAIN_DIVISION_BIT,
		.reg  = WM8994_LEFT_OUTPUT_VOLUME,	/* 1Ch */
		.mask = WM8994_HPOUT1L_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x39		/* 0dB */
	}, {
		.mode = PLAYBACK_HP | PLAYBACK_MODE | GAIN_DIVISION_BIT,
		.reg  = WM8994_RIGHT_OUTPUT_VOLUME,	/* 1Dh */
		.mask = WM8994_HPOUT1R_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x39		/* 0dB */
	}, {/* HP_NO_MIC */
		.mode = PLAYBACK_HP_NO_MIC | PLAYBACK_MODE | GAIN_DIVISION_BIT,
		.reg  = WM8994_LEFT_OUTPUT_VOLUME,	/* 1Ch */
		.mask = WM8994_HPOUT1L_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x36		/* -3dB */
	}, {
		.mode = PLAYBACK_HP_NO_MIC | PLAYBACK_MODE | GAIN_DIVISION_BIT,
		.reg  = WM8994_RIGHT_OUTPUT_VOLUME,	/* 1Dh */
		.mask = WM8994_HPOUT1R_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x36		/* -3dB */
	}, {/* RCV */
		.mode = VOICECALL_RCV | VOICECALL_MODE | GAIN_DIVISION_BIT,
		.reg  = WM8994_RIGHT_LINE_INPUT_3_4_VOLUME,	/* 18h */
		.mask = WM8994_IN2R_VOL_MASK,
		.gain = WM8994_IN2R_VU | 0x17		/* +15dB */
	}, {
		.mode = VOICECALL_RCV | VOICECALL_MODE | GAIN_DIVISION_BIT,
		.reg  = WM8994_SPEAKER_VOLUME_LEFT,	/* 26h */
		.mask = WM8994_SPKOUTL_VOL_MASK,
		.gain = WM8994_SPKOUT_VU | 0x3E		/* +5dB */
	}, {/* SPK */
		.mode = VOICECALL_SPK | VOICECALL_MODE | GAIN_DIVISION_BIT,
		.reg  = WM8994_LEFT_LINE_INPUT_1_2_VOLUME,	/* 18h */
		.mask = WM8994_IN1L_VOL_MASK,
		.gain = WM8994_IN1L_VU | 0x17		/* +15dB */
	}, {
		.mode = VOICECALL_SPK | VOICECALL_MODE | GAIN_DIVISION_BIT,
		.reg  = WM8994_SPEAKER_VOLUME_LEFT,	/* 26h */
		.mask = WM8994_SPKOUTL_VOL_MASK,
		.gain = WM8994_SPKOUT_VU | 0x3E		/* +5dB */
	}, {/* HP */
		.mode = VOICECALL_HP | VOICECALL_MODE | GAIN_DIVISION_BIT,
		.reg  = WM8994_RIGHT_LINE_INPUT_1_2_VOLUME,	/* 1Ah */
		.mask = WM8994_IN1R_VOL_MASK,
		.gain = WM8994_IN1R_VU | 0x1F		/* +29dB */
	}, {
		.mode = VOICECALL_HP | VOICECALL_MODE | GAIN_DIVISION_BIT,
		.reg  = WM8994_LEFT_OUTPUT_VOLUME,	/* 1Ch */
		.mask = WM8994_HPOUT1L_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x39		/* 0dB */
	}, {
		.mode = VOICECALL_HP | VOICECALL_MODE | GAIN_DIVISION_BIT,
		.reg  = WM8994_RIGHT_OUTPUT_VOLUME,	/* 1Dh */
		.mask = WM8994_HPOUT1R_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x39	        /* 0dB */
	}, {/* HP_NO_MIC */
		.mode = VOICECALL_HP_NO_MIC | VOICECALL_MODE | GAIN_DIVISION_BIT,
		.reg  = WM8994_LEFT_LINE_INPUT_1_2_VOLUME,	/* 18h */
		.mask = WM8994_IN1L_VOL_MASK,
		.gain = WM8994_IN1L_VU | 0x17		/* +15dB */
	}, {
		.mode = VOICECALL_HP_NO_MIC | VOICECALL_MODE | GAIN_DIVISION_BIT,
		.reg  = WM8994_LEFT_OUTPUT_VOLUME,	/* 1Ch */
		.mask = WM8994_HPOUT1L_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x39		/* 0dB */
	}, {
		.mode = VOICECALL_HP_NO_MIC | VOICECALL_MODE | GAIN_DIVISION_BIT,
		.reg  = WM8994_RIGHT_OUTPUT_VOLUME,	/* 1Dh */
		.mask = WM8994_HPOUT1R_VOL_MASK,
		.gain = WM8994_HPOUT1_VU | 0x39		/* 0dB */
	},
};

static void wait_for_dc_servo(struct snd_soc_codec *codec, unsigned int op)
{
	unsigned int reg;
	int count = 0;
	unsigned int val, start;

	val = op | WM8994_DCS_ENA_CHAN_0 | WM8994_DCS_ENA_CHAN_1;

	/* Trigger the command */
	snd_soc_write(codec, WM8994_DC_SERVO_1, val);

	start = jiffies;
	pr_debug("Waiting for DC servo...\n");

	do {
		count++;
		msleep(1);
		reg = snd_soc_read(codec, WM8994_DC_SERVO_1);
		pr_debug("DC servo: %x\n", reg);
	} while (reg & op && count < 400);

	pr_debug("DC servo took %dms\n", jiffies_to_msecs(jiffies - start));

	if (reg & op)
		pr_err("Timed out waiting for DC Servo\n");
}

int wm8994_configure_clock(struct snd_soc_codec *codec, int en)
{
/* TO DO : implementation clock by board condition
	struct wm8994_priv *wm8994 = codec->drvdata;

	if (en) {
		clk_enable(wm8994->codec_clk);
		DEBUG_LOG("USBOSC Enabled in Sleep Mode\n");
	} else {
		clk_disable(wm8994->codec_clk);
		DEBUG_LOG("USBOSC disable in Sleep Mode\n");
	}
*/
	return 0;
}

void audio_ctrl_mic_bias_gpio(struct wm8994_platform_data *pdata, int enable)
{
	DEBUG_LOG("enable = [%d]", enable);

	if (!pdata)
		pr_err("failed to turn off micbias pin\n");
	else {
		if (enable)
			pdata->set_mic_bias(true);
		else
			pdata->set_mic_bias(false);
	}
}

void wm8994_filter_SPK(struct snd_soc_codec *codec)
{
	u16 val;

	DEBUG_LOG("");

	/* removing Pop Noise when recording is stopped */
	val = wm8994_read(codec, 0x22);
	val |= (0x0003);
	wm8994_write(codec, 0x22, val);
	val = wm8994_read(codec, 0x23);
	val |= (0x0003);
	wm8994_write(codec, 0x23, val);

	/*AIF1DRC1*/
	wm8994_write(codec, 0x440, 0xbc);
	wm8994_write(codec, 0x441, 0x645);
	wm8994_write(codec, 0x442, 0x58);
	wm8994_write(codec, 0x443, 0x186);
	wm8994_write(codec, 0x444, 0x0);

	/* removing Pop Noise when recording is stopped */
	val = wm8994_read(codec, 0x22);
	val &= ~(0x0003);
	wm8994_write(codec, 0x22, val);
	val = wm8994_read(codec, 0x23);
	val &= ~(0x0003);
	wm8994_write(codec, 0x23, val);

	/*AIF1DRC2*/
	wm8994_write(codec, 0x450, 0x98);
	wm8994_write(codec, 0x451, 0x845);
	wm8994_write(codec, 0x452, 0x0);
	wm8994_write(codec, 0x453, 0x0);
	wm8994_write(codec, 0x454, 0x0);
}

void wm8994_filter_REC(struct snd_soc_codec *codec)
{
	DEBUG_LOG("");

	/*AIF1DRC1*/
	wm8994_write(codec, 0x440, 0x01ba); /* AIF1ADC1L Enable */
	wm8994_write(codec, 0x441, 0x0644);
	wm8994_write(codec, 0x442, 0x0469);
	wm8994_write(codec, 0x443, 0x0000);
	wm8994_write(codec, 0x444, 0x0260);
}

void wm8994_filter_VOIP(struct snd_soc_codec *codec)
{
	u16 val;

	DEBUG_LOG("");

	/* removing Pop Noise when recording is stopped*/
	val = wm8994_read(codec, 0x22);
	val |= (0x0003);
	wm8994_write(codec, 0x22, val);
	val = wm8994_read(codec, 0x23);
	val |= (0x0003);
	wm8994_write(codec, 0x23, val);

	/*AIF1DRC1*/
	wm8994_write(codec, 0x440, 0x19c); /* AIF1DAC1 Enable */
	wm8994_write(codec, 0x441, 0x645);
	wm8994_write(codec, 0x442, 0x850);
	wm8994_write(codec, 0x443, 0x18c);
	wm8994_write(codec, 0x444, 0x120);

	/* removing Pop Noise when recording is stopped */
	val = wm8994_read(codec, 0x22);
	val &= ~(0x0003);
	wm8994_write(codec, 0x22, val);
	val = wm8994_read(codec, 0x23);
	val &= ~(0x0003);
	wm8994_write(codec, 0x23, val);

}

void wm8994_filter_off(struct snd_soc_codec *codec)
{
	u16 val;
	DEBUG_LOG("")
	/*AIF1DRC1*/
	wm8994_write(codec, 0x440, 0x98);
	/*AIF1DRC2*/
	wm8994_write(codec, 0x450, 0x98);
	/*AIF1DAC1EQ*/
	val = wm8994_read(codec, 0x480);
	val &= ~(0x1);
	wm8994_write(codec, 0x480, val);
}

/* Audio Routing routines for the universal board..wm8994 codec*/
void wm8994_disable_path(struct snd_soc_codec *codec)
{
	struct wm8994_priv *wm8994 = codec->drvdata;

	u16 val;
	enum audio_path path = wm8994->cur_path;

	DEBUG_LOG("Path = [%d]", path);

	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_1);

	switch (path) {
	case RCV:
		/* Disbale the HPOUT2 */
		val &= ~(WM8994_HPOUT2_ENA_MASK);
		wm8994_write(codec, WM8994_POWER_MANAGEMENT_1, val);

		/* Disable left MIXOUT */
		val = wm8994_read(codec, WM8994_OUTPUT_MIXER_1);
		val &= ~(WM8994_DAC1L_TO_MIXOUTL_MASK);
		wm8994_write(codec, WM8994_OUTPUT_MIXER_1, val);

		/* Disable right MIXOUT */
		val = wm8994_read(codec, WM8994_OUTPUT_MIXER_2);
		val &= ~(WM8994_DAC1R_TO_MIXOUTR_MASK);
		wm8994_write(codec, WM8994_OUTPUT_MIXER_2, val);

		/* Disable HPOUT Mixer */
		val = wm8994_read(codec, WM8994_HPOUT2_MIXER);
		val &= ~(WM8994_MIXOUTLVOL_TO_HPOUT2_MASK |
			WM8994_MIXOUTRVOL_TO_HPOUT2_MASK);
		wm8994_write(codec, WM8994_HPOUT2_MIXER, val);

		/* Disable mixout volume control */
		val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_3);
		val &= ~(WM8994_MIXOUTLVOL_ENA_MASK |
			WM8994_MIXOUTRVOL_ENA_MASK |
			WM8994_MIXOUTL_ENA_MASK |
			WM8994_MIXOUTR_ENA_MASK);
		wm8994_write(codec, WM8994_POWER_MANAGEMENT_3, val);
		break;

	case SPK:
		/* Disbale the SPKOUTL */
		val &= ~(WM8994_SPKOUTL_ENA_MASK);
		wm8994_write(codec, WM8994_POWER_MANAGEMENT_1, val);

		/* Disable SPKLVOL */
		val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_3);
		val &= ~(WM8994_SPKLVOL_ENA_MASK);
		wm8994_write(codec, WM8994_POWER_MANAGEMENT_3, val);

		/* Disable SPKOUT mixer */
		val = wm8994_read(codec, WM8994_SPKOUT_MIXERS);
		val &= ~(WM8994_SPKMIXL_TO_SPKOUTL_MASK |
			 WM8994_SPKMIXR_TO_SPKOUTL_MASK |
			 WM8994_SPKMIXR_TO_SPKOUTR_MASK);
		wm8994_write(codec, WM8994_SPKOUT_MIXERS, val);

		/* Mute Speaker mixer */
		val = wm8994_read(codec, WM8994_SPEAKER_MIXER);
		val &= ~(WM8994_DAC1L_TO_SPKMIXL_MASK);
		wm8994_write(codec, WM8994_SPEAKER_MIXER, val);

		break;

	case HP:
	case HP_NO_MIC:
		val = wm8994_read(codec, WM8994_DAC1_LEFT_VOLUME);
		val &= ~(0x02C0);
		val |= 0x02C0;
		wm8994_write(codec, WM8994_DAC1_LEFT_VOLUME, 0x02C0);

		val = wm8994_read(codec, WM8994_DAC1_RIGHT_VOLUME);
		val &= ~(0x02C0);
		val |= 0x02C0;
		wm8994_write(codec, WM8994_DAC1_RIGHT_VOLUME, 0x02C0);

		val = wm8994_read(codec, WM8994_ANALOGUE_HP_1);
		val &= ~(0x0022);
		val |= 0x0022;
		wm8994_write(codec, WM8994_ANALOGUE_HP_1, 0x0022);

		val = wm8994_read(codec, WM8994_OUTPUT_MIXER_1);
		val &= ~(0x0);
		val |= 0x0;
		wm8994_write(codec, WM8994_OUTPUT_MIXER_1, 0x0);

		val = wm8994_read(codec, WM8994_OUTPUT_MIXER_2);
		val &= ~(0x0);
		val |= 0x0;
		wm8994_write(codec, WM8994_OUTPUT_MIXER_2, 0x0);

		#if !defined(CONFIG_TARGET_LOCALE_KOR)
		val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_5);
		val &= ~(0x0300);
		val |= 0x0300;
		wm8994_write(codec, WM8994_POWER_MANAGEMENT_5, 0x0300);
		#endif
		
		val = wm8994_read(codec, WM8994_CHARGE_PUMP_1);
		val &= ~(0x1F25);
		val |= 0x1F25;
		wm8994_write(codec, WM8994_CHARGE_PUMP_1, 0x1F25);
		break;

	case BT:
		val = wm8994_read(codec, WM8994_AIF1_DAC1_FILTERS_1);
		val &= ~(WM8994_AIF1DAC1_MUTE_MASK | WM8994_AIF1DAC1_MONO_MASK);
		val |= (WM8994_AIF1DAC1_MUTE);
		wm8994_write(codec, WM8994_AIF1_DAC1_FILTERS_1, val);
		break;

	case SPK_HP:
		val &= ~(WM8994_HPOUT1L_ENA_MASK | WM8994_HPOUT1R_ENA_MASK |
				WM8994_SPKOUTL_ENA_MASK);
		wm8994_write(codec, WM8994_POWER_MANAGEMENT_1, val);

		/* Disable DAC1L to HPOUT1L path */
		val = wm8994_read(codec, WM8994_OUTPUT_MIXER_1);
		val &= ~(WM8994_DAC1L_TO_HPOUT1L_MASK |
				WM8994_DAC1L_TO_MIXOUTL_MASK);
		wm8994_write(codec, WM8994_OUTPUT_MIXER_1, val);

		/* Disable DAC1R to HPOUT1R path */
		val = wm8994_read(codec, WM8994_OUTPUT_MIXER_2);
		val &= ~(WM8994_DAC1R_TO_HPOUT1R_MASK |
				WM8994_DAC1R_TO_MIXOUTR_MASK);
		wm8994_write(codec, WM8994_OUTPUT_MIXER_2, val);

		/* Disable Charge Pump */
		val = wm8994_read(codec, WM8994_CHARGE_PUMP_1);
		val &= ~WM8994_CP_ENA_MASK;
		val |= WM8994_CP_ENA_DEFAULT;
		wm8994_write(codec, WM8994_CHARGE_PUMP_1, val);

		/* Intermediate HP settings */
		val = wm8994_read(codec, WM8994_ANALOGUE_HP_1);
		val &= ~(WM8994_HPOUT1R_DLY_MASK | WM8994_HPOUT1R_OUTP_MASK |
		      WM8994_HPOUT1R_RMV_SHORT_MASK | WM8994_HPOUT1L_DLY_MASK |
		      WM8994_HPOUT1L_OUTP_MASK | WM8994_HPOUT1L_RMV_SHORT_MASK);
		wm8994_write(codec, WM8994_ANALOGUE_HP_1, val);

		/* Disable SPKLVOL */
		val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_3);
		val &= ~(WM8994_SPKLVOL_ENA_MASK);
		wm8994_write(codec, WM8994_POWER_MANAGEMENT_3, val);

		/* Disable SPKOUT mixer */
		val = wm8994_read(codec, WM8994_SPKOUT_MIXERS);
		val &= ~(WM8994_SPKMIXL_TO_SPKOUTL_MASK |
			 WM8994_SPKMIXR_TO_SPKOUTL_MASK |
			 WM8994_SPKMIXR_TO_SPKOUTR_MASK);
		wm8994_write(codec, WM8994_SPKOUT_MIXERS, val);

		/* Mute Speaker mixer */
		val = wm8994_read(codec, WM8994_SPEAKER_MIXER);
		val &= ~(WM8994_DAC1L_TO_SPKMIXL_MASK);
		wm8994_write(codec, WM8994_SPEAKER_MIXER, val);
		break;

	default:
		DEBUG_LOG_ERR("Path[%d] is not invaild!\n", path);
		return;
		break;
	}
}

void wm8994_disable_rec_path(struct snd_soc_codec *codec)
{
	struct wm8994_priv *wm8994 = codec->drvdata;

	u16 val;
	enum mic_path mic = wm8994->rec_path;

	wm8994->rec_path = MIC_OFF;

	if (!(wm8994->codec_state & CALL_ACTIVE))
		audio_ctrl_mic_bias_gpio(wm8994->pdata, 0);

	switch (mic) {
	case MAIN:
		DEBUG_LOG("Disabling MAIN Mic Path..\n");

		val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_2);
		val &= ~(WM8994_IN1L_ENA_MASK | WM8994_MIXINL_ENA_MASK);
		wm8994_write(codec, WM8994_POWER_MANAGEMENT_2, val);

		/* Mute IN1L PGA, update volume */
		val = wm8994_read(codec,
				WM8994_LEFT_LINE_INPUT_1_2_VOLUME);
		val &= ~(WM8994_IN1L_MUTE_MASK | WM8994_IN1L_VOL_MASK);
		val |= (WM8994_IN1L_VU | WM8994_IN1L_MUTE);
		wm8994_write(codec, WM8994_LEFT_LINE_INPUT_1_2_VOLUME,
				val);

		/*Mute the PGA */
		val = wm8994_read(codec, WM8994_INPUT_MIXER_3);
		val &= ~(WM8994_IN1L_TO_MIXINL_MASK |
			WM8994_IN1L_MIXINL_VOL_MASK |
			WM8994_MIXOUTL_MIXINL_VOL_MASK);
		wm8994_write(codec, WM8994_INPUT_MIXER_3, val);

		/* Disconnect IN1LN ans IN1LP to the inputs */
		val = wm8994_read(codec, WM8994_INPUT_MIXER_2);
		val &= (WM8994_IN1LN_TO_IN1L_MASK | WM8994_IN1LP_TO_IN1L_MASK);
		wm8994_write(codec, WM8994_INPUT_MIXER_2, val);

		/* Digital Paths */
		val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_4);
		val &= ~(WM8994_ADCL_ENA_MASK | WM8994_AIF1ADC1L_ENA_MASK);
		wm8994_write(codec, WM8994_POWER_MANAGEMENT_4, val);

		/* Disable timeslots */
		val = wm8994_read(codec, WM8994_AIF1_ADC1_LEFT_MIXER_ROUTING);
		val &= ~(WM8994_ADC1L_TO_AIF1ADC1L);
		wm8994_write(codec, WM8994_AIF1_ADC1_LEFT_MIXER_ROUTING, val);
		break;

	case EAR:
		DEBUG_LOG("Disbaling EAR Mic path..\n");
		val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_2);
		val &= ~(WM8994_IN1R_ENA_MASK | WM8994_MIXINR_ENA_MASK);
		wm8994_write(codec, WM8994_POWER_MANAGEMENT_2, val);

		/* Disable volume,unmute Right Line */
		val = wm8994_read(codec,
				WM8994_RIGHT_LINE_INPUT_1_2_VOLUME);
		val &= ~WM8994_IN1R_MUTE_MASK;	/* Unmute IN1R */
		val |= (WM8994_IN1R_VU | WM8994_IN1R_MUTE);
		wm8994_write(codec, WM8994_RIGHT_LINE_INPUT_1_2_VOLUME,
			     val);

		/* Mute right pga, set volume */
		val = wm8994_read(codec, WM8994_INPUT_MIXER_4);
		val &= ~(WM8994_IN1R_TO_MIXINR_MASK |
		      WM8994_IN1R_MIXINR_VOL_MASK |
		      WM8994_MIXOUTR_MIXINR_VOL_MASK);
		wm8994_write(codec, WM8994_INPUT_MIXER_4, val);

		/* Disconnect in1rn to inr1 and in1rp to inrp */
		val = wm8994_read(codec, WM8994_INPUT_MIXER_2);
		val &= ~(WM8994_IN1RN_TO_IN1R_MASK | WM8994_IN1RP_TO_IN1R_MASK);
		wm8994_write(codec, WM8994_INPUT_MIXER_2, val);

		/* Digital Paths */
		/* Disable right ADC and time slot */
		val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_4);
		val &= ~(WM8994_ADCR_ENA_MASK | WM8994_AIF1ADC1R_ENA_MASK);
		wm8994_write(codec, WM8994_POWER_MANAGEMENT_4, val);

		/* ADC Right mixer routing */
		val = wm8994_read(codec, WM8994_AIF1_ADC1_RIGHT_MIXER_ROUTING);
		val &= ~(WM8994_ADC1R_TO_AIF1ADC1R_MASK);
		wm8994_write(codec, WM8994_AIF1_ADC1_RIGHT_MIXER_ROUTING, val);

		/* Mixing left and right channel output to left channel */
		val = wm8994_read(codec, WM8994_AIF1_CONTROL_1);
		val &= ~(WM8994_AIF1ADCL_SRC_MASK | WM8994_AIF1ADCR_SRC_MASK);
		wm8994_write(codec, WM8994_AIF1_CONTROL_1, val);
		break;

	case BT_REC:
		DEBUG_LOG("Disbaling BT Mic path..\n");
		val = wm8994_read(codec, WM8994_AIF1_ADC1_LEFT_MIXER_ROUTING);
		val &= ~(WM8994_AIF2DACL_TO_AIF1ADC1L_MASK |
			WM8994_ADC1L_TO_AIF1ADC1L_MASK);
		wm8994_write(codec, WM8994_AIF1_ADC1_LEFT_MIXER_ROUTING, val);

		val = wm8994_read(codec, WM8994_AIF1_ADC1_RIGHT_MIXER_ROUTING);
		val &= ~(WM8994_AIF2DACR_TO_AIF1ADC1R_MASK |
			WM8994_ADC1R_TO_AIF1ADC1R_MASK);
		wm8994_write(codec, WM8994_AIF1_ADC1_RIGHT_MIXER_ROUTING, val);

		val = wm8994_read(codec, WM8994_AIF2_DAC_FILTERS_1);
		val &= ~(WM8994_AIF2DAC_MUTE_MASK);
		val |= (WM8994_AIF2DAC_MUTE);
		wm8994_write(codec, WM8994_AIF2_DAC_FILTERS_1, val);
		break;

	case MIC_OFF:
		DEBUG_LOG("Mic is already OFF!\n");
		break;

	default:
		DEBUG_LOG_ERR("Path[%d] is not invaild!\n", mic);
		break;
	}
}

void wm8994_set_bluetooth_common_setting(struct snd_soc_codec *codec)
{
	u32 val;

	wm8994_write(codec, WM8994_GPIO_1, 0xA101);
	wm8994_write(codec, WM8994_GPIO_2, 0x8100);
	wm8994_write(codec, WM8994_GPIO_3, 0x0100);
	wm8994_write(codec, WM8994_GPIO_4, 0x0100);
	wm8994_write(codec, WM8994_GPIO_5, 0x8100);
	wm8994_write(codec, WM8994_GPIO_6, 0xA101);
	wm8994_write(codec, WM8994_GPIO_7, 0x0100);
	wm8994_write(codec, WM8994_GPIO_8, 0x8100);
	wm8994_write(codec, WM8994_GPIO_9, 0x0100);
	wm8994_write(codec, WM8994_GPIO_10, 0x0100);
	wm8994_write(codec, WM8994_GPIO_11, 0x0100);

	wm8994_write(codec, WM8994_FLL2_CONTROL_2, 0x2D00);
	wm8994_write(codec, WM8994_FLL2_CONTROL_3, 0x583C);
	wm8994_write(codec, WM8994_FLL2_CONTROL_4, 0x0100);
	wm8994_write(codec, WM8994_FLL2_CONTROL_5, 0x0C80);

	wm8994_write(codec, WM8994_FLL2_CONTROL_1,
		WM8994_FLL2_FRACN_ENA | WM8994_FLL2_ENA);

	val = wm8994_read(codec, WM8994_AIF2_CLOCKING_1);
	if (!(val & WM8994_AIF2CLK_ENA))
		wm8994_write(codec, WM8994_AIF2_CLOCKING_1, 0x0018);
	wm8994_write(codec, WM8994_AIF2_RATE, 0x3 << WM8994_AIF2CLK_RATE_SHIFT);

	/* AIF2 Interface - PCM Stereo mode */
	/* Left Justified, BCLK invert, LRCLK Invert */
#ifdef CALL_I2S
	wm8994_write(codec, WM8994_AIF2_CONTROL_1, 0x4010);
#else
	wm8994_write(codec, WM8994_AIF2_CONTROL_1,
		WM8994_AIF2ADCR_SRC | WM8994_AIF2_BCLK_INV | 0x18);
#endif
	wm8994_write(codec, WM8994_AIF2_BCLK, 0x70);
	wm8994_write(codec, WM8994_AIF2_CONTROL_2, 0x0000);
	wm8994_write(codec, WM8994_AIF2_MASTER_SLAVE, WM8994_AIF2_MSTR |
		WM8994_AIF2_CLK_FRC | WM8994_AIF2_LRCLK_FRC);

	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_5);
	val &= ~(WM8994_AIF2DACL_ENA_MASK | WM8994_AIF2DACR_ENA_MASK |
		WM8994_AIF1DAC1L_ENA_MASK | WM8994_AIF1DAC1R_ENA_MASK |
		WM8994_DAC1L_ENA_MASK | WM8994_DAC1R_ENA_MASK);
	val |= (WM8994_AIF2DACL_ENA | WM8994_AIF2DACR_ENA |
		WM8994_AIF1DAC1L_ENA | WM8994_AIF1DAC1R_ENA |
		WM8994_DAC1L_ENA | WM8994_DAC1R_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_5, val);

	/* Clocking */
	val = wm8994_read(codec, WM8994_CLOCKING_1);
	val |= (WM8994_DSP_FS2CLK_ENA | WM8994_SYSCLK_SRC);
	wm8994_write(codec, WM8994_CLOCKING_1, val);

	/* AIF1 & AIF2 Output is connected to DAC1 */
	val = wm8994_read(codec, WM8994_DAC1_LEFT_MIXER_ROUTING);
	val &= ~(WM8994_AIF1DAC1L_TO_DAC1L_MASK |
		WM8994_AIF2DACL_TO_DAC1L_MASK);
	val |= (WM8994_AIF1DAC1L_TO_DAC1L | WM8994_AIF2DACL_TO_DAC1L);
	wm8994_write(codec, WM8994_DAC1_LEFT_MIXER_ROUTING, val);

	val = wm8994_read(codec, WM8994_DAC1_RIGHT_MIXER_ROUTING);
	val &= ~(WM8994_AIF1DAC1R_TO_DAC1R_MASK |
		WM8994_AIF2DACR_TO_DAC1R_MASK);
	val |= (WM8994_AIF1DAC1R_TO_DAC1R | WM8994_AIF2DACR_TO_DAC1R);
	wm8994_write(codec, WM8994_DAC1_RIGHT_MIXER_ROUTING, val);
}

void wm8994_record_headset_mic(struct snd_soc_codec *codec)
{
	struct wm8994_priv *wm8994 = codec->drvdata;

	u16 val;

	DEBUG_LOG("Recording through Headset Mic\n");

	wm8994_write(codec, WM8994_ANTIPOP_2, 0x68);

	/* Enable high pass filter to control bounce on startup */
	val = wm8994_read(codec, WM8994_AIF1_ADC1_FILTERS);
	val &= ~(WM8994_AIF1ADC1L_HPF_MASK | WM8994_AIF1ADC1R_HPF_MASK);
	val |= (WM8994_AIF1ADC1R_HPF);
	wm8994_write(codec, WM8994_AIF1_ADC1_FILTERS, val);

	/* Enable mic bias, vmid, bias generator */
	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_1);
	val &= ~(WM8994_BIAS_ENA_MASK | WM8994_VMID_SEL_MASK);
	val |= (WM8994_BIAS_ENA | WM8994_VMID_SEL_NORMAL);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_1, val);

	val = wm8994_read(codec, WM8994_INPUT_MIXER_1);
	val &= ~(WM8994_INPUTS_CLAMP_MASK);
	val |= (WM8994_INPUTS_CLAMP);
	wm8994_write(codec, WM8994_INPUT_MIXER_1, val);

	val = (WM8994_MIXINR_ENA | WM8994_IN1R_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_2, val);


	val = (WM8994_IN1RN_TO_IN1R | WM8994_IN1RP_TO_IN1R);
	wm8994_write(codec, WM8994_INPUT_MIXER_2, val);

	val = wm8994_read(codec, WM8994_RIGHT_LINE_INPUT_1_2_VOLUME);
	val &= ~(WM8994_IN1R_MUTE_MASK);
	wm8994_write(codec, WM8994_RIGHT_LINE_INPUT_1_2_VOLUME, val);

	val = wm8994_read(codec, WM8994_INPUT_MIXER_4);
	val &= ~(WM8994_IN1R_TO_MIXINR_MASK);
	val |= (WM8994_IN1R_TO_MIXINR);
	wm8994_write(codec, WM8994_INPUT_MIXER_4 , val);

	val = wm8994_read(codec, WM8994_INPUT_MIXER_1);
	val &= ~(WM8994_INPUTS_CLAMP_MASK);
	wm8994_write(codec, WM8994_INPUT_MIXER_1, val);

	val = wm8994_read(codec, WM8994_AIF1_ADC1_RIGHT_VOLUME);
	val |= (WM8994_AIF1ADC1_VU);
	wm8994_write(codec, WM8994_AIF1_ADC1_RIGHT_VOLUME, val);

	val = wm8994_read(codec, WM8994_AIF1_ADC1_FILTERS);
	val &= ~(WM8994_AIF1ADC1L_HPF_MASK | WM8994_AIF1ADC1R_HPF_MASK);
	val |= (WM8994_AIF1ADC1R_HPF | 0x2000);
	wm8994_write(codec, WM8994_AIF1_ADC1_FILTERS, val);

	wm8994_write(codec, WM8994_AIF1_MASTER_SLAVE, val);

	wm8994_write(codec, WM8994_GPIO_1, 0xA101);

	/* Mixing left channel output to right channel */
	val = wm8994_read(codec, WM8994_AIF1_CONTROL_1);
	val &= ~(WM8994_AIF1ADCL_SRC_MASK | WM8994_AIF1ADCR_SRC_MASK);
	val |= (WM8994_AIF1ADCL_SRC | WM8994_AIF1ADCR_SRC);
	wm8994_write(codec, WM8994_AIF1_CONTROL_1, val);

	/* Digital Paths  */
	/* Enable right ADC and time slot */
	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_4);
	val &= ~(WM8994_ADCR_ENA_MASK | WM8994_AIF1ADC1R_ENA_MASK);
	val |= (WM8994_AIF1ADC1R_ENA | WM8994_ADCR_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_4, val);


	/* ADC Right mixer routing */
	val = wm8994_read(codec, WM8994_AIF1_ADC1_RIGHT_MIXER_ROUTING);
	val &= ~(WM8994_ADC1R_TO_AIF1ADC1R_MASK);
	val |= WM8994_ADC1R_TO_AIF1ADC1R;
	wm8994_write(codec, WM8994_AIF1_ADC1_RIGHT_MIXER_ROUTING, val);

	val = wm8994_read(codec, WM8994_SPEAKER_MIXER);
	val &= ~WM8994_MIXINL_TO_SPKMIXL_MASK;
	wm8994_write(codec, WM8994_SPEAKER_MIXER, val);

	val = wm8994_read(codec, WM8994_OUTPUT_MIXER_1);
	val &= ~WM8994_MIXINL_TO_MIXOUTL_MASK;
	wm8994_write(codec, WM8994_OUTPUT_MIXER_1, val);

	val = wm8994_read(codec, WM8994_OUTPUT_MIXER_2);
	val &= ~WM8994_MIXINR_TO_MIXOUTR_MASK;
	wm8994_write(codec, WM8994_OUTPUT_MIXER_2, val);

	val = wm8994_read(codec, WM8994_DAC2_LEFT_MIXER_ROUTING);
	val &= ~(WM8994_ADC1_TO_DAC2L_MASK);
	wm8994_write(codec, WM8994_DAC2_LEFT_MIXER_ROUTING, val);

	val = wm8994_read(codec, WM8994_DAC2_RIGHT_MIXER_ROUTING);
	val &= ~(WM8994_ADC1_TO_DAC2R_MASK);
	wm8994_write(codec, WM8994_DAC2_RIGHT_MIXER_ROUTING, val);

#if defined(CONFIG_TARGET_LOCALE_KOR)
	if (wm8994->voip_call_active)
		wm8994_set_codec_gain(codec, VOIPCALL_MODE, VOIPCALL_HPMIC);
#else
	if (wm8994->voip_call_active == VOIP_ON)
		wm8994_set_codec_gain(codec, VOIPCALL_MODE, VOIPCALL_HPMIC);
	else if (wm8994->voip_call_active == VOIP_ON_OTHER)
		wm8994_set_codec_gain(codec, VOIPCALL_MODE, VOIPCALL_HPMIC_OTHER);
#endif
	else if (wm8994->input_source == RECOGNITION)
		wm8994_set_codec_gain(codec, RECORDING_MODE, RECORDING_REC_HP);
	else if (wm8994->input_source == CAMCORDER)
		wm8994_set_codec_gain(codec, RECORDING_MODE, RECORDING_CAM_HP);
	else
		wm8994_set_codec_gain(codec, RECORDING_MODE, RECORDING_HP);

	/* loopback for factory test */
	if (wm8994->loopback_path_control == ear) {
		wm8994_write(codec, WM8994_RIGHT_LINE_INPUT_1_2_VOLUME,
			WM8994_IN1R_VU | 0x0E);

		val = wm8994_read(codec, WM8994_AIF1_MASTER_SLAVE);
		val |= (WM8994_AIF1_MSTR | WM8994_AIF1_CLK_FRC |
			WM8994_AIF1_LRCLK_FRC);
		wm8994_write(codec, WM8994_AIF1_MASTER_SLAVE, val);

		wm8994_write(codec, WM8994_FLL1_CONTROL_2, 0x0700);
		wm8994_write(codec, WM8994_FLL1_CONTROL_3, 0x86C2);
		wm8994_write(codec, WM8994_FLL1_CONTROL_5, 0x0C88);
		wm8994_write(codec, WM8994_FLL1_CONTROL_4, 0x00E0);
		wm8994_write(codec, WM8994_FLL1_CONTROL_1,
			WM8994_FLL1_FRACN_ENA | WM8994_FLL1_ENA);

		val = wm8994_read(codec, WM8994_AIF1_CONTROL_2);
		val |= 0x01;
		wm8994_write(codec, WM8994_AIF1_CONTROL_2, val);

		DEBUG_LOG("AT Command Codec Loopback\n");
	}

}

void wm8994_record_main_mic(struct snd_soc_codec *codec)
{
	struct wm8994_priv *wm8994 = codec->drvdata;

	u16 val;

	/* Delay for removing Pop Noise when camcorder recording is started*/
	if (wm8994->input_source == CAMCORDER)
		msleep(200);

	DEBUG_LOG("Recording through Main Mic\n");
	audio_ctrl_mic_bias_gpio(wm8994->pdata, 1);

	/* Main mic volume issue fix: requested H/W */
	wm8994_write(codec, WM8994_ANTIPOP_2, 0x68);

	/* High pass filter to control bounce on enable */
	val = wm8994_read(codec, WM8994_AIF1_ADC1_FILTERS);
	val &= ~(WM8994_AIF1ADC1L_HPF_MASK | WM8994_AIF1ADC1R_HPF_MASK);
	val |= (WM8994_AIF1ADC1L_HPF);
	wm8994_write(codec, WM8994_AIF1_ADC1_FILTERS, val);

	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_1);
	val &= ~(WM8994_BIAS_ENA_MASK | WM8994_VMID_SEL_MASK);
	val |= (WM8994_BIAS_ENA | WM8994_VMID_SEL_NORMAL);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_1, val);

	val = wm8994_read(codec, WM8994_INPUT_MIXER_1);
	val &= ~(WM8994_INPUTS_CLAMP_MASK);
	val |= (WM8994_INPUTS_CLAMP);
	wm8994_write(codec, WM8994_INPUT_MIXER_1, val);

	val = (WM8994_MIXINL_ENA | WM8994_IN1L_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_2, val);

	val = (WM8994_IN1LP_TO_IN1L | WM8994_IN1LN_TO_IN1L);
	wm8994_write(codec, WM8994_INPUT_MIXER_2, val);


	val = wm8994_read(codec, WM8994_LEFT_LINE_INPUT_1_2_VOLUME);
	val &= ~(WM8994_IN1L_MUTE_MASK);
	wm8994_write(codec, WM8994_LEFT_LINE_INPUT_1_2_VOLUME, val);

	val = wm8994_read(codec, WM8994_INPUT_MIXER_3);
	val &= ~(WM8994_IN1L_TO_MIXINL_MASK);
	val |= (WM8994_IN1L_TO_MIXINL);
	wm8994_write(codec, WM8994_INPUT_MIXER_3, val);

	val = wm8994_read(codec, WM8994_INPUT_MIXER_1);
	val &= ~(WM8994_INPUTS_CLAMP_MASK);
	wm8994_write(codec, WM8994_INPUT_MIXER_1, val);


	val = wm8994_read(codec, WM8994_AIF1_ADC1_LEFT_VOLUME);
	val |= (WM8994_AIF1ADC1_VU);
	wm8994_write(codec, WM8994_AIF1_ADC1_LEFT_VOLUME, val);

	val = wm8994_read(codec, WM8994_AIF1_ADC1_FILTERS);
	val &= ~(WM8994_AIF1ADC1L_HPF_MASK | WM8994_AIF1ADC1R_HPF_MASK);
	val |= (WM8994_AIF1ADC1L_HPF | 0x2000);
	wm8994_write(codec, WM8994_AIF1_ADC1_FILTERS, val);

	wm8994_write(codec, WM8994_GPIO_1, 0xA101);

	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_4);
	val &= ~(WM8994_ADCL_ENA_MASK | WM8994_AIF1ADC1L_ENA_MASK);
	val |= (WM8994_AIF1ADC1L_ENA | WM8994_ADCL_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_4, val);

	/* Enable timeslots */
	val = wm8994_read(codec, WM8994_AIF1_ADC1_LEFT_MIXER_ROUTING);
	val |= WM8994_ADC1L_TO_AIF1ADC1L;
	wm8994_write(codec, WM8994_AIF1_ADC1_LEFT_MIXER_ROUTING, val);

	val = wm8994_read(codec, WM8994_SPEAKER_MIXER);
	val &= ~WM8994_MIXINL_TO_SPKMIXL_MASK;
	wm8994_write(codec, WM8994_SPEAKER_MIXER, val);

	val = wm8994_read(codec, WM8994_OUTPUT_MIXER_1);
	val &= ~WM8994_MIXINL_TO_MIXOUTL_MASK;
	wm8994_write(codec, WM8994_OUTPUT_MIXER_1, val);

	val = wm8994_read(codec, WM8994_OUTPUT_MIXER_2);
	val &= ~WM8994_MIXINR_TO_MIXOUTR_MASK;
	wm8994_write(codec, WM8994_OUTPUT_MIXER_2, val);

	val = wm8994_read(codec, WM8994_DAC2_LEFT_MIXER_ROUTING);
	val &= ~(WM8994_ADC1_TO_DAC2L_MASK);
	wm8994_write(codec, WM8994_DAC2_LEFT_MIXER_ROUTING, val);

	val = wm8994_read(codec, WM8994_DAC2_RIGHT_MIXER_ROUTING);
	val &= ~(WM8994_ADC1_TO_DAC2R_MASK);
	wm8994_write(codec, WM8994_DAC2_RIGHT_MIXER_ROUTING, val);

#if defined(CONFIG_TARGET_LOCALE_KOR)
	if (wm8994->voip_call_active)
		wm8994_set_codec_gain(codec, VOIPCALL_MODE, VOIPCALL_MAINMIC);
#else
	if (wm8994->voip_call_active == VOIP_ON)
		wm8994_set_codec_gain(codec, VOIPCALL_MODE, VOIPCALL_MAINMIC);
	else if (wm8994->voip_call_active == VOIP_ON_OTHER)
		wm8994_set_codec_gain(codec, VOIPCALL_MODE, VOIPCALL_MAINMIC_OTHER);
#endif
	else if (wm8994->input_source == RECOGNITION)
		wm8994_set_codec_gain(codec, RECORDING_MODE,
				RECORDING_REC_MAIN);
	else if (wm8994->input_source == CAMCORDER)
		wm8994_set_codec_gain(codec, RECORDING_MODE,
				RECORDING_CAM_MAIN);
	else
		wm8994_set_codec_gain(codec, RECORDING_MODE, RECORDING_MAIN);

	/* loopback for factory test */
	if (wm8994->loopback_path_control == spk ||
		wm8994->loopback_path_control == ear_pmic) {
		val = wm8994_read(codec, WM8994_AIF1_CONTROL_1);
		val &= ~(WM8994_AIF1ADCL_SRC_MASK | WM8994_AIF1ADCR_SRC_MASK);
		wm8994_write(codec, WM8994_AIF1_CONTROL_1, val);

		val = wm8994_read(codec, WM8994_AIF1_MASTER_SLAVE);
		val |= (WM8994_AIF1_MSTR | WM8994_AIF1_CLK_FRC |
			WM8994_AIF1_LRCLK_FRC);
		wm8994_write(codec, WM8994_AIF1_MASTER_SLAVE, val);

		wm8994_write(codec, WM8994_FLL1_CONTROL_2, 0x0700);
		wm8994_write(codec, WM8994_FLL1_CONTROL_3, 0x86C2);
		wm8994_write(codec, WM8994_FLL1_CONTROL_5, 0x0C88);
		wm8994_write(codec, WM8994_FLL1_CONTROL_4, 0x00E0);
		wm8994_write(codec, WM8994_FLL1_CONTROL_1,
			WM8994_FLL1_FRACN_ENA | WM8994_FLL1_ENA);

		val = wm8994_read(codec, WM8994_AIF1_CONTROL_2);
		val |= 0x01;
		wm8994_write(codec, WM8994_AIF1_CONTROL_2, val);

		/* Input Gain */
		val = wm8994_read(codec, WM8994_LEFT_LINE_INPUT_1_2_VOLUME);
		val &= ~(WM8994_IN1L_VOL_MASK);
		val |= (0x0100 | 0x17);
		wm8994_write(codec, WM8994_LEFT_LINE_INPUT_1_2_VOLUME, val);

		val = wm8994_read(codec, WM8994_INPUT_MIXER_3);
		val &= ~(WM8994_IN1L_MIXINL_VOL_MASK);
		wm8994_write(codec, WM8994_INPUT_MIXER_3, val);


		DEBUG_LOG("AT Command Codec Loopback\n");
	}

}

void wm8994_record_bluetooth(struct snd_soc_codec *codec)
{
#ifdef WM8994_VOIP_BT_NREC
	struct wm8994_priv *wm8994 = codec->drvdata;
#endif

	DEBUG_LOG("BT Record Path for Voice Command\n");

	wm8994_write(codec, 0x39, 0x006c);
	wm8994_write(codec, 0x01, 0x0003);
	msleep(50);

	wm8994_write(codec, 0x102, 0x0003);
	wm8994_write(codec, 0x817, 0x0000);
	wm8994_write(codec, 0x102, 0x0000);

	wm8994_write(codec, WM8994_GPIO_3, 0x0100);
	wm8994_write(codec, WM8994_GPIO_4, 0x0100);
	wm8994_write(codec, WM8994_GPIO_5, 0x8100);
	wm8994_write(codec, WM8994_GPIO_6, 0xA101);
	wm8994_write(codec, WM8994_GPIO_7, 0x0100);
	wm8994_write(codec, WM8994_GPIO_8, 0x8100);
	wm8994_write(codec, WM8994_GPIO_9, 0x0100);
	wm8994_write(codec, WM8994_GPIO_10, 0x0100);
	wm8994_write(codec, WM8994_GPIO_11, 0x0100);


	/* Enable Dac1 and DAC2 and the Timeslot0 for AIF1 */
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_4, 0x3303);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_5, 0x330C);

	/* Digital Path Configuration */
	wm8994_write(codec, WM8994_FLL2_CONTROL_2, 0x2D00);
	wm8994_write(codec, WM8994_FLL2_CONTROL_3, 0x583C);
	wm8994_write(codec, WM8994_FLL2_CONTROL_4, 0x0100);
	wm8994_write(codec, WM8994_FLL2_CONTROL_5, 0x0C80);
	wm8994_write(codec, WM8994_FLL2_CONTROL_1, 0x0005);

	wm8994_write(codec, 0x208, 0x000E);

	wm8994_write(codec, WM8994_AIF2_RATE, 0x3 << WM8994_AIF2CLK_RATE_SHIFT);
	wm8994_write(codec, WM8994_AIF2_CONTROL_1, 0x4010);
	wm8994_write(codec, WM8994_AIF2_CONTROL_2, 0x4000); /* AIF2_LOOPBACK */
	wm8994_write(codec, 0x310, 0x4010);
	wm8994_write(codec, 0x312, 0x7000);
	wm8994_write(codec, WM8994_AIF2_BCLK, 0x70);
	wm8994_write(codec, WM8994_AIF2_CONTROL_2, 0x0000);

	/* ADC oversampling disabled, DAC oversampling disabled */
	wm8994_write(codec, 0x620, 0x0000);

	wm8994_write(codec, 0x200, 0x0001); /* AIF1 Enable, AIF1CLK = MCLK1 */
	wm8994_write(codec, 0x204, 0x0019); /* AIF2 Enable, AIF2CLK = FLL2 */

	/* Tx Path Configuration */
	wm8994_write(codec, 0x606, 0x0001);
	wm8994_write(codec, 0x607, 0x0001);
	wm8994_write(codec, 0x410, 0x3800);

	/* Rx Path Configuration */
	wm8994_write(codec, 0x601, 0x0005);
	wm8994_write(codec, 0x602, 0x0005);
	wm8994_write(codec, 0x603, 0x018c);
	wm8994_write(codec, 0x604, 0x0001);
	wm8994_write(codec, 0x605, 0x0001);

	wm8994_write(codec, WM8994_DAC2_LEFT_VOLUME, 0x1C0);
	wm8994_write(codec, WM8994_DAC2_RIGHT_VOLUME, 0x1C0);

	wm8994_write(codec, 0x700, 0xA101);
	wm8994_write(codec, 0x702, 0x0100);
	wm8994_write(codec, 0x703, 0x0100);
	wm8994_write(codec, 0x704, 0x8100);
	wm8994_write(codec, 0x705, 0xA101);
	wm8994_write(codec, 0x706, 0x0100);
	wm8994_write(codec, 0x707, 0x8100);
	wm8994_write(codec, 0x708, 0x0100);
	wm8994_write(codec, 0x709, 0x0100);
	wm8994_write(codec, 0x70A, 0x0100);

	wm8994_write(codec, 0x610, 0x00C0);
	wm8994_write(codec, 0x611, 0x01C0);
	wm8994_write(codec, 0x612, 0x00C0);
	wm8994_write(codec, 0x613, 0x01C0);
	wm8994_write(codec, 0x520, 0x0000);
	wm8994_write(codec, 0x420, 0x0000);
	wm8994_write(codec, 0x502, 0x00A4);
	wm8994_write(codec, 0x503, 0x01A4);

#ifdef WM8994_VOIP_BT_NREC
	if (wm8994->voip_call_active){
		if(wm8994->voip_bt_nrec_state == VOIP_BT_NREC_OFF){
			DEBUG_LOG("BT Playback Path for SCO : VOIP_BT_NREC_OFF\n");
			wm8994_write(codec, 0x500, 0x01C8);
			wm8994_write(codec, 0x402, 0x01C0);
			wm8994_write(codec, 0x502, 0x01B0);
			wm8994_write(codec, 0x400, 0x01EF);
			//wm8994_write(codec, 0x480, 0x6318);
			//wm8994_write(codec, 0x481, 0x6300);
			wm8994_write(codec, 0x580, 0x6318);
			wm8994_write(codec, 0x581, 0x6300);

			wm8994_write(codec, 0x444, 0x0120);
			wm8994_write(codec, 0x443, 0x018C);
			wm8994_write(codec, 0x442, 0x0850);
			wm8994_write(codec, 0x441, 0x0645);
			wm8994_write(codec, 0x440, 0x019C);
			wm8994_write(codec, 0x480, 0x6318);
			wm8994_write(codec, 0x481, 0x6300);
		}else if(wm8994->voip_bt_nrec_state == VOIP_BT_NREC_ON){
			DEBUG_LOG("BT Playback Path for SCO : VOIP_BT_NREC_ON\n");
			wm8994_write(codec, 0x500, 0x01D4);
			wm8994_write(codec, 0x402, 0x01C0);
			wm8994_write(codec, 0x502, 0x01B0);
			wm8994_write(codec, 0x400, 0x01D9);
			wm8994_write(codec, 0x480, 0x6311);
			wm8994_write(codec, 0x481, 0x6B00);
			//wm8994_write(codec, 0x580, 0x6318);
			//wm8994_write(codec, 0x581, 0x6300);	

			wm8994_write(codec, 0x444, 0x0000);
			wm8994_write(codec, 0x443, 0x018C);
			wm8994_write(codec, 0x442, 0x0450);
			wm8994_write(codec, 0x441, 0x0645);
			wm8994_write(codec, 0x440, 0x019A);
			wm8994_write(codec, 0x580, 0x2193);
			wm8994_write(codec, 0x581, 0x8300);			
		}
	}

#endif		
}

void wm8994_set_playback_receiver(struct snd_soc_codec *codec)
{
	u16 val;

	DEBUG_LOG("");

	val = wm8994_read(codec, WM8994_LEFT_OPGA_VOLUME);
	val &= ~(WM8994_MIXOUTL_MUTE_N_MASK);
	val |= (WM8994_MIXOUTL_MUTE_N);
	wm8994_write(codec, WM8994_LEFT_OPGA_VOLUME, val);

	val = wm8994_read(codec, WM8994_RIGHT_OPGA_VOLUME);
	val &= ~(WM8994_MIXOUTR_MUTE_N_MASK);
	val |= (WM8994_MIXOUTR_MUTE_N);
	wm8994_write(codec, WM8994_RIGHT_OPGA_VOLUME, val);

	val = wm8994_read(codec, WM8994_HPOUT2_VOLUME);
	val &= ~(WM8994_HPOUT2_MUTE_MASK);
	wm8994_write(codec, WM8994_HPOUT2_VOLUME, val);

	/* Unmute DAC1 left */
	val = wm8994_read(codec, WM8994_DAC1_LEFT_VOLUME);
	val &= ~(WM8994_DAC1L_MUTE_MASK);
	wm8994_write(codec, WM8994_DAC1_LEFT_VOLUME, val);

	/* Unmute and volume ctrl RightDAC */
	val = wm8994_read(codec, WM8994_DAC1_RIGHT_VOLUME);
	val &= ~(WM8994_DAC1R_MUTE_MASK);
	wm8994_write(codec, WM8994_DAC1_RIGHT_VOLUME, val);

	val = wm8994_read(codec, WM8994_OUTPUT_MIXER_1);
	val &= ~(WM8994_DAC1L_TO_MIXOUTL_MASK);
	val |= (WM8994_DAC1L_TO_MIXOUTL);
	wm8994_write(codec, WM8994_OUTPUT_MIXER_1, val);

	val = wm8994_read(codec, WM8994_OUTPUT_MIXER_2);
	val &= ~(WM8994_DAC1R_TO_MIXOUTR_MASK);
	val |= (WM8994_DAC1R_TO_MIXOUTR);
	wm8994_write(codec, WM8994_OUTPUT_MIXER_2, val);

	val = wm8994_read(codec, WM8994_HPOUT2_MIXER);
	val &= ~(WM8994_MIXOUTLVOL_TO_HPOUT2_MASK |
			WM8994_MIXOUTRVOL_TO_HPOUT2_MASK);
	val |= (WM8994_MIXOUTRVOL_TO_HPOUT2 | WM8994_MIXOUTLVOL_TO_HPOUT2);
	wm8994_write(codec, WM8994_HPOUT2_MIXER, val);

	wm8994_set_codec_gain(codec, PLAYBACK_MODE, PLAYBACK_RCV);

	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_5);
	val &= ~(WM8994_DAC1R_ENA_MASK | WM8994_DAC1L_ENA_MASK |
			WM8994_AIF1DAC1R_ENA_MASK | WM8994_AIF1DAC1L_ENA_MASK);
	val |= (WM8994_AIF1DAC1L_ENA | WM8994_AIF1DAC1R_ENA |
			WM8994_DAC1L_ENA | WM8994_DAC1R_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_5, val);

	val = wm8994_read(codec, WM8994_DAC1_LEFT_MIXER_ROUTING);
	val &= ~(WM8994_AIF1DAC1L_TO_DAC1L_MASK);
	val |= (WM8994_AIF1DAC1L_TO_DAC1L);
	wm8994_write(codec, WM8994_DAC1_LEFT_MIXER_ROUTING, val);

	val = wm8994_read(codec, WM8994_DAC1_RIGHT_MIXER_ROUTING);
	val &= ~(WM8994_AIF1DAC1R_TO_DAC1R_MASK);
	val |= (WM8994_AIF1DAC1R_TO_DAC1R);
	wm8994_write(codec, WM8994_DAC1_RIGHT_MIXER_ROUTING, val);

	val = wm8994_read(codec, WM8994_CLOCKING_1);
	val &= ~(WM8994_DSP_FS1CLK_ENA_MASK | WM8994_DSP_FSINTCLK_ENA_MASK);
	val |= (WM8994_DSP_FS1CLK_ENA | WM8994_DSP_FSINTCLK_ENA);
	wm8994_write(codec, WM8994_CLOCKING_1, val);

	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_3);
	val &= ~(WM8994_MIXOUTLVOL_ENA_MASK | WM8994_MIXOUTRVOL_ENA_MASK |
	      WM8994_MIXOUTL_ENA_MASK | WM8994_MIXOUTR_ENA_MASK);
	val |= (WM8994_MIXOUTL_ENA | WM8994_MIXOUTR_ENA |
			WM8994_MIXOUTRVOL_ENA | WM8994_MIXOUTLVOL_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_3, val);

	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_1);
	val &= ~(WM8994_BIAS_ENA_MASK | WM8994_VMID_SEL_MASK |
			WM8994_HPOUT2_ENA_MASK | WM8994_HPOUT1L_ENA_MASK |
			WM8994_HPOUT1R_ENA_MASK | WM8994_SPKOUTL_ENA_MASK);
	val |= (WM8994_BIAS_ENA | WM8994_VMID_SEL_NORMAL | WM8994_HPOUT2_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_1, val);

	val = wm8994_read(codec, WM8994_AIF1_DAC1_FILTERS_1);
	val &= ~(WM8994_AIF1DAC1_MUTE_MASK | WM8994_AIF1DAC1_MONO_MASK);
	val |= (WM8994_AIF1DAC1_UNMUTE | WM8994_AIF1DAC1_MONO);
	wm8994_write(codec, WM8994_AIF1_DAC1_FILTERS_1, val);

}

void wm8994_set_playback_headset(struct snd_soc_codec *codec)
{
	struct wm8994_priv *wm8994 = codec->drvdata;

	u16 val;

	u16 testreturn1 = 0;
	u16 testreturn2 = 0;
	u16 testlow1 = 0;
	u16 testhigh1 = 0;
	u8 testlow = 0;
	u8 testhigh = 0;

	DEBUG_LOG("");

	/* Enable the Timeslot0 to DAC1L */
	val = wm8994_read(codec, WM8994_DAC1_LEFT_MIXER_ROUTING);
	val &= ~(WM8994_AIF1DAC1L_TO_DAC1L_MASK);
	val |= WM8994_AIF1DAC1L_TO_DAC1L;
	wm8994_write(codec, WM8994_DAC1_LEFT_MIXER_ROUTING, val);

	/* Enable the Timeslot0 to DAC1R */
	val = wm8994_read(codec, WM8994_DAC1_RIGHT_MIXER_ROUTING);
	val &= ~(WM8994_AIF1DAC1R_TO_DAC1R_MASK);
	val |= WM8994_AIF1DAC1R_TO_DAC1R;
	wm8994_write(codec, WM8994_DAC1_RIGHT_MIXER_ROUTING, val);

	val = wm8994_read(codec, 0x102);
	val &= ~(0x0003);
	val = 0x0003;
	wm8994_write(codec, 0x102, val);

	val = wm8994_read(codec, 0x56);
	val &= ~(0x0003);
	val = 0x0003;
	wm8994_write(codec, 0x56, val);

	val = wm8994_read(codec, 0x102);
	val &= ~(0x0000);
	val = 0x0000;
	wm8994_write(codec, 0x102, val);

	val = wm8994_read(codec, WM8994_CLASS_W_1);
	val &= ~(0x0005);
	val |= 0x0005;
	wm8994_write(codec, WM8994_CLASS_W_1, val);

	val = wm8994_read(codec, WM8994_LEFT_OUTPUT_VOLUME);
	val &= ~(WM8994_HPOUT1L_MUTE_N_MASK);
	val |= (WM8994_HPOUT1L_MUTE_N);
	wm8994_write(codec, WM8994_LEFT_OUTPUT_VOLUME, val);

	val = wm8994_read(codec, WM8994_RIGHT_OUTPUT_VOLUME);
	val &= ~(WM8994_HPOUT1R_MUTE_N_MASK);
	val |= (WM8994_HPOUT1R_MUTE_N);
	wm8994_write(codec, WM8994_RIGHT_OUTPUT_VOLUME, val);

	val = wm8994_read(codec, WM8994_LEFT_OPGA_VOLUME);
	val &= ~(WM8994_MIXOUTL_MUTE_N_MASK);
	val |= (WM8994_MIXOUTL_MUTE_N);
	wm8994_write(codec, WM8994_LEFT_OPGA_VOLUME, val);

	val = wm8994_read(codec, WM8994_RIGHT_OPGA_VOLUME);
	val &= ~(WM8994_MIXOUTR_MUTE_N_MASK);
	val |= (WM8994_MIXOUTR_MUTE_N);
	wm8994_write(codec, WM8994_RIGHT_OPGA_VOLUME, val);

#if defined(CONFIG_TARGET_LOCALE_KOR)
	if (wm8994->voip_call_active)
		wm8994_set_codec_gain(codec, VOIPCALL_MODE, VOIPCALL_HP);
#else
	if (wm8994->voip_call_active == VOIP_ON)
		wm8994_set_codec_gain(codec, VOIPCALL_MODE, VOIPCALL_HP);
	else if (wm8994->voip_call_active == VOIP_ON_OTHER)
		wm8994_set_codec_gain(codec, VOIPCALL_MODE, VOIPCALL_HP_OTHER);
#endif
	else if (wm8994->ringtone_active)
		wm8994_set_codec_gain(codec, PLAYBACK_MODE, PLAYBACK_RING_HP);
	else
		wm8994_set_codec_gain(codec, PLAYBACK_MODE, PLAYBACK_HP);

	val = wm8994_read(codec, WM8994_DC_SERVO_2);
	val &= ~(0x03E0);
	val = 0x03E0;
	wm8994_write(codec, WM8994_DC_SERVO_2, val);

	/* Enable vmid,bias, hp left and right */
	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_1);
	val &= ~(WM8994_BIAS_ENA_MASK | WM8994_VMID_SEL_MASK |
		WM8994_HPOUT1L_ENA_MASK | WM8994_HPOUT1R_ENA_MASK |
		WM8994_SPKOUTR_ENA_MASK | WM8994_SPKOUTL_ENA_MASK);
	val |= (WM8994_BIAS_ENA | WM8994_VMID_SEL_NORMAL |
		WM8994_HPOUT1R_ENA | WM8994_HPOUT1L_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_1, val);

	val = wm8994_read(codec, WM8994_ANALOGUE_HP_1);
	val &= ~(0x0022);
	val = 0x0022;
	wm8994_write(codec, WM8994_ANALOGUE_HP_1, val);

	/* Enable Charge Pump */
	/* this is from wolfson */
	val = wm8994_read(codec, WM8994_CHARGE_PUMP_1);
	val &= ~WM8994_CP_ENA_MASK ;
	val |= WM8994_CP_ENA | WM8994_CP_ENA_DEFAULT;
	wm8994_write(codec, WM8994_CHARGE_PUMP_1, val);

	msleep(5);

	/* Enable Dac1 and DAC2 and the Timeslot0 for AIF1 */
	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_5);
	val &= ~(WM8994_DAC1R_ENA_MASK | WM8994_DAC1L_ENA_MASK |
		WM8994_AIF1DAC1R_ENA_MASK | WM8994_AIF1DAC1L_ENA_MASK);
	val |= (WM8994_AIF1DAC1L_ENA | WM8994_AIF1DAC1R_ENA |
		WM8994_DAC1L_ENA | WM8994_DAC1R_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_5, val);

	/* Enable DAC1L to HPOUT1L path */
	val = wm8994_read(codec, WM8994_OUTPUT_MIXER_1);
	val &=  ~(WM8994_DAC1L_TO_HPOUT1L_MASK | WM8994_DAC1L_TO_MIXOUTL_MASK);
	val |= WM8994_DAC1L_TO_MIXOUTL;
	wm8994_write(codec, WM8994_OUTPUT_MIXER_1, val);

	/* Enable DAC1R to HPOUT1R path */
	val = wm8994_read(codec, WM8994_OUTPUT_MIXER_2);
	val &= ~(WM8994_DAC1R_TO_HPOUT1R_MASK | WM8994_DAC1R_TO_MIXOUTR_MASK);
	val |= WM8994_DAC1R_TO_MIXOUTR;
	wm8994_write(codec, WM8994_OUTPUT_MIXER_2, val);

	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_3);
	val &= ~(WM8994_MIXOUTLVOL_ENA_MASK | WM8994_MIXOUTRVOL_ENA_MASK |
		WM8994_MIXOUTL_ENA_MASK | WM8994_MIXOUTR_ENA_MASK |
		WM8994_SPKRVOL_ENA_MASK | WM8994_SPKLVOL_ENA_MASK);
	val |= (WM8994_MIXOUTLVOL_ENA | WM8994_MIXOUTRVOL_ENA |
		WM8994_MIXOUTL_ENA | WM8994_MIXOUTR_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_3, 0x0030);

	if (!wm8994->dc_servo[DCS_MEDIA]) {
		wait_for_dc_servo(codec,
				  WM8994_DCS_TRIG_SERIES_0 |
				  WM8994_DCS_TRIG_SERIES_1);
		testreturn1 = wm8994_read(codec, WM8994_DC_SERVO_4);

		testlow = (signed char)(testreturn1 & 0xff);
		testhigh = (signed char)((testreturn1>>8) & 0xff);

		testlow1 = ((signed short)(testlow-7)) & 0x00ff;
		testhigh1 = (((signed short)(testhigh-7)<<8) & 0xff00);
		testreturn2 = testlow1|testhigh1;
	} else {
		testreturn2 = wm8994->dc_servo[DCS_MEDIA];
	}

	wm8994_write(codec, WM8994_DC_SERVO_4, testreturn2);
	wm8994->dc_servo[DCS_MEDIA] = testreturn2;

	wait_for_dc_servo(codec,
			  WM8994_DCS_TRIG_DAC_WR_0 | WM8994_DCS_TRIG_DAC_WR_1);
	/* Intermediate HP settings */
	val = wm8994_read(codec, WM8994_ANALOGUE_HP_1);
	val &= ~(WM8994_HPOUT1R_DLY_MASK | WM8994_HPOUT1R_OUTP_MASK |
		WM8994_HPOUT1R_RMV_SHORT_MASK | WM8994_HPOUT1L_DLY_MASK |
		WM8994_HPOUT1L_OUTP_MASK | WM8994_HPOUT1L_RMV_SHORT_MASK);
	val = (WM8994_HPOUT1L_RMV_SHORT | WM8994_HPOUT1L_OUTP|
		WM8994_HPOUT1L_DLY | WM8994_HPOUT1R_RMV_SHORT |
		WM8994_HPOUT1R_OUTP | WM8994_HPOUT1R_DLY);
	wm8994_write(codec, WM8994_ANALOGUE_HP_1, val);

	/* Unmute DAC1 left */
	val = wm8994_read(codec, WM8994_DAC1_LEFT_VOLUME);
	val &= ~(WM8994_DAC1L_MUTE_MASK);
	wm8994_write(codec, WM8994_DAC1_LEFT_VOLUME, val);

	/* Unmute and volume ctrl RightDAC */
	val = wm8994_read(codec, WM8994_DAC1_RIGHT_VOLUME);
	val &= ~(WM8994_DAC1R_MUTE_MASK);
	wm8994_write(codec, WM8994_DAC1_RIGHT_VOLUME, val);

	/* Unmute the AF1DAC1 */
	val = wm8994_read(codec, WM8994_AIF1_DAC1_FILTERS_1);
	val &= ~(WM8994_AIF1DAC1_MUTE_MASK | WM8994_AIF1DAC1_MONO_MASK);
	val |= WM8994_AIF1DAC1_UNMUTE;
	wm8994_write(codec, WM8994_AIF1_DAC1_FILTERS_1, val);

}

void wm8994_set_playback_speaker(struct snd_soc_codec *codec)
{
	struct wm8994_priv *wm8994 = codec->drvdata;

	u16 val;

	DEBUG_LOG("");

	wm8994_filter_off(codec);

	/* Disable end point for preventing pop up noise.*/
	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_1);
	val &= ~(WM8994_SPKOUTL_ENA_MASK);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_1, val);

	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_3);
	val &= ~(WM8994_MIXOUTLVOL_ENA_MASK | WM8994_MIXOUTRVOL_ENA_MASK |
		WM8994_MIXOUTL_ENA_MASK | WM8994_MIXOUTR_ENA_MASK |
		WM8994_SPKLVOL_ENA_MASK | WM8994_SPKRVOL_ENA_MASK);
	val |= (WM8994_SPKLVOL_ENA | WM8994_SPKRVOL_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_3, val);

	/* Speaker Volume Control */
	/* Unmute the SPKMIXVOLUME */
	val = wm8994_read(codec, WM8994_SPEAKER_VOLUME_LEFT);
	val &= ~(WM8994_SPKOUTL_MUTE_N_MASK);
	val |= (WM8994_SPKOUTL_MUTE_N);
	wm8994_write(codec, WM8994_SPEAKER_VOLUME_LEFT, val);

	val = wm8994_read(codec, WM8994_SPEAKER_VOLUME_RIGHT);
	val &= ~(WM8994_SPKOUTR_MUTE_N_MASK);
	val |= (WM8994_SPKOUTR_MUTE_N);
	wm8994_write(codec, WM8994_SPEAKER_VOLUME_RIGHT, val);

	/* Unmute DAC1 left */
	val = wm8994_read(codec, WM8994_DAC1_LEFT_VOLUME);
	val &= ~(WM8994_DAC1L_MUTE_MASK);
	wm8994_write(codec, WM8994_DAC1_LEFT_VOLUME, val);

	/* Unmute and volume ctrl RightDAC */
	val = wm8994_read(codec, WM8994_DAC1_RIGHT_VOLUME);
	val &= ~(WM8994_DAC1R_MUTE_MASK);
	wm8994_write(codec, WM8994_DAC1_RIGHT_VOLUME, val);

	val = wm8994_read(codec, WM8994_SPKOUT_MIXERS);
	val &= ~(WM8994_SPKMIXL_TO_SPKOUTL_MASK |
		 WM8994_SPKMIXR_TO_SPKOUTR_MASK);
	val |= (WM8994_SPKMIXL_TO_SPKOUTL | WM8994_SPKMIXR_TO_SPKOUTR);
	wm8994_write(codec, WM8994_SPKOUT_MIXERS, val);

	/* Unmute the DAC path */
	val = wm8994_read(codec, WM8994_SPEAKER_MIXER);
	val &= ~(WM8994_DAC1L_TO_SPKMIXL_MASK | WM8994_DAC1R_TO_SPKMIXR_MASK);
	val |= (WM8994_DAC1L_TO_SPKMIXL | WM8994_DAC1R_TO_SPKMIXR) ;
	wm8994_write(codec, WM8994_SPEAKER_MIXER, val);

	/* Eable DAC1 Left and timeslot left */
	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_5);
	val &= ~(WM8994_DAC1L_ENA_MASK | WM8994_AIF1DAC1L_ENA_MASK |
		WM8994_DAC1R_ENA_MASK | WM8994_AIF1DAC1R_ENA_MASK);
	val |= (WM8994_AIF1DAC1L_ENA | WM8994_DAC1L_ENA |
		WM8994_AIF1DAC1R_ENA | WM8994_DAC1R_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_5, val);

#if defined(CONFIG_TARGET_LOCALE_KOR)
	if (wm8994->voip_call_active)
		wm8994_set_codec_gain(codec, VOIPCALL_MODE, VOIPCALL_SPK);
#else
	if (wm8994->voip_call_active == VOIP_ON)
		wm8994_set_codec_gain(codec, VOIPCALL_MODE, VOIPCALL_SPK);
	else if (wm8994->voip_call_active == VOIP_ON_OTHER)
		wm8994_set_codec_gain(codec, VOIPCALL_MODE, VOIPCALL_SPK_OTHER);
#endif
	else if (wm8994->ringtone_active)
		wm8994_set_codec_gain(codec, PLAYBACK_MODE, PLAYBACK_RING_SPK);
	else
		wm8994_set_codec_gain(codec, PLAYBACK_MODE, PLAYBACK_SPK);

	/* enable timeslot0 to left dac */
	val = wm8994_read(codec, WM8994_DAC1_LEFT_MIXER_ROUTING);
	val &= ~(WM8994_AIF1DAC1L_TO_DAC1L_MASK);
	val |= WM8994_AIF1DAC1L_TO_DAC1L;
	wm8994_write(codec, WM8994_DAC1_LEFT_MIXER_ROUTING, val);
	/* enable timeslot0 to Right dac */
	val = wm8994_read(codec, WM8994_DAC1_RIGHT_MIXER_ROUTING);
	val &= ~(WM8994_AIF1DAC1R_TO_DAC1R_MASK);
	val |= WM8994_AIF1DAC1R_TO_DAC1R;
	wm8994_write(codec, WM8994_DAC1_RIGHT_MIXER_ROUTING, val);

	/* Enbale bias,vmid and Left speaker */
	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_1);
	val &= ~(WM8994_BIAS_ENA_MASK | WM8994_VMID_SEL_MASK |
		WM8994_HPOUT1L_ENA_MASK | WM8994_HPOUT1R_ENA_MASK |
		WM8994_SPKOUTL_ENA_MASK | WM8994_SPKOUTR_ENA_MASK);

	if (wm8994->loopback_path_control == spk) {
		val |= (WM8994_BIAS_ENA | WM8994_VMID_SEL_NORMAL |
			WM8994_SPKOUTR_ENA);
	} else {
		val |= (WM8994_BIAS_ENA | WM8994_VMID_SEL_NORMAL |
			WM8994_SPKOUTL_ENA | WM8994_SPKOUTR_ENA);
	}
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_1, val);

#ifdef WM8994_MUTE_STATE
	if (wm8994->mute_state == RX_MUTE) {
		DEBUG_LOG("wm8994->mute_state == RX_MUTE");
		wm8994_write(codec, 0x3000, 0x15);
		wm8994_write(codec, 0x3001, 0x0);
		wm8994_write(codec, 0x3002, 0x6);
		wm8994_write(codec, 0x3003, 0x00d);

		wm8994_write(codec, 0x3004, 0x420);
		wm8994_write(codec, 0x3005, 0x0);
		wm8994_write(codec, 0x3006, 0x9);
		wm8994_write(codec, 0x3007, 0x10f);
		wm8994_write(codec, 0x110, 0x8100);
		wm8994->mute_state = MUTE_OFF;
	} else
#endif
	{
	/* Unmute */
	val = wm8994_read(codec, WM8994_AIF1_DAC1_FILTERS_1);
	val &= ~(WM8994_AIF1DAC1_MUTE_MASK | WM8994_AIF1DAC1_MONO_MASK);
	val |= WM8994_AIF1DAC1_UNMUTE;
	wm8994_write(codec, WM8994_AIF1_DAC1_FILTERS_1, val);
	}

}

void wm8994_set_playback_speaker_headset(struct snd_soc_codec *codec)
{
	struct wm8994_priv *wm8994 = codec->drvdata;

	u16 val;

	u16 nreadservo4val = 0;
	u16 ncompensationresult = 0;
	u16 ncompensationresultlow = 0;
	u16 ncompensationresulthigh = 0;
	u8  nservo4low = 0;
	u8  nservo4high = 0;

	DEBUG_LOG("");

	/* Enable the Timeslot0 to DAC1L */
	val = wm8994_read(codec, WM8994_DAC1_LEFT_MIXER_ROUTING);
	val &= ~(WM8994_AIF1DAC1L_TO_DAC1L_MASK);
	val |= WM8994_AIF1DAC1L_TO_DAC1L;
	wm8994_write(codec, WM8994_DAC1_LEFT_MIXER_ROUTING, val);

	/* Enable the Timeslot0 to DAC1R */
	val = wm8994_read(codec, WM8994_DAC1_RIGHT_MIXER_ROUTING);
	val &= ~(WM8994_AIF1DAC1R_TO_DAC1R_MASK);
	val |= WM8994_AIF1DAC1R_TO_DAC1R;
	wm8994_write(codec, WM8994_DAC1_RIGHT_MIXER_ROUTING, val);

	/* Speaker Volume Control */
	val = wm8994_read(codec, WM8994_SPEAKER_VOLUME_LEFT);
	val &= ~(WM8994_SPKOUTL_MUTE_N_MASK);
	val |= (WM8994_SPKOUTL_MUTE_N);
	wm8994_write(codec, WM8994_SPEAKER_VOLUME_LEFT, val);

	val = wm8994_read(codec, WM8994_SPEAKER_VOLUME_RIGHT);
	val &= ~(WM8994_SPKOUTR_MUTE_N_MASK);
	val |= (WM8994_SPKOUTR_MUTE_N);
	wm8994_write(codec, WM8994_SPEAKER_VOLUME_RIGHT, val);

	val = wm8994_read(codec, WM8994_SPKOUT_MIXERS);
	val &= ~(WM8994_SPKMIXL_TO_SPKOUTL_MASK |
		WM8994_SPKMIXR_TO_SPKOUTR_MASK);
	val |= (WM8994_SPKMIXL_TO_SPKOUTL | WM8994_SPKMIXR_TO_SPKOUTR);
	wm8994_write(codec, WM8994_SPKOUT_MIXERS, val);

	/* Unmute the DAC path */
	val = wm8994_read(codec, WM8994_SPEAKER_MIXER);
	val &= ~(WM8994_DAC1L_TO_SPKMIXL_MASK | WM8994_DAC1R_TO_SPKMIXR_MASK);
	val |= (WM8994_DAC1L_TO_SPKMIXL | WM8994_DAC1R_TO_SPKMIXR);
	wm8994_write(codec, WM8994_SPEAKER_MIXER, val);

	/* Configuring the Digital Paths */
	val = wm8994_read(codec, 0x102);
	val &= ~(0x0003);
	val = 0x0003;
	wm8994_write(codec, 0x102, val);

	val = wm8994_read(codec, 0x56);
	val &= ~(0x0003);
	val = 0x0003;
	wm8994_write(codec, 0x56, val);

	val = wm8994_read(codec, 0x102);
	val &= ~(0x0000);
	val = 0x0000;
	wm8994_write(codec, 0x102, val);

	val = wm8994_read(codec, WM8994_CLASS_W_1);
	val &= ~(0x0005);
	val = 0x0005;
	wm8994_write(codec, WM8994_CLASS_W_1, val);

	val = wm8994_read(codec, WM8994_LEFT_OUTPUT_VOLUME);
	val &= ~(WM8994_HPOUT1L_MUTE_N_MASK);
	val |= (WM8994_HPOUT1L_MUTE_N);
	wm8994_write(codec, WM8994_LEFT_OUTPUT_VOLUME, val);

	val = wm8994_read(codec, WM8994_RIGHT_OUTPUT_VOLUME);
	val &= ~(WM8994_HPOUT1R_MUTE_N_MASK);
	val |= (WM8994_HPOUT1R_MUTE_N);
	wm8994_write(codec, WM8994_RIGHT_OUTPUT_VOLUME, val);

	/* DC Servo Series Count */
	val = 0x03E0;
	wm8994_write(codec, WM8994_DC_SERVO_2, val);

	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_1);
	val &= ~(WM8994_BIAS_ENA_MASK | WM8994_VMID_SEL_MASK |
		WM8994_HPOUT1L_ENA_MASK | WM8994_HPOUT1R_ENA_MASK |
		WM8994_SPKOUTL_ENA_MASK | WM8994_SPKOUTR_ENA_MASK);
	val |= (WM8994_BIAS_ENA | WM8994_VMID_SEL_NORMAL |
		WM8994_HPOUT1R_ENA | WM8994_HPOUT1L_ENA |
		WM8994_SPKOUTL_ENA | WM8994_SPKOUTR_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_1, val);

	val = (WM8994_HPOUT1L_DLY | WM8994_HPOUT1R_DLY);
	wm8994_write(codec, WM8994_ANALOGUE_HP_1, val);

	/* Enable Charge Pump */
	/* this is from wolfson */
	val = wm8994_read(codec, WM8994_CHARGE_PUMP_1);
	val &= ~WM8994_CP_ENA_MASK ;
	val |= WM8994_CP_ENA | WM8994_CP_ENA_DEFAULT;
	wm8994_write(codec, WM8994_CHARGE_PUMP_1, val);

	msleep(5);

	/* Enable DAC1 and DAC2 and the Timeslot0 for AIF1 */
	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_5);
	val &= ~(WM8994_DAC1R_ENA_MASK | WM8994_DAC1L_ENA_MASK |
		WM8994_AIF1DAC1R_ENA_MASK | WM8994_AIF1DAC1L_ENA_MASK);
	val |= (WM8994_AIF1DAC1L_ENA | WM8994_AIF1DAC1R_ENA |
		WM8994_DAC1L_ENA | WM8994_DAC1R_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_5, val);

	/* Enbale DAC1L to HPOUT1L path */
	val = wm8994_read(codec, WM8994_OUTPUT_MIXER_1);
	val &=  ~(WM8994_DAC1L_TO_HPOUT1L_MASK | WM8994_DAC1L_TO_MIXOUTL_MASK);
	val |=  WM8994_DAC1L_TO_MIXOUTL;
	wm8994_write(codec, WM8994_OUTPUT_MIXER_1, val);

	/* Enbale DAC1R to HPOUT1R path */
	val = wm8994_read(codec, WM8994_OUTPUT_MIXER_2);
	val &= ~(WM8994_DAC1R_TO_HPOUT1R_MASK | WM8994_DAC1R_TO_MIXOUTR_MASK);
	val |= WM8994_DAC1R_TO_MIXOUTR;
	wm8994_write(codec, WM8994_OUTPUT_MIXER_2, val);

	/* Enbale bias,vmid, hp left and right and Left speaker */
	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_3);
	val &= ~(WM8994_MIXOUTLVOL_ENA_MASK | WM8994_MIXOUTRVOL_ENA_MASK |
		WM8994_MIXOUTL_ENA_MASK | WM8994_MIXOUTR_ENA_MASK |
		WM8994_SPKLVOL_ENA_MASK | WM8994_SPKRVOL_ENA_MASK);
	val |= (WM8994_MIXOUTL_ENA | WM8994_MIXOUTR_ENA |
		WM8994_SPKLVOL_ENA | WM8994_SPKRVOL_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_3, val);

	/* DC Servo */
	if (!wm8994->dc_servo[DCS_SPK_HP]) {
		wait_for_dc_servo(codec,
				  WM8994_DCS_TRIG_SERIES_0 |
				  WM8994_DCS_TRIG_SERIES_1);
		nreadservo4val = wm8994_read(codec, WM8994_DC_SERVO_4);
		nservo4low = (signed char)(nreadservo4val & 0xff);
		nservo4high = (signed char)((nreadservo4val>>8) & 0xff);

		ncompensationresultlow = ((signed short)nservo4low - 7)
			& 0x00ff;
		ncompensationresulthigh = ((signed short)(nservo4high - 7)<<8)
			& 0xff00;
		ncompensationresult = ncompensationresultlow |
			ncompensationresulthigh;
	} else {
		ncompensationresult = wm8994->dc_servo[DCS_SPK_HP];
	}

	wm8994_write(codec, WM8994_DC_SERVO_4, ncompensationresult);
	wm8994->dc_servo[DCS_SPK_HP] = ncompensationresult;

	wait_for_dc_servo(codec,
			  WM8994_DCS_TRIG_DAC_WR_1 | WM8994_DCS_TRIG_DAC_WR_0);

	val = wm8994_read(codec, WM8994_ANALOGUE_HP_1);
	val &= ~(WM8994_HPOUT1R_DLY_MASK | WM8994_HPOUT1R_OUTP_MASK |
		WM8994_HPOUT1R_RMV_SHORT_MASK |	WM8994_HPOUT1L_DLY_MASK |
		WM8994_HPOUT1L_OUTP_MASK | WM8994_HPOUT1L_RMV_SHORT_MASK);
	val |= (WM8994_HPOUT1L_RMV_SHORT | WM8994_HPOUT1L_OUTP |
		WM8994_HPOUT1L_DLY | WM8994_HPOUT1R_RMV_SHORT |
		WM8994_HPOUT1R_OUTP | WM8994_HPOUT1R_DLY);
	wm8994_write(codec, WM8994_ANALOGUE_HP_1, val);

	if (wm8994->ringtone_active)
		wm8994_set_codec_gain(codec, PLAYBACK_MODE,
				PLAYBACK_RING_SPK_HP);
	else
		wm8994_set_codec_gain(codec, PLAYBACK_MODE, PLAYBACK_SPK_HP);

	val = wm8994_read(codec, WM8994_DAC1_LEFT_VOLUME);
	val &= ~(WM8994_DAC1L_MUTE_MASK);
	wm8994_write(codec, WM8994_DAC1_LEFT_VOLUME, val);

	val = wm8994_read(codec, WM8994_DAC1_RIGHT_VOLUME);
	val &= ~(WM8994_DAC1R_MUTE_MASK);
	wm8994_write(codec, WM8994_DAC1_RIGHT_VOLUME, val);

	val = wm8994_read(codec, WM8994_AIF1_DAC1_FILTERS_1);
	val &= ~(WM8994_AIF1DAC1_MUTE_MASK | WM8994_AIF1DAC1_MONO_MASK);
	val |= WM8994_AIF1DAC1_UNMUTE;
	wm8994_write(codec, WM8994_AIF1_DAC1_FILTERS_1, val);

}

void wm8994_set_playback_bluetooth(struct snd_soc_codec *codec)
{
#ifdef WM8994_VOIP_BT_NREC
	struct wm8994_priv *wm8994 = codec->drvdata;
#endif

	DEBUG_LOG("BT Playback Path for SCO\n");

	wm8994_write(codec, 0x39, 0x006c);
	wm8994_write(codec, 0x01, 0x0003);
	msleep(50);

	wm8994_write(codec, 0x102, 0x0003);
	wm8994_write(codec, 0x817, 0x0000);
	wm8994_write(codec, 0x102, 0x0000);

	wm8994_write(codec, WM8994_GPIO_3, 0x0100);
	wm8994_write(codec, WM8994_GPIO_4, 0x0100);
	wm8994_write(codec, WM8994_GPIO_5, 0x8100);
	wm8994_write(codec, WM8994_GPIO_6, 0xA101);
	wm8994_write(codec, WM8994_GPIO_7, 0x0100);
	wm8994_write(codec, WM8994_GPIO_8, 0x8100);
	wm8994_write(codec, WM8994_GPIO_9, 0x0100);
	wm8994_write(codec, WM8994_GPIO_10, 0x0100);
	wm8994_write(codec, WM8994_GPIO_11, 0x0100);

	/* Enable Dac1 and DAC2 and the Timeslot0 for AIF1 */
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_4, 0x3303);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_5, 0x330C);

	/* Digital Path Configuration */
	wm8994_write(codec, WM8994_FLL2_CONTROL_2, 0x2D00);
	wm8994_write(codec, WM8994_FLL2_CONTROL_3, 0x583C);
	wm8994_write(codec, WM8994_FLL2_CONTROL_4, 0x0100);
	wm8994_write(codec, WM8994_FLL2_CONTROL_5, 0x0C80);
	wm8994_write(codec, WM8994_FLL2_CONTROL_1, 0x0005);

	wm8994_write(codec, 0x208, 0x000E);

	wm8994_write(codec, WM8994_AIF2_RATE, 0x3 << WM8994_AIF2CLK_RATE_SHIFT);
	wm8994_write(codec, WM8994_AIF2_CONTROL_1, 0x4010);
	wm8994_write(codec, WM8994_AIF2_CONTROL_2, 0x4000); /* AIF2_LOOPBACK */
	wm8994_write(codec, 0x310, 0x4010);
	wm8994_write(codec, 0x312, 0x7000);
	wm8994_write(codec, WM8994_AIF2_BCLK, 0x70);
	wm8994_write(codec, WM8994_AIF2_CONTROL_2, 0x0000);

	/* ADC oversampling disabled, DAC oversampling disabled */
	wm8994_write(codec, 0x620, 0x0000);

	wm8994_write(codec, 0x200, 0x0001); /* AIF1 Enable, AIF1CLK = MCLK1 */
	wm8994_write(codec, 0x204, 0x0019); /* AIF2 Enable, AIF2CLK = FLL2 */

	/* Tx Path Configuration */
	wm8994_write(codec, 0x606, 0x0001);
	wm8994_write(codec, 0x607, 0x0001);
	wm8994_write(codec, 0x410, 0x3800);

	/* Rx Path Configuration */
	wm8994_write(codec, 0x601, 0x0005);
	wm8994_write(codec, 0x602, 0x0005);
	wm8994_write(codec, 0x603, 0x018c);
	wm8994_write(codec, 0x604, 0x0001);
	wm8994_write(codec, 0x605, 0x0001);

	wm8994_write(codec, WM8994_DAC2_LEFT_VOLUME, 0x1C0);
	wm8994_write(codec, WM8994_DAC2_RIGHT_VOLUME, 0x1C0);

	wm8994_write(codec, 0x700, 0xA101);
	wm8994_write(codec, 0x702, 0x0100);
	wm8994_write(codec, 0x703, 0x0100);
	wm8994_write(codec, 0x704, 0x8100);
	wm8994_write(codec, 0x705, 0xA101);
	wm8994_write(codec, 0x706, 0x0100);
	wm8994_write(codec, 0x707, 0x8100);
	wm8994_write(codec, 0x708, 0x0100);
	wm8994_write(codec, 0x709, 0x0100);
	wm8994_write(codec, 0x70A, 0x0100);

	wm8994_write(codec, 0x610, 0x00C0);
	wm8994_write(codec, 0x611, 0x01C0);
	wm8994_write(codec, 0x612, 0x00C0);
	wm8994_write(codec, 0x613, 0x01C0);
	wm8994_write(codec, 0x520, 0x0000);
	wm8994_write(codec, 0x420, 0x0000);
	wm8994_write(codec, 0x502, 0x00A4);
	wm8994_write(codec, 0x503, 0x01A4);	

#ifdef WM8994_VOIP_BT_NREC
	if (wm8994->voip_call_active){
		if(wm8994->voip_bt_nrec_state == VOIP_BT_NREC_OFF){
			DEBUG_LOG("BT Playback Path for SCO : VOIP_BT_NREC_OFF\n");
			wm8994_write(codec, 0x500, 0x01C8);
			wm8994_write(codec, 0x402, 0x01C0);
			wm8994_write(codec, 0x502, 0x01B0);
			wm8994_write(codec, 0x400, 0x01EF);
			//wm8994_write(codec, 0x480, 0x6318);
			//wm8994_write(codec, 0x481, 0x6300);
			wm8994_write(codec, 0x580, 0x6318);
			wm8994_write(codec, 0x581, 0x6300);

			wm8994_write(codec, 0x444, 0x0120);
			wm8994_write(codec, 0x443, 0x018C);
			wm8994_write(codec, 0x442, 0x0850);
			wm8994_write(codec, 0x441, 0x0645);
			wm8994_write(codec, 0x440, 0x019C);
			wm8994_write(codec, 0x480, 0x6318);
			wm8994_write(codec, 0x481, 0x6300);
		}else if(wm8994->voip_bt_nrec_state == VOIP_BT_NREC_ON){
			DEBUG_LOG("BT Playback Path for SCO : VOIP_BT_NREC_ON\n");
			wm8994_write(codec, 0x500, 0x01D4);
			wm8994_write(codec, 0x402, 0x01C0);
			wm8994_write(codec, 0x502, 0x01B0);
			wm8994_write(codec, 0x400, 0x01D9);
			wm8994_write(codec, 0x480, 0x6311);
			wm8994_write(codec, 0x481, 0x6B00);
			//wm8994_write(codec, 0x580, 0x6318);
			//wm8994_write(codec, 0x581, 0x6300);	

			wm8994_write(codec, 0x444, 0x0000);
			wm8994_write(codec, 0x443, 0x018C);
			wm8994_write(codec, 0x442, 0x0450);
			wm8994_write(codec, 0x441, 0x0645);
			wm8994_write(codec, 0x440, 0x019A);
			wm8994_write(codec, 0x580, 0x2193);
			wm8994_write(codec, 0x581, 0x8300);			
		}
	}
#endif	
}

void wm8994_set_playback_extra_dock_speaker(struct snd_soc_codec *codec)
{

	u16 val;

	DEBUG_LOG("");

	wm8994_write(codec, WM8994_ANTIPOP_2, 0x0048);

	/*OUTPUT mute*/
	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_3);
	val &= ~(WM8994_LINEOUT2N_ENA_MASK | WM8994_LINEOUT2P_ENA_MASK);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_3, val);

	wm8994_set_codec_gain(codec, PLAYBACK_MODE, PLAYBACK_LINEOUT);

	val = wm8994_read(codec, WM8994_OUTPUT_MIXER_1);
	val &= ~(WM8994_DAC1L_TO_MIXOUTL_MASK);
	val |= (WM8994_DAC1L_TO_MIXOUTL);
	wm8994_write(codec, WM8994_OUTPUT_MIXER_1, val);

	val = wm8994_read(codec, WM8994_OUTPUT_MIXER_2);
	val &= ~(WM8994_DAC1R_TO_MIXOUTR_MASK);
	val |= (WM8994_DAC1R_TO_MIXOUTR);
	wm8994_write(codec, WM8994_OUTPUT_MIXER_2, val);


	val = wm8994_read(codec, WM8994_LINE_MIXER_2);
	val &= ~(WM8994_MIXOUTR_TO_LINEOUT2N_MASK |
		WM8994_MIXOUTL_TO_LINEOUT2N_MASK | WM8994_LINEOUT2_MODE_MASK |
		WM8994_MIXOUTR_TO_LINEOUT2P_MASK);
	val |= (WM8994_MIXOUTL_TO_LINEOUT2N | WM8994_LINEOUT2_MODE |
		WM8994_MIXOUTR_TO_LINEOUT2P);
	wm8994_write(codec, WM8994_LINE_MIXER_2, val);

	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_5);
	val &= ~(WM8994_DAC1R_ENA_MASK | WM8994_DAC1L_ENA_MASK |
		WM8994_AIF1DAC1R_ENA_MASK | WM8994_AIF1DAC1L_ENA_MASK);
	val |= (WM8994_AIF1DAC1L_ENA | WM8994_AIF1DAC1R_ENA |
		WM8994_DAC1L_ENA | WM8994_DAC1R_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_5, val);

	val = wm8994_read(codec, WM8994_AIF1_DAC1_FILTERS_1);
	val &= ~(WM8994_AIF1DAC1_MUTE_MASK | WM8994_AIF1DAC1_MONO_MASK);
	val |= (WM8994_AIF1DAC1_UNMUTE);
	wm8994_write(codec, WM8994_AIF1_DAC1_FILTERS_1, val);

	val = wm8994_read(codec, WM8994_LINE_OUTPUTS_VOLUME);
	val &= ~(WM8994_LINEOUT2N_MUTE_MASK | WM8994_LINEOUT2P_MUTE_MASK);
	wm8994_write(codec, WM8994_LINE_OUTPUTS_VOLUME, val);

	val = wm8994_read(codec, WM8994_DAC1_LEFT_MIXER_ROUTING);
	val &= ~(WM8994_AIF1DAC1L_TO_DAC1L_MASK);
	val |= (WM8994_AIF1DAC1L_TO_DAC1L);
	wm8994_write(codec, WM8994_DAC1_LEFT_MIXER_ROUTING, val);

	val = wm8994_read(codec, WM8994_DAC1_RIGHT_MIXER_ROUTING);
	val &= ~(WM8994_AIF1DAC1R_TO_DAC1R_MASK);
	val |= (WM8994_AIF1DAC1R_TO_DAC1R);
	wm8994_write(codec, WM8994_DAC1_RIGHT_MIXER_ROUTING, val);

	val = wm8994_read(codec, WM8994_CLOCKING_1);
	val &= ~(WM8994_DSP_FS1CLK_ENA_MASK | WM8994_DSP_FSINTCLK_ENA_MASK);
	val |= (WM8994_DSP_FS1CLK_ENA | WM8994_DSP_FSINTCLK_ENA);
	wm8994_write(codec, WM8994_CLOCKING_1, val);

	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_3);
	val &= ~(WM8994_MIXOUTLVOL_ENA_MASK | WM8994_MIXOUTRVOL_ENA_MASK |
		WM8994_MIXOUTL_ENA_MASK | WM8994_MIXOUTR_ENA_MASK);
	val |= (WM8994_MIXOUTL_ENA | WM8994_MIXOUTR_ENA |
		WM8994_MIXOUTRVOL_ENA | WM8994_MIXOUTLVOL_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_3, val);

	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_1);
	val &= ~(WM8994_BIAS_ENA_MASK | WM8994_VMID_SEL_MASK |
		WM8994_HPOUT2_ENA_MASK);
	val |= (WM8994_BIAS_ENA | WM8994_VMID_SEL_NORMAL);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_1, val);

	/* To enhance THD, Ch Seperation Characteristic */
	val = wm8994_read(codec, WM8994_ADDITIONAL_CONTROL);
	val &= ~(WM8994_LINEOUT1_FB_MASK | WM8994_LINEOUT2_FB_MASK);
	wm8994_write(codec, WM8994_ADDITIONAL_CONTROL, val);

	/* To tune THD, Power of each frequency */
	val = wm8994_read(codec, WM8994_ANTIPOP_1);
	val &= ~(WM8994_LINEOUT_VMID_BUF_ENA_MASK | WM8994_HPOUT2_IN_ENA_MASK |
		WM8994_LINEOUT1_DISCH_MASK | WM8994_LINEOUT2_DISCH_MASK);
	val |= WM8994_LINEOUT_VMID_BUF_ENA ;
	wm8994_write(codec, WM8994_ANTIPOP_1, val);

	msleep(230);

	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_3);
	val &= ~(WM8994_LINEOUT2N_ENA_MASK | WM8994_LINEOUT2P_ENA_MASK);
	val |= (WM8994_LINEOUT2N_ENA | WM8994_LINEOUT2P_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_3, val);

	/* to block sound to Earphone */
	wm8994_write(codec, 0x1c, 0x100);
	wm8994_write(codec, 0x1d, 0x100);
	wm8994_write(codec, 0x1, 0x303);
	wm8994_write(codec, 0x4c, 0x9f25);

}

void wm8994_set_playback_speaker_lineout(struct snd_soc_codec *codec)
{
	struct wm8994_priv *wm8994 = codec->drvdata;
	u16 val;
	DEBUG_LOG("");

	/************** Speaker *************/
	/* Disable end point for preventing pop up noise.*/
	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_1);
	val &= ~(WM8994_SPKOUTL_ENA_MASK);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_1, val);

	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_3);
	val &= ~(WM8994_MIXOUTLVOL_ENA_MASK | WM8994_MIXOUTRVOL_ENA_MASK |
		WM8994_MIXOUTL_ENA_MASK | WM8994_MIXOUTR_ENA_MASK |
		WM8994_SPKLVOL_ENA_MASK | WM8994_SPKRVOL_ENA_MASK);
	val |= (WM8994_SPKLVOL_ENA | WM8994_SPKRVOL_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_3, val);

	/* Speaker Volume Control */
	/* Unmute the SPKMIXVOLUME */
	val = wm8994_read(codec, WM8994_SPEAKER_VOLUME_LEFT);
	val &= ~(WM8994_SPKOUTL_MUTE_N_MASK);
	val |= (WM8994_SPKOUTL_MUTE_N);
	wm8994_write(codec, WM8994_SPEAKER_VOLUME_LEFT, val);

	val = wm8994_read(codec, WM8994_SPEAKER_VOLUME_RIGHT);
	val &= ~(WM8994_SPKOUTR_MUTE_N_MASK);
	val |= (WM8994_SPKOUTR_MUTE_N);
	wm8994_write(codec, WM8994_SPEAKER_VOLUME_RIGHT, val);

	/* Unmute DAC1 left */
	val = wm8994_read(codec, WM8994_DAC1_LEFT_VOLUME);
	val &= ~(WM8994_DAC1L_MUTE_MASK);
	wm8994_write(codec, WM8994_DAC1_LEFT_VOLUME, val);

	/* Unmute and volume ctrl RightDAC */
	val = wm8994_read(codec, WM8994_DAC1_RIGHT_VOLUME);
	val &= ~(WM8994_DAC1R_MUTE_MASK);
	wm8994_write(codec, WM8994_DAC1_RIGHT_VOLUME, val);

	val = wm8994_read(codec, WM8994_SPKOUT_MIXERS);
	val &= ~(WM8994_SPKMIXL_TO_SPKOUTL_MASK |
		 WM8994_SPKMIXR_TO_SPKOUTR_MASK);
	val |= (WM8994_SPKMIXL_TO_SPKOUTL | WM8994_SPKMIXR_TO_SPKOUTR);
	wm8994_write(codec, WM8994_SPKOUT_MIXERS, val);

	/* Unmute the DAC path */
	val = wm8994_read(codec, WM8994_SPEAKER_MIXER);
	val &= ~(WM8994_DAC1L_TO_SPKMIXL_MASK | WM8994_DAC1R_TO_SPKMIXR_MASK);
	val |= (WM8994_DAC1L_TO_SPKMIXL | WM8994_DAC1R_TO_SPKMIXR) ;
	wm8994_write(codec, WM8994_SPEAKER_MIXER, val);

	/* Eable DAC1 Left and timeslot left */
	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_5);
	val &= ~(WM8994_DAC1L_ENA_MASK | WM8994_AIF1DAC1L_ENA_MASK |
		WM8994_DAC1R_ENA_MASK | WM8994_AIF1DAC1R_ENA_MASK);
	val |= (WM8994_AIF1DAC1L_ENA | WM8994_DAC1L_ENA | WM8994_AIF1DAC1R_ENA |
		WM8994_DAC1R_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_5, val);

	/* Gain setting */
	wm8994_set_codec_gain(codec, PLAYBACK_MODE, PLAYBACK_SPK);

	/* enable timeslot0 to left dac */
	val = wm8994_read(codec, WM8994_DAC1_LEFT_MIXER_ROUTING);
	val &= ~(WM8994_AIF1DAC1L_TO_DAC1L_MASK);
	val |= WM8994_AIF1DAC1L_TO_DAC1L;
	wm8994_write(codec, WM8994_DAC1_LEFT_MIXER_ROUTING, val);
	/* enable timeslot0 to Right dac */
	val = wm8994_read(codec, WM8994_DAC1_RIGHT_MIXER_ROUTING);
	val &= ~(WM8994_AIF1DAC1R_TO_DAC1R_MASK);
	val |= WM8994_AIF1DAC1R_TO_DAC1R;
	wm8994_write(codec, WM8994_DAC1_RIGHT_MIXER_ROUTING, val);

	/* Enbale bias,vmid and Left speaker */
	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_1);
	val &= ~(WM8994_BIAS_ENA_MASK | WM8994_VMID_SEL_MASK |
		WM8994_HPOUT1L_ENA_MASK | WM8994_HPOUT1R_ENA_MASK |
		WM8994_SPKOUTL_ENA_MASK | WM8994_SPKOUTR_ENA_MASK);
	val |= (WM8994_BIAS_ENA | WM8994_VMID_SEL_NORMAL |
		WM8994_SPKOUTL_ENA | WM8994_SPKOUTR_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_1, val);

	/* Unmute */
	val = wm8994_read(codec, WM8994_AIF1_DAC1_FILTERS_1);
	val &= ~(WM8994_AIF1DAC1_MUTE_MASK | WM8994_AIF1DAC1_MONO_MASK);
	val |= WM8994_AIF1DAC1_UNMUTE;
	wm8994_write(codec, WM8994_AIF1_DAC1_FILTERS_1, val);

	/************** Lineout *************/
	wm8994_write(codec, WM8994_ANTIPOP_2, 0x0048);

	/*OUTPUT mute*/
	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_3);
	val &= ~(WM8994_LINEOUT2N_ENA_MASK | WM8994_LINEOUT2P_ENA_MASK);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_3, val);

	/* Gain setting */
	wm8994_set_codec_gain(codec, PLAYBACK_MODE, PLAYBACK_LINEOUT);

	val = wm8994_read(codec, WM8994_OUTPUT_MIXER_1);
	val &= ~(WM8994_DAC1L_TO_MIXOUTL_MASK);
	val |= (WM8994_DAC1L_TO_MIXOUTL);
	wm8994_write(codec, WM8994_OUTPUT_MIXER_1, val);

	val = wm8994_read(codec, WM8994_OUTPUT_MIXER_2);
	val &= ~(WM8994_DAC1R_TO_MIXOUTR_MASK);
	val |= (WM8994_DAC1R_TO_MIXOUTR);
	wm8994_write(codec, WM8994_OUTPUT_MIXER_2, val);

	val = wm8994_read(codec, WM8994_LINE_MIXER_2);
	val &= ~(WM8994_MIXOUTR_TO_LINEOUT2N_MASK |
		WM8994_MIXOUTL_TO_LINEOUT2N_MASK | WM8994_LINEOUT2_MODE_MASK |
		WM8994_MIXOUTR_TO_LINEOUT2P_MASK);
	val |= (WM8994_MIXOUTL_TO_LINEOUT2N | WM8994_LINEOUT2_MODE |
		WM8994_MIXOUTR_TO_LINEOUT2P);
	wm8994_write(codec, WM8994_LINE_MIXER_2, val);

	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_5);
	val &= ~(WM8994_DAC1R_ENA_MASK | WM8994_DAC1L_ENA_MASK |
		WM8994_AIF1DAC1R_ENA_MASK | WM8994_AIF1DAC1L_ENA_MASK);
	val |= (WM8994_AIF1DAC1L_ENA | WM8994_AIF1DAC1R_ENA |
		WM8994_DAC1L_ENA | WM8994_DAC1R_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_5, val);

	val = wm8994_read(codec, WM8994_AIF1_DAC1_FILTERS_1);
	val &= ~(WM8994_AIF1DAC1_MUTE_MASK | WM8994_AIF1DAC1_MONO_MASK);
	val |= (WM8994_AIF1DAC1_UNMUTE);
	wm8994_write(codec, WM8994_AIF1_DAC1_FILTERS_1, val);

	val = wm8994_read(codec, WM8994_LINE_OUTPUTS_VOLUME);
	val &= ~(WM8994_LINEOUT2N_MUTE_MASK | WM8994_LINEOUT2P_MUTE_MASK);
	wm8994_write(codec, WM8994_LINE_OUTPUTS_VOLUME, val);

	val = wm8994_read(codec, WM8994_DAC1_LEFT_MIXER_ROUTING);
	val &= ~(WM8994_AIF1DAC1L_TO_DAC1L_MASK);
	val |= (WM8994_AIF1DAC1L_TO_DAC1L);
	wm8994_write(codec, WM8994_DAC1_LEFT_MIXER_ROUTING, val);

	val = wm8994_read(codec, WM8994_DAC1_RIGHT_MIXER_ROUTING);
	val &= ~(WM8994_AIF1DAC1R_TO_DAC1R_MASK);
	val |= (WM8994_AIF1DAC1R_TO_DAC1R);
	wm8994_write(codec, WM8994_DAC1_RIGHT_MIXER_ROUTING, val);

	val = wm8994_read(codec, WM8994_CLOCKING_1);
	val &= ~(WM8994_DSP_FS1CLK_ENA_MASK | WM8994_DSP_FSINTCLK_ENA_MASK);
	val |= (WM8994_DSP_FS1CLK_ENA | WM8994_DSP_FSINTCLK_ENA);
	wm8994_write(codec, WM8994_CLOCKING_1, val);

	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_3);
	val &= ~(WM8994_MIXOUTLVOL_ENA_MASK | WM8994_MIXOUTRVOL_ENA_MASK |
		WM8994_MIXOUTL_ENA_MASK | WM8994_MIXOUTR_ENA_MASK);
	val |= (WM8994_MIXOUTL_ENA | WM8994_MIXOUTR_ENA |
		WM8994_MIXOUTRVOL_ENA | WM8994_MIXOUTLVOL_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_3, val);

	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_1);
	val &= ~(WM8994_BIAS_ENA_MASK | WM8994_VMID_SEL_MASK |
		WM8994_HPOUT2_ENA_MASK);
	val |= (WM8994_BIAS_ENA | WM8994_VMID_SEL_NORMAL);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_1, val);

	/* To enhance THD, Ch Seperation Characteristic (2010.08.19, KDS)*/
	val = wm8994_read(codec, WM8994_ADDITIONAL_CONTROL);
	val &= ~(WM8994_LINEOUT1_FB_MASK | WM8994_LINEOUT2_FB_MASK);
	/* To tune THD, Power of each frequency (2010.09.15, KDS)*/
	/* val |= WM8994_LINEOUT2_FB ;*/
	wm8994_write(codec, WM8994_ADDITIONAL_CONTROL, val);

	/* To tune THD, Power of each frequency (2010.09.15, KDS)*/
	val = wm8994_read(codec, WM8994_ANTIPOP_1);
	val &= ~(WM8994_LINEOUT_VMID_BUF_ENA_MASK | WM8994_HPOUT2_IN_ENA_MASK |
		WM8994_LINEOUT1_DISCH_MASK | WM8994_LINEOUT2_DISCH_MASK);
	val |= WM8994_LINEOUT_VMID_BUF_ENA ;
	wm8994_write(codec, WM8994_ANTIPOP_1, val);

	msleep(230);

	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_3);
	val &= ~(WM8994_LINEOUT2N_ENA_MASK | WM8994_LINEOUT2P_ENA_MASK);
	val |= (WM8994_LINEOUT2N_ENA | WM8994_LINEOUT2P_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_3, val);

	/* to block sound to Earphone */
	wm8994_write(codec, 0x1c, 0x100);
	wm8994_write(codec, 0x1d, 0x100);
	wm8994_write(codec, 0x1, 0x3303);
	wm8994_write(codec, 0x4c, 0x9f25);
}

#define PCM_SLAVE
void wm8994_set_voicecall_common_setting(struct snd_soc_codec *codec)
{
#ifdef WM8994_FACTORY_LOOPBACK
	struct wm8994_priv *wm8994 = codec->drvdata;
#endif
	int val;

	wm8994_write(codec, 0x102, 0x0003);
	wm8994_write(codec, 0x817, 0x0000);
	wm8994_write(codec, 0x102, 0x0000);

	/* GPIO Configuration */
	wm8994_write(codec, WM8994_GPIO_1, 0xA101);
#ifdef PCM_SLAVE
	wm8994_write(codec, WM8994_GPIO_2, 0xA100);
	wm8994_write(codec, WM8994_GPIO_3, 0xA100);
	wm8994_write(codec, WM8994_GPIO_4, 0xA100);
	wm8994_write(codec, WM8994_GPIO_5, 0xA100);
#else
	wm8994_write(codec, WM8994_GPIO_2, 0x8100);
	wm8994_write(codec, WM8994_GPIO_3, 0x0100);
	wm8994_write(codec, WM8994_GPIO_4, 0x0100);
	wm8994_write(codec, WM8994_GPIO_5, 0x8100);
#endif
	wm8994_write(codec, WM8994_GPIO_6, 0xA101);
	wm8994_write(codec, WM8994_GPIO_7, 0x0100);
	wm8994_write(codec, WM8994_GPIO_8, 0xA101);
	wm8994_write(codec, WM8994_GPIO_9, 0xA101);
	wm8994_write(codec, WM8994_GPIO_10, 0xA101);
	wm8994_write(codec, WM8994_GPIO_11, 0xA101);

	wm8994_write(codec, WM8994_FLL2_CONTROL_2, 0x2B00);
	/*wm8994_write(codec, WM8994_FLL2_CONTROL_3, 0xB357);*/
	wm8994_write(codec, WM8994_FLL2_CONTROL_4, 0x1600);
	wm8994_write(codec, WM8994_FLL2_CONTROL_5, 0x0C83);
	wm8994_write(codec, WM8994_FLL2_CONTROL_1, 0x0005);

	/*AIF2*/
	wm8994_write(codec,  0x204, 0x0018); /*AIF2_CLK1*/
	wm8994_write(codec,  0x211, 0x0003); /*AIF2_RATE*/
	wm8994_write(codec,  0x310, 0x4010); /*AIF2_CON1 //BCLK2 Invert*/
	wm8994_write(codec,  0x311, 0x0000); /*AIF2_CON2*/
#ifdef WM8994_FACTORY_LOOPBACK
	if (wm8994->loopback_path_control == spk || 
		wm8994->loopback_path_control == ear ||wm8994->loopback_path_control == ear_pmic) {
		val = wm8994_read(codec, WM8994_AIF2_CONTROL_2);
		val |= 0x01;
		wm8994_write(codec, WM8994_AIF2_CONTROL_2, val);
	}
#endif
#ifdef PCM_SLAVE
	wm8994_write(codec,  0x312, 0x0000); /*AIF2_M/S //AIF2 Slave*/
#else
	wm8994_write(codec,  0x312, 0x7000); /*AIF2_M/S //AIF2 Master*/
#endif
	wm8994_write(codec,  0x313, 0x00c0); /*AIF2_BCLK*/

	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_5);
	val &= ~(WM8994_AIF2DACL_ENA_MASK | WM8994_AIF2DACR_ENA_MASK |
		WM8994_AIF1DAC1L_ENA_MASK | WM8994_AIF1DAC1R_ENA_MASK |
		WM8994_DAC1L_ENA_MASK | WM8994_DAC1R_ENA_MASK);
	val |= (WM8994_AIF2DACL_ENA | WM8994_AIF2DACR_ENA |
		WM8994_AIF1DAC1L_ENA | WM8994_AIF1DAC1R_ENA |
		WM8994_DAC1L_ENA | WM8994_DAC1R_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_5, val);

	/* Clocking */
	val = wm8994_read(codec, WM8994_CLOCKING_1);
	val |= (WM8994_DSP_FS2CLK_ENA);
	wm8994_write(codec, WM8994_CLOCKING_1, val);

	wm8994_write(codec,  WM8994_AIF2_CLOCKING_1, 0x0019);

	wm8994_write(codec, WM8994_POWER_MANAGEMENT_6, 0x0);

	/* AIF1 & AIF2 Output is connected to DAC1 */
	val = wm8994_read(codec, WM8994_DAC1_LEFT_MIXER_ROUTING);
	val &= ~(WM8994_AIF1DAC1L_TO_DAC1L_MASK |
		WM8994_AIF2DACL_TO_DAC1L_MASK);
	val |= (WM8994_AIF1DAC1L_TO_DAC1L | WM8994_AIF2DACL_TO_DAC1L);
	wm8994_write(codec, WM8994_DAC1_LEFT_MIXER_ROUTING, val);

	val = wm8994_read(codec, WM8994_DAC1_RIGHT_MIXER_ROUTING);
	val &= ~(WM8994_AIF1DAC1R_TO_DAC1R_MASK |
		WM8994_AIF2DACR_TO_DAC1R_MASK);
	val |= (WM8994_AIF1DAC1R_TO_DAC1R | WM8994_AIF2DACR_TO_DAC1R);
	wm8994_write(codec, WM8994_DAC1_RIGHT_MIXER_ROUTING, val);

	wm8994_write(codec, 0x6, 0x0);
}

void wm8994_set_voicecall_receiver(struct snd_soc_codec *codec)
{
	struct wm8994_priv *wm8994 = codec->drvdata;

	int val;

	DEBUG_LOG("");

	audio_ctrl_mic_bias_gpio(wm8994->pdata, 1);

	wm8994_set_voicecall_common_setting(codec);

	wm8994_write(codec, 0x601, 0x0005);
	wm8994_write(codec, 0x602, 0x0005);
	wm8994_write(codec, 0x603, 0x0018c);
	/* Tx -> AIF2 Path */
	wm8994_write(codec, WM8994_DAC2_LEFT_MIXER_ROUTING,
			WM8994_ADC1_TO_DAC2L);
	wm8994_write(codec, WM8994_DAC2_RIGHT_MIXER_ROUTING,
			WM8994_ADC2_TO_DAC2R);

	/* Analogue Input Configuration*/
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_2,
		WM8994_TSHUT_ENA | WM8994_TSHUT_OPDIS | WM8994_MIXINR_ENA |
		WM8994_IN2R_ENA);
	wm8994_write(codec, WM8994_INPUT_MIXER_2, WM8994_IN2RP_TO_IN2R |
			WM8994_IN2RN_TO_IN2R);

	val = wm8994_read(codec, WM8994_INPUT_MIXER_4);
	val &= ~(WM8994_IN2R_TO_MIXINR_MASK);
	val |= (WM8994_IN2R_TO_MIXINR);
	wm8994_write(codec, WM8994_INPUT_MIXER_4, val);

	val = wm8994_read(codec, WM8994_RIGHT_LINE_INPUT_3_4_VOLUME);
	val &= ~(WM8994_IN2R_MUTE_MASK);
	wm8994_write(codec, WM8994_RIGHT_LINE_INPUT_3_4_VOLUME, val);

	/* Analogue Output Configuration*/
	wm8994_write(codec, 0x03, 0x0300);

	wm8994_write(codec, WM8994_POWER_MANAGEMENT_4,
			WM8994_AIF2ADCL_ENA | WM8994_ADCL_ENA |
			WM8994_AIF2ADCR_ENA | WM8994_ADCR_ENA);

	wm8994_write(codec, WM8994_AIF2_CLOCKING_1, 0x0019);

	val = wm8994_read(codec, WM8994_SPEAKER_VOLUME_LEFT);
	val &= ~(WM8994_SPKOUTL_MUTE_N_MASK);
	val |= (WM8994_SPKOUTL_MUTE_N);
	wm8994_write(codec, WM8994_SPEAKER_VOLUME_LEFT, val);

	val = wm8994_read(codec, WM8994_SPEAKER_VOLUME_RIGHT);
	val &= ~(WM8994_SPKOUTR_MUTE_N_MASK);
	val |= (WM8994_SPKOUTR_MUTE_N);
	wm8994_write(codec, WM8994_SPEAKER_VOLUME_RIGHT, val);

	val = wm8994_read(codec, WM8994_SPKOUT_MIXERS);
	val &= ~(WM8994_SPKMIXL_TO_SPKOUTL_MASK |
		WM8994_SPKMIXR_TO_SPKOUTR_MASK);
	val |= (WM8994_SPKMIXL_TO_SPKOUTL | WM8994_SPKMIXR_TO_SPKOUTR) ;
	wm8994_write(codec, WM8994_SPKOUT_MIXERS, val);

	wm8994_write(codec, 0x36, 0x0003);
	/* Digital Path Enables and Unmutes*/

	wm8994_write(codec, WM8994_SIDETONE, 0x01C0);


	wm8994_write(codec, WM8994_ANALOGUE_HP_1, 0x0000);
	wm8994_write(codec, WM8994_DC_SERVO_1, 0x0000);

	wm8994_write(codec, WM8994_POWER_MANAGEMENT_1,
		WM8994_SPKOUTL_ENA | WM8994_SPKOUTR_ENA |
		WM8994_VMID_SEL_NORMAL | WM8994_BIAS_ENA);

	wm8994_set_codec_gain(codec, VOICECALL_MODE, VOICECALL_RCV);

	/* Unmute DAC1 left */
	val = wm8994_read(codec, WM8994_DAC1_LEFT_VOLUME);
	val &= ~(WM8994_DAC1L_MUTE_MASK);
	wm8994_write(codec, WM8994_DAC1_LEFT_VOLUME, val);

	/* Unmute and volume ctrl RightDAC */
	val = wm8994_read(codec, WM8994_DAC1_RIGHT_VOLUME);
	val &= ~(WM8994_DAC1R_MUTE_MASK);
	wm8994_write(codec, WM8994_DAC1_RIGHT_VOLUME, val);

	wm8994_write(codec, WM8994_DAC2_LEFT_VOLUME, 0x01C0);
	wm8994_write(codec, WM8994_DAC2_RIGHT_VOLUME, 0x01C0);
	wm8994_write(codec, WM8994_AIF2_DAC_FILTERS_1, WM8994_AIF1DAC1_UNMUTE);
	wm8994_write(codec, WM8994_AIF1_DAC1_FILTERS_1, WM8994_AIF1DAC2_UNMUTE);
}

void wm8994_set_voicecall_headset(struct snd_soc_codec *codec)
{
	struct wm8994_priv *wm8994 = codec->drvdata;

	int val;

	u16 testreturn1 = 0;
	u16 testreturn2 = 0;
	u16 testlow1 = 0;
	u16 testhigh1 = 0;
	u8 testlow = 0;
	u8 testhigh = 0;

	DEBUG_LOG("");

	wm8994_set_voicecall_common_setting(codec);

	/* Digital Path Enables and Unmutes */
	if (wm8994->hw_version == 3) {	/* H/W Rev D */
		wm8994_write(codec, WM8994_DAC2_LEFT_MIXER_ROUTING,
			WM8994_ADC2_TO_DAC2L);
		wm8994_write(codec, WM8994_DAC2_MIXER_VOLUMES, 0x0180);
		wm8994_write(codec, WM8994_SIDETONE, 0x01C0);
	} else {	/* H/W Rev B */
		wm8994_write(codec, WM8994_DAC2_MIXER_VOLUMES, 0x000C);
		wm8994_write(codec, WM8994_DAC2_LEFT_MIXER_ROUTING,
			WM8994_ADC1_TO_DAC2L);
		wm8994_write(codec, WM8994_SIDETONE, 0x01C1);
	}

	/* Analogue Input Configuration */
	val = wm8994_read(codec, WM8994_POWER_MANAGEMENT_2);
	val &= ~(WM8994_TSHUT_ENA_MASK | WM8994_TSHUT_OPDIS_MASK |
		WM8994_MIXINR_ENA_MASK | WM8994_IN1R_ENA_MASK);
	val |= (WM8994_TSHUT_ENA | WM8994_TSHUT_OPDIS |
		WM8994_MIXINR_ENA | WM8994_IN1R_ENA);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_2, 0x6110);

	val = wm8994_read(codec, WM8994_RIGHT_LINE_INPUT_1_2_VOLUME);
	val &= ~(WM8994_IN1R_MUTE_MASK);
	wm8994_write(codec, WM8994_RIGHT_LINE_INPUT_1_2_VOLUME, val);

	val = wm8994_read(codec, WM8994_INPUT_MIXER_4);
	val &= ~(WM8994_IN1R_TO_MIXINR_MASK);
	val |= (WM8994_IN1R_TO_MIXINR);
	wm8994_write(codec, WM8994_INPUT_MIXER_4, val);

	val = wm8994_read(codec, WM8994_INPUT_MIXER_2);
	val &= ~(WM8994_IN1RP_TO_IN1R_MASK |  WM8994_IN1RN_TO_IN1R_MASK);
	val |= (WM8994_IN1RP_TO_IN1R | WM8994_IN1RN_TO_IN1R);
	wm8994_write(codec, WM8994_INPUT_MIXER_2, 0x0003);

	/* Unmute*/
	val = wm8994_read(codec, WM8994_LEFT_OPGA_VOLUME);
	val &= ~(WM8994_MIXOUTL_MUTE_N_MASK);
	val |= (WM8994_MIXOUTL_MUTE_N);
	wm8994_write(codec, WM8994_LEFT_OPGA_VOLUME, val);

	val = wm8994_read(codec, WM8994_RIGHT_OPGA_VOLUME);
	val &= ~(WM8994_MIXOUTR_MUTE_N_MASK);
	val |= (WM8994_MIXOUTR_MUTE_N);
	wm8994_write(codec, WM8994_RIGHT_OPGA_VOLUME, val);

	wm8994_write(codec, WM8994_POWER_MANAGEMENT_4, 0x2001);

	val = wm8994_read(codec, 0x102);
	val &= ~(0x0003);
	val = 0x0003;
	wm8994_write(codec, 0x102, val);

	val = wm8994_read(codec, 0x56);
	val &= ~(0x0003);
	val = 0x0003;
	wm8994_write(codec, 0x56, val);

	val = wm8994_read(codec, 0x102);
	val &= ~(0x0000);
	val = 0x0000;
	wm8994_write(codec, 0x102, val);

	val = wm8994_read(codec, WM8994_CLASS_W_1);
	val &= ~(0x0005);
	val |= 0x0005;
	wm8994_write(codec, WM8994_CLASS_W_1, val);

	val = wm8994_read(codec, WM8994_LEFT_OUTPUT_VOLUME);
	val &= ~(WM8994_HPOUT1L_MUTE_N_MASK);
	val |= (WM8994_HPOUT1L_MUTE_N);
	wm8994_write(codec, WM8994_LEFT_OUTPUT_VOLUME, val);

	val = wm8994_read(codec, WM8994_RIGHT_OUTPUT_VOLUME);
	val &= ~(WM8994_HPOUT1R_MUTE_N_MASK);
	val |= (WM8994_HPOUT1R_MUTE_N);
	wm8994_write(codec, WM8994_RIGHT_OUTPUT_VOLUME, val);

	val = wm8994_read(codec, WM8994_DC_SERVO_2);
	val &= ~(0x03E0);
	val = 0x03E0;
	wm8994_write(codec, WM8994_DC_SERVO_2, val);

	wm8994_write(codec, WM8994_POWER_MANAGEMENT_1, 0x0303);

	wm8994_write(codec, WM8994_ANALOGUE_HP_1, 0x0022);
	wm8994_write(codec, WM8994_CHARGE_PUMP_1, 0x9F25);

	msleep(5);

	/* Analogue Output Configuration */
	wm8994_write(codec, WM8994_OUTPUT_MIXER_1, 0x0001);
	wm8994_write(codec, WM8994_OUTPUT_MIXER_2, 0x0001);

	wm8994_write(codec, WM8994_POWER_MANAGEMENT_3, 0x0030);

	wm8994_write(codec, WM8994_AIF2_CLOCKING_1, 0x0019);

	if (!wm8994->dc_servo[DCS_VOICE]) {
		wait_for_dc_servo(codec,
				  WM8994_DCS_TRIG_SERIES_0 |
				  WM8994_DCS_TRIG_SERIES_1);

		testreturn1 = wm8994_read(codec, WM8994_DC_SERVO_4);

		testlow = (signed char)(testreturn1 & 0xff);
		testhigh = (signed char)((testreturn1>>8) & 0xff);

		testlow1 = ((signed short)testlow - 5) & 0x00ff;
		testhigh1 = (((signed short)(testhigh - 5)<<8) & 0xff00);
		testreturn2 = testlow1|testhigh1;
	} else {
		testreturn2 = wm8994->dc_servo[DCS_VOICE];
	}

	wm8994_write(codec, WM8994_DC_SERVO_4, testreturn2);

	wait_for_dc_servo(codec,
			  WM8994_DCS_TRIG_DAC_WR_0 | WM8994_DCS_TRIG_DAC_WR_1);

	wm8994->dc_servo[DCS_VOICE] = testreturn2;

	wm8994_write(codec, WM8994_ANALOGUE_HP_1, 0x00EE);

	wm8994_set_codec_gain(codec, VOICECALL_MODE, VOICECALL_HP);

	/* Unmute DAC1 left */
	val = wm8994_read(codec, WM8994_DAC1_LEFT_VOLUME);
	val &= ~(WM8994_DAC1L_MUTE_MASK);
	wm8994_write(codec, WM8994_DAC1_LEFT_VOLUME, val);

	/* Unmute and volume ctrl RightDAC */
	val = wm8994_read(codec, WM8994_DAC1_RIGHT_VOLUME);
	val &= ~(WM8994_DAC1R_MUTE_MASK);
	wm8994_write(codec, WM8994_DAC1_RIGHT_VOLUME, val);

	wm8994_write(codec, WM8994_DAC2_LEFT_VOLUME, 0x01C0);

	wm8994_write(codec, WM8994_AIF1_DAC1_FILTERS_1, 0x0000);
	wm8994_write(codec, WM8994_AIF2_DAC_FILTERS_1, 0x0000);

}

void wm8994_set_voicecall_headphone(struct snd_soc_codec *codec)
{
	struct wm8994_priv *wm8994 = codec->drvdata;

	int val;

	u16 testreturn1 = 0;
	u16 testreturn2 = 0;
	u16 testlow1 = 0;
	u16 testhigh1 = 0;
	u8 testlow = 0;
	u8 testhigh = 0;

	DEBUG_LOG("");
	audio_ctrl_mic_bias_gpio(wm8994->pdata, 1);

	wm8994_set_voicecall_common_setting(codec);

	/* Digital Path Enables and Unmutes */
	wm8994_write(codec, WM8994_DAC2_MIXER_VOLUMES, 0x000C);
	wm8994_write(codec, WM8994_DAC2_LEFT_MIXER_ROUTING,
			WM8994_ADC1_TO_DAC2L);

	/* Analogue Input Configuration */
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_2,
		WM8994_TSHUT_ENA | WM8994_TSHUT_OPDIS | WM8994_MIXINL_ENA |
		WM8994_IN1L_ENA);

	/* Unmute IN1L PGA, update volume */
	val = wm8994_read(codec, WM8994_LEFT_LINE_INPUT_1_2_VOLUME);
	val &= ~(WM8994_IN1L_MUTE_MASK);
	wm8994_write(codec, WM8994_LEFT_LINE_INPUT_1_2_VOLUME, val);

	/* Unmute the PGA */
	val = wm8994_read(codec, WM8994_INPUT_MIXER_3);
	val &= ~(WM8994_IN1L_TO_MIXINL_MASK);
	val |= (WM8994_IN1L_TO_MIXINL);
	wm8994_write(codec, WM8994_INPUT_MIXER_3, val);

	wm8994_write(codec, WM8994_INPUT_MIXER_2,
			WM8994_IN1LP_TO_IN1L | WM8994_IN1LN_TO_IN1L);

	/* Unmute*/
	val = wm8994_read(codec, WM8994_LEFT_OPGA_VOLUME);
	val &= ~(WM8994_MIXOUTL_MUTE_N_MASK);
	val |= (WM8994_MIXOUTL_MUTE_N);
	wm8994_write(codec, WM8994_LEFT_OPGA_VOLUME, val);

	val = wm8994_read(codec, WM8994_RIGHT_OPGA_VOLUME);
	val &= ~(WM8994_MIXOUTR_MUTE_N_MASK);
	val |= (WM8994_MIXOUTR_MUTE_N);
	wm8994_write(codec, WM8994_RIGHT_OPGA_VOLUME, val);

	/* Digital Path Enables and Unmutes */
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_4,
			WM8994_AIF2ADCL_ENA | WM8994_ADCL_ENA);

	val = wm8994_read(codec, 0x102);
	val &= ~(0x0003);
	val = 0x0003;
	wm8994_write(codec, 0x102, val);

	val = wm8994_read(codec, 0x56);
	val &= ~(0x0003);
	val = 0x0003;
	wm8994_write(codec, 0x56, val);

	val = wm8994_read(codec, 0x102);
	val &= ~(0x0000);
	val = 0x0000;
	wm8994_write(codec, 0x102, val);

	val = wm8994_read(codec, WM8994_CLASS_W_1);
	val &= ~(0x0005);
	val |= 0x0005;
	wm8994_write(codec, WM8994_CLASS_W_1, val);

	val = wm8994_read(codec, WM8994_LEFT_OUTPUT_VOLUME);
	val &= ~(WM8994_HPOUT1L_MUTE_N_MASK);
	val |= (WM8994_HPOUT1L_MUTE_N);
	wm8994_write(codec, WM8994_LEFT_OUTPUT_VOLUME, val);

	val = wm8994_read(codec, WM8994_RIGHT_OUTPUT_VOLUME);
	val &= ~(WM8994_HPOUT1R_MUTE_N_MASK);
	val |= (WM8994_HPOUT1R_MUTE_N);
	wm8994_write(codec, WM8994_RIGHT_OUTPUT_VOLUME, val);

	val = wm8994_read(codec, WM8994_DC_SERVO_2);
	val &= ~(0x03E0);
	val = 0x03E0;
	wm8994_write(codec, WM8994_DC_SERVO_2, val);

	wm8994_write(codec, WM8994_POWER_MANAGEMENT_1, 0x0303);

	wm8994_write(codec, WM8994_ANALOGUE_HP_1, 0x0022);
	wm8994_write(codec, WM8994_CHARGE_PUMP_1, 0x9F25);

	msleep(5);

	/* Analogue Output Configuration */
	wm8994_write(codec, WM8994_OUTPUT_MIXER_1, 0x0001);
	wm8994_write(codec, WM8994_OUTPUT_MIXER_2, 0x0001);

	wm8994_write(codec, WM8994_POWER_MANAGEMENT_3, 0x0030);

	wm8994_write(codec, WM8994_AIF2_CLOCKING_1, 0x0019);

	if (!wm8994->dc_servo[DCS_VOICE]) {
		wait_for_dc_servo(codec,
				  WM8994_DCS_TRIG_SERIES_0 |
				  WM8994_DCS_TRIG_SERIES_1);
		testreturn1 = wm8994_read(codec, WM8994_DC_SERVO_4);

		testlow = (signed char)(testreturn1 & 0xff);
		testhigh = (signed char)((testreturn1>>8) & 0xff);

		testlow1 = ((signed short)testlow - 5) & 0x00ff;
		testhigh1 = (((signed short)(testhigh - 5)<<8) & 0xff00);
		testreturn2 = testlow1|testhigh1;
	} else {
		testreturn2 = wm8994->dc_servo[DCS_VOICE];
	}

	wm8994_write(codec, WM8994_DC_SERVO_4, testreturn2);

	wait_for_dc_servo(codec,
			  WM8994_DCS_TRIG_DAC_WR_0 | WM8994_DCS_TRIG_DAC_WR_1);

	wm8994->dc_servo[DCS_VOICE] = testreturn2;

	wm8994_write(codec, WM8994_ANALOGUE_HP_1, 0x00EE);

	wm8994_set_codec_gain(codec, VOICECALL_MODE, VOICECALL_HP_NO_MIC);

	/* Unmute DAC1 left */
	val = wm8994_read(codec, WM8994_DAC1_LEFT_VOLUME);
	val &= ~(WM8994_DAC1L_MUTE_MASK);
	wm8994_write(codec, WM8994_DAC1_LEFT_VOLUME, val);

	/* Unmute and volume ctrl RightDAC */
	val = wm8994_read(codec, WM8994_DAC1_RIGHT_VOLUME);
	val &= ~(WM8994_DAC1R_MUTE_MASK);
	wm8994_write(codec, WM8994_DAC1_RIGHT_VOLUME, val);

	wm8994_write(codec, WM8994_DAC2_LEFT_VOLUME, 0x01C0);

	wm8994_write(codec, WM8994_AIF1_DAC1_FILTERS_1, 0x0000);
	wm8994_write(codec, WM8994_AIF2_DAC_FILTERS_1, 0x0000);

}

void wm8994_set_voicecall_speaker(struct snd_soc_codec *codec)
{
	struct wm8994_priv *wm8994 = codec->drvdata;

	int val;

	DEBUG_LOG("");

	audio_ctrl_mic_bias_gpio(wm8994->pdata, 1);

	wm8994_set_voicecall_common_setting(codec);

	wm8994_write(codec, 0x601, 0x0005);
	wm8994_write(codec, 0x602, 0x0005);
	wm8994_write(codec, 0x603, 0x000C);
	/* Tx -> AIF2 Path */
	wm8994_write(codec, WM8994_DAC2_LEFT_MIXER_ROUTING,
			WM8994_ADC1_TO_DAC2L);
	wm8994_write(codec, WM8994_DAC2_RIGHT_MIXER_ROUTING,
			WM8994_ADC1_TO_DAC2R);
	/* Analogue Input Configuration*/
	wm8994_write(codec, 0x02, 0x6240);
	wm8994_write(codec, WM8994_INPUT_MIXER_2, WM8994_IN1LP_TO_IN1L |
			WM8994_IN1LN_TO_IN1L);

	val = wm8994_read(codec, WM8994_INPUT_MIXER_3);
	val &= ~(WM8994_IN1L_TO_MIXINL_MASK);
	val |= (WM8994_IN1L_TO_MIXINL);
	wm8994_write(codec, WM8994_INPUT_MIXER_3, val);

	val = wm8994_read(codec, WM8994_LEFT_LINE_INPUT_1_2_VOLUME);
	val &= ~(WM8994_IN1L_MUTE_MASK);
	wm8994_write(codec, WM8994_LEFT_LINE_INPUT_1_2_VOLUME, val);

	/* Analogue Output Configuration*/
	wm8994_write(codec, 0x03, 0x0300);

	wm8994_write(codec, WM8994_POWER_MANAGEMENT_4,
			WM8994_AIF2ADCL_ENA | WM8994_ADCL_ENA |
			WM8994_AIF2ADCR_ENA | WM8994_ADCR_ENA);

	wm8994_write(codec, WM8994_AIF2_CLOCKING_1, 0x0019);

	val = wm8994_read(codec, WM8994_SPEAKER_VOLUME_LEFT);
	val &= ~(WM8994_SPKOUTL_MUTE_N_MASK);
	val |= (WM8994_SPKOUTL_MUTE_N);
	wm8994_write(codec, WM8994_SPEAKER_VOLUME_LEFT, val);

	val = wm8994_read(codec, WM8994_SPEAKER_VOLUME_RIGHT);
	val &= ~(WM8994_SPKOUTR_MUTE_N_MASK);
	val |= (WM8994_SPKOUTR_MUTE_N);
	wm8994_write(codec, WM8994_SPEAKER_VOLUME_RIGHT, val);

	val = wm8994_read(codec, WM8994_SPKOUT_MIXERS);
	val &= ~(WM8994_SPKMIXL_TO_SPKOUTL_MASK |
		WM8994_SPKMIXR_TO_SPKOUTR_MASK);
	val |= (WM8994_SPKMIXL_TO_SPKOUTL | WM8994_SPKMIXR_TO_SPKOUTR) ;
	wm8994_write(codec, WM8994_SPKOUT_MIXERS, val);

	wm8994_write(codec, 0x36, 0x0003);
	/* Digital Path Enables and Unmutes*/

	wm8994_write(codec, WM8994_SIDETONE, 0x01C0);


	wm8994_write(codec, WM8994_ANALOGUE_HP_1, 0x0000);
	wm8994_write(codec, WM8994_DC_SERVO_1, 0x0000);

	wm8994_write(codec, WM8994_POWER_MANAGEMENT_1,
		WM8994_SPKOUTL_ENA | WM8994_SPKOUTR_ENA |
		WM8994_VMID_SEL_NORMAL | WM8994_BIAS_ENA);

	wm8994_set_codec_gain(codec, VOICECALL_MODE, VOICECALL_SPK);

	/* Unmute DAC1 left */
	val = wm8994_read(codec, WM8994_DAC1_LEFT_VOLUME);
	val &= ~(WM8994_DAC1L_MUTE_MASK);
	wm8994_write(codec, WM8994_DAC1_LEFT_VOLUME, val);

	/* Unmute and volume ctrl RightDAC */
	val = wm8994_read(codec, WM8994_DAC1_RIGHT_VOLUME);
	val &= ~(WM8994_DAC1R_MUTE_MASK);
	wm8994_write(codec, WM8994_DAC1_RIGHT_VOLUME, val);
	wm8994_write(codec, WM8994_DAC2_LEFT_VOLUME, 0x01C0);
	wm8994_write(codec, WM8994_DAC2_RIGHT_VOLUME, 0x01C0);
	wm8994_write(codec, WM8994_AIF2_DAC_FILTERS_1, WM8994_AIF1DAC1_UNMUTE);
	wm8994_write(codec, WM8994_AIF1_DAC1_FILTERS_1, WM8994_AIF1DAC2_UNMUTE);
#ifdef WM8994_FACTORY_LOOPBACK
	if (wm8994->loopback_path_control == spk || 
		wm8994->loopback_path_control == ear) {
		wm8994_write(codec, WM8994_LEFT_LINE_INPUT_1_2_VOLUME, 0x0);
		wm8994_write(codec, WM8994_SPEAKER_VOLUME_LEFT, 0x156);
		wm8994_write(codec, WM8994_SPEAKER_VOLUME_RIGHT, 0x156);
		DEBUG_LOG("wm8994 factory test loop!!!!!!!!!!!!!!!!!!!!!");
	}
#endif	
}

void wm8994_set_voicecall_bluetooth(struct snd_soc_codec *codec)
{
	int val;

	DEBUG_LOG("");

	wm8994_set_voicecall_common_setting(codec);

	/* GPIO Configuration */
	wm8994_write(codec, WM8994_GPIO_8, WM8994_GP8_DIR | WM8994_GP8_DB);
	wm8994_write(codec, WM8994_GPIO_9, WM8994_GP9_DB);
	wm8994_write(codec, WM8994_GPIO_10, WM8994_GP10_DB);
	wm8994_write(codec, WM8994_GPIO_11, WM8994_GP11_DB);

	/* Digital Path Enables and Unmutes */
	val  = wm8994_read(codec, WM8994_POWER_MANAGEMENT_1);
	val &= ~(WM8994_SPKOUTL_ENA_MASK | WM8994_HPOUT2_ENA_MASK |
		WM8994_HPOUT1L_ENA_MASK | WM8994_HPOUT1R_ENA_MASK);
	wm8994_write(codec, WM8994_POWER_MANAGEMENT_1, val);

	wm8994_write(codec, WM8994_POWER_MANAGEMENT_4,
		WM8994_AIF2ADCL_ENA | WM8994_ADCL_ENA);

	/* If Input MIC is enabled, bluetooth Rx is muted. */
	wm8994_write(codec, WM8994_LEFT_LINE_INPUT_1_2_VOLUME,
			WM8994_IN1L_MUTE);
	wm8994_write(codec, WM8994_RIGHT_LINE_INPUT_1_2_VOLUME,
			WM8994_IN1R_MUTE);
	wm8994_write(codec, WM8994_INPUT_MIXER_2, 0x00);
	wm8994_write(codec, WM8994_INPUT_MIXER_3, 0x00);
	wm8994_write(codec, WM8994_INPUT_MIXER_4, 0x00);

	/*
	 * for BT DTMF Play
	 * Rx Path: AIF2ADCDAT2 select
	 * CP(CALL) Path:GPIO5/DACDAT2 select
	 * AP(DTMF) Path: DACDAT1 select
	 * Tx Path: GPIO8/DACDAT3 select
	 */

	wm8994_write(codec, WM8994_POWER_MANAGEMENT_6, 0x000C);

	/* AIF1 & AIF2 Output is connected to DAC1 */
	wm8994_write(codec, WM8994_DAC2_LEFT_MIXER_ROUTING,
		WM8994_AIF2DACL_TO_DAC2L | WM8994_AIF1DAC1L_TO_DAC2L);
	wm8994_write(codec, WM8994_DAC2_RIGHT_MIXER_ROUTING,
		WM8994_AIF2DACR_TO_DAC2R | WM8994_AIF1DAC1R_TO_DAC2R);

	wm8994_write(codec, WM8994_AIF2_CLOCKING_1, 0x0019);

	wm8994_write(codec, WM8994_DAC2_MIXER_VOLUMES, 0x000C);

	wm8994_write(codec, WM8994_DAC2_LEFT_VOLUME, 0x01C0);
	wm8994_write(codec, WM8994_DAC2_RIGHT_VOLUME, 0x01C0);

	wm8994_write(codec, WM8994_OVERSAMPLING, 0X0000);

	wm8994_set_codec_gain(codec, VOICECALL_MODE, VOICECALL_BT);

	/* Unmute DAC1 left */
	val = wm8994_read(codec, WM8994_DAC1_LEFT_VOLUME);
	val &= ~(WM8994_DAC1L_MUTE_MASK);
	wm8994_write(codec, WM8994_DAC1_LEFT_VOLUME, val);

	/* Unmute and volume ctrl RightDAC */
	val = wm8994_read(codec, WM8994_DAC1_RIGHT_VOLUME);
	val &= ~(WM8994_DAC1R_MUTE_MASK);
	wm8994_write(codec, WM8994_DAC1_RIGHT_VOLUME, val);

	wm8994_write(codec, WM8994_AIF1_DAC1_FILTERS_1, 0x0000);
	wm8994_write(codec, WM8994_AIF2_DAC_FILTERS_1, 0x0000);
}

int wm8994_set_codec_gain(struct snd_soc_codec *codec, u32 mode, u16 device)
{
	struct wm8994_priv *wm8994 = codec->drvdata;
	int i;
	u32 gain_set_bits = COMMON_SET_BIT;
	u16 val;
	struct gain_info_t *default_gain_table_p = NULL;
	int table_num = 0;

	if (wm8994->codecgain_reserve) {
		DEBUG_LOG("Tuning mode is On, Skip gain setting");
		return 0;
	}

	if (mode == PLAYBACK_MODE) {
		default_gain_table_p = playback_gain_table;
		table_num = PLAYBACK_GAIN_NUM;

		switch (device) {
		case PLAYBACK_RCV:
			gain_set_bits |= PLAYBACK_RCV;
			break;
		case PLAYBACK_SPK:
			gain_set_bits |= PLAYBACK_SPK;
			break;
		case PLAYBACK_HP:
			gain_set_bits |= PLAYBACK_HP;
			break;
		case PLAYBACK_BT:
			gain_set_bits |= PLAYBACK_BT;
			break;
		case PLAYBACK_SPK_HP:
			gain_set_bits |= PLAYBACK_SPK_HP;
			break;
		case PLAYBACK_RING_SPK:
			gain_set_bits |= (PLAYBACK_SPK | PLAYBACK_RING_SPK);
			break;
		case PLAYBACK_RING_HP:
			gain_set_bits |= (PLAYBACK_HP | PLAYBACK_RING_HP);
			break;
		case PLAYBACK_RING_SPK_HP:
			gain_set_bits |= (PLAYBACK_SPK_HP |
					PLAYBACK_RING_SPK_HP);
			break;
		case PLAYBACK_HP_NO_MIC:
			gain_set_bits |= PLAYBACK_HP_NO_MIC;
			break;
		case PLAYBACK_LINEOUT:
			gain_set_bits |= PLAYBACK_LINEOUT;
			break;
		default:
			pr_err("playback modo gain flag is wrong\n");
			break;
		}
	} else if (mode == VOICECALL_MODE) {
		default_gain_table_p = voicecall_gain_table;
		table_num = VOICECALL_GAIN_NUM;

		switch (device) {
		case VOICECALL_RCV:
			gain_set_bits |= VOICECALL_RCV;
			break;
		case VOICECALL_SPK:
			gain_set_bits |= VOICECALL_SPK;
			break;
		case VOICECALL_HP:
			gain_set_bits |= VOICECALL_HP;
			break;
		case VOICECALL_HP_NO_MIC:
			gain_set_bits |= VOICECALL_HP_NO_MIC;
			break;
		case VOICECALL_BT:
			gain_set_bits |= VOICECALL_BT;
			break;
		default:
			pr_err("voicemode gain flag is wrong\n");
		}
	} else if (mode  == RECORDING_MODE) {
		default_gain_table_p = recording_gain_table;
		table_num = RECORDING_GAIN_NUM;

		switch (device) {
		case RECORDING_MAIN:
			gain_set_bits |= RECORDING_MAIN;
			break;
		case RECORDING_HP:
			gain_set_bits |= RECORDING_HP;
			break;
		case RECORDING_BT:
			gain_set_bits |= RECORDING_BT;
			break;
		case RECORDING_REC_MAIN:
			gain_set_bits |= RECORDING_REC_MAIN;
			break;
		case RECORDING_REC_HP:
			gain_set_bits |= RECORDING_REC_HP;
			break;
		case RECORDING_REC_BT:
			gain_set_bits |= RECORDING_REC_BT;
			break;
		case RECORDING_CAM_MAIN:
			gain_set_bits |= RECORDING_CAM_MAIN;
			break;
		case RECORDING_CAM_HP:
			gain_set_bits |= RECORDING_CAM_HP;
			break;
		case RECORDING_CAM_BT:
			gain_set_bits |= RECORDING_CAM_BT;
			break;
		default:
			pr_err("recording gain flag is wrong\n");
		}
	}
	if (mode == VOIPCALL_MODE) {
		default_gain_table_p = voipcall_gain_table;
		table_num = VOIPCALL_GAIN_NUM;

		switch (device) {
		case VOIPCALL_SPK:
			gain_set_bits |= VOIPCALL_SPK;
			break;
		case VOIPCALL_HP:
			gain_set_bits |= VOIPCALL_HP;
			break;
		case VOIPCALL_MAINMIC:
			gain_set_bits |= VOIPCALL_MAINMIC;
			break;
		case VOIPCALL_HPMIC:
			gain_set_bits |= VOIPCALL_HPMIC;
			break;
		case VOIPCALL_HP_OTHER:
			gain_set_bits |= VOIPCALL_HP_OTHER;
			break;
		case VOIPCALL_HPMIC_OTHER:
			gain_set_bits |= VOIPCALL_HPMIC_OTHER;
			break;
		case VOIPCALL_SPK_OTHER:
			gain_set_bits |= VOIPCALL_SPK_OTHER;
			break;
		case VOIPCALL_MAINMIC_OTHER:
			gain_set_bits |= VOIPCALL_MAINMIC_OTHER;
			break;			
		default:
			pr_err("voipcall gain flag is wrong\n");

		}
	}

	DEBUG_LOG("Set gain mode=0x%x, device=0x%x, gain_bits=0x%x, table_num=%d, gain_code=%d",
		mode, device, gain_set_bits, table_num, wm8994->gain_code);

	/* WM8994 filter setting */
	if (mode == PLAYBACK_MODE &&
			(device == PLAYBACK_SPK || device == PLAYBACK_RING_SPK))
		wm8994_filter_SPK(codec);
	else if (mode == RECORDING_MODE &&
			device == RECORDING_CAM_MAIN)
		wm8994_filter_REC(codec);
	else if (mode == VOIPCALL_MODE &&
			(device == VOIPCALL_SPK || device == VOIPCALL_MAINMIC ||
			 device == VOIPCALL_SPK_OTHER || device == VOIPCALL_MAINMIC_OTHER))
		wm8994_filter_VOIP(codec);
	else
		wm8994_filter_off(codec);

	/* default gain table setting */
	for (i = 0; i < table_num; i++) {
		if ((default_gain_table_p + i)->mode & gain_set_bits) {
			val = wm8994_read(codec, (default_gain_table_p + i)->reg);
			val &= ~((default_gain_table_p + i)->mask);
			val |= (default_gain_table_p + i)->gain;
			wm8994_write(codec, (default_gain_table_p + i)->reg, val);
		}
	}

	if (wm8994->gain_code) {
		gain_set_bits &= ~(COMMON_SET_BIT);
		gain_set_bits |= (mode | GAIN_DIVISION_BIT);
		default_gain_table_p = gain_code_table;
		table_num = GAIN_CODE_NUM;

		for (i = 0; i < table_num; i++) {
			if ((default_gain_table_p + i)->mode == gain_set_bits) {
				val = wm8994_read(codec, (default_gain_table_p + i)->reg);
				val &= ~((default_gain_table_p + i)->mask);
				val |= (default_gain_table_p + i)->gain;
				wm8994_write(codec, (default_gain_table_p + i)->reg, val);
			}
		}

	}
	return 0;

}

u16 wm8994_get_codec_gain(u32 mode, u16 device, u16 reg)
{
	int i;
	u16 gain = 0;
	struct gain_info_t *default_gain_table_p = NULL;
	int table_num = 0;

	DEBUG_LOG("");

	switch (mode) {

	case PLAYBACK_MODE:
		default_gain_table_p = playback_gain_table;
		table_num = PLAYBACK_GAIN_NUM;
		break;

	default:
		DEBUG_LOG("Not support [%d]mode", mode);
		return 0;
		break;
	}

	for (i = 0; i < table_num; i++) {
		if ((default_gain_table_p + i)->mode == device) {
			if ((default_gain_table_p + i)->reg == reg) {
				gain = ((default_gain_table_p + i)->gain);
				break;
			}
		}
	}

	return gain;
}
