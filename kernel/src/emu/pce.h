/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* pce.h — a PC Engine / TurboGrafx-16 machine around the HuC6280. The library is
 * distinctive and story-strong (Ys I & II, Neutopia, Dungeon Explorer, the birth
 * of CD-based storytelling), a fresh narrative slice for Chiglet.
 *
 * Behavioural running verdict: a real HuCard's boot maps its banks (TAM), then
 * programs the VDC through the ST0/ST1/ST2 fast ports and the $FF-page MMIO — a
 * stream of register writes — and settles into a frame loop driven by the VDC's
 * VBlank interrupt (IRQ1) or by polling the VDC status. Because the HuC6280
 * exception vectors live in the HuCard ($FFF8 = IRQ1), the VBlank IRQ dispatches
 * straight to the game's handler — no BIOS. Random data does not do this.
 *
 * Scope: HuCard ROM (bank-paged) + work RAM + save RAM + the CPU-visible VDC/VCE
 * registers + a driven V-counter/VBlank + the interrupt controller. The real VDC
 * renderer, the PSG, the CD-ROM system, and SuperGrafx are not modelled. */
#ifndef ZXV_PCE_H
#define ZXV_PCE_H

#include <stdint.h>
#include "cpu_huc6280.h"

#define PCE_ROM_CAP  0x100000   /* 1MB — the HuCard ceiling                      */

typedef struct pce {
    cpu_huc6280_t cpu;
    uint8_t   rom[PCE_ROM_CAP];
    uint32_t  rom_size;
    uint8_t   ram[0x2000];       /* 8KB work RAM  (page $F8)                     */
    uint8_t   bram[0x2000];      /* save RAM      (page $F7)                     */
    uint8_t   vdc_addr;          /* selected VDC register                        */
    uint16_t  vdc_reg[32];
    uint8_t   irq_disable;       /* $1FF402: 1 = that IRQ source disabled        */
    uint16_t  v_counter;
    /* behavioural observation */
    uint8_t   display_on;
    uint8_t   vblank;
    uint32_t  vdc_writes;        /* VDC port writes (ST0-2 + MMIO)               */
    uint32_t  vdc_status_reads;  /* reads of the VDC status port                 */
    uint32_t  vce_writes;        /* palette writes                               */
    uint32_t  vblank_irqs;       /* VBlank IRQ1s dispatched to the game handler   */
    uint32_t  insn;
} pce_t;

/* Heuristic: does this look like a HuCard ROM? (reset vector points high) */
int  pce_is_pce(const uint8_t *img, uint32_t len);

/* Load a HuCard image (strips a 512-byte copier header if present). Returns 1. */
int  pce_load(pce_t *p, const uint8_t *img, uint32_t len);

/* Reset and run up to `budget` instructions, driving the V-counter/VBlank. */
void pce_run(pce_t *p, uint32_t budget);

/* Behavioural verdict: did the game come alive on the machine? */
int  pce_is_running(const pce_t *p);

/* On-target self-check: runs a built-in minimal HuCard program. Returns 1 on pass. */
int  pce_selfcheck(void);

#endif /* ZXV_PCE_H */
