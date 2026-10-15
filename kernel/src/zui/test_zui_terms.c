/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_terms.c — the first-boot agreement screen (zui_terms.h T1..T5).
 * Uses the shipped ASCW Golden Latin font so the text is measured by real
 * advances; run from kernel/ or pass the font path as argv[1]. */
#include <stdio.h>
#include <string.h>

#include "zui_terms.h"
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

#define W 1024
#define H 768

static uint32_t PX[W * H];
static zui_surface_t S;
static terms_reader_t R;
static zui_terms_t T;
static ttf_font_t font;
static uint8_t FONT[256 * 1024];
static zui_glyph_cache_t cache;
static zui_text_t txt;
static zui_screen_t SC;

static zui_event_t ev(uint8_t type)
{
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = type;
    return e;
}

static bool key(uint16_t k, uint8_t mods)
{
    zui_event_t e = ev(ZUI_EV_KEY_DOWN);
    e.key = k;
    e.mods = mods;
    return zui_terms_event(&T, &e);
}

static bool ptr(uint8_t type, int32_t x, int32_t y)
{
    zui_event_t e = ev(type);
    e.x = x;
    e.y = y;
    e.button = ZUI_BTN_LEFT;
    return zui_terms_event(&T, &e);
}

static void click(zui_rect_t r)
{
    ptr(ZUI_EV_PTR_DOWN, r.x + r.w / 2, r.y + r.h / 2);
    ptr(ZUI_EV_PTR_UP, r.x + r.w / 2, r.y + r.h / 2);
}

static uint32_t at(int32_t x, int32_t y)
{
    return PX[y * W + x] & 0xFFFFFF;
}

static zui_rect_t full(void)
{
    zui_rect_t a = {0, 0, W, H};
    return a;
}

static int open_(void)
{
    SC.px_w = W;
    SC.px_h = H;
    SC.scale_q8 = 256;
    return zui_terms_open(&T, &R, terms_builtin(), &txt, &SC, full());
}

static void draw(void)
{
    memset(PX, 0, sizeof PX);
    zui_terms_draw(&S, &T);
}

static void test_open_and_look(void)
{
    const zui_terms_layout_t *L = &T.lay;
    uint32_t i, keyed = 0;
    CHECK(open_() == TERMS_OK, "open");
    CHECK(zui_terms_result(&T) == ZUI_TERMS_OPEN, "starts open");
    CHECK(!terms_can_accept(&R), "agree disabled at first");
    CHECK(L->card.w <= zui_px(&SC, ZUI_TERMS_MAX_PT), "card no wider than max");
    CHECK(L->card.x > 0 && L->card.x + L->card.w < W, "card centred inside");
    CHECK(L->agree.h >= zui_px(&SC, ZUI_TOUCH_MIN), "agree is a full touch target");
    CHECK(L->decline.x + L->decline.w <= L->agree.x, "decline left of agree");
    CHECK(L->text.y + L->text.h <= L->hint.y, "text above the hint");
    CHECK(L->hint.y + L->hint.h <= L->agree.y, "hint above the buttons");
    CHECK(R.rows == (uint32_t) (L->text.h / L->line_h), "rows fill the box");

    draw();
    CHECK(at(1, 1) == ZUI_VOID, "ground is ZUI_VOID: %06x", at(1, 1));
    CHECK(at(L->card.x + L->card.w / 2, L->card.y + L->card.h / 2 - 1) != ZUI_VOID, "card drawn");
    CHECK(at(L->card.x + L->card.w / 2, L->card.y) == ZUI_GOLD, "gold card edge: %06x",
          at(L->card.x + L->card.w / 2, L->card.y));
    CHECK(at(L->agree.x + 2, L->agree.y + L->agree.h / 2) == ZUI_RAISED,
          "disabled agree is raised: %06x", at(L->agree.x + 2, L->agree.y + L->agree.h / 2));
    CHECK(at(L->decline.x + 4, L->decline.y + L->decline.h / 2) == ZUI_PANEL,
          "decline is outlined");

    /* find a [KEY] line, bring it into view and look for the gold bar */
    for (i = 0; i < R.nlines; i++)
        if (R.lines[i].key && R.lines[i].kind != TERMS_LINE_BLANK) {
            keyed = i;
            break;
        }
    CHECK(keyed > 0, "the agreement has [KEY] clauses");
    terms_scroll(&R, (int32_t) keyed, TERMS_VIA_SCROLL);
    draw();
    {
        terms_view_t v = terms_view(&R);
        int32_t row = (int32_t) (keyed - v.first);
        CHECK(row >= 0 && row < (int32_t) v.count, "key line visible");
        CHECK(at(L->text.x + 1, L->text.y + row * L->line_h + L->line_h / 2) == ZUI_GOLD,
              "gold bar beside a [KEY] line: %06x",
              at(L->text.x + 1, L->text.y + row * L->line_h + L->line_h / 2));
    }
    CHECK(!terms_can_accept(&R), "still not at the end");
}

