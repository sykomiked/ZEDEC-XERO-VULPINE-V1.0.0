/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_pdx_ledger.c — the contradiction-tolerant ledger (pdx_ledger.h,
 * P1.1-P1.4) and the four-valued agents (pdx_agent.h, P2.2).
 *
 * Ledger: double spends held in escrow, the dispute funnel (rule,
 * witnesses, arbiter, owner), frozen slots, insufficient funds, late double
 * spends, equivocation and quarantine records recomputed here, the
 * conservation guard (A = L + E) failing closed, capacity, and seeded
 * partitions at scale: every node converges on the same digest whatever the
 * merge order, A = L + E after every advance, at most one claim per slot
 * settles, and final outcomes never change.
 * Agents: Belnap-Dunn values against an independent table-driven reference,
 * no explosion, self-contradicting sources quarantined, merge as a CRDT,
 * decisions through the action table, and agents as ledger witnesses. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pdx_ledger.h"
#include "pdx_agent.h"
#include "../robin_debanks/sha256.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__);                                \
        }                                                                                          \
    } while (0)

static uint64_t rng;
static uint64_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}
static uint32_t rndn(uint32_t n)
{
    return (uint32_t) (rnd() % n);
}

/* ---- a ledger with its own storage ---- */
#define CAP  256u
#define VCAP 256u
#define NACC 8u
#define LCAP 2048u
#define QCAP 512u

typedef struct {
    plg_t lg;
    plg_entry_t cl[CAP];
    plg_vote_t vt[VCAP];
    uint64_t bal[NACC], hold[CAP];
    uint64_t gen[NACC];
    pdx_rec_t rec[LCAP];
    pdx_log_t log;
    lpx_qrec_t qrec[QCAP];
    lpx_qlog_t q;
} node_t;

static plg_cfg_t base_cfg(node_t *n, uint32_t nacc, const uint64_t *gen, uint64_t equity)
{
    plg_cfg_t c;
    memset(&c, 0, sizeof c);
    uint64_t sum = 0;
    for (uint32_t a = 0; a < nacc; a++) {
        n->gen[a] = gen[a];
        sum += gen[a];
    }
    c.n_acct = nacc;
    c.genesis = n->gen;
    c.equity = equity;
    c.assets = sum + equity;
    c.n_witness = 5;
    c.arbiter = 100;
    c.owner = 200;
    c.conflict_limit = 3;
    c.decisive = 2;
    c.first_sig_rule = false;
    return c;
}

static bool node_init(node_t *n, const plg_cfg_t *c, uint32_t qcap)
{
    pdx_log_init(&n->log, n->rec, LCAP);
    lpx_qlog_init(&n->q, n->qrec, qcap);
    return plg_init(&n->lg, c, n->cl, CAP, n->vt, VCAP, n->bal, n->hold, &n->log, &n->q);
}

static plg_claim_t mk(uint32_t epoch, uint32_t from, uint32_t to, uint32_t nonce, uint64_t amt,
                      uint64_t seq, uint32_t salt)
{
    plg_claim_t c;
    memset(&c, 0, sizeof c);
    c.epoch = epoch;
    c.from = from;
    c.to = to;
    c.nonce = nonce;
    c.amount = amt;
    c.sig_seq = seq;
    uint32_t w[7] = {epoch, from, to, nonce, (uint32_t) amt, (uint32_t) seq, salt};
    sha256((const uint8_t *) w, sizeof w, c.id);
    return c;
}

static plg_vote_t vote(uint32_t epoch, uint8_t kind, uint32_t voter, const plg_claim_t *c, bool pos)
{
    plg_vote_t v;
    memset(&v, 0, sizeof v);
    v.epoch = epoch;
    v.kind = kind;
    v.voter = voter;
    v.positive = pos;
    memcpy(v.claim, c->id, 32);
    return v;
}

static uint8_t st_of(const node_t *n, const plg_claim_t *c)
{
    int32_t i = plg_find(&n->lg, c->id);
    return i < 0 ? 0xFF : n->lg.cl[i].st;
}

/* independent L: every balance plus every hold */
static bool ref_conserved(const node_t *n)
{
    unsigned __int128 l = 0;
    for (uint32_t a = 0; a < n->lg.cfg.n_acct; a++) l += n->bal[a];
    for (uint32_t i = 0; i < n->lg.n_cl; i++) l += n->hold[i];
    return l == n->lg.liab0 && (unsigned __int128) n->lg.cfg.assets == l + n->lg.cfg.equity;
}

/* ---- ledger tests ---- */
static void t_init(void)
{
    static node_t n;
    uint64_t g[3] = {100, 50, 0};
    plg_cfg_t c = base_cfg(&n, 3, g, 25);
    CHECK(node_init(&n, &c, QCAP), "init ok");
    CHECK(n.lg.liab0 == 150 && n.bal[0] == 100 && n.bal[1] == 50, "genesis copied");
    CHECK(plg_conserved(&n.lg), "conserved at genesis");
    plg_cfg_t b = c;
    b.conflict_limit = 1;
    CHECK(!node_init(&n, &b, QCAP), "limit 1 refused");
    b.conflict_limit = PLG_MAX_BRANCH + 1;
    CHECK(!node_init(&n, &b, QCAP), "limit above branch cap refused");
    b = c;
    b.conflict_limit = PLG_MAX_BRANCH;
    CHECK(node_init(&n, &b, QCAP), "limit at branch cap ok");
    b = c;
    b.assets = c.assets + 1;
    CHECK(!node_init(&n, &b, QCAP), "A != sum + E refused");
    b = c;
    b.equity = b.assets + 1;
    CHECK(!node_init(&n, &b, QCAP), "E > A refused");
    b = c;
    b.n_acct = 0;
    CHECK(!node_init(&n, &b, QCAP), "no accounts refused");
    static node_t o;
    uint64_t big[2] = {UINT64_MAX, 1};
    b = base_cfg(&o, 2, big, 0);
    CHECK(!node_init(&o, &b, QCAP), "genesis overflow refused");
    b = c;
    b.decisive = 0;
    CHECK(node_init(&n, &b, QCAP) && n.lg.cfg.decisive == 1, "decisive 0 becomes 1");
    CHECK(!plg_init(0, &c, n.cl, CAP, n.vt, VCAP, n.bal, n.hold, &n.log, &n.q), "null ledger");
    CHECK(!plg_init(&n.lg, &c, n.cl, CAP, n.vt, VCAP, n.bal, n.hold, &n.log, 0), "null qlog");
}

static void t_single(void)
{
    static node_t n;
    uint64_t g[3] = {100, 50, 0};
    plg_cfg_t c = base_cfg(&n, 3, g, 25);
    node_init(&n, &c, QCAP);
    plg_claim_t a = mk(1, 0, 2, 0, 40, 1, 0);
    CHECK(plg_claim(&n.lg, &a, true, true) == PLG_ADDED, "claim added");
    CHECK(st_of(&n, &a) == PLG_CS_PENDING && n.bal[0] == 100, "pending moves no money");
    CHECK(lpx_action(plg_tag(PLG_CS_PENDING)) == LPX_ACT_WAIT, "pending waits");
    CHECK(plg_claim(&n.lg, &a, true, true) == PLG_DUPLICATE, "duplicate");
    plg_claim_t a2 = a;
    a2.amount = 41;
    CHECK(plg_claim(&n.lg, &a2, true, true) == PLG_REFUSED_INVALID, "same id other content");
    plg_claim_t bad = mk(1, 0, 0, 1, 5, 1, 0);
    CHECK(plg_claim(&n.lg, &bad, true, true) == PLG_REFUSED_INVALID, "self transfer");
    bad = mk(1, 0, 3, 1, 5, 1, 0);
    CHECK(plg_claim(&n.lg, &bad, true, true) == PLG_REFUSED_INVALID, "unknown recipient");
    bad = mk(1, 3, 0, 1, 5, 1, 0);
    CHECK(plg_claim(&n.lg, &bad, true, true) == PLG_REFUSED_INVALID, "unknown sender");
    bad = mk(1, 0, 1, 1, 0, 1, 0);
    CHECK(plg_claim(&n.lg, &bad, true, true) == PLG_REFUSED_INVALID, "zero amount");
    bad = mk(0, 0, 1, 1, 5, 1, 0);
    CHECK(plg_claim(&n.lg, &bad, true, true) == PLG_REFUSED_INVALID, "epoch 0");
    bad = mk(1, 0, 1, 1, 5, 1, 0);
    CHECK(plg_claim(&n.lg, &bad, false, true) == PLG_REFUSED_INVALID, "bad signature");
    /* local funds: 100 less pending 40 */
    plg_claim_t over = mk(1, 0, 1, 1, 61, 1, 0);
    CHECK(plg_claim(&n.lg, &over, true, true) == PLG_REFUSED_FUNDS, "local over pending");
    plg_claim_t fits = mk(1, 0, 1, 1, 60, 1, 0);
    CHECK(plg_claim(&n.lg, &fits, true, true) == PLG_ADDED, "local fits exactly");
    plg_claim_t over2 = mk(1, 0, 1, 2, 1, 1, 0);
    CHECK(plg_claim(&n.lg, &over2, true, true) == PLG_REFUSED_FUNDS, "local exhausted");
    CHECK(plg_claim(&n.lg, &over2, true, false) == PLG_ADDED, "merged claim not refused");
    CHECK(plg_advance(&n.lg, 1) == PLG_OK, "advance");
    CHECK(st_of(&n, &a) == PLG_CS_SETTLED && st_of(&n, &fits) == PLG_CS_SETTLED, "both settled");
    CHECK(st_of(&n, &over2) == PLG_CS_INSUFFICIENT, "over-spend insufficient");
    CHECK(n.bal[0] == 0 && n.bal[1] == 110 && n.bal[2] == 40, "balances moved");
    CHECK(plg_conserved(&n.lg) && ref_conserved(&n), "A = L + E");
    CHECK(lpx_action(plg_tag(st_of(&n, &a))) == LPX_ACT_PROCEED, "settled proceeds");
    CHECK(lpx_action(plg_tag(PLG_CS_INSUFFICIENT)) == LPX_ACT_ROLLBACK, "insufficient rolls back");
    CHECK(n.q.n == 1 && n.qrec[0].reason == PLG_Q_INSUFFICIENT && n.qrec[0].submitter == 0 &&
              !memcmp(n.qrec[0].subject, over2.id, 32) && lpx_forbidden(n.qrec[0].tag),
          "insufficient quarantined");
    CHECK(plg_advance(&n.lg, 1) == PLG_OK && n.q.n == 1, "no second record");
    plg_claim_t late = mk(1, 1, 2, 0, 1, 1, 0);
    CHECK(plg_claim(&n.lg, &late, true, true) == PLG_REFUSED_LATE, "late refused");
    CHECK(plg_advance(&n.lg, 0) == PLG_REFUSED_INVALID, "horizon never moves back");
    CHECK(pdx_log_verify(&n.log) && lpx_qlog_verify(&n.q), "logs verify");
    /* the advance record carries the digest */
    const pdx_rec_t *last = &n.rec[n.log.n - 1];
    CHECK(last->op == PLG_OP_ADVANCE && last->aux == 1 && !memcmp(last->a, n.lg.digest, 32),
          "advance record");
}

