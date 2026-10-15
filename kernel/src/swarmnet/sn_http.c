/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sn_http.c — a minimal HTTP/1.1 client for gateway blocks, and the gateways
 * as an ipfsn block source (swarmnet.h).
 * One connection per gateway, kept alive across requests; Content-Length
 * bodies only. */
#include "swarmnet.h"

static uint64_t now(sn_link_t *l)
{
    return l->clk.ms(l->clk.ctx);
}

static void drop(sn_http_t *h)
{
    if (h->sock >= 0) net_close(&h->l->net, h->sock);
    h->sock = -1;
}

/* Dotted quad, or 0. */
static int ipv4(const char *s, uint8_t ip[4])
{
    uint32_t o = 0, v = 0, d = 0;
    for (;; s++) {
        if (*s >= '0' && *s <= '9') {
            v = v * 10u + (uint32_t) (*s - '0');
            if (++d > 3 || v > 255) return 0;
        } else if ((*s == '.' && o < 3) || (*s == 0 && o == 3)) {
            if (d == 0) return 0;
            ip[o++] = (uint8_t) v;
            v = d = 0;
            if (*s == 0) return 1;
        } else {
            return 0;
        }
    }
}

static int resolve(sn_http_t *h, const char **why)
{
    if (h->resolved) return 0;
    if (ipv4(h->host, h->ip)) {
        h->resolved = 1;
        return 0;
    }
    sn_link_t *l = h->l;
    net_state_t *n = &l->net;
    if (!(n->dns_server[0] | n->dns_server[1] | n->dns_server[2] | n->dns_server[3])) {
        *why = "no DNS server from DHCP to resolve the gateway's name";
        return -1;
    }
    for (uint32_t t = 0; t < 3; t++) {
        uint64_t end = now(l) + SN_ARP_MS;
        net_dns_query(n, l->eth, h->host);
        while (now(l) < end) {
            sn_pump(l);
            if (net_dns_result(n, h->host, h->ip)) {
                h->resolved = 1;
                return 0;
            }
        }
    }
    *why = "the gateway's name did not resolve";
    return -1;
}

/* Resolve the next hop's MAC before the first segment, so the SYN does not
 * wait on the retransmit timer. */
static int arp(sn_http_t *h, const char **why)
{
    sn_link_t *l = h->l;
    net_interface_t *e = l->eth;
    uint8_t hop[4], mac[6];
    int on_link = 1;
    for (int i = 0; i < 4; i++)
        if ((h->ip[i] & e->netmask[i]) != (e->ip[i] & e->netmask[i])) on_link = 0;
    for (int i = 0; i < 4; i++) hop[i] = on_link ? h->ip[i] : e->gateway[i];
    for (uint32_t t = 0; t < 3; t++) {
        if (net_arp_lookup(&l->net, hop, mac)) return 0;
        net_arp_request(&l->net, e, hop);
        uint64_t end = now(l) + SN_ARP_MS;
        while (now(l) < end && !net_arp_lookup(&l->net, hop, mac)) sn_pump(l);
    }
    if (net_arp_lookup(&l->net, hop, mac)) return 0;
    *why = on_link ? "no ARP answer from the gateway host" : "no ARP answer from the router";
    return -1;
}

static int gw_connect(sn_http_t *h, const char **why)
{
    sn_link_t *l = h->l;
    if (h->sock >= 0) {
        tcp_state_t st = l->net.sockets[h->sock].tcp_state;
        if (st == TCP_ESTABLISHED) return 0;
        drop(h);
    }
    if (resolve(h, why) || arp(h, why)) return -1;
    h->sock = net_socket(&l->net, SOCK_TCP);
    if (h->sock < 0 || net_connect(&l->net, h->sock, h->ip, h->port) != 0) {
        drop(h);
        *why = "no TCP connection slot";
        return -1;
    }
    uint64_t end = now(l) + SN_CONNECT_MS;
    while (now(l) < end) {
        sn_pump(l);
        if (l->net.sockets[h->sock].tcp_state == TCP_ESTABLISHED) return 0;
        if (l->net.sockets[h->sock].tcp_state == TCP_CLOSED) break;
    }
    drop(h);
    *why = "TCP connection to the gateway was refused or timed out";
    return -1;
}

static uint32_t put(char *o, uint32_t n, uint32_t cap, const char *s)
{
    while (*s && n + 1 < cap) o[n++] = *s++;
    o[n] = 0;
    return n;
}

