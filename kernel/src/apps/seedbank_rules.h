/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* seedbank_rules.h — Seedbank's rules in C, the reference its Rust, KRON,
 * Eve, Sutra and MYRA blocks are checked against (blocks/seedbank). Pure
 * integer functions, no state: the model (seedbank_model.c) and the
 * language test (tests/seedbank_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs. The blockstore's
 * share in use is used_share.used_x100 (@num.used_x100), mirrored here
 * only so the model can show it; its meaning is that block's.
 *
 *   SR1 ONE BYTE OF A CID'S TEXT. A published space's CID is "b" then the
 *       base32 (RFC 4648, lower case, no padding) of a CIDv1 raw sha2-256:
 *       59 bytes (zxvd's ZXS_CID_LEN, pspace.create's form). The byte code
 *       at pos (0 the first) of a text len bytes long fits when len is 59
 *       and, at pos 0, the code is 'b' (98); elsewhere it is 'a'..'z'
 *       (97..122) or '2'..'7' (50..55). 1 fits, 0 does not. Outside the
 *       domain (len 1..255, pos 0..len-1, code 0..255) it is refused (-1).
 *   SR2 A CID'S TEXT fits when every one of its bytes fits (SR1).
 *   SR3 A BLOCK PUT is 0..1048576 bytes (1 MiB, zxvd's AIPI_MODULE_MAX for
 *       a put by path); a put by hex is further held to zxvd's 8000 bytes.
 *   SR4 A BLOCK GET is 0..65536 bytes (zxvd's ZXVD_AIPI_GET_MAX): a larger
 *       block is refused by zxvd, never cut, so Seedbank asks only for
 *       blocks it knows fit.
 *   SR5 A PIN RECORD of ^ZXSEED holds integers of at most the places its
 *       pieces declare, sign aside (blocks/seedbank/pin.myra): pin 2,
 *       bytes 7, sealed 1, stamp (seconds since 1970) 11. */
#ifndef SEEDBANK_RULES_H
#define SEEDBANK_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define SEEDBANK_CID_LEN      59                   /* SR1: zxvd's ZXS_CID_LEN */
#define SEEDBANK_TEXT_MAX     255                  /* SR1: the longest text asked about */
#define SEEDBANK_PUT_MAX      1048576              /* SR3 */
#define SEEDBANK_GET_MAX      65536                /* SR4 */
#define SEEDBANK_PIN_FIELD    99                   /* SR5: i2 */
#define SEEDBANK_BYTES_FIELD  9999999              /* SR5: i7, also Sutra's 9(7) */
#define SEEDBANK_SEALED_FIELD 9                    /* SR5: i1 */
#define SEEDBANK_STAMP_FIELD  INT64_C(99999999999) /* SR5: i11 */
#define SEEDBANK_REFUSED      (-1)

/* SR1 */
static inline int64_t seedbank_cid_byte(int64_t pos, int64_t code, int64_t len)
{
    if (len < 1 || len > SEEDBANK_TEXT_MAX || pos < 0 || pos >= len || code < 0 || code > 255)
        return SEEDBANK_REFUSED;
    if (len != SEEDBANK_CID_LEN) return 0;
    if (pos == 0) return code == 'b';
    return (code >= 'a' && code <= 'z') || (code >= '2' && code <= '7');
}

/* SR2: s[0..n) */
static inline bool seedbank_cid_ok(const char *s, uint32_t n)
{
    if (!s || n != SEEDBANK_CID_LEN) return false;
    for (uint32_t i = 0; i < n; i++)
        if (seedbank_cid_byte(i, (unsigned char) s[i], n) != 1) return false;
    return true;
}

/* SR3 */
static inline bool seedbank_put_ok(int64_t bytes)
{
    return bytes >= 0 && bytes <= SEEDBANK_PUT_MAX;
}

/* SR4 */
static inline bool seedbank_get_ok(int64_t bytes)
{
    return bytes >= 0 && bytes <= SEEDBANK_GET_MAX;
}

static inline bool seedbank_fits(int64_t v, int64_t most)
{
    return v >= -most && v <= most;
}

/* SR5 */
static inline bool seedbank_pin_record_ok(int64_t pin, int64_t bytes, int64_t sealed, int64_t stamp)
{
    return seedbank_fits(pin, SEEDBANK_PIN_FIELD) && seedbank_fits(bytes, SEEDBANK_BYTES_FIELD) &&
           seedbank_fits(sealed, SEEDBANK_SEALED_FIELD) &&
           seedbank_fits(stamp, SEEDBANK_STAMP_FIELD);
}

/* used_share.used_x100's meaning: floor(10000 * (total - left) / total);
 * refused when total is 0 or less, or left is outside 0..total. */
static inline int64_t seedbank_used_x100(int64_t total, int64_t left)
{
    if (total <= 0 || left < 0 || left > total || total > INT64_MAX / 10000)
        return SEEDBANK_REFUSED;
    return (total - left) * 10000 / total;
}

#endif
