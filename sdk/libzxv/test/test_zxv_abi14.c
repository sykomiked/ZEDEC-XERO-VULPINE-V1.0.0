/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_abi14.c — conformance test of ABI 1.4 (hardware emulation and
 * hardware updates), linked against the shared library only:
 *   - ZHP-1 profiles: host probe, reference R1, presentation, canonical
 *     round trip, private extension, digest, matching;
 *   - the emulation matrix: the standard engines, proof before service,
 *     failover, dependency calls back into the library, shadow disputes,
 *     the memo, placement policy (key-bearing work stays local),
 *     discovery, versioned contracts;
 *   - signed packages (ML-DSA-87 + SLH-DSA): keys, contract packages;
 *   - reconfigurable regions: images loaded through a simulated two-slot
 *     device, conformance failure and S0 quarantine, replay refusal,
 *     events, the audit log, fail-closed when it fills;
 *   - capability gating and the new status codes. */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_api.h"

static int checks, failures;
#define CHECK(c, ...)                                     \
    do {                                                  \
        checks++;                                         \
        if (!(c)) {                                       \
            failures++;                                   \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)
#define OK(call)                                                                   \
    do {                                                                           \
        int32_t rc_ = (call);                                                      \
        CHECK(rc_ == ZXV_OK, "%s -> %s", #call, zxv_status_name(rc_));             \
    } while (0)
#define IS(call, want)                                                                         \
    do {                                                                                       \
        int32_t rc_ = (call);                                                                  \
        CHECK(rc_ == (want), "%s -> %s, want %s", #call, zxv_status_name(rc_), #want);         \
    } while (0)

/* ===================================================================== */
/* 1. Profiles                                                           */
/* ===================================================================== */

static void test_profiles(void)
{
    zxv_hw_profile *host, *r1, *copy, *mine;
    OK(zxv_hw_profile_probe_host(&host));
    OK(zxv_hw_profile_reference(&r1));
    uint32_t n;
    OK(zxv_hw_profile_count(r1, &n));
    CHECK(n == 16, "R1 has %u nodes", n);
    OK(zxv_hw_profile_count(host, &n));
    CHECK(n >= 2, "host profile has %u nodes", n);
    uint64_t v = 0;
    OK(zxv_hw_profile_cap_get(host, 2, ZXV_HW_CAP_WORD_BITS, &v));
    CHECK(v == sizeof(void *) * 8, "word bits %llu", (unsigned long long) v);
    OK(zxv_hw_profile_cap_get(host, 2, ZXV_HW_CAP_COUNT, &v));
    CHECK(v >= 1, "cores %llu", (unsigned long long) v);

    /* The host runs the CPU natively and emulates what it lacks. */
    uint32_t nat = 0, emu = 0;
    OK(zxv_hw_profile_match(host, r1, &nat, &emu));
    CHECK(nat >= 1 && nat + emu == 14, "native %u emulated %u", nat, emu);
    uint32_t need, have, unmet;
    int32_t kind;
    OK(zxv_hw_profile_pair(host, r1, 1, &need, &kind, &have, &unmet));
    CHECK(need == 2 && kind == ZXV_HW_GROUP, "package groups (%u %d)", need, kind);
    OK(zxv_hw_profile_pair(host, r1, 2, &need, &kind, &have, &unmet));
    CHECK(need == 3 && kind == ZXV_HW_NATIVE && have == 2, "cpu pair %u %d %u", need, kind, have);
    OK(zxv_hw_profile_pair(host, r1, 7, &need, &kind, &have, &unmet));
    CHECK(kind == ZXV_HW_EMULATE, "a laptop has no ternary unit");
    /* R1's engines name their contracts; presented on this host, every
     * running node says what serves it. */
    OK(zxv_hw_profile_cap_get(r1, 6, ZXV_HW_CAP_CONTRACT_ID, &v));
    CHECK(v == ZXV_HW_FN_TENSOR, "tensor contract 0x%llx", (unsigned long long) v);
    zxv_hw_profile *pres;
    OK(zxv_hw_profile_present(host, r1, &pres));
    OK(zxv_hw_profile_cap_get(pres, 3, ZXV_HW_CAP_KIND, &v));
    CHECK(v == ZXV_HW_KIND_HARD, "cpu served by HARD (%llu)", (unsigned long long) v);
    OK(zxv_hw_profile_cap_get(pres, 8, ZXV_HW_CAP_KIND, &v));
    CHECK(v == ZXV_HW_KIND_SOFT, "ternary served by SOFT (%llu)", (unsigned long long) v);
    IS(zxv_hw_profile_cap_get(pres, 1, ZXV_HW_CAP_KIND, &v), ZXV_E_NOT_FOUND);
    zxv_hw_profile_destroy(pres);
    IS(zxv_hw_profile_pair(host, r1, 99, &need, &kind, &have, &unmet), ZXV_E_NOT_FOUND);

    /* Canonical round trip and digest. */
    size_t len = 0;
    IS(zxv_hw_profile_encode(r1, NULL, 0, &len), ZXV_E_BUFFER);
    uint8_t *buf = malloc(len);
    OK(zxv_hw_profile_encode(r1, buf, len, &len));
    OK(zxv_hw_profile_decode(buf, len, &copy));
    uint8_t d1[32], d2[32];
    OK(zxv_hw_profile_digest(r1, d1));
    OK(zxv_hw_profile_digest(copy, d2));
    CHECK(!memcmp(d1, d2, 32), "digest differs after round trip");
    buf[len - 1] ^= 0; /* untouched */
    IS(zxv_hw_profile_decode(buf, len - 1, &mine), ZXV_E_FORMAT);
    free(buf);

    /* Build one: a machine built to R1 runs everything natively. */
    OK(zxv_hw_profile_create(&mine));
    OK(zxv_hw_profile_node_add(mine, 1, ZXV_HW_ROOT, ZXV_HW_CLASS_MACHINE));
    OK(zxv_hw_profile_node_add(mine, 2, 1, ZXV_HW_CLASS_TERNARY_UNIT));
    OK(zxv_hw_profile_cap_set(mine, 2, ZXV_HW_CAP_RADIX, 3));
    IS(zxv_hw_profile_node_add(mine, 2, 1, ZXV_HW_CLASS_CACHE), ZXV_E_ARG);
    IS(zxv_hw_profile_cap_set(mine, 9, ZXV_HW_CAP_RADIX, 3), ZXV_E_NOT_FOUND);
    OK(zxv_hw_profile_node_add(mine, 3, 1, ZXV_HW_CLASS_PRIVATE | 1u));
    OK(zxv_hw_profile_pair(mine, r1, 7, &need, &kind, &have, &unmet));
    CHECK(kind == ZXV_HW_NATIVE && have == 2, "ternary unit pairs natively");
    /* The private extension rides along, opaque, and changes the digest. */
    const uint8_t ext[3] = {0xA5, 0x00, 0x5A};
    uint8_t eb[8], dm1[32], dm2[32];
    OK(zxv_hw_profile_digest(mine, dm1));
    OK(zxv_hw_profile_ext_set(mine, 3, ext, 3));
    OK(zxv_hw_profile_digest(mine, dm2));
    CHECK(memcmp(dm1, dm2, 32) != 0, "extension is part of the identity");
    IS(zxv_hw_profile_ext_get(mine, 3, eb, 2, &len), ZXV_E_BUFFER);
    CHECK(len == 3, "ext needs %zu", len);
    OK(zxv_hw_profile_ext_get(mine, 3, eb, sizeof eb, &len));
    CHECK(len == 3 && !memcmp(eb, ext, 3), "ext round trip");
    static uint8_t big[ZXV_HW_EXT_MAX + 1];
    IS(zxv_hw_profile_ext_set(mine, 3, big, sizeof big), ZXV_E_RANGE);

    zxv_hw_profile_destroy(host);
    zxv_hw_profile_destroy(r1);
    zxv_hw_profile_destroy(copy);
    zxv_hw_profile_destroy(mine);
}

/* ===================================================================== */
/* 2. The matrix                                                         */
/* ===================================================================== */

typedef struct {
    uint8_t k;
    int bad;
} addk_t;

static int32_t addk(void *user, uint32_t op, const uint8_t *in, size_t n, uint8_t *out, size_t cap,
                    size_t *out_len)
{
    (void) op;
    addk_t *a = user;
    if (a->bad == 2) return ZXV_E_IO;
    if (n > cap) return ZXV_E_BUFFER;
    for (size_t i = 0; i < n; i++) out[i] = (uint8_t) (in[i] + a->k + (a->bad && in[i] == 9));
    *out_len = n;
    return ZXV_OK;
}

/* "add16" emulated through two calls to "add8" via the library itself. */
typedef struct {
    const zxv_cap *cap;
    zxv_hw_matrix *mx;
} chain_t;

static int32_t add_twice(void *user, uint32_t op, const uint8_t *in, size_t n, uint8_t *out,
                         size_t cap, size_t *out_len)
{
    chain_t *c = user;
    uint8_t tmp[64];
    size_t l = 0;
    int32_t rc = zxv_hw_call(c->cap, c->mx, 0x100, op, in, n, tmp, sizeof tmp, &l);
    if (rc) return rc;
    return zxv_hw_call(c->cap, c->mx, 0x100, op, tmp, l, out, cap, out_len);
}

static void test_matrix(zxv_cap *cap, zxv_cap *nohw)
{
    zxv_hw_matrix *mx;
    IS(zxv_hw_matrix_create_cap(nohw, &mx), ZXV_E_CAP);
    OK(zxv_hw_matrix_create_cap(cap, &mx));
    OK(zxv_hw_function_add(cap, mx, 0x100, "add8"));
    OK(zxv_hw_function_add(cap, mx, 0x101, "add16"));
    IS(zxv_hw_function_add(cap, mx, 0x100, "again"), ZXV_E_DUPLICATE);
    OK(zxv_hw_resource_add(cap, mx, 1)); /* CPU */
    OK(zxv_hw_resource_add(cap, mx, 2)); /* accelerator */
    const uint8_t in[] = {0, 1, 9, 200}, ex8[] = {8, 9, 17, 208}, ex16[] = {16, 17, 25, 216};
    OK(zxv_hw_vector_add(cap, mx, 0x100, 0, in, 4, ex8, 4));
    OK(zxv_hw_vector_add(cap, mx, 0x101, 0, in, 4, ex16, 4));

    addk_t fx = {8, 0}, sw = {8, 0};
    chain_t ch = {cap, mx};
    uint32_t pfx, psw, p16, pv;
    OK(zxv_hw_provider_add(cap, mx, 0x100, 2, ZXV_HW_KIND_HARD, 1, addk, &fx, &pfx));
    OK(zxv_hw_provider_add(cap, mx, 0x100, 1, ZXV_HW_KIND_SOFT, 10, addk, &sw, &psw));
    OK(zxv_hw_provider_add(cap, mx, 0x101, 1, ZXV_HW_KIND_SOFT, 20, add_twice, &ch, &p16));
    IS(zxv_hw_provider_add(cap, mx, 0x100, 1, 9, 1, addk, &sw, &pv), ZXV_E_ARG);

    uint8_t out[64];
    size_t n = 0;
    IS(zxv_hw_call(cap, mx, 0x100, 0, in, 4, out, sizeof out, &n), ZXV_E_NOPROVIDER);
    int32_t lp;
    OK(zxv_hw_attest(cap, mx, pfx, &lp));
    CHECK(lp == ZXV_LP_TRUE, "fixed attest %d", lp);
    OK(zxv_hw_attest(cap, mx, psw, &lp));
    CHECK(lp == ZXV_LP_TRUE, "soft attest %d", lp);
    /* The chained provider calls add8 without declaring it: refused, so
     * its proof fails. Declared, it passes. */
    OK(zxv_hw_attest(cap, mx, p16, &lp));
    CHECK(lp == ZXV_LP_FALSE, "undeclared dependency must not prove (%d)", lp);
    OK(zxv_hw_provider_dep(cap, mx, p16, 0x100));
    OK(zxv_hw_attest(cap, mx, p16, &lp));
    CHECK(lp == ZXV_LP_TRUE, "chained attest %d", lp);
    OK(zxv_hw_call(cap, mx, 0x101, 0, in, 4, out, sizeof out, &n));
    CHECK(n == 4 && !memcmp(out, ex16, 4), "add16 output");

    OK(zxv_hw_bound(cap, mx, 0x100, &pv));
    CHECK(pv == pfx, "fixed unit serves (%u)", pv);
    OK(zxv_hw_resource_set(cap, mx, 2, ZXV_HW_RES_FAULTED));
    OK(zxv_hw_call(cap, mx, 0x100, 0, in, 4, out, sizeof out, &n));
    OK(zxv_hw_bound(cap, mx, 0x100, &pv));
    CHECK(pv == psw && !memcmp(out, ex8, 4), "failover to the emulator");
    OK(zxv_hw_resource_set(cap, mx, 2, ZXV_HW_RES_OK));

    /* A provider error comes back as its own status. */
    fx.bad = 2;
    IS(zxv_hw_call(cap, mx, 0x100, 0, in, 4, out, sizeof out, &n), ZXV_E_IO);
    fx.bad = 0;

    /* Shadow: agreement, then a silent fault -> dispute, nobody serves. */
    int32_t checked = 0;
    OK(zxv_hw_shadow(cap, mx, 0x100, 0, in, 4, out, sizeof out, &n, &checked));
    CHECK(checked == 1, "shadow ran two providers");
    sw.bad = 1;
    IS(zxv_hw_shadow(cap, mx, 0x100, 0, in, 4, out, sizeof out, &n, &checked), ZXV_E_DISPUTE);
    IS(zxv_hw_call(cap, mx, 0x100, 0, in, 4, out, sizeof out, &n), ZXV_E_NOPROVIDER);
    OK(zxv_hw_attest(cap, mx, pfx, &lp));
    CHECK(lp == ZXV_LP_TRUE, "fixed re-proves");
    OK(zxv_hw_attest(cap, mx, psw, &lp));
    CHECK(lp == ZXV_LP_FALSE, "faulty emulator fails its proof");

    /* Memo: a repeat skips the provider. */
    uint64_t hits, misses, calls0, calls1;
    IS(zxv_hw_memo(cap, mx, 0x100, 6, &hits, &misses), ZXV_E_ARG);
    OK(zxv_hw_memo(cap, mx, 0x100, 16, &hits, &misses));
    OK(zxv_hw_provider_info(cap, mx, pfx, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &calls0, NULL));
    OK(zxv_hw_call(cap, mx, 0x100, 0, in, 4, out, sizeof out, &n));
    OK(zxv_hw_call(cap, mx, 0x100, 0, in, 4, out, sizeof out, &n));
    OK(zxv_hw_provider_info(cap, mx, pfx, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &calls1, NULL));
    OK(zxv_hw_memo(cap, mx, 0x100, 16, &hits, &misses));
    CHECK(calls1 == calls0 + 1 && hits == 1 && misses == 1, "memo: calls %llu->%llu hits %llu",
          (unsigned long long) calls0, (unsigned long long) calls1, (unsigned long long) hits);
    CHECK(!memcmp(out, ex8, 4), "memo output");

    /* Versioned contracts: a new version makes everyone prove again. */
    uint32_t ver, pol, nv;
    OK(zxv_hw_function_info(cap, mx, 0x100, &ver, &pol, &nv));
    CHECK(ver == 0 && pol == 0 && nv == 1, "add8 info %u %u %u", ver, pol, nv);
    OK(zxv_hw_function_contract(cap, mx, 0x100, 2));
    IS(zxv_hw_function_contract(cap, mx, 0x100, 2), ZXV_E_ROLLBACK);
    IS(zxv_hw_call(cap, mx, 0x100, 0, in, 4, out, sizeof out, &n), ZXV_E_NOPROVIDER);
    OK(zxv_hw_vector_add(cap, mx, 0x100, 0, in, 4, ex8, 4));
    OK(zxv_hw_attest(cap, mx, pfx, &lp));
    uint32_t pcon, pflags;
    OK(zxv_hw_provider_info(cap, mx, pfx, NULL, NULL, &lp, &pcon, &pflags, NULL, NULL, NULL, NULL));
    CHECK(lp == ZXV_LP_TRUE && pcon == 2, "proved contract %u", pcon);
    OK(zxv_hw_provider_info(cap, mx, psw, NULL, NULL, NULL, NULL, &pflags, NULL, NULL, NULL, NULL));
    CHECK(pflags & ZXV_HW_PF_UNPROVEN, "emulator not proven against v2 (flags 0x%x)", pflags);

    zxv_hw_matrix_destroy(mx);
}

/* ===================================================================== */
/* 2b. Standard engines, placement and discovery                         */
/* ===================================================================== */

static int32_t remote_mac(void *user, uint32_t op, const uint8_t *in, size_t n, uint8_t *out,
                          size_t cap, size_t *out_len)
{
    /* a "remote peer" that forwards to the local SOFT engine */
    chain_t *c = user;
    int *hits = (int *) (void *) &c[1];
    (*hits)++;
    (void) op;
    (void) in;
    (void) n;
    (void) out;
    (void) cap;
    (void) out_len;
    return ZXV_E_STATE; /* must never be reached for key-bearing work */
}

static void test_engines(zxv_cap *cap)
{
    zxv_hw_matrix *mx;
    OK(zxv_hw_matrix_create_cap(cap, &mx));
    uint8_t out[512];
    size_t n = 0;
    /* Every matrix answers the standard engines on any machine. */
    OK(zxv_hw_call(cap, mx, ZXV_HW_FN_SHA256, 0, (const uint8_t *) "abc", 3, out, sizeof out, &n));
    static const uint8_t abc[4] = {0xba, 0x78, 0x16, 0xbf};
    CHECK(n == 32 && !memcmp(out, abc, 4), "sha256(abc)");
    /* Too small an output buffer is the caller's error, not the engine's. */
    IS(zxv_hw_call(cap, mx, ZXV_HW_FN_SHA256, 0, (const uint8_t *) "abc", 3, out, 8, &n), ZXV_E_BUFFER);
    uint8_t mac_in[34] = {4, 0, 'J', 'e', 'f', 'e'};
    memcpy(mac_in + 6, "what do ya want for nothing?", 28);
    OK(zxv_hw_call(cap, mx, ZXV_HW_FN_MAC, 0, mac_in, 34, out, sizeof out, &n));
    static const uint8_t rfc[4] = {0x5b, 0xdc, 0xc1, 0x46};
    CHECK(n == 32 && !memcmp(out, rfc, 4), "HMAC RFC 4231 case 2");
    uint8_t g[16] = {12, 0, 0, 0, 0, 0, 0, 0, 18};
    OK(zxv_hw_call(cap, mx, ZXV_HW_FN_GCD, 0, g, 16, out, sizeof out, &n));
    CHECK(n == 8 && out[0] == 6, "gcd(12, 18)");

    /* Discovery: count, kinds, proofs, flags. */
    uint32_t count, fn, flags, con, tput, pwr;
    int32_t kind, lp;
    OK(zxv_hw_provider_count(cap, mx, &count));
    CHECK(count >= 5, "standard engines %u", count);
    uint32_t mac_pv = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < count; i++) {
        OK(zxv_hw_provider_info(cap, mx, i, &fn, &kind, &lp, &con, &flags, &tput, &pwr, NULL, NULL));
        CHECK(kind == ZXV_HW_KIND_SOFT && lp == ZXV_LP_TRUE && con == 1 && !(flags & ZXV_HW_PF_FAULT),
              "engine %u: kind %d lp %d contract %u flags 0x%x", i, kind, lp, con, flags);
        if (fn == ZXV_HW_FN_MAC) mac_pv = i;
    }
    CHECK(mac_pv != 0xFFFFFFFFu, "MAC engine listed");
    IS(zxv_hw_provider_info(cap, mx, count, &fn, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_NOT_FOUND);
    uint32_t ver, pol, nv;
    OK(zxv_hw_function_info(cap, mx, ZXV_HW_FN_MAC, &ver, &pol, &nv));
    CHECK(ver == 1 && (pol & ZXV_HW_POLICY_KEY_BEARING) && nv >= 1, "MAC info %u 0x%x %u", ver, pol, nv);

    /* Key-bearing: a cheaper REMOTE implementation never serves it, the
     * policy cannot be cleared, and no memo keeps key material. */
    struct {
        chain_t c;
        int hits;
    } rm = {{cap, mx}, 0};
    uint32_t prem, pv;
    OK(zxv_hw_resource_add(cap, mx, 7));
    OK(zxv_hw_provider_add(cap, mx, ZXV_HW_FN_MAC, 7, ZXV_HW_KIND_REMOTE, 1, remote_mac, &rm, &prem));
    OK(zxv_hw_attest(cap, mx, prem, &lp)); /* fails its vectors anyway */
    OK(zxv_hw_call(cap, mx, ZXV_HW_FN_MAC, 0, mac_in, 34, out, sizeof out, &n));
    OK(zxv_hw_bound(cap, mx, ZXV_HW_FN_MAC, &pv));
    CHECK(pv == mac_pv, "MAC stays on the local engine (%u)", pv);
    OK(zxv_hw_provider_info(cap, mx, prem, NULL, NULL, NULL, NULL, &flags, NULL, NULL, NULL, NULL));
    CHECK(flags & ZXV_HW_PF_POLICY, "remote MAC flagged by policy (0x%x)", flags);
    IS(zxv_hw_policy_set(cap, mx, ZXV_HW_FN_MAC, 0, 0, 0), ZXV_E_POLICY);
    IS(zxv_hw_memo(cap, mx, ZXV_HW_FN_MAC, 16, NULL, NULL), ZXV_E_POLICY);
    IS(zxv_hw_policy_set(cap, mx, ZXV_HW_FN_MAC, 0x80, 0, 0), ZXV_E_ARG);

    /* A power budget: undeclared traits never qualify, declared ones do. */
    OK(zxv_hw_policy_set(cap, mx, ZXV_HW_FN_GCD, 0, 0, 500));
    IS(zxv_hw_call(cap, mx, ZXV_HW_FN_GCD, 0, g, 16, out, sizeof out, &n), ZXV_E_NOPROVIDER);
    uint32_t gcd_pv = 0;
    for (uint32_t i = 0; i < count; i++) {
        OK(zxv_hw_provider_info(cap, mx, i, &fn, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL));
        if (fn == ZXV_HW_FN_GCD) gcd_pv = i;
    }
    OK(zxv_hw_provider_traits(cap, mx, gcd_pv, 2000, 300, 4));
    IS(zxv_hw_provider_traits(cap, mx, gcd_pv, 2000, 300, 16), ZXV_E_RANGE);
    OK(zxv_hw_call(cap, mx, ZXV_HW_FN_GCD, 0, g, 16, out, sizeof out, &n));
    OK(zxv_hw_provider_info(cap, mx, gcd_pv, NULL, NULL, NULL, NULL, NULL, &tput, &pwr, NULL, NULL));
    CHECK(tput == 4 && pwr == ZXV_HW_POWER_1W, "traits %u %u", tput, pwr);
    OK(zxv_hw_policy_set(cap, mx, ZXV_HW_FN_GCD, ZXV_HW_POLICY_LOCAL_ONLY, 1000, 0));
    IS(zxv_hw_call(cap, mx, ZXV_HW_FN_GCD, 0, g, 16, out, sizeof out, &n), ZXV_E_NOPROVIDER);
    OK(zxv_hw_policy_set(cap, mx, ZXV_HW_FN_GCD, 0, 0, 0));

    zxv_hw_matrix_destroy(mx);
}

/* ===================================================================== */
/* 3. Regions                                                            */
/* ===================================================================== */

typedef struct {
    uint8_t slot[2][256];
    size_t len[2];
    int active;
    int refuse;
} sim_t;

static int32_t sim_load(void *u, uint32_t s, const uint8_t *p, size_t n)
{
    sim_t *x = u;
    if (s > 1 || n > 256) return ZXV_E_RANGE;
    memcpy(x->slot[s], p, n);
    x->len[s] = n;
    return ZXV_OK;
}
static int32_t sim_activate(void *u, uint32_t s)
{
    sim_t *x = u;
    if (x->refuse) return ZXV_E_IO;
    x->active = (int) s;
    return ZXV_OK;
}
static int32_t sim_call(void *u, uint32_t op, const uint8_t *in, size_t n, uint8_t *out, size_t cap,
                        size_t *out_len)
{
    (void) op;
    sim_t *x = u;
    if (x->active < 0 || n > cap) return ZXV_E_STATE;
    for (size_t i = 0; i < n; i++) out[i] = x->slot[x->active][in[i]];
    *out_len = n;
    return ZXV_OK;
}

/* The device must report the SHA-256 of what a slot holds: the oracle
 * is the standard SHA-256 engine of a separate matrix. */
static const zxv_cap *g_cap;
static zxv_hw_matrix *g_oracle;
static int32_t sim_readback(void *u, uint32_t s, uint8_t d[32])
{
    sim_t *x = u;
    size_t n = 0;
    return zxv_hw_call(g_cap, g_oracle, ZXV_HW_FN_SHA256, 0, x->slot[s], x->len[s], d, 32, &n);
}

static int g_events[32], g_nev;
static uint32_t g_ev_version[32];
static void on_event(void *user, uint32_t region_id, int32_t event, uint32_t reason, uint32_t version)
{
    (void) reason;
    CHECK(user == (void *) &g_nev && region_id == 1, "event context");
    if (g_nev < 32) {
        g_ev_version[g_nev] = version;
        g_events[g_nev++] = event;
    }
}

static void put32(uint8_t *b, uint32_t v)
{
    for (int i = 0; i < 4; i++) b[i] = (uint8_t) (v >> (8 * i));
}

static void test_regions(zxv_cap *cap, zxv_cap *nohw)
{
    static uint8_t pk[ZXV_HW_PK_BYTES], sk[ZXV_HW_MLDSA_SK_BYTES], ssk[ZXV_HW_SLHDSA_SK_BYTES];
    static uint8_t pk2[ZXV_HW_PK_BYTES], sk2[ZXV_HW_MLDSA_SK_BYTES], ssk2[ZXV_HW_SLHDSA_SK_BYTES];
    IS(zxv_hw_keygen(nohw, pk, sk, ssk), ZXV_OK); /* CRYPTO_SIGN is a 1.1 bit */
    OK(zxv_hw_keygen(cap, pk, sk, ssk));
    OK(zxv_hw_keygen(cap, pk2, sk2, ssk2));
    g_cap = cap;
    OK(zxv_hw_matrix_create_cap(cap, &g_oracle));

    zxv_hw_matrix *mx;
    OK(zxv_hw_matrix_create_cap(cap, &mx));
    OK(zxv_hw_function_add(cap, mx, 0x200, "inc"));
    OK(zxv_hw_resource_add(cap, mx, 1));
    OK(zxv_hw_resource_add(cap, mx, 2));
    static uint8_t vin[256], vinc[256], inc[256], wrong[256];
    for (int i = 0; i < 256; i++) {
        vin[i] = (uint8_t) i;
        vinc[i] = inc[i] = wrong[i] = (uint8_t) (i + 1);
    }
    wrong[77] = 0;
    OK(zxv_hw_vector_add(cap, mx, 0x200, 0, vin, 256, vinc, 256));
    static sim_t sim = {.active = -1};
    addk_t soft = {1, 0};
    uint32_t psoft, preg, pv;
    OK(zxv_hw_provider_add(cap, mx, 0x200, 1, ZXV_HW_KIND_SOFT, 50, addk, &soft, &psoft));
    OK(zxv_hw_provider_add(cap, mx, 0x200, 2, ZXV_HW_KIND_FABRIC, 1, sim_call, &sim, &preg));
    int32_t lp;
    OK(zxv_hw_attest(cap, mx, psoft, &lp));

    zxv_hw_region *rg;
    IS(zxv_hw_region_create_cap(nohw, mx, preg, 1, 0xD0, NULL, sim_load, sim_readback, sim_activate,
                                &sim, 12, &rg),
       ZXV_E_CAP);
    IS(zxv_hw_region_create_cap(cap, mx, psoft, 1, 0xD0, NULL, sim_load, sim_readback, sim_activate,
                                &sim, 12, &rg),
       ZXV_E_ARG);
    OK(zxv_hw_region_create_cap(cap, mx, preg, 1, 0xD0, NULL, sim_load, sim_readback, sim_activate,
                                &sim, 12, &rg));
    OK(zxv_hw_region_trust_key(cap, rg, pk));
    OK(zxv_hw_region_events(cap, rg, on_event, &g_nev));

    enum { IMG = 96 + 256 + ZXV_HW_SIG_BYTES };
    static uint8_t img[IMG], good[IMG], bad[IMG], forged[IMG];
    size_t len, glen, blen, flen;
    IS(zxv_hw_image_build(cap, ZXV_HW_IMG_PARTIAL, 0xD0, NULL, 0x200, 1, 1, inc, 256, sk, ssk, NULL,
                          0, &len),
       ZXV_E_BUFFER);
    CHECK(len == IMG, "image length %zu", len);

    /* Wrong signer: refused, recorded. */
    OK(zxv_hw_image_build(cap, ZXV_HW_IMG_PARTIAL, 0xD0, NULL, 0x200, 1, 1, inc, 256, sk2, ssk2,
                          forged, sizeof forged, &flen));
    IS(zxv_hw_region_stage(cap, rg, forged, flen), ZXV_E_VERIFY);
    /* Only one of the two signatures valid: still refused. Our ML-DSA
     * half with the other key's SLH-DSA half. */
    OK(zxv_hw_image_build(cap, ZXV_HW_IMG_PARTIAL, 0xD0, NULL, 0x200, 1, 1, inc, 256, sk, ssk, good,
                          sizeof good, &glen));
    memcpy(img, good, glen);
    memcpy(img + 96 + 256 + 4627, forged + 96 + 256 + 4627, ZXV_HW_SIG_BYTES - 4627);
    IS(zxv_hw_region_stage(cap, rg, img, glen), ZXV_E_VERIFY);
    /* Wrong hardware (the class is checked before the signature). */
    memcpy(img, good, glen);
    put32(img + 12, 0xD1);
    IS(zxv_hw_region_stage(cap, rg, img, glen), ZXV_E_TARGET);

    /* The update: stage, test, activate; the region now serves "inc". */
    OK(zxv_hw_region_stage(cap, rg, good, glen));
    IS(zxv_hw_region_commit(cap, rg), ZXV_E_STATE);
    OK(zxv_hw_region_trial(cap, rg, 5));
    OK(zxv_hw_bound(cap, mx, 0x200, &pv));
    CHECK(pv == preg, "the reconfigured region serves (%u)", pv);
    OK(zxv_hw_region_commit(cap, rg));
    int32_t st;
    uint32_t ver, floor, nq;
    OK(zxv_hw_region_info(cap, rg, &st, &ver, &floor, &nq));
    CHECK(st == ZXV_HW_REGION_ACTIVE && ver == 1 && floor == 1 && nq == 2, "info %d %u %u %u", st,
          ver, floor, nq);
    CHECK(g_nev == 5 && g_events[0] == ZXV_HW_EV_REJECTED && g_events[2] == ZXV_HW_EV_STAGED &&
              g_events[3] == ZXV_HW_EV_TESTED && g_events[4] == ZXV_HW_EV_ACTIVATED &&
              g_ev_version[4] == 1,
          "events so far: %d", g_nev);

    /* Replay: refused, recorded. */
    IS(zxv_hw_region_stage(cap, rg, good, glen), ZXV_E_ROLLBACK);

    /* A signed image with a wrong gate: rolled back to v1, quarantined,
     * blocked even under a new version number. */
    OK(zxv_hw_image_build(cap, ZXV_HW_IMG_PARTIAL, 0xD0, NULL, 0x200, 2, 1, wrong, 256, sk, ssk, bad,
                          sizeof bad, &blen));
    OK(zxv_hw_region_stage(cap, rg, bad, blen));
    IS(zxv_hw_region_trial(cap, rg, 5), ZXV_E_QUARANTINED);
    OK(zxv_hw_region_info(cap, rg, &st, &ver, &floor, &nq));
    CHECK(st == ZXV_HW_REGION_ACTIVE && ver == 1 && nq == 4, "after rollback %d %u %u", st, ver, nq);
    OK(zxv_hw_bound(cap, mx, 0x200, &pv));
    CHECK(pv == preg, "v1 back in service");
    memcpy(img, bad, blen);
    put32(img + 20, 3); /* same bytes, new version: blocked before the signature */
    IS(zxv_hw_region_stage(cap, rg, img, blen), ZXV_E_BLOCKED);

    /* The audit log holds every step, readable record by record. */
    uint8_t head[32], subj[32], hash[32];
    uint32_t count, reason, id, rv, tag;
    int32_t ev;
    OK(zxv_hw_region_log(cap, rg, head, &count));
    CHECK(count == 9, "log has %u records", count);
    OK(zxv_hw_region_log_entry(cap, rg, count - 1, &ev, &reason, &id, &rv, &tag, subj, hash));
    CHECK(ev == ZXV_HW_EV_QUARANTINED && id == 1 && rv == 2 && !memcmp(hash, head, 32) &&
              ((tag >> 2) & 3) == 2,
          "last record: ev %d id %u v %u tag %u", ev, id, rv, tag);
    OK(zxv_hw_region_log_entry(cap, rg, 4, &ev, NULL, NULL, &rv, &tag, NULL, NULL));
    CHECK(ev == ZXV_HW_EV_ACTIVATED && rv == 1 && ((tag >> 2) & 3) != 2, "activation record");
    IS(zxv_hw_region_log_entry(cap, rg, count, &ev, NULL, NULL, NULL, NULL, NULL, NULL), ZXV_E_NOT_FOUND);
    CHECK(g_nev == (int) count, "one event per record (%d vs %u)", g_nev, count);

    /* Contract packages: a newer golden-vector set for "inc", signed and
     * applied; the region and the emulator both prove it. */
    uint8_t pay[4 + 4 + 2 + 4 + 2];
    put32(pay, 0);
    put32(pay + 4, 2);
    pay[8] = 5;
    pay[9] = 250;
    put32(pay + 10, 2);
    pay[14] = 6;
    pay[15] = 251;
    static uint8_t pkg[96 + sizeof pay + ZXV_HW_SIG_BYTES];
    size_t plen;
    OK(zxv_hw_image_build(cap, ZXV_HW_IMG_VECTORS, 0, NULL, 0x200, 7, 3, pay, sizeof pay, sk, ssk, pkg,
                          sizeof pkg, &plen));
    IS(zxv_hw_image_build(cap, ZXV_HW_IMG_VECTORS, 0, NULL, 0x200, 7, 3, pay, sizeof pay - 1, sk, ssk,
                          pkg, sizeof pkg, &len),
       ZXV_E_FORMAT);
    uint32_t proven = 99;
    IS(zxv_hw_contract_apply(nohw, mx, pkg, plen, &proven), ZXV_E_CAP);
    IS(zxv_hw_contract_apply(cap, mx, pkg, plen, &proven), ZXV_E_VERIFY); /* no key trusted yet */
    OK(zxv_hw_matrix_trust_key(cap, mx, pk));
    IS(zxv_hw_contract_apply(cap, mx, good, glen, &proven), ZXV_E_FORMAT); /* an image, not vectors */
    IS(zxv_hw_region_stage(cap, rg, pkg, plen), ZXV_E_FORMAT);             /* and the other way */
    OK(zxv_hw_contract_apply(cap, mx, pkg, plen, &proven));
    CHECK(proven == 2, "proven %u", proven);
    OK(zxv_hw_function_info(cap, mx, 0x200, &ver, NULL, &count));
    CHECK(ver == 7 && count == 1, "contract %u vectors %u", ver, count);
    IS(zxv_hw_contract_apply(cap, mx, pkg, plen, &proven), ZXV_E_ROLLBACK);
    OK(zxv_hw_matrix_log(cap, mx, head, &count));
    CHECK(count == 3, "matrix log %u", count);
    OK(zxv_hw_matrix_log_entry(cap, mx, 1, &ev, NULL, &id, &rv, NULL, NULL, NULL));
    CHECK(ev == ZXV_HW_EV_CONTRACT && id == 0x200 && rv == 7, "contract record %d 0x%x %u", ev, id, rv);

    /* Three more recorded refusals fill the log (12); the next one halts
     * the region and poisons the context: fail closed. A blocked image is
     * refused before any work and is not recorded. */
    IS(zxv_hw_region_stage(cap, rg, forged, flen), ZXV_E_VERIFY);
    IS(zxv_hw_region_stage(cap, rg, forged, flen), ZXV_E_VERIFY);
    IS(zxv_hw_region_stage(cap, rg, forged, flen), ZXV_E_VERIFY);
    IS(zxv_hw_region_stage(cap, rg, forged, flen), ZXV_E_POISONED);
    IS(zxv_hw_call(cap, mx, 0x200, 0, vin, 4, vinc, 4, &len), ZXV_E_POISONED);

    zxv_hw_region_destroy(rg);
    zxv_hw_matrix_destroy(mx);
    zxv_hw_matrix_destroy(g_oracle);
}

int main(void)
{
    uint32_t maj, min, pat, feat;
    OK(zxv_version(&maj, &min, &pat));
    CHECK(maj == 1 && min >= 4, "version %u.%u", maj, min);
    OK(zxv_features(&feat));
    CHECK(feat & ZXV_FEAT_HW, "ZXV_FEAT_HW missing");
    CHECK(!strcmp(zxv_status_name(ZXV_E_QUARANTINED), "ZXV_E_QUARANTINED"), "name");
    CHECK(zxv_status_class(ZXV_E_DISPUTE) == ZXV_CLASS_REFUSED, "class");
    CHECK(zxv_status_class(ZXV_E_TARGET) == ZXV_CLASS_USAGE, "class");
    CHECK(zxv_status_class(ZXV_E_POLICY) == ZXV_CLASS_REFUSED, "class");
    CHECK(!strcmp(zxv_status_name(ZXV_E_POLICY), "ZXV_E_POLICY"), "name");

    test_profiles();

    zxv_ctx *ctx, *ctx2;
    zxv_cap *cap, *nohw, *cap2, *nohw2;
    OK(zxv_ctx_create(&ctx));
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL_1_4, &cap));
    OK(zxv_cap_attenuate(cap, ZXV_CAP_ALL, &nohw));
    IS(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL_1_4 + 1, &cap2), ZXV_E_ARG);
    test_matrix(cap, nohw);
    test_engines(cap);
    zxv_cap_destroy(nohw);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);

    OK(zxv_ctx_create(&ctx2));
    OK(zxv_cap_create(ctx2, 1, NULL, ZXV_CAP_ALL_1_4, &cap2));
    OK(zxv_cap_attenuate(cap2, ZXV_CAP_ALL, &nohw2));
    test_regions(cap2, nohw2);
    zxv_cap_destroy(nohw2);
    zxv_cap_destroy(cap2);
    zxv_ctx_destroy(ctx2);

    printf("test_zxv_abi14: %d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
