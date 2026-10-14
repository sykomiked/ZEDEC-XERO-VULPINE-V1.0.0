/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* power_ledger.h — energy accounting per app and per agent, battery state and
 * energy budgets, for desktops, laptops, phones and bare metal.
 *
 * OWNER REQUEST (2026-10-12): "do you have a feature that keeps track of power
 * usage, both for desktops and laptops and mobile devices in the OS? Because
 * that's a critical feature."
 *
 * The ledger is pure integer C with no allocation and no libc. A platform
 * layer feeds it; it never reads hardware itself:
 *   - hosted Linux, macOS and Windows: kernel/arch/hosted/zxv_power_host.c
 *   - bare-metal x86: power_rapl.c reads the RAPL energy MSRs
 *   - phones: the companion core passes in what iOS or Android report
 *
 *   P1 METERS. A meter is a cumulative energy counter in microjoules, with an
 *      optional wrap (RAPL counters are 32 bits). Each meter has a scope:
 *        WHOLE  the whole device: a platform meter (RAPL psys) or the
 *               battery drain while unplugged.
 *        PART   a non-overlapping part: CPU package, DRAM, GPU. Parts add up.
 *        SUB    inside a part (CPU cores inside the package). Shown, never
 *               added, so nothing is counted twice.
 *      A sample that runs backwards without a wrap, or that implies more
 *      power than the meter's declared ceiling, is a FAULT: its energy is
 *      dropped, the fault is counted and the reading becomes the new base.
 *      A reading at or past the wrap is no counter value at all: it is a
 *      FAULT and the old base stays.
 *   P2 INTERVAL TOTAL. pw_settle closes an interval. The device total is the
 *      largest WHOLE meter when there is one, else the sum of the PARTs. Two
 *      WHOLE meters that disagree by more than the tolerance, or PARTs that
 *      add up to more than the WHOLE, make the interval a CONFLICT (lpres
 *      BOTH): both readings are kept and the larger one is used, so budgets
 *      err on the side of nature. No meter at all is NONE (lpres NEITHER):
 *      the ledger reports no energy rather than an invented figure.
 *   P3 ATTRIBUTION. A consumer is an app, an agent or the system. Consumers
 *      report activity per interval (CPU and GPU busy time in ns). A consumer
 *      the platform meters directly (macOS per-process energy) reports its
 *      energy instead; that figure is METERED. The rest of the total is
 *      ATTRIBUTED: the GPU pool by GPU time and the remainder by CPU time,
 *      each share = pool * activity / capacity, capacity = interval * units.
 *      Idle and shared energy (screen, radios, the rounding) stays with the
 *      system consumer. Agents name their app as parent: an app's energy is
 *      split among its agents by their share of its activity, and the rest
 *      stays with the app. Every interval conserves exactly: the system's
 *      share is the total minus everything else, so the figures add up to
 *      the measured total to the microjoule.
 *   P4 BATTERY. pw_battery_t holds what the platform reports. The ledger
 *      derives health (full / design), time to empty and time to full, and a
 *      posture: PERFORM on mains, BALANCED, CONSERVE at 20% or below and
 *      CRITICAL at 5% or below. Each posture scales the swarm's tokens per
 *      cycle by a Fibonacci ratio (1, 0.618, 0.382, 0) in per-mille.
 *   P5 BUDGETS. An energy budget per consumer and one for the whole device,
 *      per rolling window. pw_admit answers OK, WAIT (the window is spent;
 *      the work waits for the next one) or HOLD (a strict budget and no
 *      measured energy to judge by). Nothing buys past a budget: an ecological
 *      limit refuses, it does not price.
 *   P6 REPORT. pw_top ranks consumers by energy; pw_render_json writes the
 *      whole ledger as integer JSON (no floats) for zxvd and the Brazier app;
 *      ext_id is a decimal string, since 64-bit ids exceed 2^53.
 */
#ifndef POWER_LEDGER_H
#define POWER_LEDGER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PW_MAX_CONSUMERS 512
#define PW_MAX_METERS    16
#define PW_NAME_LEN      32
#define PW_NONE          0xFFFFu /* no consumer / no parent */
#define PW_SYSTEM        0u      /* consumer 0 is always the system */

typedef enum {
    PW_DEV_DESKTOP = 0,
    PW_DEV_LAPTOP = 1,
    PW_DEV_MOBILE = 2,
    PW_DEV_SERVER = 3,
} pw_device_t;

typedef enum {
    PW_HOST_BAREMETAL = 0,
    PW_HOST_LINUX = 1,
    PW_HOST_MACOS = 2,
    PW_HOST_WINDOWS = 3,
    PW_HOST_IOS = 4,
    PW_HOST_ANDROID = 5,
} pw_host_t;

