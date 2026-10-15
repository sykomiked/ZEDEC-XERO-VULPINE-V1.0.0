/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sn_proto.c — the pure parsers of swarmnet.h: gateway URLs, HTTP response
 * heads and the bucket's manifest.txt. No I/O. */
#include "swarmnet.h"

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}

/* Does [p, p+n) start with the lower-case word w, case-insensitively? */
static int starts(const uint8_t *p, uint32_t n, const char *w)
{
    uint32_t i = 0;
    for (; w[i]; i++)
        if (i >= n || lower((char) p[i]) != w[i]) return 0;
    return 1;
}

int sn_url_parse(const char *url, char *host, uint32_t host_cap, uint16_t *port)
{
    const char *pre = "http://";
    uint32_t i = 0, h = 0;
    for (; pre[i]; i++)
        if (url[i] != pre[i]) return 0;
    while (url[i] && url[i] != ':' && url[i] != '/') {
        char c = url[i++];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '.'))
            return 0;
        if (h + 1 >= host_cap) return 0;
        host[h++] = c;
    }
    if (h == 0) return 0;
    host[h] = 0;
    uint32_t v = 80;
    if (url[i] == ':') {
        i++;
        v = 0;
        uint32_t d = 0;
        while (url[i] >= '0' && url[i] <= '9' && d < 6) {
            v = v * 10u + (uint32_t) (url[i++] - '0');
            d++;
        }
        if (d == 0 || v == 0 || v > 65535u) return 0;
    }
    if (url[i] == '/') i++;
    if (url[i]) return 0; /* a path prefix on a gateway is not supported */
    *port = (uint16_t) v;
    return 1;
}

int32_t sn_http_head(const uint8_t *b, uint32_t n, sn_head_t *h)
{
    uint32_t end = 0;
    for (uint32_t i = 0; i + 3 < n; i++)
        if (b[i] == '\r' && b[i + 1] == '\n' && b[i + 2] == '\r' && b[i + 3] == '\n') {
            end = i + 4;
            break;
        }
    if (!end) return 0;
    hw_memset(h, 0, sizeof *h);
    /* status line: HTTP/1.x NNN reason */
    if (end < 12 || !starts(b, end, "http/1.") || (b[7] != '0' && b[7] != '1') || b[8] != ' ')
        return -1;
    for (uint32_t i = 9; i < 12; i++) {
        if (b[i] < '0' || b[i] > '9') return -1;
        h->status = h->status * 10u + (uint32_t) (b[i] - '0');
    }
    if (b[12] != ' ' && b[12] != '\r') return -1;
    uint32_t i = 0;
    while (i < end && b[i] != '\n') i++;
    i++;
    while (i < end - 2) {
        uint32_t e = i;
        while (e < end && b[e] != '\r') e++;
        if (e + 1 >= end || b[e + 1] != '\n') return -1;
        const uint8_t *f = b + i;
        uint32_t fl = e - i;
        if (starts(f, fl, "content-length:")) {
            uint32_t k = 15, d = 0;
            uint64_t v = 0;
            while (k < fl && (f[k] == ' ' || f[k] == '\t')) k++;
            while (k < fl && f[k] >= '0' && f[k] <= '9') {
                if (++d > 12) return -1;
                v = v * 10u + (uint32_t) (f[k++] - '0');
            }
            while (k < fl && (f[k] == ' ' || f[k] == '\t')) k++;
            if (d == 0 || k != fl || (h->has_length && h->length != v)) return -1;
            h->length = v;
            h->has_length = 1;
        } else if (starts(f, fl, "transfer-encoding:")) {
            h->chunked = 1;
        } else if (starts(f, fl, "connection:")) {
            for (uint32_t k = 11; k + 5 <= fl; k++)
                if (starts(f + k, fl - k, "close")) h->close = 1;
        }
        i = e + 2;
    }
    return (int32_t) end;
}

static int is_space(char c)
{
    return c == ' ' || c == '\t';
}

