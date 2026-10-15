/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxvd_irc.c — zxvd_irc.c (irc.*, zxvd_irc.h G1-G8) behind the real RPC
 * layer, over real sockets on 127.0.0.1. Clients speak TLS 1.3 through the
 * repo's own client with TLS_VERIFY_REQUIRED and the gateway's certificate
 * pinned, so every exchange is fully authenticated. Each signed record is
 * checked with ML-DSA-87 against the key the test derives itself from the
 * gateway's seed file. The bridge is tested against the gateway itself: one
 * channel bridged to another, through the same TLS a public network gets.
 * OpenSSL interoperability is test_zxvd_irc.sh's job. POSIX only. */
#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "zxvd_groups.h"
#include "zxvd_irc.h"
#include "zxv_irc.h"
#include "../../src/mlkem/keccak.h"
#include "../../src/pqsec/pq_matrix_algs.h"
#include "../../src/robin_debanks/sha256.h"
#include "../../src/tls/handshake.h"
#include "../../src/tls/selfsigned.h"

#if !defined(MSG_NOSIGNAL)
#    define MSG_NOSIGNAL 0 /* macOS: SIGPIPE is ignored in main */
#endif

static int fails, checks;

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

/* ----- RPC ----- */

static zxvd_buf_t OUT;
static char REQB[4096];

static const char *rpc(const char *method, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
static const char *rpc(const char *method, const char *fmt, ...)
{
    va_list ap;
    int k = snprintf(REQB, sizeof REQB,
                     "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"%s\",\"params\":", method);
    va_start(ap, fmt);
    k += vsnprintf(REQB + k, sizeof REQB - (size_t) k, fmt, ap);
    va_end(ap);
    snprintf(REQB + k, sizeof REQB - (size_t) k, "}");
    CHECK(zxvd_rpc_handle_line(REQB, strlen(REQB), &OUT), "a reply for %.200s", REQB);
    return OUT.p ? OUT.p : "";
}

/* the string value of "key" (first occurrence), or "" */
static const char *jstr(const char *js, const char *key)
{
    static char v[4][24000];
    static int i;
    char pat[64];
    i = (i + 1) & 3;
    v[i][0] = 0;
    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = strstr(js, pat);
    if (!p) return v[i];
    p += strlen(pat);
    size_t n = 0;
    while (*p && *p != '"' && n < sizeof v[i] - 1) {
        if (*p == '\\' && p[1]) p++;
        v[i][n++] = *p++;
    }
    v[i][n] = 0;
    return v[i];
}

static bool has(const char *js, const char *s)
{
    return strstr(js, s) != NULL;
}

static int unhex(const char *h, uint8_t *out, int cap)
{
    int n = 0;
    for (; h[0] && h[1] && n < cap; h += 2) {
        unsigned v;
        if (sscanf(h, "%2x", &v) != 1) return -1;
        out[n++] = (uint8_t) v;
    }
    return n;
}

static void tohex(const uint8_t *p, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) snprintf(out + 2 * i, 3, "%02x", p[i]);
}

/* ----- the gateway's files ----- */

static char DIR_[256];

