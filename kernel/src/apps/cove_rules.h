/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cove_rules.h — Cove's rules in C, the reference its Eve, KRON, Sutra,
 * Rust and MYRA blocks are checked against (blocks/cove). Pure integer
 * functions, no state: the model (cove_model.c) and the language test
 * (tests/cove_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs.
 *
 *   CR1 TUNNEL OPEN: a private tunnel between two devices of the owner's
 *       mesh may open when both are active on the signed roster
 *       (selfactive and peeractive exactly 1) and neither is revoked
 *       (selfrevoked and peerrevoked exactly 0). The flags come from
 *       devmesh.devices: active is a roster row whose status is "active",
 *       revoked one whose status is "revoked" (a tombstone); a device that
 *       is not on the roster is neither, so it never opens. A flag that is
 *       no flag (2, -1) is never read as yes. Advice only: zxvd's roster
 *       decides who is in the mesh.
 *   CR2 SEQ GAP (the nonce discipline of one channel): a frame's seq after
 *       the last seq of that channel and direction is seq - last, at least
 *       1 (1 is the very next frame; more means frames were skipped or
 *       lost). The first frame follows last 0, so seqs start at 1. A seq at
 *       or below last (reuse, a replay), last below 0, or a seq past
 *       COVE_SEQ_MAX (eighteen digits, Sutra's 9(18); far below the
 *       2^64 - 1 that ehop.seal refuses) is refused (COVE_REFUSED): the
 *       channel's nonce space is spent and it must move to a new epoch.
 *   CR3 FRAME WIDTH: a sealed frame on the wire fits its band when its
 *       length is COVE_FRAME_MIN..COVE_FRAME_MAX (the 48 bytes of header,
 *       channel tag and AEAD tag around 0..2048 bytes of data) and the
 *       room the band's largest frame leaves (max_frame - framelen, worked
 *       out by the model from ehop.bands) is 0..COVE_PAYLOAD_MAX.
 *   CR4 A TUNNEL RECORD of ^ZXCOVE holds integers of at most the digits
 *       its pieces declare, sign aside (blocks/cove/tunnel.myra): tunnel 2,
 *       peer (its roster row) 1, band 1, chan and epoch 10 each, seq 18
 *       and stamp (ms since 1970) 15. No piece holds a key. */
#ifndef COVE_RULES_H
#define COVE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define COVE_DEVICES     8u            /* devmesh.h DM_MAX_DEVICES */
#define COVE_OVERHEAD    INT64_C(48)   /* EHOP_OVERHEAD: header, tag, AEAD tag */
#define COVE_PAYLOAD_MAX INT64_C(2048) /* EHOP_MAX_PAYLOAD */
#define COVE_FRAME_MIN   COVE_OVERHEAD /* a frame of no data */
#define COVE_FRAME_MAX   (COVE_PAYLOAD_MAX + COVE_OVERHEAD) /* EHOP_MAX_FRAME */
#define COVE_SEQ_MAX     INT64_C(999999999999999999)        /* eighteen digits */
#define COVE_STAMP_MAX   INT64_C(999999999999999)           /* fifteen digits of ms since 1970 */
#define COVE_REFUSED     INT64_MIN

/* CR1 */
static inline bool cove_tunnel_open_ok(int64_t selfactive, int64_t selfrevoked, int64_t peeractive,
                                       int64_t peerrevoked)
{
    return selfactive == 1 && selfrevoked == 0 && peeractive == 1 && peerrevoked == 0;
}

/* CR2: seq - last, or COVE_REFUSED */
static inline int64_t cove_tunnel_seq_gap(int64_t last, int64_t seq)
{
    if (last < 0 || seq <= last || seq > COVE_SEQ_MAX) return COVE_REFUSED;
    return seq - last;
}

/* CR3 */
static inline bool cove_tunnel_frame_ok(int64_t framelen, int64_t room)
{
    return framelen >= COVE_FRAME_MIN && framelen <= COVE_FRAME_MAX && room >= 0 &&
           room <= COVE_PAYLOAD_MAX;
}

/* CR4: v fits in `digits` decimal digits, sign aside (MYRA's iN) */
static inline bool cove_digits_fit(int64_t v, uint32_t digits)
{
    int64_t lim = 1;
    if (v == INT64_MIN) return false;
    for (uint32_t i = 0; i < digits && lim <= INT64_MAX / 10; i++) lim *= 10;
    return (v < 0 ? -v : v) < lim;
}

static inline bool cove_tunnel_record_ok(int64_t tunnel, int64_t peer, int64_t band, int64_t chan,
                                         int64_t epoch, int64_t seq, int64_t stamp)
{
    return cove_digits_fit(tunnel, 2) && cove_digits_fit(peer, 1) && cove_digits_fit(band, 1) &&
           cove_digits_fit(chan, 10) && cove_digits_fit(epoch, 10) && cove_digits_fit(seq, 18) &&
           cove_digits_fit(stamp, 15);
}

#endif
