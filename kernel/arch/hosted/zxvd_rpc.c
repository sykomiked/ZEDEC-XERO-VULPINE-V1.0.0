/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_rpc.c — zxvd's JSON-RPC 2.0 control socket (see zxvd_rpc.h). */
#if !defined(_WIN32)
#    define _DEFAULT_SOURCE
#    define _DARWIN_C_SOURCE
#    if defined(__linux__)
#        define _GNU_SOURCE /* struct ucred */
#    endif
#    include <sys/types.h>
#    include <sys/socket.h>
#    include <sys/select.h>
#    include <sys/stat.h>
#    include <sys/un.h>
#    include <unistd.h>
#    include <fcntl.h>
#    include <errno.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxvd_rpc.h"
#include "web4_web2.h" /* the bounded RFC 8259 tokenizer */
#include "zxv_api.h"   /* libzxv: the frozen C ABI (never zxv_model_*: see below) */

/* zxv_host.c has its own zxv_model_open/zxv_model_info (zxv_model_host.h)
 * with other signatures; the executable's definitions win over libzxv's
 * exported ones, so this file must never call libzxv's zxv_model_* forms. */

#define MAX_TOK  512
#define PROTOCOL "zxvd-rpc/1"

/* JSON-RPC 2.0 error codes */
#define E_PARSE    (-32700)
#define E_REQUEST  (-32600)
#define E_METHOD   (-32601)
#define E_PARAMS   (-32602)
#define E_INTERNAL (-32603)
#define E_LIBZXV   (-32000) /* a libzxv status: data = {status, name, class} */

/* ===================== output buffer ===================== */

static void grow(zxvd_buf_t *b, size_t need)
{
    if (b->oom || b->len + need + 1 <= b->cap) return;
    size_t nc = (b->cap ? b->cap * 2 : 256) + need + 1;
    char *np = realloc(b->p, nc);
    if (!np) {
        b->oom = true;
        return;
    }
    b->p = np;
    b->cap = nc;
}

void zxvd_buf_raw(zxvd_buf_t *b, const char *s, size_t n)
{
    grow(b, n);
    if (b->oom) return;
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
}

void zxvd_buf_printf(zxvd_buf_t *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    grow(b, (size_t) n);
    if (b->oom) return;
    va_start(ap, fmt);
    vsnprintf(b->p + b->len, (size_t) n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t) n;
}

void zxvd_buf_json_str(zxvd_buf_t *b, const char *s)
{
    zxvd_buf_raw(b, "\"", 1);
    for (; *s; s++) {
        unsigned char c = (unsigned char) *s;
        if (c == '"' || c == '\\') {
            char e[2] = {'\\', (char) c};
            zxvd_buf_raw(b, e, 2);
        } else if (c < 0x20) {
            zxvd_buf_printf(b, "\\u%04x", c);
        } else {
            zxvd_buf_raw(b, s, 1);
        }
    }
    zxvd_buf_raw(b, "\"", 1);
}

/* ===================== state ===================== */

static const zxvd_host_ops_t *OPS;
static zxv_ctx *CTX;
static zxv_cap *CAP; /* LEDGER_READ | LEDGER_POST, never more (zxvd_rpc.h M2) */

void zxvd_rpc_set_ops(const zxvd_host_ops_t *ops)
{
    OPS = ops;
}

static int earn_open(void);
static void books_close(void);

int zxvd_rpc_books_open(void)
{
    uint32_t ma = 0, mi = 0, pa = 0;
    if (CTX) return 0;
    /* the methods here need ABI 1.3 (zxvd_rpc.h M2): refuse an older library */
    if (zxv_version(&ma, &mi, &pa) != ZXV_OK || ma != 1 || mi < 3) return -1;
    if (zxv_ctx_create(&CTX) != ZXV_OK) return -1;
    if (zxv_cap_create(CTX, 1, NULL, ZXV_CAP_LEDGER_READ | ZXV_CAP_LEDGER_POST, &CAP) != ZXV_OK) {
        zxv_ctx_destroy(CTX);
        CTX = NULL;
        return -1;
    }
    if (earn_open() != 0) {
        books_close();
        return -1;
    }
    return 0;
}

static void books_close(void)
{
    if (CAP) zxv_cap_destroy(CAP);
    if (CTX) zxv_ctx_destroy(CTX);
    CAP = NULL;
    CTX = NULL;
}

void zxvd_rpc_books_close(void)
{
    books_close();
}

/* ===================== swarm earnings (zxvd_rpc.h E1-E4) ===================== */

