/* dlp_projector.h — DLP Pico Projector Hardware-as-Code Module
 *
 * Models the built-in DLP projector shared by the Unihertz 8849 Tank
 * phone/tablet line (Tank 3 Pro, Tank 4/4 Pro, Tank 5, Tank Pad/Pad E)
 * as a register-mapped, event-cycle-driven device, in the same
 * "hardware is code" spirit as kernel/src/hardware/rtl_device.h and
 * kernel/src/quantum/quantum_device.h.
 *
 * Every number this file computes is real, checkable physics/optics,
 * not a claim about what Unihertz's exact silicon does:
 *   - Native array (854x480), 5.4um mirror pitch, +/-17 deg tilt, and
 *     120Hz max input rate match TI's DLP2010 .2" WVGA datasheet --
 *     the DLP Pico chipset class publicly reported as consistent with
 *     these products (Tank's DLP is NOT confirmed by Unihertz to be
 *     this exact TI part number; the class characteristics are what
 *     is grounded here, not the silicon identity).
 *   - N-way pixel-shift ("wobulation"): TI documents 2-way and 4-way
 *     mechanical actuators that mechanically re-tilt/shift the same
 *     DMD between sub-frames to synthesize 2x/4x the native pixel
 *     count on-screen (TI DLPA059h Sec. 4). This IS the real
 *     mechanism the Tank 4/4 Pro teardown coverage describes (a
 *     360p-native DMD producing an apparent 720p image via a
 *     "vibrating mirror" showing 4 shifted sub-frames) -- it is what
 *     backs this module's "increase clarity without hardware
 *     changes" feature, not an unstated/undefined effect.
 *   - Laser autofocus/rangefinder: plain time-of-flight physics,
 *     distance = c * delta_t / 2.
 *   - Complementary-color dither: single-DMD sequential-color
 *     projectors time-multiplex R/G/B illumination through one DMD,
 *     which causes the well-documented "rainbow effect" color-breakup
 *     artifact on fast eye motion. Blending a small, sign-correct
 *     amount of the complementary color into temporally-adjacent
 *     color sub-fields is a real, explainable mitigation (temporal
 *     pre-emphasis in color-field-sequential displays) -- distinct
 *     from, and NOT a claim of, super-resolution via optical
 *     interference, which this module does not attempt.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */
#ifndef DLP_PROJECTOR_H
#define DLP_PROJECTOR_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"
#include "surplus.h"
#include "edp_risk.h"

/* ===== Real physical constants ===== */
#define DLP_SPEED_OF_LIGHT_M_S   SR_FROM_INT(299792458)   /* c, exact by SI definition */

/* ===== TI DLP2010-class .2" WVGA DMD (see file header) ===== */
#define DLP_NATIVE_WIDTH         854
#define DLP_NATIVE_HEIGHT        480
#define DLP_MIRROR_PITCH_UM_X100 540   /* 5.40 um, x100 to stay integer at the API boundary */
#define DLP_MIRROR_TILT_DEG_X10  170   /* 17.0 deg, x10 */
#define DLP_MAX_INPUT_HZ         120

#define DLP_MAX_LABEL_LEN        32

/* ===== Device ===== */

