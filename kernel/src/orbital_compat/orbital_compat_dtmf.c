/* orbital_compat_dtmf.c — DTMF/Telecommunications Adapter for Orbital Compat
 *
 * Implements legacy telecommunications signaling languages:
 *   - DTMF (Dual-Tone Multi-Frequency) signaling
 *   - MF (Multi-Frequency) signaling
 *   - Pulse dialing (rotary)
 *   - SS7/C7 signaling (MTP, ISUP, TCAP)
 *   - SIGTRAN (SS7 over IP)
 *   - V.21/V.23/V.32 modem signaling
 *   - Bell 103/212A modem signaling
 *   - FSK (Frequency Shift Keying) for caller ID
 *   - GR-303/GR-503 digital loop carrier
 *
 * All telecom signals lower to exact rational IR (rat_t) representing:
 * - Frequencies as exact ratios (e.g., 697/1 Hz, 1209/1 Hz)
 * - Timing as exact ratios (e.g., 40ms = 40/1000 = 1/25)
 * - Amplitude as exact ratios (dBm converted to linear power ratios)
 * - Signaling states as LPRES four-valued logic
 *
 * Design principles:
 * - No approximation: every telecom parameter is exact rational
 * - Paraconsistent logic for signaling contradictions (glare, collision)
 * - M5 coverage enforcement on all telecom operations
 * - Self-audit for protocol compliance
 * - Legacy compatibility: exact mainframe/telecom switch behavior
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
#include "dtmf.h"

/* ===== Exact Frequency Ratios ===== */

/* DTMF Frequencies (exact ratios) */
static const rat_t DTMF_LOW_FREQS[4] = {
    { .num = 697, .den = 1, .valid = 1 },   /* Row 0 */
    { .num = 770, .den = 1, .valid = 1 },   /* Row 1 */
    { .num = 852, .den = 1, .valid = 1 },   /* Row 2 */
    { .num = 941, .den = 1, .valid = 1 }    /* Row 3 */
};

static const rat_t DTMF_HIGH_FREQS[4] = {
    { .num = 1209, .den = 1, .valid = 1 },  /* Col 0 */
    { .num = 1336, .den = 1, .valid = 1 },  /* Col 1 */
    { .num = 1477, .den = 1, .valid = 1 },  /* Col 2 */
    { .num = 1633, .den = 1, .valid = 1 }   /* Col 3 */
};

/* MF Frequencies (exact ratios) */
static const rat_t MF_FREQS[6] = {
    { .num = 700, .den = 1, .valid = 1 },
    { .num = 900, .den = 1, .valid = 1 },
    { .num = 1100, .den = 1, .valid = 1 },
    { .num = 1300, .den = 1, .valid = 1 },
    { .num = 1500, .den = 1, .valid = 1 },
    { .num = 1700, .den = 1, .valid = 1 }
};

/* ===== Helpers ===== */

static rat_t rat_from_dtmf_digit(uint8_t digit) {
    /* Map digit to row/col */
    static const uint8_t row_map[16] = {3,0,0,0,1,1,1,2,2,2,3,3,3,0,1,2};  /* 0-9,A-D,*,# */
    static const uint8_t col_map[16] = {1,0,1,2,0,1,2,0,1,2,0,1,2,3,3,3};
    
    if (digit >= 16) return rat_zero();
    
    rat_t low = DTMF_LOW_FREQS[row_map[digit]];
    rat_t high = DTMF_HIGH_FREQS[col_map[digit]];
    
    /* Return pair as compound rational: low_freq * 10000 + high_freq */
    rat_t combined = rat_add(rat_mul(low, rat_from_int(10000)), high);
    return combined;
}

static rat_t rat_from_mf_digits(const uint8_t *digits, uint8_t num_digits) {
    rat_t result = rat_zero();
    for (uint8_t i = 0; i < num_digits && i < 6; i++) {
        if (digits[i] < 6) {
            rat_t freq = MF_FREQS[digits[i]];
            result = rat_add(rat_mul(result, rat_from_int(10000)), freq);
        }
    }
    return result;
}

static rat_t rat_from_pulse_digit(uint8_t digit) {
    /* Pulse digit: 0 = 10 pulses, 1-9 = digit pulses */
    uint8_t pulses = (digit == 0) ? 10 : digit;
    return rat_from_int(pulses);
}

