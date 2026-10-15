/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_companion.c — the Chiglet companion (companion.h).
 *
 * Real ML-DSA-87 keys (deterministic seeds) sign every manifest and grant.
 * Checked: manifest encoding (round trip, one encoding per manifest, every
 * byte covered by the digest); start and update rules (owner key, signature,
 * epoch, id, owner handover); grants (sequence, ceiling, signer, replay
 * across epochs) and revokes; the reasoning loop for every action and every
 * gate outcome; goal conflicts carried as Both; the invariant guard
 * (forbidden, rollback, quarantined); spend limits; mood never changing a
 * decision; the token budget; the hash-chained log, tampering and a full log
 * failing closed.
 */
#include <stdio.h>
#include <string.h>
#include "companion.h"
#include "../robin_debanks/sha256.h"
#include "../pqsec/pq_matrix_algs.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static uint8_t pk_a[CMP_PK_BYTES], sk_a[CMP_SK_BYTES];
static uint8_t pk_b[CMP_PK_BYTES], sk_b[CMP_SK_BYTES];
static uint8_t sig[CMP_SIG_BYTES];
static cmp_rec_t logbuf[1024];

static void sign(const uint8_t *sk, const uint8_t msg[32], uint8_t out[CMP_SIG_BYTES])
{
    static const char ctx[] = CMP_SIG_CTX;
    pqm_mldsa87_sign(sk, msg, 32, (const uint8_t *) ctx, sizeof ctx - 1, NULL, out);
}

static void sign_manifest(const uint8_t *sk, const cmp_manifest_t *m)
{
    uint8_t d[32];
    cmp_manifest_digest(m, d);
    sign(sk, d, sig);
}

static void sign_grant(const uint8_t *sk, const companion_t *c, uint32_t caps, uint64_t seq)
{
    uint8_t d[32];
    cmp_grant_digest(c, caps, seq, d);
    sign(sk, d, sig);
}

static cmp_manifest_t base_manifest(void)
{
    cmp_manifest_t m;
    memset(&m, 0, sizeof m);
    m.epoch = 1;
    for (int i = 0; i < 32; i++) m.companion_id[i] = (uint8_t) (0xA0 + i);
    sha256(pk_a, CMP_PK_BYTES, m.owner_pk_hash);
    memcpy(m.name, "Chiglet", 7);
    m.caps_max = CMP_CAP_LEDGER | CMP_CAP_SPEND | CMP_CAP_DEVICE | CMP_CAP_NET | CMP_CAP_VINEA;
    m.max_tokens_cycle = 4096;
    m.auto_spend_max = 1000;
    m.spend_cap = 50000;
    m.mem_slots = 64;
    m.mem_slot_bytes = 4096;
    m.n_models = 2;
    for (int i = 0; i < 32; i++) m.models[0].sha[i] = (uint8_t) i;
    m.models[0].role = CMP_ROLE_LANGUAGE;
    for (int i = 0; i < 32; i++) m.models[1].sha[i] = (uint8_t) (255 - i);
    m.models[1].role = CMP_ROLE_GATE;
    return m;
}

static void setvec(surplus_real_t *v, int a, int b, int c, int d)
{
    v[0] = SR_FROM_INT(a);
    v[1] = SR_FROM_INT(b);
    v[2] = SR_FROM_INT(c);
    v[3] = SR_FROM_INT(d);
    for (uint32_t i = 4; i < CHG_DIM; i++) v[i] = SR_ZERO;
}

static chg_model_t gate_model(void)
{
    chg_model_t m;
    memset(&m, 0, sizeof m);
    m.K = 4;
    m.D = 4;
    m.L = 2;
    m.epoch = 1;
    setvec(m.proto[0], 1, 0, 0, 0); /* support */
    setvec(m.proto[1], 0, 1, 0, 0); /* oppose */
    m.R_min = SR_FROM_FLOAT(1.5);
    m.margin_min = SR_FROM_FLOAT(0.2);
    memcpy(m.label_name[0], "support", 8);
    memcpy(m.label_name[1], "oppose", 7);
    return m;
}

