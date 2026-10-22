/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* folio_model.c — Folio's model (folio_model.h). */
#include <string.h>

#include "folio_model.h"

/* ---- text out ---- */

typedef struct {
    char *s;
    uint32_t n, cap;
    bool over;
} out_t;

static void put_ch(out_t *o, char c)
{
    if (o->n + 1u >= o->cap) {
        o->over = true;
        return;
    }
    o->s[o->n++] = c;
}

static void put_mem(out_t *o, const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) put_ch(o, s[i]);
}

static void put_str(out_t *o, const char *s)
{
    while (*s) put_ch(o, *s++);
}

/* v in decimal, at least `width` digits (zeros in front) */
static void put_int_w(out_t *o, int64_t v, uint32_t width)
{
    char t[24];
    uint32_t k = 0;
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    if (v < 0) put_ch(o, '-');
    do {
        t[k++] = (char) ('0' + u % 10u);
        u /= 10u;
    } while (u && k < sizeof t);
    while (k < width && k < sizeof t) t[k++] = '0';
    while (k) put_ch(o, t[--k]);
}

static void put_int(out_t *o, int64_t v)
{
    put_int_w(o, v, 1);
}

/* v with `places` decimals: put_dec(o, -1205, 2) is "-12.05" */
static void put_dec(out_t *o, int64_t v, uint32_t places)
{
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v, scale = 1;
    for (uint32_t i = 0; i < places; i++) scale *= 10u;
    if (v < 0) put_ch(o, '-');
    put_int(o, (int64_t) (u / scale));
    put_ch(o, '.');
    put_int_w(o, (int64_t) (u % scale), places);
}

/* ---- the document ---- */

static const folio_page_t LETTER = {12240, 15840, 1440, 1440, 1440, 1440, 240};

/* FM2: lines per page and columns for p, or false */
static bool page_fit(const folio_page_t *p, int64_t *per, int64_t *cols)
{
    if (!folio_page_setup_ok(p->width, p->height, p->left, p->right, p->top, p->bottom, p->leading))
        return false;
    if (p->left + p->right > p->width) return false;
    int64_t n = folio_page_lines(p->height, p->top, p->bottom, p->leading);
    int64_t c = folio_cells(p->width - p->left - p->right, FOLIO_CHAR_TWIPS);
    if (n < 1 || c < FOLIO_COLS_MIN) return false;
    *per = n;
    *cols = c;
    return true;
}

void folio_init(folio_t *f, zxm_link_t *link)
{
    memset(f, 0, sizeof *f);
    f->link = link;
    f->page = LETTER;
    (void) page_fit(&f->page, &f->per_page, &f->cols);
    f->job_pages = -1;
}

int folio_set_page(folio_t *f, const folio_page_t *p)
{
    int64_t per, cols;
    if (!p || !page_fit(p, &per, &cols)) return ZXM_E_ARG;
    f->page = *p;
    f->per_page = per;
    f->cols = cols;
    f->laid = false;
    return ZXM_OK;
}

int folio_set_text(folio_t *f, const char *text, uint32_t n)
{
    if (!text && n) return ZXM_E_ARG;
    if (n > FOLIO_TEXT_MAX) return ZXM_E_TOOBIG;
    if (n) memmove(f->text, text, n);
    f->text[n] = 0;
    f->len = n;
    f->dirty = true;
    f->laid = false;
    return ZXM_OK;
}

int folio_set_doc(folio_t *f, int64_t doc)
{
    if (doc < 0 || doc > 999999999) return ZXM_E_ARG;
    f->doc = doc;
    f->laid = false;
    return ZXM_OK;
}

/* ---- FM3: line breaking ---- */

static folio_row_t NROW[FOLIO_ROWS];
static folio_head_t NHEAD[FOLIO_OUTLINE];

typedef struct {
    const char *t;
    uint32_t n;
    bool over;
} lay_t;

static void push_row(lay_t *y, uint32_t at, uint32_t len)
{
    if (y->n >= FOLIO_ROWS) {
        y->over = true;
        return;
    }
    NROW[y->n].at = at;
    NROW[y->n].len = len;
    y->n++;
}

/* the byte after the character at q (a UTF-8 lead and its continuations) */
static uint32_t next_char(const char *t, uint32_t q, uint32_t end)
{
    q++;
    while (q < end && ((unsigned char) t[q] & 0xc0) == 0x80) q++;
    return q;
}

