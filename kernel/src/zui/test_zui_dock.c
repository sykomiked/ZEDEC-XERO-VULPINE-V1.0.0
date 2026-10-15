/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_dock.c — the astrolabe dock (zui_dock.h DK1..DK11). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zui_dock.h"
#include "zui_motion.h"
#include "zui_tokens.h"

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

#define W  1280
#define H  800
#define PW 390
#define PH 844
#define NS (ZUI_DOCK_RINGS * ZUI_DOCK_SLOTS)

static uint32_t PX[W * PH], PX2[W * PH];
static zui_surface_t S;
static zui_dock_t D;

static uint32_t at(int32_t x, int32_t y)
{
    return S.px[y * S.stride + x] & 0xFFFFFFu;
}

static void ground(void)
{
    zui_fill(&S, (zui_rect_t){0, 0, S.w, S.h}, ZUI_VOID);
}

static zui_event_t ev(uint8_t type, int32_t x, int32_t y)
{
    static uint32_t ms = 1000;
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.x = x;
    e.y = y;
    e.ms = ms += 16;
    return e;
}
static int send(uint8_t type, int32_t x, int32_t y)
{
    zui_event_t e = ev(type, x, y);
    return zui_dock_event(&D, &e);
}
static int key(uint16_t k, uint8_t mods)
{
    zui_event_t e = ev(ZUI_EV_KEY_DOWN, 0, 0);
    e.key = k;
    e.mods = mods;
    return zui_dock_event(&D, &e);
}
static int wheel(int32_t x, int32_t y, int32_t dy)
{
    zui_event_t e = ev(ZUI_EV_SCROLL, x, y);
    e.dy = dy;
    return zui_dock_event(&D, &e);
}

static const char *APPS[] = {
    "glint",   "quillon", "burrow",  "murmuration", "lantern",  "ledger", "atlas",  "chorus",
    "vesper",  "orrery",  "tessera", "sextant",     "almanac",  "warren", "cairn",  "beacon",
    "harbour", "kestrel", "loom",    "meridian",    "nocturne", "parley", "quarry", "reliquary"};
#define NAPPS (sizeof APPS / sizeof APPS[0])

static zui_dock_item_t OPEN[ZUI_DOCK_ITEMS];
/* n open apps (the first is in front) and p pinned (from the end of APPS). */
static void fill(uint32_t n, uint32_t p)
{
    memset(OPEN, 0, sizeof OPEN);
    for (uint32_t i = 0; i < n; i++) {
        strcpy(OPEN[i].app, APPS[i]);
        snprintf(OPEN[i].title, sizeof OPEN[i].title, "%c%s", APPS[i][0] - 32, APPS[i] + 1);
        OPEN[i].win = i + 1u;
        OPEN[i].activity = (uint8_t) (i == 1       ? ZUI_DOCK_ASK
                                      : i % 3 == 0 ? ZUI_DOCK_BUSY
                                                   : ZUI_DOCK_IDLE);
        OPEN[i].front = i == 0;
        OPEN[i].minimised = i == n - 1u && n > 3;
    }
    zui_dock_set_open(&D, OPEN, n);
    while (D.npinned) zui_dock_unpin(&D, D.pinned[0]);
    for (uint32_t i = 0; i < p; i++) {
        char t[ZUI_DOCK_NAME];
        const char *a = APPS[NAPPS - 1u - i];
        snprintf(t, sizeof t, "%c%s", a[0] - 32, a + 1);
        zui_dock_pin(&D, a, t);
    }
}

static int moves, last_move;
static void on_move(zui_dock_t *d, int32_t slot, void *ctx)
{
    (void) d, (void) ctx;
    moves++;
    last_move = slot;
}

/* every slot inside the screen, clear of the edges, no two overlapping */
static bool sound(const zui_dock_t *d, const char **why)
{
    zui_dock_slot_t s[NS];
    uint32_t n = zui_dock_slots(d, s, NS);
    int32_t half = d->icon / 2, m = ZUI_DOCK_EDGE - 1, gap = d->icon + 2 * ZUI_DOCK_PAD;
    for (uint32_t i = 0; i < n; i++) {
        if (s[i].cx - half < m || s[i].cy - half < m || s[i].cx + half > d->w - m ||
            s[i].cy + half > d->h - m) {
            *why = "off the screen";
            return false;
        }
        for (uint32_t j = i + 1u; j < n; j++) {
            int64_t dx = s[i].cx - s[j].cx, dy = s[i].cy - s[j].cy;
            if (dx * dx + dy * dy < (int64_t) gap * gap) {
                *why = "overlap";
                return false;
            }
            if (s[i].ring == s[j].ring && s[j].slot == s[i].slot + 1u && s[j].cx <= s[i].cx) {
                *why = "not left to right";
                return false;
            }
        }
    }
    return true;
}

static void test_trig(void)
{
    CHECK(zui_dock_sin(0) == 0 && zui_dock_sin(1024) == 16384 && zui_dock_sin(2048) == 0,
          "sin at 0, 90, 180");
    CHECK(zui_dock_sin(3072) == -16384 && zui_dock_cos(0) == 16384 && zui_dock_cos(2048) == -16384,
          "and 270; cos");
    CHECK(zui_dock_sin(512) >= 11584 && zui_dock_sin(512) <= 11586, "sin 45 (%d)",
          zui_dock_sin(512));
    CHECK(zui_dock_sin(-1024) == -16384 && zui_dock_sin(4096 + 1024) == 16384, "wraps");
    bool mono = true, unit = true;
    for (int32_t a = 1; a <= 1024; a++) mono = mono && zui_dock_sin(a) >= zui_dock_sin(a - 1);
    for (int32_t a = 0; a < 4096; a += 7) {
        int64_t s = zui_dock_sin(a), c = zui_dock_cos(a), r = s * s + c * c, one = 16384 * 16384;
        unit = unit && r > one - one / 1000 && r < one + one / 1000;
    }
    CHECK(mono && unit, "rises through the quarter; sin^2 + cos^2 = 1");
}

