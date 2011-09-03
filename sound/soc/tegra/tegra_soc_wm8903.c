/*
 * tegra_soc_wm8903.c  --  SoC audio for tegra
 *
 * (c) 2010-2011 Nvidia Graphics Pvt. Ltd.
 *  http://www.nvidia.com
 *
 * Copyright 2007 Wolfson Microelectronics PLC.
 * Author: Graeme Gregory
 *         graeme.gregory@wolfsonmicro.com or linux@wolfsonmicro.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include "tegra_soc.h"
#include <linux/gpio.h>
#include <sound/soc-dapm.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include "../codecs/wm8903.h"

static struct platform_device *tegra_snd_device;

extern int en_dmic;

extern struct snd_soc_dai tegra_i2s_dai[];
extern struct snd_soc_dai tegra_spdif_dai;
extern struct snd_soc_dai tegra_generic_codec_dai[];
extern struct snd_soc_platform tegra_soc_platform;
extern struct wired_jack_conf tegra_wired_jack_conf;

/* codec register values */
#define B00_IN_VOL		0
#define B00_INR_ENA		0
#define B01_INL_ENA		1
#define B01_MICDET_ENA		1
#define B00_MICBIAS_ENA		0
#define B15_DRC_ENA		15
#define B01_ADCL_ENA		1
#define B00_ADCR_ENA		0
#define B06_IN_CM_ENA		6
#define B04_IP_SEL_N		4
#define B02_IP_SEL_P		2
#define B00_MODE 		0
#define B06_AIF_ADCL		7
#define B06_AIF_ADCR		6
#define B04_ADC_HPF_ENA		4
#define R20_SIDETONE_CTRL	32
#define R29_DRC_1		41

#define B08_GPx_FN		8
#define B07_GPx_DIR		7

#define DMIC_CLK_OUT		(0x6 << B08_GPx_FN)
#define DMIC_DAT_DATA_IN	(0x6 << B08_GPx_FN)
#define GPIO_DIR_OUT		(0x0 << B07_GPx_DIR)
#define GPIO_DIR_IN			(0x1 << B07_GPx_DIR)

#define ADC_DIGITAL_VOL_9DB		0x1D8
#define ADC_DIGITAL_VOL_12DB	0x1E0
#define ADC_ANALOG_VOLUME		0x1C
#define DRC_MAX_36DB			0x03

#define SET_REG_VAL(r,m,l,v) (((r)&(~((m)<<(l))))|(((v)&(m))<<(l)))

static ssize_t digital_mic_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return sprintf(buf, "%d\n", en_dmic);
}

static ssize_t digital_mic_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	if (count > 3) {
		pr_err("%s: buffer size %d too big\n", __func__, count);
		return -EINVAL;
	}

	if (sscanf(buf, "%d", &en_dmic) != 1) {
		pr_err("%s: invalid input string [%s]\n", __func__, buf);
		return -EINVAL;
	}
	return count;
}

static DEVICE_ATTR(enable_digital_mic, 0644, digital_mic_show, digital_mic_store);

static void configure_dmic(struct snd_soc_codec *codec)
{
	u16 test4, reg;

	if (en_dmic) {
		/* Set GP1_FN as DMIC_LR */
		snd_soc_write(codec, WM8903_GPIO_CONTROL_1,
					DMIC_CLK_OUT | GPIO_DIR_OUT);

		/* Set GP2_FN as DMIC_DAT */
		snd_soc_write(codec, WM8903_GPIO_CONTROL_2,
					DMIC_DAT_DATA_IN | GPIO_DIR_IN);

		/* Enable ADC Digital volumes */
		snd_soc_write(codec, WM8903_ADC_DIGITAL_VOLUME_LEFT,
					ADC_DIGITAL_VOL_9DB);
		snd_soc_write(codec, WM8903_ADC_DIGITAL_VOLUME_RIGHT,
					ADC_DIGITAL_VOL_9DB);

		/* Enable DIG_MIC */
		test4 = WM8903_ADC_DIG_MIC;
	} else {
		/* Disable DIG_MIC */
		test4 = snd_soc_read(codec, WM8903_CLOCK_RATE_TEST_4);
		test4 &= ~WM8903_ADC_DIG_MIC;
	}

	reg = snd_soc_read(codec, WM8903_CONTROL_INTERFACE_TEST_1);
	snd_soc_write(codec, WM8903_CONTROL_INTERFACE_TEST_1,
			 reg | WM8903_TEST_KEY);
	snd_soc_write(codec, WM8903_CLOCK_RATE_TEST_4, test4);
	snd_soc_write(codec, WM8903_CONTROL_INTERFACE_TEST_1, reg);

}

