/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Ramble's model against zxvd's real dispatcher (LINK): web4.cid,
 * web4.agent_card and web4.card_verify as zxvd runs them, aipi.get through
 * a blockstore below that names its one block as zxvd's does, and
 * ledger.balance on scratch books filled through Coffer's model. This
 * harness does not link zxvd's Web4 feed group (zxvd_web4.c: web4.open,
 * web4.feed, web4.envelope, web4.verify, web4.consent), so those answers
 * are made up in the shape zxvd_web4.c writes them (FAKE), and each
 * request is checked as sent. */
#include "coffer_model.h"
#include "ramble_model.h"
#include "reckoner_rules.h"
#include "seedbank_model.h"

static ramble_t RB;
static coffer_t RBC;
static char RB_BLOCK[RAMBLE_BLOCK_MAX + 1];
static bool RB_SHORT;

static const char *rb_get(const char *cid, zxvd_buf_t *out)
{
    if (strcmp(cid, RB_BLOCK)) return "not found";
    /* "hello", or one byte short of what it says */
    zxvd_buf_printf(out, "{\"cid\":\"%s\",\"bytes\":5,\"hex\":\"%s\"}", cid,
                    RB_SHORT ? "68656c6c" : "68656c6c6f");
    return NULL;
}

#define RB_A "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define RB_B "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
/* one feed entry as zxvd_web4.c put_entry writes it */
#define RB_ENTRY(id, kind, status, tool, payload, pii, amount, ts, link)                           \
    "{\"id\":" #id ",\"kind\":\"" kind "\",\"status\":\"" status "\",\"from\":\"" RB_A             \
    "\",\"to\":\"" RB_B "\",\"tool\":\"" tool "\",\"req_id\":\"" #id "\",\"chunk\":0,"             \
    "\"final\":false,\"payload\":\"" payload "\",\"pii\":" pii ",\"amount_minor\":\"" amount       \
    "\"," ts "," link ",\"bytes\":0}"
#define RB_UNSIGNED "\"seq\":null,\"ts\":null"
#define RB_NOLINK   "\"consent\":null,\"answers\":null,\"grant\":null"
#define RB_FEED(entries, next)                                                                     \
    "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"open\":true,\"agent_id\":\"" RB_A                \
    "\",\"envelopes\":[" entries "],\"next\":" next ",\"total\":3,\"held\":2}}"