static char EARN_CCY[9] = "VFV"; /* E1 */
static uint64_t EARN_RATE = 1;   /* minor units per SWC */
static uint32_t EARN_SRC, EARN_OWNER;
static bool EARN_ACCTS;   /* both accounts are open */
static int64_t EARN_HELD; /* the owner's balance, minor units */
static uint64_t EARN_POSTS, EARN_SYNCS;

int zxvd_rpc_earnings_set(const char *ccy, uint64_t rate)
{
    size_t n = strlen(ccy);
    if (CTX || n < 3 || n >= sizeof EARN_CCY || rate == 0 || rate > (uint64_t) ZXV_AMOUNT_MAX)
        return -1;
    memcpy(EARN_CCY, ccy, n + 1);
    EARN_RATE = rate;
    return 0;
}

static int earn_open(void)
{
    /* E1: an ISO 4217 code is enabled from the table; anything else (VFV)
     * is a private unit with 2 minor units */
    int32_t st = zxv_ccy_enable_cap(CAP, EARN_CCY);
    if (st == ZXV_E_CURRENCY) st = zxv_ccy_register_private_cap(CAP, EARN_CCY, 2);
    EARN_ACCTS = false;
    EARN_HELD = 0;
    EARN_POSTS = 0;
    EARN_SYNCS = 0;
    return st == ZXV_OK ? 0 : -1;
}

int zxvd_rpc_earnings_sync(uint64_t held_swc)
{
    if (!CAP) return 0; /* no books yet, nothing to mirror */
    if (held_swc > (uint64_t) ZXV_AMOUNT_MAX / EARN_RATE) return -1;
    /* E1: the accounts open with the first settled cycle */
    if (!EARN_ACCTS) {
        if (zxv_ledger_open_account_cap(CAP, "swarm market", EARN_CCY, ZXV_ACCT_ISSUER,
                                        &EARN_SRC) != ZXV_OK ||
            zxv_ledger_open_account_cap(CAP, "owner: swarm earnings", EARN_CCY, ZXV_ACCT_HOLDER,
                                        &EARN_OWNER) != ZXV_OK)
            return -1;
        EARN_ACCTS = true;
    }
    int64_t d = (int64_t) (held_swc * EARN_RATE) - EARN_HELD;
    if (d != 0) {
        char ref[36];
        uint64_t entry;
        snprintf(ref, sizeof ref, "swarm earnings %llu", (unsigned long long) EARN_POSTS + 1u);
        int32_t st =
            d > 0 ? zxv_ledger_post_cap(CAP, EARN_SRC, EARN_OWNER, d, EARN_CCY, ref, &entry)
                  : zxv_ledger_post_cap(CAP, EARN_OWNER, EARN_SRC, -d, EARN_CCY, ref, &entry);
        if (st != ZXV_OK) return -1;
        EARN_HELD += d;
        EARN_POSTS++;
    }
    EARN_SYNCS++;
    return 0;
}

/* ===================== one request ===================== */

typedef struct {
    const char *js;
    const w4_jtok_t *t;
    int32_t n;
    int32_t params;  /* token index of the params object, or -1 */
    zxvd_buf_t *res; /* the "result" value */
    int err;         /* 0, or a JSON-RPC error code */
    char msg[160];
    int32_t zst; /* libzxv status for E_LIBZXV */
} call_t;

static void fail(call_t *c, int code, const char *fmt, ...)
{
    if (c->err) return;
    c->err = code;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->msg, sizeof c->msg, fmt, ap);
    va_end(ap);
}

/* A libzxv status: true if OK, else the call fails with its name and the
 * library's own last-error detail. */
static bool zok(call_t *c, int32_t st)
{
    if (st == ZXV_OK) return true;
    char detail[120] = "";
    size_t dl = 0;
    zxv_last_error(detail, sizeof detail, &dl);
    c->zst = st;
    fail(c, E_LIBZXV, "%s%s%s", zxv_status_name(st), detail[0] ? ": " : "", detail);
    return false;
}

static int32_t param(call_t *c, const char *key)
{
    if (c->params < 0) return W4_ERR_NOTFOUND;
    return w4_json_get(c->js, c->t, c->n, c->params, key);
}

static bool p_str(call_t *c, const char *key, char *out, uint32_t cap, bool required)
{
    int32_t i = param(c, key);
    if (i < 0) {
        if (required) fail(c, E_PARAMS, "missing string parameter \"%s\"", key);
        out[0] = 0;
        return !required;
    }
    int32_t got = c->t[i].type == W4_J_STR ? w4_json_str(c->js, &c->t[i], out, cap) : -1;
    if (got < 0) {
        fail(c, E_PARAMS, "\"%s\" must be a string of at most %u bytes", key, cap - 1);
        return false;
    }
    if ((size_t) got != strlen(out)) { /* an escaped NUL would cut it short silently */
        fail(c, E_PARAMS, "\"%s\" must not contain NUL", key);
        return false;
    }
    return true;
}

