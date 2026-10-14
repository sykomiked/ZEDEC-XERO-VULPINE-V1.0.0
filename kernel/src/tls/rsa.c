/* rsa.c — RSA verification, PKCS #1 v1.5 and PSS. See rsa.h.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "rsa.h"
#include "bn.h"

#define RSA_MAX_BYTES (RSA_MAX_BITS / 8)

static void strip(const uint8_t **p, uint32_t *len)
{
    while (*len && (*p)[0] == 0) {
        (*p)++;
        (*len)--;
    }
}

bool rsa_key_acceptable(const uint8_t *n_be, uint32_t n_len, const uint8_t *e_be, uint32_t e_len)
{
    if (!n_be || !e_be) return false;
    strip(&n_be, &n_len);
    strip(&e_be, &e_len);
    uint32_t bits = bn_bits_be(n_be, n_len);
    if (bits < RSA_MIN_BITS || bits > RSA_MAX_BITS) return false;
    if ((n_be[n_len - 1] & 1) == 0) return false;
    if (e_len == 0 || e_len > 4) return false;
    if ((e_be[e_len - 1] & 1) == 0) return false;
    if (e_len == 1 && e_be[0] < 3) return false;
    return true;
}

/* m = s^e mod n as exactly k bytes, k the modulus length. Returns k, or 0 if
 * the key or signature is unacceptable. */
static uint32_t rsa_public(const uint8_t *n_be, uint32_t n_len, const uint8_t *e_be, uint32_t e_len,
                           const uint8_t *sig, uint32_t sig_len, uint8_t *em, uint32_t *mod_bits)
{
    if (!rsa_key_acceptable(n_be, n_len, e_be, e_len) || !sig) return 0;
    strip(&n_be, &n_len);
    strip(&e_be, &e_len);
    if (sig_len != n_len) return 0;

    bn_mont_t ctx;
    uint32_t s[BN_LIMBS_MAX], m[BN_LIMBS_MAX];
    if (!bn_mont_init(&ctx, n_be, n_len)) return 0;
    if (!bn_from_be(s, ctx.n, sig, sig_len)) return 0;
    if (bn_cmp(s, ctx.m, ctx.n) >= 0) return 0;
    bn_to_mont(&ctx, m, s);
    bn_mont_pow(&ctx, m, m, e_be, e_len);
    bn_from_mont(&ctx, m, m);
    bn_to_be(em, n_len, m, ctx.n);
    *mod_bits = bn_bits_be(n_be, n_len);
    return n_len;
}

/* DER DigestInfo prefixes (RFC 8017 section 9.2, note 1), NULL parameters. */
static const uint8_t DI_SHA256[] = {0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
                                    0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};
static const uint8_t DI_SHA384[] = {0x30, 0x41, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
                                    0x65, 0x03, 0x04, 0x02, 0x02, 0x05, 0x00, 0x04, 0x30};
static const uint8_t DI_SHA512[] = {0x30, 0x51, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
                                    0x65, 0x03, 0x04, 0x02, 0x03, 0x05, 0x00, 0x04, 0x40};

bool rsa_verify_pkcs1(const uint8_t *n_be, uint32_t n_len, const uint8_t *e_be, uint32_t e_len,
                      tls_hash_t hash, const uint8_t *digest, const uint8_t *sig, uint32_t sig_len)
{
    const uint8_t *di;
    uint32_t di_len, h_len = tls_digest_len(hash);
    if (hash == TLS_HASH_SHA256) {
        di = DI_SHA256;
        di_len = sizeof DI_SHA256;
    } else if (hash == TLS_HASH_SHA384) {
        di = DI_SHA384;
        di_len = sizeof DI_SHA384;
    } else if (hash == TLS_HASH_SHA512) {
        di = DI_SHA512;
        di_len = sizeof DI_SHA512;
    } else
        return false;
    if (!digest) return false;

    uint8_t em[RSA_MAX_BYTES], want[RSA_MAX_BYTES];
    uint32_t bits = 0;
    uint32_t k = rsa_public(n_be, n_len, e_be, e_len, sig, sig_len, em, &bits);
    if (!k) return false;
    uint32_t t_len = di_len + h_len;
    if (k < t_len + 11) return false;

    /* EM = 0x00 || 0x01 || PS (0xff...) || 0x00 || DigestInfo || H */
    uint32_t at = 0;
    want[at++] = 0x00;
    want[at++] = 0x01;
    while (at < k - t_len - 1) want[at++] = 0xff;
    want[at++] = 0x00;
    for (uint32_t i = 0; i < di_len; i++) want[at++] = di[i];
    for (uint32_t i = 0; i < h_len; i++) want[at++] = digest[i];

    uint8_t diff = 0;
    for (uint32_t i = 0; i < k; i++) diff |= (uint8_t) (em[i] ^ want[i]);
    return diff == 0;
}

