/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_dns.h — names and addresses (Signpost): dns.resolve, dns.reverse,
 * dns.flush, did.parse, did.match.
 *
 *   D1 RESOLVE. dns.resolve {"name", "family"?: "any" | "ipv4" | "ipv6"}
 *      asks the OS resolver (getaddrinfo: the hosts file, the system's DNS
 *      servers, and whatever else the OS consults) on a thread beside
 *      zxvd's loop, never in it: a lookup can take seconds. It answers at
 *      once with "state": "pending" while the lookup runs (ask again),
 *      "done" with "addrs" (up to ZXVD_DNS_ADDRS, each {"addr", "family"}),
 *      or "failed" with "error": "no such name", "no answer now" (the
 *      resolver could not reach a server; try later) or "failed". A name
 *      that is already an address (1.2.3.4, ::1) is answered at once with
 *      no lookup.
 *   D2 CACHE. An answer is kept ZXVD_DNS_KEEP_MS, a failure
 *      ZXVD_DNS_FAIL_MS: getaddrinfo gives no record TTLs, so these are
 *      zxvd's own. "ttl_ms" says how long the answer has left. Asked after
 *      that, the name is looked up again. At most ZXVD_DNS_ENTRIES names
 *      and addresses are held; a new one takes the place of the answer used
 *      longest ago. When every place holds a lookup still to run, a new name
 *      is refused (ZXVD_E_DNS, "busy"). dns.flush forgets every answer;
 *      lookups still running are kept.
 *   D3 REVERSE. dns.reverse {"addr"} gives the name the resolver has for an
 *      address (getnameinfo, a name required), with the same states, cache
 *      and errors; "name" when done.
 *   D4 NAMES. 1 to 253 bytes in labels of 1 to 63 letters, digits, '-' and
 *      '_', no label starting or ending with '-'; one trailing dot is
 *      allowed and dropped; letters are compared case-blind (the cache key
 *      is lower case). Anything else is refused (-32602) before any lookup.
 *   D5 LIMITS. At most ZXVD_DNS_THREADS lookups run at once; more wait in
 *      their places ("pending"). A lookup is never cut short (the OS
 *      resolver has its own time limits), so zxvd_dns_close waits for the
 *      running ones.
 *   D6 TRUST. The control socket only. dns.* is off until the host turns it
 *      on (zxvd_dns_enable), so a test or a fuzzer that sends dns.resolve
 *      gets ZXVD_E_DNS and makes no lookup. Not on Windows yet.
 *
 *   I1 did.parse {"did"} reads a DID with no network. "did:key:z6Mk..."
 *      (an Ed25519 key: multibase base58btc of the multicodec 0xed 0x01 and
 *      the 32-byte key) gives "key" (hex). "did:zxv:" and 52 lower-case
 *      base32 letters (kernel/src/ident: SHA3-256("ZXV-ident/did" || the
 *      key's encoding)) gives "id" (hex). Both answer "supported": true.
 *      Any other well-formed "did:<method>:<id>" answers "supported":
 *      false with its method. A malformed DID, or a did:key or did:zxv
 *      whose id does not decode exactly (no spare bits, no other key type),
 *      is refused (ZXVD_E_DNS).
 *   I2 did.match {"did", "key"} says whether a key belongs to a DID: for
 *      did:key the key must be the one it carries, for did:zxv its hash
 *      must be the id. "key" is hex: the 32 bytes of an Ed25519 key, or for
 *      did:zxv the key's encoding as cards and passkeys carry it
 *      (pqm_sig_pk_encode), at most ZXVD_DID_KEY_MAX bytes. Only the bytes
 *      are checked, not that they make a valid key.
 *   I3 NOT HERE. Finding the key of a did:zxv from its id needs the peer
 *      directory, which is the peer identity's records (Vinea). did:web
 *      needs an HTTPS fetch. did.parse answers "supported": false for
 *      did:web; did.match refuses it (ZXVD_E_DNS).
 *   did.* needs no network and works with dns.* off.
 */
#ifndef ZXVD_DNS_H
#define ZXVD_DNS_H

#include <stdbool.h>
#include <stdint.h>

#include "zxvd_rpc.h"

#define ZXVD_E_DNS        (-32026)
#define ZXVD_DNS_ENTRIES  64u
#define ZXVD_DNS_THREADS  4u
#define ZXVD_DNS_ADDRS    16u
#define ZXVD_DNS_KEEP_MS  60000u
#define ZXVD_DNS_FAIL_MS  15000u
#define ZXVD_DNS_NAME_MAX 253u
#define ZXVD_DID_KEY_MAX  4096u

/* D6: true turns dns.* on, false off (lookups already running go on). */
void zxvd_dns_enable(bool on);
/* D2, D5: finish lookups that ended and start waiting ones. */
void zxvd_dns_service(void);
/* D5: wait for running lookups and forget every answer. */
void zxvd_dns_close(void);

/* the methods (zxvd_rpc.c's table) */
void zxvd_dns_resolve(zxvd_call_t *c);
void zxvd_dns_reverse(zxvd_call_t *c);
void zxvd_dns_flush(zxvd_call_t *c);
void zxvd_did_parse(zxvd_call_t *c);
void zxvd_did_match(zxvd_call_t *c);

#endif
