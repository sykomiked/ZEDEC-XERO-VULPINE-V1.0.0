/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_lock.c — the lock screen and idle auto-lock (zui_lock.h L1..L9). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zui_lock.h"
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

#define W 800
#define H 720

static uint32_t PX[W * H], PX2[W * H];
static zui_surface_t S;
static zui_lock_t L;

static zui_event_t ev(uint8_t type, uint32_t ms)
{
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.ms = ms;
    return e;
}

static bool key(uint16_t k, uint8_t mods, uint32_t ms)
{
    zui_event_t e = ev(ZUI_EV_KEY_DOWN, ms);
    e.key = k;
    e.mods = mods;
    return zui_lock_event(&L, &e);
}

static bool text(const char *s, uint32_t ms)
{
    zui_event_t e = ev(ZUI_EV_TEXT, ms);
    snprintf(e.text, sizeof e.text, "%s", s);
    return zui_lock_event(&L, &e);
}

static bool ptr(uint8_t type, int32_t x, int32_t y, uint32_t ms)
{
    zui_event_t e = ev(type, ms);
    e.x = x;
    e.y = y;
    e.button = ZUI_BTN_LEFT;
    return zui_lock_event(&L, &e);
}

/* ---- the auth callback: checks the secret and copies it for the test ---- */

static uint32_t auth_calls, auth_account, auth_n;
static uint8_t auth_seen[ZUI_LOCK_SECRET_MAX];
static int auth_answer = -1; /* -1: compare with "fox and hound" */
static const uint8_t *auth_ptr;

static int auth(void *ctx, uint32_t account, const uint8_t *secret, uint32_t n)
{
    (void) ctx;
    auth_calls++;
    auth_account = account;
    auth_n = n;
    auth_ptr = secret;
    memcpy(auth_seen, secret, n);
    if (auth_answer >= 0) return auth_answer;
    return n == 13 && !memcmp(secret, "fox and hound", 13) ? ZUI_AUTH_OK : ZUI_AUTH_FAIL;
}

static bool secret_wiped(void)
{
    for (uint32_t i = 0; i < ZUI_LOCK_SECRET_MAX; i++)
        if (L.secret[i]) return false;
    return L.nsecret == 0;
}

static int transitions, last_from = -1, last_to = -1;
static void on_state(void *ctx, zui_lock_state_t from, zui_lock_state_t to)
{
    (void) ctx;
    transitions++;
    last_from = (int) from;
    last_to = (int) to;
}

static int emergencies;
static void on_emergency(void *ctx)
{
    (void) ctx;
    emergencies++;
}

static int marks;
static zui_rect_t mark_box;
static uint32_t mark_lit;
static void mark(zui_surface_t *s, zui_rect_t box, uint32_t lit, void *ctx)
{
    (void) ctx;
    marks++;
    mark_box = box;
    mark_lit = lit;
    zui_fill(s, (zui_rect_t){box.x + box.w / 4, box.y + box.h / 4, box.w / 2, box.h / 2}, ZUI_GOLD);
}

static void setup(const zui_lock_config_t *cfg, uint32_t now)
{
    zui_lock_init(&L, cfg, now);
    zui_lock_account(&L, 7, "Ada");
    L.auth = auth;
    L.on_state = on_state;
    L.emergency = on_emergency;
    L.mark = mark;
    zui_lock_set_area(&L, (zui_rect_t){0, 0, W, H});
    transitions = 0;
    auth_calls = 0;
    auth_answer = -1;
}

static zui_lock_config_t short_cfg(void)
{
    zui_lock_config_t c;
    zui_lock_defaults(&c);
    c.idle_ms = 60000;
    c.dim_ms = 10000;
    c.grace_ms = 5000;
    return c;
}

