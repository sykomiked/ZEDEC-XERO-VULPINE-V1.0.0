/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_irc.h — irc.*: zxvd's IRC gateway, a door into the swarm for any IRC
 * client (docs/IRC_GATEWAY.md). The protocol is zxv_irc.h (E1-E9); this file
 * is the transport, the keys, the signing and the zxvd group.
 *
 * SWITCH AND DOOR
 *   G1  The group has the feature "irc", not regulated, OFF by default
 *       (zxvd_ext.h X3). While it is on, zxvd listens on 127.0.0.1 only
 *       (TCP ZXVD_IRC_PORT, or --irc-port), TLS only: there is no plain-text
 *       port. Turning it off closes every connection and the bridge.
 *       Connections from any address but 127.0.0.1 are closed unread.
 *   G2  TLS 1.3 by the repo's own server (src/tls/server.h): ChaCha20-
 *       Poly1305, x25519, a self-signed P-256 certificate for localhost,
 *       127.0.0.1 and ::1 (src/tls/selfsigned.h). A client trusts it by
 *       pinning: the certificate file ("irc-cert.pem") as its CA, or its
 *       SHA-256 fingerprint from irc.status. The handshake must finish in
 *       ZXVD_IRC_HS_MS.
 *   G3  The gateway password (PASS, zxv_irc.h E2) is 32 random hex digits;
 *       irc.status shows it, and only the control socket (owner-only) can
 *       call irc.status.
 *   G4  Files, in the control socket's folder (owner-only, checked by
 *       zxvd_rpc_open; in memory and new each start without one):
 *       "irc.key" (the certificate's P-256 key, 32 bytes), "irc.der" and
 *       "irc-cert.pem" (the certificate, ten years), "irc.pass" and
 *       "irc.seed" (G5), each written 0600 through a temporary file. A
 *       damaged file is replaced, never half-used.
 *
 * SIGNED AT THE EDGE (zxv_irc.h E7)
 *   G5  Every message that passes the gateway is signed with ML-DSA-87
 *       (FIPS 204, context "zxv/irc/v1/msg") over the record
 *         "zxv-irc/1\n" target "\n" nick "\n" truth "\n" ms "\n" text
 *       by the node identity while the mesh node runs (zxvd_irc_set_node),
 *       else by the gateway's own ML-DSA-87 key from "irc.seed". The signer
 *       id is SHA3-256 of the public key (vna_id.h I2). msgid = the first 16
 *       bytes of SHA3-256(record || signature), hex. The newest
 *       ZXVD_IRC_RECORDS records are kept for irc.record.
 *
 * WHAT IT CARRIES (and what it never does)
 *   G6  #swarm is reserved for the agent "swarm" (zxv_irc.h E8): a PRIVMSG
 *       there, or to the nick swarm, is a question to the swarm (swarm.ask);
 *       the answer comes back signed, its tag the base of the answer's
 *       64-state tag (zxv_answer_tag.h: it attests how the computation went,
 *       never that the answer is true), N when no model answered. Model
 *       answers stay advisory.
 *   G7  Nothing else is reachable from IRC: no money, no keys, no Player
 *       Card, no ledger, no settings. The gateway calls the host's ask hook
 *       and nothing more.
 *
 * BRIDGE (off by default)
 *   G8  irc.bridge {on: true, host, port = 6697, remote: "#chan", local:
 *       "#chan", nick, password?} links one local channel to one channel on a public
 *       IRC network over TLS, the server verified against the built-in roots
 *       and the clock (TLS_VERIFY_REQUIRED; a network whose certificate does
 *       not validate is refused). The local channel becomes reserved for the
 *       agent "bridge". Local messages go out as "<nick> [T] text" at most
 *       one line per ZXVD_IRC_BRIDGE_MS (a queue of ZXVD_IRC_BRIDGE_Q;
 *       overflow is dropped and counted); remote lines come in as
 *       "nick|bridge" with their stated tag, signed by this node like any
 *       message. {on: false} ends it. A bridge lasts until zxvd stops: it is
 *       never saved, and it needs the switch on.
 *
 * METHODS
 *   irc.status  {} always -> {on, listening, port, clients, password,
 *               cert_sha256, cert_file, signer, signer_id, delivered,
 *               refused, bridge: null | {state, host, port, remote, local,
 *               nick, sent, received, dropped, error}}
 *   irc.record  {msgid} always -> {msgid, target, nick, truth, ms, text,
 *               signer_id, public_key, signature, record} (hex where binary)
 *   irc.bridge  G8, behind the switch -> the bridge object of irc.status
 *   Errors: ZXVD_E_IRC (-32096) with the reason; bad parameters -32602.
 *
 * Not on Windows yet: the group is listed and irc.status answers, the switch
 * opens nothing.
 */
#ifndef ZXVD_IRC_H
#define ZXVD_IRC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zxvd_ext.h"

#define ZXVD_E_IRC         (-32096) /* zxvd_rpc.h X4: inside -32099..-32001 */
#define ZXVD_IRC_PORT      6697
#define ZXVD_IRC_HS_MS     10000u
#define ZXVD_IRC_RECORDS   256u
#define ZXVD_IRC_BRIDGE_MS 2000u
#define ZXVD_IRC_BRIDGE_Q  32u
#define ZXVD_IRC_PK_LEN    2592u /* ML-DSA-87 */
#define ZXVD_IRC_SK_LEN    4896u

const zxvd_group_t *zxvd_irc_group(void);

/* G4: dir for the files ("" or NULL: memory only); port 0 = any free port
 * (tests). 0, or -1 with the reason on stderr. Listening starts when the
 * switch is on (zxvd_irc_service). */
int zxvd_irc_open(const char *dir, uint16_t port);
void zxvd_irc_close(void);

/* G6: the swarm. ask writes the answer and its truth ('T','F','B','N'). */
void zxvd_irc_set_ask(void (*ask)(const char *prompt, char *out, size_t cap, char *truth));
/* G5: the node identity while it runs: fills pk/sk pointers, or returns
 * false (the mesh is off). NULL: always the gateway key. */
void zxvd_irc_set_node(bool (*node)(const uint8_t **pk, const uint8_t **sk));

/* For the select loop, like zxvd_rpc_fds / zxvd_rpc_service. */
int zxvd_irc_fds(void *set);
void zxvd_irc_service(void *set, uint64_t now_ms);
/* The bound port while listening, else 0. */
uint16_t zxvd_irc_port(void);

/* Unit tests only: where random bytes come from (NULL = the OS), the wall
 * clock in ms (NULL = time()), and the bridge's trust: a certificate pinned
 * instead of the built-in roots (NULL: the roots). */
void zxvd_irc_set_random(int (*fn)(uint8_t *p, size_t n));
void zxvd_irc_set_clock(uint64_t (*fn)(void));
void zxvd_irc_bridge_pin(const uint8_t *der, uint32_t len);

#endif
