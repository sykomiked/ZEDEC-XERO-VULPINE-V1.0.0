/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_vino.c — ABI 1.6: Vino Embed.
 *
 * The play lane is this file's own integer state: a fixed account table
 * and a growing voucher table (id = index + 1, never reused). It does not
 * use kernel/src/vino_stores, whose surplus maths is a double on hosted
 * builds; the only kernel pieces used are the Fibonacci ladder (through
 * zxv_coin_rung), SHA-256 and ML-DSA-87. Requests and receipts are pure
 * byte codecs plus the wallet's signature; nothing here holds live value.
 * Every refusal is checked before anything changes, so a refused call
 * leaves the sandbox exactly as it was. */
#define _POSIX_C_SOURCE 200809L
#include "zxv_internal.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "sha256.h"
#ifndef ZXV_NO_PQC
#    include "pq_matrix_algs.h"
#endif

_Static_assert(ZXV_MLDSA87_PK_BYTES == 2592u && ZXV_MLDSA87_SK_BYTES == 4896u &&
                   ZXV_MLDSA87_SIG_BYTES == 4627u,
               "ML-DSA-87");
_Static_assert(ZXV_VINO_REQ_BYTES == 160u && ZXV_VINO_RCPT_MSG_BYTES == 16u + 32u + 32u + 8u,
               "record sizes");
_Static_assert(81u + ZXV_VINO_MEMO_MAX + 15u == ZXV_VINO_REQ_BYTES, "request layout");

/* Backing never passes 2^62; faces outstanding stay below 4096 * 2^31. */
#define BACKING_MAX (UINT64_C(1) << 62)

uint32_t zxvi_feat2_vino(void)
{
    /* The play lane and the meter need nothing optional; receipts report
     * ZXV_E_NOTIMPL in a ZXV_NO_PQC build. */
    return ZXV_FEAT2_VINO;
}

/* ===================================================================== */
/* The play lane                                                         */
/* ===================================================================== */

typedef struct {
    uint32_t owner;
    uint8_t is_issuer;
    uint64_t backing; /* issuers only                                  */
    uint64_t faces;   /* issuer: outstanding; holder: what it holds     */
    uint32_t count;   /* active vouchers issued (issuer) or held        */
} acct_t;

typedef struct {
    uint64_t face;
    uint32_t issuer, holder;
    uint8_t rail;
    uint8_t state;
} voucher_t;

struct zxv_vino_play {
    zxv_ctx *ctx;
    pthread_mutex_t mu;
    uint32_t naccts;
    acct_t acct[ZXV_VINO_MAX_ACCOUNTS];
    voucher_t *v; /* every voucher ever issued, by id - 1 */
    uint64_t nv, vcap;
    uint32_t active;
};

/* 10 * backing >= 18 * outstanding, exactly. outstanding is below 2^44, so
 * 18 * outstanding cannot overflow; a backing above UINT64_MAX / 10 covers
 * any outstanding there can be. */
static bool covered(uint64_t backing, uint64_t outstanding)
{
    if (backing > UINT64_MAX / 10) return true;
    return 10 * backing >= 18 * outstanding;
}

static bool rail_ok(int32_t rail)
{
    return rail == ZXV_VINO_RAIL_BEARER || rail == ZXV_VINO_RAIL_GHOST;
}

static bool is_face(uint64_t face)
{
    char saved[ZXVI_ERR_MAX];
    uint32_t rung;
    zxvi_err_save(saved);
    bool ok = zxv_coin_rung(face, &rung) == ZXV_OK;
    zxvi_err_restore(saved);
    return ok;
}

/* Poison the context. Called with the sandbox lock held; the context lock
 * is always taken after it. */
static int32_t poison(zxv_ctx *ctx, const char *why)
{
    int32_t rc = zxvi_ctx_enter(ctx);
    if (rc == ZXV_E_POISONED) return rc;
    if (rc) return zxvi_fail(ZXV_E_INVARIANT, "%s", why);
    rc = zxvi_ctx_poison(ctx, why);
    zxvi_ctx_leave(ctx);
    return rc;
}

