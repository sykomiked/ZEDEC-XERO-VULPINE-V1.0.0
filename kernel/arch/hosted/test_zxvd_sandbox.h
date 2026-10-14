/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxvd_sandbox.h — what test_zxvd_instruments.c and test_zxvd_estate.c
 * share: requests through zxvd_rpc_handle_line, exact and partial checks,
 * and TEST-ONLY Ed25519 signing (src/provenance/test_signer.h) of the bytes
 * a *.message method returns. */
#ifndef TEST_ZXVD_SANDBOX_H
#define TEST_ZXVD_SANDBOX_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxvd_rpc.h"
#include "zxvd_ext.h"
#include "zxvd_groups.h"
#include "../../src/provenance/test_signer.h"

static int fails, checks;
static zxvd_buf_t OUT;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL %s:%d ", __FILE__, __LINE__);                                           \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

/* one request, printf-style: {"jsonrpc":"2.0","id":1,"method":M,"params":{P}} */
static const char *rpc(const char *method, const char *fmt, ...)
{
    static char line[16384], params[15000];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(params, sizeof params, fmt, ap);
    va_end(ap);
    snprintf(line, sizeof line, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":{%s}}",
             method, params);
    zxvd_rpc_handle_line(line, strlen(line), &OUT);
    return OUT.p ? OUT.p : "";
}

static bool has(const char *got, const char *needle)
{
    return strstr(got, needle) != NULL;
}

/* the result object exactly, or the error message exactly */
#define OKR(res)   "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":" res "}"
#define ERRR(c, m) "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":" c ",\"message\":\"" m "\"}}"
#define EXPECT(got, want)                                                                          \
    do {                                                                                           \
        const char *g_ = (got);                                                                    \
        CHECK(!strcmp(g_, want), "\n    want %s\n    got  %s", want, g_);                          \
    } while (0)
#define HAS(got, needle)                                                                           \
    do {                                                                                           \
        const char *g_ = (got);                                                                    \
        CHECK(has(g_, needle), "\n    want ...%s...\n    got  %s", needle, g_);                    \
    } while (0)

static void hex_of(const uint8_t *p, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) snprintf(out + 2 * i, 3, "%02x", p[i]);
}

static char *pk_hex(const test_signer_t *s)
{
    static char b[8][65];
    static int k;
    char *o = b[k++ & 7];
    hex_of(s->pk, 32, o);
    return o;
}

/* sign the "message" of the last reply; returns 128 hex chars (rotating) */
static char *sign_last(const test_signer_t *s)
{
    static char b[4][129];
    static int k;
    char *o = b[k++ & 3];
    uint8_t m[256];
    size_t n = 0;
    const char *p = OUT.p ? strstr(OUT.p, "\"message\":\"") : NULL;
    if (p)
        for (p += 11; *p != '"' && n < sizeof m; p += 2) {
            unsigned v;
            sscanf(p, "%2x", &v);
            m[n++] = (uint8_t) v;
        }
    uint8_t sig[64];
    test_signer_sign(s, m, (uint32_t) n, sig);
    hex_of(sig, 64, o);
    return o;
}

/* a signature over the bytes `method` returns for params `fmt` */
static char *sign_msg(const test_signer_t *s, const char *method, const char *fmt, ...)
{
    static char params[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(params, sizeof params, fmt, ap);
    va_end(ap);
    rpc(method, "%s", params);
    CHECK(has(OUT.p ? OUT.p : "", "\"message\":"), "%s %s: %s", method, params, OUT.p);
    return sign_last(s);
}

/* a well-formed signature that verifies for nobody */
static const char *zero_sig(void)
{
    static char z[129];
    memset(z, '0', 128);
    return z;
}

static int finish(const char *name)
{
    free(OUT.p);
    if (fails) {
        printf("[FAIL] %s: %d of %d checks failed\n", name, fails, checks);
        return 1;
    }
    printf("[PASS] %s: %d checks\n", name, checks);
    return 0;
}

#endif