static void test_idle(void)
{
    zui_lock_config_t d;
    zui_lock_defaults(&d);
    CHECK(d.idle_ms == 300000 && d.dim_ms == 10000 && d.grace_ms == 5000 && d.free_tries == 3 &&
              d.backoff_ms == 30000 && d.backoff_max_ms == 900000 && !d.previews,
          "defaults");
    zui_lock_init(&L, 0, 0);
    CHECK(L.cfg.idle_ms == 300000 && L.state == ZUI_LOCK_UNLOCKED, "NULL config: defaults");

    zui_lock_config_t c = short_cfg();
    setup(&c, 1000);
    zui_lock_tick(&L, 1000 + 49999);
    CHECK(L.state == ZUI_LOCK_UNLOCKED && zui_lock_dim(&L) == 0, "not yet dimming");
    zui_lock_tick(&L, 1000 + 50000);
    CHECK(L.state == ZUI_LOCK_DIMMING && last_from == ZUI_LOCK_UNLOCKED &&
              last_to == ZUI_LOCK_DIMMING,
          "L2: dims for the last 10 s");
    CHECK(zui_lock_dim(&L) == 0, "dim starts at 0");
    zui_lock_tick(&L, 1000 + 55000);
    CHECK(zui_lock_dim(&L) == 128, "half way: %u", zui_lock_dim(&L));
    /* input wakes it, and the event passes on to the apps */
    CHECK(!ptr(ZUI_EV_PTR_MOVE, 5, 5, 1000 + 56000), "the waking event is not eaten");
    CHECK(L.state == ZUI_LOCK_UNLOCKED && zui_lock_dim(&L) == 0, "L1: input while dimming wakes");
    zui_lock_tick(&L, 1000 + 56000 + 59999);
    CHECK(L.state == ZUI_LOCK_DIMMING, "the timer restarted at the input");
    zui_lock_tick(&L, 1000 + 56000 + 60000);
    CHECK(L.state == ZUI_LOCK_LOCKED && L.by_idle && zui_lock_is_up(&L), "L2: idle locks");
    CHECK(zui_lock_dim(&L) == 256, "locked is fully dim");

    /* what counts as input */
    setup(&c, 0);
    static const uint8_t NOT_INPUT[] = {ZUI_EV_TICK, ZUI_EV_RESIZE, ZUI_EV_FOCUS_IN,
                                        ZUI_EV_FOCUS_OUT, ZUI_EV_SCALE};
    for (uint32_t i = 0; i < sizeof NOT_INPUT; i++) {
        zui_event_t e = ev(NOT_INPUT[i], 10000 * (i + 1));
        CHECK(!zui_lock_event(&L, &e), "not eaten while unlocked (%u)", i);
    }
    CHECK(L.last_input == 0, "ticks, resize and focus are not input");
    static const uint8_t INPUT[] = {ZUI_EV_KEY_DOWN,   ZUI_EV_KEY_UP,    ZUI_EV_TEXT,
                                    ZUI_EV_PREEDIT,    ZUI_EV_PTR_DOWN,  ZUI_EV_PTR_MOVE,
                                    ZUI_EV_PTR_UP,     ZUI_EV_SCROLL,    ZUI_EV_TOUCH_BEGIN,
                                    ZUI_EV_TOUCH_MOVE, ZUI_EV_TOUCH_END, ZUI_EV_TOUCH_CANCEL};
    uint32_t ok = 0;
    for (uint32_t i = 0; i < sizeof INPUT; i++) {
        uint32_t t = 51000 + i * 100;
        zui_event_t e = ev(INPUT[i], t);
        zui_lock_event(&L, &e);
        ok += L.last_input == t;
    }
    CHECK(ok == sizeof INPUT, "every kind of input restarts the timer (%u)", ok);

    /* a tick event drives the timer too */
    zui_event_t te = ev(ZUI_EV_TICK, 51000 + 1100 + 60000);
    CHECK(zui_lock_event(&L, &te) && L.state == ZUI_LOCK_LOCKED, "a TICK event can lock");

    /* idle 0 never locks; dim longer than idle is clamped */
    c.idle_ms = 0;
    setup(&c, 0);
    zui_lock_tick(&L, 0xF0000000u);
    CHECK(L.state == ZUI_LOCK_UNLOCKED, "idle 0: never");
    c = short_cfg();
    c.dim_ms = 90000;
    setup(&c, 0);
    CHECK(L.cfg.dim_ms == 60000, "dim clamped to idle");
    zui_lock_tick(&L, 1);
    CHECK(L.state == ZUI_LOCK_DIMMING, "dims from the start");
    c.dim_ms = 0;
    setup(&c, 0);
    zui_lock_tick(&L, 59999);
    CHECK(L.state == ZUI_LOCK_UNLOCKED, "no dimming");
    zui_lock_tick(&L, 60000);
    CHECK(L.state == ZUI_LOCK_LOCKED, "straight to locked");

    /* the clock wraps */
    c = short_cfg();
    setup(&c, 0xFFFFF000u);
    zui_lock_tick(&L, 0xFFFFF000u + 30000u);
    CHECK(L.state == ZUI_LOCK_UNLOCKED, "across the wrap: still unlocked");
    zui_lock_tick(&L, 0xFFFFF000u + 60000u);
    CHECK(L.state == ZUI_LOCK_LOCKED, "across the wrap: locks on time");
}

static void test_lock_now_and_grace(void)
{
    zui_lock_config_t c = short_cfg();
    setup(&c, 0);
    CHECK(!key('l', ZUI_MOD_CMD, 100), "Cmd-L alone is not the lock key");
    CHECK(L.state == ZUI_LOCK_UNLOCKED, "still unlocked");
    CHECK(key('l', ZUI_MOD_CMD | ZUI_MOD_SHIFT | ZUI_MOD_META, 200), "L3: Cmd-Shift-L is eaten");
    CHECK(L.state == ZUI_LOCK_LOCKED && !L.by_idle, "and locks now");
    CHECK(ptr(ZUI_EV_PTR_MOVE, 1, 1, 300) && L.state == ZUI_LOCK_LOCKED,
          "L4: no grace after lock now");
    setup(&c, 0);
    CHECK(key('L', ZUI_MOD_CMD | ZUI_MOD_SHIFT, 10), "upper-case L works too");
    setup(&c, 0);
    zui_lock_now(&L);
    CHECK(L.state == ZUI_LOCK_LOCKED && transitions == 1, "zui_lock_now");
    text("abc", 100);
    zui_lock_now(&L);
    CHECK(L.state == ZUI_LOCK_LOCKED && secret_wiped() && transitions == 1,
          "lock now while locked wipes the typing, no new state");

    /* grace after an idle lock */
    setup(&c, 0);
    zui_lock_tick(&L, 60000);
    CHECK(L.state == ZUI_LOCK_LOCKED && L.by_idle, "idle lock");
    CHECK(ptr(ZUI_EV_PTR_MOVE, 3, 3, 64999), "the grace event is eaten");
    CHECK(L.state == ZUI_LOCK_UNLOCKED && auth_calls == 0, "L4: within grace, no secret needed");
    zui_lock_tick(&L, 64999 + 60000);
    CHECK(L.state == ZUI_LOCK_LOCKED, "locks again");
    CHECK(ptr(ZUI_EV_PTR_MOVE, 3, 3, 64999 + 65000), "eaten");
    CHECK(L.state == ZUI_LOCK_LOCKED, "L4: after grace it stays locked");
    c.grace_ms = 0;
    setup(&c, 0);
    zui_lock_tick(&L, 60000);
    ptr(ZUI_EV_PTR_MOVE, 3, 3, 60000);
    CHECK(L.state == ZUI_LOCK_LOCKED, "grace 0: none");
}

