/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* footlight_model.c — Footlight (see footlight_model.h). */
#include <stdio.h>
#include <string.h>

#include "footlight_model.h"

void footlight_init(footlight_t *f, zxm_link_t *link)
{
    memset(f, 0, sizeof *f);
    f->link = link;
    f->deck = 1;
    f->mode = FOOTLIGHT_ADVANCE;
}

static uint32_t len_of(const char *s, uint32_t max)
{
    uint32_t n = 0;
    while (s && n <= max && s[n]) n++;
    return n;
}

/* ---- FM1, FM2: the deck ---- */

static bool has_text(const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (s[i] != '\n' && s[i] != ' ' && s[i] != '\t' && s[i] != '\r') return true;
    return false;
}

/* FM7: each slide's place in the hall's run (FL5), from the rehearsal */
static void place_in_hall(footlight_t *f)
{
    int64_t secs[FOOTLIGHT_SLIDES], start[FOOTLIGHT_SLIDES];
    for (uint32_t i = 0; i < f->n; i++)
        secs[i] = f->slide[i].secs ? f->slide[i].secs : FOOTLIGHT_HALL_SECS;
    if (!f->n || !footlight_slide_start(secs, f->n, start)) {
        memset(f->start_ms, 0, sizeof f->start_ms);
        return;
    }
    for (uint32_t i = 0; i < f->n; i++) f->start_ms[i] = start[i] * 1000;
}

static void lay_out(footlight_t *f)
{
    for (uint32_t i = 0; i < f->n; i++) {
        footlight_slide_t *s = &f->slide[i];
        bool title = s->title[0] != 0, text = has_text(f->body + s->body_at, s->body_len);
        s->layout = title && text ? FOOTLIGHT_LAYOUT_BOTH
                    : title       ? FOOTLIGHT_LAYOUT_TITLE
                    : text        ? FOOTLIGHT_LAYOUT_TEXT
                                  : FOOTLIGHT_LAYOUT_BLANK;
    }
    place_in_hall(f);
}

static footlight_slide_t *new_slide(footlight_t *t)
{
    if (t->n >= FOOTLIGHT_SLIDES) return NULL;
    footlight_slide_t *s = &t->slide[t->n++];
    memset(s, 0, sizeof *s);
    s->body_at = t->body_len;
    return s;
}

/* "@ <seconds>": digits only, 1..FOOTLIGHT_SECS_MAX */
static bool read_secs(const char *s, uint32_t n, int64_t *v)
{
    int64_t x = 0;
    if (n < 1 || n > 4) return false;
    for (uint32_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        x = x * 10 + (s[i] - '0');
    }
    if (x < 1 || x > FOOTLIGHT_SECS_MAX) return false;
    *v = x;
    return true;
}

static int parse(footlight_t *t, const char *text, uint32_t len)
{
    footlight_slide_t *s = NULL;
    t->n = 0;
    t->body_len = 0;
    if (memchr(text, 0, len)) return ZXM_E_ARG;
    for (uint32_t at = 0; at < len;) {
        uint32_t e = at;
        while (e < len && text[e] != '\n') e++;
        const char *ln = text + at;
        uint32_t ll = e - at;
        if (ll >= 2 && ln[0] == '#' && ln[1] == ' ') {
            if (ll - 2u > FOOTLIGHT_TITLE) return ZXM_E_ARG;
            if (!(s = new_slide(t))) return ZXM_E_TOOBIG;
            memcpy(s->title, ln + 2, ll - 2u);
            s->title[ll - 2u] = 0;
        } else if (ll >= 2 && ln[0] == '@' && ln[1] == ' ') {
            int64_t v;
            if (!read_secs(ln + 2, ll - 2u, &v)) return ZXM_E_ARG;
            if (!s && !(s = new_slide(t))) return ZXM_E_TOOBIG;
            s->secs = v;
        } else if (s || ll) { /* blank lines before the first slide are nothing */
            if (!s && !(s = new_slide(t))) return ZXM_E_TOOBIG;
            memcpy(t->body + t->body_len, ln, ll); /* body_len + ll + 1 <= len + 1 */
            t->body_len += ll;
            t->body[t->body_len++] = '\n';
            s->body_len += ll + 1u;
        }
        at = e + 1u;
    }
    t->body[t->body_len] = 0;
    return ZXM_OK;
}

