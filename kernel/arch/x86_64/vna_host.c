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

/* The fleet's commons test: a contact is a member when its transport
 * address is one of the seed peers this node was started with. Every other
 * verified peer trades as a counterparty but draws nothing from the
 * commons the owner pays for. */
static int vna_is_seed(const vna_host_t *h, const uint8_t addr[6])
{
    uint32_t i, j;
    for (i = 0; i < h->nseed_addr; i++) {
        for (j = 0; j < 6; j++)
            if (h->seed_addr[i][j] != addr[j]) break;
        if (j == 6) return 1;
    }
    return 0;
}

/* A verified peer's transport address, or 0 when the routing table does not
 * have it (then it cannot be a seed, and it is not a member). */
static int vna_contact_addr(vna_host_t *h, const vna_id_t *id, uint8_t addr[6])
{
    const vna_contact_t *c = vna_rt_find(&h->node.rt, id);
    uint32_t j;
    if (!c || c->addr_len != 6) return 0;
    for (j = 0; j < 6; j++) addr[j] = c->addr[j];
    return 1;
}

/* Register `id` in the book from the key the node verified (keycache), if
 * the book does not know it yet. Members are the fleet: seed addresses
 * only — the commons is the owner's, outsiders still trade. */
static int vna_book_learn(vna_host_t *h, const vna_id_t *id)
{
    const vna_keycache_t *kc = &h->node.kc;
    const vna_keycache_ent_t *e = 0;
    uint8_t addr[6];
    uint32_t i;
    if (vna_book_find(&h->book, id)) return 1;
    for (i = 0; i < kc->cap && !e; i++)
        if (kc->e[i].used && vna_id_eq(&kc->e[i].id, id)) e = &kc->e[i];
    if (!e) return 0;
    return vna_book_add_peer(&h->book, id, e->pk, e->pow_nonce, e->pow_bits,
                             vna_contact_addr(h, id, addr) && vna_is_seed(h, addr)) == VNA_OK;
}

/* A receipt from a peer: the node verified the message (signature, PoW,
 * replay); the link checks it against the trade protocol. */
static vna_status_t vna_on_rcpt(void *ctx, const vna_id_t *from, const uint8_t *r, uint32_t len,
                                uint64_t now)
{
    vna_host_t *h = (vna_host_t *) ctx;
    if (!vna_book_learn(h, from)) {
        h->refused_econ++;
        return VNA_ERR_DENIED;
    }
    return vna_link_on_rcpt(&h->link, from, r, len, now);
}

/* Receipts the trade loop queued go out through the node (signed, sequenced
 * messages); the caller's vna_flush puts them on the wire. */
static void vna_drain_link(vna_host_t *h, uint64_t now)
{
    vna_id_t dst;
    const uint8_t *b;
    uint32_t len;
    while (vna_link_next_out(&h->link, &dst, &b, &len))
        (void) vna_node_send_rcpt(&h->node, &dst, b, len, now, &h->ob);
}

/* One economic cycle. Wants top up what the node buys (records served back
 * to us — the store part of the seed role); sells offer each peer exactly
 * the service the usage table measured it drawing (record stores and file
 * chunks), on credit at the unit price. The owner gate then turns the
 * cycle's budget into the financial pool and the settle pays the members
 * whose contribution was verified. Nothing here is trusted: every receipt
 * is countersigned or it never touches a book. */