typedef struct dlp_projector {
    uint32_t device_id;
    char name[DLP_MAX_LABEL_LEN];

    /* Native DMD array (fixed, from the datasheet class above) */
    uint32_t native_width;
    uint32_t native_height;
    surplus_real_t mirror_tilt_deg;
    uint32_t max_input_hz;

    /* Wobulation: N-way actuator. shift_x * shift_y sub-frames are
     * displayed per apparent frame, each mechanically offset by a
     * sub-pixel amount, to synthesize a higher apparent resolution
     * from the same native DMD (TI 2-way/4-way actuator technique). */
    uint32_t shift_factor_x;
    uint32_t shift_factor_y;
    uint32_t apparent_width;         /* native_width  * shift_factor_x */
    uint32_t apparent_height;        /* native_height * shift_factor_y */
    uint32_t subframe_index;         /* 0 .. shift_factor_x*shift_factor_y-1 */
    uint32_t actuator_freq_hz;       /* required mechanical shift rate */
    bool     actuator_sync_valid;    /* actuator_freq_hz >= max_input_hz * subframes -- else physically impossible */

    /* Photometrics (real photometry: illuminance = flux / area) */
    surplus_real_t rated_lumens;
    surplus_real_t throw_distance_m;
    surplus_real_t throw_ratio;       /* image_width / throw_distance, spec-sheet style */
    surplus_real_t screen_lux;        /* computed */

    /* Laser autofocus / rangefinder (shared laser module on real HW) */
    surplus_real_t measured_distance_m;
    bool autofocus_locked;            /* within focus range for this class of optics */

    /* Keystone (real projective geometry, small-angle model) */
    surplus_real_t tilt_angle_deg;
    surplus_real_t keystone_scale_top;
    surplus_real_t keystone_scale_bottom;

    /* Complementary-color dither (see file header) */
    surplus_real_t dither_strength;   /* 0..1, fraction of complementary color blended into adjacent field */

    /* M5 coordinates + coverage (kernel-wide convention, e.g. rtl_device_t) */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;

    /* Event-cycle bridging: advanced ONLY by dlp_projector_tick(),
     * which the caller drives from the kernel's cycle_pulse_t /
     * phase_coordinator_tick() stream -- never from a raw wall-clock
     * poll. This is the same clocked-hardware/event-space boundary
     * pattern as the GICv3 timer PPI -> phase_coordinator_tick(). */
    uint64_t cycle_count;
    bool irq_frame_ready;             /* one full apparent frame (all sub-frames) completed */
    bool active;
} dlp_projector_t;

/* ===== API ===== */

void dlp_projector_init(dlp_projector_t *p, uint32_t device_id, const char *name);

/* Configure the N-way actuator and recompute apparent resolution +
 * the required mechanical shift frequency. Returns false (and does
 * NOT enable the actuator) if shift_x/shift_y are 0. */
bool dlp_projector_set_wobulation(dlp_projector_t *p, uint32_t shift_x, uint32_t shift_y);

/* Real feasibility check: an N-way actuator must physically cycle at
 * >= max_input_hz * shift_x * shift_y to keep up with the apparent
 * frame rate. Sets/returns actuator_sync_valid. */
bool dlp_projector_verify_actuator_sync(dlp_projector_t *p);

/* Photometry: illuminance (lux) on a screen of the given width at
 * throw_distance_m, given throw_ratio (image_width_m = throw_distance_m
 * / throw_ratio). E = flux_lm / area_m2. */
void dlp_projector_set_photometrics(dlp_projector_t *p, surplus_real_t rated_lumens,
                                     surplus_real_t throw_ratio);
surplus_real_t dlp_projector_compute_screen_lux(dlp_projector_t *p, surplus_real_t throw_distance_m);

/* Laser autofocus: real time-of-flight, distance = c * dt / 2.
 * round_trip_ns is the measured pulse return time in nanoseconds. */
surplus_real_t dlp_projector_laser_measure(dlp_projector_t *p, surplus_real_t round_trip_ns);

/* Keystone: tilt_deg is the projector's tilt off the screen normal.
 * Uses a small-angle projective model: for a projector at height h
 * tilted by theta at throw distance d, the far/near edge width ratio
 * scales as (d +/- h*sin(theta)) / d -- approximated here via tan(theta)
 * scaled by a normalized geometry factor so scale_top/scale_bottom
 * bound to [~0.5, ~1.5] over the DMD's own +/-17 deg mechanical range. */
void dlp_projector_set_tilt(dlp_projector_t *p, surplus_real_t tilt_deg);

/* Advance exactly one event cycle (call from the kernel's event-cycle
 * loop, e.g. once per phase_coordinator_tick()/cycle_pulse_t -- NEVER
 * from a raw timer poll). Advances subframe_index; sets
 * irq_frame_ready when a full apparent frame completes. */
void dlp_projector_tick(dlp_projector_t *p);

/* Complementary-color dither: computes the complementary (255-c) of
 * each channel and blends `dither_strength` of it into the given
 * subframe's channel to pre-cancel motion-induced color-breakup on
 * temporally adjacent color fields. Operates in place on one pixel. */
void dlp_projector_apply_dither(const dlp_projector_t *p,
                                 uint8_t *r, uint8_t *g, uint8_t *b);

/* Recompute M5 coverage (r = screen_lux normalized, ell = actuator
 * sync validity) per kernel-wide convention. */
surplus_real_t dlp_projector_update_coverage(dlp_projector_t *p);

#endif /* DLP_PROJECTOR_H */
