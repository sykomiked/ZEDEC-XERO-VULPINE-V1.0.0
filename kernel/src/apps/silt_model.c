/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* silt_model.c — Silt's model (silt_model.h). */
#include <string.h>

#include "silt_model.h"

#define SILT_E_FILES (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */
#define SILT_FIELDS  12u      /* integer fields of a register line (SM1) */
#define SILT_LINE    (SILT_FIELDS * 21u + SILT_REF_MAX + SILT_PATH_MAX + 3u)
#define SILT_TEXT    (SILT_ROWS * SILT_LINE)
#define SILT_PIECE   6000u /* bytes of text in one files.write, escaped at most twice */
#define SILT_TAIL    1024u /* bytes of a PDF's end searched for startxref */
#define SILT_DAY_MAX INT64_C(999999)
#define SILT_RECEIPT 6 /* SM2: the receipt's fields */
#define ADVICE       " Advice only: the owner confirms, and nothing is erased."

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

static void say(silt_t *s, const char *a, const char *b)
{
    uint32_t at = 0;
    s->notice[0] = 0;
    if (put(s->notice, sizeof s->notice, &at, a) && b) put(s->notice, sizeof s->notice, &at, b);
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

/* SM1: a record's path is a Hoard path of 1..200 bytes with no control
 * byte or tab, no leading '/' or '.', no "..", and not Silt's own. */
static bool path_ok(const char *p)
{
    uint32_t n = len_of(p, SILT_PATH_MAX + 1u);
    if (!p || !n || n > SILT_PATH_MAX || p[0] == '/' || p[0] == '.') return false;
    if (!strncmp(p, SILT_DIR "/", sizeof SILT_DIR) || strstr(p, "..")) return false;
    for (uint32_t i = 0; i < n; i++)
        if ((unsigned char) p[i] < 0x20u || p[i] == 0x7F) return false;
    return true;
}

/* a reference: 1..32 bytes of letters, digits, '-', '/' and '.' */
static bool ref_ok(const char *r)
{
    uint32_t n = len_of(r, SILT_REF_MAX + 1u);
    if (!r || !n || n > SILT_REF_MAX) return false;
    for (uint32_t i = 0; i < n; i++) {
        char c = r[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '/' || c == '.'))
            return false;
    }
    return true;
}

static bool ends_with(const char *p, const char *tail)
{
    size_t n = strlen(p), k = strlen(tail);
    return n >= k && !strcmp(p + n - k, tail);
}

/* ---- civil dates (days since 1970) ---- */

int64_t silt_year(int64_t day)
{
    int64_t z = day + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t m = mp < 10 ? mp + 3 : mp - 9;
    return yoe + era * 400 + (m <= 2);
}

/* ---- zxvd ---- */

static bool no_such(const zxm_link_t *l, int rc)
{
    return rc == ZXM_E_REMOTE && l->err_code == SILT_E_FILES && strstr(l->err, "no such");
}

/* files.read of a whole small text file; its length, ZXM_E_STATE when it
 * does not exist, ZXM_E_TOOBIG when it does not fit. */
