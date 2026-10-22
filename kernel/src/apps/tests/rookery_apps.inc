/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Rookery's model against zxvd's real dispatcher (LINK): web4.agent_card
 * and web4.card_verify as zxvd runs them, files.* on the test's Hoard (the
 * Rookery folder, put in the Trash and removed at the end) and
 * ledger.balance on scratch books filled through Coffer's model. This
 * harness does not link zxvd's Web4 feed group (zxvd_web4.c: web4.post,
 * web4.consent, web4.feed, web4.verify), so those answers are made up in
 * the shape zxvd_web4.c writes them (FAKE), and each request is checked
 * as sent. */
#include "coffer_model.h"
#include "rookery_model.h"

static rookery_t ROOK;
static coffer_t ROOKC;
static rookery_card_t ROOK_CARD;

#define ROOK_A "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define ROOK_B "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
/* one feed entry as zxvd_web4.c put_entry writes it */
#define ROOK_ENTRY(id, kind, status, from, tool, req, payload, pii, amount, ts)                    \
    "{\"id\":" #id ",\"kind\":\"" kind "\",\"status\":\"" status "\",\"from\":\"" from             \
    "\",\"to\":\"" ROOK_B "\",\"tool\":\"" tool "\",\"req_id\":\"" #req "\",\"chunk\":0,"          \
    "\"final\":false,\"payload\":\"" payload "\",\"pii\":" pii ",\"amount_minor\":\"" amount       \
    "\"," ts ",\"consent\":null,\"answers\":null,\"grant\":null,\"bytes\":0}"
#define ROOK_UNSIGNED  "\"seq\":null,\"ts\":null"
#define ROOK_SIGNED    "\"seq\":\"7\",\"ts\":1760000000"
#define ROOK_RESULT(x) "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":" x "}"
#define ROOK_FEED(entries, next)                                                                   \
    ROOK_RESULT("{\"open\":true,\"agent_id\":\"" ROOK_A "\",\"envelopes\":[" entries               \
                "],\"next\":" next ",\"total\":4,\"held\":0}")

