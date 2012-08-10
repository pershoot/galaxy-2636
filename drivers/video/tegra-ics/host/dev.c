/*
 * drivers/video/tegra/host/dev.c
 *
 * Tegra Graphics Host Driver Entrypoint
 *
 * Copyright (c) 2010-2012, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "dev.h"
#include "bus_client.h"

#include <linux/slab.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/file.h>
#include <linux/clk.h>
#include <linux/hrtimer.h>
#define CREATE_TRACE_POINTS
#include <trace/events/nvhost.h>

#include <linux/io.h>

#include <linux/nvhost.h>
#include <linux/nvhost_ioctl.h>
#include <mach/nvmap.h>
#include <mach/gpufuse.h>
#include <mach/hardware.h>
#include <mach/iomap.h>

#include "debug.h"
#include "nvhost_job.h"
#include "t20/t20.h"

#define DRIVER_NAME            "host1x"
#define IFACE_NAME             "nvhost"
#define TRACE_MAX_LENGTH       128U

static int nvhost_major = NVHOST_MAJOR;
static int nvhost_minor;
static unsigned int register_sets;

struct nvhost_channel_userctx {
	struct nvhost_channel *ch;
	struct nvhost_hwctx *hwctx;
	struct nvhost_submit_hdr_ext hdr;
	int num_relocshifts;
	struct nvhost_job *job;
	struct nvmap_client *nvmap;
	u32 timeout;
	u32 priority;
	int clientid;
};

struct nvhost_ctrl_userctx {
	struct nvhost_master *dev;
	u32 *mod_locks;
};

/*
 * Write cmdbuf to ftrace output. Checks if cmdbuf contents should be output
 * and mmaps the cmdbuf contents if required.
 */
static void trace_write_cmdbufs(struct nvhost_job *job)
{
#if defined(CONFIG_TEGRA_NVMAP)
	struct nvmap_handle_ref handle;
	void *mem = NULL;
	int i = 0;

	for (i = 0; i < job->num_gathers; i++) {
		struct nvhost_channel_gather *gather = &job->gathers[i];
		if (nvhost_debug_trace_cmdbuf) {
			handle.handle = nvmap_id_to_handle(gather->mem_id);
			mem = nvmap_mmap(&handle);
			if (IS_ERR_OR_NULL(mem))
				mem = NULL;
		};

		if (mem) {
			u32 i;
			/*
			 * Write in batches of 128 as there seems to be a limit
			 * of how much you can output to ftrace at once.
			 */
			for (i = 0; i < gather->words; i += TRACE_MAX_LENGTH) {
				trace_nvhost_channel_write_cmdbuf_data(
					job->ch->dev->name,
					gather->mem_id,
					min(gather->words - i,
					    TRACE_MAX_LENGTH),
					gather->offset + i * sizeof(u32),
					mem);
			}
			nvmap_munmap(&handle, mem);
		}
	}
#endif
}

static int nvhost_channelrelease(struct inode *inode, struct file *filp)
{
	struct nvhost_channel_userctx *priv = filp->private_data;

	trace_nvhost_channel_release(priv->ch->dev->name);

	filp->private_data = NULL;

	nvhost_module_remove_client(priv->ch->dev, priv);
	nvhost_putchannel(priv->ch, priv->hwctx);

	if (priv->hwctx)
		priv->ch->ctxhandler->put(priv->hwctx);

	if (priv->job)
		nvhost_job_put(priv->job);

	nvmap_client_put(priv->nvmap);
	kfree(priv);
	return 0;
}

static int nvhost_channelopen(struct inode *inode, struct file *filp)
{
	struct nvhost_channel_userctx *priv;
	struct nvhost_channel *ch;

	ch = container_of(inode->i_cdev, struct nvhost_channel, cdev);
	ch = nvhost_getchannel(ch);
	if (!ch)
		return -ENOMEM;
	trace_nvhost_channel_open(ch->dev->name);

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		nvhost_putchannel(ch, NULL);
		return -ENOMEM;
	}
	filp->private_data = priv;
	priv->ch = ch;
	nvhost_module_add_client(ch->dev, priv);

	if (ch->ctxhandler && ch->ctxhandler->alloc) {
		priv->hwctx = ch->ctxhandler->alloc(ch->ctxhandler, ch);
		if (!priv->hwctx)
			goto fail;
	}
	priv->priority = NVHOST_PRIORITY_MEDIUM;
	priv->clientid = atomic_add_return(1,
			&nvhost_get_host(ch->dev)->clientid);
	priv->timeout = MAX_STUCK_CHECK_COUNT * SYNCPT_CHECK_PERIOD;

	priv->job = nvhost_job_alloc(ch, priv->hwctx, &priv->hdr,
			NULL, priv->priority, priv->clientid);
	if (!priv->job)
		goto fail;

	return 0;
