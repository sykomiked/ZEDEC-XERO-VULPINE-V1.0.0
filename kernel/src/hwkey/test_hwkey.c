/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_hwkey.c — sealed ML-DSA-87 keys (hwkey.h K1-K4): round trips, the
 * encoding, every tampered byte region, another machine's chip, provider
 * failures, the hardware policy, and that the seed never appears in what
 * is stored. A mock "chip" stands in for the TPM: its secret lives only in
 * the provider, as on real hardware. */
#include <stdio.h>
#include <string.h>
#include "hwkey.h"
#include "../mlkem/keccak.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                   \
        }                                                                                          \
    } while (0)

/* deterministic generator; records everything it hands out */
static uint64_t rng_n;
static uint8_t drawn[4096];
static uint32_t n_drawn;
static int rng(void *ctx, uint8_t *out, uint32_t len)
{
    (void) ctx;
    uint8_t b[9];
    memcpy(b, &rng_n, 8);
    b[8] = 'R';
    rng_n++;
    shake256(b, sizeof b, out, len);
    for (uint32_t i = 0; i < len && n_drawn < sizeof drawn; i++) drawn[n_drawn++] = out[i];
    return 0;
}

/* ---- a mock chip: kind TPM, secret inside, failure switches ------------ */
typedef struct {
    hwk_soft_t inner;
    int fail_create, fail_wrap, fail_unwrap, fail_random, zero_eph, unavailable;
    unsigned unwraps;
} chip_t;
static hwk_provider_t soft_of_chip;

static int chip_create(void *c, const uint8_t *l, uint32_t ll, uint8_t *h, uint32_t *hl)
{
    chip_t *x = c;
    if (x->unavailable) return HWK_ERR_UNAVAILABLE;
    if (x->fail_create) return 5;
    return soft_of_chip.create(&x->inner, l, ll, h, hl);
}
static int chip_wrap(void *c, const uint8_t *h, uint32_t hl, uint8_t *e, uint32_t *el,
                     uint8_t z[32])
{
    chip_t *x = c;
    if (x->fail_wrap) return 5;
    int r = soft_of_chip.wrap(&x->inner, h, hl, e, el, z);
    if (x->zero_eph) *el = 0;
    return r;
}
static int chip_unwrap(void *c, const uint8_t *h, uint32_t hl, const uint8_t *e, uint32_t el,
                       uint8_t z[32])
{
    chip_t *x = c;
    x->unwraps++;
    if (x->fail_unwrap) return 5;
    return soft_of_chip.unwrap(&x->inner, h, hl, e, el, z);
}
static int chip_random(void *c, uint8_t *o, uint32_t n)
{
    chip_t *x = c;
    if (x->fail_random) return 5;
    return rng(0, o, n);
}

static void chip_provider(hwk_provider_t *p, chip_t *c, uint8_t secret_byte)
{
    memset(c, 0, sizeof *c);
    memset(c->inner.secret, secret_byte, 32);
    c->inner.rng = rng;
    hwk_soft_provider(&soft_of_chip, &c->inner);
    p->kind = HWK_KIND_TPM;
    p->name = "mock-tpm";
    p->ctx = c;
    p->create = chip_create;
    p->wrap = chip_wrap;
    p->unwrap = chip_unwrap;
    p->random = chip_random;
}

static hwk_sealed_t K, K2;
static uint8_t blob[HWK_ENCODED_MAX + 8], blob2[HWK_ENCODED_MAX + 8];
static uint8_t sig[HWK_SIG_BYTES];
static const uint8_t MSG[] = "pay 12.50 to stall 7";
static const uint8_t CTX[] = "ZXV-TEST";

