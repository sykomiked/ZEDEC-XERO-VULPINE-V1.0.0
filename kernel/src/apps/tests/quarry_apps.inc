/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Quarry's model against zxvd's real dispatcher (LINK): files written
 * with files.write into the test's scratch Hoard, found with search.query,
 * read with files.read and cited, claims checked against the citations,
 * and a summary asked of the companion (swarm.ask, the stub host's set
 * answer) and held. web4.feed is served in the daemon by zxvd_web4.c,
 * which this test does not link (the dispatcher answers -32601), so its
 * answers are made up in their documented shape (FAKE). */
#include "quarry_model.h"

#define QY_OK     "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":"
#define QY_H16(x) x x x x x x x x x x x x x x x x
#define QY_ID     QY_H16("ab") QY_H16("ab")
#define QY_ENV(id, kind, status, pii, payload, ts)                                                 \
    "{\"id\":" id ",\"kind\":\"" kind "\",\"status\":\"" status "\",\"from\":\"" QY_ID             \
    "\",\"to\":\"" QY_ID "\",\"tool\":\"news\",\"req_id\":\"1\",\"chunk\":0,\"final\":true,"       \
    "\"payload\":\"" payload "\",\"pii\":" pii ",\"amount_minor\":\"0\",\"seq\":\"1\",\"ts\":" ts  \
    ",\"consent\":null,\"answers\":null,\"grant\":null,\"bytes\":10}"

static bool qy_write(const char *path, const char *text)
{
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", path);
    zxm_p_str(&LINK, "text", text);
    return zxm_call(&LINK) == ZXM_OK;
}

static bool qy_mkdir(const char *path)
{
    zxm_begin(&LINK, "files.mkdir");
    zxm_p_str(&LINK, "path", path);
    return zxm_call(&LINK) == ZXM_OK;
}

/* the hit whose path is `path`, or -1 */
static int32_t qy_hit(const quarry_t *q, const char *path)
{
    for (uint32_t i = 0; i < q->nhits; i++)
        if (!strcmp(q->hit[i].path, path)) return (int32_t) i;
    return -1;
}

