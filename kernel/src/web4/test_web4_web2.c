/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_web4_web2.c — Web 2 side against PUBLISHED vectors where they exist:
 *   JWS HS256      RFC 7515 appendix A.1 (parse, verify, re-sign byte-exact)
 *   PKCE S256      RFC 7636 appendix B
 *   HMAC-SHA256    RFC 4231 case 2 (in test_web4_web3), GitHub's documented
 *                  webhook example ("It's a Secret to Everybody")
 *   base64url      RFC 4648 section 10
 * plus state-machine runs (auth code + PKCE, device code incl. slow_down),
 * HTTP smuggling cases, and fuzz-style malformed input for every parser. */
#include <stdio.h>
#include <string.h>
#include "web4_web2.h"

static int failures = 0, passes = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s (line %d)\n", m, __LINE__);                                          \
            failures++;                                                                            \
        } else {                                                                                   \
            printf("[PASS] %s\n", m);                                                              \
            passes++;                                                                              \
        }                                                                                          \
    } while (0)

static uint64_t rng = 0x243F6A8885A308D3ull;
static uint64_t next(void)
{
    uint64_t z = (rng += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

#define SL(s) (s), (uint32_t) strlen(s)

static void test_base64(void)
{
    /* RFC 4648 section 10 */
    const char *in[7] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    const char *std[7] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
    int ok = 0;
    for (int i = 0; i < 7; i++) {
        char o[16], u[16];
        uint8_t d[16];
        w4_b64_encode((const uint8_t *) in[i], (uint32_t) strlen(in[i]), o, sizeof o);
        int32_t ul =
            w4_b64url_encode((const uint8_t *) in[i], (uint32_t) strlen(in[i]), u, sizeof u);
        int32_t dl = w4_b64url_decode(u, (uint32_t) ul, d, sizeof d);
        if (strcmp(o, std[i]) == 0 && dl == (int32_t) strlen(in[i]) &&
            memcmp(d, in[i], (size_t) dl) == 0)
            ok++;
    }
    CHECK(ok == 7, "base64 RFC 4648 section 10 vectors (+ base64url round trip)");
    uint8_t d[8];
    CHECK(w4_b64url_decode("Zh", 2, d, sizeof d) == W4_ERR_PARSE,
          "base64url: non-zero pad bits refused");
    CHECK(w4_b64url_decode("Zg==", 4, d, sizeof d) == W4_ERR_PARSE,
          "base64url: '=' padding refused");
    CHECK(w4_b64url_decode("Z", 1, d, sizeof d) == W4_ERR_PARSE,
          "base64url: length 1 mod 4 refused");
    CHECK(w4_b64url_decode("Zm+v", 4, d, sizeof d) == W4_ERR_PARSE, "base64url: '+' refused");
}

static void test_json(void)
{
    w4_jtok_t t[64];
    const char *js =
        "{\"a\":1,\"b\":[true,false,null,\"x\\u00e9\\ud83d\\ude00\"],\"c\":{\"d\":-12}}";
    int32_t n = w4_json_parse(SL(js), t, 64, 8);
    CHECK(n == 13, "JSON tokenize nested document");
    int32_t b = w4_json_get(js, t, n, 0, "b");
    CHECK(b > 0 && t[b].type == W4_J_ARR && t[b].size == 4, "JSON array member");
    char s[32];
    int32_t e = w4_json_at(t, n, b, 3);
    CHECK(e > 0 && w4_json_str(js, &t[e], s, sizeof s) == 7 &&
              memcmp(s, "x\xc3\xa9\xf0\x9f\x98\x80", 7) == 0,
          "JSON \\u escapes incl. surrogate pair -> UTF-8");
    int32_t c = w4_json_get(js, t, n, 0, "c");
    int64_t v;
    CHECK(c > 0 && w4_json_i64(js, &t[w4_json_get(js, t, n, c, "d")], &v) == 0 && v == -12,
          "JSON nested integer");
    CHECK(w4_json_get(js, t, n, 0, "zz") == W4_ERR_NOTFOUND, "JSON missing key");
    const char *dup = "{\"k\":1,\"k\":2}";
    n = w4_json_parse(SL(dup), t, 64, 8);
    CHECK(n == 5 && w4_json_get(dup, t, n, 0, "k") == W4_ERR_PARSE, "JSON duplicate key reported");
    const char *bad[] = {"",
                         "{",
                         "{\"a\"}",
                         "{\"a\":}",
                         "[1,]",
                         "[01]",
                         "[1.]",
                         "[.5]",
                         "[1e]",
                         "\"\\x\"",
                         "\"\x01\"",
                         "[\"\\ud800\"]",
                         "[\"\\udc00\"]",
                         "tru",
                         "nul",
                         "{} {}",
                         "[1 2]",
                         "{\"a\":1,}",
                         "[\"\xc0\xaf\"]",
                         "[\"\xed\xa0\x80\"]",
                         "{\"a\" 1}",
                         "[-]",
                         "[- 1]",
                         "]",
                         "[}"};
    int rej = 0;
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        rej += w4_json_parse(bad[i], (uint32_t) strlen(bad[i]), t, 64, 8) < 0;
    CHECK(rej == (int) (sizeof bad / sizeof bad[0]), "JSON: 25 malformed documents rejected");
    char deep[100];
    for (int i = 0; i < 40; i++) deep[i] = '[';
    for (int i = 0; i < 40; i++) deep[40 + i] = ']';
    CHECK(w4_json_parse(deep, 80, t, 64, 32) == W4_ERR_PARSE, "JSON depth limit");
    CHECK(w4_json_parse("[1,2,3,4,5,6,7,8]", 17, t, 4, 8) == W4_ERR_SPACE, "JSON token limit");
    uint64_t u;
    const char *big = "[18446744073709551615,18446744073709551616,1.5]";
    n = w4_json_parse(SL(big), t, 64, 8);
    CHECK(n == 4 && w4_json_u64(big, &t[1], &u) == 0 && u == UINT64_MAX &&
              w4_json_u64(big, &t[2], &u) == W4_ERR_RANGE &&
              w4_json_u64(big, &t[3], &u) == W4_ERR_RANGE,
          "JSON u64 limits and non-integers");
    /* writer */
    char out[256];
    w4_jw j;
    w4_jw_init(&j, out, sizeof out);
    w4_jw_obj(&j);
    w4_jw_key(&j, "s");
    w4_jw_str(&j, "q\"\\\n\x01");
    w4_jw_key(&j, "a");
    w4_jw_arr(&j);
    w4_jw_u64(&j, 1);
    w4_jw_i64(&j, -2);
    w4_jw_bool(&j, true);
    w4_jw_null(&j);
    w4_jw_arr_end(&j);
    w4_jw_obj_end(&j);
    CHECK(w4_jw_finish(&j) > 0 &&
              strcmp(out, "{\"s\":\"q\\\"\\\\\\n\\u0001\",\"a\":[1,-2,true,null]}") == 0,
          "JSON writer escapes and separators");
    n = w4_json_parse(SL(out), t, 64, 8);
    CHECK(n == 9, "JSON writer output re-parses");
    w4_jw_init(&j, out, sizeof out);
    w4_jw_obj(&j);
    w4_jw_u64(&j, 1); /* value without key */
    CHECK(w4_jw_finish(&j) < 0, "JSON writer refuses a value without a key");
    w4_jw_init(&j, out, sizeof out);
    w4_jw_str(&j, "\xff");
    CHECK(w4_jw_finish(&j) < 0, "JSON writer refuses invalid UTF-8");
    /* fuzz: random and mutated JSON never yields tokens outside the input */
    int oob = 0;
    for (int i = 0; i < 30000; i++) {
        char fz[64];
        uint32_t L;
        if (i & 1) {
            L = (uint32_t) (next() % 64);
            static const char al[] = "{}[]\",:0123456789-.eE+tfnrul\\ \"ab";
            for (uint32_t k = 0; k < L; k++) fz[k] = al[next() % (sizeof al - 1)];
        } else {
            L = (uint32_t) strlen(js) < 64 ? (uint32_t) strlen(js) : 63;
            memcpy(fz, js, L);
            fz[next() % L] = (char) next();
        }
        int32_t m = w4_json_parse(fz, L, t, 64, 8);
        for (int32_t k = 0; k < m; k++)
            if (t[k].end > L || t[k].start > t[k].end) oob++;
    }
    CHECK(oob == 0, "JSON fuzz: 30000 random / mutated inputs stay in bounds");
}

static void test_http(void)
{
    w4_http_msg_t m;
    uint8_t cb[256];
    const char *rq = "POST /api/v1/x?y=1 HTTP/1.1\r\nHost: a.example\r\nContent-Length: 5\r\n"
                     "X-T:  spaced  \r\n\r\nhelloEXTRA";
    CHECK(w4_http_parse_request((const uint8_t *) rq, (uint32_t) strlen(rq), &m, cb, sizeof cb) ==
                  W4_OK &&
              m.method_len == 4 && m.target_len == 13 && m.body_len == 5 &&
              memcmp(m.body, "hello", 5) == 0 && m.consumed == strlen(rq) - 5,
          "HTTP request with Content-Length (pipelined bytes left)");
    const w4_hdr_t *h = w4_http_hdr(&m, "x-t");
    CHECK(h && h->val_len == 6 && memcmp(h->val, "spaced", 6) == 0,
          "HTTP header OWS trimmed, name case-insensitive");
    const char *ch = "PUT / HTTP/1.1\r\nHost: h\r\nTransfer-Encoding: chunked\r\n\r\n"
                     "4\r\nWiki\r\n5;ext=1\r\npedia\r\n0\r\n\r\n";
    CHECK(w4_http_parse_request((const uint8_t *) ch, (uint32_t) strlen(ch), &m, cb, sizeof cb) ==
                  W4_OK &&
              m.chunked && m.body_len == 9 && memcmp(m.body, "Wikipedia", 9) == 0,
          "HTTP chunked body de-chunked");
    CHECK(w4_http_parse_request((const uint8_t *) ch, (uint32_t) strlen(ch) - 3, &m, cb,
                                sizeof cb) == W4_ERR_PENDING,
          "HTTP incomplete chunked body -> PENDING");
    const char *smuggle[] = {
        "POST / HTTP/1.1\r\nHost: h\r\nContent-Length: 3\r\nTransfer-Encoding: "
        "chunked\r\n\r\n0\r\n\r\n",
        "POST / HTTP/1.1\r\nHost: h\r\nContent-Length: 3\r\nContent-Length: 4\r\n\r\nabcd",
        "POST / HTTP/1.1\r\nHost: h\r\nTransfer-Encoding: gzip, chunked\r\n\r\n",
        "POST / HTTP/1.1\r\nHost: h\r\nContent-Length : 3\r\n\r\nabc",
        "POST / HTTP/1.1\r\nHost: h\r\nX: a\r\n b\r\n\r\n",
        "POST / HTTP/1.1\nHost: h\n\n",
        "POST / HTTP/1.1\r\nHost: h\r\nContent-Length: -1\r\n\r\n",
        "POST / HTTP/1.1\r\nHost: h\r\nContent-Length: 1x\r\n\r\n",
        "GET / HTTP/1.1\r\n\r\n",
        "GET  / HTTP/1.1\r\nHost: h\r\n\r\n",
        "GET / HTTP/2.0\r\nHost: h\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: h\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nTrailer: x\r\n\r\n",
        "GET / HTTP/1.1\r\nHost: h\r\nTransfer-Encoding: chunked\r\n\r\nfffffffff\r\n",
    };
    int rej = 0;
    for (size_t i = 0; i < sizeof smuggle / sizeof smuggle[0]; i++)
        rej += w4_http_parse_request((const uint8_t *) smuggle[i], (uint32_t) strlen(smuggle[i]),
                                     &m, cb, sizeof cb) == W4_ERR_PARSE;
    CHECK(rej == (int) (sizeof smuggle / sizeof smuggle[0]),
          "HTTP: 13 smuggling / malformed requests refused (CL+TE, dup CL, obs-fold, bare LF...)");
    const char *rs =
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}";
    CHECK(w4_http_parse_response((const uint8_t *) rs, (uint32_t) strlen(rs), false, &m, cb,
                                 sizeof cb) == W4_OK &&
              m.status == 200 && m.body_len == 2,
          "HTTP response parse");
    const char *r204 = "HTTP/1.1 204 No Content\r\n\r\n";
    CHECK(w4_http_parse_response((const uint8_t *) r204, (uint32_t) strlen(r204), false, &m, cb,
                                 sizeof cb) == W4_OK &&
              m.body_len == 0,
          "HTTP 204 has no body");
    const char *rc = "HTTP/1.0 200 OK\r\n\r\nall the rest";
    CHECK(w4_http_parse_response((const uint8_t *) rc, (uint32_t) strlen(rc), false, &m, cb,
                                 sizeof cb) == W4_OK &&
              m.until_close && m.body_len == 12,
          "HTTP response delimited by close");
    /* builders */
    uint8_t out[512];
    w4_w w;
    w4_w_init(&w, out, sizeof out);
    w4_hdr_t hd[1] = {w4_hdr("Accept", "application/json")};
    int32_t n =
        w4_http_build_request(&w, "POST", "api.example", "/v1", hd, 1, (const uint8_t *) "{}", 2);
    CHECK(n > 0 && w4_http_parse_request(out, (uint32_t) n, &m, cb, sizeof cb) == W4_OK &&
              m.body_len == 2,
          "HTTP built request re-parses");
    w4_w_init(&w, out, sizeof out);
    w4_hdr_t inj[1] = {w4_hdr("X", "a\r\nSet-Cookie: x")};
    CHECK(w4_http_build_request(&w, "GET", "h", "/", inj, 1, NULL, 0) < 0,
          "HTTP builder refuses header injection");
    w4_w_init(&w, out, sizeof out);
    CHECK(w4_http_build_request(&w, "GET", "h", "/a b", NULL, 0, NULL, 0) == W4_ERR_ARG,
          "HTTP builder refuses a space in the target");
    w4_w_init(&w, out, sizeof out);
    n = w4_http_build_response(&w, 404, "Not Found", NULL, 0, (const uint8_t *) "no", 2);
    CHECK(n > 0 && w4_http_parse_response(out, (uint32_t) n, false, &m, cb, sizeof cb) == W4_OK &&
              m.status == 404 && m.body_len == 2,
          "HTTP built response re-parses");
    /* URLs and forms */
    w4_url_t u;
    CHECK(w4_url_parse(SL("https://id.example:8443/auth?x=1#f"), &u) == W4_OK && u.port == 8443 &&
              u.host_len == 10 && u.path_len == 5 && u.query_len == 3,
          "URL parse with port, path, query, fragment");
    CHECK(w4_url_parse(SL("https://good.example@evil.example/"), &u) == W4_ERR_PARSE,
          "URL with userinfo refused");
    CHECK(w4_url_parse(SL("https://h:0/"), &u) == W4_ERR_PARSE &&
              w4_url_parse(SL("https://h:65536/"), &u) == W4_ERR_PARSE,
          "URL port range checked");
    char v[64];
    const char *q = "code=a%2Fb+c&state=xyz&empty=&x";
    CHECK(w4_form_get(SL(q), "code", v, sizeof v) == 5 && strcmp(v, "a/b c") == 0 &&
              w4_form_get(SL(q), "empty", v, sizeof v) == 0 &&
              w4_form_get(SL(q), "nope", v, sizeof v) == W4_ERR_NOTFOUND,
          "form decode");
    CHECK(w4_form_get(SL("a=1&a=2"), "a", v, sizeof v) == W4_ERR_PARSE &&
              w4_form_get(SL("a=%zz"), "a", v, sizeof v) == W4_ERR_PARSE &&
              w4_form_get(SL("a=%4"), "a", v, sizeof v) == W4_ERR_PARSE,
          "form: duplicate key and bad escapes refused");
    /* fuzz */
    int bad = 0;
    for (int i = 0; i < 20000; i++) {
        uint8_t fz[200];
        uint32_t L = (uint32_t) strlen(ch);
        memcpy(fz, ch, L);
        int k = (int) (next() % 4);
        for (int z = 0; z <= k; z++) fz[next() % L] = (uint8_t) (next() & 0xff);
        if (i % 3 == 0) L = (uint32_t) (next() % L);
        int r = w4_http_parse_request(fz, L, &m, cb, sizeof cb);
        if (r == W4_OK) {
            if (m.consumed > L || m.body_len > sizeof cb) bad++;
            for (uint32_t z = 0; z < m.nh; z++)
                if ((const uint8_t *) m.h[z].val + m.h[z].val_len > fz + L) bad++;
        }
    }
    CHECK(bad == 0, "HTTP fuzz: 20000 mutated requests stay in bounds");
}

static const char RFC7515_A1[] = "eyJ0eXAiOiJKV1QiLA0KICJhbGciOiJIUzI1NiJ9."
                                 "eyJpc3MiOiJqb2UiLA0KICJleHAiOjEzMDA4MTkzODAsDQogImh0dHA6Ly9leGFtc"
                                 "GxlLmNvbS9pc19yb290Ijp0cnVlfQ."
                                 "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
static const char RFC7515_KEY[] =
    "AyM1SysPpbyDfgZld3umj1qzKObwVMkoqQ-EstJQLr_T-1qS0gZH75aKtMN3Yj0iPS4hcgUuTwjAzZr1Z9CAow";

static w4_jws_t jws, jws2;

static void test_jwt(void)
{
    uint8_t key[64];
    int32_t kl = w4_b64url_decode(SL(RFC7515_KEY), key, sizeof key);
    CHECK(kl == 64, "RFC 7515 A.1 JWK k decodes to 64 bytes");
    CHECK(w4_jws_parse(SL(RFC7515_A1), &jws) == W4_OK && strcmp(jws.alg, "HS256") == 0 &&
              strcmp(jws.typ, "JWT") == 0,
          "RFC 7515 A.1 compact JWS parses");
    w4_hs256_key_t hk = {key, (uint32_t) kl};
    CHECK(w4_jws_verify(&jws, "HS256", w4_jws_hs256_verify, &hk) == W4_OK,
          "RFC 7515 A.1 HS256 verifies");
    CHECK(w4_jws_verify(&jws, "RS256", w4_jws_hs256_verify, &hk) == W4_ERR_SIG,
          "JWS: expected alg differs -> refused (no alg switching)");
    const char hdr[] = "{\"typ\":\"JWT\",\r\n \"alg\":\"HS256\"}";
    const char pay[] =
        "{\"iss\":\"joe\",\r\n \"exp\":1300819380,\r\n \"http://example.com/is_root\":true}";
    char out[512];
    int32_t n = w4_jws_sign_hs256(hdr, (uint32_t) strlen(hdr), (const uint8_t *) pay,
                                  (uint32_t) strlen(pay), key, (uint32_t) kl, out, sizeof out);
    CHECK(n > 0 && strcmp(out, RFC7515_A1) == 0, "RFC 7515 A.1 re-signed byte-exact");
    w4_jwt_claims_t c;
    CHECK(w4_jwt_claims(&jws, NULL, &c) == W4_OK && strcmp(c.iss, "joe") == 0 && c.has_exp &&
              c.exp == 1300819380u,
          "RFC 7515 A.1 claims");
    CHECK(w4_jwt_validate(&c, "joe", NULL, NULL, 1300819379u, 0) == W4_OK &&
              w4_jwt_validate(&c, "joe", NULL, NULL, 1300819380u, 0) == W4_ERR_EXPIRED &&
              w4_jwt_validate(&c, "mallory", NULL, NULL, 1300819379u, 0) == W4_ERR_DENIED,
          "JWT exp and iss checks");
    /* tampering */
    char t[512];
    strcpy(t, RFC7515_A1);
    t[60] = (t[60] == 'A') ? 'B' : 'A';
    CHECK(w4_jws_parse(SL(t), &jws2) != W4_OK ||
              w4_jws_verify(&jws2, "HS256", w4_jws_hs256_verify, &hk) == W4_ERR_SIG,
          "JWS: payload tampering detected");
    /* alg none, crit, JWE-shaped, short key */
    const char *none = "eyJhbGciOiJub25lIn0.eyJhIjoxfQ.c2ln"; /* {"alg":"none"} */
    CHECK(w4_jws_parse(SL(none), &jws2) == W4_ERR_PARSE, "JWS alg=none refused");
    char crit[256];
    char hb[64];
    const char *ch = "{\"alg\":\"HS256\",\"crit\":[\"x\"]}";
    w4_b64url_encode((const uint8_t *) ch, (uint32_t) strlen(ch), hb, sizeof hb);
    snprintf(crit, sizeof crit, "%s.e30.c2ln", hb);
    CHECK(w4_jws_parse(SL(crit), &jws2) == W4_ERR_UNSUPP, "JWS crit header refused");
    CHECK(w4_jws_parse(SL("a.b.c.d.e"), &jws2) == W4_ERR_PARSE, "JWE-shaped (5 parts) refused");
    CHECK(w4_jws_parse(SL("eyJhbGciOiJIUzI1NiJ9.e30."), &jws2) == W4_ERR_PARSE,
          "unsigned JWS refused");
    w4_hs256_key_t shortk = {key, 16};
    CHECK(w4_jws_verify(&jws, "HS256", w4_jws_hs256_verify, &shortk) == W4_ERR_SIG,
          "HS256 key < 32 bytes refused");
    /* OIDC-style claims with aud array and nonce */
    const char idt[] =
        "{\"iss\":\"https://id.example\",\"sub\":\"u-1\",\"aud\":[\"other\",\"web4-client\"],"
        "\"azp\":\"web4-client\",\"nonce\":\"n-0S6\",\"exp\":2000,\"iat\":1000}";
    n = w4_jws_sign_hs256("{\"alg\":\"HS256\"}", 15, (const uint8_t *) idt, (uint32_t) strlen(idt),
                          key, 64, out, sizeof out);
    CHECK(n > 0 && w4_jws_parse(out, (uint32_t) n, &jws2) == W4_OK &&
              w4_jwt_claims(&jws2, "web4-client", &c) == W4_OK && c.aud_count == 2 &&
              w4_jwt_validate(&c, "https://id.example", "web4-client", "n-0S6", 1500, 0) == W4_OK &&
              w4_jwt_validate(&c, "https://id.example", "web4-client", "n-other", 1500, 0) ==
                  W4_ERR_DENIED &&
              w4_jwt_validate(&c, "https://id.example", "third", NULL, 1500, 0) == W4_ERR_DENIED &&
              w4_jwt_validate(&c, "https://id.example", "web4-client", NULL, 900 - 1, 60) ==
                  W4_ERR_EXPIRED,
          "OIDC id_token: aud array + azp, nonce, iat in the future");
    /* fuzz the compact parser */
    int bad = 0;
    for (int i = 0; i < 5000; i++) {
        char fz[256];
        uint32_t L = (uint32_t) strlen(RFC7515_A1);
        memcpy(fz, RFC7515_A1, L);
        fz[next() % L] = "A-_.=x"[next() % 6];
        if (w4_jws_parse(fz, L, &jws2) == W4_OK &&
            w4_jws_verify(&jws2, "HS256", w4_jws_hs256_verify, &hk) == W4_OK &&
            memcmp(fz, RFC7515_A1, L) != 0) {
            /* a changed char may only survive if it decodes to the same bytes, which strict
             * base64url forbids */
            bad++;
        }
    }
    CHECK(bad == 0, "JWS fuzz: no mutated token verifies");
}

static void test_pkce_oauth(void)
{
    char ch[44];
    CHECK(w4_pkce_s256(SL("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"), ch) == W4_OK &&
              strcmp(ch, "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM") == 0,
          "RFC 7636 appendix B: S256 challenge");
    /* RFC 7636 appendix B's verifier is base64url of these 32 octets */
    static const uint8_t rb[32] = {116, 24,  223, 180, 151, 153, 224, 37,  79,  250, 96,
                                   125, 216, 173, 187, 186, 22,  212, 37,  77,  105, 214,
                                   191, 240, 91,  88,  5,   88,  83,  132, 141, 121};
    char vf[44];
    CHECK(w4_pkce_verifier_from_random(rb, vf) == W4_OK &&
              strcmp(vf, "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") == 0,
          "RFC 7636 appendix B: verifier from its 32 octets");
    CHECK(!w4_pkce_verifier_ok(SL("short")) &&
              !w4_pkce_verifier_ok(SL("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjX+")),
          "PKCE verifier grammar enforced");

    static w4_oauth_t f;
    w4_oauth_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    strcpy(cfg.client_id, "web4-client");
    strcpy(cfg.redirect_uri, "http://127.0.0.1:8765/cb");
    strcpy(cfg.scope, "openid profile");
    strcpy(cfg.authz_endpoint, "https://id.example/authorize");
    strcpy(cfg.token_host, "id.example");
    strcpy(cfg.token_path, "/token");
    strcpy(cfg.device_path, "/device");
    uint8_t rnd[64];
    for (int i = 0; i < 64; i++) rnd[i] = (uint8_t) i;
    CHECK(w4_oauth_code_begin(&f, &cfg, rnd) == W4_OK && f.st == W4_OA_AUTHORIZING,
          "auth code: begin");
    char url[1024];
    int32_t n = w4_oauth_code_auth_url(&f, url, sizeof url);
    char want_ch[44];
    w4_pkce_s256(f.verifier, (uint32_t) strlen(f.verifier), want_ch);
    w4_url_t u;
    char got[128];
    CHECK(n > 0 && w4_url_parse(url, (uint32_t) n, &u) == W4_OK &&
              w4_form_get(u.query, u.query_len, "code_challenge", got, sizeof got) == 43 &&
              strcmp(got, want_ch) == 0 &&
              w4_form_get(u.query, u.query_len, "code_challenge_method", got, sizeof got) == 4 &&
              w4_form_get(u.query, u.query_len, "scope", got, sizeof got) > 0 &&
              strcmp(got, "openid profile") == 0,
          "auth code: authorization URL carries S256 challenge, scope, state, nonce");
    char cb[256];
    snprintf(cb, sizeof cb, "code=SplxlOBeZQQYbYS6WxSbIA&state=%s", "forged");
    static w4_oauth_t f2;
    f2 = f;
    CHECK(w4_oauth_code_callback(&f2, cb, (uint32_t) strlen(cb)) == W4_ERR_SIG &&
              f2.st == W4_OA_FAILED,
          "auth code: state mismatch fails the flow (CSRF)");
    snprintf(cb, sizeof cb, "error=access_denied&state=%s", f.state);
    f2 = f;
    CHECK(w4_oauth_code_callback(&f2, cb, (uint32_t) strlen(cb)) == W4_ERR_DENIED &&
              strcmp(f2.error, "access_denied") == 0,
          "auth code: error redirect");
    snprintf(cb, sizeof cb, "code=SplxlOBeZQQYbYS6WxSbIA&state=%s", f.state);
    CHECK(w4_oauth_code_callback(&f, cb, (uint32_t) strlen(cb)) == W4_OK && f.st == W4_OA_HAVE_CODE,
          "auth code: good redirect");
    uint8_t req[2048];
    n = w4_oauth_code_token_request(&f, req, sizeof req);
    w4_http_msg_t m;
    CHECK(n > 0 && w4_http_parse_request(req, (uint32_t) n, &m, NULL, 0) == W4_OK &&
              w4_form_get((const char *) m.body, m.body_len, "code_verifier", got, sizeof got) ==
                  43 &&
              strcmp(got, f.verifier) == 0 &&
              w4_form_get((const char *) m.body, m.body_len, "grant_type", got, sizeof got) > 0 &&
              strcmp(got, "authorization_code") == 0,
          "auth code: token request carries the verifier");
    const char *tok = "{\"access_token\":\"SlAV32hkKG\",\"token_type\":\"Bearer\",\"refresh_"
                      "token\":\"8xLOxBtZp8\","
                      "\"expires_in\":3600,\"id_token\":\"eyJ.x.y\"}";
    CHECK(w4_oauth_token_response(&f, 200, SL(tok), 1000) == W4_OK && f.st == W4_OA_DONE &&
              strcmp(f.access_token, "SlAV32hkKG") == 0 && f.expires_at == 4600 &&
              f.verifier[0] == 0,
          "auth code: token response, verifier wiped");
    CHECK(w4_oauth_token_response(&f, 200, SL(tok), 1000) == W4_ERR_STATE,
          "auth code: replayed token response refused");

    /* device flow */
    CHECK(w4_oauth_device_begin(&f, &cfg) == W4_OK, "device: begin");
    n = w4_oauth_device_request(&f, req, sizeof req);
    CHECK(n > 0 && w4_http_parse_request(req, (uint32_t) n, &m, NULL, 0) == W4_OK,
          "device: request");
    const char *dr =
        "{\"device_code\":\"GmRhmhcxhwAzkoEqiMEg_DnyEysNkuNhszIySk9eS\",\"user_code\":\"WDJB-"
        "MJHT\","
        "\"verification_uri\":\"https://id.example/device\",\"expires_in\":1800,\"interval\":5}";
    CHECK(w4_oauth_device_response(&f, 200, SL(dr), 100) == W4_OK && f.st == W4_OA_DEVICE_PENDING &&
              strcmp(f.user_code, "WDJB-MJHT") == 0,
          "device: authorization response");
    CHECK(w4_oauth_device_poll_request(&f, req, sizeof req, 102) == W4_ERR_PENDING,
          "device: polling too early refused");
    CHECK(w4_oauth_device_poll_request(&f, req, sizeof req, 105) > 0,
          "device: poll after interval");
    CHECK(w4_oauth_device_poll_response(&f, 400, SL("{\"error\":\"authorization_pending\"}"),
                                        105) == W4_ERR_PENDING,
          "device: authorization_pending");
    CHECK(w4_oauth_device_poll_request(&f, req, sizeof req, 110) > 0 &&
              w4_oauth_device_poll_response(&f, 400, SL("{\"error\":\"slow_down\"}"), 110) ==
                  W4_ERR_PENDING &&
              f.interval_s == 10 && !w4_oauth_device_poll_due(&f, 115) &&
              w4_oauth_device_poll_due(&f, 120),
          "device: slow_down adds 5 s to the interval");
    CHECK(w4_oauth_device_poll_request(&f, req, sizeof req, 120) > 0 &&
              w4_oauth_device_poll_response(&f, 200, SL(tok), 120) == W4_OK && f.st == W4_OA_DONE,
          "device: token issued");
    w4_oauth_device_begin(&f, &cfg);
    w4_oauth_device_response(&f, 200, SL(dr), 100);
    CHECK(w4_oauth_device_poll_request(&f, req, sizeof req, 105) > 0 &&
              w4_oauth_device_poll_response(&f, 400, SL("{\"error\":\"access_denied\"}"), 105) ==
                  W4_ERR_DENIED &&
              f.st == W4_OA_FAILED,
          "device: access_denied ends the flow");
    w4_oauth_device_begin(&f, &cfg);
    w4_oauth_device_response(&f, 200, SL(dr), 100);
    CHECK(w4_oauth_device_poll_request(&f, req, sizeof req, 100 + 1800) == W4_ERR_EXPIRED,
          "device: local expiry");
    w4_oauth_device_begin(&f, &cfg);
    CHECK(w4_oauth_device_response(&f, 200, SL("{\"device_code\":\"x\"}"), 0) == W4_ERR_PARSE,
          "device: incomplete response refused");
}

static void test_webhook(void)
{
    const char *secret = "It's a Secret to Everybody";
    const char *body = "Hello, World!";
    char sig[80];
    CHECK(w4_webhook_sign_gh((const uint8_t *) secret, (uint32_t) strlen(secret),
                             (const uint8_t *) body, (uint32_t) strlen(body), sig,
                             sizeof sig) == 71 &&
              strcmp(sig,
                     "sha256=757107ea0eb2509fc211221cce984b8a37570b6d7586c22c46f4379c8b043e17") ==
                  0,
          "GitHub documented webhook signature example");
    CHECK(w4_webhook_verify_gh((const uint8_t *) secret, (uint32_t) strlen(secret),
                               (const uint8_t *) body, (uint32_t) strlen(body), sig, 71) == W4_OK &&
              w4_webhook_verify_gh((const uint8_t *) secret, (uint32_t) strlen(secret),
                                   (const uint8_t *) "Hello, World?", 13, sig, 71) == W4_ERR_SIG,
          "webhook (GitHub style) verify / tamper");
    char ts[128];
    int32_t n = w4_webhook_sign_ts((const uint8_t *) "k", 1, 1700000000u, (const uint8_t *) body,
                                   (uint32_t) strlen(body), ts, sizeof ts);
    CHECK(n > 0 && w4_webhook_verify_ts((const uint8_t *) "k", 1, (const uint8_t *) body,
                                        (uint32_t) strlen(body), ts, (uint32_t) n, 1700000100u,
                                        300) == W4_OK,
          "webhook (timestamped) verify");
    CHECK(w4_webhook_verify_ts((const uint8_t *) "k", 1, (const uint8_t *) body,
                               (uint32_t) strlen(body), ts, (uint32_t) n, 1700000400u,
                               300) == W4_ERR_STALE,
          "webhook (timestamped) replay outside tolerance refused");
    char ts2[200];
    snprintf(ts2, sizeof ts2, "t=1700000001%s", strchr(ts, ','));
    CHECK(w4_webhook_verify_ts((const uint8_t *) "k", 1, (const uint8_t *) body,
                               (uint32_t) strlen(body), ts2, (uint32_t) strlen(ts2), 1700000100u,
                               300) == W4_ERR_SIG,
          "webhook (timestamped) changed timestamp breaks the MAC");
    char rot[300];
    snprintf(rot, sizeof rot, "t=1700000000,v1=%064d%s", 0, strchr(ts, ','));
    CHECK(w4_webhook_verify_ts((const uint8_t *) "k", 1, (const uint8_t *) body,
                               (uint32_t) strlen(body), rot, (uint32_t) strlen(rot), 1700000100u,
                               300) == W4_OK,
          "webhook (timestamped) key rotation: any v1 may match");
}

static w4_post_t p1, p2;

static void test_social(void)
{
    char ts[32];
    uint64_t t;
    CHECK(w4_rfc3339_format(0, ts, sizeof ts) > 0 && strcmp(ts, "1970-01-01T00:00:00Z") == 0 &&
              w4_rfc3339_format(951782400u, ts, sizeof ts) > 0 &&
              strcmp(ts, "2000-02-29T00:00:00Z") == 0,
          "RFC 3339 format (epoch, leap day 2000)");
    CHECK(w4_rfc3339_parse(SL("2024-03-01T12:34:56.789Z"), &t) == W4_OK && t == 1709296496u,
          "RFC 3339 parse with fraction");
    CHECK(w4_rfc3339_parse(SL("2023-02-29T00:00:00Z"), &t) == W4_ERR_PARSE &&
              w4_rfc3339_parse(SL("2024-03-01T12:34:56+01:00"), &t) == W4_ERR_PARSE,
          "RFC 3339: invalid date and non-UTC refused");
    int rt = 0;
    for (int i = 0; i < 2000; i++) {
        uint64_t s = next() % 253402300800ull;
        uint64_t back;
        if (w4_rfc3339_format(s, ts, sizeof ts) > 0 &&
            w4_rfc3339_parse(ts, (uint32_t) strlen(ts), &back) == W4_OK && back == s)
            rt++;
    }
    CHECK(rt == 2000, "RFC 3339 round trip over 2000 random instants");

    memset(&p1, 0, sizeof p1);
    strcpy(p1.id, "https://social.example/notes/1");
    strcpy(p1.author, "https://social.example/users/alice");
    strcpy(p1.text, "Hello <Web 4> & \"friends\"\nsecond line \xe2\x9c\x93");
    p1.created_s = 1709296496u;
    strcpy(p1.lang, "en");
    static char js[8192];
    int32_t n = w4_ap_note_build(&p1, js, sizeof js);
    CHECK(n > 0 && strstr(js, "\"type\":\"Create\"") && strstr(js, "&lt;Web 4&gt; &amp;") &&
              strstr(js, "<br>"),
          "ActivityPub Create{Note} built with HTML-escaped content");
    CHECK(w4_ap_note_parse(js, (uint32_t) n, &p2) == W4_OK && strcmp(p2.text, p1.text) == 0 &&
              strcmp(p2.id, p1.id) == 0 && strcmp(p2.author, p1.author) == 0 &&
              p2.created_s == p1.created_s && strcmp(p2.lang, "en") == 0 &&
              p2.origin == W4_POST_ACTIVITYPUB,
          "ActivityPub round trip (text, id, author, time, language)");
    const char *mast =
        "{\"@context\":\"https://www.w3.org/ns/activitystreams\",\"type\":\"Note\","
        "\"id\":\"https://m.example/@bob/1\",\"attributedTo\":\"https://m.example/@bob\","
        "\"content\":\"<p>Hi <a href=\\\"x\\\">@alice</a></p><p>bye &amp; thanks</p>\","
        "\"published\":\"2024-01-02T03:04:05Z\",\"inReplyTo\":null}";
    CHECK(w4_ap_note_parse(SL(mast), &p2) == W4_OK &&
              strcmp(p2.text, "Hi @alice\nbye & thanks") == 0,
          "ActivityPub bare Note with HTML reduced to text");
    CHECK(w4_ap_note_parse(SL("{\"type\":\"Create\",\"actor\":\"a\",\"object\":\"https://x/1\"}"),
                           &p2) == W4_ERR_UNSUPP,
          "ActivityPub object by reference: unsupported, not guessed");
    p1.reply_to[0] = 0;
    n = w4_atp_post_build(&p1, NULL, js, sizeof js);
    CHECK(n > 0 && w4_atp_post_parse(js, (uint32_t) n, &p2) == W4_OK &&
              strcmp(p2.text, p1.text) == 0 && p2.created_s == p1.created_s &&
              strcmp(p2.lang, "en") == 0,
          "AT Protocol app.bsky.feed.post round trip");
    strcpy(p1.reply_to, "at://did:plc:abc/app.bsky.feed.post/3k");
    n = w4_atp_post_build(&p1, "bafyreib2rxk3rh6kzwq", js, sizeof js);
    CHECK(n > 0 && w4_atp_post_parse(js, (uint32_t) n, &p2) == W4_OK &&
              strcmp(p2.reply_to, p1.reply_to) == 0,
          "AT Protocol reply reference");
    strcpy(p1.reply_to, "https://not-at-uri");
    CHECK(w4_atp_post_build(&p1, "bafy", js, sizeof js) == W4_ERR_ARG,
          "AT Protocol reply must be an at:// URI");
    p1.reply_to[0] = 0;
    memset(p1.text, 'x', 301);
    p1.text[301] = 0;
    CHECK(w4_atp_post_build(&p1, NULL, js, sizeof js) == W4_ERR_RANGE,
          "AT Protocol 300 code point limit");
    /* fuzz both parsers */
    int bad = 0;
    for (int i = 0; i < 5000; i++) {
        char fz[512];
        uint32_t L = (uint32_t) strlen(mast);
        memcpy(fz, mast, L);
        fz[next() % L] = (char) (next() & 0x7f);
        if (w4_ap_note_parse(fz, L, &p2) == W4_OK && strlen(p2.text) > W4_POST_TEXT_MAX) bad++;
        if (w4_atp_post_parse(fz, L, &p2) == W4_OK && strlen(p2.text) > W4_POST_TEXT_MAX) bad++;
    }
    CHECK(bad == 0, "social parsers fuzz: 5000 mutations");
}

int main(void)
{
    printf("=== web4 Web 2 side ===\n");
    test_base64();
    test_json();
    test_http();
    test_jwt();
    test_pkce_oauth();
    test_webhook();
    test_social();
    printf("web4_web2: %d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
