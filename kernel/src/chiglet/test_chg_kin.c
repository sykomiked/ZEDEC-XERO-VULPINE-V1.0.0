/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_chg_kin.c — host tests for digital DNA, hobbies and visits.
 *
 *  K1  DNA records: deterministic, self-certifying, unique per registry
 *  K2  hobbies: 3-5 distinct picks, unique sets per agency
 *  K3  deviation without drift: bounded steps, task first, reset at the cap,
 *      growth-band-only budget, tie-break that never beats a better score
 *  K4  grants: both owners sign, replay and revoke refused, scope enforced
 *  K5  experience: checker-passed only, ISF-scaled, diminishing per friend,
 *      hash-chained log the owner can recompute
 */
#include <stdio.h>
#include <string.h>

#include "chg_kin.h"
#include "keccak.h"

static int g_pass, g_fail;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            g_pass++;                                                                              \
        else {                                                                                     \
            g_fail++;                                                                              \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                  \
        }                                                                                          \
    } while (0)

static uint64_t g_rng = 0xf0c5f0c5deadbeefull;
static uint64_t rnd(void)
{
    g_rng ^= g_rng >> 12;
    g_rng ^= g_rng << 25;
    g_rng ^= g_rng >> 27;
    return g_rng * 0x2545f4914f6cdd1dull;
}

static uint8_t OWN_A[32], OWN_B[32], SPECIES[32], LINEAGE[32];
static chgk_registry_t g_reg;
static chgk_dna_t g_a, g_b;

/* ===== K1 ===== */
static void k1_dna(void)
{
    printf("K1 digital DNA\n");
    memset(OWN_A, 0xa1, 32);
    memset(OWN_B, 0xb2, 32);
    memset(SPECIES, 0x5e, 32);
    memset(LINEAGE, 0x11, 32);
    chgk_registry_init(&g_reg);
    chgk_dna_t x, y;
    CHECK(chgk_dna_birth(0, OWN_A, SPECIES, LINEAGE, 0, 77, &x) == CHGK_OK);
    CHECK(chgk_dna_birth(0, OWN_A, SPECIES, LINEAGE, 0, 77, &y) == CHGK_OK);
    CHECK(memcmp(&x, &y, sizeof(x)) == 0); /* same inputs, same chiglet */
    uint8_t enc[CHGK_DNA_ENC + 1], id[32];
    uint32_t n = chgk_dna_encode(&x, enc, sizeof(enc));
    CHECK(n == CHGK_DNA_ENC);
    sha3_256(enc, n, id);
    CHECK(memcmp(id, x.id, 32) == 0);
    CHECK(chgk_dna_decode(enc, n, &y) == CHGK_OK && memcmp(&x, &y, sizeof(x)) == 0);
    CHECK(chgk_dna_decode(enc, n - 1, &y) == CHGK_ERR_PARSE);
    enc[4 + 128 + 8] ^= 1; /* a gene the seed did not grow */
    CHECK(chgk_dna_decode(enc, n, &y) == CHGK_ERR_PARSE);
    enc[4 + 128 + 8] ^= 1;
    enc[n - 4] = 3; /* generation without a parent */
    CHECK(chgk_dna_decode(enc, n, &y) == CHGK_ERR_PARSE);

    /* uniqueness: the same birth twice in one registry re-seeds */
    CHECK(chgk_dna_birth(&g_reg, OWN_A, SPECIES, LINEAGE, 0, 77, &g_a) == CHGK_OK);
    CHECK(chgk_dna_birth(&g_reg, OWN_A, SPECIES, LINEAGE, 0, 77, &y) == CHGK_OK);
    CHECK(y.seed == 78 && memcmp(y.id, g_a.id, 32) != 0);
    CHECK(chgk_dna_birth(&g_reg, OWN_B, SPECIES, LINEAGE, 0, 9001, &g_b) == CHGK_OK);

    /* the agency's family tree is a hash chain */
    chgk_dna_t kid;
    CHECK(chgk_dna_birth(&g_reg, OWN_A, SPECIES, LINEAGE, &g_a, 5, &kid) == CHGK_OK);
    CHECK(memcmp(kid.parent, g_a.id, 32) == 0 && kid.generation == 1);
    n = chgk_dna_encode(&kid, enc, sizeof(enc));
    CHECK(chgk_dna_decode(enc, n, &y) == CHGK_OK && memcmp(y.id, kid.id, 32) == 0);

    CHECK(chgk_dna_distance(&g_a, &g_a) == 0);
    CHECK(chgk_dna_distance(&g_a, &g_b) == chgk_dna_distance(&g_b, &g_a));
    bool ok = true;
    uint32_t lo = 1000, hi = 0;
    for (uint32_t i = 0; i < 300; i++) {
        chgk_dna_birth(0, OWN_A, SPECIES, LINEAGE, 0, rnd(), &x);
        chgk_dna_birth(0, OWN_B, SPECIES, LINEAGE, 0, rnd(), &y);
        uint32_t d = chgk_dna_distance(&x, &y);
        if (d > 1000) ok = false;
        if (d < lo) lo = d;
        if (d > hi) hi = d;
    }
    CHECK(ok && hi > lo + 300); /* genomes really differ */
}

