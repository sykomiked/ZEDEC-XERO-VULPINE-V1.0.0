/* test_curzi_tier.c — HOST test for CURZI-8889-A LAYER 5 (tiered access).
 *
 * Runs on the host, so it may use stdio; the code under test (curzi_tier.c)
 * may not and does not. Exit status 0 == every check passed.
 *
 * Build and run:
 *   gcc -std=c11 -Wall -Werror -Wextra -O2 \
 *       -Ikernel/include -Ikernel/src/modbind -I<dir with zxv_phase_table.h> \
 *       kernel/src/curzi/test_curzi_tier.c kernel/src/curzi/curzi_tier.c \
 *       kernel/src/mlkem/keccak.c -o /tmp/test_curzi_tier && /tmp/test_curzi_tier
 * (zxv_phase_table.h is GENERATED -- build_system/gen_phase_table.sh <out.h> --
 *  and is pulled in only because keccak.c carries a ZXV_DECLARE.)
 *
 * WHAT IS ACTUALLY BEING FALSIFIED
 * --------------------------------
 * curzi8889a.h:126 states the killing test for this layer: "if a tier key at
 * level L can derive a key at level < L, LAYER 5 is void." So the descent
 * checks are not decoration -- they are the layer's reason to exist, and they
 * are checked two ways: the API must REFUSE the request, and the forward
 * closure of a key must not CONTAIN any lower key or any other domain's key.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (CURZI-8889-A LAYER 5 slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include <stdio.h>
#include "curzi8889a.h"
#include "../mlkem/keccak.h"

#define KEYB 32u
#define LMAX CURZI_LEVELS          /* highest level index; 0 == the domain root */
#define NLVL (CURZI_LEVELS + 1u)   /* addressable indices per domain: 0..61     */

static unsigned g_fail;
static unsigned g_checks;

static void check(int cond, const char *what)
{
    g_checks++;
    if (!cond) {
        g_fail++;
        if (g_fail <= 20u) printf("  FAIL: %s\n", what);
    }
}

