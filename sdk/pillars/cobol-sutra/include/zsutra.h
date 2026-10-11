/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zsutra.h — COBOL -> Sutra, the first ZXV legacy-language pillar
 * (pillar API 0.1).
 *
 * A COBOL copybook is the record format ("the treaty") between a COBOL
 * program and everything else. libzsutra reads that format exactly:
 *
 *   1. LAYOUTS. Two front ends build the same record layout:
 *        - zs_copybook_parse: a documented subset of the COBOL data
 *          division (README.md, "Supported COBOL");
 *        - zs_sutra_parse: Sutra, a short line syntax for the same thing:
 *
 *              record payment
 *                id: x(12)
 *                amount: s9(13)v99 comp-3
 *
 *      Both give a zs_layout. Its canonical form is Sutra text, and
 *      zs_layout_digest is the SHA3-256 of that text, so a copybook and a
 *      Sutra file declare the same record exactly when their digests match.
 *
 *   2. VALUES. zs_get_* / zs_put_* convert between record bytes (DISPLAY
 *      zoned decimal with sign overpunch or a separate sign, COMP
 *      big-endian binary, COMP-3 packed decimal, PIC X text) and exact
 *      integers: an unscaled int64 plus the number of implied decimals, or
 *      a libzxv rational. Never through a float. Anything that does not
 *      fit exactly is refused (ZS_E_FIT); bytes that are not valid for the
 *      field are refused (ZS_E_DATA).
 *
 *   3. POSTINGS. zs_post maps a record (a payment) to one libzxv ledger
 *      posting through zxv_ledger_post_cap, with a capability the caller
 *      passes. Balancing (A = L + E) is the ledger's job; a refused
 *      posting comes back as the ledger's status (ZXV_E_FUNDS, ...).
 *
 * Honest limits: ASCII records only (no EBCDIC), no REDEFINES, no OCCURS
 * DEPENDING ON, no CICS or IMS, not a mainframe certification. README.md
 * lists every refusal.
 *
 * PILLAR API STABILITY. 0.x: it may change before 1.0. It calls only the
 * libzxv ABI 1.3 (zxv_api.h). Opaque handles, int32_t status returns,
 * fixed-width integers, no floats (R11).
 */
#ifndef ZSUTRA_H
#define ZSUTRA_H

#include <stddef.h>
#include <stdint.h>

#include "zxv_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZS_VERSION_MAJOR 0
#define ZS_VERSION_MINOR 1
#define ZS_VERSION_PATCH 0

/* ===== Status codes =====
 * 0 and -1 .. -99 are libzxv's (passed through unchanged, e.g. the
 * ledger's ZXV_E_FUNDS). The legacy-language pillars own -2001..;
 * cobol-sutra uses -2001 .. -2099. */
#define ZS_OK            0
#define ZS_E_SYNTAX      (-2001) /* the copybook or Sutra text does not parse        */
#define ZS_E_UNSUPPORTED (-2002) /* valid COBOL, outside the supported subset        */
                                 /* (REDEFINES, COMP-1, OCCURS DEPENDING ON, ...)    */
#define ZS_E_DATA        (-2003) /* the record bytes are not a valid value for the   */
                                 /* field (a bad digit, sign or packed nibble)        */
#define ZS_E_FIT         (-2004) /* the value does not fit exactly: too many digits, */
                                 /* more decimals than the field or currency has,    */
                                 /* a negative value for an unsigned field, text     */
                                 /* longer than the field                            */

/* Static, never-NULL name ("ZS_E_FIT"); other codes give libzxv's name. */
const char *zs_status_name(int32_t status);
/* The calling thread's last error detail (R4 two-call buffer). */
int32_t zs_last_error(char *buf, size_t cap, size_t *out_len);

