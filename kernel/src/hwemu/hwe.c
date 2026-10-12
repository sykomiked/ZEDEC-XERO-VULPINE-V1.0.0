/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwe.c — the standard engines and their SOFT implementations. See hwe.h. */
#include "hwe.h"
#include "../robin_debanks/sha256.h"
#include "../mlkem/keccak.h"
#include "lpres.h"
#ifndef HWE_NO_TENSOR
#    include "../tensor/zt_kern.h"
#endif
#ifdef HWE_GEN
static const hwe_vec_t HWE_VECS[1];
static const uint32_t hwe_vec_count = 0; /* the generator runs without vectors */
#    define HWE_VEC_COUNT hwe_vec_count
#else
#    include "hwe_vectors.h"
#endif

static uint32_t rd16(const uint8_t *b)
{
    return (uint32_t) b[0] | ((uint32_t) b[1] << 8);
}
static uint32_t rd32(const uint8_t *b)
{
    return rd16(b) | (rd16(b + 2) << 16);
}
static uint64_t rd64(const uint8_t *b)
{
    return (uint64_t) rd32(b) | ((uint64_t) rd32(b + 4) << 32);
}
static void wr64(uint8_t *b, uint64_t v)
{
    for (int i = 0; i < 8; i++) b[i] = (uint8_t) (v >> (8 * i));
}

/* ---- the engines ---- */

static int e_sha256(uint32_t op, const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap,
                    uint32_t *len)
{
    if (op != 0) return HWM_E_ARG;
    if (cap < 32) return HWM_E_BUFFER;
    sha256(in, n, out);
    *len = 32;
    return HWM_OK;
}

static int e_keccak(uint32_t op, const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap,
                    uint32_t *len)
{
    if (op == 0 || op == 1) {
        uint32_t need = op == 0 ? 32u : 64u;
        if (cap < need) return HWM_E_BUFFER;
        if (op == 0)
            sha3_256(in, n, out);
        else
            sha3_512(in, n, out);
        *len = need;
        return HWM_OK;
    }
    if (op == 2 || op == 3) {
        if (n < 2) return HWM_E_ARG;
        uint32_t want = rd16(in);
        if (want == 0 || want > 512u) return HWM_E_ARG;
        if (cap < want) return HWM_E_BUFFER;
        if (op == 2)
            shake128(in + 2, n - 2, out, want);
        else
            shake256(in + 2, n - 2, out, want);
        *len = want;
        return HWM_OK;
    }
    return HWM_E_ARG;
}

