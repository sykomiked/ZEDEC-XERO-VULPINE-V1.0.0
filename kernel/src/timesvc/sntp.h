/* sntp.h — SNTP client packets (RFC 4330, RFC 5905 on-wire format)
 *
 * A device needs the wall-clock time before it can make a verified TLS
 * connection, because certificate expiry cannot be checked without it. This
 * module builds an SNTP request and checks a reply. It does no I/O: the
 * caller sends the 48 bytes over UDP port 123 and hands back what arrives.
 *
 * Checks on a reply, all of which must pass:
 *  - 48 bytes or more, mode 4 (server), version 3 or 4.
 *  - The originate timestamp equals the nonce the request carried. The
 *    request puts 64 random bits in its transmit timestamp instead of the
 *    local clock (as RFC 9109 suggests), so an off-path sender must guess
 *    them, and the request reveals nothing about the local clock.
 *  - Stratum 1-15 and leap indicator not 3 (the server is synchronised).
 *    Stratum 0 is a kiss-o'-death: the code ("RATE", "DENY", "RSTR") is
 *    returned so the caller backs off or drops the server.
 *  - Non-zero receive and transmit timestamps, transmit not before receive,
 *    and root distance (root delay / 2 + root dispersion) under 16 s.
 *
 * SNTP is not authenticated. An attacker on the path can still lie about the
 * time; timesvc.h limits what one lying server can do (several servers must
 * agree, and the time never goes below the build floor). Network Time
 * Security (RFC 8915) would close that gap and is not done.
 *
 * Freestanding: integer only, no libc, no allocation, no 64-bit division.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_SNTP_H
#define ZXV_SNTP_H

#include <stdint.h>

#define SNTP_PORT    123u
#define SNTP_PKT_LEN 48u
/* seconds from 1900-01-01 (the NTP epoch) to 1970-01-01 */
#define NTP_UNIX_DELTA 2208988800u
/* RFC 5905 MAXDISP: a server further than this from a reference is useless */
#define SNTP_MAX_ROOT_DIST_US 16000000

typedef enum {
    SNTP_OK = 0,
    SNTP_BAD,      /* malformed, wrong mode or version, or bad timestamps */
    SNTP_NOT_OURS, /* originate timestamp is not our nonce */
    SNTP_KOD,      /* kiss-o'-death; see kod[] */
    SNTP_UNSYNCED, /* leap = 3, stratum 16+, or root distance too large */
} sntp_status_t;

typedef struct {
    int64_t offset_us; /* add to the local clock to get Unix time */
    int64_t delay_us;  /* round trip, minus the server's processing time */
    int64_t server_us; /* the server's transmit time, Unix microseconds */
    uint32_t root_dist_us;
    uint8_t stratum;
    uint8_t leap; /* 0 none, 1 +1 s at the end of the day, 2 -1 s */
    char kod[5];  /* NUL-terminated kiss code when SNTP_KOD */
} sntp_sample_t;

/* Write a 48-byte client request whose transmit timestamp is `nonce`
 * (64 random bits; keep it to check the reply). Returns SNTP_PKT_LEN. */
uint32_t sntp_build_request(uint8_t out[SNTP_PKT_LEN], uint64_t nonce);

/* Check a reply and compute the clock offset.
 *  t1_us: local clock when the request was sent,
 *  t4_us: local clock when the reply arrived (same clock, microseconds).
 *  pivot_s: a Unix time known to be within 68 years of the truth (the build
 *           time is a good choice); it picks the NTP era, so timestamps keep
 *           working past the 2036 rollover. */
sntp_status_t sntp_parse_reply(const uint8_t *pkt, uint32_t len, uint64_t nonce, int64_t t1_us,
                               int64_t t4_us, int64_t pivot_s, sntp_sample_t *out);

/* An NTP timestamp (seconds since 1900 in the high 32 bits, binary fraction
 * in the low 32) as Unix microseconds, taking the era nearest `pivot_s`. */
int64_t sntp_ntp_to_unix_us(uint64_t ntp, int64_t pivot_s);

#endif /* ZXV_SNTP_H */
