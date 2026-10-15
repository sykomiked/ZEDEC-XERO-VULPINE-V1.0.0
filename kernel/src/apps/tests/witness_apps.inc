/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Witness's model against zxvd's real dispatcher (LINK): the record from
 * zxvd's recent events (log.tail) and from its journal (journal.query),
 * sealed by journal.verify, stamped by sys.info; and against canned
 * replies (FAKE) for a chain that breaks and witnesses that disagree. */
#include "witness_model.h"

static witness_t WT;

APPS_TEST(witness)
{
    /* the kinds (WM3) */
    CHECK(witness_classify(WITNESS_ERROR, "net", "link down") == WITNESS_FAULT &&
              witness_classify(WITNESS_WARN, "rpc", "spaces.get refused (-32001): x") ==
                  WITNESS_REFUSED &&
              witness_classify(WITNESS_INFO, "rpc", "swarm.ask done in 3 ms") == WITNESS_AI &&
              witness_classify(WITNESS_INFO, "rpc", "spaces.create done in 1 ms") ==
                  WITNESS_CHANGE &&
              witness_classify(WITNESS_INFO, "rpc", "not kept: closed") == WITNESS_OTHER &&
              witness_classify(WITNESS_INFO, "rpc", " done") == WITNESS_OTHER &&
              witness_classify(WITNESS_INFO, "journal", "x done in 1 ms") == WITNESS_OTHER,
          "witness: kinds");
    int64_t k[6] = {1, 2, 2, 3, 2, 5};
    CHECK(witness_kind_count(k, 6, 2) == 3 && witness_kind_count(k, 6, 4) == 0 &&
              witness_kind_count(k, 0, 1) == 0 && witness_linked(5, 4, 4) &&
              !witness_linked(4, 4, 4) && !witness_linked(9, 3, 4) &&
              witness_record_fits(WITNESS_SEQ_MAX, 0, 9, WITNESS_STAMP_MAX) &&
              !witness_record_fits(1, 0, 10, 0),
          "witness: the rules");

    /* nothing to seal before a gather; a window backwards is refused */
    witness_init(&WT, &LINK);
    int before = sent;
    CHECK(witness_seal(&WT) == ZXM_E_STATE && witness_gather(&WT, 9, 8) == ZXM_E_ARG &&
              sent == before && !WT.gathered,
          "witness: refused before anything is asked");

    /* no journal: zxvd's recent events */
    zxvd_log_reset();
    CHECK(raw_call("spaces.create", "{\"kind\":\"office\",\"name\":\"Witness Test\"}") == ZXM_OK,
          "witness: a change");
    CHECK(raw_call("spaces.get", "{\"space\":\"nowhere\"}") == ZXM_E_REMOTE, "witness: a refusal");
    zxvd_log(ZXVD_LOG_INFO, "rpc", "swarm.ask done in 2 ms");
    zxvd_log(ZXVD_LOG_ERROR, "net", "link down");
    zxvd_log(ZXVD_LOG_INFO, "test", "a note");
    CHECK(witness_gather(&WT, 0, 0) == ZXM_OK && WT.gathered && !WT.kept && WT.n == 5 && !WT.more &&
              WT.as_of_ms > 0 && WT.e[0].prev == 0 && WT.e[1].prev == WT.e[0].index &&
              WT.e[4].prev == WT.e[3].index,
          "witness: recent events: %u %s", WT.n, LINK.err);
    CHECK(WT.e[0].kind == WITNESS_CHANGE && WT.e[1].kind == WITNESS_REFUSED &&
              WT.e[2].kind == WITNESS_AI && WT.e[3].kind == WITNESS_FAULT &&
              WT.e[4].kind == WITNESS_OTHER && WT.count[WITNESS_CHANGE] == 1 &&
              witness_count(&WT, WITNESS_REFUSED) == 1 && witness_count(&WT, WITNESS_NONE) == 0,
          "witness: each kind once");
    CHECK(witness_chain(&WT) == WT.n && WT.broken == WT.n, "witness: the chain is intact");
    CHECK(witness_seal(&WT) == ZXM_OK && WT.seal.done && !WT.seal.on &&
              WT.verdict == LPRES_STATE_TRUE,
          "witness: no journal says nothing; the chain alone: %u", WT.verdict);
    CHECK(strstr(witness_summarize(&WT), "5 recent events: 1 changes, 1 refused, 1 AI actions, "
                                         "1 faults, 1 other; chain intact; no journal kept; "
                                         "intact"),
          "witness: %s", WT.summary);
    /* a window by time keeps only what is inside it */
    CHECK(witness_gather(&WT, 1, 2) == ZXM_OK && WT.n == 0 && !WT.more &&
              strstr(witness_summarize(&WT), "nothing to link; journal not checked"),
          "witness: an old window: %s", WT.summary);
    /* more than the record keeps: the newest stay */
    for (int i = 0; i < 600; i++) zxvd_log(ZXVD_LOG_INFO, "test", "e%d", i);
    CHECK(witness_gather(&WT, 0, 0) == ZXM_OK && WT.n == WITNESS_KEEP && WT.more &&
              WT.dropped > 0 && !strcmp(WT.e[WITNESS_KEEP - 1u].text, "e599") &&
              !strcmp(WT.e[0].text, "e88") && witness_chain(&WT) == WT.n,
          "witness: full: %u %s", WT.n, WT.n ? WT.e[0].text : "-");

    /* the journal: kept across restarts */
    char jdir[256];
    snprintf(jdir, sizeof jdir, "%s/zxv-witness-XXXXXX",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(jdir) != NULL && zjr_open(jdir) == 0, "witness: a journal");
    for (int i = 0; i < 150; i++)
        zxvd_log(i % 50 ? ZXVD_LOG_INFO : ZXVD_LOG_WARN, "rpc", "files.write done in %d ms", i);
    CHECK(witness_gather(&WT, 0, 0) == ZXM_OK && WT.kept && WT.n == 151 && !WT.more &&
              !strcmp(WT.e[0].text, "opened") && WT.e[0].kind == WITNESS_OTHER &&
              WT.count[WITNESS_REFUSED] == 3 && WT.count[WITNESS_CHANGE] == 147 &&
              witness_chain(&WT) == WT.n,
          "witness: the journal, two pages: %u %s", WT.n, LINK.err);
    CHECK(witness_seal(&WT) == ZXM_OK && WT.seal.on && WT.seal.ok && WT.seal.entries == 151 &&
              WT.verdict == LPRES_STATE_TRUE &&
              strstr(witness_summarize(&WT), "151 kept events: 147 changes, 3 refused") &&
              strstr(WT.summary, "journal sealed through seq"),
          "witness: sealed: %s", WT.summary);
    zjr_close();
    CHECK(rm_tree(jdir) == 0, "witness: journal cleanup");

    /* canned: a chain that breaks, and witnesses that disagree */
#define WJ(on, entries, more)                                                                      \
    "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":77,\"host\":\"h\",\"on\":" on          \
    ",\"entries\":[" entries "],\"more\":" more ",\"next\":12,\"dropped\":0}}"
#define WE(seq, lv)                                                                                \
    "{\"seq\":" seq ",\"time_ms\":7,\"level\":\"" lv "\",\"source\":\"rpc\",\"text\":\"t\"}"
    witness_init(&WT, &FAKE);
    CANNED = WJ("true", WE("9", "info") "," WE("12", "warn") "," WE("3", "error"), "false");
    CHECK(witness_gather(&WT, 0, 0) == ZXM_OK && WT.kept && WT.n == 3 && WT.as_of_ms == 77 &&
              !strcmp(WT.host, "h") && WT.e[0].index == 3 && WT.e[2].index == 9 &&
              WT.e[2].prev == 12 && witness_chain(&WT) == 2 && WT.broken == 2,
          "witness: out of order: broken at the third entry");
#define WV(on, ok, broken, why)                                                                    \
    "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"on\":" on ",\"ok\":" ok ",\"segments\":1,"       \
    "\"entries\":3,\"first_seq\":3,\"last_seq\":12,\"broken_at\":" broken ",\"why\":\"" why "\"}}"
    CANNED = WV("true", "true", "0", "");
    CHECK(witness_seal(&WT) == ZXM_OK && WT.verdict == LPRES_STATE_BOTH &&
              strstr(witness_summarize(&WT), "chain broken at seq 9") &&
              strstr(WT.summary, "held: the witnesses disagree"),
          "witness: the journal says whole, the chain says broken: held: %s", WT.summary);
    CANNED = WV("true", "false", "5", "line 5 was changed");
    CHECK(witness_seal(&WT) == ZXM_OK && WT.verdict == LPRES_STATE_FALSE &&
              strstr(witness_summarize(&WT), "journal broken at seq 5: line 5 was changed"),
          "witness: both say broken: %s", WT.summary);
    CANNED = WV("true", "true", "5", "");
    CHECK(witness_seal(&WT) == ZXM_E_PROTOCOL, "witness: ok with a break");
    CANNED = WV("true", "false", "5", "");
    CHECK(witness_seal(&WT) == ZXM_E_PROTOCOL, "witness: a break without a reason");
    /* in order: the chain holds, and a broken journal is held against it */
    CANNED = WJ("true", WE("12", "info") "," WE("9", "info"), "false");
    CHECK(witness_gather(&WT, 0, 0) == ZXM_OK && WT.n == 2 && WT.broken == 2 &&
              WT.verdict == LPRES_STATE_NEITHER,
          "witness: in order");
    CANNED = WV("true", "false", "5", "gone");
    CHECK(witness_seal(&WT) == ZXM_OK && WT.verdict == LPRES_STATE_BOTH, "witness: held");
    /* a damaged reply keeps the record shown (WM4) */
    CANNED = WJ("true", WE("1000000000000000000", "info"), "false");
    CHECK(witness_gather(&WT, 0, 0) == ZXM_E_PROTOCOL && WT.n == 2 && WT.e[1].index == 12,
          "witness: a seq past 18 digits is not a record");
    CANNED = WJ("true", WE("4", "loud"), "false");
    CHECK(witness_gather(&WT, 0, 0) == ZXM_E_PROTOCOL && WT.n == 2, "witness: a bad level");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"time_ms\":1}}";
    CHECK(witness_gather(&WT, 0, 0) == ZXM_E_PROTOCOL && WT.n == 2, "witness: no host");
    /* no journal: the tail, the same canned page */
    CANNED = WJ("false", WE("10", "info") "," WE("11", "warn"), "false");
    CHECK(witness_gather(&WT, 0, 0) == ZXM_OK && !WT.kept && WT.n == 2 && WT.e[1].prev == 10 &&
              WT.count[WITNESS_REFUSED] == 1 && strstr(last_req, "\"method\":\"log.tail\""),
          "witness: from the tail: %s", last_req);
    CANNED = NULL;
#undef WJ
#undef WE
#undef WV
}
