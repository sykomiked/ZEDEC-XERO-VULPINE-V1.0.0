/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* gb.h — a faithful-enough Game Boy / Game Boy Color machine around the LR35902.
 * The handheld library is huge and deeply story-rich (Pokemon, Zelda, Final
 * Fantasy Legend), so this machine feeds a large, narrative slice to Chiglet.
 *
 * Behavioural running verdict, like the NES/SNES machines: a real GB game's boot
 * turns the LCD on ($FF40 bit 7), drives the I/O register file, enables the
 * VBlank interrupt, and settles into a VBlank-driven loop reading LY ($FF44).
 * Random data does none of that. Detection is near-perfect: every real cartridge
 * carries the fixed 48-byte Nintendo logo at $0104 (the boot ROM checks it).
 *
 * Scope: ROM + WRAM/VRAM/OAM/HRAM + the CPU-visible I/O registers, generic MBC
 * ROM-bank switching (MBC1/2/3/5), and a driven LY/VBlank. Real PPU rendering,
 * audio, the serial link, and precise timing are not modelled. */
#ifndef ZXV_GB_H
#define ZXV_GB_H

#include <stdint.h>
#include "cpu_lr35902.h"

#define GB_ROM_CAP  0x200000   /* 2MB — covers the vast majority of MBC games   */

typedef struct gb {
    cpu_lr35902_t cpu;
    uint8_t   rom[GB_ROM_CAP];
    uint32_t  rom_size;
    uint8_t   vram[0x2000];
    uint8_t   wram[0x2000];
    uint8_t   oam[0xA0];
    uint8_t   hram[0x7F];
    uint8_t   io[0x80];          /* $FF00-$FF7F                                  */
    uint8_t   ie;                /* $FFFF                                        */
    uint8_t   cart_ram[0x8000];
    /* MBC state */
    uint32_t  rom_bank;          /* active $4000-$7FFF bank                      */
    uint8_t   ram_enable;
    /* behavioural observation */
    uint8_t   ly;                /* current scanline                            */
    uint8_t   lcd_on;            /* LCDC bit 7 has been set                      */
    uint32_t  io_writes;         /* writes to $FF00-$FF7F                        */
    uint32_t  ly_reads;          /* reads of LY ($FF44)                          */
    uint32_t  vblanks;           /* VBlank interrupts taken                      */
    uint32_t  insn;              /* instructions executed                        */
} gb_t;

/* True iff the image carries the Nintendo logo at $0104 (a real GB cartridge). */
int  gb_is_gb(const uint8_t *img, uint32_t len);

/* Load a ROM image. Returns 1 on success. */
int  gb_load(gb_t *g, const uint8_t *img, uint32_t len);

/* Reset and run up to `budget` instructions, driving LY and delivering a VBlank
 * interrupt each frame once the game enables it. */
void gb_run(gb_t *g, uint32_t budget);

/* Behavioural verdict: did the game come alive on the machine? */
int  gb_is_running(const gb_t *g);

/* On-target self-check: runs a built-in minimal GB program. Returns 1 on pass. */
int  gb_selfcheck(void);

#endif /* ZXV_GB_H */
