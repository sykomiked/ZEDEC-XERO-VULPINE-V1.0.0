/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_caps.c — which apps may use which capabilities (see zxv_caps.h K1-K4). */
#if !defined(_WIN32)
#    define _DEFAULT_SOURCE
#    include <fcntl.h>
#    include <sys/stat.h>
#    include <unistd.h>
#else
#    include <windows.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sha256.h"
#include "zxv_caps.h"
#include "zxv_place.h"

#define MAGIC    "zxv-caps 1\n"
#define FILE_CAP (64u * 1024u)

static char PATH_[ZCP_PATH_MAX + 1];
static zcp_grant_t G[ZCP_GRANTS];
static uint32_t NG;
static bool RESET, LOADING;
static int64_t (*CLOCK)(void);

void zcp_set_clock(int64_t (*now)(void))
{
    CLOCK = now;
}

static int64_t now_s(void)
{
    return CLOCK ? CLOCK() : (int64_t) time(NULL);
}

/* ===================== names ===================== */

static bool good_app(const char *a)
{
    size_t n = a ? strlen(a) : 0;
    if (n < 1 || n > ZCP_APP_MAX) return false;
    for (size_t i = 0; i < n; i++)
        if (a[i] <= ' ' || a[i] > '~') return false;
    return true;
}

bool zcp_cap_ok(const char *c)
{
    size_t n = c ? strlen(c) : 0;
    if (n < 1 || n > ZCP_CAP_MAX || c[0] == '.' || c[n - 1] == '.') return false;
    for (size_t i = 0; i < n; i++)
        if (!((c[i] >= 'a' && c[i] <= 'z') || (c[i] >= '0' && c[i] <= '9') || c[i] == '.'))
            return false;
    return true;
}

static const char *const SENSITIVE[] = {"camera",      "mic",           "location", "shell",
                                        "ledger.post", "journal.write", "caps",     "proc.control"};

bool zcp_sensitive(const char *cap)
{
    for (size_t i = 0; cap && i < sizeof SENSITIVE / sizeof SENSITIVE[0]; i++)
        if (!strcmp(cap, SENSITIVE[i])) return true;
    return false;
}

static const char *const MODE[] = {"default", "once", "always", "never"};

const char *zcp_mode_name(zcp_mode_t m)
{
    return (unsigned) m < 4 ? MODE[m] : "default";
}

int zcp_mode_parse(const char *s)
{
    for (int i = 0; s && i < 4; i++)
        if (!strcmp(s, MODE[i])) return i;
    return -1;
}

const char *zcp_strerror(int code)
{
    switch (code) {
    case ZCP_OK:
        return "allowed";
    case ZCP_E_ARG:
        return "an app is 1 to 31 printable characters with no spaces; a capability is 1 to 23 "
               "of a-z, 0-9 and dots";
    case ZCP_E_FULL:
        return "no room for another answer";
    case ZCP_E_IO:
        return "the answers file could not be written";
    case ZCP_E_DENIED:
        return "the owner said never";
    case ZCP_E_PENDING:
        return "waiting for the owner's answer";
    case ZCP_E_NOTFOUND:
        return "no answer to take back";
    default:
        return "unknown";
    }
}

/* ===================== the file (K4) ===================== */

static const char HEX[] = "0123456789abcdef";

static void hexify(const uint8_t *b, char *out)
{
    for (size_t i = 0; i < 32; i++) {
        out[2 * i] = HEX[b[i] >> 4];
        out[2 * i + 1] = HEX[b[i] & 15];
    }
    out[64] = 0;
}

static bool replace_file(const char *from, const char *to)
{
#if defined(_WIN32)
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(from, to) == 0;
#endif
}

