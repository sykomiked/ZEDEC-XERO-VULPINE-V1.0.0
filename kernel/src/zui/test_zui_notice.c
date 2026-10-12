/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_notice.c — the notification center (zui_notice.h NC1..NC10). */
#include <stdio.h>
#include <string.h>

#include "zui_notice.h"
#include "zui_tokens.h"
#include "../font/ttf_fixture.inc" /* MINI_TTF[] */

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL (%s:%d) ", __FILE__, __LINE__);                                         \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static zui_notice_center_t C;

static const zui_notice_action_t REPLY[] = {{"Reply", "chat:reply:7"}, {"Mute", "chat:mute:7"}};

static uint32_t post_at(const char *app, const char *thread, const char *title, zui_npri_t pri,
                        uint64_t t)
{
    zui_notice_spec_t sp = {app, thread, title, "body", pri, t, REPLY, 2, 0};
    return zui_notice_post(&C, &sp, t);
}

static uint32_t post(const char *app, const char *title, zui_npri_t pri, uint64_t t)
{
    return post_at(app, "", title, pri, t);
}

static const zui_notice_t *N(uint32_t id)
{
    return zui_notice_get(&C, id);
}

static void test_post(void)
{
    zui_notice_init(&C);
    CHECK(zui_notice_count(&C) == 0 && C.cap == ZUI_NOTICE_MAX && zui_notice_banner(&C) == 0,
          "empty");
    zui_notice_action_t four[4] = {{"A", "a"}, {"B", "b"}, {"C", "c"}, {"D", "d"}};
    zui_notice_spec_t sp = {"mail", "inbox", "New mail", "From Ana", ZUI_NPRI_NORMAL,
                            100,    four,    4,          0};
    uint32_t a = zui_notice_post(&C, &sp, 100);
    const zui_notice_t *n = N(a);
    CHECK(a != 0 && n, "NC2: posted, id %u", a);
    CHECK(n && strcmp(n->app, "mail") == 0 && strcmp(n->thread, "inbox") == 0 &&
              strcmp(n->title, "New mail") == 0 && strcmp(n->body, "From Ana") == 0 &&
              n->pri == ZUI_NPRI_NORMAL && n->time_ms == 100 && !n->read && !n->ai &&
              n->held == ZUI_HELD_NONE,
          "NC1: the fields");
    CHECK(n && n->nact == ZUI_NOTICE_ACTIONS && strcmp(n->act[2].label, "C") == 0 &&
              strcmp(n->act[2].action, "c") == 0,
          "up to three actions");
    uint32_t b = post("mail", "x", (zui_npri_t) 9, 101);
    CHECK(b > a && N(b)->pri == ZUI_NPRI_URGENT, "ids grow; a priority past urgent is urgent");
    zui_notice_spec_t bare = {"app", 0, 0, 0, ZUI_NPRI_LOW, 1, 0, 3, 0};
    uint32_t c = zui_notice_post(&C, &bare, 1);
    CHECK(c && N(c)->title[0] == 0 && N(c)->thread[0] == 0 && N(c)->nact == 0,
          "NULL strings are empty; no action table, no actions");
    zui_notice_spec_t noapp = {"", 0, "t", 0, ZUI_NPRI_LOW, 1, 0, 0, 0};
    CHECK(zui_notice_post(&C, &noapp, 1) == 0, "an app id is needed");
    noapp.app = 0;
    CHECK(zui_notice_post(&C, &noapp, 1) == 0 && zui_notice_post(&C, 0, 1) == 0 &&
              zui_notice_post(0, &sp, 1) == 0,
          "NULL");
    /* cut on a UTF-8 boundary */
    char lng[300];
    memset(lng, 'x', sizeof lng);
    memcpy(lng + ZUI_NOTICE_TITLE_MAX - 2u, "\xE2\x82\xAC", 3);
    lng[sizeof lng - 1] = 0;
    zui_notice_spec_t cut = {"app", 0, lng, lng, ZUI_NPRI_LOW, 1, 0, 0, 0};
    uint32_t d = zui_notice_post(&C, &cut, 1);
    CHECK(strlen(N(d)->title) == ZUI_NOTICE_TITLE_MAX - 2u, "a title is cut before a split char");
    CHECK(strlen(N(d)->body) == ZUI_NOTICE_BODY_MAX - 1u, "a body is cut to fit");
    CHECK(zui_notice_count(&C) == 4, "four");
    CHECK(zui_notice_get(&C, 0) == 0 && zui_notice_get(&C, 999) == 0 && zui_notice_get(0, a) == 0,
          "no such notice");

    /* update in place */
    zui_notice_spec_t up = {
        "other", "other", "Two new mails", "From Ana and Bo", ZUI_NPRI_HIGH, 200, 0, 0, 0};
    zui_notice_mark_read(&C, a, true);
    CHECK(zui_notice_update(&C, a, &up, false, 200), "NC2: update");
    n = N(a);
    CHECK(strcmp(n->title, "Two new mails") == 0 && n->nact == 0 && n->pri == ZUI_NPRI_HIGH &&
              n->time_ms == 200 && n->read,
          "text, actions, priority and time replaced; still read");
    CHECK(strcmp(n->app, "mail") == 0 && strcmp(n->thread, "inbox") == 0,
          "the app and the thread stay");
    CHECK(zui_notice_update(&C, a, &up, true, 210) && !N(a)->read, "alert: unread again");
    CHECK(!zui_notice_update(&C, 999, &up, false, 1) && !zui_notice_update(&C, a, 0, false, 1),
          "update of nothing");

    /* withdraw */
    CHECK(zui_notice_withdraw(&C, b, 300) && N(b) == 0 && zui_notice_count(&C) == 3, "withdraw");
    CHECK(!zui_notice_withdraw(&C, b, 300) && !zui_notice_withdraw(0, a, 1), "only once");
}