typedef enum {
    PW_KIND_SYSTEM = 0,
    PW_KIND_APP = 1,
    PW_KIND_AGENT = 2,
} pw_kind_t;

typedef enum {
    PW_SCOPE_WHOLE = 0,
    PW_SCOPE_PART = 1,
    PW_SCOPE_SUB = 2,
} pw_scope_t;

typedef enum {
    PW_DOM_SYSTEM = 0,  /* platform / psys meter */
    PW_DOM_BATTERY = 1, /* battery drain         */
    PW_DOM_PACKAGE = 2, /* CPU package           */
    PW_DOM_CORES = 3,   /* CPU cores (PP0)       */
    PW_DOM_GPU = 4,     /* GPU (PP1 or discrete) */
    PW_DOM_DRAM = 5,
    PW_DOM_OTHER = 6,
} pw_domain_t;

/* Where a figure came from. pw_source_truth maps it to the lpres states. */
typedef enum {
    PW_SRC_NONE = 0,       /* nothing measured           -> NEITHER */
    PW_SRC_METERED = 1,    /* read off a meter            -> TRUE    */
    PW_SRC_ATTRIBUTED = 2, /* measured total, split by use -> TRUE   */
    PW_SRC_CONFLICT = 3,   /* meters disagree             -> BOTH    */
    PW_SRC_FAULT = 4,      /* the reading was rejected    -> FALSE   */
} pw_source_t;

typedef enum {
    PW_POSTURE_PERFORM = 0,
    PW_POSTURE_BALANCED = 1,
    PW_POSTURE_CONSERVE = 2,
    PW_POSTURE_CRITICAL = 3,
} pw_posture_t;

typedef enum {
    PW_ADMIT_OK = 0,
    PW_ADMIT_WAIT = 1, /* the budget window is spent             */
    PW_ADMIT_HOLD = 2, /* strict budget, nothing measured to judge */
    PW_ADMIT_BAD = 3,  /* unknown consumer                         */
} pw_admit_t;

typedef enum {
    PW_OK = 0,
    PW_E_ARG = -1,
    PW_E_FULL = -2,
    PW_E_FAULT = -3,
    PW_E_NOSPC = -4,
    PW_E_ORDER = -5,
} pw_err_t;

typedef struct {
    uint64_t window_ms; /* 0 = no budget                          */
    uint64_t max_uj;    /* energy allowed per window              */
    uint64_t start_ms;  /* the current window began here          */
    uint64_t used_uj;   /* spent in the current window            */
    bool strict;        /* HOLD when nothing measured backs it    */
} pw_budget_t;

typedef struct {
    bool present;
    bool on_mains;
    bool charging;
    uint16_t permille;   /* charge 0..1000                          */
    uint64_t now_mwh;    /* energy remaining                        */
    uint64_t full_mwh;   /* full charge capacity today              */
    uint64_t design_mwh; /* capacity when new                       */
    int64_t rate_mw;     /* > 0 discharging, < 0 charging, 0 idle   */
    uint32_t cycles;
    int32_t temp_dc; /* tenths of a degree C; INT32_MIN unknown */
    uint64_t at_ms;  /* when it was read                        */
} pw_battery_t;

typedef struct {
    char name[PW_NAME_LEN];
    pw_domain_t domain;
    pw_scope_t scope;
    uint64_t wrap_uj; /* 0 = never wraps                           */
    uint64_t max_mw;  /* plausibility ceiling, 0 = none            */
    bool have_last;
    uint64_t last_uj; /* last raw counter                          */
    uint64_t last_ms;
    uint64_t pending_uj; /* energy since the last settle              */
    uint64_t total_uj;   /* lifetime                                   */
    uint32_t faults;
    bool fresh; /* produced a delta in this interval         */
} pw_meter_t;

typedef struct {
    bool used;
    char name[PW_NAME_LEN];
    pw_kind_t kind;
    uint16_t parent; /* PW_NONE or an app's id                    */
    uint64_t ext_id; /* pid, agent id: the platform's handle      */
    /* this interval */
    uint64_t cpu_ns;
    uint64_t gpu_ns;
    uint64_t direct_uj; /* metered directly by the platform          */
    bool has_direct;
    /* after settle */
    uint64_t last_uj;     /* energy in the last settled interval       */
    uint64_t last_mw;     /* its average power                          */
    uint64_t last_cpu_ns; /* activity in the last settled interval     */
    uint64_t last_gpu_ns;
    pw_source_t last_src;
    uint64_t total_uj; /* lifetime                                   */
    uint64_t total_cpu_ns;
    uint64_t total_gpu_ns;
    pw_budget_t budget;
} pw_consumer_t;

