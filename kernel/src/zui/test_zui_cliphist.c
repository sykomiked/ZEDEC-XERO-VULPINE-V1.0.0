/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_cliphist.c — clipboard history (zui_cliphist.h CH1..CH7), and
 * its hook into the widget set's copy and paste. */
#include <stdio.h>
#include <string.h>

#include "zui_cliphist.h"

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

static zui_cliphist_t H;

static int add(const char *s)
{
    return zui_cliphist_add(&H, s, (uint32_t) strlen(s), ZUI_CLIP_PLAIN, 0);
}

static const char *at(uint32_t i)
{
    const zui_clip_t *c = zui_cliphist_get(&H, i);
    return c ? c->text : "(none)";
}

/* No byte of `s` anywhere in the history's memory: a secret left no trace. */
static bool nowhere(const zui_cliphist_t *h, const char *s)
{
    const uint8_t *m = (const uint8_t *) h;
    uint32_t n = (uint32_t) strlen(s);
    for (uint32_t i = 0; i + n <= sizeof *h; i++)
        if (memcmp(m + i, s, n) == 0) return false;
    return true;
}

static void test_ring(void)
{
    zui_cliphist_init(&H);
    CHECK(zui_cliphist_count(&H) == 0 && H.cap_bytes == ZUI_CLIP_CAP && H.bytes == 0, "empty");
    CHECK(add("one") == ZUI_CLIP_ADDED && add("two") == ZUI_CLIP_ADDED &&
              add("three") == ZUI_CLIP_ADDED,
          "three added");
    CHECK(zui_cliphist_count(&H) == 3 && strcmp(at(0), "three") == 0 && strcmp(at(2), "one") == 0,
          "newest first");
    CHECK(H.bytes == 11, "bytes counted (%u)", H.bytes);
    CHECK(zui_cliphist_get(&H, 3) == 0 && zui_cliphist_get(0, 0) == 0, "past the end");
    CHECK(zui_cliphist_get(&H, 0)->seq > zui_cliphist_get(&H, 1)->seq, "seq grows");

    /* CH2: dedupe moves to the front */
    CHECK(add("one") == ZUI_CLIP_MOVED, "one again is moved, not added");
    CHECK(zui_cliphist_count(&H) == 3 && strcmp(at(0), "one") == 0 && strcmp(at(1), "three") == 0 &&
              strcmp(at(2), "two") == 0,
          "one, three, two");
    CHECK(H.bytes == 11, "no bytes added");
    CHECK(zui_cliphist_add(&H, "one", 3, ZUI_CLIP_CODE, 0) == ZUI_CLIP_ADDED,
          "the same text with another type is another clip");
    CHECK(add("on") == ZUI_CLIP_ADDED, "a prefix is another clip");
    /* a pinned clip stays pinned when copied again */
    CHECK(zui_cliphist_pin(&H, 4, true), "pin two");
    CHECK(add("two") == ZUI_CLIP_MOVED && zui_cliphist_get(&H, 0)->pinned, "still pinned in front");

    /* type tags */
    CHECK(zui_cliphist_add(&H, "x", 1, (zui_clip_type_t) 99, 0) == ZUI_CLIP_ADDED &&
              zui_cliphist_get(&H, 0)->type == ZUI_CLIP_PLAIN,
          "an unknown type is plain");
    CHECK(zui_cliphist_add(&H, 0, 1, ZUI_CLIP_PLAIN, 0) == ZUI_CLIP_BAD &&
              zui_cliphist_add(&H, "x", 0, ZUI_CLIP_PLAIN, 0) == ZUI_CLIP_BAD &&
              zui_cliphist_add(0, "x", 1, ZUI_CLIP_PLAIN, 0) == ZUI_CLIP_BAD,
          "NULL or empty");

    /* CH1: count */
    zui_cliphist_init(&H);
    char t[16];
    for (uint32_t i = 0; i < ZUI_CLIP_MAX + 3u; i++) {
        snprintf(t, sizeof t, "clip%u", i);
        add(t);
    }
    CHECK(zui_cliphist_count(&H) == ZUI_CLIP_MAX && H.evicted == 3, "the ring holds ZUI_CLIP_MAX");
    snprintf(t, sizeof t, "clip%u", ZUI_CLIP_MAX + 2u);
    CHECK(strcmp(at(0), t) == 0 && strcmp(at(ZUI_CLIP_MAX - 1u), "clip3") == 0,
          "the three oldest went");
    /* a pinned clip survives being the oldest */
    zui_cliphist_pin(&H, ZUI_CLIP_MAX - 1u, true);
    add("next");
    CHECK(strcmp(at(ZUI_CLIP_MAX - 1u), "clip3") == 0 &&
              strcmp(at(ZUI_CLIP_MAX - 2u), "clip5") == 0,
          "the pinned oldest stays; the oldest unpinned (clip4) goes");
}