static int32_t read_text(silt_t *s, const char *path, char *text, uint32_t cap)
{
    zxm_link_t *l = s->link;
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
static int32_t read_bytes(silt_t *s, const char *path, uint64_t off, uint32_t max, uint8_t *out)
{
    static char b64[SILT_TAIL * 2u];
    zxm_link_t *l = s->link;
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

/* SM2: a PDF is broadsheet.well_formed: "%PDF-" first, and the offset its last
 * startxref gives inside the file (header 1, off, room = size - off). */
static int pdf_well_formed(silt_t *s, const char *path, int64_t size, bool *well)
{
    static uint8_t buf[SILT_TAIL];
    int64_t header = 0, off = -1;
    *well = false;
    if (size < 5 || size > BROADSHEET_FILE_MAX) return ZXM_OK;
    int32_t got = read_bytes(s, path, 0, 5u, buf);
    if (got < 0) return got;
    header = got == 5 && !memcmp(buf, "%PDF-", 5);
    uint64_t from = size > (int64_t) SILT_TAIL ? (uint64_t) size - SILT_TAIL : 0u;
    got = read_bytes(s, path, from, SILT_TAIL, buf);
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

/* files.write of text whole, in pieces cut on character boundaries. */
static int write_text(silt_t *s, const char *path, const char *text, uint32_t len)
{
    zxm_link_t *l = s->link;
    zxm_begin(l, "files.mkdir");
    zxm_p_str(l, "path", SILT_DIR);
    int rc = zxm_call(l);
    if (rc &&
        !(rc == ZXM_E_REMOTE && l->err_code == SILT_E_FILES && strstr(l->err, "already exists")))
        return rc;
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t n = len - at < SILT_PIECE ? len - at : SILT_PIECE;
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
static int journal_newest(silt_t *s, int64_t *seq)
{
    zxm_link_t *l = s->link;
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

/* ---- SM1: the register ---- */

static bool row_ok(const silt_row_t *r)
{
    return silt_case_node_ok(r->series, r->caseno, r->doc, r->regno, r->dated, r->hold) &&
           cartulary_entry_ok(r->series, r->regno % 1000000, r->size, r->received) &&
           silt_register_number(r->regno / 1000000, r->regno % 1000000) == r->regno &&
           r->series >= 1 && r->caseno >= 1 && r->doc >= 1 && r->dated >= 0 &&
           r->received >= r->dated && r->received <= SILT_DAY_MAX && r->size >= 0 && r->seq >= 1 &&
           r->prev >= 0 && r->jseq >= 0 && r->jseq <= WITNESS_SEQ_MAX && r->hold >= 0 &&
           r->hold <= 1 && (r->kind == SILT_FILED || r->kind == SILT_DISPOSED) && ref_ok(r->ref) &&
           path_ok(r->path);
}

static int read_register(silt_t *s)
{
    static char text[SILT_TEXT + 1u];
    int32_t len = read_text(s, SILT_REGISTER, text, sizeof text);
    if (len == ZXM_E_STATE) return ZXM_OK;
    if (len < 0) return len;
    for (uint32_t at = 0; at < (uint32_t) len;) {
        if (s->n >= SILT_ROWS) return ZXM_E_PROTOCOL;
        silt_row_t *r = &s->row[s->n];
        int64_t *f[SILT_FIELDS] = {&r->regno, &r->series,   &r->caseno, &r->doc,
                                   &r->dated, &r->received, &r->size,   &r->seq,
                                   &r->prev,  &r->jseq,     &r->hold,   &r->kind};
        for (uint32_t k = 0; k < SILT_FIELDS; k++)
            if (!get_i64(text, (uint32_t) len, &at, '\t', f[k])) return ZXM_E_PROTOCOL;
        if (!get_text(text, (uint32_t) len, &at, '\t', r->ref, sizeof r->ref) ||
            !get_text(text, (uint32_t) len, &at, '\n', r->path, sizeof r->path))
            return ZXM_E_PROTOCOL;
        if (!row_ok(r) || silt_find(s, r->regno) >= 0) return ZXM_E_PROTOCOL;
        s->n++;
    }
    return ZXM_OK;
}

static int write_register(silt_t *s)
{
    static char text[SILT_TEXT + 1u];
    uint32_t at = 0;
    text[0] = 0;
    for (uint32_t i = 0; i < s->n; i++) {
        const silt_row_t *r = &s->row[i];
        const int64_t f[SILT_FIELDS] = {r->regno, r->series,   r->caseno, r->doc,
                                        r->dated, r->received, r->size,   r->seq,
                                        r->prev,  r->jseq,     r->hold,   r->kind};
        for (uint32_t k = 0; k < SILT_FIELDS; k++)
            if (!put_i64(text, sizeof text, &at, f[k]) || !put(text, sizeof text, &at, "\t"))
                return ZXM_E_TOOBIG;
        if (!put(text, sizeof text, &at, r->ref) || !put(text, sizeof text, &at, "\t") ||
            !put(text, sizeof text, &at, r->path) || !put(text, sizeof text, &at, "\n"))
            return ZXM_E_TOOBIG;
    }
    return write_text(s, SILT_REGISTER, text, at);
}

void silt_init(silt_t *s, zxm_link_t *link)
{
    memset(s, 0, sizeof *s);
    s->link = link;
}

int silt_set_dest(silt_t *s, const char *dest)
{
    uint32_t n = len_of(dest, SILT_DEST_MAX + 1u);
    if (!dest || !n || n > SILT_DEST_MAX || dest[0] != '/') return ZXM_E_ARG;
    memcpy(s->dest, dest, n + 1u);
    return ZXM_OK;
}

int silt_load(silt_t *s)
{
    s->n = 0;
    s->loaded = false;
    int rc = read_register(s);
    if (rc) {
        s->n = 0;
        say(s, "the register could not be read: ", s->link->err);
        return rc;
    }
    s->loaded = true;
    return ZXM_OK;
}

int32_t silt_find(const silt_t *s, int64_t regno)
{
    for (uint32_t i = 0; i < s->n; i++)
        if (s->row[i].regno == regno) return (int32_t) i;
    return -1;
}

static bool path_filed(const silt_t *s, const char *path)
{
    for (uint32_t i = 0; i < s->n; i++)
        if (!strcmp(s->row[i].path, path)) return true;
    return false;
}

/* ---- SM2: receiving ---- */

int silt_receive(silt_t *s, const char *path, int64_t series, int64_t caseno, const char *ref,
                 int64_t dated, int64_t today, bool owner_confirmed, int64_t *regno)
{
    if (!s->loaded) return ZXM_E_STATE;
    if (!regno) return ZXM_E_ARG;
    int64_t missing = !len_of(path, 1u) + !len_of(ref, 1u);
    int64_t outside = (len_of(path, 1u) && !path_ok(path)) + (len_of(ref, 1u) && !ref_ok(ref)) +
                      (series < 1 || series > 9999) + (caseno < 1 || caseno > 999999) +
                      (dated < 0 || dated > today) + (today < 0 || today > SILT_DAY_MAX);
    if (!sealwax_sign_ready(SILT_RECEIPT, missing, outside) || path_filed(s, path))
        return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    if (s->n >= SILT_ROWS) return ZXM_E_TOOBIG;

    /* SR1: the year's next seq (tallystick.next_doc_number), the case's next doc */
    int64_t year = silt_year(today), last = 0, doc = 1;
    for (uint32_t i = 0; i < s->n; i++) {
        const silt_row_t *r = &s->row[i];
        if (r->regno / 1000000 == year && r->regno % 1000000 > last) last = r->regno % 1000000;
        if (r->series == series && r->caseno == caseno && r->doc >= doc) doc = r->doc + 1;
    }
    int64_t seq = silt_next_seq(last), no = silt_register_number(year, seq);
    if (no < 0) {
        say(s, "this year's register is full (999999 records); nothing was received.", NULL);
        return ZXM_E_STATE;
    }

    zxm_link_t *l = s->link;
    int64_t size = 0, mtime = 0, jseq = 0;
    bool dir = false;
    zxm_begin(l, "files.stat");
    zxm_p_str(l, "path", path);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "dir", &dir) || zxm_get_i64(l, l->result, "size", &size) ||
        zxm_get_i64(l, l->result, "mtime", &mtime) || size < 0)
        return ZXM_E_PROTOCOL;
    if (dir) return ZXM_E_ARG;
    if (ends_with(path, ".pdf")) {
        bool well = false;
        rc = pdf_well_formed(s, path, size, &well);
        if (rc) return rc;
        if (!well) {
            say(s, "not a well-formed PDF, so not received: ", path);
            return ZXM_E_ARG;
        }
    }
    rc = journal_newest(s, &jseq);
    if (rc) return rc;

    silt_row_t *r = &s->row[s->n];
    memset(r, 0, sizeof *r);
    r->regno = no;
    r->series = series;
    r->caseno = caseno;
    r->doc = doc;
    r->dated = dated;
    r->received = today;
    r->size = size;
    r->prev = s->n ? s->row[s->n - 1u].seq : 0;
    r->seq = r->prev + 1;
    r->jseq = jseq;
    r->kind = SILT_FILED;
    memcpy(r->ref, ref, len_of(ref, SILT_REF_MAX) + 1u);
    memcpy(r->path, path, len_of(path, SILT_PATH_MAX) + 1u);
    if (!row_ok(r)) return ZXM_E_ARG;
    s->n++;
    rc = write_register(s);
    if (rc) {
        s->n--;
        return rc;
    }
    *regno = no;
    say(s,
        silt_reference(s, ref) == LPRES_STATE_BOTH
            ? "Received and stamped; another record has this reference with a different date: "
              "both are filed and flagged, never merged: "
            : "Received and stamped: ",
        ref);
    return ZXM_OK;
}

/* ---- SM3: one reference ---- */

lpres_state_t silt_reference(const silt_t *s, const char *ref)
{
    int64_t dated[SILT_ROWS];
    uint32_t n = 0;
    if (!ref) return LPRES_STATE_NEITHER;
    for (uint32_t i = 0; i < s->n; i++)
        if (!strcmp(s->row[i].ref, ref)) dated[n++] = s->row[i].dated;
    return silt_reference_state(dated, n);
}

/* ---- SM4, SM5: retention and disposal ---- */

static bool schedule_ok(int64_t years, int64_t months)
{
    return cartulary_retention_ok(cartulary_span(years, months), 0);
}

int silt_days_left(const silt_t *s, uint32_t i, int64_t years, int64_t months, int64_t today,
                   int64_t *left)
{
    if (i >= s->n || !left || !schedule_ok(years, months) ||
        !cartulary_left(s->row[i].dated, years, months, today, left))
        return ZXM_E_ARG;
    return ZXM_OK;
}

lpres_state_t silt_disposable(const silt_t *s, uint32_t i, int64_t years, int64_t months,
                              int64_t today, int64_t approved)
{
    int64_t left = 0;
    if (silt_days_left(s, i, years, months, today, &left)) return LPRES_STATE_NEITHER;
    const silt_row_t *r = &s->row[i];
    if (r->kind != SILT_FILED) return LPRES_STATE_FALSE;
    lpres_state_t rule =
        silt_may_dispose(left, r->hold, approved) ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    return lpres_conjoin(silt_reference(s, r->ref), rule);
}

int silt_dispose(silt_t *s, int64_t regno, int64_t years, int64_t months, int64_t today,
                 int64_t approved, bool owner_confirmed)
{
    if (!s->loaded) return ZXM_E_STATE;
    int32_t i = silt_find(s, regno);
    int64_t left = 0;
    if (i < 0 || approved < 0 || approved > 1 ||
        silt_days_left(s, (uint32_t) i, years, months, today, &left))
        return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    lpres_state_t st = silt_disposable(s, (uint32_t) i, years, months, today, approved);
    if (st != LPRES_STATE_TRUE) {
        say(s,
            st == LPRES_STATE_BOTH
                ? "Not disposed of: two records under this reference disagree; correct one first."
                : "Not disposed of: its period is not over, it is on hold, or it is not approved.",
            NULL);
        return ZXM_E_STATE;
    }
    silt_row_t *r = &s->row[i];
    zxm_link_t *l = s->link;
    zxm_begin(l, "files.trash");
    zxm_p_str(l, "path", r->path);
    int rc = zxm_call(l);
    if (rc) return rc;
    r->kind = SILT_DISPOSED;
    rc = write_register(s);
    if (rc) {
        r->kind = SILT_FILED;
        return rc;
    }
    say(s, "Moved to the Trash, kept there until the Trash is emptied: ", r->path);
    return ZXM_OK;
}

int silt_hold(silt_t *s, int64_t regno, int64_t on, bool owner_confirmed)
{
    if (!s->loaded) return ZXM_E_STATE;
    int32_t i = silt_find(s, regno);
    if (i < 0 || on < 0 || on > 1) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    silt_row_t *r = &s->row[i];
    int64_t was = r->hold;
    r->hold = on;
    int rc = write_register(s);
    if (rc) r->hold = was;
    return rc;
}

/* ---- SM6: the trail ---- */

lpres_state_t silt_trail(silt_t *s)
{
    if (!s->n) {
        say(s, "The register is empty: nothing to prove.", NULL);
        return LPRES_STATE_NEITHER;
    }
    bool chain = true;
    for (uint32_t i = 0; i < s->n; i++)
        chain = chain && witness_linked(s->row[i].seq, s->row[i].prev, i ? s->row[i - 1u].seq : 0);
    lpres_state_t own = chain ? LPRES_STATE_TRUE : LPRES_STATE_FALSE, jr = LPRES_STATE_NEITHER;

    zxm_link_t *l = s->link;
    bool on = false, ok = false;
    int64_t first = 0, last = 0;
    zxm_begin(l, "journal.verify");
    if (zxm_call(l) == ZXM_OK && !zxm_get_bool(l, l->result, "on", &on) && on &&
        !zxm_get_bool(l, l->result, "ok", &ok) && !zxm_get_i64(l, l->result, "first_seq", &first) &&
        !zxm_get_i64(l, l->result, "last_seq", &last)) {
        bool inside = ok;
        for (uint32_t i = 0; i < s->n; i++)
            if (s->row[i].jseq && (s->row[i].jseq < first || s->row[i].jseq > last)) inside = false;
        jr = inside ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    }
    lpres_state_t st = lpres_kjoin(own, jr);
    say(s,
        st == LPRES_STATE_TRUE    ? "The trail holds."
        : st == LPRES_STATE_BOTH  ? "The register and the journal disagree: held for a person."
        : st == LPRES_STATE_FALSE ? "The trail is broken."
                                  : "Nothing to prove.",
        NULL);
    return st;
}

/* ---- SM7: measures ---- */

int silt_inbox(silt_t *s, const char *folder, uint32_t *waiting)
{
    char name[SILT_PATH_MAX + 1], full[2u * SILT_PATH_MAX + 2u];
    if (!s->loaded) return ZXM_E_STATE;
    if (!waiting || !path_ok(folder)) return ZXM_E_ARG;
    zxm_link_t *l = s->link;
    zxm_begin(l, "files.list");
    zxm_p_str(l, "path", folder);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "entries");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t w = 0, n = zxm_len(l, arr);
    for (uint32_t k = 0; k < n; k++) {
        int32_t e = zxm_at(l, arr, k);
        bool dir = false;
        if (e < 0 || zxm_get_bool(l, e, "dir", &dir) ||
            zxm_get_str(l, e, "name", name, sizeof name))
            return ZXM_E_PROTOCOL;
        uint32_t at = 0;
        if (dir || !put(full, sizeof full, &at, folder) || !put(full, sizeof full, &at, "/") ||
            !put(full, sizeof full, &at, name))
            continue;
        w += !path_filed(s, full);
    }
    *waiting = w;
    return ZXM_OK;
}

int silt_versions(silt_t *s, int64_t regno, uint32_t *count)
{
    int32_t i = silt_find(s, regno);
    if (!s->dest[0]) return ZXM_E_STATE;
    if (i < 0 || !count) return ZXM_E_ARG;
    zxm_link_t *l = s->link;
    zxm_begin(l, "backup.versions");
    zxm_p_str(l, "dest", s->dest);
    zxm_p_str(l, "path", s->row[i].path);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "versions");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    *count = zxm_len(l, arr);
    return ZXM_OK;
}

int silt_series_bytes(const silt_t *s, int64_t series, int64_t *bytes)
{
    int64_t size[SILT_ROWS];
    uint32_t n = 0;
    if (!bytes || series < 0 || series > 9999) return ZXM_E_ARG;
    for (uint32_t i = 0; i < s->n; i++)
        if (!series || s->row[i].series == series) size[n++] = s->row[i].size;
    return cartulary_total(size, n, bytes) ? ZXM_OK : ZXM_E_TOOBIG;
}

int64_t silt_series_share(const silt_t *s, int64_t series)
{
    int64_t mine = 0, all = 0;
    if (series < 1 || silt_series_bytes(s, series, &mine) || silt_series_bytes(s, 0, &all))
        return -1;
    return silt_share(mine, all);
}

int64_t silt_count(const silt_t *s, int64_t kind)
{
    int64_t k[SILT_ROWS];
    for (uint32_t i = 0; i < s->n; i++) k[i] = s->row[i].kind;
    return witness_kind_count(k, s->n, kind);
}

int64_t silt_grid(const silt_t *s, uint32_t i)
{
    int64_t series[SILT_ROWS], year[SILT_ROWS];
    for (uint32_t k = 0; k < s->n; k++) {
        series[k] = s->row[k].series;
        year[k] = silt_year(s->row[k].dated);
    }
    return cartulary_cell(series, year, s->n, i);
}

int silt_backlog(silt_t *s, const int64_t *arrived, const int64_t *cleared, uint32_t n,
                 int64_t backlog, rat_t *days)
{
    if (!silt_backlog_days(arrived, cleared, n, backlog, days)) {
        say(s, "The backlog does not fall at this pace, or the counts are out of range.", NULL);
        return ZXM_E_ARG;
    }
    char t[48];
    if (!rat_to_string(*days, t, sizeof t)) return ZXM_E_TOOBIG;
    say(s, "Days to clear the backlog at this pace: ", t);
    return ZXM_OK;
}

/* ---- SM8: a signed copy ---- */

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

int silt_check_signed(silt_t *s, const char *sigpath)
{
    static char hex[SILT_SIG_MAX + 2u];
    if (!path_ok(sigpath)) return ZXM_E_ARG;
    int32_t n = read_text(s, sigpath, hex, sizeof hex);
    if (n == ZXM_E_STATE || n == ZXM_E_TOOBIG) return ZXM_E_ARG;
    if (n < 0) return n;
    uint32_t len = (uint32_t) n;
    while (len && (hex[len - 1u] == '\n' || hex[len - 1u] == ' ')) len--;
    if (len > SILT_SIG_MAX || !hex_ok(hex, len)) return ZXM_E_ARG;
    zxm_link_t *l = s->link;
    zxm_begin(l, "web4.verify");
    zxm_p_strn(l, "envelope", hex, len);
    int rc = zxm_call(l);
    if (rc) return rc;
    bool valid = false;
    uint64_t pb = 0;
    char why[64] = "";
    if (zxm_get_bool(l, l->result, "valid", &valid)) return ZXM_E_PROTOCOL;
    if (valid ? zxm_get_u64(l, l->result, "payload_bytes", &pb) || pb > INT32_MAX
              : zxm_get_str(l, l->result, "reason", why, sizeof why))
        return ZXM_E_PROTOCOL;
    s->sig_checked = true;
    s->sig_valid = valid;
    s->sig_bytes = valid ? (int64_t) pb : 0;
    uint32_t at = 0;
    s->notice[0] = 0;
    put(s->notice, sizeof s->notice, &at,
        valid ? "The signed copy holds: " : "The signed copy does not hold: ");
    if (valid) {
        put_i64(s->notice, sizeof s->notice, &at, s->sig_bytes);
        put(s->notice, sizeof s->notice, &at, " bytes signed.");
    } else {
        put(s->notice, sizeof s->notice, &at, why);
        put(s->notice, sizeof s->notice, &at, ".");
    }
    put(s->notice, sizeof s->notice, &at, ADVICE);
    return ZXM_OK;
}
