/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_print_host.c — see zxv_print_host.h */
#if !defined(_WIN32)
#    define _POSIX_C_SOURCE 200809L
#    if defined(__APPLE__)
#        define _DARWIN_C_SOURCE
#    endif
#endif

#include "zxv_print_host.h"

#include <stdio.h>
#include <string.h>

static uint32_t TIMEOUT_MS = 10000;

void zph_set_timeout_ms(uint32_t ms)
{
    TIMEOUT_MS = ms ? ms : 10000;
}

static int say(char *err, size_t cap, int code, const char *msg)
{
    if (cap) snprintf(err, cap, "%s", msg);
    return code;
}

const char *zph_format_of(const char *name)
{
    static const struct {
        const char *ext, *mime;
    } F[] = {{"pdf", "application/pdf"}, {"jpg", "image/jpeg"},       {"jpeg", "image/jpeg"},
             {"png", "image/png"},       {"pwg", "image/pwg-raster"}, {"urf", "image/urf"},
             {"txt", "text/plain"}};
    const char *dot = strrchr(name, '.');
    if (!dot || strchr(dot, '/')) return NULL;
    for (size_t i = 0; i < sizeof F / sizeof *F; i++) {
        const char *a = dot + 1, *b = F[i].ext;
        while (*a && *b && (*a | 0x20) == *b) a++, b++;
        if (!*a && !*b) return F[i].mime;
    }
    return NULL;
}

#if defined(_WIN32)

int zph_printer(const char *uri, ipp_printer_t *p, char *err, size_t cap)
{
    (void) uri;
    (void) p;
    return say(err, cap, ZPH_E_PLATFORM, "printing is not available on Windows yet");
}
int zph_print(const char *uri, const char *path, const ipp_job_opts_t *o, ipp_job_t *job, char *err,
              size_t cap)
{
    (void) uri;
    (void) path;
    (void) o;
    (void) job;
    return say(err, cap, ZPH_E_PLATFORM, "printing is not available on Windows yet");
}
int zph_job(const char *uri, int32_t id, bool cancel, ipp_job_t *job, char *err, size_t cap)
{
    (void) uri;
    (void) id;
    (void) cancel;
    (void) job;
    return say(err, cap, ZPH_E_PLATFORM, "printing is not available on Windows yet");
}
int zph_scanner(const char *url, escl_caps_t *c, escl_status_t *s, char *err, size_t cap)
{
    (void) url;
    (void) c;
    (void) s;
    return say(err, cap, ZPH_E_PLATFORM, "scanning is not available on Windows yet");
}
int zph_scan(const char *url, const escl_settings_t *s, const char *folder,
             char saved[][ZXF_PATH_MAX + 1], uint32_t cap, uint32_t *pages, char *err,
             size_t errcap)
{
    (void) url;
    (void) s;
    (void) folder;
    (void) saved;
    (void) cap;
    *pages = 0;
    return say(err, errcap, ZPH_E_PLATFORM, "scanning is not available on Windows yet");
}

#else

#    include <errno.h>
#    include <fcntl.h>
#    include <netdb.h>
#    include <netinet/in.h>
#    include <poll.h>
#    include <stdlib.h>
#    include <sys/socket.h>
#    include <sys/time.h>
#    include <sys/types.h>
#    include <time.h>
#    include <unistd.h>

/* ===================== addresses (N1, N2) ===================== */

typedef struct {
    char host[256];
    char port[8];
    char path[ZPH_URI_MAX];
    char hosthdr[280]; /* the Host header */
} url_t;

