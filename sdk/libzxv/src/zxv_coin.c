/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_coin.c — ABI 1.5: Fibonacci coins (kernel/src/vino_stores) and swarm
 * token denominations (kernel/src/swarm/swarm_denom).
 *
 * Every face, split and canonical form is the kernel's. This file checks
 * arguments before the kernel sees them (the kernel folds every refusal into
 * one SWARM_ERR_ARG / VINO_ERR_*), so each refusal gets its own status, and
 * owns the zxv_token_wallet handle. Only the integer entry points of
 * vino_stores are used (vino_ladder_denomination, vino_zeckendorf): its
 * surplus_real_t ones are a double in the host build, and this file holds no
 * floating point. */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "zxv_internal.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "swarm_denom.h"
#include "vino_stores.h"

/* The ABI constants are the kernel's; a mismatch must not compile. */
_Static_assert(ZXV_COIN_RUNGS == VINO_LADDER_RUNGS, "coin rungs");
_Static_assert(ZXV_TOKEN_TIERS == SWARM_DENOM_TIERS, "token tiers");
_Static_assert(ZXV_TOKEN_SUPER_TIER == SWARM_DENOM_SUPER_TIER, "super tier");
_Static_assert(ZXV_OCTANE_PLAIN == SWARM_OCTANE_PLAIN && ZXV_OCTANE_REGULAR == SWARM_OCTANE_REGULAR &&
                   ZXV_OCTANE_MIDGRADE == SWARM_OCTANE_MIDGRADE &&
                   ZXV_OCTANE_PREMIUM == SWARM_OCTANE_PREMIUM &&
                   ZXV_OCTANE_RACING == SWARM_OCTANE_RACING && ZXV_OCTANE_MAX == SWARM_OCTANE_MAX,
               "octane grades");
_Static_assert(ZXV_TOKEN_CODE_MAX_BITS == SWARM_DENOM_CODE_MAX_BITS, "code length");
_Static_assert((uint64_t) ZXV_TOKEN_CODE_MAX_BYTES * 8u <= UINT32_MAX, "bit positions fit 32 bits");

uint32_t zxvi_feat2_coin(void)
{
    return ZXV_FEAT2_COIN;
}

/* ===================================================================== */
/* VFV coins                                                             */
/* ===================================================================== */

int32_t zxv_coin_face(uint32_t rung, uint64_t *out_face)
{
    if (!out_face) return zxvi_fail(ZXV_E_ARG, "coin_face: out_face is NULL");
    if (rung > VINO_LADDER_RUNGS)
        return zxvi_fail(ZXV_E_RANGE, "coin_face: rung %u is not 0..%u", rung, VINO_LADDER_RUNGS);
    *out_face = vino_ladder_denomination(rung);
    return zxvi_ok();
}

int32_t zxv_coin_rung(uint64_t face, uint32_t *out_rung)
{
    if (!out_rung) return zxvi_fail(ZXV_E_ARG, "coin_rung: out_rung is NULL");
    /* vino_denomination_rung's integer meaning, without its double. */
    if (face != 0)
        for (uint32_t r = 1; r <= VINO_LADDER_RUNGS; r++)
            if (vino_ladder_denomination(r) == face) {
                *out_rung = r;
                return zxvi_ok();
            }
    return zxvi_fail(ZXV_E_DENOM, "coin_rung: %llu is not a Fibonacci coin face",
                     (unsigned long long) face);
}

