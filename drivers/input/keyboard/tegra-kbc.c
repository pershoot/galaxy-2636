/*
 * drivers/input/keyboard/tegra-kbc.c
 *
 * Keyboard class input driver for the NVIDIA Tegra SoC internal matrix
 * keyboard controller
 *
 * Copyright (c) 2009-2010, NVIDIA Corporation.
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

/*#define DEBUG           1*/
/*#define VERBOSE_DEBUG   1*/
#include <linux/module.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <mach/clk.h>
#include <mach/kbc.h>

#define KBC_CONTROL_0	0
#define KBC_INT_0	4
#define KBC_ROW_CFG0_0	8
#define KBC_COL_CFG0_0	0x18
#define KBC_TO_CNT_0	0x24
#define KBC_RPT_DLY_0	0x2c
#define KBC_KP_ENT0_0	0x30
#define KBC_KP_ENT1_0	0x34
#define KBC_ROW0_MASK_0	0x38

#define res_size(res)	((res)->end - (res)->start + 1)

#define SEC_DEBUG 1

struct tegra_kbc {
	void __iomem *mmio;
	struct input_dev *idev;
	struct device *dev;
	int irq;
	unsigned int wake_enable_keys[KBC_MAX_ROW];
	spinlock_t lock;
	unsigned int repoll_time;
	struct tegra_kbc_platform_data *pdata;
	int *plain_keycode;
	int *fn_keycode;
	struct work_struct key_repeat;
	struct workqueue_struct *kbc_work_queue;
	struct clk *clk;
	int row_seq[KBC_MAX_ROW];
	int col_seq[KBC_MAX_COL];
	int ncols;
      unsigned int pressed_cnt;
};

static struct tegra_kbc *p3_kbc;

#if SEC_DEBUG
static char* code_to_str(int code)
{
	switch (code) {
	case KEY_VOLUMEUP:
		return "Vol-Up";
	case KEY_VOLUMEDOWN:
		return "Vol-Dn";
	};
	return "Unknown";
}
#endif

/* For checking H/W faulty. */
static ssize_t tegra_kbc_keyshort_test(struct device *dev, struct device_attribute *attr, char *buf)
{
	int pressed = 0;

	printk("KBC: tegra_kbc_keyshort_test\n");

//    printk("[Key] 0x%x", p3_kbc->pressed_cnt);
        if(p3_kbc->pressed_cnt)
        {
            pressed++;
        }

	return sprintf(buf, "%d\n", pressed);
}
static DEVICE_ATTR(matrixkey_pressed, S_IRUGO | S_IWUSR | S_IWOTH | S_IXOTH, tegra_kbc_keyshort_test, NULL);

static int tegra_kbc_filter_keys(struct tegra_kbc *kbc, int *prows, int *pcols,
		int nkey_pressed)
{
	int i = 0;
	int j = 0;
	int k = 0;
	int filter_keys[2] = {0};
	int is_filtered = false;
	int new_key_press_count = nkey_pressed;

	dev_dbg(kbc->dev, "%s\n", __func__);

	if (nkey_pressed <= 3) {
		for (i = 0; i < nkey_pressed; i++) {
			for (j = (i + 1); j < nkey_pressed; j++) {
				if ((prows[i] + 1 == prows[j]) ||
						(prows[j] + 1 == prows[i])) {
					for (k = j; i < (nkey_pressed - 1);
									i++) {
						prows[k] = prows[k+1];
						pcols[k] = pcols[k+1];
					}
					nkey_pressed--;
				}
				if ((pcols[i] + 1 == pcols[j]) ||
						(pcols[j] + 1 == pcols[i])) {
					for (k = j; i < (nkey_pressed - 1);
									i++) {
						prows[k] = prows[k+1];
						pcols[k] = pcols[k+1];
					}
					nkey_pressed--;
				}
			}
		}
		return nkey_pressed;
	}

	for (i = 0; i < nkey_pressed; i++) {
		for (j = (i + 1); j < nkey_pressed; j++) {
			if (prows[i] == prows[j]) {
				for (k = 0; k < nkey_pressed; k++) {
					if (k == i)
						continue;

					if (pcols[i] == pcols[k]) {
						filter_keys[0] = k;
						is_filtered = true;
					}
				}
				for (k = 0; k < nkey_pressed; k++) {
					if (k == j)
						continue;
					if (pcols[j] == pcols[k]) {
						filter_keys[1] = k;
						is_filtered = true;
					}
				}
				goto end;
			}
		}
	}

end:
	if (is_filtered) {
		for (i = filter_keys[0]; i < (nkey_pressed - 1); i++) {
			prows[i] = prows[i+1];
			pcols[i] = pcols[i+1];
		}
		new_key_press_count--;
		for (i = filter_keys[1]; i < (nkey_pressed - 1); i++) {
			prows[i] = prows[i+1];
			pcols[i] = pcols[i+1];
		}
		new_key_press_count--;
	}
	nkey_pressed = new_key_press_count;
	return new_key_press_count;
}