static void test_soft_round_trip(void)
{
    hwk_soft_t s = {.rng = rng};
    memset(s.secret, 0x11, 32);
    hwk_provider_t p;
    hwk_soft_provider(&p, &s);
    CHECK(hwk_keygen(&p, (const uint8_t *) "acct", 4, &K) == HWK_OK, "software keygen");
    CHECK(K.kind == HWK_KIND_SOFTWARE && K.label_len == 4 && !memcmp(K.label, "acct", 4),
          "kind and label recorded");
    CHECK(hwk_check(&p, &K) == HWK_OK, "seal opens on the same machine");
    CHECK(hwk_sign(&p, &K, MSG, sizeof MSG, CTX, sizeof CTX, sig) == HWK_OK, "sign");
    CHECK(pqm_mldsa87_verify(K.pk, MSG, sizeof MSG, CTX, sizeof CTX, sig),
          "signature verifies under the stored public key");
    CHECK(!pqm_mldsa87_verify(K.pk, MSG, sizeof MSG - 1, CTX, sizeof CTX, sig),
          "and not for another message");
    CHECK(!pqm_mldsa87_verify(K.pk, MSG, sizeof MSG, CTX, sizeof CTX - 1, sig),
          "and not under another context");
    CHECK(hwk_require_hardware(&K) == HWK_ERR_POLICY,
          "software key refused where hardware is required");

    /* another machine: same blob, different file secret */
    hwk_soft_t s2 = {.rng = rng};
    memset(s2.secret, 0x12, 32);
    hwk_provider_t p2;
    hwk_soft_provider(&p2, &s2);
    CHECK(hwk_check(&p2, &K) == HWK_ERR_AUTH, "another machine's secret cannot open the seal");
    CHECK(hwk_sign(&p2, &K, MSG, sizeof MSG, CTX, sizeof CTX, sig) == HWK_ERR_AUTH,
          "nor sign with it");

    /* same label, same machine: a different seed each time */
    CHECK(hwk_keygen(&p, (const uint8_t *) "acct", 4, &K2) == HWK_OK &&
              memcmp(K.pk, K2.pk, HWK_PK_BYTES) != 0 && memcmp(K.eph, K2.eph, K.eph_len) != 0,
          "every keygen draws a new seed and a new ephemeral");
    uint8_t id1[32], id2[32];
    hwk_key_id(K.pk, id1);
    hwk_key_id(K2.pk, id2);
    CHECK(memcmp(id1, id2, 32) != 0, "key ids differ");
}

