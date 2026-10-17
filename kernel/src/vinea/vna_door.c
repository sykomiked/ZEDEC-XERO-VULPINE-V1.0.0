/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_door.c — the seed's plain-http door. See vna_door.h. */
#include "vna_door.h"

/* the node's public identity, on every answer: a probe or crawler learns in
 * one header block what this stack is, which exchange it serves, and which
 * post-quantum suite the :8724 trunk speaks */
#define ZXV_DOOR_BANNER                                                                           \
    "Server: ZEDEC-XERO-VULPINE/ZXV-Kernel (Post-Quantum Edge)\r\n"                                 \
    "X-CISEX-Exchange: Crown-Intelligence-Service-Exchange\r\n"                                     \
    "X-PQ-Protocol: ML-KEM + ML-DSA-87\r\n"

static void put_head(vna_door_conn_t *c, const char *s)
{
    uint32_t n = 0;
    while (s[n] && c->head_len + n < VNA_DOOR_REQ_MAX) {
        c->head[c->head_len + n] = (uint8_t) s[n];
        n++;
    }
    c->head_len += n;
}

static void put_head_u64(vna_door_conn_t *c, uint64_t v)
{
    char b[20];
    uint32_t n = 0;
    uint64_t div = 10000000000000000000ull;
    while (div > 1 && v < div) div /= 10;
    while (div) {
        b[n++] = (char) ('0' + v / div % 10);
        div /= 10;
    }
    b[n] = 0;
    put_head(c, b);
}

static void resp(vna_door_conn_t *c, const char *line, const char *extra, const char *body)
{
    c->head_len = c->head_sent = 0;
    put_head(c, "HTTP/1.1 ");
    put_head(c, line);
    put_head(c, "\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n");
    put_head(c, ZXV_DOOR_BANNER);
    if (extra) put_head(c, extra);
    put_head(c, "\r\n");
    if (body)
        for (uint32_t i = 0; body[i] && c->head_len < VNA_DOOR_REQ_MAX; i++)
            c->head[c->head_len++] = (uint8_t) body[i];
    c->state = VNA_DOOR_SEND;
}

static uint64_t after_of(const vna_door_conn_t *c)
{
    /* "?after=<digits>" anywhere in the path; missing or malformed is 0. */
    const char *p = c->path;
    uint32_t i = 0;
    while (i + 6 < c->path_len) {
        if (p[i] == 'a' && p[i + 1] == 'f' && p[i + 2] == 't' && p[i + 3] == 'e' &&
            p[i + 4] == 'r' && p[i + 5] == '=') {
            uint64_t v = 0;
            for (i += 6; i < c->path_len && p[i] >= '0' && p[i] <= '9'; i++)
                v = v * 10 + (uint8_t) (p[i] - '0');
            return v;
        }
        i++;
    }
    return 0;
}

static int is_l2_path(const vna_door_conn_t *c)
{
    return c->path_len >= 3 && c->path[0] == '/' && c->path[1] == 'l' && c->path[2] == '2' &&
           (c->path_len == 3 || c->path[3] == '?');
}

static int is_cisex_path(const vna_door_conn_t *c)
{
    return c->path_len >= 6 && c->path[0] == '/' && c->path[1] == 'c' && c->path[2] == 'i' &&
           c->path[3] == 's' && c->path[4] == 'e' && c->path[5] == 'x' &&
           (c->path_len == 6 || c->path[6] == '?');
}

static int is_smic_path(const vna_door_conn_t *c)
{
    return c->path_len >= 5 && c->path[0] == '/' && c->path[1] == 's' && c->path[2] == 'm' &&
           c->path[3] == 'i' && c->path[4] == 'c' && (c->path_len == 5 || c->path[5] == '?');
}

static int is_card_path(const vna_door_conn_t *c)
{
    return c->path_len == 5 && c->path[0] == '/' && c->path[1] == 'c' && c->path[2] == 'a' &&
           c->path[3] == 'r' && c->path[4] == 'd';
}

static void on_body(vna_door_t *d, vna_door_conn_t *c);

