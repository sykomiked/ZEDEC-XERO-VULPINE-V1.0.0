/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* folio_model.h — Folio, documents with layout, as a model (zxm.h):
 * letters, reports and books kept as UTF-8 text files in the Hoard
 * (files.read, files.write), laid out on a page, written as a PDF and
 * printed (print.status, print.submit, print.job). No drawing, no
 * allocation, integers only; the rules are folio_rules.h.
 *
 *   FM1 THE TEXT. Lines end with "\n". A line "# title", "## title" or
 *       "### title" is a heading of level 1, 2 or 3; a line holding only
 *       a form feed ("\f") starts a new page; every other line is a
 *       paragraph, an empty line a blank line.
 *   FM2 THE PAGE. Width, height, margins and leading in twips (1440 to the
 *       inch), inside FR6; the lines per page are FR1, the columns FR5 (the
 *       text width over FOLIO_CHAR_TWIPS, one 10-point Courier character).
 *       A page with no line or fewer than FOLIO_COLS_MIN columns is
 *       refused. The default is US Letter with one-inch margins and a
 *       12-point leading (54 lines of 78 columns).
 *   FM3 LINE BREAKING (C, text work). A paragraph is broken at spaces into
 *       lines of at most the columns, counted in characters (UTF-8 code
 *       points); a word longer than a line is cut. A form feed pads to the
 *       end of its page. The pages are FR2 of the laid lines. A heading is
 *       an outline entry (level, page and line from 1) whose record must
 *       fit FR7. A layout that does not fit (FOLIO_ROWS lines,
 *       FOLIO_OUTLINE headings, FOLIO_PAGES pages) is refused and the last
 *       layout is kept.
 *   FM4 AN AMOUNT TABLE. Items (a description, a quantity in thousandths, a
 *       unit price in minor units): each amount is FR3, the total FR4. An
 *       item that would be refused, or would make the total refused, is
 *       not added. folio_put_table writes the table into the text, the
 *       amounts with two decimals, the quantities with three.
 *   FM5 SAVING writes the text whole (files.write); opening reads it whole
 *       (files.read) and keeps the old document when it does not fit.
 *   FM6 THE PDF. One page object per laid page, US-ASCII text in Courier
 *       (a character outside it shows as '?'), positions in hundredths of a
 *       point (a twip is 5 of them). Written with files.write, then printed
 *       with print.submit once print.status says the printer accepts jobs
 *       and takes PDF; print.job follows the job. The owner chooses the
 *       printer and the copies; Folio never prints on its own. */
#ifndef FOLIO_MODEL_H
#define FOLIO_MODEL_H

#include "folio_rules.h"
#include "zxm.h"

#define FOLIO_TEXT_MAX   32768u /* bytes of a document's text */
#define FOLIO_ROWS       4096u  /* laid lines */
#define FOLIO_PAGES      256u
#define FOLIO_OUTLINE    64u
#define FOLIO_TITLE      63u /* bytes of a heading kept in the outline */
#define FOLIO_ITEMS      32u
#define FOLIO_DESC       32u /* bytes of an item's description, and its column */
#define FOLIO_QTY_W      12u /* the table's other columns, in characters: */
#define FOLIO_PRICE_W    14u /* 74 in all, inside a Letter page's 78 */
#define FOLIO_AMOUNT_W   16u
#define FOLIO_PATH_MAX   255u
#define FOLIO_URI_MAX    511u
#define FOLIO_STATE_MAX  16u
#define FOLIO_PDF_MAX    (512u * 1024u)
#define FOLIO_FONT_PT    10
#define FOLIO_COLS_MIN   20
#define FOLIO_COPIES_MAX 999 /* print.submit */

typedef struct {
    int64_t width, height, left, right, top, bottom, leading; /* twips */
} folio_page_t;

typedef struct {
    uint32_t at, len; /* bytes of the text; len 0 is a blank line */
} folio_row_t;

typedef struct {
    int64_t level, page, line; /* page and line from 1 */
    char title[FOLIO_TITLE + 1];
} folio_head_t;

typedef struct {
    char desc[FOLIO_DESC + 1];
    int64_t qty;   /* thousandths of a unit */
    int64_t price; /* minor units */
    int64_t amount;
} folio_item_t;

typedef struct {
    zxm_link_t *link;
    char space[ZXM_SPACE_MAX + 1]; /* "" = home */
    char path[FOLIO_PATH_MAX + 1]; /* the document's text file */
    int64_t doc;                   /* its number, for its outline records */
    folio_page_t page;
    int64_t per_page, cols;
    char text[FOLIO_TEXT_MAX + 1];
    uint32_t len;
    bool dirty; /* changed since saved */
    folio_row_t row[FOLIO_ROWS];
    uint32_t nrows;
    int64_t pages;
    bool laid; /* the layout is of the text as it is */
    folio_head_t head[FOLIO_OUTLINE];
    uint32_t nheads;
    folio_item_t item[FOLIO_ITEMS];
    uint32_t nitems;
    int64_t total; /* the items' total, minor units */
    bool printer_known, accepting, takes_pdf;
    int64_t job; /* 0: none sent */
    char job_state[FOLIO_STATE_MAX];
    int64_t job_pages; /* -1: the printer does not say */
} folio_t;

void folio_init(folio_t *f, zxm_link_t *link);

/* FM2: ZXM_E_ARG outside FR6 or with no room for a line or for
 * FOLIO_COLS_MIN columns; the page is kept. */
int folio_set_page(folio_t *f, const folio_page_t *p);

/* FM1: the document's whole text (n bytes, valid UTF-8 not checked here:
 * zxvd checks it on write). ZXM_E_TOOBIG past FOLIO_TEXT_MAX. */
int folio_set_text(folio_t *f, const char *text, uint32_t n);
int folio_set_doc(folio_t *f, int64_t doc); /* 0..999999999 */

/* FM3 */
int folio_layout(folio_t *f);

/* FM4 */
int folio_add_item(folio_t *f, const char *desc, int64_t qty, int64_t price);
int folio_clear_items(folio_t *f);
int folio_put_table(folio_t *f);
/* minor units with two decimals ("-12.05"); the bytes written, or -1 */
int32_t folio_money(int64_t minor, char *out, uint32_t cap);

/* FM5 */
int folio_open(folio_t *f, const char *space, const char *path);
int folio_save(folio_t *f, const char *space, const char *path);

/* FM6 */
int folio_export_pdf(folio_t *f, const char *pdf);
int folio_printer(folio_t *f, const char *uri);
int folio_print(folio_t *f, const char *uri, const char *pdf, uint32_t copies);
int folio_follow(folio_t *f, const char *uri);

/* The PDF last composed (for the view's preview and the tests). */
const char *folio_pdf_text(uint32_t *len);

#endif
