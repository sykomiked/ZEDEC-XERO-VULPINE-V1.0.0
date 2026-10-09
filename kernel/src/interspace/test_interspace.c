/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_interspace.c — known-answer tests for the interstitial commons.
 *
 * A test that only checks "the code ran" cannot catch a numeric bug, so every
 * function that produces a number is asserted against a HAND-COMPUTED answer:
 * the York-Antwerp general-average worked example, the salvage schedule, the
 * safe-passage deadline arithmetic, and the cryptographic immunity/recognition
 * flips. Host build only (TEST_HOST): stdio lives here, never in the module. */

#include <stdio.h>
#include <math.h>
#include "interspace.h"
#include "onepolicy.h"
#include "constellation_coordinator.h"

static int g_asserts = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do {                                          \
    g_asserts++;                                                       \
    if (!(cond)) { g_fail++; printf("  FAIL: %s\n", msg); }            \
} while (0)

/* surplus_real_t is double under TEST_HOST; compare with a tolerance. */
static int near(surplus_real_t a, double b) { return fabs((double)a - b) < 1e-9; }

/* ===== 1. GENERAL AVERAGE ===== */
static void test_general_average(void) {
    printf("[general average]\n");

    /* York-Antwerp worked example: values {50000,45000,5000} (total 100000),
     * sacrifice 10000 -> {5000,4500,500}, sum EXACTLY 10000, each ratio 0.10. */
    ga_party_t p3[3] = {
        { 0, SR_FROM_INT(50000) },
        { 1, SR_FROM_INT(45000) },
        { 2, SR_FROM_INT(5000)  },
    };
    surplus_real_t out[3];
    zxv_status_t st = lex_rhodia_general_average(SR_FROM_INT(10000), p3, 3, out);
    CHECK(st == ZXV_OK, "GA york-antwerp returns OK");
    CHECK(near(out[0], 5000.0), "GA contrib[0]==5000");
    CHECK(near(out[1], 4500.0), "GA contrib[1]==4500");
    CHECK(near(out[2], 500.0),  "GA contrib[2]==500");
    surplus_real_t sum = SR_ADD(SR_ADD(out[0], out[1]), out[2]);
    CHECK(near(sum, 10000.0), "GA conserves: sum==sacrifice EXACTLY");
    /* each contrib/stake == 0.10 */
    CHECK(near(SR_DIV(out[0], p3[0].stake), 0.10), "GA ratio0==0.10");
    CHECK(near(SR_DIV(out[1], p3[1].stake), 0.10), "GA ratio1==0.10");
    CHECK(near(SR_DIV(out[2], p3[2].stake), 0.10), "GA ratio2==0.10");

    /* stakes {2,3,5} sacrifice 100 -> {20,30,50} sum 100 */
    ga_party_t q[3] = { {0,SR_FROM_INT(2)}, {1,SR_FROM_INT(3)}, {2,SR_FROM_INT(5)} };
    surplus_real_t o2[3];
    CHECK(lex_rhodia_general_average(SR_FROM_INT(100), q, 3, o2) == ZXV_OK, "GA 235 OK");
    CHECK(near(o2[0], 20.0), "GA 235 -> 20");
    CHECK(near(o2[1], 30.0), "GA 235 -> 30");
    CHECK(near(o2[2], 50.0), "GA 235 -> 50");
    CHECK(near(SR_ADD(SR_ADD(o2[0],o2[1]),o2[2]), 100.0), "GA 235 conserves");

    /* zero-stake party contributes 0 */
    ga_party_t z[2] = { {0, SR_ZERO}, {1, SR_FROM_INT(10)} };
    surplus_real_t oz[2];
    CHECK(lex_rhodia_general_average(SR_FROM_INT(5), z, 2, oz) == ZXV_OK, "GA zero-stake OK");
    CHECK(near(oz[0], 0.0), "GA zero-stake party -> 0");
    CHECK(near(oz[1], 5.0), "GA the other party carries all 5");

    /* total_stake == 0 -> EDEGEN (never divide by zero) */
    ga_party_t d[2] = { {0, SR_ZERO}, {1, SR_ZERO} };
    surplus_real_t od[2];
    CHECK(lex_rhodia_general_average(SR_FROM_INT(5), d, 2, od) == ZXV_EDEGEN,
          "GA total_stake==0 -> EDEGEN");
    CHECK(lex_rhodia_general_average(SR_FROM_INT(5), p3, 0, out) == ZXV_EDEGEN,
          "GA n==0 -> EDEGEN");
}

