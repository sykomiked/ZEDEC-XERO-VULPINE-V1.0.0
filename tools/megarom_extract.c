/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* megarom_extract.c — Phase A of the MegaROM: run real game ROMs through the
 * emulator machines, break down each game's MECHANICS into an evidence vector,
 * and produce TWO corpora:
 *   (1) the deduplicated MECHANIC PALETTE (all runnable games; Chiglet-orthogonal
 *       distinct mechanics — the set the MegaROM composes from), and
 *   (2) the SOCIAL/STORY training set for Chiglet (story-capable games ONLY —
 *       NES/SNES-era+; Atari/arcade classics are pre-story and contribute
 *       mechanics only), written as Sutra-style exact rationals.
 * Online dedup: each game is compared against the GROWING palette (chg_interaction
 * ~0=redundant/parallel, ~1=orthogonal), so this scales to 144k without holding
 * all vectors at once. Host tool.
 *
 *   cc -Ikernel/src/emu -Ikernel/include -Ikernel/src/chiglet -Ikernel/src/surplus \
 *      tools/megarom_extract.c kernel/src/emu/{nes,sms,cpu6502,cpu_z80,game_universe,megarom_synth}.c \
 *      kernel/src/chiglet/chiglet.c kernel/src/surplus/surplus.c -lm -o /tmp/mrx
 *   /tmp/mrx <rom> [rom...]
 */
#include "nes.h"
#include "sms.h"
#include "game_universe.h"
#include "megarom_synth.h"
#include "chiglet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PALETTE_MAX 256
#define SR_TO_D(x) ((double)(x) / (double)SR_ONE)   /* surplus -> 0..1 for readout */

static nes_t g_nes;
static sms_t g_sms;

/* Run one ROM through the right machine; fill a game_run_t. Returns 1 if runnable. */
static int run_rom(const uint8_t *buf, uint32_t n, game_run_t *out){
    memset(out, 0, sizeof *out);
    out->bytes = n;
    if (nes_is_ines(buf, n) && nes_load_ines(&g_nes, buf, n)){
        nes_run(&g_nes, 300000u, 2000u);
        out->is_nes = 1; out->best_core = 6502;
        out->best_insn = g_nes.insn;
        out->permille_6502 = g_nes.insn ? (uint16_t)((uint64_t)g_nes.cpu.illegal*1000/g_nes.insn) : 1000;
        out->permille_z80 = 1000;
        out->nes_running = (uint8_t)nes_is_running(&g_nes);
        out->running = out->nes_running;
        out->nes_ppu_writes = g_nes.ppu_reg_writes;
        out->nes_vblank_polls = g_nes.ppu_status_reads;
        out->nes_nmis = g_nes.nmis_taken;
        return 1;
    }
    if (sms_load(&g_sms, buf, n)){
        sms_run(&g_sms, 3000000u, 15000u);
        out->best_core = 80;
        out->best_insn = g_sms.insn;
        out->permille_z80 = g_sms.insn ? (uint16_t)((uint64_t)g_sms.cpu.illegal*1000/g_sms.insn) : 1000;
        out->permille_6502 = 1000;
        out->running = (uint8_t)sms_is_running(&g_sms);
        /* map the SMS display/frame/interrupt metrics onto the vector's slots */
        out->nes_ppu_writes = g_sms.vdp_reg_writes;
        out->nes_vblank_polls = g_sms.vdp_status_reads;
        out->nes_nmis = g_sms.frame_ints;
        return out->running;   /* only count SMS games that came up running */
    }
    return 0;
}

int main(int argc, char **argv){
    static surplus_real_t palette[PALETTE_MAX][GU_DIM];
    int n_palette = 0, n_story = 0, n_run = 0, n_total = 0;
    static uint8_t buf[1 << 20];

    FILE *pf = fopen("/tmp/megarom_palette.sutra", "w");
    FILE *sf = fopen("/tmp/chiglet_social.sutra", "w");
    if (pf) fprintf(pf, "# MegaROM mechanic palette (distinct, Chiglet-orthogonal). dims: exec purity display frame irq content live arch\n");
    if (sf) fprintf(sf, "# Chiglet social/story training set (story-era games only), Sutra rationals\n");

    for (int i = 1; i < argc; i++){
        FILE *f = fopen(argv[i], "rb");
        if (!f) continue;
        size_t n = fread(buf, 1, sizeof buf, f); fclose(f);
        n_total++;
        game_run_t run;
        if (!run_rom(buf, (uint32_t)n, &run)) continue;   /* not runnable */
        n_run++;
        surplus_real_t v[GU_DIM];
        game_universe_vector(&run, v);

        /* online dedup into the mechanic palette */
        int redundant = 0;
        for (int j = 0; j < n_palette && !redundant; j++){
            surplus_real_t u = chg_interaction(v, palette[j], GU_DIM);
            if (u < SR_FROM_FLOAT(0.120)) redundant = 1;  /* interaction<0.12 => same mechanic */;
        }
        if (!redundant && n_palette < PALETTE_MAX){
            memcpy(palette[n_palette], v, sizeof v);
            n_palette++;
            if (pf){ const char *b=strrchr(argv[i],'/'); b=b?b+1:argv[i];
                fprintf(pf, "MECH");
                for (int d=0; d<GU_DIM; d++) fprintf(pf, " %.3f", SR_TO_D(v[d]));
                fprintf(pf, "  # %.48s\n", b); }
        }

        /* story-era games feed Chiglet's social/Sutra training set */
        if (gu_story_capable(v)){
            n_story++;
            if (sf){ const char *b=strrchr(argv[i],'/'); b=b?b+1:argv[i];
                fprintf(sf, "STORY");
                for (int d=0; d<GU_DIM; d++) fprintf(sf, " %.3f", SR_TO_D(v[d]));
                fprintf(sf, "  # %.48s\n", b); }
        }
    }
    if (pf) fclose(pf); if (sf) fclose(sf);
    printf("processed=%d runnable=%d | DISTINCT MECHANICS=%d | story/social contributors=%d\n",
           n_total, n_run, n_palette, n_story);
    printf("mechanic palette -> /tmp/megarom_palette.sutra   social/story set -> /tmp/chiglet_social.sutra\n");
    return 0;
}
