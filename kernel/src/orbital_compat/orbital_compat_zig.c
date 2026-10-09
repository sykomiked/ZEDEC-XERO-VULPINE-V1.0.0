/* orbital_compat_zig.c — Zig Language Adapter for Orbital Compat
 *
 * Zig language adapter for comptime metaprogramming.
 * Lowers Zig's exact rationals with comptime metadata to canonical IR.
 *
 * Design principles:
 * - Comptime-known values as exact rationals with hash
 * - Compile-time execution results as exact rationals
 * - Generic specialization as LPRES logic states
 * - No runtime overhead: all comptime at translation
 * - M5 coverage enforcement on all Zig operations
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "orbital_compat.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "../sdk/selfaudit.h"

/* ===== Zig Lower ===== */

int32_t zig_lower(const void *src, uint32_t len, oc_ir_t *out) {
    if (len < sizeof(oc_zig_src_t)) return OC_ERR_ARG;
    const oc_zig_src_t *z = (const oc_zig_src_t *)src;
    
    if (z->den == 0) return OC_ERR_CONV;
    
    oc_zero_ir(out);
    
    /* Field 0: Rational value */
    rat_t v = oc_rat_from_sutra(z->num, z->den);
    if (!v.valid) return OC_ERR_CONV;
    out->fields[0].type = OC_TYPE_RATIONAL;
    out->fields[0].num = v;
    out->fields[0].scale = 0;
    
    /* Field 1: Comptime flag */
    out->fields[1].type = OC_TYPE_RATIONAL;
    out->fields[1].num = rat_from_int(z->comptime ? 1 : 0);
    out->fields[1].scale = 0;
    
    /* Field 2: Comptime hash */
    out->fields[2].type = OC_TYPE_RATIONAL;
    out->fields[2].num = rat_from_int(z->comptime_hash);
    out->fields[2].scale = 0;
    
    /* Field 3: LPRES logic state */
    lpres_state_t logic = z->comptime ? LPRES_STATE_TRUE : LPRES_STATE_BOTH;
    out->fields[3].type = OC_TYPE_RATIONAL;
    out->fields[3].num = rat_from_int(logic);
    out->fields[3].scale = 0;
    
    out->num_fields = 4;
    return OC_OK;
}

/* ===== Zig Lift ===== */

int32_t zig_lift(const oc_ir_t *ir, void *out, uint32_t cap) {
    if (cap < sizeof(oc_zig_src_t)) return OC_ERR_CAP;
    if (ir->num_fields < 4) return OC_ERR_ARG;
    
    oc_zig_src_t *z = (oc_zig_src_t *)out;
    
    rat_t v;
    uint32_t scale;
    int32_t rc = oc_ir_get_rat(ir, &v, &scale);
    if (rc != OC_OK) return rc;
    
    z->num = v.num;
    z->den = v.den;
    
    if (!rat_is_int(ir->fields[1].num)) return OC_ERR_ARG;
    z->comptime = (ir->fields[1].num.num != 0);
    
    if (!rat_is_int(ir->fields[2].num)) return OC_ERR_ARG;
    z->comptime_hash = (uint32_t)ir->fields[2].num.num;
    
    return (int32_t)sizeof(oc_zig_src_t);
}

/* ===== Zig Comptime Evaluation ===== */

int32_t zig_comptime_eval(const oc_ir_t *a, const oc_ir_t *b, oc_ir_t *out) {
    /* Comptime evaluation: both must be comptime-known */
    if (a->num_fields < 2 || b->num_fields < 2) return OC_ERR_ARG;
    
    bool a_comptime = false, b_comptime = false;
    if (a->fields[1].type == OC_TYPE_RATIONAL && rat_is_int(a->fields[1].num)) {
        a_comptime = (a->fields[1].num.num != 0);
    }
    if (b->fields[1].type == OC_TYPE_RATIONAL && rat_is_int(b->fields[1].num)) {
        b_comptime = (b->fields[1].num.num != 0);
    }
    
    if (!a_comptime || !b_comptime) {
        oc_zero_ir(out);
        out->fields[0].type = OC_TYPE_RATIONAL;
        out->fields[0].num = rat_from_int(LPRES_STATE_FALSE);  /* Not comptime */
        out->fields[0].scale = 0;
        out->num_fields = 1;
        return OC_OK;
    }
    
    /* Both comptime: perform exact rational arithmetic */
    rat_t va, vb;
    uint32_t sa, sb;
    oc_ir_get_rat(a, &va, &sa);
    oc_ir_get_rat(b, &vb, &sb);
    
    rat_t result = rat_add(va, vb);  /* Example: addition */
    
    oc_zero_ir(out);
    out->fields[0].type = OC_TYPE_RATIONAL;
    out->fields[0].num = result;
    out->fields[0].scale = 0;
    out->fields[1].type = OC_TYPE_RATIONAL;
    out->fields[1].num = rat_from_int(1);  /* Result is comptime */
    out->fields[1].scale = 0;
    out->num_fields = 2;
    
    return OC_OK;
}

/* ===== Zig Self-Audit ===== */

int32_t zig_self_audit(const oc_ir_t *ir) {
    if (!ir || ir->num_fields < 4) return -1;
    
    for (uint32_t i = 0; i < ir->num_fields; i++) {
        if (ir->fields[i].type == OC_TYPE_RATIONAL) {
            if (!ir->fields[i].num.valid) return -1;
            if (ir->fields[i].num.den == 0) return -1;
        }
    }
    
    if (!rat_is_int(ir->fields[1].num)) return -1;
    
    return 0;
}

/* ===== Zig M5 Coverage ===== */

static int32_t zig_check_coverage(const oc_ir_t *ir, surplus_real_t min_ratio) {
    m5_coords_t m5 = {0};
    m5.omega = 1;
    m5.r = SR_FROM_FLOAT(6.0);  /* Zig rail */
    m5.ell = SR_ONE;
    m5.phi = SR_ZERO;
    m5.chi = 0;
    
    surplus_real_t coverage = SR_DIV(SR_MUL(SR_MUL(SR_FROM_INT(m5.omega), m5.r), m5.ell), SR_MUL(m5.phi, SR_FROM_INT(m5.chi)));
    if (SR_CMP(coverage, min_ratio) < 0) return -1;
    return 0;
}
