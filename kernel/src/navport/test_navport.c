/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_navport.c — naval ports (navport.h N1-N9). The signature hook is a
 * stand-in (SHA-256 keyed by the signer's id); production passes ML-DSA. */
#include <stdio.h>
#include <string.h>
#include "navport.h"
#include "pay_assure.h"
#include "../robin_debanks/sha256.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                   \
        }                                                                                          \
    } while (0)

static void sign(uint32_t who, const uint8_t *m, uint32_t n, uint8_t sig[32])
{
    uint8_t b[8 + 128];
    memset(b, 0, sizeof b);
    memcpy(b, &who, 4);
    b[4] = 0x4E;
    memcpy(b + 8, m, n);
    sha256(b, 8 + n, sig);
}
static bool verify(void *ctx, uint32_t who, const uint8_t *m, uint32_t n, const uint8_t *sig,
                   uint32_t sl)
{
    (void) ctx;
    uint8_t want[32];
    if (sl != 32 || n > 128) return false;
    sign(who, m, n, want);
    return memcmp(want, sig, 32) == 0;
}

#define KA 101u
#define KB 202u
#define KC 303u
static const uint32_t KEYS[8] = {KA, KB, KC, 404, 505, 606, 707, 808};

static navp_status_t vote(navp_t *p, uint32_t m, const uint8_t hash[32])
{
    uint8_t msg[NAVP_CLOSE_MSG], sig[32];
    navp_close_msg(p->port_id, p->cycle, hash, msg);
    sign(p->m[m].key, msg, sizeof msg, sig);
    return navp_close_vote(p, m, hash, sig, 32);
}

static void report_true(navp_t *p, uint32_t m)
{
    uint64_t no[8] = {0}, vo[8] = {0};
    for (uint32_t j = 0; j < p->n; j++) {
        no[j] = p->hold[m][j];
        vo[j] = p->hold[j][m];
    }
    navp_books_report(p, m, no, vo);
}

/* full honest close: every live member reports and votes */
static navp_status_t close_all(navp_t *p)
{
    for (uint32_t i = 0; i < p->n; i++)
        if (p->m[i].state == NAVP_M_ACTIVE || p->m[i].state == NAVP_M_EXITING) report_true(p, i);
    navp_status_t s = navp_close_prepare(p);
    if (s != NAVP_OK) return s;
    for (uint32_t i = 0; i < p->n; i++)
        if (p->m[i].state == NAVP_M_ACTIVE || p->m[i].state == NAVP_M_EXITING) vote(p, i, p->hash);
    return navp_close_commit(p);
}

/* three members: A pegged 150/1 (1.50 per V-Bill, 2 decimals),
 * B 1/1, C 7/3 */
static void port3(navp_t *p, navp_config_t *cfg)
{
    navp_init(p, 77, cfg, verify, NULL);
    navp_member_add(p, KA, "TCBDC", 150, 1, 100000, NULL);
    navp_member_add(p, KB, "TFIAT", 1, 1, 100000, NULL);
    navp_member_add(p, KC, "TGOLD", 7, 3, 100000, NULL);
    for (uint32_t i = 0; i < 3; i++) navp_evidence(p, i, 3, 0);
}

