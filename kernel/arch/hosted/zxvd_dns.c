/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_dns.c — names and addresses (see zxvd_dns.h D1-D6, I1-I3). */
#if !defined(_WIN32)
#    define _DEFAULT_SOURCE
#    define _DARWIN_C_SOURCE
#    include <sys/types.h>
#    include <sys/socket.h>
#    include <arpa/inet.h>
#    include <netdb.h>
#    include <netinet/in.h>
#    include <pthread.h>
#endif
#include <stdio.h>
#include <string.h>

#include "zxvd_dns.h"
#include "zxvd_ext.h" /* zxvd_buf_hex */
#include "zxvd_log.h" /* zxvd_mono_us */
#include "keccak.h"   /* sha3_256, for did:zxv */

#define E_PARAMS (-32602)

/* ===================== DIDs (I1-I3): no network ===================== */

#define DID_MAX    (8u + ZXVD_DNS_NAME_MAX + 256u)
#define DID_ZXV_ID 32u

static const char B58[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
static const char B32[] = "abcdefghijklmnopqrstuvwxyz234567";

/* Base58btc text to exactly `want` bytes; false for any other length or a
 * letter outside the alphabet. A leading '1' is a zero byte, so an
 * encoding with spare leading ones has another length and is refused. */
static bool b58_decode(const char *s, uint8_t *out, uint32_t want)
{
    uint8_t num[64] = {0}; /* big-endian, the value's last `want` bytes */
    uint32_t zeros = 0, n = 0;
    while (s[zeros] == '1') zeros++;
    for (const char *p = s + zeros; *p; p++) {
        const char *at = strchr(B58, *p);
        if (!at) return false;
        uint32_t carry = (uint32_t) (at - B58);
        for (uint32_t i = 0; i < sizeof num; i++) {
            carry += 58u * num[sizeof num - 1 - i];
            num[sizeof num - 1 - i] = (uint8_t) carry;
            carry >>= 8;
        }
        if (carry) return false; /* longer than any key */
        n++;
    }
    uint32_t lead = 0; /* the value's own length */
    while (lead < sizeof num && !num[lead]) lead++;
    uint32_t len = (uint32_t) sizeof num - lead;
    if (zeros + len != want || (n == 0 && zeros == 0)) return false;
    memset(out, 0, zeros);
    memcpy(out + zeros, num + lead, len);
    return true;
}

/* did:zxv's 52 letters to the 32-byte id; the last letter's 4 spare bits
 * must be 0 (one spelling per id). */
static bool b32_decode(const char *s, uint8_t out[DID_ZXV_ID])
{
    uint32_t acc = 0, bits = 0, o = 0, i = 0;
    for (; s[i]; i++) {
        const char *at = strchr(B32, s[i]);
        if (!at || i >= 52u) return false;
        acc = (acc << 5) | (uint32_t) (at - B32);
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (uint8_t) (acc >> bits);
        }
        acc &= (1u << bits) - 1u;
    }
    return i == 52u && o == DID_ZXV_ID && acc == 0;
}

typedef struct {
    char method[33];
    const char *id;  /* in the caller's text */
    uint8_t raw[34]; /* did:key: 0xed 0x01 + key; did:zxv: the id */
    uint32_t kind;   /* 0 other, 1 did:key Ed25519, 2 did:zxv */
} did_t;

/* "did:" method ":" id, the method 1-32 lower-case letters and digits, the
 * id 1 or more of the W3C id characters (letters, digits, . - _ : %). */
