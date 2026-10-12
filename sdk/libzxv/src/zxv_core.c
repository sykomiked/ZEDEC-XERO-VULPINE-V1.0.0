/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_core.c — ABI 1.1: exact rationals, four-valued logic, capability
 * handles, the attestation registry, the UBH-168 wire, hashes and secure
 * memory. Every computation is delegated to the kernel module named in each
 * section; this file only validates arguments, maps status codes and owns
 * the handles. */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "zxv_internal.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "keccak.h"
#include "lpres.h"
#include "rational.h"
#include "zt_harmonic_wire.h"
#include "zxv_q16.h"

/* OS randomness for zxv_random. ZXV_HAVE_ARC4RANDOM may also come from the
 * build (the TypeScript wasm build maps it to WASI random_get). */
#if defined(ZXV_HAVE_ARC4RANDOM)
#    include <stdlib.h> /* arc4random_buf */
#elif defined(__APPLE__) || defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__) || \
    defined(__ANDROID__)
/* Android: bionic has arc4random_buf at every API level, while getrandom()
 * is only declared from API 28. */
#    include <stdlib.h>
#    define ZXV_HAVE_ARC4RANDOM 1
#elif defined(_WIN32)
#    include <windows.h>
#    include <bcrypt.h> /* BCryptGenRandom; link bcrypt.lib / -lbcrypt */
#    if defined(_MSC_VER)
#        pragma comment(lib, "bcrypt")
#    endif
#    define ZXV_RANDOM_BCRYPT 1
#elif defined(__wasi__)
#    include <unistd.h> /* getentropy, over WASI random_get */
#    define ZXV_RANDOM_GETENTROPY 1
#elif defined(__linux__)
#    include <sys/random.h>
#    include <errno.h>
#endif

uint32_t zxvi_features_11(void)
{
    uint32_t m = ZXV_FEAT_RATIONAL | ZXV_FEAT_LPRES | ZXV_FEAT_CAPS | ZXV_FEAT_RMAG |
                 ZXV_FEAT_PHASE | ZXV_FEAT_WIRE | ZXV_FEAT_SWARM | ZXV_FEAT_HASH |
                 ZXV_FEAT_EVM | ZXV_FEAT_HEX;
#ifndef ZXV_NO_PQC
    m |= ZXV_FEAT_HW; /* regions need ML-DSA; ZXV_NO_PQC builds return NOTIMPL */
#endif
#ifndef ZXV_NO_PQC
    m |= ZXV_FEAT_MLKEM1024 | ZXV_FEAT_MLDSA87 | ZXV_FEAT_ED25519;
#endif
#ifndef ZXV_NO_TENSOR
    m |= ZXV_FEAT_TENSOR;
#endif
    /* ZXV_FEAT_LORA: the engine has no LoRA path yet. */
    return m;
}

void zxvi_wipe(void *p, size_t n)
{
    if (!p) return;
    volatile unsigned char *v = (volatile unsigned char *) p;
    while (n--) *v++ = 0;
    __asm__ __volatile__("" : : "r"(p) : "memory");
}

/* ===================================================================== */
/* Exact rationals (kernel/src/rational)                                 */
/* ===================================================================== */

static int32_t rat_in(int64_t num, int64_t den, rat_t *out, const char *what)
{
    if (den == 0) return zxvi_fail(ZXV_E_ARG, "%s has a zero denominator", what);
    if (num == INT64_MIN || den == INT64_MIN)
        return zxvi_fail(ZXV_E_RANGE, "%s uses INT64_MIN, whose negation is not an int64", what);
    rat_t r = rat_make(num, den);
    if (!r.valid) return zxvi_fail(ZXV_E_RANGE, "%s cannot be normalized", what);
    *out = r;
    return ZXV_OK;
}

static int32_t rat_out(rat_t r, int64_t *out_num, int64_t *out_den, const char *op)
{
    if (!out_num || !out_den) return zxvi_fail(ZXV_E_ARG, "out_num or out_den is NULL");
    if (!r.valid || r.num == INT64_MIN)
        return zxvi_fail(ZXV_E_RANGE, "%s overflows 64-bit numerator/denominator", op);
    *out_num = r.num;
    *out_den = r.den;
    return zxvi_ok();
}

int32_t zxv_rat_normalize(int64_t num, int64_t den, int64_t *out_num, int64_t *out_den)
{
    rat_t a = {0, 1, false};
    int32_t rc = rat_in(num, den, &a, "value");
    return rc ? rc : rat_out(a, out_num, out_den, "normalize");
}

typedef rat_t (*rat_op)(rat_t, rat_t);

static int32_t rat_bin(int64_t an, int64_t ad, int64_t bn, int64_t bd, int64_t *on, int64_t *od,
                       rat_op op, const char *name)
{
    rat_t a = {0, 1, false}, b = {0, 1, false};
    int32_t rc;
    if ((rc = rat_in(an, ad, &a, "a")) || (rc = rat_in(bn, bd, &b, "b"))) return rc;
    return rat_out(op(a, b), on, od, name);
}