/* Evidence sets: three independent experts for, three against, three that
 * split (conflict), four copies of one (redundant). */
static surplus_real_t ev_for[3][CHG_DIM], ev_against[3][CHG_DIM], ev_split[3][CHG_DIM],
    ev_same[4][CHG_DIM];

static void make_evidence(void)
{
    setvec(ev_for[0], 1, 0, 1, 0);
    setvec(ev_for[1], 1, 0, 0, 1);
    setvec(ev_for[2], 1, 0, -1, 0);
    setvec(ev_against[0], 0, 1, 1, 0);
    setvec(ev_against[1], 0, 1, 0, 1);
    setvec(ev_against[2], 0, 1, -1, 0);
    setvec(ev_split[0], 1, 0, 0, 0);
    setvec(ev_split[1], 0, 1, 0, 0);
    setvec(ev_split[2], 0, 0, 1, 0);
    for (int i = 0; i < 4; i++) setvec(ev_same[i], 1, 0, 0, 0);
}

static cmp_proposal_t prop(cmp_action_t a, const surplus_real_t (*ev)[CHG_DIM], uint32_t k)
{
    cmp_proposal_t p;
    memset(&p, 0, sizeof p);
    p.action = a;
    p.ev = ev;
    p.k = k;
    p.digest[0] = (uint8_t) a;
    p.pos0 = 2; /* steady evidence: neutral polarity */
    p.pos1 = 2;
    return p;
}

static bool inv_true(const cmp_proposal_t *p, void *ctx)
{
    (void) p;
    (void) ctx;
    return true;
}
static bool inv_false(const cmp_proposal_t *p, void *ctx)
{
    (void) p;
    if (ctx) (*(int *) ctx)++;
    return false;
}

static void start(companion_t *c, uint32_t cap)
{
    cmp_manifest_t m = base_manifest();
    sign_manifest(sk_a, &m);
    cmp_status_t rc = cmp_start(c, &m, pk_a, sig, logbuf, cap);
    CHECK(rc == CMP_OK, "start (%d)", rc);
}

static void grant(companion_t *c, uint32_t caps)
{
    sign_grant(sk_a, c, caps, c->grant_seq);
    cmp_status_t rc = cmp_grant(c, caps, c->grant_seq, sig);
    CHECK(rc == CMP_OK, "grant 0x%x (%d)", caps, rc);
}

static void test_manifest(void)
{
    cmp_manifest_t m = base_manifest(), back;
    uint8_t e[CMP_MANIFEST_BYTES], e2[CMP_MANIFEST_BYTES];
    cmp_manifest_encode(&m, e);
    CHECK(cmp_manifest_decode(e, &back) == CMP_OK, "decode");
    cmp_manifest_encode(&back, e2);
    CHECK(!memcmp(e, e2, sizeof e), "round trip is byte-identical");
    uint8_t d0[32], d1[32];
    cmp_manifest_digest(&m, d0);
    /* Every byte is covered: a flipped byte either fails to decode or
     * changes the digest. */
    int uncovered = 0, rejected = 0;
    for (uint32_t i = 0; i < CMP_MANIFEST_BYTES; i++) {
        memcpy(e2, e, sizeof e);
        e2[i] ^= 0x01;
        if (cmp_manifest_decode(e2, &back) != CMP_OK) {
            rejected++;
            continue;
        }
        cmp_manifest_digest(&back, d1);
        if (!memcmp(d0, d1, 32)) uncovered++;
    }
    CHECK(uncovered == 0, "%d manifest bytes not covered by the digest", uncovered);
    printf("  manifest byte flips: %d refused by decode, %d change the digest\n", rejected,
           (int) CMP_MANIFEST_BYTES - rejected);
    memcpy(e2, e, sizeof e);
    e2[0] ^= 1;
    CHECK(cmp_manifest_decode(e2, &back) == CMP_ERR_FORMAT, "bad magic");
    cmp_manifest_t x = m;
    x.n_models = 5;
    cmp_manifest_encode(&x, e2);
    CHECK(cmp_manifest_decode(e2, &back) == CMP_ERR_FORMAT, "five models refused");
    x = m;
    x.caps_max = 0x40;
    cmp_manifest_encode(&x, e2);
    CHECK(cmp_manifest_decode(e2, &back) == CMP_ERR_FORMAT, "unknown capability refused");
    x = m;
    x.auto_spend_max = x.spend_cap + 1;
    cmp_manifest_encode(&x, e2);
    CHECK(cmp_manifest_decode(e2, &back) == CMP_ERR_FORMAT, "auto limit above hard cap refused");
    x = m;
    x.models[1].role = 9;
    cmp_manifest_encode(&x, e2);
    CHECK(cmp_manifest_decode(e2, &back) == CMP_ERR_FORMAT, "unknown model role refused");
    memcpy(e2, e, sizeof e);
    e2[152 + 36 * 3 + 5] = 1; /* an unused model slot */
    CHECK(cmp_manifest_decode(e2, &back) == CMP_ERR_FORMAT, "non-zero unused slot refused");
}