static void test_groups(void)
{
    zui_notice_init(&C);
    uint32_t m1 = post_at("mail", "ana", "m1", ZUI_NPRI_NORMAL, 100);
    uint32_t c1 = post_at("chat", "bo", "c1", ZUI_NPRI_NORMAL, 110);
    uint32_t m2 = post_at("mail", "cy", "m2", ZUI_NPRI_NORMAL, 120);
    uint32_t c2 = post_at("chat", "bo", "c2", ZUI_NPRI_NORMAL, 130);
    uint32_t m3 = post_at("mail", "ana", "m3", ZUI_NPRI_NORMAL, 140);
    uint32_t u1 = post_at("updates", "", "u1", ZUI_NPRI_LOW, 90);
    zui_notice_group_t g[8];
    uint32_t n = zui_notice_apps(&C, g, 8);
    CHECK(n == 3, "three apps");
    CHECK(n == 3 && strcmp(g[0].app, "mail") == 0 && g[0].count == 3 && g[0].unread == 3 &&
              g[0].newest == m3 && g[0].newest_ms == 140 && g[0].thread == 0,
          "NC3: mail first (newest at 140)");
    CHECK(n == 3 && strcmp(g[1].app, "chat") == 0 && g[1].count == 2 && g[1].newest == c2,
          "then chat");
    CHECK(n == 3 && strcmp(g[2].app, "updates") == 0 && g[2].newest == u1, "then updates");
    CHECK(zui_notice_apps(&C, g, 1) == 3 && strcmp(g[0].app, "mail") == 0, "cap");
    n = zui_notice_threads(&C, "mail", g, 8);
    CHECK(n == 2 && strcmp(g[0].thread, "ana") == 0 && g[0].count == 2 &&
              strcmp(g[1].thread, "cy") == 0,
          "mail's threads, newest first");
    CHECK(zui_notice_threads(&C, "nope", g, 8) == 0 && zui_notice_threads(&C, 0, g, 8) == 0,
          "no such app");
    uint32_t ids[8];
    n = zui_notice_list(&C, 0, 0, ids, 8);
    CHECK(n == 6 && ids[0] == m3 && ids[1] == c2 && ids[2] == m2 && ids[3] == c1 && ids[4] == m1 &&
              ids[5] == u1,
          "everything, newest first");
    n = zui_notice_list(&C, "mail", 0, ids, 8);
    CHECK(n == 3 && ids[0] == m3 && ids[1] == m2 && ids[2] == m1, "one app");
    n = zui_notice_list(&C, "mail", "ana", ids, 8);
    CHECK(n == 2 && ids[0] == m3 && ids[1] == m1, "one thread");
    CHECK(zui_notice_list(&C, "mail", "ana", ids, 1) == 2 && ids[0] == m3, "cap: the count");
    CHECK(zui_notice_list(&C, 0, 0, 0, 0) == 6, "count only");
    /* ties in time: the later post is newer */
    uint32_t t1 = post_at("tie", "", "first", ZUI_NPRI_LOW, 500);
    uint32_t t2 = post_at("tie", "", "second", ZUI_NPRI_LOW, 500);
    CHECK(zui_notice_list(&C, "tie", 0, ids, 8) == 2 && ids[0] == t2 && ids[1] == t1,
          "same time: the later post first");
    /* an update moves a notice by its new time */
    zui_notice_spec_t up = {0, 0, "m1 again", 0, ZUI_NPRI_NORMAL, 600, 0, 0, 0};
    zui_notice_update(&C, m1, &up, false, 600);
    zui_notice_apps(&C, g, 8);
    CHECK(strcmp(g[0].app, "mail") == 0 && g[0].newest == m1, "updated: mail's newest is m1");
    zui_notice_threads(&C, "mail", g, 8);
    CHECK(strcmp(g[0].thread, "ana") == 0, "and ana's thread leads");

    /* NC4: read */
    CHECK(zui_notice_unread(&C, 0, 0) == 8 && zui_notice_unread(&C, "mail", 0) == 3 &&
              zui_notice_unread(&C, "mail", "ana") == 2,
          "unread counts");
    CHECK(zui_notice_mark_read(&C, m3, true) && N(m3)->read, "mark read");
    CHECK(zui_notice_unread(&C, "mail", "ana") == 1, "one fewer");
    CHECK(zui_notice_mark_group_read(&C, "mail", "ana") == 1 &&
              zui_notice_unread(&C, "mail", 0) == 1,
          "a thread read");
    CHECK(zui_notice_mark_group_read(&C, "chat", 0) == 2 && zui_notice_unread(&C, "chat", 0) == 0,
          "an app read");
    zui_notice_apps(&C, g, 8);
    CHECK(g[0].unread == 1, "group unread follows");
    CHECK(zui_notice_mark_read(&C, m3, false) && !N(m3)->read, "unread again");
    CHECK(zui_notice_mark_group_read(&C, 0, 0) == 5 && zui_notice_unread(&C, 0, 0) == 0,
          "everything read");
    CHECK(!zui_notice_mark_read(&C, 999, true), "no such notice");

    /* withdraw a thread */
    CHECK(zui_notice_withdraw_thread(&C, "mail", "ana", 700) == 2 && zui_notice_count(&C) == 6 &&
              N(m2),
          "a thread withdrawn; another thread of the app stays");
    CHECK(zui_notice_withdraw_thread(&C, "updates", 0, 700) == 1, "NULL thread is \"\"");
    CHECK(zui_notice_withdraw_thread(&C, 0, "x", 700) == 0, "no app");
    (void) c1;
}