/* MGF1 (RFC 8017 B.2.1): out ^= MGF1(seed, len). */
static void mgf1_xor(tls_hash_t hash, const uint8_t *seed, uint32_t seed_len, uint8_t *out,
                     uint32_t len)
{
    uint8_t blk[TLS_DIGEST_MAX];
    uint32_t h_len = tls_digest_len(hash);
    for (uint32_t ctr = 0, done = 0; done < len; ctr++) {
        uint8_t c[4] = {(uint8_t) (ctr >> 24), (uint8_t) (ctr >> 16), (uint8_t) (ctr >> 8),
                        (uint8_t) ctr};
        tls_digest3(hash, seed, seed_len, c, 4, 0, 0, blk);
        for (uint32_t i = 0; i < h_len && done < len; i++) out[done++] ^= blk[i];
    }
}

bool rsa_verify_pss(const uint8_t *n_be, uint32_t n_len, const uint8_t *e_be, uint32_t e_len,
                    tls_hash_t hash, const uint8_t *digest, const uint8_t *sig, uint32_t sig_len)
{
    uint32_t h_len = tls_digest_len(hash), s_len = h_len;
    if (!h_len || !digest) return false;

    uint8_t m[RSA_MAX_BYTES];
    uint32_t mod_bits = 0;
    uint32_t k = rsa_public(n_be, n_len, e_be, e_len, sig, sig_len, m, &mod_bits);
    if (!k) return false;

    /* EMSA-PSS-VERIFY (RFC 8017 9.1.2) with emBits = modBits - 1 */
    uint32_t em_bits = mod_bits - 1;
    uint32_t em_len = (em_bits + 7) / 8;
    if (k - em_len > 1) return false;
    if (k > em_len && m[0] != 0) return false;
    uint8_t *em = m + (k - em_len);

    if (em_len < h_len + s_len + 2) return false;
    if (em[em_len - 1] != 0xbc) return false;
    uint32_t db_len = em_len - h_len - 1;
    const uint8_t *h = em + db_len;
    uint32_t top_bits = 8 * em_len - em_bits; /* 0..7 */
    uint8_t top_mask = (uint8_t) (0xff << (8 - top_bits));
    if (top_bits && (em[0] & top_mask)) return false;

    uint8_t db[RSA_MAX_BYTES];
    for (uint32_t i = 0; i < db_len; i++) db[i] = em[i];
    mgf1_xor(hash, h, h_len, db, db_len);
    if (top_bits) db[0] &= (uint8_t) ~top_mask;

    uint32_t ps_len = db_len - s_len - 1;
    for (uint32_t i = 0; i < ps_len; i++)
        if (db[i] != 0) return false;
    if (db[ps_len] != 0x01) return false;

    static const uint8_t zeros[8] = {0};
    uint8_t h2[TLS_DIGEST_MAX];
    tls_digest3(hash, zeros, 8, digest, h_len, db + db_len - s_len, s_len, h2);
    uint8_t diff = 0;
    for (uint32_t i = 0; i < h_len; i++) diff |= (uint8_t) (h[i] ^ h2[i]);
    return diff == 0;
}
