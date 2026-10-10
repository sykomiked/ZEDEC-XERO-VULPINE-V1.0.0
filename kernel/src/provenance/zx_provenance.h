/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zx_provenance.h — the ONE canonical encoding that update, broker, zxpkg and
 * zx_upcheck signatures are computed over.
 *
 * WHY
 * ---
 * A signature over only a content ID leaves every other field unsigned: an old
 * signed blob can be re-listed under a higher version, another architecture or
 * other dependencies. The owner's decision: a signature covers the complete
 * manifest,
 *
 *     SHA-256( PackageID || Version || Architecture || Dependencies || ContentCID )
 *
 * in a canonical encoding where no two different manifests produce the same
 * bytes. "Concatenate the fields" alone is ambiguous ("ab"+"c" == "a"+"bc"), so
 * every variable-length field is LENGTH-PREFIXED and integers are fixed width.
 *
 * THE ENCODING (all integers big-endian)
 * ---------------------------------------
 *     lp(x)   = u32(len(x)) || x
 *     preimage = lp("zxv-provenance-v1")          fixed tag, version of this format
 *             || lp(domain)                        who signs: update / broker / ...
 *             || lp(PackageID)
 *             || u64(Version)                      strictly monotonic per PackageID
 *             || lp(Architecture)                  e.g. "aarch64", "x86_64", "any"
 *             || u32(n_deps) || lp(dep_0) || ... || lp(dep_{n-1})
 *             || lp(ContentCID)
 *     digest   = SHA-256(preimage)
 *
 * The digest is what the publisher signs (Ed25519 for update/broker/ZSP, ML-DSA
 * for zx_upcheck manifests). The domain keeps a signature made for one module
 * from being accepted by another. Dependencies are signed in the order given;
 * a publisher that wants an order-independent list must sort before signing,
 * and the verifier recomputes from the stored order, so reordering breaks the
 * signature rather than silently matching.
 *
 * zxp_canon_* is the general encoder (used directly by the broker for its
 * listing terms); zxp_manifest_digest is the manifest form above. Header-only
 * (static inline) so every module that already links SHA-256 can use it
 * without a new object in each architecture's makefile.
 *
 * Freestanding: integer only, no libc, no allocation.
 */
#ifndef ZXV_PROVENANCE_H
#define ZXV_PROVENANCE_H

#include <stdint.h>
#include <stdbool.h>
#include "../robin_debanks/sha256.h"

#define ZXP_DIGEST_LEN 32u
#define ZXP_FORMAT_TAG "zxv-provenance-v1"
#define ZXP_MAX_DEPS   64u

/* Domains (the module that signs). */
#define ZXP_DOMAIN_UPDATE "zxv-update-v1"
#define ZXP_DOMAIN_BROKER "zxv-broker-listing-v1"
#define ZXP_DOMAIN_ZXPKG  "zxv-zxpkg-release-v1"
#define ZXP_DOMAIN_UPMAN  "zxv-upcheck-entry-v1"

typedef struct {
    sha256_ctx_t h;
} zxp_canon_t;

typedef struct {
    const uint8_t *p;
    uint32_t len;
} zxp_bytes_t;

typedef struct {
    zxp_bytes_t package_id;
    uint64_t version;
    zxp_bytes_t arch;
    const zxp_bytes_t *deps; /* n_deps entries (may be NULL when n_deps == 0) */
    uint32_t n_deps;
    zxp_bytes_t cid;
} zxp_manifest_t;

static inline uint32_t zxp_strlen(const char *s)
{
    uint32_t n = 0;
    if (s)
        while (s[n]) n++;
    return n;
}

static inline zxp_bytes_t zxp_str(const char *s)
{
    zxp_bytes_t b;
    b.p = (const uint8_t *) s;
    b.len = zxp_strlen(s);
    return b;
}

static inline zxp_bytes_t zxp_mem(const void *p, uint32_t len)
{
    zxp_bytes_t b;
    b.p = (const uint8_t *) p;
    b.len = len;
    return b;
}

static inline void zxp_canon_u32(zxp_canon_t *c, uint32_t v)
{
    uint8_t b[4] = {(uint8_t) (v >> 24), (uint8_t) (v >> 16), (uint8_t) (v >> 8), (uint8_t) v};
    sha256_update(&c->h, b, 4);
}

static inline void zxp_canon_u64(zxp_canon_t *c, uint64_t v)
{
    zxp_canon_u32(c, (uint32_t) (v >> 32));
    zxp_canon_u32(c, (uint32_t) v);
}

/* One length-prefixed field. A NULL pointer with len 0 encodes the empty field. */
static inline void zxp_canon_bytes(zxp_canon_t *c, const uint8_t *p, uint32_t len)
{
    zxp_canon_u32(c, len);
    if (len) sha256_update(&c->h, p, len);
}

static inline void zxp_canon_init(zxp_canon_t *c, const char *domain)
{
    sha256_init(&c->h);
    zxp_canon_bytes(c, (const uint8_t *) ZXP_FORMAT_TAG, zxp_strlen(ZXP_FORMAT_TAG));
    zxp_canon_bytes(c, (const uint8_t *) domain, zxp_strlen(domain));
}

static inline void zxp_canon_final(zxp_canon_t *c, uint8_t out[ZXP_DIGEST_LEN])
{
    sha256_final(&c->h, out);
}

/* The manifest digest. Returns false (and a zero digest) on a malformed
 * manifest: a missing pointer for a non-empty field, more than ZXP_MAX_DEPS
 * dependencies, or an empty PackageID / Architecture / ContentCID. */
static inline bool zxp_manifest_digest(const char *domain, const zxp_manifest_t *m,
                                       uint8_t out[ZXP_DIGEST_LEN])
{
    for (uint32_t i = 0; i < ZXP_DIGEST_LEN; i++) out[i] = 0;
    if (!domain || !m || m->n_deps > ZXP_MAX_DEPS || (m->n_deps && !m->deps)) return false;
    if (!m->package_id.len || !m->package_id.p || !m->arch.len || !m->arch.p || !m->cid.len ||
        !m->cid.p)
        return false;
    for (uint32_t i = 0; i < m->n_deps; i++)
        if (m->deps[i].len && !m->deps[i].p) return false;
    zxp_canon_t c;
    zxp_canon_init(&c, domain);
    zxp_canon_bytes(&c, m->package_id.p, m->package_id.len);
    zxp_canon_u64(&c, m->version);
    zxp_canon_bytes(&c, m->arch.p, m->arch.len);
    zxp_canon_u32(&c, m->n_deps);
    for (uint32_t i = 0; i < m->n_deps; i++) zxp_canon_bytes(&c, m->deps[i].p, m->deps[i].len);
    zxp_canon_bytes(&c, m->cid.p, m->cid.len);
    zxp_canon_final(&c, out);
    return true;
}

#endif /* ZXV_PROVENANCE_H */
