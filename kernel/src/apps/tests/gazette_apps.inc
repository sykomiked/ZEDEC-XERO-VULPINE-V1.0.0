/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Gazette's model. social.feed goes through zxvd's real dispatcher (LINK):
 * zxvd_people.c, which serves social.* in the daemon, is not linked into
 * this test, so social.feed is served by a small host that answers in its
 * own shape (api/zxvd.openrpc.json), set as zxvd's people ops as
 * test_zxvd_rpc.c does. chat.*, web4.feed, pspace.* and irc.* are served
 * in the daemon by zxvd_chat.c, zxvd_web4.c, zxvd_pspace.c and
 * zxvd_irc.c, none of which this test links (the dispatcher answers
 * -32601), so their answers are made up in their documented shapes
 * (FAKE). */
#include "gazette_model.h"

#define GZ_H16(x) x x x x x x x x x x x x x x x x
#define GZ_H64(x) GZ_H16(x) GZ_H16(x) GZ_H16(x) GZ_H16(x)
#define GZ_ME     GZ_H64("a")
#define GZ_BO     GZ_H64("b")
#define GZ_CY     GZ_H64("c")
#define GZ_DI     GZ_H64("d")
#define GZ_OWN    GZ_H64("E") /* the owner's Web4 companion, upper case as zxvd may give it */
#define GZ_FAR    GZ_H64("F") /* another companion */
#define GZ_P1     GZ_H64("1")
#define GZ_P2     GZ_H64("2")
#define GZ_P3     GZ_H64("3")
#define GZ_P4     GZ_H64("4")
#define GZ_P5     GZ_H64("5")
#define GZ_P6     GZ_H64("6")
#define GZ_P7     GZ_H64("7")
#define GZ_CID    "bafkreigh2akiscaildcqabsyg3dfr6chu3fgpregiymsck7e7aqa4s52zy"
#define GZ_MSG    "0123456789abcdef0123456789abcdef"
#define GZ_OK     "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":"

#define GZ_POST(id, author, mine, kind, lamport, wall, text)                                       \
    "{\"id\":\"" id "\",\"author\":\"" author "\",\"mine\":" mine ",\"kind\":\"" kind              \
    "\",\"lamport\":\"" lamport "\",\"wall_ms\":\"" wall "\",\"text\":\"" text                     \
    "\",\"reply_to\":null,\"replies\":4,\"reactions\":[]}"

#define GZ_ENV(id, kind, status, from, payload, ts)                                                \
    "{\"id\":" id ",\"kind\":\"" kind "\",\"status\":\"" status "\",\"from\":\"" from              \
    "\",\"to\":\"" GZ_ME "\",\"tool\":\"news\",\"req_id\":\"1\",\"chunk\":0,\"final\":true,"       \
    "\"payload\":\"" payload "\",\"pii\":false,\"amount_minor\":\"0\",\"seq\":\"1\",\"ts\":" ts    \
    ",\"consent\":null,\"answers\":null,\"grant\":null,\"bytes\":10}"

static gazette_t GZ;
static char GZ_BUF[16384];
static uint32_t GZ_FEEDS;

/* the item with this id, or -1 */
static int32_t gz_item(const char *id)
{
    for (uint32_t i = 0; i < GZ.nitem; i++)
        if (!strcmp(GZ.item[i].id, id)) return (int32_t) i;
    return -1;
}

/* pspace.events: n posts by Bo from index `from`, next from + n */
static const char *gz_events(uint32_t from, uint32_t n)
{
    uint32_t len = (uint32_t) snprintf(GZ_BUF, sizeof GZ_BUF, "%s{\"events\":[", GZ_OK);
    for (uint32_t i = 0; i < n; i++)
        len += (uint32_t) snprintf(GZ_BUF + len, sizeof GZ_BUF - len,
                                   "%s{\"index\":%u,\"kind\":\"post\",\"lamport\":%u,"
                                   "\"created_ms\":1700000000000,\"author\":\"%s\",\"text\":\"e\"}",
                                   i ? "," : "", from + i, from + i + 1u, GZ_BO);
    snprintf(GZ_BUF + len, sizeof GZ_BUF - len, "],\"next\":%u}}", from + n);
    return GZ_BUF;
}

