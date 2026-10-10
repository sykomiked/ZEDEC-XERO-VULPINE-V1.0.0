/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_net_host.c — UDP glue for one Vinea node. See zxv_net_host.h (U1-U5). */
#if defined(_WIN32)
#    define WIN32_LEAN_AND_MEAN
#    include <winsock2.h>
#    include <ws2tcpip.h>
#    include <windows.h>
typedef SOCKET nsock_t;
#    define NCLOSE    closesocket
#    define NBAD_SOCK INVALID_SOCKET
typedef int nlen_t;
#else
#    define _DEFAULT_SOURCE
#    define _DARWIN_C_SOURCE
#    include <sys/types.h>
#    include <sys/socket.h>
#    include <sys/time.h>
#    include <netinet/in.h>
#    include <arpa/inet.h>
#    include <errno.h>
#    include <fcntl.h>
#    include <unistd.h>
typedef int nsock_t;
#    define NCLOSE    close
#    define NBAD_SOCK (-1)
typedef socklen_t nlen_t;
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "zxv_net_host.h"
#include "zxv_http_guard.h" /* zxv_os_random */
#include "vna_node.h"
#include "vna_link.h"

#define POOL_CAP    256u
#define KC_CAP      48u
#define RP_CAP      64u
#define DD_CAP      64u
#define STORE_CAP   16u
#define ADDR_LEN    6u
#define READ_BURST  64u
#define TICK_MS     100u
#define REFRESH_MS  60000u
#define DGRAM_SLACK 64u
#define BOOK_CAP    32u
#define IMPORT_CAP  ((uint64_t) 1 << 20) /* internal rate one cycle may import */

struct zxv_net {
    zxv_net_mode_t mode;
    nsock_t s;
    uint16_t port;
    char bind_ip[16];
    char error[96];
    uint64_t last_tick, last_refresh;
    uint32_t in, out, refused, policy, send_err;
    vna_identity_t idn;
    vna_node_t node;
    vna_contact_t pool[POOL_CAP];
    vna_keycache_ent_t kc[KC_CAP];
    vna_replay_ent_t rp[RP_CAP];
    uint8_t dd[DD_CAP][32];
    vna_store_slot_t store[STORE_CAP];
    vna_outbox_t ob;
    uint8_t *obuf;
    uint8_t rbuf[VNA_MSG_WIRE_MAX + VNA_XFORM_MAX_OVERHEAD + DGRAM_SLACK];
    /* U6: the mesh economy (vna_econ.h) and its trade loop (vna_link.h) */
    vna_book_t book;
    vna_account_t acct[BOOK_CAP];
    vna_gate_t gate;
    uint8_t owner_secret[32];
    vna_link_t link;
};

uint64_t zxv_net_wall_ms(void)
{
#if defined(_WIN32)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t t = ((uint64_t) ft.dwHighDateTime << 32) | ft.dwLowDateTime; /* 100 ns since 1601 */
    return t / 10000u - 11644473600000ull;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t) ts.tv_sec * 1000u + (uint64_t) ts.tv_nsec / 1000000u;
#endif
}

const char *zxv_net_mode_name(zxv_net_mode_t m)
{
    return m == ZXV_NET_LAN ? "lan" : m == ZXV_NET_ONLINE ? "online" : "off";
}

int zxv_net_mode_parse(const char *s, zxv_net_mode_t *m)
{
    if (!s || !m) return -1;
    if (!strcmp(s, "off"))
        *m = ZXV_NET_OFF;
    else if (!strcmp(s, "lan"))
        *m = ZXV_NET_LAN;
    else if (!strcmp(s, "online"))
        *m = ZXV_NET_ONLINE;
    else
        return -1;
    return 0;
}

zxv_net_t *zxv_net_new(void)
{
    zxv_net_t *n = (zxv_net_t *) calloc(1, sizeof *n);
    if (!n) return NULL;
    n->s = NBAD_SOCK;
    n->mode = ZXV_NET_OFF;
    return n;
}

