/* test_browser.c — the ZXV browser module against byte-exact expectations.
 *
 * The rule this file follows, borrowed from src/net/test_dhcp.c: an assertion
 * that only says "the call returned ok" proves nothing. Every check below
 * pins a COMPUTED VALUE — the exact request bytes on the wire, the exact
 * de-chunked body, the exact token stream, the exact pixel box, the exact
 * coverage fraction.
 *
 * Three things this file exists specifically to prove:
 *   1. With no transport bound, every network entry point returns
 *      BROWSER_ERR_NO_TRANSPORT, NOT success. (§4)
 *   2. browser_verify_coverage() CAN fail, and the exact fractions that make
 *      it fail are checked. (§11)
 *   3. The tokenizer survives unterminated tags, unclosed quotes, stray
 *      angle brackets, 100000-deep nesting, 64 KiB of adversarial garbage,
 *      and EVERY prefix of a valid document — under ASan/UBSan. (§8)
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "browser.h"

static int failures = 0;
static int checks = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

/* Large objects live in .bss — a browser_t is ~8.8 MiB (see LIMITATIONS 9). */
static browser_t g_b;
static html_element_t g_els[1024];
static char g_scratch[262144];

/* ===================================================================== */
/* Mock transport — the ONLY place a "network" exists in this test.       */
/* ===================================================================== */

static char mock_req[16384];
static uint32_t mock_reqlen;
static char mock_host[256];
static uint16_t mock_port;
static bool mock_tls;
static const char *mock_resp;
static uint32_t mock_resplen;
static uint32_t mock_off;
static int mock_open_rc;   /* <0 to force an open failure */
static int mock_recv_rc;   /* <0 to force a recv failure  */
static bool mock_lie_recv; /* recv claims more than it wrote */
static int mock_opens, mock_closes;

static void mock_reset(const char *resp, uint32_t resplen)
{
    mock_reqlen = 0;
    mock_req[0] = 0;
    mock_host[0] = 0;
    mock_port = 0;
    mock_tls = false;
    mock_resp = resp;
    mock_resplen = resplen;
    mock_off = 0;
    mock_open_rc = 3;
    mock_recv_rc = 0;
    mock_lie_recv = false;
    mock_opens = 0;
    mock_closes = 0;
}

static int mock_open(void *ctx, const char *host, uint16_t port, bool tls)
{
    (void) ctx;
    mock_opens++;
    snprintf(mock_host, sizeof(mock_host), "%s", host);
    mock_port = port;
    mock_tls = tls;
    return mock_open_rc;
}

static int mock_send(void *ctx, int conn, const uint8_t *buf, uint32_t n)
{
    (void) ctx;
    (void) conn;
    if (mock_reqlen + n < sizeof(mock_req)) {
        memcpy(mock_req + mock_reqlen, buf, n);
        mock_reqlen += n;
        mock_req[mock_reqlen] = 0;
    }
    return (int) n;
}

/* Deliberately dribbles 7 bytes at a time so the receive loop is exercised. */
static int mock_recv(void *ctx, int conn, uint8_t *buf, uint32_t cap)
{
    (void) ctx;
    (void) conn;
    if (mock_recv_rc < 0) return mock_recv_rc;
    if (mock_lie_recv) { /* a hostile/broken driver */
        memcpy(buf, "AAAA", 4);
        return (int) (cap + 100); /* claims to have written past the buffer */
    }
    uint32_t left = mock_resplen - mock_off;
    if (left == 0) return 0;
    uint32_t n = left < 7 ? left : 7;
    if (n > cap) n = cap;
    memcpy(buf, mock_resp + mock_off, n);
    mock_off += n;
    return (int) n;
}

static void mock_close(void *ctx, int conn)
{
    (void) ctx;
    (void) conn;
    mock_closes++;
}

static const http_transport_ops_t MOCK_OPS = {mock_open, mock_send, mock_recv, mock_close, NULL};

/* ===================================================================== */

static uint32_t parse_into(browser_tab_t *t, const char *html)
{
    uint32_t n = 0;
    html_parse(html, (uint32_t) strlen(html), t->elements, BROWSER_MAX_ELEMENTS, &n);
    t->num_elements = n;
    return n;
}

