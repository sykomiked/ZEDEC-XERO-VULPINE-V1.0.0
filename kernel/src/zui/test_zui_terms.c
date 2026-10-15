/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_terms.c — the first-boot agreement screen (zui_terms.h T1..T5).
 * Uses the shipped ASCW Golden Latin font so the text is measured by real
 * advances; run from kernel/ or pass the font path as argv[1]. */
#include <stdio.h>
#include <string.h>

#include "zui_terms.h"
#include "zui_tokens.h"
#include "zui_srgb_lut.h"

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
    CHECK(L->card.w <= zui_px(&SC, ZUI_TERMS_MAX_PT + ZUI_TERMS_LIST_PT) + L->pad,
          "card no wider than max");
    CHECK(L->text.w <= zui_px(&SC, ZUI_TERMS_MAX_PT), "text column no wider than max");
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
    CHECK(T.focus == ZUI_TERMS_FOCUS_CONTENTS, "tab wraps to the contents");
    key(ZUI_KEY_TAB, 0);
    CHECK(T.focus == ZUI_TERMS_FOCUS_TEXT, "then the text");
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

/* ---- T6 contents ---- */

/* Row of entry `e` in the list, scrolling the list with the wheel if needed. */
static int32_t row_of(uint32_t e)
{
    const zui_terms_layout_t *L = &T.lay;
    uint32_t vis = (uint32_t) (L->contents.h / L->entry_h), guard;
    if (e == 0) return 0;
    for (guard = 0; guard < 100; guard++) {
        if (e - 1 >= T.list_top && e - 1 < T.list_top + vis - 1) return (int32_t) (e - T.list_top);
        zui_event_t w = ev(ZUI_EV_SCROLL);
        w.x = L->contents.x + 4;
        w.y = L->contents.y + L->contents.h / 2;
        w.dy = e - 1 < T.list_top ? -10 : 10;
        zui_terms_event(&T, &w);
    }
    return -1;
}

static zui_rect_t row_rect(int32_t row)
{
    zui_rect_t r = {T.lay.contents.x, T.lay.contents.y + row * T.lay.entry_h, T.lay.contents.w,
                    T.lay.entry_h};
    return r;
}

static void test_contents_wide(void)
{
    const zui_terms_layout_t *L = &T.lay;
    uint32_t n, k, key12 = 0, e;
    int32_t row;
    open_();
    n = zui_terms_entries(&T);
    CHECK(n == (uint32_t) R.nsections + 1 && n >= 21, "one entry per section plus the summary: %u",
          n);
    CHECK(L->toggle.w == 0, "wide: no contents button");
    CHECK(L->contents.w > 0 && L->contents.x + L->contents.w < L->text.x, "list beside the text");
    CHECK(L->contents.y == L->text.y, "list level with the text");
    CHECK(zui_terms_current(&T) == 0, "starts in the short version");
    draw();
    CHECK(at(L->contents.x + L->contents.w - 4, L->contents.y + 2) == ZUI_RAISED,
          "current entry lit: %06x", at(L->contents.x + L->contents.w - 4, L->contents.y + 2));

    for (k = 0; k < R.nsections; k++)
        if (R.section_no[k] == 12) key12 = k + 1;
    CHECK(key12 && R.section_key[key12 - 1], "section 12 is a [KEY] section");
    row = row_of(key12);
    CHECK(row > 0, "section 12 reachable in the list");
    draw();
    {
        zui_rect_t r = row_rect(row);
        CHECK(at(r.x + 1, r.y + r.h / 2) == ZUI_GOLD, "gold bar on a [KEY] entry: %06x",
              at(r.x + 1, r.y + r.h / 2));
        click(r);
    }
    CHECK(R.top == R.section_line[key12 - 1], "click jumps to the section: %u", R.top);
    CHECK(zui_terms_current(&T) == key12, "the jumped-to section is current");
    CHECK(T.focus == ZUI_TERMS_FOCUS_CONTENTS, "focus stays in the list on a wide screen");
    CHECK(!terms_can_accept(&R), "a jump into the middle does not unlock I agree");
    CHECK(zui_terms_result(&T) == ZUI_TERMS_OPEN, "still open");

    /* the pinned summary stays at row 0 after the list scrolls */
    CHECK(row_of(n - 1) > 0, "last entry reachable");
    CHECK(T.list_top > 0 || (uint32_t) (L->contents.h / L->entry_h) >= n, "list scrolled");
    draw();
    click(row_rect(0));
    CHECK(R.top == 0, "pinned entry goes back to the short version");

    /* keyboard: Up/Down/Home/End/Enter in the list */
    T.focus = ZUI_TERMS_FOCUS_TEXT;
    key(ZUI_KEY_TAB, ZUI_MOD_SHIFT);
    CHECK(T.focus == ZUI_TERMS_FOCUS_CONTENTS, "shift-tab from the text reaches the list");
    key(ZUI_KEY_DOWN, 0);
    key(ZUI_KEY_DOWN, 0);
    CHECK(T.list_sel == 2, "down moves the selection: %u", T.list_sel);
    key(ZUI_KEY_UP, 0);
    CHECK(T.list_sel == 1, "up");
    key(ZUI_KEY_ENTER, 0);
    CHECK(R.top == R.section_line[0], "enter jumps to section 1");
    key(ZUI_KEY_END, 0);
    CHECK(T.list_sel == n - 1, "end selects the last entry");
    e = T.list_sel;
    key(ZUI_KEY_ENTER, 0);
    CHECK(zui_terms_current(&T) == e || R.top == R.nlines - R.rows, "enter on the last section");
    CHECK(terms_can_accept(&R), "the last section shows the end");
    CHECK(R.via == TERMS_VIA_SECTION, "the end was reached by the section list");
    key(ZUI_KEY_HOME, 0);
    CHECK(T.list_sel == 0, "home selects the summary");
    CHECK(!key(ZUI_KEY_ESCAPE, 0), "escape does nothing on a wide screen");
}

