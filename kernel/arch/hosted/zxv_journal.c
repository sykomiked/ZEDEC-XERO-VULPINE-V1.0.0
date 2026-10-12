/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_journal.c — the journal (see zxv_journal.h J1-J7). */
#if !defined(_WIN32)
#    define _DEFAULT_SOURCE
#    include <sys/stat.h>
#    include <sys/types.h>
#    define SEP "/"
#else
#    include <windows.h>
#    include <direct.h>
#    define SEP "\\"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sha256.h"
#include "zxv_journal.h"

#define LINE_CAP 512u
#define PROBE    100000u /* segment numbers searched past the head */

static char DIR_[ZJR_DIR_MAX + 1];
static FILE *CUR;
static uint64_t CUR_N, FIRST_N; /* the segment written, the oldest kept */
static uint64_t CUR_BYTES, TOTAL_BYTES;
static uint64_t SEQ; /* the last seq written */
static uint8_t HASH[32];
static uint32_t SEG_LIMIT = ZJR_SEGMENT, TOTAL_LIMIT = ZJR_TOTAL;

/* ===================== files ===================== */

static bool seg_path(uint64_t n, char *out, size_t cap)
{
    return (size_t) snprintf(out, cap, "%s" SEP "%llu.jnl", DIR_, (unsigned long long) n) < cap;
}

static int64_t file_size(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    long n = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    fclose(f);
    return n;
}

