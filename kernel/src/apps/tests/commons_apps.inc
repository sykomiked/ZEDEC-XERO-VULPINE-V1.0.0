/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Commons's model. Groups go through zxvd's real dispatcher (LINK):
 * zxvd_people.c, which serves social.* in the daemon, is not linked into
 * this test, so the group methods are served by a small host that answers
 * in social.*'s own shapes (api/zxvd.openrpc.json), set as zxvd's people
 * ops as test_zxvd_rpc.c does. chat.*, pspace.* and irc.* are served in
 * the daemon by zxvd_chat.c, zxvd_pspace.c and zxvd_irc.c, none of which
 * this test links (the dispatcher answers -32601), so their answers are
 * made up in their documented shapes (FAKE). */
#include "commons_model.h"

#define CM_H16(x) x x x x x x x x x x x x x x x x
#define CM_H64(x) CM_H16(x) CM_H16(x) CM_H16(x) CM_H16(x)
#define CM_ME     CM_H64("a")
#define CM_BO     CM_H64("b")
#define CM_CY     CM_H64("c")
#define CM_P1     CM_H64("1")
#define CM_P2     CM_H64("2")
#define CM_P3     CM_H64("3")
#define CM_P4     CM_H64("4")
#define CM_CID1   "bafkreigardenroom"
#define CM_CID2   "bafkreiporchroom"
#define CM_CID3   "bafkreinewroom"
#define CM_OK     "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":"

#define CM_POST(id, author, mine, kind, lamport, text, reply, reacts)                              \
    "{\"id\":\"" id "\",\"author\":\"" author "\",\"mine\":" mine ",\"kind\":\"" kind              \
    "\",\"lamport\":\"" lamport "\",\"wall_ms\":\"1700000000000\",\"text\":\"" text                \
    "\",\"reply_to\":" reply ",\"replies\":0,\"reactions\":[" reacts "]}"

/* ---- the group host: social.* in its own shapes ---- */
typedef struct {
    char id[65], name[65];
    bool open;
    long long members;
    const char *role;
} cm_group_t;

static cm_group_t CMG[6];
static uint32_t CMG_N, CM_POSTS;
static char CM_LAST_GROUP[65], CM_LAST_TEXT[64];

static void cm_put_group(zxvd_buf_t *out, const cm_group_t *g)
{
    zxvd_buf_printf(
        out,
        "{\"id\":\"%s\",\"name\":\"%s\",\"kind\":\"group\",\"open\":%s,\"owner\":\"%s\","
        "\"members\":%lld,\"my_role\":\"%s\",\"invited\":false,\"applied\":0,"
        "\"ignored\":0,\"charter\":false}",
        g->id, g->name, g->open ? "true" : "false", CM_BO, g->members, g->role);
}

/* the string after "key":" in the params, into out */
static bool cm_str(const char *ps, const char *key, char *out, size_t cap)
{
    char k[32];
    snprintf(k, sizeof k, "\"%s\":\"", key);
    const char *p = strstr(ps, k);
    size_t n = 0;
    out[0] = 0;
    if (!p) return false;
    for (p += strlen(k); *p && *p != '"' && n + 1 < cap; p++) out[n++] = *p;
    out[n] = 0;
    return true;
}

static cm_group_t *cm_find(const char *id)
{
    for (uint32_t i = 0; i < CMG_N; i++)
        if (!strcmp(CMG[i].id, id)) return &CMG[i];
    return NULL;
}

