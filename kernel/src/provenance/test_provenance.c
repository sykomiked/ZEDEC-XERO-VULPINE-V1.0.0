/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_provenance.c — the canonical manifest encoding (zx_provenance.h).
 *
 *   1. The digest equals SHA-256 of a preimage written out byte by byte here
 *      from the format description, so the encoding is pinned, not just
 *      self-consistent.
 *   2. Every field changes the digest, and moving bytes between adjacent fields
 *      (the classic concatenation ambiguity) changes it too.
 *   3. Malformed manifests are refused with a zero digest.
 */
#include <stdio.h>
#include <string.h>
#include "zx_provenance.h"

static int failures = 0, checks = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

static uint32_t put_u32(uint8_t *o, uint32_t v)
{
    o[0] = (uint8_t) (v >> 24);
    o[1] = (uint8_t) (v >> 16);
    o[2] = (uint8_t) (v >> 8);
    o[3] = (uint8_t) v;
    return 4;
}
static uint32_t put_lp(uint8_t *o, const void *p, uint32_t n)
{
    put_u32(o, n);
    memcpy(o + 4, p, n);
    return 4 + n;
}

static void digest_of(const char *id, uint64_t ver, const char *arch, const char *d0,
                      const char *d1, uint32_t nd, const uint8_t *cid, uint32_t cl, uint8_t out[32])
{
    zxp_bytes_t d[2] = {zxp_str(d0), zxp_str(d1)};
    zxp_manifest_t m = {zxp_str(id), ver, zxp_str(arch), d, nd, zxp_mem(cid, cl)};
    zxp_manifest_digest(ZXP_DOMAIN_UPDATE, &m, out);
}

int main(void)
{
    printf("=== provenance: canonical length-prefixed manifest digest ===\n");
    uint8_t cid[32];
    for (int i = 0; i < 32; i++) cid[i] = (uint8_t) (0xC0 + i);

    /* 1. known preimage, built by hand */
    {
        uint8_t pre[512];
        uint32_t n = 0;
        n += put_lp(pre + n, "zxv-provenance-v1", 17);
        n += put_lp(pre + n, "zxv-update-v1", 13);
        n += put_lp(pre + n, "org.zxv.shell", 13);
        n += put_u32(pre + n, 0x00000001u); /* version 0x0000000100000002, big-endian */
        n += put_u32(pre + n, 0x00000002u);
        n += put_lp(pre + n, "aarch64", 7);
        n += put_u32(pre + n, 2); /* two dependencies */
        n += put_lp(pre + n, "libc0", 5);
        n += put_lp(pre + n, "libtri", 6);
        n += put_lp(pre + n, cid, 32);
        uint8_t want[32], got[32];
        sha256(pre, n, want);
        digest_of("org.zxv.shell", 0x0000000100000002ull, "aarch64", "libc0", "libtri", 2, cid, 32,
                  got);
        CHECK(memcmp(want, got, 32) == 0,
              "digest == SHA-256 of the documented preimage (lp fields, u64 BE version)");
    }

    /* 2. each field matters; boundaries are unambiguous */
    {
        uint8_t base[32], x[32];
        digest_of("pkg", 7, "x86_64", "a", "b", 2, cid, 32, base);
        digest_of("pkh", 7, "x86_64", "a", "b", 2, cid, 32, x);
        CHECK(memcmp(base, x, 32) != 0, "PackageID changes the digest");
        digest_of("pkg", 8, "x86_64", "a", "b", 2, cid, 32, x);
        CHECK(memcmp(base, x, 32) != 0, "Version changes the digest");
        digest_of("pkg", 7ull | (1ull << 40), "x86_64", "a", "b", 2, cid, 32, x);
        CHECK(memcmp(base, x, 32) != 0, "the high 32 bits of Version are covered");
        digest_of("pkg", 7, "aarch64", "a", "b", 2, cid, 32, x);
        CHECK(memcmp(base, x, 32) != 0, "Architecture changes the digest");
        digest_of("pkg", 7, "x86_64", "a", "c", 2, cid, 32, x);
        CHECK(memcmp(base, x, 32) != 0, "a Dependency changes the digest");
        digest_of("pkg", 7, "x86_64", "b", "a", 2, cid, 32, x);
        CHECK(memcmp(base, x, 32) != 0, "Dependency order changes the digest");
        digest_of("pkg", 7, "x86_64", "a", "b", 1, cid, 32, x);
        CHECK(memcmp(base, x, 32) != 0, "the Dependency count changes the digest");
        uint8_t cid2[32];
        memcpy(cid2, cid, 32);
        cid2[31] ^= 1;
        digest_of("pkg", 7, "x86_64", "a", "b", 2, cid2, 32, x);
        CHECK(memcmp(base, x, 32) != 0, "the ContentCID changes the digest");
        digest_of("pkg", 7, "x86_64", "a", "b", 2, cid, 31, x);
        CHECK(memcmp(base, x, 32) != 0, "the ContentCID length is covered");

        uint8_t p[32], q[32];
        digest_of("ab", 1, "c", "", "", 0, cid, 32, p);
        digest_of("a", 1, "bc", "", "", 0, cid, 32, q);
        CHECK(memcmp(p, q, 32) != 0, "id \"ab\"+arch \"c\" != id \"a\"+arch \"bc\"");
        digest_of("pkg", 1, "any", "ab", "c", 2, cid, 32, p);
        digest_of("pkg", 1, "any", "a", "bc", 2, cid, 32, q);
        CHECK(memcmp(p, q, 32) != 0, "deps {\"ab\",\"c\"} != deps {\"a\",\"bc\"}");

        /* the domain separates modules */
        zxp_manifest_t m = {zxp_str("pkg"), 7, zxp_str("x86_64"), 0, 0, zxp_mem(cid, 32)};
        uint8_t u[32], b[32];
        zxp_manifest_digest(ZXP_DOMAIN_UPDATE, &m, u);
        zxp_manifest_digest(ZXP_DOMAIN_ZXPKG, &m, b);
        CHECK(memcmp(u, b, 32) != 0, "a digest for one domain is not valid in another");
    }

    /* 3. malformed manifests */
    {
        uint8_t out[32], zero[32] = {0};
        zxp_manifest_t m = {zxp_str(""), 1, zxp_str("any"), 0, 0, zxp_mem(cid, 32)};
        CHECK(!zxp_manifest_digest(ZXP_DOMAIN_UPDATE, &m, out) && memcmp(out, zero, 32) == 0,
              "an empty PackageID is refused (zero digest)");
        m.package_id = zxp_str("p");
        m.arch = zxp_str("");
        CHECK(!zxp_manifest_digest(ZXP_DOMAIN_UPDATE, &m, out), "an empty Architecture is refused");
        m.arch = zxp_str("any");
        m.cid = zxp_mem(0, 0);
        CHECK(!zxp_manifest_digest(ZXP_DOMAIN_UPDATE, &m, out), "an empty ContentCID is refused");
        m.cid = zxp_mem(cid, 32);
        m.n_deps = 3;
        m.deps = 0;
        CHECK(!zxp_manifest_digest(ZXP_DOMAIN_UPDATE, &m, out),
              "a dependency count without a list is refused");
        m.n_deps = ZXP_MAX_DEPS + 1;
        CHECK(!zxp_manifest_digest(ZXP_DOMAIN_UPDATE, &m, out), "too many dependencies refused");
    }

    printf("\n%s: %d of %d checks failed\n", failures ? "*** FAILED ***" : "ALL PASS", failures,
           checks);
    return failures ? 1 : 0;
}
