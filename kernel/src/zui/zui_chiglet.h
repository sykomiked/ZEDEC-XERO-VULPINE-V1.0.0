/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_chiglet.h — the companion, drawn from its own DNA.
 *
 * Every person's Chiglet carries 8 genes (swarm/swarm_dna.h: home emotion,
 * temperament, curiosity, caution, warmth, pace, verbosity, specialty) and a
 * generation count that the evolution ratchet (swarm_evolve.h V1) raises as
 * it improves with its person. zui_chiglet_draw() turns exactly those bytes
 * into the companion's on-screen form, so its look is not a costume picked
 * from a menu: it IS its genome, and it changes when the genome does.
 *
 *   temperament -> body roundness        curiosity  -> antennae (0..3)
 *   caution     -> shield ring weight    warmth     -> warm/cool body tint
 *   pace        -> orbiting motes        verbosity  -> mouth width
 *   home emotion-> accent colour         specialty  -> chest emblem
 *   generation  -> halo rings (one per evolution step shown, up to 4)
 *
 * The mood drawn on its face is the Chiglet runtime's honest state: DECIDED
 * (steady eyes), UNCERTAIN (one eye narrowed, the "not sure" face the runtime
 * returns when its evidence is not independent enough), SPEAKING (open mouth).
 *
 * Deterministic: same genes, generation, mood and tick -> same pixels.
 */
#ifndef ZXV_ZUI_CHIGLET_H
#define ZXV_ZUI_CHIGLET_H

#include <stdint.h>
#include "zui_canvas.h"

typedef enum {
    ZUI_CHG_DECIDED = 0,
    ZUI_CHG_UNCERTAIN,
    ZUI_CHG_SPEAKING
} zui_chg_mood_t;

typedef struct {
    uint8_t        genes[8];
    uint32_t       generation;
    zui_chg_mood_t mood;
} zui_chiglet_t;

/* Draw centred at (cx, cy), body radius r pixels. tick animates the motes. */
void zui_chiglet_draw(zui_surface_t *s, const zui_chiglet_t *c,
                      int32_t cx, int32_t cy, int32_t r, uint32_t tick);

/* The accent colour its home-emotion gene selects (for its speech bubble). */
uint32_t zui_chiglet_accent(const zui_chiglet_t *c);

#endif /* ZXV_ZUI_CHIGLET_H */
