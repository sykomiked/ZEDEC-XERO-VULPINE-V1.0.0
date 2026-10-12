/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_emu_game_runner.c: unit tests for kernel/src/emu/game_runner.c. Drives
 * game_runner_run through a synthetic block device and checks every game_run_t
 * field: the NULL/empty device, the 128-sector probe cap, read failures, each
 * console route (NES, GB, GBA, Genesis, SNES, PCE; console results are compared
 * with the same machine module run directly as an oracle, at the full-read
 * size boundaries), the fall-through when a full re-read fails, the bare-CPU
 * 6502/Z80 verdict (running thresholds, illegal-rate boundary, core choice and
 * its tie rule) and the coverage points each route marks. */
#include "game_runner.h"
#include "gb.h"
#include "gba.h"
#include "genesis.h"
#include "nes.h"
#include "pce.h"
#include "snes.h"
#include "zxv_cover.h"
#include <stdio.h>
#include <string.h>

static int fails = 0;
static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            printf("  FAIL line %d: %s\n", __LINE__, #cond);                                       \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

/* ------------------------------------------------------------------ device */

static uint8_t g_img[GBA_ROM_CAP]; /* device contents; zero beyond what a test writes */

typedef struct {
    uint32_t fail_call; /* the call number (1-based) that fails; 0 = never */
    uint32_t calls;     /* read_sector calls seen                          */
    uint32_t max_lba;   /* highest LBA requested                           */
} devctx_t;

static int dev_read(block_device_t *d, uint32_t lba, uint8_t *buf)
{
    devctx_t *x = (devctx_t *) d->driver_data;
    x->calls++;
    if (lba > x->max_lba) x->max_lba = lba;
    if (x->fail_call && x->calls >= x->fail_call) return -1;
    memcpy(buf, g_img + (size_t) lba * 512u, 512);
    return 0;
}

static block_device_t g_dev;
static devctx_t g_ctx;

static block_device_t *mkdev(uint32_t sectors, uint32_t fail_call)
{
    memset(&g_dev, 0, sizeof g_dev);
    memset(&g_ctx, 0, sizeof g_ctx);
    g_ctx.fail_call = fail_call;
    g_dev.present = true;
    g_dev.total_sectors = sectors;
    g_dev.driver_data = &g_ctx;
    g_dev.read_sector = dev_read;
    return &g_dev;
}

static void clear_img(void) { memset(g_img, 0, sizeof g_img); }

/* -------------------------------------------------------------- coverage */

static uint64_t g_seen;
static void cov_puts(const char *s)
{
    for (unsigned i = 0; i < (unsigned) COV_POINT_COUNT; i++)
        if (strcmp(s, zxv_cover_name((zxv_cover_id_t) i)) == 0) g_seen |= 1ull << i;
}
static uint64_t cover_now(void)
{
    g_seen = 0;
    zxv_cover_report(cov_puts);
    return g_seen;
}
#define BIT(id) (1ull << (id))

/* -------------------------------------------------------------- compare */

static int same_run(const game_run_t *a, const game_run_t *e, int line)
{
    int bad = 0;
#define F(f)                                                                                       \
    if (a->f != e->f) {                                                                            \
        printf("  line %d: %s = %lu, want %lu\n", line, #f, (unsigned long) a->f,                  \
               (unsigned long) e->f);                                                              \
        bad = 1;                                                                                   \
    }
    F(bytes) F(insn_6502) F(ill_6502) F(pc_6502) F(jam_6502) F(insn_z80) F(ill_z80) F(pc_z80)
    F(jam_z80) F(permille_6502) F(permille_z80) F(best_insn) F(best_core) F(running) F(is_nes)
    F(nes_running) F(nes_ppu_writes) F(nes_vblank_polls) F(nes_nmis) F(is_snes) F(snes_running)
    F(snes_ppu_writes) F(snes_cpu_reg_writes) F(snes_nmis) F(is_gb) F(gb_running)
    F(gb_io_writes) F(gb_vblanks) F(gb_lcd_on) F(is_gba) F(gba_running) F(gba_io_writes)
    F(gba_vcount_reads) F(gba_vblank_irqs) F(is_genesis) F(gen_running) F(gen_vdp_reg_writes)
    F(gen_vdp_data_writes) F(gen_vblank_irqs) F(is_pce) F(pce_running) F(pce_vdc_writes)
    F(pce_vblank_irqs)
#undef F
    if (!bad && memcmp(a, e, sizeof *a) != 0) {
        printf("  line %d: padding bytes differ\n", line);
        bad = 1;
    }
    return !bad;
}
#define SAME(a, e) CHECK(same_run((a), (e), __LINE__))

static game_run_t g_out, g_exp;

/* Run the device and return game_runner_run's result; g_out is pre-filled with
 * garbage so every field must be written. */
static int run(block_device_t *d)
{
    memset(&g_out, 0x5A, sizeof g_out);
    return game_runner_run(d, &g_out);
}

static void exp_reset(uint32_t bytes)
{
    memset(&g_exp, 0, sizeof g_exp);
    g_exp.bytes = bytes;
}

/* -------------------------------------------------------- empty devices */

static void test_empty(void)
{
    zxv_cover_reset();
    zxv_cover_mark(COV_PCE_RUN); /* stale history must be cleared */
    exp_reset(0);
    CHECK(run(NULL) == 0);
    SAME(&g_out, &g_exp);
    CHECK(cover_now() == BIT(COV_GAME_RUNNER_RUN));

    block_device_t *d = mkdev(4, 0);
    d->read_sector = NULL;
    CHECK(run(d) == 0);
    SAME(&g_out, &g_exp);

    d = mkdev(0, 0);
    CHECK(run(d) == 0);
    SAME(&g_out, &g_exp);
    CHECK(g_ctx.calls == 0);
    CHECK(cover_now() == BIT(COV_GAME_RUNNER_RUN));

    d = mkdev(4, 1); /* the very first read fails */
    CHECK(run(d) == 0);
    SAME(&g_out, &g_exp);
    CHECK(g_ctx.calls == 1);
}

/* ------------------------------------------------- bare-CPU raw images */

static void put(uint32_t off, const uint8_t *b, uint32_t n) { memcpy(g_img + off, b, n); }
static void vec6502(uint32_t len, uint16_t pc)
{
    g_img[len - 4] = (uint8_t) pc;
    g_img[len - 3] = (uint8_t) (pc >> 8);
}

/* Fill the expected bare-CPU fields. */
static void exp_raw(uint32_t i6, uint32_t l6, uint16_t pc6, uint32_t iz, uint32_t lz, uint16_t pcz,
                    uint8_t jz, uint16_t pm6, uint16_t pmz, uint16_t core)
{
    g_exp.insn_6502 = i6;
    g_exp.ill_6502 = l6;
    g_exp.pc_6502 = pc6;
    g_exp.jam_6502 = 0;
    g_exp.insn_z80 = iz;
    g_exp.ill_z80 = lz;
    g_exp.pc_z80 = pcz;
    g_exp.jam_z80 = jz;
    g_exp.permille_6502 = pm6;
    g_exp.permille_z80 = pmz;
    g_exp.best_core = core;
    g_exp.running = core ? 1 : 0;
    g_exp.best_insn = core == 6502 ? i6 : core == 80 ? iz : 0;
}

#define BUDGET 200000u

static void test_raw_tie(void)
{
    /* 512 bytes at $FE00. 6502: JMP $FE10 forever (0 illegal). Z80 from 0:
     * NOPs and DJNZ loops, never halts, 0 illegal. Equal permille -> 6502. */
    clear_img();
    static const uint8_t jmp[] = {0x4C, 0x10, 0xFE};
    put(0x10, jmp, sizeof jmp);
    vec6502(512, 0xFE10);
    g_img[511] = 0xED; /* $FFFF in the 6502 map; the Z80 map must not keep it */
    exp_reset(512);
    CHECK(run(mkdev(1, 0)) == 1);
    exp_raw(BUDGET, 0, 0xFE10, BUDGET, 4, 0x0554, 0, 0, 0, 6502);
    SAME(&g_out, &g_exp);
    CHECK(cover_now() == (BIT(COV_GAME_RUNNER_RUN) | BIT(COV_ROM_READ)));
}

static void test_raw_z80_threshold(void)
{
    /* 8192 bytes at $E000. Z80: NOPs then HALT, retiring exactly n
     * instructions. 6502: 3 illegal + JMP loop at $FF40 (750 permille). */
    static const uint8_t loop6[] = {0x1A, 0x1A, 0x1A, 0x4C, 0x40, 0xFF};
    for (uint32_t n = 7999; n <= 8000; n++) {
        clear_img();
        g_img[n - 1] = 0x76;
        put(8000 + 2, loop6, sizeof loop6); /* past the HALT */
        vec6502(8192, 0xE000 + 8002);
        g_img[8000 + 2 + 4] = (uint8_t) ((0xE000 + 8002) & 0xFF);
        g_img[8000 + 2 + 5] = (uint8_t) ((0xE000 + 8002) >> 8);
        exp_reset(8192);
        int r = run(mkdev(16, 0));
        uint16_t core = n >= 8000 ? 80 : 0;
        CHECK(r == (core != 0));
        exp_raw(BUDGET, 150000, 0xE000 + 8002, n, 0, (uint16_t) n, 1, 750, 0, core);
        SAME(&g_out, &g_exp);
        CHECK(g_ctx.calls == 16);
    }
}

/* 6502 loop at $FE40 with `ill` illegal NOPs, then `nops` legal NOPs, then
 * JMP $FE40; Z80 HALTs at once (1 instruction, not running). */
static void raw_6502_loop(uint32_t ill, uint32_t nops)
{
    clear_img();
    g_img[0] = 0x76;
    uint32_t p = 0x40;
    for (uint32_t i = 0; i < ill; i++) g_img[p++] = 0x1A;
    for (uint32_t i = 0; i < nops; i++) g_img[p++] = 0xEA;
    g_img[p++] = 0x4C;
    g_img[p++] = 0x40;
    g_img[p++] = 0xFE;
    vec6502(512, 0xFE40);
}

static void test_raw_6502_rate(void)
{
    /* 1 illegal in 10 instructions: exactly 100 permille, not running. */
    raw_6502_loop(1, 8);
    exp_reset(512);
    CHECK(run(mkdev(1, 0)) == 0);
    exp_raw(BUDGET, 20000, 0xFE40, 1, 0, 1, 1, 100, 0, 0);
    SAME(&g_out, &g_exp);

    /* 10 illegal in 101 instructions: 19810 illegal, 99 permille, running. */
    raw_6502_loop(10, 90);
    exp_reset(512);
    CHECK(run(mkdev(1, 0)) == 1);
    exp_raw(BUDGET, 19810, g_out.pc_6502, 1, 0, 1, 1, 99, 0, 6502);
    SAME(&g_out, &g_exp);
    CHECK(g_out.pc_6502 == 0xFE40 + 20);
}

static void test_raw_choice(void)
{
    /* Both run; Z80 has 1 illegal (ED 00) per 20 instructions = 50 permille,
     * 6502 is clean -> 6502. */
    clear_img();
    g_img[0] = 0xED;
    g_img[1] = 0x00;
    g_img[20] = 0x18; /* JR -22 -> 0 */
    g_img[21] = 0xEA;
    static const uint8_t jmp[] = {0x4C, 0x40, 0xFE};
    put(0x40, jmp, sizeof jmp);
    vec6502(512, 0xFE40);
    exp_reset(512);
    CHECK(run(mkdev(1, 0)) == 1);
    exp_raw(BUDGET, 0, 0xFE40, BUDGET, 10000, 0, 0, 0, 50, 6502);
    SAME(&g_out, &g_exp);

    /* Both run; 6502 has 1 illegal per 20 (50 permille), Z80 clean -> Z80. */
    clear_img();
    g_img[0] = 0x18; /* JR -2 */
    g_img[1] = 0xFE;
    uint32_t p = 0x40;
    g_img[p++] = 0x1A;
    for (int i = 0; i < 18; i++) g_img[p++] = 0xEA;
    g_img[p++] = 0x4C;
    g_img[p++] = 0x40;
    g_img[p++] = 0xFE;
    vec6502(512, 0xFE40);
    exp_reset(512);
    CHECK(run(mkdev(1, 0)) == 1);
    exp_raw(BUDGET, 10000, 0xFE40, BUDGET, 0, 0, 0, 50, 0, 80);
    SAME(&g_out, &g_exp);
}

static void test_probe_cap(void)
{
    static const uint8_t jmp[] = {0x4C, 0x00, 0x90};
    /* 127 sectors -> 65024 bytes, image at $0200 (vector still at the end). */
    clear_img();
    put(0x9000 - 0x200, jmp, sizeof jmp);
    g_img[0] = 0x76;
    vec6502(65024, 0x9000);
    exp_reset(65024);
    CHECK(run(mkdev(127, 0)) == 1);
    exp_raw(BUDGET, 0, 0x9000, 1, 0, 1, 1, 0, 0, 6502);
    SAME(&g_out, &g_exp);
    CHECK(g_ctx.calls == 127);

    /* A read failure stops the probe: sectors 0..1 good, call 3 fails. */
    clear_img();
    g_img[0] = 0x76;
    put(0x10, jmp, sizeof jmp);
    g_img[0x11] = 0x10;
    g_img[0x12] = 0xFC;
    vec6502(1024, 0xFC10);
    exp_reset(1024);
    CHECK(run(mkdev(4, 3)) == 1);
    exp_raw(BUDGET, 0, 0xFC10, 1, 0, 1, 1, 0, 0, 6502);
    SAME(&g_out, &g_exp);
    CHECK(g_ctx.calls == 3);

    /* 129 sectors: only the first 128 (64KB) are read; mapped at $0000, so the
     * reset vector is the last 4 bytes of the 64KB window. Byte $FFFF is left
     * nonzero in memory for test_ines_rejected, which runs next. */
    clear_img();
    put(0x9000, jmp, sizeof jmp);
    g_img[0] = 0x76;
    vec6502(65536, 0x9000);
    g_img[65535] = 0x12;
    g_img[65536 + 0x1000] = 0x02; /* beyond the cap: must not matter */
    exp_reset(65536);
    CHECK(run(mkdev(129, 0)) == 1);
    exp_raw(BUDGET, 0, 0x9000, 1, 0, 1, 1, 0, 0, 6502);
    SAME(&g_out, &g_exp);
    CHECK(g_ctx.calls == 128);
    CHECK(g_ctx.max_lba == 127);

}

static void test_ines_rejected(void)
{
    /* iNES header with PRG count 0: nes_load_ines rejects it and the bare-CPU
     * probe maps nothing (all zero memory: the 6502 BRKs from $0000). A raw
     * mapping would instead start at the tail vector $FE40. */
    clear_img();
    static const uint8_t hdr[] = {'N', 'E', 'S', 0x1A, 0, 0, 0, 0};
    put(0, hdr, sizeof hdr);
    static const uint8_t jmp[] = {0x4C, 0x40, 0xFE};
    put(0x40, jmp, sizeof jmp);
    vec6502(512, 0xFE40);
    exp_reset(512);
    run(mkdev(1, 0));
    CHECK(g_out.is_nes == 0);
    CHECK(g_out.insn_6502 == BUDGET);
    CHECK(g_out.ill_6502 == 0);
    CHECK(g_out.pc_6502 != 0xFE40);
    CHECK(g_out.pc_6502 < 0x0100);
    uint16_t pc_zero = g_out.pc_6502;

    /* Trainer flag with a 512-byte image: PRG would start past the end, so
     * nes_load_ines rejects; the probe mirrors bytes 16.. at $8000/$C000 and
     * the vectors stay zero. */
    g_img[4] = 1;
    g_img[6] = 0x04;
    run(mkdev(1, 0));
    CHECK(g_out.is_nes == 0);
    CHECK(g_out.pc_6502 == pc_zero);
    CHECK(g_out.insn_6502 == BUDGET);
    CHECK(cover_now() == (BIT(COV_GAME_RUNNER_RUN) | BIT(COV_ROM_READ)));

    /* Same image without the iNES magic: raw mapping, runs from $FE40. */
    g_img[3] = 0x1B;
    run(mkdev(1, 0));
    CHECK(g_out.pc_6502 == 0xFE40);
}

static void test_raw_first_byte_and_ports(void)
{
    /* 6502 code starts at the image's first byte ($FE00: JMP $FE00); the Z80
     * runs the same bytes from 0 and executes OUT (n),A with no port hook. */
    clear_img();
    static const uint8_t code[] = {0x4C, 0x00, 0xFE, 0x00, 0xD3, 0x10};
    put(0, code, sizeof code);
    vec6502(512, 0xFE00);
    exp_reset(512);
    CHECK(run(mkdev(1, 0)) == 1);
    exp_raw(BUDGET, 0, 0xFE00, BUDGET, 0, 0x0D4C, 0, 0, 0, 6502);
    SAME(&g_out, &g_exp);
}

static void test_raw_permille_rounding(void)
{
    /* Z80: ED 00 (illegal), 999 NOPs, HALT -> 1 illegal in 1001 = 0 permille
     * (truncated). 6502 at $FD00: 1 illegal + 89 NOP + JMP (91 per loop) ->
     * 2198 illegal in 200000 = 10 permille (10.99 truncated), running. */
    clear_img();
    g_img[0] = 0xED;
    g_img[1] = 0x00;
    g_img[1001] = 0x76;
    uint32_t p = 0x500;
    g_img[p++] = 0x1A;
    for (int i = 0; i < 89; i++) g_img[p++] = 0xEA;
    g_img[p++] = 0x4C;
    g_img[p++] = 0x00;
    g_img[p++] = 0xFD;
    vec6502(2048, 0xFD00);
    exp_reset(2048);
    CHECK(run(mkdev(4, 0)) == 1);
    exp_raw(BUDGET, 2198, 0xFD00 + 73, 1001, 1, 1002, 1, 10, 0, 6502);
    SAME(&g_out, &g_exp);
}

/* ------------------------------------------------------- console routes */

static nes_t o_nes;
static gb_t o_gb;
static gba_t o_gba;
static genesis_t o_gen;
static snes_t o_snes;
static pce_t o_pce;

static uint32_t build_nes(int live)
{
    clear_img();
    g_img[0] = 'N';
    g_img[1] = 'E';
    g_img[2] = 'S';
    g_img[3] = 0x1A;
    g_img[4] = 1;
    if (live) { /* nes_selfcheck's program: enable NMI, program PPU, poll vblank */
        static const uint8_t code[] = {0x78, 0xA2, 0xFF, 0x9A, 0xA9, 0x80, 0x8D, 0x00,
                                       0x20, 0xA9, 0x1E, 0x8D, 0x01, 0x20, 0x2C, 0x02,
                                       0x20, 0x10, 0xFB, 0x4C, 0x0E, 0xC0};
        put(16, code, sizeof code);
        g_img[16 + 0x16] = 0x40;
        uint32_t v = 16 + 0x3FFA;
        g_img[v + 0] = 0x16;
        g_img[v + 1] = 0xC0;
        g_img[v + 2] = 0x00;
        g_img[v + 3] = 0xC0;
        g_img[v + 4] = 0x16;
        g_img[v + 5] = 0xC0;
    }
    return 33; /* 16 + 16384 bytes -> 33 sectors */
}

static void test_nes(void)
{
    for (int live = 1; live >= 0; live--) {
        uint32_t secs = build_nes(live);
        uint32_t len = secs * 512;
        CHECK(nes_load_ines(&o_nes, g_img, len) == 1);
        nes_run(&o_nes, 300000u, 2000u);
        int want = nes_is_running(&o_nes);
        CHECK(want == live);
        exp_reset(len);
        g_exp.is_nes = 1;
        g_exp.nes_running = (uint8_t) want;
        g_exp.nes_ppu_writes = o_nes.ppu_reg_writes;
        g_exp.nes_vblank_polls = o_nes.ppu_status_reads;
        g_exp.nes_nmis = o_nes.nmis_taken;
        g_exp.running = (uint8_t) want;
        g_exp.best_core = 6502;
        g_exp.best_insn = o_nes.insn;
        CHECK(run(mkdev(secs, 0)) == want);
        SAME(&g_out, &g_exp);
        CHECK(g_ctx.calls == secs);
        uint64_t cov = BIT(COV_GAME_RUNNER_RUN) | BIT(COV_ROM_READ) | BIT(COV_NES_IS_INES) |
                       BIT(COV_NES_LOAD) | BIT(COV_CPU6502_STEP) | BIT(COV_NES_RUN);
        if (want) cov |= BIT(COV_NES_RUNNING);
        CHECK(cover_now() == cov);
    }
    CHECK(o_nes.insn == 300000u); /* the dead image ran the full budget */
}

static const uint8_t GB_LOGO[16] = {0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
                                    0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D};

static void build_gb(void)
{
    clear_img();
    memcpy(g_img + 0x104, GB_LOGO, 16);
    g_img[0x100] = 0x00;
    g_img[0x101] = 0xC3;
    g_img[0x102] = 0x50;
    g_img[0x103] = 0x01;
    static const uint8_t isr[] = {0xF0, 0x80, 0x3C, 0xE0, 0x80, 0xD9};
    put(0x40, isr, sizeof isr);
    static const uint8_t main_[] = {0x31, 0xFE, 0xFF, 0x3E, 0x91, 0xE0, 0x40, 0x3E, 0xE4,
                                    0xE0, 0x47, 0x3E, 0x00, 0xE0, 0x42, 0xE0, 0x43, 0x3E,
                                    0x01, 0xE0, 0x0F, 0xEA, 0xFF, 0xFF, 0xFB, 0xF0, 0x44,
                                    0x18, 0xFC};
    put(0x150, main_, sizeof main_);
}

static void exp_gb(uint32_t probe_len, uint32_t full)
{
    CHECK(gb_load(&o_gb, g_img, full) == 1);
    gb_run(&o_gb, 400000u);
    exp_reset(probe_len);
    g_exp.is_gb = 1;
    g_exp.gb_running = (uint8_t) gb_is_running(&o_gb);
    g_exp.gb_io_writes = o_gb.io_writes;
    g_exp.gb_vblanks = o_gb.vblanks;
    g_exp.gb_lcd_on = o_gb.lcd_on;
    g_exp.running = g_exp.gb_running;
    g_exp.best_core = 8080;
    g_exp.best_insn = o_gb.insn;
}

static const uint64_t COV_GB_ALL = BIT(COV_GAME_RUNNER_RUN) | BIT(COV_ROM_READ) |
                                   BIT(COV_GB_IS_GB) | BIT(COV_GB_LOAD) |
                                   BIT(COV_CPU_LR35902_STEP) | BIT(COV_GB_RUN);

static void test_gb(void)
{
    build_gb();
    exp_gb(0x8000, 0x8000);
    CHECK(g_exp.gb_running == 1);
    CHECK(run(mkdev(64, 0)) == 1);
    SAME(&g_out, &g_exp);
    CHECK(g_ctx.calls == 64 + 64);
    CHECK(cover_now() == (COV_GB_ALL | BIT(COV_GB_RUNNING)));

    /* Large device: the probe reads 128 sectors, the full read stops at the
     * 2MB cap (4096 sectors). */
    g_exp.bytes = 65536; /* same program, so the same machine results */
    CHECK(run(mkdev(5000, 0)) == 1);
    SAME(&g_out, &g_exp);
    CHECK(g_ctx.calls == 128 + 4096);
    CHECK(g_ctx.max_lba == 4095);

    /* Not running: logo present, code is all zero (NOP slide), LCD never on. */
    clear_img();
    memcpy(g_img + 0x104, GB_LOGO, 16);
    exp_gb(512, 512);
    CHECK(g_exp.gb_running == 0);
    CHECK(run(mkdev(1, 0)) == 0);
    SAME(&g_out, &g_exp);
    CHECK(cover_now() == COV_GB_ALL);

    /* Full re-read fails (call 2): the GB route is skipped and the image falls
     * through to the bare-CPU probe. */
    run(mkdev(1, 2));
    CHECK(g_out.is_gb == 0);
    CHECK(g_out.bytes == 512);
    CHECK(g_out.insn_6502 == BUDGET);
    CHECK(g_out.insn_z80 == BUDGET);
    CHECK(g_ctx.calls == 2);
    CHECK(cover_now() == (BIT(COV_GAME_RUNNER_RUN) | BIT(COV_ROM_READ) | BIT(COV_GB_IS_GB)));
}

static void build_gba(void)
{
    clear_img();
    uint32_t entry = 0xEA000000u | ((0xC0 - 8) >> 2);
    g_img[0] = (uint8_t) entry;
    g_img[1] = (uint8_t) (entry >> 8);
    g_img[2] = (uint8_t) (entry >> 16);
    g_img[3] = (uint8_t) (entry >> 24);
    static const uint8_t logo[4] = {0x24, 0xFF, 0xAE, 0x51};
    put(4, logo, 4);
    g_img[0xB2] = 0x96;
    static const uint32_t code[] = {0xE3A00404, 0xE3A01C01, 0xE5801000,
                                    0xE5801000, 0xE1D020B6, 0xEAFFFFFD};
    for (unsigned i = 0; i < 6; i++)
        for (unsigned b = 0; b < 4; b++) g_img[0xC0 + 4 * i + b] = (uint8_t) (code[i] >> (8 * b));
}

static void test_gba(void)
{
    build_gba();
    CHECK(gba_load(&o_gba, g_img, 1024) == 1);
    gba_run(&o_gba, 500000u);
    exp_reset(1024);
    g_exp.is_gba = 1;
    g_exp.gba_running = (uint8_t) gba_is_running(&o_gba);
    g_exp.gba_io_writes = o_gba.io_writes;
    g_exp.gba_vcount_reads = o_gba.vcount_reads;
    g_exp.gba_vblank_irqs = o_gba.vblank_irqs;
    g_exp.running = g_exp.gba_running;
    g_exp.best_core = 7;
    g_exp.best_insn = o_gba.insn;
    CHECK(g_exp.gba_running == 1);
    CHECK(run(mkdev(2, 0)) == 1);
    SAME(&g_out, &g_exp);
    CHECK(g_ctx.calls == 4);
    CHECK(cover_now() == (BIT(COV_GAME_RUNNER_RUN) | BIT(COV_ROM_READ) | BIT(COV_GBA_IS) |
                          BIT(COV_GBA_LOAD) | BIT(COV_CPU_ARM7_STEP) | BIT(COV_GBA_RUN)));

    /* Not running: header only, the branch lands in zeros. */
    memset(g_img + 0xC0, 0, 24);
    exp_reset(512);
    g_exp.is_gba = 1;
    g_exp.best_core = 7;
    g_exp.best_insn = 500000u; /* one instruction per budget step */
    CHECK(run(mkdev(1, 0)) == 0);
    SAME(&g_out, &g_exp);

    /* Full re-read fails: fall through. */
    run(mkdev(1, 2));
    CHECK(g_out.is_gba == 0);
    CHECK(g_out.insn_6502 == BUDGET);
    CHECK(cover_now() == (BIT(COV_GAME_RUNNER_RUN) | BIT(COV_ROM_READ) | BIT(COV_GBA_IS)));
}

#define W16(a, v)                                                                                  \
    do {                                                                                           \
        g_img[(a)] = (uint8_t) ((v) >> 8);                                                         \
        g_img[(a) + 1] = (uint8_t) (v);                                                            \
    } while (0)
#define W32(a, v)                                                                                  \
    do {                                                                                           \
        W16((a), (uint32_t) (v) >> 16);                                                            \
        W16((a) + 2, (v) & 0xFFFF);                                                                \
    } while (0)

static void build_genesis(void)
{
    clear_img();
    W32(0x00, 0x00FFFFF0);
    W32(0x04, 0x00000200);
    W32(0x78, 0x00000300);
    g_img[0x100] = 'S';
    g_img[0x101] = 'E';
    g_img[0x102] = 'G';
    g_img[0x103] = 'A';
    uint32_t p = 0x200;
    W16(p, 0x41F9); p += 2; W32(p, 0x00C00004); p += 4;
    W16(p, 0x43F9); p += 2; W32(p, 0x00C00000); p += 4;
    static const uint16_t ops[] = {0x30BC, 0x8004, 0x30BC, 0x8164, 0x30BC, 0x8230, 0x30BC,
                                   0x8328, 0x30BC, 0x8407, 0x30BC, 0x857C, 0x30BC, 0x8700,
                                   0x30BC, 0x4000, 0x30BC, 0x0000, 0x32BC, 0x0FF0, 0x32BC,
                                   0x0FF0, 0x32BC, 0x0FF0, 0x32BC, 0x0FF0, 0x3010, 0x60FC};
    for (unsigned i = 0; i < sizeof ops / sizeof ops[0]; i++) { W16(p, ops[i]); p += 2; }
    p = 0x300;
    W16(p, 0x33FC); p += 2; W16(p, 0x0001); p += 2; W32(p, 0x00FF0000); p += 4;
    W16(p, 0x4E73);
}

static void exp_gen(uint32_t len)
{
    CHECK(genesis_load(&o_gen, g_img, len) == 1);
    genesis_run(&o_gen, 400000u);
    exp_reset(len);
    g_exp.is_genesis = 1;
    g_exp.gen_running = (uint8_t) genesis_is_running(&o_gen);
    g_exp.gen_vdp_reg_writes = o_gen.vdp_reg_writes;
    g_exp.gen_vdp_data_writes = o_gen.vdp_data_writes;
    g_exp.gen_vblank_irqs = o_gen.vblank_irqs;
    g_exp.running = g_exp.gen_running;
    g_exp.best_core = 68;
    g_exp.best_insn = o_gen.insn;
}

static void test_genesis(void)
{
    build_genesis();
    exp_gen(0x8000);
    CHECK(g_exp.gen_running == 1);
    CHECK(run(mkdev(64, 0)) == 1);
    SAME(&g_out, &g_exp);
    CHECK(g_ctx.calls == 128);
    CHECK(cover_now() == (BIT(COV_GAME_RUNNER_RUN) | BIT(COV_ROM_READ)));

    /* 512 bytes: exactly the 0x200 full-read minimum. Entry at $180 (BRA self). */
    clear_img();
    W32(0x00, 0x00FFFFF0);
    W32(0x04, 0x00000180);
    g_img[0x100] = 'S';
    g_img[0x101] = 'E';
    g_img[0x102] = 'G';
    g_img[0x103] = 'A';
    W16(0x180, 0x60FE);
    exp_reset(512);
    g_exp.is_genesis = 1;
    g_exp.best_core = 68;
    g_exp.best_insn = 400000u;
    CHECK(run(mkdev(1, 0)) == 0);
    SAME(&g_out, &g_exp);

    /* Full re-read fails: fall through. */
    run(mkdev(1, 2));
    CHECK(g_out.is_genesis == 0);
    CHECK(g_out.insn_6502 == BUDGET);
}

static void build_snes(void)
{
    clear_img();
    static const uint8_t main_[] = {0x78, 0x18, 0xFB, 0xC2, 0x30, 0xA2, 0xFF, 0x1F, 0x9A,
                                    0xE2, 0x20, 0xA9, 0x8F, 0x8D, 0x00, 0x21, 0xA9, 0x0F,
                                    0x8D, 0x2C, 0x21, 0xA9, 0x81, 0x8D, 0x00, 0x42, 0xCB,
                                    0x80, 0xFD};
    put(0, main_, sizeof main_);
    static const uint8_t nmi[] = {0xE2, 0x20, 0xAF, 0x00, 0x00, 0x7E, 0x1A,
                                  0x8F, 0x00, 0x00, 0x7E, 0x40};
    put(0x100, nmi, sizeof nmi);
    g_img[0x7FEA] = 0x00;
    g_img[0x7FEB] = 0x81;
    g_img[0x7FFC] = 0x00;
    g_img[0x7FFD] = 0x80;
    /* LoROM header checksum pair: complement ^ checksum == 0xFFFF */
    g_img[0x7FDC] = 0x34;
    g_img[0x7FDD] = 0x12;
    g_img[0x7FDE] = 0xCB;
    g_img[0x7FDF] = 0xED;
}

static void test_snes(void)
{
    build_snes();
    CHECK(snes_load(&o_snes, g_img, 0x8000) == 1);
    snes_run(&o_snes, 300000u, 1500u);
    exp_reset(0x8000);
    g_exp.is_snes = 1;
    g_exp.snes_running = (uint8_t) snes_is_running(&o_snes);
    g_exp.snes_ppu_writes = o_snes.ppu_writes;
    g_exp.snes_cpu_reg_writes = o_snes.cpu_reg_writes;
    g_exp.snes_nmis = o_snes.nmis_taken;
    g_exp.running = g_exp.snes_running;
    g_exp.best_core = 816;
    g_exp.best_insn = o_snes.insn;
    CHECK(g_exp.snes_running == 1);
    CHECK(run(mkdev(64, 0)) == 1);
    SAME(&g_out, &g_exp);
    CHECK(g_ctx.calls == 128);
    CHECK(cover_now() == (BIT(COV_GAME_RUNNER_RUN) | BIT(COV_ROM_READ)));

    /* Full re-read returns only 63 sectors (< 0x8000): fall through. */
    run(mkdev(64, 64 + 64));
    CHECK(g_out.is_snes == 0);
    CHECK(g_out.insn_6502 == BUDGET);
    CHECK(g_ctx.calls == 128);
}

static void build_pce(void)
{
    clear_img();
    static const uint8_t code[] = {0x78, 0xD4, 0xD8, 0x03, 0x05, 0x13, 0xC8, 0x23, 0x00,
                                   0x03, 0x00, 0x13, 0x00, 0x23, 0x00, 0x03, 0x07, 0x13,
                                   0x00, 0x23, 0x00, 0x58, 0x80, 0xFE};
    put(0, code, sizeof code);
    g_img[0x100] = 0x40;
    g_img[0x1FF8] = 0x00;
    g_img[0x1FF9] = 0xE1;
    g_img[0x1FFE] = 0x00;
    g_img[0x1FFF] = 0xE0;
}

static void test_pce(void)
{
    build_pce();
    CHECK(pce_load(&o_pce, g_img, 0x2000) == 1);
    pce_run(&o_pce, 400000u);
    exp_reset(0x2000);
    g_exp.is_pce = 1;
    g_exp.pce_running = (uint8_t) pce_is_running(&o_pce);
    g_exp.pce_vdc_writes = o_pce.vdc_writes;
    g_exp.pce_vblank_irqs = o_pce.vblank_irqs;
    g_exp.running = g_exp.pce_running;
    g_exp.best_core = 6280;
    g_exp.best_insn = o_pce.insn;
    CHECK(g_exp.pce_running == 1);
    CHECK(run(mkdev(16, 0)) == 1);
    SAME(&g_out, &g_exp);
    CHECK(g_ctx.calls == 32);
    CHECK(cover_now() == (BIT(COV_GAME_RUNNER_RUN) | BIT(COV_ROM_READ)));

    /* Not running: entry byte is a plausible opcode but the code is a NOP
     * slide that never touches the VDC. */
    clear_img();
    g_img[0] = 0xEA;
    g_img[0x1FFE] = 0x00;
    g_img[0x1FFF] = 0xE0;
    exp_reset(0x2000);
    g_exp.is_pce = 1;
    g_exp.best_core = 6280;
    g_exp.best_insn = 400000u;
    CHECK(run(mkdev(16, 0)) == 0);
    SAME(&g_out, &g_exp);

    /* Full re-read short of 0x2000: fall through. */
    build_pce();
    run(mkdev(16, 16 + 16));
    CHECK(g_out.is_pce == 0);
    CHECK(g_out.insn_6502 == BUDGET);
    CHECK(g_out.insn_z80 > 0);
}

int main(void)
{
    test_empty();
    test_raw_z80_threshold();
    test_raw_6502_rate();
    test_raw_choice();
    test_raw_first_byte_and_ports();
    test_raw_permille_rounding();
    test_probe_cap();
    test_ines_rejected();
    test_nes();
    test_raw_tie(); /* after a NES run: the coverage set must not keep NES points */
    test_gb();
    test_gba();
    test_genesis();
    test_snes();
    test_pce();
    if (fails) {
        printf("test_emu_game_runner: %d of %d checks FAILED\n", fails, checks);
        return 1;
    }
    printf("%d checks passed\n", checks);
    return 0;
}
