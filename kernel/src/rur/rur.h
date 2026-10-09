/* rur.h — Root Universal Representation Type System
 *
 * The typed compatibility and semantic-metadata layer connecting source-language
 * frontends to ZXV Event IR. RUR is not a second runtime authority and is not
 * directly privileged by the kernel.
 *
 * IR hierarchy:
 *   source_language_AST → RUR_semantic_bridge → ZXV_Event_IR →
 *   typed_computation_IR → architecture_backend → target
 *
 * Rule: never flatten source-language metadata before required numeric, layout,
 * exception, ownership, aliasing, array, and calling semantics have been verified.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef RUR_H
#define RUR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "event_space.h"

/* ===== RUR Constants ===== */

#define RUR_MAX_NAME_LEN        64
#define RUR_MAX_DIMENSIONS      8     /* max array rank */
#define RUR_MAX_FIELDS          64    /* max record fields */
#define RUR_MAX_METADATA        16    /* max metadata entries */
#define RUR_MAX_LANG_LEN        32
#define RUR_MAX_DIALECT_LEN     32

/* ===== Semantic Beams ===== */

typedef enum {
    RUR_BEAM_RECORD_TRANSACTION  = 0,  /* COBOL, Sutra */
    RUR_BEAM_NUMERICAL_SCIENTIFIC = 1,  /* FORTRAN */
    RUR_BEAM_SYMBOLIC_FUNCTIONAL  = 2,  /* Lisp */
    RUR_BEAM_CONCATENATIVE        = 3,  /* Forth */
    RUR_BEAM_ARRAY_RANK           = 4,  /* APL, J */
    RUR_BEAM_EVENT_CONTRACT       = 5,  /* Sutra, ZXV native */
    RUR_BEAM_COUNT
} rur_beam_t;

/* ===== RUR Scalar Types ===== */

typedef enum {
    RUR_SCALAR_NONE      = 0,
    RUR_SCALAR_SINT      = 1,  /* signed integer with width */
    RUR_SCALAR_UINT      = 2,  /* unsigned integer with width */
    RUR_SCALAR_DECIMAL   = 3,  /* exact scaled decimal */
    RUR_SCALAR_RATIONAL  = 4,  /* exact rational */
    RUR_SCALAR_FLOAT     = 5,  /* IEEE binary floating */
    RUR_SCALAR_DEC_FLOAT = 6,  /* decimal floating */
    RUR_SCALAR_COMPLEX   = 7,  /* complex over declared component type */
    RUR_SCALAR_LOGICAL   = 8,  /* paraconsistent logical state */
    RUR_SCALAR_CHAR      = 9,  /* character codepoint */
    RUR_SCALAR_STRING    = 10, /* fixed or variable string */
    RUR_SCALAR_CAPABILITY = 11, /* opaque capability handle */
} rur_scalar_type_t;

/* ===== RUR Aggregate Types ===== */

typedef enum {
    RUR_AGG_NONE      = 0,
    RUR_AGG_RECORD    = 1,  /* record with layout and encoding */
    RUR_AGG_VARIANT   = 2,  /* variant/union and redefinition view */
    RUR_AGG_ARRAY     = 3,  /* array with rank, shape, axes, origin, stride, order */
    RUR_AGG_LIST      = 4,  /* list/tree/symbol/closure */
    RUR_AGG_STACK     = 5,  /* stack and execution token */
    RUR_AGG_STREAM    = 6,  /* stream/file/table/database/device contract */
} rur_aggregate_type_t;

/* ===== RUR Paraconsistent Logical States ===== */

typedef enum {
    RUR_LOGIC_TRUE       = 0,
    RUR_LOGIC_FALSE      = 1,
    RUR_LOGIC_BOTH       = 2,  /* contradiction — both true and false */
    RUR_LOGIC_NEITHER    = 3,  /* unresolved — neither true nor false */
} rur_logic_state_t;

/* ===== RUR Numeric Metadata ===== */

