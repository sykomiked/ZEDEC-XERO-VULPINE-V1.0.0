/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* assize_model.c — Assize's model (assize_model.h). */
#include <string.h>

#include "assize_model.h"

#define ASSIZE_E_FILES    (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */
#define ASSIZE_FIELDS     17u      /* integer fields of a docket line (AM1) */
#define ASSIZE_LINE       (ASSIZE_FIELDS * 21u + ASSIZE_PATH_MAX + 2u)
#define ASSIZE_TEXT       (ASSIZE_ROWS * ASSIZE_LINE)
#define ASSIZE_PIECE      6000u /* bytes of text in one files.write, escaped at most twice */
#define ASSIZE_TAIL       1024u /* bytes of a PDF's end searched for startxref */
#define ASSIZE_DAY_MS     INT64_C(86400000)
#define ASSIZE_NONE       "-" /* no document */
#define ASSIZE_FORM_OPEN  1
#define ASSIZE_FORM_LODGE 2
#define ASSIZE_FORM_RULE  3
#define ADVICE            " Advice only: the ruling is the arbiter's."

/* ---- small text helpers, bounded ---- */

static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    size_t n = strlen(s);
    if (*at + n + 1u > cap) return false;
    memcpy(out + *at, s, n + 1u);
    *at += (uint32_t) n;
    return true;
}

static bool put_i64(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char d[24];
    uint32_t k = sizeof d - 1u;
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    d[k] = 0;
    do {
        d[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u);
    if (v < 0) d[--k] = '-';
    return put(out, cap, at, d + k);
}

static uint32_t len_of(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    while (s && n < cap && s[n]) n++;
    return n;
}

static void say(assize_t *a, const char *x, const char *y)
{
    uint32_t at = 0;
    a->notice[0] = 0;
    if (put(a->notice, sizeof a->notice, &at, x) && y) put(a->notice, sizeof a->notice, &at, y);
}

/* A decimal integer of t[*at..n) up to `end`, a sign allowed; false when
 * it is empty, not a number, past 18 digits or has no end. */
static bool get_i64(const char *t, uint32_t n, uint32_t *at, char end, int64_t *v)
{
    uint32_t i = *at, digits = 0;
    bool neg = i < n && t[i] == '-';
    int64_t x = 0;
    if (neg) i++;
    while (i < n && t[i] >= '0' && t[i] <= '9') {
        if (++digits > 18u) return false;
        x = x * 10 + (t[i++] - '0');
    }
    if (!digits || i >= n || t[i] != end) return false;
    *v = neg ? -x : x;
    *at = i + 1u;
    return true;
}

/* text of t[*at..n) up to `end`, at most cap - 1 bytes */
static bool get_text(const char *t, uint32_t n, uint32_t *at, char end, char *out, uint32_t cap)
{
    uint32_t k = 0, i = *at;
    while (i < n && t[i] != end) {
        if (k + 1u >= cap) return false;
        out[k++] = t[i++];
    }
    if (i >= n) return false;
    out[k] = 0;
    *at = i + 1u;
    return true;
}

/* AM1: a document's path is a Hoard path of 1..120 bytes with no control
 * byte or tab, no leading '/', '.' or '-', no "..", and not Assize's own. */
static bool path_ok(const char *p)
{
    uint32_t n = len_of(p, ASSIZE_PATH_MAX + 1u);
    if (!p || !n || n > ASSIZE_PATH_MAX || p[0] == '/' || p[0] == '.' || p[0] == '-') return false;
    if (!strncmp(p, ASSIZE_DIR "/", sizeof ASSIZE_DIR) || strstr(p, "..")) return false;
    for (uint32_t i = 0; i < n; i++)
        if ((unsigned char) p[i] < 0x20u || p[i] == 0x7F) return false;
    return true;
}

static bool ends_with(const char *p, const char *tail)
{
    size_t n = strlen(p), k = strlen(tail);
    return n >= k && !strcmp(p + n - k, tail);
}

/* ---- forms (sealwax) ---- */

/* one field of a form: its sealwax kind, required flag and bounds */
typedef struct {
    int64_t kind, required, lo, hi;
} assize_field_t;

/* AM2, AM3: count a form's missing and outside fields, each field's
 * definition first held to sealwax.field_record_ok and its bounds; a
 * definition that does not hold makes the form never ready. value[k] is a
 * number, or a text's length; filled[k] says whether anything was given. */
static bool form_ready(int64_t form, const assize_field_t *f, const int64_t *value,
                       const bool *filled, uint32_t n, int64_t extra_outside)
{
    int64_t missing = 0, outside = extra_outside;
    for (uint32_t k = 0; k < n; k++) {
        if (!sealwax_record_fits(form, (int64_t) k + 1, f[k].kind, f[k].required, f[k].lo, f[k].hi,
                                 0) ||
            !sealwax_bounds_ok(f[k].kind, f[k].lo, f[k].hi))
            return false;
        if (!filled[k]) {
            missing += f[k].required;
            continue;
        }
        outside += value[k] < f[k].lo || value[k] > f[k].hi;
    }
    return sealwax_sign_ready((int64_t) n, missing, outside);
}

/* ---- civil days ---- */

static int64_t opened_day(const assize_t *a)
{
    return a->n ? a->line[0].filed : 0;
}

static bool is_party(const assize_t *a, int64_t party)
{
    return a->n && party >= 1 && (party == a->line[0].party || party == a->line[0].other);
}

/* ---- zxvd ---- */

static bool no_such(const zxm_link_t *l, int rc)
{
    return rc == ZXM_E_REMOTE && l->err_code == ASSIZE_E_FILES && strstr(l->err, "no such");
}

/* files.read of a whole small text file; its length, ZXM_E_STATE when it
 * does not exist, ZXM_E_TOOBIG when it does not fit. */
static int32_t read_text(assize_t *a, const char *path, char *text, uint32_t cap)
{
    zxm_link_t *l = a->link;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", path);
    zxm_p_u64(l, "max", cap - 1u);
    int rc = zxm_call(l);
    if (no_such(l, rc)) return ZXM_E_STATE;
    if (rc) return rc;
    bool eof = false;
    int32_t t = zxm_get(l, l->result, "text");
    if (t < 0 || zxm_get_bool(l, l->result, "eof", &eof)) return ZXM_E_PROTOCOL;
    if (!eof) return ZXM_E_TOOBIG;
    int32_t len = w4_json_str(l->resp, &l->tok[t], text, cap);
    return len < 0 ? ZXM_E_TOOBIG : len;
}

static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    return c == '+' ? 62 : c == '/' ? 63 : -1;
}

