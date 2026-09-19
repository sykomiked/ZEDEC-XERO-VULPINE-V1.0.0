/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* game_runner.c — actually RUN an attached ROM on the ZEDEC OS.
 *
 * The Game Master attaches each ROM as a raw virtio-blk device. A raw ROM has
 * no ZXVFS superblock, so when the filesystem mount fails the kernel treats the
 * device as a ROM and runs it here: read the first 64KB of the dataset and
 * EXECUTE it through the unified-emulator CPU cores, reporting how many
 * instructions actually retire. This upgrades "no fault" (kernel survived
 * ingesting the bytes) to "the game's code is executing on the OS":
 *   - a core that MATCHES the ROM's architecture sustains execution (retires
 *     many instructions, does not jam);
 *   - a mismatched core hits an illegal/KIL almost immediately and jams.
 * So the instruction count per core is the running signal. We try both cores we
 * have (MOS 6502 and Zilog Z80), which between them cover the largest slice of
 * the corpus (NES/Atari/C64/Apple/BBC on 6502; ZX Spectrum/CPC/MSX/Coleco on
 * Z80). Honest scope: this proves the ROM's bytes execute as real CPU
 * instructions on ZEDEC — not that the full console (PPU/APU/mappers) is
 * emulated. */
#include "game_runner.h"
#include "zxv_cover.h"
#include "cpu6502.h"
#include "cpu_z80.h"
#include "nes.h"
#include "snes.h"
#include "gb.h"
#include "gba.h"
#include "genesis.h"
#include "pce.h"

static nes_t     g_nes;   /* the NES machine (mapper 0) for iNES images  */
static snes_t    g_snes;  /* the SNES machine (LoROM/HiROM) for .sfc/.smc */
static gb_t      g_gb;    /* the Game Boy machine for .gb/.gbc            */
static gba_t     g_gba;   /* the Game Boy Advance machine for .gba        */
static genesis_t g_gen;   /* the Sega Genesis machine for .md/.bin/.gen   */
static pce_t     g_pce;   /* the PC Engine machine for .pce               */
static uint8_t g_full_img[GBA_ROM_CAP];   /* full-ROM buffer (largest console cap) */

static uint8_t g_raw[65536];      /* raw ROM bytes as read from the device   */
static uint8_t g_mem[65536];      /* per-core address space (rebuilt each run) */

static uint8_t rd6(cpu6502_t *c, uint16_t a){ (void)c; return g_mem[a]; }
static void    wr6(cpu6502_t *c, uint16_t a, uint8_t v){ (void)c; g_mem[a] = v; }
static uint8_t rdz(cpu_z80_t *c, uint16_t a){ (void)c; return g_mem[a]; }
static void    wrz(cpu_z80_t *c, uint16_t a, uint8_t v){ (void)c; g_mem[a] = v; }

#define GR_BUDGET 200000u          /* instruction budget per core */

/* Read up to 64KB of the raw device into g_raw. Returns bytes read. */
static uint32_t load_rom(block_device_t *dev){
    for (int i = 0; i < 65536; i++) g_raw[i] = 0;
    if (!dev || !dev->read_sector) return 0;
    uint32_t secs = dev->total_sectors;
    if (secs > 128) secs = 128;                     /* 128 * 512 = 64KB */
    uint32_t got = 0; uint8_t sec[512];
    for (uint32_t s = 0; s < secs; s++){
        if (dev->read_sector(dev, s, sec) != 0) break;
        for (int i = 0; i < 512 && got < 65536; i++) g_raw[got++] = sec[i];
    }
    return got;
}

/* Read the WHOLE device (capped) into dst. SNES HiROM games jump straight to
 * high banks, so the 64KB probe read is not enough — they need the full ROM. */
static uint32_t load_rom_full(block_device_t *dev, uint8_t *dst, uint32_t cap){
    if (!dev || !dev->read_sector) return 0;
    uint32_t maxsec = cap / 512;
    uint32_t secs = dev->total_sectors; if (secs > maxsec) secs = maxsec;
    uint32_t got = 0; uint8_t sec[512];
    for (uint32_t s = 0; s < secs; s++){
        if (dev->read_sector(dev, s, sec) != 0) break;
        for (int i = 0; i < 512 && got < cap; i++) dst[got++] = sec[i];
    }
    return got;
}