static void test_edit(void)
{
    zui_cliphist_init(&H);
    add("a1");
    add("b22");
    add("c333");
    add("d4444");
    /* CH3: delete */
    CHECK(zui_cliphist_delete(&H, 1), "delete c333");
    CHECK(zui_cliphist_count(&H) == 3 && strcmp(at(1), "b22") == 0 && H.bytes == 10,
          "gone, bytes down (%u)", H.bytes);
    CHECK(nowhere(&H, "c333"), "its text is wiped");
    CHECK(!zui_cliphist_delete(&H, 3) && !zui_cliphist_delete(0, 0), "past the end");
    /* pin */
    CHECK(zui_cliphist_pin(&H, 2, true) && zui_cliphist_get(&H, 2)->pinned, "pin a1");
    CHECK(zui_cliphist_pin(&H, 2, false) && !zui_cliphist_get(&H, 2)->pinned, "unpin");
    CHECK(!zui_cliphist_pin(&H, 9, true) && !zui_cliphist_pin(0, 0, true), "pin past the end");
    zui_cliphist_pin(&H, 1, true);
    /* clear */
    CHECK(zui_cliphist_clear(&H, false) == 2, "clear removes the unpinned");
    CHECK(zui_cliphist_count(&H) == 1 && strcmp(at(0), "b22") == 0 && H.bytes == 3,
          "the pinned one stays");
    CHECK(nowhere(&H, "d4444") && nowhere(&H, "a1"), "cleared text is wiped");
    char out[16];
    CHECK(zui_cliphist_current(&H, out, sizeof out) == 0, "clear empties the clipboard too");
    CHECK(zui_cliphist_clear(&H, true) == 1 && zui_cliphist_count(&H) == 0 && H.bytes == 0,
          "and the pinned when asked");
    CHECK(zui_cliphist_clear(0, true) == 0, "NULL");

    /* use: back to the front, and onto the clipboard */
    add("x");
    add("y");
    add("z");
    CHECK(zui_cliphist_use(&H, 2), "use x");
    CHECK(strcmp(at(0), "x") == 0 && strcmp(at(1), "z") == 0, "x in front");
    uint32_t n = zui_cliphist_current(&H, out, sizeof out);
    CHECK(n == 1 && out[0] == 'x', "x is the clipboard");
    CHECK(!zui_cliphist_use(&H, 3) && !zui_cliphist_use(0, 0), "use past the end");
}

