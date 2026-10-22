/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_applib.c — the app library and the app card (zui_applib.h AL1..AL6),
 * on the real pack (argv[1], else assets/icons/apps/zxv-apps.zxap). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zui_applib.h"
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

#define W 1280
#define H 800
static zui_vec_work_t WK;
static uint32_t PX[W * H], PX2[W * H];
static uint32_t CPX[16 * 128 * 128];
static zui_applib_cache_t CC;
static zui_apps_t A;
static zui_applib_t L;

static uint8_t *load(const char *p, uint32_t *n)
{
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    uint8_t *b = malloc((size_t) len);
    if (!b || fread(b, 1, (size_t) len, f) != (size_t) len) {
        fclose(f);
        free(b);
        return 0;
    }
    fclose(f);
    *n = (uint32_t) len;
    return b;
}

static bool ev(zui_ev_type_t t, int32_t x, int32_t y)
{
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = t;
    e.x = x;
    e.y = y;
    e.button = ZUI_BTN_LEFT;
    return zui_applib_event(&L, &e);
}
static bool keyev(uint16_t k, uint8_t mods)
{
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = ZUI_EV_KEY_DOWN;
    e.key = k;
    e.mods = mods;
    return zui_applib_event(&L, &e);
}
static bool typed(const char *t)
{
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = ZUI_EV_TEXT;
    strncpy(e.text, t, ZUI_TEXT_EV_MAX);
    return zui_applib_event(&L, &e);
}
static int32_t mx(zui_rect_t r)
{
    return r.x + r.w / 2;
}
static int32_t my(zui_rect_t r)
{
    return r.y + r.h / 2;
}
static void click(zui_rect_t r)
{
    ev(ZUI_EV_PTR_DOWN, mx(r), my(r));
    ev(ZUI_EV_PTR_UP, mx(r), my(r));
}
static uint32_t hash(const uint32_t *p)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < W * H; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}
static void fresh(zui_applib_cache_t *c)
{
    zui_applib_init(&L, &A, &WK, c, 0, 256u);
    zui_applib_place(&L, (zui_rect_t){0, 0, W, H}, 256u);
}

static void geometry(void)
{
    fresh(0);
    CHECK(L.nshown == 407 && L.focus == 0 && L.hover == -1 && L.layer == 0, "starts on all 407");
    uint32_t c = zui_applib_columns(&L);
    CHECK(c == (uint32_t) ((W - 64 + 8) / (112 + 8)), "columns fill the width (%u)", c);
    zui_rect_t g = zui_applib_grid(&L), t0 = zui_applib_tile(&L, 0), t1 = zui_applib_tile(&L, 1);
    zui_rect_t tc = zui_applib_tile(&L, c);
    CHECK(t0.w == 112 && t0.h == 120 && t1.x == t0.x + 120 && t1.y == t0.y, "a row");
    CHECK(tc.x == t0.x && tc.y == t0.y + 128, "the next row");
    CHECK(t0.y == g.y && t0.x >= g.x &&
              t0.x - g.x == g.x + g.w - (zui_applib_tile(&L, c - 1).x + 112),
          "the grid is centred");
    zui_rect_t f = zui_applib_field(&L), s0 = zui_applib_segment(&L, 0),
               s3 = zui_applib_segment(&L, 3);
    CHECK(f.x + f.w == W - 32 && f.w == 280 && s0.x == 32 && s3.x + s3.w < f.x, "header layout");
    CHECK(zui_applib_segment(&L, 9).x == s3.x, "a layer past 3 is the last segment");
    CHECK(g.y > f.y + f.h, "the grid starts under the header");
    int32_t rows = (int32_t) ((407 + c - 1) / c);
    CHECK(zui_applib_max_scroll(&L) == rows * 128 - 8 - g.h, "max scroll %d",
          zui_applib_max_scroll(&L));
    /* 2x */
    zui_applib_place(&L, (zui_rect_t){0, 0, 2 * W, 2 * H}, 512u);
    CHECK(zui_applib_tile(&L, 0).w == 224 && zui_applib_columns(&L) == c, "2x keeps the columns");
    zui_applib_place(&L, (zui_rect_t){0, 0, 600, 400}, 256u);
    CHECK(zui_applib_columns(&L) == 4 && zui_applib_field(&L).w == (600 - 64) / 2, "narrow");
    zui_applib_place(&L, (zui_rect_t){0, 0, 60, 400}, 256u);
    CHECK(zui_applib_columns(&L) == 1, "never fewer than one column");
}

