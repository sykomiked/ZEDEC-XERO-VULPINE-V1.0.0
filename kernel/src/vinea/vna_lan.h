/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_lan.h — LAN peer discovery for Vinea, mDNS / DNS-SD style.
 *
 * Lets nodes on one link find each other with no internet, no bootstrap
 * server and no configuration: a LAN-only island forms a working DHT by
 * itself, and islands that later meet (a cable, a Wi-Fi join, one cached peer)
 * merge through ordinary Kademlia lookups.
 *
 * Like the rest of Vinea this is a pure state machine: no sockets. The host
 * owns the multicast socket (suggested: an administratively scoped group such
 * as 239.255.90.86 / ff02::5a56 on a fixed port; the group is an opaque
 * address token here) and hands datagrams in and out.
 *
 * SERVICE "_zxv._udp" (DNS-SD service type). The behaviour follows RFC 6762 /
 * RFC 6763; the BYTES do not: messages are Vinea schema records, not DNS
 * resource records, so a stock mDNS responder neither parses nor answers
 * them (a host that wants classic DNS-SD visibility can additionally publish a
 * PTR/SRV/TXT for _zxv._udp pointing at its port; Vinea never relies on it).
 *
 * MESSAGE (schema vna_lan_msg_schema, little-endian)
 *   magic "VND2" | version | kind | service (VAR, must be "_zxv._udp") |
 *   qid u64 | known[] (ARR of NodeIDs, QUERY only) | record (VAR, ANNOUNCE only)
 * QUERY    "who offers _zxv._udp?"; carries the NodeIDs the querier already
 *          knows (known-answer suppression, RFC 6762 §7.1): those stay quiet.
 * ANNOUNCE one SIGNED node record (vna_noderec_t, vna_wire.h): NodeID, pk,
 *          PoW nonce, addresses, seq, lifetime, ML-DSA-65 signature
 *          (context "vinea/v2/node-record"). The LAN wrapper itself is
 *          unsigned and carries no authority; everything a receiver acts on
 *          is inside the signed record. A GOODBYE is an announce whose record
 *          has VNA_NR_GOODBYE set (so it is signed too and cannot be forged to
 *          knock a peer off).
 *
 * STATE MACHINE (times from the caller; RFC 6762 §8.3 and §5.2 in spirit)
 *   start:  sign a record, send one QUERY and one ANNOUNCE at once, then
 *           `burst - 1` more announcements at 1 s, 2 s, 4 s ... and queries at
 *           1 s, 2 s ... (doubling), then re-announce every steady_ms.
 *   record: re-signed with seq + 1 when half its lifetime is gone.
 *   query:  answered after a random 20..120 ms delay unless our id is in the
 *           known list; at most one answer per answer_min_ms (rate limit).
 *   receive ANNOUNCE: refuse our own id and any seq not newer than the one
 *           already held (replay / reordering) BEFORE any signature work,
 *           then verify the record (binding, PoW, signature, created not in
 *           the future beyond skew, not expired), then seed the node's routing table
 * (vna_node_seed_verified). A GOODBYE removes the peer from the LAN table and routing table. stop:
 * send a GOODBYE.
 *
 * The per-frame transform hook (vna_xform_t) applies to LAN datagrams too, so
 * a private network (src/ehop) can hide its discovery traffic from others on
 * the same link.
 */
#ifndef VNA_LAN_H
#define VNA_LAN_H

#include "vna_node.h"

#define VNA_LAN_MAGIC   0x32444E56u /* "VND2" */
#define VNA_LAN_SERVICE "_zxv._udp"
#define VNA_LAN_SVC_MAX 32u
#define VNA_LAN_KNOWN   32u
#define VNA_LAN_MSG_MAX (VNA_NODEREC_MAX + 64u + VNA_LAN_KNOWN * 32u)

typedef enum { VNA_LAN_QUERY = 1, VNA_LAN_ANNOUNCE = 2 } vna_lan_kind_t;