static void t_double_spend_witness(void)
{
    static node_t n;
    uint64_t g[3] = {100, 0, 0};
    plg_cfg_t c = base_cfg(&n, 3, g, 0);
    node_init(&n, &c, QCAP);
    plg_claim_t x = mk(1, 0, 1, 7, 30, 5, 1);
    plg_claim_t y = mk(1, 0, 2, 7, 70, 5, 2);
    CHECK(plg_claim(&n.lg, &x, true, true) == PLG_ADDED, "x added");
    CHECK(plg_claim(&n.lg, &y, true, true) == PLG_ADDED_BOTH, "y makes the slot Both");
    CHECK(plg_advance(&n.lg, 1) == PLG_OK, "advance");
    CHECK(st_of(&n, &x) == PLG_CS_HELD && st_of(&n, &y) == PLG_CS_HELD, "both held");
    CHECK(n.lg.escrow == 70 && n.bal[0] == 30 && n.bal[1] == 0 && n.bal[2] == 0,
          "largest amount in escrow");
    CHECK(ref_conserved(&n), "conserved while held");
    lpx_t t = plg_tag(PLG_CS_HELD);
    CHECK(lpx_base(t) == LPX_B_BOTH && pdx_kind_of_tag(t) == PDX_KIND_NEUTRAL, "held is neutral");
    CHECK(lpx_action(t) == LPX_ACT_ESCALATE, "held escalates");
    CHECK(n.q.n == 0, "held claims are not quarantined");
    /* one witness is not decisive (decisive = 2) */
    plg_vote_t v0 = vote(2, PLG_V_WITNESS, 0, &x, true);
    CHECK(plg_vote(&n.lg, &v0, true) == PLG_ADDED, "witness 0");
    CHECK(plg_vote(&n.lg, &v0, true) == PLG_DUPLICATE, "duplicate vote");
    plg_vote_t v0b = vote(3, PLG_V_WITNESS, 0, &x, true);
    CHECK(plg_vote(&n.lg, &v0b, true) == PLG_ADDED, "same witness, later epoch");
    plg_vote_t bad = vote(2, PLG_V_WITNESS, 5, &x, true);
    CHECK(plg_vote(&n.lg, &bad, true) == PLG_REFUSED_INVALID, "unregistered witness");
    bad = vote(2, PLG_V_ARBITER, 1, &x, true);
    CHECK(plg_vote(&n.lg, &bad, true) == PLG_REFUSED_INVALID, "wrong arbiter");
    bad = vote(2, PLG_V_OWNER, 200, &x, false);
    CHECK(plg_vote(&n.lg, &bad, true) == PLG_REFUSED_INVALID, "negative owner ruling");
    bad = vote(2, 9, 0, &x, true);
    CHECK(plg_vote(&n.lg, &bad, true) == PLG_REFUSED_INVALID, "unknown vote kind");
    bad = vote(2, PLG_V_WITNESS, 1, &x, true);
    CHECK(plg_vote(&n.lg, &bad, false) == PLG_REFUSED_INVALID, "vote bad signature");
    CHECK(plg_advance(&n.lg, 3) == PLG_OK && st_of(&n, &x) == PLG_CS_HELD,
          "one witness counted once: still held");
    /* witness 1 against y, witness 2 for x: x net 2, y net -1: lead 3 */
    plg_vote_t v1 = vote(4, PLG_V_WITNESS, 1, &y, false);
    plg_vote_t v2 = vote(4, PLG_V_WITNESS, 2, &x, true);
    CHECK(plg_vote(&n.lg, &v1, true) == PLG_ADDED && plg_vote(&n.lg, &v2, true) == PLG_ADDED,
          "two more witnesses");
    plg_vote_t old = vote(3, PLG_V_WITNESS, 3, &x, true);
    CHECK(plg_vote(&n.lg, &old, true) == PLG_REFUSED_LATE, "late vote refused");
    CHECK(plg_advance(&n.lg, 4) == PLG_OK, "advance");
    CHECK(st_of(&n, &x) == PLG_CS_SETTLED && st_of(&n, &y) == PLG_CS_LOST, "x won, y lost");
    CHECK(n.bal[0] == 70 && n.bal[1] == 30 && n.bal[2] == 0 && n.lg.escrow == 0,
          "x paid, the rest refunded");
    CHECK(ref_conserved(&n), "conserved after settle");
    /* equivocation record: subject = H(tag || head id || lost id) */
    /* whichever of x and y is the slot's head, the pair is (x, y) */
    uint8_t buf[16 + 64], subj[32];
    memcpy(buf, "zxv/plg-equiv/v1", 16);
    memcpy(buf + 16, x.id, 32);
    memcpy(buf + 48, y.id, 32);
    sha256(buf, sizeof buf, subj);
    CHECK(n.q.n == 1 && n.qrec[0].reason == PLG_Q_EQUIVOCATION && n.qrec[0].submitter == 0 &&
              !memcmp(n.qrec[0].subject, subj, 32),
          "equivocation recorded with both ids");
    CHECK(lpx_qlog_verify(&n.q) && pdx_log_verify(&n.log), "logs verify");
    /* a later claim on the closed slot is lost and recorded */
    plg_claim_t z = mk(5, 0, 2, 7, 1, 1, 3);
    CHECK(plg_claim(&n.lg, &z, true, true) == PLG_ADDED_BOTH, "late double spend stored");
    CHECK(plg_advance(&n.lg, 5) == PLG_OK && st_of(&n, &z) == PLG_CS_LOST, "late one lost");
    CHECK(n.bal[0] == 70 && n.q.n == 2 && n.qrec[1].reason == PLG_Q_EQUIVOCATION, "recorded");
}

static void t_first_sig_and_rulings(void)
{
    static node_t n;
    uint64_t g[3] = {100, 0, 0};
    plg_cfg_t c = base_cfg(&n, 3, g, 0);
    c.first_sig_rule = true;
    node_init(&n, &c, QCAP);
    plg_claim_t x = mk(1, 0, 1, 0, 10, 9, 1);
    plg_claim_t y = mk(1, 0, 2, 0, 20, 4, 2);
    plg_claim(&n.lg, &x, true, true);
    plg_claim(&n.lg, &y, true, true);
    plg_advance(&n.lg, 1);
    CHECK(st_of(&n, &y) == PLG_CS_SETTLED && st_of(&n, &x) == PLG_CS_LOST, "lowest seq wins");
    CHECK(n.bal[0] == 80 && n.bal[2] == 20, "paid at once");
    /* a tie stays held, then the arbiter rules */
    plg_claim_t p = mk(2, 0, 1, 1, 10, 3, 1);
    plg_claim_t q = mk(2, 0, 2, 1, 15, 3, 2);
    plg_claim(&n.lg, &p, true, true);
    plg_claim(&n.lg, &q, true, true);
    plg_advance(&n.lg, 2);
    CHECK(st_of(&n, &p) == PLG_CS_HELD && n.lg.escrow == 15, "tie held");
    /* a third claim with a lower seq breaks the tie under the rule */
    plg_claim_t r = mk(3, 0, 1, 1, 5, 1, 3);
    plg_claim(&n.lg, &r, true, true);
    plg_advance(&n.lg, 3);
    CHECK(st_of(&n, &r) == PLG_CS_SETTLED && st_of(&n, &p) == PLG_CS_LOST &&
              st_of(&n, &q) == PLG_CS_LOST,
          "new lowest seq settles the held slot");
    CHECK(n.bal[0] == 75 && n.bal[1] == 5 && n.lg.escrow == 0 && ref_conserved(&n), "refunded");

    static node_t m;
    c = base_cfg(&m, 3, g, 0);
    node_init(&m, &c, QCAP);
    x = mk(1, 0, 1, 0, 10, 1, 1);
    y = mk(1, 0, 2, 0, 20, 1, 2);
    plg_claim(&m.lg, &x, true, true);
    plg_claim(&m.lg, &y, true, true);
    plg_vote_t arb = vote(2, PLG_V_ARBITER, 100, &x, true);
    plg_vote_t own = vote(2, PLG_V_OWNER, 200, &y, true);
    plg_vote(&m.lg, &arb, true);
    plg_vote(&m.lg, &own, true);
    plg_advance(&m.lg, 2);
    CHECK(st_of(&m, &y) == PLG_CS_SETTLED && st_of(&m, &x) == PLG_CS_LOST, "owner over arbiter");
    CHECK(m.bal[0] == 80 && m.bal[2] == 20 && ref_conserved(&m), "owner's pick paid");

    static node_t k;
    c = base_cfg(&k, 3, g, 0);
    node_init(&k, &c, QCAP);
    plg_claim(&k.lg, &x, true, true);
    plg_claim(&k.lg, &y, true, true);
    plg_vote(&k.lg, &arb, true);
    plg_vote_t w0 = vote(2, PLG_V_WITNESS, 0, &y, true), w1 = vote(2, PLG_V_WITNESS, 1, &y, true);
    plg_vote(&k.lg, &w0, true);
    plg_vote(&k.lg, &w1, true);
    plg_advance(&k.lg, 2);
    CHECK(st_of(&k, &x) == PLG_CS_SETTLED, "arbiter over witnesses");
    /* an arbiter ruling for a claim of another slot does nothing */
    static node_t o;
    node_init(&o, &c, QCAP);
    plg_claim_t s1 = mk(1, 0, 1, 4, 10, 1, 1), s2 = mk(1, 0, 2, 4, 10, 1, 2);
    plg_claim(&o.lg, &s1, true, true);
    plg_claim(&o.lg, &s2, true, true);
    plg_claim_t other = mk(1, 0, 1, 5, 10, 1, 1);
    plg_claim(&o.lg, &other, true, true);
    plg_vote_t arb2 = vote(2, PLG_V_ARBITER, 100, &other, true);
    plg_vote(&o.lg, &arb2, true);
    plg_advance(&o.lg, 2);
    CHECK(st_of(&o, &s1) == PLG_CS_HELD && st_of(&o, &other) == PLG_CS_SETTLED,
          "ruling only within its slot");
    /* a ruling for a claim that is not yet visible waits for it */
    static node_t v;
    node_init(&v, &c, QCAP);
    plg_claim(&v.lg, &s1, true, true);
    plg_claim(&v.lg, &s2, true, true);
    plg_claim_t s3 = mk(4, 0, 2, 4, 5, 1, 3);
    plg_vote_t early = vote(2, PLG_V_OWNER, 200, &s3, true);
    plg_vote(&v.lg, &early, true);
    plg_advance(&v.lg, 3);
    CHECK(st_of(&v, &s1) == PLG_CS_HELD, "ruling for an unseen claim does nothing");
    plg_claim(&v.lg, &s3, true, false);
    plg_advance(&v.lg, 4);
    CHECK(st_of(&v, &s3) == PLG_CS_SETTLED && v.bal[0] == 95 && ref_conserved(&v),
          "ruling applies once the claim is visible");
}

static void t_frozen(void)
{
    static node_t n;
    uint64_t g[3] = {100, 0, 0};
    plg_cfg_t c = base_cfg(&n, 3, g, 0);
    c.conflict_limit = 2;
    c.first_sig_rule = true;
    node_init(&n, &c, QCAP);
    plg_claim_t a = mk(1, 0, 1, 0, 10, 1, 1), b = mk(1, 0, 2, 0, 20, 2, 2),
                d = mk(1, 0, 1, 0, 30, 3, 3);
    plg_claim(&n.lg, &a, true, true);
    plg_claim(&n.lg, &b, true, true);
    plg_claim(&n.lg, &d, true, true);
    plg_vote_t arb = vote(2, PLG_V_ARBITER, 100, &a, true);
    plg_vote_t w0 = vote(2, PLG_V_WITNESS, 0, &a, true), w1 = vote(2, PLG_V_WITNESS, 1, &a, true);
    plg_vote(&n.lg, &arb, true);
    plg_vote(&n.lg, &w0, true);
    plg_vote(&n.lg, &w1, true);
    plg_advance(&n.lg, 2);
    CHECK(st_of(&n, &a) == PLG_CS_FROZEN && st_of(&n, &d) == PLG_CS_FROZEN,
          "past the limit: frozen; rule, arbiter and witnesses ignored");
    CHECK(n.lg.escrow == 30 && n.bal[0] == 70 && ref_conserved(&n), "frozen funds in escrow");
    lpx_t t = plg_tag(PLG_CS_FROZEN);
    CHECK(pdx_kind_of_tag(t) == PDX_KIND_DESTRUCTIVE && lpx_action(t) == LPX_ACT_ESCALATE,
          "frozen is destructive and escalates");
    plg_vote_t own = vote(3, PLG_V_OWNER, 200, &b, true);
    plg_vote(&n.lg, &own, true);
    plg_advance(&n.lg, 3);
    CHECK(st_of(&n, &b) == PLG_CS_SETTLED && st_of(&n, &a) == PLG_CS_LOST &&
              st_of(&n, &d) == PLG_CS_LOST,
          "owner settles a frozen slot");
    CHECK(n.bal[0] == 80 && n.bal[2] == 20 && n.lg.escrow == 0 && ref_conserved(&n), "paid");
    CHECK(n.q.n == 2, "two equivocation records");
    /* a held slot that a new claim pushes over the limit freezes */
    static node_t m;
    node_init(&m, &c, QCAP);
    plg_claim_t p = mk(1, 0, 1, 0, 10, 5, 1), q = mk(1, 0, 2, 0, 10, 5, 2);
    plg_claim(&m.lg, &p, true, true);
    plg_claim(&m.lg, &q, true, true);
    plg_advance(&m.lg, 1);
    CHECK(st_of(&m, &p) == PLG_CS_HELD, "tie held");
    plg_claim_t r = mk(2, 0, 1, 0, 50, 1, 3);
    plg_claim(&m.lg, &r, true, true);
    plg_advance(&m.lg, 2);
    CHECK(st_of(&m, &r) == PLG_CS_FROZEN && st_of(&m, &p) == PLG_CS_FROZEN, "now frozen");
    CHECK(m.lg.escrow == 50 && m.bal[0] == 50 && ref_conserved(&m), "escrow topped up");
}