static int tegra_kbc_keycode(struct tegra_kbc *kbc, int r, int c, bool fn_key)
{
	int code_index = kbc->row_seq[r] * kbc->ncols + kbc->col_seq[c];
	if (!fn_key)
		return kbc->plain_keycode[code_index];
	else
		return kbc->fn_keycode[code_index];
}

static void tegra_kbc_report_keys(struct tegra_kbc *kbc, int *fifo)
{
	int curr_fifo[KBC_MAX_KPRESS_EVENT];
	int rows_val[KBC_MAX_KPRESS_EVENT], cols_val[KBC_MAX_KPRESS_EVENT];
	u32 kp_ent_val[(KBC_MAX_KPRESS_EVENT + 3) / 4];
	u32 *kp_ents = kp_ent_val;
	u32 kp_ent = 0;
	unsigned long flags;
	int i, j, valid = 0;
	bool fn = false;

	dev_dbg(kbc->dev, "KBC: tegra_kbc_report_keys\n");

	local_irq_save(flags);
	for (i = 0; i < ARRAY_SIZE(kp_ent_val); i++)
		kp_ent_val[i] = readl(kbc->mmio + KBC_KP_ENT0_0 + (i*4));
	local_irq_restore(flags);

	valid = 0;
	for (i = 0; i < KBC_MAX_KPRESS_EVENT; i++) {
		if (!(i&3))
			kp_ent = *kp_ents++;

		if (kp_ent & 0x80) {
			cols_val[valid] = kp_ent & 0x7;
			rows_val[valid++] = (kp_ent >> 3) & 0xf;
		}
		kp_ent >>= 8;
	}

	if (kbc->pdata->is_filter_keys)
		valid = tegra_kbc_filter_keys(kbc, rows_val, cols_val, valid);

	for (i = 0; i < valid; i++) {
		int k = tegra_kbc_keycode(kbc, rows_val[i], cols_val[i], false);
		if (k == KEY_FN) {
			fn = true;
			break;
		}
	}

	j = 0;
	for (i = 0; i < valid; i++) {
		int k = tegra_kbc_keycode(kbc, rows_val[i], cols_val[i], fn);
		if (likely(k != -1))
			curr_fifo[j++] = k;
	}
	valid = j;

	for (i = 0; i < KBC_MAX_KPRESS_EVENT; i++) {
		if (fifo[i] == -1)
			continue;
		for (j = 0; j < valid; j++) {
			if (curr_fifo[j] == fifo[i]) {
				curr_fifo[j] = -1;
				break;
			}
		}
		if (j == valid) {
			input_report_key(kbc->idev, fifo[i], 0);
			fifo[i] = -1;
		}
	}
	for (j = 0; j < valid; j++) {
		if (curr_fifo[j] == -1)
			continue;
		for (i = 0; i < KBC_MAX_KPRESS_EVENT; i++) {
			if (fifo[i] == -1)
				break;
		}
		if (i != KBC_MAX_KPRESS_EVENT) {
			fifo[i] = curr_fifo[j];
#if SEC_DEBUG
			printk("Key: %s (%s)\n", code_to_str(fifo[i]), "pressed");
#endif
			input_report_key(kbc->idev, fifo[i], 1);
			kbc->pressed_cnt |= 1 << i;
		} else
			WARN_ON(1);
	}
}