typedef struct {
    pw_device_t device;
    pw_host_t host;
    uint32_t cpu_units;    /* logical CPUs                             */
    uint32_t gpu_units;    /* GPUs (0 = none known)                    */
    uint32_t tol_permille; /* meter agreement tolerance              */
    pw_meter_t meters[PW_MAX_METERS];
    uint32_t nmeters;
    pw_consumer_t consumers[PW_MAX_CONSUMERS];
    pw_battery_t battery;
    pw_budget_t device_budget;
    /* interval */
    bool started;
    uint64_t interval_start_ms;
    uint64_t last_settle_ms;
    uint64_t last_total_uj;
    uint64_t last_total_mw;
    uint64_t last_other_uj; /* the second reading on a conflict      */
    pw_source_t last_src;
    bool last_partial; /* total is only the metered consumers  */
    uint64_t total_uj;
    uint64_t intervals;
    uint64_t conflicts;
} pw_ledger_t;

/* Lifecycle. tol_permille 0 picks 100 (10%). */
void pw_init(pw_ledger_t *l, pw_device_t device, pw_host_t host, uint32_t cpu_units,
             uint32_t gpu_units, uint64_t now_ms);

/* Meters (P1). Returns the meter index or a pw_err_t. */
int pw_meter_add(pw_ledger_t *l, const char *name, pw_domain_t domain, pw_scope_t scope,
                 uint64_t wrap_uj, uint64_t max_mw);
int pw_meter_sample(pw_ledger_t *l, uint32_t meter, uint64_t counter_uj, uint64_t now_ms);

/* Consumers (P3). Returns the consumer id or a pw_err_t. An agent's parent
 * must be an app; an app or the system has no parent. */
int pw_consumer_add(pw_ledger_t *l, pw_kind_t kind, const char *name, uint16_t parent,
                    uint64_t ext_id);
int pw_consumer_find(const pw_ledger_t *l, pw_kind_t kind, uint64_t ext_id);
int pw_consumer_remove(pw_ledger_t *l, uint16_t id);
int pw_note_activity(pw_ledger_t *l, uint16_t id, uint64_t cpu_ns, uint64_t gpu_ns);
int pw_note_energy(pw_ledger_t *l, uint16_t id, uint64_t uj);

/* Close the interval [last settle, now_ms] (P2, P3, P5). */
int pw_settle(pw_ledger_t *l, uint64_t now_ms);

/* Battery (P4). */
void pw_battery_set(pw_ledger_t *l, const pw_battery_t *b);
uint32_t pw_battery_health_permille(const pw_battery_t *b);  /* 0 = unknown */
uint64_t pw_battery_minutes_to_empty(const pw_battery_t *b); /* 0 = n/a  */
uint64_t pw_battery_minutes_to_full(const pw_battery_t *b);  /* 0 = n/a  */
pw_posture_t pw_posture(const pw_ledger_t *l);
uint32_t pw_posture_token_scale(pw_posture_t p); /* per-mille */

/* Budgets (P5). window_ms 0 clears. id PW_NONE sets the device budget. */
int pw_budget_set(pw_ledger_t *l, uint16_t id, uint64_t window_ms, uint64_t max_uj, bool strict,
                  uint64_t now_ms);
pw_admit_t pw_admit(const pw_ledger_t *l, uint16_t id, uint64_t want_uj, uint64_t now_ms);

/* Reports (P6). pw_top fills ids by energy, highest first (ties: lower id).
 * kind < 0 means every kind. lifetime picks total_uj over last_uj. */
uint32_t pw_top(const pw_ledger_t *l, int kind, bool lifetime, uint16_t *ids, uint32_t max);
int pw_source_truth(pw_source_t s); /* an lpres_state_t value */
const char *pw_source_name(pw_source_t s);
const char *pw_posture_name(pw_posture_t p);
const char *pw_kind_name(pw_kind_t k);
const char *pw_device_name(pw_device_t d);
const char *pw_host_name(pw_host_t h);
/* Writes NUL-terminated JSON. Returns its length or PW_E_NOSPC. */
int pw_render_json(const pw_ledger_t *l, char *out, size_t cap);

/* floor(a * b / c) without overflow; c == 0 gives 0. Exposed for tests. */
uint64_t pw_muldiv(uint64_t a, uint64_t b, uint64_t c);

#endif /* POWER_LEDGER_H */
