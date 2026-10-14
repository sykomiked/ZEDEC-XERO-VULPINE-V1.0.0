/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_zxvd_rpc.c — zxvd's JSON-RPC request handler (kernel/arch/hosted/
 * zxvd_rpc.c, zxvd_rpc_handle_line) against the real libzxv books and a stub
 * host, without a socket. The books are reopened fresh for every input, with
 * one issuer (account 0) and two holders (1, 2) in VFV, the issuer funding
 * holder 1 with 1000000.
 *
 * Byte 0 picks the mode:
 *   0  lines: the rest is split on LF, each line handed to handle_line
 *   1  amount: the rest is the raw JSON text of "amount_minor" in a
 *      ledger.post from holder 1 to holder 2 (byte 1 picks from/to)
 *   2  earnings: a stream of fz_edge64 values for zxvd_rpc_earnings_sync
 *      interleaved with swarm.earnings / ledger.balance reads
 *   3  structured: a request built from fragments (ids of every type,
 *      duplicate keys, NUL-escaped keys and method names, nesting, batches)
 *
 * Invariants checked after every request:
 *   R1  never crashes (ASan/UBSan), whatever the bytes (NUL, bad UTF-8, depth)
 *   R2  a response is one line (no raw LF), valid RFC 8259 JSON (re-parsed
 *       with the same tokenizer: well-formed UTF-8), an object with
 *       "jsonrpc":"2.0", an "id" and exactly one of "result" / "error"
 *   R3  a request with no "id" member (a notification, M4) returns false;
 *       a line that is not an object (array = batch) is -32600 or -32700
 *       with "id":null (T3)
 *   R4  M3: amount_minor accepted iff it is a JSON integer or a [-]digits
 *       string in int64; a fraction, exponent, "+", space, escape or
 *       overflow is -32602 and the books do not move
 *   R5  a successful post moves exactly amount_minor from `from` to `to`;
 *       any refused request leaves every balance unchanged
 *   R6  conservation: the equity of the three accounts sums to the same
 *       value after every request; ledger.check stays ok
 *   R7  E2: after each accepted earnings_sync(h) the owner balance is
 *       h*rate; a refused one (-1) leaves it unchanged
 *   R8  duplicate "id"/"method"/"jsonrpc"/"params" members are refused
 *       (-32600) and never executed
 *   R9  a method name with an escaped NUL ("zxvd.shutdown\u0000x") is not
 *       the method before the NUL: it is never executed */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fuzz_in.h"
#include "web4_web2.h"
#include "zxv_api.h"
#include "zxvd_rpc.h"

/* ---------------- stub host ---------------- */
static unsigned g_exec; /* host side effects, to see that refused requests do nothing */
static void h_state(zxvd_buf_t *o) { g_exec++; zxvd_buf_raw(o, "{\"tick\":7}", 10); }
static void h_ask(const char *q, zxvd_buf_t *o) { g_exec++; zxvd_buf_json_str(o, q); }
static void h_model(zxvd_buf_t *o) { g_exec++; zxvd_buf_raw(o, "{}", 2); }
static void h_notes(zxvd_buf_t *o) { g_exec++; zxvd_buf_raw(o, "{\"notes\":[]}", 12); }
static uint32_t h_notes_read(void) { g_exec++; return 3; }
static const char *h_net(const char *c) { g_exec++; return strcmp(c, "lan") ? NULL : "on"; }
static void h_update(zxvd_buf_t *o) { g_exec++; zxvd_buf_raw(o, "{\"s\":1}", 7); }
static void h_shutdown(void) { g_exec++; }
static const zxvd_host_ops_t OPS = {h_state, h_ask,      h_model,    h_notes, h_notes_read,
                                    h_net,   h_update,   h_shutdown, "fuzz"};
static const char *p_ask(const char *peer, const char *prompt, uint64_t mx, zxvd_buf_t *o)
{
    g_exec++;
    (void) peer;
    (void) prompt;
    if (mx == 7) return "too cheap";
    zxvd_buf_printf(o, "{\"paid\":\"%llu\"}", (unsigned long long) mx);
    return NULL;
}
static const char *p_serve(bool on, uint64_t price, zxvd_buf_t *o)
{
    g_exec++;
    zxvd_buf_printf(o, "{\"on\":%s,\"p\":\"%llu\"}", on ? "true" : "false",
                    (unsigned long long) price);
    return NULL;
}
static void p_status(zxvd_buf_t *o) { g_exec++; zxvd_buf_raw(o, "{}", 2); }
static const zxvd_peer_ops_t PEER = {p_ask, p_serve, p_status};

