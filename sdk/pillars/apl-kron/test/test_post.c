/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_post.c — the posting bridge: a KRON result becomes libzxv ledger
 * postings through zxv_ledger_post_cap, and every refusal comes back as a
 * status: the capability, funds, duplicate references, amounts that are
 * not whole minor units, and a halted (poisoned) context. */
#include "kr_test.h"

static kr_value *val(const char *apl) {
    kr_program *p = prog_s(apl, KR_SPELL_APL);
    kr_value *v = NULL;
    kr_diag d;
    if (p && kr_eval(p, NULL, 0, 0, &v, &d) != ZXV_OK) fprintf(stderr, "%s: %s\n", apl, d.msg);
    kr_program_destroy(p);
    return v;
}

static int64_t equity(const zxv_cap *cap, uint32_t acct) {
    int64_t e = -1;
    zxv_ledger_balance_cap(cap, acct, NULL, NULL, &e);
    return e;
}

int main(void) {
    zxv_ctx *ctx = NULL;
    zxv_cap *root = NULL, *poster = NULL, *reader = NULL;
    uint32_t bank = 0, alice = 0, bob = 0;
    uint64_t ids[8], posted = 99;
    kr_value *v;
    int32_t st;

    CHECK(zxv_ctx_create(&ctx) == ZXV_OK, "context");
    CHECK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root) == ZXV_OK, "root cap");
    CHECK(zxv_ccy_enable_cap(root, "USD") == ZXV_OK, "USD");
    zxv_ledger_open_account_cap(root, "bank", "USD", ZXV_ACCT_ISSUER, &bank);
    zxv_ledger_open_account_cap(root, "alice", "USD", ZXV_ACCT_HOLDER, &alice);
    zxv_ledger_open_account_cap(root, "bob", "USD", ZXV_ACCT_HOLDER, &bob);
    CHECK(zxv_cap_attenuate(root, ZXV_CAP_LEDGER_POST | ZXV_CAP_LEDGER_READ, &poster) == ZXV_OK, "poster cap");
    CHECK(zxv_cap_attenuate(root, ZXV_CAP_LEDGER_READ, &reader) == ZXV_OK, "reader cap");

    /* 1. a payroll computed exactly: 3 salaries of 1000.00 USD split 1r3
     *    each, in cents: 100000 × 1r3 is not whole, so the program rounds
     *    nothing and must say how: ⌊ gives the cents, the remainder goes
     *    to the last one */
    v = val("s←⌊100000×3⍴1r3 ⋄ s+0 0,100000-+/s");
    CHECK(v && strcmp(vtext(v), "(vec 33333 33333 33334)") == 0, "the split: %s", vtext(v));
    st = kr_post(poster, v, bank, alice, "USD", "PAYROLL", ids, 8, &posted);
    CHECK(st == ZXV_OK && posted == 3, "three postings: %s %s", kr_status_name(st), last_err());
    CHECK(ids[0] > 0 && ids[1] > ids[0] && ids[2] > ids[1], "entry ids");
    CHECK(equity(poster, alice) == 100000, "alice got exactly 1000.00: %lld", (long long) equity(poster, alice));
    kr_value_destroy(v);

    /* 2. alice pays bob a scalar */
    v = val("250×100");
    CHECK(kr_post(poster, v, alice, bob, "USD", "PAY", NULL, 0, &posted) == ZXV_OK && posted == 1, "a scalar posts");
    CHECK(equity(poster, alice) == 75000 && equity(poster, bob) == 25000, "balances move exactly");
    kr_value_destroy(v);

    /* 3. refusals */
    v = val("1r2 3");
    CHECK(kr_post(poster, v, alice, bob, "USD", "HALF", NULL, 0, &posted) == KR_E_DOMAIN && posted == 0,
          "a fraction of a cent is refused before any posting");
    kr_value_destroy(v);
    v = val("5 0");
    CHECK(kr_post(poster, v, alice, bob, "USD", "ZERO", NULL, 0, &posted) == ZXV_E_RANGE && posted == 0, "zero");
    kr_value_destroy(v);
    v = val("1 2<3");
    CHECK(kr_post(poster, v, alice, bob, "USD", "T", NULL, 0, &posted) == KR_E_DOMAIN, "truth is not money");
    kr_value_destroy(v);
    v = val("2 2⍴1");
    CHECK(kr_post(poster, v, alice, bob, "USD", "M", NULL, 0, &posted) == KR_E_RANK, "a matrix");
    kr_value_destroy(v);
    v = val("100 200");
    CHECK(kr_post(reader, v, alice, bob, "USD", "RO", NULL, 0, &posted) == ZXV_E_CAP && posted == 0, "read-only cap");
    CHECK(kr_post(poster, v, alice, bob, "USD", "PAY", NULL, 0, &posted) == ZXV_E_DUPLICATE && posted == 0,
          "PAY-0 was used: %s", last_err());
    CHECK(strstr(last_err(), "PAY-0") != NULL, "the message names the reference: %s", last_err());
    CHECK(kr_post(poster, v, alice, bob, "EUR", "EU", NULL, 0, &posted) != ZXV_OK, "a currency the accounts do not hold");
    CHECK(kr_post(poster, v, alice, bob, "USD", "THIS-PREFIX-IS-FAR-TOO-LONG", NULL, 0, &posted) == ZXV_E_TOO_LONG,
          "reference prefix");
    CHECK(kr_post(NULL, v, alice, bob, "USD", "X", NULL, 0, &posted) == ZXV_E_ARG, "no cap");
    kr_value_destroy(v);
    /* a partial batch: the ledger has no batch transaction */
    v = val("1000 99999999");
    st = kr_post(poster, v, alice, bob, "USD", "BIG", NULL, 0, &posted);
    CHECK(st == ZXV_E_FUNDS && posted == 1, "the second posting overdraws: %s, %llu posted", kr_status_name(st),
          (unsigned long long) posted);
    CHECK(equity(poster, alice) == 74000 && equity(poster, bob) == 26000, "the first one stands");
    kr_value_destroy(v);
    /* revocation */
    zxv_cap_revoke(poster);
    v = val("1");
    CHECK(kr_post(poster, v, alice, bob, "USD", "REV", NULL, 0, &posted) == ZXV_E_CAP, "a revoked cap");
    kr_value_destroy(v);
    /* a halted context fails closed */
    {
        zxv_cap *p2 = NULL;
        zxv_cap_attenuate(root, ZXV_CAP_LEDGER_POST, &p2);
        zxv_ctx_halt(ctx, "test");
        v = val("1");
        st = kr_post(p2, v, alice, bob, "USD", "HALT", NULL, 0, &posted);
        CHECK(st == ZXV_E_POISONED, "poisoned: %s", kr_status_name(st));
        CHECK(kr_status_class(st) == ZXV_CLASS_FATAL, "fatal class");
        kr_value_destroy(v);
        zxv_cap_destroy(p2);
    }
    zxv_cap_destroy(reader);
    zxv_cap_destroy(poster);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
    return report("test_post");
}