int sn_http_get(sn_http_t *h, const char *path, uint8_t *buf, uint32_t cap, uint32_t *len,
                const char **why)
{
    sn_link_t *l = h->l;
    char req[512];
    uint32_t n = 0;
    *len = 0;
    if (gw_connect(h, why)) return -1;
    n = put(req, n, sizeof req, "GET ");
    n = put(req, n, sizeof req, path);
    n = put(req, n, sizeof req, " HTTP/1.1\r\nHost: ");
    n = put(req, n, sizeof req, h->host);
    n = put(req, n, sizeof req,
            "\r\nAccept: application/vnd.ipld.raw\r\nUser-Agent: ZXV-swarmnet\r\n\r\n");
    if (n + 1 >= sizeof req) {
        *why = "request too long";
        return -1;
    }
    uint32_t stall = h->stall_ms ? h->stall_ms : SN_STALL_MS;
    uint32_t sent = 0;
    uint64_t last = now(l);
    h->timed_out = 0;
    while (sent < n) {
        int32_t k = net_send(&l->net, h->sock, req + sent, n - sent);
        if (k > 0) {
            sent += (uint32_t) k;
            last = now(l);
        } else if (now(l) - last > stall) {
            drop(h);
            h->timed_out = 1;
            *why = "the gateway stopped accepting the request";
            return -1;
        }
        sn_pump(l);
    }
    /* the head */
    sn_head_t hd;
    uint32_t hn = 0;
    int32_t he = 0;
    last = now(l);
    while (he == 0) {
        if (hn == sizeof h->head) {
            drop(h);
            *why = "response head larger than 2 KiB";
            return -1;
        }
        int32_t k = net_recv(&l->net, h->sock, h->head + hn, (uint32_t) sizeof h->head - hn);
        if (k > 0) {
            hn += (uint32_t) k;
            last = now(l);
            he = sn_http_head(h->head, hn, &hd);
            continue;
        }
        tcp_state_t st = l->net.sockets[h->sock].tcp_state;
        if (st != TCP_ESTABLISHED || now(l) - last > stall) {
            h->timed_out = st == TCP_ESTABLISHED;
            drop(h);
            *why = st != TCP_ESTABLISHED ? "the gateway closed the connection"
                                         : "the gateway did not answer";
            return -1;
        }
        sn_pump(l);
    }
    if (he < 0) {
        drop(h);
        *why = "malformed HTTP response";
        return -1;
    }
    if (hd.status != 200 || hd.chunked || !hd.has_length || hd.length > cap) {
        drop(h);
        *why = hd.status == 404   ? "the gateway does not have the block (404)"
               : hd.status != 200 ? "the gateway did not answer 200"
               : hd.chunked       ? "chunked responses are not supported"
               : !hd.has_length   ? "no Content-Length"
                                  : "block larger than the buffer";
        return -1;
    }
    /* the body: whatever followed the head, then the rest */
    uint32_t have = hn - (uint32_t) he;
    if (have > hd.length) {
        drop(h); /* bytes past the body: no pipelining, so the stream is confused */
        *why = "more bytes than Content-Length";
        return -1;
    }
    hw_memcpy(buf, h->head + he, have);
    last = now(l);
    while (have < hd.length) {
        int32_t k = net_recv(&l->net, h->sock, buf + have, (uint32_t) hd.length - have);
        if (k > 0) {
            have += (uint32_t) k;
            last = now(l);
            continue;
        }
        tcp_conn_t *c = &l->net.tcp_conns[l->net.sockets[h->sock].tcp_idx];
        if (c->peer_fin || c->reset || now(l) - last > stall) {
            h->timed_out = !(c->peer_fin || c->reset);
            drop(h);
            *why = c->peer_fin || c->reset ? "the gateway closed mid-body"
                                           : "the gateway stalled mid-body";
            return -1;
        }
        sn_pump(l);
    }
    if (hd.close) drop(h);
    *len = have;
    return 0;
}

/* ---- blocks from the gateways ---------------------------------------------- */

int sn_gateway_get(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    sn_fetch_t *f = (sn_fetch_t *) ctx;
    char path[16 + IPFSN_CID_STR_MAX];
    const char *pre = "/ipfs/", *suf = "?format=raw";
    uint32_t n = 0;
    for (uint32_t i = 0; pre[i]; i++) path[n++] = pre[i];
    int k = ipfsn_cid_to_string(cid, path + n, IPFSN_CID_STR_MAX);
    if (k < 0) return IPFSN_ERR_ARG;
    n += (uint32_t) k;
    for (uint32_t i = 0; suf[i]; i++) path[n++] = suf[i];
    path[n] = 0;
    /* the gateway that served last first, then the others in turn; one try
     * each: the step machine waits and comes back (swarmnet.h, ELASTIC) */
    uint32_t usable = 0;
    for (uint32_t i = 0; i < f->nhttp; i++) {
        uint32_t g = (f->g0 + i) % f->nhttp;
        sn_http_t *h = &f->http[g];
        if (h->bad) continue;
        usable++;
        const char *why = 0;
        int ok = sn_http_get(h, path, buf, cap, len, &why) == 0;
        sn_http_adapt(h, ok);
        if (ok) {
            if (ipfsn_cid_verify(cid, buf, *len) == IPFSN_OK) {
                f->g0 = g;
                return IPFSN_OK;
            }
            h->bad = 1;
            usable--;
            why = "sent a block that does not match its CID; not asked again this boot";
        }
        hw_line_t s;
        hw_line_init(&s);
        hw_line_s(&s, "  [SWARM_FETCH] gateway ");
        hw_line_s(&s, h->host);
        hw_line_s(&s, ": ");
        hw_line_s(&s, why);
        f->p->log(s.b);
    }
    /* none left that is honest: no wait will help */
    return usable ? IPFSN_ERR_TRANSPORT : IPFSN_ERR_HASH;
}

uint32_t sn_fetch_gateways(sn_fetch_t *f, sn_link_t *l, const hw_models_t *m)
{
    f->link = l;
    f->nhttp = 0;
    for (uint32_t i = 0; i < m->ngateway; i++) {
        sn_http_t *h = &f->http[f->nhttp];
        hw_memset(h, 0, sizeof *h);
        if (!sn_url_parse(m->gateway[i], h->host, sizeof h->host, &h->port)) {
            hw_line_t s;
            hw_line_init(&s);
            hw_line_s(&s, "  [SWARM_FETCH] skipping gateway ");
            hw_line_s(&s, m->gateway[i]);
            hw_line_s(&s, ": only plain http:// gateways in this path (no TLS yet)");
            f->p->log(s.b);
            continue;
        }
        h->l = l;
        h->sock = -1;
        f->nhttp++;
    }
    f->get = sn_gateway_get;
    f->get_ctx = f;
    f->clk = l->clk;
    return f->nhttp;
}