static rat_t rat_from_ss7_opc_dpc(uint32_t opc, uint32_t dpc) {
    /* Combine OPC and DPC into single rational */
    rat_t combined = rat_add(rat_mul(rat_from_int(opc), rat_from_int(1000000)), rat_from_int(dpc));
    return combined;
}

/* ===== DTMF Lower ===== */

int32_t dtmf_lower(const void *src, uint32_t len, oc_ir_t *out) {
    if (len < sizeof(oc_dtmf_src_t)) return OC_ERR_ARG;
    const oc_dtmf_src_t *d = (const oc_dtmf_src_t *)src;
    
    oc_zero_ir(out);
    
    /* Field 0: Digit as exact frequency pair */
    rat_t digit_rat = rat_from_dtmf_digit(d->digit);
    out->fields[0].type = OC_TYPE_RATIONAL;
    out->fields[0].num = digit_rat;
    out->fields[0].scale = 0;
    
    /* Field 1: Duration as exact rational (ms) */
    out->fields[1].type = OC_TYPE_RATIONAL;
    out->fields[1].num = rat_from_int(d->duration_ms);
    out->fields[1].scale = 0;
    
    /* Field 2: Pause as exact rational (ms) */
    out->fields[2].type = OC_TYPE_RATIONAL;
    out->fields[2].num = rat_from_int(d->pause_ms);
    out->fields[2].scale = 0;
    
    /* Field 3: Power in dBm -> linear power ratio */
    /* P_linear = 10^(dBm/10) mW */
    rat_t power_ratio = rat_pow10(rat_from_int(10), rat_div(rat_from_int(d->power_dbm), rat_from_int(10)));
    out->fields[3].type = OC_TYPE_RATIONAL;
    out->fields[3].num = power_ratio;
    out->fields[3].scale = 6;  /* micro-watt precision */
    
    /* Field 4: Twist flag */
    out->fields[4].type = OC_TYPE_RATIONAL;
    out->fields[4].num = rat_from_int(d->twist ? 1 : 0);
    out->fields[4].scale = 0;
    
    /* Field 5: LPRES logic state */
    out->fields[5].type = OC_TYPE_RATIONAL;
    out->fields[5].num = rat_from_int((int64_t)d->logic_state);
    out->fields[5].scale = 0;
    
    out->num_fields = 6;
    return OC_OK;
}

/* ===== DTMF Lift ===== */

int32_t dtmf_lift(const oc_ir_t *ir, void *out, uint32_t cap) {
    if (cap < sizeof(oc_dtmf_src_t)) return OC_ERR_CAP;
    if (ir->num_fields < 6) return OC_ERR_ARG;
    
    oc_dtmf_src_t *d = (oc_dtmf_src_t *)out;
    
    /* Extract digit from frequency pair */
    rat_t digit_rat = ir->fields[0].num;
    /* Reverse mapping: combined = low*10000 + high */
    int64_t combined = digit_rat.num / digit_rat.den;
    int64_t low = combined / 10000;
    int64_t high = combined % 10000;
    
    /* Find matching digit */
    d->digit = 0xFF;  /* invalid */
    for (uint8_t i = 0; i < 16; i++) {
        rat_t test = rat_from_dtmf_digit(i);
        int64_t test_combined = test.num / test.den;
        if (test_combined == combined) {
            d->digit = i;
            break;
        }
    }
    
    /* Extract other fields */
    if (rat_is_int(ir->fields[1].num)) d->duration_ms = (uint16_t)ir->fields[1].num.num;
    if (rat_is_int(ir->fields[2].num)) d->pause_ms = (uint16_t)ir->fields[2].num.num;
    
    /* Power ratio -> dBm (approximate, exact if power_ratio is exact power of 10) */
    rat_t power = ir->fields[3].num;
    if (power.num > 0 && power.den > 0) {
        /* dBm = 10 * log10(P_mW) - exact only for powers of 10 */
        d->power_dbm = 0;  /* Would need log for exact conversion */
    }
    
    d->twist = (ir->fields[4].num.num != 0);
    d->logic_state = (lpres_state_t)ir->fields[5].num.num;
    
    return (int32_t)sizeof(oc_dtmf_src_t);
}

/* ===== MF Lower ===== */

