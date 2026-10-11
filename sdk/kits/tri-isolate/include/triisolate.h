/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* triisolate.h — Tri-Isolate: run a Z-Wasm plugin in a child process (kit
 * API 0.1).
 *
 * Each plugin runs in its own worker process (fork + exec of the ti-worker
 * program). Inside the worker the plugin runs under Z-Wasm exactly as it
 * would in-process: the same validator, fuel meter and host functions. The
 * host process talks to the worker over a connected Unix socket pair using
 * length-prefixed frames (see README.md, "Wire format"), and:
 *
 *   - validates the module itself before any worker sees it;
 *   - gives each call its fuel from the host's own rmag budget and charges
 *     what was used (or the whole allotment if the worker misbehaves);
 *   - kills the worker (SIGKILL) when a call runs out of fuel, traps, goes
 *     past its wall-clock timeout, or breaks the protocol;
 *   - keeps the S0 proposal queue on the host side: a proposal is only a
 *     value, and the host accepts or rejects it with its OWN capability,
 *     exactly as with Z-Wasm.
 *
 * WHAT THIS IS. A process boundary, not a virtual machine. The worker is
 * an ordinary process of the same user: a separate address space, empty
 * environment, no inherited file descriptors but the socket, a core-dump
 * limit of zero and optional address-space limit. On Linux (x86-64 and
 * arm64) an optional seccomp filter (TI_F_SECCOMP) narrows the system
 * calls the worker can make after start-up. Without it, code that escapes
 * the Wasm sandbox inside the worker can do anything the user can. See
 * README.md, "What isolation this gives".
 *
 * PLATFORMS. Linux and macOS. Windows is not supported: every function
 * returns ZXV_E_NOTIMPL there.
 *
 * THREADS. A host and its plugins are used from one thread at a time (the
 * same rule as Z-Wasm). Different hosts are independent.
 *
 * KIT API STABILITY. 0.x: it may change before 1.0. It calls libzxv ABI
 * 1.3 and Z-Wasm kit API 0.2. Opaque handles, int32_t status returns, no
 * floats. */
#ifndef TRIISOLATE_H
#define TRIISOLATE_H

#include <stddef.h>
#include <stdint.h>

#include "zwasm.h"
#include "zxv_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TI_VERSION_MAJOR 0
#define TI_VERSION_MINOR 1
#define TI_VERSION_PATCH 0

/* Status codes: libzxv's 0 / -1..-99 and Z-Wasm's -1001.. pass through
 * (ZW_E_REJECTED, ZW_E_LINK, ZW_E_TRAP, ZW_E_FUEL, ZW_E_EXPORT);
 * Tri-Isolate owns -1501... */
#define TI_E_SPAWN    (-1501) /* the worker could not be started               */
#define TI_E_PROTOCOL (-1502) /* the worker broke the protocol; it was killed  */
#define TI_E_TIMEOUT  (-1503) /* past the wall-clock timeout; it was killed    */
#define TI_E_CRASHED  (-1504) /* the worker exited or died unexpectedly        */
#define TI_E_DEAD     (-1505) /* the plugin stopped earlier; destroy it        */

const char *ti_status_name(int32_t status);
/* The calling thread's last error message (R4 two-call buffer). */
int32_t ti_last_error(char *buf, size_t cap, size_t *out_len);

/* ===== Hosts ===== */
typedef struct ti_host ti_host;
typedef struct ti_plugin ti_plugin;

/* Create a host. `budget_cap` must hold ZXV_CAP_BUDGET; `budget` is a
 * budget id in `rmag` that pays for every call's fuel (as Z-Wasm). The
 * host keeps both pointers: they must outlive it. `worker_path` is the
 * ti-worker program (copied, < 1024 bytes); it is run with an empty
 * environment, so give a path, not a name to look up in PATH. The host
 * starts with no grants, Z-Wasm's default limits, call fuel 10^7, a
 * 5000 ms call timeout, a 10000 ms load timeout and accept caps
 * ZXV_CAP_LEDGER_POST. */
int32_t ti_host_create(const zxv_cap *budget_cap, zxv_rmag *rmag, uint32_t budget,
                       const char *worker_path, ti_host **out_host);
/* Every plugin of the host must be destroyed first (a documented rule,
 * like zw_host_destroy). NULL is ignored. */
void ti_host_destroy(ti_host *host);
/* Z-Wasm grant groups (ZW_GRANT_*). Only affects plugins loaded later. */
int32_t ti_host_grant(ti_host *host, uint32_t grants);
/* A Z-Wasm policy limit (ZW_LIM_*): used by the host's own validation and
 * sent to each worker for its Z-Wasm host. */
int32_t ti_host_set_limit(ti_host *host, int32_t which, uint64_t value);
/* Most fuel one call may use (1 .. 10^15). */
int32_t ti_host_set_call_fuel(ti_host *host, uint64_t max_fuel);
/* Wall-clock timeouts in milliseconds (1 .. 3600000): one call, and
 * starting a worker plus loading the plugin. */
int32_t ti_host_set_timeouts(ti_host *host, uint32_t call_ms, uint32_t load_ms);
/* Capability bits needed to accept or reject a proposal (as Z-Wasm). */
int32_t ti_host_set_accept_caps(ti_host *host, uint64_t needed);
/* Worker address-space limit in bytes (RLIMIT_AS); 0 = none (default).
 * Below 1 MiB is ZXV_E_RANGE. Linux only (macOS does not enforce
 * RLIMIT_AS): ZXV_E_NOTIMPL elsewhere. Sanitizer builds of the worker
 * reserve a lot of address space and do not run under a small limit. */
int32_t ti_host_set_memory_limit(ti_host *host, uint64_t bytes);