int32_t zxv_vino_play_create(const zxv_cap *cap, zxv_vino_play **out)
{
    if (!out) return zxvi_fail(ZXV_E_ARG, "vino_play_create: out is NULL");
    *out = NULL;
    zxv_ctx *ctx;
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_VINO, "vino_play_create", &ctx);
    if (rc) return rc;
    zxv_vino_play *p = calloc(1, sizeof *p);
    if (!p) return zxvi_fail(ZXV_E_NOMEM, "vino_play_create: sandbox");
    p->ctx = ctx;
    pthread_mutex_init(&p->mu, NULL);
    *out = p;
    return zxvi_ok();
}

void zxv_vino_play_destroy(zxv_vino_play *p)
{
    if (!p) return;
    pthread_mutex_destroy(&p->mu);
    free(p->v);
    free(p);
}

#define ENTER(h, what)                                                                             \
    if (!(h)) return zxvi_fail(ZXV_E_ARG, "%s: handle is NULL", what);                             \
    int32_t rc_ = zxvi_cap_gate_ctx(cap, ZXV_CAP_VINO, (what), (h)->ctx);                          \
    if (rc_) return rc_;                                                                           \
    pthread_mutex_lock(&(h)->mu)
#define LEAVE(h, value)                                                                            \
    do {                                                                                           \
        int32_t r_ = (value);                                                                      \
        pthread_mutex_unlock(&(h)->mu);                                                            \
        return r_;                                                                                 \
    } while (0)

static acct_t *account(zxv_vino_play *p, uint32_t a)
{
    return a < p->naccts ? &p->acct[a] : NULL;
}

static voucher_t *voucher(zxv_vino_play *p, uint64_t id)
{
    return id >= 1 && id <= p->nv ? &p->v[id - 1] : NULL;
}

int32_t zxv_vino_play_open(const zxv_cap *cap, zxv_vino_play *p, uint32_t owner, int32_t is_issuer,
                           uint32_t *out_account)
{
    ENTER(p, "vino_play_open");
    if (!out_account) LEAVE(p, zxvi_fail(ZXV_E_ARG, "vino_play_open: out_account is NULL"));
    if (is_issuer != 0 && is_issuer != 1)
        LEAVE(p, zxvi_fail(ZXV_E_ARG, "vino_play_open: is_issuer must be 0 or 1, not %d",
                           (int) is_issuer));
    if (p->naccts >= ZXV_VINO_MAX_ACCOUNTS)
        LEAVE(p, zxvi_fail(ZXV_E_CAPACITY, "vino_play_open: %u accounts are open",
                           (unsigned) ZXV_VINO_MAX_ACCOUNTS));
    acct_t *a = &p->acct[p->naccts];
    memset(a, 0, sizeof *a);
    a->owner = owner;
    a->is_issuer = (uint8_t) is_issuer;
    *out_account = p->naccts++;
    LEAVE(p, zxvi_ok());
}

/* The checks play_back and play_unback share; NULL on a refusal (the
 * detail is set in *rc). */
static acct_t *issuer_amount(zxv_vino_play *p, uint32_t issuer, uint64_t amount, const char *what,
                             int32_t *rc)
{
    acct_t *a = account(p, issuer);
    if (!a) {
        *rc = zxvi_fail(ZXV_E_NOT_FOUND, "%s: no account %u", what, (unsigned) issuer);
        return NULL;
    }
    if (!a->is_issuer) {
        *rc = zxvi_fail(ZXV_E_ARG, "%s: account %u is not an issuer", what, (unsigned) issuer);
        return NULL;
    }
    if (amount == 0 || amount > ZXV_VINO_AMOUNT_MAX) {
        *rc = zxvi_fail(ZXV_E_RANGE, "%s: amount %llu is not in 1..2^59", what,
                        (unsigned long long) amount);
        return NULL;
    }
    return a;
}

int32_t zxv_vino_play_back(const zxv_cap *cap, zxv_vino_play *p, uint32_t issuer, uint64_t amount)
{
    ENTER(p, "vino_play_back");
    int32_t rc = ZXV_OK;
    acct_t *a = issuer_amount(p, issuer, amount, "vino_play_back", &rc);
    if (!a) LEAVE(p, rc);
    if (amount > BACKING_MAX - a->backing)
        LEAVE(p, zxvi_fail(ZXV_E_RANGE, "vino_play_back: backing would pass 2^62"));
    a->backing += amount;
    LEAVE(p, zxvi_ok());
}