static bool did_read(zxvd_call_t *c, const char *s, did_t *d)
{
    memset(d, 0, sizeof *d);
    if (strncmp(s, "did:", 4) != 0) {
        zxvd_fail(c, ZXVD_E_DNS, "a DID starts with \"did:\"");
        return false;
    }
    const char *m = s + 4, *colon = strchr(m, ':');
    size_t mn = colon ? (size_t) (colon - m) : 0;
    bool ok = mn >= 1 && mn < sizeof d->method && colon[1];
    for (size_t i = 0; ok && i < mn; i++)
        ok = (m[i] >= 'a' && m[i] <= 'z') || (m[i] >= '0' && m[i] <= '9');
    for (const char *p = colon ? colon + 1 : ""; ok && *p; p++)
        ok = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
             *p == '.' || *p == '-' || *p == '_' || *p == ':' || *p == '%';
    if (!ok) {
        zxvd_fail(c, ZXVD_E_DNS, "not a DID: did:<method>:<id>");
        return false;
    }
    memcpy(d->method, m, mn);
    d->id = colon + 1;
    if (!strcmp(d->method, "key")) {
        if (d->id[0] != 'z' || !b58_decode(d->id + 1, d->raw, 34) || d->raw[0] != 0xed ||
            d->raw[1] != 0x01) {
            zxvd_fail(c, ZXVD_E_DNS, "did:key here is an Ed25519 key: z6Mk and its 32 bytes");
            return false;
        }
        d->kind = 1;
    } else if (!strcmp(d->method, "zxv")) {
        if (!b32_decode(d->id, d->raw)) {
            zxvd_fail(c, ZXVD_E_DNS, "did:zxv is 52 lower-case base32 letters (32 bytes)");
            return false;
        }
        d->kind = 2;
    }
    return true;
}

static bool p_did(zxvd_call_t *c, did_t *d, char *text)
{
    return zxvd_param_str(c, "did", text, DID_MAX, true) && did_read(c, text, d);
}

void zxvd_did_parse(zxvd_call_t *c)
{
    static char text[DID_MAX];
    did_t d;
    if (!p_did(c, &d, text)) return;
    zxvd_buf_t *r = zxvd_result(c);
    zxvd_buf_raw(r, "{\"did\":", 7);
    zxvd_buf_json_str(r, text);
    zxvd_buf_printf(r, ",\"method\":\"%s\",\"supported\":%s", d.method, d.kind ? "true" : "false");
    if (d.kind == 1) {
        zxvd_buf_raw(r, ",\"type\":\"ed25519\",\"key\":", 24);
        zxvd_buf_hex(r, d.raw + 2, 32); /* a quoted hex string */
    } else if (d.kind == 2) {
        zxvd_buf_raw(r, ",\"id\":", 6);
        zxvd_buf_hex(r, d.raw, DID_ZXV_ID);
    }
    zxvd_buf_raw(r, "}", 1);
}

static int hexval(char h)
{
    return h >= '0' && h <= '9'   ? h - '0'
           : h >= 'a' && h <= 'f' ? h - 'a' + 10
           : h >= 'A' && h <= 'F' ? h - 'A' + 10
                                  : -1;
}

void zxvd_did_match(zxvd_call_t *c)
{
    static char text[DID_MAX], hex[2u * ZXVD_DID_KEY_MAX + 1u];
    static uint8_t buf[13u + ZXVD_DID_KEY_MAX];
    did_t d;
    if (!p_did(c, &d, text) || !zxvd_param_str(c, "key", hex, sizeof hex, true)) return;
    if (!d.kind) return zxvd_fail(c, ZXVD_E_DNS, "did:%s keys cannot be checked here", d.method);
    size_t hn = strlen(hex), n = hn / 2u;
    bool ok = hn && hn % 2u == 0;
    for (size_t i = 0; ok && i < n; i++) {
        int a = hexval(hex[2 * i]), b = hexval(hex[2 * i + 1]);
        ok = a >= 0 && b >= 0;
        if (ok) buf[13 + i] = (uint8_t) (a << 4 | b);
    }
    if (!ok) return zxvd_fail(c, E_PARAMS, "\"key\" must be hex, 1 to %u bytes", ZXVD_DID_KEY_MAX);
    bool match;
    if (d.kind == 1) {
        if (n != 32) return zxvd_fail(c, E_PARAMS, "an Ed25519 \"key\" is 32 bytes");
        match = !memcmp(buf + 13, d.raw + 2, 32);
    } else {
        uint8_t h[32];
        memcpy(buf, "ZXV-ident/did", 13);
        sha3_256(buf, 13 + n, h);
        match = !memcmp(h, d.raw, 32);
    }
    zxvd_buf_t *r = zxvd_result(c);
    zxvd_buf_raw(r, "{\"did\":", 7);
    zxvd_buf_json_str(r, text);
    zxvd_buf_printf(r, ",\"match\":%s}", match ? "true" : "false");
}