int32_t zxv_rat_add(int64_t an, int64_t ad, int64_t bn, int64_t bd, int64_t *on, int64_t *od)
{
    return rat_bin(an, ad, bn, bd, on, od, rat_add, "add");
}

int32_t zxv_rat_sub(int64_t an, int64_t ad, int64_t bn, int64_t bd, int64_t *on, int64_t *od)
{
    return rat_bin(an, ad, bn, bd, on, od, rat_sub, "sub");
}

int32_t zxv_rat_mul(int64_t an, int64_t ad, int64_t bn, int64_t bd, int64_t *on, int64_t *od)
{
    return rat_bin(an, ad, bn, bd, on, od, rat_mul, "mul");
}

int32_t zxv_rat_div(int64_t an, int64_t ad, int64_t bn, int64_t bd, int64_t *on, int64_t *od)
{
    if (bn == 0 && bd != 0) return zxvi_fail(ZXV_E_ARG, "division by zero");
    return rat_bin(an, ad, bn, bd, on, od, rat_div, "div");
}

int32_t zxv_rat_cmp(int64_t an, int64_t ad, int64_t bn, int64_t bd, int32_t *out_cmp)
{
    if (!out_cmp) return zxvi_fail(ZXV_E_ARG, "out_cmp is NULL");
    rat_t a = {0, 1, false}, b = {0, 1, false};
    int32_t rc;
    if ((rc = rat_in(an, ad, &a, "a")) || (rc = rat_in(bn, bd, &b, "b"))) return rc;
    *out_cmp = rat_cmp(a, b);
    return zxvi_ok();
}

/* Strict grammar: [+-]?D+ ( '.' D+ | '/' D+ )?  — no spaces, no exponent. */
static bool rat_syntax(const char *s)
{
    if (*s == '+' || *s == '-') s++;
    if (!(*s >= '0' && *s <= '9')) return false;
    while (*s >= '0' && *s <= '9') s++;
    if (*s == '.' || *s == '/') {
        s++;
        if (!(*s >= '0' && *s <= '9')) return false;
        while (*s >= '0' && *s <= '9') s++;
    }
    return *s == '\0';
}

int32_t zxv_rat_parse(const char *text, int64_t *out_num, int64_t *out_den)
{
    if (!text) return zxvi_fail(ZXV_E_ARG, "text is NULL");
    if (!rat_syntax(text))
        return zxvi_fail(ZXV_E_FORMAT, "\"%.40s\" is not an exact number (use 12.75 or 3/4)", text);
    rat_t r = rat_from_string(text);
    if (!r.valid) {
        const char *slash = strchr(text, '/');
        bool zero_den = slash && strspn(slash + 1, "0") == strlen(slash + 1);
        if (zero_den) return zxvi_fail(ZXV_E_ARG, "\"%.40s\" has a zero denominator", text);
        return zxvi_fail(ZXV_E_RANGE, "\"%.40s\" does not fit 64-bit numerator/denominator", text);
    }
    return rat_out(r, out_num, out_den, "parse");
}

int32_t zxv_rat_format(int64_t num, int64_t den, char *buf, size_t cap, size_t *out_len)
{
    rat_t r = {0, 1, false};
    int32_t rc = rat_in(num, den, &r, "value");
    if (rc) return rc;
    char tmp[128];
    uint32_t n = rat_to_string(r, tmp, sizeof tmp);
    if (n >= sizeof tmp) return zxvi_fail(ZXV_E_INTERNAL, "rendering exceeded %zu bytes", sizeof tmp);
    return zxvi_out_text_n(tmp, n, buf, cap, out_len);
}

int32_t zxv_rat_from_ieee754(uint64_t bits, int64_t *out_num, int64_t *out_den)
{
    if (!out_num || !out_den) return zxvi_fail(ZXV_E_ARG, "out_num or out_den is NULL");
    bool neg = (bits >> 63) != 0;
    int32_t exp = (int32_t) ((bits >> 52) & 0x7FF);
    uint64_t frac = bits & ((UINT64_C(1) << 52) - 1);
    if (exp == 0x7FF)
        return zxvi_fail(ZXV_E_FLOAT, frac ? "NaN has no rational value" : "infinity has no rational value");
    uint64_t m;
    int32_t e; /* value = m * 2^e */
    if (exp == 0) {
        if (frac == 0) {
            *out_num = 0;
            *out_den = 1;
            return zxvi_ok();
        }
        m = frac;
        e = -1074;
    } else {
        m = frac | (UINT64_C(1) << 52);
        e = exp - 1075;
    }
    while ((m & 1) == 0) {
        m >>= 1;
        e++;
    }
    if (e >= 0) {
        if (e > 62 || m > ((uint64_t) INT64_MAX >> e))
            return zxvi_fail(ZXV_E_RANGE, "the double's exact value exceeds int64");
        *out_num = (int64_t) (m << e) * (neg ? -1 : 1);
        *out_den = 1;
    } else {
        if (-e > 62) return zxvi_fail(ZXV_E_RANGE, "the double's exact denominator exceeds 2^62");
        *out_num = (int64_t) m * (neg ? -1 : 1);
        *out_den = (int64_t) 1 << -e;
    }
    return zxvi_ok();
}

