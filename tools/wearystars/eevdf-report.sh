#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# One-shot, read-only report. Never mount debugfs or change scheduler knobs.
set -eu

if [ "$#" -gt 1 ] || [ "${1-}" = "--help" ]; then
	printf '%s\n' "Usage: sh $0 [monitor-directory]"
	exit 0
fi

monitor_dir=${1:-/sys/kernel/debug/wearystars/eevdf}
for monitor_file in status counters; do
	if [ ! -r "$monitor_dir/$monitor_file" ]; then
		printf '%s\n' "Cannot read $monitor_dir/$monitor_file" >&2
		printf '%s\n' \
			'Requires CONFIG_WEARYSTARS_EEVDF_DEBUG=y and root access to debugfs.' >&2
		exit 1
	fi
done

# Check both reads before emitting a report; avoid presenting partial success.
monitor_status=$(cat "$monitor_dir/status")
monitor_counters=$(cat "$monitor_dir/counters")

printf '%s\n' 'WearyStars EEVDF Monitor'
date -u '+%Y-%m-%d %H:%M:%S UTC'
printf '\n%s\n' 'Status: per-CPU snapshots of last-accounted state'
printf '%s\n' "$monitor_status" | awk '
/^cpu=/ { print "" }
{ print }
'
printf '\n%s\n' 'Counters: since boot, attributed to the executing CPU'
printf '%s\n' "$monitor_counters" | awk '
/^format=/ { print; next }
/^cpu=/ {
	print ""
	for (i = 1; i <= NF; i++) {
		pos = index($i, "=")
		key = substr($i, 1, pos - 1)
		value = substr($i, pos + 1)
		printf "  %-28s %s\n", key, value
	}
}
'
printf '\n%s\n' 'D/Q and virtual timestamps are printed without numeric conversion.'
printf '%s\n' 'Status and counters are separate samples; no global simultaneous snapshot.'
