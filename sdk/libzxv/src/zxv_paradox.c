/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_paradox.c — ABI 1.5: the paradox engine over kernel/src/paradox
 * (paradox.h: classifier, registry, the four operators, complex-paradox
 * graphs). Every decision is the kernel's; this file validates arguments,
 * checks the (kind, tag) class pair, owns the handles (the book with its
 * three logs and registry, policies, holds, graphs), gates on
 * ZXV_CAP_PARADOX and turns the kernel's outcomes into status codes and
 * byte records. */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "zxv_internal.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "paradox.h"
#include "sha256.h"

_Static_assert(ZXV_PDX_KIND_CONSTRUCTIVE == PDX_KIND_CONSTRUCTIVE &&
                   ZXV_PDX_KIND_DESTRUCTIVE == PDX_KIND_DESTRUCTIVE &&
                   ZXV_PDX_KIND_NEUTRAL == PDX_KIND_NEUTRAL &&
                   ZXV_PDX_KIND_SPECIAL == PDX_KIND_SPECIAL && ZXV_PDX_KIND_NONE == PDX_KIND_NONE,
               "kinds");
_Static_assert(ZXV_PDX_OUT_APPLIED == PDX_OUT_APPLIED && ZXV_PDX_OUT_KEPT == PDX_OUT_KEPT &&
                   ZXV_PDX_OUT_PROPOSAL_FOR_OWNER == PDX_OUT_PROPOSAL_FOR_OWNER &&
                   ZXV_PDX_OUT_ROLLED_BACK == PDX_OUT_ROLLED_BACK &&
                   ZXV_PDX_OUT_CONTAINED == PDX_OUT_CONTAINED &&
                   ZXV_PDX_OUT_DEFERRED == PDX_OUT_DEFERRED &&
                   ZXV_PDX_OUT_SETTLED == PDX_OUT_SETTLED &&
                   ZXV_PDX_OUT_RECORDED == PDX_OUT_RECORDED &&
                   ZXV_PDX_OUT_ACCEPTED == PDX_OUT_ACCEPTED &&
                   ZXV_PDX_OUT_PENDING == PDX_OUT_PENDING &&
                   ZXV_PDX_OUT_RESOLVED == PDX_OUT_RESOLVED &&
                   ZXV_PDX_OUT_ESCALATE_TO_OWNER == PDX_OUT_ESCALATE_TO_OWNER &&
                   ZXV_PDX_OUT_REFUSED == PDX_OUT_REFUSED &&
                   ZXV_PDX_OUT_LOG_FULL == PDX_OUT_LOG_FULL,
               "outcomes");
_Static_assert(ZXV_PDX_NS_OPEN == PDX_NS_OPEN && ZXV_PDX_NS_CLEAR == PDX_NS_CLEAR &&
                   ZXV_PDX_NS_CONTAINED == PDX_NS_CONTAINED &&
                   ZXV_PDX_NS_ROLLED_BACK == PDX_NS_ROLLED_BACK &&
                   ZXV_PDX_NS_TAINTED == PDX_NS_TAINTED &&
                   ZXV_PDX_NS_SANDBOXED == PDX_NS_SANDBOXED &&
                   ZXV_PDX_NS_WAITING == PDX_NS_WAITING && ZXV_PDX_NS_HELD == PDX_NS_HELD &&
                   ZXV_PDX_NS_BLOCKED == PDX_NS_BLOCKED && ZXV_PDX_NS_APPLIED == PDX_NS_APPLIED,
               "node states");
_Static_assert(ZXV_PDX_FLAG_MONEY == PDX_DRAFT_MONEY && ZXV_PDX_FLAG_DEVICE == PDX_DRAFT_DEVICE,
               "flags");
_Static_assert(ZXV_PDX_TRIP_STEPS == PDX_TRIP_STEPS && ZXV_PDX_TRIP_BUDGET == PDX_TRIP_BUDGET &&
                   ZXV_PDX_TRIP_CYCLE == PDX_TRIP_CYCLE &&
                   ZXV_PDX_TRIP_INVARIANT == PDX_TRIP_INVARIANT &&
                   ZXV_PDX_TRIP_FORBIDDEN == PDX_TRIP_FORBIDDEN,
               "trips");
_Static_assert(ZXV_PDX_HASH_BYTES == PDX_HASH && ZXV_PDX_GRAPH_MAX == PDX_GRAPH_MAX &&
                   ZXV_PDX_MAX_DEPS == PDX_MAX_DEPS,
               "sizes");
_Static_assert(ZXV_PDX_SRC_SEED == PDX_SRC_SEED, "seed");
_Static_assert(ZXV_HEX_ACT_ROLLBACK == LPX_ACT_ROLLBACK && ZXV_HEX_POL_FORBIDDEN == LPX_P_FORBIDDEN,
               "hex");

#define FLAGS_ALL (ZXV_PDX_FLAG_MONEY | ZXV_PDX_FLAG_DEVICE)

uint32_t zxvi_feat2_paradox(void)
{
    return ZXV_FEAT2_PARADOX;
}

/* ===================================================================== */
/* helpers                                                               */
/* ===================================================================== */

static void le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}
static void le64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

static bool registry_kind(int32_t k)
{
    return k == ZXV_PDX_KIND_CONSTRUCTIVE || k == ZXV_PDX_KIND_DESTRUCTIVE ||
           k == ZXV_PDX_KIND_NEUTRAL;
}

static uint8_t kind_pol(int32_t kind)
{
    if (kind == PDX_KIND_CONSTRUCTIVE) return LPX_P_NEGENTROPIC;
    if (kind == PDX_KIND_DESTRUCTIVE) return LPX_P_ENTROPIC;
    return LPX_P_NEUTRAL;
}

/* THE CLASS PAIR: is (kind, tag) something pdx_classify can return? */
static int32_t mk_class(int32_t kind, uint8_t tag, pdx_class_t *c, const char *what)
{
    if (tag >= LPX_STATES) return zxvi_fail(ZXV_E_ARG, "%s: tag %u is not 0..63", what, tag);
    if (kind < 0 || kind > ZXV_PDX_KIND_NONE)
        return zxvi_fail(ZXV_E_ARG, "%s: kind %d is not 0..4", what, (int) kind);
    bool forb = lpx_forbidden(tag);
    if (!forb) {
        uint8_t base = lpx_base(tag), pol = lpx_pol(tag);
        bool ok;
        if (kind == ZXV_PDX_KIND_NONE)
            ok = base == LPX_B_TRUE || base == LPX_B_FALSE;
        else
            ok = (base == LPX_B_BOTH || base == LPX_B_NEITHER) && pol == kind_pol(kind) &&
                 (kind != ZXV_PDX_KIND_SPECIAL ||
                  (base == LPX_B_BOTH && lpx_phase(tag) == LPX_PH_POTENTIAL));
        if (!ok)
            return zxvi_fail(ZXV_E_ARG,
                             "%s: kind %s does not match tag %u (classify the tag first)", what,
                             pdx_kind_name((uint8_t) kind), tag);
    }
    c->kind = (uint8_t) kind;
    c->tag = tag;
    c->forbidden = forb;
    c->reg_index = -1;
    return ZXV_OK;
}

/* ===================================================================== */
/* pure                                                                  */
/* ===================================================================== */