APPS_TEST(quarry)
{
    static quarry_t Q;
    static char longq[QUARRY_QUERY_MAX + 2], longc[QUARRY_CLAIM_MAX + 2];
    const char *was_answer = ANSWER;
    int64_t n1 = 0, n2 = 0, n3 = 0, nw = 0;
    uint32_t c0 = 99, c1 = 99, c2 = 99;
    memset(longq, 'p', QUARRY_QUERY_MAX + 1);
    memset(longc, 'c', QUARRY_CLAIM_MAX + 1);

    /* the rules (QR1-QR4) */
    {
        static const int64_t col[5] = {1, 2, 2, 1, 1};
        CHECK(quarry_sourced(1, 1) && quarry_sourced(9999, 1) && !quarry_sourced(10000, 1) &&
                  !quarry_sourced(0, 1) && !quarry_sourced(5, 0) && !quarry_sourced(5, 2) &&
                  quarry_length_ok(25, 1) && quarry_length_ok(500, 32) &&
                  !quarry_length_ok(24, 1) && !quarry_length_ok(100, 33) &&
                  quarry_record_ok(9999, 999, 9, 999999999999, 99999) &&
                  !quarry_record_ok(1, 1, 1, 1000000000000, 0) && quarry_count(col, 5, 1) == 3 &&
                  quarry_count(col, 5, 2) == 2 && quarry_count(NULL, 5, 1) == 0,
              "quarry: the rules");
    }

    /* nothing searched: nothing to cite or summarize, and nothing sent */
    quarry_init(&Q, &LINK);
    int before = sent;
    CHECK(quarry_search(&Q, "") == ZXM_E_ARG && quarry_search(&Q, "   ") == ZXM_E_ARG &&
              quarry_search(&Q, longq) == ZXM_E_ARG && quarry_search(&Q, "a\nb") == ZXM_E_ARG &&
              quarry_search(&Q, "bad\xc3") == ZXM_E_ARG && quarry_search(&Q, NULL) == ZXM_E_ARG &&
              quarry_cite_hit(&Q, 0, &n1) == ZXM_E_ARG &&
              quarry_cite_env(&Q, 0, &n1) == ZXM_E_ARG &&
              quarry_summarize(&Q, 100) == ZXM_E_STATE && quarry_feed(&Q, 0) == ZXM_E_ARG &&
              quarry_feed(&Q, QUARRY_ENVS + 1u) == ZXM_E_ARG &&
              quarry_claim(&Q, "", 1, &c0) == ZXM_E_ARG &&
              quarry_claim(&Q, longc, 1, &c0) == ZXM_E_ARG &&
              quarry_claim(&Q, "a\tb", 1, &c0) == ZXM_E_ARG &&
              quarry_claim(&Q, "fine", -1, &c0) == ZXM_E_ARG && quarry_accept(&Q) == ZXM_E_STATE &&
              sent == before,
          "quarry: refused before anything is sent");

    /* QM1: a topic searched in the Hoard */
    CHECK(qy_mkdir("Research") &&
              qy_write("Research/pumice.txt",
                       "Pumice floats on water because its glass is full of gas bubbles.") &&
              qy_write("Research/scoria.txt",
                       "Scoria is darker and denser than pumice, and it sinks.") &&
              qy_write("Research/granite.txt", "Granite cools slowly deep underground."),
          "quarry: three files written: %s", LINK.err);
    CHECK(quarry_search(&Q, "pumice") == ZXM_OK && Q.current == 1 && Q.ntopics == 1 &&
              Q.nhits == 2 && Q.total == 2 && qy_hit(&Q, "Research/pumice.txt") == 0 &&
              qy_hit(&Q, "Research/scoria.txt") == 1 && qy_hit(&Q, "Research/granite.txt") < 0 &&
              Q.hit[0].size > 0 && Q.hit[0].score > 0 && Q.hit[0].mtime > 0,
          "quarry: two hits for pumice, the one named for it first (%u, %s)", Q.nhits, LINK.err);

    /* QM3: cited with the head of their text, once each */
    CHECK(quarry_cite_hit(&Q, 0, &n1) == ZXM_OK && n1 == 1 && Q.ncite == 1 &&
              Q.cite[0].kind == QUARRY_KIND_FILE && Q.cite[0].topic == 1 &&
              !strcmp(Q.cite[0].ref, "Research/pumice.txt") &&
              !strncmp(Q.cite[0].quote, "Pumice floats", 13) && Q.cite[0].stamp == Q.hit[0].mtime &&
              quarry_cite_hit(&Q, 0, &n2) == ZXM_OK && n2 == 1 && Q.ncite == 1 &&
              quarry_cite_hit(&Q, 1, &n2) == ZXM_OK && n2 == 2 && Q.ncite == 2 &&
              quarry_cite_hit(&Q, 2, &n3) == ZXM_E_ARG,
          "quarry: two citations, the same file not twice: %s", LINK.err);

    /* QM4, QM5: claims against the citations; sources per topic */
    CHECK(quarry_claim(&Q, "Pumice floats.", n1, &c0) == ZXM_OK && Q.claim[c0].sourced &&
              quarry_claim(&Q, "Scoria sinks.", n2, &c1) == ZXM_OK && Q.claim[c1].sourced &&
              quarry_claim(&Q, "Obsidian is glass.", 7, &c2) == ZXM_OK && !Q.claim[c2].sourced &&
              quarry_claim(&Q, "Stone is old.", 0, &c2) == ZXM_OK && !Q.claim[c2].sourced &&
              quarry_unsourced(&Q) == 2 && quarry_topic_sources(&Q, 1) == 2 &&
              quarry_topic_sources(&Q, 2) == 0 && quarry_claim_sourced(&Q, 1) &&
              !quarry_claim_sourced(&Q, 3),
          "quarry: two claims sourced, two not");

    /* QM6: the summary, inside QR2 or nothing is sent */
    before = sent;
    CHECK(quarry_summarize(&Q, 24) == ZXM_E_ARG && quarry_summarize(&Q, 501) == ZXM_E_ARG &&
              sent == before && Q.summary.state == ZXM_HELD_NONE,
          "quarry: a summary outside its length limits is never asked");
    ANSWER = "Pumice floats [1] while scoria sinks [2]; obsidian [7] is glass.";
    CHECK(quarry_summarize(&Q, 100) == ZXM_OK && Q.summary.state == ZXM_HELD_PENDING &&
              !strcmp(Q.summary.text, ANSWER) && Q.marks == 3 && Q.unheld == 1 &&
              Q.answer_words == 11 && !Q.long_answer && Q.summary_sources == 2 &&
              Q.summary_topic == 1 && Q.summary.cost.tokens > 0 &&
              strstr(last_prompt, "Summarize these sources on \"pumice\" in at most 100 words.") &&
              strstr(last_prompt, "[1] Research/pumice.txt\nPumice floats") &&
              strstr(last_prompt, "[2] Research/scoria.txt\nScoria is darker") &&
              !strstr(last_prompt, "granite"),
          "quarry: the summary held, one number in it naming no citation: %s", last_prompt);
    CHECK(quarry_accept(&Q) == ZXM_OK && Q.summary.state == ZXM_HELD_ACCEPTED &&
              quarry_accept(&Q) == ZXM_E_STATE,
          "quarry: accepted on the owner's word");
    ANSWER = "one two three four five six seven eight nine ten eleven twelve thirteen fourteen "
             "fifteen sixteen seventeen eighteen nineteen twenty twentyone twentytwo twentythree "
             "twentyfour twentyfive twentysix [9999]";
    CHECK(quarry_summarize(&Q, 25) == ZXM_OK && Q.answer_words == 27 && Q.long_answer &&
              Q.marks == 1 && Q.unheld == 1 && quarry_reject(&Q) == ZXM_OK &&
              Q.summary.state == ZXM_HELD_REJECTED,
          "quarry: an answer past its words is flagged, and rejected");

    /* QM4: a citation dropped only on the owner's word; its claims lose their source */
    CHECK(quarry_drop(&Q, n1, false) == ZXM_E_STATE && Q.ncite == 2 &&
              quarry_drop(&Q, 99, true) == ZXM_E_ARG && quarry_drop(&Q, n1, true) == ZXM_OK &&
              Q.ncite == 1 && quarry_find(&Q, n1) < 0 && quarry_find(&Q, n2) == 0 &&
              !Q.claim[c0].sourced && quarry_unsourced(&Q) == 3 && quarry_topic_sources(&Q, 1) == 1,
          "quarry: a citation dropped, confirmed");

    /* QM1: a second topic; the same file cited under it takes a new number */
    CHECK(quarry_search(&Q, "scoria") == ZXM_OK && Q.current == 2 && Q.ntopics == 2 &&
              Q.nhits == 1 && quarry_cite_hit(&Q, 0, &n3) == ZXM_OK && n3 == 3 &&
              quarry_topic_sources(&Q, 2) == 1 && quarry_search(&Q, "pumice") == ZXM_OK &&
              Q.current == 1 && Q.ntopics == 2,
          "quarry: two topics, a source in each: %s", LINK.err);

    /* QM2: the Web4 feed (FAKE) */
    Q.link = &FAKE;
    CANNED = QY_OK "{\"open\":true,\"agent_id\":\"" QY_ID "\",\"envelopes\":[" QY_ENV(
        "9", "RESPONSE", "signed", "false", "Pumice is mined on Lipari.",
        "1700000000") "," QY_ENV("8", "CONSENT", "signed", "false", "",
                                 "1700000000") "," QY_ENV("7", "RESPONSE", "signed", "true",
                                                          "my address",
                                                          "null") "," QY_ENV("6", "REQUEST",
                                                                             "signed", "false",
                                                                             "pumice prices?",
                                                                             "null") "],\"next\":"
                                                                                     "null,"
                                                                                     "\"total\":4,"
                                                                                     "\"held\":0}}";
    CHECK(quarry_feed(&Q, 20) == ZXM_OK && Q.feed_open && Q.nenv == 3 && Q.env[0].id == 9 &&
              Q.env[0].stamp == 1700000000 && !Q.env[0].pii && Q.env[1].pii &&
              Q.env[2].stamp == 0 && !strcmp(Q.env[2].text, "pumice prices?") &&
              strstr(last_req, "\"method\":\"web4.feed\"") &&
              strstr(last_req, "\"status\":\"signed\""),
          "quarry: three envelopes of four, the consent passed over (%s)", FAKE.err);
    CHECK(quarry_cite_env(&Q, 0, &nw) == ZXM_OK && nw == 4 &&
              Q.cite[Q.ncite - 1u].kind == QUARRY_KIND_WEB4 &&
              !strcmp(Q.cite[Q.ncite - 1u].ref, "web4:9") &&
              !strcmp(Q.cite[Q.ncite - 1u].quote, "Pumice is mined on Lipari.") &&
              quarry_cite_env(&Q, 0, &n3) == ZXM_OK && n3 == 4 &&
              quarry_cite_env(&Q, 1, &n3) == ZXM_E_STATE && quarry_topic_sources(&Q, 1) == 2,
          "quarry: an envelope cited, never one marked pii");
    CANNED = QY_OK "{\"open\":false,\"agent_id\":null,\"envelopes\":[" QY_ENV(
        "5", "RESPONSE", "signed", "false", "x", "1") "],\"next\":null,\"total\":1,\"held\":0}}";
    CHECK(quarry_feed(&Q, 20) == ZXM_E_PROTOCOL && Q.nenv == 3,
          "quarry: envelopes from a closed feed refused, the page before kept");
    CANNED = QY_OK "{\"open\":true,\"agent_id\":null,\"envelopes\":[" QY_ENV(
        "5", "RESPONSE", "signed", "false", "x", "1000000000000") "],\"next\":null,\"total\":1,"
                                                                  "\"held\":0}}";
    CHECK(quarry_feed(&Q, 20) == ZXM_E_PROTOCOL && Q.nenv == 3,
          "quarry: a stamp past 12 digits refused");
    Q.link = &LINK;
    ANSWER = was_answer;
}