/* ===== K2 ===== */
static void k2_hobbies(void)
{
    printf("K2 hobbies\n");
    for (uint32_t i = 0; i < CHGK_HOBBY_CAT; i++) {
        CHECK(chgk_hobby_name(i) && chgk_hobby_name(i)[0] && strcmp(chgk_hobby_name(i), "?"));
        for (uint32_t j = 0; j < i; j++)
            if (strcmp(chgk_hobby_name(i), chgk_hobby_name(j)) == 0) CHECK(!"duplicate hobby");
    }
    CHECK(strcmp(chgk_hobby_name(CHGK_HOBBY_CAT), "?") == 0);

    chgk_hobbies_t h, h2;
    CHECK(chgk_hobbies_for(0, &g_a, &h) == CHGK_OK);
    CHECK(chgk_hobbies_for(0, &g_a, &h2) == CHGK_OK && memcmp(&h, &h2, sizeof(h)) == 0);

    chgk_registry_t agency;
    chgk_registry_init(&agency);
    bool ok = true;
    uint32_t sizes[6] = {0};
    for (uint32_t m = 0; m < 60; m++) {
        chgk_dna_t d;
        chgk_dna_birth(&agency, OWN_A, SPECIES, LINEAGE, &g_a, m * 1000u, &d);
        if (chgk_hobbies_for(&agency, &d, &h) != CHGK_OK) ok = false;
        if (h.n < CHGK_HOBBY_MIN || h.n > CHGK_HOBBY_MAX) ok = false;
        sizes[h.n]++;
        for (uint32_t i = 0; i < h.n; i++) {
            if (h.pick[i] >= CHGK_HOBBY_CAT || h.w[i] != h.dna_w[i]) ok = false;
            if (h.dna_w[i] < 200 || h.dna_w[i] > 1000) ok = false;
            if (i && h.pick[i - 1] >= h.pick[i]) ok = false; /* sorted, distinct */
        }
    }
    CHECK(ok && agency.n_sets == 60);
    for (uint32_t i = 0; i < 60; i++)
        for (uint32_t j = 0; j < i; j++) CHECK(memcmp(agency.set[i], agency.set[j], 5) != 0);
    CHECK(sizes[3] && sizes[4] && sizes[5]);

    /* the same DNA asking twice in one agency: second set differs */
    chgk_registry_t r2;
    chgk_registry_init(&r2);
    chgk_hobbies_for(&r2, &g_a, &h);
    chgk_hobbies_for(&r2, &g_a, &h2);
    CHECK(memcmp(h.pick, h2.pick, CHGK_HOBBY_MAX) != 0);
}

