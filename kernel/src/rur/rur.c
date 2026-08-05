/* rur.c — Root Universal Representation Type System
 *
 * Implements the RUR registry, value descriptors, and type safety checks.
 * RUR is the typed compatibility layer between source-language frontends
 * and ZXV Event IR. It is not a runtime authority.
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3 (kernel component)
 */

#include "rur.h"

/* ===== Helpers ===== */

static void copy_str(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i + 1 < max && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

/* ===== Registry ===== */

void rur_registry_init(rur_registry_t *reg) {
    if (!reg) return;
    ev_memset(reg, 0, sizeof(*reg));
}

int32_t rur_register_module(rur_registry_t *reg,
                            const char *name,
                            const char *language,
                            const char *dialect) {
    if (!reg || !name) return -1;
    if (reg->count >= RUR_MAX_MODULES) return -1;

    rur_module_t *mod = &reg->modules[reg->count];
    ev_memset(mod, 0, sizeof(*mod));
    copy_str(mod->name, name, RUR_MAX_NAME_LEN);
    if (language) copy_str(mod->source_language, language, RUR_MAX_LANG_LEN);
    if (dialect) copy_str(mod->source_dialect, dialect, RUR_MAX_DIALECT_LEN);
    mod->rur_version = 1;
    mod->event_ir_version = 1;
    mod->active = true;

    return (int32_t)reg->count++;
}

rur_module_t *rur_get_module(rur_registry_t *reg, uint32_t idx) {
    if (!reg || idx >= reg->count) return NULL;
    return &reg->modules[idx];
}

bool rur_set_beam(rur_module_t *mod, rur_beam_t beam, bool enabled) {
    if (!mod || beam >= RUR_BEAM_COUNT) return false;
    mod->beams[beam] = enabled;
    return true;
}

bool rur_set_triad_digests(rur_module_t *mod,
                           const uint8_t *s_plus, const uint8_t *s_minus,
                           const uint8_t *s_zero, bool has_s_zero) {
    if (!mod || !s_plus || !s_minus) return false;
    ev_memcpy(mod->s_plus_digest, s_plus, 32);
    ev_memcpy(mod->s_minus_digest, s_minus, 32);
    if (has_s_zero && s_zero) {
        ev_memcpy(mod->s_zero_digest, s_zero, 32);
        mod->has_s_zero = true;
    } else {
        ev_memset(mod->s_zero_digest, 0, 32);
        mod->has_s_zero = false;
    }
    return true;
}

/* ===== Value Descriptors ===== */

void rur_value_init(rur_value_t *val) {
    if (!val) return;
    ev_memset(val, 0, sizeof(*val));
    val->scalar_type = RUR_SCALAR_NONE;
    val->agg_type = RUR_AGG_NONE;
    val->is_mutable = true;
    val->is_deterministic = true;
}

void rur_value_init_scalar(rur_value_t *val, rur_scalar_type_t type, uint8_t width) {
    if (!val) return;
    rur_value_init(val);
    val->scalar_type = type;
    val->int_width = width;
}

void rur_value_init_array(rur_value_t *val, rur_scalar_type_t elem_type,
                          uint8_t rank, const uint32_t *shape) {
    if (!val || !shape) return;
    rur_value_init(val);
    val->agg_type = RUR_AGG_ARRAY;
    val->scalar_type = RUR_SCALAR_NONE;
    val->element_type = elem_type;
    val->rank = rank;
    if (rank > RUR_MAX_DIMENSIONS) rank = RUR_MAX_DIMENSIONS;
    for (uint8_t i = 0; i < rank; i++) {
        val->shape[i] = shape[i];
        val->origin[i] = 0;  /* default 0-origin */
        val->stride[i] = 0;  /* computed by backend */
    }
}

void rur_value_init_decimal(rur_value_t *val, uint16_t precision, uint16_t scale,
                            rur_rounding_t rounding) {
    if (!val) return;
    rur_value_init(val);
    val->scalar_type = RUR_SCALAR_DECIMAL;
    val->precision = precision;
    val->scale = scale;
    val->rounding = rounding;
    val->overflow_policy = RUR_OVERFLOW_ERROR;
}

/* ===== Type Safety ===== */

bool rur_conversion_is_safe(const rur_value_t *from, const rur_value_t *to) {
    if (!from || !to) return false;

    /* Same type and width is always safe */
    if (from->scalar_type == to->scalar_type && from->int_width == to->int_width)
        return true;

    /* Integer widening is safe */
    if ((from->scalar_type == RUR_SCALAR_SINT || from->scalar_type == RUR_SCALAR_UINT) &&
        (to->scalar_type == RUR_SCALAR_SINT || to->scalar_type == RUR_SCALAR_UINT)) {
        return to->int_width >= from->int_width;
    }

    /* Integer to decimal: safe if decimal has enough precision */
    if ((from->scalar_type == RUR_SCALAR_SINT || from->scalar_type == RUR_SCALAR_UINT) &&
        to->scalar_type == RUR_SCALAR_DECIMAL) {
        return to->precision >= from->int_width * 3 + 1;  /* conservative */
    }

    /* Decimal to rational: safe (exact) */
    if (from->scalar_type == RUR_SCALAR_DECIMAL &&
        to->scalar_type == RUR_SCALAR_RATIONAL)
        return true;

    /* Rational to float: NOT safe (lossy) */
    if (from->scalar_type == RUR_SCALAR_RATIONAL &&
        to->scalar_type == RUR_SCALAR_FLOAT)
        return false;

    /* Float to integer: NOT safe (lossy) */
    if (from->scalar_type == RUR_SCALAR_FLOAT &&
        (to->scalar_type == RUR_SCALAR_SINT || to->scalar_type == RUR_SCALAR_UINT))
        return false;

    /* Decimal to float: NOT safe (lossy) */
    if (from->scalar_type == RUR_SCALAR_DECIMAL &&
        to->scalar_type == RUR_SCALAR_FLOAT)
        return false;

    /* Float to float: safe if target is wider */
    if (from->scalar_type == RUR_SCALAR_FLOAT &&
        to->scalar_type == RUR_SCALAR_FLOAT)
        return to->float_format >= from->float_format;

    /* Logical to logical: safe */
    if (from->scalar_type == RUR_SCALAR_LOGICAL &&
        to->scalar_type == RUR_SCALAR_LOGICAL)
        return true;

    /* String to string: safe if target is variable or same fixed size */
    if (from->scalar_type == RUR_SCALAR_STRING &&
        to->scalar_type == RUR_SCALAR_STRING)
        return true;

    /* Capability handles: never convertible */
    if (from->scalar_type == RUR_SCALAR_CAPABILITY ||
        to->scalar_type == RUR_SCALAR_CAPABILITY)
        return false;

    return false;
}

/* ===== Name Functions ===== */

const char *rur_beam_name(rur_beam_t beam) {
    switch (beam) {
        case RUR_BEAM_RECORD_TRANSACTION:   return "record_transaction";
        case RUR_BEAM_NUMERICAL_SCIENTIFIC: return "numerical_scientific";
        case RUR_BEAM_SYMBOLIC_FUNCTIONAL:  return "symbolic_functional";
        case RUR_BEAM_CONCATENATIVE:        return "concatenative";
        case RUR_BEAM_ARRAY_RANK:           return "array_rank";
        case RUR_BEAM_EVENT_CONTRACT:       return "event_contract";
        default:                             return "unknown";
    }
}

const char *rur_scalar_type_name(rur_scalar_type_t type) {
    switch (type) {
        case RUR_SCALAR_NONE:       return "none";
        case RUR_SCALAR_SINT:       return "sint";
        case RUR_SCALAR_UINT:       return "uint";
        case RUR_SCALAR_DECIMAL:    return "decimal";
        case RUR_SCALAR_RATIONAL:   return "rational";
        case RUR_SCALAR_FLOAT:      return "float";
        case RUR_SCALAR_DEC_FLOAT:  return "dec_float";
        case RUR_SCALAR_COMPLEX:    return "complex";
        case RUR_SCALAR_LOGICAL:    return "logical";
        case RUR_SCALAR_CHAR:       return "char";
        case RUR_SCALAR_STRING:     return "string";
        case RUR_SCALAR_CAPABILITY: return "capability";
        default:                     return "unknown";
    }
}

const char *rur_aggregate_type_name(rur_aggregate_type_t type) {
    switch (type) {
        case RUR_AGG_NONE:     return "none";
        case RUR_AGG_RECORD:   return "record";
        case RUR_AGG_VARIANT:  return "variant";
        case RUR_AGG_ARRAY:    return "array";
        case RUR_AGG_LIST:     return "list";
        case RUR_AGG_STACK:    return "stack";
        case RUR_AGG_STREAM:   return "stream";
        default:                return "unknown";
    }
}

const char *rur_logic_state_name(rur_logic_state_t state) {
    switch (state) {
        case RUR_LOGIC_TRUE:    return "true";
        case RUR_LOGIC_FALSE:   return "false";
        case RUR_LOGIC_BOTH:    return "contradiction";
        case RUR_LOGIC_NEITHER: return "unresolved";
        default:                 return "unknown";
    }
}

const char *rur_op_family_name(rur_op_family_t op) {
    switch (op) {
        case RUR_OP_TYPED_CONST:      return "typed_const";
        case RUR_OP_CONVERSION:       return "conversion";
        case RUR_OP_RECORD_FIELD:     return "record_field";
        case RUR_OP_ARRAY_INDEX:      return "array_index";
        case RUR_OP_LIST_CONS:        return "list_cons";
        case RUR_OP_STACK_PUSH:       return "stack_push";
        case RUR_OP_STACK_POP:        return "stack_pop";
        case RUR_OP_INT_ARITH:        return "int_arith";
        case RUR_OP_DECIMAL_ARITH:    return "decimal_arith";
        case RUR_OP_RATIONAL_ARITH:   return "rational_arith";
        case RUR_OP_FLOAT_ARITH:      return "float_arith";
        case RUR_OP_COMPLEX_ARITH:    return "complex_arith";
        case RUR_OP_LOGICAL_OP:       return "logical_op";
        case RUR_OP_CONTROL_FLOW:     return "control_flow";
        case RUR_OP_CALL:             return "call";
        case RUR_OP_RETURN:           return "return";
        case RUR_OP_CONDITION:        return "condition";
        case RUR_OP_EXCEPTION:        return "exception";
        case RUR_OP_ALLOC:            return "alloc";
        case RUR_OP_GC_SAFEPOINT:     return "gc_safepoint";
        case RUR_OP_FILE_IO:          return "file_io";
        case RUR_OP_DB_IO:            return "db_io";
        case RUR_OP_TERMINAL_IO:      return "terminal_io";
        case RUR_OP_NETWORK_IO:       return "network_io";
        case RUR_OP_DEVICE_IO:        return "device_io";
        case RUR_OP_EVENT_EMIT:       return "event_emit";
        case RUR_OP_EVENT_RECEIVE:    return "event_receive";
        case RUR_OP_EVENT_WAIT:       return "event_wait";
        case RUR_OP_EVENT_CANCEL:     return "event_cancel";
        case RUR_OP_EVENT_COMPENSATE: return "event_compensate";
        case RUR_OP_EVENT_RESOLVE:    return "event_resolve";
        case RUR_OP_CAP_ACQUIRE:      return "cap_acquire";
        case RUR_OP_CAP_USE:          return "cap_use";
        case RUR_OP_CAP_NARROW:       return "cap_narrow";
        case RUR_OP_CAP_RELEASE:      return "cap_release";
        case RUR_OP_VECTOR_MAP:       return "vector_map";
        case RUR_OP_VECTOR_REDUCE:    return "vector_reduce";
        case RUR_OP_VECTOR_SCAN:      return "vector_scan";
        case RUR_OP_VECTOR_INNER:     return "vector_inner";
        case RUR_OP_VECTOR_OUTER:     return "vector_outer";
        case RUR_OP_SYM_QUOTE:        return "sym_quote";
        case RUR_OP_SYM_EXPAND:       return "sym_expand";
        case RUR_OP_SYM_MATCH:        return "sym_match";
        case RUR_OP_SYM_REWRITE:      return "sym_rewrite";
        case RUR_OP_SYM_EVAL:         return "sym_eval";
        case RUR_OP_DEBUG:            return "debug";
        case RUR_OP_PROFILE:          return "profile";
        case RUR_OP_AUDIT:            return "audit";
        case RUR_OP_PROVENANCE:       return "provenance";
        default:                       return "unknown";
    }
}
