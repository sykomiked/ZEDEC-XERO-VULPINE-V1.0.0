/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sealwax_model.c — Sealwax, fill and sign forms (see sealwax_model.h). */
#include <string.h>

#include "sealwax_model.h"

static const char *const KIND[4] = {"", "text", "amount", "number"};
static const char *const ADVICE = " Advice only: read the form before you sign it; Sealwax never "
                                  "signs, prints or sends it by itself.";

/* ---- text ------------------------------------------------------------------ */

/* Append n bytes of s to out at *at, bounded by cap (room for a NUL kept). */
static bool putn(char *out, uint32_t cap, uint32_t *at, const char *s, uint32_t n)
{
    if (*at + n + 1u > cap) return false;
    memcpy(out + *at, s, n);
    *at += n;
    out[*at] = 0;
    return true;
}

static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    return putn(out, cap, at, s, (uint32_t) strlen(s));
}

/* v in decimal, at least `width` digits (zeros in front), '-' first when negative. */
static bool put_num(char *out, uint32_t cap, uint32_t *at, int64_t v, int width)
{
    char t[24];
    int k = 23;
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    t[k] = 0;
    do {
        t[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while ((u || 23 - k < width) && k > 1);
    if (v < 0) t[--k] = '-';
    return put(out, cap, at, t + k);
}

/* minor units as units.cc */
static bool put_money(char *out, uint32_t cap, uint32_t *at, int64_t minor)
{
    if (minor < 0 && !put(out, cap, at, "-")) return false;
    uint64_t u = minor < 0 ? (uint64_t) 0 - (uint64_t) minor : (uint64_t) minor;
    return put_num(out, cap, at, (int64_t) (u / 100u), 1) && put(out, cap, at, ".") &&
           put_num(out, cap, at, (int64_t) (u % 100u), 2);
}

/* Code points in s[0..n), or -1 when it is not UTF-8 or holds a control
 * character. */
static int64_t utf8_chars(const char *s, uint32_t n)
{
    int64_t chars = 0;
    uint32_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char) s[i];
        uint32_t len = c < 0x80u              ? 1u
                       : (c & 0xe0u) == 0xc0u ? 2u
                       : (c & 0xf0u) == 0xe0u ? 3u
                       : (c & 0xf8u) == 0xf0u ? 4u
                                              : 0u;
        if (!len || i + len > n || c < 0x20u || c == 0x7fu || c == 0xc0u || c == 0xc1u || c > 0xf4u)
            return -1;
        for (uint32_t k = 1; k < len; k++)
            if (((unsigned char) s[i + k] & 0xc0u) != 0x80u) return -1;
        i += len;
        chars++;
    }
    return chars;
}

/* An optional '-' (when sign) and 1..15 digits, the whole of s[0..n). */
static bool parse_int(const char *s, uint32_t n, bool sign, int64_t *v)
{
    bool neg = false;
    uint32_t i = 0;
    if (sign && n && s[0] == '-') neg = true, i = 1;
    if (i >= n || n - i > 15u) return false;
    int64_t x = 0;
    for (; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        x = x * 10 + (s[i] - '0');
    }
    *v = neg ? -x : x;
    return true;
}

/* SM2: an amount as entered, to minor units by SR4 */
static bool parse_amount(const char *s, uint32_t n, int64_t *minor)
{
    uint32_t dot = 0;
    while (dot < n && s[dot] != '.') dot++;
    int64_t units, cents = 0;
    if (!parse_int(s, dot, false, &units)) return false;
    if (dot < n) {
        uint32_t frac = n - dot - 1u;
        if (frac < 1u || frac > 2u || !parse_int(s + dot + 1, frac, false, &cents)) return false;
        if (frac == 1u) cents *= 10;
    }
    int64_t m = sealwax_amount_minor(units, cents);
    if (m == SEALWAX_REFUSED) return false;
    *minor = m;
    return true;
}

