/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_pirate_fleet.c — assert the NUMBERS, not that the code merely ran.
 *
 * Host-only (TEST_HOST): stdio is allowed HERE and nowhere in the module.
 *
 * The certification anchor uses REAL Ed25519 signatures produced OFFLINE with
 * OpenSSL over a FIXED 32-byte attestation (0x10..0x2f). They are embedded as
 * known-answer vectors and verified by the kernel's own ed25519_verify — no
 * signing key lives in the kernel, and no signature is fabricated. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pirate_fleet.h"

/* On the host surplus_real_t is double; compare with a small epsilon. */
static int near_eq(surplus_real_t a, double b)
{
    return fabs((double) a / (double) SR_ONE - b) < 1e-6; /* raw Q32 on the fixed path */
}

static int g_asserts = 0;
static int g_fails = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        g_asserts++;                                                                               \
        if (!(cond)) {                                                                             \
            g_fails++;                                                                             \
            printf("FAIL [%d]: %s\n", g_asserts, (msg));                                           \
        } else {                                                                                   \
            printf("ok   [%d]: %s\n", g_asserts, (msg));                                           \
        }                                                                                          \
    } while (0)
#define D(x) ((double) (x) / (double) SR_ONE)

/* ---- the fixed 32-byte telemetry attestation the officers signed ---- */
static void fill_attestation(uint8_t att[PF_ATT_LEN])
{
    for (int i = 0; i < (int) PF_ATT_LEN; i++) att[i] = (uint8_t) (0x10 + i);
}

/* ---- hex helper ---- */
static void hx(const char *h, uint8_t *o, int n)
{
    for (int i = 0; i < n; i++) {
        unsigned v;
        sscanf(h + 2 * i, "%2x", &v);
        o[i] = (uint8_t) v;
    }
}

/* Three genuine Ed25519 officers (OpenSSL, offline) over the fixed attestation. */
static const char *OFF_PUB[3] = {
    "226538e47efb2f20e545b04afad18ab4452f548f355711c8786c7f34e0ce4b6a",
    "58c0950241f7f613063471dc9a2fa167366577430fa03e76a3d0a8c568a77340",
    "853044c9e8449d642a02b785c5c1d114844c657fb059e874aace48e3431677e2"};
static const char *OFF_SIG[3] = {"cd7dcb2c118dfac906cd251ffb7d51b63b3824c2f8e1b098053566265e641b90b"
                                 "d395438ddecdf3be36a715ae74e7f5be6f1d7810daafdb02fc362b9003f8705",
                                 "9f1c92895beac401f95a48143d7e4142279d69638c1a5f90373d7e499392473c6"
                                 "85b7f84c5d3c746562efae559ec49601325d40f847e172b4a344e46ff64760e",
                                 "e6cb3df8fd99ea4b90a9e83062023327e2fe7feaf8235d3189f9b5f86027e0575"
                                 "65da3b44335885b6f532d76e1e79edc61a5393e1cc2202b67903dbbdb78fc08"};

static void vec(surplus_real_t v[CON_DIM], int axis, double mag)
{
    for (int i = 0; i < (int) CON_DIM; i++) v[i] = SR_ZERO;
    v[axis] = SR_FROM_FLOAT(mag);
}

