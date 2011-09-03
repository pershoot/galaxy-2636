/*
 * arch/arm/mach-tegra/cpu-tegra.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *	Based on arch/arm/plat-omap/cpu-omap.c, (C) 2005 Nokia Corporation
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/suspend.h>
#include <linux/debugfs.h>

#include <asm/smp_twd.h>
#include <asm/system.h>

#include <mach/hardware.h>
#include <mach/clk.h>

#include "clock.h"

static struct cpufreq_frequency_table *freq_table;

#define NUM_CPUS	2

static struct clk *cpu_clk;
static struct clk *emc_clk;

static unsigned long target_cpu_speed[NUM_CPUS];
static DEFINE_MUTEX(tegra_cpu_lock);
static bool is_suspended;

unsigned int tegra_getspeed(unsigned int cpu);
static int tegra_update_cpu_speed(unsigned long rate);
static unsigned long tegra_cpu_highest_speed(void);

#ifdef CONFIG_TEGRA_CPU_FREQ_LOCK
static DEFINE_MUTEX(tegra_cpulock_lock);
static bool is_cpufreq_locked;
static int cpulock_freq;
static int cpulock_debug_timeout;
static struct hrtimer cpulock_timer;
void tegra_cpu_lock_speed(int min_rate, int timeout_ms);
void tegra_cpu_unlock_speed(void);
#endif /* CONFIG_TEGRA_CPU_FREQ_LOCK */

#ifdef CONFIG_TEGRA_THERMAL_THROTTLE
/* CPU frequency is gradually lowered when throttling is enabled */
#define THROTTLE_DELAY		msecs_to_jiffies(2000)

static bool is_throttling;
static int throttle_lowest_index;
static int throttle_highest_index;
static int throttle_index;
static int throttle_next_index;
static struct delayed_work throttle_work;
static struct workqueue_struct *workqueue;

#define tegra_cpu_is_throttling() (is_throttling)

static void tegra_throttle_work_func(struct work_struct *work)
{
	unsigned int current_freq;

	mutex_lock(&tegra_cpu_lock);
	current_freq = tegra_getspeed(0);
	throttle_index = throttle_next_index;

	if (freq_table[throttle_index].frequency < current_freq)
		tegra_update_cpu_speed(freq_table[throttle_index].frequency);

	if (throttle_index > throttle_lowest_index) {
		throttle_next_index = throttle_index - 1;
		queue_delayed_work(workqueue, &throttle_work, THROTTLE_DELAY);
	}

	mutex_unlock(&tegra_cpu_lock);
}

/*
 * tegra_throttling_enable
 * This function may sleep
 */
void tegra_throttling_enable(bool enable)
{
	mutex_lock(&tegra_cpu_lock);

	if (enable && !is_throttling) {
		unsigned int current_freq = tegra_getspeed(0);

		is_throttling = true;

		for (throttle_index = throttle_highest_index;
		     throttle_index >= throttle_lowest_index;
		     throttle_index--)
			if (freq_table[throttle_index].frequency
			    < current_freq)
				break;

		throttle_index = max(throttle_index, throttle_lowest_index);
		throttle_next_index = throttle_index;
		queue_delayed_work(workqueue, &throttle_work, 0);
	} else if (!enable && is_throttling) {
		cancel_delayed_work_sync(&throttle_work);
		is_throttling = false;
		/* restore speed requested by governor */
		tegra_update_cpu_speed(tegra_cpu_highest_speed());
	}

	mutex_unlock(&tegra_cpu_lock);
}
EXPORT_SYMBOL_GPL(tegra_throttling_enable);

static unsigned int throttle_governor_speed(unsigned int requested_speed)
{
	return tegra_cpu_is_throttling() ?
		min(requested_speed, freq_table[throttle_index].frequency) :
		requested_speed;
}

static ssize_t show_throttle(struct cpufreq_policy *policy, char *buf)
{
	return sprintf(buf, "%u\n", is_throttling);
}

cpufreq_freq_attr_ro(throttle);