static long file(const char *name, uint8_t *buf, size_t cap, unsigned *mode)
{
    char p[512];
    snprintf(p, sizeof p, "%s/%s", DIR_, name);
    struct stat st;
    if (stat(p, &st) != 0) return -1;
    if (mode) *mode = (unsigned) (st.st_mode & 0777);
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    long n = (long) fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

/* ----- clients: the repo's TLS client over a real socket ----- */

typedef struct {
    int fd;
    tls_client_t tls;
    uint8_t rb[40000];
    uint32_t rl;
    char in[200000];
    uint32_t il;
    bool closed;
} cli_t;

static tls_trust_t PINNED, OTHER;
static uint8_t CERT[2048];
static uint32_t CERT_LEN;

static void pump_once(cli_t **cs, int n, int wait_ms);

static void cli_raw(cli_t *c, const uint8_t *p, uint32_t n)
{
    while (n) {
        ssize_t w = send(c->fd, p, n, MSG_NOSIGNAL);
        if (w < 0 && (errno == EAGAIN || errno == EINTR)) {
            pump_once(NULL, 0, 1);
            continue;
        }
        if (w <= 0) return;
        p += w;
        n -= (uint32_t) w;
    }
}

static void cli_poll(cli_t *c)
{
    if (c->fd < 0 || c->closed) return;
    for (;;) {
        ssize_t n = recv(c->fd, c->rb + c->rl, sizeof c->rb - c->rl, MSG_DONTWAIT);
        if (n == 0) {
            c->closed = true;
            break;
        }
        if (n < 0) break;
        c->rl += (uint32_t) n;
    }
    for (;;) {
        static uint8_t reply[8192];
        uint32_t rl = 0;
        uint32_t used = tls_client_feed(&c->tls, c->rb, c->rl, reply, sizeof reply, &rl);
        memmove(c->rb, c->rb + used, c->rl - used);
        c->rl -= used;
        if (rl) cli_raw(c, reply, rl);
        uint8_t app[8192];
        uint32_t got;
        while ((got = tls_client_read(&c->tls, app, sizeof app))) {
            if (c->il + got < sizeof c->in) {
                memcpy(c->in + c->il, app, got);
                c->il += got;
                c->in[c->il] = 0;
            }
        }
        if (!used || !c->rl || tls_client_failed(&c->tls)) break;
    }
}

static cli_t *ALL[8];
static int NALL;

static void pump_once(cli_t **cs, int n, int wait_ms)
{
    fd_set rd;
    FD_ZERO(&rd);
    int mx = zxvd_irc_fds(&rd);
    if (!cs) {
        cs = ALL;
        n = NALL;
    }
    for (int i = 0; i < n; i++)
        if (cs[i]->fd >= 0 && !cs[i]->closed) {
            FD_SET(cs[i]->fd, &rd);
            if (cs[i]->fd > mx) mx = cs[i]->fd;
        }
    struct timeval tv = {0, wait_ms * 1000};
    int r = select(mx + 1, &rd, NULL, NULL, &tv);
    if (r < 0) FD_ZERO(&rd);
    zxvd_irc_service(&rd, 0);
    for (int i = 0; i < n; i++) cli_poll(cs[i]);
}

/* pump until c has received s (searched from *from), or ms pass */
static const char *wait_for(cli_t *c, const char *s, int ms)
{
    for (int t = 0; t < ms; t += 5) {
        const char *p = strstr(c->in, s);
        if (p) return p;
        pump_once(NULL, 0, 5);
        if (c->closed && !strstr(c->in, s)) {
            pump_once(NULL, 0, 0);
            return strstr(c->in, s);
        }
    }
    return strstr(c->in, s);
}

static void settle(int ms)
{
    for (int t = 0; t < ms; t += 5) pump_once(NULL, 0, 5);
}

static bool cli_open(cli_t *c, uint16_t port, const tls_trust_t *trust)
{
    memset(c, 0, sizeof *c);
    c->fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(c->fd, (struct sockaddr *) &a, sizeof a) != 0) return false;
    fcntl(c->fd, F_SETFL, fcntl(c->fd, F_GETFL) | O_NONBLOCK);
    uint8_t r[32], x[32];
    for (int i = 0; i < 32; i++) r[i] = (uint8_t) rand(), x[i] = (uint8_t) rand();
    tls_client_init(&c->tls, "localhost", r, x, TLS_VERIFY_REQUIRED);
    tls_client_set_trust(&c->tls, trust, (int64_t) time(NULL));
    uint8_t hello[2048];
    uint32_t n = tls_client_hello(&c->tls, hello, sizeof hello);
    cli_raw(c, hello, n);
    ALL[NALL++] = c;
    for (int t = 0;
         t < 3000 && !tls_client_established(&c->tls) && !tls_client_failed(&c->tls) && !c->closed;
         t += 5)
        pump_once(NULL, 0, 5);
    return tls_client_established(&c->tls);
}

static void cli_say(cli_t *c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void cli_say(cli_t *c, const char *fmt, ...)
{
    char line[1200];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line - 2, fmt, ap);
    va_end(ap);
    line[n++] = '\r';
    line[n++] = '\n';
    uint8_t rec[2048];
    uint32_t rl = tls_client_send(&c->tls, (const uint8_t *) line, (uint32_t) n, rec, sizeof rec);
    cli_raw(c, rec, rl);
    pump_once(NULL, 0, 1);
}