int32_t zxv_paradox_signature(const uint8_t *data, size_t len, uint8_t out[32])
{
    if (!out || (!data && len)) return zxvi_fail(ZXV_E_ARG, "paradox_signature: NULL argument");
    pdx_signature(data, len, out);
    return zxvi_ok();
}

int32_t zxv_paradox_kind_of_tag(uint8_t tag, int32_t *out_kind)
{
    if (!out_kind) return zxvi_fail(ZXV_E_ARG, "paradox_kind_of_tag: out_kind is NULL");
    if (tag >= LPX_STATES)
        return zxvi_fail(ZXV_E_ARG, "paradox_kind_of_tag: tag %u is not 0..63", tag);
    *out_kind = pdx_kind_of_tag(tag);
    return zxvi_ok();
}

int32_t zxv_paradox_action(int32_t kind, uint8_t tag, int32_t *out_do)
{
    if (!out_do) return zxvi_fail(ZXV_E_ARG, "paradox_action: out_do is NULL");
    pdx_class_t c;
    int32_t rc = mk_class(kind, tag, &c, "paradox_action");
    if (rc) return rc;
    *out_do = c.forbidden                    ? ZXV_PDX_DO_ROLLBACK
              : kind == ZXV_PDX_KIND_NONE ? ZXV_PDX_DO_NONE
                                          : kind + 1; /* SYNTHESIZE .. RECORD */
    return zxvi_ok();
}

int32_t zxv_paradox_outcome_action(int32_t outcome, int32_t *out_action)
{
    if (!out_action) return zxvi_fail(ZXV_E_ARG, "paradox_outcome_action: out_action is NULL");
    if (outcome < 0 || outcome > ZXV_PDX_OUT_LOG_FULL)
        return zxvi_fail(ZXV_E_ARG, "paradox_outcome_action: outcome %d is not 0..13",
                         (int) outcome);
    *out_action = (int32_t) pdx_out_action((pdx_out_t) outcome);
    return zxvi_ok();
}

static void repair_what(const uint8_t *p, uint8_t out[32])
{
    sha256_ctx_t h;
    sha256_init(&h);
    sha256_update(&h, (const uint8_t *) "zxv/pdx-repair/v1", 17);
    sha256_update(&h, p, 120);
    sha256_final(&h, out);
}

int32_t zxv_paradox_repair_verify(const uint8_t proof[160])
{
    if (!proof) return zxvi_fail(ZXV_E_ARG, "paradox_repair_verify: proof is NULL");
    uint8_t w[32], d = 0;
    repair_what(proof, w);
    for (int i = 0; i < 32; i++) d |= (uint8_t) (w[i] ^ proof[120 + i]);
    return d ? zxvi_fail(ZXV_E_VERIFY, "paradox_repair_verify: proof hash does not match")
             : zxvi_ok();
}

const char *zxv_paradox_kind_name(int32_t kind)
{
    return kind >= 0 && kind <= ZXV_PDX_KIND_NONE ? pdx_kind_name((uint8_t) kind) : "INVALID";
}

const char *zxv_paradox_outcome_name(int32_t outcome)
{
    return outcome >= 0 && outcome <= ZXV_PDX_OUT_LOG_FULL ? pdx_out_name((pdx_out_t) outcome)
                                                           : "INVALID";
}

const char *zxv_paradox_node_state_name(int32_t state)
{
    static const char *const n[10] = {"open",      "clear",   "contained", "rolled-back",
                                      "tainted",   "sandboxed", "waiting", "held",
                                      "blocked",   "applied"};
    return state >= 0 && state <= ZXV_PDX_NS_APPLIED ? n[state] : "INVALID";
}

/* ===================================================================== */
/* the book                                                              */
/* ===================================================================== */

#define BOOK_MAGIC 0x5A584250u   /* "ZXBP" */
#define POLICY_MAGIC 0x5A585050u /* "ZXPP" */
#define HOLD_MAGIC 0x5A584850u   /* "ZXHP" */
#define GRAPH_MAGIC 0x5A584750u  /* "ZXGP" */

struct zxv_paradox {
    uint32_t magic;
    zxv_ctx *ctx;
    pthread_mutex_t mu;
    pdx_log_t ops, frontier;
    lpx_qlog_t q;
    pdx_registry_t reg;
    pdx_rec_t *ops_rec, *fr_rec;
    lpx_qrec_t *q_rec;
    pdx_sig_t *ent;
};

static int32_t book_ok(const zxv_paradox *b, const char *what)
{
    if (!b || b->magic != BOOK_MAGIC)
        return zxvi_fail(ZXV_E_ARG, "%s: book is NULL or not a zxv_paradox", what);
    return zxvi_ctx_check(b->ctx, what);
}

/* Gate a writing call: book valid, cap holds PARADOX and belongs to the
 * book's context. */
static int32_t book_gate(const zxv_cap *cap, const zxv_paradox *b, const char *what)
{
    if (!b || b->magic != BOOK_MAGIC)
        return zxvi_fail(ZXV_E_ARG, "%s: book is NULL or not a zxv_paradox", what);
    return zxvi_cap_gate_ctx(cap, ZXV_CAP_PARADOX, what, b->ctx);
}

static void lock(const zxv_paradox *b)
{
    pthread_mutex_lock((pthread_mutex_t *) &b->mu);
}
static void unlock(const zxv_paradox *b)
{
    pthread_mutex_unlock((pthread_mutex_t *) &b->mu);
}

static void book_free(zxv_paradox *b)
{
    free(b->ops_rec);
    free(b->fr_rec);
    free(b->q_rec);
    free(b->ent);
    free(b);
}

int32_t zxv_paradox_create(const zxv_cap *cap, uint32_t log_capacity, uint32_t registry_capacity,
                           zxv_paradox **out)
{
    zxv_ctx *ctx;
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_PARADOX, "paradox_create", &ctx);
    if (rc) return rc;
    if (!out) return zxvi_fail(ZXV_E_ARG, "paradox_create: out is NULL");
    if (log_capacity == 0 || log_capacity > ZXV_PDX_LOG_MAX)
        return zxvi_fail(ZXV_E_ARG, "paradox_create: log_capacity %u is not 1..%u", log_capacity,
                         ZXV_PDX_LOG_MAX);
    if (registry_capacity == 0 || registry_capacity > ZXV_PDX_REGISTRY_MAX)
        return zxvi_fail(ZXV_E_ARG, "paradox_create: registry_capacity %u is not 1..%u",
                         registry_capacity, ZXV_PDX_REGISTRY_MAX);
    zxv_paradox *b = calloc(1, sizeof *b);
    if (!b) return zxvi_fail(ZXV_E_NOMEM, "paradox_create: allocation failed");
    b->ops_rec = calloc(log_capacity, sizeof(pdx_rec_t));
    b->fr_rec = calloc(log_capacity, sizeof(pdx_rec_t));
    b->q_rec = calloc(log_capacity, sizeof(lpx_qrec_t));
    b->ent = calloc(registry_capacity, sizeof(pdx_sig_t));
    if (!b->ops_rec || !b->fr_rec || !b->q_rec || !b->ent) {
        book_free(b);
        return zxvi_fail(ZXV_E_NOMEM, "paradox_create: allocation failed");
    }
    b->magic = BOOK_MAGIC;
    b->ctx = ctx;
    pthread_mutex_init(&b->mu, NULL);
    pdx_log_init(&b->ops, b->ops_rec, log_capacity);
    pdx_log_init(&b->frontier, b->fr_rec, log_capacity);
    lpx_qlog_init(&b->q, b->q_rec, log_capacity);
    pdx_reg_init(&b->reg, b->ent, registry_capacity);
    *out = b;
    return zxvi_ok();
}

