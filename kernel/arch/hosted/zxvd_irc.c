/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_irc.c — zxvd's IRC gateway (zxvd_irc.h G1-G8). */
#if !defined(_WIN32)
#    define _DEFAULT_SOURCE
#    define _DARWIN_C_SOURCE
#endif
#include "zxvd_irc.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)

/* not on Windows yet: the group is listed, irc.status says so */
static void m_status(zxvd_call_t *c)
{
    zxvd_buf_raw(zxvd_res(c), "{\"on\":false,\"listening\":false,\"available\":false}", 48);
}
static void m_unavailable(zxvd_call_t *c)
{
    zxvd_fail(c, ZXVD_E_IRC, "the IRC gateway is not available on Windows yet");
}
static const zxvd_group_method_t METHODS[] = {
    {"irc.status", m_status, true},
    {"irc.record", m_unavailable, true},
    {"irc.bridge", m_unavailable, false},
};
static const zxvd_group_t GROUP = {"irc",
                                   false,
                                   "the IRC gateway on this machine (TLS, localhost)",
                                   METHODS,
                                   sizeof METHODS / sizeof METHODS[0],
                                   NULL};
const zxvd_group_t *zxvd_irc_group(void)
{
    return &GROUP;
}
int zxvd_irc_open(const char *dir, uint16_t port)
{
    (void) dir;
    (void) port;
    return 0;
}
void zxvd_irc_close(void) {}
void zxvd_irc_set_ask(void (*ask)(const char *, char *, size_t, char *))
{
    (void) ask;
}
void zxvd_irc_set_node(bool (*node)(const uint8_t **, const uint8_t **))
{
    (void) node;
}
int zxvd_irc_fds(void *set)
{
    (void) set;
    return -1;
}
void zxvd_irc_service(void *set, uint64_t now_ms)
{
    (void) set;
    (void) now_ms;
}
uint16_t zxvd_irc_port(void)
{
    return 0;
}
void zxvd_irc_set_random(int (*fn)(uint8_t *p, size_t n))
{
    (void) fn;
}
void zxvd_irc_set_clock(uint64_t (*fn)(void))
{
    (void) fn;
}
void zxvd_irc_bridge_pin(const uint8_t *der, uint32_t len)
{
    (void) der;
    (void) len;
}

#else

#    include <arpa/inet.h>
#    include <errno.h>
#    include <fcntl.h>
#    include <netdb.h>
#    include <netinet/in.h>
#    include <netinet/tcp.h>
#    include <stdlib.h>
#    include <sys/select.h>
#    include <sys/socket.h>
#    include <sys/stat.h>
#    include <sys/types.h>
#    include <time.h>
#    include <unistd.h>

#    include "zxv_http_guard.h" /* zxv_os_random */
#    include "zxv_irc.h"
#    include "../../src/mlkem/keccak.h"
#    include "../../src/pqsec/pq_matrix_algs.h"
#    include "../../src/robin_debanks/sha256.h"
#    include "../../src/tls/handshake.h"
#    include "../../src/tls/selfsigned.h"
#    include "../../src/tls/server.h"

#    if defined(MSG_NOSIGNAL)
#        define SEND_FLAGS (MSG_NOSIGNAL | MSG_DONTWAIT)
#    else
#        define SEND_FLAGS MSG_DONTWAIT
#    endif

#    define WBUF      (IRC_OUT_CAP + 4 * TLS_REC_BUF)
#    define SIGN_CTX  "zxv/irc/v1/msg"
#    define REC_MAX   1024u
#    define CERT_DAYS 3650

/* ---------------- state ---------------- */

typedef struct {
    int fd;
    uint64_t start_ms;
    tls_server_t tls;
    uint8_t rbuf[2 * TLS_REC_BUF];
    uint32_t rlen;
    uint8_t wbuf[WBUF];
    uint32_t wlen;
} conn_t;

typedef struct {
    bool used;
    char msgid[IRC_MSGID_LEN + 1];
    uint8_t rec[REC_MAX];
    uint32_t rec_len;
    uint8_t sig[PQM_MLDSA87_SIG_BYTES];
    uint8_t pk[PQM_MLDSA87_PK_BYTES];
    char target[IRC_CHAN_MAX + 2], nick[64], text[IRC_TEXT_MAX + 1];
    char truth;
    uint64_t ms;
} record_t;

enum { BR_OFF = 0, BR_CONNECTING, BR_TLS, BR_REGISTERING, BR_JOINED, BR_FAILED };
static const char *const BR_NAMES[] = {"off",         "connecting", "tls",
                                       "registering", "joined",     "failed"};

typedef struct {
    int state;
    int fd;
    char host[256], remote[IRC_CHAN_MAX + 2], local[IRC_CHAN_MAX + 2], nick[IRC_NICK_MAX + 1];
    uint16_t port;
    char err[200];
    char pass[80]; /* the network's server password, if it has one */
    tls_client_t tls;
    uint8_t rbuf[2 * TLS_REC_BUF];
    uint32_t rlen;
    uint8_t wbuf[4 * TLS_REC_BUF];
    uint32_t wlen;
    char line[1024];
    uint32_t line_len;
    char q[ZXVD_IRC_BRIDGE_Q][512];
    uint32_t qh, qn;
    uint64_t next_ms, start_ms;
    uint32_t sent, received, dropped, nick_tries;
} bridge_t;

