/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_keepsake.c — Keepsake (see zxv_keepsake.h K1-K7). */
#if !defined(_WIN32)
#    define _DEFAULT_SOURCE
#    define _DARWIN_C_SOURCE
#    include <sys/types.h>
#    include <sys/stat.h>
#    include <dirent.h>
#    include <fcntl.h>
#    include <unistd.h>
#else
#    include <windows.h>
#    include <io.h>
#endif
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sha256.h"
#include "zxv_keepsake.h"

#define ABS_MAX   4096u
#define LINE_CAP  1280u /* a snapshot line: "f", hash, size, time, path */
#define CHUNK     (64u * 1024u)
#define DEPTH_MAX 64u
#define MAGIC     "zxv-keepsake 1\n"

/* ===================== the operating system ===================== */

typedef struct {
    bool exists, dir, link;
    uint64_t size;
} kind_t;

typedef int (*each_fn)(void *ctx, const char *name);

#if defined(_WIN32)
#    define SEP "\\"

static const wchar_t *W(int slot, const char *p)
{
    static wchar_t w[2][ABS_MAX];
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, p, -1, w[slot], (int) ABS_MAX);
    if (n <= 0) return NULL;
    for (wchar_t *q = w[slot]; *q; q++)
        if (*q == L'/') *q = L'\\';
    return w[slot];
}