static int cm_people(const char *method, const char *params, size_t len, zxvd_buf_t *out, char *msg,
                     size_t msg_cap)
{
    char ps[4096], g[80];
    if (len >= sizeof ps) len = sizeof ps - 1u;
    memcpy(ps, params, len);
    ps[len] = 0;
    if (!strcmp(method, "social.groups")) {
        zxvd_buf_raw(out, "{\"groups\":[", 11);
        for (uint32_t i = 0; i < CMG_N; i++) {
            if (i) zxvd_buf_raw(out, ",", 1);
            cm_put_group(out, &CMG[i]);
        }
        zxvd_buf_raw(out, "]}", 2);
        return 0;
    }
    if (!strcmp(method, "social.group_create")) {
        cm_group_t *n = &CMG[CMG_N++];
        snprintf(n->id, sizeof n->id, "%064x", 0xe0u + CMG_N);
        cm_str(ps, "name", n->name, sizeof n->name);
        n->open = !strstr(ps, "\"open\":false");
        n->members = 1;
        n->role = "owner";
        zxvd_buf_printf(out, "{\"group\":\"%s\",\"open\":%s,\"my_role\":\"owner\"}", n->id,
                        n->open ? "true" : "false");
        return 0;
    }
    if (!strcmp(method, "social.join") || !strcmp(method, "social.leave")) {
        cm_group_t *x = cm_str(ps, "group", g, sizeof g) ? cm_find(g) : NULL;
        bool join = !strcmp(method, "social.join");
        if (!x) {
            snprintf(msg, msg_cap, "no such group");
            return ZXVD_E_PEOPLE;
        }
        if (join && !x->open) {
            snprintf(msg, msg_cap, "invite only");
            return ZXVD_E_PEOPLE;
        }
        x->role = join ? "member" : "none";
        x->members += join ? 1 : -1;
        zxvd_buf_printf(out, "{\"id\":\"%s\",\"group\":\"%s\",\"my_role\":\"%s\",\"members\":%lld}",
                        CM_P4, x->id, x->role, x->members);
        return 0;
    }
    if (!strcmp(method, "social.post")) {
        CM_POSTS++;
        cm_str(ps, "group", CM_LAST_GROUP, sizeof CM_LAST_GROUP);
        cm_str(ps, "text", CM_LAST_TEXT, sizeof CM_LAST_TEXT);
        zxvd_buf_printf(out, "{\"id\":\"%s\",\"lamport\":\"12\",\"kind\":\"post\"}", CM_P4);
        return 0;
    }
    snprintf(msg, msg_cap, "not here");
    return -32601;
}

static commons_t CO;