static void test_seal(void)
{
    zui_dock_init(&D, W, H, false);
    zui_rect_t k = zui_dock_keepout(&D);
    CHECK(k.x == 0 && k.y == H - ZUI_DOCK_SEAL && k.w == ZUI_DOCK_SEAL && k.h == ZUI_DOCK_SEAL,
          "DK1: the resting dock is the seal's corner");
    CHECK(zui_dock_hit(&D, 5, H - 5, 0) == ZUI_DOCK_HIT_SEAL &&
              zui_dock_hit(&D, 50, H - 50, 0) == ZUI_DOCK_HIT_NONE,
          "the seal is a quarter disc");
    CHECK(!zui_dock_is_open(&D) && zui_dock_slots(&D, 0, 0) == 0, "nothing open, nothing pinned");
    zui_dock_place(&D, ZUI_DOCK_TOP_RIGHT, 500, false);
    CHECK(D.seal == ZUI_DOCK_SEAL_MAX, "the seal size is clamped");
    k = zui_dock_keepout(&D);
    CHECK(k.x == W - ZUI_DOCK_SEAL_MAX && k.y == 0 && zui_dock_hit(&D, W - 3, 3, 0) == 1,
          "any corner");
    zui_dock_place(&D, ZUI_DOCK_BOTTOM_LEFT, 0, false);
    CHECK(D.seal == ZUI_DOCK_SEAL_MIN, "and at least the minimum");
    CHECK(zui_dock_corner_for_edge(0) == ZUI_DOCK_BOTTOM_LEFT &&
              zui_dock_corner_for_edge(1) == ZUI_DOCK_TOP_LEFT &&
              zui_dock_corner_for_edge(2) == ZUI_DOCK_BOTTOM_RIGHT &&
              zui_dock_corner_for_edge(3) == ZUI_DOCK_TOP_RIGHT,
          "the panel's edge picks the corner");
    zui_dock_place(&D, ZUI_DOCK_BOTTOM_LEFT, ZUI_DOCK_SEAL, false);
}

static void test_rings(void)
{
    const char *why = "";
    zui_dock_init(&D, W, H, false);
    fill(6, 10);
    CHECK(D.nitem == 16 && D.nopen == 6, "6 open, 10 pinned");
    CHECK(D.ring[0].cap >= 4 && D.ring[1].cap > D.ring[0].cap && D.ring[2].cap > D.ring[1].cap,
          "DK2: outer rings hold more (%u %u %u)", D.ring[0].cap, D.ring[1].cap, D.ring[2].cap);
    zui_dock_slot_t s[NS];
    uint32_t n = zui_dock_slots(&D, s, NS);
    CHECK(n == 16 && D.nring == 3, "everything shows; ring 2 takes the rest (%u)", n);
    bool open_in = true, pinned_out = true;
    for (uint32_t i = 0; i < n; i++) {
        if (s[i].ring == 0) open_in = open_in && D.item[s[i].item].win;
        if (s[i].ring == 1) pinned_out = pinned_out && !D.item[s[i].item].win;
    }
    CHECK(open_in && pinned_out, "ring 0 open apps, ring 1 pinned");
    CHECK(s[0].item == 0, "most recent first");
    CHECK(sound(&D, &why), "DK3: icons clear of each other and the edges: %s", why);
    CHECK(D.ring[2].n == 16 - D.ring[0].cap - D.ring[1].cap && D.item[D.ring[2].item[0]].win != 0,
          "the overflow ring: open apps first");
    fill(3, 2);
    CHECK(D.nring == 2 && zui_dock_slots(&D, 0, 0) == 5, "few apps: no third ring");
    fill(9, 0);
    CHECK(D.ring[1].n == 9 - D.ring[0].cap && D.ring[2].n == 0,
          "no pinned apps: ring 1 takes the overflow");
    /* pinning */
    fill(2, 0);
    CHECK(zui_dock_pin(&D, "glint", "Glint") && D.nitem == 2 && D.item[0].pinned,
          "an open app pinned stays on the open ring");
    CHECK(zui_dock_pin(&D, "glint", "Glint") && D.npinned == 1, "no app pinned twice");
    char nm[16];
    for (uint32_t i = 1; i < ZUI_DOCK_PINNED; i++) {
        snprintf(nm, sizeof nm, "p%u", i);
        zui_dock_pin(&D, nm, 0);
    }
    CHECK(!zui_dock_pin(&D, "one-more", 0) && D.npinned == ZUI_DOCK_PINNED, "at most PINNED");
    CHECK(!strcmp(D.item[2].title, "p1"), "no title: the app's name");
    CHECK(zui_dock_unpin(&D, "p1") && !zui_dock_unpin(&D, "p1") && D.npinned == 15, "unpin");
    /* touch sizes */
    zui_dock_init(&D, W, H, true);
    fill(6, 10);
    CHECK(D.icon >= ZUI_TOUCH_MIN && sound(&D, &why), "touch: 44 pt or more, still clear: %s", why);
    /* every corner */
    for (int c = 0; c < 4; c++) {
        zui_dock_init(&D, W, H, false);
        zui_dock_place(&D, (zui_dock_corner_t) c, ZUI_DOCK_SEAL, false);
        fill(6, 10);
        n = zui_dock_slots(&D, s, NS);
        bool quad = true;
        for (uint32_t i = 0; i < n; i++) {
            bool right = c == ZUI_DOCK_BOTTOM_RIGHT || c == ZUI_DOCK_TOP_RIGHT;
            bool top = c >= ZUI_DOCK_TOP_LEFT;
            quad = quad && (right ? s[i].cx > W / 2 : s[i].cx < W / 2) &&
                   (top ? s[i].cy < H / 2 : s[i].cy > H / 2);
        }
        CHECK(n == 16 && quad && sound(&D, &why), "corner %d: in its quarter: %s", c, why);
    }
}