static kind_t os_kind(const char *p)
{
    kind_t k = {0};
    WIN32_FILE_ATTRIBUTE_DATA a;
    const wchar_t *w = W(0, p);
    if (!w || !GetFileAttributesExW(w, GetFileExInfoStandard, &a)) return k;
    k.exists = true;
    k.dir = (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    k.link = (a.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    k.size = k.dir ? 0 : ((uint64_t) a.nFileSizeHigh << 32) | a.nFileSizeLow;
    return k;
}

static int os_mkdir(const char *p)
{
    const wchar_t *w = W(0, p);
    if (w && CreateDirectoryW(w, NULL)) return 0;
    return w && GetLastError() == ERROR_ALREADY_EXISTS ? 0 : -1;
}

static int os_rename(const char *from, const char *to)
{
    const wchar_t *a = W(0, from), *b = W(1, to);
    return a && b && MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
}

static int os_unlink(const char *p)
{
    const wchar_t *w = W(0, p);
    return w && DeleteFileW(w) ? 0 : -1;
}

static void os_rmdir(const char *p)
{
    const wchar_t *w = W(0, p);
    if (w) RemoveDirectoryW(w);
}

static FILE *os_fopen(const char *p, const char *mode)
{
    wchar_t m[8];
    const wchar_t *w = W(0, p);
    if (!w || !MultiByteToWideChar(CP_UTF8, 0, mode, -1, m, 8)) return NULL;
    return _wfopen(w, m);
}

static int os_sync(FILE *f)
{
    return fflush(f) == 0 && _commit(_fileno(f)) == 0 ? 0 : -1;
}

static int os_each(const char *dir, each_fn fn, void *ctx)
{
    char pat[ABS_MAX];
    if ((size_t) snprintf(pat, sizeof pat, "%s\\*", dir) >= sizeof pat) return -1;
    WIN32_FIND_DATAW d;
    const wchar_t *w = W(0, pat);
    HANDLE h = w ? FindFirstFileW(w, &d) : INVALID_HANDLE_VALUE;
    if (h == INVALID_HANDLE_VALUE) return -1;
    int rc = 0;
    do {
        char name[ZXF_NAME_MAX * 4 + 1];
        if (!WideCharToMultiByte(CP_UTF8, 0, d.cFileName, -1, name, sizeof name, NULL, NULL))
            continue;
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        rc = fn(ctx, name);
    } while (rc == 0 && FindNextFileW(h, &d));
    FindClose(h);
    return rc;
}

static bool os_absolute(const char *p)
{
    return (((p[0] | 0x20) >= 'a' && (p[0] | 0x20) <= 'z' && p[1] == ':' &&
             (p[2] == '\\' || p[2] == '/')) ||
            (p[0] == '\\' && p[1] == '\\'));
}

/* the full path, lower-cased (ASCII) for comparing */
static bool os_full(const char *p, char *out, size_t cap)
{
    wchar_t full[ABS_MAX];
    const wchar_t *w = W(0, p);
    if (!w || !GetFullPathNameW(w, ABS_MAX, full, NULL)) return false;
    if (!WideCharToMultiByte(CP_UTF8, 0, full, -1, out, (int) cap, NULL, NULL)) return false;
    for (char *q = out; *q; q++)
        if (*q >= 'A' && *q <= 'Z') *q = (char) (*q | 0x20);
    return true;
}

#else /* POSIX */
#    define SEP "/"

static kind_t os_kind(const char *p)
{
    kind_t k = {0};
    struct stat st;
    if (lstat(p, &st) != 0) return k;
    k.exists = true;
    k.link = S_ISLNK(st.st_mode);
    k.dir = S_ISDIR(st.st_mode);
    k.size = S_ISREG(st.st_mode) ? (uint64_t) st.st_size : 0;
    return k;
}

static int os_mkdir(const char *p)
{
    return mkdir(p, S_IRWXU) == 0 || errno == EEXIST ? 0 : -1;
}

static int os_rename(const char *from, const char *to)
{
    return rename(from, to);
}

static int os_unlink(const char *p)
{
    return unlink(p);
}

static void os_rmdir(const char *p)
{
    (void) rmdir(p);
}

static FILE *os_fopen(const char *p, const char *mode)
{
    if (mode[0] == 'r') {
        int fd = open(p, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        return fd < 0 ? NULL : fdopen(fd, "rb");
    }
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (fd < 0) return NULL;
    FILE *f = fdopen(fd, "wb");
    if (!f) close(fd);
    return f;
}

static int os_sync(FILE *f)
{
    return fflush(f) == 0 && fsync(fileno(f)) == 0 ? 0 : -1;
}

static int os_each(const char *dir, each_fn fn, void *ctx)
{
    DIR *d = opendir(dir);
    if (!d) return -1;
    struct dirent *e;
    int rc = 0;
    while (rc == 0 && (e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        rc = fn(ctx, e->d_name);
    }
    closedir(d);
    return rc;
}

static bool os_absolute(const char *p)
{
    return p[0] == '/';
}

static bool os_full(const char *p, char *out, size_t cap)
{
    char *r = realpath(p, NULL);
    if (!r) return false;
    bool ok = (size_t) snprintf(out, cap, "%s", r) < cap;
    free(r);
    return ok;
}
#endif

/* ===================== small helpers ===================== */

static int say(char *err, size_t cap, int rc, const char *fmt, ...)
{
    if (err && cap) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, cap, fmt, ap);
        va_end(ap);
    }
    return rc;
}

static void hex32(const uint8_t h[32], char out[65])
{
    static const char D[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[2 * i] = D[h[i] >> 4];
        out[2 * i + 1] = D[h[i] & 15];
    }
    out[64] = 0;
}

static bool unhex32(const char *s, uint8_t h[32])
{
    for (int i = 0; i < 64; i++) {
        char c = s[i];
        int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (v < 0) return false;
        if (i & 1)
            h[i / 2] = (uint8_t) (h[i / 2] | v);
        else
            h[i / 2] = (uint8_t) (v << 4);
    }
    return true;
}

/* ===================== the store (K1) ===================== */

typedef struct {
    char base[ABS_MAX]; /* <dest>/ZXV Keepsake */
    char space[ZXF_SPACE_ID_MAX + 1];
} store_t;

static bool join(char *out, const char *a, const char *b)
{
    return (size_t) snprintf(out, ABS_MAX, "%s" SEP "%s", a, b) < ABS_MAX;
}

static bool space_dir(const store_t *s, const char *space, char *out)
{
    return (size_t) snprintf(out, ABS_MAX, "%s" SEP "spaces" SEP "%s", s->base, space) < ABS_MAX;
}

static bool snap_path(const store_t *s, const char *space, uint32_t id, const char *ext, char *out)
{
    return (size_t) snprintf(out, ABS_MAX, "%s" SEP "spaces" SEP "%s" SEP "%u.%s", s->base, space,
                             id, ext) < ABS_MAX;
}

static bool obj_path(const store_t *s, const char hex[65], char *out)
{
    return (size_t) snprintf(out, ABS_MAX, "%s" SEP "objects" SEP "%.2s" SEP "%s", s->base, hex,
                             hex) < ABS_MAX;
}

/* K1. create: make the store's folders; else a missing store is NOTFOUND. */
static int open_store(const char *dest, bool create, store_t *s, char *err, size_t cap)
{
    if (!dest || !dest[0] || strlen(dest) > ZKS_DEST_MAX || !os_absolute(dest))
        return say(err, cap, ZKS_E_DEST, "the destination must be a folder's absolute path");
    kind_t k = os_kind(dest);
    if (k.link) return say(err, cap, ZKS_E_DEST, "the destination is a link");
    if (!k.exists || !k.dir) return say(err, cap, ZKS_E_DEST, "no folder at the destination");
    char root[ABS_MAX], fr[ABS_MAX], fd[ABS_MAX];
    if (zxf_root(root, sizeof root) || !os_full(root, fr, sizeof fr) ||
        !os_full(dest, fd, sizeof fd))
        return say(err, cap, ZKS_E_IO, "could not read the folders' full paths");
    size_t rl = strlen(fr);
    if (!strncmp(fd, fr, rl) && (fd[rl] == 0 || fd[rl] == SEP[0]))
        return say(err, cap, ZKS_E_DEST, "the destination is inside the ZXV folder");
    memset(s, 0, sizeof *s);
    snprintf(s->space, sizeof s->space, "%s", zxf_space());
    size_t dl = strlen(dest);
    while (dl > 1 && (dest[dl - 1] == '/' || dest[dl - 1] == SEP[0])) dl--;
    snprintf(s->base, sizeof s->base, "%.*s" SEP ZKS_DIR, (int) dl, dest);
    char p[ABS_MAX];
    kind_t b = os_kind(s->base);
    if (b.link || (b.exists && !b.dir))
        return say(err, cap, ZKS_E_DEST, "\"" ZKS_DIR "\" at the destination is not a folder");
    if (!create) return b.exists ? ZKS_OK : say(err, cap, ZKS_E_NOTFOUND, "no backups there yet");
    if (os_mkdir(s->base) || !join(p, s->base, "objects") || os_mkdir(p) ||
        !join(p, s->base, "spaces") || os_mkdir(p) || !space_dir(s, s->space, p) || os_mkdir(p))
        return say(err, cap, ZKS_E_IO, "could not make the backup folders");
    return ZKS_OK;
}

/* the snapshot numbers of a space, ascending; *ids malloc'd */
typedef struct {
    uint32_t *v, n, cap;
} ids_t;

static int id_of(void *ctx, const char *name)
{
    ids_t *x = ctx;
    uint32_t id = 0;
    size_t i = 0;
    if (name[0] < '1' || name[0] > '9') return 0;
    for (; name[i] >= '0' && name[i] <= '9' && i < 6; i++)
        id = id * 10 + (uint32_t) (name[i] - '0');
    if (strcmp(name + i, ".snap") || id > ZKS_SNAPS_MAX) return 0;
    if (x->n == x->cap) {
        uint32_t nc = x->cap ? x->cap * 2 : 64;
        uint32_t *nv = realloc(x->v, nc * sizeof *nv);
        if (!nv) return -1;
        x->v = nv;
        x->cap = nc;
    }
    x->v[x->n++] = id;
    return 0;
}

static int u32_cmp(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *) a, y = *(const uint32_t *) b;
    return (x > y) - (x < y);
}

static int list_ids(const store_t *s, const char *space, ids_t *x)
{
    char d[ABS_MAX];
    memset(x, 0, sizeof *x);
    if (!space_dir(s, space, d)) return ZKS_E_IO;
    if (!os_kind(d).exists) return ZKS_OK;
    if (os_each(d, id_of, x)) {
        free(x->v);
        memset(x, 0, sizeof *x);
        return ZKS_E_IO;
    }
    if (x->n) qsort(x->v, x->n, sizeof *x->v, u32_cmp);
    return ZKS_OK;
}

/* ===================== snapshots (K3) ===================== */

typedef struct {
    FILE *f;
    sha256_ctx_t h;
    char line[LINE_CAP];
    int64_t time;
    uint32_t files;
    uint64_t bytes;
} reader_t;

typedef struct {
    char hex[65];
    uint8_t hash[32];
    uint64_t size;
    int64_t mtime;
    const char *path; /* in the reader's line */
} entry_t;

static bool get_line(reader_t *r)
{
    if (!fgets(r->line, sizeof r->line, r->f)) return false;
    size_t n = strlen(r->line);
    return n && r->line[n - 1] == '\n';
}

static bool num_u64(const char **s, uint64_t *v)
{
    const char *p = *s;
    uint64_t x = 0;
    if (*p < '0' || *p > '9' || (*p == '0' && p[1] >= '0' && p[1] <= '9')) return false;
    for (; *p >= '0' && *p <= '9'; p++) {
        if (x > (UINT64_MAX - (uint64_t) (*p - '0')) / 10) return false;
        x = x * 10 + (uint64_t) (*p - '0');
    }
    *v = x;
    *s = p;
    return true;
}

static bool num_i64(const char **s, int64_t *v)
{
    bool neg = **s == '-';
    uint64_t u;
    if (neg) (*s)++;
    if (!num_u64(s, &u) || u > (uint64_t) INT64_MAX) return false;
    *v = neg ? -(int64_t) u : (int64_t) u;
    return true;
}

static int snap_open(const store_t *s, const char *space, uint32_t id, reader_t *r)
{
    char p[ABS_MAX];
    memset(r, 0, sizeof *r);
    if (!snap_path(s, space, id, "snap", p)) return ZKS_E_NOTFOUND;
    if (!(r->f = os_fopen(p, "rb"))) return ZKS_E_NOTFOUND;
    sha256_init(&r->h);
    const char *q;
    char want[8 + ZXF_SPACE_ID_MAX + 2];
    snprintf(want, sizeof want, "space %s\n", space);
    bool ok = get_line(r) && !strcmp(r->line, MAGIC);
    if (ok) sha256_update(&r->h, (const uint8_t *) r->line, strlen(r->line));
    ok = ok && get_line(r) && !strcmp(r->line, want);
    if (ok) sha256_update(&r->h, (const uint8_t *) r->line, strlen(r->line));
    ok = ok && get_line(r) && !strncmp(r->line, "time ", 5);
    q = r->line + 5;
    ok = ok && num_i64(&q, &r->time) && !strcmp(q, "\n");
    if (ok) sha256_update(&r->h, (const uint8_t *) r->line, strlen(r->line));
    if (!ok) {
        fclose(r->f);
        r->f = NULL;
        return ZKS_E_DAMAGED;
    }
    return ZKS_OK;
}

static void snap_close(reader_t *r)
{
    if (r->f) fclose(r->f);
    r->f = NULL;
}

/* 1: an entry; 0: the end, checked; ZKS_E_DAMAGED otherwise. */
static int snap_next(reader_t *r, entry_t *e)
{
    if (!get_line(r)) return ZKS_E_DAMAGED;
    const char *q = r->line + 2;
    if (!strncmp(r->line, "f ", 2)) {
        uint64_t size;
        int64_t mtime;
        if (!unhex32(q, e->hash) || q[64] != ' ') return ZKS_E_DAMAGED;
        memcpy(e->hex, q, 64);
        e->hex[64] = 0;
        q += 65;
        if (!num_u64(&q, &size) || *q++ != ' ' || !num_i64(&q, &mtime) || *q++ != ' ')
            return ZKS_E_DAMAGED;
        char *nl = strchr(q, '\n');
        *nl = 0;
        if (!zxf_valid(q) || !q[0]) return ZKS_E_DAMAGED;
        sha256_update(&r->h, (const uint8_t *) r->line, strlen(r->line));
        sha256_update(&r->h, (const uint8_t *) "\n", 1);
        e->size = size;
        e->mtime = mtime;
        e->path = q;
        if (r->files == ZKS_FILES_MAX || r->bytes > UINT64_MAX - size) return ZKS_E_DAMAGED;
        r->files++;
        r->bytes += size;
        return 1;
    }
    uint64_t files, bytes;
    uint8_t h[32], mine[32];
    q = r->line + 4;
    if (strncmp(r->line, "end ", 4) || !num_u64(&q, &files) || *q++ != ' ' ||
        !num_u64(&q, &bytes) || *q++ != ' ' || !unhex32(q, h) || strcmp(q + 64, "\n"))
        return ZKS_E_DAMAGED;
    sha256_final(&r->h, mine);
    if (files != r->files || bytes != r->bytes || memcmp(h, mine, 32) || fgetc(r->f) != EOF)
        return ZKS_E_DAMAGED;
    return 0;
}

/* the time, files and bytes from the first lines and the last, unchecked */
static bool snap_summary(const store_t *s, const char *space, uint32_t id, zks_snap_t *o)
{
    reader_t r;
    memset(o, 0, sizeof *o);
    o->id = id;
    if (snap_open(s, space, id, &r)) return false;
    o->time = r.time;
    char tail[256];
    bool ok = fseek(r.f, 0, SEEK_END) == 0;
    long end = ok ? ftell(r.f) : -1;
    long from = end > (long) sizeof tail - 1 ? end - (long) sizeof tail + 1 : 0;
    size_t n = 0;
    ok = ok && end > 0 && fseek(r.f, from, SEEK_SET) == 0;
    if (ok) n = fread(tail, 1, (size_t) (end - from), r.f);
    snap_close(&r);
    tail[n] = 0;
    if (!ok || n < 2 || tail[n - 1] != '\n') return false;
    tail[n - 1] = 0;
    char *last = strrchr(tail, '\n');
    const char *q = last ? last + 1 : NULL;
    uint64_t files, bytes;
    if (!q || strncmp(q, "end ", 4)) return false;
    q += 4;
    if (!num_u64(&q, &files) || *q++ != ' ' || !num_u64(&q, &bytes) || files > ZKS_FILES_MAX)
        return false;
    o->files = (uint32_t) files;
    o->bytes = bytes;
    return true;
}

/* ===================== objects (K2, K4) ===================== */

/* Hash a stored object; ZKS_E_NOTFOUND when it is missing, DAMAGED when its
 * bytes do not match its name or size. */
static int check_object(const store_t *s, const char hex[65], uint64_t size)
{
    static uint8_t buf[CHUNK];
    char p[ABS_MAX];
    if (!obj_path(s, hex, p)) return ZKS_E_NOTFOUND;
    FILE *f = os_fopen(p, "rb");
    if (!f) return ZKS_E_NOTFOUND;
    sha256_ctx_t h;
    sha256_init(&h);
    uint64_t n = 0;
    size_t k;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0) {
        sha256_update(&h, buf, k);
        n += k;
    }
    bool rd = !ferror(f);
    fclose(f);
    uint8_t got[32];
    char gh[65];
    sha256_final(&h, got);
    hex32(got, gh);
    return rd && n == size && !strcmp(gh, hex) ? ZKS_OK : ZKS_E_DAMAGED;
}

