/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_login.c — the login screen after the boot vault. See zui_login.h. */
#include "zui_login.h"
#include "zui_tokens.h"

static int32_t S(int32_t sc, int32_t pt)
{
    return (pt * sc + 128) / 256;
}

typedef struct {
    int32_t mid, ring_cy, ring_r, name_base;
    zui_rect_t mark, field, enter, msg;
    int32_t row_cy, row_r, step;
    int32_t left, width;
    bool has_field;
} lay_t;

static lay_t layout(zui_rect_t a, int32_t sc, const zui_login_t *l)
{
    lay_t L;
    if (sc <= 0) sc = 256;
    L.mid = a.x + a.w / 2;
    L.left = a.x;
    L.width = a.w;
    int32_t m = S(sc, 96);
    L.mark = (zui_rect_t){L.mid - m / 2, a.y + S(sc, 28), m, m};
    /* the row sits at the bottom; the person block is centred in what is left */
    L.row_r = S(sc, 32);
    L.step = S(sc, 112);
    L.row_cy = a.y + a.h - S(sc, 30) - S(sc, 24) - L.row_r;
    int32_t top = L.mark.y + L.mark.h + S(sc, 12), bottom = L.row_cy - L.row_r - S(sc, 16);
    int32_t fixed = S(sc, 46) + S(sc, 26) + S(sc, 48) + S(sc, 12) + S(sc, 22);
    L.ring_r = (bottom - top - fixed) / 2;
    if (L.ring_r > a.h * 15 / 100) L.ring_r = a.h * 15 / 100;
    if (L.ring_r > S(sc, 96)) L.ring_r = S(sc, 96);
    if (L.ring_r < S(sc, 20)) L.ring_r = S(sc, 20);
    int32_t block = 2 * L.ring_r + fixed;
    L.ring_cy = top + (bottom - top - block) / 2 + L.ring_r;
    if (L.ring_cy < top + L.ring_r) L.ring_cy = top + L.ring_r;
    L.name_base = L.ring_cy + L.ring_r + S(sc, 46);
    uint8_t lock = (l && l->n && l->chosen < l->n) ? l->people[l->chosen].lock : ZUI_LOCK_NONE;
    L.has_field = lock == ZUI_LOCK_PASS;
    int32_t fw = S(sc, 340), fh = S(sc, 48), fy = L.name_base + S(sc, 26);
    if (fw > a.w - 2 * S(sc, 24)) fw = a.w - 2 * S(sc, 24);
    if (L.has_field) {
        L.field = (zui_rect_t){L.mid - fw / 2, fy, fw, fh};
        L.enter = (zui_rect_t){L.field.x + fw - fh + S(sc, 6), fy + S(sc, 6), fh - S(sc, 12),
                               fh - S(sc, 12)};
    } else {
        L.field = (zui_rect_t){L.mid - fw / 2, fy, fw, fh}; /* the hint / button line */
        int32_t bw = S(sc, 168);
        L.enter = (zui_rect_t){L.mid - bw / 2, fy, bw, fh};
    }
    L.msg = (zui_rect_t){L.field.x, fy + fh + S(sc, 12), fw, S(sc, 22)};
    return L;
}

/* The row: centred as a whole when everyone fits; otherwise it scrolls to keep
 * the chosen person in the middle, without running past either end. */
static int32_t row_x(const lay_t *L, const zui_login_t *l, uint32_t i)
{
    uint32_t total = l->n + (l->can_add ? 1u : 0u);
    int32_t span = (int32_t) (total ? total - 1 : 0) * L->step;
    if (span + 2 * L->step <= L->width) return L->mid - span / 2 + (int32_t) i * L->step;
    int32_t first = L->mid - (int32_t) l->chosen * L->step; /* chosen in the middle */
    int32_t lo = L->left + L->step / 2, hi = L->left + L->width - L->step / 2;
    if (first > lo) first = lo;
    if (first + span < hi) first = hi - span;
    return first + (int32_t) i * L->step;
}

void zui_initials(const char *name, char out[3])
{
    uint32_t k = 0;
    bool start = true;
    out[0] = out[1] = out[2] = 0;
    if (!name) return;
    for (const char *p = name; *p && k < 2; p++) {
        char c = *p;
        bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!alnum) {
            start = true;
            continue;
        }
        if (start) {
            out[k++] = (c >= 'a' && c <= 'z') ? (char) (c - 32) : c;
            start = false;
        }
    }
}

