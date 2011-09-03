/*
 * tegra_i2s.c  --  ALSA Soc Audio Layer
 *
 * (c) 2010 Nvidia Graphics Pvt. Ltd.
 *  http://www.nvidia.com
 *
 * (c) 2006 Wolfson Microelectronics PLC.
 * Graeme Gregory graeme.gregory@wolfsonmicro.com or linux@wolfsonmicro.com
 *
 * (c) 2004-2005 Simtec Electronics
 *    http://armlinux.simtec.co.uk/
 *    Ben Dooks <ben@simtec.co.uk>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include "tegra_soc.h"

int en_dmic;

/* i2s controller */

static void *das_base = IO_ADDRESS(TEGRA_APB_MISC_BASE);

struct tegra_i2s_info {
	struct platform_device *pdev;
	struct tegra_audio_platform_data *pdata;
	struct clk *i2s_clk;
	struct clk *dap_mclk;
	struct clk *audio_sync_clk;
	phys_addr_t i2s_phys;
	void __iomem *i2s_base;

	unsigned long dma_req_sel;

	int irq;
	/* Control for whole I2S (Data format, etc.) */
	unsigned int bit_format;
	struct i2s_runtime_data i2s_regs;
	struct das_regs_cache das_regs;
};

void setup_dma_request(struct snd_pcm_substream *substream,
			struct tegra_dma_req *req,
			void (*dma_callback)(struct tegra_dma_req *req),
			void *dma_data)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	struct tegra_i2s_info *info = cpu_dai->private_data;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		req->to_memory = false;
		req->dest_addr =
			i2s_get_fifo_phy_base(cpu_dai->id, I2S_FIFO_TX);
		req->dest_wrap = 4;
		req->source_wrap = 0;
		if (info->bit_format == TEGRA_AUDIO_BIT_FORMAT_DSP)
			req->dest_bus_width = info->pdata->dsp_bus_width;
		else
			req->dest_bus_width = info->pdata->i2s_bus_width;
		req->source_bus_width = 32;
	} else {
		req->to_memory = true;
		req->source_addr =
			i2s_get_fifo_phy_base(cpu_dai->id, I2S_FIFO_RX);
		req->dest_wrap = 0;
		req->source_wrap = 4;
		if (info->bit_format == TEGRA_AUDIO_BIT_FORMAT_DSP)
			req->source_bus_width = info->pdata->dsp_bus_width;
		else
			req->source_bus_width = info->pdata->i2s_bus_width;
		req->dest_bus_width = 32;
	}

	req->complete = dma_callback;
	req->dev = dma_data;
	req->req_sel = info->dma_req_sel;

	return;
}


/* playback */
static inline void start_i2s_playback(struct snd_soc_dai *cpu_dai)
{
	i2s_fifo_set_attention_level(cpu_dai->id, I2S_FIFO_TX,
					I2S_FIFO_ATN_LVL_FOUR_SLOTS);
	i2s_fifo_enable(cpu_dai->id, I2S_FIFO_TX, 1);
}

static inline void stop_i2s_playback(struct snd_soc_dai *cpu_dai)
{
	i2s_set_fifo_irq_on_err(cpu_dai->id, I2S_FIFO_TX, 0);
	i2s_set_fifo_irq_on_qe(cpu_dai->id, I2S_FIFO_TX, 0);
	i2s_fifo_enable(cpu_dai->id, I2S_FIFO_TX, 0);
	while (i2s_get_status(cpu_dai->id) & I2S_I2S_FIFO_TX_BUSY);
}

/* recording */
static inline void start_i2s_capture(struct snd_soc_dai *cpu_dai)
{
	i2s_fifo_set_attention_level(cpu_dai->id, I2S_FIFO_RX,
					I2S_FIFO_ATN_LVL_FOUR_SLOTS);
	i2s_fifo_enable(cpu_dai->id, I2S_FIFO_RX, 1);
}

static inline void stop_i2s_capture(struct snd_soc_dai *cpu_dai)
{
	i2s_set_fifo_irq_on_err(cpu_dai->id, I2S_FIFO_RX, 0);
	i2s_set_fifo_irq_on_qe(cpu_dai->id, I2S_FIFO_RX, 0);
	i2s_fifo_enable(cpu_dai->id, I2S_FIFO_RX, 0);
	while (i2s_get_status(cpu_dai->id) & I2S_I2S_FIFO_RX_BUSY);
}