typedef struct {
    vna_id_t id;
} vna_lan_known_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t kind;
    uint16_t svc_len;
    uint8_t svc[VNA_LAN_SVC_MAX];
    uint64_t qid;
    uint16_t nknown;
    vna_lan_known_t known[VNA_LAN_KNOWN];
    uint16_t rec_len;
    uint8_t rec[VNA_NODEREC_MAX];
} vna_lan_msg_t;

extern const vna_schema_t vna_lan_msg_schema;

typedef struct {
    uint64_t ttl_ms;        /* lifetime of our node record (default 120 s) */
    uint64_t steady_ms;     /* re-announce interval after the burst (default 60 s) */
    uint32_t burst;         /* initial announcements (default 3) */
    uint32_t query_burst;   /* initial queries (default 3) */
    uint64_t skew_ms;       /* clock skew accepted on records (default 30 s) */
    uint64_t answer_min_ms; /* at most one answer per this (default 1 s) */
    uint8_t flags;          /* record flags, e.g. VNA_NR_HIGHCAP (advisory only) */
} vna_lan_cfg_t;

void vna_lan_cfg_default(vna_lan_cfg_t *c);

typedef struct {
    bool used;
    vna_id_t id;
    uint64_t seq;
    uint64_t expires;
    uint8_t flags;
} vna_lan_peer_t;

typedef struct {
    vna_node_t *node;
    vna_lan_cfg_t cfg;
    uint8_t group[VNA_ADDR_MAX];
    uint8_t group_len;
    vna_addr_el_t self_addr[VNA_NR_ADDRS];
    uint16_t naddr;
    vna_lan_peer_t *peers;
    uint32_t peer_cap;
    vna_drbg_t rng;
    const vna_xform_t *xform;
    bool running;
    uint64_t rec_seq, rec_created, rec_expires;
    uint16_t rec_len;
    uint8_t rec[VNA_NODEREC_MAX];
    uint64_t next_announce, announce_gap;
    uint32_t announces_left;
    uint64_t next_query, query_gap;
    uint32_t queries_left;
    bool answer_due;
    uint64_t answer_at, last_answer, answer_qid;
    /* the record of the peer accepted last (for the host's peer cache) */
    uint16_t last_len;
    uint8_t last_rec[VNA_NODEREC_MAX];
    /* scratch */
    vna_lan_msg_t msg;
    vna_noderec_t nr;
    uint8_t xbuf[VNA_LAN_MSG_MAX + VNA_XFORM_MAX_OVERHEAD];
    /* statistics */
    uint32_t sent_query, sent_announce, accepted, suppressed, rate_limited, goodbyes;
    uint32_t rej_parse, rej_record, rej_old, rej_self, rej_xform;
} vna_lan_t;

/* group: the host's multicast address token (outbox entries carry it).
 * addrs: this node's own reachable addresses, put in its record. peers: the
 * caller's table of LAN peers. */
vna_status_t vna_lan_init(vna_lan_t *l, vna_node_t *node, const vna_lan_cfg_t *cfg,
                          const uint8_t *group, uint32_t group_len, const vna_addr_el_t *addrs,
                          uint32_t naddr, vna_lan_peer_t *peers, uint32_t peer_cap,
                          const uint8_t seed[32]);
void vna_lan_set_xform(vna_lan_t *l, const vna_xform_t *x);

vna_status_t vna_lan_start(vna_lan_t *l, uint64_t now, vna_outbox_t *ob);
void vna_lan_tick(vna_lan_t *l, uint64_t now, vna_outbox_t *ob);
/* A datagram that arrived on the LAN discovery socket. The node may queue
 * DHT messages (the join lookup) into ob as well. */
vna_status_t vna_lan_handle(vna_lan_t *l, const uint8_t *buf, uint32_t len, uint64_t now,
                            vna_outbox_t *ob);
vna_status_t vna_lan_stop(vna_lan_t *l, uint64_t now, vna_outbox_t *ob);

uint32_t vna_lan_peer_count(const vna_lan_t *l, uint64_t now);

#endif /* VNA_LAN_H */