/* ---------------- response checking ---------------- */
#define RT 4096
static w4_jtok_t g_rt[RT];
static zxvd_buf_t g_out;

typedef struct {
    bool ok;    /* has result */
    int code;   /* error code, or 0 */
    int32_t res; /* token of result */
    int32_t n;
    const char *js;
} resp_t;

static resp_t check_resp(bool sent)
{
    resp_t r = {false, 0, -1, 0, NULL};
    if (!sent) return r;
    if (g_out.oom) abort();
    if (!g_out.p || !g_out.len) abort();
    if (memchr(g_out.p, '\n', g_out.len)) abort(); /* R2: one line */
    int32_t n = w4_json_parse(g_out.p, (uint32_t) g_out.len, g_rt, RT, W4_JSON_MAX_DEPTH);
    if (n <= 0 || g_rt[0].type != W4_J_OBJ) abort();
    int32_t v = w4_json_get(g_out.p, g_rt, n, 0, "jsonrpc");
    if (v < 0 || !w4_json_str_eq(g_out.p, &g_rt[v], "2.0")) abort();
    int32_t id = w4_json_get(g_out.p, g_rt, n, 0, "id");
    if (id < 0) abort();
    int32_t res = w4_json_get(g_out.p, g_rt, n, 0, "result");
    int32_t err = w4_json_get(g_out.p, g_rt, n, 0, "error");
    if ((res >= 0) == (err >= 0)) abort();
    r.n = n;
    r.js = g_out.p;
    if (res >= 0) {
        r.ok = true;
        r.res = res;
    } else {
        int32_t c = w4_json_get(g_out.p, g_rt, n, err, "code");
        int64_t code = 0;
        if (c < 0 || w4_json_i64(g_out.p, &g_rt[c], &code) != W4_OK) abort();
        r.code = (int) code;
    }
    return r;
}

static resp_t call(const char *line, size_t len, bool *sent_out)
{
    bool sent = zxvd_rpc_handle_line(line, len, &g_out);
    if (sent_out) *sent_out = sent;
    return check_resp(sent);
}

static resp_t calls(const char *s)
{
    char *b = fz_dupz((const uint8_t *) s, strlen(s));
    bool sent;
    resp_t r = call(b, strlen(s), &sent);
    free(b);
    if (!sent) abort();
    return r;
}

/* the decimal-string member `key` of the result object */
static int64_t res_i64(resp_t r, const char *key)
{
    if (!r.ok) abort();
    int32_t i = w4_json_get(r.js, g_rt, r.n, r.res, key);
    if (i < 0) abort();
    char tmp[32];
    int32_t l = w4_json_str(r.js, &g_rt[i], tmp, sizeof tmp);
    if (l <= 0) abort();
    return strtoll(tmp, NULL, 10);
}

static int64_t equity(uint32_t acct)
{
    char req[160];
    snprintf(req, sizeof req,
             "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ledger.balance\",\"params\":{\"account\":%u}}",
             (unsigned) acct);
    return res_i64(calls(req), "equity_minor");
}

static int64_t g_eq[3];
static void snap(int64_t *e)
{
    for (uint32_t a = 0; a < 3; a++) e[a] = equity(a);
}
static void expect_books(const int64_t *want)
{
    int64_t now[3];
    snap(now);
    if (memcmp(now, want, sizeof now)) abort();
    resp_t r = calls("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ledger.check\"}");
    if (!r.ok) abort();
}
static int64_t sum3(const int64_t *e) { return e[0] + e[1] + e[2]; }

static void books_fresh(void)
{
    zxvd_rpc_books_close();
    if (zxvd_rpc_books_open() != 0) abort();
    if (!calls("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ledger.open_account\",\"params\":{"
               "\"name\":\"iss\",\"currency\":\"VFV\",\"kind\":\"issuer\"}}")
             .ok ||
        !calls("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ledger.open_account\",\"params\":{"
               "\"name\":\"h1\",\"currency\":\"VFV\"}}")
             .ok ||
        !calls("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ledger.open_account\",\"params\":{"
               "\"name\":\"h2\",\"currency\":\"VFV\"}}")
             .ok ||
        !calls("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ledger.post\",\"params\":{\"from\":0,"
               "\"to\":1,\"amount_minor\":1000000,\"currency\":\"VFV\",\"reference\":\"seed\"}}")
             .ok)
        abort();
    snap(g_eq);
}