static void t_funding(void)
{
    /* same-epoch chain: 2 -> 1 then 1 -> 0, with 1 starting empty */
    static node_t n;
    uint64_t g[3] = {0, 0, 50};
    plg_cfg_t c = base_cfg(&n, 3, g, 0);
    node_init(&n, &c, QCAP);
    plg_claim_t b = mk(1, 1, 0, 0, 30, 1, 0);
    plg_claim_t a = mk(1, 2, 1, 0, 40, 1, 0);
    plg_claim(&n.lg, &b, true, false);
    plg_claim(&n.lg, &a, true, true);
    plg_advance(&n.lg, 1);
    CHECK(st_of(&n, &a) == PLG_CS_SETTLED && st_of(&n, &b) == PLG_CS_SETTLED,
          "fixpoint settles the chain");
    CHECK(n.bal[0] == 30 && n.bal[1] == 10 && n.bal[2] == 10, "chain balances");
    /* income in a LATER epoch never rescues an earlier insufficient claim */
    static node_t m;
    node_init(&m, &c, QCAP);
    plg_claim_t early = mk(1, 1, 0, 0, 30, 1, 0);
    plg_claim_t income = mk(2, 2, 1, 0, 40, 1, 0);
    plg_claim(&m.lg, &early, true, false);
    plg_advance(&m.lg, 1);
    CHECK(st_of(&m, &early) == PLG_CS_INSUFFICIENT, "insufficient at its epoch");
    plg_claim(&m.lg, &income, true, true);
    plg_advance(&m.lg, 2);
    CHECK(st_of(&m, &early) == PLG_CS_INSUFFICIENT && m.bal[1] == 40, "stays insufficient");
    CHECK(m.q.n == 1, "one record");
    /* a dispute the sender cannot fully cover holds what there is */
    static node_t k;
    uint64_t g2[3] = {10, 0, 0};
    plg_cfg_t c2 = base_cfg(&k, 3, g2, 5);
    node_init(&k, &c2, QCAP);
    plg_claim_t x = mk(1, 0, 1, 0, 8, 1, 1), y = mk(1, 0, 2, 0, 15, 1, 2);
    plg_claim(&k.lg, &x, true, false);
    plg_claim(&k.lg, &y, true, false);
    plg_advance(&k.lg, 1);
    CHECK(st_of(&k, &x) == PLG_CS_HELD && k.lg.escrow == 10 && k.bal[0] == 0, "partial hold");
    plg_vote_t own = vote(2, PLG_V_OWNER, 200, &y, true);
    plg_vote(&k.lg, &own, true);
    plg_advance(&k.lg, 2);
    CHECK(st_of(&k, &y) == PLG_CS_INSUFFICIENT && st_of(&k, &x) == PLG_CS_LOST,
          "unfundable pick is insufficient");
    CHECK(k.bal[0] == 10 && k.lg.escrow == 0 && ref_conserved(&k), "escrow refunded");
    /* the winner needs a top-up from the balance */
    static node_t h;
    uint64_t g3[3] = {100, 0, 0};
    plg_cfg_t c3 = base_cfg(&h, 3, g3, 0);
    node_init(&h, &c3, QCAP);
    plg_claim_t u = mk(1, 0, 1, 0, 10, 1, 1), w = mk(1, 0, 2, 0, 20, 1, 2);
    plg_claim(&h.lg, &u, true, true);
    plg_claim(&h.lg, &w, true, true);
    plg_advance(&h.lg, 1);
    plg_claim_t big = mk(2, 0, 1, 0, 60, 1, 3);
    plg_claim(&h.lg, &big, true, false);
    plg_advance(&h.lg, 2);
    CHECK(h.lg.escrow == 60 && h.bal[0] == 40, "top-up to the new largest");
    plg_vote_t o2 = vote(3, PLG_V_OWNER, 200, &u, true);
    plg_vote(&h.lg, &o2, true);
    plg_advance(&h.lg, 3);
    CHECK(h.bal[0] == 90 && h.bal[1] == 10 && ref_conserved(&h), "smaller winner refunds rest");
}

static void t_forbidden(void)
{
    static node_t n;
    uint64_t g[3] = {100, 50, 0};
    plg_cfg_t c = base_cfg(&n, 3, g, 7);
    node_init(&n, &c, QCAP);
    plg_claim_t a = mk(1, 0, 2, 0, 40, 1, 0);
    plg_claim(&n.lg, &a, true, true);
    CHECK(plg_advance(&n.lg, 1) == PLG_OK, "fine");
    n.gen[1] = 51; /* someone edits genesis: L no longer matches */
    plg_claim_t b = mk(2, 1, 2, 0, 5, 1, 0);
    plg_claim(&n.lg, &b, true, true);
    uint32_t q0 = n.q.n;
    CHECK(plg_advance(&n.lg, 2) == PLG_FORBIDDEN, "conservation broken: forbidden");
    CHECK(n.lg.halted, "halted");
    CHECK(n.q.n == q0 + 1 && n.qrec[q0].reason == PLG_Q_CONSERVATION &&
              lpx_forbidden(n.qrec[q0].tag),
          "quarantine record");
    CHECK(n.rec[n.log.n - 1].outcome == PLG_FORBIDDEN, "logged");
    CHECK(plg_claim(&n.lg, &b, true, true) == PLG_HALTED, "claims refused after halt");
    plg_vote_t v = vote(3, PLG_V_WITNESS, 0, &b, true);
    CHECK(plg_vote(&n.lg, &v, true) == PLG_HALTED, "votes refused after halt");
    CHECK(plg_advance(&n.lg, 3) == PLG_HALTED, "advance refused after halt");
    /* a corrupt balance caught by plg_conserved */
    static node_t m;
    c = base_cfg(&m, 3, g, 7);
    node_init(&m, &c, QCAP);
    CHECK(plg_conserved(&m.lg), "ok");
    m.bal[0]++;
    CHECK(!plg_conserved(&m.lg), "extra unit caught");
    m.bal[0]--;
    m.lg.escrow = 1;
    CHECK(!plg_conserved(&m.lg), "escrow without a hold caught");
    m.lg.escrow = 0;
    plg_claim_t e = mk(5, 0, 1, 0, 1, 1, 0);
    plg_claim(&m.lg, &e, true, true);
    m.hold[0] = 1;
    CHECK(!plg_conserved(&m.lg), "hold without escrow caught");
    m.lg.escrow = 1;
    CHECK(!plg_conserved(&m.lg), "escrow beyond liabilities caught");
    m.hold[0] = 0;
    m.lg.escrow = 0;
    m.bal[0] = UINT64_MAX;
    uint64_t l;
    CHECK(!plg_liabilities(&m.lg, &l) && !plg_conserved(&m.lg), "overflow caught");
}

static void t_capacity(void)
{
    static node_t n;
    uint64_t g[2] = {1000, 0};
    plg_cfg_t c = base_cfg(&n, 2, g, 0);
    pdx_log_init(&n.log, n.rec, LCAP);
    lpx_qlog_init(&n.q, n.qrec, 1);
    plg_init(&n.lg, &c, n.cl, 3, n.vt, 1, n.bal, n.hold, &n.log, &n.q);
    plg_claim_t a = mk(1, 0, 1, 0, 1, 1, 0), b = mk(1, 0, 1, 1, 2000, 1, 0),
                d = mk(1, 0, 1, 2, 2000, 1, 0), e = mk(1, 0, 1, 3, 1, 1, 0);
    CHECK(plg_claim(&n.lg, &a, true, false) == PLG_ADDED, "1");
    CHECK(plg_claim(&n.lg, &b, true, false) == PLG_ADDED, "2");
    CHECK(plg_claim(&n.lg, &d, true, false) == PLG_ADDED, "3");
    CHECK(plg_claim(&n.lg, &e, true, false) == PLG_FULL, "claims full");
    plg_vote_t v = vote(1, PLG_V_WITNESS, 0, &a, true), v2 = vote(1, PLG_V_WITNESS, 1, &a, true);
    CHECK(plg_vote(&n.lg, &v, true) == PLG_ADDED && plg_vote(&n.lg, &v2, true) == PLG_FULL,
          "votes full");
    /* two insufficient claims, room for one record: halt */
    CHECK(plg_advance(&n.lg, 1) == PLG_FULL && n.lg.halted && n.q.n == 0, "qlog full halts");
    /* operator log full */
    static node_t m;
    pdx_log_init(&m.log, m.rec, 1);
    lpx_qlog_init(&m.q, m.qrec, QCAP);
    plg_init(&m.lg, &c, m.cl, CAP, m.vt, VCAP, m.bal, m.hold, &m.log, &m.q);
    CHECK(plg_claim(&m.lg, &a, true, false) == PLG_ADDED, "one record fits");
    CHECK(plg_claim(&m.lg, &b, true, false) == PLG_FULL && m.lg.n_cl == 1, "log full refuses");
    CHECK(plg_vote(&m.lg, &v, true) == PLG_FULL, "log full refuses votes");
    CHECK(plg_advance(&m.lg, 1) == PLG_FULL, "log full halts advance");
}

/* ---- P1.3 / P1.4: seeded partitions at scale ---- */
#define NODES 3u
static node_t nd[NODES];

static void random_events(node_t *n, uint32_t epoch, uint32_t nclaims, uint32_t salt)
{
    for (uint32_t k = 0; k < nclaims; k++) {
        uint32_t from = rndn(NACC), to = rndn(NACC);
        if (to == from) to = (to + 1) % NACC;
        /* a small nonce range so partitions collide: cross-partition double spends */
        plg_claim_t c = mk(epoch, from, to, rndn(6), 1 + rndn(60), rndn(4), salt * 1000 + k);
        plg_claim(&n->lg, &c, true, rndn(2) == 0);
    }
    uint32_t nv = rndn(4);
    for (uint32_t k = 0; k < nv && n->lg.n_cl; k++) {
        const plg_claim_t *t = &n->lg.cl[rndn(n->lg.n_cl)].c;
        uint32_t r = rndn(10);
        plg_vote_t v = r == 0   ? vote(epoch, PLG_V_ARBITER, 100, t, true)
                       : r == 1 ? vote(epoch, PLG_V_OWNER, 200, t, true)
                                : vote(epoch, PLG_V_WITNESS, rndn(5), t, rndn(3) != 0);
        plg_vote(&n->lg, &v, true);
    }
}

static bool slot_invariants(const node_t *n)
{
    for (uint32_t i = 0; i < n->lg.n_cl; i++) {
        const plg_entry_t *e = &n->lg.cl[i];
        if (e->c.epoch > n->lg.horizon && e->st != PLG_CS_PENDING) return false;
        if (e->c.epoch <= n->lg.horizon && e->st == PLG_CS_PENDING) return false;
        if (e->st != PLG_CS_SETTLED) continue;
        for (uint32_t j = 0; j < n->lg.n_cl; j++)
            if (j != i && n->lg.cl[j].c.from == e->c.from && n->lg.cl[j].c.nonce == e->c.nonce &&
                (n->lg.cl[j].st == PLG_CS_SETTLED || n->lg.cl[j].st == PLG_CS_HELD))
                return false;
    }
    for (uint32_t a = 0; a < n->lg.cfg.n_acct; a++)
        if (n->bal[a] > n->lg.liab0) return false;
    return true;
}

static void t_partitions(void)
{
    uint32_t seeds = 80, conv = 0, cons = 0, inv = 0, stable = 0, both = 0, held_seen = 0;
    static uint8_t snap[CAP];
    static uint8_t snapid[CAP][32];
    for (uint32_t s = 1; s <= seeds; s++) {
        rng = 0x9E3779B97F4A7C15ull * s;
        uint64_t g[NACC];
        for (uint32_t a = 0; a < NACC; a++) g[a] = rndn(150);
        uint32_t limit = 2 + rndn(3); /* every node runs the same published config */
        for (uint32_t i = 0; i < NODES; i++) {
            plg_cfg_t c = base_cfg(&nd[i], NACC, g, 1000);
            c.first_sig_rule = s & 1u;
            c.conflict_limit = limit;
            node_init(&nd[i], &c, QCAP);
        }
        uint32_t horizon = 0, epoch = 1, ok_c = 1, ok_v = 1, ok_i = 1, ok_s = 1;
        uint32_t nsnap = 0;
        for (uint32_t round = 0; round < 4; round++) {
            /* partitioned: each node works alone for two epochs */
            for (uint32_t e = 0; e < 2; e++, epoch++)
                for (uint32_t i = 0; i < NODES; i++) random_events(&nd[i], epoch, 4, s * 16 + i);
            /* reconnect: merge in a random pairwise order until every set agrees */
            for (uint32_t k = 0; k < 2 * NODES * NODES; k++) {
                uint32_t a = rndn(NODES), b = rndn(NODES);
                if (a != b) plg_merge(&nd[a].lg, &nd[b].lg, 0, 0);
            }
            for (uint32_t i = 0; i < NODES; i++)
                for (uint32_t j = 0; j < NODES; j++)
                    if (i != j) plg_merge(&nd[i].lg, &nd[j].lg, 0, 0);
            horizon = epoch - 1;
            for (uint32_t i = 0; i < NODES; i++) {
                if (plg_advance(&nd[i].lg, horizon) != PLG_OK) ok_c = 0;
                if (!plg_conserved(&nd[i].lg) || !ref_conserved(&nd[i])) ok_c = 0;
                if (!slot_invariants(&nd[i])) ok_i = 0;
                if (!pdx_log_verify(&nd[i].log) || !lpx_qlog_verify(&nd[i].q)) ok_i = 0;
            }
            for (uint32_t i = 1; i < NODES; i++)
                if (memcmp(nd[i].lg.digest, nd[0].lg.digest, 32)) ok_v = 0;
            /* earlier final outcomes unchanged */
            for (uint32_t k = 0; k < nsnap; k++) {
                int32_t ix = plg_find(&nd[0].lg, snapid[k]);
                if (ix < 0 || nd[0].lg.cl[ix].st != snap[k]) ok_s = 0;
            }
            /* outcomes that can still change: held and frozen (their ruling may come) */
            nsnap = 0;
            for (uint32_t i = 0; i < nd[0].lg.n_cl; i++) {
                uint8_t st = nd[0].lg.cl[i].st;
                if (st == PLG_CS_HELD || st == PLG_CS_FROZEN) {
                    held_seen++;
                    continue;
                }
                memcpy(snapid[nsnap], nd[0].lg.cl[i].c.id, 32);
                snap[nsnap++] = st;
            }
        }
        for (uint32_t i = 0; i < nd[0].lg.n_cl; i++)
            for (uint32_t j = i + 1; j < nd[0].lg.n_cl; j++)
                if (nd[0].lg.cl[i].c.from == nd[0].lg.cl[j].c.from &&
                    nd[0].lg.cl[i].c.nonce == nd[0].lg.cl[j].c.nonce)
                    both = 1;
        conv += ok_v;
        cons += ok_c;
        inv += ok_i;
        stable += ok_s;
    }
    CHECK(conv == seeds, "every node converges on the same digest");
    CHECK(cons == seeds, "A = L + E after every advance");
    CHECK(inv == seeds, "one claim per slot settles; balances bounded; logs verify");
    CHECK(stable == seeds, "final outcomes never change");
    CHECK(both && held_seen > 0, "the scenarios really contain double spends and disputes");
    printf("  partitions: %u seeds x %u nodes x 4 rounds, %u held/frozen observations\n", seeds,
           NODES, held_seen);
}

