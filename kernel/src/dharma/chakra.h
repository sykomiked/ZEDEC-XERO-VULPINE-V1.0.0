/* chakra.h — The Chakra System: Nodes and Yodes
 *
 * Seven chakra nodes strung along the Sushumna (the central channel),
 * connected by yodes -- named after the Hebrew Yod (a single point/
 * flame, the smallest letter, associated with Keter/the source and
 * literally meaning "hand": a point of contact). A yode is the
 * directed CONNECTION between two chakra nodes, carrying an exact-
 * rational conductance -- how freely charge moves through it. Nodes
 * are the graph's vertices; yodes are its edges.
 *
 * ---- Kundalini: inductive reasoning, ascending ----
 * Kundalini rises from Muladhara (root, grounded, particular) to
 * Sahasrara (crown, transcendent, general) through the linked yodes,
 * each step's charge informed by aggregating the charge below it --
 * particular instances generalizing upward, the same direction as
 * Bodhi's convergence to the zeta^0 source.
 *
 * ---- Appu: deductive reasoning, descending ----
 * Appu applies a general/transcendent Bodhi observation downward,
 * from Sahasrara to Muladhara, instantiating it as concrete charge at
 * each specific node along the way -- general principle deduced down
 * into particular, local instances.
 *
 * Kundalini and Appu are deliberately opposite currents over the SAME
 * graph, not two different graphs: induction and deduction operating
 * on one structure, in opposite directions.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef CHAKRA_H
#define CHAKRA_H

#include "m5_types.h"
#include "sephirot.h"
#include "bodhi.h"

typedef enum {
    CHAKRA_MULADHARA = 0,    /* root -- grounded, local, particular */
    CHAKRA_SVADHISTHANA = 1,
    CHAKRA_MANIPURA = 2,
    CHAKRA_ANAHATA = 3,      /* heart -- the balance point */
    CHAKRA_VISHUDDHA = 4,
    CHAKRA_AJNA = 5,
    CHAKRA_SAHASRARA = 6,    /* crown -- transcendent, general */
} chakra_node_id_t;

#define CHAKRA_NUM_NODES 7
#define CHAKRA_MAX_YODES 12  /* Sushumna's 6 direct links + room for Ida/Pingala-style cross-links */

typedef struct chakra_node {
    chakra_node_id_t id;
    l13_phase_t phase;   /* which OS-layer phase currently resides at this node; 0 = none */
    rational_t charge;   /* RMAG-exact energy at this node */
    uint32_t task_id;    /* which task/karma-locality this node currently represents; 0 = none */
} chakra_node_t;

typedef struct chakra_yode {
    chakra_node_id_t from, to;
    rational_t conductance;  /* how freely charge flows from `from` to `to`; 0 = no link */
    bool active;
} chakra_yode_t;

typedef struct chakra_system {
    chakra_node_t nodes[CHAKRA_NUM_NODES];
    chakra_yode_t yodes[CHAKRA_MAX_YODES];
    uint32_t num_yodes;
} chakra_system_t;

/* Initializes all 7 nodes at zero charge/no phase, and links the
 * default Sushumna path (Muladhara -> ... -> Sahasrara, 6 yodes) at
 * unit conductance. Additional yodes (e.g. Ida/Pingala cross-links)
 * can be added afterward via chakra_link. */
void chakra_init(chakra_system_t *c);

/* Adds (or updates, if the same from/to pair already exists) a yode
 * between two nodes. Returns the yode's index, or -1 if
 * CHAKRA_MAX_YODES is already full and this is a genuinely new link. */
int32_t chakra_link(chakra_system_t *c, chakra_node_id_t from, chakra_node_id_t to,
                     rational_t conductance);

/* Direct accessor: the conductance of the yode from `from` to `to`,
 * or an exact rational zero (not null) if no such yode is active. */
rational_t chakra_conductance(const chakra_system_t *c, chakra_node_id_t from, chakra_node_id_t to);

/* Kundalini: inductive ascent. Starting at Muladhara with seed_charge,
 * propagates upward through each active yode in the Sushumna path,
 * accumulating (aggregating, not just passing through) charge scaled
 * by each yode's conductance, updating every node's charge along the
 * way. Returns the final charge that reaches Sahasrara -- the
 * generalized result of the inductive climb. */
rational_t kundalini_rise(chakra_system_t *c, rational_t seed_charge);

/* Appu: deductive descent. Starting from a transcendent Bodhi
 * observation at Sahasrara, applies it downward through the Sushumna
 * path, instantiating a deduced, node-specific charge at each node
 * (scaled by that node's incoming yode conductance) all the way to
 * Muladhara -- the general principle particularized at each level. */
void appu_descend(chakra_system_t *c, bodhi_state_t transcendent_observation);

#endif