static void test_encoding(void)
{
    chip_t c;
    hwk_provider_t p;
    chip_provider(&p, &c, 0x21);
    CHECK(hwk_keygen(&p, (const uint8_t *) "till-1", 6, &K) == HWK_OK, "chip keygen");
    CHECK(hwk_require_hardware(&K) == HWK_OK, "chip key passes the hardware policy");
    uint32_t n = hwk_encode(&K, blob, sizeof blob);
    CHECK(n == 12u + 6 + K.handle_len + K.eph_len + 64u + HWK_PK_BYTES, "encoded length");
    CHECK(hwk_encode(&K, blob2, n - 1) == 0, "encode refuses a short buffer");
    CHECK(hwk_decode(blob, n, &K2) == HWK_OK && hwk_encode(&K2, blob2, sizeof blob2) == n &&
              !memcmp(blob, blob2, n),
          "decode(encode(k)) round trips byte for byte");
    CHECK(hwk_check(&p, &K2) == HWK_OK, "a decoded key opens");
    CHECK(hwk_decode(blob, n - 1, &K2) == HWK_ERR_FORMAT, "truncated refused");
    CHECK(hwk_decode(blob, n + 1, &K2) == HWK_ERR_FORMAT, "trailing byte refused");
    CHECK(hwk_decode(blob, 11, &K2) == HWK_ERR_FORMAT, "header-only refused");
    memcpy(blob2, blob, n);
    blob2[0] = 'X';
    CHECK(hwk_decode(blob2, n, &K2) == HWK_ERR_FORMAT, "wrong magic refused");
    memcpy(blob2, blob, n);
    blob2[4] = 9;
    CHECK(hwk_decode(blob2, n, &K2) == HWK_ERR_FORMAT, "unknown kind refused");
    memcpy(blob2, blob, n);
    blob2[4] = 0;
    CHECK(hwk_decode(blob2, n, &K2) == HWK_ERR_FORMAT, "kind NONE refused");
    memcpy(blob2, blob, n);
    blob2[5] = HWK_LABEL_MAX + 1;
    CHECK(hwk_decode(blob2, n, &K2) == HWK_ERR_FORMAT, "oversized label refused");
    memcpy(blob2, blob, n);
    blob2[7] = 0xff;
    CHECK(hwk_decode(blob2, n, &K2) == HWK_ERR_FORMAT, "oversized handle refused");
    memcpy(blob2, blob, n);
    blob2[8] = 0;
    blob2[9] = 0;
    CHECK(hwk_decode(blob2, n, &K2) == HWK_ERR_FORMAT, "empty ephemeral refused");
    memcpy(blob2, blob, n);
    blob2[10] = 1;
    CHECK(hwk_decode(blob2, n, &K2) == HWK_ERR_FORMAT, "reserved bytes must be zero");
    CHECK(hwk_decode(NULL, n, &K2) == HWK_ERR_ARG && hwk_decode(blob, n, NULL) == HWK_ERR_ARG,
          "NULL refused");
    CHECK(hwk_encode(NULL, blob2, sizeof blob2) == 0, "encode NULL refused");

    /* K4: every region of the stored blob is covered */
    uint32_t off_label = 12, off_handle = off_label + K.label_len,
             off_eph = off_handle + K.handle_len, off_ct = off_eph + K.eph_len,
             off_tag = off_ct + 32, off_pk = off_tag + 32;
    uint32_t at[] = {off_label, off_handle, off_eph, off_ct, off_tag, off_pk, n - 1};
    const char *what[] = {"label", "handle",     "ephemeral",           "ciphertext",
                          "tag",   "public key", "last public-key byte"};
    for (unsigned i = 0; i < sizeof at / sizeof at[0]; i++) {
        memcpy(blob2, blob, n);
        blob2[at[i]] ^= 0x01;
        char m[80];
        snprintf(m, sizeof m, "a flipped %s byte fails closed", what[i]);
        CHECK(hwk_decode(blob2, n, &K2) == HWK_OK && hwk_check(&p, &K2) == HWK_ERR_AUTH, m);
        CHECK(hwk_sign(&p, &K2, MSG, sizeof MSG, 0, 0, sig) == HWK_ERR_AUTH, "and cannot sign");
    }

    /* a public key swapped in from another sealed key fails, even re-MACed by nobody */
    hwk_sealed_t other;
    CHECK(hwk_keygen(&p, (const uint8_t *) "till-2", 6, &other) == HWK_OK, "second chip key");
    memcpy(&K2, &K, sizeof K);
    memcpy(K2.pk, other.pk, HWK_PK_BYTES);
    CHECK(hwk_check(&p, &K2) == HWK_ERR_AUTH, "public key swap detected");

    /* a software provider cannot open a chip key (kind mismatch) */
    hwk_provider_t sp;
    hwk_soft_provider(&sp, &c.inner);
    CHECK(hwk_check(&sp, &K) == HWK_ERR_AUTH, "provider kind must match the sealing kind");
}

static int contains(const uint8_t *hay, uint32_t n, const uint8_t *needle, uint32_t m)
{
    for (uint32_t i = 0; i + m <= n; i++)
        if (!memcmp(hay + i, needle, m)) return 1;
    return 0;
}