static void test_args(void)
{
    navp_t p;
    navp_config_t c;
    navp_config_default(&c);
    CHECK(!c.yield_enabled, "yield off by default");
    CHECK(c.mode == NAVP_SUSPEND, "suspend by default");
    CHECK(navp_init(&p, 1, NULL, NULL, NULL) == NAVP_E_ARG, "verify required");
    c.stale_cap_bps = 10001;
    CHECK(navp_init(&p, 1, &c, verify, NULL) == NAVP_E_ARG, "stale cap > 100%");
    navp_config_default(&c);
    c.mode = 7;
    CHECK(navp_init(&p, 1, &c, verify, NULL) == NAVP_E_ARG, "bad mode");
    CHECK(navp_init(&p, 1, NULL, verify, NULL) == NAVP_OK, "init");
    CHECK(navp_member_add(&p, KA, "", 1, 1, 0, NULL) == NAVP_E_ARG, "empty code");
    CHECK(navp_member_add(&p, KA, "THIRTEENCHARS", 1, 1, 0, NULL) == NAVP_E_ARG, "long code");
    CHECK(navp_member_add(&p, KA, "T", 0, 1, 0, NULL) == NAVP_E_ARG, "zero num");
    CHECK(navp_member_add(&p, KA, "T", 1, 0, 0, NULL) == NAVP_E_ARG, "zero den");
    CHECK(navp_member_add(&p, KA, "T", 1, 1, NAVP_AMOUNT_MAX, NULL) == NAVP_E_ARG, "stake max");
    uint32_t idx = 9;
    CHECK(navp_member_add(&p, KA, "T", 1, 1, 0, &idx) == NAVP_OK && idx == 0, "add");
    CHECK(navp_member_add(&p, KA, "U", 1, 1, 0, NULL) == NAVP_E_DUP, "dup key");
    for (uint32_t i = 1; i < 8; i++)
        CHECK(navp_member_add(&p, KEYS[i], "T", 1, 1, 0, NULL) == NAVP_OK, "fill");
    CHECK(navp_member_add(&p, 999, "T", 1, 1, 0, NULL) == NAVP_E_FULL, "full");
    navp_config_default(&c);
    c.min_stake = 50;
    navp_init(&p, 1, &c, verify, NULL);
    CHECK(navp_member_add(&p, KA, "T", 1, 1, 49, NULL) == NAVP_E_ARG, "below min stake");
    CHECK(strcmp(navp_status_name(NAVP_E_LIMIT), "LIMIT") == 0, "status name");
    CHECK(navp_quorum(&p) == 0, "empty quorum");
}

static void test_quote(void)
{
    navp_t p;
    port3(&p, NULL);
    navp_quote_t q;
    /* A -> B: 15000 A-minor (150.00), fee floor(15000*8889/1e7) = 13 */
    CHECK(navp_quote(&p, 0, 1, 15000, &q) == NAVP_OK, "quote");
    CHECK(q.fee == pay_assure_fee(15000) && q.fee == 13, "fee 0.08889%");
    CHECK(q.v == (15000 - 13) / 150, "v = floor(xn*den/num)");
    CHECK(q.y == q.v, "B at 1/1");
    CHECK(q.x_used == q.v * 150 && q.x_used <= 15000 - 13, "x_used <= xn");
    CHECK(q.fee_v == 0, "fee value floors");
    /* B -> C: 300 B, fee 0, v=300, y = floor(300*7/3)=700 */
    CHECK(navp_quote(&p, 1, 2, 300, &q) == NAVP_OK && q.fee == 0 && q.v == 300 && q.y == 700,
          "B->C");
    /* C -> B: 10 C: v = floor(10*3/7)=4, x_used = ceil(4*7/3)=10 */
    CHECK(navp_quote(&p, 2, 1, 10, &q) == NAVP_OK && q.v == 4 && q.x_used == 10 && q.y == 4,
          "C->B ceil");
    CHECK(navp_quote(&p, 2, 1, 7, &q) == NAVP_OK && q.v == 3 && q.x_used == 7, "C->B exact");
    CHECK(navp_quote(&p, 0, 1, 149, &q) == NAVP_E_DUST, "dust");
    CHECK(navp_quote(&p, 0, 0, 1000, &q) == NAVP_E_ARG, "self");
    CHECK(navp_quote(&p, 0, 5, 1000, &q) == NAVP_E_UNKNOWN, "unknown");
    CHECK(navp_quote(&p, 0, 1, 0, &q) == NAVP_E_ARG, "zero");
    CHECK(navp_quote(&p, 0, 1, NAVP_AMOUNT_MAX, &q) == NAVP_E_ARG, "too big");
    /* exhaustive small: x_used never exceeds xn, y matches the definition */
    int bad = 0;
    for (uint64_t x = 1; x < 3000; x++)
        for (uint32_t f = 0; f < 3; f++)
            for (uint32_t t = 0; t < 3; t++) {
                if (f == t) continue;
                navp_status_t s = navp_quote(&p, f, t, x, &q);
                if (s == NAVP_E_DUST) continue;
                uint64_t xn = x - pay_assure_fee(x);
                if (s != NAVP_OK || q.x_used > xn || q.v != xn * p.m[f].den / p.m[f].num ||
                    q.y != q.v * p.m[t].num / p.m[t].den ||
                    q.x_used * p.m[f].den < q.v * p.m[f].num ||
                    (q.x_used - 1) * p.m[f].den >= q.v * p.m[f].num)
                    bad++;
            }
    CHECK(bad == 0, "quote exhaustive");
}

