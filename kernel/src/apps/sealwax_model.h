/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sealwax_model.h — Sealwax, fill and sign forms, as a model (zxm.h): a
 * form kept as a text file in the Hoard (files.read, files.write), filled
 * field by field, written as a one-page PDF and printed (print.status,
 * print.submit), and a signed copy of it checked (web4.verify). No
 * drawing, no allocation, integers only; the rules are sealwax_rules.h.
 *
 *   SM1 THE FORM. A title of at most SEALWAX_TITLE_MAX bytes and up to
 *       SEALWAX_FIELDS fields, each with a name ([a-z0-9_], 1 to
 *       SEALWAX_NAME_MAX bytes, unique in the form), a kind, a required flag
 *       and bounds inside SR1. A field that would break any of these is not
 *       added.
 *   SM2 FILLING. A value is at most SEALWAX_VALUE_MAX characters (UTF-8
 *       code points) with no control character. An amount is entered as
 *       "units" or "units.c" or "units.cc" (no sign, no grouping) and kept
 *       in minor units by SR4; a number is an optional '-' and at most 15
 *       digits. A value that cannot be read is refused and the field keeps
 *       what it had; an empty value empties the field. Bounds are not a
 *       reason to refuse a value: a value outside them is kept and counted
 *       (SR2), so the owner sees what to fix.
 *   SM3 THE REVIEW, after every change: what is missing and outside (SR2),
 *       ready to sign (SR3), the fields left in fill order (SR5; a field
 *       outside its bounds counts as not filled) and the total of the
 *       amount fields (SR7). The notice says it in words and always ends
 *       with the advice line: Sealwax never signs, prints or sends a form
 *       by itself, and legal and financial forms are advice only.
 *   SM4 THE FILE. Lines end with "\n":
 *           sealwax 1
 *           title <title>
 *           field <name> <text|amount|number> <0|1> <lo> <hi>[ <value>]
 *       amounts written as units.cc. Saving writes it whole (files.write);
 *       opening reads it whole (files.read) and keeps the old form when the
 *       file does not hold one that fits SM1 and SM2.
 *   SM5 PRINTING. The form (its title, each field, the total, ready or
 *       DRAFT, and a line for a hand signature) is written as a one-page
 *       PDF in 10-point Courier (characters outside US-ASCII as '?', lines
 *       cut at SEALWAX_PDF_COLS), then printed with print.submit once
 *       print.status says the printer accepts jobs and takes PDF. Only on
 *       the owner's word (confirmed): without it nothing is sent.
 *   SM6 A SIGNED COPY is the hex of a Web4 envelope the owner signed
 *       elsewhere, kept in a file. It is read (files.read) and checked with
 *       web4.verify against the owner's key. zxvd gives back the size of
 *       what was signed and not the bytes, so the copy is matched to the
 *       form by size only: bound when the signature holds, it is a
 *       REQUEST, RESPONSE or STREAM, its payload is as many bytes as the
 *       form's text, and the form has not changed since it was saved or
 *       opened. */
#ifndef SEALWAX_MODEL_H
#define SEALWAX_MODEL_H

#include "sealwax_rules.h"
#include "zxm.h"

#define SEALWAX_NAME_MAX    31u
#define SEALWAX_TITLE_MAX   79u
#define SEALWAX_VALUE_BYTES (4u * SEALWAX_VALUE_MAX) /* UTF-8, four bytes a character at most */
#define SEALWAX_TEXT_MAX    24576u                   /* bytes of a form's file */
#define SEALWAX_SIG_MAX     16000u /* hex characters of a signed copy, inside a request line */
#define SEALWAX_PDF_MAX     16384u
#define SEALWAX_PDF_COLS    78u
#define SEALWAX_PATH_MAX    255u
#define SEALWAX_URI_MAX     511u
#define SEALWAX_STATE_MAX   16u
#define SEALWAX_REASON_MAX  63u
#define SEALWAX_NOTICE_MAX  319u
#define SEALWAX_COPIES_MAX  999u /* print.submit */