static void test_bloom(void)
{
    zui_dock_init(&D, W, H, false);
    D.on_move = on_move;
    fill(6, 10);
    CHECK(send(ZUI_EV_PTR_MOVE, 600, 400) == ZUI_DOCK_IGNORED && !zui_dock_is_open(&D),
          "the pointer elsewhere: nothing");
    CHECK(send(ZUI_EV_PTR_MOVE, 10, H - 10) == ZUI_DOCK_USED && zui_dock_is_open(&D) &&
              D.phase == ZUI_DOCK_BLOOMING,
          "DK2: onto the seal: it blooms");
    zui_dock_tick(&D, ZUI_DOCK_BLOOM_MS / 2);
    CHECK(D.phase == ZUI_DOCK_BLOOMING, "over BLOOM_MS");
    zui_dock_tick(&D, ZUI_DOCK_BLOOM_MS);
    CHECK(D.phase == ZUI_DOCK_OPEN && D.focus == -1 && !D.kbd, "open, no keyboard focus");
    zui_dock_slot_t s;
    zui_dock_slot(&D, 2, &s);
    moves = 0;
    CHECK(send(ZUI_EV_PTR_MOVE, s.cx + 3, s.cy - 2) == ZUI_DOCK_USED && D.hot == 2 && moves == 1 &&
              last_move == 2,
          "DK9: the pointer on a slot is told");
    CHECK(zui_dock_hit(&D, s.cx, s.cy, 0) == ZUI_DOCK_HIT_SLOT, "slots are hit");
    send(ZUI_EV_PTR_MOVE, 900, 300);
    CHECK(D.leave == ZUI_DOCK_LEAVE_MS && D.hot == -1, "DK5: leaving starts the count");
    zui_dock_tick(&D, ZUI_DOCK_LEAVE_MS - 50);
    send(ZUI_EV_PTR_MOVE, s.cx, s.cy);
    zui_dock_tick(&D, 200);
    CHECK(zui_dock_is_open(&D) && D.leave < 0, "coming back in time keeps it open");
    send(ZUI_EV_PTR_MOVE, 900, 300);
    zui_dock_tick(&D, ZUI_DOCK_LEAVE_MS);
    CHECK(D.phase == ZUI_DOCK_CLOSING, "after LEAVE_MS it closes");
    zui_dock_tick(&D, ZUI_DOCK_BLOOM_MS);
    CHECK(D.phase == ZUI_DOCK_REST && D.t == 0, "and rests");

    /* DK6: press and let go on a slot */
    send(ZUI_EV_PTR_MOVE, 10, H - 10);
    zui_dock_tick(&D, ZUI_DOCK_BLOOM_MS);
    zui_dock_slot(&D, 7, &s);
    CHECK(send(ZUI_EV_PTR_DOWN, s.cx, s.cy) == ZUI_DOCK_USED, "a press on a slot");
    CHECK(send(ZUI_EV_PTR_UP, s.cx, s.cy) == ZUI_DOCK_CHOSE && zui_dock_chosen(&D) &&
              zui_dock_chosen(&D) == &D.item[s.item] && D.phase == ZUI_DOCK_CLOSING,
          "letting go chooses it and closes");
    zui_dock_tick(&D, ZUI_DOCK_BLOOM_MS);
    /* press the seal, sweep, let go on a slot */
    CHECK(send(ZUI_EV_PTR_DOWN, 4, H - 4) == ZUI_DOCK_USED && zui_dock_is_open(&D),
          "a press on the seal opens it");
    zui_dock_slot(&D, 0, &s);
    send(ZUI_EV_PTR_MOVE, s.cx, s.cy);
    CHECK(send(ZUI_EV_PTR_UP, s.cx, s.cy) == ZUI_DOCK_CHOSE && zui_dock_chosen(&D)->win == 1,
          "one gesture: press, sweep, release");
    zui_dock_tick(&D, ZUI_DOCK_BLOOM_MS);
    /* the seal as a toggle */
    send(ZUI_EV_PTR_DOWN, 4, H - 4);
    send(ZUI_EV_PTR_UP, 4, H - 4);
    CHECK(zui_dock_is_open(&D), "a click opens it and it stays");
    send(ZUI_EV_PTR_DOWN, 4, H - 4);
    send(ZUI_EV_PTR_UP, 4, H - 4);
    CHECK(!zui_dock_is_open(&D), "a second click closes it");
    zui_dock_tick(&D, ZUI_DOCK_BLOOM_MS);
    /* a press outside */
    send(ZUI_EV_PTR_MOVE, 10, H - 10);
    CHECK(send(ZUI_EV_PTR_DOWN, 900, 200) == ZUI_DOCK_IGNORED && !zui_dock_is_open(&D),
          "a press outside a hover bloom closes it and goes on to the windows");
    zui_dock_tick(&D, ZUI_DOCK_BLOOM_MS);
    send(ZUI_EV_PTR_MOVE, 900, 200);
    CHECK(send(ZUI_EV_PTR_UP, 900, 200) == ZUI_DOCK_IGNORED, "and its release too");

    /* touch: a tap opens and stays; its pointer twin is not counted twice */
    zui_event_t t = ev(ZUI_EV_TOUCH_BEGIN, 6, H - 6), p = t;
    p.type = ZUI_EV_PTR_DOWN;
    CHECK(zui_dock_event(&D, &t) == ZUI_DOCK_USED && zui_dock_event(&D, &p) == ZUI_DOCK_USED &&
              zui_dock_is_open(&D) && D.sticky,
          "touch: a tap on the seal opens it");
    t.type = ZUI_EV_TOUCH_END;
    p = t;
    p.type = ZUI_EV_PTR_UP;
    zui_dock_event(&D, &t);
    zui_dock_event(&D, &p);
    CHECK(zui_dock_is_open(&D), "the twin of the lift does not close it");
    send(ZUI_EV_PTR_MOVE, 900, 300);
    zui_dock_tick(&D, ZUI_DOCK_LEAVE_MS * 2);
    CHECK(zui_dock_is_open(&D), "a touch bloom has no hover to leave");
    zui_dock_slot(&D, 3, &s);
    int chose = 0;
    t = ev(ZUI_EV_TOUCH_BEGIN, s.cx, s.cy);
    p = t;
    p.type = ZUI_EV_PTR_DOWN;
    chose += zui_dock_event(&D, &t) == ZUI_DOCK_CHOSE;
    chose += zui_dock_event(&D, &p) == ZUI_DOCK_CHOSE;
    t.type = ZUI_EV_TOUCH_END;
    p = t;
    p.type = ZUI_EV_PTR_UP;
    chose += zui_dock_event(&D, &t) == ZUI_DOCK_CHOSE;
    chose += zui_dock_event(&D, &p) == ZUI_DOCK_CHOSE;
    CHECK(chose == 1, "a tap on a slot chooses once (%d)", chose);
    zui_dock_tick(&D, ZUI_DOCK_BLOOM_MS);
    zui_dock_bloom(&D, true, false);
    D.sticky = true;
    CHECK(send(ZUI_EV_PTR_DOWN, 900, 200) == ZUI_DOCK_USED && !zui_dock_is_open(&D),
          "a sticky bloom eats the press that closes it");
    zui_event_t two = ev(ZUI_EV_TOUCH_BEGIN, 6, H - 6);
    two.touch_id = 1;
    CHECK(zui_dock_event(&D, &two) == ZUI_DOCK_IGNORED, "a second finger is not the dock's");
}

