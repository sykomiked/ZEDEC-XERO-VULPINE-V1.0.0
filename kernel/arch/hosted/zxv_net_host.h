/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_net_host.h — the hosted app's UDP glue for one Vinea node
 * (kernel/src/vinea/vna_node.h, a pure state machine with no sockets).
 *
 *   U1 OFF BY DEFAULT. Mode OFF opens no socket and sends nothing. Only the
 *      user turns networking on: the --net flag or the window's Network
 *      setting (POST /api/net). Nothing leaves the machine before that.
 *   U2 MODES.  LAN: the socket is bound (default 0.0.0.0) and datagrams are
 *      sent to and accepted from private addresses only (10/8, 172.16/12,
 *      192.168/16, 169.254/16, 127/8); anything else is dropped and counted.
 *      ONLINE: any IPv4 address. IPv4 only for now.
 *   U3 EVENT LOOP. One non-blocking UDP socket. The app adds zxv_net_fd() to
 *      the select() set it already uses for HTTP and calls zxv_net_on_readable
 *      when it is readable, and zxv_net_tick on every pass (RPC timeouts,
 *      lookups, republishing). Every datagram goes to vna_node_handle, which
 *      verifies it (ML-DSA-87, PoW, replay window) before acting on it; the
 *      node's outbox is sent after each call. Send and receive buffers are
 *      raised to 1 MiB, since macOS limits a datagram to the send buffer
 *      (9216 bytes by default) and a signed receipt is larger.
 *   U4 ADDRESSES. The Vinea transport address is 6 bytes: IPv4, then the
 *      port, both big-endian.
 *   U5 IDENTITY. With a state folder (zxv_net_set_state_dir; the app passes
 *      its own, see U9) the ML-DSA-87 identity is created once from the OS
 *      CSPRNG and kept, so the NodeID survives restarts. Without one, a
 *      fresh identity per start (the old behaviour; tests use it). The node
 *      runs at degree ROUTE with no sharing agreement: it answers PING /
 *      FIND_NODE / FIND_VALUE and refuses STORE and every Hackronomicon
 *      request.
 *
 *   U6 ECONOMY. Each start opens a mesh book (vna_econ.h), an owner gate and
 *      the trade loop (vna_link.h); the node hands every trade receipt to
 *      it. zxv_net_econ_cycle, called when the swarm closes a market cycle,
 *      turns this cycle's verified remote compute into the next cycle's
 *      budget rate and settles the book. With no sharing agreement attached
 *      the node sells nothing and wants nothing, so the loop imports 0 until
 *      the owner enables trading.
 *
 *   U7 TRADES. zxv_net_on_trade hands every applied trade (vna_link.h L5) to
 *      the app, which credits the owner's verified contributions (quest.h).
 *      The hook survives restarts of the node.
 *
 *   U8 OWNER TRADING. Only the owner, through the app's token-guarded API,
 *      shares or trades; nothing is shared by default. zxv_net_share sets
 *      what the owner shares (compute tokens per cycle, storage bytes): a
 *      sharing agreement (vna_agree.h) to everyone the node can reach (U2
 *      still applies), per-peer quota equal to the whole amount, rebuilt
 *      for each start's identity. zxv_net_want states a want (T2);
 *      zxv_net_sell offers a named peer the node has heard from (its key
 *      was verified) a trade on CREDIT (T1). Wants do not survive a
 *      restart. The node itself stays at degree ROUTE for DHT requests.
 *   U9 STATE FOLDER. Holds `vinea_identity` (the 32-byte seed the key pair
 *      is derived from, the NodeID it must give, and the last LAN record
 *      sequence) and `vinea_peers` (NodeID + address of up to 64 contacts).
 *      The folder is created 0700; on POSIX it is refused if group or others
 *      can write it, and the identity file if anyone but the owner can read
 *      it (like an SSH key). Files are written to a temporary name, synced
 *      and renamed, so a crash leaves the old file. A file whose seed does
 *      not give the NodeID it records is refused (the node does not start)
 *      rather than silently replaced. The seed is not encrypted at rest:
 *      anyone who can read the file can act as this node (Keychain storage is
 *      a follow-up). On start the cached peers are pinged (the reply proves
 *      who answers); on stop and every 5 minutes the table is saved. One
 *      process at a time holds the folder (a lock on `vinea.lock`); a second
 *      instance on the same folder runs with a fresh identity and keeps
 *      nothing (status state_busy), so two never share a NodeID.
 *   U10 LAN DISCOVERY. In LAN and ONLINE modes (unless turned off) a second
 *      UDP socket joins the administratively scoped group 239.255.90.86 on
 *      port 8724 (TTL 1, never routed) and runs vna_lan.h: signed node
 *      records announced and answered, so nodes on one link find each other
 *      with no address typed. The record carries this node's LAN address and
 *      Vinea port. A persisted identity resumes its record sequence, so
 *      peers never refuse its records as replays after a restart.
 *   U11 NAT (basic). Every PONG reports the sender's address as the peer saw
 *      it; the status shows the public address two or more peers agree on.
 *      zxv_net_punch asks contacts to introduce this node to a NodeID behind
 *      another NAT (UDP hole punching, vna_node.h N2). In ONLINE mode contacts
 *      idle for 25 s are pinged so NAT mappings stay open. No port mapping
 *      (NAT-PMP / PCP / UPnP) yet, and a symmetric NAT on both sides still
 *      cannot be traversed without a relay.
 *   U12 AIPI. zxv_net_attach_aipi binds an AIPI context (src/aipi) to the
 *      node through aipi_vna.h: calls and claims travel as signed Vinea
 *      messages. Bounties: the owner sets an escrow budget
 *      (zxv_net_aipi_budget, default 0: only free calls); an accepted claim's
 *      bounty becomes a want for one compute receipt on credit bound to that
 *      executor (vna_link_want_from), and when an executor is told it was
 *      paid, it offers that receipt (only if the owner shares compute). Both
 *      books then apply it through the trade loop (U6).
 *   U13 LISTINGS. zxv_net_peers lists the routing table's contacts (every one
 *      answered a signed message, so its NodeID is verified), each joined
 *      with its mesh-book account when there is one. zxv_net_wants and
 *      zxv_net_pending list the open wants and the receipts still waiting on
 *      the other side (a SELL is an offer the buyer has not accepted yet; a
 *      BUY is a countersigned copy the seller has not committed). Each
 *      returns how many exist (not just how many fit) and 0 when off.
 * HONEST LIMITS: no IPv6; frames up to about 17.6 KB rely on IP
 * fragmentation off the loopback; TCP sessions (zxv_peer_host.h) are not
 * hole-punched. Single-threaded.
 */