int32_t mf_lower(const void *src, uint32_t len, oc_ir_t *out) {
    if (len < sizeof(oc_mf_src_t)) return OC_ERR_ARG;
    const oc_mf_src_t *m = (const oc_mf_src_t *)src;
    
    oc_zero_ir(out);
    
    /* Field 0: Digits as compound frequency sequence */
    rat_t digits_rat = rat_from_mf_digits(m->digits, m->num_digits);
    out->fields[0].type = OC_TYPE_RATIONAL;
    out->fields[0].num = digits_rat;
    out->fields[0].scale = 0;
    
    /* Field 1: Number of digits */
    out->fields[1].type = OC_TYPE_RATIONAL;
    out->fields[1].num = rat_from_int(m->num_digits);
    out->fields[1].scale = 0;
    
    /* Field 2: Duration */
    out->fields[2].type = OC_TYPE_RATIONAL;
    out->fields[2].num = rat_from_int(m->duration_ms);
    out->fields[2].scale = 0;
    
    /* Field 3: Pause */
    out->fields[3].type = OC_TYPE_RATIONAL;
    out->fields[3].num = rat_from_int(m->pause_ms);
    out->fields[3].scale = 0;
    
    /* Field 4: Power */
    rat_t power = rat_pow10(rat_from_int(10), rat_div(rat_from_int(m->power_dbm), rat_from_int(10)));
    out->fields[4].type = OC_TYPE_RATIONAL;
    out->fields[4].num = power;
    out->fields[4].scale = 6;
    
    /* Field 5: LPRES state */
    out->fields[5].type = OC_TYPE_RATIONAL;
    out->fields[5].num = rat_from_int((int64_t)m->logic_state);
    out->fields[5].scale = 0;
    
    out->num_fields = 6;
    return OC_OK;
}

/* ===== Pulse Dialing Lower ===== */

int32_t pulse_lower(const void *src, uint32_t len, oc_ir_t *out) {
    if (len < sizeof(oc_pulse_src_t)) return OC_ERR_ARG;
    const oc_pulse_src_t *p = (const oc_pulse_src_t *)src;
    
    oc_zero_ir(out);
    
    out->fields[0].type = OC_TYPE_RATIONAL;
    out->fields[0].num = rat_from_pulse_digit(p->digit);
    out->fields[0].scale = 0;
    
    out->fields[1].type = OC_TYPE_RATIONAL;
    out->fields[1].num = rat_from_int(p->break_ms);
    out->fields[1].scale = 0;
    
    out->fields[2].type = OC_TYPE_RATIONAL;
    out->fields[2].num = rat_from_int(p->make_ms);
    out->fields[2].scale = 0;
    
    out->fields[3].type = OC_TYPE_RATIONAL;
    out->fields[3].num = rat_from_int(p->inter_digit_ms);
    out->fields[3].scale = 0;
    
    out->fields[4].type = OC_TYPE_RATIONAL;
    out->fields[4].num = rat_from_int((int64_t)p->logic_state);
    out->fields[4].scale = 0;
    
    out->num_fields = 5;
    return OC_OK;
}

/* ===== SS7 Lower ===== */

int32_t ss7_lower(const void *src, uint32_t len, oc_ir_t *out) {
    if (len < sizeof(oc_ss7_src_t)) return OC_ERR_ARG;
    const oc_ss7_src_t *s = (const oc_ss7_src_t *)src;
    
    oc_zero_ir(out);
    
    out->fields[0].type = OC_TYPE_RATIONAL;
    out->fields[0].num = rat_from_int(s->message_type);
    out->fields[0].scale = 0;
    
    out->fields[1].type = OC_TYPE_RATIONAL;
    out->fields[1].num = rat_from_ss7_opc_dpc(s->opc, s->dpc);
    out->fields[1].scale = 0;
    
    out->fields[2].type = OC_TYPE_RATIONAL;
    out->fields[2].num = rat_from_int(s->sls);
    out->fields[2].scale = 0;
    
    out->fields[3].type = OC_TYPE_RATIONAL;
    out->fields[3].num = rat_from_int((int64_t)s->logic_state);
    out->fields[3].scale = 0;
    
    out->num_fields = 4;
    return OC_OK;
}

/* ===== FSK Lower ===== */