/* R4 reference: is `s` (a JSON value's raw text) an acceptable amount? */
static bool ref_amount(const char *s, size_t n, int64_t *v)
{
    bool str = n >= 2 && s[0] == '"' && s[n - 1] == '"';
    if (str) {
        s++;
        n -= 2;
    }
    size_t i = 0;
    bool neg = false;
    if (i < n && s[i] == '-') {
        neg = true;
        i++;
    }
    if (i >= n) return false;
    if (str && n - i > 19) return false;
    /* a JSON number must not have a leading zero (unless it is "0"/"-0") */
    if (!str && s[i] == '0' && n - i > 1) return false;
    unsigned __int128 x = 0;
    for (; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        x = x * 10u + (unsigned) (s[i] - '0');
        if (x > ((unsigned __int128) 1 << 64)) return false;
    }
    if (!neg && x > (unsigned __int128) INT64_MAX) return false;
    if (neg && x > (unsigned __int128) INT64_MAX + (str ? 0u : 1u)) return false;
    *v = neg ? (int64_t) (0 - (uint64_t) x) : (int64_t) x;
    return true;
}

static unsigned g_ref; /* fresh reference per post */

static void mode_amount(fz_in *in)
{
    uint8_t sel = fz_u8(in);
    uint32_t from = sel & 1u ? 2u : 1u, to = from == 1u ? 2u : 1u;
    if (sel & 2u) from = 0;
    char amt[300];
    size_t an = fz_bytes(in, amt, sizeof amt - 1);
    amt[an] = 0;
    if (sel & 4u) { /* map onto the characters numbers are made of */
        static const char AL[] = "0123456789999990-+.eE\" \\u,9";
        for (size_t k = 0; k < an; k++) amt[k] = AL[(uint8_t) amt[k] % (sizeof AL - 1)];
        if (an > 40) an = 1 + (uint8_t) amt[0] % 40u;
        amt[an] = 0;
    }
    if (memchr(amt, 0, an)) an = strlen(amt);
    /* trim JSON whitespace: the tokenizer skips it around a value */
    size_t a0 = 0;
    while (a0 < an && strchr(" \t\r\n", amt[a0])) a0++;
    while (an > a0 && strchr(" \t\r\n", amt[an - 1])) an--;
    memmove(amt, amt + a0, an - a0);
    an -= a0;
    amt[an] = 0;
    /* R4 only applies when the text is exactly one JSON value */
    static w4_jtok_t ta[64];
    int32_t na = w4_json_parse(amt, (uint32_t) an, ta, 64, 16);
    bool single = na > 0;
    char req[600];
    int rn = snprintf(req, sizeof req,
                      "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"ledger.post\",\"params\":{"
                      "\"from\":%u,\"to\":%u,\"amount_minor\":%.*s,\"currency\":\"VFV\","
                      "\"reference\":\"f%u\"}}",
                      (unsigned) from, (unsigned) to, (int) an, amt, ++g_ref);
    if (rn < 0 || (size_t) rn >= sizeof req) return;
    /* does the line even tokenize? (a syntax error is -32700, also fine) */
    static w4_jtok_t tk[64];
    int32_t ntk = w4_json_parse(req, (uint32_t) rn, tk, 64, 16);
    int64_t want = 0;
    bool acceptable = ref_amount(amt, an, &want);
    bool sent;
    char *line = fz_dupz((const uint8_t *) req, (size_t) rn);
    resp_t r = call(line, (size_t) rn, &sent);
    free(line);
    if (!sent) abort();
    int64_t before[3];
    memcpy(before, g_eq, sizeof before);
    if (ntk <= 0) {
        if (r.ok || (r.code != -32700 && r.code != -32600)) abort();
        expect_books(before);
        return;
    }
    if (!single) { /* injected members: only conservation applies */
        int64_t now[3];
        snap(now);
        if (sum3(now) != 0) abort();
        memcpy(g_eq, now, sizeof g_eq);
        return;
    }
    if (!acceptable) {
        if (r.code != -32602) abort(); /* R4 */
        expect_books(before);
        return;
    }
    if (r.code == -32602) abort(); /* an acceptable amount must get through */
    if (r.ok) {
        int64_t after[3];
        memcpy(after, before, sizeof after);
        after[from] -= want;
        after[to] += want;
        expect_books(after); /* R5 */
        memcpy(g_eq, after, sizeof g_eq);
    } else {
        if (r.code != -32000) abort();
        expect_books(before);
    }
    if (sum3(g_eq) != 0) abort(); /* R6 */
}

