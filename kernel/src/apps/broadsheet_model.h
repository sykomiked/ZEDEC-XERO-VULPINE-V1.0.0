/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* broadsheet_model.h — Broadsheet, the document viewer, as a model (zxm.h):
 * a PDF in the Hoard opened and checked, fitted to a view, annotated in a
 * file of its own beside it, and printed. The rules are broadsheet_rules.h.
 *
 * zxvd methods used: files.read (base64 for the document, utf8 for its
 * notes), files.write (the notes only), print.status and print.submit. No
 * drawing, no allocation, integers only. The file is read in windows of
 * BROADSHEET_WIN bytes, under zxvd's 49152 a read.
 *
 *   BM1 OPENING. The path ends ".pdf" (any case) and leaves room for its
 *       notes' name. The header is present when the file starts "%PDF-",
 *       a digit, "." and a digit.
 *   BM2 WELL FORMED. The last "startxref" in the file's last
 *       BROADSHEET_TAIL bytes gives the first cross-reference offset; it
 *       must be followed by "%%EOF". Each offset is checked by BR2 before
 *       anything is read at it. At an offset is a cross-reference table
 *       ("xref", then subsections of "first count" and count entries of
 *       ten offset digits, five generation digits and n or f); the offset
 *       of every in-use (n) entry is checked by BR2, and the trailer's
 *       /Prev, when it has one, is the next table's offset (at most
 *       BROADSHEET_SECTIONS tables, none twice). The document is accepted
 *       when every offset is inside the file (BR1 over the document); the
 *       first fault found is kept and nothing more is read:
 *       BROADSHEET_F_HEADER and BROADSHEET_F_BOUNDS are BR2's 1 and 2,
 *       BROADSHEET_F_FORM is no startxref, no %%EOF after it or a table
 *       that does not parse, BROADSHEET_F_STREAM a cross-reference stream
 *       (compressed; Broadsheet has no decoder for it, so it is refused,
 *       not guessed), BROADSHEET_F_LARGE more than BROADSHEET_XREFS
 *       offsets or BROADSHEET_SECTIONS tables. A file that changes size
 *       while it is read stops the open (ZXM_E_STATE).
 *   BM3 THE PAGE. Its size in points is the first "/MediaBox [llx lly urx
 *       ury]" in the file's first BROADSHEET_WIN bytes, each number's
 *       fraction dropped (width urx - llx, height ury - lly, each
 *       1..BROADSHEET_BOX_MAX); otherwise US Letter, 612 x 792.
 *   BM4 ZOOM TO FIT. The page at its own size is 96 pixels an inch, a
 *       point 4/3 of a pixel (rounded down, at least 1); the zoom that fits
 *       it in a view is BR3.
 *   BM5 ANNOTATIONS go to "<path>.notes" beside the document, never into
 *       it: a first line "broadsheet notes 1", then one line a note,
 *       "note page x y kind stamp text", the text 0..BROADSHEET_TEXT bytes
 *       with no control character. Every record is BR4, its number unique;
 *       a notes file that does not parse is refused whole and never
 *       overwritten (saving is ZXM_E_STATE until a good one is read). A new
 *       note takes the next number after the highest. Saving writes the
 *       file whole, and only over a notes file that was read: when there
 *       was none, the write is exclusive, so it never replaces one made
 *       meanwhile.
 *   BM6 PRINTING sends the document as it is with print.submit, once
 *       print.status says the printer accepts jobs and takes PDF. Only an
 *       accepted document is printed; the owner chooses the printer and
 *       the copies (0 the printer's default, at most 999). */
#ifndef BROADSHEET_MODEL_H
#define BROADSHEET_MODEL_H

#include "broadsheet_rules.h"
#include "zxm.h"

#define BROADSHEET_PATH_MAX   255u
#define BROADSHEET_SUFFIX     ".notes"
#define BROADSHEET_WIN        8192u /* bytes in one read */
#define BROADSHEET_TAIL       1024u /* BM2: where startxref is looked for */
#define BROADSHEET_XREFS      4096u /* BM2: offsets checked in one file */
#define BROADSHEET_SECTIONS   8u    /* BM2: cross-reference tables followed */
#define BROADSHEET_TRAILER    4096u /* BM2: bytes of a trailer looked at for /Prev */
#define BROADSHEET_BOX_MAX    14400 /* BM3: points, PDF's largest page (200 inches) */
#define BROADSHEET_LETTER_W   612
#define BROADSHEET_LETTER_H   792
#define BROADSHEET_NOTES      32u   /* BM5: notes in one document */
#define BROADSHEET_TEXT       120u  /* BM5: bytes of a note's text */
#define BROADSHEET_NOTES_MAX  8192u /* BM5: bytes of a notes file */
#define BROADSHEET_URI_MAX    511u
#define BROADSHEET_STATE_MAX  16u
#define BROADSHEET_COPIES_MAX 999u
#define BROADSHEET_E_FILES    (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */

/* Why a document is refused (broadsheet_t.fault), BM2. */
enum {
    BROADSHEET_F_NONE = 0,
    BROADSHEET_F_HEADER = 1, /* BR2 1 */
    BROADSHEET_F_BOUNDS = 2, /* BR2 2 */
    BROADSHEET_F_FORM = 3,
    BROADSHEET_F_STREAM = 4,
    BROADSHEET_F_LARGE = 5,
};

typedef struct {
    int64_t note, page, x, y, kind, stamp; /* BR4 */
    char text[BROADSHEET_TEXT + 1];
} broadsheet_note_t;

typedef struct {
    zxm_link_t *link;
    /* the document open (BM1), "" for none */
    char path[BROADSHEET_PATH_MAX + 1];
    char notes_path[BROADSHEET_PATH_MAX + 1];
    int64_t size;
    int64_t header; /* 1 present, 0 not */
    int64_t xref[BROADSHEET_XREFS];
    uint32_t nxref;   /* the offsets checked so far */
    uint32_t tables;  /* cross-reference tables read */
    int64_t accepted; /* BR1 over the document: 1, 0 */
    int32_t fault;
    int64_t fault_at;       /* the offset that failed, -1 for none */
    int64_t page_w, page_h; /* BM3, points */
    bool box_found;         /* from the file, not the default */
    int64_t zoom;           /* BM4, x100; 0 until fitted */
    broadsheet_note_t note[BROADSHEET_NOTES];
    uint32_t nnotes;
    bool notes_loaded; /* BM5: a notes file read, or none there */
    bool notes_found;  /* one is there and was read */
    bool notes_dirty;  /* changed since read or saved */
    /* BM6 */
    bool printer_known, accepting, takes_pdf;
    int64_t job; /* 0: none sent */
    char job_state[BROADSHEET_STATE_MAX];
    /* the read window (BM2) */
    uint8_t win[BROADSHEET_WIN];
    int64_t wbase;
    uint32_t wlen;
    int rc; /* the first read error in a parse */
} broadsheet_t;

void broadsheet_init(broadsheet_t *b, zxm_link_t *link);

/* BM1..BM3, then BM5's notes. ZXM_E_ARG with fault set when the document is
 * refused; ZXM_E_ARG with fault BROADSHEET_F_NONE and notes_loaded false
 * when it is accepted but its notes file does not parse. */
int broadsheet_open(broadsheet_t *b, const char *path);

/* BM4: ZXM_E_STATE with no accepted document, ZXM_E_ARG when BR3 refuses. */
int broadsheet_fit(broadsheet_t *b, int64_t vieww, int64_t viewh);

/* BM5: a note added (its number in *note), removed, the notes saved. */
int broadsheet_note_add(broadsheet_t *b, int64_t page, int64_t x, int64_t y, int64_t kind,
                        int64_t stamp, const char *text, int64_t *note);
int broadsheet_note_remove(broadsheet_t *b, int64_t note);
int broadsheet_notes_save(broadsheet_t *b);

/* BM6 */
int broadsheet_printer(broadsheet_t *b, const char *uri);
int broadsheet_print(broadsheet_t *b, const char *uri, uint32_t copies);

#endif
