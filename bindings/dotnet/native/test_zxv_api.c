/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_api.c — conformance test of the zxv_api.h C ABI, linked against
 * the built shared library exactly as a binding would use it. Asserts the
 * numbers and the status codes, not merely that calls returned. */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "zxv_api.h"

static int g_n, g_fail;
#define CHECK(c, m)                                                                       \
    do {                                                                                  \
        g_n++;                                                                            \
        if (!(c)) {                                                                       \
            g_fail++;                                                                     \
            char e_[256];                                                                 \
            size_t l_;                                                                    \
            zxv_last_error(e_, sizeof e_, &l_);                                           \
            printf("FAIL [%d] %s:%d: %s (last error: %s)\n", g_n, __FILE__, __LINE__, m, e_); \
        }                                                                                 \
    } while (0)

static zxv_ctx *g_ctx;
static uint32_t g_issuer, g_pool[4];

static void *worker(void *arg)
{
    int id = (int) (size_t) arg;
    for (int i = 0; i < 20; i++) {
        char ref[36];
        snprintf(ref, sizeof ref, "T%d-%d", id, i);
        zxv_ledger_post(g_ctx, g_issuer, g_pool[id], 100, "NGN", ref, NULL);
    }
    return NULL;
}

int main(void)
{
    uint32_t maj = 0, min = 0, pat = 0, feat = 0;
    CHECK(zxv_version(&maj, &min, &pat) == ZXV_OK && maj == 1, "version 1.x");
    CHECK(zxv_features(&feat) == ZXV_OK, "features");
    CHECK(feat & ZXV_FEAT_LEDGER, "ledger feature live");
    CHECK(feat & ZXV_FEAT_MX_PACS008, "pacs.008 feature live");
    CHECK(strcmp(zxv_status_name(ZXV_E_FUNDS), "ZXV_E_FUNDS") == 0, "status name");
    CHECK(strcmp(zxv_status_name(-12345), "ZXV_E_UNKNOWN") == 0, "unknown status name");
    bool has_iso = (feat & ZXV_FEAT_ISO4217) != 0;
    printf("features=0x%08x iso4217=%d\n", feat, has_iso);

    /* Rails */
    uint16_t r = 0;
    CHECK(zxv_rail_numeric(ZXV_RAIL_DEBIT, &r) == ZXV_OK && r == 555, "DEBIT 555");
    CHECK(zxv_rail_numeric(ZXV_RAIL_CREDIT, &r) == ZXV_OK && r == 777, "CREDIT 777");
    CHECK(zxv_rail_numeric(ZXV_RAIL_EQUITY, &r) == ZXV_OK && r == 888, "EQUITY 888");
    char buf[512];
    size_t len = 0;
    CHECK(zxv_ccy_caveat(777, NULL, 0, &len) == ZXV_E_BUFFER && len > 0, "caveat size query");
    CHECK(zxv_ccy_caveat(777, buf, len + 1, &len) == ZXV_OK && strstr(buf, "NRE"), "777 caveat");
    CHECK(zxv_ccy_caveat(777, buf, len, &len) == ZXV_E_BUFFER, "exact-length buffer lacks NUL room");
    CHECK(zxv_ccy_caveat(555, buf, sizeof buf, &len) == ZXV_OK && strstr(buf, "NCR"), "555 caveat");
    CHECK(zxv_ccy_caveat(888, buf, sizeof buf, &len) == ZXV_OK && strstr(buf, "PNS"), "888 caveat");

    /* ISO 4217 */
    if (has_iso) {
        uint16_t num = 0;
        uint8_t mu = 0;
        uint32_t fl = 0;
        CHECK(zxv_iso4217_by_alpha("NGN", &num, &mu, &fl) == ZXV_OK && num == 566 && mu == 2 &&
                  (fl & ZXV_CCYF_PAYABLE),
              "NGN 566/2");
        CHECK(zxv_iso4217_by_alpha("XOF", &num, &mu, NULL) == ZXV_OK && num == 952 && mu == 0, "XOF 952/0");
        CHECK(zxv_iso4217_by_alpha("BHD", NULL, &mu, NULL) == ZXV_OK && mu == 3, "BHD 3 minor");
        CHECK(zxv_iso4217_by_alpha("VFV", NULL, NULL, NULL) == ZXV_E_NOT_FOUND, "VFV not ISO");
        CHECK(zxv_iso4217_by_numeric(936, buf, sizeof buf, &len, &mu, NULL) == ZXV_OK &&
                  strcmp(buf, "GHS") == 0,
              "936 -> GHS");
        CHECK(zxv_iso4217_by_numeric(555, buf, sizeof buf, &len, NULL, NULL) == ZXV_E_NOT_FOUND, "555 not ISO");
        CHECK(zxv_iso4217_by_numeric(777, buf, sizeof buf, &len, NULL, NULL) == ZXV_E_NOT_FOUND, "777 not ISO");
        CHECK(zxv_iso4217_by_numeric(888, buf, sizeof buf, &len, NULL, NULL) == ZXV_E_NOT_FOUND, "888 not ISO");
        CHECK(zxv_iso4217_name("KES", buf, sizeof buf, &len) == ZXV_OK && strcmp(buf, "Kenyan Shilling") == 0, "KES name");
        uint32_t cnt = 0;
        CHECK(zxv_iso4217_count(&cnt) == ZXV_OK && cnt > 150, "table count");
        int32_t au = 0;
        CHECK(zxv_au_is_member("GH", &au) == ZXV_OK && au == 1, "GH in AU");
        CHECK(zxv_au_is_member("FR", &au) == ZXV_OK && au == 0, "FR not in AU");
    } else {
        CHECK(zxv_iso4217_by_alpha("NGN", NULL, NULL, NULL) == ZXV_E_NOTIMPL, "no table -> NOTIMPL");
    }

    /* Context + currency config */
    CHECK(zxv_ctx_create(&g_ctx) == ZXV_OK, "ctx create");
    if (has_iso) {
        CHECK(zxv_ccy_enable(g_ctx, "NGN") == ZXV_OK, "enable NGN");
        CHECK(zxv_ccy_enable(g_ctx, "GHS") == ZXV_OK, "enable GHS");
        CHECK(zxv_ccy_enable(g_ctx, "XXX") == ZXV_E_CURRENCY, "XXX not payable");
        CHECK(zxv_ccy_enable_with_minor(g_ctx, "NGN", 3) == ZXV_E_CURRENCY, "minor mismatch refused");
    } else {
        CHECK(zxv_ccy_enable_with_minor(g_ctx, "NGN", 2) == ZXV_OK, "enable NGN/2");
        CHECK(zxv_ccy_enable_with_minor(g_ctx, "GHS", 2) == ZXV_OK, "enable GHS/2");
    }
    CHECK(zxv_ccy_enable(g_ctx, "ngn") == ZXV_E_CURRENCY, "lower-case refused");
    CHECK(zxv_ccy_register_private(g_ctx, "VFV", 2) == ZXV_OK, "register VFV");
    uint8_t mu = 0;
    uint32_t fl = 0;
    CHECK(zxv_ccy_get(g_ctx, "VFV", &mu, &fl) == ZXV_OK && (fl & ZXV_CCYF_PRIVATE) && mu == 2, "VFV private");
    uint32_t ne = 0;
    CHECK(zxv_ccy_enabled_count(g_ctx, &ne) == ZXV_OK && ne == 3, "3 enabled");

    /* Ledger */
    uint32_t a = 0, b = 0, g = 0;
    CHECK(zxv_ledger_open_account(g_ctx, "PAPSS settlement NGN", "NGN", ZXV_ACCT_ISSUER, &g_issuer) == ZXV_OK, "issuer");
    CHECK(zxv_ledger_open_account(g_ctx, "Ada Obi", "NGN", ZXV_ACCT_HOLDER, &a) == ZXV_OK, "holder a");
    CHECK(zxv_ledger_open_account(g_ctx, "Kwame Mensah \xc3\xa9", "NGN", ZXV_ACCT_HOLDER, &b) == ZXV_OK, "holder b (UTF-8)");
    CHECK(zxv_ledger_open_account(g_ctx, "Ghana", "GHS", ZXV_ACCT_HOLDER, &g) == ZXV_OK, "holder GHS");
    CHECK(zxv_ledger_open_account(g_ctx, "bad \xc0\xaf", "NGN", ZXV_ACCT_HOLDER, &a) == ZXV_E_UTF8, "overlong UTF-8 refused");
    CHECK(zxv_ledger_open_account(g_ctx, "x", "ZZZ", ZXV_ACCT_HOLDER, &a) == ZXV_E_CURRENCY, "ccy not enabled");

    uint64_t eid = 0;
    CHECK(zxv_ledger_post(g_ctx, a, b, 100, "NGN", "E2E-0", &eid) == ZXV_E_FUNDS, "holder cannot overdraw");
    CHECK(zxv_ledger_post(g_ctx, g_issuer, a, 1500075, "NGN", "FUND-1", &eid) == ZXV_OK && eid > 0, "fund a");
    CHECK(zxv_ledger_post(g_ctx, a, b, 250050, "NGN", "E2E-1", &eid) == ZXV_OK, "a -> b");
    CHECK(zxv_ledger_post(g_ctx, a, b, 1, "NGN", "E2E-1", &eid) == ZXV_E_DUPLICATE, "duplicate reference");
    CHECK(zxv_ledger_post(g_ctx, a, g, 1, "NGN", "E2E-2", &eid) == ZXV_E_CURRENCY, "no implicit FX");
    CHECK(zxv_ledger_post(g_ctx, a, b, 0, "NGN", "E2E-3", &eid) == ZXV_E_RANGE, "zero refused");
    CHECK(zxv_ledger_post(g_ctx, a, b, ZXV_AMOUNT_MAX + 1, "NGN", "E2E-4", &eid) == ZXV_E_RANGE, "> 2^53 refused");

    int64_t d = 0, c = 0, e = 0;
    CHECK(zxv_ledger_balance(g_ctx, a, &d, &c, &e) == ZXV_OK && d == 1500075 && c == 250050 && e == 1250025, "a rails");
    CHECK(zxv_ledger_balance(g_ctx, b, &d, &c, &e) == ZXV_OK && d == 250050 && c == 0 && e == 250050, "b rails");
    CHECK(zxv_ledger_balance(g_ctx, g_issuer, &d, &c, &e) == ZXV_OK && e == -1500075, "issuer carries credit");
    uint32_t n = 0;
    CHECK(zxv_ledger_entry_count(g_ctx, a, &n) == ZXV_OK && n == 2, "a has 2 lines");
    int64_t amt = 0;
    uint32_t cp = 0;
    CHECK(zxv_ledger_entry_at(g_ctx, a, 1, &eid, &amt, &cp, buf, sizeof buf, &len) == ZXV_OK && amt == -250050 &&
              cp == b && strcmp(buf, "E2E-1") == 0,
          "a line 1");
    CHECK(zxv_ledger_check(g_ctx) == ZXV_OK, "ledger invariants");

    /* Concurrency: 4 threads x 20 postings on one context. */
    pthread_t th[4];
    for (int i = 0; i < 4; i++) zxv_ledger_open_account(g_ctx, "pool", "NGN", ZXV_ACCT_HOLDER, &g_pool[i]);
    for (int i = 0; i < 4; i++) pthread_create(&th[i], NULL, worker, (void *) (size_t) i);
    for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
    int ok_all = 1;
    for (int i = 0; i < 4; i++) {
        zxv_ledger_balance(g_ctx, g_pool[i], NULL, NULL, &e);
        ok_all &= e == 2000;
    }
    CHECK(ok_all, "concurrent postings exact");

    /* Volume: far beyond one kernel triple-ledger segment (256 entries) and
     * one book (512 accounts) -- the adapter segments transparently. */
    uint32_t hot = 0, many[600];
    zxv_ledger_open_account(g_ctx, "hot issuer", "NGN", ZXV_ACCT_ISSUER, &hot);
    int vol_ok = 1;
    for (int i = 0; i < 600; i++) {
        char ref[36];
        snprintf(ref, sizeof ref, "VOL-%d", i);
        vol_ok &= zxv_ledger_open_account(g_ctx, "payee", "NGN", ZXV_ACCT_HOLDER, &many[i]) == ZXV_OK;
        vol_ok &= zxv_ledger_post(g_ctx, hot, many[i], 7, "NGN", ref, NULL) == ZXV_OK;
    }
    for (int i = 0; i < 400; i++) {
        char ref[36];
        snprintf(ref, sizeof ref, "VOL2-%d", i);
        vol_ok &= zxv_ledger_post(g_ctx, hot, many[0], 1, "NGN", ref, NULL) == ZXV_OK;
    }
    CHECK(vol_ok, "1000 postings on one account, 600 accounts");
    CHECK(zxv_ledger_balance(g_ctx, hot, NULL, NULL, &e) == ZXV_OK && e == -(600 * 7 + 400), "hot issuer exact");
    CHECK(zxv_ledger_entry_count(g_ctx, hot, &n) == ZXV_OK && n == 1000, "hot journal complete");
    CHECK(zxv_ledger_check(g_ctx) == ZXV_OK, "invariants across segments and books");
    CHECK(zxv_ledger_check(g_ctx) == ZXV_OK, "invariants after concurrency");

    /* pacs.008 */
    zxv_msg *m = NULL;
    CHECK(zxv_msg_create(g_ctx, ZXV_MX_PACS008, &m) == ZXV_OK, "pacs008 create");
    zxv_msg_set_text(m, ZXV_FLD_MSG_ID, "MSG-0001");
    zxv_msg_set_text(m, ZXV_FLD_CRE_DT_TM, "2026-10-09T12:00:00Z");
    zxv_msg_set_text(m, ZXV_FLD_DEBTOR_NAME, "Ada & Sons <Lagos>");
    zxv_msg_set_text(m, ZXV_FLD_DEBTOR_ACCT, "NG0001");
    zxv_msg_set_text(m, ZXV_FLD_CREDITOR_NAME, "Kwame Mensah");
    CHECK(zxv_msg_render(m, NULL, 0, &len) == ZXV_E_FIELD, "missing fields detected");
    zxv_msg_set_text(m, ZXV_FLD_CREDITOR_ACCT, "GH0002");
    zxv_msg_set_text(m, ZXV_FLD_END_TO_END_ID, "E2E-1");
    CHECK(zxv_msg_set_text(m, ZXV_FLD_CCY, "VFV") == ZXV_E_CURRENCY, "VFV refused on the wire");
    CHECK(zxv_msg_set_text(m, ZXV_FLD_ACCT_ID, "x") == ZXV_E_FIELD, "camt field on pacs refused");
    CHECK(zxv_msg_set_text(m, ZXV_FLD_CCY, "NGN") == ZXV_OK, "NGN on the wire");
    CHECK(zxv_msg_set_amount(m, ZXV_AMT_SETTLEMENT, 250050, 2) == ZXV_OK, "amount");
    len = 0;
    CHECK(zxv_msg_render(m, NULL, 0, &len) == ZXV_E_BUFFER && len > 200, "render size query");
    char xml[8192];
    CHECK(zxv_msg_render(m, xml, sizeof xml, &len) == ZXV_OK && strlen(xml) == len, "render");
    CHECK(strstr(xml, "pacs.008.001.09") != NULL, "namespace");
    CHECK(strstr(xml, "Ccy=\"NGN\">2500.50<") != NULL, "exact amount NGN 2500.50");
    CHECK(strstr(xml, "Ada &amp; Sons &lt;Lagos&gt;") != NULL, "XML escaped");
    CHECK(strstr(xml, "555") == NULL && strstr(xml, "777") == NULL, "no rail numerics on the wire");
    zxv_msg_set_amount(m, ZXV_AMT_SETTLEMENT, 250050, 3);
    CHECK(zxv_msg_render(m, xml, sizeof xml, &len) == ZXV_E_CURRENCY, "minor-unit mismatch refused");
    zxv_msg_destroy(m);

    /* camt.053 */
    CHECK(zxv_msg_create(g_ctx, ZXV_MX_CAMT053, &m) == ZXV_OK, "camt053 create");
    zxv_msg_set_text(m, ZXV_FLD_MSG_ID, "STMT-1");
    zxv_msg_set_text(m, ZXV_FLD_CRE_DT_TM, "2026-10-09T23:59:59Z");
    zxv_msg_set_text(m, ZXV_FLD_ACCT_ID, "NG0001");
    zxv_msg_set_text(m, ZXV_FLD_CCY, "NGN");
    zxv_msg_set_amount(m, ZXV_AMT_OPENING, 0, 2);
    zxv_msg_add_entry(m, 1500075, 2, ZXV_CRDT, ZXV_STS_BOOK);
    zxv_msg_add_entry(m, 250050, 2, ZXV_DBIT, ZXV_STS_BOOK);
    zxv_msg_set_amount(m, ZXV_AMT_CLOSING, 1250000, 2);
    CHECK(zxv_msg_render(m, xml, sizeof xml, &len) == ZXV_E_INVARIANT, "closing must reconcile");
    zxv_msg_set_amount(m, ZXV_AMT_CLOSING, 1250025, 2);
    CHECK(zxv_msg_render(m, xml, sizeof xml, &len) == ZXV_OK, "camt053 render");
    CHECK(strstr(xml, "camt.053.001.08") && strstr(xml, "12500.25"), "camt053 content");
    zxv_msg_destroy(m);

    /* Stubs */
    CHECK(zxv_msg_create(g_ctx, ZXV_MX_PACS002, &m) == ZXV_E_NOTIMPL, "pacs002 pending");
    zxv_netting *nc = NULL;
    CHECK(zxv_netting_open(g_ctx, "C1", "NGN", &nc) == ZXV_E_NOTIMPL && nc == NULL, "netting pending");
    uint64_t card = 0;
    CHECK(zxv_card_issue(g_ctx, ZXV_NET_DRAGON, a, 1000, &card, buf, sizeof buf, &len) == ZXV_E_NOTIMPL, "cards pending");
    int32_t passed = 0;
    CHECK(zxv_vss_run(g_ctx, ZXV_VSS_C1_BALANCE, &passed, buf, sizeof buf, &len) == ZXV_E_NOTIMPL, "vss pending");
    if (feat & ZXV_FEAT_CARD_CHECK) {
        int32_t v = 0;
        CHECK(zxv_card_check_digit(ZXV_NET_DRAGON, "79927398713", &v) == ZXV_OK && v == 1, "Luhn valid");
        CHECK(zxv_card_check_digit(ZXV_NET_DRAGON, "79927398710", &v) == ZXV_OK && v == 0, "Luhn invalid");
        CHECK(zxv_card_check_digit(ZXV_NET_THUNDERBIRD, "2363", &v) == ZXV_OK && v == 1, "Verhoeff valid");
        CHECK(zxv_card_check_digit(ZXV_NET_PHOENIX, "5724", &v) == ZXV_OK && v == 1, "Damm valid");
    }

    zxv_ctx_destroy(g_ctx);
    printf("%d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
