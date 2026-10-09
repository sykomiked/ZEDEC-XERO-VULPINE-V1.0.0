/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_http_guard.c — the local API guard (zxv_http_guard.h G1-G5),
 * request by request, including the attacks it exists to stop. */
#include <stdio.h>
#include <string.h>

#include "zxv_http_guard.h"

static int fails;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s (line %d)\n", msg, __LINE__);                                          \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static zxv_guard_t g;
static char req[2048];

/* Build a request: extra is a block of header lines, each ending in \r\n. */
static zxv_guard_verdict_t run(const char *line, const char *extra, bool api)
{
    snprintf(req, sizeof req, "%s\r\n%s\r\nbody", line, extra);
    return zxv_guard_check(&g, req, api);
}

int main(void)
{
    /* G1: tokens */
    char t1[ZXV_TOKEN_HEX + 1], t2[ZXV_TOKEN_HEX + 1];
    CHECK(zxv_guard_token_new(t1) == 0 && zxv_guard_token_new(t2) == 0, "CSPRNG answers");
    CHECK(zxv_guard_token_valid(t1) && zxv_guard_token_valid(t2), "tokens are 64 hex");
    CHECK(strcmp(t1, t2) != 0, "two draws differ");
    CHECK(!zxv_guard_token_valid(NULL) && !zxv_guard_token_valid("") &&
              !zxv_guard_token_valid("ABCD"),
          "malformed tokens refused");
    char upper[ZXV_TOKEN_HEX + 1];
    memcpy(upper, t1, sizeof upper);
    upper[0] = 'A';
    CHECK(!zxv_guard_token_valid(upper), "uppercase hex refused");

    g.port = 8722;
    g.any_loopback_port = false;
    memcpy(g.token, t1, sizeof g.token);
    char tok[128], bad[128];
    snprintf(tok, sizeof tok, "X-ZXV-Token: %s\r\n", t1);
    snprintf(bad, sizeof bad, "X-ZXV-Token: %s\r\n", t2);
    char h[512];

    /* the app's own requests pass */
    snprintf(h, sizeof h, "Host: 127.0.0.1:8722\r\n%s", tok);
    CHECK(run("GET /api/state HTTP/1.1", h, true) == ZXV_GUARD_OK, "own GET passes");
    snprintf(h, sizeof h,
             "Host: 127.0.0.1:8722\r\nOrigin: http://127.0.0.1:8722\r\n"
             "Sec-Fetch-Site: same-origin\r\nContent-Length: 4\r\n%s",
             tok);
    CHECK(run("POST /api/quit HTTP/1.1", h, true) == ZXV_GUARD_OK, "own POST passes");
    snprintf(h, sizeof h, "host:   LOCALHOST:8722  \r\nx-zxv-token: %s\r\n", t1);
    CHECK(run("GET /api/state HTTP/1.1", h, true) == ZXV_GUARD_OK,
          "header names and localhost are case-insensitive");
    CHECK(run("GET / HTTP/1.1", "Host: 127.0.0.1:8722\r\nSec-Fetch-Site: none\r\n", false) ==
              ZXV_GUARD_OK,
          "the page itself needs no token");

    /* G1: token required on the API */
    CHECK(run("POST /api/quit HTTP/1.1", "Host: 127.0.0.1:8722\r\n", true) == ZXV_GUARD_NO_TOKEN,
          "no token refused");
    snprintf(h, sizeof h, "Host: 127.0.0.1:8722\r\n%s", bad);
    CHECK(run("POST /api/quit HTTP/1.1", h, true) == ZXV_GUARD_BAD_TOKEN, "wrong token refused");
    snprintf(h, sizeof h, "Host: 127.0.0.1:8722\r\nX-ZXV-Token: %.63s\r\n", t1);
    CHECK(run("POST /api/quit HTTP/1.1", h, true) == ZXV_GUARD_BAD_TOKEN, "short token refused");
    snprintf(h, sizeof h, "Host: 127.0.0.1:8722\r\nX-ZXV-Token: %sa\r\n", t1);
    CHECK(run("POST /api/quit HTTP/1.1", h, true) == ZXV_GUARD_BAD_TOKEN, "long token refused");

    /* the attack in gap 14: a cross-site simple POST from any web page */
    CHECK(run("POST /api/quit HTTP/1.1",
              "Host: 127.0.0.1:8722\r\nOrigin: https://evil.example\r\n"
              "Sec-Fetch-Site: cross-site\r\nContent-Type: text/plain\r\n",
              true) != ZXV_GUARD_OK,
          "cross-site simple POST refused");
    /* ... even if it somehow had the token */
    snprintf(h, sizeof h, "Host: 127.0.0.1:8722\r\nOrigin: https://evil.example\r\n%s", tok);
    CHECK(run("POST /api/quit HTTP/1.1", h, true) == ZXV_GUARD_BAD_ORIGIN, "foreign Origin");
    snprintf(h, sizeof h, "Host: 127.0.0.1:8722\r\nOrigin: null\r\n%s", tok);
    CHECK(run("POST /api/quit HTTP/1.1", h, true) == ZXV_GUARD_BAD_ORIGIN, "null Origin");
    snprintf(h, sizeof h, "Host: 127.0.0.1:8722\r\nOrigin: http://127.0.0.1:9999\r\n%s", tok);
    CHECK(run("POST /api/quit HTTP/1.1", h, true) == ZXV_GUARD_BAD_ORIGIN, "other-port Origin");
    snprintf(h, sizeof h, "Host: 127.0.0.1:8722\r\nOrigin: http://127.0.0.1:8722.evil\r\n%s", tok);
    CHECK(run("POST /api/quit HTTP/1.1", h, true) == ZXV_GUARD_BAD_ORIGIN, "suffix Origin");
    snprintf(h, sizeof h, "Host: 127.0.0.1:8722\r\nSec-Fetch-Site: same-site\r\n%s", tok);
    CHECK(run("GET /api/state HTTP/1.1", h, true) == ZXV_GUARD_CROSS_SITE, "same-site refused");
    snprintf(h, sizeof h, "Host: 127.0.0.1:8722\r\nSec-Fetch-Site: cross-site\r\n");
    CHECK(run("GET / HTTP/1.1", h, false) == ZXV_GUARD_CROSS_SITE, "cross-site page load refused");

    /* G2: DNS rebinding: the attacker's name resolves to 127.0.0.1 */
    snprintf(h, sizeof h, "Host: rebind.evil.example:8722\r\n%s", tok);
    CHECK(run("GET /api/state HTTP/1.1", h, true) == ZXV_GUARD_BAD_HOST, "rebound Host");
    CHECK(run("GET / HTTP/1.1", "Host: rebind.evil.example:8722\r\n", false) == ZXV_GUARD_BAD_HOST,
          "rebound Host on the page too");
    snprintf(h, sizeof h, "Host: 127.0.0.1.evil.example:8722\r\n%s", tok);
    CHECK(run("GET /api/state HTTP/1.1", h, true) == ZXV_GUARD_BAD_HOST, "prefix Host");
    snprintf(h, sizeof h, "Host: 127.0.0.1:87220\r\n%s", tok);
    CHECK(run("GET /api/state HTTP/1.1", h, true) == ZXV_GUARD_BAD_HOST, "wrong port");
    snprintf(h, sizeof h, "Host: 127.0.0.1\r\n%s", tok);
    CHECK(run("GET /api/state HTTP/1.1", h, true) == ZXV_GUARD_BAD_HOST, "missing port");
    CHECK(run("GET /api/state HTTP/1.1", tok, true) == ZXV_GUARD_BAD_HOST, "no Host");
    snprintf(h, sizeof h, "Host: 127.0.0.1:8722\r\nHost: evil.example\r\n%s", tok);
    CHECK(run("GET /api/state HTTP/1.1", h, true) == ZXV_GUARD_BAD_SHAPE, "two Hosts");

    /* tunnel mode: any loopback port, still only loopback names */
    g.any_loopback_port = true;
    snprintf(h, sizeof h, "Host: localhost:18722\r\nOrigin: http://localhost:18722\r\n%s", tok);
    CHECK(run("POST /api/ask HTTP/1.1", h, true) == ZXV_GUARD_OK, "tunnel port accepted");
    snprintf(h, sizeof h, "Host: evil.example:18722\r\n%s", tok);
    CHECK(run("POST /api/ask HTTP/1.1", h, true) == ZXV_GUARD_BAD_HOST, "tunnel needs loopback");
    g.any_loopback_port = false;

    /* G5: shape */
    snprintf(h, sizeof h, "Host: 127.0.0.1:8722\r\n%s", tok);
    CHECK(run("OPTIONS /api/quit HTTP/1.1", h, true) == ZXV_GUARD_BAD_METHOD, "no preflight");
    CHECK(run("PUT /api/quit HTTP/1.1", h, true) == ZXV_GUARD_BAD_METHOD, "no PUT");
    snprintf(h, sizeof h, "Host: 127.0.0.1:8722\r\nTransfer-Encoding: chunked\r\n%s", tok);
    CHECK(run("POST /api/ask HTTP/1.1", h, true) == ZXV_GUARD_BAD_SHAPE, "no chunked bodies");
    snprintf(h, sizeof h, "Host: 127.0.0.1:8722\r\n%s%s", tok, bad);
    CHECK(run("POST /api/ask HTTP/1.1", h, true) == ZXV_GUARD_BAD_SHAPE, "two tokens");
    snprintf(h, sizeof h, "Host: 127.0.0.1:8722\r\nContent-Length: 1\r\nContent-Length: 2\r\n%s",
             tok);
    CHECK(run("POST /api/ask HTTP/1.1", h, true) == ZXV_GUARD_BAD_SHAPE, "two lengths");

    /* a header that only appears in the body is not a header */
    snprintf(req, sizeof req, "GET /api/state HTTP/1.1\r\nHost: 127.0.0.1:8722\r\n\r\n%s", tok);
    CHECK(zxv_guard_check(&g, req, true) == ZXV_GUARD_NO_TOKEN, "token in body ignored");

    if (fails) {
        printf("test_zxv_http_guard: %d failure(s)\n", fails);
        return 1;
    }
    printf("test_zxv_http_guard: all checks passed\n");
    return 0;
}