/* Copy a file of the space into the store; its hash in hex, its size in *n. */
static int store_file(const store_t *s, const char *rel, char hex[65], uint64_t *n, bool *fresh,
                      int *zxf)
{
    static uint8_t buf[ZXF_READ_MAX];
    char tmp[ABS_MAX], p[ABS_MAX], dir[ABS_MAX];
    if (!join(tmp, s->base, "objects" SEP "incoming.tmp")) return ZKS_E_IO;
    FILE *f = os_fopen(tmp, "wb");
    if (!f) return ZKS_E_IO;
    sha256_ctx_t h;
    sha256_init(&h);
    uint64_t off = 0, size = 0;
    int rc = 0;
    for (;;) {
        uint32_t got;
        rc = zxf_read(rel, off, buf, sizeof buf, false, &got, &size);
        if (rc || !got) break;
        sha256_update(&h, buf, got);
        if (fwrite(buf, 1, got, f) != got) rc = ZXF_E_IO;
        off += got;
        if (rc || off >= size) break;
    }
    bool ok = os_sync(f) == 0;
    ok = fclose(f) == 0 && ok;
    if (rc || !ok) {
        os_unlink(tmp);
        if (rc) *zxf = rc;
        return rc ? ZKS_E_FILE : ZKS_E_IO;
    }
    uint8_t d[32];
    sha256_final(&h, d);
    hex32(d, hex);
    *n = off;
    *fresh = false;
    if (!obj_path(s, hex, p)) return ZKS_E_IO;
    kind_t k = os_kind(p);
    if (k.exists && !k.link && k.size == off && check_object(s, hex, off) == ZKS_OK) {
        os_unlink(tmp); /* already kept */
        return ZKS_OK;
    }
    snprintf(dir, sizeof dir, "%s", p);
    *strrchr(dir, SEP[0]) = 0;
    if (os_mkdir(dir) || os_rename(tmp, p)) {
        os_unlink(tmp);
        return ZKS_E_IO;
    }
    *fresh = true;
    return check_object(s, hex, off); /* K4: read it back */
}