static int tegra_hifi_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	struct snd_soc_codec *codec = codec_dai->codec;
	struct tegra_audio_data* audio_data = rtd->socdev->codec_data;
	enum dac_dap_data_format data_fmt;
	int dai_flag = 0, sys_clk;
	int err;

	if (tegra_das_is_port_master(tegra_audio_codec_type_hifi))
		dai_flag |= SND_SOC_DAIFMT_CBM_CFM;
	else
		dai_flag |= SND_SOC_DAIFMT_CBS_CFS;

	data_fmt = tegra_das_get_codec_data_fmt(tegra_audio_codec_type_hifi);

	/* We are supporting DSP and I2s format for now */
	if (data_fmt & dac_dap_data_format_i2s)
		dai_flag |= SND_SOC_DAIFMT_I2S;
	else
		dai_flag |= SND_SOC_DAIFMT_DSP_A;

	err = snd_soc_dai_set_fmt(codec_dai, dai_flag);
	if (err < 0) {
		pr_err("codec_dai fmt not set \n");
		return err;
	}

	err = snd_soc_dai_set_fmt(cpu_dai, dai_flag);
	if (err < 0) {
		pr_err("cpu_dai fmt not set \n");
		return err;
	}

	sys_clk = clk_get_rate(audio_data->dap_mclk);
	err = snd_soc_dai_set_sysclk(codec_dai, 0, sys_clk, SND_SOC_CLOCK_IN);
	if (err < 0) {
		pr_err("codec_dai clock not set\n");
		return err;
	}

	err = snd_soc_dai_set_sysclk(cpu_dai, 0, sys_clk, SND_SOC_CLOCK_IN);
	if (err < 0) {
		pr_err("cpu_dai clock not set\n");
		return err;
	}

	if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK) {
		int CtrlReg = 0;
		int VolumeCtrlReg = 0;
		int SidetoneCtrlReg = 0;

		snd_soc_write(codec, WM8903_ANALOGUE_LEFT_INPUT_0, 0X7);
		snd_soc_write(codec, WM8903_ANALOGUE_RIGHT_INPUT_0, 0X7);
		/* Mic Bias enable */
		CtrlReg = (0x1<<B00_MICBIAS_ENA) | (0x1<<B01_MICDET_ENA);
		snd_soc_write(codec, WM8903_MIC_BIAS_CONTROL_0, CtrlReg);
		/* Enable DRC */
		CtrlReg = snd_soc_read(codec, WM8903_DRC_0);
		CtrlReg |= (1<<B15_DRC_ENA);
		snd_soc_write(codec, WM8903_DRC_0, CtrlReg);
		/* Single Ended Mic */
		CtrlReg = (0x0<<B06_IN_CM_ENA) |
			(0x0<<B00_MODE) | (0x0<<B04_IP_SEL_N)
					| (0x1<<B02_IP_SEL_P);
		VolumeCtrlReg = (0x1C << B00_IN_VOL);
		/* Mic Setting */
		snd_soc_write(codec, WM8903_ANALOGUE_LEFT_INPUT_1, CtrlReg);
		snd_soc_write(codec, WM8903_ANALOGUE_RIGHT_INPUT_1, CtrlReg);
		/* voulme for single ended mic */
		snd_soc_write(codec, WM8903_ANALOGUE_LEFT_INPUT_0,
				VolumeCtrlReg);
		snd_soc_write(codec, WM8903_ANALOGUE_RIGHT_INPUT_0,
				VolumeCtrlReg);
		/* replicate mic setting on both channels */
		CtrlReg = snd_soc_read(codec, WM8903_AUDIO_INTERFACE_0);
		CtrlReg  = SET_REG_VAL(CtrlReg, 0x1, B06_AIF_ADCR, 0x0);
		CtrlReg  = SET_REG_VAL(CtrlReg, 0x1, B06_AIF_ADCL, 0x0);
		snd_soc_write(codec, WM8903_AUDIO_INTERFACE_0, CtrlReg);
		/* Enable analog inputs */
		CtrlReg = (0x1<<B01_INL_ENA) | (0x1<<B00_INR_ENA);
		snd_soc_write(codec, WM8903_POWER_MANAGEMENT_0, CtrlReg);
		/* ADC Settings */
		CtrlReg = snd_soc_read(codec, WM8903_ADC_DIGITAL_0);
		CtrlReg |= (0x1<<B04_ADC_HPF_ENA);
		snd_soc_write(codec, WM8903_ADC_DIGITAL_0, CtrlReg);
		SidetoneCtrlReg = 0;
		snd_soc_write(codec, R20_SIDETONE_CTRL, SidetoneCtrlReg);
		/* Enable ADC */
		CtrlReg = snd_soc_read(codec, WM8903_POWER_MANAGEMENT_6);
		CtrlReg |= (0x1<<B00_ADCR_ENA)|(0x1<<B01_ADCL_ENA);
		snd_soc_write(codec, WM8903_POWER_MANAGEMENT_6, CtrlReg);
		CtrlReg = snd_soc_read(codec, R29_DRC_1);
		CtrlReg |= 0x3; /*mic volume 18 db */
		snd_soc_write(codec, R29_DRC_1, CtrlReg);

		configure_dmic(codec);

	}

	return 0;
}