static int parse(const char *uri, bool ipp_ok, url_t *u, char *err, size_t cap)
{
    const char *p;
    const char *def;
    memset(u, 0, sizeof *u);
    if (strlen(uri) >= ZPH_URI_MAX) return say(err, cap, ZPH_E_URI, "the address is too long");
    for (p = uri; *p; p++)
        if ((unsigned char) *p <= 0x20 || *p == 0x7f)
            return say(err, cap, ZPH_E_URI, "the address has a space or control character");
    if (ipp_ok && !strncmp(uri, "ipp://", 6)) {
        p = uri + 6;
        def = "631";
    } else if (!strncmp(uri, "http://", 7)) {
        p = uri + 7;
        def = "80";
    } else if (!strncmp(uri, "ipps://", 7) || !strncmp(uri, "https://", 8))
        return say(err, cap, ZPH_E_URI, "secure (ipps, https) devices are not supported yet");
    else
        return say(err, cap, ZPH_E_URI,
                   ipp_ok ? "a printer address starts ipp:// or http://"
                          : "a scanner address starts http://");
    const char *h = p, *he;
    if (*p == '[') {
        he = strchr(p, ']');
        if (!he) return say(err, cap, ZPH_E_URI, "an IPv6 address needs its closing ]");
        h = p + 1;
        p = he + 1;
    } else {
        while (*p && *p != ':' && *p != '/') p++;
        he = p;
    }
    size_t hl = (size_t) (he - h);
    if (!hl || hl >= sizeof u->host) return say(err, cap, ZPH_E_URI, "the address has no host");
    memcpy(u->host, h, hl);
    if (strpbrk(u->host, "@?#%")) return say(err, cap, ZPH_E_URI, "the host is not a plain name");
    const char *port = def;
    char pb[8];
    if (*p == ':') {
        const char *ps = ++p;
        unsigned long v = 0;
        while (*p >= '0' && *p <= '9' && p - ps < 6) v = v * 10u + (unsigned long) (*p++ - '0');
        if (p == ps || p - ps > 5 || v == 0 || v > 65535 || (*p && *p != '/'))
            return say(err, cap, ZPH_E_URI, "the port is not a number from 1 to 65535");
        snprintf(pb, sizeof pb, "%lu", v);
        port = pb;
    } else if (*p && *p != '/')
        return say(err, cap, ZPH_E_URI, "the address is not host[:port]/path");
    snprintf(u->port, sizeof u->port, "%s", port);
    snprintf(u->path, sizeof u->path, "%s", *p ? p : "/");
    bool v6 = strchr(u->host, ':') != NULL;
    snprintf(u->hosthdr, sizeof u->hosthdr, "%s%s%s%s%s", v6 ? "[" : "", u->host, v6 ? "]" : "",
             port == def ? "" : ":", port == def ? "" : port);
    return ZPH_OK;
}

static bool local4(uint32_t a)
{
    return (a >> 24) == 127 || (a >> 24) == 10 || (a >> 20) == 0xAC1 || (a >> 16) == 0xC0A8 ||
           (a >> 16) == 0xA9FE;
}

static bool local_addr(const struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *s = (const void *) sa;
        return local4(ntohl(s->sin_addr.s_addr));
    }
    if (sa->sa_family == AF_INET6) {
        const uint8_t *b = ((const struct sockaddr_in6 *) (const void *) sa)->sin6_addr.s6_addr;
        static const uint8_t loop[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        static const uint8_t mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
        if (!memcmp(b, loop, 16)) return true;
        if ((b[0] & 0xfe) == 0xfc) return true;                 /* fc00::/7 */
        if (b[0] == 0xfe && (b[1] & 0xc0) == 0x80) return true; /* fe80::/10 */
        if (!memcmp(b, mapped, 12))
            return local4((uint32_t) b[12] << 24 | (uint32_t) b[13] << 16 | (uint32_t) b[14] << 8 |
                          b[15]);
    }
    return false;
}

static int dial(const url_t *u, char *err, size_t cap)
{
    struct addrinfo hints, *res, *ai;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(u->host, u->port, &hints, &res) != 0)
        return say(err, cap, ZPH_E_NET, "the device's name could not be found");
    int code = ZPH_E_LOCAL;
    for (ai = res; ai; ai = ai->ai_next) {
        if (!local_addr(ai->ai_addr)) continue;
        code = ZPH_E_NET;
        int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        int rc = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (rc != 0 && errno == EINPROGRESS) {
            struct pollfd pf = {fd, POLLOUT, 0};
            int e = 0;
            socklen_t el = sizeof e;
            rc = poll(&pf, 1, (int) TIMEOUT_MS) == 1 &&
                         getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &el) == 0 && e == 0
                     ? 0
                     : -1;
        }
        if (rc != 0) {
            close(fd);
            continue;
        }
        fcntl(fd, F_SETFL, fl);
        struct timeval tv = {(time_t) (TIMEOUT_MS / 1000),
                             (suseconds_t) (TIMEOUT_MS % 1000) * 1000};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#    if defined(SO_NOSIGPIPE)
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#    endif
        freeaddrinfo(res);
        return fd;
    }
    freeaddrinfo(res);
    return say(err, cap, code,
               code == ZPH_E_LOCAL ? "the device is not on the local network"
                                   : "the device did not answer");
}

