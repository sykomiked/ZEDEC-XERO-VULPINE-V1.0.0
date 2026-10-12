/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zeve.h — Ada type declarations and Eve, the third ZXV legacy-language
 * pillar (pillar API 0.1).
 *
 * An Ada package specification of plain data types is a record format
 * ("the treaty") between an Ada program and everything else. libzeve
 * reads a documented subset of it exactly:
 *
 *   1. LAYOUTS. Two front ends build the same package of types:
 *        - ze_ada_parse: a subset of an Ada package specification
 *          (integer range types, decimal fixed point "delta 0.01 digits 15",
 *          enumerations, constrained arrays, String (1 .. n), records and
 *          subtypes; README.md, "Supported Ada");
 *        - ze_eve_parse: Eve, a short syntax for the same thing:
 *
 *              eve payments
 *              money = dec(15, 2)
 *              payment {
 *                ref: str(16)
 *                amount: money
 *              }
 *
 *      Both give a ze_pkg. Its canonical form is Eve text, and
 *      ze_pkg_digest is the SHA3-256 of that text, so an Ada spec and an
 *      Eve file declare the same types exactly when their digests match.
 *
 *   2. VALUES. Every type has one canonical byte encoding (big-endian
 *      integers of the narrowest width that holds the declared range,
 *      enumeration positions, Latin-1 text, arrays row-major, records
 *      packed in declaration order). ze_get_* / ze_put_* convert between
 *      those bytes and exact values: an integer plus its implied scale, or
 *      a libzxv rational. Never through a float. A value outside the
 *      declared range, or with more decimals than the type has, is
 *      refused (ZE_E_FIT); bytes that are not a valid value are refused
 *      (ZE_E_DATA).
 *
 *   3. POSTINGS. ze_post maps a record to one libzxv ledger posting
 *      through zxv_ledger_post_cap, with a capability the caller passes.
 *      Balancing is the ledger's job; its refusals come back unchanged.
 *
 * The canonical encoding is this pillar's wire format. It is NOT the
 * in-memory representation GNAT (or any compiler) chooses for the record;
 * Ada leaves that to the implementation unless representation clauses fix
 * it, and this subset refuses representation clauses.
 *
 * PILLAR API STABILITY. 0.x: it may change before 1.0. It calls only the
 * libzxv ABI 1.3 (zxv_api.h). Opaque handles, int32_t status returns,
 * fixed-width integers, no floats (R11).
 */
#ifndef ZEVE_H
#define ZEVE_H

#include <stddef.h>
#include <stdint.h>

#include "zxv_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZE_VERSION_MAJOR 0
#define ZE_VERSION_MINOR 1
#define ZE_VERSION_PATCH 0

/* ===== Status codes =====
 * 0 and -1 .. -99 are libzxv's (passed through unchanged). The legacy
 * pillars own -2001 and below; ada-eve uses -2201 .. -2299. */
#define ZE_OK            0
#define ZE_E_SYNTAX      (-2201) /* the Ada or Eve text does not parse                 */
#define ZE_E_UNSUPPORTED (-2202) /* valid Ada, outside the supported subset (floats,   */
                                 /* access types, tagged records, rep clauses, ...)    */
#define ZE_E_NAME        (-2203) /* an unknown, duplicate or reserved name, or a use   */
                                 /* before the declaration                             */
#define ZE_E_CONSTRAINT  (-2204) /* a declared range or bound is invalid: empty, out   */
                                 /* of its base type, beyond int64, not a multiple of  */
                                 /* the delta, a delta that is not a power of ten      */
#define ZE_E_DATA        (-2205) /* the record bytes are not a valid value of the type */
#define ZE_E_FIT         (-2206) /* the value does not fit exactly: outside the range, */
                                 /* more decimals than the type, not a literal of the  */
                                 /* enumeration, text longer than the string           */
#define ZE_E_LIMIT       (-2207) /* a size or count limit (types, record bytes, ...)    */

/* Static, never-NULL name ("ZE_E_FIT"); other codes give libzxv's name. */
const char *ze_status_name(int32_t status);
/* ZXV_CLASS_* of a status: the pillar's codes are USAGE (syntax,
 * unsupported, name, constraint) or REFUSED (data, fit, limit); libzxv
 * codes keep libzxv's class. */