static int tegra_voice_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	struct tegra_audio_data* audio_data = rtd->socdev->codec_data;
	enum dac_dap_data_format data_fmt;
	int dai_flag = 0, sys_clk;
	int err;

	if (tegra_das_is_port_master(tegra_audio_codec_type_bluetooth))
		dai_flag |= SND_SOC_DAIFMT_CBM_CFM;
	else
		dai_flag |= SND_SOC_DAIFMT_CBS_CFS;

	data_fmt = tegra_das_get_codec_data_fmt(tegra_audio_codec_type_bluetooth);

	/* We are supporting DSP and I2s format for now */
	if (data_fmt & dac_dap_data_format_dsp)
		dai_flag |= SND_SOC_DAIFMT_DSP_A;
	else
		dai_flag |= SND_SOC_DAIFMT_I2S;

	err = snd_soc_dai_set_fmt(codec_dai, dai_flag);
	if (err < 0) {
		pr_err("codec_dai fmt not set \n");
		return err;
	}

	err = snd_soc_dai_set_fmt(cpu_dai, dai_flag);
	if (err < 0) {
		pr_err("cpu_dai fmt not set \n");
		return err;
	}

	sys_clk = clk_get_rate(audio_data->dap_mclk);
	err = snd_soc_dai_set_sysclk(codec_dai, 0, sys_clk, SND_SOC_CLOCK_IN);
	if (err < 0) {
		pr_err("cpu_dai clock not set\n");
		return err;
	}

	err = snd_soc_dai_set_sysclk(cpu_dai, 0, sys_clk, SND_SOC_CLOCK_IN);
	if (err < 0) {
		pr_err("cpu_dai clock not set\n");
		return err;
	}

	return 0;
}

static int tegra_spdif_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	return 0;
}

int tegra_codec_startup(struct snd_pcm_substream *substream)
{
	tegra_das_power_mode(true);

	return 0;
}