/* the same event set in any order gives the same state */
static void t_order_free(void)
{
    static plg_claim_t ev[60];
    static plg_vote_t vv[20];
    static node_t a, b;
    uint32_t same = 0, trials = 60;
    for (uint32_t t = 1; t <= trials; t++) {
        rng = 0xD1B54A32D192ED03ull * t;
        uint64_t g[NACC];
        for (uint32_t i = 0; i < NACC; i++) g[i] = rndn(100);
        for (uint32_t i = 0; i < 60; i++) {
            uint32_t from = rndn(NACC), to = (from + 1 + rndn(NACC - 1)) % NACC;
            ev[i] = mk(1 + rndn(5), from, to, rndn(4), 1 + rndn(50), rndn(3), i);
        }
        for (uint32_t i = 0; i < 20; i++)
            vv[i] = vote(1 + rndn(5), PLG_V_WITNESS, rndn(5), &ev[rndn(60)], rndn(2));
        plg_cfg_t c = base_cfg(&a, NACC, g, 0);
        c.first_sig_rule = t & 1u;
        node_init(&a, &c, QCAP);
        node_init(&b, &c, QCAP);
        for (uint32_t i = 0; i < 60; i++) plg_claim(&a.lg, &ev[i], true, false);
        for (uint32_t i = 0; i < 20; i++) plg_vote(&a.lg, &vv[i], true);
        /* b: reversed and interleaved, every event twice */
        for (uint32_t i = 0; i < 60; i++) {
            plg_claim(&b.lg, &ev[59 - i], true, false);
            if (i < 20) plg_vote(&b.lg, &vv[19 - i], true);
            plg_claim(&b.lg, &ev[(i * 7) % 60], true, false);
        }
        plg_advance(&a.lg, 5);
        plg_advance(&b.lg, 5);
        if (!memcmp(a.lg.digest, b.lg.digest, 32) && ref_conserved(&a)) same++;
    }
    CHECK(same == trials, "arrival order and duplicates never change the state");
}

/* ---- agents ---- */
#define NP 8u
typedef struct {
    pag_agent_t a;
    pag_set_t ft[NP], ff[NP], t[NP], f[NP];
    uint32_t p0[NP], n0[NP];
} agent_t;

static bool agent_init(agent_t *g, const pag_rule_t *r, uint32_t nr)
{
    return pag_init(&g->a, NP, g->ft, g->ff, g->t, g->f, g->p0, g->n0, r, nr);
}

enum { FEE_OK = 0, FUNDS_OK, PAY_OUT, WEATHER, NOT_BLOCKED, SHIP, NP_X, NP_Y };

static void t_agent_jurisdictions(void)
{
    static agent_t g;
    /* pay_out <- fee_ok & funds_ok ; ship <- pay_out & not_blocked */
    pag_rule_t r[2] = {{PAY_OUT, 2, {FEE_OK, FUNDS_OK, 0, 0}, {false, false, false, false}},
                       {SHIP, 2, {PAY_OUT, NOT_BLOCKED, 0, 0}, {false, false, false, false}}};
    CHECK(agent_init(&g, r, 2), "agent init");
    pag_tell(&g.a, FEE_OK, 0, true);  /* jurisdiction A allows the fee */
    pag_tell(&g.a, FEE_OK, 1, false); /* jurisdiction B forbids it */
    pag_tell(&g.a, FUNDS_OK, 2, true);
    pag_tell(&g.a, WEATHER, 3, true);
    pag_tell(&g.a, NOT_BLOCKED, 3, true);
    CHECK(pag_run(&g.a, 0), "runs");
    CHECK(pag_value(&g.a, FEE_OK) == LPRES_STATE_BOTH, "fee is Both");
    CHECK(pag_value(&g.a, PAY_OUT) == LPRES_STATE_BOTH, "conclusion carries the contradiction");
    CHECK(pag_value(&g.a, SHIP) == LPRES_STATE_BOTH, "and so does what depends on it");
    CHECK(pag_value(&g.a, WEATHER) == LPRES_STATE_TRUE, "an unrelated fact stays True");
    CHECK(pag_value(&g.a, NP_X) == LPRES_STATE_NEITHER, "no explosion: unknown stays Neither");
    CHECK(g.a.t[PAY_OUT] == 0x5u && g.a.f[PAY_OUT] == 0x2u, "support sets carry provenance");
    pag_decision_t d0 = pag_decide(&g.a, PAY_OUT, false, 0, 0);
    CHECK(d0.kind == PDX_KIND_CONSTRUCTIVE, "first sight: evidence grew from nothing");
    pag_snapshot(&g.a);
    pag_decision_t d = pag_decide(&g.a, PAY_OUT, false, 0, 0);
    CHECK(d.act == LPX_ACT_ESCALATE && d.kind == PDX_KIND_NEUTRAL, "Both: ask a person");
    d = pag_decide(&g.a, WEATHER, false, 0, 0);
    CHECK(d.act == LPX_ACT_PROCEED && d.kind == PDX_KIND_NONE, "True: proceed");
    d = pag_decide(&g.a, WEATHER, true, 0, 0);
    CHECK(d.act == LPX_ACT_WAIT && lpx_pol(d.tag) == LPX_P_ENTROPIC, "stale True waits");
    d = pag_decide(&g.a, NP_X, false, 0, 0);
    CHECK(d.act == LPX_ACT_WAIT, "Neither waits");
    pag_snapshot(&g.a);
    /* B withdraws nothing, but two more sources back A: converging */
    pag_tell(&g.a, FEE_OK, 4, true);
    pag_tell(&g.a, FEE_OK, 5, true);
    pag_run(&g.a, 0);
    d = pag_decide(&g.a, FEE_OK, false, 0, 0);
    CHECK(d.kind == PDX_KIND_CONSTRUCTIVE && d.act == LPX_ACT_WAIT && d.pos == 3 && d.neg == 1,
          "converging dispute: constructive, waits in escrow");
    pag_snapshot(&g.a);
    pag_tell(&g.a, FEE_OK, 6, false);
    pag_tell(&g.a, FEE_OK, 7, false);
    pag_run(&g.a, 0);
    d = pag_decide(&g.a, FEE_OK, false, 0, 0);
    CHECK(d.kind == PDX_KIND_DESTRUCTIVE && d.act == LPX_ACT_ESCALATE, "diverging: destructive");
    /* a registry signature overrides the trend */
    pdx_sig_t ent[2];
    pdx_registry_t reg;
    pdx_reg_init(&reg, ent, 2);
    uint8_t sig[32];
    pdx_signature((const uint8_t *) "fee-dispute", 11, sig);
    pdx_reg_add(&reg, sig, PDX_KIND_NEUTRAL, 1, 0, PDX_SRC_SEED);
    d = pag_decide(&g.a, FEE_OK, false, &reg, sig);
    CHECK(d.kind == PDX_KIND_NEUTRAL, "registry overrides");
    /* False refuses */
    static agent_t h;
    agent_init(&h, r, 2);
    pag_tell(&h.a, FUNDS_OK, 0, false);
    pag_run(&h.a, 0);
    CHECK(pag_value(&h.a, PAY_OUT) == LPRES_STATE_FALSE, "false body: the only rule is false");
    CHECK(pag_decide(&h.a, PAY_OUT, false, 0, 0).act == LPX_ACT_REFUSE, "False refuses");
    CHECK(pag_value(&h.a, SHIP) == LPRES_STATE_FALSE, "and propagates through conjunction");
}

static void t_agent_rules(void)
{
    static agent_t g;
    /* x <- not y ; x <- weather  (two rules for one head) */
    pag_rule_t r[2] = {{NP_X, 1, {NP_Y, 0, 0, 0}, {true, false, false, false}},
                       {NP_X, 1, {WEATHER, 0, 0, 0}, {false, false, false, false}}};
    agent_init(&g, r, 2);
    pag_tell(&g.a, NP_Y, 0, true);
    pag_run(&g.a, 0);
    CHECK(pag_value(&g.a, NP_X) == LPRES_STATE_NEITHER,
          "one rule false, the other unknown: not told false");
    pag_tell(&g.a, WEATHER, 1, false);
    pag_run(&g.a, 0);
    CHECK(pag_value(&g.a, NP_X) == LPRES_STATE_FALSE, "every rule false: told false");
    pag_tell(&g.a, NP_X, 2, true);
    pag_run(&g.a, 0);
    CHECK(pag_value(&g.a, NP_X) == LPRES_STATE_BOTH, "a fact joins the derived value");
    /* Neither & Both is False in Belnap-Dunn */
    static agent_t h;
    pag_rule_t r2[1] = {{NP_X, 2, {FEE_OK, FUNDS_OK, 0, 0}, {false, false, false, false}}};
    agent_init(&h, r2, 1);
    pag_tell(&h.a, FUNDS_OK, 0, true);
    pag_tell(&h.a, FUNDS_OK, 1, false);
    pag_run(&h.a, 0);
    CHECK(pag_value(&h.a, NP_X) == LPRES_STATE_FALSE, "Neither & Both = False");
    CHECK(lpres_conjoin(LPRES_STATE_NEITHER, LPRES_STATE_BOTH) == LPRES_STATE_NEITHER,
          "lpres differs there, as documented");
    /* bad inputs */
    pag_rule_t badr[1] = {{NP, 1, {0, 0, 0, 0}, {false, false, false, false}}};
    CHECK(!agent_init(&h, badr, 1), "head out of range");
    badr[0].head = 0;
    badr[0].lit[0] = NP;
    CHECK(!agent_init(&h, badr, 1), "literal out of range");
    badr[0].lit[0] = 0;
    badr[0].n = 0;
    CHECK(!agent_init(&h, badr, 1), "empty body");
    badr[0].n = PAG_MAX_BODY + 1;
    CHECK(!agent_init(&h, badr, 1), "body too long");
    CHECK(!pag_init(&h.a, 0, h.ft, h.ff, h.t, h.f, h.p0, h.n0, 0, 0), "no propositions");
    CHECK(!pag_tell(&g.a, NP, 0, true) && !pag_tell(&g.a, 0, 64, true), "tell out of range");
    CHECK(pag_value(&g.a, NP) == LPRES_STATE_NEITHER, "value out of range");
    CHECK(pag_count(0) == 0 && pag_count(~(pag_set_t) 0) == 64 && pag_count(0x8001u) == 2, "count");
    /* a cycle: x <- y, y <- x reaches its fixed point */
    static agent_t c;
    pag_rule_t cy[2] = {{NP_X, 1, {NP_Y, 0, 0, 0}, {false, false, false, false}},
                        {NP_Y, 1, {NP_X, 0, 0, 0}, {false, false, false, false}}};
    agent_init(&c, cy, 2);
    pag_tell(&c.a, NP_X, 3, true);
    CHECK(pag_run(&c.a, 0) && pag_value(&c.a, NP_Y) == LPRES_STATE_TRUE && c.a.rounds <= 3,
          "cycle converges");
}