static void test_typing_and_auth(void)
{
    zui_lock_config_t c = short_cfg();
    setup(&c, 0);
    CHECK(!zui_lock_submit(&L), "nothing to submit while unlocked");
    zui_lock_now(&L);
    CHECK(!zui_lock_submit(&L) && auth_calls == 0, "an empty secret is not sent");
    CHECK(text("fox", 10) && text(" and", 20) && text(" hound", 30), "typing is eaten");
    CHECK(L.nsecret == 13 && zui_lock_typed_chars(&L) == 13, "13 bytes, 13 dots");
    CHECK(key(ZUI_KEY_ENTER, 0, 40), "Enter");
    CHECK(auth_calls == 1 && auth_account == 7 && auth_n == 13 &&
              !memcmp(auth_seen, "fox and hound", 13),
          "L6: the callback gets the account and the bytes");
    CHECK(auth_ptr == L.secret && secret_wiped(), "the very buffer is wiped after the call");
    CHECK(L.state == ZUI_LOCK_UNLOCKED && last_to == ZUI_LOCK_UNLOCKED, "unlocked");

    /* UTF-8, backspace, escape, overflow */
    zui_lock_now(&L);
    text("\xC3\xA9t\xC3\xA9", 50); /* été */
    CHECK(L.nsecret == 5 && zui_lock_typed_chars(&L) == 3, "a dot per character");
    key(ZUI_KEY_BACKSPACE, 0, 60);
    CHECK(L.nsecret == 3 && zui_lock_typed_chars(&L) == 2 && L.secret[3] == 0 && L.secret[4] == 0,
          "backspace drops a whole character and zeroes it");
    key(ZUI_KEY_BACKSPACE, 0, 61);
    key(ZUI_KEY_BACKSPACE, 0, 62);
    key(ZUI_KEY_BACKSPACE, 0, 63);
    CHECK(L.nsecret == 0, "backspace on empty is fine");
    text("secret", 70);
    key(ZUI_KEY_ESCAPE, 0, 80);
    CHECK(secret_wiped() && L.state == ZUI_LOCK_LOCKED, "Escape wipes");
    char chunk[ZUI_TEXT_EV_MAX + 1];
    memset(chunk, 'x', 32);
    chunk[32] = 0;
    for (int i = 0; i < 4; i++) text(chunk, 90);
    CHECK(L.nsecret == 128, "fills to the cap");
    text("y", 91);
    CHECK(L.nsecret == 128 && L.secret[127] == 'x', "past the cap: dropped");
    key(ZUI_KEY_ESCAPE, 0, 92);

    /* L6: typed and left: wiped after 30 s */
    text("half", 1000);
    zui_lock_tick(&L, 1000 + ZUI_LOCK_TYPED_MS - 1);
    CHECK(L.nsecret == 4, "kept a while");
    zui_lock_tick(&L, 1000 + ZUI_LOCK_TYPED_MS);
    CHECK(secret_wiped(), "wiped after ZUI_LOCK_TYPED_MS");

    /* no auth callback: nothing unlocks */
    L.auth = 0;
    text("fox and hound", 40000);
    key(ZUI_KEY_ENTER, 0, 40001);
    CHECK(L.state == ZUI_LOCK_FAILED && secret_wiped(), "without auth: failed, wiped");
    L.auth = auth;

    /* odd answers count as failure */
    setup(&c, 0);
    zui_lock_now(&L);
    auth_answer = 77;
    text("a", 1);
    key(ZUI_KEY_ENTER, 0, 2);
    CHECK(L.state == ZUI_LOCK_FAILED && L.fails == 1, "an unknown answer is a failure");

    /* pending */
    setup(&c, 0);
    zui_lock_now(&L);
    auth_answer = ZUI_AUTH_PENDING;
    text("fox", 1);
    key(ZUI_KEY_ENTER, 0, 2);
    CHECK(L.state == ZUI_LOCK_AUTHENTICATING && secret_wiped(), "pending: authenticating, wiped");
    CHECK(text("more", 3) && L.nsecret == 0, "typing while deciding is ignored");
    CHECK(key(ZUI_KEY_ENTER, 0, 4) && auth_calls == 1, "no second submit");
    zui_lock_auth_result(&L, false);
    CHECK(L.state == ZUI_LOCK_FAILED && L.fails == 1, "a late no");
    zui_lock_auth_result(&L, true);
    CHECK(L.state == ZUI_LOCK_FAILED, "a result when not deciding is ignored");
    text("x", 5); /* no wait: back to locked and typed */
    CHECK(L.state == ZUI_LOCK_LOCKED && L.nsecret == 1, "after a free failure, typing resumes");
    key(ZUI_KEY_ENTER, 0, 6);
    zui_lock_auth_result(&L, true);
    CHECK(L.state == ZUI_LOCK_UNLOCKED && L.fails == 0, "a late yes unlocks and clears");
}