/* ===== Limits ===== */
#define ZS_MAX_TEXT    (1024u * 1024u) /* bytes of copybook or Sutra text          */
#define ZS_MAX_FIELDS  4096u           /* data items in one layout                 */
#define ZS_MAX_NAME    30u             /* bytes in a data name (COBOL's limit)     */
#define ZS_MAX_DEPTH   48u             /* levels below 01 (01..49)                 */
#define ZS_MAX_DIGITS  18u             /* digits in a numeric picture (fits int64) */
#define ZS_MAX_OCCURS  65535u          /* largest OCCURS n                         */
#define ZS_MAX_DIMS    7u              /* nested OCCURS (COBOL's limit)            */
#define ZS_MAX_RECORD  (1024u * 1024u) /* bytes in one record                      */

/* ===== Layouts ===== */
typedef struct zs_layout zs_layout;

/* Where a parse was refused. line and col are 1-based (col counts bytes),
 * offset is 0-based. msg is one line, NUL-terminated, truncated to fit;
 * it starts "line L, column C: ". */
typedef struct {
    uint32_t line, col;
    uint64_t offset;
    char msg[256];
} zs_diag;

/* Copybook source format. Fixed is the classic card layout: columns 1-6
 * are a sequence area, column 7 is the indicator ('*' or '/' comment,
 * 'D' debug line, treated as a comment), columns 8-72 hold the code and
 * columns 73+ are ignored. Free format uses the whole line. Both accept
 * "*>" comments. */
#define ZS_CB_FIXED 0u
#define ZS_CB_FREE  1u

/* Parse a copybook (one 01 record). `text` need not be NUL-terminated;
 * `len` bytes are read (at most ZS_MAX_TEXT, valid UTF-8). Returns ZXV_OK
 * with *out_layout, ZS_E_SYNTAX or ZS_E_UNSUPPORTED (with *diag filled),
 * ZXV_E_ARG or ZXV_E_NOMEM. `diag` may be NULL. */
int32_t zs_copybook_parse(const char *text, size_t len, uint32_t format, zs_layout **out_layout,
                          zs_diag *diag);

/* Parse Sutra text (README.md, "Sutra"). Same contract as above. */
int32_t zs_sutra_parse(const char *text, size_t len, zs_layout **out_layout, zs_diag *diag);

void zs_layout_destroy(zs_layout *layout);

/* The canonical Sutra text of a layout (R4). zs_sutra_parse of it gives
 * the same layout. Names are lower case, pictures written out
 * (s9(13)v9(2)), no comments. */
int32_t zs_layout_sutra(const zs_layout *layout, char *buf, size_t cap, size_t *out_len);
/* The same layout as a fixed-format copybook (R4), levels 01/05/10/...
 * zs_copybook_parse of it gives the same layout. */
int32_t zs_layout_copybook(const zs_layout *layout, char *buf, size_t cap, size_t *out_len);
/* SHA3-256 of the canonical Sutra text: the layout's identity. */
int32_t zs_layout_digest(const zs_layout *layout, uint8_t out[32]);
/* *out_same = 1 when both layouts declare the same record, else 0. */
int32_t zs_layout_same(const zs_layout *a, const zs_layout *b, int32_t *out_same);
/* Bytes in one record. */
int32_t zs_layout_size(const zs_layout *layout, uint64_t *out_bytes);

/* Sign overpunch convention for signed DISPLAY fields without SEPARATE.
 * It is a property of the data, not of the layout, so it is not part of
 * the digest.
 *   IBM   (default) the EBCDIC zones as ASCII characters: +0..+9 are
 *         '{' 'A'..'I', -0..-9 are '}' 'J'..'R'; a plain digit reads as
 *         positive. GnuCOBOL with -fsign=EBCDIC writes this.
 *   ASCII GnuCOBOL's default and Micro Focus: positive is the plain
 *         digit, -0..-9 are 'p'..'y' (0x70 | digit). */
#define ZS_OVERPUNCH_IBM   0
#define ZS_OVERPUNCH_ASCII 1
int32_t zs_layout_set_overpunch(zs_layout *layout, int32_t style);

/* ===== Fields =====
 * Fields are numbered in declaration order; field 0 is the 01 record.
 * FILLER items are fields too (named "FILLER"), but zs_find never finds
 * them. */
