/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_fusion.c — the socket finds the plug, and the deck grows a new room.
 *
 * Anchored against KNOWN set-algebra answers and against SHA-256 determinism,
 * not against our own prior output. */

#include <stdio.h>
#include <string.h>
#include "fusion.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (cond) {                                                                                \
            g_pass++;                                                                              \
        } else {                                                                                   \
            g_fail++;                                                                              \
            printf("  FAIL: %s (line %d)\n", (msg), __LINE__);                                     \
        }                                                                                          \
    } while (0)

/* Example capability bits — the plug pins / socket pins of this toy ecosystem. */
enum {
    CAP_STORAGE = 1u << 0, /* provides/needs a block store   */
    CAP_CRYPTO = 1u << 1,  /* provides/needs signing         */
    CAP_NET = 1u << 2,     /* provides/needs networking      */
    CAP_UI = 1u << 3       /* provides/needs a display       */
};

/* Distinct 32-byte content digests so we can tell members apart. */
static void fill(uint8_t d[32], uint8_t seed)
{
    for (int i = 0; i < 32; i++) d[i] = (uint8_t) (seed + i);
}

/* Build a program with given provides / needs capability masks. */
static void mk(fuse_program_t *p, uint8_t seed, uint32_t provides, uint32_t needs)
{
    uint8_t id[32], src[32], dp[32], dn[32], d0[32];
    fill(id, seed);
    fill(src, (uint8_t) (seed + 100));
    fill(dp, (uint8_t) (seed + 1));
    fill(dn, (uint8_t) (seed + 2));
    fill(d0, (uint8_t) (seed + 3));
    fuse_program_init(p, id, src);
    fuse_set_provides(p, provides, dp);
    fuse_set_needs(p, needs, dn);
    fuse_set_neutral(p, 0u, d0);
}

static int cid_eq(const uint8_t a[32], const uint8_t b[32])
{
    return memcmp(a, b, 32) == 0;
}

