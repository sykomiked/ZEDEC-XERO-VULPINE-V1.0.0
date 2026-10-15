/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Talisman's model against zxvd's real dispatcher (LINK): player.card with
 * no view set (zxvd says it is not available), then through view ops that
 * write the cards below, in the shape zxv_host.c's rpc_player_card writes
 * (sicily.h's default thresholds: Bronze 100 / 1000 / 9000, Silver 250 /
 * 10000 / 9500, Gold 500 / 100000 / 9800, Platinum 800 / 1000000 / 9950). */
#include "talisman_model.h"

#define TL_SRC_CRED "verified contributions from mesh trades"
#define TL_SRC_BOND "not built yet: no bond can be posted"
#define TL_INPUTS(c, b, u, g)                                                                      \
    "\"inputs\":{\"credibility\":{\"value\":\"" c "\",\"source\":\"" TL_SRC_CRED "\"},"            \
    "\"bond\":{\"value\":\"" b "\",\"source\":\"" TL_SRC_BOND "\"},"                               \
    "\"uptime_bps\":{\"value\":\"" u "\",\"source\":\"not measured yet\"},"                        \
    "\"giving\":{\"value\":\"" g "\",\"source\":\"not tracked yet\"}}"
#define TL_NEXT(t, m, s, c, b, u, miss)                                                            \
    "{\"tier\":" #t ",\"metal\":\"" m "\",\"stone\":\"" s "\",\"credibility\":\"" c "\","          \
    "\"bond\":\"" b "\",\"uptime_bps\":\"" u "\",\"missing\":[" miss "]}"
#define TL_CARD(t, m, s, inputs, next, perks)                                                      \
    "{\"tier\":" #t ",\"metal\":\"" m "\",\"stone\":\"" s "\"," inputs ",\"next\":" next           \
    ",\"perks\":[" perks "]}"
#define TL_COPPER(c, b, u, g, miss)                                                                \
    TL_CARD(0, "Copper", "Obsidian", TL_INPUTS(c, b, u, g),                                        \
            TL_NEXT(1, "Bronze", "Carnelian", "100", "1000", "9000", miss), "")
#define TL_BRONZE_PERKS "\"free registration\",\"storefronts\""
#define TL_BRONZE(c, b, u, g, miss)                                                                \
    TL_CARD(1, "Bronze", "Carnelian", TL_INPUTS(c, b, u, g),                                       \
            TL_NEXT(2, "Silver", "Jade", "250", "10000", "9500", miss), TL_BRONZE_PERKS)

static const char *TL_CARD_JSON;

static void tl_card(zxvd_buf_t *o)
{
    zxvd_buf_raw(o, TL_CARD_JSON, strlen(TL_CARD_JSON));
}

/* refresh on `day` with this card */
static int tl_show(talisman_t *t, const char *card, uint32_t day)
{
    TL_CARD_JSON = card;
    return talisman_refresh(t, day);
}