APPS_TEST(commons)
{
    static const zxvd_people_ops_t people = {cm_people};
    static char big[COMMONS_TEXT_MAX + 2];
    char cid[COMMONS_CID_MAX + 1], gid[COMMONS_HEX_ID + 1];

    /* the start: this node's own room, always open to post (CR1) */
    commons_init(&CO, &LINK);
    CHECK(CO.nroom == 1 && CO.room[0].kind == COMMONS_ROOM_HERE && commons_may_post(&CO, 0) &&
              !commons_may_post(&CO, 1) && commons_unread(&CO, 0) == 0 &&
              commons_unread(&CO, 9) == COMMONS_REFUSED,
          "commons: this node's room, nothing unread, no other room");

    /* chat, pspace and irc are not served by the dispatcher this test links */
    CHECK(commons_me(&CO) == ZXM_E_REMOTE && LINK.err_code == -32601 && !CO.me[0],
          "commons: chat.me is not linked here: -32601 (%s)", LINK.err);
    CHECK(commons_spaces(&CO) == ZXM_E_REMOTE && commons_irc_status(&CO) == ZXM_E_REMOTE &&
              CO.nroom == 1,
          "commons: pspace.list and irc.status not linked here, nothing kept");

    /* ---- this node's chat, with zxvd's answers made up (FAKE) ---- */
    CO.link = &FAKE;
    CANNED = CM_OK "{\"node_id\":\"" CM_ME "\",\"saved\":true,\"records\":3,\"following\":[\"" CM_BO
                   "\"]}}";
    CHECK(commons_me(&CO) == ZXM_OK && !strcmp(CO.me, CM_ME) && CO.saved && CO.records == 3 &&
              CO.nfollowing == 1 && !strcmp(CO.following[0], CM_BO),
          "commons: chat.me: the owner and one followed (%s)", FAKE.err);
    CANNED = CM_OK "{\"node_id\":\"abc\",\"saved\":true,\"records\":3,\"following\":[]}}";
    CHECK(commons_me(&CO) == ZXM_E_PROTOCOL && !strcmp(CO.me, CM_ME) && CO.nfollowing == 1,
          "commons: a short NodeID refused, the owner kept");

    CANNED = CM_OK "{\"node_id\":\"" CM_CY "\",\"following\":true}}";
    CHECK(commons_follow(&CO, CM_CY, true) == ZXM_OK && CO.nfollowing == 2 &&
              strstr(last_req, "\"follow\":true") != NULL,
          "commons: follow Cy (%s)", FAKE.err);
    CANNED = CM_OK "{\"node_id\":\"" CM_BO "\",\"following\":false}}";
    CHECK(commons_follow(&CO, CM_BO, false) == ZXM_OK && CO.nfollowing == 1 &&
              !strcmp(CO.following[0], CM_CY) && commons_follow(&CO, CM_ME, true) == ZXM_E_ARG &&
              commons_follow(&CO, "ab", true) == ZXM_E_ARG,
          "commons: stop following Bo; never oneself or a short id");

    /* CM2: the feed in zxvd's order, never re-ranked; CM4: two posts by others unread */
    CANNED = CM_OK
        "{\"posts\":[" CM_POST(CM_P1, CM_BO, "false", "post", "7", "hello", "null", "") "," CM_POST(
            CM_P2, CM_ME, "true", "post", "8", "mine", "null",
            "") "," CM_POST(CM_P3, CM_CY, "false", "reply", "5", "hi back", "\"" CM_P1 "\"",
                            "{\"emoji\":\"+1\",\"count\":2,\"mine\":false}") "]}}";
    CHECK(commons_feed(&CO) == ZXM_OK && CO.nfeed == 3 && CO.feed[0].lamport == 7 &&
              CO.feed[1].lamport == 8 && CO.feed[2].lamport == 5 && CO.feed[1].mine &&
              CO.feed[2].kind == COMMONS_REPLY && !strcmp(CO.feed[2].reply_to, CM_P1) &&
              CO.feed[2].nreact == 1 && CO.feed[2].react[0].count == 2 &&
              commons_unread(&CO, 0) == 2,
          "commons: the feed as zxvd ranks it, two unread (%s)", FAKE.err);
    CHECK(commons_notice(&CO) == ZXM_OK && !strcmp(CO.notice, "2 new in this node."),
          "commons: the notice: %s", CO.notice);
    CHECK(commons_feed(&CO) == ZXM_OK && commons_unread(&CO, 0) == 2,
          "commons: the same feed again counts nothing twice");
    CHECK(commons_mark_read(&CO, 0) == ZXM_OK && commons_unread(&CO, 0) == 0 &&
              commons_feed(&CO) == ZXM_OK && commons_unread(&CO, 0) == 0 &&
              commons_notice(&CO) == ZXM_OK && !CO.notice[0] &&
              commons_mark_read(&CO, 9) == ZXM_E_ARG,
          "commons: read: none unread, and the same feed adds none");
    CANNED = CM_OK "{\"posts\":[" CM_POST(CM_P4, CM_BO, "false", "post", "9", "news", "null",
                                          "") "," CM_POST(CM_P1, CM_BO, "false", "post", "7",
                                                          "hello", "null", "") "]}}";
    CHECK(commons_feed(&CO) == ZXM_OK && commons_unread(&CO, 0) == 1 && CO.nfeed == 2,
          "commons: one new post past the read mark");
    CANNED = CM_OK "{\"posts\":[" CM_POST(CM_P4, CM_BO, "false", "post", "1000000000000000000",
                                          "far", "null", "") "]}}";
    CHECK(commons_feed(&CO) == ZXM_E_PROTOCOL && CO.nfeed == 2 && commons_unread(&CO, 0) == 1,
          "commons: a Lamport time past 18 digits refused (CR5), the page before kept");

    CANNED = CM_OK
        "{\"root\":" CM_POST(CM_P1, CM_BO, "false", "post", "7", "hello", "null",
                             "") ",\"replies\":[" CM_POST(CM_P3, CM_CY, "false", "reply", "8",
                                                          "hi back", "\"" CM_P1 "\"", "") "]}}";
    CHECK(commons_thread(&CO, CM_P1) == ZXM_OK && !strcmp(CO.root.id, CM_P1) && CO.nthread == 1 &&
              CO.thread_total == 1 && !strcmp(CO.thread[0].text, "hi back") &&
              commons_thread(&CO, CM_P2) == ZXM_E_PROTOCOL && commons_thread(&CO, "x") == ZXM_E_ARG,
          "commons: a thread; another root's answer refused (%s)", FAKE.err);

    /* CM3: post and react within the text limits (CR2) */
    memset(big, 'a', COMMONS_TEXT_MAX + 1u);
    big[COMMONS_TEXT_MAX + 1u] = 0;
    CANNED = CM_OK "{\"id\":\"" CM_P4 "\",\"author\":\"" CM_ME "\",\"kind\":\"post\"}}";
    CHECK(commons_post(&CO, 0, "   ", NULL) == ZXM_E_ARG &&
              commons_post(&CO, 0, "", NULL) == ZXM_E_ARG &&
              commons_post(&CO, 0, big, NULL) == ZXM_E_ARG &&
              commons_post(&CO, 0, "hi", "zz") == ZXM_E_ARG,
          "commons: blank, empty, 1025 bytes or a bad reply: not sent");
    big[COMMONS_TEXT_MAX] = 0;
    CHECK(commons_post(&CO, 0, big, NULL) == ZXM_OK && strstr(last_req, "chat.post") != NULL,
          "commons: 1024 bytes posted (%s)", FAKE.err);
    CHECK(commons_post(&CO, 0, "thanks", CM_P1) == ZXM_OK &&
              strstr(last_req, "\"reply_to\":\"" CM_P1 "\"") != NULL,
          "commons: a reply (%s)", FAKE.err);
    CANNED = CM_OK "{\"id\":\"" CM_P4 "\",\"kind\":\"reaction\"}}";
    CHECK(commons_react(&CO, CM_P1, "+1") == ZXM_OK && strstr(last_req, "chat.react") != NULL &&
              commons_react(&CO, CM_P1, "") == ZXM_E_ARG &&
              commons_react(&CO, CM_P1, "abcdefghijklmnopqrstuvwxyz0123456") == ZXM_E_ARG,
          "commons: a reaction of 1..32 bytes (%s)", FAKE.err);

    /* ---- rooms across nodes (CM5) ---- */
    CANNED = CM_OK "{\"online\":true,\"spaces\":[{\"cid\":\"" CM_CID1
                   "\",\"phase\":\"in\",\"members\":2,\"name\":\"Garden\",\"kind\":\"room\"},"
                   "{\"cid\":\"" CM_CID2 "\",\"phase\":\"joining\",\"members\":0}]}}";
    CHECK(commons_spaces(&CO) == ZXM_OK && CO.nroom == 3 && CO.online &&
              commons_find(&CO, CM_CID1) == 1 && commons_may_post(&CO, 1) &&
              !commons_may_post(&CO, 2) && !strcmp(CO.room[1].name, "Garden"),
          "commons: two spaces; one joined may be posted in, one joining may not (%s)", FAKE.err);
    CANNED = CM_OK "{\"posted\":true,\"cid\":\"" CM_CID1 "\"}}";
    CHECK(commons_post(&CO, 2, "early", NULL) == ZXM_E_STATE &&
              commons_post(&CO, 1, "hi", CM_P1) == ZXM_E_ARG &&
              commons_post(&CO, 1, "hello garden", NULL) == ZXM_OK &&
              strstr(last_req, "pspace.post") != NULL && strstr(last_req, CM_CID1) != NULL,
          "commons: posted in the Garden; not while joining, no replies (%s)", FAKE.err);

    CANNED = CM_OK "{\"events\":["
                   "{\"index\":1,\"kind\":\"post\",\"lamport\":1,\"created_ms\":1700000000000,"
                   "\"author\":\"" CM_BO "\",\"text\":\"one\"},"
                   "{\"index\":2,\"kind\":\"post\",\"lamport\":2,\"created_ms\":1700000000001,"
                   "\"author\":\"" CM_ME "\",\"text\":\"mine\"},"
                   "{\"index\":3,\"kind\":\"hall\",\"lamport\":3,\"created_ms\":1700000000002,"
                   "\"author\":\"" CM_BO "\",\"state\":\"play\",\"pos_ms\":0,\"rate_q16\":65536},"
                   "{\"index\":4,\"kind\":\"post\",\"lamport\":4,\"created_ms\":1700000000003,"
                   "\"author\":\"" CM_CY "\",\"text\":\"two\"}],\"next\":4}}";
    CHECK(commons_space_events(&CO, 1) == ZXM_OK && CO.nevents == 4 &&
              CO.events[1].code == COMMONS_EV_MINE && CO.events[2].code == COMMONS_EV_HALL &&
              commons_unread(&CO, 1) == 2 && CO.room[1].next == 4 &&
              strstr(last_req, "\"since\":0") != NULL,
          "commons: two posts by others among four events (WR2) (%s)", FAKE.err);
    CANNED = CM_OK "{\"events\":[],\"next\":4}}";
    CHECK(commons_space_events(&CO, 1) == ZXM_OK && strstr(last_req, "\"since\":4") != NULL &&
              commons_unread(&CO, 1) == 2 && commons_space_events(&CO, 0) == ZXM_E_ARG,
          "commons: asked again from 4, nothing new; this node has no events");
    CANNED = CM_OK "{\"events\":[{\"index\":2,\"kind\":\"post\",\"lamport\":1,\"created_ms\":1,"
                   "\"author\":\"" CM_BO "\",\"text\":\"old\"}],\"next\":5}}";
    CHECK(commons_space_events(&CO, 1) == ZXM_E_PROTOCOL && commons_unread(&CO, 1) == 2 &&
              CO.room[1].next == 4,
          "commons: an event before the one asked from is refused");
    CANNED = CM_OK "{\"events\":[{\"index\":4,\"kind\":\"post\",\"lamport\":1,\"created_ms\":1,"
                   "\"author\":\"" CM_BO "\",\"text\":\"again\"}],\"next\":4}}";
    CHECK(commons_space_events(&CO, 1) == ZXM_E_PROTOCOL && CO.room[1].next == 4,
          "commons: the event asked from is not sent again (since is exclusive)");
    CANNED = CM_OK "{\"events\":[{\"index\":6,\"kind\":\"post\",\"lamport\":1,\"created_ms\":1,"
                   "\"author\":\"" CM_BO "\",\"text\":\"past\"}],\"next\":5}}";
    CHECK(commons_space_events(&CO, 1) == ZXM_E_PROTOCOL && CO.room[1].next == 4,
          "commons: an event past next is refused");
    CHECK(commons_notice(&CO) == ZXM_OK &&
              !strcmp(CO.notice, "1 new in this node; 2 new in Garden."),
          "commons: the notice: %s", CO.notice);

    CANNED = CM_OK "{\"members\":[{\"id\":\"" CM_BO "\",\"addr\":\"10.0.0.2:9000\","
                   "\"last_seen_ms\":1}],\"count\":2}}";
    CHECK(commons_space_members(&CO, 1) == ZXM_OK && CO.nmember == 1 && CO.room[1].members == 2,
          "commons: the Garden's members (%s)", FAKE.err);
    CANNED = CM_OK "{\"members\":[],\"count\":1000000}}";
    CHECK(commons_space_members(&CO, 1) == ZXM_E_PROTOCOL && CO.room[1].members == 2,
          "commons: members past 6 digits refused (CR4)");

    CANNED = CM_OK "{\"cid\":\"" CM_CID3 "\"}}";
    CHECK(commons_space_create(&CO, "Porch talk", "Bad Topic", cid) == ZXM_E_ARG &&
              commons_space_create(&CO, "", NULL, cid) == ZXM_E_ARG &&
              commons_space_create(&CO, "Porch talk", "chat", cid) == ZXM_OK &&
              !strcmp(cid, CM_CID3) && strstr(last_req, "\"kind\":\"room\"") != NULL &&
              commons_may_post(&CO, (uint32_t) commons_find(&CO, CM_CID3)),
          "commons: a room made, hosted here (%s)", FAKE.err);
    CANNED = CM_OK "{\"cid\":\"" CM_CID2 "\",\"phase\":\"in\"}}";
    CHECK(commons_space_join(&CO, "Bnot") == ZXM_E_ARG &&
              commons_space_join(&CO, CM_CID2) == ZXM_OK && commons_may_post(&CO, 2) &&
              CO.nroom == 4,
          "commons: the porch joined (%s)", FAKE.err);
    CANNED = CM_OK "{\"online\":false,\"spaces\":[{\"cid\":\"" CM_CID1
                   "\",\"phase\":\"in\",\"members\":2,\"name\":\"Garden\"}]}}";
    CHECK(commons_spaces(&CO) == ZXM_OK && CO.nroom == 2 && !commons_may_post(&CO, 1) &&
              commons_unread(&CO, 1) == 2 && CO.room[1].next == 4,
          "commons: offline: no posting; the Garden keeps its counts (%s)", FAKE.err);

    /* ---- the IRC bridge: the owner's act (CM6) ---- */
    CANNED = CM_OK "{\"on\":true,\"listening\":true,\"port\":6697,\"clients\":1,\"password\":"
                   "\"00112233445566778899aabbccddeeff\",\"signer\":\"node\",\"delivered\":3,"
                   "\"refused\":0,\"bridge\":null}}";
    CHECK(commons_irc_status(&CO) == ZXM_OK && CO.irc.on && CO.irc.port == 6697 &&
              !CO.irc.bridged && CO.irc.delivered == 3,
          "commons: the gateway on, no bridge (%s)", FAKE.err);
    CANNED = CM_OK "{\"state\":\"connecting\",\"host\":\"irc.example.org\",\"port\":6697,"
                   "\"remote\":\"#zxv\",\"local\":\"#porch\",\"nick\":\"owner\",\"sent\":0,"
                   "\"received\":0,\"dropped\":0,\"queued\":0,\"error\":null}}";
    last_req[0] = 0;
    CHECK(commons_irc_bridge(&CO, true, "irc.example.org", 6697, "#zxv", "#porch", "owner", NULL,
                             false) == ZXM_E_STATE &&
              !last_req[0] && !CO.irc.bridged,
          "commons: not confirmed: nothing sent");
    CHECK(commons_irc_bridge(&CO, true, "irc.example.org", 6697, "#zxv", "#swarm", "owner", NULL,
                             true) == ZXM_E_ARG &&
              commons_irc_bridge(&CO, true, "irc.example.org", 0, "#zxv", "#porch", "owner", NULL,
                                 true) == ZXM_E_ARG &&
              commons_irc_bridge(&CO, true, "irc.example.org", 6697, "zxv", "#porch", "owner", NULL,
                                 true) == ZXM_E_ARG &&
              !last_req[0],
          "commons: #swarm, port 0 or a channel without # refused");
    CHECK(commons_irc_bridge(&CO, true, "irc.example.org", 6697, "#zxv", "#porch", "owner", "pw",
                             true) == ZXM_OK &&
              CO.irc.bridged && !strcmp(CO.irc.state, "connecting") &&
              strstr(last_req, "\"password\":\"pw\"") != NULL,
          "commons: bridged on the owner's word (%s)", FAKE.err);
    CANNED = CM_OK "null}";
    CHECK(commons_irc_bridge(&CO, false, NULL, 0, NULL, NULL, NULL, NULL, true) == ZXM_OK &&
              !CO.irc.bridged,
          "commons: the bridge ended (%s)", FAKE.err);
    CANNED = CM_OK "{\"msgid\":\"0123456789abcdef0123456789abcdef\",\"target\":\"#porch\","
                   "\"nick\":\"bo\",\"truth\":\"B\",\"ms\":\"1700000000000\",\"text\":\"both\"}}";
    CHECK(commons_irc_record(&CO, "0123456789abcdef0123456789abcdef") == ZXM_OK &&
              CO.irc_record.truth == 'B' && CO.irc_record.ms == 1700000000000LL &&
              commons_irc_record(&CO, "0123") == ZXM_E_ARG,
          "commons: a relayed record (%s)", FAKE.err);
    CANNED = NULL;

    /* ---- groups through the dispatcher ---- */
    CO.link = &LINK;
    zxvd_rpc_set_people_ops(NULL);
    CHECK(commons_groups(&CO) == ZXM_E_REMOTE && LINK.err_code == -32601,
          "commons: no people group: -32601 (%s)", LINK.err);
    zxvd_rpc_set_people_ops(&people);
    CMG_N = 2;
    snprintf(CMG[0].id, sizeof CMG[0].id, "%064x", 0xd1u);
    snprintf(CMG[0].name, sizeof CMG[0].name, "Allotment");
    CMG[0].open = true, CMG[0].members = 4, CMG[0].role = "none";
    snprintf(CMG[1].id, sizeof CMG[1].id, "%064x", 0xd2u);
    snprintf(CMG[1].name, sizeof CMG[1].name, "Choir");
    CMG[1].open = false, CMG[1].members = 9, CMG[1].role = "member";
    CHECK(commons_groups(&CO) == ZXM_OK && CO.nroom == 4 && commons_unread(&CO, 1) == 2 &&
              CO.room[2].kind == COMMONS_ROOM_GROUP && !commons_may_post(&CO, 2) &&
              commons_may_post(&CO, 3),
          "commons: two groups; posting in the one joined (%s)", LINK.err);
    CHECK(commons_post(&CO, 2, "hello", NULL) == ZXM_E_STATE && CM_POSTS == 0 &&
              commons_join(&CO, CMG[0].id) == ZXM_OK && commons_may_post(&CO, 2) &&
              CO.room[2].members == 5 && commons_post(&CO, 2, "hello", NULL) == ZXM_OK &&
              CM_POSTS == 1 && !strcmp(CM_LAST_GROUP, CMG[0].id) && !strcmp(CM_LAST_TEXT, "hello"),
          "commons: joined the Allotment, then posted there (%s)", LINK.err);
    CHECK(commons_join(&CO, CMG[0].id) == ZXM_E_STATE && commons_leave(&CO, CMG[1].id) == ZXM_OK &&
              !commons_may_post(&CO, 3) && commons_leave(&CO, CMG[1].id) == ZXM_E_STATE,
          "commons: no second join; left the Choir (%s)", LINK.err);
    CHECK(commons_group_create(&CO, "Porch", false, gid) == ZXM_OK && strlen(gid) == 64u &&
              commons_may_post(&CO, (uint32_t) commons_find(&CO, gid)) &&
              commons_leave(&CO, gid) == ZXM_E_STATE &&
              commons_group_create(&CO, "", true, gid) == ZXM_E_ARG,
          "commons: a group founded; its owner cannot leave (%s)", LINK.err);
    CMG[0].role = "observer";
    CHECK(commons_groups(&CO) == ZXM_OK && !commons_may_post(&CO, 2) && CO.nroom == 5,
          "commons: an observer may not post");
    zxvd_rpc_set_people_ops(NULL);
}
