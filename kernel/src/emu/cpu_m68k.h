/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* cpu_m68k.h — a Motorola 68000: the Sega Genesis / Mega Drive main CPU (also
 * the Amiga, Atari ST, early Mac, and many arcade boards). One core unlocks the
 * story-rich 16-bit Sega library (Phantasy Star IV, Shining Force, Story of Thor,
 * Landstalker) for Chiglet's training.
 *
 * BIG-ENDIAN (opposite of every other core so far) — the bus callbacks read and
 * write in big-endian order. 32-bit registers: eight data (D0-D7) and eight
 * address (A0-A7, A7 = SP with banked USP/SSP). Byte/word/long operation sizes;
 * a rich, orthogonal set of addressing modes. The common instructions game code
 * uses are implemented; rare/undefined encodings are counted in `illegal` and
 * never crash. Freestanding, integer-only. */
#ifndef ZXV_CPU_M68K_H
#define ZXV_CPU_M68K_H

#include <stdint.h>

/* SR flags */
#define M68_C 0x0001u
#define M68_V 0x0002u
#define M68_Z 0x0004u
#define M68_N 0x0008u
#define M68_X 0x0010u
#define M68_S 0x2000u   /* supervisor */
#define M68_T 0x8000u   /* trace */

typedef struct cpu_m68k {
    uint32_t d[8];       /* data registers                                      */
    uint32_t a[8];       /* address registers; a[7] is the active stack pointer */
    uint32_t usp, ssp;   /* inactive stack pointers                             */
    uint32_t pc;
    uint16_t sr;
    uint64_t cycles;
    /* big-endian bus over the 24-bit address space */
    uint8_t  (*read8) (struct cpu_m68k *c, uint32_t addr);
    uint16_t (*read16)(struct cpu_m68k *c, uint32_t addr);
    uint32_t (*read32)(struct cpu_m68k *c, uint32_t addr);
    void     (*write8) (struct cpu_m68k *c, uint32_t addr, uint8_t v);
    void     (*write16)(struct cpu_m68k *c, uint32_t addr, uint16_t v);
    void     (*write32)(struct cpu_m68k *c, uint32_t addr, uint32_t v);
    void    *ctx;
    uint8_t  stopped;    /* STOP instruction                                    */
    uint8_t  last_group; /* top nibble of the last opcode (diagnostics)         */
    uint32_t illegal;    /* unimplemented / illegal encodings                   */
} cpu_m68k_t;

/* Reset: read SP from $000000 and PC from $000004, enter supervisor mode. */
void     cpu_m68k_reset(cpu_m68k_t *c);

/* Raise an interrupt at `level` (1-7); if above the mask, vector through the
 * autovector table. Returns 1 if taken. */
int      cpu_m68k_irq(cpu_m68k_t *c, int level);

/* Execute one instruction; returns cycles (approximate). */
int      cpu_m68k_step(cpu_m68k_t *c);

/* Run until at least max_cycles elapse or the CPU stops. */
uint64_t cpu_m68k_run(cpu_m68k_t *c, uint64_t max_cycles);

#endif /* ZXV_CPU_M68K_H */
