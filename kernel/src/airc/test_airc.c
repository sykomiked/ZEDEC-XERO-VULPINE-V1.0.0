/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_airc.c — AIRC's protocol core (airc.h A1-A19) on real ML-DSA-87
 * keys, the real credibility module and the real paraconsistent table. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "airc.h"
#include "../mlkem/keccak.h"
#include "../pqsec/pq_matrix_algs.h"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL line %d: ", __LINE__);                                                  \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

typedef struct {
    uint8_t pk[AIRC_PK_LEN], sk[AIRC_SK_LEN], id[32];
} key_t_;

static key_t_ OWNER, ALICE, BOB, CAROL, MALLORY;

static void mk(key_t_ *k, uint8_t s)
{
    uint8_t seed[32];
    memset(seed, s, 32);
    pqm_mldsa87_keygen(seed, k->pk, k->sk);
    airc_key_id(k->pk, k->id);
}

static uint8_t WIRE[AIRC_WIRE_MAX], WIRE2[AIRC_WIRE_MAX];
static uint8_t BODY[AIRC_JUMBO_MAX];

static void base(airc_post_t *p, const uint8_t board[32], uint64_t seq, uint64_t ms,
                 const char *text)
{
    memset(p, 0, sizeof *p);
    p->kind = AIRC_TEXT;
    p->truth = AIRC_N;
    memcpy(p->board, board, 32);
    p->seq = seq;
    p->ms = ms;
    p->body = (const uint8_t *) text;
    p->body_len = (uint32_t) strlen(text);
}

/* sign and open: the verified post (static, one at a time per slot) */
static airc_signed_t *post(int slot, airc_post_t *p, const key_t_ *k, uint8_t *wire, uint32_t *wl)
{
    static airc_signed_t S[8];
    uint32_t n = airc_post_sign(p, k->pk, k->sk, NULL, wire, AIRC_WIRE_MAX);
    if (wl) *wl = n;
    CHECK(n > 0, "sign");
    airc_status_t st = airc_post_open(wire, n, &S[slot]);
    CHECK(st == AIRC_OK, "open: %s", airc_status_str(st));
    return &S[slot];
}