int32_t zxv_vino_play_unback(const zxv_cap *cap, zxv_vino_play *p, uint32_t issuer, uint64_t amount)
{
    ENTER(p, "vino_play_unback");
    int32_t rc = ZXV_OK;
    acct_t *a = issuer_amount(p, issuer, amount, "vino_play_unback", &rc);
    if (!a) LEAVE(p, rc);
    if (amount > a->backing)
        LEAVE(p, zxvi_fail(ZXV_E_FUNDS, "vino_play_unback: %llu is more than the backing %llu",
                           (unsigned long long) amount, (unsigned long long) a->backing));
    if (!covered(a->backing - amount, a->faces))
        LEAVE(p, zxvi_fail(
                     ZXV_E_COVERAGE, "vino_play_unback: backing %llu would not cover 1.8x of %llu",
                     (unsigned long long) (a->backing - amount), (unsigned long long) a->faces));
    a->backing -= amount;
    LEAVE(p, zxvi_ok());
}

int32_t zxv_vino_issue(const zxv_cap *cap, zxv_vino_play *p, uint32_t issuer, uint32_t holder,
                       uint64_t face, int32_t rail, uint64_t *out_id)
{
    ENTER(p, "vino_issue");
    if (!out_id) LEAVE(p, zxvi_fail(ZXV_E_ARG, "vino_issue: out_id is NULL"));
    acct_t *is = account(p, issuer), *ho = account(p, holder);
    if (!is || !ho)
        LEAVE(p, zxvi_fail(ZXV_E_NOT_FOUND, "vino_issue: no account %u",
                           (unsigned) (is ? holder : issuer)));
    if (!is->is_issuer)
        LEAVE(p,
              zxvi_fail(ZXV_E_ARG, "vino_issue: account %u is not an issuer", (unsigned) issuer));
    if (ho->is_issuer)
        LEAVE(p, zxvi_fail(ZXV_E_ARG, "vino_issue: account %u is not a holder", (unsigned) holder));
    if (!rail_ok(rail))
        LEAVE(p, zxvi_fail(ZXV_E_ARG, "vino_issue: rail %d is not 0 or 1", (int) rail));
    if (!is_face(face))
        LEAVE(p, zxvi_fail(ZXV_E_DENOM, "vino_issue: %llu is not a Fibonacci ladder face",
                           (unsigned long long) face));
    if (p->active >= ZXV_VINO_MAX_VOUCHERS)
        LEAVE(p, zxvi_fail(ZXV_E_CAPACITY, "vino_issue: %u vouchers are active",
                           (unsigned) ZXV_VINO_MAX_VOUCHERS));
    if (!covered(is->backing, is->faces + face))
        LEAVE(p,
              zxvi_fail(ZXV_E_COVERAGE,
                        "vino_issue: backing %llu does not cover 1.8x of %llu outstanding",
                        (unsigned long long) is->backing, (unsigned long long) (is->faces + face)));
    if (p->nv == p->vcap) {
        uint64_t n = p->vcap ? p->vcap * 2 : 64;
        if (n > SIZE_MAX / sizeof(voucher_t))
            LEAVE(p, zxvi_fail(ZXV_E_NOMEM, "vino_issue: voucher table"));
        voucher_t *nvp = realloc(p->v, (size_t) n * sizeof(voucher_t));
        if (!nvp) LEAVE(p, zxvi_fail(ZXV_E_NOMEM, "vino_issue: voucher table"));
        p->v = nvp;
        p->vcap = n;
    }
    voucher_t *v = &p->v[p->nv++];
    v->face = face;
    v->issuer = issuer;
    v->holder = holder;
    v->rail = (uint8_t) rail;
    v->state = ZXV_VINO_V_ACTIVE;
    is->faces += face;
    is->count++;
    ho->faces += face;
    ho->count++;
    p->active++;
    *out_id = p->nv;
    LEAVE(p, zxvi_ok());
}

/* The checks every holder action shares: the voucher exists and is active,
 * `holder` exists and holds it, `rail` is a rail. NULL on a refusal. */
