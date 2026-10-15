/* dlp_projector.c — DLP Pico Projector Hardware-as-Code Module
 *
 * Implementation. See dlp_projector.h for the physics grounding of
 * every computation here (photometry, time-of-flight, wobulation
 * timing feasibility, keystone geometry, complementary-color dither).
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "dlp_projector.h"

/* ===== chipset profiles =====
 * Sources (TI public documents, read 2026-10):
 *   DLP2010   ti.com/product/DLP2010; data sheet DLPS123C (Jul 2023).
 *   DLPC3430 / DLPC3435  ti.com/product/DLPC3430, /DLPC3435.
 *   DLP230NP  ti.com/product/DLP230NP; data sheet DLPS144G (Oct 2024).
 *   DLPC3436  ti.com/product/DLPC3436.
 * Not verified, so not modelled: controller and PMIC power (no figure on
 * the product pages; system power is dominated by the illumination,
 * which belongs to the optical module, not the chipset), and whether the
 * DLP230NP optical engine moves an actuator or relies on mirror switching
 * alone for its four pixels per mirror (DLPS144G says "fast switching
 * speed ... combined with advanced DLP image processing"). */
static const dlp_profile_t PROFILES[DLP_PROFILE_COUNT] = {
    {
        .id = DLP_PROFILE_DLP2010,
        .dmd = "DLP2010",
        .controller = "DLPC3430/DLPC3435",
        .pmic = "DLPA200x/DLPA3000",
        .native_width = DLP_NATIVE_WIDTH,   /* 854 columns, orthogonal (DLPS123C 1) */
        .native_height = DLP_NATIVE_HEIGHT, /* 480 rows */
        .screen_width = DLP_NATIVE_WIDTH,   /* WVGA shown as is */
        .screen_height = DLP_NATIVE_HEIGHT,
        .shift_x = 1,
        .shift_y = 1,
        .mirror_pitch_um_x100 = DLP_MIRROR_PITCH_UM_X100, /* 5.4 um */
        .mirror_tilt_deg_x10 = DLP_MIRROR_TILT_DEG_X10,   /* +/-17 deg */
        .diagonal_mm_x100 = 529,                          /* 0.2 in = 5.29 mm */
        .max_input_hz = DLP_MAX_INPUT_HZ,                 /* 120 Hz (product page) */
        .controller_max_hz = 240,                         /* DLPC3430/3435 page */
        .dmd_sublvds_bits = 4,                            /* 4-bit SubLVDS */
        .dmd_dclk_max_mhz = 600,                          /* DLPS123C rec. DCLK max */
        /* MIPI DSI is on the DLPC3430 only; the common set is listed. */
        .host_if = DLP_HOST_PARALLEL24 | DLP_HOST_BT656,
        .host_pclk_max_mhz = 155,
        .dmd_power_typ_uw = 90800,  /* PTOTAL typ 90.8 mW (DLPS123C) */
        .dmd_power_max_uw = 140300, /* PTOTAL max 140.3 mW */
        .system_lumens_max = 200,   /* product page */
    },
    {
        .id = DLP_PROFILE_DLP230NP,
        .dmd = "DLP230NP",
        .controller = "DLPC3436",
        .pmic = "DLPA2000/DLPA2005/DLPA3000/DLPA3005",
        .native_width = 960,  /* 960 columns, square arrangement (DLPS144G 5.x, 6.1) */
        .native_height = 540, /* 540 rows */
        .screen_width = 1920, /* "Displays 1920 x 1080 pixels on the screen" */
        .screen_height = 1080,
        .shift_x = 2, /* four distinct pixels per mirror per frame */
        .shift_y = 2,
        .mirror_pitch_um_x100 = 540, /* 5.4 um */
        .mirror_tilt_deg_x10 = 170,  /* 17 deg */
        .diagonal_mm_x100 = 595,     /* 0.23 in = 5.95 mm */
        .max_input_hz = 60,          /* product page; DLPC3436: 60 Hz at 1080p */
        .controller_max_hz = 240,    /* DLPC3436 page */
        .dmd_sublvds_bits = 8,       /* 8-bit SubLVDS */
        .dmd_dclk_max_mhz = 540,     /* DLPS144G rec. DCLK 300..540 MHz */
        /* DLPC3436 page lists a 24-bit parallel interface and "selectable
         * input pixel data formats"; BT656 and MIPI DSI are not listed. */
        .host_if = DLP_HOST_PARALLEL24,
        .host_pclk_max_mhz = 155,
        .dmd_power_typ_uw = 151220, /* PTOTAL typ 151.22 mW (DLPS144G 5.6) */
        .dmd_power_max_uw = 190300, /* PTOTAL max 190.3 mW */
        /* 450 lm is TI's LED chipset figure; the bay spec asks 500-800 lm
         * from a laser-phosphor engine, which TI does not rate here. */
        .system_lumens_max = 450,
    },
};

const dlp_profile_t *dlp_profile_get(uint32_t id)
{
    return id < DLP_PROFILE_COUNT ? &PROFILES[id] : 0;
}

