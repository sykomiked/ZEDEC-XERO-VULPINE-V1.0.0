/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_pay_bounty.c — host tests for pay_bounty (build with -DTEST_HOST;
 * libc is used by the TEST only). Every rule B1-B9 and invariant I1-I4 in
 * pay_bounty.h has checks here, including the refusals.
 */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "pay_bounty.h"

static int g_fail, g_pass;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            g_pass++;                                                                              \
        else {                                                                                     \
            g_fail++;                                                                              \
            if (g_fail < 60) printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                   \
        }                                                                                          \
    } while (0)

#define ARBITER 900u

/* toy signature for tests only: sig = SHA3(key(signer) || digest) */
static void toy_sign(uint32_t signer, const uint8_t d[32], uint8_t sig[32])
{
    pay_hbuf h;
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "TOY-KEY");
    pay_hbuf_u64(&h, signer * 2654435761u);
    pay_hbuf_put(&h, d, 32);
    pay_hbuf_final(&h, sig);
}
static bool toy_verify(void *ctx, uint32_t signer, const uint8_t d[32], const uint8_t *sig,
                       size_t sl)
{
    uint8_t w[32];
    (void) ctx;
    if (sl != 32) return false;
    toy_sign(signer, d, w);
    return memcmp(w, sig, 32) == 0;
}

/* the audit is decided by the deliverable's first byte: P pass, F fail,
 * U unavailable; it also counts its calls */
static int g_audits;
static pay_bty_audit_t toy_audit(void *ctx, uint8_t kind, const uint8_t subject[32],
                                 const uint8_t deliverable[32])
{
    (void) ctx;
    (void) kind;
    (void) subject;
    g_audits++;
    if (deliverable[0] == 'F') return PAY_BTY_AUDIT_FAIL;
    if (deliverable[0] == 'U') return PAY_BTY_AUDIT_UNAVAILABLE;
    return PAY_BTY_AUDIT_PASS;
}

static void subj(uint8_t s[32], uint32_t n)
{
    memset(s, 0, 32);
    snprintf((char *) s, 32, "issue-%" PRIu32, n);
}
static uint32_t g_dctr;
static void dlv(uint8_t d[32], char mode)
{
    memset(d, 0, 32);
    snprintf((char *) d, 32, "%c-patch-%" PRIu32, mode, ++g_dctr);
}
static pay_status_t deliver(pay_bounty_ctx_t *B, uint32_t id, uint32_t fixer, const uint8_t d[32])
{
    uint8_t dg[32], sig[32];
    pay_bounty_delivery_digest(id, fixer, d, dg);
    toy_sign(fixer, dg, sig);
    return pay_bounty_deliver(B, id, fixer, d, sig, 32);
}
static pay_status_t vote(pay_bounty_ctx_t *B, uint32_t id, uint32_t r, bool ok, uint8_t tier)
{
    const pay_bounty_t *b = pay_bounty_get(B, id);
    uint8_t dg[32], sig[32];
    pay_bounty_vote_digest(id, b->deliverable, r, ok, tier, dg);
    toy_sign(r, dg, sig);
    return pay_bounty_vote(B, id, r, ok, tier, sig, 32);
}
static pay_status_t rule(pay_bounty_ctx_t *B, uint32_t id, bool ok, uint8_t tier, uint32_t signer)
{
    const pay_bounty_t *b = pay_bounty_get(B, id);
    uint8_t dg[32], sig[32];
    pay_bounty_rule_digest(id, b->deliverable, ok, tier, dg);
    toy_sign(signer, dg, sig);
    return pay_bounty_rule(B, id, ok, tier, sig, 32);
}
static pay_status_t fraud(pay_bounty_ctx_t *B, uint32_t id, uint32_t signer)
{
    const pay_bounty_t *b = pay_bounty_get(B, id);
    uint8_t dg[32], sig[32];
    pay_bounty_fraud_digest(id, b->deliverable, dg);
    toy_sign(signer, dg, sig);
    return pay_bounty_fraud(B, id, sig, 32);
}
static const uint8_t k_beacon[32] = "beacon-published-after-cutoff";

/* all panel votes the same way */
static void panel_votes(pay_bounty_ctx_t *B, uint32_t id, uint32_t n_ok, uint32_t n_no,
                        uint8_t tier)
{
    const pay_bounty_t *b = pay_bounty_get(B, id);
    uint32_t k = 0;
    for (uint32_t i = 0; i < n_ok; i++, k++) CHECK(vote(B, id, b->panel[k], true, tier) == PAY_OK);
    for (uint32_t i = 0; i < n_no; i++, k++) CHECK(vote(B, id, b->panel[k], false, 0) == PAY_OK);
}

static pay_ledger_t g_L;
static pay_bounty_ctx_t g_B;
static uint32_t g_fbk[PAY_ASSURE_BUCKETS];

static void setup(pay_bty_cfg_t *cfg)
{
    pay_ledger_init(&g_L, 0);
    uint32_t dummy;
    CHECK(pay_ledger_open(&g_L, 4000, 0, PAY_CAP_FINANCIAL, 0, &dummy) == PAY_OK);
    for (int b = 0; b < PAY_ASSURE_BUCKETS; b++)
        CHECK(pay_ledger_open(&g_L, 999 - (uint32_t) b, 0, PAY_CAP_FINANCIAL, PAY_ACCT_COMMONS,
                              &g_fbk[b]) == PAY_OK);
    pay_bounty_cfg_default(cfg);
    cfg->base[PAY_BTY_BUG] = 1000;
    cfg->base[PAY_BTY_SECURITY] = 3000;
    cfg->base[PAY_BTY_IMPROVEMENT] = 800;
    cfg->epoch_cap = 100000;
    cfg->arbiter = ARBITER;
}

static uint64_t debit(uint32_t acct)
{
    const pay_account_t *a = pay_ledger_account(&g_L, acct);
    return a ? a->debit : 0;
}

static void test_cfg_and_value(void)
{
    pay_bty_cfg_t c;
    pay_bounty_cfg_default(&c);
    CHECK(c.fixer_share.num == 8 && c.fixer_share.den == 21);
    CHECK(c.panel_size == 5 && c.quorum == 3);
    CHECK(c.max_open == 8 && c.max_active == 3 && c.max_strikes == 3);
    CHECK(!c.allow_self_fix && c.epoch_cap == 0);
    for (int k = 0; k < PAY_BTY_KIND_COUNT; k++) CHECK(c.base[k] == 0);
    c.base[PAY_BTY_BUG] = 100;
    CHECK(pay_bounty_value(&c, PAY_BTY_BUG, 1) == 100);
    CHECK(pay_bounty_value(&c, PAY_BTY_BUG, 2) == 200);
    CHECK(pay_bounty_value(&c, PAY_BTY_BUG, 3) == 300);
    CHECK(pay_bounty_value(&c, PAY_BTY_BUG, 4) == 500);
    CHECK(pay_bounty_value(&c, PAY_BTY_BUG, 5) == 800);
    CHECK(pay_bounty_value(&c, PAY_BTY_BUG, 0) == 0);
    CHECK(pay_bounty_value(&c, PAY_BTY_BUG, 6) == 0);
    CHECK(pay_bounty_value(&c, PAY_BTY_DOCS, 1) == 0); /* closed kind */
    c.epoch_cap = 777;                                 /* sits right after base[] */
    CHECK(pay_bounty_value(&c, PAY_BTY_KIND_COUNT, 1) == 0);
    c.epoch_cap = 0;
    CHECK(pay_bounty_value(0, PAY_BTY_BUG, 1) == 0);
    c.base[PAY_BTY_BUG] = PAY_BAL_MAX / 8; /* tier 5 reaches PAY_BAL_MAX */
    CHECK(pay_bounty_value(&c, PAY_BTY_BUG, 5) == 0);
    CHECK(pay_bounty_value(&c, PAY_BTY_BUG, 4) == (PAY_BAL_MAX / 8) * 5);
    c.base[PAY_BTY_BUG] = UINT64_MAX;
    CHECK(pay_bounty_value(&c, PAY_BTY_BUG, 2) == 0);
}