static voucher_t *held(zxv_vino_play *p, uint64_t id, uint32_t holder, int32_t rail,
                       const char *what, int32_t *rc)
{
    voucher_t *v = voucher(p, id);
    if (!v) {
        *rc = zxvi_fail(ZXV_E_NOT_FOUND, "%s: no voucher %llu", what, (unsigned long long) id);
        return NULL;
    }
    if (!account(p, holder)) {
        *rc = zxvi_fail(ZXV_E_NOT_FOUND, "%s: no account %u", what, (unsigned) holder);
        return NULL;
    }
    if (v->state != ZXV_VINO_V_ACTIVE) {
        *rc =
            zxvi_fail(ZXV_E_STATE, "%s: voucher %llu was redeemed", what, (unsigned long long) id);
        return NULL;
    }
    if (v->holder != holder) {
        *rc = zxvi_fail(ZXV_E_ARG, "%s: account %u does not hold voucher %llu", what,
                        (unsigned) holder, (unsigned long long) id);
        return NULL;
    }
    if (!rail_ok(rail)) {
        *rc = zxvi_fail(ZXV_E_ARG, "%s: rail %d is not 0 or 1", what, (int) rail);
        return NULL;
    }
    return v;
}

int32_t zxv_vino_transfer(const zxv_cap *cap, zxv_vino_play *p, uint64_t id, uint32_t from,
                          uint32_t to, int32_t rail)
{
    ENTER(p, "vino_transfer");
    int32_t rc = ZXV_OK;
    voucher_t *v = held(p, id, from, rail, "vino_transfer", &rc);
    if (!v) LEAVE(p, rc);
    acct_t *dst = account(p, to);
    if (!dst) LEAVE(p, zxvi_fail(ZXV_E_NOT_FOUND, "vino_transfer: no account %u", (unsigned) to));
    if (dst->is_issuer)
        LEAVE(p, zxvi_fail(ZXV_E_ARG, "vino_transfer: account %u is not a holder", (unsigned) to));
    if (to == from) LEAVE(p, zxvi_fail(ZXV_E_ARG, "vino_transfer: from and to are the same"));
    if (v->rail != (uint8_t) rail)
        LEAVE(p, zxvi_fail(ZXV_E_RAIL, "vino_transfer: voucher %llu is on rail %d, not %d",
                           (unsigned long long) id, (int) v->rail, (int) rail));
    acct_t *src = &p->acct[from];
    src->faces -= v->face;
    src->count--;
    dst->faces += v->face;
    dst->count++;
    v->holder = to;
    LEAVE(p, zxvi_ok());
}

int32_t zxv_vino_set_rail(const zxv_cap *cap, zxv_vino_play *p, uint64_t id, uint32_t holder,
                          int32_t rail)
{
    ENTER(p, "vino_set_rail");
    int32_t rc = ZXV_OK;
    voucher_t *v = held(p, id, holder, rail, "vino_set_rail", &rc);
    if (!v) LEAVE(p, rc);
    if (v->rail == (uint8_t) rail)
        LEAVE(p, zxvi_fail(ZXV_E_RAIL, "vino_set_rail: voucher %llu is already on rail %d",
                           (unsigned long long) id, (int) rail));
    v->rail = (uint8_t) rail;
    LEAVE(p, zxvi_ok());
}

int32_t zxv_vino_redeem(const zxv_cap *cap, zxv_vino_play *p, uint64_t id, uint32_t holder)
{
    ENTER(p, "vino_redeem");
    int32_t rc = ZXV_OK;
    voucher_t *v = held(p, id, holder, ZXV_VINO_RAIL_BEARER, "vino_redeem", &rc);
    if (!v) LEAVE(p, rc);
    acct_t *ho = &p->acct[holder], *is = &p->acct[v->issuer];
    ho->faces -= v->face;
    ho->count--;
    is->faces -= v->face;
    is->count--;
    v->state = ZXV_VINO_V_REDEEMED;
    p->active--;
    LEAVE(p, zxvi_ok());
}

int32_t zxv_vino_voucher_info(const zxv_cap *cap, zxv_vino_play *p, uint64_t id, uint64_t *out_face,
                              uint32_t *out_issuer, uint32_t *out_holder, int32_t *out_rail,
                              int32_t *out_state)
{
    ENTER(p, "vino_voucher_info");
    voucher_t *v = voucher(p, id);
    if (!v)
        LEAVE(p, zxvi_fail(ZXV_E_NOT_FOUND, "vino_voucher_info: no voucher %llu",
                           (unsigned long long) id));
    if (out_face) *out_face = v->face;
    if (out_issuer) *out_issuer = v->issuer;
    if (out_holder) *out_holder = v->holder;
    if (out_rail) *out_rail = v->rail;
    if (out_state) *out_state = v->state;
    LEAVE(p, zxvi_ok());
}