/* ===================== backup ===================== */

typedef struct {
    char *path;
    uint64_t size;
    int64_t mtime;
    char hex[65];
} prev_t;

static int prev_cmp(const void *a, const void *b)
{
    return strcmp(((const prev_t *) a)->path, ((const prev_t *) b)->path);
}

typedef struct {
    prev_t *v;
    uint32_t n;
} prevs_t;

static void prevs_free(prevs_t *p)
{
    for (uint32_t i = 0; i < p->n; i++) free(p->v[i].path);
    free(p->v);
    memset(p, 0, sizeof *p);
}

/* the newest whole snapshot's files, sorted by path; none is fine */
static void load_prev(const store_t *s, const ids_t *x, prevs_t *out)
{
    memset(out, 0, sizeof *out);
    for (uint32_t i = x->n; i-- > 0;) {
        reader_t r;
        entry_t e;
        if (snap_open(s, s->space, x->v[i], &r)) continue;
        prevs_t p = {0};
        uint32_t cap = 0;
        int rc;
        while ((rc = snap_next(&r, &e)) == 1) {
            if (p.n == cap) {
                cap = cap ? cap * 2 : 256;
                prev_t *nv = realloc(p.v, cap * sizeof *nv);
                if (!nv) {
                    rc = ZKS_E_IO;
                    break;
                }
                p.v = nv;
            }
            prev_t *q = &p.v[p.n];
            q->path = malloc(strlen(e.path) + 1);
            if (!q->path) {
                rc = ZKS_E_IO;
                break;
            }
            strcpy(q->path, e.path);
            q->size = e.size;
            q->mtime = e.mtime;
            memcpy(q->hex, e.hex, 65);
            p.n++;
        }
        snap_close(&r);
        if (rc == 0) {
            if (p.n) qsort(p.v, p.n, sizeof *p.v, prev_cmp);
            *out = p;
            return;
        }
        prevs_free(&p);
    }
}