static surplus_real_t tilt_deg_of(const dlp_profile_t *pr)
{
    return SR_DIV(SR_FROM_INT((int64_t) pr->mirror_tilt_deg_x10), SR_FROM_INT(10));
}

void dlp_projector_init(dlp_projector_t *p, uint32_t device_id, const char *name) {
    (void) dlp_projector_init_profile(p, device_id, name, DLP_PROFILE_DLP2010);
}

bool dlp_projector_init_profile(dlp_projector_t *p, uint32_t device_id, const char *name,
                                uint32_t profile)
{
    const dlp_profile_t *pr = dlp_profile_get(profile);
    if (!p || !pr) return false;
    p->device_id = device_id;
    p->profile = profile;
    int i;
    for (i = 0; i < DLP_MAX_LABEL_LEN - 1 && name && name[i]; i++) p->name[i] = name[i];
    p->name[i] = '\0';

    p->native_width = pr->native_width;
    p->native_height = pr->native_height;
    p->mirror_tilt_deg = tilt_deg_of(pr);
    p->max_input_hz = pr->max_input_hz;

    /* The chipset's own sub-frame shift (1x1 = none); set_wobulation()
     * may change it afterwards like before. */
    p->shift_factor_x = pr->shift_x;
    p->shift_factor_y = pr->shift_y;
    p->apparent_width = p->native_width * pr->shift_x;
    p->apparent_height = p->native_height * pr->shift_y;
    p->subframe_index = 0;
    p->actuator_freq_hz = p->max_input_hz * pr->shift_x * pr->shift_y;
    p->actuator_sync_valid = true;

    p->rated_lumens = SR_ZERO;
    p->throw_distance_m = SR_ZERO;
    p->throw_ratio = SR_ZERO;
    p->screen_lux = SR_ZERO;

    p->measured_distance_m = SR_ZERO;
    p->autofocus_locked = false;

    p->tilt_angle_deg = SR_ZERO;
    p->keystone_scale_top = SR_ONE;
    p->keystone_scale_bottom = SR_ONE;

    p->dither_strength = SR_FROM_FLOAT(0.15);

    p->m5.omega = device_id;
    p->m5.r = SR_ZERO;
    p->m5.ell = SR_ZERO;
    p->m5.phi = SR_ZERO;
    p->m5.chi = 0;
    p->coverage_ratio = SR_ZERO;

    p->cycle_count = 0;
    p->irq_frame_ready = false;
    p->active = true;
    return true;
}

bool dlp_projector_verify_actuator_sync(dlp_projector_t *p) {
    if (!p) return false;
    uint32_t subframes = p->shift_factor_x * p->shift_factor_y;
    if (subframes == 0) subframes = 1;
    uint32_t required_hz = p->max_input_hz * subframes;
    p->actuator_sync_valid = (p->actuator_freq_hz >= required_hz);
    return p->actuator_sync_valid;
}

bool dlp_projector_set_wobulation(dlp_projector_t *p, uint32_t shift_x, uint32_t shift_y) {
    if (!p || shift_x == 0 || shift_y == 0) return false;
    p->shift_factor_x = shift_x;
    p->shift_factor_y = shift_y;
    p->apparent_width = p->native_width * shift_x;
    p->apparent_height = p->native_height * shift_y;
    p->subframe_index = 0;
    /* Default to exactly the minimum physically-required mechanical
     * shift rate; a caller wiring real hardware can overwrite
     * actuator_freq_hz with a measured value and re-verify. */
    p->actuator_freq_hz = p->max_input_hz * shift_x * shift_y;
    dlp_projector_verify_actuator_sync(p);
    return true;
}

void dlp_projector_set_photometrics(dlp_projector_t *p, surplus_real_t rated_lumens,
                                     surplus_real_t throw_ratio) {
    if (!p) return;
    p->rated_lumens = rated_lumens;
    p->throw_ratio = throw_ratio;
}

surplus_real_t dlp_projector_compute_screen_lux(dlp_projector_t *p, surplus_real_t throw_distance_m) {
    if (!p) return SR_ZERO;
    p->throw_distance_m = throw_distance_m;
    if (p->throw_ratio == SR_ZERO || throw_distance_m == SR_ZERO) {
        p->screen_lux = SR_ZERO;
        return SR_ZERO;
    }
    /* Spec-sheet throw-ratio convention: throw_ratio = distance / image_width. */
    surplus_real_t image_width_m = SR_DIV(throw_distance_m, p->throw_ratio);
    /* Native array aspect (854:480 or 960:540, both ~16:9) gives the image height. */
    surplus_real_t aspect = SR_DIV(SR_FROM_INT((int64_t)p->native_height),
                                    SR_FROM_INT((int64_t)p->native_width));
    surplus_real_t image_height_m = SR_MUL(image_width_m, aspect);
    surplus_real_t area_m2 = SR_MUL(image_width_m, image_height_m);
    if (area_m2 == SR_ZERO) {
        p->screen_lux = SR_ZERO;
        return SR_ZERO;
    }
    /* E (lux) = Phi (lm) / A (m^2) -- the definition of illuminance. */
    p->screen_lux = SR_DIV(p->rated_lumens, area_m2);
    return p->screen_lux;
}