/* ---- the people host: social.feed in its own shape ---- */
static int gz_people(const char *method, const char *params, size_t len, zxvd_buf_t *out, char *msg,
                     size_t msg_cap)
{
    (void) params;
    (void) len;
    if (strcmp(method, "social.feed")) {
        snprintf(msg, msg_cap, "not here");
        return -32601;
    }
    GZ_FEEDS++;
    zxvd_buf_printf(
        out,
        "{\"me\":\"%s\",\"held\":3,\"candidates\":3,\"diversity\":0,\"items\":["
        "{\"id\":\"%s\",\"author\":\"%s\",\"mine\":false,\"kind\":\"post\",\"parent\":null,"
        "\"group\":null,\"text\":\"social one\",\"lamport\":\"20\",\"wall_ms\":null,"
        "\"reactions\":[{\"reaction\":\"+1\",\"count\":9,\"mine\":false}],\"replies\":7,"
        "\"score\":65536,\"surplus\":65536,\"fresh\":0,\"rep\":0},"
        "{\"id\":\"%s\",\"author\":\"%s\",\"mine\":true,\"kind\":\"post\",\"parent\":null,"
        "\"group\":null,\"text\":\"my own\",\"lamport\":\"21\",\"wall_ms\":\"1700000000000\","
        "\"reactions\":[],\"replies\":0,\"score\":1,\"surplus\":1,\"fresh\":0,\"rep\":0}]}",
        GZ_ME, GZ_P7, GZ_CY, GZ_P5, GZ_ME);
    return 0;
}