int32_t zxv_coin_split(uint64_t amount, uint32_t *rungs, size_t cap, size_t *out_count)
{
    if (!out_count) return zxvi_fail(ZXV_E_ARG, "coin_split: out_count is NULL");
    uint32_t n = 0;
    /* First pass sizes the split; the kernel saturates the count at
     * UINT32_MAX, so that value means "too many to name". */
    int32_t k = vino_zeckendorf(amount, NULL, 0, &n);
    if (k != VINO_OK && k != VINO_ERR_CAPACITY)
        return zxvi_fail(ZXV_E_INTERNAL, "coin_split: vino_zeckendorf returned %d", (int) k);
    if (n == UINT32_MAX)
        return zxvi_fail(ZXV_E_RANGE, "coin_split: %llu needs more than %u coins",
                         (unsigned long long) amount, UINT32_MAX - 1u);
    *out_count = n;
    if (n == 0) return zxvi_ok();
    if (!rungs || cap < n)
        return zxvi_fail(ZXV_E_BUFFER, "coin_split: rungs needs room for %u coins", n);
    uint32_t got = 0;
    k = vino_zeckendorf(amount, rungs, n, &got);
    if (k != VINO_OK || got != n)
        return zxvi_fail(ZXV_E_INTERNAL, "coin_split: vino_zeckendorf returned %d", (int) k);
    return zxvi_ok();
}

/* ===================================================================== */
/* Token denominations (pure, on a counts[64] array)                     */
/* ===================================================================== */

static int32_t bad_octane(const char *what, uint32_t octane)
{
    return zxvi_fail(ZXV_E_RANGE, "%s: octane %u is not 0..%u", what, octane,
                     SWARM_DENOM_TIERS - 1u);
}

/* A scratch kernel wallet holding a copy of counts at `octane`. */
static void load(swarm_wallet_t *w, const uint64_t counts[64], uint32_t octane)
{
    w->model_id = 0;
    w->octane = octane;
    memcpy(w->count, counts, sizeof w->count);
}

int32_t zxv_token_face(uint32_t tier, uint64_t *out_face, int32_t *out_is_super)
{
    if (tier >= SWARM_DENOM_TIERS)
        return zxvi_fail(ZXV_E_RANGE, "token_face: tier %u is not 0..%u", tier,
                         SWARM_DENOM_TIERS - 1u);
    if (out_face) *out_face = swarm_denom_face(tier);
    if (out_is_super) *out_is_super = swarm_denom_is_super(tier) ? 1 : 0;
    return zxvi_ok();
}

int32_t zxv_token_tier(uint64_t face, uint32_t *out_tier)
{
    if (!out_tier) return zxvi_fail(ZXV_E_ARG, "token_tier: out_tier is NULL");
    for (uint32_t k = 0; k < SWARM_DENOM_TIERS; k++)
        if (swarm_denom_face(k) == face) {
            *out_tier = k;
            return zxvi_ok();
        }
    return zxvi_fail(ZXV_E_DENOM, "token_tier: %llu is not a token face",
                     (unsigned long long) face);
}

int32_t zxv_token_value(const uint64_t counts[64], uint64_t *out_value, uint64_t *out_pieces)
{
    if (!counts) return zxvi_fail(ZXV_E_ARG, "token_value: counts is NULL");
    swarm_wallet_t w;
    uint64_t v;
    load(&w, counts, 0);
    if (!swarm_denom_value(&w, &v))
        return zxvi_fail(ZXV_E_RANGE, "token_value: the value passes 64 bits");
    if (out_value) *out_value = v;
    if (out_pieces) *out_pieces = swarm_denom_pieces(&w); /* <= value: never saturates */
    return zxvi_ok();
}

int32_t zxv_token_canonical(uint64_t value, uint32_t octane, uint64_t out_counts[64])
{
    if (!out_counts) return zxvi_fail(ZXV_E_ARG, "token_canonical: out_counts is NULL");
    if (octane >= SWARM_DENOM_TIERS) return bad_octane("token_canonical", octane);
    swarm_wallet_t w;
    if (swarm_denom_init(&w, 0, octane) != SWARM_OK || swarm_denom_set(&w, value) != SWARM_OK)
        return zxvi_fail(ZXV_E_INTERNAL, "token_canonical: swarm_denom_set refused");
    memcpy(out_counts, w.count, sizeof w.count);
    return zxvi_ok();
}

