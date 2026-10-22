/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Lifeboat's model against zxvd's real dispatcher (LINK), on scratch state
 * only: two engines of its own behind feature switches (one plain, one
 * regulated) added through zxvd_rpc_add_group, a scratch journal folder, a
 * scratch books file in the test's own folder and a scratch Keepsake
 * destination, never the real ones. A journal that breaks, a disk that is
 * no disk and update.check (not linked in this in-process zxvd) are
 * answered on a link of its own (LFB_LINK), which passes every other
 * request to the real dispatcher. Every switch that was on before is put
 * back on at the end. */
#include "lifeboat_model.h"

static lifeboat_t LFB, LFB2;
static zxm_link_t LFB_LINK;
static const char *LFB_JOURNAL; /* journal.verify's result, when set */
static const char *LFB_DISK;    /* sys.disk's result, when set */
static const char *LFB_UPDATE;  /* update.check's result, when set */
static const char *LFB_SET;     /* features.set's result, when set */

static int32_t lfb_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    const char *r = NULL;
    if (strstr(req, "\"journal.verify\"")) r = LFB_JOURNAL;
    if (strstr(req, "\"sys.disk\"")) r = LFB_DISK;
    if (strstr(req, "\"update.check\"")) r = LFB_UPDATE;
    if (strstr(req, "\"features.set\"")) r = LFB_SET;
    if (r) return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq, r);
    return inproc(NULL, req, len, resp, cap);
}

static void lfb_ping(zxvd_call_t *c)
{
    zxvd_buf_raw(zxvd_res(c), "{\"ok\":true}", 11);
}

static const zxvd_group_method_t LFB_PLAIN_M[] = {{"lbplain.ping", lfb_ping, false}};
static const zxvd_group_method_t LFB_MONEY_M[] = {{"lbmoney.ping", lfb_ping, false}};
static const zxvd_group_t LFB_PLAIN = {"lbplain", false, "a plain engine", LFB_PLAIN_M, 1, NULL};
static const zxvd_group_t LFB_MONEY = {"lbmoney", true, "a regulated engine", LFB_MONEY_M, 1, NULL};

#define LFB_JV(ok, broken, why)                                                                    \
    "{\"on\":true,\"ok\":" ok ",\"segments\":2,\"entries\":40,\"first_seq\":1,\"last_seq\":40,"    \
    "\"broken_at\":" broken ",\"why\":\"" why "\"}"

