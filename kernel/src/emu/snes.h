/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* snes.h — a faithful-enough SNES / Super Famicom machine (LoROM) around the
 * 65816 core. The 16-bit era is where the corpus gets STORY-rich, so this is the
 * machine that feeds the deepest material to Chiglet's social/Sutra training.
 *
 * Like the NES machine, it answers "is the GAME actually running?" behaviorally.
 * A real SNES game's reset code has a recognizable shape: SEI/CLC/XCE into native
 * mode, REP/SEP to size the registers, set the stack, then force-blank the screen
 * ($2100), configure the PPU ($2101-$213F), and enable the vblank NMI ($4200 bit
 * 7) before settling into an NMI-driven loop. Random data does none of that. So
 * RUNNING = (native mode entered + PPU configured + NMI enabled + NMIs taken +
 * low illegal-opcode rate), not a raw instruction count.
 *
 * Scope: LoROM mapping, WRAM ($7E-$7F + the $0000-$1FFF mirror), and the
 * CPU-visible PPU/CPU register writes needed to run init + observe life. HiROM,
 * SPC700 audio, real PPU rendering, DMA/HDMA transfer effects, and coprocessors
 * are not modelled. */
#ifndef ZXV_SNES_H
#define ZXV_SNES_H

#include <stdint.h>
#include "cpu65816.h"

#define SNES_ROM_CAP  0x80000   /* 512KB window — bank-0 init always fits       */
#define SNES_WRAM     0x20000   /* 128KB $7E0000-$7FFFFF                          */

typedef struct snes {
    cpu65816_t cpu;
    uint8_t    wram[SNES_WRAM];
    uint8_t    rom[SNES_ROM_CAP];
    uint32_t   rom_size;          /* actual bytes copied (<= cap)                 */
    /* CPU-visible register shadow + behavioural observation */
    uint8_t    inidisp;           /* last $2100                                   */
    uint8_t    nmitimen;          /* last $4200                                   */
    uint8_t    vblank;            /* our driven vblank/NMI flag ($4210 bit7)      */
    uint32_t   ppu_writes;        /* writes to $2100-$213F                        */
    uint32_t   cpu_reg_writes;    /* writes to $4200-$421F                        */
    uint32_t   nmis_taken;        /* vblank NMIs delivered                        */
    uint32_t   insn;              /* instructions executed                        */
    uint8_t    nmi_enabled;       /* $4200 bit7 latched                           */
    /* Minimal APU ($2140-$2143) handshake stub. Real SNES games upload a program
     * to the SPC700 and spin on this port protocol before enabling NMI; with no
     * audio CPU the reads would read 0 forever and the game hangs in init. We
     * echo writes and present the IPL "ready" signal ($AA/$BB) so the handshake
     * completes and the game proceeds. This is a STUB, not an SPC700. */
    uint8_t    apu[4];            /* $2140-$2143 latches ($AA/$BB ready at reset)  */
    uint32_t   apu_reads;         /* diagnostics: reads of the APU ports          */
    uint8_t    settled;           /* CPU ended in a tight loop (not wandering)    */
} snes_t;

/* Heuristic: does this image look like a LoROM SNES ROM? (copier header aware) */
int  snes_is_lorom(const uint8_t *img, uint32_t len);

/* Load a ROM image (strips a 512-byte copier header if present). Returns 1 ok. */
int  snes_load(snes_t *s, const uint8_t *img, uint32_t len);

/* Reset and run up to `budget` instructions, delivering a vblank NMI every
 * `nmi_period` instructions once the game enables NMIs. */
void snes_run(snes_t *s, uint32_t budget, uint32_t nmi_period);

/* Behavioural verdict: did the game come alive on the machine? */
int  snes_is_running(const snes_t *s);

/* On-target self-check: runs a built-in minimal LoROM program. Returns 1 on pass. */
int  snes_selfcheck(void);

#endif /* ZXV_SNES_H */