int32_t zxv_rat_to_q16(int64_t num, int64_t den, int32_t *out_q16)
{
    if (!out_q16) return zxvi_fail(ZXV_E_ARG, "out_q16 is NULL");
    rat_t r = {0, 1, false};
    int32_t rc = rat_in(num, den, &r, "value");
    if (rc) return rc;
    int32_t q;
    /* zxv_q16.h: __int128 where the compiler has it, else a portable
     * 64-bit long division with the same result. */
    if (!zxv_q16_round(r.num, r.den, &q)) return zxvi_fail(ZXV_E_RANGE, "value is outside Q16.16");
    *out_q16 = q;
    return zxvi_ok();
}

/* ===================================================================== */
/* Four-valued logic (kernel/src/lpres)                                  */
/* ===================================================================== */

static bool lp_ok(int32_t v)
{
    return v >= ZXV_LP_NEITHER && v <= ZXV_LP_BOTH;
}

int32_t zxv_lp_not(int32_t a, int32_t *out)
{
    if (!out || !lp_ok(a)) return zxvi_fail(ZXV_E_ARG, "invalid lpres value or NULL out");
    *out = (int32_t) lpres_negate((lpres_state_t) a);
    return zxvi_ok();
}

int32_t zxv_lp_and(int32_t a, int32_t b, int32_t *out)
{
    if (!out || !lp_ok(a) || !lp_ok(b)) return zxvi_fail(ZXV_E_ARG, "invalid lpres value or NULL out");
    *out = (int32_t) lpres_conjoin((lpres_state_t) a, (lpres_state_t) b);
    return zxvi_ok();
}

int32_t zxv_lp_or(int32_t a, int32_t b, int32_t *out)
{
    if (!out || !lp_ok(a) || !lp_ok(b)) return zxvi_fail(ZXV_E_ARG, "invalid lpres value or NULL out");
    *out = (int32_t) lpres_disjoin((lpres_state_t) a, (lpres_state_t) b);
    return zxvi_ok();
}

int32_t zxv_lp_from_evidence(uint32_t positive, uint32_t negative, int32_t *out)
{
    if (!out) return zxvi_fail(ZXV_E_ARG, "out is NULL");
    *out = positive && negative ? ZXV_LP_BOTH
           : positive           ? ZXV_LP_TRUE
           : negative           ? ZXV_LP_FALSE
                                : ZXV_LP_NEITHER;
    return zxvi_ok();
}

const char *zxv_lp_name(int32_t value)
{
    switch (value) {
    case ZXV_LP_NEITHER: return "NEITHER";
    case ZXV_LP_TRUE: return "TRUE";
    case ZXV_LP_FALSE: return "FALSE";
    case ZXV_LP_BOTH: return "BOTH";
    default: return "INVALID";
    }
}

/* ===================================================================== */
/* Capabilities                                                          */
/* ===================================================================== */

#define CAP_MAGIC 0x5A584341u /* "ZXCA" */

struct zxv_cap {
    uint32_t magic;
    zxv_ctx *ctx;
    const struct zxv_cap *parent;
    uint32_t process_id;
    uint8_t identity[ZXV_IDENTITY_BYTES];
    uint64_t caps;
    _Atomic int32_t attestation;
    _Atomic int32_t revoked;
};

int32_t zxv_cap_create(zxv_ctx *ctx, uint32_t process_id, const uint8_t *identity, uint64_t caps,
                       zxv_cap **out_cap)
{
    if (!ctx || !out_cap) return zxvi_fail(ZXV_E_ARG, "ctx or out_cap is NULL");
    if (caps & ~ZXV_CAP_ALL_1_4) return zxvi_fail(ZXV_E_ARG, "unknown capability bits 0x%llx",
                                                  (unsigned long long) (caps & ~ZXV_CAP_ALL_1_4));
    int32_t rc = zxvi_ctx_enter(ctx);
    if (rc) return rc;
    zxvi_ctx_leave(ctx);
    zxv_cap *c = calloc(1, sizeof *c);
    if (!c) return zxvi_fail(ZXV_E_NOMEM, "capability allocation failed");
    c->magic = CAP_MAGIC;
    c->ctx = ctx;
    c->process_id = process_id;
    if (identity) memcpy(c->identity, identity, ZXV_IDENTITY_BYTES);
    c->caps = caps;
    atomic_init(&c->attestation, ZXV_LP_TRUE);
    atomic_init(&c->revoked, 0);
    *out_cap = c;
    return zxvi_ok();
}

static bool cap_live(const zxv_cap *c)
{
    for (; c; c = c->parent)
        if (atomic_load(&((zxv_cap *) c)->revoked)) return false;
    return true;
}

