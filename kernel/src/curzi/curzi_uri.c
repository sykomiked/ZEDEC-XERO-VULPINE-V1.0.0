/* curzi_uri.c — the 36n9:// parser (curzi_uri.h). Copyright (c) 2024-2026
 * Michael Laurence Curzi and 36N9 Genetics, LLC; SPDX-License-Identifier:
 * Apache-2.0. */
#include "curzi_uri.h"

#include <stddef.h>

static bool has(const char *s, uint32_t len, const char *pre, uint32_t pre_len)
{
    if (len < pre_len) return false;
    for (uint32_t i = 0; i < pre_len; i++)
        if (s[i] != pre[i]) return false;
    return true;
}

static uint32_t hex_u32(const char *s, uint32_t len, uint32_t *out)
{
    uint32_t v = 0, i = 0;
    for (; i < len; i++) {
        char c = s[i];
        uint32_t d;
        if (c >= '0' && c <= '9')
            d = (uint32_t) (c - '0');
        else if (c >= 'a' && c <= 'f')
            d = (uint32_t) (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            d = (uint32_t) (c - 'A' + 10);
        else
            break;
        if (v > 0x0FFFFFFFu) return 0; /* overflow */
        v = (v << 4) | d;
    }
    *out = v;
    return i;
}

static uint32_t dec_u32(const char *s, uint32_t len, uint32_t *out)
{
    uint32_t v = 0, i = 0;
    for (; i < len; i++) {
        char c = s[i];
        if (c < '0' || c > '9') break;
        if (v > 429496729u) return 0;
        v = v * 10 + (uint32_t) (c - '0');
    }
    *out = v;
    return i;
}

int curzi_uri_parse(const char *uri, uint32_t len, curzi_uri_t *out)
{
    const char *p;
    uint32_t rest, i;
    if (!uri || !out || len < 7) return -1;
    if (!has(uri, len, CURZI_SCHEME, 7)) return -1;
    out->route = CURZI_ROUTE_INVALID;
    out->basis_bank_id = 0;
    out->byte_offset = 0;
    out->frame_count = 1;
    out->port = CURZI_DEFAULT_PORT;
    out->slot = 0;
    out->cid[0] = 0;
    out->node[0] = 0;
    p = uri + 7;
    rest = len - 7;
    if (has(p, rest, "ipfs/", 5)) {
        /* 36n9://ipfs/<cid> — the raw intercept */
        const char *c = p + 5;
        uint32_t cl = rest - 5;
        for (i = 0; i < cl && i < CURZI_CID_MAX - 1; i++) {
            char ch = c[i];
            if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                  ch == '-' || ch == '_' || ch == '.'))
                break;
            out->cid[i] = ch;
        }
        out->cid[i] = 0;
        if (!i) return -1;
        out->route = CURZI_ROUTE_IPFS;
        return 0;
    }
    if (has(p, rest, "bank.", 5)) {
        const char *q = p + 5;
        uint32_t ql = rest - 5, n;
        uint32_t id;
        n = hex_u32(q, ql, &id);
        if (!n || n >= ql || q[n] != '/') return -1;
        out->basis_bank_id = id;
        q += n + 1;
        ql -= n + 1;
        if (ql >= 2 && q[0] == '0' && (q[1] == 'x' || q[1] == 'X')) {
            q += 2;
            ql -= 2;
        }
        n = hex_u32(q, ql, &out->byte_offset);
        if (!n) return -1;
        q += n;
        ql -= n;
        if (ql && q[0] == ':') {
            q += 1;
            ql -= 1;
            n = dec_u32(q, ql, &out->frame_count);
            if (!n || !out->frame_count) return -1;
        }
        out->route = CURZI_ROUTE_BANK;
        return 0;
    }
    if (has(p, rest, "stream.", 7) || has(p, rest, "aipi.", 5)) {
        bool aipi = p[0] == 'a';
        const char *q = p + (aipi ? 5 : 7);
        uint32_t ql = rest - (aipi ? 5 : 7), n;
        for (i = 0; i < ql && i < 23; i++) {
            char ch = q[i];
            if (ch == ':' || ch == '/' || ch == '?') break;
            if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-'))
                return -1;
            out->node[i] = ch;
        }
        out->node[i] = 0;
        q += i;
        ql -= i;
        if (!i) return -1;
        if (ql && q[0] == ':') {
            uint32_t port;
            q += 1;
            ql -= 1;
            n = dec_u32(q, ql, &port);
            if (!n || port > 65535) return -1;
            out->port = (uint16_t) port;
            q += n;
            ql -= n;
        }
        out->route = aipi ? CURZI_ROUTE_AIPI : CURZI_ROUTE_STREAM;
        if (aipi && ql >= 8 && has(q, ql, "/tensor/", 8)) {
            q += 8;
            ql -= 8;
            if (ql >= 1 && q[0] >= '0' && q[0] <= '9') {
                out->slot = (uint8_t) (q[0] - '0');
                if (ql >= 2 && q[1] >= '0' && q[1] <= '9')
                    out->slot = (uint8_t) (out->slot * 10 + (q[1] - '0'));
            }
        }
        return 0;
    }
    return -1;
}

int curzi_uri_gateway(const curzi_uri_t *u, const char *bank_cid, char *dst, uint32_t cap)
{
    static const char gw[] = "https://ipfs.filebase.io/ipfs/";
    uint32_t n = 0, i;
    if (!u || !dst) return -1;
    for (i = 0; gw[i] && n < cap - 1; i++) dst[n++] = gw[i];
    if (u->route == CURZI_ROUTE_IPFS) {
        for (i = 0; u->cid[i] && n < cap - 1; i++) dst[n++] = u->cid[i];
    } else if (u->route == CURZI_ROUTE_BANK) {
        if (!bank_cid) return -1;
        for (i = 0; bank_cid[i] && n < cap - 1; i++) dst[n++] = bank_cid[i];
    } else {
        return -1; /* streams and sockets are not gateway-mappable */
    }
    dst[n] = 0;
    return (int) n;
}