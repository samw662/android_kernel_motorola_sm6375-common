// SPDX-License-Identifier: GPL-2.0-only
/* Optional, value-only EEVDF observation. Never refresh scheduler accounting. */
#include <linux/cpu.h>
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/smp.h>
#include <linux/string.h>

#include "sched.h"
#include "eevdf_monitor.h"

DEFINE_PER_CPU_SHARED_ALIGNED(struct weary_eevdf_counters, weary_eevdf_counters);

static const char * const weary_event_names[WEARY_EEVDF_EVENTS] = {
	"pick_eevdf_calls", "request_renewals", "protected_pick_returns",
	"preempt_short_nominations", "delayed_enter", "delayed_enqueue_complete",
	"delayed_dequeue_complete",
	"wakeups_retained", "yield_calls", "fair_migrations",
};

static void weary_status_print(struct seq_file *m, int cpu,
			       const struct weary_eevdf_snapshot *s)
{
	seq_printf(m, "cpu=%d online=1 rq_clock=%llu current_pid=%d",
		   cpu, s->clock, s->current_pid);
	if (s->comm_valid) {
		seq_puts(m, " current_comm=\"");
		seq_escape_mem_ascii(m, s->comm, strlen(s->comm));
		seq_putc(m, '"');
	}
	seq_printf(m, " comm_valid=%u\n", s->comm_valid);
	seq_printf(m, " nr_running=%u fair_queued=%u fair_runnable=%u delayed=%u\n",
		   s->nr_running, s->queued, s->runnable, s->delayed);
	seq_printf(m, " min_vruntime=%llu zero_vruntime=%llu V=%llu A=%lld W=%llu\n",
		   s->min_vruntime, s->zero_vruntime, s->V,
		   s->avg_vruntime, s->avg_load);
	seq_printf(m, " load_weight=%llu pelt_load=%lu pelt_runnable=%lu pelt_util=%lu\n",
		   s->load_weight, s->pelt_load, s->pelt_runnable, s->pelt_util);
#ifdef CONFIG_SCHED_WALT
	seq_printf(m, " walt_runnable_demand_scaled=%llu walt_predicted_demand_scaled=%llu rtg_high_prio=%u big_tasks=%d\n",
		   s->walt_demand, s->walt_predicted, s->rtg_high_prio, s->big_tasks);
#endif
	seq_printf(m, " fair_current_valid=%u", s->fair_valid);
	if (s->fair_valid)
		seq_printf(m, " fair_current_pid=%d on_rq=%u delayed=%u eligible=%u vruntime=%llu deadline=%llu slice=%llu weight=%llu D=%lld",
			   s->fair_pid, s->on_rq, s->entity_delayed, s->eligible,
			   s->vruntime, s->deadline, s->slice, s->weight, s->D);
	seq_putc(m, '\n');
	seq_printf(m, " protected_valid=%u", s->protected_valid);
	if (s->protected_valid)
		seq_printf(m, " protected_pid=%d vprot=%llu Q=%lld protection_active=%u",
			   s->protected_pid, s->vprot, s->Q, s->protection_active);
	seq_putc(m, '\n');
	seq_printf(m, " timeline_valid=%u", s->timeline_valid);
	if (s->timeline_valid)
		seq_printf(m, " min_deadline=%llu min_slice=%llu max_slice=%llu",
			   s->min_deadline, s->min_slice, s->max_slice);
	seq_putc(m, '\n');
	seq_printf(m, " feature_mask=%#x RUN_TO_PARITY=%u PREEMPT_SHORT=%u DELAY_DEQUEUE=%u DELAY_ZERO=%u\n\n",
		   s->features,
		   !!(s->features & (1U << __SCHED_FEAT_RUN_TO_PARITY)),
		   !!(s->features & (1U << __SCHED_FEAT_PREEMPT_SHORT)),
		   !!(s->features & (1U << __SCHED_FEAT_DELAY_DEQUEUE)),
		   !!(s->features & (1U << __SCHED_FEAT_DELAY_ZERO)));
}

