/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_cliphist.c — clipboard history (zui_cliphist.h). */
#include "zui_cliphist.h"

/* Wipe through a volatile pointer so the stores are not dropped (CH3, CH5). */
static void wipe(void *v, uint32_t n)
{
    volatile uint8_t *z = v;
    for (uint32_t i = 0; i < n; i++) z[i] = 0;
}

/* The longest prefix of s[0..n) no longer than cap that ends on a whole
 * UTF-8 character. */
static uint32_t fit(const char *s, uint32_t n, uint32_t cap)
{
    if (n <= cap) return n;
    uint32_t k = cap;
    while (k > 0 && ((uint8_t) s[k] & 0xC0u) == 0x80u) k--;
    return k;
}

void zui_cliphist_init(zui_cliphist_t *h)
{
    wipe(h, sizeof *h);
    h->cap_bytes = ZUI_CLIP_CAP;
    h->next_seq = 1;
}

static void set_current(zui_cliphist_t *h, const char *s, uint32_t n, bool sensitive)
{
    wipe(h->cur, sizeof h->cur);
    n = fit(s, n, ZUI_CLIP_TEXT - 1u);
    for (uint32_t i = 0; i < n; i++) h->cur[i] = s[i];
    h->cur_len = n;
    h->cur_sensitive = sensitive;
}

static void remove_at(zui_cliphist_t *h, uint32_t i)
{
    h->bytes -= h->clip[i].len;
    for (uint32_t j = i + 1u; j < h->n; j++) h->clip[j - 1u] = h->clip[j];
    h->n--;
    wipe(&h->clip[h->n], sizeof h->clip[h->n]);
}

/* Move clip i to the front, keeping the others in order. */
static void to_front(zui_cliphist_t *h, uint32_t i)
{
    if (!i) {
        h->clip[0].seq = h->next_seq++;
        return;
    }
    zui_clip_t t = h->clip[i];
    for (uint32_t j = i; j > 0; j--) h->clip[j] = h->clip[j - 1u];
    h->clip[0] = t;
    h->clip[0].seq = h->next_seq++;
    wipe(&t, sizeof t);
}

/* CH4: drop the oldest unpinned clips until `need` more bytes and one
 * more clip fit. False when the pinned ones alone leave no room. */
static bool make_room(zui_cliphist_t *h, uint32_t need, bool one_more)
{
    while (h->bytes + need > h->cap_bytes || (one_more && h->n >= ZUI_CLIP_MAX)) {
        int32_t victim = -1;
        for (int32_t i = (int32_t) h->n - 1; i >= 0 && victim < 0; i--)
            if (!h->clip[i].pinned) victim = i;
        if (victim < 0) return false;
        remove_at(h, (uint32_t) victim);
        h->evicted++;
    }
    return true;
}

static bool same_text(const zui_clip_t *c, const char *s, uint32_t n, uint8_t type)
{
    if (c->len != n || c->type != type) return false;
    for (uint32_t i = 0; i < n; i++)
        if (c->text[i] != s[i]) return false;
    return true;
}

int zui_cliphist_add(zui_cliphist_t *h, const char *s, uint32_t n, zui_clip_type_t type,
                     uint32_t flags)
{
    if (!h || !s || !n) return ZUI_CLIP_BAD;
    if ((uint32_t) type >= (uint32_t) ZUI_CLIP_TYPES) type = ZUI_CLIP_PLAIN;
    bool sensitive = (flags & ZUI_CLIP_SENSITIVE) != 0;
    set_current(h, s, n, sensitive);
    if (sensitive) { /* CH5 */
        h->skipped++;
        return ZUI_CLIP_SKIPPED;
    }
    if (n > ZUI_CLIP_TEXT - 1u || n > h->cap_bytes) return ZUI_CLIP_TOO_BIG;
    for (uint32_t i = 0; i < h->n; i++) /* CH2 */
        if (same_text(&h->clip[i], s, n, (uint8_t) type)) {
            to_front(h, i);
            return ZUI_CLIP_MOVED;
        }
    if (!make_room(h, n, true)) return ZUI_CLIP_TOO_BIG;
    for (uint32_t j = h->n; j > 0; j--) h->clip[j] = h->clip[j - 1u];
    h->n++;
    zui_clip_t *c = &h->clip[0];
    wipe(c, sizeof *c);
    for (uint32_t i = 0; i < n; i++) c->text[i] = s[i];
    c->len = n;
    c->type = (uint8_t) type;
    c->seq = h->next_seq++;
    h->bytes += n;
    return ZUI_CLIP_ADDED;
}