static void test_convert_limit(void)
{
    navp_t p;
    port3(&p, NULL);
    navp_quote_t q;
    /* B sells 100088 B to A: fee 88, net debit of B = 100000 = share: allowed */
    CHECK(navp_convert(&p, 1, 0, 100088, &q) == NAVP_OK && q.v == 100000, "at limit");
    CHECK(p.hold[0][1] == 100000 && p.val[0][1] == 100000, "A holds B's vouchers");
    CHECK(navp_convert(&p, 1, 2, 1, &q) == NAVP_E_LIMIT, "one past limit");
    CHECK(navp_check(&p), "check");
    /* inflow to B (C sells C for B) makes room */
    CHECK(navp_convert(&p, 2, 1, 70, &q) == NAVP_OK && q.v == 30, "C->B");
    CHECK(navp_convert(&p, 1, 2, 30, &q) == NAVP_OK, "room again");
    CHECK(navp_convert(&p, 1, 2, 1, &q) == NAVP_E_LIMIT, "full again");
    CHECK(p.m[1].fee_due == pay_assure_fee(100088) && p.m[1].fee_due == 88, "fee on outflows");
    CHECK(navp_check(&p), "check");
    /* deposit extends */
    CHECK(navp_deposit(&p, 1, 5) == NAVP_OK, "deposit");
    CHECK(navp_convert(&p, 1, 2, 5, &q) == NAVP_OK, "after deposit");
    /* withdraw cannot cut below net debit */
    CHECK(navp_withdraw(&p, 1, 1) == NAVP_E_LIMIT, "withdraw below debit");
    CHECK(navp_withdraw(&p, 0, 1000) == NAVP_OK, "creditor withdraw");
    CHECK(navp_withdraw(&p, 9, 1) == NAVP_E_UNKNOWN, "unknown withdraw");
    CHECK(navp_deposit(&p, 0, 0) == NAVP_E_ARG, "zero deposit");
    CHECK(navp_check(&p), "check after withdraw");
}

static void test_health(void)
{
    navp_t p;
    navp_config_t c;
    navp_config_default(&c);
    port3(&p, &c);
    navp_quote_t q;
    CHECK(navp_health(&p, 0) == NAVP_TRUE, "true");
    CHECK(navp_evidence(&p, 0, 0, 1) == NAVP_BOTH, "both");
    CHECK(navp_convert(&p, 0, 1, 15000, &q) == NAVP_E_HELD, "both held");
    CHECK(navp_convert(&p, 1, 0, 100, &q) == NAVP_OK, "inflow on both ok");
    /* FALSE */
    port3(&p, &c);
    p.m[0].on_votes = 0;
    navp_evidence(&p, 0, 0, 2);
    CHECK(navp_health(&p, 0) == NAVP_FALSE, "false");
    CHECK(navp_convert(&p, 0, 1, 15000, &q) == NAVP_E_DEPEG, "suspend on false");
    CHECK(navp_convert(&p, 1, 0, 100, &q) == NAVP_OK, "inflow on false ok");
    c.mode = NAVP_DEFEND;
    port3(&p, &c);
    p.m[0].on_votes = 0;
    navp_evidence(&p, 0, 0, 2);
    CHECK(navp_convert(&p, 0, 1, 15000, &q) == NAVP_OK, "defend on false");
    /* NEITHER: 10% of 100000 = 10000 V */
    navp_config_default(&c);
    navp_init(&p, 1, &c, verify, NULL);
    navp_member_add(&p, KA, "A", 1, 1, 100000, NULL);
    navp_member_add(&p, KB, "B", 1, 1, 100000, NULL);
    CHECK(navp_health(&p, 0) == NAVP_NEITHER, "neither");
    CHECK(navp_convert(&p, 0, 1, 10008, &q) == NAVP_OK && q.v == 10000, "stale within cap");
    CHECK(navp_convert(&p, 0, 1, 1, &q) == NAVP_E_STALE, "stale cap");
    navp_evidence(&p, 0, 1, 0);
    CHECK(navp_convert(&p, 0, 1, 1, &q) == NAVP_OK, "fresh evidence lifts");
    CHECK(navp_evidence(&p, 7, 1, 0) == NAVP_NEITHER, "unknown evidence");
    /* evidence resets at close */
    CHECK(close_all(&p) == NAVP_OK, "close");
    CHECK(navp_health(&p, 0) == NAVP_NEITHER && p.m[0].stale_used == 0, "reset at close");
}

