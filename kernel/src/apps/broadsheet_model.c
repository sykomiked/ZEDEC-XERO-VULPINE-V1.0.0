/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* broadsheet_model.c — Broadsheet, the document viewer (see
 * broadsheet_model.h). A document is checked by BR1 and BR2 before anything
 * else is read from it; its annotations live in a file of their own and the
 * document itself is never written. */
#include <string.h>

#include "broadsheet_model.h"

#define NOTES_HEAD "broadsheet notes 1\n"
#define OFF_DIGITS 10u /* a cross-reference entry's offset, and startxref's */
#define GEN_DIGITS 5u  /* a cross-reference entry's generation */
#define BOX_DIGITS 6u  /* BM3: a /MediaBox number's whole part */
#define NUM_DIGITS 15u /* BM5: a note's numbers, the stamp the longest */

/* ---- state ----------------------------------------------------------------- */

void broadsheet_init(broadsheet_t *b, zxm_link_t *link)
{
    memset(b, 0, sizeof *b);
    b->link = link;
    b->fault_at = -1;
    b->page_w = BROADSHEET_LETTER_W;
    b->page_h = BROADSHEET_LETTER_H;
}

/* The first fault of a parse, unless a read already failed (b->rc). */
static bool fail(broadsheet_t *b, int32_t fault, int64_t at)
{
    if (!b->rc && !b->fault) {
        b->fault = fault;
        b->fault_at = at;
    }
    return false;
}

/* ---- reading: files.read, base64 ------------------------------------------- */

static int32_t b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Padded base64 into out; the bytes decoded, or -1 (not base64, or no room). */
static int32_t b64_dec(const char *in, uint32_t n, uint8_t *out, uint32_t cap)
{
    uint32_t k = 0;
    if (n % 4u) return -1;
    for (uint32_t i = 0; i < n; i += 4) {
        int32_t v[4];
        uint32_t pad = 0;
        for (uint32_t j = 0; j < 4; j++) {
            if (i + 4 == n && j >= 2 && in[i + j] == '=') {
                if (j == 2 && in[i + 3] != '=') return -1;
                pad++;
                v[j] = 0;
                continue;
            }
            v[j] = b64_val(in[i + j]);
            if (v[j] < 0 || pad) return -1;
        }
        uint32_t w =
            (uint32_t) v[0] << 18 | (uint32_t) v[1] << 12 | (uint32_t) v[2] << 6 | (uint32_t) v[3];
        uint32_t got = 3u - pad;
        if (k + got > cap) return -1;
        out[k++] = (uint8_t) (w >> 16);
        if (got > 1) out[k++] = (uint8_t) (w >> 8);
        if (got > 2) out[k++] = (uint8_t) w;
    }
    return (int32_t) k;
}

static char B64[(BROADSHEET_WIN / 3u + 1u) * 4u + 8u];

/* One files.read of at most BROADSHEET_WIN bytes at off: the base64 text
 * into B64 (*chars), and the file's size. */
static int read_b64(zxm_link_t *l, const char *path, int64_t off, uint32_t *chars, uint64_t *size)
{
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", path);
    zxm_p_u64(l, "offset", (uint64_t) off);
    zxm_p_u64(l, "max", BROADSHEET_WIN);
    zxm_p_str(l, "encoding", "base64");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t tk = zxm_get(l, l->result, "base64");
    if (tk < 0 || zxm_get_u64(l, l->result, "size", size)) return ZXM_E_PROTOCOL;
    int32_t m = w4_json_str(l->resp, &l->tok[tk], B64, sizeof B64);
    if (m < 0) return ZXM_E_TOOBIG;
    *chars = (uint32_t) m;
    return ZXM_OK;
}

/* The window at p (0 <= p < size), from B64 just read. ZXM_E_STATE when the
 * file has changed size since it was opened, or ends before p. */
static int fill(broadsheet_t *b, int64_t p, uint32_t chars, uint64_t size)
{
    if ((int64_t) size != b->size) return ZXM_E_STATE;
    int32_t k = b64_dec(B64, chars, b->win, BROADSHEET_WIN);
    if (k < 0) return ZXM_E_PROTOCOL;
    if (k == 0 && p < b->size) return ZXM_E_STATE;
    b->wbase = p;
    b->wlen = (uint32_t) k;
    return ZXM_OK;
}

static int load(broadsheet_t *b, int64_t p)
{
    uint32_t chars = 0;
    uint64_t size = 0;
    int rc = read_b64(b->link, b->path, p, &chars, &size);
    return rc ? rc : fill(b, p, chars, size);
}