static int weary_status_show(struct seq_file *m, void *unused)
{
	int cpu;

	seq_puts(m, "format=1 accounting=last_accounted cpu_snapshots=independent features=configured_mask\n");
	cpus_read_lock();
	for_each_possible_cpu(cpu) {
		struct weary_eevdf_snapshot s = { };
		struct rq *rq = cpu_rq(cpu);
		unsigned long flags;

		if (!cpu_online(cpu)) {
			seq_printf(m, "cpu=%d online=0\n\n", cpu);
			continue;
		}
		raw_spin_lock_irqsave(&rq->lock, flags);
		weary_eevdf_snapshot(rq, &s);
		raw_spin_unlock_irqrestore(&rq->lock, flags);
		weary_status_print(m, cpu, &s);
	}
	cpus_read_unlock();
	return 0;
}

/* Scheduler hooks never run in NMI; IRQ exclusion freezes local increments. */
static void weary_counter_copy(void *data)
{
	struct weary_eevdf_counters *s = data;
	const struct weary_eevdf_counters *c = this_cpu_ptr(&weary_eevdf_counters);
	int i;

	for (i = 0; i < WEARY_EEVDF_EVENTS; i++)
		s->event[i] = READ_ONCE(c->event[i]);
}

static int weary_counters_show(struct seq_file *m, void *unused)
{
	int cpu, i, ret = 0;

	seq_puts(m, "format=1 attribution=execution_cpu cpu_snapshots=independent reset=boot\n");
	cpus_read_lock();
	for_each_possible_cpu(cpu) {
		struct weary_eevdf_counters s;
		bool online = cpu_online(cpu);

		if (online) {
			ret = smp_call_function_single(cpu, weary_counter_copy, &s, 1);
			if (ret)
				break;
		} else {
			/* CPU hotplug is excluded; offline CPUs cannot execute hooks. */
			s = per_cpu(weary_eevdf_counters, cpu);
		}
		seq_printf(m, "cpu=%d online=%u", cpu, online);
		for (i = 0; i < WEARY_EEVDF_EVENTS; i++)
			seq_printf(m, " %s=%llu", weary_event_names[i], s.event[i]);
		seq_putc(m, '\n');
	}
	cpus_read_unlock();
	return ret;
}

static int weary_status_open(struct inode *inode, struct file *file)
{
	/* Avoid repeated lock sampling when seq_read grows its initial buffer. */
	return single_open_size(file, weary_status_show, NULL,
				256 + (size_t)nr_cpu_ids * 2048);
}

static int weary_counters_open(struct inode *inode, struct file *file)
{
	return single_open_size(file, weary_counters_show, NULL,
				256 + (size_t)nr_cpu_ids * 1024);
}

static const struct file_operations weary_status_fops = {
	.open = weary_status_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations weary_counters_fops = {
	.open = weary_counters_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init weary_eevdf_debug_init(void)
{
	struct dentry *root, *dir, *file;
	int ret;

	root = debugfs_create_dir("wearystars", NULL);
	if (IS_ERR_OR_NULL(root))
		return root ? PTR_ERR(root) : -ENOMEM;
	dir = debugfs_create_dir("eevdf", root);
	if (IS_ERR_OR_NULL(dir)) {
		ret = dir ? PTR_ERR(dir) : -ENOMEM;
		goto fail;
	}
	file = debugfs_create_file("status", 0400, dir, NULL, &weary_status_fops);
	if (IS_ERR_OR_NULL(file)) {
		ret = file ? PTR_ERR(file) : -ENOMEM;
		goto fail;
	}
	file = debugfs_create_file("counters", 0400, dir, NULL, &weary_counters_fops);
	if (IS_ERR_OR_NULL(file)) {
		ret = file ? PTR_ERR(file) : -ENOMEM;
		goto fail;
	}
	return 0;
fail:
	debugfs_remove_recursive(root);
	return ret;
}
late_initcall(weary_eevdf_debug_init);