static void test_agree_gate(void)
{
    const zui_terms_layout_t *L = &T.lay;
    open_();
    click(L->agree);
    CHECK(zui_terms_result(&T) == ZUI_TERMS_OPEN, "agree before the end does nothing");
    CHECK(T.focus == ZUI_TERMS_FOCUS_AGREE, "a press moves focus to the button");
    key(ZUI_KEY_TAB, 0);
    CHECK(T.focus == ZUI_TERMS_FOCUS_TEXT, "tab wraps to the text");
    /* Tab to agree, Enter: still nothing */
    key(ZUI_KEY_TAB, 0);
    key(ZUI_KEY_TAB, 0);
    CHECK(T.focus == ZUI_TERMS_FOCUS_AGREE, "tab tab = agree");
    key(ZUI_KEY_ENTER, 0);
    CHECK(zui_terms_result(&T) == ZUI_TERMS_OPEN, "enter on agree before the end does nothing");
    key(ZUI_KEY_TAB, ZUI_MOD_SHIFT);
    key(ZUI_KEY_TAB, ZUI_MOD_SHIFT);
    CHECK(T.focus == ZUI_TERMS_FOCUS_TEXT, "shift-tab back to the text");
    CHECK(key(ZUI_KEY_END, 0), "End used");
    CHECK(terms_can_accept(&R), "End reaches the end");
    CHECK(R.via == TERMS_VIA_KEYBOARD, "recorded via keyboard");
    draw();
    CHECK(at(L->agree.x + 2, L->agree.y + L->agree.h / 2) == ZUI_GOLD,
          "enabled agree is gold: %06x", at(L->agree.x + 2, L->agree.y + L->agree.h / 2));
    /* down on agree, up elsewhere: not a press */
    ptr(ZUI_EV_PTR_DOWN, L->agree.x + 2, L->agree.y + 2);
    ptr(ZUI_EV_PTR_UP, L->card.x + 2, L->card.y + 2);
    CHECK(zui_terms_result(&T) == ZUI_TERMS_OPEN, "press must go down and up on the button");
    key(ZUI_KEY_TAB, 0);
    CHECK(T.focus == ZUI_TERMS_FOCUS_TEXT, "focus back on the text");
    key(ZUI_KEY_TAB, 0);
    key(ZUI_KEY_TAB, 0);
    key(ZUI_KEY_ENTER, 0);
    CHECK(zui_terms_result(&T) == ZUI_TERMS_ACCEPTED, "tab+enter accepts");
    CHECK(!key(ZUI_KEY_HOME, 0), "no input after a result");

    /* the shell records it */
    {
        terms_record_t rec;
        char line[512];
        uint8_t prev[32] = {0};
        CHECK(terms_accept(&R, "seat-1", 1760000000ull, prev, &rec) == TERMS_OK,
              "terms_accept after agree");
        CHECK(terms_record_format(&rec, line, sizeof line) > 0, "record formats");
    }
}

static void test_decline(void)
{
    open_();
    click(T.lay.decline);
    CHECK(zui_terms_result(&T) == ZUI_TERMS_DECLINED, "click decline");
    draw();
    CHECK(terms_view(&R).hint == TERMS_HINT_DECLINED, "decline hint");
    open_();
    key(ZUI_KEY_TAB, 0);
    key(ZUI_KEY_SPACE, 0);
    CHECK(zui_terms_result(&T) == ZUI_TERMS_DECLINED, "space on decline");
}

static void test_scroll_routes(void)
{
    zui_event_t e;
    uint32_t top;
    const zui_terms_layout_t *L = &T.lay;
    open_();
    e = ev(ZUI_EV_SCROLL);
    e.dy = L->line_h - 1;
    zui_terms_event(&T, &e);
    CHECK(R.top == 0, "part of a line is not yet a line");
    e.dy = 1;
    zui_terms_event(&T, &e);
    CHECK(R.top == 1, "two halves are one line: %u", R.top);
    e.dy = 3 * L->line_h;
    zui_terms_event(&T, &e);
    CHECK(R.top == 4, "wheel 3 lines: %u", R.top);
    key(ZUI_KEY_DOWN, 0);
    CHECK(R.top == 5, "down");
    key(ZUI_KEY_UP, 0);
    CHECK(R.top == 4, "up");
    key(ZUI_KEY_PAGE_DOWN, 0);
    CHECK(R.top > 4, "page down");
    top = R.top;
    key(ZUI_KEY_SPACE, 0);
    CHECK(R.top > top, "space pages the text");
    key(ZUI_KEY_HOME, 0);
    CHECK(R.top == 0, "home");

    /* touch drag: finger up moves the text up */
    {
        zui_event_t b = ev(ZUI_EV_TOUCH_BEGIN), m = ev(ZUI_EV_TOUCH_MOVE), u = ev(ZUI_EV_TOUCH_END);
        b.x = m.x = u.x = L->text.x + L->text.w / 2;
        b.y = L->text.y + L->text.h - 5;
        m.y = u.y = b.y - 2 * L->line_h;
        zui_terms_event(&T, &b);
        zui_terms_event(&T, &m);
        zui_terms_event(&T, &u);
        CHECK(R.top == 2, "drag two lines: %u", R.top);
        CHECK(!T.dragging, "drag ends");
    }
    /* scroll bar track below the thumb pages */
    top = R.top;
    ptr(ZUI_EV_PTR_DOWN, L->bar.x + L->bar.w / 2, L->bar.y + L->bar.h - 2);
    ptr(ZUI_EV_PTR_UP, L->bar.x + L->bar.w / 2, L->bar.y + L->bar.h - 2);
    CHECK(R.top == top + R.rows, "track click pages: %u", R.top);
    CHECK(zui_terms_result(&T) == ZUI_TERMS_OPEN, "still open");

    /* wheel all the way reaches the end */
    e = ev(ZUI_EV_SCROLL);
    e.dy = 100000 * L->line_h;
    zui_terms_event(&T, &e);
    CHECK(terms_can_accept(&R), "wheel to the end");
    CHECK(R.via == TERMS_VIA_SCROLL, "via scroll");
    click(L->agree);
    CHECK(zui_terms_result(&T) == ZUI_TERMS_ACCEPTED, "click agree after the end");
}