/* one paragraph t[at..end) broken into lines of at most cols characters */
static void wrap(lay_t *y, uint32_t at, uint32_t end, int64_t cols)
{
    const char *t = y->t;
    if (at >= end) {
        push_row(y, at, 0);
        return;
    }
    uint32_t p = at;
    while (p < end && !y->over) {
        uint32_t q = p, space = UINT32_MAX;
        int64_t chars = 0;
        while (q < end && chars < cols) {
            if (t[q] == ' ') space = q;
            q = next_char(t, q, end);
            chars++;
        }
        uint32_t stop = q, next = q;
        bool soft = false; /* broken at a space, not inside a word */
        if (q < end) {
            if (t[q] == ' ') {
                soft = true;
            } else if (space != UINT32_MAX && space > p) {
                stop = space;
                next = space;
                soft = true;
            }
        }
        while (stop > p && t[stop - 1u] == ' ') stop--;
        push_row(y, p, stop - p);
        p = next;
        if (soft)
            while (p < end && t[p] == ' ') p++;
    }
}

/* "# ", "## " or "### ": the level, else 0 */
static int64_t heading_level(const char *s, uint32_t n)
{
    uint32_t k = 0;
    while (k < n && k < 4u && s[k] == '#') k++;
    if (k < 1u || k > 3u || k >= n || s[k] != ' ') return 0;
    return (int64_t) k;
}

int folio_layout(folio_t *f)
{
    if (f->per_page < 1 || f->cols < FOLIO_COLS_MIN) return ZXM_E_STATE;
    lay_t y = {f->text, 0, false};
    uint32_t nh = 0, i = 0;
    while (i < f->len && !y.over) {
        uint32_t e = i;
        while (e < f->len && f->text[e] != '\n') e++;
        if (e - i == 1u && f->text[i] == '\f') {
            while (y.n % (uint32_t) f->per_page && !y.over) push_row(&y, i, 0);
        } else {
            uint32_t at = i;
            int64_t level = heading_level(f->text + i, e - i);
            if (level) {
                int64_t page = (int64_t) y.n / f->per_page + 1, line = (int64_t) y.n + 1;
                if (nh >= FOLIO_OUTLINE || !folio_outline_ok(f->doc, nh + 1, level, page, line))
                    return ZXM_E_TOOBIG;
                at = i + (uint32_t) level + 1u;
                folio_head_t *h = &NHEAD[nh++];
                uint32_t tl =
                    zxm_utf8_cut(f->text + at, e - at > FOLIO_TITLE ? FOLIO_TITLE : e - at);
                memcpy(h->title, f->text + at, tl);
                h->title[tl] = 0;
                h->level = level;
                h->page = page;
                h->line = line;
            }
            wrap(&y, at, e, f->cols);
        }
        i = e + 1u;
    }
    if (y.over) return ZXM_E_TOOBIG;
    int64_t pages = folio_page_count((int64_t) y.n, f->per_page);
    if (pages < 0 || pages > (int64_t) FOLIO_PAGES) return ZXM_E_TOOBIG;
    memcpy(f->row, NROW, y.n * sizeof NROW[0]);
    memcpy(f->head, NHEAD, nh * sizeof NHEAD[0]);
    f->nrows = y.n;
    f->nheads = nh;
    f->pages = pages;
    f->laid = true;
    return ZXM_OK;
}

/* ---- FM4: the amount table ---- */

int folio_add_item(folio_t *f, const char *desc, int64_t qty, int64_t price)
{
    if (!desc || !desc[0] || strlen(desc) > FOLIO_DESC || strchr(desc, '\n')) return ZXM_E_ARG;
    if (f->nitems >= FOLIO_ITEMS) return ZXM_E_TOOBIG;
    int64_t amount = folio_scaled_amount(qty, price);
    if (amount < 0) return ZXM_E_ARG;
    int64_t a[FOLIO_ITEMS + 1], total;
    for (uint32_t i = 0; i < f->nitems; i++) a[i] = f->item[i].amount;
    a[f->nitems] = amount;
    if (!folio_table_total(a, f->nitems + 1u, &total)) return ZXM_E_ARG;
    folio_item_t *it = &f->item[f->nitems++];
    memcpy(it->desc, desc, strlen(desc) + 1u);
    it->qty = qty;
    it->price = price;
    it->amount = amount;
    f->total = total;
    return ZXM_OK;
}

int folio_clear_items(folio_t *f)
{
    f->nitems = 0;
    f->total = 0;
    return ZXM_OK;
}

int32_t folio_money(int64_t minor, char *out, uint32_t cap)
{
    if (!out || !cap) return -1;
    out_t o = {out, 0, cap, false};
    put_dec(&o, minor, 2);
    if (o.over) return -1;
    out[o.n] = 0;
    return (int32_t) o.n;
}