static void test_start_update(void)
{
    companion_t c;
    cmp_manifest_t m = base_manifest();
    sign_manifest(sk_a, &m);
    CHECK(cmp_start(&c, &m, pk_b, sig, logbuf, 256) == CMP_ERR_OWNER, "wrong owner key");
    CHECK(!c.live, "not live after a refused start");
    sign_manifest(sk_b, &m);
    CHECK(cmp_start(&c, &m, pk_a, sig, logbuf, 256) == CMP_ERR_SIG, "signed by someone else");
    sign_manifest(sk_a, &m);
    cmp_manifest_t t = m;
    t.spend_cap = 1000000;
    CHECK(cmp_start(&c, &t, pk_a, sig, logbuf, 256) == CMP_ERR_SIG, "tampered after signing");
    CHECK(cmp_start(&c, &m, pk_a, sig, logbuf, 0) == CMP_ERR_ARG, "no log storage");
    CHECK(cmp_start(&c, &m, pk_a, sig, logbuf, 256) == CMP_OK, "good start");
    CHECK(c.granted == 0 && c.log.n == 1 && c.log.rec[0].kind == CMP_REC_BORN,
          "starts with no capabilities, one BORN record");

    grant(&c, CMP_CAP_LEDGER | CMP_CAP_SPEND | CMP_CAP_NET);
    cmp_manifest_t u = m;
    CHECK((sign_manifest(sk_a, &u), cmp_update_manifest(&c, &u, NULL, sig)) == CMP_ERR_EPOCH,
          "same epoch refused");
    u.epoch = 2;
    u.companion_id[0] ^= 1;
    sign_manifest(sk_a, &u);
    CHECK(cmp_update_manifest(&c, &u, NULL, sig) == CMP_ERR_ID, "companion id cannot change");
    u = m;
    u.epoch = 2;
    u.caps_max = CMP_CAP_LEDGER | CMP_CAP_NET; /* drop SPEND from the ceiling */
    sign_manifest(sk_b, &u);
    CHECK(cmp_update_manifest(&c, &u, NULL, sig) == CMP_ERR_SIG, "update signed by a stranger");
    sign_manifest(sk_a, &u);
    CHECK(cmp_update_manifest(&c, &u, NULL, sig) == CMP_OK, "update by the owner");
    CHECK(c.granted == (CMP_CAP_LEDGER | CMP_CAP_NET), "grants above the new ceiling dropped");

    /* Owner handover: the old owner signs a manifest naming the new key. */
    cmp_manifest_t h = u;
    h.epoch = 3;
    sha256(pk_b, CMP_PK_BYTES, h.owner_pk_hash);
    sign_manifest(sk_b, &h);
    CHECK(cmp_update_manifest(&c, &h, pk_b, sig) == CMP_ERR_SIG,
          "the new owner cannot take over by signing alone");
    sign_manifest(sk_a, &h);
    CHECK(cmp_update_manifest(&c, &h, NULL, sig) == CMP_ERR_OWNER, "handover needs the new key");
    CHECK(cmp_update_manifest(&c, &h, pk_a, sig) == CMP_ERR_OWNER,
          "handover key must match the new owner hash");
    CHECK(cmp_update_manifest(&c, &h, pk_b, sig) == CMP_OK, "handover signed by the old owner");
    CHECK(c.granted == 0, "a new owner grants afresh");
    sign_grant(sk_a, &c, CMP_CAP_NET, c.grant_seq);
    CHECK(cmp_grant(&c, CMP_CAP_NET, c.grant_seq, sig) == CMP_ERR_SIG, "old owner cannot grant");
    sign_grant(sk_b, &c, CMP_CAP_NET, c.grant_seq);
    CHECK(cmp_grant(&c, CMP_CAP_NET, c.grant_seq, sig) == CMP_OK, "new owner grants");
    CHECK(cmp_log_verify(&c.log), "log verifies after updates");
}

