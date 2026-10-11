/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxe_ledger.c — the fixed-memory ledger: unit tests, then a
 * differential run against libzxv's own ledger (the same random calls on
 * both; every status and every balance must agree). */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>

#include "zxe.h"
#include "zxe_test.h"
#include "zxp.h"
#include "zxp_posix.h"

#define OK(call) IS((call), ZXV_OK)

static zxe_account g_acc[8];
static zxe_refkey g_refs[16];

static int g_sink_calls, g_sink_fail;
static uint8_t g_sink_head[32];
static int sink(void *user, uint64_t id, uint32_t from, uint32_t to, int64_t amount,
                const char *ccy, const char *ref, const uint8_t head[32])
{
    (void) user;
    (void) id;
    (void) from;
    (void) to;
    (void) amount;
    (void) ccy;
    (void) ref;
    g_sink_calls++;
    memcpy(g_sink_head, head, 32);
    return g_sink_fail;
}

static void test_unit(void)
{
    zxv_ctx *ctx;
    zxv_cap *root, *ro, *nopost;
    zxe_ledger L;
    uint32_t iss, al, bo, x;
    uint64_t id;
    int64_t d, c, e;
    uint8_t h0[32], h1[32], zero[32] = {0};
    OK(zxv_ctx_create(&ctx));
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root));
    OK(zxv_cap_attenuate(root, ZXV_CAP_LEDGER_READ, &ro));
    OK(zxv_cap_attenuate(root, ZXV_CAP_BUDGET, &nopost));
    IS(zxe_ledger_init(&L, ctx, g_acc, 8, g_refs, 12), ZXV_E_ARG); /* not a power of two */
    IS(zxe_ledger_init(NULL, ctx, g_acc, 8, g_refs, 16), ZXV_E_ARG);
    OK(zxe_ledger_init(&L, ctx, g_acc, 8, g_refs, 16));

    IS(zxe_ledger_ccy_enable(&L, root, "eur", 2), ZXV_E_CURRENCY);
    IS(zxe_ledger_ccy_enable(&L, root, "EUR", 5), ZXV_E_ARG);
    IS(zxe_ledger_ccy_enable(&L, ro, "EUR", 2), ZXV_E_CAP);
    OK(zxe_ledger_ccy_enable(&L, root, "EUR", 2));
    OK(zxe_ledger_ccy_enable(&L, root, "EUR", 2)); /* idempotent */
    OK(zxe_ledger_ccy_enable(&L, root, "USD", 2));

    IS(zxe_ledger_open_account(&L, root, "x", "GBP", ZXV_ACCT_HOLDER, &x), ZXV_E_CURRENCY);
    IS(zxe_ledger_open_account(&L, root, "", "EUR", ZXV_ACCT_HOLDER, &x), ZXV_E_ARG);
    IS(zxe_ledger_open_account(&L, root, "\xC0\x80", "EUR", ZXV_ACCT_HOLDER, &x), ZXV_E_UTF8);
    IS(zxe_ledger_open_account(&L, root, "x", "EUR", 7, &x), ZXV_E_ARG);
    OK(zxe_ledger_open_account(&L, root, "issuer", "EUR", ZXV_ACCT_ISSUER, &iss));
    OK(zxe_ledger_open_account(&L, root, "alice", "EUR", ZXV_ACCT_HOLDER, &al));
    OK(zxe_ledger_open_account(&L, root, "bob", "EUR", ZXV_ACCT_HOLDER, &bo));
    OK(zxe_ledger_open_account(&L, root, "carol", "USD", ZXV_ACCT_HOLDER, &x));
    IS(zxe_ledger_open_account(&L, ro, "dave", "EUR", ZXV_ACCT_HOLDER, &x), ZXV_E_CAP);

    OK(zxe_ledger_head(&L, ro, h0));
    IS(memcmp(h0, zero, 32), 0);
    OK(zxe_ledger_set_sink(&L, root, sink, NULL));
    OK(zxe_ledger_post(&L, root, iss, al, 10000, "EUR", "mint-1", &id));
    IS(id, 1);
    IS(g_sink_calls, 1);
    OK(zxe_ledger_head(&L, ro, h1));
    IS(memcmp(h1, g_sink_head, 32), 0);
    CHECK(memcmp(h1, zero, 32) != 0, "head moved");
    OK(zxe_ledger_post(&L, root, al, bo, 2500, "EUR", "pay-1", &id));
    IS(id, 2);
    IS(zxe_ledger_post(&L, root, al, bo, 7501, "EUR", "pay-2", &id), ZXV_E_FUNDS);
    IS(zxe_ledger_post(&L, root, al, bo, 1, "EUR", "pay-1", &id), ZXV_E_DUPLICATE);
    IS(zxe_ledger_post(&L, root, al, x, 1, "EUR", "pay-3", &id), ZXV_E_CURRENCY);
    IS(zxe_ledger_post(&L, root, al, al, 1, "EUR", "pay-3", &id), ZXV_E_ARG);
    IS(zxe_ledger_post(&L, root, al, 99, 1, "EUR", "pay-3", &id), ZXV_E_NOT_FOUND);
    IS(zxe_ledger_post(&L, root, al, bo, 0, "EUR", "pay-3", &id), ZXV_E_RANGE);
    IS(zxe_ledger_post(&L, root, al, bo, ZXE_AMOUNT_LIMIT + 1, "EUR", "pay-3", &id), ZXV_E_RANGE);
    IS(zxe_ledger_post(&L, root, al, bo, 1, "EUR", "123456789012345678901234567890123456", &id),
       ZXV_E_TOO_LONG);
    IS(zxe_ledger_post(&L, ro, al, bo, 1, "EUR", "pay-3", &id), ZXV_E_CAP);
    IS(zxe_ledger_post(&L, nopost, al, bo, 1, "EUR", "pay-3", &id), ZXV_E_CAP);
    /* write-ahead: a failing journal sink refuses the posting */
    g_sink_fail = 1;
    IS(zxe_ledger_post(&L, root, al, bo, 1, "EUR", "pay-3", &id), ZXV_E_IO);
    g_sink_fail = 0;
    OK(zxe_ledger_head(&L, ro, h0));
    IS(memcmp(h0, h1, 32) == 0, 0); /* head is the second posting's */
    OK(zxe_ledger_post(&L, root, al, bo, 1, "EUR", "pay-3", &id)); /* not a duplicate */
    IS(id, 3);
    OK(zxe_ledger_balance(&L, ro, al, &d, &c, &e));
    CHECK(d == 10000 && c == 2501 && e == 7499, "alice %lld %lld %lld", (long long) d,
          (long long) c, (long long) e);
    OK(zxe_ledger_balance(&L, ro, iss, NULL, NULL, &e));
    IS(e, -10000); /* issuers may carry a credit balance */
    OK(zxe_ledger_check(&L, ro));

    /* reference capacity: 16 slots hold 8 references */
    for (int i = 0; i < 5; i++) {
        char ref[24];
        snprintf(ref, sizeof ref, "fill-%d", i);
        OK(zxe_ledger_post(&L, root, iss, al, 1, "EUR", ref, &id));
    }
    IS(zxe_ledger_post(&L, root, iss, al, 1, "EUR", "fill-x", &id), ZXV_E_CAPACITY);
    OK(zxe_ledger_reset_refs(&L, root));
    OK(zxe_ledger_post(&L, root, iss, al, 1, "EUR", "fill-x", &id));

    /* a broken invariant halts the ledger and the context */
    zxe_ledger_test_corrupt(&L, bo, 5);
    IS(zxe_ledger_check(&L, ro), ZXV_E_INVARIANT);
    int32_t p = 0;
    OK(zxv_ctx_is_poisoned(ctx, &p));
    IS(p, 1);
    IS(zxe_ledger_post(&L, root, iss, al, 1, "EUR", "after", &id), ZXV_E_POISONED);
    IS(zxe_ledger_balance(&L, ro, al, &d, &c, &e), ZXV_E_POISONED);
    zxe_ledger_destroy(&L);
    IS(zxe_ledger_post(&L, root, iss, al, 1, "EUR", "after", &id), ZXV_E_ARG);
    zxv_cap_destroy(nopost);
    zxv_cap_destroy(ro);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
}