APPS_TEST(gazette)
{
    static const zxvd_people_ops_t people = {gz_people};
    uint32_t view[GAZETTE_ITEMS], space = 0, chan = 0;

    gazette_init(&GZ, &LINK);
    CHECK(GZ.nsrc == 0 && GZ.nitem == 0 && gazette_unread_total(&GZ) == 0 &&
              gazette_unread(&GZ, 0) == GAZETTE_REFUSED && !gazette_shown(&GZ, 0) &&
              gazette_view(&GZ, view, GAZETTE_ITEMS) == 0,
          "gazette: nothing followed, nothing unread");
    CHECK(gazette_me(&GZ) == ZXM_E_REMOTE && LINK.err_code == -32601 && !GZ.me[0],
          "gazette: chat.me is not linked here: -32601 (%s)", LINK.err);

    /* ---- GM1: people followed through zxvd (FAKE) ---- */
    GZ.link = &FAKE;
    CANNED = GZ_OK "{\"node_id\":\"" GZ_ME "\",\"saved\":true,\"records\":3,\"following\":[\"" GZ_BO
                   "\"]}}";
    CHECK(gazette_me(&GZ) == ZXM_OK && !strcmp(GZ.me, GZ_ME) && GZ.nsrc == 1 &&
              GZ.src[0].kind == GAZETTE_SRC_NODE && GZ.src[0].followed &&
              !strcmp(GZ.src[0].key, GZ_BO),
          "gazette: chat.me: the owner follows Bo (%s)", FAKE.err);
    CANNED = GZ_OK "{\"node_id\":\"" GZ_CY "\",\"following\":true}}";
    CHECK(gazette_follow(&GZ, GZ_CY, true) == ZXM_OK && GZ.nsrc == 2 && GZ.src[1].followed &&
              strstr(last_req, "\"follow\":true") != NULL &&
              gazette_follow(&GZ, GZ_ME, true) == ZXM_E_ARG &&
              gazette_follow(&GZ, "ab", true) == ZXM_E_ARG,
          "gazette: follow Cy; never oneself or a short id (%s)", FAKE.err);
    CANNED = GZ_OK "{\"node_id\":\"" GZ_CY "\",\"following\":true}}";
    CHECK(gazette_follow(&GZ, GZ_CY, false) == ZXM_E_PROTOCOL && GZ.src[1].followed,
          "gazette: zxvd's answer disagrees with the ask: refused, kept followed");

    /* ---- GM2, GM3: chat.feed in zxvd's order; the owner's own and reactions left out ---- */
    CANNED = GZ_OK
        "{\"posts\":[" GZ_POST(GZ_P1, GZ_BO, "false", "post", "7", "1700000000000", "hello") "," GZ_POST(
            GZ_P2, GZ_ME, "true", "post", "8", "1700000000001",
            "mine") "," GZ_POST(GZ_P3, GZ_CY, "false", "reply", "5", "1700000000002",
                                "hi back") "," GZ_POST(GZ_P4, GZ_DI, "false", "reaction", "9",
                                                       "1700000000003",
                                                       "+1") "," GZ_POST(GZ_P5, GZ_DI, "false",
                                                                         "post", "3",
                                                                         "1700000000004",
                                                                         "from di") "]}}";
    CHECK(gazette_chat(&GZ) == ZXM_OK && GZ.nitem == 3 && GZ.nsrc == 3 &&
              !strcmp(GZ.item[0].id, GZ_P1) && !strcmp(GZ.item[1].id, GZ_P3) &&
              !strcmp(GZ.item[2].id, GZ_P5) && GZ.item[0].order == 7 && GZ.item[1].order == 5 &&
              GZ.item[0].stamp == 1700000000000LL && !GZ.src[2].followed &&
              !strcmp(GZ.src[2].key, GZ_DI) && gazette_find(&GZ, GAZETTE_SRC_NODE, GZ_ME) < 0,
          "gazette: three posts by others, as zxvd ranks them (%s)", FAKE.err);
    CHECK(gazette_view(&GZ, view, GAZETTE_ITEMS) == 2 && view[0] == 0 && view[1] == 1 &&
              !gazette_shown(&GZ, 2) && gazette_unread(&GZ, 0) == 1 &&
              gazette_unread(&GZ, 2) == 1 && gazette_unread_total(&GZ) == 2,
          "gazette: Di is not followed: two shown, two in the badge");
    CHECK(gazette_chat(&GZ) == ZXM_OK && GZ.nitem == 3 && gazette_unread_total(&GZ) == 2,
          "gazette: the same page again adds nothing");
    CHECK(gazette_mute(&GZ, 1, true) == ZXM_OK && gazette_unread_total(&GZ) == 1 &&
              gazette_view(&GZ, view, GAZETTE_ITEMS) == 1 &&
              gazette_mute(&GZ, 1, false) == ZXM_OK && gazette_mute(&GZ, 9, true) == ZXM_E_ARG &&
              gazette_keep(&GZ, 2, true) == ZXM_E_STATE,
          "gazette: muting Cy hides Cy; a person is followed through zxvd only");

    /* GM7: a notice only when the owner asked for one */
    CHECK(gazette_notice(&GZ) == ZXM_OK && !GZ.notice[0], "gazette: no notices unless asked");
    gazette_notices(&GZ, true);
    CHECK(gazette_notice(&GZ) == ZXM_OK && !strcmp(GZ.notice, "2 new from the sources you follow."),
          "gazette: the notice: %s", GZ.notice);

    /* GM5: read */
    CHECK(gazette_mark_read(&GZ, 0) == ZXM_OK && gazette_unread(&GZ, 0) == 0 &&
              gazette_mark_read(&GZ, 0) == ZXM_OK && GZ.src[0].seen == 1 &&
              gazette_mark_source_read(&GZ, 1) == ZXM_OK && gazette_unread_total(&GZ) == 0 &&
              gazette_notice(&GZ) == ZXM_OK && !GZ.notice[0] &&
              gazette_mark_read(&GZ, 99) == ZXM_E_ARG &&
              gazette_mark_source_read(&GZ, 99) == ZXM_E_ARG,
          "gazette: Bo's and Cy's read; nothing in the badge");

    /* GR3: a page with a stamp past 13 digits is refused whole */
    CANNED = GZ_OK "{\"posts\":[" GZ_POST(GZ_P6, GZ_BO, "false", "post", "11", "1700000000000",
                                          "fine") "," GZ_POST(GZ_P4, GZ_BO, "false", "post", "12",
                                                              "10000000000000", "far") "]}}";
    CHECK(gazette_chat(&GZ) == ZXM_E_PROTOCOL && GZ.nitem == 3 && GZ.src[0].newest == 1,
          "gazette: a stamp past i13 refused, the list before kept");

    /* GM2: read items let go are not brought back */
    CHECK(gazette_drop_read(&GZ) == 2 && GZ.nitem == 1 && !strcmp(GZ.item[0].id, GZ_P5) &&
              GZ.src[0].mark == 7,
          "gazette: two read items let go");
    CANNED = GZ_OK "{\"posts\":[" GZ_POST(GZ_P1, GZ_BO, "false", "post", "7", "1700000000000",
                                          "hello") "," GZ_POST(GZ_P6, GZ_BO, "false", "post", "11",
                                                               "1700000000005", "news") "]}}";
    CHECK(gazette_chat(&GZ) == ZXM_OK && GZ.nitem == 2 && gz_item(GZ_P1) < 0 &&
              gz_item(GZ_P6) == 1 && gazette_unread(&GZ, 0) == 1,
          "gazette: a let-go post stays gone, the new one comes (%s)", FAKE.err);

    /* ---- GM6: Web4, signed envelopes from other companions only ---- */
    CANNED = GZ_OK "{\"open\":true,\"agent_id\":\"" GZ_OWN "\",\"envelopes\":[" GZ_ENV(
        "3", "RESPONSE", "signed", GZ_FAR, "rates are 5",
        "1700000000") "," GZ_ENV("2", "REQUEST", "signed", GZ_OWN, "ask",
                                 "1700000000") "," GZ_ENV("1", "CONSENT", "signed", GZ_FAR, "",
                                                          "1700000000") "],\"next\":null,"
                                                                        "\"total\":3,\"held\":0}}";
    CHECK(gazette_web4(&GZ, 20) == ZXM_OK && GZ.nitem == 3 && !strcmp(GZ.item[2].id, "3") &&
              GZ.item[2].stamp == 1700000000000LL && !strcmp(GZ.item[2].text, "rates are 5") &&
              strstr(last_req, "\"status\":\"signed\"") != NULL &&
              gazette_find(&GZ, GAZETTE_SRC_AGENT, GZ_H64("f")) == 3 && !gazette_shown(&GZ, 2),
          "gazette: one envelope from another companion, not followed yet (%s)", FAKE.err);
    CHECK(gazette_keep(&GZ, 3, true) == ZXM_OK && gazette_shown(&GZ, 2) &&
              gazette_unread_total(&GZ) == 2 && gazette_web4(&GZ, 0) == ZXM_E_ARG,
          "gazette: the companion followed here");
    CANNED = GZ_OK "{\"open\":false,\"agent_id\":null,\"envelopes\":[" GZ_ENV(
        "4", "REQUEST", "signed", GZ_FAR, "x", "1") "],\"next\":null,\"total\":1,\"held\":0}}";
    CHECK(gazette_web4(&GZ, 20) == ZXM_E_PROTOCOL && GZ.nitem == 3,
          "gazette: envelopes from a closed feed refused");

    /* ---- spaces: Seedbank's CID rule, then pspace.events ---- */
    CHECK(gazette_add_space(&GZ, "bafkrei", &space) == ZXM_E_ARG &&
              gazette_add_space(&GZ, GZ_CID, &space) == ZXM_OK && space == 4 &&
              GZ.src[4].followed && gazette_space(&GZ, 0) == ZXM_E_ARG,
          "gazette: a 59-byte CID followed; a short one refused");
    CANNED = GZ_OK "{\"events\":["
                   "{\"index\":0,\"kind\":\"post\",\"lamport\":1,\"created_ms\":1700000000000,"
                   "\"author\":\"" GZ_BO "\",\"text\":\"one\"},"
                   "{\"index\":1,\"kind\":\"hall\",\"lamport\":2,\"created_ms\":1700000000001,"
                   "\"author\":\"" GZ_BO "\",\"state\":\"play\",\"pos_ms\":0,\"rate_q16\":65536},"
                   "{\"index\":2,\"kind\":\"post\",\"lamport\":3,\"created_ms\":1700000000002,"
                   "\"author\":\"" GZ_ME "\",\"text\":\"mine\"},"
                   "{\"index\":3,\"kind\":\"post\",\"lamport\":4,\"created_ms\":1700000000003,"
                   "\"author\":\"" GZ_CY "\",\"text\":\"two\"}],\"next\":4}}";
    CHECK(gazette_space(&GZ, space) == ZXM_OK && GZ.nitem == 5 && gazette_unread(&GZ, space) == 2 &&
              GZ.src[space].next == 4 && strstr(last_req, "\"since\":0") != NULL &&
              strstr(last_req, GZ_CID) != NULL,
          "gazette: two posts by others among four events (%s)", FAKE.err);
    CANNED = GZ_OK "{\"events\":[{\"index\":2,\"kind\":\"post\",\"lamport\":1,\"created_ms\":1,"
                   "\"author\":\"" GZ_BO "\",\"text\":\"old\"}],\"next\":5}}";
    CHECK(gazette_space(&GZ, space) == ZXM_E_PROTOCOL && strstr(last_req, "\"since\":4") != NULL &&
              GZ.src[space].next == 4 && GZ.nitem == 5,
          "gazette: an event before the one asked from is refused");

    /* ---- IRC: one relayed record by its msgid ---- */
    CHECK(gazette_add_channel(&GZ, "news", &chan) == ZXM_E_ARG &&
              gazette_add_channel(&GZ, "#news", &chan) == ZXM_OK && chan == 5,
          "gazette: #news followed; a name without # refused");
    CANNED = GZ_OK "{\"msgid\":\"" GZ_MSG "\",\"target\":\"#news\",\"nick\":\"bo\",\"truth\":\"T\","
                   "\"ms\":\"1700000000009\",\"text\":\"the bridge is open\"}}";
    CHECK(gazette_irc(&GZ, "0123") == ZXM_E_ARG && gazette_irc(&GZ, GZ_MSG) == ZXM_OK &&
              GZ.nitem == 6 && GZ.item[5].source == chan && !strcmp(GZ.item[5].by, "bo") &&
              gazette_shown(&GZ, 5) && gazette_irc(&GZ, GZ_MSG) == ZXM_OK && GZ.nitem == 6,
          "gazette: a relayed message, once (%s)", FAKE.err);

    /* ---- GM2: the list is bounded; read items make room, unread ones never go ---- */
    {
        uint32_t from = 4;
        int rc = ZXM_OK;
        while (rc == ZXM_OK && GZ.nitem + GAZETTE_EVENTS <= GAZETTE_ITEMS) {
            CANNED = gz_events(from, GAZETTE_EVENTS);
            rc = gazette_space(&GZ, space);
            from += GAZETTE_EVENTS;
        }
        uint32_t held = GZ.nitem;
        CANNED = gz_events(from, GAZETTE_EVENTS);
        CHECK(rc == ZXM_OK && gazette_space(&GZ, space) == ZXM_E_TOOBIG && GZ.nitem == held &&
                  GZ.src[space].next == (int64_t) from,
              "gazette: a full list of unread items takes no more (%u held)", held);
        CHECK(gazette_mark_source_read(&GZ, space) == ZXM_OK &&
                  gazette_space(&GZ, space) == ZXM_OK && GZ.nitem <= GAZETTE_ITEMS &&
                  gazette_unread(&GZ, space) == GAZETTE_EVENTS && gz_item(GZ_MSG) >= 0,
              "gazette: the space's read items let go to make room (%s)", FAKE.err);
    }
    CANNED = NULL;

    /* ---- social.feed through the dispatcher ---- */
    GZ.link = &LINK;
    zxvd_rpc_set_people_ops(NULL);
    CHECK(gazette_social(&GZ, 20) == ZXM_E_REMOTE && LINK.err_code == -32601,
          "gazette: no people group: -32601 (%s)", LINK.err);
    zxvd_rpc_set_people_ops(&people);
    CHECK(gazette_social(&GZ, 0) == ZXM_E_ARG && gazette_social(&GZ, 51) == ZXM_E_ARG &&
              GZ_FEEDS == 0,
          "gazette: a limit outside 1..50 is never sent");
    {
        int64_t before = gazette_unread(&GZ, 1);
        uint32_t own = 0;
        int rc = gazette_social(&GZ, 20);
        int32_t at = gz_item(GZ_P7);
        for (uint32_t i = 0; i < GZ.nitem; i++)
            own += GZ.item[i].feed == GAZETTE_SOCIAL && !strcmp(GZ.item[i].id, GZ_P5);
        CHECK(rc == ZXM_OK && GZ_FEEDS == 1 && at >= 0 && GZ.item[at].feed == GAZETTE_SOCIAL &&
                  GZ.item[at].stamp == 0 && !strcmp(GZ.item[at].text, "social one") && !own &&
                  gazette_unread(&GZ, 1) == before + 1,
              "gazette: Cy's social post kept, the owner's own left out (%s)", LINK.err);
    }
    zxvd_rpc_set_people_ops(NULL);
}