int footlight_set_text(footlight_t *f, const char *text, uint32_t len)
{
    static footlight_t t; /* the new deck, kept apart until it is whole (FM8) */
    if (!text && len) return ZXM_E_ARG;
    if (len > FOOTLIGHT_TEXT_MAX) return ZXM_E_TOOBIG;
    t = *f;
    int rc = parse(&t, text ? text : "", len);
    if (rc) return rc;
    lay_out(&t);
    t.cur = t.n ? 1u : 0u;
    *f = t;
    return ZXM_OK;
}

int32_t footlight_text(const footlight_t *f, char *out, uint32_t cap)
{
    uint32_t at = 0;
    if (!out || !cap) return -1;
    for (uint32_t i = 0; i < f->n; i++) {
        const footlight_slide_t *s = &f->slide[i];
        char head[FOOTLIGHT_TITLE + 16u];
        int h = snprintf(head, sizeof head, "# %s\n", s->title);
        if (h < 0 || at + (uint32_t) h >= cap) return -1;
        memcpy(out + at, head, (size_t) h);
        at += (uint32_t) h;
        if (s->secs) {
            h = snprintf(head, sizeof head, "@ %lld\n", (long long) s->secs);
            if (h < 0 || at + (uint32_t) h >= cap) return -1;
            memcpy(out + at, head, (size_t) h);
            at += (uint32_t) h;
        }
        if (at + s->body_len >= cap) return -1;
        memcpy(out + at, f->body + s->body_at, s->body_len);
        at += s->body_len;
    }
    out[at] = 0;
    return (int32_t) at;
}

static bool path_ok(const char *path)
{
    uint32_t n = len_of(path, FOOTLIGHT_PATH_MAX);
    return n >= 1 && n <= FOOTLIGHT_PATH_MAX;
}

/* One request's text: at most this many bytes, each escaping to at most
 * six ("\u0001"), well inside zxvd's request line with the path. */
#define PIECE 2600u

static int write_text(zxm_link_t *l, const char *path, const char *text, uint32_t len)
{
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t n = len - at;
        if (n > PIECE) n = zxm_utf8_cut(text + at, PIECE);
        if (!n && len) return ZXM_E_ARG;
        bool last = at + n >= len;
        zxm_begin(l, "files.write");
        zxm_p_str(l, "path", path);
        zxm_p_strn(l, "text", text + at, n);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
    } while (at < len);
    return ZXM_OK;
}

static char TEXT[FOOTLIGHT_TEXT_MAX + 1u];

int footlight_open(footlight_t *f, const char *path)
{
    static char chunk[FOOTLIGHT_TEXT_MAX + 1u];
    if (!path_ok(path)) return ZXM_E_ARG;
    zxm_link_t *l = f->link;
    uint32_t len = 0;
    uint64_t off = 0;
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_str(l, "path", path);
        zxm_p_u64(l, "offset", off);
        int rc = zxm_call(l);
        if (rc) return rc;
        uint64_t size, next_off;
        bool eof;
        int32_t t = zxm_get(l, l->result, "text");
        if (zxm_get_u64(l, l->result, "size", &size) ||
            zxm_get_u64(l, l->result, "next", &next_off) ||
            zxm_get_bool(l, l->result, "eof", &eof) || t < 0)
            return ZXM_E_PROTOCOL;
        if (size > FOOTLIGHT_TEXT_MAX) return ZXM_E_TOOBIG;
        int32_t n = w4_json_str(l->resp, &l->tok[t], chunk, sizeof chunk);
        if (n < 0 || len + (uint32_t) n > FOOTLIGHT_TEXT_MAX) return ZXM_E_TOOBIG;
        memcpy(TEXT + len, chunk, (size_t) n);
        len += (uint32_t) n;
        if (eof) break;
        if (next_off <= off) return ZXM_E_PROTOCOL;
        off = next_off;
    }
    int rc = footlight_set_text(f, TEXT, len);
    if (rc) return rc;
    memcpy(f->path, path, len_of(path, FOOTLIGHT_PATH_MAX) + 1u);
    return ZXM_OK;
}