static void t_agent_self_contradiction(void)
{
    static agent_t g;
    pag_rule_t r[1] = {{PAY_OUT, 2, {FEE_OK, FUNDS_OK, 0, 0}, {false, false, false, false}}};
    agent_init(&g, r, 1);
    lpx_qrec_t qr[4];
    lpx_qlog_t q;
    lpx_qlog_init(&q, qr, 4);
    pag_tell(&g.a, FEE_OK, 9, true);
    pag_tell(&g.a, FUNDS_OK, 9, true);
    pag_tell(&g.a, FUNDS_OK, 9, false); /* source 9 says both */
    pag_tell(&g.a, FEE_OK, 2, true);
    pag_tell(&g.a, FUNDS_OK, 3, true);
    CHECK(pag_run(&g.a, &q), "runs");
    CHECK(g.a.bad == ((pag_set_t) 1 << 9), "source 9 is forbidden");
    CHECK(pag_value(&g.a, FUNDS_OK) == LPRES_STATE_TRUE, "its premises are masked out");
    CHECK(g.a.t[PAY_OUT] == 0xCu, "and never reach a conclusion");
    uint8_t b[18], subj[32];
    memcpy(b, "zxv/pag-bad/v1", 14);
    b[14] = 9;
    b[15] = b[16] = b[17] = 0;
    sha256(b, 18, subj);
    CHECK(q.n == 1 && qr[0].submitter == 9 && qr[0].reason == PAG_Q_SELF_CONTRADICTION &&
              lpx_forbidden(qr[0].tag) && !memcmp(qr[0].subject, subj, 32),
          "quarantined once");
    CHECK(pag_run(&g.a, &q) && q.n == 1, "not twice");
    /* fail closed when the record does not fit */
    static agent_t h;
    agent_init(&h, r, 1);
    lpx_qlog_init(&q, qr, 1);
    pag_tell(&h.a, FEE_OK, 1, true);
    pag_tell(&h.a, FEE_OK, 1, false);
    pag_tell(&h.a, FUNDS_OK, 2, true);
    pag_tell(&h.a, FUNDS_OK, 2, false);
    CHECK(!pag_run(&h.a, &q) && q.n == 0, "no room: fail closed");
}

/* independent reference: explicit four-valued Fitting iteration on tables */
static const uint8_t R_AND[4][4] = {/* N T F B (lpres numbering) */
                                    {0, 0, 2, 2},
                                    {0, 1, 2, 3},
                                    {2, 2, 2, 2},
                                    {2, 3, 2, 3}};
static const uint8_t R_OR[4][4] = {{0, 1, 0, 1}, {1, 1, 1, 1}, {0, 1, 2, 3}, {1, 1, 3, 3}};
static const uint8_t R_NOT[4] = {0, 2, 1, 3};

static void t_agent_reference(void)
{
    uint32_t agree = 0, trials = 1000, noexp = 0;
    for (uint32_t t = 1; t <= trials; t++) {
        rng = 0xA0761D6478BD642Full * t;
        static agent_t g, h;
        pag_rule_t r[6];
        uint32_t nr = 1 + rndn(6);
        for (uint32_t i = 0; i < nr; i++) {
            r[i].head = rndn(NP);
            r[i].n = (uint8_t) (1 + rndn(PAG_MAX_BODY));
            for (uint32_t k = 0; k < PAG_MAX_BODY; k++) {
                r[i].lit[k] = rndn(NP);
                r[i].neg[k] = rndn(3) == 0;
            }
        }
        agent_init(&g, r, nr);
        agent_init(&h, r, nr);
        uint8_t fact[NP] = {0};
        uint32_t nf = rndn(6);
        for (uint32_t i = 0; i < nf; i++) {
            uint32_t p = rndn(NP), s = 10 + i; /* each source speaks once: none is bad */
            bool v = rndn(2);
            pag_tell(&g.a, p, s, v);
            pag_tell(&h.a, p, s, v);
            fact[p] |= v ? 1u : 2u;
        }
        pag_run(&g.a, 0);
        uint8_t val[NP];
        for (uint32_t p = 0; p < NP; p++) val[p] = fact[p];
        for (uint32_t round = 0; round < 64; round++)
            for (uint32_t p = 0; p < NP; p++) {
                bool any = false;
                uint8_t d = 2; /* disjunction identity: False */
                for (uint32_t i = 0; i < nr; i++) {
                    if (r[i].head != p) continue;
                    uint8_t cj = 1; /* conjunction identity: True */
                    for (uint32_t k = 0; k < r[i].n; k++) {
                        uint8_t l = val[r[i].lit[k]];
                        cj = R_AND[cj][r[i].neg[k] ? R_NOT[l] : l];
                    }
                    d = any ? R_OR[d][cj] : cj;
                    any = true;
                }
                if (any) val[p] = (uint8_t) (fact[p] | d | val[p]);
            }
        bool same = true;
        for (uint32_t p = 0; p < NP; p++)
            if ((uint8_t) pag_value(&g.a, p) != val[p]) same = false;
        agree += same;
        /* no explosion: add p and not-p from two new sources; anything that
         * does not depend on p keeps its value */
        uint32_t p = rndn(NP);
        pag_tell(&h.a, p, 40, true);
        pag_tell(&h.a, p, 41, false);
        pag_run(&h.a, 0);
        bool dep[NP] = {false};
        dep[p] = true;
        for (uint32_t round = 0; round < NP; round++)
            for (uint32_t i = 0; i < nr; i++)
                for (uint32_t k = 0; k < r[i].n; k++)
                    if (dep[r[i].lit[k]]) dep[r[i].head] = true;
        bool ok = pag_value(&h.a, p) == LPRES_STATE_BOTH;
        for (uint32_t x = 0; x < NP; x++)
            if (!dep[x] && pag_value(&h.a, x) != pag_value(&g.a, x)) ok = false;
        noexp += ok;
    }
    CHECK(agree == trials, "support sets match the four-valued reference");
    CHECK(noexp == trials, "a contradiction touches only what depends on it");
}

static void t_agent_merge(void)
{
    uint32_t ok = 0, trials = 300;
    pag_rule_t r[2] = {{PAY_OUT, 2, {FEE_OK, FUNDS_OK, 0, 0}, {false, false, false, false}},
                       {SHIP, 2, {PAY_OUT, NOT_BLOCKED, 0, 0}, {false, true, false, false}}};
    for (uint32_t t = 1; t <= trials; t++) {
        rng = 0x2545F4914F6CDD1Dull * t;
        static agent_t a, b, c, ab, ba, abc, cab;
        agent_t *all[7] = {&a, &b, &c, &ab, &ba, &abc, &cab};
        for (uint32_t i = 0; i < 7; i++) agent_init(all[i], r, 2);
        for (uint32_t i = 0; i < 3; i++) {
            uint32_t n = rndn(8);
            for (uint32_t k = 0; k < n; k++) pag_tell(&all[i]->a, rndn(NP), rndn(16), rndn(2));
        }
        pag_merge(&ab.a, &a.a);
        pag_merge(&ab.a, &b.a);
        pag_merge(&ba.a, &b.a);
        pag_merge(&ba.a, &a.a);
        pag_merge(&ba.a, &a.a); /* idempotent */
        pag_merge(&abc.a, &ab.a);
        pag_merge(&abc.a, &c.a);
        pag_merge(&cab.a, &c.a);
        pag_merge(&cab.a, &ba.a);
        bool same = true;
        for (uint32_t i = 3; i < 7; i++) pag_run(&all[i]->a, 0);
        for (uint32_t p = 0; p < NP; p++)
            if (ab.a.t[p] != ba.a.t[p] || ab.a.f[p] != ba.a.f[p] || abc.a.t[p] != cab.a.t[p] ||
                abc.a.f[p] != cab.a.f[p])
                same = false;
        if (ab.a.bad != ba.a.bad || abc.a.bad != cab.a.bad) same = false;
        ok += same;
    }
    CHECK(ok == trials, "merge is commutative, associative and idempotent");
    static agent_t x, y;
    pag_set_t s1[3], s2[3], s3[3], s4[3];
    uint32_t u1[3], u2[3];
    pag_init(&y.a, 3, s1, s2, s3, s4, u1, u2, 0, 0);
    agent_init(&x, 0, 0);
    CHECK(!pag_merge(&x.a, &y.a), "sizes must match");
}

/* agents as ledger witnesses */
static void t_agents_witness(void)
{
    static node_t n;
    uint64_t g[3] = {100, 0, 0};
    plg_cfg_t c = base_cfg(&n, 3, g, 10);
    node_init(&n, &c, QCAP);
    plg_claim_t x = mk(1, 0, 1, 0, 40, 1, 1), y = mk(1, 0, 2, 0, 60, 1, 2);
    plg_claim(&n.lg, &x, true, true);
    plg_claim(&n.lg, &y, true, true);
    plg_advance(&n.lg, 1);
    CHECK(st_of(&n, &x) == PLG_CS_HELD && ref_conserved(&n), "dispute held");
    /* proposition 0: "x is the valid claim". Five agents, peers 0..9 report */
    static agent_t ag[5];
    uint32_t round_votes[2] = {0, 0};
    for (uint32_t round = 0; round < 2; round++) {
        for (uint32_t i = 0; i < 5; i++) {
            if (!round) agent_init(&ag[i], 0, 0);
            pag_snapshot(&ag[i].a);
            /* round 0: every agent hears both sides (Both). Round 1: agents 0-2
             * hear three more peers back x; 3-4 still see a split. */
            if (!round) {
                pag_tell(&ag[i].a, 0, 0, true);
                pag_tell(&ag[i].a, 0, 1, false);
            } else if (i < 3) {
                ag[i].a.ff[0] = 0; /* fresh view: peer 1 withdrew its report */
                pag_tell(&ag[i].a, 0, 2, true);
                pag_tell(&ag[i].a, 0, 3, true);
            }
            pag_run(&ag[i].a, 0);
            pag_decision_t d = pag_decide(&ag[i].a, 0, false, 0, 0);
            if (d.act == LPX_ACT_PROCEED && lpx_base(d.tag) == LPX_B_TRUE) {
                plg_vote_t v = vote(2 + round, PLG_V_WITNESS, i, &x, true);
                plg_vote(&n.lg, &v, true);
                round_votes[round]++;
            }
        }
        plg_advance(&n.lg, 2 + round);
        CHECK(ref_conserved(&n) && plg_conserved(&n.lg), "conserved after each round");
        if (!round)
            CHECK(round_votes[0] == 0 && st_of(&n, &x) == PLG_CS_HELD,
                  "a split swarm abstains: money stays in escrow");
    }
    CHECK(round_votes[1] == 3, "three agents became sure");
    CHECK(st_of(&n, &x) == PLG_CS_SETTLED && st_of(&n, &y) == PLG_CS_LOST, "x settles");
    CHECK(n.bal[0] == 60 && n.bal[1] == 40 && n.bal[2] == 0, "one spend, never both");
}

/* ---- byte-level and boundary checks (mutation gate) ---- */
static void le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}
static void le64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

/* independent restatement of the state digest layout */
static void ref_digest(const node_t *n, uint8_t out[32])
{
    static uint8_t b[16 + 8 + 8 * NACC + 8 + 4 + CAP * 33 + 4 + VCAP * 42];
    uint32_t k = 0;
    memcpy(b, "zxv/plg-state/v1", 16);
    k = 16;
    le32(b + k, n->lg.horizon);
    k += 4;
    le32(b + k, n->lg.cfg.n_acct);
    k += 4;
    for (uint32_t a = 0; a < n->lg.cfg.n_acct; a++, k += 8) le64(b + k, n->bal[a]);
    le64(b + k, n->lg.escrow);
    k += 8;
    le32(b + k, n->lg.n_cl);
    k += 4;
    for (uint32_t i = 0; i < n->lg.n_cl; i++) {
        memcpy(b + k, n->lg.cl[i].c.id, 32);
        b[k + 32] = n->lg.cl[i].st;
        k += 33;
    }
    le32(b + k, n->lg.n_vt);
    k += 4;
    for (uint32_t i = 0; i < n->lg.n_vt; i++) {
        memcpy(b + k, n->lg.vt[i].claim, 32);
        le32(b + k + 32, n->lg.vt[i].epoch);
        le32(b + k + 36, n->lg.vt[i].voter);
        b[k + 40] = n->lg.vt[i].kind;
        b[k + 41] = n->lg.vt[i].positive;
        k += 42;
    }
    sha256(b, k, out);
}

static bool sorted(const node_t *n)
{
    for (uint32_t i = 1; i < n->lg.n_cl; i++) {
        const plg_claim_t *a = &n->lg.cl[i - 1].c, *b = &n->lg.cl[i].c;
        int c = a->epoch != b->epoch   ? (a->epoch < b->epoch ? -1 : 1)
                : a->from != b->from   ? (a->from < b->from ? -1 : 1)
                : a->nonce != b->nonce ? (a->nonce < b->nonce ? -1 : 1)
                                       : memcmp(a->id, b->id, 32);
        if (c >= 0) return false;
    }
    for (uint32_t i = 1; i < n->lg.n_vt; i++) {
        const plg_vote_t *a = &n->lg.vt[i - 1], *b = &n->lg.vt[i];
        int c = a->epoch != b->epoch             ? (a->epoch < b->epoch ? -1 : 1)
                : a->kind != b->kind             ? (a->kind < b->kind ? -1 : 1)
                : a->voter != b->voter           ? (a->voter < b->voter ? -1 : 1)
                : memcmp(a->claim, b->claim, 32) ? memcmp(a->claim, b->claim, 32)
                                                 : (int) a->positive - (int) b->positive;
        if (c >= 0) return false;
    }
    return true;
}