void zxv_net_stop(zxv_net_t *n)
{
    if (!n) return;
    if (n->s != NBAD_SOCK) NCLOSE(n->s);
    n->s = NBAD_SOCK;
    n->mode = ZXV_NET_OFF;
    n->port = 0;
    free(n->obuf);
    n->obuf = NULL;
    /* the secret key does not outlive the session */
    memset(&n->idn, 0, sizeof n->idn);
    memset(&n->node, 0, sizeof n->node);
    memset(n->owner_secret, 0, sizeof n->owner_secret);
    memset(&n->book, 0, sizeof n->book);
    memset(&n->link, 0, sizeof n->link);
}

void zxv_net_free(zxv_net_t *n)
{
    if (!n) return;
    zxv_net_stop(n);
    free(n);
}

intptr_t zxv_net_fd(const zxv_net_t *n)
{
    return (n && n->s != NBAD_SOCK) ? (intptr_t) n->s : (intptr_t) -1;
}

/* U2: private-use, link-local and loopback IPv4 ranges. */
static bool ip_private(const uint8_t a[4])
{
    return a[0] == 10 || a[0] == 127 || (a[0] == 172 && (a[1] & 0xF0) == 16) ||
           (a[0] == 192 && a[1] == 168) || (a[0] == 169 && a[1] == 254);
}

static bool allowed(const zxv_net_t *n, const uint8_t a[4])
{
    if (n->mode == ZXV_NET_ONLINE) return true;
    if (n->mode == ZXV_NET_LAN) return ip_private(a);
    return false;
}

static void to_addr6(const struct sockaddr_in *sa, uint8_t out[ADDR_LEN])
{
    memcpy(out, &sa->sin_addr.s_addr, 4); /* already network order */
    memcpy(out + 4, &sa->sin_port, 2);
}

static void flush(zxv_net_t *n)
{
    for (uint32_t i = 0; i < n->ob.n; i++) {
        const vna_out_ent_t *e = &n->ob.e[i];
        if (e->addr_len != ADDR_LEN || !allowed(n, e->addr)) {
            n->policy++;
            continue;
        }
        struct sockaddr_in to;
        memset(&to, 0, sizeof to);
        to.sin_family = AF_INET;
        memcpy(&to.sin_addr.s_addr, e->addr, 4);
        memcpy(&to.sin_port, e->addr + 4, 2);
        if (to.sin_port == 0) {
            n->policy++;
            continue;
        }
        int k = (int) sendto(n->s, (const char *) n->ob.buf + e->off, (int) e->len, 0,
                             (const struct sockaddr *) &to, sizeof to);
        if (k == (int) e->len)
            n->out++;
        else
            n->send_err++;
    }
    vna_outbox_clear(&n->ob);
}

/* U6: send what the trade loop queued */
static void drain_link(zxv_net_t *n, uint64_t now_ms)
{
    vna_id_t dst;
    const uint8_t *b;
    uint32_t len;
    while (vna_link_next_out(&n->link, &dst, &b, &len))
        (void) vna_node_send_rcpt(&n->node, &dst, b, len, now_ms, &n->ob);
}

/* U6: a receipt from a peer. The node verified the message (signature, PoW,
 * replay); a peer the book does not know yet is registered from the key the
 * node verified, not a commons member (the owner admits members). */
static vna_status_t on_rcpt(void *ctx, const vna_id_t *from, const uint8_t *r, uint32_t len,
                            uint64_t now)
{
    zxv_net_t *n = (zxv_net_t *) ctx;
    if (!vna_book_find(&n->book, from)) {
        const vna_keycache_t *kc = &n->node.kc;
        const vna_keycache_ent_t *e = NULL;
        for (uint32_t i = 0; i < kc->cap && !e; i++)
            if (kc->e[i].used && vna_id_eq(&kc->e[i].id, from)) e = &kc->e[i];
        if (!e ||
            vna_book_add_peer(&n->book, from, e->pk, e->pow_nonce, e->pow_bits, false) != VNA_OK)
            return VNA_ERR_DENIED;
    }
    return vna_link_on_rcpt(&n->link, from, r, len, now);
}