void tegra_codec_shutdown(struct snd_pcm_substream *substream)
{
	tegra_das_power_mode(false);
}

int tegra_soc_suspend_pre(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

int tegra_soc_suspend_post(struct platform_device *pdev, pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct tegra_audio_data* audio_data = socdev->codec_data;

	clk_disable(audio_data->dap_mclk);

	return 0;
}

int tegra_soc_resume_pre(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct tegra_audio_data* audio_data = socdev->codec_data;

	clk_enable(audio_data->dap_mclk);

	return 0;
}

int tegra_soc_resume_post(struct platform_device *pdev)
{
	return 0;
}

static struct snd_soc_ops tegra_hifi_ops = {
	.hw_params = tegra_hifi_hw_params,
	.startup = tegra_codec_startup,
	.shutdown = tegra_codec_shutdown,
};

static struct snd_soc_ops tegra_voice_ops = {
	.hw_params = tegra_voice_hw_params,
	.startup = tegra_codec_startup,
	.shutdown = tegra_codec_shutdown,
};

static struct snd_soc_ops tegra_spdif_ops = {
	.hw_params = tegra_spdif_hw_params,
};

void tegra_ext_control(struct snd_soc_codec *codec, int new_con)
{
	struct tegra_audio_data* audio_data = codec->socdev->codec_data;

	/* Disconnect old codec routes and connect new routes*/
	if (new_con & TEGRA_HEADPHONE)
		snd_soc_dapm_enable_pin(codec, "Headphone");
	else
		snd_soc_dapm_disable_pin(codec, "Headphone");

	if (new_con & (TEGRA_LINEOUT | TEGRA_EAR_SPK))
		snd_soc_dapm_enable_pin(codec, "Lineout");
	else
		snd_soc_dapm_disable_pin(codec, "Lineout");

	if (new_con & TEGRA_SPK)
		snd_soc_dapm_enable_pin(codec, "Int Spk");
	else
		snd_soc_dapm_disable_pin(codec, "Int Spk");

	if (new_con & TEGRA_INT_MIC)
		snd_soc_dapm_enable_pin(codec, "Int Mic");
	else
		snd_soc_dapm_disable_pin(codec, "Int Mic");

	if (new_con & TEGRA_EXT_MIC)
		snd_soc_dapm_enable_pin(codec, "Ext Mic");
	else
		snd_soc_dapm_disable_pin(codec, "Ext Mic");

	if (new_con & TEGRA_LINEIN)
		snd_soc_dapm_enable_pin(codec, "Linein");
	else
		snd_soc_dapm_disable_pin(codec, "Linein");

	if (new_con & TEGRA_HEADSET)
		snd_soc_dapm_enable_pin(codec, "Headset");
	else
		snd_soc_dapm_disable_pin(codec, "Headset");

	/* signal a DAPM event */
	snd_soc_dapm_sync(codec);
	audio_data->codec_con = new_con;
}

static int tegra_dapm_event_int_spk(struct snd_soc_dapm_widget* w,
				    struct snd_kcontrol* k, int event)
{
	if (tegra_wired_jack_conf.en_spkr != -1) {
		if (tegra_wired_jack_conf.amp_reg) {
			if (SND_SOC_DAPM_EVENT_ON(event) &&
				!tegra_wired_jack_conf.amp_reg_enabled) {
				regulator_enable(tegra_wired_jack_conf.amp_reg);
				tegra_wired_jack_conf.amp_reg_enabled = 1;
			}
			else if (!SND_SOC_DAPM_EVENT_ON(event) &&
				tegra_wired_jack_conf.amp_reg_enabled) {
				regulator_disable(tegra_wired_jack_conf.amp_reg);
				tegra_wired_jack_conf.amp_reg_enabled = 0;
			}
		}

		gpio_set_value_cansleep(tegra_wired_jack_conf.en_spkr,
			SND_SOC_DAPM_EVENT_ON(event) ? 1 : 0);
	}

	return 0;
}

static int tegra_dapm_event_int_mic(struct snd_soc_dapm_widget* w,
				    struct snd_kcontrol* k, int event)
{
	if (tegra_wired_jack_conf.en_mic_int != -1)
		gpio_set_value_cansleep(tegra_wired_jack_conf.en_mic_int,
			SND_SOC_DAPM_EVENT_ON(event) ? 1 : 0);

	if (tegra_wired_jack_conf.en_mic_ext != -1)
		gpio_set_value_cansleep(tegra_wired_jack_conf.en_mic_ext,
			SND_SOC_DAPM_EVENT_ON(event) ? 0 : 1);

	return 0;
}

static int tegra_dapm_event_ext_mic(struct snd_soc_dapm_widget* w,
				    struct snd_kcontrol* k, int event)
{
	if (tegra_wired_jack_conf.en_mic_ext != -1)
		gpio_set_value_cansleep(tegra_wired_jack_conf.en_mic_ext,
			SND_SOC_DAPM_EVENT_ON(event) ? 1 : 0);

	if (tegra_wired_jack_conf.en_mic_int != -1)
		gpio_set_value_cansleep(tegra_wired_jack_conf.en_mic_int,
			SND_SOC_DAPM_EVENT_ON(event) ? 0 : 1);

	return 0;
}

/*tegra machine dapm widgets */
static const struct snd_soc_dapm_widget tegra_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_HP("Headset", NULL),
	SND_SOC_DAPM_SPK("Lineout", NULL),
	SND_SOC_DAPM_SPK("Int Spk", tegra_dapm_event_int_spk),
	SND_SOC_DAPM_MIC("Ext Mic", tegra_dapm_event_ext_mic),
	SND_SOC_DAPM_MIC("Int Mic", tegra_dapm_event_int_mic),
	SND_SOC_DAPM_LINE("Linein", NULL),
};