int32_t ze_status_class(int32_t status);
/* The calling thread's last error detail (R4 two-call buffer). */
int32_t ze_last_error(char *buf, size_t cap, size_t *out_len);

/* ===== Limits ===== */
#define ZE_MAX_TEXT     (1024u * 1024u) /* bytes of Ada or Eve text                  */
#define ZE_MAX_TYPES    4096u           /* types in one package (named + anonymous)  */
#define ZE_MAX_COMPS    16384u          /* record components in one package          */
#define ZE_MAX_LITERALS 65536u          /* enumeration literals in one package       */
#define ZE_MAX_ENUM     4096u           /* literals in one enumeration               */
#define ZE_MAX_NAME     63u             /* bytes in a name                           */
#define ZE_MAX_DIMS     4u              /* array dimensions                          */
#define ZE_MAX_DIGITS   18u             /* decimal digits (fits int64)               */
#define ZE_MAX_STRING   65535u          /* characters in String (1 .. n)             */
#define ZE_MAX_SIZE     (1024u * 1024u) /* bytes in one encoded value                */

/* ===== Packages ===== */
typedef struct ze_pkg ze_pkg;

/* Where a parse was refused. line and col are 1-based (col counts bytes),
 * offset is 0-based. msg is one line, NUL-terminated, truncated to fit;
 * it starts "line L, column C: ". */
typedef struct {
    uint32_t line, col;
    uint64_t offset;
    char msg[256];
} ze_diag;

/* Parse an Ada package specification. `text` need not be NUL-terminated;
 * `len` bytes are read (at most ZE_MAX_TEXT, else ZE_E_LIMIT; text that is
 * not valid UTF-8 is ZE_E_SYNTAX at the first bad byte). Returns ZXV_OK
 * with *out_pkg, or ZE_E_SYNTAX / UNSUPPORTED / NAME / CONSTRAINT / LIMIT
 * with *diag filled, or ZXV_E_ARG / ZXV_E_NOMEM. `diag` may be NULL. */
int32_t ze_ada_parse(const char *text, size_t len, ze_pkg **out_pkg, ze_diag *diag);
/* Parse Eve text (README.md, "Eve"). Same contract. */
int32_t ze_eve_parse(const char *text, size_t len, ze_pkg **out_pkg, ze_diag *diag);
void ze_pkg_destroy(ze_pkg *pkg);

/* The canonical Eve text (R4). ze_eve_parse of it gives the same package. */
int32_t ze_pkg_eve(const ze_pkg *pkg, char *buf, size_t cap, size_t *out_len);
/* The same package as an Ada specification (R4). ze_ada_parse of it gives
 * the same package, and GNAT compiles it (make golden-gnat). */
int32_t ze_pkg_ada(const ze_pkg *pkg, char *buf, size_t cap, size_t *out_len);
/* SHA3-256 of the canonical Eve text: the package's identity. */
int32_t ze_pkg_digest(const ze_pkg *pkg, uint8_t out[32]);
/* *out_same = 1 when both declare the same types, else 0. */
int32_t ze_pkg_same(const ze_pkg *a, const ze_pkg *b, int32_t *out_same);
/* The package name, lower case (R4). */
int32_t ze_pkg_name(const ze_pkg *pkg, char *buf, size_t cap, size_t *out_len);

/* ===== Types =====
 * Types are numbered in declaration order. Type 0 is the predefined
 * Boolean (an enumeration of false, true). A constrained use inside a
 * declaration (a component "Qty : Count range 0 .. 10", a String (1 .. 8),
 * an array element with a range) is an anonymous type with an empty name,
 * numbered where it appears. */
#define ZE_K_INT    1 /* type T is range L .. H                              */
#define ZE_K_DEC    2 /* type T is delta 10.0**(-s) digits d [range L .. H]  */
#define ZE_K_ENUM   3 /* type T is (A, B, C); Boolean                        */
#define ZE_K_TEXT   4 /* String (1 .. n)                                     */
#define ZE_K_ARRAY  5 /* type T is array (I {, I}) of E                      */
#define ZE_K_RECORD 6 /* type T is record ... end record                     */