zui_clip_type_t zui_clip_guess(const char *s, uint32_t n)
{
    if (!s || !n) return ZUI_CLIP_PLAIN;
    for (uint32_t i = 0; i < n; i++)
        if (s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r') return ZUI_CLIP_PLAIN;
    /* a link: a scheme of letters (and + - .) then "://" and something */
    uint32_t k = 0;
    while (k < n &&
           ((s[k] >= 'a' && s[k] <= 'z') || (s[k] >= 'A' && s[k] <= 'Z') ||
            (k && ((s[k] >= '0' && s[k] <= '9') || s[k] == '+' || s[k] == '-' || s[k] == '.'))))
        k++;
    if (k && k + 3u < n && s[k] == ':' && s[k + 1u] == '/' && s[k + 2u] == '/')
        return ZUI_CLIP_LINK;
    if (s[0] == '/' || (n > 1 && s[0] == '~' && s[1] == '/')) return ZUI_CLIP_PATH;
    bool digit = false;
    for (uint32_t i = 0; i < n; i++) {
        char c = s[i];
        if (c >= '0' && c <= '9')
            digit = true;
        else if (!((c == '-' || c == '+') && i == 0) && c != '.' && c != ',')
            return ZUI_CLIP_PLAIN;
    }
    return digit ? ZUI_CLIP_NUMBER : ZUI_CLIP_PLAIN;
}

const zui_clip_t *zui_cliphist_get(const zui_cliphist_t *h, uint32_t i)
{
    return (h && i < h->n) ? &h->clip[i] : 0;
}

uint32_t zui_cliphist_count(const zui_cliphist_t *h)
{
    return h ? h->n : 0;
}

bool zui_cliphist_pin(zui_cliphist_t *h, uint32_t i, bool pinned)
{
    if (!h || i >= h->n) return false;
    h->clip[i].pinned = pinned;
    return true;
}

bool zui_cliphist_delete(zui_cliphist_t *h, uint32_t i)
{
    if (!h || i >= h->n) return false;
    remove_at(h, i);
    return true;
}

uint32_t zui_cliphist_clear(zui_cliphist_t *h, bool also_pinned)
{
    uint32_t gone = 0;
    if (!h) return 0;
    for (int32_t i = (int32_t) h->n - 1; i >= 0; i--)
        if (also_pinned || !h->clip[i].pinned) {
            remove_at(h, (uint32_t) i);
            gone++;
        }
    zui_cliphist_forget(h);
    return gone;
}

bool zui_cliphist_use(zui_cliphist_t *h, uint32_t i)
{
    if (!h || i >= h->n) return false;
    to_front(h, i);
    set_current(h, h->clip[0].text, h->clip[0].len, false);
    if (h->next_set) h->next_set(h->next_ctx, h->clip[0].text, h->clip[0].len);
    return true;
}

void zui_cliphist_set_cap(zui_cliphist_t *h, uint32_t cap_bytes)
{
    if (!h) return;
    h->cap_bytes = cap_bytes;
    make_room(h, 0, false);
}

void zui_cliphist_forget(zui_cliphist_t *h)
{
    if (!h) return;
    wipe(h->cur, sizeof h->cur);
    h->cur_len = 0;
    h->cur_sensitive = false;
}

uint32_t zui_cliphist_current(const zui_cliphist_t *h, char *out, uint32_t cap)
{
    if (!h || !out) return 0;
    uint32_t n = fit(h->cur, h->cur_len, cap);
    for (uint32_t i = 0; i < n; i++) out[i] = h->cur[i];
    return n;
}

/* ---- CH6: the toolkit hook ------------------------------------------------ */

/* CH5: a copy while a secret field has the focus is a secret. */
static bool secret_focus(const zui_ui_t *ui)
{
    if (!ui || ui->focus < 0 || (uint32_t) ui->focus >= ui->n) return false;
    const zui_widget_t *w = &ui->w[ui->focus];
    return w->kind == ZUI_W_FIELD && w->u.field.secret;
}

bool zui_cliphist_clip_set(void *ctx, const char *s, uint32_t n)
{
    zui_cliphist_t *h = ctx;
    if (!h || !s) return false;
    uint32_t flags = secret_focus(h->ui) ? ZUI_CLIP_SENSITIVE : 0u;
    if (zui_cliphist_add(h, s, n, zui_clip_guess(s, n), flags) == ZUI_CLIP_BAD) {
        zui_cliphist_forget(h); /* copying nothing empties the clipboard */
    }
    if (h->next_set) h->next_set(h->next_ctx, s, n);
    return true; /* the history holds the clipboard; the view keeps no copy */
}

uint32_t zui_cliphist_clip_get(void *ctx, char *out, uint32_t cap)
{
    const zui_cliphist_t *h = ctx;
    if (!h || !out) return 0;
    if (h->next_get) return h->next_get(h->next_ctx, out, cap);
    return zui_cliphist_current(h, out, cap);
}

void zui_cliphist_attach(zui_cliphist_t *h, zui_ui_t *ui)
{
    if (!h || !ui || ui->clip_ctx == h) return;
    h->ui = ui;
    h->next_set = ui->clip_set;
    h->next_get = ui->clip_get;
    h->next_ctx = ui->clip_ctx;
    ui->clip_set = zui_cliphist_clip_set;
    ui->clip_get = zui_cliphist_clip_get;
    ui->clip_ctx = h;
}

void zui_cliphist_detach(zui_cliphist_t *h, zui_ui_t *ui)
{
    if (!h || !ui || ui->clip_ctx != h) return;
    ui->clip_set = h->next_set;
    ui->clip_get = h->next_get;
    ui->clip_ctx = h->next_ctx;
    h->ui = 0;
    h->next_set = 0;
    h->next_get = 0;
    h->next_ctx = 0;
}
