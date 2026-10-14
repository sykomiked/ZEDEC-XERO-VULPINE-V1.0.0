/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_files.c — the ZXV home folder (see zxv_files.h F1-F6). */
#if !defined(_WIN32)
#    define _DEFAULT_SOURCE
#    define _DARWIN_C_SOURCE
#    include <sys/types.h>
#    include <sys/stat.h>
#    include <dirent.h>
#    include <unistd.h>
#    include <fcntl.h>
#else
#    include <windows.h>
#    include <io.h>
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_files.h"

#define STAGE   ".zxv-staging"
#define ABS_MAX 4096u

static char ROOT[ABS_MAX];
static char SPACE[ZXF_SPACE_ID_MAX + 1]; /* F7: "" = home */
static char BASE[ABS_MAX];               /* ROOT, or the space's folder */

/* ===================== the operating system ===================== */

typedef struct {
    bool exists, dir, link;
    uint64_t size;
    int64_t mtime;
} kind_t;

typedef int (*each_fn)(void *ctx, const char *name);

#if defined(_WIN32)

/* UTF-8 -> UTF-16, with '/' turned into '\' (both are separators to Windows,
 * but the extended-length forms want '\'). Two slots, for rename. */
static const wchar_t *W(int slot, const char *p)
{
    static wchar_t w[2][ABS_MAX];
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, p, -1, w[slot], (int) ABS_MAX);
    if (n <= 0) return NULL;
    for (wchar_t *q = w[slot]; *q; q++)
        if (*q == L'/') *q = L'\\';
    return w[slot];
}

static int64_t ft_unix(FILETIME f)
{
    uint64_t t = ((uint64_t) f.dwHighDateTime << 32) | f.dwLowDateTime;
    return (int64_t) (t / 10000000ull) - 11644473600ll;
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
    k.mtime = ft_unix(a.ftLastWriteTime);
    return k;
}

static int os_mkdir(const char *p)
{
    const wchar_t *w = W(0, p);
    if (w && CreateDirectoryW(w, NULL)) return 0;
    return w && GetLastError() == ERROR_ALREADY_EXISTS ? ZXF_E_EXISTS : ZXF_E_IO;
}

static int os_rename(const char *from, const char *to, bool replace)
{
    const wchar_t *a = W(0, from), *b = W(1, to);
    if (!a || !b) return ZXF_E_PATH;
    if (MoveFileExW(a, b, replace ? MOVEFILE_REPLACE_EXISTING : 0)) return 0;
    DWORD e = GetLastError();
    return e == ERROR_ALREADY_EXISTS || e == ERROR_FILE_EXISTS ? ZXF_E_EXISTS : ZXF_E_IO;
}

static int os_unlink(const char *p)
{
    const wchar_t *w = W(0, p);
    return w && DeleteFileW(w) ? 0 : ZXF_E_IO;
}

