/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Ninefold's model against zxvd's real dispatcher (LINK). The accounts go
 * to a scratch books file in the test's own folder, never to real books;
 * the money is moved in through Coffer's own owner-confirmed transfers.
 * swarm.earnings is answered by the FAKE link where a settled cycle is
 * needed, since the test runs no swarm market. */
#include "ninefold_model.h"

static ninefold_t NF, NF2;

APPS_TEST(ninefold)
{
    /* the rules (NF1 - NF3) */
    CHECK(!ninefold_state_reserved(0) && !ninefold_state_reserved(3) &&
              ninefold_state_reserved(4) && ninefold_state_reserved(7) &&
              !ninefold_state_reserved(8) && !ninefold_state_reserved(-1) &&
              !ninefold_state_reserved(9),
          "state-reserved: social to spiritual");
    CHECK(ninefold_reserved_n(0) == 0 && ninefold_reserved_n(5) == 1 &&
              ninefold_reserved_n(8) == 0 && ninefold_reserved_n(9) == NINEFOLD_REFUSED &&
              ninefold_reserved_n(-1) == NINEFOLD_REFUSED,
          "state-reserved as a number, refused past the nine");
    CHECK(ninefold_form_holding(0, 0) == 0 && ninefold_form_holding(1, 250000) == 250000 &&
              ninefold_form_holding(8, NINEFOLD_MINOR_MAX) == NINEFOLD_MINOR_MAX &&
              ninefold_form_holding(4, NINEFOLD_MINOR_MAX) == 0 &&
              ninefold_form_holding(0, NINEFOLD_MINOR_MAX + 1) == NINEFOLD_REFUSED &&
              ninefold_form_holding(0, -1) == NINEFOLD_REFUSED &&
              ninefold_form_holding(9, 1) == NINEFOLD_REFUSED &&
              ninefold_form_holding(-1, 1) == NINEFOLD_REFUSED,
          "holding: priced or 0, its edges");
    CHECK(ninefold_form_share(0, 0, 1) == 0 && ninefold_form_share(0, 1, 3) == 3333 &&
              ninefold_form_share(1, 2, 3) == 6666 &&
              ninefold_form_share(8, NINEFOLD_WHOLE_MAX, NINEFOLD_WHOLE_MAX) == 10000 &&
              ninefold_form_share(6, 5, 10) == 0 &&
              ninefold_form_share(0, 1, 0) == NINEFOLD_REFUSED &&
              ninefold_form_share(0, 4, 3) == NINEFOLD_REFUSED &&
              ninefold_form_share(0, -1, 3) == NINEFOLD_REFUSED &&
              ninefold_form_share(0, 1, NINEFOLD_WHOLE_MAX + 1) == NINEFOLD_REFUSED &&
              ninefold_form_share(9, 1, 3) == NINEFOLD_REFUSED,
          "share: floored, 0 when state-reserved, its edges");
    CHECK(ninefold_form_of("Manufactured: lathe") == NINEFOLD_MANUFACTURED &&
              ninefold_form_of("social:guild standing") == NINEFOLD_SOCIAL &&
              ninefold_form_of("system: rack") == NINEFOLD_SYSTEM &&
              ninefold_form_of("cash") == NINEFOLD_FINANCIAL &&
              ninefold_form_of("human resources") == NINEFOLD_FINANCIAL &&
              ninefold_form_of("natural") == NINEFOLD_FINANCIAL &&
              ninefold_form_of("owner: swarm earnings") == NINEFOLD_FINANCIAL &&
              ninefold_form_of(NULL) == NINEFOLD_FINANCIAL,
          "NF4: a form's name and a colon, else financial");
    CHECK(!strcmp(ninefold_form_name(2), "intellectual") && !strcmp(ninefold_form_name(9), "") &&
              !strcmp(ninefold_form_name(-1), ""),
          "form names");

    /* a scratch books file, never the owner's */
    char root[256], scratch[512];
    CHECK(zxf_root(root, sizeof root) == 0, "the test's folder");
    snprintf(scratch, sizeof scratch, "%s/ninefold-scratch.books", root);
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file(scratch) == 0 && zxvd_rpc_books_open() == 0, "scratch books open");
    ninefold_init(&NF, &LINK);
    CHECK(!NF.loaded && NF.earnings == -1 && ninefold_load(&NF, "usd") == ZXM_E_ARG &&
              ninefold_load(&NF, NULL) == ZXM_E_ARG && !NF.loaded,
          "a bad currency refused before anything is asked");

    /* accounts in five forms, two currencies; money moved in by Coffer */
    coffer_t *cf = &NF.books;
    uint32_t employer = 0, cash = 0, lathe = 0, patent = 0, guild = 0, rack = 0, euros = 0;
    uint64_t entry = 0;
    CHECK(coffer_enable(cf, "USD") == ZXM_OK && coffer_enable(cf, "EUR") == ZXM_OK &&
              coffer_open_account(cf, "employer", "USD", true, &employer) == ZXM_OK &&
              coffer_open_account(cf, "cash", "USD", false, &cash) == ZXM_OK &&
              coffer_open_account(cf, "Manufactured: lathe", "USD", false, &lathe) == ZXM_OK &&
              coffer_open_account(cf, "intellectual: patent", "USD", false, &patent) == ZXM_OK &&
              coffer_open_account(cf, "social: guild standing", "USD", false, &guild) == ZXM_OK &&
              coffer_open_account(cf, "system: rack", "USD", false, &rack) == ZXM_OK &&
              coffer_open_account(cf, "travel", "EUR", false, &euros) == ZXM_OK,
          "accounts: %s", LINK.err);
    CHECK(coffer_propose(cf, employer, cash, 600000, "nf cash") == ZXM_OK &&
              coffer_confirm(cf, &entry) == ZXM_OK &&
              coffer_propose(cf, employer, lathe, 300000, "nf lathe") == ZXM_OK &&
              coffer_confirm(cf, &entry) == ZXM_OK &&
              coffer_propose(cf, employer, patent, 100000, "nf patent") == ZXM_OK &&
              coffer_confirm(cf, &entry) == ZXM_OK &&
              coffer_propose(cf, employer, guild, 50000, "nf guild") == ZXM_OK &&
              coffer_confirm(cf, &entry) == ZXM_OK,
          "money in, each transfer confirmed: %s", LINK.err);

    /* NF2, NF3: what each form holds and its share of the priced whole */
    CHECK(ninefold_load(&NF, "USD") == ZXM_OK && NF.loaded && !strcmp(NF.ccy, "USD") &&
              NF.earnings == -1,
          "swarm.earnings, ledger.accounts, ledger.balance: %s", LINK.err);
    CHECK(NF.holding[NINEFOLD_FINANCIAL] == 600000 && NF.holding[NINEFOLD_MANUFACTURED] == 300000 &&
              NF.holding[NINEFOLD_INTELLECTUAL] == 100000 && NF.holding[NINEFOLD_HUMAN] == 0 &&
              NF.holding[NINEFOLD_SOCIAL] == 0 && NF.holding[NINEFOLD_SYSTEM] == 0 &&
              NF.whole == 1000000,
          "holdings by form, the guild never priced: %lld %lld %lld whole %lld",
          (long long) NF.holding[0], (long long) NF.holding[1], (long long) NF.holding[2],
          (long long) NF.whole);
    CHECK(NF.accounts[NINEFOLD_FINANCIAL] == 1 && NF.accounts[NINEFOLD_SOCIAL] == 1 &&
              NF.accounts[NINEFOLD_SYSTEM] == 1 && NF.accounts[NINEFOLD_HUMAN] == 0,
          "accounts counted by form, the issuer and the euros left out");
    CHECK(NF.shared && NF.share[NINEFOLD_FINANCIAL] == 6000 &&
              NF.share[NINEFOLD_MANUFACTURED] == 3000 && NF.share[NINEFOLD_INTELLECTUAL] == 1000 &&
              NF.share[NINEFOLD_SOCIAL] == 0 && NF.share[NINEFOLD_SYSTEM] == 0,
          "shares of the priced whole");
    CHECK(strstr(NF.notice, "Held in 3 priced forms of USD") &&
              strstr(NF.notice, "1 account in a state-reserved form, counted and never priced"),
          "the notice: %s", NF.notice);
    CHECK(ninefold_load(&NF, "EUR") == ZXM_OK && NF.whole == 0 && !NF.shared &&
              NF.accounts[NINEFOLD_FINANCIAL] == 1 && NF.holding[NINEFOLD_FINANCIAL] == 0,
          "nothing held in euros: no shares");

    /* the earnings account is financial, whatever its name (FAKE answers swarm.earnings) */
    char canned[256];
    snprintf(canned, sizeof canned,
             "{\"jsonrpc\":\"2.0\",\"id\":%%u,\"result\":{\"currency\":\"USD\","
             "\"owner_account\":%u,\"source_account\":%u}}",
             (unsigned) guild, (unsigned) employer);
    CANNED = canned;
    NF.link = &FAKE;
    CHECK(ninefold_load(&NF, "USD") == ZXM_OK && NF.earnings == (int64_t) guild &&
              NF.holding[NINEFOLD_FINANCIAL] == 650000 && NF.accounts[NINEFOLD_SOCIAL] == 0 &&
              NF.whole == 1050000 && NF.share[NINEFOLD_FINANCIAL] == 6190,
          "the swarm earnings account counts as financial: %lld",
          (long long) NF.holding[NINEFOLD_FINANCIAL]);
    /* NF5: an answer that is not one is refused and the copy kept */
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"owner_account\":\"x\"}}";
    CHECK(ninefold_load(&NF, "EUR") == ZXM_E_PROTOCOL && !strcmp(NF.ccy, "USD") &&
              NF.whole == 1050000,
          "a bad earnings answer: refused, copy kept");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{}}";
    CHECK(ninefold_load(&NF, "EUR") == ZXM_E_PROTOCOL && NF.whole == 1050000,
          "no owner_account: refused");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"owner_account\":null}}";
    CHECK(ninefold_load(&NF, "USD") == ZXM_OK && NF.earnings == -1 && NF.whole == 1000000,
          "before the first settled cycle: no earnings account");
    NF.link = &LINK;

    /* the scratch file is replayed: a second model reads the same forms */
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_open() == 0, "scratch books replayed");
    ninefold_init(&NF2, &LINK);
    CHECK(ninefold_load(&NF2, "USD") == ZXM_OK &&
              !memcmp(NF2.holding, NF.holding, sizeof NF.holding) &&
              !memcmp(NF2.share, NF.share, sizeof NF.share) && NF2.whole == NF.whole,
          "read back from the scratch file: %s", LINK.err);

    /* back to books in memory: the scratch file is removed */
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file("") == 0 && remove(scratch) == 0, "scratch books removed");
}