#define ZS_K_GROUP 0 /* a group: its bytes, as text                       */
#define ZS_K_TEXT  1 /* PIC X(n)                                           */
#define ZS_K_NUM   2 /* PIC [S]9(i)[V9(d)]                                 */

#define ZS_U_DISPLAY 0 /* zoned decimal, one ASCII digit per byte          */
#define ZS_U_COMP    1 /* COMP / BINARY / COMP-4: big-endian binary        */
#define ZS_U_COMP3   2 /* COMP-3 / PACKED-DECIMAL: packed decimal          */

#define ZS_SIGN_NONE           0 /* unsigned picture (no S)                 */
#define ZS_SIGN_TRAILING       1 /* overpunched on the last digit (default) */
#define ZS_SIGN_LEADING        2 /* overpunched on the first digit          */
#define ZS_SIGN_TRAILING_SEP   3 /* a separate '+'/'-' byte after the digits */
#define ZS_SIGN_LEADING_SEP    4 /* a separate '+'/'-' byte before them     */
/* COMP and COMP-3 fields that are signed report ZS_SIGN_TRAILING. */

typedef struct {
    char name[ZS_MAX_NAME + 1]; /* upper case; "FILLER" for a filler        */
    uint32_t parent;            /* index of the enclosing group; field 0: 0 */
    uint32_t depth;             /* 0 for the record, 1 for its items, ...   */
    int32_t kind;               /* ZS_K_*                                   */
    int32_t usage;              /* ZS_U_* (groups: ZS_U_DISPLAY)            */
    int32_t sign;               /* ZS_SIGN_*                                */
    uint32_t digits;            /* NUM: total digits; TEXT: characters      */
    uint32_t scale;             /* NUM: implied decimals (after V)          */
    uint32_t occurs;            /* OCCURS n; 0 when there is no OCCURS      */
    uint32_t dims;              /* subscripts needed (OCCURS here + above)  */
    uint64_t offset;            /* byte offset in the record, subscripts 1  */
    uint64_t size;              /* bytes of one occurrence                  */
} zs_field;

int32_t zs_field_count(const zs_layout *layout, uint32_t *out_count);
int32_t zs_field_at(const zs_layout *layout, uint32_t index, zs_field *out_field);

/* Resolve a reference to one item: "PAY-AMOUNT", "LINE-QTY(2)" or
 * "CELL(1, 3)", case-insensitive. Subscripts are 1-based, outermost first,
 * and exactly as many as the field's dims. *out_offset is the item's byte
 * offset in the record. Either out pointer may be NULL. ZXV_E_NOT_FOUND
 * for an unknown name, ZXV_E_RANGE for a subscript out of range,
 * ZXV_E_FORMAT for a malformed reference. */
int32_t zs_find(const zs_layout *layout, const char *ref, uint32_t *out_index,
                uint64_t *out_offset);

/* ===== Values =====
 * Every call takes the record bytes and their length; `rec_len` must be
 * at least the layout's size (ZXV_E_ARG otherwise). Out parameters are
 * written only on ZXV_OK. */

/* A numeric item as an exact decimal: value = *out_unscaled / 10^*out_scale,
 * where *out_scale is the field's implied decimals (S9(5)V99 holding
 * -123.45 gives -12345 and 2). */
int32_t zs_get_decimal(const zs_layout *layout, const uint8_t *rec, size_t rec_len,
                       const char *ref, int64_t *out_unscaled, uint32_t *out_scale);
/* A numeric item as a reduced libzxv rational (-123.45 -> -2469/20). */
int32_t zs_get_rat(const zs_layout *layout, const uint8_t *rec, size_t rec_len, const char *ref,
                   int64_t *out_num, int64_t *out_den);
/* Any item as text (R4): a number as exact decimal text with all of the
 * field's decimals ("-123.45", "0.50", "42"); TEXT and GROUP items as
 * their raw bytes. */
int32_t zs_get_value(const zs_layout *layout, const uint8_t *rec, size_t rec_len, const char *ref,
                     char *buf, size_t cap, size_t *out_len);