/* Host flags (ti_host_set_flags). */
#define TI_F_SECCOMP         0x1u /* Linux only: the worker installs a seccomp   */
                                  /* allow-list before it reads any plugin byte; */
                                  /* ZXV_E_NOTIMPL where ti_seccomp_available()  */
                                  /* is 0                                        */
#define TI_F_CHARGE_ALLOTTED 0x2u /* charge each call's whole allotment, not the */
                                  /* fuel the worker reports                     */
#define TI_F_INHERIT_STDERR  0x4u /* the worker keeps the host's stderr (for     */
                                  /* debugging); default /dev/null               */
int32_t ti_host_set_flags(ti_host *host, uint32_t flags);
/* 1 if this build can install the seccomp filter (Linux x86-64 / arm64). */
int32_t ti_seccomp_available(void);

/* Counters since the host was created. Any out pointer may be NULL. */
int32_t ti_host_stats(const ti_host *host, uint64_t *out_spawned, uint64_t *out_killed,
                      uint64_t *out_calls);

/* ===== Plugins ===== */
#define TI_PLUGIN_READY 0
#define TI_PLUGIN_DEAD  1 /* stopped (fuel, trap, timeout, crash, protocol)  */

/* Validate the module with the host's policy and grants (ZW_E_REJECTED and
 * no worker if it fails), start a worker, and load the plugin in it. The
 * worker's own Z-Wasm result passes through (ZW_E_REJECTED, ZW_E_LINK);
 * TI_E_SPAWN, TI_E_TIMEOUT, TI_E_CRASHED or TI_E_PROTOCOL if the worker
 * fails. `name` (<= 31 bytes, may be NULL) labels its proposals. */
int32_t ti_plugin_load(ti_host *host, const uint8_t *wasm, size_t len, const char *name,
                       ti_plugin **out_plugin);
/* Ask the worker to quit, wait briefly, then SIGKILL it; reap it; free.
 * NULL is ignored. */
void ti_plugin_destroy(ti_plugin *plugin);
/* Stop the worker as destroy does, but keep the handle so its exit can be
 * read. The plugin is DEAD afterwards. */
int32_t ti_plugin_stop(ti_plugin *plugin);
int32_t ti_plugin_state(const ti_plugin *plugin, int32_t *out_state);
/* The worker's process id (0 once it is reaped). */
int32_t ti_plugin_pid(const ti_plugin *plugin, int64_t *out_pid);

/* How the worker ended (ti_plugin_exit). */
#define TI_EXIT_RUNNING 0 /* still running                                 */
#define TI_EXIT_CODE    1 /* exited; *out_value is its exit status          */
#define TI_EXIT_SIGNAL  2 /* killed; *out_value is the signal number        */
#define TI_EXIT_UNKNOWN 3 /* reaped by someone else (e.g. SIGCHLD ignored)  */
int32_t ti_plugin_exit(const ti_plugin *plugin, int32_t *out_how, int32_t *out_value);

/* Call an exported i32/i64 function, as zw_plugin_call: at most 8 int64
 * arguments, at most one result. Fuel = min(the call fuel, floor(the
 * budget's remaining)); none left gives ZXV_E_BUDGET and nothing is sent.
 *
 * Outcomes:
 *   ZXV_OK          the result, and the call's proposals are in the host's
 *                   queue (ZXV_E_CAPACITY instead, with the result written,
 *                   if they do not fit: then none of them is queued).
 *   ZW_E_EXPORT, ZXV_E_RANGE, ZXV_E_ARG
 *                   the worker refused the call; nothing ran; READY.
 *   ZW_E_FUEL, ZW_E_TRAP
 *                   the plugin stopped; the worker is killed; DEAD.
 *   TI_E_TIMEOUT, TI_E_CRASHED, TI_E_PROTOCOL
 *                   the worker is killed (if still alive); DEAD; the whole
 *                   allotment is charged.
 *   TI_E_DEAD       an earlier call stopped it.
 * *out_fuel_used is the fuel charged. Any out pointer may be NULL. */
int32_t ti_plugin_call(ti_plugin *plugin, const char *export_name, const int64_t *args,
                       uint32_t nargs, int64_t *out_result, int32_t *out_has_result,
                       uint64_t *out_fuel_used);

/* ===== The S0 proposal queue (host side; as Z-Wasm's) =====
 * States are ZXV_LP_NEITHER (pending), ZXV_LP_TRUE (accepted) and
 * ZXV_LP_FALSE (rejected). Ids start at 1. At most 1024 pending. */
int32_t ti_queue_pending(const ti_host *host, uint32_t *out_count);
int32_t ti_queue_next(const ti_host *host, uint64_t *out_id);
int32_t ti_proposal_get(const ti_host *host, uint64_t id, int32_t *out_state, uint32_t *out_kind,
                        int64_t *out_num, int64_t *out_den, char plugin_name[32], uint8_t *label,
                        size_t label_cap, size_t *out_label_len);
/* `cap` is the HOST's capability and must pass zxv_cap_check for the
 * accept caps (ZXV_E_CAP otherwise; the proposal stays pending). */
int32_t ti_proposal_accept(ti_host *host, const zxv_cap *cap, uint64_t id);
int32_t ti_proposal_reject(ti_host *host, const zxv_cap *cap, uint64_t id);
/* Accept and post (a positive integer value), as zw_proposal_accept_post. */
int32_t ti_proposal_accept_post(ti_host *host, const zxv_cap *cap, uint64_t id,
                                uint32_t from_account, uint32_t to_account, const char *ccy,
                                const char *reference, uint64_t *out_entry_id);

#ifdef __cplusplus
}
#endif

#endif /* TRIISOLATE_H */