/* characters (UTF-8 code points) in s[0..n) */
static uint32_t chars_in(const char *s, uint32_t n)
{
    uint32_t c = 0;
    for (uint32_t i = 0; i < n; i++) c += ((unsigned char) s[i] & 0xc0) != 0x80;
    return c;
}

/* s right-aligned in a field of w characters */
static void put_right(out_t *o, const char *s, uint32_t n, uint32_t w)
{
    for (uint32_t k = chars_in(s, n); k < w; k++) put_ch(o, ' ');
    put_mem(o, s, n);
}

/* s left-aligned in a field of w characters */
static void put_left(out_t *o, const char *s, uint32_t n, uint32_t w)
{
    put_mem(o, s, n);
    for (uint32_t k = chars_in(s, n); k < w; k++) put_ch(o, ' ');
}

static uint32_t num_text(char *b, uint32_t cap, int64_t v, uint32_t places)
{
    out_t o = {b, 0, cap, false};
    put_dec(&o, v, places);
    return o.over ? 0 : o.n;
}

static char TABLE[FOLIO_TEXT_MAX + 1];

int folio_put_table(folio_t *f)
{
    if (!f->nitems) return ZXM_E_STATE;
    out_t o = {TABLE, 0, sizeof TABLE, false};
    char b[32];
    uint32_t n;
    if (f->len && f->text[f->len - 1u] != '\n') put_ch(&o, '\n');
    put_left(&o, "Item", 4, FOLIO_DESC);
    put_right(&o, "Qty", 3, FOLIO_QTY_W);
    put_right(&o, "Price", 5, FOLIO_PRICE_W);
    put_right(&o, "Amount", 6, FOLIO_AMOUNT_W);
    put_ch(&o, '\n');
    for (uint32_t i = 0; i < f->nitems; i++) {
        const folio_item_t *it = &f->item[i];
        put_left(&o, it->desc, (uint32_t) strlen(it->desc), FOLIO_DESC);
        n = num_text(b, sizeof b, it->qty, 3);
        put_right(&o, b, n, FOLIO_QTY_W);
        n = num_text(b, sizeof b, it->price, 2);
        put_right(&o, b, n, FOLIO_PRICE_W);
        n = num_text(b, sizeof b, it->amount, 2);
        put_right(&o, b, n, FOLIO_AMOUNT_W);
        put_ch(&o, '\n');
    }
    put_left(&o, "Total", 5, FOLIO_DESC);
    n = num_text(b, sizeof b, f->total, 2);
    put_right(&o, b, n, FOLIO_QTY_W + FOLIO_PRICE_W + FOLIO_AMOUNT_W);
    put_ch(&o, '\n');
    if (o.over || f->len + o.n > FOLIO_TEXT_MAX) return ZXM_E_TOOBIG;
    memcpy(f->text + f->len, TABLE, o.n);
    f->len += o.n;
    f->text[f->len] = 0;
    f->dirty = true;
    f->laid = false;
    return ZXM_OK;
}

/* ---- FM5: zxvd ---- */

/* One request's text: at most this many bytes, each escaping to at most
 * six ("\u000c"), well inside zxvd's request line with the path. */
#define PIECE 2600u

static bool name_ok(const char *space, const char *path)
{
    return space && path && path[0] && strlen(space) <= ZXM_SPACE_MAX &&
           strlen(path) <= FOLIO_PATH_MAX;
}

static int write_text(zxm_link_t *l, const char *space, const char *path, const char *text,
                      uint32_t len)
{
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t n = len - at;
        if (n > PIECE) n = zxm_utf8_cut(text + at, PIECE);
        if (!n && len) return ZXM_E_ARG;
        bool last = at + n >= len;
        zxm_begin(l, "files.write");
        zxm_p_space(l, space);
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

static void set_name(char *dst, uint32_t cap, const char *s)
{
    size_t n = strlen(s);
    if (n >= cap) n = cap - 1u;
    memcpy(dst, s, n);
    dst[n] = 0;
}

static char READ[FOLIO_TEXT_MAX + 1];

int folio_open(folio_t *f, const char *space, const char *path)
{
    static char chunk[FOLIO_TEXT_MAX + 1];
    if (!name_ok(space, path)) return ZXM_E_ARG;
    zxm_link_t *l = f->link;
    uint32_t len = 0;
    uint64_t off = 0;
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_space(l, space);
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
        if (size > FOLIO_TEXT_MAX) return ZXM_E_TOOBIG;
        int32_t n = w4_json_str(l->resp, &l->tok[t], chunk, sizeof chunk);
        if (n < 0 || len + (uint32_t) n > FOLIO_TEXT_MAX) return ZXM_E_TOOBIG;
        memcpy(READ + len, chunk, (size_t) n);
        len += (uint32_t) n;
        if (eof) break;
        if (next_off <= off) return ZXM_E_PROTOCOL;
        off = next_off;
    }
    memcpy(f->text, READ, len);
    f->text[len] = 0;
    f->len = len;
    set_name(f->space, sizeof f->space, space);
    set_name(f->path, sizeof f->path, path);
    f->dirty = false;
    f->laid = false;
    return ZXM_OK;
}

