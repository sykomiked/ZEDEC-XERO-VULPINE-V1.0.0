/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Hamlet's model. The circle goes through zxvd's real dispatcher (LINK):
 * zxvd_people.c, which serves social.* in the daemon, is not linked into
 * this test, so social.groups, social.join, social.leave, social.feed,
 * social.post and social.react are served by a small host that answers in
 * their own shapes (api/zxvd.openrpc.json), set as zxvd's people ops as
 * test_zxvd_rpc.c does. The Web4 feed group (zxvd_web4.c: web4.post,
 * web4.consent) is not linked either, so a gift's answers are made up in
 * the shape zxvd_web4.c writes them (FAKE), and each request is checked
 * as sent. */
#include "hamlet_model.h"

#define HM_H16(x) x x x x x x x x x x x x x x x x
#define HM_H64(x) HM_H16(x) HM_H16(x) HM_H16(x) HM_H16(x)
#define HM_ME     HM_H64("a")
#define HM_BO     HM_H64("b")
#define HM_CY     HM_H64("c")
#define HM_G1     HM_H64("1")
#define HM_G2     HM_H64("2")
#define HM_G3     HM_H64("3")
#define HM_P1     HM_H64("4")
#define HM_P2     HM_H64("5")
#define HM_P3     HM_H64("6")
#define HM_P4     HM_H64("7")
#define HM_P5     HM_H64("8")
#define HM_P6     HM_H64("9")
#define HM_TO     HM_H64("d")

/* one item of social.feed in its own shape */
#define HM_ITEM(id, author, mine, kind, parent, group, lamport, reacts)                            \
    "{\"id\":\"" id "\",\"author\":\"" author "\",\"mine\":" mine ",\"kind\":\"" kind              \
    "\",\"parent\":" parent ",\"group\":" group ",\"text\":\"hello\",\"lamport\":\"" lamport       \
    "\",\"wall_ms\":null,\"reactions\":[" reacts "],\"replies\":0,\"score\":65536,"                \
    "\"surplus\":65536,\"fresh\":0,\"rep\":0}"
#define HM_Q(x) "\"" x "\""

/* one Web4 feed entry as zxvd_web4.c put_entry writes it */
#define HM_ENTRY(id, status, amount, ts)                                                           \
    "{\"id\":" #id ",\"kind\":\"REQUEST\",\"status\":\"" status "\",\"from\":\"" HM_ME             \
    "\",\"to\":\"" HM_TO "\",\"tool\":\"gift\",\"req_id\":\"" #id "\",\"chunk\":0,"                \
    "\"final\":false,\"payload\":\"for the seeds\",\"pii\":false,\"amount_minor\":\"" amount       \
    "\",\"seq\":null,\"ts\":" ts ",\"consent\":null,\"answers\":null,\"grant\":null,\"bytes\":0}"
#define HM_OK "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":"

/* ---- the circle's host: social.* in its own shapes ---- */
typedef struct {
    const char *id, *name, *role;
    long long members;
} hm_group_t;

static hm_group_t HMG[3];
static const char *HM_FEED;
static uint32_t HM_POSTS, HM_REACTS;
static char HM_LAST[4096];

static hm_group_t *hm_find(const char *ps)
{
    for (uint32_t i = 0; i < 3; i++) {
        char k[96];
        snprintf(k, sizeof k, "\"group\":\"%s\"", HMG[i].id);
        if (strstr(ps, k)) return &HMG[i];
    }
    return NULL;
}

