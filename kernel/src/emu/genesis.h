/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* genesis.h — a Sega Genesis / Mega Drive machine around the Motorola 68000. The
 * marquee 16-bit Sega library is deeply story-rich (Phantasy Star IV, Shining
 * Force, Story of Thor, Landstalker), a prime narrative slice for Chiglet.
 *
 * Behavioural running verdict, like the other machines: a real game's boot
 * programs the VDP through its control port ($C00004) — a stream of register
 * writes ($8xxx) — uploads pattern/tile data through the data port ($C00000),
 * enables the display, and settles into a frame loop polling the VDP status or
 * driven by the VBlank interrupt (level 6). Because the 68000 exception vectors
 * live in the ROM header, the VBlank IRQ dispatches straight to the game's own
 * handler — no BIOS needed. Random data does none of this.
 *
 * Scope: ROM + 68000 work RAM + the CPU-visible VDP registers/ports + Z80/IO
 * stubs + a driven V-counter/VBlank. The real VDP renderer, the Z80 sound CPU
 * (we HAVE a Z80 core; wiring it is future), and precise timing are not modelled. */
#ifndef ZXV_GENESIS_H
#define ZXV_GENESIS_H

#include <stdint.h>
#include "cpu_m68k.h"

#define GEN_ROM_CAP  0x400000   /* 4MB — the Genesis ROM ceiling                 */

typedef struct genesis {
    cpu_m68k_t cpu;
    uint8_t   rom[GEN_ROM_CAP];
    uint32_t  rom_size;
    uint8_t   ram[0x10000];      /* 64KB work RAM $FF0000-$FFFFFF                 */
    uint8_t   zram[0x2000];      /* 8KB Z80 RAM $A00000                           */
    uint8_t   vdp_reg[32];
    uint8_t   vdp_latch;         /* control-port first/second write toggle       */
    uint16_t  v_counter;
    /* behavioural observation */
    uint8_t   display_on;
    uint8_t   vblank;
    uint32_t  vdp_reg_writes;    /* $8xxx register writes to the control port     */
    uint32_t  vdp_data_writes;   /* writes to the data port ($C00000)             */
    uint32_t  vdp_status_reads;  /* reads of the control port ($C00004)           */
    uint32_t  vblank_irqs;       /* VBlank IRQs dispatched to the game handler    */
    uint32_t  insn;
} genesis_t;

/* True iff the ROM header carries "SEGA" at $0100 (every Mega Drive cartridge). */
int  genesis_is_genesis(const uint8_t *img, uint32_t len);

/* Load a ROM image. Returns 1 on success. */
int  genesis_load(genesis_t *g, const uint8_t *img, uint32_t len);

/* Reset and run up to `budget` instructions, driving the V-counter and VBlank. */
void genesis_run(genesis_t *g, uint32_t budget);

/* Behavioural verdict: did the game come alive on the machine? */
int  genesis_is_running(const genesis_t *g);

/* On-target self-check: runs a built-in minimal Genesis program. Returns 1 on pass. */
int  genesis_selfcheck(void);

#endif /* ZXV_GENESIS_H */