static void test_backoff(void)
{
    zui_lock_config_t c = short_cfg();
    setup(&c, 0);
    zui_lock_tick(&L, 60000); /* idle lock: grace would apply ... */
    uint32_t t = 60000;
    text("x", t + 6000); /* ... but not after 5 s */
    CHECK(L.state == ZUI_LOCK_LOCKED && L.nsecret == 1, "typing after grace");
    key(ZUI_KEY_ESCAPE, 0, t + 6000);
    t += 6000;
    static const uint32_t WAIT[] = {0, 0, 0, 30000, 60000, 120000, 240000, 480000, 900000, 900000};
    for (uint32_t i = 0; i < 10; i++) {
        text("wrong", t);
        key(ZUI_KEY_ENTER, 0, t);
        CHECK(L.state == ZUI_LOCK_FAILED && L.fails == i + 1, "failure %u", i + 1);
        CHECK(zui_lock_wait_ms(&L) == WAIT[i], "L5: failure %u waits %u (want %u)", i + 1,
              zui_lock_wait_ms(&L), WAIT[i]);
        if (WAIT[i]) {
            CHECK(text("fox and hound", t + 1) && L.nsecret == 0, "typing ignored while waiting");
            key(ZUI_KEY_ENTER, 0, t + 2);
            CHECK(auth_calls == i + 1 && L.state == ZUI_LOCK_FAILED, "no attempt while waiting");
            zui_lock_tick(&L, t + WAIT[i] - 1);
            CHECK(L.state == ZUI_LOCK_FAILED && zui_lock_wait_ms(&L) == 1, "1 ms left");
            zui_lock_tick(&L, t + WAIT[i]);
            CHECK(L.state == ZUI_LOCK_LOCKED && zui_lock_wait_ms(&L) == 0, "then locked again");
        }
        t += WAIT[i] + 10;
    }
    /* the grace period never applies after a failure */
    CHECK(!L.by_idle, "no grace after failing");
    text("fox and hound", t);
    key(ZUI_KEY_ENTER, 0, t);
    CHECK(L.state == ZUI_LOCK_UNLOCKED && L.fails == 0, "a success clears the count");
    zui_lock_now(&L);
    text("nope", t + 10);
    key(ZUI_KEY_ENTER, 0, t + 10);
    CHECK(zui_lock_wait_ms(&L) == 0, "the count started over");
    CHECK(zui_lock_wait_ms(&L) == 0 && strcmp(zui_lock_state_name(L.state), "failed") == 0,
          "state name");
    CHECK(!strcmp(zui_lock_state_name(ZUI_LOCK_AUTHENTICATING), "authenticating") &&
              !strcmp(zui_lock_state_name((zui_lock_state_t) 9), "?"),
          "state names");
}