/* Build the 6502 address space from the ROM image. Handles the one format that
 * needs it to reach a real entry point: iNES (.nes) — strip the 16-byte header
 * and map PRG-ROM to $8000..$FFFF (16KB mirrored, 32KB direct) so the reset
 * vector at $FFFC/$FFFD is the game's real entry. Everything else is mapped raw
 * with its tail at $FFFF. (CHR/mapper banking beyond this is future work.) */
static void prep_6502_image(uint32_t len){
    for (int i = 0; i < 65536; i++) g_mem[i] = 0;
    if (len >= 16 && g_raw[0]==0x4E && g_raw[1]==0x45 && g_raw[2]==0x53 && g_raw[3]==0x1A){
        uint32_t prg = (uint32_t)g_raw[4] * 16384u;           /* PRG-ROM size */
        uint32_t off = 16;
        if (off + prg > len) prg = (len > off) ? (len - off) : 0;
        if (prg >= 32768u){
            for (uint32_t i = 0; i < 32768u; i++) g_mem[0x8000u + i] = g_raw[off + i];
        } else {
            for (uint32_t i = 0; i < prg && i < 16384u; i++){  /* mirror 16KB bank */
                g_mem[0x8000u + i] = g_raw[off + i];
                g_mem[0xC000u + i] = g_raw[off + i];
            }
        }
        return;
    }
    uint32_t at = (len >= 65536u) ? 0u : (65536u - len);
    for (uint32_t i = 0; i < len && (at + i) < 65536u; i++) g_mem[at + i] = g_raw[i];
}

/* Run the 6502 core from the prepared image: reset (loads PC from $FFFC/$FFFD),
 * then step the budget, tracking illegal opcodes. */
static void run_6502(uint32_t len, uint32_t *insn, uint32_t *ill, uint16_t *pc, uint8_t *jam){
    prep_6502_image(len);
    cpu6502_t c; c.read = rd6; c.write = wr6; c.ctx = g_mem;
    c.jammed = 0; c.last_opcode = 0; c.illegal = 0;
    cpu6502_reset(&c);
    uint32_t n = 0;
    while (n < GR_BUDGET && !c.jammed){ if (cpu6502_step(&c) == 0) break; n++; }
    *insn = n; *ill = c.illegal; *pc = c.pc; *jam = c.jammed;
}

/* Run the Z80 core: image at 0 (Z80 resets PC=0), reset, step the budget. */
static void run_z80(uint32_t len, uint32_t *insn, uint32_t *ill, uint16_t *pc, uint8_t *jam){
    for (int i = 0; i < 65536; i++) g_mem[i] = 0;
    for (uint32_t i = 0; i < len && i < 65536u; i++) g_mem[i] = g_raw[i];
    cpu_z80_t c; for (unsigned k = 0; k < sizeof c; k++) ((uint8_t*)&c)[k] = 0;
    c.read = rdz; c.write = wrz; c.ctx = g_mem;   /* in/out stay NULL (zeroed) */
    cpu_z80_reset(&c);
    uint32_t n = 0;
    while (n < GR_BUDGET && !c.jammed && !c.halted){ if (cpu_z80_step(&c) == 0) break; n++; }
    *insn = n; *ill = c.illegal; *pc = c.pc; *jam = (uint8_t)(c.jammed || c.halted);
}

/* A core is genuinely running iff it retired enough instructions AND kept its
 * illegal-opcode rate low (real code ~0%, random data ~50%). */
static int core_runs(uint32_t insn, uint16_t permille){
    return (insn >= GR_RUNNING_MIN && permille < GR_ILLEGAL_MAX) ? 1 : 0;
}