int footlight_save(footlight_t *f, const char *path)
{
    if (!path_ok(path)) return ZXM_E_ARG;
    int32_t len = footlight_text(f, TEXT, sizeof TEXT);
    if (len < 0) return ZXM_E_TOOBIG;
    int rc = write_text(f->link, path, TEXT, (uint32_t) len);
    if (rc) return rc;
    memcpy(f->path, path, len_of(path, FOOTLIGHT_PATH_MAX) + 1u);
    return ZXM_OK;
}

bool footlight_record_fits(const footlight_t *f, uint32_t place, int64_t stamp_ms)
{
    if (place < 1 || place > f->n) return false;
    const footlight_slide_t *s = &f->slide[place - 1u];
    return footlight_slide_record_ok(f->deck, place, s->secs, s->layout, s->body_len, stamp_ms);
}

int footlight_set_deck(footlight_t *f, int64_t deck)
{
    if (deck < 1 || !footlight_slide_record_ok(deck, 1, 0, 0, 0, 0)) return ZXM_E_ARG;
    f->deck = deck;
    return ZXM_OK;
}

/* ---- FM3: the fit ---- */

int footlight_fit_window(footlight_t *f, int64_t winw, int64_t winh)
{
    footlight_fit_t x;
    x.winw = winw;
    x.winh = winh;
    x.w = footlight_fit_w(winw, winh);
    x.h = footlight_fit_h(winw, winh);
    if (x.w < 0 || x.h < 0) return ZXM_E_ARG;
    x.left = footlight_bar(winw, x.w);
    x.top = footlight_bar(winh, x.h);
    if (x.left < 0 || x.top < 0) return ZXM_E_ARG;
    f->fit = x;
    return ZXM_OK;
}

/* ---- FM4: rehearsal ---- */

int footlight_set_secs(footlight_t *f, uint32_t place, int64_t secs)
{
    if (place < 1 || place > f->n || secs < 0 || secs > FOOTLIGHT_SECS_MAX) return ZXM_E_ARG;
    f->slide[place - 1u].secs = secs;
    place_in_hall(f);
    return ZXM_OK;
}

int footlight_set_talk(footlight_t *f, int64_t talk)
{
    if (talk < 1 || talk > FOOTLIGHT_TALK_MAX) return ZXM_E_ARG;
    f->talk = talk;
    return ZXM_OK;
}

int footlight_rehearsal(const footlight_t *f, int64_t *over)
{
    int64_t secs[FOOTLIGHT_SLIDES];
    if (!over) return ZXM_E_ARG;
    if (!f->n || !f->talk) return ZXM_E_STATE;
    for (uint32_t i = 0; i < f->n; i++) {
        if (!f->slide[i].secs) return ZXM_E_STATE; /* a slide not rehearsed */
        secs[i] = f->slide[i].secs;
    }
    return footlight_rehearsal_over(secs, f->n, f->talk, over) ? ZXM_OK : ZXM_E_STATE;
}

int32_t footlight_clock(int64_t secs, char *out, uint32_t cap)
{
    if (!out || !cap) return -1;
    bool under = secs < 0;
    int64_t a = under ? -secs : secs;
    int64_t hms = a > INT64_MAX / 1000 ? FOOTLIGHT_REFUSED : footlight_hms(a * 1000);
    if (hms < 0) return -1;
    int n = snprintf(out, cap, "%s%lld:%02lld:%02lld", under ? "-" : "", (long long) (hms / 10000),
                     (long long) (hms / 100 % 100), (long long) (hms % 100));
    if (n < 0 || (uint32_t) n >= cap) return -1;
    return n;
}

