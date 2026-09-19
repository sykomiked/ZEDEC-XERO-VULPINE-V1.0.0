/* sdk_bridge_lang.h — SDK Bridge Language Bindings
 *
 * Exposes kernel capabilities to all 15 Orbital Compat languages
 * through the canonical IR. Each language calls these functions
 * via oc_lower/oc_lift with the appropriate source/target types.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef SDK_BRIDGE_LANG_H
#define SDK_BRIDGE_LANG_H

#include <stdint.h>
#include <stdbool.h>
#include "orbital_compat.h"
#include "financial_fabric.h"
#include "surplus.h"
#include "lpres.h"
#include "m5_types.h"
#include "sdk_bridge.h"

/* ============================================================================
 * CAPABILITY OPERATION CODES (passed as first IR field)
 * ============================================================================ */

typedef enum {
    /* Financial Fabric Operations */
    SB_OP_FF_CREATE_ACCOUNT      = 0x1000,
    SB_OP_FF_CREATE_DERIVATIVE   = 0x1001,
    SB_OP_FF_SETTLE_DERIVATIVE   = 0x1002,
    SB_OP_FF_TEMPORAL_ARB        = 0x1003,
    SB_OP_FF_CREATE_ASSURANCE    = 0x1004,
    SB_OP_FF_VERIFY_GENERATION   = 0x1005,
    SB_OP_FF_FORWARD_CAPITAL     = 0x1006,
    SB_OP_FF_TOKENIZE_TREATY     = 0x1007,
    SB_OP_FF_VERIFY_TREATY       = 0x1008,
    SB_OP_FF_MESH_SETTLE         = 0x1009,
    SB_OP_FF_POST_QUOTE          = 0x100A,
    SB_OP_FF_VALUE_POSITION      = 0x100B,
    SB_OP_FF_OPEN_POSITION       = 0x100C,
    SB_OP_FF_SETTLE              = 0x100D,
    SB_OP_FF_PNL_REPORT          = 0x100E,
    
    /* Crypto Wallet Operations */
    SB_OP_CW_CREATE_WALLET       = 0x2000,
    SB_OP_CW_DERIVE_KEY          = 0x2001,
    SB_OP_CW_STORE_FILE          = 0x2002,
    SB_OP_CW_LOAD_FILE           = 0x2003,
    SB_OP_CW_PHASE_SHIFT         = 0x2004,
    SB_OP_CW_VERIFY_INTEGRITY    = 0x2005,
    
    /* Mesh Network Operations */
    SB_OP_MN_CREATE_NETWORK      = 0x3000,
    SB_OP_MN_JOIN_NETWORK        = 0x3001,
    SB_OP_MN_CREATE_ROUTE        = 0x3002,
    SB_OP_MN_SEND_MESSAGE        = 0x3003,
    SB_OP_MN_FEDERATE            = 0x3004,
    
    /* Identity Operations */
    SB_OP_ID_REGISTER            = 0x4000,
    SB_OP_ID_VERIFY              = 0x4001,
    SB_OP_ID_ATTEST              = 0x4002,
    
    /* Capital/Rails Operations */
    SB_OP_RAILS_ISSUE_CARD       = 0x5000,
    SB_OP_RAILS_PROCESS_TX       = 0x5001,
    SB_OP_RAILS_SETTLE           = 0x5002,
    
    /* Crypto Bridge Operations */
    SB_OP_BRIDGE_CREATE          = 0x6000,
    SB_OP_BRIDGE_EXECUTE         = 0x6001,
    SB_OP_BRIDGE_TOKENIZE        = 0x6002,
    SB_OP_BRIDGE_REDEEM          = 0x6003,
    
    /* Vino/Vena Operations */
    SB_OP_VINO_CREATE_ACCOUNT    = 0x7000,
    SB_OP_VINO_TRANSFER          = 0x7001,
    SB_OP_VENA_DEPLOY            = 0x7002,
    SB_OP_VENA_EXECUTE           = 0x7003,
    
    /* Self-Audit/Heal Operations */
    SB_OP_SELF_AUDIT             = 0xF000,
    SB_OP_SELF_HEAL              = 0xF001,
    SB_OP_GET_STATS              = 0xF002,
} sb_lang_op_t;

/* ============================================================================
 * RESULT CODES (returned in IR field 1)
 * ============================================================================ */

typedef enum {
    SB_LANG_OK           = 0,
    SB_LANG_ERR_CAP      = -1,  /* Capability not granted */
    SB_LANG_ERR_COVERAGE = -2,  /* M5 coverage below threshold */
    SB_LANG_ERR_AUDIT    = -3,  /* Self-audit failed */
    SB_LANG_ERR_ARG      = -4,  /* Invalid arguments */
    SB_LANG_ERR_FINANCIAL = -5, /* Financial operation failed */
    SB_LANG_ERR_CRYPTO   = -6,  /* Crypto operation failed */
    SB_LANG_ERR_MESH     = -7,  /* Mesh operation failed */
    SB_LANG_ERR_IDENTITY = -8,  /* Identity operation failed */
    SB_LANG_ERR_INTERNAL = -9,  /* Internal error */
} sb_lang_result_t;

/* ============================================================================
 * LANGUAGE CONTEXT (per-process, created via SDK Bridge)
 * ============================================================================ */