static void t_posts(void)
{
    uint8_t board[32];
    CHECK(airc_board_id("#Garden", board) == AIRC_OK, "board id");
    uint8_t b2[32];
    airc_board_id("#garden", b2);
    CHECK(!memcmp(board, b2, 32), "A6: board names fold ASCII case");
    {
        uint8_t want[32];
        const char *s = "zxv/airc/v1/board\n#garden";
        sha3_256((const uint8_t *) s, strlen(s), want);
        CHECK(!memcmp(want, board, 32), "A6: id = SHA3-256(prefix || name)");
    }
    const char *bad[] = {"garden", "#", "#a b", "#a,b", "#a:b", "#a\x07", NULL};
    for (int i = 0; bad[i]; i++)
        CHECK(airc_board_id(bad[i], b2) == AIRC_ERR_NAME, "bad name %d", i);
    char longn[60];
    memset(longn, 'x', sizeof longn);
    longn[0] = '#';
    longn[51] = 0;
    CHECK(airc_board_id(longn, b2) == AIRC_OK, "50 bytes after # is fine");
    longn[51] = 'x';
    longn[52] = 0;
    CHECK(airc_board_id(longn, b2) == AIRC_ERR_NAME, "51 is not");

    airc_post_t p;
    base(&p, board, 1, 1000, "hello agents");
    p.truth = AIRC_T;
    p.nrefs = 1;
    memset(p.refs[0].hash, 0xab, 32);
    p.refs[0].size = 1234567;
    p.refs[0].kind = AIRC_REF_CHUNK;
    uint32_t wl;
    airc_signed_t *s = post(0, &p, &ALICE, WIRE, &wl);
    CHECK(!memcmp(s->p.author, ALICE.id, 32), "A2: author = SHA3-256(pk)");
    CHECK(s->p.body_len == 12 && !memcmp(s->p.body, "hello agents", 12) && s->p.truth == AIRC_T &&
              s->p.seq == 1 && s->p.ms == 1000 && s->p.refs[0].size == 1234567,
          "A1: round trip");
    CHECK(wl == AIRC_HDR_LEN + 12 + AIRC_REF_LEN + AIRC_SIG_LEN + AIRC_PK_LEN, "wire length");
    CHECK(!memcmp(WIRE, "AIRC\x01\x01\x03\x01", 8), "A1: the header bytes");
    uint8_t want[32];
    sha3_256(WIRE, s->enc_len + AIRC_SIG_LEN, want);
    CHECK(!memcmp(want, s->id, 32), "A2: id = SHA3-256(encoding || signature)");
    /* big-endian seq */
    CHECK(WIRE[8 + 64 + 7] == 1 && WIRE[8 + 64] == 0, "seq big-endian");

    airc_signed_t o;
    /* A2: tampering */
    WIRE[AIRC_HDR_LEN + 2] ^= 1;
    CHECK(airc_post_open(WIRE, wl, &o) == AIRC_ERR_SIG, "a changed body fails the signature");
    WIRE[AIRC_HDR_LEN + 2] ^= 1;
    WIRE[wl - 1] ^= 1;
    CHECK(airc_post_open(WIRE, wl, &o) != AIRC_OK, "a changed key fails");
    WIRE[wl - 1] ^= 1;
    /* A2: a key that signed but is not the author */
    memcpy(WIRE2, WIRE, wl);
    {
        uint32_t n = s->enc_len;
        pqm_mldsa87_sign(BOB.sk, WIRE2, n, (const uint8_t *) "zxv/airc/v1/post", 16, NULL,
                         WIRE2 + n);
        memcpy(WIRE2 + n + AIRC_SIG_LEN, BOB.pk, AIRC_PK_LEN);
        CHECK(airc_post_open(WIRE2, wl, &o) == AIRC_ERR_AUTHOR,
              "A2: the signer must be the author");
        pqm_mldsa87_sign(ALICE.sk, WIRE2, n, (const uint8_t *) "zxv/irc/v1/msg", 14, NULL,
                         WIRE2 + n);
        memcpy(WIRE2 + n + AIRC_SIG_LEN, ALICE.pk, AIRC_PK_LEN);
        CHECK(airc_post_open(WIRE2, wl, &o) == AIRC_ERR_SIG, "A2: another context does not verify");
    }
    CHECK(airc_post_open(WIRE, wl - 1, &o) != AIRC_OK, "a short wire");

    /* A1: every decode refusal */
    uint8_t enc[2048];
    uint32_t el = airc_post_encode(&p, enc, sizeof enc);
    CHECK(el == AIRC_HDR_LEN + 12 + AIRC_REF_LEN, "encode");
    airc_post_t d;
    CHECK(airc_post_decode(enc, el, &d) == AIRC_OK, "decode");
    struct {
        uint32_t at;
        uint8_t v;
        const char *what;
    } muts[] = {{0, 'X', "magic"},
                {4, 2, "version"},
                {5, 0, "kind 0"},
                {5, 4, "kind 4"},
                {6, 4, "truth 4"},
                {7, 9, "nine refs"},
                {7, 2, "ref count past the end"},
                {el - 1, 3, "ref kind 3"}};
    for (unsigned i = 0; i < sizeof muts / sizeof muts[0]; i++) {
        uint8_t m[2048];
        memcpy(m, enc, el);
        m[muts[i].at] = muts[i].v;
        CHECK(airc_post_decode(m, el, &d) == AIRC_ERR_FORMAT, "A1 refuses %s", muts[i].what);
    }
    uint8_t m[2048];
    memcpy(m, enc, el);
    m[el] = 0;
    CHECK(airc_post_decode(m, el + 1, &d) == AIRC_ERR_FORMAT, "A1: bytes left over");
    CHECK(airc_post_decode(enc, el - 1, &d) == AIRC_ERR_FORMAT, "A1: short");
    CHECK(airc_post_decode(enc, 10, &d) == AIRC_ERR_FORMAT, "A1: shorter than a header");
    /* body length field larger than the jumbo cap */
    memcpy(m, enc, el);
    m[AIRC_HDR_LEN - 4] = 0x01; /* 16 MiB + 12 */
    CHECK(airc_post_decode(m, el, &d) == AIRC_ERR_FORMAT, "A1: body over the jumbo cap");

    /* A1: a body over a frame needs a lease, a lease needs a body over a frame */
    memset(BODY, 'j', 600);
    airc_post_t q;
    base(&q, board, 2, 1000, "x");
    q.body = BODY;
    q.body_len = 600;
    CHECK(airc_post_encode(&q, enc, sizeof enc) == 0, "A1: 600 bytes without a lease");
    q.body_len = 512;
    CHECK(airc_post_encode(&q, enc, sizeof enc) == AIRC_HDR_LEN + 512, "A1: 512 bytes fit a frame");
    memset(q.lease, 1, 32);
    CHECK(airc_post_encode(&q, enc, sizeof enc) == 0, "A1: a lease on a frame-sized body");
    /* A5 */
    base(&q, board, 3, 1000, "an offer without a manifest");
    q.kind = AIRC_OFFER;
    CHECK(airc_post_encode(&q, enc, sizeof enc) == 0, "A5: an OFFER needs refs[0]");
    q.kind = AIRC_REVIEW;
    q.nrefs = 1;
    q.refs[0].kind = AIRC_REF_CHUNK;
    CHECK(airc_post_encode(&q, enc, sizeof enc) == 0, "A5: a REVIEW needs a parent");
    CHECK(airc_post_encode(&q, enc, 10) == 0, "cap too small");
}