static int tegra_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct tegra_i2s_info *info = dai->private_data;
	int ret = 0;
	int val;
	unsigned int i2s_id = dai->id;
	unsigned int rate;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		val = I2S_BIT_SIZE_16;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val = I2S_BIT_SIZE_24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		val = I2S_BIT_SIZE_32;
		break;
	default:
		ret =-EINVAL;
		goto err;
	}

	i2s_set_bit_size(i2s_id, val);

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
		ret = -EINVAL;
		goto err;
	}

	rate = clk_get_rate(info->i2s_clk);
	if (info->bit_format == TEGRA_AUDIO_BIT_FORMAT_DSP)
		rate *= 2;

	i2s_set_channel_bit_count(i2s_id, val, rate);

	return 0;

err:
	return ret;
}


static int tegra_i2s_set_dai_fmt(struct snd_soc_dai *cpu_dai,
					unsigned int fmt)
{
	int val1;
	int val2;
	unsigned int i2s_id = cpu_dai->id;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		val1 = 1;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		val1= 0;
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
	case SND_SOC_DAIFMT_CBM_CFS:
		/* Tegra does not support different combinations of
		 * master and slave for FSYNC and BCLK */
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		val1 = I2S_BIT_FORMAT_DSP;
		val2 = 0;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		val1 = I2S_BIT_FORMAT_DSP;
		val2 = 1;
		break;
	case SND_SOC_DAIFMT_I2S:
		val1 = I2S_BIT_FORMAT_I2S;
		val2 = 0;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		val1 = I2S_BIT_FORMAT_RJM;
		val2 = 0;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		val1 = I2S_BIT_FORMAT_LJM;
		val2 = 0;
		break;
	default:
		return -EINVAL;
	}

	i2s_set_bit_format(i2s_id,val1);
	i2s_set_left_right_control_polarity(i2s_id,val2);

	/* Clock inversion */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		/* frame inversion not valid for DSP modes */
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_NF:
			/* aif1 |= WM8903_AIF_BCLK_INV; */
			break;
		default:
			return -EINVAL;
		}
		break;
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_IF:
			/* aif1 |= WM8903_AIF_BCLK_INV |
			 * WM8903_AIF_LRCLK_INV; */
			break;
		case SND_SOC_DAIFMT_IB_NF:
			/* aif1 |= WM8903_AIF_BCLK_INV; */
			break;
		case SND_SOC_DAIFMT_NB_IF:
			/* aif1 |= WM8903_AIF_LRCLK_INV; */
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int tegra_i2s_set_dai_sysclk(struct snd_soc_dai *cpu_dai,
					int clk_id, unsigned int freq, int dir)
{
	struct tegra_i2s_info* info = cpu_dai->private_data;
	struct tegra_audio_platform_data *pdata = info->pdev->dev.platform_data;

	if (info && info->i2s_clk) {
		clk_set_rate(info->i2s_clk, pdata->i2s_clk_rate);
		if (clk_enable(info->i2s_clk)) {
			pr_err("%s: failed to enable i2s-%d clock\n", __func__,
			      cpu_dai->id+1);
			return -EIO;
		}
	}
	else {
		pr_err("%s: could not get i2s-%d clock\n", __func__,
		      cpu_dai->id+1);
		return -EIO;
	}

	return 0;
}