static void test_seed_never_stored(void)
{
    chip_t c;
    hwk_provider_t p;
    chip_provider(&p, &c, 0x31);
    n_drawn = 0;
    CHECK(hwk_keygen(&p, (const uint8_t *) "x", 1, &K) == HWK_OK, "keygen");
    /* soft wrap draws the 32-byte ephemeral first, then keygen draws the seed */
    CHECK(n_drawn == 64, "two draws: ephemeral, then seed");
    const uint8_t *seed = drawn + 32;
    uint32_t n = hwk_encode(&K, blob, sizeof blob);
    int found = 0;
    for (uint32_t w = 0; w + 8 <= 32; w++) found |= contains(blob, n, seed + w, 8);
    CHECK(!found, "no 8-byte window of the seed appears in the stored blob");
    CHECK(contains(blob, n, drawn, 32), "the ephemeral (public) is stored, as designed");
    uint8_t pk[HWK_PK_BYTES], sk[PQM_MLDSA87_SK_BYTES];
    pqm_mldsa87_keygen(seed, pk, sk);
    CHECK(!memcmp(pk, K.pk, HWK_PK_BYTES), "the stored public key is the seed's");
    found = 0;
    for (uint32_t w = 0; w + 16 <= 64; w += 16) found |= contains(blob, n, sk + 64 + w, 16);
    CHECK(!found, "no secret-key material in the blob");
    unsigned before = c.unwraps;
    CHECK(hwk_sign(&p, &K, MSG, sizeof MSG, 0, 0, sig) == HWK_OK && c.unwraps == before + 1,
          "each signature asks the chip once");
}

static void test_provider_failures(void)
{
    chip_t c;
    hwk_provider_t p;
    chip_provider(&p, &c, 0x41);
    c.unavailable = 1;
    CHECK(hwk_keygen(&p, 0, 0, &K) == HWK_ERR_UNAVAILABLE, "no chip reported as unavailable");
    c.unavailable = 0;
    c.fail_create = 1;
    CHECK(hwk_keygen(&p, 0, 0, &K) == HWK_ERR_HW, "create failure");
    c.fail_create = 0;
    c.fail_wrap = 1;
    CHECK(hwk_keygen(&p, 0, 0, &K) == HWK_ERR_HW, "wrap failure");
    uint8_t z[sizeof K];
    memset(z, 0, sizeof z);
    CHECK(!memcmp(&K, z, sizeof K), "a failed keygen leaves nothing behind");
    c.fail_wrap = 0;
    c.zero_eph = 1;
    CHECK(hwk_keygen(&p, 0, 0, &K) == HWK_ERR_HW, "empty ephemeral from the chip refused");
    c.zero_eph = 0;
    c.fail_random = 1;
    CHECK(hwk_keygen(&p, 0, 0, &K) == HWK_ERR_HW, "random failure");
    c.fail_random = 0;
    CHECK(hwk_keygen(&p, 0, 0, &K) == HWK_OK && K.label_len == 0, "empty label is fine");
    c.fail_unwrap = 1;
    CHECK(hwk_sign(&p, &K, MSG, sizeof MSG, 0, 0, sig) == HWK_ERR_HW, "unwrap failure");
    c.fail_unwrap = 0;
    c.fail_random = 1;
    CHECK(hwk_sign(&p, &K, MSG, sizeof MSG, 0, 0, sig) == HWK_ERR_HW,
          "sign needs fresh randomness (hedged)");
    c.fail_random = 0;

    uint8_t big[300];
    memset(big, 'c', sizeof big);
    CHECK(hwk_sign(&p, &K, MSG, sizeof MSG, big, 256, sig) == HWK_ERR_ARG, "context over 255");
    CHECK(hwk_sign(&p, &K, MSG, sizeof MSG, big, 255, sig) == HWK_OK, "context of 255 is fine");
    CHECK(hwk_sign(&p, &K, NULL, 3, 0, 0, sig) == HWK_ERR_ARG, "NULL message with length");
    CHECK(hwk_sign(&p, &K, NULL, 0, 0, 0, sig) == HWK_OK, "empty message");
    CHECK(hwk_keygen(&p, big, HWK_LABEL_MAX + 1, &K2) == HWK_ERR_ARG, "label over 64");
    CHECK(hwk_keygen(NULL, 0, 0, &K2) == HWK_ERR_ARG && hwk_keygen(&p, 0, 0, NULL) == HWK_ERR_ARG,
          "NULL refused");
    hwk_provider_t q = p;
    q.random = 0;
    CHECK(hwk_keygen(&q, 0, 0, &K2) == HWK_ERR_ARG, "incomplete provider refused");
    q = p;
    q.kind = HWK_KIND_NONE;
    CHECK(hwk_keygen(&q, 0, 0, &K2) == HWK_ERR_ARG, "provider kind NONE refused");
    CHECK(hwk_require_hardware(NULL) == HWK_ERR_ARG, "policy on NULL");
    K2.kind = HWK_KIND_SECURE_ENCLAVE;
    CHECK(hwk_require_hardware(&K2) == HWK_OK, "Secure Enclave passes the hardware policy");
}