static bool make_dir(const char *p)
{
#if defined(_WIN32)
    if (_mkdir(p) != 0 && GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES) return false;
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) &&
           !(a & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    struct stat st;
    (void) mkdir(p, S_IRWXU);
    return lstat(p, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static bool replace_file(const char *from, const char *to)
{
#if defined(_WIN32)
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(from, to) == 0;
#endif
}

static void hexify(const uint8_t h[32], char out[65])
{
    static const char D[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[2 * i] = D[h[i] >> 4];
        out[2 * i + 1] = D[h[i] & 15];
    }
    out[64] = 0;
}

static bool unhex(const char *s, uint8_t h[32])
{
    for (int i = 0; i < 64; i++) {
        char c = s[i];
        int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (v < 0) return false;
        h[i / 2] = (uint8_t) (i & 1 ? h[i / 2] | v : v << 4);
    }
    return true;
}

static bool num(const char **s, uint64_t *v)
{
    const char *p = *s;
    uint64_t x = 0;
    if (*p < '0' || *p > '9') return false;
    for (; *p >= '0' && *p <= '9'; p++) {
        if (x > (UINT64_MAX - (uint64_t) (*p - '0')) / 10) return false;
        x = x * 10 + (uint64_t) (*p - '0');
    }
    *v = x;
    *s = p;
    return true;
}

static const char *LEVEL[] = {"info", "warn", "error"};

/* the hashed part of a line: "<seq> <ms> <level> <source> <text>" */
static void line_hash(const uint8_t prev[32], const zjr_entry_t *e, uint8_t out[32])
{
    char body[LINE_CAP];
    int n = snprintf(body, sizeof body, "%llu %llu %s %s %s", (unsigned long long) e->seq,
                     (unsigned long long) e->ms, LEVEL[e->level], e->source, e->text);
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, prev, 32);
    sha256_update(&c, (const uint8_t *) body, (size_t) n);
    sha256_final(&c, out);
}

/* ===================== reading a segment ===================== */

typedef struct {
    uint64_t next_seq; /* the header's */
    uint8_t prev[32];
} head_t;

enum { SEG_WHOLE = 0, SEG_BROKEN = 1, SEG_TORN = 2, SEG_GONE = -1 };

typedef int (*each_fn)(void *ctx, const zjr_entry_t *e);

/* Read segment n: its header into *h, then each line that parses and
 * chains, in order, to fn (when given). *seq and hash end at the last good
 * line (the header's when none). SEG_BROKEN: a whole line that does not
 * parse or chain (*bad = the seq it should have had); SEG_TORN: a last line
 * without its newline; SEG_GONE: no file, or a bad header. */
static int read_seg(uint64_t n, head_t *h, each_fn fn, void *ctx, uint64_t *seq, uint8_t hash[32],
                    uint64_t *bad)
{
    char p[ZJR_DIR_MAX + 32], line[LINE_CAP];
    if (!seg_path(n, p, sizeof p)) return SEG_GONE;
    FILE *f = fopen(p, "rb");
    if (!f) return SEG_GONE;
    const char *q = line + 14;
    bool ok = fgets(line, sizeof line, f) && !strncmp(line, "zxv-journal 1 ", 14) &&
              num(&q, &h->next_seq) && *q++ == ' ' && unhex(q, h->prev) && !strcmp(q + 64, "\n") &&
              h->next_seq > 0;
    if (!ok) {
        fclose(f);
        return SEG_GONE;
    }
    *seq = h->next_seq - 1;
    memcpy(hash, h->prev, 32);
    int rc = SEG_WHOLE;
    while (rc == SEG_WHOLE && fgets(line, sizeof line, f)) {
        size_t len = strlen(line);
        if (!len || line[len - 1] != '\n') {
            rc = SEG_TORN;
            break;
        }
        line[len - 1] = 0;
        zjr_entry_t e;
        uint64_t lv = 3;
        uint8_t want[32], got[32];
        memset(&e, 0, sizeof e);
        q = line;
        const char *src, *sp;
        ok = num(&q, &e.seq) && *q++ == ' ' && num(&q, &e.ms) && *q++ == ' ';
        for (uint64_t i = 0; ok && i < 3 && lv == 3; i++) {
            size_t k = strlen(LEVEL[i]);
            if (!strncmp(q, LEVEL[i], k) && q[k] == ' ') {
                lv = i;
                q += k + 1;
            }
        }
        ok = ok && lv < 3;
        src = q;
        sp = ok ? strchr(q, ' ') : NULL;
        ok = ok && sp && sp > src && (size_t) (sp - src) < sizeof e.source && unhex(sp + 1, got) &&
             sp[65] == ' ' && strlen(sp + 66) <= ZXVD_LOG_TEXT && e.seq == *seq + 1;
        if (ok) {
            memcpy(e.source, src, (size_t) (sp - src));
            memcpy(e.text, sp + 66, strlen(sp + 66) + 1);
            e.level = (uint8_t) lv;
            line_hash(hash, &e, want);
            ok = !memcmp(want, got, 32);
        }
        if (!ok) {
            *bad = *seq + 1;
            rc = SEG_BROKEN;
            break;
        }
        *seq = e.seq;
        memcpy(hash, got, 32);
        if (fn && fn(ctx, &e)) break;
    }
    fclose(f);
    return rc;
}

/* ===================== writing ===================== */

static bool write_head(void)
{
    char p[ZJR_DIR_MAX + 32], t[ZJR_DIR_MAX + 32];
    snprintf(p, sizeof p, "%s" SEP "head", DIR_);
    snprintf(t, sizeof t, "%s" SEP "head.tmp", DIR_);
    FILE *f = fopen(t, "wb");
    if (!f) return false;
    bool ok =
        fprintf(f, "%llu %llu\n", (unsigned long long) CUR_N, (unsigned long long) FIRST_N) > 0;
    ok = fclose(f) == 0 && ok;
    return ok && replace_file(t, p);
}

/* Let the oldest segments go while all of them pass the limit. The head
 * names the new oldest before any file goes, so a crash between the two
 * leaves a stray old file rather than a gap that reads as a missing one. */
static bool prune(void)
{
    char p[ZJR_DIR_MAX + 32];
    uint64_t from = FIRST_N, total = TOTAL_BYTES;
    while (total > TOTAL_LIMIT && FIRST_N < CUR_N) {
        int64_t sz = seg_path(FIRST_N, p, sizeof p) ? file_size(p) : -1;
        if (sz > 0) total -= (uint64_t) sz < total ? (uint64_t) sz : total;
        FIRST_N++;
    }
    if (!write_head()) return false;
    for (uint64_t k = from; k < FIRST_N; k++)
        if (seg_path(k, p, sizeof p)) remove(p);
    TOTAL_BYTES = total;
    return true;
}

/* start segment CUR_N + 1 */
static bool next_segment(void)
{
    char p[ZJR_DIR_MAX + 32], hex[65];
    if (CUR) fclose(CUR);
    CUR = NULL;
    CUR_N++;
    if (!seg_path(CUR_N, p, sizeof p) || !(CUR = fopen(p, "wb"))) return false;
    hexify(HASH, hex);
    int n = fprintf(CUR, "zxv-journal 1 %llu %s\n", (unsigned long long) SEQ + 1, hex);
    if (!FIRST_N) FIRST_N = CUR_N;
    if (n > 0) TOTAL_BYTES += (uint64_t) n;
    if (n <= 0 || fflush(CUR) != 0 || !prune()) {
        fclose(CUR);
        CUR = NULL;
        return false;
    }
    CUR_BYTES = (uint64_t) n;
    return true;
}

static void sink(const zxvd_log_entry_t *le)
{
    if (!CUR) return;
    zjr_entry_t e;
    memset(&e, 0, sizeof e);
    e.seq = SEQ + 1;
    e.ms = le->unix_ms;
    e.level = le->level <= ZXVD_LOG_ERROR ? le->level : ZXVD_LOG_ERROR;
    for (size_t i = 0; le->source[i] && i < sizeof e.source - 1; i++)
        e.source[i] = le->source[i] == ' ' ? '_' : le->source[i];
    if (!e.source[0]) e.source[0] = '-';
    memcpy(e.text, le->text, strlen(le->text) + 1);
    uint8_t h[32];
    char hex[65];
    line_hash(HASH, &e, h);
    hexify(h, hex);
    int n = fprintf(CUR, "%llu %llu %s %s %s %s\n", (unsigned long long) e.seq,
                    (unsigned long long) e.ms, LEVEL[e.level], e.source, hex, e.text);
    if (n <= 0 || fflush(CUR) != 0) { /* the disk refused: stop rather than chain past a gap */
        fclose(CUR);
        CUR = NULL;
        zxvd_log_set_sink(NULL);
        return;
    }
    SEQ = e.seq;
    memcpy(HASH, h, 32);
    CUR_BYTES += (uint64_t) n;
    TOTAL_BYTES += (uint64_t) n;
    if (CUR_BYTES >= SEG_LIMIT && !next_segment()) zxvd_log_set_sink(NULL);
}

/* ===================== open, close ===================== */

void zjr_set_limits(uint32_t segment_bytes, uint32_t total_bytes)
{
    SEG_LIMIT = segment_bytes ? segment_bytes : ZJR_SEGMENT;
    TOTAL_LIMIT = total_bytes ? total_bytes : ZJR_TOTAL;
}

void zjr_close(void)
{
    if (CUR) fclose(CUR);
    CUR = NULL;
    zxvd_log_set_sink(NULL);
    DIR_[0] = 0;
    CUR_N = FIRST_N = CUR_BYTES = TOTAL_BYTES = SEQ = 0;
    memset(HASH, 0, sizeof HASH);
}

bool zjr_on(void)
{
    return CUR != NULL;
}

static bool seg_exists(uint64_t n)
{
    char p[ZJR_DIR_MAX + 32];
    return seg_path(n, p, sizeof p) && file_size(p) >= 0;
}

int zjr_open(const char *dir)
{
    zjr_close();
    if (!dir || !dir[0]) return 0;
    if (strlen(dir) > ZJR_DIR_MAX || !make_dir(dir)) return -1;
    memcpy(DIR_, dir, strlen(dir) + 1);
    /* the newest segment: the head's, or past it if the head is behind */
    char p[ZJR_DIR_MAX + 32], buf[32];
    uint64_t n = 0, oldest = 0;
    snprintf(p, sizeof p, "%s" SEP "head", DIR_);
    FILE *f = fopen(p, "rb");
    if (f) {
        const char *q = buf;
        if (!fgets(buf, sizeof buf, f) || !num(&q, &n))
            n = 0;
        else if (*q != ' ' || (q++, !num(&q, &oldest)) || oldest > n)
            oldest = 0;
        fclose(f);
    }
    for (uint32_t i = 0; i < PROBE && seg_exists(n + 1); i++) n++;
    /* where the chain goes on: the newest segment that reads */
    for (uint64_t k = n; k > 0; k--) {
        head_t h;
        uint64_t bad;
        if (read_seg(k, &h, NULL, NULL, &SEQ, HASH, &bad) != SEG_GONE) break;
    }
    /* what is kept, oldest first: the head says, so a segment gone from the
     * middle reads as missing (J7); with no word from the head, the run of
     * segments that ends at the newest */
    FIRST_N = oldest ? oldest : n;
    while (!oldest && FIRST_N > 1 && seg_exists(FIRST_N - 1)) FIRST_N--;
    for (uint64_t k = FIRST_N; k && k <= n; k++) {
        if (!seg_path(k, p, sizeof p)) break;
        int64_t sz = file_size(p);
        if (sz > 0) TOTAL_BYTES += (uint64_t) sz;
    }
    CUR_N = n;
    if (!next_segment()) {
        zjr_close();
        return -1;
    }
    zxvd_log_set_sink(sink);
    zxvd_log(ZXVD_LOG_INFO, "journal", "opened");
    return 0;
}

/* ===================== queries ===================== */

static char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char) (c | 0x20) : c;
}