static int set_nonblocking(nsock_t s)
{
#if defined(_WIN32)
    u_long one = 1;
    return ioctlsocket(s, FIONBIO, &one) == 0 ? 0 : -1;
#else
    int fl = fcntl(s, F_GETFL, 0);
    if (fl < 0 || fcntl(s, F_SETFL, fl | O_NONBLOCK) != 0) return -1;
    (void) fcntl(s, F_SETFD, FD_CLOEXEC);
    return 0;
#endif
}

static int fail(zxv_net_t *n, const char *why)
{
    char keep[sizeof n->error];
    snprintf(keep, sizeof keep, "%s", why);
    zxv_net_stop(n);
    memcpy(n->error, keep, sizeof keep);
    return -1;
}

int zxv_net_start(zxv_net_t *n, zxv_net_mode_t mode, const char *bind_ip, uint16_t port,
                  uint64_t now_ms)
{
    if (!n) return -1;
    zxv_net_stop(n);
    n->error[0] = 0;
    n->in = n->out = n->refused = n->policy = n->send_err = 0;
    if (mode == ZXV_NET_OFF) return 0; /* U1: nothing opened */
    if (mode != ZXV_NET_LAN && mode != ZXV_NET_ONLINE) return fail(n, "unknown mode");
    const char *ip = bind_ip && bind_ip[0] ? bind_ip : "0.0.0.0";
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &sa.sin_addr) != 1) return fail(n, "bad bind address");

    /* U5: identity and node */
    uint8_t seed[32], dseed[32];
    if (zxv_os_random(seed, sizeof seed) != 0 || zxv_os_random(dseed, sizeof dseed) != 0)
        return fail(n, "no secure random source");
    vna_status_t st = vna_identity_create(&n->idn, seed, 0, 1);
    memset(seed, 0, sizeof seed);
    if (st != VNA_OK) return fail(n, "identity creation failed");
    vna_node_cfg_t cfg;
    vna_node_cfg_default(&cfg);
    cfg.degree = VNA_DEG_ROUTE;
    vna_node_mem_t mem = {n->pool, POOL_CAP, n->kc,  KC_CAP,   n->rp,
                          RP_CAP,  n->dd,    DD_CAP, n->store, STORE_CAP};
    st = vna_node_init(&n->node, &n->idn, &cfg, &mem, dseed);
    memset(dseed, 0, sizeof dseed);
    if (st != VNA_OK) return fail(n, "node init failed");
    vna_node_set_agreement(&n->node, NULL, NULL, NULL, NULL);

    /* U6: the book, the owner gate and the trade loop. No agreement is
     * attached, so this node sells nothing (fail closed) and wants nothing
     * until its owner says so; the loop still runs every cycle. */
    uint8_t lseed[32];
    if (zxv_os_random(n->owner_secret, sizeof n->owner_secret) != 0 ||
        zxv_os_random(lseed, sizeof lseed) != 0)
        return fail(n, "no secure random source");
    vna_book_init(&n->book, &n->idn, n->acct, BOOK_CAP);
    vna_gate_init(&n->gate, n->owner_secret, 0, IMPORT_CAP, 4);
    if (vna_gate_begin_cycle(&n->gate, n->owner_secret) != VNA_OK) return fail(n, "gate failed");
    vna_link_init(&n->link, &n->book, NULL, NULL, lseed, 1000, 8);
    memset(lseed, 0, sizeof lseed);
    vna_node_set_rcpt_handler(&n->node, on_rcpt, n);
    size_t obcap = (size_t) (VNA_OUTBOX_MAX + 1u) * (VNA_MSG_WIRE_MAX + VNA_XFORM_MAX_OVERHEAD);
    if (!(n->obuf = (uint8_t *) malloc(obcap))) return fail(n, "out of memory");
    vna_outbox_init(&n->ob, n->obuf, (uint32_t) obcap);

    /* U3: the socket */
    n->s = socket(AF_INET, SOCK_DGRAM, 0);
    if (n->s == NBAD_SOCK) return fail(n, "socket failed");
    if (set_nonblocking(n->s) != 0) return fail(n, "could not make the socket non-blocking");
    if (bind(n->s, (struct sockaddr *) &sa, sizeof sa) != 0) return fail(n, "port is busy");
    nlen_t al = sizeof sa;
    if (getsockname(n->s, (struct sockaddr *) &sa, &al) == 0) port = ntohs(sa.sin_port);
    n->port = port;
    snprintf(n->bind_ip, sizeof n->bind_ip, "%s", ip);
    n->mode = mode;
    n->last_tick = n->last_refresh = now_ms;
    return 0;
}