fail:
	nvhost_channelrelease(inode, filp);
	return -ENOMEM;
}

static int set_submit(struct nvhost_channel_userctx *ctx)
{
	struct device *device = &ctx->ch->dev->dev;

	/* submit should have at least 1 cmdbuf */
	if (!ctx->hdr.num_cmdbufs)
		return -EIO;

	if (!ctx->nvmap) {
		dev_err(device, "no nvmap context set\n");
		return -EFAULT;
	}

	ctx->job = nvhost_job_realloc(ctx->job,
			&ctx->hdr,
			ctx->nvmap,
			ctx->priority,
			ctx->clientid);
	if (!ctx->job)
		return -ENOMEM;
	ctx->job->timeout = ctx->timeout;

	if (ctx->hdr.submit_version >= NVHOST_SUBMIT_VERSION_V2)
		ctx->num_relocshifts = ctx->hdr.num_relocs;

	return 0;
}

static void reset_submit(struct nvhost_channel_userctx *ctx)
{
	ctx->hdr.num_cmdbufs = 0;
	ctx->hdr.num_relocs = 0;
	ctx->num_relocshifts = 0;
	ctx->hdr.num_waitchks = 0;
}

static ssize_t nvhost_channelwrite(struct file *filp, const char __user *buf,
				size_t count, loff_t *offp)
{
	struct nvhost_channel_userctx *priv = filp->private_data;
	size_t remaining = count;
	int err = 0;
	struct nvhost_job *job = priv->job;
	struct nvhost_submit_hdr_ext *hdr = &priv->hdr;
	const char *chname = priv->ch->dev->name;

	if (!job)
		return -EIO;

	while (remaining) {
		size_t consumed;
		if (!hdr->num_relocs &&
		    !priv->num_relocshifts &&
		    !hdr->num_cmdbufs &&
		    !hdr->num_waitchks) {
			consumed = sizeof(struct nvhost_submit_hdr);
			if (remaining < consumed)
				break;
			if (copy_from_user(hdr, buf, consumed)) {
				err = -EFAULT;
				break;
			}
			hdr->submit_version = NVHOST_SUBMIT_VERSION_V0;
			err = set_submit(priv);
			if (err)
				break;
			trace_nvhost_channel_write_submit(chname,
			  count, hdr->num_cmdbufs, hdr->num_relocs,
			  hdr->syncpt_id, hdr->syncpt_incrs);
		} else if (hdr->num_cmdbufs) {
			struct nvhost_cmdbuf cmdbuf;
			consumed = sizeof(cmdbuf);
			if (remaining < consumed)
				break;
			if (copy_from_user(&cmdbuf, buf, consumed)) {
				err = -EFAULT;
				break;
			}
			trace_nvhost_channel_write_cmdbuf(chname,
				cmdbuf.mem, cmdbuf.words, cmdbuf.offset);
			nvhost_job_add_gather(job,
				cmdbuf.mem, cmdbuf.words, cmdbuf.offset);
			hdr->num_cmdbufs--;
		} else if (hdr->num_relocs) {
			struct nvmap_pinarray_elem *elem =
						&job->pinarray[job->num_pins];
			consumed = sizeof(struct nvhost_reloc);
			if (remaining < consumed)
				break;
			if (copy_from_user(elem, buf, consumed)) {
				err = -EFAULT;
				break;
			}
			elem->patch_mem =
				nvmap_convert_handle_u2k(elem->patch_mem);
			elem->pin_mem =
				nvmap_convert_handle_u2k(elem->pin_mem);
			trace_nvhost_channel_write_reloc(chname);
			job->num_pins++;
			hdr->num_relocs--;
		} else if (hdr->num_waitchks) {
			struct nvhost_waitchk *waitchk =
					&job->waitchk[job->num_waitchk];
			consumed = sizeof(struct nvhost_waitchk);
			if (remaining < consumed)
				break;
			if (copy_from_user(waitchk, buf, consumed)) {
				err = -EFAULT;
				break;
			}
			waitchk->mem = nvmap_convert_handle_u2k(waitchk->mem);
			trace_nvhost_channel_write_waitchks(
			  chname, 1,
			  hdr->waitchk_mask);
			job->num_waitchk++;
			hdr->num_waitchks--;
		} else if (priv->num_relocshifts) {
			int next_shift =
				job->num_pins - priv->num_relocshifts;
			consumed = sizeof(struct nvhost_reloc_shift);
			if (remaining < consumed)
				break;
			if (copy_from_user(
					&job->pinarray[next_shift].reloc_shift,
					buf, consumed)) {
				err = -EFAULT;
				break;
			}
			priv->num_relocshifts--;
		} else {
			err = -EFAULT;
			break;
		}
		remaining -= consumed;
		buf += consumed;
	}

	if (err < 0) {
		dev_err(&priv->ch->dev->dev, "channel write error\n");
		reset_submit(priv);
		return err;
	}

	return count - remaining;
}