typedef struct {
    char name[ZE_MAX_NAME + 1]; /* lower case; "" for an anonymous type        */
    int32_t kind;               /* ZE_K_*                                       */
    int32_t is_subtype;         /* 1: a subtype of `base` (named or anonymous)  */
    uint32_t base;              /* subtype: the type it constrains; else itself */
    int64_t lo, hi;             /* INT: the range; DEC: the range in units of   */
                                /* 10^-scale; ENUM: positions; else 0           */
    uint32_t digits, scale;     /* DEC (a subtype reports its base's)           */
    uint32_t count;             /* ENUM: literals of the base; TEXT: characters;*/
                                /* ARRAY: elements; RECORD: components          */
    uint32_t elem;              /* ARRAY: the element type                      */
    uint32_t dims;              /* ARRAY: dimensions (1..ZE_MAX_DIMS)           */
    uint64_t size;              /* bytes of one value in the canonical encoding */
} ze_type;

/* One array dimension. index is the index type (an INT or ENUM type, or
 * Boolean) or UINT32_MAX for a literal range ("array (1 .. 12)", whose
 * index type in Ada is Integer). lo/hi are its bounds (positions for an
 * enumeration). */
typedef struct {
    uint32_t index;
    int64_t lo, hi;
} ze_dim;

/* One record component, in declaration order. */
typedef struct {
    char name[ZE_MAX_NAME + 1];
    uint32_t type;
    uint64_t offset; /* byte offset inside the record's encoding */
} ze_component;

int32_t ze_type_count(const ze_pkg *pkg, uint32_t *out_count);
int32_t ze_type_at(const ze_pkg *pkg, uint32_t index, ze_type *out_type);
/* A named type, case-insensitive. ZXV_E_NOT_FOUND if there is none. */
int32_t ze_type_find(const ze_pkg *pkg, const char *name, uint32_t *out_index);
int32_t ze_array_dim(const ze_pkg *pkg, uint32_t type, uint32_t dim, ze_dim *out_dim);
int32_t ze_component_at(const ze_pkg *pkg, uint32_t type, uint32_t index, ze_component *out);
/* The literal at a position of an enumeration (or of its subtype's base),
 * lower case (R4). */
int32_t ze_enum_literal(const ze_pkg *pkg, uint32_t type, uint32_t pos, char *buf, size_t cap,
                        size_t *out_len);

/* ===== Values =====
 * A value of type `type` is `size` bytes (ze_type.size). A path selects a
 * part of it: "" is the whole value, "amount" a component, "lines(3)" an
 * array element, "grid(2, usd).qty" both. Names and enumeration indexes
 * are case-insensitive; integer indexes are decimal with an optional
 * sign. ZXV_E_NOT_FOUND for an unknown component, ZXV_E_RANGE for an
 * index out of range, ZXV_E_FORMAT for a malformed path. Every call takes
 * the bytes and their length; `len` must be at least the type's size
 * (ZXV_E_ARG otherwise). Out parameters are written only on ZXV_OK. */

/* Resolve a path: the scalar or composite type it names and its byte
 * offset. Either out pointer may be NULL. */
int32_t ze_find(const ze_pkg *pkg, uint32_t type, const char *path, uint32_t *out_type,
                uint64_t *out_offset);

/* An INT or DEC value as an exact decimal: value = *out_units / 10^*out_scale
 * (an INT has scale 0). Anything else is ZXV_E_ARG. */
int32_t ze_get_decimal(const ze_pkg *pkg, uint32_t type, const uint8_t *val, size_t len,
                       const char *path, int64_t *out_units, uint32_t *out_scale);
/* An INT or DEC value as a reduced libzxv rational (-123.45 -> -2469/20). */
int32_t ze_get_rat(const ze_pkg *pkg, uint32_t type, const uint8_t *val, size_t len,
                   const char *path, int64_t *out_num, int64_t *out_den);
/* An enumeration value's position (0-based, in its base type). */
int32_t ze_get_enum(const ze_pkg *pkg, uint32_t type, const uint8_t *val, size_t len,
                    const char *path, uint32_t *out_pos);
/* Any scalar as text (R4): "42", "-123.45" (all of the type's decimals),
 * an enumeration literal in lower case, or a String's bytes. */
