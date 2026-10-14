/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_phone.h — the phone link: zxvd's JSON-RPC methods for the owner's
 * paired phones (mobile/companion), over the local network.
 *
 * The control socket (zxvd_rpc.h) is a UNIX socket and never leaves the
 * machine. A phone reaches the same methods through this second door, which
 * is closed unless zxvd is started with --phone-link.
 *
 * PAIRING
 *   P1  The owner pairs a phone over the control socket while the link is on
 *       (L1): phone.pair {name,
 *       allow_post?} returns a link "zxv-phone:1?h=HOST&p=PORT&id=ID&k=KEY&n=NAME"
 *       (ID 16 bytes and KEY 32 random bytes, both lowercase hex). The link is
 *       the phone's only secret: shown once, as text or a QR code, and never
 *       sent over the network by zxvd. phone.list shows the pairings (never a
 *       key); phone.revoke {phone_id} ends one and closes its connections.
 *   P2  Pairings live in "phones" beside the control socket (the folder
 *       zxvd_rpc.h T1 checks), written 0600 through a temporary file and
 *       rename. At most ZXVD_PHONE_MAX pairings.
 *
 * TRANSPORT (zxv-phone/1, TCP; a Noise NNpsk0-style handshake)
 *   L1  Off by default. --phone-link at start, or phone.link {on: true} over
 *       the control socket, listens on TCP ZXVD_PHONE_PORT (or --phone-port /
 *       "port") on every IPv4 interface, and accepts only private,
 *       link-local and loopback addresses (10/8, 172.16/12, 192.168/16,
 *       169.254/16, 127/8). Not on Windows yet.
 *   L2  Phone -> zxvd: "ZXP1" | id(16) | e_c(32), its fresh X25519 key. An
 *       unknown or revoked id is closed without an answer.
 *       zxvd -> phone: e_s(32) | confirm(32), with
 *         th      = SHA-256("zxv-phone/1" | id | e_c | e_s)
 *         prk     = HKDF-Extract(salt = KEY, ikm = X25519(e_s, e_c) | th)
 *         k_c2s | k_s2c | k_conf = HKDF-Expand(prk, "zxv-phone/1 keys", 96)
 *         confirm = HMAC-SHA256(k_conf, "server confirm" | th)
 *       The phone checks confirm (zxvd knows KEY); zxvd learns the phone knows
 *       KEY when its first frame opens. Fresh keys on both sides give forward
 *       secrecy; a replayed phone frame fails against a new e_s.
 *   L3  Frames both ways: len(4, big-endian) | ChaCha20-Poly1305(pt) | tag(16),
 *       nonce = 0(4) | seq(8, big-endian) counting from 0 per direction, AAD =
 *       the 4 length bytes. A frame that fails to open closes the connection.
 *       Phone frames hold one JSON-RPC request (at most ZXVD_RPC_MAX_LINE
 *       bytes); zxvd answers each with one frame holding the response (at
 *       most ZXVD_PHONE_MAX_OUT bytes). Notifications are refused.
 *   L4  At most ZXVD_PHONE_MAX_CONN connections; the handshake must finish in
 *       ZXVD_PHONE_HS_MS and a silent connection closes after ZXVD_RPC_IDLE_MS.
 *
 * WHAT A PHONE MAY CALL
 *   A1  Everything a view reads, swarm.ask and notes.mark_read.
 *   A2  Never zxvd.shutdown, phone.*, peer.serve or update installs.
 *   A3  Money and network changes (ledger.post, ledger.open_account,
 *       currency.enable, net.command, peer.ask) only when the pairing was made
 *       with allow_post; otherwise error ZXVD_E_PHONE.
 */
#ifndef ZXVD_PHONE_H
#define ZXVD_PHONE_H

#include <stdbool.h>
#include <stdint.h>

#include "zxvd_rpc.h"

#define ZXVD_PHONE_PORT     47410
#define ZXVD_PHONE_MAX      16
#define ZXVD_PHONE_MAX_CONN 4
#define ZXVD_PHONE_HS_MS    10000u
#define ZXVD_PHONE_MAX_OUT  (4u << 20)

/* Load the pairings from state_dir (P2) and serve phone.* on the control
 * socket. 0, or -1 with a reason on stderr. */
int zxvd_phone_init(const char *state_dir);
/* L1: start listening (also phone.link {on: true}); bind_ip NULL is every
 * interface. NULL, or why not. Already listening is not an error. */
const char *zxvd_phone_listen(const char *bind_ip, uint16_t port);
/* Stop listening and close every phone's connection (phone.link {on: false}). */
void zxvd_phone_unlisten(void);
/* The bound port (after port 0), or 0 when closed. */
uint16_t zxvd_phone_port(void);
/* Like zxvd_rpc_fds / zxvd_rpc_service, for the select loop. */
int zxvd_phone_fds(void *set);
void zxvd_phone_service(void *set, uint64_t now_ms);
void zxvd_phone_close(void);

/* phone.link, phone.pair, phone.list and phone.revoke reach this file
 * through the zxvd_phone_ops_t that zxvd_phone_init hands to zxvd_rpc.c (A1-A3
 * are enforced there, by zxvd_rpc_handle_phone_line). */

#endif
