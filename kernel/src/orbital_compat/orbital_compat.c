/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* orbital_compat.c — the elevator car.
 *
 * The registry + the canonical IR + built-in COBOL/Fortran/C lowerings that
 * REUSE Lightning Rod's measured adapters. No new arithmetic is invented
 * here: every numeric conversion is a call into lightningrod, which is
 * exact by construction and asserted against known mainframe answers.
 */
#include "orbital_compat.h"
#include "../lightningrod/lightningrod.h"

/* ---- tiny freestanding helpers (no libc) ---- */
static void oc_zero_ir(oc_ir_t *ir) {
    ir->num_fields = 0;
    for (uint32_t i = 0; i < OC_MAX_FIELDS; i++) {
        oc_field_t *f = &ir->fields[i];
        f->type = OC_TYPE_NONE;
        f->num = rat_zero();
        f->text_len = 0;
        f->scale = 0;
        for (uint32_t j = 0; j < OC_TEXT_MAX; j++) f->text[j] = 0;
    }
}

/* 10^scale as int64. 10^18 is the largest power of ten that fits int64, so a
 * scale > 18 would overflow (signed-overflow UB) and yield a wrong-but-plausible
 * value. Callers on the LIFT path must reject scale > 18 first; this caps the
 * loop defensively so it can never run away or overflow past 10^18. */
static int64_t oc_pow10_i64(uint32_t scale) {
    if (scale > 18) return 0;               /* out of range — caller must have refused */
    int64_t p = 1;
    for (uint32_t i = 0; i < scale; i++) p *= 10;
    return p;
}

/* ===================== the registry ===================== */
/* Deterministic, phase-tick ordered: registration mutates this table and
 * nothing else; same registration order => same table => same behaviour. */
static oc_lang_ops_t g_ops[OC_LANG_MAX];
static bool          g_bound[OC_LANG_MAX];

int32_t oc_register_lang(oc_lang_t lang, const oc_lang_ops_t *ops) {
    if ((uint32_t)lang >= (uint32_t)OC_LANG_MAX || !ops || !ops->lower || !ops->lift)
        return OC_ERR_ARG;
    g_ops[lang]   = *ops;
    g_bound[lang] = true;
    return OC_OK;
}

bool oc_lang_registered(oc_lang_t lang) {
    if ((uint32_t)lang >= (uint32_t)OC_LANG_MAX) return false;
    return g_bound[lang];
}

const char *oc_lang_name(oc_lang_t lang) {
    switch (lang) {
        case OC_LANG_COBOL:   return "COBOL";
        case OC_LANG_FORTRAN: return "Fortran";
        case OC_LANG_C:       return "C";
        case OC_LANG_SUTRA:   return "Sutra";
        case OC_LANG_FUTURE:  return "Future";
        default:              return "?";
    }
}

int32_t oc_lower(oc_lang_t from, const void *src, uint32_t len, oc_ir_t *out) {
    if (!out) return OC_ERR_ARG;
    if ((uint32_t)from >= (uint32_t)OC_LANG_MAX || !g_bound[from])
        return OC_ERR_NO_LANG;                     /* fail closed, never invent */
    if (!src) return OC_ERR_ARG;
    oc_zero_ir(out);
    return g_ops[from].lower(src, len, out);
}

int32_t oc_lift(oc_lang_t to, const oc_ir_t *ir, void *out, uint32_t cap) {
    if (!ir || !out) return OC_ERR_ARG;
    if ((uint32_t)to >= (uint32_t)OC_LANG_MAX || !g_bound[to])
        return OC_ERR_NO_LANG;                     /* fail closed */
    return g_ops[to].lift(ir, out, cap);
}

bool oc_field_eq(const oc_field_t *a, const oc_field_t *b) {
    if (!a || !b) return false;
    if (a->type != b->type) return false;
    switch (a->type) {
        case OC_TYPE_RATIONAL:
            return rat_eq(a->num, b->num);        /* scale hint ignored */
        case OC_TYPE_TEXT: {
            /* Clamp to the real array bound: a caller-supplied text_len larger than
             * the fixed text[] would read out of bounds. */
            if (a->text_len > OC_TEXT_MAX || b->text_len > OC_TEXT_MAX) return false;
            if (a->text_len != b->text_len) return false;
            for (uint32_t i = 0; i < a->text_len; i++)
                if (a->text[i] != b->text[i]) return false;
            return true;
        }
        case OC_TYPE_NONE:
        default:
            return true;
    }
}

/* helper: put a single exact-rational field into a fresh IR */
static int32_t oc_ir_single_rat(oc_ir_t *out, rat_t v, uint32_t scale) {
    if (!v.valid) return OC_ERR_CONV;
    out->num_fields = 1;
    out->fields[0].type  = OC_TYPE_RATIONAL;
    out->fields[0].num   = v;
    out->fields[0].scale = scale;
    return OC_OK;
}

/* helper: read the single leading rational field of an IR */
static int32_t oc_ir_get_rat(const oc_ir_t *ir, rat_t *v, uint32_t *scale) {
    if (ir->num_fields < 1 || ir->fields[0].type != OC_TYPE_RATIONAL)
        return OC_ERR_ARG;
    *v = ir->fields[0].num;
    if (scale) *scale = ir->fields[0].scale;
    if (!v->valid) return OC_ERR_CONV;
    return OC_OK;
}