static void test_spoke(void)
{
    open_();
    zui_terms_spoke(&T, terms_builtin()->len);
    CHECK(terms_can_accept(&R), "screen reader reaches the end");
    CHECK(R.via == TERMS_VIA_READER, "via reader");
}

static void test_resize(void)
{
    zui_event_t e;
    uint32_t nl;
    open_();
    key(ZUI_KEY_END, 0);
    nl = R.nlines;
    /* phone portrait */
    e = ev(ZUI_EV_RESIZE);
    e.x = 390;
    e.y = 720;
    CHECK(zui_terms_event(&T, &e), "resize used");
    CHECK(terms_can_accept(&R), "read state kept across resize");
    CHECK(R.nlines > nl, "narrower = more lines (%u > %u)", R.nlines, nl);
    CHECK(T.lay.card.x >= 0 && T.lay.card.x + T.lay.card.w <= 390, "card fits the phone");
    CHECK(T.lay.decline.w == T.lay.agree.w && T.lay.decline.x < T.lay.agree.x,
          "phone buttons split in halves");
    CHECK(T.lay.agree.x + T.lay.agree.w <= T.lay.card.x + T.lay.card.w, "agree inside card");
    /* text size up (2x) */
    e = ev(ZUI_EV_SCALE);
    e.x = 512;
    zui_terms_event(&T, &e);
    CHECK(T.screen.scale_q8 == 512, "scale applied");
    CHECK(terms_can_accept(&R), "read state kept across scale");
    /* too small keeps the old layout */
    {
        zui_rect_t tiny = {0, 0, 40, 40};
        zui_terms_layout_t before = T.lay;
        CHECK(zui_terms_resize(&T, &T.screen, tiny) != TERMS_OK, "tiny refused");
        CHECK(memcmp(&before, &T.lay, sizeof before) == 0, "old layout kept");
    }
    /* draws at phone size without leaving the surface */
    zui_surface_init(&S, PX, 390, 720, W);
    draw();
    zui_surface_init(&S, PX, W, H, W);
    CHECK(zui_terms_open(&T, &R, terms_builtin(), &txt, &SC, (zui_rect_t){0, 0, 40, 40}) !=
              TERMS_OK,
          "open in a tiny area refused");
}

static void test_args(void)
{
    CHECK(zui_terms_open(NULL, &R, terms_builtin(), &txt, &SC, full()) == TERMS_ERR_ARG, "null t");
    CHECK(zui_terms_open(&T, NULL, terms_builtin(), &txt, &SC, full()) == TERMS_ERR_ARG, "null r");
    CHECK(zui_terms_open(&T, &R, NULL, &txt, &SC, full()) == TERMS_ERR_ARG, "null doc");
    CHECK(zui_terms_open(&T, &R, terms_builtin(), NULL, &SC, full()) == TERMS_ERR_ARG, "null font");
    CHECK(!zui_terms_event(NULL, NULL), "null event");
    zui_terms_draw(NULL, NULL);
    zui_terms_spoke(NULL, 0);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "../assets/fonts/ascw-golden/ASCW-Golden-Latin.ttf";
    FILE *fp = fopen(path, "rb");
    size_t n = fp ? fread(FONT, 1, sizeof FONT, fp) : 0;
    if (fp) fclose(fp);
    if (!n || n == sizeof FONT || !ttf_parse(&font, FONT, (uint32_t) n)) {
        printf("test_zui_terms: cannot load %s\n", path);
        return 1;
    }
    zui_glyph_cache_init(&cache);
    txt.font = &font;
    txt.cache = &cache;
    txt.size_px = 16;
    txt.tabular = false;
    zui_surface_init(&S, PX, W, H, W);
    test_open_and_look();
    test_agree_gate();
    test_decline();
    test_scroll_routes();
    test_spoke();
    test_resize();
    test_args();
    printf("%s test_zui_terms: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