/* Tegra machine audio map (connections to the codec pins) */
static const struct snd_soc_dapm_route audio_map[] = {

	/* headphone connected to LHPOUT1, RHPOUT1 */
	{"Headphone", NULL, "HPOUTR"},
	{"Headphone", NULL, "HPOUTL"},

	/* headset Jack  - in = micin, out = HPOUT*/
	{"Headset", NULL, "HPOUTR"},
	{"Headset", NULL, "HPOUTL"},
	{"IN1L", NULL, "Headset"},
	{"IN1R", NULL, "Headset"},

	/* lineout connected to LINEOUTR and LINEOUTL */
	{"Lineout", NULL, "LINEOUTR"},
	{"Lineout", NULL, "LINEOUTL"},

	/* build-in speaker connected to LON/P RON/P */
	{"Int Spk", NULL, "RON"},
	{"Int Spk", NULL, "ROP"},
	{"Int Spk", NULL, "LON"},
	{"Int Spk", NULL, "LOP"},

	/* internal mic is mono */
	{"IN1R", NULL, "Int Mic"},

	/* external mic is stero */
	{"IN1L", NULL, "Ext Mic"},
	{"IN1R", NULL, "Ext Mic"},

	/* Line In */
	{"IN3L", NULL, "Linein"},
	{"IN3R", NULL, "Linein"},
};


static int tegra_codec_init(struct snd_soc_codec *codec)
{
	struct tegra_audio_data* audio_data = codec->socdev->codec_data;
	int err = 0;

	if (!audio_data->init_done) {
		audio_data->dap_mclk = tegra_das_get_dap_mclk();
		if (!audio_data->dap_mclk) {
			pr_err("Failed to get dap mclk \n");
			err = -ENODEV;
			return err;
		}
		clk_enable(audio_data->dap_mclk);

		/* Add tegra specific widgets */
		snd_soc_dapm_new_controls(codec, tegra_dapm_widgets,
					ARRAY_SIZE(tegra_dapm_widgets));

		/* Set up tegra specific audio path audio_map */
		snd_soc_dapm_add_routes(codec, audio_map,
					ARRAY_SIZE(audio_map));

		/* Add jack detection */
		err = tegra_jack_init(codec);
		if (err < 0) {
			pr_err("Failed in jack init \n");
			return err;
		}

		/* Default to OFF */
		tegra_ext_control(codec, TEGRA_AUDIO_OFF);

		err = tegra_controls_init(codec);
		if (err < 0) {
			pr_err("Failed in controls init \n");
			return err;
		}

		audio_data->codec = codec;
		audio_data->init_done = 1;
	}

	return err;
}

