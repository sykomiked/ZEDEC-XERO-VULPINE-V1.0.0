/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* cpu65816.h — a WDC 65C816 core: the 6502's 16-bit superset. One core unlocks
 * the SNES / Super Famicom slice of the corpus (the story-rich 16-bit era the
 * design specifically wants for Chiglet's social/Sutra training).
 *
 * Over the 6502 it adds: 16-bit A/X/Y whose width is chosen at runtime by the M
 * (accumulator) and X (index) status bits; a 24-bit address space (program bank
 * PBR + data bank DBR); a movable direct page (D); a 16-bit stack; and a hidden
 * EMULATION flag E toggled by XCE that snaps the chip back to strict 6502
 * behaviour on reset. Every one of the 256 opcodes is decoded for its correct
 * LENGTH (via an addressing-mode table, with the immediate width folded in), so
 * the program counter can never desync; the common operations real boot/init
 * code uses are executed, and any operation left as a stub is counted in
 * `illegal` (never a crash, never a skip). Freestanding, integer-only. The bus
 * is a 24-bit read/write callback pair so one core maps onto every 65816 system.
 */
#ifndef ZXV_CPU65816_H
#define ZXV_CPU65816_H

#include <stdint.h>
#include <stdbool.h>

/* Status flags (P): NVMXDIZC in native mode. In emulation mode M/X read as the
 * 6502 unused/break bits but the core forces 8-bit A/X/Y regardless. */
#define CPU816_C 0x01u   /* carry                                  */
#define CPU816_Z 0x02u   /* zero                                   */
#define CPU816_I 0x04u   /* IRQ disable                            */
#define CPU816_D 0x08u   /* decimal                                */
#define CPU816_X 0x10u   /* index width: 1 = 8-bit X/Y             */
#define CPU816_M 0x20u   /* accumulator width: 1 = 8-bit A         */
#define CPU816_V 0x40u   /* overflow                               */
#define CPU816_N 0x80u   /* negative                               */

typedef struct cpu65816 {
    uint16_t a, x, y;      /* accumulator, index X/Y (low 8 in 8-bit mode) */
    uint16_t sp;           /* stack pointer (16-bit; page 1 in emu mode)   */
    uint16_t d;            /* direct page register                         */
    uint16_t pc;           /* program counter within bank PBR              */
    uint8_t  pbr, dbr;     /* program bank, data bank                       */
    uint8_t  p;            /* status flags                                  */
    uint8_t  e;            /* 1 = emulation (6502) mode, 0 = native         */
    uint64_t cycles;
    /* 24-bit bus. addr is a full bank:offset. */
    uint8_t (*read)(struct cpu65816 *c, uint32_t addr);
    void    (*write)(struct cpu65816 *c, uint32_t addr, uint8_t val);
    void    *ctx;
    /* Diagnostics for the Game Master fault log / running verdict. */
    uint8_t  stopped;      /* set by STP (and latched by WAI until IRQ)     */
    uint8_t  last_opcode;
    uint32_t illegal;      /* operations decoded but left as a stub (as NOP)*/
} cpu65816_t;

/* True iff A is currently 8-bit (emulation mode, or M set). */
static inline int cpu816_acc8(const cpu65816_t *c){ return c->e || (c->p & CPU816_M); }
/* True iff X/Y are currently 8-bit. */
static inline int cpu816_idx8(const cpu65816_t *c){ return c->e || (c->p & CPU816_X); }

/* Reset: E=1 (emulation), PC from $00FFFC/D, SP high byte forced to $01, M/X set,
 * D=0, DBR/PBR=0, I set, D(ecimal) cleared. */
void     cpu65816_reset(cpu65816_t *c);

/* Deliver an IRQ / NMI (native vectors $00FFEE/$00FFEA, emulation $00FFFE/$00FFFA). */
void     cpu65816_irq(cpu65816_t *c);
void     cpu65816_nmi(cpu65816_t *c);

/* Execute exactly one instruction; returns cycles it took (approx). */
int      cpu65816_step(cpu65816_t *c);

/* Run until at least max_cycles elapse or the CPU stops. Returns cycles run. */
uint64_t cpu65816_run(cpu65816_t *c, uint64_t max_cycles);

#endif /* ZXV_CPU65816_H */
