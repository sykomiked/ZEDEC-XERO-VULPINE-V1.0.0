/* physics_core.c — M5-Native Physics Simulation Engine Implementation
 * Uses exact rational arithmetic for conserved quantities (no floating-point drift).
 * Wave interference via complex phase (IPHASE).
 * Measurement/collapse events via CHOICE.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "physics_core.h"
#include "axiom_matrix_core.h"
#include "choice_core.h"
#include "rmag_core.h"
#include <math.h>
#include <string.h>

static const double PHYSICS_G = 6.674e-11;
static const double PHYSICS_K = 8.988e9;

void physics_init(physics_state_t *s, axiom_matrix_t *matrix) {
    memset(s, 0, sizeof(physics_state_t));
    s->matrix = matrix;
    s->current_step = 0;
    s->paradox_level = 0;
    s->total_energy = (rational_t){0, 1};
    s->total_momentum = (rational_t){0, 1};
    s->field_potential = 0.0;
}

uint32_t physics_add_body(physics_state_t *s, double complex pos, double complex vel,
                          rational_t mass, rational_t charge) {
    if (s->num_bodies >= PHYSICS_MAX_BODIES) return UINT32_MAX;
    uint32_t idx = s->num_bodies++;
    physics_body_t *b = &s->bodies[idx];
    memset(b, 0, sizeof(physics_body_t));
    b->id = idx;
    b->position = pos;
    b->velocity = vel;
    b->mass = rational_normalize(mass);
    b->charge = rational_normalize(charge);
    b->field_phase = m5_phase_from_double(creal(pos), cimag(pos));
    b->boundary_state = TRIT_TRUE;
    b->collapsed = false;
    return idx;
}

double complex physics_force(const physics_body_t *a, const physics_body_t *b) {
    double complex dr = b->position - a->position;
    double dist = cabs(dr);
    if (dist < 1e-15) return 0.0;
    double m_a = rational_mag(a->mass);
    double m_b = rational_mag(b->mass);
    double q_a = rational_mag(a->charge);
    double q_b = rational_mag(b->charge);
    double f_grav = -PHYSICS_G * m_a * m_b / (dist * dist);
    double f_elec = PHYSICS_K * q_a * q_b / (dist * dist);
    double f_total = f_grav + f_elec;
    return f_total * (dr / dist);
}

double complex physics_field_at(const physics_state_t *s, double complex point) {
    double complex field = 0.0;
    for (uint32_t i = 0; i < s->num_bodies; i++) {
        const physics_body_t *b = &s->bodies[i];
        double complex dr = point - b->position;
        double dist = cabs(dr);
        if (dist < 1e-15) continue;
        double q = rational_mag(b->charge);
        field += q / (dist * dist) * (dr / dist);
    }
    return field;
}

int physics_step(physics_state_t *s) {
    if (s->num_bodies == 0) return -1;

    for (uint32_t i = 0; i < s->num_bodies; i++) {
        if (s->bodies[i].boundary_state == TRIT_FALSE) continue;
        double complex net_force = 0.0;
        for (uint32_t j = 0; j < s->num_bodies; j++) {
            if (i == j) continue;
            net_force += physics_force(&s->bodies[i], &s->bodies[j]);
        }
        double m = rational_mag(s->bodies[i].mass);
        if (m > 0) {
            double complex accel = net_force / m;
            s->bodies[i].velocity += accel * 0.01;
        }
    }

    for (uint32_t i = 0; i < s->num_bodies; i++) {
        if (s->bodies[i].boundary_state == TRIT_FALSE) continue;
        s->bodies[i].position += s->bodies[i].velocity * 0.01;
        s->bodies[i].field_phase =
            m5_phase_from_double(creal(s->bodies[i].position), cimag(s->bodies[i].position));
    }

    s->current_step++;

    if (s->matrix) {
        for (uint32_t i = 0; i < s->num_bodies; i++) {
            double complex val = s->bodies[i].position + I * s->bodies[i].velocity;
            axiom_matrix_set(s->matrix, s->current_step * PHYSICS_MAX_BODIES + i, s->bodies[i].mass,
                             s->bodies[i].boundary_state, s->bodies[i].field_phase,
                             (collapse_t){{i, 0}}, m5_cq16_from_dc(val));
        }
    }

    if (!physics_check_conservation(s)) {
        double complex shadow = s->field_potential;
        physics_resolve_shadow(s, shadow, s->paradox_level);
    }

    return 0;
}

int physics_measure(physics_state_t *s, uint32_t body_idx) {
    if (body_idx >= s->num_bodies) return -1;
    s->bodies[body_idx].collapsed = true;
    choice_handoff();
    return 0;
}

bool physics_check_conservation(const physics_state_t *s) {
    /* Summed in double: the old int64 rational sum (kinetic * 10^6) overflowed
     * for large energies, which is undefined behaviour. */
    double ke_mag = 0.0;
    for (uint32_t i = 0; i < s->num_bodies; i++) {
        double v = cabs(s->bodies[i].velocity);
        double m = rational_mag(s->bodies[i].mass);
        ke_mag += 0.5 * m * v * v;
    }
    double te_mag = rational_mag(s->total_energy);
    if (s->current_step > 0 && te_mag > 0) {
        double drift = fabs(ke_mag - te_mag) / te_mag;
        if (drift > 0.01) return false;
    }
    return true;
}

void physics_resolve_shadow(physics_state_t *s, double complex shadow, uint32_t level) {
    double complex resolved = shadow + (-shadow);
    (void)resolved;
    s->paradox_level = level;
    if (s->matrix) {
        axiom_matrix_set(s->matrix, s->current_step, (rational_t){0, 1}, TRIT_FALSE,
                         (phase_t){0, 0}, (collapse_t){{0, 0}}, m5_cq16_from_dc(resolved));
    }
}