static void test_keys(void)
{
    zui_dock_init(&D, W, H, false);
    D.on_move = on_move;
    fill(6, 10);
    CHECK(key(ZUI_KEY_RIGHT, 0) == ZUI_DOCK_IGNORED, "closed: keys are not the dock's");
    moves = 0;
    CHECK(key('D', ZUI_MOD_CMD | ZUI_MOD_SHIFT) == ZUI_DOCK_USED && zui_dock_is_open(&D) && D.kbd &&
              D.focus == 0 && moves == 1,
          "DK8: the shortcut opens it, the first slot focused and spoken");
    key(ZUI_KEY_LEFT, 0);
    CHECK(D.focus == 0, "Left at the start of a ring that does not turn stays");
    key(ZUI_KEY_RIGHT, 0);
    key(ZUI_KEY_RIGHT, 0);
    CHECK(D.focus == 2, "Right moves along");
    zui_dock_slot_t a, b;
    zui_dock_slot(&D, 2, &a);
    key(ZUI_KEY_UP, 0);
    zui_dock_slot(&D, D.focus, &b);
    CHECK(b.ring == 1, "Up: out to the next ring (bottom corner)");
    zui_dock_slot_t all[NS];
    uint32_t n = zui_dock_slots(&D, all, NS);
    int64_t best = -1;
    for (uint32_t i = 0; i < n; i++) {
        if (all[i].ring != 1) continue;
        int64_t dx = all[i].cx - a.cx, dy = all[i].cy - a.cy, dd = dx * dx + dy * dy;
        if (best < 0 || dd < best) best = dd;
    }
    int64_t dx = b.cx - a.cx, dy = b.cy - a.cy;
    CHECK(dx * dx + dy * dy == best, "to the nearest slot");
    key(ZUI_KEY_DOWN, 0);
    zui_dock_slot(&D, D.focus, &b);
    CHECK(b.ring == 0, "Down: back in");
    key(ZUI_KEY_END, 0);
    CHECK(D.focus == (int32_t) D.ring[0].cap - 1, "End: the ring's last");
    key(ZUI_KEY_HOME, 0);
    CHECK(D.focus == 0, "Home: its first");
    key(ZUI_KEY_TAB, ZUI_MOD_SHIFT);
    CHECK(D.focus == (int32_t) n - 1, "Shift-Tab wraps to the last slot");
    key(ZUI_KEY_TAB, 0);
    CHECK(D.focus == 0, "Tab wraps back");
    CHECK(key('x', 0) == ZUI_DOCK_USED, "the open dock has the keyboard");
    key(ZUI_KEY_RIGHT, 0);
    CHECK(key(ZUI_KEY_ENTER, 0) == ZUI_DOCK_CHOSE && zui_dock_chosen(&D) == &D.item[1] &&
              !zui_dock_is_open(&D),
          "Enter chooses and closes");
    key('d', ZUI_MOD_CMD | ZUI_MOD_SHIFT);
    CHECK(key(ZUI_KEY_ESCAPE, 0) == ZUI_DOCK_USED && !zui_dock_is_open(&D), "Esc closes");
    zui_dock_tick(&D, ZUI_DOCK_BLOOM_MS);
    key('d', ZUI_MOD_CMD | ZUI_MOD_SHIFT);
    key('d', ZUI_MOD_CMD | ZUI_MOD_SHIFT);
    CHECK(!zui_dock_is_open(&D), "the shortcut again closes it");
    zui_dock_tick(&D, ZUI_DOCK_BLOOM_MS);
    send(ZUI_EV_PTR_MOVE, 10, H - 10);
    CHECK(zui_dock_is_open(&D) && key(ZUI_KEY_RIGHT, 0) == ZUI_DOCK_IGNORED,
          "a bloom opened by the pointer takes no keys");
    zui_event_t tx = ev(ZUI_EV_TEXT, 0, 0);
    CHECK(zui_dock_event(&D, &tx) == ZUI_DOCK_IGNORED, "nor text");
}