/* ===================== DNS (D1-D6) ===================== */

#if defined(_WIN32)

void zxvd_dns_enable(bool on)
{
    (void) on;
}
void zxvd_dns_service(void) {}
void zxvd_dns_close(void) {}

static void not_here(zxvd_call_t *c)
{
    zxvd_fail(c, ZXVD_E_DNS, "dns.* is not on Windows yet");
}
void zxvd_dns_resolve(zxvd_call_t *c)
{
    not_here(c);
}
void zxvd_dns_reverse(zxvd_call_t *c)
{
    not_here(c);
}
void zxvd_dns_flush(zxvd_call_t *c)
{
    not_here(c);
}

#else

enum { S_FREE, S_WAIT, S_RUN, S_DONE };
enum { FAM_ANY, FAM_4, FAM_6, FAM_REV };

#    define ADDR_TEXT 46u

typedef struct {
    int state; /* S_*: the loop's view; S_RUN until it sees `ended` */
    int fam;   /* FAM_*; FAM_REV: key is an address to name */
    char key[ZXVD_DNS_NAME_MAX + 1];
    pthread_t th;
    bool ended; /* the thread is done (under MU) */
    int err;    /* 0, or which error (ERR_*) */
    uint32_t naddr;
    char addr[ZXVD_DNS_ADDRS][ADDR_TEXT];
    int afam[ZXVD_DNS_ADDRS];         /* 4 or 6 */
    char name[ZXVD_DNS_NAME_MAX + 2]; /* a reverse answer */
    uint64_t done_ms, used_ms;
} ent_t;

enum { ERR_NONE, ERR_NONAME, ERR_AGAIN, ERR_FAIL };
static const char *const ERR_TEXT[] = {"", "no such name", "no answer now", "failed"};

static ent_t E[ZXVD_DNS_ENTRIES];
static pthread_mutex_t MU = PTHREAD_MUTEX_INITIALIZER;
static bool ON;

void zxvd_dns_enable(bool on)
{
    ON = on;
}

static uint64_t now_ms(void)
{
    return zxvd_mono_us() / 1000u;
}

static int map_err(int e)
{
    if (e == EAI_NONAME) return ERR_NONAME;
#    if defined(EAI_NODATA) && EAI_NODATA != EAI_NONAME
    if (e == EAI_NODATA) return ERR_NONAME;
#    endif
    if (e == EAI_AGAIN) return ERR_AGAIN;
    return ERR_FAIL;
}

/* The lookup, on its own thread: it writes only its own entry's answer
 * fields, then sets `ended` under MU; the loop reads them after that. */
