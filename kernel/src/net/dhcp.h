/* dhcp.h — DHCP client: how ZXV gets an address on a network it has never seen
 *
 * WHY THIS EXISTS
 * ---------------
 * The stack previously hard-coded 10.0.2.15 because net_dhcp_discover() was an
 * empty stub whose comment said "Would build and send DHCP DISCOVER". A machine
 * that can only work on a network whose numbering it was told in advance is not
 * networked. This is the real thing.
 *
 * THE EXCHANGE (RFC 2131)
 * -----------------------
 *   DISCOVER  broadcast, "is anyone offering?"      client -> 255.255.255.255
 *   OFFER     a server proposes an address          server -> client
 *   REQUEST   broadcast, "I accept THAT one, from THAT server"
 *   ACK       the lease is committed                server -> client
 *
 * REQUEST is broadcast rather than unicast on purpose: every server that made
 * an offer must see which one was accepted, so the losers can release their
 * reservations. Skipping that is a common bug that leaks addresses on a network
 * with redundant servers.
 *
 * DESIGN
 * ------
 * Message BUILDING and PARSING are pure functions over byte buffers, with no
 * I/O, so the wire format is fully host-testable against a real capture. The
 * caller owns the transport. State lives in one dhcp_client_t so a caller can
 * drive the exchange from an event loop without blocking.
 *
 * SAFETY
 * ------
 * Options arrive from an untrusted server. Every option walk is bounded by the
 * buffer length AND by each option's own length byte, and a malformed or
 * truncated option list terminates the walk rather than running off the end.
 *
 * Freestanding: integer only, no libc, no allocation.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV DHCP slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_DHCP_H
#define ZXV_DHCP_H

#include <stdint.h>
#include <stdbool.h>

#define DHCP_SERVER_PORT   67u
#define DHCP_CLIENT_PORT   68u
#define DHCP_MAGIC         0x63825363u
#define DHCP_MIN_LEN       240u    /* fixed header through the magic cookie */
#define DHCP_MAX_LEN       576u

/* message types (option 53) */
#define DHCP_DISCOVER 1u
#define DHCP_OFFER    2u
#define DHCP_REQUEST  3u
#define DHCP_DECLINE  4u
#define DHCP_ACK      5u
#define DHCP_NAK      6u
#define DHCP_RELEASE  7u

/* options we care about */
#define DHCP_OPT_SUBNET     1u
#define DHCP_OPT_ROUTER     3u
#define DHCP_OPT_DNS        6u
#define DHCP_OPT_REQUESTED  50u
#define DHCP_OPT_LEASE      51u
#define DHCP_OPT_MSGTYPE    53u
#define DHCP_OPT_SERVERID   54u
#define DHCP_OPT_PARAMLIST  55u
#define DHCP_OPT_END        255u

typedef enum {
    DHCP_STATE_INIT = 0,
    DHCP_STATE_SELECTING,   /* DISCOVER sent, waiting for an OFFER  */
    DHCP_STATE_REQUESTING,  /* REQUEST sent, waiting for an ACK     */
    DHCP_STATE_BOUND,       /* we hold a lease                      */
    DHCP_STATE_FAILED       /* NAK, or the exchange gave up         */
} dhcp_state_t;

typedef struct {
    dhcp_state_t state;
    uint32_t xid;             /* transaction id — ties replies to our request */
    uint8_t  mac[6];
    uint8_t  offered_ip[4];
    uint8_t  server_id[4];
    /* filled once BOUND */
    uint8_t  ip[4];
    uint8_t  netmask[4];
    uint8_t  gateway[4];
    uint8_t  dns[4];
    uint32_t lease_secs;
} dhcp_client_t;

void dhcp_init(dhcp_client_t *c, const uint8_t mac[6], uint32_t xid);

/* Build a DISCOVER into `out`. Returns the length written, 0 on error.
 * Moves the client to SELECTING. */
uint32_t dhcp_build_discover(dhcp_client_t *c, uint8_t *out, uint32_t cap);

/* Build a REQUEST for the address that was offered. Returns length, 0 on
 * error (e.g. nothing has been offered yet). Moves to REQUESTING. */
uint32_t dhcp_build_request(dhcp_client_t *c, uint8_t *out, uint32_t cap);

/* Feed a received DHCP message (the UDP payload). Advances the state machine:
 * an OFFER while SELECTING records the address; an ACK while REQUESTING binds
 * the lease; a NAK fails. Returns the message type seen, or 0 if the message
 * is not for us / malformed. */
uint32_t dhcp_input(dhcp_client_t *c, const uint8_t *msg, uint32_t len);

/* Read one option out of a message. Returns the value length and sets *val,
 * or 0 if absent. Bounded and safe on malformed input. */
uint32_t dhcp_get_option(const uint8_t *msg, uint32_t len, uint8_t opt,
                         const uint8_t **val);

bool dhcp_is_bound(const dhcp_client_t *c);

#endif /* ZXV_DHCP_H */