int32_t zxv_cap_attenuate(const zxv_cap *parent, uint64_t keep, zxv_cap **out_cap)
{
    if (!parent || parent->magic != CAP_MAGIC || !out_cap)
        return zxvi_fail(ZXV_E_ARG, "parent is not a capability or out_cap is NULL");
    int32_t rc = zxvi_ctx_check(parent->ctx, "cap_attenuate");
    if (rc) return rc;
    if (!cap_live(parent)) return zxvi_fail(ZXV_E_CAP, "parent capability is revoked");
    zxv_cap *c = calloc(1, sizeof *c);
    if (!c) return zxvi_fail(ZXV_E_NOMEM, "capability allocation failed");
    c->magic = CAP_MAGIC;
    c->ctx = parent->ctx;
    c->parent = parent;
    c->process_id = parent->process_id;
    memcpy(c->identity, parent->identity, ZXV_IDENTITY_BYTES);
    c->caps = parent->caps & keep;
    atomic_init(&c->attestation, atomic_load(&((zxv_cap *) parent)->attestation));
    atomic_init(&c->revoked, 0);
    *out_cap = c;
    return zxvi_ok();
}

int32_t zxv_cap_revoke(zxv_cap *cap)
{
    if (!cap || cap->magic != CAP_MAGIC) return zxvi_fail(ZXV_E_ARG, "not a capability");
    atomic_store(&cap->revoked, 1);
    return zxvi_ok();
}

int32_t zxv_cap_set_attestation(zxv_cap *cap, int32_t lp_state)
{
    if (!cap || cap->magic != CAP_MAGIC) return zxvi_fail(ZXV_E_ARG, "not a capability");
    if (!lp_ok(lp_state)) return zxvi_fail(ZXV_E_ARG, "attestation %d is not a ZXV_LP_* value", lp_state);
    int32_t rc = zxvi_ctx_check(cap->ctx, "cap_set_attestation");
    if (rc) return rc;
    atomic_store(&cap->attestation, lp_state);
    return zxvi_ok();
}

int32_t zxv_cap_info(const zxv_cap *cap, uint64_t *out_caps, uint32_t *out_process_id,
                     int32_t *out_attestation, int32_t *out_revoked)
{
    if (!cap || cap->magic != CAP_MAGIC) return zxvi_fail(ZXV_E_ARG, "not a capability");
    int32_t rc = zxvi_ctx_check(cap->ctx, "cap_info");
    if (rc) return rc;
    if (out_caps) *out_caps = cap->caps;
    if (out_process_id) *out_process_id = cap->process_id;
    if (out_attestation) *out_attestation = atomic_load(&((zxv_cap *) cap)->attestation);
    if (out_revoked) *out_revoked = cap_live(cap) ? 0 : 1;
    return zxvi_ok();
}

int32_t zxvi_cap_gate(const zxv_cap *cap, uint64_t needed, const char *what, zxv_ctx **out_ctx)
{
    if (!cap || cap->magic != CAP_MAGIC)
        return zxvi_fail(ZXV_E_ARG, "%s: cap is NULL or not a capability", what);
    /* Fail closed first: a halted context is FATAL whatever the cap says. */
    if (zxvi_ctx_is_poisoned(cap->ctx))
        return zxvi_fail(ZXV_E_POISONED, "%s: context halted after a broken invariant", what);
    if (!cap_live(cap)) return zxvi_fail(ZXV_E_CAP, "%s: capability is revoked", what);
    if ((cap->caps & needed) != needed)
        return zxvi_fail(ZXV_E_CAP, "%s: capability lacks 0x%llx", what,
                         (unsigned long long) (needed & ~cap->caps));
    int32_t att = atomic_load(&((zxv_cap *) cap)->attestation);
    if (att != ZXV_LP_TRUE)
        return zxvi_fail(ZXV_E_CAP, "%s: attestation is %s%s", what, zxv_lp_name(att),
                         att == ZXV_LP_BOTH ? " (contradictory attestation)" : "");
    if (out_ctx) *out_ctx = cap->ctx;
    return ZXV_OK;
}

int32_t zxvi_cap_gate_ctx(const zxv_cap *cap, uint64_t needed, const char *what,
                          zxv_ctx *handle_ctx)
{
    zxv_ctx *ctx;
    int32_t rc = zxvi_cap_gate(cap, needed, what, &ctx);
    if (rc) return rc;
    if (ctx != handle_ctx)
        return zxvi_fail(ZXV_E_CAP, "%s: capability belongs to a different context than the handle",
                         what);
    return ZXV_OK;
}

int32_t zxv_cap_check(const zxv_cap *cap, uint64_t needed)
{
    int32_t rc = zxvi_cap_gate(cap, needed, "check", NULL);
    return rc ? rc : zxvi_ok();
}

void zxv_cap_destroy(zxv_cap *cap)
{
    if (!cap || cap->magic != CAP_MAGIC) return;
    cap->magic = 0;
    zxvi_wipe(cap->identity, sizeof cap->identity);
    free(cap);
}

int32_t zxv_ledger_post_cap(const zxv_cap *cap, uint32_t from_account, uint32_t to_account,
                            int64_t amount_minor, const char *ccy, const char *reference,
                            uint64_t *out_entry_id)
{
    zxv_ctx *ctx;
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_LEDGER_POST, "ledger_post", &ctx);
    return rc ? rc : zxv_ledger_post(ctx, from_account, to_account, amount_minor, ccy, reference,
                                     out_entry_id);
}