/* ================= built-in COBOL adapter (COMP-3) ================= */
/* Reuses Lightning Rod's measured packed-decimal conversions verbatim. */
static int32_t cobol_lower(const void *src, uint32_t len, oc_ir_t *out) {
    if (len < sizeof(oc_cobol_src_t)) return OC_ERR_ARG;
    const oc_cobol_src_t *c = (const oc_cobol_src_t *)src;
    rat_t v;
    lr_result_t r = lr_packed_to_rat(c->bytes, c->len, c->scale, &v);
    if (!r.ok) return OC_ERR_CONV;                 /* malformed BCD -> refuse */
    return oc_ir_single_rat(out, v, c->scale);
}
/* COBOL back-end: lift the IR's exact value back into a COMP-3 field, using
 * the field's scale hint. Refuses (never rounds) if it will not fit. */
static int32_t cobol_lift(const oc_ir_t *ir, void *out, uint32_t cap) {
    rat_t v; uint32_t scale;
    int32_t rc = oc_ir_get_rat(ir, &v, &scale);
    if (rc != OC_OK) return rc;
    uint32_t written = 0;
    lr_result_t w = lr_rat_to_packed(v, scale, (uint8_t *)out, cap, &written);
    if (!w.ok) return OC_ERR_CONV;
    return (int32_t)written;                        /* bytes written */
}

/* ================= built-in Fortran adapter (fixed-format) ========= */
/* Reuses Lightning Rod's scaled-integer conversion. */
static int32_t fortran_lower(const void *src, uint32_t len, oc_ir_t *out) {
    if (len < sizeof(oc_fortran_src_t)) return OC_ERR_ARG;
    const oc_fortran_src_t *f = (const oc_fortran_src_t *)src;
    rat_t v;
    lr_result_t r = lr_scaled_to_rat(f->value, f->scale, &v);
    if (!r.ok) return OC_ERR_CONV;
    return oc_ir_single_rat(out, v, f->scale);
}
/* Fortran back-end: reconstruct the exact scaled integer at the field's
 * scale. value = ir_value * 10^scale, which MUST be a whole number or the
 * field cannot hold it (refuse, never round). */
static int32_t fortran_lift(const oc_ir_t *ir, void *out, uint32_t cap) {
    if (cap < sizeof(oc_fortran_src_t)) return OC_ERR_CAP;
    rat_t v; uint32_t scale;
    int32_t rc = oc_ir_get_rat(ir, &v, &scale);
    if (rc != OC_OK) return rc;
    /* 10^scale must fit int64: refuse a scale the field cannot hold rather than
     * overflow and emit a wrong-but-plausible scaled integer (never fabricate). */
    if (scale > 18) return OC_ERR_CONV;
    rat_t scaled = rat_mul(v, rat_from_int(oc_pow10_i64(scale)));
    if (!scaled.valid || !rat_is_int(scaled)) return OC_ERR_CONV;
    oc_fortran_src_t *f = (oc_fortran_src_t *)out;
    f->value = scaled.num;                          /* den == 1 when integer */
    f->scale = scale;
    return (int32_t)sizeof(oc_fortran_src_t);
}

/* ================= built-in C adapter (binary integer) ============= */
static int32_t c_lower(const void *src, uint32_t len, oc_ir_t *out) {
    if (len < sizeof(oc_c_src_t)) return OC_ERR_ARG;
    const oc_c_src_t *c = (const oc_c_src_t *)src;
    rat_t v;
    lr_result_t r = lr_scaled_to_rat(c->value, 0, &v);
    if (!r.ok) return OC_ERR_CONV;
    return oc_ir_single_rat(out, v, 0);
}
static int32_t c_lift(const oc_ir_t *ir, void *out, uint32_t cap) {
    if (cap < sizeof(oc_c_src_t)) return OC_ERR_CAP;
    rat_t v; uint32_t scale;
    int32_t rc = oc_ir_get_rat(ir, &v, &scale);
    if (rc != OC_OK) return rc;
    if (!rat_is_int(v)) return OC_ERR_CONV;         /* C int can't hold a fraction */
    oc_c_src_t *c = (oc_c_src_t *)out;
    c->value = v.num;
    return (int32_t)sizeof(oc_c_src_t);
}

static const oc_lang_ops_t OC_COBOL_OPS   = { "COBOL/COMP-3",  cobol_lower,   cobol_lift   };
static const oc_lang_ops_t OC_FORTRAN_OPS = { "Fortran/fixed", fortran_lower, fortran_lift };
static const oc_lang_ops_t OC_C_OPS       = { "C/binary",      c_lower,       c_lift       };

void oc_register_builtins(void) {
    oc_register_lang(OC_LANG_COBOL,   &OC_COBOL_OPS);
    oc_register_lang(OC_LANG_FORTRAN, &OC_FORTRAN_OPS);
    oc_register_lang(OC_LANG_C,       &OC_C_OPS);
    /* Sutra and FUTURE stay UNBOUND on purpose: an unbound language must
     * fail closed, and Sutra demonstrates the ops-boundary registration. */
}