static const prev_t *find_prev(const prevs_t *p, const char *path)
{
    prev_t key = {.path = (char *) path};
    return p->n ? bsearch(&key, p->v, p->n, sizeof *p->v, prev_cmp) : NULL;
}

typedef struct {
    const store_t *s;
    const prevs_t *prev;
    bool full;
    FILE *out;
    sha256_ctx_t h;
    zks_run_t *r;
    int *zxf;
} run_t;

static bool put(run_t *w, const char *fmt, ...)
{
    char line[LINE_CAP + 64];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t) n >= sizeof line) return false;
    sha256_update(&w->h, (const uint8_t *) line, (size_t) n);
    return fwrite(line, 1, (size_t) n, w->out) == (size_t) n;
}

static int back_file(run_t *w, const char *path, const zxf_entry_t *e)
{
    if (w->r->files == ZKS_FILES_MAX) return ZKS_E_FULL;
    const prev_t *o = find_prev(w->prev, path);
    char hex[65], p[ABS_MAX];
    uint64_t n = e->size;
    bool fresh = false;
    kind_t k = {0};
    if (!w->full && o && o->size == e->size && o->mtime == e->mtime && obj_path(w->s, o->hex, p))
        k = os_kind(p);
    if (k.exists && !k.dir && k.size == e->size)
        memcpy(hex, o->hex, 65); /* unchanged (K2) */
    else {
        int rc = store_file(w->s, path, hex, &n, &fresh, w->zxf);
        if (rc == ZKS_E_FILE && (*w->zxf == ZXF_E_NOTFOUND || *w->zxf == ZXF_E_PATH)) {
            *w->zxf = 0; /* gone, or a link, since the listing */
            w->r->skipped++;
            return ZKS_OK;
        }
        if (rc) return rc;
    }
    if (!put(w, "f %s %llu %lld %s\n", hex, (unsigned long long) n, (long long) e->mtime, path))
        return ZKS_E_IO;
    w->r->files++;
    w->r->bytes += n;
    if (fresh) {
        w->r->new_files++;
        w->r->new_bytes += n;
    }
    return ZKS_OK;
}

static int back_walk(run_t *w, const char *dir, uint32_t depth)
{
    zxf_entry_t *v;
    uint32_t n;
    int rc = zxf_list(dir, &v, &n);
    if (rc) {
        if (depth && (rc == ZXF_E_NOTFOUND || rc == ZXF_E_PATH)) return ZKS_OK; /* gone */
        *w->zxf = rc;
        return ZKS_E_FILE;
    }
    static char path[DEPTH_MAX][ZXF_PATH_MAX + 1];
    int out = ZKS_OK;
    for (uint32_t i = 0; i < n && !out; i++) {
        char *p = path[depth];
        int k = snprintf(p, ZXF_PATH_MAX + 1, "%s%s%s", dir, dir[0] ? "/" : "", v[i].name);
        if (k < 0 || (size_t) k > ZXF_PATH_MAX || (v[i].dir && depth + 1 >= DEPTH_MAX)) {
            w->r->skipped++;
            continue;
        }
        out = v[i].dir ? back_walk(w, p, depth + 1) : back_file(w, p, &v[i]);
    }
    free(v);
    return out;
}

int zks_run(const char *dest, bool full, zks_run_t *r, int *zxf, char *err, size_t cap)
{
    store_t s;
    ids_t x;
    memset(r, 0, sizeof *r);
    *zxf = 0;
    int rc = open_store(dest, true, &s, err, cap);
    if (rc) return rc;
    if (list_ids(&s, s.space, &x)) return say(err, cap, ZKS_E_IO, "could not read the snapshots");
    uint32_t id = x.n ? x.v[x.n - 1] + 1 : 1;
    if (id > ZKS_SNAPS_MAX) {
        free(x.v);
        return say(err, cap, ZKS_E_FULL, "this space has used every snapshot number");
    }
    char tmp[ABS_MAX], fin[ABS_MAX];
    if (!snap_path(&s, s.space, id, "tmp", tmp) || !snap_path(&s, s.space, id, "snap", fin)) {
        free(x.v);
        return say(err, cap, ZKS_E_DEST, "the destination's path is too long");
    }
    prevs_t prev;
    load_prev(&s, &x, &prev);
    free(x.v);
    run_t w = {
        .s = &s, .prev = &prev, .full = full, .out = os_fopen(tmp, "wb"), .r = r, .zxf = zxf};
    if (!w.out) {
        prevs_free(&prev);
        return say(err, cap, ZKS_E_IO, "could not write the snapshot");
    }
    sha256_init(&w.h);
    r->snapshot = id;
    r->time = (int64_t) time(NULL);
    rc = put(&w, MAGIC) && put(&w, "space %s\n", s.space) &&
                 put(&w, "time %lld\n", (long long) r->time)
             ? back_walk(&w, "", 0)
             : ZKS_E_IO;
    prevs_free(&prev);
    if (!rc) {
        uint8_t d[32];
        char hex[65];
        sha256_final(&w.h, d);
        hex32(d, hex);
        char line[128];
        int n = snprintf(line, sizeof line, "end %u %llu %s\n", r->files,
                         (unsigned long long) r->bytes, hex);
        if (fwrite(line, 1, (size_t) n, w.out) != (size_t) n) rc = ZKS_E_IO;
    }
    bool ok = os_sync(w.out) == 0;
    ok = fclose(w.out) == 0 && ok;
    if (!rc && (!ok || os_rename(tmp, fin))) rc = ZKS_E_IO;
    if (rc) {
        os_unlink(tmp);
        r->snapshot = 0;
        if (rc == ZKS_E_FILE) return say(err, cap, rc, "the ZXV folder: %s", zxf_strerror(*zxf));
        if (rc == ZKS_E_FULL)
            return say(err, cap, rc, "more than %u files in this space", ZKS_FILES_MAX);
        if (rc == ZKS_E_DAMAGED)
            return say(err, cap, rc, "a copy read back wrong; the destination may be failing");
        return say(err, cap, rc, "could not write to the destination");
    }
    return ZKS_OK;
}