static struct {
    bool open;
    char dir[1024];
    uint16_t want_port, port;
    int ls;
    irc_t e;
    conn_t c[IRC_MAX_CLIENTS];
    uint8_t cert[TLS_SELFSIGNED_MAX];
    uint32_t cert_len;
    uint8_t key[32];
    char pass[33];
    char fp[100]; /* SHA-256 fingerprint, AB:CD:... */
    uint8_t gw_pk[PQM_MLDSA87_PK_BYTES], gw_sk[PQM_MLDSA87_SK_BYTES];
    record_t rec[ZXVD_IRC_RECORDS];
    uint32_t rec_next;
    bridge_t br;
} G;

static void (*ASK)(const char *prompt, char *out, size_t cap, char *truth);
static bool (*NODE)(const uint8_t **pk, const uint8_t **sk);
static int (*RANDOM)(uint8_t *p, size_t n);
static uint64_t (*CLOCK)(void);
static const uint8_t *PIN;
static uint32_t PIN_LEN;
static tls_trust_t TRUST;
static bool TRUST_READY;

void zxvd_irc_set_ask(void (*ask)(const char *, char *, size_t, char *))
{
    ASK = ask;
}
void zxvd_irc_set_node(bool (*node)(const uint8_t **, const uint8_t **))
{
    NODE = node;
}
void zxvd_irc_set_random(int (*fn)(uint8_t *p, size_t n))
{
    RANDOM = fn;
}
void zxvd_irc_set_clock(uint64_t (*fn)(void))
{
    CLOCK = fn;
}
void zxvd_irc_bridge_pin(const uint8_t *der, uint32_t len)
{
    PIN = der;
    PIN_LEN = len;
    TRUST_READY = false;
}

static int rnd(uint8_t *p, size_t n)
{
    return RANDOM ? RANDOM(p, n) : zxv_os_random(p, n);
}
static uint64_t wall_ms(void)
{
    if (CLOCK) return CLOCK();
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t) ts.tv_sec * 1000u + (uint64_t) ts.tv_nsec / 1000000u;
}
static uint64_t clock_cb(void *ctx)
{
    (void) ctx;
    return wall_ms();
}
static void hex(const uint8_t *p, size_t n, char *out)
{
    static const char H[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = H[p[i] >> 4];
        out[2 * i + 1] = H[p[i] & 15];
    }
    out[2 * n] = 0;
}
/* copy at most cap - 1 bytes (callers check lengths first) */
static void scpy(char *d, size_t cap, const char *s)
{
    size_t n = strlen(s);
    if (n >= cap) n = cap - 1;
    memcpy(d, s, n);
    d[n] = 0;
}
static void wipe(void *p, size_t n)
{
    volatile uint8_t *v = p;
    while (n--) *v++ = 0;
}

/* ---------------- files (G4) ---------------- */

static bool path_of(char *out, size_t cap, const char *name)
{
    return G.dir[0] && (size_t) snprintf(out, cap, "%s/%s", G.dir, name) < cap;
}

static long read_file(const char *name, uint8_t *buf, size_t cap)
{
    char p[1100];
    if (!path_of(p, sizeof p, name)) return -1;
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, cap, f);
    int more = fgetc(f);
    fclose(f);
    return more == EOF ? (long) n : -1;
}

static int write_file(const char *name, const void *data, size_t len)
{
    char p[1100], t[1120];
    if (!path_of(p, sizeof p, name)) return 0; /* memory only */
    snprintf(t, sizeof t, "%s.tmp", p);
    int fd = open(t, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    const uint8_t *d = data;
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, d + off, len - off);
        if (w <= 0) {
            close(fd);
            unlink(t);
            return -1;
        }
        off += (size_t) w;
    }
    if (fsync(fd) != 0 || close(fd) != 0 || rename(t, p) != 0) {
        unlink(t);
        return -1;
    }
    return 0;
}

/* G2/G4: the certificate and its key, reused while they hold together and
 * have more than 30 days left */
static int load_cert(void)
{
    uint8_t pub[65];
    long kl = read_file("irc.key", G.key, sizeof G.key);
    long cl = read_file("irc.der", G.cert, sizeof G.cert);
    int64_t now = (int64_t) (wall_ms() / 1000u);
    if (kl == 32 && cl > 0 && ecdsa_p256_public(G.key, pub)) {
        x509_cert_t x;
        if (x509_parse(&x, G.cert, (uint32_t) cl) == X509_OK && x.raw.len == (uint32_t) cl &&
            x.key.type == X509_KEY_EC && x.key.point_len == 65 && !memcmp(x.key.point, pub, 65) &&
            x.not_after > now + 30 * 86400LL) {
            G.cert_len = (uint32_t) cl;
            return 0;
        }
    }
    uint8_t ser[16];
    for (int tries = 0; tries < 8; tries++) {
        if (rnd(G.key, 32) != 0 || rnd(ser, 16) != 0) return -1;
        if (ecdsa_p256_public(G.key, pub)) break;
    }
    G.cert_len = tls_selfsigned_p256(G.key, ser, "zxvd IRC gateway", now - 86400,
                                     now + CERT_DAYS * 86400LL, G.cert, sizeof G.cert);
    if (!G.cert_len) return -1;
    char pem[2048];
    uint32_t pl = tls_cert_pem(G.cert, G.cert_len, pem, sizeof pem);
    if (write_file("irc.key", G.key, 32) != 0 || write_file("irc.der", G.cert, G.cert_len) != 0 ||
        write_file("irc-cert.pem", pem, pl) != 0)
        return -1;
    return 0;
}