static void t_bytes(void)
{
    static node_t n;
    rng = 0x5DEECE66Dull;
    uint64_t g[4] = {300, 7, 0, 1ull << 60};
    plg_cfg_t c = base_cfg(&n, 4, g, 3);
    node_init(&n, &c, QCAP);
    uint8_t d[32];
    const uint32_t E0 = 0x01020300u; /* epochs past 2^24: every byte is hashed */
    ref_digest(&n, d);
    CHECK(!memcmp(d, n.lg.digest, 32), "genesis digest layout");
    for (uint32_t k = 0; k < 40; k++) {
        uint32_t from = rndn(4), to = (from + 1 + rndn(3)) % 4;
        plg_claim_t cl = mk(E0 + 1 + rndn(4), from, to, rndn(3), 1 + rndn(90), rndn(3), 500 + k);
        plg_claim(&n.lg, &cl, true, false);
        if (k % 3 == 0) {
            plg_vote_t v = vote(E0 + 1 + rndn(4), (uint8_t) rndn(3), 0, &cl, true);
            v.voter = v.kind == PLG_V_WITNESS ? rndn(5) : v.kind == PLG_V_ARBITER ? 100 : 200;
            v.positive = v.kind != PLG_V_WITNESS || rndn(2);
            plg_vote(&n.lg, &v, true);
        }
    }
    CHECK(sorted(&n), "claims and votes in canonical order");
    CHECK(n.lg.n_cl == 40 && n.lg.n_vt > 5, "events stored");
    plg_advance(&n.lg, E0 + 2);
    ref_digest(&n, d);
    CHECK(!memcmp(d, n.lg.digest, 32), "digest layout after advance");
    plg_advance(&n.lg, E0 + 4);
    ref_digest(&n, d);
    CHECK(!memcmp(d, n.lg.digest, 32) && ref_conserved(&n), "digest layout at the end");
    const pdx_rec_t *r = &n.rec[n.log.n - 1];
    CHECK(r->op == PLG_OP_ADVANCE && r->outcome == PLG_OK && r->aux == E0 + 4 &&
              !memcmp(r->b, n.q.head, 32) && lpx_base(r->tag) == LPX_B_TRUE,
          "advance record binds the quarantine head");
    /* claim and vote records */
    static node_t m;
    c = base_cfg(&m, 4, g, 3);
    node_init(&m, &c, QCAP);
    plg_claim_t a = mk(3, 0, 1, 0, 5, 1, 1), b = mk(3, 0, 2, 0, 6, 1, 2);
    plg_claim(&m.lg, &a, true, true);
    plg_claim(&m.lg, &b, true, true);
    CHECK(m.rec[0].op == PLG_OP_CLAIM && m.rec[0].aux == 3 && !memcmp(m.rec[0].a, a.id, 32) &&
              m.rec[0].outcome == PLG_ADDED && m.rec[0].kind == PDX_KIND_NONE,
          "claim record");
    CHECK(m.rec[1].outcome == PLG_ADDED_BOTH && m.rec[1].kind == PDX_KIND_NEUTRAL, "Both record");
    plg_vote_t v = vote(4, PLG_V_WITNESS, 3, &a, false);
    plg_vote(&m.lg, &v, true);
    CHECK(m.rec[2].op == PLG_OP_VOTE && m.rec[2].aux == 4 && !memcmp(m.rec[2].a, a.id, 32),
          "vote record");
    CHECK(m.log.n == 3 && pdx_log_verify(&m.log), "three records");
}

static void t_boundaries(void)
{
    uint64_t g[3] = {100, 0, 0};
    /* lead exactly the margin settles; one less does not */
    static node_t n;
    plg_cfg_t c = base_cfg(&n, 3, g, 0);
    c.decisive = 3;
    node_init(&n, &c, QCAP);
    plg_claim_t x = mk(1, 0, 1, 0, 10, 1, 1), y = mk(1, 0, 2, 0, 10, 1, 2);
    plg_claim(&n.lg, &x, true, true);
    plg_claim(&n.lg, &y, true, true);
    plg_vote_t v0 = vote(2, PLG_V_WITNESS, 0, &x, true), v1 = vote(2, PLG_V_WITNESS, 1, &x, true);
    plg_vote(&n.lg, &v0, true);
    plg_vote(&n.lg, &v1, true);
    plg_advance(&n.lg, 2);
    CHECK(st_of(&n, &x) == PLG_CS_HELD, "lead 2 of 3: held");
    /* witness 2 first says no to x, then yes: only its first vote counts */
    plg_vote_t wn = vote(3, PLG_V_WITNESS, 2, &x, false), wp = vote(4, PLG_V_WITNESS, 2, &x, true);
    plg_vote(&n.lg, &wn, true);
    plg_vote(&n.lg, &wp, true);
    plg_advance(&n.lg, 4);
    CHECK(st_of(&n, &x) == PLG_CS_HELD, "a witness's later vote does not count again");
    plg_vote_t v3 = vote(5, PLG_V_WITNESS, 3, &y, false), v4 = vote(5, PLG_V_WITNESS, 4, &x, true);
    plg_vote(&n.lg, &v3, true);
    plg_vote(&n.lg, &v4, true);
    plg_advance(&n.lg, 5);
    CHECK(st_of(&n, &x) == PLG_CS_SETTLED, "lead exactly 3: settled");
    /* the unique lowest seq among a tie of the others */
    static node_t m;
    c = base_cfg(&m, 3, g, 0);
    c.first_sig_rule = true;
    node_init(&m, &c, QCAP);
    plg_claim_t a = mk(1, 0, 1, 0, 10, 5, 1), b = mk(1, 0, 2, 0, 10, 5, 2),
                d = mk(1, 0, 1, 0, 11, 1, 3);
    plg_claim(&m.lg, &a, true, true);
    plg_claim(&m.lg, &b, true, true);
    plg_claim(&m.lg, &d, true, true);
    plg_advance(&m.lg, 1);
    CHECK(st_of(&m, &d) == PLG_CS_SETTLED, "unique minimum after a tie wins");
    plg_claim_t e = mk(2, 0, 1, 1, 10, 1, 1), f = mk(2, 0, 2, 1, 10, 5, 2),
                h = mk(2, 0, 1, 1, 11, 1, 3);
    plg_claim(&m.lg, &e, true, true);
    plg_claim(&m.lg, &f, true, true);
    plg_claim(&m.lg, &h, true, true);
    plg_advance(&m.lg, 2);
    CHECK(st_of(&m, &e) == PLG_CS_HELD, "a tie at the minimum stays held");
    /* exact funding: amount - escrow == balance */
    static node_t k;
    uint64_t g2[3] = {30, 0, 0};
    c = base_cfg(&k, 3, g2, 0);
    node_init(&k, &c, QCAP);
    plg_claim_t s = mk(1, 0, 1, 0, 10, 1, 1), t = mk(1, 0, 2, 0, 20, 1, 2);
    plg_claim(&k.lg, &s, true, true);
    plg_claim(&k.lg, &t, true, true);
    plg_advance(&k.lg, 1);
    plg_claim_t u = mk(2, 0, 1, 0, 30, 1, 3);
    plg_claim(&k.lg, &u, true, true);
    plg_vote_t o = vote(2, PLG_V_OWNER, 200, &u, true);
    plg_vote(&k.lg, &o, true);
    plg_advance(&k.lg, 2);
    CHECK(st_of(&k, &u) == PLG_CS_SETTLED && k.bal[0] == 0 && k.bal[1] == 30, "exact funding");
    static node_t z;
    node_init(&z, &c, QCAP);
    plg_claim_t w = mk(1, 0, 1, 0, 30, 1, 1);
    plg_claim(&z.lg, &w, true, true);
    plg_advance(&z.lg, 1);
    CHECK(st_of(&z, &w) == PLG_CS_SETTLED && z.bal[0] == 0, "spend the whole balance");
    /* the local check counts the largest claim of each other pending slot */
    static node_t l;
    uint64_t g3[3] = {100, 0, 0};
    c = base_cfg(&l, 3, g3, 0);
    node_init(&l, &c, QCAP);
    plg_claim_t p1 = mk(1, 0, 1, 0, 30, 1, 1), p2 = mk(1, 0, 2, 0, 50, 1, 2);
    plg_claim(&l.lg, &p1, true, true);
    plg_claim(&l.lg, &p2, true, true);
    plg_claim_t q1 = mk(1, 0, 1, 1, 50, 1, 1), q2 = mk(1, 0, 1, 1, 51, 1, 2);
    CHECK(plg_claim(&l.lg, &q2, true, true) == PLG_REFUSED_FUNDS, "51 over the 50 held back");
    CHECK(plg_claim(&l.lg, &q1, true, true) == PLG_ADDED, "50 fits: max, not sum");
    CHECK(plg_claim(&l.lg, &q2, true, true) == PLG_ADDED_BOTH, "conflicts skip the check");
    plg_advance(&l.lg, 1);
    plg_claim_t r1 = mk(2, 0, 2, 2, 1, 1, 1);
    CHECK(l.bal[0] == 0 && plg_claim(&l.lg, &r1, true, true) == PLG_REFUSED_FUNDS,
          "final balance after holds");
    static node_t f2;
    uint64_t g4[3] = {100, 0, 0};
    c = base_cfg(&f2, 3, g4, 0);
    node_init(&f2, &c, QCAP);
    plg_claim_t s1 = mk(1, 0, 1, 0, 40, 1, 1);
    plg_claim(&f2.lg, &s1, true, true);
    plg_advance(&f2.lg, 1);
    plg_claim_t s2 = mk(2, 0, 1, 1, 60, 1, 1);
    CHECK(plg_claim(&f2.lg, &s2, true, true) == PLG_ADDED, "settled slots are not pending");
}

static void t_equivocation_pairs(void)
{
    /* three claims on one slot; every LOST one is paired with the head, the
     * head with the next claim */
    static node_t n;
    uint64_t g[3] = {100, 0, 0};
    plg_cfg_t c = base_cfg(&n, 3, g, 0);
    node_init(&n, &c, QCAP);
    plg_claim_t a = mk(1, 0, 1, 0, 10, 1, 1), b = mk(1, 0, 2, 0, 10, 1, 2),
                d = mk(1, 0, 1, 0, 10, 1, 3);
    plg_claim(&n.lg, &a, true, true);
    plg_claim(&n.lg, &b, true, true);
    plg_claim(&n.lg, &d, true, true);
    const uint8_t *h = n.lg.cl[0].c.id, *s = n.lg.cl[1].c.id, *t = n.lg.cl[2].c.id;
    plg_vote_t o = vote(2, PLG_V_OWNER, 200, &n.lg.cl[2].c, true); /* the last one wins */
    plg_vote(&n.lg, &o, true);
    plg_advance(&n.lg, 2);
    CHECK(n.lg.cl[2].st == PLG_CS_SETTLED && n.q.n == 2, "two lost, two records");
    uint8_t buf[80], e1[32], e2[32];
    memcpy(buf, "zxv/plg-equiv/v1", 16);
    memcpy(buf + 16, s, 32); /* head lost: paired with the next */
    memcpy(buf + 48, h, 32);
    sha256(buf, 80, e1);
    memcpy(buf + 16, h, 32);
    memcpy(buf + 48, s, 32);
    sha256(buf, 80, e2);
    CHECK(!memcmp(n.qrec[0].subject, e1, 32) && !memcmp(n.qrec[1].subject, e2, 32),
          "pair subjects");
    CHECK(lpx_base(n.qrec[0].tag) == LPX_B_FALSE && lpx_forbidden(n.qrec[0].tag) &&
              lpx_phase(n.qrec[0].tag) == LPX_PH_MANIFEST,
          "lost tag, forbidden");
    (void) t;
}