/* base64 (padded) to bytes; their count, or -1 */
static int32_t b64_decode(const char *in, uint32_t n, uint8_t *out, uint32_t cap)
{
    uint32_t o = 0;
    if (n % 4u) return -1;
    for (uint32_t i = 0; i < n; i += 4u) {
        uint32_t v = 0, pad = 0;
        for (uint32_t j = 0; j < 4u; j++) {
            int d = b64_val(in[i + j]);
            if (in[i + j] == '=' && i + 4u == n && j >= 2u) {
                pad++;
                d = 0;
            } else if (d < 0 || pad) {
                return -1;
            }
            v = v << 6 | (uint32_t) d;
        }
        for (uint32_t j = 0; j < 3u - pad; j++) {
            if (o >= cap) return -1;
            out[o++] = (uint8_t) (v >> (16u - 8u * j));
        }
    }
    return (int32_t) o;
}

/* files.read of bytes [off, off + max) of a file, base64; their count, or a ZXM_E_* code */
static int32_t read_bytes(assize_t *a, const char *path, uint64_t off, uint32_t max, uint8_t *out)
{
    static char b64[ASSIZE_TAIL * 2u];
    zxm_link_t *l = a->link;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", path);
    zxm_p_u64(l, "offset", off);
    zxm_p_u64(l, "max", max);
    zxm_p_str(l, "encoding", "base64");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "base64", b64, sizeof b64)) return ZXM_E_PROTOCOL;
    int32_t got = b64_decode(b64, len_of(b64, sizeof b64), out, max);
    return got < 0 ? ZXM_E_PROTOCOL : got;
}

/* AM3: a PDF is broadsheet.well_formed: "%PDF-" first, and the offset its last
 * startxref gives inside the file (header 1, off, room = size - off). */