int32_t ze_get_value(const ze_pkg *pkg, uint32_t type, const uint8_t *val, size_t len,
                     const char *path, char *buf, size_t cap, size_t *out_len);

/* Store units / 10^scale into an INT or DEC. More decimals than the type
 * has are accepted only when the extra digits are zero; anything outside
 * the declared range is ZE_E_FIT. Nothing is written on failure. */
int32_t ze_put_decimal(const ze_pkg *pkg, uint32_t type, uint8_t *val, size_t len,
                       const char *path, int64_t units, uint32_t scale);
/* Store an exact rational (1/4 into a delta 0.01 type is 0.25; 1/3 is ZE_E_FIT). */
int32_t ze_put_rat(const ze_pkg *pkg, uint32_t type, uint8_t *val, size_t len, const char *path,
                   int64_t num, int64_t den);
/* Store an enumeration position (ZE_E_FIT outside the subtype's range). */
int32_t ze_put_enum(const ze_pkg *pkg, uint32_t type, uint8_t *val, size_t len, const char *path,
                    uint32_t pos);
/* Store text: a String takes the bytes padded with spaces (longer is
 * ZE_E_FIT, never truncated); a number takes exact decimal text ("-1.50",
 * the whole int64 range) or a fraction ("3/4", through zxv_rat_parse); an
 * enumeration takes a literal. */
int32_t ze_put_value(const ze_pkg *pkg, uint32_t type, uint8_t *val, size_t len, const char *path,
                     const char *text, size_t text_len);

/* Fill a value with the canonical initial value: every number is the value
 * in its range nearest to zero, every enumeration its first value, every
 * String spaces. Ada itself gives such components no default. */
int32_t ze_value_init(const ze_pkg *pkg, uint32_t type, uint8_t *val, size_t len);
/* Check every scalar of a value. ZXV_OK, or the first ZE_E_DATA with the
 * path named in ze_last_error. */
int32_t ze_value_check(const ze_pkg *pkg, uint32_t type, const uint8_t *val, size_t len);

/* ===== Postings =====
 * Which components of a record make one ledger posting: the record moves
 * `amount` (in currency units, e.g. a delta 0.01 component holding
 * 1234.56) from account `from` to account `to`. */
typedef struct {
    const char *amount;         /* INT or DEC path: the amount in currency units     */
    const char *from;           /* INT or DEC path, a whole number: the account      */
    const char *to;             /* debited and the account credited (ledger ids)     */
    const char *currency;       /* a String path, or an enumeration path whose       */
                                /* literal is the code (NGN), or NULL                */
    const char *fixed_currency; /* the currency when `currency` is NULL              */
    const char *reference;      /* a String path: unique reference (<= 35 bytes      */
                                /* after trailing spaces are trimmed)                */
    int32_t minor_units;        /* -1: from ISO 4217; 0..18: this many               */
} ze_posting_map;

typedef struct {
    int64_t amount_minor; /* exact minor units (1234.56 NGN -> 123456) */
    uint32_t from, to;
    char currency[8];
    char reference[36];
    uint64_t entry_id;    /* set by ze_post on ZXV_OK, else 0          */
} ze_posting;

/* Read a posting without touching any ledger. ZE_E_FIT if the amount has
 * more decimals than the currency's minor units or an account is not a
 * whole number in 0..4294967295; ZXV_E_CURRENCY for a code ISO 4217 does
 * not know (when minor_units is -1). */
int32_t ze_posting_read(const ze_pkg *pkg, uint32_t type, const ze_posting_map *map,
                        const uint8_t *val, size_t len, ze_posting *out);
/* Read the posting and post it with zxv_ledger_post_cap(cap, ...), which
 * checks the cap (LEDGER_POST) on every call. Any refusal is returned as
 * is (ZXV_E_CAP, ZXV_E_FUNDS, ZXV_E_DUPLICATE, ZXV_E_POISONED, ...). `out`
 * may be NULL; when given it is filled as far as the record could be
 * read. */
int32_t ze_post(const zxv_cap *cap, const ze_pkg *pkg, uint32_t type, const ze_posting_map *map,
                const uint8_t *val, size_t len, ze_posting *out);

#ifdef __cplusplus
}
#endif

#endif /* ZEVE_H */