static void mode_lines(const uint8_t *p, size_t n)
{
    size_t s = 0;
    for (size_t i = 0; i <= n; i++) {
        if (i < n && p[i] != '\n') continue;
        size_t L = i - s;
        char *line = (char *) fz_dup(p + s, L); /* exact size, no NUL */
        unsigned ex0 = g_exec;
        bool sent;
        resp_t r = call(line, L, &sent);
        /* a parse failure or refused request runs nothing on the host */
        if (!sent || r.code != 0) {
            if (r.code == -32700 || r.code == -32600 || r.code == -32601)
                if (g_exec != ex0) abort();
        }
        /* R3: anything that tokenizes to a non-object is refused with id null */
        static w4_jtok_t tk[600];
        int32_t ntk = L <= ZXVD_RPC_MAX_LINE ? w4_json_parse(line, (uint32_t) L, tk, 600, 16) : -1;
        if (ntk > 0 && ntk <= 512 && tk[0].type != W4_J_OBJ) {
            if (!sent || r.code != -32600) abort();
        }
        if (ntk > 0 && ntk <= 512 && tk[0].type == W4_J_OBJ) {
            int32_t id = w4_json_get(line, tk, ntk, 0, "id");
            if (id == W4_ERR_NOTFOUND && sent) abort(); /* M4 */
            if (id == W4_ERR_PARSE && (!sent || r.code != -32600)) abort(); /* R8 */
            static const char *const dupk[] = {"method", "jsonrpc", "params"};
            for (int k = 0; k < 3; k++)
                if (w4_json_get(line, tk, ntk, 0, dupk[k]) == W4_ERR_PARSE && sent &&
                    r.code != -32600)
                    abort(); /* R8 */
        }
        free(line);
        int64_t now[3];
        snap(now);
        if (sum3(now) != sum3(g_eq)) abort(); /* R6 */
        memcpy(g_eq, now, sizeof g_eq);
        s = i + 1;
    }
    if (!calls("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ledger.check\"}").ok) abort();
}

static void mode_earn(fz_in *in)
{
    int64_t held = 0;
    for (int k = 0; k < 24 && in->n; k++) {
        uint64_t h = fz_edge64(in);
        if (fz_u8(in) & 1u) h &= 0xFFFFFFFFu;
        int rc = zxvd_rpc_earnings_sync(h);
        resp_t r = calls("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"swarm.earnings\"}");
        int64_t bal = res_i64(r, "balance_minor");
        if (rc == 0) {
            if ((uint64_t) bal != h) abort(); /* rate 1 */
            held = bal;
        } else {
            if (rc != -1 || bal != held) abort(); /* R7 */
        }
        if (h > (uint64_t) ZXV_AMOUNT_MAX && rc == 0) abort();
    }
    if (!calls("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ledger.check\"}").ok) abort();
}