int32_t zxv_token_is_canonical(const uint64_t counts[64], uint32_t octane, int32_t *out_canonical)
{
    if (!counts || !out_canonical)
        return zxvi_fail(ZXV_E_ARG, "token_is_canonical: counts or out_canonical is NULL");
    if (octane >= SWARM_DENOM_TIERS) return bad_octane("token_is_canonical", octane);
    swarm_wallet_t w;
    uint64_t v;
    load(&w, counts, octane);
    if (!swarm_denom_value(&w, &v))
        return zxvi_fail(ZXV_E_RANGE, "token_is_canonical: the value passes 64 bits");
    *out_canonical = swarm_denom_is_canonical(&w) ? 1 : 0;
    return zxvi_ok();
}

int32_t zxv_token_compress(uint64_t counts[64], uint32_t octane)
{
    if (!counts) return zxvi_fail(ZXV_E_ARG, "token_compress: counts is NULL");
    if (octane >= SWARM_DENOM_TIERS) return bad_octane("token_compress", octane);
    swarm_wallet_t w;
    uint64_t v;
    load(&w, counts, octane);
    if (!swarm_denom_value(&w, &v))
        return zxvi_fail(ZXV_E_RANGE, "token_compress: the value passes 64 bits");
    if (swarm_denom_compress(&w) != SWARM_OK)
        return zxvi_fail(ZXV_E_INTERNAL, "token_compress: swarm_denom_compress refused");
    memcpy(counts, w.count, sizeof w.count);
    return zxvi_ok();
}

int32_t zxv_token_fuse(uint64_t counts[64], uint32_t octane, uint32_t tier)
{
    if (!counts) return zxvi_fail(ZXV_E_ARG, "token_fuse: counts is NULL");
    if (octane >= SWARM_DENOM_TIERS) return bad_octane("token_fuse", octane);
    /* Checked here, before tier + 2 is formed: the kernel's own test
     * (tier + 2 > octane) wraps for tier >= UINT32_MAX - 1. */
    if (tier > SWARM_DENOM_TIERS - 3u)
        return zxvi_fail(ZXV_E_RANGE, "token_fuse: tier %u is not 0..%u", tier,
                         SWARM_DENOM_TIERS - 3u);
    if (tier + 2u > octane)
        return zxvi_fail(ZXV_E_STATE, "token_fuse: tier %u is denser than octane %u",
                         tier + 2u, octane);
    if (counts[tier] == 0 || counts[tier + 1u] == 0)
        return zxvi_fail(ZXV_E_FUNDS, "token_fuse: needs one tier-%u and one tier-%u token",
                         tier, tier + 1u);
    if (counts[tier + 2u] == UINT64_MAX)
        return zxvi_fail(ZXV_E_RANGE, "token_fuse: tier %u count is full", tier + 2u);
    swarm_wallet_t w;
    load(&w, counts, octane);
    if (swarm_denom_fuse(&w, tier) != SWARM_OK)
        return zxvi_fail(ZXV_E_INTERNAL, "token_fuse: swarm_denom_fuse refused");
    memcpy(counts, w.count, sizeof w.count);
    return zxvi_ok();
}

int32_t zxv_token_split(uint64_t counts[64], uint32_t tier)
{
    if (!counts) return zxvi_fail(ZXV_E_ARG, "token_split: counts is NULL");
    if (tier == 0 || tier >= SWARM_DENOM_TIERS)
        return zxvi_fail(ZXV_E_RANGE, "token_split: tier %u is not 1..%u", tier,
                         SWARM_DENOM_TIERS - 1u);
    if (counts[tier] == 0)
        return zxvi_fail(ZXV_E_FUNDS, "token_split: no tier-%u token", tier);
    if (tier == 1 ? counts[0] > UINT64_MAX - 2u
                  : counts[tier - 1u] == UINT64_MAX || counts[tier - 2u] == UINT64_MAX)
        return zxvi_fail(ZXV_E_RANGE, "token_split: a lower tier count is full");
    swarm_wallet_t w;
    load(&w, counts, SWARM_OCTANE_MAX);
    if (swarm_denom_split(&w, tier) != SWARM_OK)
        return zxvi_fail(ZXV_E_INTERNAL, "token_split: swarm_denom_split refused");
    memcpy(counts, w.count, sizeof w.count);
    return zxvi_ok();
}