static void filters(void)
{
    fresh(0);
    zui_rect_t s2 = zui_applib_segment(&L, 2);
    click(s2);
    CHECK(L.layer == 2 && L.nshown == 127 && L.shown[0] == 144, "AL1 layer 2 (%u)", L.nshown);
    ev(ZUI_EV_PTR_DOWN, mx(s2), my(s2));
    ev(ZUI_EV_PTR_UP, mx(zui_applib_segment(&L, 3)), my(s2));
    CHECK(L.layer == 2, "sliding off a segment does nothing");
    keyev(ZUI_KEY_TAB, 0);
    CHECK(L.layer == 3 && L.nshown == 136, "Tab: the next layer");
    keyev(ZUI_KEY_TAB, 0);
    CHECK(L.layer == 0 && L.nshown == 407, "and round to all");
    keyev(ZUI_KEY_TAB, ZUI_MOD_SHIFT);
    CHECK(L.layer == 3, "Shift-Tab goes back");
    zui_applib_set_layer(&L, 7);
    CHECK(L.layer == 0, "a bad layer is all");

    CHECK(typed("quill") && L.nshown >= 1 && !strcmp(L.query, "quill"), "typing searches");
    zui_app_t app;
    zui_apps_get(&A, L.shown[0], &app);
    CHECK(!strcmp(app.name, "Quillon"), "Quillon first");
    L.scroll = 50;
    typed("o");
    CHECK(L.scroll == 0 && L.focus == 0, "a new search starts at the top");
    keyev(ZUI_KEY_BACKSPACE, 0);
    CHECK(!strcmp(L.query, "quill"), "Backspace");
    typed("\x01\x7f");
    CHECK(!strcmp(L.query, "quill"), "controls never search");
    keyev(ZUI_KEY_ESCAPE, 0);
    CHECK(L.qlen == 0 && L.nshown == 407 && L.act == ZUI_APPLIB_NONE, "Escape clears");
    keyev(ZUI_KEY_ESCAPE, 0);
    CHECK(L.act == ZUI_APPLIB_CLOSE, "Escape on nothing closes");
    typed("\xC3\xA9");
    CHECK(L.qlen == 2, "UTF-8 is kept");
    keyev(ZUI_KEY_BACKSPACE, 0);
    CHECK(L.qlen == 0, "Backspace takes a whole character");
    keyev(ZUI_KEY_BACKSPACE, 0);
    CHECK(L.qlen == 0, "and stops at empty");
    char big[80];
    memset(big, 'a', 79);
    big[79] = 0;
    zui_applib_set_query(&L, big);
    CHECK(L.qlen == ZUI_APPLIB_QUERY - 1, "the search is bounded");
    zui_applib_set_query(&L, "money");
    uint32_t money = L.nshown;
    CHECK(money >= 12, "a category matches (%u)", money);
    zui_applib_set_layer(&L, 2);
    CHECK(L.nshown < money && !strcmp(L.query, "money"), "search and layer together");
    CHECK(!keyev('k', ZUI_MOD_CMD), "shortcuts are not search");
    CHECK(!keyev(ZUI_KEY_BACKSPACE, ZUI_MOD_CMD) && !strcmp(L.query, "money"),
          "Cmd-Backspace is the owner's, not the search's");
}

