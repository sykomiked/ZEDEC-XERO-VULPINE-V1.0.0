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
 *      verifies it (ML-DSA-65, PoW, replay window) before acting on it; the
 *      node's outbox is sent after each call.
 *   U4 ADDRESSES. The Vinea transport address is 6 bytes: IPv4, then the
 *      port, both big-endian.
 *   U5 IDENTITY. A fresh ML-DSA-65 identity per start, seeded from the OS
 *      CSPRNG (no key is written to disk yet, so the NodeID changes at each
 *      start). The node runs at degree ROUTE with no sharing agreement: it
 *      answers PING / FIND_NODE / FIND_VALUE and refuses STORE and every
 *      Hackronomicon request.
 *
 *   U6 ECONOMY. Each start opens a mesh book (vna_econ.h), an owner gate and
 *      the trade loop (vna_link.h); the node hands every trade receipt to
 *      it. zxv_net_econ_cycle, called when the swarm closes a market cycle,
 *      turns this cycle's verified remote compute into the next cycle's
 *      budget rate and settles the book. With no sharing agreement attached
 *      the node sells nothing and wants nothing, so the loop imports 0 until
 *      the owner enables trading.
 *
 * HONEST LIMITS: no LAN multicast discovery yet (vna_lan.h needs a multicast
 * socket; peers are added by address with --peer or POST /api/net), no NAT
 * traversal, no IPv6, frames up to 13,700 bytes rely on IP fragmentation off
 * the loopback, no persisted peers or identity. Single-threaded.
 */
#ifndef ZXV_NET_HOST_H
#define ZXV_NET_HOST_H

#include <stdbool.h>
#include <stdint.h>
#include "swarm_budget.h"

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

/* U6: close the economic cycle: verified remote compute -> next cycle's rate
 * (base_rate + imported) on sb, then settle the book. Off: does nothing,
 * returns 0. 0, or -1 if the loop refused (the rate is then unchanged). */
int zxv_net_econ_cycle(zxv_net_t *n, swarm_budget_t *sb, uint64_t base_rate, uint64_t now_ms,
                       uint64_t *imported);

/* Wall-clock milliseconds (Vinea's replay window compares timestamps
 * between peers, so this is real time, not a monotonic clock). */
uint64_t zxv_net_wall_ms(void);

#endif /* ZXV_NET_HOST_H */