static void test_turning(void)
{
    zui_dock_init(&D, W, H, false);
    D.on_move = on_move;
    fill(10, 16);
    zui_dock_ring_t *g = &D.ring[2];
    CHECK(g->n > g->cap && D.ring[0].n == D.ring[0].cap, "DK4: an overflow ring with more (%u/%u)",
          g->n, g->cap);
    zui_dock_bloom(&D, true, false);
    zui_dock_tick(&D, ZUI_DOCK_BLOOM_MS);
    zui_dock_slot_t s, s2;
    int32_t last = (int32_t) zui_dock_slots(&D, 0, 0) - 1;
    zui_dock_slot(&D, last, &s);
    uint16_t before = s.item;
    CHECK(wheel(s.cx, s.cy, 39) == ZUI_DOCK_USED && g->rot == 0, "less than a step: no turn");
    wheel(s.cx, s.cy, 1);
    zui_dock_slot(&D, last, &s2);
    CHECK(g->rot == 1 && g->spin == ZUI_DOCK_SPIN_MS && s2.item != before,
          "a step of scroll turns the ring one slot");
    memset(PX, 0, sizeof PX);
    ground();
    zui_dock_draw(&D, &S); /* mid-turn, with the slots at both ends fading */
    zui_dock_tick(&D, ZUI_DOCK_SPIN_MS);
    CHECK(g->spin == 0, "over SPIN_MS");
    wheel(s.cx, s.cy, -40);
    CHECK(g->rot == 0, "back");
    wheel(s.cx, s.cy, -40);
    CHECK(g->rot == g->n - 1u, "and round");
    zui_dock_slot(&D, 0, &s);
    wheel(s.cx, s.cy, 80);
    CHECK(g->rot == 1 && D.ring[0].rot == 0, "on a ring that does not turn: the turning one");
    CHECK(!zui_dock_turn(&D, 0, 1) && !zui_dock_turn(&D, 9, 1), "only rings with more turn");
    /* arrows at the end of a turning ring turn it */
    zui_dock_bloom(&D, true, true);
    key(ZUI_KEY_END, 0);
    key(ZUI_KEY_UP, 0);
    key(ZUI_KEY_UP, 0);
    zui_dock_slot(&D, D.focus, &s);
    CHECK(s.ring == 2, "on the overflow ring");
    key(ZUI_KEY_END, 0);
    uint16_t rot = g->rot;
    moves = 0;
    key(ZUI_KEY_RIGHT, 0);
    CHECK(g->rot == (rot + 1u) % g->n && moves == 1, "Right past the end turns it, spoken");
    key(ZUI_KEY_HOME, 0);
    key(ZUI_KEY_LEFT, 0);
    CHECK(g->rot == rot, "Left past the start turns back");
    /* a short screen: no room for ring 2, rings 0 and 1 keep all and turn */
    zui_dock_init(&D, 500, 260, true);
    fill(10, 10);
    CHECK(D.ring[2].cap == 0 && D.ring[1].cap && D.ring[0].n == 10 && D.ring[1].n == 10 &&
              D.ring[0].n > D.ring[0].cap,
          "short screen: rings 0 and 1 keep all and turn (%u %u)", D.ring[0].cap, D.ring[1].cap);
    const char *why = "";
    CHECK(sound(&D, &why), "and stay on the screen: %s", why);
    zui_dock_init(&D, 500, 200, true);
    fill(4, 4);
    CHECK(D.ring[1].cap == 0 && D.ring[0].n == 8 && D.nring == 1, "room for one ring: all on it");
    CHECK(sound(&D, &why), "and stay on the screen: %s", why);
}

static bool gold_near(int32_t x, int32_t y, int32_t r)
{
    for (int32_t j = y - r; j <= y + r; j++)
        for (int32_t i = x - r; i <= x + r; i++)
            if (i >= 0 && j >= 0 && i < S.w && j < S.h && at(i, j) == ZUI_GOLD) return true;
    return false;
}