static int hm_people(const char *method, const char *params, size_t len, zxvd_buf_t *out, char *msg,
                     size_t msg_cap)
{
    if (len >= sizeof HM_LAST) len = sizeof HM_LAST - 1u;
    memcpy(HM_LAST, params, len);
    HM_LAST[len] = 0;
    if (!strcmp(method, "social.groups")) {
        zxvd_buf_raw(out, "{\"groups\":[", 11);
        for (uint32_t i = 0; i < 3; i++)
            zxvd_buf_printf(out,
                            "%s{\"id\":\"%s\",\"name\":\"%s\",\"kind\":\"group\",\"open\":true,"
                            "\"owner\":\"%s\",\"members\":%lld,\"my_role\":\"%s\","
                            "\"invited\":false,\"applied\":0,\"ignored\":0,\"charter\":false}",
                            i ? "," : "", HMG[i].id, HMG[i].name, HM_BO, HMG[i].members,
                            HMG[i].role);
        zxvd_buf_raw(out, "]}", 2);
        return 0;
    }
    if (!strcmp(method, "social.join") || !strcmp(method, "social.leave")) {
        hm_group_t *g = hm_find(HM_LAST);
        bool join = !strcmp(method, "social.join");
        if (!g) {
            snprintf(msg, msg_cap, "no such group");
            return ZXVD_E_PEOPLE;
        }
        g->role = join ? "member" : "none";
        g->members += join ? 1 : -1;
        zxvd_buf_printf(out, "{\"id\":\"%s\",\"group\":\"%s\",\"my_role\":\"%s\",\"members\":%lld}",
                        HM_P6, g->id, g->role, g->members);
        return 0;
    }
    if (!strcmp(method, "social.feed")) {
        zxvd_buf_printf(out,
                        "{\"me\":\"%s\",\"held\":6,\"candidates\":6,\"diversity\":30000,"
                        "\"items\":[%s]}",
                        HM_ME, HM_FEED);
        return 0;
    }
    if (!strcmp(method, "social.post")) {
        HM_POSTS++;
        zxvd_buf_printf(out, "{\"id\":\"%s\",\"lamport\":\"20\",\"kind\":\"%s\"}", HM_P6,
                        strstr(HM_LAST, "reply_to") ? "reply" : "post");
        return 0;
    }
    if (!strcmp(method, "social.react")) {
        HM_REACTS++;
        zxvd_buf_printf(out, "{\"id\":\"%s\"}", HM_P6);
        return 0;
    }
    snprintf(msg, msg_cap, "not here");
    return -32601;
}

static hamlet_t HM;

