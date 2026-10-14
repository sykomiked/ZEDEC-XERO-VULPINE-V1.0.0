/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hwkey.c — ML-DSA-87 seeds sealed to a P-256 key in the TPM or Secure
 * Enclave (hwkey.h K1-K4). Freestanding. */
#include "hwkey.h"
#include "../mlkem/keccak.h"

static const uint8_t KDF_DOM[12] = {'Z', 'X', 'V', '-', 'H', 'W', 'K', '1', '-', 'K', 'E', 'K'};
static const uint8_t TAG_DOM[12] = {'Z', 'X', 'V', '-', 'H', 'W', 'K', '1', '-', 'T', 'A', 'G'};
static const uint8_t MAGIC[4] = {'Z', 'H', 'K', '1'};

void hwk_wipe(void *p, size_t n)
{
    volatile uint8_t *v = (volatile uint8_t *) p;
    for (size_t i = 0; i < n; i++) v[i] = 0;
}

bool hwk_ct_eq(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t d = 0;
    for (size_t i = 0; i < n; i++) d |= (uint8_t) (a[i] ^ b[i]);
    return d == 0;
}

static void cpy(uint8_t *d, const uint8_t *s, size_t n)
{
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

void hwk_key_id(const uint8_t pk[HWK_PK_BYTES], uint8_t id[HWK_ID_BYTES])
{
    shake256(pk, HWK_PK_BYTES, id, HWK_ID_BYTES);
}

static bool fields_ok(const hwk_sealed_t *k)
{
    return k->kind >= HWK_KIND_TPM && k->kind <= HWK_KIND_SOFTWARE &&
           k->label_len <= HWK_LABEL_MAX && k->handle_len <= HWK_HANDLE_MAX &&
           k->eph_len <= HWK_EPH_MAX && k->eph_len > 0;
}

uint32_t hwk_encode(const hwk_sealed_t *k, uint8_t *out, uint32_t cap)
{
    if (!k || !out || !fields_ok(k)) return 0;
    uint32_t need = 12u + k->label_len + k->handle_len + k->eph_len + 64u + HWK_PK_BYTES;
    if (cap < need) return 0;
    uint32_t o = 0;
    cpy(out, MAGIC, 4);
    o = 4;
    out[o++] = k->kind;
    out[o++] = k->label_len;
    out[o++] = (uint8_t) k->handle_len;
    out[o++] = (uint8_t) (k->handle_len >> 8);
    out[o++] = (uint8_t) k->eph_len;
    out[o++] = (uint8_t) (k->eph_len >> 8);
    out[o++] = 0;
    out[o++] = 0;
    cpy(out + o, k->label, k->label_len);
    o += k->label_len;
    cpy(out + o, k->handle, k->handle_len);
    o += k->handle_len;
    cpy(out + o, k->eph, k->eph_len);
    o += k->eph_len;
    cpy(out + o, k->ct, 32);
    o += 32;
    cpy(out + o, k->tag, 32);
    o += 32;
    cpy(out + o, k->pk, HWK_PK_BYTES);
    o += HWK_PK_BYTES;
    return o;
}

hwk_status_t hwk_decode(const uint8_t *in, uint32_t len, hwk_sealed_t *k)
{
    if (!in || !k) return HWK_ERR_ARG;
    if (len < 12u || !hwk_ct_eq(in, MAGIC, 4) || in[10] || in[11]) return HWK_ERR_FORMAT;
    hwk_wipe(k, sizeof *k);
    k->kind = in[4];
    k->label_len = in[5];
    k->handle_len = (uint16_t) (in[6] | (in[7] << 8));
    k->eph_len = (uint16_t) (in[8] | (in[9] << 8));
    if (!fields_ok(k)) return HWK_ERR_FORMAT;
    uint32_t need = 12u + k->label_len + k->handle_len + k->eph_len + 64u + HWK_PK_BYTES;
    if (len != need) return HWK_ERR_FORMAT;
    uint32_t o = 12;
    cpy(k->label, in + o, k->label_len);
    o += k->label_len;
    cpy(k->handle, in + o, k->handle_len);
    o += k->handle_len;
    cpy(k->eph, in + o, k->eph_len);
    o += k->eph_len;
    cpy(k->ct, in + o, 32);
    o += 32;
    cpy(k->tag, in + o, 32);
    o += 32;
    cpy(k->pk, in + o, HWK_PK_BYTES);
    return HWK_OK;
}

/* K3: pad || mac key from (kind, handle, E, Z). */
static void kdf(const hwk_sealed_t *k, const uint8_t z[HWK_SECRET_BYTES], uint8_t out[64])
{
    uint8_t buf[sizeof KDF_DOM + 1 + 2 + HWK_HANDLE_MAX + 2 + HWK_EPH_MAX + HWK_SECRET_BYTES];
    uint32_t o = 0;
    cpy(buf, KDF_DOM, sizeof KDF_DOM);
    o = sizeof KDF_DOM;
    buf[o++] = k->kind;
    buf[o++] = (uint8_t) k->handle_len;
    buf[o++] = (uint8_t) (k->handle_len >> 8);
    cpy(buf + o, k->handle, k->handle_len);
    o += k->handle_len;
    buf[o++] = (uint8_t) k->eph_len;
    buf[o++] = (uint8_t) (k->eph_len >> 8);
    cpy(buf + o, k->eph, k->eph_len);
    o += k->eph_len;
    cpy(buf + o, z, HWK_SECRET_BYTES);
    o += HWK_SECRET_BYTES;
    shake256(buf, o, out, 64);
    hwk_wipe(buf, sizeof buf);
}

/* K4: keyed SHAKE256 over every stored byte (tag field as zeros). */
static void mac(const hwk_sealed_t *k, const uint8_t key[32], uint8_t tag[32])
{
    uint8_t buf[32 + sizeof TAG_DOM + HWK_ENCODED_MAX];
    cpy(buf, key, 32);
    cpy(buf + 32, TAG_DOM, sizeof TAG_DOM);
    uint32_t n = hwk_encode(k, buf + 32 + sizeof TAG_DOM, HWK_ENCODED_MAX);
    /* zero the tag field: header 12 + label + handle + eph + ct 32 */
    uint8_t *t = buf + 32 + sizeof TAG_DOM + 12u + k->label_len + k->handle_len + k->eph_len + 32u;
    for (unsigned i = 0; i < 32; i++) t[i] = 0;
    shake256(buf, 32 + sizeof TAG_DOM + n, tag, 32);
    hwk_wipe(buf, 32);
}

static hwk_status_t cb_status(int rc)
{
    if (rc == 0) return HWK_OK;
    if (rc == HWK_ERR_UNAVAILABLE) return HWK_ERR_UNAVAILABLE;
    return HWK_ERR_HW;
}

static bool provider_ok(const hwk_provider_t *p)
{
    return p && p->create && p->wrap && p->unwrap && p->random && p->kind >= HWK_KIND_TPM &&
           p->kind <= HWK_KIND_SOFTWARE;
}

hwk_status_t hwk_keygen(const hwk_provider_t *p, const uint8_t *label, uint32_t label_len,
                        hwk_sealed_t *out)
{
    if (!provider_ok(p) || !out || (!label && label_len) || label_len > HWK_LABEL_MAX)
        return HWK_ERR_ARG;
    hwk_wipe(out, sizeof *out);
    out->kind = (uint8_t) p->kind;
    out->label_len = (uint8_t) label_len;
    cpy(out->label, label, label_len);

    uint32_t hl = HWK_HANDLE_MAX;
    hwk_status_t st = cb_status(p->create(p->ctx, label, label_len, out->handle, &hl));
    if (st != HWK_OK) return st;
    if (hl > HWK_HANDLE_MAX) return HWK_ERR_HW;
    out->handle_len = (uint16_t) hl;

    uint8_t z[HWK_SECRET_BYTES], kek[64], seed[HWK_SEED_BYTES];
    uint8_t sk[PQM_MLDSA87_SK_BYTES];
    uint32_t el = HWK_EPH_MAX;
    st = cb_status(p->wrap(p->ctx, out->handle, out->handle_len, out->eph, &el, z));
    if (st == HWK_OK && (el == 0 || el > HWK_EPH_MAX)) st = HWK_ERR_HW;
    if (st == HWK_OK) st = cb_status(p->random(p->ctx, seed, sizeof seed));
    if (st == HWK_OK) {
        out->eph_len = (uint16_t) el;
        kdf(out, z, kek);
        pqm_mldsa87_keygen(seed, out->pk, sk);
        for (unsigned i = 0; i < HWK_SEED_BYTES; i++) out->ct[i] = seed[i] ^ kek[i];
        mac(out, kek + 32, out->tag);
    }
    hwk_wipe(z, sizeof z);
    hwk_wipe(kek, sizeof kek);
    hwk_wipe(seed, sizeof seed);
    hwk_wipe(sk, sizeof sk);
    if (st != HWK_OK) hwk_wipe(out, sizeof *out);
    return st;
}

/* Open the seal into seed and sk (caller wipes both). */
static hwk_status_t open_seal(const hwk_provider_t *p, const hwk_sealed_t *k,
                              uint8_t seed[HWK_SEED_BYTES], uint8_t sk[PQM_MLDSA87_SK_BYTES])
{
    if (!provider_ok(p) || !k) return HWK_ERR_ARG;
    if (!fields_ok(k)) return HWK_ERR_FORMAT;
    if ((uint8_t) p->kind != k->kind) return HWK_ERR_AUTH;
    uint8_t z[HWK_SECRET_BYTES], kek[64], tag[32];
    uint8_t pk[HWK_PK_BYTES];
    hwk_status_t st = cb_status(p->unwrap(p->ctx, k->handle, k->handle_len, k->eph, k->eph_len, z));
    if (st == HWK_OK) {
        kdf(k, z, kek);
        mac(k, kek + 32, tag);
        if (!hwk_ct_eq(tag, k->tag, 32)) st = HWK_ERR_AUTH;
    }
    if (st == HWK_OK) {
        for (unsigned i = 0; i < HWK_SEED_BYTES; i++) seed[i] = k->ct[i] ^ kek[i];
        pqm_mldsa87_keygen(seed, pk, sk);
        if (!hwk_ct_eq(pk, k->pk, HWK_PK_BYTES)) st = HWK_ERR_AUTH;
    }
    hwk_wipe(z, sizeof z);
    hwk_wipe(kek, sizeof kek);
    return st;
}

hwk_status_t hwk_check(const hwk_provider_t *p, const hwk_sealed_t *k)
{
    uint8_t seed[HWK_SEED_BYTES], sk[PQM_MLDSA87_SK_BYTES];
    hwk_status_t st = open_seal(p, k, seed, sk);
    hwk_wipe(seed, sizeof seed);
    hwk_wipe(sk, sizeof sk);
    return st;
}

hwk_status_t hwk_sign(const hwk_provider_t *p, const hwk_sealed_t *k, const uint8_t *msg,
                      size_t msg_len, const uint8_t *ctx, size_t ctx_len,
                      uint8_t sig[HWK_SIG_BYTES])
{
    if (!sig || (!msg && msg_len) || (!ctx && ctx_len) || ctx_len > PQM_MLDSA_CTX_BYTES)
        return HWK_ERR_ARG;
    uint8_t seed[HWK_SEED_BYTES], sk[PQM_MLDSA87_SK_BYTES], rnd[32];
    hwk_status_t st = open_seal(p, k, seed, sk);
    if (st == HWK_OK) st = cb_status(p->random(p->ctx, rnd, sizeof rnd));
    if (st == HWK_OK) pqm_mldsa87_sign(sk, msg, msg_len, ctx, ctx_len, rnd, sig);
    hwk_wipe(seed, sizeof seed);
    hwk_wipe(sk, sizeof sk);
    hwk_wipe(rnd, sizeof rnd);
    return st;
}

hwk_status_t hwk_require_hardware(const hwk_sealed_t *k)
{
    if (!k) return HWK_ERR_ARG;
    return (k->kind == HWK_KIND_TPM || k->kind == HWK_KIND_SECURE_ENCLAVE) ? HWK_OK
                                                                           : HWK_ERR_POLICY;
}