/* The byte at p: 0..255, -1 outside the file, -2 when a read failed (b->rc). */
static int32_t byte_at(broadsheet_t *b, int64_t p)
{
    if (b->rc) return -2;
    if (p < 0 || p >= b->size) return -1;
    if (p < b->wbase || p >= b->wbase + (int64_t) b->wlen) {
        int rc = load(b, p);
        if (rc) {
            b->rc = rc;
            return -2;
        }
    }
    return b->win[p - b->wbase];
}

static bool is_space(int32_t c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == 0;
}

static bool is_digit(int32_t c)
{
    return c >= '0' && c <= '9';
}

static int64_t skip_space(broadsheet_t *b, int64_t p)
{
    while (is_space(byte_at(b, p))) p++;
    return p;
}

static bool match(broadsheet_t *b, int64_t p, const char *s)
{
    for (uint32_t i = 0; s[i]; i++)
        if (byte_at(b, p + i) != (uint8_t) s[i]) return false;
    return true;
}

/* 1..most digits at *p, moved past them; -1 for none or too many. */
static int64_t read_uint(broadsheet_t *b, int64_t *p, uint32_t most)
{
    int64_t v = 0;
    uint32_t n = 0;
    for (;;) {
        int32_t c = byte_at(b, *p);
        if (!is_digit(c)) break;
        if (++n > most) return -1;
        v = v * 10 + (c - '0');
        (*p)++;
    }
    return n ? v : -1;
}

/* ---- BM2: the cross-reference offsets -------------------------------------- */

/* One offset by BR2, kept when it is inside the file. */
static bool check_offset(broadsheet_t *b, int64_t off)
{
    if (b->nxref >= BROADSHEET_XREFS) return fail(b, BROADSHEET_F_LARGE, off);
    int64_t f = broadsheet_form_fault(b->header, off, b->size - off);
    if (f == 1) return fail(b, BROADSHEET_F_HEADER, off);
    if (f != 0) return fail(b, BROADSHEET_F_BOUNDS, off); /* 2, or past zxvd's largest file */
    b->xref[b->nxref++] = off;
    return true;
}

/* Not a table at off: "N G obj" there is a cross-reference stream. */
static bool not_table(broadsheet_t *b, int64_t off, int64_t p)
{
    if (read_uint(b, &p, OFF_DIGITS) >= 0 && is_space(byte_at(b, p))) {
        p = skip_space(b, p);
        if (read_uint(b, &p, GEN_DIGITS) >= 0 && is_space(byte_at(b, p)) &&
            match(b, skip_space(b, p), "obj"))
            return fail(b, BROADSHEET_F_STREAM, off);
    }
    return fail(b, BROADSHEET_F_FORM, off);
}

/* The trailer after a table: its /Prev into *prev (-1 for none). */
static bool read_trailer(broadsheet_t *b, int64_t p, int64_t *prev)
{
    int64_t end = p + BROADSHEET_TRAILER;
    *prev = -1;
    for (; p < end && byte_at(b, p) >= 0; p++) {
        if (match(b, p, "startxref")) return true;
        if (!match(b, p, "/Prev")) continue;
        int64_t q = p + 5;
        if (!is_space(byte_at(b, q))) continue; /* a longer name, /PrevSomething */
        q = skip_space(b, q);
        *prev = read_uint(b, &q, OFF_DIGITS);
        return *prev >= 0 ? true : fail(b, BROADSHEET_F_FORM, p);
    }
    return b->rc ? false : true;
}

/* The table at off (BM2): every in-use entry's offset checked; the
 * trailer's /Prev into *prev. *entries counts every entry read. */