static void cli_close(cli_t *c)
{
    if (c->fd >= 0) close(c->fd);
    c->fd = -1;
    for (int i = 0; i < NALL; i++)
        if (ALL[i] == c) ALL[i] = ALL[--NALL];
}

static void cli_clear(cli_t *c)
{
    c->il = 0;
    c->in[0] = 0;
}

/* ----- the swarm and the node ----- */

static int ASKS;
static void ask(const char *prompt, char *out, size_t cap, char *truth)
{
    ASKS++;
    snprintf(out, cap, "answer: %s", prompt);
    *truth = 'B';
}

static uint8_t NODE_PK[PQM_MLDSA87_PK_BYTES], NODE_SK[PQM_MLDSA87_SK_BYTES];
static bool NODE_ON;
static bool node(const uint8_t **pk, const uint8_t **sk)
{
    if (!NODE_ON) return false;
    *pk = NODE_PK;
    *sk = NODE_SK;
    return true;
}

/* G5: the record named by the msgid in line, fetched with irc.record and
 * checked: its id, its fields, its signature by pk */
static void check_record(const char *line, const char *target, const char *nick, char truth,
                         const char *text, const uint8_t *pk)
{
    const char *m = strstr(line, "msgid=");
    CHECK(m != NULL, "a msgid tag in %.200s", line);
    if (!m) return;
    char id[IRC_MSGID_LEN + 1];
    memcpy(id, m + 6, IRC_MSGID_LEN);
    id[IRC_MSGID_LEN] = 0;
    const char *js = rpc("irc.record", "{\"msgid\":\"%s\"}", id);
    CHECK(has(js, "\"result\":"), "irc.record %s: %.200s", id, js);
    static uint8_t rec[2048], sig[PQM_MLDSA87_SIG_BYTES + 1], kp[PQM_MLDSA87_PK_BYTES + 1];
    int rl = unhex(jstr(js, "record"), rec, sizeof rec);
    int sl = unhex(jstr(js, "signature"), sig, sizeof sig);
    int kl = unhex(jstr(js, "public_key"), kp, sizeof kp);
    CHECK(rl > 0 && sl == (int) PQM_MLDSA87_SIG_BYTES && kl == (int) PQM_MLDSA87_PK_BYTES,
          "record, signature and key sizes %d %d %d", rl, sl, kl);
    if (rl <= 0 || sl != (int) PQM_MLDSA87_SIG_BYTES || kl != (int) PQM_MLDSA87_PK_BYTES) return;
    CHECK(!memcmp(kp, pk, PQM_MLDSA87_PK_BYTES), "signed by the expected key");
    char want[1200];
    int wl = snprintf(want, sizeof want, "zxv-irc/1\n%s\n%s\n%c\n%s\n%s", target, nick, truth,
                      jstr(js, "ms"), text);
    CHECK(wl == rl && !memcmp(rec, want, (size_t) rl), "the record is %.300s", want);
    static const uint8_t CTX[] = "zxv/irc/v1/msg";
    CHECK(!strcmp(jstr(js, "context"), "zxv/irc/v1/msg"), "the context");
    CHECK(pqm_mldsa87_verify(pk, rec, (size_t) rl, CTX, sizeof CTX - 1, sig),
          "ML-DSA-87 verifies the record");
    rec[rl - 1] ^= 1;
    CHECK(!pqm_mldsa87_verify(pk, rec, (size_t) rl, CTX, sizeof CTX - 1, sig),
          "a changed record does not verify");
    rec[rl - 1] ^= 1;
    static uint8_t both[2048 + PQM_MLDSA87_SIG_BYTES];
    memcpy(both, rec, (size_t) rl);
    memcpy(both + rl, sig, PQM_MLDSA87_SIG_BYTES);
    uint8_t h[32];
    char hh[65];
    sha3_256(both, (size_t) rl + PQM_MLDSA87_SIG_BYTES, h);
    tohex(h, 16, hh);
    CHECK(!strcmp(hh, id), "msgid = SHA3-256(record || signature)[0..16]");
    CHECK(!strcmp(jstr(js, "target"), target) && !strcmp(jstr(js, "nick"), nick),
          "target and nick in irc.record");
    uint8_t sid[32];
    char sidh[65];
    sha3_256(pk, PQM_MLDSA87_PK_BYTES, sid);
    tohex(sid, 32, sidh);
    CHECK(!strcmp(jstr(js, "signer_id"), sidh), "signer_id = SHA3-256(public key)");
}

