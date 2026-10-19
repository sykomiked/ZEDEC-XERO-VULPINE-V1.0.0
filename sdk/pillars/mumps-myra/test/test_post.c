/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_post.c — the posting bridge: global nodes become libzxv ledger
 * postings through zxv_ledger_post_cap, and every refusal comes back as a
 * status: the capability, funds, duplicate references, amount range,
 * currency, decimals that do not fit, and a halted (poisoned) context.
 * All names and references are synthetic. */
#include "zm_test.h"

static const zm_posting_map k_map = {"amount", "from", "to", "ccy", NULL, "ref", -1};

static zm_schema *g_s;
static zm_record *g_r;

/* A payment node ^ZXPAY(7,seq) decoded from its value string. */
static int32_t payment(uint32_t seq, const char *ref, const char *amount, const char *ccy,
                       uint32_t from, uint32_t to) {
    char rf[64], v[256];
    zm_record_destroy(g_r);
    g_r = NULL;
    snprintf(rf, sizeof rf, "^ZXPAY(7,%u)", (unsigned) seq);
    snprintf(v, sizeof v, "%s^%s^%s^%u^%u^67854^N", ref, amount, ccy, (unsigned) from, (unsigned) to);
    return zm_node_decode(g_s, rf, strlen(rf), v, strlen(v), &g_r);
}

static int64_t equity(const zxv_cap *cap, uint32_t acct) {
    int64_t e = -1;
    zxv_ledger_balance_cap(cap, acct, NULL, NULL, &e);
    return e;
}