int32_t zxv_ledger_balance_cap(const zxv_cap *cap, uint32_t account, int64_t *out_debit_minor,
                               int64_t *out_credit_minor, int64_t *out_equity_minor)
{
    zxv_ctx *ctx;
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_LEDGER_READ, "ledger_balance", &ctx);
    return rc ? rc : zxv_ledger_balance(ctx, account, out_debit_minor, out_credit_minor,
                                        out_equity_minor);
}

int32_t zxv_ledger_check_cap(const zxv_cap *cap)
{
    zxv_ctx *ctx;
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_LEDGER_READ, "ledger_check", &ctx);
    return rc ? rc : zxv_ledger_check(ctx);
}

/* ABI 1.2: the rest of the 1.0 context surface through a capability (R10).
 * Changing configuration or opening accounts needs LEDGER_POST; reading
 * accounts and journals needs LEDGER_READ. Semantics are the ctx forms'. */
#define CAP_CTX(bit, name)                                       \
    zxv_ctx *ctx;                                                \
    int32_t rc = zxvi_cap_gate(cap, (bit), (name), &ctx);        \
    if (rc) return rc

int32_t zxv_ccy_enable_cap(const zxv_cap *cap, const char *alpha)
{
    CAP_CTX(ZXV_CAP_LEDGER_POST, "ccy_enable");
    return zxv_ccy_enable(ctx, alpha);
}

int32_t zxv_ccy_enable_with_minor_cap(const zxv_cap *cap, const char *alpha, uint8_t minor_units)
{
    CAP_CTX(ZXV_CAP_LEDGER_POST, "ccy_enable_with_minor");
    return zxv_ccy_enable_with_minor(ctx, alpha, minor_units);
}

int32_t zxv_ccy_register_private_cap(const zxv_cap *cap, const char *code, uint8_t minor_units)
{
    CAP_CTX(ZXV_CAP_LEDGER_POST, "ccy_register_private");
    return zxv_ccy_register_private(ctx, code, minor_units);
}

int32_t zxv_ledger_open_account_cap(const zxv_cap *cap, const char *name, const char *ccy,
                                    int32_t kind, uint32_t *out_account_id)
{
    CAP_CTX(ZXV_CAP_LEDGER_POST, "ledger_open_account");
    return zxv_ledger_open_account(ctx, name, ccy, kind, out_account_id);
}

int32_t zxv_ledger_account_ccy_cap(const zxv_cap *cap, uint32_t account, char *buf, size_t cap_len,
                                   size_t *out_len)
{
    CAP_CTX(ZXV_CAP_LEDGER_READ, "ledger_account_ccy");
    return zxv_ledger_account_ccy(ctx, account, buf, cap_len, out_len);
}

int32_t zxv_ledger_account_name_cap(const zxv_cap *cap, uint32_t account, char *buf, size_t cap_len,
                                    size_t *out_len)
{
    CAP_CTX(ZXV_CAP_LEDGER_READ, "ledger_account_name");
    return zxv_ledger_account_name(ctx, account, buf, cap_len, out_len);
}

int32_t zxv_ledger_account_kind_cap(const zxv_cap *cap, uint32_t account, int32_t *out_kind)
{
    CAP_CTX(ZXV_CAP_LEDGER_READ, "ledger_account_kind");
    return zxv_ledger_account_kind(ctx, account, out_kind);
}

int32_t zxv_ledger_account_count_cap(const zxv_cap *cap, uint32_t *out_count)
{
    CAP_CTX(ZXV_CAP_LEDGER_READ, "ledger_account_count");
    return zxv_ledger_account_count(ctx, out_count);
}

int32_t zxv_ledger_entry_count_cap(const zxv_cap *cap, uint32_t account, uint32_t *out_count)
{
    CAP_CTX(ZXV_CAP_LEDGER_READ, "ledger_entry_count");
    return zxv_ledger_entry_count(ctx, account, out_count);
}

int32_t zxv_ledger_entry_at_cap(const zxv_cap *cap, uint32_t account, uint32_t index,
                                uint64_t *out_entry_id, int64_t *out_signed_minor,
                                uint32_t *out_counterparty, char *ref_buf, size_t ref_cap,
                                size_t *out_ref_len)
{
    CAP_CTX(ZXV_CAP_LEDGER_READ, "ledger_entry_at");
    return zxv_ledger_entry_at(ctx, account, index, out_entry_id, out_signed_minor,
                               out_counterparty, ref_buf, ref_cap, out_ref_len);
}

/* ===================================================================== */
/* Attestation registry (kernel/src/lpres)                               */
/* ===================================================================== */

struct zxv_lpres {
    zxv_ctx *ctx;
    pthread_mutex_t mu;
    lpres_registry_t reg;
};

