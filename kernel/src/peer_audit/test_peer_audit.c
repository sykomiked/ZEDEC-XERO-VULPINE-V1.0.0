/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_peer_audit.c — self-audit, replay, committee, quorum, canaries,
 * evidence and spot checks over a simulated 10-node swarm. Host test. */
#include <stdio.h>
#include <string.h>
#include "peer_audit.h"
#include "pa_runtime.h"
#include "../pqsec/pq_security.h" /* prototype drift vs pa_pq.h is a compile error */
#include "../lpres/lpres.h"
#include "../tensor/zt.h"
#include "../robin_debanks/sha256.h"

_Static_assert(PA_NODE_ACTIVE == (int) LPRES_STATE_TRUE, "ACTIVE is S+");
_Static_assert(PA_NODE_CONTRADICTION == (int) LPRES_STATE_BOTH, "CONTRADICTION is BOTH");
_Static_assert(PA_SIG_LEN == PQ_MLDSA65_SIG_BYTES, "sig size");

static int g_pass, g_fail;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        if (cond) {                                                                                \
            g_pass++;                                                                              \
        } else {                                                                                   \
            g_fail++;                                                                              \
            printf("[FAIL] %s (line %d)\n", msg, __LINE__);                                        \
        }                                                                                          \
    } while (0)

#define N_NODES 10u
#define F_FAULT 2u
#define K_COMM  (3u * F_FAULT + 1u)

static uint8_t g_pk[N_NODES][PA_PK_LEN];
static uint8_t g_sk[N_NODES][PA_SK_LEN];
static pa_ctx_t g_ctx[N_NODES];
static pay_ledger_t g_led[N_NODES];
static swarm_budget_t g_bud[N_NODES];
static pa_runtime_t g_rt[N_NODES];
static pay_ledger_t g_led_tmp;     /* shared replay scratch */
static swarm_budget_t g_bud_tmp;   /* shared replay scratch */
static pay_ledger_t g_node_tmp;    /* author-side scratch */
static swarm_budget_t g_node_btmp; /* author-side scratch */
static uint32_t g_ids[N_NODES];
static uint32_t ACC_ISSUER, ACC_ALICE, ACC_BOB;

typedef struct {
    uint32_t n;
    uint32_t peer[16];
    pa_revoke_reason_t why[16];
    bool had_evidence[16];
} revoke_log_t;
static revoke_log_t g_rev[N_NODES];

static void on_revoke(void *ctx, uint32_t peer, pa_revoke_reason_t why,
                      const uint8_t evidence_hash[PA_HASH_LEN])
{
    revoke_log_t *l = (revoke_log_t *) ctx;
    if (l->n < 16) {
        l->peer[l->n] = peer;
        l->why[l->n] = why;
        l->had_evidence[l->n] = evidence_hash != NULL;
        l->n++;
    }
}

static int node_index(uint32_t id)
{
    for (uint32_t i = 0; i < N_NODES; i++)
        if (g_ids[i] == id) return (int) i;
    return -1;
}

static void genesis(pay_ledger_t *L, swarm_budget_t *b)
{
    pay_ledger_init(L, NULL);
    uint32_t a;
    pay_ledger_open(L, 1, 0, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER, &a);
    ACC_ISSUER = a;
    pay_ledger_open(L, 2, 0, PAY_CAP_FINANCIAL, 0, &a);
    ACC_ALICE = a;
    pay_ledger_open(L, 3, 0, PAY_CAP_FINANCIAL, 0, &a);
    ACC_BOB = a;
    pay_posting_req_t req;
    memset(&req, 0, sizeof req);
    uint8_t rnd[16];
    for (int i = 0; i < 16; i++) rnd[i] = (uint8_t) (0xA0 + i);
    pay_uetr_from_random(rnd, req.uetr);
    memset(req.idem_key, 0x11, 32);
    strcpy(req.e2e, "GENESIS");
    req.initiator = 1;
    pay_receipt_t rc;
    pay_status_t st = pay_ledger_issue(L, &req, ACC_ISSUER, ACC_ALICE, 1000000, &rc);
    if (st != PAY_OK) printf("genesis issue failed %d\n", st);

    swarm_budget_init(b, 3, 1000);
    swarm_budget_register(b, 1, 0);
    swarm_budget_register(b, 2, 1);
    swarm_budget_register(b, 3, 2);
    swarm_budget_begin_cycle(b);
}

static void setup(void)
{
    for (uint32_t i = 0; i < N_NODES; i++) {
        uint8_t seed[32];
        memset(seed, (int) (0x40 + i), 32);
        pq_mldsa65_keygen(seed, g_pk[i], g_sk[i]);
        g_ids[i] = 101 + i;
    }
    for (uint32_t i = 0; i < N_NODES; i++) {
        genesis(&g_led[i], &g_bud[i]);
        g_rt[i].ledger = &g_led[i];
        g_rt[i].ledger_tmp = &g_led_tmp;
        g_rt[i].budget = &g_bud[i];
        g_rt[i].budget_tmp = &g_bud_tmp;
        g_rt[i].asset = 0;
        pa_init(&g_ctx[i], g_ids[i], g_sk[i], NULL, pa_runtime_replay, &g_rt[i], on_revoke,
                &g_rev[i]);
        for (uint32_t j = 0; j < N_NODES; j++) pa_peer_add(&g_ctx[i], g_ids[j], g_pk[j]);
    }
}