/* ===================== HTTP/1.1 ===================== */

static bool send_all(int fd, const void *d, size_t n)
{
    const uint8_t *p = d;
    while (n) {
#    if defined(MSG_NOSIGNAL)
        ssize_t k = send(fd, p, n, MSG_NOSIGNAL);
#    else
        ssize_t k = send(fd, p, n, 0);
#    endif
        if (k <= 0) {
            if (k < 0 && errno == EINTR) continue;
            return false;
        }
        p += k;
        n -= (size_t) k;
    }
    return true;
}

typedef struct {
    int fd;
    uint8_t b[16384];
    uint32_t pos, len;
    bool eof, bad;
} rd_t;

static bool fill(rd_t *r)
{
    if (r->pos < r->len) return true;
    if (r->eof || r->bad) return false;
    ssize_t k;
    do k = recv(r->fd, r->b, sizeof r->b, 0);
    while (k < 0 && errno == EINTR);
    if (k < 0) r->bad = true;
    if (k == 0) r->eof = true;
    if (k <= 0) return false;
    r->pos = 0;
    r->len = (uint32_t) k;
    return true;
}

/* one header line without its CRLF; false at the end or when too long */
static bool line(rd_t *r, char *out, uint32_t cap)
{
    uint32_t n = 0;
    for (;;) {
        if (!fill(r)) return false;
        char c = (char) r->b[r->pos++];
        if (c == '\n') break;
        if (n + 1 >= cap) return false;
        out[n++] = c;
    }
    if (n && out[n - 1] == '\r') n--;
    out[n] = 0;
    return true;
}

typedef int (*sink_fn)(void *ctx, const uint8_t *d, uint32_t n);

/* up to n bytes into the sink (all that is left when n is UINT64_MAX) */
static int body_n(rd_t *r, uint64_t n, bool to_eof, sink_fn sink, void *ctx)
{
    while (to_eof || n) {
        if (!fill(r)) return to_eof && r->eof ? 0 : -1;
        uint32_t k = r->len - r->pos;
        if (!to_eof && k > n) k = (uint32_t) n;
        int rc = sink(ctx, r->b + r->pos, k);
        if (rc) return rc;
        r->pos += k;
        n -= to_eof ? 0 : k;
    }
    return 0;
}

static bool ieq(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if ((a[i] | 0x20) != (b[i] | 0x20)) return false;
    return true;
}

typedef int (*body_fn)(void *ctx, int fd);

typedef struct {
    const char *method, *ctype;
    uint64_t len;
    const uint8_t *body; /* or body_fn below */
    body_fn more;        /* writes the rest of the body after `body` */
    void *more_ctx;
    uint64_t body_len; /* bytes of `body` */
} req_t;

typedef struct {
    int status;
    char location[ZPH_URI_MAX];
} resp_t;

