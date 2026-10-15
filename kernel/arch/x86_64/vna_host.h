/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_host.h — the bare-metal Vinea (Web4) node the x86_64 kernel runs once
 * the network link is up.
 *
 * The hosted build wires the same node to OS sockets
 * (kernel/arch/hosted/zxv_net_host.c); this host wires it to the kernel net
 * stack through the swarmnet link: datagrams to the Vinea port arrive at the
 * stack's raw-UDP hook (a node record carries an ML-DSA-87 key and signature
 * and does not fit the socket rx_buf) and leave through net_connect +
 * net_send on a socket bound to the port, so the source port is stable.
 *
 * Seed peers come from the core disc's /ZXV/UPSTREAM.CID "peer <ipv4> <port>"
 * lines (hw_models.h), stored as six-byte Vinea transport addresses: four
 * IPv4 octets, then the port big-endian — the same form the hosted node's
 * ADDR_LEN 6 uses.
 *
 * Bounded, integer only, no allocation: the routing table, key cache, replay
 * window, record store, outbox and the re-send ring are all arrays owned by
 * this struct. What does not fit is dropped and counted; the protocol
 * retries on its own timeouts.
 */
#ifndef ZXV_VNA_HOST_H
#define ZXV_VNA_HOST_H

#include <stdint.h>
#include "swarmnet/swarmnet.h"
#include "vinea/vna_node.h"

#define VNAH_PORT      8724u /* the fleet's Vinea UDP port */
#define VNAH_POOL_CAP  256u  /* routing-table contacts          */
#define VNAH_KC_CAP    48u   /* verified key cache              */
#define VNAH_RP_CAP    64u   /* replay-window entries           */
#define VNAH_DD_CAP    64u   /* dedupe hashes                   */
#define VNAH_STORE_CAP 64u   /* records held for the network    */
#define VNAH_OBUF_CAP  (256u * 1024u)
#define VNAH_PEND_CAP  4u /* datagrams waiting on ARP        */
#define VNAH_PEND_MAX  (VNA_MSG_WIRE_MAX + VNA_XFORM_MAX_OVERHEAD)
#define VNAH_TICK_MS   200u

/* One datagram that could not be sent yet (the next hop's MAC was not in the
 * ARP cache; the request is in flight). Bounded: when the ring is full the
 * datagram is dropped and counted. */
typedef struct {
    uint8_t addr[6];
    uint16_t len;
    uint8_t data[VNAH_PEND_MAX];
} vnah_pend_t;

typedef struct {
    sn_link_t *l;
    int32_t sock;
    vna_identity_t idn;
    vna_node_t node;
    vna_contact_t pool[VNAH_POOL_CAP];
    vna_keycache_ent_t kc[VNAH_KC_CAP];
    vna_replay_ent_t rp[VNAH_RP_CAP];
    uint8_t dd[VNAH_DD_CAP][32];
    vna_store_slot_t store[VNAH_STORE_CAP];
    vna_outbox_t ob;
    uint8_t obuf[VNAH_OBUF_CAP];
    vnah_pend_t pend[VNAH_PEND_CAP];
    uint32_t npend;
    uint64_t last_tick;
    uint32_t (*entropy)(void); /* a hardware u32 source; 0 when the CPU has none */
    uint32_t seeds, sent, recv, refused, dropped_tx, dropped_ob;
    uint8_t up;
} vna_host_t;

/* Bind the Vinea node to the link: identity derived from the NIC's MAC (each
 * machine keeps one NodeID across reboots and no two machines share one),
 * the node's tables in this host's arrays, the UDP hook on VNAH_PORT, then
 * one bootstrap ping per seed. 1 = running; 0 = refused (logged). */
int vna_host_start(vna_host_t *h, sn_link_t *l, const uint8_t peers[][6], uint32_t npeer,
                   uint32_t (*entropy)(void));

/* The idle-loop hook: drain the NIC (hooked datagrams reach the node inside
 * sn_pump), tick the node at VNAH_TICK_MS, flush the outbox and the re-send
 * ring to the wire. */
void vna_host_pump(vna_host_t *h, uint64_t now);

/* One serial line: node id, our transport address, table size, counters. */
void vna_host_report(const vna_host_t *h);

#endif /* ZXV_VNA_HOST_H */