static void test_banners(void)
{
    zui_notice_init(&C);
    uint32_t a = post("mail", "a", ZUI_NPRI_NORMAL, 1000);
    CHECK(zui_notice_banner(&C) == a && N(a)->banner == ZUI_BANNER_SHOWING, "NC5: a banner");
    uint32_t b = post("mail", "b", ZUI_NPRI_NORMAL, 1100);
    uint32_t c = post("chat", "c", ZUI_NPRI_HIGH, 1200);
    CHECK(zui_notice_banner(&C) == a && zui_notice_waiting(&C) == 2, "one at a time");
    zui_notice_tick(&C, 1000 + ZUI_NOTICE_BANNER_MS - 1);
    CHECK(zui_notice_banner(&C) == a, "until its time is up");
    zui_notice_tick(&C, 1000 + ZUI_NOTICE_BANNER_MS);
    CHECK(zui_notice_banner(&C) == c && N(a)->banner == ZUI_BANNER_NONE,
          "then the next: the higher priority first");
    CHECK(!N(a)->read, "a banner that timed out stays unread in the panel");
    zui_notice_banner_dismiss(&C, 6100);
    CHECK(zui_notice_banner(&C) == b && N(c)->banner == ZUI_BANNER_NONE && !N(c)->read,
          "dismissed: the next one, the dismissed stays unread");
    zui_notice_tick(&C, 6100 + ZUI_NOTICE_BANNER_MS);
    CHECK(zui_notice_banner(&C) == 0 && zui_notice_waiting(&C) == 0, "all shown");

    /* an urgent one takes the place of a normal one, and stays */
    uint32_t d = post("mail", "d", ZUI_NPRI_NORMAL, 20000);
    uint32_t u = post("phone", "ringing", ZUI_NPRI_URGENT, 20100);
    CHECK(zui_notice_banner(&C) == u && N(d)->banner == ZUI_BANNER_WAITING,
          "urgent pre-empts; the normal one waits again");
    zui_notice_tick(&C, 20100 + 10u * ZUI_NOTICE_BANNER_MS);
    CHECK(zui_notice_banner(&C) == u, "an urgent banner stays");
    uint32_t u2 = post("phone", "second call", ZUI_NPRI_URGENT, 80000);
    CHECK(zui_notice_banner(&C) == u && N(u2)->banner == ZUI_BANNER_WAITING,
          "an urgent one does not push out another urgent one");
    const char *act;
    CHECK(zui_notice_act(&C, u, 0, &act, 80001) == ZUI_NOTICE_OK &&
              strcmp(act, "chat:reply:7") == 0,
          "acting on it");
    CHECK(N(u)->read && zui_notice_banner(&C) == u2, "read, and the next urgent shows");
    zui_notice_banner_dismiss(&C, 80002);
    CHECK(zui_notice_banner(&C) == d, "then the waiting normal one");
    CHECK(C.banner_since == 80002, "its time starts when it shows");

    /* reading a waiting notice takes it out of the queue */
    uint32_t e = post("mail", "e", ZUI_NPRI_NORMAL, 80003);
    CHECK(N(e)->banner == ZUI_BANNER_WAITING, "e waits");
    zui_notice_mark_read(&C, e, true);
    CHECK(N(e)->banner == ZUI_BANNER_NONE && zui_notice_waiting(&C) == 0, "read: no banner");
    /* withdrawing the banner on screen shows the next */
    uint32_t f = post("mail", "f", ZUI_NPRI_NORMAL, 80004);
    zui_notice_withdraw(&C, d, 80005);
    CHECK(zui_notice_banner(&C) == f, "withdrawn: the next one");
    /* an update with alert shows it afresh; without, nothing */
    zui_notice_tick(&C, 80005 + ZUI_NOTICE_BANNER_MS);
    CHECK(zui_notice_banner(&C) == 0, "quiet");
    zui_notice_spec_t up = {0, 0, "f2", 0, ZUI_NPRI_NORMAL, 90000, 0, 0, 0};
    zui_notice_update(&C, f, &up, false, 90000);
    CHECK(zui_notice_banner(&C) == 0, "a quiet update does not banner");
    zui_notice_update(&C, f, &up, true, 90001);
    CHECK(zui_notice_banner(&C) == f && C.banner_since == 90001, "an alerting one does");
    zui_notice_update(&C, f, &up, true, 92000);
    CHECK(zui_notice_banner(&C) == f && C.banner_since == 92000, "again: its time restarts");

    /* NC5: the queue is bounded */
    zui_notice_init(&C);
    uint32_t first = post("q", "showing", ZUI_NPRI_NORMAL, 1);
    uint32_t w[ZUI_NOTICE_BANNERS];
    for (uint32_t i = 0; i < ZUI_NOTICE_BANNERS; i++)
        w[i] = post("q", "w", i == 0 ? ZUI_NPRI_LOW : ZUI_NPRI_NORMAL, 2 + i);
    CHECK(zui_notice_banner(&C) == first && zui_notice_waiting(&C) == ZUI_NOTICE_BANNERS, "full");
    uint32_t hi = post("q", "high", ZUI_NPRI_HIGH, 100);
    CHECK(N(hi)->banner == ZUI_BANNER_WAITING && N(w[0])->banner == ZUI_BANNER_NONE &&
              C.banner_dropped == 1,
          "the low one lost its banner to the high one");
    CHECK(N(w[0]) && !N(w[0])->read, "but is still in the panel, unread");
    uint32_t lo = post("q", "low", ZUI_NPRI_LOW, 101);
    CHECK(N(lo)->banner == ZUI_BANNER_NONE && C.banner_dropped == 2 &&
              zui_notice_waiting(&C) == ZUI_NOTICE_BANNERS,
          "a low newcomer gets no banner when everything waiting is more urgent");
    uint32_t nm = post("q", "normal", ZUI_NPRI_NORMAL, 102);
    CHECK(N(nm)->banner == ZUI_BANNER_WAITING && N(w[1])->banner == ZUI_BANNER_NONE,
          "an equal newcomer replaces the oldest of its priority");
    zui_notice_tick(&C, 1 + ZUI_NOTICE_BANNER_MS);
    CHECK(zui_notice_banner(&C) == hi, "the high one shows next");
    zui_notice_tick(&C, 0);
    CHECK(zui_notice_banner(&C) == hi, "a clock that steps back changes nothing");
}