APPS_TEST(hamlet)
{
    static const zxvd_people_ops_t people = {hm_people};
    const int64_t NOW = 1760000000;
    HMG[0] = (hm_group_t){HM_G1, "garden", "member", 5};
    HMG[1] = (hm_group_t){HM_G2, "porch", "none", 3};
    HMG[2] = (hm_group_t){HM_G3, "kitchen", "owner", 2};

    hamlet_init(&HM, &LINK);
    CHECK(HM.ngroup == 0 && HM.ceiling == 0 && hamlet_unread(&HM, 0) == HAMLET_REFUSED &&
              hamlet_mark_read(&HM, 0, NOW) == ZXM_E_ARG,
          "hamlet: no groups, no ceiling, nothing to read");

    /* without the people group the dispatcher answers -32601 and nothing is kept */
    CHECK(hamlet_groups(&HM) == ZXM_E_REMOTE && LINK.err_code == -32601 && HM.ngroup == 0,
          "hamlet: social.groups not served: %s", LINK.err);
    zxvd_rpc_set_people_ops(&people);

    /* HM1: the groups with the seat's role */
    CHECK(hamlet_groups(&HM) == ZXM_OK && HM.ngroup == 3 &&
              HM.group[0].role == HAMLET_ROLE_MEMBER && HM.group[1].role == HAMLET_ROLE_NONE &&
              HM.group[2].role == HAMLET_ROLE_OWNER && !strcmp(HM.group[1].name, "porch") &&
              HM.group[0].members == 5 && hamlet_find_group(&HM, HM_G3) == 2 &&
              hamlet_find_group(&HM, HM_ME) == -1,
          "hamlet: three groups, a member, none and the owner (%s)", LINK.err);

    /* HM2, HM3: the feed in zxvd's order, each item shown by HR2 */
    HM_FEED = HM_ITEM(HM_P1, HM_BO, "false", "post", "null", HM_Q(HM_G1), "7", "") "," HM_ITEM(
        HM_P2, HM_ME, "true", "post", "null", "null", "8",
        "") "," HM_ITEM(HM_P3, HM_CY, "false", "post", "null", HM_Q(HM_G2), "9",
                        "") "," HM_ITEM(HM_P4, HM_CY, "false", "reply", HM_Q(HM_P1), HM_Q(HM_G1),
                                        "10",
                                        "{\"reaction\":\"+1\","
                                        "\"count\":2,\"mine\":"
                                        "true}") "," HM_ITEM(HM_P5, HM_BO, "false", "post", "null",
                                                             "null", "11", "");
    CHECK(hamlet_feed(&HM, 0) == ZXM_E_ARG && hamlet_feed(&HM, 51) == ZXM_E_ARG,
          "hamlet: a limit outside 1..50 is not sent");
    CHECK(hamlet_feed(&HM, 20) == ZXM_OK && HM.nfeed == 5 && !strcmp(HM.me, HM_ME) &&
              HM.feed[0].lamport == 7 && HM.feed[4].lamport == 11 && HM.feed[1].mine &&
              HM.feed[3].kind == HAMLET_REPLY && !strcmp(HM.feed[3].parent, HM_P1) &&
              HM.feed[3].nreact == 1 && HM.feed[3].react[0].count == 2 &&
              strstr(HM_LAST, "\"limit\":20") != NULL,
          "hamlet: the feed as zxvd ranks it (%s)", LINK.err);
    CHECK(hamlet_shown(&HM, 0) && hamlet_shown(&HM, 1) && !hamlet_shown(&HM, 2) &&
              hamlet_shown(&HM, 3) && hamlet_shown(&HM, 4) && !hamlet_shown(&HM, 9) &&
              HM.nshown == 4,
          "hamlet: the porch's post is not shown to a seat outside the porch (HR2)");

    /* HM4: unread per group through Commons's rule */
    CHECK(hamlet_unread(&HM, 0) == 2 && hamlet_unread(&HM, 1) == 0 && hamlet_unread(&HM, 2) == 0,
          "hamlet: two unread in the garden, none in the porch");
    CHECK(hamlet_notice(&HM) == ZXM_OK && !strcmp(HM.notice, "2 new posts in garden"),
          "hamlet: the notice: %s", HM.notice);
    CHECK(hamlet_feed(&HM, 20) == ZXM_OK && hamlet_unread(&HM, 0) == 2,
          "hamlet: the same feed again counts nothing twice");

    /* HM4: mutes, an author's and a group's */
    CHECK(hamlet_mute_author(&HM, HM_BO, true) == ZXM_OK && !hamlet_shown(&HM, 0) &&
              hamlet_shown(&HM, 3) && !hamlet_shown(&HM, 4) && HM.nshown == 2 &&
              hamlet_mute_author(&HM, "bo", true) == ZXM_E_ARG,
          "hamlet: Bo muted: his posts are not shown");
    CHECK(hamlet_mute_author(&HM, HM_BO, false) == ZXM_OK && HM.nmuted == 0 && HM.nshown == 4,
          "hamlet: Bo unmuted");
    CHECK(hamlet_mute_group(&HM, 0, true) == ZXM_OK && !hamlet_shown(&HM, 0) &&
              !hamlet_shown(&HM, 3) && hamlet_shown(&HM, 4) && hamlet_notice(&HM) == ZXM_OK &&
              !HM.notice[0] && hamlet_mute_group(&HM, 3, true) == ZXM_E_ARG,
          "hamlet: the garden muted: its posts and its notice are not shown");
    CHECK(hamlet_mute_group(&HM, 0, false) == ZXM_OK && HM.nshown == 4, "hamlet: garden unmuted");

    /* HM4: the read marker (HR3) */
    CHECK(hamlet_mark_read(&HM, 0, -1) == ZXM_E_ARG && hamlet_mark_read(&HM, 0, NOW) == ZXM_OK &&
              hamlet_unread(&HM, 0) == 0 && HM.group[0].mark == 10 && HM.group[0].stamp == NOW,
          "hamlet: the garden read to Lamport 10");
    CHECK(hamlet_feed(&HM, 20) == ZXM_OK && hamlet_unread(&HM, 0) == 0,
          "hamlet: the same feed adds nothing past the marker");
    CHECK(hamlet_groups(&HM) == ZXM_OK && HM.group[0].mark == 10 && hamlet_unread(&HM, 0) == 0,
          "hamlet: a refresh of the groups keeps the marker");
    const char *before = HM_FEED;
    HM_FEED = HM_ITEM(HM_P6, HM_CY, "false", "post", "null", HM_Q(HM_G1), "12", "");
    CHECK(hamlet_feed(&HM, 20) == ZXM_OK && hamlet_unread(&HM, 0) == 1 && HM.nfeed == 1,
          "hamlet: one new post past the marker");
    HM_FEED =
        HM_ITEM(HM_P6, HM_CY, "false", "post", "null", HM_Q(HM_G1), "1000000000000000000", "");
    CHECK(hamlet_feed(&HM, 20) == ZXM_E_PROTOCOL && HM.nfeed == 1 && hamlet_unread(&HM, 0) == 1,
          "hamlet: a Lamport time past 18 digits refused, the page before kept");
    HM_FEED = HM_ITEM(HM_P6, HM_ME, "false", "post", "null", "null", "13", "");
    CHECK(hamlet_feed(&HM, 20) == ZXM_E_PROTOCOL && HM.nfeed == 1,
          "hamlet: an item of the owner's not marked mine is refused");
    HM_FEED = before;

    /* HM1: join the porch, and its post is shown; the owner never leaves */
    CHECK(hamlet_join(&HM, HM_G2) == ZXM_OK && HM.group[1].role == HAMLET_ROLE_MEMBER &&
              HM.group[1].members == 4 && hamlet_feed(&HM, 20) == ZXM_OK && hamlet_shown(&HM, 2),
          "hamlet: in the porch, its post is shown (%s)", LINK.err);
    CHECK(hamlet_join(&HM, HM_G2) == ZXM_E_STATE && hamlet_leave(&HM, HM_G3) == ZXM_E_STATE &&
              hamlet_join(&HM, HM_ME) == ZXM_E_STATE && hamlet_join(&HM, "1") == ZXM_E_ARG,
          "hamlet: no second join, the owner stays, no unknown group");

    /* posting and reacting, within zxvd's limits */
    static char big[COMMONS_TEXT_MAX + 2];
    memset(big, 'a', COMMONS_TEXT_MAX + 1u);
    big[COMMONS_TEXT_MAX + 1u] = 0;
    uint32_t posts = HM_POSTS;
    CHECK(hamlet_post(&HM, 0, "   ", NULL) == ZXM_E_ARG &&
              hamlet_post(&HM, 0, big, NULL) == ZXM_E_ARG &&
              hamlet_post(&HM, 3, "hi", NULL) == ZXM_E_ARG &&
              hamlet_post(&HM, 0, "hi", "zz") == ZXM_E_ARG && HM_POSTS == posts,
          "hamlet: blank, 1025 bytes, no such group or a bad reply: not sent");
    big[COMMONS_TEXT_MAX] = 0;
    CHECK(hamlet_post(&HM, 0, big, NULL) == ZXM_OK && strstr(HM_LAST, "\"group\":\"" HM_G1 "\"") &&
              hamlet_post(&HM, -1, "to all", NULL) == ZXM_OK && !strstr(HM_LAST, "group") &&
              hamlet_post(&HM, 0, "thanks", HM_P1) == ZXM_OK &&
              strstr(HM_LAST, "\"reply_to\":\"" HM_P1 "\"") && HM_POSTS == posts + 3,
          "hamlet: 1024 bytes to the garden, one to all, a reply (%s)", LINK.err);
    CHECK(hamlet_leave(&HM, HM_G2) == ZXM_OK && hamlet_post(&HM, 1, "hi", NULL) == ZXM_E_STATE &&
              HM_POSTS == posts + 3,
          "hamlet: out of the porch, nothing posted there");
    CHECK(hamlet_react(&HM, HM_P1, "+1") == ZXM_OK && HM_REACTS == 1 &&
              hamlet_react(&HM, HM_P1, "") == ZXM_E_ARG &&
              hamlet_react(&HM, HM_P1, "123456789012345678901234567890123") == ZXM_E_ARG &&
              hamlet_react(&HM, "x", "+1") == ZXM_E_ARG && HM_REACTS == 1,
          "hamlet: a reaction of 1..32 bytes to a held post");
    zxvd_rpc_set_people_ops(NULL);

    /* HM5: a gift is the owner's act, within the owner's ceiling */
    HM.link = &FAKE;
    uint32_t seq = FAKE.seq;
    CHECK(hamlet_gift(&HM, HM_TO, 2500, "for the seeds", NOW, true) == ZXM_E_ARG &&
              hamlet_set_ceiling(&HM, 10000, false) == ZXM_E_STATE && HM.ceiling == 0 &&
              hamlet_set_ceiling(&HM, -1, true) == ZXM_E_ARG &&
              hamlet_set_ceiling(&HM, HAMLET_MINOR_MAX + 1, true) == ZXM_E_ARG &&
              hamlet_set_ceiling(&HM, 10000, true) == ZXM_OK && HM.ceiling == 10000,
          "hamlet: no gift before the owner sets a ceiling, and only the owner sets it");
    CHECK(hamlet_gift(&HM, HM_TO, 2500, "for the seeds", NOW, false) == ZXM_E_STATE &&
              hamlet_gift(&HM, HM_TO, 0, "for the seeds", NOW, true) == ZXM_E_ARG &&
              hamlet_gift(&HM, HM_TO, 10001, "for the seeds", NOW, true) == ZXM_E_ARG &&
              hamlet_gift(&HM, "dd", 2500, "for the seeds", NOW, true) == ZXM_E_ARG &&
              hamlet_gift(&HM, HM_TO, 2500, "for the seeds", -2, true) == ZXM_E_ARG &&
              FAKE.seq == seq && HM.gift.status == HAMLET_GIFT_NONE,
          "hamlet: unconfirmed, nothing, past the ceiling, a short id, a bad clock: not sent");
    CANNED = HM_OK HM_ENTRY(7, "signed", "2500", "1760000000") "}";
    CHECK(hamlet_gift(&HM, HM_TO, 2500, "for the seeds", NOW, true) == ZXM_E_PROTOCOL &&
              HM.gift.status == HAMLET_GIFT_NONE,
          "hamlet: money signed at once is not zxvd's answer: refused");
    CANNED = HM_OK HM_ENTRY(7, "held", "2500", "null") "}";
    CHECK(hamlet_gift(&HM, HM_TO, 2500, "for the seeds", NOW, true) == ZXM_OK &&
              HM.gift.status == HAMLET_GIFT_HELD && HM.gift.id == 7 && HM.gift.amount == 2500 &&
              strstr(FAKE.req, "\"web4.post\"") && strstr(FAKE.req, "\"kind\":\"REQUEST\"") &&
              strstr(FAKE.req, "\"to\":\"" HM_TO "\"") && strstr(FAKE.req, "\"tool\":\"gift\"") &&
              strstr(FAKE.req, "\"amount_minor\":2500") && strstr(FAKE.req, "\"now\":1760000000"),
          "hamlet: 25.00 posted, held for consent: %s", FAKE.req);
    seq = FAKE.seq;
    CHECK(hamlet_gift(&HM, HM_TO, 100, "more", NOW, true) == ZXM_E_STATE &&
              hamlet_gift_answer(&HM, true, 600, NOW, false) == ZXM_E_STATE &&
              hamlet_gift_answer(&HM, true, 0, NOW, true) == ZXM_E_ARG &&
              hamlet_gift_answer(&HM, true, HAMLET_GRANT_MAX + 1, NOW, true) == ZXM_E_ARG &&
              FAKE.seq == seq,
          "hamlet: one held gift at a time; the answer is the owner's, for 1..86400 s");
    /* RR1: a grant past the ceiling is never asked for */
    CHECK(hamlet_set_ceiling(&HM, 1000, true) == ZXM_OK &&
              hamlet_gift_answer(&HM, true, 600, NOW, true) == ZXM_E_STATE && FAKE.seq == seq &&
              hamlet_set_ceiling(&HM, 10000, true) == ZXM_OK,
          "hamlet: a ceiling lowered under the held gift: no grant asked");
    CANNED = HM_OK
        "{\"envelope\":" HM_ENTRY(7, "signed", "2500", "1760000000") ",\"consent\":{\"id\":8}}}";
    CHECK(hamlet_gift_answer(&HM, true, 600, NOW, true) == ZXM_OK &&
              HM.gift.status == HAMLET_GIFT_SIGNED && strstr(FAKE.req, "\"web4.consent\"") &&
              strstr(FAKE.req, "\"id\":7") && strstr(FAKE.req, "\"grant\":true") &&
              strstr(FAKE.req, "\"expires_in\":600"),
          "hamlet: the owner's grant signs the gift: %s", FAKE.req);
    CHECK(hamlet_gift_answer(&HM, false, 600, NOW, true) == ZXM_E_STATE,
          "hamlet: a signed gift has nothing left to answer");

    /* a second gift, denied */
    CANNED = HM_OK HM_ENTRY(9, "held", "100", "null") "}";
    CHECK(hamlet_gift(&HM, HM_TO, 100, "", -1, true) == ZXM_OK && HM.gift.id == 9 &&
              !strstr(FAKE.req, "\"now\""),
          "hamlet: one minor unit up, on zxvd's clock: %s", FAKE.req);
    CANNED =
        HM_OK "{\"envelope\":" HM_ENTRY(8, "denied", "100", "null") ",\"consent\":{\"id\":10}}}";
    CHECK(hamlet_gift_answer(&HM, false, 600, NOW, true) == ZXM_E_PROTOCOL &&
              HM.gift.status == HAMLET_GIFT_HELD,
          "hamlet: an answer for another envelope is refused");
    CANNED =
        HM_OK "{\"envelope\":" HM_ENTRY(9, "denied", "100", "null") ",\"consent\":{\"id\":10}}}";
    CHECK(hamlet_gift_answer(&HM, false, 600, NOW, true) == ZXM_OK &&
              HM.gift.status == HAMLET_GIFT_DENIED && strstr(FAKE.req, "\"grant\":false"),
          "hamlet: the owner's deny drops the gift");
    CANNED = NULL;
    HM.link = &LINK;
}