static void tegra_kbc_key_repeat(struct work_struct *work)
{
	struct tegra_kbc *kbc;
	unsigned long flags;
	u32 val;
	int fifo[KBC_MAX_KPRESS_EVENT];
	int i;

	kbc = container_of(work, struct tegra_kbc, key_repeat);
	dev_dbg(kbc->dev, "KBC: tegra_kbc_key_repeat\n");

	for (i = 0; i < ARRAY_SIZE(fifo); i++)
		fifo[i] = -1;

	while (1) {
		val = (readl(kbc->mmio + KBC_INT_0) >> 4) & 0xf;
		if (!val) {
			/* release any pressed keys and exit the loop */
			for (i = 0; i < ARRAY_SIZE(fifo); i++) {
				if (fifo[i] == -1)
					continue;
#if SEC_DEBUG
				printk("Key: %s (%s)\n", code_to_str(fifo[i]), "released");
#endif
				input_report_key(kbc->idev, fifo[i], 0);
				kbc->pressed_cnt &= ~(1 << i);;
			}
			break;
		}
		tegra_kbc_report_keys(kbc, fifo);
		msleep((val == 1) ? kbc->repoll_time : 1);
	}

	spin_lock_irqsave(&kbc->lock, flags);
	val = readl(kbc->mmio + KBC_CONTROL_0);
	val |= (1<<3);
	writel(val, kbc->mmio + KBC_CONTROL_0);
	spin_unlock_irqrestore(&kbc->lock, flags);
}

static void tegra_kbc_close(struct input_dev *dev)
{
	struct tegra_kbc *kbc = input_get_drvdata(dev);
	unsigned long flags;
	u32 val;

	dev_dbg(kbc->dev, "KBC: tegra_kbc_close\n");

	spin_lock_irqsave(&kbc->lock, flags);
	val = readl(kbc->mmio + KBC_CONTROL_0);
	val &= ~1;
	writel(val, kbc->mmio + KBC_CONTROL_0);
	spin_unlock_irqrestore(&kbc->lock, flags);

	clk_disable(kbc->clk);
}

static void tegra_kbc_setup_wakekeys(struct tegra_kbc *kbc, bool filter)
{
	int i;
	unsigned int rst_val;

	dev_dbg(kbc->dev, "KBC: tegra_kbc_setup_wakekeys\n");

	BUG_ON(kbc->pdata->wake_key_cnt > KBC_MAX_KEY);
	rst_val = (filter && (kbc->pdata->wake_key_cnt ||
				kbc->pdata->is_wake_on_any_key)) ? ~0 : 0;

	for (i = 0; i < KBC_MAX_ROW; i++)
		writel(rst_val, kbc->mmio + KBC_ROW0_MASK_0 + i * 4);

	if (filter) {
		for (i = 0; i < KBC_MAX_ROW; i++) {
			if (kbc->wake_enable_keys[i] != rst_val)
				writel(kbc->wake_enable_keys[i],
					kbc->mmio + KBC_ROW0_MASK_0 + i * 4);
		}
	}
}

static void tegra_kbc_config_pins(struct tegra_kbc *kbc)
{
	const struct tegra_kbc_platform_data *pdata = kbc->pdata;
	int i;
	unsigned int row_config[4];
	unsigned int col_config[3];

	dev_dbg(kbc->dev, "KBC: tegra_kbc_config_pins\n");
	for (i = 0; i < 4; i++)
		row_config[i] = 0;
	for (i = 0; i < 3; i++)
		col_config[i] = 0;

	for (i = 0; i < KBC_MAX_GPIO; i++) {
		u32 r_shift = 5 * (pdata->pin_cfg[i].num % 6);
		u32 c_shift = 4 * (pdata->pin_cfg[i].num % 8);
		u32 r_mask = 0x1f << r_shift;
		u32 c_mask = 0xf << c_shift;
		u32 index;

		if (pdata->pin_cfg[i].pin_type == kbc_pin_unused)
			continue;

		if (pdata->pin_cfg[i].pin_type == kbc_pin_row) {
			index = pdata->pin_cfg[i].num / 6;
			row_config[index] &= ~r_mask;
			row_config[index] |=
				((pdata->pin_cfg[i].num << 1) | 1) << r_shift;
		} else {
			index = (pdata->pin_cfg[i].num + KBC_MAX_ROW) / 8;
			col_config[index] &= ~c_mask;
			col_config[index] |=
				((pdata->pin_cfg[i].num << 1) | 1) << c_shift;
		}
	}
	for (i = 0; i < 4; i++) {
		u32 r_offs = i * 4 + KBC_ROW_CFG0_0;
		writel(row_config[i], kbc->mmio + r_offs);
	}
	for (i = 0; i < 3; i++) {
		u32 c_offs = i * 4 + KBC_COL_CFG0_0;
		writel(col_config[i], kbc->mmio + c_offs);
	}
}

