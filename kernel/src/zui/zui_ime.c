/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_ime.c — the input-method framework and the built-in engines
 * (zui_ime.h). */
#include "zui_ime.h"

/* ---- small helpers ------------------------------------------------------------- */

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static bool streq(const char *a, const char *b)
{
    uint32_t i = 0;
    for (; a[i] && a[i] == b[i]; i++) {
    }
    return a[i] == b[i];
}

static void zero(void *p, uint32_t n)
{
    uint8_t *b = (uint8_t *) p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

static void copy(void *d, const void *s, uint32_t n)
{
    uint8_t *a = (uint8_t *) d;
    const uint8_t *b = (const uint8_t *) s;
    for (uint32_t i = 0; i < n; i++) a[i] = b[i];
}

static void scopy(char *dst, uint32_t cap, const char *src)
{
    uint32_t n = zui_utf8_fit(src, slen(src), cap - 1);
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
    dst[n] = 0;
}

/* ---- engine helpers ---------------------------------------------------------------- */

void zui_ime_set_preedit(zui_ime_t *ime, const char *utf8)
{
    scopy(ime->pre, ZUI_IME_PRE_MAX, utf8 ? utf8 : "");
    ime->caret = slen(ime->pre);
    if (!ime->pre[0]) zui_ime_cands_clear(ime);
}

void zui_ime_send(zui_ime_t *ime, const char *utf8)
{
    uint32_t n = slen(utf8);
    uint32_t room = ZUI_IME_COMMIT_MAX - 1 - ime->ncommit;
    n = zui_utf8_fit(utf8, n, room);
    for (uint32_t i = 0; i < n; i++) ime->commit[ime->ncommit++] = utf8[i];
    ime->commit[ime->ncommit] = 0;
}

void zui_ime_send_cp(zui_ime_t *ime, uint32_t cp)
{
    char b[5];
    uint32_t n = zui_km_utf8_put(b, cp);
    b[n] = 0;
    zui_ime_send(ime, b);
}

void zui_ime_cands_clear(zui_ime_t *ime)
{
    ime->ncand = 0;
    ime->sel = 0;
}

bool zui_ime_cand_add(zui_ime_t *ime, const char *utf8, uint32_t len, uint8_t used)
{
    if (ime->ncand >= ZUI_IME_CAND_MAX || len == 0 || len >= ZUI_IME_CAND_BYTES) return false;
    zui_ime_cand_t *c = &ime->cand[ime->ncand++];
    for (uint32_t i = 0; i < len; i++) c->text[i] = utf8[i];
    c->text[len] = 0;
    c->used = used;
    return true;
}

static uint32_t page_first(const zui_ime_t *ime)
{
    return ime->sel - ime->sel % ZUI_IME_PAGE;
}

bool zui_ime_cand_nav(zui_ime_t *ime, uint16_t key)
{
    if (ime->ncand == 0) return false;
    uint32_t last = ime->ncand - 1;
    switch (key) {
    case ZUI_KEY_LEFT:
    case ZUI_KEY_UP:
        if (ime->sel > 0) ime->sel--;
        return true;
    case ZUI_KEY_RIGHT:
    case ZUI_KEY_DOWN:
        if (ime->sel < last) ime->sel++;
        return true;
    case ZUI_KEY_PAGE_UP:
        ime->sel = page_first(ime) >= ZUI_IME_PAGE ? page_first(ime) - ZUI_IME_PAGE : 0;
        return true;
    case ZUI_KEY_PAGE_DOWN:
        if (page_first(ime) + ZUI_IME_PAGE <= last) ime->sel = page_first(ime) + ZUI_IME_PAGE;
        return true;
    case ZUI_KEY_HOME:
        ime->sel = 0;
        return true;
    case ZUI_KEY_END:
        ime->sel = last;
        return true;
    default:
        return false;
    }
}

int32_t zui_ime_cand_digit(const zui_ime_t *ime, uint32_t cp)
{
    if (cp < '1' || cp > '9') return -1;
    uint32_t i = page_first(ime) + (cp - '1');
    return i < ime->ncand ? (int32_t) i : -1;
}

/* ---- the framework ------------------------------------------------------------------ */

typedef struct {
    zui_event_t *out;
    uint32_t cap, n;
    uint32_t ms;
    uint8_t mods;
    zui_ime_t *ime;
} sink_t;

static void put(sink_t *s, const zui_event_t *e)
{
    if (s->n >= s->cap) {
        s->ime->dropped++;
        return;
    }
    copy(&s->out[s->n++], e, (uint32_t) sizeof *e);
}

static void put_text(sink_t *s, uint8_t type, const char *t, uint32_t n)
{
    zui_event_t e;
    zero(&e, (uint32_t) sizeof e);
    e.type = type;
    e.ms = s->ms;
    e.mods = s->mods;
    n = zui_utf8_fit(t, n, ZUI_TEXT_EV_MAX);
    for (uint32_t i = 0; i < n; i++) e.text[i] = t[i];
    e.text[n] = 0;
    put(s, &e);
}

/* Send committed text, split on character boundaries. */
static void put_commit(zui_ime_t *ime, sink_t *s)
{
    uint32_t at = 0;
    while (at < ime->ncommit) {
        uint32_t k = zui_utf8_fit(ime->commit + at, ime->ncommit - at, ZUI_TEXT_EV_MAX);
        if (k == 0) k = ime->ncommit - at; /* cannot happen with valid UTF-8 */
        put_text(s, ZUI_EV_TEXT, ime->commit + at, k);
        at += k;
    }
    ime->ncommit = 0;
    ime->commit[0] = 0;
}

/* M2: report what changed since the app was last told. */
static void flush(zui_ime_t *ime, sink_t *s)
{
    bool changed = !streq(ime->pre, ime->sent);
    if (changed && !ime->pre[0]) {
        put_text(s, ZUI_EV_PREEDIT, "", 0);
        ime->sent[0] = 0;
        changed = false;
    }
    put_commit(ime, s);
    if (changed) {
        put_text(s, ZUI_EV_PREEDIT, ime->pre, slen(ime->pre));
        scopy(ime->sent, ZUI_IME_PRE_MAX, ime->pre);
    }
}

bool zui_ime_composing(const zui_ime_t *ime)
{
    return ime->pre[0] != 0;
}

const char *zui_ime_preedit(const zui_ime_t *ime)
{
    return ime->pre;
}

uint32_t zui_ime_cand_count(const zui_ime_t *ime)
{
    return ime->ncand;
}

const char *zui_ime_cand(const zui_ime_t *ime, uint32_t i)
{
    return i < ime->ncand ? ime->cand[i].text : 0;
}

static void end_composition(zui_ime_t *ime)
{
    zui_ime_set_preedit(ime, "");
    zui_ime_cands_clear(ime);
}

static void do_commit(zui_ime_t *ime)
{
    if (!zui_ime_composing(ime)) return;
    if (ime->eng && ime->eng->commit)
        ime->eng->commit(ime);
    else
        zui_ime_send(ime, ime->pre);
    end_composition(ime);
}

static void do_cancel(zui_ime_t *ime)
{
    if (!zui_ime_composing(ime)) return;
    if (ime->eng && ime->eng->cancel) ime->eng->cancel(ime);
    end_composition(ime);
}

void zui_ime_init(zui_ime_t *ime, const zui_ime_engine_t *eng)
{
    zero(ime, (uint32_t) sizeof *ime);
    ime->eng = eng;
    if (eng && eng->reset) eng->reset(ime);
}

static sink_t mk_sink(zui_ime_t *ime, zui_event_t *out, uint32_t cap, uint32_t ms, uint8_t mods)
{
    sink_t s = {out, cap, 0, ms, mods, ime};
    return s;
}

uint32_t zui_ime_use(zui_ime_t *ime, const zui_ime_engine_t *eng, zui_event_t *out, uint32_t cap)
{
    sink_t s = mk_sink(ime, out, cap, 0, 0);
    do_commit(ime);
    flush(ime, &s);
    ime->eng = eng;
    zero(&ime->st, (uint32_t) sizeof ime->st);
    if (eng && eng->reset) eng->reset(ime);
    return s.n;
}

uint32_t zui_ime_commit(zui_ime_t *ime, zui_event_t *out, uint32_t cap)
{
    sink_t s = mk_sink(ime, out, cap, 0, 0);
    do_commit(ime);
    flush(ime, &s);
    return s.n;
}

uint32_t zui_ime_cancel(zui_ime_t *ime, zui_event_t *out, uint32_t cap)
{
    sink_t s = mk_sink(ime, out, cap, 0, 0);
    do_cancel(ime);
    flush(ime, &s);
    return s.n;
}

uint32_t zui_ime_choose(zui_ime_t *ime, uint32_t i, zui_event_t *out, uint32_t cap)
{
    sink_t s = mk_sink(ime, out, cap, 0, 0);
    if (i < ime->ncand && ime->eng && ime->eng->select) {
        ime->sel = i;
        ime->eng->select(ime, i);
    }
    flush(ime, &s);
    return s.n;
}

static void keep(zui_ime_t *ime, uint16_t key)
{
    if (key < 1024u) ime->kept[key >> 5] |= 1u << (key & 31u);
}

static bool unkeep(zui_ime_t *ime, uint16_t key)
{
    if (key >= 1024u || !(ime->kept[key >> 5] & (1u << (key & 31u)))) return false;
    ime->kept[key >> 5] &= ~(1u << (key & 31u));
    return true;
}

static bool printable(uint16_t key)
{
    return key >= 0x20u && key < 0x7Fu;
}

/* Characters passed through untouched, gathered into one TEXT. */
typedef struct {
    char b[ZUI_TEXT_EV_MAX + 1];
    uint32_t n;
} pass_t;

static void pass_flush(pass_t *p, sink_t *s)
{
    if (p->n) put_text(s, ZUI_EV_TEXT, p->b, p->n);
    p->n = 0;
}

static void feed_text(zui_ime_t *ime, sink_t *s, const char *t)
{
    pass_t p;
    p.n = 0;
    uint32_t n = slen(t), at = 0;
    while (at < n) {
        uint32_t cp;
        uint32_t used = zui_km_utf8_get(t + at, n - at, &cp);
        if (ime->eng->text(ime, cp)) {
            pass_flush(&p, s);
            flush(ime, s);
        } else {
            if (zui_ime_composing(ime)) {
                pass_flush(&p, s);
                do_commit(ime);
                flush(ime, s);
            }
            for (uint32_t i = 0; i < used && p.n < ZUI_TEXT_EV_MAX; i++) p.b[p.n++] = t[at + i];
        }
        at += used;
    }
    pass_flush(&p, s);
}

uint32_t zui_ime_feed(zui_ime_t *ime, const zui_event_t *e, zui_event_t *out, uint32_t cap)
{
    sink_t s = mk_sink(ime, out, cap, e->ms, e->mods);
    if (!ime->eng) {
        put(&s, e);
        return s.n;
    }
    switch (e->type) {
    case ZUI_EV_KEY_DOWN: {
        bool shortcut = (e->mods & (ZUI_MOD_CTRL | ZUI_MOD_ALT | ZUI_MOD_META | ZUI_MOD_CMD)) != 0;
        if (zui_ime_composing(ime)) {
            if (!shortcut && printable(e->key)) {
                keep(ime, e->key); /* M3: its text follows */
                break;
            }
            if (!shortcut && ime->eng->key(ime, e->key, e->mods)) {
                keep(ime, e->key);
                flush(ime, &s);
                break;
            }
            do_commit(ime);
            flush(ime, &s);
            put(&s, e);
            break;
        }
        if (ime->eng->key(ime, e->key, e->mods)) {
            keep(ime, e->key);
            flush(ime, &s);
            break;
        }
        put(&s, e);
        break;
    }
    case ZUI_EV_KEY_UP:
        if (!unkeep(ime, e->key)) put(&s, e);
        break;
    case ZUI_EV_TEXT:
        feed_text(ime, &s, e->text);
        break;
    case ZUI_EV_FOCUS_OUT:
        do_commit(ime);
        flush(ime, &s);
        put(&s, e);
        break;
    default:
        put(&s, e);
        break;
    }
    return s.n;
}

/* ---- M4 ------------------------------------------------------------------------------ */

void zui_ime_set_caret(zui_ime_t *ime, int32_t x, int32_t y, int32_t h)
{
    ime->caret_x = x;
    ime->caret_y = y;
    ime->caret_h = h;
}

void zui_ime_window(const zui_ime_t *ime, zui_ime_window_t *w)
{
    zero(w, (uint32_t) sizeof *w);
    w->x = ime->caret_x;
    w->y = ime->caret_y + ime->caret_h;
    if (ime->ncand == 0) return;
    w->visible = true;
    w->sel = ime->sel;
    w->page = ime->sel / ZUI_IME_PAGE;
    w->pages = (ime->ncand + ZUI_IME_PAGE - 1) / ZUI_IME_PAGE;
    w->first = w->page * ZUI_IME_PAGE;
    w->count = ime->ncand - w->first;
    if (w->count > ZUI_IME_PAGE) w->count = ZUI_IME_PAGE;
}

/* ---- hex entry ---------------------------------------------------------------------- */

static int32_t hexval(uint32_t c)
{
    if (c >= '0' && c <= '9') return (int32_t) (c - '0');
    if (c >= 'a' && c <= 'f') return (int32_t) (c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (int32_t) (c - 'A' + 10);
    return -1;
}

static bool hex_valid(uint32_t v)
{
    return v != 0 && v < 0x110000u && (v < 0xD800u || v > 0xDFFFu);
}

static void hex_show(zui_ime_t *ime)
{
    zui_ime_hex_t *h = &ime->st.hex;
    char b[8];
    b[0] = 'u';
    for (uint32_t i = 0; i < h->digits; i++) {
        uint32_t d = (h->value >> (4 * (h->digits - 1 - i))) & 0xFu;
        b[1 + i] = (char) (d < 10 ? '0' + d : 'a' + d - 10);
    }
    b[1 + h->digits] = 0;
    zui_ime_set_preedit(ime, b);
    zui_ime_cands_clear(ime);
    if (h->digits && hex_valid(h->value)) {
        char c[5];
        uint32_t n = zui_km_utf8_put(c, h->value);
        zui_ime_cand_add(ime, c, n, 0);
    }
}

static void hex_reset(zui_ime_t *ime)
{
    ime->st.hex.value = 0;
    ime->st.hex.digits = 0;
}

static void hex_finish(zui_ime_t *ime)
{
    zui_ime_hex_t *h = &ime->st.hex;
    if (h->digits && hex_valid(h->value)) zui_ime_send_cp(ime, h->value);
    hex_reset(ime);
    end_composition(ime);
}

static void hex_cancel(zui_ime_t *ime)
{
    hex_reset(ime);
    end_composition(ime);
}

static bool hex_key(zui_ime_t *ime, uint16_t key, uint8_t mods)
{
    if (!zui_ime_composing(ime)) {
        if ((key == 'u' || key == 'U') && (mods & ZUI_MOD_CTRL) && (mods & ZUI_MOD_SHIFT)) {
            hex_reset(ime);
            hex_show(ime);
            return true;
        }
        return false;
    }
    zui_ime_hex_t *h = &ime->st.hex;
    switch (key) {
    case ZUI_KEY_BACKSPACE:
        if (h->digits == 0) {
            hex_cancel(ime);
            return true;
        }
        h->value >>= 4;
        h->digits--;
        hex_show(ime);
        return true;
    case ZUI_KEY_ESCAPE:
        hex_cancel(ime);
        return true;
    case ZUI_KEY_ENTER:
        hex_finish(ime);
        return true;
    default:
        return false;
    }
}

static bool hex_text(zui_ime_t *ime, uint32_t cp)
{
    if (!zui_ime_composing(ime)) return false;
    zui_ime_hex_t *h = &ime->st.hex;
    int32_t d = hexval(cp);
    if (d >= 0) {
        if (h->digits < 6) {
            h->value = (h->value << 4) | (uint32_t) d;
            h->digits++;
            hex_show(ime);
        }
        return true;
    }
    if (cp == ' ') hex_finish(ime);
    return true; /* anything else is ignored while entering a code */
}

static void hex_select(zui_ime_t *ime, uint32_t i)
{
    (void) i;
    hex_finish(ime);
}

const zui_ime_engine_t zui_ime_hex = {"hex",    "Unicode hex entry", 0,          hex_reset, hex_key,
                                      hex_text, hex_finish,          hex_cancel, hex_select};

/* ---- compose -------------------------------------------------------------------------- */

static const zui_compose_seq_t COMPOSE_SEQ[] = {
    {"'a", "á"},  {"'e", "é"},  {"'i", "í"},  {"'o", "ó"},  {"'u", "ú"},  {"'y", "ý"},
    {"'A", "Á"},  {"'E", "É"},  {"'I", "Í"},  {"'O", "Ó"},  {"'U", "Ú"},  {"`a", "à"},
    {"`e", "è"},  {"`i", "ì"},  {"`o", "ò"},  {"`u", "ù"},  {"^a", "â"},  {"^e", "ê"},
    {"^i", "î"},  {"^o", "ô"},  {"^u", "û"},  {"\"a", "ä"}, {"\"e", "ë"}, {"\"i", "ï"},
    {"\"o", "ö"}, {"\"u", "ü"}, {"\"A", "Ä"}, {"\"O", "Ö"}, {"\"U", "Ü"}, {"~n", "ñ"},
    {"~N", "Ñ"},  {"~a", "ã"},  {"~o", "õ"},  {",c", "ç"},  {",C", "Ç"},  {"ss", "ß"},
    {"ae", "æ"},  {"AE", "Æ"},  {"oe", "œ"},  {"o/", "ø"},  {"O/", "Ø"},  {"oa", "å"},
    {"oc", "©"},  {"or", "®"},  {"tm", "™"},  {"e=", "€"},  {"L-", "£"},  {"Y=", "¥"},
    {"c/", "¢"},  {"<<", "«"},  {">>", "»"},  {"!!", "¡"},  {"??", "¿"},  {"oo", "°"},
    {"+-", "±"},  {"xx", "×"},  {":-", "÷"},  {"12", "½"},  {"14", "¼"},  {"34", "¾"},
    {"..", "…"},  {"---", "—"}, {"--.", "–"}, {"->", "→"},  {"<-", "←"},  {"=/", "≠"},
    {"<=", "≤"},  {">=", "≥"},
};

const zui_compose_table_t zui_compose_default = {COMPOSE_SEQ,
                                                 sizeof COMPOSE_SEQ / sizeof COMPOSE_SEQ[0]};

#define COMPOSE_MARK "·" /* shown while a sequence is being typed */

static const zui_compose_table_t *compose_table(const zui_ime_t *ime)
{
    const zui_compose_table_t *t = (const zui_compose_table_t *) ime->eng->data;
    return t ? t : &zui_compose_default;
}

static void compose_show(zui_ime_t *ime)
{
    char b[24] = COMPOSE_MARK;
    uint32_t m = slen(b);
    for (uint32_t i = 0; i < ime->st.compose.n; i++) b[m + i] = ime->st.compose.seq[i];
    b[m + ime->st.compose.n] = 0;
    zui_ime_set_preedit(ime, b);
}

static void compose_reset(zui_ime_t *ime)
{
    ime->st.compose.n = 0;
    ime->st.compose.seq[0] = 0;
}

/* exact: the matching entry or -1; longer: some entry continues the sequence */
static int32_t compose_find(const zui_ime_t *ime, bool *longer)
{
    const zui_compose_table_t *t = compose_table(ime);
    const zui_ime_compose_t *c = &ime->st.compose;
    int32_t exact = -1;
    *longer = false;
    for (uint32_t i = 0; i < t->n; i++) {
        const char *s = t->seq[i].seq;
        uint32_t k = 0;
        while (k < c->n && s[k] && s[k] == c->seq[k]) k++;
        if (k < c->n) continue;
        if (s[k] == 0) {
            if (exact < 0) exact = (int32_t) i;
        } else {
            *longer = true;
        }
    }
    return exact;
}

static void compose_commit(zui_ime_t *ime)
{
    bool longer;
    int32_t ex = compose_find(ime, &longer);
    if (ex >= 0)
        zui_ime_send(ime, compose_table(ime)->seq[ex].out);
    else
        zui_ime_send(ime, ime->st.compose.seq);
    compose_reset(ime);
    end_composition(ime);
}

static void compose_cancel(zui_ime_t *ime)
{
    compose_reset(ime);
    end_composition(ime);
}

static bool compose_key(zui_ime_t *ime, uint16_t key, uint8_t mods)
{
    (void) mods;
    if (!zui_ime_composing(ime)) {
        if (key != ZUI_KEY_COMPOSE) return false;
        compose_reset(ime);
        compose_show(ime);
        return true;
    }
    zui_ime_compose_t *c = &ime->st.compose;
    switch (key) {
    case ZUI_KEY_COMPOSE:
    case ZUI_KEY_ESCAPE:
        compose_cancel(ime);
        return true;
    case ZUI_KEY_BACKSPACE:
        if (c->n == 0) {
            compose_cancel(ime);
            return true;
        }
        c->n = zui_utf8_prev(c->seq, c->n);
        c->seq[c->n] = 0;
        compose_show(ime);
        return true;
    default:
        return false;
    }
}

static bool compose_text(zui_ime_t *ime, uint32_t cp)
{
    if (!zui_ime_composing(ime)) return false;
    zui_ime_compose_t *c = &ime->st.compose;
    char b[5];
    uint32_t k = zui_km_utf8_put(b, cp);
    if (c->n + k >= sizeof c->seq) {
        compose_commit(ime);
        return false;
    }
    for (uint32_t i = 0; i < k; i++) c->seq[c->n++] = b[i];
    c->seq[c->n] = 0;
    bool longer;
    int32_t ex = compose_find(ime, &longer);
    if (ex >= 0 && !longer) {
        zui_ime_send(ime, compose_table(ime)->seq[ex].out);
        compose_reset(ime);
        end_composition(ime);
    } else if (longer) {
        compose_show(ime);
    } else { /* matches nothing: send it as typed */
        zui_ime_send(ime, c->seq);
        compose_reset(ime);
        end_composition(ime);
    }
    return true;
}

const zui_ime_engine_t zui_ime_compose = {"compose",      "Compose sequences", &zui_compose_default,
                                          compose_reset,  compose_key,         compose_text,
                                          compose_commit, compose_cancel,      0};

/* ---- table-driven phonetic engine ------------------------------------------------------ */

/* SAMPLE DATA. A handful of Pinyin syllables and words, enough to show the
 * candidate path; not a dictionary. */
static const zui_ime_entry_t PINYIN_SAMPLE[] = {
    {"a", "啊 阿"},           {"de", "的 得 德"},
    {"guo", "国 过 果 锅"},   {"hao", "好 号 豪 浩 耗"},
    {"jian", "见 件 间 建"},  {"ma", "吗 妈 马 码 骂"},
    {"ni", "你 尼 泥 妮 逆"}, {"nihao", "你好"},
    {"ren", "人 认 任 仁"},   {"shi", "是 十 时 事 市 使 世 试 室 师"},
    {"wo", "我 握 窝"},       {"xie", "谢 写 些 鞋"},
    {"xiexie", "谢谢"},       {"zai", "在 再 载"},
    {"zaijian", "再见"},      {"zhong", "中 种 重 钟 终"},
    {"zhongguo", "中国"},
};

const zui_ime_dict_t zui_pinyin_sample_dict = {PINYIN_SAMPLE,
                                               sizeof PINYIN_SAMPLE / sizeof PINYIN_SAMPLE[0]};

static const zui_ime_dict_t *dict_of(const zui_ime_t *ime)
{
    const zui_ime_dict_t *d = (const zui_ime_dict_t *) ime->eng->data;
    return d ? d : &zui_pinyin_sample_dict;
}

static void add_cands(zui_ime_t *ime, const char *list, uint8_t used)
{
    uint32_t at = 0, n = slen(list);
    while (at < n) {
        while (at < n && list[at] == ' ') at++;
        uint32_t s = at;
        while (at < n && list[at] != ' ') at++;
        if (at > s) zui_ime_cand_add(ime, list + s, at - s, used);
    }
}

static bool starts(const char *s, uint32_t sn, const char *with, uint32_t wn)
{
    if (wn > sn) return false;
    for (uint32_t i = 0; i < wn; i++)
        if (s[i] != with[i]) return false;
    return true;
}

static void table_recompute(zui_ime_t *ime)
{
    zui_ime_table_t *t = &ime->st.table;
    t->in[t->n] = 0;
    zui_ime_cands_clear(ime);
    zui_ime_set_preedit(ime, t->in);
    if (t->n == 0) return;
    const zui_ime_dict_t *d = dict_of(ime);
    uint32_t L = 0;
    for (uint32_t i = 0; i < d->n; i++) {
        uint32_t k = slen(d->e[i].keys);
        if (k > L && starts(t->in, t->n, d->e[i].keys, k)) L = k;
    }
    if (L) {
        for (uint32_t i = 0; i < d->n; i++)
            if (slen(d->e[i].keys) == L && starts(t->in, t->n, d->e[i].keys, L))
                add_cands(ime, d->e[i].cands, (uint8_t) L);
    }
    if (L == 0 || L == t->n) {
        for (uint32_t i = 0; i < d->n; i++) {
            uint32_t k = slen(d->e[i].keys);
            if (k > t->n && starts(d->e[i].keys, k, t->in, t->n))
                add_cands(ime, d->e[i].cands, (uint8_t) t->n);
        }
    }
}

void zui_ime_table_reset(zui_ime_t *ime)
{
    ime->st.table.n = 0;
    ime->st.table.in[0] = 0;
}

void zui_ime_table_cancel(zui_ime_t *ime)
{
    zui_ime_table_reset(ime);
    end_composition(ime);
}

void zui_ime_table_commit(zui_ime_t *ime)
{
    zui_ime_table_t *t = &ime->st.table;
    t->in[t->n] = 0;
    zui_ime_send(ime, t->in);
    zui_ime_table_cancel(ime);
}

void zui_ime_table_select(zui_ime_t *ime, uint32_t i)
{
    if (i >= ime->ncand) return;
    zui_ime_table_t *t = &ime->st.table;
    zui_ime_send(ime, ime->cand[i].text);
    uint32_t used = ime->cand[i].used;
    if (used > t->n) used = t->n;
    for (uint32_t k = used; k <= t->n; k++) t->in[k - used] = t->in[k];
    t->n -= used;
    table_recompute(ime);
}

bool zui_ime_table_key(zui_ime_t *ime, uint16_t key, uint8_t mods)
{
    (void) mods;
    if (!zui_ime_composing(ime)) return false;
    zui_ime_table_t *t = &ime->st.table;
    switch (key) {
    case ZUI_KEY_BACKSPACE:
        if (t->n) t->n--;
        table_recompute(ime);
        return true;
    case ZUI_KEY_ESCAPE:
        zui_ime_table_cancel(ime);
        return true;
    case ZUI_KEY_ENTER:
        zui_ime_table_commit(ime);
        return true;
    default:
        return zui_ime_cand_nav(ime, key);
    }
}

bool zui_ime_table_text(zui_ime_t *ime, uint32_t cp)
{
    zui_ime_table_t *t = &ime->st.table;
    if (cp >= 'a' && cp <= 'z') {
        if (t->n < sizeof t->in - 1) {
            t->in[t->n++] = (char) cp;
            table_recompute(ime);
        }
        return true;
    }
    if (!zui_ime_composing(ime)) return false;
    if (cp == ' ') {
        if (ime->ncand)
            zui_ime_table_select(ime, ime->sel);
        else
            zui_ime_table_commit(ime);
        return true;
    }
    int32_t d = zui_ime_cand_digit(ime, cp);
    if (d >= 0) {
        zui_ime_table_select(ime, (uint32_t) d);
        return true;
    }
    if (cp >= '1' && cp <= '9') return true; /* no such candidate: ignore */
    if (cp == '-' || cp == '=') {            /* page keys */
        zui_ime_cand_nav(ime, cp == '-' ? ZUI_KEY_PAGE_UP : ZUI_KEY_PAGE_DOWN);
        return true;
    }
    return false;
}

const zui_ime_engine_t zui_ime_pinyin_sample = {
    "pinyin-sample",      "Pinyin (sample table)", &zui_pinyin_sample_dict,
    zui_ime_table_reset,  zui_ime_table_key,       zui_ime_table_text,
    zui_ime_table_commit, zui_ime_table_cancel,    zui_ime_table_select};
