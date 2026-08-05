/* dharana.h — 112 Non-Linear Gate Array (Vijnana Bhairava Frame)
 *
 * Uses the 112 dharanas of the Vijnana Bhairava Tantra as a symbolic
 * naming/organizational frame (per explicit "talking symbolically"
 * intent) for 112 gate instances, grouped into 4 distinctly-real
 * functional classes of 28. No claim is made that meditative states
 * are physically equivalent to circuit behavior -- the naming is
 * structural, the mathematics underneath is ordinary and checkable.
 *
 * phi(112) = 112 * (1/2) * (6/7) = 48   (112 = 2^4 * 7), verified.
 *
 * Honesty note on Class A ("zero phase lag"): a true continuous-time
 * Hilbert-transform phase detector is non-causal in its ideal form
 * and needs windowing/IIR approximation for real-time use -- genuine
 * causal implementations always have SOME latency, proportional to
 * filter length, not literally zero. Class A here uses an exact,
 * causal, discrete substitute: sign-change + finite-difference
 * "velocity" thresholding, which needs only the current and previous
 * sample (minimum possible causal latency: one sample).
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef DHARANA_H
#define DHARANA_H

#include "m5_types.h"

#define DHARANA_COUNT      112
#define DHARANA_CLASS_SIZE 28
#define DHARANA_DIM        48  /* phi(112) */

typedef enum {
    DHARANA_CLASS_SANDHI       = 0, /* gates 1-28:   zero-crossing / phase-inversion */
    DHARANA_CLASS_VISRANTI     = 1, /* gates 29-56:  fixed-point attractor */
    DHARANA_CLASS_DVAITADVAITA = 2, /* gates 57-84:  paraconsistent polarity resolution */
    DHARANA_CLASS_SUNYA        = 3, /* gates 85-112: void / high-impedance reset */
} dharana_class_t;

static inline dharana_class_t dharana_class_of(uint32_t gate_id /* 1..112 */) {
    return (dharana_class_t)((gate_id - 1) / DHARANA_CLASS_SIZE);
}
const char *dharana_class_name(dharana_class_t c);
const char *dharana_gate_name(uint32_t gate_id); /* e.g. "Sandhi-7"; NOT thread-safe (static buffer) */

/* ---- Class A: Sandhi (zero-crossing) gates ----
 * Fires when two consecutive samples have strictly opposite sign AND
 * the finite-difference magnitude |sample - prev| >= epsilon (the
 * discrete analogue of v=0, |dv/dt| > epsilon). */
typedef struct sandhi_gate {
    uint32_t gate_id;
    rational_t prev;
    bool has_prev;
    rational_t epsilon;
    uint32_t crossing_count;
} sandhi_gate_t;

void sandhi_init(sandhi_gate_t *g, uint32_t gate_id, rational_t epsilon);
bool sandhi_feed(sandhi_gate_t *g, rational_t sample); /* true iff this sample completes a crossing */

/* ---- Class B: Visranti (fixed-point attractor) gates ----
 * Damped fixed-point iteration x_{n+1} = (x_n + attractor)/2, a real
 * contraction map with ratio 1/2 (error halves every iteration) that
 * converges exactly (in exact rational arithmetic) to within any
 * rational tolerance in a bounded number of steps. */
typedef struct visranti_gate {
    uint32_t gate_id;
    rational_t attractor;
    rational_t tolerance;
    uint32_t max_iters;
} visranti_gate_t;

void visranti_init(visranti_gate_t *g, uint32_t gate_id, rational_t attractor,
                    rational_t tolerance, uint32_t max_iters);
rational_t visranti_settle(const visranti_gate_t *g, rational_t noisy_input, uint32_t *iters_used);

/* ---- Class C: Dvaitadvaita (paraconsistent polarity) gates ----
 * Thin wrapper around the EXISTING trit_t glut logic in m5_types.h --
 * merges two conflicting trits via trit_charge summation rather than
 * inventing new paraconsistent semantics that would compete with the
 * kernel's real L6 (trit_t) logic. */
typedef struct dvaitadvaita_gate {
    uint32_t gate_id;
} dvaitadvaita_gate_t;

void dvaitadvaita_init(dvaitadvaita_gate_t *g, uint32_t gate_id);
trit_t dvaitadvaita_resolve(const dvaitadvaita_gate_t *g, trit_t a, trit_t b);

/* ---- Class D: Sunya (void/reset) gates ----
 * "Flushes... without losing data": returns the pre-reset snapshot
 * to the caller before clearing the live register to substrate zero. */
typedef struct sunya_gate {
    uint32_t gate_id;
    rational_t register_value;
    bool live;
} sunya_gate_t;

void sunya_init(sunya_gate_t *g, uint32_t gate_id);
void sunya_write(sunya_gate_t *g, rational_t value);
rational_t sunya_reset(sunya_gate_t *g); /* clears to zero; returns pre-reset snapshot */

/* ---- The full 112-gate array ---- */
typedef struct dharana_array {
    sandhi_gate_t       sandhi[DHARANA_CLASS_SIZE];
    visranti_gate_t     visranti[DHARANA_CLASS_SIZE];
    dvaitadvaita_gate_t dvaitadvaita[DHARANA_CLASS_SIZE];
    sunya_gate_t        sunya[DHARANA_CLASS_SIZE];
} dharana_array_t;

void dharana_array_init(dharana_array_t *arr);

#endif /* DHARANA_H */
