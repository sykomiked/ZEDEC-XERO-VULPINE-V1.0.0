/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cupel_rules.h — Cupel's rules in C, the reference its Trebago, KRON, Eve
 * and MYRA blocks are checked against (blocks/cupel), with the rules it
 * composes from nearer the core. Pure integer functions, no state: the
 * model (cupel_model.c) and the language test (tests/cupel_zxapp.inc)
 * both include this, so the languages are tested against the very C the
 * model runs.
 *
 * Units. Cupel works in reduced units, as Lennard-Jones runs do: every
 * particle's mass is the unit, so a force is an acceleration and a step
 * has no division. A position is an integer in 1/scale of a length unit,
 * a velocity in 1/scale of a length unit a tick, a force in 1/scale of a
 * length unit a tick squared, and dt is whole ticks. Nothing is a float.
 *
 *   CR1 NET FORCE FROM THE PAIR TABLE (pair_force_net): one row per entry
 *       of the table, the pair (p, q) and the force on p along one axis
 *       (q takes the opposite, Newton's third law). The net force on atom
 *       want is the sum of the forces of the rows whose p is want, less
 *       those whose q is want, exact. Each force is -CUPEL_FORCE_MAX ..
 *       CUPEL_FORCE_MAX, else the whole table is refused (Trebago divides
 *       by zero; KRON adds it: held).
 *   CR2 THE KICK (leapfrog_velocity): the half-step velocity after one
 *       step, v + force * dt, exact. v and force -CUPEL_VEL_MAX ..
 *       CUPEL_VEL_MAX and -CUPEL_FORCE_MAX .. CUPEL_FORCE_MAX, dt 1 ..
 *       CUPEL_DT_MAX, else refused (Trebago refuses, KRON answers: held).
 *   CR3 THE DRIFT (leapfrog_position): the position after one step,
 *       x + (v + force * dt) * dt, the kick then the drift, exact; x
 *       -CUPEL_COORD_MAX .. CUPEL_COORD_MAX and CR2's domain, else refused.
 *   CR4 PRECISION BOUNDS (sim_precision_ok, a truth): the fixed-point scale
 *       1 .. CUPEL_SCALE_MAX, the steps left (stepgap = the declared steps
 *       less the steps taken) 0 .. CUPEL_STEPS_MAX and the drift's room
 *       (driftgap = the bound less |energy - energy at the start|) 0 ..
 *       CUPEL_BOUND_MAX, each against its own range (Eve's); KRON reads
 *       any scale above 0 and any gap from 0 up: past a range it is held.
 *       A run whose bounds fail halts.
 *   CR5 LATTICE SITE (lattice_site_index): the site (ix, iy, iz) of a
 *       periodic cubic lattice of side sites a side, each index wrapped
 *       into 0 .. side - 1 (floored), numbered (x * side + y) * side + z.
 *       side 1 .. CUPEL_COORD_MAX and every index -CUPEL_COORD_MAX ..
 *       CUPEL_COORD_MAX, else refused (KRON alone; side 0 is its division
 *       by zero).
 *   CR6 MODEL TREE NODE (sim_tree_node_ok) fits ^ZXCUPEL(model, molecule,
 *       atom) = element ^ x ^ y ^ z: model 2 digits, molecule 4, atom 3,
 *       element 3, each coordinate 7, sign aside.
 *   CR7 RUN NODE (sim_run_record_ok) fits ^ZXCUPELRUN(run) = model ^ steps
 *       ^ energy ^ drift ^ halted: run 3 digits, model 2, steps 7, energy
 *       18, drift 18, halted 1, sign aside.
 *   CR8 SPRING. A pair's force on p along one axis is
 *       floor(k * (dq - dp - rest) / scale), k 1 .. CUPEL_K_MAX the spring's
 *       stiffness in 1/scale a tick squared; twice its stored energy is
 *       floor(k * |stretch|^2 / scale), the squared length gnomon.side_len_sq
 *       (GR4) of the stretch, axis by axis.
 *   CR9 TWO MODELS, ONE ENERGY (lpres_state_t): model A's energy a and B's
 *       b after the same steps, with a tolerance tol: A's claim is TRUE
 *       evidence, B's agrees (TRUE) when |a - b| <= tol and contradicts
 *       (FALSE) otherwise, joined with lpres_kjoin: TRUE or BOTH, and both
 *       values are shown. A tolerance below 0 compares nothing: NEITHER.
 *   CR10 A RUN'S SCORE, 0 .. 100 (the points retort.model_score_total
 *       adds): 100 - floor(100 * drift / bound) for a run that kept its
 *       bounds (a bound of 0 with no drift is 100), 0 for one that halted.
 *   Composed, as the parts' own rules: furrow_range_total (FW1), the
 *   energy of the molecules lo .. hi; relief_screen_x and relief_screen_y
 *   (RR1, RR2), an atom on the screen; relief_mesh_limits_ok (RR5), a
 *   model's atoms and pairs; retort_model_score_total (RT1),
 *   retort_run_limits_ok (RT3) and retort_run_record_fits (RT5), a run's
 *   score total, limits and record; gnomon_triangle_area2 (GR3), a lattice
 *   face's area as twice its half triangle, and gnomon_side_len_sq (GR4). */
#ifndef CUPEL_RULES_H
#define CUPEL_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "furrow_rules.h"
#include "gnomon_rules.h"
#include "lpres.h"
#include "relief_rules.h"
#include "retort_rules.h"

#define CUPEL_COORD_MAX GNOMON_COORD_MAX              /* a position, in 1/scale */
#define CUPEL_VEL_MAX   INT64_C(1000000)              /* CR2 */
#define CUPEL_FORCE_MAX INT64_C(1000000)              /* CR1, CR2 */
#define CUPEL_DT_MAX    INT64_C(1000)                 /* CR2: ticks a step */
#define CUPEL_SCALE_MAX INT64_C(1000000)              /* CR4 */
#define CUPEL_STEPS_MAX INT64_C(1000000)              /* CR4: steps a run may declare */
#define CUPEL_BOUND_MAX INT64_C(9999999999999999)     /* CR4: 16 digits, so CR10 never overflows */
#define CUPEL_K_MAX     INT64_C(1000000)              /* CR8 */
#define CUPEL_ATOMS     ((uint32_t) RELIEF_VERTS_MAX) /* RR5: an atom is a vertex */
#define CUPEL_PAIRS     ((uint32_t) RELIEF_FACES_MAX) /* RR5: a pair is a face drawn */
#define CUPEL_REFUSED   INT64_MIN

static inline bool cupel_in(int64_t v, int64_t lo, int64_t hi)
{
    return v >= lo && v <= hi;
}

/* n / d floored toward minus infinity; d > 0 */
static inline int64_t cupel_floor_div(int64_t n, int64_t d)
{
    int64_t q = n / d;
    if (n % d != 0 && n < 0) q -= 1;
    return q;
}

/* CR1: the net force on want, or CUPEL_REFUSED */
static inline int64_t cupel_pair_force_net(int64_t want, const int64_t *p, const int64_t *q,
                                           const int64_t *force, uint32_t n)
{
    int64_t s = 0;
    if (n && (!p || !q || !force)) return CUPEL_REFUSED;
    for (uint32_t i = 0; i < n; i++)
        if (!cupel_in(force[i], -CUPEL_FORCE_MAX, CUPEL_FORCE_MAX)) return CUPEL_REFUSED;
    for (uint32_t i = 0; i < n; i++) { /* n <= 2^26 rows of 10^6 stays far inside int64 */
        if (p[i] == want) s += force[i];
        if (q[i] == want) s -= force[i];
    }
    return s;
}

static inline bool cupel_kick_ok(int64_t v, int64_t force, int64_t dt)
{
    return cupel_in(v, -CUPEL_VEL_MAX, CUPEL_VEL_MAX) &&
           cupel_in(force, -CUPEL_FORCE_MAX, CUPEL_FORCE_MAX) && cupel_in(dt, 1, CUPEL_DT_MAX);
}

/* CR2: at most 10^6 + 10^9 */
static inline int64_t cupel_leapfrog_velocity(int64_t v, int64_t force, int64_t dt)
{
    if (!cupel_kick_ok(v, force, dt)) return CUPEL_REFUSED;
    return v + force * dt;
}

/* CR3: at most 10^6 + (10^6 + 10^9) * 10^3 */
static inline int64_t cupel_leapfrog_position(int64_t x, int64_t v, int64_t force, int64_t dt)
{
    if (!cupel_in(x, -CUPEL_COORD_MAX, CUPEL_COORD_MAX) || !cupel_kick_ok(v, force, dt))
        return CUPEL_REFUSED;
    return x + (v + force * dt) * dt;
}

/* CR4 */
static inline bool cupel_precision_ok(int64_t scale, int64_t stepgap, int64_t driftgap)
{
    return cupel_in(scale, 1, CUPEL_SCALE_MAX) && cupel_in(stepgap, 0, CUPEL_STEPS_MAX) &&
           cupel_in(driftgap, 0, CUPEL_BOUND_MAX);
}

/* CR4: the two gaps of a run; energies are 0 or more, so |e - e0| fits */
static inline void cupel_precision_gaps(int64_t declared, int64_t taken, int64_t bound, int64_t e0,
                                        int64_t e, int64_t out[2])
{
    int64_t drift = e >= e0 ? e - e0 : e0 - e;
    out[0] = declared - taken;
    out[1] = bound - drift;
}

/* CR5: the site's number, or CUPEL_REFUSED */
static inline int64_t cupel_lattice_site(int64_t ix, int64_t iy, int64_t iz, int64_t side)
{
    if (!cupel_in(side, 1, CUPEL_COORD_MAX) || !cupel_in(ix, -CUPEL_COORD_MAX, CUPEL_COORD_MAX) ||
        !cupel_in(iy, -CUPEL_COORD_MAX, CUPEL_COORD_MAX) ||
        !cupel_in(iz, -CUPEL_COORD_MAX, CUPEL_COORD_MAX))
        return CUPEL_REFUSED;
    int64_t a = ix - side * cupel_floor_div(ix, side), b = iy - side * cupel_floor_div(iy, side),
            c = iz - side * cupel_floor_div(iz, side);
    return (a * side + b) * side + c; /* below side^3 <= 10^18 */
}

/* CR6 */
static inline bool cupel_tree_node_ok(int64_t model, int64_t molecule, int64_t atom,
                                      int64_t element, int64_t x, int64_t y, int64_t z)
{
    return retort_digits_fit(model, 2) && retort_digits_fit(molecule, 4) &&
           retort_digits_fit(atom, 3) && retort_digits_fit(element, 3) && retort_digits_fit(x, 7) &&
           retort_digits_fit(y, 7) && retort_digits_fit(z, 7);
}

/* CR7 */
static inline bool cupel_run_node_ok(int64_t run, int64_t model, int64_t steps, int64_t energy,
                                     int64_t drift, int64_t halted)
{
    return retort_digits_fit(run, 3) && retort_digits_fit(model, 2) &&
           retort_digits_fit(steps, 7) && retort_digits_fit(energy, 18) &&
           retort_digits_fit(drift, 18) && retort_digits_fit(halted, 1);
}

/* CR8: the force on p along one axis; false outside the domain */
static inline bool cupel_spring_force(int64_t k, int64_t stretch, int64_t scale, int64_t *out)
{
    if (!cupel_in(k, 1, CUPEL_K_MAX) || !cupel_in(scale, 1, CUPEL_SCALE_MAX) ||
        !cupel_in(stretch, -CUPEL_COORD_MAX, CUPEL_COORD_MAX))
        return false;
    *out = cupel_floor_div(k * stretch, scale);
    return true;
}

/* CR8: twice a spring's energy, |s|^2 by gnomon.side_len_sq, axis by axis */
static inline bool cupel_spring_energy2(int64_t k, const int64_t s[3], int64_t scale, int64_t *out)
{
    int64_t xy = 0, z = 0;
    if (!cupel_in(k, 1, CUPEL_K_MAX) || !cupel_in(scale, 1, CUPEL_SCALE_MAX) ||
        !gnomon_side_len_sq(0, 0, s[0], s[1], &xy) || !gnomon_side_len_sq(0, 0, s[2], 0, &z))
        return false;
    *out = cupel_floor_div(k * (xy + z), scale); /* at most 10^6 * 3 * 10^12 */
    return true;
}

/* twice an atom's kinetic energy, |v|^2 by gnomon.side_len_sq */
static inline bool cupel_kinetic2(const int64_t v[3], int64_t *out)
{
    int64_t xy = 0, z = 0;
    if (!gnomon_side_len_sq(0, 0, v[0], v[1], &xy) || !gnomon_side_len_sq(0, 0, v[2], 0, &z))
        return false;
    *out = xy + z;
    return true;
}

/* CR9: shown[0] = a, shown[1] = b whatever the answer */
static inline lpres_state_t cupel_energy_state(int64_t a, int64_t b, int64_t tol, int64_t shown[2])
{
    if (shown) shown[0] = a, shown[1] = b;
    if (tol < 0) return LPRES_STATE_NEITHER;
    /* |a - b| <= tol without overflow: a - b computed only when it fits */
    bool agree;
    if ((b > 0 && a <= INT64_MIN + b) || (b < 0 && a > INT64_MAX + b))
        agree = false;
    else
        agree = (a >= b ? a - b : b - a) <= tol;
    return lpres_kjoin(LPRES_STATE_TRUE, agree ? LPRES_STATE_TRUE : LPRES_STATE_FALSE);
}

/* CR10 */
static inline int64_t cupel_score(int64_t drift, int64_t bound, bool halted)
{
    if (halted || drift < 0 || bound < 0 || drift > bound || bound > CUPEL_BOUND_MAX) return 0;
    if (!bound) return 100;
    return 100 - drift * 100 / bound; /* drift * 100 < 10^18 */
}

#endif