static void choosing(void)
{
    fresh(0);
    zui_rect_t t5 = zui_applib_tile(&L, 5);
    CHECK(ev(ZUI_EV_PTR_MOVE, mx(t5), my(t5)) && L.hover == 5, "hover");
    ev(ZUI_EV_PTR_MOVE, t5.x + t5.w + 2, my(t5));
    CHECK(L.hover == -1, "the gap is no tile");
    ev(ZUI_EV_PTR_DOWN, mx(t5), my(t5));
    CHECK(L.focus == 5 && L.act == ZUI_APPLIB_NONE, "press focuses");
    ev(ZUI_EV_PTR_UP, mx(t5), my(t5));
    CHECK(L.act == ZUI_APPLIB_OPEN && L.act_app == 5, "AL3 release opens");
    ev(ZUI_EV_PTR_DOWN, mx(t5), my(t5));
    ev(ZUI_EV_PTR_UP, mx(zui_applib_tile(&L, 6)), my(t5));
    CHECK(L.act == ZUI_APPLIB_NONE, "sliding off cancels");
    CHECK(!ev(ZUI_EV_PTR_MOVE, W + 5, 5) && L.hover == -1, "outside");
    zui_applib_set_layer(&L, 3);
    click(zui_applib_tile(&L, 0));
    CHECK(L.act == ZUI_APPLIB_OPEN && L.act_app == 271, "the catalogue index, not the position");
    keyev(ZUI_KEY_RIGHT, 0);
    keyev(ZUI_KEY_ENTER, 0);
    CHECK(L.act_app == 272, "Enter opens the focused tile");

    fresh(0);
    uint32_t c = zui_applib_columns(&L);
    keyev(ZUI_KEY_LEFT, 0);
    CHECK(L.focus == 0, "no tile before the first");
    keyev(ZUI_KEY_DOWN, 0);
    CHECK(L.focus == (int32_t) c, "Down is a row");
    keyev(ZUI_KEY_UP, 0);
    CHECK(L.focus == 0, "Up");
    keyev(ZUI_KEY_END, 0);
    CHECK(L.focus == 406 && L.scroll == zui_applib_max_scroll(&L), "End reveals the last");
    keyev(ZUI_KEY_RIGHT, 0);
    CHECK(L.focus == 406, "no tile after the last");
    keyev(ZUI_KEY_HOME, 0);
    CHECK(L.focus == 0 && L.scroll == 0, "Home");
    keyev(ZUI_KEY_PAGE_DOWN, 0);
    zui_rect_t g = zui_applib_grid(&L), tf = zui_applib_tile(&L, (uint32_t) L.focus);
    CHECK(L.focus > (int32_t) c && tf.y >= g.y && tf.y + tf.h <= g.y + g.h,
          "Page Down keeps the focus in view");
    keyev(ZUI_KEY_PAGE_UP, 0);
    CHECK(L.focus == 0, "Page Up");

    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = ZUI_EV_SCROLL;
    e.x = W / 2;
    e.y = H / 2;
    e.dy = 300;
    CHECK(zui_applib_event(&L, &e) && L.scroll == 300, "the wheel scrolls");
    e.dy = 100000;
    zui_applib_event(&L, &e);
    CHECK(L.scroll == zui_applib_max_scroll(&L), "clamped at the end");
    e.dy = -200000;
    zui_applib_event(&L, &e);
    CHECK(L.scroll == 0, "and at the top");
    e.x = W + 10;
    CHECK(!zui_applib_event(&L, &e), "the wheel outside");
    L.scroll = 260;
    zui_rect_t t = zui_applib_tile(&L, c * 3);
    click(t);
    CHECK(L.act_app == (int32_t) (c * 3), "a click on a scrolled grid");
    zui_applib_place(&L, (zui_rect_t){0, 0, W, H * 10}, 256u);
    CHECK(L.scroll <= zui_applib_max_scroll(&L), "a taller screen pulls the scroll back");
}

static void empty_state(void)
{
    fresh(0);
    typed("qqqz");
    CHECK(L.nshown == 0 && L.focus == -1, "AL4 nothing matches");
    keyev(ZUI_KEY_ENTER, 0);
    CHECK(L.act == ZUI_APPLIB_NONE, "Enter on nothing");
    keyev(ZUI_KEY_DOWN, 0);
    CHECK(L.focus == -1, "keys on nothing");
    zui_rect_t g = zui_applib_grid(&L);
    zui_rect_t b = {g.x + (g.w - 140) / 2, g.y + 150, 140, 32};
    click(b);
    CHECK(L.qlen == 0 && L.nshown == 407, "the Clear search button");
    typed("zz");
    click(zui_applib_clear_btn(&L));
    CHECK(L.qlen == 0, "the field's clear button");
    zui_surface_t s;
    zui_surface_init(&s, PX, W, H, W);
    typed("qqqz");
    zui_applib_draw(&L, &s);
    uint32_t gold = 0;
    for (int32_t y = g.y; y < g.y + 200; y++)
        for (int32_t x = g.x; x < g.x + g.w; x++)
            if (PX[y * W + x] == ZUI_GOLD) gold++;
    CHECK(gold > 500, "the empty state draws its mark and button (%u)", gold);
}

