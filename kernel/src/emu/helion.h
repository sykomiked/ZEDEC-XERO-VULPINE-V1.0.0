/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* helion.h — Helion: 2D->3D expansion by SPIRAL LOGIC, not extrusion.
 *
 * The rule (design intent): expanding two dimensions into three is NOT done by
 * "popping out the dimension and filling in the plank" (a straight orthogonal
 * extrusion). It is done by SPIN — like molecular spin, at a macro scale. Every
 * object has its own spin relative to its SHAPE; the spin IS the spiral; the
 * spin is spiral relative to the object's EVENT-SPACE TRAJECTORY; and the
 * object's spin is PERPENDICULAR to the spin of its environment/setting.
 *
 * That perpendicularity is the M5 principle: the object's rotational frame stays
 * orthogonal to its environment's, so they never collapse into one frame. As the
 * object spins about an axis perpendicular to the environment's spin, the
 * rotation CARRIES its 2D extent into the third dimension — the new dimension
 * emerges from the spin (a helix along the trajectory), never a flat prism.
 *
 * Integer-safe surplus_real_t vector math (double host / Q32.32 target). This is
 * the correct primitive for lifting 2D content into the 3D engines (Cinder/Quill)
 * and the holographic field — spin-lift instead of extrude. */
#ifndef ZXV_HELION_H
#define ZXV_HELION_H

#include <stdint.h>
#include "surplus.h"

typedef struct { surplus_real_t x, y, z; } h_vec3;

typedef struct helion_field {
    h_vec3        env_spin;     /* the environment/setting's spin axis (unit)     */
    h_vec3        obj_spin;     /* the object's spin axis — made perpendicular     */
    h_vec3        traj_dir;     /* the object's event-space trajectory direction   */
    h_vec3        origin;       /* where the object sits                           */
    surplus_real_t step_cos;    /* cos of one spin step (the spiral increment)     */
    surplus_real_t step_sin;    /* sin of one spin step                            */
    surplus_real_t traj_step;   /* trajectory advance per spin step                */
} helion_field_t;

/* Make v perpendicular to the unit axis e (Gram-Schmidt: v - (v.e)e), normalized. */
h_vec3 helion_perpendicular(h_vec3 v, h_vec3 e);

/* Configure a spin-lift field: the object's spin axis is derived from its shape's
 * principal direction and forced perpendicular to the environment spin. */
void   helion_configure(helion_field_t *f, h_vec3 env_spin, h_vec3 shape_principal,
                        h_vec3 traj_dir, h_vec3 origin, int spin_step_degrees);

/* Spiral-lift a 2D point (a,b) at spin step i into 3D: the object's plane, spun i
 * steps about its (perpendicular) axis and advanced along its trajectory. This is
 * the helix — NOT (origin + b in a fixed plane). */
h_vec3 helion_lift(const helion_field_t *f, surplus_real_t a, surplus_real_t b, int i);

/* On-target self-check: verify the four laws of the model —
 *   (1) object spin is perpendicular to environment spin (dot ~ 0);
 *   (2) the spin accumulates a consistent-chirality rotation (a spiral, not an
 *       oscillation or a straight line);
 *   (3) the lift genuinely enters the 3rd dimension (all three axes gain extent);
 *   (4) it is NOT extrusion (adjacent slices differ by rotation, not translation).
 * Returns 1 if spiral logic holds. */
int    helion_selfcheck(uint32_t *perp_permille_out);

#endif /* ZXV_HELION_H */