void zxv_net_on_readable(zxv_net_t *n, uint64_t now_ms)
{
    if (!n || n->s == NBAD_SOCK) return;
    for (uint32_t i = 0; i < READ_BURST; i++) {
        struct sockaddr_in from;
        nlen_t fl = sizeof from;
        int k = (int) recvfrom(n->s, (char *) n->rbuf, (int) sizeof n->rbuf, 0,
                               (struct sockaddr *) &from, &fl);
        if (k < 0) {
#if defined(_WIN32)
            int e = WSAGetLastError();
            if (e == WSAECONNRESET || e == WSAEMSGSIZE) continue; /* ICMP echo of an old send */
#else
            if (errno == EINTR || errno == ECONNREFUSED) continue;
#endif
            return; /* would block: drained */
        }
        uint8_t a[ADDR_LEN];
        if (fl < (nlen_t) sizeof from || from.sin_family != AF_INET) continue;
        to_addr6(&from, a);
        if (!allowed(n, a)) {
            n->policy++;
            continue;
        }
        if ((size_t) k > VNA_MSG_WIRE_MAX + VNA_XFORM_MAX_OVERHEAD) {
            n->refused++;
            continue;
        }
        n->in++;
        if (vna_node_handle(&n->node, n->rbuf, (uint32_t) k, a, ADDR_LEN, now_ms, &n->ob) != VNA_OK)
            n->refused++;
        drain_link(n, now_ms);
        flush(n);
    }
}

void zxv_net_tick(zxv_net_t *n, uint64_t now_ms)
{
    if (!n || n->s == NBAD_SOCK) return;
    if (now_ms < n->last_tick + TICK_MS) return;
    n->last_tick = now_ms;
    vna_node_tick(&n->node, now_ms, &n->ob);
    vna_link_tick(&n->link, now_ms);
    drain_link(n, now_ms);
    if (now_ms >= n->last_refresh + REFRESH_MS) {
        n->last_refresh = now_ms;
        vna_node_refresh(&n->node, now_ms, REFRESH_MS * 15u, 2, &n->ob);
    }
    flush(n);
}

static int parse_ip(const char *ip, uint8_t out[4])
{
    struct in_addr ia;
    if (!ip || inet_pton(AF_INET, ip, &ia) != 1) return -1;
    memcpy(out, &ia.s_addr, 4);
    return 0;
}

int zxv_net_add_peer(zxv_net_t *n, const char *ip, uint16_t port, uint64_t now_ms)
{
    if (!n || n->s == NBAD_SOCK || port == 0) return -1;
    uint8_t a[ADDR_LEN];
    if (parse_ip(ip, a) != 0) return -1;
    if (!allowed(n, a)) {
        n->policy++;
        return -1;
    }
    a[4] = (uint8_t) (port >> 8);
    a[5] = (uint8_t) port;
    vna_status_t st = vna_node_bootstrap(&n->node, a, ADDR_LEN, now_ms, &n->ob);
    flush(n);
    return st == VNA_OK ? 0 : -1;
}

int zxv_net_add_peer_str(zxv_net_t *n, const char *ip_port, uint64_t now_ms)
{
    char ip[16];
    if (!ip_port) return -1;
    const char *c = strrchr(ip_port, ':');
    if (!c || c == ip_port || (size_t) (c - ip_port) >= sizeof ip) return -1;
    memcpy(ip, ip_port, (size_t) (c - ip_port));
    ip[c - ip_port] = 0;
    char *e;
    long p = strtol(c + 1, &e, 10);
    if (*e || p <= 0 || p > 65535) return -1;
    return zxv_net_add_peer(n, ip, (uint16_t) p, now_ms);
}

