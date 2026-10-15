/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cp_common.h — the signed manifest shared by the compliance plug-ins
 * (capctl, taxhook, kycwrap) and the small integer helpers they use.
 *
 * WHY ONE MANIFEST SHAPE
 * The jurisdiction plug-in ABI (roadmap R3.3, built on another branch as
 * kernel/src/juris) loads userland plug-ins that come with a signed manifest
 * and fail closed. The V-Bill hub's limit rule (kernel/src/vhub, H5/H6) is
 * the first example. These plug-ins do not depend on that unmerged code; they
 * carry the same ingredients so a later adapter can register them there:
 *
 *   M1 HEADER. Every manifest starts with the same 68 bytes, little-endian:
 *        magic "ZXVM" | format u16 (=1) | kind u16 | country[2] (ISO 3166-1
 *        alpha-2, or "**" for "no single country") | region[6] (ISO 3166-2
 *        subdivision suffix, zero padded, may be empty) | version u32 |
 *        not_before u64 | not_after u64 (unix seconds, inclusive) |
 *        signer key id [32].
 *      The body follows; its layout belongs to the kind.
 *   M2 SIGNATURE. The signer signs CP_MSG_LEN bytes:
 *        "zxv/compliance-manifest/v1" || SHA-256(header || body).
 *      The kind and country are inside the hash, so a signature for one
 *      kind or country never fits another. The signature scheme is whatever
 *      the caller's verify hook checks; cpk.h supplies ML-DSA-87 (FIPS 204).
 *   M3 FAIL CLOSED. A manifest is installed only when it decodes exactly
 *      (no trailing bytes), its signature verifies, `now` is inside its
 *      validity window and its version is strictly above the one installed
 *      for the same slot and not below the configured floor. A plug-in that
 *      needs a manifest and has none valid at evaluation time refuses.
 *
 * INTEGERS. No floating point. cp_udivmod64 is a shift-subtract division so
 * the freestanding objects need no 64-bit division helper on 32-bit targets.
 * Byte copies are explicit loops (no libc).
 *
 * HONEST LIMITS. The manifest proves who signed a table and when it is
 * valid. It does not prove the table matches the law; that is the signer's
 * responsibility (a compliance officer or tax adviser per country).
 */
#ifndef ZXV_CP_COMMON_H
#define ZXV_CP_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CP_MAGIC      0x4D56585Au /* "ZXVM" little-endian */
#define CP_FORMAT     1u
#define CP_HDR_LEN    68u
#define CP_KEYID_LEN  32u
#define CP_REGION_LEN 6u
#define CP_DOMAIN     "zxv/compliance-manifest/v1"
#define CP_DOMAIN_LEN 26u
#define CP_MSG_LEN    (CP_DOMAIN_LEN + 32u)

/* Manifest kinds. */
#define CP_KIND_CAPCTL     1u /* capital-control profile (capctl.h) */
#define CP_KIND_TAXTABLE   2u /* tax rate table (taxhook.h) */
#define CP_KIND_KYC_REVOKE 3u /* KYC issuer revocation list (kycwrap.h) */
#define CP_KIND_KYC_TOKEN  4u /* KYC predicate token issuer key (kycwrap.h; not a manifest) */

typedef struct {
    uint16_t kind;
    char country[2];
    char region[CP_REGION_LEN]; /* zero padded */
    uint32_t version;
    uint64_t not_before, not_after;
    uint8_t key_id[CP_KEYID_LEN];
} cp_hdr_t;

/* verify(ctx, hdr, msg, msg_len, sig, sig_len): true only when sig is a
 * valid signature over msg by the key hdr->key_id AND that key is trusted
 * for hdr->kind and hdr->country. NULL refuses every manifest. */
typedef bool (*cp_verify_fn)(void *ctx, const cp_hdr_t *hdr, const uint8_t *msg, size_t msg_len,
                             const uint8_t *sig, size_t sig_len);

/* ---- byte helpers ---------------------------------------------------- */
static inline void cp_copy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *) dst;
    const uint8_t *s = (const uint8_t *) src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}
static inline void cp_zero(void *dst, size_t n)
{
    uint8_t *d = (uint8_t *) dst;
    for (size_t i = 0; i < n; i++) d[i] = 0;
}
static inline bool cp_eq(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *) a, *y = (const uint8_t *) b;
    uint8_t acc = 0;
    for (size_t i = 0; i < n; i++) acc |= (uint8_t) (x[i] ^ y[i]);
    return acc == 0;
}

/* A writer and a reader over a caller buffer. A writer that runs out of
 * room, or a reader that runs past the end, sets bad and stays bad. */
typedef struct {
    uint8_t *p;
    size_t cap, len;
    bool bad;
} cp_w_t;
typedef struct {
    const uint8_t *p;
    size_t len, off;
    bool bad;
} cp_r_t;

void cp_put(cp_w_t *w, const void *src, size_t n);
void cp_put8(cp_w_t *w, uint8_t v);
void cp_put16(cp_w_t *w, uint16_t v);
void cp_put32(cp_w_t *w, uint32_t v);
void cp_put64(cp_w_t *w, uint64_t v);
void cp_get(cp_r_t *r, void *dst, size_t n);
uint8_t cp_get8(cp_r_t *r);
uint16_t cp_get16(cp_r_t *r);
uint32_t cp_get32(cp_r_t *r);
uint64_t cp_get64(cp_r_t *r);

/* ---- integer helpers ------------------------------------------------- */
/* n / d with *rem = n % d; d must be nonzero (returns 0, rem 0 if not). */
uint64_t cp_udivmod64(uint64_t n, uint64_t d, uint64_t *rem);
/* *out = a + b; false (and *out untouched) on overflow. */
static inline bool cp_add(uint64_t a, uint64_t b, uint64_t *out)
{
    if (a > UINT64_MAX - b) return false;
    *out = a + b;
    return true;
}
/* Two capital letters, or "**" when wild is allowed. */
bool cp_cc_ok(const char cc[2], bool wild);

/* ---- manifest header ------------------------------------------------- */
void cp_hdr_put(cp_w_t *w, const cp_hdr_t *h);
/* Reads and checks magic, format and the country code ("**" allowed only
 * when wild). Region bytes must be uppercase letters/digits then zeros. */
bool cp_hdr_get(cp_r_t *r, cp_hdr_t *h, bool wild);
/* M2: the bytes the signer signs, over the whole encoded manifest. */
void cp_manifest_msg(const uint8_t *manifest, size_t len, uint8_t out[CP_MSG_LEN]);
/* M3 signature step: true when verify accepts sig over the manifest. */
bool cp_manifest_verify(cp_verify_fn verify, void *vctx, const cp_hdr_t *h, const uint8_t *manifest,
                        size_t len, const uint8_t *sig, size_t sig_len);

#endif /* ZXV_CP_COMMON_H */
