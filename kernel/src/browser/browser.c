/* browser.c — ZEDEC XERO pqOS Web Browser implementation
 *
 * See browser.h for the LIMITATIONS block; it is the contract, and it is
 * deliberately blunt about what this module does not do (no sockets, no CSS,
 * no JS, no image decode, no cookie jar).
 *
 * The shape of this file follows kernel/src/virtio/vring.c: everything that
 * can be computed without hardware is computed here, for real, and the one
 * thing that cannot — moving bytes over a wire — is a function-pointer table
 * (http_transport_ops_t) that a future src/net + src/tls shim fills in. With
 * no table bound, every path that would need the network returns
 * BROWSER_ERR_NO_TRANSPORT. Nothing returns success for a fetch that did not
 * happen, and no tab is ever flagged `loaded` without bytes having arrived.
 *
 * The tokenizer is written as a flat index loop over an explicit (buf, len)
 * pair. It never dereferences buf[i] without having proved i < len in the
 * same expression chain, never recurses, and never trusts a length that came
 * out of the document. That is the whole reason it is structured this way:
 * HTML is attacker-controlled input.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#ifdef TEST_HOST
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#else
#include "freestanding.h"
#endif

#include "browser.h"

/* ===================================================================== */
/* Small freestanding helpers — no libc string or memory calls here.      */
/* ===================================================================== */

static void b_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++) d[i] = (uint8_t)v;
}

/* Forward copy only; every caller below has dst <= src, which is the only
 * direction the in-place chunk de-framer needs. */
