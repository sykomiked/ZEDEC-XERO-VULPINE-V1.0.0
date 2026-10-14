/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_dist.c — the ring all-gather and its launchers (zt_dist.h). Hosted only:
 * POSIX sockets, poll, fork. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "zt_dist.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL /* macOS: SO_NOSIGPIPE on each socket instead */
#    define MSG_NOSIGNAL 0
#endif

static void no_sigpipe(int fd)
{
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#else
    (void) fd;
#endif
}

struct zt_dist_ring {
    zt_dist_t d;
    int fd_next, fd_prev;
    uint32_t timeout_ms;
    uint8_t *sbuf, *rbuf;
    uint64_t cap;
    uint32_t *counts; /* [size] */
    uint64_t bytes_sent, calls;
};

static uint64_t ms_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000u + (uint64_t) ts.tv_nsec / 1000000u;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

/* Values [v0, v1) of each of B vectors, then the count: D2. */
static uint64_t pack(uint8_t *o, const zt_fx *y, uint32_t ystride, uint32_t B, uint32_t v0,
                     uint32_t v1, uint32_t count)
{
    uint8_t *p = o;
    for (uint32_t b = 0; b < B; b++) {
        const zt_fx *row = y + (uint64_t) b * ystride;
        for (uint32_t i = v0; i < v1; i++, p += 4) put32(p, (uint32_t) row[i]);
    }
    put32(p, count);
    return (uint64_t) (p + 4 - o);
}

static uint32_t unpack(const uint8_t *p, zt_fx *y, uint32_t ystride, uint32_t B, uint32_t v0,
                       uint32_t v1)
{
    for (uint32_t b = 0; b < B; b++) {
        zt_fx *row = y + (uint64_t) b * ystride;
        for (uint32_t i = v0; i < v1; i++, p += 4) row[i] = (zt_fx) get32(p);
    }
    return get32(p);
}