/* ---- hwkey.h K3/K4 written out independently --------------------------- */
static uint32_t enc_by_hand(const hwk_sealed_t *k, uint8_t *o, int zero_tag)
{
    uint32_t n = 0;
    memcpy(o, "ZHK1", 4);
    n = 4;
    o[n++] = k->kind;
    o[n++] = k->label_len;
    o[n++] = (uint8_t) (k->handle_len & 0xff);
    o[n++] = (uint8_t) (k->handle_len >> 8);
    o[n++] = (uint8_t) (k->eph_len & 0xff);
    o[n++] = (uint8_t) (k->eph_len >> 8);
    o[n++] = 0;
    o[n++] = 0;
    memcpy(o + n, k->label, k->label_len);
    n += k->label_len;
    memcpy(o + n, k->handle, k->handle_len);
    n += k->handle_len;
    memcpy(o + n, k->eph, k->eph_len);
    n += k->eph_len;
    memcpy(o + n, k->ct, 32);
    n += 32;
    if (zero_tag)
        memset(o + n, 0, 32);
    else
        memcpy(o + n, k->tag, 32);
    n += 32;
    memcpy(o + n, k->pk, HWK_PK_BYTES);
    return n + HWK_PK_BYTES;
}

static void kek_by_hand(const hwk_sealed_t *k, const uint8_t z[32], uint8_t kek[64])
{
    static uint8_t b[2048];
    uint32_t n = 0;
    memcpy(b, "ZXV-HWK1-KEK", 12);
    n = 12;
    b[n++] = k->kind;
    b[n++] = (uint8_t) (k->handle_len & 0xff);
    b[n++] = (uint8_t) (k->handle_len >> 8);
    memcpy(b + n, k->handle, k->handle_len);
    n += k->handle_len;
    b[n++] = (uint8_t) (k->eph_len & 0xff);
    b[n++] = (uint8_t) (k->eph_len >> 8);
    memcpy(b + n, k->eph, k->eph_len);
    n += k->eph_len;
    memcpy(b + n, z, 32);
    shake256(b, n + 32, kek, 64);
}

static void tag_by_hand(const hwk_sealed_t *k, const uint8_t mackey[32], uint8_t tag[32])
{
    static uint8_t b[32 + 12 + HWK_ENCODED_MAX];
    memcpy(b, mackey, 32);
    memcpy(b + 32, "ZXV-HWK1-TAG", 12);
    uint32_t n = enc_by_hand(k, b + 44, 1);
    shake256(b, 44 + n, tag, 32);
}

/* hwkey_soft.c's Z and handle, written out */
static void soft_z_by_hand(const uint8_t secret[32], const hwk_sealed_t *k, uint8_t z[32])
{
    uint8_t b[12 + 32 + 16 + 32];
    memcpy(b, "ZXV-HWKS-Z  ", 12);
    memcpy(b + 12, secret, 32);
    memcpy(b + 44, k->handle, 16);
    memcpy(b + 60, k->eph, 32);
    shake256(b, sizeof b, z, 32);
}

