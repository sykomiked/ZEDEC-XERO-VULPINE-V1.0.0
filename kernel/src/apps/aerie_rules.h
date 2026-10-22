/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* aerie_rules.h — Aerie's rules in C, the reference its Forth, KRON, Eve
 * and MYRA blocks are checked against (blocks/aerie). Pure integer
 * functions, no state: the model (aerie_model.c) and the language test
 * (tests/aerie_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs.
 *
 *   AR1 A LINE FRAME'S TAG (zt_harmonic_wire.h W3): tag = 100 * sync +
 *       10 * shell + trunk, sync 0..1, shell 0..9, trunk 0..9, so a tag
 *       splits back into trunk = tag mod 10, shell = (tag / 10) mod 10 and
 *       sync = tag / 100. A part outside its range is refused
 *       (AERIE_REFUSED): tags 200..255 are reserved and no part makes one.
 *   AR2 FITS THE BAND (ehop.h, ehop_frame.c): an Endian Hopping band is a
 *       message class of the sealed path (control, settle, media, chat,
 *       bulk), not a radio frequency band. A message of `bytes` fits a
 *       band whose largest frame on the wire is `frame` bytes when it is
 *       0..AERIE_PAYLOAD_MAX bytes and bytes + AERIE_OVERHEAD <= frame (the
 *       clear header, the channel tag and the AEAD tag take
 *       AERIE_OVERHEAD); a frame is AERIE_FRAME_MIN..AERIE_FRAME_MAX.
 *   AR3 RESOLVER THRESHOLD (zt_harmonic_wire.h W5): the correlation
 *       threshold harmonic.decode resolves a frame with, Q16.16, kept
 *       inside AERIE_THR_MIN..AERIE_THR_MAX. zxvd takes 0..2^31 - 1; Aerie
 *       keeps it above 0, so the faintest correlation is never read as a
 *       truth, and at most 1.0, the strength of one full-scale word.
 *   AR4 A MESSAGE LOG RECORD of ^ZXAERIE holds integers of at most the
 *       digits its pieces declare, sign aside (blocks/aerie/log.myra): seq
 *       9, bearer 1, band 1, dir 1, sender 10, bytes 4, stamp 13.
 *   AR5 NO SECRETS ON AMATEUR RADIO (docs/P2P_PROTOCOL_TWINS.md P13,
 *       docs/retail/BEARERS.md, 47 CFR 97.113 and 97.309): a message may
 *       go out when its bearer is private (amateur 0), or when it is
 *       amateur (amateur 1) and in the clear (sealed 0). An amateur bearer
 *       with a sealed (encrypted) message is refused, and so is a flag
 *       that is neither 0 nor 1: it fails closed. */
#ifndef AERIE_RULES_H
#define AERIE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define AERIE_REFUSED     (-1)
#define AERIE_OVERHEAD    48   /* EHOP_HDR_BYTES + EHOP_TAG_BYTES + EHOP_MAC_BYTES */
#define AERIE_PAYLOAD_MAX 2048 /* EHOP_MAX_PAYLOAD */
#define AERIE_FRAME_MIN   (AERIE_OVERHEAD + 1)
#define AERIE_FRAME_MAX   (AERIE_PAYLOAD_MAX + AERIE_OVERHEAD) /* EHOP_MAX_FRAME */
#define AERIE_THR_MIN     1
#define AERIE_THR_MAX     65536     /* 1.0 in Q16.16 */
#define AERIE_THR_DEFAULT 4096      /* zxvd's own default, 1/16 */
#define AERIE_SEQ_MAX     999999999 /* the log record's seq:i9 */
#define AERIE_FREIGHT_MAX 3360u     /* freight.pack's largest payload */
#define AERIE_STAMP_MAX   9999999999999LL

/* the two bearers (AR4, AR5) */
enum { AERIE_PRIVATE = 0, AERIE_AMATEUR = 1 };

/* AR1 */
static inline int64_t aerie_frame_tag(int64_t sync, int64_t shell, int64_t trunk)
{
    if (sync < 0 || sync > 1 || shell < 0 || shell > 9 || trunk < 0 || trunk > 9)
        return AERIE_REFUSED;
    return 100 * sync + 10 * shell + trunk;
}

/* AR1, the split: true when tag is one AR1 makes, with its three parts */
static inline bool aerie_tag_split(int64_t tag, int64_t *sync, int64_t *shell, int64_t *trunk)
{
    if (tag < 0 || tag > 199) return false;
    *sync = tag / 100;
    *shell = tag / 10 % 10;
    *trunk = tag % 10;
    return true;
}

/* AR2 */
static inline bool aerie_band_fits(int64_t bytes, int64_t frame)
{
    return bytes >= 0 && bytes <= AERIE_PAYLOAD_MAX && frame >= AERIE_FRAME_MIN &&
           frame <= AERIE_FRAME_MAX && frame - bytes >= AERIE_OVERHEAD;
}

/* AR3 */
static inline bool aerie_threshold_ok(int64_t thr)
{
    return thr >= AERIE_THR_MIN && thr <= AERIE_THR_MAX;
}

/* AR4: does x have at most `digits` digits, sign aside? */
static inline bool aerie_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool aerie_msg_ok(int64_t seq, int64_t bearer, int64_t band, int64_t dir,
                                int64_t sender, int64_t bytes, int64_t stamp)
{
    return aerie_digits_fit(seq, 9) && aerie_digits_fit(bearer, 1) && aerie_digits_fit(band, 1) &&
           aerie_digits_fit(dir, 1) && aerie_digits_fit(sender, 10) && aerie_digits_fit(bytes, 4) &&
           aerie_digits_fit(stamp, 13);
}

/* AR5 */
static inline bool aerie_ham_clear_ok(int64_t amateur, int64_t sealed)
{
    if (amateur < 0 || amateur > 1 || sealed < 0 || sealed > 1) return false;
    return !(amateur == 1 && sealed == 1);
}

#endif
