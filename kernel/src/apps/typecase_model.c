/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* typecase_model.c — Typecase, fonts (see typecase_model.h). A font is
 * checked by TR1 before anything else is read from it, and installed only
 * when accepted (TM6). */
#include <string.h>

#include "typecase_model.h"

#define SFNT_TRUE  0x00010000u
#define SFNT_APPLE 0x74727565u /* 'true' */
#define SFNT_CFF   0x4F54544Fu /* 'OTTO' */
#define TAG_CMAP   0x636D6170u /* 'cmap' */
#define TAG_NAME   0x6E616D65u /* 'name' */
#define DIR_BYTES  (12u + 16u * TYPECASE_TABLES_MAX)
#define PIECE      36864u /* bytes in one read, under zxvd's 49152 */
#define COPY       9216u  /* bytes in one copied part: 12288 base64 characters */
#define CMAP_SUBS  64u    /* character map records looked at */
#define NAME_RECS  256u   /* name records looked at */
#define CP_LAST    0x10FFFFu
#define FMT4_MAX   65535u

/* ---- TM2: the scripts and the letters counted for each ----------------------- */

typedef struct {
    uint32_t lo, hi;
} cp_range_t;

static const cp_range_t RANGES[] = {
    {0x0041, 0x005A}, {0x0061, 0x007A}, {0x00C0, 0x00D6}, {0x00D8, 0x00F6}, {0x00F8, 0x024F},
    {0x0391, 0x03A1}, {0x03A3, 0x03A9}, {0x03B1, 0x03C9}, {0x0400, 0x04FF}, {0x05D0, 0x05EA},
    {0x0621, 0x063A}, {0x0641, 0x064A}, {0x0904, 0x0939}, {0x0958, 0x0961}, {0x0E01, 0x0E2E},
    {0x3041, 0x3096}, {0x30A1, 0x30FA}, {0xAC00, 0xD7A3}, {0x4E00, 0x9FFF},
};

static const struct {
    const char *name;
    uint8_t first, n; /* its ranges in RANGES */
} SCRIPTS[TYPECASE_SCRIPTS] = {
    {"Latin", 0, 5},     {"Greek", 5, 3},       {"Cyrillic", 8, 1}, {"Hebrew", 9, 1},
    {"Arabic", 10, 2},   {"Devanagari", 12, 2}, {"Thai", 14, 1},    {"Hiragana", 15, 1},
    {"Katakana", 16, 1}, {"Hangul", 17, 1},     {"Han", 18, 1},
};

const char *typecase_script_name(uint32_t script)
{
    return script < TYPECASE_SCRIPTS ? SCRIPTS[script].name : NULL;
}

int64_t typecase_script_points(uint32_t script)
{
    int64_t n = 0;
    if (script >= TYPECASE_SCRIPTS) return 0;
    for (uint32_t r = SCRIPTS[script].first; r < SCRIPTS[script].first + SCRIPTS[script].n; r++)
        n += (int64_t) (RANGES[r].hi - RANGES[r].lo + 1u);
    return n;
}

/* ---- state ----------------------------------------------------------------- */

static void forget_font(typecase_t *t)
{
    t->path[0] = 0;
    t->size = 0;
    t->ntables = 0;
    memset(t->table, 0, sizeof t->table);
    t->accepted = 0;
    t->fault = TYPECASE_F_NONE;
    t->family[0] = 0;
    memset(t->covered, 0, sizeof t->covered);
    memset(t->cover_x100, 0, sizeof t->cover_x100);
    memset(t->sample, 0, sizeof t->sample);
    memset(t->nsample, 0, sizeof t->nsample);
}

void typecase_init(typecase_t *t, zxm_link_t *link)
{
    memset(t, 0, sizeof *t);
    t->link = link;
}

static int refuse(typecase_t *t, int32_t fault)
{
    t->fault = fault;
    return ZXM_E_ARG;
}