/* a stand-in for zx_notify's bus: the fields its gate reads */
typedef struct {
    bool dnd;
    uint8_t dnd_breakthrough;
    int asked;
} bus_t;

static zui_gate_t bus_gate(void *ctx, const zui_notice_t *n)
{
    bus_t *b = ctx;
    b->asked++;
    return zui_notice_decide(b->dnd, (zui_npri_t) b->dnd_breakthrough, false, false,
                             (zui_npri_t) n->pri);
}

static void test_dnd(void)
{
    /* NC6: the decision, as zx_notify N6 makes it */
    for (int p = 0; p < 4; p++) {
        CHECK(zui_notice_decide(false, ZUI_NPRI_URGENT, false, false, (zui_npri_t) p) ==
                  ZUI_GATE_SHOW,
              "off: everything shows (%d)", p);
        CHECK(zui_notice_decide(true, ZUI_NPRI_HIGH, false, false, (zui_npri_t) p) ==
                  (p >= ZUI_NPRI_HIGH ? ZUI_GATE_SHOW : ZUI_GATE_HOLD),
              "on: at least the breakthrough shows, else held (%d)", p);
        CHECK(zui_notice_decide(true, ZUI_NPRI_URGENT, true, false, (zui_npri_t) p) ==
                  ZUI_GATE_SHOW,
              "on: an allowed app shows (%d)", p);
        CHECK(zui_notice_decide(false, ZUI_NPRI_URGENT, true, true, (zui_npri_t) p) ==
                      ZUI_GATE_SILENT &&
                  zui_notice_decide(true, ZUI_NPRI_LOW, false, true, (zui_npri_t) p) ==
                      ZUI_GATE_SILENT,
              "muted: never, whatever else (%d)", p);
    }

    zui_notice_init(&C);
    CHECK(zui_notice_set_dnd(&C, true, ZUI_NPRI_URGENT, 0) == 0 && C.dnd, "on");
    uint32_t a = post("mail", "a", ZUI_NPRI_NORMAL, 10);
    uint32_t b = post("mail", "b", ZUI_NPRI_HIGH, 20);
    CHECK(zui_notice_banner(&C) == 0 && N(a)->banner == ZUI_BANNER_HELD &&
              N(b)->banner == ZUI_BANNER_HELD,
          "held while on");
    CHECK(zui_notice_unread(&C, 0, 0) == 2, "but kept, unread");
    uint32_t u = post("phone", "call", ZUI_NPRI_URGENT, 30);
    CHECK(zui_notice_banner(&C) == u, "urgent breaks through");
    CHECK(zui_notice_allow(&C, "family", true), "allow an app");
    uint32_t fam = post("family", "dinner", ZUI_NPRI_LOW, 40);
    CHECK(N(fam)->banner == ZUI_BANNER_WAITING, "an allowed app breaks through");
    CHECK(zui_notice_mute(&C, "ads", true), "mute an app");
    uint32_t ad = post("ads", "sale", ZUI_NPRI_URGENT, 50);
    CHECK(N(ad)->banner == ZUI_BANNER_NONE && N(ad) && !N(ad)->read, "muted: kept, never shown");
    zui_notice_mark_read(&C, b, true);
    CHECK(zui_notice_set_dnd(&C, false, ZUI_NPRI_URGENT, 60) == 1, "off: one held released");
    CHECK(N(a)->banner == ZUI_BANNER_WAITING && N(b)->banner == ZUI_BANNER_NONE,
          "the read one is not released");
    CHECK(N(ad)->banner == ZUI_BANNER_NONE, "muted stays silent");
    zui_notice_act(&C, u, -1, &(const char *){0}, 61);
    CHECK(zui_notice_banner(&C) == fam || zui_notice_banner(&C) == a, "the next shows");
    /* turning it on again holds what waits */
    zui_notice_init(&C);
    a = post("mail", "a", ZUI_NPRI_NORMAL, 10);
    b = post("mail", "b", ZUI_NPRI_NORMAL, 11);
    zui_notice_set_dnd(&C, true, ZUI_NPRI_HIGH, 12);
    CHECK(zui_notice_banner(&C) == a && N(b)->banner == ZUI_BANNER_HELD,
          "what is on screen stays, what waits is held");
    uint32_t h = post("mail", "high", ZUI_NPRI_HIGH, 13);
    CHECK(N(h)->banner == ZUI_BANNER_WAITING, "breakthrough set to high");
    /* muting an app takes its banner down */
    zui_notice_mute(&C, "mail", true);
    CHECK(zui_notice_banner(&C) == 0 && N(h)->banner == ZUI_BANNER_NONE &&
              N(b)->banner == ZUI_BANNER_NONE,
          "muted: off the screen and out of the queue");
    CHECK(zui_notice_mute(&C, "mail", false) && C.nmuted == 0, "unmute");
    CHECK(zui_notice_allow(&C, "x", true) && zui_notice_allow(&C, "x", true) && C.nallow == 1,
          "allowed once");
    CHECK(zui_notice_allow(&C, "x", false) && C.nallow == 0 && zui_notice_allow(&C, "x", false),
          "removed; removing again is fine");
    for (uint32_t i = 0; i < ZUI_NOTICE_LIST; i++) {
        char id[8];
        snprintf(id, sizeof id, "a%u", i);
        zui_notice_allow(&C, id, true);
    }
    CHECK(!zui_notice_allow(&C, "one-more", true) && !zui_notice_allow(&C, "", true) &&
              !zui_notice_allow(0, "x", true),
          "a full list, an empty id, NULL");

    /* NC6: a gate that asks the bus */
    static bus_t bus = {true, ZUI_NPRI_URGENT, 0};
    zui_notice_init(&C);
    zui_notice_set_gate(&C, bus_gate, &bus);
    a = post("mail", "a", ZUI_NPRI_HIGH, 10);
    CHECK(bus.asked == 1 && N(a)->banner == ZUI_BANNER_HELD, "the bus says hold");
    u = post("phone", "u", ZUI_NPRI_URGENT, 11);
    CHECK(zui_notice_banner(&C) == u, "and lets urgent through");
    bus.dnd = false;
    CHECK(zui_notice_regate(&C, 12) == 1 && N(a)->banner == ZUI_BANNER_WAITING,
          "regate after the bus changed releases it");
    zui_notice_set_gate(&C, 0, 0);
    CHECK(C.gate == 0, "back to the center's own");
}