static int tegra_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			start_i2s_playback(dai);
		else
			start_i2s_capture(dai);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			stop_i2s_playback(dai);
		else
			stop_i2s_capture(dai);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int i2s_configure(struct tegra_i2s_info *info )
{
	struct platform_device *pdev = info->pdev;
	struct tegra_audio_platform_data *pdata = pdev->dev.platform_data;
	struct clk *i2s_clk;
	unsigned int i2s_id = pdev->id;
	unsigned int rate;

	i2s_enable_fifos(i2s_id, 0);
	i2s_fifo_clear(i2s_id, I2S_FIFO_TX);
	i2s_fifo_clear(i2s_id, I2S_FIFO_RX);
	i2s_set_left_right_control_polarity(i2s_id, 0); /* default */

	i2s_clk = clk_get(&pdev->dev, NULL);
	if (!i2s_clk) {
		dev_err(&pdev->dev, "%s: could not get i2s clock\n",
			__func__);
		return -EIO;
	}

	rate = clk_get_rate(i2s_clk);
	if (info->bit_format == TEGRA_AUDIO_BIT_FORMAT_DSP)
		rate *= 2;

	if (pdata->i2s_master && pdata->i2s_master_clk)
		i2s_set_channel_bit_count(i2s_id, pdata->i2s_master_clk, rate);

	i2s_set_master(i2s_id, pdata->i2s_master);

	i2s_set_fifo_mode(i2s_id, I2S_FIFO_TX, 1);
	i2s_set_fifo_mode(i2s_id, I2S_FIFO_RX, 0);

	i2s_set_bit_format(i2s_id, pdata->mode);
	i2s_set_bit_size(i2s_id, pdata->bit_size);
	i2s_set_fifo_format(i2s_id, pdata->fifo_fmt);

	if (i2s_id == 0)
		en_dmic = pdata->en_dmic;

	return 0;
}

#ifdef CONFIG_PM
int tegra_i2s_suspend(struct snd_soc_dai *cpu_dai)
{
	struct tegra_i2s_info *info = cpu_dai->private_data;

	i2s_get_all_regs(cpu_dai->id, &info->i2s_regs);
	tegra_das_get_all_regs(&info->das_regs);

	return 0;
}

int tegra_i2s_resume(struct snd_soc_dai *cpu_dai)
{
	struct tegra_i2s_info *info = cpu_dai->private_data;

	tegra_das_set_all_regs(&info->das_regs);
	i2s_set_all_regs(cpu_dai->id, &info->i2s_regs);

	return 0;
}

#else
#define tegra_i2s_suspend	NULL
#define tegra_i2s_resume	NULL
#endif

static int tegra_i2s_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct tegra_i2s_info *info = dai->private_data;

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	/* This code is intended to prevent pop noise when i2s port is closed */
	clk_enable(info->audio_sync_clk);
#else
	clk_enable(info->dap_mclk);
	clk_enable(info->audio_sync_clk);

	/* set das pins state to normal */
	tegra_das_power_mode(true);
#endif

	return 0;
}

static void tegra_i2s_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct tegra_i2s_info *info = dai->private_data;

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	/* This code is intended to prevent pop noise when i2s port is closed */
	clk_disable(info->audio_sync_clk);
#else
	/* set das pins state to tristate */
	tegra_das_power_mode(false);

	clk_disable(info->dap_mclk);
	clk_disable(info->audio_sync_clk);
#endif
	return;
}

static int tegra_i2s_probe(struct platform_device *pdev,
				struct snd_soc_dai *dai)
{
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	writel(0, das_base+APB_MISC_DAS_DAP_CTRL_SEL_0);
	writel(0, das_base+APB_MISC_DAS_DAC_INPUT_DATA_CLK_SEL_0);
	writel(DAP_CTRL_SEL_DAP3, das_base + APB_MISC_DAS_DAP_CTRL_SEL_1);
/*	writel(DAP_CTRL_SEL_DAP3,das_base+ APB_MISC_DAS_DAP_CTRL_SEL_3); */
/*	writel(((1<<31) | DAP_CTRL_SEL_DAP2 | DAP_CTRL_SEL_DAP4),
			das_base + APB_MISC_DAS_DAP_CTRL_SEL_2);*/
	writel(((1<<31) | DAP_CTRL_SEL_DAP2),
			das_base + APB_MISC_DAS_DAP_CTRL_SEL_2);
#endif

	return 0;
}

static struct snd_soc_dai_ops tegra_i2s_dai_ops = {
	.startup	= tegra_i2s_startup,
	.shutdown	= tegra_i2s_shutdown,
	.trigger	= tegra_i2s_trigger,
	.hw_params	= tegra_i2s_hw_params,
	.set_fmt	= tegra_i2s_set_dai_fmt,
	.set_sysclk	= tegra_i2s_set_dai_sysclk,
};

