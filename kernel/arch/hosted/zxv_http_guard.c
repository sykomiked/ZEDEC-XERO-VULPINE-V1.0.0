/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_http_guard.c — token, Host, Origin and fetch-metadata checks for the
 * hosted app's local HTTP API. See zxv_http_guard.h for the rules. */
#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    include <bcrypt.h>
#else
#    define _DEFAULT_SOURCE
#    define _DARWIN_C_SOURCE
#    include <fcntl.h>
#    include <unistd.h>
#    if defined(__APPLE__)
#        include <stdlib.h> /* arc4random_buf */
#    endif
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_http_guard.h"

/* ---------- G1: the token ---------- */

int zxv_os_random(uint8_t *p, size_t n)
{
#if defined(_WIN32)
    return BCryptGenRandom(NULL, p, (ULONG) n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 ? 0 : -1;
#elif defined(__APPLE__)
    arc4random_buf(p, n); /* the kernel CSPRNG on every supported macOS */
    return 0;
#else
    if (n <= 256 && getentropy(p, n) == 0) return 0;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    size_t got = 0;
    while (got < n) {
        ssize_t k = read(fd, p + got, n - got);
        if (k <= 0) break;
        got += (size_t) k;
    }
    close(fd);
    return got == n ? 0 : -1;
#endif
}

int zxv_guard_token_new(char out[ZXV_TOKEN_HEX + 1])
{
    static const char hex[] = "0123456789abcdef";
    uint8_t raw[ZXV_TOKEN_BYTES];
    out[0] = 0;
    if (zxv_os_random(raw, sizeof raw) != 0) return -1;
    /* an all-zero draw means the source is broken, not that we were unlucky */
    uint8_t any = 0;
    for (size_t i = 0; i < sizeof raw; i++) any |= raw[i];
    if (!any) return -1;
    for (size_t i = 0; i < sizeof raw; i++) {
        out[2 * i] = hex[raw[i] >> 4];
        out[2 * i + 1] = hex[raw[i] & 15];
    }
    out[ZXV_TOKEN_HEX] = 0;
    memset(raw, 0, sizeof raw);
    return 0;
}

bool zxv_guard_token_valid(const char *s)
{
    if (!s) return false;
    for (size_t i = 0; i < ZXV_TOKEN_HEX; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return s[ZXV_TOKEN_HEX] == 0;
}

/* ---------- header parsing ---------- */

static int lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static bool ieq(const char *a, size_t alen, const char *b)
{
    size_t blen = strlen(b);
    if (alen != blen) return false;
    for (size_t i = 0; i < alen; i++)
        if (lower((unsigned char) a[i]) != lower((unsigned char) b[i])) return false;
    return true;
}

uint32_t zxv_http_header(const char *req, const char *name, const char **val, size_t *len)
{
    uint32_t count = 0;
    size_t nlen = strlen(name);
    const char *end = strstr(req, "\r\n\r\n");
    if (!end) end = req + strlen(req);
    const char *line = strstr(req, "\r\n"); /* skip the request line */
    if (val) *val = NULL;
    if (len) *len = 0;
    while (line && line < end) {
        line += 2;
        const char *eol = strstr(line, "\r\n");
        if (!eol || eol > end) eol = end;
        const char *colon = memchr(line, ':', (size_t) (eol - line));
        if (colon && (size_t) (colon - line) == nlen && ieq(line, nlen, name)) {
            const char *v = colon + 1, *ve = eol;
            while (v < ve && (*v == ' ' || *v == '\t')) v++;
            while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t')) ve--;
            if (count == 0) {
                if (val) *val = v;
                if (len) *len = (size_t) (ve - v);
            }
            count++;
        }
        line = eol < end ? eol : NULL;
    }
    return count;
}

/* ---------- G2: Host ---------- */

static bool digits(const char *p, size_t n)
{
    if (n == 0 || n > 5) return false;
    for (size_t i = 0; i < n; i++)
        if (p[i] < '0' || p[i] > '9') return false;
    return true;
}

static bool host_ok(const zxv_guard_t *g, const char *h, size_t n)
{
    static const char *const names[] = {"127.0.0.1", "localhost"};
    for (size_t k = 0; k < 2; k++) {
        size_t m = strlen(names[k]);
        if (n < m || !ieq(h, m, names[k])) continue;
        if (n == m) return g->port == 80; /* browsers drop the default port */
        if (h[m] != ':') continue;
        const char *p = h + m + 1;
        size_t pn = n - m - 1;
        if (!digits(p, pn)) continue;
        if (g->any_loopback_port) return true;
        char want[8];
        int wn = snprintf(want, sizeof want, "%d", g->port);
        if (wn > 0 && (size_t) wn == pn && !memcmp(p, want, pn)) return true;
    }
    return false;
}

/* ---------- G1: constant-time token compare ---------- */

static bool token_eq(const char *got, size_t n, const char *want)
{
    if (n != ZXV_TOKEN_HEX || !zxv_guard_token_valid(want)) return false;
    unsigned diff = 0;
    for (size_t i = 0; i < ZXV_TOKEN_HEX; i++)
        diff |= (unsigned) ((unsigned char) got[i] ^ (unsigned char) want[i]);
    return diff == 0;
}

/* ---------- the decision ---------- */

zxv_guard_verdict_t zxv_guard_check(const zxv_guard_t *g, const char *req, bool need_token)
{
    if (strncmp(req, "GET ", 4) != 0 && strncmp(req, "POST ", 5) != 0) return ZXV_GUARD_BAD_METHOD;

    const char *v;
    size_t n;
    if (zxv_http_header(req, "Transfer-Encoding", NULL, NULL) != 0) return ZXV_GUARD_BAD_SHAPE;
    if (zxv_http_header(req, "Content-Length", NULL, NULL) > 1) return ZXV_GUARD_BAD_SHAPE;

    uint32_t hc = zxv_http_header(req, "Host", &v, &n);
    if (hc != 1) return hc ? ZXV_GUARD_BAD_SHAPE : ZXV_GUARD_BAD_HOST;
    const char *host = v;
    size_t host_n = n;
    if (!host_ok(g, host, host_n)) return ZXV_GUARD_BAD_HOST;

    uint32_t oc = zxv_http_header(req, "Origin", &v, &n);
    if (oc > 1) return ZXV_GUARD_BAD_SHAPE;
    if (oc == 1) {
        /* must be http:// + this request's own Host ("null" never matches) */
        bool same = n == 7 + host_n && !strncmp(v, "http://", 7);
        for (size_t i = 0; same && i < host_n; i++)
            if (lower((unsigned char) v[7 + i]) != lower((unsigned char) host[i])) same = false;
        if (!same) return ZXV_GUARD_BAD_ORIGIN;
    }

    if (zxv_http_header(req, "Sec-Fetch-Site", &v, &n) >= 1) {
        if (!ieq(v, n, "same-origin") && !ieq(v, n, "none")) return ZXV_GUARD_CROSS_SITE;
    }

    uint32_t tc = zxv_http_header(req, ZXV_TOKEN_HEADER, &v, &n);
    if (tc > 1) return ZXV_GUARD_BAD_SHAPE;
    if (need_token) {
        if (tc == 0) return ZXV_GUARD_NO_TOKEN;
        if (!token_eq(v, n, g->token)) return ZXV_GUARD_BAD_TOKEN;
    }
    return ZXV_GUARD_OK;
}

const char *zxv_guard_reason(zxv_guard_verdict_t v)
{
    switch (v) {
    case ZXV_GUARD_OK:
        return "ok";
    case ZXV_GUARD_BAD_METHOD:
        return "method not allowed";
    case ZXV_GUARD_BAD_SHAPE:
        return "malformed request";
    case ZXV_GUARD_BAD_HOST:
        return "host not allowed";
    case ZXV_GUARD_BAD_ORIGIN:
        return "origin not allowed";
    case ZXV_GUARD_CROSS_SITE:
        return "cross-site request refused";
    case ZXV_GUARD_NO_TOKEN:
        return "token required";
    case ZXV_GUARD_BAD_TOKEN:
        return "bad token";
    }
    return "refused";
}

/* ---------- G6: the web root ---------- */

static bool web_char(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
           ch == '.' || ch == '_' || ch == '-';
}

int zxv_web_path(const char *path, size_t len, char *out, size_t cap)
{
    static const char INDEX[] = "index.html";
    if (!out || cap == 0) return -1;
    out[0] = 0;
    if (!path && len) return -1;
    bool folder = len == 0 || path[len - 1] == '/';
    size_t need = len + (folder ? sizeof INDEX - 1 : 0);
    if (need + 1 > cap || need + 1 > ZXV_WEB_PATH_MAX) return -1;
    /* every segment is checked before anything is written */
    uint32_t depth = 0;
    size_t at = 0;
    while (at < len) {
        size_t end = at;
        while (end < len && path[end] != '/') {
            if (!web_char(path[end])) return -1;
            end++;
        }
        if (end == at || path[at] == '.') return -1; /* empty, "..", or hidden */
        if (++depth > ZXV_WEB_DEPTH) return -1;
        at = end + 1; /* past the slash; a trailing slash ends the loop */
    }
    if (folder && depth >= ZXV_WEB_DEPTH) return -1; /* the index is one more segment */
    if (len) memcpy(out, path, len);
    if (folder) memcpy(out + len, INDEX, sizeof INDEX - 1);
    out[need] = 0;
    if (!zxv_web_type(out)) {
        out[0] = 0;
        return -1;
    }
    return (int) need;
}

const char *zxv_web_type(const char *rel)
{
    static const struct {
        const char *ext, *type;
    } T[] = {
        {".html", "text/html; charset=utf-8"},
        {".js", "text/javascript; charset=utf-8"},
        {".wasm", "application/wasm"},
        {".json", "application/json"},
        {".css", "text/css; charset=utf-8"},
        {".zxvg", "application/octet-stream"},
        {".ttf", "font/ttf"},
        {".otf", "font/otf"},
        {".svg", "image/svg+xml"},
    };
    if (!rel) return NULL;
    size_t n = strlen(rel);
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        size_t e = strlen(T[i].ext);
        if (n > e && !memcmp(rel + n - e, T[i].ext, e) && rel[n - e - 1] != '/') return T[i].type;
    }
    return NULL;
}