static void test_grants(void)
{
    companion_t c;
    start(&c, 256);
    sign_grant(sk_a, &c, CMP_CAP_NET, 0);
    CHECK(cmp_grant(&c, CMP_CAP_NET, 1, sig) == CMP_ERR_SEQ, "skipped sequence number");
    CHECK(cmp_grant(&c, CMP_CAP_NET, 0, sig) == CMP_OK, "grant NET");
    CHECK(cmp_grant(&c, CMP_CAP_NET, 0, sig) == CMP_ERR_SEQ, "replayed grant refused");
    sign_grant(sk_a, &c, CMP_CAP_MEM_EXPORT, 1);
    CHECK(cmp_grant(&c, CMP_CAP_MEM_EXPORT, 1, sig) == CMP_ERR_CEILING,
          "grant beyond the manifest ceiling");
    sign_grant(sk_a, &c, CMP_CAP_DEVICE, 1);
    CHECK(cmp_grant(&c, CMP_CAP_DEVICE | CMP_CAP_SPEND, 1, sig) == CMP_ERR_SIG,
          "signature for other capabilities");
    sign_grant(sk_b, &c, CMP_CAP_DEVICE, 1);
    CHECK(cmp_grant(&c, CMP_CAP_DEVICE, 1, sig) == CMP_ERR_SIG, "grant by a stranger");
    /* A grant signed for epoch 1 does not work after the manifest moves on. */
    sign_grant(sk_a, &c, CMP_CAP_DEVICE, 1);
    cmp_manifest_t u = c.man;
    u.epoch = 2;
    uint8_t keep[CMP_SIG_BYTES];
    memcpy(keep, sig, sizeof keep);
    sign_manifest(sk_a, &u);
    CHECK(cmp_update_manifest(&c, &u, NULL, sig) == CMP_OK, "epoch 2");
    CHECK(cmp_grant(&c, CMP_CAP_DEVICE, 1, keep) == CMP_ERR_SIG,
          "a grant signed under the old epoch is refused");
    CHECK(cmp_revoke(&c, CMP_CAP_NET) == CMP_OK && c.granted == 0, "revoke needs no signature");
    CHECK(c.log.rec[c.log.n - 1].kind == CMP_REC_REVOKE, "revoke logged");
    CHECK(cmp_grant(&c, 0, c.grant_seq, sig) == CMP_ERR_ARG, "empty grant refused");
}