int32_t zxv_vino_account_info(const zxv_cap *cap, zxv_vino_play *p, uint32_t account_id,
                              uint32_t *out_owner, int32_t *out_is_issuer, uint64_t *out_backing,
                              uint64_t *out_faces, uint32_t *out_count)
{
    ENTER(p, "vino_account_info");
    acct_t *a = account(p, account_id);
    if (!a)
        LEAVE(p, zxvi_fail(ZXV_E_NOT_FOUND, "vino_account_info: no account %u",
                           (unsigned) account_id));
    if (out_owner) *out_owner = a->owner;
    if (out_is_issuer) *out_is_issuer = a->is_issuer;
    if (out_backing) *out_backing = a->backing;
    if (out_faces) *out_faces = a->faces;
    if (out_count) *out_count = a->count;
    LEAVE(p, zxvi_ok());
}

static bool touches(const voucher_t *v, const acct_t *a, uint32_t id)
{
    return v->state == ZXV_VINO_V_ACTIVE && (a->is_issuer ? v->issuer : v->holder) == id;
}

int32_t zxv_vino_vouchers(const zxv_cap *cap, zxv_vino_play *p, uint32_t account_id, uint64_t *ids,
                          size_t cap_len, size_t *out_count)
{
    ENTER(p, "vino_vouchers");
    if (!out_count) LEAVE(p, zxvi_fail(ZXV_E_ARG, "vino_vouchers: out_count is NULL"));
    acct_t *a = account(p, account_id);
    if (!a)
        LEAVE(p, zxvi_fail(ZXV_E_NOT_FOUND, "vino_vouchers: no account %u", (unsigned) account_id));
    *out_count = a->count;
    if (a->count > cap_len || (a->count && !ids))
        LEAVE(p, zxvi_fail(ZXV_E_BUFFER, "vino_vouchers: %u ids, room for %zu", (unsigned) a->count,
                           ids ? cap_len : (size_t) 0));
    size_t n = 0;
    for (uint64_t i = 0; i < p->nv && n < a->count; i++)
        if (touches(&p->v[i], a, account_id)) ids[n++] = i + 1;
    LEAVE(p, zxvi_ok());
}

int32_t zxv_vino_play_check(const zxv_cap *cap, zxv_vino_play *p)
{
    ENTER(p, "vino_play_check");
    uint64_t faces[ZXV_VINO_MAX_ACCOUNTS] = {0};
    uint32_t count[ZXV_VINO_MAX_ACCOUNTS] = {0};
    uint32_t active = 0;
    const char *bad = NULL;
    for (uint64_t i = 0; i < p->nv && !bad; i++) {
        const voucher_t *v = &p->v[i];
        if (v->state == ZXV_VINO_V_REDEEMED) continue;
        if (v->state != ZXV_VINO_V_ACTIVE)
            bad = "vino: a voucher has an unknown state";
        else if (!is_face(v->face))
            bad = "vino: an active voucher has no ladder face";
        else if (!rail_ok(v->rail))
            bad = "vino: an active voucher is on no rail";
        else if (v->issuer >= p->naccts || !p->acct[v->issuer].is_issuer ||
                 v->holder >= p->naccts || p->acct[v->holder].is_issuer)
            bad = "vino: an active voucher has no issuer or holder";
        else {
            faces[v->issuer] += v->face;
            faces[v->holder] += v->face;
            count[v->issuer]++;
            count[v->holder]++;
            active++;
        }
    }
    if (!bad && active != p->active) bad = "vino: the active voucher count is wrong";
    for (uint32_t a = 0; a < p->naccts && !bad; a++) {
        const acct_t *x = &p->acct[a];
        if (x->faces != faces[a] || x->count != count[a])
            bad = "vino: an account's total is not the sum of its vouchers";
        else if (x->is_issuer ? (x->backing > BACKING_MAX || !covered(x->backing, x->faces))
                              : x->backing != 0)
            bad = "vino: an issuer is below 1.8x coverage";
    }
    LEAVE(p, bad ? poison(p->ctx, bad) : zxvi_ok());
}

/* ===================================================================== */
/* Payment requests and receipts (pure)                                  */
/* ===================================================================== */

static const uint8_t REQ_MAGIC[4] = {'Z', 'X', 'V', 'R'};
static const uint8_t RCPT_TAG[16] = "zxv/vino-rcpt/v1";