int main(void)
{
    printf("=== the Jolly Dragon Rogers Pirate Fleet ===\n");

    /* Large structs stay off the stack (ASan would rightly cry otherwise). */
    static con_commons_t commons;
    static triple_ledger_t tl;
    static vino_stores_t vs;
    static pf_fleet_t fleet;

    con_init(&commons);
    triple_ledger_init(&tl);
    vino_stores_init(&vs, &tl);
    pf_init(&fleet, &commons, &vs);

    uint8_t founders[3][PF_PUBKEY_LEN];
    hx(OFF_PUB[0], founders[0], 32);
    hx(OFF_PUB[1], founders[1], 32);
    hx(OFF_PUB[2], founders[2], 32);

    /* ===== charter registration -> APPLIED ===== */
    {
        int32_t rc = jdr_crew_charter(&fleet, 7, founders, 3, 0x0F);
        CHECK(rc == PF_OK, "charter registers the crew");
        pf_crew_t *cr = 0;
        for (uint32_t i = 0; i < PF_MAX_CREWS; i++)
            if (fleet.crew[i].in_use && fleet.crew[i].crew_id == 7) cr = &fleet.crew[i];
        CHECK(cr && cr->state == CREW_APPLIED, "new charter is in state APPLIED");
        CHECK(cr && cr->flagged_out, "the crew flags OUT of the port, not into it");
        CHECK(jdr_crew_charter(&fleet, 7, founders, 3, 0) == PF_ERR_EXISTS,
              "a duplicate crew_id is refused");
    }

    /* ===== ANCHOR 3: the anti-capture wall ===== */
    {
        CHECK(jdr_cap_wall_ok(PF_CAP_CREDENTIAL),
              "A3: an actor with ONLY the credential cap passes the wall");
        CHECK(jdr_cap_wall_ok(PF_CAP_TREASURY_AUDIT),
              "A3: an actor with ONLY the treasury cap passes the wall");
        CHECK(!jdr_cap_wall_ok(PF_CAP_CREDENTIAL | PF_CAP_TREASURY_AUDIT),
              "A3: an actor holding BOTH caps is REJECTED (separation of mint and money)");

        /* the wall is enforced through the FSM, not merely as a predicate */
        int32_t rc = jdr_crew_transition(&fleet, 7, CREW_CREDENTIALED,
                                         PF_CAP_CREDENTIAL | PF_CAP_TREASURY_AUDIT);
        CHECK(rc == PF_ERR_DENIED, "A3: a both-caps actor cannot drive the FSM at all (wall wins)");

        /* credential step needs the credential cap... */
        CHECK(jdr_crew_transition(&fleet, 7, CREW_CREDENTIALED, PF_CAP_TREASURY_AUDIT) ==
                  PF_ERR_DENIED,
              "A3: credential step refused to an actor lacking CAP_CREDENTIAL");
        CHECK(jdr_crew_transition(&fleet, 7, CREW_CREDENTIALED, PF_CAP_CREDENTIAL) == PF_OK,
              "A3: credential step ALLOWED with CAP_CREDENTIAL alone");
        /* ...and the treasury step needs the treasury cap */
        CHECK(jdr_crew_transition(&fleet, 7, CREW_TREASURY_OPEN, PF_CAP_CREDENTIAL) ==
                  PF_ERR_DENIED,
              "A3: treasury step refused to an actor lacking CAP_TREASURY_AUDIT");
        CHECK(jdr_crew_transition(&fleet, 7, CREW_TREASURY_OPEN, PF_CAP_TREASURY_AUDIT) == PF_OK,
              "A3: treasury step ALLOWED with CAP_TREASURY_AUDIT alone");
        CHECK(jdr_crew_transition(&fleet, 7, CREW_ACTIVE, 0) == PF_OK,
              "A3: the crew reaches ACTIVE");
        /* an illegal skip is refused */
        CHECK(jdr_crew_transition(&fleet, 7, CREW_APPLIED, PF_CAP_CREDENTIAL) == PF_ERR_STATE,
              "A3: an illegal FSM edge is refused");
    }

    /* ===== ANCHORS 1 & 2: ISF stranger-matching ===== */
    {
        surplus_real_t v[CON_DIM];
        /* me: sociable */
        vec(v, 0, 1.0);
        con_join(&commons, 1, v, 200);
        vec(v, 1, 1.0);
        con_join(&commons, 2, v, 200); /* complementary */
        vec(v, 0, 1.0);
        con_join(&commons, 3, v, 200); /* identical to me */
        vec(v, 4, 1.0);
        con_join(&commons, 4, v, 200); /* complementary */

        con_match_t out[16];
        uint32_t n = jdr_match_strangers(&fleet, 1, out, 16);
        CHECK(n >= 2, "A2: matches are offered");
        CHECK(out[0].id != 3, "A2: the identical person is NOT the top pick (no echo chamber)");
        int sorted = 1;
        for (uint32_t i = 1; i < n; i++)
            if (D(out[i].surplus) > D(out[i - 1].surplus) + 1e-9) sorted = 0;
        CHECK(sorted, "A2: offered PURELY by ISF surplus, best first — no re-ranking term");

        /* A1: sociability budget. con_budget = sociability*16/255. */
        vec(v, 0, 1.0);
        con_join(&commons, 50, v, 0); /* deep introvert */
        for (uint32_t k = 0; k < 20; k++) {
            vec(v, (int) (k % CON_DIM), 1.0);
            con_join(&commons, 200 + k, v, 255);
        }
        uint32_t ni = jdr_match_strangers(&fleet, 50, out, 16);
        CHECK(ni == 0, "A1: an introvert (sociability 0, budget 0) is offered ZERO contacts");

        vec(v, 0, 1.0);
        con_join(&commons, 51, v, 255); /* extrovert, budget 16 */
        uint32_t ne = jdr_match_strangers(&fleet, 51, out, 16);
        uint32_t budget51 = (255u * 16u) / 255u; /* == 16 */
        CHECK(ne > 0 && ne <= budget51,
              "A1: matching NEVER exceeds the sociability budget (sociability*16/255)");
    }

    /* ===== ANCHOR 4: node certification quorum (REAL Ed25519) ===== */
    uint32_t node_a = 0, node_b = 0;
    {
        stewardship_deed_t deed;
        memset(&deed, 0, sizeof deed);
        deed.n_officers = 3;
        hx(OFF_PUB[0], deed.officer_pubkey[0], 32);
        hx(OFF_PUB[1], deed.officer_pubkey[1], 32);
        hx(OFF_PUB[2], deed.officer_pubkey[2], 32);
        fill_attestation(deed.attestation);
        deed.telemetry_real = true; /* a REAL metered deed */

        int32_t nid = jdr_node_register(&fleet, 7, &deed);
        CHECK(nid > 0, "A4: a compute node registers as a stewardship deed");
        node_a = (uint32_t) nid;

        /* A4b: a deed with DUPLICATE officer pubkeys is refused — otherwise one
         * signer could replay a single signature across the duplicated slots and
         * fake the 2/3 quorum, self-certifying a node and drawing Vino for work no
         * real quorum witnessed. */
        {
            stewardship_deed_t dup;
            memset(&dup, 0, sizeof dup);
            dup.n_officers = 3;
            hx(OFF_PUB[0], dup.officer_pubkey[0], 32);
            hx(OFF_PUB[0], dup.officer_pubkey[1], 32); /* same key again */
            hx(OFF_PUB[0], dup.officer_pubkey[2], 32); /* and again */
            fill_attestation(dup.attestation);
            dup.telemetry_real = true;
            CHECK(jdr_node_register(&fleet, 7, &dup) == PF_ERR_ARG,
                  "A4b: a deed with duplicate officers is refused (no fake quorum)");
        }

        /* build the three genuine signatures */
        uint8_t sigs[3][PF_SIG_LEN];
        hx(OFF_SIG[0], sigs[0], 64);
        hx(OFF_SIG[1], sigs[1], 64);
        hx(OFF_SIG[2], sigs[2], 64);

        /* --- 1 of 3 valid: below 2/3 --- REJECT --- */
        uint8_t bad[3][PF_SIG_LEN];
        memcpy(bad, sigs, sizeof bad);
        bad[1][0] ^= 0x01; /* corrupt officer 1's sig */
        bad[2][0] ^= 0x01; /* corrupt officer 2's sig */
        int32_t rc = jdr_node_certify(&fleet, node_a, (const uint8_t(*)[PF_SIG_LEN]) bad, 3);
        CHECK(rc == PF_ERR_QUORUM, "A4: 1/3 valid officers is REJECTED (< 2/3 quorum)");
        pf_node_t *nda = 0;
        for (uint32_t i = 0; i < PF_MAX_NODES; i++)
            if (fleet.node[i].in_use && fleet.node[i].node_id == node_a) nda = &fleet.node[i];
        CHECK(nda && !nda->certified, "A4: the node stays UNcertified after a short quorum");

        /* --- 2 of 3 valid: exactly 2/3 --- ACCEPT --- */
        uint8_t two[3][PF_SIG_LEN];
        memcpy(two, sigs, sizeof two);
        two[2][0] ^= 0x01; /* only officer 2 corrupted -> 2 valid */
        rc = jdr_node_certify(&fleet, node_a, (const uint8_t(*)[PF_SIG_LEN]) two, 3);
        CHECK(rc == PF_OK, "A4: 2/3 valid officers is ACCEPTED (>= 2/3 quorum)");
        CHECK(nda && nda->certified, "A4: the node is now certified");
    }

    /* ===== ANCHOR 5: Vino accrues ONLY for verified metered work ===== */
    {
        /* --- an UNVERIFIED / SIMULATED node earns NOTHING --- */
        stewardship_deed_t sim;
        memset(&sim, 0, sizeof sim);
        sim.n_officers = 3;
        hx(OFF_PUB[0], sim.officer_pubkey[0], 32);
        hx(OFF_PUB[1], sim.officer_pubkey[1], 32);
        hx(OFF_PUB[2], sim.officer_pubkey[2], 32);
        fill_attestation(sim.attestation);
        sim.telemetry_real = false; /* SIMULATED work */
        int32_t nid = jdr_node_register(&fleet, 7, &sim);
        CHECK(nid > 0, "A5: a simulated-work node also registers");
        node_b = (uint32_t) nid;

        /* certify it with the genuine quorum so ONLY the telemetry gate remains */
        uint8_t sigs[3][PF_SIG_LEN];
        hx(OFF_SIG[0], sigs[0], 64);
        hx(OFF_SIG[1], sigs[1], 64);
        hx(OFF_SIG[2], sigs[2], 64);
        CHECK(jdr_node_certify(&fleet, node_b, (const uint8_t(*)[PF_SIG_LEN]) sigs, 3) == PF_OK,
              "A5: the simulated node passes the officer quorum (certification != real work)");

        surplus_real_t assets_before = tl.total_assets;
        int32_t rc = jdr_accrue_vino(&fleet, node_b, SR_FROM_INT(500));
        CHECK(rc == PF_ERR_NO_TELEMETRY,
              "A5: SIMULATED work accrues NOTHING (typed not-available, never invented)");
        CHECK(SR_CMP(tl.total_assets, assets_before) == 0,
              "A5: the ledger is untouched by simulated work (ZERO accrual)");

        /* accrual before certification is likewise refused */
        stewardship_deed_t raw;
        memset(&raw, 0, sizeof raw);
        raw.n_officers = 3;
        hx(OFF_PUB[0], raw.officer_pubkey[0], 32);
        hx(OFF_PUB[1], raw.officer_pubkey[1], 32);
        hx(OFF_PUB[2], raw.officer_pubkey[2], 32);
        fill_attestation(raw.attestation);
        raw.telemetry_real = true;
        int32_t rnid = jdr_node_register(&fleet, 7, &raw);
        CHECK(jdr_accrue_vino(&fleet, (uint32_t) rnid, SR_FROM_INT(500)) == PF_ERR_UNCERTIFIED,
              "A5: accrual before certification is refused (fail closed)");

        /* --- the CERTIFIED, REAL-telemetry node (node_a) earns positive Vino --- */
        assets_before = tl.total_assets;
        surplus_real_t liab_before = tl.total_liabilities;
        surplus_real_t work = SR_FROM_INT(500);
        rc = jdr_accrue_vino(&fleet, node_a, work);
        CHECK(rc == PF_OK, "A5: VERIFIED metered work accrues Vino through the triple rail");
        surplus_real_t d_assets = SR_SUB(tl.total_assets, assets_before);
        surplus_real_t d_liab = SR_SUB(tl.total_liabilities, liab_before);
        /* debit 2w / credit w -> assets +1000, liabilities +500 on w=500 */
        CHECK(near_eq(d_assets, 1000.0), "A5: assets rise by 2*work == 1000 (positive, real)");
        CHECK(near_eq(d_liab, 500.0), "A5: liabilities rise by work == 500");
        surplus_real_t equity = SR_SUB(tl.total_assets, tl.total_liabilities);
        CHECK(SR_CMP(equity, SR_ZERO) >= 0, "A5: system equity stays >= 0 (no debt)");
        /* coverage assets/liab must remain >= 1.8x */
        surplus_real_t cov = SR_DIV(tl.total_assets, tl.total_liabilities);
        surplus_real_t floor18 = SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10));
        CHECK(SR_CMP(cov, floor18) >= 0, "A5: ledger coverage stays >= 1.8x after accrual");

        /* zero/negative work earns nothing even when everything else is valid */
        assets_before = tl.total_assets;
        CHECK(jdr_accrue_vino(&fleet, node_a, SR_ZERO) == PF_ERR_NO_WORK,
              "A5: zero metered work earns nothing");
        CHECK(SR_CMP(tl.total_assets, assets_before) == 0,
              "A5: ledger untouched by a zero-work claim");
    }

    /* ===== channels + the Chiglet companion ===== */
    {
        const uint8_t hail[] = "Ahoy — the ledger is sovereign; everybody eats.";
        int32_t rc = jdr_channel_post(&fleet, 7, hail, (uint32_t) sizeof(hail) - 1);
        CHECK(rc == PF_OK, "channel: a post is anchored to the crew's channel");
        CHECK(jdr_channel_depth(&fleet) >= 1, "channel: the post is retained on-device");
        CHECK(jdr_channel_post(&fleet, 999, hail, 4) == PF_ERR_NOT_FOUND,
              "channel: posting to a non-existent crew fails closed");

        /* the companion reads complementary participants as high-affinity */
        surplus_real_t a[CHG_DIM], b[CHG_DIM], same[CHG_DIM];
        vec(a, 0, 1.0);
        vec(b, 3, 1.0);
        for (int i = 0; i < (int) CHG_DIM; i++) same[i] = a[i];
        CHECK(D(jdr_channel_affinity(a, b)) > 0.9,
              "channel companion: complementary participants read HIGH (chg_interaction)");
        CHECK(D(jdr_channel_affinity(a, same)) < 0.05,
              "channel companion: identical participants read LOW (no echo chamber)");
    }

    /* ===== fail-closed when the Vino engine is UNBOUND ===== */
    {
        static pf_fleet_t bare;
        pf_init(&bare, &commons, 0 /* no settlement engine */);
        stewardship_deed_t deed;
        memset(&deed, 0, sizeof deed);
        deed.n_officers = 3;
        hx(OFF_PUB[0], deed.officer_pubkey[0], 32);
        hx(OFF_PUB[1], deed.officer_pubkey[1], 32);
        hx(OFF_PUB[2], deed.officer_pubkey[2], 32);
        fill_attestation(deed.attestation);
        deed.telemetry_real = true;
        jdr_crew_charter(&bare, 1, founders, 3, 0);
        int32_t nid = jdr_node_register(&bare, 1, &deed);
        uint8_t sigs[3][PF_SIG_LEN];
        hx(OFF_SIG[0], sigs[0], 64);
        hx(OFF_SIG[1], sigs[1], 64);
        hx(OFF_SIG[2], sigs[2], 64);
        jdr_node_certify(&bare, (uint32_t) nid, (const uint8_t(*)[PF_SIG_LEN]) sigs, 3);
        CHECK(jdr_accrue_vino(&bare, (uint32_t) nid, SR_FROM_INT(500)) == PF_ERR_NO_BACKEND,
              "ops boundary: with NO settlement engine bound, accrual fails closed");
    }

    printf("\n=== %d assertions, %d failures ===\n", g_asserts, g_fails);
    return g_fails ? 1 : 0;
}
