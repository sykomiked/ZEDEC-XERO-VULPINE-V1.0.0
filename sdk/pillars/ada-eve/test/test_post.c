/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_post.c — the posting bridge: a record read exactly into minor
 * units and posted through zxv_ledger_post_cap. The ledger decides
 * (capability, funds, currency, duplicate reference); every refusal comes
 * back unchanged, including ZXV_E_POISONED from a halted context. */
#include "ze_test.h"

static const char k_layout[] =
    "eve pay\n"
    "currency = (ngn, kes, jpy, usd)\n"
    "acct = 0 .. 2**32 - 1\n"
    "money = dec(15, 2)\nwhole = dec(12, 2)\n"
    "payment {\n"
    "  ref: str(12)\n"
    "  amount: money\n"
    "  ccy: currency\n"
    "  code: str(3)\n"
    "  from, to: acct\n"
    "  dfrom: whole\n"
    "}\n";

typedef struct {
    ze_pkg *p;
    uint32_t t;
    size_t n;
    uint8_t v[128];
} rec;

static void fill(rec *r, const char *ref, const char *amount, const char *ccy, uint32_t from, uint32_t to) {
    char b[24];
    if (ze_value_init(r->p, r->t, r->v, r->n) != ZXV_OK) abort();
    if (put(r->p, r->t, r->v, r->n, "ref", ref) != ZXV_OK) abort();
    if (put(r->p, r->t, r->v, r->n, "amount", amount) != ZXV_OK) abort();
    if (put(r->p, r->t, r->v, r->n, "ccy", ccy) != ZXV_OK) abort();
    if (put(r->p, r->t, r->v, r->n, "code", ccy) != ZXV_OK) abort();
    snprintf(b, sizeof b, "%u", (unsigned) from);
    if (put(r->p, r->t, r->v, r->n, "from", b) != ZXV_OK) abort();
    snprintf(b, sizeof b, "%u", (unsigned) to);
    if (put(r->p, r->t, r->v, r->n, "to", b) != ZXV_OK) abort();
}