static int pdf_well_formed(assize_t *a, const char *path, int64_t size, bool *well)
{
    static uint8_t buf[ASSIZE_TAIL];
    int64_t header = 0, off = -1;
    *well = false;
    if (size < 5 || size > BROADSHEET_FILE_MAX) return ZXM_OK;
    int32_t got = read_bytes(a, path, 0, 5u, buf);
    if (got < 0) return got;
    header = got == 5 && !memcmp(buf, "%PDF-", 5);
    uint64_t from = size > (int64_t) ASSIZE_TAIL ? (uint64_t) size - ASSIZE_TAIL : 0u;
    got = read_bytes(a, path, from, ASSIZE_TAIL, buf);
    if (got < 0) return got;
    for (int32_t i = got - 9; i >= 0; i--) {
        if (memcmp(buf + i, "startxref", 9)) continue;
        int32_t k = i + 9;
        while (k < got && (buf[k] == ' ' || buf[k] == '\r' || buf[k] == '\n')) k++;
        int64_t v = 0;
        int32_t digits = 0;
        while (k < got && buf[k] >= '0' && buf[k] <= '9' && digits < 10) {
            v = v * 10 + (buf[k++] - '0');
            digits++;
        }
        if (digits) off = v;
        break;
    }
    *well = broadsheet_well_formed(header, off, size - off) == 1;
    return ZXM_OK;
}

/* files.stat of a lodged document: ZXM_E_ARG for a folder or none */
static int doc_size(assize_t *a, const char *path, int64_t *size)
{
    zxm_link_t *l = a->link;
    bool dir = false;
    zxm_begin(l, "files.stat");
    zxm_p_str(l, "path", path);
    int rc = zxm_call(l);
    if (no_such(l, rc)) return ZXM_E_ARG;
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "dir", &dir) || zxm_get_i64(l, l->result, "size", size) ||
        *size < 0)
        return ZXM_E_PROTOCOL;
    return dir ? ZXM_E_ARG : ZXM_OK;
}

/* files.write of text whole, in pieces cut on character boundaries. */
static int write_text(assize_t *a, const char *path, const char *text, uint32_t len)
{
    zxm_link_t *l = a->link;
    zxm_begin(l, "files.mkdir");
    zxm_p_str(l, "path", ASSIZE_DIR);
    int rc = zxm_call(l);
    if (rc &&
        !(rc == ZXM_E_REMOTE && l->err_code == ASSIZE_E_FILES && strstr(l->err, "already exists")))
        return rc;
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t n = len - at < ASSIZE_PIECE ? len - at : ASSIZE_PIECE;
        if (at + n < len) n = zxm_utf8_cut(text + at, n);
        bool last = at + n >= len;
        zxm_begin(l, "files.write");
        zxm_p_str(l, "path", path);
        zxm_p_strn(l, "text", text + at, n);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
    } while (at < len);
    return ZXM_OK;
}

/* The journal's newest seq (0 with no journal or no entry). */
static int journal_newest(assize_t *a, int64_t *seq)
{
    zxm_link_t *l = a->link;
    bool on = false;
    *seq = 0;
    zxm_begin(l, "journal.query");
    zxm_p_u64(l, "max", 1);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "entries");
    if (zxm_get_bool(l, l->result, "on", &on) || arr < 0 || l->tok[arr].type != W4_J_ARR)
        return ZXM_E_PROTOCOL;
    if (!on || !zxm_len(l, arr)) return ZXM_OK;
    int32_t e = zxm_at(l, arr, 0);
    if (e < 0 || zxm_get_i64(l, e, "seq", seq) || *seq < 1 || *seq > WITNESS_SEQ_MAX)
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

/* ---- AM1: the case file ---- */

static bool case_file(int64_t caseno, char *out, uint32_t cap)
{
    char d[8];
    if (caseno < 1 || caseno > ASSIZE_CASE_MAX) return false;
    int64_t v = caseno;
    for (int k = 5; k >= 0; k--) {
        d[k] = (char) ('0' + (int) (v % 10));
        v /= 10;
    }
    d[6] = 0;
    uint32_t at = 0;
    out[0] = 0;
    return put(out, cap, &at, ASSIZE_DIR "/case-") && put(out, cap, &at, d) &&
           put(out, cap, &at, ".txt");
}

static bool in(int64_t v, int64_t lo, int64_t hi)
{
    return v >= lo && v <= hi;
}

