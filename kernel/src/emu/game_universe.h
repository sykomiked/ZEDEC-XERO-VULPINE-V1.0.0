/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* game_universe.h — a game ROM as a STATE/UNIVERSE: its mechanics as an 8-dim
 * evidence vector fed to Chiglet, which holds many at once (the multiverse) and
 * counts the DISTINCT ones. First stone of the MegaROM (mechanic extraction +
 * dedup). See game_universe.c. GU_DIM must equal CHG_DIM. */
#ifndef ZXV_GAME_UNIVERSE_H
#define ZXV_GAME_UNIVERSE_H

#include "surplus.h"
#include "game_runner.h"

#define GU_DIM 8   /* == CHG_DIM: the mechanics evidence dimension */

/* Turn a game's emulator run into its 8-dim mechanics evidence vector. */
void game_universe_vector(const game_run_t *run, surplus_real_t out[GU_DIM]);

/* Prove the multiverse holds several game universes, counts the distinct ones,
 * and collapses a duplicate. Returns 1 on pass; reports the distinct counts. */
int game_universe_selfcheck(uint32_t *distinct3_out, uint32_t *distinct4_out);

#endif /* ZXV_GAME_UNIVERSE_H */