static void test_cap(void)
{
    zui_cliphist_init(&H);
    zui_cliphist_set_cap(&H, 20);
    CHECK(add("aaaaa") == ZUI_CLIP_ADDED && add("bbbbb") == ZUI_CLIP_ADDED &&
              add("ccccc") == ZUI_CLIP_ADDED && add("ddddd") == ZUI_CLIP_ADDED,
          "20 bytes fit");
    CHECK(H.bytes == 20 && zui_cliphist_count(&H) == 4, "full");
    CHECK(add("eeeeeeee") == ZUI_CLIP_ADDED, "8 more");
    CHECK(H.bytes <= 20 && zui_cliphist_count(&H) == 3 && strcmp(at(2), "ccccc") == 0,
          "CH4: the two oldest went to make room (%u bytes)", H.bytes);
    CHECK(add("012345678901234567890") == ZUI_CLIP_TOO_BIG, "21 bytes never fit in 20");
    CHECK(zui_cliphist_count(&H) == 3, "and nothing was evicted for it");
    char out[32];
    CHECK(zui_cliphist_current(&H, out, sizeof out) == 21, "still copied to the clipboard");
    /* pinned clips are not evicted: then a new clip may not fit */
    zui_cliphist_pin(&H, 0, true);
    zui_cliphist_pin(&H, 1, true);
    zui_cliphist_pin(&H, 2, true);
    CHECK(H.bytes == 18 && add("fff") == ZUI_CLIP_TOO_BIG, "pinned fill it: no room (%u bytes)",
          H.bytes);
    CHECK(add("ff") == ZUI_CLIP_ADDED && H.bytes == 20, "two bytes still fit");
    zui_cliphist_delete(&H, 0);
    CHECK(zui_cliphist_count(&H) == 3, "the pinned all stay");
    /* a lower cap evicts at once, oldest unpinned first */
    zui_cliphist_init(&H);
    add("1111");
    add("2222");
    add("3333");
    zui_cliphist_pin(&H, 2, true); /* 1111 */
    zui_cliphist_set_cap(&H, 8);
    CHECK(zui_cliphist_count(&H) == 2 && strcmp(at(0), "3333") == 0 && strcmp(at(1), "1111") == 0,
          "lowering the cap drops 2222, keeps the pinned 1111");
    zui_cliphist_set_cap(&H, 0);
    CHECK(zui_cliphist_count(&H) == 1 && strcmp(at(0), "1111") == 0, "cap 0 leaves only pinned");
    zui_cliphist_set_cap(0, 1);

    /* CH1: longer than a clip holds */
    static char big[ZUI_CLIP_TEXT + 10];
    memset(big, 'q', sizeof big);
    zui_cliphist_init(&H);
    CHECK(zui_cliphist_add(&H, big, ZUI_CLIP_TEXT - 1u, ZUI_CLIP_PLAIN, 0) == ZUI_CLIP_ADDED,
          "ZUI_CLIP_TEXT - 1 bytes fit");
    CHECK(zui_cliphist_add(&H, big, ZUI_CLIP_TEXT, ZUI_CLIP_PLAIN, 0) == ZUI_CLIP_TOO_BIG,
          "one more does not");
    static char cur[ZUI_CLIP_TEXT + 10];
    CHECK(zui_cliphist_current(&H, cur, sizeof cur) == ZUI_CLIP_TEXT - 1u,
          "the clipboard keeps what fits");
    /* cut on a character boundary */
    memcpy(big + ZUI_CLIP_TEXT - 2u, "\xE2\x82\xAC", 3);
    zui_cliphist_add(&H, big, ZUI_CLIP_TEXT + 5u, ZUI_CLIP_PLAIN, 0);
    CHECK(zui_cliphist_current(&H, cur, sizeof cur) == ZUI_CLIP_TEXT - 2u,
          "the clipboard is cut before a split character");
    char small[4];
    zui_cliphist_add(&H, "ab\xE2\x82\xAC", 5, ZUI_CLIP_PLAIN, 0);
    CHECK(zui_cliphist_current(&H, small, 4) == 2, "current() cuts whole characters too");
    CHECK(zui_cliphist_current(&H, 0, 4) == 0 && zui_cliphist_current(0, small, 4) == 0, "NULL");
}