static bool line_ok(int64_t caseno, const assize_line_t *r)
{
    bool doc = !strcmp(r->path, ASSIZE_NONE) || path_ok(r->path);
    return assize_node_ok(caseno, r->seq, r->kind, r->party, r->filed, r->claim) &&
           in(r->filed, 0, ASSIZE_DAY_MAX) &&
           witness_record_fits(r->seq, r->prev, r->kind, r->filed * ASSIZE_DAY_MS) &&
           in(r->seq, 1, ASSIZE_SEQ_MAX) && in(r->prev, 0, r->seq - 1) &&
           in(r->kind, ASSIZE_OPENED, ASSIZE_NOTE) && in(r->party, 0, ASSIZE_PARTY_MAX) &&
           in(r->other, 0, ASSIZE_SEQ_MAX) && in(r->due, 0, ASSIZE_DAY_MAX) &&
           in(r->claim, 0, ASSIZE_CLAIM_MAX) && in(r->site, 0, ASSIZE_CLAIM_MAX) &&
           in(r->pages, 0, ASSIZE_DAY_MAX) && in(r->exhibits, 0, ASSIZE_SEQ_MAX) &&
           in(r->page, 0, BROADSHEET_PAGE_MAX) && in(r->x, 0, BROADSHEET_POINT_MAX) &&
           in(r->y, 0, BROADSHEET_POINT_MAX) && in(r->mark, 0, BROADSHEET_UNDERLINE) &&
           in(r->finding, 0, ASSIZE_DISMISSED) && in(r->jseq, 0, WITNESS_SEQ_MAX) && doc;
}

static int write_case(assize_t *a)
{
    static char text[ASSIZE_TEXT + 1u];
    uint32_t at = 0;
    text[0] = 0;
    for (uint32_t i = 0; i < a->n; i++) {
        const assize_line_t *r = &a->line[i];
        const int64_t f[ASSIZE_FIELDS] = {r->seq,      r->prev, r->kind,  r->party, r->other,
                                          r->filed,    r->due,  r->claim, r->site,  r->pages,
                                          r->exhibits, r->page, r->x,     r->y,     r->mark,
                                          r->finding,  r->jseq};
        for (uint32_t k = 0; k < ASSIZE_FIELDS; k++)
            if (!put_i64(text, sizeof text, &at, f[k]) || !put(text, sizeof text, &at, "\t"))
                return ZXM_E_TOOBIG;
        if (!put(text, sizeof text, &at, r->path) || !put(text, sizeof text, &at, "\n"))
            return ZXM_E_TOOBIG;
    }
    return write_text(a, a->file, text, at);
}

static int read_case(assize_t *a)
{
    static char text[ASSIZE_TEXT + 1u];
    int32_t len = read_text(a, a->file, text, sizeof text);
    if (len < 0) return len;
    for (uint32_t at = 0; at < (uint32_t) len;) {
        if (a->n >= ASSIZE_ROWS) return ZXM_E_PROTOCOL;
        assize_line_t *r = &a->line[a->n];
        int64_t *f[ASSIZE_FIELDS] = {&r->seq,      &r->prev, &r->kind,  &r->party, &r->other,
                                     &r->filed,    &r->due,  &r->claim, &r->site,  &r->pages,
                                     &r->exhibits, &r->page, &r->x,     &r->y,     &r->mark,
                                     &r->finding,  &r->jseq};
        for (uint32_t k = 0; k < ASSIZE_FIELDS; k++)
            if (!get_i64(text, (uint32_t) len, &at, '\t', f[k])) return ZXM_E_PROTOCOL;
        if (!get_text(text, (uint32_t) len, &at, '\n', r->path, sizeof r->path))
            return ZXM_E_PROTOCOL;
        if (!line_ok(a->caseno, r) || assize_find(a, r->seq) >= 0 ||
            (r->kind == ASSIZE_OPENED) != (a->n == 0))
            return ZXM_E_PROTOCOL;
        a->n++;
    }
    return a->n ? ZXM_OK : ZXM_E_PROTOCOL;
}

void assize_init(assize_t *a, zxm_link_t *link)
{
    memset(a, 0, sizeof *a);
    a->link = link;
}

int32_t assize_find(const assize_t *a, int64_t seq)
{
    for (uint32_t i = 0; i < a->n; i++)
        if (a->line[i].seq == seq) return (int32_t) i;
    return -1;
}