static void test_close(void)
{
    navp_t p;
    port3(&p, NULL);
    navp_quote_t q;
    /* oscillation: A->B 30000 A (v=199), B->A 150 B, B->C 100, C->A 70 */
    navp_convert(&p, 0, 1, 30000, &q);
    uint64_t vab = q.v;
    navp_convert(&p, 1, 0, 150, &q);
    navp_convert(&p, 1, 2, 100, &q);
    navp_convert(&p, 2, 0, 70, &q);
    uint64_t s0 = p.m[0].share, s1 = p.m[1].share, s2 = p.m[2].share;
    int64_t n0 = (int64_t) (p.val[0][1] + p.val[0][2]) - (int64_t) (p.val[1][0] + p.val[2][0]);
    for (uint32_t i = 0; i < 3; i++) report_true(&p, i);
    CHECK(navp_convert(&p, 0, 1, 15000, &q) == NAVP_OK, "open still converts");
    n0 = (int64_t) (p.val[0][1] + p.val[0][2]) - (int64_t) (p.val[1][0] + p.val[2][0]);
    /* the report was before the last conversion: A's nostro still right,
       B's nostro and A's vostro about (1,0) are both stale -> FALSE */
    CHECK(navp_close_prepare(&p) == NAVP_OK, "prepare");
    CHECK(p.tag[1][0] == NAVP_FALSE, "both witnesses stale = FALSE");
    CHECK(p.tag[2][0] == NAVP_TRUE, "untouched pair TRUE");
    CHECK(p.tag[0][1] == NAVP_TRUE, "matching = TRUE");
    CHECK(navp_convert(&p, 0, 1, 15000, &q) == NAVP_E_STATE, "closing blocks");
    CHECK(navp_close_abort(&p) == NAVP_OK, "abort");
    CHECK(navp_close_abort(&p) == NAVP_E_STATE, "abort twice");
    for (uint32_t i = 0; i < 3; i++) report_true(&p, i);
    CHECK(navp_close_prepare(&p) == NAVP_OK, "prepare again");
    CHECK(p.net[0] + p.net[1] + p.net[2] == 0, "nets sum to zero");
    CHECK(p.net[0] == n0, "A's net");
    CHECK(p.gross >= p.settled, "gross >= settled");
    uint8_t other[32];
    memcpy(other, p.hash, 32);
    other[0] ^= 1;
    CHECK(navp_close_commit(&p) == NAVP_E_QUORUM, "no votes");
    CHECK(vote(&p, 0, p.hash) == NAVP_OK, "vote A");
    CHECK(vote(&p, 0, p.hash) == NAVP_E_DUP, "dup vote");
    uint8_t bad[32] = {0};
    CHECK(navp_close_vote(&p, 1, p.hash, bad, 32) == NAVP_E_SIG, "bad sig");
    CHECK(vote(&p, 1, p.hash) == NAVP_OK, "vote B");
    CHECK(navp_quorum(&p) == 3, "quorum of 3 is 3");
    CHECK(navp_close_commit(&p) == NAVP_E_QUORUM, "2 of 3");
    CHECK(vote(&p, 2, other) == NAVP_OK, "conflicting vote counted");
    CHECK(navp_close_commit(&p) == NAVP_E_CONFLICT, "conflict = BOTH, held");
    CHECK(p.m[0].share == s0, "nothing moved on conflict");
    navp_close_abort(&p);
    CHECK(close_all(&p) == NAVP_OK, "honest close");
    CHECK((int64_t) p.m[0].share == (int64_t) s0 + n0, "A settled");
    CHECK(p.m[0].share + p.m[1].share + p.m[2].share == s0 + s1 + s2, "V-Bills conserved");
    for (uint32_t i = 0; i < 3; i++)
        for (uint32_t j = 0; j < 3; j++)
            CHECK(p.hold[i][j] == 0 && p.val[i][j] == 0, "all retired");
    CHECK(p.cycle == 1 && p.closes == 1, "cycle advanced");
    CHECK(p.total_gross >= vab, "gross recorded");
    CHECK(navp_check(&p), "check");
    CHECK(navp_close_vote(&p, 0, p.hash, bad, 32) == NAVP_E_STATE, "vote when open");
    CHECK(navp_close_commit(&p) == NAVP_E_STATE, "commit when open");
}