/* Store unscaled / 10^scale into a numeric item. More decimals than the
 * field holds are accepted only when the extra digits are zero; anything
 * else that does not fit is ZS_E_FIT. Nothing is written on failure. */
int32_t zs_put_decimal(const zs_layout *layout, uint8_t *rec, size_t rec_len, const char *ref,
                       int64_t unscaled, uint32_t scale);
/* Store an exact rational (1/4 into V99 is 0.25; 1/3 is ZS_E_FIT). */
int32_t zs_put_rat(const zs_layout *layout, uint8_t *rec, size_t rec_len, const char *ref,
                   int64_t num, int64_t den);
/* Store text: a TEXT or GROUP item is padded with spaces (longer text is
 * ZS_E_FIT, never truncated); a numeric item takes exact decimal text
 * ("-123.45", "3/4") through zxv_rat_parse. */
int32_t zs_put_value(const zs_layout *layout, uint8_t *rec, size_t rec_len, const char *ref,
                     const char *text, size_t text_len);

/* Fill a whole record with initial values: spaces in TEXT items and
 * fillers, zero in numeric items (in each field's own encoding). */
int32_t zs_record_init(const zs_layout *layout, uint8_t *rec, size_t rec_len);

/* Check every elementary item of a record (all occurrences). ZXV_OK, or
 * the first ZS_E_DATA with the field named in zs_last_error. */
int32_t zs_record_check(const zs_layout *layout, const uint8_t *rec, size_t rec_len);

/* ===== Postings =====
 * Which fields of a record make one ledger posting. The record moves
 * `amount` (in currency units, e.g. 1234.56 in a S9(13)V99 field) from
 * account `from` to account `to`. */
typedef struct {
    const char *amount;         /* numeric item: the amount in currency units     */
    const char *from;           /* integer item: the account debited (ledger id)  */
    const char *to;             /* integer item: the account credited             */
    const char *currency;       /* X(3) item with the currency code, or NULL      */
    const char *fixed_currency; /* the currency when `currency` is NULL           */
    const char *reference;      /* TEXT item: unique reference (<= 35 bytes after */
                                /* trailing spaces are trimmed)                    */
    int32_t minor_units;        /* -1: from ISO 4217; 0..18: this many            */
} zs_posting_map;

/* A posting read from a record, before or after it is posted. */
typedef struct {
    int64_t amount_minor; /* exact minor units (1234.56 NGN -> 123456)   */
    uint32_t from, to;
    char currency[8];
    char reference[36];
    uint64_t entry_id;    /* set by zs_post on ZXV_OK, else 0            */
} zs_posting;

/* Read a posting from a record without touching any ledger. ZS_E_FIT if
 * the amount has more decimals than the currency's minor units, or an
 * account number is not a whole number in 0..4294967295; ZS_E_DATA for
 * bad bytes; ZXV_E_CURRENCY for a code ISO 4217 does not know (when
 * minor_units is -1). The amount's sign and size are left to the ledger. */
int32_t zs_posting_read(const zs_layout *layout, const zs_posting_map *map, const uint8_t *rec,
                        size_t rec_len, zs_posting *out);

/* Read the posting and post it with zxv_ledger_post_cap(cap, ...), which
 * checks the cap (LEDGER_POST) on every call. Any refusal is returned as
 * is: ZXV_E_CAP, ZXV_E_FUNDS, ZXV_E_RANGE (amount <= 0 or too large),
 * ZXV_E_DUPLICATE (reference used), ZXV_E_CURRENCY, ZXV_E_POISONED, ...
 * `out` may be NULL; when given it is filled as far as the record could
 * be read, and out->entry_id is the ledger's entry id on ZXV_OK. */
int32_t zs_post(const zxv_cap *cap, const zs_layout *layout, const zs_posting_map *map,
                const uint8_t *rec, size_t rec_len, zs_posting *out);

#ifdef __cplusplus
}
#endif

#endif /* ZSUTRA_H */