int main(void) {
    rec r;
    zxv_ctx *ctx = NULL;
    zxv_cap *root = NULL, *reader = NULL;
    uint32_t bank = 0, alice = 0, bob = 0;
    ze_posting po;
    int64_t d, c, e;
    ze_posting_map m = {"amount", "from", "to", "ccy", NULL, "ref", -1};
    ze_posting_map ms = {"amount", "from", "to", "code", NULL, "ref", -1};

    r.p = parse(0, k_layout);
    CHECK(r.p != NULL, "layout");
    if (!r.p) return report("test_post");
    r.t = type_of(r.p, "payment");
    r.n = (size_t) size_of(r.p, r.t);

    CHECK(zxv_ctx_create(&ctx) == ZXV_OK && zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root) == ZXV_OK &&
              zxv_cap_attenuate(root, ZXV_CAP_LEDGER_READ, &reader) == ZXV_OK,
          "context and capabilities");
    CHECK(zxv_ccy_enable_cap(root, "NGN") == ZXV_OK && zxv_ccy_enable_cap(root, "JPY") == ZXV_OK &&
              zxv_ledger_open_account_cap(root, "bank", "NGN", ZXV_ACCT_ISSUER, &bank) == ZXV_OK &&
              zxv_ledger_open_account_cap(root, "alice", "NGN", ZXV_ACCT_HOLDER, &alice) == ZXV_OK &&
              zxv_ledger_open_account_cap(root, "bob", "NGN", ZXV_ACCT_HOLDER, &bob) == ZXV_OK,
          "accounts");

    /* reading */
    fill(&r, "FUND-1", "1234.56", "ngn", bank, alice);
    CHECK(ze_posting_read(r.p, r.t, &m, r.v, r.n, &po) == ZXV_OK && po.amount_minor == 123456 && po.from == bank &&
              po.to == alice && strcmp(po.currency, "NGN") == 0 && strcmp(po.reference, "FUND-1") == 0 &&
              po.entry_id == 0,
          "read: 1234.56 NGN is 123456 minor units, the literal upper-cased");
    CHECK(ze_posting_read(r.p, r.t, &ms, r.v, r.n, &po) == ZXV_E_CURRENCY,
          "a String currency is taken as written: 'ngn' is not an ISO 4217 code");
    if (put(r.p, r.t, r.v, r.n, "code", "NGN") != ZXV_OK) abort();
    CHECK(ze_posting_read(r.p, r.t, &ms, r.v, r.n, &po) == ZXV_OK && strcmp(po.currency, "NGN") == 0,
          "a String currency NGN");
    {
        ze_posting_map mf = m;
        mf.currency = NULL;
        mf.fixed_currency = "NGN";
        CHECK(ze_posting_read(r.p, r.t, &mf, r.v, r.n, &po) == ZXV_OK && strcmp(po.currency, "NGN") == 0,
              "a fixed currency");
        mf.minor_units = 0;
        CHECK(ze_posting_read(r.p, r.t, &mf, r.v, r.n, &po) == ZE_E_FIT, "1234.56 has no whole minor units at 0");
        mf.minor_units = 4;
        CHECK(ze_posting_read(r.p, r.t, &mf, r.v, r.n, &po) == ZXV_OK && po.amount_minor == 12345600, "4 minor units");
        mf.fixed_currency = NULL;
        CHECK(ze_posting_read(r.p, r.t, &mf, r.v, r.n, &po) == ZXV_E_ARG, "no currency at all");
        mf = m;
        mf.from = "dfrom";
        if (put(r.p, r.t, r.v, r.n, "dfrom", "1.5") != ZXV_OK) abort();
        CHECK(ze_posting_read(r.p, r.t, &mf, r.v, r.n, &po) == ZE_E_FIT, "an account of 1.5 is refused");
        if (put(r.p, r.t, r.v, r.n, "dfrom", "2.00") != ZXV_OK) abort();
        CHECK(ze_posting_read(r.p, r.t, &mf, r.v, r.n, &po) == ZXV_OK && po.from == 2, "2.00 is account 2");
        mf = m;
        mf.amount = "ref";
        CHECK(ze_posting_read(r.p, r.t, &mf, r.v, r.n, &po) == ZXV_E_ARG, "the amount must be a number");
        mf = m;
        mf.reference = "nope";
        CHECK(ze_posting_read(r.p, r.t, &mf, r.v, r.n, &po) == ZXV_E_NOT_FOUND, "an unknown path");
        mf = m;
        mf.currency = "amount";
        CHECK(ze_posting_read(r.p, r.t, &mf, r.v, r.n, &po) == ZXV_E_ARG, "a number is not a currency");
    }
    fill(&r, "X", "1.00", "jpy", bank, alice);
    CHECK(ze_posting_read(r.p, r.t, &m, r.v, r.n, &po) == ZXV_OK && po.amount_minor == 1, "JPY has 0 minor units");
    fill(&r, "X", "1.50", "jpy", bank, alice);
    CHECK(ze_posting_read(r.p, r.t, &m, r.v, r.n, &po) == ZE_E_FIT, "1.50 JPY is refused, never rounded");

    /* posting */
    fill(&r, "FUND-1", "1234.56", "ngn", bank, alice);
    CHECK(ze_post(reader, r.p, r.t, &m, r.v, r.n, &po) == ZXV_E_CAP, "a reader cannot post");
    CHECK(ze_post(root, r.p, r.t, &m, r.v, r.n, &po) == ZXV_OK && po.entry_id == 1, "posted as entry 1");
    CHECK(zxv_ledger_balance_cap(reader, alice, &d, &c, &e) == ZXV_OK && e == 123456, "alice holds 123456");
    CHECK(ze_post(root, r.p, r.t, &m, r.v, r.n, NULL) == ZXV_E_DUPLICATE, "the same reference twice");
    fill(&r, "PAY-1", "2000.00", "ngn", alice, bob);
    CHECK(ze_post(root, r.p, r.t, &m, r.v, r.n, &po) == ZXV_E_FUNDS, "alice cannot go below zero");
    fill(&r, "PAY-2", "1.00", "kes", alice, bob);
    CHECK(ze_post(root, r.p, r.t, &m, r.v, r.n, &po) == ZXV_E_CURRENCY, "KES is not enabled");
    fill(&r, "PAY-3", "0.00", "ngn", alice, bob);
    CHECK(ze_post(root, r.p, r.t, &m, r.v, r.n, &po) == ZXV_E_RANGE, "amount 0");
    fill(&r, "PAY-4", "-1.00", "ngn", alice, bob);
    CHECK(ze_post(root, r.p, r.t, &m, r.v, r.n, &po) == ZXV_E_RANGE, "a negative amount");
    fill(&r, "PAY-5", "34.56", "ngn", alice, bob);
    CHECK(ze_post(root, r.p, r.t, &m, r.v, r.n, &po) == ZXV_OK && po.entry_id == 2, "posted as entry 2");
    CHECK(zxv_ledger_balance_cap(reader, bob, &d, &c, &e) == ZXV_OK && e == 3456, "bob holds 34.56");
    CHECK(zxv_ledger_check_cap(reader) == ZXV_OK, "the books balance");
    CHECK(ze_post(NULL, r.p, r.t, &m, r.v, r.n, &po) == ZXV_E_ARG, "no capability");

    /* a halted context: every posting is ZXV_E_POISONED, returned as is */
    CHECK(zxv_ctx_halt(ctx, "test_post: deliberate halt") == ZXV_OK, "halt");
    fill(&r, "PAY-6", "1.00", "ngn", alice, bob);
    CHECK(ze_post(root, r.p, r.t, &m, r.v, r.n, &po) == ZXV_E_POISONED, "a poisoned context");
    CHECK(ze_status_class(ZXV_E_POISONED) == ZXV_CLASS_FATAL, "which is FATAL");

    zxv_cap_destroy(reader);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
    ze_pkg_destroy(r.p);
    return report("test_post");
}
