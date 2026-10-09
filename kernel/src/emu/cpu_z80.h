/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cpu_z80.h — core #2 of the unified emulator: a Zilog Z80.
 *
 * The Z80 exists here to be *non-compatible* with the 6502 on every axis that
 * matters: a different register file (A/F,B/C,D/E,H/L + SP), a different flag
 * model (S Z 5 H 3 P/V N C — parity/overflow, half-carry, add-subtract — NONE
 * of which line up with the 6502's N V - B D I Z C), a different reset vector
 * (PC=0, not $FFFC), and a different memory idiom. That orthogonality is the
 * point: running a Z80 and a 6502 in parallel and measuring their RELATIONSHIP
 * is how the kernel's non-binary logic states (LPRES four-valued / Tri-Space /
 * phase) get exercised. See emu_relate.c.
 *
 * Scope: the base 8-bit ISA plus the CB and ED prefix pages and port I/O —
 * 8/16-bit loads, the full A-ALU group, INC/DEC, ADD/ADC/SBC HL,dd, rotates,
 * JP/JR/DJNZ/CALL/RET (+ conditionals), PUSH/POP, EX DE,HL / EX AF,AF' / EXX /
 * EX (SP),HL, SCF/CCF/CPL/DI/EI/IM/NOP/HALT, IN/OUT (n) and (C), CB (RLC..SRL,
 * BIT/RES/SET), ED (LDI/LDD/LDIR/LDDR, IM 0/1/2, NEG, 16-bit ADC/SBC HL,
 * LD (nn),dd / LD dd,(nn), RETI/RETN, LD A,I/R), and cpu_z80_int() for the
 * maskable interrupt (IM1 = RST $38). The DD/FD (IX/IY) prefix pages are NOT
 * yet decoded — flagged (illegal++) and skipped so the Game Master logs the gap
 * rather than crashing. Freestanding + integer-only. Bus + port I/O are callback
 * pairs, so the same CPU maps onto every Z80 system by swapping them.
 */
#ifndef ZXV_CPU_Z80_H
#define ZXV_CPU_Z80_H

#include <stdint.h>
#include <stdbool.h>

/* Flag register (F) bits — SZ5H3PNC. Deliberately unlike the 6502's. */
#define Z80_C  0x01u   /* carry                      */
#define Z80_N  0x02u   /* add/subtract (1 after SUB) */
#define Z80_PV 0x04u   /* parity / overflow          */
#define Z80_F3 0x08u   /* undocumented copy of bit 3 */
#define Z80_H  0x10u   /* half-carry (bit 3->4)      */
#define Z80_F5 0x20u   /* undocumented copy of bit 5 */
#define Z80_Z  0x40u   /* zero                       */
#define Z80_S  0x80u   /* sign (bit 7)               */

typedef struct cpu_z80 {
    uint8_t  a, f, b, c, d, e, h, l;   /* main register file            */
    uint8_t  a2, f2, b2, c2, d2, e2, h2, l2; /* alternate set (EX AF/EXX) */
    uint16_t ix, iy;                   /* index registers (DD/FD)        */
    uint8_t  i, r;                     /* interrupt vector / refresh     */
    uint16_t sp, pc;                   /* stack pointer, program counter */
    uint8_t  iff1, iff2;               /* interrupt enable latches       */
    uint8_t  im;                       /* interrupt mode 0/1/2           */
    uint8_t  halted;                   /* set by HALT                    */
    uint64_t cycles;                   /* total T-states executed        */
    uint8_t (*read)(struct cpu_z80 *c, uint16_t addr);
    void    (*write)(struct cpu_z80 *c, uint16_t addr, uint8_t val);
    /* Port I/O (Z80 IN/OUT). NULL => IN reads 0xFF, OUT is a no-op. */
    uint8_t (*in)(struct cpu_z80 *c, uint16_t port);
    void    (*out)(struct cpu_z80 *c, uint16_t port, uint8_t val);
    void    *ctx;
    /* Game Master diagnostics. */
    uint8_t  jammed;
    uint8_t  last_opcode;
    uint32_t illegal;                  /* undecoded/prefixed opcodes seen */
} cpu_z80_t;

/* Deliver a maskable interrupt (IM1 = RST $38). Taken only if iff1 set. */
void cpu_z80_int(cpu_z80_t *c);

/* Reset: PC=0, SP=$FFFF, interrupts disabled, flags cleared. */
void     cpu_z80_reset(cpu_z80_t *c);

/* Execute one instruction. Returns T-states taken (0 if jammed). */
int      cpu_z80_step(cpu_z80_t *c);

/* Run until >= max_cycles T-states elapse, or HALT/jam. Returns T-states run. */
uint64_t cpu_z80_run(cpu_z80_t *c, uint64_t max_cycles);

#endif /* ZXV_CPU_Z80_H */