static void circle(zui_surface_t *s, int32_t cx, int32_t cy, int32_t r, uint32_t rgb, uint32_t a)
{
    zui_fill_rrect(s, (zui_rect_t){cx - r, cy - r, 2 * r, 2 * r}, r, rgb, a);
}
static void ring(zui_surface_t *s, int32_t cx, int32_t cy, int32_t r, int32_t w, uint32_t rgb,
                 uint32_t a)
{
    zui_stroke_rrect(s, (zui_rect_t){cx - r, cy - r, 2 * r, 2 * r}, r, w, rgb, a);
}

static void centred(zui_surface_t *s, const zui_text_t *t, int32_t cx, int32_t base, uint32_t rgb,
                    uint32_t a, const char *str)
{
    if (!t || !t->font || !t->cache || !str) return;
    zui_text_a(s, t, cx - zui_text_width(t, str) / 2, base, rgb, a, str);
}

/* the person's picture: their portrait, or engraved initials in a gold ring */
static void person(zui_surface_t *s, const zui_login_t *l, const zui_text_t *t, uint32_t i,
                   int32_t cx, int32_t cy, int32_t r, int32_t sc, uint32_t a, bool chosen)
{
    const zui_login_person_t *p = &l->people[i];
    uint32_t acc = p->color ? p->color : ZUI_GOLD;
    if (chosen) circle(s, cx, cy, r + S(sc, 10), ZUI_GOLD_GLOW, a * 22u / 255u); /* warm halo */
    circle(s, cx, cy, r, ZUI_PANEL, a);
    circle(s, cx, cy + S(sc, 2), r - S(sc, 3), ZUI_ENGRAVE_LO, a * 120u / 255u); /* carved bowl */
    circle(s, cx, cy, r - S(sc, 4), ZUI_PANEL, a);
    if (l->portrait)
        l->portrait(s, (zui_rect_t){cx - r * 3 / 4, cy - r * 3 / 4, r * 3 / 2, r * 3 / 2}, i,
                    l->portrait_ctx);
    else if (t && t->font) {
        char ini[3];
        zui_initials(p->guest ? "Guest" : p->name, ini);
        int32_t asc = zui_text_ascent(t);
        centred(s, t, cx, cy + asc * 7 / 20, acc, a, ini);
    }
    ring(s, cx, cy, r,
         chosen         ? S(sc, 3)
         : S(sc, 2) < 1 ? 1
                        : S(sc, 2),
         acc, chosen ? a : a * 150u / 255u);
    zui_foil_rect(s, l->depth, (zui_rect_t){cx - r, cy - r, 2 * r, 2 * r}, r,
                  chosen ? S(sc, 20) : S(sc, 8));
}

static void utoa(uint32_t v, char *o)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n) *o++ = t[--n];
    *o = 0;
}

static int32_t shake_dx(int32_t sc, uint32_t q)
{
    if (q == 0 || q >= 256u) return 0;
    uint32_t ph = (q * 4u) & 255u, tri = ph < 128u ? ph : 255u - ph; /* 0..127..0 */
    int32_t amp = S(sc, 10) * (int32_t) (256u - q) / 256;
    int32_t v = amp * ((int32_t) tri - 64) / 64;
    return ((q * 4u) >> 8) & 1u ? -v : v;
}