/* Send ns bytes to fd_next while receiving nr bytes from fd_prev (D1). */
static int exchange(zt_dist_ring_t *r, const uint8_t *s, uint64_t ns, uint8_t *d, uint64_t nr)
{
    uint64_t so = 0, ro = 0, last = ms_now();
    while (so < ns || ro < nr) {
        struct pollfd p[2];
        int np = 0, is = -1, ir = -1;
        if (so < ns) {
            p[np].fd = r->fd_next;
            p[np].events = POLLOUT;
            is = np++;
        }
        if (ro < nr) {
            p[np].fd = r->fd_prev;
            p[np].events = POLLIN;
            ir = np++;
        }
        int k = poll(p, (nfds_t) np, 100);
        if (k < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        bool moved = false;
        if (is >= 0 && (p[is].revents & (POLLOUT | POLLERR | POLLHUP))) {
            ssize_t w = send(r->fd_next, s + so, (size_t) (ns - so), MSG_NOSIGNAL);
            if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return -1;
            if (w > 0) so += (uint64_t) w, moved = true;
        }
        if (ir >= 0 && (p[ir].revents & (POLLIN | POLLERR | POLLHUP))) {
            ssize_t g = recv(r->fd_prev, d + ro, (size_t) (nr - ro), 0);
            if (g == 0) return -1; /* the peer closed */
            if (g < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return -1;
            if (g > 0) ro += (uint64_t) g, moved = true;
        }
        uint64_t t = ms_now();
        if (moved)
            last = t;
        else if (t - last > r->timeout_ms)
            return -1;
    }
    r->bytes_sent += ns;
    return 0;
}

static int ring_allgather(void *ctx, zt_fx *y, uint32_t ystride, uint32_t B, uint32_t n,
                          uint32_t unit, uint32_t *count)
{
    zt_dist_ring_t *r = ctx;
    uint32_t P = r->d.size, me = r->d.rank, nu = unit ? n / unit : 0;
    r->calls++;
    if (P < 2) return 0;
    /* the largest part's message */
    uint32_t maxv = 0;
    for (uint32_t q = 0; q < P; q++) {
        uint32_t u0, u1;
        zt_dist_part(nu, P, q, &u0, &u1);
        if ((u1 - u0) * unit > maxv) maxv = (u1 - u0) * unit;
    }
    uint64_t need = (uint64_t) B * maxv * 4 + 4;
    if (need > r->cap) {
        uint8_t *s2 = realloc(r->sbuf, (size_t) need), *r2;
        if (!s2) return -1;
        r->sbuf = s2;
        r2 = realloc(r->rbuf, (size_t) need);
        if (!r2) return -1;
        r->rbuf = r2;
        r->cap = need;
    }
    r->counts[me] = *count;
    for (uint32_t t = 0; t + 1 < P; t++) {
        uint32_t ps = (me + P - t) % P, pr = (me + 2 * P - t - 1) % P, s0, s1, q0, q1;
        zt_dist_part(nu, P, ps, &s0, &s1);
        zt_dist_part(nu, P, pr, &q0, &q1);
        uint64_t ns = pack(r->sbuf, y, ystride, B, s0 * unit, s1 * unit, r->counts[ps]);
        uint64_t nr = (uint64_t) B * (q1 - q0) * unit * 4 + 4;
        if (exchange(r, r->sbuf, ns, r->rbuf, nr)) return -1;
        r->counts[pr] = unpack(r->rbuf, y, ystride, B, q0 * unit, q1 * unit);
    }
    uint32_t sum = 0;
    for (uint32_t q = 0; q < P; q++) sum += r->counts[q];
    *count = sum;
    return 0;
}

zt_dist_ring_t *zt_dist_ring_new(uint32_t rank, uint32_t size, int fd_next, int fd_prev,
                                 uint32_t timeout_ms)
{
    if (!size || rank >= size) return NULL;
    zt_dist_ring_t *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    r->counts = calloc(size, sizeof *r->counts);
    if (!r->counts) {
        free(r);
        return NULL;
    }
    r->d.rank = rank;
    r->d.size = size;
    r->d.allgather = ring_allgather;
    r->d.ctx = r;
    r->fd_next = fd_next;
    r->fd_prev = fd_prev;
    r->timeout_ms = timeout_ms ? timeout_ms : 60000u;
    int one = 1;
    for (int i = 0; i < 2; i++) {
        int fd = i ? fd_prev : fd_next;
        if (fd < 0) continue;
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        no_sigpipe(fd);
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one,
                   sizeof one); /* fails harmlessly on AF_UNIX */
    }
    return r;
}

void zt_dist_ring_free(zt_dist_ring_t *r)
{
    if (!r) return;
    if (r->fd_next >= 0) close(r->fd_next);
    if (r->fd_prev >= 0 && r->fd_prev != r->fd_next) close(r->fd_prev);
    free(r->sbuf);
    free(r->rbuf);
    free(r->counts);
    free(r);
}

const zt_dist_t *zt_dist_ring_dist(zt_dist_ring_t *r)
{
    return &r->d;
}

uint64_t zt_dist_ring_bytes_sent(const zt_dist_ring_t *r)
{
    return r->bytes_sent;
}

uint64_t zt_dist_ring_calls(const zt_dist_ring_t *r)
{
    return r->calls;
}

int zt_dist_fork_ring(uint32_t size, int *fd_next, int *fd_prev, pid_t *pids)
{
    *fd_next = *fd_prev = -1;
    if (size < 2) return size == 1 ? 0 : -1;
    int(*sp)[2] = calloc(size, sizeof *sp); /* edge i: rank i -> rank i + 1 */
    if (!sp) return -1;
    for (uint32_t i = 0; i < size; i++) {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp[i])) {
            for (uint32_t j = 0; j < i; j++) close(sp[j][0]), close(sp[j][1]);
            free(sp);
            return -1;
        }
    }
    uint32_t me = 0;
    for (uint32_t c = 1; c < size; c++) {
        pid_t p = fork();
        if (p < 0) {
            free(sp);
            return -1; /* children already started fail on their closed links */
        }
        if (p == 0) {
            me = c;
            break;
        }
        if (pids) pids[c - 1] = p;
    }
    uint32_t prev = (me + size - 1) % size;
    for (uint32_t i = 0; i < size; i++) {
        if (i == me)
            *fd_next = sp[i][0];
        else
            close(sp[i][0]);
        if (i == prev)
            *fd_prev = sp[i][1];
        else
            close(sp[i][1]);
    }
    free(sp);
    return (int) me;
}