static void on_request(vna_door_t *d, vna_door_conn_t *c)
{
    /* the request line's method and target; anything else is refused */
    uint8_t m_get =
        c->rx_len >= 4 && c->rx[0] == 'G' && c->rx[1] == 'E' && c->rx[2] == 'T' && c->rx[3] == ' ';
    uint8_t m_post = c->rx_len >= 5 && c->rx[0] == 'P' && c->rx[1] == 'O' && c->rx[2] == 'S' &&
                     c->rx[3] == 'T' && c->rx[4] == ' ';
    uint32_t mlen = m_get ? 4 : m_post ? 5 : 0;
    if (!mlen) {
        resp(c, "405 Method Not Allowed\r\n", "Content-Length: 0\r\n", 0);
        return;
    }
    uint32_t e = mlen;
    while (e < c->rx_len && c->rx[e] != ' ' && c->rx[e] != '\r') e++;
    if (e - mlen > VNA_DOOR_PATH_MAX - 1) {
        resp(c, "414 URI Too Long\r\n", "Content-Length: 0\r\n", 0);
        return;
    }
    c->path_len = e - mlen;
    for (uint32_t i = 0; i < c->path_len; i++) c->path[i] = (char) c->rx[mlen + i];
    c->path[c->path_len] = 0;

    if (m_post) {
        if (!is_l2_path(c) && !is_cisex_path(c) && !is_smic_path(c) && !is_card_path(c)) {
            resp(c, "404 Not Found\r\n", "Content-Length: 0\r\n", 0);
            return;
        }
        /* Content-Length: the one header the door reads */
        uint64_t cl = 0;
        for (uint32_t i = 0; i + 15 < c->rx_len; i++) {
            if (c->rx[i] == 'C' && c->rx[i + 1] == 'o' && c->rx[i + 2] == 'n' &&
                c->rx[i + 3] == 't' && c->rx[i + 4] == 'e' && c->rx[i + 5] == 'n' &&
                c->rx[i + 6] == 't' && c->rx[i + 7] == '-' && c->rx[i + 8] == 'L' &&
                c->rx[i + 14] == ':') {
                uint32_t j = i + 15;
                while (j < c->rx_len && c->rx[j] == ' ') j++;
                while (j < c->rx_len && c->rx[j] >= '0' && c->rx[j] <= '9')
                    cl = cl * 10 + (uint8_t) (c->rx[j++] - '0');
                break;
            }
        }
        if (cl > VNA_DOOR_BODY_MAX) {
            resp(c, "413 Payload Too Large\r\n", "Content-Length: 0\r\n", 0);
            return;
        }
        c->body_need = (uint32_t) cl;
        c->state = VNA_DOOR_BODY;
        /* an empty body is a complete request: the query endpoints answer
         * their summary for in_len == 0 */
        if (cl == 0) on_body(d, c);
        return;
    }

    /* GET */
    if (is_l2_path(c)) {
        c->after = after_of(c);
        c->tick = c->after;
        c->opened = c->last_out = d->now;
        c->head_len = c->head_sent = 0;
        put_head(c, "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
                    "Access-Control-Allow-Origin: *\r\nConnection: close\r\n" ZXV_DOOR_BANNER
                "\r\n");
        c->state = VNA_DOOR_STREAM;
        return;
    }
    if (c->path_len == 1 && c->path[0] == '/') {
        const char *idx = "/index.html";
        for (uint32_t i = 0; i < 12; i++) c->path[i] = idx[i];
        c->path_len = 11;
    }
    int32_t sz = d->size ? d->size(d->ctx, c->path, c->path_len) : -1;
    if (sz < 0) {
        resp(c, "404 Not Found\r\n", "Content-Length: 0\r\n", 0);
        return;
    }
    c->head_len = c->head_sent = 0;
    const char *ct = d->type ? d->type(d->ctx, c->path, c->path_len) : 0;
    put_head(c, "HTTP/1.1 200 OK\r\nContent-Type: ");
    put_head(c, ct && ct[0] ? ct : "application/octet-stream");
    put_head(c, "\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n" ZXV_DOOR_BANNER
                "Content-Length: ");
    put_head_u64(c, (uint64_t) sz);
    put_head(c, "\r\n\r\n");
    c->body_off = 0;
    c->body_left = sz;
    c->state = VNA_DOOR_FILE;
}