int32_t zxv_net_find_node(zxv_net_t *n, const uint8_t id[32], uint64_t now_ms)
{
    if (!n || n->s == NBAD_SOCK || !id) return -1;
    vna_id_t t;
    memcpy(t.b, id, sizeof t.b);
    int32_t slot = vna_node_lookup(&n->node, &t, VNA_LK_FIND_NODE, now_ms, &n->ob);
    flush(n);
    return slot;
}

int zxv_net_find_result(zxv_net_t *n, int32_t slot)
{
    if (!n || n->s == NBAD_SOCK || slot < 0) return 0;
    if (!vna_node_lookup_done(&n->node, slot)) return -1;
    const vna_node_lookup_t *L = vna_node_lookup_get(&n->node, slot);
    int hit = 0;
    if (L) {
        static vna_cand_t res[VNA_KAD_K];
        uint32_t k = vna_lookup_result(&L->lk, VNA_KAD_K, res);
        for (uint32_t i = 0; i < k; i++) hit |= vna_id_eq(&res[i].id, &L->lk.target);
    }
    vna_node_lookup_release(&n->node, slot);
    return hit;
}

void zxv_net_node_id(const zxv_net_t *n, uint8_t out[32])
{
    memset(out, 0, 32);
    if (n && n->s != NBAD_SOCK) memcpy(out, n->idn.id.b, 32);
}

bool zxv_net_knows(const zxv_net_t *n, const char *ip, uint16_t port)
{
    uint8_t a[ADDR_LEN];
    if (!n || n->s == NBAD_SOCK || parse_ip(ip, a) != 0) return false;
    a[4] = (uint8_t) (port >> 8);
    a[5] = (uint8_t) port;
    static vna_contact_t c[POOL_CAP];
    uint32_t k = vna_node_contacts(&n->node, c, POOL_CAP);
    for (uint32_t i = 0; i < k; i++)
        if (c[i].addr_len == ADDR_LEN && !memcmp(c[i].addr, a, ADDR_LEN)) return true;
    return false;
}

int zxv_net_econ_cycle(zxv_net_t *n, swarm_budget_t *sb, uint64_t base_rate, uint64_t now_ms,
                       uint64_t *imported)
{
    if (imported) *imported = 0;
    if (!n || !sb) return -1;
    if (n->s == NBAD_SOCK) return 0; /* U1: off, nothing to close */
    vna_status_t st =
        vna_link_cycle(&n->link, &n->gate, n->owner_secret, sb, base_rate, now_ms, imported);
    drain_link(n, now_ms);
    flush(n);
    return st == VNA_OK ? 0 : -1;
}

void zxv_net_status(const zxv_net_t *n, zxv_net_status_t *st)
{
    memset(st, 0, sizeof *st);
    if (!n) return;
    st->mode = n->mode;
    st->bound = n->s != NBAD_SOCK;
    st->port = n->port;
    snprintf(st->bind_ip, sizeof st->bind_ip, "%s", st->bound ? n->bind_ip : "");
    snprintf(st->error, sizeof st->error, "%s", n->error);
    st->datagrams_in = n->in;
    st->datagrams_out = n->out;
    st->refused_in = n->refused;
    st->policy_drops = n->policy;
    st->send_errors = n->send_err;
    if (!st->bound) return;
    st->trades = n->link.committed + n->link.confirmed;
    st->receipts_refused = n->link.refused + n->node.rcpt_refused;
    st->imported = n->link.imported;
    st->conserved = vna_book_conserved(&n->book);
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        st->node_id[2 * i] = hex[n->idn.id.b[i] >> 4];
        st->node_id[2 * i + 1] = hex[n->idn.id.b[i] & 15];
    }
    st->node_id[16] = 0;
    static vna_contact_t c[POOL_CAP];
    st->peers = vna_node_contacts(&n->node, c, POOL_CAP);
}