int32_t zxv_token_code_bits(uint64_t value, uint32_t *out_bits)
{
    if (!out_bits) return zxvi_fail(ZXV_E_ARG, "token_code_bits: out_bits is NULL");
    if (value == UINT64_MAX)
        return zxvi_fail(ZXV_E_RANGE, "token_code_bits: UINT64_MAX has no Fibonacci code");
    uint8_t tmp[(SWARM_DENOM_CODE_MAX_BITS + 7u) / 8u] = {0};
    uint32_t pos = 0;
    if (swarm_denom_code_put(tmp, SWARM_DENOM_CODE_MAX_BITS, &pos, value) != SWARM_OK)
        return zxvi_fail(ZXV_E_INTERNAL, "token_code_bits: swarm_denom_code_put refused");
    *out_bits = pos;
    return zxvi_ok();
}

static int32_t code_args(const char *what, const void *buf, size_t buf_len, const uint32_t *bit,
                         const void *val)
{
    if (!buf || !bit || !val) return zxvi_fail(ZXV_E_ARG, "%s: a pointer is NULL", what);
    if (buf_len > ZXV_TOKEN_CODE_MAX_BYTES)
        return zxvi_fail(ZXV_E_TOO_LONG, "%s: buf_len %zu is over %u bytes", what, buf_len,
                         ZXV_TOKEN_CODE_MAX_BYTES);
    if (*bit > buf_len * 8u)
        return zxvi_fail(ZXV_E_RANGE, "%s: bit %u is past the %zu-bit buffer", what, *bit,
                         buf_len * 8u);
    return ZXV_OK;
}

int32_t zxv_token_code_put(uint8_t *buf, size_t buf_len, uint32_t *inout_bit, uint64_t value)
{
    int32_t rc = code_args("token_code_put", buf, buf_len, inout_bit, buf);
    if (rc) return rc;
    uint32_t need;
    if ((rc = zxv_token_code_bits(value, &need))) return rc;
    uint32_t cap = (uint32_t) (buf_len * 8u);
    if (need > cap - *inout_bit)
        return zxvi_fail(ZXV_E_BUFFER, "token_code_put: needs %u bits, %u left", need,
                         cap - *inout_bit);
    uint32_t pos = *inout_bit;
    if (swarm_denom_code_put(buf, cap, &pos, value) != SWARM_OK || pos != *inout_bit + need)
        return zxvi_fail(ZXV_E_INTERNAL, "token_code_put: swarm_denom_code_put refused");
    *inout_bit = pos;
    return zxvi_ok();
}

int32_t zxv_token_code_get(const uint8_t *buf, size_t buf_len, uint32_t *inout_bit,
                           uint64_t *out_value)
{
    int32_t rc = code_args("token_code_get", buf, buf_len, inout_bit, out_value);
    if (rc) return rc;
    uint32_t pos = *inout_bit;
    uint64_t v;
    if (swarm_denom_code_get(buf, (uint32_t) (buf_len * 8u), &pos, &v) != SWARM_OK)
        return zxvi_fail(ZXV_E_FORMAT,
                         "token_code_get: no complete 64-bit Fibonacci code at bit %u",
                         *inout_bit);
    *inout_bit = pos;
    *out_value = v;
    return zxvi_ok();
}

/* ===================================================================== */
/* The token wallet                                                      */
/* ===================================================================== */

#define WALLET_MAGIC 0x5A58544Bu /* "ZXTK" */

struct zxv_token_wallet {
    uint32_t magic;
    zxv_ctx *ctx;
    pthread_mutex_t mu;
    swarm_wallet_t w; /* always canonical at w.octane */
};

static bool wallet_live(const zxv_token_wallet *w)
{
    return w && w->magic == WALLET_MAGIC;
}

static int32_t wallet_arg(const char *what)
{
    return zxvi_fail(ZXV_E_ARG, "%s: wallet is NULL or not a zxv_token_wallet", what);
}

