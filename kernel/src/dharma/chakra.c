/* chakra.c — The Chakra System: Nodes and Yodes
 *
 * Seven chakra nodes strung along the Sushumna, connected by yodes.
 * Kundalini rises inductively (Muladhara -> Sahasrara); Appu descends
 * deductively (Sahasrara -> Muladhara). All charge is exact-rational.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "chakra.h"
#include "../rmag/rmag_core.h"

void chakra_init(chakra_system_t *c) {
    for (uint32_t i = 0; i < CHAKRA_NUM_NODES; i++) {
        c->nodes[i].id = (chakra_node_id_t)i;
        c->nodes[i].phase = (l13_phase_t)0;
        c->nodes[i].charge = (rational_t){0, 1};
        c->nodes[i].task_id = 0;
    }
    c->num_yodes = 0;
    /* Default Sushumna path: Muladhara -> ... -> Sahasrara, unit conductance */
    for (uint32_t i = 0; i < CHAKRA_NUM_NODES - 1; i++) {
        chakra_link(c, (chakra_node_id_t)i, (chakra_node_id_t)(i + 1),
                     (rational_t){1, 1});
    }
}

int32_t chakra_link(chakra_system_t *c, chakra_node_id_t from,
                     chakra_node_id_t to, rational_t conductance) {
    /* Check if a yode from->to already exists; update it if so */
    for (uint32_t i = 0; i < c->num_yodes; i++) {
        if (c->yodes[i].from == from && c->yodes[i].to == to && c->yodes[i].active) {
            c->yodes[i].conductance = conductance;
            return (int32_t)i;
        }
    }
    if (c->num_yodes >= CHAKRA_MAX_YODES) return -1;
    int32_t idx = (int32_t)c->num_yodes;
    c->yodes[idx].from = from;
    c->yodes[idx].to = to;
    c->yodes[idx].conductance = conductance;
    c->yodes[idx].active = true;
    c->num_yodes++;
    return idx;
}

rational_t chakra_conductance(const chakra_system_t *c,
                               chakra_node_id_t from, chakra_node_id_t to) {
    for (uint32_t i = 0; i < c->num_yodes; i++) {
        if (c->yodes[i].from == from && c->yodes[i].to == to && c->yodes[i].active)
            return c->yodes[i].conductance;
    }
    return (rational_t){0, 1};
}

rational_t kundalini_rise(chakra_system_t *c, rational_t seed_charge) {
    /* Start at Muladhara with the seed charge */
    c->nodes[CHAKRA_MULADHARA].charge = seed_charge;
    /* Propagate upward through the Sushumna path */
    for (uint32_t i = 0; i < CHAKRA_NUM_NODES - 1; i++) {
        rational_t cond = chakra_conductance(c, (chakra_node_id_t)i,
                                              (chakra_node_id_t)(i + 1));
        /* charge at next node = charge at current * conductance (accumulated) */
        rational_t propagated = rmag_mul_quotas(c->nodes[i].charge, cond);
        /* Accumulate: next node's charge += propagated */
        c->nodes[i + 1].charge = rmag_add_quotas(c->nodes[i + 1].charge, propagated);
    }
    return c->nodes[CHAKRA_SAHASRARA].charge;
}

void appu_descend(chakra_system_t *c, bodhi_state_t transcendent_observation) {
    /* Start at Sahasrara with the transcendent observation's coherence */
    c->nodes[CHAKRA_SAHASRARA].charge = transcendent_observation.coherence;
    /* Propagate downward through the Sushumna path (reverse direction) */
    for (int32_t i = (int32_t)CHAKRA_SAHASRARA; i > 0; i--) {
        rational_t cond = chakra_conductance(c, (chakra_node_id_t)(i - 1),
                                              (chakra_node_id_t)i);
        /* Deduced charge at lower node = charge at higher * conductance */
        rational_t deduced = rmag_mul_quotas(c->nodes[i].charge, cond);
        c->nodes[i - 1].charge = rmag_add_quotas(c->nodes[i - 1].charge, deduced);
    }
}

/* ---- DECLARATION -----------------------------------------------------------

 * REQUIRES measured from chakra.o's `nm -u` = {rmag_add_quotas,
 * rmag_mul_quotas}. Nothing else crosses this file's boundary -- in
 * particular it does NOT reach the cyclotomic algebra, which the name would
 * have suggested and the object file refutes.
 */
#include "zxv_decl.h"
ZXV_DECLARE(chakra,
    ZXV_PROVIDES(chakra_ready),
    ZXV_REQUIRES(rmag_ready),
    ZXV_NO_BRINGUP);