typedef struct sb_lang_context {
    uint32_t process_id;
    word168_t identity;
    sb_capability_t capabilities[SB_MAX_CAPABILITIES];
    uint32_t num_capabilities;
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    lpres_state_t attestation;
    uint32_t financial_account_id;
    bool active;
} sb_lang_context_t;

/* ============================================================================
 * API: Language-agnostic capability execution
 * ============================================================================ */

/* Execute a capability operation from any language.
 * 
 * Input IR format:
 *   fields[0]: operation code (sb_lang_op_t)
 *   fields[1..N]: operation-specific arguments as exact rationals
 * 
 * Output IR format:
 *   fields[0]: result code (sb_lang_result_t)
 *   fields[1]: operation-specific return value
 *   fields[2]: LPRES attestation state
 *   fields[3]: M5 coverage ratio at execution time
 *   fields[4]: operation ID for tracing
 */
int32_t sb_lang_execute(sb_lang_context_t *ctx,
                        const oc_ir_t *input,
                        oc_ir_t *output);

/* Create a language context for a process identity */
int32_t sb_lang_create_context(sdk_bridge_t *bridge,
                               const word168_t *identity,
                               sb_lang_context_t *out_ctx);

/* Destroy a language context */
void sb_lang_destroy_context(sb_lang_context_t *ctx);

/* Check if context has a capability */
bool sb_lang_has_capability(const sb_lang_context_t *ctx, sb_capability_t cap);

/* Grant capability to context */
int32_t sb_lang_grant_capability(sdk_bridge_t *bridge,
                                 sb_lang_context_t *ctx,
                                 sb_capability_t cap);

/* Run self-audit on context */
int32_t sb_lang_self_audit(sdk_bridge_t *bridge,
                           sb_lang_context_t *ctx,
                           oc_ir_t *out_report);

/* Run self-heal on context */
int32_t sb_lang_self_heal(sdk_bridge_t *bridge,
                          sb_lang_context_t *ctx,
                          oc_ir_t *out_report);

/* ============================================================================
 * HELPER: Build operation IR from rational arguments
 * ============================================================================ */

static inline int32_t sb_lang_build_op(oc_ir_t *ir, sb_lang_op_t op, 
                                        const rat_t *args, uint32_t num_args) {
    if (!ir || num_args > OC_MAX_FIELDS - 1) return OC_ERR_ARG;
    oc_zero_ir(ir);
    ir->num_fields = 1 + num_args;
    ir->fields[0].type = OC_TYPE_RATIONAL;
    ir->fields[0].num = rat_from_int((int64_t)op);
    ir->fields[0].scale = 0;
    for (uint32_t i = 0; i < num_args; i++) {
        ir->fields[i + 1].type = OC_TYPE_RATIONAL;
        ir->fields[i + 1].num = args[i];
        ir->fields[i + 1].scale = 0;
    }
    return OC_OK;
}

/* Convert rat_t to surplus_real_t (for coverage ratios) */
static inline surplus_real_t rat_to_surplus(rat_t r) {
    if (!r.valid || r.den == 0) return SR_ZERO;
    int64_t whole = r.num / r.den;
    int64_t frac_num = r.num % r.den;
    if (frac_num < 0) frac_num = -frac_num;
    int64_t frac = (frac_num * 4294967296LL) / r.den;
    if (r.num < 0) frac = -frac;
    return (surplus_real_t)((whole << 32) | (frac & 0xFFFFFFFF));
}

/* Convert surplus_real_t to rat_t */
static inline rat_t surplus_to_rat(surplus_real_t sr) {
    rat_t r = {0};
    int64_t whole = sr >> 32;
    int64_t frac = sr & 0xFFFFFFFF;
    if (frac < 0) frac = -frac;
    r.num = whole * 4294967296LL + frac;
    r.den = 4294967296LL;
    r.valid = 1;
    return r;
}

/* ============================================================================
 * HELPER: Parse result IR
 * ============================================================================ */

static inline sb_lang_result_t sb_lang_parse_result(const oc_ir_t *ir,
                                                     rat_t *out_value,
                                                     lpres_state_t *out_attestation,
                                                     surplus_real_t *out_coverage,
                                                     uint32_t *out_op_id) {
    if (!ir || ir->num_fields < 5) return SB_LANG_ERR_ARG;
    if (ir->fields[0].type != OC_TYPE_RATIONAL) return SB_LANG_ERR_ARG;
    if (!rat_is_int(ir->fields[0].num)) return SB_LANG_ERR_ARG;
    
    sb_lang_result_t result = (sb_lang_result_t)ir->fields[0].num.num;
    if (out_value) *out_value = ir->fields[1].num;
    if (out_attestation) {
        if (ir->fields[2].type == OC_TYPE_RATIONAL && rat_is_int(ir->fields[2].num)) {
            *out_attestation = (lpres_state_t)ir->fields[2].num.num;
        } else {
            *out_attestation = LPRES_STATE_NEITHER;
        }
    }
    if (out_coverage) {
        if (ir->fields[3].type == OC_TYPE_RATIONAL) {
            *out_coverage = rat_to_surplus(ir->fields[3].num);
        } else {
            *out_coverage = SR_ZERO;
        }
    }
    if (out_op_id) {
        if (ir->fields[4].type == OC_TYPE_RATIONAL && rat_is_int(ir->fields[4].num)) {
            *out_op_id = (uint32_t)ir->fields[4].num.num;
        } else {
            *out_op_id = 0;
        }
    }
    return result;
}

#endif /* SDK_BRIDGE_LANG_H */