int folio_save(folio_t *f, const char *space, const char *path)
{
    if (!name_ok(space, path)) return ZXM_E_ARG;
    int rc = write_text(f->link, space, path, f->text, f->len);
    if (rc) return rc;
    set_name(f->space, sizeof f->space, space);
    set_name(f->path, sizeof f->path, path);
    f->dirty = false;
    return ZXM_OK;
}

/* ---- FM6: the PDF, and printing it ---- */

static char PDF[FOLIO_PDF_MAX];
static uint32_t PDF_LEN;
static char BODY[192u * 1024u];
static uint32_t AT[4u + 2u * FOLIO_PAGES];

const char *folio_pdf_text(uint32_t *len)
{
    if (len) *len = PDF_LEN;
    return PDF;
}

static bool ends_pdf(const char *s)
{
    size_t n = strlen(s);
    if (n < 5) return false;
    const char *e = s + n - 4;
    return e[0] == '.' && (e[1] | 32) == 'p' && (e[2] | 32) == 'd' && (e[3] | 32) == 'f';
}

/* one row's text as a PDF string: ( ) \ escaped, US-ASCII only */
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

/* page p's text operators; their length, or -1 */
static int32_t page_body(const folio_t *f, uint32_t p)
{
    out_t o = {BODY, 0, sizeof BODY, false};
    const folio_page_t *g = &f->page;
    put_str(&o, "BT\n/F1 ");
    put_int(&o, FOLIO_FONT_PT);
    put_str(&o, " Tf\n");
    uint32_t first = p * (uint32_t) f->per_page;
    for (uint32_t k = 0; k < (uint32_t) f->per_page && first + k < f->nrows; k++) {
        const folio_row_t *r = &f->row[first + k];
        if (!r->len) continue;
        /* the baseline: 8 points (160 twips) under the top of its line */
        int64_t y = g->height - g->top - (int64_t) k * g->leading - 160;
        put_str(&o, "1 0 0 1 ");
        put_dec(&o, g->left * 5, 2);
        put_ch(&o, ' ');
        put_dec(&o, y * 5, 2);
        put_str(&o, " Tm\n");
        put_pdf_text(&o, f->text + r->at, r->len);
        put_str(&o, " Tj\n");
    }
    put_str(&o, "ET\n");
    return o.over ? -1 : (int32_t) o.n;
}

