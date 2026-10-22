/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Neap's model against zxvd's real dispatcher, on scratch books only: a
 * books file in the test's own folder, never real books, closed again (in
 * memory) at the end and the saved report removed. This harness does not
 * link zxvd's payments group (zxvd_pay.c), so ledger.hold, ledger.holds,
 * ledger.release and iso20022.render are answered on a link of its own
 * (NEAP_LINK): a hold is one real ledger.post to an "escrow: USD" holder
 * account opened on the first hold, as zxvd_pay.c P5 posts it, and a
 * release one real ledger.post on to the payee, so the balances Neap reads
 * back are the books' own. print.submit is answered there too (no
 * printer); every other request goes to the real dispatcher. */
#include "neap_model.h"

static neap_t NP;
static zxm_link_t NEAP_LINK;
static int64_t NEAP_ESCROW = -1; /* the escrow account, once opened */
static int64_t NEAP_HELD[4][4];  /* hold, from, to, amount */
static int64_t NEAP_STATE[4];    /* 1 held, 2 released */
static uint32_t NEAP_NH;
static bool NEAP_OFF;                   /* answer -32012, the escrow switch off */
static char NEAP_LAST[ZXM_REQ_MAX + 1]; /* the last request NEAP_LINK answered itself */

/* "key":123 or "key":"123" in s */
static int64_t neap_num(const char *s, const char *key)
{
    char k[40];
    snprintf(k, sizeof k, "\"%s\":", key);
    const char *p = strstr(s, k);
    if (!p) return -1;
    p += strlen(k);
    if (*p == '"') p++;
    return strtoll(p, NULL, 10);
}

/* one request to the real dispatcher; its response line into r */
static bool neap_real(const char *method, const char *params, char *r, uint32_t cap)
{
    char q[1024];
    int len =
        snprintf(q, sizeof q, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":%s}",
                 method, params);
    int32_t got = inproc(NULL, q, (uint32_t) len, r, cap - 1u);
    if (got < 0) return false;
    r[got < (int32_t) cap ? got : (int32_t) cap - 1] = 0;
    return strstr(r, "\"result\"") != NULL;
}

static bool neap_post(int64_t from, int64_t to, int64_t amount, const char *ref, int64_t *entry)
{
    char p[256], r[512];
    snprintf(p, sizeof p,
             "{\"from\":%lld,\"to\":%lld,\"amount_minor\":%lld,\"currency\":\"USD\","
             "\"reference\":\"%s\"}",
             (long long) from, (long long) to, (long long) amount, ref);
    if (!neap_real("ledger.post", p, r, sizeof r)) return false;
    *entry = neap_num(r, "entry_id");
    return *entry >= 0;
}

static int neap_hold_json(char *o, size_t cap, uint32_t i, int64_t closed)
{
    char c[24] = "null";
    if (closed >= 0) snprintf(c, sizeof c, "\"%lld\"", (long long) closed);
    return snprintf(o, cap,
                    "{\"hold\":%lld,\"state\":\"%s\",\"from\":%lld,\"to\":%lld,\"amount_minor\":"
                    "\"%lld\",\"currency\":\"USD\",\"escrow_account\":%lld,\"entry_id\":\"1\","
                    "\"closed_entry_id\":%s,\"releases_on\":\"owner\"}",
                    (long long) NEAP_HELD[i][0], NEAP_STATE[i] == 1 ? "held" : "released",
                    (long long) NEAP_HELD[i][1], (long long) NEAP_HELD[i][2],
                    (long long) NEAP_HELD[i][3], (long long) NEAP_ESCROW, c);
}