static void test_notices_and_emergency(void)
{
    zui_lock_config_t c = short_cfg();
    setup(&c, 0);
    zui_lock_notify(&L, "Murmuration", "ignored");
    CHECK(L.nnotice == 0, "L7: nothing collected while unlocked");
    zui_lock_now(&L);
    zui_lock_notify(&L, "Quillon", "Draft saved: plans.txt");
    zui_lock_notify(&L, "Glint", "Your balance changed");
    CHECK(L.nnotice == 2, "two notices");
    CHECK(!strcmp(zui_lock_notice_text(&L, 0), "Notification") &&
              !strcmp(zui_lock_notice_text(&L, 1), "Notification"),
          "L7: previews hidden by default");
    CHECK(!strcmp(L.notice[0].app, "Quillon"), "the app shows");
    L.cfg.previews = true;
    CHECK(!strcmp(zui_lock_notice_text(&L, 1), "Your balance changed"), "previews when allowed");
    CHECK(!strcmp(zui_lock_notice_text(&L, 5), ""), "past the end");
    for (int i = 0; i < 5; i++) zui_lock_notify(&L, "Spindle", i == 4 ? "last" : "more");
    CHECK(L.nnotice == ZUI_LOCK_NOTICES && !strcmp(L.notice[3].text, "last") &&
              !strcmp(L.notice[0].app, "Spindle"),
          "the oldest drop out");
    char lng[200];
    memset(lng, 'a', sizeof lng);
    lng[94] = (char) 0xC3; /* é straddles the cut */
    lng[95] = (char) 0xA9;
    lng[199] = 0;
    zui_lock_notify(&L, "A", lng);
    CHECK(strlen(L.notice[3].text) == 94, "cut before a split character (%zu)",
          strlen(L.notice[3].text));

    /* L8: emergency, by pointer, in any locked state */
    zui_lock_layout_t lay;
    zui_lock_layout(&L, L.area, &lay);
    int32_t ex = lay.emergency.x + lay.emergency.w / 2, ey = lay.emergency.y + lay.emergency.h / 2;
    CHECK(ptr(ZUI_EV_PTR_DOWN, ex, ey, 100) && emergencies == 1 && L.state == ZUI_LOCK_LOCKED,
          "Emergency calls the hook and unlocks nothing");
    CHECK(ptr(ZUI_EV_PTR_DOWN, ex - 200, ey, 101) && emergencies == 1, "elsewhere: nothing");
    for (int i = 0; i < 4; i++) {
        text("bad", 200);
        key(ZUI_KEY_ENTER, 0, 200);
    }
    CHECK(zui_lock_wait_ms(&L) > 0, "in a backoff");
    zui_event_t tb = ev(ZUI_EV_TOUCH_BEGIN, 300);
    tb.x = ex;
    tb.y = ey;
    CHECK(zui_lock_event(&L, &tb) && emergencies == 2, "a touch reaches it during a backoff");
    zui_lock_emergency(&L);
    CHECK(emergencies == 3 && L.emergencies == 3, "and the direct call");
    L.emergency = 0;
    zui_lock_emergency(&L);
    CHECK(L.emergencies == 4, "no hook: counted, nothing called");
    L.emergency = on_emergency;
    /* unlock clears the notices */
    setup(&c, 0);
    zui_lock_now(&L);
    zui_lock_notify(&L, "Burrow", "Copied");
    text("fox and hound", 10);
    key(ZUI_KEY_ENTER, 0, 10);
    CHECK(L.state == ZUI_LOCK_UNLOCKED && L.nnotice == 0, "L7: dropped on unlock");
    zui_lock_emergency(&L);
    CHECK(emergencies == 3, "no emergency entry while unlocked");
}

/* ---- dates ----------------------------------------------------------------- */

