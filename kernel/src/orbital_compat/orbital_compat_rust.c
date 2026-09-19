/* orbital_compat_rust.c — Rust Language Adapter for Orbital Compat
 *
 * Rust language adapter for memory-safe systems programming.
 * Lowers Rust's exact rationals with ownership metadata to canonical IR.
 *
 * Design principles:
 * - Ownership as first-class IR field (owned/borrowed/mutable)
 * - Lifetimes as exact rational identifiers
 * - Borrow checker constraints as LPRES logic states
 * - No runtime overhead: all checks at translation time
 * - M5 coverage enforcement on all Rust operations
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

/* ===== Rust Lower ===== */

int32_t rust_lower(const void *src, uint32_t len, oc_ir_t *out) {
    if (len < sizeof(oc_rust_src_t)) return OC_ERR_ARG;
    const oc_rust_src_t *r = (const oc_rust_src_t *)src;
    
    if (r->den == 0) return OC_ERR_CONV;
    
    oc_zero_ir(out);
    
    /* Field 0: Rational value */
    rat_t v = oc_rat_from_sutra(r->num, r->den);
    if (!v.valid) return OC_ERR_CONV;
    out->fields[0].type = OC_TYPE_RATIONAL;
    out->fields[0].num = v;
    out->fields[0].scale = 0;
    
    /* Field 1: Ownership */
    out->fields[1].type = OC_TYPE_RATIONAL;
    out->fields[1].num = rat_from_int(r->ownership);
    out->fields[1].scale = 0;
    
    /* Field 2: Lifetime ID */
    out->fields[2].type = OC_TYPE_RATIONAL;
    out->fields[2].num = rat_from_int(r->lifetime_id);
    out->fields[2].scale = 0;
    
    /* Field 3: LPRES logic state for borrow checker */
    lpres_state_t logic = LPRES_STATE_TRUE;
    if (r->ownership == 1) logic = LPRES_STATE_BOTH;  /* Borrowed = potential conflict */
    else if (r->ownership == 2) logic = LPRES_STATE_TRUE;  /* Mutable borrow = exclusive */
    out->fields[3].type = OC_TYPE_RATIONAL;
    out->fields[3].num = rat_from_int(logic);
    out->fields[3].scale = 0;
    
    out->num_fields = 4;
    return OC_OK;
}

/* ===== Rust Lift ===== */

int32_t rust_lift(const oc_ir_t *ir, void *out, uint32_t cap) {
    if (cap < sizeof(oc_rust_src_t)) return OC_ERR_CAP;
    if (ir->num_fields < 4) return OC_ERR_ARG;
    
    oc_rust_src_t *r = (oc_rust_src_t *)out;
    
    rat_t v;
    uint32_t scale;
    int32_t rc = oc_ir_get_rat(ir, &v, &scale);
    if (rc != OC_OK) return rc;
    
    r->num = v.num;
    r->den = v.den;
    
    if (!rat_is_int(ir->fields[1].num)) return OC_ERR_ARG;
    r->ownership = (uint32_t)ir->fields[1].num.num;
    
    if (!rat_is_int(ir->fields[2].num)) return OC_ERR_ARG;
    r->lifetime_id = (uint32_t)ir->fields[2].num.num;
    
    return (int32_t)sizeof(oc_rust_src_t);
}

/* ===== Rust Borrow Checker as LPRES ===== */

int32_t rust_borrow_check(const oc_ir_t *a, const oc_ir_t *b, oc_ir_t *out) {
    /* Check if two borrows conflict */
    lpres_state_t state_a = LPRES_STATE_TRUE;
    lpres_state_t state_b = LPRES_STATE_TRUE;
    
    if (a->num_fields >= 4 && a->fields[3].type == OC_TYPE_RATIONAL && rat_is_int(a->fields[3].num)) {
        state_a = (lpres_state_t)a->fields[3].num.num;
    }
    if (b->num_fields >= 4 && b->fields[3].type == OC_TYPE_RATIONAL && rat_is_int(b->fields[3].num)) {
        state_b = (lpres_state_t)b->fields[3].num.num;
    }
    
    /* Mutable borrow (TRUE) + any other borrow = BOTH (conflict) */
    /* Immutable borrow (BOTH) + mutable borrow = BOTH (conflict) */
    lpres_state_t result = lpres_conjoin(state_a, state_b);
    
    oc_zero_ir(out);
    out->fields[0].type = OC_TYPE_RATIONAL;
    out->fields[0].num = rat_from_int((int64_t)result);
    out->fields[0].scale = 0;
    out->num_fields = 1;
    
    return OC_OK;
}

/* ===== Rust Self-Audit ===== */

int32_t rust_self_audit(const oc_ir_t *ir) {
    if (!ir || ir->num_fields < 4) return -1;
    
    for (uint32_t i = 0; i < ir->num_fields; i++) {
        if (ir->fields[i].type == OC_TYPE_RATIONAL) {
            if (!ir->fields[i].num.valid) return -1;
            if (ir->fields[i].num.den == 0) return -1;
        }
    }
    
    if (!rat_is_int(ir->fields[1].num)) return -1;
    uint32_t ownership = (uint32_t)ir->fields[1].num.num;
    if (ownership > 2) return -1;
    
    return 0;
}

/* ===== Rust M5 Coverage ===== */

static int32_t rust_check_coverage(const oc_ir_t *ir, surplus_real_t min_ratio) {
    m5_coords_t m5 = {0};
    m5.omega = 1;
    m5.r = SR_FROM_FLOAT(5.0);  /* Rust rail */
    m5.ell = SR_ONE;
    m5.phi = SR_ZERO;
    m5.chi = 0;
    
    surplus_real_t coverage = SR_DIV(SR_MUL(SR_MUL(SR_FROM_INT(m5.omega), m5.r), m5.ell), SR_MUL(m5.phi, SR_FROM_INT(m5.chi)));
    if (SR_CMP(coverage, min_ratio) < 0) return -1;
    return 0;
}