static bool name_ok(const char *name)
{
    size_t n = name ? strlen(name) : 0;
    if (!n || n > SEALWAX_NAME_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

static bool path_ok(const char *space, const char *path)
{
    return space && path && path[0] && strlen(space) <= ZXM_SPACE_MAX &&
           strlen(path) <= SEALWAX_PATH_MAX;
}

static void set_name(char *dst, uint32_t cap, const char *s)
{
    size_t n = strlen(s);
    if (n >= cap) n = cap - 1u;
    memcpy(dst, s, n);
    dst[n] = 0;
}

/* ---- SM3: the review ------------------------------------------------------- */

static void review(sealwax_t *s)
{
    int64_t req = 0, done = 0, amounts[SEALWAX_FIELDS];
    uint32_t na = 0;
    s->missing = s->outside = 0;
    for (uint32_t i = 0; i < s->nfields; i++) {
        sealwax_field_t *f = &s->field[i];
        f->filled = f->bytes > 0;
        int64_t v = f->kind == SEALWAX_TEXT ? f->chars : f->num;
        f->outside = f->filled && (v < f->lo || v > f->hi);
        if (f->required && !f->filled) s->missing++;
        if (f->outside) s->outside++;
        if (f->required) req |= (int64_t) 1 << i;
        if (f->filled && !f->outside) done |= (int64_t) 1 << i;
        if (f->kind == SEALWAX_AMOUNT && f->filled) amounts[na++] = f->num;
    }
    s->ready = sealwax_sign_ready(s->nfields, s->missing, s->outside);
    int32_t n = s->nfields ? sealwax_fill_order(s->nfields, req, done, s->order) : 0;
    s->norder = n > 0 ? (uint32_t) n : 0u;
    s->total = 0;
    s->total_ok = sealwax_total(amounts, na, &s->total);

    char *o = s->notice;
    const uint32_t cap = sizeof s->notice;
    uint32_t at = 0;
    o[0] = 0;
    if (!s->nfields) {
        put(o, cap, &at, "No fields yet.");
    } else if (s->ready) {
        put(o, cap, &at, "Ready to sign: ");
        put_num(o, cap, &at, s->nfields, 1);
        put(o, cap, &at, " fields in bounds");
        if (na && s->total_ok) {
            put(o, cap, &at, ", amounts total ");
            put_money(o, cap, &at, s->total);
        }
        put(o, cap, &at, ".");
    } else {
        put(o, cap, &at, "Not ready: ");
        put_num(o, cap, &at, s->missing, 1);
        put(o, cap, &at, " required empty, ");
        put_num(o, cap, &at, s->outside, 1);
        put(o, cap, &at, " outside bounds");
        if (s->norder && s->order[0] > 0) {
            put(o, cap, &at, "; next: ");
            put(o, cap, &at, s->field[s->order[0] - 1].name);
        }
        put(o, cap, &at, ".");
    }
    if (!s->total_ok) put(o, cap, &at, " The amounts' total is past 64 bits.");
    put(o, cap, &at, ADVICE);
}

/* ---- SM1, SM2 ---------------------------------------------------------------- */

void sealwax_init(sealwax_t *s, zxm_link_t *link)
{
    memset(s, 0, sizeof *s);
    s->link = link;
    s->sig.ts = -1;
    review(s);
}

int sealwax_set_title(sealwax_t *s, const char *title)
{
    if (!title) return ZXM_E_ARG;
    uint32_t n = (uint32_t) strlen(title);
    if (n > SEALWAX_TITLE_MAX || utf8_chars(title, n) < 0) return ZXM_E_ARG;
    memcpy(s->title, title, n);
    s->title[n] = 0;
    s->dirty = true;
    return ZXM_OK;
}

int sealwax_set_form(sealwax_t *s, int64_t form)
{
    if (form < 0 || form > 999999999) return ZXM_E_ARG;
    s->form = form;
    return ZXM_OK;
}

int32_t sealwax_find(const sealwax_t *s, const char *name)
{
    for (uint32_t i = 0; name && i < s->nfields; i++)
        if (!strcmp(s->field[i].name, name)) return (int32_t) i;
    return -1;
}

int sealwax_add_field(sealwax_t *s, const char *name, int64_t kind, int64_t required, int64_t lo,
                      int64_t hi)
{
    if (s->nfields >= SEALWAX_FIELDS || !name_ok(name) || sealwax_find(s, name) >= 0 ||
        (required != 0 && required != 1) || !sealwax_bounds_ok(kind, lo, hi))
        return ZXM_E_ARG;
    sealwax_field_t *f = &s->field[s->nfields];
    memset(f, 0, sizeof *f);
    set_name(f->name, sizeof f->name, name);
    f->kind = kind;
    f->required = required;
    f->lo = lo;
    f->hi = hi;
    s->nfields++;
    s->dirty = true;
    review(s);
    return ZXM_OK;
}

/* SM2: value read into what field f would hold; false when it cannot be */
static bool read_value(const sealwax_field_t *f, const char *v, uint32_t n, int64_t *chars,
                       int64_t *num)
{
    *num = 0;
    if (n > SEALWAX_VALUE_BYTES) return false;
    *chars = utf8_chars(v, n);
    if (*chars < 0 || *chars > SEALWAX_VALUE_MAX) return false;
    if (!n || f->kind == SEALWAX_TEXT) return true;
    if (f->kind == SEALWAX_AMOUNT) return parse_amount(v, n, num);
    return parse_int(v, n, true, num);
}

static int fill_n(sealwax_t *s, uint32_t i, const char *v, uint32_t n)
{
    int64_t chars, num;
    if (i >= s->nfields || !v || !read_value(&s->field[i], v, n, &chars, &num)) return ZXM_E_ARG;
    sealwax_field_t *f = &s->field[i];
    memcpy(f->value, v, n);
    f->value[n] = 0;
    f->bytes = n;
    f->chars = chars;
    f->num = num;
    s->dirty = true;
    review(s);
    return ZXM_OK;
}

int sealwax_fill(sealwax_t *s, uint32_t i, const char *value)
{
    if (!value) return ZXM_E_ARG;
    size_t n = strlen(value);
    if (n > SEALWAX_VALUE_BYTES) return ZXM_E_ARG;
    return fill_n(s, i, value, (uint32_t) n);
}

bool sealwax_record(const sealwax_t *s, uint32_t i)
{
    if (i >= s->nfields) return false;
    const sealwax_field_t *f = &s->field[i];
    return sealwax_record_fits(s->form, (int64_t) i + 1, f->kind, f->required, f->lo, f->hi,
                               f->chars);
}

/* ---- SM4: the file ------------------------------------------------------------ */

int32_t sealwax_text(const sealwax_t *s, char *out, uint32_t cap)
{
    uint32_t at = 0;
    bool ok = cap > 0 && put(out, cap, &at, "sealwax 1\ntitle ") && put(out, cap, &at, s->title) &&
              put(out, cap, &at, "\n");
    for (uint32_t i = 0; ok && i < s->nfields; i++) {
        const sealwax_field_t *f = &s->field[i];
        ok = put(out, cap, &at, "field ") && put(out, cap, &at, f->name) &&
             put(out, cap, &at, " ") && put(out, cap, &at, KIND[f->kind]) &&
             put(out, cap, &at, f->required ? " 1 " : " 0 ") && put_num(out, cap, &at, f->lo, 1) &&
             put(out, cap, &at, " ") && put_num(out, cap, &at, f->hi, 1);
        if (ok && f->bytes) {
            ok = put(out, cap, &at, " ");
            if (ok && f->kind == SEALWAX_AMOUNT)
                ok = put_money(out, cap, &at, f->num);
            else if (ok && f->kind == SEALWAX_NUMBER)
                ok = put_num(out, cap, &at, f->num, 1);
            else if (ok)
                ok = putn(out, cap, &at, f->value, f->bytes);
        }
        ok = ok && put(out, cap, &at, "\n");
    }
    return ok ? (int32_t) at : -1;
}

/* the next space-separated word of line[*i..n) */
static bool word(const char *line, uint32_t n, uint32_t *i, uint32_t *at, uint32_t *len)
{
    if (*i >= n) return false;
    *at = *i;
    while (*i < n && line[*i] != ' ') (*i)++;
    *len = *i - *at;
    if (*i < n) (*i)++; /* the space */
    return *len > 0;
}

static bool is(const char *w, uint32_t n, const char *lit)
{
    return strlen(lit) == n && !memcmp(w, lit, n);
}

static sealwax_t PARSED;

/* one "field" line, after its first word, into PARSED */
static bool parse_field(const char *l, uint32_t n, uint32_t i)
{
    uint32_t at[5], len[5];
    for (int k = 0; k < 5; k++)
        if (!word(l, n, &i, &at[k], &len[k])) return false;
    char name[SEALWAX_NAME_MAX + 1];
    int64_t kind = 0, req, lo, hi;
    if (len[0] > SEALWAX_NAME_MAX) return false;
    memcpy(name, l + at[0], len[0]);
    name[len[0]] = 0;
    for (int64_t k = 1; k <= 3; k++)
        if (is(l + at[1], len[1], KIND[k])) kind = k;
    if (!kind || !parse_int(l + at[2], len[2], false, &req) ||
        !parse_int(l + at[3], len[3], true, &lo) || !parse_int(l + at[4], len[4], true, &hi) ||
        sealwax_add_field(&PARSED, name, kind, req, lo, hi) != ZXM_OK)
        return false;
    /* the value is the rest of the line, after one space */
    uint32_t v = at[4] + len[4];
    if (v == n) return true;
    return v + 1u < n && fill_n(&PARSED, PARSED.nfields - 1u, l + v + 1, n - v - 1u) == ZXM_OK;
}

int sealwax_parse(sealwax_t *s, const char *text, uint32_t n)
{
    if (!text || n > SEALWAX_TEXT_MAX) return ZXM_E_ARG;
    sealwax_init(&PARSED, s->link);
    uint32_t pos = 0, line = 0;
    bool titled = false;
    while (pos < n) {
        uint32_t e = pos;
        while (e < n && text[e] != '\n') e++;
        if (e == n) return ZXM_E_ARG; /* every line ends with "\n" */
        const char *l = text + pos;
        uint32_t len = e - pos, i = 0, at, wl;
        if (line == 0) {
            if (!is(l, len, "sealwax 1")) return ZXM_E_ARG;
        } else if (line == 1) {
            if (len < 6 || memcmp(l, "title ", 6) || len - 6u > SEALWAX_TITLE_MAX ||
                utf8_chars(l + 6, len - 6u) < 0)
                return ZXM_E_ARG;
            memcpy(PARSED.title, l + 6, len - 6u);
            PARSED.title[len - 6u] = 0;
            titled = true;
        } else if (!word(l, len, &i, &at, &wl) || !is(l + at, wl, "field") ||
                   !parse_field(l, len, i)) {
            return ZXM_E_ARG;
        }
        pos = e + 1u;
        line++;
    }
    if (!titled) return ZXM_E_ARG;
    PARSED.link = s->link;
    PARSED.form = s->form;
    memcpy(PARSED.space, s->space, sizeof s->space);
    memcpy(PARSED.path, s->path, sizeof s->path);
    PARSED.printer_known = s->printer_known;
    PARSED.accepting = s->accepting;
    PARSED.takes_pdf = s->takes_pdf;
    PARSED.job = s->job;
    memcpy(PARSED.job_state, s->job_state, sizeof s->job_state);
    PARSED.dirty = true;
    PARSED.saved_len = s->saved_len;
    *s = PARSED;
    review(s);
    return ZXM_OK;
}

/* One request's text: at most this many bytes, each escaping to at most
 * six ("\u001f"), well inside zxvd's request line with the path. */
#define PIECE 2600u

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

/* the whole of a text file into out (cap - 1 bytes at most); its length or <0 */
static int32_t read_text(zxm_link_t *l, const char *space, const char *path, char *out,
                         uint32_t cap)
{
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
        if (size >= cap) return ZXM_E_TOOBIG;
        int32_t n = w4_json_str(l->resp, &l->tok[t], out + len, cap - len);
        if (n < 0 || len + (uint32_t) n >= cap) return ZXM_E_TOOBIG;
        len += (uint32_t) n;
        if (eof) break;
        if (next_off <= off) return ZXM_E_PROTOCOL;
        off = next_off;
    }
    out[len] = 0;
    return (int32_t) len;
}

static char BUF[SEALWAX_TEXT_MAX + 1];

int sealwax_open(sealwax_t *s, const char *space, const char *path)
{
    if (!path_ok(space, path)) return ZXM_E_ARG;
    int32_t n = read_text(s->link, space, path, BUF, sizeof BUF);
    if (n < 0) return n;
    int rc = sealwax_parse(s, BUF, (uint32_t) n);
    if (rc) return rc;
    set_name(s->space, sizeof s->space, space);
    set_name(s->path, sizeof s->path, path);
    s->dirty = false;
    s->saved_len = (uint32_t) n;
    return ZXM_OK;
}

int sealwax_save(sealwax_t *s, const char *space, const char *path)
{
    if (!path_ok(space, path)) return ZXM_E_ARG;
    int32_t n = sealwax_text(s, BUF, sizeof BUF);
    if (n < 0) return ZXM_E_TOOBIG;
    int rc = write_text(s->link, space, path, BUF, (uint32_t) n);
    if (rc) return rc;
    set_name(s->space, sizeof s->space, space);
    set_name(s->path, sizeof s->path, path);
    s->dirty = false;
    s->saved_len = (uint32_t) n;
    return ZXM_OK;
}

/* ---- SM5: the PDF and the printer ---------------------------------------------- */

static char PDF[SEALWAX_PDF_MAX + 1];
static uint32_t PDF_LEN;

const char *sealwax_pdf_text(uint32_t *len)
{
    if (len) *len = PDF_LEN;
    return PDF;
}

/* one line of the page: "(" text ") Tj T*", US-ASCII, cut at the columns */
static bool pdf_line(char *o, uint32_t cap, uint32_t *at, const char *a, const char *b, uint32_t bn)
{
    char line[SEALWAX_PDF_COLS + 1];
    uint32_t k = 0, an = (uint32_t) strlen(a);
    for (uint32_t i = 0; i < an + bn && k < SEALWAX_PDF_COLS; i++) {
        unsigned char c = (unsigned char) (i < an ? a[i] : b[i - an]);
        if (c >= 0x80u && (c & 0xc0u) == 0x80u) continue; /* a character's tail: one '?' */
        line[k++] = c < 0x20u || c >= 0x7fu ? '?' : (char) c;
    }
    if (!put(o, cap, at, "(")) return false;
    for (uint32_t i = 0; i < k; i++) {
        char c = line[i];
        if ((c == '(' || c == ')' || c == '\\') && !put(o, cap, at, "\\")) return false;
        if (!putn(o, cap, at, &c, 1)) return false;
    }
    return put(o, cap, at, ") Tj T*\n");
}

/* xref entry: ten digits, a space, "00000 n \n" */
static bool pdf_xref(char *o, uint32_t cap, uint32_t *at, uint32_t off)
{
    return put_num(o, cap, at, off, 10) && put(o, cap, at, " 00000 n \n");
}

static bool compose_pdf(const sealwax_t *s)
{
    static char body[SEALWAX_PDF_MAX];
    uint32_t b = 0, cap = sizeof body;
    char v[SEALWAX_VALUE_BYTES + 64];
    bool ok = put(body, cap, &b, "BT /F1 10 Tf 14 TL 72 740 Td\n") &&
              pdf_line(body, cap, &b, s->title[0] ? s->title : "Form", "", 0) &&
              pdf_line(body, cap, &b, "", "", 0);
    for (uint32_t i = 0; ok && i < s->nfields; i++) {
        const sealwax_field_t *f = &s->field[i];
        uint32_t vn = 0;
        v[0] = 0;
        if (f->kind == SEALWAX_AMOUNT && f->filled)
            ok = put_money(v, sizeof v, &vn, f->num);
        else
            ok = putn(v, sizeof v, &vn, f->value, f->bytes);
        if (ok && f->required) ok = put(v, sizeof v, &vn, f->filled ? "" : "(required)");
        if (ok && f->outside) ok = put(v, sizeof v, &vn, "  <- outside its bounds");
        char head[SEALWAX_NAME_MAX + 4];
        uint32_t hn = 0;
        ok = ok && put(head, sizeof head, &hn, f->name) && put(head, sizeof head, &hn, ": ") &&
             pdf_line(body, cap, &b, head, v, vn);
    }
    if (ok && s->total_ok) {
        uint32_t vn = 0;
        ok = put_money(v, sizeof v, &vn, s->total) && pdf_line(body, cap, &b, "", "", 0) &&
             pdf_line(body, cap, &b, "Total of amounts: ", v, vn);
    }
    ok =
        ok && pdf_line(body, cap, &b, "", "", 0) &&
        pdf_line(body, cap, &b, s->ready ? "Ready to sign." : "DRAFT: not ready to sign.", "", 0) &&
        pdf_line(body, cap, &b, "", "", 0) &&
        pdf_line(body, cap, &b, "Signature: ______________________  Date: ____________", "", 0) &&
        pdf_line(body, cap, &b, "Advice only: read the form before you sign it.", "", 0) &&
        put(body, cap, &b, "ET\n");
    if (!ok) return false;

    char *o = PDF;
    uint32_t at = 0, off[6];
    cap = sizeof PDF;
    ok = put(o, cap, &at, "%PDF-1.4\n");
    off[1] = at;
    ok = ok && put(o, cap, &at, "1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\n");
    off[2] = at;
    ok = ok && put(o, cap, &at, "2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj\n");
    off[3] = at;
    ok = ok && put(o, cap, &at,
                   "3 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << "
                   "/Font << /F1 4 0 R >> >> /Contents 5 0 R >> endobj\n");
    off[4] = at;
    ok = ok &&
         put(o, cap, &at, "4 0 obj << /Type /Font /Subtype /Type1 /BaseFont /Courier >> endobj\n");
    off[5] = at;
    ok = ok && put(o, cap, &at, "5 0 obj << /Length ") && put_num(o, cap, &at, b, 1) &&
         put(o, cap, &at, " >>\nstream\n") && putn(o, cap, &at, body, b) &&
         put(o, cap, &at, "endstream endobj\n");
    uint32_t xref = at;
    ok = ok && put(o, cap, &at, "xref\n0 6\n0000000000 65535 f \n");
    for (uint32_t k = 1; ok && k <= 5; k++) ok = pdf_xref(o, cap, &at, off[k]);
    ok = ok && put(o, cap, &at, "trailer << /Size 6 /Root 1 0 R >>\nstartxref\n") &&
         put_num(o, cap, &at, xref, 1) && put(o, cap, &at, "\n%%EOF\n");
    PDF_LEN = ok ? at : 0;
    return ok;
}

static bool ends_pdf(const char *p)
{
    size_t n = strlen(p);
    return n > 4 && !strcmp(p + n - 4, ".pdf");
}

static bool uri_ok(const char *uri)
{
    return uri && uri[0] && strlen(uri) <= SEALWAX_URI_MAX;
}

int sealwax_export_pdf(sealwax_t *s, const char *pdf)
{
    if (!path_ok(s->space, pdf) || !ends_pdf(pdf) || !s->nfields) return ZXM_E_ARG;
    if (!compose_pdf(s)) return ZXM_E_TOOBIG;
    return write_text(s->link, s->space, pdf, PDF, PDF_LEN);
}

int sealwax_printer(sealwax_t *s, const char *uri)
{
    if (!uri_ok(uri)) return ZXM_E_ARG;
    zxm_link_t *l = s->link;
    s->printer_known = false;
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
    s->printer_known = true;
    s->accepting = accepting;
    s->takes_pdf = pdf;
    return ZXM_OK;
}

int sealwax_print(sealwax_t *s, const char *uri, const char *pdf, uint32_t copies, bool confirmed)
{
    if (!uri_ok(uri) || !path_ok(s->space, pdf) || !ends_pdf(pdf) || copies > SEALWAX_COPIES_MAX)
        return ZXM_E_ARG;
    if (!confirmed) return ZXM_E_STATE; /* the owner's word, or nothing is sent */
    int rc = sealwax_printer(s, uri);
    if (rc) return rc;
    if (!s->accepting || !s->takes_pdf) return ZXM_E_STATE;
    zxm_link_t *l = s->link;
    zxm_begin(l, "print.submit");
    zxm_p_space(l, s->space);
    zxm_p_str(l, "uri", uri);
    zxm_p_str(l, "path", pdf);
    if (copies) zxm_p_u64(l, "copies", copies);
    rc = zxm_call(l);
    if (rc) return rc;
    uint64_t id;
    char state[SEALWAX_STATE_MAX];
    if (zxm_get_u64(l, l->result, "job_id", &id) || id == 0 || id > INT32_MAX ||
        zxm_get_str(l, l->result, "state", state, sizeof state))
        return ZXM_E_PROTOCOL;
    s->job = (int64_t) id;
    memcpy(s->job_state, state, sizeof state);
    return ZXM_OK;
}

/* ---- SM6: a signed copy ----------------------------------------------------------- */

static bool hex_ok(const char *h, uint32_t n)
{
    if (!n || n % 2u) return false;
    for (uint32_t i = 0; i < n; i++) {
        char c = h[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

int sealwax_check_signed(sealwax_t *s, const char *sigpath)
{
    static char hex[SEALWAX_SIG_MAX + 2];
    if (!path_ok(s->space, sigpath)) return ZXM_E_ARG;
    zxm_link_t *l = s->link;
    int32_t n = read_text(l, s->space, sigpath, hex, sizeof hex);
    if (n < 0) return n;
    uint32_t len = (uint32_t) n;
    while (len && (hex[len - 1] == '\n' || hex[len - 1] == ' ')) len--;
    if (len > SEALWAX_SIG_MAX || !hex_ok(hex, len)) return ZXM_E_ARG;
    zxm_begin(l, "web4.verify");
    zxm_p_strn(l, "envelope", hex, len);
    int rc = zxm_call(l);
    if (rc) return rc;
    sealwax_sig_t g;
    memset(&g, 0, sizeof g);
    g.ts = -1;
    if (zxm_get_bool(l, l->result, "valid", &g.valid)) return ZXM_E_PROTOCOL;
    if (!g.valid) {
        if (zxm_get_str(l, l->result, "reason", g.reason, sizeof g.reason)) return ZXM_E_PROTOCOL;
    } else {
        uint64_t pb;
        if (zxm_get_str(l, l->result, "kind", g.kind, sizeof g.kind) ||
            zxm_get_str(l, l->result, "from", g.from, sizeof g.from) ||
            zxm_get_u64(l, l->result, "payload_bytes", &pb) || pb > INT32_MAX ||
            strlen(g.from) != 64u)
            return ZXM_E_PROTOCOL;
        g.payload_bytes = (int64_t) pb;
        if (zxm_get(l, l->result, "ts") >= 0 && zxm_get_i64(l, l->result, "ts", &g.ts))
            return ZXM_E_PROTOCOL;
        bool carries =
            !strcmp(g.kind, "REQUEST") || !strcmp(g.kind, "RESPONSE") || !strcmp(g.kind, "STREAM");
        g.bound = carries && !s->dirty && s->saved_len > 0 && g.payload_bytes == s->saved_len;
    }
    g.checked = true;
    s->sig = g;

    char *o = s->notice;
    const uint32_t cap = sizeof s->notice;
    uint32_t at = 0;
    o[0] = 0;
    if (!g.valid) {
        put(o, cap, &at, "The signed copy does not hold: ");
        put(o, cap, &at, g.reason);
        put(o, cap, &at, ".");
    } else if (g.bound) {
        put(o, cap, &at, "The signed copy holds: signed with the owner's key, ");
        put_num(o, cap, &at, g.payload_bytes, 1);
        put(o, cap, &at, " bytes, as many as this form (matched by size only).");
    } else {
        put(o, cap, &at, "The signature holds, but it is not this form as saved: ");
        put_num(o, cap, &at, g.payload_bytes, 1);
        put(o, cap, &at, " bytes signed, ");
        put_num(o, cap, &at, s->saved_len, 1);
        put(o, cap, &at, s->dirty ? " saved and changed since." : " in the form.");
    }
    put(o, cap, &at, ADVICE);
    return ZXM_OK;
}