static void test_ai(void)
{
    zui_notice_init(&C);
    zui_notice_set_dnd(&C, true, ZUI_NPRI_HIGH, 0);
    zui_notice_action_t send[] = {{"Send reply", "mail:send:3"}};
    zui_notice_spec_t sp = {"mail",          "ana", "Draft ready", "I wrote a reply to Ana",
                            ZUI_NPRI_URGENT, 10,    send,          1,
                            "companion"};
    uint32_t a = zui_notice_post(&C, &sp, 10);
    const zui_notice_t *n = N(a);
    CHECK(n->ai && strcmp(n->agent, "companion") == 0 && n->held == ZUI_HELD_PENDING,
          "NC7: marked and held");
    CHECK(n->pri == ZUI_NPRI_NORMAL, "its priority is at most normal");
    CHECK(n->banner == ZUI_BANNER_HELD, "so it never breaks through do-not-disturb");
    const char *act = "x";
    CHECK(zui_notice_act(&C, a, 0, &act, 11) == ZUI_NOTICE_E_HELD && act == 0 && !N(a)->read,
          "NC8: its action is refused until the person accepts");
    CHECK(zui_notice_act(&C, a, -1, &act, 12) == ZUI_NOTICE_OK && strcmp(act, "mail") == 0,
          "but it can be opened");
    CHECK(zui_notice_accept(&C, a) && N(a)->held == ZUI_HELD_ACCEPTED, "accepted");
    CHECK(!zui_notice_accept(&C, a) && !zui_notice_reject(&C, a), "decided once");
    CHECK(zui_notice_act(&C, a, 0, &act, 13) == ZUI_NOTICE_OK && strcmp(act, "mail:send:3") == 0,
          "then its action is handed back, for the app to run");
    /* new AI words wait again */
    zui_notice_spec_t up = {0, 0, "Draft changed", 0, ZUI_NPRI_LOW, 20, send, 1, 0};
    zui_notice_update(&C, a, &up, false, 20);
    CHECK(N(a)->ai && N(a)->held == ZUI_HELD_PENDING, "an update of an AI notice is held again");
    CHECK(zui_notice_reject(&C, a) && N(a)->held == ZUI_HELD_REJECTED && N(a)->read, "rejected");
    CHECK(zui_notice_act(&C, a, 0, &act, 21) == ZUI_NOTICE_E_REJECTED, "its actions stay refused");
    /* a person's notice updated by an AI becomes AI */
    zui_notice_spec_t mine = {"notes", 0, "Meeting", 0, ZUI_NPRI_HIGH, 30, send, 1, 0};
    uint32_t m = zui_notice_post(&C, &mine, 30);
    CHECK(!N(m)->ai && N(m)->held == ZUI_HELD_NONE, "not AI");
    CHECK(!zui_notice_accept(&C, m) && !zui_notice_reject(&C, m), "nothing to accept");
    zui_notice_spec_t ai_up = {0, 0, "Meeting moved", 0, ZUI_NPRI_HIGH, 31, send, 1, "planner"};
    zui_notice_update(&C, m, &ai_up, false, 31);
    CHECK(N(m)->ai && N(m)->held == ZUI_HELD_PENDING && strcmp(N(m)->agent, "planner") == 0 &&
              N(m)->pri == ZUI_NPRI_NORMAL,
          "an AI update makes it held, at normal");
    /* act's other refusals */
    CHECK(zui_notice_act(&C, a, 1, &act, 40) == ZUI_NOTICE_E_NONE &&
              zui_notice_act(&C, a, -2, &act, 40) == ZUI_NOTICE_E_NONE &&
              zui_notice_act(&C, 999, 0, &act, 40) == ZUI_NOTICE_E_NONE &&
              zui_notice_act(&C, a, 0, 0, 40) == ZUI_NOTICE_E_ARG,
          "no such action, notice, or nowhere to put it");
}

