/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* joinery_rules.h — Joinery's rules in C, the reference its KRON, Trebago,
 * Eve and MYRA blocks are checked against (blocks/joinery). Pure integer
 * functions, no state: the model (joinery_model.c) and the language test
 * (tests/joinery_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs. JOINERY_REFUSED is a refusal.
 *
 *   JR1 WHERE A LINE STARTS: linebytes holds the bytes of each line ended
 *       by a newline, the newline counted (1..65537 each: a line of at
 *       most JOINERY_LINE_MAX bytes and its newline); line `line` (0 the
 *       first, 0..n, n meaning the end of the last of them) starts at the
 *       exact sum of the lines before it. Anything else is refused.
 *   JR2 WHICH LINE HOLDS A BYTE: the line of byte offset off (0..
 *       JOINERY_FILE_MAX) is how many of those lines end at or before it;
 *       the text after the last newline is the line after them. A line
 *       length outside 1..65537 or an offset outside the domain is
 *       refused.
 *   JR3 A COLUMN'S BYTE OFFSET: width holds the UTF-8 width of each
 *       character of one line (1..4 bytes); column col (0..n) starts after
 *       the exact sum of the widths before it. A width that is no UTF-8
 *       width or a column outside the line is refused.
 *   JR4 THE COLUMN AT A BYTE OFFSET: the inverse of JR3: how many
 *       characters end at or before off, where off must be a character's
 *       boundary (0, or where one ends); an offset inside a character,
 *       past the line or negative is refused.
 *   JR5 THE BUFFER'S LIMITS: the file 0..JOINERY_FILE_MAX bytes, its
 *       longest line 0..JOINERY_LINE_MAX bytes (its newline aside) and no
 *       longer than the file, and the undo history 0..JOINERY_UNDO_MAX
 *       steps.
 *   JR6 AN UNDO RECORD of ^ZXJOINERY holds integers of at most the digits
 *       its pieces declare, sign aside (blocks/joinery/undo.myra): step 4,
 *       offset, removed and inserted 8 each.
 *   JR7 HOW FULL THE BUFFER IS of its JOINERY_FILE_MAX bytes, in
 *       hundredths of a percent: used_share.used_x100's floor(10000 *
 *       (total - left) / total), total JOINERY_FILE_MAX and left what is
 *       still free. */
#ifndef JOINERY_RULES_H
#define JOINERY_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define JOINERY_FILE_MAX      INT64_C(16777216) /* 16 MiB in one file */
#define JOINERY_LINE_MAX      INT64_C(65536)    /* bytes in one line, its newline aside */
#define JOINERY_LINEBYTES_MAX (JOINERY_LINE_MAX + 1)
#define JOINERY_UNDO_MAX      1024u /* steps of undo history */
#define JOINERY_REFUSED       INT64_C(-1)

/* JR1 */
static inline int64_t joinery_line_start(const int64_t *linebytes, uint32_t n, int64_t line)
{
    int64_t s = 0;
    if ((n && !linebytes) || line < 0 || line > (int64_t) n) return JOINERY_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (linebytes[i] < 1 || linebytes[i] > JOINERY_LINEBYTES_MAX) return JOINERY_REFUSED;
        if ((int64_t) i < line) s += linebytes[i];
    }
    return s;
}

/* JR2 */
static inline int64_t joinery_offset_line(const int64_t *linebytes, uint32_t n, int64_t off)
{
    int64_t end = 0, line = 0;
    if ((n && !linebytes) || off < 0 || off > JOINERY_FILE_MAX) return JOINERY_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (linebytes[i] < 1 || linebytes[i] > JOINERY_LINEBYTES_MAX) return JOINERY_REFUSED;
        end += linebytes[i];
        line += end <= off;
    }
    return line;
}

/* JR3 */
static inline int64_t joinery_col_offset(const int64_t *width, uint32_t n, int64_t col)
{
    int64_t s = 0;
    if ((n && !width) || col < 0 || col > (int64_t) n) return JOINERY_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (width[i] < 1 || width[i] > 4) return JOINERY_REFUSED;
        if ((int64_t) i < col) s += width[i];
    }
    return s;
}

/* JR4 */
static inline int64_t joinery_offset_col(const int64_t *width, uint32_t n, int64_t off)
{
    int64_t end = 0, col = 0;
    bool on = off == 0;
    if ((n && !width) || off < 0) return JOINERY_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (width[i] < 1 || width[i] > 4) return JOINERY_REFUSED;
        end += width[i];
        col += end <= off;
        on = on || end == off;
    }
    return on ? col : JOINERY_REFUSED;
}

/* JR5 */
static inline bool joinery_buffer_ok(int64_t filebytes, int64_t linebytes, int64_t undosteps)
{
    return filebytes >= 0 && filebytes <= JOINERY_FILE_MAX && linebytes >= 0 &&
           linebytes <= JOINERY_LINE_MAX && linebytes <= filebytes && undosteps >= 0 &&
           undosteps <= (int64_t) JOINERY_UNDO_MAX;
}

/* JR6: does x have at most `digits` digits, sign aside? */
static inline bool joinery_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool joinery_undo_fits(int64_t step, int64_t offset, int64_t removed,
                                     int64_t inserted)
{
    return joinery_digits_fit(step, 4) && joinery_digits_fit(offset, 8) &&
           joinery_digits_fit(removed, 8) && joinery_digits_fit(inserted, 8);
}

/* JR7 (@num.used_x100) */
static inline int64_t joinery_fill_x100(int64_t bytes)
{
    if (bytes < 0 || bytes > JOINERY_FILE_MAX) return JOINERY_REFUSED;
    return bytes * 10000 / JOINERY_FILE_MAX;
}

/* A UTF-8 lead byte's width (1..4), 0 for a continuation or no lead. */
static inline int64_t joinery_utf8_width(uint8_t lead)
{
    return lead < 0x80   ? 1
           : lead < 0xc2 ? 0
           : lead < 0xe0 ? 2
           : lead < 0xf0 ? 3
           : lead < 0xf5 ? 4
                         : 0;
}

#endif