void zui_login_draw(zui_surface_t *s, zui_rect_t a, int32_t sc, const zui_login_t *l,
                    const zui_login_fonts_t *f, uint32_t tick)
{
    if (!s || !l) return;
    if (sc <= 0) sc = 256;
    const zui_text_t *big = f ? f->big : 0, *small = f ? f->small : 0,
                     *ini = f && f->initials ? f->initials : big;
    zui_fill(s, a, ZUI_VOID);
    lay_t L = layout(a, sc, l);

    if (l->mark) l->mark(s, L.mark, l->mark_ctx);
    if (l->clock && small && small->font)
        zui_text_a(s, small, a.x + a.w - S(sc, 24) - zui_text_width(small, l->clock),
                   L.mark.y + L.mark.h / 2 + zui_text_ascent(small) / 3, ZUI_TEXT_2, 255u,
                   l->clock);
    if (!l->n || l->chosen >= l->n) {
        centred(s, small, L.mid, L.ring_cy, ZUI_MUTED, 255u,
                "Create the first person on this device");
        return;
    }

    const zui_login_person_t *p = &l->people[l->chosen];
    person(s, l, ini, l->chosen, L.mid, L.ring_cy, L.ring_r, sc, 255u, true);
    centred(s, big, L.mid, L.name_base, ZUI_TEXT, 255u, p->guest ? "Guest" : p->name);

    int32_t dx = shake_dx(sc, l->shake_q8);
    if (p->lock == ZUI_LOCK_PASS) {
        zui_rect_t fr = L.field;
        fr.x += dx;
        int32_t rad = fr.h / 2;
        zui_fill_rrect(s, fr, rad, ZUI_RAISED, 255u);
        zui_engrave(s, fr, rad, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 60u);
        zui_stroke_rrect(s, fr, rad, 1, l->wrong ? ZUI_ALERT : ZUI_EDGE, 255u);
        zui_foil_rect(s, l->depth, fr, rad, S(sc, 8));
        int32_t dot = S(sc, 4), gap = S(sc, 16), x = fr.x + S(sc, 22), cy = fr.y + fr.h / 2;
        int32_t room = (L.enter.x + dx - S(sc, 14) - x) / (gap > 0 ? gap : 1);
        uint32_t shown = l->typed > (uint32_t) (room > 0 ? room : 0)
                             ? (uint32_t) (room > 0 ? room : 0)
                             : l->typed;
        for (uint32_t i = 0; i < shown; i++)
            circle(s, x + (int32_t) i * gap + dot, cy, dot, ZUI_GOLD, 255u);
        if (!l->typed && small)
            zui_text_a(s, small, x, cy + zui_text_ascent(small) / 3, ZUI_FAINT, 255u, "Passphrase");
        if ((tick & 31u) < 16u && !l->wait_s) {
            int32_t cx = x + (int32_t) shown * gap + S(sc, 2);
            zui_fill(s, (zui_rect_t){cx, cy - S(sc, 11), S(sc, 2) < 1 ? 1 : S(sc, 2), S(sc, 22)},
                     ZUI_GOLD);
        }
        zui_rect_t e = L.enter;
        e.x += dx;
        zui_fill_rrect(s, e, e.h / 2, l->wait_s ? ZUI_HAIRLINE : ZUI_GOLD, 255u);
        int32_t ax = e.x + e.w / 2, ay = e.y + e.h / 2, k = e.h / 5; /* the arrow */
        zui_fill(s, (zui_rect_t){ax - k, ay - 1, 2 * k, 2}, ZUI_VOID);
        for (int32_t j = 0; j < k; j++) {
            zui_fill(s, (zui_rect_t){ax + k - j - 1, ay - j - 1, 2, 2}, ZUI_VOID);
            zui_fill(s, (zui_rect_t){ax + k - j - 1, ay + j - 1, 2, 2}, ZUI_VOID);
        }
    } else if (p->lock == ZUI_LOCK_PASSKEY) {
        int32_t cy = L.field.y + L.field.h / 2;
        ring(s, L.mid, cy, S(sc, 22), S(sc, 2) < 1 ? 1 : S(sc, 2), ZUI_GOLD, 255u);
        ring(s, L.mid, cy, S(sc, 14), 1, ZUI_GOLD, 180u);
        ring(s, L.mid, cy, S(sc, 6), 1, ZUI_GOLD, 140u);
        centred(s, small, L.mid, L.field.y + L.field.h + S(sc, 30), ZUI_TEXT_2, 255u,
                "Use this device's unlock");
    } else {
        zui_rect_t e = L.enter;
        zui_fill_rrect(s, e, e.h / 2, ZUI_GOLD, 255u);
        centred(s, small, L.mid,
                e.y + e.h / 2 + (small && small->font ? zui_text_ascent(small) / 3 : 0), ZUI_VOID,
                255u, "Enter");
    }

    if (l->wait_s || l->wrong) {
        char msg[48], num[12];
        const char *lead = "Try again in ";
        uint32_t v = l->wait_s >= 120u ? (l->wait_s + 59u) / 60u : l->wait_s;
        const char *unit = l->wait_s >= 120u ? " min" : " s";
        uint32_t k = 0;
        if (l->wait_s) {
            for (const char *q = lead; *q; q++) msg[k++] = *q;
            utoa(v, num);
            for (const char *q = num; *q; q++) msg[k++] = *q;
            for (const char *q = unit; *q; q++) msg[k++] = *q;
        } else
            for (const char *q = "That did not match"; *q; q++) msg[k++] = *q;
        msg[k] = 0;
        centred(s, small, L.mid, L.msg.y + L.msg.h - S(sc, 4), ZUI_ALERT, 255u, msg);
    }

    /* the row of everyone: centred on the chosen one, fading toward the edges */
    uint32_t total = l->n + (l->can_add ? 1u : 0u);
    int32_t edge = a.w / 2;
    for (uint32_t i = 0; i < total; i++) {
        int32_t cx = row_x(&L, l, i), d = cx - L.mid;
        if (d < 0) d = -d;
        if (cx + L.row_r < a.x || cx - L.row_r > a.x + a.w) continue;
        uint32_t span = (total ? total - 1u : 0u) * (uint32_t) L.step, al;
        if ((int32_t) span + 2 * L.step <= a.w)
            al = i == l->chosen ? 255u : 205u; /* fits: no fade */
        else
            al = d >= edge ? 40u : 255u - (uint32_t) (d * 215 / edge);
        if (i < l->n) {
            person(s, l, f && f->row_initials ? f->row_initials : small, i, cx, L.row_cy, L.row_r,
                   sc, al, i == l->chosen);
            if (small && small->font) {
                const char *nm = l->people[i].guest ? "Guest" : l->people[i].name;
                int32_t w = zui_text_width(small, nm), mw = L.step - S(sc, 12);
                if (w <= mw)
                    zui_text_a(s, small, cx - w / 2, L.row_cy + L.row_r + S(sc, 24),
                               i == l->chosen ? ZUI_TEXT : ZUI_MUTED, al, nm);
                else
                    zui_text_fit(s, small, cx - mw / 2, L.row_cy + L.row_r + S(sc, 24), mw,
                                 ZUI_MUTED, nm);
            }
        } else {
            circle(s, cx, L.row_cy, L.row_r, ZUI_VOID, 255u);
            zui_dash_rrect(s,
                           (zui_rect_t){cx - L.row_r, L.row_cy - L.row_r, 2 * L.row_r, 2 * L.row_r},
                           L.row_r, 1, S(sc, 6), ZUI_GOLD);
            int32_t k = L.row_r / 3;
            zui_fill(s, (zui_rect_t){cx - k, L.row_cy - 1, 2 * k, 2}, ZUI_GOLD);
            zui_fill(s, (zui_rect_t){cx - 1, L.row_cy - k, 2, 2 * k}, ZUI_GOLD);
            centred(s, small, cx, L.row_cy + L.row_r + S(sc, 24), ZUI_MUTED, al, "Add person");
        }
    }
}