void zxv_paradox_destroy(zxv_paradox *book)
{
    if (!book || book->magic != BOOK_MAGIC) return;
    book->magic = 0;
    pthread_mutex_destroy(&book->mu);
    book_free(book);
}

int32_t zxv_paradox_classify(const zxv_paradox *book, uint8_t tag, const uint32_t evidence[4],
                             int32_t stale, const uint8_t sig[32], int32_t *out_kind,
                             uint8_t *out_tag, int32_t *out_reg_index)
{
    if (book) {
        int32_t rc = book_ok(book, "paradox_classify");
        if (rc) return rc;
    }
    if (tag >= LPX_STATES) return zxvi_fail(ZXV_E_ARG, "paradox_classify: tag %u is not 0..63", tag);
    pdx_evidence_t ev;
    if (evidence) {
        ev.pos0 = evidence[0];
        ev.neg0 = evidence[1];
        ev.pos1 = evidence[2];
        ev.neg1 = evidence[3];
        ev.stale = stale != 0;
    }
    pdx_class_t c;
    if (book) lock(book);
    c = pdx_classify(tag, evidence ? &ev : NULL, book ? &book->reg : NULL, sig);
    if (book) unlock(book);
    if (out_kind) *out_kind = c.kind;
    if (out_tag) *out_tag = c.tag;
    if (out_reg_index) *out_reg_index = c.reg_index;
    return zxvi_ok();
}

/* ---- registry ---- */

int32_t zxv_paradox_registry_add(const zxv_cap *cap, zxv_paradox *book, const uint8_t sig[32],
                                 int32_t kind, uint8_t reason, uint8_t flags, int32_t owner_ok,
                                 uint32_t *out_index)
{
    int32_t rc = book_gate(cap, book, "paradox_registry_add");
    if (rc) return rc;
    if (!sig) return zxvi_fail(ZXV_E_ARG, "paradox_registry_add: sig is NULL");
    if (!registry_kind(kind))
        return zxvi_fail(ZXV_E_ARG,
                         "paradox_registry_add: kind %d is not constructive, destructive or neutral",
                         (int) kind);
    if (flags & ~FLAGS_ALL)
        return zxvi_fail(ZXV_E_ARG, "paradox_registry_add: unknown flag bits 0x%x", flags);
    if ((flags & FLAGS_ALL) && !owner_ok)
        return zxvi_fail(ZXV_E_PARADOX_OWNER,
                         "paradox_registry_add: a money or device entry needs the owner");
    lock(book);
    if (pdx_reg_find(&book->reg, sig) >= 0) {
        unlock(book);
        return zxvi_fail(ZXV_E_DUPLICATE, "paradox_registry_add: signature already registered");
    }
    if (book->reg.n >= book->reg.cap || book->ops.n >= book->ops.cap) {
        bool reg_full = book->reg.n >= book->reg.cap;
        unlock(book);
        return zxvi_fail(ZXV_E_CAPACITY, "paradox_registry_add: %s is full",
                         reg_full ? "the registry" : "the operator log");
    }
    uint32_t idx = book->reg.n;
    pdx_log_append(&book->ops, PDX_OP_REVIEW, (uint8_t) kind, 0, PDX_OUT_ACCEPTED, idx, NULL, sig);
    pdx_reg_add(&book->reg, sig, (uint8_t) kind, reason, flags, PDX_SRC_SEED);
    unlock(book);
    if (out_index) *out_index = idx;
    return zxvi_ok();
}

int32_t zxv_paradox_registry_find(const zxv_paradox *book, const uint8_t sig[32],
                                  uint32_t *out_index)
{
    int32_t rc = book_ok(book, "paradox_registry_find");
    if (rc) return rc;
    if (!sig || !out_index) return zxvi_fail(ZXV_E_ARG, "paradox_registry_find: NULL argument");
    lock(book);
    int32_t i = pdx_reg_find(&book->reg, sig);
    unlock(book);
    if (i < 0) return zxvi_fail(ZXV_E_NOT_FOUND, "paradox_registry_find: signature not registered");
    *out_index = (uint32_t) i;
    return zxvi_ok();
}

int32_t zxv_paradox_registry_count(const zxv_paradox *book, uint32_t *out_count)
{
    int32_t rc = book_ok(book, "paradox_registry_count");
    if (rc) return rc;
    if (!out_count) return zxvi_fail(ZXV_E_ARG, "paradox_registry_count: out_count is NULL");
    lock(book);
    *out_count = book->reg.n;
    unlock(book);
    return zxvi_ok();
}

int32_t zxv_paradox_registry_at(const zxv_paradox *book, uint32_t index, uint8_t out_sig[32],
                                int32_t *out_kind, uint8_t *out_reason, uint8_t *out_flags,
                                uint64_t *out_src)
{
    int32_t rc = book_ok(book, "paradox_registry_at");
    if (rc) return rc;
    lock(book);
    if (index >= book->reg.n) {
        uint32_t n = book->reg.n;
        unlock(book);
        return zxvi_fail(ZXV_E_NOT_FOUND, "paradox_registry_at: index %u, registry holds %u",
                         index, n);
    }
    const pdx_sig_t *e = &book->reg.ent[index];
    if (out_sig) memcpy(out_sig, e->sig, 32);
    if (out_kind) *out_kind = e->kind;
    if (out_reason) *out_reason = e->reason;
    if (out_flags) *out_flags = e->flags;
    if (out_src) *out_src = e->src;
    unlock(book);
    return zxvi_ok();
}

/* ---- logs ---- */

static int32_t which_ok(int32_t which, const char *what)
{
    if (which < ZXV_PDX_LOG_OPS || which > ZXV_PDX_LOG_QUARANTINE)
        return zxvi_fail(ZXV_E_ARG, "%s: log %d is not a ZXV_PDX_LOG_* value", what, (int) which);
    return ZXV_OK;
}

static const pdx_log_t *plog(const zxv_paradox *b, int32_t which)
{
    return which == ZXV_PDX_LOG_OPS ? &b->ops : &b->frontier;
}

int32_t zxv_paradox_log_count(const zxv_paradox *book, int32_t which, uint32_t *out_count)
{
    int32_t rc = book_ok(book, "paradox_log_count");
    if (rc || (rc = which_ok(which, "paradox_log_count"))) return rc;
    if (!out_count) return zxvi_fail(ZXV_E_ARG, "paradox_log_count: out_count is NULL");
    lock(book);
    *out_count = which == ZXV_PDX_LOG_QUARANTINE ? book->q.n : plog(book, which)->n;
    unlock(book);
    return zxvi_ok();
}

int32_t zxv_paradox_log_head(const zxv_paradox *book, int32_t which, uint8_t out_head[32])
{
    int32_t rc = book_ok(book, "paradox_log_head");
    if (rc || (rc = which_ok(which, "paradox_log_head"))) return rc;
    if (!out_head) return zxvi_fail(ZXV_E_ARG, "paradox_log_head: out_head is NULL");
    lock(book);
    memcpy(out_head, which == ZXV_PDX_LOG_QUARANTINE ? book->q.head : plog(book, which)->head, 32);
    unlock(book);
    return zxvi_ok();
}