static int save(void)
{
    if (!PATH_[0] || LOADING) return 0;
    static char buf[FILE_CAP];
    size_t n = strlen(MAGIC);
    memcpy(buf, MAGIC, n);
    for (uint32_t i = 0; i < NG; i++) {
        if (G[i].mode != ZCP_ALWAYS && G[i].mode != ZCP_NEVER) continue; /* lasting answers */
        int k = snprintf(buf + n, sizeof buf - n, "grant\t%s\t%s\t%s\n", G[i].app, G[i].cap,
                         MODE[G[i].mode]);
        if (k < 0 || (size_t) k >= sizeof buf - n) return ZCP_E_IO;
        n += (size_t) k;
    }
    if (n + 70 >= sizeof buf) return ZCP_E_IO;
    uint8_t h[32];
    char hex[65];
    sha256((const uint8_t *) buf, n, h);
    hexify(h, hex);
    n += (size_t) snprintf(buf + n, sizeof buf - n, "end %s\n", hex);
    char tmp[ZCP_PATH_MAX + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", PATH_);
#if defined(_WIN32)
    FILE *f = fopen(tmp, "wb");
#else
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    FILE *f = fd < 0 ? NULL : fdopen(fd, "wb");
    if (fd >= 0 && !f) close(fd);
#endif
    if (!f) return ZCP_E_IO;
    bool ok = fwrite(buf, 1, n, f) == n;
    ok = fclose(f) == 0 && ok;
    if (!ok || !replace_file(tmp, PATH_)) {
        remove(tmp);
        return ZCP_E_IO;
    }
    return 0;
}

static zcp_grant_t *find(const char *app, const char *cap)
{
    for (uint32_t i = 0; i < NG; i++)
        if (!strcmp(G[i].app, app) && !strcmp(G[i].cap, cap)) return &G[i];
    return NULL;
}

static zcp_grant_t *make(const char *app, const char *cap)
{
    zcp_grant_t *g = find(app, cap);
    if (g || NG == ZCP_GRANTS) return g;
    g = &G[NG++];
    memset(g, 0, sizeof *g);
    memcpy(g->app, app, strlen(app) + 1);
    memcpy(g->cap, cap, strlen(cap) + 1);
    return g;
}

static void drop(zcp_grant_t *g)
{
    uint32_t i = (uint32_t) (g - G);
    memmove(&G[i], &G[i + 1], (NG - i - 1) * sizeof G[0]);
    NG--;
}

static bool load_line(char *l)
{
    char *f[4];
    int k = 0;
    f[k++] = l;
    for (char *p = l; *p; p++)
        if (*p == '\t') {
            if (k == 4) return false;
            *p = 0;
            f[k++] = p + 1;
        }
    if (k != 4 || strcmp(f[0], "grant")) return false;
    int m = zcp_mode_parse(f[3]);
    if ((m != ZCP_ALWAYS && m != ZCP_NEVER) || !good_app(f[1]) || !zcp_cap_ok(f[2]) ||
        !strcmp(f[2], "location") || find(f[1], f[2]))
        return false;
    zcp_grant_t *g = make(f[1], f[2]);
    if (!g) return false;
    g->mode = (uint8_t) m;
    return true;
}

/* 1 loaded, 0 no file, -1 damaged */
static int load(void)
{
    static char buf[FILE_CAP + 2];
    FILE *f = fopen(PATH_, "rb");
    if (!f) return 0;
    size_t n = fread(buf, 1, FILE_CAP + 1, f);
    fclose(f);
    buf[n] = 0;
    if (n > FILE_CAP || n < strlen(MAGIC) + 69 || strncmp(buf, MAGIC, strlen(MAGIC)) ||
        strlen(buf) != n)
        return -1;
    char *end = buf + n - 69; /* "end <64 hex>\n" */
    if (strncmp(end, "end ", 4) || end[68] != '\n' || end[-1] != '\n') return -1;
    uint8_t got[32];
    char hex[65];
    sha256((const uint8_t *) buf, (size_t) (end - buf), got);
    hexify(got, hex);
    if (memcmp(hex, end + 4, 64)) return -1;
    *end = 0;
    for (char *l = buf + strlen(MAGIC); *l;) {
        char *nl = strchr(l, '\n'); /* the text above "end" ends in "\n" */
        *nl = 0;
        if (!load_line(l)) return -1;
        l = nl + 1;
    }
    return 1;
}

int zcp_open(const char *path)
{
    zcp_close();
    if (!path || !path[0]) return 0;
    if (strlen(path) > ZCP_PATH_MAX) return ZCP_E_ARG;
    memcpy(PATH_, path, strlen(path) + 1);
    LOADING = true;
    int rc = load();
    LOADING = false;
    if (rc < 0) {
        memset(G, 0, sizeof G);
        NG = 0;
        char aside[ZCP_PATH_MAX + 16];
        snprintf(aside, sizeof aside, "%s.damaged", PATH_);
        if (!replace_file(PATH_, aside)) {
            PATH_[0] = 0;
            return ZCP_E_IO;
        }
        RESET = true;
    }
    if (rc <= 0 && save() != 0) {
        PATH_[0] = 0;
        RESET = false;
        return ZCP_E_IO;
    }
    return 0;
}

void zcp_close(void)
{
    PATH_[0] = 0;
    RESET = false;
    memset(G, 0, sizeof G);
    NG = 0;
}

bool zcp_was_reset(void)
{
    return RESET;
}

/* ===================== answers (K1-K3) ===================== */

int zcp_check(const char *app, const char *cap)
{
    if (!good_app(app) || !zcp_cap_ok(cap)) return ZCP_E_ARG;
    if (!strcmp(cap, "location")) { /* K3 */
        zpl_here_t h;
        int r = zpl_ask(app, &h);
        memset(&h, 0, sizeof h);
        return r == 0 || r == ZPL_E_NONE ? ZCP_OK
               : r == ZPL_E_DENIED       ? ZCP_E_DENIED
               : r == ZPL_E_PENDING      ? ZCP_E_PENDING
               : r == ZPL_E_FULL         ? ZCP_E_FULL
                                         : ZCP_E_ARG;
    }
    zcp_grant_t *g = find(app, cap);
    if (g) g->asked = now_s();
    if (g && g->mode == ZCP_NEVER) return ZCP_E_DENIED;
    if (g && g->mode == ZCP_ALWAYS) return ZCP_OK;
    if (g && g->mode == ZCP_ONCE) { /* used up */
        drop(g);
        return ZCP_OK;
    }
    if (!zcp_sensitive(cap)) return ZCP_OK;
    if (!g && !(g = make(app, cap))) return ZCP_E_FULL;
    g->pending = true;
    g->asked = now_s();
    return ZCP_E_PENDING;
}

int zcp_set(const char *app, const char *cap, zcp_mode_t mode)
{
    if (!good_app(app) || !zcp_cap_ok(cap) || (unsigned) mode > ZCP_NEVER) return ZCP_E_ARG;
    if (!strcmp(cap, "location")) { /* K3 */
        static const zpl_mode_t TO[] = {ZPL_ASK, ZPL_ONCE, ZPL_ALWAYS, ZPL_NEVER};
        int r = zpl_permit(app, TO[mode]);
        return r == 0 ? 0 : r == ZPL_E_FULL ? ZCP_E_FULL : r == ZPL_E_IO ? ZCP_E_IO : ZCP_E_ARG;
    }
    zcp_grant_t *g = find(app, cap);
    if (mode == ZCP_DEFAULT) {
        if (!g) return ZCP_E_NOTFOUND;
        bool lasting = g->mode == ZCP_ALWAYS || g->mode == ZCP_NEVER;
        drop(g);
        return lasting ? save() : 0;
    }
    if (!g && !(g = make(app, cap))) return ZCP_E_FULL;
    bool lasting =
        g->mode == ZCP_ALWAYS || g->mode == ZCP_NEVER || mode == ZCP_ALWAYS || mode == ZCP_NEVER;
    g->mode = (uint8_t) mode;
    g->pending = false;
    return lasting ? save() : 0;
}

uint32_t zcp_list(const char *app, zcp_grant_t *out, uint32_t cap)
{
    uint32_t n = 0;
    bool all = !app || !app[0];
    for (uint32_t i = 0; i < NG; i++) {
        if (!all && strcmp(G[i].app, app)) continue;
        if (n < cap) out[n] = G[i];
        n++;
    }
    for (uint32_t i = 0; i < zpl_app_count(); i++) { /* K3 */
        const zpl_app_t *a = zpl_app(i);
        if (!all && strcmp(a->app, app)) continue;
        if (a->mode == ZPL_ASK && !a->pending) continue; /* nothing to show */
        if (n < cap) {
            static const uint8_t FROM[] = {ZCP_DEFAULT, ZCP_ONCE, ZCP_ALWAYS, ZCP_NEVER};
            memset(&out[n], 0, sizeof out[n]);
            memcpy(out[n].app, a->app, strlen(a->app) + 1);
            memcpy(out[n].cap, "location", 9);
            out[n].mode = a->mode < 4 ? FROM[a->mode] : ZCP_DEFAULT;
            out[n].pending = a->pending;
            out[n].asked = a->asked;
        }
        n++;
    }
    return n;
}