APPS_TEST(rookery)
{
    const int64_t NOW = 1760000000;
    rookery_addr_t ad;
    char long_addr[300];
    int before;

    /* RK1: an address counted and judged (KR3) */
    CHECK(rookery_address("ada@hoard.example", &ad) == ROOKERY_ADDR_FINE && ad.size == 17 &&
              ad.ats == 1 && ad.local == 3 && ad.domain == 13 && ad.longest == 7 &&
              ad.shortest == 5 && ad.bad == 0,
          "ada@hoard.example: %lld bytes, labels %lld..%lld", (long long) ad.size,
          (long long) ad.shortest, (long long) ad.longest);
    CHECK(rookery_address("ab", &ad) == ROOKERY_ADDR_LENGTH &&
              rookery_address("a@b@c", &ad) == ROOKERY_ADDR_AT && ad.ats == 2 &&
              rookery_address("rye.field", &ad) == ROOKERY_ADDR_AT &&
              rookery_address("\100b.c", &ad) == ROOKERY_ADDR_LOCAL &&
              rookery_address("ab@", &ad) == ROOKERY_ADDR_DOMAIN &&
              rookery_address("a@b..c", &ad) == ROOKERY_ADDR_LABEL && ad.shortest == 0 &&
              rookery_address("a@b.", &ad) == ROOKERY_ADDR_LABEL &&
              rookery_address("a b@c", &ad) == ROOKERY_ADDR_BYTE && ad.bad == 1 &&
              rookery_address("a@b_c", &ad) == ROOKERY_ADDR_BYTE &&
              rookery_address("o'neil+rye@b-c.d", &ad) == ROOKERY_ADDR_FINE &&
              rookery_address(NULL, &ad) == ROOKERY_REFUSED,
          "too short, two '@', none, no local part, no domain, an empty label, a stray byte");
    memset(long_addr, 'a', sizeof long_addr - 1u);
    long_addr[sizeof long_addr - 1u] = 0;
    long_addr[1] = '@';
    CHECK(rookery_address(long_addr, &ad) == ROOKERY_ADDR_LENGTH && ad.size == 255,
          "an address past 254 bytes is counted to 255 and refused");
    memset(long_addr, 'a', 70);
    memcpy(long_addr + 65, "\100b.cd", 6);
    CHECK(rookery_address(long_addr, &ad) == ROOKERY_ADDR_LOCAL && ad.local == 65,
          "a local part of 65 bytes");
    memcpy(long_addr + 64, "\100b.cd", 6);
    CHECK(rookery_address(long_addr, &ad) == ROOKERY_ADDR_FINE && ad.local == 64,
          "a local part of 64 bytes");

    /* RK1: composing; a letter that does not fit keeps the one before */
    static char big[ROOKERY_BODY_MAX + 2];
    memset(big, 'x', ROOKERY_BODY_MAX + 1u);
    rookery_init(&ROOK, &LINK);
    CHECK(rookery_compose(&ROOK, "ab", "Seeds", "x", false, 0, 0) == ZXM_E_ARG &&
              rookery_compose(&ROOK, "ada@hoard.example", "Seeds", big, false, 0, 0) == ZXM_E_ARG &&
              rookery_compose(&ROOK, "ada@hoard.example", "Two\nlines", "x", false, 0, 0) ==
                  ZXM_E_ARG &&
              rookery_compose(&ROOK, "ada@hoard.example", "Seeds", "x", false, -1, 0) ==
                  ZXM_E_ARG &&
              rookery_compose(&ROOK, "ada@hoard.example", NULL, "x", false, 0, 0) == ZXM_E_ARG &&
              !ROOK.composed,
          "a bad address, a body past 1837 bytes, a subject of two lines, a negative amount");
    big[ROOKERY_BODY_MAX] = 0;
    CHECK(rookery_compose(&ROOK, "ada@hoard.example", "Seeds", big, false, 0, 0) == ZXM_OK &&
              ROOK.draft.size == 9 + 5 + 2 + ROOKERY_BODY_MAX,
          "a body of exactly 1837 bytes");
    CHECK(rookery_compose(&ROOK, "ada@hoard.example", "Seeds", "The rye is in.", false, 0, 0) ==
                  ZXM_OK &&
              ROOK.composed && ROOK.draft.size == 30 &&
              !strcmp(ROOK.payload, "Subject: Seeds\n\nThe rye is in.") &&
              ROOK.draft.folder == ROOKERY_DRAFTS,
          "a letter composed: %s", ROOK.payload);
    CHECK(rookery_compose(&ROOK, "a@b", "x", "y", false, ROOKERY_AMOUNT_MAX + 1, 0) == ZXM_E_ARG &&
              !strcmp(ROOK.draft.subject, "Seeds"),
          "an amount past 18 digits: the letter before kept");

    /* RK2: the recipient's card, on a card zxvd made from a test seed */
    {
        char seed[65];
        memset(seed, '7', 64);
        seed[64] = 0;
        zxm_begin(&LINK, "web4.agent_card");
        zxm_p_str(&LINK, "seed_hex", seed);
        zxm_p_str(&LINK, "name", "ada");
        zxm_p_u64(&LINK, "bound_ms", 1700000000000u);
        CHECK(zxm_call(&LINK) == ZXM_OK &&
                  !zxm_get_str(&LINK, LINK.result, "pk_hex", ROOK_CARD.pk_hex,
                               sizeof ROOK_CARD.pk_hex) &&
                  !zxm_get_str(&LINK, LINK.result, "bind_sig_hex", ROOK_CARD.sig_hex,
                               sizeof ROOK_CARD.sig_hex) &&
                  !zxm_get_str(&LINK, LINK.result, "agent_id", ROOK_CARD.agent_id,
                               sizeof ROOK_CARD.agent_id),
              "a card to check: %s", LINK.err);
        memcpy(ROOK_CARD.name, "ada", 4);
        ROOK_CARD.bound_ms = 1700000000000u;
    }
    before = sent;
    CHECK(rookery_send(&ROOK, ROOK_CARD.agent_id, false, NOW) == ZXM_E_STATE && sent == before,
          "no card checked yet: KR1 does not hold, nothing sent");
    CHECK(rookery_check_card(&ROOK, &ROOK_CARD) == ZXM_OK && ROOK.verified == 1 &&
              !strcmp(ROOK.card_id, ROOK_CARD.agent_id),
          "ada's card verifies: %s", LINK.err);
    {
        rookery_card_t other = ROOK_CARD;
        memcpy(other.name, "bea", 4);
        CHECK(rookery_check_card(&ROOK, &other) == ZXM_OK && ROOK.verified == 0 && ROOK.reason[0],
              "another name on ada's key: not valid (%s)", ROOK.reason);
        before = sent;
        memcpy(other.name, "Bea", 4);
        CHECK(rookery_check_card(&ROOK, &other) == ZXM_E_ARG && sent == before,
              "a name zxvd would refuse: nothing sent");
        other = ROOK_CARD;
        other.agent_id[3] = 'g';
        CHECK(rookery_check_card(&ROOK, &other) == ZXM_E_ARG &&
                  rookery_check_card(&ROOK, NULL) == ZXM_E_ARG && sent == before,
              "an agent_id that is no hex: nothing sent");
    }
    CHECK(rookery_check_card(&ROOK, &ROOK_CARD) == ZXM_OK && ROOK.verified == 1, "checked again");

    /* RK5: the Hoard's folders, a draft kept and listed */
    CHECK(zxf_use_space(NULL) == 0, "home");
    CHECK(rookery_folders(&ROOK) == ZXM_OK && rookery_folders(&ROOK) == ZXM_OK,
          "Rookery and its five folders, made once: %s", LINK.err);
    CHECK(rookery_keep(&ROOK, ROOKERY_DRAFTS, "seeds") == ZXM_OK, "the draft kept: %s", LINK.err);
    CHECK(rookery_list(&ROOK, ROOKERY_DRAFTS, NOW) == ZXM_OK && ROOK.nfiles == 1 &&
              !strcmp(ROOK.files[0].name, "seeds") && ROOK.files[0].days == -1 &&
              ROOK.files[0].size ==
                  (uint32_t) strlen("To: ada@hoard.example\nThread: 0\nPersonal: no\nAmount: 0\n"
                                    "Subject: Seeds\n\nThe rye is in."),
          "the drafts: %u, %s %u bytes", (unsigned) ROOK.nfiles, ROOK.files[0].name,
          (unsigned) ROOK.files[0].size);
    {
        static const uint8_t junk[3] = {'x', 'y', 'z'};
        uint64_t size = 0;
        CHECK(zxf_write("Rookery/Drafts/notes.txt", junk, 3, ZXF_W_REPLACE, false, &size) == 0 &&
                  zxf_mkdir("Rookery/Drafts/old") == 0 &&
                  rookery_list(&ROOK, ROOKERY_DRAFTS, NOW) == ZXM_OK && ROOK.nfiles == 1,
              "a file that is no letter and a folder are not Rookery's");
    }
    CHECK(rookery_keep(&ROOK, ROOKERY_TRASH, "old-seeds") == ZXM_OK &&
              rookery_list(&ROOK, ROOKERY_TRASH, NOW) == ZXM_OK && ROOK.nfiles == 1,
          "a letter in the Trash: %s", LINK.err);
    int64_t put = ROOK.files[0].mtime;
    CHECK(rookery_list(&ROOK, ROOKERY_TRASH, put) == ZXM_OK &&
              ROOK.files[0].days == ROOKERY_TRASH_DAYS &&
              rookery_list(&ROOK, ROOKERY_TRASH, put + 29 * 86400) == ZXM_OK &&
              ROOK.files[0].days == 1 &&
              rookery_list(&ROOK, ROOKERY_TRASH, put + 31 * 86400) == ZXM_OK &&
              ROOK.files[0].days == 0,
          "the Trash's days left (KR6): %lld", (long long) ROOK.files[0].days);
    before = sent;
    CHECK(rookery_keep(&ROOK, ROOKERY_FOLDERS, "x") == ZXM_E_ARG &&
              rookery_keep(&ROOK, ROOKERY_DRAFTS, ".hidden") == ZXM_E_ARG &&
              rookery_keep(&ROOK, ROOKERY_DRAFTS, "Up") == ZXM_E_ARG &&
              rookery_list(&ROOK, ROOKERY_FOLDERS, NOW) == ZXM_E_ARG &&
              rookery_list(&ROOK, ROOKERY_INBOX, -1) == ZXM_E_ARG && sent == before,
          "no such folder, a hidden or upper-case name, no time: nothing sent");
    CHECK(rookery_list(&ROOK, ROOKERY_INBOX, NOW) == ZXM_OK && ROOK.nfiles == 0, "an empty inbox");

    /* RK3: posting, as zxvd_web4.c answers web4.post */
    ROOK.link = &FAKE;
    uint32_t seq = FAKE.seq;
    CHECK(rookery_send(&ROOK, ROOK_A, false, NOW) == ZXM_E_STATE &&
              rookery_send(&ROOK, "ab", false, NOW) == ZXM_E_ARG &&
              rookery_send(&ROOK, ROOK_CARD.agent_id, false, -2) == ZXM_E_ARG && FAKE.seq == seq,
          "to a companion whose card was not checked: nothing sent");
    CANNED =
        ROOK_RESULT(ROOK_ENTRY(7, "REQUEST", "signed", ROOK_A, "mail", 7,
                               "Subject: Seeds\\n\\nThe rye is in.", "false", "0", ROOK_SIGNED));
    CHECK(rookery_send(&ROOK, ROOK_CARD.agent_id, false, NOW) == ZXM_OK && ROOK.posted == 7 &&
              ROOK.posted_status == ROOKERY_ST_SIGNED && ROOK.draft.folder == ROOKERY_SENT &&
              strstr(FAKE.req, "\"web4.post\"") && strstr(FAKE.req, "\"kind\":\"REQUEST\"") &&
              strstr(FAKE.req, "\"tool\":\"mail\"") &&
              strstr(FAKE.req, "\"payload\":\"Subject: Seeds\\n\\nThe rye is in.\"") &&
              !strstr(FAKE.req, "\"pii\"") && !strstr(FAKE.req, "req_id") &&
              strstr(FAKE.req, "\"now\":1760000000"),
          "letter 7 posted and signed: %s", FAKE.req);
    CHECK(rookery_notice(&ROOK) == ZXM_E_STATE, "a signed letter has no notice");

    /* a reply in thread 7 with personal data: held until the owner consents */
    CHECK(rookery_compose(&ROOK, "ada@hoard.example", "Re: Seeds", "My address is 3 Rye Lane.",
                          true, 0, 7) == ZXM_OK,
          "a reply composed");
    seq = FAKE.seq;
    CHECK(rookery_send(&ROOK, ROOK_CARD.agent_id, false, NOW) == ZXM_E_STATE && FAKE.seq == seq,
          "personal data with no consent: KR1 does not hold, nothing sent");
    CANNED = ROOK_RESULT(
        ROOK_ENTRY(8, "RESPONSE", "signed", ROOK_A, "mail", 7, "x", "true", "0", ROOK_SIGNED));
    CHECK(rookery_send(&ROOK, ROOK_CARD.agent_id, true, NOW) == ZXM_E_PROTOCOL && ROOK.posted == 7,
          "zxvd signing personal data at once is not its rule: refused");
    CANNED = ROOK_RESULT(
        ROOK_ENTRY(8, "RESPONSE", "held", ROOK_A, "mail", 7, "x", "true", "0", ROOK_UNSIGNED));
    CHECK(rookery_send(&ROOK, ROOK_CARD.agent_id, true, NOW) == ZXM_OK && ROOK.posted == 8 &&
              ROOK.posted_status == ROOKERY_ST_HELD && ROOK.draft.folder == ROOKERY_HELD &&
              strstr(FAKE.req, "\"kind\":\"RESPONSE\"") && strstr(FAKE.req, "\"req_id\":7") &&
              strstr(FAKE.req, "\"pii\":true"),
          "letter 8 posted, held for consent: %s", FAKE.req);
    CHECK(rookery_notice(&ROOK) == ZXM_OK && strstr(ROOK.notice, "Letter 8 to ada@hoard.example") &&
              strstr(ROOK.notice, "personal data") && strstr(ROOK.notice, "Nothing is signed"),
          "the notice: %s", ROOK.notice);
    seq = FAKE.seq;
    CHECK(rookery_consent(&ROOK, 8, true, 600, NOW, false) == ZXM_E_STATE &&
              rookery_consent(&ROOK, 8, true, 0, NOW, true) == ZXM_E_ARG &&
              rookery_consent(&ROOK, 8, true, ROOKERY_GRANT_MAX + 1, NOW, true) == ZXM_E_ARG &&
              rookery_consent(&ROOK, 8, true, 600, -1, true) == ZXM_E_ARG &&
              rookery_consent(&ROOK, 9, true, 600, NOW, true) == ZXM_E_ARG && FAKE.seq == seq,
          "never by itself, never past a day: nothing sent");
    CANNED = ROOK_RESULT("{\"envelope\":" ROOK_ENTRY(8, "RESPONSE", "denied", ROOK_A, "mail", 7, "",
                                                     "true", "0", ROOK_UNSIGNED) "}");
    CHECK(rookery_consent(&ROOK, 8, true, 600, NOW, true) == ZXM_E_PROTOCOL &&
              ROOK.posted_status == ROOKERY_ST_HELD,
          "a grant answered with a denial: refused");
    CANNED = ROOK_RESULT("{\"envelope\":" ROOK_ENTRY(8, "RESPONSE", "signed", ROOK_A, "mail", 7,
                                                     "x", "true", "0", ROOK_SIGNED) "}");
    CHECK(rookery_consent(&ROOK, 8, true, 600, NOW, true) == ZXM_OK &&
              ROOK.posted_status == ROOKERY_ST_SIGNED && ROOK.draft.folder == ROOKERY_SENT &&
              strstr(FAKE.req, "\"web4.consent\"") && strstr(FAKE.req, "\"grant\":true") &&
              strstr(FAKE.req, "\"expires_in\":600"),
          "the owner granted letter 8: %s", FAKE.req);
    CHECK(rookery_consent(&ROOK, 8, true, 600, NOW, true) == ZXM_E_STATE,
          "a signed letter is answered already");

    /* RK4: the feed, letters only, filed by folder and thread */
    CANNED = ROOK_FEED(
        ROOK_ENTRY(9, "REQUEST", "signed", ROOK_B, "mail", 9, "Subject: Hello\\n\\nFrom bea.", "false", "0", ROOK_SIGNED) "," ROOK_ENTRY(
            8, "RESPONSE", "signed", ROOK_A, "mail", 7, "Subject: Re: Seeds\\n\\nMy address.",
            "true", "0",
            ROOK_SIGNED) "," ROOK_ENTRY(7, "REQUEST", "signed", ROOK_A, "mail", 7,
                                        "Subject: "
                                        "Seeds\\n\\nThe "
                                        "rye is in.",
                                        "false", "0",
                                        ROOK_SIGNED) "," ROOK_ENTRY(6, "REQUEST", "signed", ROOK_A,
                                                                    "chat", 6, "hi", "false", "0",
                                                                    ROOK_SIGNED) "," ROOK_ENTRY(5,
                                                                                                "RE"
                                                                                                "QU"
                                                                                                "ES"
                                                                                                "T",
                                                                                                "he"
                                                                                                "l"
                                                                                                "d",
                                                                                                ROOK_A,
                                                                                                "ma"
                                                                                                "i"
                                                                                                "l",
                                                                                                5,
                                                                                                "no"
                                                                                                " s"
                                                                                                "ub"
                                                                                                "je"
                                                                                                "ct"
                                                                                                " h"
                                                                                                "er"
                                                                                                "e",
                                                                                                "tr"
                                                                                                "u"
                                                                                                "e",
                                                                                                "0",
                                                                                                ROOK_UNSIGNED),
        "4");
    uint32_t ids[4] = {0, 0, 0, 0};
    CHECK(rookery_feed(&ROOK, 0, 5, ROOK_A) == ZXM_OK && ROOK.n == 4 && ROOK.next == 4 &&
              ROOK.page[0].id == 9 && ROOK.page[0].folder == ROOKERY_INBOX &&
              !strcmp(ROOK.page[0].subject, "Hello") && !strcmp(ROOK.page[0].body, "From bea.") &&
              ROOK.page[1].id == 8 && ROOK.page[1].thread == 7 &&
              ROOK.page[1].folder == ROOKERY_SENT && ROOK.page[1].pii && ROOK.page[2].id == 7 &&
              ROOK.page[3].id == 5 && ROOK.page[3].folder == ROOKERY_HELD &&
              !ROOK.page[3].subject[0] && !strcmp(ROOK.page[3].body, "no subject here") &&
              ROOK.page[3].stamp == -1 && strstr(FAKE.req, "\"web4.feed\"") &&
              strstr(FAKE.req, "\"limit\":5"),
          "a page: four letters, the chat left out: %s", FAKE.err);
    CHECK(rookery_thread(&ROOK, 7, ids, 4) == 2 && ids[0] == 8 && ids[1] == 7 &&
              rookery_thread(&ROOK, 9, ids, 4) == 1 && rookery_thread(&ROOK, 3, ids, 4) == 0 &&
              rookery_thread(&ROOK, 0, ids, 4) == 0 && rookery_thread(&ROOK, 7, NULL, 0) == 2,
          "thread 7 holds letters 8 and 7");
    CANNED = ROOK_FEED(ROOK_ENTRY(4, "RESPONSE", "signed", ROOK_A, "mail", 99999999999, "x",
                                  "false", "0", ROOK_SIGNED),
                       "null");
    CHECK(rookery_feed(&ROOK, 0, 5, ROOK_A) == ZXM_E_PROTOCOL && ROOK.n == 4 &&
              ROOK.page[0].id == 9,
          "a thread past a feed id is no ^ZXROOKERY record: the page before kept");
    CANNED = ROOK_FEED(
        ROOK_ENTRY(4, "REQUEST", "signed", ROOK_A, "mail", 3, "x", "false", "0", ROOK_SIGNED),
        "null");
    CHECK(rookery_feed(&ROOK, 0, 5, ROOK_A) == ZXM_E_PROTOCOL && ROOK.n == 4,
          "a request that begins another's thread: refused");
    CANNED =
        ROOK_FEED(ROOK_ENTRY(3, "REQUEST", "signed", ROOK_A, "chat", 3, "x", "false", "0",
                             ROOK_SIGNED) "," ROOK_ENTRY(4, "REQUEST", "signed", ROOK_A, "chat", 4,
                                                         "x", "false", "0", ROOK_SIGNED),
                  "null");
    CHECK(rookery_feed(&ROOK, 0, 5, ROOK_A) == ZXM_E_PROTOCOL && ROOK.n == 4,
          "oldest first, even of what is not a letter: refused");
    CANNED = ROOK_FEED(
        ROOK_ENTRY(4, "REQUEST", "signed", ROOK_A, "mail", 4, "x", "false", "0", ROOK_UNSIGNED),
        "null");
    CHECK(rookery_feed(&ROOK, 0, 5, ROOK_A) == ZXM_E_PROTOCOL && ROOK.n == 4,
          "signed with no time");
    seq = FAKE.seq;
    CHECK(rookery_feed(&ROOK, 0, 0, ROOK_A) == ZXM_E_ARG &&
              rookery_feed(&ROOK, 0, 51, ROOK_A) == ZXM_E_ARG &&
              rookery_feed(&ROOK, 0, 5, "xyz") == ZXM_E_ARG && FAKE.seq == seq,
          "a bad page or owner: nothing sent");

    /* RK4: a letter's signature, as web4.verify gives it */
    CANNED = ROOK_RESULT("{\"valid\":true,\"kind\":\"REQUEST\",\"consent\":false}");
    CHECK(rookery_verify(&ROOK, 9) == ZXM_OK && ROOK.sig_valid &&
              strstr(FAKE.req, "\"web4.verify\""),
          "letter 9 verifies: %s", FAKE.err);
    CANNED = ROOK_RESULT("{\"valid\":false,\"reason\":\"signature\"}");
    CHECK(rookery_verify(&ROOK, 9) == ZXM_OK && !ROOK.sig_valid &&
              !strcmp(ROOK.reason, "signature"),
          "a signature that does not check: %s", ROOK.reason);
    CANNED = ROOK_RESULT("{\"valid\":false}");
    CHECK(rookery_verify(&ROOK, 9) == ZXM_E_PROTOCOL, "not valid with no reason: refused");
    seq = FAKE.seq;
    CHECK(rookery_verify(&ROOK, 5) == ZXM_E_STATE && rookery_verify(&ROOK, 2) == ZXM_E_ARG &&
              FAKE.seq == seq,
          "a held letter has no signature yet; one not on the page is not asked for");

    /* RK3 on the page: the owner denies held letter 5; its words are wiped */
    CANNED = ROOK_RESULT("{\"envelope\":" ROOK_ENTRY(5, "REQUEST", "denied", ROOK_A, "mail", 5, "",
                                                     "true", "0", ROOK_UNSIGNED) "}");
    CHECK(rookery_consent(&ROOK, 5, false, 600, NOW, true) == ZXM_OK &&
              ROOK.page[3].status == ROOKERY_ST_DENIED && ROOK.page[3].folder == ROOKERY_TRASH &&
              !ROOK.page[3].body[0] && strstr(FAKE.req, "\"grant\":false"),
          "the owner denied letter 5: %s", FAKE.req);

    /* RK6: what an account would hold after a letter's money, on scratch books */
    ROOK.link = &LINK;
    {
        char root[256], scratch[512], path[COFFER_PATH_MAX + 1];
        bool saved = false;
        uint32_t payer = 0, cash = 0;
        uint64_t entry = 0;
        CHECK(rookery_money_after(&ROOK, 1) == ZXM_E_ARG,
              "a letter that moves no money: not asked");
        CHECK(rookery_compose(&ROOK, "ada@hoard.example", "For the seeds", "Paid.", false, 2500,
                              7) == ZXM_OK,
              "a letter that moves 2500");
        CHECK(zxf_root(root, sizeof root) == 0, "the test's folder");
        snprintf(scratch, sizeof scratch, "%s/rookery-scratch.books", root);
        zxvd_rpc_books_close();
        CHECK(zxvd_rpc_books_file(scratch) == 0 && zxvd_rpc_books_open() == 0,
              "scratch books open");
        coffer_init(&ROOKC, &LINK, COFFER_DAY_CAP);
        CHECK(coffer_books(&ROOKC, path, sizeof path, &saved) == ZXM_OK && saved &&
                  !strcmp(path, scratch) && coffer_enable(&ROOKC, "USD") == ZXM_OK &&
                  coffer_open_account(&ROOKC, "payer", "USD", true, &payer) == ZXM_OK &&
                  coffer_open_account(&ROOKC, "cash", "USD", false, &cash) == ZXM_OK &&
                  coffer_propose(&ROOKC, payer, cash, 10000, "in") == ZXM_OK &&
                  coffer_confirm(&ROOKC, &entry) == ZXM_OK && coffer_load(&ROOKC) == ZXM_OK,
              "cash holds 10000: %s", LINK.err);
        const coffer_account_t *ca = coffer_account(&ROOKC, cash);
        CHECK(ca && rookery_money_after(&ROOK, cash) == ZXM_OK && ROOK.balance == ca->balance &&
                  ROOK.after == coffer_balance(ca->balance, -2500),
              "after the letter: %lld of %lld", (long long) ROOK.after, (long long) ROOK.balance);
        zxvd_rpc_books_close();
        CHECK(zxvd_rpc_books_file("") == 0 && remove(scratch) == 0, "scratch books removed");
    }

    /* the Hoard as it was: Rookery into the Trash, then out of it for good */
    {
        char trashed[512], gone[8];
        CHECK(zxf_trash("Rookery", trashed, sizeof trashed) == 0 &&
                  zxf_trash(trashed, gone, sizeof gone) == 0 && !gone[0],
              "Rookery removed: %s", trashed);
    }
}