/* ===================== list, versions, verify ===================== */

int zks_list(const char *dest, zks_snap_t *out, uint32_t cap, uint32_t *n, uint32_t *total,
             char *err, size_t errcap)
{
    store_t s;
    ids_t x;
    *n = *total = 0;
    int rc = open_store(dest, false, &s, err, errcap);
    if (rc == ZKS_E_NOTFOUND) return ZKS_OK; /* nothing yet */
    if (rc) return rc;
    if (list_ids(&s, s.space, &x))
        return say(err, errcap, ZKS_E_IO, "could not read the snapshots");
    *total = x.n;
    for (uint32_t i = x.n; i-- > 0 && *n < cap;) {
        zks_snap_t *o = &out[(*n)++];
        o->damaged = !snap_summary(&s, s.space, x.v[i], o);
    }
    free(x.v);
    return ZKS_OK;
}

int zks_versions(const char *dest, const char *path, zks_version_t *out, uint32_t cap, uint32_t *n,
                 char *err, size_t errcap)
{
    store_t s;
    ids_t x;
    *n = 0;
    if (!path || !path[0] || !zxf_valid(path))
        return say(err, errcap, ZKS_E_NOTFOUND, "not a file's path");
    int rc = open_store(dest, false, &s, err, errcap);
    if (rc == ZKS_E_NOTFOUND) return ZKS_OK;
    if (rc) return rc;
    if (list_ids(&s, s.space, &x))
        return say(err, errcap, ZKS_E_IO, "could not read the snapshots");
    for (uint32_t i = x.n; i-- > 0 && *n < cap;) {
        reader_t r;
        entry_t e;
        zks_version_t hit = {0};
        bool found = false;
        if (snap_open(&s, s.space, x.v[i], &r)) continue;
        while ((rc = snap_next(&r, &e)) == 1)
            if (!found && !strcmp(e.path, path)) {
                found = true;
                hit = (zks_version_t){x.v[i], r.time, e.size, e.mtime, {0}};
                memcpy(hit.hash, e.hex, 65);
            }
        snap_close(&r);
        if (rc || !found) continue; /* damaged, or not in it */
        bool seen = false;
        for (uint32_t k = 0; k < *n && !seen; k++) seen = !strcmp(out[k].hash, hit.hash);
        if (!seen) out[(*n)++] = hit;
    }
    free(x.v);
    return ZKS_OK;
}

/* the snapshot to use: id, or the newest when 0 */
static int pick(const store_t *s, uint32_t *id, char *err, size_t cap)
{
    ids_t x;
    if (list_ids(s, s->space, &x)) return say(err, cap, ZKS_E_IO, "could not read the snapshots");
    bool found = false;
    for (uint32_t i = 0; i < x.n; i++) found = found || x.v[i] == *id;
    if (!*id && x.n) {
        *id = x.v[x.n - 1];
        found = true;
    }
    free(x.v);
    return found ? ZKS_OK : say(err, cap, ZKS_E_NOTFOUND, "no such snapshot");
}

int zks_verify(const char *dest, uint32_t snapshot, zks_verify_t *v, char *err, size_t cap)
{
    store_t s;
    reader_t r;
    entry_t e;
    memset(v, 0, sizeof *v);
    int rc = open_store(dest, false, &s, err, cap);
    if (rc || (rc = pick(&s, &snapshot, err, cap)) != 0) return rc;
    v->snapshot = snapshot;
    if (snap_open(&s, s.space, snapshot, &r))
        return say(err, cap, ZKS_E_DAMAGED, "snapshot %u is damaged", snapshot);
    while ((rc = snap_next(&r, &e)) == 1) {
        int k = check_object(&s, e.hex, e.size);
        v->files++;
        v->bytes += e.size;
        if (k == ZKS_OK) continue;
        if (k == ZKS_E_NOTFOUND)
            v->missing++;
        else
            v->bad++;
        if (!v->first_bad[0]) snprintf(v->first_bad, sizeof v->first_bad, "%s", e.path);
    }
    snap_close(&r);
    if (rc) return say(err, cap, ZKS_E_DAMAGED, "snapshot %u is damaged", snapshot);
    return ZKS_OK;
}

/* ===================== restore (K5) ===================== */

typedef struct {
    char hex[65];
    uint64_t size;
    char *rest; /* the path below what is restored; "" for the file itself */
} want_t;

static int zfail(int *zxf, int code)
{
    *zxf = code;
    return ZKS_E_FILE;
}

static int mkdirs(const char *rel, int *zxf)
{
    char p[ZXF_PATH_MAX + 1];
    snprintf(p, sizeof p, "%s", rel);
    for (char *q = p; *q; q++) {
        if (*q != '/') continue;
        *q = 0;
        int rc = zxf_mkdir(p);
        *q = '/';
        if (rc && rc != ZXF_E_EXISTS) return zfail(zxf, rc);
    }
    int rc = zxf_mkdir(p);
    return rc && rc != ZXF_E_EXISTS ? zfail(zxf, rc) : ZKS_OK;
}

static int write_back(const store_t *s, const want_t *w, const char *rel, int *zxf)
{
    static uint8_t buf[CHUNK];
    char p[ABS_MAX];
    uint64_t size;
    if (!obj_path(s, w->hex, p)) return ZKS_E_IO;
    FILE *f = os_fopen(p, "rb");
    if (!f) return ZKS_E_NOTFOUND;
    int rc = ZXF_OK, mode = ZXF_W_BEGIN;
    size_t k;
    do {
        k = fread(buf, 1, sizeof buf, f);
        rc = zxf_write(rel, buf, (uint32_t) k, mode, true, &size);
        mode = ZXF_W_ADD;
    } while (!rc && k == sizeof buf);
    bool rd = !ferror(f);
    fclose(f);
    if (!rc) rc = zxf_write(rel, buf, 0, ZXF_W_END, true, &size);
    if (rc) return zfail(zxf, rc);
    return rd && size == w->size ? ZKS_OK : ZKS_E_IO;
}