/* one request on a fresh connection; the reply's body goes to sink */
static int http(const url_t *u, const char *path, const req_t *q, resp_t *s, sink_fn sink,
                void *sctx, char *err, size_t cap)
{
    memset(s, 0, sizeof *s);
    int fd = dial(u, err, cap);
    if (fd < 0) return fd;
    static char head[1024];
    int hn = snprintf(head, sizeof head,
                      "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: ZXV-Platen/1\r\n"
                      "Connection: close\r\n",
                      q->method, path, u->hosthdr);
    if (q->ctype)
        hn += snprintf(head + hn, sizeof head - (size_t) hn,
                       "Content-Type: %s\r\nContent-Length: %llu\r\n", q->ctype,
                       (unsigned long long) q->len);
    hn += snprintf(head + hn, sizeof head - (size_t) hn, "\r\n");
    int rc = ZPH_OK;
    if (hn <= 0 || (size_t) hn >= sizeof head || !send_all(fd, head, (size_t) hn) ||
        (q->body_len && !send_all(fd, q->body, (size_t) q->body_len)))
        rc = say(err, cap, ZPH_E_NET, "the device closed the connection");
    if (!rc && q->more && (rc = q->more(q->more_ctx, fd)) != 0) {
        close(fd);
        return rc == ZPH_E_FILE ? rc : say(err, cap, ZPH_E_NET, "the device stopped taking data");
    }
    static rd_t r;
    memset(&r, 0, sizeof r);
    r.fd = fd;
    char l[1024];
    uint64_t clen = 0;
    bool have_len = false, chunked = false;
    do { /* interim 1xx answers come first: skip them */
        if (!rc && (!line(&r, l, sizeof l) || strncmp(l, "HTTP/1.", 7) || strlen(l) < 12 ||
                    (s->status = atoi(l + 9)) < 100 || s->status > 599))
            rc = say(err, cap, ZPH_E_PROTO, "the device's answer was not HTTP");
        while (!rc) {
            if (!line(&r, l, sizeof l)) {
                rc = say(err, cap, ZPH_E_PROTO, "the device's answer ended early");
                break;
            }
            if (!l[0]) break;
            if (s->status < 200) continue;
            if (ieq(l, "content-length:", 15)) {
                char *e;
                unsigned long long v = strtoull(l + 15, &e, 10);
                while (*e == ' ') e++;
                if (*e) rc = say(err, cap, ZPH_E_PROTO, "the device's answer has a bad length");
                clen = v;
                have_len = true;
            } else if (ieq(l, "transfer-encoding:", 18) && strstr(l, "chunked"))
                chunked = true;
            else if (ieq(l, "location:", 9)) {
                const char *v = l + 9;
                while (*v == ' ') v++;
                size_t vl = strlen(v); /* too long to be a job: left out */
                if (vl < sizeof s->location) memcpy(s->location, v, vl + 1);
            }
        }
    } while (!rc && s->status < 200);
    if (!rc && chunked) {
        for (;;) {
            if (!line(&r, l, sizeof l)) {
                rc = say(err, cap, ZPH_E_PROTO, "a chunk of the answer is missing");
                break;
            }
            char *e;
            unsigned long long n = strtoull(l, &e, 16);
            if (e == l || (*e && *e != ';' && *e != ' ')) {
                rc = say(err, cap, ZPH_E_PROTO, "a chunk of the answer has a bad size");
                break;
            }
            if (!n) break;
            int k = body_n(&r, n, false, sink, sctx);
            if (k) {
                rc = k > 0 ? k : say(err, cap, ZPH_E_PROTO, "the answer ended inside a chunk");
                break;
            }
            if (!line(&r, l, sizeof l) || l[0]) {
                rc = say(err, cap, ZPH_E_PROTO, "a chunk of the answer has no end");
                break;
            }
        }
    } else if (!rc && (have_len || (s->status != 204 && s->status != 304))) {
        int k = body_n(&r, have_len ? clen : 0, !have_len, sink, sctx);
        if (k) rc = k > 0 ? k : say(err, cap, ZPH_E_PROTO, "the answer ended early");
    }
    close(fd);
    return rc;
}

/* the reply body into a buffer (ZPH_E_PROTO when it does not fit) */
typedef struct {
    uint8_t *p;
    uint32_t len, cap;
} mem_t;

static int to_mem(void *ctx, const uint8_t *d, uint32_t n)
{
    mem_t *m = ctx;
    if (n > m->cap - m->len) return ZPH_E_PROTO;
    memcpy(m->p + m->len, d, n);
    m->len += n;
    return 0;
}

static int http_status(int status, char *err, size_t cap)
{
    if (status >= 200 && status < 300) return ZPH_OK;
    if (cap) snprintf(err, cap, "the device answered HTTP %d", status);
    return ZPH_E_HTTP;
}

/* ===================== printing ===================== */

#    define IPP_REPLY_MAX (1024u * 1024u)
static uint8_t REPLY[IPP_REPLY_MAX];
static uint8_t REQ[8192];
static uint32_t SEQ;