/* ===== K3 ===== */
static void k3_drift(void)
{
    printf("K3 deviation without drift\n");
    chgk_hobbies_t h, start;
    chgk_hobbies_for(0, &g_a, &h);
    start = h;
    uint16_t pull[CHGK_HOBBY_MAX] = {1000, 1, 1, 1, 1};

    /* task not OK: nothing grows */
    CHECK(!chgk_hobby_step(&h, pull, false));
    CHECK(memcmp(h.w, start.w, sizeof(h.w)) == 0 && h.cycles == 1);

    /* one cycle moves each weight by at most 3 */
    chgk_hobby_step(&h, pull, true);
    for (uint32_t i = 0; i < h.n; i++) {
        int d = (int) h.w[i] - (int) start.w[i];
        CHECK(d >= -3 && d <= 3);
    }
    /* keep pulling one way: drift grows until the cap resets it */
    uint32_t cycles = 0;
    bool reset = false;
    while (!reset && cycles < 5000) {
        uint32_t before = chgk_hobby_drift(&h);
        reset = chgk_hobby_step(&h, pull, true);
        if (!reset) CHECK(chgk_hobby_drift(&h) <= CHGK_DRIFT_CAP || before > CHGK_DRIFT_CAP);
        cycles++;
    }
    CHECK(reset && h.resets == 1 && cycles > 10);
    CHECK(memcmp(h.w, h.dna_w, sizeof(h.w)) == 0 && chgk_hobby_drift(&h) == 0);

    /* interest close to the DNA's own: it deviates, constructively, and stays */
    uint16_t near[CHGK_HOBBY_MAX];
    for (uint32_t i = 0; i < CHGK_HOBBY_MAX; i++)
        near[i] = (uint16_t) (i < h.n ? (h.dna_w[i] * 11u) / 10u : 0);
    for (uint32_t c = 0; c < 2000; c++) CHECK(!chgk_hobby_step(&h, near, true));
    CHECK(memcmp(h.w, h.dna_w, sizeof(h.w)) != 0 && h.resets == 1);

    /* budget: only the idle growth tokens, never more */
    uint64_t out[CHGK_HOBBY_MAX];
    CHECK(chgk_hobby_budget(&h, 0, out) == 0);
    uint64_t g = chgk_hobby_budget(&h, 1000003, out);
    CHECK(g <= 1000003 && g + h.n >= 1000003);
    uint64_t sum = 0;
    for (uint32_t i = 0; i < CHGK_HOBBY_MAX; i++) sum += out[i];
    CHECK(sum == g);
    for (uint32_t i = h.n; i < CHGK_HOBBY_MAX; i++) CHECK(out[i] == 0);
    g = chgk_hobby_budget(&h, (uint64_t) 1 << 62, out);
    CHECK(g <= (uint64_t) 1 << 62 && g + h.n >= (uint64_t) 1 << 62);

    /* tie-break: a hobby never beats a better score */
    uint32_t sc[4] = {10, 30, 30, 20};
    uint8_t tag[4] = {h.pick[0], 0xff, h.pick[1], h.pick[0]};
    CHECK(chgk_hobby_tiebreak(&h, sc, tag, 4) == 2);
    uint8_t none[4] = {0xff, 0xff, 0xff, 0xff};
    CHECK(chgk_hobby_tiebreak(&h, sc, none, 4) == 1);
    uint32_t sc2[3] = {5, 9, 7};
    uint8_t tag2[3] = {h.pick[0], 0xff, h.pick[0]};
    CHECK(chgk_hobby_tiebreak(&h, sc2, tag2, 3) == 1);
    CHECK(chgk_hobby_tiebreak(&h, sc2, tag2, 0) == -1);
    /* between two hobby-tagged ties, the heavier hobby wins */
    uint32_t sc3[2] = {4, 4};
    uint8_t tag3[2] = {h.pick[0], h.pick[1]};
    int32_t want = h.w[1] > h.w[0] ? 1 : 0;
    CHECK(chgk_hobby_tiebreak(&h, sc3, tag3, 2) == want);
}