int assize_load(assize_t *a, int64_t caseno)
{
    a->n = 0;
    a->loaded = false;
    if (!case_file(caseno, a->file, sizeof a->file)) return ZXM_E_ARG;
    a->caseno = caseno;
    int rc = read_case(a);
    if (rc) {
        a->n = 0;
        say(a, rc == ZXM_E_STATE ? "No such case." : "The case file could not be read: ",
            rc == ZXM_E_STATE ? NULL : a->link->err);
        return rc;
    }
    a->loaded = true;
    return ZXM_OK;
}

/* ---- AM2: opening ---- */

int assize_open(assize_t *a, int64_t caseno, int64_t claimant, int64_t respondent, int64_t today,
                int64_t due, bool owner_confirmed)
{
    static const assize_field_t form[5] = {{SEALWAX_NUMBER, 1, 1, ASSIZE_CASE_MAX},
                                           {SEALWAX_NUMBER, 1, 1, ASSIZE_PARTY_MAX},
                                           {SEALWAX_NUMBER, 1, 1, ASSIZE_PARTY_MAX},
                                           {SEALWAX_NUMBER, 1, 0, ASSIZE_DAY_MAX},
                                           {SEALWAX_NUMBER, 1, 0, ASSIZE_DAY_MAX}};
    const int64_t value[5] = {caseno, claimant, respondent, today, due};
    static const bool filled[5] = {true, true, true, true, true};
    char file[ASSIZE_FILE_MAX];
    int64_t left = 0;
    if (!form_ready(ASSIZE_FORM_OPEN, form, value, filled, 5u,
                    (claimant == respondent) + (due < today)) ||
        !assize_days_left(due, today, &left) || left > ASSIZE_LEFT_MAX ||
        !case_file(caseno, file, sizeof file))
        return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;

    static char probe[ASSIZE_TEXT + 1u];
    int32_t got = read_text(a, file, probe, sizeof probe);
    if (got >= 0 || got == ZXM_E_TOOBIG) {
        say(a, "A case of that number is already open; nothing was changed.", NULL);
        return ZXM_E_STATE;
    }
    if (got != ZXM_E_STATE) return got;
    int64_t jseq = 0;
    int rc = journal_newest(a, &jseq);
    if (rc) return rc;

    assize_line_t keep[1];
    uint32_t keep_n = a->n;
    bool keep_loaded = a->loaded;
    int64_t keep_case = a->caseno;
    char keep_file[ASSIZE_FILE_MAX];
    memcpy(keep, a->line, sizeof keep);
    memcpy(keep_file, a->file, sizeof keep_file);

    assize_line_t *r = &a->line[0];
    memset(r, 0, sizeof *r);
    r->seq = 1;
    r->kind = ASSIZE_OPENED;
    r->party = claimant;
    r->other = respondent;
    r->filed = today;
    r->due = due;
    r->jseq = jseq;
    memcpy(r->path, ASSIZE_NONE, sizeof ASSIZE_NONE);
    a->n = 1;
    a->caseno = caseno;
    memcpy(a->file, file, sizeof file);
    rc = line_ok(caseno, r) ? write_case(a) : ZXM_E_ARG;
    if (rc) {
        memcpy(a->line, keep, sizeof keep);
        memcpy(a->file, keep_file, sizeof keep_file);
        a->n = keep_n;
        a->loaded = keep_loaded;
        a->caseno = keep_case;
        return rc;
    }
    a->loaded = true;
    say(a, "Case opened; the respondent's deadline is set. Notify the parties: ", file);
    return ZXM_OK;
}

/* ---- AM4: the evidence matrix ---- */

/* the site a filing asserts for a claim, 0 when no filing asserts it */
static int64_t asserted_site(const assize_t *a, int64_t claim)
{
    for (uint32_t i = 0; i < a->n; i++)
        if (a->line[i].kind == ASSIZE_FILING && a->line[i].claim == claim) return a->line[i].site;
    return 0;
}

static bool ruled(const assize_t *a, int64_t claim)
{
    for (uint32_t i = 0; i < a->n; i++)
        if (a->line[i].kind == ASSIZE_RULING && a->line[i].claim == claim) return true;
    return false;
}