static int32_t neap_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    static char res[4096];
    bool mine = strstr(req, "\"ledger.hold\"") || strstr(req, "\"ledger.holds\"") ||
                strstr(req, "\"ledger.release\"") || strstr(req, "\"iso20022.render\"") ||
                strstr(req, "\"print.submit\"");
    if (!mine) return inproc(NULL, req, len, resp, cap);
    memcpy(NEAP_LAST, req, len);
    NEAP_LAST[len] = 0;
    if (NEAP_OFF && !strstr(req, "\"ledger.holds\""))
        return snprintf(resp, cap,
                        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32012,\"message\":"
                        "\"escrow is off\"}}",
                        l->seq);
    res[0] = 0;
    if (strstr(req, "\"ledger.holds\"")) {
        char r[512], p[64];
        int at = snprintf(res, sizeof res, "{\"holds\":[");
        int64_t held = 0;
        for (uint32_t i = 0; i < NEAP_NH; i++) {
            at += snprintf(res + at, sizeof res - (size_t) at, i ? "," : "");
            at += neap_hold_json(res + at, sizeof res - (size_t) at, i, -1);
            if (NEAP_STATE[i] == 1) held += NEAP_HELD[i][3];
        }
        snprintf(p, sizeof p, "{\"account\":%lld}", (long long) NEAP_ESCROW);
        int64_t bal = NEAP_ESCROW >= 0 && neap_real("ledger.balance", p, r, sizeof r)
                          ? neap_num(r, "equity_minor")
                          : 0;
        snprintf(res + at, sizeof res - (size_t) at,
                 "],\"escrow_accounts\":[{\"account\":%lld,\"currency\":\"USD\",\"balance_minor\":"
                 "\"%lld\",\"held_minor\":\"%lld\",\"consistent\":%s}]}",
                 (long long) NEAP_ESCROW, (long long) bal, (long long) held,
                 bal == held ? "true" : "false");
    } else if (strstr(req, "\"ledger.hold\"")) {
        int64_t from = neap_num(req, "from"), to = neap_num(req, "to"),
                amount = neap_num(req, "amount_minor"), entry = -1;
        char r[512], ref[40];
        if (NEAP_ESCROW < 0 &&
            neap_real("ledger.open_account",
                      "{\"name\":\"escrow: USD\",\"currency\":\"USD\",\"kind\":\"holder\"}", r,
                      sizeof r))
            NEAP_ESCROW = neap_num(r, "account");
        snprintf(ref, sizeof ref, "escrow %u to %lld", NEAP_NH + 1u, (long long) to);
        if (NEAP_ESCROW < 0 || NEAP_NH >= 4u || !neap_post(from, NEAP_ESCROW, amount, ref, &entry))
            return snprintf(resp, cap,
                            "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32001,"
                            "\"message\":\"no hold\"}}",
                            l->seq);
        NEAP_HELD[NEAP_NH][0] = NEAP_NH + 1;
        NEAP_HELD[NEAP_NH][1] = from;
        NEAP_HELD[NEAP_NH][2] = to;
        NEAP_HELD[NEAP_NH][3] = amount;
        NEAP_STATE[NEAP_NH] = 1;
        neap_hold_json(res, sizeof res, NEAP_NH++, -1);
    } else if (strstr(req, "\"ledger.release\"")) {
        int64_t h = neap_num(req, "hold"), entry = -1;
        char ref[40];
        snprintf(ref, sizeof ref, "escrow %lld release", (long long) h);
        if (h < 1 || h > (int64_t) NEAP_NH || NEAP_STATE[h - 1] != 1 ||
            !neap_post(NEAP_ESCROW, NEAP_HELD[h - 1][2], NEAP_HELD[h - 1][3], ref, &entry))
            return snprintf(resp, cap,
                            "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32001,"
                            "\"message\":\"no such hold\"}}",
                            l->seq);
        NEAP_STATE[h - 1] = 2;
        neap_hold_json(res, sizeof res, (uint32_t) (h - 1), entry);
    } else if (strstr(req, "\"iso20022.render\"")) {
        snprintf(res, sizeof res,
                 "{\"ok\":true,\"kind\":\"camt053\",\"xml\":\"<Document xmlns=\\\"urn:iso:std:iso:"
                 "20022:tech:xsd:camt.053.001.08\\\"><Ccy>USD</Ccy></Document>\\n\"}");
    } else {
        snprintf(res, sizeof res, "{\"job_id\":7,\"state\":\"pending\"}");
    }
    return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq, res);
}

static int64_t neap_balance_of(const neap_t *n, uint32_t account)
{
    const coffer_account_t *a = coffer_account(&n->books, account);
    return a ? a->balance : INT64_MIN;
}