static void on_body(vna_door_t *d, vna_door_conn_t *c)
{
    if (is_cisex_path(c) || is_smic_path(c) || is_card_path(c)) {
        /* delegated routing: the request is consumed, so c->rx is scratch
         * for the reply datagram */
        vna_door_query_fn q = is_cisex_path(c) ? d->query : is_smic_path(c) ? d->query2 : d->query3;
        void *qc = is_cisex_path(c) ? d->qctx : is_smic_path(c) ? d->qctx2 : d->qctx3;
        int32_t n;
        if (!q) {
            resp(c, "404 Not Found\r\n", "Content-Length: 0\r\n", 0);
            return;
        }
        n = q(qc, c->body, c->body_len, c->rx, VNA_DOOR_REQ_MAX - 400);
        if (n < 0) {
            resp(c, "400 Bad Request\r\n", "Content-Length: 0\r\n", 0);
            return;
        }
        c->head_len = c->head_sent = 0;
        put_head(c, "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
                    "Access-Control-Allow-Origin: *\r\nConnection: close\r\n" ZXV_DOOR_BANNER
                    "Content-Length: ");
        put_head_u64(c, (uint64_t) n);
        put_head(c, "\r\n\r\n");
        for (int32_t i = 0; i < n && c->head_len < VNA_DOOR_REQ_MAX; i++)
            c->head[c->head_len++] = c->rx[i];
        c->state = VNA_DOOR_SEND;
        return;
    }
    /* a POST carries no Lamport field: the door's clock names its order */
    int st = vna_door_delta_in(d, c->body, c->body_len, d->page.clock + 1);
    char b = st >= 0 && st <= 3 ? (char) ('0' + st) : 'x';
    c->head_len = c->head_sent = 0;
    put_head(c, "HTTP/1.1 ");
    put_head(c, st >= 0 ? "200 OK\r\n" : "400 Bad Request\r\n");
    put_head(c, "Content-Length: 1\r\nAccess-Control-Allow-Origin: *\r\n"
                "Connection: close\r\n" ZXV_DOOR_BANNER "\r\n");
    c->head[c->head_len++] = (uint8_t) b;
    c->state = VNA_DOOR_SEND;
}

void vna_door_init(vna_door_t *d, void *ctx, vna_door_size_fn size, vna_door_type_fn type,
                   vna_door_body_fn body, vna_door_gossip_fn gossip)
{
    if (!d) return;
    vna_zero(d, sizeof *d);
    d->ctx = ctx;
    d->size = size;
    d->type = type;
    d->body = body;
    d->gossip = gossip;
    vna_delta_page_init(&d->page);
}

int vna_door_open(vna_door_t *d)
{
    if (!d) return -1;
    for (uint32_t i = 0; i < VNA_DOOR_CONNS; i++) {
        if (!d->conn[i].used) {
            vna_zero(&d->conn[i], sizeof d->conn[i]);
            d->conn[i].used = 1;
            return (int) i;
        }
    }
    return -1;
}

void vna_door_close(vna_door_t *d, uint32_t c)
{
    if (d && c < VNA_DOOR_CONNS) d->conn[c].used = 0;
}

int vna_door_input(vna_door_t *d, uint32_t c, const uint8_t *data, uint32_t n)
{
    if (!d || !data || c >= VNA_DOOR_CONNS || !d->conn[c].used) return VNA_ERR_ARG;
    vna_door_conn_t *k = &d->conn[c];
    if (k->state == VNA_DOOR_BODY) {
        uint32_t take = k->body_need - k->body_len;
        if (take > n) take = n;
        vna_copy(k->body + k->body_len, data, take);
        k->body_len += take;
        if (k->body_len == k->body_need) on_body(d, k);
        return VNA_OK;
    }
    if (k->state != VNA_DOOR_REQ) return VNA_OK; /* a stream ignores upstream bytes */
    if (k->rx_len + n > VNA_DOOR_REQ_MAX) {
        k->state = VNA_DOOR_DONE;
        return VNA_ERR_SPACE;
    }
    vna_copy(k->rx + k->rx_len, data, n);
    k->rx_len += n;
    /* the head ends at the first blank line */
    for (uint32_t i = 0; i + 3 < k->rx_len; i++) {
        if (k->rx[i] == '\r' && k->rx[i + 1] == '\n' && k->rx[i + 2] == '\r' &&
            k->rx[i + 3] == '\n') {
            /* any bytes past the head are the start of a body */
            uint32_t rest = k->rx_len - (i + 4);
            if (rest > VNA_DOOR_BODY_MAX) rest = VNA_DOOR_BODY_MAX;
            vna_copy(k->body, k->rx + i + 4, rest);
            k->body_len = rest;
            uint32_t head_n = i + 4;
            on_request(d, k);
            k->rx_len = head_n; /* the request is parsed; room for its answer */
            if (k->state == VNA_DOOR_BODY && k->body_len >= k->body_need) {
                k->body_len = k->body_need;
                on_body(d, k);
            }
            return VNA_OK;
        }
    }
    return VNA_OK;
}

static uint32_t stage_head(vna_door_conn_t *k, uint8_t *out, uint32_t cap)
{
    uint32_t left = k->head_len - k->head_sent;
    if (left > cap) left = cap;
    vna_copy(out, k->head + k->head_sent, left);
    k->head_sent += left;
    if (k->head_sent == k->head_len && (k->state == VNA_DOOR_SEND)) k->state = VNA_DOOR_DONE;
    return left;
}