static int tegra_kbc_open(struct input_dev *dev)
{
	struct tegra_kbc *kbc = input_get_drvdata(dev);
	unsigned long flags;
	u32 val = 0;

	dev_dbg(kbc->dev, "KBC: tegra_kbc_open\n");

	clk_enable(kbc->clk);

	/* Reset the KBC controller to clear all previous status.*/
	tegra_periph_reset_assert(kbc->clk);
	udelay(100);
	tegra_periph_reset_deassert(kbc->clk);
	udelay(100);

	tegra_kbc_config_pins(kbc);
	tegra_kbc_setup_wakekeys(kbc, false);

	writel(kbc->pdata->repeat_cnt, kbc->mmio + KBC_RPT_DLY_0);

	val = kbc->pdata->debounce_cnt << 4;
	val |= 1<<14; /* fifo interrupt threshold = 1 entry */
	val |= 1<<3;  /* interrupt on FIFO threshold reached */
	val |= 1;     /* enable */
	writel(val, kbc->mmio + KBC_CONTROL_0);

	/* Bit 19:0 is for scan timeout count */
	kbc->pdata->scan_timeout_cnt &= 0xFFFFF;
	writel(kbc->pdata->scan_timeout_cnt, kbc->mmio + KBC_TO_CNT_0);

	/* atomically clear out any remaining entries in the key FIFO
	 * and enable keyboard interrupts */
	spin_lock_irqsave(&kbc->lock, flags);

	while (1) {
		val = readl(kbc->mmio + KBC_INT_0);
		val >>= 4;
		if (val) {
			val = readl(kbc->mmio + KBC_KP_ENT0_0);
			val = readl(kbc->mmio + KBC_KP_ENT1_0);
		} else {
			break;
		}
	}
	writel(0x7, kbc->mmio + KBC_INT_0);
	spin_unlock_irqrestore(&kbc->lock, flags);
	return 0;
}

static irqreturn_t tegra_kbc_isr(int irq, void *args)
{
	struct tegra_kbc *kbc = args;
	u32 val, ctl;

	dev_dbg(kbc->dev, "KBC: tegra_kbc_isr\n");

	/* until all keys are released, defer further processing to
	 * the polling loop in tegra_kbc_key_repeat */
	ctl = readl(kbc->mmio + KBC_CONTROL_0);
	ctl &= ~(1<<3);
	writel(ctl, kbc->mmio + KBC_CONTROL_0);

	/* quickly bail out & reenable interrupts if the interrupt source
	 * wasn't fifo count threshold */
	val = readl(kbc->mmio + KBC_INT_0);
	writel(val, kbc->mmio + KBC_INT_0);

	if (!(val & (1<<2))) {
		ctl |= 1<<3;
		writel(ctl, kbc->mmio + KBC_CONTROL_0);
		return IRQ_HANDLED;
	}

	queue_work(kbc->kbc_work_queue, &kbc->key_repeat);
	return IRQ_HANDLED;
}