lpres_state_t assize_claim(const assize_t *a, int64_t claim)
{
    int64_t cl[ASSIZE_ROWS], says[ASSIZE_ROWS];
    uint32_t n = 0;
    int32_t mine = -1;
    if (claim < 1 || claim > ASSIZE_CLAIM_MAX) return LPRES_STATE_NEITHER;
    for (uint32_t i = 0; i < a->n; i++) {
        const assize_line_t *r = &a->line[i];
        if (r->kind != ASSIZE_EVIDENCE || r->claim < 1) continue;
        if (r->claim == claim && mine < 0) mine = (int32_t) n;
        cl[n] = r->claim;
        says[n++] = assize_says(asserted_site(a, r->claim), r->site);
    }
    if (mine < 0) return LPRES_STATE_NEITHER;
    int64_t code = assize_claim_state(cl, says, n, (uint32_t) mine);
    return code < 0 ? LPRES_STATE_NEITHER : (lpres_state_t) code;
}

bool assize_escrowed(const assize_t *a, int64_t claim)
{
    return assize_claim(a, claim) == LPRES_STATE_BOTH && !ruled(a, claim);
}

/* ---- AM3: lodging ---- */

static bool lodged(const assize_t *a, const char *path)
{
    for (uint32_t i = 0; i < a->n; i++)
        if (!strcmp(a->line[i].path, path)) return true;
    return false;
}

/* append r as the docket's next line (its seq from tallystick.next_doc_number), stamped
 * with the journal, and write the case; on failure the case is as it was */
static int append(assize_t *a, assize_line_t *r)
{
    if (a->n >= ASSIZE_ROWS) return ZXM_E_TOOBIG;
    int64_t last = a->line[a->n - 1u].seq, seq = assize_next_seq(last), jseq = 0;
    if (seq < 0) {
        say(a, "This case's docket is full (9999 lines); nothing was lodged.", NULL);
        return ZXM_E_STATE;
    }
    int rc = journal_newest(a, &jseq);
    if (rc) return rc;
    r->seq = seq;
    r->prev = last;
    r->jseq = jseq;
    if (!line_ok(a->caseno, r)) return ZXM_E_ARG;
    a->line[a->n++] = *r;
    rc = write_case(a);
    if (rc) a->n--;
    return rc;
}

int assize_lodge(assize_t *a, int64_t kind, int64_t party, int64_t today, int64_t pages,
                 int64_t exhibits, int64_t claim, int64_t site, const char *path,
                 bool owner_confirmed, int64_t *docket)
{
    static const assize_field_t form[7] = {
        {SEALWAX_NUMBER, 1, 1, ASSIZE_PARTY_MAX}, {SEALWAX_NUMBER, 1, 0, ASSIZE_DAY_MAX},
        {SEALWAX_NUMBER, 1, 0, ASSIZE_DAY_MAX},   {SEALWAX_NUMBER, 1, 0, ASSIZE_SEQ_MAX},
        {SEALWAX_NUMBER, 1, 1, ASSIZE_CLAIM_MAX}, {SEALWAX_NUMBER, 1, 0, ASSIZE_CLAIM_MAX},
        {SEALWAX_TEXT, 1, 1, ASSIZE_PATH_MAX}};
    if (!a->loaded) return ZXM_E_STATE;
    if (!docket || (kind != ASSIZE_FILING && kind != ASSIZE_EVIDENCE)) return ZXM_E_ARG;
    uint32_t plen = len_of(path, ASSIZE_PATH_MAX + 1u);
    const int64_t value[7] = {party, today, pages, exhibits, claim, site, (int64_t) plen};
    const bool filled[7] = {true, true, true, true, true, true, plen > 0};
    int64_t asserted = asserted_site(a, claim);
    /* a filing asserts a claim once, at a site; evidence bears on an asserted claim */
    int64_t wrong = (plen && !path_ok(path)) + (today < opened_day(a)) +
                    (kind == ASSIZE_FILING ? (site < 1) + (asserted != 0) : (asserted == 0));
    if (!form_ready(ASSIZE_FORM_LODGE, form, value, filled, 7u, wrong) || lodged(a, path))
        return ZXM_E_ARG;
    if (ruled(a, claim)) {
        say(a, "The arbiter has ruled on this claim; nothing more is lodged on it.", NULL);
        return ZXM_E_STATE;
    }
    int64_t left = 0;
    if (!assize_days_left(a->line[0].due, today, &left) ||
        !assize_filing_accepted(is_party(a, party), left, pages, exhibits)) {
        say(a,
            !is_party(a, party) ? "Not accepted: not lodged by a party to the case."
            : left < 0          ? "Not accepted: the deadline has passed."
                                : "Not accepted: past the procedural limits (250 pages, 99 "
                                  "exhibits).",
            NULL);
        return ZXM_E_STATE;
    }
    if (!owner_confirmed) return ZXM_E_STATE;

    int64_t size = 0;
    int rc = doc_size(a, path, &size);
    if (rc) return rc;
    if (ends_with(path, ".pdf")) {
        bool well = false;
        rc = pdf_well_formed(a, path, size, &well);
        if (rc) return rc;
        if (!well) {
            say(a, "Not a well-formed PDF, so not lodged: ", path);
            return ZXM_E_ARG;
        }
    }
    assize_line_t r;
    memset(&r, 0, sizeof r);
    r.kind = kind;
    r.party = party;
    r.filed = today;
    r.claim = claim;
    r.site = site;
    r.pages = pages;
    r.exhibits = exhibits;
    memcpy(r.path, path, plen + 1u);
    rc = append(a, &r);
    if (rc) return rc;
    *docket = assize_docket_number(a->caseno, r.seq);
    say(a,
        kind == ASSIZE_EVIDENCE && assize_escrowed(a, claim)
            ? "Lodged. The evidence on this claim disagrees: both are kept, never merged, and the "
              "claim is held in escrow until the arbiter rules: "
            : "Lodged on the docket; notify the other party: ",
        path);
    return ZXM_OK;
}