#ifndef ZXV_NET_HOST_H
#define ZXV_NET_HOST_H

#include <stdbool.h>
#include <stdint.h>
#include "swarm_budget.h"
#include "vna_link.h"
#include "aipi_vna.h"

typedef enum { ZXV_NET_OFF = 0, ZXV_NET_LAN, ZXV_NET_ONLINE } zxv_net_mode_t;

#define ZXV_NET_DEFAULT_PORT 8723u

typedef struct zxv_net zxv_net_t;

typedef struct {
    zxv_net_mode_t mode;
    bool bound;
    uint16_t port;
    char bind_ip[16];
    char node_id[17]; /* first 8 bytes of the NodeID, hex; "" when off */
    uint32_t peers;   /* contacts in the routing table */
    uint32_t datagrams_in, datagrams_out, refused_in, policy_drops, send_errors;
    char error[96]; /* why the last start failed, or "" */
    /* U6: the mesh economy */
    uint32_t trades;           /* receipts committed (as seller) or confirmed (as buyer) */
    uint32_t receipts_refused; /* by the node or the trade loop */
    uint64_t imported;         /* internal rate added from verified remote compute */
    bool conserved;            /* the book's conservation check */
    /* U8 */
    char node_id_full[65]; /* the whole NodeID, hex; "" when off */
    uint64_t share_compute, share_storage;
    uint32_t wants; /* open wants */
    /* U9 */
    bool persisted;        /* the identity came from (or went to) the state folder */
    bool state_busy;       /* another process holds the state folder: fresh identity */
    uint32_t cached_peers; /* peers loaded from the cache at start */
    /* U10 */
    bool lan_discovery;                                /* the multicast socket is open */
    uint32_t lan_peers;                                /* peers heard on the LAN, not expired */
    uint32_t lan_announces, lan_accepted, lan_refused; /* refused: malformed or forged */
    /* U11 */
    char public_addr[22]; /* "a.b.c.d:port" two or more peers agree on, or "" */
    uint32_t public_votes;
    uint32_t punch_asked, punch_relayed, punch_ok;
    /* U12 */
    bool aipi;
    uint32_t aipi_calls_out, aipi_served, aipi_claims_in, aipi_claims_refused;
    uint64_t aipi_escrow, aipi_budget;
} zxv_net_status_t;

