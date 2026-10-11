/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zmyra.h — MUMPS/M globals and MYRA, the fifth ZXV legacy-language pillar
 * (pillar API 0.1).
 *
 * In M (MUMPS) data lives in globals: sparse, sorted, hierarchical
 * key-value trees such as ^ZXPAY(7,1)="REF-1^12.5^NGN^1^2^67854^N".
 * The subscripts are the key, the value is a string, and by convention
 * its fields are "^"-delimited pieces. What each subscript and piece
 * means is written down outside the data, in a data dictionary or a
 * "global map". libzmyra reads that declaration exactly:
 *
 *   1. SCHEMAS. Two front ends build the same schema:
 *        - zm_gmap_parse: the global map, a documented line format
 *          (README.md, "The global map"):
 *
 *              GLOBAL ^ZXPAY
 *              NODE ^ZXPAY(BATCH,SEQ)
 *                KEY   BATCH  INTEGER(9)
 *                KEY   SEQ    INTEGER(9)
 *                PIECE 1 REF    TEXT(16)     REQUIRED
 *                PIECE 2 AMOUNT NUMBER(15,2) REQUIRED
 *
 *        - zm_myra_parse: MYRA, one line per node:
 *
 *              ^ZXPAY(batch:i9,seq:i9)=ref:t16!^amount:n15.2!
 *
 *      Both give a zm_schema. Its canonical form is MYRA text, and
 *      zm_schema_digest is the SHA3-256 of that text, so a global map and
 *      a MYRA file declare the same schema exactly when the digests match.
 *
 *   2. VALUES. A zm_record holds one node: its key subscripts and its
 *      pieces as typed values. zm_record_decode reads a value string and
 *      refuses anything that is not exactly valid: a number that is not
 *      in M canonical form ("12.50", "0.5", "1E3", "+1"), more digits or
 *      decimals than the field declares, text that is too long or holds
 *      a "^" or a control character, a date outside $H 0..2980013. Values
 *      are an unscaled int64 plus a scale, or a libzxv rational. Never a
 *      float, never rounded. zm_record_encode writes the canonical value
 *      string back.
 *
 *   3. COLLATION. zm_collate orders two subscripts the way M does: the
 *      empty subscript first, then canonical numbers in numeric order,
 *      then every other string in byte order. zm_is_canonic says which
 *      strings M treats as numbers (GT.M's 18-digit precision).
 *
 *   4. POSTINGS. zm_post maps a record to one libzxv ledger posting
 *      through zxv_ledger_post_cap, with a capability the caller passes.
 *      Balancing is the ledger's job; a refused posting comes back as the
 *      ledger's status (ZXV_E_FUNDS, ...).
 *
 * NOT A CLINICAL OR REGULATED-SYSTEM COMPONENT. Many M systems hold health
 * records. This library makes no HIPAA, clinical-safety or certification
 * claim; it checks data shapes, not meaning. Any clinical use is advisory
 * only and needs a qualified human's approval.
 *
 * Honest limits: no FileMan data dictionaries, no $H times (dates only),
 * no set-of-codes or pointer fields, no multi-line word processing nodes,
 * no database access (your M code or engine reads and writes globals;
 * libzmyra interprets the values). README.md lists every refusal.
 *
 * PILLAR API STABILITY. 0.x: it may change before 1.0. It calls only the
 * libzxv ABI 1.3 (zxv_api.h). Opaque handles, int32_t status returns,
 * fixed-width integers, no floats (R11).
 */
#ifndef ZMYRA_H
#define ZMYRA_H

#include <stddef.h>
#include <stdint.h>

#include "zxv_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZM_VERSION_MAJOR 0
#define ZM_VERSION_MINOR 1
#define ZM_VERSION_PATCH 0

/* ===== Status codes =====
 * 0 and -1 .. -99 are libzxv's (passed through unchanged, e.g. the
 * ledger's ZXV_E_FUNDS). mumps-myra uses -2401 .. -2499. */
#define ZM_OK            0
#define ZM_E_SYNTAX      (-2401) /* the global map or MYRA text does not parse      */
#define ZM_E_UNSUPPORTED (-2402) /* valid-looking, outside the supported subset      */
                                 /* (a second global, a type this pillar lacks)      */
#define ZM_E_DATA        (-2403) /* a value string is not valid for the node: a      */
                                 /* non-canonical number, a bad date, a control      */
                                 /* character, a required piece that is empty, a    */
                                 /* non-empty piece the schema does not declare      */
#define ZM_E_FIT         (-2404) /* the value does not fit the field exactly: too    */
                                 /* many digits or decimals, text too long, a        */
                                 /* fraction in an integer field, a date out of range */
#define ZM_E_KEY         (-2405) /* a global reference does not match the schema:    */
                                 /* wrong global, no node with these subscripts, a   */
                                 /* subscript of the wrong type, a non-canonical     */
                                 /* number subscript                                 */

/* Static, never-NULL name ("ZM_E_FIT"); other codes give libzxv's name. */
const char *zm_status_name(int32_t status);
/* The calling thread's last error detail (R4 two-call buffer). */
int32_t zm_last_error(char *buf, size_t cap, size_t *out_len);

/* ===== Limits ===== */
#define ZM_MAX_TEXT     (1024u * 1024u) /* bytes of global map or MYRA text        */
#define ZM_MAX_NODES    256u            /* nodes in one schema                     */
#define ZM_MAX_SUBS     31u             /* subscripts in one node (GT.M's limit)   */
#define ZM_MAX_PIECES   255u            /* highest piece number                    */
#define ZM_MAX_NAME     30u             /* bytes in a field name                   */
#define ZM_MAX_GNAME    31u             /* bytes in a global name, without the ^   */
#define ZM_MAX_LITERAL  63u             /* bytes in a literal subscript            */
#define ZM_MAX_TEXTLEN  32767u          /* TEXT(n): largest n                      */
#define ZM_MAX_DIGITS   18u             /* INTEGER(n), NUMBER(p,s): largest n, p   */
#define ZM_MAX_VALUE    (1024u * 1024u) /* bytes in one value string (GT.M's 1 MiB) */
#define ZM_MAX_REF      8192u           /* bytes in one global reference text      */
#define ZM_MAX_DATE     2980013         /* $H day of 9999-12-31 (day 0 = 1840-12-31) */

/* ===== Schemas ===== */
typedef struct zm_schema zm_schema;

/* Where a parse was refused. line and col are 1-based (col counts bytes),
 * offset is 0-based. msg is one line, NUL-terminated, truncated to fit;
 * it starts "line L, column C: ". */
typedef struct {
    uint32_t line, col;
    uint64_t offset;
    char msg[256];
} zm_diag;

/* Parse a global map (README.md, "The global map"). `text` need not be
 * NUL-terminated; `len` bytes are read (at most ZM_MAX_TEXT, valid UTF-8).
 * Returns ZXV_OK with *out_schema, ZM_E_SYNTAX or ZM_E_UNSUPPORTED (with
 * *diag filled), ZXV_E_ARG, ZXV_E_UTF8, ZXV_E_TOO_LONG or ZXV_E_NOMEM.
 * `diag` may be NULL. */
int32_t zm_gmap_parse(const char *text, size_t len, zm_schema **out_schema, zm_diag *diag);
/* Parse MYRA text (README.md, "MYRA"). Same contract as above. */
int32_t zm_myra_parse(const char *text, size_t len, zm_schema **out_schema, zm_diag *diag);
/* Either form: text whose first word (after blank and ';' comment lines)
 * is GLOBAL is a global map, anything else is MYRA. */
int32_t zm_schema_parse(const char *text, size_t len, zm_schema **out_schema, zm_diag *diag);

void zm_schema_destroy(zm_schema *schema);

/* The canonical MYRA text of a schema (R4): one line per node in
 * declaration order, lower-case field names, no spaces, no comments, no
 * trailing empty pieces. zm_myra_parse of it gives the same schema. */
int32_t zm_schema_myra(const zm_schema *schema, char *buf, size_t cap, size_t *out_len);
/* The same schema as a global map (R4). zm_gmap_parse of it gives the
 * same schema. */
int32_t zm_schema_gmap(const zm_schema *schema, char *buf, size_t cap, size_t *out_len);
/* SHA3-256 of the canonical MYRA text: the schema's identity. */
int32_t zm_schema_digest(const zm_schema *schema, uint8_t out[32]);
/* *out_same = 1 when both schemas declare the same nodes, else 0. */
int32_t zm_schema_same(const zm_schema *a, const zm_schema *b, int32_t *out_same);
/* The global's name without the "^" (R4). Case matters in M: ^ZXPAY and
 * ^zxpay are two globals. */
int32_t zm_schema_global(const zm_schema *schema, char *buf, size_t cap, size_t *out_len);

/* ===== Nodes and fields =====
 * A node is one subscript pattern of the global, for example
 * ^ZXPAY(BATCH,SEQ) or ^ZXPAY("B",REF,BATCH,SEQ). Each subscript is a key
 * field or a literal. A node's fields are its keys (in subscript order)
 * followed by its pieces (in piece order). */
#define ZM_T_TEXT 1 /* TEXT(n)      MYRA tN     printable UTF-8, 0..n bytes, no "^" */
#define ZM_T_INT  2 /* INTEGER(n)   MYRA iN     canonical integer, at most n digits */
#define ZM_T_NUM  3 /* NUMBER(p,s)  MYRA nP.S   canonical number, at most p digits, */
                    /*                          s of them after the point           */
#define ZM_T_DATE 4 /* DATE         MYRA d      $H day count 0..2980013             */

typedef struct {
    char name[ZM_MAX_NAME + 1]; /* lower case                                   */
    int32_t type;               /* ZM_T_*                                       */
    uint32_t len;               /* TEXT: max bytes; INT: max digits; NUM: p     */
    uint32_t scale;             /* NUM: s (1..p); otherwise 0                   */
    int32_t required;           /* a piece that may not be empty; keys: 1       */
    uint32_t sub;               /* key: 1-based subscript position; piece: 0    */
    uint32_t piece;             /* piece: 1-based piece number; key: 0          */
} zm_field;

typedef struct {
    uint32_t subs;   /* subscripts (keys + literals)                           */
    uint32_t keys;   /* key fields                                             */
    uint32_t pieces; /* piece fields                                           */
    uint32_t width;  /* highest piece number declared (0: the value is empty)  */
} zm_node_info;

int32_t zm_node_count(const zm_schema *schema, uint32_t *out_count);
int32_t zm_node_info_at(const zm_schema *schema, uint32_t node, zm_node_info *out_info);
/* The node's subscript pattern as canonical MYRA (R4):
 * ^ZXPAY("B",ref:t16,batch:i9,seq:i9). */
int32_t zm_node_pattern(const zm_schema *schema, uint32_t node, char *buf, size_t cap,
                        size_t *out_len);
/* Field `index` (keys first, then pieces) of a node. */
int32_t zm_field_at(const zm_schema *schema, uint32_t node, uint32_t index, zm_field *out_field);
/* A field by name (case-insensitive). ZXV_E_NOT_FOUND if the node has
 * no such field. */
int32_t zm_field_find(const zm_schema *schema, uint32_t node, const char *name,
                      uint32_t *out_index);

/* ===== Global references =====
 * A reference is written the way M's $QUERY and ZWRITE print it:
 * ^NAME or ^NAME(s1,s2,...), each subscript a canonical number (7, -2.5,
 * .5) or a quoted string ("B", "say ""hi"""). A quoted string that is a
 * canonical number is that number, as in M (^X("10") is ^X(10)). A bare
 * number that is not canonical (01, 1.0, 1E3) is ZM_E_KEY: M would store
 * it under another subscript. The empty subscript "" is ZM_E_KEY (M
 * globals refuse null subscripts by default). */

/* Which node a reference belongs to. ZM_E_KEY (with the reason in
 * zm_last_error) when the global differs or no node matches. */
int32_t zm_node_match(const zm_schema *schema, const char *ref, size_t ref_len,
                      uint32_t *out_node);

/* ===== Collation ===== */
/* 1 if s[0..n) is a number in M canonical form, as GT.M and YottaDB
 * decide it (x=+x): an optional "-", no leading zeros, no trailing zeros
 * after the point, no "+", no exponent, no "-0", at most 18 significant
 * digits, magnitude below 1E47 and not below 1E-43. "0", "7", "-2.5",
 * ".5", "1000000000000000000000" are; "", "01", "0.5", "1.0", "1E3",
 * "-0", " 1" are not. */
int zm_is_canonic(const char *s, size_t n);
/* M collation of two subscripts: "" first, then canonical numbers in
 * numeric order, then all other strings in byte (memcmp) order, a prefix
 * before a longer string. *out_cmp is -1, 0 or 1. Numbers are compared
 * exactly as decimal text, never converted. */
int32_t zm_collate(const char *a, size_t a_len, const char *b, size_t b_len, int32_t *out_cmp);
/* M ($QUERY) order of two global references: global name in byte order,
 * then subscripts left to right by zm_collate, a parent before its
 * descendants. ZM_E_KEY if either reference is malformed. */
int32_t zm_ref_collate(const char *a, size_t a_len, const char *b, size_t b_len,
                       int32_t *out_cmp);

/* ===== Dates =====
 * $H day counts: day 1 is 1841-01-01, day 0 1840-12-31, day 67854
 * 2026-10-11. Proleptic Gregorian, years 1840..9999. */
int32_t zm_date_from_h(int64_t h, int32_t *out_year, int32_t *out_month, int32_t *out_day);
int32_t zm_date_to_h(int32_t year, int32_t month, int32_t day, int64_t *out_h);

/* ===== Records ===== */
typedef struct zm_record zm_record;

/* An empty record for `node`: every key unset, every piece empty. The
 * schema must outlive the record. */
int32_t zm_record_create(const zm_schema *schema, uint32_t node, zm_record **out_record);
void zm_record_destroy(zm_record *record);
/* Unset every key and empty every piece. */
int32_t zm_record_clear(zm_record *record);
/* The node a record belongs to. */
int32_t zm_record_node(const zm_record *record, uint32_t *out_node);

/* Set the key fields from a reference (see "Global references"). The
 * reference must match this record's node, else ZM_E_KEY; nothing is
 * changed on failure. */
int32_t zm_record_set_ref(zm_record *record, const char *ref, size_t ref_len);
/* The record's reference (R4), as M prints it: ^ZXPAY(7,1). ZXV_E_FIELD
 * if a key is unset. */
int32_t zm_record_ref(const zm_record *record, char *buf, size_t cap, size_t *out_len);

/* Read the pieces from a value string (`len` bytes, at most ZM_MAX_VALUE;
 * need not be NUL-terminated). The keys are not touched. ZM_E_DATA or
 * ZM_E_FIT (the field and piece named in zm_last_error) leave the record
 * unchanged. Trailing empty pieces are accepted. */
int32_t zm_record_decode(zm_record *record, const char *value, size_t len);
/* The canonical value string (R4): each piece in M canonical form, empty
 * pieces empty, no trailing "^". ZM_E_DATA if a required piece is empty. */
int32_t zm_record_encode(const zm_record *record, char *buf, size_t cap, size_t *out_len);

/* One call: match the reference to a node, create the record, set its
 * keys and decode the value. */
int32_t zm_node_decode(const zm_schema *schema, const char *ref, size_t ref_len,
                       const char *value, size_t value_len, zm_record **out_record);

/* ===== Field values =====
 * Fields are named (case-insensitive) and may be keys or pieces. An
 * unknown name is ZXV_E_NOT_FOUND; asking a TEXT field for a number is
 * ZXV_E_ARG. Out parameters are written only on ZXV_OK. */

/* 1 if the field is unset (a key) or empty (a piece). */
int32_t zm_is_null(const zm_record *record, const char *name, int32_t *out_null);
/* INTEGER, NUMBER or DATE as an exact decimal: value = unscaled / 10^scale
 * with the field's scale (NUMBER(15,2) holding 12.5 gives 1250 and 2; a
 * DATE gives its $H day and 0). ZXV_E_FIELD when the field is null. */
int32_t zm_get_decimal(const zm_record *record, const char *name, int64_t *out_unscaled,
                       uint32_t *out_scale);
/* The same as a reduced libzxv rational (12.5 -> 25/2). */
int32_t zm_get_rat(const zm_record *record, const char *name, int64_t *out_num,
                   int64_t *out_den);
/* Any field as display text (R4): TEXT as stored; INTEGER as digits;
 * NUMBER with all of the field's decimals ("12.50", "-0.25"); DATE as
 * YYYY-MM-DD. A null field is "". */
int32_t zm_get_value(const zm_record *record, const char *name, char *buf, size_t cap,
                     size_t *out_len);
/* Any field as it is written in M (R4): numbers in canonical form
 * ("12.5", "-.25"), DATE as the $H day count, TEXT as stored. */
int32_t zm_get_mtext(const zm_record *record, const char *name, char *buf, size_t cap,
                     size_t *out_len);

/* Store unscaled / 10^scale. More decimals than the field holds are
 * accepted only when the extra digits are zero; anything else that does
 * not fit is ZM_E_FIT. A DATE takes its $H day count (scale 0). Nothing is
 * written on failure. */
int32_t zm_put_decimal(zm_record *record, const char *name, int64_t unscaled, uint32_t scale);
/* Store an exact rational (1/4 into NUMBER(9,2) is .25; 1/3 is ZM_E_FIT). */
int32_t zm_put_rat(zm_record *record, const char *name, int64_t num, int64_t den);
/* Store display text: TEXT as is (too long, "^" or a control character
 * is refused, never truncated); a number as decimal text ("-0012.50",
 * "+3") or a rational ("3/4"); a DATE as YYYY-MM-DD. Empty text makes a
 * piece null; a key cannot be null (ZXV_E_ARG). */
int32_t zm_put_value(zm_record *record, const char *name, const char *text, size_t text_len);
/* Empty a piece (ZXV_E_ARG for a key). */
int32_t zm_put_null(zm_record *record, const char *name);

/* ===== Postings =====
 * Which fields of a node make one ledger posting. The record moves
 * `amount` (in currency units, e.g. 1234.56 in a NUMBER(15,2) field) from
 * account `from` to account `to`. Any field may be a key or a piece. */
typedef struct {
    const char *amount;         /* INTEGER or NUMBER: amount in currency units    */
    const char *from;           /* INTEGER or NUMBER: the account debited          */
    const char *to;             /* INTEGER or NUMBER: the account credited         */
    const char *currency;       /* TEXT field with the currency code, or NULL      */
    const char *fixed_currency; /* the currency when `currency` is NULL            */
    const char *reference;      /* a field whose M text is the unique reference    */
                                /* (<= 35 bytes), or NULL for the record's own     */
                                /* reference without the "^": ZXPAY(7,1)           */
    int32_t minor_units;        /* -1: from ISO 4217; 0..18: this many             */
} zm_posting_map;

typedef struct {
    int64_t amount_minor; /* exact minor units (12.5 NGN -> 1250)        */
    uint32_t from, to;
    char currency[8];
    char reference[36];
    uint64_t entry_id;    /* set by zm_post on ZXV_OK, else 0            */
} zm_posting;

/* Read a posting from a record without touching any ledger. ZM_E_FIT if
 * the amount has more decimals than the currency's minor units, an
 * account is not a whole number in 0..4294967295, or the reference is
 * longer than 35 bytes; ZXV_E_FIELD if a mapped field is null;
 * ZXV_E_CURRENCY for a code ISO 4217 does not know (when minor_units is
 * -1). The amount's sign and size are left to the ledger. */
int32_t zm_posting_read(const zm_record *record, const zm_posting_map *map, zm_posting *out);

/* Read the posting and post it with zxv_ledger_post_cap(cap, ...), which
 * checks the cap (LEDGER_POST) on every call. Any refusal is returned as
 * is: ZXV_E_CAP, ZXV_E_FUNDS, ZXV_E_RANGE, ZXV_E_DUPLICATE,
 * ZXV_E_CURRENCY, ZXV_E_POISONED, ... `out` may be NULL; when given it is
 * filled as far as the record could be read, and out->entry_id is the
 * ledger's entry id on ZXV_OK. */
int32_t zm_post(const zxv_cap *cap, const zm_record *record, const zm_posting_map *map,
                zm_posting *out);

#ifdef __cplusplus
}
#endif

#endif /* ZMYRA_H */