/* ---- AM5: rulings ---- */

int64_t assize_finding_follows(const assize_t *a, int64_t claim, int64_t finding)
{
    return twelvefold_kept((int64_t) assize_claim(a, claim), finding);
}

int assize_rule(assize_t *a, int64_t claim, int64_t finding, int64_t today, const char *path,
                bool owner_confirmed)
{
    static const assize_field_t form[4] = {{SEALWAX_NUMBER, 1, 1, ASSIZE_CLAIM_MAX},
                                           {SEALWAX_NUMBER, 1, ASSIZE_UPHELD, ASSIZE_DISMISSED},
                                           {SEALWAX_NUMBER, 1, 0, ASSIZE_DAY_MAX},
                                           {SEALWAX_TEXT, 1, 1, ASSIZE_PATH_MAX}};
    if (!a->loaded) return ZXM_E_STATE;
    uint32_t plen = len_of(path, ASSIZE_PATH_MAX + 1u);
    const int64_t value[4] = {claim, finding, today, (int64_t) plen};
    const bool filled[4] = {true, true, true, plen > 0};
    int64_t wrong =
        (plen && !path_ok(path)) + (today < opened_day(a)) + (asserted_site(a, claim) == 0);
    if (!form_ready(ASSIZE_FORM_RULE, form, value, filled, 4u, wrong) || lodged(a, path))
        return ZXM_E_ARG;
    if (ruled(a, claim)) {
        say(a, "This claim is already ruled; a ruling is recorded once.", NULL);
        return ZXM_E_STATE;
    }
    if (!owner_confirmed) return ZXM_E_STATE;
    int64_t size = 0;
    int rc = doc_size(a, path, &size);
    if (rc) return rc;
    assize_line_t r;
    memset(&r, 0, sizeof r);
    r.kind = ASSIZE_RULING;
    r.filed = today;
    r.claim = claim;
    r.finding = finding;
    memcpy(r.path, path, plen + 1u);
    rc = append(a, &r);
    if (rc) return rc;
    say(a,
        assize_finding_follows(a, claim, finding) == 1
            ? "The arbiter's ruling is recorded and the escrow released; the finding follows from "
              "the evidence." ADVICE
            : "The arbiter's ruling is recorded and the escrow released; the finding does not "
              "follow from the evidence as lodged, and it stands." ADVICE,
        NULL);
    return ZXM_OK;
}

/* ---- AM6: hearings and annotations ---- */