static void wipe(volatile uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

static int e_mac(uint32_t op, const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap,
                 uint32_t *len)
{
    if (op != 0 || n < 2) return HWM_E_ARG;
    uint32_t klen = rd16(in);
    if (klen > n - 2) return HWM_E_ARG;
    if (cap < 32) return HWM_E_BUFFER;
    const uint8_t *key = in + 2, *msg = in + 2 + klen;
    uint32_t mlen = n - 2 - klen;
    uint8_t k0[64], pad[64], inner[32];
    for (int i = 0; i < 64; i++) k0[i] = 0;
    if (klen > 64)
        sha256(key, klen, k0);
    else
        for (uint32_t i = 0; i < klen; i++) k0[i] = key[i];
    sha256_ctx_t c;
    for (int i = 0; i < 64; i++) pad[i] = (uint8_t) (k0[i] ^ 0x36);
    sha256_init(&c);
    sha256_update(&c, pad, 64);
    sha256_update(&c, msg, mlen);
    sha256_final(&c, inner);
    for (int i = 0; i < 64; i++) pad[i] = (uint8_t) (k0[i] ^ 0x5c);
    sha256_init(&c);
    sha256_update(&c, pad, 64);
    sha256_update(&c, inner, 32);
    sha256_final(&c, out);
    /* key-bearing: nothing derived from the key stays behind */
    wipe(k0, sizeof k0);
    wipe(pad, sizeof pad);
    wipe(inner, sizeof inner);
    wipe((volatile uint8_t *) &c, (uint32_t) sizeof c);
    *len = 32;
    return HWM_OK;
}

static uint64_t gcd64(uint64_t a, uint64_t b)
{
    while (b) {
        uint64_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

static int e_gcd(uint32_t op, const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap,
                 uint32_t *len)
{
    if (op != 0 || n == 0 || n % 16u || n / 16u > 32u) return HWM_E_ARG;
    uint32_t k = n / 16u;
    if (cap < 8u * k) return HWM_E_BUFFER;
    for (uint32_t i = 0; i < k; i++)
        wr64(out + 8u * i, gcd64(rd64(in + 16u * i), rd64(in + 16u * i + 8u)));
    *len = 8u * k;
    return HWM_OK;
}

static lpres_state_t lane(const uint8_t *v, uint32_t i)
{
    return (lpres_state_t) ((v[i / 4u] >> (2u * (i % 4u))) & 3u);
}

static int e_logic64(uint32_t op, const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap,
                     uint32_t *len)
{
    if (op > 2 || n != (op == 2 ? 16u : 32u)) return HWM_E_ARG;
    if (cap < 16) return HWM_E_BUFFER;
    for (uint32_t i = 0; i < 16; i++) out[i] = 0;
    for (uint32_t i = 0; i < 64; i++) {
        lpres_state_t a = lane(in, i), r;
        if (op == 0)
            r = lpres_conjoin(a, lane(in + 16, i));
        else if (op == 1)
            r = lpres_disjoin(a, lane(in + 16, i));
        else
            r = lpres_negate(a);
        out[i / 4u] |= (uint8_t) (((uint32_t) r & 3u) << (2u * (i % 4u)));
    }
    *len = 16;
    return HWM_OK;
}

#ifndef HWE_NO_TENSOR
static int e_tensor(uint32_t op, const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap,
                    uint32_t *len)
{
    if (op != 0 || n < 4) return HWM_E_ARG;
    uint32_t nb = rd32(in);
    if (nb == 0 || nb > 8u || n != 4u + 34u * nb + 64u * nb + nb) return HWM_E_ARG;
    if (cap < 4) return HWM_E_BUFFER;
    int16_t a[32 * 8];
    const uint8_t *pa = in + 4 + 34u * nb;
    for (uint32_t i = 0; i < 32u * nb; i++) a[i] = (int16_t) rd16(pa + 2u * i);
    zt_fx r = zt_dot_raw_c(in + 4, a, pa + 64u * nb, nb);
    uint32_t u = (uint32_t) r;
    for (int i = 0; i < 4; i++) out[i] = (uint8_t) (u >> (8 * i));
    *len = 4;
    return HWM_OK;
}
#endif

int hwe_soft_call(void *inst, hwm_t *m, uint32_t op, const uint8_t *in, uint32_t in_len,
                  uint8_t *out, uint32_t out_cap, uint32_t *out_len)
{
    (void) m;
    if ((in_len && !in) || !out_len || (out_cap && !out)) return HWM_E_ARG;
    switch ((uint32_t) (uintptr_t) inst) {
    case HWE_FN_SHA256:
        return e_sha256(op, in, in_len, out, out_cap, out_len);
    case HWE_FN_KECCAK:
        return e_keccak(op, in, in_len, out, out_cap, out_len);
    case HWE_FN_MAC:
        return e_mac(op, in, in_len, out, out_cap, out_len);
    case HWE_FN_GCD:
        return e_gcd(op, in, in_len, out, out_cap, out_len);
    case HWE_FN_LOGIC64:
        return e_logic64(op, in, in_len, out, out_cap, out_len);
#ifndef HWE_NO_TENSOR
    case HWE_FN_TENSOR:
        return e_tensor(op, in, in_len, out, out_cap, out_len);
#endif
    default:
        return HWM_E_ARG;
    }
}

/* ---- the catalogue ---- */

static const uint32_t FN_IDS[HWE_COUNT] = {HWE_FN_SHA256, HWE_FN_KECCAK,  HWE_FN_MAC,
                                           HWE_FN_GCD,    HWE_FN_LOGIC64, HWE_FN_TENSOR};
static const char *const NAMES[HWE_COUNT] = {"hash.sha256", "hash.keccak", "mac.hmac-sha256",
                                             "gcd",         "logic64",     "tensor.int"};

uint32_t hwe_available(void)
{
#ifdef HWE_NO_TENSOR
    return HWE_ALL & ~HWE_TENSOR;
#else
    return HWE_ALL;
#endif
}

uint32_t hwe_fn_id(uint32_t bit)
{
    for (uint32_t i = 0; i < HWE_COUNT; i++)
        if (bit == (1u << i)) return FN_IDS[i];
    return 0;
}

const char *hwe_name(uint32_t fn_id)
{
    for (uint32_t i = 0; i < HWE_COUNT; i++)
        if (FN_IDS[i] == fn_id) return NAMES[i];
    return "unknown";
}

int hwe_install(hwm_t *m, uint32_t res_index, uint32_t mask, uint32_t *out_installed)
{
    if (out_installed) *out_installed = 0;
    if (!m || res_index >= m->nres || (mask & ~HWE_ALL)) return HWM_E_ARG;
    mask &= hwe_available();
    uint32_t done = 0;
    for (uint32_t i = 0; i < HWE_COUNT; i++) {
        if (!(mask & (1u << i))) continue;
        uint32_t fn;
        if (hwm_func_find(m, FN_IDS[i], 0) == HWM_OK) continue; /* already there */
        int rc = hwm_func_add(m, FN_IDS[i], NAMES[i], &fn);
        if (rc != HWM_OK) return rc;
        rc = hwm_contract_begin(m, fn, HWE_CONTRACT_V1);
        if (rc != HWM_OK) return rc;
        for (uint32_t k = 0; k < HWE_VEC_COUNT; k++) {
            const hwe_vec_t *v = &HWE_VECS[k];
            if (v->fn != FN_IDS[i]) continue;
            rc = hwm_vector_add(m, fn, v->op, v->in, v->in_len, v->out, v->out_len);
            if (rc != HWM_OK) return rc;
        }
        if (FN_IDS[i] == HWE_FN_MAC) {
            rc = hwm_policy_set(m, fn, HWM_F_KEY_BEARING, 0, 0);
            if (rc != HWM_OK) return rc;
        }
        uint32_t pv;
        rc = hwm_provider_add(m, fn, res_index, HWM_KIND_SOFT, HWE_SOFT_COST, hwe_soft_call,
                              (void *) (uintptr_t) FN_IDS[i], &pv);
        if (rc != HWM_OK) return rc;
        hwm_provider_traits(m, pv, 0, 0, 1);
        if (hwm_attest(m, pv, 0) != LPRES_STATE_TRUE) return HWM_E_PROVIDER;
        done |= 1u << i;
    }
    if (out_installed) *out_installed = done;
    return HWM_OK;
}