static void test_secret(void)
{
    zui_cliphist_init(&H);
    add("public");
    const char *pw = "hunter2-correct-horse";
    CHECK(zui_cliphist_add(&H, pw, (uint32_t) strlen(pw), ZUI_CLIP_PLAIN, ZUI_CLIP_SENSITIVE) ==
              ZUI_CLIP_SKIPPED,
          "CH5: a sensitive clip is skipped");
    CHECK(zui_cliphist_count(&H) == 1 && strcmp(at(0), "public") == 0 && H.skipped == 1,
          "not in the history");
    char out[64];
    uint32_t n = zui_cliphist_current(&H, out, sizeof out);
    CHECK(n == strlen(pw) && memcmp(out, pw, n) == 0 && H.cur_sensitive,
          "but on the clipboard to paste once");
    add("next");
    CHECK(nowhere(&H, pw), "the next copy overwrites it: no trace");
    zui_cliphist_add(&H, pw, (uint32_t) strlen(pw), ZUI_CLIP_PLAIN, ZUI_CLIP_SENSITIVE);
    zui_cliphist_forget(&H);
    CHECK(nowhere(&H, pw) && H.cur_len == 0 && !H.cur_sensitive, "forget wipes it");
    zui_cliphist_add(&H, pw, (uint32_t) strlen(pw), ZUI_CLIP_PLAIN, ZUI_CLIP_SENSITIVE);
    zui_cliphist_clear(&H, false);
    CHECK(nowhere(&H, pw), "clear wipes it");
    /* a sensitive copy of text already in the history does not move it */
    zui_cliphist_init(&H);
    add("same");
    add("other");
    CHECK(zui_cliphist_add(&H, "same", 4, ZUI_CLIP_PLAIN, ZUI_CLIP_SENSITIVE) == ZUI_CLIP_SKIPPED &&
              strcmp(at(0), "other") == 0,
          "a sensitive copy never touches the history");
    zui_cliphist_forget(0);
}

static void test_guess(void)
{
    static const struct {
        const char *s;
        zui_clip_type_t t;
    } g[] = {
        {"https://example.org/a?b=1", ZUI_CLIP_LINK},
        {"zxv+peer://node/7", ZUI_CLIP_LINK},
        {"http://", ZUI_CLIP_PLAIN},
        {"://x", ZUI_CLIP_PLAIN},
        {"see https://x.org", ZUI_CLIP_PLAIN},
        {"/home/me/notes.txt", ZUI_CLIP_PATH},
        {"~/notes.txt", ZUI_CLIP_PATH},
        {"/a path/with space", ZUI_CLIP_PLAIN},
        {"42", ZUI_CLIP_NUMBER},
        {"-3.25", ZUI_CLIP_NUMBER},
        {"1,000,000", ZUI_CLIP_NUMBER},
        {"+7", ZUI_CLIP_NUMBER},
        {"4-2", ZUI_CLIP_PLAIN},
        {".", ZUI_CLIP_PLAIN},
        {"-", ZUI_CLIP_PLAIN},
        {"hello", ZUI_CLIP_PLAIN},
        {"line\nline", ZUI_CLIP_PLAIN},
    };
    for (uint32_t i = 0; i < sizeof g / sizeof g[0]; i++)
        CHECK(zui_clip_guess(g[i].s, (uint32_t) strlen(g[i].s)) == g[i].t, "guess \"%s\" -> %d",
              g[i].s, (int) zui_clip_guess(g[i].s, (uint32_t) strlen(g[i].s)));
    CHECK(zui_clip_guess(0, 3) == ZUI_CLIP_PLAIN && zui_clip_guess("x", 0) == ZUI_CLIP_PLAIN,
          "nothing is plain");
}

/* ---- CH6: the toolkit hook ------------------------------------------------ */

static zui_ui_t UI;
static char field_buf[128], secret_buf[64];
static zui_id_t FIELD, SECRET;
static uint32_t now = 1000;