static int load_secrets(void)
{
    uint8_t buf[64];
    long n = read_file("irc.pass", buf, sizeof buf);
    bool ok = n == 32;
    for (long i = 0; ok && i < n; i++)
        ok = (buf[i] >= '0' && buf[i] <= '9') || (buf[i] >= 'a' && buf[i] <= 'f');
    if (ok) {
        memcpy(G.pass, buf, 32);
        G.pass[32] = 0;
    } else {
        uint8_t r[16];
        if (rnd(r, 16) != 0) return -1;
        hex(r, 16, G.pass);
        if (write_file("irc.pass", G.pass, 32) != 0) return -1;
    }
    uint8_t seed[32];
    if (read_file("irc.seed", seed, sizeof seed) != 32) {
        if (rnd(seed, 32) != 0 || write_file("irc.seed", seed, 32) != 0) return -1;
    }
    pqm_mldsa87_keygen(seed, G.gw_pk, G.gw_sk);
    wipe(seed, sizeof seed);
    uint8_t h[32];
    sha256(G.cert, G.cert_len, h);
    for (int i = 0; i < 32; i++) snprintf(G.fp + 3 * i, 4, "%02X%s", h[i], i < 31 ? ":" : "");
    return 0;
}

/* ---------------- signing (G5) ---------------- */

static void signer(const uint8_t **pk, const uint8_t **sk, const char **kind)
{
    if (NODE && NODE(pk, sk)) {
        *kind = "node";
        return;
    }
    *pk = G.gw_pk;
    *sk = G.gw_sk;
    *kind = "gateway";
}

static bool sign_cb(void *ctx, const irc_msg_t *m, char *msgid)
{
    (void) ctx;
    record_t *r = &G.rec[G.rec_next];
    int n = snprintf((char *) r->rec, sizeof r->rec, "zxv-irc/1\n%s\n%s\n%c\n%llu\n%s", m->target,
                     m->nick, m->truth, (unsigned long long) m->ms, m->text);
    if (n < 0 || (size_t) n >= sizeof r->rec) return false;
    const uint8_t *pk, *sk;
    const char *kind;
    signer(&pk, &sk, &kind);
    uint8_t rn[32];
    if (rnd(rn, 32) != 0) return false;
    pqm_mldsa87_sign(sk, r->rec, (size_t) n, (const uint8_t *) SIGN_CTX, sizeof SIGN_CTX - 1, rn,
                     r->sig);
    static uint8_t both[REC_MAX + PQM_MLDSA87_SIG_BYTES];
    memcpy(both, r->rec, (size_t) n);
    memcpy(both + n, r->sig, PQM_MLDSA87_SIG_BYTES);
    uint8_t h[32];
    sha3_256(both, (size_t) n + PQM_MLDSA87_SIG_BYTES, h);
    hex(h, IRC_MSGID_LEN / 2, msgid);
    r->used = true;
    r->rec_len = (uint32_t) n;
    memcpy(r->pk, pk, PQM_MLDSA87_PK_BYTES);
    memcpy(r->msgid, msgid, IRC_MSGID_LEN + 1);
    snprintf(r->target, sizeof r->target, "%s", m->target);
    snprintf(r->nick, sizeof r->nick, "%s", m->nick);
    snprintf(r->text, sizeof r->text, "%s", m->text);
    r->truth = m->truth;
    r->ms = m->ms;
    G.rec_next = (G.rec_next + 1) % ZXVD_IRC_RECORDS;
    return true;
}

/* ---------------- the swarm (G6) and the bridge's way out (G8) ---------------- */

static void serve_cb(void *ctx, irc_t *s, const irc_msg_t *m)
{
    (void) ctx;
    char ans[1400], truth = 'N';
    const char *to = m->target[0] == '#' ? m->target : m->nick;
    if (strcmp(m->target, "#swarm") != 0 && strcmp(m->target, "swarm") != 0) return;
    if (!ASK) {
        irc_say(s, to, "swarm", 'N', "The swarm is not connected to this gateway.");
        return;
    }
    ans[0] = 0;
    ASK(m->text, ans, sizeof ans, &truth);
    if (!strchr("TFBN", truth) || !truth) truth = 'N';
    irc_say(s, to, "swarm", truth, ans[0] ? ans : "(no answer)");
}

static bool ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a >= 'A' && *a <= 'Z' ? (char) (*a + 32) : *a;
        char y = *b >= 'A' && *b <= 'Z' ? (char) (*b + 32) : *b;
        if (x != y) return false;
    }
    return *a == *b;
}

static void relay_cb(void *ctx, irc_t *s, const irc_msg_t *m)
{
    (void) ctx;
    (void) s;
    bridge_t *b = &G.br;
    if (b->state != BR_JOINED || !ieq(m->target, b->local)) return;
    if (strchr(m->nick, '|') || !strcmp(m->nick, "bridge")) return; /* from the bridge itself */
    if (b->qn == ZXVD_IRC_BRIDGE_Q) {
        b->dropped++;
        return;
    }
    char *q = b->q[(b->qh + b->qn) % ZXVD_IRC_BRIDGE_Q];
    if (!strncmp(m->text, "\001ACTION ", 8))
        snprintf(q, 512, "PRIVMSG %s :\001ACTION <%s> [%c] %s", b->remote, m->nick, m->truth,
                 m->text + 8);
    else
        snprintf(q, 512, "PRIVMSG %s :<%s> [%c] %s", b->remote, m->nick, m->truth, m->text);
    b->qn++;
}

static const irc_hooks_t HOOKS = {NULL, sign_cb, serve_cb, relay_cb, clock_cb};

/* ---------------- connections (G1, G2) ---------------- */

static void conn_close(int i, const char *why)
{
    conn_t *c = &G.c[i];
    if (c->fd < 0) return;
    close(c->fd);
    c->fd = -1;
    irc_drop(&G.e, i, why);
    wipe(&c->tls, sizeof c->tls);
    c->rlen = c->wlen = 0;
}