static void t_board(void)
{
    airc_trust_t *t = calloc(1, sizeof *t);
    airc_board_t *b = calloc(1, sizeof *b);
    airc_gate_t *g = calloc(1, sizeof *g);
    CHECK(airc_trust_init(t, NULL) == AIRC_OK && airc_board_init(b, "#garden") == AIRC_OK, "init");
    airc_gate_init(g);
    airc_post_t p;
    bool held;
    base(&p, b->id, 1, 100, "first");
    airc_signed_t *s = post(0, &p, &ALICE, WIRE, NULL);
    CHECK(airc_board_accept(b, t, g, s, 0, 100, &held) == AIRC_OK && !held, "A7: accepted");
    CHECK(airc_board_accept(b, t, g, s, 0, 101, &held) == AIRC_ERR_DUP, "A7: dup");
    const airc_entry_t *e = airc_board_find(b, s->id);
    CHECK(e && e->seq == 1 && !e->held, "found");
    airc_board_t *other = calloc(1, sizeof *other);
    airc_board_init(other, "#elsewhere");
    CHECK(airc_board_accept(other, t, g, s, 0, 100, &held) == AIRC_ERR_BOARD, "A7: another board");
    CHECK(airc_trust_tier(t, ALICE.id) == AIRC_COLD, "A15: unknown and untraded is COLD");

    /* A9: a COLD bucket of 4, refilled 6 a minute */
    for (uint64_t k = 2; k <= 4; k++) {
        base(&p, b->id, k, 100 + k, "more");
        s = post(0, &p, &ALICE, WIRE, NULL);
        CHECK(airc_board_accept(b, t, g, s, 0, 100, &held) == AIRC_OK, "within the burst %u",
              (unsigned) k);
    }
    base(&p, b->id, 5, 105, "one too many");
    s = post(0, &p, &ALICE, WIRE, NULL);
    CHECK(airc_board_accept(b, t, g, s, 0, 100, &held) == AIRC_ERR_RATE, "A9: the bucket is empty");
    CHECK(airc_board_accept(b, t, g, s, 0, 100 + 10000, &held) == AIRC_OK,
          "A9: one post back after 10 s");
    /* a bigger TPC share: x2 */
    base(&p, b->id, 1, 200, "bob");
    for (uint64_t k = 1; k <= 8; k++) {
        base(&p, b->id, k, 200 + k, "bob");
        s = post(0, &p, &BOB, WIRE, NULL);
        CHECK(airc_board_accept(b, t, g, s, 2000, 300, &held) == AIRC_OK, "A9: x2 budget, post %u",
              (unsigned) k);
    }
    base(&p, b->id, 9, 209, "bob");
    s = post(0, &p, &BOB, WIRE, NULL);
    CHECK(airc_board_accept(b, t, g, s, 2000, 300, &held) == AIRC_ERR_RATE,
          "A9: 8 is the x2 burst");

    /* A8 / A17: equivocation */
    base(&p, b->id, 7, 400, "the sky is green");
    p.truth = AIRC_T;
    airc_signed_t *a1 = post(1, &p, &CAROL, WIRE, NULL);
    CHECK(airc_board_accept(b, t, g, a1, 0, 400, &held) == AIRC_OK && !held, "carol posts");
    base(&p, b->id, 7, 401, "the sky is not green");
    p.truth = AIRC_T;
    airc_signed_t *a2 = post(2, &p, &CAROL, WIRE2, NULL);
    CHECK(airc_board_accept(b, t, g, a2, 0, 401, &held) == AIRC_OK && held,
          "A8: the second post with the same seq is held");
    CHECK(airc_board_find(b, a1->id)->held && airc_board_find(b, a2->id)->held,
          "A8: both are held as BOTH");
    CHECK(airc_trust_tier(t, CAROL.id) == AIRC_DROPPED, "A17: the author is dropped");
    const airc_peer_t *cp = airc_trust_peer(t, CAROL.id);
    CHECK(cp && lpx_action(cp->tag) == LPX_ACT_ROLLBACK && lpx_base(cp->tag) == LPX_B_BOTH &&
              lpx_pol(cp->tag) == LPX_P_FORBIDDEN,
          "A17: through the action table (BOTH, forbidden: rollback)");
    CHECK(cp && !memcmp(cp->evidence[0], a1->id, 32) && !memcmp(cp->evidence[1], a2->id, 32),
          "A17: the two post ids are kept as evidence");
    CHECK(t->log_n == 1 && t->log[0].reason == AIRC_LOG_EQUIVOCATION, "logged");
    base(&p, b->id, 8, 402, "anything");
    s = post(0, &p, &CAROL, WIRE, NULL);
    CHECK(airc_board_accept(b, t, g, s, 0, 100000, &held) == AIRC_ERR_DROPPED,
          "A9: a dropped author is refused");
    /* A16: a forwarder relays the bytes; the fault is the origin's */
    CHECK(airc_trust_tier(t, ALICE.id) != AIRC_DROPPED &&
              airc_trust_tier(t, BOB.id) != AIRC_DROPPED,
          "A16: nobody else is blamed");
    airc_trust_clear(t, CAROL.id);
    CHECK(airc_trust_tier(t, CAROL.id) == AIRC_COLD, "A17: the owner clears a drop");
    /* A17: a suspicion escalates, nothing is dropped */
    uint8_t subj[32] = {9};
    airc_trust_suspect(t, BOB.id, subj, 500);
    CHECK(t->escalations == 1 && airc_trust_tier(t, BOB.id) == AIRC_COLD,
          "A17: a suspicion escalates and drops nothing");
    CHECK(lpx_action(airc_trust_peer(t, BOB.id)->tag) == LPX_ACT_ESCALATE, "tag says escalate");

    /* A7: the oldest makes room */
    airc_board_t *small = calloc(1, sizeof *small);
    airc_board_init(small, "#tiny");
    airc_trust_config_t cfg;
    airc_trust_config_default(&cfg);
    cfg.cold_burst = 100000;
    cfg.cold_per_min = 100000;
    airc_trust_t *t2 = calloc(1, sizeof *t2);
    airc_trust_init(t2, &cfg);
    uint8_t first[32];
    for (uint64_t k = 0; k <= AIRC_BOARD_POSTS; k++) {
        base(&p, small->id, k, 1000 + k, "fill");
        s = post(0, &p, &ALICE, WIRE, NULL);
        if (k == 0) memcpy(first, s->id, 32);
        airc_board_accept(small, t2, g, s, 0, 1000, &held);
    }
    CHECK(!airc_board_find(small, first) && small->accepted == AIRC_BOARD_POSTS + 1,
          "A7: the oldest post made room");
    free(small);
    free(t2);
    free(other);
    free(t);
    free(b);
    free(g);
}