static void b_memmove_fwd(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d == s) return;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static uint32_t b_strlen(const char *s) {
    uint32_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

/* Bounded copy. Returns true only if the WHOLE source fitted. */
static bool b_copyn(char *dst, uint32_t cap, const char *src, uint32_t n) {
    if (!dst || cap == 0) return false;
    uint32_t i = 0;
    for (; i + 1 < cap && i < n && src && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
    return (i == n) || (src && i < n && src[i] == '\0');
}

static bool b_copy(char *dst, uint32_t cap, const char *src) {
    return b_copyn(dst, cap, src, src ? b_strlen(src) : 0);
}

/* Length-delimited copy for text that may legitimately contain NUL bytes.
 *
 * b_copyn() stops at the first NUL in the source and reports SUCCESS, which
 * for a length-delimited HTML buffer means "silently threw away everything
 * after the NUL and said nothing". HTML5 replaces U+0000 in character data
 * with U+FFFD, so that is what this does; the caller then really does hold
 * the whole run, and a `false` return really does mean it did not fit.
 * Returns true only if the WHOLE source fitted. */
static bool b_copy_text(char *dst, uint32_t cap, const char *src, uint32_t n) {
    if (!dst || cap == 0) return false;
    uint32_t o = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (src[i] == '\0') {
            if (o + 4 > cap) { dst[o] = '\0'; return false; }
            dst[o++] = (char)0xEF; dst[o++] = (char)0xBF; dst[o++] = (char)0xBD;
        } else {
            if (o + 2 > cap) { dst[o] = '\0'; return false; }
            dst[o++] = src[i];
        }
    }
    dst[o] = '\0';
    return true;
}

static char b_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool b_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static bool b_is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool b_is_digit(char c) { return c >= '0' && c <= '9'; }

static int b_hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool b_ieq(const char *a, const char *b) {
    uint32_t i = 0;
    if (!a || !b) return false;
    while (a[i] && b[i]) {
        if (b_lower(a[i]) != b_lower(b[i])) return false;
        i++;
    }
    return a[i] == b[i];
}

/* Case-insensitive compare of `n` bytes of a against NUL-terminated b, where
 * b must be exactly n long. Bounds are the caller's responsibility. */
static bool b_ieq_len(const char *a, uint32_t n, const char *b) {
    uint32_t i = 0;
    for (; i < n; i++) {
        if (!b[i]) return false;
        if (b_lower(a[i]) != b_lower(b[i])) return false;
    }
    return b[i] == '\0';
}

static void b_lower_str(char *s) {
    for (uint32_t i = 0; s[i]; i++) s[i] = b_lower(s[i]);
}

const char *browser_strerror(int err) {
    switch (err) {
        case BROWSER_OK:                return "ok";
        case BROWSER_ERR_ARG:           return "bad argument";
        case BROWSER_ERR_NOENT:         return "no such entry";
        case BROWSER_ERR_FULL:          return "table full";
        case BROWSER_ERR_NO_TRANSPORT:  return "no HTTP transport bound";
        case BROWSER_ERR_BAD_URL:       return "malformed or unsupported URL";
        case BROWSER_ERR_IO:            return "transport I/O failure";
        case BROWSER_ERR_TOO_LARGE:     return "response exceeds receive buffer";
        case BROWSER_ERR_PROTOCOL:      return "malformed HTTP response";
        case BROWSER_ERR_TRUNCATED:     return "truncated";
        case BROWSER_ERR_NO_HISTORY:    return "no history entry in that direction";
        default:                        return "unknown error";
    }
}

/* ===================================================================== */
/* Bounded appender — every HTTP/URL string is built through this so that  */
/* an overflow is recorded rather than written.                           */
/* ===================================================================== */

typedef struct {
    char *buf;
    uint32_t cap;
    uint32_t len;
    bool ovf;
} b_app_t;

static void app_init(b_app_t *a, char *buf, uint32_t cap) {
    a->buf = buf; a->cap = cap; a->len = 0; a->ovf = (cap == 0);
    if (cap) buf[0] = '\0';
}

static void app_mem(b_app_t *a, const char *s, uint32_t n) {
    if (a->ovf) return;
    if (a->len + n + 1 > a->cap) { a->ovf = true; return; }
    for (uint32_t i = 0; i < n; i++) a->buf[a->len + i] = s[i];
    a->len += n;
    a->buf[a->len] = '\0';
}

static void app_str(b_app_t *a, const char *s) {
    if (!s) return;
    app_mem(a, s, b_strlen(s));
}

static void app_ch(b_app_t *a, char c) { app_mem(a, &c, 1); }

static void app_u32(b_app_t *a, uint32_t v) {
    char tmp[12];
    uint32_t t = 0;
    if (v == 0) tmp[t++] = '0';
    while (v > 0 && t < sizeof(tmp)) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
    char rev[12];
    for (uint32_t i = 0; i < t; i++) rev[i] = tmp[t - 1 - i];
    app_mem(a, rev, t);
}

/* ===================================================================== */
/* URL parsing                                                            */
/* ===================================================================== */

static uint16_t scheme_default_port(const char *scheme) {
    if (b_ieq(scheme, "http"))  return 80;
    if (b_ieq(scheme, "https")) return 443;
    if (b_ieq(scheme, "ws"))    return 80;
    if (b_ieq(scheme, "wss"))   return 443;
    if (b_ieq(scheme, "ftp"))   return 21;
    return 0;
}

/* A host must be non-empty and free of control characters, whitespace, and
 * the delimiters that would let a crafted URL smuggle a second request line
 * into http_build_request(). Rejecting them here is the only reason the
 * builder can concatenate host into a header without escaping. */
static bool host_is_sane(const char *h) {
    if (!h || !h[0]) return false;
    for (uint32_t i = 0; h[i]; i++) {
        unsigned char c = (unsigned char)h[i];
        if (c <= 0x20 || c == 0x7F) return false;
        if (c == '/' || c == '\\' || c == '?' || c == '#' || c == '@') return false;
    }
    return true;
}

/* Path/query/fragment must not carry CR or LF either — same reason. */
static bool field_is_sane(const char *s) {
    for (uint32_t i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\r' || c == '\n' || c == '\0') return false;
    }
    return true;
}

static int parse_url_inner(const char *url, browser_url_t *out) {
    b_memset(out, 0, sizeof(*out));
    if (!url) return BROWSER_ERR_BAD_URL;

    uint32_t n = b_strlen(url);
    if (n == 0 || n >= BROWSER_MAX_URL_LEN) return BROWSER_ERR_BAD_URL;

    uint32_t i = 0;

    /* ---- scheme ---- */
    if (b_is_alpha(url[0])) {
        uint32_t j = 0;
        while (j < n && (b_is_alpha(url[j]) || b_is_digit(url[j]) ||
                         url[j] == '+' || url[j] == '-' || url[j] == '.')) j++;
        if (j < n && url[j] == ':' && j > 0 && j < sizeof(out->scheme)) {
            if (!b_copyn(out->scheme, sizeof(out->scheme), url, j))
                return BROWSER_ERR_BAD_URL;
            b_lower_str(out->scheme);
            i = j + 1;
        }
    }

    /* ---- authority ---- */
    bool have_authority = false;
    if (i + 1 < n && url[i] == '/' && url[i + 1] == '/') {
        have_authority = true;
        i += 2;
        uint32_t astart = i;
        while (i < n && url[i] != '/' && url[i] != '?' && url[i] != '#') i++;
        uint32_t aend = i;

        /* strip userinfo */
        uint32_t hstart = astart;
        for (uint32_t k = astart; k < aend; k++) {
            if (url[k] == '@') hstart = k + 1;
        }

        /* split host[:port]; tolerate a bracketed IPv6 literal */
        uint32_t hend = aend;
        uint32_t pstart = 0;
        bool have_port = false;
        if (hstart < aend && url[hstart] == '[') {
            uint32_t k = hstart;
            while (k < aend && url[k] != ']') k++;
            if (k >= aend) return BROWSER_ERR_BAD_URL;   /* unterminated [ */
            hend = k + 1;
            if (hend < aend && url[hend] == ':') { have_port = true; pstart = hend + 1; }
            else if (hend != aend) return BROWSER_ERR_BAD_URL;
        } else {
            for (uint32_t k = hstart; k < aend; k++) {
                if (url[k] == ':') { hend = k; have_port = true; pstart = k + 1; break; }
            }
        }

        if (hend <= hstart) return BROWSER_ERR_BAD_URL;  /* "http://" or "http://:80" */
        if (!b_copyn(out->host, sizeof(out->host), url + hstart, hend - hstart))
            return BROWSER_ERR_BAD_URL;
        b_lower_str(out->host);
        if (!host_is_sane(out->host)) return BROWSER_ERR_BAD_URL;

        if (have_port) {
            if (pstart >= aend) {
                out->port = scheme_default_port(out->scheme);   /* "host:" */
            } else {
                uint32_t v = 0;
                for (uint32_t k = pstart; k < aend; k++) {
                    if (!b_is_digit(url[k])) return BROWSER_ERR_BAD_URL;
                    v = v * 10 + (uint32_t)(url[k] - '0');
                    if (v > 65535) return BROWSER_ERR_BAD_URL;
                }
                if (v == 0) return BROWSER_ERR_BAD_URL;
                out->port = (uint16_t)v;
            }
        } else {
            out->port = scheme_default_port(out->scheme);
        }
    }

    /* ---- path / query / fragment ---- */
    uint32_t pstart = i;
    while (i < n && url[i] != '?' && url[i] != '#') i++;
    uint32_t pend = i;

    if (pend > pstart) {
        if (!b_copyn(out->path, sizeof(out->path), url + pstart, pend - pstart))
            return BROWSER_ERR_BAD_URL;
    } else if (have_authority) {
        out->path[0] = '/'; out->path[1] = '\0';
    }

    if (i < n && url[i] == '?') {
        i++;
        uint32_t qs = i;
        while (i < n && url[i] != '#') i++;
        if (!b_copyn(out->query, sizeof(out->query), url + qs, i - qs))
            return BROWSER_ERR_BAD_URL;
    }
    if (i < n && url[i] == '#') {
        i++;
        if (!b_copyn(out->fragment, sizeof(out->fragment), url + i, n - i))
            return BROWSER_ERR_BAD_URL;
    }

    if (!field_is_sane(out->path) || !field_is_sane(out->query) ||
        !field_is_sane(out->fragment))
        return BROWSER_ERR_BAD_URL;

    /* An authority always implies an absolute path. */
    if (have_authority && out->path[0] != '/') {
        /* only reachable when the path was empty; b_copyn above set it */
        out->path[0] = '/'; out->path[1] = '\0';
    }

    out->tls      = b_ieq(out->scheme, "https") || b_ieq(out->scheme, "wss");
    out->absolute = (out->scheme[0] != '\0') && (out->host[0] != '\0');
    out->valid    = true;
    return BROWSER_OK;
}

int browser_parse_url(const char *url, browser_url_t *out) {
    if (!out) return BROWSER_ERR_ARG;
    int rc = parse_url_inner(url, out);
    /* The header promises a zeroed `out` on failure. parse_url_inner() bails
     * from the middle of the split with the fields it had already filled in
     * (host, and a path still carrying whatever CRLF payload got it rejected),
     * so the zeroing has to happen HERE, not only on entry. */
    if (rc != BROWSER_OK) b_memset(out, 0, sizeof(*out));
    return rc;
}

/* RFC 3986 §5.2.4 remove_dot_segments, iterative and bounded.
 *
 * ".." pops by scanning the OUTPUT back to the previous '/', so the only
 * limit on how deep a path may be is `cap`. A previous revision kept a
 * 64-entry stack of segment offsets and, once that stack saturated, a single
 * ".." unwound sixteen or more segments at once — silently returning a
 * DIFFERENT path with no error. `floor_o` is the offset just past the leading
 * '/', which ".." must never pop through (RFC: an absolute path stays
 * absolute however many ".." segments precede it). */
static void url_remove_dots(const char *in, char *out, uint32_t cap) {
    uint32_t o = 0, i = 0, floor_o = 0;
    if (cap == 0) return;
    out[0] = '\0';
    if (in[0] == '/') { if (o + 1 < cap) out[o++] = '/'; i = 1; floor_o = o; }
    while (in[i]) {
        uint32_t s = i;
        while (in[i] && in[i] != '/') i++;
        uint32_t seglen = i - s;
        bool had_slash = (in[i] == '/');
        if (had_slash) i++;
        bool last = (in[i] == '\0');

        if (seglen == 1 && in[s] == '.') {
            if (last && !had_slash && o > 0 && out[o - 1] != '/' && o + 1 < cap)
                out[o++] = '/';
            continue;
        }
        if (seglen == 2 && in[s] == '.' && in[s + 1] == '.') {
            if (o > floor_o) {
                if (out[o - 1] == '/') o--;                  /* off the separator */
                while (o > floor_o && out[o - 1] != '/') o--; /* to the previous '/' */
            }
            if (last && !had_slash && o > 0 && out[o - 1] != '/' && o + 1 < cap)
                out[o++] = '/';
            continue;
        }
        for (uint32_t k = 0; k < seglen && o + 1 < cap; k++) out[o++] = in[s + k];
        if (had_slash && o + 1 < cap) out[o++] = '/';
    }
    out[o < cap ? o : cap - 1] = '\0';
}

/* Returns false if the URL did not fit in `cap`. A caller that ignored this
 * would hand back a PREFIX of a URL — a different, usually still-fetchable
 * address — under a success code. */
static bool url_serialize(const browser_url_t *u, char *out, uint32_t cap) {
    b_app_t a;
    app_init(&a, out, cap);
    if (u->scheme[0]) { app_str(&a, u->scheme); app_ch(&a, ':'); }
    if (u->host[0]) {
        app_str(&a, "//");
        app_str(&a, u->host);
        uint16_t dflt = scheme_default_port(u->scheme);
        if (u->port != 0 && u->port != dflt) { app_ch(&a, ':'); app_u32(&a, u->port); }
    }
    if (u->path[0]) app_str(&a, u->path);
    else if (u->host[0]) app_ch(&a, '/');
    if (u->query[0])    { app_ch(&a, '?'); app_str(&a, u->query); }
    if (u->fragment[0]) { app_ch(&a, '#'); app_str(&a, u->fragment); }
    return !a.ovf;
}

int browser_resolve_url(const char *base, const char *ref, char *out, uint32_t cap) {
    if (!base || !ref || !out || cap == 0) return BROWSER_ERR_ARG;
    out[0] = '\0';

    browser_url_t b;
    if (browser_parse_url(base, &b) != BROWSER_OK) return BROWSER_ERR_BAD_URL;
    if (!b.absolute) return BROWSER_ERR_BAD_URL;

    uint32_t rn = b_strlen(ref);
    browser_url_t t = b;

    if (rn == 0) {
        t.fragment[0] = '\0';
    } else if (ref[0] == '#') {
        if (!b_copy(t.fragment, sizeof(t.fragment), ref + 1)) return BROWSER_ERR_BAD_URL;
    } else {
        browser_url_t r;
        if (browser_parse_url(ref, &r) != BROWSER_OK) return BROWSER_ERR_BAD_URL;

        if (r.scheme[0]) {
            /* RFC 3986 §5.2.2 first branch: a reference that CARRIES A SCHEME
             * is already the target, authority or not. Testing r.absolute
             * (scheme AND host) instead sent every opaque reference —
             * "mailto:x", "javascript:x", "data:x", the RFC's own "g:h" —
             * down the path-merge branch, which rewrote it into
             * "http://<base-host>/<base-dir>/x": a fetchable URL on the base's
             * own origin that the document never asked for. */
            t = r;
            char tmp[BROWSER_MAX_URL_LEN];
            url_remove_dots(t.path, tmp, sizeof(tmp));
            if (!b_copy(t.path, sizeof(t.path), tmp)) return BROWSER_ERR_BAD_URL;
        } else if (r.host[0]) {
            /* scheme-relative "//host/path" — inherit only the scheme */
            t = r;
            b_copy(t.scheme, sizeof(t.scheme), b.scheme);
            t.tls = b.tls;
            if (t.port == 0) t.port = scheme_default_port(t.scheme);
            t.absolute = true;
        } else if (ref[0] == '?') {
            b_copy(t.query, sizeof(t.query), r.query);
            b_copy(t.fragment, sizeof(t.fragment), r.fragment);
        } else if (r.path[0] == '/') {
            char tmp[BROWSER_MAX_URL_LEN];
            url_remove_dots(r.path, tmp, sizeof(tmp));
            b_copy(t.path, sizeof(t.path), tmp);
            b_copy(t.query, sizeof(t.query), r.query);
            b_copy(t.fragment, sizeof(t.fragment), r.fragment);
        } else if (r.path[0] == '\0') {
            b_copy(t.query, sizeof(t.query), r.query);
            b_copy(t.fragment, sizeof(t.fragment), r.fragment);
        } else {
            /* merge with the base path's directory */
            char merged[BROWSER_MAX_URL_LEN];
            b_app_t a;
            app_init(&a, merged, sizeof(merged));
            uint32_t bl = b_strlen(b.path);
            uint32_t cut = 0;
            for (uint32_t k = 0; k < bl; k++) if (b.path[k] == '/') cut = k + 1;
            if (cut == 0) app_ch(&a, '/');
            else app_mem(&a, b.path, cut);
            app_str(&a, r.path);
            if (a.ovf) return BROWSER_ERR_BAD_URL;

            char tmp[BROWSER_MAX_URL_LEN];
            url_remove_dots(merged, tmp, sizeof(tmp));
            b_copy(t.path, sizeof(t.path), tmp);
            b_copy(t.query, sizeof(t.query), r.query);
            b_copy(t.fragment, sizeof(t.fragment), r.fragment);
        }
    }

    /* A URL that did not fit is a DIFFERENT URL, and usually still a fetchable
     * one ("http://a/b/c/g" clipped to "http://" or "http://a"). Returning
     * BROWSER_OK with the prefix would be a silent redirect, so refuse. */
    if (!url_serialize(&t, out, cap)) { out[0] = '\0'; return BROWSER_ERR_FULL; }
    if (out[0] == '\0') return BROWSER_ERR_BAD_URL;
    return BROWSER_OK;
}

/* ===================================================================== */
/* HTTP — request building                                                */
/* ===================================================================== */

static const char *method_name(http_method_t m) {
    switch (m) {
        case HTTP_GET:     return "GET";
        case HTTP_POST:    return "POST";
        case HTTP_PUT:     return "PUT";
        case HTTP_DELETE:  return "DELETE";
        case HTTP_HEAD:    return "HEAD";
        case HTTP_OPTIONS: return "OPTIONS";
        default:           return (const char *)0;
    }
}

int http_build_request(char *out, uint32_t cap, http_method_t method,
                       const browser_url_t *url, const char *user_agent,
                       const char *body, uint32_t body_len) {
    if (!out || cap == 0 || !url) return BROWSER_ERR_ARG;
    const char *mn = method_name(method);
    if (!mn) return BROWSER_ERR_ARG;
    if (!url->valid || !url->absolute) return BROWSER_ERR_BAD_URL;
    if (!b_ieq(url->scheme, "http") && !b_ieq(url->scheme, "https"))
        return BROWSER_ERR_BAD_URL;
    if (!host_is_sane(url->host)) return BROWSER_ERR_BAD_URL;
    if (body_len > 0 && !body) return BROWSER_ERR_ARG;

    b_app_t a;
    app_init(&a, out, cap);

    app_str(&a, mn);
    app_ch(&a, ' ');
    app_str(&a, url->path[0] ? url->path : "/");
    if (url->query[0]) { app_ch(&a, '?'); app_str(&a, url->query); }
    app_str(&a, " HTTP/1.1\r\n");

    /* Host carries the port only when it is not the scheme default. */
    app_str(&a, "Host: ");
    app_str(&a, url->host);
    {
        uint16_t dflt = scheme_default_port(url->scheme);
        if (url->port != 0 && url->port != dflt) {
            app_ch(&a, ':');
            app_u32(&a, url->port);
        }
    }
    app_str(&a, "\r\n");

    app_str(&a, "User-Agent: ");
    app_str(&a, (user_agent && user_agent[0]) ? user_agent : "ZXV-Browser/1.0");
    app_str(&a, "\r\n");
    app_str(&a, "Accept: */*\r\n");
    app_str(&a, "Connection: close\r\n");

    if (body_len > 0) {
        app_str(&a, "Content-Length: ");
        app_u32(&a, body_len);
        app_str(&a, "\r\n");
    }
    app_str(&a, "\r\n");
    if (body_len > 0) app_mem(&a, body, body_len);

    if (a.ovf) return BROWSER_ERR_FULL;
    return (int)a.len;
}

/* ===================================================================== */
/* HTTP — response parsing                                                */
/* ===================================================================== */

/* Find the end of the header block. Accepts CRLFCRLF and the bare-LF variant
 * some embedded servers emit. Returns the offset of the first body byte, or
 * 0 when no terminator is present in the buffer. */
static uint32_t find_header_end(const uint8_t *b, uint32_t n, uint32_t from) {
    for (uint32_t i = from; i < n; i++) {
        if (b[i] == '\n') {
            if (i + 1 < n && b[i + 1] == '\n') return i + 2;
            if (i + 2 < n && b[i + 1] == '\r' && b[i + 2] == '\n') return i + 3;
        }
    }
    return 0;
}

/* Copy the value of `name` out of a raw header block. Returns length. */
static uint32_t header_scan(const char *blk, uint32_t blklen, const char *name,
                            char *out, uint32_t cap) {
    uint32_t nlen = b_strlen(name);
    if (out && cap) out[0] = '\0';
    if (nlen == 0) return 0;

    uint32_t i = 0;
    while (i < blklen) {
        uint32_t ls = i;
        while (i < blklen && blk[i] != '\n') i++;
        uint32_t le = i;                       /* exclusive, before '\n' */
        if (le > ls && blk[le - 1] == '\r') le--;
        if (i < blklen) i++;                   /* step over '\n' */

        if (le == ls) continue;                /* blank line */
        /* name: value */
        uint32_t c = ls;
        while (c < le && blk[c] != ':') c++;
        if (c >= le) continue;                 /* no colon — not a header */
        uint32_t namelen = c - ls;
        if (namelen != nlen) continue;
        if (!b_ieq_len(blk + ls, namelen, name)) continue;

        uint32_t v = c + 1;
        while (v < le && (blk[v] == ' ' || blk[v] == '\t')) v++;
        uint32_t vend = le;
        while (vend > v && (blk[vend - 1] == ' ' || blk[vend - 1] == '\t')) vend--;

        uint32_t got = 0;
        if (out && cap) {
            for (; got + 1 < cap && v + got < vend; got++) out[got] = blk[v + got];
        } else {
            got = vend - v;
        }

        /* RFC 7230 §3.2.4 obs-fold: a following line that starts with SP or
         * HTAB CONTINUES this field, and a user agent must replace the fold
         * with a space. Skipping the continuation instead returned "part1"
         * for "X-Long: part1 / <SP>part2" — and for Content-Length that is not
         * cosmetic: "Content-Length: 2\r\n  0000" would frame a 2-byte body
         * off a header the server did not write. Unfolded here, that same
         * input becomes the non-numeric "2 0000" and the message is rejected. */
        while (i < blklen && (blk[i] == ' ' || blk[i] == '\t')) {
            uint32_t cs = i;
            while (i < blklen && blk[i] != '\n') i++;
            uint32_t ce = i;
            if (ce > cs && blk[ce - 1] == '\r') ce--;
            if (i < blklen) i++;
            while (cs < ce && (blk[cs] == ' ' || blk[cs] == '\t')) cs++;
            while (ce > cs && (blk[ce - 1] == ' ' || blk[ce - 1] == '\t')) ce--;
            if (out && cap) {
                if (got + 1 < cap) out[got++] = ' ';
                for (uint32_t k = cs; k < ce && got + 1 < cap; k++) out[got++] = blk[k];
            } else {
                got += 1 + (ce - cs);
            }
        }
        if (out && cap) out[got] = '\0';
        return got;
    }
    return 0;
}

/* RFC 7230 §3.3.3: a message carrying two Content-Length fields with
 * DIFFERING values has invalid framing and must be treated as an
 * unrecoverable error. Silently taking the first (which is what a plain
 * "find the header" scan does) is the response-splitting primitive: one
 * length for us, another for anything else reading the same bytes.
 * Returns true when `name` occurs more than once with values that differ. */
static bool header_conflicts(const char *blk, uint32_t blklen, const char *name) {
    uint32_t nlen = b_strlen(name);
    if (nlen == 0) return false;

    bool seen = false;
    char first[64];
    first[0] = '\0';

    uint32_t i = 0;
    while (i < blklen) {
        uint32_t ls = i;
        while (i < blklen && blk[i] != '\n') i++;
        uint32_t le = i;
        if (le > ls && blk[le - 1] == '\r') le--;
        if (i < blklen) i++;
        if (le == ls) continue;

        uint32_t c = ls;
        while (c < le && blk[c] != ':') c++;
        if (c >= le) continue;
        if (c - ls != nlen) continue;
        if (!b_ieq_len(blk + ls, nlen, name)) continue;

        uint32_t v = c + 1;
        while (v < le && (blk[v] == ' ' || blk[v] == '\t')) v++;
        uint32_t vend = le;
        while (vend > v && (blk[vend - 1] == ' ' || blk[vend - 1] == '\t')) vend--;

        char cur[64];
        b_copyn(cur, sizeof(cur), blk + v, vend - v);
        if (!seen) { b_copy(first, sizeof(first), cur); seen = true; }
        else if (!b_ieq(first, cur)) return true;
    }
    return false;
}

uint32_t http_get_header(const http_response_t *resp, const char *name,
                         char *out, uint32_t cap) {
    if (!resp || !name) { if (out && cap) out[0] = '\0'; return 0; }
    return header_scan(resp->headers, b_strlen(resp->headers), name, out, cap);
}

static bool value_has_token(const char *v, const char *tok) {
    uint32_t n = b_strlen(v), t = b_strlen(tok);
    if (t == 0 || n < t) return false;
    for (uint32_t i = 0; i + t <= n; i++) {
        if (b_ieq_len(v + i, t, tok)) {
            bool lb = (i == 0) || v[i - 1] == ',' || v[i - 1] == ' ' || v[i - 1] == '\t';
            char after = v[i + t];
            bool rb = (after == '\0' || after == ',' || after == ' ' ||
                       after == '\t' || after == ';');
            if (lb && rb) return true;
        }
    }
    return false;
}

/* De-frame a chunked body in place. `buf` spans [start,end); the decoded body
 * is written back starting at `start`. Returns BROWSER_OK with *outlen set,
 * BROWSER_ERR_TRUNCATED if the stream ends mid-message, BROWSER_ERR_PROTOCOL
 * if a chunk header is not hex or a chunk is not CRLF-terminated. */
static int dechunk(uint8_t *buf, uint32_t start, uint32_t end, uint32_t *outlen) {
    uint32_t p = start, w = start;
    *outlen = 0;
    for (;;) {
        /* chunk size, hex, optional ";ext", terminated by LF */
        uint32_t ls = p;
        while (p < end && buf[p] != '\n') p++;
        if (p >= end) return BROWSER_ERR_TRUNCATED;
        uint32_t le = p;
        if (le > ls && buf[le - 1] == '\r') le--;
        p++;                                   /* past LF */

        uint32_t k = ls;
        uint64_t size = 0;
        uint32_t digits = 0;
        while (k < le && buf[k] != ';' && buf[k] != ' ' && buf[k] != '\t') {
            int hv = b_hexval((char)buf[k]);
            if (hv < 0) return BROWSER_ERR_PROTOCOL;
            size = size * 16u + (uint64_t)hv;
            if (size > (uint64_t)BROWSER_MAX_PAGE_SIZE) return BROWSER_ERR_TOO_LARGE;
            digits++; k++;
        }
        if (digits == 0) return BROWSER_ERR_PROTOCOL;

        if (size == 0) {
            /* trailer section: lines until a blank one */
            for (;;) {
                if (p >= end) return BROWSER_ERR_TRUNCATED;
                uint32_t ts = p;
                while (p < end && buf[p] != '\n') p++;
                if (p >= end) return BROWSER_ERR_TRUNCATED;
                uint32_t te = p;
                if (te > ts && buf[te - 1] == '\r') te--;
                p++;
                if (te == ts) break;           /* blank line: done */
            }
            *outlen = w - start;
            return BROWSER_OK;
        }

        uint32_t sz = (uint32_t)size;
        if (p > end || end - p < sz) return BROWSER_ERR_TRUNCATED;
        b_memmove_fwd(buf + w, buf + p, sz);
        w += sz;
        p += sz;

        /* CRLF after chunk data */
        if (p >= end) return BROWSER_ERR_TRUNCATED;
        if (buf[p] == '\r') {
            p++;
            if (p >= end) return BROWSER_ERR_TRUNCATED;
        }
        if (buf[p] != '\n') return BROWSER_ERR_PROTOCOL;
        p++;
    }
}

int http_parse_response(http_response_t *resp, uint8_t *buf, uint32_t len) {
    if (!resp) return BROWSER_ERR_ARG;
    b_memset(resp, 0, sizeof(*resp));
    if (!buf) return BROWSER_ERR_ARG;
    if (len == 0) return BROWSER_ERR_TRUNCATED;

    /* ---- status line ---- */
    uint32_t i = 0;
    while (i < len && buf[i] != '\n') i++;
    if (i >= len) return BROWSER_ERR_TRUNCATED;
    uint32_t sl_end = i;
    if (sl_end > 0 && buf[sl_end - 1] == '\r') sl_end--;

    /* "HTTP/1.x SP ddd [SP reason]" */
    if (sl_end < 12) return BROWSER_ERR_PROTOCOL;
    if (!b_ieq_len((const char *)buf, 5, "HTTP/")) return BROWSER_ERR_PROTOCOL;
    if (!b_is_digit((char)buf[5]) || buf[6] != '.' || !b_is_digit((char)buf[7]))
        return BROWSER_ERR_PROTOCOL;
    if (buf[8] != ' ') return BROWSER_ERR_PROTOCOL;
    if (!b_is_digit((char)buf[9]) || !b_is_digit((char)buf[10]) || !b_is_digit((char)buf[11]))
        return BROWSER_ERR_PROTOCOL;
    resp->status_code = (uint16_t)((buf[9] - '0') * 100 + (buf[10] - '0') * 10 +
                                   (buf[11] - '0'));
    {
        uint32_t rs = 12;
        if (rs < sl_end && buf[rs] == ' ') rs++;
        uint32_t rn = (sl_end > rs) ? (sl_end - rs) : 0;
        b_copyn(resp->status_text, sizeof(resp->status_text),
                (const char *)(buf + rs), rn);
    }

    /* ---- header block ---- */
    uint32_t hdr_start = i + 1;
    uint32_t body_start = find_header_end(buf, len, hdr_start > 0 ? hdr_start - 1 : 0);
    if (body_start == 0) return BROWSER_ERR_TRUNCATED;
    uint32_t hdr_len = (body_start > hdr_start) ? (body_start - hdr_start) : 0;

    b_copyn(resp->headers, sizeof(resp->headers), (const char *)(buf + hdr_start),
            hdr_len);
    /* NOTE: only the first sizeof(headers)-1 bytes are retained; the framing
     * headers below are read from the ORIGINAL buffer, so a large header block
     * cannot change how the body is framed — it only limits http_get_header.
     *
     * When the retained copy IS short, http_get_header() would answer 0 ("not
     * present") for a header that is present on the wire. That is a silent
     * false negative, so it is recorded: headers_truncated says "0 from
     * http_get_header() means UNKNOWN here, not ABSENT". A NUL byte inside the
     * block cuts the copy short for the same reason and counts the same way. */
    resp->headers_truncated = (hdr_len + 1 > sizeof(resp->headers));
    if (!resp->headers_truncated) {
        for (uint32_t k = 0; k < hdr_len; k++) {
            if (buf[hdr_start + k] == 0) { resp->headers_truncated = true; break; }
        }
    }

    char te[64];
    char cl[32];
    header_scan((const char *)(buf + hdr_start), hdr_len, "Content-Type",
                resp->content_type, sizeof(resp->content_type));
    header_scan((const char *)(buf + hdr_start), hdr_len, "Transfer-Encoding",
                te, sizeof(te));
    uint32_t cl_len = header_scan((const char *)(buf + hdr_start), hdr_len,
                                  "Content-Length", cl, sizeof(cl));

    bool have_cl = false;
    if (cl_len > 0) {
        if (header_conflicts((const char *)(buf + hdr_start), hdr_len, "Content-Length"))
            return BROWSER_ERR_PROTOCOL;
        uint64_t v = 0;
        bool ok = true;
        for (uint32_t k = 0; cl[k]; k++) {
            if (!b_is_digit(cl[k])) { ok = false; break; }
            v = v * 10u + (uint64_t)(cl[k] - '0');
            if (v > 0xFFFFFFFFull) { ok = false; break; }
        }
        if (!ok) return BROWSER_ERR_PROTOCOL;
        resp->content_length = (uint32_t)v;
        have_cl = true;
    }
    resp->chunked = value_has_token(te, "chunked");

    uint32_t avail = len - body_start;
    resp->body = buf + body_start;

    /* 1xx / 204 / 304 and HEAD-shaped replies carry no body at all. */
    if (resp->status_code / 100 == 1 || resp->status_code == 204 ||
        resp->status_code == 304) {
        resp->body_len = 0;
        resp->success = true;
        return BROWSER_OK;
    }

    if (resp->chunked) {
        uint32_t decoded = 0;
        int rc = dechunk(buf, body_start, len, &decoded);
        if (rc != BROWSER_OK) {
            resp->body_len = 0;
            resp->success = false;
            return rc;
        }
        resp->body_len = decoded;
        resp->content_length = decoded;
        resp->success = true;
        return BROWSER_OK;
    }

    if (have_cl) {
        if (avail < resp->content_length) {
            resp->body_len = avail;         /* what we do have, honestly */
            resp->success = false;
            return BROWSER_ERR_TRUNCATED;
        }
        resp->body_len = resp->content_length;
        resp->success = true;
        return BROWSER_OK;
    }

    /* No framing headers: RFC 7230 §3.3.3 case 7 — the body runs to close. */
    resp->body_len = avail;
    resp->content_length = avail;
    resp->success = true;
    return BROWSER_OK;
}

/* ===================================================================== */
/* HTTP — transport binding and the receive pool                          */
/* ===================================================================== */

static const http_transport_ops_t *g_transport;
static uint8_t g_rx_pool[BROWSER_HTTP_POOL_SLOTS][BROWSER_HTTP_RX_BUF];
static bool    g_rx_used[BROWSER_HTTP_POOL_SLOTS];

void http_bind_transport(const http_transport_ops_t *ops) {
    g_transport = ops;
}

bool http_transport_bound(void) {
    return g_transport != (const http_transport_ops_t *)0 &&
           g_transport->open && g_transport->send && g_transport->recv;
}

static int pool_acquire(void) {
    for (uint32_t s = 0; s < BROWSER_HTTP_POOL_SLOTS; s++) {
        if (!g_rx_used[s]) { g_rx_used[s] = true; return (int)s; }
    }
    return -1;
}

static void pool_release(int slot) {
    if (slot >= 0 && (uint32_t)slot < BROWSER_HTTP_POOL_SLOTS)
        g_rx_used[slot] = false;
}

static int pool_slot_of(const uint8_t *p) {
    for (uint32_t s = 0; s < BROWSER_HTTP_POOL_SLOTS; s++) {
        if (p >= g_rx_pool[s] && p < g_rx_pool[s] + BROWSER_HTTP_RX_BUF)
            return (int)s;
    }
    return -1;
}

void http_response_free(http_response_t *resp) {
    if (!resp) return;
    if (resp->body) {
        int slot = pool_slot_of(resp->body);
        if (slot >= 0) pool_release(slot);
    }
    b_memset(resp, 0, sizeof(*resp));
}

int http_request(http_response_t *resp, http_method_t method, const char *url,
                 const char *body, uint32_t body_len) {
    if (!resp) return BROWSER_ERR_ARG;
    b_memset(resp, 0, sizeof(*resp));
    if (!url) return BROWSER_ERR_ARG;
    if (body_len > 0 && !body) return BROWSER_ERR_ARG;

    browser_url_t u;
    int rc = browser_parse_url(url, &u);
    if (rc != BROWSER_OK) return rc;
    if (!u.absolute) return BROWSER_ERR_BAD_URL;
    if (!b_ieq(u.scheme, "http") && !b_ieq(u.scheme, "https"))
        return BROWSER_ERR_BAD_URL;

    char req[BROWSER_HTTP_MAX_REQ];
    int reqlen = http_build_request(req, sizeof(req), method, &u,
                                    "ZXV-Browser/1.0", body, body_len);
    if (reqlen < 0) return reqlen;

    /* THE HARDWARE BOUNDARY. Nothing below this point can be faked. */
    if (!http_transport_bound()) return BROWSER_ERR_NO_TRANSPORT;

    int slot = pool_acquire();
    if (slot < 0) return BROWSER_ERR_FULL;
    uint8_t *rx = g_rx_pool[slot];

    const http_transport_ops_t *t = g_transport;
    int conn = t->open(t->ctx, u.host, u.port ? u.port : (u.tls ? 443 : 80), u.tls);
    if (conn < 0) { pool_release(slot); return BROWSER_ERR_IO; }

    int sent = t->send(t->ctx, conn, (const uint8_t *)req, (uint32_t)reqlen);
    if (sent != reqlen) {
        if (t->close) t->close(t->ctx, conn);
        pool_release(slot);
        return BROWSER_ERR_IO;
    }

    uint32_t total = 0;
    for (;;) {
        if (total >= BROWSER_HTTP_RX_BUF) break;
        int got = t->recv(t->ctx, conn, rx + total, BROWSER_HTTP_RX_BUF - total);
        if (got < 0) {
            if (t->close) t->close(t->ctx, conn);
            pool_release(slot);
            return BROWSER_ERR_IO;
        }
        if (got == 0) break;                       /* peer closed */
        if ((uint32_t)got > BROWSER_HTTP_RX_BUF - total) {
            /* a transport that lies about how much it wrote */
            if (t->close) t->close(t->ctx, conn);
            pool_release(slot);
            return BROWSER_ERR_IO;
        }
        total += (uint32_t)got;
    }
    if (t->close) t->close(t->ctx, conn);

    if (total == 0) { pool_release(slot); return BROWSER_ERR_IO; }
    if (total >= BROWSER_HTTP_RX_BUF) {
        /* The slot is exactly full, so we cannot tell a complete response from
         * a clipped one. Refusing is the only honest answer. */
        pool_release(slot);
        return BROWSER_ERR_TOO_LARGE;
    }

    rc = http_parse_response(resp, rx, total);
    if (rc != BROWSER_OK) {
        b_memset(resp, 0, sizeof(*resp));
        pool_release(slot);
        return rc;
    }
    /* resp->body now borrows the slot; http_response_free() returns it. */
    return BROWSER_OK;
}

/* ===================================================================== */
/* HTML tokenizer                                                         */
/* ===================================================================== */

typedef struct {
    const char *name;
    html_elem_type_t type;
} tag_map_t;

static const tag_map_t g_tags[] = {
    { "html", HTML_HTML }, { "head", HTML_HEAD }, { "body", HTML_BODY },
    { "title", HTML_TITLE }, { "meta", HTML_META }, { "link", HTML_LINK },
    { "script", HTML_SCRIPT }, { "style", HTML_STYLE },
    { "div", HTML_DIV }, { "span", HTML_SPAN }, { "p", HTML_P },
    { "br", HTML_BR }, { "hr", HTML_HR },
    { "h1", HTML_H1 }, { "h2", HTML_H2 }, { "h3", HTML_H3 },
    { "h4", HTML_H4 }, { "h5", HTML_H5 }, { "h6", HTML_H6 },
    { "a", HTML_A }, { "img", HTML_IMG }, { "table", HTML_TABLE },
    { "tr", HTML_TR }, { "td", HTML_TD }, { "th", HTML_TH },
    { "ul", HTML_UL }, { "ol", HTML_OL }, { "li", HTML_LI },
    { "form", HTML_FORM }, { "input", HTML_INPUT }, { "button", HTML_BUTTON },
};

html_elem_type_t html_tag_type(const char *tag) {
    if (!tag || !tag[0]) return HTML_UNKNOWN;
    for (uint32_t i = 0; i < sizeof(g_tags) / sizeof(g_tags[0]); i++) {
        if (b_ieq(tag, g_tags[i].name)) return g_tags[i].type;
    }
    return HTML_UNKNOWN;
}

static bool tag_is_void(const char *t) {
    static const char *v[] = { "area", "base", "br", "col", "embed", "hr", "img",
                               "input", "link", "meta", "param", "source",
                               "track", "wbr" };
    for (uint32_t i = 0; i < sizeof(v) / sizeof(v[0]); i++)
        if (b_ieq(t, v[i])) return true;
    return false;
}

static bool tag_is_rawtext(const char *t) {
    return b_ieq(t, "script") || b_ieq(t, "style");
}

static bool type_is_block(html_elem_type_t t) {
    switch (t) {
        case HTML_HTML: case HTML_HEAD: case HTML_BODY:
        case HTML_DIV: case HTML_P: case HTML_HR:
        case HTML_H1: case HTML_H2: case HTML_H3:
        case HTML_H4: case HTML_H5: case HTML_H6:
        case HTML_TABLE: case HTML_TR: case HTML_TD: case HTML_TH:
        case HTML_UL: case HTML_OL: case HTML_LI: case HTML_FORM:
        case HTML_STYLE: case HTML_SCRIPT: case HTML_TITLE:
            return true;
        default:
            return false;
    }
}

static int32_t heading_font(html_elem_type_t t) {
    switch (t) {
        case HTML_H1: return 32;
        case HTML_H2: return 28;
        case HTML_H3: return 24;
        case HTML_H4: return 20;
        case HTML_H5: return 18;
        case HTML_H6: return 16;
        default:      return 0;
    }
}

typedef struct {
    int32_t font_size;
    bool bold, italic, underline;
    int32_t link_elem;      /* index of the enclosing <a>, or -1 */
} html_style_t;

typedef struct {
    const char *s;
    uint32_t n;
    html_element_t *els;
    uint32_t max;
    uint32_t count;
    bool full;

    char open_tag[BROWSER_HTML_MAX_DEPTH][BROWSER_MAX_TAG_LEN];
    html_style_t style[BROWSER_HTML_MAX_DEPTH + 1];
    uint32_t depth;
    uint32_t over_depth;    /* opens that could not be pushed (depth clamp) */
} htmlp_t;

static html_element_t *hp_emit(htmlp_t *p) {
    if (p->count >= p->max) { p->full = true; return (html_element_t *)0; }
    html_element_t *e = &p->els[p->count];
    b_memset(e, 0, sizeof(*e));
    p->count++;
    return e;
}

static void hp_apply_style(html_element_t *e, const html_style_t *st) {
    e->font_size = st->font_size;
    e->bold = st->bold;
    e->italic = st->italic;
    e->underline = st->underline;
}

/* Forward decl: the text accumulator is defined below, but both flush sites
 * (mid-document and end-of-input) must go through ONE function. They used to
 * be two copies, and the end-of-input copy had lost the <a> inheritance — so
 * "<a href=/x>tail" with no closing tag produced text with is_link false and
 * an empty href, while the identical text before a "</a>" got both. */
typedef struct textacc textacc_t;
static void hp_flush_text(htmlp_t *p, textacc_t *ta);

/* UTF-8 encode. Returns bytes written (0..4). */
static uint32_t utf8_put(uint32_t cp, char *out) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

typedef struct { const char *name; uint32_t cp; } entity_t;

static const entity_t g_entities[] = {
    { "amp", 38 }, { "lt", 60 }, { "gt", 62 }, { "quot", 34 },
    { "apos", 39 }, { "nbsp", 160 }, { "copy", 169 }, { "reg", 174 },
    { "hellip", 0x2026 }, { "mdash", 0x2014 }, { "ndash", 0x2013 },
    { "middot", 0xB7 }, { "times", 0xD7 }, { "trade", 0x2122 },
};

/* Decode an entity starting at s[i] == '&'. On success writes UTF-8 to `out`
 * (>= 4 bytes), sets *outn, and returns the number of INPUT bytes consumed.
 * Returns 0 when this is not a well-formed entity — the caller then emits a
 * literal '&', which is exactly what a browser does. */
static uint32_t entity_decode(const char *s, uint32_t n, uint32_t i,
                              char *out, uint32_t *outn) {
    if (i >= n || s[i] != '&') return 0;
    uint32_t j = i + 1;
    uint32_t limit = i + 34;
    if (limit > n) limit = n;

    if (j < limit && s[j] == '#') {
        j++;
        uint32_t cp = 0, digits = 0;
        bool oob = false;
        if (j < limit && (s[j] == 'x' || s[j] == 'X')) {
            j++;
            while (j < limit && b_hexval(s[j]) >= 0) {
                if (!oob) {
                    cp = cp * 16u + (uint32_t)b_hexval(s[j]);
                    if (cp > 0x10FFFF) oob = true;
                }
                j++; digits++;
            }
        } else {
            while (j < limit && b_is_digit(s[j])) {
                if (!oob) {
                    cp = cp * 10u + (uint32_t)(s[j] - '0');
                    if (cp > 0x10FFFF) oob = true;
                }
                j++; digits++;
            }
        }
        if (digits == 0 || j >= limit || s[j] != ';') return 0;
        /* HTML5 "numeric character reference end state": NUL, a lone surrogate
         * and anything above U+10FFFF all become U+FFFD. Encoding a surrogate
         * directly produced ED A0 80 — CESU-8, not UTF-8 — from a header that
         * promises UTF-8 output. */
        if (oob || cp == 0 || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
        *outn = utf8_put(cp, out);
        return (j + 1) - i;
    }

    uint32_t ns = j;
    while (j < limit && (b_is_alpha(s[j]) || b_is_digit(s[j]))) j++;
    if (j >= limit || s[j] != ';' || j == ns) return 0;
    uint32_t namelen = j - ns;
    for (uint32_t k = 0; k < sizeof(g_entities) / sizeof(g_entities[0]); k++) {
        if (b_ieq_len(s + ns, namelen, g_entities[k].name)) {
            *outn = utf8_put(g_entities[k].cp, out);
            return (j + 1) - i;
        }
    }
    return 0;
}

/* Text accumulator with whitespace collapsing + entity decoding. */
struct textacc {
    char buf[BROWSER_MAX_TEXT_LEN];
    uint32_t len;
    bool trunc;
    bool last_space;
    bool has_nonspace;
    bool saw_newline;
};

static void ta_reset(textacc_t *t) {
    t->len = 0; t->trunc = false; t->last_space = false;
    t->has_nonspace = false; t->saw_newline = false;
    t->buf[0] = '\0';
}

static void ta_putc(textacc_t *t, char c) {
    if (t->len + 1 >= sizeof(t->buf)) { t->trunc = true; return; }
    t->buf[t->len++] = c;
    t->buf[t->len] = '\0';
}

static void ta_put_space(textacc_t *t) {
    if (t->last_space) return;
    ta_putc(t, ' ');
    t->last_space = true;
}

/* HTML5 replaces U+0000 in character data with U+FFFD. Storing the raw NUL
 * instead left an embedded terminator in the accumulator, so the token text
 * ended at the NUL and the rest of the run vanished with `truncated` false. */
static void ta_put_text(textacc_t *t, const char *s, uint32_t n) {
    for (uint32_t k = 0; k < n; k++) {
        if (s[k] == '\0') {
            ta_putc(t, (char)0xEF); ta_putc(t, (char)0xBF); ta_putc(t, (char)0xBD);
        } else {
            ta_putc(t, s[k]);
        }
    }
    t->last_space = false;
    t->has_nonspace = true;
}

/* Emit the pending character run as one TEXT token, if it is worth keeping.
 * The ONLY place a TEXT token is produced from the accumulator; both callers
 * therefore get the same style, the same truncation flag and the same <a>
 * inheritance. Leaves the accumulator reset. */
static void hp_flush_text(htmlp_t *p, textacc_t *ta) {
    if (ta->len == 0) return;
    bool keep = ta->has_nonspace || !ta->saw_newline;
    if (keep) {
        html_element_t *e = hp_emit(p);
        if (e) {
            e->kind = HTML_TOK_TEXT;
            e->type = HTML_TEXT;
            e->depth = (uint16_t)p->depth;
            hp_apply_style(e, &p->style[p->depth]);
            e->truncated = ta->trunc;
            /* ta->buf never holds a NUL (ta_put_text maps it to U+FFFD), so a
             * plain bounded copy of ta->len bytes is exact here. */
            if (!b_copyn(e->text, sizeof(e->text), ta->buf, ta->len))
                e->truncated = true;
            int32_t la = p->style[p->depth].link_elem;
            if (la >= 0 && (uint32_t)la < p->count) {
                e->is_link = p->els[la].is_link;
                b_copy(e->href, sizeof(e->href), p->els[la].href);
            }
        }
    }
    ta_reset(ta);
}

/* Layout wants each token's exact declared intrinsic size for <img>. */
static uint32_t attr_u32_value(const char *v) {
    uint32_t r = 0;
    for (uint32_t i = 0; v[i]; i++) {
        if (!b_is_digit(v[i])) break;
        r = r * 10u + (uint32_t)(v[i] - '0');
        if (r > 100000u) return 100000u;
    }
    return r;
}

int html_parse(const char *html, uint32_t len, html_element_t *elements,
               uint32_t max, uint32_t *count) {
    if (count) *count = 0;
    if (!html || !elements || max == 0) return BROWSER_ERR_ARG;
    if (len > BROWSER_MAX_PAGE_SIZE) return BROWSER_ERR_ARG;

    htmlp_t p;
    b_memset(&p, 0, sizeof(p));
    p.s = html; p.n = len; p.els = elements; p.max = max;
    p.style[0].font_size = BROWSER_BASE_FONT_PX;
    p.style[0].link_elem = -1;

    textacc_t ta;
    ta_reset(&ta);

    char raw_tag[BROWSER_MAX_TAG_LEN];
    bool raw = false;
    raw_tag[0] = '\0';

    uint32_t i = 0;

    while (i < len && !p.full) {

        /* ---------- RAWTEXT (script / style) ---------- */
        if (raw) {
            uint32_t start = i;
            uint32_t stop = len;
            uint32_t rtl = b_strlen(raw_tag);
            for (uint32_t k = i; k + 1 < len; k++) {
                if (html[k] != '<' || html[k + 1] != '/') continue;
                if (k + 2 + rtl > len) continue;
                if (!b_ieq_len(html + k + 2, rtl, raw_tag)) continue;
                uint32_t after = k + 2 + rtl;
                if (after < len && !b_is_space(html[after]) &&
                    html[after] != '>' && html[after] != '/') continue;
                stop = k;
                break;
            }
            if (stop > start) {
                html_element_t *e = hp_emit(&p);
                if (!e) break;
                e->kind = HTML_TOK_TEXT;
                e->type = HTML_TEXT;
                e->depth = (uint16_t)p.depth;
                hp_apply_style(e, &p.style[p.depth]);
                uint32_t n_raw = stop - start;
                if (!b_copy_text(e->text, sizeof(e->text), html + start, n_raw))
                    e->truncated = true;
            }
            i = stop;
            raw = false;
            if (i >= len) break;
            continue;
        }

        /* ---------- markup ---------- */
        if (html[i] == '<') {
            /* Classify BEFORE flushing. A '<' that does not begin markup is
             * ordinary text, and flushing first would split one character run
             * into two tokens ("a " + "< b > c") for no reason. */
            bool is_markup = (i + 1 < len) &&
                             (html[i + 1] == '!' || html[i + 1] == '/' ||
                              b_is_alpha(html[i + 1]));
            if (!is_markup) {
                ta_putc(&ta, '<');
                ta.has_nonspace = true;
                ta.last_space = false;
                i++;
                continue;
            }

            /* Flush any pending text run first. */
            hp_flush_text(&p, &ta);
            if (p.full) break;

            /* "<!" — comment, doctype, or bogus comment */
            if (i + 1 < len && html[i + 1] == '!') {
                if (i + 3 < len && html[i + 2] == '-' && html[i + 3] == '-') {
                    uint32_t cs = i + 4;
                    uint32_t ce = len;
                    for (uint32_t k = cs; k + 2 < len; k++) {
                        if (html[k] == '-' && html[k + 1] == '-' && html[k + 2] == '>') {
                            ce = k; break;
                        }
                    }
                    html_element_t *e = hp_emit(&p);
                    if (!e) break;
                    e->kind = HTML_TOK_COMMENT;
                    e->type = HTML_COMMENT;
                    e->depth = (uint16_t)p.depth;
                    hp_apply_style(e, &p.style[p.depth]);
                    b_copy(e->tag, sizeof(e->tag), "!--");
                    if (ce > cs) {
                        if (!b_copy_text(e->text, sizeof(e->text), html + cs, ce - cs))
                            e->truncated = true;
                    }
                    i = (ce == len) ? len : (ce + 3);
                    continue;
                }
                /* doctype or bogus comment: run to '>' or EOF */
                bool is_doctype = (i + 9 <= len) &&
                                  b_ieq_len(html + i + 2, 7, "doctype");
                uint32_t ds = i + 2;
                uint32_t de = len;
                for (uint32_t k = ds; k < len; k++) {
                    if (html[k] == '>') { de = k; break; }
                }
                html_element_t *e = hp_emit(&p);
                if (!e) break;
                e->kind = is_doctype ? HTML_TOK_DOCTYPE : HTML_TOK_COMMENT;
                e->type = is_doctype ? HTML_DOCTYPE : HTML_COMMENT;
                e->depth = (uint16_t)p.depth;
                hp_apply_style(e, &p.style[p.depth]);
                b_copy(e->tag, sizeof(e->tag), is_doctype ? "!doctype" : "!");
                if (de > ds) {
                    if (!b_copy_text(e->text, sizeof(e->text), html + ds, de - ds))
                        e->truncated = true;
                }
                i = (de == len) ? len : (de + 1);
                continue;
            }

            /* "</" end tag */
            bool is_end = false;
            uint32_t ns = i + 1;
            if (i + 1 < len && html[i + 1] == '/') {
                if (i + 2 < len && b_is_alpha(html[i + 2])) {
                    is_end = true;
                    ns = i + 2;
                } else {
                    /* "</" with no name: bogus comment to '>' or EOF */
                    uint32_t de = len;
                    for (uint32_t k = i + 2; k < len; k++)
                        if (html[k] == '>') { de = k; break; }
                    i = (de == len) ? len : (de + 1);
                    continue;
                }
            }
            /* is_markup guarantees html[i+1] is alphabetic here when it is
             * neither '!' nor '/', so a start tag name follows. */

            /* ---- tag name ---- */
            uint32_t j = ns;
            while (j < len && !b_is_space(html[j]) && html[j] != '>' &&
                   html[j] != '/') j++;
            char name[BROWSER_MAX_TAG_LEN];
            bool name_fit = b_copyn(name, sizeof(name), html + ns, j - ns);
            if (j == ns) {           /* impossible: we checked isalpha */
                i = j + 1;
                continue;
            }

            /* ---- attributes ---- */
            char href[BROWSER_MAX_HREF_LEN]; href[0] = '\0';
            char src[BROWSER_MAX_HREF_LEN];  src[0] = '\0';
            char alt[BROWSER_MAX_ALT_LEN];   alt[0] = '\0';
            char attrs[BROWSER_MAX_ATTRS_LEN];
            b_app_t aa; app_init(&aa, attrs, sizeof(attrs));
            bool href_fit = true, src_fit = true;
            /* Any attribute name or value that did not fit its buffer. Without
             * this, a 100-char attribute NAME was digested as its first 63
             * characters and a 200-char alt="" was silently cut to 127, both
             * with truncated == false — i.e. the element claimed to hold
             * attributes it did not have. */
            bool attr_trunc = false;
            uint32_t img_w = 0, img_h = 0;
            bool self_close = false;
            bool eof_in_tag = false;

            for (;;) {
                while (j < len && b_is_space(html[j])) j++;
                if (j >= len) { eof_in_tag = true; break; }
                if (html[j] == '>') { j++; break; }
                if (html[j] == '/') {
                    if (j + 1 < len && html[j + 1] == '>') {
                        self_close = true; j += 2; break;
                    }
                    j++;
                    continue;
                }

                uint32_t as = j;
                while (j < len && !b_is_space(html[j]) && html[j] != '=' &&
                       html[j] != '>' && html[j] != '/') j++;
                uint32_t ae = j;
                if (ae == as) { j++; continue; }   /* defensive: never spin */

                char aname[64];
                if (!b_copyn(aname, sizeof(aname), html + as, ae - as))
                    attr_trunc = true;

                char aval[BROWSER_MAX_HREF_LEN];
                aval[0] = '\0';
                bool aval_fit = true;

                while (j < len && b_is_space(html[j])) j++;
                if (j < len && html[j] == '=') {
                    j++;
                    while (j < len && b_is_space(html[j])) j++;
                    if (j < len && (html[j] == '"' || html[j] == '\'')) {
                        char q = html[j];
                        j++;
                        uint32_t vs = j;
                        while (j < len && html[j] != q) j++;
                        if (j >= len) { eof_in_tag = true; break; }  /* unclosed quote at EOF */
                        aval_fit = b_copyn(aval, sizeof(aval), html + vs, j - vs);
                        j++;
                    } else {
                        uint32_t vs = j;
                        while (j < len && !b_is_space(html[j]) && html[j] != '>') j++;
                        aval_fit = b_copyn(aval, sizeof(aval), html + vs, j - vs);
                    }
                }

                if (!aval_fit) attr_trunc = true;

                if (b_ieq(aname, "href")) {
                    b_copy(href, sizeof(href), aval);
                    href_fit = aval_fit;
                } else if (b_ieq(aname, "src")) {
                    b_copy(src, sizeof(src), aval);
                    src_fit = aval_fit;
                } else if (b_ieq(aname, "alt")) {
                    /* alt is 128 bytes against a 512-byte aval: it can fail to
                     * fit even when the attribute value itself was captured. */
                    if (!b_copy(alt, sizeof(alt), aval)) attr_trunc = true;
                } else if (b_ieq(aname, "width")) {
                    img_w = attr_u32_value(aval);
                } else if (b_ieq(aname, "height")) {
                    img_h = attr_u32_value(aval);
                }

                app_str(&aa, aname);
                app_ch(&aa, '=');
                app_str(&aa, aval);
                app_ch(&aa, ';');
            }

            if (eof_in_tag) {
                /* HTML5 "eof-in-tag": the tag token is discarded. Doing this
                 * is what makes every PREFIX of a document safe — a half-read
                 * tag never becomes a half-true element. */
                i = len;
                break;
            }

            i = j;
            html_elem_type_t type = html_tag_type(name);

            if (is_end) {
                /* pop to the matching open element, if there is one */
                uint32_t new_depth = p.depth;
                if (p.over_depth > 0) {
                    p.over_depth--;
                } else {
                    bool found = false;
                    uint32_t k = p.depth;
                    while (k > 0) {
                        k--;
                        if (b_ieq(p.open_tag[k], name)) { new_depth = k; found = true; break; }
                    }
                    if (found) p.depth = new_depth;
                }
                html_element_t *e = hp_emit(&p);
                if (!e) break;
                e->kind = HTML_TOK_END;
                e->type = type;
                e->depth = (uint16_t)p.depth;
                e->is_block = type_is_block(type);
                b_copy(e->tag, sizeof(e->tag), name);
                if (!name_fit || attr_trunc) e->truncated = true;
                hp_apply_style(e, &p.style[p.depth]);
                continue;
            }

            /* ---- start tag ---- */
            bool is_void = tag_is_void(name);
            html_style_t st = p.style[p.depth];
            int32_t hf = heading_font(type);
            if (hf) st.font_size = hf;
            if (b_ieq(name, "b") || b_ieq(name, "strong")) st.bold = true;
            if (b_ieq(name, "i") || b_ieq(name, "em")) st.italic = true;
            if (b_ieq(name, "u")) st.underline = true;
            if (hf) st.bold = true;

            html_element_t *e = hp_emit(&p);
            if (!e) break;
            uint32_t my_index = p.count - 1;
            e->kind = (is_void || self_close) ? HTML_TOK_SELF_CLOSE : HTML_TOK_START;
            e->type = type;
            e->depth = (uint16_t)p.depth;
            e->is_block = type_is_block(type);
            e->is_image = (type == HTML_IMG);
            b_copy(e->tag, sizeof(e->tag), name);
            if (!name_fit) e->truncated = true;
            b_copy(e->attrs, sizeof(e->attrs), attrs);
            if (aa.ovf) e->truncated = true;
            b_copy(e->href, sizeof(e->href), href);
            b_copy(e->src, sizeof(e->src), src);
            b_copy(e->alt, sizeof(e->alt), alt);
            if (!href_fit || !src_fit || attr_trunc) e->truncated = true;
            if (type == HTML_IMG) { e->w = img_w; e->h = img_h; }

            /* A link is only navigable if we hold its href IN FULL. A
             * truncated URL is a different URL. */
            if (type == HTML_A && href[0] && href_fit) {
                e->is_link = true;
                st.link_elem = (int32_t)my_index;
                st.underline = true;
            } else if (type == HTML_A) {
                st.link_elem = -1;
            }
            hp_apply_style(e, &st);

            if (!is_void && !self_close) {
                if (p.depth < BROWSER_HTML_MAX_DEPTH) {
                    b_copy(p.open_tag[p.depth], BROWSER_MAX_TAG_LEN, name);
                    p.style[p.depth + 1] = st;
                    p.depth++;
                } else {
                    p.over_depth++;      /* clamped: keep the balance honest */
                }
                if (tag_is_rawtext(name)) {
                    raw = true;
                    b_copy(raw_tag, sizeof(raw_tag), name);
                }
            }
            continue;
        }

        /* ---------- character data ---------- */
        if (html[i] == '&') {
            char ebuf[4];
            uint32_t en = 0;
            uint32_t used = entity_decode(html, len, i, ebuf, &en);
            if (used > 0) {
                ta_put_text(&ta, ebuf, en);
                i += used;
                continue;
            }
            ta_putc(&ta, '&');
            ta.has_nonspace = true;
            ta.last_space = false;
            i++;
            continue;
        }

        if (b_is_space(html[i])) {
            if (html[i] == '\n' || html[i] == '\r') ta.saw_newline = true;
            ta_put_space(&ta);
            i++;
            continue;
        }

        {
            uint32_t ts = i;
            while (i < len && html[i] != '<' && html[i] != '&' &&
                   !b_is_space(html[i])) i++;
            ta_put_text(&ta, html + ts, i - ts);
        }
    }

    /* trailing text run — same function, so it inherits <a> too */
    if (!p.full) hp_flush_text(&p, &ta);

    if (count) *count = p.count;
    return p.full ? BROWSER_ERR_TRUNCATED : BROWSER_OK;
}

/* ===================================================================== */
/* Layout                                                                 */
/* ===================================================================== */

int html_render(browser_tab_t *tab, uint32_t viewport_w, uint32_t viewport_h) {
    if (!tab) return BROWSER_ERR_ARG;
    if (viewport_w == 0 || viewport_h == 0) return BROWSER_ERR_ARG;
    if (tab->num_elements > BROWSER_MAX_ELEMENTS) return BROWSER_ERR_ARG;

    tab->viewport_w = viewport_w;
    tab->viewport_h = viewport_h;

    const uint32_t margin = BROWSER_LAYOUT_MARGIN;
    uint32_t avail = (viewport_w > 2 * margin) ? (viewport_w - 2 * margin) : 1;
    uint32_t right = margin + avail;

    uint32_t cx = margin, cy = margin, line_h = 0;
    uint32_t widest = margin;

    for (uint32_t idx = 0; idx < tab->num_elements; idx++) {
        html_element_t *e = &tab->elements[idx];

        /* font_size is caller-writable (html_render takes a tab, not a parse
         * result). Unclamped, font_size = 2e9 made line_h wrap uint32 and the
         * function reported content_w = 705032720 for a two-word paragraph —
         * an extent that then drives browser_scroll()'s clamp. The clamp is
         * written back so no element claims a size the layout did not use. */
        if (e->font_size > BROWSER_MAX_FONT_PX) e->font_size = BROWSER_MAX_FONT_PX;
        uint32_t fs = (e->font_size > 0) ? (uint32_t)e->font_size
                                         : (uint32_t)BROWSER_BASE_FONT_PX;
        uint32_t cw = BROWSER_CHAR_W(fs); if (cw == 0) cw = 1;
        uint32_t lh = BROWSER_LINE_H(fs); if (lh == 0) lh = 1;

        e->fg_color = e->is_link ? tab->accent_color : tab->fg_color;
        e->bg_color = tab->bg_color;

        if (e->type == HTML_BR) {
            if (line_h == 0) line_h = lh;
            e->x = cx; e->y = cy; e->w = 0; e->h = lh;
            cy += line_h; cx = margin; line_h = 0;
            continue;
        }

        if (e->type == HTML_HR) {
            if (line_h || cx > margin) { cy += line_h; cx = margin; line_h = 0; }
            cy += BROWSER_BLOCK_MARGIN;
            e->x = margin; e->y = cy; e->w = avail; e->h = BROWSER_HR_THICKNESS;
            cy += BROWSER_HR_THICKNESS + BROWSER_BLOCK_MARGIN;
            if (margin + avail > widest) widest = margin + avail;
            continue;
        }

        if (e->kind == HTML_TOK_TEXT) {
            e->x = cx; e->y = cy;
            if (line_h < lh) line_h = lh;
            uint32_t lines = 1;
            uint32_t start_x = cx;
            const char *s = e->text;
            uint32_t q = 0;
            while (s[q]) {
                uint32_t ws = q;
                while (s[q] && s[q] != ' ') q++;
                uint32_t wlen = q - ws;
                if (wlen) {
                    uint32_t wpx = wlen * cw;
                    if (cx > margin && cx + wpx > right) {
                        cy += line_h;
                        cx = margin;
                        line_h = lh;
                        lines++;
                    }
                    cx += wpx;
                    if (cx > widest) widest = cx;
                }
                if (s[q] == ' ') {
                    if (cx < right) cx += cw;
                    q++;
                }
            }
            e->h = lines * lh;
            e->w = (lines == 1) ? (cx > start_x ? cx - start_x : 0) : avail;
            continue;
        }

        if (e->type == HTML_IMG && e->is_image) {
            uint32_t iw = e->w ? e->w : BROWSER_IMG_PLACEHOLDER;
            uint32_t ih = e->h ? e->h : BROWSER_IMG_PLACEHOLDER;
            if (iw > avail) iw = avail;
            if (cx > margin && cx + iw > right) {
                cy += line_h ? line_h : lh; cx = margin; line_h = 0;
            }
            e->x = cx; e->y = cy; e->w = iw; e->h = ih;
            cx += iw;
            if (cx > widest) widest = cx;
            if (line_h < ih) line_h = ih;
            continue;
        }

        if (e->type == HTML_INPUT || e->type == HTML_BUTTON) {
            uint32_t iw = (e->type == HTML_INPUT ? 20u : 8u) * cw;
            if (iw > avail) iw = avail;
            if (cx > margin && cx + iw > right) {
                cy += line_h ? line_h : lh; cx = margin; line_h = 0;
            }
            e->x = cx; e->y = cy; e->w = iw; e->h = lh;
            cx += iw;
            if (cx > widest) widest = cx;
            if (line_h < lh) line_h = lh;
            continue;
        }

        if (e->is_block) {
            if (line_h || cx > margin) { cy += line_h; cx = margin; line_h = 0; }
            cy += BROWSER_BLOCK_MARGIN;
            e->x = margin; e->y = cy; e->w = avail; e->h = 0;
            if (margin + avail > widest) widest = margin + avail;
            continue;
        }

        /* inline start/end/comment/doctype: a zero box at the cursor */
        e->x = cx; e->y = cy; e->w = 0; e->h = 0;
    }

    if (line_h) { cy += line_h; line_h = 0; }
    cy += margin;

    tab->content_h = cy;
    tab->content_w = (widest + margin > viewport_w) ? (widest + margin) : viewport_w;

    /* Re-clamp any existing scroll against the new extents. */
    int32_t max_x = (int32_t)(tab->content_w > viewport_w ? tab->content_w - viewport_w : 0);
    int32_t max_y = (int32_t)(tab->content_h > viewport_h ? tab->content_h - viewport_h : 0);
    if (tab->scroll_x > max_x) tab->scroll_x = max_x;
    if (tab->scroll_y > max_y) tab->scroll_y = max_y;
    if (tab->scroll_x < 0) tab->scroll_x = 0;
    if (tab->scroll_y < 0) tab->scroll_y = 0;

    return BROWSER_OK;
}

/* ===================================================================== */
/* Browser                                                                */
/* ===================================================================== */

void browser_set_dragon_theme(browser_t *b) {
    if (!b) return;
    b->theme_bg     = 0x0A0000;   /* deep black-red  */
    b->theme_fg     = 0xFFD700;   /* gold            */
    b->theme_accent = 0xCC1100;   /* dragon red      */
    b->theme_link   = 0xFF6600;   /* fire orange     */
    b->theme_header = 0x330000;   /* dark crimson    */
    for (uint32_t i = 0; i < BROWSER_MAX_TABS; i++) {
        b->tabs[i].bg_color     = b->theme_bg;
        b->tabs[i].fg_color     = b->theme_fg;
        b->tabs[i].accent_color = b->theme_link;
    }
}

void browser_init(browser_t *b, const char *name) {
    if (!b) return;                 /* arm32 kernel_main calls this with NULL */
    b_memset(b, 0, sizeof(*b));
    b->device_id = 0x42524F57;      /* 'BROW' */
    b_copy(b->name, sizeof(b->name), name ? name : "ZXV-Browser");
    b_copy(b->homepage, sizeof(b->homepage), "http://localhost/");
    b_copy(b->user_agent, sizeof(b->user_agent), "ZXV-Browser/1.0");
    b->javascript_enabled = false;  /* LIMITATIONS 5: there is no engine */
    b->images_enabled = true;
    b->cookies_enabled = false;     /* LIMITATIONS 7: there is no jar */
    b->default_zoom = 100;
    b->next_tab_id = 1;
    b->active_tab = 0;
    browser_set_dragon_theme(b);

    b->m5.omega = 0;
    b->m5.r   = SR_ONE;
    b->m5.ell = SR_ZERO;
    b->m5.phi = SR_ZERO;
    b->m5.chi = 0;
    b->coverage_r = 1.0;
    b->coverage_l = 0.0;
}

browser_tab_t *browser_get_tab(browser_t *b, uint32_t tab_id) {
    if (!b || tab_id == 0) return (browser_tab_t *)0;
    for (uint32_t i = 0; i < BROWSER_MAX_TABS; i++) {
        if (b->tabs[i].active && b->tabs[i].tab_id == tab_id) return &b->tabs[i];
    }
    return (browser_tab_t *)0;
}

uint32_t browser_new_tab(browser_t *b, const char *url) {
    if (!b) return 0;
    for (uint32_t i = 0; i < BROWSER_MAX_TABS; i++) {
        if (b->tabs[i].active) continue;
        browser_tab_t *t = &b->tabs[i];
        b_memset(t, 0, sizeof(*t));
        t->tab_id = b->next_tab_id++;
        if (b->next_tab_id == 0) b->next_tab_id = 1;
        t->active = true;
        t->viewport_w = 1024;
        t->viewport_h = 768;
        t->bg_color = b->theme_bg;
        t->fg_color = b->theme_fg;
        t->accent_color = b->theme_link;
        if (url && url[0]) b_copy(t->url, sizeof(t->url), url);
        b->num_tabs++;
        b->active_tab = t->tab_id;
        return t->tab_id;
    }
    return 0;
}

int browser_close_tab(browser_t *b, uint32_t tab_id) {
    if (!b) return BROWSER_ERR_ARG;
    browser_tab_t *t = browser_get_tab(b, tab_id);
    if (!t) return BROWSER_ERR_NOENT;
    http_response_free(&t->response);
    b_memset(t, 0, sizeof(*t));
    if (b->num_tabs > 0) b->num_tabs--;
    if (b->active_tab == tab_id) {
        b->active_tab = 0;
        for (uint32_t i = 0; i < BROWSER_MAX_TABS; i++) {
            if (b->tabs[i].active) { b->active_tab = b->tabs[i].tab_id; break; }
        }
    }
    return BROWSER_OK;
}

/* Extract the first <title>text</title> from an already-tokenized stream. */
static void tab_extract_title(browser_tab_t *t) {
    for (uint32_t i = 0; i + 1 < t->num_elements; i++) {
        if (t->elements[i].kind == HTML_TOK_START &&
            t->elements[i].type == HTML_TITLE) {
            if (t->elements[i + 1].kind == HTML_TOK_TEXT) {
                b_copy(t->title, sizeof(t->title), t->elements[i + 1].text);
                return;
            }
        }
    }
}

/* Apply default_zoom to the parsed font sizes, then lay the tab out. This is
 * where default_zoom actually does something; html_render() never sees the
 * browser_t. */
static int tab_layout(browser_t *b, browser_tab_t *t) {
    uint32_t z = b->default_zoom ? b->default_zoom : 100;
    for (uint32_t i = 0; i < t->num_elements; i++) {
        int64_t fs = (int64_t)t->elements[i].font_size * (int64_t)z / 100;
        if (fs < 1) fs = 1;
        if (fs > BROWSER_MAX_FONT_PX) fs = BROWSER_MAX_FONT_PX;
        t->elements[i].font_size = (int32_t)fs;
        if (!b->images_enabled && t->elements[i].type == HTML_IMG)
            t->elements[i].is_image = false;   /* reserve no box */
    }
    return html_render(t, t->viewport_w ? t->viewport_w : 1024,
                          t->viewport_h ? t->viewport_h : 768);
}

/* Hand the receive slot back but keep the response METADATA.
 *
 * Once the bytes are tokenized into t->elements the raw body is dead weight,
 * and there are only BROWSER_HTTP_POOL_SLOTS slots against BROWSER_MAX_TABS
 * tabs. A tab that kept its slot for life would cap the whole browser at two
 * loaded tabs, so the slot goes back here. After this, response.body is NULL
 * and response.body_len is 0; status_code, headers, content_type and
 * content_length survive. */
static void tab_release_body(browser_tab_t *t) {
    http_response_t keep = t->response;
    http_response_free(&t->response);
    keep.body = (uint8_t *)0;
    keep.body_len = 0;
    t->response = keep;
}

/* Tokenize the tab's fetched body into its element table and lay it out. */
static int tab_load_document(browser_t *b, browser_tab_t *t) {
    t->num_elements = 0;
    t->title[0] = '\0';
    if (!t->response.body || t->response.body_len == 0) {
        t->loaded = false;
        tab_release_body(t);
        return BROWSER_OK;
    }
    uint32_t n = t->response.body_len;
    if (n > BROWSER_MAX_PAGE_SIZE) n = BROWSER_MAX_PAGE_SIZE;
    uint32_t cnt = 0;
    int rc = html_parse((const char *)t->response.body, n, t->elements,
                        BROWSER_MAX_ELEMENTS, &cnt);
    t->num_elements = cnt;
    tab_extract_title(t);
    tab_layout(b, t);
    t->loaded = (cnt > 0);
    tab_release_body(t);
    return rc;
}

int browser_add_history(browser_t *b, const char *url, const char *title) {
    if (!b || !url || !url[0]) return BROWSER_ERR_ARG;
    /* A history entry that does not hold the WHOLE url is a pointer to a
     * different page: browser_back() would rewrite the tab's URL to the
     * truncated one and, with a transport bound, fetch it. Refuse instead —
     * browser_add_bookmark() already refuses, and these must agree. */
    if (b_strlen(url) >= BROWSER_MAX_URL_LEN) return BROWSER_ERR_ARG;

    if (b->history_count > 0) {
        browser_history_t *cur = &b->history[b->history_index];
        bool same = true;
        uint32_t k = 0;
        for (; cur->url[k] && url[k]; k++) if (cur->url[k] != url[k]) { same = false; break; }
        if (same && cur->url[k] != url[k]) same = false;
        if (same) {
            if (title) b_copy(cur->title, sizeof(cur->title), title);
            return BROWSER_OK;
        }
    }

    uint32_t at;
    if (b->history_count == 0) {
        at = 0;
    } else {
        at = b->history_index + 1;
        if (at >= BROWSER_MAX_HISTORY) {
            /* Drop the oldest entry to make room; the stack keeps its shape. */
            for (uint32_t i = 0; i + 1 < BROWSER_MAX_HISTORY; i++)
                b->history[i] = b->history[i + 1];
            at = BROWSER_MAX_HISTORY - 1;
        }
    }

    browser_history_t *h = &b->history[at];
    b_memset(h, 0, sizeof(*h));
    b_copy(h->url, sizeof(h->url), url);
    if (title) b_copy(h->title, sizeof(h->title), title);
    b->nav_ordinal++;
    h->timestamp = b->nav_ordinal;
    b->history_index = at;
    b->history_count = at + 1;     /* everything after `at` is discarded */
    return BROWSER_OK;
}

browser_history_t *browser_get_history(browser_t *b, uint32_t *count) {
    if (count) *count = b ? b->history_count : 0;
    return b ? b->history : (browser_history_t *)0;
}

int browser_clear_history(browser_t *b) {
    if (!b) return BROWSER_ERR_ARG;
    b_memset(b->history, 0, sizeof(b->history));
    b->history_count = 0;
    b->history_index = 0;
    return BROWSER_OK;
}

int browser_navigate(browser_t *b, uint32_t tab_id, const char *url) {
    if (!b || !url) return BROWSER_ERR_ARG;
    browser_tab_t *t = browser_get_tab(b, tab_id);
    if (!t) return BROWSER_ERR_NOENT;

    browser_url_t u;
    int rc = browser_parse_url(url, &u);
    if (rc != BROWSER_OK) return rc;
    if (!u.absolute) return BROWSER_ERR_BAD_URL;
    if (!b_ieq(u.scheme, "http") && !b_ieq(u.scheme, "https"))
        return BROWSER_ERR_BAD_URL;

    /* Commit the navigation: the URL is now this tab's identity whatever
     * happens on the wire. */
    http_response_free(&t->response);
    b_copy(t->url, sizeof(t->url), url);
    t->title[0] = '\0';
    t->num_elements = 0;
    t->loaded = false;
    t->loading = false;
    t->scroll_x = 0;
    t->scroll_y = 0;
    t->content_w = 0;
    t->content_h = 0;
    browser_add_history(b, url, "");
    b->active_tab = t->tab_id;
    b->m5.omega++;

    if (!http_transport_bound()) return BROWSER_ERR_NO_TRANSPORT;

    t->loading = true;
    rc = http_request(&t->response, HTTP_GET, url, (const char *)0, 0);
    t->loading = false;
    if (rc != BROWSER_OK) {
        t->loaded = false;
        return rc;
    }

    int prc = tab_load_document(b, t);
    if (t->title[0]) {
        browser_history_t *h = (b->history_count > 0) ? &b->history[b->history_index]
                                                      : (browser_history_t *)0;
        if (h) b_copy(h->title, sizeof(h->title), t->title);
    }
    return prc;
}

static int browser_goto_history(browser_t *b, browser_tab_t *t) {
    browser_history_t *h = &b->history[b->history_index];
    http_response_free(&t->response);
    b_copy(t->url, sizeof(t->url), h->url);
    b_copy(t->title, sizeof(t->title), h->title);
    t->num_elements = 0;
    t->loaded = false;
    t->loading = false;
    t->scroll_x = 0;
    t->scroll_y = 0;
    t->content_w = 0;
    t->content_h = 0;
    if (!http_transport_bound()) return BROWSER_OK;   /* stack move only */
    t->loading = true;
    int rc = http_request(&t->response, HTTP_GET, t->url, (const char *)0, 0);
    t->loading = false;
    if (rc != BROWSER_OK) return rc;
    return tab_load_document(b, t);
}

int browser_back(browser_t *b, uint32_t tab_id) {
    if (!b) return BROWSER_ERR_ARG;
    browser_tab_t *t = browser_get_tab(b, tab_id);
    if (!t) return BROWSER_ERR_NOENT;
    if (b->history_count == 0 || b->history_index == 0) return BROWSER_ERR_NO_HISTORY;
    b->history_index--;
    return browser_goto_history(b, t);
}

int browser_forward(browser_t *b, uint32_t tab_id) {
    if (!b) return BROWSER_ERR_ARG;
    browser_tab_t *t = browser_get_tab(b, tab_id);
    if (!t) return BROWSER_ERR_NOENT;
    if (b->history_count == 0 || b->history_index + 1 >= b->history_count)
        return BROWSER_ERR_NO_HISTORY;
    b->history_index++;
    return browser_goto_history(b, t);
}

int browser_refresh(browser_t *b, uint32_t tab_id) {
    if (!b) return BROWSER_ERR_ARG;
    browser_tab_t *t = browser_get_tab(b, tab_id);
    if (!t) return BROWSER_ERR_NOENT;
    if (!t->url[0]) return BROWSER_ERR_BAD_URL;
    if (!http_transport_bound()) return BROWSER_ERR_NO_TRANSPORT;

    char url[BROWSER_MAX_URL_LEN];
    b_copy(url, sizeof(url), t->url);
    http_response_free(&t->response);
    t->num_elements = 0;
    t->loaded = false;
    t->loading = true;
    int rc = http_request(&t->response, HTTP_GET, url, (const char *)0, 0);
    t->loading = false;
    if (rc != BROWSER_OK) { t->loaded = false; return rc; }
    return tab_load_document(b, t);
}

int browser_stop(browser_t *b, uint32_t tab_id) {
    if (!b) return BROWSER_ERR_ARG;
    browser_tab_t *t = browser_get_tab(b, tab_id);
    if (!t) return BROWSER_ERR_NOENT;
    t->loading = false;    /* the postcondition "this tab is not loading" */
    return BROWSER_OK;
}

int browser_scroll(browser_t *b, uint32_t tab_id, int32_t dx, int32_t dy) {
    if (!b) return BROWSER_ERR_ARG;
    browser_tab_t *t = browser_get_tab(b, tab_id);
    if (!t) return BROWSER_ERR_NOENT;

    int32_t max_x = 0, max_y = 0;
    if (t->content_w > t->viewport_w) max_x = (int32_t)(t->content_w - t->viewport_w);
    if (t->content_h > t->viewport_h) max_y = (int32_t)(t->content_h - t->viewport_h);

    int64_t nx = (int64_t)t->scroll_x + (int64_t)dx;
    int64_t ny = (int64_t)t->scroll_y + (int64_t)dy;
    if (nx < 0) nx = 0;
    if (ny < 0) ny = 0;
    if (nx > max_x) nx = max_x;
    if (ny > max_y) ny = max_y;
    t->scroll_x = (int32_t)nx;
    t->scroll_y = (int32_t)ny;
    return BROWSER_OK;
}

/* ===== Bookmarks ===== */

int browser_add_bookmark(browser_t *b, const char *title, const char *url) {
    if (!b || !url || !url[0]) return BROWSER_ERR_ARG;
    if (b->num_bookmarks >= BROWSER_MAX_BOOKMARKS) return BROWSER_ERR_FULL;
    browser_bookmark_t *bm = &b->bookmarks[b->num_bookmarks];
    b_memset(bm, 0, sizeof(*bm));
    if (!b_copy(bm->url, sizeof(bm->url), url)) return BROWSER_ERR_ARG;
    b_copy(bm->title, sizeof(bm->title), title ? title : url);
    bm->folder = 0;
    b->num_bookmarks++;
    return BROWSER_OK;
}

int browser_remove_bookmark(browser_t *b, uint32_t index) {
    if (!b) return BROWSER_ERR_ARG;
    if (index >= b->num_bookmarks) return BROWSER_ERR_NOENT;
    for (uint32_t i = index; i + 1 < b->num_bookmarks; i++)
        b->bookmarks[i] = b->bookmarks[i + 1];
    b->num_bookmarks--;
    b_memset(&b->bookmarks[b->num_bookmarks], 0, sizeof(b->bookmarks[0]));
    return BROWSER_OK;
}

browser_bookmark_t *browser_get_bookmarks(browser_t *b, uint32_t *count) {
    if (count) *count = b ? b->num_bookmarks : 0;
    return b ? b->bookmarks : (browser_bookmark_t *)0;
}

/* ===== Settings ===== */

void browser_set_homepage(browser_t *b, const char *url) {
    if (!b || !url) return;
    b_copy(b->homepage, sizeof(b->homepage), url);
}

/* ===== Coverage ===== */

bool browser_verify_coverage(browser_t *b) {
    if (!b) return false;

    uint32_t open = 0, addressable = 0, consistent = 0;
    for (uint32_t i = 0; i < BROWSER_MAX_TABS; i++) {
        browser_tab_t *t = &b->tabs[i];
        if (!t->active) continue;
        open++;

        browser_url_t u;
        if (t->url[0] && browser_parse_url(t->url, &u) == BROWSER_OK && u.absolute)
            addressable++;

        if (t->loaded == (t->num_elements > 0)) consistent++;
    }

    if (open == 0) {
        b->coverage_r = 1.0;
        b->coverage_l = 1.0;
        b->m5.r = SR_ONE;
        b->m5.ell = SR_ONE;
        return true;             /* nothing open: nothing to fail to cover */
    }

    b->coverage_r = (double)addressable / (double)open;
    b->coverage_l = (double)consistent / (double)open;
    b->m5.r   = SR_FROM_FLOAT(b->coverage_r);
    b->m5.ell = SR_FROM_FLOAT(b->coverage_l);
    b->m5.chi = b->active_tab;

    return (b->coverage_r * b->coverage_l) >= BROWSER_COVERAGE_FLOOR;
}

/* ---- DECLARATION -----------------------------------------------------------

 * PROVIDES http_client_ready. The header's limitation #1 is explicit that
 * there is no socket in this module, so the bring-up asserts
 * http_transport_bound() is FALSE -- the module declares the parser, never the
 * network.
 *
 * REQUIRES_NONE is measured: browser.o's only undefined symbol is memcpy,
 * which the build redirects to fs_memcpy (freestanding.h), not a module edge.
 *
 * URL parsing is the right boot check: browser_resolve_url is verified against
 * all 42 RFC 3986 SS5.4 references in its own tests, and a split that puts the
 * host in the path is how a client ends up connecting to the wrong server.
 */
#include "zxv_decl.h"
static int zxvd_browser_bringup(void) {
    static browser_url_t u;
    static browser_t br;
    browser_init(&br, "zxv");
    if (http_transport_bound()) return -1;        /* there is no socket here */
    if (browser_parse_url("http://zxv.example:8080/a/b?q=1", &u) != BROWSER_OK)
        return -1;
    if (u.port != 8080u) return -1;
    return 0;
}

ZXV_DECLARE(browser,
    ZXV_PROVIDES(http_client_ready),
    ZXV_REQUIRES_NONE,
    ZXV_BRINGUP(zxvd_browser_bringup));