/* ----- the tests ----- */

static uint8_t GW_PK[PQM_MLDSA87_PK_BYTES];

static void t_off(void)
{
    const char *js = rpc("irc.status", "{}");
    CHECK(has(js, "\"on\":false,\"listening\":false,\"port\":0"), "off by default: %.200s", js);
    js = rpc("irc.bridge", "{\"on\":true}");
    CHECK(has(js, "\"code\":-32012"), "irc.bridge needs the switch: %.200s", js);
    zxvd_irc_service(NULL, 0);
    CHECK(zxvd_irc_port() == 0, "nothing listens while off");
}

static void t_files(char *pass)
{
    static const char *const NAMES[] = {"irc.key", "irc.der", "irc-cert.pem", "irc.pass",
                                        "irc.seed"};
    uint8_t buf[4096];
    for (int i = 0; i < 5; i++) {
        unsigned mode = 0;
        long n = file(NAMES[i], buf, sizeof buf, &mode);
        CHECK(n > 0 && mode == 0600, "%s written 0600 (%ld bytes, mode %o)", NAMES[i], n, mode);
    }
    CHECK(file("irc.pass", (uint8_t *) pass, 32, NULL) == 32, "the password file");
    pass[32] = 0;
    long cl = file("irc.der", CERT, sizeof CERT, NULL);
    CHECK(cl > 0 && cl <= TLS_SELFSIGNED_MAX, "the certificate");
    CERT_LEN = (uint32_t) cl;
    long pl = file("irc-cert.pem", buf, sizeof buf - 1, NULL);
    buf[pl > 0 ? pl : 0] = 0;
    char pem[2048];
    uint32_t want = tls_cert_pem(CERT, CERT_LEN, pem, sizeof pem);
    CHECK(pl == (long) want && !memcmp(buf, pem, want), "the PEM file holds the same certificate");
    uint8_t seed[32];
    CHECK(file("irc.seed", seed, sizeof seed, NULL) == 32, "the seed file");
    static uint8_t sk[PQM_MLDSA87_SK_BYTES];
    pqm_mldsa87_keygen(seed, GW_PK, sk);
    tls_trust_init(&PINNED);
    CHECK(tls_trust_add_der(&PINNED, CERT, CERT_LEN), "pin the certificate");
}

static void t_status(const char *pass, uint16_t port)
{
    const char *js = rpc("irc.status", "{}");
    char want[200];
    snprintf(want, sizeof want, "\"on\":true,\"listening\":true,\"port\":%u,\"clients\":0",
             (unsigned) port);
    CHECK(has(js, want), "status while on: %.300s", js);
    CHECK(!strcmp(jstr(js, "password"), pass), "the password in irc.status");
    uint8_t h[32];
    char fp[100];
    sha256(CERT, CERT_LEN, h);
    for (int i = 0; i < 32; i++) snprintf(fp + 3 * i, 4, "%02X%s", h[i], i < 31 ? ":" : "");
    CHECK(!strcmp(jstr(js, "cert_sha256"), fp), "the certificate's SHA-256 fingerprint");
    char cf[512];
    snprintf(cf, sizeof cf, "%s/irc-cert.pem", DIR_);
    CHECK(!strcmp(jstr(js, "cert_file"), cf), "the certificate file");
    uint8_t id[32];
    char idh[65];
    sha3_256(GW_PK, sizeof GW_PK, id);
    tohex(id, 32, idh);
    CHECK(!strcmp(jstr(js, "signer"), "gateway") && !strcmp(jstr(js, "signer_id"), idh),
          "signed by the gateway key from irc.seed while the mesh is off");
    CHECK(has(js, "\"bridge\":null"), "no bridge");
}

