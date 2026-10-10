/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cobol.h — COBOL record data types and a copybook-driven record codec, for
 * reading and writing mainframe batch files exactly.
 *
 * Numeric encodings:
 *  - COMP-3 packed decimal: two digits per byte, sign nibble last (C/F
 *    positive, D negative).
 *  - Zoned decimal (DISPLAY numeric): one EBCDIC digit per byte; the last
 *    byte's zone nibble carries the sign (F unsigned, C positive, D negative)
 *    — the classic signed-overpunch convention.
 *  - COMP / COMP-4 / COMP-5 binary: big-endian two's-complement integers
 *    (2/4/8 bytes chosen by digit count).
 *  - PIC X: fixed-width text (space padded). Implied decimal V is carried as a
 *    scale; amounts are exact scaled integers (no floating point).
 *
 * A bounded copybook parser accepts level numbers 01-49, PIC clauses, USAGE
 * (COMP-3/COMP/COMP-4/COMP-5/DISPLAY), fixed OCCURS n TIMES and REDEFINES, and
 * produces a flat field layout. A record codec reads/writes individual fields
 * by name against that layout. Fixed-length and IBM RDW variable-length record
 * readers iterate a file image.
 *
 * All amounts are handled as 64-bit scaled integers. No floating point and no
 * 64-bit division (digit extraction uses lg_udiv64_small).
 *
 * HONEST LIMITS. A pragmatic subset: no COMP-1/COMP-2 (float), no edited
 * pictures (Z, *, CR, DB, insertion), no SIGN SEPARATE/LEADING clauses, no
 * nested OCCURS or OCCURS DEPENDING ON, no 88-level condition names, and the
 * copybook must be reasonably well-formed. It is built to carry real fixed
 * record formats, not to compile arbitrary COBOL.
 */
#ifndef ZXV_LEGACY_COBOL_H
#define ZXV_LEGACY_COBOL_H

#include <stdbool.h>
#include <stdint.h>

/* ===== primitive numeric codecs ===== */
/* COMP-3: decode `nbytes` packed bytes to a signed 64-bit value. */
bool comp3_decode(const uint8_t *in, uint32_t nbytes, int64_t *out);
/* COMP-3: encode a signed value into `nbytes` packed bytes (ndigits implied by
 * nbytes = ndigits/2 + 1). Returns false if the value does not fit. */
bool comp3_encode(int64_t value, uint8_t *out, uint32_t nbytes);

/* Zoned EBCDIC decimal: `ndigits` bytes, signed overpunch in the last. */
bool zoned_decode(const uint8_t *in, uint32_t ndigits, int64_t *out);
bool zoned_encode(int64_t value, uint8_t *out, uint32_t ndigits, bool signed_field);

/* Big-endian binary (COMP/COMP-4/COMP-5): size 2/4/8. */
bool comp_decode(const uint8_t *in, uint32_t size, bool is_signed, int64_t *out);
bool comp_encode(int64_t value, uint8_t *out, uint32_t size, bool is_signed);

/* ===== copybook layout ===== */
typedef enum {
    COB_ALPHA = 0, /* PIC X */
    COB_ZONED,     /* PIC 9 DISPLAY */
    COB_COMP3,     /* packed */
    COB_COMP,      /* binary */
} cob_type;

#define COB_MAX_FIELDS 128
#define COB_MAX_NAME   32

typedef struct {
    char name[COB_MAX_NAME];
    cob_type type;
    uint32_t offset;       /* byte offset in the record */
    uint32_t size;         /* byte width of this field */
    uint32_t ndigits;      /* for numeric types */
    uint32_t scale;        /* implied decimal places (V) */
    bool is_signed;        /* S in the picture */
    uint8_t level;         /* COBOL level number */
    uint16_t occurs_index; /* 0-based copy index when expanded from OCCURS */
} cob_field;

typedef struct {
    cob_field fields[COB_MAX_FIELDS];
    uint32_t nfields;
    uint32_t record_size; /* total bytes */
} cob_layout;

/* Parse a copybook (ASCII text) into a layout. Returns true on success. */
bool cobol_parse_copybook(const char *text, uint32_t len, cob_layout *lo);

/* Find a field by name (first match, exact). Returns NULL if absent. */
const cob_field *cobol_find(const cob_layout *lo, const char *name);
/* Find the OCCURS copy `index` of a base name. */
const cob_field *cobol_find_indexed(const cob_layout *lo, const char *name, uint16_t index);

/* ===== record codec ===== */
/* Decode a numeric field to a scaled int64 (value = amount * 10^scale). */
bool cobol_get_int(const uint8_t *rec, uint32_t rec_len, const cob_field *f, int64_t *out);
/* Encode a scaled int64 into a numeric field. */
bool cobol_set_int(uint8_t *rec, uint32_t rec_len, const cob_field *f, int64_t value);
/* Read an alpha field as text (trailing spaces kept; caller trims). */
bool cobol_get_text(const uint8_t *rec, uint32_t rec_len, const cob_field *f, uint8_t *out,
                    uint32_t cap, uint32_t *n);
/* Write text into an alpha field, space-padded to width. */
bool cobol_set_text(uint8_t *rec, uint32_t rec_len, const cob_field *f, const uint8_t *in,
                    uint32_t n);

/* ===== record readers ===== */
/* Fixed-length: returns the i-th record pointer, or NULL past the end. */
const uint8_t *cobol_fixed_record(const uint8_t *buf, uint32_t buf_len, uint32_t rec_size,
                                  uint32_t index, uint32_t *out_len);

/* IBM RDW variable-length: an iterator over a VB-style image. Each record is
 * preceded by a 4-byte RDW (2-byte big-endian length including the RDW, then
 * two zero bytes). */
typedef struct {
    const uint8_t *buf;
    uint32_t len;
    uint32_t pos;
} cob_rdw_iter;

void cobol_rdw_init(cob_rdw_iter *it, const uint8_t *buf, uint32_t len);
/* Returns pointer to the next record payload (after the RDW), sets *out_len,
 * advances. Returns NULL at end or on a malformed RDW. */
const uint8_t *cobol_rdw_next(cob_rdw_iter *it, uint32_t *out_len);

#endif /* ZXV_LEGACY_COBOL_H */
