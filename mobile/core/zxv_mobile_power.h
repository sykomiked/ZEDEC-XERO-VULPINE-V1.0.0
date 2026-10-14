/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_mobile_power.h — the phone side of power tracking: the Kotlin or Swift
 * shell passes in what the phone OS reports, and the shared power ledger
 * (kernel/src/power) attributes it to the companion app and its agents.
 *
 * What a phone lets an app see:
 *   Android  BatteryManager: level, status, plug type, CURRENT_NOW (uA) and
 *            the voltage from ACTION_BATTERY_CHANGED (mV), cycle count on
 *            Android 14+. With current and voltage the battery drain is a
 *            measured whole-device meter while unplugged. Pass current as
 *            positive while discharging (vendors differ in sign; the shell
 *            normalises it using the charging status).
 *   iOS      UIDevice: level and state only. There is no current or energy
 *            API, so on an iPhone energy is reported as "none" and only the
 *            battery level, charging state and CPU time are shown.
 * Neither OS exposes other apps' energy, so on a phone the ledger tracks the
 * companion app and its agents; everything else is the system's share. */
#ifndef ZXV_MOBILE_POWER_H
#define ZXV_MOBILE_POWER_H

#include <stddef.h>
#include <stdint.h>

#define ZXV_POWER_ANDROID 5
#define ZXV_POWER_IOS     4

/* host: ZXV_POWER_ANDROID or ZXV_POWER_IOS. Returns 0 or a negative error. */
int zxv_power_start(int host, uint32_t cpus, uint64_t now_ms);

/* Battery as the phone reports it. Unknown numbers are -1. */
int zxv_power_battery(int level_permille, int charging, int plugged, int64_t current_ua,
                      int64_t voltage_mv, int64_t full_mwh, int64_t design_mwh, int cycles,
                      int temp_dc, uint64_t now_ms);

/* CPU time this app used since the last call (Process.getElapsedCpuTime
 * deltas on Android, task_info on iOS). */
int zxv_power_app_cpu(uint64_t cpu_ns);

/* An agent's CPU and GPU time since the last call. */
int zxv_power_agent(uint64_t agent_id, const char *name, uint64_t cpu_ns, uint64_t gpu_ns);

/* Closes the interval. */
int zxv_power_settle(uint64_t now_ms);

/* The ledger as JSON (power_ledger.h P6). Returns the length or < 0. */
int zxv_power_json(char *out, size_t cap);

/* Per-mille scale for the swarm's tokens per cycle (1000, 618, 382, 0). */
uint32_t zxv_power_token_scale(void);

#endif /* ZXV_MOBILE_POWER_H */
