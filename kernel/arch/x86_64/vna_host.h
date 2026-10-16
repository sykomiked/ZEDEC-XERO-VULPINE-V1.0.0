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
#include "vinea/vna_link.h"
#include "vinea/vna_space.h"
#include "vinea/vna_door.h"
#include "swarm/swarm_budget.h"

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

/* The seed fleet's economy. A cycle is one settle: the node offers the
 * service its usage table measured (records and file chunks peers drew
 * against our agreement, on credit), the owner gate converts the cycle
 * budget into the financial pool (the audited mint op, bounded by
 * VNAH_EXPORT_MAX), and the book distributes it to members weighted by
 * verified contribution. What peers' settles pay this node accumulates in
 * book.held_in — the earned float vna_host_take_earned hands to the vino
 * bridge. Membership is the seed list only: the owner's own infrastructure
 * shares its commons; every other peer still trades as a counterparty. */
#define VNAH_BOOK_CAP   32u            /* counterparty accounts            */
#define VNAH_USAGE_CAP  32u            /* peers quota-tracked at once      */
#define VNAH_LEASE_CAP  8u             /* memory leases (unused at SERVE)  */
#define VNAH_CYCLE_MS   600000u        /* one settle every ten minutes     */
#define VNAH_BASE_RATE  144u           /* per-cycle emission, F(12) units  */
#define VNAH_EXPORT_MAX VNAH_BASE_RATE /* gate export cap == rate    */
#define VNAH_IMPORT_CAP (1ull << 20)   /* verified-compute import cap */
#define VNAH_UNIT_PRICE 1u             /* $YHVH units per served unit      */
#define VNAH_WANT_UNITS 4096u          /* record units bought per cycle    */
#define VNAH_MODEL_ID   1u             /* the node's own budget slot       */
#define VNAH_QUOTA_MS   3600000u       /* agreement quota period           */
#define VNAH_Q_RECORDS  2048u          /* records one peer draws / period  */
#define VNAH_Q_CHUNKS   4096u          /* file chunks one peer draws       */
#define VNAH_RETRY_MS   1000u          /* link resend interval             */
#define VNAH_TRIES      8u             /* link gives up after this many    */
#define VNAH_DOOR_PORT  80u            /* the seed's plain-http door       */
#define VNAH_DOOR_TX    1400u          /* door tx staging, one segment     */

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
    /* the trade loop and its tables */
    vna_book_t book;
    vna_account_t acct[VNAH_BOOK_CAP];
    vna_gate_t gate;
    uint8_t owner_secret[32];
    vna_agreement_t agr;
    vna_usage_t us;
    vna_usage_ent_t ue[VNAH_USAGE_CAP];
    vna_lease_t le[VNAH_LEASE_CAP];
    vna_link_t link;
    swarm_budget_t sb;
    const uint8_t (*seed_addr)[6]; /* peers table: the fleet, commons members */
    uint32_t nseed_addr;
    const char *door_src;                 /* the https base the door names at /boot.src, or 0 */
    uint64_t served_rec[VNAH_USAGE_CAP];  /* record units already sold, per slot */
    uint64_t served_file[VNAH_USAGE_CAP]; /* chunk units already sold, per slot  */
    uint64_t earned_posted;               /* held_in already handed to the bridge */
    uint64_t last_cycle;
    uint64_t imported_total; /* compute imported through the gate    */
    uint32_t sold, earn_cycles, refused_econ;
    uint32_t (*entropy)(void); /* a hardware u32 source; 0 when the CPU has none */
    uint32_t seeds, sent, recv, refused, dropped_tx, dropped_ob;
    /* the bus and the door: the shared L2 state space every seed opens on
     * the same canonical CID, and the plain-http front that serves the
     * doorway and carries browser deltas onto it (vna_door.h). Each door
     * socket is a listener: the stack accepts one peer per listener, so the
     * door holds VNA_DOOR_CONNS of them on the same port. door_slot maps a
     * socket to its door connection, -1 while still listening. */
    vna_spaces_t spaces;
    vna_door_t door;
    int32_t door_sock[VNA_DOOR_CONNS];
    int32_t door_slot[VNA_DOOR_CONNS];
    uint8_t door_tx[VNA_DOOR_CONNS][VNAH_DOOR_TX];
    uint16_t door_tx_len[VNA_DOOR_CONNS];
    uint16_t door_tx_off[VNA_DOOR_CONNS];
    uint8_t bus_cid[VNA_CID_RAW_LEN];
    uint8_t up;
} vna_host_t;

/* Bind the Vinea node to the link: identity derived from the NIC's MAC (each
 * machine keeps one NodeID across reboots and no two machines share one),
 * the node's tables in this host's arrays, the UDP hook on VNAH_PORT, then
 * one bootstrap ping per seed. `door_src` (may be NULL) is the https base a
 * browser learns at GET /boot.src — the gateways' root for boot.json and
 * boot.pack, which the door does not carry. 1 = running; 0 = refused
 * (logged). */
int vna_host_start(vna_host_t *h, sn_link_t *l, const uint8_t peers[][6], uint32_t npeer,
                   const char *door_src, uint32_t (*entropy)(void));

/* The idle-loop hook: drain the NIC (hooked datagrams reach the node inside
 * sn_pump), tick the node and the trade loop at VNAH_TICK_MS, run one settle
 * every VNAH_CYCLE_MS, and flush the outbox and the re-send ring to the
 * wire. */
void vna_host_pump(vna_host_t *h, uint64_t now);

/* The float this node has earned since the last call (peers' settle
 * DISTRIBUTEs paying our verified contribution), in $YHVH voucher units.
 * Returns the delta and marks it handed off: the caller's ledger posting is
 * the only other copy, so a caller that cannot post must keep the amount
 * and retry. */
uint64_t vna_host_take_earned(vna_host_t *h);

/* One serial line: node id, our transport address, table size, counters. */
void vna_host_report(const vna_host_t *h);

#endif /* ZXV_VNA_HOST_H */