int32_t zxv_lpres_create(zxv_ctx *ctx, zxv_lpres **out_reg)
{
    if (!ctx || !out_reg) return zxvi_fail(ZXV_E_ARG, "ctx or out_reg is NULL");
    int32_t rc = zxvi_ctx_check(ctx, "lpres_create");
    if (rc) return rc;
    zxv_lpres *r = calloc(1, sizeof *r);
    if (!r) return zxvi_fail(ZXV_E_NOMEM, "registry allocation failed");
    r->ctx = ctx;
    pthread_mutex_init(&r->mu, NULL);
    lpres_registry_init(&r->reg);
    *out_reg = r;
    return zxvi_ok();
}

int32_t zxv_lpres_create_cap(const zxv_cap *cap, zxv_lpres **out_reg)
{
    zxv_ctx *ctx;
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_ATTEST, "lpres_create", &ctx);
    return rc ? rc : zxv_lpres_create(ctx, out_reg);
}

void zxv_lpres_destroy(zxv_lpres *reg)
{
    if (!reg) return;
    pthread_mutex_destroy(&reg->mu);
    free(reg);
}

int32_t zxv_lpres_attest(const zxv_cap *cap, zxv_lpres *reg, const char *name, int32_t subject,
                         int32_t lp_state, const char *attester, const uint8_t *digest,
                         uint32_t digest_len, uint32_t *out_index)
{
    if (!reg || !out_index) return zxvi_fail(ZXV_E_ARG, "reg or out_index is NULL");
    int32_t rc = zxvi_cap_gate_ctx(cap, ZXV_CAP_ATTEST, "lpres_attest", reg->ctx);
    if (rc) return rc;
    if ((rc = zxvi_check_str(name, LPRES_MAX_NAME_LEN - 1, "name", false)) ||
        (rc = zxvi_check_str(attester, LPRES_MAX_NAME_LEN - 1, "attester", false)))
        return rc;
    if (subject < 0 || subject > LPRES_SUBJECT_FIRMWARE)
        return zxvi_fail(ZXV_E_ARG, "subject %d is not 0..%d", subject, LPRES_SUBJECT_FIRMWARE);
    if (!lp_ok(lp_state)) return zxvi_fail(ZXV_E_ARG, "state %d is not a ZXV_LP_* value", lp_state);
    if (digest_len > LPRES_MAX_DIGEST_LEN || (digest_len && !digest))
        return zxvi_fail(ZXV_E_ARG, "digest must be at most %d bytes", LPRES_MAX_DIGEST_LEN);
    pthread_mutex_lock(&reg->mu);
    if (lpres_find(&reg->reg, name)) {
        pthread_mutex_unlock(&reg->mu);
        return zxvi_fail(ZXV_E_DUPLICATE, "\"%s\" is already attested", name);
    }
    int32_t idx = lpres_attest(&reg->reg, name, (lpres_subject_type_t) subject,
                               (lpres_state_t) lp_state, attester, digest, digest_len);
    pthread_mutex_unlock(&reg->mu);
    if (idx < 0) return zxvi_fail(ZXV_E_CAPACITY, "registry is full (%d)", LPRES_MAX_ATTESTATIONS);
    *out_index = (uint32_t) idx;
    return zxvi_ok();
}

int32_t zxv_lpres_evidence(const zxv_cap *cap, zxv_lpres *reg, uint32_t index, int32_t positive)
{
    if (!reg) return zxvi_fail(ZXV_E_ARG, "reg is NULL");
    int32_t rc = zxvi_cap_gate_ctx(cap, ZXV_CAP_ATTEST, "lpres_evidence", reg->ctx);
    if (rc) return rc;
    pthread_mutex_lock(&reg->mu);
    bool okay = lpres_add_evidence(&reg->reg, index, positive != 0);
    lpres_attestation_t *a = okay ? lpres_get(&reg->reg, index) : NULL;
    if (a) {
        int32_t s;
        zxv_lp_from_evidence(a->positive_evidence, a->negative_evidence, &s);
        lpres_update_state(&reg->reg, index, (lpres_state_t) s);
    }
    pthread_mutex_unlock(&reg->mu);
    if (!okay) return zxvi_fail(ZXV_E_NOT_FOUND, "attestation %u does not exist or is revoked", index);
    return zxvi_ok();
}

int32_t zxv_lpres_find(zxv_lpres *reg, const char *name, uint32_t *out_index)
{
    if (!reg || !name || !out_index) return zxvi_fail(ZXV_E_ARG, "NULL argument");
    int32_t rc = zxvi_ctx_check(reg->ctx, "lpres_find");
    if (rc) return rc;
    pthread_mutex_lock(&reg->mu);
    lpres_attestation_t *a = lpres_find(&reg->reg, name);
    uint32_t idx = a ? (uint32_t) (a - reg->reg.attestations) : 0;
    pthread_mutex_unlock(&reg->mu);
    if (!a) return zxvi_fail(ZXV_E_NOT_FOUND, "\"%.31s\" is not attested", name);
    *out_index = idx;
    return zxvi_ok();
}