static void put64(uint8_t *p, uint64_t x)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (x >> (8 * i));
}
static uint64_t get64(const uint8_t *p)
{
    uint64_t x = 0;
    for (int i = 7; i >= 0; i--) x = (x << 8) | p[i];
    return x;
}

static bool printable(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if ((unsigned char) s[i] < 0x20 || (unsigned char) s[i] > 0x7e) return false;
    return true;
}

/* The rules encode and decode share (everything but the memo). */
static int32_t check_fields(int32_t lane, uint32_t flags, uint64_t amount, uint64_t created,
                            uint64_t expires, const char *what)
{
    if (lane != ZXV_VINO_LANE_PLAY && lane != ZXV_VINO_LANE_LIVE)
        return zxvi_fail(ZXV_E_LANE, "%s: lane %d is not 1 (play) or 2 (live)", what, (int) lane);
    if (flags & ~ZXV_VINO_REQ_FLAGS_KNOWN)
        return zxvi_fail(ZXV_E_FORMAT, "%s: unknown flags 0x%x", what,
                         (unsigned) (flags & ~ZXV_VINO_REQ_FLAGS_KNOWN));
    if (lane == ZXV_VINO_LANE_LIVE && (flags & ZXV_VINO_REQ_CHANCE))
        return zxvi_fail(ZXV_E_CHANCE, "%s: a live request cannot carry a paid chance outcome",
                         what);
    if (amount == 0 || amount > ZXV_VINO_AMOUNT_MAX)
        return zxvi_fail(ZXV_E_RANGE, "%s: amount %llu is not in 1..2^59", what,
                         (unsigned long long) amount);
    if (expires <= created)
        return zxvi_fail(ZXV_E_RANGE, "%s: expires_at must be after created_at", what);
    return ZXV_OK;
}

int32_t zxv_vino_request_encode(const zxv_vino_request *req, uint8_t out[160], uint8_t out_id[32])
{
    if (!req || !out) return zxvi_fail(ZXV_E_ARG, "vino_request_encode: req or out is NULL");
    int32_t rc = check_fields(req->lane, req->flags, req->amount, req->created_at, req->expires_at,
                              "vino_request_encode");
    if (rc) return rc;
    const char *memo = req->memo ? req->memo : "";
    size_t n = strnlen(memo, ZXV_VINO_MEMO_MAX + 1);
    if (n > ZXV_VINO_MEMO_MAX)
        return zxvi_fail(ZXV_E_TOO_LONG, "vino_request_encode: memo is over 64 bytes");
    if (!printable(memo, n))
        return zxvi_fail(ZXV_E_FORMAT, "vino_request_encode: memo is not printable ASCII");
    uint8_t b[ZXV_VINO_REQ_BYTES] = {0};
    memcpy(b, REQ_MAGIC, 4);
    b[4] = 1;
    b[5] = (uint8_t) req->lane;
    b[6] = (uint8_t) req->flags;
    b[7] = (uint8_t) (req->flags >> 8);
    memcpy(b + 8, req->merchant, 32);
    memcpy(b + 40, req->nonce, 16);
    put64(b + 56, req->amount);
    put64(b + 64, req->created_at);
    put64(b + 72, req->expires_at);
    b[80] = (uint8_t) n;
    memcpy(b + 81, memo, n);
    memcpy(out, b, sizeof b);
    if (out_id) sha256(b, sizeof b, out_id);
    return zxvi_ok();
}