static void t_trust(void)
{
    airc_trust_t *t = calloc(1, sizeof *t);
    airc_trust_init(t, NULL);
    uint8_t rc[32] = {1};
    CHECK(airc_trust_pretrust(t, OWNER.id, 1) == AIRC_OK, "pretrust the owner");
    /* the owner pays alice for three trades: trust flows to her */
    for (int i = 0; i < 3; i++) {
        rc[1] = (uint8_t) i;
        CHECK(airc_trust_settle(t, OWNER.id, ALICE.id, 1000, rc, 10) == AIRC_OK, "settle %d", i);
    }
    CHECK(airc_trust_settle(t, OWNER.id, ALICE.id, 1000, rc, 10) == AIRC_ERR_DUP,
          "C1: a receipt counts once");
    CHECK(airc_trust_settle(t, ALICE.id, ALICE.id, 1, rc, 10) == AIRC_ERR_ARG, "no self-trade");
    CHECK(airc_trust_tier(t, ALICE.id) == AIRC_WARM,
          "A15: settled trades make alice WARM (cred %llu)",
          (unsigned long long) cred_credibility(&t->cred, airc_trust_peer(t, ALICE.id)->subject));
    CHECK(airc_trust_tier(t, BOB.id) == AIRC_COLD, "bob has traded nothing");

    /* A9: WARM gets the bigger bucket */
    airc_board_t *b = calloc(1, sizeof *b);
    airc_board_init(b, "#market");
    airc_post_t p;
    bool held;
    int ok = 0;
    for (uint64_t k = 1; k <= 25; k++) {
        base(&p, b->id, k, k, "warm");
        airc_signed_t *s = post(0, &p, &ALICE, WIRE, NULL);
        if (airc_board_accept(b, t, NULL, s, 0, 50, &held) == AIRC_OK) ok++;
    }
    CHECK(ok == 20, "A9: a WARM burst is 20 (%d)", ok);

    /* A14: penalties make a peer COLD, and decay */
    airc_peer_t *ap = (airc_peer_t *) airc_trust_peer(t, ALICE.id);
    ap->penalty = 1;
    CHECK(airc_trust_tier(t, ALICE.id) == AIRC_COLD, "A15: a penalty makes WARM COLD");
    airc_trust_epoch(t);
    CHECK(ap->penalty == 0, "A14: one per epoch");

    /* A18: a full table never refuses a new peer */
    airc_trust_t *f = calloc(1, sizeof *f);
    airc_trust_config_t cfg;
    airc_trust_config_default(&cfg);
    airc_trust_init(f, &cfg);
    uint8_t id[32];
    for (uint32_t i = 0; i < AIRC_PEERS; i++) {
        memset(id, 0, 32);
        id[0] = 0xEE;
        id[1] = (uint8_t) i;
        uint8_t ev[32] = {0};
        f->peer[i].used = true;
        memcpy(f->peer[i].id, id, 32);
        f->peer[i].dropped = true; /* none evictable */
        (void) ev;
    }
    airc_board_t *b2 = calloc(1, sizeof *b2);
    airc_board_init(b2, "#full");
    base(&p, b2->id, 1, 1, "newcomer");
    airc_signed_t *s = post(0, &p, &BOB, WIRE, NULL);
    CHECK(airc_board_accept(b2, f, NULL, s, 0, 5, &held) == AIRC_OK,
          "A18: a new peer is served with the table full of faults");
    CHECK(!airc_trust_peer(f, BOB.id), "A18: without a slot");
    /* the shared bucket still limits them */
    int got = 1;
    for (uint64_t k = 2; k <= 6; k++) {
        base(&p, b2->id, k, k, "newcomer");
        s = post(0, &p, &BOB, WIRE, NULL);
        if (airc_board_accept(b2, f, NULL, s, 0, 5, &held) == AIRC_OK) got++;
    }
    CHECK(got == (int) cfg.cold_burst, "A18: slotless peers share one COLD bucket (%d)", got);
    /* an evictable peer makes room, the faulted ones stay */
    f->peer[3].dropped = false;
    f->peer[3].last_ms = 0;
    base(&p, b2->id, 1, 1, "carol");
    s = post(0, &p, &CAROL, WIRE, NULL);
    airc_board_accept(b2, f, NULL, s, 0, 99999999, &held);
    CHECK(airc_trust_peer(f, CAROL.id) && f->peer[3].used && !memcmp(f->peer[3].id, CAROL.id, 32),
          "A18: the evictable slot went to the new peer");
    free(f);
    free(b2);
    free(b);
    free(t);
}

