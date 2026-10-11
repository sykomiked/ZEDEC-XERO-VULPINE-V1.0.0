/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_hex.c — ABI 1.3: the 64-state hexagram tag and its quarantine log.
 * Every computation is the kernel's (kernel/src/lpres/lpres_hex.c, rules
 * X1-X6); this file validates arguments (a tag above 63 or a field above 3
 * is ZXV_E_ARG, never masked), turns the kernel's silent refusals into
 * status codes, and owns the zxv_hexlog handle. */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "zxv_internal.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "lpres_hex.h"

/* The ABI constants are the kernel's codes; a mismatch must not compile. */
_Static_assert(ZXV_HEX_STATES == LPX_STATES, "64 tags");
_Static_assert(ZXV_HEX_BASE_FALSE == LPX_B_FALSE && ZXV_HEX_BASE_NEITHER == LPX_B_NEITHER &&
                   ZXV_HEX_BASE_BOTH == LPX_B_BOTH && ZXV_HEX_BASE_TRUE == LPX_B_TRUE,
               "base codes");
_Static_assert(ZXV_HEX_POL_ENTROPIC == LPX_P_ENTROPIC && ZXV_HEX_POL_NEUTRAL == LPX_P_NEUTRAL &&
                   ZXV_HEX_POL_FORBIDDEN == LPX_P_FORBIDDEN &&
                   ZXV_HEX_POL_NEGENTROPIC == LPX_P_NEGENTROPIC,
               "polarity codes");
_Static_assert(ZXV_HEX_PHASE_POTENTIAL == LPX_PH_POTENTIAL &&
                   ZXV_HEX_PHASE_EMERGING == LPX_PH_EMERGING &&
                   ZXV_HEX_PHASE_DISSOLVING == LPX_PH_DISSOLVING &&
                   ZXV_HEX_PHASE_MANIFEST == LPX_PH_MANIFEST,
               "phase codes");
_Static_assert(ZXV_HEX_ACT_PROCEED == LPX_ACT_PROCEED && ZXV_HEX_ACT_WAIT == LPX_ACT_WAIT &&
                   ZXV_HEX_ACT_REFUSE == LPX_ACT_REFUSE &&
                   ZXV_HEX_ACT_ESCALATE == LPX_ACT_ESCALATE &&
                   ZXV_HEX_ACT_ROLLBACK == LPX_ACT_ROLLBACK,
               "action codes");
_Static_assert(ZXV_LP_NEITHER == LPRES_STATE_NEITHER && ZXV_LP_TRUE == LPRES_STATE_TRUE &&
                   ZXV_LP_FALSE == LPRES_STATE_FALSE && ZXV_LP_BOTH == LPRES_STATE_BOTH,
               "lpres states");
_Static_assert(ZXV_HEXLOG_HASH_BYTES == LPX_QLOG_HASH, "qlog hash size");

#define TAG_OK(t) ((t) < ZXV_HEX_STATES)
#define FIELD_OK(f) ((f) <= 3u)

static int32_t bad_tag(const char *what, uint8_t t)
{
    return zxvi_fail(ZXV_E_ARG, "%s: tag %u is not 0..63", what, (unsigned) t);
}

int32_t zxv_hex_pack(uint8_t base, uint8_t polarity, uint8_t phase, uint8_t *out_tag)
{
    if (!out_tag) return zxvi_fail(ZXV_E_ARG, "hex_pack: out_tag is NULL");
    if (!FIELD_OK(base) || !FIELD_OK(polarity) || !FIELD_OK(phase))
        return zxvi_fail(ZXV_E_ARG, "hex_pack: fields must be 0..3 (got %u, %u, %u)",
                         (unsigned) base, (unsigned) polarity, (unsigned) phase);
    *out_tag = lpx_make(base, polarity, phase);
    return zxvi_ok();
}

int32_t zxv_hex_unpack(uint8_t tag, uint8_t *out_base, uint8_t *out_polarity, uint8_t *out_phase)
{
    if (!TAG_OK(tag)) return bad_tag("hex_unpack", tag);
    if (out_base) *out_base = lpx_base(tag);
    if (out_polarity) *out_polarity = lpx_pol(tag);
    if (out_phase) *out_phase = lpx_phase(tag);
    return zxvi_ok();
}

