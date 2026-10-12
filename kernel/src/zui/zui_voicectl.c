/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_voicectl.c — voice control: phrase tables, names, numbers, the grid,
 * dictation and the actions they make (zui_voicectl.h). */
#include "zui_voicectl.h"
#include "zui_tokens.h"

/* ---- VC1: English --------------------------------------------------------- */

static const zui_vc_phrase_t EN[] = {
    {ZUI_VC_CMD_TAP_NUMBER, "tap %n"},
    {ZUI_VC_CMD_TAP_NUMBER, "click %n"},
    {ZUI_VC_CMD_TAP_NUMBER, "press %n"},
    {ZUI_VC_CMD_TAP_NUMBER, "number %n"},
    {ZUI_VC_CMD_NUMBER, "%n"},
    {ZUI_VC_CMD_SHOW_NUMBERS, "show numbers"},
    {ZUI_VC_CMD_HIDE_NUMBERS, "hide numbers"},
    {ZUI_VC_CMD_SHOW_GRID, "show grid"},
    {ZUI_VC_CMD_HIDE_GRID, "hide grid"},
    {ZUI_VC_CMD_CANCEL, "cancel"},
    {ZUI_VC_CMD_SCROLL_UP, "scroll up"},
    {ZUI_VC_CMD_SCROLL_DOWN, "scroll down"},
    {ZUI_VC_CMD_BACK, "go back"},
    {ZUI_VC_CMD_CLOSE_WINDOW, "close window"},
    {ZUI_VC_CMD_CLOSE_WINDOW, "close this window"},
    {ZUI_VC_CMD_DICTATE_ON, "start dictation"},
    {ZUI_VC_CMD_DICTATE_OFF, "stop dictation"},
    {ZUI_VC_CMD_OPEN, "open %s"},
    {ZUI_VC_CMD_OPEN, "launch %s"},
    {ZUI_VC_CMD_SWITCH, "switch to %s"},
    {ZUI_VC_CMD_TYPE, "type %s"},
    {ZUI_VC_CMD_SELECT, "select %s"},
    {ZUI_VC_CMD_CLICK, "click %s"},
    {ZUI_VC_CMD_CLICK, "tap %s"},
    {ZUI_VC_CMD_CLICK, "press %s"},
};

static const char *const EN_NUM[] = {
    "zero",     "one",     "two",     "three",     "four",     "five",     "six",
    "seven",    "eight",   "nine",    "ten",       "eleven",   "twelve",   "thirteen",
    "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen", "twenty"};

const zui_vc_locale_t zui_vc_locale_en = {"en", EN, sizeof EN / sizeof EN[0], EN_NUM,
                                          sizeof EN_NUM / sizeof EN_NUM[0]};

/* ---- small helpers ------------------------------------------------------------ */

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static void mset(void *p, uint32_t n)
{
    uint8_t *b = (uint8_t *) p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

static void scopy(char *d, uint32_t cap, const char *s)
{
    uint32_t n = s ? zui_utf8_fit(s, slen(s), cap - 1u) : 0;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    d[n] = 0;
}

static void ncopy(char *d, uint32_t cap, const char *s, uint32_t n)
{
    if (n > cap - 1u) n = zui_utf8_fit(s, n, cap - 1u);
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    d[n] = 0;
}

static bool seq(const char *a, const char *b)
{
    uint32_t i = 0;
    for (; a[i] && a[i] == b[i]; i++) {
    }
    return a[i] == b[i];
}

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
}

