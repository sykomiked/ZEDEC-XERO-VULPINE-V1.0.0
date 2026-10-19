/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_reader.c — the screen reader (zui_reader.h): focus announcements, the
 * reader cursor, held answers, error lines, verbosity and the keys. */
#include "zui_reader.h"
#include "zui_progress.h"
#include "zui_tokens.h"

/* ---- small helpers ------------------------------------------------------ */

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

/* A bounded string builder that never splits a UTF-8 character. */
typedef struct {
    char *out;
    uint32_t cap, n;
} sb_t;

static void sb_n(sb_t *b, const char *s, uint32_t n)
{
    if (!b->cap || !s) return;
    uint32_t room = b->cap - 1u - b->n;
    if (n > room) n = zui_utf8_fit(s, n, room);
    for (uint32_t i = 0; i < n; i++) b->out[b->n++] = s[i];
    b->out[b->n] = 0;
}

static void sb_s(sb_t *b, const char *s)
{
    sb_n(b, s, slen(s));
}

static void sb_u(sb_t *b, uint64_t v)
{
    char num[24];
    uint32_t k = 0;
    do {
        num[k++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v && k < sizeof num);
    char d[24];
    for (uint32_t i = 0; i < k; i++) d[i] = num[k - 1u - i];
    sb_n(b, d, k);
}

/* One spoken part: parts are joined with ", ". */
static void part(sb_t *b, const char *s)
{
    if (!s || !s[0]) return;
    if (b->n) sb_s(b, ", ");
    sb_s(b, s);
}

static void part_n(sb_t *b, const char *s, uint32_t n)
{
    if (!s || !n) return;
    if (b->n) sb_s(b, ", ");
    sb_n(b, s, n);
}

/* "k of n" */
static void part_pos(sb_t *b, uint64_t k, uint64_t n)
{
    if (b->n) sb_s(b, ", ");
    sb_u(b, k);
    sb_s(b, " of ");
    sb_u(b, n);
}

static uint32_t fnv(uint32_t h, const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        h ^= (uint8_t) s[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t fnv_byte(uint32_t h, uint8_t v)
{
    h ^= v;
    return h * 16777619u;
}

/* ---- the tree ------------------------------------------------------------- */

static bool valid(const zui_reader_t *r, zui_id_t id)
{
    return r->ui && id >= 0 && (uint32_t) id < r->ui->n;
}

static const zui_widget_t *W(const zui_reader_t *r, zui_id_t id)
{
    return &r->ui->w[id];
}

static bool shown(const zui_reader_t *r, zui_id_t id)
{
    for (; id != ZUI_NONE; id = W(r, id)->parent)
        if (W(r, id)->hidden) return false;
    return true;
}

static const zui_reader_mark_t *mark_of(const zui_reader_t *r, zui_id_t id)
{
    for (uint32_t i = 0; i < r->n_mark; i++)
        if (r->mark[i].id == id) return &r->mark[i];
    return 0;
}

static uint8_t heading_of(const zui_reader_t *r, zui_id_t id)
{
    const zui_reader_mark_t *m = mark_of(r, id);
    if (m && m->heading) return m->heading;
    const zui_widget_t *w = W(r, id);
    return (w->kind == ZUI_W_LABEL && w->font == ZUI_TEXT_TITLE && w->label[0]) ? 1 : 0;
}

static zui_landmark_t landmark_of(const zui_reader_t *r, zui_id_t id)
{
    const zui_reader_mark_t *m = mark_of(r, id);
    if (m && m->landmark) return (zui_landmark_t) m->landmark;
    const zui_widget_t *w = W(r, id);
    return (w->kind == ZUI_W_BOX && w->panel && w->name[0]) ? ZUI_LANDMARK_REGION
                                                            : ZUI_LANDMARK_NONE;
}

static bool is_error_line(const zui_widget_t *w)
{
    return w->kind == ZUI_W_LABEL && w->color == ZUI_ALERT && w->label[0];
}

/* The held answer's own meta label and body are read with it. */
static bool part_of_held(const zui_reader_t *r, zui_id_t id)
{
    zui_id_t p = W(r, id)->parent;
    if (p == ZUI_NONE || W(r, p)->kind != ZUI_W_HELD) return false;
    zui_id_t meta = W(r, p)->first;
    zui_id_t body = meta != ZUI_NONE ? W(r, meta)->next : ZUI_NONE;
    return id == meta || id == body;
}

static const char *name_of(const zui_widget_t *w)
{
    if (w->name[0]) return w->name;
    if (w->label[0]) return w->label;
    if (w->kind == ZUI_W_FIELD) return w->u.field.placeholder;
    return "";
}

/* R2: an item the reader cursor stops on. */
static bool readable(const zui_reader_t *r, zui_id_t id)
{
    const zui_widget_t *w = W(r, id);
    if (w->kind == ZUI_W_SEPARATOR || part_of_held(r, id)) return false;
    if (w->kind == ZUI_W_BOX || w->kind == ZUI_W_SCROLL)
        return landmark_of(r, id) != ZUI_LANDMARK_NONE || w->name[0];
    if (w->kind == ZUI_W_LABEL) return name_of(w)[0] != 0;
    return true;
}

/* The next widget in tree order, skipping hidden subtrees. */
static zui_id_t after(const zui_reader_t *r, zui_id_t id, bool into)
{
    if (into && !W(r, id)->hidden && W(r, id)->first != ZUI_NONE) return W(r, id)->first;
    for (; id != ZUI_NONE; id = W(r, id)->parent)
        if (W(r, id)->next != ZUI_NONE) return W(r, id)->next;
    return ZUI_NONE;
}

/* Every readable item in reading order. */
static uint32_t items(const zui_reader_t *r, zui_id_t *out)
{
    uint32_t n = 0;
    zui_ui_t *ui = r->ui;
    if (!ui || ui->root == ZUI_NONE || !valid(r, ui->root)) return 0;
    for (zui_id_t id = ui->root; id != ZUI_NONE && n < ZUI_WIDGETS;) {
        bool vis = !W(r, id)->hidden;
        if (vis && readable(r, id)) out[n++] = id;
        id = after(r, id, vis);
    }
    return n;
}

/* The innermost landmark or named group around a widget. */
static zui_id_t group_of(const zui_reader_t *r, zui_id_t id)
{
    for (zui_id_t p = W(r, id)->parent; p != ZUI_NONE; p = W(r, p)->parent) {
        const zui_widget_t *w = W(r, p);
        if (landmark_of(r, p) != ZUI_LANDMARK_NONE) return p;
        if ((w->kind == ZUI_W_BOX || w->kind == ZUI_W_SCROLL) && w->name[0]) return p;
    }
    return ZUI_NONE;
}

/* ---- describing ----------------------------------------------------------- */

const char *zui_landmark_name(zui_landmark_t lm)
{
    switch (lm) {
    case ZUI_LANDMARK_BANNER:
        return "banner";
    case ZUI_LANDMARK_NAVIGATION:
        return "navigation";
    case ZUI_LANDMARK_MAIN:
        return "main";
    case ZUI_LANDMARK_SEARCH:
        return "search";
    case ZUI_LANDMARK_REGION:
        return "region";
    case ZUI_LANDMARK_COMPLEMENTARY:
        return "complementary";
    case ZUI_LANDMARK_CONTENTINFO:
        return "status";
    default:
        return "";
    }
}

const char *zui_verbosity_name(zui_verbosity_t v)
{
    return v == ZUI_VERB_LOW ? "low" : v == ZUI_VERB_HIGH ? "high" : "medium";
}

static const char *role_word(const zui_widget_t *w)
{
    switch (w->kind) {
    case ZUI_W_BOX:
        return "group";
    case ZUI_W_LABEL:
        return "";
    case ZUI_W_BUTTON:
        return "button";
    case ZUI_W_LIST:
        return "list";
    case ZUI_W_FIELD:
        return w->u.field.readonly    ? "read-only text"
               : w->u.field.multiline ? "text area"
                                      : "text field";
    case ZUI_W_SCROLL:
        return "scroll area";
    case ZUI_W_HELD:
        return "AI suggestion";
    case ZUI_W_PROGRESS:
        return "progress bar";
    case ZUI_W_CANVAS:
        return "image";
    case ZUI_W_SEPARATOR:
        return "separator";
    }
    return "";
}

/* Key symbols spoken as words: "⌘S" is "Command S". */
static void spoken_keys(sb_t *b, const char *k)
{
    static const struct {
        const char *sym, *word;
    } SYM[] = {{"\xE2\x8C\x98", "Command"}, {"\xE2\x87\xA7", "Shift"},  {"\xE2\x8C\xA5", "Option"},
               {"\xE2\x8C\x83", "Control"}, {"\xE2\x86\xA9", "Return"}, {"\xE2\x8C\xAB", "Delete"},
               {"\xE2\x8E\x8B", "Escape"},  {"\xE2\x87\xA5", "Tab"}};
    bool word_before = false;
    for (uint32_t i = 0; k[i];) {
        bool hit = false;
        for (uint32_t s = 0; s < sizeof SYM / sizeof SYM[0] && !hit; s++) {
            uint32_t n = slen(SYM[s].sym), j = 0;
            while (j < n && k[i + j] == SYM[s].sym[j]) j++;
            if (j == n) {
                if (word_before || (b->n && b->out[b->n - 1] != ' ')) sb_s(b, " ");
                sb_s(b, SYM[s].word);
                word_before = true;
                i += n;
                hit = true;
            }
        }
        if (hit) continue;
        if (word_before) sb_s(b, " ");
        word_before = false;
        uint32_t e = zui_utf8_next(k, slen(k), i);
        sb_n(b, k + i, e - i);
        i = e;
    }
}

static void describe_row(const zui_reader_t *r, zui_id_t id, int32_t row, sb_t *b)
{
    const zui_widget_t *w = W(r, id);
    const zui_list_t *l = &w->u.list;
    if (row < 0 || row >= l->rows) return;
    zui_row_t rr;
    mset(&rr, sizeof rr);
    if (l->row) l->row(r->ui, id, row, &rr, l->row_ctx);
    part(b, rr.text[0] ? rr.text : "blank");
    if (r->verbosity >= ZUI_VERB_MEDIUM) part(b, rr.detail);
    if (rr.muted) part(b, "dimmed");
    if (row == l->sel) part(b, "selected");
    if (r->verbosity >= ZUI_VERB_MEDIUM) part_pos(b, (uint64_t) row + 1u, (uint64_t) l->rows);
}

static void describe_held(const zui_reader_t *r, zui_id_t id, sb_t *b)
{
    const zui_widget_t *w = W(r, id);
    part(b, "AI suggestion");
    if (w->u.held.agent[0]) {
        part(b, "from ");
        sb_s(b, w->u.held.agent);
    }
    part(b, w->u.held.state == 1   ? "pending"
            : w->u.held.state == 2 ? "accepted"
            : w->u.held.state == 3 ? "rejected"
                                   : "");
    if (r->verbosity >= ZUI_VERB_MEDIUM) {
        if (b->n) sb_s(b, ", ");
        if (!w->u.held.exact) sb_s(b, "about ");
        sb_u(b, w->u.held.tokens);
        sb_s(b, w->u.held.tokens == 1 ? " token" : " tokens");
    }
    zui_id_t meta = w->first;
    zui_id_t body = meta != ZUI_NONE ? W(r, meta)->next : ZUI_NONE;
    if (body != ZUI_NONE && W(r, body)->kind == ZUI_W_FIELD && W(r, body)->u.field.buf) {
        const zui_field_t *f = &W(r, body)->u.field;
        part_n(b, f->buf, f->len);
    }
    if (r->verbosity >= ZUI_VERB_HIGH && w->u.held.state == 1)
        part(b, "accept with Command Return, or reject");
}

static void describe(const zui_reader_t *r, zui_id_t id, int32_t row, sb_t *b)
{
    if (!valid(r, id)) return;
    const zui_widget_t *w = W(r, id);
    zui_verbosity_t v = r->verbosity;
    if (w->kind == ZUI_W_LIST && row >= 0) {
        describe_row(r, id, row, b);
        return;
    }
    if (w->kind == ZUI_W_HELD) {
        describe_held(r, id, b);
        return;
    }
    const zui_reader_mark_t *m = mark_of(r, id);
    uint8_t hl = heading_of(r, id);
    zui_landmark_t lm = landmark_of(r, id);
    uint8_t ck = m ? m->check : ZUI_CHECK_NONE;
    part(b, name_of(w));
    /* the role */
    if (hl) {
        part(b, "heading");
        if (v >= ZUI_VERB_MEDIUM) {
            sb_s(b, " level ");
            sb_u(b, hl);
        }
    } else if (ck) {
        part(b, "check box");
    } else if (lm == ZUI_LANDMARK_REGION) {
        part(b, "region");
    } else if (lm) {
        part(b, zui_landmark_name(lm));
        sb_s(b, " landmark");
    } else if (is_error_line(w)) {
        part(b, "error");
    } else {
        part(b, role_word(w));
    }
    /* the value */
    if (w->kind == ZUI_W_FIELD) {
        const zui_field_t *f = &w->u.field;
        if (f->secret) {
            part(b, "secure");
            if (v >= ZUI_VERB_MEDIUM && f->len) {
                if (b->n) sb_s(b, ", ");
                uint32_t chars = 0;
                for (uint32_t i = 0; i < f->len; i = zui_utf8_next(f->buf, f->len, i)) chars++;
                sb_u(b, chars);
                sb_s(b, chars == 1 ? " character" : " characters");
            }
        } else if (f->len && f->buf) {
            part_n(b, f->buf, f->len);
        } else if (!f->readonly) {
            part(b, "empty");
        }
    } else if (w->kind == ZUI_W_LIST) {
        const zui_list_t *l = &w->u.list;
        if (l->rows <= 0) {
            part(b, "empty");
        } else {
            if (v >= ZUI_VERB_MEDIUM) {
                if (b->n) sb_s(b, ", ");
                sb_u(b, (uint64_t) l->rows);
                sb_s(b, l->rows == 1 ? " item" : " items");
            }
            if (l->sel >= 0 && l->sel < l->rows) describe_row(r, id, l->sel, b);
        }
    } else if (w->kind == ZUI_W_PROGRESS) {
        if (w->u.bar.p == ZUI_BAR_UNKNOWN) {
            part(b, "busy");
        } else {
            if (b->n) sb_s(b, ", ");
            sb_u(b, (w->u.bar.p * 100u + 128u) / 256u);
            sb_s(b, " percent");
        }
    }
    /* the state */
    if (w->disabled) part(b, "disabled");
    if (ck == ZUI_CHECK_ON) part(b, "checked");
    if (ck == ZUI_CHECK_OFF) part(b, "not checked");
    if (ck == ZUI_CHECK_MIXED) part(b, "partly checked");
    if (v >= ZUI_VERB_HIGH) {
        if (w->shortcut[0]) {
            part(b, "shortcut ");
            spoken_keys(b, w->shortcut);
        }
        if (m) part(b, m->hint);
    }
}

uint32_t zui_reader_describe(const zui_reader_t *r, zui_id_t id, int32_t row, char *out,
                             uint32_t cap)
{
    sb_t b = {out, cap, 0};
    if (cap) out[0] = 0;
    if (r && cap) describe(r, id, row, &b);
    return b.n;
}

/* ---- R5: speaking ----------------------------------------------------------- */

static void emit(zui_reader_t *r, const char *text, zui_say_t how)
{
    if (!r->on || !text) return;
    if (how == ZUI_SAY_INTERRUPT && r->said) how = ZUI_SAY_QUEUE;
    if (text[0]) {
        r->said = true;
        if (text != r->last) scopy(r->last, sizeof r->last, text);
    }
    r->utterances++;
    if (r->speak) r->speak(text, how, r->speak_ctx);
}

/* Build into r->buf and say it. */
static sb_t start(zui_reader_t *r)
{
    sb_t b = {r->buf, sizeof r->buf, 0};
    r->buf[0] = 0;
    return b;
}

static void say_item(zui_reader_t *r, zui_id_t id, int32_t row, zui_say_t how)
{
    sb_t b = start(r);
    describe(r, id, row, &b);
    emit(r, r->buf, how);
}

void zui_reader_announce(zui_reader_t *r, const char *text, zui_say_t how)
{
    if (!r) return;
    r->said = false;
    emit(r, text, how);
}

void zui_reader_error(zui_reader_t *r, const char *text)
{
    if (!r) return;
    r->said = false;
    sb_t b = start(r);
    sb_s(&b, "Error: ");
    sb_s(&b, text);
    emit(r, r->buf, ZUI_SAY_INTERRUPT);
}

void zui_reader_stop(zui_reader_t *r)
{
    if (!r || !r->on) return;
    r->utterances++;
    if (r->speak) r->speak("", ZUI_SAY_INTERRUPT, r->speak_ctx);
}

void zui_reader_repeat(zui_reader_t *r)
{
    if (!r) return;
    r->said = false;
    if (r->last[0]) emit(r, r->last, ZUI_SAY_INTERRUPT);
}

/* ---- setup and marks ---------------------------------------------------------- */

void zui_reader_init(zui_reader_t *r, zui_ui_t *ui, zui_speak_fn speak, void *ctx)
{
    mset(r, sizeof *r);
    r->ui = ui;
    r->speak = speak;
    r->speak_ctx = ctx;
    r->on = true;
    r->verbosity = ZUI_VERB_MEDIUM;
    r->cursor = r->focus = r->focus_group = ZUI_NONE;
    r->cursor_row = r->focus_sel = -1;
    r->menu_hot = r->palette_hot = -1;
}

void zui_reader_set_on(zui_reader_t *r, bool on)
{
    if (!r || r->on == on) return;
    r->said = false;
    if (!on) {
        emit(r, "screen reader off", ZUI_SAY_INTERRUPT);
        r->on = false;
    } else {
        r->on = true;
        emit(r, "screen reader on", ZUI_SAY_INTERRUPT);
    }
}

void zui_reader_set_verbosity(zui_reader_t *r, zui_verbosity_t v)
{
    if (r && (uint32_t) v < (uint32_t) ZUI_VERB_COUNT) r->verbosity = v;
}

static zui_reader_mark_t *mark_get(zui_reader_t *r, zui_id_t id)
{
    if (!r || !valid(r, id)) return 0;
    for (uint32_t i = 0; i < r->n_mark; i++)
        if (r->mark[i].id == id) return &r->mark[i];
    if (r->n_mark >= ZUI_READER_MARKS) return 0;
    zui_reader_mark_t *m = &r->mark[r->n_mark++];
    mset(m, sizeof *m);
    m->id = id;
    m->kind = (uint8_t) W(r, id)->kind;
    return m;
}

bool zui_reader_set_heading(zui_reader_t *r, zui_id_t id, uint8_t level)
{
    zui_reader_mark_t *m = mark_get(r, id);
    if (!m) return false;
    m->heading = level > 6 ? 6 : level;
    return true;
}

bool zui_reader_set_landmark(zui_reader_t *r, zui_id_t id, zui_landmark_t lm)
{
    zui_reader_mark_t *m = mark_get(r, id);
    if (!m || (uint32_t) lm >= (uint32_t) ZUI_LANDMARK_COUNT) return false;
    m->landmark = (uint8_t) lm;
    return true;
}

bool zui_reader_set_check(zui_reader_t *r, zui_id_t id, zui_check_t c)
{
    zui_reader_mark_t *m = mark_get(r, id);
    if (!m || (uint32_t) c > (uint32_t) ZUI_CHECK_MIXED) return false;
    m->check = (uint8_t) c;
    return true;
}

bool zui_reader_set_live(zui_reader_t *r, zui_id_t id, zui_live_t live)
{
    zui_reader_mark_t *m = mark_get(r, id);
    if (!m || (uint32_t) live > (uint32_t) ZUI_LIVE_ASSERTIVE) return false;
    m->live = (uint8_t) live;
    return true;
}

bool zui_reader_set_hint(zui_reader_t *r, zui_id_t id, const char *hint)
{
    zui_reader_mark_t *m = mark_get(r, id);
    if (!m) return false;
    scopy(m->hint, sizeof m->hint, hint);
    return true;
}

void zui_reader_clear_marks(zui_reader_t *r)
{
    if (r) r->n_mark = 0;
}

/* ---- R1, R3: watching the tree -------------------------------------------------- */

static uint32_t text_sig(const char *s, uint32_t n)
{
    return n ? (fnv(2166136261u, s, n) | 1u) : 0;
}

static void watch_lines(zui_reader_t *r)
{
    zui_ui_t *ui = r->ui;
    for (uint32_t i = 0; i < ui->n; i++) {
        zui_id_t id = (zui_id_t) i;
        const zui_widget_t *w = &ui->w[i];
        const zui_reader_mark_t *m = mark_of(r, id);
        uint8_t live = m ? m->live : ZUI_LIVE_OFF;
        bool err = live == ZUI_LIVE_OFF && is_error_line(w);
        if (!live && !err) continue;
        const char *t = w->label;
        uint32_t n = slen(t);
        if (w->kind == ZUI_W_FIELD) {
            t = w->u.field.buf;
            n = t ? w->u.field.len : 0;
        }
        uint32_t s = shown(r, id) ? text_sig(t, n) : 0;
        uint32_t was = r->sig[i];
        r->sig[i] = s;
        if (s == was || !s) continue;
        if (live == ZUI_LIVE_POLITE && !r->primed) continue; /* the baseline */
        sb_t b = start(r);
        if (err) sb_s(&b, "Error: ");
        sb_n(&b, t, n);
        emit(r, r->buf, (err || live == ZUI_LIVE_ASSERTIVE) ? ZUI_SAY_INTERRUPT : ZUI_SAY_QUEUE);
    }
}

static void watch_held(zui_reader_t *r)
{
    zui_ui_t *ui = r->ui;
    for (uint32_t i = 0; i < ui->n; i++) {
        const zui_widget_t *w = &ui->w[i];
        if (w->kind != ZUI_W_HELD) continue;
        uint32_t s = 0;
        if (shown(r, (zui_id_t) i)) {
            s = fnv_byte(2166136261u, w->u.held.state);
            s = fnv(s, w->u.held.agent, slen(w->u.held.agent));
            zui_id_t meta = w->first;
            zui_id_t body = meta != ZUI_NONE ? ui->w[meta].next : ZUI_NONE;
            if (body != ZUI_NONE && ui->w[body].u.field.buf)
                s = fnv(s, ui->w[body].u.field.buf, ui->w[body].u.field.len);
            s |= 1u;
        }
        uint32_t was = r->sig[i];
        r->sig[i] = s;
        if (s == was || !s) continue;
        if (w->u.held.state == 1) {
            say_item(r, (zui_id_t) i, -1, ZUI_SAY_QUEUE);
        } else if (was && w->u.held.state == 2) {
            emit(r, "AI suggestion accepted", ZUI_SAY_QUEUE);
        } else if (was && w->u.held.state == 3) {
            emit(r, "AI suggestion rejected", ZUI_SAY_QUEUE);
        }
    }
}

static uint32_t menu_items(const zui_menu_t *m, int32_t upto, uint32_t *pos)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < m->n; i++) {
        if (m->item[i][0] == '-' && m->item[i][1] == 0) continue;
        n++;
        if ((int32_t) i == upto) *pos = n;
    }
    return n;
}

static void say_menu_item(zui_reader_t *r, sb_t *b)
{
    const zui_menu_t *m = &r->ui->menu;
    if (m->hot < 0 || (uint32_t) m->hot >= m->n) return;
    uint32_t pos = 0, n = menu_items(m, m->hot, &pos);
    part(b, m->item[m->hot]);
    part(b, "menu item");
    if (m->off[m->hot]) part(b, "disabled");
    if (r->verbosity >= ZUI_VERB_MEDIUM && pos) part_pos(b, pos, n);
    if (r->verbosity >= ZUI_VERB_HIGH && m->key[m->hot][0]) {
        part(b, "shortcut ");
        spoken_keys(b, m->key[m->hot]);
    }
}

static void say_option(zui_reader_t *r, sb_t *b)
{
    const zui_palette_t *p = &r->ui->palette;
    if (!p->n_match) {
        part(b, "no actions match");
        return;
    }
    if (p->hot < 0 || (uint32_t) p->hot >= p->n_match) return;
    const zui_palette_item_t *it = &p->item[p->match[p->hot]];
    part(b, it->name);
    part(b, "option");
    if (r->verbosity >= ZUI_VERB_MEDIUM) part_pos(b, (uint64_t) p->hot + 1u, p->n_match);
    if (r->verbosity >= ZUI_VERB_HIGH) {
        if (it->agent && it->agent[0]) {
            part(b, "by ");
            sb_s(b, it->agent);
        }
        if (it->est_tokens) {
            part(b, "about ");
            sb_u(b, it->est_tokens);
            sb_s(b, " tokens");
        }
    }
}

static void watch_menus(zui_reader_t *r)
{
    zui_ui_t *ui = r->ui;
    if (ui->menu.open) {
        bool opened = !r->menu_open;
        if (opened || ui->menu.hot != r->menu_hot) {
            sb_t b = start(r);
            if (opened) {
                uint32_t pos = 0;
                part(&b, "menu");
                if (r->verbosity >= ZUI_VERB_MEDIUM) {
                    uint32_t n = menu_items(&ui->menu, -1, &pos);
                    if (b.n) sb_s(&b, ", ");
                    sb_u(&b, n);
                    sb_s(&b, n == 1 ? " item" : " items");
                }
            }
            say_menu_item(r, &b);
            emit(r, r->buf, ZUI_SAY_INTERRUPT);
        }
    }
    r->menu_open = ui->menu.open;
    r->menu_hot = ui->menu.open ? ui->menu.hot : -1;
    if (ui->palette.open) {
        bool opened = !r->palette_open;
        if (opened || ui->palette.hot != r->palette_hot || ui->palette.n_match != r->palette_n) {
            sb_t b = start(r);
            if (opened) {
                part(&b, "Ask");
                part(&b, "dialog");
                if (r->verbosity >= ZUI_VERB_MEDIUM) part(&b, ui->palette.context);
            }
            say_option(r, &b);
            emit(r, r->buf, ZUI_SAY_INTERRUPT);
        }
    }
    r->palette_open = ui->palette.open;
    r->palette_hot = ui->palette.open ? ui->palette.hot : -1;
    r->palette_n = ui->palette.open ? ui->palette.n_match : 0;
}

static void say_focus(zui_reader_t *r, bool with_group, zui_say_t how)
{
    zui_id_t f = r->ui->focus;
    if (!valid(r, f)) return;
    sb_t b = start(r);
    zui_id_t g = group_of(r, f);
    if (with_group && r->verbosity >= ZUI_VERB_MEDIUM && g != ZUI_NONE && g != r->focus_group)
        describe(r, g, -1, &b);
    describe(r, f, -1, &b);
    r->focus_group = g;
    emit(r, r->buf, how);
}

static void watch_focus(zui_reader_t *r)
{
    zui_ui_t *ui = r->ui;
    zui_id_t f = valid(r, ui->focus) ? ui->focus : ZUI_NONE;
    int32_t sel = f != ZUI_NONE && ui->w[f].kind == ZUI_W_LIST ? ui->w[f].u.list.sel : -1;
    if (f != r->focus) {
        if (f != ZUI_NONE) {
            say_focus(r, true, ZUI_SAY_INTERRUPT);
            r->cursor = f;
            r->cursor_row = -1;
        }
    } else if (f != ZUI_NONE && sel != r->focus_sel && sel >= 0) {
        say_item(r, f, sel, ZUI_SAY_INTERRUPT);
        r->cursor = f;
        r->cursor_row = sel;
    }
    r->focus = f;
    r->focus_sel = sel;
}

void zui_reader_update(zui_reader_t *r)
{
    if (!r || !r->ui) return;
    zui_ui_t *ui = r->ui;
    r->said = false;
    if (ui->n < r->n_seen) {
        /* the tree was rebuilt: forget what was seen */
        mset(r->sig, sizeof r->sig);
        r->cursor = r->focus = r->focus_group = ZUI_NONE;
        r->cursor_row = r->focus_sel = -1;
        r->primed = false;
    }
    r->n_seen = ui->n;
    /* drop marks on widgets that no longer exist */
    uint32_t k = 0;
    for (uint32_t i = 0; i < r->n_mark; i++)
        if (valid(r, r->mark[i].id) && W(r, r->mark[i].id)->kind == r->mark[i].kind)
            r->mark[k++] = r->mark[i];
    r->n_mark = k;
    watch_lines(r);
    watch_focus(r);
    watch_menus(r);
    watch_held(r);
    r->primed = true;
}

/* ---- R2: the reader cursor -------------------------------------------------------- */

static int32_t index_of(const zui_reader_t *r, const zui_id_t *it, uint32_t n)
{
    if (!valid(r, r->cursor)) return -1;
    for (uint32_t i = 0; i < n; i++)
        if (it[i] == r->cursor) return (int32_t) i;
    return -1;
}

static int32_t rows_of(const zui_reader_t *r, zui_id_t id)
{
    return W(r, id)->kind == ZUI_W_LIST ? W(r, id)->u.list.rows : 0;
}

static bool is_heading(const zui_reader_t *r, zui_id_t id)
{
    return heading_of(r, id) != 0;
}

static bool is_landmark(const zui_reader_t *r, zui_id_t id)
{
    return landmark_of(r, id) != ZUI_LANDMARK_NONE;
}

static bool is_held(const zui_reader_t *r, zui_id_t id)
{
    return W(r, id)->kind == ZUI_W_HELD;
}

bool zui_reader_move(zui_reader_t *r, zui_read_move_t how)
{
    if (!r || !r->ui) return false;
    r->said = false;
    zui_id_t it[ZUI_WIDGETS];
    uint32_t n = items(r, it);
    if (!n) {
        emit(r, "nothing on screen", ZUI_SAY_INTERRUPT);
        return false;
    }
    int32_t ci = index_of(r, it, n);
    int32_t row = ci >= 0 ? r->cursor_row : -1;
    if (ci >= 0 && (row < -1 || row >= rows_of(r, it[ci]))) row = -1;
    zui_id_t to = ZUI_NONE;
    int32_t to_row = -1;
    const char *none = 0;
    bool (*want)(const zui_reader_t *, zui_id_t) = 0;
    bool forward = true;
    switch (how) {
    case ZUI_READ_NEXT:
        if (ci < 0) {
            to = it[0];
        } else if (row + 1 < rows_of(r, it[ci])) {
            to = it[ci];
            to_row = row + 1;
        } else if ((uint32_t) ci + 1u < n) {
            to = it[ci + 1];
        } else {
            none = "end";
        }
        break;
    case ZUI_READ_PREV:
        if (ci < 0) {
            to = it[n - 1];
        } else if (row >= 0) {
            to = it[ci];
            to_row = row - 1;
        } else if (ci > 0) {
            to = it[ci - 1];
            to_row = rows_of(r, to) - 1; /* back into a list: its last row */
        } else {
            none = "top";
        }
        break;
    case ZUI_READ_FIRST:
        to = it[0];
        break;
    case ZUI_READ_LAST:
        to = it[n - 1];
        break;
    case ZUI_READ_NEXT_HEADING:
    case ZUI_READ_PREV_HEADING:
        want = is_heading;
        forward = how == ZUI_READ_NEXT_HEADING;
        none = forward ? "no next heading" : "no previous heading";
        break;
    case ZUI_READ_NEXT_LANDMARK:
    case ZUI_READ_PREV_LANDMARK:
        want = is_landmark;
        forward = how == ZUI_READ_NEXT_LANDMARK;
        none = forward ? "no next landmark" : "no previous landmark";
        break;
    case ZUI_READ_NEXT_HELD:
    case ZUI_READ_PREV_HELD:
        want = is_held;
        forward = how == ZUI_READ_NEXT_HELD;
        none = forward ? "no next AI suggestion" : "no previous AI suggestion";
        break;
    }
    if (want) {
        if (forward) {
            for (int32_t i = ci + 1; i < (int32_t) n && to == ZUI_NONE; i++)
                if (want(r, it[i])) to = it[i];
        } else {
            for (int32_t i = (ci < 0 ? (int32_t) n : ci) - 1; i >= 0 && to == ZUI_NONE; i--)
                if (want(r, it[i])) to = it[i];
        }
    }
    if (to == ZUI_NONE) {
        emit(r, none ? none : "end", ZUI_SAY_INTERRUPT);
        return false;
    }
    r->cursor = to;
    r->cursor_row = to_row;
    say_item(r, to, to_row, ZUI_SAY_INTERRUPT);
    return true;
}

void zui_reader_read_all(zui_reader_t *r, bool from_cursor)
{
    if (!r || !r->ui) return;
    r->said = false;
    zui_id_t it[ZUI_WIDGETS];
    uint32_t n = items(r, it);
    int32_t ci = from_cursor ? index_of(r, it, n) : -1;
    uint32_t from = ci > 0 ? (uint32_t) ci : 0;
    if (from >= n) {
        emit(r, "nothing on screen", ZUI_SAY_INTERRUPT);
        return;
    }
    for (uint32_t i = from; i < n; i++) {
        say_item(r, it[i], -1, ZUI_SAY_INTERRUPT); /* only the first interrupts (R5) */
        r->cursor = it[i];
        r->cursor_row = -1;
    }
}

void zui_reader_where(zui_reader_t *r)
{
    if (!r || !r->ui) return;
    r->said = false;
    if (!valid(r, r->ui->focus)) {
        emit(r, "no focus", ZUI_SAY_INTERRUPT);
        return;
    }
    r->focus_group = ZUI_NONE; /* say the group again */
    say_focus(r, true, ZUI_SAY_INTERRUPT);
}

bool zui_reader_activate(zui_reader_t *r)
{
    if (!r || !r->ui) return false;
    r->said = false;
    zui_ui_t *ui = r->ui;
    zui_id_t id = r->cursor;
    if (!valid(r, id) || !shown(r, id)) {
        emit(r, "nothing to activate", ZUI_SAY_INTERRUPT);
        return false;
    }
    zui_focus(ui, id, true);
    if (ui->focus != id) {
        emit(r, "not a control", ZUI_SAY_INTERRUPT);
        return false;
    }
    if (W(r, id)->kind == ZUI_W_LIST && r->cursor_row >= 0) zui_list_select(ui, id, r->cursor_row);
    return true;
}

/* ---- R6: keys ---------------------------------------------------------------------- */

bool zui_reader_event(zui_reader_t *r, const zui_event_t *e)
{
    if (!r || !e || (e->type != ZUI_EV_KEY_DOWN && e->type != ZUI_EV_KEY_UP)) return false;
    uint8_t want = ZUI_MOD_CTRL | ZUI_MOD_ALT;
    if ((e->mods & want) != want || (e->mods & ZUI_MOD_META)) return false;
    bool shift = (e->mods & ZUI_MOD_SHIFT) != 0;
    uint16_t k = e->key;
    if (k >= 'A' && k <= 'Z') k = (uint16_t) (k - 'A' + 'a');
    bool bound = false;
    switch (k) {
    case ZUI_KEY_RIGHT:
    case ZUI_KEY_LEFT:
    case ZUI_KEY_DOWN:
    case ZUI_KEY_UP:
    case ZUI_KEY_PAGE_DOWN:
    case ZUI_KEY_PAGE_UP:
    case ZUI_KEY_HOME:
    case ZUI_KEY_END:
    case ZUI_KEY_SPACE:
    case ZUI_KEY_ENTER:
    case 'a':
    case 'f':
    case 'r':
    case 'v':
    case 's':
    case 'n':
        bound = r->on;
        break;
    case 'z':
        bound = true;
        break;
    default:
        break;
    }
    if (!bound) return false;
    if (e->type == ZUI_EV_KEY_UP) return true;
    switch (k) {
    case ZUI_KEY_RIGHT:
        zui_reader_move(r, ZUI_READ_NEXT);
        break;
    case ZUI_KEY_LEFT:
        zui_reader_move(r, ZUI_READ_PREV);
        break;
    case ZUI_KEY_DOWN:
        zui_reader_move(r, ZUI_READ_NEXT_HEADING);
        break;
    case ZUI_KEY_UP:
        zui_reader_move(r, ZUI_READ_PREV_HEADING);
        break;
    case ZUI_KEY_PAGE_DOWN:
        zui_reader_move(r, ZUI_READ_NEXT_LANDMARK);
        break;
    case ZUI_KEY_PAGE_UP:
        zui_reader_move(r, ZUI_READ_PREV_LANDMARK);
        break;
    case ZUI_KEY_HOME:
        zui_reader_move(r, ZUI_READ_FIRST);
        break;
    case ZUI_KEY_END:
        zui_reader_move(r, ZUI_READ_LAST);
        break;
    case ZUI_KEY_SPACE:
    case ZUI_KEY_ENTER:
        zui_reader_activate(r);
        break;
    case 'a':
        zui_reader_read_all(r, shift);
        break;
    case 'f':
        zui_reader_where(r);
        break;
    case 'r':
        zui_reader_repeat(r);
        break;
    case 'v': {
        r->verbosity =
            (zui_verbosity_t) (((uint32_t) r->verbosity + 1u) % (uint32_t) ZUI_VERB_COUNT);
        sb_t b = start(r);
        sb_s(&b, "verbosity ");
        sb_s(&b, zui_verbosity_name(r->verbosity));
        r->said = false;
        emit(r, r->buf, ZUI_SAY_INTERRUPT);
        break;
    }
    case 's':
        zui_reader_stop(r);
        break;
    case 'n':
        zui_reader_move(r, shift ? ZUI_READ_PREV_HELD : ZUI_READ_NEXT_HELD);
        break;
    default: /* 'z' */
        zui_reader_set_on(r, !r->on);
        break;
    }
    return true;
}

/* ---- R5: the speech engine adapter -------------------------------------------------- */

void zui_reader_voice_speak(const char *text, zui_say_t how, void *ctx)
{
    zui_reader_voice_t *v = (zui_reader_voice_t *) ctx;
    if (!v) return;
    if (how == ZUI_SAY_INTERRUPT && v->stop) {
        v->stop(v->engine);
        v->stopped++;
    }
    if (!text || !text[0]) return;
    if (v->caption) v->caption(v->engine, text);
    char tidied[1024];
    if (v->tidy) {
        uint32_t n = 0;
        while (text[n]) n++;
        if (v->tidy(text, n, tidied, sizeof tidied, v->tidy_words) == 0) return; /* only markup */
        text = tidied;
    }
    if (v->say && v->say(v->engine, text, v->lang) == 0)
        v->said++;
    else
        v->failed++;
}