struct snd_soc_dai tegra_i2s_dai[] = {
	{
		.name = "tegra-i2s-1",
		.id = 0,
		.probe = tegra_i2s_probe,
		.suspend = tegra_i2s_suspend,
		.resume = tegra_i2s_resume,
		.playback = {
			.channels_min = 2,
			.channels_max = 2,
			.rates = TEGRA_SAMPLE_RATES,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.capture = {
#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
			.channels_min = 1,
#else
			.channels_min = 2,
#endif
			.channels_max = 2,
			.rates = TEGRA_SAMPLE_RATES,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.ops = &tegra_i2s_dai_ops,
	},
	{
		.name = "tegra-i2s-2",
		.id = 1,
		.probe = tegra_i2s_probe,
		.suspend = tegra_i2s_suspend,
		.resume = tegra_i2s_resume,
		.playback = {
			.channels_min = 1,
			.channels_max = 1,
			.rates = TEGRA_VOICE_SAMPLE_RATES,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.capture = {
			.channels_min = 1,
			.channels_max = 1,
			.rates = TEGRA_VOICE_SAMPLE_RATES,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.ops = &tegra_i2s_dai_ops,
	},
};
EXPORT_SYMBOL_GPL(tegra_i2s_dai);

static int tegra_i2s_driver_probe(struct platform_device *pdev)
{
	int err = 0;
	struct resource *res, *mem;
	struct tegra_i2s_info *info;
	int i = 0;

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

	info->i2s_phys = res->start;
	info->i2s_base = ioremap(res->start, res->end - res->start + 1);
	if (!info->i2s_base) {
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

	info->i2s_clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(info->i2s_clk)) {
		err = PTR_ERR(info->i2s_clk);
		goto fail_unmap_mem;
	}
	clk_set_rate(info->i2s_clk, info->pdata->i2s_clk_rate);

	info->dap_mclk = i2s_get_clock_by_name(info->pdata->dap_clk);
	if (IS_ERR(info->dap_mclk)) {
		err = PTR_ERR(info->dap_mclk);
		goto fail_unmap_mem;
	}

	info->audio_sync_clk = i2s_get_clock_by_name(
					info->pdata->audio_sync_clk);
	if (IS_ERR(info->audio_sync_clk)) {
		err = PTR_ERR(info->audio_sync_clk);
		goto fail_unmap_mem;
	}

	info->bit_format = TEGRA_AUDIO_BIT_FORMAT_DEFAULT;
	if (info->pdata->mode == I2S_BIT_FORMAT_DSP)
		info->bit_format = TEGRA_AUDIO_BIT_FORMAT_DSP;

	i2s_configure(info);

	for (i = 0; i < ARRAY_SIZE(tegra_i2s_dai); i++) {
		if (tegra_i2s_dai[i].id == pdev->id) {
			tegra_i2s_dai[i].dev = &pdev->dev;
			tegra_i2s_dai[i].private_data = info;
			err = snd_soc_register_dai(&tegra_i2s_dai[i]);
			if (err)
				goto fail_unmap_mem;
		}
	}

	return 0;

fail_unmap_mem:
	iounmap(info->i2s_base);
fail_release_mem:
	release_mem_region(mem->start, resource_size(mem));
fail:
	kfree(info);
	return err;
}


static int __devexit tegra_i2s_driver_remove(struct platform_device *pdev)
{
	struct tegra_i2s_info *info = tegra_i2s_dai[pdev->id].private_data;

	if (info->i2s_base)
		iounmap(info->i2s_base);

	if (info)
		kfree(info);

	snd_soc_unregister_dai(&tegra_i2s_dai[pdev->id]);
	return 0;
}

static struct platform_driver tegra_i2s_driver = {
	.probe = tegra_i2s_driver_probe,
	.remove = __devexit_p(tegra_i2s_driver_remove),
	.driver = {
		.name = "i2s",
		.owner = THIS_MODULE,
	},
};

static int __init tegra_i2s_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&tegra_i2s_driver);
	return ret;
}
module_init(tegra_i2s_init);

static void __exit tegra_i2s_exit(void)
{
	platform_driver_unregister(&tegra_i2s_driver);
}
module_exit(tegra_i2s_exit);

/* Module information */
MODULE_DESCRIPTION("Tegra I2S SoC interface");
MODULE_LICENSE("GPL");