int main(void)
{
    printf("=== ZXV browser: URL / HTTP / HTML / layout / tabs ===\n\n");

    /* ================= 1. URL parsing ================= */
    printf("--- 1. URL parsing ---\n");
    {
        browser_url_t u;
        CHECK(browser_parse_url("https://user:pw@Example.COM:8443/a/b?x=1&y=2#frag", &u) ==
                  BROWSER_OK,
              "a full URL parses");
        CHECK(strcmp(u.scheme, "https") == 0, "scheme is lowercased to \"https\"");
        CHECK(strcmp(u.host, "example.com") == 0,
              "host is lowercased and userinfo is stripped -> \"example.com\"");
        CHECK(u.port == 8443, "explicit port 8443 is taken");
        CHECK(strcmp(u.path, "/a/b") == 0, "path is \"/a/b\"");
        CHECK(strcmp(u.query, "x=1&y=2") == 0, "query is \"x=1&y=2\" without the '?'");
        CHECK(strcmp(u.fragment, "frag") == 0, "fragment is \"frag\" without the '#'");
        CHECK(u.tls && u.absolute && u.valid, "https => tls, absolute, valid");

        CHECK(browser_parse_url("http://example.com", &u) == BROWSER_OK && u.port == 80 &&
                  strcmp(u.path, "/") == 0,
              "a bare authority gets port 80 and path \"/\"");

        CHECK(browser_parse_url("//cdn.example.com/lib.js", &u) == BROWSER_OK && u.scheme[0] == 0 &&
                  strcmp(u.host, "cdn.example.com") == 0 && !u.absolute,
              "\"//host/path\" has an authority but no scheme, so it is not absolute");

        CHECK(browser_parse_url("/just/a/path?q", &u) == BROWSER_OK && u.host[0] == 0 &&
                  strcmp(u.path, "/just/a/path") == 0 && strcmp(u.query, "q") == 0 && !u.absolute,
              "a root-relative reference keeps its path and query");

        CHECK(browser_parse_url("about:blank", &u) == BROWSER_OK &&
                  strcmp(u.scheme, "about") == 0 && strcmp(u.path, "blank") == 0 && !u.absolute,
              "\"about:blank\" is scheme + opaque path, not absolute");

        /* --- rejections: none of these may come back BROWSER_OK --- */
        CHECK(browser_parse_url(NULL, &u) == BROWSER_ERR_BAD_URL, "NULL url is refused");
        CHECK(browser_parse_url("", &u) == BROWSER_ERR_BAD_URL, "empty url is refused");
        CHECK(browser_parse_url("http://", &u) == BROWSER_ERR_BAD_URL,
              "\"http://\" with no host is refused");
        CHECK(browser_parse_url("http://:80/", &u) == BROWSER_ERR_BAD_URL,
              "an empty host with a port is refused");
        CHECK(browser_parse_url("http://h:99999/", &u) == BROWSER_ERR_BAD_URL,
              "a port above 65535 is refused");
        CHECK(browser_parse_url("http://h:8o/", &u) == BROWSER_ERR_BAD_URL,
              "a non-numeric port is refused");
        CHECK(browser_parse_url("http://h:0/", &u) == BROWSER_ERR_BAD_URL, "port 0 is refused");
        CHECK(browser_parse_url("http://ho st/", &u) == BROWSER_ERR_BAD_URL,
              "whitespace inside the host is refused");
        CHECK(browser_parse_url("http://h/a\r\nX-Evil: 1", &u) == BROWSER_ERR_BAD_URL,
              "a CRLF in the path is refused — that is request-splitting");
        CHECK(!u.valid, "a rejected URL leaves out->valid false, never OK-with-garbage");
        {
            char big[BROWSER_MAX_URL_LEN + 64];
            memset(big, 'a', sizeof(big) - 1);
            big[sizeof(big) - 1] = 0;
            memcpy(big, "http://h/", 9);
            CHECK(browser_parse_url(big, &u) == BROWSER_ERR_BAD_URL,
                  "a URL at/over BROWSER_MAX_URL_LEN is refused, not truncated");

            /* Over-long OVERALL, yet every individual component would fit its
             * field — so only the total-length check can catch it. Such a URL
             * cannot round-trip through browser_tab_t.url. */
            uint32_t m = 0;
            memcpy(big, "http://", 7);
            m = 7;
            memset(big + m, 'h', 200);
            m += 200;
            big[m++] = '/';
            memset(big + m, 'a', 1900);
            m += 1900;
            big[m] = 0;
            CHECK(m == 2108 && m > BROWSER_MAX_URL_LEN,
                  "the probe URL is 2108 chars, over the 2048 limit");
            CHECK(browser_parse_url(big, &u) == BROWSER_ERR_BAD_URL,
                  "an over-long URL is refused even when host (200) and path "
                  "(1901) would each fit their own field");
        }
        CHECK(browser_parse_url("http://h/", NULL) == BROWSER_ERR_ARG,
              "a NULL output struct is BROWSER_ERR_ARG");

        /* browser.h promises `out` is ZEROED on a bad URL. The parser bails
         * from the middle of the split, so it used to hand back scheme="http",
         * host="h" and a path still carrying the rejected CRLF payload — with
         * only `valid` telling the truth. Check the whole struct, byte for
         * byte, on a failure taken at every stage of the split. */
        {
            const char *bads[] = {
                "http://h/a\r\nX-Evil: 1", /* fails at the CRLF check    */
                "http://h:0/",             /* fails in the port          */
                "http://",                 /* fails in the authority     */
                "",                        /* fails on length            */
            };
            browser_url_t zero;
            memset(&zero, 0, sizeof(zero));
            int leaked = 0;
            for (unsigned k = 0; k < sizeof(bads) / sizeof(bads[0]); k++) {
                browser_url_t d;
                memset(&d, 0xA5, sizeof(d));
                if (browser_parse_url(bads[k], &d) == BROWSER_OK) leaked++;
                if (memcmp(&d, &zero, sizeof(d)) != 0) {
                    leaked++;
                    printf("       \"%s\" left scheme=\"%s\" host=\"%s\" path=\"%s\"\n", bads[k],
                           d.scheme, d.host, d.path);
                }
            }
            CHECK(leaked == 0, "a rejected URL leaves `out` bit-for-bit zero at every failure "
                               "stage — no host, and no CRLF payload, survives");
        }
    }

    /* ================= 2. URL resolution (RFC 3986 §5.4) ================= */
    printf("\n--- 2. relative URL resolution (RFC 3986 5.4) ---\n");
    {
        /* The WHOLE of RFC 3986 §5.4 — 23 normal (§5.4.1) + 19 abnormal
         * (§5.4.2) references against the RFC's own base. An earlier revision
         * of this file carried 15 hand-picked cases, and the ones it left out
         * were exactly the ones that failed: "g:h" and "http:g" (a reference
         * WITH a scheme) were being merged into the base's path.
         * The single deviation is "//g", which this module normalises to
         * "http://g/" (empty path serialises as "/") where the RFC writes
         * "http://g"; that is stated in browser.h. */
        const char *base = "http://a/b/c/d;p?q";
        struct {
            const char *ref;
            const char *want;
        } cases[] = {
            /* --- §5.4.1 normal examples --- */
            {"g:h", "g:h"},
            {"g", "http://a/b/c/g"},
            {"./g", "http://a/b/c/g"},
            {"g/", "http://a/b/c/g/"},
            {"/g", "http://a/g"},
            {"//g", "http://g/"}, /* RFC: "http://g" */
            {"?y", "http://a/b/c/d;p?y"},
            {"g?y", "http://a/b/c/g?y"},
            {"#s", "http://a/b/c/d;p?q#s"},
            {"g#s", "http://a/b/c/g#s"},
            {"g?y#s", "http://a/b/c/g?y#s"},
            {";x", "http://a/b/c/;x"},
            {"g;x", "http://a/b/c/g;x"},
            {"g;x?y#s", "http://a/b/c/g;x?y#s"},
            {"", "http://a/b/c/d;p?q"},
            {".", "http://a/b/c/"},
            {"./", "http://a/b/c/"},
            {"..", "http://a/b/"},
            {"../", "http://a/b/"},
            {"../g", "http://a/b/g"},
            {"../..", "http://a/"},
            {"../../", "http://a/"},
            {"../../g", "http://a/g"},
            /* --- §5.4.2 abnormal examples --- */
            {"../../../g", "http://a/g"},
            {"../../../../g", "http://a/g"},
            {"/./g", "http://a/g"},
            {"/../g", "http://a/g"},
            {"g.", "http://a/b/c/g."},
            {".g", "http://a/b/c/.g"},
            {"g..", "http://a/b/c/g.."},
            {"..g", "http://a/b/c/..g"},
            {"./../g", "http://a/b/g"},
            {"./g/.", "http://a/b/c/g/"},
            {"g/./h", "http://a/b/c/g/h"},
            {"g/../h", "http://a/b/c/h"},
            {"g;x=1/./y", "http://a/b/c/g;x=1/y"},
            {"g;x=1/../y", "http://a/b/c/y"},
            {"g?y/./x", "http://a/b/c/g?y/./x"},
            {"g?y/../x", "http://a/b/c/g?y/../x"},
            {"g#s/./x", "http://a/b/c/g#s/./x"},
            {"g#s/../x", "http://a/b/c/g#s/../x"},
            {"http:g", "http:g"}, /* STRICT parser */
            /* --- not from the RFC --- */
            {"http://z/q", "http://z/q"},
        };
        for (unsigned k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
            char out[BROWSER_MAX_URL_LEN];
            int rc = browser_resolve_url(base, cases[k].ref, out, sizeof(out));
            char msg[256];
            snprintf(msg, sizeof(msg), "resolve(\"%s\") == \"%s\"%s%s", cases[k].ref, cases[k].want,
                     (rc == BROWSER_OK && strcmp(out, cases[k].want) == 0) ? "" : "  got: ",
                     (rc == BROWSER_OK && strcmp(out, cases[k].want) == 0) ? "" : out);
            CHECK(rc == BROWSER_OK && strcmp(out, cases[k].want) == 0, msg);
        }
        char out[64];
        CHECK(browser_resolve_url("/not/absolute", "g", out, sizeof(out)) == BROWSER_ERR_BAD_URL,
              "a relative base cannot resolve anything");
        CHECK(browser_resolve_url(NULL, "g", out, sizeof(out)) == BROWSER_ERR_ARG,
              "resolve(NULL base) is BROWSER_ERR_ARG");

        /* A reference with a NON-http scheme must come back untouched. Merging
         * it into the base rewrote "mailto:a@b" into "http://a/b/c/a@b" — a
         * fetchable address on the base's own host that the document never
         * wrote. */
        {
            struct {
                const char *ref, *want;
            } opaque[] = {
                {"mailto:user@example.com", "mailto:user@example.com"},
                {"javascript:alert(1)", "javascript:alert(1)"},
                {"about:blank", "about:blank"},
                {"tel:+15550100", "tel:+15550100"},
            };
            int bad = 0;
            char o[128];
            for (unsigned k = 0; k < sizeof(opaque) / sizeof(opaque[0]); k++) {
                if (browser_resolve_url(base, opaque[k].ref, o, sizeof(o)) != BROWSER_OK ||
                    strcmp(o, opaque[k].want) != 0) {
                    bad++;
                    printf("       opaque ref \"%s\" -> \"%s\"\n", opaque[k].ref, o);
                }
            }
            CHECK(bad == 0, "mailto:/javascript:/about:/tel: references resolve to themselves, "
                            "never into an http URL on the base's host");
        }

        /* An output buffer too small must NOT come back OK with a prefix.
         * "http://a/b/c/g" clipped to 8 bytes is "http://", and clipped to 9
         * is "http://a" — a real, different, fetchable address. */
        {
            char small[9];
            int rc = browser_resolve_url(base, "g", small, sizeof(small));
            CHECK(rc == BROWSER_ERR_FULL,
                  "resolving into a buffer 9 bytes wide is BROWSER_ERR_FULL");
            CHECK(small[0] == '\0', "...and leaves the buffer EMPTY, not holding \"http://a\"");
            char exact[15]; /* strlen("http://a/b/c/g") + 1 */
            CHECK(browser_resolve_url(base, "g", exact, sizeof(exact)) == BROWSER_OK &&
                      strcmp(exact, "http://a/b/c/g") == 0,
                  "a buffer of exactly strlen+1 succeeds and holds the whole URL");
            CHECK(browser_resolve_url(base, "g", exact, sizeof(exact) - 1) == BROWSER_ERR_FULL,
                  "one byte less is BROWSER_ERR_FULL — the boundary is exact");

            /* Sweep every capacity: the result is either the exact URL or an
             * error. There must be no cap at which OK yields a short string. */
            int liar = 0;
            for (uint32_t cap = 1; cap <= 40; cap++) {
                char buf[40];
                memset(buf, 0x7F, sizeof(buf));
                if (browser_resolve_url(base, "g", buf, cap) == BROWSER_OK &&
                    strcmp(buf, "http://a/b/c/g") != 0)
                    liar++;
            }
            CHECK(liar == 0, "across caps 1..40 there is no capacity where OK means a "
                             "truncated URL");
        }

        /* remove_dot_segments must be correct at ANY depth. A 64-entry offset
         * stack made a single ".." unwind sixteen segments once the path went
         * past 64 deep — silently, under BROWSER_OK. */
        {
            char deep[BROWSER_MAX_URL_LEN], got[BROWSER_MAX_URL_LEN];
            char want[BROWSER_MAX_URL_LEN];
            uint32_t m = (uint32_t) snprintf(deep, sizeof(deep), "http://h");
            uint32_t w = (uint32_t) snprintf(want, sizeof(want), "http://h");
            for (int k = 0; k < 80; k++) {
                m += (uint32_t) snprintf(deep + m, sizeof(deep) - m, "/s%d", k);
                if (k < 79) w += (uint32_t) snprintf(want + w, sizeof(want) - w, "/s%d", k);
            }
            m += (uint32_t) snprintf(deep + m, sizeof(deep) - m, "/f");
            w += (uint32_t) snprintf(want + w, sizeof(want) - w, "/z");
            /* base .../s0../s79/f  +  "../z"  =>  .../s0../s78/z */
            int rc = browser_resolve_url(deep, "../z", got, sizeof(got));
            CHECK(rc == BROWSER_OK && strcmp(got, want) == 0,
                  "\"..\" on an 80-segment path drops exactly ONE segment");
        }
    }

    /* ================= 3. HTTP request building ================= */
    printf("\n--- 3. HTTP/1.1 request building (byte-exact) ---\n");
    {
        browser_url_t u;
        char req[1024];
        browser_parse_url("http://example.com/index.html?a=1", &u);
        int n = http_build_request(req, sizeof(req), HTTP_GET, &u, "ZXV/1", NULL, 0);
        const char *want = "GET /index.html?a=1 HTTP/1.1\r\n"
                           "Host: example.com\r\n"
                           "User-Agent: ZXV/1\r\n"
                           "Accept: */*\r\n"
                           "Connection: close\r\n"
                           "\r\n";
        CHECK(n == (int) strlen(want) && memcmp(req, want, (size_t) n) == 0,
              "GET serialises to the exact expected byte string");

        browser_parse_url("https://x.io:8443/p", &u);
        n = http_build_request(req, sizeof(req), HTTP_POST, &u, "ZXV/1", "k=v", 3);
        const char *want2 = "POST /p HTTP/1.1\r\n"
                            "Host: x.io:8443\r\n"
                            "User-Agent: ZXV/1\r\n"
                            "Accept: */*\r\n"
                            "Connection: close\r\n"
                            "Content-Length: 3\r\n"
                            "\r\n"
                            "k=v";
        CHECK(n == (int) strlen(want2) && memcmp(req, want2, (size_t) n) == 0,
              "POST carries a non-default port in Host and an exact Content-Length");

        browser_parse_url("http://h/p#frag", &u);
        n = http_build_request(req, sizeof(req), HTTP_GET, &u, "U", NULL, 0);
        CHECK(n > 0 && memcmp(req, "GET /p HTTP/1.1\r\n", 17) == 0,
              "the fragment is NOT sent to the server");

        browser_parse_url("https://h/p", &u);
        n = http_build_request(req, sizeof(req), HTTP_GET, &u, "U", NULL, 0);
        CHECK(n > 0 && strstr(req, "Host: h\r\n") != NULL,
              "the default port (443 for https) is omitted from Host");

        CHECK(http_build_request(req, 20, HTTP_GET, &u, "U", NULL, 0) == BROWSER_ERR_FULL,
              "a buffer too small returns BROWSER_ERR_FULL, it does not overflow");
        browser_parse_url("/relative", &u);
        CHECK(http_build_request(req, sizeof(req), HTTP_GET, &u, "U", NULL, 0) ==
                  BROWSER_ERR_BAD_URL,
              "a relative URL cannot be turned into a request");
        browser_parse_url("ftp://h/f", &u);
        CHECK(http_build_request(req, sizeof(req), HTTP_GET, &u, "U", NULL, 0) ==
                  BROWSER_ERR_BAD_URL,
              "a non-http(s) scheme is refused");
        browser_parse_url("http://h/p", &u);
        CHECK(http_build_request(req, sizeof(req), (http_method_t) 99, &u, "U", NULL, 0) ==
                  BROWSER_ERR_ARG,
              "an unknown method is refused");
        CHECK(http_build_request(NULL, 10, HTTP_GET, &u, "U", NULL, 0) == BROWSER_ERR_ARG,
              "a NULL output buffer is refused");

        /* browser_parse_url() can never produce a host containing a delimiter,
         * but http_build_request() takes a caller-owned struct. It must re-
         * check, because the host is concatenated straight into a header. */
        {
            browser_url_t evil;
            memset(&evil, 0, sizeof(evil));
            snprintf(evil.scheme, sizeof(evil.scheme), "http");
            snprintf(evil.path, sizeof(evil.path), "/");
            evil.port = 80;
            evil.valid = true;
            evil.absolute = true;

            snprintf(evil.host, sizeof(evil.host), "good.example/@evil.example");
            CHECK(http_build_request(req, sizeof(req), HTTP_GET, &evil, "U", NULL, 0) ==
                      BROWSER_ERR_BAD_URL,
                  "a hand-crafted host containing '/' or '@' is refused by the builder");

            snprintf(evil.host, sizeof(evil.host), "h\r\nX-Injected: 1");
            CHECK(http_build_request(req, sizeof(req), HTTP_GET, &evil, "U", NULL, 0) ==
                      BROWSER_ERR_BAD_URL,
                  "a hand-crafted host containing CRLF is refused — no header injection");

            snprintf(evil.host, sizeof(evil.host), "ok.example");
            CHECK(http_build_request(req, sizeof(req), HTTP_GET, &evil, "U", NULL, 0) > 0,
                  "...while the same struct with a clean host still builds");
        }
    }

    /* ================= 4. HTTP response parsing ================= */
    printf("\n--- 4. HTTP/1.1 response parsing ---\n");
    {
        http_response_t r;
        uint8_t buf[4096];

        const char *a = "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/html; charset=utf-8\r\n"
                        "Content-Length: 5\r\n\r\nhello";
        memcpy(buf, a, strlen(a));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(a)) == BROWSER_OK,
              "a Content-Length framed response parses");
        CHECK(r.status_code == 200, "status_code is exactly 200");
        CHECK(strcmp(r.status_text, "OK") == 0, "status_text is \"OK\"");
        CHECK(strcmp(r.content_type, "text/html; charset=utf-8") == 0,
              "Content-Type value is captured whole");
        CHECK(r.content_length == 5 && r.body_len == 5, "body_len is exactly 5");
        CHECK(memcmp(r.body, "hello", 5) == 0, "body bytes are \"hello\"");
        CHECK(r.success && !r.chunked, "success, not chunked");
        {
            char v[64];
            CHECK(http_get_header(&r, "CONTENT-length", v, sizeof(v)) == 1 && strcmp(v, "5") == 0,
                  "http_get_header is case-insensitive and returns \"5\"");
            CHECK(http_get_header(&r, "X-Absent", v, sizeof(v)) == 0 && v[0] == 0,
                  "an absent header returns 0 and an empty string");
        }

        const char *c = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                        "5\r\nHello\r\n7\r\n, World\r\n0\r\n\r\n";
        memcpy(buf, c, strlen(c));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(c)) == BROWSER_OK,
              "a chunked response parses");
        CHECK(r.chunked, "chunked is flagged");
        CHECK(r.body_len == 12, "the de-chunked body is exactly 12 bytes");
        CHECK(memcmp(r.body, "Hello, World", 12) == 0,
              "the de-chunked body reassembles to \"Hello, World\"");

        const char *cx = "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n"
                         "4;name=value\r\nWiki\r\n0\r\n\r\n";
        memcpy(buf, cx, strlen(cx));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(cx)) == BROWSER_OK &&
                  r.body_len == 4 && memcmp(r.body, "Wiki", 4) == 0,
              "a chunk extension \";name=value\" is skipped, body is \"Wiki\"");

        const char *tr = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                         "3\r\nabc\r\n0\r\nX-Trail: 1\r\n\r\n";
        memcpy(buf, tr, strlen(tr));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(tr)) == BROWSER_OK &&
                  r.body_len == 3 && memcmp(r.body, "abc", 3) == 0,
              "a trailer section after the 0-chunk is consumed");

        const char *t1 = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabcd";
        memcpy(buf, t1, strlen(t1));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(t1)) == BROWSER_ERR_TRUNCATED,
              "Content-Length 10 with 4 bytes present is TRUNCATED, not OK");
        CHECK(!r.success && r.body_len == 4,
              "and it reports the 4 bytes it really has, with success == false");

        const char *t2 = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nHel";
        memcpy(buf, t2, strlen(t2));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(t2)) == BROWSER_ERR_TRUNCATED,
              "a chunk cut mid-data is TRUNCATED");
        CHECK(!r.success, "and success stays false");

        const char *t3 = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nHello\r\n";
        memcpy(buf, t3, strlen(t3));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(t3)) == BROWSER_ERR_TRUNCATED,
              "a chunked stream with no terminating 0-chunk is TRUNCATED");

        const char *bad1 = "HTTX/1.1 200 OK\r\n\r\n";
        memcpy(buf, bad1, strlen(bad1));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(bad1)) == BROWSER_ERR_PROTOCOL,
              "a non-HTTP status line is PROTOCOL");
        const char *bad2 = "HTTP/1.1 2X0 OK\r\n\r\n";
        memcpy(buf, bad2, strlen(bad2));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(bad2)) == BROWSER_ERR_PROTOCOL,
              "a non-numeric status code is PROTOCOL");
        const char *bad3 = "HTTP/1.1 200 OK\r\nX: 1\r\n";
        memcpy(buf, bad3, strlen(bad3));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(bad3)) == BROWSER_ERR_TRUNCATED,
              "headers with no blank-line terminator are TRUNCATED");
        const char *bad4 = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\nab\r\n";
        memcpy(buf, bad4, strlen(bad4));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(bad4)) == BROWSER_ERR_PROTOCOL,
              "a non-hex chunk size is PROTOCOL");
        /* A chunk size of 0x100000000 truncates to 0 in 32 bits, which would
         * make an oversized chunk look like the TERMINATING chunk. The size
         * ceiling in dechunk() exists precisely to stop that. */
        const char *ovf = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                          "100000000\r\nAAAA\r\n0\r\n\r\n";
        memcpy(buf, ovf, strlen(ovf));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(ovf)) == BROWSER_ERR_TOO_LARGE,
              "a chunk size that would truncate in 32 bits is TOO_LARGE, not an empty body");
        CHECK(r.body_len == 0 && !r.success, "and it claims no body at all");

        const char *bad5 = "HTTP/1.1 200 OK\r\nContent-Length: 5x\r\n\r\nhello";
        memcpy(buf, bad5, strlen(bad5));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(bad5)) == BROWSER_ERR_PROTOCOL,
              "a non-numeric Content-Length is PROTOCOL");
        CHECK(http_parse_response(&r, buf, 0) == BROWSER_ERR_TRUNCATED,
              "a zero-length buffer is TRUNCATED");
        CHECK(http_parse_response(NULL, buf, 10) == BROWSER_ERR_ARG,
              "a NULL response struct is BROWSER_ERR_ARG");

        const char *nc = "HTTP/1.1 204 No Content\r\nX: y\r\n\r\n";
        memcpy(buf, nc, strlen(nc));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(nc)) == BROWSER_OK &&
                  r.status_code == 204 && r.body_len == 0 && r.success,
              "204 has no body by definition and still succeeds");

        /* Status codes with a NON-ZERO tens digit — 200/204/404 all hide a
         * mis-weighted middle digit behind a 0. */
        const char *tp = "HTTP/1.1 418 I'm a teapot\r\nContent-Length: 0\r\n\r\n";
        memcpy(buf, tp, strlen(tp));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(tp)) == BROWSER_OK &&
                  r.status_code == 418,
              "status \"418\" parses to exactly 418, not 100*4 + 100*1 + 8");
        CHECK(strcmp(r.status_text, "I'm a teapot") == 0,
              "and the multi-word reason phrase survives intact");
        const char *sv = "HTTP/1.1 599 X\r\nContent-Length: 0\r\n\r\n";
        memcpy(buf, sv, strlen(sv));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(sv)) == BROWSER_OK &&
                  r.status_code == 599,
              "status \"599\" parses to exactly 599");

        /* RFC 7230 §3.2.4 obs-fold. The header block claimed "folded headers"
         * were real and tested; the scanner was in fact skipping continuation
         * lines and returning only the first fragment. */
        const char *fold = "HTTP/1.1 200 OK\r\nX-Long: part1\r\n  part2\r\n"
                           "\tpart3\r\nContent-Length: 2\r\n\r\nhi";
        memcpy(buf, fold, strlen(fold));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(fold)) == BROWSER_OK &&
                  r.body_len == 2,
              "a response with folded headers parses");
        {
            char v[64];
            CHECK(http_get_header(&r, "X-Long", v, sizeof(v)) == 17 &&
                      strcmp(v, "part1 part2 part3") == 0,
                  "an obs-fold continuation is joined with a single space: "
                  "\"part1 part2 part3\", not \"part1\"");
        }
        /* ...and the same rule applied to Content-Length turns a smuggling
         * shape into a rejection instead of a wrong frame. */
        const char *foldcl = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n  0000\r\n\r\nhi";
        memcpy(buf, foldcl, strlen(foldcl));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(foldcl)) == BROWSER_ERR_PROTOCOL,
              "a FOLDED Content-Length unfolds to \"2 0000\" and is PROTOCOL, "
              "not a silently-honoured 2");

        /* RFC 7230 §3.3.3: two Content-Length values that disagree is invalid
         * framing. Taking the first is the response-splitting primitive. */
        const char *dupcl = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n"
                            "Content-Length: 9\r\n\r\nhixxxxxxx";
        memcpy(buf, dupcl, strlen(dupcl));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(dupcl)) == BROWSER_ERR_PROTOCOL,
              "two DIFFERING Content-Length headers are PROTOCOL, not \"first wins\"");
        CHECK(r.body_len == 0 && !r.success, "and no body is claimed");
        const char *samecl = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n"
                             "Content-Length: 2\r\n\r\nhi";
        memcpy(buf, samecl, strlen(samecl));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(samecl)) == BROWSER_OK &&
                  r.body_len == 2,
              "...while two AGREEING Content-Length headers are still fine (RFC 7230)");

        /* Content-Length + Transfer-Encoding: chunked — chunked wins. */
        const char *clte = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n"
                           "Transfer-Encoding: chunked\r\n\r\n5\r\nHello\r\n0\r\n\r\n";
        memcpy(buf, clte, strlen(clte));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(clte)) == BROWSER_OK && r.chunked &&
                  r.body_len == 5 && memcmp(r.body, "Hello", 5) == 0,
              "with both CL and TE:chunked, chunked frames the body (RFC 7230 3.3.3)");

        const char *negcl = "HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\nhi";
        memcpy(buf, negcl, strlen(negcl));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(negcl)) == BROWSER_ERR_PROTOCOL,
              "a negative Content-Length is PROTOCOL, not a huge unsigned length");

        /* A line whose name carries leading whitespace is NOT that header. */
        const char *sneak = "HTTP/1.1 200 OK\r\n X-Sneak: 1\r\nContent-Length: 2\r\n\r\nhi";
        memcpy(buf, sneak, strlen(sneak));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(sneak)) == BROWSER_OK &&
                  r.body_len == 2,
              "a leading-whitespace header line does not disturb framing");

        const char *cd = "HTTP/1.1 200 OK\r\nX: y\r\n\r\nfree-form body";
        memcpy(buf, cd, strlen(cd));
        CHECK(http_parse_response(&r, buf, (uint32_t) strlen(cd)) == BROWSER_OK &&
                  r.body_len == 14 && memcmp(r.body, "free-form body", 14) == 0,
              "with no framing headers the body runs to end-of-connection");

        /* A header block larger than http_response_t.headers must NOT change
         * how the body is framed — the framing headers are read from the
         * original buffer, not from the retained (truncated) copy. */
        {
            uint32_t p = 0;
            p += (uint32_t) snprintf(g_scratch + p, sizeof(g_scratch) - p, "HTTP/1.1 200 OK\r\n");
            for (int k = 0; k < 120; k++)
                p += (uint32_t) snprintf(g_scratch + p, sizeof(g_scratch) - p,
                                         "X-Pad-%03d: 0123456789012345678901234567890123456789"
                                         "0123456789012345678901234567890123456789\r\n",
                                         k);
            p += (uint32_t) snprintf(g_scratch + p, sizeof(g_scratch) - p,
                                     "Content-Length: 5\r\n\r\nHELLO");
            CHECK(p > 4096, "the padded header block really is bigger than 4096 bytes");
            CHECK(http_parse_response(&r, (uint8_t *) g_scratch, p) == BROWSER_OK &&
                      r.content_length == 5 && r.body_len == 5 && memcmp(r.body, "HELLO", 5) == 0,
                  "a >4 KiB header block still frames the body correctly");
        }
    }

    /* ================= 5. transport boundary ================= */
    printf("\n--- 5. the hardware boundary: no transport bound ---\n");
    {
        http_bind_transport(NULL);
        CHECK(!http_transport_bound(), "no transport is bound");
        http_response_t r;
        int rc = http_request(&r, HTTP_GET, "http://example.com/", NULL, 0);
        CHECK(rc == BROWSER_ERR_NO_TRANSPORT,
              "http_request with no transport returns BROWSER_ERR_NO_TRANSPORT");
        CHECK(rc != BROWSER_OK && rc != 0,
              "...and specifically NOT 0/success (this is the hollow-capability trap)");
        CHECK(r.body == NULL && r.body_len == 0 && !r.success,
              "...and leaves the response empty, claiming nothing");
        CHECK(http_request(&r, HTTP_GET, "ftp://h/x", NULL, 0) == BROWSER_ERR_BAD_URL,
              "a bad scheme is rejected before the transport is even consulted");
        CHECK(http_request(NULL, HTTP_GET, "http://h/", NULL, 0) == BROWSER_ERR_ARG,
              "a NULL response struct is refused");
        CHECK(http_request(&r, HTTP_GET, NULL, NULL, 0) == BROWSER_ERR_ARG,
              "a NULL url is refused");
    }

    /* ================= 6. transport bound: real bytes ================= */
    printf("\n--- 6. with a transport bound ---\n");
    {
        const char *resp = "HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\n<p>hi</p>\r\n";
        mock_reset(resp, (uint32_t) strlen(resp));
        http_bind_transport(&MOCK_OPS);
        CHECK(http_transport_bound(), "the mock transport is bound");

        http_response_t r;
        int rc = http_request(&r, HTTP_GET, "http://example.com/page?q=1", NULL, 0);
        CHECK(rc == BROWSER_OK, "http_request succeeds over the mock");
        const char *want = "GET /page?q=1 HTTP/1.1\r\n"
                           "Host: example.com\r\n"
                           "User-Agent: ZXV-Browser/1.0\r\n"
                           "Accept: */*\r\n"
                           "Connection: close\r\n"
                           "\r\n";
        CHECK(mock_reqlen == strlen(want) && memcmp(mock_req, want, mock_reqlen) == 0,
              "the exact bytes handed to the transport are the expected request");
        CHECK(strcmp(mock_host, "example.com") == 0 && mock_port == 80 && !mock_tls,
              "the transport was opened to example.com:80 with tls=false");
        CHECK(r.status_code == 200 && r.body_len == 11 && memcmp(r.body, "<p>hi</p>\r\n", 11) == 0,
              "the response body arrives intact through the 7-byte dribble");
        CHECK(mock_closes == 1, "the connection was closed exactly once");
        http_response_free(&r);
        CHECK(r.body == NULL, "http_response_free() detaches the borrowed body");

        mock_reset(resp, (uint32_t) strlen(resp));
        rc = http_request(&r, HTTP_GET, "https://secure.example/", NULL, 0);
        CHECK(rc == BROWSER_OK && mock_port == 443 && mock_tls,
              "an https URL asks the transport for port 443 with tls=true");
        http_response_free(&r);

        mock_reset(resp, (uint32_t) strlen(resp));
        mock_open_rc = -1;
        CHECK(http_request(&r, HTTP_GET, "http://h/", NULL, 0) == BROWSER_ERR_IO,
              "a failing open() is BROWSER_ERR_IO, never success");
        mock_reset(resp, (uint32_t) strlen(resp));
        mock_recv_rc = -5;
        CHECK(http_request(&r, HTTP_GET, "http://h/", NULL, 0) == BROWSER_ERR_IO,
              "a failing recv() is BROWSER_ERR_IO, never success");
        mock_reset("HTTP/1.1 200 OK\r\n", 0);
        CHECK(http_request(&r, HTTP_GET, "http://h/", NULL, 0) == BROWSER_ERR_IO,
              "a connection that closes with zero bytes is BROWSER_ERR_IO");

        /* A transport that LIES about how much it wrote must not be trusted
         * into an overrun. (ASan is the second half of this assertion.) */
        mock_reset(resp, (uint32_t) strlen(resp));
        mock_lie_recv = true;
        CHECK(http_request(&r, HTTP_GET, "http://h/", NULL, 0) == BROWSER_ERR_IO,
              "a recv() that returns more than the buffer it was given is BROWSER_ERR_IO");

        /* Oversized response: the receive slot fills exactly, so completeness
         * cannot be established. That must be an error, not a truncated OK. */
        {
            uint32_t big = BROWSER_HTTP_RX_BUF + 4096;
            static char bigbuf[BROWSER_HTTP_RX_BUF + 8192];
            int hn = snprintf(bigbuf, sizeof(bigbuf),
                              "HTTP/1.1 200 OK\r\nContent-Length: 200000\r\n\r\n");
            memset(bigbuf + hn, 'A', big - (uint32_t) hn);
            mock_reset(bigbuf, big);
            CHECK(http_request(&r, HTTP_GET, "http://h/", NULL, 0) == BROWSER_ERR_TOO_LARGE,
                  "a response larger than the receive slot is BROWSER_ERR_TOO_LARGE");
        }

        /* Pool accounting: BROWSER_HTTP_POOL_SLOTS live responses, then FULL. */
        {
            http_response_t a, b2, c;
            mock_reset(resp, (uint32_t) strlen(resp));
            CHECK(http_request(&a, HTTP_GET, "http://h/1", NULL, 0) == BROWSER_OK,
                  "pool slot 1 acquired");
            mock_reset(resp, (uint32_t) strlen(resp));
            CHECK(http_request(&b2, HTTP_GET, "http://h/2", NULL, 0) == BROWSER_OK,
                  "pool slot 2 acquired");
            mock_reset(resp, (uint32_t) strlen(resp));
            CHECK(http_request(&c, HTTP_GET, "http://h/3", NULL, 0) == BROWSER_ERR_FULL,
                  "a third live response returns BROWSER_ERR_FULL (2 slots, no malloc)");
            http_response_free(&a);
            mock_reset(resp, (uint32_t) strlen(resp));
            CHECK(http_request(&c, HTTP_GET, "http://h/3", NULL, 0) == BROWSER_OK,
                  "freeing one slot makes the next request succeed again");
            http_response_free(&b2);
            http_response_free(&c);
            http_response_free(&c); /* double free must be harmless */
            CHECK(c.body == NULL, "http_response_free() is idempotent");
        }
        http_bind_transport(NULL);
    }

    /* ================= 7. HTML tokenizer, well-formed ================= */
    printf("\n--- 7. HTML tokenizer ---\n");
    {
        const char *doc = "<!DOCTYPE html><html><head><title>Dragon</title></head>"
                          "<body><h1>Hi</h1><p>Hello <a href=\"/x\">there</a>!</p></body></html>";
        uint32_t n = 0;
        int rc = html_parse(doc, (uint32_t) strlen(doc), g_els, 1024, &n);
        CHECK(rc == BROWSER_OK, "a well-formed document parses without truncation");
        CHECK(n == 20, "it produces exactly 20 tokens");
        CHECK(g_els[0].kind == HTML_TOK_DOCTYPE && g_els[0].type == HTML_DOCTYPE,
              "token 0 is the DOCTYPE");
        CHECK(g_els[1].kind == HTML_TOK_START && g_els[1].type == HTML_HTML && g_els[1].depth == 0,
              "token 1 is <html> at depth 0");
        CHECK(g_els[4].kind == HTML_TOK_TEXT && strcmp(g_els[4].text, "Dragon") == 0 &&
                  g_els[4].depth == 3,
              "the title text is \"Dragon\" at depth 3");
        CHECK(g_els[5].kind == HTML_TOK_END && g_els[5].type == HTML_TITLE && g_els[5].depth == 2,
              "</title> is an END token that pops back to depth 2");
        CHECK(g_els[9].kind == HTML_TOK_TEXT && strcmp(g_els[9].text, "Hi") == 0 &&
                  g_els[9].font_size == 32 && g_els[9].bold,
              "text inside <h1> inherits font_size 32 and bold");
        CHECK(g_els[12].kind == HTML_TOK_TEXT && strcmp(g_els[12].text, "Hello ") == 0,
              "the run before <a> keeps its single trailing space");
        CHECK(g_els[13].type == HTML_A && g_els[13].is_link && strcmp(g_els[13].href, "/x") == 0,
              "<a href=\"/x\"> is a link");
        CHECK(g_els[14].kind == HTML_TOK_TEXT && strcmp(g_els[14].text, "there") == 0 &&
                  g_els[14].is_link && strcmp(g_els[14].href, "/x") == 0,
              "text inside the anchor inherits is_link and the href");
        CHECK(g_els[14].underline, "anchor text is underlined");
        CHECK(g_els[16].kind == HTML_TOK_TEXT && strcmp(g_els[16].text, "!") == 0 &&
                  !g_els[16].is_link,
              "text after </a> is NOT a link any more");
        CHECK(g_els[19].kind == HTML_TOK_END && g_els[19].type == HTML_HTML && g_els[19].depth == 0,
              "the last token is </html> back at depth 0");

        rc = html_parse(doc, (uint32_t) strlen(doc), g_els, 3, &n);
        CHECK(rc == BROWSER_ERR_TRUNCATED && n == 3,
              "a 3-element table fills and reports BROWSER_ERR_TRUNCATED with n == 3");

        CHECK(html_parse(NULL, 10, g_els, 8, &n) == BROWSER_ERR_ARG, "NULL html refused");
        CHECK(html_parse(doc, 10, NULL, 8, &n) == BROWSER_ERR_ARG, "NULL table refused");
        CHECK(html_parse(doc, 10, g_els, 0, &n) == BROWSER_ERR_ARG, "max 0 refused");
        CHECK(html_parse(doc, BROWSER_MAX_PAGE_SIZE + 1, g_els, 8, &n) == BROWSER_ERR_ARG,
              "a length above BROWSER_MAX_PAGE_SIZE is refused");

        /* entities */
        const char *ent = "<p>a &amp; b &lt;tag&gt; &#65;&#x42; &nosuch; &#;</p>";
        html_parse(ent, (uint32_t) strlen(ent), g_els, 1024, &n);
        CHECK(n == 3 && strcmp(g_els[1].text, "a & b <tag> AB &nosuch; &#;") == 0,
              "named, decimal and hex entities decode; unknown ones stay literal");

        /* nbsp becomes real UTF-8, not a lie about being ASCII */
        const char *nb = "<p>x&nbsp;y</p>";
        html_parse(nb, (uint32_t) strlen(nb), g_els, 1024, &n);
        CHECK(n == 3 && strlen(g_els[1].text) == 4 && (unsigned char) g_els[1].text[1] == 0xC2 &&
                  (unsigned char) g_els[1].text[2] == 0xA0,
              "&nbsp; encodes as the two UTF-8 bytes C2 A0");

        /* attributes digest + img intrinsic size */
        const char *im = "<br/><img src=\"a.png\" width=\"10\" height=\"20\" alt=\"A\">";
        html_parse(im, (uint32_t) strlen(im), g_els, 1024, &n);
        CHECK(n == 2, "a void element and a self-closed element are 2 tokens");
        CHECK(g_els[0].kind == HTML_TOK_SELF_CLOSE && g_els[0].type == HTML_BR,
              "<br/> is SELF_CLOSE");
        CHECK(g_els[1].kind == HTML_TOK_SELF_CLOSE && g_els[1].is_image &&
                  strcmp(g_els[1].src, "a.png") == 0 && strcmp(g_els[1].alt, "A") == 0,
              "<img> is a void element with src and alt captured");
        CHECK(g_els[1].w == 10 && g_els[1].h == 20,
              "the width/height attributes become the intrinsic box 10x20");

        /* An explicit "/>" on a NON-void element must also self-close, or the
         * whole rest of the document sinks a level. */
        const char *sc2 = "<span/>x";
        html_parse(sc2, (uint32_t) strlen(sc2), g_els, 1024, &n);
        CHECK(n == 2 && g_els[0].kind == HTML_TOK_SELF_CLOSE,
              "<span/> is SELF_CLOSE even though <span> is not a void element");
        CHECK(g_els[1].depth == 0, "so the text after it stays at depth 0 — nothing was left open");

        const char *at = "<div id=\"a\" class=\"b c\" hidden>";
        html_parse(at, (uint32_t) strlen(at), g_els, 1024, &n);
        CHECK(n == 1 && strcmp(g_els[0].attrs, "id=a;class=b c;hidden=;") == 0,
              "the attribute digest is \"id=a;class=b c;hidden=;\"");

        /* UNQUOTED attribute values end at whitespace, not at '>'. Getting
         * this wrong swallows every later attribute into the first value. */
        const char *uq = "<a href=/x/y title=hi id=z>t</a>";
        html_parse(uq, (uint32_t) strlen(uq), g_els, 1024, &n);
        CHECK(strcmp(g_els[0].href, "/x/y") == 0, "an unquoted href stops at the space: \"/x/y\"");
        CHECK(strcmp(g_els[0].attrs, "href=/x/y;title=hi;id=z;") == 0,
              "and the three unquoted attributes stay separate");

        /* The rawtext end-tag search must respect the tag-name boundary:
         * "</scriptfoo>" is NOT the end of a <script>. */
        const char *rb = "<script>x</scriptfoo>y</script>z";
        html_parse(rb, (uint32_t) strlen(rb), g_els, 1024, &n);
        CHECK(n == 4 && strcmp(g_els[1].text, "x</scriptfoo>y") == 0,
              "\"</scriptfoo>\" does not terminate <script> — the raw text runs on");
        CHECK(g_els[2].kind == HTML_TOK_END && g_els[3].kind == HTML_TOK_TEXT &&
                  strcmp(g_els[3].text, "z") == 0,
              "the real </script> does terminate it");

        /* script is RAWTEXT: markup inside it is text, not tags */
        const char *sc = "<script>if (a<b) { x = \"</p>\"; }</script><p>ok</p>";
        html_parse(sc, (uint32_t) strlen(sc), g_els, 1024, &n);
        CHECK(n == 6, "<script>..</script><p>ok</p> is 6 tokens");
        CHECK(g_els[1].kind == HTML_TOK_TEXT &&
                  strcmp(g_els[1].text, "if (a<b) { x = \"</p>\"; }") == 0,
              "script content is raw: the '<' and the '</p>' inside are NOT markup");
        CHECK(g_els[2].kind == HTML_TOK_END && g_els[2].type == HTML_SCRIPT,
              "the rawtext mode ends at </script>");

        /* comments */
        const char *cm = "<p>a<!--x-->b</p>";
        html_parse(cm, (uint32_t) strlen(cm), g_els, 1024, &n);
        CHECK(n == 5 && g_els[2].kind == HTML_TOK_COMMENT && strcmp(g_els[2].text, "x") == 0,
              "a comment is its own token");

        /* A text run longer than the field must fill it exactly, flag
         * truncation, and keep tokenizing the rest of the document. */
        {
            char *p = g_scratch;
            memcpy(p, "<p>", 3);
            memset(p + 3, 'x', 2000);
            memcpy(p + 2003, "</p><p>tail</p>", 15);
            html_parse(p, 2018, g_els, 1024, &n);
            CHECK(n == 6, "a 2000-char run plus a second paragraph is 6 tokens");
            CHECK(strlen(g_els[1].text) == BROWSER_MAX_TEXT_LEN - 1,
                  "the text field holds exactly BROWSER_MAX_TEXT_LEN-1 characters");
            CHECK(g_els[1].truncated,
                  "and the element is flagged truncated rather than pretending to be whole");
            CHECK(strcmp(g_els[4].text, "tail") == 0,
                  "and the document after the over-long run still tokenizes");
        }

        /* An attribute digest that overflows must also be flagged. */
        {
            char *p = g_scratch;
            uint32_t off = 0;
            memcpy(p, "<div", 4);
            off = 4;
            for (int k = 0; k < 40; k++)
                off += (uint32_t) snprintf(p + off, 64, " data-attribute-%02d=value%02d", k, k);
            p[off++] = '>';
            html_parse(p, off, g_els, 1024, &n);
            /* Each digested pair is "data-attribute-NN=valueNN;" = 26 bytes.
             * Nine fit (234); the tenth name and '=' fit (252) but its value
             * would pass 255, so the appender refuses it and records overflow. */
            CHECK(n == 1 && g_els[0].truncated && strlen(g_els[0].attrs) == 252,
                  "an over-long attribute digest stops at 252 bytes and flags truncated");
            CHECK(strncmp(g_els[0].attrs, "data-attribute-00=value00;data-attribute-01=value01;",
                          52) == 0,
                  "and what it did keep is intact from the start");
        }

        /* an href too long to hold in full must NOT become a navigable link */
        {
            char *p = g_scratch;
            int m = snprintf(p, 64, "<a href=\"");
            memset(p + m, 'z', 600);
            snprintf(p + m + 600, 32, "\">t</a>");
            html_parse(p, (uint32_t) strlen(p), g_els, 1024, &n);
            CHECK(g_els[0].type == HTML_A && g_els[0].truncated,
                  "a 600-char href sets html_element_t.truncated");
            CHECK(strlen(g_els[0].href) == BROWSER_MAX_HREF_LEN - 1,
                  "the href field holds exactly BROWSER_MAX_HREF_LEN-1 characters");
            CHECK(!g_els[0].is_link,
                  "and is_link stays FALSE — a truncated URL is a different URL");
        }
    }

    /* ================= 8. HOSTILE input ================= */
    printf("\n--- 8. hostile HTML (this is the point of the module) ---\n");
    {
        uint32_t n = 0;

        const char *u1 = "hello<div class=\"x\"";
        CHECK(html_parse(u1, (uint32_t) strlen(u1), g_els, 1024, &n) == BROWSER_OK && n == 1 &&
                  g_els[0].kind == HTML_TOK_TEXT,
              "a tag cut off at EOF is DISCARDED (HTML5 eof-in-tag), leaving 1 text token");

        const char *u2 = "<a href=\"/x>link</a>";
        CHECK(html_parse(u2, (uint32_t) strlen(u2), g_els, 1024, &n) == BROWSER_OK && n == 0,
              "an attribute quote never closed before EOF discards the tag");

        const char *u3 = "<a href=\"/x>link</a>\">tail";
        html_parse(u3, (uint32_t) strlen(u3), g_els, 1024, &n);
        CHECK(n == 2 && strcmp(g_els[0].href, "/x>link</a>") == 0 &&
                  strcmp(g_els[1].text, "tail") == 0,
              "a quote closed LATER swallows the '>' and the fake </a>, as browsers do");

        const char *u4 = "a < b > c";
        html_parse(u4, (uint32_t) strlen(u4), g_els, 1024, &n);
        CHECK(n == 1 && strcmp(g_els[0].text, "a < b > c") == 0,
              "stray '<' and '>' stay literal text");

        /* "</>", "</ >" and "</123>" are all bogus comments: each is consumed
         * up to its '>' and emits NOTHING, leaving exactly the three text
         * runs between them. "n >= 1" would pass for almost any behaviour,
         * including dropping two of the three runs. */
        const char *u5 = "</>x</ >y</123>z";
        html_parse(u5, (uint32_t) strlen(u5), g_els, 1024, &n);
        CHECK(n == 3 && strcmp(g_els[0].text, "x") == 0 && strcmp(g_els[1].text, "y") == 0 &&
                  strcmp(g_els[2].text, "z") == 0,
              "'</>', '</ >' and '</123>' are bogus comments that emit no token, "
              "leaving exactly the 3 text runs x/y/z");

        const char *u6 = "<!-- never closed";
        html_parse(u6, (uint32_t) strlen(u6), g_els, 1024, &n);
        CHECK(n == 1 && g_els[0].kind == HTML_TOK_COMMENT &&
                  strcmp(g_els[0].text, " never closed") == 0,
              "an unterminated comment runs to EOF and is still a single token");

        const char *u7 = "<!DOCTYPE";
        html_parse(u7, (uint32_t) strlen(u7), g_els, 1024, &n);
        CHECK(n == 1 && g_els[0].kind == HTML_TOK_DOCTYPE,
              "an unterminated doctype is still one token");

        const char *u8 = "<p>unclosed<p>and again";
        html_parse(u8, (uint32_t) strlen(u8), g_els, 1024, &n);
        CHECK(n == 4, "unclosed <p> tags do not stall the tokenizer (4 tokens)");

        const char *u9 = "</div></div></div>";
        html_parse(u9, (uint32_t) strlen(u9), g_els, 1024, &n);
        CHECK(n == 3 && g_els[2].depth == 0,
              "stray end tags with nothing open never take depth below 0");

        /* 300-deep nesting: depth must clamp at BROWSER_HTML_MAX_DEPTH and the
         * end tags must unwind back to exactly 0. */
        {
            char *p = g_scratch;
            uint32_t off = 0;
            for (int k = 0; k < 300; k++) {
                memcpy(p + off, "<div>", 5);
                off += 5;
            }
            for (int k = 0; k < 300; k++) {
                memcpy(p + off, "</div>", 6);
                off += 6;
            }
            html_parse(p, off, g_els, 1024, &n);
            CHECK(n == 600, "300 nested <div> plus 300 </div> is exactly 600 tokens");
            CHECK(g_els[299].depth == BROWSER_HTML_MAX_DEPTH,
                  "the 300th <div> reports depth clamped to BROWSER_HTML_MAX_DEPTH");
            CHECK(g_els[599].depth == 0,
                  "the last </div> unwinds back to depth 0 — the clamp stays balanced");
            uint32_t over = 0;
            for (uint32_t k = 0; k < n; k++)
                if (g_els[k].depth > BROWSER_HTML_MAX_DEPTH) over++;
            CHECK(over == 0, "no token ever reports a depth above the clamp");
        }

        /* 20000-deep nesting with a tiny element table: must not recurse, must
         * not overrun, must report truncation. */
        {
            char *p = g_scratch;
            uint32_t off = 0;
            for (int k = 0; k < 20000 && off + 6 < sizeof(g_scratch); k++) {
                memcpy(p + off, "<b>", 3);
                off += 3;
            }
            CHECK(html_parse(p, off, g_els, 64, &n) == BROWSER_ERR_TRUNCATED && n == 64,
                  "20000-deep nesting fills a 64-slot table and returns TRUNCATED");
        }

        /* 64 KiB of adversarial garbage. ASan/UBSan are the real assertion. */
        {
            static char garbage[65536];
            static const char alphabet[] = "<>/\"'&;=- !abcdefgh\n\t\r0123456789";
            uint32_t lcg = 0xC0FFEEu;
            for (uint32_t k = 0; k < sizeof(garbage); k++) {
                lcg = lcg * 1103515245u + 12345u;
                garbage[k] = alphabet[(lcg >> 16) % (sizeof(alphabet) - 1)];
            }
            int rc = html_parse(garbage, sizeof(garbage), g_els, 512, &n);
            CHECK((rc == BROWSER_OK || rc == BROWSER_ERR_TRUNCATED) && n <= 512,
                  "64 KiB of pseudo-random garbage parses without overrunning anything");
        }

        /* EVERY prefix of a valid document. */
        {
            const char *doc = "<!DOCTYPE html><html><head><title>D</title></head><body>"
                              "<h1>Hi</h1><p>x <a href=\"/y\" title='q'>z</a>&amp;<!--c--></p>"
                              "<img src=\"i.png\"><script>a<b</script></body></html>";
            uint32_t L = (uint32_t) strlen(doc);
            int bad = 0;
            for (uint32_t k = 0; k <= L; k++) {
                uint32_t cnt = 0;
                int rc = html_parse(doc, k, g_els, 512, &cnt);
                if (k == 0) {
                    if (rc != BROWSER_OK || cnt != 0) bad++;
                    continue;
                }
                if (rc != BROWSER_OK && rc != BROWSER_ERR_TRUNCATED) bad++;
                if (cnt > 512) bad++;
            }
            CHECK(bad == 0, "all 200+ prefixes of a valid document parse safely");
            printf("       (checked %u prefixes)\n", L + 1);
        }

        /* Every SUFFIX too — a document that starts mid-tag. */
        {
            const char *doc = "<div class=\"a\"><p>hi</p><!--c--></div>";
            uint32_t L = (uint32_t) strlen(doc);
            int bad = 0;
            for (uint32_t k = 0; k < L; k++) {
                uint32_t cnt = 0;
                int rc = html_parse(doc + k, L - k, g_els, 512, &cnt);
                if (rc != BROWSER_OK && rc != BROWSER_ERR_TRUNCATED) bad++;
            }
            CHECK(bad == 0, "every suffix (starting mid-tag) parses safely too");
        }

        /* An entity cut off at the EXACT end of the buffer. These use heap
         * buffers sized to the byte so ASan puts a redzone immediately after
         * the last valid byte: a tokenizer that scans "a few more characters
         * looking for the ';'" dies here rather than in production. */
        {
            struct {
                const char *doc;
                const char *want;
            } cut[] = {
                {"<p>x&amp", "x&amp"},   /* named entity, no ';'   */
                {"<p>x&#x41", "x&#x41"}, /* hex entity, no ';'     */
                {"<p>x&#65", "x&#65"},   /* decimal entity, no ';' */
                {"<p>x&", "x&"},         /* a lone ampersand       */
            };
            int bad = 0;
            for (unsigned k = 0; k < sizeof(cut) / sizeof(cut[0]); k++) {
                uint32_t L = (uint32_t) strlen(cut[k].doc);
                char *hp = (char *) malloc(L);
                memcpy(hp, cut[k].doc, L);
                uint32_t cnt = 0;
                html_parse(hp, L, g_els, 512, &cnt);
                if (cnt != 2 || strcmp(g_els[1].text, cut[k].want) != 0) bad++;
                free(hp);
            }
            CHECK(bad == 0, "an unterminated entity at the exact end of the buffer stays "
                            "literal and is never over-read");
        }

        /* Non-NUL-terminated input with embedded NULs.
         * Asserting only "rc == BROWSER_OK" here proved nothing: the tokenizer
         * used to stop the TEXT token at the NUL, drop "def", and leave
         * truncated false — an OK return over a silently shortened document.
         * HTML5 maps U+0000 in character data to U+FFFD, so the token must
         * hold the WHOLE run with the NUL replaced. */
        {
            char raw[24];
            memcpy(raw, "<p>abc\0def</p>xxxxx", 19);
            uint32_t cnt = 0;
            int rc = html_parse(raw, 14, g_els, 512, &cnt);
            CHECK(rc == BROWSER_OK && cnt == 3,
                  "a buffer with an embedded NUL and no terminator parses by length");
            CHECK(strlen(g_els[1].text) == 9 && memcmp(g_els[1].text,
                                                       "abc\xEF\xBF\xBD"
                                                       "def",
                                                       9) == 0,
                  "the NUL becomes U+FFFD (EF BF BD) and \"def\" after it survives");
            CHECK(!g_els[1].truncated, "and nothing was lost, so truncated stays false");

            /* Same rule in RAWTEXT, which does not go through the accumulator. */
            char rw[32];
            memcpy(rw, "<script>ab\0cd</script>", 22);
            html_parse(rw, 22, g_els, 512, &cnt);
            CHECK(cnt == 3 && strlen(g_els[1].text) == 7 &&
                      memcmp(g_els[1].text,
                             "ab\xEF\xBF\xBD"
                             "cd",
                             7) == 0,
                  "a NUL inside <script> RAWTEXT is U+FFFD too, and \"cd\" survives");
        }

        /* ---- every parser, on EXACT-SIZE heap buffers, under ASan ----
         * Static buffers hide over-reads: a parser that peeks one byte past
         * the length lands in the next static object and nothing complains.
         * Every input below is malloc'd to the exact byte count handed to the
         * parser, so ASan's redzone starts immediately after the last valid
         * byte and any over-read is a hard failure. */

        /* http_parse_response: every prefix of a chunked message. */
        {
            const char *full = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n"
                               "Content-Type: text/html\r\n\r\n5;a=b\r\nHello\r\n0\r\nT: 1\r\n\r\n";
            uint32_t L = (uint32_t) strlen(full);
            int bad = 0;
            for (uint32_t k = 0; k <= L; k++) {
                uint8_t *hp = (uint8_t *) malloc(k ? k : 1);
                memcpy(hp, full, k);
                http_response_t rr;
                int rc = http_parse_response(&rr, hp, k);
                /* Only the complete message may claim success. */
                if (k < L && rc == BROWSER_OK && rr.success && rr.body_len == 5) bad++;
                if (rc != BROWSER_OK && rc != BROWSER_ERR_TRUNCATED && rc != BROWSER_ERR_PROTOCOL)
                    bad++;
                free(hp);
            }
            CHECK(bad == 0, "every prefix of a chunked response, on an exact-size heap "
                            "buffer, is TRUNCATED/PROTOCOL — never a short OK");
            uint8_t *hp = (uint8_t *) malloc(L);
            memcpy(hp, full, L);
            http_response_t rr;
            CHECK(http_parse_response(&rr, hp, L) == BROWSER_OK && rr.body_len == 5 &&
                      memcmp(rr.body, "Hello", 5) == 0,
                  "...and the WHOLE message still de-chunks to \"Hello\"");
            free(hp);
        }

        /* Byte-mutation fuzz of a valid response: flip one byte at a time. */
        {
            const char *seed = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nTransfer-Encoding: chunked"
                               "\r\n\r\n5\r\nHello\r\n0\r\n\r\n";
            uint32_t L = (uint32_t) strlen(seed);
            uint32_t lcg = 0x5EEDu;
            int bad = 0;
            for (uint32_t it = 0; it < 20000; it++) {
                uint8_t *hp = (uint8_t *) malloc(L);
                memcpy(hp, seed, L);
                uint32_t muts = 1 + (it % 4);
                for (uint32_t m = 0; m < muts; m++) {
                    lcg = lcg * 1103515245u + 12345u;
                    hp[(lcg >> 16) % L] = (uint8_t) ((lcg >> 8) & 0xFF);
                }
                http_response_t rr;
                int rc = http_parse_response(&rr, hp, L);
                if (rc != BROWSER_OK && rc != BROWSER_ERR_TRUNCATED && rc != BROWSER_ERR_PROTOCOL &&
                    rc != BROWSER_ERR_TOO_LARGE)
                    bad++;
                if (rc == BROWSER_OK && rr.body && rr.body_len > L) bad++;
                free(hp);
            }
            CHECK(bad == 0, "20000 single/multi-byte mutations of a valid response only ever "
                            "return OK/TRUNCATED/PROTOCOL/TOO_LARGE, never a body past the "
                            "buffer");
        }

        /* html_parse on random-length heap slices of adversarial bytes. */
        {
            static const char alpha[] = "<>/\"'&;=- !#abcdefgh\n\t\r0123456789\\";
            uint32_t lcg = 0xBADC0DEu;
            int bad = 0;
            for (uint32_t it = 0; it < 6000; it++) {
                lcg = lcg * 1103515245u + 12345u;
                uint32_t L = 1 + ((lcg >> 16) % 400);
                char *hp = (char *) malloc(L);
                for (uint32_t k = 0; k < L; k++) {
                    lcg = lcg * 1103515245u + 12345u;
                    hp[k] = alpha[(lcg >> 16) % (sizeof(alpha) - 1)];
                }
                uint32_t cnt = 0xDEADBEEF;
                int rc = html_parse(hp, L, g_els, 64, &cnt);
                if (rc != BROWSER_OK && rc != BROWSER_ERR_TRUNCATED) bad++;
                if (cnt > 64) bad++;
                if (rc == BROWSER_ERR_TRUNCATED && cnt != 64) bad++;
                free(hp);
            }
            CHECK(bad == 0, "6000 random adversarial documents on exact-size heap buffers: "
                            "count never exceeds max, TRUNCATED always means the table filled");
        }

        /* URL parsing/resolution on random bytes, including embedded control
         * characters, into exact-size heap buffers. */
        {
            uint32_t lcg = 0xFEEDFACEu;
            int bad = 0;
            for (uint32_t it = 0; it < 8000; it++) {
                lcg = lcg * 1103515245u + 12345u;
                uint32_t L = 1 + ((lcg >> 16) % 120);
                char *u = (char *) malloc(L + 1);
                for (uint32_t k = 0; k < L; k++) {
                    lcg = lcg * 1103515245u + 12345u;
                    /* 0x21..0x7E plus the odd control char */
                    uint32_t v = (lcg >> 16) % 100;
                    u[k] = (v < 4) ? (char) (v + 1) : (char) (0x21 + (v % 0x5E));
                }
                u[L] = '\0';
                browser_url_t uu;
                int rc = browser_parse_url(u, &uu);
                if (rc != BROWSER_OK && rc != BROWSER_ERR_BAD_URL) bad++;
                if (rc == BROWSER_OK && !uu.valid) bad++;
                if (rc == BROWSER_OK && uu.host[0] && !uu.absolute && uu.scheme[0]) bad++;

                uint32_t cap = 1 + ((lcg >> 8) % 200);
                char *out = (char *) malloc(cap);
                int rr = browser_resolve_url("http://base.example/a/b?q", u, out, cap);
                if (rr != BROWSER_OK && rr != BROWSER_ERR_BAD_URL && rr != BROWSER_ERR_FULL &&
                    rr != BROWSER_ERR_ARG)
                    bad++;
                free(out);
                free(u);
            }
            CHECK(bad == 0, "8000 random URLs through parse+resolve on exact-size heap "
                            "buffers stay inside the documented return set");
        }

        /* http_build_request into every capacity from 1 upward: either the
         * exact request or BROWSER_ERR_FULL, never a partial write reported
         * as a length. */
        {
            browser_url_t bu;
            browser_parse_url("http://example.com/p?a=1", &bu);
            char full[512];
            int want = http_build_request(full, sizeof(full), HTTP_GET, &bu, "U", NULL, 0);
            int bad = 0;
            for (uint32_t cap = 1; cap <= (uint32_t) want + 4; cap++) {
                char *hp = (char *) malloc(cap);
                int rc = http_build_request(hp, cap, HTTP_GET, &bu, "U", NULL, 0);
                if (cap < (uint32_t) want + 1) {
                    if (rc != BROWSER_ERR_FULL) bad++;
                } else if (rc != want || memcmp(hp, full, (size_t) want) != 0)
                    bad++;
                free(hp);
            }
            CHECK(want > 0 && bad == 0,
                  "http_build_request is FULL below the exact size and byte-identical "
                  "at or above it — the boundary is exact");
        }
    }

    /* ================= 9. layout ================= */
    printf("\n--- 9. block layout (exact integer pixel boxes) ---\n");
    {
        static browser_tab_t tab;
        memset(&tab, 0, sizeof(tab));
        tab.fg_color = 0xFFD700;
        tab.bg_color = 0x0A0000;
        tab.accent_color = 0xFF6600;

        parse_into(&tab, "<p>hello</p>");
        CHECK(tab.num_elements == 3, "\"<p>hello</p>\" is 3 tokens");
        CHECK(html_render(&tab, 800, 600) == BROWSER_OK, "layout runs at 800x600");
        CHECK(tab.elements[0].x == 8 && tab.elements[0].y == 16 && tab.elements[0].w == 784 &&
                  tab.elements[0].h == 0,
              "<p> box is (8,16,784,0): margin 8 + block margin 8");
        CHECK(tab.elements[1].x == 8 && tab.elements[1].y == 16 && tab.elements[1].w == 40 &&
                  tab.elements[1].h == 20,
              "\"hello\" is (8,16,40,20): 5 chars x 8px, line box 16*1.25");
        CHECK(tab.elements[2].y == 44, "</p> sits at y=44 (16 + 20 + 8)");
        CHECK(tab.content_h == 52, "content height is exactly 52");
        CHECK(tab.content_w == 800, "content width equals the viewport when nothing overflows");
        CHECK(tab.elements[1].fg_color == 0xFFD700, "text takes the tab foreground colour");

        parse_into(&tab, "<h1>ZXV</h1>");
        html_render(&tab, 800, 600);
        CHECK(tab.elements[1].font_size == 32 && tab.elements[1].w == 48 && tab.elements[1].h == 40,
              "<h1> text is 32px: 3 chars x 16px wide, 40px line box");
        CHECK(tab.content_h == 72, "the h1 document is exactly 72px tall");

        /* wrapping: a 96px viewport leaves 80px, i.e. 10 characters */
        parse_into(&tab, "<p>aaaa bbbb cccc</p>");
        html_render(&tab, 96, 600);
        CHECK(tab.elements[1].h == 40, "the text wraps onto exactly 2 lines (h == 40)");
        CHECK(tab.elements[1].w == 80, "a wrapped run reports the full available width");
        CHECK(tab.content_h == 72, "the wrapped document is 72px tall");

        /* an unbreakable word overflows and makes horizontal scroll real */
        parse_into(&tab, "<p>aaaaaaaaaaaaaaaaaaaa</p>");
        html_render(&tab, 96, 600);
        CHECK(tab.content_w == 176,
              "a 20-char unbreakable word yields content_w 176 (8 + 160 + 8)");

        /* image box */
        parse_into(&tab, "<p><img src=\"a\" width=\"100\" height=\"50\"></p>");
        html_render(&tab, 800, 600);
        CHECK(tab.elements[1].w == 100 && tab.elements[1].h == 50 && tab.elements[1].x == 8 &&
                  tab.elements[1].y == 16,
              "the image reserves its declared 100x50 box at (8,16)");
        CHECK(tab.elements[2].y == 74, "the following block clears the image (y=74)");

        /* image with no dimensions falls back to the documented placeholder */
        parse_into(&tab, "<p><img src=\"a\"></p>");
        html_render(&tab, 800, 600);
        CHECK(tab.elements[1].w == BROWSER_IMG_PLACEHOLDER &&
                  tab.elements[1].h == BROWSER_IMG_PLACEHOLDER,
              "an <img> with no width/height uses the 64x64 placeholder");

        /* <br> and <hr> */
        parse_into(&tab, "<p>a<br>b</p>");
        html_render(&tab, 800, 600);
        CHECK(tab.elements[1].y == 16 && tab.elements[3].y == 36,
              "<br> moves the next run down exactly one 20px line");
        parse_into(&tab, "<hr>");
        html_render(&tab, 800, 600);
        CHECK(tab.elements[0].h == BROWSER_HR_THICKNESS && tab.elements[0].w == 784,
              "<hr> is a full-width rule of BROWSER_HR_THICKNESS px");

        CHECK(html_render(NULL, 800, 600) == BROWSER_ERR_ARG, "render(NULL) is refused");
        CHECK(html_render(&tab, 0, 600) == BROWSER_ERR_ARG, "a zero-width viewport is refused");
        CHECK(html_render(&tab, 800, 0) == BROWSER_ERR_ARG, "a zero-height viewport is refused");
    }

    /* ================= 10. tabs, history, bookmarks, scroll ================= */
    printf("\n--- 10. tabs / history / bookmarks / scroll ---\n");
    {
        browser_init(&g_b, "ZXV-Browser");
        CHECK(strcmp(g_b.name, "ZXV-Browser") == 0 && g_b.default_zoom == 100 &&
                  g_b.num_tabs == 0 && g_b.next_tab_id == 1,
              "init sets known defaults");
        CHECK(strcmp(g_b.user_agent, "ZXV-Browser/1.0") == 0, "the user agent is set");
        /* The arm32 kernel really does call this with NULL. "CHECK(true, ...)"
         * is an assertion that cannot fail, so check the observable instead:
         * the NULL call must neither fault NOR touch the browser we just set
         * up (an implementation that dereferenced a stale pointer could). */
        browser_new_tab(&g_b, "http://sentinel/");
        browser_init(NULL, "x");
        CHECK(strcmp(g_b.name, "ZXV-Browser") == 0 && g_b.num_tabs == 1 && g_b.next_tab_id == 2 &&
                  strcmp(g_b.tabs[0].url, "http://sentinel/") == 0,
              "browser_init(NULL, ...) does not fault and leaves a real browser "
              "untouched (arm32 kernel_main calls it)");
        browser_init(&g_b, "ZXV-Browser"); /* drop the sentinel tab again */

        uint32_t ids[BROWSER_MAX_TABS + 1];
        for (uint32_t k = 0; k < BROWSER_MAX_TABS; k++) ids[k] = browser_new_tab(&g_b, NULL);
        CHECK(ids[0] == 1 && ids[BROWSER_MAX_TABS - 1] == BROWSER_MAX_TABS,
              "tab ids are handed out 1..BROWSER_MAX_TABS");
        CHECK(g_b.num_tabs == BROWSER_MAX_TABS, "all 16 slots are occupied");
        CHECK(browser_new_tab(&g_b, NULL) == 0,
              "the 17th tab returns 0 — no allocator, no overflow");
        CHECK(browser_close_tab(&g_b, 5) == BROWSER_OK && g_b.num_tabs == 15,
              "closing tab 5 frees a slot");
        CHECK(browser_get_tab(&g_b, 5) == NULL, "the closed tab is gone");
        CHECK(browser_close_tab(&g_b, 5) == BROWSER_ERR_NOENT,
              "closing it twice is BROWSER_ERR_NOENT");
        CHECK(browser_close_tab(&g_b, 9999) == BROWSER_ERR_NOENT,
              "closing a nonexistent tab is BROWSER_ERR_NOENT");
        uint32_t reused = browser_new_tab(&g_b, NULL);
        CHECK(reused == BROWSER_MAX_TABS + 1,
              "the freed slot gets a FRESH id, never a recycled one");

        /* navigation with no transport */
        browser_init(&g_b, "B");
        http_bind_transport(NULL);
        uint32_t t1 = browser_new_tab(&g_b, NULL);
        int rc = browser_navigate(&g_b, t1, "http://a.example/1");
        CHECK(rc == BROWSER_ERR_NO_TRANSPORT,
              "navigate with no transport returns BROWSER_ERR_NO_TRANSPORT");
        browser_tab_t *tp = browser_get_tab(&g_b, t1);
        CHECK(tp && strcmp(tp->url, "http://a.example/1") == 0,
              "...but the URL is still committed to the tab");
        CHECK(tp && !tp->loaded && tp->num_elements == 0,
              "...and the tab is NOT flagged loaded, with an empty DOM");
        CHECK(g_b.history_count == 1 && g_b.history_index == 0,
              "...and the navigation stack has exactly one entry");

        CHECK(browser_navigate(&g_b, t1, "notaurl") == BROWSER_ERR_BAD_URL,
              "an unparseable URL is refused");
        CHECK(browser_navigate(&g_b, t1, "/relative") == BROWSER_ERR_BAD_URL,
              "a relative URL cannot be navigated to");
        CHECK(browser_navigate(&g_b, t1, "ftp://h/f") == BROWSER_ERR_BAD_URL,
              "a non-http(s) scheme is refused");
        CHECK(strcmp(tp->url, "http://a.example/1") == 0 && g_b.history_count == 1,
              "a refused navigation changes neither the tab nor the history");
        CHECK(browser_navigate(&g_b, 999, "http://h/") == BROWSER_ERR_NOENT,
              "navigating a nonexistent tab is BROWSER_ERR_NOENT");
        CHECK(browser_refresh(&g_b, t1) == BROWSER_ERR_NO_TRANSPORT,
              "refresh with no transport returns BROWSER_ERR_NO_TRANSPORT");

        /* back / forward semantics */
        browser_navigate(&g_b, t1, "http://a.example/2");
        browser_navigate(&g_b, t1, "http://a.example/3");
        CHECK(g_b.history_count == 3 && g_b.history_index == 2,
              "three navigations leave count 3, index 2");
        CHECK(browser_back(&g_b, t1) == BROWSER_OK && g_b.history_index == 1 &&
                  strcmp(tp->url, "http://a.example/2") == 0,
              "back moves to index 1 and rewrites the tab URL");
        CHECK(browser_back(&g_b, t1) == BROWSER_OK && g_b.history_index == 0,
              "back again reaches index 0");
        CHECK(browser_back(&g_b, t1) == BROWSER_ERR_NO_HISTORY,
              "back at the start is BROWSER_ERR_NO_HISTORY, not a silent no-op success");
        CHECK(browser_forward(&g_b, t1) == BROWSER_OK && g_b.history_index == 1,
              "forward returns to index 1");
        browser_navigate(&g_b, t1, "http://a.example/4");
        CHECK(g_b.history_count == 3 && g_b.history_index == 2 &&
                  strcmp(g_b.history[2].url, "http://a.example/4") == 0,
              "navigating after a back TRUNCATES the forward entries");
        CHECK(browser_forward(&g_b, t1) == BROWSER_ERR_NO_HISTORY,
              "there is nothing forward of a freshly-truncated stack");
        browser_navigate(&g_b, t1, "http://a.example/4");
        CHECK(g_b.history_count == 3,
              "re-navigating to the SAME url does not push a duplicate entry");

        /* The truncation above landed on the same count by coincidence (3 -> 3).
         * Repeat it from a DEEPER stack so a "grow-only" history_count cannot
         * survive: 4 entries, two backs, one navigation must leave exactly 3
         * and no resurrected forward entry. */
        browser_clear_history(&g_b);
        browser_navigate(&g_b, t1, "http://d.example/1");
        browser_navigate(&g_b, t1, "http://d.example/2");
        browser_navigate(&g_b, t1, "http://d.example/3");
        browser_navigate(&g_b, t1, "http://d.example/4");
        CHECK(g_b.history_count == 4 && g_b.history_index == 3, "a 4-deep stack");
        browser_back(&g_b, t1);
        browser_back(&g_b, t1);
        CHECK(g_b.history_index == 1, "two backs land on index 1");
        browser_navigate(&g_b, t1, "http://d.example/NEW");
        CHECK(g_b.history_count == 3,
              "navigating from index 1 of a 4-deep stack SHRINKS the count to 3");
        CHECK(g_b.history_index == 2 && strcmp(g_b.history[2].url, "http://d.example/NEW") == 0,
              "the new entry sits at index 2");
        CHECK(browser_forward(&g_b, t1) == BROWSER_ERR_NO_HISTORY,
              "the discarded /3 and /4 entries cannot be reached by forward");
        browser_clear_history(&g_b);
        browser_navigate(&g_b, t1, "http://a.example/1");
        browser_navigate(&g_b, t1, "http://a.example/2");
        browser_navigate(&g_b, t1, "http://a.example/3");
        {
            uint32_t hc = 0;
            browser_history_t *h = browser_get_history(&g_b, &hc);
            CHECK(h != NULL && hc == 3 && h[1].timestamp == h[0].timestamp + 1 &&
                      h[2].timestamp == h[1].timestamp + 1,
                  "history timestamps advance by exactly 1 per committed navigation");
        }
        CHECK(browser_clear_history(&g_b) == BROWSER_OK && g_b.history_count == 0 &&
                  g_b.history_index == 0,
              "clear_history empties the stack");
        CHECK(browser_back(&g_b, t1) == BROWSER_ERR_NO_HISTORY,
              "back on an empty stack is BROWSER_ERR_NO_HISTORY");

        /* history ring: overflowing BROWSER_MAX_HISTORY drops the OLDEST */
        {
            browser_init(&g_b, "B");
            browser_add_history(&g_b, "http://first/", "f");
            CHECK(g_b.history[0].timestamp == 1,
                  "a fresh browser's first history entry has ordinal 1");
            browser_init(&g_b, "B");
            char url[64];
            for (uint32_t k = 0; k < BROWSER_MAX_HISTORY + 10; k++) {
                snprintf(url, sizeof(url), "http://h/%u", k);
                browser_add_history(&g_b, url, "t");
            }
            CHECK(g_b.history_count == BROWSER_MAX_HISTORY,
                  "history saturates at BROWSER_MAX_HISTORY entries");
            snprintf(url, sizeof(url), "http://h/%u", BROWSER_MAX_HISTORY + 9);
            CHECK(strcmp(g_b.history[BROWSER_MAX_HISTORY - 1].url, url) == 0,
                  "the newest entry is kept; the oldest was dropped");
            CHECK(browser_add_history(&g_b, NULL, "t") == BROWSER_ERR_ARG,
                  "add_history(NULL url) is refused");
        }

        /* bookmarks */
        browser_init(&g_b, "B");
        CHECK(browser_add_bookmark(&g_b, "One", "http://1/") == BROWSER_OK &&
                  browser_add_bookmark(&g_b, "Two", "http://2/") == BROWSER_OK &&
                  browser_add_bookmark(&g_b, "Three", "http://3/") == BROWSER_OK,
              "three bookmarks are added");
        {
            uint32_t bc = 0;
            browser_bookmark_t *bm = browser_get_bookmarks(&g_b, &bc);
            CHECK(bc == 3 && strcmp(bm[1].url, "http://2/") == 0, "bookmark 1 is \"http://2/\"");
            CHECK(browser_remove_bookmark(&g_b, 1) == BROWSER_OK, "bookmark 1 is removed");
            browser_get_bookmarks(&g_b, &bc);
            CHECK(bc == 2 && strcmp(bm[1].url, "http://3/") == 0,
                  "the list closed up: index 1 is now \"http://3/\"");
            CHECK(strcmp(bm[1].title, "Three") == 0, "and its title moved with it");
        }
        CHECK(browser_remove_bookmark(&g_b, 99) == BROWSER_ERR_NOENT,
              "removing an out-of-range bookmark is BROWSER_ERR_NOENT");
        CHECK(browser_add_bookmark(&g_b, "x", NULL) == BROWSER_ERR_ARG,
              "a NULL bookmark url is refused");
        {
            while (g_b.num_bookmarks < BROWSER_MAX_BOOKMARKS)
                browser_add_bookmark(&g_b, "f", "http://f/");
            CHECK(browser_add_bookmark(&g_b, "over", "http://o/") == BROWSER_ERR_FULL,
                  "bookmark 129 returns BROWSER_ERR_FULL");
        }

        /* scroll clamping against measured extents */
        {
            browser_init(&g_b, "B");
            uint32_t tid = browser_new_tab(&g_b, NULL);
            browser_tab_t *t = browser_get_tab(&g_b, tid);
            parse_into(t, "<p>aaaa bbbb cccc dddd eeee ffff</p>");
            html_render(t, 96, 40);
            CHECK(t->content_h > 40, "the document is taller than the 40px viewport");
            uint32_t maxy = t->content_h - 40;
            CHECK(browser_scroll(&g_b, tid, 0, 10000) == BROWSER_OK &&
                      t->scroll_y == (int32_t) maxy,
                  "scrolling past the bottom clamps to content_h - viewport_h");
            CHECK(browser_scroll(&g_b, tid, 0, -10000) == BROWSER_OK && t->scroll_y == 0,
                  "scrolling past the top clamps to 0");
            CHECK(browser_scroll(&g_b, tid, 10000, 0) == BROWSER_OK &&
                      t->scroll_x == (int32_t) (t->content_w - t->viewport_w),
                  "horizontal scroll clamps to the measured overflow");
            CHECK(browser_scroll(&g_b, 999, 1, 1) == BROWSER_ERR_NOENT,
                  "scrolling a nonexistent tab is BROWSER_ERR_NOENT");
            /* t->loading was ALREADY false, so "stop returns OK and loading is
             * false" passed for a browser_stop() that did nothing at all. Set
             * the flag first so the assertion has something to disprove. */
            t->loading = true;
            CHECK(browser_stop(&g_b, tid) == BROWSER_OK && !t->loading,
                  "stop CLEARS a set loading flag (not just observes a clear one)");
            CHECK(browser_stop(&g_b, tid) == BROWSER_OK && !t->loading, "and is idempotent");
            CHECK(browser_stop(&g_b, 999) == BROWSER_ERR_NOENT,
                  "stopping a nonexistent tab is BROWSER_ERR_NOENT");
        }

        browser_set_homepage(&g_b, "http://home/");
        CHECK(strcmp(g_b.homepage, "http://home/") == 0, "the homepage is stored");
        browser_set_dragon_theme(&g_b);
        CHECK(g_b.theme_fg == 0xFFD700 && g_b.theme_link == 0xFF6600,
              "the dragon theme sets gold text and fire-orange links");
    }

    /* ================= 11. end-to-end load through the mock ============ */
    printf("\n--- 11. full load: fetch -> tokenize -> layout ---\n");
    {
        browser_init(&g_b, "B");
        const char *page =
            "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 73\r\n\r\n"
            "<html><head><title>Dragon Home</title></head><body><p>x</p></body></html>";
        mock_reset(page, (uint32_t) strlen(page));
        http_bind_transport(&MOCK_OPS);

        uint32_t tid = browser_new_tab(&g_b, NULL);
        int rc = browser_navigate(&g_b, tid, "http://dragon.example/index.html");
        browser_tab_t *t = browser_get_tab(&g_b, tid);
        CHECK(rc == BROWSER_OK, "the page loads end to end");
        /* html, head, title, "Dragon Home", /title, /head, body, p, "x",
         * /p, /body, /html = 12 tokens. */
        CHECK(t->loaded && t->num_elements == 12, "the tab is flagged loaded and holds 12 tokens");
        CHECK(strcmp(t->title, "Dragon Home") == 0, "the <title> was extracted as \"Dragon Home\"");
        CHECK(strcmp(g_b.history[g_b.history_index].title, "Dragon Home") == 0,
              "and copied onto the history entry");
        CHECK(t->content_h > 0 && t->viewport_w == 1024,
              "the tab was laid out at its 1024px default viewport");

        /* refresh re-fetches and re-lays out */
        mock_reset(page, (uint32_t) strlen(page));
        CHECK(browser_refresh(&g_b, tid) == BROWSER_OK && t->num_elements == 12,
              "refresh re-fetches the same document");
        CHECK(g_b.history_count == 1, "refresh does NOT push a new history entry");

        /* zoom actually scales the layout */
        g_b.default_zoom = 200;
        mock_reset("HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\n<h1>Hi</h1>", 0);
        {
            const char *p2 = "HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\n<h1>Hi</h1>";
            mock_reset(p2, (uint32_t) strlen(p2));
        }
        browser_refresh(&g_b, tid);
        CHECK(t->num_elements == 3 && t->elements[1].font_size == 64,
              "default_zoom 200 doubles the h1 font from 32 to 64");
        CHECK(t->elements[1].w == 64 && t->elements[1].h == 80,
              "and the text box becomes 2 chars x 32px wide, 80px tall");
        g_b.default_zoom = 100;

        /* images_enabled == false reserves no box */
        {
            const char *p3 = "HTTP/1.1 200 OK\r\nContent-Length: 24\r\n\r\n"
                             "<p><img src=\"a.png\"></p>";
            g_b.images_enabled = false;
            mock_reset(p3, (uint32_t) strlen(p3));
            browser_refresh(&g_b, tid);
            CHECK(t->num_elements == 3 && t->elements[1].type == HTML_IMG &&
                      t->elements[1].w == 0 && t->elements[1].h == 0,
                  "with images_enabled false the <img> reserves a 0x0 box");
            CHECK(strcmp(t->elements[1].src, "a.png") == 0, "...but the src URL is still retained");
            g_b.images_enabled = true;
        }

        /* a 404 is still a successful fetch of an unsuccessful page */
        {
            const char *p4 = "HTTP/1.1 404 Not Found\r\nContent-Length: 14\r\n\r\n"
                             "<p>gone</p>xxx";
            mock_reset(p4, (uint32_t) strlen(p4));
            browser_refresh(&g_b, tid);
            CHECK(t->response.status_code == 404,
                  "a 404 body is still parsed, with status_code 404 recorded");
        }

        /* A fetch that SUCCEEDS but yields no tokens must NOT be flagged
         * loaded. "<div" is four real bytes that tokenize to nothing (eof-in-
         * tag), so this separates "bytes arrived" from "a document exists" —
         * the exact confusion browser_verify_coverage() is built to catch. */
        {
            const char *p6 = "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\n<div";
            mock_reset(p6, (uint32_t) strlen(p6));
            CHECK(browser_refresh(&g_b, tid) == BROWSER_OK,
                  "a 200 whose body tokenizes to nothing still fetches OK");
            CHECK(t->response.content_length == 4, "four body bytes really did arrive");
            CHECK(t->num_elements == 0, "but they produce zero tokens");
            CHECK(!t->loaded, "so the tab is NOT flagged loaded — bytes are not a document");
            CHECK(browser_verify_coverage(&g_b) == true,
                  "and the coverage invariant loaded == (num_elements > 0) holds");
        }

        /* whitespace-only body: same rule */
        {
            const char *p7 = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n\n   \n";
            mock_reset(p7, (uint32_t) strlen(p7));
            browser_refresh(&g_b, tid);
            CHECK(t->num_elements == 0 && !t->loaded,
                  "a whitespace-only body is zero tokens and is not \"loaded\"");
        }

        /* back over a bound transport re-fetches */
        {
            const char *p5 = "HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\n<p>back</p>xx";
            browser_navigate(&g_b, tid, "http://dragon.example/second");
            mock_reset(p5, (uint32_t) strlen(p5));
            CHECK(browser_back(&g_b, tid) == BROWSER_OK &&
                      strcmp(t->url, "http://dragon.example/index.html") == 0,
                  "back re-fetches the previous entry when a transport is bound");
            CHECK(t->num_elements == 3 && t->loaded,
                  "and the re-fetched document is tokenized and flagged loaded");
        }
        /* A loaded tab must NOT hold a receive slot. With 2 slots and 16 tabs,
         * holding on would cap the browser at two loaded tabs — so load four
         * and check every one of them really has a DOM. */
        {
            const char *p8 = "HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\n<p>page</p>x";
            browser_init(&g_b, "B");
            uint32_t tids[4];
            char u8[64];
            int loaded = 0;
            for (uint32_t k = 0; k < 4; k++) {
                tids[k] = browser_new_tab(&g_b, NULL);
                snprintf(u8, sizeof(u8), "http://m.example/%u", k);
                mock_reset(p8, (uint32_t) strlen(p8));
                if (browser_navigate(&g_b, tids[k], u8) == BROWSER_OK) loaded++;
            }
            CHECK(loaded == 4, "four tabs load successfully — a tab does not hold a pool slot");
            int with_dom = 0;
            for (uint32_t k = 0; k < 4; k++) {
                browser_tab_t *tt = browser_get_tab(&g_b, tids[k]);
                if (tt && tt->loaded && tt->num_elements == 3) with_dom++;
            }
            CHECK(with_dom == 4, "and all four hold their own 3-token DOM");
            browser_tab_t *t0 = browser_get_tab(&g_b, tids[0]);
            CHECK(t0->response.status_code == 200 && t0->response.content_length == 11,
                  "the response METADATA survives the slot release");
            CHECK(t0->response.body == NULL && t0->response.body_len == 0,
                  "...while the borrowed body pointer is cleared, not left dangling");
            CHECK(browser_verify_coverage(&g_b) == true,
                  "and four honestly-loaded tabs pass coverage");
        }
        http_bind_transport(NULL);
    }

    /* ================= 12. coverage — and how to make it FAIL ========== */
    printf("\n--- 12. coverage verification (it must be able to fail) ---\n");
    {
        browser_init(&g_b, "B");
        CHECK(browser_verify_coverage(&g_b) == true,
              "an empty browser has nothing to cover, so it passes");
        CHECK(g_b.coverage_r == 1.0 && g_b.coverage_l == 1.0, "and reports r == 1.0, ell == 1.0");
        CHECK(browser_verify_coverage(NULL) == false, "a NULL browser fails");

        browser_new_tab(&g_b, "http://a.example/");
        CHECK(browser_verify_coverage(&g_b) == true && g_b.coverage_r == 1.0,
              "one addressable, self-consistent tab passes with r == 1.0");

        /* FAILURE 1: half the tabs hold URLs we cannot address. */
        browser_init(&g_b, "B");
        browser_new_tab(&g_b, "http://a.example/");
        browser_new_tab(&g_b, "http://b.example/");
        browser_new_tab(&g_b, NULL);
        browser_new_tab(&g_b, "gibberish");
        CHECK(browser_verify_coverage(&g_b) == false,
              "FAILS when only 2 of 4 tabs hold an addressable URL");
        CHECK(g_b.coverage_r == 0.5, "and reports r exactly 0.5");
        CHECK(g_b.coverage_l == 1.0, "with ell still 1.0 (bookkeeping is consistent)");

        /* boundary: 3 of 4 addressable is exactly the floor and passes. */
        browser_init(&g_b, "B");
        browser_new_tab(&g_b, "http://a/");
        browser_new_tab(&g_b, "http://b/");
        browser_new_tab(&g_b, "http://c/");
        browser_new_tab(&g_b, NULL);
        CHECK(browser_verify_coverage(&g_b) == true && g_b.coverage_r == 0.75,
              "3 of 4 addressable is exactly BROWSER_COVERAGE_FLOOR and passes");

        /* FAILURE 2: a tab that CLAIMS to be loaded with an empty DOM.
         * This is precisely the lie the check exists to catch. */
        browser_init(&g_b, "B");
        {
            uint32_t tid = browser_new_tab(&g_b, "http://a/");
            browser_tab_t *t = browser_get_tab(&g_b, tid);
            t->loaded = true; /* but num_elements is still 0 */
            CHECK(browser_verify_coverage(&g_b) == false,
                  "FAILS when a tab is flagged loaded but its DOM is empty");
            CHECK(g_b.coverage_l == 0.0, "and reports ell exactly 0.0");
        }

        /* FAILURE 3: the mirror image — a DOM with the flag never set. */
        browser_init(&g_b, "B");
        {
            uint32_t tid = browser_new_tab(&g_b, "http://a/");
            browser_tab_t *t = browser_get_tab(&g_b, tid);
            parse_into(t, "<p>x</p>");
            t->loaded = false; /* but num_elements is 3 */
            CHECK(t->num_elements == 3, "the tab really does hold 3 elements");
            CHECK(browser_verify_coverage(&g_b) == false,
                  "FAILS when a tab holds a DOM but was never flagged loaded");
        }

        /* and the honest state passes */
        browser_init(&g_b, "B");
        {
            uint32_t tid = browser_new_tab(&g_b, "http://a/");
            browser_tab_t *t = browser_get_tab(&g_b, tid);
            parse_into(t, "<p>x</p>");
            t->loaded = true;
            CHECK(browser_verify_coverage(&g_b) == true && g_b.coverage_r == 1.0 &&
                      g_b.coverage_l == 1.0,
                  "a truthfully-loaded tab passes with r == ell == 1.0");
        }
    }

    /* ================= 13. silent-truncation regressions ================= */
    printf("\n--- 13. things that used to succeed while losing data ---\n");
    {
        uint32_t n = 0;

        /* An over-long alt="" that still FITS the attribute digest. The digest
         * overflow flag used to be the only thing setting `truncated`, so an
         * alt cut from 200 characters to 127 was reported as intact. */
        {
            char *p = g_scratch;
            int m = snprintf(p, 64, "<img alt=\"");
            memset(p + m, 'q', 200);
            snprintf(p + m + 200, 8, "\">");
            html_parse(p, (uint32_t) strlen(p), g_els, 1024, &n);
            CHECK(n == 1 && strlen(g_els[0].attrs) < BROWSER_MAX_ATTRS_LEN - 1,
                  "the 200-char alt fits the attribute digest (so the digest "
                  "overflow flag cannot be what catches it)");
            CHECK(strlen(g_els[0].alt) == BROWSER_MAX_ALT_LEN - 1,
                  "alt holds exactly BROWSER_MAX_ALT_LEN-1 characters");
            CHECK(g_els[0].truncated, "and the element IS flagged truncated");
        }

        /* An attribute NAME longer than the 64-byte scratch it is matched in.
         * The digest then records a name the document never used. */
        {
            char *p = g_scratch;
            int m = snprintf(p, 64, "<div ");
            memset(p + m, 'z', 100);
            snprintf(p + m + 100, 8, "=v>");
            html_parse(p, (uint32_t) strlen(p), g_els, 1024, &n);
            CHECK(n == 1 && strlen(g_els[0].attrs) == 66,
                  "a 100-char attribute name is digested as 63 chars + \"=v;\"");
            CHECK(g_els[0].truncated,
                  "...and that mismatch sets truncated (it is a DIFFERENT name)");
        }

        /* Entity code points that cannot be encoded become U+FFFD, so every
         * text field really is the valid UTF-8 the header claims. A lone
         * surrogate used to come out as ED A0 80 — that is CESU-8. */
        {
            struct {
                const char *doc;
                const char *want;
                uint32_t wlen;
            } e[] = {
                {"<p>&#xD800;</p>", "\xEF\xBF\xBD", 3},
                {"<p>&#xDFFF;</p>", "\xEF\xBF\xBD", 3},
                {"<p>&#x110000;</p>", "\xEF\xBF\xBD", 3},
                {"<p>&#0;</p>", "\xEF\xBF\xBD", 3},
                {"<p>&#x10FFFF;</p>", "\xF4\x8F\xBF\xBF", 4},
                {"<p>&#65;</p>", "A", 1},
            };
            int bad = 0;
            for (unsigned k = 0; k < sizeof(e) / sizeof(e[0]); k++) {
                html_parse(e[k].doc, (uint32_t) strlen(e[k].doc), g_els, 1024, &n);
                if (n != 3 || strlen(g_els[1].text) != e[k].wlen ||
                    memcmp(g_els[1].text, e[k].want, e[k].wlen) != 0)
                    bad++;
            }
            CHECK(bad == 0, "surrogates, out-of-range and &#0; all decode to U+FFFD "
                            "(EF BF BD); U+10FFFF and &#65; are unaffected");
            /* ...while a malformed reference is still left literal. */
            const char *lit = "<p>&#; &#x; &nosuch;</p>";
            html_parse(lit, (uint32_t) strlen(lit), g_els, 1024, &n);
            CHECK(strcmp(g_els[1].text, "&#; &#x; &nosuch;") == 0,
                  "an entity with no digits or an unknown name stays literal");
        }

        /* Text that ends the buffer inside an open <a> must inherit the link
         * exactly like text that ends at a "</a>". These were two copies of
         * the flush code and only one of them did it. */
        {
            const char *eofa = "<a href=\"/x\">tail";
            html_parse(eofa, (uint32_t) strlen(eofa), g_els, 1024, &n);
            CHECK(n == 2 && g_els[1].kind == HTML_TOK_TEXT && strcmp(g_els[1].text, "tail") == 0,
                  "text after <a> with NO closing tag is still emitted");
            CHECK(g_els[1].is_link && strcmp(g_els[1].href, "/x") == 0,
                  "...and inherits is_link and href, same as the closed case");
            CHECK(g_els[1].underline, "...and the anchor underline too");
        }

        /* html_render() takes a caller-owned tab, so font_size is untrusted.
         * At 2e9 the line-box arithmetic wrapped uint32 and the function
         * reported content_w 705032720 for a two-word paragraph. */
        {
            static browser_tab_t ht;
            memset(&ht, 0, sizeof(ht));
            parse_into(&ht, "<p>hello world</p>");
            ht.elements[1].font_size = 2000000000;
            CHECK(html_render(&ht, 800, 600) == BROWSER_OK, "a hostile font size renders");
            CHECK(ht.elements[1].font_size == BROWSER_MAX_FONT_PX,
                  "font_size is clamped to BROWSER_MAX_FONT_PX and written back");
            /* 512px: char 256 wide, line box 640. "hello" = 1280px, then
             * "world" cannot fit before right=792, so 2 lines of 640 = 1280
             * tall, and the widest point is 8 + 1280 = 1288 -> +8 margin. */
            CHECK(ht.elements[1].h == 1280 && ht.elements[1].w == 784,
                  "the clamped box is exactly 784x1280");
            CHECK(ht.content_w == 1296 && ht.content_h == 1312,
                  "and the extents are exactly 1296 x 1312, not a wrapped uint32");
            int32_t big = 2147483647;
            ht.elements[1].font_size = big;
            html_render(&ht, 800, 600);
            CHECK(ht.content_w == 1296 && ht.content_h == 1312,
                  "INT32_MAX gives the identical clamped layout");
        }

        /* A history entry that does not hold the whole URL points at a
         * different page, and browser_back() would navigate there. */
        {
            browser_init(&g_b, "B");
            static char longurl[BROWSER_MAX_URL_LEN + 64];
            memset(longurl, 'u', sizeof(longurl) - 1);
            longurl[sizeof(longurl) - 1] = 0;
            memcpy(longurl, "http://h/", 9);
            CHECK(browser_add_history(&g_b, longurl, "t") == BROWSER_ERR_ARG,
                  "add_history refuses a URL at/over BROWSER_MAX_URL_LEN");
            CHECK(g_b.history_count == 0,
                  "...and stores nothing — no truncated entry, no counter bump");
            CHECK(browser_add_bookmark(&g_b, "t", longurl) == BROWSER_ERR_ARG &&
                      g_b.num_bookmarks == 0,
                  "add_bookmark agrees (it always did)");
            longurl[BROWSER_MAX_URL_LEN - 1] = 0; /* exactly the limit - 1 */
            CHECK(browser_add_history(&g_b, longurl, "t") == BROWSER_OK &&
                      strcmp(g_b.history[0].url, longurl) == 0,
                  "a URL of exactly BROWSER_MAX_URL_LEN-1 is stored whole");
        }

        /* http_get_header answering 0 for a header that IS on the wire is a
         * false negative; headers_truncated is what makes it distinguishable
         * from a genuine absence. */
        {
            http_response_t r;
            uint8_t small[128];
            const char *s = "HTTP/1.1 200 OK\r\nX-A: 1\r\nContent-Length: 2\r\n\r\nhi";
            memcpy(small, s, strlen(s));
            CHECK(http_parse_response(&r, small, (uint32_t) strlen(s)) == BROWSER_OK &&
                      !r.headers_truncated,
                  "a normal header block leaves headers_truncated false");

            uint32_t p = (uint32_t) snprintf(g_scratch, sizeof(g_scratch), "HTTP/1.1 200 OK\r\n");
            for (int k = 0; k < 100; k++)
                p += (uint32_t) snprintf(
                    g_scratch + p, sizeof(g_scratch) - p,
                    "X-Pad-%03d: 00000000000000000000000000000000000000000000\r\n", k);
            p += (uint32_t) snprintf(g_scratch + p, sizeof(g_scratch) - p,
                                     "X-Late: present\r\nContent-Length: 2\r\n\r\nhi");
            CHECK(http_parse_response(&r, (uint8_t *) g_scratch, p) == BROWSER_OK &&
                      r.body_len == 2 && memcmp(r.body, "hi", 2) == 0,
                  "a >4 KiB header block still frames the body from the wire bytes");
            {
                char v[64];
                CHECK(http_get_header(&r, "X-Late", v, sizeof(v)) == 0,
                      "http_get_header cannot see a header past the retained 4 KiB");
                CHECK(r.headers_truncated,
                      "...so headers_truncated is set: that 0 means UNKNOWN, not ABSENT");
                CHECK(http_get_header(&r, "X-Pad-000", v, sizeof(v)) == 44,
                      "a header inside the retained block is still found");
            }
            /* a NUL inside the block cuts the retained copy too */
            memcpy(small, s, strlen(s));
            small[20] = 0;
            CHECK(http_parse_response(&r, small, (uint32_t) strlen(s)) == BROWSER_OK &&
                      r.headers_truncated && r.body_len == 2,
                  "a NUL inside the header block sets headers_truncated, and the "
                  "body is STILL framed correctly from the wire bytes");
        }

        /* A failed fetch must return its pool slot. Three failures in a row
         * would otherwise exhaust a 2-slot pool and turn every later request
         * into BROWSER_ERR_FULL. */
        {
            http_response_t r;
            const char *garbage = "NOT-HTTP AT ALL\r\n\r\nxx";
            for (int k = 0; k < 3; k++) {
                mock_reset(garbage, (uint32_t) strlen(garbage));
                http_bind_transport(&MOCK_OPS);
                CHECK(http_request(&r, HTTP_GET, "http://h/", NULL, 0) == BROWSER_ERR_PROTOCOL,
                      "a malformed response is PROTOCOL");
            }
            const char *good = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
            mock_reset(good, (uint32_t) strlen(good));
            CHECK(http_request(&r, HTTP_GET, "http://h/", NULL, 0) == BROWSER_OK,
                  "and the pool is intact afterwards — the slots were released");
            http_response_free(&r);
            http_bind_transport(NULL);
        }
    }

    /* ================= 14. misc ================= */
    printf("\n--- 14. misc ---\n");
    {
        CHECK(strcmp(browser_strerror(BROWSER_ERR_NO_TRANSPORT), "no HTTP transport bound") == 0,
              "browser_strerror names the no-transport case");
        CHECK(strcmp(browser_strerror(BROWSER_OK), "ok") == 0, "strerror(OK) is \"ok\"");
        CHECK(strcmp(browser_strerror(-999), "unknown error") == 0,
              "an unknown code says so rather than lying");
        CHECK(html_tag_type("DIV") == HTML_DIV && html_tag_type("h3") == HTML_H3 &&
                  html_tag_type("blink") == HTML_UNKNOWN,
              "html_tag_type is case-insensitive and honest about unknown tags");
        CHECK(sizeof(browser_t) < 12u * 1024u * 1024u,
              "a browser_t fits the ~9 MiB budget documented in LIMITATIONS 9");
    }

    printf("\n=========================================\n");
    printf("%s: %d checks, %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", checks,
           failures);
    return failures ? 1 : 0;
}
