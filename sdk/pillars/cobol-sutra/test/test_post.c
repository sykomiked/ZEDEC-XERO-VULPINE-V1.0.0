/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_post.c — the posting bridge: payment records become libzxv ledger
 * postings through zxv_ledger_post_cap, and every refusal comes back as a
 * status: the capability, funds, duplicate references, amount range,
 * currency, decimals that do not fit, and a halted (poisoned) context. */
#include "zs_test.h"

static const zs_posting_map k_map = {"PAY-AMOUNT", "PAY-FROM", "PAY-TO", "PAY-CCY",
                                     NULL,         "PAY-REF",  -1};

static zs_layout *g_l;
static uint8_t g_rec[64];

/* Build a payment record. */
static void payment(const char *ref, const char *amount, const char *ccy, uint32_t from,
                    uint32_t to) {
    zs_record_init(g_l, g_rec, sizeof g_rec);
    zs_put_value(g_l, g_rec, sizeof g_rec, "PAY-REF", ref, strlen(ref));
    zs_put_value(g_l, g_rec, sizeof g_rec, "PAY-AMOUNT", amount, strlen(amount));
    zs_put_value(g_l, g_rec, sizeof g_rec, "PAY-CCY", ccy, strlen(ccy));
    zs_put_decimal(g_l, g_rec, sizeof g_rec, "PAY-FROM", from, 0);
    zs_put_decimal(g_l, g_rec, sizeof g_rec, "PAY-TO", to, 0);
}

static int64_t equity(const zxv_cap *cap, uint32_t acct) {
    int64_t e = -1;
    zxv_ledger_balance_cap(cap, acct, NULL, NULL, &e);
    return e;
}