int32_t zxv_lpres_state(zxv_lpres *reg, uint32_t index, int32_t *out_lp_state,
                        uint32_t *out_positive, uint32_t *out_negative)
{
    if (!reg) return zxvi_fail(ZXV_E_ARG, "reg is NULL");
    int32_t rc = zxvi_ctx_check(reg->ctx, "lpres_state");
    if (rc) return rc;
    pthread_mutex_lock(&reg->mu);
    lpres_attestation_t *a = lpres_get(&reg->reg, index);
    if (a) {
        if (out_lp_state) *out_lp_state = (int32_t) a->state;
        if (out_positive) *out_positive = a->positive_evidence;
        if (out_negative) *out_negative = a->negative_evidence;
    }
    pthread_mutex_unlock(&reg->mu);
    if (!a) return zxvi_fail(ZXV_E_NOT_FOUND, "attestation %u does not exist", index);
    return zxvi_ok();
}

int32_t zxv_lpres_gate(zxv_lpres *reg, int32_t *out_clear)
{
    if (!reg || !out_clear) return zxvi_fail(ZXV_E_ARG, "reg or out_clear is NULL");
    int32_t rc = zxvi_ctx_check(reg->ctx, "lpres_gate");
    if (rc) return rc;
    pthread_mutex_lock(&reg->mu);
    *out_clear = lpres_safety_gate_clear(&reg->reg) ? 1 : 0;
    pthread_mutex_unlock(&reg->mu);
    return zxvi_ok();
}

/* ===================================================================== */
/* UBH-168 wire (kernel/src/harmonic/zt_harmonic_wire)                   */
/* ===================================================================== */

int32_t zxv_wire_tag(uint32_t trunk, uint32_t shell, uint32_t sync, uint8_t *out_tag)
{
    if (!out_tag) return zxvi_fail(ZXV_E_ARG, "out_tag is NULL");
    if (trunk > 9 || shell > 9 || sync > 1)
        return zxvi_fail(ZXV_E_ARG, "trunk and shell must be 0..9, sync 0..1");
    *out_tag = zt_wire_make_tag((uint8_t) trunk, (uint8_t) shell, sync != 0);
    return zxvi_ok();
}

int32_t zxv_wire_pack(uint8_t tag, const uint32_t s_plus[3], const uint32_t s_minus[2],
                      uint8_t out_frame[21])
{
    if (!s_plus || !s_minus || !out_frame) return zxvi_fail(ZXV_E_ARG, "NULL argument");
    /* A tag is 100*sync + 10*shell + trunk with sync 0..1, so 0..199. */
    if (tag > 199) return zxvi_fail(ZXV_E_ARG, "tag %u is not 0..199 (100*sync + 10*shell + trunk)", tag);
    zt_ubh168_wire_frame_t f;
    zt_wire_pack_tagged(tag, s_plus, s_minus, &f);
    zt_wire_to_octets(&f, out_frame);
    return zxvi_ok();
}

int32_t zxv_wire_unpack(const uint8_t frame[21], uint8_t *out_tag, uint32_t out_s_plus[3],
                        uint32_t out_s_minus[2])
{
    if (!frame || !out_tag || !out_s_plus || !out_s_minus)
        return zxvi_fail(ZXV_E_ARG, "NULL argument");
    zt_ubh168_wire_frame_t f;
    zt_unpacked_rails_t r;
    zt_wire_from_octets(frame, &f);
    zt_wire_unpack_ubh168(&f, &r);
    *out_tag = r.tag;
    memcpy(out_s_plus, r.s_plus, sizeof r.s_plus);
    memcpy(out_s_minus, r.s_minus, sizeof r.s_minus);
    return zxvi_ok();
}

int32_t zxv_wire_resolve(const uint8_t frame[21], int32_t *out_truth)
{
    if (!frame || !out_truth) return zxvi_fail(ZXV_E_ARG, "NULL argument");
    zt_ubh168_wire_frame_t f;
    zt_unpacked_rails_t r;
    zt_wire_from_octets(frame, &f);
    zt_wire_unpack_ubh168(&f, &r);
    *out_truth = (int32_t) zt_wire_resolve_rails(&r, ZT_Q16_ONE / 16);
    return zxvi_ok();
}

int32_t zxv_wire_idle(uint32_t trunk, uint8_t out_frame[21])
{
    if (!out_frame || trunk > 9) return zxvi_fail(ZXV_E_ARG, "trunk must be 0..9");
    zt_ubh168_wire_frame_t f;
    zt_wire_idle_frame((uint8_t) trunk, &f);
    zt_wire_to_octets(&f, out_frame);
    return zxvi_ok();
}

/* ===================================================================== */
/* Hashes, randomness, secure memory                                     */
/* ===================================================================== */

int32_t zxv_sha3_256(const uint8_t *data, size_t len, uint8_t out[32])
{
    if ((!data && len) || !out) return zxvi_fail(ZXV_E_ARG, "NULL argument");
    sha3_256(data ? data : (const uint8_t *) "", len, out);
    return zxvi_ok();
}

int32_t zxv_shake256(const uint8_t *data, size_t len, uint8_t *out, size_t out_len)
{
    if ((!data && len) || (!out && out_len)) return zxvi_fail(ZXV_E_ARG, "NULL argument");
    shake256(data ? data : (const uint8_t *) "", len, out, out_len);
    return zxvi_ok();
}

