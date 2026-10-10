/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_http_guard.h — who may talk to the hosted app's local HTTP API.
 *
 * The app serves its window on 127.0.0.1 only, but every web page the user
 * opens can also reach 127.0.0.1. Without a guard, a page could POST
 * /api/quit or /api/ask (a CORS "simple request" runs before the browser
 * checks anything), and through DNS rebinding it could read /api/state.
 * The guard closes both:
 *
 *   G1 TOKEN.  A 256-bit token is drawn from the OS CSPRNG at every launch
 *      (arc4random_buf on macOS, getentropy/getrandom or /dev/urandom on
 *      Linux, BCryptGenRandom on Windows). If no CSPRNG answers, the app
 *      refuses to start. Every /api/ request must carry it in the
 *      X-ZXV-Token header; it is compared in constant time. A custom
 *      header also makes any cross-site fetch a CORS preflight, which the
 *      app never grants. The token reaches the window in the URL fragment
 *      (#token=...), which browsers never send to a server, or is injected
 *      by the native macOS shell.
 *   G2 HOST.   The Host header must be exactly 127.0.0.1:<port> or
 *      localhost:<port> (the port the app is listening on). A rebound DNS
 *      name never matches. In tunnel mode (--server, reached through
 *      ssh -L with a possibly different local port) any port is accepted
 *      but the name must still be a loopback name.
 *   G3 ORIGIN. When an Origin header is present it must be http:// plus the
 *      request's own Host; "null" and every other origin are refused.
 *   G4 FETCH METADATA. When Sec-Fetch-Site is present it must be
 *      same-origin or none (typed URL, the app opening its own window).
 *   G5 SHAPE.  Only GET and POST; repeated Host, Origin, token or
 *      Content-Length headers, and any Transfer-Encoding, are refused.
 *
 * The page itself (GET /) needs no token: it holds no secret, and a
 * navigation cannot carry a header. G2 to G5 still apply to it.
 */
#ifndef ZXV_HTTP_GUARD_H
#define ZXV_HTTP_GUARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ZXV_TOKEN_BYTES  32u
#define ZXV_TOKEN_HEX    (2u * ZXV_TOKEN_BYTES)
#define ZXV_TOKEN_HEADER "X-ZXV-Token"

typedef struct {
    int port;               /* the port the app listens on */
    bool any_loopback_port; /* tunnel mode: accept loopback names on any port */
    char token[ZXV_TOKEN_HEX + 1];
} zxv_guard_t;

typedef enum {
    ZXV_GUARD_OK = 0,
    ZXV_GUARD_BAD_METHOD,
    ZXV_GUARD_BAD_SHAPE, /* duplicate or forbidden header */
    ZXV_GUARD_BAD_HOST,
    ZXV_GUARD_BAD_ORIGIN,
    ZXV_GUARD_CROSS_SITE,
    ZXV_GUARD_NO_TOKEN,
    ZXV_GUARD_BAD_TOKEN
} zxv_guard_verdict_t;

/* Fill out[0..ZXV_TOKEN_HEX) with a fresh lowercase-hex token and NUL.
 * Returns 0, or -1 when the OS CSPRNG is unavailable (fail closed). */
int zxv_guard_token_new(char out[ZXV_TOKEN_HEX + 1]);

/* n bytes from the OS CSPRNG (also seeds the Vinea node, zxv_net_host.c).
 * Returns 0, or -1 when it is unavailable. */
int zxv_os_random(uint8_t *p, size_t n);

/* True when s is exactly ZXV_TOKEN_HEX lowercase hex digits. */
bool zxv_guard_token_valid(const char *s);

/* Find header `name` (case-insensitive) in a NUL-terminated request whose
 * header block ends at "\r\n\r\n" (or at the NUL). Returns how many times
 * it occurs; the first value, trimmed, goes to *val / *len. */
uint32_t zxv_http_header(const char *req, const char *name, const char **val, size_t *len);

/* Decide one request. need_token is true for /api/ paths. */
zxv_guard_verdict_t zxv_guard_check(const zxv_guard_t *g, const char *req, bool need_token);

/* A short reason for logs and the 403 body. */
const char *zxv_guard_reason(zxv_guard_verdict_t v);

#endif /* ZXV_HTTP_GUARD_H */
