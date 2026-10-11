/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_emu.h — emu-state: deterministic lockstep simulation over libzxv
 * (engine kit API 0.1, C11, no floating point anywhere).
 *
 * One zxv_emu holds a game's SIMULATION STATE as a fixed number of slots:
 *
 *   rat slots  exact rationals (int64 numerator over a positive int64
 *              denominator, always reduced; arithmetic through zxv_rat_*,
 *              overflow is ZXV_E_RANGE, never a wrap or a rounding)
 *   hex slots  64-state hexagram tags (zxv_hex_*, ABI 1.3), for game rules
 *              that need "true / false / both / neither" with polarity and
 *              a lifecycle phase
 *
 * and moves it forward one TICK at a time:
 *
 *   tick 0          the initial state, written during setup, then
 *                   zxv_emu_start
 *   tick t (t >= 1) the state after applying every player's input for
 *                   tick t to the state of tick t - 1
 *
 * Every committed tick is hashed (SHA3-256 of the canonical encoding below,
 * through libzxv's zxv_sha3_256) and kept in a ring of `history` snapshots,
 * so a host can compare hashes with its peers (desync detection) and roll
 * back to any kept tick (rollback netcode).
 *
 * INPUTS. Each player submits a fixed-size byte blob per tick. In strict
 * lockstep (flags 0) a tick cannot begin until every player's input for it
 * has arrived. With ZXV_EMU_F_PREDICT a missing input is PREDICTED (the
 * player's previous input, or zeros at tick 1); when the real input
 * arrives and differs, zxv_emu_rollback_pending names the earliest tick to
 * redo and zxv_emu_rollback_replay (or zxv_emu_rollback + your own loop)
 * re-simulates. A tick's hash is FINAL only when every input up to it was
 * confirmed and no rollback below it is pending; only final hashes are
 * compared with peers.
 *
 * BUDGET. zxv_emu_set_tick_budget attaches an exact per-tick budget (a
 * libzxv rmag budget, needs ZXV_CAP_BUDGET); zxv_emu_charge consumes from
 * it during a tick and returns ZXV_E_BUDGET when the tick's work would pass
 * the limit. The budget is reset at the start of every tick.
 *
 * CANONICAL ENCODING (zxv_emu_state_export, and what is hashed):
 *
 *   "zxv/emu-state/v1"         16 ASCII bytes, no NUL
 *   tick                       u64 little endian
 *   rat_slots, hex_slots       u32 little endian each
 *   for each rat slot          num i64 LE, den i64 LE (reduced, den > 0)
 *   for each hex slot          tag u8 (0..63)
 *
 * hash(t) = SHA3-256(encoding of the state at tick t). Inputs are not
 * hashed; they reach the hash through the state they produce.
 *
 * CONVENTIONS (the libzxv rules, sdk/docs/ABI.md): every function returns
 * ZXV_OK or a negative ZXV_E_* status; out parameters are written only on
 * success; buffers that return data use the two-call form (R4). No
 * parameter is float or double (R11). A zxv_emu is NOT internally locked:
 * use it from one thread at a time.
 *
 * Status codes used here (from zxv_api.h):
 *   ZXV_E_ARG       NULL pointer, slot or player out of range, bad length
 *   ZXV_E_STATE     wrong phase (setup vs ticking, begin without commit,
 *                   strict mode with inputs missing)
 *   ZXV_E_RANGE     rational overflow; input tick outside the window
 *   ZXV_E_DUPLICATE a confirmed input resubmitted with different bytes
 *   ZXV_E_BUDGET    a charge would pass the per-tick budget
 *   ZXV_E_CAPACITY  the configuration would need too much memory
 *   ZXV_E_FORMAT    zxv_emu_state_import of a malformed encoding
 *   ZXV_E_NOMEM     allocation failed
 *   ZXV_E_NOT_FOUND no such tick in the history ring / nothing recorded
 *   ZXV_E_CAP, ZXV_E_POISONED  passed through from libzxv's budget calls
 */
#ifndef ZXV_EMU_H
#define ZXV_EMU_H

#include <stddef.h>
#include <stdint.h>

#include "zxv_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#    if defined(ZXV_EMU_BUILD_SHARED)
#        define ZXV_EMU_API __declspec(dllexport)
#    else
#        define ZXV_EMU_API
#    endif
#elif defined(__GNUC__) && defined(ZXV_EMU_BUILD_SHARED)
#    define ZXV_EMU_API __attribute__((visibility("default")))
#else
#    define ZXV_EMU_API
#endif

#define ZXV_EMU_API_MAJOR 0
#define ZXV_EMU_API_MINOR 1

#define ZXV_EMU_MAX_PLAYERS   64u
#define ZXV_EMU_MAX_SLOTS     65536u
#define ZXV_EMU_MAX_INPUT     256u
#define ZXV_EMU_MIN_HISTORY   2u
#define ZXV_EMU_MAX_HISTORY   4096u
/* Snapshots + input ring together may not exceed this many bytes. */
#define ZXV_EMU_MAX_MEMORY    (256u * 1024u * 1024u)
#define ZXV_EMU_HASH_BYTES    32u
#define ZXV_EMU_DOMAIN        "zxv/emu-state/v1"
#define ZXV_EMU_DOMAIN_BYTES  16u

/* Flags for zxv_emu_create. */
#define ZXV_EMU_F_PREDICT 0x1u /* predict missing inputs (rollback mode) */

typedef struct zxv_emu zxv_emu;

/* A tick's game rules. Called between begin and commit with the tick being
 * computed; read inputs with zxv_emu_input_get and change state with the
 * zxv_emu_rat_* / zxv_emu_hex_* setters. Return ZXV_OK to commit; any
 * other status aborts the tick (the state is restored, the tick does not
 * advance) and is returned to the caller. Must be a pure function of the
 * state and the inputs: no clocks, no host randomness, no floats. */
typedef int32_t (*zxv_emu_step_fn)(zxv_emu *sim, uint64_t tick, void *user);

/* ---- lifetime ---- */

/* players 1..64, rat_slots and hex_slots 0..65536 (not both 0),
 * input_bytes 1..256, history 2..4096 (ticks of snapshots kept; also the
 * input window: inputs are accepted up to `history` ticks ahead). */
ZXV_EMU_API int32_t zxv_emu_create(uint32_t players, uint32_t rat_slots, uint32_t hex_slots,
                                   uint32_t input_bytes, uint32_t history, uint32_t flags,
                                   zxv_emu **out_sim);
/* Also releases the budget's rmag. Destroy the sim before the zxv_cap
 * passed to zxv_emu_set_tick_budget and before its context (R6). */
ZXV_EMU_API void zxv_emu_destroy(zxv_emu *sim);

/* Thread-local detail text for the last failing call on this thread
 * (R4 two-call form). Branch on status codes, never on this text. */
ZXV_EMU_API int32_t zxv_emu_last_error(char *buf, size_t cap, size_t *out_len);

ZXV_EMU_API int32_t zxv_emu_info(const zxv_emu *sim, uint32_t *out_players,
                                 uint32_t *out_rat_slots, uint32_t *out_hex_slots,
                                 uint32_t *out_input_bytes, uint32_t *out_history,
                                 uint32_t *out_flags);

/* ---- setup (before zxv_emu_start) ---- */

/* Attach an exact per-tick budget: limit num/den (>= 0) through `cap`
 * (needs ZXV_CAP_BUDGET). The sim keeps the cap pointer: keep the cap and
 * its context alive until the sim is destroyed. Callable again (before or
 * after start, never inside a tick) to change the limit. */
ZXV_EMU_API int32_t zxv_emu_set_tick_budget(zxv_emu *sim, const zxv_cap *cap, int64_t num,
                                            int64_t den);

/* Freeze the initial state as tick 0 and hash it. Setters are refused
 * outside a tick from now on. */
ZXV_EMU_API int32_t zxv_emu_start(zxv_emu *sim);

/* ---- the fixed-step clock (local pacing; never hashed) ---- */

/* One tick lasts step_num/step_den seconds (e.g. 1/60); a feed returns at
 * most max_catchup ticks (1..1024), extra ones are dropped and counted. */
ZXV_EMU_API int32_t zxv_emu_clock_set_step(zxv_emu *sim, int64_t step_num, int64_t step_den,
                                           uint32_t max_catchup);
/* Add elapsed wall time (exact, e.g. nanoseconds/1e9) and learn how many
 * ticks are due now. out_dropped may be NULL. */
ZXV_EMU_API int32_t zxv_emu_clock_feed(zxv_emu *sim, int64_t elapsed_num, int64_t elapsed_den,
                                       uint32_t *out_due, uint64_t *out_dropped);
/* The fraction of a tick accumulated, in [0, 1): for render interpolation
 * only (the host converts it at the very edge; it never feeds back). */
ZXV_EMU_API int32_t zxv_emu_clock_alpha(const zxv_emu *sim, int64_t *out_num, int64_t *out_den);

/* ---- inputs ---- */

/* Submit `player`'s input for `tick` (len must equal input_bytes). Accepted
 * for ticks 1 .. current + history. Resending a confirmed input with the
 * same bytes is always OK (network resends); different bytes is
 * ZXV_E_DUPLICATE. A first arrival for a tick already simulated (predict
 * mode) is accepted only while its rollback point is still kept
 * (tick - 1 >= oldest kept tick), else ZXV_E_RANGE: that tick can then
 * never become final, and the peers must resync by state transfer. If it
 * differs from the prediction used, a rollback becomes pending. Not
 * callable inside a tick (ZXV_E_STATE). */
ZXV_EMU_API int32_t zxv_emu_input_submit(zxv_emu *sim, uint32_t player, uint64_t tick,
                                         const uint8_t *bytes, size_t len);
/* Inside a tick: the input `player` contributes to it (R4 two-call form;
 * the blob is input_bytes long). out_predicted may be NULL. */
ZXV_EMU_API int32_t zxv_emu_input_get(const zxv_emu *sim, uint32_t player, uint8_t *buf,
                                      size_t cap, size_t *out_len, int32_t *out_predicted);
/* 1 when the next tick may begin: started, not inside a tick, and (strict
 * mode) every player's input for it has arrived, or (predict mode) no
 * rollback is pending. */
ZXV_EMU_API int32_t zxv_emu_ready(const zxv_emu *sim, int32_t *out_ready);

/* ---- ticks ---- */

/* The last committed tick (0 right after start). */
ZXV_EMU_API int32_t zxv_emu_tick(const zxv_emu *sim, uint64_t *out_tick);
/* Begin tick current + 1: resets the tick budget and fixes the inputs
 * (predicting missing ones in predict mode). ZXV_E_STATE if not started,
 * already in a tick, inputs missing (strict) or a rollback is pending. */
ZXV_EMU_API int32_t zxv_emu_begin(zxv_emu *sim);
/* Commit: hash and keep the snapshot; the tick advances. */
ZXV_EMU_API int32_t zxv_emu_commit(zxv_emu *sim);
/* Abort: restore the state of the last committed tick. */
ZXV_EMU_API int32_t zxv_emu_abort(zxv_emu *sim);
/* begin, fn, then commit (fn returned ZXV_OK) or abort (anything else). */
ZXV_EMU_API int32_t zxv_emu_advance(zxv_emu *sim, zxv_emu_step_fn fn, void *user);

/* ---- state (setters: during setup or inside a tick only) ---- */

ZXV_EMU_API int32_t zxv_emu_rat_get(const zxv_emu *sim, uint32_t slot, int64_t *out_num,
                                    int64_t *out_den);
/* Stores the reduced form (zxv_rat_normalize). */
ZXV_EMU_API int32_t zxv_emu_rat_set(zxv_emu *sim, uint32_t slot, int64_t num, int64_t den);
/* slot += num/den, slot *= num/den: exact, ZXV_E_RANGE leaves it unchanged. */
ZXV_EMU_API int32_t zxv_emu_rat_add(zxv_emu *sim, uint32_t slot, int64_t num, int64_t den);
ZXV_EMU_API int32_t zxv_emu_rat_mul(zxv_emu *sim, uint32_t slot, int64_t num, int64_t den);
ZXV_EMU_API int32_t zxv_emu_hex_get(const zxv_emu *sim, uint32_t slot, uint8_t *out_tag);
/* tag 0..63 (ZXV_E_ARG above). Combine tags with zxv_hex_* first. */
ZXV_EMU_API int32_t zxv_emu_hex_set(zxv_emu *sim, uint32_t slot, uint8_t tag);
/* Inside a tick: consume from the tick budget (no-op without a budget). */
ZXV_EMU_API int32_t zxv_emu_charge(zxv_emu *sim, int64_t num, int64_t den);

/* ---- hashes, desync, rollback ---- */

/* The hash of a kept tick; *out_final = 1 when it can no longer change
 * (out_final may be NULL). ZXV_E_NOT_FOUND for a tick not (or no longer)
 * kept. */
ZXV_EMU_API int32_t zxv_emu_hash(const zxv_emu *sim, uint64_t tick, uint8_t out[32],
                                 int32_t *out_final);
/* The highest tick whose hash is final. */
ZXV_EMU_API int32_t zxv_emu_confirmed_tick(zxv_emu *sim, uint64_t *out_tick);
/* Compare a peer's hash for `tick`: ZXV_LP_TRUE (equal), ZXV_LP_FALSE
 * (different: a desync, recorded), ZXV_LP_NEITHER (not final here yet, or
 * not kept: nothing can be said). */
ZXV_EMU_API int32_t zxv_emu_compare(zxv_emu *sim, uint64_t tick, const uint8_t remote[32],
                                    int32_t *out_lp);
/* The earliest tick a compare found different; *out_has = 0 when none. */
ZXV_EMU_API int32_t zxv_emu_desync(const zxv_emu *sim, uint64_t *out_tick, int32_t *out_has);
/* Predict mode: *out_pending = 1 and *out_tick = the earliest tick whose
 * input changed after it was simulated. */
ZXV_EMU_API int32_t zxv_emu_rollback_pending(const zxv_emu *sim, uint64_t *out_tick,
                                             int32_t *out_pending);
/* Restore the kept snapshot of `to_tick` (<= current) as the current
 * state; later snapshots are discarded, inputs are kept. A pending
 * rollback at tick p is cleared when to_tick < p (the changed input will
 * be applied when tick p is simulated again). */
ZXV_EMU_API int32_t zxv_emu_rollback(zxv_emu *sim, uint64_t to_tick);
/* If a rollback is pending at tick p: roll back to p - 1 and advance with
 * fn back to the tick the sim was at. No-op when nothing is pending. */
ZXV_EMU_API int32_t zxv_emu_rollback_replay(zxv_emu *sim, zxv_emu_step_fn fn, void *user);

/* ---- state transfer (late join, resync after a desync) ---- */

/* The canonical encoding of the current committed state (R4 two-call). */
ZXV_EMU_API int32_t zxv_emu_state_export(const zxv_emu *sim, uint8_t *buf, size_t cap,
                                         size_t *out_len);
/* Replace the state with an encoding from a sim of the same shape. The
 * encoded tick becomes the current tick (and the sim counts as started);
 * history, pending rollback and desync record are cleared; inputs for later
 * ticks are kept. ZXV_E_FORMAT on any malformed or non-canonical input. */
ZXV_EMU_API int32_t zxv_emu_state_import(zxv_emu *sim, const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ZXV_EMU_H */
