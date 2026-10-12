/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_power_host.h — feeds the power ledger (kernel/src/power) from the host
 * OS when ZXV runs hosted, so energy is tracked per app and per agent on
 * macOS, Linux and Windows desktops and laptops.
 *
 * What each host can measure, and what it cannot (nothing is invented):
 *
 *   Linux   RAPL zones in /sys/class/powercap (package, DRAM, psys) as
 *           meters; reading energy_uj needs root on kernels since 5.10, and
 *           without it the zones are skipped and the reason is kept in
 *           note. Batteries in /sys/class/power_supply (energy_* or charge_*
 *           x voltage, power_now or current_now x voltage, cycle_count);
 *           while discharging, the battery drain is a WHOLE meter. Per-app
 *           CPU time from /proc/<pid>/stat; energy is attributed by it.
 *   macOS   Per-process energy from proc_pid_rusage (ri_energy_nj, macOS 12
 *           and later) is METERED per app, for the processes this user may
 *           read. The battery comes from IOKit (AppleSmartBattery: voltage,
 *           amperage, capacities, cycle count, temperature); its drain is a
 *           WHOLE meter while unplugged. A Mac on mains has no public
 *           whole-device meter, so the device total is then a lower bound
 *           (the sum of metered apps) and is marked as one.
 *   Windows Battery from CallNtPowerInformation(SystemBatteryState); its
 *           drain is a WHOLE meter while unplugged. Per-app CPU time from
 *           GetProcessTimes. There is no public per-process energy API and
 *           no whole-device meter on mains, so a desktop PC on mains reports
 *           energy as "none" and shows CPU time only.
 *
 * Apps are grouped by executable name (helpers of one app add up). When the
 * ledger is full, further apps' activity is left to the system consumer.
 * Agents are registered by the process that runs them (zxvd) under its own
 * app with zph_note_agent, using thread CPU time around each model call.
 *
 * The Linux readers take a root directory so tests can run them against a
 * fixture tree on any POSIX host.
 */
#ifndef ZXV_POWER_HOST_H
#define ZXV_POWER_HOST_H

#include <stdint.h>
#include <stdbool.h>
#include "power_ledger.h"

#define ZPH_MAX_PROCS 4096
#define ZPH_MAX_RAPL  12
#define ZPH_PATH      512

typedef struct {
    int64_t pid;
    uint64_t start;  /* start time token, to catch pid reuse */
    uint64_t cpu_ns; /* cumulative                            */
    uint64_t nj;     /* cumulative energy, when metered       */
    bool has_nj;
    char name[PW_NAME_LEN];
} zph_proc_t;

typedef struct {
    pw_ledger_t *l;
    char root[ZPH_PATH]; /* "" = the live system */
    bool linux_paths;    /* use the /sys and /proc readers */
    uint32_t nrapl;
    struct {
        char energy[ZPH_PATH];
        int meter;
    } rapl[ZPH_MAX_RAPL];
    int batt_meter;
    bool batt_have;
    uint64_t batt_acc_uj;
    uint64_t batt_last_ms;
    int64_t batt_last_uw;
    zph_proc_t prev[ZPH_MAX_PROCS], cur[ZPH_MAX_PROCS];
    uint32_t nprev, ncur;
    bool procs_primed;
    uint32_t apps_dropped; /* apps left to the system: ledger full */
    char self_name[PW_NAME_LEN];
    char note[256];
} zxv_power_host_t;

/* Sets up the ledger for this host. root NULL or "" reads the live system;
 * a directory reads a Linux-style fixture under it (tests). */
int zph_open(zxv_power_host_t *h, pw_ledger_t *l, const char *root, uint64_t now_ms);

/* Reads battery, meters and processes into the ledger's open interval. */
int zph_sample(zxv_power_host_t *h, uint64_t now_ms);

/* Finds or adds the app consumer for an executable name. */
int zph_app(zxv_power_host_t *h, const char *name);

/* Agents run inside this process: their activity is noted under this
 * process's own app. agent_id is the caller's stable id for the agent. */
int zph_note_agent(zxv_power_host_t *h, uint64_t agent_id, const char *name, uint64_t cpu_ns,
                   uint64_t gpu_ns);

/* Monotonic milliseconds. */
uint64_t zph_now_ms(void);

/* FNV-1a over a name, the ext_id of an app. */
uint64_t zph_name_id(const char *name);

#endif /* ZXV_POWER_HOST_H */