static void flush(conn_t *c)
{
    while (c->wlen) {
        ssize_t w = send(c->fd, c->wbuf, c->wlen, SEND_FLAGS);
        if (w <= 0) return; /* EAGAIN: next time; an error shows up on read */
        memmove(c->wbuf, c->wbuf + w, c->wlen - (uint32_t) w);
        c->wlen -= (uint32_t) w;
    }
}

static void conn_read(int i)
{
    conn_t *c = &G.c[i];
    ssize_t n = recv(c->fd, c->rbuf + c->rlen, sizeof c->rbuf - c->rlen, MSG_DONTWAIT);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        conn_close(i, "Connection closed");
        return;
    }
    if (n < 0) return;
    c->rlen += (uint32_t) n;
    for (;;) {
        static uint8_t reply[4096];
        uint32_t rl = 0;
        uint32_t used = tls_server_feed(&c->tls, c->rbuf, c->rlen, reply, sizeof reply, &rl);
        memmove(c->rbuf, c->rbuf + used, c->rlen - used);
        c->rlen -= used;
        if (rl && rl <= sizeof c->wbuf - c->wlen) {
            memcpy(c->wbuf + c->wlen, reply, rl);
            c->wlen += rl;
        }
        if (tls_server_failed(&c->tls)) {
            flush(c);
            conn_close(i, "TLS failed");
            return;
        }
        char app[4096];
        uint32_t got, total = 0;
        while ((got = tls_server_read(&c->tls, (uint8_t *) app, sizeof app))) {
            irc_input(&G.e, i, app, got);
            total += got;
        }
        if (c->tls.peer_closed) {
            conn_close(i, "Connection closed");
            return;
        }
        if (!used && !total) break;
        if (!c->rlen) break;
    }
    if (c->rlen == sizeof c->rbuf) conn_close(i, "TLS record too large");
}

/* engine lines -> records -> the socket */
static void conn_pump(int i, uint64_t now)
{
    conn_t *c = &G.c[i];
    if (c->fd < 0) return;
    if (!tls_server_established(&c->tls)) {
        flush(c);
        if (now - c->start_ms > ZXVD_IRC_HS_MS) conn_close(i, "TLS handshake timeout");
        return;
    }
    char plain[TLS_MAX_PLAINTEXT];
    while (sizeof c->wbuf - c->wlen >= TLS_REC_BUF) {
        uint32_t n = irc_output(&G.e, i, plain, sizeof plain);
        if (!n) break;
        c->wlen += tls_server_send(&c->tls, (const uint8_t *) plain, n, c->wbuf + c->wlen,
                                   (uint32_t) (sizeof c->wbuf - c->wlen));
    }
    flush(c);
    if (irc_closing(&G.e, i) && c->wlen == 0) {
        char probe[1];
        if (irc_output(&G.e, i, probe, 0) == 0) {
            c->wlen = tls_server_close(&c->tls, c->wbuf, sizeof c->wbuf);
            flush(c);
            conn_close(i, NULL);
        }
    }
}

static const char *listen_on(uint16_t port)
{
    static char why[160];
    if (G.ls >= 0) return NULL;
    int fd = socket(AF_INET, SOCK_STREAM, 0), yes = 1;
    if (fd < 0) return "no socket";
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* G1: this machine only */
    if (bind(fd, (struct sockaddr *) &a, sizeof a) != 0 || listen(fd, 8) != 0) {
        snprintf(why, sizeof why, "cannot listen on 127.0.0.1:%u: %s", (unsigned) port,
                 strerror(errno));
        close(fd);
        return why;
    }
    socklen_t al = sizeof a;
    getsockname(fd, (struct sockaddr *) &a, &al);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    G.ls = fd;
    G.port = ntohs(a.sin_port);
    return NULL;
}

static void bridge_stop(const char *why);

static void unlisten(void)
{
    for (uint32_t i = 0; i < IRC_MAX_CLIENTS; i++) conn_close((int) i, "Gateway closed");
    if (G.ls >= 0) close(G.ls);
    G.ls = -1;
    G.port = 0;
    bridge_stop(NULL);
}

static void accept_one(uint64_t now)
{
    struct sockaddr_in from;
    socklen_t fl = sizeof from;
    int fd = accept(G.ls, (struct sockaddr *) &from, &fl);
    if (fd < 0) return;
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    if (from.sin_family != AF_INET || ntohl(from.sin_addr.s_addr) != INADDR_LOOPBACK) {
        close(fd); /* G1 */
        return;
    }
    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof yes);
#    if defined(SO_NOSIGPIPE)
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof yes);
#    endif
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    int id = irc_open(&G.e, now);
    if (id < 0) {
        close(fd); /* E1: full */
        return;
    }
    conn_t *c = &G.c[id];
    uint8_t r[32], x[32];
    if (rnd(r, 32) != 0 || rnd(x, 32) != 0 ||
        !tls_server_init(&c->tls, G.cert, G.cert_len, G.key, r, x)) {
        close(fd);
        irc_drop(&G.e, id, NULL);
        return;
    }
    wipe(x, sizeof x);
    c->fd = fd;
    c->start_ms = now;
    c->rlen = c->wlen = 0;
}

/* ---------------- the bridge (G8) ---------------- */

static void br_fail(const char *fmt, const char *arg)
{
    bridge_t *b = &G.br;
    snprintf(b->err, sizeof b->err, fmt, arg ? arg : "");
    if (b->fd >= 0) close(b->fd);
    b->fd = -1;
    b->state = BR_FAILED;
    irc_say(&G.e, b->local, "bridge", 'N', b->err);
}

