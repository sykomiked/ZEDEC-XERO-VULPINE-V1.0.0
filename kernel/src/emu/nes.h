/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* nes.h — a faithful-enough NES machine (mapper 0 / NROM) around the 6502 core.
 *
 * This is the first PER-CONSOLE machine, built to answer the real question the
 * bare CPU probe could not: is the GAME actually running? A machine gives the
 * 6502 its true environment — 2KB RAM (mirrored), the PPU/APU/controller
 * register map, PRG-ROM at $8000, the reset vector, and a vblank NMI — so real
 * game code follows its real path: it configures the PPU ($2000/$2001), polls
 * PPUSTATUS ($2002) for vblank, enables the APU ($4015/$4017), and settles into
 * an NMI-driven main loop. Random data does NONE of that. So the RUNNING signal
 * is behavioral (PPU configured + vblank observed / NMI taken), not a raw
 * instruction count — which is exactly what the CPU-only probe got wrong.
 *
 * Scope: mapper 0 (NROM) — 16KB/32KB PRG, no bank switching. That alone is a
 * large slice of the NES library. PPU rendering, CHR, and other mappers are not
 * modelled; we model the CPU-visible registers well enough to run init + main
 * loop and to OBSERVE that the game is alive. */
#ifndef ZXV_NES_H
#define ZXV_NES_H

#include <stdint.h>
#include "cpu6502.h"

typedef struct nes {
    cpu6502_t cpu;
    uint8_t   ram[2048];          /* $0000-$07FF, mirrored to $1FFF */
    uint8_t   prg[32768];         /* $8000-$FFFF                    */
    uint32_t  prg_mask;           /* 0x3FFF (16KB) or 0x7FFF (32KB) */
    uint8_t   ppu_ctrl, ppu_mask; /* last $2000 / $2001 written     */
    uint8_t   ppu_status;         /* $2002 (we drive the vblank bit) */
    /* behavioural observation — the RUNNING signal */
    uint32_t  ppu_reg_writes;     /* writes to $2000-$2007          */
    uint32_t  ppu_status_reads;   /* reads of $2002 (vblank polls)  */
    uint32_t  apu_io_writes;      /* writes to $4000-$4017          */
    uint32_t  nmis_taken;         /* vblank NMIs delivered          */
    uint32_t  insn;               /* instructions actually executed */
} nes_t;

/* True iff the image begins with the iNES magic "NES\x1A". */
int  nes_is_ines(const uint8_t *img, uint32_t len);

/* Load an iNES image (header + PRG). Returns 1 on success. */
int  nes_load_ines(nes_t *n, const uint8_t *img, uint32_t len);

/* Reset (PC from $FFFC) and run up to `budget` instructions, delivering a
 * vblank NMI every `nmi_period` instructions when the game enables NMIs. */
void nes_run(nes_t *n, uint32_t budget, uint32_t nmi_period);

/* Behavioural verdict: did the game actually come alive on the machine? */
int  nes_is_running(const nes_t *n);

/* On-target self-check: runs a built-in minimal NES program. Returns 1 on pass. */
int  nes_selfcheck(void);

#endif /* ZXV_NES_H */
