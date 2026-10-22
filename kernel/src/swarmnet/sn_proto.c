/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sn_proto.c — the pure parsers of swarmnet.h: gateway URLs, HTTP response
 * heads, and the lookup in the kernel's baked table. No I/O. */
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

static int same_group(const char *a, const char *b)
{
    uint32_t i = 0;
    for (; i < 17 && a[i] && a[i] == b[i]; i++) {
    }
    return i < 17 && a[i] == b[i];
}

const sn_baked_t *sn_baked_find(const sn_baked_t *t, uint32_t n, uint32_t disc, const char *group)
{
    for (uint32_t i = 0; i < n; i++)
        if (t[i].disc == disc && same_group(t[i].group, group)) return &t[i];
    return 0;
}

void sn_http_adapt(sn_http_t *h, int ok)
{
    uint32_t t = h->stall_ms ? h->stall_ms : SN_STALL_MS;
    if (ok)
        t -= (t - SN_STALL_MS) / 4u;
    else if (h->timed_out)
        t = t * 2u > SN_STALL_MAX_MS ? SN_STALL_MAX_MS : t * 2u;
    h->stall_ms = t;
}