/* ===== 2. SALVAGE ===== */
static void test_salvage(void) {
    printf("[salvage]\n");
    surplus_real_t a;
    CHECK(lex_rhodia_salvage_award(SR_FROM_INT(1000), SR_ZERO, &a) == ZXV_OK, "salvage OK");
    CHECK(near(a, 100.0), "salvage risk 0 -> 100 (base 0.10)");

    CHECK(lex_rhodia_salvage_award(SR_FROM_INT(1000), SR_ONE, &a) == ZXV_OK, "salvage r1 OK");
    CHECK(near(a, 500.0), "salvage risk 1.0 -> 500 (0.10+0.40)");

    CHECK(lex_rhodia_salvage_award(SR_FROM_INT(1000), SR_FROM_FLOAT(2.25), &a) == ZXV_OK, "salvage r2.25 OK");
    CHECK(near(a, 1000.0), "salvage risk 2.25 -> 1000 (clamped rate==1)");

    CHECK(lex_rhodia_salvage_award(SR_FROM_INT(1000), SR_FROM_INT(9), &a) == ZXV_OK, "salvage r9 OK");
    CHECK(near(a, 1000.0), "salvage huge risk still clamped to salved (never exceeds)");

    /* monotone non-decreasing in risk, and 0 < award <= salved throughout */
    surplus_real_t prev = SR_ZERO;
    int monotone = 1, bounded = 1;
    for (int i = 0; i <= 30; i++) {
        surplus_real_t r = SR_DIV(SR_FROM_INT(i), SR_FROM_INT(10)); /* 0.0 .. 3.0 */
        surplus_real_t w;
        lex_rhodia_salvage_award(SR_FROM_INT(1000), r, &w);
        if (SR_CMP(w, prev) < 0) monotone = 0;
        if (!(SR_CMP(w, SR_ZERO) > 0 && SR_CMP(w, SR_FROM_INT(1000)) <= 0)) bounded = 0;
        prev = w;
    }
    CHECK(monotone, "salvage monotone non-decreasing in risk");
    CHECK(bounded,  "salvage 0 < award <= salved for all risk");

    /* monotone in salved too */
    surplus_real_t w1, w2;
    lex_rhodia_salvage_award(SR_FROM_INT(500),  SR_ONE, &w1);
    lex_rhodia_salvage_award(SR_FROM_INT(1000), SR_ONE, &w2);
    CHECK(SR_CMP(w2, w1) > 0, "salvage monotone in salved");

    /* degenerate salved */
    CHECK(lex_rhodia_salvage_award(SR_ZERO, SR_ONE, &a) == ZXV_EDEGEN, "salvage salved 0 -> EDEGEN");
}

/* ===== 3. SOVEREIGN IMMUNITY ===== */
static void test_immunity(void) {
    printf("[sovereign immunity]\n");
    uint8_t content[] = "we are a free ship";
    uint8_t sig[64];
    for (int i = 0; i < 64; i++) sig[i] = (uint8_t)i;

    vessel_flag_t v;
    vessel_flag_raise(&v, content, sizeof(content), 7, sig);
    CHECK(vessel_flag_verify(&v, content, sizeof(content)), "correctly-formed vessel verifies TRUE");

    /* flip one content byte -> the recomputed hash diverges -> FALSE */
    content[0] ^= 0x01;
    CHECK(!vessel_flag_verify(&v, content, sizeof(content)),
          "flip one content byte -> verify FALSE (seizure impossible)");
    content[0] ^= 0x01; /* restore */
    CHECK(vessel_flag_verify(&v, content, sizeof(content)), "restore byte -> verify TRUE again");

    /* immunity vests in the keyholder alone */
    CHECK(vessel_immune_from(&v, 7), "immunity vests in keyholder 7");
    CHECK(!vessel_immune_from(&v, 3), "non-keyholder 3 gets false");
    CHECK(!vessel_immune_from(&v, 0), "non-keyholder 0 gets false");
    CHECK(!vessel_immune_from(&v, ZXV_NODE_NONE), "NODE_NONE holds no immunity");
}

