/* dns.h — DNS resolver: turning a name into an address
 *
 * WHY THIS EXISTS
 * ---------------
 * net_dns_resolve() was a stub that wrote 0.0.0.0 into its output and
 * returned. Nothing in the system could reach a host by name.
 *
 * THE DANGEROUS PART
 * ------------------
 * DNS is the most hostile parser in a small network stack, because the
 * response is attacker-influenced and the format contains BACKWARD POINTERS.
 * A name in a reply may be a chain of length-prefixed labels, or a two-byte
 * pointer to a name elsewhere in the message, or a mix. Three classic bugs
 * come out of that, and this module is written against all three:
 *
 *   1. POINTER LOOPS. A pointer that refers to itself, or two that refer to
 *      each other, spin forever. Guarded twice: a pointer must point STRICTLY
 *      BACKWARD (so each jump strictly decreases the offset, which alone
 *      guarantees termination), and the number of jumps is capped anyway.
 *
 *   2. READS OFF THE END. Every label length is checked against the message
 *      length before it is followed, not after.
 *
 *   3. UNBOUNDED EXPANSION. A short message can decompress to a very long
 *      name. Output is bounded by the caller's buffer AND by the 255-byte
 *      limit the protocol actually allows.
 *
 * ANTI-SPOOFING — AND ITS HONEST LIMIT
 * ------------------------------------
 * A reply is accepted only if the transaction id matches AND the question
 * section echoes the name we asked about. That is the standard minimum. It is
 * NOT sufficient on its own against an off-path attacker: real resistance
 * needs an unpredictable id and an unpredictable source port. The id is
 * supplied by the caller precisely so it can come from a real entropy source
 * (see net_set_entropy); if the caller supplies a predictable one, this module
 * cannot make up the difference and does not pretend to.
 *
 * Building and parsing are pure functions over byte buffers, no I/O.
 * Freestanding: integer only, no libc, no allocation.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV DNS slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_DNS_H
#define ZXV_DNS_H

#include <stdint.h>
#include <stdbool.h>

#define DNS_PORT        53u
#define DNS_MAX_NAME    256u   /* 255 protocol max + terminator */
#define DNS_MAX_MSG     512u   /* classic UDP DNS limit */
#define DNS_MAX_LABEL   63u
#define DNS_MAX_JUMPS   16u    /* compression-pointer follow limit */
#define DNS_HDR_LEN     12u

#define DNS_TYPE_A      1u
#define DNS_TYPE_CNAME  5u
#define DNS_CLASS_IN    1u

/* dns_parse_response outcomes */
#define DNS_OK          1      /* an A record was found          */
#define DNS_NO_ANSWER   0      /* well-formed, but no address     */
#define DNS_BAD        (-1)    /* malformed, or not our query     */

/* Build a standard recursive A query for `name` into `out`.
 * Returns the length written, or 0 if the name is invalid or `out` too small. */
uint32_t dns_build_query(uint8_t *out, uint32_t cap, const char *name, uint16_t id);

/* Read the (possibly compressed) name at `at` into `out` as dotted text.
 * Returns the number of characters written (excluding the terminator), or 0
 * on malformed input. Safe against pointer loops and truncation. */
uint32_t dns_read_name(const uint8_t *msg, uint32_t len, uint32_t at,
                       char *out, uint32_t out_cap);

/* Return the offset just past the name that starts at `at` — following a
 * pointer costs 2 bytes and ends the name. Returns 0 on malformed input. */
uint32_t dns_skip_name(const uint8_t *msg, uint32_t len, uint32_t at);

/* Parse a response to the query we sent for `name` with `id`.
 * Follows CNAME chains inside the message. Returns DNS_OK / DNS_NO_ANSWER /
 * DNS_BAD; on DNS_OK, ip_out holds the address and *ttl_out the record TTL
 * (ttl_out may be null). */
int dns_parse_response(const uint8_t *msg, uint32_t len, uint16_t id,
                       const char *name, uint8_t ip_out[4], uint32_t *ttl_out);

/* Case-insensitive DNS name comparison (RFC 4343). */
bool dns_name_eq(const char *a, const char *b);

#endif /* ZXV_DNS_H */