static bool space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int32_t imax(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

/* Closing punctuation at the end of s[0..n): how many bytes it takes. */
static uint32_t trailing_punct(const char *s, uint32_t n)
{
    if (n >= 1u && (s[n - 1u] == '.' || s[n - 1u] == ',' || s[n - 1u] == '!' || s[n - 1u] == '?' ||
                    s[n - 1u] == ';' || s[n - 1u] == ':'))
        return 1;
    if (n >= 3u) {
        const uint8_t *b = (const uint8_t *) s + n - 3u;
        if (b[0] == 0xE3u && b[1] == 0x80u && b[2] == 0x82u) return 3; /* 。 */
        if (b[0] == 0xE2u && b[1] == 0x80u && b[2] == 0xA6u) return 3; /* … */
        if (b[0] == 0xEFu && b[1] == 0xBCu && (b[2] == 0x9Fu || b[2] == 0x81u))
            return 3; /* ？ ！ */
    }
    return 0;
}

/* VC1: squeeze spaces and trim into raw; fold ASCII case and drop closing
 * punctuation into norm. Both share byte offsets up to norm's end. */
static uint32_t normalize(const char *s, char *raw, char *norm, uint32_t cap)
{
    uint32_t n = 0;
    bool gap = false;
    for (uint32_t i = 0; s && s[i] && n + 1u < cap; i++) {
        if (space(s[i])) {
            gap = n > 0;
            continue;
        }
        if (gap) {
            if (n + 2u >= cap) break;
            raw[n++] = ' ';
            gap = false;
        }
        raw[n++] = s[i];
    }
    n = zui_utf8_fit(raw, n, n);
    raw[n] = 0;
    uint32_t m = n;
    for (;;) {
        uint32_t p = trailing_punct(raw, m);
        if (!p) break;
        m -= p;
        while (m && raw[m - 1u] == ' ') m--;
    }
    for (uint32_t i = 0; i < m; i++) norm[i] = lower(raw[i]);
    norm[m] = 0;
    return m;
}

static void fold(char *d, uint32_t cap, const char *s)
{
    char raw[ZUI_VC_NAME * 2];
    normalize(s, raw, d, cap < sizeof raw ? cap : sizeof raw);
}

/* ---- VC1: matching a pattern -------------------------------------------------- */

static bool parse_number(const zui_voicectl_t *vc, const char *s, uint32_t *out)
{
    if (!s[0]) return false;
    uint32_t v = 0, i = 0;
    for (; s[i] >= '0' && s[i] <= '9'; i++) {
        v = v * 10u + (uint32_t) (s[i] - '0');
        if (v > 9999u) return false;
    }
    if (i && !s[i]) {
        *out = v;
        return true;
    }
    for (uint32_t l = 0; l <= vc->n_loc; l++) {
        const zui_vc_locale_t *loc = l < vc->n_loc ? vc->loc[l] : &zui_vc_locale_en;
        for (uint32_t k = 0; loc->numbers && k < loc->n_numbers; k++)
            if (loc->numbers[k] && seq(loc->numbers[k], s)) {
                *out = k;
                return true;
            }
    }
    return false;
}

/* Does the normalized phrase fit the pattern? The slot's text (if any) goes
 * to vc->arg (normalized) and vc->arg_raw (as said). */
static bool fits(zui_voicectl_t *vc, const char *pat, uint32_t n)
{
    const char *p = vc->norm;
    uint32_t pl = slen(pat), k = 0;
    while (k + 1u < pl && !(pat[k] == '%' && (pat[k + 1u] == 's' || pat[k + 1u] == 'n'))) k++;
    if (k + 1u >= pl) return seq(pat, p);
    bool num = pat[k + 1u] == 'n';
    uint32_t sl = pl - k - 2u; /* the suffix */
    if (n < k + sl + 1u) return false;
    for (uint32_t i = 0; i < k; i++)
        if (p[i] != pat[i]) return false;
    for (uint32_t i = 0; i < sl; i++)
        if (p[n - sl + i] != pat[k + 2u + i]) return false;
    uint32_t a = k, b = n - sl;
    while (a < b && p[a] == ' ') a++;
    while (b > a && p[b - 1u] == ' ') b--;
    if (a >= b) return false;
    ncopy(vc->arg, sizeof vc->arg, p + a, b - a);
    /* as said: the same bytes, and when the slot ends the phrase, its punctuation too */
    uint32_t rb = sl ? b : slen(vc->raw);
    ncopy(vc->arg_raw, sizeof vc->arg_raw, vc->raw + a, rb - a);
    if (num) {
        uint32_t v;
        return parse_number(vc, vc->arg, &v);
    }
    return true;
}

static zui_vc_cmd_t match(zui_voicectl_t *vc, uint32_t n, uint32_t *from)
{
    /* *from: where to continue (locale << 16 | phrase), to fall through */
    for (uint32_t l = *from >> 16; l <= vc->n_loc; l++) {
        const zui_vc_locale_t *loc = l < vc->n_loc ? vc->loc[l] : &zui_vc_locale_en;
        uint32_t start = l == (*from >> 16) ? (*from & 0xFFFFu) : 0;
        for (uint32_t i = start; i < loc->n_phrases; i++) {
            const zui_vc_phrase_t *ph = &loc->phrases[i];
            if (!ph->pattern || ph->cmd >= ZUI_VC_CMD_COUNT) continue;
            vc->arg[0] = vc->arg_raw[0] = 0;
            if (fits(vc, ph->pattern, n)) {
                *from = (l << 16) | (i + 1u);
                return (zui_vc_cmd_t) ph->cmd;
            }
        }
    }
    *from = 0xFFFFFFFFu;
    return ZUI_VC_CMD_COUNT;
}

zui_vc_cmd_t zui_vc_parse(zui_voicectl_t *vc, const char *utf8, char *arg, uint32_t cap)
{
    uint32_t n = normalize(utf8, vc->raw, vc->norm, sizeof vc->norm), from = 0;
    zui_vc_cmd_t c = n ? match(vc, n, &from) : ZUI_VC_CMD_COUNT;
    if (arg && cap) scopy(arg, cap, c == ZUI_VC_CMD_COUNT ? "" : vc->arg);
    return c;
}

/* ---- setup ------------------------------------------------------------------------ */

void zui_vc_init(zui_voicectl_t *vc)
{
    mset(vc, sizeof *vc);
    vc->dictation = ZUI_VC_DICT_AUTO;
    vc->clock = 1000;
}

void zui_vc_attach(zui_voicectl_t *vc, zui_ui_t *ui, int32_t x, int32_t y, zui_wm_t *wm)
{
    vc->ui = ui;
    vc->ui_x = x;
    vc->ui_y = y;
    vc->wm = wm;
    if (wm)
        vc->area = (zui_rect_t){0, 0, wm->w, wm->h};
    else if (ui)
        vc->area = (zui_rect_t){x, y, (int32_t) ui->screen.px_w, (int32_t) ui->screen.px_h};
}

void zui_vc_set_area(zui_voicectl_t *vc, zui_rect_t area)
{
    vc->area = area;
}

bool zui_vc_add_locale(zui_voicectl_t *vc, const zui_vc_locale_t *loc)
{
    if (!loc || vc->n_loc >= ZUI_VC_LOCALES) return false;
    for (uint32_t i = vc->n_loc; i > 0; i--) vc->loc[i] = vc->loc[i - 1u];
    vc->loc[0] = loc;
    vc->n_loc++;
    return true;
}

void zui_vc_clear_locales(zui_voicectl_t *vc)
{
    vc->n_loc = 0;
}

void zui_vc_set_apps(zui_voicectl_t *vc, const char *const *names, uint32_t n)
{
    vc->apps = names;
    vc->n_apps = names ? n : 0;
}

void zui_vc_set_dictation(zui_voicectl_t *vc, zui_vc_dict_t mode)
{
    vc->dictation = mode == ZUI_VC_DICT_ON || mode == ZUI_VC_DICT_OFF ? mode : ZUI_VC_DICT_AUTO;
}

void zui_vc_on_act(zui_voicectl_t *vc, zui_vc_act_fn fn, void *ctx)
{
    vc->on_act = fn;
    vc->act_ctx = ctx;
}

/* ---- targets ---------------------------------------------------------------------- */

static bool shown_w(const zui_ui_t *ui, zui_id_t id)
{
    for (zui_id_t p = id; p != ZUI_NONE; p = ui->w[p].parent)
        if (ui->w[p].hidden) return false;
    return true;
}

static bool in_held(const zui_ui_t *ui, zui_id_t id)
{
    for (zui_id_t p = id; p != ZUI_NONE; p = ui->w[p].parent)
        if (ui->w[p].kind == ZUI_W_HELD) return true;
    return false;
}

static bool usable(const zui_ui_t *ui, zui_id_t id)
{
    const zui_widget_t *w = &ui->w[id];
    if (!w->focusable || w->disabled || !shown_w(ui, id)) return false;
    if (w->kind == ZUI_W_FIELD && w->u.field.readonly && in_held(ui, id)) return false;
    if (w->r.w <= 0 || w->r.h <= 0) return false;
    return w->r.x < (int32_t) ui->screen.px_w && w->r.y < (int32_t) ui->screen.px_h &&
           w->r.x + w->r.w > 0 && w->r.y + w->r.h > 0;
}

static zui_id_t preorder_next(const zui_ui_t *ui, zui_id_t id)
{
    if (ui->w[id].first != ZUI_NONE) return ui->w[id].first;
    while (id != ZUI_NONE) {
        if (ui->w[id].next != ZUI_NONE) return ui->w[id].next;
        id = ui->w[id].parent;
    }
    return ZUI_NONE;
}

static const char *name_of(const zui_widget_t *w)
{
    if (w->name[0]) return w->name;
    if (w->label[0]) return w->label;
    if (w->kind == ZUI_W_FIELD) return w->u.field.placeholder;
    return "";
}

static zui_vc_target_t *add(zui_voicectl_t *vc, uint8_t kind, int32_t id, int32_t row, zui_rect_t r,
                            const char *name)
{
    if (vc->nt >= ZUI_VC_TARGETS) return 0;
    zui_vc_target_t *t = &vc->t[vc->nt++];
    t->kind = kind;
    t->held = false;
    t->id = id;
    t->row = row;
    t->r = r;
    scopy(t->name, sizeof t->name, name);
    return t;
}

static void add_rows(zui_voicectl_t *vc, zui_ui_t *ui, zui_id_t id)
{
    zui_widget_t *w = &ui->w[id];
    if (!w->u.list.row) return;
    int32_t x = w->r.x + w->r.w / 2, cur = -1, y0 = 0;
    for (int32_t y = w->r.y; y <= w->r.y + w->r.h; y++) {
        int32_t row = y < w->r.y + w->r.h ? zui_list_row_at(ui, id, x, y) : -1;
        if (row == cur) continue;
        if (cur >= 0) {
            zui_row_t rw;
            mset(&rw, sizeof rw);
            w->u.list.row(ui, id, cur, &rw, w->u.list.row_ctx);
            zui_rect_t r = {w->r.x + vc->ui_x, y0 + vc->ui_y, w->r.w, y - y0};
            add(vc, ZUI_VC_T_ROW, id, cur, r, rw.text);
        }
        cur = row;
        y0 = y;
    }
}

uint32_t zui_vc_collect(zui_voicectl_t *vc)
{
    vc->nt = 0;
    zui_ui_t *ui = vc->ui;
    if (ui && ui->root != ZUI_NONE && !ui->palette.open && !ui->menu.open) {
        zui_event_t e;
        mset(&e, sizeof e);
        (void) zui_ui_event(ui, &e); /* lays out if needed; does nothing else */
        for (zui_id_t id = ui->root; id != ZUI_NONE; id = preorder_next(ui, id)) {
            if (!usable(ui, id)) continue;
            zui_rect_t r = ui->w[id].r;
            r.x += vc->ui_x;
            r.y += vc->ui_y;
            zui_vc_target_t *t = add(vc, ZUI_VC_T_WIDGET, id, -1, r, name_of(&ui->w[id]));
            if (t) t->held = in_held(ui, id);
            if (ui->w[id].kind == ZUI_W_LIST) add_rows(vc, ui, id);
        }
    }
    if (vc->wm) {
        uint32_t ids[ZUI_WM_MAX];
        uint32_t n = zui_wm_strip(vc->wm, ids, ZUI_WM_MAX);
        for (uint32_t i = 0; i < n; i++) {
            const zui_wm_window_t *w = zui_wm_get(vc->wm, ids[i]);
            add(vc, ZUI_VC_T_STRIP, (int32_t) ids[i], -1, zui_wm_strip_slot(vc->wm, i, n),
                w ? w->title : "");
        }
    }
    return vc->nt;
}

static void number_all(zui_voicectl_t *vc)
{
    vc->n_pick = vc->nt;
    for (uint32_t i = 0; i < vc->nt; i++) vc->pick[i] = (uint16_t) i;
}

const zui_vc_target_t *zui_vc_numbered(const zui_voicectl_t *vc, uint32_t n)
{
    if (vc->overlay != ZUI_VC_NUMBERS && vc->overlay != ZUI_VC_PICK) return 0;
    if (n == 0 || n > vc->n_pick) return 0;
    return &vc->t[vc->pick[n - 1u]];
}

zui_rect_t zui_vc_grid_cell(const zui_voicectl_t *vc, uint32_t n)
{
    zui_rect_t g = vc->grid;
    if (n < 1u || n > 9u) return (zui_rect_t){0, 0, 0, 0};
    int32_t col = (int32_t) ((n - 1u) % 3u), row = (int32_t) ((n - 1u) / 3u);
    int32_t x0 = g.x + g.w * col / 3, x1 = g.x + g.w * (col + 1) / 3;
    int32_t y0 = g.y + g.h * row / 3, y1 = g.y + g.h * (row + 1) / 3;
    return (zui_rect_t){x0, y0, x1 - x0, y1 - y0};
}

/* ---- VC7: actions ---------------------------------------------------------------- */

static zui_vc_result_t act(zui_voicectl_t *vc, zui_vc_action_t *a)
{
    vc->last = *a;
    vc->actions++;
    if (vc->on_act)
        vc->on_act(vc, a, vc->act_ctx);
    else
        (void) zui_vc_apply(vc, a);
    return ZUI_VC_DONE;
}

static void blank(zui_vc_action_t *a, zui_vc_act_kind_t kind)
{
    mset(a, sizeof *a);
    a->kind = (uint8_t) kind;
    a->widget = ZUI_NONE;
    a->app = -1;
}

static zui_vc_result_t activate(zui_voicectl_t *vc, const zui_vc_target_t *t)
{
    zui_vc_action_t a;
    blank(&a, ZUI_VC_ACT_ACTIVATE);
    a.target = *t;
    a.widget = t->kind == ZUI_VC_T_STRIP ? ZUI_NONE : t->id;
    a.window = t->kind == ZUI_VC_T_STRIP ? (uint32_t) t->id : 0;
    a.x = t->r.x + t->r.w / 2;
    a.y = t->r.y + t->r.h / 2;
    zui_vc_result_t r = act(vc, &a);
    if (vc->overlay == ZUI_VC_PICK) vc->overlay = ZUI_VC_NONE;
    if (vc->overlay == ZUI_VC_NUMBERS) { /* VC3: count again after the action */
        zui_vc_collect(vc);
        number_all(vc);
    }
    return r;
}

static zui_vc_result_t click_at(zui_voicectl_t *vc, zui_rect_t c)
{
    zui_vc_action_t a;
    blank(&a, ZUI_VC_ACT_CLICK_AT);
    a.x = c.x + c.w / 2;
    a.y = c.y + c.h / 2;
    vc->overlay = ZUI_VC_NONE;
    return act(vc, &a);
}

/* ---- VC2: names -------------------------------------------------------------------- */

/* 3: the name; 2: starts with it as a word; 1: contains it; 0: no. */
static int level(const char *name, const char *q)
{
    char n[ZUI_VC_NAME];
    fold(n, sizeof n, name);
    uint32_t nl = slen(n), ql = slen(q);
    if (!ql || ql > nl) return 0;
    if (seq(n, q)) return 3;
    bool prefix = true;
    for (uint32_t i = 0; i < ql; i++)
        if (n[i] != q[i]) prefix = false;
    if (prefix && n[ql] == ' ') return 2;
    for (uint32_t s = 0; s + ql <= nl; s++) {
        uint32_t i = 0;
        while (i < ql && n[s + i] == q[i]) i++;
        if (i == ql) return 1;
    }
    return 0;
}

static zui_vc_result_t click_name(zui_voicectl_t *vc, const char *q)
{
    zui_vc_collect(vc);
    int best = 0;
    uint32_t n = 0;
    uint16_t hit[ZUI_VC_TARGETS];
    for (uint32_t i = 0; i < vc->nt; i++) {
        int l = level(vc->t[i].name, q);
        if (vc->t[i].held && l < 3) l = 0; /* VC7: AI answers only by exact name */
        if (!l || l < best) continue;
        if (l > best) {
            best = l;
            n = 0;
        }
        hit[n++] = (uint16_t) i;
    }
    if (!n) return ZUI_VC_NO_MATCH;
    if (n == 1) return activate(vc, &vc->t[hit[0]]);
    for (uint32_t i = 0; i < n; i++) vc->pick[i] = hit[i];
    vc->n_pick = n;
    vc->overlay = ZUI_VC_PICK;
    return ZUI_VC_AMBIGUOUS;
}

/* ---- the focused field ------------------------------------------------------------- */

static zui_widget_t *field(zui_voicectl_t *vc)
{
    if (!vc->ui) return 0;
    zui_widget_t *w = zui_w(vc->ui, vc->ui->focus);
    if (!w || w->kind != ZUI_W_FIELD || w->u.field.readonly || !w->u.field.buf) return 0;
    if (w->disabled || !shown_w(vc->ui, vc->ui->focus)) return 0;
    return w;
}

static zui_vc_result_t type_text(zui_voicectl_t *vc, const char *text, bool dictated)
{
    zui_widget_t *w = field(vc);
    if (!w || !text[0]) return ZUI_VC_NO_MATCH;
    zui_vc_action_t a;
    blank(&a, ZUI_VC_ACT_TYPE);
    a.widget = vc->ui->focus;
    const zui_field_t *f = &w->u.field;
    uint32_t s0, s1;
    zui_field_selection(vc->ui, a.widget, &s0, &s1);
    /* dictation joins words: a space before, unless at the start or after one */
    if (dictated && s0 > 0 && s0 <= f->len && f->buf[s0 - 1u] != ' ' && f->buf[s0 - 1u] != '\n') {
        a.text[0] = ' ';
        scopy(a.text + 1, sizeof a.text - 1u, text);
    } else {
        scopy(a.text, sizeof a.text, text);
    }
    act(vc, &a);
    return dictated ? ZUI_VC_DICTATED : ZUI_VC_DONE;
}

static zui_vc_result_t select_word(zui_voicectl_t *vc, const char *q)
{
    zui_widget_t *w = field(vc);
    if (!w) return ZUI_VC_NO_MATCH;
    const char *b = w->u.field.buf;
    uint32_t n = w->u.field.len, ql = slen(q);
    int64_t loose = -1;
    for (uint32_t s = 0; ql && s + ql <= n; s++) {
        uint32_t i = 0;
        while (i < ql && lower(b[s + i]) == q[i]) i++;
        if (i < ql) continue;
        bool word = (s == 0 || b[s - 1u] == ' ' || b[s - 1u] == '\n') &&
                    (s + ql == n || b[s + ql] == ' ' || b[s + ql] == '\n' || b[s + ql] == '.' ||
                     b[s + ql] == ',' || b[s + ql] == '!' || b[s + ql] == '?');
        if (word) {
            loose = s;
            break;
        }
        if (loose < 0) loose = s;
    }
    if (loose < 0) return ZUI_VC_NO_MATCH;
    zui_vc_action_t a;
    blank(&a, ZUI_VC_ACT_SELECT_TEXT);
    a.widget = vc->ui->focus;
    a.start = (uint32_t) loose;
    a.end = (uint32_t) loose + ql;
    return act(vc, &a);
}

/* ---- VC5: apps and windows ------------------------------------------------------------ */

static zui_vc_result_t open_app(zui_voicectl_t *vc, const char *q)
{
    zui_vc_action_t a;
    blank(&a, ZUI_VC_ACT_OPEN_APP);
    if (vc->n_apps) {
        int best = 0;
        for (uint32_t i = 0; i < vc->n_apps; i++) {
            int l = vc->apps[i] ? level(vc->apps[i], q) : 0;
            if (l > best) {
                best = l;
                a.app = (int32_t) i;
            }
        }
        if (a.app < 0) return ZUI_VC_NO_MATCH;
        scopy(a.text, sizeof a.text, vc->apps[a.app]);
    } else {
        scopy(a.text, sizeof a.text, vc->arg_raw);
    }
    return act(vc, &a);
}

static zui_vc_result_t switch_to(zui_voicectl_t *vc, const char *q)
{
    if (!vc->wm) return ZUI_VC_NO_MATCH;
    int best = 0;
    uint32_t id = 0;
    for (uint32_t i = 0; i < vc->wm->nm; i++) { /* most recently used first */
        const zui_wm_window_t *w = zui_wm_get(vc->wm, vc->wm->mru[i]);
        if (!w) continue;
        int l = level(w->app, q), l2 = level(w->title, q);
        if (l2 > l) l = l2;
        if (l > best) {
            best = l;
            id = w->id;
        }
    }
    if (!id) return ZUI_VC_NO_MATCH;
    zui_vc_action_t a;
    blank(&a, ZUI_VC_ACT_SWITCH_TO);
    a.window = id;
    return act(vc, &a);
}

static zui_vc_result_t scroll(zui_voicectl_t *vc, bool down)
{
    zui_rect_t r = vc->area;
    if (vc->ui) {
        zui_widget_t *w = zui_w(vc->ui, vc->ui->focus);
        if (w && w->r.w > 0) r = (zui_rect_t){w->r.x + vc->ui_x, w->r.y + vc->ui_y, w->r.w, w->r.h};
    }
    if (r.w <= 0 || r.h <= 0) return ZUI_VC_NO_MATCH;
    zui_vc_action_t a;
    blank(&a, ZUI_VC_ACT_SCROLL);
    a.x = r.x + r.w / 2;
    a.y = r.y + r.h / 2;
    int32_t page = imax(40, r.h * 3 / 4);
    a.dy = down ? page : -page;
    return act(vc, &a);
}

/* ---- VC3, VC4: numbers and the grid ------------------------------------------------- */

static zui_vc_result_t number(zui_voicectl_t *vc, uint32_t n, bool tap)
{
    if (vc->overlay == ZUI_VC_GRID) {
        zui_rect_t c = zui_vc_grid_cell(vc, n);
        if (c.w <= 0 || c.h <= 0) return ZUI_VC_NO_MATCH;
        if (tap || c.w < 3 * ZUI_VC_GRID_MIN || c.h < 3 * ZUI_VC_GRID_MIN) return click_at(vc, c);
        vc->grid = c;
        vc->grid_depth++;
        return ZUI_VC_SHOWN;
    }
    if (vc->overlay == ZUI_VC_NUMBERS || vc->overlay == ZUI_VC_PICK) {
        const zui_vc_target_t *t = zui_vc_numbered(vc, n);
        if (!t) return ZUI_VC_NO_MATCH;
        zui_vc_target_t copy = *t;
        return activate(vc, &copy);
    }
    return ZUI_VC_UNKNOWN; /* a number with nothing numbered: maybe dictation */
}

static zui_vc_result_t run(zui_voicectl_t *vc, zui_vc_cmd_t c)
{
    uint32_t n = 0;
    switch (c) {
    case ZUI_VC_CMD_CLICK:
        return click_name(vc, vc->arg);
    case ZUI_VC_CMD_TAP_NUMBER:
    case ZUI_VC_CMD_NUMBER:
        parse_number(vc, vc->arg, &n);
        if (c == ZUI_VC_CMD_TAP_NUMBER && vc->overlay == ZUI_VC_NONE) return ZUI_VC_UNKNOWN;
        return number(vc, n, c == ZUI_VC_CMD_TAP_NUMBER);
    case ZUI_VC_CMD_SHOW_NUMBERS:
        zui_vc_collect(vc);
        number_all(vc);
        vc->overlay = ZUI_VC_NUMBERS;
        return ZUI_VC_SHOWN;
    case ZUI_VC_CMD_SHOW_GRID:
        if (vc->area.w <= 0 || vc->area.h <= 0) return ZUI_VC_NO_MATCH;
        vc->overlay = ZUI_VC_GRID;
        vc->grid = vc->area;
        vc->grid_depth = 0;
        return ZUI_VC_SHOWN;
    case ZUI_VC_CMD_HIDE_NUMBERS:
    case ZUI_VC_CMD_HIDE_GRID:
    case ZUI_VC_CMD_CANCEL: {
        bool was = (c == ZUI_VC_CMD_HIDE_GRID && vc->overlay == ZUI_VC_GRID) ||
                   (c == ZUI_VC_CMD_HIDE_NUMBERS &&
                    (vc->overlay == ZUI_VC_NUMBERS || vc->overlay == ZUI_VC_PICK)) ||
                   (c == ZUI_VC_CMD_CANCEL && vc->overlay != ZUI_VC_NONE);
        if (was) {
            vc->overlay = ZUI_VC_NONE;
            return ZUI_VC_SHOWN;
        }
        if (c == ZUI_VC_CMD_CANCEL) return click_name(vc, vc->norm); /* a Cancel button */
        return ZUI_VC_NO_MATCH;
    }
    case ZUI_VC_CMD_SCROLL_UP:
    case ZUI_VC_CMD_SCROLL_DOWN:
        return scroll(vc, c == ZUI_VC_CMD_SCROLL_DOWN);
    case ZUI_VC_CMD_BACK: {
        zui_vc_action_t a;
        blank(&a, ZUI_VC_ACT_BACK);
        if (vc->overlay == ZUI_VC_PICK) vc->overlay = ZUI_VC_NONE;
        return act(vc, &a);
    }
    case ZUI_VC_CMD_OPEN:
        return open_app(vc, vc->arg);
    case ZUI_VC_CMD_SWITCH:
        return switch_to(vc, vc->arg);
    case ZUI_VC_CMD_CLOSE_WINDOW: {
        uint32_t id = vc->wm ? zui_wm_front(vc->wm) : 0;
        if (!id) return ZUI_VC_NO_MATCH;
        zui_vc_action_t a;
        blank(&a, ZUI_VC_ACT_CLOSE_WINDOW);
        a.window = id;
        return act(vc, &a);
    }
    case ZUI_VC_CMD_TYPE:
        return type_text(vc, vc->arg_raw, false);
    case ZUI_VC_CMD_SELECT:
        return select_word(vc, vc->arg);
    case ZUI_VC_CMD_DICTATE_ON:
        vc->dictation = ZUI_VC_DICT_ON;
        return ZUI_VC_SHOWN;
    case ZUI_VC_CMD_DICTATE_OFF:
        vc->dictation = ZUI_VC_DICT_AUTO;
        return ZUI_VC_SHOWN;
    default:
        return ZUI_VC_UNKNOWN;
    }
}

zui_vc_result_t zui_vc_hear(zui_voicectl_t *vc, const char *utf8)
{
    uint32_t n = normalize(utf8, vc->raw, vc->norm, sizeof vc->norm);
    if (!n) return ZUI_VC_EMPTY;
    zui_vc_result_t best = ZUI_VC_UNKNOWN;
    for (uint32_t from = 0; from != 0xFFFFFFFFu;) {
        zui_vc_cmd_t c = match(vc, n, &from);
        if (c == ZUI_VC_CMD_COUNT) break;
        if (vc->dictation == ZUI_VC_DICT_ON && c != ZUI_VC_CMD_DICTATE_OFF) continue;
        zui_vc_result_t r = run(vc, c);
        if (r != ZUI_VC_UNKNOWN && r != ZUI_VC_NO_MATCH) return r;
        if (r == ZUI_VC_NO_MATCH) best = r;
    }
    /* VC6: not a command (or one with nothing to act on): dictation */
    if (vc->dictation != ZUI_VC_DICT_OFF &&
        (best == ZUI_VC_UNKNOWN || vc->dictation == ZUI_VC_DICT_ON) && field(vc))
        return type_text(vc, vc->raw, true);
    return best;
}

/* ---- VC7: performing ------------------------------------------------------------------ */

static bool ui_key(zui_voicectl_t *vc, uint16_t key)
{
    zui_event_t e;
    mset(&e, sizeof e);
    e.type = ZUI_EV_KEY_DOWN;
    e.key = key;
    e.ms = vc->clock;
    bool used = zui_ui_event(vc->ui, &e);
    e.type = ZUI_EV_KEY_UP;
    (void) zui_ui_event(vc->ui, &e);
    return used;
}

static bool send(zui_voicectl_t *vc, zui_event_t *e)
{
    e->ms = vc->clock;
    if (vc->wm) return zui_wm_event(vc->wm, e);
    if (!vc->ui) return false;
    e->x -= vc->ui_x;
    e->y -= vc->ui_y;
    return zui_ui_event(vc->ui, e);
}

static bool click(zui_voicectl_t *vc, int32_t x, int32_t y, bool to_ui)
{
    vc->clock += 1000u; /* far from the last click: never a double tap */
    zui_event_t e;
    zui_wm_t *wm = vc->wm;
    if (to_ui) vc->wm = 0;
    mset(&e, sizeof e);
    e.type = ZUI_EV_PTR_DOWN;
    e.button = ZUI_BTN_LEFT;
    e.x = x;
    e.y = y;
    bool used = send(vc, &e);
    mset(&e, sizeof e);
    e.type = ZUI_EV_PTR_UP;
    e.button = ZUI_BTN_LEFT;
    e.x = x;
    e.y = y;
    used = send(vc, &e) || used;
    vc->wm = wm;
    return used;
}

bool zui_vc_apply(zui_voicectl_t *vc, const zui_vc_action_t *a)
{
    if (!a) return false;
    vc->clock += 1u;
    switch (a->kind) {
    case ZUI_VC_ACT_ACTIVATE: {
        const zui_vc_target_t *t = &a->target;
        if (t->kind == ZUI_VC_T_STRIP) return vc->wm && click(vc, a->x, a->y, false);
        zui_widget_t *w = vc->ui ? zui_w(vc->ui, t->id) : 0;
        if (!w) return false;
        if (t->kind == ZUI_VC_T_ROW) return vc->ui && click(vc, a->x, a->y, true);
        zui_focus(vc->ui, t->id, true);
        if (w->kind == ZUI_W_FIELD) return true; /* a field is used by talking into it */
        return ui_key(vc, ZUI_KEY_ENTER);
    }
    case ZUI_VC_ACT_CLICK_AT:
        return click(vc, a->x, a->y, false);
    case ZUI_VC_ACT_SCROLL: {
        zui_event_t e;
        mset(&e, sizeof e);
        e.type = ZUI_EV_SCROLL;
        e.x = a->x;
        e.y = a->y;
        e.dy = a->dy;
        return send(vc, &e);
    }
    case ZUI_VC_ACT_BACK:
        if (vc->ui) return ui_key(vc, ZUI_KEY_BACK);
        if (vc->wm) {
            zui_event_t e;
            mset(&e, sizeof e);
            e.type = ZUI_EV_KEY_DOWN;
            e.key = ZUI_KEY_BACK;
            e.ms = vc->clock;
            return zui_wm_event(vc->wm, &e);
        }
        return false;
    case ZUI_VC_ACT_SWITCH_TO:
        return vc->wm && zui_wm_focus(vc->wm, a->window);
    case ZUI_VC_ACT_CLOSE_WINDOW:
        return vc->wm && zui_wm_close(vc->wm, a->window);
    case ZUI_VC_ACT_TYPE: {
        if (!vc->ui || a->widget != vc->ui->focus) return false;
        uint32_t n = slen(a->text), at = 0;
        bool used = false;
        while (at < n) { /* in pieces that fit an event, never splitting a character */
            uint32_t k = zui_utf8_fit(a->text + at, n - at, ZUI_TEXT_EV_MAX);
            if (!k) break;
            zui_event_t e;
            mset(&e, sizeof e);
            e.type = ZUI_EV_TEXT;
            e.ms = vc->clock;
            ncopy(e.text, sizeof e.text, a->text + at, k);
            used = zui_ui_event(vc->ui, &e) || used;
            at += k;
        }
        return used;
    }
    case ZUI_VC_ACT_SELECT_TEXT:
        if (!vc->ui || !zui_w(vc->ui, a->widget)) return false;
        zui_field_select(vc->ui, a->widget, a->start, a->end);
        return true;
    default: /* OPEN_APP: the launcher's (on_act) */
        return false;
    }
}

/* ---- drawing ------------------------------------------------------------------------- */

static void put_num(char *d, uint32_t v)
{
    char b[8];
    uint32_t k = 0;
    do {
        b[k++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v && k < 6u);
    for (uint32_t i = 0; i < k; i++) d[i] = b[k - 1u - i];
    d[k] = 0;
}

static void badge(zui_surface_t *s, const zui_text_t *text, int32_t x, int32_t y, uint32_t n)
{
    char num[8];
    put_num(num, n);
    int32_t w = 22, h = 20;
    if (text && text->font) w = imax(w, zui_text_width(text, num) + 10);
    zui_rect_t r = {x, y, w, h};
    zui_fill_rrect(s, r, ZUI_R_CONTROL, ZUI_PANEL, 255u);
    zui_engrave(s, r, ZUI_R_CONTROL, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 90u);
    if (text && text->font) {
        int32_t base = y + (h + zui_text_ascent(text) + zui_text_descent(text)) / 2;
        zui_text(s, text, x + (w - zui_text_width(text, num)) / 2, base, ZUI_TEXT, num);
    }
}

void zui_vc_draw(const zui_voicectl_t *vc, zui_surface_t *s, const zui_text_t *text)
{
    if (vc->overlay == ZUI_VC_NUMBERS || vc->overlay == ZUI_VC_PICK) {
        for (uint32_t i = 0; i < vc->n_pick; i++) {
            const zui_vc_target_t *t = &vc->t[vc->pick[i]];
            if (vc->overlay == ZUI_VC_PICK)
                zui_engrave(s, t->r, ZUI_R_CONTROL, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 140u);
            badge(s, text, t->r.x + 2, t->r.y + 2, i + 1u);
        }
        return;
    }
    if (vc->overlay != ZUI_VC_GRID) return;
    zui_rect_t g = vc->grid;
    zui_engrave(s, g, 0, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 140u);
    for (int32_t k = 1; k < 3; k++) {
        int32_t x = g.x + g.w * k / 3, y = g.y + g.h * k / 3;
        zui_vline(s, x - 1, g.y, g.h, 1, ZUI_ENGRAVE_LO);
        zui_vline(s, x, g.y, g.h, 1, ZUI_TEXT_2);
        zui_hline(s, g.x, y - 1, g.w, 1, ZUI_ENGRAVE_LO);
        zui_hline(s, g.x, y, g.w, 1, ZUI_TEXT_2);
    }
    for (uint32_t n = 1; n <= 9u; n++) {
        zui_rect_t c = zui_vc_grid_cell(vc, n);
        badge(s, text, c.x + c.w / 2 - 11, c.y + c.h / 2 - 10, n);
    }
}
