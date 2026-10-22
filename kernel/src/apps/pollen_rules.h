/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pollen_rules.h — Pollen's rules in C, the reference its Trebago, KRON,
 * Eve and MYRA blocks (blocks/pollen) are checked against, beside the C of
 * the entries it uses from Ferry, Witness and Hedgerow (their own rules
 * headers). Pure integer functions, no state: the model (pollen_model.c)
 * and the language test (tests/pollen_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   PR1 FREIGHTS FOR A FILE (freight_count): zxvd's freight.pack carries
 *       POLLEN_ROW_BYTES (20) bytes a data row and k data rows a freight,
 *       so a file of size bytes takes ceil(size / (20 * k)) freights, none
 *       for an empty file. size 0..POLLEN_SIZE_MAX, k 1..POLLEN_K_MAX
 *       (168), anything else refused (POLLEN_REFUSED).
 *   PR2 PACKETS TO REBUILD IT (freight_packets): any k of a freight's 168
 *       packets rebuild it (Cauchy Reed-Solomon), so the receiver needs
 *       k * PR1 packets; the sender puts 168 * PR1 on the wire. Refused as
 *       PR1.
 *   PR3 OFFER LIMITS (share_limits_ok): each file of an offer
 *       1..POLLEN_FILE_MAX bytes (16 MiB), 1..POLLEN_FILES_MAX (16) files in
 *       one offer, and 0..POLLEN_OPEN_MAX - 1 offers already open when one
 *       more opens, so at most POLLEN_OPEN_MAX (8) are open. Each field
 *       against its own range.
 *   PR4 AN OFFER RECORD of ^ZXPOLLEN holds integers of at most the digits
 *       its pieces declare, sign aside (blocks/pollen/record.myra): offer 4,
 *       files 2, bytes 9, state 1, stamp 15 (ms since 1970). */
#ifndef POLLEN_RULES_H
#define POLLEN_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "ferry_rules.h"
#include "hedgerow_rules.h"
#include "witness_rules.h"

#define POLLEN_REFUSED   (-1)
#define POLLEN_ROW_BYTES 20                       /* bytes a freight data row carries */
#define POLLEN_ROWS      168                      /* packets in one freight */
#define POLLEN_K_MAX     168                      /* data rows a freight may have */
#define POLLEN_SIZE_MAX  INT64_C(999999999999)    /* PR1's sizes, just under a terabyte */
#define POLLEN_FILE_MAX  INT64_C(16777216)        /* PR3: 16 MiB a file */
#define POLLEN_FILES_MAX 16                       /* PR3: files in one offer */
#define POLLEN_OPEN_MAX  8                        /* PR3: offers open at once */
#define POLLEN_OFFER_MAX 9999                     /* PR4: an offer's number, 4 digits */
#define POLLEN_STAMP_MAX INT64_C(999999999999999) /* PR4: 15 digits of ms */
#define POLLEN_SPARE     INT64_C(67108864)        /* 64 MiB kept free on a receiver by default */

/* PR4: an offer's state */
enum {
    POLLEN_DRAFTED = 0,   /* made, not yet approved */
    POLLEN_OPEN = 1,      /* shared on the owner's approval */
    POLLEN_WITHDRAWN = 2, /* the owner took it back */
};

/* PR1 */
static inline int64_t pollen_freights(int64_t size, int64_t k)
{
    if (size < 0 || size > POLLEN_SIZE_MAX || k < 1 || k > POLLEN_K_MAX) return POLLEN_REFUSED;
    int64_t per = POLLEN_ROW_BYTES * k;
    return (size + per - 1) / per; /* below 2^40: no overflow */
}

/* PR2 */
static inline int64_t pollen_packets(int64_t size, int64_t k)
{
    int64_t f = pollen_freights(size, k);
    return f == POLLEN_REFUSED ? POLLEN_REFUSED : f * k;
}

/* PR3 */
static inline bool pollen_share_limits_ok(int64_t bytes, int64_t files, int64_t open)
{
    return bytes >= 1 && bytes <= POLLEN_FILE_MAX && files >= 1 && files <= POLLEN_FILES_MAX &&
           open >= 0 && open < POLLEN_OPEN_MAX;
}

/* PR4 */
static inline bool pollen_record_fits(int64_t offer, int64_t files, int64_t bytes, int64_t state,
                                      int64_t stamp)
{
    return witness_digits_fit(offer, 4) && witness_digits_fit(files, 2) &&
           witness_digits_fit(bytes, 9) && witness_digits_fit(state, 1) &&
           witness_digits_fit(stamp, 15);
}

/* A peer is nearby when its address is this machine, a private network or
 * link-local (Hedgerow's addr_scope, HR4); a public address or none is not. */
static inline bool pollen_nearby_scope(int64_t scope)
{
    return scope == HEDGEROW_SCOPE_LOOPBACK || scope == HEDGEROW_SCOPE_PRIVATE ||
           scope == HEDGEROW_SCOPE_LINK;
}

#endif