static bool p_i64(call_t *c, const char *key, int64_t *v)
{
    int32_t i = param(c, key);
    if (i < 0) {
        fail(c, E_PARAMS, "missing integer parameter \"%s\"", key);
        return false;
    }
    if (c->t[i].type != W4_J_NUM || w4_json_i64(c->js, &c->t[i], v) != W4_OK) {
        fail(c, E_PARAMS, "\"%s\" must be an integer (no fraction or exponent)", key);
        return false;
    }
    return true;
}

static bool p_u32(call_t *c, const char *key, uint32_t *v)
{
    int64_t x;
    if (!p_i64(c, key, &x)) return false;
    if (x < 0 || x > (int64_t) UINT32_MAX) {
        fail(c, E_PARAMS, "\"%s\" is out of range", key);
        return false;
    }
    *v = (uint32_t) x;
    return true;
}

/* M3: an amount is a JSON integer or a decimal string ("-12", no sign "+",
 * no spaces, no fraction): the SDKs send 64-bit values as strings. */
static bool p_amount(call_t *c, const char *key, int64_t *v)
{
    int32_t i = param(c, key);
    if (i < 0) {
        fail(c, E_PARAMS, "missing integer parameter \"%s\"", key);
        return false;
    }
    if (c->t[i].type == W4_J_NUM && w4_json_i64(c->js, &c->t[i], v) == W4_OK) return true;
    if (c->t[i].type == W4_J_STR) {
        const char *p = c->js + c->t[i].start, *e = c->js + c->t[i].end;
        bool neg = p < e && *p == '-';
        if (neg) p++;
        uint64_t x = 0;
        bool ok = p < e && e - p <= 19;
        for (; ok && p < e; p++) {
            if (*p < '0' || *p > '9')
                ok = false;
            else
                x = x * 10u + (uint64_t) (*p - '0');
        }
        if (ok && x <= (uint64_t) INT64_MAX) {
            *v = neg ? -(int64_t) x : (int64_t) x;
            return true;
        }
    }
    fail(c, E_PARAMS, "\"%s\" must be an integer or a decimal string (no fraction or exponent)",
         key);
    return false;
}

/* ----- host methods (M1) ----- */

static void m_state(call_t *c)
{
    if (!OPS || !OPS->state) return fail(c, E_METHOD, "not available");
    OPS->state(c->res);
}

static void m_ask(call_t *c)
{
    char prompt[4096];
    if (!OPS || !OPS->ask) return fail(c, E_METHOD, "not available");
    if (!p_str(c, "prompt", prompt, sizeof prompt, true)) return;
    zxvd_buf_t a = {0};
    OPS->ask(prompt, &a);
    zxvd_buf_raw(c->res, "{\"answer\":", 10);
    if (a.p)
        zxvd_buf_raw(c->res, a.p, a.len);
    else
        zxvd_buf_raw(c->res, "\"\"", 2);
    zxvd_buf_raw(c->res, "}", 1);
    free(a.p);
}

static void m_model(call_t *c)
{
    if (!OPS || !OPS->model) return fail(c, E_METHOD, "not available");
    OPS->model(c->res);
}

static void m_notes(call_t *c)
{
    if (!OPS || !OPS->notes) return fail(c, E_METHOD, "not available");
    OPS->notes(c->res);
}

static void m_notes_read(call_t *c)
{
    if (!OPS || !OPS->notes_read) return fail(c, E_METHOD, "not available");
    zxvd_buf_printf(c->res, "{\"marked\":%u}", (unsigned) OPS->notes_read());
}

static void m_net(call_t *c)
{
    char cmd[256];
    if (!OPS || !OPS->net) return fail(c, E_METHOD, "not available");
    if (!p_str(c, "command", cmd, sizeof cmd, true)) return;
    const char *r = OPS->net(cmd);
    if (!r) return fail(c, E_PARAMS, "command must be off, lan, online, peer, share, want or sell");
    zxvd_buf_raw(c->res, "{\"result\":", 10);
    zxvd_buf_json_str(c->res, r);
    zxvd_buf_raw(c->res, "}", 1);
}

/* ----- peer.* (zxv_peer_host.h) ----- */

static const zxvd_peer_ops_t *PEER;

void zxvd_rpc_set_peer_ops(const zxvd_peer_ops_t *ops)
{
    PEER = ops;
}

static bool p_price(call_t *c, const char *key, uint64_t *v)
{
    int64_t x;
    if (!p_amount(c, key, &x)) return false;
    if (x < 0) {
        fail(c, E_PARAMS, "\"%s\" must not be negative", key);
        return false;
    }
    *v = (uint64_t) x;
    return true;
}

