/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* cpu6502.h — the first core of the ZEDEC pqOS unified emulator: a MOS 6502.
 *
 * One 6502 core covers the LARGEST slice of the legacy corpus — NES, Atari
 * 2600/5200/7800, Commodore 64/VIC-20, Apple II, and more. It is headless
 * (CPU + bus only, no PPU/APU): the Game Master feeds a ROM's bytes through it
 * as a DATASET and the kernel logs any fault the execution stream provokes.
 * Freestanding + integer-only (no libc, no float). The bus is a callback pair
 * so the same core maps onto every 6502 system by swapping read/write.
 */
#ifndef ZXV_CPU6502_H
#define ZXV_CPU6502_H

#include <stdint.h>
#include <stdbool.h>

/* Status flags (P register): NV-BDIZC */
#define CPU6502_C 0x01u   /* carry            */
#define CPU6502_Z 0x02u   /* zero             */
#define CPU6502_I 0x04u   /* IRQ disable      */
#define CPU6502_D 0x08u   /* decimal mode     */
#define CPU6502_B 0x10u   /* break            */
#define CPU6502_U 0x20u   /* unused (always 1)*/
#define CPU6502_V 0x40u   /* overflow         */
#define CPU6502_N 0x80u   /* negative         */

typedef struct cpu6502 {
    uint8_t  a, x, y, sp, p;   /* accumulator, index X/Y, stack ptr, status  */
    uint16_t pc;               /* program counter                            */
    uint64_t cycles;           /* total cycles executed                      */
    /* Bus: every memory access goes through these so the core is system-
     * agnostic. ctx is the bus/system state (e.g. a 64KB RAM array). */
    uint8_t (*read)(struct cpu6502 *c, uint16_t addr);
    void    (*write)(struct cpu6502 *c, uint16_t addr, uint8_t val);
    void    *ctx;
    /* Diagnostics for the Game Master fault log. */
    uint8_t  jammed;           /* set on a KIL/illegal-jam opcode            */
    uint8_t  last_opcode;
} cpu6502_t;

/* Load PC from the reset vector at $FFFC/$FFFD; init SP=$FD, I set. */
void     cpu6502_reset(cpu6502_t *c);

/* Deliver an IRQ / NMI (edge). Return true if taken. */
void     cpu6502_irq(cpu6502_t *c);
void     cpu6502_nmi(cpu6502_t *c);

/* Execute exactly one instruction. Returns the cycle count it took (0 if the
 * CPU is jammed). Updates c->cycles. */
int      cpu6502_step(cpu6502_t *c);

/* Run until at least max_cycles have elapsed or the CPU jams. Returns cycles
 * actually run. */
uint64_t cpu6502_run(cpu6502_t *c, uint64_t max_cycles);

#endif /* ZXV_CPU6502_H */