static void test_draw(void)
{
    zui_dock_init(&D, W, H, false);
    fill(6, 10);
    D.clock = ZUI_DOCK_FAST_MS / 2; /* the tick's pulse at its height */
    ground();
    zui_dock_draw(&D, &S);
    int32_t r = D.seal;
    CHECK(at(r / 2, H - r / 4) != ZUI_VOID && at(r + 10, H - r - 10) == ZUI_VOID,
          "DK10: at rest only the seal is drawn");
    int32_t tx = r * 707 / 1000, ty = H - r * 707 / 1000;
    CHECK(gold_near(tx, ty, 3), "DK7: an app needs input: the gold tick on the seal");
    OPEN[1].activity = ZUI_DOCK_IDLE;
    zui_dock_set_open(&D, OPEN, 6);
    ground();
    zui_dock_draw(&D, &S);
    CHECK(!gold_near(tx, ty, 6), "none asking: no tick");
    OPEN[1].activity = ZUI_DOCK_ASK;
    zui_dock_set_open(&D, OPEN, 6);
    zui_dock_bloom(&D, true, true);
    zui_dock_tick(&D, ZUI_DOCK_BLOOM_MS);
    ground();
    zui_dock_draw(&D, &S);
    zui_dock_slot_t s;
    zui_dock_slot(&D, 0, &s);
    int32_t h = D.icon / 2;
    CHECK(at(s.cx + h + 1, s.cy) == ZUI_GOLD || at(s.cx + h + 2, s.cy) == ZUI_GOLD,
          "the focused slot has the gold ring");
    zui_dock_slot(&D, 2, &s);
    CHECK(!gold_near(s.cx + h + 2, s.cy, 1), "the others do not");
    zui_dock_slot(&D, 1, &s); /* the asking app: its pip, toward the seal */
    int32_t px = s.cx * (D.ring[0].r - h - 6) / D.ring[0].r;
    int32_t py = H - (H - s.cy) * (D.ring[0].r - h - 6) / D.ring[0].r;
    CHECK(gold_near(px, py, 2), "its pip is gold");
    zui_dock_slot(&D, 0, &s);
    px = s.cx * (D.ring[0].r - h - 6) / D.ring[0].r;
    py = H - (H - s.cy) * (D.ring[0].r - h - 6) / D.ring[0].r;
    bool green = false;
    for (int32_t j = py - 2; j <= py + 2; j++)
        for (int32_t i = px - 2; i <= px + 2; i++) {
            uint32_t c = at(i, j);
            green = green || (((c >> 8) & 0xFF) > 0xA0 && (c >> 16) < 0xA0);
        }
    CHECK(green, "a running app's pip is green");
    memcpy(PX2, PX, sizeof PX);
    ground();
    zui_dock_draw(&D, &S);
    CHECK(!memcmp(PX, PX2, sizeof PX), "DK11: the same state, the same pixels");
    zui_dock_tick(&D, 200);
    ground();
    zui_dock_draw(&D, &S);
    CHECK(memcmp(PX, PX2, sizeof PX) != 0, "DK7: the pips pulse with time");

    /* reduced motion: instant, and the pips still */
    zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION, true);
    zui_motion_set_crossfade(0);
    zui_dock_bloom(&D, false, false);
    CHECK(D.phase == ZUI_DOCK_REST, "DK5: reduced motion closes at once");
    zui_dock_bloom(&D, true, true);
    CHECK(D.phase == ZUI_DOCK_OPEN, "and opens at once");
    ground();
    zui_dock_draw(&D, &S);
    memcpy(PX2, PX, sizeof PX);
    zui_dock_tick(&D, 333);
    ground();
    zui_dock_draw(&D, &S);
    CHECK(!memcmp(PX, PX2, sizeof PX), "the pips are still; shape and colour tell them apart");
    zui_dock_bloom(&D, true, true);
    zui_dock_turn(&D, 2, 1);
    CHECK(D.ring[2].spin == 0, "a turn is a cut");
    zui_motion_reset();

    /* blooming: the rings grow out of the seal */
    zui_dock_bloom(&D, false, false);
    zui_dock_tick(&D, ZUI_DOCK_BLOOM_MS);
    zui_dock_bloom(&D, true, false);
    zui_dock_tick(&D, ZUI_DOCK_BLOOM_MS / 4);
    ground();
    zui_dock_draw(&D, &S);
    int32_t o = D.ring[2].r + D.icon / 2 + 12;
    CHECK(at(o * 7 / 10 - 4, H - o * 7 / 10 + 4) == ZUI_VOID, "early in the bloom, not yet out");
    zui_dock_tick(&D, ZUI_DOCK_BLOOM_MS);
    ground();
    zui_dock_draw(&D, &S);
    CHECK(at(o * 7 / 10 - 4, H - o * 7 / 10 + 4) != ZUI_VOID, "open: the carved panel");
    zui_dock_place(&D, ZUI_DOCK_BOTTOM_LEFT, ZUI_DOCK_SEAL, true);
    zui_dock_bloom(&D, false, false);
    zui_dock_tick(&D, ZUI_DOCK_BLOOM_MS);
    OPEN[1].activity = ZUI_DOCK_IDLE;
    zui_dock_set_open(&D, OPEN, 6);
    ground();
    zui_dock_draw(&D, &S);
    uint32_t faint = at(r / 3, H - r / 3);
    send(ZUI_EV_PTR_MOVE, 3, H - 3);
    zui_dock_bloom(&D, false, false);
    zui_dock_tick(&D, ZUI_DOCK_BLOOM_MS);
    D.near = true;
    ground();
    zui_dock_draw(&D, &S);
    CHECK(faint != at(r / 3, H - r / 3) && faint != ZUI_VOID, "auto-hide: faint until reached");
}

static void test_phone(void)
{
    zui_surface_init(&S, PX, PW, PH, PW);
    zui_dock_init(&D, PW, PH, true);
    fill(6, 8);
    CHECK(D.compact, "a narrow screen is compact");
    zui_rect_t k = zui_dock_keepout(&D);
    CHECK(k.x == PW / 2 - D.seal && k.w == 2 * D.seal && k.y == PH - D.seal,
          "DK10: a half seal at the bottom centre");
    CHECK(zui_dock_hit(&D, PW / 2, PH - 3, 0) == ZUI_DOCK_HIT_SEAL &&
              zui_dock_hit(&D, 3, PH - 3, 0) == ZUI_DOCK_HIT_NONE,
          "hit there");
    const char *why = "";
    zui_dock_slot_t s[NS];
    uint32_t n = zui_dock_slots(&D, s, NS);
    bool above = true;
    for (uint32_t i = 0; i < n; i++) {
        int32_t dx = s[i].cx - PW / 2, dy = PH - s[i].cy, m = D.seal + D.icon / 2;
        above = above && dx * dx + dy * dy > m * m && s[i].cy < PH - D.icon / 2;
    }
    CHECK(n == 14 && above && sound(&D, &why), "half rings rising: everything shows, clear: %s",
          why);
    CHECK(D.icon >= ZUI_TOUCH_MIN, "touch-sized");
    zui_event_t t = ev(ZUI_EV_TOUCH_BEGIN, PW / 2, PH - 5);
    zui_dock_event(&D, &t);
    zui_dock_tick(&D, ZUI_DOCK_BLOOM_MS);
    t = ev(ZUI_EV_TOUCH_END, s[4].cx, s[4].cy);
    CHECK(zui_dock_event(&D, &t) == ZUI_DOCK_CHOSE, "a sweep up from the seal chooses");
    zui_dock_event(&D, &(zui_event_t){.type = ZUI_EV_RESIZE, .x = W, .y = H});
    CHECK(!D.compact && D.w == W && zui_dock_hit(&D, 3, H - 3, 0) == ZUI_DOCK_HIT_SEAL,
          "a wider screen: back to the corner");
    zui_surface_init(&S, PX, W, H, W);
}

