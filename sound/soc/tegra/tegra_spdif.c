/*
 * tegra_spdif.c  --  ALSA Soc Audio Layer
 *
 * Copyright (c) 2010-2011, NVIDIA Corporation.
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

/* spdif controller */
struct tegra_spdif_info {
	struct platform_device *pdev;
	struct tegra_audio_platform_data *pdata;
	struct clk *spdif_clk;
	unsigned long spdif_phys;
	unsigned long spdif_base;

	unsigned long dma_req_sel;
	int irq;

	int ref_count;
	struct spdif_regs_cache spdif_regs;
};

void setup_spdif_dma_request(struct snd_pcm_substream *substream,
			struct tegra_dma_req *req,
			void (*dma_callback)(struct tegra_dma_req *req),
			void *dma_data)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	struct tegra_spdif_info *info = cpu_dai->private_data;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		req->to_memory = false;
		req->dest_addr = spdif_get_fifo_phy_base(info->spdif_phys,
								AUDIO_TX_MODE);
		req->dest_wrap = 4;
		req->source_wrap = 0;
		req->dest_bus_width = 32;
		req->source_bus_width = 32;
	} else {
		req->to_memory = true;
		req->dest_addr = spdif_get_fifo_phy_base(info->spdif_phys,
								AUDIO_RX_MODE);
		req->dest_wrap = 0;
		req->source_wrap = 4;
		req->dest_bus_width = 32;
		req->source_bus_width = 32;
	}
	req->complete = dma_callback;
	req->dev = dma_data;
	req->req_sel = info->dma_req_sel;

	return;
}

/* playback */
static inline void start_spdif_playback(struct snd_soc_dai *dai)
{
	struct tegra_spdif_info *info = dai->private_data;

	spdif_fifo_set_attention_level(info->spdif_base, AUDIO_TX_MODE,
					SPDIF_FIFO_ATN_LVL_FOUR_SLOTS);
	spdif_fifo_enable(info->spdif_base, AUDIO_TX_MODE, true);
}

static inline void stop_spdif_playback(struct snd_soc_dai *dai)
{
	struct tegra_spdif_info *info = dai->private_data;

	spdif_fifo_enable(info->spdif_base, AUDIO_TX_MODE, false);
	while (spdif_get_status(info->spdif_base) & SPDIF_STATUS_0_TX_BSY);
}

/* capture */
static inline void start_spdif_capture(struct snd_soc_dai *dai)
{
	struct tegra_spdif_info *info = dai->private_data;

	spdif_fifo_set_attention_level(info->spdif_base, AUDIO_RX_MODE,
					SPDIF_FIFO_ATN_LVL_FOUR_SLOTS);
	spdif_fifo_enable(info->spdif_base, AUDIO_RX_MODE, true);
}

static inline void stop_spdif_capture(struct snd_soc_dai *dai)
{
	struct tegra_spdif_info *info = dai->private_data;

	spdif_fifo_enable(info->spdif_base, AUDIO_RX_MODE, false);
	while (spdif_get_status(info->spdif_base) & SPDIF_STATUS_0_RX_BSY);
}

static int tegra_spdif_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct tegra_spdif_info *info = dai->private_data;
	int val;
	unsigned int rate, sample_size;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		val = SPDIF_BIT_MODE_MODE16BIT;
		sample_size = 16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val = SPDIF_BIT_MODE_MODE24BIT;
		sample_size = 16;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		val = SPDIF_BIT_MODE_MODERAW;
		sample_size = 32;
		break;
	default:
		return -EINVAL;
	}

	spdif_set_bit_mode(info->spdif_base, val);
	spdif_set_fifo_packed(info->spdif_base, 1);

	switch (params_rate(params)) {
	case 8000:
	case 32000:
	case 44100:
	case 48000:
	case 88200:
	case 96000:
		val = params_rate(params);
		break;
	default:
		return -EINVAL;
	}

	/* Min BCLK = samplerate * channel * bits per sample * 4 */
	rate = val * params_channels(params) * sample_size * 4;

	/* Ensure Spdif clk rate is atleast greater than min BCLK */
	clk_set_rate(info->spdif_clk, rate);
	if (clk_get_rate(info->spdif_clk) < rate)
		clk_set_rate(info->spdif_clk, rate << 1);

	spdif_set_sample_rate(info->spdif_base, val);

	return 0;
}


static int tegra_spdif_set_dai_fmt(struct snd_soc_dai *cpu_dai,
					unsigned int fmt)
{
	return 0;
}

static int tegra_spdif_set_dai_sysclk(struct snd_soc_dai *cpu_dai,
					int clk_id, unsigned int freq, int dir)
{
	return 0;
}

static int tegra_spdif_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			start_spdif_playback(dai);
		else
			start_spdif_capture(dai);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			stop_spdif_playback(dai);
		else
			stop_spdif_capture(dai);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

#ifdef CONFIG_PM
int tegra_spdif_suspend(struct snd_soc_dai *cpu_dai)
{
	struct tegra_spdif_info* info = cpu_dai->private_data;

	spdif_get_all_regs(info->spdif_base, &info->spdif_regs);
	return 0;
}

