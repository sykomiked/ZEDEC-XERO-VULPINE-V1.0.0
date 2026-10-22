/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* warding_rules.h — Warding's rules in C, the reference its Eve, KRON,
 * Sutra, Trebago and MYRA blocks are checked against (blocks/warding).
 * Pure integer functions, no state: the model (warding_model.c) and the
 * language test (tests/warding_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 * A key opens one thing: a door (kind 1), a device of the owner's mesh
 * (kind 2) or an account (kind 3, a passkey). It is known by its serial
 * in the register and by public ids, never by its bytes.
 *
 *   WR1 A KEY IS ACTIVE (key_active) when it was issued (issued = 1), no
 *       device says it is revoked (revoked = 0, the count of devices that
 *       do) and it has not expired: its days left (lockbox.days_left, 0 is
 *       due today or past) 1..WARDING_EXPIRY_MAX. Any other value is no
 *       (a flag that is no flag is never read as yes).
 *   WR2 EXPIRY LIMITS (key_expiry_ok): a key expires 1..WARDING_EXPIRY_MAX
 *       days after it is issued; a key that never expires is not offered.
 *   WR3 THE KEY REGISTER (key_serial_ok): a serial may enter the register
 *       only above the last one entered (serial > last), so serials rise
 *       strictly and none is used twice; both 0..WARDING_SERIAL_MAX
 *       (Sutra's 9(9)), anything else refused (WARDING_REFUSED): the
 *       register never wraps or restarts.
 *   WR4 ENTROPY (key_entropy_bits), from two lengths only, never the key's
 *       bytes: chars (the key's length in symbols, 0..WARDING_CHARS_MAX)
 *       and alphabet (how many symbols it is drawn from,
 *       1..WARDING_ALPHABET_MAX): floor(chars * WARDING_LOG2_MILLI[alphabet]
 *       / 1000), the table floor(1000 * log2(a)), the largest m with
 *       2^m <= a^1000, made exactly in integers. Outside the domain
 *       refused. Advice only: below WARDING_BITS_MIN the owner is told.
 *   WR5 THE ACCESS GRID (access_grid_cell), holders by doors or devices:
 *       one key a row (holder, item, live 0 or 1), row i's cell the exact
 *       sum of live over every row with its holder and its item, itself
 *       included: how many live keys that holder has to that item.
 *   WR6 A HOLDER NODE (holder_node_ok) fits ^ZXWARD(holder, kind, item) =
 *       serial ^ expires: holder and item at most 4 digits, kind 1,
 *       serial 9 and expires (seconds since 1970) 12, sign aside (MYRA's
 *       i-fields).
 *   WR7 ONE KEY, MANY DEVICES. Each device of the mesh says of a key TRUE
 *       (active there), FALSE (revoked there) or NEITHER (not heard). The
 *       key is lpres_kjoin of every device's word: revoked on one and
 *       active on another is BOTH, and a BOTH key is locked out on every
 *       device until the revocation reaches them all. Only TRUE opens. */
#ifndef WARDING_RULES_H
#define WARDING_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "lpres.h"

#define WARDING_REFUSED      (-1)
#define WARDING_EXPIRY_MAX   3650                  /* WR1, WR2: ten years of 365 days */
#define WARDING_SERIAL_MAX   INT64_C(999999999)    /* WR3: Sutra's 9(9) */
#define WARDING_CHARS_MAX    4096                  /* WR4 */
#define WARDING_ALPHABET_MAX 256                   /* WR4: one byte a symbol */
#define WARDING_BITS_MIN     128                   /* WR4: below it, advice */
#define WARDING_STAMP_MAX    INT64_C(253402300799) /* lockbox's year 9999 */

enum { WARDING_DOOR = 1, WARDING_DEVICE = 2, WARDING_ACCOUNT = 3 };

/* WR4: floor(1000 * log2(a)) for a = 0..256 (0 and 1 hold no bits) */
static const int64_t WARDING_LOG2_MILLI[WARDING_ALPHABET_MAX + 1] = {
    0,    0,    1000, 1584, 2000, 2321, 2584, 2807, 3000, 3169, 3321, 3459, 3584, 3700, 3807, 3906,
    4000, 4087, 4169, 4247, 4321, 4392, 4459, 4523, 4584, 4643, 4700, 4754, 4807, 4857, 4906, 4954,
    5000, 5044, 5087, 5129, 5169, 5209, 5247, 5285, 5321, 5357, 5392, 5426, 5459, 5491, 5523, 5554,
    5584, 5614, 5643, 5672, 5700, 5727, 5754, 5781, 5807, 5832, 5857, 5882, 5906, 5930, 5954, 5977,
    6000, 6022, 6044, 6066, 6087, 6108, 6129, 6149, 6169, 6189, 6209, 6228, 6247, 6266, 6285, 6303,
    6321, 6339, 6357, 6375, 6392, 6409, 6426, 6442, 6459, 6475, 6491, 6507, 6523, 6539, 6554, 6569,
    6584, 6599, 6614, 6629, 6643, 6658, 6672, 6686, 6700, 6714, 6727, 6741, 6754, 6768, 6781, 6794,
    6807, 6820, 6832, 6845, 6857, 6870, 6882, 6894, 6906, 6918, 6930, 6942, 6954, 6965, 6977, 6988,
    7000, 7011, 7022, 7033, 7044, 7055, 7066, 7076, 7087, 7098, 7108, 7118, 7129, 7139, 7149, 7159,
    7169, 7179, 7189, 7199, 7209, 7219, 7228, 7238, 7247, 7257, 7266, 7276, 7285, 7294, 7303, 7312,
    7321, 7330, 7339, 7348, 7357, 7366, 7375, 7383, 7392, 7400, 7409, 7417, 7426, 7434, 7442, 7451,
    7459, 7467, 7475, 7483, 7491, 7499, 7507, 7515, 7523, 7531, 7539, 7546, 7554, 7562, 7569, 7577,
    7584, 7592, 7599, 7607, 7614, 7622, 7629, 7636, 7643, 7651, 7658, 7665, 7672, 7679, 7686, 7693,
    7700, 7707, 7714, 7721, 7727, 7734, 7741, 7748, 7754, 7761, 7768, 7774, 7781, 7787, 7794, 7800,
    7807, 7813, 7820, 7826, 7832, 7839, 7845, 7851, 7857, 7864, 7870, 7876, 7882, 7888, 7894, 7900,
    7906, 7912, 7918, 7924, 7930, 7936, 7942, 7948, 7954, 7960, 7965, 7971, 7977, 7982, 7988, 7994,
    8000};

/* WR1 */
static inline bool warding_key_active(int64_t issued, int64_t revoked, int64_t left)
{
    return issued == 1 && revoked == 0 && left >= 1 && left <= WARDING_EXPIRY_MAX;
}

/* WR2 */
static inline bool warding_expiry_ok(int64_t days)
{
    return days >= 1 && days <= WARDING_EXPIRY_MAX;
}

/* WR3: 1 accepted, 0 not above the last, WARDING_REFUSED outside 9(9) */
static inline int64_t warding_serial_ok(int64_t serial, int64_t last)
{
    if (serial < 0 || serial > WARDING_SERIAL_MAX || last < 0 || last > WARDING_SERIAL_MAX)
        return WARDING_REFUSED;
    return serial > last ? 1 : 0;
}

/* WR4 */
static inline int64_t warding_entropy_bits(int64_t chars, int64_t alphabet)
{
    if (chars < 0 || chars > WARDING_CHARS_MAX || alphabet < 1 || alphabet > WARDING_ALPHABET_MAX)
        return WARDING_REFUSED;
    return chars * WARDING_LOG2_MILLI[alphabet] / 1000; /* at most 32768000 / 1000 */
}

/* WR5: row i's cell, or WARDING_REFUSED when the sum passes 64 bits */
static inline int64_t warding_grid_cell(const int64_t *holder, const int64_t *item,
                                        const int64_t *live, uint32_t n, uint32_t i)
{
    int64_t s = 0;
    if (!holder || !item || !live || i >= n) return WARDING_REFUSED;
    for (uint32_t j = 0; j < n; j++) {
        if (holder[j] != holder[i] || item[j] != item[i]) continue;
        if ((live[j] > 0 && s > INT64_MAX - live[j]) ||
            (live[j] < 0 && s < INT64_MIN + 1 - live[j]))
            return WARDING_REFUSED;
        s += live[j];
    }
    return s;
}

/* WR6: |v| has at most `digits` digits (1..18) */
static inline bool warding_digits(int64_t v, int digits)
{
    int64_t top = 1;
    for (int k = 0; k < digits && k < 18; k++) top *= 10;
    return v > -top && v < top;
}

static inline bool warding_node_ok(int64_t holder, int64_t kind, int64_t item, int64_t serial,
                                   int64_t expires)
{
    return warding_digits(holder, 4) && warding_digits(kind, 1) && warding_digits(item, 4) &&
           warding_digits(serial, 9) && warding_digits(expires, 12);
}

/* WR7: what one device says of a key */
static inline lpres_state_t warding_device_word(bool heard, bool revoked)
{
    if (!heard) return LPRES_STATE_NEITHER;
    return revoked ? LPRES_STATE_FALSE : LPRES_STATE_TRUE;
}

/* WR7: the key over every device's word */
static inline lpres_state_t warding_key_state(const lpres_state_t *word, uint32_t n)
{
    lpres_state_t s = LPRES_STATE_NEITHER;
    for (uint32_t i = 0; word && i < n; i++) s = lpres_kjoin(s, word[i]);
    return s;
}

#endif
