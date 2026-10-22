/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Sieve's model against zxvd's real dispatcher (LINK): swarm.ask through
 * the stub host's companion and journal.query on a journal on disk, as
 * zxvd runs them. This harness links neither the Playground nor zxvd's
 * Web4 feed group, so play.draft, web4.feed and web4.consent are answered
 * on a link of their own (SV_LINK) in the shape zxvd writes them
 * (zxvd_rpc.c m_play_draft, zxvd_web4.c), which passes every other request
 * to the real dispatcher and keeps what it was sent. */
#include "sieve_model.h"

static sieve_t SV;
static zxm_link_t SV_LINK;
static const char *SV_DRAFT, *SV_FEED, *SV_CONSENT; /* canned results */
static uint32_t SV_SENT;                            /* play.* and web4.* requests sent */
static char SV_REQ[ZXM_REQ_MAX + 1];                /* the last of them */

static int32_t sv_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    const char *r = NULL;
    if (strstr(req, "\"method\":\"play.") || strstr(req, "\"method\":\"web4.")) {
        SV_SENT++;
        memcpy(SV_REQ, req, len);
        SV_REQ[len] = 0;
        if (strstr(req, "\"play.draft\"")) r = SV_DRAFT;
        if (strstr(req, "\"web4.feed\"")) r = SV_FEED;
        if (strstr(req, "\"web4.consent\"")) r = SV_CONSENT;
        if (!r) return -1;
        return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq, r);
    }
    return inproc(NULL, req, len, resp, cap);
}

#define SV_A "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
/* one feed entry as zxvd_web4.c put_entry writes it */
#define SV_ENTRY(id, status, tool, payload, pii, amount, ts, link)                                 \
    "{\"id\":" #id ",\"kind\":\"REQUEST\",\"status\":\"" status "\",\"from\":\"" SV_A              \
    "\",\"to\":\"" SV_A "\",\"tool\":\"" tool "\",\"req_id\":\"" #id "\",\"chunk\":0,"             \
    "\"final\":false,\"payload\":\"" payload "\",\"pii\":" pii ",\"amount_minor\":\"" amount       \
    "\"," ts "," link ",\"bytes\":0}"
#define SV_UNSIGNED "\"seq\":null,\"ts\":null"
#define SV_NOLINK   "\"consent\":null,\"answers\":null,\"grant\":null"
#define SV_FEED_OF(entries)                                                                        \
    "{\"open\":true,\"agent_id\":\"" SV_A "\",\"envelopes\":[" entries                             \
    "],\"next\":null,\"total\":5,\"held\":2}"