static void test_speech(void)
{
    char buf[2048];
    zui_dock_init(&D, W, H, false);
    fill(4, 2);
    zui_dock_a11y(&D, buf, sizeof buf);
    CHECK(!strcmp(buf, "button \"Apps, 1 needs input\" collapsed\n"), "DK9: collapsed: %s", buf);
    zui_dock_bloom(&D, true, true);
    zui_dock_a11y(&D, buf, sizeof buf);
    CHECK(!strncmp(buf,
                   "toolbar \"Apps\" expanded\n  button \"Glint, running, in front\" focused\n",
                   68),
          "expanded, each slot a button: %s", buf);
    CHECK(strstr(buf, "\"Quillon, needs input\"") &&
              strstr(buf, "\"Murmuration, running, "
                          "minimised\"") &&
              strstr(buf, "\"Reliquary, not running\""),
          "the states");
    zui_dock_say(&D, 2, buf, sizeof buf);
    CHECK(!strcmp(buf, "Burrow, open"), "say one slot: %s", buf);
    CHECK(zui_dock_say(&D, 99, buf, sizeof buf) == 0 && !buf[0], "no such slot");
    CHECK(zui_dock_a11y(&D, buf, 10) == 9 && strlen(buf) == 9, "a short buffer is cut");
    fill(4, 0);
    OPEN[1].activity = ZUI_DOCK_ASK;
    OPEN[2].activity = ZUI_DOCK_ASK;
    zui_dock_set_open(&D, OPEN, 4);
    zui_dock_bloom(&D, false, false);
    zui_dock_a11y(&D, buf, sizeof buf);
    CHECK(!strcmp(buf, "button \"Apps, 2 need input\" collapsed\n"), "two: %s", buf);
}

/* ---- pictures for people (not checked): ZXV_DOCK_SHOT names a folder,
 * ZXV_DOCK_FONT a TrueType file. -------------------------------------------- */

static zui_glyph_cache_t CACHE;

/* test-only vector art: a few engraved shapes so the apps differ */
static void tri(zui_surface_t *s, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2,
                int32_t y2, uint32_t rgb, uint32_t a)
{
    int32_t ymin = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int32_t ymax = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    for (int32_t y = ymin; y <= ymax; y++) {
        int32_t xs[3], n = 0, px[3] = {x0, x1, x2}, py[3] = {y0, y1, y2};
        for (int i = 0; i < 3; i++) {
            int j = (i + 1) % 3;
            if ((py[i] <= y && py[j] > y) || (py[j] <= y && py[i] > y))
                xs[n++] = px[i] + (y - py[i]) * (px[j] - px[i]) / (py[j] - py[i]);
        }
        if (n == 2) {
            int32_t a0 = xs[0] < xs[1] ? xs[0] : xs[1], a1 = xs[0] < xs[1] ? xs[1] : xs[0];
            zui_fill_a(s, (zui_rect_t){a0, y, a1 - a0 + 1, 1}, rgb, a);
        }
    }
}

static void art(zui_surface_t *s, zui_rect_t b, const zui_dock_item_t *it, uint32_t lit, void *ctx)
{
    (void) ctx;
    uint32_t a = (255u * (lit > 256u ? 256u : lit)) >> 8;
    int32_t r = b.w / 2, cx = b.x + r, cy = b.y + r, q = r / 2;
    zui_fill_rrect(s, b, r, ZUI_RAISED, a);
    zui_engrave(s, b, r, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, a * 70u / 255u);
    uint32_t ink = ZUI_TEXT_2;
    if (!it) { /* the fox's mark */
        tri(s, cx - q - 2, cy - q, cx, cy + q + 2, cx + q + 2, cy - q, ZUI_TEXT, a);
        tri(s, cx - q - 2, cy - q, cx - q + 1, cy - q - 6, cx - 2, cy - q, ZUI_TEXT, a);
        tri(s, cx + q + 2, cy - q, cx + q - 1, cy - q - 6, cx + 2, cy - q, ZUI_TEXT, a);
        zui_fill_rrect(s, (zui_rect_t){cx - 1, cy + q - 2, 3, 3}, 1, ZUI_PANEL, a);
        return;
    }
    uint32_t h = 0;
    for (const char *p = it->app; *p; p++) h = h * 31u + (uint8_t) *p;
    switch (h % 6u) {
    case 0:
        zui_stroke_rrect(s, (zui_rect_t){cx - q, cy - q, 2 * q, 2 * q}, q, 2, ink, a);
        zui_fill_rrect(s, (zui_rect_t){cx - 2, cy - 2, 4, 4}, 2, ink, a);
        break;
    case 1:
        tri(s, cx, cy - q - 2, cx - q - 1, cy + q, cx + q + 1, cy + q, ink, a);
        break;
    case 2:
        zui_fill_rrect(s, (zui_rect_t){cx - q, cy - q, 2 * q, 2 * q}, 3, ink, a);
        zui_fill_rrect(s, (zui_rect_t){cx - q + 3, cy - q + 3, 2 * q - 6, 2 * q - 6}, 2, ZUI_RAISED,
                       a);
        break;
    case 3:
        zui_fill_rrect(s, (zui_rect_t){cx - q, cy - q, 2 * q, 2 * q}, q, ink, a);
        zui_fill_rrect(s, (zui_rect_t){cx - q + 4, cy - q - 2, 2 * q, 2 * q}, q, ZUI_RAISED, a);
        break;
    case 4:
        zui_fill_a(s, (zui_rect_t){cx - q, cy - 1, 2 * q, 3}, ink, a);
        zui_fill_a(s, (zui_rect_t){cx - 1, cy - q, 3, 2 * q}, ink, a);
        break;
    default:
        for (int i = -1; i <= 1; i++)
            zui_fill_a(s, (zui_rect_t){cx - q, cy + i * (q / 2 + 1) - 1, 2 * q, 2}, ink, a);
        break;
    }
}