static void bridge_stop(const char *why)
{
    bridge_t *b = &G.br;
    if (b->state == BR_OFF) return;
    if (b->fd >= 0) {
        if (tls_client_established(&b->tls)) {
            uint8_t out[512];
            const char *q = "QUIT :bridge closed\r\n";
            uint32_t n = tls_client_send(&b->tls, (const uint8_t *) q, (uint32_t) strlen(q), out,
                                         sizeof out);
            if (n) (void) !send(b->fd, out, n, SEND_FLAGS);
        }
        close(b->fd);
    }
    if (why) irc_say(&G.e, b->local, "bridge", 'N', why);
    irc_unreserve(&G.e, b->local);
    wipe(&b->tls, sizeof b->tls);
    memset(b, 0, sizeof *b);
    b->fd = -1;
}

static void br_send(const char *line)
{
    bridge_t *b = &G.br;
    char l[600];
    int n = snprintf(l, sizeof l, "%s\r\n", line);
    if (n <= 0 || (size_t) n >= sizeof l) return;
    if (sizeof b->wbuf - b->wlen < TLS_REC_BUF) return;
    b->wlen += tls_client_send(&b->tls, (const uint8_t *) l, (uint32_t) n, b->wbuf + b->wlen,
                               (uint32_t) (sizeof b->wbuf - b->wlen));
}

static void br_flush(void)
{
    bridge_t *b = &G.br;
    while (b->wlen && b->fd >= 0) {
        ssize_t w = send(b->fd, b->wbuf, b->wlen, SEND_FLAGS);
        if (w <= 0) return;
        memmove(b->wbuf, b->wbuf + w, b->wlen - (uint32_t) w);
        b->wlen -= (uint32_t) w;
    }
}

/* keep nick characters only, at most 23, then "|bridge" */
static void remote_nick(const char *pfx, char *out)
{
    size_t k = 0;
    for (const char *p = pfx; *p && *p != '!' && *p != '@' && k < 23; p++)
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
            strchr("[]\\`_^{}-", *p))
            out[k++] = *p;
    if (!k) out[k++] = 'x';
    out[k] = 0;
    strcat(out, "|bridge");
}

static void br_line(char *l)
{
    bridge_t *b = &G.br;
    char *pfx = NULL;
    if (*l == '@') { /* tags from the network: read past */
        char *sp = strchr(l, ' ');
        if (!sp) return;
        l = sp + 1;
    }
    if (*l == ':') {
        pfx = l + 1;
        char *sp = strchr(l, ' ');
        if (!sp) return;
        *sp = 0;
        l = sp + 1;
    }
    char *rest = strchr(l, ' ');
    if (rest) *rest++ = 0;
    if (!strcmp(l, "PING")) {
        char out[540];
        snprintf(out, sizeof out, "PONG %s", rest ? rest : ":x");
        br_send(out);
    } else if (!strcmp(l, "001")) {
        b->state = BR_REGISTERING;
        char out[80];
        snprintf(out, sizeof out, "JOIN %s", b->remote);
        br_send(out);
    } else if (!strcmp(l, "433") || !strcmp(l, "432")) {
        if (++b->nick_tries > 3 || strlen(b->nick) >= IRC_NICK_MAX) {
            br_fail("the network refused the nick %s", b->nick);
            return;
        }
        strcat(b->nick, "_");
        char out[64];
        snprintf(out, sizeof out, "NICK %s", b->nick);
        br_send(out);
    } else if (!strcmp(l, "JOIN") && pfx && b->state == BR_REGISTERING) {
        size_t nl = strlen(b->nick);
        if (!strncmp(pfx, b->nick, nl) && pfx[nl] == '!') {
            b->state = BR_JOINED;
            char msg[400];
            snprintf(msg, sizeof msg, "Bridged to %s on %s", b->remote, b->host);
            irc_say(&G.e, b->local, "bridge", 'N', msg);
        }
    } else if ((!strcmp(l, "PRIVMSG") || !strcmp(l, "NOTICE")) && pfx && rest &&
               b->state == BR_JOINED) {
        char *text = strstr(rest, " :");
        if (!text) return;
        *text = 0;
        text += 2;
        if (!ieq(rest, b->remote)) return; /* only the bridged channel */
        char from[IRC_NICK_MAX + 1];
        remote_nick(pfx, from);
        const char *body = text;
        char truth, act[600];
        if (!strncmp(text, "\001ACTION ", 8)) {
            truth = irc_truth_of(text + 8, &body);
            snprintf(act, sizeof act, "\001ACTION %s", body);
            body = act;
        } else if (text[0] == '\001') {
            return; /* other CTCP never crosses */
        } else {
            truth = irc_truth_of(text, &body);
        }
        if (*body) {
            b->received++;
            irc_say(&G.e, b->local, from, truth, body);
        }
    } else if (!strcmp(l, "ERROR")) {
        br_fail("the network closed the link: %s", rest ? rest : "");
    }
}