static int eq32(const uint8_t *a, const uint8_t *b)
{
    for (unsigned i = 0; i < KEYB; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static int is_zero32(const uint8_t *a)
{
    uint8_t acc = 0;
    for (unsigned i = 0; i < KEYB; i++) acc = (uint8_t)(acc | a[i]);
    return acc == 0;
}

static void hex32(const char *tag, const uint8_t *a)
{
    printf("  %s ", tag);
    for (unsigned i = 0; i < KEYB; i++) printf("%02x", a[i]);
    printf("\n");
}

/* The whole space: [domain][level index]. 144 * 62 * 32 = 285,696 bytes,
 * static because the test wants the ENTIRE key set in hand at once to prove
 * global distinctness, not a sampled one. */
static uint8_t g_key[CURZI_DOMAINS][NLVL][KEYB];
static uint8_t g_key2[CURZI_DOMAINS][NLVL][KEYB];

/* Two deliberately unremarkable system roots. Neither is secret and neither is
 * chosen for any property: root A is the all-zero root so a third party can
 * recompute the printed vectors with three lines of Python, root B is 0x01..0x20
 * so that "different system root => different everything" is checkable. */
static uint8_t g_sysroot_a[KEYB];
static uint8_t g_sysroot_b[KEYB];

static void build(uint8_t (*tab)[NLVL][KEYB], const uint8_t *sysroot)
{
    for (unsigned d = 0; d < CURZI_DOMAINS; d++) {
        curzi_err_t e = curzi_tier_root(sysroot, (uint16_t)d, tab[d][0]);
        check(e == CURZI_OK, "curzi_tier_root returned OK");
        for (unsigned L = 1; L <= LMAX; L++) {
            e = curzi_tier_derive(tab[d][L - 1u], (uint16_t)d,
                                  (uint8_t)(L - 1u), (uint8_t)L, tab[d][L]);
            check(e == CURZI_OK, "one-step derive returned OK");
        }
    }
}

/* ---- 0. the hash underneath is the NIST-verified one --------------------- */
static void t0_hash_anchor(void)
{
    /* FIPS 202 / NIST CAVP: SHA3-256("abc"). If this line ever fails, LAYER 5
     * is not the thing that is broken -- keccak.c is -- and every derived key
     * in the system is wrong (see keccak.c:31-49 for the last time that
     * happened and how quietly it happened). */
    static const uint8_t want[KEYB] = {
        0x3a,0x98,0x5d,0xa7,0x4f,0xe2,0x25,0xb2, 0x04,0x5c,0x17,0x2d,0x6b,0xd3,0x90,0xbd,
        0x85,0x5f,0x08,0x6e,0x3e,0x9d,0x52,0x5b, 0x46,0xbf,0xe2,0x45,0x11,0x43,0x15,0x32
    };
    const uint8_t abc[3] = { 'a', 'b', 'c' };
    uint8_t got[KEYB];
    sha3_256(abc, sizeof abc, got);
    check(eq32(got, want), "sha3_256(\"abc\") matches FIPS 202");
}

/* ---- 1. the wire format is what the spec says, byte for byte -------------
 * Rebuilt here from the SPEC TEXT rather than from curzi_tier.c's helpers, so
 * this catches a field-order, endianness or label change in the implementation
 * instead of agreeing with it by construction. */
static void t1_wire_format(void)
{
    uint8_t buf[64];
    uint8_t want[KEYB], got[KEYB];
    unsigned o;
    const char *lbl;
    const uint16_t dom = 143u;   /* the last valid domain: a real edge */

    o = 0;
    for (unsigned i = 0; i < KEYB; i++) buf[o++] = g_sysroot_a[i];
    lbl = "CURZI-8889-A/domain";
    for (unsigned i = 0; lbl[i] != '\0'; i++) buf[o++] = (uint8_t)lbl[i];
    buf[o++] = (uint8_t)(dom & 0xFFu);
    buf[o++] = (uint8_t)(dom >> 8);
    check(o == 53u, "domain preimage is 32 + 19 + 2 = 53 bytes");
    sha3_256(buf, o, want);
    check(curzi_tier_root(g_sysroot_a, dom, got) == CURZI_OK, "root(143) OK");
    check(eq32(want, got), "domain root == H(sysroot || \"CURZI-8889-A/domain\" || d_le16)");

    /* level 1 of that domain, from the spec formula. */
    o = 0;
    for (unsigned i = 0; i < KEYB; i++) buf[o++] = want[i];   /* parent = domain root */
    lbl = "CURZI-8889-A/level";
    for (unsigned i = 0; lbl[i] != '\0'; i++) buf[o++] = (uint8_t)lbl[i];
    buf[o++] = (uint8_t)(dom & 0xFFu);
    buf[o++] = (uint8_t)(dom >> 8);
    buf[o++] = 1u;
    check(o == 53u, "level preimage is 32 + 18 + 2 + 1 = 53 bytes");
    {
        uint8_t want1[KEYB], got1[KEYB];
        sha3_256(buf, o, want1);
        check(curzi_tier_derive(got, dom, 0u, 1u, got1) == CURZI_OK, "derive(0->1) OK");
        check(eq32(want1, got1),
              "level 1 == H(domain root || \"CURZI-8889-A/level\" || d_le16 || 1)");
    }
}

/* ---- 2. ascent works, and equals the direct walk from the domain root ---- */
static void t2_ascent(void)
{
    uint8_t out[KEYB];

    /* (a) EVERY domain, from the domain root to every level: the "matches
     * deriving from the domain root directly" requirement. */
    for (unsigned d = 0; d < CURZI_DOMAINS; d++) {
        for (unsigned T = 0; T <= LMAX; T++) {
            curzi_err_t e = curzi_tier_derive(g_key[d][0], (uint16_t)d, 0u,
                                              (uint8_t)T, out);
            check(e == CURZI_OK, "ascent from domain root returned OK");
            check(eq32(out, g_key[d][T]), "ascent from root matches the chain");
        }
    }

    /* (b) the full triangle L <= T for a spread of domains: a grant at ANY
     * level reproduces every level above it, one jump or many. */
    static const unsigned probe[] = { 0u, 1u, 2u, 71u, 142u, 143u };
    for (unsigned p = 0; p < sizeof probe / sizeof probe[0]; p++) {
        unsigned d = probe[p];
        for (unsigned L = 0; L <= LMAX; L++) {
            for (unsigned T = L; T <= LMAX; T++) {
                curzi_err_t e = curzi_tier_derive(g_key[d][L], (uint16_t)d,
                                                  (uint8_t)L, (uint8_t)T, out);
                check(e == CURZI_OK, "ascent L->T returned OK");
                check(eq32(out, g_key[d][T]), "ascent L->T matches the chain");
            }
        }
    }

    /* (c) T == L is the identity: it returns what was presented, granting
     * nothing. */
    check(curzi_tier_derive(g_key[9][30], 9u, 30u, 30u, out) == CURZI_OK, "identity OK");
    check(eq32(out, g_key[9][30]), "derive(k,d,L,L) == k");

    /* (d) aliasing: out_key may be the same buffer as parent_key. A caller
     * walking a chain in place is the obvious usage and must not corrupt. */
    {
        uint8_t k[KEYB];
        for (unsigned i = 0; i < KEYB; i++) k[i] = g_key[9][30][i];
        check(curzi_tier_derive(k, 9u, 30u, 45u, k) == CURZI_OK, "aliased derive OK");
        check(eq32(k, g_key[9][45]), "aliased derive is correct");
    }
}

/* ---- 3. descent is refused, and is absent from the forward closure ------- */
static void t3_descent(void)
{
    uint8_t out[KEYB];

    /* (a) API refusal, over the whole lower triangle of one domain. */
    for (unsigned L = 1; L <= LMAX; L++) {
        for (unsigned T = 0; T < L; T++) {
            for (unsigned i = 0; i < KEYB; i++) out[i] = 0xAAu;  /* poison */
            curzi_err_t e = curzi_tier_derive(g_key[7][L], 7u,
                                              (uint8_t)L, (uint8_t)T, out);
            check(e == CURZI_E_TIER_DESCEND, "descent refused with E_TIER_DESCEND");
            check(is_zero32(out), "descent leaves out_key zeroed, never partial");
        }
    }

    /* (b) the closure claim. Everything reachable UPWARD from (d,L) is
     * {K(d,L..61)} and nothing else; in particular no K(d, <L). This is the
     * empirical half of curzi8889a.h:126. */
    for (unsigned L = 0; L <= LMAX; L++) {
        for (unsigned T = L; T <= LMAX; T++) {
            check(curzi_tier_derive(g_key[7][L], 7u, (uint8_t)L, (uint8_t)T, out)
                  == CURZI_OK, "closure walk OK");
            for (unsigned B = 0; B < L; B++) {
                check(!eq32(out, g_key[7][B]),
                      "no key derivable from level L equals a key below L");
            }
        }
    }
}

/* ---- 4. domain isolation ------------------------------------------------ */
static void t4_isolation(void)
{
    uint8_t out[KEYB];

    /* (a) GLOBAL distinctness: all 144 * 62 = 8928 addressable keys differ.
     * O(n^2) on purpose -- a hash-table dedup could mask a collision behind a
     * bucketing bug, and 39.8M 32-byte compares cost under a second. */
    unsigned collisions = 0;
    const uint8_t *flat = &g_key[0][0][0];
    const unsigned n = CURZI_DOMAINS * NLVL;
    for (unsigned i = 0; i < n; i++) {
        for (unsigned j = i + 1u; j < n; j++) {
            if (eq32(flat + (size_t)i * KEYB, flat + (size_t)j * KEYB)) collisions++;
        }
    }
    check(collisions == 0, "all 8928 addressable keys are pairwise distinct");
    printf("  distinctness: %u keys compared pairwise, %u collisions\n", n, collisions);

    /* (b) the isolation claim stated directly, for two named domains. */
    for (unsigned L = 0; L <= LMAX; L++) {
        for (unsigned T = 0; T <= LMAX; T++) {
            check(!eq32(g_key[11][L], g_key[12][T]),
                  "no key of domain 11 equals any key of domain 12");
        }
    }

    /* (c) NO LAUNDERING. A holder of domain 11's key cannot relabel it into
     * domain 12: the domain index is bound into every step, so feeding an
     * 11-key through the 12-chain lands nowhere in domain 12 (nor anywhere in
     * domain 11). Without the domain in the preimage this test would fail and
     * the 144 domains would be one domain wearing 144 hats. */
    for (unsigned L = 0; L < LMAX; L++) {
        check(curzi_tier_derive(g_key[11][L], 12u, (uint8_t)L,
                                (uint8_t)(L + 1u), out) == CURZI_OK, "cross-label OK");
        for (unsigned T = 0; T <= LMAX; T++) {
            check(!eq32(out, g_key[12][T]), "laundered key is not any domain-12 key");
            check(!eq32(out, g_key[11][T]), "laundered key is not any domain-11 key");
        }
    }

    /* (d) a different system root gives a disjoint universe. */
    {
        static uint8_t rootb[CURZI_DOMAINS][KEYB];
        for (unsigned d = 0; d < CURZI_DOMAINS; d++) {
            check(curzi_tier_root(g_sysroot_b, (uint16_t)d, rootb[d]) == CURZI_OK,
                  "root under system root B OK");
        }
        unsigned c2 = 0;
        for (unsigned d = 0; d < CURZI_DOMAINS; d++) {
            for (unsigned e = 0; e < CURZI_DOMAINS; e++) {
                if (eq32(rootb[d], g_key[e][0])) c2++;
            }
        }
        check(c2 == 0, "no domain root under system root B equals one under root A");
    }
}

/* ---- 5. range and null refusals ----------------------------------------- */
static void t5_refusals(void)
{
    uint8_t out[KEYB];
    uint8_t k[KEYB];
    for (unsigned i = 0; i < KEYB; i++) k[i] = g_key[0][0][i];

    static const uint16_t bad_dom[] = { 144u, 145u, 256u, 1000u, 0xFFFFu };
    for (unsigned i = 0; i < sizeof bad_dom / sizeof bad_dom[0]; i++) {
        for (unsigned j = 0; j < KEYB; j++) out[j] = 0xAAu;
        check(curzi_tier_root(g_sysroot_a, bad_dom[i], out) == CURZI_E_TIER_RANGE,
              "root: out-of-range domain refused");
        check(is_zero32(out), "root: out-of-range domain zeroes out_key");

        for (unsigned j = 0; j < KEYB; j++) out[j] = 0xAAu;
        check(curzi_tier_derive(k, bad_dom[i], 0u, 1u, out) == CURZI_E_TIER_RANGE,
              "derive: out-of-range domain refused");
        check(is_zero32(out), "derive: out-of-range domain zeroes out_key");
    }
    check(curzi_tier_root(g_sysroot_a, 143u, out) == CURZI_OK, "domain 143 is valid");

    static const uint8_t bad_lvl[] = { 62u, 63u, 100u, 200u, 255u };
    for (unsigned i = 0; i < sizeof bad_lvl / sizeof bad_lvl[0]; i++) {
        for (unsigned j = 0; j < KEYB; j++) out[j] = 0xAAu;
        check(curzi_tier_derive(k, 0u, 0u, bad_lvl[i], out) == CURZI_E_TIER_RANGE,
              "derive: to_level out of range refused");
        check(is_zero32(out), "derive: bad to_level zeroes out_key");

        for (unsigned j = 0; j < KEYB; j++) out[j] = 0xAAu;
        check(curzi_tier_derive(k, 0u, bad_lvl[i], bad_lvl[i], out) == CURZI_E_TIER_RANGE,
              "derive: from_level out of range refused");
        check(is_zero32(out), "derive: bad from_level zeroes out_key");
    }
    check(curzi_tier_derive(k, 0u, 61u, 61u, out) == CURZI_OK, "level 61 is valid");

    /* RANGE outranks DESCEND when both apply: the coordinates do not exist, so
     * reporting a direction error would be the wrong diagnosis. */
    check(curzi_tier_derive(k, 0u, 200u, 5u, out) == CURZI_E_TIER_RANGE,
          "out-of-range beats descent in the error ordering");
    check(curzi_tier_derive(k, 300u, 5u, 200u, out) == CURZI_E_TIER_RANGE,
          "out-of-range domain beats out-of-range level");

    for (unsigned j = 0; j < KEYB; j++) out[j] = 0xAAu;
    check(curzi_tier_root(0, 0u, out) == CURZI_E_NULL, "root: null system_root refused");
    check(is_zero32(out), "root: null system_root still zeroes out_key (fail closed is total)");
    check(curzi_tier_root(g_sysroot_a, 0u, 0) == CURZI_E_NULL, "root: null out refused");

    for (unsigned j = 0; j < KEYB; j++) out[j] = 0xAAu;
    check(curzi_tier_derive(0, 0u, 0u, 1u, out) == CURZI_E_NULL, "derive: null parent refused");
    check(is_zero32(out), "derive: null parent still zeroes out_key");
    check(curzi_tier_derive(k, 0u, 0u, 1u, 0) == CURZI_E_NULL, "derive: null out refused");
    check(CURZI_OK == 0, "0 is the only success value");

    /* The documented aliasing consequence, pinned so it cannot change silently:
     * a REFUSED call on an aliased buffer destroys the caller's parent key. */
    {
        uint8_t alias[KEYB];
        for (unsigned j = 0; j < KEYB; j++) alias[j] = g_key[0][5][j];
        check(curzi_tier_derive(alias, 0u, 5u, 4u, alias) == CURZI_E_TIER_DESCEND,
              "aliased descent refused");
        check(is_zero32(alias),
              "aliased refusal zeroes the shared buffer (documented in curzi_tier.c)");
    }
}

/* ---- 6. determinism ----------------------------------------------------- */
static void t6_determinism(void)
{
    build(g_key2, g_sysroot_a);
    unsigned diff = 0;
    for (unsigned d = 0; d < CURZI_DOMAINS; d++) {
        for (unsigned L = 0; L <= LMAX; L++) {
            if (!eq32(g_key[d][L], g_key2[d][L])) diff++;
        }
    }
    check(diff == 0, "the whole 8928-key space rebuilds bit-identically");

    uint8_t a[KEYB], b[KEYB];
    for (unsigned i = 0; i < 8; i++) {
        check(curzi_tier_derive(g_key[3][4], 3u, 4u, 60u, a) == CURZI_OK, "repeat OK");
        if (i == 0) { for (unsigned j = 0; j < KEYB; j++) b[j] = a[j]; }
        check(eq32(a, b), "repeated derive is deterministic");
    }
}

/* ---- 7. the tier arithmetic, recomputed rather than trusted -------------- */
static void t7_arithmetic(void)
{
    const unsigned level_keys   = CURZI_DOMAINS * CURZI_LEVELS;   /* 144 * 61 */
    const unsigned domain_roots = CURZI_DOMAINS;
    const unsigned system_root  = 1u;
    const unsigned addressable  = system_root + domain_roots + level_keys;
    const unsigned structural   = system_root + domain_roots;
    const unsigned grantable    = addressable - structural;

    check(level_keys  == 8784u, "144 * 61 == 8784 level keys");
    check(addressable == 8929u, "1 + 144 + 8784 == 8929 addressable, as the header says");
    check(structural  == 145u,  "structural roots are the system root + 144 domain roots");
    check(grantable   == 8784u, "grantable == addressable - structural");
    check(grantable   == level_keys, "the grantable tiers are exactly the level keys");

    /* The key set actually BUILT above is the addressable space minus the one
     * system root, which is never derived from anything. */
    check(CURZI_DOMAINS * NLVL == addressable - system_root,
          "the built table holds every addressable key except the system root");

    printf("\n  ---- TIER ARITHMETIC, MEASURED ----\n");
    printf("    domains ...................... %u\n", (unsigned)CURZI_DOMAINS);
    printf("    levels per domain ............ %u  (indices 1..%u; index 0 == domain root)\n",
           (unsigned)CURZI_LEVELS, (unsigned)CURZI_LEVELS);
    printf("    level keys (144 * 61) ........ %u   GRANTABLE\n", level_keys);
    printf("    domain roots ................. %u    structural\n", domain_roots);
    printf("    system root .................. %u      structural\n", system_root);
    printf("    addressable .................. %u\n", addressable);
    printf("    structural (never handed out)  %u\n", structural);
    printf("    GRANTABLE TIERS .............. %u\n", grantable);
    printf("    CURZI_TIER_GRANTABLE ......... %u\n", (unsigned)CURZI_TIER_GRANTABLE);
    printf("    CURZI_TIER_ADDRESSABLE ....... %u\n", (unsigned)CURZI_TIER_ADDRESSABLE);
    printf("    CURZI_TIER_NAME (identity) ... %u   <- a NAME, not a count\n", (unsigned)CURZI_TIER_NAME);

    /* The header used to claim 8889 GRANTABLE tiers while also stating
     * 144*61 = 8784, which cannot both be true. This test caught it and the
     * header was corrected rather than the structure padded: 8889 = 3 * 2963
     * with 2963 prime, so no (uint16 domain, uint8 level) rectangle can yield
     * it, and inventing filler tiers to reach the label would be making the
     * code lie to match a comment. These are now hard assertions: if anybody
     * "fixes" the constants back to agree with the name, this FAILS. */
    if (grantable != CURZI_TIER_GRANTABLE) {
        printf("  FAIL: grantable %u != CURZI_TIER_GRANTABLE %u\n",
               grantable, (unsigned)CURZI_TIER_GRANTABLE); g_fail++;
    }
    if (addressable != CURZI_TIER_ADDRESSABLE) {
        printf("  FAIL: addressable %u != CURZI_TIER_ADDRESSABLE %u\n",
               addressable, (unsigned)CURZI_TIER_ADDRESSABLE); g_fail++;
    }
    if (CURZI_TIER_GRANTABLE == CURZI_TIER_NAME) {
        printf("  FAIL: the name was turned into a count; see curzi8889a.h LAYER 5\n");
        g_fail++;
    }

    /* The factorisation claim is arithmetic, so check it rather than assert it:
     * no divisor of 8889 in 2..255 means no rectangle with a side this
     * contract's types can express. */
    {
        unsigned small_divisors = 0;
        for (unsigned f = 2u; f <= 255u; f++) if (8889u % f == 0u) small_divisors++;
        check(small_divisors == 1u, "8889 has exactly one divisor in 2..255 (namely 3)");
        check(8889u % 3u == 0u && 8889u / 3u == 2963u, "8889 == 3 * 2963");
        unsigned p = 1u;
        for (unsigned f = 2u; f * f <= 2963u; f++) if (2963u % f == 0u) p = 0u;
        check(p == 1u, "2963 is prime");
    }
}

int main(void)
{
    for (unsigned i = 0; i < KEYB; i++) {
        g_sysroot_a[i] = 0u;
        g_sysroot_b[i] = (uint8_t)(i + 1u);
    }

    printf("CURZI-8889-A LAYER 5 (tiered access) — host test\n");
    printf("  %u domains x %u levels, level index 0 == domain root\n\n",
           (unsigned)CURZI_DOMAINS, (unsigned)CURZI_LEVELS);

    t0_hash_anchor();
    build(g_key, g_sysroot_a);
    t1_wire_format();
    t2_ascent();
    t3_descent();
    t4_isolation();
    t5_refusals();
    t6_determinism();
    t7_arithmetic();

    /* Printed so a third party can recompute them from the formulas in
     * curzi_tier.c with any SHA3-256 and compare, which is the no-backdoor
     * argument's condition 3 (curzi8889a.h:48-49). System root = 32 zero bytes. */
    printf("\n  ---- RECOMPUTABLE VECTORS (system root = 32 x 0x00) ----\n");
    hex32("domain   0 root  :", g_key[0][0]);
    hex32("domain   0 lvl  1:", g_key[0][1]);
    hex32("domain   0 lvl 61:", g_key[0][LMAX]);
    hex32("domain 143 root  :", g_key[143][0]);
    hex32("domain 143 lvl 61:", g_key[143][LMAX]);

    printf("\n  checks run: %u, failures: %u\n", g_checks, g_fail);
    if (g_fail != 0u) {
        printf("RESULT: FAIL\n");
        return 1;
    }
    printf("RESULT: PASS\n");
    return 0;
}