static void t_nulls_and_names(void)
{
    static node_t n;
    uint64_t g[2] = {10, 0};
    plg_cfg_t c = base_cfg(&n, 2, g, 0);
    plg_entry_t *cl = n.cl;
    CHECK(!plg_init(&n.lg, 0, cl, CAP, n.vt, VCAP, n.bal, n.hold, &n.log, &n.q), "null cfg");
    CHECK(!plg_init(&n.lg, &c, 0, CAP, n.vt, VCAP, n.bal, n.hold, &n.log, &n.q), "null claims");
    CHECK(!plg_init(&n.lg, &c, cl, CAP, 0, VCAP, n.bal, n.hold, &n.log, &n.q), "null votes");
    CHECK(!plg_init(&n.lg, &c, cl, CAP, n.vt, VCAP, 0, n.hold, &n.log, &n.q), "null balances");
    CHECK(!plg_init(&n.lg, &c, cl, CAP, n.vt, VCAP, n.bal, 0, &n.log, &n.q), "null holds");
    CHECK(!plg_init(&n.lg, &c, cl, CAP, n.vt, VCAP, n.bal, n.hold, 0, &n.q), "null log");
    plg_cfg_t b = c;
    b.genesis = 0;
    CHECK(!plg_init(&n.lg, &b, cl, CAP, n.vt, VCAP, n.bal, n.hold, &n.log, &n.q), "null genesis");
    node_init(&n, &c, QCAP);
    plg_claim_t x = mk(1, 0, 1, 0, 1, 1, 0);
    plg_vote_t v = vote(1, PLG_V_WITNESS, 0, &x, true);
    uint64_t l = 7;
    CHECK(plg_find(0, x.id) == -1 && plg_find(&n.lg, 0) == -1, "find null");
    CHECK(plg_claim(0, &x, true, true) == PLG_REFUSED_INVALID &&
              plg_claim(&n.lg, 0, true, true) == PLG_REFUSED_INVALID,
          "claim null");
    CHECK(plg_vote(0, &v, true) == PLG_REFUSED_INVALID &&
              plg_vote(&n.lg, 0, true) == PLG_REFUSED_INVALID,
          "vote null");
    CHECK(plg_merge(0, &n.lg, 0, 0) == PLG_REFUSED_INVALID &&
              plg_merge(&n.lg, 0, 0, 0) == PLG_REFUSED_INVALID,
          "merge null");
    CHECK(plg_advance(0, 1) == PLG_REFUSED_INVALID, "advance null");
    CHECK(!plg_liabilities(0, &l) && !plg_liabilities(&n.lg, 0) && l == 7, "liabilities null");
    CHECK(plg_liabilities(&n.lg, &l) && l == 10, "liabilities");
    /* merge counters and stop conditions */
    static node_t m;
    node_init(&m, &c, QCAP);
    plg_claim(&n.lg, &x, true, true);
    plg_vote(&n.lg, &v, true);
    uint32_t added = 9, late = 9;
    CHECK(plg_merge(&m.lg, &n.lg, &added, &late) == PLG_OK && added == 2 && late == 0, "merged");
    CHECK(plg_merge(&m.lg, &n.lg, &added, &late) == PLG_OK && added == 0 && late == 0, "again");
    static node_t k;
    node_init(&k, &c, QCAP);
    plg_advance(&k.lg, 1);
    CHECK(plg_merge(&k.lg, &n.lg, &added, &late) == PLG_OK && added == 0 && late == 2,
          "late events counted");
    k.lg.halted = true;
    CHECK(plg_merge(&k.lg, &n.lg, 0, 0) == PLG_HALTED, "halted merge");
    static node_t f;
    pdx_log_init(&f.log, f.rec, LCAP);
    lpx_qlog_init(&f.q, f.qrec, QCAP);
    plg_init(&f.lg, &c, f.cl, 0, f.vt, VCAP, f.bal, f.hold, &f.log, &f.q);
    CHECK(plg_merge(&f.lg, &n.lg, 0, 0) == PLG_FULL, "full merge");
    plg_init(&f.lg, &c, f.cl, CAP, f.vt, 0, f.bal, f.hold, &f.log, &f.q);
    CHECK(plg_merge(&f.lg, &n.lg, &added, 0) == PLG_FULL && added == 1, "full on votes");
    /* names and tags */
    static const char *const sn[6] = {"pending", "settled", "lost",
                                      "held",    "frozen",  "insufficient"};
    bool ok = !strcmp(plg_state_name(6), "?");
    for (uint8_t s = 0; s < 6; s++) ok = ok && !strcmp(plg_state_name(s), sn[s]);
    CHECK(ok, "every state name");
    static const char *const rn[10] = {"added",           "added-both",   "duplicate",     "ok",
                                       "refused-invalid", "refused-late", "refused-funds", "full",
                                       "halted",          "forbidden"};
    ok = !strcmp(plg_res_name((plg_res_t) 10), "?");
    for (int r = 0; r < 10; r++) ok = ok && !strcmp(plg_res_name((plg_res_t) r), rn[r]);
    CHECK(ok, "every result name");
    const lpx_t want[6] = {lpx_make(LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_PH_EMERGING),
                           lpx_make(LPX_B_TRUE, LPX_P_NEUTRAL, LPX_PH_MANIFEST),
                           lpx_make(LPX_B_FALSE, LPX_P_NEUTRAL, LPX_PH_MANIFEST),
                           lpx_make(LPX_B_BOTH, LPX_P_NEUTRAL, LPX_PH_MANIFEST),
                           lpx_make(LPX_B_BOTH, LPX_P_ENTROPIC, LPX_PH_MANIFEST),
                           lpx_make(LPX_B_FALSE, LPX_P_FORBIDDEN, LPX_PH_MANIFEST)};
    ok = plg_tag(6) == lpx_make(LPX_B_NEITHER, LPX_P_FORBIDDEN, LPX_PH_POTENTIAL);
    for (uint8_t s = 0; s < 6; s++) ok = ok && plg_tag(s) == want[s];
    CHECK(ok, "every tag");
}

static void t_agent_edges(void)
{
    /* exact-size heap arrays so ASan sees any step past the end */
    pag_set_t *ft = malloc(3 * sizeof *ft), *ff = malloc(3 * sizeof *ff),
              *t = malloc(3 * sizeof *t), *f = malloc(3 * sizeof *f);
    uint32_t *p0 = malloc(3 * sizeof *p0), *n0 = malloc(3 * sizeof *n0);
    for (int i = 0; i < 3; i++) {
        ft[i] = ff[i] = t[i] = f[i] = 0xFF;
        p0[i] = n0[i] = 9;
    }
    pag_rule_t r[2] = {{0, 1, {1, 0, 0, 0}, {false, false, false, false}},
                       {1, 1, {2, 0, 0, 0}, {false, false, false, false}}};
    pag_agent_t a, b;
    CHECK(!pag_init(&a, 3, ft, ff, t, f, p0, n0, 0, 2), "rules missing");
    CHECK(!pag_init(0, 3, ft, ff, t, f, p0, n0, r, 2), "null agent");
    CHECK(!pag_init(&a, 3, 0, ff, t, f, p0, n0, r, 2) &&
              !pag_init(&a, 3, ft, 0, t, f, p0, n0, r, 2) &&
              !pag_init(&a, 3, ft, ff, 0, f, p0, n0, r, 2) &&
              !pag_init(&a, 3, ft, ff, t, 0, p0, n0, r, 2) &&
              !pag_init(&a, 3, ft, ff, t, f, 0, n0, r, 2) &&
              !pag_init(&a, 3, ft, ff, t, f, p0, 0, r, 2),
          "null arrays");
    a.bad = a.qlogged = 5;
    a.rounds = 5;
    CHECK(pag_init(&a, 3, ft, ff, t, f, p0, n0, r, 2), "init");
    bool clean = !a.bad && !a.qlogged && !a.rounds;
    for (int i = 0; i < 3; i++)
        clean = clean && !ft[i] && !ff[i] && !t[i] && !f[i] && !p0[i] && !n0[i];
    CHECK(clean, "init clears everything");
    CHECK(pag_tell(&a, 2, 63, true) && pag_tell(&a, 0, 1, false), "tell returns true");
    /* chain 2 -> 1 -> 0, against the scan order: one step per round */
    CHECK(pag_run(&a, 0) && a.rounds == 3, "rounds counted exactly");
    CHECK(pag_value(&a, 0) == LPRES_STATE_BOTH && a.t[0] == (pag_set_t) 1 << 63, "chain end");
    pag_snapshot(&a);
    CHECK(p0[0] == 1 && n0[0] == 1 && p0[2] == 1 && n0[2] == 0, "snapshot counts every prop");
    pag_snapshot(0);
    CHECK(pag_run(0, 0) == false, "run null");
    CHECK(pag_value(0, 0) == LPRES_STATE_NEITHER && pag_value(&a, 3) == LPRES_STATE_NEITHER,
          "value null / past the end");
    pag_decision_t d = pag_decide(0, 0, false, 0, 0);
    CHECK(d.pos == 0 && d.neg == 0 && d.act == LPX_ACT_WAIT, "decide null");
    d = pag_decide(&a, 3, false, 0, 0);
    CHECK(d.pos == 0 && d.neg == 0 && d.act == LPX_ACT_WAIT, "decide past the end");
    d = pag_decide(&a, 2, false, 0, 0);
    CHECK(d.pos == 1 && d.neg == 0 && d.act == LPX_ACT_PROCEED, "decide last prop");
    /* merge returns true and reaches the last prop */
    pag_set_t bt[3], bf[3], bt2[3], bf2[3];
    uint32_t bp[3], bn[3];
    pag_init(&b, 3, bt, bf, bt2, bf2, bp, bn, 0, 0);
    pag_tell(&b, 2, 7, true);
    CHECK(pag_merge(&a, &b) && ft[2] == (((pag_set_t) 1 << 63) | ((pag_set_t) 1 << 7)),
          "merge reaches the last prop");
    CHECK(!pag_merge(0, &b) && !pag_merge(&a, 0), "merge null");
    /* quarantine exactly fits, and a corrupt log refuses */
    lpx_qrec_t qr[2];
    lpx_qlog_t q;
    lpx_qlog_init(&q, qr, 2);
    pag_tell(&a, 0, 4, true);
    pag_tell(&a, 0, 4, false);
    pag_tell(&a, 1, 5, true);
    pag_tell(&a, 1, 5, false);
    CHECK(pag_run(&a, &q) && q.n == 2 && qr[0].submitter == 4 && qr[1].submitter == 5,
          "two records fit exactly");
    lpx_qlog_init(&q, qr, 2);
    q.n = 3;
    pag_tell(&a, 2, 6, true);
    pag_tell(&a, 2, 6, false);
    CHECK(!pag_run(&a, &q), "a log past its capacity refuses");
    CHECK(pag_run(&a, 0) && a.bad == ((pag_set_t) 0x70), "no log: still masked");
    free(ft);
    free(ff);
    free(t);
    free(f);
    free(p0);
    free(n0);
}