static void m_peer_ask(call_t *c)
{
    char peer[64], prompt[4097];
    uint64_t max_price;
    if (!PEER || !PEER->ask) return fail(c, E_METHOD, "not available");
    if (!p_str(c, "peer", peer, sizeof peer, true) ||
        !p_str(c, "prompt", prompt, sizeof prompt, true) ||
        !p_price(c, "max_price_minor", &max_price))
        return;
    const char *why = PEER->ask(peer, prompt, max_price, c->res);
    if (why) {
        c->res->len = 0;
        fail(c, ZXVD_E_PEER, "%s", why);
    }
}

static void m_peer_serve(call_t *c)
{
    uint64_t price = 0;
    if (!PEER || !PEER->serve) return fail(c, E_METHOD, "not available");
    bool on = param(c, "price_minor") >= 0;
    if (on && !p_price(c, "price_minor", &price)) return;
    const char *why = PEER->serve(on, price, c->res);
    if (why) {
        c->res->len = 0;
        fail(c, ZXVD_E_PEER, "%s", why);
    }
}

static void m_peer_status(call_t *c)
{
    if (!PEER || !PEER->status) return fail(c, E_METHOD, "not available");
    PEER->status(c->res);
}

static void m_update(call_t *c)
{
    if (!OPS || !OPS->update_check) return fail(c, E_METHOD, "not available");
    OPS->update_check(c->res);
}

static void m_shutdown(call_t *c)
{
    if (!OPS || !OPS->shutdown) return fail(c, E_METHOD, "not available");
    OPS->shutdown();
    zxvd_buf_raw(c->res, "{\"stopping\":true}", 17);
}

static void m_version(call_t *c)
{
    uint32_t ma = 0, mi = 0, pa = 0, feat = 0;
    zxv_version(&ma, &mi, &pa);
    zxv_features(&feat);
    zxvd_buf_raw(c->res, "{\"app\":", 7);
    zxvd_buf_json_str(c->res, OPS && OPS->app_version ? OPS->app_version : "unknown");
    zxvd_buf_printf(
        c->res, ",\"protocol\":\"" PROTOCOL "\",\"libzxv\":\"%u.%u.%u\",\"libzxv_features\":%u}",
        (unsigned) ma, (unsigned) mi, (unsigned) pa, (unsigned) feat);
}

/* ----- book methods (M2), all through CAP ----- */

static bool books(call_t *c)
{
    if (CAP) return true;
    fail(c, E_INTERNAL, "the books are not open");
    return false;
}

static void m_ccy_enable(call_t *c)
{
    char code[16];
    if (!books(c) || !p_str(c, "code", code, sizeof code, true)) return;
    if (!zok(c, zxv_ccy_enable_cap(CAP, code))) return;
    zxvd_buf_raw(c->res, "{\"enabled\":", 11);
    zxvd_buf_json_str(c->res, code);
    zxvd_buf_raw(c->res, "}", 1);
}

static void m_open_account(call_t *c)
{
    char name[64], ccy[16], kind[16];
    uint32_t id;
    if (!books(c) || !p_str(c, "name", name, sizeof name, true) ||
        !p_str(c, "currency", ccy, sizeof ccy, true) || !p_str(c, "kind", kind, sizeof kind, false))
        return;
    int32_t k;
    if (!kind[0] || !strcmp(kind, "holder"))
        k = ZXV_ACCT_HOLDER;
    else if (!strcmp(kind, "issuer"))
        k = ZXV_ACCT_ISSUER;
    else
        return fail(c, E_PARAMS, "kind must be \"holder\" or \"issuer\"");
    if (!zok(c, zxv_ledger_open_account_cap(CAP, name, ccy, k, &id))) return;
    zxvd_buf_printf(c->res, "{\"account\":%u}", (unsigned) id);
}

static void m_post(call_t *c)
{
    uint32_t from, to;
    int64_t amount;
    char ccy[16], ref[40];
    uint64_t entry;
    if (!books(c) || !p_u32(c, "from", &from) || !p_u32(c, "to", &to) ||
        !p_amount(c, "amount_minor", &amount) || !p_str(c, "currency", ccy, sizeof ccy, true) ||
        !p_str(c, "reference", ref, sizeof ref, true))
        return;
    if (!zok(c, zxv_ledger_post_cap(CAP, from, to, amount, ccy, ref, &entry))) return;
    zxvd_buf_printf(c->res, "{\"entry_id\":\"%llu\"}", (unsigned long long) entry);
}