static void test_format_by_hand(void)
{
    hwk_soft_t s = {.rng = rng};
    memset(s.secret, 0x51, 32);
    hwk_provider_t p;
    hwk_soft_provider(&p, &s);
    n_drawn = 0;
    CHECK(hwk_keygen(&p, (const uint8_t *) "shop", 4, &K) == HWK_OK, "keygen");
    uint8_t idb[12 + 32 + 4], h[16];
    memcpy(idb, "ZXV-HWKS-ID ", 12);
    memcpy(idb + 12, s.secret, 32);
    memcpy(idb + 44, "shop", 4);
    shake256(idb, sizeof idb, h, 16);
    CHECK(K.handle_len == 16 && !memcmp(K.handle, h, 16), "software handle = SHAKE256(id domain)");
    CHECK(K.eph_len == 32 && !memcmp(K.eph, drawn, 32), "ephemeral is the first draw");
    uint8_t z[32], kek[64], tag[32], seed[32];
    soft_z_by_hand(s.secret, &K, z);
    kek_by_hand(&K, z, kek);
    for (int i = 0; i < 32; i++) seed[i] = K.ct[i] ^ kek[i];
    CHECK(!memcmp(seed, drawn + 32, 32), "K3: ct = seed XOR pad, pad from the KDF as specified");
    tag_by_hand(&K, kek + 32, tag);
    CHECK(!memcmp(tag, K.tag, 32), "K4: tag = SHAKE256(mac key || domain || blob, tag zeroed)");
    uint32_t n = hwk_encode(&K, blob, sizeof blob);
    CHECK(n == enc_by_hand(&K, blob2, 0) && !memcmp(blob, blob2, n), "encoding as specified");
    CHECK(hwk_encode(&K, blob2, n) == n, "an exact-size buffer is enough");

    /* a public key swapped in WITH a valid tag (forged by someone holding Z)
     * is still caught: the seed's own public key must match */
    hwk_sealed_t other;
    CHECK(hwk_keygen(&p, (const uint8_t *) "shop2", 5, &other) == HWK_OK, "second key");
    memcpy(&K2, &K, sizeof K);
    memcpy(K2.pk, other.pk, HWK_PK_BYTES);
    tag_by_hand(&K2, kek + 32, K2.tag);
    CHECK(hwk_check(&p, &K2) == HWK_ERR_AUTH, "K4: re-MACed foreign public key refused");

    /* fields at their limits round trip; one past is refused */
    memset(&K2, 0, sizeof K2);
    K2.kind = HWK_KIND_SECURE_ENCLAVE;
    K2.label_len = HWK_LABEL_MAX;
    K2.handle_len = HWK_HANDLE_MAX;
    K2.eph_len = HWK_EPH_MAX;
    for (unsigned i = 0; i < HWK_HANDLE_MAX; i++) K2.handle[i] = (uint8_t) (i * 7);
    for (unsigned i = 0; i < HWK_EPH_MAX; i++) K2.eph[i] = (uint8_t) (i * 3 + 1);
    for (unsigned i = 0; i < HWK_LABEL_MAX; i++) K2.label[i] = (uint8_t) ('a' + i % 26);
    n = hwk_encode(&K2, blob, sizeof blob);
    CHECK(n == HWK_ENCODED_MAX && n == enc_by_hand(&K2, blob2, 0) && !memcmp(blob, blob2, n),
          "largest blob encodes as specified (two-byte lengths, high byte set)");
    hwk_sealed_t K3;
    CHECK(hwk_decode(blob, n, &K3) == HWK_OK && !memcmp(&K3, &K2, sizeof K2),
          "and decodes back to the same fields");
    K2.label_len = HWK_LABEL_MAX + 1;
    CHECK(hwk_encode(&K2, blob, sizeof blob) == 0, "label one past the limit");
    K2.label_len = HWK_LABEL_MAX;
    K2.handle_len = HWK_HANDLE_MAX + 1;
    CHECK(hwk_encode(&K2, blob, sizeof blob) == 0, "handle one past the limit");
    K2.handle_len = HWK_HANDLE_MAX;
    K2.eph_len = HWK_EPH_MAX + 1;
    CHECK(hwk_encode(&K2, blob, sizeof blob) == 0, "ephemeral one past the limit");
    K2.eph_len = HWK_EPH_MAX;
    K2.kind = HWK_KIND_SOFTWARE + 1;
    CHECK(hwk_encode(&K2, blob, sizeof blob) == 0, "kind past SOFTWARE");
    K2.kind = HWK_KIND_SOFTWARE;
    CHECK(hwk_encode(&K2, blob, sizeof blob) == n, "kind SOFTWARE is valid");
    blob[11] = 1;
    CHECK(hwk_decode(blob, n, &K3) == HWK_ERR_FORMAT, "second reserved byte must be zero");
    K2.label_len = HWK_LABEL_MAX + 1;
    CHECK(hwk_check(&p, &K2) == HWK_ERR_FORMAT, "opening refuses out-of-range fields");
    CHECK(hwk_check(NULL, &K) == HWK_ERR_ARG && hwk_check(&p, NULL) == HWK_ERR_ARG,
          "opening refuses NULL");

    uint8_t lab[HWK_LABEL_MAX];
    memset(lab, 'L', sizeof lab);
    CHECK(hwk_keygen(&p, lab, HWK_LABEL_MAX, &K2) == HWK_OK && K2.label_len == HWK_LABEL_MAX,
          "a 64-byte label is allowed");
    CHECK(hwk_keygen(&p, NULL, 1, &K2) == HWK_ERR_ARG, "NULL label with a length");
}