int32_t zxv_hex_action(uint8_t tag, int32_t *out_action)
{
    if (!out_action) return zxvi_fail(ZXV_E_ARG, "hex_action: out_action is NULL");
    if (!TAG_OK(tag)) return bad_tag("hex_action", tag);
    *out_action = (int32_t) lpx_action(tag);
    return zxvi_ok();
}

int32_t zxv_hex_phase_next(uint8_t phase, uint8_t *out_phase)
{
    if (!out_phase) return zxvi_fail(ZXV_E_ARG, "hex_phase_next: out_phase is NULL");
    if (!FIELD_OK(phase))
        return zxvi_fail(ZXV_E_ARG, "hex_phase_next: phase %u is not 0..3", (unsigned) phase);
    *out_phase = lpx_phase_next(phase);
    return zxvi_ok();
}

int32_t zxv_hex_step(uint8_t tag, uint8_t phase, uint8_t *out_tag)
{
    if (!out_tag) return zxvi_fail(ZXV_E_ARG, "hex_step: out_tag is NULL");
    if (!TAG_OK(tag)) return bad_tag("hex_step", tag);
    if (!FIELD_OK(phase)) return zxvi_fail(ZXV_E_ARG, "hex_step: phase %u is not 0..3", (unsigned) phase);
    lpx_t t = tag;
    if (!lpx_advance(&t, phase))
        return zxvi_fail(ZXV_E_STATE, "hex_step: %s -> %s skips a phase (next is %s)",
                         lpx_phase_name(lpx_phase(tag)), lpx_phase_name(phase),
                         lpx_phase_name(lpx_phase_next(lpx_phase(tag))));
    *out_tag = t;
    return zxvi_ok();
}

int32_t zxv_hex_set(uint8_t tag, uint8_t base, uint8_t polarity, uint8_t *out_tag)
{
    if (!out_tag) return zxvi_fail(ZXV_E_ARG, "hex_set: out_tag is NULL");
    if (!TAG_OK(tag)) return bad_tag("hex_set", tag);
    if (!FIELD_OK(base) || !FIELD_OK(polarity))
        return zxvi_fail(ZXV_E_ARG, "hex_set: base and polarity must be 0..3");
    if (lpx_forbidden(tag) && polarity != LPX_P_FORBIDDEN)
        return zxvi_fail(ZXV_E_STATE, "hex_set: a forbidden tag stays forbidden (absorbing)");
    *out_tag = lpx_set(tag, base, polarity);
    return zxvi_ok();
}

int32_t zxv_hex_forbid(uint8_t tag, uint8_t *out_tag)
{
    if (!out_tag) return zxvi_fail(ZXV_E_ARG, "hex_forbid: out_tag is NULL");
    if (!TAG_OK(tag)) return bad_tag("hex_forbid", tag);
    *out_tag = lpx_forbid(tag);
    return zxvi_ok();
}

int32_t zxv_hex_and(uint8_t a, uint8_t b, uint8_t *out_tag)
{
    if (!out_tag) return zxvi_fail(ZXV_E_ARG, "hex_and: out_tag is NULL");
    if (!TAG_OK(a)) return bad_tag("hex_and", a);
    if (!TAG_OK(b)) return bad_tag("hex_and", b);
    *out_tag = lpx_and(a, b);
    return zxvi_ok();
}

int32_t zxv_hex_or(uint8_t a, uint8_t b, uint8_t *out_tag)
{
    if (!out_tag) return zxvi_fail(ZXV_E_ARG, "hex_or: out_tag is NULL");
    if (!TAG_OK(a)) return bad_tag("hex_or", a);
    if (!TAG_OK(b)) return bad_tag("hex_or", b);
    *out_tag = lpx_or(a, b);
    return zxvi_ok();
}

int32_t zxv_hex_not(uint8_t a, uint8_t *out_tag)
{
    if (!out_tag) return zxvi_fail(ZXV_E_ARG, "hex_not: out_tag is NULL");
    if (!TAG_OK(a)) return bad_tag("hex_not", a);
    *out_tag = lpx_not(a);
    return zxvi_ok();
}

int32_t zxv_hex_polarity(uint32_t pos0, uint32_t neg0, uint32_t pos1, uint32_t neg1, int32_t stale,
                         uint8_t *out_polarity)
{
    if (!out_polarity) return zxvi_fail(ZXV_E_ARG, "hex_polarity: out_polarity is NULL");
    *out_polarity = lpx_polarity(pos0, neg0, pos1, neg1, stale != 0);
    return zxvi_ok();
}