static void test_carry_and_short(void)
{
    navp_t p;
    navp_config_t c;
    navp_config_default(&c);
    c.quorum = 2;
    port3(&p, &c);
    navp_quote_t q;
    /* C silent: pairs with C are NEITHER and carry */
    navp_convert(&p, 0, 1, 15000, &q);
    navp_convert(&p, 1, 2, 500, &q);
    report_true(&p, 0);
    report_true(&p, 1);
    CHECK(navp_close_prepare(&p) == NAVP_OK, "prepare");
    CHECK(p.tag[2][1] == NAVP_TRUE, "B's vostro alone is TRUE");
    /* only B reported about (2,1): one witness for, none against -> TRUE */
    vote(&p, 0, p.hash);
    vote(&p, 1, p.hash);
    CHECK(navp_close_commit(&p) == NAVP_OK, "quorum 2 without C");
    CHECK(navp_check(&p), "check");
    /* now nobody reports about C's pair: NEITHER carries */
    navp_convert(&p, 1, 2, 300, &q);
    report_true(&p, 0);
    CHECK(navp_close_prepare(&p) == NAVP_OK, "prepare 2");
    CHECK(p.tag[2][1] == NAVP_NEITHER, "no witness = NEITHER");
    vote(&p, 0, p.hash);
    vote(&p, 1, p.hash);
    CHECK(navp_close_commit(&p) == NAVP_OK, "commit");
    CHECK(p.hold[2][1] > 0, "NEITHER pair carried");
    CHECK(navp_check(&p), "check carried");
    /* SHORT: B owes A, A owes C, C owes B; exclude the pair that offsets B */
    navp_init(&p, 5, &c, verify, NULL);
    navp_member_add(&p, KA, "A", 1, 1, 100, NULL);
    navp_member_add(&p, KB, "B", 1, 1, 100, NULL);
    navp_member_add(&p, KC, "C", 1, 1, 0, NULL);
    for (uint32_t i = 0; i < 3; i++) navp_evidence(&p, i, 1, 0);
    CHECK(navp_convert(&p, 2, 1, 100, &q) == NAVP_E_LIMIT, "C has no share");
    CHECK(navp_convert(&p, 0, 1, 100, &q) == NAVP_OK, "B holds A's 100");
    CHECK(navp_convert(&p, 1, 0, 200, &q) == NAVP_OK, "A holds B's 200 (B net 100)");
    report_true(&p, 0);
    report_true(&p, 1);
    p.nostro_rep[1][0] = 7; /* B disputes its A holding: (1,0) BOTH */
    CHECK(navp_close_prepare(&p) == NAVP_E_SHORT, "excluded offset leaves B short");
    CHECK(p.pstate == NAVP_OPEN, "still open");
    report_true(&p, 1);
    CHECK(close_all(&p) == NAVP_OK, "resolved");
    CHECK(p.m[0].share == 200 && p.m[1].share == 0, "settled 100");
    CHECK(navp_check(&p), "check");
}