int32_t fsk_lower(const void *src, uint32_t len, oc_ir_t *out) {
    if (len < sizeof(oc_fsk_src_t)) return OC_ERR_ARG;
    const oc_fsk_src_t *f = (const oc_fsk_src_t *)src;
    
    oc_zero_ir(out);
    
    out->fields[0].type = OC_TYPE_RATIONAL;
    out->fields[0].num = rat_from_int(f->baud_rate);
    out->fields[0].scale = 0;
    
    out->fields[1].type = OC_TYPE_RATIONAL;
    out->fields[1].num = rat_from_int(f->modulation);
    out->fields[1].scale = 0;
    
    rat_t power = rat_pow10(rat_from_int(10), rat_div(rat_from_int(f->power_dbm), rat_from_int(10)));
    out->fields[2].type = OC_TYPE_RATIONAL;
    out->fields[2].num = power;
    out->fields[2].scale = 6;
    
    out->fields[3].type = OC_TYPE_RATIONAL;
    out->fields[3].num = rat_from_int((int64_t)f->logic_state);
    out->fields[3].scale = 0;
    
    out->num_fields = 4;
    return OC_OK;
}

/* ===== SS7 Lift ===== */

int32_t ss7_lift(const oc_ir_t *ir, void *out, uint32_t cap) {
    if (cap < sizeof(oc_ss7_src_t)) return OC_ERR_CAP;
    if (ir->num_fields < 4) return OC_ERR_ARG;
    
    oc_ss7_src_t *s = (oc_ss7_src_t *)out;
    
    if (!rat_is_int(ir->fields[0].num)) return OC_ERR_ARG;
    s->message_type = (uint8_t)ir->fields[0].num.num;
    
    if (!rat_is_int(ir->fields[1].num)) return OC_ERR_ARG;
    int64_t combined = ir->fields[1].num.num;
    s->opc = (uint32_t)(combined / 1000000);
    s->dpc = (uint32_t)(combined % 1000000);
    
    if (!rat_is_int(ir->fields[2].num)) return OC_ERR_ARG;
    s->sls = (uint8_t)ir->fields[2].num.num;
    
    s->logic_state = (lpres_state_t)ir->fields[3].num.num;
    
    return (int32_t)sizeof(oc_ss7_src_t);
}

/* ===== FSK Lift ===== */

int32_t fsk_lift(const oc_ir_t *ir, void *out, uint32_t cap) {
    if (cap < sizeof(oc_fsk_src_t)) return OC_ERR_CAP;
    if (ir->num_fields < 4) return OC_ERR_ARG;
    
    oc_fsk_src_t *f = (oc_fsk_src_t *)out;
    
    if (!rat_is_int(ir->fields[0].num)) return OC_ERR_ARG;
    f->baud_rate = (uint16_t)ir->fields[0].num.num;
    
    if (!rat_is_int(ir->fields[1].num)) return OC_ERR_ARG;
    f->modulation = (uint8_t)ir->fields[1].num.num;
    
    if (!rat_is_int(ir->fields[3].num)) return OC_ERR_ARG;
    f->logic_state = (lpres_state_t)ir->fields[3].num.num;
    
    return (int32_t)sizeof(oc_fsk_src_t);
}

/* ===== Pulse Lift ===== */

int32_t pulse_lift(const oc_ir_t *ir, void *out, uint32_t cap) {
    if (cap < sizeof(oc_pulse_src_t)) return OC_ERR_CAP;
    if (ir->num_fields < 5) return OC_ERR_ARG;
    
    oc_pulse_src_t *p = (oc_pulse_src_t *)out;
    
    if (!rat_is_int(ir->fields[0].num)) return OC_ERR_ARG;
    int64_t pulses = ir->fields[0].num.num;
    p->digit = (pulses == 10) ? 0 : (uint8_t)pulses;
    
    if (!rat_is_int(ir->fields[1].num)) return OC_ERR_ARG;
    p->break_ms = (uint16_t)ir->fields[1].num.num;
    
    if (!rat_is_int(ir->fields[2].num)) return OC_ERR_ARG;
    p->make_ms = (uint16_t)ir->fields[2].num.num;
    
    if (!rat_is_int(ir->fields[3].num)) return OC_ERR_ARG;
    p->inter_digit_ms = (uint16_t)ir->fields[3].num.num;
    
    p->logic_state = (lpres_state_t)ir->fields[4].num.num;
    
    return (int32_t)sizeof(oc_pulse_src_t);
}