int32_t zxv_hex_from_lp(int32_t lp_state, uint8_t *out_base)
{
    if (!out_base) return zxvi_fail(ZXV_E_ARG, "hex_from_lp: out_base is NULL");
    if (lp_state < ZXV_LP_NEITHER || lp_state > ZXV_LP_BOTH)
        return zxvi_fail(ZXV_E_ARG, "hex_from_lp: %d is not a ZXV_LP_* value", (int) lp_state);
    *out_base = lpx_base_from_lpres((lpres_state_t) lp_state);
    return zxvi_ok();
}

int32_t zxv_hex_to_lp(uint8_t base, int32_t *out_lp_state)
{
    if (!out_lp_state) return zxvi_fail(ZXV_E_ARG, "hex_to_lp: out_lp_state is NULL");
    if (!FIELD_OK(base)) return zxvi_fail(ZXV_E_ARG, "hex_to_lp: base %u is not 0..3", (unsigned) base);
    *out_lp_state = (int32_t) lpx_base_to_lpres(base);
    return zxvi_ok();
}

/* "base/polarity/phase" for all 64 tags, in tag order, from the kernel's
 * own field names (checked against them by test_zxv_abi13). */
#define PH(b, p) b "/" p "/potential", b "/" p "/emerging", b "/" p "/dissolving", b "/" p "/manifest"
#define BASE(b) PH(b, "entropic"), PH(b, "neutral"), PH(b, "forbidden"), PH(b, "negentropic")
static const char *const HEX_NAMES[64] = {BASE("false"), BASE("neither"), BASE("both"),
                                          BASE("true")};
#undef BASE
#undef PH

const char *zxv_hex_name(uint8_t tag)
{
    return TAG_OK(tag) ? HEX_NAMES[tag] : "INVALID";
}

const char *zxv_hex_action_name(int32_t action)
{
    return action >= ZXV_HEX_ACT_PROCEED && action <= ZXV_HEX_ACT_ROLLBACK
               ? lpx_action_name((lpx_action_t) action)
               : "INVALID";
}

/* ===================================================================== */
/* The quarantine log                                                    */
/* ===================================================================== */

#define HEXLOG_MAGIC 0x5A58514Cu /* "ZXQL" */

struct zxv_hexlog {
    uint32_t magic;
    zxv_ctx *ctx;
    pthread_mutex_t mu;
    lpx_qlog_t q;
    lpx_qrec_t rec[];
};

static int32_t log_ok(const zxv_hexlog *log, const char *what)
{
    if (!log || log->magic != HEXLOG_MAGIC)
        return zxvi_fail(ZXV_E_ARG, "%s: log is NULL or not a zxv_hexlog", what);
    return zxvi_ctx_check(log->ctx, what);
}

int32_t zxv_hexlog_create(const zxv_cap *cap, uint32_t capacity, zxv_hexlog **out_log)
{
    zxv_ctx *ctx;
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_ATTEST, "hexlog_create", &ctx);
    if (rc) return rc;
    if (!out_log) return zxvi_fail(ZXV_E_ARG, "hexlog_create: out_log is NULL");
    if (capacity == 0 || capacity > ZXV_HEXLOG_MAX_RECORDS)
        return zxvi_fail(ZXV_E_ARG, "hexlog_create: capacity %u is not 1..%u", capacity,
                         ZXV_HEXLOG_MAX_RECORDS);
    zxv_hexlog *l = calloc(1, sizeof *l + (size_t) capacity * sizeof(lpx_qrec_t));
    if (!l) return zxvi_fail(ZXV_E_NOMEM, "hexlog_create: allocation failed");
    l->magic = HEXLOG_MAGIC;
    l->ctx = ctx;
    pthread_mutex_init(&l->mu, NULL);
    lpx_qlog_init(&l->q, l->rec, capacity);
    *out_log = l;
    return zxvi_ok();
}

void zxv_hexlog_destroy(zxv_hexlog *log)
{
    if (!log || log->magic != HEXLOG_MAGIC) return;
    log->magic = 0;
    pthread_mutex_destroy(&log->mu);
    free(log);
}