static bool has_ci(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] && lower(hay[i]) == lower(needle[i])) i++;
        if (i == n) return true;
    }
    return false;
}

typedef struct {
    zjr_entry_t *v;
    uint32_t n, cap;
} grab_t;

static int grab(void *ctx, const zjr_entry_t *e)
{
    grab_t *g = ctx;
    if (g->n == g->cap) {
        uint32_t nc = g->cap ? g->cap * 2 : 256;
        zjr_entry_t *nv = realloc(g->v, nc * sizeof *nv);
        if (!nv) return 1;
        g->v = nv;
        g->cap = nc;
    }
    g->v[g->n++] = *e;
    return 0;
}

static bool match(const zjr_query_t *q, const zjr_entry_t *e)
{
    return (!q->before || e->seq < q->before) && e->ms >= q->since_ms &&
           (!q->until_ms || e->ms <= q->until_ms) && e->level >= q->min_level &&
           (!q->source || !q->source[0] || !strcmp(q->source, e->source)) &&
           (!q->text || !q->text[0] || has_ci(e->source, q->text) || has_ci(e->text, q->text));
}

int zjr_query(const zjr_query_t *q, zjr_entry_t *out, uint32_t cap, uint32_t *n, bool *more)
{
    *n = 0;
    *more = false;
    if (!CUR) return -1;
    grab_t g = {0};
    for (uint64_t k = CUR_N; k >= FIRST_N && k > 0 && !*more; k--) {
        head_t h;
        uint64_t seq, bad;
        uint8_t hash[32];
        g.n = 0;
        if (read_seg(k, &h, grab, &g, &seq, hash, &bad) == SEG_GONE) continue;
        for (uint32_t i = g.n; i-- > 0;) {
            if (!match(q, &g.v[i])) continue;
            if (*n == cap) {
                *more = true;
                break;
            }
            out[(*n)++] = g.v[i];
        }
    }
    free(g.v);
    return 0;
}