static void test_join(void)
{
    /* (for, against) pairs ORed: the expected table written out. */
    static const uint8_t T = LPX_B_TRUE, F = LPX_B_FALSE, B = LPX_B_BOTH, N = LPX_B_NEITHER;
    static const uint8_t want[4][4] = {
        /* a = FALSE  */ [LPX_B_FALSE] =
            {[LPX_B_FALSE] = F, [LPX_B_NEITHER] = F, [LPX_B_BOTH] = B, [LPX_B_TRUE] = B},
        [LPX_B_NEITHER] =
            {[LPX_B_FALSE] = F, [LPX_B_NEITHER] = N, [LPX_B_BOTH] = B, [LPX_B_TRUE] = T},
        [LPX_B_BOTH] = {[LPX_B_FALSE] = B, [LPX_B_NEITHER] = B, [LPX_B_BOTH] = B, [LPX_B_TRUE] = B},
        [LPX_B_TRUE] = {
            [LPX_B_FALSE] = B, [LPX_B_NEITHER] = T, [LPX_B_BOTH] = B, [LPX_B_TRUE] = T}};
    int bad = 0;
    for (uint8_t a = 0; a < 4; a++)
        for (uint8_t b = 0; b < 4; b++)
            if (cmp_base_join(a, b) != want[a][b]) bad++;
    CHECK(bad == 0, "knowledge join table (%d entries wrong)", bad);
}

static void expect(companion_t *c, const cmp_proposal_t *p, cmp_invariant_fn inv, void *ctx,
                   cmp_verdict_t v, cmp_why_t w, const char *what)
{
    cmp_decision_t d;
    cmp_status_t rc = cmp_decide(c, p, inv, ctx, &d);
    CHECK(rc == CMP_OK && d.verdict == v && d.why == w,
          "%s: got %s / %s (tag %s/%s, gate %s), want %s / %s", what, cmp_verdict_name(d.verdict),
          cmp_why_name(d.why), lpx_base_name(lpx_base(d.tag)), lpx_pol_name(lpx_pol(d.tag)),
          chg_state_name(d.gate), cmp_verdict_name(v), cmp_why_name(w));
}