/* the oldest unframed log entry for this stream, or 0 */
static vna_door_ent_t *log_next(vna_door_t *d, vna_door_conn_t *k)
{
    for (uint32_t i = 0; i < d->log_n; i++) {
        vna_door_ent_t *e = &d->log[(d->log_head + i) % VNA_DOOR_DELTAS];
        if (e->tick > k->tick) return e;
    }
    return 0;
}

uint32_t vna_door_pull(vna_door_t *d, uint32_t c, uint8_t *out, uint32_t cap)
{
    if (!d || !out || c >= VNA_DOOR_CONNS || !d->conn[c].used || !cap) return 0;
    vna_door_conn_t *k = &d->conn[c];
    uint32_t n = 0;

    if (k->tx_sent < k->tx_len) { /* a staged frame first */
        uint32_t take = k->tx_len - k->tx_sent;
        if (take > cap) take = cap;
        vna_copy(out, k->tx + k->tx_sent, take);
        k->tx_sent += take;
        return take;
    }

    if (k->head_sent < k->head_len) {
        n = stage_head(k, out, cap);
        return n;
    }

    if (k->state == VNA_DOOR_FILE) {
        if (!d->body || k->body_left == 0) {
            k->state = VNA_DOOR_DONE;
            return 0;
        }
        int32_t got = d->body(d->ctx, k->path, k->path_len, k->body_off, out, cap);
        if (got <= 0) {
            k->state = VNA_DOOR_DONE;
            return 0;
        }
        k->body_off += (uint64_t) got;
        if (k->body_left >= got) k->body_left -= got;
        return (uint32_t) got;
    }

    if (k->state != VNA_DOOR_STREAM) return 0;

    vna_door_ent_t *e = log_next(d, k);
    if (e) {
        /* [u16le len][u64le tick][bytes] */
        if (cap < 10u + e->len) {
            /* stage what the caller's buffer cannot take whole */
            uint32_t fl = 10u + e->len;
            vna_put16(k->tx, (uint16_t) e->len);
            vna_put64(k->tx + 2, e->tick);
            vna_copy(k->tx + 10, e->bytes, e->len);
            k->tx_len = fl;
            k->tx_sent = 0;
            k->tick = e->tick;
            uint32_t take = fl > cap ? cap : fl;
            vna_copy(out, k->tx, take);
            k->tx_sent = take;
            return take;
        }
        vna_put16(out, (uint16_t) e->len);
        vna_put64(out + 2, e->tick);
        vna_copy(out + 10, e->bytes, e->len);
        k->tick = e->tick;
        k->last_out = d->now;
        return 10 + e->len;
    }
    if (d->now - k->last_out >= VNA_DOOR_KEEPALIVE_MS && cap >= 10) {
        vna_put16(out, 0);
        vna_put64(out + 2, k->tick);
        k->last_out = d->now;
        return 10;
    }
    return 0;
}

void vna_door_tick(vna_door_t *d, uint64_t now)
{
    if (!d) return;
    d->now = now;
    for (uint32_t i = 0; i < VNA_DOOR_CONNS; i++) {
        vna_door_conn_t *k = &d->conn[i];
        if (k->used && k->state == VNA_DOOR_STREAM && now - k->opened >= VNA_DOOR_STREAM_MS)
            k->state = VNA_DOOR_DONE;
    }
}

static int delta_in(vna_door_t *d, const uint8_t *bytes, uint32_t n, uint64_t tick, int gossip)
{
    if (!d || !bytes) return VNA_ERR_ARG;
    int st = vna_delta_apply(&d->page, bytes, n, tick);
    if (st < 0) return st;
    vna_door_ent_t *e;
    if (d->log_n == VNA_DOOR_DELTAS) {
        e = &d->log[d->log_head];
        d->log_head = (d->log_head + 1) % VNA_DOOR_DELTAS;
    } else {
        e = &d->log[(d->log_head + d->log_n) % VNA_DOOR_DELTAS];
        d->log_n++;
    }
    e->tick = tick;
    e->len = (uint16_t) n;
    vna_copy(e->bytes, bytes, n);
    if (gossip && d->gossip) d->gossip(d->ctx, bytes, n);
    return st;
}

int vna_door_delta_in(vna_door_t *d, const uint8_t *bytes, uint32_t n, uint64_t tick)
{
    return delta_in(d, bytes, n, tick, 1);
}

int vna_door_delta_hear(vna_door_t *d, const uint8_t *bytes, uint32_t n, uint64_t tick)
{
    return delta_in(d, bytes, n, tick, 0);
}

const vna_delta_page_t *vna_door_page(const vna_door_t *d)
{
    return d ? &d->page : 0;
}
