/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* orbital_compat.h — ZXV Orbital Compatibility Engine
 *
 * WHAT THIS IS
 * ------------
 * One normalized intermediate representation (IR) that legacy codes
 * (COBOL, Fortran), modern languages (C), and future / Sutra-AI languages
 * all LOWER into and LIFT out of. Translate N languages by describing each
 * once against the IR — not by writing N*N pairwise bridges.
 *
 * The IR is a canonical typed record: each field carries a NORMALIZED TYPE
 * plus an EXACT value. The numeric normal form is `rat_t` (WyvernEye exact
 * rational), so a value is language-, width- and endian-agnostic at the
 * logical level: 123.45 lowered from a COBOL COMP-3 field and from a
 * Fortran fixed-format field is the SAME rational, bit-for-bit.
 *
 * WHY IT CAN CARRY MORE LOGIC THAN A CLOCK-BOUND STACK
 * ---------------------------------------------------
 * The IR is a phase-tick dataflow record, not a clock-cycle instruction
 * stream: a field's identity is its normalized value, ordered by
 * registration/phase-tick, never by a wall-clock instant. Same inputs =>
 * same IR, always.
 *
 * WHAT IS REAL vs WHAT IS AN OPS BOUNDARY
 * ---------------------------------------
 * The REAL, on-device core is: the registry + the canonical IR + the
 * COBOL/Fortran/C numeric lowerings, which reuse Lightning Rod's MEASURED
 * representation adapters (lr_packed_to_rat, lr_scaled_to_rat, ...). Those
 * are asserted against Lightning Rod's own known answers.
 *
 * Every language front/back-end BEYOND what Lightning Rod already
 * implements is an OPS BOUNDARY: bind a real adapter via oc_register_lang.
 * An unbound language fails CLOSED with OC_ERR_NO_LANG — it never fabricates
 * a conversion. A full compiler for any language is out of scope; this is
 * the elevator car, not the language it carries.
 *
 * FLAVOR: the orbital elevator lifts COBOL and Fortran to the same floor
 * the future ships from.
 *
 * Freestanding: integer/rational only, no libc, no allocation, fixed arrays.
 */
#ifndef ZXV_ORBITAL_COMPAT_H
#define ZXV_ORBITAL_COMPAT_H

#include <stdint.h>
#include <stdbool.h>
#include "../rational/rational.h"

/* ===== language registry ===== */
typedef enum {
    OC_LANG_COBOL   = 0,
    OC_LANG_FORTRAN = 1,
    OC_LANG_C       = 2,
    OC_LANG_SUTRA   = 3,   /* the native AI language: exact rationals */
    OC_LANG_FUTURE  = 4,   /* reserved: unbound until an adapter arrives */
    OC_LANG_MAX
} oc_lang_t;

/* ===== status / error codes (functions return int32_t) ===== */
typedef enum {
    OC_OK          =  0,
    OC_ERR_NO_LANG = -1,   /* language not registered -> fail closed */
    OC_ERR_ARG     = -2,   /* null / out-of-range argument */
    OC_ERR_CONV    = -3,   /* the representation adapter refused (not exact) */
    OC_ERR_CAP     = -4    /* output buffer too small */
} oc_status_t;

/* ===== the canonical IR ===== */
#define OC_MAX_FIELDS 8
#define OC_TEXT_MAX   64

typedef enum {
    OC_TYPE_NONE = 0,
    OC_TYPE_RATIONAL,      /* exact numeric normal form (rat_t)          */
    OC_TYPE_TEXT           /* canonical ASCII/NUL text (front-ends bind)  */
} oc_type_t;

typedef struct {
    oc_type_t type;
    rat_t     num;                  /* valid when type == OC_TYPE_RATIONAL */
    uint8_t   text[OC_TEXT_MAX];    /* valid when type == OC_TYPE_TEXT     */
    uint32_t  text_len;
    /* `scale` is a NON-canonical lift hint (implied decimal places): the
     * rational is already exact and scale-independent. Two fields with the
     * same value but different scale hints are still EQUAL (oc_field_eq). */
    uint32_t  scale;
} oc_field_t;

typedef struct {
    uint32_t   num_fields;
    oc_field_t fields[OC_MAX_FIELDS];
} oc_ir_t;

/* ===== ops boundary: a language front/back-end ===== */
/* lower: a source representation -> IR.   lift: IR -> a target representation.
 * Both return OC_OK (0) or a negative oc_status_t. A back/front-end that is
 * not bound is simply absent from the registry (fails closed). */
typedef int32_t (*oc_lower_fn)(const void *src, uint32_t len, oc_ir_t *out);
typedef int32_t (*oc_lift_fn)(const oc_ir_t *ir, void *out, uint32_t cap);

typedef struct {
    const char *name;
    oc_lower_fn lower;
    oc_lift_fn  lift;
} oc_lang_ops_t;

/* ===== source/target descriptors for the built-in adapters ===== */
/* These are the on-the-wire shapes the reused Lightning Rod adapters read
 * and write. Callers hand oc_lower a pointer to one of these as `src`. */

/* COBOL: a COMP-3 packed-decimal field + its implied scale (PIC ...Vnn). */
typedef struct {
    const uint8_t *bytes;
    uint32_t       len;
    uint32_t       scale;
} oc_cobol_src_t;

/* Fortran: a fixed-format numeric read as a scaled integer (value*10^-scale). */
typedef struct {
    int64_t  value;
    uint32_t scale;
} oc_fortran_src_t;

/* C: a plain two's-complement integer. */
typedef struct {
    int64_t value;
} oc_c_src_t;

/* Sutra: native exact rational num/den (the AI language speaks ratios). */
typedef struct {
    int64_t num;
    int64_t den;
} oc_sutra_src_t;

/* ===== registry API ===== */

/* Bind a language's real adapter. Returns OC_OK or OC_ERR_ARG. */
int32_t oc_register_lang(oc_lang_t lang, const oc_lang_ops_t *ops);

/* Register the built-in, Lightning-Rod-backed adapters (COBOL, Fortran, C).
 * Sutra and FUTURE are intentionally left UNBOUND so the fail-closed and
 * ops-boundary behaviour is demonstrable. Idempotent. */
void oc_register_builtins(void);

/* Is this language currently bound? */
bool oc_lang_registered(oc_lang_t lang);

/* Human-readable name, for diagnostics/shell. Never NULL. */
const char *oc_lang_name(oc_lang_t lang);

/* ===== translate through the IR ===== */

/* Lower a source representation to the canonical IR.
 * Fails closed (OC_ERR_NO_LANG) if `from` is not registered. */
int32_t oc_lower(oc_lang_t from, const void *src, uint32_t len, oc_ir_t *out);

/* Lift the canonical IR to a target representation.
 * Fails closed (OC_ERR_NO_LANG) if `to` is not registered. */
int32_t oc_lift(oc_lang_t to, const oc_ir_t *ir, void *out, uint32_t cap);

/* ===== IR helpers ===== */

/* Canonical field equality: same type, and for RATIONAL, exactly-equal
 * value (rat_eq). The scale hint is deliberately IGNORED — that is what
 * makes the IR language-agnostic. */
bool oc_field_eq(const oc_field_t *a, const oc_field_t *b);

#endif /* ZXV_ORBITAL_COMPAT_H */