static bool read_table(broadsheet_t *b, int64_t off, int64_t *prev, uint32_t *entries)
{
    int64_t p = skip_space(b, off);
    *prev = -1;
    if (!match(b, p, "xref")) return not_table(b, off, p);
    p += 4;
    for (;;) {
        p = skip_space(b, p);
        if (match(b, p, "trailer")) return read_trailer(b, p + 7, prev);
        int64_t first = read_uint(b, &p, OFF_DIGITS);
        if (first < 0 || byte_at(b, p) != ' ') return fail(b, BROADSHEET_F_FORM, p);
        p = skip_space(b, p);
        int64_t count = read_uint(b, &p, OFF_DIGITS);
        if (count < 0 || !is_space(byte_at(b, p))) return fail(b, BROADSHEET_F_FORM, p);
        p = skip_space(b, p);
        for (int64_t i = 0; i < count; i++) {
            if (*entries >= BROADSHEET_XREFS) return fail(b, BROADSHEET_F_LARGE, p);
            (*entries)++;
            int64_t q = p;
            int64_t eoff = read_uint(b, &q, OFF_DIGITS);
            if (eoff < 0 || q != p + OFF_DIGITS || byte_at(b, q) != ' ')
                return fail(b, BROADSHEET_F_FORM, p);
            q++;
            int64_t g = q;
            if (read_uint(b, &q, GEN_DIGITS) < 0 || q != g + GEN_DIGITS || byte_at(b, q) != ' ')
                return fail(b, BROADSHEET_F_FORM, p);
            int32_t kind = byte_at(b, ++q);
            int32_t eol = byte_at(b, ++q);
            if ((kind != 'n' && kind != 'f') || (eol != ' ' && eol != '\r' && eol != '\n'))
                return fail(b, BROADSHEET_F_FORM, p);
            eol = byte_at(b, ++q);
            if (eol == '\r' || eol == '\n') q++;
            if (kind == 'n' && !check_offset(b, eoff)) return false;
            p = q;
        }
    }
}

/* The last startxref in the file's tail, followed by %%EOF: its offset, or -1. */
static int64_t find_startxref(broadsheet_t *b)
{
    int64_t start = b->size > BROADSHEET_TAIL ? b->size - BROADSHEET_TAIL : 0;
    if (b->size < 9) return -1;
    if (start < b->wbase || b->size > b->wbase + (int64_t) b->wlen) {
        int rc = load(b, start);
        if (rc) {
            b->rc = rc;
            return -1;
        }
    }
    for (int64_t p = b->size - 9; p >= start; p--) {
        if (!match(b, p, "startxref")) continue;
        int64_t q = skip_space(b, p + 9);
        int64_t off = read_uint(b, &q, OFF_DIGITS);
        if (off < 0 || !match(b, skip_space(b, q), "%%EOF")) return -1;
        return off;
    }
    return -1;
}

/* ---- BM1, BM3: the header and the page ------------------------------------- */

static bool header_present(const broadsheet_t *b)
{
    const uint8_t *w = b->win;
    return b->wbase == 0 && b->wlen >= 8u && !memcmp(w, "%PDF-", 5) && is_digit(w[5]) &&
           w[6] == '.' && is_digit(w[7]);
}

/* A /MediaBox number at win[*k]: its whole part, the fraction dropped. */
static bool box_number(const broadsheet_t *b, uint32_t *k, int64_t *v)
{
    uint32_t i = *k, n = 0, frac = 0;
    bool neg = false;
    int64_t x = 0;
    while (i < b->wlen && is_space(b->win[i])) i++;
    if (i < b->wlen && (b->win[i] == '-' || b->win[i] == '+')) neg = b->win[i++] == '-';
    while (i < b->wlen && is_digit(b->win[i])) {
        if (++n > BOX_DIGITS) return false;
        x = x * 10 + (b->win[i++] - '0');
    }
    if (i < b->wlen && b->win[i] == '.')
        for (i++; i < b->wlen && is_digit(b->win[i]); i++) frac++;
    if (!n && !frac) return false;
    *v = neg ? -x : x;
    *k = i;
    return true;
}

static void page_box(broadsheet_t *b)
{
    static const char key[] = "/MediaBox";
    const uint32_t kl = sizeof key - 1u;
    for (uint32_t i = 0; i + kl <= b->wlen; i++) {
        if (memcmp(b->win + i, key, kl)) continue;
        uint32_t k = i + kl;
        int64_t v[4];
        while (k < b->wlen && is_space(b->win[k])) k++;
        if (k >= b->wlen || b->win[k] != '[') return;
        k++;
        for (uint32_t j = 0; j < 4u; j++)
            if (!box_number(b, &k, &v[j])) return;
        int64_t w = v[2] - v[0], h = v[3] - v[1];
        if (w < 1 || w > BROADSHEET_BOX_MAX || h < 1 || h > BROADSHEET_BOX_MAX) return;
        b->page_w = w;
        b->page_h = h;
        b->box_found = true;
        return;
    }
}

/* ---- BM5: the notes file --------------------------------------------------- */

static bool text_ok(const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if ((uint8_t) s[i] < 0x20u || (uint8_t) s[i] == 0x7Fu) return false;
    return true;
}