int32_t zxv_paradox_log_record(const zxv_paradox *book, int32_t which, uint32_t index,
                               uint8_t *buf, size_t cap, size_t *out_len)
{
    int32_t rc = book_ok(book, "paradox_log_record");
    if (rc || (rc = which_ok(which, "paradox_log_record"))) return rc;
    if (!out_len) return zxvi_fail(ZXV_E_ARG, "paradox_log_record: out_len is NULL");
    bool quar = which == ZXV_PDX_LOG_QUARANTINE;
    size_t need = quar ? ZXV_PDX_QREC_BYTES : ZXV_PDX_REC_BYTES;
    uint8_t tmp[ZXV_PDX_REC_BYTES];
    lock(book);
    uint32_t n = quar ? book->q.n : plog(book, which)->n;
    if (index >= n) {
        unlock(book);
        return zxvi_fail(ZXV_E_NOT_FOUND, "paradox_log_record: index %u, log holds %u", index, n);
    }
    if (quar) {
        const lpx_qrec_t *r = &book->q.rec[index];
        le64(tmp, r->seq);
        tmp[8] = r->tag;
        tmp[9] = r->reason;
        le32(tmp + 10, r->submitter);
        memcpy(tmp + 14, r->subject, 32);
        memcpy(tmp + 46, r->prev, 32);
        memcpy(tmp + 78, r->hash, 32);
    } else {
        const pdx_rec_t *r = &plog(book, which)->rec[index];
        le64(tmp, r->seq);
        tmp[8] = r->op;
        tmp[9] = r->kind;
        tmp[10] = r->tag;
        tmp[11] = r->outcome;
        le32(tmp + 12, r->aux);
        memcpy(tmp + 16, r->a, 32);
        memcpy(tmp + 48, r->b, 32);
        memcpy(tmp + 80, r->prev, 32);
        memcpy(tmp + 112, r->hash, 32);
    }
    unlock(book);
    *out_len = need;
    if (!buf || cap < need)
        return zxvi_fail(ZXV_E_BUFFER, "paradox_log_record: needs %zu bytes", need);
    memcpy(buf, tmp, need);
    return zxvi_ok();
}

int32_t zxv_paradox_log_verify(zxv_paradox *book, int32_t which)
{
    int32_t rc = book_ok(book, "paradox_log_verify");
    if (rc || (rc = which_ok(which, "paradox_log_verify"))) return rc;
    lock(book);
    bool ok = which == ZXV_PDX_LOG_QUARANTINE ? lpx_qlog_verify(&book->q)
                                              : pdx_log_verify(plog(book, which));
    unlock(book);
    if (ok) return zxvi_ok();
    if ((rc = zxvi_ctx_enter(book->ctx))) return rc;
    rc = zxvi_ctx_poison(book->ctx, "paradox log chain does not verify");
    zxvi_ctx_leave(book->ctx);
    return rc;
}

/* ===================================================================== */
/* constructive                                                          */
/* ===================================================================== */

struct zxv_paradox_policy {
    uint32_t magic;
    zxv_ctx *ctx;
    uint32_t flags;
    pdx_params_t p;
    int64_t val[ZXV_PDX_POLICY_MAX], prev[ZXV_PDX_POLICY_MAX];
    int64_t lo[ZXV_PDX_POLICY_MAX], hi[ZXV_PDX_POLICY_MAX];
};

static int32_t policy_ok(const zxv_paradox_policy *p, const char *what)
{
    if (!p || p->magic != POLICY_MAGIC)
        return zxvi_fail(ZXV_E_ARG, "%s: policy is NULL or not a zxv_paradox_policy", what);
    return zxvi_ctx_check(p->ctx, what);
}

int32_t zxv_paradox_policy_create(const zxv_cap *cap, uint32_t n, const int64_t *lo,
                                  const int64_t *hi, const int64_t *initial, uint32_t flags,
                                  zxv_paradox_policy **out)
{
    zxv_ctx *ctx;
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_PARADOX, "paradox_policy_create", &ctx);
    if (rc) return rc;
    if (!out || !lo || !hi || !initial)
        return zxvi_fail(ZXV_E_ARG, "paradox_policy_create: NULL argument");
    if (n == 0 || n > ZXV_PDX_POLICY_MAX)
        return zxvi_fail(ZXV_E_ARG, "paradox_policy_create: n %u is not 1..%u", n,
                         ZXV_PDX_POLICY_MAX);
    if (flags & ~FLAGS_ALL)
        return zxvi_fail(ZXV_E_ARG, "paradox_policy_create: unknown flag bits 0x%x", flags);
    for (uint32_t i = 0; i < n; i++)
        if (lo[i] > hi[i] || initial[i] < lo[i] || initial[i] > hi[i])
            return zxvi_fail(ZXV_E_RANGE,
                             "paradox_policy_create: entry %u: need lo <= initial <= hi", i);
    zxv_paradox_policy *p = calloc(1, sizeof *p);
    if (!p) return zxvi_fail(ZXV_E_NOMEM, "paradox_policy_create: allocation failed");
    p->magic = POLICY_MAGIC;
    p->ctx = ctx;
    p->flags = flags;
    memcpy(p->val, initial, n * sizeof(int64_t));
    memcpy(p->prev, initial, n * sizeof(int64_t));
    memcpy(p->lo, lo, n * sizeof(int64_t));
    memcpy(p->hi, hi, n * sizeof(int64_t));
    p->p.val = p->val;
    p->p.prev = p->prev;
    p->p.lo = p->lo;
    p->p.hi = p->hi;
    p->p.n = n;
    p->p.armed = false;
    p->p.tension_at = 0;
    *out = p;
    return zxvi_ok();
}

void zxv_paradox_policy_destroy(zxv_paradox_policy *policy)
{
    if (!policy || policy->magic != POLICY_MAGIC) return;
    policy->magic = 0;
    free(policy);
}

/* Bounds no value can meet (lo > hi): the kernel then makes any proposal
 * a PROPOSAL_FOR_OWNER and changes nothing. Used for money/device
 * policies, which are never changed automatically. */

int32_t zxv_paradox_constructive(const zxv_cap *cap, zxv_paradox *book, zxv_paradox_policy *policy,
                                 int32_t kind, uint8_t tag, const int64_t *synth, uint32_t n,
                                 uint32_t tension, int32_t *out_outcome)
{
    int32_t rc = book_gate(cap, book, "paradox_constructive");
    if (rc) return rc;
    if (!policy || policy->magic != POLICY_MAGIC || !out_outcome)
        return zxvi_fail(ZXV_E_ARG, "paradox_constructive: NULL or invalid argument");
    if (policy->ctx != book->ctx)
        return zxvi_fail(ZXV_E_ARG, "paradox_constructive: policy and book are from different contexts");
    pdx_class_t c;
    if ((rc = mk_class(kind, tag, &c, "paradox_constructive"))) return rc;
    if (synth && n != policy->p.n)
        return zxvi_fail(ZXV_E_ARG, "paradox_constructive: n %u, policy has %u", n, policy->p.n);
    pdx_params_t view = policy->p;
    int64_t no_lo[ZXV_PDX_POLICY_MAX], no_hi[ZXV_PDX_POLICY_MAX];
    if (policy->flags & FLAGS_ALL) {
        for (uint32_t i = 0; i < ZXV_PDX_POLICY_MAX; i++) {
            no_lo[i] = INT64_MAX;
            no_hi[i] = INT64_MIN;
        }
        view.lo = no_lo;
        view.hi = no_hi;
    }
    lock(book);
    pdx_out_t o = pdx_constructive(&view, synth, tension, &c, &book->ops);
    unlock(book);
    policy->p.armed = view.armed;
    policy->p.tension_at = view.tension_at;
    if (o == PDX_OUT_LOG_FULL)
        return zxvi_fail(ZXV_E_CAPACITY, "paradox_constructive: operator log is full; halt");
    *out_outcome = (int32_t) o;
    return zxvi_ok();
}