/* chip switches for odd lengths */
static uint32_t force_hl, force_el;
static int odd_create(void *c, const uint8_t *l, uint32_t ll, uint8_t *h, uint32_t *hl)
{
    int r = chip_create(c, l, ll, h, hl);
    if (force_hl) *hl = force_hl;
    return r;
}
static int odd_wrap(void *c, const uint8_t *h, uint32_t hl, uint8_t *e, uint32_t *el, uint8_t z[32])
{
    int r = chip_wrap(c, h, hl, e, el, z);
    if (force_el) *el = force_el;
    return r;
}

static void test_odd_lengths(void)
{
    chip_t c;
    hwk_provider_t p;
    chip_provider(&p, &c, 0x61);
    p.create = odd_create;
    p.wrap = odd_wrap;
    force_hl = HWK_HANDLE_MAX + 1;
    CHECK(hwk_keygen(&p, 0, 0, &K2) == HWK_ERR_HW, "a chip handle over 1024 bytes is refused");
    force_hl = 0;
    force_el = HWK_EPH_MAX + 1;
    CHECK(hwk_keygen(&p, 0, 0, &K2) == HWK_ERR_HW, "a chip ephemeral over 160 bytes is refused");
    force_el = HWK_EPH_MAX; /* the extra bytes are whatever the chip left: still sealed and MACed */
    CHECK(hwk_keygen(&p, 0, 0, &K2) == HWK_OK && K2.eph_len == HWK_EPH_MAX,
          "a 160-byte ephemeral is accepted");
    force_el = 0;
    memset(sig, 0xaa, sizeof sig);
    CHECK(hwk_keygen(&p, 0, 0, &K2) == HWK_OK, "keygen");
    c.fail_random = 1;
    CHECK(hwk_sign(&p, &K2, MSG, sizeof MSG, 0, 0, sig) == HWK_ERR_HW && sig[0] == 0xaa &&
              sig[HWK_SIG_BYTES - 1] == 0xaa,
          "no signature is written when the randomness fails");
    c.fail_random = 0;
    c.fail_unwrap = 1;
    CHECK(hwk_sign(&p, &K2, MSG, sizeof MSG, 0, 0, sig) == HWK_ERR_HW && sig[0] == 0xaa,
          "nor when the chip refuses");
    c.fail_unwrap = 0;
}

/* a chip with the largest handle and ephemeral the format allows (the
 * software key underneath uses the first 16 and 32 bytes) */
