/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cpu_lr35902.h — a Sharp LR35902 (the Game Boy / Game Boy Color CPU): an 8080/
 * Z80 hybrid. One core unlocks the enormous, deeply story-rich handheld slice
 * (Pokemon, Zelda: Link's Awakening, Final Fantasy Legend/Adventure, Kirby) that
 * the design wants for Chiglet's social/Sutra training.
 *
 * Vs the Z80 it DROPS the IX/IY index registers, the shadow register set, and
 * the port I/O space (I/O is memory-mapped at $FF00-$FF7F). It ADDS the (HL+)/
 * (HL-) auto-inc/dec loads, LDH ($FF00+n) accesses, ADD SP,r8 / LD HL,SP+r8,
 * STOP, and CB-prefixed SWAP. Flags are Z/N/H/C only (top nibble of F). Eleven
 * opcodes are undefined; they are counted in `illegal` (never crash the run).
 * Freestanding, integer-only. Bus is a 16-bit read/write callback pair. */
#ifndef ZXV_CPU_LR35902_H
#define ZXV_CPU_LR35902_H

#include <stdint.h>

#define LR_FLAG_Z 0x80u   /* zero       */
#define LR_FLAG_N 0x40u   /* subtract   */
#define LR_FLAG_H 0x20u   /* half-carry */
#define LR_FLAG_C 0x10u   /* carry      */

typedef struct cpu_lr35902 {
    uint8_t  a, f, b, c, d, e, h, l;   /* registers (F holds only Z/N/H/C)       */
    uint16_t sp, pc;
    uint8_t  ime;                      /* interrupt master enable                */
    uint8_t  halted;                   /* HALT until an interrupt                */
    uint8_t  stopped;                  /* STOP                                    */
    uint64_t cycles;                   /* machine cycles (T-states)              */
    uint8_t (*read)(struct cpu_lr35902 *c, uint16_t addr);
    void    (*write)(struct cpu_lr35902 *c, uint16_t addr, uint8_t val);
    void    *ctx;
    uint8_t  last_opcode;
    uint32_t illegal;                  /* undefined opcodes executed             */
} cpu_lr35902_t;

/* Reset to the post-boot-ROM state: PC=$0100, SP=$FFFE, AF=$01B0, etc. */
void     cpu_lr_reset(cpu_lr35902_t *c);

/* Request an interrupt vector ($40 VBlank, $48 STAT, $50 Timer, $58 Serial,
 * $60 Joypad) when IME is set. Returns 1 if taken (also wakes HALT). */
int      cpu_lr_interrupt(cpu_lr35902_t *c, uint16_t vector);

/* Execute one instruction; returns T-states it took. */
int      cpu_lr_step(cpu_lr35902_t *c);

/* Run until at least max_cycles elapse or the CPU stops. */
uint64_t cpu_lr_run(cpu_lr35902_t *c, uint64_t max_cycles);

#endif /* ZXV_CPU_LR35902_H */