/* Keccak-256 with Ethereum's original 0x01 padding. A compact, separate
 * permutation: the kernel's is file-static and fixed to FIPS 202 padding. */
static void keccakf(uint64_t s[25])
{
    static const uint64_t RC[24] = {
        0x0000000000000001ull, 0x0000000000008082ull, 0x800000000000808aull,
        0x8000000080008000ull, 0x000000000000808bull, 0x0000000080000001ull,
        0x8000000080008081ull, 0x8000000000008009ull, 0x000000000000008aull,
        0x0000000000000088ull, 0x0000000080008009ull, 0x000000008000000aull,
        0x000000008000808bull, 0x800000000000008bull, 0x8000000000008089ull,
        0x8000000000008003ull, 0x8000000000008002ull, 0x8000000000000080ull,
        0x000000000000800aull, 0x800000008000000aull, 0x8000000080008081ull,
        0x8000000000008080ull, 0x0000000080000001ull, 0x8000000080008008ull};
    static const int R[24] = {1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14,
                              27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44};
    static const int P[24] = {10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4,
                              15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1};
    for (int round = 0; round < 24; round++) {
        uint64_t c[5];
        for (int x = 0; x < 5; x++) c[x] = s[x] ^ s[x + 5] ^ s[x + 10] ^ s[x + 15] ^ s[x + 20];
        for (int x = 0; x < 5; x++) {
            uint64_t d = c[(x + 4) % 5] ^ ((c[(x + 1) % 5] << 1) | (c[(x + 1) % 5] >> 63));
            for (int y = 0; y < 25; y += 5) s[y + x] ^= d;
        }
        uint64_t t = s[1];
        for (int i = 0; i < 24; i++) {
            uint64_t u = s[P[i]];
            s[P[i]] = (t << R[i]) | (t >> (64 - R[i]));
            t = u;
        }
        for (int y = 0; y < 25; y += 5) {
            uint64_t row[5];
            for (int x = 0; x < 5; x++) row[x] = s[y + x];
            for (int x = 0; x < 5; x++) s[y + x] = row[x] ^ (~row[(x + 1) % 5] & row[(x + 2) % 5]);
        }
        s[0] ^= RC[round];
    }
}

int32_t zxv_keccak256(const uint8_t *data, size_t len, uint8_t out[32])
{
    if ((!data && len) || !out) return zxvi_fail(ZXV_E_ARG, "NULL argument");
    enum { RATE = 136 };
    uint64_t s[25] = {0};
    uint8_t block[RATE];
    for (;;) {
        size_t n = len < RATE ? len : RATE;
        memset(block, 0, RATE);
        if (n) memcpy(block, data, n);
        if (n < RATE) {
            block[n] ^= 0x01;
            block[RATE - 1] ^= 0x80;
        }
        for (int i = 0; i < RATE / 8; i++) {
            uint64_t w = 0;
            for (int b = 7; b >= 0; b--) w = (w << 8) | block[8 * i + b];
            s[i] ^= w;
        }
        keccakf(s);
        if (n < RATE) break;
        data += RATE;
        len -= RATE;
    }
    for (int i = 0; i < 32; i++) out[i] = (uint8_t) (s[i / 8] >> (8 * (i % 8)));
    zxvi_wipe(s, sizeof s);
    return zxvi_ok();
}

int32_t zxv_random(uint8_t *buf, size_t len)
{
    if (!buf && len) return zxvi_fail(ZXV_E_ARG, "buf is NULL");
#if defined(ZXV_HAVE_ARC4RANDOM)
    arc4random_buf(buf, len);
    return zxvi_ok();
#elif defined(ZXV_RANDOM_BCRYPT)
    size_t got = 0;
    while (got < len) {
        ULONG n = len - got > 0x40000000u ? 0x40000000u : (ULONG) (len - got);
        if (BCryptGenRandom(NULL, buf + got, n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
            return zxvi_fail(ZXV_E_IO, "BCryptGenRandom failed");
        got += n;
    }
    return zxvi_ok();
#elif defined(ZXV_RANDOM_GETENTROPY)
    size_t got = 0;
    while (got < len) {
        size_t n = len - got > 256 ? 256 : len - got; /* getentropy's limit */
        if (getentropy(buf + got, n) != 0) return zxvi_fail(ZXV_E_IO, "getentropy failed");
        got += n;
    }
    return zxvi_ok();
#elif defined(__linux__)
    size_t got = 0;
    while (got < len) {
        ssize_t r = getrandom(buf + got, len - got, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            return zxvi_fail(ZXV_E_IO, "getrandom failed");
        }
        got += (size_t) r;
    }
    return zxvi_ok();
#else
    (void) len;
    return zxvi_fail(ZXV_E_NOTIMPL, "no OS random source on this platform");
#endif
}

int32_t zxv_secure_zero(void *buf, size_t len)
{
    if (!buf && len) return zxvi_fail(ZXV_E_ARG, "buf is NULL");
    zxvi_wipe(buf, len);
    return zxvi_ok();
}