/* ---- FM5: run order ---- */

int footlight_go(footlight_t *f, uint32_t place)
{
    if (place < 1 || place > f->n) return ZXM_E_ARG;
    f->cur = place;
    return ZXM_OK;
}

int footlight_set_mode(footlight_t *f, int64_t mode)
{
    if (mode != FOOTLIGHT_ADVANCE && mode != FOOTLIGHT_HOLD && mode != FOOTLIGHT_LOOP)
        return ZXM_E_ARG;
    f->mode = mode;
    return ZXM_OK;
}

int footlight_step(footlight_t *f)
{
    if (!f->cur) return ZXM_E_STATE; /* ended, or no deck */
    int64_t next = footlight_next(f->cur, f->n, f->mode);
    if (next < 0) return ZXM_E_STATE;
    f->cur = (uint32_t) next;
    return ZXM_OK;
}

/* ---- FM6: handout ---- */

#define PDF_MAX  (512u * 1024u)
#define PAGE_MAX 16384u /* one page's operators: 47 lines of 70 characters, escaped */

typedef struct {
    char *p;
    uint32_t n, cap;
    bool over;
} out_t;

static void put_mem(out_t *o, const char *s, uint32_t n)
{
    if (o->over || n > o->cap - o->n) {
        o->over = true;
        return;
    }
    memcpy(o->p + o->n, s, n);
    o->n += n;
}

static void put_str(out_t *o, const char *s)
{
    put_mem(o, s, (uint32_t) strlen(s));
}

static void put_ch(out_t *o, char c)
{
    put_mem(o, &c, 1);
}

/* v at least w digits wide, zeros before (w 0: as it is) */
static void put_int_w(out_t *o, int64_t v, uint32_t w)
{
    char d[24];
    uint32_t k = sizeof d;
    bool neg = v < 0;
    uint64_t u = neg ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    do {
        d[--k] = (char) ('0' + u % 10u);
        u /= 10u;
    } while (u && k > 1);
    while (sizeof d - k < w && k > 1) d[--k] = '0';
    if (neg) d[--k] = '-';
    put_mem(o, d + k, (uint32_t) (sizeof d - k));
}

static void put_int(out_t *o, int64_t v)
{
    put_int_w(o, v, 0);
}

/* text as a PDF string: ( ) \ escaped, US-ASCII only */
static void put_pdf_text(out_t *o, const char *s, uint32_t n)
{
    put_ch(o, '(');
    for (uint32_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char) s[i];
        if ((c & 0xc0) == 0x80) continue; /* the rest of a character shown as '?' */
        if (c >= 0x80)
            put_ch(o, '?');
        else if (c < 0x20 || c == 0x7f)
            put_ch(o, ' ');
        else {
            if (c == '(' || c == ')' || c == '\\') put_ch(o, '\\');
            put_ch(o, (char) c);
        }
    }
    put_ch(o, ')');
}

static char PDF[PDF_MAX];
static uint32_t PDF_LEN;
static char PAGE[PAGE_MAX];
static uint32_t AT[4u + 2u * FOOTLIGHT_HANDOUT_PAGES];

const char *footlight_handout_pdf(uint32_t *len)
{
    if (len) *len = PDF_LEN;
    return PDF;
}

typedef struct {
    out_t pdf, page;
    uint32_t pages, row;
} hand_t;

static void page_begin(hand_t *h)
{
    h->page.n = 0;
    h->row = 0;
    put_str(&h->page, "BT\n");
}