static void test_contents_narrow(void)
{
    zui_screen_t sc = SC;
    zui_rect_t a = {0, 0, 390, 720};
    const zui_terms_layout_t *L = &T.lay;
    uint32_t key13 = 0, k;
    int32_t row;
    open_();
    sc.px_w = 390;
    sc.px_h = 720;
    CHECK(zui_terms_resize(&T, &sc, a) == TERMS_OK, "phone size");
    CHECK(L->toggle.w > 0 && L->toggle.h >= zui_px(&sc, ZUI_TOUCH_MIN),
          "contents button, full target");
    CHECK(L->toggle.y + L->toggle.h <= L->text.y, "button above the text");
    CHECK(L->contents.x == L->text.x && L->contents.y == L->text.y, "list opens over the text");
    click(L->toggle);
    CHECK(T.list_open && T.focus == ZUI_TERMS_FOCUS_CONTENTS, "button opens the list");
    zui_surface_init(&S, PX, 390, 720, W);
    draw();
    for (k = 0; k < R.nsections; k++)
        if (R.section_no[k] == 13) key13 = k + 1;
    row = row_of(key13);
    CHECK(row > 0, "section 13 in the list");
    click(row_rect(row));
    CHECK(!T.list_open, "choosing closes the list");
    CHECK(T.focus == ZUI_TERMS_FOCUS_TEXT, "and returns to the text");
    CHECK(zui_terms_current(&T) == key13, "at section 13");
    /* keyboard: Enter on the button opens, Escape closes */
    T.focus = ZUI_TERMS_FOCUS_CONTENTS;
    key(ZUI_KEY_ENTER, 0);
    CHECK(T.list_open && T.list_sel == key13, "enter opens at the current section");
    key(ZUI_KEY_ESCAPE, 0);
    CHECK(!T.list_open, "escape closes");
    key(ZUI_KEY_ENTER, 0);
    key(ZUI_KEY_TAB, 0);
    CHECK(!T.list_open && T.focus == ZUI_TERMS_FOCUS_TEXT, "tabbing away closes the list");
    /* the text drag does nothing while the list covers it */
    click(L->toggle);
    {
        uint32_t top = R.top;
        zui_event_t b = ev(ZUI_EV_PTR_DOWN), m = ev(ZUI_EV_PTR_MOVE), u = ev(ZUI_EV_PTR_UP);
        b.x = m.x = u.x = L->text.x + L->text.w / 2;
        b.y = L->text.y + L->text.h - 5;
        m.y = u.y = 0;
        zui_terms_event(&T, &b);
        zui_terms_event(&T, &m);
        zui_terms_event(&T, &u);
        CHECK(R.top == top, "no drag under the open list");
    }
    draw();
    zui_surface_init(&S, PX, W, H, W);
    /* back to wide: the list sits beside the text again */
    CHECK(zui_terms_resize(&T, &SC, full()) == TERMS_OK && !T.list_open && L->toggle.w == 0,
          "wide again");
}

