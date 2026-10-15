/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* typecase_rules.h — Typecase's rules in C, the reference its KRON and
 * Trebago blocks are checked against (blocks/typecase). Pure integer
 * functions, no state: the model (typecase_model.c) and the language test
 * (tests/typecase_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs.
 *
 *   TR1 A FONT IS ACCEPTED when its table directory names 1..256 tables,
 *       the file is at most 32 MiB (33554432 bytes) and every table lies
 *       inside the file: offset + length <= size. Offsets and lengths are
 *       unsigned 32-bit fields of the file, so a negative one, or a negative
 *       size, is no measurement and is refused (-1). 1 accepted, 0 not. More
 *       than 256 tables (or none) is not accepted, whatever the records say,
 *       and the records past the 256th are never read.
 *   TR2 A SCRIPT'S COVERAGE is the share of the script's code points the
 *       font maps to a glyph, in hundredths of a percent:
 *       floor(10000 * covered / total), never rounded up; total is
 *       1..1114112 (every code point Unicode has, 0x110000) and covered
 *       0..total, anything else refused (-1): a font cannot cover more of a
 *       script than the script has. */
#ifndef TYPECASE_RULES_H
#define TYPECASE_RULES_H

#include <stdint.h>

#define TYPECASE_TABLES_MAX 256      /* TR1 */
#define TYPECASE_FILE_MAX   33554432 /* TR1: 32 MiB */
#define TYPECASE_CP_MAX     1114112  /* TR2: 0x110000 code points */
#define TYPECASE_X100       10000    /* TR2: 100.00 % */
#define TYPECASE_REFUSED    (-1)

/* TR1: off[0..n) and len[0..n) are the table records, size the file's bytes. */
static inline int64_t typecase_font_ok(const int64_t *off, const int64_t *len, uint32_t n,
                                       int64_t size)
{
    if (n == 0 || n > TYPECASE_TABLES_MAX || !off || !len) return 0;
    if (size < 0) return TYPECASE_REFUSED;
    for (uint32_t i = 0; i < n; i++)
        if (off[i] < 0 || len[i] < 0) return TYPECASE_REFUSED;
    if (size > TYPECASE_FILE_MAX) return 0;
    for (uint32_t i = 0; i < n; i++)
        if (off[i] > size || len[i] > size - off[i]) return 0;
    return 1;
}

/* TR2 */
static inline int64_t typecase_coverage_x100(int64_t covered, int64_t total)
{
    if (total < 1 || total > TYPECASE_CP_MAX || covered < 0 || covered > total)
        return TYPECASE_REFUSED;
    return covered * TYPECASE_X100 / total;
}

#endif