/* POST an IPP request (and a document after it) to the printer */
static int ipp_post(const url_t *u, const uint8_t *q, uint32_t qn, body_fn doc, void *dctx,
                    uint64_t doc_len, uint32_t *got, char *err, size_t cap)
{
    req_t rq = {"POST", "application/ipp", qn + doc_len, q, doc, dctx, qn};
    resp_t rs;
    mem_t m = {REPLY, 0, sizeof REPLY};
    int rc = http(u, u->path, &rq, &rs, to_mem, &m, err, cap);
    if (rc == ZPH_E_PROTO && m.len == m.cap)
        return say(err, cap, ZPH_E_PROTO, "the printer's answer is too long");
    if (rc) return rc;
    if ((rc = http_status(rs.status, err, cap)) != 0) return rc;
    *got = m.len;
    return ZPH_OK;
}

int zph_printer(const char *uri, ipp_printer_t *p, char *err, size_t cap)
{
    url_t u;
    int rc = parse(uri, true, &u, err, cap);
    if (rc) return rc;
    ipp_w_t w;
    ipp_target_t t = {uri, "zxv"};
    ipp_w_init(&w, REQ, sizeof REQ);
    if (ipp_req_printer(&w, ++SEQ, &t)) return say(err, cap, ZPH_E_URI, "the address is too long");
    uint32_t n;
    if ((rc = ipp_post(&u, REQ, w.len, NULL, NULL, 0, &n, err, cap)) != 0) return rc;
    if (ipp_parse_printer(REPLY, n, p))
        return say(err, cap, ZPH_E_PROTO, "the printer's answer was not IPP");
    if (!IPP_STATUS_OK(p->status)) {
        if (cap) snprintf(err, cap, "the printer refused (IPP status 0x%04x)", p->status);
        return ZPH_E_HTTP;
    }
    return ZPH_OK;
}

typedef struct {
    const char *path;
    uint64_t size;
} doc_t;

static int send_doc(void *ctx, int fd)
{
    doc_t *d = ctx;
    static uint8_t b[ZXF_READ_MAX];
    uint64_t off = 0, size = 0;
    while (off < d->size) {
        uint32_t got = 0;
        if (zxf_read(d->path, off, b, sizeof b, false, &got, &size) != ZXF_OK || !got)
            return ZPH_E_FILE;
        if (!send_all(fd, b, got)) return ZPH_E_NET;
        off += got;
    }
    return 0;
}

static int job_reply(uint32_t n, ipp_job_t *job, char *err, size_t cap)
{
    if (ipp_parse_job(REPLY, n, job))
        return say(err, cap, ZPH_E_PROTO, "the printer's answer was not IPP");
    if (!IPP_STATUS_OK(job->status)) {
        if (cap)
            snprintf(err, cap, "the printer refused (IPP status 0x%04x)%s%s", job->status,
                     job->message[0] ? ": " : "", job->message);
        return ZPH_E_HTTP;
    }
    return ZPH_OK;
}

int zph_print(const char *uri, const char *path, const ipp_job_opts_t *o, ipp_job_t *job, char *err,
              size_t cap)
{
    url_t u;
    int rc = parse(uri, true, &u, err, cap);
    if (rc) return rc;
    zxf_entry_t e;
    int frc = zxf_stat(path, &e);
    if (frc || e.dir) {
        if (cap) snprintf(err, cap, "the file: %s", frc ? zxf_strerror(frc) : "it is a folder");
        return ZPH_E_FILE;
    }
    const char *fmt = zph_format_of(path);
    static ipp_printer_t p;
    if ((rc = zph_printer(uri, &p, err, cap)) != 0) return rc;
    if (!fmt || !ipp_takes(&p, fmt)) {
        if (cap) {
            int k = snprintf(err, cap, "the printer does not take %s files; it takes",
                             fmt ? fmt : "these");
            for (uint32_t i = 0; i < p.nformats && k > 0 && (size_t) k < cap; i++)
                k += snprintf(err + k, cap - (size_t) k, "%s %s", i ? "," : "", p.formats[i]);
        }
        return ZPH_E_FORMAT;
    }
    ipp_job_opts_t jo = *o;
    jo.format = fmt;
    const char *base = strrchr(path, '/');
    if (!jo.job_name || !jo.job_name[0]) jo.job_name = base ? base + 1 : path;
    ipp_target_t t = {uri, "zxv"};
    ipp_w_t w;
    ipp_w_init(&w, REQ, sizeof REQ);
    if (ipp_req_print_job(&w, ++SEQ, &t, &jo))
        return say(err, cap, ZPH_E_URI, "the job's settings are too long");
    doc_t d = {path, e.size};
    uint32_t n;
    rc = ipp_post(&u, REQ, w.len, send_doc, &d, e.size, &n, err, cap);
    if (rc == ZPH_E_FILE) return say(err, cap, rc, "the file could not be read while sending");
    if (rc) return rc;
    return job_reply(n, job, err, cap);
}