static void backdrop(int32_t w, int32_t h)
{
    zui_fill(&S, (zui_rect_t){0, 0, w, h}, ZUI_VOID);
    if (w < 600) {
        zui_rect_t c = {0, 0, w, h - D.seal - 8};
        zui_fill(&S, (zui_rect_t){0, 0, w, 44}, ZUI_PANEL);
        zui_hline(&S, 0, 43, w, 1, ZUI_HAIRLINE);
        (void) c;
        return;
    }
    zui_rect_t r = {300, 90, 820, 560};
    zui_fill_rrect(&S, r, ZUI_R_PANEL, ZUI_VOID, 255);
    zui_fill_rrect(&S, (zui_rect_t){r.x, r.y, r.w, 42}, ZUI_R_PANEL, ZUI_PANEL, 255);
    zui_fill(&S, (zui_rect_t){r.x, r.y + 32, r.w, 10}, ZUI_PANEL);
    zui_hline(&S, r.x, r.y + 31, r.w, 1, ZUI_HAIRLINE);
    zui_fill(&S, (zui_rect_t){r.x + 1, r.y + 32, 220, r.h - 33}, ZUI_PANEL);
    zui_vline(&S, r.x + 221, r.y + 32, r.h - 33, 1, ZUI_HAIRLINE);
    zui_engrave(&S, r, ZUI_R_PANEL, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 90u);
}

static void shot(const char *dir, const char *name, int32_t w, int32_t h)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int32_t i = 0; i < w * h; i++) {
        unsigned char rgb[3] = {(unsigned char) (PX[i] >> 16), (unsigned char) (PX[i] >> 8),
                                (unsigned char) PX[i]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void pictures(void)
{
    const char *dir = getenv("ZXV_DOCK_SHOT"), *font = getenv("ZXV_DOCK_FONT");
    if (!dir) return;
    static ttf_font_t F;
    static uint8_t data[8u << 20];
    static zui_text_t T;
    FILE *fp = font ? fopen(font, "rb") : 0;
    if (fp) {
        size_t n = fread(data, 1, sizeof data, fp);
        fclose(fp);
        if (ttf_parse(&F, data, (uint32_t) n)) {
            zui_glyph_cache_init(&CACHE);
            T = (zui_text_t){&F, &CACHE, 14, false};
        }
    }
    zui_surface_init(&S, PX, W, H, W);
    /* 1: at rest, an app asking */
    zui_dock_init(&D, W, H, false);
    D.icon_fn = art;
    D.text = T.font ? &T : 0;
    fill(6, 10);
    D.clock = ZUI_DOCK_FAST_MS / 2;
    backdrop(W, H);
    zui_dock_draw(&D, &S);
    shot(dir, "1-seal", W, H);
    /* 2: bloomed, 6 open and 10 pinned, the pointer on one */
    zui_dock_bloom(&D, true, false);
    zui_dock_tick(&D, ZUI_DOCK_BLOOM_MS);
    zui_dock_slot_t s;
    zui_dock_slot(&D, 1, &s);
    send(ZUI_EV_PTR_MOVE, s.cx, s.cy);
    backdrop(W, H);
    zui_dock_draw(&D, &S);
    shot(dir, "2-bloom", W, H);
    /* 3: more than fit: the overflow ring, turned three, keyboard focus on it */
    fill(9, 16);
    zui_dock_bloom(&D, true, true);
    zui_dock_turn(&D, 2, 3);
    zui_dock_tick(&D, ZUI_DOCK_SPIN_MS);
    key(ZUI_KEY_UP, 0);
    key(ZUI_KEY_UP, 0);
    key(ZUI_KEY_RIGHT, 0);
    backdrop(W, H);
    zui_dock_draw(&D, &S);
    shot(dir, "3-overflow", W, H);
    /* 3b: the same, caught mid-turn */
    zui_dock_turn(&D, 2, 1);
    zui_dock_tick(&D, ZUI_DOCK_SPIN_MS / 2);
    backdrop(W, H);
    zui_dock_draw(&D, &S);
    shot(dir, "3b-turning", W, H);
    /* 4: a phone */
    zui_surface_init(&S, PX, PW, PH, PW);
    zui_dock_init(&D, PW, PH, true);
    D.icon_fn = art;
    D.text = T.font ? &T : 0;
    fill(5, 8);
    zui_dock_bloom(&D, true, true);
    zui_dock_tick(&D, ZUI_DOCK_BLOOM_MS);
    key(ZUI_KEY_RIGHT, 0);
    backdrop(PW, PH);
    zui_dock_draw(&D, &S);
    shot(dir, "4-phone", PW, PH);
    zui_dock_bloom(&D, false, false);
    zui_dock_tick(&D, ZUI_DOCK_BLOOM_MS);
    backdrop(PW, PH);
    zui_dock_draw(&D, &S);
    shot(dir, "4b-phone-seal", PW, PH);
    zui_surface_init(&S, PX, W, H, W);
}

int main(void)
{
    zui_surface_init(&S, PX, W, H, W);
    zui_motion_reset();
    test_trig();
    test_seal();
    test_rings();
    test_bloom();
    test_keys();
    test_turning();
    test_draw();
    test_phone();
    test_speech();
    pictures();
    printf("%s test_zui_dock: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails, checks);
    return fails ? 1 : 0;
}
