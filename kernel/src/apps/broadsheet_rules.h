/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* broadsheet_rules.h — Broadsheet's rules in C, the reference its Eve, KRON,
 * Rust, Sutra and MYRA blocks are checked against (blocks/broadsheet). Pure
 * integer functions, no state: the model (broadsheet_model.c) and the
 * language test (tests/broadsheet_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   BR1 ONE CROSS-REFERENCE OFFSET IS WELL FORMED when the document's
 *       header is present (header 1), the offset is 0..BROADSHEET_FILE_MAX-1
 *       and room, the file's size less the offset, is 1..BROADSHEET_FILE_MAX:
 *       the offset names a byte inside the file. 1 well formed, 0 not.
 *       BROADSHEET_FILE_MAX is zxvd's largest file (zxv_files.h
 *       ZXF_FILE_MAX, 256 MiB), so no offset past it is a measurement.
 *   BR2 WHY IT IS NOT, the first that applies: 1 no header (header 0), 2
 *       the offset outside the file (below 0, or room below 1), 0 none. A
 *       header that is neither 0 nor 1, an offset past BROADSHEET_FILE_MAX-1
 *       or a room past BROADSHEET_FILE_MAX is no measurement: refused (-1).
 *       Inside that domain BR1 is 1 exactly when BR2 is 0.
 *   BR1 over a document: broadsheet_all_inside is 1 when the file has at
 *       least one cross-reference offset and every one is BR1 against the
 *       file's size; a size outside 0..BROADSHEET_FILE_MAX is refused (-1).
 *   BR3 ZOOM TO FIT a page in a view, in hundredths of a percent (10000 is
 *       the page at its own size): min(floor(10000 * vieww / pagew),
 *       floor(10000 * viewh / pageh)). Page sides 1..99999 pixels, view
 *       sides 0..99999 (Sutra's 9(5)); anything else refused (-1).
 *   BR4 AN ANNOTATION'S RECORD fits ^ZXBROAD: note 1..9999, page 1..999999,
 *       x and y 0..99999 points from the page's lower left corner, kind
 *       1..3 (BROADSHEET_HIGHLIGHT, BROADSHEET_NOTE, BROADSHEET_UNDERLINE)
 *       and stamp 0..999999999999999 ms since 1970. */
#ifndef BROADSHEET_RULES_H
#define BROADSHEET_RULES_H

#include <stdint.h>

#define BROADSHEET_FILE_MAX  268435456 /* BR1: zxvd's ZXF_FILE_MAX */
#define BROADSHEET_SIDE_MAX  99999     /* BR3: pixels */
#define BROADSHEET_X100      10000     /* BR3: 100.00 % */
#define BROADSHEET_NOTE_MAX  9999      /* BR4 */
#define BROADSHEET_PAGE_MAX  999999
#define BROADSHEET_POINT_MAX 99999
#define BROADSHEET_STAMP_MAX INT64_C(999999999999999)
#define BROADSHEET_REFUSED   (-1)

enum { BROADSHEET_HIGHLIGHT = 1, BROADSHEET_NOTE = 2, BROADSHEET_UNDERLINE = 3 };

/* BR1 */
static inline int64_t broadsheet_well_formed(int64_t header, int64_t off, int64_t room)
{
    return header == 1 && off >= 0 && off <= BROADSHEET_FILE_MAX - 1 && room >= 1 &&
           room <= BROADSHEET_FILE_MAX;
}

/* BR2 */
static inline int64_t broadsheet_form_fault(int64_t header, int64_t off, int64_t room)
{
    if (header < 0 || header > 1 || off > BROADSHEET_FILE_MAX - 1 || room > BROADSHEET_FILE_MAX)
        return BROADSHEET_REFUSED;
    if (header == 0) return 1;
    if (off < 0 || room < 1) return 2;
    return 0;
}

/* BR1 over a document: off[0..n) its cross-reference offsets, size its bytes. */
static inline int64_t broadsheet_all_inside(int64_t header, const int64_t *off, uint32_t n,
                                            int64_t size)
{
    if (size < 0 || size > BROADSHEET_FILE_MAX) return BROADSHEET_REFUSED;
    if (!n || !off) return 0;
    for (uint32_t i = 0; i < n; i++)
        if (!broadsheet_well_formed(header, off[i], size - off[i])) return 0;
    return 1;
}

/* BR3 */
static inline int64_t broadsheet_fit_zoom(int64_t pagew, int64_t pageh, int64_t vieww,
                                          int64_t viewh)
{
    if (pagew < 1 || pagew > BROADSHEET_SIDE_MAX || pageh < 1 || pageh > BROADSHEET_SIDE_MAX ||
        vieww < 0 || vieww > BROADSHEET_SIDE_MAX || viewh < 0 || viewh > BROADSHEET_SIDE_MAX)
        return BROADSHEET_REFUSED;
    int64_t across = vieww * BROADSHEET_X100 / pagew, down = viewh * BROADSHEET_X100 / pageh;
    return down < across ? down : across;
}

/* BR4 */
static inline int64_t broadsheet_note_ok(int64_t note, int64_t page, int64_t x, int64_t y,
                                         int64_t kind, int64_t stamp)
{
    return note >= 1 && note <= BROADSHEET_NOTE_MAX && page >= 1 && page <= BROADSHEET_PAGE_MAX &&
           x >= 0 && x <= BROADSHEET_POINT_MAX && y >= 0 && y <= BROADSHEET_POINT_MAX &&
           kind >= BROADSHEET_HIGHLIGHT && kind <= BROADSHEET_UNDERLINE && stamp >= 0 &&
           stamp <= BROADSHEET_STAMP_MAX;
}

#endif