static int nvhost_ioctl_channel_flush(
	struct nvhost_channel_userctx *ctx,
	struct nvhost_get_param_args *args,
	int null_kickoff)
{
	struct device *device = &ctx->ch->dev->dev;
	int err;

	trace_nvhost_ioctl_channel_flush(ctx->ch->dev->name);

	if (!ctx->job ||
	    ctx->hdr.num_relocs ||
	    ctx->hdr.num_cmdbufs ||
	    ctx->hdr.num_waitchks) {
		reset_submit(ctx);
		dev_err(device, "channel submit out of sync\n");
		return -EFAULT;
	}

	err = nvhost_job_pin(ctx->job);
	if (err) {
		dev_warn(device, "nvhost_job_pin failed: %d\n", err);
		return err;
	}

	if (nvhost_debug_null_kickoff_pid == current->tgid)
		null_kickoff = 1;
	ctx->job->null_kickoff = null_kickoff;

	if ((nvhost_debug_force_timeout_pid == current->tgid) &&
	    (nvhost_debug_force_timeout_channel == ctx->ch->chid)) {
		ctx->timeout = nvhost_debug_force_timeout_val;
	}

	trace_write_cmdbufs(ctx->job);

	/* context switch if needed, and submit user's gathers to the channel */
	err = nvhost_channel_submit(ctx->job);
	args->value = ctx->job->syncpt_end;
	if (err)
		nvhost_job_unpin(ctx->job);

	return err;
}

static int nvhost_ioctl_channel_read_3d_reg(
	struct nvhost_channel_userctx *ctx,
	struct nvhost_read_3d_reg_args *args)
{
	BUG_ON(!channel_op(ctx->ch).read3dreg);
	return channel_op(ctx->ch).read3dreg(ctx->ch, ctx->hwctx,
			args->offset, &args->value);
}