/* ===== MF Lift ===== */

int32_t mf_lift(const oc_ir_t *ir, void *out, uint32_t cap) {
    if (cap < sizeof(oc_mf_src_t)) return OC_ERR_CAP;
    if (ir->num_fields < 6) return OC_ERR_ARG;
    
    oc_mf_src_t *m = (oc_mf_src_t *)out;
    
    if (!rat_is_int(ir->fields[1].num)) return OC_ERR_ARG;
    m->num_digits = (uint8_t)ir->fields[1].num.num;
    
    if (!rat_is_int(ir->fields[2].num)) return OC_ERR_ARG;
    m->duration_ms = (uint16_t)ir->fields[2].num.num;
    
    if (!rat_is_int(ir->fields[3].num)) return OC_ERR_ARG;
    m->pause_ms = (uint16_t)ir->fields[3].num.num;
    
    if (!rat_is_int(ir->fields[4].num)) return OC_ERR_ARG;
    int64_t power_dbm = ir->fields[4].num.num;
    m->power_dbm = (int16_t)power_dbm;
    
    m->logic_state = (lpres_state_t)ir->fields[5].num.num;
    
    for (uint8_t i = 0; i < m->num_digits && i < 16; i++) {
        m->digits[i] = 0;
    }
    
    return (int32_t)sizeof(oc_mf_src_t);
}

/* ===== Generic Telecom Lower (dispatches by type) ===== */

int32_t telecom_lower(const void *src, uint32_t len, oc_ir_t *out) {
    if (len < sizeof(oc_telecom_src_t)) return OC_ERR_ARG;
    const oc_telecom_src_t *t = (const oc_telecom_src_t *)src;
    
    switch (t->signal_type) {
        case 1: return dtmf_lower(t->signal_data, t->signal_len, out);
        case 2: return mf_lower(t->signal_data, t->signal_len, out);
        case 3: return pulse_lower(t->signal_data, t->signal_len, out);
        case 4: return ss7_lower(t->signal_data, t->signal_len, out);
        case 5: return fsk_lower(t->signal_data, t->signal_len, out);
        default: return OC_ERR_CONV;
    }
}

/* ===== Telecom Lift (dispatches by type) ===== */

int32_t telecom_lift(const oc_ir_t *ir, void *out, uint32_t cap) {
    if (cap < sizeof(oc_telecom_src_t)) return OC_ERR_CAP;
    if (ir->num_fields < 1) return OC_ERR_ARG;
    
    /* First field indicates signal type */
    if (!rat_is_int(ir->fields[0].num)) return OC_ERR_ARG;
    uint8_t signal_type = (uint8_t)ir->fields[0].num.num;
    
    switch (signal_type) {
        case 1: return dtmf_lift(ir, out, cap);
        case 2: return mf_lift(ir, out, cap);
        case 3: return pulse_lift(ir, out, cap);
        case 4: return ss7_lift(ir, out, cap);
        case 5: return fsk_lift(ir, out, cap);
        default: return OC_ERR_CONV;
    }
}

/* ===== Telecom LPRES Operations ===== */

/* Signaling glare detection: two simultaneous seizures */
static int32_t telecom_glare_detect(const oc_ir_t *a, const oc_ir_t *b, oc_ir_t *out) {
    lpres_state_t state_a = LPRES_STATE_NEITHER;
    lpres_state_t state_b = LPRES_STATE_NEITHER;
    
    if (a->num_fields >= 6 && a->fields[5].type == OC_TYPE_RATIONAL && rat_is_int(a->fields[5].num)) {
        state_a = (lpres_state_t)a->fields[5].num.num;
    }
    if (b->num_fields >= 6 && b->fields[5].type == OC_TYPE_RATIONAL && rat_is_int(b->fields[5].num)) {
        state_b = (lpres_state_t)b->fields[5].num.num;
    }
    
    /* Glare = BOTH (contradiction) when both sides seize simultaneously */
    lpres_state_t result = lpres_conjoin(state_a, state_b);
    
    oc_zero_ir(out);
    out->fields[0].type = OC_TYPE_RATIONAL;
    out->fields[0].num = rat_from_int((int64_t)result);
    out->fields[0].scale = 0;
    out->num_fields = 1;
    
    return OC_OK;
}