static void test_decide(void)
{
    companion_t c;
    start(&c, 256);
    cmp_proposal_t p;

    /* No gate model yet: every answer is unknown, and unknown waits. */
    p = prop(CMP_ACT_REPLY, ev_for, 3);
    expect(&c, &p, NULL, NULL, CMP_WAIT, CMP_WHY_UNKNOWN, "no gate model");
    chg_model_t gm = gate_model();
    CHECK(cmp_load_gate(&c, &gm) == CMP_OK, "load gate");

    expect(&c, &p, NULL, NULL, CMP_PROCEED, CMP_WHY_OK, "reply, evidence for");
    p = prop(CMP_ACT_REPLY, ev_against, 3);
    expect(&c, &p, NULL, NULL, CMP_REFUSE, CMP_WHY_OPPOSED, "reply, evidence against");
    p = prop(CMP_ACT_REPLY, ev_split, 3);
    expect(&c, &p, NULL, NULL, CMP_ASK, CMP_WHY_CONFLICT, "split evidence (Both, steady)");
    p.pos0 = 2, p.neg0 = 2, p.pos1 = 4, p.neg1 = 1; /* converging */
    expect(&c, &p, NULL, NULL, CMP_WAIT, CMP_WHY_CONFLICT, "split evidence, converging");
    p = prop(CMP_ACT_REPLY, ev_same, 4);
    expect(&c, &p, NULL, NULL, CMP_WAIT, CMP_WHY_UNKNOWN, "four copies of one expert");
    p = prop(CMP_ACT_REPLY, ev_for, 0);
    expect(&c, &p, NULL, NULL, CMP_WAIT, CMP_WHY_UNKNOWN, "no evidence");
    p = prop(CMP_ACT_REPLY, ev_for, 3);
    p.pos0 = 5, p.neg0 = 0, p.pos1 = 5, p.neg1 = 3; /* margin shrinking */
    expect(&c, &p, NULL, NULL, CMP_WAIT, CMP_WHY_WEAKENING, "true but weakening");
    p = prop(CMP_ACT_REPLY, ev_for, 3);
    p.stale = true;
    expect(&c, &p, NULL, NULL, CMP_WAIT, CMP_WHY_WEAKENING, "true but stale");

    /* Goals: conflicts are carried as Both, never dropped. */
    p = prop(CMP_ACT_REPLY, ev_for, 3);
    p.n_goals = 2;
    p.goal[0] = LPX_B_TRUE;
    p.goal[1] = LPX_B_NEITHER;
    expect(&c, &p, NULL, NULL, CMP_PROCEED, CMP_WHY_OK, "goals agree");
    p.goal[1] = LPX_B_FALSE;
    expect(&c, &p, NULL, NULL, CMP_ASK, CMP_WHY_CONFLICT, "one goal against: Both, ask");
    p = prop(CMP_ACT_REPLY, ev_against, 3);
    p.n_goals = 1;
    p.goal[0] = LPX_B_TRUE;
    expect(&c, &p, NULL, NULL, CMP_ASK, CMP_WHY_CONFLICT, "goal for, gate against: ask");
    p = prop(CMP_ACT_REPLY, ev_for, 0);
    p.n_goals = 1;
    p.goal[0] = LPX_B_TRUE;
    expect(&c, &p, NULL, NULL, CMP_PROCEED, CMP_WHY_OK, "a goal alone can carry a reply");
    p.goal[0] = 9; /* out of range: masked to a base, never above TRUE's rights */
    p.n_goals = 9;
    expect(&c, &p, NULL, NULL, CMP_REFUSE, CMP_WHY_BAD_INPUT, "too many goals");

    /* Capabilities: every action that needs one is refused without it. */
    for (int a = 0; a < CMP_ACT_COUNT; a++) {
        p = prop((cmp_action_t) a, ev_for, 3);
        cmp_decision_t d;
        cmp_decide(&c, &p, inv_true, NULL, &d);
        if (cmp_action_caps((cmp_action_t) a))
            CHECK(d.verdict == CMP_REFUSE && d.why == CMP_WHY_NO_CAP, "action %d without caps", a);
        else
            CHECK(d.verdict == CMP_PROCEED, "action %d needs no capability", a);
    }
    grant(&c, CMP_CAP_LEDGER); /* SPEND still missing */
    p = prop(CMP_ACT_SPEND, ev_for, 3);
    p.amount = 10;
    expect(&c, &p, inv_true, NULL, CMP_REFUSE, CMP_WHY_NO_CAP, "spend needs SPEND and LEDGER");
    grant(&c, CMP_CAP_SPEND | CMP_CAP_DEVICE | CMP_CAP_NET | CMP_CAP_VINEA);
    for (int a = 0; a < CMP_ACT_COUNT; a++) {
        if (a == CMP_ACT_MEMORY_EXPORT) continue;
        p = prop((cmp_action_t) a, ev_for, 3);
        p.amount = 10;
        cmp_decision_t d;
        cmp_decide(&c, &p, inv_true, NULL, &d);
        CHECK(d.verdict == CMP_PROCEED, "action %d proceeds once granted (%s)", a,
              cmp_why_name(d.why));
    }

    /* Invariant guard on money and devices. */
    int calls = 0;
    static const cmp_action_t guarded[3] = {CMP_ACT_LEDGER_POST, CMP_ACT_SPEND, CMP_ACT_DEVICE};
    for (int i = 0; i < 3; i++) {
        p = prop(guarded[i], ev_for, 3);
        p.amount = 10;
        cmp_decision_t d;
        cmp_decide(&c, &p, inv_false, &calls, &d);
        CHECK(d.verdict == CMP_REFUSE && d.why == CMP_WHY_INVARIANT && d.quarantined &&
                  lpx_forbidden(d.tag) && d.lpx == LPX_ACT_ROLLBACK,
              "guarded action %d with a failed invariant: forbidden, rolled back", guarded[i]);
        cmp_decide(&c, &p, NULL, NULL, &d);
        CHECK(d.verdict == CMP_REFUSE && d.quarantined, "guarded action %d with no invariant check",
              guarded[i]);
    }
    CHECK(calls == 3, "the invariant ran once per guarded proposal (%d)", calls);
    p = prop(CMP_ACT_NET, ev_for, 3);
    expect(&c, &p, inv_false, &calls, CMP_PROCEED, CMP_WHY_OK, "unguarded action ignores it");
    CHECK(calls == 3, "and does not call it");
    p = prop(CMP_ACT_SPEND, ev_against, 3);
    p.amount = 10;
    expect(&c, &p, inv_false, NULL, CMP_REFUSE, CMP_WHY_INVARIANT,
           "opposed and invariant-breaking: quarantined");

    /* Spend limits. */
    p = prop(CMP_ACT_SPEND, ev_for, 3);
    p.amount = 1000;
    expect(&c, &p, inv_true, NULL, CMP_PROCEED, CMP_WHY_OK, "spend at the automatic limit");
    p.amount = 1001;
    expect(&c, &p, inv_true, NULL, CMP_ASK, CMP_WHY_OWNER_LIMIT, "spend above it asks");
    p.amount = 50001;
    expect(&c, &p, inv_true, NULL, CMP_REFUSE, CMP_WHY_OVER_CAP, "spend above the hard cap");
    p = prop(CMP_ACT_SPEND, ev_split, 3);
    p.amount = 5;
    expect(&c, &p, inv_true, NULL, CMP_ASK, CMP_WHY_CONFLICT, "a conflicted spend asks");

    p = prop((cmp_action_t) 99, ev_for, 3);
    expect(&c, &p, inv_true, NULL, CMP_REFUSE, CMP_WHY_BAD_INPUT, "unknown action");
    p = prop(CMP_ACT_REPLY, ev_for, 9);
    expect(&c, &p, inv_true, NULL, CMP_REFUSE, CMP_WHY_BAD_INPUT, "too many experts");
    p = prop(CMP_ACT_REPLY, NULL, 2);
    expect(&c, &p, inv_true, NULL, CMP_REFUSE, CMP_WHY_BAD_INPUT, "evidence count without data");

    CHECK(cmp_log_verify(&c.log), "log verifies");
    const cmp_rec_t *last = &c.log.rec[c.log.n - 1];
    CHECK(last->kind == CMP_REC_DECIDE && last->verdict == CMP_REFUSE, "last decision logged");
    cmp_decision_t d;
    p = prop(CMP_ACT_REPLY, ev_for, 3);
    cmp_decide(&c, &p, NULL, NULL, &d);
    CHECK(c.log.rec[d.seq].r_permille >= 1500 && c.log.rec[d.seq].r_permille <= 3000,
          "the gate's R is logged (%u per mille)", c.log.rec[d.seq].r_permille);
    CHECK(c.log.rec[d.seq].tag == d.tag && c.log.rec[d.seq].subject[0] == CMP_ACT_REPLY,
          "tag and proposal digest logged");
    c.log.rec[3].verdict ^= 1;
    CHECK(!cmp_log_verify(&c.log), "a tampered record breaks the chain");
    c.log.rec[3].verdict ^= 1;
    CHECK(cmp_log_verify(&c.log), "restored");
}