APPS_TEST(neap)
{
    /* the rules (NR1-NR7) */
    CHECK(neap_entry_may_post(1, 0, 0, 1) && neap_entry_may_post(NEAP_AMOUNT_MAX, 0, 0, 1) &&
              !neap_entry_may_post(NEAP_AMOUNT_MAX + 1, 0, 0, 1) &&
              !neap_entry_may_post(0, 0, 0, 1) && !neap_entry_may_post(5, 1, 0, 1) &&
              !neap_entry_may_post(5, 0, 1, 1) && !neap_entry_may_post(5, 0, 0, 0),
          "neap: an entry posts only balanced, every account open, the period open");
    CHECK(neap_equation_gap(168000, 50000, 118000) == 0 && neap_equation_gap(1, 0, 0) == 1 &&
              neap_equation_gap(NEAP_MINOR_MAX + 1, 0, 0) == NEAP_REFUSED &&
              neap_equation_gap(NEAP_MINOR_MAX, -1, 0) == NEAP_REFUSED,
          "neap: A = L + E, 18 digits either way");
    CHECK(neap_straight_charge(100000, 10000, 7, 1) == 12857 &&
              neap_straight_charge(100000, 10000, 7, 7) == 12858 &&
              neap_straight_charge(1, 0, 1, 1) == 1 &&
              neap_straight_charge(1, 2, 1, 1) == NEAP_REFUSED &&
              neap_straight_charge(1, 0, 0, 1) == NEAP_REFUSED &&
              neap_straight_charge(1, 0, 2, 3) == NEAP_REFUSED &&
              neap_straight_charge(NEAP_COST_MAX + 1, 0, 1, 1) == NEAP_REFUSED,
          "neap: straight line, the remainder in the last period");
    CHECK(neap_declining_charge(100000, 10000, 2000, 0) == 20000 &&
              neap_declining_charge(100000, 99000, 2000, 0) == 1000 &&
              neap_declining_charge(100000, 10000, 2000, 1) == 90000 &&
              neap_declining_charge(1000, 0, 3333, 0) == 333 &&
              neap_declining_charge(1, 0, 10001, 0) == NEAP_REFUSED &&
              neap_declining_charge(1, 0, 0, 2) == NEAP_REFUSED,
          "neap: declining balance, never below salvage");
    CHECK(neap_gl_line_amount(500, 0, 1) == 500 && neap_gl_line_amount(500, 0, -1) == -500 &&
              neap_gl_line_amount(0, 500, -1) == 500 &&
              neap_gl_line_amount(NEAP_MINOR_MAX + 1, 0, 1) == NEAP_REFUSED &&
              neap_gl_line_amount(1, 0, 0) == NEAP_REFUSED,
          "neap: a general ledger line on its account's normal side");
    CHECK(neap_node_ok(0, 999999, 5, 1) && !neap_node_ok(0, 1000000, 5, 1) &&
              !neap_node_ok(0, 1, 10, 1),
          "neap: a chart node fits ^ZXNEAP");
    {
        int64_t ac[5] = {1, 1, 2, 1, 2}, mo[5] = {1, 1, 1, 2, 1}, am[5] = {100, 250, 40, 7, -10},
                out[5] = {0};
        int64_t big[2] = {INT64_MAX, 1}, same[2] = {1, 1};
        CHECK(neap_grid(ac, mo, am, 5, out) == ZXM_OK && out[0] == 350 && out[1] == 350 &&
                  out[2] == 30 && out[3] == 7 && out[4] == 30 &&
                  neap_grid(same, same, big, 2, out) == ZXM_E_ARG,
              "neap: the account grid, refused past 64 bits");
    }

    /* scratch books */
    char root[256], scratch[512], path[COFFER_PATH_MAX + 1], file[600];
    bool saved = false;
    CHECK(zxf_root(root, sizeof root) == 0, "neap: the test's folder");
    snprintf(scratch, sizeof scratch, "%s/neap-scratch.books", root);
    remove(scratch);
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file(scratch) == 0 && zxvd_rpc_books_open() == 0,
          "neap: scratch books open");
    zxm_link_init(&NEAP_LINK, neap_send, &NEAP_LINK);
    neap_init(&NP, &NEAP_LINK, INT64_C(10000000));
    CHECK(coffer_books(&NP.books, path, sizeof path, &saved) == ZXM_OK && saved &&
              !strcmp(path, scratch),
          "neap: every post goes to the scratch file: %s", path);
    uint32_t bank = 0, loan = 0, capital = 0, sales = 0, rent = 0, spare = 0;
    CHECK(coffer_enable(&NP.books, "USD") == ZXM_OK &&
              coffer_open_account(&NP.books, "bank", "USD", false, &bank) == ZXM_OK &&
              coffer_open_account(&NP.books, "loan", "USD", true, &loan) == ZXM_OK &&
              coffer_open_account(&NP.books, "capital", "USD", true, &capital) == ZXM_OK &&
              coffer_open_account(&NP.books, "sales", "USD", true, &sales) == ZXM_OK &&
              coffer_open_account(&NP.books, "rent", "USD", false, &rent) == ZXM_OK &&
              coffer_open_account(&NP.books, "spare", "USD", false, &spare) == ZXM_OK,
          "neap: accounts: %s", NEAP_LINK.err);

    /* NM2: the chart, every node checked before it is kept */
    CHECK(neap_load(&NP) == ZXM_OK && neap_equation(&NP) == ZXM_E_STATE && !NP.equation_ok,
          "neap: no chart yet, no equation");
    CHECK(neap_chart_add(&NP, 1, 0, NEAP_ASSET, -1) == ZXM_OK &&
              neap_chart_add(&NP, 2, 0, NEAP_LIABILITY, -1) == ZXM_OK &&
              neap_chart_add(&NP, 3, 0, NEAP_EQUITY, -1) == ZXM_OK &&
              neap_chart_add(&NP, 4, 0, NEAP_INCOME, -1) == ZXM_OK &&
              neap_chart_add(&NP, 5, 0, NEAP_EXPENSE, -1) == ZXM_OK &&
              neap_chart_add(&NP, 101, 1, NEAP_ASSET, bank) == ZXM_OK &&
              neap_chart_add(&NP, 102, 1, NEAP_ASSET, spare) == ZXM_OK &&
              neap_chart_add(&NP, 201, 2, NEAP_LIABILITY, loan) == ZXM_OK &&
              neap_chart_add(&NP, 301, 3, NEAP_EQUITY, capital) == ZXM_OK &&
              neap_chart_add(&NP, 401, 4, NEAP_INCOME, sales) == ZXM_OK && !strcmp(NP.ccy, "USD"),
          "neap: the chart");
    int before = sent;
    CHECK(neap_chart_add(&NP, 0, 0, NEAP_ASSET, -1) == ZXM_E_ARG &&
              neap_chart_add(&NP, 1000000, 0, NEAP_ASSET, -1) == ZXM_E_ARG &&
              neap_chart_add(&NP, 9, 0, 6, -1) == ZXM_E_ARG &&
              neap_chart_add(&NP, 101, 1, NEAP_ASSET, -1) == ZXM_E_ARG &&
              neap_chart_add(&NP, 9, 77, NEAP_ASSET, -1) == ZXM_E_ARG &&
              neap_chart_add(&NP, 9, 1, NEAP_ASSET, bank) == ZXM_E_ARG &&
              neap_chart_add(&NP, 9, 1, NEAP_ASSET, 9999) == ZXM_E_ARG && NP.nn == 10 &&
              sent == before,
          "neap: a node that does not fit, twice, or under nothing is refused before anything is "
          "asked");
    CHECK(neap_equation(&NP) == ZXM_E_STATE && strstr(NP.notice, "not in the chart"),
          "neap: rent is not in the chart yet: %s", NP.notice);
    CHECK(neap_chart_add(&NP, 501, 5, NEAP_EXPENSE, rent) == ZXM_OK &&
              neap_equation(&NP) == ZXM_OK && NP.equation_ok && NP.assets == 0,
          "neap: every account charted: A = L + E at nothing");

    /* NM3, NM4: proposed, held for the owner, then posted, A = L + E after each */
    const struct {
        uint32_t *from, *to;
        int64_t amount;
        const char *ref;
    } post[4] = {{&capital, &bank, 100000, "capital 1"},
                 {&loan, &bank, 50000, "loan 1"},
                 {&sales, &bank, 30000, "sale 1"},
                 {&bank, &rent, 12000, "rent 1"}};
    uint64_t entry = 0;
    for (uint32_t i = 0; i < 4u; i++) {
        CHECK(neap_propose(&NP, *post[i].from, *post[i].to, post[i].amount, post[i].ref) ==
                      ZXM_OK &&
                  NP.books.held.pending,
              "neap: %s proposed: %s", post[i].ref, NP.notice);
        before = sent;
        CHECK(neap_confirm(&NP, false, &entry) == ZXM_E_STATE && NP.books.held.pending &&
                  sent == before && strstr(NP.notice, "waits for your word"),
              "neap: %s waits for the owner", post[i].ref);
        CHECK(neap_confirm(&NP, true, &entry) == ZXM_OK && NP.equation_ok && NP.gap == 0 &&
                  NP.books.books_ok && !NP.books.held.pending,
              "neap: %s posted, A = L + E: %s %s", post[i].ref, NP.notice, NEAP_LINK.err);
    }
    CHECK(NP.assets == 168000 && NP.liabilities == 50000 && NP.equity == 118000,
          "neap: A %lld = L %lld + E %lld", (long long) NP.assets, (long long) NP.liabilities,
          (long long) NP.equity);
    CHECK(neap_propose(&NP, bank, rent, 0, "nothing") == ZXM_E_ARG &&
              neap_propose(&NP, bank, rent, NEAP_AMOUNT_MAX + 1, "too much") == ZXM_E_ARG &&
              neap_propose(&NP, bank, rent, 500000, "overdrawn") == ZXM_E_ARG &&
              !NP.books.held.pending,
          "neap: nothing, too much and an overdraft refused (coffer.transfer_ok too): %s",
          NP.notice);

    /* a closed account and a closed period: nothing posts */
    CHECK(neap_chart_close(&NP, 101, true) == ZXM_E_STATE &&
              neap_chart_close(&NP, 102, false) == ZXM_E_STATE &&
              neap_chart_close(&NP, 102, true) == ZXM_OK &&
              neap_propose(&NP, bank, spare, 100, "to a closed one") == ZXM_E_ARG &&
              strstr(NP.notice, "closed or not in the chart"),
          "neap: a closed account takes nothing: %s", NP.notice);
    CHECK(reckoner_set_period(&NP.rk, 0, INT64_C(1000000)) == ZXM_OK &&
              reckoner_close(&NP.rk) == ZXM_OK &&
              neap_propose(&NP, bank, rent, 100, "late") == ZXM_E_STATE &&
              strstr(NP.notice, "period is closed") && reckoner_reopen(&NP.rk) == ZXM_OK,
          "neap: a closed period takes nothing");
    CHECK(neap_propose(&NP, bank, rent, 100, "then closed") == ZXM_OK &&
              reckoner_close(&NP.rk) == ZXM_OK && neap_confirm(&NP, true, &entry) == ZXM_E_STATE &&
              reckoner_reopen(&NP.rk) == ZXM_OK && neap_cancel(&NP) == ZXM_OK &&
              !NP.books.held.pending,
          "neap: a period closed after the proposal stops the post");

    /* NM6, the BOTH test: the statement's 300.00 matches an entry; its
     * 250.00 has no matching book entry: BOTH, never picked */
    lpres_state_t st = LPRES_STATE_NEITHER;
    CHECK(neap_reconcile(&NP, bank, 30000, &st) == ZXM_OK && st == LPRES_STATE_TRUE &&
              NP.line[0].entry >= 0 && NP.period_state == LPRES_STATE_TRUE,
          "neap: a line the books have: TRUE");
    CHECK(neap_reconcile(&NP, bank, 25000, &st) == ZXM_OK && st == LPRES_STATE_BOTH && st == 3 &&
              NP.line[1].entry == -1 && NP.period_state == LPRES_STATE_BOTH,
          "neap: a statement line of 250.00 and no book entry: BOTH (state %d)", (int) st);
    CHECK(neap_reconcile(&NP, bank, 0, &st) == ZXM_E_ARG &&
              neap_reconcile(&NP, 9999, 1, &st) == ZXM_E_STATE,
          "neap: a line of nothing, or of no charted account, refused");

    /* NM7: the 250.00 on a suspense hold, with the owner's word; A = L + E still */
    int64_t bank_before = neap_balance_of(&NP, bank);
    before = sent;
    CHECK(neap_suspend(&NP, 1, bank, rent, false) == ZXM_E_STATE && sent == before &&
              neap_suspend(&NP, 0, bank, rent, true) == ZXM_E_STATE,
          "neap: a hold waits for the owner, and only a BOTH line is held");
    NEAP_OFF = true;
    CHECK(neap_suspend(&NP, 1, bank, rent, true) == ZXM_E_REMOTE && NP.line[1].hold == 0 &&
              NEAP_LINK.err_code == -32012,
          "neap: the escrow switch off: nothing held");
    NEAP_OFF = false;
    CHECK(neap_suspend(&NP, 1, bank, rent, true) == ZXM_OK && NP.line[1].hold == 1 &&
              NP.line[1].state == LPRES_STATE_BOTH && NP.period_state == LPRES_STATE_BOTH,
          "neap: on suspense, still BOTH: %s %s", NP.notice, NEAP_LINK.err);
    CHECK(strstr(NEAP_LAST, "\"ledger.hold\"") && strstr(NEAP_LAST, "\"amount_minor\":25000") &&
              NEAP_ESCROW >= 0 && neap_balance_of(&NP, (uint32_t) NEAP_ESCROW) == 25000 &&
              neap_balance_of(&NP, bank) == bank_before - 25000,
          "neap: the hold's amount is 250.00 on the suspense account: %s", NEAP_LAST);
    const neap_node_t *sus = neap_node_of(&NP, (uint32_t) NEAP_ESCROW);
    CHECK(sus && sus->code == NEAP_SUSPENSE && sus->kind == NEAP_ASSET && NP.equation_ok &&
              NP.gap == 0 && NP.assets == 168000 && NP.assets == NP.liabilities + NP.equity,
          "neap: suspense is an asset and A = L + E still holds");
    CHECK(neap_holds(&NP) == ZXM_OK && NP.nh == 1 && NP.hold[0].amount == 25000 &&
              !strcmp(NP.hold[0].state, "held") && NP.escrow_ok,
          "neap: ledger.holds read back, the escrow account matches: %s", NP.notice);
    CHECK(neap_suspend(&NP, 1, bank, rent, true) == ZXM_E_STATE, "neap: one hold a line");

    /* released with the owner's word: booked now, the period TRUE again */
    before = sent;
    CHECK(neap_release(&NP, 1, false) == ZXM_E_STATE && sent == before &&
              neap_release(&NP, 0, true) == ZXM_E_STATE,
          "neap: a release waits for the owner");
    CHECK(neap_release(&NP, 1, true) == ZXM_OK && NP.line[1].state == LPRES_STATE_TRUE &&
              NP.line[1].entry >= 0 && NP.period_state == LPRES_STATE_TRUE && NP.equation_ok &&
              NP.assets == 143000 && NP.liabilities == 50000 && NP.equity == 93000 &&
              neap_balance_of(&NP, rent) == 37000,
          "neap: released to the payee: A %lld = L + E %lld", (long long) NP.assets,
          (long long) (NP.liabilities + NP.equity));
    CHECK(neap_holds(&NP) == ZXM_OK && !strcmp(NP.hold[0].state, "released"),
          "neap: the hold reads released");

    /* NR5: the bank's ledger lines, and the loan's on its credit side */
    {
        int64_t lines[COFFER_ENTRIES];
        uint32_t k = 0;
        CHECK(neap_ledger_lines(&NP, bank, lines, COFFER_ENTRIES, &k) == ZXM_OK && k == 5 &&
                  lines[0] == 100000 && lines[3] == -12000 && lines[4] == -25000,
              "neap: the bank's lines: %u", k);
        CHECK(neap_ledger_lines(&NP, loan, lines, COFFER_ENTRIES, &k) == ZXM_OK && k == 1 &&
                  lines[0] == 50000 && neap_ledger_lines(&NP, bank, lines, 2, &k) == ZXM_E_TOOBIG,
              "neap: the loan's line on its credit side");
    }

    /* NM8: schedules that sum to cost less salvage */
    CHECK(neap_schedule(&NP, NEAP_STRAIGHT, 100000, 10000, 7, 0) == ZXM_OK && NP.ndep == 7 &&
              NP.dep[0] == 12857 && NP.dep[6] == 12858,
          "neap: straight line, the remainder last");
    CHECK(neap_schedule(&NP, NEAP_DECLINING, 100000, 10000, 5, 2000) == ZXM_OK && NP.ndep == 5 &&
              NP.dep[0] == 20000 && NP.dep[1] == 16000 && NP.dep[2] == 12800 &&
              NP.dep[3] == 10240 && NP.dep[4] == 30960,
          "neap: declining balance, the rest above salvage last: %lld", (long long) NP.dep[4]);
    CHECK(neap_schedule(&NP, NEAP_STRAIGHT, 100, 200, 3, 0) == ZXM_E_ARG &&
              neap_schedule(&NP, NEAP_STRAIGHT, 100, 0, 0, 0) == ZXM_E_ARG &&
              neap_schedule(&NP, 3, 100, 0, 3, 0) == ZXM_E_ARG && NP.ndep == 5,
          "neap: a schedule that does not fit is refused, the last kept");

    /* NM9: the report, saved and printed only from a closed period whose books hold */
    CHECK(neap_report(&NP) == ZXM_OK && strstr(NP.text, "not tax advice") &&
              strstr(NP.text, "Assets: 143000") && strstr(NP.text, "A = L + E: holds") &&
              strstr(NP.text, "bank 143000") && strstr(NP.text, "loan 50000") &&
              strstr(NP.text, "held BOTH: 0") && strstr(NP.text, "Period 5: 30960"),
          "neap: the report: %s", NP.text);
    CHECK(neap_save(&NP, "neap-report.txt") == ZXM_E_STATE &&
              strstr(NP.notice, "Close the period") && neap_save(&NP, "a/b.txt") == ZXM_E_ARG &&
              neap_print(&NP, "ipp://printer/ipp/print") == ZXM_E_STATE,
          "neap: an open period is never reported");
    CHECK(reckoner_close(&NP.rk) == ZXM_OK && neap_save(&NP, "neap-report.txt") == ZXM_OK &&
              !strcmp(NP.saved, "neap-report.txt"),
          "neap: saved: %s", NEAP_LINK.err);
    snprintf(file, sizeof file, "%s/neap-report.txt", root);
    FILE *f = fopen(file, "rb");
    long size = -1;
    if (f) {
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fclose(f);
    }
    CHECK(size == (long) NP.text_len, "neap: the file is the report: %ld of %u", size, NP.text_len);
    CHECK(neap_save(&NP, "neap-report.txt") == ZXM_E_REMOTE && strstr(NP.notice, "never replaces"),
          "neap: never over an earlier report");
    CHECK(neap_print(&NP, "http://printer") == ZXM_E_ARG &&
              neap_print(&NP, "ipp://printer/ipp/print") == ZXM_OK && NP.job == 7 &&
              strstr(NEAP_LAST, "\"print.submit\"") &&
              strstr(NEAP_LAST, "\"path\":\"neap-report.txt\""),
          "neap: printed: %s", NEAP_LAST);

    /* the statement as camt.053 and the advisory estimate, through Reckoner */
    CHECK(reckoner_statement(&NP.rk, bank) == ZXM_OK && NP.rk.st.closing == 143000 &&
              reckoner_render(&NP.rk, "neap-1", "2026-10-22T09:00:00") == ZXM_OK &&
              strstr(NEAP_LAST, "\"iso20022.render\"") &&
              strstr(NEAP_LAST, "\"closing\":\"143000\""),
          "neap: the bank's statement rendered: %s", NEAP_LINK.err);
    {
        int64_t lo[2] = {0, 100000}, hi[2] = {100000, 999999}, rt[2] = {0, 2000};
        CHECK(reckoner_jurisdiction(&NP.rk) == ZXM_OK &&
                  reckoner_tax(&NP.rk, 130000, lo, hi, rt, 2) == ZXM_OK &&
                  NP.rk.tax.total == 6000 && strstr(NP.rk.text, "not tax advice"),
              "neap: the tax estimate is advice: %s", NEAP_LINK.err);
    }

    /* back to books in memory: the scratch file and the report removed */
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file("") == 0 && remove(scratch) == 0, "neap: scratch books removed");
    CHECK(remove(file) == 0, "neap: report removed");
}