/* K5: a free name for `path`, "name (restored).ext", "(restored N)" */
static int free_name(const char *path, bool file, char out[ZXF_PATH_MAX + 1])
{
    zxf_entry_t e;
    if (zxf_stat(path, &e) == ZXF_E_NOTFOUND) {
        snprintf(out, ZXF_PATH_MAX + 1, "%s", path);
        return ZKS_OK;
    }
    const char *slash = strrchr(path, '/'), *name = slash ? slash + 1 : path;
    const char *dot = file ? strrchr(name, '.') : NULL;
    size_t stem = dot && dot != name ? (size_t) (dot - path) : strlen(path);
    for (uint32_t i = 1; i < 1000; i++) {
        char tag[24];
        snprintf(tag, sizeof tag, i == 1 ? " (restored)" : " (restored %u)", i);
        int n = snprintf(out, ZXF_PATH_MAX + 1, "%.*s%s%s", (int) stem, path, tag, path + stem);
        if (n < 0 || n > (int) ZXF_PATH_MAX || !zxf_valid(out)) return ZKS_E_NOTFOUND;
        if (zxf_stat(out, &e) == ZXF_E_NOTFOUND) return ZKS_OK;
    }
    return ZKS_E_NOTFOUND;
}

int zks_restore(const char *dest, uint32_t snapshot, const char *path, const char *to,
                char landed[ZXF_PATH_MAX + 1], uint32_t *files, int *zxf, char *err, size_t cap)
{
    store_t s;
    reader_t r;
    entry_t e;
    *files = 0;
    *zxf = 0;
    landed[0] = 0;
    err[0] = 0;
    if (!path) path = "";
    if (!to) to = "";
    if ((path[0] && !zxf_valid(path)) || (to[0] && !zxf_valid(to)))
        return say(err, cap, ZKS_E_NOTFOUND, "not a valid path");
    int rc = open_store(dest, false, &s, err, cap);
    if (rc || (rc = pick(&s, &snapshot, err, cap)) != 0) return rc;
    if (snap_open(&s, s.space, snapshot, &r))
        return say(err, cap, ZKS_E_DAMAGED, "snapshot %u is damaged", snapshot);
    want_t *v = NULL;
    uint32_t n = 0, vcap = 0;
    bool single = false;
    size_t pl = strlen(path);
    while ((rc = snap_next(&r, &e)) == 1) {
        bool same = pl && !strcmp(e.path, path);
        if (!same && pl && (strncmp(e.path, path, pl) || e.path[pl] != '/')) continue;
        if (n == vcap) {
            vcap = vcap ? vcap * 2 : 64;
            want_t *nv = realloc(v, vcap * sizeof *nv);
            if (!nv) {
                rc = ZKS_E_IO;
                break;
            }
            v = nv;
        }
        const char *rest = same ? "" : e.path + (pl ? pl + 1 : 0);
        v[n].rest = malloc(strlen(rest) + 1);
        if (!v[n].rest) {
            rc = ZKS_E_IO;
            break;
        }
        strcpy(v[n].rest, rest);
        memcpy(v[n].hex, e.hex, 65);
        v[n].size = e.size;
        single = single || same;
        n++;
    }
    snap_close(&r);
    if (rc == ZKS_E_IO) {
        say(err, cap, rc, "out of memory");
    } else if (rc) {
        rc = say(err, cap, ZKS_E_DAMAGED, "snapshot %u is damaged", snapshot);
    } else if (!n) {
        rc = say(err, cap, ZKS_E_NOTFOUND, "\"%s\" is not in snapshot %u", path, snapshot);
    }
    /* every object is checked before anything is written */
    for (uint32_t i = 0; i < n && !rc; i++)
        if (check_object(&s, v[i].hex, v[i].size))
            rc = say(err, cap, ZKS_E_DAMAGED, "a kept copy of \"%s%s%s\" is missing or damaged",
                     path, pl && v[i].rest[0] ? "/" : "", v[i].rest);
    if (!rc) {
        char def[ZXF_PATH_MAX + 1];
        if (pl)
            snprintf(def, sizeof def, "%s", path);
        else
            snprintf(def, sizeof def, "Keepsake %u", snapshot);
        zxf_entry_t st;
        if (to[0]) {
            int k = zxf_stat(to, &st);
            if (k != ZXF_E_NOTFOUND)
                rc = zfail(zxf, k ? k : ZXF_E_EXISTS);
            else
                snprintf(landed, ZXF_PATH_MAX + 1, "%s", to);
        } else if (free_name(def, single, landed))
            rc = say(err, cap, ZKS_E_NOTFOUND, "no free name beside \"%s\"", def);
    }
    if (!rc) {
        /* its folder, if that went too */
        char parent[ZXF_PATH_MAX + 1];
        snprintf(parent, sizeof parent, "%s", landed);
        char *sl = strrchr(parent, '/');
        if (sl) {
            *sl = 0;
            rc = mkdirs(parent, zxf);
        }
    }
    if (!rc && !single) rc = mkdirs(landed, zxf);
    for (uint32_t i = 0; i < n && !rc; i++) {
        char rel[ZXF_PATH_MAX + 1];
        int k = snprintf(rel, sizeof rel, "%s%s%s", landed, v[i].rest[0] ? "/" : "", v[i].rest);
        if (k < 0 || (size_t) k > ZXF_PATH_MAX) {
            rc = zfail(zxf, ZXF_E_PATH);
            break;
        }
        char *sl = strrchr(rel, '/');
        if (sl && v[i].rest[0] && strchr(v[i].rest, '/')) {
            *sl = 0;
            rc = mkdirs(rel, zxf);
            *sl = '/';
        }
        if (!rc) rc = write_back(&s, &v[i], rel, zxf);
        if (!rc) (*files)++;
    }
    for (uint32_t i = 0; i < n; i++) free(v[i].rest);
    free(v);
    if (rc == ZKS_E_FILE) return say(err, cap, rc, "the ZXV folder: %s", zxf_strerror(*zxf));
    if (rc && !err[0]) return say(err, cap, rc, "could not read a kept copy");
    return rc;
}