int32_t zxv_vino_request_decode(const uint8_t in[160], zxv_vino_request *req, char *memo_buf,
                                size_t memo_buf_len, uint8_t out_id[32])
{
    if (!in || !req || !memo_buf)
        return zxvi_fail(ZXV_E_ARG, "vino_request_decode: in, req or memo_buf is NULL");
    if (memo_buf_len < ZXV_VINO_MEMO_MAX + 1)
        return zxvi_fail(ZXV_E_ARG, "vino_request_decode: memo_buf needs 65 bytes, has %zu",
                         memo_buf_len);
    if (memcmp(in, REQ_MAGIC, 4) || in[4] != 1)
        return zxvi_fail(ZXV_E_FORMAT, "vino_request_decode: not a version 1 request");
    zxv_vino_request r;
    memset(&r, 0, sizeof r);
    r.lane = in[5];
    r.flags = (uint32_t) in[6] | ((uint32_t) in[7] << 8);
    memcpy(r.merchant, in + 8, 32);
    memcpy(r.nonce, in + 40, 16);
    r.amount = get64(in + 56);
    r.created_at = get64(in + 64);
    r.expires_at = get64(in + 72);
    int32_t rc =
        check_fields(r.lane, r.flags, r.amount, r.created_at, r.expires_at, "vino_request_decode");
    if (rc) return rc;
    size_t n = in[80];
    if (n > ZXV_VINO_MEMO_MAX)
        return zxvi_fail(ZXV_E_TOO_LONG, "vino_request_decode: memo length %zu is over 64", n);
    if (!printable((const char *) in + 81, n))
        return zxvi_fail(ZXV_E_FORMAT, "vino_request_decode: memo is not printable ASCII");
    for (size_t i = 81 + n; i < ZXV_VINO_REQ_BYTES; i++)
        if (in[i]) return zxvi_fail(ZXV_E_FORMAT, "vino_request_decode: padding is not zero");
    memcpy(memo_buf, in + 81, n);
    memo_buf[n] = '\0';
    r.memo = memo_buf;
    *req = r;
    if (out_id) sha256(in, ZXV_VINO_REQ_BYTES, out_id);
    return zxvi_ok();
}

int32_t zxv_vino_receipt_msg(const uint8_t request_id[32], const uint8_t wallet_pk[2592],
                             uint64_t approved_at, uint8_t out[88])
{
    if (!request_id || !wallet_pk || !out)
        return zxvi_fail(ZXV_E_ARG, "vino_receipt_msg: NULL argument");
    uint8_t m[ZXV_VINO_RCPT_MSG_BYTES];
    memcpy(m, RCPT_TAG, 16);
    memcpy(m + 16, request_id, 32);
    sha256(wallet_pk, ZXV_MLDSA87_PK_BYTES, m + 48);
    put64(m + 80, approved_at);
    memcpy(out, m, sizeof m);
    return zxvi_ok();
}

#ifndef ZXV_NO_PQC
/* Decode `request` and check approved_at against its window; on ZXV_OK
 * msg holds the bytes the wallet signs. */
static int32_t approval(const uint8_t request[160], const uint8_t wallet_pk[2592],
                        uint64_t approved_at, uint8_t msg[88], zxv_vino_request *r,
                        const char *what)
{
    char memo[ZXV_VINO_MEMO_MAX + 1];
    uint8_t id[32];
    int32_t rc = zxv_vino_request_decode(request, r, memo, sizeof memo, id);
    if (rc) return rc;
    if (approved_at < r->created_at || approved_at > r->expires_at)
        return zxvi_fail(ZXV_E_EXPIRED, "%s: approved_at %llu is outside [%llu, %llu]", what,
                         (unsigned long long) approved_at, (unsigned long long) r->created_at,
                         (unsigned long long) r->expires_at);
    r->memo = NULL; /* memo[] goes out of scope */
    return zxv_vino_receipt_msg(id, wallet_pk, approved_at, msg);
}

int32_t zxv_vino_receipt_sign(const zxv_cap *cap, const uint8_t request[160],
                              const uint8_t wallet_sk[4896], const uint8_t wallet_pk[2592],
                              uint64_t approved_at, uint8_t sig[4627])
{
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_CRYPTO_SIGN, "vino_receipt_sign", NULL);
    if (rc) return rc;
    if (!request || !wallet_sk || !wallet_pk || !sig)
        return zxvi_fail(ZXV_E_ARG, "vino_receipt_sign: NULL argument");
    uint8_t msg[ZXV_VINO_RCPT_MSG_BYTES];
    zxv_vino_request r;
    rc = approval(request, wallet_pk, approved_at, msg, &r, "vino_receipt_sign");
    if (rc) return rc;
    return zxv_mldsa87_sign(cap, wallet_sk, msg, sizeof msg, NULL, 0, sig);
}

