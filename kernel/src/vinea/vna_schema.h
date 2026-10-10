/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_schema.h — every Vinea wire record is DECLARED once as a schema and
 * packed / validated by ONE generic walker.
 *
 * In the spirit of Sutra's typed DATA SECTION (src/sutra/sutra.h): a record
 * is a static table of typed fields (name, type, fixed or maximum length,
 * integer upper bound, which triple-ledger axis it belongs to). There are no
 * hand-written per-message parsers anywhere in src/vinea; messages,
 * agreements, provider records, trade receipts, ledger entries, handshake
 * messages and file chunks all go through vna_schema_pack/unpack, so every
 * byte that comes off the network is bounds-checked by the same audited code.
 *
 * WIRE RULES (fixed, versioned, little-endian, length-checked)
 *   S1  U8/U16/U32/U64 are little-endian; a field with maxv != 0 must be
 *       <= maxv, else the record is rejected.
 *   S2  CONST32 must equal its declared value (record magic).
 *   S3  FIXED is exactly `max` raw bytes.
 *   S4  VAR is a u16 length then that many bytes; length <= max.
 *   S5  ARR is a u16 count (<= max) then `count` elements, each packed by the
 *       element sub-schema (which may not itself contain ARR).
 *   S6  HK is a VAR holding one line of Hackronomicon shorthand. It must parse
 *       with swarm_hk_parse (K1, K3 no stacking) and be byte-identical to
 *       swarm_hk_canonical of its own parse (K2 one reading). A sender that
 *       sends a non-canonical line is refused, so both sides sign and read the
 *       same bytes.
 *   S7  SIG fields (FIXED, flagged sig) come last. The signed region of a
 *       record is every byte before its first SIG field.
 *   S8  Unpack must consume the input exactly: trailing bytes are an error.
 *   Any failure leaves the output record in an unspecified but bounded state
 *   and returns -1: callers treat that as reject (fail closed).
 */
#ifndef VNA_SCHEMA_H
#define VNA_SCHEMA_H

#include "vna_common.h"

typedef enum {
    VNA_F_U8 = 0,
    VNA_F_U16,
    VNA_F_U32,
    VNA_F_U64,
    VNA_F_CONST32,
    VNA_F_FIXED,
    VNA_F_VAR,
    VNA_F_ARR,
    VNA_F_HK
} vna_ftype_t;

/* Which axis of the triple ledger (finance/triple_ledger.h) a field feeds. */
typedef enum {
    VNA_AXIS_NONE = 0,
    VNA_AXIS_FINANCIAL = 1,  /* rational axis: amounts, debit/credit */
    VNA_AXIS_PROVENANCE = 2, /* logical axis: who attests, hashes, signatures */
    VNA_AXIS_EXTERNALITY = 3 /* imaginary axis: demand pressure, relationships */
} vna_axis_t;

typedef struct vna_schema vna_schema_t;

typedef struct {
    const char *name;
    uint8_t type;            /* vna_ftype_t */
    uint8_t axis;            /* vna_axis_t */
    uint8_t sig;             /* 1: signature field (S7) */
    uint32_t off;            /* offsetof the value in the C record */
    uint32_t len_off;        /* VAR/HK/ARR: offsetof the uint16_t length/count */
    uint32_t max;            /* FIXED: size; VAR/HK: max bytes; ARR: max elements */
    uint64_t maxv;           /* integer fields: inclusive upper bound (0 = any); CONST32: value */
    const vna_schema_t *sub; /* ARR element schema */
    uint32_t stride;         /* ARR: sizeof one element struct */
} vna_field_t;

struct vna_schema {
    const char *name;
    uint16_t id;       /* stable record type id (UBH target_type) */
    uint8_t ubh_class; /* UBH-168 frame class (ubh_frame_class_t) */
    const vna_field_t *f;
    uint16_t n;
};

/* Field declaration helpers. T is the record struct type. */
#define VNA_FU8(T, m, mx, ax)  {#m, VNA_F_U8, ax, 0, offsetof(T, m), 0, 0, mx, 0, 0}
#define VNA_FU16(T, m, mx, ax) {#m, VNA_F_U16, ax, 0, offsetof(T, m), 0, 0, mx, 0, 0}
#define VNA_FU32(T, m, mx, ax) {#m, VNA_F_U32, ax, 0, offsetof(T, m), 0, 0, mx, 0, 0}
#define VNA_FU64(T, m, mx, ax) {#m, VNA_F_U64, ax, 0, offsetof(T, m), 0, 0, mx, 0, 0}
#define VNA_FCONST(T, m, v)    {#m, VNA_F_CONST32, 0, 0, offsetof(T, m), 0, 0, v, 0, 0}
#define VNA_FFIX(T, m, ax)                                                                         \
    {#m, VNA_F_FIXED, ax, 0, offsetof(T, m), 0, sizeof(((T *) 0)->m), 0, 0, 0}
#define VNA_FSIG(T, m)                                                                             \
    {#m, VNA_F_FIXED, VNA_AXIS_PROVENANCE, 1, offsetof(T, m), 0, sizeof(((T *) 0)->m), 0, 0, 0}
#define VNA_FVAR(T, m, l, ax)                                                                      \
    {#m, VNA_F_VAR, ax, 0, offsetof(T, m), offsetof(T, l), sizeof(((T *) 0)->m), 0, 0, 0}
#define VNA_FHK(T, m, l, ax)                                                                       \
    {#m, VNA_F_HK, ax, 0, offsetof(T, m), offsetof(T, l), sizeof(((T *) 0)->m), 0, 0, 0}
#define VNA_FARR(T, m, l, sub_, ET, ax)                                                            \
    {                                                                                              \
        #m,                                                                                        \
        VNA_F_ARR,                                                                                 \
        ax,                                                                                        \
        0,                                                                                         \
        offsetof(T, m),                                                                            \
        offsetof(T, l),                                                                            \
        sizeof(((T *) 0)->m) / sizeof(ET),                                                         \
        0,                                                                                         \
        sub_,                                                                                      \
        sizeof(ET)}

/* Pack `rec` into out[0..cap). with_sig = false stops before the first SIG
 * field (that prefix is exactly what gets signed). Returns bytes written, or
 * -1 if a length/bound is violated or cap is too small. */
int32_t vna_schema_pack(const vna_schema_t *s, const void *rec, uint8_t *out, uint32_t cap,
                        bool with_sig);

/* Unpack in[0..len) into `rec` under rules S1-S8. On success returns len and,
 * if sig_off != NULL, writes the offset of the first SIG field (= length of
 * the signed region; = len if the schema has no SIG). Returns -1 otherwise. */
int32_t vna_schema_unpack(const vna_schema_t *s, const uint8_t *in, uint32_t len, void *rec,
                          uint32_t *sig_off);

/* Largest encoding a record of this schema can have. */
uint32_t vna_schema_max_len(const vna_schema_t *s);

/* Hackronomicon helper (rule S6): true iff text[0..len) parses and is in
 * canonical form. */
bool vna_hk_is_canonical(const uint8_t *text, uint32_t len);

/* Canonicalise one line of Hackronomicon shorthand into out (no NUL is
 * counted in the result). Returns its length, or -1 if it does not parse,
 * stacks more than 5 operators, or does not fit. */
int32_t vna_hk_canonicalize(const char *text, uint8_t *out, uint32_t cap);

#endif /* VNA_SCHEMA_H */