static void m_balance(call_t *c)
{
    uint32_t acct;
    int64_t d, cr, eq;
    if (!books(c) || !p_u32(c, "account", &acct)) return;
    if (!zok(c, zxv_ledger_balance_cap(CAP, acct, &d, &cr, &eq))) return;
    zxvd_buf_printf(
        c->res, "{\"debit_minor\":\"%lld\",\"credit_minor\":\"%lld\",\"equity_minor\":\"%lld\"}",
        (long long) d, (long long) cr, (long long) eq);
}

static void m_accounts(call_t *c)
{
    uint32_t n;
    if (!books(c) || !zok(c, zxv_ledger_account_count_cap(CAP, &n))) return;
    zxvd_buf_raw(c->res, "{\"accounts\":[", 13);
    for (uint32_t i = 0; i < n; i++) {
        char name[80], ccy[16];
        size_t l;
        int32_t kind;
        if (!zok(c, zxv_ledger_account_name_cap(CAP, i, name, sizeof name, &l)) ||
            !zok(c, zxv_ledger_account_ccy_cap(CAP, i, ccy, sizeof ccy, &l)) ||
            !zok(c, zxv_ledger_account_kind_cap(CAP, i, &kind)))
            return;
        zxvd_buf_printf(c->res, "%s{\"account\":%u,\"name\":", i ? "," : "", (unsigned) i);
        zxvd_buf_json_str(c->res, name);
        zxvd_buf_raw(c->res, ",\"currency\":", 12);
        zxvd_buf_json_str(c->res, ccy);
        zxvd_buf_printf(c->res, ",\"kind\":\"%s\"}", kind == ZXV_ACCT_ISSUER ? "issuer" : "holder");
    }
    zxvd_buf_raw(c->res, "]}", 2);
}

static void m_entries(call_t *c)
{
    uint32_t acct, n;
    if (!books(c) || !p_u32(c, "account", &acct) ||
        !zok(c, zxv_ledger_entry_count_cap(CAP, acct, &n)))
        return;
    zxvd_buf_raw(c->res, "{\"entries\":[", 12);
    for (uint32_t i = 0; i < n; i++) {
        uint64_t id;
        int64_t amt;
        uint32_t cp;
        char ref[64];
        size_t rl;
        if (!zok(c, zxv_ledger_entry_at_cap(CAP, acct, i, &id, &amt, &cp, ref, sizeof ref, &rl)))
            return;
        zxvd_buf_printf(c->res,
                        "%s{\"entry_id\":\"%llu\",\"signed_minor\":\"%lld\",\"counterparty\":%u,"
                        "\"reference\":",
                        i ? "," : "", (unsigned long long) id, (long long) amt, (unsigned) cp);
        zxvd_buf_json_str(c->res, ref);
        zxvd_buf_raw(c->res, "}", 1);
    }
    zxvd_buf_raw(c->res, "]}", 2);
}

static void m_earnings(call_t *c)
{
    if (!books(c)) return;
    zxvd_buf_raw(c->res, "{\"currency\":", 12);
    zxvd_buf_json_str(c->res, EARN_CCY);
    zxvd_buf_printf(c->res, ",\"rate_minor\":\"%llu\"", (unsigned long long) EARN_RATE);
    if (EARN_ACCTS)
        zxvd_buf_printf(c->res, ",\"source_account\":%u,\"owner_account\":%u", (unsigned) EARN_SRC,
                        (unsigned) EARN_OWNER);
    else
        zxvd_buf_raw(c->res, ",\"source_account\":null,\"owner_account\":null", 43);
    zxvd_buf_printf(
        c->res, ",\"balance_minor\":\"%lld\",\"postings\":\"%llu\",\"cycles\":\"%llu\"}",
        (long long) EARN_HELD, (unsigned long long) EARN_POSTS, (unsigned long long) EARN_SYNCS);
}

static void m_check(call_t *c)
{
    if (!books(c) || !zok(c, zxv_ledger_check_cap(CAP))) return;
    zxvd_buf_raw(c->res, "{\"ok\":true}", 11);
}

static void m_discover(call_t *c);

typedef struct {
    const char *name;
    void (*fn)(call_t *);
} method_t;

static const method_t METHODS[] = {
    {"rpc.discover", m_discover},
    {"zxvd.version", m_version},
    {"zxvd.shutdown", m_shutdown},
    {"swarm.state", m_state},
    {"swarm.ask", m_ask},
    {"model.info", m_model},
    {"notes.list", m_notes},
    {"notes.mark_read", m_notes_read},
    {"net.command", m_net},
    {"peer.ask", m_peer_ask},
    {"peer.serve", m_peer_serve},
    {"peer.status", m_peer_status},
    {"update.check", m_update},
    {"currency.enable", m_ccy_enable},
    {"ledger.open_account", m_open_account},
    {"ledger.post", m_post},
    {"ledger.balance", m_balance},
    {"ledger.accounts", m_accounts},
    {"ledger.entries", m_entries},
    {"ledger.check", m_check},
    {"swarm.earnings", m_earnings},
};
#define N_METHODS (sizeof METHODS / sizeof METHODS[0])

