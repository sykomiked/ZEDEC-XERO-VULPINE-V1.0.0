/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_policy.c — zxv_policy: transfers, jurisdiction packs, four-valued
 * terms, settlement into zxt (run on the engine), escrow and mediation
 * bundles, the COBOL records and ZXV_POLICY. */
#include "policy_cobol.h"
#include "zxt_exec.h"

#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            g_pass++;                                                                              \
        } else {                                                                                   \
            g_fail++;                                                                              \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

#define T_ LPRES_STATE_TRUE
#define F_ LPRES_STATE_FALSE
#define B_ LPRES_STATE_BOTH
#define N_ LPRES_STATE_NEITHER

static const char SRC[] = "CONTRACT SUPPLY-2027 20261015\n"
                          "DOC 9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08\n"
                          "LAW KE\n"
                          "PARTY ACME US-DE\n"
                          "PARTY BETA KE\n"
                          "PARTY CARGO GB\n"
                          "SIGN 2\n"
                          "WINDOW 20261015 20271231\n"
                          "LOCK 20261224 20261226\n"
                          "CAP EUR 500\n"
                          "CAP USD 2000000\n"
                          "LIMIT ACME USD 1500000\n"
                          "ALLOW GB\n"
                          "ALLOW KE\n"
                          "ALLOW US\n"
                          "DENY US-NY\n"
                          "DUE T1 ACME BETA USD 1200000 20261231 PENALTY 12000\n"
                          "DUE T2 ACME BETA USD 300000 20270331 AFTER T1\n"
                          "DUE T3 BETA CARGO USD 50000 20261231\n"
                          "GATE G1 ALL T1 T3\n"
                          "GATE G2 ANY T1 T3\n"
                          "DUE T4 CARGO ACME USD 7000 20270115 AFTER G1\n"
                          "END\n";

static zxp_contract_t C;

static policy_txn_t txn(uint8_t from, uint8_t to, int64_t amount, uint32_t date)
{
    policy_txn_t t;
    memset(&t, 0, sizeof t);
    t.from = from;
    t.to = to;
    memcpy(t.asset, "USD", 4);
    t.amount = amount;
    t.date = date;
    strcpy(t.from_jur, C.party[from].jur);
    strcpy(t.to_jur, C.party[to].jur);
    t.signers = (uint16_t) ((1u << from) | (1u << to));
    memcpy(t.doc, C.doc, 32);
    return t;
}

static void test_belnap_ops(void)
{
    const lpres_state_t v[4] = {N_, T_, F_, B_};
    /* knowledge join and meet on lpres's encoding */
    CHECK(policy_k_join(N_, T_) == T_ && policy_k_join(T_, F_) == B_ && policy_k_join(F_, N_) == F_ &&
              policy_k_join(B_, N_) == B_,
          "k-join");
    CHECK(policy_k_meet(T_, F_) == N_ && policy_k_meet(B_, T_) == T_ && policy_k_meet(B_, B_) == B_,
          "k-meet");
    for (int a = 0; a < 4; a++)
        for (int b = 0; b < 4; b++) {
            lpres_state_t j = policy_k_join(v[a], v[b]), m = policy_k_meet(v[a], v[b]);
            if (j != policy_k_join(v[b], v[a]) || policy_k_join(j, v[a]) != j || policy_k_meet(m, v[a]) != m)
                CHECK(0, "k lattice laws at %d,%d", a, b);
        }
    /* truth ops are the kernel's own: B ^ N = N, B v N = B (not textbook F/T) */
    CHECK(lpres_conjoin(B_, N_) == N_ && lpres_disjoin(B_, N_) == B_, "kernel truth rule kept");
}

static void test_transfers(void)
{
    policy_result_t r;
    policy_txn_t t = txn(0, 1, 1000000, 20261101);
    r = policy_check(&C, NULL, NULL, &t);
    CHECK(r.decision == POLICY_ALLOW && r.value == T_ && !r.escalate && r.deny_mask == 0, "clean allow");

    t = txn(0, 1, 1000000, 20261014);
    r = policy_check(&C, NULL, NULL, &t);
    CHECK(r.decision == POLICY_DENY && r.deny_mask == POLICY_R_WINDOW && r.value == F_, "before window");
    t = txn(0, 1, 1000000, 20261225);
    r = policy_check(&C, NULL, NULL, &t);
    CHECK(r.decision == POLICY_DENY && r.deny_mask == POLICY_R_LOCK, "lockout");
    t = txn(0, 1, 1000000, 20261227);
    CHECK(policy_check(&C, NULL, NULL, &t).decision == POLICY_ALLOW, "after lockout");
    t.signers = 1;
    r = policy_check(&C, NULL, NULL, &t);
    CHECK(r.decision == POLICY_DENY && r.deny_mask == POLICY_R_SIGN, "one signature of two");
    t.signers = 6;
    r = policy_check(&C, NULL, NULL, &t);
    CHECK(r.decision == POLICY_DENY && r.deny_mask == POLICY_R_PAYER, "payer did not sign");
    t = txn(0, 1, 2000001, 20261101);
    r = policy_check(&C, NULL, NULL, &t);
    CHECK(r.decision == POLICY_DENY && (r.deny_mask & POLICY_R_CAP), "over cap");
    t = txn(0, 1, 2000000, 20261101);
    r = policy_check(&C, NULL, NULL, &t);
    CHECK(r.decision == POLICY_DENY && r.deny_mask == POLICY_R_LIMIT, "over limit on its own");
    t = txn(1, 2, 2000000, 20261101);
    CHECK(policy_check(&C, NULL, NULL, &t).decision == POLICY_ALLOW, "limit applies to ACME only");
    t = txn(0, 1, 5, 20261101);
    memcpy(t.asset, "GBP", 4);
    r = policy_check(&C, NULL, NULL, &t);
    CHECK(r.decision == POLICY_DENY && r.deny_mask == POLICY_R_ASSET, "asset not in caps");

    /* cumulative limit through commit */
    policy_state_t s;
    policy_state_init(&s);
    t = txn(0, 1, 1000000, 20261101);
    CHECK(policy_commit(&C, NULL, &s, &t).decision == POLICY_ALLOW && s.spent[0] == 1000000, "commit 1");
    t.amount = 500000;
    CHECK(policy_commit(&C, NULL, &s, &t).decision == POLICY_ALLOW && s.spent[0] == 1500000, "commit 2");
    t.amount = 1;
    r = policy_commit(&C, NULL, &s, &t);
    CHECK(r.decision == POLICY_DENY && r.deny_mask == POLICY_R_LIMIT && s.spent[0] == 1500000,
          "commit 3 refused, total unchanged");
    s.spent[0] = -1;
    CHECK(policy_check(&C, NULL, &s, &t).fault == POLICY_E_STATE, "corrupt state is a fault");

    /* jurisdiction tags: allow list, deny of a subdivision, prefix cover */
    t = txn(0, 1, 10, 20261101);
    strcpy(t.from_jur, "US-NY");
    r = policy_check(&C, NULL, NULL, &t);
    CHECK(r.decision == POLICY_DENY && r.escalate && (r.both_mask & POLICY_R_JUR_FROM) && r.value == B_,
          "US allowed and US-NY denied: BOTH, escalated (value %d)", r.value);
    strcpy(t.from_jur, "FR");
    r = policy_check(&C, NULL, NULL, &t);
    CHECK(r.decision == POLICY_DENY && r.deny_mask == POLICY_R_JUR_FROM && !r.escalate, "FR not allowed");

    /* faults */
    t = txn(0, 1, 10, 20261101);
    t.doc[31] ^= 1;
    r = policy_check(&C, NULL, NULL, &t);
    CHECK(r.decision == POLICY_FAULT && r.fault == POLICY_E_DOC, "wrong document");
    t = txn(0, 1, 0, 20261101);
    CHECK(policy_check(&C, NULL, NULL, &t).fault == POLICY_E_TXN, "zero amount");
    t = txn(0, 1, 10, 20261101);
    t.to = 0;
    CHECK(policy_check(&C, NULL, NULL, &t).fault == POLICY_E_TXN, "to self");
    t.to = 3;
    CHECK(policy_check(&C, NULL, NULL, &t).fault == POLICY_E_TXN, "unknown party");
    t = txn(0, 1, 10, 20261131);
    CHECK(policy_check(&C, NULL, NULL, &t).fault == POLICY_E_TXN, "bad date");
    zxp_contract_t bad = C;
    bad.sign = 9;
    t = txn(0, 1, 10, 20261101);
    CHECK(policy_check(&bad, NULL, NULL, &t).fault == ZXP_E_SIGN, "invalid contract");
    CHECK(policy_check(NULL, NULL, NULL, &t).decision == POLICY_FAULT, "null contract");

    /* in force */
    CHECK(policy_in_force(&C, 3, 20261101) == T_ && policy_in_force(&C, 1, 20261101) == F_ &&
              policy_in_force(&C, 3, 20261225) == F_ && policy_in_force(&C, 3, 20280101) == F_ &&
              policy_in_force(&C, 0xFFF8, 20261101) == F_,
          "in force");
}

static lpres_state_t deny_cargo(void *ctx, const zxp_contract_t *c, const policy_txn_t *t)
{
    (void) c;
    (*(int *) ctx)++;
    return t->to == 2 ? F_ : N_;
}
static lpres_state_t bad_hook(void *ctx, const zxp_contract_t *c, uint32_t i)
{
    (void) ctx;
    (void) c;
    (void) i;
    return (lpres_state_t) 7;
}

static void test_packs(void)
{
    policy_host_t h;
    policy_pack_t ke, gb, us, other;
    policy_host_init(&h);
    CHECK(policy_pack_init(&ke, "KE", "mixed", 0) && policy_pack_init(&gb, "GB", "common", 0) &&
              policy_pack_init(&us, "US", "common", 0) && policy_pack_init(&other, "JP", "civil", 0),
          "pack init");
    CHECK(!policy_pack_init(&other, "J", "civil", 0) && !policy_pack_init(&other, "JP", "civil", 0x80),
          "pack init refusals");
    policy_pack_init(&other, "JP", "civil", 0);
    CHECK(policy_pack_applies(&ke, &C) && policy_pack_applies(&us, &C) && !policy_pack_applies(&other, &C),
          "applies by LAW or party tag");

    /* The contract ALLOWs GB; the KE pack denies GB: BOTH -> escalated deny. */
    ke.n_deny = 1;
    strcpy(ke.deny[0], "GB");
    CHECK(policy_host_register(&h, &ke) && policy_host_register(&h, &other), "register");
    policy_txn_t t = txn(1, 2, 10, 20261101);
    policy_result_t r = policy_check(&C, &h, NULL, &t);
    CHECK(r.decision == POLICY_DENY && r.escalate && r.both_mask == POLICY_R_JUR_TO && r.value == B_,
          "contract allow + pack deny = BOTH (value %d)", r.value);
    /* a pack that does not apply has no say */
    other.n_deny = 1;
    strcpy(other.deny[0], "KE");
    t = txn(0, 1, 10, 20261101);
    CHECK(policy_check(&C, &h, NULL, &t).decision == POLICY_ALLOW, "JP pack ignored");

    /* A pack's transfer hook (where a host binds juris_check_transfer). */
    int calls = 0;
    us.txn_fn = deny_cargo;
    us.ctx = &calls;
    policy_host_t h2;
    policy_host_init(&h2);
    CHECK(policy_host_register(&h2, &us), "register us");
    t = txn(0, 2, 10, 20261101);
    t.signers = 5;
    r = policy_check(&C, &h2, NULL, &t);
    CHECK(r.decision == POLICY_DENY && r.deny_mask == POLICY_R_PACK && calls == 1, "hook denies");
    t = txn(0, 1, 10, 20261101);
    CHECK(policy_check(&C, &h2, NULL, &t).decision == POLICY_ALLOW && calls == 2, "hook neutral");

    /* registration refusals */
    policy_pack_t unsorted;
    policy_pack_init(&unsorted, "KE", "x", 0);
    unsorted.n_allow = 2;
    strcpy(unsorted.allow[0], "US");
    strcpy(unsorted.allow[1], "GB");
    CHECK(!policy_host_register(&h2, &unsorted) && !policy_host_register(&h2, NULL), "bad packs refused");
    for (int i = 0; i < 20; i++) policy_host_register(&h2, &other);
    CHECK(h2.n == POLICY_MAX_PACKS, "host full at %u", h2.n);

    /* NO_PENALTY flag and DEFER flag on terms; an out-of-range hook is N */
    policy_host_t h3;
    policy_pack_t np, df, hk;
    policy_host_init(&h3);
    policy_pack_init(&np, "KE", "islamic-config", POLICY_PACK_NO_PENALTY);
    policy_pack_init(&df, "KE", "customary", POLICY_PACK_DEFER);
    policy_pack_init(&hk, "KE", "hook", 0);
    hk.term_fn = bad_hook;
    policy_host_register(&h3, &df);
    policy_host_register(&h3, &hk);
    policy_term_t tm[ZXP_MAX_CLAUSES];
    CHECK(policy_terms(&C, &h3, NULL, 0, 20261101, tm) == 0 && tm[0].compliance == N_, "defer + bad hook: N");
    policy_host_register(&h3, &np);
    policy_terms(&C, &h3, NULL, 0, 20261101, tm);
    CHECK(tm[0].compliance == F_ && tm[1].compliance == T_, "penalty term refused by config");
}

static void test_terms(void)
{
    policy_term_t tm[ZXP_MAX_CLAUSES];
    /* nothing attested, before any date */
    CHECK(policy_terms(&C, NULL, NULL, 0, 20261101, tm) == 0, "terms");
    CHECK(tm[0].status == POLICY_T_OPEN && tm[0].value == N_ && tm[1].status == POLICY_T_WAITING &&
              tm[3].status == POLICY_T_GATE && tm[3].value == N_ && tm[5].status == POLICY_T_WAITING,
          "initial: open / waiting");
    /* on the date, still nothing attested */
    policy_terms(&C, NULL, NULL, 0, 20261231, tm);
    CHECK(tm[0].status == POLICY_T_DUE && tm[0].value == N_, "due today");
    /* lapse without evidence is a breach */
    policy_terms(&C, NULL, NULL, 0, 20270101, tm);
    CHECK(tm[0].status == POLICY_T_BREACHED && tm[0].value == F_ && tm[1].status == POLICY_T_EXCUSED &&
              tm[1].value == T_,
          "lapsed: breach; dependent excused");

    policy_att_t a[4] = {
        {0, T_, 20261220, 7}, /* observer 7: T1 performed */
        {2, T_, 20261230, 7}, /* T3 performed */
        {0, F_, 20261222, 9}, /* observer 9: T1 not performed */
        {2, T_, 20270105, 9}, /* dated after `now` below: ignored */
    };
    policy_terms(&C, NULL, a, 2, 20261231, tm);
    CHECK(tm[0].status == POLICY_T_PERFORMED && tm[0].value == T_ && tm[0].performed_on == 20261220 &&
              tm[1].status == POLICY_T_OPEN && tm[3].value == T_ && tm[4].value == T_ &&
              tm[5].status == POLICY_T_OPEN,
          "performed; AFTER T1 now open; G1 and G2 true; T4 open");
    /* a second observer contradicts: N v T = T, T v F = B, nothing else stops */
    policy_terms(&C, NULL, a, 4, 20261231, tm);
    CHECK(tm[0].evidence == B_ && tm[0].status == POLICY_T_CONTESTED && tm[0].value == B_ &&
              tm[0].att_true == 1 && tm[0].att_false == 4,
          "contested evidence");
    CHECK(tm[2].status == POLICY_T_PERFORMED && tm[2].att_true == 2, "T3 unaffected, future att ignored");
    CHECK(tm[1].status == POLICY_T_CONTESTED && tm[1].condition == B_, "AFTER a contested term");
    CHECK(tm[3].value == B_ && tm[4].value == T_, "ALL(B,T)=B, ANY(B,T)=T");
    CHECK(tm[5].status == POLICY_T_CONTESTED, "AFTER G1 contested");
    /* kernel rule inside gates: ALL(B, N) = N, ANY(B, N) = B */
    policy_att_t only_contest[2] = {{0, T_, 20261220, 7}, {0, F_, 20261222, 9}};
    policy_terms(&C, NULL, only_contest, 2, 20261225, tm);
    CHECK(tm[0].value == B_ && tm[2].value == N_ && tm[3].value == N_ && tm[4].value == B_,
          "gates on B and N: ALL %d ANY %d", tm[3].value, tm[4].value);
    /* late performance */
    policy_att_t late[1] = {{0, T_, 20270110, 7}};
    policy_terms(&C, NULL, late, 1, 20270201, tm);
    CHECK(tm[0].status == POLICY_T_LATE && tm[0].value == T_, "late");
    /* refusals */
    policy_att_t badv[1] = {{0, B_, 20261220, 7}};
    CHECK(policy_terms(&C, NULL, badv, 1, 20261231, tm) == POLICY_E_TXN, "verdict must be T or F");
    CHECK(policy_terms(&C, NULL, a, POLICY_MAX_ATTS + 1u, 20261231, tm) == POLICY_E_TXN, "too many");
    CHECK(policy_terms(&C, NULL, a, 1, 20261232, tm) == POLICY_E_TXN, "bad now");
    char text[200];
    uint32_t n = policy_att_text(&C, &a[0], text, sizeof text);
    CHECK(n > 0 && strcmp(text, "ATT SUPPLY-2027 T1 T 20261220 7 "
                                "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08\n") == 0,
          "attestation text '%s'", text);
    policy_att_t gate_att = {3, T_, 20261220, 1};
    CHECK(policy_att_text(&C, &gate_att, text, sizeof text) == 0, "no attestation on a gate");
    CHECK(policy_att_text(&C, &a[0], text, 20) == 0, "short buffer");
}

static zxv_engine_t WS;

static int64_t run_net(const zxt_treaty_t *t, zxt_result_t *r)
{
    uint8_t seed[32] = {1};
    int64_t sum = 0;
    if (zxt_exec(t, &WS, seed, r) != ZXT_OK) return -1;
    for (uint32_t i = 0; i < r->n_party; i++) sum += r->net[i];
    return sum;
}

static void test_lower(void)
{
    static zxt_treaty_t t;
    policy_settle_t rep;
    zxt_result_t r;
    char asset[4] = "USD";

    /* before any date: everything deferred, an empty batch */
    CHECK(policy_lower(&C, NULL, NULL, 0, 20261101, asset, &t, &rep) == ZXT_OK && t.n_owe == 0 &&
              rep.n_deferred == 4 && t.n_party == 3,
          "all deferred: %u", rep.n_deferred);
    /* on 20261231: T1 and T3 due today */
    CHECK(policy_lower(&C, NULL, NULL, 0, 20261231, asset, &t, &rep) == ZXT_OK && t.n_owe == 2 &&
              t.owe[0].from == 0 && t.owe[0].to == 1 && t.owe[0].amount == 1200000 &&
              t.owe[1].amount == 50000,
          "due today lowered");
    CHECK(run_net(&t, &r) == 0 && r.net[0] == -1200000 && r.net[2] == 50000, "nets to zero");
    /* after the date with nothing attested: amount plus penalty */
    CHECK(policy_lower(&C, NULL, NULL, 0, 20270102, asset, &t, &rep) == ZXT_OK && t.n_owe == 3 &&
              t.owe[1].amount == 12000,
          "breach: amount + penalty (%u lines)", t.n_owe);
    /* performed terms produce nothing; contested T1 goes to escrow */
    policy_att_t a[3] = {{0, T_, 20261220, 7}, {2, T_, 20261230, 7}, {0, F_, 20261222, 9}};
    CHECK(policy_lower(&C, NULL, a, 2, 20261231, asset, &t, &rep) == ZXT_OK && t.n_owe == 0 &&
              rep.n_escrow == 0,
          "performed: nothing to settle");
    CHECK(policy_lower(&C, NULL, a, 3, 20261231, asset, &t, &rep) == ZXT_OK && rep.n_escrow == 1 &&
              t.n_party == 4 && strcmp(t.party[3].id, "ESCROW") == 0 && t.owe[0].to == 3 &&
              t.owe[0].amount == 1200000 && rep.escrow[0].reason == POLICY_ESC_EVIDENCE &&
              rep.escrow[0].offset == (int32_t) (strstr(SRC, "DUE T1") - SRC),
          "contested: escrow line + offset");
    CHECK(run_net(&t, &r) == 0 && r.net[3] == 1200000, "escrow holds the contested amount");
    CHECK(t.party[0].w_num == 1 && t.party[0].w_den == 4, "equal weights");
    /* the mediation bundle */
    char b[600];
    uint32_t n = policy_bundle_text(&C, &rep.escrow[0], a, 3, b, sizeof b);
    char want[600];
    snprintf(want, sizeof want,
             "MEDIATION SUPPLY-2027 T1 AT %d EVIDENCE\n"
             "DOC 9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08\n"
             "AMOUNT USD 1200000\nFOR 7 20261220\nAGAINST 9 20261222\nEND\n",
             rep.escrow[0].offset);
    CHECK(n == strlen(want) && strcmp(b, want) == 0, "bundle:\n%s", b);
    CHECK(policy_bundle_text(&C, &rep.escrow[0], a, 3, b, 30) == 0, "bundle short buffer");
    /* a pack that refuses penalties: T1 blocked, the rest settles */
    policy_host_t h;
    policy_pack_t np;
    policy_host_init(&h);
    policy_pack_init(&np, "KE", "config", POLICY_PACK_NO_PENALTY);
    policy_host_register(&h, &np);
    CHECK(policy_lower(&C, &h, NULL, 0, 20261231, asset, &t, &rep) == ZXT_OK && rep.n_blocked == 1 &&
              rep.blocked[0] == 0 && t.n_owe == 1 && t.owe[0].amount == 50000,
          "blocked by pack, rest settles");
    /* other asset: nothing */
    char eur[4] = "EUR";
    CHECK(policy_lower(&C, NULL, NULL, 0, 20270102, eur, &t, &rep) == ZXT_OK && t.n_owe == 0,
          "other asset");
    char bad[4] = "usd";
    CHECK(policy_lower(&C, NULL, NULL, 0, 20270102, bad, &t, &rep) == ZXP_E_ARG, "bad asset");
}

static lpres_state_t all_true(void *ctx, const zxp_contract_t *c, uint32_t i)
{
    (void) ctx;
    (void) c;
    (void) i;
    return T_;
}

static void test_compliance_escrow(void)
{
    static zxt_treaty_t t;
    policy_settle_t rep;
    policy_host_t h;
    policy_pack_t np, yes;
    policy_host_init(&h);
    policy_pack_init(&np, "KE", "config-a", POLICY_PACK_NO_PENALTY);
    policy_pack_init(&yes, "US", "config-b", 0);
    yes.term_fn = all_true;
    policy_host_register(&h, &np);
    policy_host_register(&h, &yes);
    /* T1 (penalty): KE says F, US says T -> B: escrowed when due */
    CHECK(policy_lower(&C, &h, NULL, 0, 20261231, "USD", &t, &rep) == ZXT_OK && rep.n_escrow == 1 &&
              rep.escrow[0].clause == 0 && rep.escrow[0].reason == POLICY_ESC_COMPLIANCE &&
              t.n_owe == 2,
          "compliance conflict escrowed, T3 settles");
    /* before its date it defers */
    CHECK(policy_lower(&C, &h, NULL, 0, 20261101, "USD", &t, &rep) == ZXT_OK && rep.n_escrow == 0, "defers");
}

static void test_cobol_records(void)
{
    static uint8_t crec[ZXP_REC_LEN];
    uint8_t q[POLICY_TXN_REC_LEN + 1], res[POLICY_RES_REC_LEN];
    policy_txn_t t;
    CHECK(zxp_emit_record(&C, crec, sizeof crec) == ZXP_OK, "contract record");
    memcpy(q, "ZXQ10102USD+000000000001000000"
              "20261101US-DE KE    YYNNNNNNNNNNNNN"
              "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08",
           POLICY_TXN_REC_LEN);
    CHECK(policy_parse_txn_record(q, POLICY_TXN_REC_LEN, &t) == 0 && t.from == 0 && t.to == 1 &&
              t.amount == 1000000 && t.signers == 3 && strcmp(t.to_jur, "KE") == 0,
          "txn record");
    CHECK(ZXV_POLICY((const char *) crec, (const char *) q, (char *) res) == 0, "ZXV_POLICY allow");
    CHECK(memcmp(res, "ZXP1ALLOWNTNNNNNNNNNNNNNNNNNNNN+000000000", POLICY_RES_REC_LEN) == 0,
          "result record %.41s", res);
    q[50 + 1] = 'N';
    CHECK(ZXV_POLICY((const char *) crec, (const char *) q, (char *) res) == 1 &&
              memcmp(res + 4, "DENY NFNNYNNNNNN", 16) == 0,
          "deny: signatures (%.41s)", res);
    q[50 + 1] = 'X';
    CHECK(ZXV_POLICY((const char *) crec, (const char *) q, (char *) res) == POLICY_E_TXN &&
              memcmp(res + 4, "FAULTNF", 7) == 0 && memcmp(res + 31, "-0040", 5) == 0,
          "fault: bad signer flag (%.41s)", res);
    q[50 + 1] = 'Y';
    q[11] = '-';
    CHECK(policy_parse_txn_record(q, POLICY_TXN_REC_LEN, &t) == POLICY_E_TXN, "negative amount");
    q[11] = '+';
    crec[ZXP_REC_HEAD + 2] = '?'; /* LAW table byte */
    CHECK(ZXV_POLICY((const char *) crec, (const char *) q, (char *) res) == ZXP_E_JUR &&
              memcmp(res + 36, "00137", 5) == 0,
          "contract fault with offset (%.41s)", res);
}

int main(void)
{
    uint32_t off = 0;
    if (zxp_parse_sutra(SRC, (uint32_t) strlen(SRC), &C, &off) != ZXP_OK) {
        printf("FAIL: fixture does not parse (offset %u)\n", off);
        return 1;
    }
    test_belnap_ops();
    test_transfers();
    test_packs();
    test_terms();
    test_lower();
    test_compliance_escrow();
    test_cobol_records();
    printf("%s: %d passed, %d failed\n", g_fail ? "FAIL" : "PASS", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