/* a stand-in platform clipboard */
static char plat[256];
static uint32_t plat_len, plat_sets;
static bool plat_set(void *ctx, const char *s, uint32_t n)
{
    (void) ctx;
    if (n >= sizeof plat) return false;
    memcpy(plat, s, n);
    plat_len = n;
    plat_sets++;
    return true;
}
static uint32_t plat_get(void *ctx, char *out, uint32_t cap)
{
    (void) ctx;
    uint32_t n = plat_len < cap ? plat_len : cap;
    memcpy(out, plat, n);
    return n;
}

static void key(uint16_t k, uint8_t mods)
{
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = ZUI_EV_KEY_DOWN;
    e.key = k;
    e.mods = mods;
    e.ms = ++now;
    zui_ui_event(&UI, &e);
}

static void build(void)
{
    static const zui_screen_t laptop = {1280, 800, 256, ZUI_INPUT_POINTER};
    zui_theme_t th;
    memset(&th, 0, sizeof th);
    th.body.size_px = 16; /* no font: fixed advances */
    th.label.size_px = 12;
    th.title.size_px = 20;
    th.mono.size_px = 16;
    zui_ui_init(&UI, &laptop, &th);
    zui_id_t root = zui_box(&UI, ZUI_NONE, ZUI_COL);
    FIELD = zui_field(&UI, root, field_buf, sizeof field_buf, false);
    SECRET = zui_field(&UI, root, secret_buf, sizeof secret_buf, false);
    zui_w(&UI, SECRET)->u.field.secret = true;
    zui_relayout(&UI);
}

static void set_field(zui_id_t f, const char *s)
{
    zui_widget_t *w = zui_w(&UI, f);
    snprintf(w->u.field.buf, w->u.field.cap, "%s", s);
    zui_field_set(&UI, f, (uint32_t) strlen(s));
}