static void br_read(uint64_t now)
{
    bridge_t *b = &G.br;
    ssize_t n = recv(b->fd, b->rbuf + b->rlen, sizeof b->rbuf - b->rlen, MSG_DONTWAIT);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        br_fail("the network closed the connection%s", NULL);
        return;
    }
    if (n < 0) return;
    b->rlen += (uint32_t) n;
    for (;;) {
        static uint8_t reply[4096];
        uint32_t rl = 0;
        uint32_t used = tls_client_feed(&b->tls, b->rbuf, b->rlen, reply, sizeof reply, &rl);
        memmove(b->rbuf, b->rbuf + used, b->rlen - used);
        b->rlen -= used;
        if (rl && rl <= sizeof b->wbuf - b->wlen) {
            memcpy(b->wbuf + b->wlen, reply, rl);
            b->wlen += rl;
        }
        if (tls_client_failed(&b->tls)) {
            br_fail("TLS: %s", b->tls.error == TLS_ERR_BAD_CERT
                                   ? tls_cert_status_str(b->tls.cert_status)
                                   : tls_error_str(b->tls.error));
            return;
        }
        if (b->state == BR_TLS && tls_client_established(&b->tls)) {
            b->state = BR_REGISTERING;
            char out[160];
            if (b->pass[0]) {
                snprintf(out, sizeof out, "PASS %s", b->pass);
                br_send(out);
                wipe(b->pass, sizeof b->pass);
            }
            snprintf(out, sizeof out, "NICK %s", b->nick);
            br_send(out);
            snprintf(out, sizeof out, "USER %s 0 * :ZXV gateway bridge", b->nick);
            br_send(out);
        }
        char app[4096];
        uint32_t got, total = 0;
        while ((got = tls_client_read(&b->tls, (uint8_t *) app, sizeof app))) {
            total += got;
            for (uint32_t i = 0; i < got; i++) {
                if (app[i] == '\n') {
                    if (b->line_len && b->line[b->line_len - 1] == '\r') b->line_len--;
                    b->line[b->line_len] = 0;
                    b->line_len = 0;
                    br_line(b->line);
                    if (b->state == BR_FAILED || b->state == BR_OFF) return;
                } else if (b->line_len < sizeof b->line - 1) {
                    b->line[b->line_len++] = app[i];
                }
            }
        }
        if (!used && !total) break;
        if (!b->rlen) break;
    }
    (void) now;
}

static void br_tick(uint64_t now)
{
    bridge_t *b = &G.br;
    if (b->fd < 0) return;
    if (b->state == BR_CONNECTING) {
        fd_set w;
        FD_ZERO(&w);
        FD_SET(b->fd, &w);
        struct timeval tv = {0, 0};
        if (select(b->fd + 1, NULL, &w, NULL, &tv) > 0) {
            int err = 0;
            socklen_t el = sizeof err;
            getsockopt(b->fd, SOL_SOCKET, SO_ERROR, &err, &el);
            if (err) {
                br_fail("cannot connect: %s", strerror(err));
                return;
            }
            b->state = BR_TLS;
            b->wlen = tls_client_hello(&b->tls, b->wbuf, sizeof b->wbuf);
        } else if (now - b->start_ms > ZXVD_IRC_HS_MS) {
            br_fail("cannot connect: timed out%s", NULL);
            return;
        }
    }
    if (b->state == BR_TLS && now - b->start_ms > 3 * ZXVD_IRC_HS_MS) {
        br_fail("TLS handshake timed out%s", NULL);
        return;
    }
    if (b->state == BR_JOINED && b->qn && now >= b->next_ms) {
        br_send(b->q[b->qh]);
        b->qh = (b->qh + 1) % ZXVD_IRC_BRIDGE_Q;
        b->qn--;
        b->sent++;
        b->next_ms = now + ZXVD_IRC_BRIDGE_MS;
    }
    br_flush();
}

static const char *bridge_start(const char *host, uint16_t port, const char *remote,
                                const char *local, const char *nick, const char *pass, uint64_t now)
{
    bridge_t *b = &G.br;
    if (G.ls < 0) return "the gateway is not running (turn the irc feature on)";
    if (b->state != BR_OFF) bridge_stop("The bridge was replaced.");
    if (!host[0] || strlen(host) >= sizeof b->host) return "host must be a host name";
    if (remote[0] != '#' || strlen(remote) > IRC_CHAN_MAX + 1 || strpbrk(remote, " ,\a:"))
        return "remote must be a channel name";
    if (strlen(local) > IRC_CHAN_MAX + 1) return "local must be a channel name not in use";
    if (!strcmp(local, "#swarm")) return "#swarm cannot be bridged";
    if (!irc_nick_valid(nick)) return "nick is not a valid nickname";
    if (strlen(pass) >= sizeof b->pass || strpbrk(pass, " \r\n"))
        return "password must be one word";
    memset(b, 0, sizeof *b);
    b->fd = -1;
    scpy(b->host, sizeof b->host, host);
    scpy(b->remote, sizeof b->remote, remote);
    scpy(b->local, sizeof b->local, local);
    scpy(b->nick, sizeof b->nick, nick);
    scpy(b->pass, sizeof b->pass, pass);
    b->port = port;
    char topic[IRC_TOPIC_MAX];
    snprintf(topic, sizeof topic, "Bridged to %.51s on %.120s (public; off when zxvd stops)",
             remote, host);
    if (irc_reserve(&G.e, local, "bridge", topic) != 0)
        return "local must be a channel name not in use";
    if (!TRUST_READY) {
        tls_trust_init(&TRUST);
        if (PIN)
            tls_trust_add_der(&TRUST, PIN, PIN_LEN);
        else
            tls_trust_add_builtin(&TRUST);
        TRUST_READY = true;
    }
    char ps[8];
    snprintf(ps, sizeof ps, "%u", (unsigned) port);
    struct addrinfo hints, *ai = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    static char why[200];
    int gai = getaddrinfo(host, ps, &hints, &ai);
    if (gai != 0 || !ai) {
        snprintf(why, sizeof why, "cannot resolve %.100s: %s", host, gai_strerror(gai));
        irc_unreserve(&G.e, local);
        memset(b, 0, sizeof *b);
        b->fd = -1;
        return why;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) {
        fcntl(fd, F_SETFD, FD_CLOEXEC);
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        int yes = 1;
#    if defined(SO_NOSIGPIPE)
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof yes);
#    endif
        (void) yes;
    }
    if (fd < 0 || (connect(fd, ai->ai_addr, ai->ai_addrlen) != 0 && errno != EINPROGRESS)) {
        snprintf(why, sizeof why, "cannot connect to %.100s: %s", host, strerror(errno));
        if (fd >= 0) close(fd);
        freeaddrinfo(ai);
        irc_unreserve(&G.e, local);
        memset(b, 0, sizeof *b);
        b->fd = -1;
        return why;
    }
    freeaddrinfo(ai);
    uint8_t r[32], x[32];
    if (rnd(r, 32) != 0 || rnd(x, 32) != 0) {
        close(fd);
        irc_unreserve(&G.e, local);
        memset(b, 0, sizeof *b);
        b->fd = -1;
        return "no random source";
    }
    tls_client_init(&b->tls, host, r, x, TLS_VERIFY_REQUIRED);
    tls_client_set_trust(&b->tls, &TRUST, (int64_t) (now / 1000u));
    wipe(x, sizeof x);
    b->fd = fd;
    b->state = BR_CONNECTING;
    b->start_ms = now;
    return NULL;
}

