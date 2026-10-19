/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zv.c — the shared app shell and formatting (zv.h). */
#include "zv.h"
#include "zui_progress.h"
#include "zui_tokens.h"

static uint32_t put(char *out, uint32_t cap, uint32_t n, const char *s)
{
    while (s && *s && n + 1u < cap) out[n++] = *s++;
    if (cap) out[n < cap ? n : cap - 1u] = 0;
    return n;
}

uint32_t zv_cat(char *out, uint32_t cap, const char *a, const char *b)
{
    uint32_t n = put(out, cap, 0, a);
    return put(out, cap, n, b);
}

uint32_t zv_u64(char *out, uint32_t cap, uint64_t v)
{
    char d[24];
    uint32_t k = 0;
    do {
        d[k++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v);
    uint32_t n = 0;
    while (k && n + 1u < cap) out[n++] = d[--k];
    if (cap) out[n] = 0;
    return n;
}

uint32_t zv_i64(char *out, uint32_t cap, int64_t v)
{
    if (v >= 0) return zv_u64(out, cap, (uint64_t) v);
    if (cap < 2) return zv_u64(out, cap, 0);
    out[0] = '-';
    return 1u + zv_u64(out + 1, cap - 1u, (uint64_t) (-(v + 1)) + 1u);
}

uint32_t zv_minor(char *out, uint32_t cap, int64_t v, uint32_t places)
{
    uint64_t u = v < 0 ? (uint64_t) (-(v + 1)) + 1u : (uint64_t) v, p = 1;
    for (uint32_t i = 0; i < places; i++) p *= 10u;
    uint32_t n = v < 0 ? put(out, cap, 0, "-") : 0;
    n += zv_u64(out + n, cap - n, u / p);
    if (!places) return n;
    n = put(out, cap, n, ".");
    char frac[24];
    uint64_t f = u % p;
    for (uint32_t i = places; i > 0; i--) {
        frac[i - 1] = (char) ('0' + f % 10u);
        f /= 10u;
    }
    frac[places] = 0;
    return put(out, cap, n, frac);
}

uint32_t zv_bytes(char *out, uint32_t cap, uint64_t v)
{
    static const char *const U[] = {" B", " KB", " MB", " GB", " TB"};
    uint32_t u = 0;
    uint64_t whole = v, tenth = 0;
    while (whole >= 1024u && u < 4u) {
        tenth = ((whole % 1024u) * 10u) / 1024u;
        whole /= 1024u;
        u++;
    }
    uint32_t n = zv_u64(out, cap, whole);
    if (u && whole < 10u) { /* one decimal under ten: "3.4 MB" */
        n = put(out, cap, n, ".");
        char d[2] = {(char) ('0' + tenth), 0};
        n = put(out, cap, n, d);
    }
    return put(out, cap, n, U[u]);
}

void zv_shell_build(zv_shell_t *s, zui_ui_t *ui, const char *title)
{
    s->ui = ui;
    s->npal = 0;
    s->ai = 0;
    s->status_text[0] = s->error_text[0] = 0;
    s->root = zui_box(ui, ZUI_NONE, ZUI_COL);
    zui_set_pad(ui, s->root, 0, 0);
    s->top = zui_box(ui, s->root, ZUI_ROW);
    zui_set_pad(ui, s->top, ZUI_S3, ZUI_S3);
    s->title = zui_label(ui, s->top, title);
    zui_set_font(ui, s->title, ZUI_TEXT_TITLE, ZUI_TEXT);
    zui_id_t gap = zui_box(ui, s->top, ZUI_ROW);
    zui_set_size(ui, gap, ZUI_SIZE_FLEX, 1);
    s->status = zui_label(ui, s->top, "");
    zui_set_font(ui, s->status, ZUI_TEXT_LABEL, ZUI_MUTED);
    zui_separator(ui, s->root);
    s->body = zui_box(ui, s->root, ZUI_ROW);
    zui_set_size(ui, s->body, ZUI_SIZE_FLEX, 1);
    zui_set_pad(ui, s->body, ZUI_S3, ZUI_S3);
    zui_id_t foot = zui_box(ui, s->root, ZUI_COL);
    zui_set_pad(ui, foot, ZUI_S3, ZUI_S2);
    s->held = zui_held(ui, foot);
    s->error = zui_label(ui, foot, "");
    zui_set_font(ui, s->error, ZUI_TEXT_LABEL, ZUI_ALERT);
    zui_set_hidden(ui, s->error, true);
    s->bar = zui_progress(ui, s->root);
    zui_set_hidden(ui, s->bar, true);
}

void zv_shell_palette(zv_shell_t *s, const zxm_ai_action_t *t, uint32_t n)
{
    if (n > ZUI_PALETTE_MAX) n = ZUI_PALETTE_MAX;
    for (uint32_t i = 0; i < n; i++) {
        s->pal[i].name = t[i].name;
        s->pal[i].agent = t[i].agent;
        s->pal[i].est_tokens = t[i].est_tokens;
    }
    s->npal = n;
    s->ai = t;
    zui_palette_set(s->ui, s->root, s->pal, n);
}

void zv_shell_status(zv_shell_t *s, const char *text)
{
    zv_cat(s->status_text, sizeof s->status_text, text, 0);
    zui_set_text(s->ui, s->status, s->status_text);
}

void zv_shell_result(zv_shell_t *s, int rc, const zxm_link_t *l)
{
    const char *m = 0;
    switch (rc) {
    case ZXM_OK:
        break;
    case ZXM_E_REMOTE:
        m = l && l->err[0] ? l->err : "zxvd refused";
        break;
    case ZXM_E_TRANSPORT:
        m = "zxvd is not answering";
        break;
    case ZXM_E_TOOBIG:
        m = "That is too large to send in one go";
        break;
    case ZXM_E_PROTOCOL:
        m = "zxvd sent a reply this app does not understand";
        break;
    case ZXM_E_STATE:
        m = "That does not apply here";
        break;
    case ZXM_E_ARG:
        m = "That is not a valid value";
        break;
    default:
        m = "Something went wrong";
        break;
    }
    zv_cat(s->error_text, sizeof s->error_text, m, 0);
    zui_set_text(s->ui, s->error, s->error_text);
    zui_set_hidden(s->ui, s->error, rc == ZXM_OK);
}

void zv_shell_held(zv_shell_t *s, const zxm_held_t *h)
{
    if (!h || h->state == ZXM_HELD_NONE || h->state == ZXM_HELD_REJECTED) {
        zui_held_set(s->ui, s->held, "", 0, false, 0, 0);
        return;
    }
    uint8_t st = h->state == ZXM_HELD_PENDING ? 1 : 2;
    zui_held_set(s->ui, s->held, h->cost.agent, h->cost.tokens, h->cost.exact, h->text, st);
}

bool zv_compact(const zv_shell_t *s)
{
    return s->ui->plan.cls == ZUI_COMPACT;
}
