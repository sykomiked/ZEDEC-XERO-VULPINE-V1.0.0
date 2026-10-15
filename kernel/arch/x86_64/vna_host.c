/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_host.c — the bare-metal Vinea host; see vna_host.h. */

#include "vna_host.h"
#include "uart_16550.h"
#include "robin_debanks/sha256.h"

static void put_hex_bytes(const uint8_t *b, uint32_t n)
{
    static const char hex[] = "0123456789abcdef";
    for (uint32_t i = 0; i < n; i++) {
        uart_putc(hex[b[i] >> 4]);
        uart_putc(hex[b[i] & 15]);
    }
}

static void put_u16(uint16_t v)
{
    uart_put_dec(v);
}

/* The stack's raw-UDP hook: a datagram to the Vinea port is the node's next
 * input. Runs inside sn_pump, the same context as vna_host_pump, so the node
 * is single-threaded like every host the protocol was written for. */
static void vna_rx(void *ctx, const uint8_t src_ip[NET_IP_LEN], uint16_t src_port,
                   const uint8_t *data, uint32_t len)
{
    vna_host_t *h = (vna_host_t *) ctx;
    uint8_t a[6];
    uint32_t i;
    for (i = 0; i < 4 && i < NET_IP_LEN; i++) a[i] = src_ip[i];
    a[4] = (uint8_t) (src_port >> 8);
    a[5] = (uint8_t) src_port;
    h->recv++;
    if (vna_node_handle(&h->node, data, len, a, 6, h->l->clk.ms(h->l->clk.ctx), &h->ob) != VNA_OK)
        h->refused++;
}

/* Send one datagram to a six-byte Vinea address. 0 = on the wire; -1 = the
 * next hop is not in the ARP cache yet (the request went out; the caller may
 * keep the datagram in the re-send ring). */
static int vna_sendto(vna_host_t *h, const uint8_t addr[6], const uint8_t *data, uint32_t len)
{
    uint16_t port = (uint16_t) ((addr[4] << 8) | addr[5]);
    net_connect(&h->l->net, h->sock, addr, port);
    if (net_send(&h->l->net, h->sock, data, len) < 0) return -1;
    h->sent++;
    return 0;
}

/* Every outbox entry to the wire; entries that cannot be sent keep their
 * bytes in the re-send ring (or are dropped and counted when it is full). */
static void vna_flush(vna_host_t *h)
{
    uint32_t i;
    for (i = 0; i < h->ob.n; i++) {
        vna_out_ent_t *e = &h->ob.e[i];
        if (e->addr_len != 6 || e->len > VNAH_PEND_MAX) {
            h->dropped_ob++;
            continue;
        }
        if (vna_sendto(h, e->addr, h->ob.buf + e->off, e->len) == 0) continue;
        if (h->npend < VNAH_PEND_CAP) {
            vnah_pend_t *p = &h->pend[h->npend++];
            uint32_t j;
            for (j = 0; j < 6; j++) p->addr[j] = e->addr[j];
            for (j = 0; j < e->len; j++) p->data[j] = h->ob.buf[e->off + j];
            p->len = (uint16_t) e->len;
        } else {
            h->dropped_tx++;
        }
    }
    vna_outbox_clear(&h->ob);
}