static hwk_soft_t wide_soft;
static hwk_provider_t wide_inner;
static uint32_t wide_hl = HWK_HANDLE_MAX;
static int wide_create(void *c, const uint8_t *l, uint32_t ll, uint8_t *h, uint32_t *hl)
{
    (void) c;
    uint32_t n = 16;
    if (wide_inner.create(&wide_soft, l, ll, h, &n)) return -1;
    for (uint32_t i = 16; i < wide_hl; i++) h[i] = (uint8_t) (i ^ 0x5a);
    *hl = wide_hl;
    return 0;
}
static int wide_wrap(void *c, const uint8_t *h, uint32_t hl, uint8_t *e, uint32_t *el,
                     uint8_t z[32])
{
    (void) c;
    (void) hl;
    uint32_t n = 32;
    if (wide_inner.wrap(&wide_soft, h, 16, e, &n, z)) return -1;
    for (uint32_t i = 32; i < HWK_EPH_MAX; i++) e[i] = (uint8_t) (i * 5);
    *el = HWK_EPH_MAX;
    return 0;
}
static int wide_unwrap(void *c, const uint8_t *h, uint32_t hl, const uint8_t *e, uint32_t el,
                       uint8_t z[32])
{
    (void) c;
    (void) hl;
    (void) el;
    return wide_inner.unwrap(&wide_soft, h, 16, e, 32, z);
}

static void test_widest(void)
{
    memset(&wide_soft, 0, sizeof wide_soft);
    memset(wide_soft.secret, 0x71, 32);
    wide_soft.rng = rng;
    hwk_soft_provider(&wide_inner, &wide_soft);
    hwk_provider_t p = {
        HWK_KIND_SECURE_ENCLAVE, "wide", 0, wide_create, wide_wrap, wide_unwrap, chip_random};
    chip_t c;
    memset(&c, 0, sizeof c);
    p.ctx = &c;
    uint8_t lab[HWK_LABEL_MAX];
    memset(lab, 'w', sizeof lab);
    n_drawn = 0;
    CHECK(hwk_keygen(&p, lab, sizeof lab, &K) == HWK_OK && K.handle_len == HWK_HANDLE_MAX &&
              K.eph_len == HWK_EPH_MAX,
          "largest handle, ephemeral and label");
    uint8_t z[32], kek[64], tag[32], seed[32];
    soft_z_by_hand(wide_soft.secret, &K, z);
    kek_by_hand(&K, z, kek);
    for (int i = 0; i < 32; i++) seed[i] = K.ct[i] ^ kek[i];
    CHECK(!memcmp(seed, drawn + 32, 32), "K3 with two-byte lengths above 255");
    tag_by_hand(&K, kek + 32, tag);
    CHECK(!memcmp(tag, K.tag, 32), "K4 over the largest blob");
    CHECK(hwk_check(&p, &K) == HWK_OK && hwk_sign(&p, &K, MSG, sizeof MSG, 0, 0, sig) == HWK_OK,
          "opens and signs");
    wide_hl = HWK_HANDLE_MAX + 1;
    CHECK(hwk_keygen(&p, 0, 0, &K2) == HWK_ERR_HW, "a 1025-byte handle from the chip is refused");
    wide_hl = HWK_HANDLE_MAX;
    memset(&K2, 0, sizeof K2);
    K2.kind = HWK_KIND_TPM;
    K2.eph_len = 1;
    uint32_t n = hwk_encode(&K2, blob, sizeof blob);
    CHECK(n > 0 && hwk_decode(blob, n, &K2) == HWK_OK, "a one-byte ephemeral is a valid field");
}

static void test_helpers(void)
{
    uint8_t a[5] = {1, 2, 3, 4, 5}, b[5] = {1, 2, 3, 4, 5};
    CHECK(hwk_ct_eq(a, b, 5), "equal");
    b[4] = 6;
    CHECK(!hwk_ct_eq(a, b, 5) && hwk_ct_eq(a, b, 4), "last byte matters");
    b[0] = 0;
    CHECK(!hwk_ct_eq(a, b, 1), "first byte matters");
    hwk_wipe(a, 5);
    CHECK(!a[0] && !a[4], "wipe");
}

int main(void)
{
    test_soft_round_trip();
    test_encoding();
    test_seed_never_stored();
    test_provider_failures();
    test_format_by_hand();
    test_odd_lengths();
    test_widest();
    test_helpers();
    printf("%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