static int __devinit tegra_kbc_probe(struct platform_device *pdev)
{
	struct tegra_kbc *kbc;
	struct tegra_kbc_platform_data *pdata = pdev->dev.platform_data;
	struct resource *res;
	int irq;
	int err;
	int rows[KBC_MAX_ROW];
	int cols[KBC_MAX_COL];
	int i, j;
	int nr = 0;
	int nc = 0;
	char name[64];

	dev_dbg(&pdev->dev, "KBC: tegra_kbc_probe\n");

	if (!pdata)
		return -EINVAL;

	/* Validate the data entry */
	if (!pdata->plain_keycode) {
		dev_err(&pdev->dev, "No key codes\n");
		return -EINVAL;
	}

	for (i = 0; i < KBC_MAX_GPIO; i++) {
		if ((pdata->pin_cfg[i].pin_type == kbc_pin_row) &&
			(pdata->pin_cfg[i].num >= KBC_MAX_ROW)) {
				dev_err(&pdev->dev, "Invalid row number\n");
				return -EINVAL;
		} else if ((pdata->pin_cfg[i].pin_type == kbc_pin_col) &&
			(pdata->pin_cfg[i].num >= KBC_MAX_COL)) {
				dev_err(&pdev->dev, "Invalid column number\n");
				return -EINVAL;
		}
	}

	kbc = kzalloc(sizeof(*kbc), GFP_KERNEL);
	if (!kbc)
		return -ENOMEM;

	kbc->pdata = pdata;
	kbc->irq = -EINVAL;

	memset(rows, 0, sizeof(rows));
	memset(cols, 0, sizeof(cols));

	kbc->idev = input_allocate_device();
	if (!kbc->idev) {
		err = -ENOMEM;
		goto fail;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get I/O memory\n");
		err = -ENXIO;
		goto fail;
	}
	res = request_mem_region(res->start, res_size(res), pdev->name);
	if (!res) {
		dev_err(&pdev->dev, "failed to request I/O memory\n");
		err = -EBUSY;
		goto fail;
	}
	kbc->mmio = ioremap(res->start, res_size(res));
	if (!kbc->mmio) {
		dev_err(&pdev->dev, "failed to remap I/O memory\n");
		err = -ENXIO;
		goto fail;
	}
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "failed to get keypad IRQ\n");
		err = -ENXIO;
		goto fail;
	}
	kbc->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR_OR_NULL(kbc->clk)) {
		dev_err(&pdev->dev, "failed to get keypad clock\n");
		err = (kbc->clk) ? PTR_ERR(kbc->clk) : -ENODEV;
		kbc->clk = NULL;
		goto fail;
	}

	platform_set_drvdata(pdev, kbc);

	kbc->dev = &pdev->dev;
#ifdef CONFIG_SAMSUNG_INPUT
	kbc->idev->name = "sec_key";
#else
	kbc->idev->name = pdev->name;
#endif
	input_set_drvdata(kbc->idev, kbc);
	kbc->idev->id.bustype = BUS_HOST;
	kbc->idev->open = tegra_kbc_open;
	kbc->idev->close = tegra_kbc_close;
	kbc->idev->dev.parent = &pdev->dev;
	spin_lock_init(&kbc->lock);

	for (i = 0; i < KBC_MAX_GPIO; i++) {
		if (pdata->pin_cfg[i].pin_type == kbc_pin_row) {
			rows[pdata->pin_cfg[i].num] = 1;
			kbc->row_seq[pdata->pin_cfg[i].num] = nr++;
		} else if (pdata->pin_cfg[i].pin_type == kbc_pin_col) {
			cols[pdata->pin_cfg[i].num] = 1;
			kbc->col_seq[pdata->pin_cfg[i].num] = nc++;
		}
	}
	kbc->ncols = nc;

	for (i = 0; i < pdata->wake_key_cnt; i++)
		kbc->wake_enable_keys[i] = ~0u;

	for (i = 0; i < pdata->wake_key_cnt; i++)
		kbc->wake_enable_keys[kbc->pdata->wake_cfg[i].row] &=
					~(1 << kbc->pdata->wake_cfg[i].col);

	pdata->debounce_cnt = min_t(unsigned int, pdata->debounce_cnt, 0x3fful);
	kbc->repoll_time = 5 + (16+pdata->debounce_cnt)*nr + pdata->repeat_cnt;
	kbc->repoll_time = (kbc->repoll_time + 31) / 32;

	kbc->plain_keycode = pdata->plain_keycode;
	kbc->fn_keycode = pdata->fn_keycode;

	kbc->idev->evbit[0] = BIT_MASK(EV_KEY);
	for (i = 0; i < KBC_MAX_ROW; i++) {
		if (!rows[i])
			continue;
		for (j = 0; j < KBC_MAX_COL; j++) {
			int keycode;
			if (!cols[j])
				continue;
			keycode = tegra_kbc_keycode(kbc, i, j, false);
			if (keycode == KEY_RESERVED)
				continue;
			set_bit(keycode, kbc->idev->keybit);
		}
	}

	/* create the workqueue for the kbc path */
	snprintf(name, sizeof(name), "tegra-kbc");
	kbc->kbc_work_queue = create_singlethread_workqueue(name);
	if (kbc->kbc_work_queue == NULL) {
		dev_err(&pdev->dev, "Failed to create work queue\n");
		err = -ENODEV;
		goto fail;
	}

	/* keycode FIFO needs to be read atomically; leave local
	 * interrupts disabled when handling KBC interrupt */
	INIT_WORK(&kbc->key_repeat, tegra_kbc_key_repeat);

	err = request_irq(irq, tegra_kbc_isr, IRQF_DISABLED, pdev->name, kbc);
	if (err) {
		dev_err(&pdev->dev, "failed to request keypad IRQ\n");
		goto fail;
	}
	kbc->irq = irq;

	err = input_register_device(kbc->idev);
	if (err) {
		dev_err(&pdev->dev, "failed to register input device\n");
		goto fail;
	}

	device_init_wakeup(&pdev->dev, 1);

	kbc->pressed_cnt = 0;
	p3_kbc = kbc;
	if (device_create_file(&pdev->dev, &dev_attr_matrixkey_pressed) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_matrixkey_pressed.attr.name);


	return 0;