int32_t zxv_paradox_policy_values(const zxv_paradox_policy *policy, int64_t *out_val,
                                  int64_t *out_prev, uint32_t n, int32_t *out_armed,
                                  uint32_t *out_tension_at)
{
    int32_t rc = policy_ok(policy, "paradox_policy_values");
    if (rc) return rc;
    if ((out_val || out_prev) && n != policy->p.n)
        return zxvi_fail(ZXV_E_ARG, "paradox_policy_values: n %u, policy has %u", n, policy->p.n);
    if (out_val) memcpy(out_val, policy->val, n * sizeof(int64_t));
    if (out_prev) memcpy(out_prev, policy->prev, n * sizeof(int64_t));
    if (out_armed) *out_armed = policy->p.armed ? 1 : 0;
    if (out_tension_at) *out_tension_at = policy->p.tension_at;
    return zxvi_ok();
}

int32_t zxv_paradox_policy_bounds(const zxv_paradox_policy *policy, int64_t *out_lo,
                                  int64_t *out_hi, uint32_t n, uint32_t *out_flags)
{
    int32_t rc = policy_ok(policy, "paradox_policy_bounds");
    if (rc) return rc;
    if ((out_lo || out_hi) && n != policy->p.n)
        return zxvi_fail(ZXV_E_ARG, "paradox_policy_bounds: n %u, policy has %u", n, policy->p.n);
    if (out_lo) memcpy(out_lo, policy->lo, n * sizeof(int64_t));
    if (out_hi) memcpy(out_hi, policy->hi, n * sizeof(int64_t));
    if (out_flags) *out_flags = policy->flags;
    return zxvi_ok();
}

int32_t zxv_paradox_policy_proposal(const zxv_paradox_policy *policy, const int64_t *synth,
                                    uint32_t n, uint8_t *buf, size_t cap, size_t *out_len)
{
    int32_t rc = policy_ok(policy, "paradox_policy_proposal");
    if (rc) return rc;
    if (!synth || !out_len) return zxvi_fail(ZXV_E_ARG, "paradox_policy_proposal: NULL argument");
    if (n != policy->p.n)
        return zxvi_fail(ZXV_E_ARG, "paradox_policy_proposal: n %u, policy has %u", n, policy->p.n);
    size_t need = ZXV_PDX_PROPOSAL_BYTES(n);
    *out_len = need;
    if (!buf || cap < need)
        return zxvi_fail(ZXV_E_BUFFER, "paradox_policy_proposal: needs %zu bytes", need);
    uint32_t bad = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (synth[i] < policy->lo[i] || synth[i] > policy->hi[i]) bad++;
        uint8_t *e = buf + 12 + 32 * i;
        le64(e, (uint64_t) policy->lo[i]);
        le64(e + 8, (uint64_t) policy->hi[i]);
        le64(e + 16, (uint64_t) policy->val[i]);
        le64(e + 24, (uint64_t) synth[i]);
    }
    le32(buf, n);
    le32(buf + 4, bad);
    le32(buf + 8, policy->flags);
    return zxvi_ok();
}

/* ===================================================================== */
/* destructive                                                           */
/* ===================================================================== */

static bool invariant_cb(const uint8_t *state, uint32_t len, void *ctx)
{
    (void) state;
    (void) len;
    return *(const int32_t *) ctx != 0;
}

int32_t zxv_paradox_destructive(const zxv_cap *cap, zxv_paradox *book, int32_t kind, uint8_t tag,
                                uint32_t steps, uint32_t step_limit, uint32_t spent,
                                uint32_t budget, const uint64_t *trace, uint32_t trace_n,
                                int32_t invariant_ok, uint8_t *state, const uint8_t *checkpoint,
                                uint32_t len, const uint8_t *pattern, uint32_t pattern_len,
                                uint32_t submitter, int32_t *out_outcome, uint8_t out_proof[160])
{
    int32_t rc = book_gate(cap, book, "paradox_destructive");
    if (rc) return rc;
    if (!out_outcome || !out_proof || (len && (!state || !checkpoint)) ||
        (pattern_len && !pattern) || (trace_n && !trace))
        return zxvi_fail(ZXV_E_ARG, "paradox_destructive: NULL argument");
    if (trace_n > ZXV_PDX_TRACE_MAX)
        return zxvi_fail(ZXV_E_RANGE, "paradox_destructive: trace_n %u exceeds %u", trace_n,
                         ZXV_PDX_TRACE_MAX);
    pdx_class_t c;
    if ((rc = mk_class(kind, tag, &c, "paradox_destructive"))) return rc;
    uint8_t dummy = 0;
    int32_t inv = invariant_ok;
    pdx_watch_t w = {steps, step_limit, spent, budget, trace, trace_n, invariant_cb, &inv};
    pdx_repair_t pr;
    memset(&pr, 0, sizeof pr);
    lock(book);
    pdx_out_t o = pdx_destructive(&c, &w, len ? state : &dummy, len ? checkpoint : &dummy, len,
                                  pattern, pattern_len, submitter, &book->q, &book->ops, &pr);
    unlock(book);
    if (o == PDX_OUT_LOG_FULL)
        return zxvi_fail(ZXV_E_CAPACITY,
                         "paradox_destructive: a log is full; state was rolled back; halt");
    if (o == PDX_OUT_CONTAINED) {
        le32(out_proof, pr.trips);
        le32(out_proof + 4, pr.steps);
        le32(out_proof + 8, spent);
        le32(out_proof + 12, trace_n);
        le32(out_proof + 16, submitter);
        le32(out_proof + 20, len);
        memcpy(out_proof + 24, pr.before, 32);
        memcpy(out_proof + 56, pr.after, 32);
        memcpy(out_proof + 88, pr.sig, 32);
        memcpy(out_proof + 120, pr.what, 32);
        le64(out_proof + 152, pr.qseq);
    }
    *out_outcome = (int32_t) o;
    return zxvi_ok();
}

/* ===================================================================== */
/* neutral                                                               */
/* ===================================================================== */

struct zxv_paradox_hold {
    uint32_t magic;
    zxv_ctx *ctx;
    pdx_hold_t h;
    pdx_branch_t br[ZXV_PDX_HOLD_MAX];
};

static int32_t hold_ok(const zxv_paradox_hold *h, const char *what)
{
    if (!h || h->magic != HOLD_MAGIC)
        return zxvi_fail(ZXV_E_ARG, "%s: hold is NULL or not a zxv_paradox_hold", what);
    return zxvi_ctx_check(h->ctx, what);
}