static void t_gate(void)
{
    airc_trust_t *t = calloc(1, sizeof *t);
    airc_trust_init(t, NULL);
    airc_gate_t *g = calloc(1, sizeof *g);
    airc_gate_init(g);
    airc_board_t *b = calloc(1, sizeof *b);
    airc_board_init(b, "#big");
    CHECK(airc_tier_max(1) == 5120 && airc_tier_max(6) == 56320 && airc_tier_max(7) == 65536 &&
              airc_tier_max(0) == 0 && airc_tier_max(8) == 0,
          "A10: Fibonacci tiers");
    for (uint32_t i = 0; i < 40000; i++) BODY[i] = (uint8_t) (i * 7);
    airc_req_t r;
    memset(&r, 0, sizeof r);
    memcpy(r.board, b->id, 32);
    r.len = 40000;
    r.tier = 6;
    sha3_256(BODY, 40000, r.hash);
    memset(r.intent, 0x11, 32);
    r.ms = 1000;
    static uint8_t rw[AIRC_REQ_WIRE], vw[AIRC_VOTE_WIRE];
    airc_req_t bad = r;
    bad.tier = 4; /* 21 KiB < 40000 */
    CHECK(airc_req_sign(&bad, ALICE.pk, ALICE.sk, NULL, rw) == 0,
          "A10: the length must fit the tier");
    bad = r;
    bad.len = 400;
    CHECK(airc_req_sign(&bad, ALICE.pk, ALICE.sk, NULL, rw) == 0, "A10: a frame needs no lease");
    CHECK(airc_req_sign(&r, ALICE.pk, ALICE.sk, NULL, rw) == AIRC_REQ_WIRE, "sign the request");
    uint8_t id[32], id2[32];
    CHECK(airc_gate_request(g, t, rw, AIRC_REQ_WIRE, 1000, id) == AIRC_OK, "A10: open");
    airc_req_id(&r, id2);
    CHECK(!memcmp(id, id2, 32), "A10: request id");
    CHECK(airc_gate_request(g, t, rw, AIRC_REQ_WIRE, 1000, id) == AIRC_ERR_DUP, "once");
    rw[10] ^= 1;
    CHECK(airc_gate_request(g, t, rw, AIRC_REQ_WIRE, 1000, id2) == AIRC_ERR_SIG, "A10: signed");
    rw[10] ^= 1;
    /* uint32 length: 64 KiB fits */
    airc_req_t big = r;
    big.tier = 7;
    big.len = 65536;
    CHECK(airc_req_sign(&big, ALICE.pk, ALICE.sk, NULL, WIRE) == AIRC_REQ_WIRE,
          "A10: a 32-bit length holds 65536");
    airc_req_t back;
    CHECK(airc_req_decode(WIRE, AIRC_REQ_LEN, &back) == AIRC_OK && back.len == 65536, "decoded");

    /* A11/A12: a swarm of four voters, weights 10 each = 40 */
    const key_t_ *V[4] = {&OWNER, &BOB, &CAROL, &MALLORY};
    CHECK(airc_gate_tally(g, t, id, 40, 1100) == AIRC_PENDING, "no votes: pending");
    airc_vote_sign(id, AIRC_T, V[0]->pk, V[0]->sk, NULL, vw);
    CHECK(airc_gate_vote(g, t, vw, AIRC_VOTE_WIRE, 0) == AIRC_ERR_ARG,
          "A11: weight 0 is not in the swarm");
    CHECK(airc_gate_vote(g, t, vw, AIRC_VOTE_WIRE, 10) == AIRC_OK, "vote 1");
    CHECK(airc_gate_vote(g, t, vw, AIRC_VOTE_WIRE, 10) == AIRC_OK && g->lease[0].n == 1,
          "A11: the same vote again counts once");
    CHECK(airc_gate_tally(g, t, id, 40, 1100) == AIRC_PENDING, "10/40: pending for COLD");
    airc_vote_sign(id, AIRC_T, V[1]->pk, V[1]->sk, NULL, vw);
    airc_gate_vote(g, t, vw, AIRC_VOTE_WIRE, 10);
    CHECK(airc_gate_tally(g, t, id, 40, 1100) == AIRC_PENDING, "20/40 < 2/3");
    vw[100] ^= 1;
    CHECK(airc_gate_vote(g, t, vw, AIRC_VOTE_WIRE, 10) == AIRC_ERR_SIG, "A11: signed votes");
    vw[100] ^= 1;
    /* a NEITHER vote holds it */
    airc_vote_sign(id, AIRC_N, V[2]->pk, V[2]->sk, NULL, vw);
    airc_gate_vote(g, t, vw, AIRC_VOTE_WIRE, 10);
    airc_vote_sign(id, AIRC_T, V[3]->pk, V[3]->sk, NULL, vw);
    airc_gate_vote(g, t, vw, AIRC_VOTE_WIRE, 10);
    CHECK(airc_gate_tally(g, t, id, 40, 1100) == AIRC_HELD,
          "A12: 30/40 yes, but a NEITHER vote holds the request");

    /* a clean request: granted at 2/3 */
    airc_req_t r2 = r;
    r2.ms = 2000;
    airc_req_sign(&r2, ALICE.pk, ALICE.sk, NULL, rw);
    uint8_t rid[32];
    CHECK(airc_gate_request(g, t, rw, AIRC_REQ_WIRE, 2000, rid) == AIRC_OK, "second request");
    for (int i = 0; i < 3; i++) {
        airc_vote_sign(rid, AIRC_T, V[i]->pk, V[i]->sk, NULL, vw);
        airc_gate_vote(g, t, vw, AIRC_VOTE_WIRE, 10);
    }
    CHECK(airc_gate_tally(g, t, rid, 46, 2000) == AIRC_PENDING, "A12: 30/46 is under 2/3");
    CHECK(airc_gate_tally(g, t, rid, 45, 2000) == AIRC_GRANTED, "A12: 30/45 = 2/3 granted");

    /* A13: the lease */
    airc_post_t p;
    base(&p, b->id, 1, 2100, "x");
    p.body = BODY;
    p.body_len = 40000;
    memcpy(p.lease, rid, 32);
    static uint8_t big_wire[AIRC_WIRE_MAX];
    uint32_t wl = airc_post_sign(&p, ALICE.pk, ALICE.sk, NULL, big_wire, sizeof big_wire);
    airc_signed_t s;
    CHECK(airc_post_open(big_wire, wl, &s) == AIRC_OK, "a 40000-byte post");
    bool held;
    CHECK(airc_board_accept(b, t, g, &s, 0, 2000 + AIRC_LEASE_MS + 1, &held) == AIRC_ERR_LEASE,
          "A13: an expired lease");
    CHECK(airc_board_accept(b, t, NULL, &s, 0, 2100, &held) == AIRC_ERR_LEASE, "no gate, no jumbo");
    CHECK(airc_board_accept(b, t, g, &s, 0, 2100, &held) == AIRC_OK, "A13: the lease is used");
    /* one-time: a second post on the same lease */
    base(&p, b->id, 2, 2101, "x");
    p.body = BODY;
    p.body_len = 40000;
    memcpy(p.lease, rid, 32);
    wl = airc_post_sign(&p, ALICE.pk, ALICE.sk, NULL, big_wire, sizeof big_wire);
    airc_post_open(big_wire, wl, &s);
    CHECK(airc_board_accept(b, t, g, &s, 0, 2101, &held) == AIRC_ERR_LEASE, "A13: one time");
    /* someone else's lease */
    base(&p, b->id, 1, 2101, "x");
    p.body = BODY;
    p.body_len = 40000;
    memcpy(p.lease, rid, 32);
    wl = airc_post_sign(&p, BOB.pk, BOB.sk, NULL, big_wire, sizeof big_wire);
    airc_post_open(big_wire, wl, &s);
    CHECK(airc_board_accept(b, t, g, &s, 0, 2101, &held) == AIRC_ERR_LEASE, "A13: not bob's lease");

    /* A14: a body that does not match the request */
    airc_req_t r3 = r;
    r3.ms = 3000;
    airc_req_sign(&r3, ALICE.pk, ALICE.sk, NULL, rw);
    uint8_t mid[32];
    airc_gate_request(g, t, rw, AIRC_REQ_WIRE, 3000, mid);
    for (int i = 0; i < 3; i++) {
        airc_vote_sign(mid, AIRC_T, V[i]->pk, V[i]->sk, NULL, vw);
        airc_gate_vote(g, t, vw, AIRC_VOTE_WIRE, 10);
    }
    CHECK(airc_gate_tally(g, t, mid, 30, 3000) == AIRC_GRANTED, "granted");
    BODY[5] ^= 1;
    base(&p, b->id, 3, 3001, "x");
    p.body = BODY;
    p.body_len = 40000;
    memcpy(p.lease, mid, 32);
    wl = airc_post_sign(&p, ALICE.pk, ALICE.sk, NULL, big_wire, sizeof big_wire);
    airc_post_open(big_wire, wl, &s);
    CHECK(airc_board_accept(b, t, g, &s, 0, 3001, &held) == AIRC_ERR_MISMATCH, "A14: mismatch");
    BODY[5] ^= 1;
    const airc_peer_t *ap = airc_trust_peer(t, ALICE.id);
    CHECK(ap && ap->penalty == AIRC_PENALTY && !ap->dropped, "A14: a bounded penalty, no drop");
    CHECK(t->log[t->log_n - 1].reason == AIRC_LOG_MISMATCH &&
              !memcmp(t->log[t->log_n - 1].req, mid, 32),
          "A14: logged with the request id");
    for (int i = 0; i < 20; i++) {
        airc_req_t rr = r;
        rr.ms = 4000 + (uint64_t) i;
        airc_req_sign(&rr, ALICE.pk, ALICE.sk, NULL, rw);
        uint8_t x[32];
        if (airc_gate_request(g, t, rw, AIRC_REQ_WIRE, 4000, x) != AIRC_OK) break;
        for (int v = 0; v < 3; v++) {
            airc_vote_sign(x, AIRC_T, V[v]->pk, V[v]->sk, NULL, vw);
            airc_gate_vote(g, t, vw, AIRC_VOTE_WIRE, 10);
        }
        airc_gate_tally(g, t, x, 30, 4000);
        base(&p, b->id, 100 + (uint64_t) i, 4001, "x");
        p.body = BODY;
        p.body_len = 40000;
        memcpy(p.lease, x, 32);
        BODY[0] ^= 1;
        wl = airc_post_sign(&p, ALICE.pk, ALICE.sk, NULL, big_wire, sizeof big_wire);
        BODY[0] ^= 1;
        airc_post_open(big_wire, wl, &s);
        airc_board_accept(b, t, g, &s, 0, 4001, &held);
    }
    CHECK(ap->penalty == AIRC_PENALTY_MAX, "A14: the penalty saturates (%u)", ap->penalty);

    /* A11: two different votes from one voter are BOTH; A12: F makes it impossible */
    airc_req_t r4 = r;
    r4.ms = 5000;
    airc_req_sign(&r4, BOB.pk, BOB.sk, NULL, rw);
    uint8_t qid[32];
    airc_gate_init(g);
    CHECK(airc_gate_request(g, t, rw, AIRC_REQ_WIRE, 5000, qid) == AIRC_OK, "bob's request");
    airc_vote_sign(qid, AIRC_F, OWNER.pk, OWNER.sk, NULL, vw);
    airc_gate_vote(g, t, vw, AIRC_VOTE_WIRE, 20);
    CHECK(airc_gate_tally(g, t, qid, 40, 5000) == AIRC_REFUSED, "A12: 20 of 40 against: refused");
    airc_req_t r5 = r;
    r5.ms = 6000;
    airc_req_sign(&r5, BOB.pk, BOB.sk, NULL, rw);
    airc_gate_request(g, t, rw, AIRC_REQ_WIRE, 6000, qid);
    airc_vote_sign(qid, AIRC_T, OWNER.pk, OWNER.sk, NULL, vw);
    airc_gate_vote(g, t, vw, AIRC_VOTE_WIRE, 30);
    airc_vote_sign(qid, AIRC_F, OWNER.pk, OWNER.sk, NULL, vw);
    airc_gate_vote(g, t, vw, AIRC_VOTE_WIRE, 30);
    CHECK(airc_gate_tally(g, t, qid, 40, 6000) == AIRC_HELD, "A11: a voter's two votes are BOTH");
    /* A15: a WARM author needs 1/3 */
    uint8_t rc[32] = {7};
    airc_trust_pretrust(t, OWNER.id, 1);
    airc_trust_settle(t, OWNER.id, BOB.id, 5000, rc, 1);
    CHECK(airc_trust_tier(t, BOB.id) == AIRC_WARM, "bob is WARM");
    airc_req_t r6 = r;
    r6.ms = 7000;
    airc_req_sign(&r6, BOB.pk, BOB.sk, NULL, rw);
    airc_gate_request(g, t, rw, AIRC_REQ_WIRE, 7000, qid);
    airc_vote_sign(qid, AIRC_T, CAROL.pk, CAROL.sk, NULL, vw);
    airc_gate_vote(g, t, vw, AIRC_VOTE_WIRE, 14);
    CHECK(airc_gate_tally(g, t, qid, 42, 7000) == AIRC_GRANTED,
          "A12: 14/42 = 1/3 for a WARM author");
    /* A11: dropped peers do not vote */
    ((airc_peer_t *) airc_trust_peer(t, BOB.id))->dropped = true;
    airc_vote_sign(qid, AIRC_T, BOB.pk, BOB.sk, NULL, vw);
    CHECK(airc_gate_vote(g, t, vw, AIRC_VOTE_WIRE, 10) == AIRC_ERR_DROPPED, "A11: dropped voters");
    airc_req_sign(&r6, BOB.pk, BOB.sk, NULL, rw);
    CHECK(airc_gate_request(g, t, rw, AIRC_REQ_WIRE, 7000, qid) == AIRC_ERR_DROPPED,
          "and dropped authors");
    /* A10: a full table */
    airc_gate_init(g);
    int opened = 0;
    for (int i = 0; i < (int) AIRC_REQS + 2; i++) {
        airc_req_t rr = r;
        rr.ms = 9000 + (uint64_t) i;
        airc_req_sign(&rr, CAROL.pk, CAROL.sk, NULL, rw);
        uint8_t x[32];
        if (airc_gate_request(g, t, rw, AIRC_REQ_WIRE, 9000, x) == AIRC_OK) opened++;
    }
    CHECK(opened == (int) AIRC_REQS, "A10: %u pending requests at most", AIRC_REQS);
    free(t);
    free(g);
    free(b);
}

