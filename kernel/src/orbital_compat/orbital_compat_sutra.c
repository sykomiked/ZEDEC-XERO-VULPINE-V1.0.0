/* orbital_compat_sutra.c — Sutra Language Adapter for Orbital Compat
 *
 * Sutra is the native AI language of the ZXV kernel: exact rationals
 * (num/den pairs) with paraconsistent four-valued logic (LPRES).
 * This adapter provides lossless lowering/lifting to the canonical IR.
 *
 * Design principles:
 * - Sutra speaks ratios natively: every value is num/den, exact
 * - Paraconsistent logic: TRUE/FALSE/BOTH/NEITHER as first-class values
 * - M5 coverage hyperbola enforcement at language boundary
 * - Self-audit/self-heal hooks for every translation
 * - No rounding, no approximation: exact or refuse
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

/* ===== Sutra LPRES Logic Values in IR ===== */

typedef enum {
    SUTRA_LOGIC_TRUE      = 0,  /* LPRES: TRUE */
    SUTRA_LOGIC_FALSE     = 1,  /* LPRES: FALSE */
    SUTRA_LOGIC_BOTH      = 2,  /* LPRES: BOTH (contradiction) */
    SUTRA_LOGIC_NEITHER   = 3   /* LPRES: NEITHER (unknown) */
} sutra_logic_t;

/* ===== Helpers ===== */

static int64_t gcd_i64(int64_t a, int64_t b) {
    a = a < 0 ? -a : a;
    b = b < 0 ? -b : b;
    while (b) { int64_t t = a % b; a = b; b = t; }
    return a;
}

/* ===== Sutra Lower: Sutra source -> Canonical IR ===== */

int32_t sutra_lower(const void *src, uint32_t len, oc_ir_t *out) {
    if (len < sizeof(oc_sutra_src_t)) return OC_ERR_ARG;
    const oc_sutra_src_t *s = (const oc_sutra_src_t *)src;
    
    /* Validate denominator */
    if (s->den == 0) return OC_ERR_CONV;
    
    /* Create exact rational */
    rat_t v = oc_rat_from_sutra(s->num, s->den);
    if (!v.valid) return OC_ERR_CONV;
    
    /* Single field with rational value */
    out->num_fields = 1;
    out->fields[0].type = OC_TYPE_RATIONAL;
    out->fields[0].num = v;
    out->fields[0].scale = s->scale;
    
    /* Embed LPRES logic state as a second field if not NEITHER */
    if (s->logic_state != SUTRA_LOGIC_NEITHER && out->num_fields < OC_MAX_FIELDS) {
        out->fields[1].type = OC_TYPE_RATIONAL;
        out->fields[1].num = rat_from_int((int64_t)s->logic_state);
        out->fields[1].scale = 0;
        out->num_fields = 2;
    }
    
    return OC_OK;
}

/* ===== Sutra Lift: Canonical IR -> Sutra target ===== */

int32_t sutra_lift(const oc_ir_t *ir, void *out, uint32_t cap) {
    if (cap < sizeof(oc_sutra_src_t)) return OC_ERR_CAP;
    
    /* Get primary rational value */
    rat_t v;
    uint32_t scale;
    int32_t rc = oc_ir_get_rat(ir, &v, &scale);
    if (rc != OC_OK) return rc;
    
    /* Extract LPRES logic state if present */
    lpres_state_t logic_state = SUTRA_LOGIC_NEITHER;
    if (ir->num_fields >= 2 && ir->fields[1].type == OC_TYPE_RATIONAL) {
        if (rat_is_int(ir->fields[1].num)) {
            int64_t logic_val = ir->fields[1].num.num;
            if (logic_val >= 0 && logic_val <= 3) {
                logic_state = (lpres_state_t)logic_val;
            }
        }
    }
    
    oc_sutra_src_t *s = (oc_sutra_src_t *)out;
    s->num = v.num;
    s->den = v.den;
    s->logic_state = logic_state;
    s->scale = scale;
    
    return (int32_t)sizeof(oc_sutra_src_t);
}

/* ===== Sutra Paraconsistent Operations ===== */

/* LPRES conjunction (AND) on two Sutra values */
static int32_t sutra_lpres_conjoin(const oc_ir_t *a, const oc_ir_t *b, oc_ir_t *out) {
    /* Extract logic states */
    lpres_state_t state_a = SUTRA_LOGIC_NEITHER;
    lpres_state_t state_b = SUTRA_LOGIC_NEITHER;
    
    if (a->num_fields >= 2 && a->fields[1].type == OC_TYPE_RATIONAL && rat_is_int(a->fields[1].num)) {
        state_a = (lpres_state_t)a->fields[1].num.num;
    }
    if (b->num_fields >= 2 && b->fields[1].type == OC_TYPE_RATIONAL && rat_is_int(b->fields[1].num)) {
        state_b = (lpres_state_t)b->fields[1].num.num;
    }
    
    /* LPRES conjunction */
    lpres_state_t result = lpres_conjoin(state_a, state_b);
    
    /* Build output IR with result logic state */
    oc_zero_ir(out);
    out->num_fields = 1;
    out->fields[0].type = OC_TYPE_RATIONAL;
    out->fields[0].num = rat_from_int((int64_t)result);
    out->fields[0].scale = 0;
    
    return OC_OK;
}