int32_t zxv_paradox_hold_create(const zxv_cap *cap, uint32_t capacity, uint32_t decisive,
                                zxv_paradox_hold **out)
{
    zxv_ctx *ctx;
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_PARADOX, "paradox_hold_create", &ctx);
    if (rc) return rc;
    if (!out) return zxvi_fail(ZXV_E_ARG, "paradox_hold_create: out is NULL");
    if (capacity == 0 || capacity > ZXV_PDX_HOLD_MAX)
        return zxvi_fail(ZXV_E_ARG, "paradox_hold_create: capacity %u is not 1..%u", capacity,
                         ZXV_PDX_HOLD_MAX);
    zxv_paradox_hold *h = calloc(1, sizeof *h);
    if (!h) return zxvi_fail(ZXV_E_NOMEM, "paradox_hold_create: allocation failed");
    h->magic = HOLD_MAGIC;
    h->ctx = ctx;
    pdx_hold_init(&h->h, h->br, capacity, decisive);
    *out = h;
    return zxvi_ok();
}

void zxv_paradox_hold_destroy(zxv_paradox_hold *hold)
{
    if (!hold || hold->magic != HOLD_MAGIC) return;
    hold->magic = 0;
    free(hold);
}

static int32_t hold_find(const pdx_hold_t *h, const uint8_t id[32])
{
    for (uint32_t i = 0; i < h->n; i++)
        if (!memcmp(h->br[i].id, id, 32)) return (int32_t) i;
    return -1;
}

int32_t zxv_paradox_hold_add(zxv_paradox_hold *hold, const uint8_t id[32])
{
    int32_t rc = hold_ok(hold, "paradox_hold_add");
    if (rc) return rc;
    if (!id) return zxvi_fail(ZXV_E_ARG, "paradox_hold_add: id is NULL");
    if (hold->h.settled >= 0) return zxvi_fail(ZXV_E_STATE, "paradox_hold_add: already settled");
    if (hold_find(&hold->h, id) < 0 && hold->h.n >= hold->h.cap)
        return zxvi_fail(ZXV_E_CAPACITY, "paradox_hold_add: hold is full (%u)", hold->h.cap);
    if (!pdx_hold_add(&hold->h, id))
        return zxvi_fail(ZXV_E_INTERNAL, "paradox_hold_add: kernel refused");
    return zxvi_ok();
}

int32_t zxv_paradox_hold_evidence(zxv_paradox_hold *hold, const uint8_t id[32], int32_t positive)
{
    int32_t rc = hold_ok(hold, "paradox_hold_evidence");
    if (rc) return rc;
    if (!id) return zxvi_fail(ZXV_E_ARG, "paradox_hold_evidence: id is NULL");
    if (hold->h.settled >= 0)
        return zxvi_fail(ZXV_E_STATE, "paradox_hold_evidence: already settled");
    if (!pdx_hold_evidence(&hold->h, id, positive != 0))
        return zxvi_fail(ZXV_E_NOT_FOUND, "paradox_hold_evidence: no such branch");
    return zxvi_ok();
}

int32_t zxv_paradox_hold_info(const zxv_paradox_hold *hold, uint32_t *out_count,
                              int32_t *out_settled, int32_t *out_decisive)
{
    int32_t rc = hold_ok(hold, "paradox_hold_info");
    if (rc) return rc;
    if (out_count) *out_count = hold->h.n;
    if (out_settled) *out_settled = hold->h.settled;
    if (out_decisive) *out_decisive = pdx_hold_decisive(&hold->h);
    return zxvi_ok();
}

int32_t zxv_paradox_hold_branch(const zxv_paradox_hold *hold, uint32_t index, uint8_t out_id[32],
                                uint32_t *out_pos, uint32_t *out_neg)
{
    int32_t rc = hold_ok(hold, "paradox_hold_branch");
    if (rc) return rc;
    if (index >= hold->h.n)
        return zxvi_fail(ZXV_E_NOT_FOUND, "paradox_hold_branch: index %u, hold has %u", index,
                         hold->h.n);
    const pdx_branch_t *b = &hold->h.br[index];
    if (out_id) memcpy(out_id, b->id, 32);
    if (out_pos) *out_pos = b->pos;
    if (out_neg) *out_neg = b->neg;
    return zxvi_ok();
}

int32_t zxv_paradox_neutral(const zxv_cap *cap, zxv_paradox *book, zxv_paradox_hold *hold,
                            int32_t kind, uint8_t tag, int32_t owner_pick, int32_t *out_outcome,
                            int32_t *out_settled)
{
    int32_t rc = book_gate(cap, book, "paradox_neutral");
    if (rc) return rc;
    if (!hold || hold->magic != HOLD_MAGIC || !out_outcome)
        return zxvi_fail(ZXV_E_ARG, "paradox_neutral: NULL or invalid argument");
    if (hold->ctx != book->ctx)
        return zxvi_fail(ZXV_E_ARG, "paradox_neutral: hold and book are from different contexts");
    pdx_class_t c;
    if ((rc = mk_class(kind, tag, &c, "paradox_neutral"))) return rc;
    if (owner_pick < -1) return zxvi_fail(ZXV_E_ARG, "paradox_neutral: owner_pick %d", (int) owner_pick);
    if (owner_pick >= 0 && (uint32_t) owner_pick >= hold->h.n)
        return zxvi_fail(ZXV_E_NOT_FOUND, "paradox_neutral: owner_pick %d, hold has %u",
                         (int) owner_pick, hold->h.n);
    lock(book);
    pdx_out_t o = pdx_neutral(&hold->h, &c, owner_pick, &book->ops);
    unlock(book);
    if (o == PDX_OUT_LOG_FULL)
        return zxvi_fail(ZXV_E_CAPACITY, "paradox_neutral: operator log is full; halt");
    *out_outcome = (int32_t) o;
    if (out_settled) *out_settled = hold->h.settled;
    return zxvi_ok();
}

/* ===================================================================== */
/* special                                                               */
/* ===================================================================== */

int32_t zxv_paradox_special(const zxv_cap *cap, zxv_paradox *book, int32_t kind, uint8_t tag,
                            const uint8_t sig[32], const uint8_t subject[32], int32_t *out_outcome,
                            uint32_t *out_rec)
{
    int32_t rc = book_gate(cap, book, "paradox_special");
    if (rc) return rc;
    if (!sig || !subject || !out_outcome)
        return zxvi_fail(ZXV_E_ARG, "paradox_special: NULL argument");
    pdx_class_t c;
    if ((rc = mk_class(kind, tag, &c, "paradox_special"))) return rc;
    uint32_t rec = UINT32_MAX;
    lock(book);
    pdx_out_t o = pdx_special(&book->frontier, &c, sig, subject, &book->ops, &rec);
    unlock(book);
    if (o == PDX_OUT_LOG_FULL)
        return zxvi_fail(ZXV_E_CAPACITY, "paradox_special: a log is full; halt");
    *out_outcome = (int32_t) o;
    if (out_rec) *out_rec = rec; /* the kernel sets it only when RECORDED */
    return zxvi_ok();
}