/* ---------------- open, close, the loop ---------------- */

int zxvd_irc_open(const char *dir, uint16_t port)
{
    if (G.open) zxvd_irc_close();
    memset(&G, 0, sizeof G);
    G.ls = -1;
    G.br.fd = -1;
    for (uint32_t i = 0; i < IRC_MAX_CLIENTS; i++) G.c[i].fd = -1;
    if (dir && strlen(dir) >= sizeof G.dir) {
        fprintf(stderr, "zxvd: irc: folder name too long\n");
        return -1;
    }
    snprintf(G.dir, sizeof G.dir, "%s", dir ? dir : "");
    G.want_port = port;
    if (load_cert() != 0 || load_secrets() != 0) {
        fprintf(stderr, "zxvd: irc: the gateway's key, certificate or password could not be "
                        "made or saved\n");
        wipe(&G, sizeof G);
        G.ls = -1;
        return -1;
    }
    irc_init(&G.e, "zxvd.local", G.pass, &HOOKS);
    irc_reserve(&G.e, "#swarm", "swarm", "Ask the swarm here; its answers are advisory");
    G.open = true;
    return 0;
}

void zxvd_irc_close(void)
{
    if (!G.open) return;
    unlisten();
    wipe(G.key, sizeof G.key);
    wipe(G.gw_sk, sizeof G.gw_sk);
    wipe(G.pass, sizeof G.pass);
    G.open = false;
}

uint16_t zxvd_irc_port(void)
{
    return G.ls >= 0 ? G.port : 0;
}

int zxvd_irc_fds(void *set)
{
    fd_set *s = set;
    if (!G.open || G.ls < 0) return -1;
    int mx = G.ls;
    FD_SET(G.ls, s);
    for (uint32_t i = 0; i < IRC_MAX_CLIENTS; i++)
        if (G.c[i].fd >= 0) {
            FD_SET(G.c[i].fd, s);
            if (G.c[i].fd > mx) mx = G.c[i].fd;
        }
    if (G.br.fd >= 0) {
        FD_SET(G.br.fd, s);
        if (G.br.fd > mx) mx = G.br.fd;
    }
    return mx;
}

void zxvd_irc_service(void *set, uint64_t now_ms)
{
    fd_set *s = set;
    (void) now_ms;
    if (!G.open) return;
    uint64_t now = wall_ms();
    bool on = zxvd_rpc_enabled("irc");
    if (!on && G.ls >= 0) unlisten(); /* G1 */
    if (on && G.ls < 0) {
        const char *why = listen_on(G.want_port);
        if (why) {
            static char last[160];
            if (strcmp(last, why)) fprintf(stderr, "zxvd: irc: %s\n", why);
            snprintf(last, sizeof last, "%s", why);
            return;
        }
    }
    if (G.ls < 0) return;
    for (uint32_t i = 0; i < IRC_MAX_CLIENTS; i++)
        if (G.c[i].fd >= 0 && s && FD_ISSET(G.c[i].fd, s)) conn_read((int) i);
    if (G.br.fd >= 0 && s && FD_ISSET(G.br.fd, s) && G.br.state >= BR_TLS &&
        G.br.state <= BR_JOINED)
        br_read(now);
    br_tick(now);
    irc_tick(&G.e, now);
    for (uint32_t i = 0; i < IRC_MAX_CLIENTS; i++) conn_pump((int) i, now);
    if (s && FD_ISSET(G.ls, s)) accept_one(now);
}

/* ---------------- the methods ---------------- */

static void bridge_json(zxvd_buf_t *o)
{
    bridge_t *b = &G.br;
    if (b->state == BR_OFF) {
        zxvd_buf_raw(o, "null", 4);
        return;
    }
    zxvd_buf_printf(o, "{\"state\":\"%s\",\"host\":", BR_NAMES[b->state]);
    zxvd_buf_json_str(o, b->host);
    zxvd_buf_printf(o, ",\"port\":%u,\"remote\":", (unsigned) b->port);
    zxvd_buf_json_str(o, b->remote);
    zxvd_buf_raw(o, ",\"local\":", 9);
    zxvd_buf_json_str(o, b->local);
    zxvd_buf_raw(o, ",\"nick\":", 8);
    zxvd_buf_json_str(o, b->nick);
    zxvd_buf_printf(o,
                    ",\"sent\":%u,\"received\":%u,\"dropped\":%u,\"queued\":%u,\"error\":", b->sent,
                    b->received, b->dropped, b->qn);
    if (b->err[0])
        zxvd_buf_json_str(o, b->err);
    else
        zxvd_buf_raw(o, "null", 4);
    zxvd_buf_raw(o, "}", 1);
}