static void t_tls(uint16_t port)
{
    cli_t c;
    /* a client that pins another certificate refuses the gateway */
    CHECK(!cli_open(&c, port, &OTHER), "an unpinned certificate is refused");
    CHECK(tls_client_failed(&c.tls) && c.tls.error == TLS_ERR_BAD_CERT, "as a bad certificate");
    cli_close(&c);
    CHECK(cli_open(&c, port, &PINNED), "TLS with the pinned certificate");
    CHECK(tls_client_is_authenticated(&c.tls), "the gateway is authenticated");
    /* G2: plain text is not IRC here */
    cli_close(&c);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(connect(fd, (struct sockaddr *) &a, sizeof a) == 0, "connect");
    (void) !send(fd, "NICK x\r\nUSER x 0 * :x\r\n", 23, MSG_NOSIGNAL);
    char b[256];
    ssize_t got = 0, n;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    bool shut = false;
    for (int t = 0; t < 2000 && !shut; t += 5) {
        pump_once(NULL, 0, 5);
        while ((n = recv(fd, b + got, sizeof b - 1 - (size_t) got, 0)) > 0) got += n;
        if (n == 0) shut = true;
    }
    b[got] = 0;
    CHECK(shut, "a plain-text client is closed");
    CHECK(!strstr(b, "001") && !strstr(b, "NOTICE"), "and gets no IRC");
    close(fd);
    settle(20);
}

static void t_pass(uint16_t port, const char *pass)
{
    cli_t c;
    CHECK(cli_open(&c, port, &PINNED), "connect");
    cli_say(&c, "PASS 00000000000000000000000000000000");
    cli_say(&c, "NICK mallory");
    cli_say(&c, "USER m 0 * :M");
    CHECK(wait_for(&c, " 464 ", 2000) != NULL, "a wrong password is 464: %.200s", c.in);
    wait_for(&c, "\x01never", 300);
    CHECK(c.closed && !strstr(c.in, " 001 "), "and closed, never welcomed");
    cli_close(&c);
    CHECK(cli_open(&c, port, &PINNED), "connect");
    cli_say(&c, "NICK mallory");
    cli_say(&c, "USER m 0 * :M");
    CHECK(wait_for(&c, " 464 ", 2000) != NULL, "no password is 464 too");
    cli_close(&c);
    (void) pass;
    settle(20);
}

static void login(cli_t *c, uint16_t port, const char *nick, const char *pass, bool tags)
{
    CHECK(cli_open(c, port, &PINNED), "connect %s", nick);
    if (tags) cli_say(c, "CAP LS 302");
    cli_say(c, "PASS %s", pass);
    cli_say(c, "NICK %s", nick);
    cli_say(c, "USER %s 0 * :%s", nick, nick);
    if (tags) {
        cli_say(c, "CAP REQ :message-tags server-time");
        cli_say(c, "CAP END");
    }
    CHECK(wait_for(c, " 001 ", 2000) != NULL, "%s welcomed: %.300s", nick, c->in);
    cli_clear(c);
}

static void t_swarm(cli_t *alice, cli_t *bob)
{
    cli_say(alice, "JOIN #swarm");
    CHECK(wait_for(alice, " 366 ", 2000) != NULL, "joined #swarm");
    CHECK(strstr(alice->in, "swarm") && strstr(alice->in, " 353 "), "the swarm is listed in NAMES");
    cli_say(bob, "JOIN #swarm");
    CHECK(wait_for(bob, " 366 ", 2000) != NULL, "bob joined #swarm");
    cli_clear(alice);
    cli_clear(bob);
    int asks = ASKS;
    cli_say(alice, "PRIVMSG #swarm :[T] what is two and two");
    const char *a = wait_for(alice, ":swarm!", 3000);
    CHECK(a != NULL, "the swarm answers: %.300s", alice->in);
    CHECK(ASKS == asks + 1, "one question to the swarm");
    const char *l = strstr(alice->in, "PRIVMSG #swarm :[B] answer: what is two and two");
    CHECK(l != NULL, "its answer carries its truth tag B: %.300s", alice->in);
    CHECK(strstr(alice->in, "zxv/truth=B") && strstr(alice->in, "time="),
          "and the tags alice asked for");
    if (a) {
        char line[1200];
        const char *s = a;
        while (s > alice->in && s[-1] != '\n') s--;
        size_t n = strcspn(s, "\r\n");
        snprintf(line, sizeof line, "%.*s", (int) n, s);
        check_record(line, "#swarm", "swarm", 'B', "answer: what is two and two", GW_PK);
    }
    CHECK(wait_for(bob, ":alice!", 2000) &&
              strstr(bob->in, "PRIVMSG #swarm :[T] what is two and two"),
          "bob sees alice's question with her tag: %.300s", bob->in);
    CHECK(!strstr(bob->in, "msgid="), "bob asked for no tags and gets none");
    CHECK(wait_for(bob, ":swarm!", 2000) != NULL, "bob sees the answer too");
    /* a message with no tag states nothing: N */
    cli_clear(bob);
    cli_say(alice, "PRIVMSG bob :hello");
    CHECK(wait_for(bob, "PRIVMSG bob :[N] hello", 2000) != NULL, "an untagged message is N");
    /* the node identity signs while the mesh runs */
    NODE_ON = true;
    const char *js = rpc("irc.status", "{}");
    CHECK(!strcmp(jstr(js, "signer"), "node"), "signer is the node while the mesh runs");
    cli_clear(alice);
    cli_say(alice, "PRIVMSG swarm :[F] ping");
    a = wait_for(alice, ":swarm!", 3000);
    CHECK(a && strstr(alice->in, "PRIVMSG alice :[B] answer: ping"),
          "a private question is answered privately: %.300s", alice->in);
    if (a) {
        char line[1200];
        const char *s = a;
        while (s > alice->in && s[-1] != '\n') s--;
        snprintf(line, sizeof line, "%.*s", (int) strcspn(s, "\r\n"), s);
        check_record(line, "alice", "swarm", 'B', "answer: ping", NODE_PK);
    }
    NODE_ON = false;
    js = rpc("irc.record", "{\"msgid\":\"00000000000000000000000000000000\"}");
    CHECK(has(js, "\"code\":-32095"), "an unknown record: %.200s", js);
    js = rpc("irc.record", "{}");
    CHECK(has(js, "\"code\":-32602"), "irc.record needs msgid: %.200s", js);
}

