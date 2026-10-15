/* orbital_compat_sutra.c — Sutra Language Adapter for Orbital Compat
 *
 * Sutra is the native AI language of the ZXV kernel: exact rationals
 * (num/den pairs) with paraconsistent four-valued logic (LPRES).
 * This adapter provides lossless lowering/lifting to the canonical IR.
 *
 * Design principles:
 * - Sutra speaks ratios natively: every value is num/den, exact
 * - Paraconsistent logic: TRUE/FALSE/BOTH/NEITHER as first-class values
 * - Self-audit/self-heal hooks for every translation
 * - No rounding, no approximation: exact or refuse
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "orbital_compat.h"
#include "lpres.h"
#include "../sdk/selfaudit.h"

/* ===== Sutra LPRES Logic Values in IR =====
 *
 * The logic state rides in field 1 in lpres numbering (NEITHER 0, TRUE 1,
 * FALSE 2, BOTH 3), so it lines up with lpres_conjoin and friends. NEITHER is
 * the absence of a tag: it is never embedded, and a missing or malformed tag
 * lifts back as NEITHER. A contradiction (BOTH) always survives the trip. */

/* ===== Sutra Lower: Sutra source -> Canonical IR ===== */

int32_t sutra_lower(const void *src, uint32_t len, oc_ir_t *out)
{
    if (len < sizeof(oc_sutra_src_t)) return OC_ERR_ARG;
    const oc_sutra_src_t *s = (const oc_sutra_src_t *)src;

    /* Validate denominator */
    if (s->den == 0) return OC_ERR_CONV;
    if (s->logic_state > LPRES_STATE_BOTH) return OC_ERR_CONV;

    /* Create exact rational */
    rat_t v = oc_rat_from_sutra(s->num, s->den);
    if (!v.valid) return OC_ERR_CONV;

    /* Single field with rational value */
    out->num_fields = 1;
    out->fields[0].type = OC_TYPE_RATIONAL;
    out->fields[0].num = v;
    out->fields[0].scale = s->scale;

    /* Embed the LPRES logic state as a second field unless it is NEITHER */
    if (s->logic_state != LPRES_STATE_NEITHER) {
        out->fields[1].type = OC_TYPE_RATIONAL;
        out->fields[1].num = rat_from_int((int64_t)s->logic_state);
        out->fields[1].scale = 0;
        out->num_fields = 2;
    }

    return OC_OK;
}

/* ===== Sutra Lift: Canonical IR -> Sutra target ===== */

int32_t sutra_lift(const oc_ir_t *ir, void *out, uint32_t cap)
{
    if (cap < sizeof(oc_sutra_src_t)) return OC_ERR_CAP;

    /* Get primary rational value */
    rat_t v;
    uint32_t scale;
    int32_t rc = oc_ir_get_rat(ir, &v, &scale);
    if (rc != OC_OK) return rc;

    oc_sutra_src_t *s = (oc_sutra_src_t *)out;
    s->num = v.num;
    s->den = v.den;
    s->logic_state = oc_field_lpres(ir, 1, LPRES_STATE_NEITHER);
    s->scale = scale;

    return (int32_t)sizeof(oc_sutra_src_t);
}

/* ===== Sutra Paraconsistent Operations ===== */

/* The result of a logic op is one field holding the lpres state. */
static void sutra_logic_result(oc_ir_t *out, lpres_state_t result)
{
    oc_zero_ir(out);
    out->num_fields = 1;
    out->fields[0].type = OC_TYPE_RATIONAL;
    out->fields[0].num = rat_from_int((int64_t)result);
    out->fields[0].scale = 0;
}

/* LPRES conjunction (AND) on two Sutra values */
static int32_t sutra_lpres_conjoin(const oc_ir_t *a, const oc_ir_t *b, oc_ir_t *out) {
    if (!a || !b || !out) return OC_ERR_ARG;
    sutra_logic_result(out, lpres_conjoin(oc_field_lpres(a, 1, LPRES_STATE_NEITHER),
                                          oc_field_lpres(b, 1, LPRES_STATE_NEITHER)));
    return OC_OK;
}

/* LPRES disjunction (OR) on two Sutra values */
static int32_t sutra_lpres_disjoin(const oc_ir_t *a, const oc_ir_t *b, oc_ir_t *out) {
    if (!a || !b || !out) return OC_ERR_ARG;
    sutra_logic_result(out, lpres_disjoin(oc_field_lpres(a, 1, LPRES_STATE_NEITHER),
                                          oc_field_lpres(b, 1, LPRES_STATE_NEITHER)));
    return OC_OK;
}

/* LPRES negation (NOT) on a Sutra value */
static int32_t sutra_lpres_negate(const oc_ir_t *a, oc_ir_t *out) {
    if (!a || !out) return OC_ERR_ARG;
    sutra_logic_result(out, lpres_negate(oc_field_lpres(a, 1, LPRES_STATE_NEITHER)));
    return OC_OK;
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
        if (val < LPRES_STATE_NEITHER || val > LPRES_STATE_BOTH) return -1;
    }
    
    return 0;
}

/* ===== Language Ops Registration ===== */

static const oc_lang_ops_t OC_SUTRA_OPS = {
    "Sutra/exact-rational",
    sutra_lower,
    sutra_lift
};

static const oc_sutra_extended_ops_t OC_SUTRA_EXTENDED = {
    .base = OC_SUTRA_OPS,
    .lpres_conjoin = sutra_lpres_conjoin,
    .lpres_disjoin = sutra_lpres_disjoin,
    .lpres_negate = sutra_lpres_negate,
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