static void m_status(zxvd_call_t *c)
{
    zxvd_buf_t *o = zxvd_res(c);
    if (!G.open) {
        zxvd_buf_raw(o, "{\"on\":false,\"listening\":false,\"available\":false}", 48);
        return;
    }
    const uint8_t *pk, *sk;
    const char *kind;
    signer(&pk, &sk, &kind);
    uint8_t id[32];
    char idh[65], path[1100];
    sha3_256(pk, PQM_MLDSA87_PK_BYTES, id);
    hex(id, 32, idh);
    zxvd_buf_printf(o, "{\"on\":%s,\"listening\":%s,\"port\":%u,\"clients\":%u,\"password\":",
                    zxvd_rpc_enabled("irc") ? "true" : "false", G.ls >= 0 ? "true" : "false",
                    (unsigned) zxvd_irc_port(), irc_clients(&G.e));
    zxvd_buf_json_str(o, G.pass);
    zxvd_buf_raw(o, ",\"cert_sha256\":", 15);
    zxvd_buf_json_str(o, G.fp);
    zxvd_buf_raw(o, ",\"cert_file\":", 13);
    if (path_of(path, sizeof path, "irc-cert.pem"))
        zxvd_buf_json_str(o, path);
    else
        zxvd_buf_raw(o, "null", 4);
    zxvd_buf_printf(o,
                    ",\"signer\":\"%s\",\"signer_id\":\"%s\",\"delivered\":%llu,\"refused\":%llu,"
                    "\"bridge\":",
                    kind, idh, (unsigned long long) G.e.delivered,
                    (unsigned long long) G.e.refused);
    bridge_json(o);
    zxvd_buf_raw(o, "}", 1);
}

static void m_record(zxvd_call_t *c)
{
    char want[IRC_MSGID_LEN + 8];
    if (!zxvd_p_str(c, "msgid", want, sizeof want, true)) return;
    for (uint32_t i = 0; i < ZXVD_IRC_RECORDS; i++) {
        record_t *r = &G.rec[i];
        if (!G.open || !r->used || strcmp(r->msgid, want)) continue;
        zxvd_buf_t *o = zxvd_res(c);
        uint8_t id[32];
        char idh[65];
        sha3_256(r->pk, PQM_MLDSA87_PK_BYTES, id);
        hex(id, 32, idh);
        zxvd_buf_raw(o, "{\"msgid\":", 9);
        zxvd_buf_json_str(o, r->msgid);
        zxvd_buf_raw(o, ",\"target\":", 10);
        zxvd_buf_json_str(o, r->target);
        zxvd_buf_raw(o, ",\"nick\":", 8);
        zxvd_buf_json_str(o, r->nick);
        zxvd_buf_printf(o, ",\"truth\":\"%c\",\"ms\":\"%llu\",\"text\":", r->truth,
                        (unsigned long long) r->ms);
        zxvd_buf_json_str(o, r->text);
        zxvd_buf_printf(o, ",\"signer_id\":\"%s\",\"context\":\"%s\",\"public_key\":", idh,
                        SIGN_CTX);
        zxvd_buf_hex(o, r->pk, PQM_MLDSA87_PK_BYTES);
        zxvd_buf_raw(o, ",\"signature\":", 13);
        zxvd_buf_hex(o, r->sig, PQM_MLDSA87_SIG_BYTES);
        zxvd_buf_raw(o, ",\"record\":", 10);
        zxvd_buf_hex(o, r->rec, r->rec_len);
        zxvd_buf_raw(o, "}", 1);
        return;
    }
    zxvd_fail(c, ZXVD_E_IRC, "no such record (only the newest %u are kept)", ZXVD_IRC_RECORDS);
}

static void m_bridge(zxvd_call_t *c)
{
    bool on = false;
    char host[256], remote[64], local[64], nick[40], pass[80] = "";
    uint32_t port = 6697;
    if (!zxvd_p_bool(c, "on", false, &on)) return;
    if (!G.open) {
        zxvd_fail(c, ZXVD_E_IRC, "the gateway is not open");
        return;
    }
    if (!on) {
        bridge_stop("The bridge was turned off.");
        bridge_json(zxvd_res(c));
        return;
    }
    if (!zxvd_p_str(c, "host", host, sizeof host, true) ||
        !zxvd_p_str(c, "remote", remote, sizeof remote, true) ||
        !zxvd_p_str(c, "local", local, sizeof local, true) ||
        !zxvd_p_str(c, "nick", nick, sizeof nick, true) ||
        !zxvd_p_str(c, "password", pass, sizeof pass, false) ||
        (zxvd_p_has(c, "port") && !zxvd_p_u32(c, "port", &port)))
        return;
    if (port == 0 || port > 65535) {
        zxvd_fail(c, ZXVD_E_PARAMS, "port must be 1..65535");
        return;
    }
    const char *why = bridge_start(host, (uint16_t) port, remote, local, nick, pass, wall_ms());
    wipe(pass, sizeof pass);
    if (why) {
        zxvd_fail(c, ZXVD_E_IRC, "%s", why);
        return;
    }
    bridge_json(zxvd_res(c));
}

static const zxvd_group_method_t METHODS[] = {
    {"irc.status", m_status, true},
    {"irc.record", m_record, true},
    {"irc.bridge", m_bridge, false},
};

static const zxvd_group_t GROUP = {"irc",
                                   false,
                                   "the IRC gateway on this machine (TLS, localhost only)",
                                   METHODS,
                                   sizeof METHODS / sizeof METHODS[0],
                                   zxvd_irc_close};

const zxvd_group_t *zxvd_irc_group(void)
{
    return &GROUP;
}

#endif
