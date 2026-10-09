/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_self.h — awareness of self, and awareness of that awareness.
 *
 * Each agent (and the swarm as a whole) keeps a set of interaction RINGS,
 * one for every thing it interacts with:
 *
 *   inner reflection (itself), being (its body: the machine and its budget),
 *   the user, the system (the ZXV kernel), the swarm, the internet, the world,
 *   and any further ring added at run time.
 *
 *   A1  OBSERVE.  For every interaction the agent records what it expected
 *       and what happened (both x1000). The gap feeds the ring's stress, an
 *       integer moving average: stress' = (3 * stress + gap) / 4.
 *   A2  REFLECT.  Each growth harmonic it looks at all its rings. The ring
 *       under most stress gets its attention. Stress at or above 382/1000
 *       (1 - 1/phi, rounded) means RECALIBRATE that ring. Otherwise it
 *       GROWs if the budget has room, and REFINEs (gets more efficient) if
 *       it does not. It never idles and never grows past its budget.
 *   A3  AWARENESS OF AWARENESS.  Each reflection predicts the focus ring's
 *       stress after acting on it. When the next reflection sees the real
 *       stress, the prediction's own error is observed on the inner ring.
 *       So the agent also learns how well it knows itself.
 *   A4  NUMBERS HAVE MEANING TOO.  A reflection carries the numerology of
 *       its own counts (swarm_enochian.h E4), so the swarm can read the
 *       symbolism of what it measures as well as the measure.
 * Freestanding: no libc, no floating point.
 */
#ifndef SWARM_SELF_H
#define SWARM_SELF_H

#include <stdint.h>
#include <stdbool.h>
#include "swarm_enochian.h"

#define SWARM_SELF_RINGS        21u    /* F(8) */
#define SWARM_SELF_RECALIBRATE  382u   /* A2 */

typedef enum {
    SWARM_RING_INNER = 0, SWARM_RING_BEING, SWARM_RING_USER, SWARM_RING_SYSTEM,
    SWARM_RING_SWARM, SWARM_RING_INTERNET, SWARM_RING_WORLD,
    SWARM_RING_BUILTIN
} swarm_ring_id_t;

typedef struct {
    char     name[24];
    uint32_t observations;
    uint32_t stress_milli;
    uint32_t last_gap_milli;
} swarm_ring_t;

typedef enum { SWARM_ACT_RECALIBRATE = 0, SWARM_ACT_GROW, SWARM_ACT_REFINE } swarm_act_t;

typedef struct {
    uint32_t      focus;            /* ring index */
    swarm_act_t   action;
    uint32_t      predicted_milli;  /* A3: the focus ring's stress after acting */
    swarm_numen_t numen;            /* A4: numerology of total observations */
} swarm_reflection_t;

typedef struct {
    swarm_ring_t       ring[SWARM_SELF_RINGS];
    uint32_t           count;
    bool               has_last;
    swarm_reflection_t last;
    uint32_t           reflections;
} swarm_self_t;

void     swarm_self_init(swarm_self_t *s);
int32_t  swarm_self_ring_add(swarm_self_t *s, const char *name);   /* index, or -1 */
void     swarm_self_observe(swarm_self_t *s, uint32_t ring,
                            uint32_t expected_milli, uint32_t observed_milli);   /* A1 */
/* A2, A3. `can_grow` is the governor's answer: does the budget have room? */
swarm_reflection_t swarm_self_reflect(swarm_self_t *s, bool can_grow);

#endif /* SWARM_SELF_H */