static void t_more_edges(void)
{
    /* genesis sum exactly at the top of uint64 */
    static node_t n;
    uint64_t top[2] = {UINT64_MAX - 1, 1};
    plg_cfg_t c = base_cfg(&n, 2, top, 0);
    CHECK(node_init(&n, &c, QCAP), "sum == UINT64_MAX accepted");
    /* tampered genesis with no events at all is still caught */
    static node_t m;
    uint64_t g[3] = {100, 50, 0};
    c = base_cfg(&m, 3, g, 0);
    node_init(&m, &c, QCAP);
    m.gen[0] = 99;
    CHECK(plg_advance(&m.lg, 5) == PLG_FORBIDDEN && m.qrec[0].submitter == 0 &&
              m.qrec[0].reason == PLG_Q_CONSERVATION,
          "forbidden with an empty replay");
    /* liabilities: each failure path directly */
    static node_t k;
    c = base_cfg(&k, 3, g, 0);
    node_init(&k, &c, QCAP);
    uint64_t l;
    plg_claim_t x = mk(1, 0, 1, 0, 5, 1, 0);
    plg_claim(&k.lg, &x, true, true);
    CHECK(k.hold[0] == 0 && plg_conserved(&k.lg), "a new claim holds nothing");
    k.hold[0] = UINT64_MAX;
    k.lg.escrow = UINT64_MAX;
    CHECK(!plg_liabilities(&k.lg, &l), "balances plus a huge hold overflow");
    k.hold[0] = 1;
    k.lg.escrow = 2;
    CHECK(!plg_liabilities(&k.lg, &l), "holds must add up to escrow");
    k.hold[0] = 0;
    k.lg.escrow = 0;
    /* a three-step chain inside one epoch: 2 -> 1 -> 0 -> nobody needs three passes */
    static node_t h;
    uint64_t g2[4] = {0, 0, 0, 90};
    c = base_cfg(&h, 4, g2, 0);
    node_init(&h, &c, QCAP);
    plg_claim_t c0 = mk(1, 0, 3, 0, 10, 1, 0), c1 = mk(1, 1, 0, 0, 20, 1, 0),
                c2 = mk(1, 2, 1, 0, 30, 1, 0), c3 = mk(1, 3, 2, 0, 40, 1, 0);
    plg_claim(&h.lg, &c0, true, false);
    plg_claim(&h.lg, &c1, true, false);
    plg_claim(&h.lg, &c2, true, false);
    plg_claim(&h.lg, &c3, true, false);
    plg_advance(&h.lg, 1);
    CHECK(st_of(&h, &c0) == PLG_CS_SETTLED && st_of(&h, &c1) == PLG_CS_SETTLED &&
              st_of(&h, &c2) == PLG_CS_SETTLED,
          "long chain settles");
    CHECK(h.bal[0] == 10 && h.bal[1] == 10 && h.bal[2] == 10 && h.bal[3] == 60, "chain balances");
    /* a dispute funded later in the same epoch holds in full (no rule) */
    static node_t f;
    uint64_t g3[3] = {0, 50, 0};
    c = base_cfg(&f, 3, g3, 0);
    node_init(&f, &c, QCAP);
    plg_claim_t d1 = mk(1, 0, 2, 0, 20, 1, 1), d2 = mk(1, 0, 2, 0, 30, 1, 2),
                in = mk(1, 1, 0, 0, 50, 1, 0);
    plg_claim(&f.lg, &d1, true, false);
    plg_claim(&f.lg, &d2, true, false);
    plg_claim(&f.lg, &in, true, false);
    plg_advance(&f.lg, 1);
    CHECK(f.lg.escrow == 30 && f.bal[0] == 20 && st_of(&f, &d1) == PLG_CS_HELD,
          "income later in the epoch fills the hold");
    /* and with the rule, the winner is paid from that income */
    static node_t fr;
    c.first_sig_rule = true;
    node_init(&fr, &c, QCAP);
    plg_claim_t e1 = mk(1, 0, 2, 0, 20, 1, 1), e2 = mk(1, 0, 2, 0, 30, 2, 2);
    plg_claim(&fr.lg, &e1, true, false);
    plg_claim(&fr.lg, &e2, true, false);
    plg_claim(&fr.lg, &in, true, false);
    plg_advance(&fr.lg, 1);
    CHECK(st_of(&fr, &e1) == PLG_CS_SETTLED && fr.bal[0] == 30 && fr.bal[2] == 20,
          "rule winner waits for same-epoch income");
    /* the winner needs more than escrow; the balance has exactly the rest */
    static node_t w;
    uint64_t g4[3] = {10, 5, 0};
    c = base_cfg(&w, 3, g4, 0);
    node_init(&w, &c, QCAP);
    plg_claim_t s1 = mk(1, 0, 2, 0, 8, 1, 1), s2 = mk(1, 0, 2, 0, 15, 1, 2);
    plg_claim(&w.lg, &s1, true, false);
    plg_claim(&w.lg, &s2, true, false);
    plg_advance(&w.lg, 1);
    CHECK(w.lg.escrow == 10 && w.bal[0] == 0, "partial hold");
    plg_claim_t gift = mk(2, 1, 0, 0, 5, 1, 0);
    plg_claim(&w.lg, &gift, true, true);
    plg_vote_t o = vote(2, PLG_V_OWNER, 200, &s2, true);
    plg_vote(&w.lg, &o, true);
    plg_advance(&w.lg, 2);
    CHECK(st_of(&w, &s2) == PLG_CS_SETTLED && w.bal[0] == 0 && w.bal[2] == 15 && w.lg.escrow == 0 &&
              ref_conserved(&w),
          "escrow plus the exact rest");
    /* a ruling in the same epoch as the dispute, on a slot that is not first */
    static node_t r;
    uint64_t g5[3] = {100, 100, 0};
    c = base_cfg(&r, 3, g5, 0);
    node_init(&r, &c, QCAP);
    plg_claim_t first = mk(1, 0, 2, 0, 1, 1, 0);
    plg_claim_t t1 = mk(1, 1, 2, 0, 10, 1, 1), t2 = mk(1, 1, 0, 0, 20, 1, 2);
    plg_claim(&r.lg, &first, true, true);
    plg_claim(&r.lg, &t1, true, true);
    plg_claim(&r.lg, &t2, true, true);
    plg_vote_t a1 = vote(1, PLG_V_ARBITER, 100, &t2, true);
    plg_vote(&r.lg, &a1, true);
    plg_advance(&r.lg, 1);
    CHECK(st_of(&r, &t2) == PLG_CS_SETTLED && r.bal[1] == 80, "same-epoch ruling, second slot");
    /* a witness's repeat vote is ignored even when it is not the first vote */
    static node_t v;
    c = base_cfg(&v, 3, g5, 0);
    c.decisive = 1;
    node_init(&v, &c, QCAP);
    plg_claim_t x1 = mk(1, 1, 2, 0, 10, 1, 1), x2 = mk(1, 1, 0, 0, 10, 1, 2),
                other = mk(1, 0, 2, 0, 1, 1, 0);
    plg_claim(&v.lg, &x1, true, true);
    plg_claim(&v.lg, &x2, true, true);
    plg_claim(&v.lg, &other, true, true);
    plg_vote_t w0 = vote(2, PLG_V_WITNESS, 0, &other, true),
               w1n = vote(2, PLG_V_WITNESS, 1, &x1, false),
               w2n = vote(2, PLG_V_WITNESS, 2, &x2, false),
               w1p = vote(3, PLG_V_WITNESS, 1, &x1, true);
    plg_vote(&v.lg, &w0, true);
    plg_vote(&v.lg, &w1n, true);
    plg_vote(&v.lg, &w2n, true);
    plg_vote(&v.lg, &w1p, true);
    plg_advance(&v.lg, 3);
    CHECK(st_of(&v, &x1) == PLG_CS_HELD, "repeat ignored: still a tie");
    /* the local check reads only the slot's own claims */
    static node_t lc;
    c = base_cfg(&lc, 3, g5, 0);
    node_init(&lc, &c, QCAP);
    plg_claim_t pa = mk(1, 0, 2, 0, 10, 1, 1), pb = mk(1, 1, 2, 0, 90, 1, 1);
    plg_claim(&lc.lg, &pa, true, true);
    plg_claim(&lc.lg, &pb, true, true);
    plg_claim_t pc = mk(1, 0, 2, 1, 90, 1, 1);
    CHECK(plg_claim(&lc.lg, &pc, true, true) == PLG_ADDED, "another sender's claim not counted");
    /* the quarantine log exactly fits, or is one short */
    static node_t qf;
    uint64_t g6[2] = {0, 0};
    c = base_cfg(&qf, 2, g6, 0);
    node_init(&qf, &c, 2);
    plg_claim_t i1 = mk(1, 0, 1, 0, 1, 1, 0), i2 = mk(1, 0, 1, 1, 1, 1, 0);
    plg_claim(&qf.lg, &i1, true, false);
    plg_claim(&qf.lg, &i2, true, false);
    CHECK(plg_advance(&qf.lg, 1) == PLG_OK && qf.q.n == 2, "two records fit exactly");
    static node_t qs;
    node_init(&qs, &c, 1);
    plg_claim(&qs.lg, &i1, true, false);
    plg_claim(&qs.lg, &i2, true, false);
    CHECK(plg_advance(&qs.lg, 1) == PLG_FULL, "one short halts");
    /* a rule-settled dispute funds a chain scanned before it, in one epoch */
    static node_t rf;
    uint64_t g7[4] = {0, 0, 30, 0};
    c = base_cfg(&rf, 4, g7, 0);
    c.first_sig_rule = true;
    node_init(&rf, &c, QCAP);
    plg_claim_t cx = mk(1, 0, 3, 0, 10, 1, 0), cw = mk(1, 1, 0, 0, 15, 1, 0),
                r1 = mk(1, 2, 1, 0, 20, 1, 1), r2 = mk(1, 2, 3, 0, 25, 2, 2);
    plg_claim(&rf.lg, &cx, true, false);
    plg_claim(&rf.lg, &cw, true, false);
    plg_claim(&rf.lg, &r1, true, false);
    plg_claim(&rf.lg, &r2, true, false);
    plg_advance(&rf.lg, 1);
    CHECK(st_of(&rf, &r1) == PLG_CS_SETTLED && st_of(&rf, &cw) == PLG_CS_SETTLED &&
              st_of(&rf, &cx) == PLG_CS_SETTLED && rf.bal[3] == 10,
          "a settle makes room for another pass");
    /* agents: a bad second rule or literal is caught */
    static agent_t ag;
    pag_rule_t rr[2] = {{0, 1, {1, 0, 0, 0}, {false, false, false, false}},
                        {NP, 1, {1, 0, 0, 0}, {false, false, false, false}}};
    CHECK(!agent_init(&ag, rr, 2), "second rule checked");
    rr[1].head = 0;
    rr[1].n = 2;
    rr[1].lit[1] = NP;
    CHECK(!agent_init(&ag, rr, 2), "second literal checked");
    /* merge reaches prop 0; a source 0 that contradicts itself is recorded */
    static agent_t a0, b0;
    agent_init(&a0, 0, 0);
    agent_init(&b0, 0, 0);
    pag_tell(&b0.a, 0, 0, true);
    pag_tell(&b0.a, 0, 0, false);
    pag_merge(&a0.a, &b0.a);
    CHECK(a0.a.ft[0] == 1 && a0.a.ff[0] == 1, "merge reaches prop 0");
    lpx_qrec_t qr[2];
    lpx_qlog_t q;
    lpx_qlog_init(&q, qr, 2);
    q.n = 1;
    pag_tell(&a0.a, 1, 1, true);
    pag_tell(&a0.a, 1, 1, false);
    CHECK(!pag_run(&a0.a, &q), "one free slot, two bad sources: refuse");
    lpx_qlog_init(&q, qr, 2);
    CHECK(pag_run(&a0.a, &q) && q.n == 2 && qr[0].submitter == 0, "source 0 recorded");
    pag_decision_t d = pag_decide(0, 0, false, 0, 0);
    CHECK(lpx_pol(d.tag) == LPX_P_NEUTRAL && lpx_base(d.tag) == LPX_B_NEITHER, "null: no trend");
}

/* exact-size heap storage: ASan sees any loop that steps one past the end */
typedef struct {
    plg_t lg;
    plg_entry_t *cl;
    plg_vote_t *vt;
    uint64_t *bal, *hold, *gen;
    pdx_rec_t rec[256];
    pdx_log_t log;
    lpx_qrec_t qrec[16];
    lpx_qlog_t q;
} xnode_t;

static void xinit(xnode_t *x, uint32_t nacc, uint32_t ncl, uint32_t nvt, bool rule)
{
    x->cl = malloc(ncl * sizeof *x->cl);
    x->vt = malloc(nvt * sizeof *x->vt);
    x->bal = malloc(nacc * sizeof *x->bal);
    x->hold = malloc(ncl * sizeof *x->hold);
    x->gen = malloc(nacc * sizeof *x->gen);
    uint64_t sum = 0;
    for (uint32_t a = 0; a < nacc; a++) sum += x->gen[a] = 40 + a;
    plg_cfg_t c;
    memset(&c, 0, sizeof c);
    c.n_acct = nacc;
    c.genesis = x->gen;
    c.assets = sum;
    c.n_witness = 3;
    c.arbiter = 100;
    c.owner = 200;
    c.conflict_limit = 2;
    c.decisive = 1;
    c.first_sig_rule = rule;
    pdx_log_init(&x->log, x->rec, 256);
    lpx_qlog_init(&x->q, x->qrec, 16);
    plg_init(&x->lg, &c, x->cl, ncl, x->vt, nvt, x->bal, x->hold, &x->log, &x->q);
}

static void xfree(xnode_t *x)
{
    free(x->cl);
    free(x->vt);
    free(x->bal);
    free(x->hold);
    free(x->gen);
}

static void t_exact(void)
{
    for (int rule = 0; rule < 2; rule++) {
        static xnode_t a, b;
        xinit(&a, 3, 6, 3, rule);
        xinit(&b, 3, 6, 3, rule);
        plg_claim_t c[6] = {mk(1, 2, 0, 0, 5, 1, 1),  mk(1, 2, 1, 0, 7, 2, 2),
                            mk(1, 2, 0, 1, 50, 1, 3), mk(2, 0, 1, 0, 3, 1, 4),
                            mk(2, 1, 0, 0, 4, 1, 5),  mk(2, 2, 1, 0, 9, 1, 6)};
        for (int i = 0; i < 6; i++) plg_claim(&a.lg, &c[i], true, i != 2);
        plg_vote_t v[3] = {vote(2, PLG_V_WITNESS, 2, &c[1], true),
                           vote(2, PLG_V_WITNESS, 1, &c[0], false),
                           vote(3, PLG_V_OWNER, 200, &c[0], true)};
        for (int i = 0; i < 3; i++) plg_vote(&a.lg, &v[i], true);
        uint8_t missing[32] = {1};
        CHECK(a.lg.n_cl == 6 && a.lg.n_vt == 3 && plg_find(&a.lg, missing) == -1, "exact fill");
        uint32_t added = 0;
        CHECK(plg_merge(&b.lg, &a.lg, &added, 0) == PLG_OK && added == 9, "exact merge");
        CHECK(plg_advance(&a.lg, 1) == PLG_OK && plg_advance(&a.lg, 3) == PLG_OK &&
                  plg_advance(&b.lg, 3) == PLG_OK,
              "exact advance");
        CHECK(!memcmp(a.lg.digest, b.lg.digest, 32) && plg_conserved(&a.lg), "exact agree");
        CHECK(a.lg.cl[0].st == PLG_CS_SETTLED || a.lg.cl[1].st == PLG_CS_SETTLED, "one wins");
        xfree(&a);
        xfree(&b);
    }
}

int main(void)
{
    t_exact();
    t_more_edges();
    t_bytes();
    t_boundaries();
    t_equivocation_pairs();
    t_nulls_and_names();
    t_agent_edges();
    t_init();
    t_single();
    t_double_spend_witness();
    t_first_sig_and_rulings();
    t_frozen();
    t_funding();
    t_forbidden();
    t_capacity();
    t_partitions();
    t_order_free();
    t_agent_jurisdictions();
    t_agent_rules();
    t_agent_self_contradiction();
    t_agent_reference();
    t_agent_merge();
    t_agents_witness();
    for (uint32_t s = 0; s < 6; s++) (void) plg_state_name((uint8_t) s);
    CHECK(!strcmp(plg_state_name(PLG_CS_HELD), "held") && !strcmp(plg_state_name(9), "?"),
          "state names");
    CHECK(!strcmp(plg_res_name(PLG_FORBIDDEN), "forbidden") &&
              !strcmp(plg_res_name((plg_res_t) 99), "?"),
          "result names");
    CHECK(lpx_forbidden(plg_tag(77)), "unknown state tags forbidden");
    printf("%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