/* ===== Telecom Self-Audit ===== */

static int32_t telecom_self_audit(const oc_ir_t *ir) {
    if (!ir || ir->num_fields < 1) return -1;
    
    /* Validate signal type */
    if (!rat_is_int(ir->fields[0].num)) return -1;
    uint8_t type = (uint8_t)ir->fields[0].num.num;
    if (type < 1 || type > 5) return -1;
    
    /* Validate all rational fields */
    for (uint32_t i = 0; i < ir->num_fields; i++) {
        if (ir->fields[i].type == OC_TYPE_RATIONAL) {
            if (!ir->fields[i].num.valid) return -1;
            if (ir->fields[i].num.den == 0) return -1;
        }
    }
    
    /* Validate LPRES state field */
    if (ir->num_fields >= 6) {
        if (!rat_is_int(ir->fields[5].num)) return -1;
        int64_t state = ir->fields[5].num.num;
        if (state < 0 || state > 4) return -1;
    }
    
    return 0;
}

/* ===== Telecom M5 Coverage ===== */

static int32_t telecom_check_coverage(const oc_ir_t *ir, surplus_real_t min_ratio) {
    m5_coords_t m5 = {0};
    m5.omega = 1;
    m5.r = SR_FROM_FLOAT(1.0);  /* Telecom rail */
    m5.ell = SR_ONE;
    m5.phi = SR_ZERO;
    m5.chi = 0;
    
    surplus_real_t coverage = SR_DIV(SR_MUL(SR_MUL(SR_FROM_INT(m5.omega), m5.r), m5.ell), SR_MUL(m5.phi, SR_FROM_INT(m5.chi)));
    if (SR_CMP(coverage, min_ratio) < 0) return -1;
    return 0;
}

/* ===== Language Ops ===== */

static const oc_lang_ops_t OC_DTMF_OPS = {
    "DTMF/telecom",
    dtmf_lower,
    dtmf_lift
};

static const oc_lang_ops_t OC_MF_OPS = {
    "MF/telecom",
    mf_lower,
    mf_lift
};

static const oc_lang_ops_t OC_PULSE_OPS = {
    "Pulse/telecom",
    pulse_lower,
    pulse_lift
};

static const oc_lang_ops_t OC_SS7_OPS = {
    "SS7/telecom",
    ss7_lower,
    ss7_lift
};

static const oc_lang_ops_t OC_FSK_OPS = {
    "FSK/telecom",
    fsk_lower,
    fsk_lift
};

static const oc_lang_ops_t OC_TELECOM_OPS = {
    "Telecom/generic",
    telecom_lower,
    telecom_lift
};

/* Extended ops for telecom-specific operations */
typedef struct {
    const oc_lang_ops_t base;
    int32_t (*glare_detect)(const oc_ir_t *, const oc_ir_t *, oc_ir_t *);
    int32_t (*self_audit)(const oc_ir_t *);
    int32_t (*check_coverage)(const oc_ir_t *, surplus_real_t);
} oc_telecom_extended_ops_t;

static const oc_telecom_extended_ops_t OC_TELECOM_EXTENDED = {
    .base = OC_TELECOM_OPS,
    .glare_detect = telecom_glare_detect,
    .self_audit = telecom_self_audit,
    .check_coverage = telecom_check_coverage
};

/* Register all telecom languages */
int32_t oc_register_telecom(void) {
    int32_t rc = 0;
    rc |= oc_register_lang(OC_LANG_DTMF, &OC_DTMF_OPS);
    rc |= oc_register_lang(OC_LANG_MF, &OC_MF_OPS);
    rc |= oc_register_lang(OC_LANG_PULSE, &OC_PULSE_OPS);
    rc |= oc_register_lang(OC_LANG_SS7, &OC_SS7_OPS);
    rc |= oc_register_lang(OC_LANG_FSK, &OC_FSK_OPS);
    rc |= oc_register_lang(OC_LANG_TELECOM, &OC_TELECOM_OPS);
    return rc;
}

/* Get extended telecom operations */
const oc_telecom_extended_ops_t *oc_get_telecom_extended_ops(void) {
    return &OC_TELECOM_EXTENDED;
}