typedef enum {
    RUR_ROUND_NEAREST    = 0,
    RUR_ROUND_DOWN       = 1,
    RUR_ROUND_UP         = 2,
    RUR_ROUND_TRUNCATE   = 3,
} rur_rounding_t;

typedef enum {
    RUR_OVERFLOW_WRAP    = 0,
    RUR_OVERFLOW_SATURATE = 1,
    RUR_OVERFLOW_ERROR   = 2,
    RUR_OVERFLOW_UNDEF   = 3,
} rur_overflow_t;

/* ===== RUR Value Descriptor ===== */

typedef struct rur_value {
    rur_scalar_type_t scalar_type;
    rur_aggregate_type_t agg_type;

    /* Width for integers (8, 16, 32, 64) */
    uint8_t int_width;

    /* Decimal/rational precision and scale */
    uint16_t precision;
    uint16_t scale;
    rur_rounding_t rounding;
    rur_overflow_t overflow_policy;

    /* Float format (16, 32, 64, 128) */
    uint8_t float_format;

    /* Complex component type (for RUR_SCALAR_COMPLEX) */
    rur_scalar_type_t complex_component;

    /* Paraconsistent logical state (for RUR_SCALAR_LOGICAL) */
    rur_logic_state_t logic_state;

    /* Array metadata (for RUR_AGG_ARRAY) */
    uint8_t rank;                           /* number of dimensions */
    uint32_t shape[RUR_MAX_DIMENSIONS];     /* size per dimension */
    int32_t origin[RUR_MAX_DIMENSIONS];     /* indexing origin (0 or 1) */
    uint32_t stride[RUR_MAX_DIMENSIONS];    /* stride per dimension */
    rur_scalar_type_t element_type;         /* array element scalar type */

    /* Ownership and mutability */
    bool is_owned;
    bool is_mutable;
    bool is_alias;

    /* Effect and capability metadata */
    bool has_effect;
    bool has_capability;
    char capability_name[RUR_MAX_NAME_LEN];

    /* Determinism */
    bool is_deterministic;

    /* Source provenance */
    char source_language[RUR_MAX_LANG_LEN];
    char source_dialect[RUR_MAX_DIALECT_LEN];
    uint32_t source_line;
    uint32_t source_column;
} rur_value_t;

/* ===== RUR Instruction Families ===== */

typedef enum {
    RUR_OP_TYPED_CONST        = 0,
    RUR_OP_CONVERSION         = 1,
    RUR_OP_RECORD_FIELD       = 2,
    RUR_OP_ARRAY_INDEX        = 3,
    RUR_OP_LIST_CONS          = 4,
    RUR_OP_STACK_PUSH         = 5,
    RUR_OP_STACK_POP          = 6,
    RUR_OP_INT_ARITH          = 7,
    RUR_OP_DECIMAL_ARITH      = 8,
    RUR_OP_RATIONAL_ARITH     = 9,
    RUR_OP_FLOAT_ARITH        = 10,
    RUR_OP_COMPLEX_ARITH      = 11,
    RUR_OP_LOGICAL_OP         = 12,
    RUR_OP_CONTROL_FLOW       = 13,
    RUR_OP_CALL               = 14,
    RUR_OP_RETURN             = 15,
    RUR_OP_CONDITION          = 16,
    RUR_OP_EXCEPTION          = 17,
    RUR_OP_ALLOC              = 18,
    RUR_OP_GC_SAFEPOINT       = 19,
    RUR_OP_FILE_IO            = 20,
    RUR_OP_DB_IO              = 21,
    RUR_OP_TERMINAL_IO        = 22,
    RUR_OP_NETWORK_IO         = 23,
    RUR_OP_DEVICE_IO          = 24,
    RUR_OP_EVENT_EMIT         = 25,
    RUR_OP_EVENT_RECEIVE      = 26,
    RUR_OP_EVENT_WAIT         = 27,
    RUR_OP_EVENT_CANCEL       = 28,
    RUR_OP_EVENT_COMPENSATE   = 29,
    RUR_OP_EVENT_RESOLVE      = 30,
    RUR_OP_CAP_ACQUIRE        = 31,
    RUR_OP_CAP_USE            = 32,
    RUR_OP_CAP_NARROW         = 33,
    RUR_OP_CAP_RELEASE        = 34,
    RUR_OP_VECTOR_MAP         = 35,
    RUR_OP_VECTOR_REDUCE      = 36,
    RUR_OP_VECTOR_SCAN        = 37,
    RUR_OP_VECTOR_INNER       = 38,
    RUR_OP_VECTOR_OUTER       = 39,
    RUR_OP_SYM_QUOTE          = 40,
    RUR_OP_SYM_EXPAND         = 41,
    RUR_OP_SYM_MATCH          = 42,
    RUR_OP_SYM_REWRITE        = 43,
    RUR_OP_SYM_EVAL           = 44,
    RUR_OP_DEBUG              = 45,
    RUR_OP_PROFILE            = 46,
    RUR_OP_AUDIT              = 47,
    RUR_OP_PROVENANCE         = 48,
} rur_op_family_t;