/* Value of a held wallet; the canonical form never overflows (D5). */
static uint64_t held_value(const zxv_token_wallet *w)
{
    uint64_t v = 0;
    (void) swarm_denom_value(&w->w, &v);
    return v;
}

int32_t zxv_token_wallet_create(const zxv_cap *cap, uint32_t model_id, uint32_t octane,
                                zxv_token_wallet **out_wallet)
{
    zxv_ctx *ctx;
    int32_t rc = zxvi_cap_gate(cap, ZXV_CAP_MINT, "token_wallet_create", &ctx);
    if (rc) return rc;
    if (!out_wallet) return zxvi_fail(ZXV_E_ARG, "token_wallet_create: out_wallet is NULL");
    if (octane >= SWARM_DENOM_TIERS) return bad_octane("token_wallet_create", octane);
    zxv_token_wallet *w = calloc(1, sizeof *w);
    if (!w) return zxvi_fail(ZXV_E_NOMEM, "token_wallet_create: allocation failed");
    if (swarm_denom_init(&w->w, model_id, octane) != SWARM_OK) {
        free(w);
        return zxvi_fail(ZXV_E_INTERNAL, "token_wallet_create: swarm_denom_init refused");
    }
    w->magic = WALLET_MAGIC;
    w->ctx = ctx;
    pthread_mutex_init(&w->mu, NULL);
    *out_wallet = w;
    return zxvi_ok();
}

void zxv_token_wallet_destroy(zxv_token_wallet *wallet)
{
    if (!wallet_live(wallet)) return;
    wallet->magic = 0;
    pthread_mutex_destroy(&wallet->mu);
    free(wallet);
}

int32_t zxv_token_wallet_mint(const zxv_cap *cap, zxv_token_wallet *wallet, uint64_t amount)
{
    if (!wallet_live(wallet)) return wallet_arg("token_wallet_mint");
    int32_t rc = zxvi_cap_gate_ctx(cap, ZXV_CAP_MINT, "token_wallet_mint", wallet->ctx);
    if (rc) return rc;
    pthread_mutex_lock(&wallet->mu);
    uint64_t v = held_value(wallet);
    if (amount > UINT64_MAX - v) {
        pthread_mutex_unlock(&wallet->mu);
        return zxvi_fail(ZXV_E_RANGE, "token_wallet_mint: %llu + %llu passes 64 bits",
                         (unsigned long long) v, (unsigned long long) amount);
    }
    swarm_status_t st = swarm_denom_set(&wallet->w, v + amount);
    pthread_mutex_unlock(&wallet->mu);
    if (st != SWARM_OK) return zxvi_fail(ZXV_E_INTERNAL, "token_wallet_mint: kernel %d", (int) st);
    return zxvi_ok();
}

int32_t zxv_token_wallet_spend(const zxv_cap *cap, zxv_token_wallet *wallet, uint64_t amount)
{
    if (!wallet_live(wallet)) return wallet_arg("token_wallet_spend");
    int32_t rc = zxvi_cap_gate_ctx(cap, ZXV_CAP_MINT, "token_wallet_spend", wallet->ctx);
    if (rc) return rc;
    pthread_mutex_lock(&wallet->mu);
    uint64_t v = held_value(wallet);
    if (amount > v) {
        pthread_mutex_unlock(&wallet->mu);
        return zxvi_fail(ZXV_E_FUNDS, "token_wallet_spend: holds %llu, asked %llu",
                         (unsigned long long) v, (unsigned long long) amount);
    }
    swarm_status_t st = swarm_denom_spend(&wallet->w, amount);
    pthread_mutex_unlock(&wallet->mu);
    if (st != SWARM_OK) return zxvi_fail(ZXV_E_INTERNAL, "token_wallet_spend: kernel %d", (int) st);
    return zxvi_ok();
}