int assize_hearing(assize_t *a, int64_t day, int64_t today, bool owner_confirmed)
{
    if (!a->loaded) return ZXM_E_STATE;
    if (!in(today, opened_day(a), ASSIZE_DAY_MAX) || !in(day, today, ASSIZE_DAY_MAX))
        return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    assize_line_t r;
    memset(&r, 0, sizeof r);
    r.kind = ASSIZE_HEARING;
    r.filed = today;
    r.due = day;
    memcpy(r.path, ASSIZE_NONE, sizeof ASSIZE_NONE);
    int rc = append(a, &r);
    if (!rc) say(a, "Hearing set; notify the parties of its day.", NULL);
    return rc;
}

int assize_annotate(assize_t *a, int64_t target, int64_t page, int64_t x, int64_t y, int64_t mark,
                    int64_t today, bool owner_confirmed)
{
    if (!a->loaded) return ZXM_E_STATE;
    int32_t t = assize_find(a, target);
    if (t < 0 || a->line[t].kind != ASSIZE_EVIDENCE || !in(today, opened_day(a), ASSIZE_DAY_MAX) ||
        page > a->line[t].pages || a->n >= ASSIZE_ROWS)
        return ZXM_E_ARG;
    int64_t note = assize_next_seq(a->line[a->n - 1u].seq);
    if (note < 0 || !broadsheet_note_ok(note, page, x, y, mark, today * ASSIZE_DAY_MS))
        return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    assize_line_t r;
    memset(&r, 0, sizeof r);
    r.kind = ASSIZE_NOTE;
    r.filed = today;
    r.other = target;
    r.claim = a->line[t].claim;
    r.page = page;
    r.x = x;
    r.y = y;
    r.mark = mark;
    memcpy(r.path, ASSIZE_NONE, sizeof ASSIZE_NONE);
    return append(a, &r);
}

/* ---- AM7: the trail ---- */

lpres_state_t assize_trail(assize_t *a)
{
    if (!a->loaded || !a->n) {
        say(a, "No case is open: nothing to prove.", NULL);
        return LPRES_STATE_NEITHER;
    }
    bool chain = true;
    for (uint32_t i = 0; i < a->n; i++)
        chain =
            chain && witness_linked(a->line[i].seq, a->line[i].prev, i ? a->line[i - 1u].seq : 0);
    lpres_state_t own = chain ? LPRES_STATE_TRUE : LPRES_STATE_FALSE, jr = LPRES_STATE_NEITHER;

    zxm_link_t *l = a->link;
    bool on = false, ok = false;
    int64_t first = 0, last = 0;
    zxm_begin(l, "journal.verify");
    if (zxm_call(l) == ZXM_OK && !zxm_get_bool(l, l->result, "on", &on) && on &&
        !zxm_get_bool(l, l->result, "ok", &ok) && !zxm_get_i64(l, l->result, "first_seq", &first) &&
        !zxm_get_i64(l, l->result, "last_seq", &last)) {
        bool inside = ok;
        for (uint32_t i = 0; i < a->n; i++)
            if (a->line[i].jseq && (a->line[i].jseq < first || a->line[i].jseq > last))
                inside = false;
        jr = inside ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    }
    lpres_state_t st = lpres_kjoin(own, jr);
    say(a,
        st == LPRES_STATE_TRUE    ? "The docket's trail holds."
        : st == LPRES_STATE_BOTH  ? "The docket and the journal disagree: held for a person."
        : st == LPRES_STATE_FALSE ? "The docket's trail is broken."
                                  : "Nothing to prove.",
        NULL);
    return st;
}

/* ---- AM8: measures ---- */

int assize_respond_left(const assize_t *a, int64_t today, int64_t *left)
{
    if (!a->loaded) return ZXM_E_STATE;
    return assize_days_left(a->line[0].due, today, left) ? ZXM_OK : ZXM_E_ARG;
}

int64_t assize_count(const assize_t *a, int64_t kind)
{
    int64_t k[ASSIZE_ROWS];
    for (uint32_t i = 0; i < a->n; i++) k[i] = a->line[i].kind;
    return witness_kind_count(k, a->n, kind);
}

int64_t assize_claims_of(const assize_t *a, int64_t code)
{
    int64_t st[ASSIZE_ROWS];
    uint32_t n = 0;
    for (uint32_t i = 0; i < a->n; i++)
        if (a->line[i].kind == ASSIZE_FILING) st[n++] = (int64_t) assize_claim(a, a->line[i].claim);
    return witness_kind_count(st, n, code);
}
