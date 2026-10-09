/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cpu_huc6280.h — a Hudson HuC6280: the PC Engine / TurboGrafx-16 CPU. A 65C02
 * superset, so it inherits the whole 6502/65C02 instruction set and adds:
 *   - eight Memory Paging Registers (MPR) that map the 16-bit logical space onto
 *     a 21-bit physical space in 8KB pages (TAM/TMA set/read them);
 *   - ST0/ST1/ST2, a fast path to the VDC address/data ports;
 *   - block transfers TII/TDD/TIN/TIA/TAI (memory/VRAM copies);
 *   - register helpers (SXY/SAX/SAY/CLA/CLX/CLY), CSH/CSL clock control, and the
 *     65C02 additions (BRA, STZ, PHX/PLX/PHY/PLY, INC/DEC A, JMP (abs,X), ...).
 * One core unlocks the story-strong PC Engine library (Ys, Neutopia, Dungeon
 * Explorer, the visual-novel era). The bus callbacks take a 21-bit PHYSICAL
 * address; the core applies the MPR mapping before every access. Freestanding. */
#ifndef ZXV_CPU_HUC6280_H
#define ZXV_CPU_HUC6280_H

#include <stdint.h>
#include <stdbool.h>

#define HUC_C 0x01u
#define HUC_Z 0x02u
#define HUC_I 0x04u
#define HUC_D 0x08u
#define HUC_B 0x10u
#define HUC_T 0x20u   /* memory-operand flag (used as the 65C02 'unused' bit too) */
#define HUC_V 0x40u
#define HUC_N 0x80u

typedef struct cpu_huc6280 {
    uint8_t  a, x, y, sp, p;
    uint16_t pc;
    uint8_t  mpr[8];             /* memory paging registers                      */
    uint64_t cycles;
    /* bus over the 21-bit physical space (2MB); the core maps logical->physical */
    uint8_t (*read)(struct cpu_huc6280 *c, uint32_t phys);
    void    (*write)(struct cpu_huc6280 *c, uint32_t phys, uint8_t val);
    void    *ctx;
    uint8_t  jammed;
    uint8_t  last_opcode;
    uint32_t illegal;
} cpu_huc6280_t;

/* Map a 16-bit logical address to its 21-bit physical address via the MPRs. */
static inline uint32_t huc_phys(const cpu_huc6280_t *c, uint16_t addr){
    return ((uint32_t)c->mpr[(addr >> 13) & 7] << 13) | (addr & 0x1FFF);
}

/* Reset: MPR7=0, load PC from the reset vector at logical $FFFE/$FFFF, I set. */
void     cpu_huc6280_reset(cpu_huc6280_t *c);

void     cpu_huc6280_irq(cpu_huc6280_t *c);   /* IRQ1 (VDC) -> vector $FFF8 */
void     cpu_huc6280_nmi(cpu_huc6280_t *c);

int      cpu_huc6280_step(cpu_huc6280_t *c);
uint64_t cpu_huc6280_run(cpu_huc6280_t *c, uint64_t max_cycles);

#endif /* ZXV_CPU_HUC6280_H */