static void page_end(hand_t *h)
{
    uint32_t obj = 4u + 2u * h->pages;
    put_str(&h->page, "ET\n");
    if (h->page.over || h->pages >= FOOTLIGHT_HANDOUT_PAGES) {
        h->pdf.over = true;
        return;
    }
    AT[obj] = h->pdf.n;
    put_int(&h->pdf, obj);
    put_str(&h->pdf, " 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                     "/Resources << /Font << /F1 3 0 R >> >> /Contents ");
    put_int(&h->pdf, obj + 1u);
    put_str(&h->pdf, " 0 R >>\nendobj\n");
    AT[obj + 1u] = h->pdf.n;
    put_int(&h->pdf, obj + 1u);
    put_str(&h->pdf, " 0 obj\n<< /Length ");
    put_int(&h->pdf, h->page.n);
    put_str(&h->pdf, " >>\nstream\n");
    put_mem(&h->pdf, h->page.p, h->page.n);
    put_str(&h->pdf, "endstream\nendobj\n");
    h->pages++;
}

/* one line at the next row, a page turned first when this one is full */
static void page_line(hand_t *h, uint32_t pt, const char *s, uint32_t n)
{
    if (h->row >= FOOTLIGHT_HANDOUT_ROWS) {
        page_end(h);
        page_begin(h);
    }
    if (n) {
        put_str(&h->page, "/F1 ");
        put_int(&h->page, pt);
        put_str(&h->page, " Tf\n1 0 0 1 72 ");
        put_int(&h->page, 720 - 14 * (int64_t) h->row);
        put_str(&h->page, " Tm\n");
        put_pdf_text(&h->page, s, n);
        put_str(&h->page, " Tj\n");
    }
    h->row++;
}

/* a line of text cut every FOOTLIGHT_HANDOUT_COLS characters */
static void text_line(hand_t *h, const char *s, uint32_t n)
{
    uint32_t from = 0, chars = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (((unsigned char) s[i] & 0xc0) == 0x80) continue;
        if (chars == FOOTLIGHT_HANDOUT_COLS) {
            page_line(h, 11, s + from, i - from);
            from = i;
            chars = 0;
        }
        chars++;
    }
    page_line(h, 11, s + from, n - from);
}

static void slide_pages(hand_t *h, const footlight_t *f, uint32_t i)
{
    const footlight_slide_t *s = &f->slide[i];
    char clock[24], head[FOOTLIGHT_TITLE + 48u];
    int n;
    if (s->secs && footlight_clock(s->secs, clock, sizeof clock) > 0)
        n = snprintf(head, sizeof head, "%u. %s [%s]", (unsigned) (i + 1u), s->title, clock);
    else
        n = snprintf(head, sizeof head, "%u. %s", (unsigned) (i + 1u), s->title);
    page_begin(h);
    if (n < 0) {
        h->pdf.over = true;
        return;
    }
    page_line(h, 14, head, (uint32_t) n);
    page_line(h, 11, "", 0);
    const char *b = f->body + s->body_at;
    for (uint32_t at = 0; at < s->body_len;) {
        uint32_t e = at;
        while (e < s->body_len && b[e] != '\n') e++;
        text_line(h, b + at, e - at);
        at = e + 1u;
    }
    page_end(h);
}

