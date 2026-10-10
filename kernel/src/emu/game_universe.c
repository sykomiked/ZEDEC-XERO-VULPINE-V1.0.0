/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* game_universe.c — each game ROM is its own STATE/UNIVERSE (rules + mechanics,
 * and for later eras a story of character relationships). This turns a game's
 * emulator run into an 8-dim MECHANICS evidence vector and feeds a set of them
 * to Chiglet, which holds them SIMULTANEOUSLY (a multiverse) and — via its
 * Interaction Surplus Framework — collapses redundant mechanics and counts the
 * genuinely DISTINCT universes (k_distinct / R). That is precisely the first
 * step of the MegaROM: extract every game's mechanics, keep the distinct best,
 * drop the duplicates. The orthogonality Chiglet weights by is the same
 * perpendicularity the M5 substrate and the congruence gate are built on.
 *
 * The 8 mechanics dimensions (each normalised to [0,1]):
 *   0 exec        — how much real code the game sustains (aliveness)
 *   1 purity      — fraction of legal opcodes (code vs data)
 *   2 display     — graphics-register programming (PPU/VDP config)
 *   3 frame_sync  — vblank polling (animation / timing discipline)
 *   4 interrupt   — NMI / frame-IRQ driven main loop (real-time structure)
 *   5 content     — ROM size (a proxy for story/content capacity)
 *   6 liveness    — did it actually come up running on its machine
 *   7 arch        — the universe's "physics": which CPU family
 */
#include "game_universe.h"
#include "game_runner.h"
#include "chiglet.h"
#include "surplus.h"

static surplus_real_t clamp01_ratio(uint64_t num, uint64_t den){
    if (den == 0) return SR_ZERO;
    if (num >= den) return SR_ONE;
    /* exact rational -> surplus_real_t (Sutra speaks ratios) */
    return SR_DIV(SR_FROM_INT((int64_t)num), SR_FROM_INT((int64_t)den));
}

void game_universe_vector(const game_run_t *run, surplus_real_t out[GU_DIM]){
    uint32_t permille = run->permille_6502 < run->permille_z80 ? run->permille_6502 : run->permille_z80;
    out[0] = clamp01_ratio(run->best_insn, 200000u);                 /* exec        */
    out[1] = SR_SUB(SR_ONE, clamp01_ratio(permille, 1000u));         /* purity      */
    out[2] = clamp01_ratio(run->nes_ppu_writes, 8000u);              /* display     */
    out[3] = clamp01_ratio(run->nes_vblank_polls, 40000u);           /* frame_sync  */
    out[4] = clamp01_ratio(run->nes_nmis, 150u);                     /* interrupt   */
    out[5] = clamp01_ratio(run->bytes, 65536u);                      /* content     */
    out[6] = run->running ? SR_FROM_FLOAT(0.8) : SR_FROM_FLOAT(0.2); /* liveness    */
    out[7] = (run->best_core == 6502) ? SR_FROM_FLOAT(0.3)           /* arch        */
           : (run->best_core == 80)   ? SR_FROM_FLOAT(0.7)
                                      : SR_FROM_FLOAT(0.5);
}

/* Self-check: prove the multiverse holds several game universes at once, counts
 * the DISTINCT ones, and collapses a duplicate — the mechanic-dedup that the
 * MegaROM is built on. Returns 1 on pass. Uses archetypal mechanics vectors
 * (a maze game, a story RPG, a shooter) + a near-duplicate of the RPG. */
int game_universe_selfcheck(uint32_t *distinct3_out, uint32_t *distinct4_out){
    /* dominant-mechanic profiles chosen to be genuinely different universes */
    surplus_real_t ev[4][GU_DIM] = {
        /* maze:   display+frame, tiny content, 6502            */
        { SR_FROM_FLOAT(0.7),SR_FROM_FLOAT(0.9),SR_FROM_FLOAT(0.8),SR_FROM_FLOAT(0.8),
          SR_FROM_FLOAT(0.2),SR_FROM_FLOAT(0.1),SR_FROM_FLOAT(0.8),SR_FROM_FLOAT(0.3) },
        /* rpg:    content-heavy, interrupt-driven, big         */
        { SR_FROM_FLOAT(0.7),SR_FROM_FLOAT(0.9),SR_FROM_FLOAT(0.4),SR_FROM_FLOAT(0.3),
          SR_FROM_FLOAT(0.8),SR_FROM_FLOAT(0.9),SR_FROM_FLOAT(0.8),SR_FROM_FLOAT(0.3) },
        /* shooter: display+io heavy, Z80 physics              */
        { SR_FROM_FLOAT(0.7),SR_FROM_FLOAT(0.9),SR_FROM_FLOAT(0.9),SR_FROM_FLOAT(0.2),
          SR_FROM_FLOAT(0.5),SR_FROM_FLOAT(0.3),SR_FROM_FLOAT(0.8),SR_FROM_FLOAT(0.7) },
        /* rpg CLONE: ~= rpg (a duplicate universe -> must collapse) */
        { SR_FROM_FLOAT(0.71),SR_FROM_FLOAT(0.89),SR_FROM_FLOAT(0.41),SR_FROM_FLOAT(0.31),
          SR_FROM_FLOAT(0.79),SR_FROM_FLOAT(0.9),SR_FROM_FLOAT(0.81),SR_FROM_FLOAT(0.3) },
    };
    uint32_t d3 = 0, d4 = 0;
    (void)chg_effective_experts((const surplus_real_t (*)[CHG_DIM])ev, 3u, GU_DIM, &d3);
    (void)chg_effective_experts((const surplus_real_t (*)[CHG_DIM])ev, 4u, GU_DIM, &d4);
    if (distinct3_out) *distinct3_out = d3;
    if (distinct4_out) *distinct4_out = d4;
    /* pairwise relationships: two different universes are MORE independent than
     * an rpg and its clone. */
    surplus_real_t i_maze_rpg  = chg_interaction(ev[0], ev[1], GU_DIM);
    surplus_real_t i_rpg_clone = chg_interaction(ev[1], ev[3], GU_DIM);
    /* PASS: adding the clone does NOT add a distinct universe, AND distinct
     * universes are more independent than a universe and its duplicate. */
    return (d4 == d3) && (i_maze_rpg > i_rpg_clone) ? 1 : 0;
}