static uint32_t be16(const uint8_t *p)
{
    return (uint32_t) p[0] << 8 | (uint32_t) p[1];
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | (uint32_t) p[3];
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

static char B64[(PIECE / 3u + 1u) * 4u + 8u];

/* One files.read of at most max bytes at off: the base64 text into B64
 * (*chars), and the file's size. */
static int read_b64(zxm_link_t *l, const char *path, uint64_t off, uint32_t max, uint32_t *chars,
                    uint64_t *size)
{
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", path);
    zxm_p_u64(l, "offset", off);
    zxm_p_u64(l, "max", max);
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

/* Exactly n bytes at off of the open font into out. ZXM_E_STATE when the
 * file has changed size since it was opened, or ends before them. */
static int read_at(typecase_t *t, int64_t off, uint32_t n, uint8_t *out)
{
    uint32_t got = 0;
    while (got < n) {
        uint32_t want = n - got < PIECE ? n - got : PIECE, chars = 0;
        uint64_t size = 0;
        int rc = read_b64(t->link, t->path, (uint64_t) off + got, want, &chars, &size);
        if (rc) return rc;
        if (size != (uint64_t) t->size) return ZXM_E_STATE;
        int32_t k = b64_dec(B64, chars, out + got, want);
        if (k < 0) return ZXM_E_PROTOCOL;
        if (k == 0) return ZXM_E_STATE;
        got += (uint32_t) k;
    }
    return ZXM_OK;
}

static int32_t find_table(const typecase_t *t, uint32_t tag)
{
    for (uint32_t i = 0; i < t->ntables && i < TYPECASE_TABLES_MAX; i++)
        if (t->table[i].tag == tag) return (int32_t) i;
    return -1;
}

/* ---- TM4: the family name -------------------------------------------------- */

/* cp as UTF-8 at name[*k], when it fits TYPECASE_NAME bytes; false when not. */
static bool put_utf8(char *s, uint32_t *k, uint32_t cap, uint32_t cp)
{
    uint8_t b[4];
    uint32_t n;
    if (cp < 0x20u || cp == 0x7Fu || (cp >= 0xD800u && cp <= 0xDFFFu) || cp > CP_LAST) cp = '?';
    if (cp < 0x80u) {
        b[0] = (uint8_t) cp, n = 1;
    } else if (cp < 0x800u) {
        b[0] = (uint8_t) (0xC0u | cp >> 6), b[1] = (uint8_t) (0x80u | (cp & 0x3Fu)), n = 2;
    } else if (cp < 0x10000u) {
        b[0] = (uint8_t) (0xE0u | cp >> 12), b[1] = (uint8_t) (0x80u | (cp >> 6 & 0x3Fu));
        b[2] = (uint8_t) (0x80u | (cp & 0x3Fu)), n = 3;
    } else {
        b[0] = (uint8_t) (0xF0u | cp >> 18), b[1] = (uint8_t) (0x80u | (cp >> 12 & 0x3Fu));
        b[2] = (uint8_t) (0x80u | (cp >> 6 & 0x3Fu)), b[3] = (uint8_t) (0x80u | (cp & 0x3Fu));
        n = 4;
    }
    if (*k + n > cap) return false;
    memcpy(s + *k, b, n);
    *k += n;
    s[*k] = 0;
    return true;
}

static int read_family(typecase_t *t)
{
    static uint8_t rec[6u + 12u * NAME_RECS], str[2u * TYPECASE_NAME];
    int32_t ni = find_table(t, TAG_NAME);
    t->family[0] = 0;
    if (ni < 0 || t->table[ni].len < 6) return ZXM_OK;
    int64_t base = t->table[ni].off, tlen = t->table[ni].len;
    int rc = read_at(t, base, 6, rec);
    if (rc) return rc;
    uint32_t count = be16(rec + 2), strings = be16(rec + 4);
    if (count > NAME_RECS) count = NAME_RECS;
    if (6 + 12 * (int64_t) count > tlen) return ZXM_OK;
    if (count && (rc = read_at(t, base + 6, 12u * count, rec + 6))) return rc;
    uint32_t best = 0, pick = 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *r = rec + 6u + 12u * i;
        uint32_t plat = be16(r), enc = be16(r + 2), id = be16(r + 6), rank = 0;
        if (id != 1u) continue;
        if (plat == 3u && (enc == 1u || enc == 10u))
            rank = 3;
        else if (plat == 0u)
            rank = 2;
        else if (plat == 1u && enc == 0u)
            rank = 1;
        if (rank > best) best = rank, pick = i;
    }
    if (!best) return ZXM_OK;
    const uint8_t *r = rec + 6u + 12u * pick;
    int64_t at = (int64_t) strings + be16(r + 10), len = be16(r + 8);
    if (at + len > tlen) return ZXM_OK; /* a name outside its table: none */
    bool wide = best > 1u;
    uint32_t want = (uint32_t) len, cap = wide ? 2u * TYPECASE_NAME : TYPECASE_NAME;
    if (want > cap) want = cap;
    if (wide) want &= ~1u;
    if (want && (rc = read_at(t, base + at, want, str))) return rc;
    uint32_t k = 0;
    for (uint32_t i = 0; i < want; i += wide ? 2u : 1u) {
        uint32_t cp = wide ? be16(str + i) : (str[i] < 0x80u ? str[i] : (uint32_t) '?');
        if (!put_utf8(t->family, &k, TYPECASE_NAME, cp)) break;
    }
    return ZXM_OK;
}

/* ---- TM2, TM3: the character map into covered runs ------------------------- */

/* lo..hi all have a glyph: count them for each script, and keep the first. */
static void cover_run(typecase_t *t, uint32_t lo, uint32_t hi)
{
    for (uint32_t s = 0; s < TYPECASE_SCRIPTS; s++)
        for (uint32_t r = SCRIPTS[s].first; r < SCRIPTS[s].first + SCRIPTS[s].n; r++) {
            uint32_t a = lo > RANGES[r].lo ? lo : RANGES[r].lo;
            uint32_t b = hi < RANGES[r].hi ? hi : RANGES[r].hi;
            if (a > b) continue;
            t->covered[s] += (int64_t) (b - a + 1u);
            for (uint32_t c = a; c <= b && t->nsample[s] < TYPECASE_SAMPLE; c++)
                t->sample[s][t->nsample[s]++] = c;
        }
}

static uint8_t SUB[FMT4_MAX + 1u];

/* Format 4: segments of 16-bit code points (sub is the subtable, length bytes). */
static int cmap_format4(typecase_t *t, uint32_t length)
{
    if (length < 16u) return refuse(t, TYPECASE_F_CMAP);
    uint32_t seg2 = be16(SUB + 6), seg = seg2 / 2u;
    if (!seg || seg2 % 2u || 16u + 8u * seg > length) return refuse(t, TYPECASE_F_CMAP);
    uint32_t ends = 14u, starts = 16u + seg2, deltas = 16u + 2u * seg2, ros = 16u + 3u * seg2;
    int64_t prev = -1, run_lo = -1, run_hi = -1;
    for (uint32_t i = 0; i < seg; i++) {
        uint32_t e = be16(SUB + ends + 2u * i), s = be16(SUB + starts + 2u * i);
        uint32_t delta = be16(SUB + deltas + 2u * i), ro = be16(SUB + ros + 2u * i);
        if (s > e || (int64_t) s <= prev) return refuse(t, TYPECASE_F_CMAP);
        prev = e;
        for (uint32_t c = s; c <= e; c++) {
            uint32_t g = 0;
            if (c == 0xFFFFu) break; /* the end marker maps nothing */
            if (ro) {
                uint32_t pos = ros + 2u * i + ro + 2u * (c - s);
                if (pos + 2u > length) return refuse(t, TYPECASE_F_CMAP);
                g = be16(SUB + pos);
                if (g) g = (g + delta) & 0xFFFFu;
            } else {
                g = (c + delta) & 0xFFFFu;
            }
            if (g && run_hi >= 0 && (int64_t) c == run_hi + 1) {
                run_hi = c;
            } else if (g) {
                if (run_hi >= 0) cover_run(t, (uint32_t) run_lo, (uint32_t) run_hi);
                run_lo = run_hi = c;
            } else if (run_hi >= 0) {
                cover_run(t, (uint32_t) run_lo, (uint32_t) run_hi);
                run_lo = run_hi = -1;
            }
        }
    }
    if (run_hi >= 0) cover_run(t, (uint32_t) run_lo, (uint32_t) run_hi);
    return ZXM_OK;
}

/* Format 12: groups of 32-bit code points, read in parts from the table. */
static int cmap_format12(typecase_t *t, int64_t at, int64_t room)
{
    uint8_t h[16];
    int rc = read_at(t, at, 16, h);
    if (rc) return rc;
    int64_t length = be32(h + 4), groups = be32(h + 12);
    if (length > room || 16 + 12 * groups > length) return refuse(t, TYPECASE_F_CMAP);
    int64_t prev = -1, done = 0;
    while (done < groups) {
        int64_t part = groups - done < PIECE / 12u ? groups - done : PIECE / 12u;
        if ((rc = read_at(t, at + 16 + 12 * done, (uint32_t) (12 * part), SUB))) return rc;
        for (int64_t g = 0; g < part; g++) {
            const uint8_t *p = SUB + 12 * g;
            uint32_t lo = be32(p), hi = be32(p + 4), glyph = be32(p + 8);
            if (lo > hi || hi > CP_LAST || (int64_t) lo <= prev) return refuse(t, TYPECASE_F_CMAP);
            prev = hi;
            if (!glyph) lo++; /* the first maps to glyph 0, the missing glyph */
            if (lo <= hi) cover_run(t, lo, hi);
        }
        done += part;
    }
    return ZXM_OK;
}

static int read_cmap(typecase_t *t)
{
    static uint8_t rec[4u + 8u * CMAP_SUBS];
    int32_t ci = find_table(t, TAG_CMAP);
    if (ci < 0 || t->table[ci].len < 4) return refuse(t, TYPECASE_F_CMAP);
    int64_t base = t->table[ci].off, tlen = t->table[ci].len;
    int rc = read_at(t, base, 4, rec);
    if (rc) return rc;
    uint32_t nsub = be16(rec + 2);
    if (nsub > CMAP_SUBS) nsub = CMAP_SUBS;
    if (4 + 8 * (int64_t) nsub > tlen) return refuse(t, TYPECASE_F_CMAP);
    if (nsub && (rc = read_at(t, base + 4, 8u * nsub, rec + 4))) return rc;
    uint32_t best = 0;
    int64_t pick = 0;
    for (uint32_t i = 0; i < nsub; i++) {
        const uint8_t *r = rec + 4u + 8u * i;
        uint32_t plat = be16(r), enc = be16(r + 2), rank = 0;
        int64_t off = be32(r + 4);
        uint8_t f[2];
        bool wide = (plat == 3u && enc == 10u) || (plat == 0u && (enc == 4u || enc == 6u));
        bool narrow = (plat == 3u && enc == 1u) || (plat == 0u && enc <= 3u);
        if ((!wide && !narrow) || off + 4 > tlen) continue;
        if ((rc = read_at(t, base + off, 2, f))) return rc;
        if (wide && be16(f) == 12u)
            rank = 2;
        else if (narrow && be16(f) == 4u)
            rank = 1;
        if (rank > best) best = rank, pick = off;
    }
    if (best == 2u) return cmap_format12(t, base + pick, tlen - pick);
    if (best != 1u) return refuse(t, TYPECASE_F_CMAP);
    uint8_t h[4];
    if ((rc = read_at(t, base + pick, 4, h))) return rc;
    uint32_t length = be16(h + 2);
    if ((int64_t) length > tlen - pick) return refuse(t, TYPECASE_F_CMAP);
    if ((rc = read_at(t, base + pick, length, SUB))) return rc;
    return cmap_format4(t, length);
}

/* ---- opening a font (TM1) -------------------------------------------------- */

int typecase_open(typecase_t *t, const char *path)
{
    static uint8_t dir[DIR_BYTES];
    static int64_t off[TYPECASE_TABLES_MAX], len[TYPECASE_TABLES_MAX];
    uint32_t chars = 0;
    uint64_t size = 0;
    if (!path || !path[0] || strlen(path) > TYPECASE_PATH_MAX) return ZXM_E_ARG;
    int rc = read_b64(t->link, path, 0, DIR_BYTES, &chars, &size);
    if (rc) return rc;
    int32_t got = b64_dec(B64, chars, dir, DIR_BYTES);
    if (got < 0) return ZXM_E_PROTOCOL;
    forget_font(t);
    memcpy(t->path, path, strlen(path) + 1u);
    t->size = (int64_t) size;
    if (got < 12) return refuse(t, TYPECASE_F_FORM);
    uint32_t ver = be32(dir), n = be16(dir + 4);
    if (ver != SFNT_TRUE && ver != SFNT_APPLE && ver != SFNT_CFF) return refuse(t, TYPECASE_F_FORM);
    t->ntables = n;
    if (n <= TYPECASE_TABLES_MAX) {
        if ((uint32_t) got < 12u + 16u * n) return refuse(t, TYPECASE_F_FORM);
        for (uint32_t i = 0; i < n; i++) {
            const uint8_t *r = dir + 12u + 16u * i;
            t->table[i].tag = be32(r);
            t->table[i].off = off[i] = be32(r + 8);
            t->table[i].len = len[i] = be32(r + 12);
        }
    }
    t->accepted = typecase_font_ok(off, len, n, t->size);
    if (t->accepted != 1) return refuse(t, TYPECASE_F_TABLES);
    if ((rc = read_family(t))) return rc;
    if ((rc = read_cmap(t))) {
        memset(t->covered, 0, sizeof t->covered);
        memset(t->nsample, 0, sizeof t->nsample);
        return rc;
    }
    for (uint32_t s = 0; s < TYPECASE_SCRIPTS; s++) {
        t->cover_x100[s] = typecase_coverage_x100(t->covered[s], typecase_script_points(s));
        if (t->cover_x100[s] < 0) {
            memset(t->cover_x100, 0, sizeof t->cover_x100);
            return refuse(t, TYPECASE_F_COVER);
        }
    }
    return ZXM_OK;
}

/* ---- finding fonts: files.list --------------------------------------------- */

static bool is_font(const char *name)
{
    size_t n = strlen(name);
    if (n < 5u) return false;
    const char *e = name + n - 4u;
    char a = (char) (e[1] | 0x20), b = (char) (e[2] | 0x20), c = (char) (e[3] | 0x20);
    return e[0] == '.' &&
           ((a == 't' && b == 't' && c == 'f') || (a == 'o' && b == 't' && c == 'f'));
}

int typecase_list(typecase_t *t, const char *folder)
{
    static typecase_file_t next[TYPECASE_LIST];
    char name[TYPECASE_PATH_MAX + 1];
    zxm_link_t *l = t->link;
    size_t fl = folder ? strlen(folder) : 0u;
    if (!folder || fl >= TYPECASE_PATH_MAX) return ZXM_E_ARG;
    zxm_begin(l, "files.list");
    zxm_p_str(l, "path", folder);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "entries");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = 0, total = zxm_len(l, arr);
    for (uint32_t i = 0; i < total && n < TYPECASE_LIST; i++) {
        int32_t e = zxm_at(l, arr, i);
        bool dir = false;
        typecase_file_t f;
        memset(&f, 0, sizeof f);
        if (zxm_get_bool(l, e, "dir", &dir)) return ZXM_E_PROTOCOL;
        if (dir) continue;
        rc = zxm_get_str(l, e, "name", name, sizeof name);
        if (rc == ZXM_E_TOOBIG) continue; /* a name too long to keep */
        if (rc || zxm_get_u64(l, e, "size", &f.size) || zxm_get_i64(l, e, "mtime", &f.mtime))
            return ZXM_E_PROTOCOL;
        size_t nl = strlen(name);
        if (!is_font(name) || fl + (fl ? 1u : 0u) + nl > TYPECASE_PATH_MAX) continue;
        memcpy(f.path, folder, fl);
        if (fl) f.path[fl] = '/';
        memcpy(f.path + fl + (fl ? 1u : 0u), name, nl + 1u);
        next[n++] = f;
    }
    memcpy(t->file, next, (size_t) n * sizeof next[0]);
    t->nfiles = n;
    return ZXM_OK;
}