typedef struct {
    char name[SEALWAX_NAME_MAX + 1];
    int64_t kind, required, lo, hi;
    char value[SEALWAX_VALUE_BYTES + 1];
    uint32_t bytes; /* of value */
    int64_t chars;  /* code points of value */
    int64_t num;    /* an amount's minor units or a number; 0 for text */
    bool filled;    /* SR2 */
    bool outside;   /* SR2: filled and not inside lo..hi */
} sealwax_field_t;

typedef struct {
    bool checked; /* a signed copy was checked */
    bool valid;   /* web4.verify: the signature holds */
    bool bound;   /* SM6: and it is this form's, by size */
    char kind[SEALWAX_STATE_MAX];
    char reason[SEALWAX_REASON_MAX + 1]; /* why not valid */
    char from[65];                       /* the signer's agent id, hex */
    int64_t ts;                          /* when it was signed, unix seconds; -1 not said */
    int64_t payload_bytes;
} sealwax_sig_t;

typedef struct {
    zxm_link_t *link;
    char space[ZXM_SPACE_MAX + 1]; /* "" = home */
    char path[SEALWAX_PATH_MAX + 1];
    int64_t form; /* its number, for its field records (SR6) */
    char title[SEALWAX_TITLE_MAX + 1];
    sealwax_field_t field[SEALWAX_FIELDS];
    uint32_t nfields;
    /* SM3 */
    int64_t missing, outside;
    bool ready;
    int64_t order[SEALWAX_FIELDS]; /* field numbers from 1; {0} when none is left */
    uint32_t norder;
    int64_t total; /* the amount fields' sum, minor units */
    bool total_ok; /* false: past 64 bits (SR7) */
    char notice[SEALWAX_NOTICE_MAX + 1];
    /* SM4 */
    bool dirty;         /* changed since saved or opened */
    uint32_t saved_len; /* bytes of the form's text as last saved or opened */
    /* SM5 */
    bool printer_known, accepting, takes_pdf;
    int64_t job; /* 0: none sent */
    char job_state[SEALWAX_STATE_MAX];
    /* SM6 */
    sealwax_sig_t sig;
} sealwax_t;

void sealwax_init(sealwax_t *s, zxm_link_t *link);

/* SM1 */
int sealwax_set_title(sealwax_t *s, const char *title);
int sealwax_set_form(sealwax_t *s, int64_t form); /* 0..999999999 */
int sealwax_add_field(sealwax_t *s, const char *name, int64_t kind, int64_t required, int64_t lo,
                      int64_t hi);
int32_t sealwax_find(const sealwax_t *s, const char *name); /* its index, or -1 */

/* SM2: field i (from 0) */
int sealwax_fill(sealwax_t *s, uint32_t i, const char *value);

/* SR6: does field i make a ^ZXSEALWAX record? */
bool sealwax_record(const sealwax_t *s, uint32_t i);

/* SM4: the form as its file's text; its length, or -1 when it does not fit */
int32_t sealwax_text(const sealwax_t *s, char *out, uint32_t cap);
int sealwax_parse(sealwax_t *s, const char *text, uint32_t n);
int sealwax_open(sealwax_t *s, const char *space, const char *path);
int sealwax_save(sealwax_t *s, const char *space, const char *path);

/* SM5 */
int sealwax_export_pdf(sealwax_t *s, const char *pdf);
const char *sealwax_pdf_text(uint32_t *len); /* the PDF last composed */
int sealwax_printer(sealwax_t *s, const char *uri);
int sealwax_print(sealwax_t *s, const char *uri, const char *pdf, uint32_t copies, bool confirmed);

/* SM6: the signed copy in sigpath, in the form's space */
int sealwax_check_signed(sealwax_t *s, const char *sigpath);

#endif