static void test_history(void)
{
    zui_notice_init(&C);
    zui_notice_set_cap(&C, 5);
    uint32_t id[7];
    for (uint32_t i = 0; i < 7; i++) id[i] = post("app", "n", ZUI_NPRI_NORMAL, 1000 - i);
    CHECK(zui_notice_count(&C) == 5 && C.evicted == 2, "NC9: capped at 5, two evicted");
    CHECK(!N(id[0]) && !N(id[1]) && N(id[2]) && N(id[6]),
          "the oldest posted went, though their times were later");
    CHECK(zui_notice_banner(&C) == id[2], "the evicted banner went too; the next shows");
    zui_notice_set_cap(&C, 2);
    CHECK(zui_notice_count(&C) == 2 && N(id[5]) && N(id[6]) && C.evicted == 5, "a smaller cap");
    zui_notice_set_cap(&C, 1000);
    CHECK(C.cap == ZUI_NOTICE_MAX, "never above ZUI_NOTICE_MAX");
    for (uint32_t i = 0; i < ZUI_NOTICE_MAX + 4u; i++) post("many", "n", ZUI_NPRI_LOW, i);
    CHECK(zui_notice_count(&C) == ZUI_NOTICE_MAX, "full pool, still posting");
    zui_notice_set_cap(&C, 0);
    CHECK(zui_notice_count(&C) == 0 && post("x", "y", ZUI_NPRI_LOW, 1) == 0, "cap 0 keeps nothing");
    zui_notice_set_cap(0, 3);
}

/* ---- NC10: drawing ------------------------------------------------------------ */

#define W 480
#define H 720
static uint32_t PX[W * H], PX2[W * H];

static uint32_t count_rgb(const uint32_t *px, uint32_t rgb)
{
    uint32_t k = 0;
    for (int i = 0; i < W * H; i++) k += px[i] == rgb;
    return k;
}

/* every gold pixel lies inside rectangle r */
static bool gold_only_in(const uint32_t *px, zui_rect_t r)
{
    for (int32_t y = 0; y < H; y++)
        for (int32_t x = 0; x < W; x++)
            if (px[y * W + x] == ZUI_GOLD &&
                !(x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h))
                return false;
    return true;
}

static bool find_hit(int32_t action, bool banner, uint32_t id, zui_rect_t *out)
{
    for (uint32_t i = 0; i < C.nhit; i++)
        if (C.hit[i].action == action && C.hit[i].banner == banner && C.hit[i].id == id) {
            *out = C.hit[i].r;
            return true;
        }
    return false;
}