/* ===== K4 / K5 ===== */
/* Test-only signature: sig = SHA3(pk || msg). Real grants use the owners'
 * ML-DSA keys through the same hook. */
static void fake_sign(const uint8_t pk[32], const uint8_t *msg, uint32_t len, uint8_t sig[32])
{
    uint8_t buf[256];
    memcpy(buf, pk, 32);
    memcpy(buf + 32, msg, len);
    sha3_256(buf, 32 + len, sig);
}

static bool fake_verify(const uint8_t *pk, uint32_t pk_len, const uint8_t *msg, uint32_t msg_len,
                        const uint8_t *sig, uint32_t sig_len, void *ctx)
{
    (void) ctx;
    if (pk_len != 32 || sig_len != 32) return false;
    uint8_t want[32];
    fake_sign(pk, msg, msg_len, want);
    return memcmp(want, sig, 32) == 0;
}

static chgk_grant_t grant(uint8_t tag, const chgk_dna_t *v, const chgk_dna_t *h, uint32_t topics,
                          uint8_t depth, uint64_t nb, uint64_t ex)
{
    chgk_grant_t g;
    memset(&g, 0, sizeof(g));
    memset(g.gid, tag, 16);
    memcpy(g.visitor, v->id, 32);
    memcpy(g.host, h->id, 32);
    g.topics = topics;
    g.depth = depth;
    g.not_before = nb;
    g.expires = ex;
    return g;
}

static chgk_claim_t claim(uint8_t kind, uint8_t topic, uint8_t verdict, uint8_t salt)
{
    chgk_claim_t c;
    memset(&c, 0, sizeof(c));
    c.kind = kind;
    c.topic = topic;
    c.verdict = verdict;
    memset(c.claim, salt, 32);
    memset(c.evidence, (uint8_t) (salt ^ 0x5a), 32);
    return c;
}

