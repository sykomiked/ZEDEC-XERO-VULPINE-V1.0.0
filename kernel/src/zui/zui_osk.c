/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_osk.c — the on-screen keyboard (zui_osk.h). */
#include "zui_osk.h"
#include "zui_tokens.h"

#define OSK_REPEAT_MS 50u
#define Q_KEY         4u /* a letter key, quarter units          */
#define Q_WIDE        6u /* Shift, Backspace, page keys          */
#define Q_ENTER       8u
#define Q_SPACE_MIN   16u
#define Q_PAD         8u /* keypad keys on the numbers page      */
#define Q_GRID_MIN    40u

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static void copy(void *d, const void *s, uint32_t n)
{
    uint8_t *a = (uint8_t *) d;
    const uint8_t *b = (const uint8_t *) s;
    for (uint32_t i = 0; i < n; i++) a[i] = b[i];
}

static void scopy(char *d, uint32_t cap, const char *s)
{
    uint32_t n = zui_utf8_fit(s, slen(s), cap - 1);
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    d[n] = 0;
}

static void zero(void *p, uint32_t n)
{
    uint8_t *b = (uint8_t *) p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

/* ---- O1, O2: building a page --------------------------------------------------------- */

typedef struct {
    zui_osk_t *o;
    uint32_t row, in_row;
} builder_t;

static zui_osk_key_t *add(builder_t *b, uint8_t kind, uint16_t qw, uint16_t key, const char *label,
                          const char *text)
{
    zui_osk_t *o = b->o;
    if (o->nkeys >= ZUI_OSK_KEYS_MAX || b->in_row >= ZUI_OSK_ROW_KEYS) return 0;
    zui_osk_key_t *k = &o->keys[o->nkeys++];
    zero(k, (uint32_t) sizeof *k);
    k->kind = kind;
    k->row = (uint8_t) b->row;
    k->pos = -1;
    k->qw = qw;
    k->key = key;
    scopy(k->label, sizeof k->label, label);
    scopy(k->text, sizeof k->text, text);
    b->in_row++;
    return k;
}

static void next_row(builder_t *b)
{
    b->row++;
    b->in_row = 0;
}

static void add_ascii(builder_t *b, const char *chars, uint16_t qw)
{
    for (const char *c = chars; *c; c++) {
        char t[2] = {*c, 0};
        uint16_t key = (uint16_t) zui_km_lower((uint32_t) (uint8_t) *c);
        add(b, ZUI_OSKK_CHAR, qw, key, t, t);
    }
}

/* A letter key from the layout, base level (capital on the shift page). */
static void add_letter(builder_t *b, int32_t pos, bool upper)
{
    const zui_keymap_t *km = b->o->km;
    uint32_t v = km->key[pos][ZUI_KM_BASE];
    if (upper) v = zui_km_upper(v);
    char t[8];
    uint32_t n = zui_km_utf8_put(t, v);
    t[n] = 0;
    zui_osk_key_t *k = add(b, ZUI_OSKK_CHAR, Q_KEY, zui_keymap_shortcut(km, pos), t, t);
    if (k) k->pos = (int8_t) pos;
}

static bool is_letter_key(const zui_keymap_t *km, int32_t pos)
{
    uint32_t v = km->key[pos][ZUI_KM_BASE];
    return v && !ZUI_KM_IS_DEAD(v) && !ZUI_KM_IS_STR(v) && zui_km_is_letter(v);
}

static void bottom_row(builder_t *b, uint8_t page_to, const char *page_label)
{
    zui_osk_key_t *k = add(b, ZUI_OSKK_PAGE, Q_WIDE, ZUI_KEY_NONE, page_label, "");
    if (k) k->page_to = page_to;
    add_ascii(b, ",", Q_KEY);
    add(b, ZUI_OSKK_SPACE, Q_SPACE_MIN, ZUI_KEY_SPACE, "", " ");
    add_ascii(b, ".", Q_KEY);
    add(b, ZUI_OSKK_ENTER, Q_ENTER, ZUI_KEY_ENTER, "⏎", "");
}

static void build(zui_osk_t *o)
{
    builder_t b = {o, 0, 0};
    o->nkeys = 0;
    bool upper = o->page == ZUI_OSK_SHIFT;
    if (o->page == ZUI_OSK_LETTERS || o->page == ZUI_OSK_SHIFT) {
        const zui_keymap_t *km = o->km;
        if (km) {
            /* O1: top row, then digit-row letters whose shift is not a digit */
            for (int32_t c = 0; c < 16; c++)
                if (is_letter_key(km, ZUI_KP(ZUI_KP_ROW_D, c)))
                    add_letter(&b, ZUI_KP(ZUI_KP_ROW_D, c), upper);
            for (int32_t c = 0; c < 16; c++) {
                int32_t p = ZUI_KP(ZUI_KP_ROW_E, c);
                uint32_t s = km->key[p][ZUI_KM_SHIFT];
                if (is_letter_key(km, p) && !(s >= '0' && s <= '9')) add_letter(&b, p, upper);
            }
            next_row(&b);
            for (int32_t c = 0; c < 16; c++)
                if (is_letter_key(km, ZUI_KP(ZUI_KP_ROW_C, c)))
                    add_letter(&b, ZUI_KP(ZUI_KP_ROW_C, c), upper);
            next_row(&b);
            add(&b, ZUI_OSKK_SHIFT, Q_WIDE, ZUI_KEY_NONE, "⇧", "");
            for (int32_t c = 0; c < 16; c++)
                if (is_letter_key(km, ZUI_KP(ZUI_KP_ROW_B, c)))
                    add_letter(&b, ZUI_KP(ZUI_KP_ROW_B, c), upper);
        } else {
            add_ascii(&b, upper ? "QWERTYUIOP" : "qwertyuiop", Q_KEY);
            next_row(&b);
            add_ascii(&b, upper ? "ASDFGHJKL" : "asdfghjkl", Q_KEY);
            next_row(&b);
            add(&b, ZUI_OSKK_SHIFT, Q_WIDE, ZUI_KEY_NONE, "⇧", "");
            add_ascii(&b, upper ? "ZXCVBNM" : "zxcvbnm", Q_KEY);
        }
        add(&b, ZUI_OSKK_BACKSPACE, Q_WIDE, ZUI_KEY_BACKSPACE, "⌫", "");
        next_row(&b);
        bottom_row(&b, ZUI_OSK_SYMBOLS, "?123");
    } else if (o->page == ZUI_OSK_SYMBOLS) {
        add_ascii(&b, "1234567890", Q_KEY);
        next_row(&b);
        add_ascii(&b, "@#$_&-+()/", Q_KEY);
        next_row(&b);
        zui_osk_key_t *k = add(&b, ZUI_OSKK_PAGE, Q_WIDE, ZUI_KEY_NONE, "123", "");
        if (k) k->page_to = ZUI_OSK_NUMBERS;
        add_ascii(&b, "*\"':;!?", Q_KEY);
        add(&b, ZUI_OSKK_BACKSPACE, Q_WIDE, ZUI_KEY_BACKSPACE, "⌫", "");
        next_row(&b);
        bottom_row(&b, ZUI_OSK_LETTERS, "ABC");
    } else {
        add_ascii(&b, "123", Q_PAD);
        add(&b, ZUI_OSKK_BACKSPACE, Q_PAD, ZUI_KEY_BACKSPACE, "⌫", "");
        next_row(&b);
        add_ascii(&b, "456", Q_PAD);
        add(&b, ZUI_OSKK_SPACE, Q_PAD, ZUI_KEY_SPACE, "", " ");
        next_row(&b);
        add_ascii(&b, "789", Q_PAD);
        add(&b, ZUI_OSKK_ENTER, Q_PAD, ZUI_KEY_ENTER, "⏎", "");
        next_row(&b);
        zui_osk_key_t *k = add(&b, ZUI_OSKK_PAGE, Q_PAD, ZUI_KEY_NONE, "ABC", "");
        if (k) k->page_to = ZUI_OSK_LETTERS;
        add_ascii(&b, "0.", Q_PAD);
        k = add(&b, ZUI_OSKK_PAGE, Q_PAD, ZUI_KEY_NONE, "#+=", "");
        if (k) k->page_to = ZUI_OSK_SYMBOLS;
    }
    o->nrows = b.row + 1;
    if (o->nrows > ZUI_OSK_ROWS) o->nrows = ZUI_OSK_ROWS;
}

/* O1: quarter-unit positions; the widest row sets the grid. */
static void arrange(zui_osk_t *o)
{
    uint16_t grid = o->page == ZUI_OSK_NUMBERS ? 0 : Q_GRID_MIN;
    uint16_t nat[ZUI_OSK_ROWS] = {0, 0, 0, 0};
    for (uint32_t i = 0; i < o->nkeys; i++) nat[o->keys[i].row] += o->keys[i].qw;
    for (uint32_t r = 0; r < o->nrows; r++)
        if (nat[r] > grid) grid = nat[r];
    o->grid_q = grid;
    for (uint32_t r = 0; r < o->nrows; r++) {
        uint16_t extra = (uint16_t) (grid - nat[r]);
        int32_t space = -1, shift = -1, bksp = -1;
        for (uint32_t i = 0; i < o->nkeys; i++) {
            if (o->keys[i].row != r) continue;
            if (o->keys[i].kind == ZUI_OSKK_SPACE) space = (int32_t) i;
            if (o->keys[i].kind == ZUI_OSKK_SHIFT) shift = (int32_t) i;
            if (o->keys[i].kind == ZUI_OSKK_BACKSPACE) bksp = (int32_t) i;
        }
        uint16_t x = 0;
        if (space >= 0) {
            o->keys[space].qw = (uint16_t) (o->keys[space].qw + extra);
        } else if (shift >= 0 && bksp >= 0) {
            o->keys[shift].qw = (uint16_t) (o->keys[shift].qw + extra / 2);
            o->keys[bksp].qw = (uint16_t) (o->keys[bksp].qw + (extra - extra / 2));
        } else {
            x = (uint16_t) (extra / 2);
        }
        for (uint32_t i = 0; i < o->nkeys; i++) {
            if (o->keys[i].row != r) continue;
            o->keys[i].qx = x;
            x = (uint16_t) (x + o->keys[i].qw);
        }
    }
}

static int32_t px_of(const zui_osk_t *o, uint32_t pt)
{
    return (int32_t) (((uint64_t) pt * o->scale_q8 + 128u) >> 8);
}

static int32_t qx_px(const zui_osk_t *o, uint32_t q)
{
    if (o->grid_q == 0) return o->rect.x;
    return o->rect.x + (int32_t) (((int64_t) q * o->rect.w) / o->grid_q);
}

/* O3: pixel rects and slots. */
static void place(zui_osk_t *o)
{
    o->touch_px = px_of(o, ZUI_TOUCH_MIN);
    if (o->touch_px < 1) o->touch_px = 1;
    int32_t rows = (int32_t) (o->nrows ? o->nrows : 1);
    o->row_h = o->rect.h / rows;
    if (o->row_h < o->touch_px) o->row_h = o->touch_px;
    o->top = o->rect.y + o->rect.h - rows * o->row_h;
    int32_t gap = px_of(o, 3);
    if (gap < 1) gap = 1;
    o->cramped = false;
    for (uint32_t i = 0; i < o->nkeys; i++) {
        zui_osk_key_t *k = &o->keys[i];
        int32_t x0 = qx_px(o, k->qx), x1 = qx_px(o, (uint32_t) (k->qx + k->qw));
        int32_t y0 = o->top + (int32_t) k->row * o->row_h;
        k->r.x = x0 + gap;
        k->r.y = y0 + gap;
        k->r.w = x1 - x0 - 2 * gap;
        k->r.h = o->row_h - 2 * gap;
        if (k->r.w < 1) k->r.w = 1;
        if (k->r.h < 1) k->r.h = 1;
        k->hit.x = x0;
        k->hit.y = y0;
        k->hit.w = x1 - x0;
        k->hit.h = o->row_h;
        if (k->hit.w < o->touch_px) o->cramped = true;
    }
    /* the row ends belong to the end keys */
    for (uint32_t r = 0; r < o->nrows; r++) {
        int32_t first = -1, last = -1;
        for (uint32_t i = 0; i < o->nkeys; i++) {
            if (o->keys[i].row != r) continue;
            if (first < 0) first = (int32_t) i;
            last = (int32_t) i;
        }
        if (first < 0) continue;
        zui_osk_key_t *f = &o->keys[first], *l = &o->keys[last];
        f->hit.w += f->hit.x - o->rect.x;
        f->hit.x = o->rect.x;
        l->hit.w = o->rect.x + o->rect.w - l->hit.x;
    }
}

static void rebuild(zui_osk_t *o)
{
    build(o);
    arrange(o);
    place(o);
    o->pressed = -1;
    o->repeating = false;
}

void zui_osk_init(zui_osk_t *o, const zui_keymap_t *km)
{
    zero(o, (uint32_t) sizeof *o);
    o->km = km;
    o->page = ZUI_OSK_LETTERS;
    o->pressed = -1;
    o->scale_q8 = 256;
    o->repeat_delay_ms = ZUI_LONGPRESS_MS;
    o->repeat_ms = OSK_REPEAT_MS;
    rebuild(o);
}

void zui_osk_set_keymap(zui_osk_t *o, const zui_keymap_t *km)
{
    o->km = km;
    rebuild(o);
}

void zui_osk_set_page(zui_osk_t *o, uint8_t page)
{
    if (page >= ZUI_OSK_PAGES) return;
    o->page = page;
    o->caps_lock = false;
    rebuild(o);
}

void zui_osk_layout(zui_osk_t *o, zui_rect_t rect, uint32_t scale_q8)
{
    o->rect = rect;
    o->scale_q8 = scale_q8 ? scale_q8 : 256;
    place(o);
}

int32_t zui_osk_min_height(const zui_osk_t *o, uint32_t scale_q8)
{
    int32_t t = (int32_t) (((uint64_t) ZUI_TOUCH_MIN * (scale_q8 ? scale_q8 : 256) + 128u) >> 8);
    return t * (int32_t) o->nrows;
}

static bool in(zui_rect_t r, int32_t x, int32_t y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

int32_t zui_osk_hit(const zui_osk_t *o, int32_t x, int32_t y)
{
    for (uint32_t i = 0; i < o->nkeys; i++)
        if (in(o->keys[i].hit, x, y)) return (int32_t) i;
    return -1;
}

/* ---- O4..O6: events ------------------------------------------------------------------- */

typedef struct {
    zui_event_t *out;
    uint32_t cap, n;
} sink_t;

static void emit(sink_t *s, uint8_t type, uint16_t key, uint8_t mods, bool repeat, uint32_t ms,
                 const char *text)
{
    if (s->n >= s->cap) return;
    zui_event_t *e = &s->out[s->n++];
    zero(e, (uint32_t) sizeof *e);
    e->type = type;
    e->key = key;
    e->mods = mods;
    e->repeat = repeat ? 1 : 0;
    e->ms = ms;
    if (text) scopy(e->text, sizeof e->text, text);
}

static bool repeatable(const zui_osk_key_t *k)
{
    return k->kind == ZUI_OSKK_CHAR || k->kind == ZUI_OSKK_SPACE || k->kind == ZUI_OSKK_BACKSPACE;
}

static uint8_t key_mods(const zui_osk_t *o, const zui_osk_key_t *k)
{
    return (o->page == ZUI_OSK_SHIFT && k->kind == ZUI_OSKK_CHAR) ? ZUI_MOD_SHIFT : 0;
}

static void send_down(const zui_osk_t *o, sink_t *s, const zui_osk_key_t *k, bool repeat,
                      uint32_t ms)
{
    emit(s, ZUI_EV_KEY_DOWN, k->key, key_mods(o, k), repeat, ms, 0);
    if (k->text[0] && (k->kind == ZUI_OSKK_CHAR || k->kind == ZUI_OSKK_SPACE))
        emit(s, ZUI_EV_TEXT, 0, key_mods(o, k), false, ms, k->text);
}

static void after_char(zui_osk_t *o)
{
    if (o->page == ZUI_OSK_SHIFT && !o->caps_lock) {
        o->page = ZUI_OSK_LETTERS;
        rebuild(o);
    }
}

/* O2, O4: the key under the finger is let go. */
static void release(zui_osk_t *o, sink_t *s, uint32_t ms)
{
    int32_t i = o->pressed;
    o->pressed = -1;
    if (i < 0) return;
    zui_osk_key_t k;
    copy(&k, &o->keys[i], (uint32_t) sizeof k);
    if (o->repeating) {
        o->repeating = false;
        emit(s, ZUI_EV_KEY_UP, k.key, key_mods(o, &k), false, ms, 0);
        if (k.kind == ZUI_OSKK_CHAR) after_char(o);
        return;
    }
    switch (k.kind) {
    case ZUI_OSKK_SHIFT:
        if (o->page == ZUI_OSK_LETTERS) {
            o->page = ZUI_OSK_SHIFT;
            o->caps_lock = false;
            o->shift_tapped = true;
            o->shift_ms = ms;
        } else if (!o->caps_lock && o->shift_tapped && ms - o->shift_ms <= ZUI_DOUBLE_MS) {
            o->caps_lock = true;
            o->shift_tapped = false;
        } else {
            o->page = ZUI_OSK_LETTERS;
            o->caps_lock = false;
            o->shift_tapped = false;
        }
        rebuild(o);
        return;
    case ZUI_OSKK_PAGE:
        o->page = k.page_to;
        o->caps_lock = false;
        rebuild(o);
        return;
    default:
        send_down(o, s, &k, false, ms);
        emit(s, ZUI_EV_KEY_UP, k.key, key_mods(o, &k), false, ms, 0);
        if (k.kind == ZUI_OSKK_CHAR) after_char(o);
        return;
    }
}

/* O5: repeats that are due by ms. Each needs two slots. */
static void repeat_due(zui_osk_t *o, sink_t *s, uint32_t ms)
{
    if (o->pressed < 0) return;
    const zui_osk_key_t *k = &o->keys[o->pressed];
    if (!repeatable(k)) return;
    while ((int32_t) (ms - o->next_ms) >= 0 && s->n + 2 <= s->cap) {
        send_down(o, s, k, o->repeating, o->next_ms);
        o->repeating = true;
        o->next_ms += o->repeat_ms ? o->repeat_ms : 1u;
    }
}

static void press(zui_osk_t *o, int32_t k, uint32_t ms)
{
    o->pressed = k;
    o->repeating = false;
    o->down_ms = ms;
    o->next_ms = ms + o->repeat_delay_ms;
}

uint32_t zui_osk_feed(zui_osk_t *o, const zui_event_t *e, zui_event_t *out, uint32_t cap)
{
    sink_t s = {out, cap, 0};
    switch (e->type) {
    case ZUI_EV_TOUCH_BEGIN: {
        o->touch_seen = true;
        int32_t k = zui_osk_hit(o, e->x, e->y);
        if (k < 0) break;
        if (o->pressed >= 0) {
            /* O6: a new finger sends the held key first; that may turn the
             * page (shift is one capital), so look again */
            repeat_due(o, &s, e->ms);
            release(o, &s, e->ms);
            k = zui_osk_hit(o, e->x, e->y);
            if (k < 0) break;
        }
        press(o, k, e->ms);
        o->by_touch = true;
        o->touch_id = e->touch_id;
        break;
    }
    case ZUI_EV_TOUCH_MOVE:
        if (o->pressed >= 0 && o->by_touch && e->touch_id == o->touch_id) {
            repeat_due(o, &s, e->ms);
            int32_t k = zui_osk_hit(o, e->x, e->y);
            if (!o->repeating && k >= 0 && k != o->pressed) press(o, k, e->ms);
        }
        break;
    case ZUI_EV_TOUCH_END:
        if (o->pressed >= 0 && o->by_touch && e->touch_id == o->touch_id) {
            repeat_due(o, &s, e->ms);
            release(o, &s, e->ms);
        }
        break;
    case ZUI_EV_TOUCH_CANCEL:
        if (o->pressed >= 0 && o->by_touch && e->touch_id == o->touch_id) {
            if (o->repeating) {
                const zui_osk_key_t *k = &o->keys[o->pressed];
                emit(&s, ZUI_EV_KEY_UP, k->key, key_mods(o, k), false, e->ms, 0);
            }
            o->pressed = -1;
            o->repeating = false;
        }
        break;
    case ZUI_EV_PTR_DOWN: {
        if (o->touch_seen || e->button != ZUI_BTN_LEFT) break;
        int32_t k = zui_osk_hit(o, e->x, e->y);
        if (k >= 0) {
            press(o, k, e->ms);
            o->by_touch = false;
        }
        break;
    }
    case ZUI_EV_PTR_MOVE:
        if (o->touch_seen || o->pressed < 0 || o->by_touch) break;
        repeat_due(o, &s, e->ms);
        if (!o->repeating) {
            int32_t k = zui_osk_hit(o, e->x, e->y);
            if (k >= 0 && k != o->pressed) press(o, k, e->ms);
        }
        break;
    case ZUI_EV_PTR_UP:
        if (o->touch_seen || o->pressed < 0 || o->by_touch || e->button != ZUI_BTN_LEFT) break;
        repeat_due(o, &s, e->ms);
        release(o, &s, e->ms);
        break;
    case ZUI_EV_TICK:
        repeat_due(o, &s, e->ms);
        break;
    default:
        break;
    }
    return s.n;
}

uint32_t zui_osk_feed_queue(zui_osk_t *o, const zui_event_t *e, zui_queue_t *q)
{
    zui_event_t ev[ZUI_OSK_EV_MAX];
    uint32_t n = zui_osk_feed(o, e, ev, ZUI_OSK_EV_MAX);
    for (uint32_t i = 0; i < n; i++) zui_push(q, &ev[i]);
    return n;
}

/* ---- O7: drawing ---------------------------------------------------------------------- */

static void label(zui_surface_t *s, const zui_text_t *t, zui_rect_t r, uint32_t rgb,
                  const char *txt)
{
    if (!t || !txt[0]) return;
    int32_t w = zui_text_width(t, txt);
    int32_t asc = zui_text_ascent(t), desc = zui_text_descent(t);
    int32_t x = r.x + (r.w - w) / 2;
    int32_t base = r.y + (r.h - (asc - desc)) / 2 + asc;
    if (w > r.w)
        zui_text_fit(s, t, r.x + 2, base, r.w - 4, rgb, txt);
    else
        zui_text(s, t, x, base, rgb, txt);
}

void zui_osk_paint(const zui_osk_t *o, zui_surface_t *s, const zui_text_t *t)
{
    int32_t rows = (int32_t) o->nrows;
    zui_rect_t ground = {o->rect.x, o->top, o->rect.w, rows * o->row_h};
    if (o->rect.y < ground.y) {
        ground.h += ground.y - o->rect.y;
        ground.y = o->rect.y;
    }
    zui_fill(s, ground, ZUI_VOID);
    int32_t rad = px_of(o, ZUI_R_CONTROL);
    for (uint32_t i = 0; i < o->nkeys; i++) {
        const zui_osk_key_t *k = &o->keys[i];
        bool down = (int32_t) i == o->pressed;
        uint32_t fill, ink = ZUI_TEXT;
        if (k->kind == ZUI_OSKK_ENTER) {
            fill = down ? ZUI_GOLD_HI : ZUI_GOLD; /* the one accent */
            ink = ZUI_VOID;
        } else if (down) {
            fill = ZUI_RAISED;
        } else if (k->kind == ZUI_OSKK_CHAR || k->kind == ZUI_OSKK_SPACE) {
            fill = ZUI_PANEL;
        } else if (k->kind == ZUI_OSKK_SHIFT && o->caps_lock) {
            fill = ZUI_RAISED;
        } else {
            fill = ZUI_VOID;
            ink = ZUI_TEXT_2;
        }
        zui_fill_rrect(s, k->r, rad, fill, 255);
        if (k->kind != ZUI_OSKK_ENTER)
            zui_engrave(s, k->r, rad, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 48);
        if (k->kind == ZUI_OSKK_SHIFT && (o->page == ZUI_OSK_SHIFT)) {
            /* shift on: a bar under the arrow; locked: a wider one */
            int32_t bw = o->caps_lock ? k->r.w / 2 : k->r.w / 4;
            zui_hline(s, k->r.x + (k->r.w - bw) / 2, k->r.y + k->r.h - px_of(o, 6), bw,
                      px_of(o, 2) > 0 ? px_of(o, 2) : 1, ZUI_TEXT);
        }
        if (k->kind == ZUI_OSKK_SPACE && o->km && k->qw >= 16) {
            label(s, t, k->r, ZUI_MUTED, o->km->name);
            continue;
        }
        label(s, t, k->r, ink, k->label);
    }
}