static void test_hook(void)
{
    build();
    zui_cliphist_init(&H);
    zui_cliphist_attach(&H, &UI);
    CHECK(UI.clip_set == zui_cliphist_clip_set && UI.clip_get == zui_cliphist_clip_get &&
              UI.clip_ctx == &H && H.ui == &UI,
          "attached");
    zui_cliphist_attach(&H, &UI);
    CHECK(H.next_set == 0 && H.next_ctx == 0, "attaching twice does not chain to itself");

    /* a copy in a field lands in the history */
    set_field(FIELD, "hello world");
    zui_focus(&UI, FIELD, true);
    zui_field_select(&UI, FIELD, 0, 5);
    key('c', ZUI_MOD_CMD);
    CHECK(zui_cliphist_count(&H) == 1 && strcmp(at(0), "hello") == 0, "Cmd-C feeds the history");
    CHECK(UI.clip_local_len == 0, "the view kept no copy of its own");
    /* paste reads the current clip */
    zui_field_select(&UI, FIELD, 11, 11);
    key('v', ZUI_MOD_CMD);
    CHECK(strcmp(field_buf, "hello worldhello") == 0, "Cmd-V pastes it: %s", field_buf);
    /* cut */
    zui_field_select(&UI, FIELD, 6, 11);
    key('x', ZUI_MOD_CMD);
    CHECK(strcmp(field_buf, "hello hello") == 0 && strcmp(at(0), "world") == 0 &&
              zui_cliphist_count(&H) == 2,
          "Cmd-X too: %s", field_buf);
    /* a link copied is tagged */
    set_field(FIELD, "https://zxv.example/x");
    zui_field_select(&UI, FIELD, 0, (uint32_t) strlen(field_buf));
    key('c', ZUI_MOD_CMD);
    CHECK(zui_cliphist_get(&H, 0)->type == ZUI_CLIP_LINK, "tagged as a link");
    /* use an older clip, then paste */
    CHECK(zui_cliphist_use(&H, 2), "use hello");
    set_field(FIELD, "");
    key('v', ZUI_MOD_CMD);
    CHECK(strcmp(field_buf, "hello") == 0, "paste gives the clip used: %s", field_buf);

    /* CH5: a secret field */
    set_field(SECRET, "s3cret-pass");
    zui_focus(&UI, SECRET, true);
    zui_field_select(&UI, SECRET, 0, 11);
    uint32_t before = zui_cliphist_count(&H);
    key('c', ZUI_MOD_CMD);
    CHECK(zui_cliphist_count(&H) == before && nowhere(&H, "s3cret-pass"),
          "the toolkit copies nothing from a secret field");
    /* and if something is copied while a secret field has the focus, the
     * history still keeps it out */
    CHECK(zui_cliphist_clip_set(&H, "s3cret-pass", 11), "a copy with a secret field focused");
    CHECK(zui_cliphist_count(&H) == before && H.cur_sensitive, "skipped, held as sensitive");
    char out[32];
    CHECK(zui_cliphist_clip_get(&H, out, sizeof out) == 11, "still pasteable once");
    zui_focus(&UI, FIELD, true);
    CHECK(zui_cliphist_clip_set(&H, "plain", 5) && strcmp(at(0), "plain") == 0 &&
              nowhere(&H, "s3cret-pass"),
          "the next copy keeps history and wipes the secret");
    CHECK(zui_cliphist_clip_set(&H, "", 0), "copying nothing");
    CHECK(zui_cliphist_clip_get(&H, out, sizeof out) == 0, "empties the clipboard");
    CHECK(!zui_cliphist_clip_set(0, "x", 1) && !zui_cliphist_clip_set(&H, 0, 1) &&
              zui_cliphist_clip_get(0, out, 4) == 0 && zui_cliphist_clip_get(&H, 0, 4) == 0,
          "NULL");

    zui_cliphist_detach(&H, &UI);
    CHECK(UI.clip_set == 0 && UI.clip_get == 0 && UI.clip_ctx == 0 && H.ui == 0, "detached");
    zui_cliphist_detach(&H, &UI);

    /* CH6: chained to a platform clipboard */
    build();
    UI.clip_set = plat_set;
    UI.clip_get = plat_get;
    UI.clip_ctx = plat;
    zui_cliphist_init(&H);
    zui_cliphist_attach(&H, &UI);
    CHECK(H.next_set == plat_set && H.next_get == plat_get && H.next_ctx == plat,
          "keeps the platform's");
    set_field(FIELD, "to the platform");
    zui_focus(&UI, FIELD, true);
    zui_field_select(&UI, FIELD, 0, 2);
    plat_sets = 0;
    key('c', ZUI_MOD_CMD);
    CHECK(plat_sets == 1 && plat_len == 2 && memcmp(plat, "to", 2) == 0 && strcmp(at(0), "to") == 0,
          "a copy reaches both");
    /* something copied in another program: paste reads the platform */
    memcpy(plat, "elsewhere", 9);
    plat_len = 9;
    set_field(FIELD, "");
    key('v', ZUI_MOD_CMD);
    CHECK(strcmp(field_buf, "elsewhere") == 0, "paste reads the platform: %s", field_buf);
    CHECK(zui_cliphist_count(&H) == 1, "a paste records nothing");
    add("from history");
    CHECK(zui_cliphist_use(&H, 1) && plat_len == 2 && memcmp(plat, "to", 2) == 0,
          "use hands the clip to the platform");
    zui_cliphist_detach(&H, &UI);
    CHECK(UI.clip_set == plat_set && UI.clip_get == plat_get && UI.clip_ctx == plat,
          "detach gives the platform's back");
    zui_cliphist_attach(0, &UI);
    zui_cliphist_attach(&H, 0);
}

int main(void)
{
    test_ring();
    test_edit();
    test_cap();
    test_secret();
    test_guess();
    test_hook();
    printf("%s test_zui_cliphist: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
