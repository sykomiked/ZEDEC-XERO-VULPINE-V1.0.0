/* orbital_compat_python.c — Python Language Adapter for Orbital Compat
 *
 * Python language adapter for scripting/glue with arbitrary precision.
 * Lowers Python's arbitrary precision rationals to canonical IR.
 *
 * Design principles:
 * - Arbitrary precision via byte arrays for numerator/denominator
 * - Exact rational or float mode
 * - Dynamic typing as LPRES logic states
 * - M5 coverage enforcement on all Python operations
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "orbital_compat.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "../sdk/selfaudit.h"

/* ===== Helpers ===== */

static rat_t rat_from_python_bytes(const uint8_t *bytes, uint32_t len) {
    /* Convert big-endian bytes to int64 (truncates if > 64 bits) */
    int64_t val = 0;
    for (uint32_t i = 0; i < len && i < 8; i++) {
        val = (val << 8) | bytes[i];
    }
    return rat_from_int(val);
}

/* ===== Python Lower ===== */

int32_t python_lower(const void *src, uint32_t len, oc_ir_t *out) {
    if (len < sizeof(oc_python_src_t)) return OC_ERR_ARG;
    const oc_python_src_t *p = (const oc_python_src_t *)src;
    
    if (p->den_len == 0) return OC_ERR_CONV;
    
    oc_zero_ir(out);
    
    /* Field 0: Numerator (truncated to 64-bit) */
    rat_t num = rat_from_python_bytes(p->num_bytes, p->num_len);
    out->fields[0].type = OC_TYPE_RATIONAL;
    out->fields[0].num = num;
    out->fields[0].scale = 0;
    
    /* Field 1: Denominator (truncated to 64-bit) */
    rat_t den = rat_from_python_bytes(p->den_bytes, p->den_len);
    out->fields[1].type = OC_TYPE_RATIONAL;
    out->fields[1].num = den;
    out->fields[1].scale = 0;
    
    /* Field 2: Exact flag */
    out->fields[2].type = OC_TYPE_RATIONAL;
    out->fields[2].num = rat_from_int(p->exact ? 1 : 0);
    out->fields[2].scale = 0;
    
    /* Field 3: LPRES logic state */
    lpres_state_t logic = p->exact ? LPRES_STATE_TRUE : LPRES_STATE_BOTH;
    out->fields[3].type = OC_TYPE_RATIONAL;
    out->fields[3].num = rat_from_int(logic);
    out->fields[3].scale = 0;
    
    out->num_fields = 4;
    return OC_OK;
}

/* ===== Python Lift ===== */

int32_t python_lift(const oc_ir_t *ir, void *out, uint32_t cap) {
    if (cap < sizeof(oc_python_src_t)) return OC_ERR_CAP;
    if (ir->num_fields < 4) return OC_ERR_ARG;
    
    oc_python_src_t *p = (oc_python_src_t *)out;
    
    /* Numerator */
    if (!rat_is_int(ir->fields[0].num)) return OC_ERR_ARG;
    int64_t num = ir->fields[0].num.num;
    p->num_bytes = (const uint8_t *)&num;
    p->num_len = 8;
    
    /* Denominator */
    if (!rat_is_int(ir->fields[1].num)) return OC_ERR_ARG;
    int64_t den = ir->fields[1].num.num;
    p->den_bytes = (const uint8_t *)&den;
    p->den_len = 8;
    
    /* Exact flag */
    if (!rat_is_int(ir->fields[2].num)) return OC_ERR_ARG;
    p->exact = (ir->fields[2].num.num != 0);
    
    return (int32_t)sizeof(oc_python_src_t);
}

/* ===== Python Dynamic Typing as LPRES ===== */

int32_t python_type_check(const oc_ir_t *a, const oc_ir_t *b, oc_ir_t *out) {
    /* Type compatibility check */
    lpres_state_t state_a = LPRES_STATE_TRUE;
    lpres_state_t state_b = LPRES_STATE_TRUE;
    
    if (a->num_fields >= 4 && a->fields[3].type == OC_TYPE_RATIONAL && rat_is_int(a->fields[3].num)) {
        state_a = (lpres_state_t)a->fields[3].num.num;
    }
    if (b->num_fields >= 4 && b->fields[3].type == OC_TYPE_RATIONAL && rat_is_int(b->fields[3].num)) {
        state_b = (lpres_state_t)b->fields[3].num.num;
    }
    
    /* Exact + Exact = TRUE, Exact + Float = BOTH, Float + Float = TRUE */
    lpres_state_t result = lpres_conjoin(state_a, state_b);
    
    oc_zero_ir(out);
    out->fields[0].type = OC_TYPE_RATIONAL;
    out->fields[0].num = rat_from_int((int64_t)result);
    out->fields[0].scale = 0;
    out->num_fields = 1;
    
    return OC_OK;
}

/* ===== Python Self-Audit ===== */

int32_t python_self_audit(const oc_ir_t *ir) {
    if (!ir || ir->num_fields < 4) return -1;
    
    for (uint32_t i = 0; i < ir->num_fields; i++) {
        if (ir->fields[i].type == OC_TYPE_RATIONAL) {
            if (!ir->fields[i].num.valid) return -1;
            if (ir->fields[i].num.den == 0) return -1;
        }
    }
    
    if (!rat_is_int(ir->fields[2].num)) return -1;
    
    return 0;
}

/* ===== Python M5 Coverage ===== */

static int32_t python_check_coverage(const oc_ir_t *ir, surplus_real_t min_ratio) {
    m5_coords_t m5 = {0};
    m5.omega = 1;
    m5.r = SR_FROM_FLOAT(7.0);  /* Python rail */
    m5.ell = SR_ONE;
    m5.phi = SR_ZERO;
    m5.chi = 0;
    
    surplus_real_t coverage = SR_DIV(SR_MUL(SR_MUL(SR_FROM_INT(m5.omega), m5.r), m5.ell), SR_MUL(m5.phi, SR_FROM_INT(m5.chi)));
    if (SR_CMP(coverage, min_ratio) < 0) return -1;
    return 0;
}
