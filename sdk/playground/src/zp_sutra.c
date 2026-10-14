/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zp_sutra.c - Sutra steps (sdk/pillars/cobol-sutra): COBOL copybooks and
 * Sutra record layouts, read exactly.
 *
 * The step's file is a record layout. It is read as Sutra when a line
 * starts with the word `record` in column 1 (Sutra's header), else as a
 * fixed-format COBOL copybook. A refused layout is reported at its line
 * and column. Outputs: `size` (bytes in one record) and `digest` (the
 * SHA3-256 of the canonical layout: a copybook and a Sutra file declare
 * the same record exactly when their digests match).
 *
 * Then a record is built: every input whose name is a field reference
 * ("pay-amount", "st-line-amount(2)") is put into a fresh, initialized
 * record with zs_put_value. A value that does not fit the field's PICTURE
 * is refused, naming the field and pointing at its declaration: never
 * truncated, never rounded. Fields with no input stay as initialized
 * (spaces, zeros) and a note says which. Every elementary item is read
 * back and output as `name = value` (exact decimal text, or the field's
 * bytes for text, padding included).
 *
 * Step options:
 *   form=sutra|copybook|free   how to read the step's file (default:
 *                  detected; free = free-format copybook)
 *   same=FILE      another template file, in either spelling, must declare
 *                  the same record (same digest), else the step is refused
 *                  at the first line where the two differ
 *   hex            output `hex`: the record's bytes
 *   export=FIELD:ALIAS,...   also output a numeric field under a short
 *                  name that a KRON step can bind (KRON names cannot hold
 *                  '-'); a field with OCCURS gives the vector of all its
 *                  occurrences ("12.50 -3.25 0.00")
 *   posting=AMOUNT,CCY,FROM,TO,REF   field names: read the payment with
 *                  zs_posting_read and output `posting` as
 *                  "FROM->TO AMOUNT CCY REF". It is NOT posted: the
 *                  Playground never touches anyone's books.
 *   compare=FILE   FILE holds NAME = VALUE lines: the same record as
 *                  another system (a counterparty's COBOL core) reports
 *                  it. Each field is put into a record of our layout and
 *                  compared byte for byte with ours. Equal: fine. Different
 *                  (or theirs does not fit our PICTURE): BOTH, held for a
 *                  person; nothing is resolved silently. A field reported
 *                  by one side only: NEITHER, pending.
 */
#include "zp_internal.h"

#include <zsutra.h>
#include <zxv_api.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ITEMS 4096u
#define MAX_HEX   4096u /* record bytes shown by `hex` */
#define REF_CAP   112u

typedef struct {
    char ref[REF_CAP]; /* lower case, e.g. "st-line-amount(2)" */
    uint32_t index;
    uint64_t off, size;
} item_t;

typedef struct {
    uint32_t index;
    uint64_t off;
    char ref[REF_CAP];
} key_t_;

static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int name_char(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
}

static void lower_copy(char *dst, size_t cap, const char *s) {
    size_t i = 0;
    for (; s[i] && i + 1u < cap; i++) dst[i] = (char) lc((unsigned char) s[i]);
    dst[i] = '\0';
}

/* Sutra when a line starts with "record" in column 1. */
static int looks_sutra(const char *t, size_t n) {
    size_t i = 0;
    while (i < n) {
        if (n - i >= 6u) {
            int ok = 1;
            for (size_t k = 0; k < 6u; k++)
                if (lc((unsigned char) t[i + k]) != "record"[k]) ok = 0;
            if (ok && (i + 6u == n || t[i + 6u] == ' ' || t[i + 6u] == '\t' || t[i + 6u] == '\n' ||
                       t[i + 6u] == '\r'))
                return 1;
        }
        while (i < n && t[i] != '\n') i++;
        i++;
    }
    return 0;
}

/* Where a field is declared: the first whole-word, case-insensitive use of
 * its name outside a comment. Line 0 if not found. */
static void find_decl(const char *t, size_t n, const char *name, uint32_t *line, uint32_t *col) {
    size_t nl = strlen(name), i = 0;
    *line = 0;
    *col = 0;
    while (i < n) {
        size_t e = i, a = i, end;
        while (e < n && t[e] != '\n') e++;
        while (a < e && (t[a] == ' ' || t[a] == '\t')) a++;
        end = e;
        if (a < e && (t[a] == '#' || t[a] == '*')) end = a; /* a comment line */
        for (size_t p = i; p < end; p++) {
            if (t[p] == '#' || (t[p] == '*' && p + 1u < end && t[p + 1u] == '>')) break;
            if (p + nl <= end && (p == i || !name_char((unsigned char) t[p - 1u])) &&
                (p + nl == end || !name_char((unsigned char) t[p + nl]))) {
                size_t k = 0;
                while (k < nl && lc((unsigned char) t[p + k]) == lc((unsigned char) name[k])) k++;
                if (k == nl) {
                    zp_line_col(t, n, p, line, col);
                    return;
                }
            }
        }
        i = e + 1u;
    }
}

/* The diag message without its "line L, column C: " prefix. */
static const char *diag_msg(const zs_diag *d) {
    const char *m = strstr(d->msg, ": ");
    return m && strncmp(d->msg, "line ", 5) == 0 ? m + 2 : d->msg;
}

static int32_t parse_layout(const char *t, size_t n, const char *form, zs_layout **out, zs_diag *d,
                            int *is_sutra) {
    memset(d, 0, sizeof *d);
    if (form && strcmp(form, "sutra") == 0) *is_sutra = 1;
    else if (form && (strcmp(form, "copybook") == 0 || strcmp(form, "free") == 0)) *is_sutra = 0;
    else *is_sutra = looks_sutra(t, n);
    if (*is_sutra) return zs_sutra_parse(t, n, out, d);
    return zs_copybook_parse(t, n, form && strcmp(form, "free") == 0 ? ZS_CB_FREE : ZS_CB_FIXED, out, d);
}

static void picture(const zs_field *f, char *buf, size_t cap) {
    static const char *usage[] = {"", " comp", " comp-3"};
    if (f->kind == ZS_K_TEXT) {
        snprintf(buf, cap, "x(%u)", (unsigned) f->digits);
    } else if (f->kind == ZS_K_NUM) {
        char ip[16] = "", dp[16] = "";
        if (f->digits > f->scale) snprintf(ip, sizeof ip, "9(%u)", (unsigned) (f->digits - f->scale));
        if (f->scale) snprintf(dp, sizeof dp, "v9(%u)", (unsigned) f->scale);
        snprintf(buf, cap, "%s%s%s%s", f->sign != ZS_SIGN_NONE ? "s" : "", ip, dp,
                 f->usage >= 0 && f->usage <= 2 ? usage[f->usage] : "");
    } else {
        snprintf(buf, cap, "a group");
    }
}

static void last_error(char *buf, size_t cap) {
    size_t n = 0;
    buf[0] = '\0';
    if (zs_last_error(buf, cap, &n) != ZXV_OK) buf[0] = '\0';
}

/* Every elementary item, all occurrences, in declaration order. */
static int32_t list_items(const zs_layout *l, item_t **out, uint32_t *n_out) {
    uint32_t nf = 0, n = 0;
    item_t *it = (item_t *) calloc(MAX_ITEMS, sizeof *it);
    if (!it) return ZXV_E_NOMEM;
    zs_field_count(l, &nf);
    for (uint32_t i = 1; i < nf; i++) {
        zs_field f;
        uint32_t occ[ZS_MAX_DIMS + 1], sub[ZS_MAX_DIMS + 1], nd = 0;
        zs_field_at(l, i, &f);
        if (f.kind == ZS_K_GROUP || strcmp(f.name, "FILLER") == 0) continue;
        for (uint32_t k = i;;) {
            zs_field a;
            zs_field_at(l, k, &a);
            if (a.occurs && nd <= ZS_MAX_DIMS) occ[nd++] = a.occurs;
            if (k == 0) break;
            k = a.parent;
        }
        for (uint32_t k = 0; k < nd / 2u; k++) {
            uint32_t t = occ[k];
            occ[k] = occ[nd - 1u - k];
            occ[nd - 1u - k] = t;
        }
        for (uint32_t k = 0; k < nd; k++) sub[k] = 1;
        for (;;) {
            char ref[REF_CAP];
            size_t rn;
            uint32_t d;
            if (n >= MAX_ITEMS) {
                free(it);
                return ZP_E_LIMIT;
            }
            lower_copy(ref, sizeof ref, f.name);
            rn = strlen(ref);
            for (d = 0; d < nd && rn < sizeof ref; d++)
                rn += (size_t) snprintf(ref + rn, sizeof ref - rn, "%s%u", d ? ", " : "(", (unsigned) sub[d]);
            if (nd && rn + 1u < sizeof ref) {
                ref[rn++] = ')';
                ref[rn] = '\0';
            }
            memcpy(it[n].ref, ref, sizeof ref);
            it[n].index = i;
            it[n].size = f.size;
            if (zs_find(l, ref, NULL, &it[n].off) != ZXV_OK) {
                free(it);
                return ZXV_E_INTERNAL;
            }
            n++;
            for (d = nd; d-- > 0;) {
                if (++sub[d] <= occ[d]) break;
                sub[d] = 1;
            }
            if (d == UINT32_MAX) break;
        }
    }
    *out = it;
    *n_out = n;
    return ZXV_OK;
}

/* zs_get_value as a NUL-terminated malloc'd string. */
static char *get_text(const zs_layout *l, const uint8_t *rec, size_t rl, const char *ref, uint64_t size,
                      int32_t *st) {
    size_t cap = (size_t) size + 64u, n = 0;
    char *b = (char *) malloc(cap);
    if (!b) {
        *st = ZXV_E_NOMEM;
        return NULL;
    }
    *st = zs_get_value(l, rec, rl, ref, b, cap, &n);
    if (*st != ZXV_OK) {
        free(b);
        return NULL;
    }
    b[n < cap ? n : cap - 1u] = '\0';
    return b;
}

static void rtrim(char *s) {
    size_t n = strlen(s);
    while (n && s[n - 1u] == ' ') s[--n] = '\0';
}

/* Same record? Refuse at the first canonical line that differs. */
static int check_same(zp_step *s, const zs_layout *ours, const char *ourname, const char *src, size_t len) {
    const char *fname = zp_step_opt(s, "same");
    const zp_file *f = zp_step_file(s, fname);
    zs_layout *th = NULL;
    zs_diag d;
    int is_sutra = 0;
    int32_t same = 0, st;
    char why[512];
    if (!f) {
        snprintf(why, sizeof why, "same=%s: the template has no file %s", fname, fname);
        zp_refuse(s, ZP_E_FILE, NULL, 0, 0, why);
        return 0;
    }
    st = parse_layout(f->text, f->len, NULL, &th, &d, &is_sutra);
    if (st != ZXV_OK) {
        snprintf(why, sizeof why, "%s line %u, column %u: %s", fname, (unsigned) d.line, (unsigned) d.col,
                 diag_msg(&d));
        zp_refuse(s, st, zs_status_name(st), d.line, d.col, why);
        return 0;
    }
    zs_layout_same(ours, th, &same);
    if (!same) {
        size_t na = 0, nb = 0;
        char *a, *b;
        zs_layout_sutra(ours, NULL, 0, &na);
        zs_layout_sutra(th, NULL, 0, &nb);
        a = (char *) malloc(na + 1u);
        b = (char *) malloc(nb + 1u);
        if (!a || !b) {
            zp_refuse(s, ZXV_E_NOMEM, NULL, 0, 0, "out of memory");
        } else {
            size_t i = 0, ls = 0, ea, eb;
            uint32_t line = 0, col = 0;
            char word[ZS_MAX_NAME + 1];
            size_t w = 0;
            zs_layout_sutra(ours, a, na + 1u, &na);
            zs_layout_sutra(th, b, nb + 1u, &nb);
            a[na] = b[nb] = '\0';
            while (i < na && i < nb && a[i] == b[i]) {
                if (a[i] == '\n') ls = i + 1u;
                i++;
            }
            ea = ls;
            while (ea < na && a[ea] != '\n') ea++;
            eb = ls;
            while (eb < nb && b[eb] != '\n') eb++;
            /* the field named on that line, to point at it in our file */
            {
                size_t p = ls;
                while (p < na && a[p] == ' ') p++;
                while (p < na && name_char((unsigned char) a[p]) && w < ZS_MAX_NAME) word[w++] = a[p++];
                word[w] = '\0';
                if (w) find_decl(src, len, word, &line, &col);
            }
            {
                size_t la = ls, lb = ls;
                while (la < ea && a[la] == ' ') la++;
                while (lb < eb && b[lb] == ' ') lb++;
                snprintf(why, sizeof why,
                         "%s and %s declare different records: %s has '%.*s', %s has '%.*s'. Both must describe "
                         "the same bytes",
                         ourname, fname, ourname, (int) (ea - la > 120u ? 120u : ea - la), a + la, fname,
                         (int) (eb - lb > 120u ? 120u : eb - lb), b + lb);
            }
            zp_refuse(s, ZS_E_UNSUPPORTED, "LAYOUT_DIFFERS", line, col, why);
        }
        free(a);
        free(b);
        zs_layout_destroy(th);
        return 0;
    }
    zp_notef(s, "%s (%s) declares exactly the same record: same digest", fname, is_sutra ? "Sutra" : "copybook");
    zs_layout_destroy(th);
    return 1;
}

/* Index of the non-filler field called `name` (any case), or 0. */
static uint32_t field_named(const zs_layout *l, const char *name) {
    uint32_t nf = 0;
    char want[REF_CAP];
    lower_copy(want, sizeof want, name);
    zs_field_count(l, &nf);
    for (uint32_t i = 1; i < nf; i++) {
        zs_field f;
        char low[ZS_MAX_NAME + 1];
        zs_field_at(l, i, &f);
        lower_copy(low, sizeof low, f.name);
        if (strcmp(low, want) == 0 && strcmp(f.name, "FILLER") != 0) return i;
    }
    return 0;
}

static int32_t key_find(const key_t_ *k, uint32_t n, uint32_t index, uint64_t off) {
    for (uint32_t i = 0; i < n; i++)
        if (k[i].index == index && k[i].off == off) return (int32_t) i;
    return -1;
}

/* Their NAME = VALUE lines against our record. */
static void compare(zp_step *s, const zs_layout *l, const uint8_t *ours, size_t rl, const key_t_ *ok,
                    uint32_t nok) {
    const char *fname = zp_step_opt(s, "compare");
    const zp_file *f = zp_step_file(s, fname);
    uint8_t *th;
    key_t_ *tk;
    uint32_t ntk = 0, agree = 0, differ = 0, only = 0;
    size_t i = 0;
    if (!f) {
        char why[160];
        snprintf(why, sizeof why, "compare=%s: the template has no file %s", fname, fname);
        zp_refuse(s, ZP_E_FILE, NULL, 0, 0, why);
        return;
    }
    th = (uint8_t *) malloc(rl ? rl : 1u);
    tk = (key_t_ *) calloc(MAX_ITEMS, sizeof *tk);
    if (!th || !tk) {
        zp_refuse(s, ZXV_E_NOMEM, NULL, 0, 0, "out of memory");
        goto done;
    }
    zs_record_init(l, th, rl);
    while (i < f->len) {
        size_t e = i, a = i, b, eq;
        while (e < f->len && f->text[e] != '\n') e++;
        while (a < e && (f->text[a] == ' ' || f->text[a] == '\t')) a++;
        b = e;
        while (b > a && (f->text[b - 1u] == ' ' || f->text[b - 1u] == '\t' || f->text[b - 1u] == '\r')) b--;
        if (a < b && f->text[a] != '#') {
            char name[REF_CAP], why[400];
            size_t ne, vs;
            uint32_t idx = 0;
            uint64_t off = 0;
            int32_t st;
            eq = a;
            while (eq < b && f->text[eq] != '=') eq++;
            if (eq == b) {
                uint32_t line, col;
                zp_line_col(f->text, f->len, a, &line, &col);
                snprintf(why, sizeof why, "%s line %u: expected NAME = VALUE", fname, (unsigned) line);
                zp_refuse(s, ZXV_E_FORMAT, NULL, line, col, why);
                goto done;
            }
            ne = eq;
            while (ne > a && (f->text[ne - 1u] == ' ' || f->text[ne - 1u] == '\t')) ne--;
            vs = eq + 1u;
            while (vs < b && (f->text[vs] == ' ' || f->text[vs] == '\t')) vs++;
            if (ne - a >= sizeof name) ne = a + sizeof name - 1u;
            memcpy(name, f->text + a, ne - a);
            name[ne - a] = '\0';
            st = zs_find(l, name, &idx, &off);
            if (st != ZXV_OK) {
                snprintf(why, sizeof why, "theirs reports %s, which our layout does not have", name);
                zp_pending(s, why);
                only++;
            } else {
                /* a field given twice: the later line wins, as in inputs */
                int32_t j = key_find(tk, ntk, idx, off);
                int held_before = j >= 0 && !tk[j].ref[0];
                if (j < 0 && ntk < MAX_ITEMS) {
                    j = (int32_t) ntk++;
                    tk[j].index = idx;
                    tk[j].off = off;
                }
                if (j >= 0 && !held_before) {
                    st = zs_put_value(l, th, rl, name, f->text + vs, b - vs);
                    if (st != ZXV_OK) {
                        zs_field fd;
                        char pic[48], det[200];
                        zs_field_at(l, idx, &fd);
                        picture(&fd, pic, sizeof pic);
                        last_error(det, sizeof det);
                        snprintf(why, sizeof why,
                                 "theirs gives %s = %.*s, which does not fit our PIC %s (%s): held for a person",
                                 name, (int) (b - vs > 60u ? 60u : b - vs), f->text + vs, pic, det);
                        zp_hold(s, why);
                        differ++;
                        tk[j].ref[0] = '\0'; /* reported but did not fit: already held */
                    } else {
                        lower_copy(tk[j].ref, sizeof tk[j].ref, name);
                    }
                }
            }
        }
        i = e + 1u;
    }
    for (uint32_t k = 0; k < nok; k++) {
        int32_t j = key_find(tk, ntk, ok[k].index, ok[k].off);
        zs_field fd;
        char why[600];
        if (j < 0) {
            snprintf(why, sizeof why, "theirs does not report %s: cannot compare it", ok[k].ref);
            zp_pending(s, why);
            only++;
            continue;
        }
        if (!tk[j].ref[0]) continue; /* already held: did not fit */
        zs_field_at(l, ok[k].index, &fd);
        {
            int32_t st1, st2;
            char *a = get_text(l, ours, rl, ok[k].ref, fd.size, &st1);
            char *b = get_text(l, th, rl, ok[k].ref, fd.size, &st2);
            if (!a || !b) {
                zp_refuse(s, st1 != ZXV_OK ? st1 : st2, NULL, 0, 0, "a compared field could not be read back");
            } else if (strcmp(a, b) != 0) {
                rtrim(a);
                rtrim(b);
                snprintf(why, sizeof why,
                         "our record and theirs disagree on %s (ours %.80s, theirs %.80s): held for a person",
                         ok[k].ref, a, b);
                zp_hold(s, why);
                differ++;
            } else {
                agree++;
            }
            free(a);
            free(b);
        }
    }
    for (uint32_t j = 0; j < ntk; j++) {
        char why[200];
        if (!tk[j].ref[0] || key_find(ok, nok, tk[j].index, tk[j].off) >= 0) continue;
        snprintf(why, sizeof why, "theirs reports %s, which our inputs do not set: cannot compare it", tk[j].ref);
        zp_pending(s, why);
        only++;
    }
    zp_notef(s, "compared with %s: %u field(s) agree, %u disagree, %u reported by one side only", fname,
             (unsigned) agree, (unsigned) differ, (unsigned) only);
done:
    free(th);
    free(tk);
}

static void posting(zp_step *s, const zs_layout *l, const uint8_t *rec, size_t rl, const char *spec) {
    char part[5][ZP_MAX_NAME + 1];
    uint32_t np = 0;
    const char *p = spec;
    zs_posting_map map;
    zs_posting po;
    int32_t st;
    uint8_t minor = 0;
    char amt[320], out[480], why[640];
    while (np < 5) {
        const char *e = strchr(p, ',');
        size_t n = e ? (size_t) (e - p) : strlen(p);
        if (n > ZP_MAX_NAME) n = ZP_MAX_NAME;
        memcpy(part[np], p, n);
        part[np][n] = '\0';
        np++;
        if (!e) break;
        p = e + 1;
    }
    if (np != 5 || strchr(p, ',') || !part[4][0]) {
        zp_refuse(s, ZP_E_ARG, NULL, 0, 0, "posting= needs five field names: AMOUNT,CCY,FROM,TO,REF");
        return;
    }
    memset(&map, 0, sizeof map);
    map.amount = part[0];
    map.currency = part[1];
    map.from = part[2];
    map.to = part[3];
    map.reference = part[4];
    map.minor_units = -1;
    memset(&po, 0, sizeof po);
    st = zs_posting_read(l, &map, rec, rl, &po);
    if (st != ZXV_OK) {
        char det[256];
        last_error(det, sizeof det);
        snprintf(why, sizeof why, "the posting cannot be read from the record: %s", det[0] ? det : zs_status_name(st));
        zp_refuse(s, st, zs_status_name(st), 0, 0, why);
        return;
    }
    if (zxv_iso4217_by_alpha(po.currency, NULL, &minor, NULL) != ZXV_OK || minor > 18u) minor = 0;
    {
        uint64_t mag = po.amount_minor < 0 ? (uint64_t) 0 - (uint64_t) po.amount_minor : (uint64_t) po.amount_minor;
        uint64_t pw = 1;
        for (uint8_t k = 0; k < minor; k++) pw *= 10u;
        if (minor)
            snprintf(amt, sizeof amt, "%s%llu.%0*llu", po.amount_minor < 0 ? "-" : "", (unsigned long long) (mag / pw),
                     (int) minor, (unsigned long long) (mag % pw));
        else
            snprintf(amt, sizeof amt, "%s%llu", po.amount_minor < 0 ? "-" : "", (unsigned long long) mag);
    }
    if (po.amount_minor <= 0) {
        snprintf(why, sizeof why, "%s = %s: a posting amount must be above zero; a ledger would refuse it", part[0],
                 amt);
        zp_refuse(s, ZXV_E_RANGE, NULL, 0, 0, why);
        return;
    }
    snprintf(out, sizeof out, "%u->%u %s %s %s", (unsigned) po.from, (unsigned) po.to, amt, po.currency,
             po.reference);
    zp_output(s, "posting", out);
    zp_note(s, "posting: read only. It was NOT posted to any ledger; the Playground never touches your books.");
}

static void export_fields(zp_step *s, const zs_layout *l, const uint8_t *rec, size_t rl, const item_t *it,
                          uint32_t n, const char *spec) {
    const char *p = spec;
    while (*p) {
        const char *e = strchr(p, ',');
        size_t len = e ? (size_t) (e - p) : strlen(p);
        char pair[ZP_MAX_NAME + 1], *colon;
        uint32_t idx = 0;
        zs_field f;
        zp_buf v = {0};
        char why[200];
        if (len > ZP_MAX_NAME) len = ZP_MAX_NAME;
        memcpy(pair, p, len);
        pair[len] = '\0';
        colon = strchr(pair, ':');
        if (!colon || colon == pair || !colon[1]) {
            snprintf(why, sizeof why, "export=: '%s' should be FIELD:ALIAS", pair);
            zp_refuse(s, ZP_E_ARG, NULL, 0, 0, why);
            return;
        }
        *colon = '\0';
        idx = field_named(l, pair);
        if (!idx || (zs_field_at(l, idx, &f), f.kind != ZS_K_NUM)) {
            snprintf(why, sizeof why, "export=: %s is not a numeric field of this record", pair);
            zp_refuse(s, ZP_E_ARG, NULL, 0, 0, why);
            return;
        }
        for (uint32_t k = 0; k < n; k++) {
            int32_t st;
            char *t;
            if (it[k].index != idx) continue;
            t = get_text(l, rec, rl, it[k].ref, it[k].size, &st);
            if (!t) {
                zp_refuse(s, st, zs_status_name(st), 0, 0, "an exported field could not be read back");
                zp_buf_free(&v);
                return;
            }
            if (v.len) zp_buf_puts(&v, " ");
            zp_buf_puts(&v, t);
            free(t);
        }
        if (v.oom) {
            zp_refuse(s, ZXV_E_NOMEM, NULL, 0, 0, "out of memory");
        } else {
            zp_output(s, colon + 1, v.p ? v.p : "");
            zp_notef(s, "%s is %s, for the next step", colon + 1, pair);
        }
        zp_buf_free(&v);
        if (!e) break;
        p = e + 1;
    }
}

void zp_lang_sutra(zp_step *s, const char *src, size_t len) {
    zs_layout *l = NULL;
    zs_diag d;
    int is_sutra = 0;
    int32_t st;
    uint64_t size = 0;
    uint8_t dg[32];
    char hex[65], num[32], why[600];
    uint8_t *rec = NULL, *mask = NULL;
    item_t *it = NULL;
    key_t_ *keys = NULL;
    uint32_t nit = 0, nkeys = 0;
    zp_buf unused = {0}, unset = {0};
    uint32_t n_unused = 0, n_unset = 0;
    const char *opt;
    /* the inputs as the step found them, before its own outputs */
    const uint32_t n_in = zp_input_count(s);

    st = parse_layout(src, len, zp_step_opt(s, "form"), &l, &d, &is_sutra);
    if (st != ZXV_OK) {
        zp_refuse(s, st, zs_status_name(st), d.line, d.col, diag_msg(&d));
        return;
    }
    zp_notef(s, "read as %s", is_sutra ? "Sutra" : "a COBOL copybook");
    zs_layout_size(l, &size);
    zs_layout_digest(l, dg);
    for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", dg[i]);
    snprintf(num, sizeof num, "%llu", (unsigned long long) size);
    zp_output(s, "size", num);
    zp_output(s, "digest", hex);

    if (zp_step_opt(s, "same") && !check_same(s, l, is_sutra ? "the Sutra layout" : "the copybook", src, len))
        goto done;

    rec = (uint8_t *) malloc((size_t) size ? (size_t) size : 1u);
    mask = (uint8_t *) calloc((size_t) size ? (size_t) size : 1u, 1u);
    keys = (key_t_ *) calloc(MAX_ITEMS, sizeof *keys);
    if (!rec || !mask || !keys) {
        zp_refuse(s, ZXV_E_NOMEM, NULL, 0, 0, "out of memory");
        goto done;
    }
    zs_record_init(l, rec, (size_t) size);

    /* inputs that name fields go into the record */
    for (uint32_t i = 0; i < n_in; i++) {
        const char *name, *value;
        uint32_t idx = 0;
        uint64_t off = 0;
        zs_field f;
        zp_input_at(s, i, &name, &value);
        st = zs_find(l, name, &idx, &off);
        if (st == ZXV_E_NOT_FOUND) {
            if (n_unused++) zp_buf_puts(&unused, ", ");
            zp_buf_puts(&unused, name);
            continue;
        }
        if (st != ZXV_OK) {
            char base[REF_CAP];
            uint32_t line, col;
            size_t k = 0;
            while (name[k] && name[k] != '(' && name[k] != ' ' && k + 1u < sizeof base) {
                base[k] = name[k];
                k++;
            }
            base[k] = '\0';
            find_decl(src, len, base, &line, &col);
            if (!field_named(l, base)) { /* not about this record, e.g. NET.ACME */
                if (n_unused++) zp_buf_puts(&unused, ", ");
                zp_buf_puts(&unused, name);
                continue;
            }
            if (!strchr(name, '(')) {
                snprintf(why, sizeof why, "input %s: a table field needs its subscripts, e.g. %s(1)", name, base);
                zp_refuse(s, st, zs_status_name(st), line, col, why);
                goto done;
            }
            snprintf(why, sizeof why, "input %s: %s", name,
                     st == ZXV_E_RANGE ? "a subscript is outside the table, or the field is not a table"
                                       : "not a valid reference to this field (a table field needs its subscripts, "
                                         "e.g. name(1))");
            zp_refuse(s, st, zs_status_name(st), line, col, why);
            goto done;
        }
        zs_field_at(l, idx, &f);
        st = zs_put_value(l, rec, (size_t) size, name, value, strlen(value));
        if (st != ZXV_OK) {
            char pic[48], det[256];
            uint32_t line, col;
            picture(&f, pic, sizeof pic);
            last_error(det, sizeof det);
            find_decl(src, len, f.name, &line, &col);
            snprintf(why, sizeof why, "%s = %.80s does not fit PIC %s: %s. Refused: never truncated or rounded", name,
                     value, pic, det[0] ? det : zs_status_name(st));
            zp_refuse(s, st, zs_status_name(st), line, col, why);
            goto done;
        }
        for (uint64_t b = 0; b < f.size && off + b < size; b++) mask[off + b] = 1;
        if (nkeys < MAX_ITEMS) {
            keys[nkeys].index = idx;
            keys[nkeys].off = off;
            lower_copy(keys[nkeys].ref, sizeof keys[nkeys].ref, name);
            nkeys++;
        }
    }

    /* read every elementary item back */
    st = list_items(l, &it, &nit);
    if (st != ZXV_OK) {
        zp_refuse(s, st, zs_status_name(st), 0, 0,
                  st == ZP_E_LIMIT ? "the record has more than 4096 items to show" : "the record could not be listed");
        goto done;
    }
    for (uint32_t k = 0; k < nit; k++) {
        char *t = get_text(l, rec, (size_t) size, it[k].ref, it[k].size, &st);
        if (!t) {
            snprintf(why, sizeof why, "%s could not be read back", it[k].ref);
            zp_refuse(s, st, zs_status_name(st), 0, 0, why);
            goto done;
        }
        zp_output(s, it[k].ref, t);
        free(t);
        if (!mask[it[k].off]) {
            if (n_unset++) zp_buf_puts(&unset, ", ");
            zp_buf_puts(&unset, it[k].ref);
        }
    }
    if (n_unset)
        zp_notef(s, "%u field(s) had no input and stay as initialized (spaces or zero): %.600s", (unsigned) n_unset,
                 unset.p);
    if (n_unused) zp_notef(s, "inputs that are not fields of this record (not used here): %.600s", unused.p);

    if ((opt = zp_step_opt(s, "export")) != NULL) export_fields(s, l, rec, (size_t) size, it, nit, opt);
    if (zp_step_opt(s, "hex")) {
        if (size > MAX_HEX) {
            zp_notef(s, "hex: the record is %llu bytes; only records up to %u bytes are shown",
                     (unsigned long long) size, MAX_HEX);
        } else {
            char *h = (char *) malloc((size_t) size * 2u + 1u);
            if (!h) {
                zp_refuse(s, ZXV_E_NOMEM, NULL, 0, 0, "out of memory");
                goto done;
            }
            for (uint64_t b = 0; b < size; b++) snprintf(h + 2u * b, 3, "%02x", rec[b]);
            h[size * 2u] = '\0';
            zp_output(s, "hex", h);
            free(h);
        }
    }
    if ((opt = zp_step_opt(s, "posting")) != NULL) posting(s, l, rec, (size_t) size, opt);
    if (zp_step_opt(s, "compare")) compare(s, l, rec, (size_t) size, keys, nkeys);

done:
    if (unused.oom || unset.oom) zp_refuse(s, ZXV_E_NOMEM, NULL, 0, 0, "out of memory");
    zp_buf_free(&unused);
    zp_buf_free(&unset);
    free(it);
    free(keys);
    free(rec);
    free(mask);
    zs_layout_destroy(l);
}
