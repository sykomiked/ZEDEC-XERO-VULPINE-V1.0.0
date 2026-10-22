/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Reckoner's model against zxvd's real dispatcher (LINK). The books are a
 * scratch file in the test's own folder, never real books: they are
 * filled through Coffer's model, read back by Reckoner's, and closed again
 * (in memory) at the end, the saved reports removed. */
#include "coffer_model.h"
#include "reckoner_model.h"

static reckoner_t RK;
static coffer_t RKC;

APPS_TEST(reckoner)
{
    /* the rules */
    CHECK(reckoner_statement_close(0, 0, 0) == 0 && reckoner_statement_close(100, 250, 50) == 300 &&
              reckoner_statement_close(-500, 0, 100) == -600 &&
              reckoner_statement_close(RECKONER_MINOR_MAX, 0, RECKONER_MINOR_MAX) == 0 &&
              reckoner_statement_close(RECKONER_MINOR_MAX, 1, 0) == RECKONER_REFUSED &&
              reckoner_statement_close(-RECKONER_MINOR_MAX, 0, 1) == RECKONER_REFUSED &&
              reckoner_statement_close(0, -1, 0) == RECKONER_REFUSED &&
              reckoner_statement_close(0, 0, RECKONER_MINOR_MAX + 1) == RECKONER_REFUSED,
          "statement close: 18 digits either way, each direction at least 0");
    CHECK(reckoner_band_tax(50000, 10000, 100000, 2000) == 8000 &&
              reckoner_band_tax(150000, 10000, 100000, 2000) == 18000 &&
              reckoner_band_tax(10000, 10000, 100000, 2000) == 0 &&
              reckoner_band_tax(10001, 0, 100000, 3333) == 3333 &&
              reckoner_band_tax(RECKONER_TAX_MAX, 0, RECKONER_TAX_MAX, 10000) == RECKONER_TAX_MAX &&
              reckoner_band_tax(RECKONER_TAX_MAX + 1, 0, RECKONER_TAX_MAX, 10000) ==
                  RECKONER_REFUSED &&
              reckoner_band_tax(1, 0, 1, 10001) == RECKONER_REFUSED &&
              reckoner_band_tax(1, 0, 1, -1) == RECKONER_REFUSED &&
              reckoner_band_tax(1, 2, 1, 1) == RECKONER_REFUSED &&
              reckoner_band_tax(-1, 0, 1, 1) == RECKONER_REFUSED,
          "band tax: its edges");
    int64_t lo[3] = {0, 1000000, 5000000}, hi[3] = {1000000, 5000000, 99999999},
            rt[3] = {0, 2000, 4000}, tax[3] = {7, 7, 7};
    int64_t bad_lo[2] = {0, 50}, bad_hi[2] = {100, 200}, bad_rt[2] = {1000, 1000};
    CHECK(reckoner_tax_estimate(6000000, lo, hi, rt, 3, tax) == 1200000 && tax[1] == 800000 &&
              reckoner_tax_estimate(0, lo, hi, rt, 3, tax) == 0 &&
              reckoner_tax_estimate(6000000, lo, hi, rt, 0, tax) == RECKONER_REFUSED &&
              reckoner_tax_estimate(6000000, lo, hi, rt, RECKONER_BANDS + 1, tax) ==
                  RECKONER_REFUSED &&
              reckoner_tax_estimate(150, bad_lo, bad_hi, bad_rt, 2, tax) == RECKONER_REFUSED &&
              tax[0] == 0,
          "tax estimate: bands in order, never overlapping");
    CHECK(reckoner_report_ok(1, 1) && !reckoner_report_ok(1, 0) && !reckoner_report_ok(0, 1) &&
              !reckoner_report_ok(2, 1) && !reckoner_report_ok(1, -1),
          "report allowed: each flag exactly 1");

    /* scratch books, filled through Coffer */
    char root[256], scratch[512], path[COFFER_PATH_MAX + 1], file[600];
    bool saved = false;
    CHECK(zxf_root(root, sizeof root) == 0, "the test's folder");
    snprintf(scratch, sizeof scratch, "%s/reckoner-scratch.books", root);
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file(scratch) == 0 && zxvd_rpc_books_open() == 0, "scratch books open");
    coffer_init(&RKC, &LINK, COFFER_DAY_CAP);
    CHECK(coffer_books(&RKC, path, sizeof path, &saved) == ZXM_OK && saved &&
              !strcmp(path, scratch),
          "every post goes to the scratch file: %s", path);
    uint32_t employer = 0, cash = 0, savings = 0;
    uint64_t entry = 0;
    CHECK(coffer_enable(&RKC, "USD") == ZXM_OK &&
              coffer_open_account(&RKC, "employer", "USD", true, &employer) == ZXM_OK &&
              coffer_open_account(&RKC, "cash", "USD", false, &cash) == ZXM_OK &&
              coffer_open_account(&RKC, "savings", "USD", false, &savings) == ZXM_OK,
          "accounts: %s", LINK.err);
    CHECK(coffer_propose(&RKC, employer, cash, 250000, "salary 1") == ZXM_OK &&
              coffer_confirm(&RKC, &entry) == ZXM_OK &&
              coffer_propose(&RKC, cash, savings, 60000, "save 1") == ZXM_OK &&
              coffer_confirm(&RKC, &entry) == ZXM_OK &&
              coffer_propose(&RKC, employer, cash, 1500, "refund 1") == ZXM_OK &&
              coffer_confirm(&RKC, &entry) == ZXM_OK &&
              coffer_propose(&RKC, cash, savings, 20000, "save 2") == ZXM_OK &&
              coffer_confirm(&RKC, &entry) == ZXM_OK &&
              coffer_propose(&RKC, cash, savings, 5, "save 3") == ZXM_OK &&
              coffer_confirm(&RKC, &entry) == ZXM_OK,
          "five postings: %s", LINK.err);
    CHECK(coffer_entries(&RKC, cash) == ZXM_OK && RKC.ne == 5, "cash's journal: %u", RKC.ne);
    uint64_t id[5];
    for (uint32_t i = 0; i < 5; i++) id[i] = RKC.e[i].id;

    /* nothing is asked before it is checked */
    reckoner_init(&RK, &LINK);
    int before = sent;
    CHECK(RK.job == -1 && !RK.period_set && reckoner_statement(&RK, cash) == ZXM_E_STATE &&
              reckoner_set_period(&RK, 5, 4) == ZXM_E_ARG && reckoner_close(&RK) == ZXM_E_STATE &&
              reckoner_reopen(&RK) == ZXM_E_STATE &&
              reckoner_render(&RK, "stmt-1", "2026-10-21T09:00:00") == ZXM_E_STATE &&
              reckoner_render(&RK, "stmt 1", "2026-10-21T09:00:00") == ZXM_E_ARG &&
              reckoner_render(&RK, "stmt-1", "2026-13-21T09:00:00") == ZXM_E_ARG &&
              reckoner_render(&RK, "stmt-1", "2026-10-21 09:00:00") == ZXM_E_ARG &&
              reckoner_save_statement(&RK, "a/b.xml") == ZXM_E_ARG &&
              reckoner_save_statement(&RK, ".hidden") == ZXM_E_ARG &&
              reckoner_save_statement(&RK, "s.xml") == ZXM_E_STATE &&
              reckoner_save_estimate(&RK, "t.txt") == ZXM_E_STATE &&
              reckoner_print(&RK, "http://printer") == ZXM_E_ARG &&
              reckoner_print(&RK, "ipp://printer/ipp/print") == ZXM_E_STATE && sent == before,
          "bad input and wrong order refused before anything is asked");

    /* RK3: the period is entries 2..4 of cash: opening 190000, in 1500, out 20000 */
    CHECK(reckoner_set_period(&RK, id[1] + 1, id[3]) == ZXM_OK &&
              reckoner_statement(&RK, cash) == ZXM_OK && RK.st.ready && RK.st.n == 2 &&
              RK.st.opening == 190000 && RK.st.credits == 1500 && RK.st.debits == 20000 &&
              RK.st.net == -18500 && RK.st.closing == 171500 && !strcmp(RK.st.ccy, "USD") &&
              !strcmp(RK.st.name, "cash") && RK.st.e[0].minor == 1500 && RK.st.e[1].minor == -20000,
          "a statement: %lld %lld %lld %s", (long long) RK.st.opening, (long long) RK.st.closing,
          (long long) RK.st.n, LINK.err);
    CHECK(reckoner_statement(&RK, 999) == ZXM_E_ARG && RK.st.ready && RK.st.account == cash,
          "no such account: the statement kept");
    CHECK(reckoner_set_period(&RK, 0, id[4]) == ZXM_OK && !RK.st.ready &&
              reckoner_statement(&RK, cash) == ZXM_OK && RK.st.opening == 0 && RK.st.n == 5 &&
              RK.st.closing == 171495 && RK.st.credits == 251500 && RK.st.debits == 80005,
          "the whole journal: %lld", (long long) RK.st.closing);

    /* RK4: an open period is never reported */
    before = sent;
    CHECK(reckoner_render(&RK, "stmt-1", "2026-10-21T09:00:00") == ZXM_E_STATE &&
              strstr(RK.notice, "Close the period") && sent == before && !reckoner_allowed(&RK),
          "an open period: %s", RK.notice);
    CHECK(reckoner_close(&RK) == ZXM_OK && RK.closed && !RK.st.ready &&
              reckoner_set_period(&RK, 0, 1) == ZXM_E_STATE && RK.to == id[4] &&
              reckoner_close(&RK) == ZXM_E_STATE,
          "closed: it cannot be moved");
    CHECK(reckoner_render(&RK, "stmt-1", "2026-10-21T09:00:00") == ZXM_E_STATE,
          "read again from the closed period first");
    CHECK(reckoner_statement(&RK, cash) == ZXM_OK && reckoner_check(&RK) == ZXM_OK && RK.books_ok &&
              reckoner_allowed(&RK) && strstr(RK.notice, "The books hold"),
          "ledger.check: %s", LINK.err);

    /* RK5: camt.053. This harness does not link zxvd's payments group
     * (zxvd_pay.c), so iso20022.render is not found here: the request is
     * checked as sent, and one answer serves both ledger.check and the
     * render. */
    CHECK(reckoner_render(&RK, "stmt-1", "2026-10-21T09:00:00") == ZXM_E_REMOTE && RK.xml_len == 0,
          "iso20022.render is not in this harness: %s", LINK.err);
    zxm_link_t *real = RK.link;
    RK.link = &FAKE;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"ok\":true,\"kind\":\"camt053\","
             "\"xml\":\"<Document xmlns=\\\"urn:iso:std:iso:20022:tech:xsd:camt.053.001.08\\\">"
             "<BkToCstmrStmt><GrpHdr><MsgId>stmt-1</MsgId></GrpHdr><Stmt><Acct><Ccy>USD</Ccy>"
             "</Acct></Stmt></BkToCstmrStmt></Document>\\n\"}}";
    CHECK(reckoner_render(&RK, "stmt-1", "2026-10-21T09:00:00") == ZXM_OK && RK.xml_len > 0 &&
              strstr(RK.xml, "camt.053") && strstr(RK.xml, "stmt-1") && strstr(RK.xml, "USD") &&
              strstr(RK.notice, "nothing was sent"),
          "rendered: %s", RK.notice);
    CHECK(strstr(FAKE.req, "\"iso20022.render\"") && strstr(FAKE.req, "\"kind\":\"camt053\"") &&
              strstr(FAKE.req, "\"msg_id\":\"stmt-1\"") &&
              strstr(FAKE.req, "\"cre_dt_tm\":\"2026-10-21T09:00:00\"") &&
              strstr(FAKE.req, "\"ccy\":\"USD\"") && strstr(FAKE.req, "\"opening\":\"0\"") &&
              strstr(FAKE.req, "\"closing\":\"171495\"") &&
              strstr(FAKE.req, "{\"amount_minor\":\"250000\",\"direction\":\"credit\","
                               "\"status\":\"booked\"}") &&
              strstr(FAKE.req, "{\"amount_minor\":\"5\",\"direction\":\"debit\"") &&
              strstr(FAKE.req, "\"tx_count\":5}"),
          "the camt.053 asked for: %s", FAKE.req);
    static char kept[RECKONER_XML_MAX + 1];
    memcpy(kept, RK.xml, RK.xml_len + 1u);
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"ok\":true,\"kind\":\"pacs008\","
             "\"xml\":\"<x/>\"}}";
    CHECK(reckoner_render(&RK, "stmt-1", "2026-10-21T09:00:00") == ZXM_E_PROTOCOL,
          "another kind answered: refused");
    memcpy(RK.xml, kept, strlen(kept) + 1u);
    RK.xml_len = (uint32_t) strlen(kept);
    RK.link = real;

    /* the jurisdiction and RK6, the estimate */
    CHECK(reckoner_jurisdiction(&RK) == ZXM_OK && RK.juris_loaded && strlen(RK.country) <= 2,
          "jurisdiction.get: %s", LINK.err);
    CHECK(reckoner_tax(&RK, 6000000, lo, hi, rt, 3) == ZXM_OK && RK.tax.ready &&
              RK.tax.total == 1200000 && RK.tax.tax[2] == 400000 &&
              strstr(RK.text, "not tax advice") && strstr(RK.text, "A person files") &&
              strstr(RK.text, "Band 1000000 to 5000000 at 20.00%: 800000") &&
              strstr(RK.text, "Estimated tax: 1200000 minor units"),
          "an estimate: %s", RK.text);
    CHECK(reckoner_tax(&RK, 150, bad_lo, bad_hi, bad_rt, 2) == ZXM_E_ARG && RK.tax.total == 1200000,
          "overlapping bands refused, the estimate kept");

    /* RK7: saved through the Banker, a new file only */
    CHECK(reckoner_save_statement(&RK, "reckoner-stmt-1.xml") == ZXM_OK &&
              !strcmp(RK.saved, "reckoner-stmt-1.xml"),
          "statement saved: %s", LINK.err);
    snprintf(file, sizeof file, "%s/reckoner-stmt-1.xml", root);
    FILE *f = fopen(file, "rb");
    long size = -1;
    if (f) {
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fclose(f);
    }
    CHECK(size == (long) RK.xml_len, "the file is the statement: %ld of %u", size, RK.xml_len);
    CHECK(reckoner_save_statement(&RK, "reckoner-stmt-1.xml") == ZXM_E_REMOTE &&
              strstr(RK.notice, "never replaces"),
          "never over an earlier report: %s", LINK.err);
    CHECK(reckoner_save_estimate(&RK, "reckoner-tax-1.txt") == ZXM_OK &&
              !strcmp(RK.saved, "reckoner-tax-1.txt"),
          "estimate saved: %s", LINK.err);

    /* a long statement goes in parts: the same file, whole */
    static char big[RECKONER_XML_MAX + 1];
    memcpy(big, RK.xml, RK.xml_len + 1u);
    uint32_t big_len = RK.xml_len;
    CHECK(big_len > 0, "a statement to repeat");
    while (big_len > 0 && RK.xml_len + big_len < 12000u) {
        memcpy(RK.xml + RK.xml_len, big, big_len);
        RK.xml_len += big_len;
    }
    RK.xml[RK.xml_len] = 0;
    CHECK(reckoner_save_statement(&RK, "reckoner-stmt-long.xml") == ZXM_OK,
          "a long statement in parts: %s", LINK.err);
    snprintf(file, sizeof file, "%s/reckoner-stmt-long.xml", root);
    f = fopen(file, "rb");
    size = -1;
    if (f) {
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fclose(f);
    }
    CHECK(size == (long) RK.xml_len, "whole: %ld of %u", size, RK.xml_len);
    remove(file);

    /* RK4 again: books that do not hold stop a save; a print answers its job */
    RK.link = &FAKE;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"ok\":false}}";
    CHECK(reckoner_save_estimate(&RK, "reckoner-tax-2.txt") == ZXM_E_STATE && !RK.books_ok &&
              !reckoner_allowed(&RK) && strstr(RK.notice, "do not hold"),
          "books that do not hold: %s", RK.notice);
    CHECK(reckoner_print(&RK, "ipp://printer/ipp/print") == ZXM_E_STATE, "nor a print");
    RK.books_ok = true;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"job_id\":42,\"state\":\"pending\"}}";
    CHECK(reckoner_print(&RK, "ipp://printer/ipp/print") == ZXM_OK && RK.job == 42 &&
              strstr(FAKE.req, "\"print.submit\"") &&
              strstr(FAKE.req, "\"path\":\"reckoner-stmt-long.xml\""),
          "print.submit: job %lld", (long long) RK.job);
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"state\":\"pending\"}}";
    CHECK(reckoner_print(&RK, "ipp://printer/ipp/print") == ZXM_E_PROTOCOL && RK.job == 42,
          "a job without its id");
    /* RK3: an answer that does not add up is refused, the statement kept */
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"accounts\":[{\"account\":1,"
             "\"name\":\"cash\",\"currency\":\"usd\",\"kind\":\"holder\"}]}}";
    CHECK(reckoner_statement(&RK, 1) == ZXM_E_PROTOCOL && RK.st.closing == 171495,
          "a currency that is no code");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"country\":\"usa\",\"standard\":\"\","
             "\"report_standard\":\"\"}}";
    CHECK(reckoner_jurisdiction(&RK) == ZXM_E_PROTOCOL, "a country that is no code");
    RK.link = real;
    CHECK(reckoner_reopen(&RK) == ZXM_OK && !RK.closed && RK.xml_len == 0 &&
              reckoner_save_statement(&RK, "reckoner-stmt-2.xml") == ZXM_E_STATE,
          "reopened: nothing rendered from it remains");

    /* back to books in memory: the scratch file and reports removed */
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file("") == 0 && remove(scratch) == 0, "scratch books removed");
    snprintf(file, sizeof file, "%s/reckoner-stmt-1.xml", root);
    CHECK(remove(file) == 0, "statement removed");
    snprintf(file, sizeof file, "%s/reckoner-tax-1.txt", root);
    CHECK(remove(file) == 0, "estimate removed");
}