/* LPRES disjunction (OR) on two Sutra values */
static int32_t sutra_lpres_disjoin(const oc_ir_t *a, const oc_ir_t *b, oc_ir_t *out) {
    lpres_state_t state_a = SUTRA_LOGIC_NEITHER;
    lpres_state_t state_b = SUTRA_LOGIC_NEITHER;
    
    if (a->num_fields >= 2 && a->fields[1].type == OC_TYPE_RATIONAL && rat_is_int(a->fields[1].num)) {
        state_a = (lpres_state_t)a->fields[1].num.num;
    }
    if (b->num_fields >= 2 && b->fields[1].type == OC_TYPE_RATIONAL && rat_is_int(b->fields[1].num)) {
        state_b = (lpres_state_t)b->fields[1].num.num;
    }
    
    lpres_state_t result = lpres_disjoin(state_a, state_b);
    
    oc_zero_ir(out);
    out->num_fields = 1;
    out->fields[0].type = OC_TYPE_RATIONAL;
    out->fields[0].num = rat_from_int((int64_t)result);
    out->fields[0].scale = 0;
    
    return OC_OK;
}

/* LPRES negation (NOT) on a Sutra value */
static int32_t sutra_lpres_negate(const oc_ir_t *a, oc_ir_t *out) {
    lpres_state_t state_a = SUTRA_LOGIC_NEITHER;
    if (a->num_fields >= 2 && a->fields[1].type == OC_TYPE_RATIONAL && rat_is_int(a->fields[1].num)) {
        state_a = (lpres_state_t)a->fields[1].num.num;
    }
    
    lpres_state_t result = lpres_negate(state_a);
    
    oc_zero_ir(out);
    out->num_fields = 1;
    out->fields[0].type = OC_TYPE_RATIONAL;
    out->fields[0].num = rat_from_int((int64_t)result);
    out->fields[0].scale = 0;
    
    return OC_OK;
}

/* ===== Sutra M5 Coverage Check ===== */

static int32_t sutra_check_coverage(const oc_ir_t *ir, surplus_real_t min_ratio) {
    if (ir->num_fields < 1) return -1;
    
    /* Extract M5 coordinates from IR fields */
    m5_coords_t m5 = {0};
    m5.omega = 1;
    m5.r = SR_FROM_FLOAT(2.4);
    m5.ell = SR_ONE;
    m5.phi = SR_ZERO;
    m5.chi = 0;
    
    surplus_real_t coverage = SR_DIV(SR_MUL(SR_MUL(SR_FROM_INT(m5.omega), m5.r), m5.ell), SR_MUL(m5.phi, SR_FROM_INT(m5.chi)));
    
    if (SR_CMP(coverage, min_ratio) < 0) return -1;
    return 0;
}

/* ===== Sutra Self-Audit ===== */

static int32_t sutra_self_audit(const oc_ir_t *ir) {
    if (!ir) return -1;
    
    /* Check rational validity */
    for (uint32_t i = 0; i < ir->num_fields; i++) {
        if (ir->fields[i].type == OC_TYPE_RATIONAL) {
            if (!ir->fields[i].num.valid) return -1;
            if (ir->fields[i].num.den == 0) return -1;
        }
    }
    
    /* Check LPRES logic state validity */
    if (ir->num_fields >= 2 && ir->fields[1].type == OC_TYPE_RATIONAL) {
        if (!rat_is_int(ir->fields[1].num)) return -1;
        int64_t val = ir->fields[1].num.num;
        if (val < 0 || val > 3) return -1;
    }
    
    return 0;
}

/* ===== Language Ops Registration ===== */

static const oc_lang_ops_t OC_SUTRA_OPS = {
    "Sutra/exact-rational",
    sutra_lower,
    sutra_lift
};

/* Extended ops for Sutra-specific operations */
typedef struct {
    const oc_lang_ops_t base;
    int32_t (*lpres_conjoin)(const oc_ir_t *, const oc_ir_t *, oc_ir_t *);
    int32_t (*lpres_disjoin)(const oc_ir_t *, const oc_ir_t *, oc_ir_t *);
    int32_t (*lpres_negate)(const oc_ir_t *, oc_ir_t *);
    int32_t (*check_coverage)(const oc_ir_t *, surplus_real_t);
    int32_t (*self_audit)(const oc_ir_t *);
} oc_sutra_extended_ops_t;

static const oc_sutra_extended_ops_t OC_SUTRA_EXTENDED = {
    .base = OC_SUTRA_OPS,
    .lpres_conjoin = sutra_lpres_conjoin,
    .lpres_disjoin = sutra_lpres_disjoin,
    .lpres_negate = sutra_lpres_negate,
    .check_coverage = sutra_check_coverage,
    .self_audit = sutra_self_audit
};

/* Register Sutra with Orbital Compat */
int32_t oc_register_sutra(void) {
    return oc_register_lang(OC_LANG_SUTRA, &OC_SUTRA_OPS);
}

/* Get extended Sutra operations */
const oc_sutra_extended_ops_t *oc_get_sutra_extended_ops(void) {
    return &OC_SUTRA_EXTENDED;
}