static bool inside(zui_rect_t r, int32_t x, int32_t y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

zui_login_hit_t zui_login_hit(zui_rect_t a, int32_t sc, const zui_login_t *l, int32_t x, int32_t y,
                              uint32_t *index)
{
    if (!l || !inside(a, x, y)) return ZUI_LOGIN_NOTHING;
    if (sc <= 0) sc = 256;
    lay_t L = layout(a, sc, l);
    uint32_t total = l->n + (l->can_add ? 1u : 0u);
    for (uint32_t i = 0; i < total; i++) {
        int32_t cx = row_x(&L, l, i), dx = x - cx, dy = y - L.row_cy, r = L.row_r + S(sc, 6);
        if (dx * dx + dy * dy <= r * r) {
            if (i >= l->n) return ZUI_LOGIN_ADD;
            if (index) *index = i;
            return ZUI_LOGIN_PERSON;
        }
    }
    if (!l->n || l->chosen >= l->n) return ZUI_LOGIN_NOTHING;
    uint8_t lock = l->people[l->chosen].lock;
    if (lock == ZUI_LOCK_PASSKEY) return ZUI_LOGIN_NOTHING;
    /* the chosen person's ring and name act on them directly: an open seat
     * enters, a passphrase seat focuses its field. The row wins where the
     * block and a row circle meet, because row hits return above. */
    {
        int32_t pad = L.ring_r + S(sc, 6);
        zui_rect_t pb = {L.mid - pad, L.ring_cy - pad, 2 * pad,
                         L.name_base - (L.ring_cy - L.ring_r) + S(sc, 16)};
        if (inside(pb, x, y)) return lock == ZUI_LOCK_PASS ? ZUI_LOGIN_FIELD : ZUI_LOGIN_ENTER;
    }
    if (inside(L.enter, x, y)) return ZUI_LOGIN_ENTER;
    if (lock == ZUI_LOCK_PASS && inside(L.field, x, y)) return ZUI_LOGIN_FIELD;
    return ZUI_LOGIN_NOTHING;
}