static void test_mood(void)
{
    companion_t c;
    start(&c, 1024);
    chg_model_t gm = gate_model();
    cmp_load_gate(&c, &gm);
    grant(&c, CMP_CAP_LEDGER | CMP_CAP_SPEND);
    const surplus_real_t(*sets[4])[CHG_DIM] = {ev_for, ev_against, ev_split, ev_same};
    const uint32_t ks[4] = {3, 3, 3, 4};
    cmp_decision_t ref[4][3], d;
    static const cmp_action_t acts[3] = {CMP_ACT_REPLY, CMP_ACT_SPEND, CMP_ACT_DEVICE};
    for (int s = 0; s < 4; s++)
        for (int a = 0; a < 3; a++) {
            cmp_proposal_t p = prop(acts[a], sets[s], ks[s]);
            p.amount = 2000;
            cmp_decide(&c, &p, inv_true, NULL, &ref[s][a]);
        }
    int diff = 0;
    for (int e = 0; e < SWARM_EMO_COUNT; e++)
        for (uint8_t in = 0; in <= SWARM_EMO_MAX_INTENSITY; in++) {
            swarm_feeling_t f = {(swarm_emotion_t) e, in};
            CHECK(cmp_set_mood(&c, f) == CMP_OK, "set mood %d/%u", e, in);
            for (int s = 0; s < 4; s++)
                for (int a = 0; a < 3; a++) {
                    cmp_proposal_t p = prop(acts[a], sets[s], ks[s]);
                    p.amount = 2000;
                    cmp_decide(&c, &p, inv_true, NULL, &d);
                    if (d.verdict != ref[s][a].verdict || d.why != ref[s][a].why ||
                        d.tag != ref[s][a].tag)
                        diff++;
                }
        }
    CHECK(diff == 0, "mood changed %d decisions (it must change none)", diff);
    swarm_feeling_t bad = {SWARM_EMO_COUNT, 1};
    CHECK(cmp_set_mood(&c, bad) == CMP_ERR_ARG, "unknown emotion");
    bad.emotion = SWARM_EMO_JOY;
    bad.intensity = 6;
    CHECK(cmp_set_mood(&c, bad) == CMP_ERR_ARG, "intensity above 5");

    swarm_feeling_t f = {SWARM_EMO_NEUTRAL, 0};
    cmp_set_mood(&c, f);
    CHECK(cmp_token_budget(&c, 2100) == 2100, "calm budget unchanged");
    f.emotion = SWARM_EMO_CURIOUS;
    f.intensity = 5; /* charge 8 */
    cmp_set_mood(&c, f);
    CHECK(cmp_token_budget(&c, 2100) == 2900, "intensity 5: 2100 * 29 / 21 = 2900 (%u)",
          cmp_token_budget(&c, 2100));
    CHECK(cmp_token_budget(&c, 4000) == 4096, "capped at the manifest ceiling");
    CHECK(cmp_tone(&c)->codepoint == 0x1F914, "tone follows the mood (curious)");
}