/* ===== 4. SAFE PASSAGE ===== */
static void test_safe_passage(void) {
    printf("[safe passage]\n");
    vessel_flag_t v;
    uint8_t c[4] = { 1,2,3,4 };
    vessel_flag_raise(&v, c, sizeof(c), 2, NULL);

    safe_passage_grant_t g;
    safe_passage_request(&g, &v, 9 /*through*/, 100 /*issued*/, 50 /*ttl*/);
    CHECK(g.ttl_deadline == 150, "safe passage deadline == issued+ttl == 150");
    CHECK(safe_passage_valid(&g, 149), "valid at 149 (before deadline)");
    CHECK(safe_passage_valid(&g, 150), "valid at 150 (deadline INCLUSIVE)");
    CHECK(!safe_passage_valid(&g, 151), "invalid at 151 (after deadline)");
    CHECK(!safe_passage_valid(&g, 99),  "invalid at 99 (non-retroactive, before issue)");
    CHECK(safe_passage_valid(&g, 100),  "valid at 100 (issue tick)");

    /* saturating ttl never wraps into the past */
    safe_passage_request(&g, &v, 9, 100, UINT64_MAX);
    CHECK(g.ttl_deadline == UINT64_MAX, "huge ttl saturates, does not wrap");
}

/* ===== 5. COMMONS ===== */
static void test_commons(void) {
    printf("[interstitial commons]\n");
    uint8_t rid[32]; for (int i = 0; i < 32; i++) rid[i] = (uint8_t)(i*7);

    interstitial_region_t r;
    interstitial_open(&r, rid, ZXV_RES_COMMUNIS);
    CHECK(r.owner == ZXV_NODE_NONE, "commons owner pinned to NODE_NONE");
    CHECK(interstitial_is_commons(&r), "region is res communis");
    CHECK(interstitial_claim(&r, 5) == ZXV_EIMMUNE, "claim on commons -> EIMMUNE");
    CHECK(r.owner == ZXV_NODE_NONE, "owner still NODE_NONE after a claim attempt");

    interstitial_region_t rn;
    interstitial_open(&rn, rid, ZXV_RES_NULLIUS);
    CHECK(rn.owner == ZXV_NODE_NONE, "res nullius owner also pinned");
    CHECK(!interstitial_is_commons(&rn), "res nullius is not res communis");
    CHECK(interstitial_claim(&rn, 5) == ZXV_EPERM, "claim on res nullius -> EPERM (no seizure)");
}

/* ===== 6. FLAG-STATE CO-JURISDICTION ===== */
static void test_cojurisdiction(void) {
    printf("[co-jurisdiction]\n");
    uint8_t content[] = "cargo manifest";
    vessel_flag_t v;
    vessel_flag_raise(&v, content, sizeof(content), 4, NULL);

    op_term_t good = {0};
    good.give_a = SR_FROM_INT(10); good.give_b = SR_FROM_INT(10);
    good.reciprocal = true;
    CHECK(op_symbiotic_ok(&good), "sanity: good term is symbiotic");

    op_term_t usury = good;
    usury.interest = SR_FROM_INT(1);   /* -> OP_VOID_USURY */
    CHECK(!op_symbiotic_ok(&usury), "sanity: usurious term is not symbiotic");

    cojurisdiction_t c = { &good, v };
    CHECK(cojurisdiction_eval(&c, content, sizeof(content), 4) == COJ_ALLOW,
          "host symbiotic + sovereign intact -> ALLOW");

    c.host_flag_term = &usury;
    CHECK(cojurisdiction_eval(&c, content, sizeof(content), 4) == COJ_DENY_HOST,
          "host term fails One Policy -> DENY_HOST");

    /* tamper the content: sovereign flag no longer matches */
    c.host_flag_term = &good;
    content[0] ^= 0x20;
    CHECK(cojurisdiction_eval(&c, content, sizeof(content), 4) == COJ_DENY_SOVEREIGN,
          "content tampered -> DENY_SOVEREIGN");
    content[0] ^= 0x20;

    /* identity bypasses the host flag entirely */
    c.host_flag_term = &usury;   /* even with a hostile port... */
    CHECK(cojurisdiction_identity_ok(&c, 4), "identity of keyholder OK despite hostile port");
    CHECK(!cojurisdiction_identity_ok(&c, 1), "identity of non-keyholder denied");
}