int tegra_spdif_resume(struct snd_soc_dai *cpu_dai)
{
	struct tegra_spdif_info* info = cpu_dai->private_data;

	spdif_set_all_regs(info->spdif_base, &info->spdif_regs);
	return 0;
}

#else
#define tegra_spdif_suspend	NULL
#define tegra_spdif_resume	NULL
#endif

static int tegra_spdif_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct tegra_spdif_info *info = dai->private_data;

	if (!info->ref_count)
		clk_enable(info->spdif_clk);

	info->ref_count++;
	return 0;
}

static void tegra_spdif_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct tegra_spdif_info *info = dai->private_data;

	if (info->ref_count > 0)
		info->ref_count--;

	if (!info->ref_count)
		clk_disable(info->spdif_clk);

	return;
}

static int tegra_spdif_probe(struct platform_device *pdev,
				struct snd_soc_dai *dai)
{
	return 0;
}

static struct snd_soc_dai_ops tegra_spdif_dai_ops = {
	.startup	= tegra_spdif_startup,
	.shutdown	= tegra_spdif_shutdown,
	.trigger	= tegra_spdif_trigger,
	.hw_params	= tegra_spdif_hw_params,
	.set_fmt	= tegra_spdif_set_dai_fmt,
	.set_sysclk	= tegra_spdif_set_dai_sysclk,
};

struct snd_soc_dai tegra_spdif_dai = {
	.name = "tegra-spdif",
	.id = 0,
	.probe = tegra_spdif_probe,
	.suspend = tegra_spdif_suspend,
	.resume = tegra_spdif_resume,
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = TEGRA_SAMPLE_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = TEGRA_SAMPLE_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &tegra_spdif_dai_ops,
};
EXPORT_SYMBOL_GPL(tegra_spdif_dai);

static int tegra_spdif_driver_probe(struct platform_device *pdev)
{
	int err = 0;
	struct resource *res, *mem;
	struct tegra_spdif_info *info;

	pr_info("%s\n", __func__);

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->pdev = pdev;
	info->pdata = pdev->dev.platform_data;
	info->pdata->driver_data = info;
	BUG_ON(!info->pdata);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no mem resource!\n");
		err = -ENODEV;
		goto fail;
	}

	mem = request_mem_region(res->start, resource_size(res), pdev->name);
	if (!mem) {
		dev_err(&pdev->dev, "memory region already claimed!\n");
		err = -EBUSY;
		goto fail;
	}

	info->spdif_phys = res->start;
	info->spdif_base = (unsigned long)ioremap(res->start, res->end - res->start + 1);
	if (!info->spdif_base) {
		dev_err(&pdev->dev, "cannot remap iomem!\n");
		err = -ENOMEM;
		goto fail_release_mem;
	}

	res = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (!res) {
		dev_err(&pdev->dev, "no dma resource!\n");
		err = -ENODEV;
		goto fail_unmap_mem;
	}
	info->dma_req_sel = res->start;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "no irq resource!\n");
		err = -ENODEV;
		goto fail_unmap_mem;
	}
	info->irq = res->start;

	info->spdif_clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(info->spdif_clk)) {
		err = PTR_ERR(info->spdif_clk);
		goto fail_unmap_mem;
	}
	clk_enable(info->spdif_clk);
	clk_set_rate(info->spdif_clk, info->pdata->spdif_clk_rate);

	spdif_initialize(info->spdif_base, AUDIO_TX_MODE);
	spdif_initialize(info->spdif_base, AUDIO_RX_MODE);

	tegra_spdif_dai.dev = &pdev->dev;
	tegra_spdif_dai.private_data = info;
	err = snd_soc_register_dai(&tegra_spdif_dai);
	if (err)
		goto fail_unmap_mem;

	/* Disable SPDIF clock to save power */
	clk_disable(info->spdif_clk);
	return 0;

fail_unmap_mem:
	iounmap((void __iomem*)info->spdif_base);

fail_release_mem:
	release_mem_region(mem->start, resource_size(mem));
fail:
	kfree(info);
	return err;
}


static int __devexit tegra_spdif_driver_remove(struct platform_device *pdev)
{
	struct tegra_spdif_info *info = tegra_spdif_dai.private_data;

	if (info->spdif_base)
		iounmap((void __iomem*)info->spdif_base);

	if (info)
		kfree(info);

	snd_soc_unregister_dai(&tegra_spdif_dai);
	return 0;
}

static struct platform_driver tegra_spdif_driver = {
	.probe = tegra_spdif_driver_probe,
	.remove = __devexit_p(tegra_spdif_driver_remove),
	.driver = {
		.name = "spdif_out",
		.owner = THIS_MODULE,
	},
};

static int __init tegra_spdif_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&tegra_spdif_driver);
	return ret;
}
module_init(tegra_spdif_init);

static void __exit tegra_spdif_exit(void)
{
	platform_driver_unregister(&tegra_spdif_driver);
}
module_exit(tegra_spdif_exit);

/* Module information */
MODULE_DESCRIPTION("Tegra SPDIF SoC interface");
MODULE_LICENSE("GPL");