static void m_discover(call_t *c)
{
    zxvd_buf_raw(c->res, "{\"protocol\":\"" PROTOCOL "\",\"methods\":[",
                 sizeof("{\"protocol\":\"" PROTOCOL "\",\"methods\":[") - 1);
    for (size_t i = 0; i < N_METHODS; i++) {
        if (i) zxvd_buf_raw(c->res, ",", 1);
        zxvd_buf_json_str(c->res, METHODS[i].name);
    }
    zxvd_buf_raw(c->res, "]}", 2);
}

/* the id, copied verbatim (a string or an integer), or null */
static void put_id(zxvd_buf_t *out, const char *js, const w4_jtok_t *t, int32_t id)
{
    if (id < 0) {
        zxvd_buf_raw(out, "null", 4);
    } else if (t[id].type == W4_J_STR) {
        zxvd_buf_raw(out, "\"", 1);
        zxvd_buf_raw(out, js + t[id].start, t[id].end - t[id].start);
        zxvd_buf_raw(out, "\"", 1);
    } else {
        zxvd_buf_raw(out, js + t[id].start, t[id].end - t[id].start);
    }
}

static void put_error(zxvd_buf_t *out, const char *js, const w4_jtok_t *t, int32_t id, int code,
                      const char *msg, int32_t zst)
{
    zxvd_buf_raw(out, "{\"jsonrpc\":\"2.0\",\"id\":", 22);
    put_id(out, js, t, id);
    zxvd_buf_printf(out, ",\"error\":{\"code\":%d,\"message\":", code);
    zxvd_buf_json_str(out, msg);
    if (code == E_LIBZXV) {
        static const char *const CLASS[] = {"OK", "USAGE", "REFUSED", "FATAL", "SYSTEM"};
        int32_t k = zxv_status_class(zst);
        zxvd_buf_printf(out, ",\"data\":{\"status\":%d,\"name\":", (int) zst);
        zxvd_buf_json_str(out, zxv_status_name(zst));
        zxvd_buf_printf(out, ",\"class\":\"%s\"}", k >= 0 && k <= 4 ? CLASS[k] : "SYSTEM");
    }
    zxvd_buf_raw(out, "}}", 2);
}

bool zxvd_rpc_handle_line(const char *line, size_t len, zxvd_buf_t *out)
{
    static w4_jtok_t t[MAX_TOK];
    out->len = 0;
    if (len > ZXVD_RPC_MAX_LINE) {
        put_error(out, line, t, -1, E_REQUEST, "request too long", 0);
        return true;
    }
    int32_t n = w4_json_parse(line, (uint32_t) len, t, MAX_TOK, 16);
    if (n <= 0) {
        put_error(out, line, t, -1, n == W4_ERR_SPACE ? E_REQUEST : E_PARSE,
                  n == W4_ERR_SPACE ? "request too complex" : "parse error", 0);
        return true;
    }
    if (t[0].type != W4_J_OBJ) {
        put_error(out, line, t, -1, E_REQUEST,
                  t[0].type == W4_J_ARR ? "batches are not supported" : "not a request object", 0);
        return true;
    }
    int32_t id = w4_json_get(line, t, n, 0, "id");
    if (id == W4_ERR_PARSE) {
        put_error(out, line, t, -1, E_REQUEST, "duplicate id", 0);
        return true;
    }
    bool notify = id < 0;
    if (!notify && t[id].type != W4_J_STR && t[id].type != W4_J_NUM && t[id].type != W4_J_NULL) {
        put_error(out, line, t, -1, E_REQUEST, "id must be a string, number or null", 0);
        return true;
    }
    if (!notify && t[id].type == W4_J_NULL) id = -1; /* answered with "id":null */
    int32_t v = w4_json_get(line, t, n, 0, "jsonrpc");
    int32_t m = w4_json_get(line, t, n, 0, "method");
    int32_t p = w4_json_get(line, t, n, 0, "params");
    if (v < 0 || !w4_json_str_eq(line, &t[v], "2.0") || m < 0 || t[m].type != W4_J_STR ||
        p == W4_ERR_PARSE) {
        put_error(out, line, t, id, E_REQUEST, "invalid request", 0);
        return !notify;
    }
    if (p >= 0 && t[p].type != W4_J_OBJ) {
        put_error(out, line, t, id, E_PARAMS, "params must be an object", 0);
        return !notify;
    }
    const method_t *fn = NULL;
    for (size_t i = 0; i < N_METHODS; i++)
        if (w4_json_str_eq(line, &t[m], METHODS[i].name)) fn = &METHODS[i];
    if (!fn) {
        put_error(out, line, t, id, E_METHOD, "method not found", 0);
        return !notify;
    }
    zxvd_buf_t res = {0};
    call_t c = {line, t, n, p, &res, 0, "", 0};
    fn->fn(&c);
    if (!c.err && res.oom) fail(&c, E_INTERNAL, "out of memory");
    if (!c.err && !res.len) fail(&c, E_INTERNAL, "no result");
    if (c.err) {
        put_error(out, line, t, id, c.err, c.msg, c.zst);
    } else {
        zxvd_buf_raw(out, "{\"jsonrpc\":\"2.0\",\"id\":", 22);
        put_id(out, line, t, id);
        zxvd_buf_raw(out, ",\"result\":", 10);
        zxvd_buf_raw(out, res.p, res.len);
        zxvd_buf_raw(out, "}", 1);
    }
    free(res.p);
    return !notify;
}

