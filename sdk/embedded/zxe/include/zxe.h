/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxe.h — small helpers for running ZXV on microcontrollers, on top of the
 * libzxv C ABI (zxv_api.h) and the port layer (zxp.h).
 *
 *   zxe_ledger   an exact double-entry ledger in caller-supplied, fixed
 *                memory. libzxv's own ledger needs a ~24 MB kernel book per
 *                512 accounts (README "What runs"), which no MCU has; this
 *                one keeps libzxv's posting rules, status codes and
 *                capability checks, and a host differential test
 *                (test/test_zxe_ledger.c) checks it against libzxv's ledger
 *                call for call. It keeps balances and a SHA3-256 hash chain
 *                of postings, not the journal: stream the journal out
 *                through the sink (write-ahead: a sink failure refuses the
 *                posting).
 *   zxe_clock    drives a zxv_phase clock from the monotonic microsecond
 *                clock at an integer tick rate, exactly (the remainder is
 *                carried, so there is no drift and no floating point).
 *
 * ADVISORY ONLY for any actuation: see README "Safety". */
#ifndef ZXE_H
#define ZXE_H

#include <stddef.h>
#include <stdint.h>

#include "zxp.h"
#include "zxv_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== Ledger ===== */

#define ZXE_LEDGER_NAME_MAX 63u /* as libzxv */
#define ZXE_LEDGER_REF_MAX  35u /* as libzxv */
#ifndef ZXE_LEDGER_MAX_CCY
#    define ZXE_LEDGER_MAX_CCY 4u
#endif

typedef struct {
    char name[ZXE_LEDGER_NAME_MAX + 1];
    uint8_t ccy;  /* index into the ledger's currency table */
    uint8_t kind; /* ZXV_ACCT_HOLDER or ZXV_ACCT_ISSUER */
    int64_t debit;  /* 555: sum received */
    int64_t credit; /* 777: sum sent     */
} zxe_account;

/* A reference key: the first 16 bytes of SHA3-256(reference). Two
 * different references with equal keys (probability ~2^-128 per pair) make
 * the second posting a false DUPLICATE: a refusal, never a double post. */
typedef struct {
    uint8_t k[16];
} zxe_refkey;

/* Called once per posting, BEFORE it is committed, with the 32-byte chain
 * head the posting will produce. Return 0 to commit; anything else refuses
 * the posting with ZXV_E_IO and changes nothing (write-ahead journal). */
typedef int (*zxe_journal_fn)(void *user, uint64_t entry_id, uint32_t from, uint32_t to,
                              int64_t amount_minor, const char *ccy, const char *reference,
                              const uint8_t new_head[32]);

typedef struct {
    char code[4];
    uint8_t minor;
} zxe_ccy;

typedef struct {
    zxv_ctx *ctx;
    zxp_mutex_t mu;
    zxe_account *acct;
    uint32_t n_acct, max_acct;
    zxe_ccy ccy[ZXE_LEDGER_MAX_CCY];
    uint32_t n_ccy;
    zxe_refkey *refs;
    uint32_t ref_slots, n_refs; /* open addressing, at most half full */
    uint64_t next_entry_id;
    uint8_t head[32];
    zxe_journal_fn sink;
    void *sink_user;
    int poisoned;
    int ready;
} zxe_ledger;

/* Bind a ledger to a context (its poisoning is shared both ways: a halted
 * context refuses the ledger, and a broken ledger invariant halts the
 * context) and to caller memory: `accounts[max_accounts]` and
 * `refs[ref_slots]` (ref_slots a power of two >= 2; at most ref_slots/2
 * postings, then ZXV_E_CAPACITY until the period is closed with
 * zxe_ledger_reset_refs). Needs zxp_init() to have succeeded (it creates
 * a mutex). */
int32_t zxe_ledger_init(zxe_ledger *l, zxv_ctx *ctx, zxe_account *accounts, uint32_t max_accounts,
                        zxe_refkey *refs, uint32_t ref_slots);