static int send_all(int fd, const uint8_t *b, size_t n)
{
    while (n) {
        ssize_t w = send(fd, b, n, MSG_NOSIGNAL);
        if (w <= 0) {
            if (w < 0 && errno == EINTR) continue;
            return -1;
        }
        b += w, n -= (size_t) w;
    }
    return 0;
}

static int recv_all(int fd, uint8_t *b, size_t n)
{
    while (n) {
        ssize_t g = recv(fd, b, n, 0);
        if (g <= 0) {
            if (g < 0 && errno == EINTR) continue;
            return -1;
        }
        b += g, n -= (size_t) g;
    }
    return 0;
}

int zt_dist_tcp_ring(uint32_t rank, uint32_t size, const char *const *hosts, uint16_t port,
                     uint32_t timeout_ms, int *fd_next, int *fd_prev)
{
    *fd_next = *fd_prev = -1;
    if (size < 2 || rank >= size || (uint32_t) port + size > 65535u) return -1;
    int ls = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    if (ls < 0) return -1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((uint16_t) (port + rank));
    if (bind(ls, (struct sockaddr *) &a, sizeof a) || listen(ls, 4)) {
        close(ls);
        return -1;
    }
    /* connect to the next rank, retrying until it listens */
    uint32_t nx = (rank + 1) % size;
    struct addrinfo hint, *res = NULL;
    memset(&hint, 0, sizeof hint);
    hint.ai_family = AF_INET;
    hint.ai_socktype = SOCK_STREAM;
    char ps[8];
    snprintf(ps, sizeof ps, "%u", (unsigned) (port + nx));
    if (getaddrinfo(hosts[nx], ps, &hint, &res) || !res) {
        close(ls);
        return -1;
    }
    uint64_t t0 = ms_now();
    int c = -1;
    for (;;) {
        c = socket(AF_INET, SOCK_STREAM, 0);
        if (c >= 0) no_sigpipe(c);
        if (c >= 0 && !connect(c, res->ai_addr, res->ai_addrlen)) break;
        if (c >= 0) close(c);
        c = -1;
        if (ms_now() - t0 > timeout_ms) break;
        struct timespec ts = {0, 50 * 1000000L};
        nanosleep(&ts, NULL);
    }
    freeaddrinfo(res);
    if (c < 0) {
        close(ls);
        return -1;
    }
    /* hello: "ZTD1", sender rank, size */
    uint8_t hello[12] = {'Z', 'T', 'D', '1'}, got[12];
    put32(hello + 4, rank);
    put32(hello + 8, size);
    struct pollfd pf = {ls, POLLIN, 0};
    int64_t left = (int64_t) timeout_ms - (int64_t) (ms_now() - t0);
    int pr = send_all(c, hello, sizeof hello) ? -1 : poll(&pf, 1, left > 0 ? (int) left : 1);
    int acc = pr > 0 ? accept(ls, NULL, NULL) : -1;
    if (acc >= 0) no_sigpipe(acc);
    close(ls);
    if (acc < 0 || recv_all(acc, got, sizeof got) || memcmp(got, "ZTD1", 4) ||
        get32(got + 4) != (rank + size - 1) % size || get32(got + 8) != size) {
        close(c);
        if (acc >= 0) close(acc);
        return -1;
    }
    *fd_next = c;
    *fd_prev = acc;
    return 0;
}