static void vna_settle(vna_host_t *h, uint64_t now)
{
    uint64_t got;
    uint32_t i, left = 0;

    /* Keep open wants for the service this node consumes (records stored
     * for it and file chunks served to it — the other half of the seed
     * role); each slot refills only after peers' offers drain the old one. */
    for (i = 0; i < VNA_LINK_WANTS; i++)
        if (h->link.want[i].used && (h->link.want[i].resource == VNA_RES_RECORD ||
                                     h->link.want[i].resource == VNA_RES_FILE))
            left += (uint32_t) h->link.want[i].units_left;
    if (!left) {
        (void) vna_link_want(&h->link, VNA_RES_RECORD, SWARM_CAP_FINANCIAL, VNAH_WANT_UNITS,
                             VNAH_WANT_UNITS * VNAH_UNIT_PRICE);
        (void) vna_link_want(&h->link, VNA_RES_FILE, SWARM_CAP_FINANCIAL, VNAH_WANT_UNITS,
                             VNAH_WANT_UNITS * VNAH_UNIT_PRICE);
    }

    /* Sell what the agreement actually served this cycle, per peer, under
     * the resource that was drawn. The served_*[] shadows keep the deltas
     * across quota periods; a reused slot counts only its fresh owner's
     * draw. */
    for (i = 0; i < VNAH_USAGE_CAP; i++) {
        vna_usage_ent_t *e = &h->ue[i];
        uint64_t rec, ch;
        if (!e->used) {
            h->served_rec[i] = h->served_file[i] = 0;
            continue;
        }
        rec = e->records;
        ch = e->chunks;
        if ((rec > h->served_rec[i] || ch > h->served_file[i]) && vna_book_learn(h, &e->peer)) {
            if (rec > h->served_rec[i]) {
                uint64_t units = rec - h->served_rec[i];
                if (vna_link_sell(&h->link, &e->peer, VNA_TR_CREDIT, SWARM_CAP_FINANCIAL,
                                  VNA_RES_RECORD, units, units * VNAH_UNIT_PRICE,
                                  "serve(record, form: financial)", now) == VNA_OK)
                    h->sold++;
            }
            if (ch > h->served_file[i]) {
                uint64_t units = ch - h->served_file[i];
                if (vna_link_sell(&h->link, &e->peer, VNA_TR_CREDIT, SWARM_CAP_FINANCIAL,
                                  VNA_RES_FILE, units, units * VNAH_UNIT_PRICE,
                                  "serve(file, form: financial)", now) == VNA_OK)
                    h->sold++;
            }
        }
        h->served_rec[i] = rec;
        h->served_file[i] = ch;
    }

    /* The cycle's emission: the budget slot's allotment becomes the pool
     * the settle splits over the members' verified contribution. */
    (void) swarm_budget_begin_cycle(&h->sb);
    (void) vna_gate_export(&h->gate, h->owner_secret, &h->sb, VNAH_MODEL_ID, &h->book,
                           SWARM_CAP_FINANCIAL, VNAH_EXPORT_MAX, &got);
    vna_usage_new_cycle(&h->us);
    (void) vna_usage_expire(&h->us, now);
    {
        uint64_t imported = 0;
        if (vna_link_cycle(&h->link, &h->gate, h->owner_secret, &h->sb, VNAH_BASE_RATE, now,
                           &imported) != VNA_OK)
            h->refused_econ++;
        h->imported_total += imported;
    }
    vna_drain_link(h, now);
    h->earn_cycles++;
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
    h->seed_addr = peers;
    h->nseed_addr = npeer;
    for (i = 0; i < VNAH_USAGE_CAP; i++) h->served_rec[i] = h->served_file[i] = 0;
    h->earned_posted = h->imported_total = 0;
    h->last_cycle = 0;
    h->sold = h->earn_cycles = h->refused_econ = 0;
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
    h->last_cycle = now; /* the first settle runs one full cycle in */
    /* the wall clock as a floor: no message seq a peer may still hold is
     * ever reused after a restart */
    vna_node_resume_seq(&h->node, now << 10);

    /* The economy. The gate and the trade loop's signature randomness are
     * keyed like the identity: deterministically from this machine's seed,
     * so nothing depends on a random source the CPU may not have. The
     * agreement is the seed role itself: serve the network (records, file
     * chunks, routing) to anyone, bounded by the per-peer quotas. */
    vna_book_init(&h->book, &h->idn, h->acct, VNAH_BOOK_CAP);
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) "zxv/vinea/gate/v1", 17);
    sha256_update(&c, iseed, sizeof iseed);
    sha256_final(&c, h->owner_secret);
    vna_gate_init(&h->gate, h->owner_secret, VNAH_EXPORT_MAX, VNAH_IMPORT_CAP, 4);
    if (vna_gate_begin_cycle(&h->gate, h->owner_secret) != VNA_OK) {
        uart_puts("  [VNA] gate failed\n");
        return 0;
    }
    vna_usage_init(&h->us, h->ue, VNAH_USAGE_CAP, h->le, VNAH_LEASE_CAP);
    vna_agree_default(&h->agr, &h->idn.id);
    h->agr.degree = VNA_DEG_SERVE;
    h->agr.audience = VNA_AUD_EVERYONE;
    h->agr.quota_period_ms = VNAH_QUOTA_MS;
    h->agr.q_records = VNAH_Q_RECORDS;
    h->agr.q_chunks = VNAH_Q_CHUNKS;
    {
        uint8_t lseed[32];
        sha256_init(&c);
        sha256_update(&c, (const uint8_t *) "zxv/vinea/link/v1", 17);
        sha256_update(&c, iseed, sizeof iseed);
        sha256_final(&c, lseed);
        vna_link_init(&h->link, &h->book, &h->agr, &h->us, lseed, VNAH_RETRY_MS, VNAH_TRIES);
    }
    vna_node_set_agreement(&h->node, &h->agr, &h->us, 0, 0);
    vna_node_set_rcpt_handler(&h->node, vna_on_rcpt, h);
    (void) swarm_budget_init(&h->sb, 1, VNAH_BASE_RATE);
    (void) swarm_budget_register(&h->sb, VNAH_MODEL_ID, 0);

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
        vna_link_tick(&h->link, now); /* L2: resend what is still waiting */
    }
    if (now >= h->last_cycle + VNAH_CYCLE_MS) {
        h->last_cycle = now;
        vna_settle(h, now);
    }
    vna_drain_link(h, now);

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

uint64_t vna_host_take_earned(vna_host_t *h)
{
    uint64_t e;
    if (!h || !h->up) return 0;
    e = h->book.held_in[SWARM_CAP_FINANCIAL];
    if (e <= h->earned_posted) return 0;
    e -= h->earned_posted;
    h->earned_posted += e;
    return e;
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
    uart_puts(" cycle ");
    uart_put_dec(h->book.cycle);
    uart_puts(" pool ");
    uart_put_dec(h->book.pool[SWARM_CAP_FINANCIAL]);
    uart_puts(" held_in ");
    uart_put_dec(h->book.held_in[SWARM_CAP_FINANCIAL]);
    uart_puts(" sold ");
    uart_put_dec(h->sold);
    uart_puts(" traded ");
    uart_put_dec(h->link.committed + h->link.confirmed);
    uart_puts(" refused_econ ");
    uart_put_dec(h->refused_econ + h->link.refused);
    uart_puts("\n");
}
