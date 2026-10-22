/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Mercer's model against zxvd's real dispatcher (LINK). The books are a
 * scratch file in the test's own folder, never real books: accounts are
 * opened through Coffer's model, sales posted through Mercer's (and
 * Tallystick's under it), and the books closed again (in memory) at the
 * end, the saved catalogue and receipt removed. The market's offers and
 * the seller's card are made up (FAKE), since this zxvd has neither. */
#include "mercer_model.h"

static mercer_t MERC_A, MERC_B;
static coffer_t MERC_BOOKS;

/* the entries on account's journal, read from the books */
static uint32_t mercer_entries_of(uint32_t account)
{
    if (coffer_load(&MERC_BOOKS) != ZXM_OK || coffer_entries(&MERC_BOOKS, account) != ZXM_OK)
        return UINT32_MAX;
    return MERC_BOOKS.ne;
}

APPS_TEST(mercer)
{
    /* the rules (MR1-MR6) */
    CHECK(mercer_stock_after(10, 3) == 7 && mercer_stock_after(5, 5) == 0 &&
              mercer_stock_after(MERCER_STOCK_MAX, 1) == MERCER_STOCK_MAX - 1 &&
              mercer_stock_after(3, 4) == MERCER_REFUSED &&
              mercer_stock_after(3, 0) == MERCER_REFUSED &&
              mercer_stock_after(-1, 1) == MERCER_REFUSED &&
              mercer_stock_after(MERCER_STOCK_MAX + 1, 1) == MERCER_REFUSED,
          "mercer: stock after a sale, never below zero");
    CHECK(mercer_cost_plus(1000, 25, 100) == 1250 && mercer_cost_plus(1, 1, 2) == 2 &&
              mercer_cost_plus(1, 1, 3) == 1 && mercer_cost_plus(0, 1, 1) == 0 &&
              mercer_cost_plus(7, 99999, 99999) == 14 &&
              mercer_cost_plus(MERCER_COST_MAX, MERCER_RATE_MAX, 1) == INT64_C(99999999900000) &&
              mercer_cost_plus(1, 1, 0) == MERCER_REFUSED &&
              mercer_cost_plus(MERCER_COST_MAX + 1, 0, 1) == MERCER_REFUSED &&
              mercer_cost_plus(1, -1, 1) == MERCER_REFUSED &&
              mercer_cost_plus(1, MERCER_RATE_MAX + 1, 1) == MERCER_REFUSED,
          "mercer: price from cost and margin, rounded half up");
    CHECK(mercer_credit_ok(500, 1000, 0) && mercer_credit_ok(1, 0, 0) == true &&
              !mercer_credit_ok(500, 1000, -1) && !mercer_credit_ok(0, 1000, 500) &&
              !mercer_credit_ok(500, MERCER_AMOUNT_MAX + 1, 0) &&
              !mercer_credit_ok(500, 1000, MERCER_MINOR_MAX + 1),
          "mercer: a sale on credit inside the declared limit");
    {
        static const int64_t pr[4] = {1, 1, 2, 1}, pl[4] = {1, 1, 1, 2}, q[4] = {3, 4, 5, 6};
        CHECK(mercer_grid_cell(pr, pl, q, 4, 0) == 7 && mercer_grid_cell(pr, pl, q, 4, 2) == 5 &&
                  mercer_grid_cell(pr, pl, q, 4, 3) == 6 &&
                  mercer_grid_cell(pr, pl, q, 4, 4) == MERCER_REFUSED,
              "mercer: the stock grid");
    }
    CHECK(mercer_node_ok(9999, 999999, INT64_C(999999999999999), 999999999) &&
              !mercer_node_ok(10000, 1, 1, 1) && !mercer_node_ok(1, 1000000, 1, 1) &&
              !mercer_node_ok(1, 1, INT64_C(1000000000000000), 1) &&
              !mercer_node_ok(1, 1, 1, 1000000000),
          "mercer: a product node fits ^ZXMERCER");
    CHECK(mercer_sale_state(true, 3, 1500, 2, 1000) == LPRES_STATE_BOTH &&
              mercer_sale_state(true, 3, 1500, 3, 1500) == LPRES_STATE_TRUE &&
              mercer_sale_state(false, 3, 1500, 0, 0) == LPRES_STATE_TRUE,
          "mercer: the till and the receipt joined");

    /* scratch books: a counter and a customer (issuers) pay into the owner's cash */
    char root[256], scratch[512], path[COFFER_PATH_MAX + 1], file[600];
    bool saved = false;
    CHECK(zxf_root(root, sizeof root) == 0, "mercer: the test's folder");
    snprintf(scratch, sizeof scratch, "%s/mercer-scratch.books", root);
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file(scratch) == 0 && zxvd_rpc_books_open() == 0,
          "mercer: scratch books open");
    coffer_init(&MERC_BOOKS, &LINK, COFFER_DAY_CAP);
    CHECK(coffer_books(&MERC_BOOKS, path, sizeof path, &saved) == ZXM_OK && saved &&
              !strcmp(path, scratch),
          "mercer: every post goes to the scratch file: %s", path);
    uint32_t counter = 0, cash = 0, cust = 0, euro = 0;
    CHECK(coffer_enable(&MERC_BOOKS, "USD") == ZXM_OK &&
              coffer_enable(&MERC_BOOKS, "EUR") == ZXM_OK &&
              coffer_open_account(&MERC_BOOKS, "counter", "USD", true, &counter) == ZXM_OK &&
              coffer_open_account(&MERC_BOOKS, "cash", "USD", false, &cash) == ZXM_OK &&
              coffer_open_account(&MERC_BOOKS, "Ada", "USD", true, &cust) == ZXM_OK &&
              coffer_open_account(&MERC_BOOKS, "euro", "EUR", false, &euro) == ZXM_OK,
          "mercer: accounts: %s", LINK.err);

    /* nothing is asked before it is checked */
    mercer_t *m = &MERC_A;
    mercer_init(m, &LINK);
    uint64_t entry = 0;
    int64_t num = 0, den = 0;
    int before = sent;
    CHECK(mercer_set_till(m, counter, cash, 0, 1, true) == ZXM_E_STATE &&
              mercer_set_till(m, counter, cash, 2, 1, true) == ZXM_E_ARG &&
              mercer_ring(m, 101, 1, 0) == ZXM_E_ARG && mercer_post(m, true, NULL) == ZXM_E_ARG &&
              mercer_post(m, true, &entry) == ZXM_E_STATE &&
              mercer_receipt(m, 1, 1) == ZXM_E_STATE && mercer_rule(m, true, true) == ZXM_E_STATE &&
              mercer_save_receipt(m, "a/b.txt", true) == ZXM_E_ARG &&
              mercer_save_receipt(m, "r.txt", true) == ZXM_E_STATE &&
              mercer_print_receipt(m, "http://printer", true) == ZXM_E_ARG &&
              mercer_print_receipt(m, "ipp://printer/ipp/print", true) == ZXM_E_STATE &&
              mercer_save_catalogue(m, "../c", true) == ZXM_E_ARG &&
              mercer_list(m, 101, 0, 0) == ZXM_E_ARG &&
              mercer_unit_price(m, &num, &den) == ZXM_E_STATE &&
              mercer_set_market_limits(m, 0, 1, 1, true) == ZXM_E_ARG && sent == before,
          "mercer: bad input and wrong order refused before anything is asked");

    /* MM3: the till, on the owner's word */
    CHECK(mercer_load(m) == ZXM_OK && m->till.books.loaded, "mercer: accounts read: %s", LINK.err);
    CHECK(mercer_set_till(m, cash, counter, 0, 1, true) == ZXM_E_ARG &&
              mercer_set_till(m, counter, euro, 0, 1, true) == ZXM_E_ARG &&
              mercer_set_till(m, counter, counter, 0, 1, true) == ZXM_E_ARG,
          "mercer: the till's accounts checked");
    before = sent;
    CHECK(mercer_set_till(m, counter, cash, 0, 1, false) == ZXM_E_STATE && !m->till_set &&
              strstr(m->notice, "Waiting for your word") && sent == before,
          "mercer: the till is not set without the owner's word");
    CHECK(mercer_set_till(m, counter, cash, 0, 1, true) == ZXM_OK && m->till_set &&
              tallystick_set_last(&m->till, 87) == ZXM_OK,
          "mercer: till set; the last number given was 87");

    /* MM2: the catalogue */
    CHECK(mercer_add_product(m, 1, 101, "Bolt box", 1000, 25, 100, 10) == ZXM_OK &&
              mercer_add_product(m, 2, 202, "Widget", 400, 1, 4, 5) == ZXM_OK && m->np == 2 &&
              m->p[0].price == 1250 && m->p[0].factor_n == 5 && m->p[0].factor_d == 4 &&
              m->p[1].price == 500,
          "mercer: two products, priced from cost and margin");
    CHECK(mercer_add_product(m, 1, 101, "Again", 1, 0, 1, 1) == ZXM_E_ARG &&
              mercer_add_product(m, 0, 303, "No category", 1, 0, 1, 1) == ZXM_E_ARG &&
              mercer_add_product(m, 1, 303, "", 1, 0, 1, 1) == ZXM_E_ARG &&
              mercer_add_product(m, 1, 303, "A\nB", 1, 0, 1, 1) == ZXM_E_ARG &&
              mercer_add_product(m, 1, 303, "Dear", 9999999, 1, 1, 1) == ZXM_E_ARG &&
              mercer_add_product(m, 1, 303, "Cost", MERCER_COST_MAX + 1, 0, 1, 1) == ZXM_E_ARG &&
              mercer_add_product(m, 1, 303, "Margin", 1, 1, 0, 1) == ZXM_E_ARG &&
              mercer_add_product(m, 1, 303, "Stock", 1, 0, 1, -1) == ZXM_E_ARG && m->np == 2,
          "mercer: a product that does not fit is refused: %s", m->notice);

    /* MM7: the catalogue in the Hoard, saved only on the owner's word */
    before = sent;
    CHECK(mercer_save_catalogue(m, "mercer-catalogue.txt", false) == ZXM_E_STATE && sent == before,
          "mercer: no word, no file");
    CHECK(mercer_save_catalogue(m, "mercer-catalogue.txt", true) == ZXM_OK,
          "mercer: catalogue saved: %s", LINK.err);
    mercer_init(&MERC_B, &LINK);
    CHECK(mercer_load_catalogue(&MERC_B, "mercer-catalogue.txt") == ZXM_OK && MERC_B.np == 2 &&
              MERC_B.p[0].code == 101 && MERC_B.p[0].price == 1250 &&
              !strcmp(MERC_B.p[1].name, "Widget") && MERC_B.p[1].stock == 5 &&
              MERC_B.p[1].factor_n == 5 && MERC_B.p[1].factor_d == 4,
          "mercer: catalogue read back: %s", LINK.err);
    MERC_B.link = &FAKE;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"text\":\"mercer catalogue\\n1 101 "
             "1000 25 100 010 Bolt box\\n\",\"eof\":true}}";
    CHECK(mercer_load_catalogue(&MERC_B, "c.txt") == ZXM_E_PROTOCOL && MERC_B.np == 2,
          "mercer: a leading zero is refused, nothing kept");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"text\":\"mercer catalogue\\n1 101 "
             "1000 25 0 10 Bolt box\\n\",\"eof\":true}}";
    CHECK(mercer_load_catalogue(&MERC_B, "c.txt") == ZXM_E_PROTOCOL && MERC_B.np == 2,
          "mercer: a margin over 0 is refused");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"text\":\"mercer catalogue\\n1 101 "
             "1000 25 100 10 A\\n1 101 1 0 1 1 B\\n\",\"eof\":true}}";
    CHECK(mercer_load_catalogue(&MERC_B, "c.txt") == ZXM_E_PROTOCOL && MERC_B.np == 2,
          "mercer: two products with one code are refused");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"text\":\"mercer catalogue\\n\","
             "\"eof\":false}}";
    CHECK(mercer_load_catalogue(&MERC_B, "c.txt") == ZXM_E_PROTOCOL && MERC_B.np == 2,
          "mercer: a catalogue read only in part is refused");
    MERC_B.link = &LINK;

    /* MM4, MM5, the BOTH test: sale 88 rung as 3 Widgets for 1500, the
     * buyer's receipt says 2 for 1000: held, not posted, the books balanced */
    uint32_t cash_before = mercer_entries_of(cash);
    CHECK(mercer_ring(m, 202, 6, 0) == ZXM_E_ARG && mercer_ring(m, 202, 0, 0) == ZXM_E_ARG &&
              mercer_ring(m, 202, 1, cust) == ZXM_E_ARG,
          "mercer: more than the stock, none, or credit with no limit: refused");
    before = sent;
    CHECK(mercer_ring(m, 202, 3, 0) == ZXM_OK && m->sale.open && m->sale.number == 88 &&
              m->sale.amount == 1500 && m->sale.state == LPRES_STATE_TRUE && sent == before &&
              mercer_ring(m, 101, 1, 0) == ZXM_E_STATE,
          "mercer: sale 88 rung up, nothing sent: %s", m->notice);
    CHECK(mercer_receipt(m, 2, 1000) == ZXM_OK && m->sale.state == LPRES_STATE_BOTH &&
              strstr(m->notice, "Sale 88: the till says 3 units for 1500, the receipt 2 for 1000"),
          "mercer: the receipt disagrees: BOTH: %s", m->notice);
    before = sent;
    CHECK(mercer_post(m, true, &entry) == ZXM_E_STATE && sent == before && m->p[1].stock == 5 &&
              strstr(m->notice, "Held"),
          "mercer: BOTH is never posted, even on the owner's word");
    CHECK(mercer_entries_of(cash) == cash_before && coffer_check(&MERC_BOOKS) == ZXM_OK &&
              MERC_BOOKS.books_ok,
          "mercer: nothing in the books, and they balance (%u entries)", cash_before);
    before = sent;
    CHECK(mercer_rule(m, true, false) == ZXM_E_STATE && m->sale.state == LPRES_STATE_BOTH &&
              sent == before,
          "mercer: a ruling waits for the owner's word");
    CHECK(mercer_rule(m, true, true) == ZXM_OK && m->sale.state == LPRES_STATE_TRUE &&
              m->sale.ruled && mercer_rule(m, true, true) == ZXM_E_STATE,
          "mercer: the owner rules the till's record stands");

    /* MM6: posted on the owner's word, read back, paid in full, a receipt */
    before = sent;
    CHECK(mercer_post(m, false, &entry) == ZXM_E_STATE && sent == before && m->sale.open &&
              m->p[1].stock == 5,
          "mercer: no word, nothing posted");
    CHECK(mercer_post(m, true, &entry) == ZXM_OK && entry > 0 && !m->sale.open &&
              m->p[1].stock == 2 && m->till.inv.paid_full && m->till.inv.receipt == 89 &&
              m->till.doc == TSTK_DOC_RECEIPT && strstr(m->till.text, "Receipt 89\nFor invoice 88"),
          "mercer: sale 88 posted, receipt 89: %s %s", m->notice, LINK.err);
    CHECK(mercer_entries_of(cash) == cash_before + 1u && MERC_BOOKS.e[cash_before].minor == 1500 &&
              coffer_check(&MERC_BOOKS) == ZXM_OK && MERC_BOOKS.books_ok,
          "mercer: one entry of 1500 in cash, the books balance");

    /* MM7: the receipt saved and printed on the owner's word */
    before = sent;
    CHECK(mercer_save_receipt(m, "mercer-rcpt-89.txt", false) == ZXM_E_STATE && sent == before,
          "mercer: no word, no receipt file");
    CHECK(mercer_save_receipt(m, "mercer-rcpt-89.txt", true) == ZXM_OK &&
              !strcmp(m->till.saved, "mercer-rcpt-89.txt"),
          "mercer: receipt saved: %s", LINK.err);
    snprintf(file, sizeof file, "%s/mercer-rcpt-89.txt", root);
    FILE *f = fopen(file, "rb");
    long size = -1;
    if (f) {
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fclose(f);
    }
    CHECK(size == (long) m->till.text_len, "mercer: the file is the receipt: %ld", size);
    zxm_link_t *real = m->till.link;
    m->till.link = &FAKE;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"job_id\":5,\"state\":\"pending\"}}";
    CHECK(mercer_print_receipt(m, "ipp://printer/ipp/print", false) == ZXM_E_STATE &&
              m->till.job == -1 &&
              mercer_print_receipt(m, "ipp://printer/ipp/print", true) == ZXM_OK &&
              m->till.job == 5 && strstr(FAKE.req, "\"path\":\"mercer-rcpt-89.txt\""),
          "mercer: printed on the owner's word: job %lld", (long long) m->till.job);
    m->till.link = real;

    /* a receipt that agrees; then one that does not, voided by the owner */
    CHECK(mercer_ring(m, 101, 1, 0) == ZXM_OK && m->sale.number == 90 &&
              mercer_receipt(m, 1, 1250) == ZXM_OK && m->sale.state == LPRES_STATE_TRUE &&
              mercer_post(m, true, &entry) == ZXM_OK && m->p[0].stock == 9 &&
              m->till.inv.receipt == 91,
          "mercer: sale 90 agrees and is posted: %s", m->notice);
    CHECK(mercer_unit_price(m, &num, &den) == ZXM_OK && num == 1375 && den == 2,
          "mercer: price per unit across the day: %lld/%lld", (long long) num, (long long) den);
    before = sent;
    CHECK(mercer_ring(m, 101, 2, 0) == ZXM_OK && mercer_receipt(m, 1, 1250) == ZXM_OK &&
              m->sale.state == LPRES_STATE_BOTH && mercer_rule(m, false, true) == ZXM_OK &&
              !m->sale.open && m->sale.state == LPRES_STATE_FALSE && m->p[0].stock == 9 &&
              sent == before && mercer_post(m, true, &entry) == ZXM_E_STATE,
          "mercer: voided: nothing posted, the stock as it was");

    /* MR3: a sale on credit, inside the declared limit */
    before = sent;
    CHECK(mercer_set_credit(m, cash, 2000, true) == ZXM_E_ARG &&
              mercer_set_credit(m, cust, -1, true) == ZXM_E_ARG &&
              mercer_set_credit(m, cust, 2000, false) == ZXM_E_STATE && m->nc == 0 &&
              sent == before && mercer_set_credit(m, cust, 2000, true) == ZXM_OK && m->nc == 1,
          "mercer: a credit limit on the owner's word: %s", m->notice);
    CHECK(mercer_ring(m, 202, 2, cust) == ZXM_OK && m->sale.customer == 0 &&
              mercer_post(m, true, &entry) == ZXM_OK && entry == 0 && m->c[0].owed == 1000 &&
              m->p[1].stock == 0 && mercer_entries_of(cash) == cash_before + 2u,
          "mercer: 1000 on the tab, no money moved: %s", m->notice);
    CHECK(mercer_ring(m, 101, 1, cust) == ZXM_E_ARG && strstr(m->notice, "declared limit") &&
              mercer_ring(m, 202, 1, 0) == ZXM_E_ARG &&
              mercer_set_credit(m, cust, 999, true) == ZXM_E_ARG,
          "mercer: past the limit, out of stock, a limit under the tab: refused");
    CHECK(mercer_unit_price(m, &num, &den) == ZXM_OK && num == 625 && den == 1,
          "mercer: the day's sales, 3750 for 6 units");
    before = sent;
    CHECK(mercer_credit_payment(m, cust, 1001, true, &entry) == ZXM_E_ARG &&
              mercer_credit_payment(m, cust, 400, false, &entry) == ZXM_E_STATE && sent == before &&
              m->c[0].owed == 1000,
          "mercer: a payment against the tab waits for the owner's word");
    CHECK(mercer_credit_payment(m, cust, 400, true, &entry) == ZXM_OK && entry > 0 &&
              m->c[0].owed == 600 && mercer_entries_of(cash) == cash_before + 3u &&
              MERC_BOOKS.e[cash_before + 2u].minor == 400 && coffer_check(&MERC_BOOKS) == ZXM_OK &&
              MERC_BOOKS.books_ok,
          "mercer: 400 paid against the tab, 600 owed: %s %s", m->notice, LINK.err);

    /* MM9: a stocktake by product and place */
    {
        static const int64_t pr[3] = {101, 101, 202}, pl[3] = {1, 2, 1}, q[3] = {4, 5, 0},
                             q2[3] = {4, 4, 0}, bad[3] = {101, 999, 202};
        int64_t cell[3];
        uint32_t differ = 9;
        CHECK(mercer_stocktake(m, pr, pl, q, 3, cell, &differ) == ZXM_OK && cell[0] == 4 &&
                  cell[1] == 5 && cell[2] == 0 && differ == 0,
              "mercer: the count matches the stock");
        CHECK(mercer_stocktake(m, pr, pl, q2, 3, cell, &differ) == ZXM_OK && differ == 1 &&
                  mercer_stocktake(m, bad, pl, q, 3, cell, &differ) == ZXM_E_ARG,
              "mercer: one product counted short; an unknown product refused");
    }

    /* MM8: listings, shown and never sent */
    before = sent;
    CHECK(mercer_list(m, 101, 0, 0) == ZXM_E_STATE &&
              mercer_set_market_limits(m, 100000, 100, 100, false) == ZXM_E_STATE &&
              sent == before && mercer_set_market_limits(m, 100000, 100, 100, true) == ZXM_OK &&
              mercer_list(m, 101, 0, 1) == ZXM_E_ARG && mercer_list(m, 202, 0, 0) == ZXM_E_STATE &&
              mercer_list(m, 101, 10873, 10000) == ZXM_OK && sent == before,
          "mercer: listed under the owner's limits: %s", m->notice);
    CHECK(!strcmp(m->listing, "Bolt box: 1250 a unit (cost x 5/4), 9 in stock. Assurance fee 1. "
                              "At 10873/10000: 1359. Tiers give no priority, discount or credit."),
          "mercer: the listing: %s", m->listing);
    m->market.link = &FAKE;
    m->card.link = &FAKE;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"share\":{\"compute\":\"10\","
             "\"storage\":\"0\"},\"wants\":[{\"slot\":0,\"resource\":\"compute\","
             "\"units_left\":\"4\",\"max_price\":\"300\"}],\"outstanding\":[]}}";
    CHECK(mercer_read_offers(m) == ZXM_OK && m->market.nwant == 1, "mercer: the market's offers");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"tier\":0,\"metal\":\"Copper\","
             "\"stone\":\"Obsidian\",\"inputs\":{\"credibility\":{\"value\":\"50\",\"source\":"
             "\"trades\"},\"bond\":{\"value\":\"0\",\"source\":\"none\"},\"uptime_bps\":{"
             "\"value\":\"0\",\"source\":\"none\"},\"giving\":{\"value\":\"0\",\"source\":"
             "\"none\"}},\"next\":{\"tier\":1,\"metal\":\"Bronze\",\"stone\":\"Carnelian\","
             "\"credibility\":\"100\",\"bond\":\"1000\",\"uptime_bps\":\"9000\",\"missing\":["
             "\"bond\",\"uptime_bps\"]},\"perks\":[]}}";
    CHECK(mercer_read_card(m, 20000) == ZXM_OK && m->card.have && !m->card.next.earned,
          "mercer: the seller's card: %s", m->card.notice);
    m->market.link = &LINK;
    m->card.link = &LINK;
    CHECK(mercer_list(m, 101, 0, 0) == ZXM_OK &&
              !strcmp(m->listing, "Bolt box: 1250 a unit (cost x 5/4), 9 in stock. Assurance fee "
                                  "1. Market: 1 wants, 0 offers outstanding. Seller: Copper / "
                                  "Obsidian, next tier not earned yet. Tiers give no priority, "
                                  "discount or credit."),
          "mercer: the listing with the market and the card: %s", m->listing);

    /* back to books in memory: the scratch file and documents removed */
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file("") == 0 && remove(scratch) == 0, "mercer: scratch books removed");
    snprintf(file, sizeof file, "%s/mercer-rcpt-89.txt", root);
    CHECK(remove(file) == 0, "mercer: receipt removed");
    snprintf(file, sizeof file, "%s/mercer-catalogue.txt", root);
    CHECK(remove(file) == 0, "mercer: catalogue removed");
}