static void drawing(void)
{
    zui_surface_t s;
    zui_surface_init(&s, PX, W, H, W);
    fresh(0);
    L.focus = -1;
    zui_applib_draw(&L, &s);
    uint32_t h1 = hash(PX);
    zui_applib_cache_init(&CC, CPX, 16, 128);
    fresh(&CC);
    L.focus = -1;
    zui_surface_init(&s, PX2, W, H, W);
    zui_applib_draw(&L, &s);
    CHECK(hash(PX2) == h1, "AL6 the cache draws the same pixels");
    zui_applib_draw(&L, &s);
    CHECK(hash(PX2) == h1, "and again from the cache");
    CHECK(CC.tag[0] >= 0 && CC.tag_size[0] == 64, "the cache holds icons");
    zui_applib_cache_init(&CC, CPX, 16, 32);
    fresh(&CC);
    L.focus = -1;
    zui_applib_draw(&L, &s);
    CHECK(hash(PX2) == h1 && CC.tag[0] == -1, "a cache too small is not used");
    zui_applib_cache_init(&CC, CPX, 200, 128);
    CHECK(CC.slots == 64, "slots are capped");

    /* the focus ring is gold; the grid stays inside the box */
    fresh(0);
    zui_surface_init(&s, PX, W, H, W);
    for (uint32_t i = 0; i < W * H; i++) PX[i] = 0x123456u;
    zui_applib_place(&L, (zui_rect_t){100, 50, 900, 600}, 256u);
    zui_applib_draw(&L, &s);
    CHECK(PX[10 * W + 10] == 0x123456u && PX[(H - 5) * W + W - 5] == 0x123456u,
          "nothing outside the box");
    zui_rect_t t0 = zui_applib_tile(&L, 0);
    CHECK(PX[(t0.y + t0.h / 2) * W + t0.x] == ZUI_GOLD, "the focused tile's ring");
    CHECK(s.clip.x == 0 && s.clip.w == W && s.clip.h == H, "the clip is given back");
    /* the scroll indicator shows on a tall grid only */
    int32_t ix = 100 + 900 - 16 - 2;
    zui_rect_t g = zui_applib_grid(&L);
    CHECK(PX[(g.y + 4) * W + ix] == ZUI_MUTED, "the indicator's thumb at the top");
    /* a very long grid keeps a thumb big enough to see (24 pt) */
    zui_applib_place(&L, (zui_rect_t){0, 0, 200, 300}, 256u);
    for (uint32_t i = 0; i < W * H; i++) PX[i] = 0;
    zui_applib_draw(&L, &s);
    int32_t thumb = 0, tx = 200 - 16; /* the bar's middle column */
    for (int32_t y = 0; y < 300; y++)
        if (PX[y * W + tx] == ZUI_MUTED) thumb++;
    CHECK(thumb >= 22 && thumb <= 24, "the thumb's least size (%d)", thumb);
    zui_applib_place(&L, (zui_rect_t){100, 50, 900, 600}, 256u);
    zui_applib_set_layer(&L, 1);
    zui_applib_set_query(&L, "quillon");
    zui_applib_draw(&L, &s);
    CHECK(PX[(g.y + 4) * W + ix] == ZUI_VOID, "no indicator when it all fits");

    /* the card */
    zui_app_t app;
    zui_apps_get(&A, 150, &app);
    zui_rect_t box = {0, 0, 700, 600};
    CHECK(zui_applib_card_open(256u, false, box).w == 0, "no Open when not built");
    zui_rect_t ob = zui_applib_card_open(256u, true, box);
    CHECK(ob.w == 120 && ob.h == 32 && ob.x == 24 + 96 + 24, "Open sits beside the icon");
    zui_surface_init(&s, PX, W, H, W);
    zui_applib_card(&s, &WK, &L.f, 256u, &app, true, box);
    CHECK(PX[(ob.y + 2) * W + ob.x + 4] == ZUI_GOLD, "the Open button is the gold one");
    CHECK(PX[(box.y + 60) * W + box.x + 70] != ZUI_VOID, "the icon is drawn");
    uint32_t hb = hash(PX);
    zui_applib_card(&s, &WK, &L.f, 256u, &app, false, box);
    CHECK(hash(PX) != hb && PX[(ob.y + 2) * W + ob.x + 4] != ZUI_GOLD, "not built: no gold button");

    /* the card's height ends under its last line, with the S5 margin;
     * measured with the real face, since the sections are text */
    static uint8_t fdata[4u << 20];
    static ttf_font_t font;
    static zui_glyph_cache_t gc;
    const char *fp[] = {"../assets/fonts/ascw-golden/ASCW-Golden-Latin2.ttf",
                        "assets/fonts/ascw-golden/ASCW-Golden-Latin2.ttf"};
    bool have_font = false;
    for (int i = 0; i < 2 && !have_font; i++) {
        FILE *ff = fopen(fp[i], "rb");
        if (!ff) continue;
        size_t fn = fread(fdata, 1, sizeof fdata, ff);
        fclose(ff);
        have_font = ttf_parse(&font, fdata, (uint32_t) fn);
    }
    CHECK(have_font, "the ASCW Golden face for the card's text");
    if (!have_font) return;
    zui_glyph_cache_init(&gc);
    zui_text_t body = {&font, &gc, 15, false}, cap = {&font, &gc, 12, false};
    zui_applib_faces_t keep = L.f;
    L.f.body = &body;
    L.f.caption = &cap;
    int32_t ch = zui_applib_card_height(&L.f, 256u, &app, box.w);
    zui_rect_t tall = {0, 0, box.w, H};
    for (uint32_t i = 0; i < W * H; i++) PX[i] = 0;
    zui_applib_card(&s, &WK, &L.f, 256u, &app, false, tall);
    int32_t last = -1; /* the lowest row with ink */
    for (int32_t y = 0; y < H; y++)
        for (int32_t x = 0; x < box.w; x++)
            if (PX[y * W + x] != ZUI_VOID) last = y;
    CHECK(ch > 24 + 96 + 24 && ch < 600 && last < ch && last >= ch - 24 - 28,
          "the card fits its content (height %d, last ink row %d)", ch, last);
    CHECK(zui_applib_card_height(&L.f, 256u, &app, 300) > ch,
          "a narrower card wraps onto more lines");
    L.f = keep;

    /* by role (zui_palette.h C6): the dark set is the tokens, light its own */
    fresh(0);
    zui_applib_place(&L, (zui_rect_t){100, 50, 900, 600}, 256u);
    zui_applib_draw(&L, &s);
    uint32_t hd = hash(PX);
    L.f.colors = zui_scheme_colors(ZUI_SCHEME_DARK);
    zui_applib_draw(&L, &s);
    CHECK(hash(PX) == hd, "the dark set draws the same pixels as none");
    const zui_colors_t *lt = zui_scheme_colors(ZUI_SCHEME_HIGH_CONTRAST);
    L.f.colors = lt;
    zui_applib_draw(&L, &s);
    CHECK(PX[(50 + 5) * W + 100 + 5] == lt->c[ZUI_C_VOID], "the high-contrast ground");
    CHECK(PX[(t0.y + t0.h / 2) * W + t0.x] == lt->c[ZUI_C_GOLD], "its gold focus ring");
    CHECK(PX[(g.y + 4) * W + ix] == lt->c[ZUI_C_MUTED], "its indicator");
    zui_applib_card(&s, &WK, &L.f, 256u, &app, true, box);
    CHECK(PX[(ob.y + 2) * W + ob.x + 4] == lt->c[ZUI_C_GOLD] &&
              PX[(box.y + box.h - 4) * W + box.x + 4] == lt->c[ZUI_C_VOID],
          "the card paints by role too");
}

int main(int argc, char **argv)
{
    const char *try[] = {argc > 1 ? argv[1] : "assets/icons/apps/zxv-apps.zxap",
                         "../assets/icons/apps/zxv-apps.zxap", "assets/icons/apps/zxv-apps.zxap"};
    uint8_t *p = 0;
    uint32_t n = 0;
    for (int i = 0; i < 3 && !p; i++) p = load(try[i], &n);
    if (!p || zui_apps_bind(&A, p, n) != 0) {
        printf("[FAIL] test_zui_applib: no pack (run tools/zui/gen_appicons.py)\n");
        return 1;
    }
    geometry();
    filters();
    choosing();
    empty_state();
    drawing();
    free(p);
    if (fails) {
        printf("test_zui_applib: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("[PASS] test_zui_applib: %d/%d checks\n", checks, checks);
    return 0;
}