static void t_bridge(cli_t *alice, cli_t *bob, uint16_t port, const char *pass)
{
    const char *js;
    js = rpc("irc.bridge",
             "{\"on\":true,\"host\":\"localhost\",\"port\":%u,\"remote\":\"#far\","
             "\"local\":\"#swarm\",\"nick\":\"brg\"}",
             (unsigned) port);
    CHECK(has(js, "\"code\":-32095") && has(js, "#swarm cannot be bridged"), "#swarm: %.200s", js);
    js = rpc("irc.bridge", "{\"on\":true,\"host\":\"localhost\",\"remote\":\"far\","
                           "\"local\":\"#near\",\"nick\":\"brg\"}");
    CHECK(has(js, "remote must be a channel name"), "remote: %.200s", js);
    js = rpc("irc.bridge", "{\"on\":true,\"host\":\"localhost\",\"remote\":\"#far\","
                           "\"local\":\"#near\",\"nick\":\"9brg\"}");
    CHECK(has(js, "nick is not a valid nickname"), "nick: %.200s", js);
    js = rpc("irc.bridge", "{\"on\":true,\"host\":\"localhost\",\"port\":0,\"remote\":\"#far\","
                           "\"local\":\"#near\",\"nick\":\"brg\"}");
    CHECK(has(js, "\"code\":-32602"), "port 0: %.200s", js);

    /* the built-in roots do not know a self-signed gateway: refused */
    zxvd_irc_bridge_pin(NULL, 0);
    js = rpc("irc.bridge",
             "{\"on\":true,\"host\":\"localhost\",\"port\":%u,\"remote\":\"#far\","
             "\"local\":\"#near\",\"nick\":\"brg\",\"password\":\"%s\"}",
             (unsigned) port, pass);
    CHECK(has(js, "\"state\":\"connecting\""), "the bridge starts: %.300s", js);
    for (int t = 0; t < 3000; t += 5) {
        pump_once(NULL, 0, 5);
        js = rpc("irc.status", "{}");
        if (has(js, "\"state\":\"failed\"")) break;
    }
    CHECK(has(js, "\"state\":\"failed\"") && has(js, "TLS: "),
          "a network whose certificate does not validate is refused: %.400s", js);
    js = rpc("irc.bridge", "{\"on\":false}");
    CHECK(has(js, "\"result\":null"), "off: %.200s", js);

    /* pinned: the gateway bridged to itself, #near <-> #far */
    zxvd_irc_bridge_pin(CERT, CERT_LEN);
    js = rpc("irc.bridge",
             "{\"on\":true,\"host\":\"localhost\",\"port\":%u,\"remote\":\"#far\","
             "\"local\":\"#near\",\"nick\":\"brg\",\"password\":\"%s\"}",
             (unsigned) port, pass);
    CHECK(!has(js, "password") && !has(js, pass), "the password is never shown");
    for (int t = 0; t < 4000; t += 5) {
        pump_once(NULL, 0, 5);
        js = rpc("irc.status", "{}");
        if (has(js, "\"state\":\"joined\"") || has(js, "\"state\":\"failed\"")) break;
    }
    CHECK(has(js, "\"state\":\"joined\""), "the bridge joined: %.400s", js);
    cli_clear(alice);
    cli_clear(bob);
    cli_say(alice, "JOIN #near");
    CHECK(wait_for(alice, " 366 ", 2000) != NULL, "alice in #near");
    CHECK(strstr(alice->in, "Bridged to #far on localhost"), "the topic says it is bridged");
    cli_say(bob, "JOIN #far");
    CHECK(wait_for(bob, " 366 ", 2000) != NULL, "bob in #far");
    CHECK(strstr(bob->in, "brg"), "the bridge is in #far");
    cli_clear(alice);
    cli_clear(bob);
    cli_say(alice, "PRIVMSG #near :[F] hello far");
    CHECK(wait_for(bob, "PRIVMSG #far :", 3000) && strstr(bob->in, "<alice> [F] hello far") &&
              strstr(bob->in, ":brg!"),
          "alice's line goes out as <alice> [F] text: %.300s", bob->in);
    cli_say(bob, "PRIVMSG #far :[T] hi near");
    const char *a = wait_for(alice, "PRIVMSG #near :[T] hi near", 3000);
    CHECK(a && strstr(alice->in, ":bob|bridge!"),
          "bob's line comes in as bob|bridge with its tag: %.300s", alice->in);
    if (a) {
        char line[1200];
        const char *s = a;
        while (s > alice->in && s[-1] != '\n') s--;
        snprintf(line, sizeof line, "%.*s", (int) strcspn(s, "\r\n"), s);
        check_record(line, "#near", "bob|bridge", 'T', "hi near", GW_PK);
    }
    cli_say(bob, "PRIVMSG #far :\001VERSION\001");
    CHECK(wait_for(bob, "only CTCP ACTION crosses", 2000) != NULL, "other CTCP is refused");
    cli_say(bob, "PRIVMSG #far :\001ACTION [T] waves\001");
    CHECK(wait_for(alice, "\001ACTION [T] waves\001", 3000) && !strstr(alice->in, "VERSION"),
          "an ACTION crosses with its tag, other CTCP never: %.300s", alice->in);
    /* the outbound pace: one line per ZXVD_IRC_BRIDGE_MS */
    cli_clear(bob);
    for (int i = 0; i < 3; i++) cli_say(alice, "PRIVMSG #near :line %d", i);
    CHECK(wait_for(bob, "line 0", 3000) && !strstr(bob->in, "line 1"), "paced: %.300s", bob->in);
    js = rpc("irc.status", "{}");
    CHECK(has(js, "\"queued\":2"), "two queued: %.400s", js);
    CHECK(has(js, "\"sent\":2,\"received\":2"), "counted: %.400s", js);
    js = rpc("irc.bridge", "{\"on\":false}");
    CHECK(has(js, "\"result\":null"), "off: %.200s", js);
    CHECK(wait_for(alice, "The bridge was turned off.", 2000) != NULL, "#near is told");
    CHECK(wait_for(bob, ":brg!", 2000) && strstr(bob->in, "QUIT"),
          "the bridge left the network: %.300s", bob->in);
}