fail:
	if (kbc->irq >= 0)
		free_irq(kbc->irq, pdev);
	if (kbc->idev)
		input_free_device(kbc->idev);
	if (kbc->clk)
		clk_put(kbc->clk);
	if (kbc->mmio)
		iounmap(kbc->mmio);
	if (kbc->kbc_work_queue)
		destroy_workqueue(kbc->kbc_work_queue);
	kfree(kbc);

	return err;
}

static int __devexit tegra_kbc_remove(struct platform_device *pdev)
{
	struct tegra_kbc *kbc = platform_get_drvdata(pdev);
	struct resource *res;

	dev_dbg(kbc->dev, "KBC: tegra_kbc_remove\n");

	free_irq(kbc->irq, pdev);
	clk_disable(kbc->clk);
	clk_put(kbc->clk);

	input_unregister_device(kbc->idev);
	input_free_device(kbc->idev);
	iounmap(kbc->mmio);
	destroy_workqueue(kbc->kbc_work_queue);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, res_size(res));

	kfree(kbc);
	return 0;
}

#ifdef CONFIG_PM
static int tegra_kbc_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct tegra_kbc *kbc = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "KBC: tegra_kbc_suspend\n");
	if (device_may_wakeup(&pdev->dev) &&
		(kbc->pdata->is_wake_on_any_key || kbc->pdata->wake_key_cnt)) {
		tegra_kbc_setup_wakekeys(kbc, true);
		enable_irq_wake(kbc->irq);
		/* Forcefully clear the interrupt status */
		writel(0x7, kbc->mmio + KBC_INT_0);
		msleep(30);
	} else {
		tegra_kbc_close(kbc->idev);
	}
	return 0;
}

static int tegra_kbc_resume(struct platform_device *pdev)
{
	struct tegra_kbc *kbc = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "KBC: tegra_kbc_resume\n");

#ifdef CONFIG_MACH_SAMSUNG_VARIATION_TEGRA
	if (device_may_wakeup(&pdev->dev) &&
		(kbc->pdata->is_wake_on_any_key || kbc->pdata->wake_key_cnt)) {
#else
	if (device_may_wakeup(&pdev->dev)) {
#endif
		disable_irq_wake(kbc->irq);
		tegra_kbc_setup_wakekeys(kbc, false);
	} else if (kbc->idev->users)
		return tegra_kbc_open(kbc->idev);

	return 0;
}
#endif

static struct platform_driver tegra_kbc_driver = {
	.probe   = tegra_kbc_probe,
	.remove  = tegra_kbc_remove,
#ifdef CONFIG_PM
	.suspend = tegra_kbc_suspend,
	.resume  = tegra_kbc_resume,
#endif
	.driver = {
		.name = "tegra-kbc"
	}
};

static int __devinit tegra_kbc_init(void)
{
	pr_debug("KBC: tegra_kbc_init\n");
	return platform_driver_register(&tegra_kbc_driver);
}

static void __exit tegra_kbc_exit(void)
{
	pr_debug("KBC: tegra_kbc_exit\n");
	platform_driver_unregister(&tegra_kbc_driver);
}

module_init(tegra_kbc_init);
module_exit(tegra_kbc_exit);

MODULE_DESCRIPTION("Tegra matrix keyboard controller driver");