int32_t zxv_paradox_draft(const zxv_cap *cap, zxv_paradox *book, uint32_t rec, int32_t kind,
                          uint8_t reason, uint8_t flags, uint8_t out_draft[71])
{
    int32_t rc = book_gate(cap, book, "paradox_draft");
    if (rc) return rc;
    if (!out_draft) return zxvi_fail(ZXV_E_ARG, "paradox_draft: out_draft is NULL");
    if (!registry_kind(kind))
        return zxvi_fail(ZXV_E_ARG,
                         "paradox_draft: kind %d is not constructive, destructive or neutral",
                         (int) kind);
    if (flags & ~FLAGS_ALL)
        return zxvi_fail(ZXV_E_ARG, "paradox_draft: unknown flag bits 0x%x", flags);
    pdx_draft_t d;
    lock(book);
    if (rec >= book->frontier.n) {
        uint32_t n = book->frontier.n;
        unlock(book);
        return zxvi_fail(ZXV_E_NOT_FOUND, "paradox_draft: record %u, frontier holds %u", rec, n);
    }
    pdx_out_t o = pdx_draft(&book->frontier, rec, (uint8_t) kind, reason, flags, &d, &book->ops);
    unlock(book);
    if (o == PDX_OUT_LOG_FULL)
        return zxvi_fail(ZXV_E_CAPACITY, "paradox_draft: operator log is full; halt");
    if (o != PDX_OUT_RECORDED)
        return zxvi_fail(ZXV_E_INTERNAL, "paradox_draft: kernel returned %s", pdx_out_name(o));
    le32(out_draft, d.rec);
    memcpy(out_draft + 4, d.rec_hash, 32);
    memcpy(out_draft + 36, d.sig, 32);
    out_draft[68] = d.kind;
    out_draft[69] = d.reason;
    out_draft[70] = d.flags;
    return zxvi_ok();
}

int32_t zxv_paradox_review(const zxv_cap *cap, zxv_paradox *book, const uint8_t draft[71],
                           int32_t owner_ok, int32_t consensus_ok, int32_t *out_outcome)
{
    int32_t rc = book_gate(cap, book, "paradox_review");
    if (rc) return rc;
    if (!draft || !out_outcome) return zxvi_fail(ZXV_E_ARG, "paradox_review: NULL argument");
    pdx_draft_t d;
    d.rec = rd32(draft);
    memcpy(d.rec_hash, draft + 4, 32);
    memcpy(d.sig, draft + 36, 32);
    d.kind = draft[68];
    d.reason = draft[69];
    d.flags = draft[70];
    if (!registry_kind(d.kind) || (d.flags & ~FLAGS_ALL))
        return zxvi_fail(ZXV_E_FORMAT, "paradox_review: bad kind %u or flags 0x%x in the draft",
                         d.kind, d.flags);
    lock(book);
    if (d.rec >= book->frontier.n) {
        uint32_t n = book->frontier.n;
        unlock(book);
        return zxvi_fail(ZXV_E_NOT_FOUND, "paradox_review: record %u, frontier holds %u", d.rec, n);
    }
    if (!pdx_log_verify(&book->frontier)) {
        unlock(book);
        if ((rc = zxvi_ctx_enter(book->ctx))) return rc;
        rc = zxvi_ctx_poison(book->ctx, "paradox frontier chain does not verify");
        zxvi_ctx_leave(book->ctx);
        return rc;
    }
    const pdx_rec_t *r = &book->frontier.rec[d.rec];
    if (memcmp(r->hash, d.rec_hash, 32) || memcmp(r->a, d.sig, 32)) {
        unlock(book);
        return zxvi_fail(ZXV_E_VERIFY, "paradox_review: draft does not match frontier record %u",
                         d.rec);
    }
    pdx_out_t o = pdx_review(&book->reg, &book->frontier, &d, owner_ok != 0, consensus_ok != 0,
                             &book->ops);
    unlock(book);
    if (o == PDX_OUT_LOG_FULL)
        return zxvi_fail(ZXV_E_CAPACITY, "paradox_review: operator log is full; halt");
    *out_outcome = (int32_t) o;
    return zxvi_ok();
}

/* ===================================================================== */
/* complex paradoxes                                                     */
/* ===================================================================== */

struct zxv_paradox_graph {
    uint32_t magic;
    zxv_ctx *ctx;
    uint32_t step_limit;
    bool loaded, escalated;
    uint32_t n, nupd;
    pdx_node_t nodes[PDX_GRAPH_MAX];
    pdx_update_t upd[ZXV_PDX_UPDATES_MAX];
    pdx_graph_t g;
};

static int32_t graph_ok(const zxv_paradox_graph *g, const char *what)
{
    if (!g || g->magic != GRAPH_MAGIC)
        return zxvi_fail(ZXV_E_ARG, "%s: graph is NULL or not a zxv_paradox_graph", what);
    return zxvi_ctx_check(g->ctx, what);
}

static int32_t node_find(const zxv_paradox_graph *g, const uint8_t id[32])
{
    for (uint32_t i = 0; i < g->n; i++)
        if (!memcmp(g->nodes[i].id, id, 32)) return (int32_t) i;
    return -1;
}

static int32_t tension_ok(int32_t dir, const char *what)
{
    if (dir < -1 || dir > 1)
        return zxvi_fail(ZXV_E_ARG, "%s: direction %d is not -1, 0 or +1", what, (int) dir);
    return ZXV_OK;
}

int32_t zxv_paradox_graph_create(const zxv_cap *cap, uint32_t step_limit, zxv_paradox_graph **out)
{
    zxv_ctx *ctx;
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_PARADOX, "paradox_graph_create", &ctx);
    if (rc) return rc;
    if (!out) return zxvi_fail(ZXV_E_ARG, "paradox_graph_create: out is NULL");
    if (step_limit == 0) return zxvi_fail(ZXV_E_ARG, "paradox_graph_create: step_limit is 0");
    zxv_paradox_graph *g = calloc(1, sizeof *g);
    if (!g) return zxvi_fail(ZXV_E_NOMEM, "paradox_graph_create: allocation failed");
    g->magic = GRAPH_MAGIC;
    g->ctx = ctx;
    g->step_limit = step_limit;
    *out = g;
    return zxvi_ok();
}

void zxv_paradox_graph_destroy(zxv_paradox_graph *g)
{
    if (!g || g->magic != GRAPH_MAGIC) return;
    g->magic = 0;
    free(g);
}

int32_t zxv_paradox_graph_add_node(zxv_paradox_graph *g, const uint8_t id[32], int32_t kind,
                                   uint8_t tag, uint32_t mag, int32_t dir)
{
    int32_t rc = graph_ok(g, "paradox_graph_add_node");
    if (rc) return rc;
    if (!id) return zxvi_fail(ZXV_E_ARG, "paradox_graph_add_node: id is NULL");
    pdx_class_t c;
    if ((rc = mk_class(kind, tag, &c, "paradox_graph_add_node")) ||
        (rc = tension_ok(dir, "paradox_graph_add_node")))
        return rc;
    if (g->loaded)
        return zxvi_fail(ZXV_E_STATE, "paradox_graph_add_node: the graph has run a pass");
    if (node_find(g, id) >= 0)
        return zxvi_fail(ZXV_E_DUPLICATE, "paradox_graph_add_node: id already present");
    if (g->n >= PDX_GRAPH_MAX)
        return zxvi_fail(ZXV_E_CAPACITY, "paradox_graph_add_node: graph is full (%u)",
                         PDX_GRAPH_MAX);
    pdx_node_t *x = &g->nodes[g->n++];
    memset(x, 0, sizeof *x);
    memcpy(x->id, id, 32);
    x->kind = c.kind;
    x->forbidden = c.forbidden;
    x->t.mag = mag;
    x->t.dir = (int8_t) dir;
    return zxvi_ok();
}

