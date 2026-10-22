/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* toolchest_rules.h — Toolchest's rules in C, the reference its Sutra,
 * KRON, Eve, Trebago and MYRA blocks are checked against
 * (blocks/toolchest). Pure integer functions, no state: the model
 * (toolchest_model.c) and the language test (tests/toolchest_zxapp.inc)
 * both include this, so the languages are tested against the very C the
 * model runs.
 *
 *   TC1 VERSION FITS: a kit built for libzxv's ABI k = kmajor.kminor.kpatch
 *       fits the running ABI d = dmajor.dminor.dpatch when the majors are
 *       the same and d is at least k (sdk/docs/STABILITY.md: a major is a
 *       new library, a minor only adds). Each part is 0..999; one outside
 *       is no version: refused (TOOLCHEST_REFUSED). 1 fits, 0 not.
 *   TC2 INSTALL ALLOWED: a kit may be offered for install when both of its
 *       release's signatures verified (each verify result exactly 1), it
 *       fits (TC1 answered exactly 1) and room, the bytes the owner may
 *       still write less the bytes it needs (TC3 plus TC4), is
 *       0..TOOLCHEST_ROOM_MAX. Advice only: the owner confirms.
 *   TC3 DOWNLOAD BYTES: the exact sum of a kit's recipe blocks, each one
 *       1..FAB_BLOCK_MAX (2 MiB, kernel/src/fab/fab.h), 1..4096 of them
 *       (FAB_MAX_BLOCKS). A block outside its range refuses the sum.
 *   TC4 INSTALLED BYTES: the exact sum of the files a kit puts on disk,
 *       each 0..TOOLCHEST_FILE_MAX, 1..4096 of them. A file outside its
 *       range refuses the sum.
 *   TC5 A KIT RECORD of ^ZXTOOLCHEST holds integers of at most the digits
 *       its pieces declare, sign aside (blocks/toolchest/kit.myra): kit 3,
 *       lang 2, major, minor and patch 3 each, bytes 15.
 *   TC6 DISK AFTER: the share of the Hoard's volume in use once a kit's
 *       bytes are written, used_share.used_x100 (@num.used_x100) used as it
 *       is: floor(10000 * (total - left) / total), left = avail - need;
 *       refused when total is 0 or left is outside 0..total. */
#ifndef TOOLCHEST_RULES_H
#define TOOLCHEST_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define TOOLCHEST_PART_MAX  INT64_C(999)                /* one version part: Sutra 9(3) */
#define TOOLCHEST_BLOCK_MAX INT64_C(2097152)            /* FAB_BLOCK_MAX, 2 MiB */
#define TOOLCHEST_FILE_MAX  INT64_C(999999999999)       /* one installed file, 12 digits */
#define TOOLCHEST_PARTS_MAX 4096u                       /* FAB_MAX_BLOCKS; files likewise */
#define TOOLCHEST_ROOM_MAX  INT64_C(999999999999999999) /* Eve's room range, 18 digits */
#define TOOLCHEST_REFUSED   INT64_MIN

static inline bool toolchest_part_ok(int64_t p)
{
    return p >= 0 && p <= TOOLCHEST_PART_MAX;
}

/* TC1: 1 when the kit's ABI k fits the running ABI d, 0 when not. */
static inline int64_t toolchest_version_fits(int64_t kmajor, int64_t kminor, int64_t kpatch,
                                             int64_t dmajor, int64_t dminor, int64_t dpatch)
{
    if (!toolchest_part_ok(kmajor) || !toolchest_part_ok(kminor) || !toolchest_part_ok(kpatch) ||
        !toolchest_part_ok(dmajor) || !toolchest_part_ok(dminor) || !toolchest_part_ok(dpatch))
        return TOOLCHEST_REFUSED;
    if (dmajor != kmajor) return 0;
    if (dminor != kminor) return dminor > kminor ? 1 : 0;
    return dpatch >= kpatch ? 1 : 0;
}

/* TC2: both signatures verified, the kit fits and the room is in range. */
static inline bool toolchest_install_allowed(int64_t firstsig, int64_t secondsig, int64_t fits,
                                             int64_t room)
{
    return firstsig == 1 && secondsig == 1 && fits == 1 && room >= 0 && room <= TOOLCHEST_ROOM_MAX;
}

/* TC3 and TC4: the exact sum of n parts each lo..hi, or TOOLCHEST_REFUSED. */
static inline int64_t toolchest_sum_parts(const int64_t *part, uint32_t n, int64_t lo, int64_t hi)
{
    int64_t s = 0;
    if (!part || n == 0 || n > TOOLCHEST_PARTS_MAX) return TOOLCHEST_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (part[i] < lo || part[i] > hi) return TOOLCHEST_REFUSED;
        s += part[i]; /* 4096 parts of at most 12 digits stay far below int64 */
    }
    return s;
}

static inline int64_t toolchest_download_bytes(const int64_t *block, uint32_t n)
{
    return toolchest_sum_parts(block, n, 1, TOOLCHEST_BLOCK_MAX);
}

static inline int64_t toolchest_installed_bytes(const int64_t *file, uint32_t n)
{
    return toolchest_sum_parts(file, n, 0, TOOLCHEST_FILE_MAX);
}

/* TC5: v fits in `digits` decimal digits, sign aside (MYRA's iN). */
static inline bool toolchest_digits_fit(int64_t v, uint32_t digits)
{
    int64_t lim = 1;
    if (v == INT64_MIN) return false;
    for (uint32_t i = 0; i < digits && lim <= INT64_MAX / 10; i++) lim *= 10;
    return (v < 0 ? -v : v) < lim;
}

static inline bool toolchest_kit_record_fits(int64_t kit, int64_t lang, int64_t major,
                                             int64_t minor, int64_t patch, int64_t bytes)
{
    return toolchest_digits_fit(kit, 3) && toolchest_digits_fit(lang, 2) &&
           toolchest_digits_fit(major, 3) && toolchest_digits_fit(minor, 3) &&
           toolchest_digits_fit(patch, 3) && toolchest_digits_fit(bytes, 15);
}

/* TC6: used_share.used_x100's meaning. */
static inline int64_t toolchest_used_x100(int64_t total, int64_t left)
{
    if (total <= 0 || left < 0 || left > total || total > INT64_MAX / 10000)
        return TOOLCHEST_REFUSED;
    return (total - left) * 10000 / total;
}

#endif
