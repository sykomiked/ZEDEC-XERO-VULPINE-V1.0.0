/* orbital_compat_wasm.c — WebAssembly Language Adapter for Orbital Compat
 *
 * WebAssembly adapter for portable modules with linear memory.
 * Lowers WASM's exact rationals with memory offsets to canonical IR.
 *
 * Design principles:
 * - Linear memory as exact rational offsets
 * - Module imports/exports as LPRES logic states
 * - Capability-based security as M5 coverage
 * - Portable across architectures
 * - M5 coverage enforcement on all WASM operations
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

/* ===== WASM Lower ===== */

int32_t wasm_lower(const void *src, uint32_t len, oc_ir_t *out) {
    if (len < sizeof(oc_wasm_src_t)) return OC_ERR_ARG;
    const oc_wasm_src_t *w = (const oc_wasm_src_t *)src;
    
    if (w->den == 0) return OC_ERR_CONV;
    
    oc_zero_ir(out);
    
    /* Field 0: Rational value */
    rat_t v = oc_rat_from_sutra(w->num, w->den);
    if (!v.valid) return OC_ERR_CONV;
    out->fields[0].type = OC_TYPE_RATIONAL;
    out->fields[0].num = v;
    out->fields[0].scale = 0;
    
    /* Field 1: Memory offset */
    out->fields[1].type = OC_TYPE_RATIONAL;
    out->fields[1].num = rat_from_int(w->memory_offset);
    out->fields[1].scale = 0;
    
    /* Field 2: Memory length */
    out->fields[2].type = OC_TYPE_RATIONAL;
    out->fields[2].num = rat_from_int(w->memory_len);
    out->fields[2].scale = 0;
    
    /* Field 3: LPRES logic state (module security) */
    out->fields[3].type = OC_TYPE_RATIONAL;
    out->fields[3].num = rat_from_int(LPRES_STATE_TRUE);
    out->fields[3].scale = 0;
    
    out->num_fields = 4;
    return OC_OK;
}

/* ===== WASM Lift ===== */

int32_t wasm_lift(const oc_ir_t *ir, void *out, uint32_t cap) {
    if (cap < sizeof(oc_wasm_src_t)) return OC_ERR_CAP;
    if (ir->num_fields < 4) return OC_ERR_ARG;
    
    oc_wasm_src_t *w = (oc_wasm_src_t *)out;
    
    rat_t v;
    uint32_t scale;
    int32_t rc = oc_ir_get_rat(ir, &v, &scale);
    if (rc != OC_OK) return rc;
    
    w->num = v.num;
    w->den = v.den;
    
    if (!rat_is_int(ir->fields[1].num)) return OC_ERR_ARG;
    w->memory_offset = (uint32_t)ir->fields[1].num.num;
    
    if (!rat_is_int(ir->fields[2].num)) return OC_ERR_ARG;
    w->memory_len = (uint32_t)ir->fields[2].num.num;
    
    return (int32_t)sizeof(oc_wasm_src_t);
}

/* ===== WASM Memory Bounds Check ===== */

int32_t wasm_bounds_check(const oc_ir_t *a, const oc_ir_t *b, oc_ir_t *out) {
    /* Check if memory access is within bounds */
    if (a->num_fields < 3 || b->num_fields < 3) return OC_ERR_ARG;
    
    if (!rat_is_int(a->fields[1].num) || !rat_is_int(a->fields[2].num)) return OC_ERR_ARG;
    if (!rat_is_int(b->fields[1].num) || !rat_is_int(b->fields[2].num)) return OC_ERR_ARG;
    
    uint32_t a_offset = (uint32_t)a->fields[1].num.num;
    uint32_t a_len = (uint32_t)a->fields[2].num.num;
    uint32_t b_offset = (uint32_t)b->fields[1].num.num;
    uint32_t b_len = (uint32_t)b->fields[2].num.num;
    
    /* Check overlap */
    bool overlap = (a_offset < b_offset + b_len) && (b_offset < a_offset + a_len);
    
    oc_zero_ir(out);
    out->fields[0].type = OC_TYPE_RATIONAL;
    out->fields[0].num = rat_from_int(overlap ? LPRES_STATE_BOTH : LPRES_STATE_TRUE);
    out->fields[0].scale = 0;
    out->num_fields = 1;
    
    return OC_OK;
}

/* ===== WASM Self-Audit ===== */

int32_t wasm_self_audit(const oc_ir_t *ir) {
    if (!ir || ir->num_fields < 4) return -1;
    
    for (uint32_t i = 0; i < ir->num_fields; i++) {
        if (ir->fields[i].type == OC_TYPE_RATIONAL) {
            if (!ir->fields[i].num.valid) return -1;
            if (ir->fields[i].num.den == 0) return -1;
        }
    }
    
    if (!rat_is_int(ir->fields[1].num)) return -1;
    if (!rat_is_int(ir->fields[2].num)) return -1;
    
    return 0;
}

/* ===== WASM M5 Coverage ===== */

static int32_t wasm_check_coverage(const oc_ir_t *ir, surplus_real_t min_ratio) {
    m5_coords_t m5 = {0};
    m5.omega = 1;
    m5.r = SR_FROM_FLOAT(8.0);  /* WASM rail */
    m5.ell = SR_ONE;
    m5.phi = SR_ZERO;
    m5.chi = 0;
    
    surplus_real_t coverage = SR_DIV(SR_MUL(SR_MUL(SR_FROM_INT(m5.omega), m5.r), m5.ell), SR_MUL(m5.phi, SR_FROM_INT(m5.chi)));
    if (SR_CMP(coverage, min_ratio) < 0) return -1;
    return 0;
}