int32_t zxv_paradox_graph_add_dep(zxv_paradox_graph *g, const uint8_t id[32], const uint8_t dep[32])
{
    int32_t rc = graph_ok(g, "paradox_graph_add_dep");
    if (rc) return rc;
    if (!id || !dep) return zxvi_fail(ZXV_E_ARG, "paradox_graph_add_dep: NULL argument");
    if (!memcmp(id, dep, 32))
        return zxvi_fail(ZXV_E_ARG, "paradox_graph_add_dep: a node cannot depend on itself");
    if (g->loaded) return zxvi_fail(ZXV_E_STATE, "paradox_graph_add_dep: the graph has run a pass");
    int32_t i = node_find(g, id);
    if (i < 0) return zxvi_fail(ZXV_E_NOT_FOUND, "paradox_graph_add_dep: no node with that id");
    pdx_node_t *x = &g->nodes[i];
    for (uint32_t d = 0; d < x->ndeps; d++)
        if (!memcmp(x->deps[d], dep, 32)) return zxvi_ok();
    if (x->ndeps >= PDX_MAX_DEPS)
        return zxvi_fail(ZXV_E_CAPACITY, "paradox_graph_add_dep: node has %u dependencies",
                         PDX_MAX_DEPS);
    memcpy(x->deps[x->ndeps++], dep, 32);
    return zxvi_ok();
}

int32_t zxv_paradox_graph_update(zxv_paradox_graph *g, const uint8_t id[32], int32_t kind,
                                 uint8_t tag, uint32_t mag, int32_t dir)
{
    int32_t rc = graph_ok(g, "paradox_graph_update");
    if (rc) return rc;
    if (!id) return zxvi_fail(ZXV_E_ARG, "paradox_graph_update: id is NULL");
    pdx_class_t c;
    if ((rc = mk_class(kind, tag, &c, "paradox_graph_update")) ||
        (rc = tension_ok(dir, "paradox_graph_update")))
        return rc;
    if (node_find(g, id) < 0)
        return zxvi_fail(ZXV_E_NOT_FOUND, "paradox_graph_update: no node with that id");
    if (g->nupd >= ZXV_PDX_UPDATES_MAX)
        return zxvi_fail(ZXV_E_CAPACITY, "paradox_graph_update: %u updates queued",
                         ZXV_PDX_UPDATES_MAX);
    pdx_update_t *u = &g->upd[g->nupd++];
    memset(u, 0, sizeof *u);
    memcpy(u->id, id, 32);
    u->kind = c.kind;
    u->forbidden = c.forbidden;
    u->t.mag = mag;
    u->t.dir = (int8_t) dir;
    return zxvi_ok();
}

int32_t zxv_paradox_graph_pass(const zxv_cap *cap, zxv_paradox *book, zxv_paradox_graph *g,
                               int32_t *out_outcome)
{
    int32_t rc = book_gate(cap, book, "paradox_graph_pass");
    if (rc) return rc;
    if (!g || g->magic != GRAPH_MAGIC || !out_outcome)
        return zxvi_fail(ZXV_E_ARG, "paradox_graph_pass: NULL or invalid argument");
    if (g->ctx != book->ctx)
        return zxvi_fail(ZXV_E_ARG, "paradox_graph_pass: graph and book are from different contexts");
    if (!g->loaded) {
        for (uint32_t i = 0; i < g->n; i++)
            for (uint32_t d = 0; d < g->nodes[i].ndeps; d++)
                if (node_find(g, g->nodes[i].deps[d]) < 0)
                    return zxvi_fail(ZXV_E_NOT_FOUND,
                                     "paradox_graph_pass: node %u depends on an id that is not "
                                     "in the graph",
                                     i);
    }
    lock(book);
    if (book->ops.n >= book->ops.cap) {
        unlock(book);
        return zxvi_fail(ZXV_E_CAPACITY, "paradox_graph_pass: operator log is full; halt");
    }
    if (!g->loaded) {
        if (!pdx_graph_load(&g->g, g->nodes, g->n, g->step_limit)) {
            unlock(book);
            return zxvi_fail(ZXV_E_INTERNAL, "paradox_graph_pass: kernel refused the graph");
        }
        g->loaded = true;
    }
    if (g->nupd) pdx_graph_update(&g->g, g->upd, g->nupd); /* ids checked when queued */
    g->nupd = 0;
    pdx_out_t o = pdx_graph_pass(&g->g, &book->ops);
    unlock(book);
    if (o == PDX_OUT_ESCALATE_TO_OWNER) {
        g->escalated = true;
        return zxvi_fail(ZXV_E_PARADOX_STEP,
                         "paradox_graph_pass: step limit %u reached unresolved; escalated to the "
                         "owner",
                         g->step_limit);
    }
    if (o != PDX_OUT_RESOLVED && o != PDX_OUT_PENDING)
        return zxvi_fail(ZXV_E_INTERNAL, "paradox_graph_pass: kernel returned %s", pdx_out_name(o));
    *out_outcome = (int32_t) o;
    return zxvi_ok();
}

int32_t zxv_paradox_graph_state(const zxv_paradox_graph *g, const uint8_t id[32],
                                int32_t *out_state)
{
    int32_t rc = graph_ok(g, "paradox_graph_state");
    if (rc) return rc;
    if (!id || !out_state) return zxvi_fail(ZXV_E_ARG, "paradox_graph_state: NULL argument");
    int32_t s = g->loaded ? pdx_graph_state(&g->g, id) : (node_find(g, id) >= 0 ? PDX_NS_OPEN : -1);
    if (s < 0) return zxvi_fail(ZXV_E_NOT_FOUND, "paradox_graph_state: no node with that id");
    *out_state = s;
    return zxvi_ok();
}

int32_t zxv_paradox_graph_node_at(const zxv_paradox_graph *g, uint32_t index, uint8_t out_id[32],
                                  int32_t *out_kind, int32_t *out_state)
{
    int32_t rc = graph_ok(g, "paradox_graph_node_at");
    if (rc) return rc;
    if (!g->loaded)
        return zxvi_fail(ZXV_E_STATE, "paradox_graph_node_at: no pass has run (no id order yet)");
    if (index >= g->n)
        return zxvi_fail(ZXV_E_NOT_FOUND, "paradox_graph_node_at: index %u, graph has %u", index,
                         g->n);
    if (out_id) memcpy(out_id, g->nodes[g->g.ord[index]].id, 32);
    if (out_kind) *out_kind = g->g.kind[index];
    if (out_state) *out_state = g->g.st[index];
    return zxvi_ok();
}

int32_t zxv_paradox_graph_info(const zxv_paradox_graph *g, uint32_t *out_nodes, uint32_t *out_pass,
                               uint32_t *out_total_mag, int32_t *out_total_dir,
                               int32_t *out_escalated, uint8_t out_chain[32])
{
    int32_t rc = graph_ok(g, "paradox_graph_info");
    if (rc) return rc;
    if (out_nodes) *out_nodes = g->n;
    if (out_pass) *out_pass = g->loaded ? g->g.pass : 0;
    if (out_total_mag) *out_total_mag = g->loaded ? g->g.total.mag : 0;
    if (out_total_dir) *out_total_dir = g->loaded ? g->g.total.dir : 0;
    if (out_escalated) *out_escalated = g->escalated ? 1 : 0;
    if (out_chain) {
        if (g->loaded)
            memcpy(out_chain, g->g.chain, 32);
        else
            memset(out_chain, 0, 32);
    }
    return zxvi_ok();
}