void zxe_ledger_destroy(zxe_ledger *l);
/* The journal sink (NULL for none). LEDGER_POST. */
int32_t zxe_ledger_set_sink(zxe_ledger *l, const zxv_cap *cap, zxe_journal_fn fn, void *user);

/* As zxv_ccy_enable_with_minor_cap without an ISO 4217 table: three
 * upper-case letters, 0..4 minor units, idempotent. LEDGER_POST. */
int32_t zxe_ledger_ccy_enable(zxe_ledger *l, const zxv_cap *cap, const char *alpha,
                              uint8_t minor_units);
/* As zxv_ledger_open_account_cap. LEDGER_POST. */
int32_t zxe_ledger_open_account(zxe_ledger *l, const zxv_cap *cap, const char *name,
                                const char *ccy, int32_t kind, uint32_t *out_account_id);
/* As zxv_ledger_post_cap: same validation order and status codes. */
int32_t zxe_ledger_post(zxe_ledger *l, const zxv_cap *cap, uint32_t from_account,
                        uint32_t to_account, int64_t amount_minor, const char *ccy,
                        const char *reference, uint64_t *out_entry_id);
/* As zxv_ledger_balance_cap. LEDGER_READ. */
int32_t zxe_ledger_balance(zxe_ledger *l, const zxv_cap *cap, uint32_t account,
                           int64_t *out_debit_minor, int64_t *out_credit_minor,
                           int64_t *out_equity_minor);
/* As zxv_ledger_check_cap: per currency sum(debit) == sum(credit), every
 * holder >= 0. A failure poisons the ledger AND halts the context. */
int32_t zxe_ledger_check(zxe_ledger *l, const zxv_cap *cap);
/* The hash-chain head after the last posting (32 zeros when none):
 * head_k = SHA3-256("zxe/ledger/v1" || head_{k-1} || entry_id u64 LE ||
 * from u32 LE || to u32 LE || amount i64 LE || len(ccy) u8 || ccy ||
 * len(ref) u8 || ref). LEDGER_READ. */
int32_t zxe_ledger_head(zxe_ledger *l, const zxv_cap *cap, uint8_t out_head[32]);
/* Close a reference period: forget the reference keys (balances and the
 * head stay). Only safe when the upstream system guarantees references of
 * the old period are never replayed. LEDGER_POST. */
int32_t zxe_ledger_reset_refs(zxe_ledger *l, const zxv_cap *cap);
/* Corrupt the ledger on purpose (tests only, ZXE_TESTING). */
#ifdef ZXE_TESTING
void zxe_ledger_test_corrupt(zxe_ledger *l, uint32_t account, int64_t delta_debit);
#endif

/* ===== Clock ===== */

typedef struct {
    uint64_t last_us;
    uint64_t rem;     /* carried remainder, in tick-microseconds (< 1e6) */
    uint32_t tps;     /* ticks per second */
    int started;
} zxe_clock;

/* ticks_per_second > 0. ZXV_PHASE_CYCLE (27,720) per second makes one
 * harmonic fundamental per second. */
int32_t zxe_clock_init(zxe_clock *c, uint32_t ticks_per_second);
/* Advance `phase` by the whole ticks elapsed since the previous pump, at
 * time `now_us`. The first call only records the time. Time going
 * backwards is ZXV_E_STATE. Out pointers may be NULL. Needs PHASE. */
int32_t zxe_clock_pump_at(zxe_clock *c, const zxv_cap *cap, zxv_phase *phase, uint64_t now_us,
                          uint64_t *out_ticks_added, uint64_t *out_now_tick,
                          uint32_t *out_cleared);
/* The same at zxp_monotonic_us(). */
int32_t zxe_clock_pump(zxe_clock *c, const zxv_cap *cap, zxv_phase *phase,
                       uint64_t *out_ticks_added, uint64_t *out_now_tick, uint32_t *out_cleared);

#ifdef __cplusplus
}
#endif

#endif /* ZXE_H */