/* ===== 7. FEDERATION ===== */
static void test_federation(void) {
    printf("[federation]\n");
    uint8_t charterA[] = "charter of microstate A";
    uint8_t charterB[] = "charter of microstate B";

    microstate_t A, B;
    CHECK(microstate_constitute(&A, 0, charterA, sizeof(charterA)) == ZXV_OK, "A constitutes");
    CHECK(microstate_constitute(&B, 1, charterB, sizeof(charterB)) == ZXV_OK, "B constitutes");

    /* identity is stable and non-zero BEFORE any recognition */
    int nonzero = 0; for (int i = 0; i < 32; i++) if (A.identity_cid[i]) nonzero = 1;
    CHECK(nonzero, "A.identity_cid non-zero before recognition");
    microstate_t A2;
    microstate_constitute(&A2, 0, charterA, sizeof(charterA));
    int stable = 1; for (int i = 0; i < 32; i++) if (A2.identity_cid[i] != A.identity_cid[i]) stable = 0;
    CHECK(stable, "identity_cid is stable (deterministic) for same self+charter");

    /* seal B's writ, then A recognises B */
    microstate_seal_writ(&B);
    CHECK(microstate_recognize(&A, &B) == ZXV_OK, "A recognises a well-sealed B");
    CHECK(microstate_is_recognized(&A, 1), "A now recognises node 1");
    CHECK(!microstate_is_recognized(&A, 2), "A does not recognise an unrelated node");

    /* bad writ sig -> EBADSIG (recognition theatre is prohibited) */
    microstate_t Bbad = B;
    Bbad.writ_sig[5] ^= 0xFF;
    microstate_t A3; microstate_constitute(&A3, 0, charterA, sizeof(charterA));
    CHECK(microstate_recognize(&A3, &Bbad) == ZXV_EBADSIG, "bad writ sig -> EBADSIG");
    CHECK(!microstate_is_recognized(&A3, 1), "a rejected peer is NOT admitted");

    /* forging a byte in the RESERVED tail (32..63) of the 64-byte writ_sig must
     * ALSO be rejected — the whole advertised field is verified, not just [0..31]. */
    microstate_t Btail = B;
    Btail.writ_sig[40] = 0xEE;
    microstate_t A4; microstate_constitute(&A4, 0, charterA, sizeof(charterA));
    CHECK(microstate_recognize(&A4, &Btail) == ZXV_EBADSIG,
          "a forged byte in the reserved writ_sig tail -> EBADSIG (whole field verified)");

    /* substituted charter -> identity no longer recomputes -> ECHARTER */
    microstate_t Bcharter = B;
    Bcharter.charter_cid[0] ^= 0xFF;    /* identity_cid no longer matches */
    CHECK(microstate_recognize(&A3, &Bcharter) == ZXV_ECHARTER, "substituted charter -> ECHARTER");

    /* federation anchor recomputes, never a cached true */
    uint8_t anchor[32];
    CHECK(federation_anchor(&A, anchor) == ZXV_OK, "anchor computed");
    CHECK(federation_verify(&A, anchor), "anchor verifies (recomputed)");
    anchor[0] ^= 0x01;
    CHECK(!federation_verify(&A, anchor), "tampered anchor fails verify");

    /* ---- council ---- */
    /* 9 members: amendment threshold ceil(2*9/3)=6 */
    council_t co = { 0x1FF /*9 seats*/, 0 };
    CHECK(council_carries(&co, 6, true),  "9 members: 6 yes carries amendment (2/3 line)");
    CHECK(!council_carries(&co, 5, true), "9 members: 5 yes fails amendment (below 2/3)");
    /* simple majority for ordinary business: 5 of 9 */
    CHECK(council_carries(&co, 5, false),  "9 members: 5 yes carries ordinary (simple majority)");
    CHECK(!council_carries(&co, 4, false), "9 members: 4 yes fails ordinary (not a majority)");

    /* an EMPTY council carries nothing — 0 votes must not pass a 2/3 amendment */
    council_t empty = { 0, 0 };
    CHECK(!council_carries(&empty, 0, true),  "empty council: 0 yes does NOT carry an amendment");
    CHECK(!council_carries(&empty, 0, false), "empty council: 0 yes does NOT carry ordinary business");

    /* the reserved EIGHTH office can never be bound */
    CHECK(council_bind_office(&co, 0) == ZXV_OK, "office 0 binds");
    CHECK(council_bind_office(&co, ZXV_COUNCIL_EIGHTH_OFFICE) == ZXV_EPERM,
          "the Eighth office can NEVER bind -> EPERM");
    CHECK((co.office_mask & (1u << ZXV_COUNCIL_EIGHTH_OFFICE)) == 0,
          "the Eighth seat stayed empty");
    CHECK(council_bind_office(&co, 40) == ZXV_EPERM, "out-of-range office -> EPERM");
}