/* ===================== the socket (T1-T4) ===================== */

#if defined(_WIN32)
int zxvd_rpc_open(const char *path, const zxvd_host_ops_t *ops)
{
    (void) path;
    (void) ops;
    fprintf(stderr, "zxvd: the RPC socket is not built for Windows yet\n");
    return -1;
}
int zxvd_rpc_fds(void *set)
{
    (void) set;
    return -1;
}
void zxvd_rpc_service(void *set, uint64_t now_ms)
{
    (void) set;
    (void) now_ms;
}
void zxvd_rpc_close(void)
{
    books_close();
}
#else

typedef struct {
    int fd;                         /* -1 when free */
    char in[ZXVD_RPC_MAX_LINE + 2]; /* a full-length line, its LF, and a NUL */
    size_t n;
    uint64_t last_ms;
} client_t;

static int LS = -1;
static char PATH_[sizeof(((struct sockaddr_un *) 0)->sun_path)];
static client_t CL[ZXVD_RPC_MAX_CLIENTS];

static bool same_user(int fd)
{
#    if defined(__linux__)
    struct ucred cr;
    socklen_t l = sizeof cr;
    return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cr, &l) == 0 && cr.uid == geteuid();
#    else
    uid_t u;
    gid_t g;
    return getpeereid(fd, &u, &g) == 0 && u == geteuid();
#    endif
}

/* Open the socket's folder, creating it 0700 if missing, and check that it is
 * ours and that nobody else can write in it (else they could swap the socket
 * file under us). Returns the folder's descriptor (name = the socket's file
 * name in it), or -1. Every later look at or change to the socket file goes
 * through this descriptor (fstatat, unlinkat, fchmodat), so what was checked
 * is what is changed. A bare file name is the current folder, the owner's own
 * choice, and is not checked; a file in "/" is refused (not our folder). */
static int open_dir(const char *path, const char **name)
{
    char dir[sizeof PATH_];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    if (!slash) {
        *name = path;
        return open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    }
    *name = path + (slash - dir) + 1;
    *slash = 0;
    (void) mkdir(dir, S_IRWXU); /* EEXIST is fine: the checks below decide */
    int dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    struct stat st;
    if (fstat(dfd, &st) != 0 || st.st_uid != geteuid() || (st.st_mode & (S_IWGRP | S_IWOTH))) {
        close(dfd);
        return -1;
    }
    return dfd;
}

static int open_fail(int dfd, bool books)
{
    close(dfd);
    if (books) books_close();
    return -1;
}