/* ---- TM5: the preview ------------------------------------------------------ */

int32_t typecase_preview(const typecase_t *t, uint32_t script, char *out, uint32_t cap)
{
    uint32_t k = 0;
    if (!t->path[0] || script >= TYPECASE_SCRIPTS || !out || !cap) return -1;
    out[0] = 0;
    for (uint32_t i = 0; i < t->nsample[script]; i++)
        if (!put_utf8(out, &k, cap - 1u, t->sample[script][i])) return -1;
    return (int32_t) k;
}

/* ---- TM6: install, a copy in parts ----------------------------------------- */

int typecase_install(typecase_t *t, const char *folder)
{
    static char target[TYPECASE_PATH_MAX + 1];
    static uint8_t part[COPY];
    zxm_link_t *l = t->link;
    if (!t->path[0] || t->accepted != 1) return ZXM_E_STATE;
    if (!folder) return ZXM_E_ARG;
    const char *slash = strrchr(t->path, '/');
    const char *base = slash ? slash + 1 : t->path;
    size_t fl = strlen(folder), bl = strlen(base);
    if (fl + (fl ? 1u : 0u) + bl > TYPECASE_PATH_MAX) return ZXM_E_ARG;
    memcpy(target, folder, fl);
    if (fl) target[fl] = '/';
    memcpy(target + fl + (fl ? 1u : 0u), base, bl + 1u);
    if (!strcmp(target, t->path)) return ZXM_E_ARG;
    uint64_t at = 0, whole = (uint64_t) t->size, size = 0;
    bool first = true;
    do {
        uint32_t want = whole - at < COPY ? (uint32_t) (whole - at) : COPY, chars = 0;
        int rc = read_b64(l, t->path, at, want, &chars, &size);
        if (rc) return rc;
        int32_t k = b64_dec(B64, chars, part, COPY);
        if (k < 0) return ZXM_E_PROTOCOL;
        if (size != whole || (uint32_t) k != want) return ZXM_E_STATE;
        bool last = at + want >= whole;
        zxm_begin(l, "files.write");
        zxm_p_str(l, "path", target);
        zxm_p_strn(l, "base64", B64, chars);
        zxm_p_bool(l, "exclusive", true); /* never over a file that is there */
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        if ((rc = zxm_call(l))) return rc;
        at += want;
        first = false;
        zxm_progress(l, at, whole);
    } while (at < whole);
    if (zxm_get_u64(l, l->result, "size", &size) || size != whole) return ZXM_E_PROTOCOL;
    memcpy(t->installed, target, strlen(target) + 1u);
    return ZXM_OK;
}