/* An integer at s[*k] (an optional '-', 1..NUM_DIGITS digits). */
static bool parse_int(const char *s, uint32_t n, uint32_t *k, int64_t *v)
{
    uint32_t i = *k, d = 0;
    bool neg = i < n && s[i] == '-';
    int64_t x = 0;
    if (neg) i++;
    while (i < n && is_digit(s[i])) {
        if (++d > NUM_DIGITS) return false;
        x = x * 10 + (s[i++] - '0');
    }
    if (!d) return false;
    *v = neg ? -x : x;
    *k = i;
    return true;
}

static bool parse_notes(broadsheet_t *b, const char *s, uint32_t n)
{
    static broadsheet_note_t next[BROADSHEET_NOTES];
    const uint32_t hl = sizeof NOTES_HEAD - 1u;
    uint32_t k = hl, m = 0;
    if (n < hl || memcmp(s, NOTES_HEAD, hl)) return false;
    while (k < n) {
        if (m == BROADSHEET_NOTES) return false;
        broadsheet_note_t *e = &next[m];
        int64_t *f[6] = {&e->note, &e->page, &e->x, &e->y, &e->kind, &e->stamp};
        uint32_t t = 0;
        for (uint32_t j = 0; j < 6u; j++) {
            if (!parse_int(s, n, &k, f[j]) || k >= n || s[k] != ' ') return false;
            k++;
        }
        while (k < n && s[k] != '\n') {
            if (t == BROADSHEET_TEXT) return false;
            e->text[t++] = s[k++];
        }
        if (k >= n || !text_ok(e->text, t)) return false; /* every line ends */
        k++;
        e->text[t] = 0;
        if (broadsheet_note_ok(e->note, e->page, e->x, e->y, e->kind, e->stamp) != 1) return false;
        for (uint32_t i = 0; i < m; i++)
            if (next[i].note == e->note) return false;
        m++;
    }
    memcpy(b->note, next, (size_t) m * sizeof next[0]);
    b->nnotes = m;
    return true;
}

static int load_notes(broadsheet_t *b)
{
    static char text[BROADSHEET_NOTES_MAX + 1];
    zxm_link_t *l = b->link;
    uint64_t size = 0;
    bool eof = false;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", b->notes_path);
    zxm_p_u64(l, "max", BROADSHEET_NOTES_MAX);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE && l->err_code == BROADSHEET_E_FILES && strstr(l->err, "no such file")) {
        b->notes_loaded = true; /* none yet */
        return ZXM_OK;
    }
    if (rc) return rc;
    int32_t t = zxm_get(l, l->result, "text");
    if (t < 0 || zxm_get_u64(l, l->result, "size", &size) ||
        zxm_get_bool(l, l->result, "eof", &eof))
        return ZXM_E_PROTOCOL;
    if (!eof || size > BROADSHEET_NOTES_MAX) return ZXM_E_ARG;
    int32_t n = w4_json_str(l->resp, &l->tok[t], text, sizeof text);
    if (n < 0 || !parse_notes(b, text, (uint32_t) n)) return ZXM_E_ARG;
    b->notes_loaded = true;
    b->notes_found = true;
    return ZXM_OK;
}

static bool put(char *out, uint32_t *k, uint32_t cap, const char *s, uint32_t n)
{
    if (*k + n > cap) return false;
    memcpy(out + *k, s, n);
    *k += n;
    return true;
}

static bool put_int(char *out, uint32_t *k, uint32_t cap, int64_t v)
{
    char d[24];
    uint32_t n = 0;
    bool neg = v < 0;
    uint64_t u = neg ? 0u - (uint64_t) v : (uint64_t) v;
    do {
        d[sizeof d - 1u - n++] = (char) ('0' + u % 10u);
        u /= 10u;
    } while (u);
    if (neg) d[sizeof d - 1u - n++] = '-';
    return put(out, k, cap, d + sizeof d - n, n);
}

/* The notes file's text (BM5); its length, or -1 when it does not fit. */
static int32_t compose(const broadsheet_t *b, char *out, uint32_t cap)
{
    uint32_t k = 0;
    if (!put(out, &k, cap, NOTES_HEAD, sizeof NOTES_HEAD - 1u)) return -1;
    for (uint32_t i = 0; i < b->nnotes; i++) {
        const broadsheet_note_t *e = &b->note[i];
        const int64_t f[6] = {e->note, e->page, e->x, e->y, e->kind, e->stamp};
        for (uint32_t j = 0; j < 6u; j++)
            if (!put_int(out, &k, cap, f[j]) || !put(out, &k, cap, " ", 1)) return -1;
        if (!put(out, &k, cap, e->text, (uint32_t) strlen(e->text)) || !put(out, &k, cap, "\n", 1))
            return -1;
    }
    return (int32_t) k;
}