int game_runner_run(block_device_t *dev, game_run_t *out){
    for (unsigned i = 0; i < sizeof *out; i++) ((uint8_t*)out)[i] = 0;
    /* Reset FIRST so the report describes THIS ROM, not the boot's history. */
    zxv_cover_reset();
    zxv_cover_mark(COV_GAME_RUNNER_RUN);
    uint32_t bytes = load_rom(dev);
    out->bytes = bytes;
    if (bytes == 0) return 0;
    zxv_cover_mark(COV_ROM_READ);
    /* If this is an iNES image, run it on the real NES machine (mapper 0) — that
     * gives a genuine "is the game running" verdict (PPU configured + vblank/NMI
     * driven), not the bare-CPU probe that can't tell code from data. */
    if (nes_is_ines(g_raw, bytes) && nes_load_ines(&g_nes, g_raw, bytes)){
        zxv_cover_mark(COV_NES_IS_INES); zxv_cover_mark(COV_NES_LOAD);
        zxv_cover_mark(COV_CPU6502_STEP);
        nes_run(&g_nes, 300000u, 2000u);
        zxv_cover_mark(COV_NES_RUN);
        out->is_nes = 1;
        out->nes_running    = (uint8_t)nes_is_running(&g_nes);
        out->nes_ppu_writes = g_nes.ppu_reg_writes;
        out->nes_vblank_polls = g_nes.ppu_status_reads;
        out->nes_nmis       = g_nes.nmis_taken;
        if (out->nes_running) zxv_cover_mark(COV_NES_RUNNING);
        out->running        = out->nes_running;
        out->best_core      = 6502;
        out->best_insn      = g_nes.insn;
        return out->running;
    }
    /* If it carries the Nintendo logo, it is a Game Boy cartridge — run it on the
     * LR35902 machine. The logo is an exact 16-byte match, so this route is only
     * taken by real GB ROMs. */
    if (gb_is_gb(g_raw, bytes)){
        zxv_cover_mark(COV_GB_IS_GB);
        uint32_t full = load_rom_full(dev, g_full_img, GB_ROM_CAP);
        if (full >= 0x150 && gb_load(&g_gb, g_full_img, full)){
            zxv_cover_mark(COV_GB_LOAD); zxv_cover_mark(COV_CPU_LR35902_STEP);
            gb_run(&g_gb, 400000u);
            zxv_cover_mark(COV_GB_RUN);
            out->is_gb       = 1;
            out->gb_running  = (uint8_t)gb_is_running(&g_gb);
            out->gb_io_writes = g_gb.io_writes;
            out->gb_vblanks  = g_gb.vblanks;
            out->gb_lcd_on   = g_gb.lcd_on;
            if (out->gb_running) zxv_cover_mark(COV_GB_RUNNING);
            out->running     = out->gb_running;
            out->best_core   = 8080;
            out->best_insn   = g_gb.insn;
            return out->running;
        }
    }
    /* If it has a GBA cartridge header (logo + $96 + ARM branch entry), run it on
     * the ARM7TDMI machine. Header check is very specific, so only real GBA ROMs
     * route here. */
    if (gba_is_gba(g_raw, bytes)){
        zxv_cover_mark(COV_GBA_IS);
        uint32_t full = load_rom_full(dev, g_full_img, GBA_ROM_CAP);
        if (full >= 0xC0 && gba_load(&g_gba, g_full_img, full)){
            zxv_cover_mark(COV_GBA_LOAD); zxv_cover_mark(COV_CPU_ARM7_STEP);
            gba_run(&g_gba, 500000u);
            zxv_cover_mark(COV_GBA_RUN);
            out->is_gba          = 1;
            out->gba_running     = (uint8_t)gba_is_running(&g_gba);
            out->gba_io_writes   = g_gba.io_writes;
            out->gba_vcount_reads = g_gba.vcount_reads;
            out->gba_vblank_irqs = g_gba.vblank_irqs;
            out->running         = out->gba_running;
            out->best_core       = 7;   /* ARM7 */
            out->best_insn       = g_gba.insn;
            return out->running;
        }
    }
    /* If the ROM header carries "SEGA", it is a Genesis / Mega Drive cartridge —
     * run it on the 68000 machine. */
    if (genesis_is_genesis(g_raw, bytes)){
        uint32_t full = load_rom_full(dev, g_full_img, GEN_ROM_CAP);
        if (full >= 0x200 && genesis_load(&g_gen, g_full_img, full)){
            genesis_run(&g_gen, 400000u);
            out->is_genesis        = 1;
            out->gen_running       = (uint8_t)genesis_is_running(&g_gen);
            out->gen_vdp_reg_writes = g_gen.vdp_reg_writes;
            out->gen_vdp_data_writes = g_gen.vdp_data_writes;
            out->gen_vblank_irqs   = g_gen.vblank_irqs;
            out->running           = out->gen_running;
            out->best_core         = 68;   /* 68000 */
            out->best_insn         = g_gen.insn;
            return out->running;
        }
    }
    /* If it looks like an SNES image (LoROM or HiROM), run it on the 65816
     * machine. The behavioural verdict (native mode + settled + NMI loop / APU
     * handshake) is the real signal; a mis-routed image simply reports running=0.
     * Checksum-strict so headerless HuCards fall through to the PC Engine. */
    if (snes_is_snes_strict(g_raw, bytes)){
        /* Re-read the FULL ROM (up to 4MB) so HiROM games that jump to high banks
         * execute real code, not a wrapped truncation. */
        uint32_t full = load_rom_full(dev, g_full_img, SNES_ROM_CAP);
        if (full >= 0x8000 && snes_load(&g_snes, g_full_img, full)){
            snes_run(&g_snes, 300000u, 1500u);
            out->is_snes             = 1;
            out->snes_running        = (uint8_t)snes_is_running(&g_snes);
            out->snes_ppu_writes     = g_snes.ppu_writes;
            out->snes_cpu_reg_writes = g_snes.cpu_reg_writes;
            out->snes_nmis           = g_snes.nmis_taken;
            out->running             = out->snes_running;
            out->best_core           = 816;
            out->best_insn           = g_snes.insn;
            return out->running;
        }
    }
    /* HuCard heuristic (reset vector points high) — tried LAST, after the specific
     * detectors, since PC Engine ROMs carry no magic string. Run it on the
     * HuC6280 machine; a mis-routed image simply reports running=0. */
    if (pce_is_pce(g_raw, bytes)){
        uint32_t full = load_rom_full(dev, g_full_img, PCE_ROM_CAP);
        if (full >= 0x2000 && pce_load(&g_pce, g_full_img, full)){
            pce_run(&g_pce, 400000u);
            out->is_pce         = 1;
            out->pce_running    = (uint8_t)pce_is_running(&g_pce);
            out->pce_vdc_writes = g_pce.vdc_writes;
            out->pce_vblank_irqs = g_pce.vblank_irqs;
            out->running        = out->pce_running;
            out->best_core      = 6280;
            out->best_insn      = g_pce.insn;
            return out->running;
        }
    }
    run_6502(bytes, &out->insn_6502, &out->ill_6502, &out->pc_6502, &out->jam_6502);
    run_z80 (bytes, &out->insn_z80,  &out->ill_z80,  &out->pc_z80,  &out->jam_z80);
    out->permille_6502 = out->insn_6502 ? (uint16_t)((uint64_t)out->ill_6502 * 1000u / out->insn_6502) : 1000;
    out->permille_z80  = out->insn_z80  ? (uint16_t)((uint64_t)out->ill_z80  * 1000u / out->insn_z80)  : 1000;
    int r6 = core_runs(out->insn_6502, out->permille_6502);
    int rz = core_runs(out->insn_z80,  out->permille_z80);
    if (r6 && (!rz || out->permille_6502 <= out->permille_z80)){
        out->running = 1; out->best_core = 6502; out->best_insn = out->insn_6502;
    } else if (rz){
        out->running = 1; out->best_core = 80;   out->best_insn = out->insn_z80;
    } else {
        out->running = 0; out->best_core = 0;     out->best_insn = 0;
    }
    return out->running;
}