surplus_real_t dlp_projector_laser_measure(dlp_projector_t *p, surplus_real_t round_trip_ns) {
    if (!p) return SR_ZERO;
    /* distance = c * dt / 2 (time-of-flight); dt_s = round_trip_ns * 1e-9 */
    surplus_real_t dt_s = SR_DIV(round_trip_ns, SR_FROM_INT(1000000000));
    surplus_real_t dist = SR_DIV(SR_MUL(DLP_SPEED_OF_LIGHT_M_S, dt_s), SR_FROM_INT(2));
    p->measured_distance_m = dist;
    /* Lock range matches the published Tank 5 laser-AF spec (0.5m-4m). */
    p->autofocus_locked = (SR_CMP(dist, SR_FROM_FLOAT(0.5)) >= 0) &&
                           (SR_CMP(dist, SR_FROM_FLOAT(4.0)) <= 0);
    return dist;
}

void dlp_projector_set_tilt(dlp_projector_t *p, surplus_real_t tilt_deg) {
    if (!p) return;
    p->tilt_angle_deg = tilt_deg;

    /* Small-angle keystone model, normalized against the DMD's own
     * mechanical tilt range so scale factors stay in a physically
     * sane band; beyond that range the optics don't meaningfully
     * resolve keystone anyway, so the ratio is clamped to +/-1. */
    surplus_real_t max_tilt = p->mirror_tilt_deg;
    surplus_real_t ratio = SR_DIV(tilt_deg, max_tilt);
    if (SR_CMP(ratio, SR_ONE) > 0) ratio = SR_ONE;
    surplus_real_t neg_one = SR_SUB(SR_ZERO, SR_ONE);
    if (SR_CMP(ratio, neg_one) < 0) ratio = neg_one;

    surplus_real_t half = SR_FROM_FLOAT(0.5);
    p->keystone_scale_top    = SR_SUB(SR_ONE, SR_MUL(ratio, half));
    p->keystone_scale_bottom = SR_ADD(SR_ONE, SR_MUL(ratio, half));
}

void dlp_projector_tick(dlp_projector_t *p) {
    if (!p || !p->active) return;
    p->cycle_count++;
    uint32_t subframes = p->shift_factor_x * p->shift_factor_y;
    if (subframes == 0) subframes = 1;
    p->subframe_index = (p->subframe_index + 1) % subframes;
    p->irq_frame_ready = (p->subframe_index == 0);
}

void dlp_projector_apply_dither(const dlp_projector_t *p,
                                 uint8_t *r, uint8_t *g, uint8_t *b) {
    if (!p || !r || !g || !b) return;
    surplus_real_t strength = p->dither_strength;
    if (SR_CMP(strength, SR_ZERO) <= 0) return;
    if (SR_CMP(strength, SR_ONE) > 0) strength = SR_ONE;
    surplus_real_t keep = SR_SUB(SR_ONE, strength);

    uint8_t comp_r = (uint8_t)(255 - *r);
    uint8_t comp_g = (uint8_t)(255 - *g);
    uint8_t comp_b = (uint8_t)(255 - *b);

    surplus_real_t new_r = SR_ADD(SR_MUL(SR_FROM_INT(*r), keep), SR_MUL(SR_FROM_INT(comp_r), strength));
    surplus_real_t new_g = SR_ADD(SR_MUL(SR_FROM_INT(*g), keep), SR_MUL(SR_FROM_INT(comp_g), strength));
    surplus_real_t new_b = SR_ADD(SR_MUL(SR_FROM_INT(*b), keep), SR_MUL(SR_FROM_INT(comp_b), strength));

#ifdef TEST_HOST
    *r = (uint8_t)(new_r + 0.5);
    *g = (uint8_t)(new_g + 0.5);
    *b = (uint8_t)(new_b + 0.5);
#else
    *r = (uint8_t)((new_r + (SR_ONE >> 1)) >> SR_SHIFT);
    *g = (uint8_t)((new_g + (SR_ONE >> 1)) >> SR_SHIFT);
    *b = (uint8_t)((new_b + (SR_ONE >> 1)) >> SR_SHIFT);
#endif
}

surplus_real_t dlp_projector_update_coverage(dlp_projector_t *p) {
    if (!p) return SR_ZERO;
    /* r = screen illuminance normalized against a common projector
     * siting target (~150 lux for indoor presentation legibility);
     * ell = actuator sync validity as the M5 truth axis. */
    surplus_real_t target_lux = SR_FROM_INT(150);
    p->m5.r = (p->throw_distance_m == SR_ZERO) ? SR_ZERO : SR_DIV(p->screen_lux, target_lux);
    p->m5.ell = p->actuator_sync_valid ? SR_ONE : SR_ZERO;
    p->m5.phi = SR_ZERO;

    surplus_real_t product = SR_MUL(p->m5.r, p->m5.ell);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(COVERAGE_FLOOR_NUM), SR_FROM_INT(COVERAGE_FLOOR_DEN));
    p->coverage_ratio = SR_DIV(product, floor);
    return p->coverage_ratio;
}