static void test_draw(void)
{
    zui_surface_t s;
    zui_surface_init(&s, PX, W, H, W);
    zui_rect_t all = {0, 0, W, H};
    zui_notice_init(&C);
    int32_t need = zui_notice_draw_center(&C, &s, all, 0);
    CHECK(need > 0 && PX[0] == ZUI_VOID && PX[W * H - 1] == ZUI_VOID, "empty: the ground");
    CHECK(count_rgb(PX, ZUI_GOLD) == 0, "nothing gold when there is nothing to do");

    uint32_t a = post_at("mail", "ana", "a", ZUI_NPRI_NORMAL, 100);
    uint32_t b = post_at("chat", "bo", "b", ZUI_NPRI_NORMAL, 200);
    zui_notice_draw_center(&C, &s, all, 0);
    zui_rect_t r;
    CHECK(find_hit(0, false, b, &r), "the newest notice's first action was laid out");
    CHECK(PX[(r.y + r.h / 2) * W + r.x + r.w / 2] == ZUI_GOLD,
          "NC10: the selected (newest) notice's first action is gold");
    CHECK(gold_only_in(PX, r), "and nothing else is gold");
    zui_rect_t r2;
    CHECK(find_hit(1, false, b, &r2) && PX[(r2.y + r2.h / 2) * W + r2.x + r2.w / 2] == ZUI_RAISED,
          "its second action is carved");
    CHECK(find_hit(0, false, a, &r2) && PX[(r2.y + r2.h / 2) * W + r2.x + r2.w / 2] == ZUI_RAISED,
          "another notice's first action is carved");
    zui_rect_t card;
    CHECK(find_hit(ZUI_NOTICE_HIT_CARD, false, a, &card), "a card laid out");
    CHECK(PX[(card.y + card.h / 2) * W + card.x + card.w - 20] == ZUI_PANEL, "a carved panel");
    CHECK(card.y > r.y, "chat (newer) is above mail");
    /* NC10: hit testing */
    int32_t act;
    bool ban;
    CHECK(zui_notice_hit(&C, r.x + 2, r.y + 2, &act, &ban) == b && act == 0 && !ban,
          "a button is hit before its card");
    CHECK(zui_notice_hit(&C, card.x + card.w - 5, card.y + 5, &act, &ban) == a &&
              act == ZUI_NOTICE_HIT_CARD,
          "the card itself");
    CHECK(zui_notice_hit(&C, 1, 1, &act, &ban) == 0, "the ground is nothing");
    /* selecting another notice moves the gold */
    C.sel = a;
    zui_notice_draw_center(&C, &s, all, 0);
    CHECK(find_hit(0, false, a, &r) && gold_only_in(PX, r) &&
              PX[(r.y + r.h / 2) * W + r.x + r.w / 2] == ZUI_GOLD,
          "the gold follows the selection");
    /* deterministic */
    zui_surface_t s2;
    zui_surface_init(&s2, PX2, W, H, W);
    zui_notice_draw_center(&C, &s2, all, 0);
    CHECK(memcmp(PX, PX2, sizeof PX) == 0, "the same pixels twice");

    /* an AI notice: Accept is the primary action while it is held */
    zui_notice_spec_t sp = {"mail", "ana", "Draft", "AI draft", ZUI_NPRI_NORMAL,
                            300,    REPLY, 1,       "companion"};
    uint32_t ai = zui_notice_post(&C, &sp, 300);
    C.sel = ai;
    zui_notice_draw_center(&C, &s, all, 0);
    zui_rect_t acc, rej;
    CHECK(find_hit(ZUI_NOTICE_HIT_ACCEPT, false, ai, &acc) &&
              find_hit(ZUI_NOTICE_HIT_REJECT, false, ai, &rej),
          "held: Accept and Reject, not its actions");
    CHECK(!find_hit(0, false, ai, &r), "its own action is not offered yet");
    CHECK(PX[(acc.y + acc.h / 2) * W + acc.x + acc.w / 2] == ZUI_GOLD && gold_only_in(PX, acc),
          "Accept is the gold one");
    CHECK(zui_notice_hit(&C, rej.x + 1, rej.y + 1, &act, &ban) == ai &&
              act == ZUI_NOTICE_HIT_REJECT,
          "Reject is hit");
    CHECK(find_hit(ZUI_NOTICE_HIT_CARD, false, ai, &card), "its card");
    uint32_t dash = 0, plain = 0;
    zui_rect_t other;
    CHECK(find_hit(ZUI_NOTICE_HIT_CARD, false, a, &other), "a person's card");
    for (int32_t x = 0; x < card.w; x++) {
        dash += PX[(card.y + 3) * W + card.x + x] == ZUI_TEXT_2;
        plain += PX[(other.y + 3) * W + other.x + x] == ZUI_TEXT_2;
    }
    CHECK(dash > 20 && plain == 0, "a dashed edge marks the AI's card (%u, %u)", dash, plain);
    zui_notice_accept(&C, ai);
    zui_notice_draw_center(&C, &s, all, 0);
    CHECK(find_hit(0, false, ai, &r) && !find_hit(ZUI_NOTICE_HIT_ACCEPT, false, ai, &acc),
          "accepted: its action is offered");

    /* "+n more" and scrolling */
    for (int i = 0; i < 6; i++) post_at("feed", "t", "f", ZUI_NPRI_LOW, 1000 + (uint64_t) i);
    need = zui_notice_draw_center(&C, &s, all, 0);
    uint32_t cards = 0;
    for (uint32_t i = 0; i < C.nhit; i++)
        if (C.hit[i].action == ZUI_NOTICE_HIT_CARD && strcmp(N(C.hit[i].id)->app, "feed") == 0)
            cards++;
    CHECK(cards == ZUI_NOTICE_SHOWN, "a thread shows %u, then a line (%u)", ZUI_NOTICE_SHOWN,
          cards);
    CHECK(need > H, "more than fits: the content is %d tall", need);
    C.scroll = need - H;
    zui_notice_draw_center(&C, &s, all, 0);
    bool inside = true;
    for (uint32_t i = 0; i < C.nhit; i++) {
        zui_rect_t h = C.hit[i].r;
        inside = inside && h.y >= 0 && h.y + h.h <= H;
    }
    CHECK(inside, "hits are clipped to the panel");
    C.scroll = 0;

    /* the banner */
    zui_notice_tick(&C, 1000000);
    zui_notice_init(&C);
    CHECK(!zui_notice_draw_banner(&C, &s, all, 0), "no banner, nothing drawn");
    uint32_t bn = post("chat", "hello", ZUI_NPRI_NORMAL, 10);
    zui_fill(&s, all, ZUI_VOID);
    zui_rect_t br = {20, 20, 400, 200};
    CHECK(zui_notice_draw_banner(&C, &s, br, 0), "the banner drawn");
    CHECK(find_hit(0, true, bn, &r) && PX[(r.y + r.h / 2) * W + r.x + r.w / 2] == ZUI_GOLD &&
              gold_only_in(PX, r),
          "its first action is the gold one");
    CHECK(zui_notice_hit(&C, r.x + 1, r.y + 1, &act, &ban) == bn && act == 0 && ban,
          "and the banner is hit");
    CHECK(PX[(br.y + br.h - 2) * W + br.x + 50] == ZUI_VOID, "it is only as tall as it needs");
    /* banner hits come before the panel's under it */
    zui_notice_draw_center(&C, &s, all, 0);
    zui_notice_draw_banner(&C, &s, br, 0);
    CHECK(zui_notice_hit(&C, r.x + 1, r.y + 1, &act, &ban) == bn && ban, "banner first");
    zui_notice_banner_dismiss(&C, 11);
    CHECK(!zui_notice_draw_banner(&C, &s, br, 0), "dismissed: none");
    for (uint32_t i = 0; i < C.nhit; i++) CHECK(!C.hit[i].banner, "its hits are gone");

    /* the caller's clip is kept */
    zui_fill(&s, all, 0x123456u);
    zui_clip(&s, (zui_rect_t){0, 0, 100, 100});
    zui_notice_draw_center(&C, &s, all, 0);
    CHECK(s.clip.w == 100 && s.clip.h == 100, "clip kept");
    CHECK(PX[300 * W + 300] == 0x123456u && PX[50 * W + 50] != 0x123456u,
          "drawn inside the caller's clip only");
    zui_unclip(&s);
    CHECK(zui_notice_draw_center(0, &s, all, 0) == 0 &&
              zui_notice_draw_center(&C, 0, all, 0) == 0 &&
              !zui_notice_draw_banner(0, &s, all, 0) && zui_notice_hit(0, 0, 0, &act, &ban) == 0,
          "NULL");
}

