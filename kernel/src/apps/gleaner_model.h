/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gleaner_model.h — Gleaner, datasets, as a model (zxm.h): a dataset is a
 * CSV file in the Hoard; the model reads it into rows, splits them into a
 * training and a test set, counts the rows of each class, filters them over
 * their columns, writes the two sets back into the Hoard, names a selection
 * by its CID and publishes it to this node's blockstore. The rules are
 * gleaner_rules.h.
 *
 * zxvd methods used: files.list, files.read, files.write, search.query,
 * web4.cid and aipi.put. No drawing, no allocation, integers only.
 *
 *   GM1 A DATASET is UTF-8 text: a header line, then one line per row, each
 *       line ending in LF (a CR before it is dropped; the last LF may be
 *       left out). Fields are split on ',' with no quoting: a field holding
 *       '"' or a control byte is refused. The header names each column once
 *       and must name label, source and date; pii is optional. A label or a
 *       source is 1..GLEANER_NAME bytes; each distinct one gets an index in
 *       order of first appearance (at most GLEANER_CLASSES of each). A date
 *       is YYYYMMDD (month 1..12, day 1..31). pii is 0 or 1. Every row has
 *       the header's count of columns, and the dataset fits GR3.
 *   GM2 PERSONAL DATA. A row is personal when its pii is 1; with no pii
 *       column every row is personal, since nothing says otherwise.
 *   GM3 THE SELECTION is the header and every kept row, in file order, each
 *       line ending in LF. Opening keeps every row; gleaner_filter keeps the
 *       rows that match (GR2). The counts (witness.kind_count, WR2) are of
 *       the kept rows, and of those in the test set.
 *   GM4 THE SPLIT marks each row test or train by its index in the file
 *       (GR1), so it does not change when a filter does; gleaner_save
 *       writes the kept training rows and the kept test rows to two new
 *       files in the Hoard (never over a file that is there).
 *   GM5 LEAVING. Writing into the Hoard and naming the selection by its CID
 *       (web4.cid, computed by zxvd on this device) keep it inside.
 *       gleaner_publish puts the selection into the blockstore (aipi.put)
 *       only when it may leave (GR5): no kept row is personal, or the owner
 *       said yes to this very selection (gleaner_consent). A selection with
 *       a personal row is sealed at rest (private). Opening and filtering
 *       take the consent back: it never carries over to other rows. */
#ifndef GLEANER_MODEL_H
#define GLEANER_MODEL_H

#include "gleaner_rules.h"
#include "zxm.h"

#define GLEANER_NAME     31u  /* bytes of a label or a source */
#define GLEANER_CLASSES  64u  /* labels, and sources, of one dataset */
#define GLEANER_PATH_MAX 255u /* a path in the Hoard that Gleaner keeps */
#define GLEANER_LIST     64u  /* files kept from one folder or search */
#define GLEANER_HITS     20u  /* search.query's max */
#define GLEANER_CID_MAX  127u

/* GM1: why a text is not a dataset (gleaner_t.fault). */
enum {
    GLEANER_PARSED = 0,
    GLEANER_F_HEADER = 1,  /* a name twice or empty; label, source or date missing */
    GLEANER_F_RAGGED = 2,  /* a row whose count of columns is not the header's */
    GLEANER_F_FIELD = 3,   /* a '"' or control byte, or a label or source empty or too long */
    GLEANER_F_DATE = 4,    /* not YYYYMMDD */
    GLEANER_F_PII = 5,     /* pii not 0 or 1 */
    GLEANER_F_CLASSES = 6, /* more than GLEANER_CLASSES labels or sources */
    GLEANER_F_SIZE = 7,    /* rows, columns or bytes outside GR3 */
};

typedef struct {
    int64_t label, source, date; /* indexes into the tables; YYYYMMDD */
    bool pii;                    /* GM2 */
    bool test;                   /* GM4 */
    bool kept;                   /* GM3 */
    uint16_t off, len;           /* the row's bytes in text, no line end */
} gleaner_row_t;

typedef struct {
    char path[GLEANER_PATH_MAX + 1];
    uint64_t size;
    int64_t mtime;
} gleaner_file_t;

typedef struct {
    zxm_link_t *link;
    char path[GLEANER_PATH_MAX + 1]; /* the dataset open, "" for none */
    char text[GLEANER_BYTES + 1];
    uint32_t len;
    uint16_t head_len; /* the header's bytes, no line end */
    uint32_t cols;
    bool has_pii;
    gleaner_row_t row[GLEANER_ROWS];
    uint32_t nrows;
    char label[GLEANER_CLASSES][GLEANER_NAME + 1];
    uint32_t nlabels;
    char source[GLEANER_CLASSES][GLEANER_NAME + 1];
    uint32_t nsources;
    int32_t fault;       /* GM1, of the last text that was refused */
    uint32_t fault_line; /* its line, 1 the header */
    /* GM3, GM4 */
    int64_t share; /* the split's test share, -1 before gleaner_split */
    uint32_t nkept, nkept_test;
    int64_t count[GLEANER_CLASSES];      /* kept rows of each class */
    int64_t count_test[GLEANER_CLASSES]; /* of those, in the test set */
    uint32_t personal;                   /* kept rows that are personal */
    /* GM5 */
    bool consent;
    char cid[GLEANER_CID_MAX + 1];   /* web4.cid of the selection */
    char block[GLEANER_CID_MAX + 1]; /* aipi.put's CID, once published */
    bool sealed;                     /* the block was put private */
    /* files.list or search.query: the CSV files found */
    gleaner_file_t file[GLEANER_LIST];
    uint32_t nfiles;
} gleaner_t;

void gleaner_init(gleaner_t *g, zxm_link_t *link);

/* GM1: take text[0..n) as the dataset (from path, or "" when it came from
 * no file); on a refusal the dataset open stays and fault says why. */
int gleaner_parse(gleaner_t *g, const char *path, const char *text, uint32_t n);

/* files.list: the CSV files in a folder of the Hoard ("" its top). */
int gleaner_list(gleaner_t *g, const char *folder);
/* search.query: the CSV files whose name, folders or text hold every word. */
int gleaner_find(gleaner_t *g, const char *words);
/* files.read: open the dataset at path (GM1). ZXM_E_TOOBIG past GLEANER_BYTES. */
int gleaner_open(gleaner_t *g, const char *path);

/* GM4: split at share (hundredths of a percent, 0..10000). */
int gleaner_split(gleaner_t *g, int64_t share);
/* GM3: keep the rows that match (GR2): lab and src are indexes (negative:
 * any), from..to dates YYYYMMDD. */
int gleaner_filter(gleaner_t *g, int64_t lab, int64_t src, int64_t from, int64_t to);
/* The index of a label or a source by its name, or -1. */
int64_t gleaner_label_of(const gleaner_t *g, const char *name);
int64_t gleaner_source_of(const gleaner_t *g, const char *name);
/* GM3: the selection's bytes into out (at most cap); its length, or -1. */
int32_t gleaner_selection(const gleaner_t *g, char *out, uint32_t cap);

/* GM4: files.write the kept training and test rows to two new files. */
int gleaner_save(gleaner_t *g, const char *train_path, const char *test_path);
/* GM5: web4.cid of the selection (g->cid). */
int gleaner_name(gleaner_t *g);
/* GM5: the owner's word on the personal rows of the current selection. */
void gleaner_consent(gleaner_t *g, bool yes);
/* GM5: aipi.put the selection (g->block); ZXM_E_STATE, nothing sent, when
 * it may not leave. */
int gleaner_publish(gleaner_t *g);

#endif