static void *lookup(void *arg)
{
    ent_t *e = arg;
    if (e->fam == FAM_REV) {
        struct sockaddr_storage ss;
        socklen_t sl;
        memset(&ss, 0, sizeof ss);
        struct sockaddr_in *s4 = (struct sockaddr_in *) &ss;
        struct sockaddr_in6 *s6 = (struct sockaddr_in6 *) &ss;
        if (inet_pton(AF_INET, e->key, &s4->sin_addr) == 1) {
            s4->sin_family = AF_INET;
            sl = sizeof *s4;
        } else {
            inet_pton(AF_INET6, e->key, &s6->sin6_addr); /* checked before the thread */
            s6->sin6_family = AF_INET6;
            sl = sizeof *s6;
        }
        int r =
            getnameinfo((struct sockaddr *) &ss, sl, e->name, sizeof e->name, NULL, 0, NI_NAMEREQD);
        e->err = r ? map_err(r) : ERR_NONE;
    } else {
        struct addrinfo hints, *res = NULL;
        memset(&hints, 0, sizeof hints);
        hints.ai_family = e->fam == FAM_4 ? AF_INET : e->fam == FAM_6 ? AF_INET6 : AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM; /* one entry per address */
        int r = getaddrinfo(e->key, NULL, &hints, &res);
        e->err = r ? map_err(r) : ERR_NONE;
        e->naddr = 0;
        for (struct addrinfo *a = res; !r && a && e->naddr < ZXVD_DNS_ADDRS; a = a->ai_next) {
            char t[ADDR_TEXT];
            const void *src = a->ai_family == AF_INET
                                  ? (const void *) &((struct sockaddr_in *) a->ai_addr)->sin_addr
                                  : (const void *) &((struct sockaddr_in6 *) a->ai_addr)->sin6_addr;
            if ((a->ai_family != AF_INET && a->ai_family != AF_INET6) ||
                !inet_ntop(a->ai_family, src, t, sizeof t))
                continue;
            bool seen = false;
            for (uint32_t i = 0; i < e->naddr && !seen; i++) seen = !strcmp(e->addr[i], t);
            if (seen) continue;
            memcpy(e->addr[e->naddr], t, sizeof t);
            e->afam[e->naddr++] = a->ai_family == AF_INET ? 4 : 6;
        }
        if (res) freeaddrinfo(res);
        if (!r && !e->naddr) e->err = ERR_NONAME;
    }
    pthread_mutex_lock(&MU);
    e->ended = true;
    pthread_mutex_unlock(&MU);
    return NULL;
}

static uint32_t running(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < ZXVD_DNS_ENTRIES; i++) n += E[i].state == S_RUN;
    return n;
}

/* D5: join the lookups that ended, then start waiting ones, oldest ask
 * first. */
void zxvd_dns_service(void)
{
    uint64_t now = now_ms();
    for (uint32_t i = 0; i < ZXVD_DNS_ENTRIES; i++) {
        if (E[i].state != S_RUN) continue;
        pthread_mutex_lock(&MU);
        bool ended = E[i].ended;
        pthread_mutex_unlock(&MU);
        if (!ended) continue;
        pthread_join(E[i].th, NULL);
        E[i].state = S_DONE;
        E[i].done_ms = now;
    }
    for (uint32_t run = running(); run < ZXVD_DNS_THREADS; run++) {
        ent_t *next = NULL;
        for (uint32_t i = 0; i < ZXVD_DNS_ENTRIES; i++)
            if (E[i].state == S_WAIT && (!next || E[i].used_ms < next->used_ms)) next = &E[i];
        if (!next) break;
        next->ended = false;
        next->state = S_RUN;
        if (pthread_create(&next->th, NULL, lookup, next) != 0) {
            next->state = S_DONE; /* no thread: a failure, kept the short time */
            next->err = ERR_AGAIN;
            next->done_ms = now;
        }
    }
}

void zxvd_dns_close(void)
{
    for (uint32_t i = 0; i < ZXVD_DNS_ENTRIES; i++)
        if (E[i].state == S_RUN) pthread_join(E[i].th, NULL);
    memset(E, 0, sizeof E);
}

static bool fresh(const ent_t *e, uint64_t now)
{
    return now - e->done_ms < (e->err ? ZXVD_DNS_FAIL_MS : ZXVD_DNS_KEEP_MS);
}

static const char *const FAM_NAME[] = {"any", "ipv4", "ipv6"};

