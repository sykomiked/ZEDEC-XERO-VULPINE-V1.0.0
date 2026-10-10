/* physics_core.h — M5-Native Physics Simulation Engine
 * N-body/field solver using the five kernel subsystems:
 *   omega = time-step ordering, r = conserved quantities (exact rational),
 *   ell = boundary conditions, i-phi = wave phase, chi = measurement/collapse.
 * Per Kernel Spec §5.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef PHYSICS_CORE_H
#define PHYSICS_CORE_H

#include "m5_host_float.h" /* host-only module: double / double complex */
#include "m5_types.h"

#define PHYSICS_MAX_BODIES 512
#define PHYSICS_MAX_STEPS 100000

typedef struct physics_body {
    ordinal_t id;
    double complex position;
    double complex velocity;
    rational_t mass;
    rational_t charge;
    phase_t field_phase;
    trit_t boundary_state;
    bool collapsed;
} physics_body_t;

typedef struct physics_state {
    physics_body_t bodies[PHYSICS_MAX_BODIES];
    uint32_t num_bodies;
    ordinal_t current_step;
    rational_t total_energy;
    rational_t total_momentum;
    double complex field_potential;
    uint32_t paradox_level;
    axiom_matrix_t *matrix;
} physics_state_t;

void physics_init(physics_state_t *s, axiom_matrix_t *matrix);
uint32_t physics_add_body(physics_state_t *s, double complex pos, double complex vel,
                          rational_t mass, rational_t charge);
int physics_step(physics_state_t *s);
double complex physics_force(const physics_body_t *a, const physics_body_t *b);
double complex physics_field_at(const physics_state_t *s, double complex point);
int physics_measure(physics_state_t *s, uint32_t body_idx);
bool physics_check_conservation(const physics_state_t *s);
void physics_resolve_shadow(physics_state_t *s, double complex shadow, uint32_t level);

#endif