static void test_proposals(void)
{
    navp_t p;
    port3(&p, NULL);
    navp_quote_t q;
    uint32_t id;
    uint8_t msg[NAVP_PROP_MSG], sig[32];
    navp_convert(&p, 0, 1, 15000, &q);
    uint64_t v_before = p.val[1][0];
    CHECK(navp_propose_peg(&p, 0, 300, 1, &id) == NAVP_OK, "propose devaluation");
    CHECK(navp_propose_peg(&p, 0, 0, 1, &id) == NAVP_E_ARG, "bad peg");
    navp_propose_peg(&p, 0, 300, 1, &id);
    navp_prop_msg(&p, id, msg);
    for (uint32_t m = 1; m < 3; m++) {
        sign(p.m[m].key, msg, sizeof msg, sig);
        CHECK(navp_approve(&p, id, m, sig, 32) == NAVP_OK, "approve other");
    }
    navp_proposal_t *r = NULL;
    for (uint32_t k = 0; k < NAVP_MAX_PROPOSALS; k++)
        if (p.prop[k].id == id) r = &p.prop[k];
    CHECK(r && !r->ratified, "others alone cannot reset A's peg");
    sign(p.m[0].key, msg, sizeof msg, sig);
    CHECK(navp_approve(&p, id, 0, sig, 32) == NAVP_OK && r->ratified, "with A: ratified");
    CHECK(navp_approve(&p, id, 0, sig, 32) == NAVP_E_DUP, "dup approve");
    CHECK(navp_approve(&p, 999, 0, sig, 32) == NAVP_E_UNKNOWN, "unknown prop");
    CHECK(p.m[0].num == 150, "not before close");
    CHECK(close_all(&p) == NAVP_OK, "close");
    CHECK(p.m[0].num == 300, "after close");
    CHECK(p.m[1].share == 100000 + v_before, "settled at the old peg");
    navp_evidence(&p, 0, 1, 0);
    navp_quote(&p, 0, 1, 30000, &q);
    CHECK(q.v == (30000 - pay_assure_fee(30000)) / 300, "new peg quotes");
    /* admit D */
    CHECK(navp_propose_admit(&p, KA, "D", 1, 1, 10, &id) == NAVP_E_DUP, "key taken");
    CHECK(navp_propose_admit(&p, 404, "TCOMP", 5, 1, 5000, &id) == NAVP_OK, "admit");
    navp_prop_msg(&p, id, msg);
    for (uint32_t m = 0; m < 3; m++) {
        sign(p.m[m].key, msg, sizeof msg, sig);
        navp_approve(&p, id, m, sig, 32);
    }
    CHECK(close_all(&p) == NAVP_OK, "close admits");
    CHECK(p.n == 4 && p.m[3].share == 5000 && p.m[3].state == NAVP_M_ACTIVE, "D admitted");
    CHECK(navp_quorum(&p) == 3, "quorum of 4 is 3");
    CHECK(navp_check(&p), "check after admit");
    CHECK(navp_member_add(&p, 505, "E", 1, 1, 0, NULL) == NAVP_E_STATE, "no genesis after start");
    /* exit B */
    uint8_t em[NAVP_EXIT_MSG];
    navp_exit_msg(p.port_id, 1, em);
    sign(KA, em, sizeof em, sig);
    CHECK(navp_exit(&p, 1, sig, 32) == NAVP_E_SIG, "exit needs own sig");
    sign(KB, em, sizeof em, sig);
    navp_evidence(&p, 2, 1, 0);
    navp_convert(&p, 2, 1, 70, &q);
    CHECK(navp_exit(&p, 1, sig, 32) == NAVP_OK, "exit");
    CHECK(navp_convert(&p, 2, 1, 70, &q) == NAVP_E_STATE, "no conversions with exiting");
    uint64_t b_share = p.m[1].share;
    CHECK(close_all(&p) == NAVP_OK, "close exits");
    CHECK(p.m[1].state == NAVP_M_EXITED && p.m[1].share == 0 && p.m[1].returned == b_share + 30,
          "share returned after settling");
    CHECK(navp_check(&p), "check after exit");
    CHECK(navp_exit(&p, 1, sig, 32) == NAVP_E_STATE, "exit twice");
}