/* ===== 8. MINISTER (composes constellation consent) ===== */
static void test_minister(void) {
    printf("[minister of interstitial affairs]\n");
    cc_coordinator_t cc;
    cc_coordinator_init(&cc, "interspace.local");
    int32_t i0 = cc_register_node(&cc, "node0", "arm64", 1);   /* -> slot 0 */
    int32_t i1 = cc_register_node(&cc, "node1", "arm64", 1);   /* -> slot 1 */
    CHECK(i0 == 0 && i1 == 1, "two nodes registered into slots 0 and 1");
    /* drive node0 to ACTIVE (consenting); leave node1 merely discovered */
    cc_node_authenticate(&cc, 0);
    cc_node_activate(&cc, 0);

    minister_t m; minister_init(&m, &cc);
    CHECK(minister_node_consents(&m, 0), "active node 0 has consented");
    CHECK(!minister_node_consents(&m, 1), "un-activated node 1 has NOT consented");
    CHECK(!minister_node_consents(&m, ZXV_NODE_NONE), "NODE_NONE never consents");

    ga_party_t parties[2] = { {0, SR_FROM_INT(3)}, {1, SR_FROM_INT(1)} };
    surplus_real_t contrib[2];
    uint32_t bound = 0xFFFFFFFF;
    CHECK(minister_adjudicate_average(&m, SR_FROM_INT(4), parties, 2, contrib, &bound) == ZXV_OK,
          "minister adjudicates OK");
    /* numbers are computed for BOTH: 4 * 3/4 = 3 and 4 * 1/4 = 1 */
    CHECK(near(contrib[0], 3.0), "party0 fair share == 3");
    CHECK(near(contrib[1], 1.0), "party1 fair share == 1 (computed even if unbound)");
    /* but only the consenting node is BOUND */
    CHECK(bound == 0x1u, "only consenting node 0 is bound (bit0 set, bit1 clear)");

    /* the Minister cannot seize — ever */
    interstitial_region_t r; uint8_t rid[32] = {0};
    interstitial_open(&r, rid, ZXV_RES_COMMUNIS);
    CHECK(!minister_may_seize(&m, &r), "minister_may_seize is always false");

    /* no fabric bound -> nobody consents (fail closed) */
    minister_t m0; minister_init(&m0, NULL);
    CHECK(!minister_node_consents(&m0, 0), "no fabric -> no consent (fail closed)");
}

int main(void) {
    printf("=== interspace: the law older than yours ===\n");
    test_general_average();
    test_salvage();
    test_immunity();
    test_safe_passage();
    test_commons();
    test_cojurisdiction();
    test_federation();
    test_minister();
    printf("\n%d assertions, %d failures\n", g_asserts, g_fail);
    if (g_fail == 0) printf("ALL PASS — protection, not predation.\n");
    return g_fail ? 1 : 0;
}