/* A new, stopped (OFF) host; NULL if out of memory. */
zxv_net_t *zxv_net_new(void);
void zxv_net_free(zxv_net_t *n);

/* Turn networking on in mode LAN or ONLINE (OFF stops it). bind_ip NULL means
 * 0.0.0.0; port 0 picks a free one. Restarting with another mode keeps
 * nothing. 0, or -1 with the reason in the status. */
int zxv_net_start(zxv_net_t *n, zxv_net_mode_t mode, const char *bind_ip, uint16_t port,
                  uint64_t now_ms);
void zxv_net_stop(zxv_net_t *n);

/* The socket to add to select()'s read set, or -1 when off. On Windows the
 * value is the SOCKET cast to intptr_t. */
intptr_t zxv_net_fd(const zxv_net_t *n);

/* U3: drain the readable socket (at most 64 datagrams per call). */
void zxv_net_on_readable(zxv_net_t *n, uint64_t now_ms);
/* U3: timeouts, lookups and republishing; cheap to call often. */
void zxv_net_tick(zxv_net_t *n, uint64_t now_ms);

/* Ping a peer by address (Kademlia join on its answer). "a.b.c.d" and port.
 * 0, or -1 (off, bad address, or refused by the LAN policy). */
int zxv_net_add_peer(zxv_net_t *n, const char *ip, uint16_t port, uint64_t now_ms);
/* "a.b.c.d:port" form of the same. */
int zxv_net_add_peer_str(zxv_net_t *n, const char *ip_port, uint64_t now_ms);

void zxv_net_status(const zxv_net_t *n, zxv_net_status_t *st);
const char *zxv_net_mode_name(zxv_net_mode_t m);
/* "off" / "lan" / "online" -> mode; -1 if none of them. */
int zxv_net_mode_parse(const char *s, zxv_net_mode_t *m);

/* FIND_NODE: start an iterative lookup of the 32-byte NodeID. Returns the
 * lookup slot (>= 0) or negative. */
int32_t zxv_net_find_node(zxv_net_t *n, const uint8_t id[32], uint64_t now_ms);
/* -1 still running; when done (the slot is then released): 1 if the target
 * itself answered, else 0. */
int zxv_net_find_result(zxv_net_t *n, int32_t slot);
/* This node's full 32-byte NodeID (zeros when off). */
void zxv_net_node_id(const zxv_net_t *n, uint8_t out[32]);

/* True when this host knows a contact at ip:port (tests). */
bool zxv_net_knows(const zxv_net_t *n, const char *ip, uint16_t port);
/* The verified NodeID of the contact at ip:port: 0, or -1 if none. */
int zxv_net_contact_id(const zxv_net_t *n, const char *ip, uint16_t port, uint8_t id[32]);
/* This start's identity (zxv_peer_host.h uses it for TCP sessions), or NULL
 * when off. The pointer stays valid across restarts; the key changes. */
const vna_identity_t *zxv_net_identity(const zxv_net_t *n);

/* U6: close the economic cycle: verified remote compute -> next cycle's rate
 * (base_rate + imported) on sb, then settle the book. Off: does nothing,
 * returns 0. 0, or -1 if the loop refused (the rate is then unchanged). */
int zxv_net_econ_cycle(zxv_net_t *n, swarm_budget_t *sb, uint64_t base_rate, uint64_t now_ms,
                       uint64_t *imported);

/* Wall-clock milliseconds (Vinea's replay window compares timestamps
 * between peers, so this is real time, not a monotonic clock). */
uint64_t zxv_net_wall_ms(void);

/* U7: hear every trade this node applies (NULL clears). */
void zxv_net_on_trade(zxv_net_t *n, vna_link_done_fn fn, void *ctx);

/* U8: what the owner shares: compute tokens per cycle and storage bytes
 * (both 0: share nothing). Kept across restarts. 0, or -1. */
int zxv_net_share(zxv_net_t *n, uint64_t compute_per_cycle, uint64_t storage_bytes);
/* U8: want `units` of VNA_RES_COMPUTE or VNA_RES_STORAGE, at most
 * `max_price` per receipt. The want slot (>= 0), or -1 (off, full, bad). */