/* the answer for one entry (or a literal: e NULL, the address in key) */
static void answer(zxvd_call_t *c, const ent_t *e, uint64_t now)
{
    zxvd_buf_t *r = zxvd_result(c);
    if (e->fam == FAM_REV)
        zxvd_buf_printf(r, "{\"addr\":\"%s\"", e->key);
    else
        zxvd_buf_printf(r, "{\"name\":\"%s\",\"family\":\"%s\"", e->key, FAM_NAME[e->fam]);
    if (e->state != S_DONE) {
        zxvd_buf_raw(r, ",\"state\":\"pending\"}", 19);
        return;
    }
    uint64_t keep = e->err ? ZXVD_DNS_FAIL_MS : ZXVD_DNS_KEEP_MS, age = now - e->done_ms;
    zxvd_buf_printf(r, ",\"state\":\"%s\"", e->err ? "failed" : "done");
    if (e->err)
        zxvd_buf_printf(r, ",\"error\":\"%s\"", ERR_TEXT[e->err]);
    else if (e->fam == FAM_REV) {
        zxvd_buf_raw(r, ",\"name\":", 8);
        zxvd_buf_json_str(r, e->name);
    } else {
        zxvd_buf_raw(r, ",\"addrs\":[", 10);
        for (uint32_t i = 0; i < e->naddr; i++)
            zxvd_buf_printf(r, "%s{\"addr\":\"%s\",\"family\":\"ipv%d\"}", i ? "," : "", e->addr[i],
                            e->afam[i]);
        zxvd_buf_raw(r, "]", 1);
    }
    zxvd_buf_printf(r, ",\"ttl_ms\":%llu}", (unsigned long long) (age < keep ? keep - age : 0));
}

/* D2: the entry for (fam, key), asked now: a fresh answer is used, a stale
 * one asked again, a new one placed. NULL when every place waits or runs. */
static ent_t *ask(int fam, const char *key, uint64_t now)
{
    ent_t *e = NULL, *old = NULL;
    for (uint32_t i = 0; i < ZXVD_DNS_ENTRIES && !e; i++)
        if (E[i].state != S_FREE && E[i].fam == fam && !strcmp(E[i].key, key)) e = &E[i];
    if (!e) {
        for (uint32_t i = 0; i < ZXVD_DNS_ENTRIES && !e; i++)
            if (E[i].state == S_FREE) e = &E[i];
        for (uint32_t i = 0; i < ZXVD_DNS_ENTRIES && !e; i++)
            if (E[i].state == S_DONE && (!old || E[i].used_ms < old->used_ms)) old = &E[i];
        if (!e) e = old;
        if (!e) return NULL;
        memset(e, 0, sizeof *e);
        e->fam = fam;
        memcpy(e->key, key, strlen(key) + 1);
        e->state = S_WAIT;
    } else if (e->state == S_DONE && !fresh(e, now)) {
        e->state = S_WAIT;
    }
    e->used_ms = now;
    if (e->state == S_WAIT) zxvd_dns_service(); /* start it now if a thread is free */
    return e;
}

/* D4: the name in lower case without its trailing dot; false if not a name */
static bool name_ok(const char *in, char *out)
{
    size_t n = strlen(in);
    if (n && in[n - 1] == '.') n--;
    if (!n || n > ZXVD_DNS_NAME_MAX) return false;
    size_t label = 0;
    for (size_t i = 0; i < n; i++) {
        char ch = in[i];
        if (ch == '.') {
            if (!label || in[i - 1] == '-') return false;
            label = 0;
        } else {
            bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                      (ch >= '0' && ch <= '9') || ch == '_' || (ch == '-' && label);
            if (!ok || ++label > 63u) return false;
        }
        out[i] = ch >= 'A' && ch <= 'Z' ? (char) (ch - 'A' + 'a') : ch;
    }
    if (!label || in[n - 1] == '-') return false; /* an empty or bad last label */
    out[n] = 0;
    return true;
}

static bool on_or_fail(zxvd_call_t *c)
{
    if (!ON) zxvd_fail(c, ZXVD_E_DNS, "dns.* is off in this zxvd");
    return ON;
}