/* ---- differential: zxe_ledger vs libzxv's ledger ---- */
#define NACC 6
static zxe_account d_acc[NACC];
static zxe_refkey d_refs[1u << 15];

static uint64_t rng = 88172645463325252ull;
static uint64_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

static long g_ops, g_agree_fail, g_posted, g_refused;
static void diff_run(uint64_t seed, int ops)
{
    rng = seed;
    zxv_ctx *cz, *ce;
    zxv_cap *rz, *re, *rz_ro, *re_ro;
    zxe_ledger L;
    OK(zxv_ctx_create(&cz));
    OK(zxv_ctx_create(&ce));
    OK(zxv_cap_create(cz, 1, NULL, ZXV_CAP_ALL, &rz));
    OK(zxv_cap_create(ce, 1, NULL, ZXV_CAP_ALL, &re));
    OK(zxv_cap_attenuate(rz, ZXV_CAP_LEDGER_READ, &rz_ro));
    OK(zxv_cap_attenuate(re, ZXV_CAP_LEDGER_READ, &re_ro));
    OK(zxe_ledger_init(&L, ce, d_acc, NACC, d_refs, (uint32_t) (sizeof d_refs / sizeof d_refs[0])));
    static const char *const ccys[] = {"EUR", "USD", "JPY", "eur", "EURO", ""};
    static const char *const names[] = {"a", "b", "c", "", "\xE2\x82\xAC", "\xED\xA0\x80"};
    uint32_t n_open = 0;
    for (int i = 0; i < ops; i++) {
        int32_t rz_rc, re_rc;
        uint32_t k = (uint32_t) (next() % 100);
        if (k < 4) {
            const char *cc = ccys[next() % 6];
            uint8_t minor = (uint8_t) (next() % 6);
            rz_rc = zxv_ccy_enable_with_minor_cap(rz, cc, minor);
            re_rc = zxe_ledger_ccy_enable(&L, re, cc, minor);
        } else if (k < 10) {
            uint32_t ia = 0, ib = 0;
            const char *nm = names[next() % 6];
            const char *cc = ccys[next() % 4];
            int32_t kind = (int32_t) (next() % 3);
            if (n_open >= NACC) {
                re_rc = zxe_ledger_open_account(&L, re, nm, cc, kind, &ib);
                IS(re_rc == ZXV_OK, 0);
                continue; /* fixed memory: libzxv would grow */
            }
            rz_rc = zxv_ledger_open_account_cap(rz, nm, cc, kind, &ia);
            re_rc = zxe_ledger_open_account(&L, re, nm, cc, kind, &ib);
            if (rz_rc == ZXV_OK && re_rc == ZXV_OK) {
                IS(ia, ib);
                n_open++;
            }
        } else if (k < 95) {
            uint32_t from = (uint32_t) (next() % (NACC + 1)), to = (uint32_t) (next() % (NACC + 1));
            int64_t amt;
            uint32_t m = (uint32_t) (next() % 10);
            if (m == 0) amt = -(int64_t) (next() % 5);
            else if (m == 1) amt = ZXE_AMOUNT_LIMIT - (int64_t) (next() % 3) + 1;
            else if (m == 2) amt = (int64_t) (next() % (uint64_t) ZXE_AMOUNT_LIMIT);
            else amt = (int64_t) (next() % 100000);
            char ref[48];
            uint32_t rk = (uint32_t) (next() % 50);
            if (rk == 0) snprintf(ref, sizeof ref, "%s", "");
            else if (rk == 1) snprintf(ref, sizeof ref, "%s", "0123456789012345678901234567890123456");
            else snprintf(ref, sizeof ref, "r%llu", (unsigned long long) (next() % 12000));
            const char *cc = ccys[next() % 4];
            uint64_t idz = 0, ide = 0;
            rz_rc = zxv_ledger_post_cap(rz, from, to, amt, cc, ref, &idz);
            re_rc = zxe_ledger_post(&L, re, from, to, amt, cc, ref, &ide);
            if (rz_rc == ZXV_OK && re_rc == ZXV_OK) IS(idz, ide);
            if (rz_rc == ZXV_OK) g_posted++;
            else g_refused++;
        } else {
            uint32_t acct = (uint32_t) (next() % (NACC + 1));
            int64_t d1 = 0, c1 = 0, e1 = 0, d2 = 0, c2 = 0, e2 = 0;
            rz_rc = zxv_ledger_balance_cap(rz_ro, acct, &d1, &c1, &e1);
            re_rc = zxe_ledger_balance(&L, re_ro, acct, &d2, &c2, &e2);
            if (rz_rc == ZXV_OK && re_rc == ZXV_OK)
                CHECK(d1 == d2 && c1 == c2 && e1 == e2, "balance of %u differs", acct);
        }
        g_ops++;
        if (rz_rc != re_rc) {
            g_agree_fail++;
            CHECK(0, "op %d (kind %u): libzxv %s, zxe %s", i, k, zxv_status_name(rz_rc),
                  zxv_status_name(re_rc));
        }
    }
    OK(zxv_ledger_check_cap(rz_ro));
    OK(zxe_ledger_check(&L, re_ro));
    for (uint32_t a = 0; a < n_open; a++) {
        int64_t d1, c1, e1, d2, c2, e2;
        OK(zxv_ledger_balance_cap(rz_ro, a, &d1, &c1, &e1));
        OK(zxe_ledger_balance(&L, re_ro, a, &d2, &c2, &e2));
        CHECK(d1 == d2 && c1 == c2 && e1 == e2, "final balance of %u differs", a);
    }
    zxe_ledger_destroy(&L);
    zxv_cap_destroy(rz_ro);
    zxv_cap_destroy(re_ro);
    zxv_cap_destroy(rz);
    zxv_cap_destroy(re);
    zxv_ctx_destroy(cz);
    zxv_ctx_destroy(ce);
}

int main(int argc, char **argv)
{
    IS(zxp_set_entropy(zxp_posix_entropy, NULL), ZXP_OK);
    IS(zxp_init(), ZXP_OK);
    test_unit();
    int ops = argc > 1 ? atoi(argv[1]) : 20000;
    for (uint64_t s = 1; s <= 4; s++) diff_run(s * 0x9E3779B97F4A7C15ull, ops);
    printf("differential: %ld operations (%ld postings accepted, %ld refused), %ld disagreements\n",
           g_ops, g_posted, g_refused, g_agree_fail);
    CHECK(g_posted > 1000 && g_refused > 1000, "both paths exercised");
    return finish("test_zxe_ledger");
}