#ifdef CONFIG_DEBUG_FS
static int throttle_debug_set(void *data, u64 val)
{
	tegra_throttling_enable(val);
	return 0;
}
static int throttle_debug_get(void *data, u64 *val)
{
	*val = (u64) is_throttling;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(throttle_fops, throttle_debug_get, throttle_debug_set, "%llu\n");

#ifdef CONFIG_TEGRA_CPU_FREQ_LOCK
/*
 * At first, set cpulock_debug_timeout,
 * then set specific cpu frequency for locking
 */
static int cpulock_debug_set(void *data, u64 val)
{
	if (val)
		tegra_cpu_lock_speed(val, cpulock_debug_timeout);
	else {
		/* if val == 0, cpufreq will be unlocked */
		tegra_cpu_unlock_speed();
	}


	return 0;
}
static int cpulock_debug_get(void *data, u64 *val)
{
	*val = (u64) cpulock_freq;
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(cpulock_fops, cpulock_debug_get, cpulock_debug_set, "%llu\n");

/* if timeout is 0, cpufreq will be locked infinitely */
static int cpulock_timeout_debug_set(void *data, u64 timeout_ms)
{
	cpulock_debug_timeout = timeout_ms;
	return 0;
}
static int cpulock_timeout_debug_get(void *data, u64 *val)
{
	*val = (u64) cpulock_debug_timeout;
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(cpulock_timeout_fops, cpulock_timeout_debug_get, cpulock_timeout_debug_set, "%llu\n");
#endif /* CONFIG_TEGRA_CPU_FREQ_LOCK */

static struct dentry *cpu_tegra_debugfs_root;

static int __init tegra_cpu_debug_init(void)
{
	cpu_tegra_debugfs_root = debugfs_create_dir("cpu-tegra", 0);

	if (!cpu_tegra_debugfs_root)
		return -ENOMEM;

	if (!debugfs_create_file("throttle", 0644, cpu_tegra_debugfs_root, NULL, &throttle_fops))
		goto err_out;

#ifdef CONFIG_TEGRA_CPU_FREQ_LOCK 
	/*
	 * root permission is required for testing
	 * /d/cpu-tegra/cpulock
	 * /d/cpu-tegra/cpulock_timeout
	 */
	if (!debugfs_create_file("cpulock", 0644, cpu_tegra_debugfs_root, NULL, &cpulock_fops))
		goto err_out;
	if (!debugfs_create_file("cpulock_timeout", 0644, cpu_tegra_debugfs_root, NULL, &cpulock_timeout_fops))
		goto err_out;
#endif /* CONFIG_TEGRA_CPU_FREQ_LOCK */

	return 0;

err_out:
	debugfs_remove_recursive(cpu_tegra_debugfs_root);
	return -ENOMEM;

}

static void __exit tegra_cpu_debug_exit(void)
{
	debugfs_remove_recursive(cpu_tegra_debugfs_root);
}

late_initcall(tegra_cpu_debug_init);
module_exit(tegra_cpu_debug_exit);
#endif /* CONFIG_DEBUG_FS */

#else /* CONFIG_TEGRA_THERMAL_THROTTLE */
#define tegra_cpu_is_throttling() (0)
#define throttle_governor_speed(requested_speed) (requested_speed)

void tegra_throttling_enable(bool enable)
{
}
#endif /* CONFIG_TEGRA_THERMAL_THROTTLE */

int tegra_verify_speed(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, freq_table);
}

unsigned int tegra_getspeed(unsigned int cpu)
{
	unsigned long rate;

	if (cpu >= NUM_CPUS)
		return 0;

	rate = clk_get_rate(cpu_clk) / 1000;
	return rate;
}

static int tegra_update_cpu_speed(unsigned long rate)
{
	int ret = 0;
	struct cpufreq_freqs freqs;

	freqs.old = tegra_getspeed(0);
#ifdef CONFIG_TEGRA_CPU_FREQ_LOCK
	/*
	 * Thermal throttling supersedes cpufreq lock.
	 * cpufreq goes down to minimum during the suspend mode.
	 */
	if (!tegra_cpu_is_throttling() && is_cpufreq_locked && !is_suspended && (rate < cpulock_freq))
		freqs.new = cpulock_freq;
	else
		freqs.new = rate;
#else
	freqs.new = rate;
#endif /* CONFIG_TEGRA_CPU_FREQ_LOCK */	

	if (freqs.old == freqs.new)
		return ret;

	/*
	 * Vote on memory bus frequency based on cpu frequency
	 * This sets the minimum frequency, display or avp may request higher
	 */
	if (rate >= 816000)
		clk_set_rate(emc_clk, 600000000); /* cpu 816 MHz, emc max */
	else if (rate >= 608000)
		clk_set_rate(emc_clk, 300000000); /* cpu 608 MHz, emc 150Mhz */
	else if (rate >= 456000)
		clk_set_rate(emc_clk, 150000000); /* cpu 456 MHz, emc 75Mhz */
	else if (rate >= 312000)
		clk_set_rate(emc_clk, 100000000); /* cpu 312 MHz, emc 50Mhz */
	else
		clk_set_rate(emc_clk, 50000000);  /* emc 25Mhz */

	for_each_online_cpu(freqs.cpu)
		cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

#ifdef CONFIG_CPU_FREQ_DEBUG
	printk(KERN_DEBUG "cpufreq-tegra: transition: %u --> %u\n",
	       freqs.old, freqs.new);
#endif

	ret = clk_set_rate(cpu_clk, freqs.new * 1000);
	if (ret) {
		pr_err("cpu-tegra: Failed to set cpu frequency to %d kHz\n",
			freqs.new);
		return ret;
	}

	for_each_online_cpu(freqs.cpu)
		cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return 0;
}



#ifdef CONFIG_TEGRA_CPU_FREQ_LOCK
/*
 * c.f. cpufreq_frequency_table in tegra2_clocks.c
 * 216000, 312000, 456000, 608000, 760000, 816000, 912000, 1000000 etc.
 * min_rate is in KHz.
 */
void tegra_cpu_lock_speed(int min_rate, int timeout_ms)
{
	int idx = 0,found = 0;

	/* cpu frequency validity test */
	while (freq_table[idx].frequency != CPUFREQ_TABLE_END) {
		if (freq_table[idx++].frequency == min_rate) {
			found = 1;
			break;
		}
	}
	if (!found) {
		pr_err("cpu-tegra: Failed to lock cpu frequency to %d kHz\n", min_rate);
		return;
	}

	mutex_lock(&tegra_cpulock_lock);

	printk(KERN_DEBUG "%s: min_rate(%d),timeout(%d)\n",
	       __func__, min_rate, timeout_ms);
	cpulock_freq = min_rate;
	is_cpufreq_locked = true;
	tegra_update_cpu_speed(tegra_getspeed(0));
	if (timeout_ms) {
		hrtimer_cancel(&cpulock_timer);
		hrtimer_start(&cpulock_timer,
			ns_to_ktime((u64)timeout_ms * NSEC_PER_MSEC),
			HRTIMER_MODE_REL);
	}

	mutex_unlock(&tegra_cpulock_lock);
}
EXPORT_SYMBOL_GPL(tegra_cpu_lock_speed);

void tegra_cpu_unlock_speed(void)
{
	mutex_lock(&tegra_cpulock_lock);

	cpulock_freq = 0;
	is_cpufreq_locked = false;
	hrtimer_cancel(&cpulock_timer);

	mutex_unlock(&tegra_cpulock_lock);
}
EXPORT_SYMBOL_GPL(tegra_cpu_unlock_speed);

static enum hrtimer_restart tegra_cpulock_timer_func(struct hrtimer *timer)
{
	cpulock_freq = 0;
	is_cpufreq_locked = false;
	printk(KERN_DEBUG "%s is called\n", __func__);

	return HRTIMER_NORESTART;
}
#endif /* CONFIG_TEGRA_CPU_FREQ_LOCK */
static unsigned long tegra_cpu_highest_speed(void) {
	unsigned long rate = 0;
	int i;

	for_each_online_cpu(i)
		rate = max(rate, target_cpu_speed[i]);
	return rate;
}

static int tegra_target(struct cpufreq_policy *policy,
		       unsigned int target_freq,
		       unsigned int relation)
{
	int idx;
	unsigned int freq;
	unsigned int new_speed;
	int ret = 0;

	mutex_lock(&tegra_cpu_lock);

	if (is_suspended) {
		ret = -EBUSY;
		goto out;
	}

	cpufreq_frequency_table_target(policy, freq_table, target_freq,
		relation, &idx);

	freq = freq_table[idx].frequency;

	target_cpu_speed[policy->cpu] = freq;
	new_speed = throttle_governor_speed(tegra_cpu_highest_speed());
	ret = tegra_update_cpu_speed(new_speed);
out:
	mutex_unlock(&tegra_cpu_lock);
	return ret;
}


static int tegra_pm_notify(struct notifier_block *nb, unsigned long event,
	void *dummy)
{
	mutex_lock(&tegra_cpu_lock);
	if (event == PM_SUSPEND_PREPARE) {
		is_suspended = true;
		pr_info("Tegra cpufreq suspend: setting frequency to %d kHz\n",
			freq_table[0].frequency);
		tegra_update_cpu_speed(freq_table[0].frequency);
	} else if (event == PM_POST_SUSPEND) {
		is_suspended = false;
	}
	mutex_unlock(&tegra_cpu_lock);

	return NOTIFY_OK;
}

static struct notifier_block tegra_cpu_pm_notifier = {
	.notifier_call = tegra_pm_notify,
};

static int tegra_cpu_init(struct cpufreq_policy *policy)
{
	if (policy->cpu >= NUM_CPUS)
		return -EINVAL;

	cpu_clk = clk_get_sys(NULL, "cpu");
	if (IS_ERR(cpu_clk))
		return PTR_ERR(cpu_clk);

	emc_clk = clk_get_sys("cpu", "emc");
	if (IS_ERR(emc_clk)) {
		clk_put(cpu_clk);
		return PTR_ERR(emc_clk);
	}
	clk_enable(emc_clk);

	cpufreq_frequency_table_cpuinfo(policy, freq_table);
	cpufreq_frequency_table_get_attr(freq_table, policy->cpu);
	policy->cur = tegra_getspeed(policy->cpu);
	target_cpu_speed[policy->cpu] = policy->cur;

	/* FIXME: what's the actual transition time? */
	policy->cpuinfo.transition_latency = 300 * 1000;

	policy->shared_type = CPUFREQ_SHARED_TYPE_ALL;
	cpumask_copy(policy->related_cpus, cpu_possible_mask);

	if (policy->cpu == 0) {
		register_pm_notifier(&tegra_cpu_pm_notifier);
	}

	return 0;
}

static int tegra_cpu_exit(struct cpufreq_policy *policy)
{
	cpufreq_frequency_table_cpuinfo(policy, freq_table);
	clk_disable(emc_clk);
	clk_put(emc_clk);
	clk_put(cpu_clk);
	return 0;
}

static struct freq_attr *tegra_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
#ifdef CONFIG_TEGRA_THERMAL_THROTTLE
	&throttle,
#endif
	NULL,
};

static struct cpufreq_driver tegra_cpufreq_driver = {
	.verify		= tegra_verify_speed,
	.target		= tegra_target,
	.get		= tegra_getspeed,
	.init		= tegra_cpu_init,
	.exit		= tegra_cpu_exit,
	.name		= "tegra",
	.attr		= tegra_cpufreq_attr,
};

static int __init tegra_cpufreq_init(void)
{
	struct tegra_cpufreq_table_data *table_data =
		tegra_cpufreq_table_get();
	BUG_ON(!table_data);

#ifdef CONFIG_TEGRA_THERMAL_THROTTLE
	/*
	 * High-priority, others flags default: not bound to a specific
	 * CPU, has rescue worker task (in case of allocation deadlock,
	 * etc.).  Single-threaded.
	 */
	workqueue = alloc_workqueue("cpu-tegra",
				    WQ_HIGHPRI | WQ_UNBOUND | WQ_RESCUER, 1);
	if (!workqueue)
		return -ENOMEM;
	INIT_DELAYED_WORK(&throttle_work, tegra_throttle_work_func);

	throttle_lowest_index = table_data->throttle_lowest_index;
	throttle_highest_index = table_data->throttle_highest_index;
#endif
#ifdef CONFIG_TEGRA_CPU_FREQ_LOCK
	hrtimer_init(&cpulock_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	cpulock_timer.function = tegra_cpulock_timer_func;
#endif /* CONFIG_TEGRA_CPU_FREQ_LOCK */
	freq_table = table_data->freq_table;
	return cpufreq_register_driver(&tegra_cpufreq_driver);
}

static void __exit tegra_cpufreq_exit(void)
{
#ifdef CONFIG_TEGRA_THERMAL_THROTTLE
	destroy_workqueue(workqueue);
#endif
        cpufreq_unregister_driver(&tegra_cpufreq_driver);
}


MODULE_AUTHOR("Colin Cross <ccross@android.com>");
MODULE_DESCRIPTION("cpufreq driver for Nvidia Tegra2");
MODULE_LICENSE("GPL");
module_init(tegra_cpufreq_init);
module_exit(tegra_cpufreq_exit);
