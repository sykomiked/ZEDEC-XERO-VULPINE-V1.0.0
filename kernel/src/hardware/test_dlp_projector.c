/* test_dlp_projector.c — Functional correctness tests for the DLP
 * Pico Projector hardware-as-code module.
 *
 * These tests verify the module's formulas match the physics
 * documented in dlp_projector.h (photometry, time-of-flight,
 * wobulation timing feasibility, keystone geometry, complementary
 * dither) by independently recomputing expected values from the
 * same real-world equations, not by asserting against the
 * implementation's own output.
 */
#include "dlp_projector.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static int feq(double a, double b, double eps)
{
    double diff = fabs(a - b);
    double scale = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    if (scale < 1.0) scale = 1.0;
    return diff <= eps * scale;
}

int main(void)
{
    /* ===== init defaults ===== */
    dlp_projector_t p;
    dlp_projector_init(&p, 7, "Tank5-Projector");
    assert(p.device_id == 7);
    assert(strcmp(p.name, "Tank5-Projector") == 0);
    assert(p.native_width == 854 && p.native_height == 480);
    assert(feq(p.mirror_tilt_deg, 17.0, 1e-9));
    assert(p.max_input_hz == 120);
    assert(p.shift_factor_x == 1 && p.shift_factor_y == 1);
    assert(p.apparent_width == 854 && p.apparent_height == 480);
    assert(p.actuator_sync_valid);
    assert(!p.autofocus_locked);
    assert(p.active);

    /* ===== wobulation: 4-way actuator (TI 4-way technique) ===== */
    {
        assert(dlp_projector_set_wobulation(&p, 2, 2));
        assert(p.apparent_width == 854 * 2);
        assert(p.apparent_height == 480 * 2);
        assert(p.actuator_freq_hz == 120u * 4u);
        assert(p.actuator_sync_valid);

        /* Physically insufficient actuator rate must be flagged invalid */
        p.actuator_freq_hz = 100;
        assert(!dlp_projector_verify_actuator_sync(&p));
        assert(!p.actuator_sync_valid);

        /* Restore valid config, reject zero shift factors */
        assert(dlp_projector_set_wobulation(&p, 2, 2));
        assert(!dlp_projector_set_wobulation(&p, 0, 2));
        assert(!dlp_projector_set_wobulation(&p, 2, 0));
    }

    /* ===== event-cycle tick: subframe cycling + frame-ready IRQ ===== */
    {
        dlp_projector_t q;
        dlp_projector_init(&q, 1, "tick-test");
        dlp_projector_set_wobulation(&q, 2, 2); /* 4 sub-frames per apparent frame */

        uint64_t before = q.cycle_count;
        for (int i = 1; i <= 3; i++) {
            dlp_projector_tick(&q);
            assert((uint32_t) i == q.subframe_index);
            assert(!q.irq_frame_ready);
        }
        dlp_projector_tick(&q); /* 4th sub-frame completes the apparent frame */
        assert(q.subframe_index == 0);
        assert(q.irq_frame_ready);
        assert(q.cycle_count == before + 4);

        dlp_projector_tick(&q);
        assert(q.subframe_index == 1);
        assert(!q.irq_frame_ready);
    }

    /* ===== photometry: E (lux) = Phi (lm) / A (m^2) ===== */
    {
        dlp_projector_t q;
        dlp_projector_init(&q, 2, "photometry");
        double lumens = 100.0, throw_ratio = 1.5, distance_m = 3.0;
        dlp_projector_set_photometrics(&q, SR_FROM_FLOAT(lumens), SR_FROM_FLOAT(throw_ratio));
        double got = dlp_projector_compute_screen_lux(&q, SR_FROM_FLOAT(distance_m));

        double image_w = distance_m / throw_ratio;
        double image_h = image_w * (480.0 / 854.0);
        double expected = lumens / (image_w * image_h);
        assert(feq(got, expected, 1e-9));
        assert(feq(q.screen_lux, expected, 1e-9));

        /* Degenerate cases must not divide by zero */
        assert(dlp_projector_compute_screen_lux(&q, SR_ZERO) == SR_ZERO);
        dlp_projector_t r;
        dlp_projector_init(&r, 3, "zero-throw-ratio");
        assert(dlp_projector_compute_screen_lux(&r, SR_FROM_FLOAT(2.0)) == SR_ZERO);
    }

    /* ===== laser autofocus: time-of-flight distance = c*dt/2 ===== */
    {
        dlp_projector_t q;
        dlp_projector_init(&q, 4, "laser-af");
        const double c = 299792458.0;

        double target_m = 2.0;
        double round_trip_ns = (2.0 * target_m / c) * 1e9;
        double got = dlp_projector_laser_measure(&q, SR_FROM_FLOAT(round_trip_ns));
        assert(feq(got, target_m, 1e-6));
        assert(q.autofocus_locked); /* within published 0.5-4m lock range */

        /* Lock-range boundaries (Tank 5 spec: 0.5m-4m) */
        double rt_049 = (2.0 * 0.49 / c) * 1e9;
        dlp_projector_laser_measure(&q, SR_FROM_FLOAT(rt_049));
        assert(!q.autofocus_locked);

        double rt_401 = (2.0 * 4.01 / c) * 1e9;
        dlp_projector_laser_measure(&q, SR_FROM_FLOAT(rt_401));
        assert(!q.autofocus_locked);
    }

    /* ===== keystone: small-angle model normalized to +/-17 deg ===== */
    {
        dlp_projector_t q;
        dlp_projector_init(&q, 5, "keystone");

        dlp_projector_set_tilt(&q, SR_FROM_FLOAT(8.5)); /* exactly half of 17.0 */
        assert(feq(q.keystone_scale_top, 0.75, 1e-9));
        assert(feq(q.keystone_scale_bottom, 1.25, 1e-9));

        /* Beyond the DMD's mechanical tilt range: clamps at +/-1.0 ratio */
        dlp_projector_set_tilt(&q, SR_FROM_FLOAT(34.0));
        assert(feq(q.keystone_scale_top, 0.5, 1e-9));
        assert(feq(q.keystone_scale_bottom, 1.5, 1e-9));

        /* Negative tilt flips which edge is compressed/expanded */
        dlp_projector_set_tilt(&q, SR_FROM_FLOAT(-8.5));
        assert(feq(q.keystone_scale_top, 1.25, 1e-9));
        assert(feq(q.keystone_scale_bottom, 0.75, 1e-9));
    }

    /* ===== complementary-color dither ===== */
    {
        dlp_projector_t q;
        dlp_projector_init(&q, 6, "dither");
        assert(feq(q.dither_strength, 0.15, 1e-9)); /* documented default */

        uint8_t r = 200, g = 100, b = 50;
        dlp_projector_apply_dither(&q, &r, &g, &b);
        /* new = old*(1-s) + (255-old)*s, s=0.15 */
        assert(r == 178); /* 200*0.85 + 55*0.15  = 178.25 -> 178 */
        assert(g == 108); /* 100*0.85 + 155*0.15 = 108.25 -> 108 */
        assert(b == 73);  /* 50*0.85  + 205*0.15 = 73.25  -> 73  */

        /* Zero strength must be a strict no-op */
        dlp_projector_t z;
        dlp_projector_init(&z, 8, "zero-dither");
        z.dither_strength = SR_ZERO;
        uint8_t zr = 10, zg = 20, zb = 30;
        dlp_projector_apply_dither(&z, &zr, &zg, &zb);
        assert(zr == 10 && zg == 20 && zb == 30);
    }

    /* ===== M5 coverage ===== */
    {
        dlp_projector_t q;
        dlp_projector_init(&q, 9, "coverage");
        dlp_projector_set_wobulation(&q, 2, 2);
        dlp_projector_set_photometrics(&q, SR_FROM_FLOAT(220.0), SR_FROM_FLOAT(1.2));
        dlp_projector_compute_screen_lux(&q, SR_FROM_FLOAT(2.0));

        surplus_real_t cov = dlp_projector_update_coverage(&q);
        assert(feq(q.m5.ell, 1.0, 1e-9)); /* actuator sync valid */
        double expected_r = q.screen_lux / 150.0;
        assert(feq(q.m5.r, expected_r, 1e-9));
        double expected_cov = (expected_r * 1.0) / 1.8;
        assert(feq(cov, expected_cov, 1e-9));
    }

    /* ===== chipset profiles: DLP2010 (Tank) and DLP230NP (1080p bay) ===== */
    {
        const dlp_profile_t *a = dlp_profile_get(DLP_PROFILE_DLP2010);
        const dlp_profile_t *n = dlp_profile_get(DLP_PROFILE_DLP230NP);
        assert(a && n && dlp_profile_get(DLP_PROFILE_COUNT) == NULL);
        assert(a->id == DLP_PROFILE_DLP2010 && n->id == DLP_PROFILE_DLP230NP);
        /* TI DLPS123C / product page */
        assert(strcmp(a->dmd, "DLP2010") == 0);
        assert(a->native_width == 854 && a->native_height == 480);
        assert(a->screen_width == 854 && a->screen_height == 480);
        assert(a->mirror_pitch_um_x100 == 540 && a->mirror_tilt_deg_x10 == 170);
        assert(a->diagonal_mm_x100 == 529 && a->max_input_hz == 120);
        assert(a->dmd_sublvds_bits == 4 && a->dmd_power_typ_uw == 90800);
        /* TI DLPS144G / product pages (DLP230NP, DLPC3436) */
        assert(strcmp(n->dmd, "DLP230NP") == 0 && strcmp(n->controller, "DLPC3436") == 0);
        assert(n->native_width == 960 && n->native_height == 540);
        assert(n->screen_width == 1920 && n->screen_height == 1080);
        assert(n->native_width * n->shift_x == n->screen_width);
        assert(n->native_height * n->shift_y == n->screen_height);
        assert(n->mirror_pitch_um_x100 == 540 && n->mirror_tilt_deg_x10 == 170);
        assert(n->diagonal_mm_x100 == 595 && n->max_input_hz == 60);
        assert(n->dmd_sublvds_bits == 8 && n->dmd_dclk_max_mhz == 540);
        assert(n->host_if == DLP_HOST_PARALLEL24 && n->host_pclk_max_mhz == 155);
        assert(n->dmd_power_typ_uw == 151220 && n->dmd_power_max_uw == 190300);
        assert(n->dmd_power_typ_uw <= n->dmd_power_max_uw);
        assert(a->dmd_power_typ_uw <= a->dmd_power_max_uw);
        /* active array = pitch x mirrors: 5.4 um x 960 = 5.184 mm (DLPS144G) */
        assert(n->mirror_pitch_um_x100 * n->native_width == 518400);

        /* dlp_projector_init is the DLP2010 profile, unchanged */
        dlp_projector_t d, e;
        memset(&d, 0, sizeof d);
        memset(&e, 0xFF, sizeof e);
        dlp_projector_init(&d, 11, "same");
        assert(dlp_projector_init_profile(&e, 11, "same", DLP_PROFILE_DLP2010));
        assert(d.profile == DLP_PROFILE_DLP2010 && e.profile == DLP_PROFILE_DLP2010);
        assert(d.native_width == e.native_width && d.native_height == e.native_height);
        assert(d.mirror_tilt_deg == e.mirror_tilt_deg && d.max_input_hz == e.max_input_hz);
        assert(d.apparent_width == e.apparent_width && d.actuator_freq_hz == e.actuator_freq_hz);
        assert(d.mirror_tilt_deg == SR_FROM_INT(17));

        /* 1080p profile: native 960x540, 2x2 built-in shift, 60 Hz */
        dlp_projector_t q;
        assert(dlp_projector_init_profile(&q, 12, "Bay-1080p", DLP_PROFILE_DLP230NP));
        assert(q.profile == DLP_PROFILE_DLP230NP);
        assert(q.native_width == 960 && q.native_height == 540);
        assert(q.apparent_width == 1920 && q.apparent_height == 1080);
        assert(q.shift_factor_x == 2 && q.shift_factor_y == 2);
        assert(q.max_input_hz == 60 && q.actuator_freq_hz == 240u);
        assert(dlp_projector_verify_actuator_sync(&q));
        assert(feq(q.mirror_tilt_deg, 17.0, 1e-9));

        /* four sub-frames per frame from init */
        for (int i = 1; i <= 3; i++) {
            dlp_projector_tick(&q);
            assert(q.subframe_index == (uint32_t) i && !q.irq_frame_ready);
        }
        dlp_projector_tick(&q);
        assert(q.subframe_index == 0 && q.irq_frame_ready);

        /* photometry uses the profile's own aspect (540/960) */
        double lumens = 450.0, tr = 0.25, dist = 0.55;
        dlp_projector_set_photometrics(&q, SR_FROM_FLOAT(lumens), SR_FROM_FLOAT(tr));
        double got = dlp_projector_compute_screen_lux(&q, SR_FROM_FLOAT(dist));
        double w = dist / tr, h = w * (540.0 / 960.0);
        assert(feq(got, lumens / (w * h), 1e-9));

        /* keystone normalised to the profile's tilt */
        dlp_projector_set_tilt(&q, SR_FROM_FLOAT(8.5));
        assert(feq(q.keystone_scale_top, 0.75, 1e-9));
        assert(feq(q.keystone_scale_bottom, 1.25, 1e-9));

        /* wobulation still overrides from the profile's native array */
        assert(dlp_projector_set_wobulation(&q, 1, 1));
        assert(q.apparent_width == 960 && q.actuator_freq_hz == 60u);

        /* unknown profile: refused, the device is not touched */
        dlp_projector_t u;
        memset(&u, 0x5A, sizeof u);
        assert(!dlp_projector_init_profile(&u, 13, "bad", DLP_PROFILE_COUNT));
        assert(u.device_id == 0x5A5A5A5Au);
        assert(!dlp_projector_init_profile(NULL, 13, "bad", DLP_PROFILE_DLP2010));
    }

    printf("All DLP Projector tests passed\n");
    return 0;
}