int32_t zxv_net_want(zxv_net_t *n, uint8_t resource, uint64_t units, uint64_t max_price);
/* U8: offer `units` of compute or storage to the peer with this NodeID for
 * `price` on credit. 0 when offered (the trade completes when the buyer
 * accepts), -1 when off, unknown peer, or refused (agreement, L1). */
int zxv_net_sell(zxv_net_t *n, const uint8_t buyer[32], uint8_t resource, uint64_t units,
                 uint64_t price, uint64_t now_ms);

/* U9: the folder for the identity and the peer cache; NULL or "" keeps
 * nothing (a fresh identity per start). Takes effect at the next start. */
int zxv_net_set_state_dir(zxv_net_t *n, const char *dir);
/* U9: write the peer cache now (also done on stop and every 5 minutes). */
int zxv_net_save_peers(zxv_net_t *n);

/* U10: LAN discovery on (default) or off; port 0 = 8724. Next start. */
void zxv_net_set_lan_discovery(zxv_net_t *n, bool on, uint16_t port);
/* U10: the discovery socket for select()'s read set, or -1. It is also
 * drained on every zxv_net_on_readable and zxv_net_tick. */
intptr_t zxv_net_lan_fd(const zxv_net_t *n);

/* U11: ask up to three contacts to introduce this node to `target`.
 * Returns how many were asked (0: off, or no contact to ask). */
int zxv_net_punch(zxv_net_t *n, const uint8_t target[32], uint64_t now_ms);

/* U12: bind an AIPI context to the node (NULL detaches). Kept across
 * restarts; while the node is off the context has no mesh (local only). */
int zxv_net_attach_aipi(zxv_net_t *n, aipi_ctx_t *ai);
/* U12: the adapter (executors, serving, announce), or NULL when off or
 * nothing is attached. */
aipi_vna_t *zxv_net_aipi(zxv_net_t *n);
/* U12: the most this node keeps in escrow for bounties at once (0: only
 * free calls). Kept across restarts. */
void zxv_net_aipi_budget(zxv_net_t *n, uint64_t max_escrow);
/* U12: serve calls from peers (needs zxv_net_share compute > 0 too). */
void zxv_net_aipi_serve(zxv_net_t *n, bool on, uint64_t min_bounty);
/* U12: aipi_vna_execute, then send what it queued. */
int zxv_net_aipi_execute(zxv_net_t *n, const ipfsn_cid_t *fn_cid, const uint8_t *in,
                         uint32_t in_len, bool input_private, uint64_t bounty, uint64_t ttl_ms,
                         uint64_t now_ms, uint32_t *job);

/* U13 */
typedef struct {
    char id[65];    /* NodeID, hex */
    char addr[22];  /* "a.b.c.d:port"; "" for a non-IPv4 address */
    uint8_t bucket; /* Kademlia bucket */
    bool ubh;       /* the peer advertised UBH-168 */
    uint64_t last_seen_ms;
    bool in_book;        /* the mesh book has an account for it */
    bool member;         /* admitted to the commons by the owner */
    uint64_t held;       /* units issued by the peer, held by this node */
    uint64_t receivable; /* delivered to the peer on credit */
    uint64_t social;     /* Crown: earned, never moved */
    uint32_t pending;    /* receipts outstanding with this peer */
} zxv_net_peer_t;
uint32_t zxv_net_peers(const zxv_net_t *n, zxv_net_peer_t *out, uint32_t cap);

typedef struct {
    uint32_t slot;
    uint8_t resource; /* VNA_RES_COMPUTE or VNA_RES_STORAGE */
    uint64_t units_left, max_price;
} zxv_net_want_info_t;
uint32_t zxv_net_wants(const zxv_net_t *n, zxv_net_want_info_t *out, uint32_t cap);

typedef struct {
    bool selling;  /* SELL: offered, buyer not yet accepted; else BUY */
    char peer[65]; /* NodeID, hex */
    uint64_t units, pair_seq;
    uint32_t tries;
    int32_t want; /* BUY: the want slot it reserves from; -1 for SELL */
} zxv_net_pend_info_t;
uint32_t zxv_net_pending(const zxv_net_t *n, zxv_net_pend_info_t *out, uint32_t cap);

#endif /* ZXV_NET_HOST_H */
