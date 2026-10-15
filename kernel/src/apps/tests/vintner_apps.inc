/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Vintner's model against zxvd's real dispatcher (LINK). Every sale goes to
 * a scratch books file in the test's own folder, never to real books, as
 * Coffer's test does. The grant engine is not linked in this in-process
 * zxvd, so grants.status is answered on a link of Vintner's own (VN_LINK),
 * which passes every other request to the real dispatcher. */
#include "vintner_model.h"

static vintner_t VN, VN2;
static coffer_t VN_CF;
static zxm_link_t VN_LINK;
static const char *VN_GRANTS = "{}"; /* the grants.status result */
static char vn_req[ZXM_REQ_MAX + 1];

static int32_t vn_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    if (strstr(req, "\"grants.status\"")) {
        memcpy(vn_req, req, len);
        vn_req[len] = 0;
        sent++;
        return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq,
                        VN_GRANTS);
    }
    return inproc(NULL, req, len, resp, cap);
}

static const coffer_account_t *vn_named(const coffer_t *c, const char *name)
{
    for (uint32_t i = 0; i < c->n; i++)
        if (!strcmp(c->a[i].name, name)) return &c->a[i];
    return 0;
}

APPS_TEST(vintner)
{
    /* the rules (VN1-VN4) */
    CHECK(vintner_sale_value(0, 0) == 0 && vintner_sale_value(12, 2499) == 29988 &&
              vintner_sale_value(VINTNER_NINE, VINTNER_NINE) == INT64_C(999999998000000001) &&
              vintner_sale_value(VINTNER_NINE + 1, 1) == VINTNER_REFUSED &&
              vintner_sale_value(1, VINTNER_NINE + 1) == VINTNER_REFUSED &&
              vintner_sale_value(-1, 1) == VINTNER_REFUSED &&
              vintner_sale_value(1, -1) == VINTNER_REFUSED,
          "vintner: a sale's value, nine digits each");
    CHECK(vintner_flow_gap(1000000, 750000, 250000) == 0 && vintner_flow_gap(100, 90, 5) == 5 &&
              vintner_flow_gap(0, 0, 1) == -1 && vintner_flow_gap(INT64_MAX, 0, 0) == INT64_MAX &&
              vintner_flow_gap(INT64_MAX, -1, 0) == VINTNER_REFUSED &&
              vintner_flow_gap(0, INT64_MAX, 1) == VINTNER_REFUSED &&
              vintner_flow_gap(-2, INT64_MAX, 0) == VINTNER_REFUSED,
          "vintner: what a stage leaves unaccounted, exact, refused past 64 bits");
    CHECK(vintner_probe_tenths(-880) == -550 && vintner_probe_tenths(2000) == 1250 &&
              vintner_probe_tenths(0) == 0 && vintner_probe_tenths(-1) == -1 &&
              vintner_probe_tenths(1) == 0 && vintner_probe_tenths(200) == 125 &&
              vintner_probe_tenths(-879) == -550 && vintner_probe_tenths(2001) == VINTNER_REFUSED &&
              vintner_probe_tenths(-881) == VINTNER_REFUSED,
          "vintner: probe counts in tenths, floored, refused outside the range");
    CHECK(vintner_lot_fits(1, 2024, 1, 0, 0) &&
              vintner_lot_fits(VINTNER_NINE, 9999, 9, VINTNER_NINE, VINTNER_VOLUME_MAX) &&
              vintner_lot_fits(-7, 2024, 1, 0, 0) &&
              !vintner_lot_fits(VINTNER_NINE + 1, 1, 1, 0, 0) &&
              !vintner_lot_fits(1, 10000, 1, 0, 0) && !vintner_lot_fits(1, 1, 10, 0, 0) &&
              !vintner_lot_fits(1, 1, 1, VINTNER_NINE + 1, 0) &&
              !vintner_lot_fits(1, 1, 1, 0, VINTNER_VOLUME_MAX + 1),
          "vintner: a ^ZXVINTNER record's digits");

    /* a scratch books file, never the owner's */
    char root[256], scratch[512];
    CHECK(zxf_root(root, sizeof root) == 0, "vintner: the test's folder");
    snprintf(scratch, sizeof scratch, "%s/vintner-scratch.books", root);
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file(scratch) == 0 && zxvd_rpc_books_open() == 0,
          "vintner: scratch books open");
    coffer_init(&VN_CF, &LINK, -1);
    CHECK(coffer_enable(&VN_CF, "USD") == ZXM_OK, "vintner: USD enabled: %s", LINK.err);
    zxm_link_init(&VN_LINK, vn_send, &VN_LINK);
    vintner_init(&VN, &VN_LINK);

    /* nothing is sent before it is checked */
    int before = sent;
    uint64_t entry = 0;
    CHECK(vintner_open_books(&VN, "usd") == ZXM_E_ARG &&
              vintner_open_books(&VN, "USDX") == ZXM_E_ARG &&
              vintner_open_books(&VN, NULL) == ZXM_E_ARG &&
              vintner_propose_sale(&VN, 101, 1, 1, "x") == ZXM_E_STATE &&
              vintner_confirm_sale(&VN, &entry) == ZXM_E_STATE &&
              vintner_confirm_sale(&VN, NULL) == ZXM_E_ARG && vintner_sales(&VN) == ZXM_E_STATE &&
              vintner_revenue(&VN) == ZXM_E_STATE && sent == before,
          "vintner: bad input refused before anything is asked");

    /* the books: two accounts, opened once */
    CHECK(vintner_open_books(&VN, "USD") == ZXM_OK && VN.books && VN.revenue == 0 &&
              !strcmp(VN.ccy, "USD") && VN.sales != VN.buyers,
          "vintner: the sales and buyers accounts: %s", VN_LINK.err);
    CHECK(coffer_load(&VN_CF) == ZXM_OK && vn_named(&VN_CF, VINTNER_SALES_NAME) &&
              !vn_named(&VN_CF, VINTNER_SALES_NAME)->issuer &&
              vn_named(&VN_CF, VINTNER_SALES_NAME)->id == VN.sales &&
              vn_named(&VN_CF, VINTNER_BUYERS_NAME)->issuer &&
              vn_named(&VN_CF, VINTNER_BUYERS_NAME)->id == VN.buyers,
          "vintner: ledger.open_account: a holder for sales, an issuer for buyers");
    uint32_t accounts = VN_CF.n;
    vintner_init(&VN2, &VN_LINK);
    CHECK(vintner_open_books(&VN2, "USD") == ZXM_OK && VN2.sales == VN.sales &&
              VN2.buyers == VN.buyers && coffer_load(&VN_CF) == ZXM_OK && VN_CF.n == accounts,
          "vintner: the accounts are found again, never opened twice");

    /* VN2: lots, every step conserved */
    CHECK(vintner_add_lot(&VN, 101, 2024, 1000000) == ZXM_OK &&
              vintner_add_lot(&VN, 101, 2024, 5) == ZXM_E_STATE &&
              vintner_add_lot(&VN, 102, 0, 5) == ZXM_E_ARG &&
              vintner_add_lot(&VN, 102, 10000, 5) == ZXM_E_ARG &&
              vintner_add_lot(&VN, 102, 2024, 0) == ZXM_E_ARG &&
              vintner_add_lot(&VN, 1000000000u, 2024, 5) == ZXM_E_ARG &&
              vintner_add_lot(&VN, 102, 2024, VINTNER_VOLUME_MAX + 1) == ZXM_E_ARG && VN.nlots == 1,
          "vintner: lots added, bad ones refused");
    const vintner_lot_t *l = vintner_lot(&VN, 101);
    CHECK(l && l->stage == VINTNER_HARVEST && l->volume == 1000000 && vintner_conserved(l) &&
              !vintner_lot(&VN, 102),
          "vintner: a lot at harvest");
    CHECK(vintner_advance(&VN, 101, 750000, 200000) == ZXM_E_ARG && l->stage == VINTNER_HARVEST &&
              l->volume == 1000000 && l->lost == 0 && strstr(VN.notice, "do not add up"),
          "vintner: 50 l unaccounted: refused, nothing changed: %s", VN.notice);
    CHECK(vintner_advance(&VN, 101, 1000001, 0) == ZXM_E_ARG &&
              vintner_advance(&VN, 101, -1, 1000001) == ZXM_E_ARG &&
              vintner_advance(&VN, 101, 1000000, -1) == ZXM_E_ARG &&
              vintner_advance(&VN, 7, 1, 0) == ZXM_E_ARG,
          "vintner: more out than in, negatives and unknown lots refused");
    CHECK(vintner_bottle(&VN, 101, 1, 750, 0) == ZXM_E_STATE, "vintner: no bottling at harvest");
    CHECK(vintner_advance(&VN, 101, 750000, 250000) == ZXM_OK && l->stage == VINTNER_MUST &&
              vintner_advance(&VN, 101, 600000, 150000) == ZXM_OK && l->stage == VINTNER_BARREL &&
              l->lost == 400000 && vintner_conserved(l),
          "vintner: pressed to must, racked to barrel, conserved");
    CHECK(vintner_advance(&VN, 101, 600000, 0) == ZXM_E_STATE,
          "vintner: no more steps before bottling");
    CHECK(vintner_bottle(&VN, 101, 801, 750, 0) == ZXM_E_ARG &&
              vintner_bottle(&VN, 101, 798, 750, 1499) == ZXM_E_ARG &&
              vintner_bottle(&VN, 101, 0, 750, 0) == ZXM_E_ARG &&
              vintner_bottle(&VN, 101, 1, 0, 0) == ZXM_E_ARG && l->stage == VINTNER_BARREL,
          "vintner: bottling that does not add up is refused");
    CHECK(vintner_bottle(&VN, 101, 798, 750, 1500) == ZXM_OK && l->stage == VINTNER_BOTTLED &&
              l->bottles == 798 && l->volume == 598500 && l->lost == 401500 && vintner_conserved(l),
          "vintner: 798 bottles of 750 ml, 1.5 l lost, conserved");
    CHECK(vintner_bottle(&VN, 101, 1, 750, 0) == ZXM_E_STATE, "vintner: bottled once");
    CHECK(vintner_add_lot(&VN, 102, 2025, 400000) == ZXM_OK, "vintner: a second lot");

    /* VN3: the cellar probe */
    before = sent;
    CHECK(vintner_probe(&VN, 200) == ZXM_OK && VN.probe_ok && VN.probe_tenths == 125 &&
              !strcmp(VN.notice, "Cellar at 12.5 degrees."),
          "vintner: probe: %s", VN.notice);
    CHECK(vintner_probe(&VN, -1) == ZXM_OK && VN.probe_tenths == -1 &&
              !strcmp(VN.notice, "Cellar at -0.1 degrees."),
          "vintner: probe below zero: %s", VN.notice);
    CHECK(vintner_probe(&VN, 2001) == ZXM_E_ARG && !VN.probe_ok &&
              strstr(VN.notice, "check the probe") && sent == before,
          "vintner: a count past the range: refused, nothing sent");

    /* labels from the grant engine */
    uint32_t found = 9;
    VN_GRANTS = "{\"set_up\":false,\"keys\":0,\"minted\":false,\"sandbox\":true}";
    CHECK(vintner_labels(&VN, 1700000000u, &found) == ZXM_OK && found == 0 &&
              strstr(vn_req, "\"now\":1700000000") && !l->labelled,
          "vintner: no grant engine set up: no labels: %s", vn_req);
    VN_GRANTS = "{\"set_up\":true,\"lots\":[{\"lot\":7,\"count\":\"1\",\"backed\":false,"
                "\"label\":\"BRL-V lot 7\",\"status\":\"unbacked\"},{\"lot\":101,\"count\":\"3\","
                "\"backed\":true,\"label\":\"BRL-V lot 101\",\"status\":\"backed\"}]}";
    CHECK(vintner_labels(&VN, 1700000000u, &found) == ZXM_OK && found == 1 && l->labelled &&
              l->backed && !strcmp(l->label, "BRL-V lot 101") && !strcmp(l->status, "backed") &&
              !vintner_lot(&VN, 102)->labelled,
          "vintner: a barrel lot's label: %s %s", l->label, l->status);
    VN_GRANTS = "{\"set_up\":true,\"lots\":[{\"lot\":101,\"count\":\"3\",\"backed\":false,"
                "\"label\":\"\",\"status\":\"unbacked\"}]}";
    CHECK(vintner_labels(&VN, 1700000000u, &found) == ZXM_E_PROTOCOL && l->backed &&
              !strcmp(l->label, "BRL-V lot 101"),
          "vintner: an empty label is refused and the copy kept");
    VN_GRANTS = "{\"set_up\":true,\"lots\":7}";
    CHECK(vintner_labels(&VN, 1700000000u, &found) == ZXM_E_PROTOCOL &&
              vintner_labels(&VN, 1, NULL) == ZXM_E_ARG,
          "vintner: lots that are no list refused");

    /* VN5: a sale waits for the owner; nothing is posted until confirmed */
    before = sent;
    CHECK(vintner_propose_sale(&VN, 102, 1, 1, "lot 102") == ZXM_E_STATE &&
              strstr(VN.notice, "bottled lot") &&
              vintner_propose_sale(&VN, 9, 1, 1, "x") == ZXM_E_ARG &&
              vintner_propose_sale(&VN, 101, 799, 1, "x") == ZXM_E_ARG &&
              vintner_propose_sale(&VN, 101, 0, 1, "x") == ZXM_E_ARG &&
              vintner_propose_sale(&VN, 101, 1, 0, "zero") == ZXM_E_ARG &&
              vintner_propose_sale(&VN, 101, 1, VINTNER_NINE + 1, "dear") == ZXM_E_ARG &&
              vintner_propose_sale(&VN, 101, 1, -1, "neg") == ZXM_E_ARG &&
              vintner_propose_sale(&VN, 101, 1, 1, "") == ZXM_E_ARG &&
              vintner_propose_sale(&VN, 101, 1, 1, "a reference that is longer than 35 b") ==
                  ZXM_E_ARG &&
              !VN.held.pending && sent == before,
          "vintner: bad sales refused before anything is asked");
    CHECK(vintner_propose_sale(&VN, 101, 12, 2499, "case 1") == ZXM_OK && VN.held.pending &&
              VN.held.value == 29988 && sent == before &&
              strstr(VN.notice, "Sell 12 bottles of lot 101 at 2499 = 29988 minor units of USD") &&
              strstr(VN.notice, "Waiting for your confirmation"),
          "vintner: proposed, nothing sent: %s", VN.notice);
    CHECK(vintner_propose_sale(&VN, 101, 1, 1, "case 2") == ZXM_E_STATE && sent == before &&
              vintner_open_books(&VN, "USD") == ZXM_E_STATE,
          "vintner: one sale waits at a time");
    CHECK(vintner_revenue(&VN) == ZXM_OK && VN.revenue == 0 && l->sold == 0,
          "vintner: the books do not have it yet");
    CHECK(vintner_confirm_sale(&VN, &entry) == ZXM_OK && !VN.held.pending && VN.revenue == 29988 &&
              l->sold == 12 && strstr(VN.notice, "Posted: 12 bottles"),
          "vintner: confirmed and posted: %s", VN_LINK.err);
    CHECK(vintner_confirm_sale(&VN, &entry) == ZXM_E_STATE &&
              vintner_cancel_sale(&VN) == ZXM_E_STATE,
          "vintner: nothing waits now");
    CHECK(vintner_propose_sale(&VN, 101, 787, 1, "too many") == ZXM_E_ARG &&
              strstr(VN.notice, "left to sell"),
          "vintner: only what is left can be sold");
    before = sent;
    CHECK(vintner_propose_sale(&VN, 101, 6, 2499, "case 9") == ZXM_OK &&
              vintner_cancel_sale(&VN) == ZXM_OK && !VN.held.pending && sent == before &&
              strstr(VN.notice, "nothing was posted") && l->sold == 12,
          "vintner: a cancelled sale posts nothing");
    CHECK(vintner_propose_sale(&VN, 101, 6, 2499, "case 1") == ZXM_OK &&
              vintner_confirm_sale(&VN, &entry) == ZXM_E_REMOTE && !VN.held.pending &&
              strstr(VN.notice, "refused") && l->sold == 12 && VN.revenue == 29988,
          "vintner: the books refuse a reference already used: %d %s", VN_LINK.err_code,
          VN_LINK.err);
    CHECK(vintner_propose_sale(&VN, 101, 786, 1500, "case 2") == ZXM_OK &&
              vintner_confirm_sale(&VN, &entry) == ZXM_OK && VN.revenue == 29988 + 1179000 &&
              l->sold == 798,
          "vintner: the rest sold: %s", VN_LINK.err);
    CHECK(vintner_propose_sale(&VN, 101, 1, 1, "none left") == ZXM_E_ARG, "vintner: sold out");

    /* the sales journal and the books check */
    CHECK(vintner_sales(&VN) == ZXM_OK && VN.ne == 2 && VN.entry[0] == 29988 &&
              VN.entry[1] == 1179000,
          "vintner: ledger.entries, oldest first: %u %s", VN.ne, VN_LINK.err);
    CHECK(coffer_check(&VN_CF) == ZXM_OK && VN_CF.books_ok, "vintner: the books hold: %s",
          LINK.err);
    CHECK(vintner_revenue(&VN2) == ZXM_OK && VN2.revenue == VN.revenue,
          "vintner: a second model reads the same revenue");

    /* VN6: an answer that does not add up is refused and the copy kept */
    zxm_link_t *real = VN.link;
    VN.link = &FAKE;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"debit_minor\":\"5\",\"credit_minor\":"
             "\"1\",\"equity_minor\":\"5\"}}";
    CHECK(vintner_revenue(&VN) == ZXM_E_PROTOCOL && VN.revenue == 1208988,
          "vintner: equity not debit - credit");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"entries\":[{\"entry_id\":\"1\","
             "\"signed_minor\":\"5\",\"counterparty\":0,\"reference\":\"x\"}]}}";
    CHECK(vintner_sales(&VN) == ZXM_E_PROTOCOL && VN.ne == 2, "vintner: entries not the revenue");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"entries\":[{\"entry_id\":\"1\","
             "\"signed_minor\":\"01208988\",\"counterparty\":0,\"reference\":\"x\"}]}}";
    CHECK(vintner_sales(&VN) == ZXM_E_PROTOCOL && VN.ne == 2, "vintner: a leading zero");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"accounts\":[{\"account\":3,"
             "\"name\":\"vintner sales\",\"currency\":\"EUR\",\"kind\":\"issuer\"},{\"account\":4,"
             "\"name\":\"vintner sales\",\"currency\":\"USD\",\"kind\":\"issuer\"}]}}";
    CHECK(vintner_open_books(&VN, "USD") == ZXM_E_PROTOCOL && VN.sales == VN2.sales,
          "vintner: a sales account that is an issuer is refused");
    VN.link = real;

    /* back to books in memory: the scratch file is removed */
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file("") == 0 && remove(scratch) == 0, "vintner: scratch books removed");
    CHECK(vintner_revenue(&VN2) == ZXM_E_REMOTE && VN2.revenue == VN.revenue,
          "vintner: books closed: %s", VN_LINK.err);
}