static void test_init(void)
{
    pay_bty_cfg_t c, x;
    setup(&c);
    x = c;
    x.epoch_cap = 0;
    CHECK(pay_bounty_init(&g_B, &g_L, &x, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_ERR_ARG);
    x = c;
    x.epoch_cap = PAY_BAL_MAX;
    CHECK(pay_bounty_init(&g_B, &g_L, &x, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_ERR_ARG);
    x = c;
    x.fixer_share.den = 0;
    CHECK(pay_bounty_init(&g_B, &g_L, &x, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_ERR_ARG);
    x = c;
    x.fixer_share.num = 0;
    CHECK(pay_bounty_init(&g_B, &g_L, &x, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_ERR_ARG);
    x = c;
    x.fixer_share = (pay_rat_t){22, 21};
    CHECK(pay_bounty_init(&g_B, &g_L, &x, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_ERR_ARG);
    x = c;
    x.fixer_share = (pay_rat_t){21, 21};
    CHECK(pay_bounty_init(&g_B, &g_L, &x, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_OK);
    x = c;
    x.panel_size = 0;
    CHECK(pay_bounty_init(&g_B, &g_L, &x, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_ERR_ARG);
    x = c;
    x.panel_size = PAY_BTY_PANEL_MAX + 1;
    CHECK(pay_bounty_init(&g_B, &g_L, &x, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_ERR_ARG);
    x = c;
    x.panel_size = PAY_BTY_PANEL_MAX;
    CHECK(pay_bounty_init(&g_B, &g_L, &x, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_OK);
    x = c;
    x.quorum = 0;
    CHECK(pay_bounty_init(&g_B, &g_L, &x, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_ERR_ARG);
    x = c;
    x.quorum = 6;
    CHECK(pay_bounty_init(&g_B, &g_L, &x, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_ERR_ARG);
    x = c;
    x.quorum = 5;
    CHECK(pay_bounty_init(&g_B, &g_L, &x, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_OK);
    x = c;
    x.max_open = 0;
    CHECK(pay_bounty_init(&g_B, &g_L, &x, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_ERR_ARG);
    x = c;
    x.max_active = 0;
    CHECK(pay_bounty_init(&g_B, &g_L, &x, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_ERR_ARG);
    x = c;
    x.max_strikes = 0;
    CHECK(pay_bounty_init(&g_B, &g_L, &x, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_ERR_ARG);
    x = c;
    x.epoch_cap = 3000 * 8 - 1; /* security tier 5 = 24000 would not fit */
    CHECK(pay_bounty_init(&g_B, &g_L, &x, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_ERR_LIMIT);
    x.epoch_cap = 3000 * 8;
    CHECK(pay_bounty_init(&g_B, &g_L, &x, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_OK);
    x = c;
    x.base[PAY_BTY_DOCS] = PAY_BAL_MAX / 4; /* overflows the value itself */
    CHECK(pay_bounty_init(&g_B, &g_L, &x, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_ERR_LIMIT);
    CHECK(pay_bounty_init(0, &g_L, &c, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_ERR_ARG);
    CHECK(pay_bounty_init(&g_B, 0, &c, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_ERR_ARG);
    CHECK(pay_bounty_init(&g_B, &g_L, 0, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_ERR_ARG);
    CHECK(pay_bounty_init(&g_B, &g_L, &c, 0, 0, toy_verify, toy_audit, 0) == PAY_ERR_ARG);
    CHECK(pay_bounty_init(&g_B, &g_L, &c, 0, g_fbk, 0, toy_audit, 0) == PAY_ERR_ARG);
    CHECK(pay_bounty_init(&g_B, &g_L, &c, 0, g_fbk, toy_verify, 0, 0) == PAY_ERR_ARG);
    uint32_t plain, bad[PAY_ASSURE_BUCKETS];
    CHECK(pay_ledger_open(&g_L, 5, 0, PAY_CAP_FINANCIAL, 0, &plain) == PAY_OK);
    memcpy(bad, g_fbk, sizeof bad);
    bad[2] = plain; /* not COMMONS */
    CHECK(pay_bounty_init(&g_B, &g_L, &c, 0, bad, toy_verify, toy_audit, 0) == PAY_ERR_NO_ACCOUNT);
    bad[2] = 4242; /* no such account */
    CHECK(pay_bounty_init(&g_B, &g_L, &c, 0, bad, toy_verify, toy_audit, 0) == PAY_ERR_NO_ACCOUNT);
    CHECK(pay_bounty_init(&g_B, &g_L, &c, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_OK);
    const pay_account_t *ia = pay_ledger_account(&g_L, g_B.issuer_acct);
    CHECK(ia && (ia->flags & PAY_ACCT_ISSUER) && ia->credit == 0 && ia->owner == 0);
    for (int b = 0; b < PAY_ASSURE_BUCKETS; b++) CHECK(g_B.fee_acct[b] == g_fbk[b]);
    CHECK(pay_bounty_check(&g_B));
}

/* the full lifecycle on one context */
static void test_lifecycle(void)
{
    pay_bty_cfg_t c;
    setup(&c);
    CHECK(pay_bounty_init(&g_B, &g_L, &c, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_OK);
    pay_bounty_ctx_t *B = &g_B;
    uint8_t s[32], d[32], d2[32];

    /* B1: posting is free and needs no account */
    uint32_t id, id2, id3;
    subj(s, 1);
    CHECK(pay_bounty_post(B, 1, PAY_BTY_DOCS, 1, s, &id) == PAY_ERR_POLICY); /* closed kind */
    CHECK(pay_bounty_post(B, 1, PAY_BTY_BUG, 0, s, &id) == PAY_ERR_POLICY);
    CHECK(pay_bounty_post(B, 1, PAY_BTY_BUG, 6, s, &id) == PAY_ERR_POLICY);
    CHECK(pay_bounty_post(0, 1, PAY_BTY_BUG, 3, s, &id) == PAY_ERR_ARG);
    CHECK(pay_bounty_post(B, 1, PAY_BTY_BUG, 3, 0, &id) == PAY_ERR_ARG);
    CHECK(pay_bounty_post(B, 1, PAY_BTY_BUG, 3, s, 0) == PAY_ERR_ARG);
    uint32_t n_acct = g_L.n_accounts;
    CHECK(pay_bounty_post(B, 1, PAY_BTY_BUG, 3, s, &id) == PAY_OK);
    CHECK(id == 1 && g_L.n_accounts == n_acct); /* nothing opened, nothing paid */
    CHECK(pay_bounty_person(B, 1) == 0);
    CHECK(pay_bounty_post(B, 2, PAY_BTY_SECURITY, 1, s, &id2) == PAY_DUPLICATE); /* same subject */
    const pay_bounty_t *b = pay_bounty_get(B, id);
    CHECK(b->state == PAY_BTY_OPEN && b->poster == 1 && b->tier == 3 && b->kind == PAY_BTY_BUG);
    CHECK(pay_bounty_get(B, 0) == 0 && pay_bounty_get(B, 99) == 0);
    CHECK(B->stats.posted == 1);

    /* B5: no reviewers yet, so the audit cannot draw a panel */
    dlv(d, 'P');
    CHECK(deliver(B, id, 1, d) == PAY_ERR_POLICY); /* the poster may not fix their own */
    uint8_t dg[32], sig[32];
    pay_bounty_delivery_digest(id, 2, d, dg);
    toy_sign(3, dg, sig); /* signed by the wrong person */
    CHECK(pay_bounty_deliver(B, id, 2, d, sig, 32) == PAY_ERR_POLICY);
    CHECK(pay_bounty_deliver(B, id, 2, d, sig, 0) == PAY_ERR_ARG);
    CHECK(pay_bounty_deliver(B, 77, 2, d, sig, 32) == PAY_ERR_NOT_FOUND);
    CHECK(pay_bounty_get(B, id)->state == PAY_BTY_OPEN);
    CHECK(deliver(B, id, 2, d) == PAY_OK);
    CHECK(b->state == PAY_BTY_DELIVERED && b->fixer == 2 && memcmp(b->deliverable, d, 32) == 0);
    CHECK(pay_bounty_person(B, 2) && pay_bounty_person(B, 2)->active == 1);
    CHECK(B->stats.delivered == 1);
    {
        const pay_account_t *fa = pay_ledger_account(&g_L, pay_bounty_person(B, 2)->vfv_acct);
        CHECK(fa && fa->flags == 0 && fa->owner == 2 && fa->asset == g_L.vfv_asset);
    }
    dlv(d2, 'P');
    CHECK(deliver(B, id, 3, d2) == PAY_ERR_STATE); /* one delivery at a time */
    int a0 = g_audits;
    CHECK(pay_bounty_audit(B, id, k_beacon) == PAY_ERR_LIMIT);
    CHECK(g_audits == a0); /* the audit is not even run */
    CHECK(b->state == PAY_BTY_DELIVERED);

    /* reviewers 10..16; the poster 1 and fixer 2 are reviewers too */
    for (uint32_t r = 10; r <= 16; r++) CHECK(pay_bounty_add_reviewer(B, r) == PAY_OK);
    CHECK(pay_bounty_add_reviewer(B, 1) == PAY_OK);
    CHECK(pay_bounty_add_reviewer(B, 2) == PAY_OK);
    CHECK(pay_bounty_add_reviewer(B, 10) == PAY_DUPLICATE);
    CHECK(pay_bounty_add_reviewer(0, 10) == PAY_ERR_ARG);

    /* B4: unavailable keeps waiting; pass draws the panel */
    CHECK(pay_bounty_audit(B, 99, k_beacon) == PAY_ERR_NOT_FOUND);
    CHECK(pay_bounty_audit(B, id, 0) == PAY_ERR_ARG);
    CHECK(pay_bounty_audit(B, id, k_beacon) == PAY_OK);
    CHECK(b->state == PAY_BTY_REVIEW && b->audited && b->n_panel == 5);
    CHECK(B->stats.audit_pass == 1);
    CHECK(pay_bounty_audit(B, id, k_beacon) == PAY_ERR_STATE);
    for (uint32_t k = 0; k < b->n_panel; k++) {
        CHECK(b->panel[k] >= 10 && b->panel[k] <= 16); /* never 1 (poster) nor 2 (fixer) */
        for (uint32_t j = 0; j < k; j++) CHECK(b->panel[j] != b->panel[k]);
    }
    CHECK(pay_bounty_check(B));

    /* B6: votes */
    uint32_t outsider = 0;
    for (uint32_t r = 10; r <= 16; r++) {
        bool in = false;
        for (uint32_t k = 0; k < b->n_panel; k++) in |= b->panel[k] == r;
        if (!in) outsider = r;
    }
    CHECK(outsider != 0);
    CHECK(vote(B, id, outsider, true, 3) == PAY_ERR_POLICY);  /* not on the panel */
    CHECK(vote(B, id, 2, true, 3) == PAY_ERR_POLICY);         /* the fixer cannot vote */
    CHECK(vote(B, id, b->panel[0], true, 4) == PAY_ERR_ARG);  /* above the posted tier */
    CHECK(vote(B, id, b->panel[0], true, 0) == PAY_ERR_ARG);  /* approve needs a tier */
    CHECK(vote(B, id, b->panel[0], false, 1) == PAY_ERR_ARG); /* reject carries no tier */
    pay_bounty_vote_digest(id, b->deliverable, b->panel[0], true, 3, dg);
    toy_sign(b->panel[1], dg, sig);
    CHECK(pay_bounty_vote(B, id, b->panel[0], true, 3, sig, 32) == PAY_ERR_POLICY);
    CHECK(pay_bounty_vote(B, id, b->panel[0], true, 3, sig, 0) == PAY_ERR_ARG);
    CHECK(pay_bounty_verdict(B, id) == PAY_BTY_V_NEITHER);
    CHECK(vote(B, id, b->panel[0], true, 3) == PAY_OK);
    CHECK(vote(B, id, b->panel[0], true, 3) == PAY_DUPLICATE);
    CHECK(vote(B, id, b->panel[1], true, 2) == PAY_OK); /* judges it worth less */
    CHECK(pay_bounty_verdict(B, id) == PAY_BTY_V_NEITHER);
    CHECK(vote(B, id, b->panel[2], true, 3) == PAY_OK);
    CHECK(pay_bounty_verdict(B, id) == PAY_BTY_V_TRUE);

    /* B7 + B8: minted at the lowest approving tier (2), fee taken */
    uint64_t g = pay_bounty_value(&B->cfg, PAY_BTY_BUG, 2);
    CHECK(g == 2000);
    CHECK(pay_bounty_close_epoch(B, 100) == PAY_OK);
    CHECK(b->state == PAY_BTY_PAID && b->paid == g && b->paid_tier == 2 && b->paid_epoch == 0);
    CHECK(pay_bounty_verdict(B, id) == PAY_BTY_V_TRUE);
    uint64_t fee = pay_assure_fee(g);
    CHECK(debit(pay_bounty_person(B, 2)->vfv_acct) == g - fee);
    CHECK(B->minted == g && B->fees == fee && B->net == g - fee && B->epoch == 1);
    CHECK(B->epoch_minted == 0 && pay_bounty_person(B, 2)->epoch_minted == 0);
    CHECK(pay_ledger_account(&g_L, B->issuer_acct)->credit == g);
    uint64_t parts = 0;
    for (int k = 0; k < PAY_ASSURE_BUCKETS; k++) parts += debit(g_fbk[k]);
    CHECK(parts == fee);
    CHECK(pay_bounty_person(B, 2)->active == 0 && pay_bounty_person(B, 2)->earned == g);
    CHECK(pay_bounty_person(B, 1) && pay_bounty_person(B, 1)->earned == 0); /* poster got nothing */
    CHECK(B->stats.paid == 1);
    CHECK(pay_bounty_check(B));
    CHECK(vote(B, id, b->panel[3], false, 0) == PAY_ERR_STATE); /* too late */
    CHECK(pay_bounty_withdraw(B, id, 1) == PAY_ERR_STATE);

    /* B3: a deliverable is used once, ever */
    subj(s, 2);
    CHECK(pay_bounty_post(B, 1, PAY_BTY_IMPROVEMENT, 2, s, &id2) == PAY_OK && id2 == 2);
    CHECK(deliver(B, id2, 3, d) == PAY_ERR_REPLAY);

    /* B4 FAIL: the delivery is rejected and the bounty reopens */
    uint8_t df[32];
    dlv(df, 'F');
    CHECK(deliver(B, id2, 3, df) == PAY_OK);
    CHECK(pay_bounty_person(B, 3)->active == 1);
    CHECK(pay_bounty_audit(B, id2, k_beacon) == PAY_ERR_POLICY);
    b = pay_bounty_get(B, id2);
    CHECK(b->state == PAY_BTY_OPEN && b->fixer == 0 && !b->audited && b->n_panel == 0);
    CHECK(pay_bounty_person(B, 3)->active == 0 && B->stats.audit_fail == 1);
    CHECK(deliver(B, id2, 3, df) == PAY_ERR_REPLAY);
    uint8_t du[32];
    dlv(du, 'U');
    CHECK(deliver(B, id2, 3, du) == PAY_OK);
    CHECK(pay_bounty_audit(B, id2, k_beacon) == PAY_ERR_STATE);
    CHECK(b->state == PAY_BTY_DELIVERED);

    /* B6 FALSE: rejected by quorum, reopens for someone else */
    subj(s, 3);
    CHECK(pay_bounty_post(B, 4, PAY_BTY_BUG, 1, s, &id3) == PAY_OK);
    dlv(d, 'P');
    CHECK(deliver(B, id3, 5, d) == PAY_OK);
    CHECK(pay_bounty_audit(B, id3, k_beacon) == PAY_OK);
    panel_votes(B, id3, 0, 2, 0);
    CHECK(pay_bounty_verdict(B, id3) == PAY_BTY_V_NEITHER);
    panel_votes(B, id3, 0, 0, 0);
    b = pay_bounty_get(B, id3);
    CHECK(vote(B, id3, b->panel[2], false, 0) == PAY_OK);
    CHECK(pay_bounty_verdict(B, id3) == PAY_BTY_V_FALSE);
    uint64_t m0 = B->minted;
    CHECK(pay_bounty_close_epoch(B, 101) == PAY_OK);
    CHECK(b->state == PAY_BTY_OPEN && B->minted == m0 && B->stats.rejected == 1);
    CHECK(pay_bounty_person(B, 5)->active == 0);

    /* B6 BOTH: one dissent holds it; the arbiter decides */
    dlv(d, 'P');
    CHECK(deliver(B, id3, 6, d) == PAY_OK);
    CHECK(pay_bounty_audit(B, id3, k_beacon) == PAY_OK);
    panel_votes(B, id3, 4, 1, 1);
    CHECK(pay_bounty_verdict(B, id3) == PAY_BTY_V_BOTH);
    CHECK(pay_bounty_close_epoch(B, 102) == PAY_OK);
    CHECK(b->state == PAY_BTY_HELD && B->minted == m0 && B->stats.held == 1);
    CHECK(pay_bounty_verdict(B, id3) == PAY_BTY_V_BOTH);
    CHECK(pay_bounty_close_epoch(B, 103) == PAY_OK); /* stays held */
    CHECK(b->state == PAY_BTY_HELD && B->minted == m0);
    CHECK(rule(B, id3, true, 1, 7) == PAY_ERR_POLICY);    /* not the arbiter */
    CHECK(rule(B, id3, true, 2, ARBITER) == PAY_ERR_ARG); /* above the posted tier */
    CHECK(rule(B, id3, false, 1, ARBITER) == PAY_ERR_ARG);
    CHECK(rule(B, id2, true, 1, ARBITER) == PAY_ERR_STATE); /* not held */
    CHECK(rule(B, id3, true, 1, ARBITER) == PAY_OK);
    CHECK(b->state == PAY_BTY_APPROVED && b->ruled && b->paid_tier == 1);
    CHECK(pay_bounty_verdict(B, id3) == PAY_BTY_V_TRUE);
    CHECK(pay_bounty_close_epoch(B, 104) == PAY_OK);
    CHECK(b->state == PAY_BTY_PAID && b->paid == 1000 && B->minted == m0 + 1000);
    CHECK(pay_bounty_check(B));

    /* the arbiter can also reject a held bounty */
    uint32_t id4;
    subj(s, 4);
    CHECK(pay_bounty_post(B, 4, PAY_BTY_SECURITY, 5, s, &id4) == PAY_OK);
    dlv(d, 'P');
    CHECK(deliver(B, id4, 6, d) == PAY_OK);
    CHECK(pay_bounty_audit(B, id4, k_beacon) == PAY_OK);
    panel_votes(B, id4, 1, 1, 5);
    CHECK(pay_bounty_close_epoch(B, 105) == PAY_OK);
    CHECK(pay_bounty_get(B, id4)->state == PAY_BTY_HELD);
    CHECK(rule(B, id4, false, 0, ARBITER) == PAY_OK);
    CHECK(pay_bounty_get(B, id4)->state == PAY_BTY_OPEN && B->stats.rejected == 2);
    CHECK(pay_bounty_person(B, 6)->active == 0);

    /* withdraw: only the poster, only while open */
    CHECK(pay_bounty_withdraw(B, id4, 5) == PAY_ERR_POLICY);
    CHECK(pay_bounty_withdraw(B, 99, 4) == PAY_ERR_NOT_FOUND);
    CHECK(pay_bounty_withdraw(0, id4, 4) == PAY_ERR_ARG);
    CHECK(pay_bounty_withdraw(B, id4, 4) == PAY_OK);
    CHECK(pay_bounty_get(B, id4)->state == PAY_BTY_WITHDRAWN);
    CHECK(deliver(B, id4, 6, d2) == PAY_ERR_STATE);
    uint32_t id5;
    CHECK(pay_bounty_post(B, 8, PAY_BTY_SECURITY, 5, s, &id5) == PAY_OK); /* subject free again */
    CHECK(pay_bounty_check(B));
}

/* B1 max_open, B3 max_active, B7 caps and carry, B9 fraud and suspension */
static void test_limits_caps_fraud(void)
{
    pay_bty_cfg_t c;
    setup(&c);
    c.epoch_cap = 10000;             /* fixer share 8/21 -> 3809 */
    c.base[PAY_BTY_SECURITY] = 1000; /* tier 5 = 8000 fits the cap */
    c.max_open = 3;
    c.max_active = 2;
    c.max_strikes = 2;
    CHECK(pay_bounty_init(&g_B, &g_L, &c, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_OK);
    pay_bounty_ctx_t *B = &g_B;
    for (uint32_t r = 10; r <= 17; r++) CHECK(pay_bounty_add_reviewer(B, r) == PAY_OK);
    uint8_t s[32], d[32];
    uint32_t ids[16], n = 0;

    for (uint32_t i = 0; i < 3; i++) {
        subj(s, 100 + i);
        CHECK(pay_bounty_post(B, 1, PAY_BTY_BUG, 3, s, &ids[n++]) == PAY_OK);
    }
    subj(s, 103);
    uint32_t junk;
    CHECK(pay_bounty_post(B, 1, PAY_BTY_BUG, 3, s, &junk) == PAY_ERR_LIMIT);
    CHECK(pay_bounty_post(B, 2, PAY_BTY_BUG, 3, s, &ids[n++]) == PAY_OK); /* others may */
    CHECK(pay_bounty_withdraw(B, ids[0], 1) == PAY_OK);
    subj(s, 104);
    CHECK(pay_bounty_post(B, 1, PAY_BTY_BUG, 3, s, &ids[n++]) == PAY_OK); /* slot freed */

    /* fixer 3 may have 2 deliveries in flight */
    dlv(d, 'P');
    CHECK(deliver(B, ids[1], 3, d) == PAY_OK);
    dlv(d, 'P');
    CHECK(deliver(B, ids[2], 3, d) == PAY_OK);
    dlv(d, 'P');
    CHECK(deliver(B, ids[3], 3, d) == PAY_ERR_LIMIT);
    CHECK(deliver(B, ids[3], 4, d) == PAY_OK);
    dlv(d, 'P');
    CHECK(deliver(B, ids[4], 5, d) == PAY_OK);

    /* four bounties of 3000 each approved, in this order: ids 1,2 (fixer 3),
     * 3 (fixer 4), 4 (fixer 5). Epoch cap 10000; fixer cap 3809. */
    uint32_t order[4] = {ids[1], ids[2], ids[3], ids[4]};
    for (int i = 0; i < 4; i++) {
        CHECK(pay_bounty_audit(B, order[i], k_beacon) == PAY_OK);
        panel_votes(B, order[i], 3, 0, 3);
    }
    CHECK(pay_bounty_close_epoch(B, 200) == PAY_OK);
    /* first: fixer 3 #1 (3000), second: fixer 3 #2 would make 6000 > 3809 ->
     * carried, third: fixer 4 (6000), fourth: fixer 5 (9000) */
    CHECK(pay_bounty_get(B, order[0])->state == PAY_BTY_PAID);
    CHECK(pay_bounty_get(B, order[1])->state == PAY_BTY_APPROVED);
    CHECK(pay_bounty_get(B, order[2])->state == PAY_BTY_PAID);
    CHECK(pay_bounty_get(B, order[3])->state == PAY_BTY_PAID);
    CHECK(B->minted == 9000 && B->stats.carried == 1);
    CHECK(pay_bounty_person(B, 3)->active == 1);
    CHECK(pay_bounty_check(B));
    /* next epoch the carried one mints first */
    CHECK(pay_bounty_close_epoch(B, 201) == PAY_OK);
    CHECK(pay_bounty_get(B, order[1])->state == PAY_BTY_PAID && B->minted == 12000);
    CHECK(pay_bounty_get(B, order[1])->paid_epoch == 1);
    CHECK(pay_bounty_person(B, 3)->earned == 6000 && pay_bounty_person(B, 3)->active == 0);
    CHECK(pay_bounty_check(B));

    /* the epoch cap carries too: 4 more bounties of 3000 from distinct fixers */
    uint32_t more[4];
    for (uint32_t i = 0; i < 4; i++) {
        subj(s, 200 + i);
        CHECK(pay_bounty_post(B, 30 + i, PAY_BTY_BUG, 3, s, &more[i]) == PAY_OK);
        dlv(d, 'P');
        CHECK(deliver(B, more[i], 40 + i, d) == PAY_OK);
        CHECK(pay_bounty_audit(B, more[i], k_beacon) == PAY_OK);
        panel_votes(B, more[i], 3, 0, 3);
    }
    uint64_t m0 = B->minted;
    CHECK(pay_bounty_close_epoch(B, 202) == PAY_OK);
    CHECK(B->minted == m0 + 9000); /* 3 fit under 10000, the 4th carries */
    CHECK(pay_bounty_get(B, more[3])->state == PAY_BTY_APPROVED);
    CHECK(pay_bounty_close_epoch(B, 203) == PAY_OK);
    CHECK(B->minted == m0 + 12000 && pay_bounty_get(B, more[3])->state == PAY_BTY_PAID);
    CHECK(pay_bounty_check(B));

    /* B9 on a PAID bounty: marked, never clawed back; strikes */
    const pay_bounty_t *pb = pay_bounty_get(B, order[2]);
    uint32_t approver = pb->panel[0], quiet = pb->panel[4];
    uint64_t fixer_bal = debit(pay_bounty_person(B, 4)->vfv_acct), mm = B->minted;
    uint32_t s_quiet = pay_bounty_person(B, quiet)->strikes;
    CHECK(fraud(B, order[2], 7) == PAY_ERR_POLICY); /* not the arbiter */
    CHECK(fraud(B, order[2], ARBITER) == PAY_OK);
    CHECK(pb->state == PAY_BTY_PAID && pb->fraud);
    CHECK(fraud(B, order[2], ARBITER) == PAY_DUPLICATE);
    CHECK(debit(pay_bounty_person(B, 4)->vfv_acct) == fixer_bal && B->minted == mm);
    CHECK(pay_bounty_person(B, 4)->strikes == 1 && !pay_bounty_person(B, 4)->suspended);
    for (uint32_t k = 0; k < 3; k++) CHECK(pay_bounty_person(B, pb->panel[k])->strikes >= 1);
    CHECK(pay_bounty_person(B, approver)->strikes >= 1);
    CHECK(pay_bounty_person(B, quiet)->strikes == s_quiet); /* did not approve */
    CHECK(B->stats.frauds == 1);
    CHECK(pay_bounty_check(B));

    /* B9 on an APPROVED bounty: nothing mints, it reopens; second strike
     * suspends fixer 4 */
    uint32_t idf;
    subj(s, 300);
    CHECK(pay_bounty_post(B, 50, PAY_BTY_BUG, 1, s, &idf) == PAY_OK);
    dlv(d, 'P');
    CHECK(deliver(B, idf, 4, d) == PAY_OK);
    CHECK(fraud(B, idf, ARBITER) == PAY_ERR_STATE); /* not audited yet */
    CHECK(pay_bounty_audit(B, idf, k_beacon) == PAY_OK);
    panel_votes(B, idf, 3, 0, 1);
    CHECK(pay_bounty_close_epoch(B, 204) == PAY_OK); /* ... it was paid: use a fresh one */
    uint32_t idg;
    subj(s, 301);
    CHECK(pay_bounty_post(B, 50, PAY_BTY_BUG, 1, s, &idg) == PAY_OK);
    dlv(d, 'P');
    CHECK(deliver(B, idg, 4, d) == PAY_OK);
    CHECK(pay_bounty_audit(B, idg, k_beacon) == PAY_OK);
    panel_votes(B, idg, 3, 0, 1);
    CHECK(pay_bounty_verdict(B, idg) == PAY_BTY_V_TRUE);
    mm = B->minted;
    CHECK(rule(B, idg, true, 1, ARBITER) == PAY_ERR_STATE);
    CHECK(pay_bounty_close_epoch(B, 2041) == PAY_OK); /* approved, but ... */
    CHECK(pay_bounty_get(B, idg)->state == PAY_BTY_PAID);
    subj(s, 302);
    CHECK(pay_bounty_post(B, 50, PAY_BTY_BUG, 2, s, &idg) == PAY_OK);
    dlv(d, 'P');
    CHECK(deliver(B, idg, 4, d) == PAY_OK);
    CHECK(pay_bounty_audit(B, idg, k_beacon) == PAY_OK);
    panel_votes(B, idg, 3, 0, 2);
    /* make it APPROVED without minting: an epoch with no room left */
    B->cfg.epoch_cap = 0;
    CHECK(pay_bounty_close_epoch(B, 2042) == PAY_OK);
    B->cfg.epoch_cap = 10000;
    CHECK(pay_bounty_get(B, idg)->state == PAY_BTY_APPROVED &&
          pay_bounty_get(B, idg)->paid_tier == 2 && pay_bounty_get(B, idg)->approve_seq != 0);
    mm = B->minted;
    CHECK(fraud(B, idg, ARBITER) == PAY_OK); /* found before it was paid */
    const pay_bounty_t *gb = pay_bounty_get(B, idg);
    CHECK(gb->state == PAY_BTY_OPEN && !gb->fraud && gb->fixer == 0);
    CHECK(gb->paid_tier == 0 && gb->approve_seq == 0 && gb->n_panel == 0 && !gb->audited);
    for (uint32_t k = 0; k < PAY_BTY_PANEL_MAX; k++)
        CHECK(gb->panel[k] == 0 && gb->vote[k] == 0 && gb->vote_tier[k] == 0);
    CHECK(pay_bounty_person(B, 4)->suspended && pay_bounty_person(B, 4)->active == 0);
    CHECK(pay_bounty_close_epoch(B, 205) == PAY_OK);
    CHECK(B->minted == mm);
    dlv(d, 'P');
    CHECK(deliver(B, idg, 4, d) == PAY_ERR_POLICY); /* suspended */
    CHECK(fraud(B, idg, ARBITER) == PAY_ERR_STATE);
    CHECK(pay_bounty_check(B));

    /* suspended reviewers are never drawn */
    uint32_t sus = 0;
    for (uint32_t r = 10; r <= 17; r++)
        if (pay_bounty_person(B, r)->suspended) sus++;
    for (uint32_t i = 0; i < 3; i++) {
        uint32_t idh;
        subj(s, 400 + i);
        CHECK(pay_bounty_post(B, 60, PAY_BTY_BUG, 1, s, &idh) == PAY_OK);
        dlv(d, 'P');
        CHECK(deliver(B, idh, 61 + i, d) == PAY_OK);
        pay_status_t st = pay_bounty_audit(B, idh, k_beacon);
        if (8 - sus < 5) {
            CHECK(st == PAY_ERR_LIMIT);
            continue;
        }
        CHECK(st == PAY_OK);
        const pay_bounty_t *hb = pay_bounty_get(B, idh);
        for (uint32_t k = 0; k < hb->n_panel; k++)
            CHECK(!pay_bounty_person(B, hb->panel[k])->suspended);
    }
    printf("  limits: minted %" PRIu64 ", fees %" PRIu64 ", carried %" PRIu64
           ", suspended reviewers %" PRIu32 "\n",
           B->minted, B->fees, B->stats.carried, sus);
    CHECK(pay_bounty_check(B));
}

/* B5: the panel is a function of the beacon; and I1-I3 catch tampering */
static void test_panel_and_check(void)
{
    pay_bty_cfg_t c;
    setup(&c);
    CHECK(pay_bounty_init(&g_B, &g_L, &c, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_OK);
    pay_bounty_ctx_t *B = &g_B;
    for (uint32_t r = 10; r < 40; r++) CHECK(pay_bounty_add_reviewer(B, r) == PAY_OK);
    uint8_t s[32], d[32];
    uint32_t hits[40] = {0};
    for (uint32_t i = 0; i < 60; i++) {
        uint32_t id;
        subj(s, 500 + i);
        uint32_t poster = 10 + (i % 30), fixer = 10 + ((i + 7) % 30); /* both are reviewers */
        CHECK(pay_bounty_post(B, poster, PAY_BTY_BUG, 2, s, &id) == PAY_OK);
        dlv(d, 'P');
        CHECK(deliver(B, id, fixer, d) == PAY_OK);
        uint8_t beacon[32];
        memset(beacon, 0, 32);
        beacon[0] = (uint8_t) i;
        CHECK(pay_bounty_audit(B, id, beacon) == PAY_OK);
        const pay_bounty_t *b = pay_bounty_get(B, id);
        for (uint32_t k = 0; k < b->n_panel; k++) {
            CHECK(b->panel[k] != poster && b->panel[k] != fixer);
            hits[b->panel[k]]++;
        }
        if (i < 20) {
            panel_votes(B, id, 3, 0, 2);
        }
    }
    uint32_t drawn = 0;
    for (uint32_t r = 10; r < 40; r++) drawn += hits[r] > 0;
    CHECK(drawn >= 25); /* the draw spreads over the pool */
    CHECK(pay_bounty_close_epoch(B, 300) == PAY_OK);
    CHECK(B->stats.paid == 20 && B->minted == 40000);
    CHECK(pay_bounty_check(B));

    /* the same beacon gives the same panel; another beacon another one */
    static pay_ledger_t L2;
    static pay_bounty_ctx_t B2, B3;
    uint32_t f2[PAY_ASSURE_BUCKETS];
    uint32_t p2[PAY_BTY_PANEL_MAX], p3[PAY_BTY_PANEL_MAX];
    for (int run = 0; run < 3; run++) {
        pay_bounty_ctx_t *X = run == 2 ? &B3 : &B2;
        pay_ledger_init(&L2, 0);
        for (int b = 0; b < PAY_ASSURE_BUCKETS; b++)
            CHECK(pay_ledger_open(&L2, 999 - (uint32_t) b, 0, PAY_CAP_FINANCIAL, PAY_ACCT_COMMONS,
                                  &f2[b]) == PAY_OK);
        CHECK(pay_bounty_init(X, &L2, &c, 0, f2, toy_verify, toy_audit, 0) == PAY_OK);
        for (uint32_t r = 10; r < 40; r++) CHECK(pay_bounty_add_reviewer(X, r) == PAY_OK);
        uint32_t id;
        subj(s, 1);
        CHECK(pay_bounty_post(X, 1, PAY_BTY_BUG, 2, s, &id) == PAY_OK);
        memset(d, 0, 32);
        memcpy(d, "P-fixed", 7);
        CHECK(deliver(X, id, 2, d) == PAY_OK);
        uint8_t beacon[32];
        memset(beacon, run == 2 ? 0x55 : 0xAA, 32);
        CHECK(pay_bounty_audit(X, id, beacon) == PAY_OK);
        if (run == 0) memcpy(p2, X->b[0].panel, sizeof p2);
        if (run == 1) CHECK(memcmp(p2, X->b[0].panel, sizeof p2) == 0);
        if (run == 2) memcpy(p3, X->b[0].panel, sizeof p3);
    }
    CHECK(memcmp(p2, p3, sizeof p2) != 0);

    /* tampering is caught (restored after each) */
    pay_bounty_t *pb = &B->b[0];
    CHECK(pb->state == PAY_BTY_PAID);
    pb->paid++;
    CHECK(!pay_bounty_check(B));
    pb->paid--;
    pb->audited = false;
    CHECK(!pay_bounty_check(B));
    pb->audited = true;
    pb->paid_tier = 1;
    CHECK(!pay_bounty_check(B)); /* paid 2000 is not value(tier 1) */
    pb->paid_tier = 2;
    uint8_t v = pb->vote[0];
    pb->vote[0] = 2; /* a dissent the verdict never saw */
    CHECK(!pay_bounty_check(B));
    pb->vote[0] = v;
    uint32_t pm = pb->panel[0];
    pb->panel[0] = pb->fixer;
    CHECK(!pay_bounty_check(B));
    pb->panel[0] = pb->poster;
    CHECK(!pay_bounty_check(B));
    pb->panel[0] = pm;
    B->minted++;
    CHECK(!pay_bounty_check(B));
    B->minted--;
    B->fees++;
    CHECK(!pay_bounty_check(B));
    B->fees--;
    B->epoch_minted = B->cfg.epoch_cap + 1;
    CHECK(!pay_bounty_check(B));
    B->epoch_minted = 0;
    B->p[0].earned++;
    CHECK(!pay_bounty_check(B));
    B->p[0].earned--;
    pay_bounty_t *ob = &B->b[59]; /* unpaid */
    CHECK(ob->state == PAY_BTY_REVIEW);
    ob->paid = 1;
    CHECK(!pay_bounty_check(B));
    ob->paid = 0;
    /* a suspended panel member's vote is refused */
    pay_bty_person_t *sp = 0;
    for (uint32_t i = 0; i < B->n_p; i++)
        if (B->p[i].owner == ob->panel[0]) sp = &B->p[i];
    CHECK(sp != 0);
    sp->suspended = true;
    CHECK(vote(B, ob->id, ob->panel[0], true, 1) == PAY_ERR_POLICY);
    sp->suspended = false;
    CHECK(vote(B, ob->id, ob->panel[0], true, 1) == PAY_OK);
    pb->ruled = true; /* a ruling covers the verdict, not the tier bound */
    pb->paid_tier = 3;
    CHECK(!pay_bounty_check(B));
    pb->paid_tier = 2;
    pb->ruled = false;
    CHECK(pay_bounty_check(B));
    CHECK(!pay_bounty_check(0));
    CHECK(pay_bounty_close_epoch(0, 1) == PAY_ERR_ARG);
    CHECK(pay_bounty_verdict(0, 1) == PAY_BTY_V_NEITHER);
    CHECK(pay_bounty_verdict(B, 999) == PAY_BTY_V_NEITHER);
    CHECK(pay_bounty_verdict(B, B->b[1].id) == PAY_BTY_V_TRUE); /* paid */
    CHECK(pay_bounty_person(0, 1) == 0 && pay_bounty_get(0, 1) == 0);
}

/* known-answer digests, guards, full tables and exact cap boundaries */
static void kat(const char *dom, uint32_t id, const uint8_t *mid, uint32_t a, int has_a, uint64_t b,
                int has_b, uint64_t c, int has_c, uint8_t out[32])
{
    pay_hbuf h;
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, dom);
    pay_hbuf_u64(&h, id);
    if (has_a == 2) pay_hbuf_u64(&h, a);
    if (mid) pay_hbuf_put(&h, mid, 32);
    if (has_a == 1) pay_hbuf_u64(&h, a);
    if (has_b) pay_hbuf_u64(&h, b);
    if (has_c) pay_hbuf_u64(&h, c);
    pay_hbuf_final(&h, out);
}

static void test_edges(void)
{
    uint8_t d[32], x[32], y[32];
    memset(d, 0x5A, 32);
    pay_bounty_delivery_digest(7, 9, d, x);
    kat("ZXV-BOUNTY-DELIVER-v1", 7, d, 9, 2, 0, 0, 0, 0, y);
    CHECK(memcmp(x, y, 32) == 0);
    pay_bounty_vote_digest(7, d, 9, true, 4, x);
    kat("ZXV-BOUNTY-VOTE-v1", 7, d, 9, 1, 1, 1, 4, 1, y);
    CHECK(memcmp(x, y, 32) == 0);
    pay_bounty_vote_digest(7, d, 9, false, 0, x);
    kat("ZXV-BOUNTY-VOTE-v1", 7, d, 9, 1, 0, 1, 0, 1, y);
    CHECK(memcmp(x, y, 32) == 0);
    pay_bounty_rule_digest(7, d, true, 3, x);
    kat("ZXV-BOUNTY-RULE-v1", 7, d, 1, 1, 3, 1, 0, 0, y);
    CHECK(memcmp(x, y, 32) == 0);
    pay_bounty_rule_digest(7, d, false, 0, x);
    kat("ZXV-BOUNTY-RULE-v1", 7, d, 0, 1, 0, 1, 0, 0, y);
    CHECK(memcmp(x, y, 32) == 0);
    pay_bounty_fraud_digest(7, d, x);
    kat("ZXV-BOUNTY-FRAUD-v1", 7, d, 0, 0, 0, 0, 0, 0, y);
    CHECK(memcmp(x, y, 32) == 0);
    /* every byte of the deliverable is bound */
    uint8_t d2[32];
    memcpy(d2, d, 32);
    d2[31] ^= 1;
    pay_bounty_fraud_digest(7, d2, y);
    pay_bounty_fraud_digest(7, d, x);
    CHECK(memcmp(x, y, 32) != 0);

    /* exact boundaries: the epoch cap and the fixer cap are inclusive;
     * exactly panel_size eligible reviewers is enough */
    pay_bty_cfg_t c;
    setup(&c);
    c.base[PAY_BTY_SECURITY] = 0;
    c.base[PAY_BTY_IMPROVEMENT] = 0;
    c.epoch_cap = 8000; /* init needs bug tier 5 to fit */
    c.fixer_share = (pay_rat_t){1, 2};
    CHECK(pay_bounty_init(&g_B, &g_L, &c, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_OK);
    pay_bounty_ctx_t *B = &g_B;
    B->cfg.epoch_cap = 6000; /* then 3000 per fixer */
    for (uint32_t r = 10; r <= 14; r++) CHECK(pay_bounty_add_reviewer(B, r) == PAY_OK);
    uint8_t s[32];
    uint32_t id[4];
    uint8_t tiers[4] = {1, 2, 3, 1};
    uint32_t fixers[4] = {20, 20, 21, 22};
    for (int i = 0; i < 4; i++) {
        subj(s, 900 + (uint32_t) i);
        CHECK(pay_bounty_post(B, 1, PAY_BTY_BUG, tiers[i], s, &id[i]) == PAY_OK);
        dlv(d, 'P');
        CHECK(deliver(B, id[i], fixers[i], d) == PAY_OK);
        CHECK(pay_bounty_audit(B, id[i], k_beacon) == PAY_OK); /* exactly 5 */
        panel_votes(B, id[i], 3, 0, tiers[i]);
    }
    /* 1000 + 2000 (fixer 20 at exactly 3000) + 3000 = 6000 = the cap; the
     * fourth carries */
    CHECK(pay_bounty_close_epoch(B, 1) == PAY_OK);
    CHECK(B->minted == 6000);
    for (int i = 0; i < 3; i++) CHECK(pay_bounty_get(B, id[i])->state == PAY_BTY_PAID);
    CHECK(pay_bounty_get(B, id[3])->state == PAY_BTY_APPROVED);
    CHECK(pay_bounty_check(B));
    B->epoch_minted = B->cfg.epoch_cap; /* at the cap is still valid */
    CHECK(pay_bounty_check(B));
    B->epoch_minted = 0;
    CHECK(pay_bounty_close_epoch(B, 2) == PAY_OK);
    CHECK(B->minted == 7000 && pay_bounty_check(B));

    /* the poster's own reviewer slot does not count: 5 reviewers, poster 10 */
    uint32_t idp;
    subj(s, 950);
    CHECK(pay_bounty_post(B, 10, PAY_BTY_BUG, 1, s, &idp) == PAY_OK);
    dlv(d, 'P');
    CHECK(deliver(B, idp, 23, d) == PAY_OK);
    CHECK(pay_bounty_audit(B, idp, k_beacon) == PAY_ERR_LIMIT);
    CHECK(pay_bounty_add_reviewer(B, 15) == PAY_OK);
    CHECK(pay_bounty_audit(B, idp, k_beacon) == PAY_OK);

    /* guards on vote, rule and fraud */
    uint8_t sig[32] = {0};
    CHECK(pay_bounty_vote(0, idp, 11, true, 1, sig, 32) == PAY_ERR_ARG);
    CHECK(pay_bounty_vote(B, idp, 11, true, 1, 0, 32) == PAY_ERR_ARG);
    CHECK(pay_bounty_vote(B, 999, 11, true, 1, sig, 32) == PAY_ERR_NOT_FOUND);
    CHECK(pay_bounty_rule(0, idp, true, 1, sig, 32) == PAY_ERR_ARG);
    CHECK(pay_bounty_rule(B, idp, true, 1, 0, 32) == PAY_ERR_ARG);
    CHECK(pay_bounty_rule(B, idp, true, 1, sig, 0) == PAY_ERR_ARG);
    CHECK(pay_bounty_rule(B, 999, true, 1, sig, 32) == PAY_ERR_NOT_FOUND);
    CHECK(pay_bounty_fraud(0, idp, sig, 32) == PAY_ERR_ARG);
    CHECK(pay_bounty_fraud(B, idp, 0, 32) == PAY_ERR_ARG);
    CHECK(pay_bounty_fraud(B, idp, sig, 0) == PAY_ERR_ARG);
    CHECK(pay_bounty_fraud(B, 999, sig, 32) == PAY_ERR_NOT_FOUND);
    CHECK(pay_bounty_deliver(0, idp, 2, d, sig, 32) == PAY_ERR_ARG);
    CHECK(pay_bounty_deliver(B, idp, 2, 0, sig, 32) == PAY_ERR_ARG);
    CHECK(pay_bounty_deliver(B, idp, 2, d, 0, 32) == PAY_ERR_ARG);
    CHECK(pay_bounty_audit(0, idp, k_beacon) == PAY_ERR_ARG);
    CHECK(pay_bounty_add_reviewer(B, 15) == PAY_DUPLICATE);

    /* subjects and deliverables differing only in the last byte are distinct */
    uint8_t sa[32], sb[32];
    memset(sa, 0x33, 32);
    memcpy(sb, sa, 32);
    sb[31] ^= 0x80;
    uint32_t ia, ib;
    CHECK(pay_bounty_post(B, 70, PAY_BTY_BUG, 1, sa, &ia) == PAY_OK);
    CHECK(pay_bounty_post(B, 70, PAY_BTY_BUG, 1, sb, &ib) == PAY_OK);
    CHECK(memcmp(pay_bounty_get(B, ib)->subject, sb, 32) == 0);
    uint8_t da[32], db[32];
    memset(da, 'P', 32);
    memcpy(db, da, 32);
    db[31] ^= 0x80;
    CHECK(deliver(B, ia, 71, da) == PAY_OK);
    CHECK(deliver(B, ib, 72, db) == PAY_OK);
    CHECK(memcmp(pay_bounty_get(B, ib)->deliverable, db, 32) == 0);

    /* a ruling still needs the tier bound and the exact value */
    pay_bounty_t *pb = &B->b[0];
    CHECK(pb->state == PAY_BTY_PAID);
    pb->ruled = true;
    CHECK(pay_bounty_check(B));
    pb->paid_tier = 0;
    CHECK(!pay_bounty_check(B));
    pb->paid_tier = 1;
    pb->paid++;
    CHECK(!pay_bounty_check(B));
    pb->paid--;
    pb->ruled = false;
    CHECK(pay_bounty_check(B));

    /* full tables */
    setup(&c);
    c.max_open = PAY_BTY_MAX + 1;
    CHECK(pay_bounty_init(&g_B, &g_L, &c, 0, g_fbk, toy_verify, toy_audit, 0) == PAY_OK);
    uint32_t last = 0;
    for (uint32_t i = 0; i < PAY_BTY_MAX; i++) {
        subj(s, 10000 + i);
        CHECK(pay_bounty_post(B, 1, PAY_BTY_BUG, 1, s, &last) == PAY_OK);
    }
    CHECK(last == PAY_BTY_MAX);
    subj(s, 99999);
    CHECK(pay_bounty_post(B, 1, PAY_BTY_BUG, 1, s, &last) == PAY_ERR_FULL);
    for (uint32_t r = 0; r < PAY_BTY_PEOPLE; r++)
        CHECK(pay_bounty_add_reviewer(B, 5000 + r) == PAY_OK);
    CHECK(pay_bounty_add_reviewer(B, 9999) == PAY_ERR_FULL);
    dlv(d, 'P');
    CHECK(deliver(B, 1, 9999, d) == PAY_ERR_FULL); /* a new fixer has no room */
    CHECK(pay_bounty_get(B, 1)->state == PAY_BTY_OPEN);
    CHECK(deliver(B, 1, 5000, d) == PAY_OK); /* an existing person still can */
    CHECK(pay_bounty_check(B));
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    test_cfg_and_value();
    test_init();
    test_lifecycle();
    test_limits_caps_fraud();
    test_panel_and_check();
    test_edges();
    printf("test_pay_bounty: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