static void test_draw_text(void)
{
    static ttf_font_t font;
    static zui_glyph_cache_t cache;
    CHECK(ttf_parse(&font, MINI_TTF, sizeof MINI_TTF), "fixture font");
    zui_glyph_cache_init(&cache);
    zui_text_t t = {&font, &cache, 14, false};
    zui_surface_t s;
    zui_surface_init(&s, PX, W, H, W);
    zui_rect_t all = {0, 0, W, H};
    zui_notice_init(&C);
    zui_notice_spec_t sp = {"mail", "ana", "Hello", "World",    ZUI_NPRI_NORMAL,
                            1,      REPLY, 2,       "companion"};
    zui_notice_post(&C, &sp, 1);
    post("chat", "Hi", ZUI_NPRI_LOW, 2);
    int32_t need = zui_notice_draw_center(&C, &s, all, &t);
    CHECK(need > 0, "drawn with a font");
    zui_surface_t s2;
    zui_surface_init(&s2, PX2, W, H, W);
    zui_notice_draw_center(&C, &s2, all, 0);
    CHECK(memcmp(PX, PX2, sizeof PX) != 0, "text drawn: the pixels differ from no text");
    zui_notice_draw_center(&C, &s2, all, &t);
    CHECK(memcmp(PX, PX2, sizeof PX) == 0, "and the same text twice is the same");
    CHECK(zui_notice_draw_banner(&C, &s, all, &t), "a banner with text");
}

int main(void)
{
    test_post();
    test_groups();
    test_banners();
    test_dnd();
    test_ai();
    test_history();
    test_draw();
    test_draw_text();
    printf("%s test_zui_notice: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
