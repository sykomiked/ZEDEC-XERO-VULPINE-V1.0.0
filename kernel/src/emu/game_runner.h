/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* game_runner.h — run an attached raw-ROM block device through the emulator
 * cores and report whether the game's code actually executes on ZEDEC. */
#ifndef ZXV_GAME_RUNNER_H
#define ZXV_GAME_RUNNER_H

#include <stdint.h>
#include "blockdev.h"

/* "Running" = a core retired at least this many instructions AND kept its
 * illegal-opcode rate below GR_ILLEGAL_MAX permille. Sustained count alone is
 * not enough: the 6502 treats illegal opcodes as NOPs and rarely jams, so
 * random data also runs the full budget — but at a ~50% illegal rate, whereas
 * real code is near 0%. The illegal RATE is the true discriminator. */
#define GR_RUNNING_MIN  8000u        /* min instructions retired            */
#define GR_ILLEGAL_MAX  100u         /* max illegal permille (100 = 10%)    */

typedef struct game_run {
    uint32_t bytes;                          /* ROM bytes read from the device */
    uint32_t insn_6502; uint32_t ill_6502; uint16_t pc_6502; uint8_t jam_6502;
    uint32_t insn_z80;  uint32_t ill_z80;  uint16_t pc_z80;  uint8_t jam_z80;
    uint16_t permille_6502;                  /* illegal rate, permille         */
    uint16_t permille_z80;
    uint32_t best_insn;                      /* instructions on the running core */
    uint16_t best_core;                      /* 6502 or 80 (0 if none)         */
    uint8_t  running;                        /* 1 if a core is genuinely running */
    /* NES machine result (when the image is iNES) — a REAL running verdict */
    uint8_t  is_nes;                         /* 1 if the image is an iNES ROM  */
    uint8_t  nes_running;                    /* 1 if the game came up running  */
    uint32_t nes_ppu_writes;                 /* PPU register writes            */
    uint32_t nes_vblank_polls;               /* $2002 reads                    */
    uint32_t nes_nmis;                       /* vblank NMIs taken              */
    /* SNES machine result (when the image is LoROM) — a REAL running verdict.
     * LoROM bank $00 (reset vector + init) sits in the first 32KB, so the 64KB
     * read is enough to run init and observe the game come alive. */
    uint8_t  is_snes;                        /* 1 if the image looks like LoROM */
    uint8_t  snes_running;                   /* 1 if the game came up running  */
    uint32_t snes_ppu_writes;                /* $2100-$213F writes             */
    uint32_t snes_cpu_reg_writes;            /* $4200-$421F writes             */
    uint32_t snes_nmis;                      /* vblank NMIs taken              */
    /* Game Boy machine result (image carries the Nintendo logo) */
    uint8_t  is_gb;                          /* 1 if a Game Boy cartridge      */
    uint8_t  gb_running;                     /* 1 if the game came up running  */
    uint32_t gb_io_writes;                   /* $FF00-$FF7F writes             */
    uint32_t gb_vblanks;                     /* VBlank interrupts taken        */
    uint8_t  gb_lcd_on;                      /* LCD was enabled                */
    /* Game Boy Advance machine result (image has a GBA cartridge header) */
    uint8_t  is_gba;                         /* 1 if a GBA cartridge           */
    uint8_t  gba_running;                    /* 1 if the game came up running  */
    uint32_t gba_io_writes;                  /* $04000000-$040003FF writes     */
    uint32_t gba_vcount_reads;               /* VCOUNT polls                   */
    uint32_t gba_vblank_irqs;                /* VBlank IRQs dispatched         */
} game_run_t;

/* Read up to 64KB of the raw device, execute it on the 6502 and Z80 cores, and
 * fill *out. Returns out->running (1 = the game's code is executing on ZEDEC). */
int game_runner_run(block_device_t *dev, game_run_t *out);

#endif /* ZXV_GAME_RUNNER_H */