static bool leap(int64_t y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

/* walk years and months: slow and obviously right */
static void ref_civil(int64_t t, int32_t *y, int32_t *m, int32_t *d)
{
    int64_t days = t >= 0 ? t / 86400 : -((-t + 86399) / 86400);
    int64_t yr = 1970;
    while (days < 0) {
        yr--;
        days += leap(yr) ? 366 : 365;
    }
    while (days >= (leap(yr) ? 366 : 365)) {
        days -= leap(yr) ? 366 : 365;
        yr++;
    }
    static const int ML[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int mo = 0;
    for (;;) {
        int len = ML[mo] + (mo == 1 && leap(yr));
        if (days < len) break;
        days -= len;
        mo++;
    }
    *y = (int32_t) yr;
    *m = mo + 1;
    *d = (int32_t) days + 1;
}

static void test_dates(void)
{
    zui_civil_t c;
    zui_civil(0, 0, &c);
    CHECK(c.year == 1970 && c.month == 1 && c.day == 1 && c.wday == 4 && c.hour == 0, "epoch");
    zui_civil(-1, 0, &c);
    CHECK(c.year == 1969 && c.month == 12 && c.day == 31 && c.wday == 3 && c.hour == 23 &&
              c.min == 59 && c.sec == 59,
          "one second before");
    zui_civil(1791813909, 0, &c);
    CHECK(c.year == 2026 && c.month == 10 && c.day == 12 && c.wday == 1 && c.hour == 14 &&
              c.min == 5 && c.sec == 9,
          "today");
    zui_civil(1791813909, 330, &c);
    CHECK(c.hour == 19 && c.min == 35 && c.day == 12, "+05:30");
    zui_civil(1791813909, -15 * 60, &c);
    CHECK(c.day == 11 && c.hour == 23 && c.wday == 0, "-15:00 is yesterday, a Sunday");
    zui_civil(-2208988800LL, 0, &c);
    CHECK(c.year == 1900 && c.month == 1 && c.day == 1 && c.wday == 1, "1900-01-01 a Monday");
    zui_civil(4107542400LL - 1, 0, &c);
    CHECK(c.year == 2100 && c.month == 2 && c.day == 28, "2100 is not a leap year");
    zui_civil(951782400LL, 0, &c); /* 2000-02-29 */
    CHECK(c.year == 2000 && c.month == 2 && c.day == 29 && c.wday == 2, "2000-02-29 a Tuesday");
    uint32_t seed = 99, bad = 0;
    for (int i = 0; i < 3000; i++) {
        seed = seed * 1664525u + 1013904223u;
        int64_t t = ((int64_t) (seed >> 1) - (1LL << 30)) * 5 + (int64_t) (seed % 86400u);
        int32_t y, m, d;
        ref_civil(t, &y, &m, &d);
        zui_civil(t, 0, &c);
        int64_t days = t >= 0 ? t / 86400 : -((-t + 86399) / 86400);
        int32_t wd = (int32_t) (((days % 7) + 11) % 7);
        bad += c.year != y || c.month != m || c.day != d || c.wday != wd ||
               (int64_t) c.hour * 3600 + c.min * 60 + c.sec != t - days * 86400;
    }
    CHECK(bad == 0, "3000 random instants match the slow walk (%u bad)", bad);

    char b[64];
    CHECK(zui_lock_time_text(b, sizeof b, 1791813909, 0, true) == 5 && !strcmp(b, "14:05"),
          "24-hour: %s", b);
    zui_lock_time_text(b, sizeof b, 1791813909, 0, false);
    CHECK(!strcmp(b, "2:05 PM"), "12-hour: %s", b);
    zui_lock_time_text(b, sizeof b, 0, 0, false);
    CHECK(!strcmp(b, "12:00 AM"), "midnight: %s", b);
    zui_lock_time_text(b, sizeof b, 12 * 3600 + 30 * 60, 0, false);
    CHECK(!strcmp(b, "12:30 PM"), "noon: %s", b);
    zui_lock_time_text(b, sizeof b, 9 * 3600 + 7 * 60, 0, true);
    CHECK(!strcmp(b, "09:07"), "leading zeros: %s", b);
    zui_lock_date_text(b, sizeof b, 1791813909, 0);
    CHECK(!strcmp(b, "Monday 12 October 2026"), "date: %s", b);
    zui_lock_date_text(b, sizeof b, -2208988800LL, 0);
    CHECK(!strcmp(b, "Monday 1 January 1900"), "date: %s", b);
    zui_lock_date_text(b, 8, 1791813909, 0);
    CHECK(!strcmp(b, "Monday "), "cut to fit: '%s'", b);
}

/* ---- drawing ------------------------------------------------------------- */

static uint32_t at(int32_t x, int32_t y)
{
    return PX[y * W + x] & 0xFFFFFFu;
}

static bool any_in(zui_rect_t r, uint32_t rgb)
{
    for (int32_t y = r.y; y < r.y + r.h; y++)
        for (int32_t x = r.x; x < r.x + r.w; x++)
            if (x >= 0 && y >= 0 && x < W && y < H && at(x, y) == rgb) return true;
    return false;
}

static bool inside_r(zui_rect_t in, zui_rect_t out)
{
    return in.x >= out.x && in.y >= out.y && in.x + in.w <= out.x + out.w &&
           in.y + in.h <= out.y + out.h;
}

static void test_layout(void)
{
    static const zui_rect_t AREAS[] = {{0, 0, W, H}, {0, 0, 360, 720}, {100, 50, 1920, 1080}};
    for (uint32_t k = 0; k < 3; k++) {
        zui_lock_layout_t o;
        zui_lock_layout(&L, AREAS[k], &o);
        zui_rect_t a = AREAS[k];
        CHECK(inside_r(o.mark, a) && inside_r(o.time, a) && inside_r(o.date, a) &&
                  inside_r(o.field, a) && inside_r(o.emergency, a),
              "area %u: parts inside", k);
        CHECK(o.mark.y + o.mark.h <= o.time.y && o.time.y + o.time.h <= o.date.y &&
                  o.date.y + o.date.h <= o.account.y && o.account.y + o.account.h <= o.field.y &&
                  o.field.y + o.field.h <= o.message.y,
              "area %u: top to bottom", k);
        int32_t cx = a.x + a.w / 2;
        CHECK(o.mark.x + o.mark.w / 2 == cx && o.field.x + o.field.w / 2 == cx &&
                  o.emergency.x + o.emergency.w / 2 == cx,
              "area %u: centred", k);
        CHECK(o.emergency.y + o.emergency.h <= a.y + a.h &&
                  o.emergency.y > o.message.y + o.message.h,
              "area %u: emergency at the bottom", k);
        CHECK(o.mark.w >= 48 && o.mark.w <= 128 && o.mark.y >= a.y, "area %u: mark size", k);
    }
}

static void test_draw(void)
{
    zui_surface_init(&S, PX, W, H, W);
    zui_lock_config_t c = short_cfg();
    setup(&c, 0);
    zui_fill(&S, (zui_rect_t){0, 0, W, H}, 0x336699u);
    marks = 0;
    zui_lock_draw(&L, &S, (zui_rect_t){0, 0, W, H}, 1791813909, 0, 0);
    CHECK(at(0, 0) == 0x336699u && at(W / 2, H / 2) == 0x336699u && marks == 0,
          "unlocked: draws nothing");
    zui_lock_tick(&L, 55000);
    zui_lock_draw(&L, &S, (zui_rect_t){0, 0, W, H}, 1791813909, 0, 0);
    uint32_t p = at(10, 10);
    CHECK(p != 0x336699u && (p & 0xFF) < 0x99 && (p & 0xFF) > 0x20, "dimming darkens: %06x", p);

    zui_lock_now(&L);
    zui_lock_draw(&L, &S, (zui_rect_t){0, 0, W, H}, 1791813909, 0, 0);
    zui_lock_layout_t o;
    zui_lock_layout(&L, (zui_rect_t){0, 0, W, H}, &o);
    CHECK(at(0, 0) == ZUI_VOID && at(W - 1, H - 1) == ZUI_VOID, "L9: the #002911 ground");
    CHECK(marks == 1 && mark_lit == 256 && mark_box.x == o.mark.x && mark_box.w == o.mark.w,
          "the fox mark at the top, lit");
    CHECK(at(o.mark.x + o.mark.w / 2, o.mark.y + o.mark.h / 2) == ZUI_GOLD, "the mark drew");
    CHECK(at(o.field.x + o.field.w / 2, o.field.y + o.field.h / 2) == ZUI_RAISED, "a carved field");
    CHECK(any_in((zui_rect_t){o.field.x, o.field.y, o.field.w, 2}, ZUI_ENGRAVE_LO) ||
              !any_in(o.field, 0x336699u),
          "engraved edge");
    CHECK(any_in(o.field, ZUI_GOLD), "the gold caret at tick 0");
    zui_lock_draw(&L, &S, (zui_rect_t){0, 0, W, H}, 1791813909, 0, 500);
    CHECK(!any_in(o.field, ZUI_GOLD), "the caret blinks");
    CHECK(!any_in(o.message, ZUI_ALERT), "no failure shown");
    CHECK(any_in(o.emergency, ZUI_EDGE), "the Emergency button");

    /* dots: one per character */
    text("ab\xC3\xA9", 10);
    zui_lock_draw(&L, &S, (zui_rect_t){0, 0, W, H}, 1791813909, 0, 500);
    int32_t cy = o.field.y + o.field.h / 2, x0 = o.field.x + ZUI_S3 + 4;
    CHECK(at(x0, cy) == ZUI_TEXT && at(x0 + 14, cy) == ZUI_TEXT && at(x0 + 28, cy) == ZUI_TEXT,
          "three dots");
    CHECK(at(x0 + 42, cy) == ZUI_RAISED, "and no fourth");
    for (int i = 0; i < 40; i++) text("z", 11);
    zui_lock_draw(&L, &S, (zui_rect_t){0, 0, W, H}, 1791813909, 0, 500);
    CHECK(at(o.field.x + o.field.w - 4, cy) != ZUI_TEXT, "dots stop inside the field");
    key(ZUI_KEY_ESCAPE, 0, 12);

    /* failure: the alert edge */
    text("bad", 20);
    key(ZUI_KEY_ENTER, 0, 21);
    zui_lock_draw(&L, &S, (zui_rect_t){0, 0, W, H}, 1791813909, 0, 0);
    CHECK(any_in(o.field, ZUI_ALERT), "L9: a failure shows in the alert colour");
    CHECK(!any_in(o.field, ZUI_GOLD), "no caret while failed");

    /* authenticating: the ZXV bar under the field */
    setup(&c, 0);
    zui_lock_now(&L);
    auth_answer = ZUI_AUTH_PENDING;
    text("x", 1);
    key(ZUI_KEY_ENTER, 0, 2);
    zui_fill(&S, (zui_rect_t){0, 0, W, H}, 0);
    zui_lock_draw(&L, &S, (zui_rect_t){0, 0, W, H}, 1791813909, 0, 0);
    bool bar = false;
    for (int32_t x = o.message.x; x < o.message.x + o.message.w && !bar; x++)
        for (int32_t y = o.message.y; y < o.message.y + o.message.h && !bar; y++)
            bar = at(x, y) != ZUI_VOID;
    CHECK(bar, "the bar breathes while deciding");

    /* notices */
    setup(&c, 0);
    zui_lock_now(&L);
    zui_lock_notify(&L, "Quillon", "saved");
    zui_lock_notify(&L, "Glint", "paid");
    zui_lock_draw(&L, &S, (zui_rect_t){0, 0, W, H}, 1791813909, 0, 0);
    char sum[32];
    zui_lock_summary(&L, sum, sizeof sum);
    CHECK(!strcmp(sum, "2 notifications") &&
              at(o.notice[0].x + o.notice[0].w / 2, o.notice[0].y + o.notice[0].h / 2) ==
                  ZUI_PANEL &&
              at(o.notice[1].x + o.notice[1].w / 2, o.notice[1].y + o.notice[1].h / 2) == ZUI_VOID,
          "previews off: one card saying how many (L7)");
    L.cfg.previews = true;
    zui_fill(&S, (zui_rect_t){0, 0, W, H}, 0);
    zui_lock_draw(&L, &S, (zui_rect_t){0, 0, W, H}, 1791813909, 0, 0);
    CHECK(at(o.notice[0].x + o.notice[0].w / 2, o.notice[0].y + o.notice[0].h / 2) == ZUI_PANEL &&
              at(o.notice[1].x + o.notice[1].w / 2, o.notice[1].y + o.notice[1].h / 2) == ZUI_PANEL,
          "two carved notices");
    CHECK(at(o.notice[2].x + o.notice[2].w / 2, o.notice[2].y + o.notice[2].h / 2) == ZUI_VOID,
          "no third");

    /* the area: only it is drawn, and it is remembered for hits */
    zui_fill(&S, (zui_rect_t){0, 0, W, H}, 0x336699u);
    zui_rect_t half = {W / 2, 0, W / 2, H};
    zui_lock_draw(&L, &S, half, 0, 0, 0);
    CHECK(at(W / 2 - 1, 5) == 0x336699u && at(W / 2, 5) == ZUI_VOID, "clipped to its area");
    CHECK(L.area.x == W / 2, "the area is remembered");

    /* deterministic */
    zui_lock_draw(&L, &S, (zui_rect_t){0, 0, W, H}, 1791813909, 120, 77);
    memcpy(PX2, PX, sizeof PX);
    zui_lock_draw(&L, &S, (zui_rect_t){0, 0, W, H}, 1791813909, 120, 77);
    CHECK(!memcmp(PX, PX2, sizeof PX), "same inputs, same pixels");

    /* with a font: the time and the account are drawn */
    static ttf_font_t font;
    static zui_glyph_cache_t cache;
    CHECK(ttf_parse(&font, MINI_TTF, sizeof MINI_TTF), "fixture font");
    zui_glyph_cache_init(&cache);
    zui_text_t big = {&font, &cache, 64, true}, small = {&font, &cache, 16, false};
    L.big = &big;
    L.text = &small;
    zui_lock_draw(&L, &S, (zui_rect_t){0, 0, W, H}, 1791813909, 0, 77);
    CHECK(memcmp(PX, PX2, sizeof PX) != 0, "text changes the picture");
    uint32_t outside = 0;
    for (int32_t i = 0; i < W * H; i++) outside += (PX[i] & 0xFFFFFFu) == 0x336699u;
    CHECK(outside == 0, "the whole area is the lock's");
}

/* L11: a stand-in for the seats service; its own result codes are mapped by
   a thin adapter, as the real one on the seats branch will be */
enum { SEAT_OK = 10, SEAT_DENIED = 11, SEAT_LATER = 12 };
static int seat_calls;
static int seat_unlock_stub(uint32_t uid, const uint8_t *pw, uint32_t n)
{
    seat_calls++;
    if (uid != 7) return SEAT_DENIED;
    if (n == 4 && !memcmp(pw, "slow", 4)) return SEAT_LATER;
    return n == 5 && !memcmp(pw, "vixen", 5) ? SEAT_OK : SEAT_DENIED;
}
static int seat_adapter(void *ctx, uint32_t account, const uint8_t *secret, uint32_t n)
{
    int *tag = ctx;
    if (*tag != 42) return ZUI_AUTH_FAIL;
    switch (seat_unlock_stub(account, secret, n)) {
    case SEAT_OK:
        return ZUI_AUTH_OK;
    case SEAT_LATER:
        return ZUI_AUTH_PENDING;
    default:
        return ZUI_AUTH_FAIL;
    }
}

static void test_failure_hold_and_adapter(void)
{
    zui_lock_config_t c = short_cfg();
    setup(&c, 0);
    zui_lock_now(&L);
    /* L10: a free try goes straight back to LOCKED but the words stay */
    text("wrong", 100);
    key(ZUI_KEY_ENTER, 0, 100);
    CHECK(zui_lock_wait_ms(&L) == 0 && zui_lock_failure_shown(&L), "L10: failure shown");
    zui_lock_tick(&L, 101);
    CHECK(L.state == ZUI_LOCK_LOCKED, "L10: free try is locked again at once");
    CHECK(zui_lock_failure_shown(&L), "L10: 'Wrong password' still shown");
    zui_lock_tick(&L, 100 + ZUI_LOCK_SAY_MS - 1);
    CHECK(zui_lock_failure_shown(&L), "L10: shown for 2 s less 1 ms");
    zui_lock_tick(&L, 100 + ZUI_LOCK_SAY_MS);
    CHECK(!zui_lock_failure_shown(&L), "L10: gone after %u ms", ZUI_LOCK_SAY_MS);
    CHECK(ZUI_LOCK_SAY_MS >= 2000, "L10: at least 2 s");

    /* L11: the adapter */
    static int tag = 42;
    zui_lock_set_auth(&L, seat_adapter, &tag);
    CHECK(L.auth == seat_adapter && L.auth_ctx == &tag, "L11: adapter installed");
    seat_calls = 0;
    text("fox", 3000);
    key(ZUI_KEY_ENTER, 0, 3000);
    CHECK(seat_calls == 1 && L.state == ZUI_LOCK_FAILED, "L11: denied maps to FAIL");
    zui_lock_tick(&L, 3001);
    text("slow", 3002);
    key(ZUI_KEY_ENTER, 0, 3002);
    CHECK(L.state == ZUI_LOCK_AUTHENTICATING && secret_wiped(), "L11: later maps to PENDING");
    zui_lock_auth_result(&L, false);
    zui_lock_tick(&L, 3003 + 30000);
    CHECK(L.state == ZUI_LOCK_LOCKED, "L11: back to locked");
    text("vixen", 40000);
    key(ZUI_KEY_ENTER, 0, 40000);
    CHECK(seat_calls == 3 && L.state == ZUI_LOCK_UNLOCKED && !zui_lock_failure_shown(&L),
          "L11: OK unlocks and clears the words");
    zui_lock_set_auth(&L, NULL, NULL);
    zui_lock_now(&L);
    text("vixen", 41000);
    key(ZUI_KEY_ENTER, 0, 41000);
    CHECK(L.state == ZUI_LOCK_FAILED && seat_calls == 3, "L11: no adapter never unlocks");
}

int main(void)
{
    test_idle();
    test_lock_now_and_grace();
    test_typing_and_auth();
    test_backoff();
    test_failure_hold_and_adapter();
    test_notices_and_emergency();
    test_dates();
    test_layout();
    test_draw();
    printf("%s test_zui_lock: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails, checks);
    return fails ? 1 : 0;
}