static void t_switch_off(cli_t *alice, cli_t *bob)
{
    const char *js = rpc("features.set", "{\"feature\":\"irc\",\"enabled\":false}");
    CHECK(has(js, "\"result\""), "switch off: %.200s", js);
    settle(50);
    CHECK(zxvd_irc_port() == 0, "turning it off stops listening");
    CHECK(alice->closed && bob->closed, "and closes every connection");
    js = rpc("irc.status", "{}");
    CHECK(has(js, "\"on\":false,\"listening\":false"), "status: %.200s", js);
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    srand(7);
    char tmpl[] = "/tmp/zxvd-irc.XXXXXX";
    const char *tmp = getenv("ZXV_TMP");
    if (tmp && strlen(tmp) < 200) {
        snprintf(DIR_, sizeof DIR_, "%s/zxvd-irc.XXXXXX", tmp);
        if (!mkdtemp(DIR_)) snprintf(DIR_, sizeof DIR_, "%s", mkdtemp(tmpl));
    } else {
        snprintf(DIR_, sizeof DIR_, "%s", mkdtemp(tmpl));
    }
    /* a second, unrelated certificate for the refusal */
    {
        uint8_t k[32], ser[16], der[TLS_SELFSIGNED_MAX];
        for (int i = 0; i < 32; i++) k[i] = (uint8_t) (i + 1);
        memset(ser, 3, 16);
        int64_t now = (int64_t) time(NULL);
        uint32_t n =
            tls_selfsigned_p256(k, ser, "other", now - 86400, now + 86400 * 30, der, sizeof der);
        tls_trust_init(&OTHER);
        tls_trust_add_der(&OTHER, der, n);
    }
    uint8_t seed[32];
    memset(seed, 0x5a, 32);
    pqm_mldsa87_keygen(seed, NODE_PK, NODE_SK);

    CHECK(zxvd_rpc_add_group(zxvd_irc_group()) == 0, "add the irc group");
    zxvd_irc_set_ask(ask);
    zxvd_irc_set_node(node);
    CHECK(zxvd_irc_open(DIR_, 0) == 0, "open in %s", DIR_);
    char pass[40];
    t_off();
    t_files(pass);
    CHECK(strlen(pass) == 32 && strspn(pass, "0123456789abcdef") == 32, "32 hex digits");

    CHECK(zxvd_rpc_enable("irc") == 0, "enable irc");
    zxvd_irc_service(NULL, 0);
    uint16_t port = zxvd_irc_port();
    CHECK(port != 0, "listening once on");
    t_status(pass, port);
    t_tls(port);
    t_pass(port, pass);
    static cli_t alice, bob;
    login(&alice, port, "alice", pass, true);
    login(&bob, port, "bob", pass, false);
    t_swarm(&alice, &bob);
    t_bridge(&alice, &bob, port, pass);
    t_switch_off(&alice, &bob);
    cli_close(&alice);
    cli_close(&bob);

    /* G4: a restart keeps the password, the certificate and the gateway key */
    zxvd_irc_close();
    CHECK(zxvd_irc_open(DIR_, 0) == 0, "reopen");
    const char *js = rpc("irc.status", "{}");
    CHECK(!strcmp(jstr(js, "password"), pass), "the same password after a restart");
    uint8_t c2[2048];
    CHECK(file("irc.der", c2, sizeof c2, NULL) == (long) CERT_LEN && !memcmp(c2, CERT, CERT_LEN),
          "the same certificate");
    uint8_t id[32];
    char idh[65];
    sha3_256(GW_PK, sizeof GW_PK, id);
    tohex(id, 32, idh);
    CHECK(!strcmp(jstr(js, "signer_id"), idh), "the same gateway key");
    /* a damaged password file is replaced, never half-used */
    char p[512];
    snprintf(p, sizeof p, "%s/irc.pass", DIR_);
    FILE *f = fopen(p, "wb");
    fputs("not hex!", f);
    fclose(f);
    zxvd_irc_close();
    CHECK(zxvd_irc_open(DIR_, 0) == 0, "reopen with a damaged password file");
    js = rpc("irc.status", "{}");
    CHECK(strlen(jstr(js, "password")) == 32 && strcmp(jstr(js, "password"), pass),
          "a new password");
    /* memory only */
    zxvd_irc_close();
    CHECK(zxvd_irc_open("", 0) == 0, "open in memory");
    js = rpc("irc.status", "{}");
    CHECK(has(js, "\"cert_file\":null"), "no files in memory: %.300s", js);
    zxvd_irc_close();
    js = rpc("irc.status", "{}");
    CHECK(has(js, "\"available\":false"), "closed: %.200s", js);

    const char *const NAMES[] = {"irc.key", "irc.der", "irc-cert.pem", "irc.pass", "irc.seed"};
    for (int i = 0; i < 5; i++) {
        snprintf(p, sizeof p, "%s/%s", DIR_, NAMES[i]);
        unlink(p);
    }
    rmdir(DIR_);
    zxvd_rpc_groups_reset();
    free(OUT.p);
    printf("%s: %d checks, %d failures\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails != 0;
}