int zjr_verify(zjr_verify_t *v)
{
    memset(v, 0, sizeof *v);
    if (!CUR) return -1;
    uint64_t seq = 0;
    uint8_t hash[32];
    for (uint64_t k = FIRST_N; k <= CUR_N; k++) {
        head_t h;
        uint64_t end, bad = 0;
        uint8_t last[32];
        int rc = read_seg(k, &h, NULL, NULL, &end, last, &bad);
        if (rc == SEG_GONE) {
            v->broken_at = seq + 1;
            snprintf(v->why, sizeof v->why, "segment %llu is missing or its header is damaged",
                     (unsigned long long) k);
            return 0;
        }
        if (k > FIRST_N && (h.next_seq != seq + 1 || memcmp(h.prev, hash, 32))) {
            v->broken_at = seq + 1;
            snprintf(v->why, sizeof v->why, "segment %llu does not follow the one before it",
                     (unsigned long long) k);
            return 0;
        }
        if (k == FIRST_N) v->first_seq = h.next_seq;
        v->segments++;
        v->entries += end - (h.next_seq - 1);
        if (rc == SEG_BROKEN) {
            v->broken_at = bad;
            snprintf(v->why, sizeof v->why, "line %llu was changed or is missing",
                     (unsigned long long) bad);
            return 0;
        }
        seq = end;
        memcpy(hash, last, 32);
    }
    v->last_seq = seq;
    v->ok = true;
    return 0;
}