/* ---- T7 contrast (WCAG 2.1 relative luminance, integer) ---- */

static uint64_t lum(uint32_t c)
{
    return 2126u * zui_srgb_to_lin[(c >> 16) & 255] + 7152u * zui_srgb_to_lin[(c >> 8) & 255] +
           722u * zui_srgb_to_lin[c & 255];
}

static uint32_t contrast_x100(uint32_t a, uint32_t b)
{
    uint64_t la = lum(a), lb = lum(b), off = 2047500u; /* 0.05 of 4095 x 10000 */
    if (la < lb) {
        uint64_t x = la;
        la = lb;
        lb = x;
    }
    return (uint32_t) (((la + off) * 100u) / (lb + off));
}

static void test_contrast(void)
{
    static const struct {
        uint32_t fg, bg, min;
        const char *what;
    } P[] = {
        {ZUI_TEXT_2, ZUI_PANEL, 450, "body text"},
        {ZUI_TEXT, ZUI_PANEL, 450, "headings, [KEY] lines, Decline label"},
        {ZUI_MUTED, ZUI_PANEL, 450, "hint: read to the end"},
        {ZUI_OK, ZUI_PANEL, 450, "hint: you've reached the end"},
        {ZUI_MUTED, ZUI_VOID, 450, "muted text on the ground"},
        {ZUI_VOID, ZUI_GOLD, 450, "I agree label"},
        {ZUI_MUTED, ZUI_RAISED, 450, "disabled I agree label"},
        {ZUI_TEXT, ZUI_RAISED, 450, "current contents entry"},
        {ZUI_GOLD, ZUI_PANEL, 450, "the short version entry"},
        {ZUI_GOLD, ZUI_RAISED, 450, "the short version entry, current"},
        {ZUI_TEXT_2, ZUI_PANEL, 450, "contents entries"},
        {ZUI_FAINT, ZUI_PANEL, 300, "Decline and Contents outlines"},
        {ZUI_MUTED, ZUI_HAIRLINE, 300, "scroll thumb on its track"},
        {ZUI_GOLD_GLOW, ZUI_PANEL, 300, "focus ring"},
        {ZUI_GOLD, ZUI_PANEL, 300, "[KEY] bar"},
    };
    uint32_t i;
    for (i = 0; i < sizeof P / sizeof P[0]; i++) {
        uint32_t c = contrast_x100(P[i].fg, P[i].bg);
        CHECK(c >= P[i].min, "%s: %u.%02u:1 < %u.%02u:1", P[i].what, c / 100, c % 100,
              P[i].min / 100, P[i].min % 100);
    }
    CHECK(contrast_x100(0xFFFFFF, 0x000000) == 2100, "white on black is 21:1: %u",
          contrast_x100(0xFFFFFF, 0x000000));
    /* the hint and buttons are drawn in these colours */
    open_();
    draw();
    {
        const zui_terms_layout_t *L = &T.lay;
        uint32_t x, y, faint = 0;
        for (x = (uint32_t) L->decline.x; x < (uint32_t) (L->decline.x + L->decline.w); x++)
            if (at((int32_t) x, L->decline.y) == ZUI_FAINT) faint++;
        CHECK(faint > 0, "Decline outline drawn in ZUI_FAINT");
        for (y = 0, x = 0; y < (uint32_t) L->agree.h && !x; y++)
            for (int32_t xx = L->agree.x; xx < L->agree.x + L->agree.w; xx++)
                if (at(xx, L->agree.y + (int32_t) y) == ZUI_MUTED) x = 1;
        CHECK(x, "disabled I agree label in ZUI_MUTED");
    }
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
    test_contents_wide();
    test_contents_narrow();
    test_contrast();
    test_args();
    printf("%s test_zui_terms: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
