/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Tideturn's model against zxvd's real dispatcher (LINK): zxvd.version and
 * sys.info as zxvd answers them. update.check is not linked in this
 * in-process zxvd, and its app version is "test", so a version and a check
 * are answered on a link of its own (TDT_LINK), which passes every other
 * request to the real dispatcher. Nothing is ever installed: Tideturn has
 * no way to. */
#include "tideturn_model.h"

static tideturn_t TDT, TDT2;
static zxm_link_t TDT_LINK;
static const char *TDT_VERSION; /* zxvd.version's result, when set */
static const char *TDT_UPDATE;  /* update.check's result, when set */

static int32_t tdt_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    const char *r = NULL;
    if (strstr(req, "\"zxvd.version\"")) r = TDT_VERSION;
    if (strstr(req, "\"update.check\"")) r = TDT_UPDATE;
    if (r) return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq, r);
    return inproc(NULL, req, len, resp, cap);
}

#define TDT_V(app, lib) "{\"app\":\"" app "\",\"protocol\":\"p\",\"libzxv\":\"" lib "\"}"

APPS_TEST(tideturn)
{
    tideturn_version_t v;
    int before;

    /* TM1: the version form */
    CHECK(tideturn_parse("1.2.3", &v) == ZXM_OK && v.major == 1 && v.minor == 2 && v.patch == 3 &&
              tideturn_parse("0.0.0", &v) == ZXM_OK && v.major == 0 &&
              tideturn_parse("999.999.999", &v) == ZXM_OK && v.patch == 999 &&
              tideturn_parse("10.200.30", &v) == ZXM_OK && v.minor == 200,
          "tideturn: versions that parse");
    CHECK(tideturn_parse("1000.0.0", &v) == ZXM_E_ARG &&
              tideturn_parse("01.0.0", &v) == ZXM_E_ARG && tideturn_parse("1.2", &v) == ZXM_E_ARG &&
              tideturn_parse("1.2.3.4", &v) == ZXM_E_ARG &&
              tideturn_parse("1..3", &v) == ZXM_E_ARG && tideturn_parse("1.2.", &v) == ZXM_E_ARG &&
              tideturn_parse("-1.2.3", &v) == ZXM_E_ARG &&
              tideturn_parse("1.2.3 ", &v) == ZXM_E_ARG && tideturn_parse("", &v) == ZXM_E_ARG &&
              tideturn_parse("test", &v) == ZXM_E_ARG && tideturn_parse(NULL, &v) == ZXM_E_ARG,
          "tideturn: versions that do not parse");
    /* TR1-TR3 through the model's own C */
    tideturn_version_t a = {1, 10, 0}, b = {1, 9, 999}, bad = {1, 1000, 0};
    CHECK(tideturn_order(&a, &b) == 1 && tideturn_order(&b, &a) == -1 &&
              tideturn_order(&a, &a) == 0 && tideturn_order(&bad, &a) == TIDETURN_REFUSED &&
              tideturn_dual_signed(1, 1) && !tideturn_dual_signed(1, 0) &&
              !tideturn_dual_signed(2, 1) &&
              tideturn_record_fits(TIDETURN_SEQ_MAX, 999, 999, 999, 1, 7, TIDETURN_STAMP_MAX) &&
              !tideturn_record_fits(1, 1000, 0, 0, 1, 1, 0),
          "tideturn: the rules");
    /* TM2: zxvd's status texts */
    CHECK(tideturn_class("up to date") == TIDETURN_ST_CURRENT &&
              tideturn_class("update available") == TIDETURN_ST_NEWER &&
              tideturn_class("update needs a newer installed version") == TIDETURN_ST_NEEDS &&
              tideturn_class("unsigned bucket contents") == TIDETURN_ST_UNSIGNED &&
              tideturn_class("IPNS record expired") == TIDETURN_ST_RECORD &&
              tideturn_class("signed manifest is malformed") == TIDETURN_ST_MANIFEST &&
              tideturn_class("network unavailable") == TIDETURN_ST_OFFLINE &&
              tideturn_class("no buckets") == TIDETURN_ST_OFFLINE &&
              tideturn_class("something new") == TIDETURN_ST_OTHER &&
              tideturn_class("") == TIDETURN_ST_NONE,
          "tideturn: status classes");

    /* the real dispatcher: its version is "test", which is no version */
    tideturn_init(&TDT, &LINK);
    CHECK(tideturn_read_version(&TDT) == ZXM_OK && !TDT.known && !strcmp(TDT.app, "test") &&
              strstr(TDT.notice, "no version Tideturn can compare"),
          "tideturn: zxvd's own version: %s %s", TDT.app, LINK.err);
    before = sent;
    CHECK(tideturn_offer(&TDT, "1.3.0", "1.0.0", 1, 1) == ZXM_E_STATE && sent == before &&
              TDT.n == 0 && !TDT.pending,
          "tideturn: nothing judged against an unknown version, nothing asked");
    CHECK(tideturn_check(&TDT) == ZXM_E_REMOTE && TDT.n == 0 && TDT.status == TIDETURN_ST_NONE,
          "tideturn: no update host in this harness: %s", LINK.err);
    CHECK(tideturn_confirm(&TDT, true) == ZXM_E_STATE, "tideturn: no offer waits");

    /* its own link: a known version, and update.check answered */
    zxm_link_init(&TDT_LINK, tdt_send, &TDT_LINK);
    tideturn_init(&TDT2, &TDT_LINK);
    TDT_VERSION = TDT_V("1.2.0", "nine");
    CHECK(tideturn_read_version(&TDT2) == ZXM_E_PROTOCOL && !TDT2.known,
          "tideturn: a libzxv version that is none: not zxvd's answer");
    TDT_VERSION = TDT_V("1.2.0", "0.4.1");
    CHECK(tideturn_read_version(&TDT2) == ZXM_OK && TDT2.known && TDT2.running.major == 1 &&
              TDT2.running.minor == 2 && TDT2.running.patch == 0 && TDT2.lib.minor == 4 &&
              !strcmp(TDT2.notice, "Running 1.2.0."),
          "tideturn: running 1.2.0: %s", TDT2.notice);

    /* TM2: checks, each a record stamped by zxvd's clock */
    TDT_UPDATE = "{\"status\":\"update available\"}";
    CHECK(tideturn_check(&TDT2) == ZXM_OK && TDT2.status == TIDETURN_ST_NEWER && TDT2.n == 1 &&
              TDT2.rec[0].seq == 1 && TDT2.rec[0].prev == 0 && TDT2.rec[0].sig == 1 &&
              TDT2.rec[0].outcome == TIDETURN_CHECKED && TDT2.rec[0].v.minor == 2 &&
              TDT2.rec[0].stamp > 0 &&
              strstr(TDT2.notice, "nothing is installed without your word"),
          "tideturn: an update is offered: %s", TDT2.notice);
    TDT_UPDATE = "{\"status\":\"\"}";
    CHECK(tideturn_check(&TDT2) == ZXM_E_PROTOCOL && TDT2.n == 1 &&
              TDT2.status == TIDETURN_ST_NEWER,
          "tideturn: an empty status is no answer; nothing kept");
    TDT_UPDATE = "{\"status\":\"unsigned bucket contents\"}";
    CHECK(tideturn_check(&TDT2) == ZXM_OK && TDT2.status == TIDETURN_ST_UNSIGNED && TDT2.n == 2 &&
              TDT2.rec[1].sig == 0 && TDT2.rec[1].prev == 1 &&
              strstr(TDT2.notice, "never installable"),
          "tideturn: unsigned: %s", TDT2.notice);

    /* TM3: offers, each refused for the first reason that applies */
    CHECK(tideturn_offer(&TDT2, "1.3.0", "1.0.0", 1, 0) == ZXM_OK &&
              TDT2.verdict == TIDETURN_NO_SIGNATURE && !TDT2.pending && TDT2.rec[2].sig == 0 &&
              strstr(TDT2.notice, "second not verified"),
          "tideturn: one signature only: %s", TDT2.notice);
    CHECK(tideturn_offer(&TDT2, "1.3.0", "1.0.0", 2, 1) == ZXM_OK &&
              TDT2.verdict == TIDETURN_NO_SIGNATURE && !TDT2.pending,
          "tideturn: a verify result of 2 is no result");
    CHECK(tideturn_offer(&TDT2, "1.2.0", "1.0.0", 1, 1) == ZXM_OK &&
              TDT2.verdict == TIDETURN_NOT_NEWER && !TDT2.pending &&
              strstr(TDT2.notice, "not newer than 1.2.0"),
          "tideturn: the same version: %s", TDT2.notice);
    CHECK(tideturn_offer(&TDT2, "1.1.9", "1.0.0", 1, 1) == ZXM_OK &&
              TDT2.verdict == TIDETURN_NOT_NEWER,
          "tideturn: an older version (a rollback) refused");
    CHECK(tideturn_offer(&TDT2, "2.0.0", "1.5.0", 1, 1) == ZXM_OK &&
              TDT2.verdict == TIDETURN_TOO_OLD && !TDT2.pending &&
              strstr(TDT2.notice, "applies over 1.5.0 or later"),
          "tideturn: needs a newer installed version: %s", TDT2.notice);
    before = sent;
    uint32_t n = TDT2.n;
    CHECK(tideturn_offer(&TDT2, "1.3.0", "1.4.0", 1, 1) == ZXM_E_ARG &&
              tideturn_offer(&TDT2, "01.3.0", "1.0.0", 1, 1) == ZXM_E_ARG &&
              tideturn_offer(&TDT2, "1.3.0", NULL, 1, 1) == ZXM_E_ARG && sent == before &&
              TDT2.n == n,
          "tideturn: offers that are no release: nothing asked, nothing kept");

    /* TM3-TM4: dual-signed, newer and applicable: it waits for the owner */
    CHECK(tideturn_offer(&TDT2, "1.3.0", "1.2.0", 1, 1) == ZXM_OK &&
              TDT2.verdict == TIDETURN_OFFERED && TDT2.pending && TDT2.rec[TDT2.n - 1u].sig == 1 &&
              TDT2.rec[TDT2.n - 1u].v.minor == 3 &&
              strstr(TDT2.notice, "1.3.0 is newer than 1.2.0 and both signatures verify"),
          "tideturn: offered: %s", TDT2.notice);
    CHECK(tideturn_offer(&TDT2, "1.4.0", "1.0.0", 1, 1) == ZXM_E_STATE && TDT2.offer.minor == 3,
          "tideturn: one offer waits at a time");
    CHECK(tideturn_confirm(&TDT2, true) == ZXM_OK && !TDT2.pending &&
              TDT2.rec[TDT2.n - 1u].outcome == TIDETURN_APPROVED &&
              strstr(TDT2.notice, "Tideturn installs nothing") && TDT2.running.minor == 2,
          "tideturn: approved, nothing installed: %s", TDT2.notice);
    CHECK(tideturn_confirm(&TDT2, true) == ZXM_E_STATE, "tideturn: a word is spent once");
    CHECK(tideturn_offer(&TDT2, "1.2.1", "1.2.0", 1, 1) == ZXM_OK && TDT2.pending &&
              tideturn_confirm(&TDT2, false) == ZXM_OK && !TDT2.pending &&
              TDT2.rec[TDT2.n - 1u].outcome == TIDETURN_DECLINED &&
              strstr(TDT2.notice, "You declined 1.2.1"),
          "tideturn: declined: %s", TDT2.notice);

    /* TM5: the trail is linked and every record fits */
    CHECK(TDT2.n == 11 && tideturn_trail(&TDT2) == TDT2.n, "tideturn: the trail is intact: %u",
          TDT2.n);
    bool fit = true;
    for (uint32_t i = 0; i < TDT2.n; i++) fit = fit && tideturn_record_ok(&TDT2, i);
    CHECK(fit && !tideturn_record_ok(&TDT2, TDT2.n), "tideturn: every record fits ^ZXTIDE");
    TDT_UPDATE = "{\"status\":\"up to date\"}";
    bool ok = true;
    for (uint32_t i = 0; i < TIDETURN_KEEP; i++) ok = ok && tideturn_check(&TDT2) == ZXM_OK;
    CHECK(ok && TDT2.n == TIDETURN_KEEP && TDT2.rec[0].seq == 12 && TDT2.rec[0].prev == 11 &&
              TDT2.rec[TIDETURN_KEEP - 1u].seq == 11 + (int64_t) TIDETURN_KEEP &&
              tideturn_trail(&TDT2) == TDT2.n && TDT2.status == TIDETURN_ST_CURRENT,
          "tideturn: full: the newest kept, still linked: %lld", (long long) TDT2.rec[0].seq);
    TDT2.rec[5].prev = 2;
    CHECK(tideturn_trail(&TDT2) == 5, "tideturn: a record that skips is found");
    TDT2.rec[5].prev = TDT2.rec[4].seq;
    TDT2.rec[7].seq = TDT2.rec[6].seq;
    CHECK(tideturn_trail(&TDT2) == 7, "tideturn: a record not past the one before is found");

    /* sys.info that is not zxvd's: nothing kept */
    TDT_VERSION = NULL;
    TDT_UPDATE = NULL;
    tideturn_init(&TDT2, &FAKE);
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":" TDT_V("3.0.0", "1.0.0") "}";
    CHECK(tideturn_read_version(&TDT2) == ZXM_OK && TDT2.known && TDT2.running.major == 3,
          "tideturn: a version from a canned reply");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":-5}}";
    CHECK(tideturn_offer(&TDT2, "3.1.0", "3.0.0", 1, 1) == ZXM_E_PROTOCOL && TDT2.n == 0 &&
              !TDT2.pending,
          "tideturn: a clock before 1970 is no stamp");
    CANNED = NULL;
}
#undef TDT_V