int zxvd_rpc_open(const char *path, const zxvd_host_ops_t *ops)
{
    struct sockaddr_un a;
    if (!path || strlen(path) >= sizeof a.sun_path) {
        fprintf(stderr, "zxvd: --rpc path too long (max %zu bytes)\n", sizeof a.sun_path - 1);
        return -1;
    }
    const char *name;
    int dfd = open_dir(path, &name);
    if (dfd < 0) {
        fprintf(stderr,
                "zxvd: the folder of %s is not usable (missing, not ours, or writable by others)\n",
                path);
        return -1;
    }
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    memcpy(a.sun_path, path, strlen(path) + 1);
    struct stat st;
    if (fstatat(dfd, name, &st, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!S_ISSOCK(st.st_mode)) {
            fprintf(stderr, "zxvd: %s exists and is not a socket; refusing to replace it\n", path);
            return open_fail(dfd, false);
        }
        int probe = socket(AF_UNIX, SOCK_STREAM, 0);
        bool live = probe >= 0 && connect(probe, (struct sockaddr *) &a, sizeof a) == 0;
        if (probe >= 0) close(probe);
        if (live) {
            fprintf(stderr, "zxvd: another zxvd answers on %s\n", path);
            return open_fail(dfd, false);
        }
        unlinkat(dfd, name, 0); /* stale */
    }
    if (zxvd_rpc_books_open() != 0) {
        fprintf(stderr, "zxvd: the books could not be opened (libzxv older than ABI 1.3, "
                        "or the earnings currency was refused)\n");
        return open_fail(dfd, false);
    }
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return open_fail(dfd, true);
    mode_t old = umask(S_IXUSR | S_IRWXG | S_IRWXO); /* the socket file is born 0600 */
    int b = bind(fd, (struct sockaddr *) &a, sizeof a);
    umask(old);
    if (b != 0 || fchmodat(dfd, name, S_IRUSR | S_IWUSR, 0) != 0 || listen(fd, 8) != 0) {
        fprintf(stderr, "zxvd: cannot listen on %s: %s\n", path, strerror(errno));
        close(fd);
        return open_fail(dfd, true);
    }
    close(dfd);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    LS = fd;
    snprintf(PATH_, sizeof PATH_, "%s", path);
    for (int i = 0; i < ZXVD_RPC_MAX_CLIENTS; i++) CL[i].fd = -1;
    OPS = ops;
    return 0;
}

int zxvd_rpc_fds(void *set)
{
    fd_set *s = set;
    if (LS < 0) return -1;
    int mx = LS;
    FD_SET(LS, s);
    for (int i = 0; i < ZXVD_RPC_MAX_CLIENTS; i++)
        if (CL[i].fd >= 0) {
            FD_SET(CL[i].fd, s);
            if (CL[i].fd > mx) mx = CL[i].fd;
        }
    return mx;
}

static void drop(client_t *c)
{
    close(c->fd);
    c->fd = -1;
    c->n = 0;
}

/* send all of it, or give up on this client (2 s send timeout) */
static bool send_line(client_t *c, const zxvd_buf_t *b)
{
    size_t off = 0;
    while (off < b->len) {
        ssize_t k = send(c->fd, b->p + off, b->len - off, 0);
        if (k <= 0) return false;
        off += (size_t) k;
    }
    return send(c->fd, "\n", 1, 0) == 1;
}

static void serve(client_t *c, uint64_t now_ms)
{
    ssize_t k = recv(c->fd, c->in + c->n, sizeof c->in - 1 - c->n, 0);
    if (k <= 0) {
        drop(c);
        return;
    }
    c->n += (size_t) k;
    c->last_ms = now_ms;
    zxvd_buf_t out = {0};
    for (;;) {
        char *nl = memchr(c->in, '\n', c->n);
        if (!nl) break;
        size_t ll = (size_t) (nl - c->in);
        size_t body = ll && c->in[ll - 1] == '\r' ? ll - 1 : ll;
        bool reply = body ? zxvd_rpc_handle_line(c->in, body, &out) : false;
        if (reply && (out.oom || !send_line(c, &out))) {
            drop(c);
            free(out.p);
            return;
        }
        memmove(c->in, nl + 1, c->n - ll - 1);
        c->n -= ll + 1;
    }
    free(out.p);
    if (c->n >= sizeof c->in - 1) drop(c); /* T3: line too long */
}

void zxvd_rpc_service(void *set, uint64_t now_ms)
{
    fd_set *s = set;
    if (LS < 0) return;
    for (int i = 0; i < ZXVD_RPC_MAX_CLIENTS; i++) {
        client_t *c = &CL[i];
        if (c->fd < 0) continue;
        if (FD_ISSET(c->fd, s))
            serve(c, now_ms);
        else if (now_ms - c->last_ms > ZXVD_RPC_IDLE_MS)
            drop(c); /* T4 */
    }
    if (FD_ISSET(LS, s)) {
        int fd = accept(LS, NULL, NULL);
        if (fd < 0) return;
        fcntl(fd, F_SETFD, FD_CLOEXEC);
        if (!same_user(fd)) { /* T2 */
            close(fd);
            return;
        }
        struct timeval to = {2, 0};
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof to);
        for (int i = 0; i < ZXVD_RPC_MAX_CLIENTS; i++)
            if (CL[i].fd < 0) {
                CL[i].fd = fd;
                CL[i].n = 0;
                CL[i].last_ms = now_ms;
                return;
            }
        close(fd); /* T4: full */
    }
}

void zxvd_rpc_close(void)
{
    for (int i = 0; i < ZXVD_RPC_MAX_CLIENTS; i++)
        if (CL[i].fd >= 0) drop(&CL[i]);
    if (LS >= 0) {
        close(LS);
        unlink(PATH_);
        LS = -1;
    }
    books_close();
}
#endif
