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

/* ---- DECLARATION -----------------------------------------------------------
 * oseq is what makes the whole event-space model legal: it decides
 * happens-before WITHOUT a clock, which is exactly the property the readiness
 * fixpoint relies on. It includes nothing but its own types, so it requires
 * nothing.
 *
 * The bring-up checks the order relation itself rather than any registry state
 * (the registry instance is owned by whoever calls oseq_init). A relation that
 * is not strict -- a > b and b > a both true -- would make happens-before
 * meaningless while every caller kept working, which is the failure mode worth
 * catching here. */
#include "zxv_decl.h"

static int oseq_bringup(void) {
    if (!oseq_is_valid_ordinal((ordinal_t)1)) return -1;
    if (!oseq_is_later((ordinal_t)2, (ordinal_t)1)) return -1;
    if (oseq_is_later((ordinal_t)1, (ordinal_t)2)) return -1;   /* antisymmetry */
    if (oseq_is_later((ordinal_t)1, (ordinal_t)1)) return -1;   /* irreflexive  */
    return 0;
}

ZXV_DECLARE(oseq,
    ZXV_PROVIDES(oseq_ready),
    ZXV_REQUIRES_NONE,
    ZXV_BRINGUP(oseq_bringup));