int32_t zxv_hexlog_append(const zxv_cap *cap, zxv_hexlog *log, uint8_t tag, uint8_t reason,
                          uint32_t submitter, const uint8_t subject[32], uint64_t *out_seq)
{
    if (!log || log->magic != HEXLOG_MAGIC)
        return zxvi_fail(ZXV_E_ARG, "hexlog_append: log is NULL or not a zxv_hexlog");
    int32_t rc = zxvi_cap_gate_ctx(cap, ZXV_CAP_ATTEST, "hexlog_append", log->ctx);
    if (rc) return rc;
    if (!subject) return zxvi_fail(ZXV_E_ARG, "hexlog_append: subject is NULL");
    if (!TAG_OK(tag)) return bad_tag("hexlog_append", tag);
    if (!lpx_forbidden(tag))
        return zxvi_fail(ZXV_E_ARG, "hexlog_append: only forbidden tags are quarantined (%s)",
                         HEX_NAMES[tag]);
    pthread_mutex_lock(&log->mu);
    uint32_t seq = log->q.n;
    bool ok = lpx_qlog_append(&log->q, tag, reason, submitter, subject);
    uint32_t capacity = log->q.cap;
    pthread_mutex_unlock(&log->mu);
    if (!ok)
        return zxvi_fail(ZXV_E_CAPACITY,
                         "hexlog_append: quarantine log is full (%u records); halt the context",
                         capacity);
    if (out_seq) *out_seq = seq;
    return zxvi_ok();
}

int32_t zxv_hexlog_count(zxv_hexlog *log, uint32_t *out_count)
{
    int32_t rc = log_ok(log, "hexlog_count");
    if (rc) return rc;
    if (!out_count) return zxvi_fail(ZXV_E_ARG, "hexlog_count: out_count is NULL");
    pthread_mutex_lock(&log->mu);
    *out_count = log->q.n;
    pthread_mutex_unlock(&log->mu);
    return zxvi_ok();
}

int32_t zxv_hexlog_head(zxv_hexlog *log, uint8_t out_head[32])
{
    int32_t rc = log_ok(log, "hexlog_head");
    if (rc) return rc;
    if (!out_head) return zxvi_fail(ZXV_E_ARG, "hexlog_head: out_head is NULL");
    pthread_mutex_lock(&log->mu);
    memcpy(out_head, log->q.head, LPX_QLOG_HASH);
    pthread_mutex_unlock(&log->mu);
    return zxvi_ok();
}

int32_t zxv_hexlog_at(zxv_hexlog *log, uint32_t index, uint64_t *out_seq, uint8_t *out_tag,
                      uint8_t *out_reason, uint32_t *out_submitter, uint8_t out_subject[32],
                      uint8_t out_prev[32], uint8_t out_hash[32])
{
    int32_t rc = log_ok(log, "hexlog_at");
    if (rc) return rc;
    pthread_mutex_lock(&log->mu);
    if (index >= log->q.n) {
        uint32_t n = log->q.n;
        pthread_mutex_unlock(&log->mu);
        return zxvi_fail(ZXV_E_NOT_FOUND, "hexlog_at: index %u, log holds %u", index, n);
    }
    const lpx_qrec_t *r = &log->q.rec[index];
    if (out_seq) *out_seq = r->seq;
    if (out_tag) *out_tag = r->tag;
    if (out_reason) *out_reason = r->reason;
    if (out_submitter) *out_submitter = r->submitter;
    if (out_subject) memcpy(out_subject, r->subject, 32);
    if (out_prev) memcpy(out_prev, r->prev, LPX_QLOG_HASH);
    if (out_hash) memcpy(out_hash, r->hash, LPX_QLOG_HASH);
    pthread_mutex_unlock(&log->mu);
    return zxvi_ok();
}

int32_t zxv_hexlog_verify(zxv_hexlog *log)
{
    int32_t rc = log_ok(log, "hexlog_verify");
    if (rc) return rc;
    pthread_mutex_lock(&log->mu);
    bool ok = lpx_qlog_verify(&log->q);
    pthread_mutex_unlock(&log->mu);
    if (ok) return zxvi_ok();
    if ((rc = zxvi_ctx_enter(log->ctx))) return rc;
    rc = zxvi_ctx_poison(log->ctx, "quarantine log chain does not verify");
    zxvi_ctx_leave(log->ctx);
    return rc;
}