static void test_yield(void)
{
    navp_t p;
    navp_config_t c;
    navp_config_default(&c);
    port3(&p, &c);
    uint64_t sh[8];
    CHECK(navp_yield(&p, 1000, sh) == NAVP_E_DISABLED, "off by default");
    c.yield_enabled = true;
    c.fee_weight = 0;
    port3(&p, &c);
    p.m[0].share = 1;
    p.m[1].share = 1;
    p.m[2].share = 1;
    p.genesis = 3;
    CHECK(navp_yield(&p, 100, sh) == NAVP_OK, "yield");
    CHECK(sh[0] == 34 && sh[1] == 33 && sh[2] == 33, "largest remainder, lower index");
    p.m[0].share = 0;
    p.m[1].share = 0;
    p.m[2].share = 0;
    CHECK(navp_yield(&p, 100, sh) == NAVP_E_ARG, "no units");
    /* fee weight: fees paid count, compression does not */
    c.fee_weight = 10;
    port3(&p, &c);
    navp_quote_t q;
    for (int i = 0; i < 20; i++) {
        navp_convert(&p, 1, 2, 30000, &q);
        navp_convert(&p, 2, 1, 70000, &q);
    }
    CHECK(p.m[1].fee_v > 0, "fees counted");
    CHECK(navp_yield(&p, 1000000, sh) == NAVP_OK, "yield with fees");
    CHECK(sh[0] + sh[1] + sh[2] == 1000000, "sum exact");
    CHECK(sh[1] > sh[0], "fee payer weighted up");
    CHECK(p.m[1].fee_v == 0, "fee counters reset");
    int bad = 0;
    for (uint64_t pool = 1; pool < 400; pool++) {
        p.m[0].share = pool * 7 % 13 + 1;
        p.m[1].share = pool * 5 % 11;
        p.m[2].share = pool % 3;
        if (navp_yield(&p, pool, sh) != NAVP_OK || sh[0] + sh[1] + sh[2] != pool) bad++;
    }
    CHECK(bad == 0, "yield sums exactly");
}

/* random sessions: the invariants hold after every operation */
static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return rs;
}

static void test_fuzz(void)
{
    navp_config_t c;
    navp_config_default(&c);
    int bad = 0, ops = 0, ok = 0;
    for (int run = 0; run < 200; run++) {
        navp_t p;
        c.mode = (navp_mode_t) (run & 1);
        navp_init(&p, (uint32_t) run, &c, verify, NULL);
        uint32_t k = 2 + (uint32_t) (rnd() % 7);
        for (uint32_t i = 0; i < k; i++)
            navp_member_add(&p, KEYS[i], "T", 1 + rnd() % 500, 1 + rnd() % 50, rnd() % 200000,
                            NULL);
        for (int step = 0; step < 300; step++) {
            uint32_t a = (uint32_t) (rnd() % k), b = (uint32_t) (rnd() % k);
            switch (rnd() % 10) {
            case 0:
                navp_evidence(&p, a, (uint32_t) (rnd() % 3), (uint32_t) (rnd() % 2));
                break;
            case 1:
                navp_deposit(&p, a, rnd() % 5000 + 1);
                break;
            case 2:
                navp_withdraw(&p, a, rnd() % 5000 + 1);
                break;
            case 3:
                report_true(&p, a);
                if (rnd() % 4 == 0) p.nostro_rep[a][b] ^= 1;
                break;
            case 4:
                if (navp_close_prepare(&p) == NAVP_OK) {
                    for (uint32_t i = 0; i < k; i++)
                        if (rnd() % 5) vote(&p, i, p.hash);
                    if (navp_close_commit(&p) != NAVP_OK) navp_close_abort(&p);
                }
                break;
            default: {
                navp_quote_t q;
                if (navp_convert(&p, a, b, rnd() % 200000 + 1, &q) == NAVP_OK) ok++;
            }
            }
            ops++;
            if (!navp_check(&p)) bad++;
        }
    }
    CHECK(bad == 0, "invariants hold in random sessions");
    CHECK(ok > 1000, "random sessions converted");
    printf("fuzz: %d ops, %d conversions\n", ops, ok);
}

int main(void)
{
    test_args();
    test_quote();
    test_convert_limit();
    test_health();
    test_close();
    test_carry_and_short();
    test_proposals();
    test_yield();
    test_fuzz();
    printf("%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
