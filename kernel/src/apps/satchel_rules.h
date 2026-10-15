/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* satchel_rules.h — Satchel's rules in C, the reference its Eve, KRON and
 * MYRA blocks (blocks/satchel) and the Banker's and Witness's entries it
 * uses are checked against. Pure integer functions, no state: the model
 * (satchel_model.c) and the language test (tests/satchel_zxapp.inc) both
 * include this, so the languages are tested against the very C the model
 * runs.
 *
 *   SR1 PACKAGE LIMITS (package_ok): 1..SATCHEL_FILES_MAX files, the
 *       package 1..SATCHEL_BYTES_MAX bytes in all, its largest file
 *       1..SATCHEL_FILE_MAX bytes (zxvd's fab.put takes one file of at
 *       most 2 MiB as one block, FAB_BLOCK_MAX), and the largest file no
 *       bigger than the whole package.
 *   SR2 THE TOTAL: the exact sum of the file sizes, refused (-1) when a
 *       size is negative or a partial sum passes 64 bits (banker.total).
 *   SR3 A FILE'S SHARE of the package, in hundredths of a percent:
 *       floor(10000 * size / total), a total of 0 counted as 1; refused
 *       (-1) for a negative size or one past the total (banker.share).
 *   SR4 A FILE'S KIND by its name's ending: 1 a manifest (.zxapp,
 *       .zxblock), 2 code (.wasm .c .h .cpp .rs .kron .sutra .eve .tb
 *       .myra .bas .f .fs), 3 an image (.svg .png), 4 text (.md .txt .tsv
 *       .json), 0 anything else. The files of each kind are counted with
 *       Witness's count by kind (witness.kind_count, WR2).
 *   SR5 A PACKAGE RECORD of ^ZXSATCHEL holds integers of at most the
 *       digits its pieces declare, sign aside (blocks/satchel/record.myra):
 *       seq 9, files 2, bytes 9, signed 1, stamp 15 (ms since 1970). */
#ifndef SATCHEL_RULES_H
#define SATCHEL_RULES_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "witness_rules.h"

#define SATCHEL_FILES_MAX 64
#define SATCHEL_FILE_MAX  INT64_C(2097152)   /* 2 MiB: one fab block */
#define SATCHEL_BYTES_MAX INT64_C(134217728) /* 64 files of 2 MiB */
#define SATCHEL_REFUSED   (-1)

enum {
    SATCHEL_K_OTHER = 0,
    SATCHEL_K_MANIFEST = 1,
    SATCHEL_K_CODE = 2,
    SATCHEL_K_IMAGE = 3,
    SATCHEL_K_TEXT = 4,
    SATCHEL_KINDS = 5,
};

/* SR1 */
static inline bool satchel_package_ok(int64_t files, int64_t bytes, int64_t largest)
{
    return files >= 1 && files <= SATCHEL_FILES_MAX && bytes >= 1 && bytes <= SATCHEL_BYTES_MAX &&
           largest >= 1 && largest <= SATCHEL_FILE_MAX && largest <= bytes;
}

/* SR2 */
static inline int64_t satchel_total(const int64_t *size, uint32_t n)
{
    int64_t t = 0;
    if (!size) return SATCHEL_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (size[i] < 0 || t > INT64_MAX - size[i]) return SATCHEL_REFUSED;
        t += size[i];
    }
    return t;
}

/* SR3 */
static inline int64_t satchel_share_x100(int64_t size, int64_t total)
{
    if (total < 1) total = 1;
    if (size < 0 || size > total || size > INT64_MAX / 10000) return SATCHEL_REFUSED;
    return size * 10000 / total;
}

/* SR4: does name end in ext (ext with its dot)? */
static inline bool satchel_ends(const char *name, size_t n, const char *ext)
{
    size_t e = strlen(ext);
    return n > e && !memcmp(name + n - e, ext, e);
}

static inline int64_t satchel_kind_of(const char *name)
{
    static const char *const MAN[] = {".zxapp", ".zxblock"};
    static const char *const CODE[] = {".wasm", ".c",  ".h",    ".cpp", ".rs", ".kron", ".sutra",
                                       ".eve",  ".tb", ".myra", ".bas", ".f",  ".fs"};
    static const char *const IMG[] = {".svg", ".png"};
    static const char *const TXT[] = {".md", ".txt", ".tsv", ".json"};
    size_t n = name ? strlen(name) : 0u;
    for (size_t i = 0; i < sizeof MAN / sizeof MAN[0]; i++)
        if (satchel_ends(name, n, MAN[i])) return SATCHEL_K_MANIFEST;
    for (size_t i = 0; i < sizeof CODE / sizeof CODE[0]; i++)
        if (satchel_ends(name, n, CODE[i])) return SATCHEL_K_CODE;
    for (size_t i = 0; i < sizeof IMG / sizeof IMG[0]; i++)
        if (satchel_ends(name, n, IMG[i])) return SATCHEL_K_IMAGE;
    for (size_t i = 0; i < sizeof TXT / sizeof TXT[0]; i++)
        if (satchel_ends(name, n, TXT[i])) return SATCHEL_K_TEXT;
    return SATCHEL_K_OTHER;
}

/* SR5 */
static inline bool satchel_record_fits(int64_t seq, int64_t files, int64_t bytes, int64_t sig,
                                       int64_t stamp)
{
    return witness_digits_fit(seq, 9) && witness_digits_fit(files, 2) &&
           witness_digits_fit(bytes, 9) && witness_digits_fit(sig, 1) &&
           witness_digits_fit(stamp, 15);
}

#endif
