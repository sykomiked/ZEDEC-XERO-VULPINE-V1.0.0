/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* signpost_rules.h — Signpost's rules in C, the reference its Rust, Eve,
 * KRON and MYRA blocks are checked against (blocks/signpost). Pure integer
 * functions, no state: the model (signpost_model.c) and the language test
 * (tests/signpost_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs.
 *
 *   SP1 A NAME is labels joined by dots, one trailing dot allowed (the
 *       root). Each label is 1..63 bytes of a-z, A-Z, 0-9 and '-', never
 *       starting or ending with '-'; the whole name, trailing dot aside, is
 *       at most 253 bytes. A label is judged from three numbers: its length
 *       (label), the name's length (total) and its bytes that are not
 *       allowed where they stand (bad).
 *   SP2 ITS FAULT, the first that applies: 1 the label is empty or longer
 *       than 63; 2 the name is longer than 253, or shorter than the label
 *       (a measurement that cannot be); 3 a byte not allowed; 0 none. A
 *       negative count is no measurement: refused (SIGNPOST_REFUSED).
 *   SP3 A LABEL FITS when its fault is 0 (signpost_name_ok).
 *   SP4 AN IPv4 ADDRESS'S CLASS from its four octets a.b.c.d (RFC 6890):
 *       0 public, 1 this network 0/8, 2 loopback 127/8, 3 private 10/8,
 *       172.16/12 and 192.168/16, 4 link-local 169.254/16, 5 shared
 *       100.64/10, 6 multicast 224/4, 7 reserved 240/4, 8 the broadcast
 *       255.255.255.255. An octet outside 0..255 is refused.
 *   SP5 A CACHE RECORD of ^ZXSIGNPOST holds integers of at most the digits
 *       its pieces declare, sign aside (blocks/signpost/cache.myra): seq 9,
 *       class 1, addr 10, stored 11, ttl 10.
 *   SP6 SECONDS LEFT before a cached answer stored at `stored` with a
 *       time to live of `ttl` lapses, at `now`: stored + ttl - now, never
 *       below 0 (0 is lapsed). */
#ifndef SIGNPOST_RULES_H
#define SIGNPOST_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define SIGNPOST_LABEL_MAX 63
#define SIGNPOST_NAME_MAX  253
#define SIGNPOST_REFUSED   (-1)

enum {
    SIGNPOST_FITS = 0,      /* SP2: no fault */
    SIGNPOST_BAD_LABEL = 1, /* empty, or past 63 bytes */
    SIGNPOST_BAD_TOTAL = 2, /* past 253 bytes, or shorter than its label */
    SIGNPOST_BAD_BYTE = 3,  /* a byte not allowed where it stands */
};

enum {
    SIGNPOST_PUBLIC = 0,
    SIGNPOST_THIS_NET = 1,
    SIGNPOST_LOOPBACK = 2,
    SIGNPOST_PRIVATE = 3,
    SIGNPOST_LINK_LOCAL = 4,
    SIGNPOST_SHARED = 5,
    SIGNPOST_MULTICAST = 6,
    SIGNPOST_RESERVED = 7,
    SIGNPOST_BROADCAST = 8,
};
#define SIGNPOST_CLASSES 9

/* SP2 */
static inline int64_t signpost_name_fault(int64_t label, int64_t total, int64_t bad)
{
    if (label < 0 || total < 0 || bad < 0) return SIGNPOST_REFUSED;
    if (label < 1 || label > SIGNPOST_LABEL_MAX) return SIGNPOST_BAD_LABEL;
    if (total < label || total > SIGNPOST_NAME_MAX) return SIGNPOST_BAD_TOTAL;
    if (bad > 0) return SIGNPOST_BAD_BYTE;
    return SIGNPOST_FITS;
}

/* SP3 */
static inline bool signpost_name_ok(int64_t label, int64_t total, int64_t bad)
{
    return signpost_name_fault(label, total, bad) == SIGNPOST_FITS;
}

/* SP4 */
static inline int64_t signpost_addr_class(int64_t a, int64_t b, int64_t c, int64_t d)
{
    if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255)
        return SIGNPOST_REFUSED;
    if (a == 0) return SIGNPOST_THIS_NET;
    if (a == 127) return SIGNPOST_LOOPBACK;
    if (a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168))
        return SIGNPOST_PRIVATE;
    if (a == 169 && b == 254) return SIGNPOST_LINK_LOCAL;
    if (a == 100 && b >= 64 && b <= 127) return SIGNPOST_SHARED;
    if (a >= 224 && a <= 239) return SIGNPOST_MULTICAST;
    if (a == 255 && b == 255 && c == 255 && d == 255) return SIGNPOST_BROADCAST;
    if (a >= 240) return SIGNPOST_RESERVED;
    return SIGNPOST_PUBLIC;
}

/* SP5: does x have at most `digits` digits, sign aside? */
static inline bool signpost_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool signpost_cache_fits(int64_t seq, int64_t cls, int64_t addr, int64_t stored,
                                       int64_t ttl)
{
    return signpost_digits_fit(seq, 9) && signpost_digits_fit(cls, 1) &&
           signpost_digits_fit(addr, 10) && signpost_digits_fit(stored, 11) &&
           signpost_digits_fit(ttl, 10);
}

/* SP6: SIGNPOST_REFUSED when stored + ttl - now does not fit int64. */
static inline int64_t signpost_ttl_left(int64_t stored, int64_t ttl, int64_t now)
{
    if ((ttl > 0 && stored > INT64_MAX - ttl) || (ttl < 0 && stored < INT64_MIN - ttl))
        return SIGNPOST_REFUSED;
    int64_t end = stored + ttl;
    if ((now < 0 && end > INT64_MAX + now) || (now > 0 && end < INT64_MIN + now))
        return SIGNPOST_REFUSED;
    int64_t left = end - now;
    return left > 0 ? left : 0;
}

#endif