/* ---- mode 3: requests built from fragments ---- */
static const char *const FR_ID[] = {
    "\"id\":1,", "\"id\":\"x\",", "\"id\":null,", "\"id\":-0,", "\"id\":1e999,", "\"id\":[1],",
    "\"id\":{},", "\"id\":true,", "\"id\":1,\"id\":2,", "\"i\\u0064\":3,", "\"id\\u0000\":4,",
    "\"id\":\"\\u00e9\\ud83d\\ude00\",", "",
};
static const char *const FR_METHOD[] = {
    "\"method\":\"ledger.post\"",       "\"method\":\"ledger.post\\u0000x\"",
    "\"method\":\"zxvd.shutdown\"",     "\"method\":\"zxvd.shutdown\\u0000\"",
    "\"method\":\"swarm.ask\"",         "\"method\":\"ledger.balance\"",
    "\"method\":\"peer.ask\"",          "\"method\":\"peer.serve\"",
    "\"method\":\"rpc.discover\"",      "\"method\":1",
    "\"method\":\"ledger.post\",\"method\":\"swarm.state\"", "\"m\\u0065thod\":\"swarm.state\"",
    "\"method\":\"net.command\"",       "\"method\":\"swarm.earnings\"",
};
static const char *const FR_P[] = {
    "\"from\":1", "\"to\":2", "\"from\":0", "\"to\":0", "\"amount_minor\":5",
    "\"amount_minor\":\"7\"", "\"amount_minor\":-1", "\"amount_minor\":9223372036854775807",
    "\"amount_minor\":\"-9223372036854775808\"", "\"amount_minor\":1.0", "\"amount_minor\":1E2",
    "\"amount_minor\":\"0x10\"", "\"amount_minor\":\" 5\"", "\"amount_minor\":\"+5\"",
    "\"amount_minor\":\"\\u0035\"", "\"amount_minor\\u0000\":6", "\"currency\":\"VFV\"",
    "\"currency\":\"VFV\\u0000\"", "\"reference\":\"r\"", "\"account\":1", "\"account\":4294967296",
    "\"prompt\":\"\\ud83d\\ude00\\u0001\"", "\"peer\":\"p\"", "\"max_price_minor\":7",
    "\"max_price_minor\":\"-1\"", "\"price_minor\":18446744073709551615", "\"command\":\"lan\"",
    "\"x\":[[[[[[[[[[[[1]]]]]]]]]]]]", "\"amount_minor\":{\"a\":1}",
};

static void mode_struct(fz_in *in)
{
    char req[4096];
    size_t n = 0;
#define PUT(s)                                                                                     \
    do {                                                                                           \
        const char *s_ = (s);                                                                      \
        size_t l_ = strlen(s_);                                                                    \
        if (n + l_ >= sizeof req) goto done;                                                       \
        memcpy(req + n, s_, l_);                                                                   \
        n += l_;                                                                                   \
    } while (0)
    uint8_t f = fz_u8(in);
    unsigned mi = 0; /* read below, in input order; 0 if the line ran out first */
    if (f & 0x80) PUT("[");
    PUT("{");
    PUT(FR_ID[fz_u8(in) % (sizeof FR_ID / sizeof FR_ID[0])]);
    PUT((f & 1) ? "\"jsonrpc\":\"2.0\"," : "\"jsonrpc\":\"2.0\",\"jsonrpc\":\"2.0\",");
    mi = fz_u8(in) % (sizeof FR_METHOD / sizeof FR_METHOD[0]);
    PUT(FR_METHOD[mi]);
    if (!(f & 2)) {
        PUT(",\"params\":{");
        unsigned k = fz_u8(in) % 8u;
        for (unsigned j = 0; j < k; j++) {
            if (j) PUT(",");
            PUT(FR_P[fz_u8(in) % (sizeof FR_P / sizeof FR_P[0])]);
        }
        PUT("}");
        if (f & 4) PUT(",\"params\":{}");
    }
    PUT("}");
    if (f & 0x80) PUT("]");
done:;
    char *line = fz_dupz((const uint8_t *) req, n);
    unsigned ex0 = g_exec;
    bool sent;
    resp_t r = call(line, n, &sent);
    /* R8 duplicate members: never executed */
    if (!(f & 1) || ((f & 4) && !(f & 2)) || (f & 0x80))
        if (g_exec != ex0) abort();
    if ((f & 0x80) && (!sent || r.code != -32600)) abort(); /* T3 batch */
    /* R9: "name\u0000junk" is not the method "name" (it must not run) */
    if (strstr(FR_METHOD[mi], "\\u0000") && (g_exec != ex0 || (sent && r.ok))) abort();
    free(line);
    int64_t now[3];
    snap(now);
    if (sum3(now) != sum3(g_eq)) abort();
    memcpy(g_eq, now, sizeof g_eq);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static bool init;
    if (!init) {
        init = true;
        zxvd_rpc_set_ops(&OPS);
        zxvd_rpc_set_peer_ops(&PEER);
    }
    if (size < 1 || size > 2u * ZXVD_RPC_MAX_LINE) return 0;
    books_fresh();
    fz_in in;
    fz_init(&in, data + 1, size - 1);
    switch (data[0] & 3u) {
    case 0: mode_lines(data + 1, size - 1); break;
    case 1: mode_amount(&in); break;
    case 2: mode_earn(&in); break;
    default:
        for (int k = 0; k < 8 && in.n; k++) mode_struct(&in);
        break;
    }
    return 0;
}