int main(void) {
    zxv_ctx *ctx = NULL;
    zxv_cap *root = NULL, *poster = NULL, *reader = NULL;
    uint32_t bank = 0, alice = 0, bob = 0, jbank = 0, jcarol = 0, vbank = 0, vdan = 0;
    zs_posting p;
    int32_t st;
    int32_t poisoned = 0;

    g_l = cpy(k_payment_cpy);
    CHECK(g_l != NULL, "payment layout");
    if (!g_l) return report("test_post");

    CHECK(zxv_ctx_create(&ctx) == ZXV_OK, "context");
    CHECK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root) == ZXV_OK, "root cap");
    CHECK(zxv_ccy_enable_cap(root, "NGN") == ZXV_OK && zxv_ccy_enable_cap(root, "JPY") == ZXV_OK,
          "currencies");
    CHECK(zxv_ccy_register_private_cap(root, "VFV", 3) == ZXV_OK, "a private unit");
    zxv_ledger_open_account_cap(root, "bank", "NGN", ZXV_ACCT_ISSUER, &bank);
    zxv_ledger_open_account_cap(root, "alice", "NGN", ZXV_ACCT_HOLDER, &alice);
    zxv_ledger_open_account_cap(root, "bob", "NGN", ZXV_ACCT_HOLDER, &bob);
    zxv_ledger_open_account_cap(root, "jp-bank", "JPY", ZXV_ACCT_ISSUER, &jbank);
    zxv_ledger_open_account_cap(root, "carol", "JPY", ZXV_ACCT_HOLDER, &jcarol);
    zxv_ledger_open_account_cap(root, "vfv-pool", "VFV", ZXV_ACCT_ISSUER, &vbank);
    zxv_ledger_open_account_cap(root, "dan", "VFV", ZXV_ACCT_HOLDER, &vdan);
    /* the bridge gets a narrow cap: post and read, nothing else */
    CHECK(zxv_cap_attenuate(root, ZXV_CAP_LEDGER_POST | ZXV_CAP_LEDGER_READ, &poster) == ZXV_OK,
          "poster cap");
    CHECK(zxv_cap_attenuate(root, ZXV_CAP_LEDGER_READ, &reader) == ZXV_OK, "reader cap");

    /* 1. funding: the issuer pays alice 1000.00 NGN */
    payment("FUND-1", "1000.00", "NGN", bank, alice);
    st = zs_post(poster, g_l, &k_map, g_rec, sizeof g_rec, &p);
    CHECK(st == ZXV_OK && p.entry_id > 0, "funding posts: %s", zs_status_name(st));
    CHECK(p.amount_minor == 100000 && strcmp(p.currency, "NGN") == 0 &&
              strcmp(p.reference, "FUND-1") == 0 && p.from == bank && p.to == alice,
          "the posting read from the record");
    CHECK(equity(poster, alice) == 100000, "alice has 1000.00");

    /* 2. alice pays bob 250.25 */
    payment("PAY-0001", "250.25", "NGN", alice, bob);
    CHECK(zs_post(poster, g_l, &k_map, g_rec, sizeof g_rec, &p) == ZXV_OK, "payment posts");
    CHECK(equity(poster, alice) == 74975 && equity(poster, bob) == 25025, "balances move exactly");

    /* 3. refusals come back as statuses and change nothing */
    payment("PAY-0002", "5000.00", "NGN", alice, bob);
    CHECK(zs_post(poster, g_l, &k_map, g_rec, sizeof g_rec, &p) == ZXV_E_FUNDS, "insufficient funds");
    CHECK(strstr(last_err(), "PAY-0002") != NULL, "the message names the reference: %s", last_err());
    payment("PAY-0001", "1.00", "NGN", alice, bob);
    CHECK(zs_post(poster, g_l, &k_map, g_rec, sizeof g_rec, &p) == ZXV_E_DUPLICATE, "duplicate reference");
    payment("PAY-0003", "0.00", "NGN", alice, bob);
    CHECK(zs_post(poster, g_l, &k_map, g_rec, sizeof g_rec, &p) == ZXV_E_RANGE, "zero amount");
    payment("PAY-0004", "-5.00", "NGN", alice, bob);
    CHECK(zs_post(poster, g_l, &k_map, g_rec, sizeof g_rec, &p) == ZXV_E_RANGE, "negative amount");
    CHECK(p.amount_minor == -500, "the record was still read exactly (-500)");
    payment("PAY-0005", "1.00", "NGN", alice, 999);
    CHECK(zs_post(poster, g_l, &k_map, g_rec, sizeof g_rec, &p) == ZXV_E_NOT_FOUND, "unknown account");
    payment("PAY-0006", "1.00", "NGN", alice, alice);
    CHECK(zs_post(poster, g_l, &k_map, g_rec, sizeof g_rec, &p) == ZXV_E_ARG, "same account");
    payment("PAY-0007", "1.00", "USD", alice, bob);
    CHECK(zs_post(poster, g_l, &k_map, g_rec, sizeof g_rec, &p) == ZXV_E_CURRENCY,
          "USD into NGN accounts (no implicit FX)");
    payment("PAY-0008", "1.00", "ZZZ", alice, bob);
    CHECK(zs_post(poster, g_l, &k_map, g_rec, sizeof g_rec, &p) == ZXV_E_CURRENCY,
          "a code ISO 4217 does not know");
    payment("PAY-0009", "1.00", "NGN", alice, bob);
    CHECK(zs_post(reader, g_l, &k_map, g_rec, sizeof g_rec, &p) == ZXV_E_CAP, "a read-only cap");
    CHECK(zs_post(NULL, g_l, &k_map, g_rec, sizeof g_rec, &p) == ZXV_E_ARG, "no cap");
    CHECK(equity(poster, alice) == 74975 && equity(poster, bob) == 25025, "nothing moved");

    /* 4. exactness against the currency's minor units */
    {
        static const char t[] = "record pay3\n  pay-ref: x(16)\n  pay-amount: s9(9)v999 comp-3\n"
                                "  pay-ccy: x(3)\n  pay-from: 9(9) comp\n  pay-to: 9(9) comp\n";
        zs_layout *l3 = sutra(t), *keep = g_l;
        CHECK(l3 != NULL, "a layout with 3 decimals");
        g_l = l3;
        payment("JP-1", "12.000", "JPY", jbank, jcarol);
        CHECK(zs_post(poster, l3, &k_map, g_rec, sizeof g_rec, &p) == ZXV_OK && p.amount_minor == 12,
              "12.000 JPY is 12 yen");
        payment("JP-2", "12.500", "JPY", jbank, jcarol);
        CHECK(zs_post(poster, l3, &k_map, g_rec, sizeof g_rec, &p) == ZS_E_FIT,
              "12.500 JPY: yen has no decimals; refused, never rounded");
        payment("NG-1", "1.005", "NGN", alice, bob);
        CHECK(zs_post(poster, l3, &k_map, g_rec, sizeof g_rec, &p) == ZS_E_FIT,
              "1.005 NGN has three decimals");
        /* a private unit: ISO 4217 does not know VFV, so the map says 3 */
        {
            zs_posting_map vm = k_map;
            payment("VFV-1", "2.125", "VFV", vbank, vdan);
            CHECK(zs_post(poster, l3, &vm, g_rec, sizeof g_rec, &p) == ZXV_E_CURRENCY,
                  "VFV without minor_units");
            vm.minor_units = 3;
            CHECK(zs_post(poster, l3, &vm, g_rec, sizeof g_rec, &p) == ZXV_OK && p.amount_minor == 2125,
                  "VFV with minor_units 3 is 2125");
            vm.minor_units = 19;
            CHECK(zs_post(poster, l3, &vm, g_rec, sizeof g_rec, &p) == ZXV_E_ARG, "minor_units 19");
            vm.minor_units = 3;
            vm.currency = NULL;
            vm.fixed_currency = "VFV";
            payment("VFV-2", "0.001", "XXX", vbank, vdan);
            CHECK(zs_post(poster, l3, &vm, g_rec, sizeof g_rec, &p) == ZXV_OK && p.amount_minor == 1,
                  "a fixed currency ignores the record's code");
        }
        g_l = keep;
        zs_layout_destroy(l3);
    }
    /* 5. accounts must be whole numbers in uint32 */
    {
        zs_layout *la = sutra("record p\n  ref: x(8)\n  amt: 9(5)v99\n  a: 9(3)v9\n  b: 9(10)\n");
        zs_posting_map am = {"amt", "a", "b", NULL, "NGN", "ref", -1};
        uint8_t r[32];
        zs_record_init(la, r, sizeof r);
        zs_put_value(la, r, sizeof r, "ref", "X", 1);
        zs_put_value(la, r, sizeof r, "amt", "1", 1);
        zs_put_value(la, r, sizeof r, "a", "1.5", 3);
        CHECK(zs_posting_read(la, &am, r, sizeof r, &p) == ZS_E_FIT, "account 1.5");
        zs_put_value(la, r, sizeof r, "a", "1.0", 3);
        zs_put_value(la, r, sizeof r, "b", "4294967296", 10);
        CHECK(zs_posting_read(la, &am, r, sizeof r, &p) == ZS_E_FIT, "account 2^32");
        zs_put_value(la, r, sizeof r, "b", "4294967295", 10);
        CHECK(zs_posting_read(la, &am, r, sizeof r, &p) == ZXV_OK && p.from == 1 &&
                  p.to == UINT32_MAX && p.amount_minor == 100,
              "account 1.0 and 2^32-1 are fine");
        am.reference = "amt";
        CHECK(zs_posting_read(la, &am, r, sizeof r, &p) == ZXV_E_ARG, "reference must be PIC X");
        am.reference = "missing";
        CHECK(zs_posting_read(la, &am, r, sizeof r, &p) == ZXV_E_NOT_FOUND, "unknown field");
        CHECK(zs_posting_read(la, NULL, r, sizeof r, &p) == ZXV_E_ARG, "NULL map");
        zs_layout_destroy(la);
    }

    /* 6. A = L + E holds, then a halted context refuses everything */
    CHECK(zxv_ledger_check_cap(reader) == ZXV_OK, "the ledger balances");
    CHECK(zxv_ctx_halt(ctx, "test: operator kill switch") == ZXV_OK, "halt");
    zxv_ctx_is_poisoned(ctx, &poisoned);
    CHECK(poisoned == 1, "poisoned");
    payment("PAY-0010", "1.00", "NGN", alice, bob);
    CHECK(zs_post(poster, g_l, &k_map, g_rec, sizeof g_rec, &p) == ZXV_E_POISONED,
          "a halted context refuses the posting (FATAL, never retried)");
    CHECK(zxv_status_class(ZXV_E_POISONED) == ZXV_CLASS_FATAL, "class FATAL");

    zxv_cap_destroy(reader);
    zxv_cap_destroy(poster);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
    zs_layout_destroy(g_l);
    return report("test_post");
}
