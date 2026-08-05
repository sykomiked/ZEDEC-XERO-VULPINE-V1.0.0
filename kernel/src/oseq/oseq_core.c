#include "m5_types.h"
#include "oseq_core.h"
#include <string.h>
#include <assert.h>

void oseq_init(oseq_state_t *state) {
    state->current_cycle = 0;
    state->num_devices = 0;
    memset(state->devices, 0, sizeof(state->devices));
}

ordinal_t oseq_register_device(oseq_state_t *state, const char *name) {
    assert(state->num_devices < OSEQ_MAX_DEVICES);
    assert(strlen(name) < 32);
    strcpy(state->devices[state->num_devices].name, name);
    state->devices[state->num_devices].ordinal = state->current_cycle;
    state->num_devices++;
    state->current_cycle++;
    return state->devices[state->num_devices - 1].ordinal;
}

bool oseq_is_valid_ordinal(const ordinal_t ordinal) {
    return ordinal < UINT64_MAX;
}

bool oseq_is_later(const ordinal_t a, const ordinal_t b) {
    return a > b;
}
