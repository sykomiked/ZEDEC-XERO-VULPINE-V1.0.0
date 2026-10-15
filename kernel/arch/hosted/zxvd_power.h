/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_power.h — power.*: energy per app and per agent on this machine, the
 * battery and its posture, and energy budgets, read and set over zxvd's
 * socket. The ledger is kernel/src/power (power_ledger.h P1-P6); the host
 * readers are zxv_power_host.h; docs/POWER.md says what each OS can measure.
 *
 *   W1  The group (zxvd_power.c, zxvd_groups.h) has the feature "power" and
 *       is not regulated: an energy cap is the owner's own ecological limit,
 *       not a payment. power.status, power.top and thermal.read are
 *       `always` (zxvd_ext.h X4); power.budget, the setter, answers only
 *       with the switch on.
 *   W2  zxvd_power_open reads this machine once (zxv_power_host.h zph_open)
 *       and registers zxvd itself as an app; zxvd_power_tick samples the
 *       host and settles the ledger at most once a second. zxvd is one
 *       thread, so the ledger needs no lock; a caller that adds threads
 *       must hold one lock around every zxvd_power_* call.
 *   W3  Agents: around each model call the host notes the process CPU time
 *       the call took (zxvd_power_cpu_ns before and after; the engine's
 *       worker threads are counted with the caller) with
 *       zxvd_power_note_agent. The agent is a consumer under zxvd's own app,
 *       keyed by the agent's stable id.
 *   W4  Admission: before an agent's model call the host asks
 *       zxvd_power_admit. false (power_ledger.h WAIT or HOLD: a budget
 *       window is spent, or a strict budget has nothing measured) means the
 *       agent skips this call. Nothing buys past a budget. An agent never
 *       noted yet is judged by zxvd's own and the device's budgets; with
 *       power not open every agent is admitted.
 *   W5  zxvd_power_token_scale: the battery posture's per-mille scale of
 *       the swarm's tokens per cycle (1000 on mains or without a battery,
 *       618 on battery, 382 at 20% or below, 0 at 5% or below).
 *   W6  power.status: the whole ledger as integer JSON (pw_render_json).
 *       power.top {kind: "app"|"agent"|"all" = "all", lifetime = false,
 *       n: 1..64 = 10}: the consumers by energy, highest first, each
 *       {id, kind, name, parent, uj, mw, source, lifetime_uj, cpu_ns};
 *       parent is null for none, uj and mw are the last settled interval.
 *   W7  power.budget {target: "device"|"app"|"agent", app: exe name (target
 *       "app"), agent: id (target "agent"), window_ms (0 clears), max_uj,
 *       strict = false}: sets the target's budget (pw_budget_set) and
 *       answers {ok: true}. An app not seen yet is added; an agent must have
 *       been noted.
 *   W8  Errors: ZXVD_E_POWER (-32095) when power is not open or the target
 *       is unknown; bad parameters are ZXVD_E_PARAMS.
 *   W9  thermal.read {}: this machine's temperature sensors (CPU, GPU,
 *       case/board, other) as integer JSON, read afresh on each call by
 *       zxv_thermal_host.h (T1-T7): {state, host, reason, classes, readings,
 *       rejected, dropped}, millidegrees C, each reading with its source,
 *       driver, label and path. `always`, and it answers whether or not the
 *       ledger is open. A host with no readable sensor answers state
 *       "neither" with the reason, never a number; a refused value is a
 *       reading with state "false", its reason and mdeg null. The macOS
 *       readers are not compiled or tested in the Linux CI containers.
 * Integers only.
 */
#ifndef ZXVD_POWER_H
#define ZXVD_POWER_H

#include <stdbool.h>
#include <stdint.h>

#define ZXVD_E_POWER (-32095) /* W8; in -32099..-32001 (zxvd_rpc.h X4) */

/* W2: root NULL or "" reads this machine; a directory reads a Linux-style
 * fixture under it (tests). 0, or -1 when the host could not be read. */
int zxvd_power_open(const char *root);
void zxvd_power_close(void);
bool zxvd_power_is_open(void);
/* W2: sample and settle when a second has passed since the last time; force
 * does it now (tests). */
void zxvd_power_tick(bool force);
/* W3 */
uint64_t zxvd_power_cpu_ns(void);
void zxvd_power_note_agent(uint64_t agent_id, const char *name, uint64_t cpu_ns, uint64_t gpu_ns);
/* W4 */
bool zxvd_power_admit(uint64_t agent_id);
/* W5 */
uint32_t zxvd_power_token_scale(void);
/* Unit tests only: where thermal.read looks (W9; NULL or "" = this
 * machine, a directory = a Linux-style fixture under it). */
void zxvd_power_set_thermal_root(const char *root);
/* Unit tests only: the monotonic clock in ms (NULL = zph_now_ms). */
void zxvd_power_set_clock(uint64_t (*fn)(void));

#endif
