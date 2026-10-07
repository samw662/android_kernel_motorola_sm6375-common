/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _SCHED_EEVDF_MONITOR_H
#define _SCHED_EEVDF_MONITOR_H

/* Counters are attributed to the CPU executing the hook, not the target rq. */
enum weary_eevdf_event {
	WEARY_EEVDF_PICK,
	WEARY_EEVDF_RENEW,
	WEARY_EEVDF_PROTECTED,
	WEARY_EEVDF_SHORT,
	WEARY_EEVDF_DELAYED_ENTER,
	WEARY_EEVDF_DELAYED_ENQUEUE,
	WEARY_EEVDF_DELAYED_COMPLETE,
	WEARY_EEVDF_RETAINED_WAKE,
	WEARY_EEVDF_YIELD,
	WEARY_EEVDF_MIGRATE,
	WEARY_EEVDF_EVENTS,
};

#ifdef CONFIG_WEARYSTARS_EEVDF_DEBUG
#include <linux/percpu.h>

struct weary_eevdf_counters {
	u64 event[WEARY_EEVDF_EVENTS];
};

DECLARE_PER_CPU_SHARED_ALIGNED(struct weary_eevdf_counters, weary_eevdf_counters);

static inline void weary_eevdf_count(enum weary_eevdf_event event)
{
	this_cpu_inc(weary_eevdf_counters.event[event]);
}

/* Value-only copy; no task or rq pointers survive the locked collection. */
struct weary_eevdf_snapshot {
	unsigned int nr_running, queued, runnable, delayed;
	unsigned int features;
	pid_t current_pid, fair_pid, protected_pid;
	char comm[TASK_COMM_LEN];
	bool comm_valid, fair_valid, protected_valid;
	bool on_rq, entity_delayed, eligible, protection_active, timeline_valid;
	u64 clock, min_vruntime, zero_vruntime, V;
	s64 avg_vruntime;
	u64 avg_load, load_weight;
	unsigned long pelt_load, pelt_runnable, pelt_util;
	u64 vruntime, deadline, slice, weight;
	s64 D, Q;
	u64 vprot, min_deadline, min_slice, max_slice;
#ifdef CONFIG_SCHED_WALT
	u64 walt_demand, walt_predicted;
	unsigned int rtg_high_prio;
	int big_tasks;
#endif
};

struct rq;
void weary_eevdf_snapshot(struct rq *rq, struct weary_eevdf_snapshot *s);
#else
static inline void weary_eevdf_count(enum weary_eevdf_event event) { }
#endif

#endif