int main(void) {
    zxv_ctx *ctx = NULL;
    zxv_cap *root = NULL, *poster = NULL, *reader = NULL;
    uint32_t bank = 0, alice = 0, bob = 0, jbank = 0, carol = 0, vpool = 0, dan = 0;
    zm_posting p;
    int32_t st, poisoned = 0;

    g_s = myra(k_pay_myra);
    CHECK(g_s != NULL, "payment schema");
    if (!g_s) return report("test_post");

    CHECK(zxv_ctx_create(&ctx) == ZXV_OK, "context");
    CHECK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root) == ZXV_OK, "root cap");
    CHECK(zxv_ccy_enable_cap(root, "NGN") == ZXV_OK && zxv_ccy_enable_cap(root, "JPY") == ZXV_OK,
          "currencies");
    CHECK(zxv_ccy_register_private_cap(root, "VFV", 3) == ZXV_OK, "a private unit");
    zxv_ledger_open_account_cap(root, "bank", "NGN", ZXV_ACCT_ISSUER, &bank);
    zxv_ledger_open_account_cap(root, "alice", "NGN", ZXV_ACCT_HOLDER, &alice);
    zxv_ledger_open_account_cap(root, "bob", "NGN", ZXV_ACCT_HOLDER, &bob);
    zxv_ledger_open_account_cap(root, "jp-bank", "JPY", ZXV_ACCT_ISSUER, &jbank);
    zxv_ledger_open_account_cap(root, "carol", "JPY", ZXV_ACCT_HOLDER, &carol);
    zxv_ledger_open_account_cap(root, "vfv-pool", "VFV", ZXV_ACCT_ISSUER, &vpool);
    zxv_ledger_open_account_cap(root, "dan", "VFV", ZXV_ACCT_HOLDER, &dan);
    /* the bridge gets a narrow cap: post and read, nothing else */
    CHECK(zxv_cap_attenuate(root, ZXV_CAP_LEDGER_POST | ZXV_CAP_LEDGER_READ, &poster) == ZXV_OK,
          "poster cap");
    CHECK(zxv_cap_attenuate(root, ZXV_CAP_LEDGER_READ, &reader) == ZXV_OK, "reader cap");

    /* 1. funding: the issuer pays alice 1000 NGN */
    CHECK(payment(1, "FUND-1", "1000", "NGN", bank, alice) == ZXV_OK, "record");
    st = zm_post(poster, g_r, &k_map, &p);
    CHECK(st == ZXV_OK && p.entry_id > 0, "funding posts: %s", zm_status_name(st));
    CHECK(p.amount_minor == 100000 && strcmp(p.currency, "NGN") == 0 &&
              strcmp(p.reference, "FUND-1") == 0 && p.from == bank && p.to == alice,
          "the posting read from the node");
    CHECK(equity(poster, alice) == 100000, "alice has 1000.00");

    /* 2. alice pays bob 250.25 */
    payment(2, "PAY-0001", "250.25", "NGN", alice, bob);
    CHECK(zm_post(poster, g_r, &k_map, &p) == ZXV_OK, "payment posts");
    CHECK(equity(poster, alice) == 74975 && equity(poster, bob) == 25025, "balances move exactly");

    /* 3. refusals come back as statuses and change nothing */
    payment(3, "PAY-0002", "5000", "NGN", alice, bob);
    CHECK(zm_post(poster, g_r, &k_map, &p) == ZXV_E_FUNDS, "insufficient funds");
    CHECK(strstr(last_err(), "PAY-0002") != NULL, "the message names the reference: %s", last_err());
    payment(4, "PAY-0001", "1", "NGN", alice, bob);
    CHECK(zm_post(poster, g_r, &k_map, &p) == ZXV_E_DUPLICATE, "duplicate reference");
    payment(5, "PAY-0003", "0", "NGN", alice, bob);
    CHECK(zm_post(poster, g_r, &k_map, &p) == ZXV_E_RANGE, "zero amount");
    payment(6, "PAY-0004", "-5", "NGN", alice, bob);
    CHECK(zm_post(poster, g_r, &k_map, &p) == ZXV_E_RANGE, "negative amount");
    CHECK(p.amount_minor == -500, "the node was still read exactly (-500)");
    payment(7, "PAY-0005", "1", "NGN", alice, 999);
    CHECK(zm_post(poster, g_r, &k_map, &p) == ZXV_E_NOT_FOUND, "unknown account");
    payment(8, "PAY-0006", "1", "NGN", alice, alice);
    CHECK(zm_post(poster, g_r, &k_map, &p) == ZXV_E_ARG, "same account");
    payment(9, "PAY-0007", "1", "USD", alice, bob);
    CHECK(zm_post(poster, g_r, &k_map, &p) == ZXV_E_CURRENCY, "USD into NGN accounts (no implicit FX)");
    payment(10, "PAY-0008", "1", "ZZZ", alice, bob);
    CHECK(zm_post(poster, g_r, &k_map, &p) == ZXV_E_CURRENCY, "a code ISO 4217 does not know");
    payment(11, "PAY-0009", "1", "NGN", alice, bob);
    CHECK(zm_post(reader, g_r, &k_map, &p) == ZXV_E_CAP, "a read-only cap");
    CHECK(zm_post(NULL, g_r, &k_map, &p) == ZXV_E_ARG, "no cap");
    CHECK(equity(poster, alice) == 74975 && equity(poster, bob) == 25025, "nothing moved");

    /* 4. exactness against the currency's minor units */
    payment(12, "JP-1", "12", "JPY", jbank, carol);
    CHECK(zm_post(poster, g_r, &k_map, &p) == ZXV_OK && p.amount_minor == 12, "12 JPY is 12 yen");
    payment(13, "JP-2", "12.5", "JPY", jbank, carol);
    CHECK(zm_post(poster, g_r, &k_map, &p) == ZM_E_FIT, "12.5 JPY: yen has no decimals; refused, never rounded");
    {
        zm_schema *s3 = myra("^ZXP3(id:i9)=ref:t16!^amount:n12.3!^ccy:t3!^from:i10!^to:i10!");
        zm_record *r3 = NULL;
        zm_posting_map vm = k_map;
        CHECK(s3 != NULL, "a schema with 3 decimals");
        CHECK(zm_node_decode(s3, "^ZXP3(1)", 8, "NG-1^1.005^NGN^1^2", 18, &r3) == ZXV_OK, "1.005");
        CHECK(zm_post(poster, r3, &k_map, &p) == ZM_E_FIT, "1.005 NGN has three decimals");
        zm_record_destroy(r3);
        r3 = NULL;
        /* a private unit: ISO 4217 does not know VFV, so the map says 3 */
        {
            char v[64];
            snprintf(v, sizeof v, "VFV-1^2.125^VFV^%u^%u", (unsigned) vpool, (unsigned) dan);
            zm_node_decode(s3, "^ZXP3(2)", 8, v, strlen(v), &r3);
        }
        CHECK(zm_post(poster, r3, &vm, &p) == ZXV_E_CURRENCY, "VFV without minor_units");
        vm.minor_units = 3;
        CHECK(zm_post(poster, r3, &vm, &p) == ZXV_OK && p.amount_minor == 2125, "VFV with minor_units 3");
        vm.minor_units = 19;
        CHECK(zm_post(poster, r3, &vm, &p) == ZXV_E_ARG, "minor_units 19");
        zm_record_destroy(r3);
        r3 = NULL;
        {
            char v[64];
            snprintf(v, sizeof v, "VFV-2^.001^XXX^%u^%u", (unsigned) vpool, (unsigned) dan);
            zm_node_decode(s3, "^ZXP3(3)", 8, v, strlen(v), &r3);
        }
        vm.minor_units = 3;
        vm.currency = NULL;
        vm.fixed_currency = "VFV";
        CHECK(zm_post(poster, r3, &vm, &p) == ZXV_OK && p.amount_minor == 1,
              "a fixed currency ignores the node's code");
        /* the node's own reference when the map names none */
        vm.reference = NULL;
        CHECK(zm_posting_read(r3, &vm, &p) == ZXV_OK && strcmp(p.reference, "ZXP3(3)") == 0,
              "reference from the node: %s", p.reference);
        zm_record_destroy(r3);
        zm_schema_destroy(s3);
    }
    /* 5. accounts must be whole numbers in uint32; references fit 35 bytes */
    {
        zm_schema *sa = myra("^ZXPA(\"K\",key:t40)=amt:n7.2^a:n4.1^b:i10^ccy:t3\n"
                             "^ZXPA(\"L\",acct:i10)=amt:i5^to:i10^ccy:t3^ref:t36\n");
        zm_record *r = NULL;
        zm_posting_map am = {"amt", "a", "b", NULL, "NGN", "key", -1};
        CHECK(sa != NULL, "schema");
        zm_node_decode(sa, "^ZXPA(\"K\",\"X\")", 14, "1^1.5^1", 7, &r);
        CHECK(zm_posting_read(r, &am, &p) == ZM_E_FIT, "account 1.5");
        zm_record_destroy(r);
        r = NULL;
        zm_node_decode(sa, "^ZXPA(\"K\",\"X\")", 14, "1^1^4294967296", 14, &r);
        CHECK(zm_posting_read(r, &am, &p) == ZM_E_FIT, "account 2^32");
        zm_record_destroy(r);
        r = NULL;
        zm_node_decode(sa, "^ZXPA(\"K\",\"X\")", 14, "1^-1^1", 6, &r);
        CHECK(zm_posting_read(r, &am, &p) == ZM_E_FIT, "account -1");
        zm_record_destroy(r);
        r = NULL;
        zm_node_decode(sa, "^ZXPA(\"K\",\"X\")", 14, "1^1^4294967295", 14, &r);
        CHECK(zm_posting_read(r, &am, &p) == ZXV_OK && p.from == 1 && p.to == UINT32_MAX &&
                  p.amount_minor == 100,
              "account 1 (from n4.1) and 2^32-1 are fine");
        zm_record_destroy(r);
        r = NULL;
        zm_node_decode(sa, "^ZXPA(\"K\",\"X\")", 14, "^1^2", 4, &r);
        CHECK(zm_posting_read(r, &am, &p) == ZXV_E_FIELD, "an empty amount");
        am.from = "missing";
        CHECK(zm_posting_read(r, &am, &p) == ZXV_E_NOT_FOUND, "unknown field");
        am.from = "key";
        CHECK(zm_posting_read(r, &am, &p) == ZXV_E_ARG, "a TEXT account");
        CHECK(zm_posting_read(r, NULL, &p) == ZXV_E_ARG, "NULL map");
        CHECK(zm_posting_read(r, &am, NULL) == ZXV_E_ARG, "NULL out");
        zm_record_destroy(r);
        r = NULL;
        /* a key as the account; a 36-byte reference */
        {
            zm_posting_map lm = {"amt", "acct", "to", "ccy", NULL, "ref", -1};
            {
                static const char v[] = "3^6^NGN^ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
                CHECK(zm_node_decode(sa, "^ZXPA(\"L\",5)", 12, v, strlen(v), &r) == ZXV_OK, "L node");
            }
            CHECK(zm_posting_read(r, &lm, &p) == ZM_E_FIT, "a 36-byte reference");
            lm.reference = "acct";
            CHECK(zm_posting_read(r, &lm, &p) == ZXV_OK && p.from == 5 && p.to == 6 &&
                      p.amount_minor == 300 && strcmp(p.reference, "5") == 0,
                  "a key is the account and the reference");
            zm_record_destroy(r);
            r = NULL;
        r = NULL;
        }
        zm_schema_destroy(sa);
    }

    /* 6. A = L + E holds, then a halted context refuses everything */
    CHECK(zxv_ledger_check_cap(reader) == ZXV_OK, "the ledger balances");
    CHECK(zxv_ctx_halt(ctx, "test: operator kill switch") == ZXV_OK, "halt");
    zxv_ctx_is_poisoned(ctx, &poisoned);
    CHECK(poisoned == 1, "poisoned");
    payment(14, "PAY-0010", "1", "NGN", alice, bob);
    CHECK(zm_post(poster, g_r, &k_map, &p) == ZXV_E_POISONED,
          "a halted context refuses the posting (FATAL, never retried)");
    CHECK(zxv_status_class(ZXV_E_POISONED) == ZXV_CLASS_FATAL, "class FATAL");

    zm_record_destroy(g_r);
    zxv_cap_destroy(reader);
    zxv_cap_destroy(poster);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
    zm_schema_destroy(g_s);
    return report("test_post");
}