int zph_job(const char *uri, int32_t id, bool cancel, ipp_job_t *job, char *err, size_t cap)
{
    url_t u;
    int rc = parse(uri, true, &u, err, cap);
    if (rc) return rc;
    ipp_target_t t = {uri, "zxv"};
    ipp_w_t w;
    ipp_w_init(&w, REQ, sizeof REQ);
    if (ipp_req_job(&w, cancel ? IPP_OP_CANCEL_JOB : IPP_OP_GET_JOB_ATTRIBUTES, ++SEQ, &t, id))
        return say(err, cap, ZPH_E_URI, "the address is too long");
    uint32_t n;
    if ((rc = ipp_post(&u, REQ, w.len, NULL, NULL, 0, &n, err, cap)) != 0) return rc;
    return job_reply(n, job, err, cap);
}

/* ===================== scanning ===================== */

static char XML[65536];

static int escl_get(const url_t *u, const char *leaf, uint32_t *n, char *err, size_t cap)
{
    char path[ZPH_URI_MAX + 32];
    size_t pl = strlen(u->path);
    snprintf(path, sizeof path, "%s%s%s", u->path, pl && u->path[pl - 1] == '/' ? "" : "/", leaf);
    req_t rq = {"GET", NULL, 0, NULL, NULL, NULL, 0};
    resp_t rs;
    mem_t m = {(uint8_t *) XML, 0, sizeof XML - 1};
    int rc = http(u, path, &rq, &rs, to_mem, &m, err, cap);
    if (rc == ZPH_E_PROTO && m.len == m.cap)
        return say(err, cap, ZPH_E_PROTO, "the scanner's answer is too long");
    if (rc || (rc = http_status(rs.status, err, cap)) != 0) return rc;
    XML[m.len] = 0;
    *n = m.len;
    return ZPH_OK;
}

int zph_scanner(const char *url, escl_caps_t *c, escl_status_t *s, char *err, size_t cap)
{
    url_t u;
    uint32_t n;
    int rc = parse(url, false, &u, err, cap);
    if (rc || (rc = escl_get(&u, "ScannerCapabilities", &n, err, cap)) != 0) return rc;
    if (escl_parse_caps(XML, n, c))
        return say(err, cap, ZPH_E_PROTO, "the scanner's capabilities were not eSCL");
    if ((rc = escl_get(&u, "ScannerStatus", &n, err, cap)) != 0) return rc;
    if (escl_parse_status(XML, n, s))
        return say(err, cap, ZPH_E_PROTO, "the scanner's status was not eSCL");
    return ZPH_OK;
}

/* one scanned page, written into the ZXV folder as it arrives */
typedef struct {
    const char *path;
    bool begun;
    uint64_t bytes;
} page_t;

static int to_page(void *ctx, const uint8_t *d, uint32_t n)
{
    page_t *p = ctx;
    uint64_t size;
    int rc = zxf_write(p->path, d, n, p->begun ? ZXF_W_ADD : ZXF_W_BEGIN, true, &size);
    p->begun = true;
    p->bytes += n;
    return rc ? ZPH_E_FILE : 0;
}

static const char *ext_of(const char *mime)
{
    return !strcmp(mime, "application/pdf") ? "pdf" : !strcmp(mime, "image/png") ? "png" : "jpg";
}

