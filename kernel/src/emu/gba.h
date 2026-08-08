/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* gba.h — a Game Boy Advance machine around the ARM7TDMI. The GBA library is
 * large and story-strong (Golden Sun, Fire Emblem, FF Tactics Advance, Pokemon),
 * so this feeds a rich modern-handheld narrative slice to Chiglet.
 *
 * Behavioural running verdict, like the other machines: a real GBA game's boot
 * configures the display ($04000000 DISPCNT), drives the I/O register file, and
 * settles into a frame loop polling VCOUNT ($04000006) / DISPSTAT for VBlank.
 * Random data does not. Detection is near-perfect: the cartridge header carries
 * the fixed Nintendo logo at $04 and the constant $96 at offset $B2, with an ARM
 * branch as the entry word.
 *
 * Scope: the full address map (BIOS stub, EWRAM/IWRAM, I/O, palette, VRAM, OAM,
 * Game Pak ROM, SRAM) + the CPU-visible I/O registers + a driven VCOUNT/VBlank.
 * The BIOS, real PPU rendering, DMA, timers, and SWI services are not modelled;
 * SWIs are no-ops (games that busy-poll still show their init activity). */
#ifndef ZXV_GBA_H
#define ZXV_GBA_H

#include <stdint.h>
#include "cpu_arm7.h"

#define GBA_ROM_CAP  0x800000   /* 8MB — covers a large fraction of the library  */

typedef struct gba {
    cpu_arm7_t cpu;
    uint8_t   rom[GBA_ROM_CAP];
    uint32_t  rom_size;
    uint8_t   ewram[0x40000];    /* 256KB $02000000 */
    uint8_t   iwram[0x8000];     /* 32KB  $03000000 */
    uint8_t   io[0x400];         /* $04000000-$040003FF */
    uint8_t   pal[0x400];        /* $05000000 */
    uint8_t   vram[0x18000];     /* 96KB  $06000000 */
    uint8_t   oam[0x400];        /* $07000000 */
    uint8_t   sram[0x10000];     /* $0E000000 */
    /* behavioural observation */
    uint16_t  vcount;
    uint8_t   dispcnt_set;
    uint32_t  io_writes;
    uint32_t  vcount_reads;
    uint32_t  vblank_irqs;       /* VBlank IRQs dispatched to the game handler    */
    uint32_t  insn;
} gba_t;

/* True iff the image has a GBA cartridge header (logo prefix + $96 + ARM entry). */
int  gba_is_gba(const uint8_t *img, uint32_t len);

/* Load a ROM image. Returns 1 on success. */
int  gba_load(gba_t *g, const uint8_t *img, uint32_t len);

/* Reset (entry $08000000) and run up to `budget` instructions, driving VCOUNT. */
void gba_run(gba_t *g, uint32_t budget);

/* Behavioural verdict: did the game come alive on the machine? */
int  gba_is_running(const gba_t *g);

/* On-target self-check: runs a built-in minimal GBA program. Returns 1 on pass. */
int  gba_selfcheck(void);

#endif /* ZXV_GBA_H */
