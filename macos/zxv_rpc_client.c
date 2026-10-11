/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_rpc_client.c — see zxv_rpc_client.h. */
#if !defined(__APPLE__)
#    define _DEFAULT_SOURCE
#endif
#include "zxv_rpc_client.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(MSG_NOSIGNAL)
#    define SEND_FLAGS MSG_NOSIGNAL
#else
#    define SEND_FLAGS 0 /* macOS: SO_NOSIGPIPE below */
#endif

static int open_socket(const char *path, int timeout_ms)
{
    struct sockaddr_un a;
    if (!path || strlen(path) >= sizeof a.sun_path) return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
#if defined(SO_NOSIGPIPE)
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    struct timeval tv = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    memcpy(a.sun_path, path, strlen(path) + 1);
    if (connect(fd, (struct sockaddr *) &a, sizeof a) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int send_all(int fd, const char *p, size_t n)
{
    while (n) {
        ssize_t k = send(fd, p, n, SEND_FLAGS);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return -1;
        p += k;
        n -= (size_t) k;
    }
    return 0;
}

long zxv_rpc_call(const char *sock_path, const char *request, char *reply, size_t cap,
                  int timeout_ms)
{
    if (!request || !reply || cap == 0 || timeout_ms <= 0) return -1;
    reply[0] = 0;
    if (strchr(request, '\n')) return -1; /* one line is one request */
    int fd = open_socket(sock_path, timeout_ms);
    if (fd < 0) return -1;
    long r = -1;
    if (send_all(fd, request, strlen(request)) == 0 && send_all(fd, "\n", 1) == 0) {
        size_t n = 0;
        for (;;) {
            if (n + 1 >= cap) break; /* does not fit */
            ssize_t k = recv(fd, reply + n, cap - 1 - n, 0);
            if (k < 0 && errno == EINTR) continue;
            if (k <= 0) break; /* timed out or closed before the newline */
            char *nl = memchr(reply + n, '\n', (size_t) k);
            n += (size_t) k;
            if (nl) {
                *nl = 0;
                r = (long) (nl - reply);
                break;
            }
        }
        if (r < 0) reply[0] = 0;
    }
    close(fd);
    return r;
}

long zxv_rpc_unread(const char *reply)
{
    if (!reply || strstr(reply, "\"error\":")) return -1;
    const char *res = strstr(reply, "\"result\":");
    const char *p = res ? strstr(res, "\"unread\":") : NULL;
    if (!p) return -1;
    p += 9;
    if (*p < '0' || *p > '9') return -1;
    long v = 0;
    for (; *p >= '0' && *p <= '9'; p++) {
        if (v > 100000000L) return -1;
        v = v * 10 + (*p - '0');
    }
    return v;
}