/* the first free "folder/Scan N.ext" */
static bool free_name(const char *folder, const char *ext, char *out)
{
    for (uint32_t i = 1; i < 100000; i++) {
        int k =
            snprintf(out, ZXF_PATH_MAX + 1, "%s%sScan %u.%s", folder, folder[0] ? "/" : "", i, ext);
        if (k < 0 || k > (int) ZXF_PATH_MAX) return false;
        zxf_entry_t e;
        if (zxf_stat(out, &e) == ZXF_E_NOTFOUND) return true;
    }
    return false;
}

static void nap_ms(uint32_t ms)
{
    struct timespec ts = {(time_t) (ms / 1000), (long) (ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

int zph_scan(const char *url, const escl_settings_t *s, const char *folder,
             char saved[][ZXF_PATH_MAX + 1], uint32_t cap, uint32_t *pages, char *err,
             size_t errcap)
{
    *pages = 0;
    url_t u;
    int rc = parse(url, false, &u, err, errcap);
    if (rc) return rc;
    zxf_entry_t e;
    int frc = folder[0] ? zxf_stat(folder, &e) : ZXF_OK;
    if (frc || (folder[0] && !e.dir)) {
        if (errcap)
            snprintf(err, errcap, "the folder: %s", frc ? zxf_strerror(frc) : "it is a file");
        return ZPH_E_FILE;
    }
    uint32_t xn;
    static char settings[4096];
    int er = escl_settings(s, settings, sizeof settings, &xn);
    if (er) return say(err, errcap, ZPH_E_URI, "the scan settings are out of range");
    char path[ZPH_URI_MAX + 32];
    size_t pl = strlen(u.path);
    snprintf(path, sizeof path, "%s%sScanJobs", u.path, pl && u.path[pl - 1] == '/' ? "" : "/");
    req_t rq = {"POST", "text/xml", xn, (const uint8_t *) settings, NULL, NULL, xn};
    resp_t rs;
    mem_t m = {(uint8_t *) XML, 0, sizeof XML - 1};
    if ((rc = http(&u, path, &rq, &rs, to_mem, &m, err, errcap)) != 0) return rc;
    if (rs.status == 503) return say(err, errcap, ZPH_E_HTTP, "the scanner is busy");
    if (rs.status == 409)
        return say(err, errcap, ZPH_E_HTTP, "the scanner has no paper loaded there");
    if ((rc = http_status(rs.status, err, errcap)) != 0) return rc;
    char job[ZPH_URI_MAX];
    if (!escl_job_path(rs.location, job, sizeof job - 16))
        return say(err, errcap, ZPH_E_PROTO, "the scanner did not say where the scan is");
    strcat(job, "/NextDocument");
    const char *ext = ext_of(s->format);
    uint32_t busy = 0;
    while (*pages < cap && *pages < ZPH_PAGES_MAX) {
        char *name = saved[*pages];
        if (!free_name(folder, ext, name)) return say(err, errcap, ZPH_E_FILE, "no free file name");
        page_t pg = {name, false, 0};
        req_t g = {"GET", NULL, 0, NULL, NULL, NULL, 0};
        rc = http(&u, job, &g, &rs, to_page, &pg, err, errcap);
        if (rc == ZPH_OK && rs.status == 200 && pg.bytes) {
            uint64_t size;
            if (zxf_write(name, NULL, 0, ZXF_W_END, true, &size))
                return say(err, errcap, ZPH_E_FILE, "the page could not be saved");
            (*pages)++;
            busy = 0;
            continue;
        }
        if (rc == ZPH_E_FILE) return say(err, errcap, rc, "the page could not be saved");
        if (rc) return rc;
        if (rs.status == 404 || (rs.status == 200 && !pg.bytes)) break; /* no more pages */
        if (rs.status == 503 && ++busy <= 30) { /* still warming up or moving paper */
            nap_ms(TIMEOUT_MS < 10000 ? 10 : 1000);
            continue;
        }
        if ((rc = http_status(rs.status, err, errcap)) != 0) return rc;
        break; /* another success with no page: done */
    }
    return ZPH_OK;
}

#endif