static int32_t pdf_compose(const folio_t *f)
{
    uint32_t pages = (uint32_t) f->pages, nobj = 3u + 2u * pages;
    out_t o = {PDF, 0, sizeof PDF, false};
    put_str(&o, "%PDF-1.4\n");
    AT[1] = o.n;
    put_str(&o, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    AT[2] = o.n;
    put_str(&o, "2 0 obj\n<< /Type /Pages /Kids [");
    for (uint32_t p = 0; p < pages; p++) {
        if (p) put_ch(&o, ' ');
        put_int(&o, 4 + 2 * (int64_t) p);
        put_str(&o, " 0 R");
    }
    put_str(&o, "] /Count ");
    put_int(&o, pages);
    put_str(&o, " >>\nendobj\n");
    AT[3] = o.n;
    put_str(&o, "3 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>\nendobj\n");
    for (uint32_t p = 0; p < pages && !o.over; p++) {
        int32_t blen = page_body(f, p);
        if (blen < 0) return -1;
        AT[4u + 2u * p] = o.n;
        put_int(&o, 4 + 2 * (int64_t) p);
        put_str(&o, " 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ");
        put_dec(&o, f->page.width * 5, 2);
        put_ch(&o, ' ');
        put_dec(&o, f->page.height * 5, 2);
        put_str(&o, "] /Resources << /Font << /F1 3 0 R >> >> /Contents ");
        put_int(&o, 5 + 2 * (int64_t) p);
        put_str(&o, " 0 R >>\nendobj\n");
        AT[5u + 2u * p] = o.n;
        put_int(&o, 5 + 2 * (int64_t) p);
        put_str(&o, " 0 obj\n<< /Length ");
        put_int(&o, blen);
        put_str(&o, " >>\nstream\n");
        put_mem(&o, BODY, (uint32_t) blen);
        put_str(&o, "endstream\nendobj\n");
    }
    uint32_t xref = o.n;
    put_str(&o, "xref\n0 ");
    put_int(&o, nobj + 1u);
    put_str(&o, "\n0000000000 65535 f \n");
    for (uint32_t k = 1; k <= nobj; k++) {
        put_int_w(&o, AT[k], 10);
        put_str(&o, " 00000 n \n");
    }
    put_str(&o, "trailer\n<< /Size ");
    put_int(&o, nobj + 1u);
    put_str(&o, " /Root 1 0 R >>\nstartxref\n");
    put_int(&o, xref);
    put_str(&o, "\n%%EOF\n");
    return o.over ? -1 : (int32_t) o.n;
}

int folio_export_pdf(folio_t *f, const char *pdf)
{
    if (!name_ok(f->space, pdf) || !ends_pdf(pdf)) return ZXM_E_ARG;
    if (!f->laid || f->pages < 1) return ZXM_E_STATE;
    int32_t len = pdf_compose(f);
    if (len < 0) return ZXM_E_TOOBIG;
    PDF_LEN = (uint32_t) len;
    return write_text(f->link, f->space, pdf, PDF, PDF_LEN);
}

static bool uri_ok(const char *uri)
{
    return uri && uri[0] && strlen(uri) <= FOLIO_URI_MAX;
}

int folio_printer(folio_t *f, const char *uri)
{
    if (!uri_ok(uri)) return ZXM_E_ARG;
    zxm_link_t *l = f->link;
    f->printer_known = false;
    zxm_begin(l, "print.status");
    zxm_p_str(l, "uri", uri);
    int rc = zxm_call(l);
    if (rc) return rc;
    bool accepting;
    int32_t fm = zxm_get(l, l->result, "formats");
    if (zxm_get_bool(l, l->result, "accepting", &accepting) || fm < 0 ||
        l->tok[fm].type != W4_J_ARR)
        return ZXM_E_PROTOCOL;
    bool pdf = false;
    for (uint32_t i = 0; i < zxm_len(l, fm); i++) {
        int32_t t = zxm_at(l, fm, i);
        if (t >= 0 && w4_json_str_eq(l->resp, &l->tok[t], "application/pdf")) pdf = true;
    }
    f->printer_known = true;
    f->accepting = accepting;
    f->takes_pdf = pdf;
    return ZXM_OK;
}

static int read_job(folio_t *f)
{
    zxm_link_t *l = f->link;
    uint64_t id;
    int64_t pages = -1;
    char state[FOLIO_STATE_MAX];
    if (zxm_get_u64(l, l->result, "job_id", &id) || id == 0 || id > INT32_MAX ||
        zxm_get_str(l, l->result, "state", state, sizeof state))
        return ZXM_E_PROTOCOL;
    if (zxm_get(l, l->result, "pages") >= 0 && zxm_get_i64(l, l->result, "pages", &pages))
        return ZXM_E_PROTOCOL;
    f->job = (int64_t) id;
    memcpy(f->job_state, state, sizeof state);
    f->job_pages = pages;
    return ZXM_OK;
}

int folio_print(folio_t *f, const char *uri, const char *pdf, uint32_t copies)
{
    if (!uri_ok(uri) || !name_ok(f->space, pdf) || !ends_pdf(pdf) || copies > FOLIO_COPIES_MAX)
        return ZXM_E_ARG;
    int rc = folio_printer(f, uri);
    if (rc) return rc;
    if (!f->accepting || !f->takes_pdf) return ZXM_E_STATE;
    zxm_link_t *l = f->link;
    zxm_begin(l, "print.submit");
    zxm_p_space(l, f->space);
    zxm_p_str(l, "uri", uri);
    zxm_p_str(l, "path", pdf);
    if (copies) zxm_p_u64(l, "copies", copies);
    rc = zxm_call(l);
    if (rc) return rc;
    return read_job(f);
}

int folio_follow(folio_t *f, const char *uri)
{
    if (!uri_ok(uri)) return ZXM_E_ARG;
    if (!f->job) return ZXM_E_STATE;
    zxm_link_t *l = f->link;
    zxm_begin(l, "print.job");
    zxm_p_str(l, "uri", uri);
    zxm_p_u64(l, "job_id", (uint64_t) f->job);
    int rc = zxm_call(l);
    if (rc) return rc;
    return read_job(f);
}