static int os_rmdir(const char *p)
{
    const wchar_t *w = W(0, p);
    return w && RemoveDirectoryW(w) ? 0 : ZXF_E_IO;
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
    if ((size_t) snprintf(pat, sizeof pat, "%s\\*", dir) >= sizeof pat) return ZXF_E_PATH;
    WIN32_FIND_DATAW d;
    const wchar_t *w = W(0, pat);
    HANDLE h = w ? FindFirstFileW(w, &d) : INVALID_HANDLE_VALUE;
    if (h == INVALID_HANDLE_VALUE) return ZXF_E_IO;
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

static const char *home_docs(char *out, size_t cap)
{
    const char *up = getenv("USERPROFILE");
    if (!up || !up[0]) return NULL;
    snprintf(out, cap, "%s\\Documents", up);
    if (!os_kind(out).dir) snprintf(out, cap, "%s", up);
    return out;
}
#    define SEP "\\"

#else /* POSIX */

static kind_t os_kind(const char *p)
{
    kind_t k = {0};
    struct stat st;
    if (lstat(p, &st) != 0) return k;
    k.exists = true;
    k.link = S_ISLNK(st.st_mode);
    k.dir = S_ISDIR(st.st_mode);
    k.size = S_ISREG(st.st_mode) ? (uint64_t) st.st_size : 0;
    k.mtime = (int64_t) st.st_mtime;
    return k;
}

static int os_mkdir(const char *p)
{
    if (mkdir(p, S_IRWXU) == 0) return 0;
    return errno == EEXIST ? ZXF_E_EXISTS : ZXF_E_IO;
}

static int os_rename(const char *from, const char *to, bool replace)
{
    /* the owner's own folder: a check before the rename is enough here */
    if (!replace && os_kind(to).exists) return ZXF_E_EXISTS;
    return rename(from, to) == 0 ? 0 : ZXF_E_IO;
}

static int os_unlink(const char *p)
{
    return unlink(p) == 0 ? 0 : ZXF_E_IO;
}

static int os_rmdir(const char *p)
{
    return rmdir(p) == 0 ? 0 : ZXF_E_IO;
}

static FILE *os_fopen(const char *p, const char *mode)
{
    /* never through a link planted at the target's name (F3) */
    if (mode[0] == 'r') {
        int fd = open(p, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        return fd < 0 ? NULL : fdopen(fd, "rb");
    }
    int fl = O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC | (mode[0] == 'a' ? O_APPEND : O_TRUNC);
    int fd = open(p, fl, S_IRUSR | S_IWUSR);
    if (fd < 0) return NULL;
    FILE *f = fdopen(fd, mode[0] == 'a' ? "ab" : "wb");
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
    if (!d) return ZXF_E_IO;
    struct dirent *e;
    int rc = 0;
    while (rc == 0 && (e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        rc = fn(ctx, e->d_name);
    }
    closedir(d);
    return rc;
}

static const char *home_docs(char *out, size_t cap)
{
    const char *home = getenv("HOME");
    if (!home || home[0] != '/') return NULL;
    snprintf(out, cap, "%s/Documents", home);
    if (!os_kind(out).dir) snprintf(out, cap, "%s", home);
    return out;
}
#    define SEP "/"
#endif

/* ===================== paths (F2, F3) ===================== */

static bool utf8_ok(const uint8_t *s, size_t n, bool allow_ctl)
{
    for (size_t i = 0; i < n;) {
        uint8_t c = s[i];
        if (c < 0x80) {
            if (!allow_ctl && (c < 0x20 || c == 0x7f)) return false;
            if (allow_ctl && c == 0) return false;
            i++;
            continue;
        }
        size_t k = c >= 0xc2 && c <= 0xdf   ? 1
                   : c >= 0xe0 && c <= 0xef ? 2
                   : c >= 0xf0 && c <= 0xf4 ? 3
                                            : 0;
        if (!k) return false;
        uint32_t cp = c & (k == 1 ? 0x1f : k == 2 ? 0x0f : 0x07);
        for (size_t j = 1; j <= k; j++) {
            if (i + j >= n || (s[i + j] & 0xc0) != 0x80) return false;
            cp = cp << 6 | (s[i + j] & 0x3f);
        }
        if ((k == 2 && cp < 0x800) || (k == 3 && (cp < 0x10000 || cp > 0x10ffff)) ||
            (cp >= 0xd800 && cp <= 0xdfff))
            return false;
        i += k + 1;
    }
    return true;
}

static bool seg_ok(const char *s, size_t n, bool first)
{
    if (n == 0 || n > ZXF_NAME_MAX) return false;
    if (first && n == sizeof ZXF_TRASH - 1 && !memcmp(s, ZXF_TRASH, n)) return true;
    if (s[0] == '.' || s[n - 1] == '.' || s[n - 1] == ' ') return false;
    for (size_t i = 0; i < n; i++)
        if (s[i] == '/' || s[i] == '\\' || s[i] == ':') return false;
    return utf8_ok((const uint8_t *) s, n, false);
}

bool zxf_valid(const char *rel)
{
    size_t n = strlen(rel);
    if (n == 0) return true;
    if (n > ZXF_PATH_MAX) return false;
    const char *s = rel;
    for (bool first = true;; first = false) {
        const char *e = strchr(s, '/');
        size_t l = e ? (size_t) (e - s) : strlen(s);
        if (!seg_ok(s, l, first)) return false;
        if (!e) return true;
        s = e + 1;
    }
}

static bool in_trash(const char *rel)
{
    size_t t = sizeof ZXF_TRASH - 1;
    return !strncmp(rel, ZXF_TRASH, t) && (rel[t] == 0 || rel[t] == '/');
}

static int make_root(void)
{
    if (ROOT[0]) {
        kind_t k = os_kind(ROOT);
        if (k.exists) return k.dir ? 0 : ZXF_E_ROOT;
    } else {
        const char *env = getenv("ZXV_FILES_ROOT");
        char docs[ABS_MAX];
        if (env && env[0]) {
            if (strlen(env) >= sizeof ROOT) return ZXF_E_ROOT;
            snprintf(ROOT, sizeof ROOT, "%s", env);
        } else if (home_docs(docs, sizeof docs)) {
            if ((size_t) snprintf(ROOT, sizeof ROOT, "%s" SEP "ZXV", docs) >= sizeof ROOT) {
                ROOT[0] = 0;
                return ZXF_E_ROOT;
            }
        } else {
            return ZXF_E_ROOT;
        }
        kind_t k = os_kind(ROOT);
        if (k.exists) return k.dir ? 0 : ZXF_E_ROOT;
    }
    int rc = os_mkdir(ROOT);
    return rc == 0 || rc == ZXF_E_EXISTS ? 0 : ZXF_E_ROOT;
}

/* A folder under the root that only this module names: made when missing,
 * refused when it is a link or not a folder (F3). */
static int own_dir(char *abs, size_t cap, const char *a, const char *b)
{
    if ((size_t) snprintf(abs, cap, "%s" SEP "%s%s%s", ROOT, a, b ? SEP : "", b ? b : "") >= cap)
        return ZXF_E_PATH;
    kind_t k = os_kind(abs);
    if (k.link || (k.exists && !k.dir)) return ZXF_E_PATH;
    if (!k.exists && os_mkdir(abs) != 0) return ZXF_E_IO;
    return 0;
}

/* F7: BASE = the folder the paths are relative to now. */
static int make_base(void)
{
    int rc = make_root();
    if (rc) return rc;
    if (!SPACE[0]) {
        snprintf(BASE, sizeof BASE, "%s", ROOT);
        return 0;
    }
    char sp[ABS_MAX];
    if ((rc = own_dir(sp, sizeof sp, ZXF_SPACES, NULL)) != 0) return rc;
    return own_dir(BASE, sizeof BASE, ZXF_SPACES, SPACE);
}

bool zxf_space_id_ok(const char *id)
{
    size_t n = strlen(id);
    if (n == 0 || n > ZXF_SPACE_ID_MAX || id[0] == '-' || id[n - 1] == '-') return false;
    for (size_t i = 0; i < n; i++)
        if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9') || id[i] == '-'))
            return false;
    return strcmp(id, "home") != 0;
}

int zxf_use_space(const char *id)
{
    if (!id || !id[0] || !strcmp(id, "home")) {
        SPACE[0] = 0;
        return 0;
    }
    if (!zxf_space_id_ok(id)) return ZXF_E_PATH;
    snprintf(SPACE, sizeof SPACE, "%s", id);
    return 0;
}

const char *zxf_space(void)
{
    return SPACE[0] ? SPACE : "home";
}

/* ----- F7 manifests: <root>/.spaces/<id>.space, written whole ----- */

static int meta_path(const char *id, const char *ext, char *abs, size_t cap)
{
    char sp[ABS_MAX];
    if (!zxf_space_id_ok(id)) return ZXF_E_PATH;
    int rc = make_root();
    if (rc || (rc = own_dir(sp, sizeof sp, ZXF_SPACES, NULL)) != 0) return rc;
    if ((size_t) snprintf(abs, cap, "%s" SEP "%s%s", sp, id, ext) >= cap) return ZXF_E_PATH;
    kind_t k = os_kind(abs);
    return k.link || k.dir ? ZXF_E_PATH : 0;
}

int zxf_meta_read(const char *id, char *buf, uint32_t cap, uint32_t *n)
{
    char abs[ABS_MAX];
    *n = 0;
    int rc = meta_path(id, ".space", abs, sizeof abs);
    if (rc) return rc;
    kind_t k = os_kind(abs);
    if (!k.exists) return ZXF_E_NOTFOUND;
    if (k.size >= cap) return ZXF_E_TOOBIG;
    FILE *f = os_fopen(abs, "rb");
    if (!f) return ZXF_E_IO;
    size_t r = fread(buf, 1, cap - 1, f);
    bool err = ferror(f) != 0;
    fclose(f);
    if (err) return ZXF_E_IO;
    buf[r] = 0;
    *n = (uint32_t) r;
    return 0;
}

int zxf_meta_write(const char *id, const char *buf, uint32_t n, bool exclusive)
{
    char abs[ABS_MAX], tmp[ABS_MAX];
    int rc = meta_path(id, ".space", abs, sizeof abs);
    if (rc || (rc = meta_path(id, ".space.tmp", tmp, sizeof tmp)) != 0) return rc;
    if (exclusive && os_kind(abs).exists) return ZXF_E_EXISTS;
    FILE *f = os_fopen(tmp, "wb");
    if (!f) return ZXF_E_IO;
    bool ok = (n == 0 || fwrite(buf, 1, n, f) == n) && os_sync(f) == 0;
    ok = (fclose(f) == 0) && ok;
    if (!ok || (rc = os_rename(tmp, abs, true)) != 0) {
        os_unlink(tmp);
        return ok ? rc : ZXF_E_IO;
    }
    return 0;
}

typedef struct {
    zxf_meta_fn fn;
    void *ctx;
} meta_each_t;

static int meta_one(void *ctx, const char *name)
{
    meta_each_t *m = ctx;
    size_t n = strlen(name), e = sizeof ".space" - 1;
    char id[ZXF_SPACE_ID_MAX + 1];
    if (n <= e || n - e > ZXF_SPACE_ID_MAX || strcmp(name + n - e, ".space")) return 0;
    memcpy(id, name, n - e);
    id[n - e] = 0;
    return zxf_space_id_ok(id) ? m->fn(m->ctx, id) : 0;
}

int zxf_meta_each(zxf_meta_fn fn, void *ctx)
{
    char sp[ABS_MAX];
    int rc = make_root();
    if (rc || (rc = own_dir(sp, sizeof sp, ZXF_SPACES, NULL)) != 0) return rc;
    meta_each_t m = {fn, ctx};
    return os_each(sp, meta_one, &m);
}

int zxf_set_root(const char *abs)
{
    if (!abs || !abs[0] || strlen(abs) >= sizeof ROOT) return ZXF_E_ROOT;
    snprintf(ROOT, sizeof ROOT, "%s", abs);
    return make_root();
}

int zxf_root(char *out, size_t cap)
{
    int rc = make_root();
    if (rc) return rc;
    if ((size_t) snprintf(out, cap, "%s", ROOT) >= cap) return ZXF_E_ROOT;
    return 0;
}

/* The absolute path of `rel` (or of a name inside the root's own folders),
 * after F2 and F3: no link on any segment, and every segment but the last a
 * folder. *k: what is at the full path. */
static int resolve(const char *rel, char *abs, size_t cap, kind_t *k)
{
    int rc = make_base();
    if (rc) return rc;
    if (!zxf_valid(rel)) return ZXF_E_PATH;
    size_t at = (size_t) snprintf(abs, cap, "%s", BASE);
    if (at >= cap || at + 1 + strlen(rel) >= cap) return ZXF_E_PATH;
    kind_t kk = os_kind(abs);
    for (const char *s = rel; *s;) {
        const char *e = strchr(s, '/');
        size_t l = e ? (size_t) (e - s) : strlen(s);
        abs[at++] = SEP[0];
        memcpy(abs + at, s, l);
        at += l;
        abs[at] = 0;
        kk = os_kind(abs);
        if (kk.link) return ZXF_E_PATH;
        if (e && kk.exists && !kk.dir) return ZXF_E_NOTDIR;
        if (e && !kk.exists) {
            /* the rest cannot exist: finish the name for the caller */
            snprintf(abs + at, cap - at, "%s", e);
#if defined(_WIN32)
            for (char *q = abs + at; *q; q++)
                if (*q == '/') *q = '\\';
#endif
            kk.exists = false;
            if (k) *k = kk;
            return 0;
        }
        s = e ? e + 1 : s + l;
    }
    if (k) *k = kk;
    return 0;
}

/* The folder holding `rel` exists and is a folder. */
static int parent_ok(const char *rel)
{
    const char *sl = strrchr(rel, '/');
    if (!sl) return 0;
    char par[ZXF_PATH_MAX + 1], abs[ABS_MAX];
    memcpy(par, rel, (size_t) (sl - rel));
    par[sl - rel] = 0;
    kind_t k;
    int rc = resolve(par, abs, sizeof abs, &k);
    if (rc) return rc;
    if (!k.exists) return ZXF_E_NOTFOUND;
    return k.dir ? 0 : ZXF_E_NOTDIR;
}

static int special_dir(const char *name, char *abs, size_t cap)
{
    int rc = make_base();
    if (rc) return rc;
    if ((size_t) snprintf(abs, cap, "%s" SEP "%s", BASE, name) >= cap) return ZXF_E_PATH;
    kind_t k = os_kind(abs);
    if (k.link || (k.exists && !k.dir)) return ZXF_E_PATH;
    if (!k.exists && os_mkdir(abs) != 0) return ZXF_E_IO;
    return 0;
}

/* ===================== listing ===================== */

typedef struct {
    const char *dir;
    zxf_entry_t *v;
    uint32_t n, cap;
    int err;
} list_t;

static int list_one(void *ctx, const char *name)
{
    list_t *L = ctx;
    if (name[0] == '.') return 0;
    if (L->n == ZXF_LIST_MAX) return ZXF_E_TOOBIG;
    if (strlen(name) > ZXF_NAME_MAX) return 0;
    char abs[ABS_MAX];
    if ((size_t) snprintf(abs, sizeof abs, "%s" SEP "%s", L->dir, name) >= sizeof abs) return 0;
    kind_t k = os_kind(abs);
    if (!k.exists || k.link) return 0; /* links are not shown (F3) */
    if (L->n == L->cap) {
        uint32_t nc = L->cap ? L->cap * 2 : 32;
        zxf_entry_t *nv = realloc(L->v, nc * sizeof *nv);
        if (!nv) return ZXF_E_IO;
        L->v = nv;
        L->cap = nc;
    }
    zxf_entry_t *e = &L->v[L->n++];
    snprintf(e->name, sizeof e->name, "%s", name);
    e->dir = k.dir;
    e->size = k.size;
    e->mtime = k.mtime;
    return 0;
}

static int by_kind_name(const void *a, const void *b)
{
    const zxf_entry_t *x = a, *y = b;
    if (x->dir != y->dir) return x->dir ? -1 : 1;
    return strcmp(x->name, y->name);
}

int zxf_list(const char *rel, zxf_entry_t **out, uint32_t *n)
{
    char abs[ABS_MAX];
    kind_t k;
    *out = NULL;
    *n = 0;
    if (!strcmp(rel, ZXF_TRASH)) {
        int rc = special_dir(ZXF_TRASH, abs, sizeof abs);
        if (rc) return rc;
    } else {
        int rc = resolve(rel, abs, sizeof abs, &k);
        if (rc) return rc;
        if (!k.exists) return ZXF_E_NOTFOUND;
        if (!k.dir) return ZXF_E_NOTDIR;
    }
    list_t L = {abs, NULL, 0, 0, 0};
    int rc = os_each(abs, list_one, &L);
    if (rc) {
        free(L.v);
        return rc;
    }
    if (L.n) qsort(L.v, L.n, sizeof *L.v, by_kind_name);
    *out = L.v;
    *n = L.n;
    return 0;
}

int zxf_stat(const char *rel, zxf_entry_t *e)
{
    char abs[ABS_MAX];
    kind_t k;
    int rc = resolve(rel, abs, sizeof abs, &k);
    if (rc) return rc;
    if (!k.exists) return ZXF_E_NOTFOUND;
    const char *sl = strrchr(rel, '/');
    snprintf(e->name, sizeof e->name, "%s", sl ? sl + 1 : rel);
    e->dir = k.dir;
    e->size = k.size;
    e->mtime = k.mtime;
    return 0;
}

/* ===================== reading and writing (F4, F6) ===================== */

int zxf_read(const char *rel, uint64_t off, uint8_t *buf, uint32_t cap, bool text, uint32_t *got,
             uint64_t *size)
{
    char abs[ABS_MAX];
    kind_t k;
    *got = 0;
    *size = 0;
    int rc = resolve(rel, abs, sizeof abs, &k);
    if (rc) return rc;
    if (!k.exists) return ZXF_E_NOTFOUND;
    if (k.dir) return ZXF_E_ISDIR;
    *size = k.size;
    if (cap > ZXF_READ_MAX) cap = ZXF_READ_MAX;
    if (off >= k.size) return 0;
    FILE *f = os_fopen(abs, "rb");
    if (!f) return ZXF_E_IO;
#if defined(_WIN32)
    int sk = _fseeki64(f, (long long) off, SEEK_SET);
#else
    int sk = fseeko(f, (off_t) off, SEEK_SET);
#endif
    size_t r = sk == 0 ? fread(buf, 1, cap, f) : 0;
    bool err = sk != 0 || ferror(f);
    fclose(f);
    if (err) return ZXF_E_IO;
    if (text) {
        bool eof = off + r >= k.size;
        /* step back over a character cut at the end of the chunk */
        size_t end = r;
        if (!eof) {
            size_t back = 0;
            while (back < 4 && back < end && (buf[end - 1 - back] & 0xc0) == 0x80) back++;
            if (back < end && buf[end - 1 - back] >= 0xc0) {
                uint8_t c = buf[end - 1 - back];
                size_t need = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : 2;
                if (back + 1 < need) end -= back + 1;
            }
            if (end == 0 && r) return ZXF_E_BINARY;
        }
        if (!utf8_ok(buf, end, true)) return ZXF_E_BINARY;
        r = end;
    }
    *got = (uint32_t) r;
    return 0;
}

static uint64_t fnv(const char *s)
{
    uint64_t h = 1469598103934665603ull;
    for (; *s; s++) h = (h ^ (uint8_t) *s) * 1099511628211ull;
    return h;
}

int zxf_write(const char *rel, const uint8_t *d, uint32_t n, int mode, bool exclusive,
              uint64_t *size)
{
    char abs[ABS_MAX], st[ABS_MAX];
    kind_t k;
    *size = 0;
    if (!rel[0] || in_trash(rel)) return ZXF_E_PATH;
    int rc = resolve(rel, abs, sizeof abs, &k);
    if (rc) return rc;
    if (k.exists && k.dir) return ZXF_E_ISDIR;
    if (exclusive && k.exists) return ZXF_E_EXISTS;
    if ((rc = parent_ok(rel)) != 0) return rc;
    if ((rc = special_dir(STAGE, st, sizeof st)) != 0) return rc;
    size_t sl = strlen(st);
    if (snprintf(st + sl, sizeof st - sl, SEP "%016llx.%s", (unsigned long long) fnv(rel),
                 mode == ZXF_W_REPLACE ? "tmp" : "part") < 0)
        return ZXF_E_IO;
    kind_t sk = os_kind(st);
    if (sk.link || sk.dir) return ZXF_E_PATH;
    if ((mode == ZXF_W_ADD || mode == ZXF_W_END) && !sk.exists) return ZXF_E_STAGE;
    uint64_t have = mode == ZXF_W_ADD || mode == ZXF_W_END ? sk.size : 0;
    if (have + n > ZXF_FILE_MAX) return ZXF_E_TOOBIG;
    FILE *f = os_fopen(st, mode == ZXF_W_ADD || mode == ZXF_W_END ? "ab" : "wb");
    if (!f) return ZXF_E_IO;
    bool ok = (n == 0 || fwrite(d, 1, n, f) == n);
    if (mode == ZXF_W_REPLACE || mode == ZXF_W_END) ok = ok && os_sync(f) == 0;
    ok = (fclose(f) == 0) && ok;
    if (!ok) {
        os_unlink(st);
        return ZXF_E_IO;
    }
    *size = have + n;
    if (mode == ZXF_W_BEGIN || mode == ZXF_W_ADD) return 0;
    /* the target may have appeared while the parts gathered */
    if (exclusive && os_kind(abs).exists) {
        os_unlink(st);
        return ZXF_E_EXISTS;
    }
    if ((rc = os_rename(st, abs, true)) != 0) {
        os_unlink(st);
        return rc;
    }
    return 0;
}

int zxf_mkdir(const char *rel)
{
    char abs[ABS_MAX];
    kind_t k;
    if (!rel[0] || in_trash(rel)) return ZXF_E_PATH;
    int rc = resolve(rel, abs, sizeof abs, &k);
    if (rc) return rc;
    if (k.exists) return ZXF_E_EXISTS;
    if ((rc = parent_ok(rel)) != 0) return rc;
    return os_mkdir(abs);
}

int zxf_rename(const char *from, const char *to)
{
    char a[ABS_MAX], b[ABS_MAX];
    kind_t ka, kb;
    if (!from[0] || !to[0] || !strcmp(from, ZXF_TRASH) || !strcmp(to, ZXF_TRASH)) return ZXF_E_PATH;
    if (in_trash(to) && !in_trash(from)) return ZXF_E_PATH; /* zxf_trash puts things there */
    int rc = resolve(from, a, sizeof a, &ka);
    if (rc) return rc;
    if (!ka.exists) return ZXF_E_NOTFOUND;
    if ((rc = resolve(to, b, sizeof b, &kb)) != 0) return rc;
    if (kb.exists) return ZXF_E_EXISTS;
    if ((rc = parent_ok(to)) != 0) return rc;
    /* a folder cannot go inside itself */
    size_t fl = strlen(from);
    if (ka.dir && !strncmp(to, from, fl) && to[fl] == '/') return ZXF_E_PATH;
    return os_rename(a, b, false);
}

typedef struct {
    const char *dir;
    int depth;
} tree_t;

static int remove_tree(const char *abs, int depth);

static int remove_child(void *ctx, const char *name)
{
    tree_t *t = ctx;
    char p[ABS_MAX];
    if ((size_t) snprintf(p, sizeof p, "%s" SEP "%s", t->dir, name) >= sizeof p) return ZXF_E_PATH;
    return remove_tree(p, t->depth + 1);
}

/* Remove a file, a link (never what it points at) or a folder and all in it. */
static int remove_tree(const char *abs, int depth)
{
    if (depth > 64) return ZXF_E_DEPTH;
    kind_t k = os_kind(abs);
    if (!k.exists) return 0;
    if (k.dir && !k.link) {
        tree_t t = {abs, depth};
        int rc = os_each(abs, remove_child, &t);
        if (rc) return rc;
        return os_rmdir(abs);
    }
#if defined(_WIN32)
    if (k.dir) return os_rmdir(abs); /* a directory junction */
#endif
    return os_unlink(abs);
}

int zxf_trash(const char *rel, char *trashed, size_t cap)
{
    char abs[ABS_MAX], tr[ABS_MAX], dst[ABS_MAX];
    kind_t k;
    if (trashed && cap) trashed[0] = 0;
    if (!rel[0] || !strcmp(rel, ZXF_TRASH)) return ZXF_E_PATH;
    int rc = resolve(rel, abs, sizeof abs, &k);
    if (rc) return rc;
    if (!k.exists) return ZXF_E_NOTFOUND;
    if (in_trash(rel)) return remove_tree(abs, 0);
    if ((rc = special_dir(ZXF_TRASH, tr, sizeof tr)) != 0) return rc;
    const char *sl = strrchr(rel, '/');
    const char *name = sl ? sl + 1 : rel;
    const char *dot = k.dir ? NULL : strrchr(name, '.');
    size_t stem = dot && dot != name ? (size_t) (dot - name) : strlen(name);
    char nm[ZXF_NAME_MAX + 1];
    for (int i = 1; i <= 1000; i++) {
        if (i == 1)
            snprintf(nm, sizeof nm, "%s", name);
        else if ((size_t) snprintf(nm, sizeof nm, "%.*s %d%s", (int) (stem < 240 ? stem : 240),
                                   name, i, name + stem) >= sizeof nm)
            return ZXF_E_PATH;
        if ((size_t) snprintf(dst, sizeof dst, "%s" SEP "%s", tr, nm) >= sizeof dst)
            return ZXF_E_PATH;
        if (os_kind(dst).exists) continue;
        if ((rc = os_rename(abs, dst, false)) != 0) return rc;
        if (trashed) snprintf(trashed, cap, ZXF_TRASH "/%s", nm);
        return 0;
    }
    return ZXF_E_EXISTS;
}

typedef struct {
    const char *dir;
    uint32_t n;
} empty_t;

static int empty_one(void *ctx, const char *name)
{
    empty_t *e = ctx;
    char p[ABS_MAX];
    if ((size_t) snprintf(p, sizeof p, "%s" SEP "%s", e->dir, name) >= sizeof p) return ZXF_E_PATH;
    int rc = remove_tree(p, 0);
    if (!rc) e->n++;
    return rc;
}

int zxf_empty_trash(uint32_t *removed)
{
    char tr[ABS_MAX];
    *removed = 0;
    int rc = special_dir(ZXF_TRASH, tr, sizeof tr);
    if (rc) return rc;
    empty_t e = {tr, 0};
    rc = os_each(tr, empty_one, &e);
    *removed = e.n;
    return rc;
}

const char *zxf_strerror(int code)
{
    switch (code) {
    case ZXF_OK:
        return "ok";
    case ZXF_E_PATH:
        return "not a valid name or path in the ZXV folder";
    case ZXF_E_NOTFOUND:
        return "no such file or folder";
    case ZXF_E_EXISTS:
        return "something with that name already exists";
    case ZXF_E_NOTDIR:
        return "not a folder";
    case ZXF_E_ISDIR:
        return "is a folder";
    case ZXF_E_IO:
        return "the file system refused";
    case ZXF_E_TOOBIG:
        return "too large";
    case ZXF_E_ROOT:
        return "the ZXV folder cannot be found or made";
    case ZXF_E_BINARY:
        return "not text (not UTF-8)";
    case ZXF_E_STAGE:
        return "no \"begin\" for this file";
    case ZXF_E_DEPTH:
        return "folders nested too deep";
    default:
        return "unknown error";
    }
}