APPS_TEST(talisman)
{
    static talisman_t T, keep;
    static const zxvd_view_ops_t VIEW = {NULL, NULL, tl_card};
    int64_t r = 0, m = 0;

    /* the rules (TM1, TM2) */
    CHECK(talisman_effective_cred(300, 0) == 300 && talisman_effective_cred(300, 50) == 350 &&
              talisman_effective_cred(300, 500) == 400 && talisman_effective_cred(2, 9) == 2 &&
              talisman_effective_cred(UINT64_MAX, UINT64_MAX) == UINT64_MAX,
          "effective credibility: giving counts up to a third, held at 64 bits");
    talisman_measure(50, false, 100, false, &r, &m);
    CHECK(r == 5000 && m == -50, "half way: %lld %lld", (long long) r, (long long) m);
    talisman_measure(UINT64_MAX, false, 100, false, &r, &m);
    CHECK(r == 10000 && m == TALISMAN_MARGIN_MAX, "far past: held at the threshold");
    talisman_measure(0, true, 1000, false, &r, &m);
    CHECK(r == 0 && m == -1, "missing: never met");
    talisman_measure(0, true, 0, false, &r, &m);
    CHECK(r == 0 && m == -1, "missing is never met, even against no threshold");
    talisman_measure(5, false, 0, false, &r, &m);
    CHECK(r == 10000 && m == 5, "no threshold: met");
    talisman_measure(10001, false, 9500, true, &r, &m);
    CHECK(r == 0 && m == -1, "an uptime past 10000 bps is no measurement");
    talisman_measure(10000, false, 9950, true, &r, &m);
    CHECK(r == 10000 && m == 50, "an uptime of 10000 bps meets Platinum's");
    talisman_measure(5, false, (uint64_t) TALISMAN_VALUE_MAX + 1u, false, &r, &m);
    CHECK(r == TALISMAN_REFUSED && m == 5 - (TALISMAN_VALUE_MAX + 1),
          "a threshold past 9(9): not measured, still short: %lld", (long long) m);

    /* the real dispatcher with no card view: zxvd says so, nothing kept (TM5) */
    talisman_init(&T, &LINK);
    zxvd_rpc_set_view_ops(NULL);
    int before = sent;
    CHECK(talisman_refresh(&T, 20000) == ZXM_E_REMOTE && LINK.err_code == -32601 &&
              sent == before + 1 && !T.have && T.nhist == 0 && !T.notice[0],
          "no card view: %s", LINK.err);

    /* a Copper card as a node has it today: bond and uptime reported as 0 */
    zxvd_rpc_set_view_ops(&VIEW);
    CHECK(tl_show(&T, TL_COPPER("50", "0", "0", "0", "\"credibility\",\"bond\",\"uptime_bps\""),
                  20000) == ZXM_OK &&
              T.have && T.tier == 0 && !strcmp(T.metal, "Copper") && !strcmp(T.stone, "Obsidian") &&
              T.in[TALISMAN_CRED].value == 50 && !T.in[TALISMAN_CRED].missing &&
              T.in[TALISMAN_BOND].missing && T.in[TALISMAN_UPTIME].missing &&
              T.in[TALISMAN_GIVING].missing && !strcmp(T.in[TALISMAN_BOND].source, TL_SRC_BOND) &&
              T.effective_cred == 50 && T.next.have && T.next.tier == 1 &&
              T.next.need[TALISMAN_BOND] == 1000 && T.next.reach_x100[TALISMAN_CRED] == 5000 &&
              T.next.reach_x100[TALISMAN_BOND] == 0 && T.next.margin[TALISMAN_UPTIME] == -1 &&
              !T.next.met[TALISMAN_CRED] && T.next.zxvd_missing[TALISMAN_BOND] && !T.next.earned &&
              T.nperks == 0,
          "Copper: %s", LINK.err);
    CHECK(!strcmp(T.notice, "Tier Copper / Obsidian. Next, Bronze / Carnelian: credibility 50 of "
                            "100 (50.00%); bond missing (" TL_SRC_BOND
                            "); uptime missing (not measured yet). Perks: none. Tiers give no "
                            "priority, discount or credit."),
          "the Copper notice: %s", T.notice);
    CHECK(T.nhist == 1 && talisman_mark(&T, 0)->day == 20000 && talisman_mark(&T, 0)->tier == 0 &&
              !talisman_mark(&T, 1),
          "the first mark");

    /* a missing input is never met, even when the others are (TM2) */
    CHECK(tl_show(&T, TL_COPPER("100", "1000", "0", "0", "\"uptime_bps\""), 20000) == ZXM_OK &&
              T.next.met[TALISMAN_CRED] && T.next.met[TALISMAN_BOND] &&
              !T.next.met[TALISMAN_UPTIME] && !T.next.earned && T.nhist == 1,
          "two of three met, uptime missing: not earned");

    /* Bronze: every input measured, Silver half way on bond; the tier changed */
    CHECK(tl_show(&T, TL_BRONZE("300", "5000", "9600", "0", "\"bond\""), 20001) == ZXM_OK &&
              T.tier == 1 && !T.in[TALISMAN_UPTIME].missing &&
              T.next.reach_x100[TALISMAN_CRED] == 10000 &&
              T.next.reach_x100[TALISMAN_BOND] == 5000 &&
              T.next.reach_x100[TALISMAN_UPTIME] == 10000 && T.next.margin[TALISMAN_CRED] == 50 &&
              T.next.margin[TALISMAN_BOND] == -5000 && T.next.margin[TALISMAN_UPTIME] == 100 &&
              !T.next.earned && T.nperks == 2 && !strcmp(T.perks[1], "storefronts"),
          "Bronze: %s", LINK.err);
    CHECK(!strcmp(T.notice, "Tier Bronze / Carnelian. Next, Silver / Jade: credibility 300 of 250 "
                            "(100.00%, met); bond 5000 of 10000 (50.00%); uptime 9600 of 9500 bps "
                            "(100.00%, met). Perks: free registration, storefronts. Tiers give no "
                            "priority, discount or credit."),
          "the Bronze notice: %s", T.notice);
    CHECK(T.nhist == 2 && talisman_mark(&T, 1)->day == 20001 && talisman_mark(&T, 1)->tier == 1,
          "the tier's change is marked");

    /* giving counts toward credibility up to a third (TM1): 200 + 50 meets 250 */
    CHECK(tl_show(&T, TL_BRONZE("200", "10000", "9500", "50", ""), 20001) == ZXM_OK &&
              T.effective_cred == 250 && T.next.met[TALISMAN_CRED] && T.next.earned &&
              strstr(T.notice, "credibility 250 of 250 (100.00%, met)") &&
              strstr(T.notice, "every threshold is met"),
          "giving counted, every threshold met: %s", T.notice);
    CHECK(tl_show(&T, TL_BRONZE("200", "10000", "9500", "1000", "\"credibility\""), 20001) ==
                  ZXM_OK &&
              T.effective_cred == 266 && T.next.earned,
          "giving held at a third of credibility");

    /* an uptime past 10000 bps is no measurement: missing, never met (TM2) */
    CHECK(tl_show(&T, TL_BRONZE("300", "10000", "10500", "0", ""), 20001) == ZXM_OK &&
              !T.next.met[TALISMAN_UPTIME] && !T.next.earned &&
              strstr(T.notice, "uptime missing (past 10000 bps: no measurement)"),
          "a malformed uptime: %s", T.notice);

    /* Platinum: no next tier; the perks; the change is marked */
    CHECK(tl_show(&T,
                  TL_CARD(4, "Platinum", "Ruby", TL_INPUTS("900", "1000000", "9990", "0"), "null",
                          TL_BRONZE_PERKS ",\"forwards\",\"IP notes\","
                                          "\"cross-border licensing eligibility\""),
                  20002) == ZXM_OK &&
              T.tier == 4 && !T.next.have && !T.next.earned && T.nperks == 5 &&
              !strcmp(T.notice, "Tier Platinum / Ruby, the highest a card earns. Perks: free "
                                "registration, storefronts, forwards, IP notes, cross-border "
                                "licensing eligibility. Tiers give no priority, discount or "
                                "credit.") &&
              T.nhist == 3 && talisman_mark(&T, 2)->tier == 4,
          "Platinum: %s", T.notice);

    /* replies that are not the card, and a day gone back, change nothing (TM4, TM5) */
    keep = T;
    static const char *const BAD[] = {
        TL_CARD(5, "Curzi", "Admas", TL_INPUTS("1", "1", "1", "1"), "null", ""),
        TL_CARD(0, "Copper", "Obsidian", TL_INPUTS("1", "1", "1", "1"), "null", ""),
        TL_CARD(0, "Copper", "Obsidian", TL_INPUTS("1", "1", "1", "1"),
                TL_NEXT(2, "Silver", "Jade", "250", "10000", "9500", ""), ""),
        TL_COPPER("1", "1", "1", "1", "\"giving\""),
        TL_COPPER("1.5", "1", "1", "1", ""),
        TL_COPPER("-1", "1", "1", "1", ""),
        TL_COPPER("", "1", "1", "1", ""),
        TL_COPPER("18446744073709551616", "1", "1", "1", ""),
        TL_CARD(0, "Copper", "Obsidian", TL_INPUTS("1", "1", "1", "1"),
                TL_NEXT(1, "Bronze", "Carnelian", "100", "1000", "9000", ""),
                "\"a\",\"b\",\"c\",\"d\",\"e\",\"f\",\"g\",\"h\",\"i\""),
        TL_CARD(0, "Copper", "Obsidian", TL_INPUTS("1", "1", "1", "1"),
                TL_NEXT(1, "Bronze", "Carnelian", "100", "1000", "9000", ""), "5"),
        TL_CARD(0, "Copper2", "Obsidian", TL_INPUTS("1", "1", "1", "1"),
                TL_NEXT(1, "Bronze", "Carnelian", "100", "1000", "9000", ""), ""),
        ("{\"tier\":0,\"metal\":\"Copper\",\"stone\":\"Obsidian\",\"inputs\":{\"credibility\":"
         "{\"value\":\"1\",\"source\":\"x\"}},\"next\":null,\"perks\":[]}"),
        "[1]",
    };
    for (uint32_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++)
        CHECK(tl_show(&T, BAD[i], 20003) == ZXM_E_PROTOCOL, "not a card (%u): %s", i, BAD[i]);
    before = sent;
    CHECK(tl_show(&T, TL_COPPER("1", "1", "1", "1", ""), 20001) == ZXM_E_ARG && sent == before,
          "a day before the last mark: nothing asked");
    CHECK(T.have && T.tier == keep.tier && T.nhist == keep.nhist && T.nperks == keep.nperks &&
              !strcmp(T.notice, keep.notice) && !memcmp(T.hist, keep.hist, sizeof T.hist),
          "nothing kept from a bad reply: %s", T.notice);

    /* the history holds TALISMAN_HISTORY marks; the oldest goes when full (TM4) */
    talisman_init(&T, &LINK);
    for (uint32_t d = 0; d < TALISMAN_HISTORY + 3u; d++)
        CHECK(tl_show(&T,
                      d % 2u ? TL_BRONZE("300", "5000", "9600", "0", "\"bond\"")
                             : TL_COPPER("50", "0", "0", "0", "\"bond\",\"uptime_bps\""),
                      30000u + d) == ZXM_OK,
              "day %u", d);
    CHECK(T.nhist == TALISMAN_HISTORY && T.dropped == 3 && talisman_mark(&T, 0)->day == 30003 &&
              talisman_mark(&T, 0)->tier == 1 &&
              talisman_mark(&T, TALISMAN_HISTORY - 1u)->day == 30000u + TALISMAN_HISTORY + 2u &&
              !talisman_mark(&T, TALISMAN_HISTORY),
          "a full history: %u marks, %u dropped", T.nhist, T.dropped);
    zxvd_rpc_set_view_ops(NULL);
}
