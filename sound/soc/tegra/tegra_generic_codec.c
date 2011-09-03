 /*
  * tegra_generic_codec.c  --  Generic codec interface for tegra
  *
  * Copyright  2011 Nvidia Graphics Pvt. Ltd.
  *
  * Author: Sumit Bhattacharya
  *             sumitb@nvidia.com
  *  http://www.nvidia.com
  *
  *
  * This program is free software; you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation; either version 2 of the License, or
  * (at your option) any later version.
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

#include "tegra_soc.h"

static struct snd_soc_codec* tegra_generic_codec;
static struct platform_device* tegra_generic_codec_dev;

// Stubbed implementations of generic codec ops
static int tegra_generic_codec_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	return 0;
}

static void tegra_generic_codec_shutdown(struct snd_pcm_substream *substream,
			    struct snd_soc_dai *dai)
{
	return;
}
static int tegra_generic_codec_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	return 0;
}

static int tegra_generic_codec_mute(struct snd_soc_dai *dai, int mute)
{
	return 0;
}


static int tegra_generic_codec_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	return 0;
}

static int tegra_generic_codec_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	return 0;
}

static struct snd_soc_dai_ops tegra_generic_codec_stub_ops = {
	.startup	= tegra_generic_codec_startup,
	.shutdown	= tegra_generic_codec_shutdown,
	.hw_params	= tegra_generic_codec_hw_params,
	.digital_mute	= tegra_generic_codec_mute,
	.set_fmt	= tegra_generic_codec_set_dai_fmt,
	.set_sysclk	= tegra_generic_codec_set_dai_sysclk,
};

struct snd_soc_dai tegra_generic_codec_dai[] = {
	{
		.name = "tegra_generic_voice_codec",
		.id = 0,
		.playback = {
			.stream_name    = "Playback",
			.channels_min   = 1,
			.channels_max   = 1,
			.rates          = TEGRA_VOICE_SAMPLE_RATES,
			.formats        = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.capture = {
			.stream_name    = "Capture",
			.channels_min   = 1,
			.channels_max   = 1,
			.rates          = TEGRA_VOICE_SAMPLE_RATES,
			.formats        = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.ops = &tegra_generic_codec_stub_ops,
	}
};
EXPORT_SYMBOL_GPL(tegra_generic_codec_dai);

static int generic_codec_init(struct platform_device *pdev)
{
	int ret = 0;

	tegra_generic_codec = kzalloc(sizeof(struct snd_soc_codec), GFP_KERNEL);
	if (!tegra_generic_codec)
		return -ENOMEM;

	mutex_init(&tegra_generic_codec->mutex);

	tegra_generic_codec->dev = &pdev->dev;
	tegra_generic_codec->name = "tegra_generic_codec";
	tegra_generic_codec->owner = THIS_MODULE;
	tegra_generic_codec->dai = tegra_generic_codec_dai;
	tegra_generic_codec->num_dai = ARRAY_SIZE(tegra_generic_codec_dai);
	tegra_generic_codec->write = NULL;
	tegra_generic_codec->read = NULL;
	tegra_generic_codec_dai[0].dev = &pdev->dev;
	INIT_LIST_HEAD(&tegra_generic_codec->dapm_widgets);
	INIT_LIST_HEAD(&tegra_generic_codec->dapm_paths);

	ret = snd_soc_register_codec(tegra_generic_codec);
	if (ret != 0) {
		pr_err("codec: failed to register tegra_generic_codec\n");
		goto codec_err;
	}

	ret = snd_soc_register_dais(tegra_generic_codec_dai, ARRAY_SIZE(tegra_generic_codec_dai));
	if (ret != 0) {
		pr_err("codec: failed to register dais\n");
		goto dai_err;
	}

	return ret;
dai_err:
	snd_soc_unregister_codec(tegra_generic_codec);
codec_err:
	kfree(tegra_generic_codec);
	tegra_generic_codec = NULL;

	return ret;
}


static int generic_codec_remove(struct platform_device *pdev)
{
	if (!tegra_generic_codec)
		return 0;

	snd_soc_unregister_dais(tegra_generic_codec_dai, ARRAY_SIZE(tegra_generic_codec_dai));
	snd_soc_unregister_codec(tegra_generic_codec);
	kfree(tegra_generic_codec);
	tegra_generic_codec = NULL;
	tegra_generic_codec_dai[0].dev = NULL;

	return 0;
}

static int __init tegra_generic_codec_init(void)
{
	int ret = 0;

	tegra_generic_codec_dev =
		platform_device_register_simple("tegra_generic_codec", -1, NULL, 0);
	if (!tegra_generic_codec_dev)
		return -ENOMEM;

	ret = generic_codec_init(tegra_generic_codec_dev);
	if (ret != 0)
		goto codec_err;

	return 0;

codec_err:
	platform_device_unregister(tegra_generic_codec_dev);
	tegra_generic_codec_dev = 0;
	return ret;
}

static void __exit tegra_generic_codec_exit(void)
{
	generic_codec_remove(tegra_generic_codec_dev);
	platform_device_unregister(tegra_generic_codec_dev);
	tegra_generic_codec_dev = 0;
}

module_init(tegra_generic_codec_init);
module_exit(tegra_generic_codec_exit);

/* Module information */
MODULE_DESCRIPTION("Tegra ALSA Generic Codec Interface");
MODULE_LICENSE("GPL");