int32_t zxv_vino_receipt_verify(const uint8_t request[160], const uint8_t wallet_pk[2592],
                                uint64_t approved_at, const uint8_t sig[4627], uint64_t now)
{
    if (!request || !wallet_pk || !sig)
        return zxvi_fail(ZXV_E_ARG, "vino_receipt_verify: NULL argument");
    uint8_t msg[ZXV_VINO_RCPT_MSG_BYTES];
    zxv_vino_request r;
    int32_t rc = approval(request, wallet_pk, approved_at, msg, &r, "vino_receipt_verify");
    if (rc) return rc;
    if (now > r.expires_at)
        return zxvi_fail(ZXV_E_EXPIRED, "vino_receipt_verify: the request expired at %llu",
                         (unsigned long long) r.expires_at);
    /* The reference verify keeps no mutable state: no lock is needed. */
    if (!pqm_mldsa87_verify(wallet_pk, msg, sizeof msg, NULL, 0, sig))
        return zxvi_fail(ZXV_E_VERIFY, "vino_receipt_verify: not this wallet's approval");
    return zxvi_ok();
}
#else /* ZXV_NO_PQC */
int32_t zxv_vino_receipt_sign(const zxv_cap *cap, const uint8_t request[160],
                              const uint8_t wallet_sk[4896], const uint8_t wallet_pk[2592],
                              uint64_t approved_at, uint8_t sig[4627])
{
    (void) cap, (void) request, (void) wallet_sk, (void) wallet_pk, (void) approved_at, (void) sig;
    return zxvi_fail(ZXV_E_NOTIMPL, "built without post-quantum crypto (ZXV_NO_PQC)");
}
int32_t zxv_vino_receipt_verify(const uint8_t request[160], const uint8_t wallet_pk[2592],
                                uint64_t approved_at, const uint8_t sig[4627], uint64_t now)
{
    (void) request, (void) wallet_pk, (void) approved_at, (void) sig, (void) now;
    return zxvi_fail(ZXV_E_NOTIMPL, "built without post-quantum crypto (ZXV_NO_PQC)");
}
#endif

/* ===================================================================== */
/* The daily meter                                                       */
/* ===================================================================== */

struct zxv_vino_meter {
    zxv_ctx *ctx;
    pthread_mutex_t mu;
    uint64_t cap_per_day, day, used;
};

int32_t zxv_vino_meter_create(const zxv_cap *cap, uint64_t daily_cap, zxv_vino_meter **out)
{
    if (!out) return zxvi_fail(ZXV_E_ARG, "vino_meter_create: out is NULL");
    *out = NULL;
    zxv_ctx *ctx;
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_VINO, "vino_meter_create", &ctx);
    if (rc) return rc;
    if (daily_cap == 0 || daily_cap > ZXV_VINO_AMOUNT_MAX)
        return zxvi_fail(ZXV_E_RANGE, "vino_meter_create: daily_cap %llu is not in 1..2^59",
                         (unsigned long long) daily_cap);
    zxv_vino_meter *m = calloc(1, sizeof *m);
    if (!m) return zxvi_fail(ZXV_E_NOMEM, "vino_meter_create: meter");
    m->ctx = ctx;
    m->cap_per_day = daily_cap;
    pthread_mutex_init(&m->mu, NULL);
    *out = m;
    return zxvi_ok();
}

void zxv_vino_meter_destroy(zxv_vino_meter *m)
{
    if (!m) return;
    pthread_mutex_destroy(&m->mu);
    free(m);
}

int32_t zxv_vino_meter_admit(const zxv_cap *cap, zxv_vino_meter *m, uint64_t day, uint64_t amount,
                             uint64_t *out_left)
{
    ENTER(m, "vino_meter_admit");
    if (day < m->day)
        LEAVE(m, zxvi_fail(ZXV_E_RANGE, "vino_meter_admit: day %llu is before day %llu",
                           (unsigned long long) day, (unsigned long long) m->day));
    if (amount == 0 || amount > ZXV_VINO_AMOUNT_MAX)
        LEAVE(m, zxvi_fail(ZXV_E_RANGE, "vino_meter_admit: amount %llu is not in 1..2^59",
                           (unsigned long long) amount));
    uint64_t used = day > m->day ? 0 : m->used;
    if (amount > m->cap_per_day - used) {
        if (out_left) *out_left = m->cap_per_day - used;
        LEAVE(m, zxvi_fail(ZXV_E_LIMIT, "vino_meter_admit: %llu would pass the daily cap",
                           (unsigned long long) amount));
    }
    m->day = day;
    m->used = used + amount;
    if (out_left) *out_left = m->cap_per_day - m->used;
    LEAVE(m, zxvi_ok());
}