/* the handout's PDF into PDF; its length, or -1 when it does not fit */
static int32_t handout_compose(const footlight_t *f)
{
    static hand_t h;
    memset(&h, 0, sizeof h);
    h.pdf = (out_t){PDF, 0, sizeof PDF, false};
    h.page = (out_t){PAGE, 0, sizeof PAGE, false};
    put_str(&h.pdf, "%PDF-1.4\n");
    AT[1] = h.pdf.n;
    put_str(&h.pdf, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    AT[3] = h.pdf.n;
    put_str(&h.pdf, "3 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>\nendobj\n");
    for (uint32_t i = 0; i < f->n && !h.pdf.over; i++) slide_pages(&h, f, i);
    if (h.pdf.over) return -1;
    AT[2] = h.pdf.n;
    put_str(&h.pdf, "2 0 obj\n<< /Type /Pages /Kids [");
    for (uint32_t p = 0; p < h.pages; p++) {
        if (p) put_ch(&h.pdf, ' ');
        put_int(&h.pdf, 4 + 2 * (int64_t) p);
        put_str(&h.pdf, " 0 R");
    }
    put_str(&h.pdf, "] /Count ");
    put_int(&h.pdf, h.pages);
    put_str(&h.pdf, " >>\nendobj\n");
    uint32_t nobj = 3u + 2u * h.pages, xref = h.pdf.n;
    put_str(&h.pdf, "xref\n0 ");
    put_int(&h.pdf, nobj + 1u);
    put_str(&h.pdf, "\n0000000000 65535 f \n");
    for (uint32_t k = 1; k <= nobj; k++) {
        put_int_w(&h.pdf, AT[k], 10);
        put_str(&h.pdf, " 00000 n \n");
    }
    put_str(&h.pdf, "trailer\n<< /Size ");
    put_int(&h.pdf, nobj + 1u);
    put_str(&h.pdf, " /Root 1 0 R >>\nstartxref\n");
    put_int(&h.pdf, xref);
    put_str(&h.pdf, "\n%%EOF\n");
    return h.pdf.over ? -1 : (int32_t) h.pdf.n;
}

static bool ends_pdf(const char *path)
{
    uint32_t n = len_of(path, FOOTLIGHT_PATH_MAX);
    if (n < 5) return false;
    const char *e = path + n - 4;
    return e[0] == '.' && (e[1] | 32) == 'p' && (e[2] | 32) == 'd' && (e[3] | 32) == 'f';
}

int footlight_handout(footlight_t *f, const char *path)
{
    if (!path_ok(path) || !ends_pdf(path)) return ZXM_E_ARG;
    if (!f->n) return ZXM_E_STATE;
    int32_t len = handout_compose(f);
    if (len < 0) {
        PDF_LEN = 0;
        return ZXM_E_TOOBIG;
    }
    PDF_LEN = (uint32_t) len;
    return write_text(f->link, path, PDF, PDF_LEN);
}

int footlight_print(footlight_t *f, const char *uri, const char *path, uint32_t copies)
{
    uint32_t un = len_of(uri, FOOTLIGHT_URI_MAX);
    if (un < 1 || un > FOOTLIGHT_URI_MAX || !path_ok(path) || !ends_pdf(path) ||
        copies > FOOTLIGHT_COPIES_MAX)
        return ZXM_E_ARG;
    zxm_link_t *l = f->link;
    zxm_begin(l, "print.submit");
    zxm_p_str(l, "uri", uri);
    zxm_p_str(l, "path", path);
    if (copies) zxm_p_u64(l, "copies", copies);
    int rc = zxm_call(l);
    if (rc) return rc;
    uint64_t id;
    char state[FOOTLIGHT_STATE_MAX];
    if (zxm_get_u64(l, l->result, "job_id", &id) || id == 0 || id > INT32_MAX ||
        zxm_get_str(l, l->result, "state", state, sizeof state))
        return ZXM_E_PROTOCOL;
    f->job = (int64_t) id;
    memcpy(f->job_state, state, sizeof state);
    return ZXM_OK;
}

/* ---- FM7: the watch hall ---- */

static bool cid_ok(const char *s, uint32_t max)
{
    uint32_t n = len_of(s, max + 1u);
    if (n < 2u || n > max || s[0] != 'b') return false;
    for (uint32_t i = 1; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '2' && s[i] <= '7'))) return false;
    return true;
}

int footlight_hall_create(footlight_t *f, const char *name, bool owner_confirmed)
{
    uint32_t n = len_of(name, FOOTLIGHT_HALL_NAME);
    if (n < 1 || n > FOOTLIGHT_HALL_NAME) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE; /* a hall is public to its CID's holders */
    zxm_link_t *l = f->link;
    char cid[FOOTLIGHT_CID_MAX + 1u];
    zxm_begin(l, "pspace.create");
    zxm_p_str(l, "kind", "hall");
    zxm_p_str(l, "gov", "owner");
    zxm_p_str(l, "name", name);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "cid", cid, sizeof cid) || !cid_ok(cid, FOOTLIGHT_CID_MAX))
        return ZXM_E_PROTOCOL;
    memcpy(f->hall_cid, cid, sizeof cid);
    memset(&f->hall, 0, sizeof f->hall);
    return ZXM_OK;
}