/* ---- BM1..BM3: opening ----------------------------------------------------- */

static bool ends_pdf(const char *p, size_t n)
{
    if (n < 5u) return false;
    const char *e = p + n - 4u;
    return e[0] == '.' && (e[1] | 0x20) == 'p' && (e[2] | 0x20) == 'd' && (e[3] | 0x20) == 'f';
}

int broadsheet_open(broadsheet_t *b, const char *path)
{
    static int64_t seen[BROADSHEET_SECTIONS];
    const size_t sl = sizeof BROADSHEET_SUFFIX - 1u;
    size_t n = path ? strlen(path) : 0u;
    uint32_t chars = 0, entries = 0;
    uint64_t size = 0;
    if (!n || n + sl > BROADSHEET_PATH_MAX || !ends_pdf(path, n)) return ZXM_E_ARG;
    zxm_link_t *l = b->link;
    int rc = read_b64(l, path, 0, &chars, &size);
    if (rc) return rc; /* the document open before stays open */
    broadsheet_init(b, l);
    memcpy(b->path, path, n + 1u);
    memcpy(b->notes_path, path, n);
    memcpy(b->notes_path + n, BROADSHEET_SUFFIX, sl + 1u);
    if (size > BROADSHEET_FILE_MAX) {
        fail(b, BROADSHEET_F_LARGE, -1);
        return ZXM_E_ARG;
    }
    b->size = (int64_t) size;
    if ((rc = fill(b, 0, chars, size))) {
        broadsheet_init(b, l);
        return rc;
    }
    b->header = header_present(b) ? 1 : 0;
    page_box(b);
    if (!b->header) {
        fail(b, BROADSHEET_F_HEADER, -1); /* BR2: the header first */
    } else {
        int64_t at = find_startxref(b);
        if (at < 0) fail(b, BROADSHEET_F_FORM, -1);
        while (at >= 0 && check_offset(b, at)) {
            for (uint32_t i = 0; i < b->tables; i++)
                if (seen[i] == at) fail(b, BROADSHEET_F_FORM, at); /* a /Prev loop */
            if (b->fault) break;
            if (b->tables == BROADSHEET_SECTIONS) {
                fail(b, BROADSHEET_F_LARGE, at);
                break;
            }
            seen[b->tables++] = at;
            if (!read_table(b, at, &at, &entries)) break;
        }
    }
    if (b->rc) {
        rc = b->rc;
        broadsheet_init(b, l); /* a read failed: nothing is open */
        return rc;
    }
    if (!b->fault && broadsheet_all_inside(b->header, b->xref, b->nxref, b->size) != 1)
        fail(b, BROADSHEET_F_BOUNDS, -1);
    if (b->fault) return ZXM_E_ARG; /* refused: kept open to say why, nothing more read */
    b->accepted = 1;
    return load_notes(b);
}

/* ---- BM4: zoom to fit ------------------------------------------------------ */

static int64_t to_pixels(int64_t points)
{
    int64_t px = points * 4 / 3;
    return px < 1 ? 1 : px;
}

int broadsheet_fit(broadsheet_t *b, int64_t vieww, int64_t viewh)
{
    if (b->accepted != 1) return ZXM_E_STATE;
    int64_t z = broadsheet_fit_zoom(to_pixels(b->page_w), to_pixels(b->page_h), vieww, viewh);
    if (z < 0) return ZXM_E_ARG;
    b->zoom = z;
    return ZXM_OK;
}

/* ---- BM5: annotations ------------------------------------------------------ */

int broadsheet_note_add(broadsheet_t *b, int64_t page, int64_t x, int64_t y, int64_t kind,
                        int64_t stamp, const char *text, int64_t *note)
{
    if (b->accepted != 1 || !b->notes_loaded) return ZXM_E_STATE;
    size_t tl = text ? strlen(text) : 0u;
    if (tl > BROADSHEET_TEXT || !text_ok(text ? text : "", (uint32_t) tl)) return ZXM_E_ARG;
    if (b->nnotes == BROADSHEET_NOTES) return ZXM_E_TOOBIG;
    int64_t next = 1;
    for (uint32_t i = 0; i < b->nnotes; i++)
        if (b->note[i].note >= next) next = b->note[i].note + 1;
    if (broadsheet_note_ok(next, page, x, y, kind, stamp) != 1) return ZXM_E_ARG;
    broadsheet_note_t *e = &b->note[b->nnotes++];
    e->note = next, e->page = page, e->x = x, e->y = y, e->kind = kind, e->stamp = stamp;
    if (tl) memcpy(e->text, text, tl);
    e->text[tl] = 0;
    b->notes_dirty = true;
    if (note) *note = next;
    return ZXM_OK;
}