int vna_host_start(vna_host_t *h, sn_link_t *l, const uint8_t peers[][6], uint32_t npeer,
                   uint32_t (*entropy)(void))
{
    uint8_t iseed[32], dseed[32], a[6];
    sha256_ctx_t c;
    vna_node_cfg_t cfg;
    vna_node_mem_t mem;
    uint64_t now;
    uint32_t i, j;

    if (!h || !l || !l->eth || !l->nic) return 0;
    h->l = l;
    h->sock = -1;
    h->npend = 0;
    h->last_tick = 0;
    h->entropy = entropy;
    h->seeds = h->sent = h->recv = h->refused = h->dropped_tx = h->dropped_ob = 0;
    h->up = 0;

    /* One NodeID per machine: the seed is the MAC folded through SHA-256, so
     * it is stable across reboots and cannot collide with another NIC. */
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) "zxv/vinea/node/v1", 17);
    sha256_update(&c, l->nic->mac, NET_MAC_LEN);
    sha256_final(&c, iseed);
    if (vna_identity_create(&h->idn, iseed, 0, 1) != VNA_OK) {
        uart_puts("  [VNA] identity creation failed\n");
        return 0;
    }
    if (entropy) {
        for (i = 0; i < 8; i++) {
            uint32_t r = entropy();
            for (j = 0; j < 4; j++) dseed[i * 4 + j] = (uint8_t) (r >> (j * 8));
        }
    } else {
        /* no hardware source: the node still runs, its sequence numbers are
         * the wall clock's and its DRBG key is unique-to-the-machine */
        sha256_init(&c);
        sha256_update(&c, (const uint8_t *) "zxv/vinea/drbg/v1", 17);
        sha256_update(&c, iseed, sizeof iseed);
        sha256_final(&c, dseed);
    }

    vna_node_cfg_default(&cfg);
    mem.pool = h->pool;
    mem.pool_cap = VNAH_POOL_CAP;
    mem.kc = h->kc;
    mem.kc_cap = VNAH_KC_CAP;
    mem.rp = h->rp;
    mem.rp_cap = VNAH_RP_CAP;
    mem.dd = h->dd;
    mem.dd_cap = VNAH_DD_CAP;
    mem.store = h->store;
    mem.store_cap = VNAH_STORE_CAP;
    if (vna_node_init(&h->node, &h->idn, &cfg, &mem, dseed) != VNA_OK) {
        uart_puts("  [VNA] node init failed\n");
        return 0;
    }
    now = l->clk.ms(l->clk.ctx);
    /* the wall clock as a floor: no message seq a peer may still hold is
     * ever reused after a restart */
    vna_node_resume_seq(&h->node, now << 10);
    vna_node_set_agreement(&h->node, 0, 0, 0, 0);

    h->sock = net_socket(&l->net, SOCK_UDP);
    if (h->sock < 0 || net_bind(&l->net, h->sock, VNAH_PORT) != 0) {
        uart_puts("  [VNA] UDP socket/bind failed\n");
        return 0;
    }
    vna_outbox_init(&h->ob, h->obuf, VNAH_OBUF_CAP);
    net_udp_hook(&l->net, VNAH_PORT, vna_rx, h);

    h->up = 1;
    uart_puts("  [VNA] node ");
    put_hex_bytes(h->idn.id.b, 8);
    uart_puts(" on ");
    for (i = 0; i < 4; i++) {
        uart_put_dec(l->eth->ip[i]);
        if (i < 3) uart_putc('.');
    }
    uart_putc(':');
    put_u16(VNAH_PORT);
    uart_puts("\n");

    if (peers) {
        for (i = 0; i < npeer; i++) {
            /* our own address is never a peer */
            for (j = 0; j < 4; j++) a[j] = peers[i][j];
            if (a[0] == l->eth->ip[0] && a[1] == l->eth->ip[1] && a[2] == l->eth->ip[2] &&
                a[3] == l->eth->ip[3])
                continue;
            for (j = 0; j < 6; j++) a[j] = peers[i][j];
            if (vna_node_bootstrap(&h->node, a, 6, now, &h->ob) == VNA_OK) h->seeds++;
        }
        vna_flush(h);
    }
    return 1;
}

void vna_host_pump(vna_host_t *h, uint64_t now)
{
    uint32_t i;
    if (!h || !h->up) return;

    sn_pump(h->l); /* NIC frames -> stack; the hook hands datagrams to the node */

    if (now >= h->last_tick + VNAH_TICK_MS) {
        h->last_tick = now;
        vna_node_tick(&h->node, now, &h->ob);
    }

    /* the re-send ring first (older datagrams), then whatever the node just
     * queued */
    i = 0;
    while (i < h->npend) {
        vnah_pend_t *p = &h->pend[i];
        if (vna_sendto(h, p->addr, p->data, p->len) != 0) {
            i++;
            continue;
        }
        h->npend--;
        if (i < h->npend) *p = h->pend[h->npend];
    }
    vna_flush(h);
}

void vna_host_report(const vna_host_t *h)
{
    vna_contact_t c[8];
    uint32_t n;
    if (!h || !h->up) return;
    n = vna_node_contacts(&h->node, c, 8);
    uart_puts("  [VNA] id ");
    put_hex_bytes(h->idn.id.b, 8);
    uart_puts(" contacts ");
    uart_put_dec(n);
    uart_puts(" seeds ");
    uart_put_dec(h->seeds);
    uart_puts(" rx ");
    uart_put_dec(h->recv);
    uart_puts(" tx ");
    uart_put_dec(h->sent);
    uart_puts(" refused ");
    uart_put_dec(h->refused);
    uart_puts(" dropped ");
    uart_put_dec(h->dropped_tx + h->dropped_ob + h->ob.dropped);
    uart_puts("\n");
}