APPS_TEST(sieve)
{
    const int64_t NOW = 1760000000;
    sieve_log_t line;
    const char *text = NULL;
    uint32_t len = 0;
    zxm_link_init(&SV_LINK, sv_send, &SV_LINK);
    sieve_init(&SV, &SV_LINK);

    /* SM1: a tagged answer is held, never applied */
    ANSWER = "Call it Harvest Notes.";
    int before = sent, was_asked = asked;
    CHECK(sieve_ask(&SV, 27, "A title for my notes", NOW, 600, 7) == ZXM_OK && SV.n == 1 &&
              SV.item[0].id == 1 && SV.item[0].state == SIEVE_PENDING &&
              SV.item[0].source == SIEVE_SRC_ANSWER && SV.item[0].app == 27 &&
              !strcmp(SV.item[0].text, "Call it Harvest Notes.") && SV.item[0].version == 7 &&
              asked == was_asked + 1 && !strcmp(last_prompt, "A title for my notes"),
          "sieve: an answer held: %s", SV_LINK.err);
    before = sent;
    was_asked = asked;
    CHECK(sieve_ask(&SV, 0, "x", NOW, 600, 7) == ZXM_E_ARG &&
              sieve_ask(&SV, SIEVE_APP_FIELD + 1, "x", NOW, 600, 7) == ZXM_E_ARG &&
              sieve_ask(&SV, 27, "x", NOW, 0, 7) == ZXM_E_ARG &&
              sieve_ask(&SV, 27, "x", NOW, SIEVE_HOLD_MAX + 1, 7) == ZXM_E_ARG &&
              sieve_ask(&SV, 27, "x", -1, 600, 7) == ZXM_E_ARG &&
              sieve_ask(&SV, 27, "x", SIEVE_NOW_MAX + 1, 600, 7) == ZXM_E_ARG &&
              sieve_ask(&SV, 27, "x", NOW, 600, -1) == ZXM_E_ARG &&
              sieve_ask(&SV, 27, "", NOW, 600, 7) == ZXM_E_ARG &&
              sieve_ask(&SV, 27, NULL, NOW, 600, 7) == ZXM_E_ARG && sent == before &&
              asked == was_asked && SV.n == 1,
          "sieve: no app, no hold, no time, no version, no prompt: nothing asked");
    ANSWER = "";
    CHECK(sieve_ask(&SV, 27, "x", NOW, 600, 7) == ZXM_E_PROTOCOL && SV.n == 1,
          "sieve: an empty answer is nothing to hold");
    ANSWER = "ok";

    /* SM1: an untrusted draft, held the same way */
    SV_DRAFT = "{\"model\":false,\"draft\":\"total = 1 + 2\"}";
    CHECK(sieve_draft(&SV, 27, "treaty", "add one and two", NOW, 900, 7) == ZXM_OK && SV.n == 2 &&
              SV.item[1].source == SIEVE_SRC_DRAFT && !SV.item[1].model &&
              !strcmp(SV.item[1].what, "treaty") && !strcmp(SV.item[1].text, "total = 1 + 2") &&
              strstr(SV_REQ, "\"play.draft\"") && strstr(SV_REQ, "\"template\":\"treaty\""),
          "sieve: a draft held: %s", SV_LINK.err);
    uint32_t sv_sent = SV_SENT;
    CHECK(sieve_draft(&SV, 27, "", "x", NOW, 900, 7) == ZXM_E_ARG &&
              sieve_draft(&SV, 27, "treaty", "", NOW, 900, 7) == ZXM_E_ARG &&
              sieve_draft(&SV, 27, NULL, "x", NOW, 900, 7) == ZXM_E_ARG && SV_SENT == sv_sent,
          "sieve: no template or no request: nothing sent");
    SV_DRAFT = "{\"draft\":\"x\"}";
    CHECK(sieve_draft(&SV, 27, "treaty", "x", NOW, 900, 7) == ZXM_E_PROTOCOL && SV.n == 2,
          "sieve: a draft that does not say who wrote it: not held");

    /* SM1: the envelopes zxvd holds for consent */
    SV_FEED = SV_FEED_OF(SV_ENTRY(9, "held", "pay", "for the seeds", "false", "2500", SV_UNSIGNED,
                                  SV_NOLINK) "," SV_ENTRY(4, "held", "chat", "my address", "true",
                                                          "0", SV_UNSIGNED, SV_NOLINK));
    CHECK(sieve_gather(&SV, 27, NOW, 600) == ZXM_OK && SV.n == 4 && SV.added == 2 &&
              SV.item[2].ref == 9 && SV.item[2].amount == 2500 && !SV.item[2].pii &&
              !strcmp(SV.item[2].what, "pay") && SV.item[3].ref == 4 && SV.item[3].pii &&
              strstr(SV_REQ, "\"web4.feed\"") && strstr(SV_REQ, "\"status\":\"held\""),
          "sieve: two envelopes held: %s", SV_LINK.err);
    CHECK(sieve_gather(&SV, 27, NOW, 600) == ZXM_OK && SV.n == 4 && SV.added == 0,
          "sieve: the same envelopes are not held twice");
    SV_FEED = SV_FEED_OF(SV_ENTRY(4, "held", "chat", "x", "true", "0", SV_UNSIGNED,
                                  SV_NOLINK) "," SV_ENTRY(9, "held", "pay", "x", "false", "1",
                                                          SV_UNSIGNED, SV_NOLINK));
    CHECK(sieve_gather(&SV, 27, NOW, 600) == ZXM_E_PROTOCOL && SV.n == 4,
          "sieve: oldest first: nothing kept");
    SV_FEED = SV_FEED_OF(
        SV_ENTRY(12, "held", "pay", "x", "false", "1000000000000000000", SV_UNSIGNED, SV_NOLINK));
    CHECK(sieve_gather(&SV, 27, NOW, 600) == ZXM_E_PROTOCOL && SV.n == 4,
          "sieve: an amount past 18 digits: nothing kept");

    /* SM6: counts */
    CHECK(sieve_held_for(&SV, 27, NOW) == 4 && sieve_held_for(&SV, 5, NOW) == 0 &&
              sieve_held_for(&SV, 27, NOW + 600) == 1 && sieve_held_for(&SV, 27, NOW + 900) == 0 &&
              sieve_held_for(&SV, 27, -1) == SIEVE_REFUSED,
          "sieve: held for app 27, then lapsing");
    CHECK(sieve_held_by_source(&SV, SIEVE_SRC_ANSWER) == 1 &&
              sieve_held_by_source(&SV, SIEVE_SRC_DRAFT) == 1 &&
              sieve_held_by_source(&SV, SIEVE_SRC_ENVELOPE) == 2 &&
              sieve_held_by_source(&SV, 3) == SIEVE_REFUSED,
          "sieve: held by source (Witness's kind_count)");

    /* SM7: the notice, composed only */
    CHECK(sieve_notice(&SV, 1, NOW) == ZXM_OK && strstr(SV.notice, "Suggestion 1, an answer") &&
              strstr(SV.notice, "nothing is applied") && strstr(SV.notice, "10 minutes") &&
              sieve_notice(&SV, 1, NOW + 600) == ZXM_E_STATE &&
              sieve_notice(&SV, 99, NOW) == ZXM_E_ARG,
          "sieve: notice: %s", SV.notice);

    /* SM3: never by itself; never stale or lapsed */
    before = sent;
    sv_sent = SV_SENT;
    CHECK(sieve_accept(&SV, 1, NOW, 7, false) == ZXM_E_STATE &&
              sieve_accept(&SV, 3, NOW, 0, false) == ZXM_E_STATE &&
              sieve_reject(&SV, 3, NOW, false) == ZXM_E_STATE &&
              sieve_accept(&SV, 1, NOW, 8, true) == ZXM_E_STATE &&
              sieve_accept(&SV, 1, NOW + 600, 7, true) == ZXM_E_STATE &&
              sieve_accept(&SV, 99, NOW, 7, true) == ZXM_E_ARG &&
              sieve_accept(&SV, 1, -1, 7, true) == ZXM_E_ARG &&
              sieve_accept(&SV, 1, NOW, -1, true) == ZXM_E_ARG &&
              SV.item[0].state == SIEVE_PENDING && SV.item[2].state == SIEVE_PENDING &&
              SV_SENT == sv_sent && sent == before && sieve_log_take(&SV, &line) == ZXM_E_STATE,
          "sieve: no word, a moved target, a lapsed hold: nothing changes, nothing sent or logged");
    CHECK(!sieve_may_apply(&SV, 1, NOW, 7), "sieve: a pending answer may not be applied");

    /* SM3, SM4: the owner accepts the answer, and it is logged */
    CHECK(sieve_accept(&SV, 1, NOW + 10, 7, true) == ZXM_OK && SV.item[0].state == SIEVE_ACCEPTED &&
              SV.item[0].decided == NOW + 10 && sent == before && SV_SENT == sv_sent,
          "sieve: the answer accepted on Sieve's side");
    CHECK(sieve_log_take(&SV, &line) == ZXM_OK && line.id == 1 && line.state == SIEVE_ACCEPTED &&
              line.at == NOW + 10 && line.app == 27 && line.source == SIEVE_SRC_ANSWER &&
              sieve_log_take(&SV, &line) == ZXM_E_STATE,
          "sieve: the accept logged");
    CHECK(sieve_accept(&SV, 1, NOW + 11, 7, true) == ZXM_E_STATE, "sieve: accepted once");

    /* SM5: applied only while SR1 says it may be */
    CHECK(sieve_may_apply(&SV, 1, NOW + 599, 7) && !sieve_may_apply(&SV, 1, NOW + 600, 7) &&
              !sieve_may_apply(&SV, 1, NOW + 20, 8) && !sieve_may_apply(&SV, 1, NOW + 20, -1) &&
              !sieve_may_apply(&SV, 99, NOW, 7),
          "sieve: accepted, not expired, target unchanged since");
    CHECK(sieve_apply(&SV, 1, NOW + 20, 7, false, &text, &len) == ZXM_E_STATE &&
              sieve_apply(&SV, 1, NOW + 20, 8, true, &text, &len) == ZXM_E_STATE &&
              sieve_apply(&SV, 1, NOW + 600, 7, true, &text, &len) == ZXM_E_STATE &&
              sieve_apply(&SV, 2, NOW + 20, 7, true, &text, &len) == ZXM_E_STATE && !text &&
              SV.item[0].state == SIEVE_ACCEPTED && SV.head == SV.tail,
          "sieve: no word, a moved target, a lapsed hold, a draft not accepted: not applied");
    CHECK(sieve_apply(&SV, 1, NOW + 20, 7, true, &text, &len) == ZXM_OK && text &&
              !strcmp(text, "Call it Harvest Notes.") && len == 22 &&
              SV.item[0].state == SIEVE_APPLIED && !sieve_may_apply(&SV, 1, NOW + 21, 7) &&
              sieve_log_take(&SV, &line) == ZXM_OK && line.state == SIEVE_APPLIED,
          "sieve: applied once, handed to the caller, logged");

    /* SM3: an envelope's accept is the owner's consent, for the hold's time left */
    SV_CONSENT = "{\"envelope\":" SV_ENTRY(
        9, "signed", "pay", "for the seeds", "false", "2500", "\"seq\":\"8\",\"ts\":1760000100",
        "\"consent\":10,\"answers\":null,\"grant\":null") ","
                                                          "\"consent\":" SV_ENTRY(
                                                              10, "signed", "pay", "", "false", "0",
                                                              "\"seq\":\"9\",\"ts\":1760000100",
                                                              "\"consent\":null,\"answers\":9,"
                                                              "\"grant\":true") "}";
    CHECK(sieve_accept(&SV, 3, NOW + 100, 0, true) == ZXM_OK &&
              SV.item[2].state == SIEVE_ACCEPTED && strstr(SV_REQ, "\"web4.consent\"") &&
              strstr(SV_REQ, "\"id\":9") && strstr(SV_REQ, "\"grant\":true") &&
              strstr(SV_REQ, "\"expires_in\":500") && strstr(SV_REQ, "\"now\":1760000100") &&
              sieve_log_take(&SV, &line) == ZXM_OK && line.id == 3 &&
              line.source == SIEVE_SRC_ENVELOPE,
          "sieve: envelope 9 granted for its 500 seconds left: %s", SV_LINK.err);
    /* a deny, as zxvd answers it; a consent answer for another envelope is refused */
    SV_CONSENT = "{\"envelope\":" SV_ENTRY(
        4, "denied", "chat", "", "true", "0", SV_UNSIGNED,
        "\"consent\":11,\"answers\":null,\"grant\":null") ","
                                                          "\"consent\":" SV_ENTRY(
                                                              11, "signed", "chat", "", "false",
                                                              "0",
                                                              "\"seq\":\"10\",\"ts\":1760000100",
                                                              "\"consent\":null,\"answers\":5,"
                                                              "\"grant\":false") "}";
    CHECK(sieve_reject(&SV, 4, NOW + 100, true) == ZXM_E_PROTOCOL &&
              SV.item[3].state == SIEVE_PENDING && SV.item[3].text[0] && SV.head == SV.tail,
          "sieve: an answer for another envelope: nothing changes");
    SV_CONSENT = "{\"envelope\":" SV_ENTRY(
        4, "denied", "chat", "", "true", "0", SV_UNSIGNED,
        "\"consent\":11,\"answers\":null,\"grant\":null") ","
                                                          "\"consent\":" SV_ENTRY(
                                                              11, "signed", "chat", "", "false",
                                                              "0",
                                                              "\"seq\":\"10\",\"ts\":1760000100",
                                                              "\"consent\":null,\"answers\":4,"
                                                              "\"grant\":false") "}";
    CHECK(sieve_reject(&SV, 4, NOW + 100, true) == ZXM_OK && SV.item[3].state == SIEVE_REJECTED &&
              !SV.item[3].text[0] && SV.item[3].len == 0 && strstr(SV_REQ, "\"grant\":false") &&
              !strstr(SV_REQ, "expires_in") && sieve_log_take(&SV, &line) == ZXM_OK &&
              line.id == 4 && line.state == SIEVE_REJECTED,
          "sieve: envelope 4 denied, its text dropped, logged");
    sv_sent = SV_SENT;
    CHECK(sieve_reject(&SV, 2, NOW + 100, true) == ZXM_OK && SV.item[1].state == SIEVE_REJECTED &&
              SV_SENT == sv_sent && sieve_log_take(&SV, &line) == ZXM_OK && line.id == 2,
          "sieve: the draft rejected on Sieve's side, logged");

    /* SM4: a full log refuses the action before it is done */
    CHECK(sieve_ask(&SV, 30, "another", NOW, 600, 1) == ZXM_OK && SV.n == 5, "sieve: one more");
    SV.head = SV.tail + SIEVE_LOG; /* 64 lines the view has not taken */
    CHECK(sieve_accept(&SV, 5, NOW, 1, true) == ZXM_E_TOOBIG &&
              sieve_reject(&SV, 5, NOW, true) == ZXM_E_TOOBIG && SV.item[4].state == SIEVE_PENDING,
          "sieve: a full log: not accepted, not rejected");
    SV.head = SV.tail;

    /* SM1: a full table is backpressure: nothing asked */
    while (SV.n < SIEVE_MAX)
        if (sieve_ask(&SV, 31, "fill", NOW, 600, 1) != ZXM_OK) break;
    before = sent;
    was_asked = asked;
    sv_sent = SV_SENT;
    SV_FEED = SV_FEED_OF(SV_ENTRY(15, "held", "pay", "x", "false", "1", SV_UNSIGNED, SV_NOLINK));
    CHECK(SV.n == SIEVE_MAX && sieve_ask(&SV, 31, "one too many", NOW, 600, 1) == ZXM_E_TOOBIG &&
              sieve_draft(&SV, 31, "treaty", "x", NOW, 600, 1) == ZXM_E_TOOBIG && sent == before &&
              asked == was_asked && SV_SENT == sv_sent,
          "sieve: the table full: nothing asked (%u)", SV.n);
    CHECK(sieve_gather(&SV, 31, NOW, 600) == ZXM_E_TOOBIG && SV.n == SIEVE_MAX,
          "sieve: no room for a new envelope: none kept");

    /* SM1: the sweep is the owner's too; it drops what is decided or lapsed */
    CHECK(sieve_sweep(&SV, NOW + 100, false) == ZXM_E_STATE && SV.n == SIEVE_MAX,
          "sieve: no sweep without the owner's word");
    CHECK(sieve_sweep(&SV, NOW + 100, true) == ZXM_OK && SV.n == SIEVE_MAX - 3 &&
              sieve_find(&SV, 1) < 0 && sieve_find(&SV, 2) < 0 && sieve_find(&SV, 4) < 0 &&
              sieve_find(&SV, 3) >= 0 && sieve_find(&SV, 5) >= 0,
          "sieve: applied and rejected dropped: %u", SV.n);
    CHECK(sieve_sweep(&SV, NOW + 600, true) == ZXM_OK && SV.n == 0 &&
              sieve_held_for(&SV, 31, NOW) == 0,
          "sieve: lapsed dropped: %u", SV.n);

    /* SM4: what zxvd journaled of the consent answers */
    CHECK(sieve_journal(&SV, 0) == ZXM_OK && !SV.journal_on && SV.journal_consents == 0,
          "sieve: no journal: %s", SV_LINK.err);
    char jdir[256];
    snprintf(jdir, sizeof jdir, "%s/zxv-sieve-XXXXXX",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(jdir) != NULL && zjr_open(jdir) == 0, "sieve: a journal");
    zxvd_log(ZXVD_LOG_INFO, "rpc", "web4.consent done in 3 ms");
    zxvd_log(ZXVD_LOG_INFO, "rpc", "files.write done in 2 ms");
    zxvd_log(ZXVD_LOG_WARN, "rpc", "web4.consent refused (-32090): held");
    zxvd_log(ZXVD_LOG_INFO, "rpc", "web4.consent done in 1 ms");
    zxvd_log(ZXVD_LOG_INFO, "web4", "web4.consent done in 9 ms");
    CHECK(sieve_journal(&SV, 0) == ZXM_OK && SV.journal_on && SV.journal_consents == 2 &&
              SV.journal_newest > 0 && !SV.journal_more,
          "sieve: two consent answers journaled: %u %s", SV.journal_consents, SV_LINK.err);
    zjr_close();
    CHECK(rm_tree(jdir) == 0, "sieve: journal cleanup");
}