int32_t zxv_token_wallet_transfer(const zxv_cap *cap, zxv_token_wallet *from, zxv_token_wallet *to,
                                  uint64_t amount)
{
    if (!wallet_live(from) || !wallet_live(to)) return wallet_arg("token_wallet_transfer");
    int32_t rc = zxvi_cap_gate_ctx(cap, ZXV_CAP_MINT, "token_wallet_transfer", from->ctx);
    if (rc) return rc;
    if (to->ctx != from->ctx)
        return zxvi_fail(ZXV_E_CAP, "token_wallet_transfer: the wallets belong to two contexts");
    /* Lock in address order so two opposite transfers cannot deadlock. */
    zxv_token_wallet *a = from < to ? from : to, *b = from < to ? to : from;
    pthread_mutex_lock(&a->mu);
    if (b != a) pthread_mutex_lock(&b->mu);
    uint64_t vf = held_value(from), vt = held_value(to);
    if (amount > vf) {
        rc = zxvi_fail(ZXV_E_FUNDS, "token_wallet_transfer: holds %llu, asked %llu",
                       (unsigned long long) vf, (unsigned long long) amount);
    } else if (from != to && amount > UINT64_MAX - vt) {
        rc = zxvi_fail(ZXV_E_RANGE, "token_wallet_transfer: the receiver would pass 64 bits");
    } else {
        swarm_status_t st = swarm_denom_transfer(&from->w, &to->w, amount);
        rc = st == SWARM_OK ? zxvi_ok()
                            : zxvi_fail(ZXV_E_INTERNAL, "token_wallet_transfer: kernel %d", (int) st);
    }
    if (b != a) pthread_mutex_unlock(&b->mu);
    pthread_mutex_unlock(&a->mu);
    return rc;
}

int32_t zxv_token_wallet_set_octane(const zxv_cap *cap, zxv_token_wallet *wallet, uint32_t octane)
{
    if (!wallet_live(wallet)) return wallet_arg("token_wallet_set_octane");
    int32_t rc = zxvi_cap_gate_ctx(cap, ZXV_CAP_MINT, "token_wallet_set_octane", wallet->ctx);
    if (rc) return rc;
    if (octane >= SWARM_DENOM_TIERS) return bad_octane("token_wallet_set_octane", octane);
    pthread_mutex_lock(&wallet->mu);
    swarm_status_t st = swarm_denom_set_octane(&wallet->w, octane);
    pthread_mutex_unlock(&wallet->mu);
    if (st != SWARM_OK)
        return zxvi_fail(ZXV_E_INTERNAL, "token_wallet_set_octane: kernel %d", (int) st);
    return zxvi_ok();
}

int32_t zxv_token_wallet_info(zxv_token_wallet *wallet, uint32_t *out_model_id,
                              uint32_t *out_octane, uint64_t *out_value, uint64_t *out_pieces)
{
    if (!wallet_live(wallet)) return wallet_arg("token_wallet_info");
    int32_t rc = zxvi_ctx_check(wallet->ctx, "token_wallet_info");
    if (rc) return rc;
    pthread_mutex_lock(&wallet->mu);
    if (out_model_id) *out_model_id = wallet->w.model_id;
    if (out_octane) *out_octane = wallet->w.octane;
    if (out_value) *out_value = held_value(wallet);
    if (out_pieces) *out_pieces = swarm_denom_pieces(&wallet->w);
    pthread_mutex_unlock(&wallet->mu);
    return zxvi_ok();
}

int32_t zxv_token_wallet_counts(zxv_token_wallet *wallet, uint64_t out_counts[64])
{
    if (!wallet_live(wallet)) return wallet_arg("token_wallet_counts");
    int32_t rc = zxvi_ctx_check(wallet->ctx, "token_wallet_counts");
    if (rc) return rc;
    if (!out_counts) return zxvi_fail(ZXV_E_ARG, "token_wallet_counts: out_counts is NULL");
    pthread_mutex_lock(&wallet->mu);
    memcpy(out_counts, wallet->w.count, sizeof wallet->w.count);
    pthread_mutex_unlock(&wallet->mu);
    return zxvi_ok();
}