APPS_TEST(lifeboat)
{
    /* the rules (LFB1-LFB6) */
    CHECK(lifeboat_start_ok(1, 1, 1, 1) && !lifeboat_start_ok(1, 1, 0, 1) &&
              !lifeboat_start_ok(0, 1, 9, 1) && !lifeboat_start_ok(1, 2, 9, 1) &&
              lifeboat_start_n(1, 1, 9, 1) == 1 && lifeboat_start_n(1, 0, 9, 1) == 0 &&
              lifeboat_start_n(1, 1, 9, 3) == LIFEBOAT_REFUSED,
          "lifeboat: safe to start only with every check passed");
    CHECK(lifeboat_repair_step(1, 1, 0, 0) == LIFEBOAT_STEP_SPACE &&
              lifeboat_repair_step(1, 0, 9, 0) == LIFEBOAT_STEP_TEST &&
              lifeboat_repair_step(0, 0, 9, 1) == LIFEBOAT_STEP_RESTORE &&
              lifeboat_repair_step(0, 1, 9, 1) == LIFEBOAT_STEP_JOURNAL &&
              lifeboat_repair_step(1, 1, 9, 1) == LIFEBOAT_STEP_NONE,
          "lifeboat: the next repair step, the first that applies");
    CHECK(lifeboat_restore_fits(100, 300, 200) && !lifeboat_restore_fits(101, 300, 200) &&
              !lifeboat_restore_fits(1, LIFEBOAT_BYTES_MAX, LIFEBOAT_BYTES_MAX),
          "lifeboat: a restore fits with the floor kept free");
    CHECK(lifeboat_record_fits(1, 0, 1, 1, 1, -5, 1700000000000) &&
              !lifeboat_record_fits(1000000000, 0, 1, 1, 1, 0, 0) &&
              !lifeboat_record_fits(1, 10, 1, 1, 1, 0, 0),
          "lifeboat: a ^ZXLIFEBOAT record's digits");

    /* scratch state: a Keepsake destination, a journal folder, files in the Hoard */
    char dest[256], jdir[256], root[256], scratch[512];
    const char *tmp = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
    snprintf(dest, sizeof dest, "%s/zxv-lifeboat-XXXXXX", tmp);
    snprintf(jdir, sizeof jdir, "%s/zxv-lifeboat-j-XXXXXX", tmp);
    uint64_t size = 0;
    CHECK(mkdtemp(dest) != NULL && mkdtemp(jdir) != NULL && zxf_root(root, sizeof root) == 0,
          "lifeboat: scratch folders");
    snprintf(scratch, sizeof scratch, "%s/lifeboat-scratch.books", root);
    CHECK(zxf_mkdir("lifeboat") == 0 &&
              zxf_write("lifeboat/plan.txt", (const uint8_t *) "keep afloat", 11, ZXF_W_REPLACE,
                        false, &size) == 0,
          "lifeboat: a file to save");

    /* two engines of our own, turned on */
    CHECK(zxvd_rpc_add_group(&LFB_PLAIN) == 0 && zxvd_rpc_add_group(&LFB_MONEY) == 0 &&
              raw_call("features.set", "{\"feature\":\"lbplain\",\"enabled\":true}") == ZXM_OK &&
              raw_call("features.set", "{\"feature\":\"lbmoney\",\"enabled\":true}") == ZXM_OK &&
              zxvd_rpc_enabled("lbplain") && zxvd_rpc_enabled("lbmoney"),
          "lifeboat: two engines on: %s", LINK.err);

    /* LM1: nothing without the owner's word, nothing out of order */
    lifeboat_init(&LFB, &LINK);
    static char longp[LIFEBOAT_PATH + 2];
    memset(longp, 'p', LIFEBOAT_PATH + 1);
    int before = sent;
    CHECK(lifeboat_safe_start(&LFB, false) == ZXM_E_STATE &&
              lifeboat_restore(&LFB, NULL, NULL, false) == ZXM_E_STATE &&
              lifeboat_restore(&LFB, "lifeboat/plan.txt", NULL, true) == ZXM_E_STATE &&
              lifeboat_restore(&LFB, longp, NULL, true) == ZXM_E_ARG &&
              lifeboat_restore(&LFB, NULL, longp, true) == ZXM_E_ARG &&
              lifeboat_set_dest(&LFB, NULL) == ZXM_E_ARG &&
              lifeboat_set_dest(&LFB, "relative") == ZXM_E_ARG &&
              lifeboat_set_dest(&LFB, longp) == ZXM_E_ARG &&
              lifeboat_set_floor(&LFB, -1) == ZXM_E_ARG &&
              lifeboat_set_floor(&LFB, LIFEBOAT_FLOOR_MAX + 1) == ZXM_E_ARG &&
              LFB.floor == LIFEBOAT_FLOOR && !LFB.dest[0] && sent == before &&
              zxvd_rpc_enabled("lbplain"),
          "lifeboat: no approval, no check, bad input: nothing sent");

    /* LM2: safe start, every switch off */
    CHECK(lifeboat_switches(&LFB) == ZXM_OK && LFB.listed && !LFB.safe &&
              lifeboat_find(&LFB, "lbplain") && lifeboat_find(&LFB, "lbplain")->enabled &&
              lifeboat_find(&LFB, "lbmoney")->regulated && !lifeboat_find(&LFB, "nothing"),
          "lifeboat: the switches: %s", LINK.err);
    static char was_on[LIFEBOAT_SWITCHES][LIFEBOAT_NAME_MAX + 1];
    uint32_t non = 0;
    for (uint32_t i = 0; i < LFB.nsw; i++)
        if (LFB.sw[i].enabled && strncmp(LFB.sw[i].name, "lb", 2))
            memcpy(was_on[non++], LFB.sw[i].name, LIFEBOAT_NAME_MAX + 1);
    CHECK(lifeboat_safe_start(&LFB, true) == ZXM_OK && LFB.safe && LFB.switched_off == non + 2 &&
              !zxvd_rpc_enabled("lbplain") && !zxvd_rpc_enabled("lbmoney") &&
              !lifeboat_find(&LFB, "lbmoney")->enabled && strstr(LFB.notice, "safe start turned "),
          "lifeboat: every switch off: %s %s", LFB.notice, LINK.err);
    CHECK(raw_call("lbplain.ping", "{}") == ZXM_E_REMOTE &&
              lifeboat_safe_start(&LFB, true) == ZXM_OK && LFB.switched_off == 0,
          "lifeboat: an engine off does not answer; nothing left to turn off");

    /* LM3: a check with nothing set up: no journal, books closed, no destination */
    zxvd_rpc_books_close();
    CHECK(lifeboat_set_floor(&LFB, 0) == ZXM_OK && lifeboat_check(&LFB) == ZXM_OK &&
              LFB.check.done && !LFB.check.journal_on && LFB.check.journal == 1 &&
              !LFB.check.books_ok && LFB.check.books == 0 && LFB.check.books_why[0] &&
              LFB.check.tested == 0 &&
              !strcmp(LFB.check.backup_why, "no Keepsake destination set") && LFB.check.total > 0 &&
              LFB.check.room >= 1 && LFB.check.used_x100 >= 0 && LFB.check.used_x100 <= 10000 &&
              LFB.check.step == LIFEBOAT_STEP_TEST && !LFB.check.start_ok && LFB.nrec == 1 &&
              LFB.rec[0].seq == 1 && LFB.rec[0].step == LIFEBOAT_STEP_TEST &&
              LFB.rec[0].stamp > 0 && lifeboat_record(&LFB, 0) && !lifeboat_record(&LFB, 1) &&
              strstr(LFB.notice, "Not safe to start yet.") &&
              strstr(LFB.notice, "Journal: zxvd keeps none") &&
              strstr(LFB.notice, "Next: test a backup"),
          "lifeboat: nothing set up: test a backup first: %s %s", LFB.notice, LINK.err);
    before = sent;
    CHECK(lifeboat_restore(&LFB, NULL, NULL, true) == ZXM_E_STATE && sent == before,
          "lifeboat: no tested backup: nothing restored");

    /* a scratch journal, scratch books and a backup: safe to start */
    CHECK(zjr_open(jdir) == 0, "lifeboat: a scratch journal");
    CHECK(zxvd_rpc_books_file(scratch) == 0 && zxvd_rpc_books_open() == 0,
          "lifeboat: scratch books open");
    char params[600];
    snprintf(params, sizeof params, "{\"dest\":\"%s\"}", dest);
    CHECK(lifeboat_set_dest(&LFB, dest) == ZXM_OK && raw_call("backup.run", params) == ZXM_OK,
          "lifeboat: a backup to test: %s", LINK.err);
    CHECK(lifeboat_check(&LFB) == ZXM_OK && LFB.check.journal_on && LFB.check.journal_ok &&
              LFB.check.journal == 1 && LFB.check.books_ok && LFB.check.books == 1 &&
              LFB.check.snapshots == 1 && LFB.check.snapshot == 1 && LFB.check.tested == 1 &&
              LFB.check.snap_bytes > 0 && LFB.check.bad == 0 && LFB.check.start_ok &&
              LFB.check.step == LIFEBOAT_STEP_NONE && LFB.nrec == 2 && LFB.rec[1].seq == 2 &&
              LFB.rec[1].tested == 1 && strstr(LFB.notice, "Safe to start normally.") &&
              strstr(LFB.notice, "backup 1 reads back whole") &&
              strstr(LFB.notice, "Next: start normally"),
          "lifeboat: safe to start: %s %s", LFB.notice, LINK.err);
    CHECK(strstr(LINK.req, "\"backup.verify\"") && strstr(LINK.req, "\"snapshot\":1"),
          "lifeboat: the newest snapshot verified: %s", LINK.req);

    /* LM4: a restore, only with the owner's word, always a new item */
    before = sent;
    CHECK(lifeboat_restore(&LFB, "lifeboat/plan.txt", NULL, false) == ZXM_E_STATE && sent == before,
          "lifeboat: no approval, no restore");
    CHECK(zxf_write("lifeboat/plan.txt", (const uint8_t *) "sinking", 7, ZXF_W_REPLACE, false,
                    &size) == 0 &&
              lifeboat_restore(&LFB, "lifeboat/plan.txt", NULL, true) == ZXM_OK &&
              !strcmp(LFB.landed, "lifeboat/plan (restored).txt") && LFB.restored == 1 &&
              !LFB.check.done &&
              strstr(LFB.notice, "Restored 1 files as lifeboat/plan (restored).txt"),
          "lifeboat: restored beside it: %s %s", LFB.landed, LINK.err);
    char got[32];
    uint32_t gn = 0;
    CHECK(zxf_read("lifeboat/plan (restored).txt", 0, (uint8_t *) got, sizeof got, false, &gn,
                   &size) == 0 &&
              gn == 11 && !memcmp(got, "keep afloat", 11) &&
              zxf_read("lifeboat/plan.txt", 0, (uint8_t *) got, sizeof got, false, &gn, &size) ==
                  0 &&
              gn == 7 && !memcmp(got, "sinking", 7),
          "lifeboat: the saved words back, the file itself untouched");
    before = sent;
    CHECK(lifeboat_restore(&LFB, "lifeboat/plan.txt", NULL, true) == ZXM_E_STATE && sent == before,
          "lifeboat: the check is stale after a restore: check again first");
    CHECK(lifeboat_check(&LFB) == ZXM_OK && LFB.check.start_ok &&
              lifeboat_restore(&LFB, "lifeboat", "lifeboat2", true) == ZXM_OK &&
              !strcmp(LFB.landed, "lifeboat2") && LFB.restored == 1 &&
              strstr(LINK.req, "\"to\":\"lifeboat2\""),
          "lifeboat: a folder somewhere new: %s", LINK.err);
    CHECK(lifeboat_check(&LFB) == ZXM_OK &&
              lifeboat_restore(&LFB, "lifeboat", "lifeboat2", true) == ZXM_E_REMOTE &&
              LINK.err_code == -32014 && !strcmp(LFB.landed, "lifeboat2"),
          "lifeboat: never over an item: %s", LINK.err);

    /* LFB5: a restore that does not fit above the floor is never asked */
    CHECK(lifeboat_set_floor(&LFB, LIFEBOAT_FLOOR_MAX) == ZXM_OK && !LFB.check.done &&
              lifeboat_check(&LFB) == ZXM_OK && LFB.check.room < 1 &&
              LFB.check.step == LIFEBOAT_STEP_SPACE && !LFB.check.start_ok &&
              strstr(LFB.notice, "at or under the floor") && strstr(LFB.notice, "Next: free space"),
          "lifeboat: under the floor: free space first: %s", LFB.notice);
    before = sent;
    CHECK(lifeboat_restore(&LFB, NULL, NULL, true) == ZXM_E_STATE && sent == before,
          "lifeboat: does not fit: nothing asked");
    CHECK(lifeboat_set_floor(&LFB, 0) == ZXM_OK, "lifeboat: the floor back");

    /* LM5: update.check is not linked here; on its own link it answers */
    CHECK(lifeboat_update_check(&LFB) == ZXM_E_REMOTE && !LFB.update[0],
          "lifeboat: no update host in this harness: %s", LINK.err);
    zxm_link_init(&LFB_LINK, lfb_send, &LFB_LINK);
    lifeboat_init(&LFB2, &LFB_LINK);
    LFB_UPDATE = "{\"status\":\"up to date\"}";
    CHECK(lifeboat_update_check(&LFB2) == ZXM_OK && !strcmp(LFB2.update, "up to date") &&
              strstr(LFB2.notice, "Update check: up to date (nothing installed)."),
          "lifeboat: an update check: %s", LFB2.notice);
    LFB_UPDATE = "{\"status\":\"\"}";
    CHECK(lifeboat_update_check(&LFB2) == ZXM_E_PROTOCOL && !strcmp(LFB2.update, "up to date"),
          "lifeboat: an empty status refused");
    LFB_UPDATE = NULL;

    /* LFB4 step 4: a broken journal, kept and told */
    CHECK(lifeboat_set_dest(&LFB2, dest) == ZXM_OK && lifeboat_set_floor(&LFB2, 0) == ZXM_OK,
          "lifeboat: a second view");
    LFB_JOURNAL = LFB_JV("false", "12", "line 12 was changed or is missing");
    CHECK(lifeboat_check(&LFB2) == ZXM_OK && LFB2.check.journal == 0 &&
              LFB2.check.broken_at == 12 && LFB2.check.books == 1 && LFB2.check.tested == 1 &&
              LFB2.check.step == LIFEBOAT_STEP_JOURNAL && !LFB2.check.start_ok &&
              strstr(LFB2.notice, "broken at 12 (line 12 was changed or is missing)") &&
              strstr(LFB2.notice, "Next: keep the broken journal"),
          "lifeboat: a broken journal: %s %s", LFB2.notice, LFB_LINK.err);
    static const char *const BAD_JV[] = {
        LFB_JV("true", "3", ""),  /* ok with a break */
        LFB_JV("true", "0", "x"), /* ok with a reason */
        LFB_JV("false", "3", ""), /* a break without a reason */
        "{\"on\":true}",          /* short */
    };
    for (uint32_t i = 0; i < sizeof BAD_JV / sizeof BAD_JV[0]; i++) {
        LFB_JOURNAL = BAD_JV[i];
        CHECK(lifeboat_check(&LFB2) == ZXM_E_PROTOCOL && LFB2.nrec == 1 &&
                  LFB2.check.step == LIFEBOAT_STEP_JOURNAL && LFB2.check.done,
              "lifeboat: journal reply %u refused, the last check kept", i);
    }
    LFB_JOURNAL = NULL;
    static const char *const BAD_DISK[] = {
        "{\"path\":\"/\",\"total\":10,\"free\":11,\"avail\":5}",
        "{\"path\":\"/\",\"total\":10,\"free\":5,\"avail\":11}",
        "{\"path\":\"/\",\"total\":-1,\"free\":0,\"avail\":0}",
        "{\"path\":\"/\",\"total\":10,\"free\":5}",
    };
    for (uint32_t i = 0; i < sizeof BAD_DISK / sizeof BAD_DISK[0]; i++) {
        LFB_DISK = BAD_DISK[i];
        CHECK(lifeboat_check(&LFB2) == ZXM_E_PROTOCOL && LFB2.nrec == 1,
              "lifeboat: disk reply %u refused", i);
    }
    LFB_DISK = "{\"path\":\"/\",\"total\":0,\"free\":0,\"avail\":0}";
    CHECK(lifeboat_check(&LFB2) == ZXM_OK && LFB2.check.used_x100 == LIFEBOAT_REFUSED &&
              LFB2.check.room == 0 && LFB2.check.step == LIFEBOAT_STEP_SPACE &&
              !strstr(LFB2.notice, "% used"),
          "lifeboat: a disk of nothing: free space: %s", LFB2.notice);
    LFB_DISK = "{\"path\":\"/\",\"total\":1000,\"free\":250,\"avail\":200}";
    CHECK(lifeboat_set_floor(&LFB2, 150) == ZXM_OK && lifeboat_check(&LFB2) == ZXM_OK &&
              LFB2.check.used_x100 == 7500 && LFB2.check.room == 50 &&
              strstr(LFB2.notice, "disk 75.00% used, above the floor") &&
              lifeboat_set_floor(&LFB2, 0) == ZXM_OK,
          "lifeboat: a small disk, its share through used_share's rule: %s", LFB2.notice);
    LFB_DISK = "{\"path\":\"/\",\"total\":2000000000000000000,\"free\":1500000000000000000,"
               "\"avail\":1500000000000000000}";
    CHECK(lifeboat_check(&LFB2) == ZXM_OK && LFB2.check.room == LIFEBOAT_BYTES_MAX &&
              LFB2.check.used_x100 == LIFEBOAT_REFUSED && LFB2.check.step == LIFEBOAT_STEP_NONE &&
              lifeboat_record(&LFB2, LFB2.nrec - 1u),
          "lifeboat: a room past 18 digits is held at 18: %s", LFB2.notice);
    LFB_DISK = NULL;

    /* LM2: a switch zxvd does not say back is not taken */
    CHECK(raw_call("features.set", "{\"feature\":\"lbplain\",\"enabled\":true}") == ZXM_OK,
          "lifeboat: lbplain on again");
    LFB_SET = "{\"feature\":\"lbother\",\"enabled\":false,\"regulated\":false,\"saved\":false}";
    CHECK(lifeboat_safe_start(&LFB2, true) == ZXM_E_PROTOCOL && !LFB2.safe &&
              lifeboat_find(&LFB2, "lbplain")->enabled,
          "lifeboat: another switch said back: not taken");
    LFB_SET = "{\"feature\":\"lbplain\",\"enabled\":true,\"regulated\":false,\"saved\":false}";
    CHECK(lifeboat_safe_start(&LFB2, true) == ZXM_E_PROTOCOL &&
              lifeboat_find(&LFB2, "lbplain")->enabled,
          "lifeboat: still on said back: not taken");
    LFB_SET = NULL;
    CHECK(lifeboat_safe_start(&LFB2, true) == ZXM_OK && !zxvd_rpc_enabled("lbplain") && LFB2.safe,
          "lifeboat: off for real");

    /* made up replies: nothing out of bounds changes what was shown */
    zxm_link_init(&FAKE, canned, &FAKE);
    LFB2.link = &FAKE;
    uint32_t n = LFB2.nsw;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"features\":[{\"name\":\"a\","
             "\"enabled\":true,\"regulated\":false},{\"name\":\"a\",\"enabled\":false,"
             "\"regulated\":false}],\"saved\":false}}";
    CHECK(lifeboat_switches(&LFB2) == ZXM_E_PROTOCOL && LFB2.nsw == n, "lifeboat: a switch twice");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"features\":[{\"name\":\"Bad Name\","
             "\"enabled\":true,\"regulated\":false}],\"saved\":false}}";
    CHECK(lifeboat_switches(&LFB2) == ZXM_E_PROTOCOL && LFB2.nsw == n, "lifeboat: no such name");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"to\":\"\",\"files\":1}}";
    LFB2.check.done = true; /* as if checked: a restore reply that names no place */
    CHECK(lifeboat_restore(&LFB2, NULL, NULL, true) == ZXM_E_PROTOCOL && !LFB2.landed[0],
          "lifeboat: a restore landing nowhere refused");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"to\":\"elsewhere\",\"files\":1}}";
    CHECK(lifeboat_restore(&LFB2, NULL, "here", true) == ZXM_E_PROTOCOL && !LFB2.landed[0],
          "lifeboat: a restore landing somewhere not asked refused");
    CANNED = NULL;
    LFB2.link = &LFB_LINK;

    /* LFB6: the records, the newest kept */
    for (uint32_t i = 0; i < LIFEBOAT_RECORDS + 2u; i++) (void) lifeboat_check(&LFB);
    CHECK(LFB.nrec == LIFEBOAT_RECORDS && LFB.rec[LIFEBOAT_RECORDS - 1u].seq == LFB.seq &&
              LFB.rec[0].seq == LFB.seq - (int64_t) LIFEBOAT_RECORDS + 1 &&
              lifeboat_record(&LFB, LIFEBOAT_RECORDS - 1u) &&
              !lifeboat_record(&LFB, LIFEBOAT_RECORDS),
          "lifeboat: %u records, the newest %lld", LFB.nrec, (long long) LFB.seq);

    /* clean up: the scratch journal, books and destination; switches as they were */
    zjr_close();
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file("") == 0 && remove(scratch) == 0, "lifeboat: scratch books removed");
    CHECK(rm_tree(jdir) == 0 && rm_tree(dest) == 0, "lifeboat: scratch folders removed");
    zxvd_rpc_remove_group(&LFB_MONEY);
    zxvd_rpc_remove_group(&LFB_PLAIN);
    bool back = true;
    for (uint32_t i = 0; i < non; i++) {
        zxm_begin(&LINK, "features.set");
        zxm_p_str(&LINK, "feature", was_on[i]);
        zxm_p_bool(&LINK, "enabled", true);
        back = zxm_call(&LINK) == ZXM_OK && back;
    }
    CHECK(back, "lifeboat: %u switches on again, as they were", non);
}
