/* orbital_compat_asm.c — Assembly Language Adapter for Orbital Compat
 *
 * Assembly language adapter for architecture-specific machine code.
 * Lowers raw machine bytes to exact rational IR representing:
 * - Instruction opcodes as exact integers
 * - Register operands as exact integers
 * - Immediate values as exact rationals
 * - Memory addresses as exact rationals
 *
 * Design principles:
 * - No interpretation: raw bytes preserved as exact rationals
 * - Architecture-agnostic IR: same instruction = same rational
 * - Paraconsistent logic for undefined/illegal instructions
 * - M5 coverage enforcement on all assembly operations
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

/* ===== Helpers ===== */

static rat_t rat_from_asm_instruction(const uint8_t *bytes, uint32_t len, uint8_t arch) {
    /* Encode instruction as exact rational: arch * 2^32 + bytes as integer */
    rat_t r = rat_from_int((int64_t)arch);
    r = rat_mul(r, rat_from_int(1LL << 32));
    
    for (uint32_t i = 0; i < len && i < 8; i++) {
        r = rat_add(r, rat_from_int(bytes[i]));
        if (i < 7) r = rat_mul(r, rat_from_int(256));
    }
    return r;
}

/* ===== Assembly Lower ===== */

int32_t asm_lower(const void *src, uint32_t len, oc_ir_t *out) {
    if (len < sizeof(oc_asm_src_t)) return OC_ERR_ARG;
    const oc_asm_src_t *a = (const oc_asm_src_t *)src;
    
    if (a->len == 0 || a->len > 64) return OC_ERR_CONV;
    
    oc_zero_ir(out);
    
    /* Field 0: Architecture */
    out->fields[0].type = OC_TYPE_RATIONAL;
    out->fields[0].num = rat_from_int(a->arch);
    out->fields[0].scale = 0;
    
    /* Field 1: Instruction bytes as exact rational */
    rat_t instr = rat_from_asm_instruction(a->bytes, a->len, a->arch);
    out->fields[1].type = OC_TYPE_RATIONAL;
    out->fields[1].num = instr;
    out->fields[1].scale = 0;
    
    /* Field 2: Length */
    out->fields[2].type = OC_TYPE_RATIONAL;
    out->fields[2].num = rat_from_int(a->len);
    out->fields[2].scale = 0;
    
    /* Field 3: Offset */
    out->fields[3].type = OC_TYPE_RATIONAL;
    out->fields[3].num = rat_from_int(a->offset);
    out->fields[3].scale = 0;
    
    out->num_fields = 4;
    return OC_OK;
}

/* ===== Assembly Lift ===== */

int32_t asm_lift(const oc_ir_t *ir, void *out, uint32_t cap) {
    if (cap < sizeof(oc_asm_src_t)) return OC_ERR_CAP;
    if (ir->num_fields < 4) return OC_ERR_ARG;
    
    oc_asm_src_t *a = (oc_asm_src_t *)out;
    
    if (!rat_is_int(ir->fields[0].num)) return OC_ERR_ARG;
    a->arch = (uint8_t)ir->fields[0].num.num;
    
    if (!rat_is_int(ir->fields[2].num)) return OC_ERR_ARG;
    a->len = (uint32_t)ir->fields[2].num.num;
    
    if (!rat_is_int(ir->fields[3].num)) return OC_ERR_ARG;
    a->offset = (uint32_t)ir->fields[3].num.num;
    
    /* Reconstruct bytes from instruction rational */
    rat_t instr = ir->fields[1].num;
    for (uint32_t i = 0; i < a->len && i < 8; i++) {
        if (!rat_is_int(instr)) return OC_ERR_CONV;
        a->bytes[i] = (uint8_t)(instr.num & 0xFF);
        instr = rat_div(instr, rat_from_int(256));
    }
    
    return (int32_t)sizeof(oc_asm_src_t);
}

/* ===== Assembly Self-Audit ===== */

int32_t asm_self_audit(const oc_ir_t *ir) {
    if (!ir || ir->num_fields < 4) return -1;
    
    for (uint32_t i = 0; i < ir->num_fields; i++) {
        if (ir->fields[i].type == OC_TYPE_RATIONAL) {
            if (!ir->fields[i].num.valid) return -1;
            if (ir->fields[i].num.den == 0) return -1;
        }
    }
    
    if (!rat_is_int(ir->fields[0].num)) return -1;
    uint8_t arch = (uint8_t)ir->fields[0].num.num;
    if (arch > 3) return -1;  /* Invalid architecture */
    
    return 0;
}

/* ===== Assembly M5 Coverage ===== */

static int32_t asm_check_coverage(const oc_ir_t *ir, surplus_real_t min_ratio) {
    m5_coords_t m5 = {0};
    m5.omega = 1;
    m5.r = SR_FROM_FLOAT(0.5);  /* Assembly rail - lowest level */
    m5.ell = SR_ONE;
    m5.phi = SR_ZERO;
    m5.chi = 0;
    
    surplus_real_t coverage = SR_DIV(SR_MUL(SR_MUL(SR_FROM_INT(m5.omega), m5.r), m5.ell), SR_MUL(m5.phi, SR_FROM_INT(m5.chi)));
    if (SR_CMP(coverage, min_ratio) < 0) return -1;
    return 0;
}