static int field(const char **q, const char *end, const char **f, uint32_t *len)
{
    const char *a = *q;
    while (a < end && is_space(*a)) a++;
    if (a == end) return 0;
    *f = a;
    while (a < end && !is_space(*a)) a++;
    *len = (uint32_t) (a - *f);
    *q = a;
    return 1;
}

static int same(const char *f, uint32_t len, const char *w)
{
    uint32_t i = 0;
    for (; i < len; i++)
        if (w[i] != f[i]) return 0;
    return w[i] == 0;
}

static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Copy a field into out (cap with NUL); 0 if it does not fit or is empty. */
static int copy(const char *f, uint32_t len, char *out, uint32_t cap)
{
    if (len == 0 || len >= cap) return 0;
    for (uint32_t i = 0; i < len; i++) out[i] = f[i];
    out[len] = 0;
    return 1;
}

const char *sn_manifest_find(const char *t, uint32_t n, uint32_t disc, const char *group,
                             sn_entry_t *e, uint32_t *line)
{
    const char *q = t, *end = t + n;
    *line = 0;
    while (q < end) {
        const char *le = q, *f, *a = q;
        uint32_t len;
        while (le < end && *le != '\n') le++;
        (*line)++;
        const char *ln = le > q && le[-1] == '\r' ? le - 1 : le;
        q = le < end ? le + 1 : end;
        if (!field(&a, ln, &f, &len) || *f == '#') continue;
        if (!same(f, len, "swarm")) {
            if (!same(f, len, "core") && !same(f, len, "cartridge") && !same(f, len, "layer"))
                return "unknown kind";
            continue;
        }
        const char *fl[7];
        uint32_t fn[7];
        for (int k = 0; k < 7; k++)
            if (!field(&a, ln, &fl[k], &fn[k])) return "a swarm line needs 8 fields";
        if (field(&a, ln, &f, &len)) return "extra fields";
        /* arch disc group bytes sha cid path */
        uint32_t d = 0;
        for (uint32_t i = 0; i < fn[1]; i++) {
            if (fl[1][i] < '0' || fl[1][i] > '9' || fn[1] > 3) return "disc is not a number";
            d = d * 10u + (uint32_t) (fl[1][i] - '0');
        }
        if (d != disc || !same(fl[2], fn[2], group)) continue;
        if (!same(fl[0], fn[0], "all")) return "a swarm disc must be for arch all";
        hw_memset(e, 0, sizeof *e);
        e->disc = d;
        if (!copy(fl[2], fn[2], e->group, sizeof e->group)) return "group too long";
        uint64_t b = 0;
        if (fn[3] == 0 || fn[3] > 10) return "bytes is not a size";
        for (uint32_t i = 0; i < fn[3]; i++) {
            if (fl[3][i] < '0' || fl[3][i] > '9') return "bytes is not a size";
            b = b * 10u + (uint32_t) (fl[3][i] - '0');
        }
        if (b == 0 || b > 1800000000u) return "bytes is not a size up to 1.8 GB";
        e->bytes = b;
        if (fn[4] != 64) return "sha256 is not 64 lower-case hex digits";
        for (uint32_t i = 0; i < 32; i++) {
            int hi = hexv(fl[4][2 * i]), lo = hexv(fl[4][2 * i + 1]);
            if (hi < 0 || lo < 0) return "sha256 is not 64 lower-case hex digits";
            e->sha[i] = (uint8_t) (hi << 4 | lo);
        }
        if (!copy(fl[5], fn[5], e->cid, sizeof e->cid)) return "cid too long";
        if (!copy(fl[6], fn[6], e->path, sizeof e->path)) return "path too long";
        *line = 0;
        return 0;
    }
    *line = 0;
    return "the bucket has no such swarm disc";
}

/* ---- adaptive response timeouts (swarmnet.h) ---- */

void sn_http_adapt(sn_http_t *h, int ok)
{
    uint32_t t = h->stall_ms ? h->stall_ms : SN_STALL_MS;
    if (ok)
        t -= (t - SN_STALL_MS) / 4u;
    else if (h->timed_out)
        t = t * 2u > SN_STALL_MAX_MS ? SN_STALL_MAX_MS : t * 2u;
    h->stall_ms = t;
}