static void k4_k5_visits(void)
{
    printf("K4 grants and visits / K5 experience\n");
    chgk_grant_t g = grant(1, &g_a, &g_b, (1u << 3) | (1u << 7), CHGK_DEPTH_CLAIMS, 100, 200);
    uint8_t body[CHGK_GRANT_ENC + 1], sv[32], sh[32];
    uint32_t n = chgk_grant_encode(&g, body, sizeof(body));
    CHECK(n == CHGK_GRANT_ENC);
    fake_sign(OWN_A, body, n, sv);
    fake_sign(OWN_B, body, n, sh);
    chgk_grant_t got;
    CHECK(chgk_grant_verify(body, n, OWN_A, OWN_B, 32, sv, sh, 32, fake_verify, 0, &got) ==
          CHGK_OK);
    CHECK(memcmp(&got, &g, sizeof(g)) == 0);
    /* one owner alone cannot grant a visit */
    CHECK(chgk_grant_verify(body, n, OWN_A, OWN_B, 32, sv, sv, 32, fake_verify, 0, &got) ==
          CHGK_ERR_SIG);
    CHECK(chgk_grant_verify(body, n, OWN_A, OWN_B, 32, sh, sh, 32, fake_verify, 0, &got) ==
          CHGK_ERR_SIG);
    body[4 + 16 + 64] ^= 0x80; /* widen the topics after signing */
    CHECK(chgk_grant_verify(body, n, OWN_A, OWN_B, 32, sv, sh, 32, fake_verify, 0, &got) ==
          CHGK_ERR_SIG);
    body[4 + 16 + 64] ^= 0x80;
    body[4 + 16 + 64 + 4] = 9; /* depth that does not exist */
    CHECK(chgk_grant_decode(body, n, &got) == CHGK_ERR_PARSE);
    chgk_grant_t bad = g;
    bad.topics = 0;
    CHECK(chgk_grant_encode(&bad, body, sizeof(body)) == 0);
    bad = grant(2, &g_a, &g_a, 1, 1, 0, 10);
    CHECK(chgk_grant_encode(&bad, body, sizeof(body)) == 0); /* a chiglet visiting itself */

    chgk_grantbook_t book_b;
    chgk_grantbook_init(&book_b);
    chgk_xp_t xb;
    chgk_xp_init(&xb, &g_b);
    chgk_visit_t v;
    CHECK(chgk_visit_open(&v, &xb, &book_b, &g, &g_b, &g_a, 99) == CHGK_ERR_EXPIRED);
    CHECK(chgk_visit_open(&v, &xb, &book_b, &g, &g_b, &g_a, 200) == CHGK_ERR_EXPIRED);
    chgk_dna_t stranger;
    chgk_dna_birth(0, OWN_A, SPECIES, LINEAGE, 0, 4242, &stranger);
    CHECK(chgk_visit_open(&v, &xb, &book_b, &g, &g_b, &stranger, 150) == CHGK_ERR_PARTY);
    CHECK(chgk_visit_open(&v, &xb, &book_b, &g, &g_b, &g_a, 150) == CHGK_OK && v.open);
    CHECK(v.meeting == 1 && v.partner_gap == chgk_dna_distance(&g_a, &g_b));

    /* scope */
    uint32_t gain = 99;
    chgk_claim_t c = claim(CHGK_K_CLAIM, 4, 0, 1);
    CHECK(chgk_visit_receive(&v, &xb, &c, true, 150, &gain) == CHGK_ERR_SCOPE && gain == 0);
    c = claim(CHGK_K_VERDICT, 3, 1, 1);
    CHECK(chgk_visit_receive(&v, &xb, &c, true, 150, &gain) == CHGK_ERR_SCOPE);
    c = claim(9, 3, 0, 1);
    CHECK(chgk_visit_receive(&v, &xb, &c, true, 150, &gain) == CHGK_ERR_SCOPE);
    c = claim(CHGK_K_CLAIM, 3, 2, 1);
    CHECK(chgk_visit_receive(&v, &xb, &c, true, 150, &gain) == CHGK_ERR_ARG);
    CHECK(xb.log_len == 0);

    /* a claim the checker rejects: logged, nothing learned */
    c = claim(CHGK_K_CLAIM, 3, 0, 2);
    CHECK(chgk_visit_receive(&v, &xb, &c, false, 150, &gain) == CHGK_OK && gain == 0);
    CHECK(xb.refused == 1 && xb.xp[3] == 0 && xb.log_len == 1);
    /* a checked claim: experience and Social capital */
    chgk_claim_t c2 = claim(CHGK_K_CLAIM, 7, 0, 3);
    CHECK(chgk_visit_receive(&v, &xb, &c2, true, 160, &gain) == CHGK_OK && gain > 100);
    CHECK(xb.xp[7] == gain && xb.social == 1 && xb.taken == 1);
    uint32_t first_gain = gain;

    /* the owner can recompute the log chain */
    uint8_t head[32] = {0};
    chgk_log_step(head, g_a.id, &c, false);
    chgk_log_step(head, g_a.id, &c2, true);
    CHECK(memcmp(head, xb.log_head, 32) == 0);
    uint8_t head2[32] = {0};
    chgk_log_step(head2, g_a.id, &c, true); /* claiming it was taken changes the chain */
    chgk_log_step(head2, g_a.id, &c2, true);
    CHECK(memcmp(head2, xb.log_head, 32) != 0);

    /* expiry closes the visit; closing is always allowed */
    CHECK(chgk_visit_receive(&v, &xb, &c2, true, 200, &gain) == CHGK_ERR_EXPIRED && !v.open);
    CHECK(chgk_visit_receive(&v, &xb, &c2, true, 150, &gain) == CHGK_ERR_CLOSED);

    /* replay and revoke */
    CHECK(chgk_visit_open(&v, &xb, &book_b, &g, &g_b, &g_a, 150) == CHGK_ERR_REPLAY);
    chgk_grant_t g2 = grant(3, &g_a, &g_b, 1u << 7, CHGK_DEPTH_VERDICTS, 100, 400);
    CHECK(chgk_revoke(&book_b, g2.gid) == CHGK_OK);
    CHECK(chgk_visit_open(&v, &xb, &book_b, &g2, &g_b, &g_a, 300) == CHGK_ERR_REVOKED);

    /* meeting the same friend again teaches less */
    chgk_grant_t g3 = grant(4, &g_a, &g_b, 1u << 7, CHGK_DEPTH_VERDICTS, 100, 400);
    CHECK(chgk_visit_open(&v, &xb, &book_b, &g3, &g_b, &g_a, 300) == CHGK_OK && v.meeting == 2);
    chgk_claim_t c3 = claim(CHGK_K_VERDICT, 7, 3, 4);
    CHECK(chgk_visit_receive(&v, &xb, &c3, true, 300, &gain) == CHGK_OK);
    CHECK(gain == first_gain / 2 || gain == (first_gain + 1) / 2 || gain + 1 == first_gain / 2);
    chgk_visit_close(&v);
    CHECK(chgk_visit_receive(&v, &xb, &c3, true, 300, &gain) == CHGK_ERR_CLOSED);

    /* the visitor's side uses the same grant from its own book */
    chgk_grantbook_t book_a;
    chgk_grantbook_init(&book_a);
    chgk_xp_t xa;
    chgk_xp_init(&xa, &g_a);
    CHECK(chgk_visit_open(&v, &xa, &book_a, &g, &g_a, &g_b, 150) == CHGK_OK);
    CHECK(chgk_visit_open(&v, &xb, &book_a, &g, &g_a, &g_b, 150) == CHGK_ERR_PARTY);

    /* a more different friend teaches more (ISF surplus of the genomes) */
    uint32_t prev_gap = 0, prev_gain = 0;
    bool mono = true;
    chgk_dna_t friends[40];
    uint32_t order[40];
    for (uint32_t i = 0; i < 40; i++) {
        chgk_dna_birth(0, OWN_B, SPECIES, LINEAGE, 0, 50000 + i * 7919u, &friends[i]);
        order[i] = i;
    }
    for (uint32_t i = 1; i < 40; i++) /* sort friends by genome distance */
        for (uint32_t j = i; j > 0 && chgk_dna_distance(&g_a, &friends[order[j - 1]]) >
                                          chgk_dna_distance(&g_a, &friends[order[j]]);
             j--) {
            uint32_t t = order[j];
            order[j] = order[j - 1], order[j - 1] = t;
        }
    for (uint32_t k = 0; k < 40; k++) {
        chgk_dna_t *f = &friends[order[k]];
        chgk_grantbook_t bk;
        chgk_grantbook_init(&bk);
        chgk_xp_t x;
        chgk_xp_init(&x, &g_a);
        chgk_grant_t gg = grant((uint8_t) (10 + k), f, &g_a, 1, CHGK_DEPTH_CLAIMS, 0, 10);
        if (chgk_visit_open(&v, &x, &bk, &gg, &g_a, f, 1) != CHGK_OK) mono = false;
        chgk_claim_t cc = claim(CHGK_K_CLAIM, 0, 0, 9);
        chgk_visit_receive(&v, &x, &cc, true, 1, &gain);
        if (v.partner_gap >= prev_gap && gain < prev_gain) mono = false;
        prev_gap = v.partner_gap, prev_gain = gain;
    }
    CHECK(mono);
    CHECK(prev_gain > 100 && prev_gain <= 100 + 2080); /* f(1) = ln 8 = 2.079 nats */
}

int main(void)
{
    k1_dna();
    k2_hobbies();
    k3_drift();
    k4_k5_visits();
    printf("chg_kin: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