static uint32_t g_xfer_ctr;
static pa_xfer_t make_xfer(uint32_t from, uint32_t to, int64_t amount)
{
    pa_xfer_t x;
    memset(&x, 0, sizeof x);
    x.from = from;
    x.to = to;
    x.amount = amount;
    x.initiator = 2;
    x.tick = 100 + g_xfer_ctr;
    g_xfer_ctr++;
    for (int i = 0; i < 32; i++) x.idem[i] = (uint8_t) (g_xfer_ctr * 7u + (uint32_t) i);
    for (int i = 0; i < 16; i++) x.rnd[i] = (uint8_t) (g_xfer_ctr * 13u + (uint32_t) i * 3u);
    x.attestor = 9;
    return x;
}

typedef enum {
    B_HONEST = 0,
    B_LIE_STATE,      /* credits the receiver one extra unit in its state and claim */
    B_LIE_OUTPUT,     /* honest state, claims the receiver got one extra unit */
    B_ACCEPT_NEGATIVE /* flawed: runs a negative amount as a reverse transfer */
} behaviour_t;

static void put64le(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}
static uint64_t get64le(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

/* The author's side: execute on a scratch copy, self-audit, sign. */
static pa_status_t node_transfer(uint32_t n, const pa_xfer_t *x, behaviour_t b, int64_t headroom,
                                 pa_record_t *rec)
{
    uint8_t in[PA_XFER_IN_LEN], out[PA_XFER_OUT_LEN], prev[32], nw[32];
    pa_xfer_encode(x, in);
    pa_ledger_state_hash(&g_led[n], prev);
    memcpy(&g_node_tmp, &g_led[n], sizeof g_node_tmp);
    if (b == B_ACCEPT_NEGATIVE && x->amount < 0) {
        pa_xfer_t y = *x;
        y.from = x->to;
        y.to = x->from;
        y.amount = -x->amount;
        uint8_t in2[PA_XFER_IN_LEN];
        pa_xfer_encode(&y, in2);
        pa_ledger_apply(&g_node_tmp, in2, sizeof in2, out);
    } else {
        pa_ledger_apply(&g_node_tmp, in, sizeof in, out);
    }
    if (b == B_LIE_STATE) {
        g_node_tmp.acct[x->to].debit += 1;
        g_node_tmp.acct[x->to].equity += 1;
        put64le(out + 16, get64le(out + 16) + 1);
    }
    if (b == B_LIE_OUTPUT) put64le(out + 16, get64le(out + 16) + 1);
    pa_ledger_state_hash(&g_node_tmp, nw);
    pa_precommit_t p;
    pa_ledger_precommit(&g_led[n], &g_node_tmp, 0, headroom, &p);
    return pa_emit_record(&g_ctx[n], PA_KIND_LEDGER_TRANSFER, prev, in, sizeof in, out, sizeof out,
                          nw, &p, rec);
}

/* Run a committee over a record; returns the verdict. faulty: committee
 * members (by draw position < n_faulty) that sign the opposite vote. */
static pa_vote_t g_votes[PA_MAX_COMMITTEE];
static pa_verdict_t run_committee(const pa_record_t *rec, uint32_t n_faulty, uint32_t observer)
{
    uint8_t rh[32];
    uint32_t comm[PA_MAX_COMMITTEE];
    pa_record_hash(rec, rh);
    if (pa_committee(g_ids, N_NODES, rec->node, rh, K_COMM, comm) != PA_OK)
        return PA_VERDICT_UNDECIDED;
    for (uint32_t i = 0; i < K_COMM; i++) {
        int vi = node_index(comm[i]);
        pa_vote(&g_ctx[vi], rec, &g_votes[i]);
        if (i < n_faulty) { /* Byzantine: flip and re-sign with its own key */
            g_votes[i].vote = g_votes[i].vote == PA_VOTE_ACCEPT ? PA_VOTE_REJECT : PA_VOTE_ACCEPT;
            uint8_t vh[32];
            pa_vote_hash(&g_votes[i], vh);
            pq_mldsa65_sign(g_sk[vi], vh, 32, (const uint8_t *) PA_CTX_VOTE,
                            (uint32_t) strlen(PA_CTX_VOTE), NULL, g_votes[i].sig);
        }
    }
    return pa_quorum(&g_ctx[observer], rh, comm, K_COMM, F_FAULT, g_votes, K_COMM, NULL);
}

static void apply_everywhere(const pa_record_t *rec)
{
    for (uint32_t i = 0; i < N_NODES; i++) pa_runtime_apply(&g_rt[i], rec);
}

static bool replicas_agree(void)
{
    uint8_t h0[32], h[32];
    pa_ledger_state_hash(&g_led[0], h0);
    for (uint32_t i = 1; i < N_NODES; i++) {
        pa_ledger_state_hash(&g_led[i], h);
        if (memcmp(h0, h, 32) != 0) return false;
    }
    return true;
}

/* ===== tests ===== */
static pa_record_t g_rec, g_rec2;
static pa_evidence_t g_ev, g_ev2;

static void test_self_audit(void)
{
    pa_ctx_t *c = &g_ctx[0];
    pa_precommit_t p;
    memset(&p, 0, sizeof p);
    p.sum_before = 100;
    p.sum_after = 100;
    p.headroom = 1;
    p.size_max = 10;
    CHECK(pa_precommit_check(c, 1, &p) == PA_OK, "precommit: conserved transition passes");
    p.minted = 5;
    p.sum_after = 105;
    CHECK(pa_precommit_check(c, 1, &p) == PA_OK, "precommit: minted accounted passes");
    p.sum_after = 104;
    CHECK(pa_precommit_check(c, 1, &p) == PA_ERR_CONSERVATION, "precommit: off-by-one minted");
    CHECK(c->state == PA_NODE_CONTRADICTION, "precommit failure enters CONTRADICTION");
    CHECK(c->n_events == 1 && c->events[0].status == PA_ERR_CONSERVATION, "event recorded");
    p.sum_after = 105;
    CHECK(pa_precommit_check(c, 1, &p) == PA_ERR_QUARANTINED, "quarantined refuses even good");
    pa_node_release(c);
    CHECK(pa_precommit_check(c, 1, &p) == PA_OK, "release restores");
    pa_precommit_t q = p;
    q.sum_before = UINT64_MAX;
    q.minted = 1;
    CHECK(pa_precommit_check(c, 1, &q) == PA_ERR_OVERFLOW, "precommit: overflow");
    pa_node_release(c);
    q = p;
    q.tokens_requested = 11;
    q.tokens_remaining = 10;
    CHECK(pa_precommit_check(c, 1, &q) == PA_ERR_BUDGET, "precommit: token budget");
    pa_node_release(c);
    q = p;
    q.headroom = 0;
    CHECK(pa_precommit_check(c, 1, &q) == PA_ERR_HEADROOM, "precommit: ISF headroom h_t <= 0");
    pa_node_release(c);
    q = p;
    q.size = 11;
    CHECK(pa_precommit_check(c, 1, &q) == PA_ERR_SIZE, "precommit: size bound");
    pa_node_release(c);
    c->n_events = 0;

    /* End to end: a lying node with self-audit ON never signs. */
    pa_xfer_t x = make_xfer(ACC_ALICE, ACC_BOB, 250);
    pa_status_t st = node_transfer(1, &x, B_LIE_STATE, 1000, &g_rec);
    CHECK(st == PA_ERR_CONSERVATION, "self-audit catches +1 minted from nothing");
    CHECK(g_ctx[1].state == PA_NODE_CONTRADICTION, "liar enters CONTRADICTION");
    uint8_t zero[PA_SIG_LEN];
    memset(zero, 0, sizeof zero);
    CHECK(memcmp(g_rec.sig, zero, PA_SIG_LEN) == 0 && g_rec.in_len == 0,
          "nothing signed or emitted (S0)");
    x = make_xfer(ACC_ALICE, ACC_BOB, 1);
    CHECK(node_transfer(1, &x, B_HONEST, 1000, &g_rec) == PA_ERR_QUARANTINED,
          "quarantined node refuses further emits");
    pa_node_release(&g_ctx[1]);
    x = make_xfer(ACC_ALICE, ACC_BOB, 1);
    CHECK(node_transfer(1, &x, B_HONEST, 0, &g_rec) == PA_ERR_HEADROOM,
          "headroom 0 stops a ledger emit");
    pa_node_release(&g_ctx[1]);
}

static void test_honest(void)
{
    pa_xfer_t x = make_xfer(ACC_ALICE, ACC_BOB, 500);
    CHECK(node_transfer(0, &x, B_HONEST, 1000, &g_rec) == PA_OK, "honest node emits");
    CHECK(pa_record_verify_sig(&g_ctx[5], &g_rec) == PA_OK, "record signature verifies");
    int ok = 0;
    for (uint32_t v = 1; v < N_NODES; v++)
        ok += pa_replay_check(&g_ctx[v], &g_rec, 0, 0, 0) == PA_OK;
    CHECK(ok == (int) N_NODES - 1, "every verifier replays the honest record bit-exact");
    CHECK(run_committee(&g_rec, 0, 9) == PA_VERDICT_ACCEPT, "committee accepts honest record");
    apply_everywhere(&g_rec);
    CHECK(replicas_agree(), "all replicas agree after apply");
    CHECK(g_led[3].acct[ACC_BOB].debit == 500 && g_led[3].acct[ACC_ALICE].debit == 999500,
          "balances moved by real pay_ledger code");
    /* a tampered record (one byte of output) fails the signature */
    g_rec2 = g_rec;
    g_rec2.out[16] ^= 1;
    CHECK(pa_replay_check(&g_ctx[2], &g_rec2, 0, 0, 0) == PA_ERR_SIG, "tamper breaks signature");
    /* replaying a record whose prev state is gone */
    CHECK(pa_replay_check(&g_ctx[2], &g_rec, 0, 0, 0) == PA_ERR_NO_STATE,
          "stale prev_state is NO_STATE, not a verdict");
}

static void test_wrong_balance(void)
{
    /* Node 1 switches its self-audit off and lies (state +1). */
    g_ctx[1].self_audit = false;
    pa_xfer_t x = make_xfer(ACC_ALICE, ACC_BOB, 300);
    CHECK(node_transfer(1, &x, B_LIE_STATE, 1000, &g_rec) == PA_OK,
          "self-audit disabled: the lie is signed and emitted");
    int caught = 0;
    for (uint32_t v = 0; v < N_NODES; v++) {
        if (v == 1) continue;
        caught += pa_replay_check(&g_ctx[v], &g_rec, 0, 0, 0) == PA_ERR_MISMATCH;
    }
    CHECK(caught == (int) N_NODES - 1, "every honest verifier catches the +1 state lie");
    CHECK(run_committee(&g_rec, 0, 0) == PA_VERDICT_REJECT, "committee rejects the state lie");

    /* Output-only lie: state honest, claim +1; self-audit can't see it. */
    x = make_xfer(ACC_ALICE, ACC_BOB, 300);
    g_ctx[1].self_audit = true;
    CHECK(node_transfer(1, &x, B_LIE_OUTPUT, 1000, &g_rec2) == PA_OK,
          "output-only lie passes local conservation");
    caught = 0;
    for (uint32_t v = 0; v < N_NODES; v++) {
        if (v == 1) continue;
        caught += pa_replay_check(&g_ctx[v], &g_rec2, 0, 0, 0) == PA_ERR_MISMATCH;
    }
    CHECK(caught == (int) N_NODES - 1, "every honest verifier catches the +1 output lie");
    g_ctx[1].self_audit = false;

    /* Evidence from node 2 against the state lie; checked on node 7. */
    CHECK(pa_evidence_make(&g_ctx[2], &g_rec, &g_ev) == PA_OK, "evidence built");
    uint8_t before[32], after[32];
    pa_ledger_state_hash(&g_led[7], before);
    uint64_t bal_before = g_led[7].acct[ACC_ALICE].debit;
    CHECK(pa_evidence_check(&g_ctx[7], &g_ev) == PA_OK, "evidence verifies on a third node");
    CHECK(pa_peer_blocked(&g_ctx[7], g_ids[1]), "accused blocklisted on the third node");
    CHECK(g_rev[7].n == 1 && g_rev[7].peer[0] == g_ids[1] &&
              g_rev[7].why[0] == PA_REVOKE_MISBEHAVIOUR && g_rev[7].had_evidence[0],
          "revoke callback called once with the evidence hash");
    pa_ledger_state_hash(&g_led[7], after);
    CHECK(memcmp(before, after, 32) == 0 && g_led[7].acct[ACC_ALICE].debit == bal_before,
          "isolation moved no funds");
    CHECK(pa_replay_check(&g_ctx[7], &g_rec2, 0, 0, 0) == PA_ERR_BLOCKED,
          "blocked peer's later records are refused");
    /* Every other honest node can check the same object independently. */
    int ok = 0;
    for (uint32_t v = 0; v < N_NODES; v++)
        if (v != 1 && v != 7 && v != 2) ok += pa_evidence_check(&g_ctx[v], &g_ev) == PA_OK;
    CHECK(ok == (int) N_NODES - 3, "evidence verifies on every other node");
    /* Evidence against an honest record cannot be built. */
    pa_xfer_t y = make_xfer(ACC_ALICE, ACC_BOB, 7);
    pa_record_t hr;
    CHECK(node_transfer(4, &y, B_HONEST, 1000, &hr) == PA_OK, "honest record for evidence test");
    CHECK(pa_evidence_make(&g_ctx[2], &hr, &g_ev2) == PA_ERR_FALSE_ACCUSATION,
          "evidence_make refuses an honest record");
}

static void sign_evidence(uint32_t signer, pa_evidence_t *e)
{
    uint8_t eh[32];
    pa_evidence_hash(e, eh);
    pq_mldsa65_sign(g_sk[signer], eh, 32, (const uint8_t *) PA_CTX_EVIDENCE,
                    (uint32_t) strlen(PA_CTX_EVIDENCE), NULL, e->sig);
}

static void test_false_accusation(void)
{
    /* Node 5 accuses node 4 over an honest record, with a made-up result. */
    pa_xfer_t x = make_xfer(ACC_ALICE, ACC_BOB, 11);
    pa_record_t hr;
    CHECK(node_transfer(4, &x, B_HONEST, 1000, &hr) == PA_OK, "honest record");
    memset(&g_ev2, 0, sizeof g_ev2);
    g_ev2.record = hr;
    g_ev2.accuser = g_ids[5];
    g_ev2.correct_len = hr.out_len;
    memcpy(g_ev2.correct_out, hr.out, hr.out_len);
    g_ev2.correct_out[16] ^= 1;
    memcpy(g_ev2.correct_state, hr.new_state, 32);
    g_ev2.correct_state[0] ^= 1;
    sign_evidence(5, &g_ev2);
    CHECK(pa_evidence_check(&g_ctx[6], &g_ev2) == PA_ERR_FALSE_ACCUSATION,
          "false accusation rejected");
    CHECK(!pa_peer_blocked(&g_ctx[6], g_ids[4]), "accused stays unblocked");
    CHECK(pa_peer_get(&g_ctx[6], g_ids[5])->strikes == 1 && !pa_peer_blocked(&g_ctx[6], g_ids[5]),
          "accuser gets a strike");
    /* Forged record (accused never signed it): also against the accuser. */
    g_ev2.record.out[16] ^= 1;
    sign_evidence(5, &g_ev2);
    CHECK(pa_evidence_check(&g_ctx[6], &g_ev2) == PA_ERR_FALSE_ACCUSATION,
          "forged record counts against the accuser");
    CHECK(pa_peer_blocked(&g_ctx[6], g_ids[5]), "accuser isolated at the strike limit");
    CHECK(g_rev[6].n == 2 && g_rev[6].peer[1] == g_ids[5] &&
              g_rev[6].why[1] == PA_REVOKE_FALSE_ACCUSER && g_rev[6].had_evidence[1],
          "revoke reason: false accuser");
    /* Bad accuser signature: dropped, unattributable, nobody penalised. */
    g_ev2.sig[5] ^= 1;
    uint32_t s8 = pa_peer_get(&g_ctx[8], g_ids[5])->strikes;
    CHECK(pa_evidence_check(&g_ctx[8], &g_ev2) == PA_ERR_SIG, "bad accuser signature rejected");
    CHECK(pa_peer_get(&g_ctx[8], g_ids[5])->strikes == s8, "no strike without attribution");
}

static void test_committee(void)
{
    uint8_t rh[32], rh2[32];
    uint32_t a[K_COMM], b[K_COMM];
    memset(rh, 0x5a, 32);
    CHECK(pa_committee(g_ids, N_NODES, g_ids[0], rh, K_COMM, a) == PA_OK, "committee drawn");
    CHECK(pa_committee(g_ids, N_NODES, g_ids[0], rh, K_COMM, b) == PA_OK && !memcmp(a, b, sizeof a),
          "committee is deterministic");
    uint32_t rev[N_NODES + 2];
    for (uint32_t i = 0; i < N_NODES; i++) rev[i] = g_ids[N_NODES - 1 - i];
    rev[N_NODES] = g_ids[3]; /* duplicates do not add weight */
    rev[N_NODES + 1] = g_ids[3];
    CHECK(pa_committee(rev, N_NODES + 2, g_ids[0], rh, K_COMM, b) == PA_OK &&
              !memcmp(a, b, sizeof a),
          "committee independent of peer-list order and duplicates");
    bool author_in = false, dup = false;
    for (uint32_t i = 0; i < K_COMM; i++) {
        if (a[i] == g_ids[0]) author_in = true;
        for (uint32_t j = 0; j < i; j++)
            if (a[i] == a[j]) dup = true;
    }
    CHECK(!author_in && !dup, "author excluded, members distinct");
    memcpy(rh2, rh, 32);
    rh2[31] ^= 1;
    pa_committee(g_ids, N_NODES, g_ids[0], rh2, K_COMM, b);
    CHECK(memcmp(a, b, sizeof a) != 0, "a different record draws a different committee");
    CHECK(pa_committee(g_ids, N_NODES, g_ids[0], rh, N_NODES, b) == PA_ERR_FULL,
          "k larger than the eligible set is refused");
    /* Uniformity: each of 9 eligible peers picked ~ 7/9 of the time. */
    uint32_t cnt[N_NODES] = {0};
    const uint32_t R = 3000;
    for (uint32_t r = 0; r < R; r++) {
        uint8_t h[32];
        uint8_t m[4] = {(uint8_t) r, (uint8_t) (r >> 8), 1, 2};
        sha256(m, 4, h);
        pa_committee(g_ids, N_NODES, g_ids[0], h, 3, a);
        for (uint32_t i = 0; i < 3; i++) cnt[node_index(a[i])]++;
    }
    /* expected R*3/9 = 1000, sigma ~ 25.8; allow 5 sigma */
    bool uni = cnt[0] == 0;
    for (uint32_t i = 1; i < N_NODES; i++)
        if (cnt[i] < 871 || cnt[i] > 1129) uni = false;
    CHECK(uni, "committee draws are uniform over eligible peers (5 sigma)");
}

static void test_byzantine(void)
{
    /* an honest record and a lying one, each with f = 2 Byzantine voters */
    pa_xfer_t x = make_xfer(ACC_ALICE, ACC_BOB, 40);
    CHECK(node_transfer(3, &x, B_HONEST, 1000, &g_rec) == PA_OK, "honest record (byz)");
    CHECK(run_committee(&g_rec, F_FAULT, 9) == PA_VERDICT_ACCEPT,
          "f faulty of 3f+1: honest record still accepted");
    pa_xfer_t y = make_xfer(ACC_ALICE, ACC_BOB, 40);
    g_ctx[8].self_audit = false;
    CHECK(node_transfer(8, &y, B_LIE_OUTPUT, 1000, &g_rec2) == PA_OK, "lying record (byz)");
    CHECK(run_committee(&g_rec2, F_FAULT, 9) == PA_VERDICT_REJECT,
          "f faulty of 3f+1: lying record still rejected");
    /* f+1 faulty: safety holds (never the wrong verdict), liveness lost */
    pa_verdict_t v = run_committee(&g_rec2, F_FAULT + 1, 9);
    CHECK(v != PA_VERDICT_ACCEPT, "f+1 faulty: lie is never accepted");
    /* forged, duplicated and outsider votes are ignored */
    uint8_t rh[32];
    uint32_t comm[K_COMM], counted = 0;
    pa_record_hash(&g_rec, rh);
    pa_committee(g_ids, N_NODES, g_rec.node, rh, K_COMM, comm);
    run_committee(&g_rec, 0, 9);
    pa_vote_t vs[K_COMM + 3];
    memcpy(vs, g_votes, sizeof(pa_vote_t) * K_COMM);
    for (uint32_t i = 0; i < 5; i++) vs[i].sig[9] ^= 1; /* 5 forged */
    vs[K_COMM] = g_votes[5];
    vs[K_COMM + 1] = g_votes[5]; /* duplicates */
    vs[K_COMM + 2] = g_votes[6];
    v = pa_quorum(&g_ctx[9], rh, comm, K_COMM, F_FAULT, vs, K_COMM + 3, &counted);
    CHECK(counted == 2 && v == PA_VERDICT_UNDECIDED, "forged and duplicate votes not counted");
    CHECK(pa_quorum(&g_ctx[9], rh, comm, 6, F_FAULT, g_votes, 6, NULL) == PA_VERDICT_UNDECIDED,
          "committee smaller than 3f+1 never decides");
    apply_everywhere(&g_rec);
    CHECK(replicas_agree(), "replicas agree after the byzantine round");
}

static void test_canary(void)
{
    /* node 2 challenges node 3 (flawed: accepts negative) and node 4. */
    pa_request_t q;
    memset(&q, 0, sizeof q);
    pa_xfer_t x = make_xfer(ACC_ALICE, ACC_BOB, -5);
    q.kind = PA_KIND_LEDGER_TRANSFER;
    memset(q.nonce, 0x33, sizeof q.nonce);
    pa_ledger_state_hash(&g_led[2], q.prev_state);
    pa_xfer_encode(&x, q.in);
    q.in_len = PA_XFER_IN_LEN;
    CHECK(pa_canary_issue(&g_ctx[2], g_ids[3], 5000, &q) == PA_OK, "canary issued to flawed node");
    /* the flawed node answers like any request */
    pa_record_t ans;
    g_ctx[3].self_audit = false; /* its local checks are also broken */
    CHECK(node_transfer(3, &x, B_ACCEPT_NEGATIVE, 1000, &ans) == PA_OK, "flawed node answers");
    pa_evidence_t ev;
    CHECK(pa_canary_check(&g_ctx[2], &q, &ans, &ev) == PA_ERR_MISMATCH,
          "canary catches a node that accepts a negative transfer");
    CHECK(pa_peer_get(&g_ctx[2], g_ids[3])->canary_failed == 1, "failure counted");
    CHECK(pa_evidence_check(&g_ctx[9], &ev) == PA_OK, "canary failure is third-party evidence");
    CHECK(pa_canary_check(&g_ctx[2], &q, &ans, NULL) == PA_ERR_NOT_FOUND, "canary consumed");

    /* honest node 4 gets the same negative canary and boundary ones */
    int64_t amounts[3] = {-5, 0, INT64_MAX};
    for (int i = 0; i < 3; i++) {
        pa_xfer_t z = make_xfer(ACC_ALICE, ACC_BOB, amounts[i]);
        pa_request_t r = q;
        pa_xfer_encode(&z, r.in);
        r.nonce[0] = (uint8_t) i;
        CHECK(pa_canary_issue(&g_ctx[2], g_ids[4], 5000, &r) == PA_OK, "canary issued");
        pa_record_t a2;
        CHECK(node_transfer(4, &z, B_HONEST, 1000, &a2) == PA_OK, "honest node answers canary");
        CHECK(a2.out[0] != 0 && !memcmp(a2.prev_state, a2.new_state, 32),
              "honest answer is fail-closed (refused, state unchanged)");
        CHECK(pa_canary_check(&g_ctx[2], &r, &a2, NULL) == PA_OK, "honest node passes canary");
    }
    /* rate limit: 4 per window (1 used on node 3, 3 on node 4) */
    pa_request_t r = q;
    r.nonce[0] = 77;
    CHECK(pa_canary_issue(&g_ctx[2], g_ids[4], 5001, &r) == PA_OK, "4th canary in window ok");
    CHECK(pa_canary_issue(&g_ctx[2], g_ids[4], 5002, &r) == PA_ERR_RATE, "5th canary rate-limited");
    CHECK(pa_canary_issue(&g_ctx[2], g_ids[4], 6001, &r) == PA_OK, "next window allows again");
    /* same request format: a canary is just a pa_request_t */
    CHECK(sizeof(pa_request_t) == sizeof q, "canary and real request share one format");
    /* bounded table: fill it, then FULL */
    pa_status_t st = PA_OK;
    uint64_t tick = 100000;
    for (uint32_t i = 0; i < PA_MAX_CANARIES + 2 && st == PA_OK; i++) {
        r.nonce[1] = (uint8_t) i;
        st = pa_canary_issue(&g_ctx[2], g_ids[5], tick, &r);
        tick += 2000; /* new window each time so only the table limits */
    }
    CHECK(st == PA_ERR_FULL, "canary table fails closed when full");
}

static void test_budget(void)
{
    uint32_t n = 6;
    pa_budget_req_t br = {PA_BUDGET_CONSUME, 2, 50};
    uint8_t in[PA_BUDGET_IN_LEN], out[PA_BUDGET_OUT_LEN], prev[32], nw[32];
    pa_budget_encode(&br, in);
    pa_budget_state_hash(&g_bud[n], prev);
    memcpy(&g_node_btmp, &g_bud[n], sizeof g_node_btmp);
    pa_budget_apply(&g_node_btmp, in, sizeof in, out);
    pa_budget_state_hash(&g_node_btmp, nw);
    pa_precommit_t p;
    pa_budget_precommit(&g_bud[n], &g_node_btmp, &br, 1000, &p);
    CHECK(pa_emit_record(&g_ctx[n], PA_KIND_BUDGET, prev, in, sizeof in, out, sizeof out, nw, &p,
                         &g_rec) == PA_OK,
          "budget consume emitted");
    CHECK(run_committee(&g_rec, 0, 0) == PA_VERDICT_ACCEPT, "budget consume accepted by replay");
    /* lie: claim one more token granted */
    out[8] += 1;
    CHECK(pa_emit_record(&g_ctx[n], PA_KIND_BUDGET, prev, in, sizeof in, out, sizeof out, nw, &p,
                         &g_rec2) == PA_OK,
          "budget lie emitted (outputs only)");
    CHECK(run_committee(&g_rec2, 0, 0) == PA_VERDICT_REJECT, "budget lie rejected");
    /* self-audit: a node that grants beyond the remaining budget */
    memcpy(&g_node_btmp, &g_bud[n], sizeof g_node_btmp);
    for (uint32_t i = 0; i < g_node_btmp.num_slots; i++)
        if (g_node_btmp.slots[i].model_id == 2)
            g_node_btmp.slots[i].used = g_node_btmp.slots[i].allotted + 5;
    pa_budget_precommit(&g_bud[n], &g_node_btmp, &br, 1000, &p);
    CHECK(pa_precommit_check(&g_ctx[n], PA_KIND_BUDGET, &p) == PA_ERR_BUDGET,
          "self-audit catches over-budget grant");
    pa_node_release(&g_ctx[n]);
    for (uint32_t i = 0; i < N_NODES; i++) pa_runtime_apply(&g_rt[i], &g_rec);
    uint8_t h0[32], h[32];
    pa_budget_state_hash(&g_bud[0], h0);
    bool same = true;
    for (uint32_t i = 1; i < N_NODES; i++) {
        pa_budget_state_hash(&g_bud[i], h);
        if (memcmp(h, h0, 32)) same = false;
    }
    CHECK(same, "budget replicas agree after apply");
}

/* ===== tiny deterministic model on the integer tensor engine ===== */
#define TV 32u
static uint32_t tiny_argmax(const zt_fx *logits, uint32_t n)
{
    uint32_t b = 0;
    for (uint32_t i = 1; i < n; i++)
        if (logits[i] > logits[b]) b = i;
    return b;
}
static int32_t tiny_infer(void *ctx, const uint8_t cid[32], uint64_t seed, const uint8_t *prompt,
                          uint32_t plen, int32_t *tok, uint32_t max)
{
    (void) ctx;
    zt_fx wf[TV * TV];
    zt_q8_t w[TV];
    for (uint32_t r = 0; r < TV; r++) {
        uint8_t h[32], m[36];
        memcpy(m, cid, 32);
        m[32] = (uint8_t) r;
        m[33] = m[34] = m[35] = 0;
        sha256(m, 36, h);
        for (uint32_t c = 0; c < TV; c++) wf[r * TV + c] = ((int32_t) h[c] - 128) * 256;
    }
    for (uint32_t r = 0; r < TV; r++) zt_quantize(&wf[r * TV], TV, &w[r], false);
    uint32_t t = 0;
    for (uint32_t i = 0; i < plen; i++) t = (t + prompt[i]) & (TV - 1);
    for (uint32_t s = 0; s < max; s++) {
        zt_fx xf[TV];
        zt_q8_t xq;
        for (uint32_t c = 0; c < TV; c++) xf[c] = 0;
        xf[t] = ZT_ONE;
        xf[(t + (uint32_t) (seed >> (s & 31))) & (TV - 1)] += ZT_ONE / 2;
        zt_quantize(xf, TV, &xq, false);
        zt_fx y[TV];
        zt_matvec(w, TV, &xq, 1, y);
        t = tiny_argmax(y, TV);
        tok[s] = (int32_t) t;
    }
    return (int32_t) max;
}

static void test_spot(void)
{
    uint8_t cid[32], beacon[32], c[32];
    memset(cid, 0x7c, 32);
    memset(beacon, 0x2b, 32);
    const uint8_t prompt[] = "hello swarm";
    int32_t tok[16], tok2[16];
    tiny_infer(0, cid, 42, prompt, sizeof prompt - 1, tok, 16);
    tiny_infer(0, cid, 42, prompt, sizeof prompt - 1, tok2, 16);
    CHECK(!memcmp(tok, tok2, sizeof tok), "tiny tensor model is deterministic");
    pa_infer_commit(cid, 42, prompt, sizeof prompt - 1, tok, 16, c);
    CHECK(pa_spot_check(tiny_infer, 0, cid, 42, prompt, sizeof prompt - 1, 16, c) == PA_OK,
          "honest inference passes spot check");
    tok[7] ^= 1;
    pa_infer_commit(cid, 42, prompt, sizeof prompt - 1, tok, 16, c);
    CHECK(pa_spot_check(tiny_infer, 0, cid, 42, prompt, sizeof prompt - 1, 16, c) ==
              PA_ERR_MISMATCH,
          "one changed token is caught when sampled");
    uint8_t cid2[32];
    memset(cid2, 0x7d, 32);
    tok[7] ^= 1;
    pa_infer_commit(cid2, 42, prompt, sizeof prompt - 1, tok, 16, c);
    CHECK(pa_spot_check(tiny_infer, 0, cid2, 42, prompt, sizeof prompt - 1, 16, c) ==
              PA_ERR_MISMATCH,
          "claiming a different model CID is caught");
    /* sampling statistics */
    const uint32_t R = 20000, rate = 6554; /* ~10.0% */
    uint32_t hits = 0, all = 0, none = 0;
    for (uint32_t r = 0; r < R; r++) {
        int32_t t1 = (int32_t) r;
        pa_infer_commit(cid, r, prompt, 3, &t1, 1, c);
        hits += pa_spot_sampled(beacon, c, rate);
        all += pa_spot_sampled(beacon, c, 65536);
        none += pa_spot_sampled(beacon, c, 0);
    }
    /* expected R*6554/65536 = 2000.1, sigma ~ 42.4; allow 5 sigma */
    CHECK(hits > 1788 && hits < 2212, "spot-check rate within 5 sigma of 10%");
    CHECK(all == R && none == 0, "rate 1.0 samples all, 0 samples none");
    printf("spot-check: %u of %u sampled at rate %u/65536\n", hits, R, rate);
}

static void test_bounds(void)
{
    static pa_ctx_t c;
    pa_init(&c, 1, NULL, NULL, NULL, NULL, NULL, NULL);
    pa_status_t st = PA_OK;
    for (uint32_t i = 0; i <= PA_MAX_PEERS && st == PA_OK; i++)
        st = pa_peer_add(&c, 1000 + i, g_pk[0]);
    CHECK(st == PA_ERR_FULL, "peer table fails closed when full");
    CHECK(pa_peer_add(&c, 1000, g_pk[1]) == PA_ERR_ARG, "a peer key is never replaced");
    pa_record_t r;
    memset(&r, 0, sizeof r);
    r.in_len = PA_MAX_IO + 1;
    uint8_t h[32];
    CHECK(!pa_record_hash(&r, h), "oversized record refused");
    CHECK(pa_emit_record(&c, 1, h, h, 1, h, 1, h, NULL, &r) == PA_ERR_ARG,
          "verify-only context cannot sign");
}

int main(void)
{
    setup();
    test_self_audit();
    test_honest();
    test_wrong_balance();
    test_false_accusation();
    test_committee();
    test_byzantine();
    test_canary();
    test_budget();
    test_spot();
    test_bounds();
    if (g_fail) {
        printf("test_peer_audit: %d failed, %d passed\n", g_fail, g_pass);
        return 1;
    }
    printf("%d checks passed\n", g_pass);
    return 0;
}