static long nvhost_channelctl(struct file *filp,
	unsigned int cmd, unsigned long arg)
{
	struct nvhost_channel_userctx *priv = filp->private_data;
	u8 buf[NVHOST_IOCTL_CHANNEL_MAX_ARG_SIZE];
	int err = 0;

	if ((_IOC_TYPE(cmd) != NVHOST_IOCTL_MAGIC) ||
		(_IOC_NR(cmd) == 0) ||
		(_IOC_NR(cmd) > NVHOST_IOCTL_CHANNEL_LAST))
		return -EFAULT;

	BUG_ON(_IOC_SIZE(cmd) > NVHOST_IOCTL_CHANNEL_MAX_ARG_SIZE);

	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		if (copy_from_user(buf, (void __user *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;
	}

	switch (cmd) {
	case NVHOST_IOCTL_CHANNEL_FLUSH:
		err = nvhost_ioctl_channel_flush(priv, (void *)buf, 0);
		break;
	case NVHOST_IOCTL_CHANNEL_NULL_KICKOFF:
		err = nvhost_ioctl_channel_flush(priv, (void *)buf, 1);
		break;
	case NVHOST_IOCTL_CHANNEL_SUBMIT_EXT:
	{
		struct nvhost_submit_hdr_ext *hdr;

		if (priv->hdr.num_relocs ||
		    priv->num_relocshifts ||
		    priv->hdr.num_cmdbufs ||
		    priv->hdr.num_waitchks) {
			reset_submit(priv);
			dev_err(&priv->ch->dev->dev,
				"channel submit out of sync\n");
			err = -EIO;
			break;
		}

		hdr = (struct nvhost_submit_hdr_ext *)buf;
		if (hdr->submit_version > NVHOST_SUBMIT_VERSION_MAX_SUPPORTED) {
			dev_err(&priv->ch->dev->dev,
				"submit version %d > max supported %d\n",
				hdr->submit_version,
				NVHOST_SUBMIT_VERSION_MAX_SUPPORTED);
			err = -EINVAL;
			break;
		}
		memcpy(&priv->hdr, hdr, sizeof(struct nvhost_submit_hdr_ext));
		err = set_submit(priv);
		trace_nvhost_ioctl_channel_submit(priv->ch->dev->name,
			priv->hdr.submit_version,
			priv->hdr.num_cmdbufs, priv->hdr.num_relocs,
			priv->hdr.num_waitchks,
			priv->hdr.syncpt_id, priv->hdr.syncpt_incrs);
		break;
	}
	case NVHOST_IOCTL_CHANNEL_GET_SYNCPOINTS:
		/* host syncpt ID is used by the RM (and never be given out) */
		BUG_ON(priv->ch->dev->syncpts & (1 << NVSYNCPT_GRAPHICS_HOST));
		((struct nvhost_get_param_args *)buf)->value =
			priv->ch->dev->syncpts;
		break;
	case NVHOST_IOCTL_CHANNEL_GET_WAITBASES:
		((struct nvhost_get_param_args *)buf)->value =
			priv->ch->dev->waitbases;
		break;
	case NVHOST_IOCTL_CHANNEL_GET_MODMUTEXES:
		((struct nvhost_get_param_args *)buf)->value =
			priv->ch->dev->modulemutexes;
		break;
	case NVHOST_IOCTL_CHANNEL_SET_NVMAP_FD:
	{
		int fd = (int)((struct nvhost_set_nvmap_fd_args *)buf)->fd;
		struct nvmap_client *new_client = nvmap_client_get_file(fd);

		if (IS_ERR(new_client)) {
			err = PTR_ERR(new_client);
			break;
		}

		if (priv->nvmap)
			nvmap_client_put(priv->nvmap);

		priv->nvmap = new_client;
		break;
	}
	case NVHOST_IOCTL_CHANNEL_READ_3D_REG:
		err = nvhost_ioctl_channel_read_3d_reg(priv, (void *)buf);
		break;
	case NVHOST_IOCTL_CHANNEL_GET_CLK_RATE:
	{
		unsigned long rate;
		struct nvhost_clk_rate_args *arg =
				(struct nvhost_clk_rate_args *)buf;

		err = nvhost_module_get_rate(priv->ch->dev, &rate, 0);
		if (err == 0)
			arg->rate = rate;
		break;
	}
	case NVHOST_IOCTL_CHANNEL_SET_CLK_RATE:
	{
		struct nvhost_clk_rate_args *arg =
				(struct nvhost_clk_rate_args *)buf;
		unsigned long rate = (unsigned long)arg->rate;

		err = nvhost_module_set_rate(priv->ch->dev, priv, rate, 0);
		break;
	}
	case NVHOST_IOCTL_CHANNEL_SET_TIMEOUT:
		priv->timeout =
			(u32)((struct nvhost_set_timeout_args *)buf)->timeout;
		dev_dbg(&priv->ch->dev->dev,
			"%s: setting buffer timeout (%d ms) for userctx 0x%p\n",
			__func__, priv->timeout, priv);
		break;
	case NVHOST_IOCTL_CHANNEL_GET_TIMEDOUT:
		((struct nvhost_get_param_args *)buf)->value =
				priv->hwctx->has_timedout;
		break;
	case NVHOST_IOCTL_CHANNEL_SET_PRIORITY:
		priv->priority =
			(u32)((struct nvhost_set_priority_args *)buf)->priority;
		break;
	default:
		err = -ENOTTY;
		break;
	}

	if ((err == 0) && (_IOC_DIR(cmd) & _IOC_READ))
		err = copy_to_user((void __user *)arg, buf, _IOC_SIZE(cmd));

	return err;
}

static const struct file_operations nvhost_channelops = {
	.owner = THIS_MODULE,
	.release = nvhost_channelrelease,
	.open = nvhost_channelopen,
	.write = nvhost_channelwrite,
	.unlocked_ioctl = nvhost_channelctl
};

static int nvhost_ctrlrelease(struct inode *inode, struct file *filp)
{
	struct nvhost_ctrl_userctx *priv = filp->private_data;
	int i;

	trace_nvhost_ctrlrelease(priv->dev->dev->name);

	filp->private_data = NULL;
	if (priv->mod_locks[0])
		nvhost_module_idle(priv->dev->dev);
	for (i = 1; i < priv->dev->syncpt.nb_mlocks; i++)
		if (priv->mod_locks[i])
			nvhost_mutex_unlock(&priv->dev->syncpt, i);
	kfree(priv->mod_locks);
	kfree(priv);
	return 0;
}

static int nvhost_ctrlopen(struct inode *inode, struct file *filp)
{
	struct nvhost_master *host = container_of(inode->i_cdev, struct nvhost_master, cdev);
	struct nvhost_ctrl_userctx *priv;
	u32 *mod_locks;

	trace_nvhost_ctrlopen(host->dev->name);

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	mod_locks = kzalloc(sizeof(u32) * host->syncpt.nb_mlocks, GFP_KERNEL);

	if (!(priv && mod_locks)) {
		kfree(priv);
		kfree(mod_locks);
		return -ENOMEM;
	}

	priv->dev = host;
	priv->mod_locks = mod_locks;
	filp->private_data = priv;
	return 0;
}

static int nvhost_ioctl_ctrl_syncpt_read(
	struct nvhost_ctrl_userctx *ctx,
	struct nvhost_ctrl_syncpt_read_args *args)
{
	if (args->id >= ctx->dev->syncpt.nb_pts)
		return -EINVAL;
	args->value = nvhost_syncpt_read(&ctx->dev->syncpt, args->id);
	trace_nvhost_ioctl_ctrl_syncpt_read(args->id, args->value);
	return 0;
}

static int nvhost_ioctl_ctrl_syncpt_incr(
	struct nvhost_ctrl_userctx *ctx,
	struct nvhost_ctrl_syncpt_incr_args *args)
{
	if (args->id >= ctx->dev->syncpt.nb_pts)
		return -EINVAL;
	trace_nvhost_ioctl_ctrl_syncpt_incr(args->id);
	nvhost_syncpt_incr(&ctx->dev->syncpt, args->id);
	return 0;
}

static int nvhost_ioctl_ctrl_syncpt_waitex(
	struct nvhost_ctrl_userctx *ctx,
	struct nvhost_ctrl_syncpt_waitex_args *args)
{
	u32 timeout;
	int err;
	if (args->id >= ctx->dev->syncpt.nb_pts)
		return -EINVAL;
	if (args->timeout == NVHOST_NO_TIMEOUT)
		timeout = MAX_SCHEDULE_TIMEOUT;
	else
		timeout = (u32)msecs_to_jiffies(args->timeout);

	err = nvhost_syncpt_wait_timeout(&ctx->dev->syncpt, args->id,
					args->thresh, timeout, &args->value);
	trace_nvhost_ioctl_ctrl_syncpt_wait(args->id, args->thresh,
	  args->timeout, args->value, err);

	return err;
}

static int nvhost_ioctl_ctrl_module_mutex(
	struct nvhost_ctrl_userctx *ctx,
	struct nvhost_ctrl_module_mutex_args *args)
{
	int err = 0;
	if (args->id >= ctx->dev->syncpt.nb_mlocks ||
	    args->lock > 1)
		return -EINVAL;

	trace_nvhost_ioctl_ctrl_module_mutex(args->lock, args->id);
	if (args->lock && !ctx->mod_locks[args->id]) {
		if (args->id == 0)
			nvhost_module_busy(ctx->dev->dev);
		else
			err = nvhost_mutex_try_lock(&ctx->dev->syncpt,
					args->id);
		if (!err)
			ctx->mod_locks[args->id] = 1;
	} else if (!args->lock && ctx->mod_locks[args->id]) {
		if (args->id == 0)
			nvhost_module_idle(ctx->dev->dev);
		else
			nvhost_mutex_unlock(&ctx->dev->syncpt, args->id);
		ctx->mod_locks[args->id] = 0;
	}
	return err;
}

static struct nvhost_device *get_ndev_by_moduleid(struct nvhost_master *host,
		u32 id)
{
	int i;

	for (i = 0; i < host->nb_channels; i++) {
		struct nvhost_device *ndev = host->channels[i].dev;
		if (id == ndev->moduleid)
			return ndev;
	}
	return NULL;
}

static int nvhost_ioctl_ctrl_module_regrdwr(
	struct nvhost_ctrl_userctx *ctx,
	struct nvhost_ctrl_module_regrdwr_args *args)
{
	u32 num_offsets = args->num_offsets;
	u32 *offsets = args->offsets;
	u32 *values = args->values;
	u32 vals[64];
	struct nvhost_device *ndev;

	trace_nvhost_ioctl_ctrl_module_regrdwr(args->id,
			args->num_offsets, args->write);
	/* Check that there is something to read and that block size is
	 * u32 aligned */
	if (num_offsets == 0 || args->block_size & 3)
		return -EINVAL;

	ndev = get_ndev_by_moduleid(ctx->dev, args->id);
	if (!ndev)
		return -EINVAL;

	while (num_offsets--) {
		int remaining = args->block_size >> 2;
		u32 offs;
		if (get_user(offs, offsets))
			return -EFAULT;
		offsets++;
		while (remaining) {
			int batch = min(remaining, 64);
			if (args->write) {
				if (copy_from_user(vals, values,
							batch*sizeof(u32)))
					return -EFAULT;
				nvhost_write_module_regs(ndev,
						offs, batch, vals);
			} else {
				nvhost_read_module_regs(ndev,
						offs, batch, vals);
				if (copy_to_user(values, vals,
							batch*sizeof(u32)))
					return -EFAULT;
			}
			remaining -= batch;
			offs += batch;
			values += batch;
		}
	}

	return 0;
}

static int nvhost_ioctl_ctrl_get_version(
	struct nvhost_ctrl_userctx *ctx,
	struct nvhost_get_param_args *args)
{
	args->value = NVHOST_SUBMIT_VERSION_MAX_SUPPORTED;
	return 0;
}

static long nvhost_ctrlctl(struct file *filp,
	unsigned int cmd, unsigned long arg)
{
	struct nvhost_ctrl_userctx *priv = filp->private_data;
	u8 buf[NVHOST_IOCTL_CTRL_MAX_ARG_SIZE];
	int err = 0;

	if ((_IOC_TYPE(cmd) != NVHOST_IOCTL_MAGIC) ||
		(_IOC_NR(cmd) == 0) ||
		(_IOC_NR(cmd) > NVHOST_IOCTL_CTRL_LAST))
		return -EFAULT;

	BUG_ON(_IOC_SIZE(cmd) > NVHOST_IOCTL_CTRL_MAX_ARG_SIZE);

	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		if (copy_from_user(buf, (void __user *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;
	}

	switch (cmd) {
	case NVHOST_IOCTL_CTRL_SYNCPT_READ:
		err = nvhost_ioctl_ctrl_syncpt_read(priv, (void *)buf);
		break;
	case NVHOST_IOCTL_CTRL_SYNCPT_INCR:
		err = nvhost_ioctl_ctrl_syncpt_incr(priv, (void *)buf);
		break;
	case NVHOST_IOCTL_CTRL_SYNCPT_WAIT:
		err = nvhost_ioctl_ctrl_syncpt_waitex(priv, (void *)buf);
		break;
	case NVHOST_IOCTL_CTRL_MODULE_MUTEX:
		err = nvhost_ioctl_ctrl_module_mutex(priv, (void *)buf);
		break;
	case NVHOST_IOCTL_CTRL_MODULE_REGRDWR:
		err = nvhost_ioctl_ctrl_module_regrdwr(priv, (void *)buf);
		break;
	case NVHOST_IOCTL_CTRL_SYNCPT_WAITEX:
		err = nvhost_ioctl_ctrl_syncpt_waitex(priv, (void *)buf);
		break;
	case NVHOST_IOCTL_CTRL_GET_VERSION:
		err = nvhost_ioctl_ctrl_get_version(priv, (void *)buf);
		break;
	default:
		err = -ENOTTY;
		break;
	}

	if ((err == 0) && (_IOC_DIR(cmd) & _IOC_READ))
		err = copy_to_user((void __user *)arg, buf, _IOC_SIZE(cmd));

	return err;
}

static const struct file_operations nvhost_ctrlops = {
	.owner = THIS_MODULE,
	.release = nvhost_ctrlrelease,
	.open = nvhost_ctrlopen,
	.unlocked_ioctl = nvhost_ctrlctl
};

static void power_on_host(struct nvhost_device *dev)
{
	struct nvhost_master *host = nvhost_get_drvdata(dev);
	nvhost_syncpt_reset(&host->syncpt);
	nvhost_intr_start(&host->intr, clk_get_rate(dev->clk[0]));
}

static int power_off_host(struct nvhost_device *dev)
{
	struct nvhost_master *host = nvhost_get_drvdata(dev);
	nvhost_syncpt_save(&host->syncpt);
	nvhost_intr_stop(&host->intr);
	return 0;
}

static int __devinit nvhost_user_init(struct nvhost_master *host)
{
	int i, err, devno;

	host->nvhost_class = class_create(THIS_MODULE, IFACE_NAME);
	if (IS_ERR(host->nvhost_class)) {
		err = PTR_ERR(host->nvhost_class);
		dev_err(&host->dev->dev, "failed to create class\n");
		goto fail;
	}

	err = alloc_chrdev_region(&devno, nvhost_minor,
				host->nb_channels + 1, IFACE_NAME);
	nvhost_major = MAJOR(devno);
	if (err < 0) {
		dev_err(&host->dev->dev, "failed to reserve chrdev region\n");
		goto fail;
	}

	for (i = 0; i < host->nb_channels; i++) {
		struct nvhost_channel *ch = &host->channels[i];

		cdev_init(&ch->cdev, &nvhost_channelops);
		ch->cdev.owner = THIS_MODULE;

		devno = MKDEV(nvhost_major, nvhost_minor + i);
		err = cdev_add(&ch->cdev, devno, 1);
		if (err < 0) {
			dev_err(&host->dev->dev, "failed to add chan %i cdev\n", i);
			goto fail;
		}
		ch->node = device_create(host->nvhost_class, NULL, devno, NULL,
				IFACE_NAME "-%s", ch->dev->name);
		if (IS_ERR(ch->node)) {
			err = PTR_ERR(ch->node);
			dev_err(&host->dev->dev, "failed to create chan %i device\n", i);
			goto fail;
		}
	}

	cdev_init(&host->cdev, &nvhost_ctrlops);
	host->cdev.owner = THIS_MODULE;
	devno = MKDEV(nvhost_major, nvhost_minor + host->nb_channels);
	err = cdev_add(&host->cdev, devno, 1);
	if (err < 0)
		goto fail;
	host->ctrl = device_create(host->nvhost_class, NULL, devno, NULL,
			IFACE_NAME "-ctrl");
	if (IS_ERR(host->ctrl)) {
		err = PTR_ERR(host->ctrl);
		dev_err(&host->dev->dev, "failed to create ctrl device\n");
		goto fail;
	}

	return 0;
fail:
	return err;
}

static void nvhost_remove_chip_support(struct nvhost_master *host)
{

	kfree(host->channels);
	host->channels = 0;

	kfree(host->intr.syncpt);
	host->intr.syncpt = 0;
}

static int __devinit nvhost_init_chip_support(struct nvhost_master *host)
{
	int err;
	switch (tegra_get_chipid()) {
	case TEGRA_CHIPID_TEGRA2:
		err = nvhost_init_t20_support(host);
		break;

	default:
		return -ENODEV;
	}

	if (err)
		return err;

	/* allocate items sized in chip specific support init */
	host->channels = kzalloc(sizeof(struct nvhost_channel) *
				 host->nb_channels, GFP_KERNEL);

	host->intr.syncpt = kzalloc(sizeof(struct nvhost_intr_syncpt) *
				    host->syncpt.nb_pts, GFP_KERNEL);

	if (!(host->channels && host->intr.syncpt)) {
		/* frees happen in the support removal phase */
		return -ENOMEM;
	}

	return 0;
}

static struct resource tegra_grhost_resources[] = {
	{
		.start = TEGRA_HOST1X_BASE,
		.end = TEGRA_HOST1X_BASE + TEGRA_HOST1X_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = TEGRA_DISPLAY_BASE,
		.end = TEGRA_DISPLAY_BASE + TEGRA_DISPLAY_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = TEGRA_DISPLAY2_BASE,
		.end = TEGRA_DISPLAY2_BASE + TEGRA_DISPLAY2_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = TEGRA_VI_BASE,
		.end = TEGRA_VI_BASE + TEGRA_VI_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = TEGRA_ISP_BASE,
		.end = TEGRA_ISP_BASE + TEGRA_ISP_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = TEGRA_MPE_BASE,
		.end = TEGRA_MPE_BASE + TEGRA_MPE_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = INT_SYNCPT_THRESH_BASE,
		.end = INT_SYNCPT_THRESH_BASE + INT_SYNCPT_THRESH_NR - 1,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = INT_HOST1X_MPCORE_GENERAL,
		.end = INT_HOST1X_MPCORE_GENERAL,
		.flags = IORESOURCE_IRQ,
	},
};

struct nvhost_device tegra_grhost_device = {
	.name = DRIVER_NAME,
	.id = -1,
	.resource = tegra_grhost_resources,
	.num_resources = ARRAY_SIZE(tegra_grhost_resources),
	.finalize_poweron = power_on_host,
	.prepare_poweroff = power_off_host,
	.clocks = {{"host1x", UINT_MAX}, {} },
	NVHOST_MODULE_NO_POWERGATE_IDS,
};

static int __devinit nvhost_probe(struct nvhost_device *dev)
{
	struct nvhost_master *host;
	struct resource *regs, *intr0, *intr1;
	int i, err;

	regs = nvhost_get_resource(dev, IORESOURCE_MEM, 0);
	intr0 = nvhost_get_resource(dev, IORESOURCE_IRQ, 0);
	intr1 = nvhost_get_resource(dev, IORESOURCE_IRQ, 1);

	if (!regs || !intr0 || !intr1) {
		dev_err(&dev->dev, "missing required platform resources\n");
		return -ENXIO;
	}

	host = kzalloc(sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->nvmap = nvmap_create_client(nvmap_dev, "nvhost");
	if (!host->nvmap) {
		dev_err(&dev->dev, "unable to create nvmap client\n");
		err = -EIO;
		goto fail;
	}

	host->reg_mem = request_mem_region(regs->start,
					resource_size(regs), dev->name);
	if (!host->reg_mem) {
		dev_err(&dev->dev, "failed to get host register memory\n");
		err = -ENXIO;
		goto fail;
	}

	host->aperture = ioremap(regs->start, resource_size(regs));
	if (!host->aperture) {
		dev_err(&dev->dev, "failed to remap host registers\n");
		err = -ENXIO;
		goto fail;
	}

	err = nvhost_init_chip_support(host);
	if (err) {
		dev_err(&dev->dev, "failed to init chip support\n");
		goto fail;
	}

	/*  Register host1x device as bus master */
	host->dev = dev;

	/*  Give pointer to host1x via driver */
	nvhost_set_drvdata(dev, host);

	nvhost_bus_add_host(host);

	BUG_ON(!host_channel_op(host).init);
	for (i = 0; i < host->nb_channels; i++) {
		struct nvhost_channel *ch = &host->channels[i];
		err = nvhost_channel_init(ch, host, i);
		if (err)
			goto fail;
	}

	err = nvhost_syncpt_init(&tegra_grhost_device, &host->syncpt);
	if (err)
		goto fail;

	err = nvhost_intr_init(&host->intr, intr1->start, intr0->start);
	if (err)
		goto fail;

	err = nvhost_user_init(host);
	if (err)
		goto fail;

	err = nvhost_module_init(&tegra_grhost_device);
	if (err)
		goto fail;

	for (i = 0; i < host->nb_channels; i++) {
		struct nvhost_channel *ch = &host->channels[i];
		nvhost_module_init(ch->dev);
	}

	for (i = 0; i < host->dev->num_clks; i++)
		clk_enable(host->dev->clk[i]);
	nvhost_syncpt_reset(&host->syncpt);
	for (i = 0; i < host->dev->num_clks; i++)
		clk_disable(host->dev->clk[0]);

	nvhost_debug_init(host);

	dev_info(&dev->dev, "initialized\n");
	return 0;

fail:
	nvhost_remove_chip_support(host);
	if (host->nvmap)
		nvmap_client_put(host->nvmap);
	kfree(host);
	return err;
}

static int __exit nvhost_remove(struct nvhost_device *dev)
{
	struct nvhost_master *host = nvhost_get_drvdata(dev);
	nvhost_intr_deinit(&host->intr);
	nvhost_syncpt_deinit(&host->syncpt);
	nvhost_remove_chip_support(host);
	return 0;
}

static int nvhost_suspend(struct nvhost_device *dev, pm_message_t state)
{
	struct nvhost_master *host = nvhost_get_drvdata(dev);
	int i, ret = 0;

	dev_info(&dev->dev, "suspending\n");

	for (i = 0; i < host->nb_channels; i++) {
		ret = nvhost_channel_suspend(&host->channels[i]);
		if (ret)
			return ret;
	}

	ret = nvhost_module_suspend(host->dev);
	dev_info(&dev->dev, "suspend status: %d\n", ret);

	return ret;
}

static int nvhost_resume(struct nvhost_device *dev)
{
	dev_info(&dev->dev, "resuming\n");
	return 0;
}

static struct nvhost_driver nvhost_driver = {
	.probe = nvhost_probe,
	.remove = __exit_p(nvhost_remove),
	.suspend = nvhost_suspend,
	.resume = nvhost_resume,
	.driver = {
		.owner = THIS_MODULE,
		.name = DRIVER_NAME
	}
};

static int __init nvhost_mod_init(void)
{
	register_sets = tegra_gpu_register_sets();
	return nvhost_driver_register(&nvhost_driver);
}

static void __exit nvhost_mod_exit(void)
{
	nvhost_driver_unregister(&nvhost_driver);
}

/* host1x master device needs nvmap to be instantiated first.
 * nvmap is instantiated via fs_initcall.
 * Hence instantiate host1x master device using rootfs_initcall
 * which is one level after fs_initcall. */
rootfs_initcall(nvhost_mod_init);
module_exit(nvhost_mod_exit);

module_param_call(register_sets, NULL, param_get_uint, &register_sets, 0444);
MODULE_PARM_DESC(register_sets, "Number of register sets");

MODULE_AUTHOR("NVIDIA");
MODULE_DESCRIPTION("Graphics host driver for Tegra products");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform-nvhost");