void zxvd_dns_resolve(zxvd_call_t *c)
{
    char in[ZXVD_DNS_NAME_MAX + 3], key[ZXVD_DNS_NAME_MAX + 1], fs[8] = "";
    if (!on_or_fail(c) || !zxvd_param_str(c, "name", in, sizeof in, true) ||
        !zxvd_param_str(c, "family", fs, sizeof fs, false))
        return;
    int fam = !fs[0] || !strcmp(fs, "any") ? FAM_ANY
              : !strcmp(fs, "ipv4")        ? FAM_4
              : !strcmp(fs, "ipv6")        ? FAM_6
                                           : -1;
    if (fam < 0) return zxvd_fail(c, E_PARAMS, "\"family\" is any, ipv4 or ipv6");
    uint8_t bin[16];
    int lit = inet_pton(AF_INET, in, bin) == 1 ? 4 : inet_pton(AF_INET6, in, bin) == 1 ? 6 : 0;
    if (lit) { /* D1: an address already; written back in its usual form */
        if ((fam == FAM_4 && lit != 4) || (fam == FAM_6 && lit != 6))
            return zxvd_fail(c, E_PARAMS, "\"%s\" is an IPv%d address", in, lit);
        char t[ADDR_TEXT];
        inet_ntop(lit == 4 ? AF_INET : AF_INET6, bin, t, sizeof t);
        zxvd_buf_printf(zxvd_result(c),
                        "{\"name\":\"%s\",\"family\":\"%s\",\"state\":\"done\",\"addrs\":[{"
                        "\"addr\":\"%s\",\"family\":\"ipv%d\"}],\"ttl_ms\":%u}",
                        t, FAM_NAME[fam], t, lit, ZXVD_DNS_KEEP_MS);
        return;
    }
    if (!name_ok(in, key))
        return zxvd_fail(c, E_PARAMS,
                         "\"name\" must be 1 to %u bytes of labels of 1 to 63 letters, digits, - "
                         "and _ (no - at a label's ends)",
                         ZXVD_DNS_NAME_MAX);
    zxvd_dns_service();
    uint64_t now = now_ms();
    ent_t *e = ask(fam, key, now);
    if (!e)
        return zxvd_fail(c, ZXVD_E_DNS, "busy: %u lookups are waiting; ask again",
                         ZXVD_DNS_ENTRIES);
    answer(c, e, now);
}

void zxvd_dns_reverse(zxvd_call_t *c)
{
    char in[ADDR_TEXT + 8], key[ADDR_TEXT];
    uint8_t bin[16];
    if (!on_or_fail(c) || !zxvd_param_str(c, "addr", in, sizeof in, true)) return;
    int af = inet_pton(AF_INET, in, bin) == 1    ? AF_INET
             : inet_pton(AF_INET6, in, bin) == 1 ? AF_INET6
                                                 : 0;
    if (!af) return zxvd_fail(c, E_PARAMS, "\"addr\" must be an IPv4 or IPv6 address");
    inet_ntop(af, bin, key, sizeof key); /* one spelling per address */
    zxvd_dns_service();
    uint64_t now = now_ms();
    ent_t *e = ask(FAM_REV, key, now);
    if (!e)
        return zxvd_fail(c, ZXVD_E_DNS, "busy: %u lookups are waiting; ask again",
                         ZXVD_DNS_ENTRIES);
    answer(c, e, now);
}

void zxvd_dns_flush(zxvd_call_t *c)
{
    if (!on_or_fail(c)) return;
    zxvd_dns_service();
    uint32_t n = 0;
    for (uint32_t i = 0; i < ZXVD_DNS_ENTRIES; i++)
        if (E[i].state == S_DONE) {
            memset(&E[i], 0, sizeof E[i]);
            n++;
        }
    zxvd_buf_printf(zxvd_result(c), "{\"forgot\":%u}", n);
}

#endif