/* ===== RUR Module ===== */

typedef struct rur_module {
    char name[RUR_MAX_NAME_LEN];
    char source_language[RUR_MAX_LANG_LEN];
    char source_dialect[RUR_MAX_DIALECT_LEN];
    uint16_t rur_version;
    uint16_t event_ir_version;

    /* Which semantic beams this module uses */
    bool beams[RUR_BEAM_COUNT];

    /* Tri-space binding */
    char triad_id[RUR_MAX_NAME_LEN];
    uint8_t s_plus_digest[32];
    uint8_t s_minus_digest[32];
    uint8_t s_zero_digest[32];
    bool has_s_zero;

    /* Module flags */
    bool is_generated;          /* true if S-/S0 was generated, not human-authored */
    bool is_reviewed;           /* true if generated material has been reviewed */
    bool is_kernel_privileged;  /* true if module has kernel privileges (never from frontend) */

    bool active;
} rur_module_t;

/* ===== RUR Registry ===== */

#define RUR_MAX_MODULES 64

typedef struct rur_registry {
    rur_module_t modules[RUR_MAX_MODULES];
    uint32_t count;
} rur_registry_t;

/* ===== API ===== */

void rur_registry_init(rur_registry_t *reg);

int32_t rur_register_module(rur_registry_t *reg,
                            const char *name,
                            const char *language,
                            const char *dialect);

rur_module_t *rur_get_module(rur_registry_t *reg, uint32_t idx);

bool rur_set_beam(rur_module_t *mod, rur_beam_t beam, bool enabled);
bool rur_set_triad_digests(rur_module_t *mod,
                           const uint8_t *s_plus, const uint8_t *s_minus,
                           const uint8_t *s_zero, bool has_s_zero);

/* Initialize a RUR value descriptor */
void rur_value_init(rur_value_t *val);

/* Initialize a scalar value with type and width */
void rur_value_init_scalar(rur_value_t *val, rur_scalar_type_t type, uint8_t width);

/* Initialize an array value with rank and shape */
void rur_value_init_array(rur_value_t *val, rur_scalar_type_t elem_type,
                          uint8_t rank, const uint32_t *shape);

/* Initialize a decimal value with precision and scale */
void rur_value_init_decimal(rur_value_t *val, uint16_t precision, uint16_t scale,
                            rur_rounding_t rounding);

/* Check if a conversion between two RUR values is safe */
bool rur_conversion_is_safe(const rur_value_t *from, const rur_value_t *to);

/* Get beam name */
const char *rur_beam_name(rur_beam_t beam);

/* Get scalar type name */
const char *rur_scalar_type_name(rur_scalar_type_t type);

/* Get aggregate type name */
const char *rur_aggregate_type_name(rur_aggregate_type_t type);

/* Get logic state name */
const char *rur_logic_state_name(rur_logic_state_t state);

/* Get op family name */
const char *rur_op_family_name(rur_op_family_t op);

#endif /* RUR_H */
