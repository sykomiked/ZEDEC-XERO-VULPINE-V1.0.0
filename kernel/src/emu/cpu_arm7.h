/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* cpu_arm7.h — an ARM7TDMI (ARMv4T): the Game Boy Advance CPU. Two instruction
 * sets in one core — 32-bit ARM and 16-bit THUMB — switched by the T bit (BX).
 * GBA boot code (crt0) runs in ARM, sets up, then BX into THUMB for the bulk of
 * the game. Unlocks the large, story-strong GBA library (Golden Sun, Fire
 * Emblem, FF Tactics Advance, Pokemon) for Chiglet's training.
 *
 * We are on an ARM64 host, but the GBA is 32-bit ARMv4T with a different
 * encoding, so this is a full interpreter, not native execution. Condition codes
 * gate every ARM instruction; PC reads ahead by 8 (ARM) / 4 (THUMB) — the
 * pipeline. Banked SP/LR/SPSR for SVC/IRQ modes cover what GBA games use;
 * unimplemented encodings are counted in `illegal`, never crash. Freestanding.
 * The bus is byte/halfword/word callbacks over the 32-bit address space. */
#ifndef ZXV_CPU_ARM7_H
#define ZXV_CPU_ARM7_H

#include <stdint.h>

/* CPSR flag bits */
#define ARM_N 0x80000000u
#define ARM_Z 0x40000000u
#define ARM_C 0x20000000u
#define ARM_V 0x10000000u
#define ARM_I 0x00000080u   /* IRQ disable */
#define ARM_F 0x00000040u   /* FIQ disable */
#define ARM_T 0x00000020u   /* THUMB state */

/* Processor modes (CPSR[4:0]) */
#define MODE_USR 0x10
#define MODE_FIQ 0x11
#define MODE_IRQ 0x12
#define MODE_SVC 0x13
#define MODE_ABT 0x17
#define MODE_UND 0x1B
#define MODE_SYS 0x1F

typedef struct cpu_arm7 {
    uint32_t r[16];          /* r[15] = PC (points 8/4 ahead while executing)     */
    uint32_t cpsr;
    uint32_t spsr;           /* SPSR of the current privileged mode               */
    /* banked SP/LR/SPSR for the modes GBA uses */
    uint32_t bank_sp[8], bank_lr[8], bank_spsr[8];   /* indexed by mode_index()   */
    uint64_t cycles;
    /* bus: little-endian byte/halfword/word */
    uint8_t  (*read8) (struct cpu_arm7 *c, uint32_t addr);
    uint16_t (*read16)(struct cpu_arm7 *c, uint32_t addr);
    uint32_t (*read32)(struct cpu_arm7 *c, uint32_t addr);
    void     (*write8) (struct cpu_arm7 *c, uint32_t addr, uint8_t v);
    void     (*write16)(struct cpu_arm7 *c, uint32_t addr, uint16_t v);
    void     (*write32)(struct cpu_arm7 *c, uint32_t addr, uint32_t v);
    void    *ctx;
    /* Optional BIOS SWI handler: the GBA BIOS provides Div/CpuSet/VBlankIntrWait/
     * decompression etc. that games depend on. If set, SWI calls it (with the
     * comment field) instead of being a no-op. */
    void     (*swi)(struct cpu_arm7 *c, uint8_t comment);
    uint8_t  halted;
    uint32_t illegal;        /* unimplemented / undefined encodings               */
} cpu_arm7_t;

/* Reset: SVC mode, I+F set, PC = reset vector (entry passed by the machine). */
void     cpu_arm7_reset(cpu_arm7_t *c, uint32_t entry);

/* Raise IRQ (if not masked): switch to IRQ mode, vector $18. Returns 1 if taken. */
int      cpu_arm7_irq(cpu_arm7_t *c);

/* Execute one instruction (ARM or THUMB per the T bit). Returns cycles (approx). */
int      cpu_arm7_step(cpu_arm7_t *c);

/* Run until at least max_cycles elapse or halted. */
uint64_t cpu_arm7_run(cpu_arm7_t *c, uint64_t max_cycles);

/* True iff currently executing THUMB. */
static inline int cpu_arm7_thumb(const cpu_arm7_t *c){ return (c->cpsr & ARM_T) != 0; }

#endif /* ZXV_CPU_ARM7_H */