int footlight_hall_set(footlight_t *f, const char *cid)
{
    if (!cid_ok(cid, FOOTLIGHT_CID_MAX)) return ZXM_E_ARG;
    memcpy(f->hall_cid, cid, len_of(cid, FOOTLIGHT_CID_MAX) + 1u);
    memset(&f->hall, 0, sizeof f->hall);
    return ZXM_OK;
}

/* the hall object of the last answer into f->hall */
static int keep_hall(footlight_t *f)
{
    zxm_link_t *l = f->link;
    footlight_hall_t h;
    char state[8];
    memset(&h, 0, sizeof h);
    if (zxm_get_bool(l, l->result, "set", &h.set)) return ZXM_E_PROTOCOL;
    if (h.set) {
        if (zxm_get_str(l, l->result, "state", state, sizeof state) ||
            zxm_get_str(l, l->result, "media", h.media, sizeof h.media) ||
            zxm_get_i64(l, l->result, "pos_ms", &h.pos_ms))
            return ZXM_E_PROTOCOL;
        h.state = !strcmp(state, "play")    ? FOOTLIGHT_HALL_PLAY
                  : !strcmp(state, "pause") ? FOOTLIGHT_HALL_PAUSE
                  : !strcmp(state, "stop")  ? FOOTLIGHT_HALL_STOP
                                            : FOOTLIGHT_HALL_NONE;
        if (h.state == FOOTLIGHT_HALL_NONE || h.pos_ms < 0) return ZXM_E_PROTOCOL;
    }
    if (zxm_get(l, l->result, "viewers") >= 0 &&
        (zxm_get_i64(l, l->result, "viewers", &h.viewers) || h.viewers < 0))
        return ZXM_E_PROTOCOL;
    if (zxm_get(l, l->result, "may_control") >= 0 &&
        zxm_get_bool(l, l->result, "may_control", &h.may_control))
        return ZXM_E_PROTOCOL;
    f->hall = h;
    return ZXM_OK;
}

static int hall_call(footlight_t *f, const char *method, const char *media, int64_t pos_ms)
{
    zxm_link_t *l = f->link;
    zxm_begin(l, method);
    zxm_p_str(l, "cid", f->hall_cid);
    if (media) zxm_p_str(l, "media", media);
    if (pos_ms >= 0) zxm_p_i64(l, "pos_ms", pos_ms);
    int rc = zxm_call(l);
    if (rc) return rc;
    return keep_hall(f);
}

int footlight_hall_load(footlight_t *f, const char *media)
{
    if (!cid_ok(media, FOOTLIGHT_MEDIA_MAX)) return ZXM_E_ARG;
    if (!f->hall_cid[0] || !f->n) return ZXM_E_STATE;
    return hall_call(f, "hall.load", media, f->cur ? f->start_ms[f->cur - 1u] : 0);
}

int footlight_hall_show(footlight_t *f)
{
    if (!f->hall_cid[0] || !f->cur) return ZXM_E_STATE;
    return hall_call(f, "hall.seek", NULL, f->start_ms[f->cur - 1u]);
}

uint32_t footlight_slide_at(const footlight_t *f, int64_t pos_ms)
{
    uint32_t k = f->n ? 1u : 0u;
    for (uint32_t i = 1; i < f->n; i++)
        if (f->start_ms[i] <= pos_ms) k = i + 1u;
    return k;
}

int footlight_hall_follow(footlight_t *f)
{
    if (!f->hall_cid[0]) return ZXM_E_STATE;
    int rc = hall_call(f, "hall.status", NULL, -1);
    if (rc) return rc;
    if (f->hall.set && f->n) f->cur = footlight_slide_at(f, f->hall.pos_ms);
    return ZXM_OK;
}