static struct snd_soc_dai_link tegra_soc_dai[] = {
	{
		.name = "WM8903",
		.stream_name = "WM8903 HiFi",
		.cpu_dai = &tegra_i2s_dai[0],
		.codec_dai = &wm8903_dai,
		.init = tegra_codec_init,
		.ops = &tegra_hifi_ops,
	},
	{
		.name = "Tegra-generic",
		.stream_name = "Tegra Generic Voice",
		.cpu_dai = &tegra_i2s_dai[1],
		.codec_dai = &tegra_generic_codec_dai[0],
		.init = tegra_codec_init,
		.ops = &tegra_voice_ops,
	},
	{
		.name = "Tegra-spdif",
		.stream_name = "Tegra Spdif",
		.cpu_dai = &tegra_spdif_dai,
		.codec_dai = &tegra_generic_codec_dai[1],
		.init = tegra_codec_init,
		.ops = &tegra_spdif_ops,
	},
};

static struct tegra_audio_data audio_data = {
	.init_done = 0,
	.play_device = TEGRA_AUDIO_DEVICE_NONE,
	.capture_device = TEGRA_AUDIO_DEVICE_NONE,
	.is_call_mode = false,
	.codec_con = TEGRA_AUDIO_OFF,
};

static struct snd_soc_card tegra_snd_soc = {
	.name = "tegra",
	.platform = &tegra_soc_platform,
	.dai_link = tegra_soc_dai,
	.num_links = ARRAY_SIZE(tegra_soc_dai),
	.suspend_pre = tegra_soc_suspend_pre,
	.suspend_post = tegra_soc_suspend_post,
	.resume_pre = tegra_soc_resume_pre,
	.resume_post = tegra_soc_resume_post,
};

static struct snd_soc_device tegra_snd_devdata = {
	.card = &tegra_snd_soc,
	.codec_dev = &soc_codec_dev_wm8903,
	.codec_data = &audio_data,
};

static int __init tegra_init(void)
{
	int ret = 0;

	tegra_snd_device = platform_device_alloc("soc-audio", -1);
	if (!tegra_snd_device) {
		pr_err("failed to allocate soc-audio \n");
		return -ENOMEM;
	}

	platform_set_drvdata(tegra_snd_device, &tegra_snd_devdata);
	tegra_snd_devdata.dev = &tegra_snd_device->dev;

	ret = platform_device_add(tegra_snd_device);
	if (ret) {
		pr_err("audio device could not be added \n");
		goto fail;
	}

	ret = device_create_file(&tegra_snd_device->dev,
							&dev_attr_enable_digital_mic);
	if (ret < 0) {
		dev_err(&tegra_snd_device->dev,
				"%s: could not create sysfs entry %s: %d\n",
				__func__, dev_attr_enable_digital_mic.attr.name, ret);
		return ret;
	}

	return 0;

fail:
	if (tegra_snd_device) {
		platform_device_put(tegra_snd_device);
		tegra_snd_device = 0;
	}

	return ret;
}

static void __exit tegra_exit(void)
{
	tegra_jack_exit();
	platform_device_unregister(tegra_snd_device);
}

module_init(tegra_init);
module_exit(tegra_exit);

/* Module information */
MODULE_DESCRIPTION("Tegra ALSA SoC");
MODULE_LICENSE("GPL");