/* ===================== prune (K7) ===================== */

typedef struct {
    uint8_t (*h)[32];
    uint32_t n, cap;
} hashes_t;

static int h_cmp(const void *a, const void *b)
{
    return memcmp(a, b, 32);
}

static void h_tidy(hashes_t *x)
{
    if (!x->n) return;
    qsort(x->h, x->n, 32, h_cmp);
    uint32_t w = 1;
    for (uint32_t i = 1; i < x->n; i++)
        if (memcmp(x->h[i], x->h[w - 1], 32)) memcpy(x->h[w++], x->h[i], 32);
    x->n = w;
}

static bool h_add(hashes_t *x, const uint8_t h[32])
{
    if (x->n == x->cap) {
        h_tidy(x);
        if (x->n * 2 >= x->cap) {
            uint32_t nc = x->cap ? x->cap * 2 : 1024;
            void *nv = realloc(x->h, (size_t) nc * 32);
            if (!nv) return false;
            x->h = nv;
            x->cap = nc;
        }
    }
    memcpy(x->h[x->n++], h, 32);
    return true;
}

typedef struct {
    const store_t *s;
    hashes_t *keep;
    const char *space;
    uint32_t drop_below; /* this space's snapshots below it go */
    int rc;
} sweep_t;

static int keep_space(void *ctx, const char *space)
{
    sweep_t *w = ctx;
    ids_t x;
    if (!zxf_space_id_ok(space) && strcmp(space, "home")) return 0;
    if (list_ids(w->s, space, &x)) return w->rc = ZKS_E_IO;
    bool mine = !strcmp(space, w->space);
    for (uint32_t i = 0; i < x.n && !w->rc; i++) {
        if (mine && x.v[i] < w->drop_below) continue;
        reader_t r;
        entry_t e;
        int rc = snap_open(w->s, space, x.v[i], &r);
        while (!rc && (rc = snap_next(&r, &e)) == 1) rc = h_add(w->keep, e.hash) ? 0 : ZKS_E_IO;
        snap_close(&r);
        if (rc) w->rc = rc == ZKS_E_IO ? ZKS_E_IO : ZKS_E_DAMAGED;
    }
    free(x.v);
    return w->rc;
}

typedef struct {
    const hashes_t *keep;
    char dir[ABS_MAX];
    uint32_t removed;
    uint64_t freed;
} gc_t;

static int gc_file(void *ctx, const char *name)
{
    gc_t *g = ctx;
    uint8_t h[32];
    char p[ABS_MAX];
    if (strlen(name) != 64 || !unhex32(name, h) || !join(p, g->dir, name)) return 0;
    if (g->keep->n && bsearch(h, g->keep->h, g->keep->n, 32, h_cmp)) return 0;
    kind_t k = os_kind(p);
    if (!k.exists || k.dir || os_unlink(p)) return 0;
    g->removed++;
    g->freed += k.size;
    return 0;
}

static int gc_dir(void *ctx, const char *name)
{
    gc_t *g = ctx;
    char objects[ABS_MAX];
    snprintf(objects, sizeof objects, "%s", g->dir);
    bool fan = strlen(name) == 2 && strspn(name, "0123456789abcdef") == 2;
    if (!fan || !join(g->dir, objects, name) || !os_kind(g->dir).dir) {
        snprintf(g->dir, sizeof g->dir, "%s", objects);
        return 0;
    }
    os_each(g->dir, gc_file, g);
    os_rmdir(g->dir); /* only when empty */
    snprintf(g->dir, sizeof g->dir, "%s", objects);
    return 0;
}

int zks_prune(const char *dest, uint32_t keep, uint32_t *snaps, uint32_t *objects, uint64_t *freed,
              char *err, size_t cap)
{
    store_t s;
    ids_t x;
    *snaps = *objects = 0;
    *freed = 0;
    if (!keep) keep = 1;
    int rc = open_store(dest, false, &s, err, cap);
    if (rc == ZKS_E_NOTFOUND) return ZKS_OK;
    if (rc) return rc;
    if (list_ids(&s, s.space, &x)) return say(err, cap, ZKS_E_IO, "could not read the snapshots");
    uint32_t drop = x.n > keep ? x.n - keep : 0;
    hashes_t kept = {0};
    char spaces[ABS_MAX];
    sweep_t w = {&s, &kept, s.space, drop ? x.v[drop] : 0, 0};
    /* what every remaining snapshot names, read before anything goes */
    if (!join(spaces, s.base, "spaces") || os_each(spaces, keep_space, &w) || w.rc) {
        free(x.v);
        free(kept.h);
        if (w.rc == ZKS_E_DAMAGED)
            return say(err, cap, ZKS_E_DAMAGED,
                       "a snapshot is damaged, so nothing was removed (run a check)");
        return say(err, cap, ZKS_E_IO, "could not read the snapshots");
    }
    h_tidy(&kept);
    for (uint32_t i = 0; i < drop; i++) {
        char p[ABS_MAX];
        if (snap_path(&s, s.space, x.v[i], "snap", p) && !os_unlink(p)) (*snaps)++;
    }
    free(x.v);
    gc_t g = {&kept, {0}, 0, 0};
    char tmp[ABS_MAX];
    if (join(g.dir, s.base, "objects")) {
        os_each(g.dir, gc_dir, &g);
        if (join(tmp, g.dir, "incoming.tmp")) os_unlink(tmp);
    }
    free(kept.h);
    *objects = g.removed;
    *freed = g.freed;
    return ZKS_OK;
}