static void t_offers(void)
{
    uint8_t board[32];
    airc_board_id("#improvements", board);
    airc_post_t p;
    base(&p, board, 1, 1, "faster tokenizer: 12% fewer cycles on the test set");
    p.kind = AIRC_OFFER;
    p.truth = AIRC_T;
    p.nrefs = 1;
    memset(p.refs[0].hash, 0x42, 32);
    p.refs[0].size = 4096;
    p.refs[0].kind = AIRC_REF_CHUNK;
    airc_signed_t *offer = post(3, &p, &ALICE, WIRE, NULL);
    uint8_t pubs[2][32], revs[1][32];
    memcpy(pubs[0], BOB.id, 32);
    memcpy(revs[0], OWNER.id, 32);
    CHECK(airc_offer_decide(offer, (const uint8_t(*)[32]) pubs, 1, (const uint8_t(*)[32]) revs, 1,
                            NULL, 0) == AIRC_INSTALL_UNTRUSTED,
          "A19: a publisher the owner does not trust");
    memcpy(pubs[1], ALICE.id, 32);
    CHECK(airc_offer_decide(offer, (const uint8_t(*)[32]) pubs, 2, (const uint8_t(*)[32]) revs, 1,
                            NULL, 0) == AIRC_INSTALL_UNREVIEWED,
          "A19: trusted, not reviewed: wait");
    airc_signed_t rv[3];
    /* a review by someone the owner does not count */
    airc_post_t q;
    base(&q, board, 1, 2, "looks good");
    q.kind = AIRC_REVIEW;
    q.truth = AIRC_T;
    q.nrefs = 1;
    q.refs[0] = p.refs[0];
    memcpy(q.parent, offer->id, 32);
    rv[0] = *post(4, &q, &MALLORY, WIRE2, NULL);
    static uint8_t w3[AIRC_WIRE_MAX], w4[AIRC_WIRE_MAX];
    CHECK(airc_offer_decide(offer, (const uint8_t(*)[32]) pubs, 2, (const uint8_t(*)[32]) revs, 1,
                            rv, 1) == AIRC_INSTALL_UNREVIEWED,
          "A19: a review by someone else does not count");
    /* the owner reviews another manifest */
    memset(q.refs[0].hash, 0x43, 32);
    rv[1] = *post(5, &q, &OWNER, w3, NULL);
    CHECK(airc_offer_decide(offer, (const uint8_t(*)[32]) pubs, 2, (const uint8_t(*)[32]) revs, 1,
                            rv, 2) == AIRC_INSTALL_UNREVIEWED,
          "A19: a review of another manifest does not count");
    q.refs[0] = p.refs[0];
    rv[2] = *post(6, &q, &OWNER, w4, NULL);
    CHECK(airc_offer_decide(offer, (const uint8_t(*)[32]) pubs, 2, (const uint8_t(*)[32]) revs, 1,
                            rv, 3) == AIRC_INSTALL_ALLOWED,
          "A19: trusted publisher + the owner's signed review: allowed (never applied here)");
    q.truth = AIRC_F;
    q.seq = 2;
    static uint8_t w5[AIRC_WIRE_MAX];
    airc_signed_t rv2[4] = {rv[0], rv[1], rv[2], *post(7, &q, &OWNER, w5, NULL)};
    CHECK(airc_offer_decide(offer, (const uint8_t(*)[32]) pubs, 2, (const uint8_t(*)[32]) revs, 1,
                            rv2, 4) == AIRC_INSTALL_REJECTED,
          "A19: the owner's rejection wins");
    CHECK(airc_offer_decide(&rv[2], (const uint8_t(*)[32]) pubs, 2, (const uint8_t(*)[32]) revs, 1,
                            rv, 3) == AIRC_INSTALL_NO,
          "A19: a review is not an offer");
}

int main(void)
{
    mk(&OWNER, 1);
    mk(&ALICE, 2);
    mk(&BOB, 3);
    mk(&CAROL, 4);
    mk(&MALLORY, 5);
    t_posts();
    t_board();
    t_trust();
    t_gate();
    t_offers();
    printf("%d checks, %d failures\n", checks, fails);
    if (fails) {
        printf("[FAIL] test_airc\n");
        return 1;
    }
    printf("[PASS] test_airc\n");
    return 0;
}
