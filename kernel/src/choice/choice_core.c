/* choice_core.c — Choice-Collapse Scheduler (BIOS Stage 4)
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "m5_types.h"
#include "choice_core.h"

static collapse_t g_collapse_state = {{0, 0}};

static inline uint32_t collapse_bit(uint32_t bit) {
    return bit ? 1u : 0u;
}

static inline collapse_t collapse_vector(const collapse_t *src) {
    collapse_t dst = {{0, 0}};
    for (int i = 0; i < 2; i++) {
        uint32_t val = src->bits[i];
        uint32_t result = 0;
        for (int b = 0; b < 32; b++) {
            result |= collapse_bit(val & (1u << b)) << b;
        }
        dst.bits[i] = result;
    }
    return dst;
}

void choice_handoff(void) {
    g_collapse_state = collapse_vector(&g_collapse_state);
}

collapse_t choice_get_state(void) {
    return g_collapse_state;
}

void choice_set_state(const collapse_t *state) {
    g_collapse_state = *state;
}