static void test_log_full(void)
{
    companion_t c;
    start(&c, 3); /* BORN + one grant + one decision */
    chg_model_t gm = gate_model();
    cmp_load_gate(&c, &gm);
    grant(&c, CMP_CAP_NET);
    cmp_proposal_t p = prop(CMP_ACT_NET, ev_for, 3);
    cmp_decision_t d;
    CHECK(cmp_decide(&c, &p, NULL, NULL, &d) == CMP_OK && d.verdict == CMP_PROCEED, "last slot");
    CHECK(cmp_decide(&c, &p, NULL, NULL, &d) == CMP_ERR_LOG && d.verdict == CMP_REFUSE &&
              d.why == CMP_WHY_LOG_FULL,
          "full log: refuse");
    sign_grant(sk_a, &c, CMP_CAP_LEDGER, c.grant_seq);
    CHECK(cmp_grant(&c, CMP_CAP_LEDGER, c.grant_seq, sig) == CMP_ERR_LOG &&
              !(c.granted & CMP_CAP_LEDGER),
          "full log: no new power");
    CHECK(cmp_revoke(&c, CMP_CAP_NET) == CMP_ERR_LOG && c.granted == 0,
          "full log: revoke still removes power");
    CHECK(cmp_log_verify(&c.log) && c.log.n == 3, "log intact");
}

int main(void)
{
    uint8_t seed[32];
    for (int i = 0; i < 32; i++) seed[i] = (uint8_t) i;
    pqm_mldsa87_keygen(seed, pk_a, sk_a);
    seed[0] = 0xFF;
    pqm_mldsa87_keygen(seed, pk_b, sk_b);
    make_evidence();
    test_manifest();
    test_start_update();
    test_grants();
    test_join();
    test_decide();
    test_mood();
    test_log_full();
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
