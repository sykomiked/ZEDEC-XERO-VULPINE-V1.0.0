/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Tallystick's model against zxvd's real dispatcher (LINK). The books are
 * a scratch file in the test's own folder, never real books: accounts are
 * opened through Coffer's model, payments posted through Tallystick's, and
 * the books closed again (in memory) at the end, the saved documents
 * removed. */
#include "tallystick_model.h"

static tallystick_t TSTK_A;
static coffer_t TSTK_BOOKS;

APPS_TEST(tallystick)
{
    /* the rules */
    CHECK(tstk_line_total(0, 0, 0, 1) == 0 && tstk_line_total(1, 1000, 1, 10) == 1100 &&
              tstk_line_total(3, 1999, 2, 25) == 6477 && tstk_line_total(1, 1, 1, 2) == 2 &&
              tstk_line_total(1, 1, 49999, 99999) == 1 && tstk_line_total(2, 5, 1, 4) == 13 &&
              tstk_line_total(TSTK_QTY_MAX, TSTK_PRICE_MAX, 1, 1) == 19999978000002 &&
              tstk_line_total(TSTK_QTY_MAX + 1, 1, 0, 1) == TSTK_REFUSED &&
              tstk_line_total(1, TSTK_PRICE_MAX + 1, 0, 1) == TSTK_REFUSED &&
              tstk_line_total(-1, 1, 0, 1) == TSTK_REFUSED &&
              tstk_line_total(1, -1, 0, 1) == TSTK_REFUSED &&
              tstk_line_total(1, 1, 2, 1) == TSTK_REFUSED &&
              tstk_line_total(1, 1, -1, 1) == TSTK_REFUSED &&
              tstk_line_total(1, 1, 0, 0) == TSTK_REFUSED &&
              tstk_line_total(1, 1, 0, TSTK_RATE_MAX + 1) == TSTK_REFUSED,
          "taxed line: qty * price plus its tax rounded half up, and its edges");
    CHECK(tstk_paid_in_full(0, 0) && tstk_paid_in_full(100, 5) &&
              tstk_paid_in_full(TSTK_TOTAL_MAX, TSTK_OVER_MAX) && !tstk_paid_in_full(100, -1) &&
              !tstk_paid_in_full(-1, 0) && !tstk_paid_in_full(TSTK_TOTAL_MAX + 1, 0) &&
              !tstk_paid_in_full(100, TSTK_OVER_MAX + 1),
          "paid in full: the margin at least 0, each field in range");
    CHECK(tstk_next_number(0) == 1 && tstk_next_number(41) == 42 &&
              tstk_next_number(TSTK_NUMBER_MAX - 1) == TSTK_NUMBER_MAX &&
              tstk_next_number(TSTK_NUMBER_MAX) == TSTK_REFUSED &&
              tstk_next_number(-1) == TSTK_REFUSED,
          "next number: nine digits, never wrapped");

    /* scratch books: a customer (issuer) pays into the owner's cash (holder) */
    char root[256], scratch[512], path[COFFER_PATH_MAX + 1], file[600];
    bool saved = false;
    CHECK(zxf_root(root, sizeof root) == 0, "the test's folder");
    snprintf(scratch, sizeof scratch, "%s/tallystick-scratch.books", root);
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file(scratch) == 0 && zxvd_rpc_books_open() == 0, "scratch books open");
    coffer_init(&TSTK_BOOKS, &LINK, COFFER_DAY_CAP);
    CHECK(coffer_books(&TSTK_BOOKS, path, sizeof path, &saved) == ZXM_OK && saved &&
              !strcmp(path, scratch),
          "every post goes to the scratch file: %s", path);
    uint32_t customer = 0, cash = 0, other = 0, euro = 0;
    CHECK(coffer_enable(&TSTK_BOOKS, "USD") == ZXM_OK &&
              coffer_enable(&TSTK_BOOKS, "EUR") == ZXM_OK &&
              coffer_open_account(&TSTK_BOOKS, "customer", "USD", true, &customer) == ZXM_OK &&
              coffer_open_account(&TSTK_BOOKS, "cash", "USD", false, &cash) == ZXM_OK &&
              coffer_open_account(&TSTK_BOOKS, "other", "USD", false, &other) == ZXM_OK &&
              coffer_open_account(&TSTK_BOOKS, "euro", "EUR", false, &euro) == ZXM_OK,
          "accounts: %s", LINK.err);

    /* nothing is asked before it is checked */
    tallystick_init(&TSTK_A, &LINK);
    int before = sent;
    uint64_t entry = 0;
    CHECK(TSTK_A.job == -1 && tallystick_draft(&TSTK_A, "Acme", customer, cash) == ZXM_E_STATE &&
              tallystick_add_line(&TSTK_A, "x", 1, 1, 0, 1) == ZXM_E_STATE &&
              tallystick_issue(&TSTK_A) == ZXM_E_STATE &&
              tallystick_propose_payment(&TSTK_A, 5) == ZXM_E_STATE &&
              tallystick_confirm_payment(&TSTK_A, &entry) == ZXM_E_STATE &&
              tallystick_receipt(&TSTK_A) == ZXM_E_STATE &&
              tallystick_save(&TSTK_A, "a/b.txt") == ZXM_E_ARG &&
              tallystick_save(&TSTK_A, ".hidden") == ZXM_E_ARG &&
              tallystick_save(&TSTK_A, "inv.txt") == ZXM_E_STATE &&
              tallystick_print(&TSTK_A, "http://printer") == ZXM_E_ARG &&
              tallystick_print(&TSTK_A, "ipp://printer/ipp/print") == ZXM_E_STATE &&
              tallystick_set_last(&TSTK_A, TSTK_NUMBER_MAX) == ZXM_E_ARG &&
              tallystick_set_last(&TSTK_A, -1) == ZXM_E_ARG &&
              tallystick_save_register(&TSTK_A, "../r") == ZXM_E_ARG &&
              tallystick_statement(&TSTK_A, cash, 5, 4, "s-1", "2026-10-22T09:00:00") ==
                  ZXM_E_ARG &&
              sent == before,
          "bad input and wrong order refused before anything is asked");

    /* a draft: the customer pays from an issuer account into a holder account */
    CHECK(tallystick_load(&TSTK_A) == ZXM_OK && TSTK_A.books.loaded, "accounts read: %s", LINK.err);
    CHECK(tallystick_draft(&TSTK_A, "Acme", cash, customer) == ZXM_E_ARG &&
              tallystick_draft(&TSTK_A, "Acme", customer, euro) == ZXM_E_ARG &&
              tallystick_draft(&TSTK_A, "Acme", customer, customer) == ZXM_E_ARG &&
              tallystick_draft(&TSTK_A, "Acme", customer, 999) == ZXM_E_ARG &&
              tallystick_draft(&TSTK_A, "", customer, cash) == ZXM_E_ARG &&
              tallystick_draft(&TSTK_A, "A\nB", customer, cash) == ZXM_E_ARG,
          "a draft's accounts and party checked");
    CHECK(tallystick_set_last(&TSTK_A, 41) == ZXM_OK && tallystick_last(&TSTK_A) == 41 &&
              tallystick_draft(&TSTK_A, "Acme Hardware", customer, cash) == ZXM_OK &&
              TSTK_A.inv.open && TSTK_A.inv.state == TSTK_DRAFT && !strcmp(TSTK_A.inv.ccy, "USD") &&
              tallystick_issue(&TSTK_A) == ZXM_E_STATE,
          "a draft with no lines is not issued");
    CHECK(tallystick_add_line(&TSTK_A, "Bolts", 3, 1999, 2, 25) == ZXM_OK &&
              tallystick_add_line(&TSTK_A, "Labour", 1, 1000, 1, 10) == ZXM_OK &&
              tallystick_add_line(&TSTK_A, "Washer", 2, 5, 1, 4) == ZXM_OK &&
              tallystick_add_line(&TSTK_A, "Too many", TSTK_QTY_MAX + 1, 1, 0, 1) == ZXM_E_ARG &&
              tallystick_add_line(&TSTK_A, "Rate", 1, 1, 3, 2) == ZXM_E_ARG && TSTK_A.inv.n == 3 &&
              TSTK_A.inv.line[0].net == 5997 && TSTK_A.inv.line[0].tax == 480 &&
              TSTK_A.inv.line[0].total == 6477 && TSTK_A.inv.line[2].tax == 3,
          "three lines, two refused: %s", TSTK_A.notice);

    /* TS1, TS2: issued with the next number, its total the sum of its lines */
    CHECK(tallystick_issue(&TSTK_A) == ZXM_OK && TSTK_A.inv.state == TSTK_ISSUED &&
              TSTK_A.inv.number == 42 && TSTK_A.inv.net == 7007 && TSTK_A.inv.tax == 583 &&
              TSTK_A.inv.total == 7590 && tallystick_last(&TSTK_A) == 42 && TSTK_A.nreg == 1 &&
              tallystick_register_ok(&TSTK_A) && TSTK_A.doc == TSTK_DOC_INVOICE &&
              strstr(TSTK_A.text, "Invoice 42\nTo: Acme Hardware") &&
              strstr(TSTK_A.text, "Bolts: 3 x 1999 = 5997, tax 2/25: 480, line total 6477") &&
              strstr(TSTK_A.text, "Total due: 7590"),
          "issued: %s", TSTK_A.text);
    CHECK(tallystick_add_line(&TSTK_A, "More", 1, 1, 0, 1) == ZXM_E_STATE && TSTK_A.inv.n == 3 &&
              tallystick_issue(&TSTK_A) == ZXM_E_STATE &&
              tallystick_set_last(&TSTK_A, 7) == ZXM_E_STATE &&
              tallystick_receipt(&TSTK_A) == ZXM_E_STATE,
          "an issued invoice is never changed, the sequence never restarted");
    TSTK_A.prev[0] = 40;
    CHECK(!tallystick_register_ok(&TSTK_A), "a broken chain is seen (witness.chain_ok)");
    TSTK_A.prev[0] = 41;

    /* TS7: saved as a new file, printed */
    CHECK(tallystick_save(&TSTK_A, "tallystick-inv-42.txt") == ZXM_OK &&
              !strcmp(TSTK_A.saved, "tallystick-inv-42.txt"),
          "invoice saved: %s", LINK.err);
    snprintf(file, sizeof file, "%s/tallystick-inv-42.txt", root);
    FILE *f = fopen(file, "rb");
    long size = -1;
    if (f) {
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fclose(f);
    }
    CHECK(size == (long) TSTK_A.text_len, "the file is the invoice: %ld of %u", size,
          TSTK_A.text_len);
    CHECK(tallystick_save(&TSTK_A, "tallystick-inv-42.txt") == ZXM_E_REMOTE &&
              strstr(TSTK_A.notice, "never replaces"),
          "never over an earlier invoice: %s", LINK.err);

    /* TS2: the register kept in the Hoard and read back */
    CHECK(tallystick_save_register(&TSTK_A, "tallystick-register.txt") == ZXM_OK &&
              tallystick_load_register(&TSTK_A, "tallystick-register.txt") == ZXM_E_STATE,
          "register saved: %s", LINK.err);

    /* TS3: a payment waits for the owner */
    before = sent;
    CHECK(tallystick_propose_payment(&TSTK_A, 0) == ZXM_E_ARG &&
              tallystick_propose_payment(&TSTK_A, 7591) == ZXM_E_ARG &&
              tallystick_propose_payment(&TSTK_A, 5000) == ZXM_OK && TSTK_A.books.held.pending &&
              TSTK_A.books.held.amount == 5000 && !strcmp(TSTK_A.books.held.ref, "INV-42 P1") &&
              strstr(TSTK_A.notice, "Waiting for your confirmation") && sent == before,
          "a payment proposed, nothing sent: %s", TSTK_A.notice);
    CHECK(tallystick_propose_payment(&TSTK_A, 10) == ZXM_E_STATE &&
              tallystick_draft(&TSTK_A, "B", customer, cash) == ZXM_E_STATE,
          "one payment waits at a time");
    CHECK(tallystick_cancel_payment(&TSTK_A) == ZXM_OK && !TSTK_A.books.held.pending &&
              tallystick_cancel_payment(&TSTK_A) == ZXM_E_STATE && sent == before,
          "cancelled: nothing posted");
    CHECK(tallystick_propose_payment(&TSTK_A, 5000) == ZXM_OK &&
              tallystick_confirm_payment(&TSTK_A, NULL) == ZXM_E_ARG &&
              tallystick_confirm_payment(&TSTK_A, &entry) == ZXM_OK && TSTK_A.inv.paid == 5000 &&
              TSTK_A.inv.payments == 1 && !TSTK_A.inv.paid_full &&
              strstr(TSTK_A.notice, "still owed"),
          "first payment posted and read back: %s %s", TSTK_A.notice, LINK.err);
    CHECK(tallystick_receipt(&TSTK_A) == ZXM_E_STATE && strstr(TSTK_A.notice, "paid in full"),
          "no receipt while owed");
    /* another posting into cash that is not this invoice's is not counted */
    CHECK(coffer_load(&TSTK_BOOKS) == ZXM_OK &&
              coffer_propose(&TSTK_BOOKS, customer, cash, 77, "INV-4 P1") == ZXM_OK &&
              coffer_confirm(&TSTK_BOOKS, &entry) == ZXM_OK &&
              coffer_propose(&TSTK_BOOKS, customer, cash, 88, "INV-421 P1") == ZXM_OK &&
              coffer_confirm(&TSTK_BOOKS, &entry) == ZXM_OK,
          "other postings: %s", LINK.err);
    CHECK(tallystick_reconcile(&TSTK_A) == ZXM_OK && TSTK_A.inv.paid == 5000 &&
              TSTK_A.inv.payments == 1,
          "only this invoice's payments: %lld", (long long) TSTK_A.inv.paid);
    CHECK(tallystick_propose_payment(&TSTK_A, 2591) == ZXM_E_ARG &&
              tallystick_propose_payment(&TSTK_A, 2590) == ZXM_OK &&
              !strcmp(TSTK_A.books.held.ref, "INV-42 P2") &&
              tallystick_confirm_payment(&TSTK_A, &entry) == ZXM_OK && TSTK_A.inv.paid == 7590 &&
              TSTK_A.inv.payments == 2 && TSTK_A.inv.paid_full &&
              strstr(TSTK_A.notice, "Paid in full"),
          "paid in full: %s", TSTK_A.notice);
    CHECK(tallystick_propose_payment(&TSTK_A, 1) == ZXM_E_STATE, "nothing more is taken");

    /* TS5: the receipt, numbered in the same sequence */
    CHECK(tallystick_receipt(&TSTK_A) == ZXM_OK && TSTK_A.inv.receipt == 43 &&
              tallystick_last(&TSTK_A) == 43 && TSTK_A.nreg == 2 &&
              tallystick_register_ok(&TSTK_A) && TSTK_A.doc == TSTK_DOC_RECEIPT &&
              strstr(TSTK_A.text, "Receipt 43\nFor invoice 42") &&
              strstr(TSTK_A.text, "Received in 2 payments: 7590") && TSTK_A.saved[0] == 0,
          "receipt: %s", TSTK_A.text);
    CHECK(tallystick_receipt(&TSTK_A) == ZXM_OK && TSTK_A.inv.receipt == 43 && TSTK_A.nreg == 2,
          "made again: the same number");
    CHECK(tallystick_save(&TSTK_A, "tallystick-rcpt-43.txt") == ZXM_OK, "receipt saved: %s",
          LINK.err);

    /* the print, and the register read back by a fresh model */
    zxm_link_t *real = TSTK_A.link;
    TSTK_A.link = &FAKE;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"job_id\":7,\"state\":\"pending\"}}";
    CHECK(tallystick_print(&TSTK_A, "ipp://printer/ipp/print") == ZXM_OK && TSTK_A.job == 7 &&
              strstr(FAKE.req, "\"print.submit\"") &&
              strstr(FAKE.req, "\"path\":\"tallystick-rcpt-43.txt\""),
          "print.submit: job %lld", (long long) TSTK_A.job);
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"state\":\"pending\"}}";
    CHECK(tallystick_print(&TSTK_A, "ipp://printer/ipp/print") == ZXM_E_PROTOCOL && TSTK_A.job == 7,
          "a job without its id");
    TSTK_A.link = real;
    CHECK(tallystick_save_register(&TSTK_A, "tallystick-register.txt") == ZXM_OK,
          "register replaced");
    static tallystick_t TSTK_B;
    tallystick_init(&TSTK_B, &LINK);
    CHECK(tallystick_load_register(&TSTK_B, "tallystick-register.txt") == ZXM_OK &&
              tallystick_last(&TSTK_B) == 43,
          "register read back: %lld %s", (long long) tallystick_last(&TSTK_B), LINK.err);
    TSTK_B.link = &FAKE;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"text\":\"tallystick register\\nlast "
             "999999999\\n\",\"eof\":true}}";
    CHECK(tallystick_load_register(&TSTK_B, "r.txt") == ZXM_E_STATE &&
              tallystick_last(&TSTK_B) == 43,
          "a register at its last number");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"text\":\"tallystick register\\nlast "
             "042\\n\",\"eof\":true}}";
    CHECK(tallystick_load_register(&TSTK_B, "r.txt") == ZXM_E_PROTOCOL, "a leading zero");
    CANNED =
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"text\":\"something else\",\"eof\":true}}";
    CHECK(tallystick_load_register(&TSTK_B, "r.txt") == ZXM_E_PROTOCOL &&
              tallystick_last(&TSTK_B) == 43,
          "not a register");
    TSTK_B.link = &LINK;

    /* TS6: a statement of cash through Reckoner. This harness does not
     * link zxvd's payments group, so iso20022.render is not found: the
     * statement is read and closed, and the render is refused there. */
    CHECK(coffer_load(&TSTK_BOOKS) == ZXM_OK && coffer_entries(&TSTK_BOOKS, cash) == ZXM_OK &&
              TSTK_BOOKS.ne == 4,
          "cash's journal: %u", TSTK_BOOKS.ne);
    uint64_t first = TSTK_BOOKS.e[0].id, lastid = TSTK_BOOKS.e[3].id;
    CHECK(tallystick_statement(&TSTK_A, cash, first, lastid, "stmt-1", "2026-10-22T09:00:00") ==
                  ZXM_E_REMOTE &&
              TSTK_A.stmt.st.ready && TSTK_A.stmt.st.closing == 7755 &&
              TSTK_A.stmt.st.credits == 7755 && TSTK_A.stmt.closed && TSTK_A.stmt.books_ok,
          "statement read: %lld %s", (long long) TSTK_A.stmt.st.closing, LINK.err);
    TSTK_A.stmt.link = &FAKE;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"ok\":true,\"kind\":\"camt053\","
             "\"xml\":\"<Document><BkToCstmrStmt><GrpHdr><MsgId>stmt-1</MsgId></GrpHdr>"
             "</BkToCstmrStmt></Document>\\n\"}}";
    CHECK(reckoner_render(&TSTK_A.stmt, "stmt-1", "2026-10-22T09:00:00") == ZXM_OK &&
              TSTK_A.stmt.xml_len > 0 && strstr(FAKE.req, "\"closing\":\"7755\""),
          "rendered as camt.053: %s", FAKE.req);
    TSTK_A.stmt.link = real;
    CHECK(tallystick_statement(&TSTK_A, cash, first, first, "stmt-2", "2026-10-22T09:00:00") ==
                  ZXM_E_REMOTE &&
              TSTK_A.stmt.st.closing == 5000 && TSTK_A.stmt.from == first &&
              TSTK_A.stmt.to == first,
          "a second statement reopens the first period: %lld", (long long) TSTK_A.stmt.st.closing);

    /* back to books in memory: the scratch file and documents removed */
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file("") == 0 && remove(scratch) == 0, "scratch books removed");
    snprintf(file, sizeof file, "%s/tallystick-inv-42.txt", root);
    CHECK(remove(file) == 0, "invoice removed");
    snprintf(file, sizeof file, "%s/tallystick-rcpt-43.txt", root);
    CHECK(remove(file) == 0, "receipt removed");
    snprintf(file, sizeof file, "%s/tallystick-register.txt", root);
    CHECK(remove(file) == 0, "register removed");
}