int broadsheet_note_remove(broadsheet_t *b, int64_t note)
{
    if (b->accepted != 1 || !b->notes_loaded) return ZXM_E_STATE;
    for (uint32_t i = 0; i < b->nnotes; i++) {
        if (b->note[i].note != note) continue;
        memmove(&b->note[i], &b->note[i + 1u], (size_t) (b->nnotes - i - 1u) * sizeof b->note[0]);
        b->nnotes--;
        b->notes_dirty = true;
        return ZXM_OK;
    }
    return ZXM_E_ARG;
}

int broadsheet_notes_save(broadsheet_t *b)
{
    static char text[BROADSHEET_NOTES_MAX + 1];
    if (b->accepted != 1 || !b->notes_loaded) return ZXM_E_STATE;
    int32_t len = compose(b, text, BROADSHEET_NOTES_MAX);
    if (len < 0) return ZXM_E_TOOBIG;
    zxm_link_t *l = b->link;
    zxm_begin(l, "files.write");
    zxm_p_str(l, "path", b->notes_path); /* never b->path: the document is not written */
    zxm_p_strn(l, "text", text, (uint32_t) len);
    if (!b->notes_found) zxm_p_bool(l, "exclusive", true);
    int rc = zxm_call(l);
    if (rc) return rc;
    uint64_t size = 0;
    if (zxm_get_u64(l, l->result, "size", &size) || size != (uint64_t) len) return ZXM_E_PROTOCOL;
    b->notes_found = true;
    b->notes_dirty = false;
    return ZXM_OK;
}

/* ---- BM6: printing --------------------------------------------------------- */

static bool uri_ok(const char *uri)
{
    return uri && uri[0] && strlen(uri) <= BROADSHEET_URI_MAX;
}

int broadsheet_printer(broadsheet_t *b, const char *uri)
{
    if (!uri_ok(uri)) return ZXM_E_ARG;
    zxm_link_t *l = b->link;
    b->printer_known = false;
    zxm_begin(l, "print.status");
    zxm_p_str(l, "uri", uri);
    int rc = zxm_call(l);
    if (rc) return rc;
    bool accepting = false, pdf = false;
    int32_t fm = zxm_get(l, l->result, "formats");
    if (zxm_get_bool(l, l->result, "accepting", &accepting) || fm < 0 ||
        l->tok[fm].type != W4_J_ARR)
        return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < zxm_len(l, fm); i++) {
        int32_t t = zxm_at(l, fm, i);
        if (t >= 0 && w4_json_str_eq(l->resp, &l->tok[t], "application/pdf")) pdf = true;
    }
    b->printer_known = true;
    b->accepting = accepting;
    b->takes_pdf = pdf;
    return ZXM_OK;
}

int broadsheet_print(broadsheet_t *b, const char *uri, uint32_t copies)
{
    if (!uri_ok(uri) || copies > BROADSHEET_COPIES_MAX) return ZXM_E_ARG;
    if (b->accepted != 1) return ZXM_E_STATE; /* only a well formed document */
    int rc = broadsheet_printer(b, uri);
    if (rc) return rc;
    if (!b->accepting || !b->takes_pdf) return ZXM_E_STATE;
    zxm_link_t *l = b->link;
    zxm_begin(l, "print.submit");
    zxm_p_str(l, "uri", uri);
    zxm_p_str(l, "path", b->path);
    if (copies) zxm_p_u64(l, "copies", copies);
    if ((rc = zxm_call(l))) return rc;
    uint64_t id = 0;
    char state[BROADSHEET_STATE_MAX];
    if (zxm_get_u64(l, l->result, "job_id", &id) || id == 0 || id > INT32_MAX ||
        zxm_get_str(l, l->result, "state", state, sizeof state))
        return ZXM_E_PROTOCOL;
    b->job = (int64_t) id;
    memcpy(b->job_state, state, sizeof state);
    return ZXM_OK;
}