int main(void)
{
    printf("=== fusion: positive space plugs into negative space ===\n");

    /* ---- ANCHOR 1: A provides EXACTLY what B needs -> clean fusion, no unmet ---- */
    {
        fuse_program_t A, B, F;
        mk(&A, 10, CAP_STORAGE | CAP_CRYPTO, 0u);     /* provides both      */
        mk(&B, 20, CAP_UI, CAP_STORAGE | CAP_CRYPTO); /* needs both         */
        CHECK(fuse_can_compose(&A, &B), "A satisfies B fully");
        CHECK(fuse_compose(&A, &B, &F) == FUSE_OK, "compose ok");
        CHECK(fuse_needs(&F) == 0u, "fused artifact has NO unmet needs");
        CHECK(fuse_provides(&F) == (CAP_STORAGE | CAP_CRYPTO | CAP_UI), "fused provides = union");
        CHECK(F.has_cid, "fused artifact is content-addressed");
    }

    /* ---- ANCHOR 2: A provides NONE of what B needs -> refused, nothing made ---- */
    {
        fuse_program_t A, B, F;
        mk(&A, 30, CAP_UI, 0u);                   /* provides UI        */
        mk(&B, 40, 0u, CAP_STORAGE | CAP_CRYPTO); /* needs storage+crypto*/
        CHECK(!fuse_can_compose(&A, &B), "A does NOT satisfy B");
        memset(&F, 0xEE, sizeof F); /* poison out         */
        CHECK(fuse_compose(&A, &B, &F) == FUSE_ERR_UNMET, "compose refused UNMET");
        /* out was NOT written: its CID bytes stay poisoned (avoid reading the
         * poisoned bool has_cid — that would itself be UB). */
        CHECK(F.cid[0] == 0xEE && F.cid[31] == 0xEE,
              "no fused artifact fabricated (out untouched)");
    }

    /* ---- ANCHOR 3: PARTIAL — remaining needs == B.needs minus A.provides ---- */
    {
        fuse_program_t A, B, F;
        mk(&A, 50, CAP_STORAGE, 0u);              /* provides storage   */
        mk(&B, 60, 0u, CAP_STORAGE | CAP_CRYPTO); /* needs storage+crypto*/
        CHECK(!fuse_can_compose(&A, &B), "not a FULL satisfy (partial)");
        CHECK(fuse_compose(&A, &B, &F) == FUSE_OK, "partial compose still ok");
        /* exact set difference: needs & ~provides */
        uint32_t expect = (CAP_STORAGE | CAP_CRYPTO) & ~(uint32_t) CAP_STORAGE;
        CHECK(fuse_needs(&F) == expect, "remaining needs == B.needs - A.provides");
        CHECK(fuse_needs(&F) == CAP_CRYPTO, "specifically: crypto still unmet");
        CHECK(fuse_provides(&F) == CAP_STORAGE, "fused provides = union of provides");
    }

    /* ---- ANCHOR 4: DETERMINISM — same inputs -> same CID; change cap -> diff ---- */
    {
        fuse_program_t A, B, F1, F2;
        mk(&A, 50, CAP_STORAGE, 0u);
        mk(&B, 60, 0u, CAP_STORAGE | CAP_CRYPTO);
        CHECK(fuse_compose(&A, &B, &F1) == FUSE_OK, "compose #1");
        CHECK(fuse_compose(&A, &B, &F2) == FUSE_OK, "compose #2");
        CHECK(cid_eq(F1.cid, F2.cid), "same A,B -> same fused CID");

        /* change one capability bit on A and the fused CID must move */
        fuse_program_t A2, F3;
        mk(&A2, 50, CAP_STORAGE | CAP_NET, 0u); /* A now also nets    */
        CHECK(fuse_compose(&A2, &B, &F3) == FUSE_OK, "compose changed A");
        CHECK(!cid_eq(F1.cid, F3.cid), "changing a PROVIDES bit changes the CID");

        /* changing B's NEEDS must ALSO move the CID — the digest preimage covers
         * provides AND needs (not just provides). */
        fuse_program_t B2, F4;
        mk(&B2, 60, 0u, CAP_STORAGE | CAP_CRYPTO | CAP_NET); /* B now also needs net */
        CHECK(fuse_compose(&A, &B2, &F4) == FUSE_OK, "compose changed-needs B");
        CHECK(!cid_eq(F1.cid, F4.cid), "changing a NEEDS bit also changes the CID");

        /* and fuse_cid is a pure function of the finished contract */
        uint8_t direct[32];
        CHECK(fuse_cid(&F1, direct) == 0, "fuse_cid ok");
        CHECK(cid_eq(direct, F1.cid), "fuse_cid recomputes the same CID");
    }

    /* ---- ANCHOR 5: COMPOSABILITY — fuse(A,B) then C == combined provides/needs ---- */
    {
        /* A provides storage; B needs storage, provides crypto; C needs crypto+net,
         * provides net. Grow the deck one room at a time. */
        fuse_program_t A, B, C, AB, ABC;
        mk(&A, 70, CAP_STORAGE, 0u);
        mk(&B, 80, CAP_CRYPTO, CAP_STORAGE);
        mk(&C, 90, CAP_NET, CAP_CRYPTO | CAP_NET);

        CHECK(fuse_compose(&A, &B, &AB) == FUSE_OK, "fuse A,B");
        CHECK(fuse_provides(&AB) == (CAP_STORAGE | CAP_CRYPTO), "AB provides union");
        CHECK(fuse_needs(&AB) == 0u, "AB fully self-satisfied");

        CHECK(fuse_compose(&AB, &C, &ABC) == FUSE_OK, "fuse AB,C");
        /* combined provides = storage|crypto|net */
        CHECK(fuse_provides(&ABC) == (CAP_STORAGE | CAP_CRYPTO | CAP_NET),
              "ABC provides = union of all three");
        /* combined needs = (all needs) & ~(all provides)
         *   needs total  = storage|crypto|net ; provides total = storage|crypto|net
         *   -> remaining = 0 */
        CHECK(fuse_needs(&ABC) == 0u, "ABC has no remaining needs");

        /* associativity of the needs-formula: a C that still wants UI stays unmet */
        fuse_program_t C2, ABC2;
        mk(&C2, 91, CAP_NET, CAP_CRYPTO | CAP_UI); /* also needs UI      */
        CHECK(fuse_compose(&AB, &C2, &ABC2) == FUSE_OK, "fuse AB,C2");
        CHECK(fuse_needs(&ABC2) == CAP_UI, "UI remains the one unmet need");
    }

    /* ---- edge: consumer that needs NOTHING composes with anyone ---- */
    {
        fuse_program_t A, B, F;
        mk(&A, 12, CAP_UI, 0u);
        mk(&B, 13, CAP_NET, 0u); /* needs nothing      */
        CHECK(fuse_can_compose(&A, &B), "needs-nothing consumer composes");
        CHECK(fuse_compose(&A, &B, &F) == FUSE_OK, "compose ok");
        CHECK(fuse_provides(&F) == (CAP_UI | CAP_NET), "provides union");
        CHECK(fuse_needs(&F) == 0u, "no needs");
    }

    /* ---- NULL safety ---- */
    {
        fuse_program_t A, F;
        mk(&A, 14, CAP_UI, 0u);
        CHECK(fuse_compose(NULL, &A, &F) == FUSE_ERR_NULL, "NULL a rejected");
        CHECK(fuse_compose(&A, NULL, &F) == FUSE_ERR_NULL, "NULL b rejected");
        CHECK(fuse_compose(&A, &A, NULL) == FUSE_ERR_NULL, "NULL out rejected");
        CHECK(!fuse_can_compose(NULL, &A), "NULL provider not composable");
        uint8_t c[32];
        CHECK(fuse_cid(NULL, c) == -1, "NULL cid rejected");
    }

    printf("=== fusion: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