APPS_TEST(ramble)
{
    static const char HELLO[] = "bafkreibm6jg3ux5qumhcn2b3flc3tyu6dmlb4xa7u5bf44yegnrjhc4yeq";
    static const zxvd_aipi_ops_t AIPI = {NULL, rb_get, NULL, NULL, NULL};
    char back[SEEDBANK_CID_LEN + 1], block[RAMBLE_BLOCK_MAX + 1];
    const int64_t NOW = 1760000000;
    ramble_init(&RB, &LINK);

    /* RM4: a CID's text and aipi's block name spell one CID (Seedbank turns it back) */
    CHECK(ramble_cid_to_block(HELLO, RB_BLOCK) == ZXM_OK && RB_BLOCK[0] == 'z' &&
              seedbank_block_to_cid(RB_BLOCK, back) == ZXM_OK && !strcmp(back, HELLO),
          "hello's CID %s is block %s", HELLO, RB_BLOCK);
    CHECK(ramble_cid_to_block("Bafkreibm6jg3ux5qumhcn2b3flc3tyu6dmlb4xa7u5bf44yegnrjhc4yeq",
                              block) == ZXM_E_ARG &&
              ramble_cid_to_block("bafkreibm6jg3ux5qumhcn2b3flc3tyu6dmlb4xa7u5bf44yegnrjhc4ye1",
                                  block) == ZXM_E_ARG &&
              ramble_cid_to_block("bafkreibm6jg3ux5qumhcn2b3flc3tyu6dmlb4xa7u5bf44yegnrjhc4ye",
                                  block) == ZXM_E_ARG &&
              ramble_cid_to_block("bafybeibm6jg3ux5qumhcn2b3flc3tyu6dmlb4xa7u5bf44yegnrjhc4yeq",
                                  block) == ZXM_E_ARG &&
              ramble_cid_to_block("bafkreibm6jg3ux5qumhcn2b3flc3tyu6dmlb4xa7u5bf44yegnrjhc4yer",
                                  block) == ZXM_E_ARG &&
              ramble_cid_to_block(NULL, block) == ZXM_E_ARG && !block[0],
          "upper case, a '1', short, not raw, stray low bits: no CID (SR2)");

    /* RM4: web4.cid, as zxvd runs it */
    CHECK(ramble_cid_of(&RB, (const uint8_t *) "hello", 5) == ZXM_OK && !strcmp(RB.cid, HELLO) &&
              !strncmp(RB.uri, "ipfs://", 7) && !strcmp(RB.uri + 7, HELLO),
          "web4.cid of hello: %s %s", RB.cid, LINK.err);
    int before = sent;
    CHECK(ramble_cid_of(&RB, NULL, 3) == ZXM_E_ARG &&
              ramble_cid_of(&RB, (const uint8_t *) "x", RAMBLE_DATA_MAX + 1u) == ZXM_E_ARG &&
              ramble_fetch(&RB, "bafy") == ZXM_E_ARG && sent == before,
          "nothing sent for what cannot be addressed");

    /* RM4: aipi.get, no blockstore, then one that holds hello */
    CHECK(ramble_fetch(&RB, HELLO) == ZXM_E_REMOTE && LINK.err_code == -32601 && RB.got_len == 0,
          "no blockstore: %s", LINK.err);
    zxvd_rpc_set_aipi_ops(&AIPI);
    CHECK(ramble_fetch(&RB, HELLO) == ZXM_OK && RB.got_len == 5 && !memcmp(RB.got, "hello", 5) &&
              !strcmp(RB.got_cid, HELLO),
          "hello fetched by its CID: %s", LINK.err);
    RB_SHORT = true;
    CHECK(ramble_fetch(&RB, HELLO) == ZXM_E_PROTOCOL && RB.got_len == 5,
          "fewer bytes than it says: not kept");
    RB_SHORT = false;
    CHECK(ramble_fetch(&RB, "bafkreihdwdcefgh4dqkjv67uzcmw7ojee6xedzdetojuzjevtenxquvyku") ==
                  ZXM_E_REMOTE &&
              RB.got_len == 5,
          "a block this node does not hold: %s", LINK.err);
    zxvd_rpc_set_aipi_ops(NULL);

    /* agent cards: web4.card_verify on a card zxvd made from a test seed */
    {
        static char pk[RAMBLE_PK_HEX + 1], sig[RAMBLE_SIG_HEX + 1];
        char seed[65], id[RAMBLE_HEX_ID + 1];
        memset(seed, '5', 64);
        seed[64] = 0;
        zxm_begin(&LINK, "web4.agent_card");
        zxm_p_str(&LINK, "seed_hex", seed);
        zxm_p_str(&LINK, "name", "ramble-1");
        zxm_p_u64(&LINK, "bound_ms", 1700000000000u);
        CHECK(zxm_call(&LINK) == ZXM_OK &&
                  !zxm_get_str(&LINK, LINK.result, "pk_hex", pk, sizeof pk) &&
                  !zxm_get_str(&LINK, LINK.result, "bind_sig_hex", sig, sizeof sig) &&
                  !zxm_get_str(&LINK, LINK.result, "agent_id", id, sizeof id),
              "a card to check: %s", LINK.err);
        CHECK(ramble_card_verify(&RB, "ramble-1", 1700000000000u, pk, sig, id) == ZXM_OK &&
                  RB.card.done && RB.card.valid && !strcmp(RB.card.agent_id, id),
              "the card verifies: %s", LINK.err);
        CHECK(ramble_card_verify(&RB, "ramble-2", 1700000000000u, pk, sig, NULL) == ZXM_OK &&
                  !RB.card.valid && RB.card.reason[0],
              "another name: not valid (%s)", RB.card.reason);
        before = sent;
        CHECK(ramble_card_verify(&RB, "Ramble", 1, pk, sig, NULL) == ZXM_E_ARG &&
                  ramble_card_verify(&RB, "ramble-1", 1, "00", sig, NULL) == ZXM_E_ARG &&
                  ramble_card_verify(&RB, "ramble-1", 1, pk, sig, "abc") == ZXM_E_ARG &&
                  sent == before,
              "a bad name, key or agent_id: nothing sent");
    }

    /* the Web4 feed group is not in this harness: zxvd says so */
    CHECK(ramble_open(&RB, NULL, NOW) == ZXM_E_REMOTE && LINK.err_code == -32601 && !RB.open,
          "web4.open is not linked here: %s", LINK.err);

    /* web4.open and RM1, the feed, as zxvd_web4.c answers them */
    RB.link = &FAKE;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"open\":true,\"new\":true,\"agent_id\":"
             "\"" RB_A "\",\"name\":\"chiglet\",\"pk\":\"00\",\"envelopes\":3,\"held\":2,"
             "\"max_envelopes\":64,\"max_held\":32,\"in_memory\":true}}";
    CHECK(ramble_open(&RB, NULL, NOW) == ZXM_OK && RB.open && !strcmp(RB.agent_id, RB_A) &&
              !strcmp(RB.name, "chiglet") && strstr(FAKE.req, "\"web4.open\"") &&
              strstr(FAKE.req, "\"now\":1760000000"),
          "the feed opened: %s", FAKE.err);
    uint32_t seq = FAKE.seq;
    CHECK(ramble_open(&RB, "Chiglet", NOW) == ZXM_E_ARG &&
              ramble_open(&RB, NULL, -2) == ZXM_E_ARG && ramble_feed(&RB, 0, 0, -1) == ZXM_E_ARG &&
              ramble_feed(&RB, 0, 51, -1) == ZXM_E_ARG && ramble_feed(&RB, 0, 20, 3) == ZXM_E_ARG &&
              FAKE.seq == seq,
          "a bad name, time, page or status: nothing sent");
    CANNED = RB_FEED(
        RB_ENTRY(5, "REQUEST", "held", "pay", "for the seeds", "false", "2500", RB_UNSIGNED,
                 RB_NOLINK) "," RB_ENTRY(4, "RESPONSE", "signed", "chat", "thanks", "false", "0",
                                         "\"seq\":\"7\",\"ts\":1759999000",
                                         RB_NOLINK) "," RB_ENTRY(3, "REQUEST", "held", "chat",
                                                                 "my address", "true", "0",
                                                                 RB_UNSIGNED, RB_NOLINK),
        "2");
    CHECK(ramble_feed(&RB, 0, 3, -1) == ZXM_OK && RB.n == 3 && RB.next == 2 && RB.total == 3 &&
              RB.held == 2 && RB.env[0].id == 5 && RB.env[0].status == RAMBLE_HELD &&
              RB.env[0].amount == 2500 && RB.env[0].ts == -1 && !strcmp(RB.env[0].tool, "pay") &&
              RB.env[1].kind == RAMBLE_RESPONSE && RB.env[1].ts == 1759999000 && RB.env[2].pii &&
              !strcmp(RB.env[2].payload, "my address") && strstr(FAKE.req, "\"web4.feed\"") &&
              strstr(FAKE.req, "\"limit\":3") && ramble_find(&RB, 4) == 1 &&
              ramble_find(&RB, 9) == -1,
          "a page of three: %s", FAKE.err);
    /* RM1: a page that does not hold together is not kept */
    CANNED = RB_FEED(RB_ENTRY(6, "REQUEST", "held", "pay", "x", "false", "1000000000000000000",
                              RB_UNSIGNED, RB_NOLINK),
                     "null");
    CHECK(ramble_feed(&RB, 0, 3, -1) == ZXM_E_PROTOCOL && RB.n == 3 && RB.env[0].id == 5,
          "an amount past 18 digits is no ^ZXRAMBLE record: the page before kept");
    CANNED = RB_FEED(RB_ENTRY(4, "RESPONSE", "signed", "chat", "x", "false", "0",
                              "\"seq\":\"7\",\"ts\":1759999000",
                              RB_NOLINK) "," RB_ENTRY(5, "REQUEST", "held", "pay", "x", "false",
                                                      "5", RB_UNSIGNED, RB_NOLINK),
                     "null");
    CHECK(ramble_feed(&RB, 0, 3, -1) == ZXM_E_PROTOCOL && RB.n == 3, "oldest first: refused");
    CANNED = RB_FEED(
        RB_ENTRY(4, "RESPONSE", "signed", "chat", "x", "false", "0", RB_UNSIGNED, RB_NOLINK),
        "null");
    CHECK(ramble_feed(&RB, 0, 3, -1) == ZXM_E_PROTOCOL && RB.n == 3, "signed with no time");
    CANNED = RB_FEED(RB_ENTRY(4, "CANCEL", "signed", "chat", "x", "false", "0",
                              "\"seq\":\"7\",\"ts\":1759999000", RB_NOLINK),
                     "null");
    CHECK(ramble_feed(&RB, 0, 3, -1) == ZXM_E_PROTOCOL && RB.n == 3, "a kind the feed has not");
    CANNED = RB_FEED(
        RB_ENTRY(3, "REQUEST", "held", "chat", "my address", "true", "0", RB_UNSIGNED, RB_NOLINK),
        "2");
    CHECK(ramble_feed(&RB, 4, 3, RAMBLE_HELD) == ZXM_OK && RB.n == 1 && RB.env[0].id == 3 &&
              strstr(FAKE.req, "\"before\":4") && strstr(FAKE.req, "\"status\":\"held\""),
          "the held ones before 4: %s", FAKE.req);
    CHECK(ramble_feed(&RB, 3, 3, RAMBLE_HELD) == ZXM_E_PROTOCOL && RB.env[0].id == 3,
          "an entry not before 'before': refused");
    CANNED = RB_FEED(
        RB_ENTRY(5, "REQUEST", "held", "pay", "for the seeds", "false", "2500", RB_UNSIGNED,
                 RB_NOLINK) "," RB_ENTRY(4, "RESPONSE", "signed", "chat", "thanks", "false", "0",
                                         "\"seq\":\"7\",\"ts\":1759999000",
                                         RB_NOLINK) "," RB_ENTRY(3, "REQUEST", "held", "chat",
                                                                 "my address", "true", "0",
                                                                 RB_UNSIGNED, RB_NOLINK),
        "2");
    CHECK(ramble_feed(&RB, 0, 3, -1) == ZXM_OK && RB.n == 3, "the first page again");

    /* RM2: nothing held leaves; a signed envelope's bytes and their check */
    seq = FAKE.seq;
    CHECK(ramble_envelope(&RB, 5) == ZXM_E_STATE && ramble_verify(&RB, 3) == ZXM_E_STATE &&
              ramble_envelope(&RB, 9) == ZXM_E_ARG && FAKE.seq == seq && RB.raw_len == 0,
          "a held envelope's bytes are never asked for");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"id\":4,\"kind\":\"RESPONSE\","
             "\"bytes\":4,\"envelope\":\"5734ab01\"}}";
    CHECK(ramble_envelope(&RB, 4) == ZXM_OK && RB.raw_len == 4 && RB.raw[0] == 0x57 &&
              RB.raw[3] == 0x01 && RB.raw_id == 4 && strstr(FAKE.req, "\"web4.envelope\""),
          "envelope 4's bytes: %s", FAKE.err);
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"id\":4,\"kind\":\"RESPONSE\","
             "\"bytes\":5,\"envelope\":\"5734ab01\"}}";
    CHECK(ramble_envelope(&RB, 4) == ZXM_E_PROTOCOL && RB.raw_len == 4,
          "fewer bytes than it says: the bytes before kept");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"valid\":true,\"kind\":\"RESPONSE\","
             "\"consent\":true,\"consent_scope\":{\"pii\":false,\"money\":true},"
             "\"max_minor\":\"2500\",\"payload_bytes\":6}}";
    CHECK(ramble_verify(&RB, 4) == ZXM_OK && RB.verdict.done && RB.verdict.valid &&
              RB.verdict.consent && RB.verdict.scope == RAMBLE_SCOPE_MONEY &&
              RB.verdict.max_minor == 2500 && strstr(FAKE.req, "\"web4.verify\""),
          "envelope 4 verifies, a money consent attached: %s", FAKE.err);
    CANNED =
        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"valid\":false,\"reason\":\"signature\"}}";
    CHECK(ramble_verify(&RB, 4) == ZXM_OK && !RB.verdict.valid &&
              !strcmp(RB.verdict.reason, "signature") && RB.verdict.scope == -1,
          "a signature that does not check: %s", RB.verdict.reason);
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"valid\":false}}";
    CHECK(ramble_verify(&RB, 4) == ZXM_E_PROTOCOL && !strcmp(RB.verdict.reason, "signature"),
          "not valid with no reason: refused");

    /* RM6: the notice of a held envelope, composed only */
    CHECK(ramble_notice(&RB, 5) == ZXM_OK && strstr(RB.notice, "Envelope 5 (pay)") &&
              strstr(RB.notice, "2500 minor units") && strstr(RB.notice, "Nothing leaves") &&
              ramble_notice(&RB, 3) == ZXM_OK && strstr(RB.notice, "personal data") &&
              ramble_notice(&RB, 4) == ZXM_E_STATE && ramble_notice(&RB, 9) == ZXM_E_ARG,
          "notices: %s", RB.notice);

    /* RM3: consent is the owner's act */
    seq = FAKE.seq;
    CHECK(ramble_consent(&RB, 5, true, 600, NOW, false) == ZXM_E_STATE &&
              ramble_consent(&RB, 5, false, 600, NOW, false) == ZXM_E_STATE &&
              ramble_consent(&RB, 5, true, 0, NOW, true) == ZXM_E_ARG &&
              ramble_consent(&RB, 5, true, RAMBLE_GRANT_MAX + 1, NOW, true) == ZXM_E_ARG &&
              ramble_consent(&RB, 5, true, 600, -1, true) == ZXM_E_ARG &&
              ramble_consent(&RB, 4, true, 600, NOW, true) == ZXM_E_STATE &&
              ramble_consent(&RB, 9, true, 600, NOW, true) == ZXM_E_ARG && FAKE.seq == seq &&
              RB.ngrants == 0 && !ramble_covers(&RB, 5, false, 2500, NOW),
          "never by itself, never past a day, only for a held envelope: nothing sent");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"envelope\":" RB_ENTRY(
        5, "REQUEST", "signed", "pay", "for the seeds", "false", "2500",
        "\"seq\":\"8\",\"ts\":1760000000",
        "\"consent\":6,\"answers\":null,\"grant\":null") ","
                                                         "\"consent\":" RB_ENTRY(
                                                             6, "CONSENT", "signed", "pay", "",
                                                             "false", "0",
                                                             "\"seq\":\"9\",\"ts\":1760000000",
                                                             "\"consent\":null,\"answers\":5,"
                                                             "\"grant\":true") "}}";
    CHECK(ramble_consent(&RB, 5, true, 600, NOW, true) == ZXM_OK && RB.ngrants == 1 &&
              RB.env[0].status == RAMBLE_SIGNED && RB.env[0].consent == 6 && RB.held == 1 &&
              strstr(FAKE.req, "\"web4.consent\"") && strstr(FAKE.req, "\"grant\":true") &&
              strstr(FAKE.req, "\"expires_in\":600"),
          "the owner granted envelope 5: %s", FAKE.err);
    CHECK(ramble_covers(&RB, 5, false, 2500, NOW) && ramble_covers(&RB, 5, false, 1, NOW + 599) &&
              !ramble_covers(&RB, 5, false, 2501, NOW) && !ramble_covers(&RB, 5, true, 2500, NOW) &&
              !ramble_covers(&RB, 5, false, 2500, NOW + 600) &&
              !ramble_covers(&RB, 5, false, 2500, NOW - 86401) &&
              !ramble_covers(&RB, 3, true, 0, NOW),
          "it covers its own amount until it expires, nothing more (RR1)");
    CHECK(ramble_consent(&RB, 5, true, 600, NOW, true) == ZXM_E_STATE,
          "a signed envelope is answered already");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"envelope\":" RB_ENTRY(
        3, "REQUEST", "denied", "chat", "", "true", "0", RB_UNSIGNED,
        "\"consent\":7,\"answers\":null,\"grant\":null") ","
                                                         "\"consent\":" RB_ENTRY(
                                                             7, "CONSENT", "signed", "chat", "",
                                                             "false", "0",
                                                             "\"seq\":\"10\",\"ts\":1760000000",
                                                             "\"consent\":null,\"answers\":3,"
                                                             "\"grant\":false") "}}";
    CHECK(ramble_consent(&RB, 3, false, 600, NOW, true) == ZXM_OK && RB.ngrants == 1 &&
              RB.env[2].status == RAMBLE_DENIED && !RB.env[2].payload[0] && RB.held == 0 &&
              strstr(FAKE.req, "\"grant\":false"),
          "the owner denied envelope 3: %s", FAKE.err);

    /* RM5: what an account would hold after a held envelope's money, on scratch books */
    {
        char root[256], scratch[512], path[COFFER_PATH_MAX + 1];
        bool saved = false;
        uint32_t payer = 0, cash = 0;
        uint64_t entry = 0;
        RB.link = &LINK;
        CHECK(zxf_root(root, sizeof root) == 0, "the test's folder");
        snprintf(scratch, sizeof scratch, "%s/ramble-scratch.books", root);
        zxvd_rpc_books_close();
        CHECK(zxvd_rpc_books_file(scratch) == 0 && zxvd_rpc_books_open() == 0,
              "scratch books open");
        coffer_init(&RBC, &LINK, COFFER_DAY_CAP);
        CHECK(coffer_books(&RBC, path, sizeof path, &saved) == ZXM_OK && saved &&
                  !strcmp(path, scratch) && coffer_enable(&RBC, "USD") == ZXM_OK &&
                  coffer_open_account(&RBC, "payer", "USD", true, &payer) == ZXM_OK &&
                  coffer_open_account(&RBC, "cash", "USD", false, &cash) == ZXM_OK &&
                  coffer_propose(&RBC, payer, cash, 10000, "in") == ZXM_OK &&
                  coffer_confirm(&RBC, &entry) == ZXM_OK && coffer_load(&RBC) == ZXM_OK,
              "cash holds 10000: %s", LINK.err);
        const coffer_account_t *ca = coffer_account(&RBC, cash);
        CHECK(ca && ramble_money_after(&RB, cash, 5) == ZXM_OK && RB.balance == ca->balance &&
                  RB.after == reckoner_statement_close(ca->balance, 0, 2500),
              "after envelope 5: %lld of %lld", (long long) RB.after, (long long) RB.balance);
        CHECK(ramble_money_after(&RB, cash, 4) == ZXM_E_ARG &&
                  ramble_money_after(&RB, cash, 9) == ZXM_E_ARG,
              "an envelope that moves no money, or none: nothing asked");
        zxvd_rpc_books_close();
        CHECK(zxvd_rpc_books_file("") == 0 && remove(scratch) == 0, "scratch books removed");
    }
}
