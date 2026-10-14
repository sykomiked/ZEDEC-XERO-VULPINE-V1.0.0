/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_nes.c — host test for the NES mapper-0 machine (nes.c + cpu6502.c).
 *
 * With no arguments it runs a tiny NROM iNES image assembled by hand below
 * (original code written for this test, no commercial ROM) and checks the
 * real CPU, RAM mirroring, PPU/APU register map, vblank polling and NMI
 * delivery, plus the loader's bank layout and the "not running" verdict for
 * an image that never touches the PPU. Exit status is non-zero on failure.
 *
 * With file arguments it additionally runs each file through the machine and
 * prints the behavioural observation (a diagnostic, not a check):
 *   cc -Ikernel/src/emu kernel/src/emu/cpu6502.c kernel/src/emu/nes.c \
 *      kernel/src/emu/test_nes.c -o tnes && ./tnes [<file>...]
 */
#include "nes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks, g_failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        g_checks++;                                                                                \
        if (!(c)) {                                                                                \
            g_failures++;                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
        }                                                                                          \
    } while (0)

static nes_t g_nes;

/* 16KB NROM program, mirrored at $8000 and $C000; code starts at $C000.
 *
 *   C000 78        SEI
 *   C001 D8        CLD
 *   C002 A2 FF     LDX #$FF
 *   C004 9A        TXS
 *   C005 A9 00     LDA #$00
 *   C007 8D 00 20  STA $2000      ; NMI off
 *   C00A A9 42     LDA #$42
 *   C00C 85 10     STA $10        ; RAM[$10] = $42
 *   C00E A9 77     LDA #$77
 *   C010 8D 14 08  STA $0814      ; mirror of $0014
 *   C013 A9 00     LDA #$00
 *   C015 A2 05     LDX #$05
 *   C017 86 13 L1: STX $13
 *   C019 18        CLC
 *   C01A 65 13     ADC $13
 *   C01C CA        DEX
 *   C01D D0 F8     BNE L1         ; A = 5+4+3+2+1 = 15
 *   C01F 85 12     STA $12
 *   C021 A9 0F     LDA #$0F
 *   C023 8D 15 40  STA $4015      ; APU channel enable
 *   C026 A9 40     LDA #$40
 *   C028 8D 17 40  STA $4017      ; APU frame counter
 *   C02B 2C 02 20 W: BIT $2002    ; wait for vblank
 *   C02E 10 FB     BPL W
 *   C030 A9 80     LDA #$80
 *   C032 8D 00 20  STA $2000      ; NMI on
 *   C035 A9 1E     LDA #$1E
 *   C037 8D 01 20  STA $2001      ; show background + sprites
 *   C03A 4C 3A C0 M: JMP M
 *   C03D E6 16   NMI: INC $16     ; count handled NMIs
 *   C03F 40        RTI
 *   C040 40      IRQ: RTI
 *   vectors: NMI=$C03D RESET=$C000 IRQ=$C040
 */
static const uint8_t k_prog[] = {
    0x78, 0xD8, 0xA2, 0xFF, 0x9A, 0xA9, 0x00, 0x8D, 0x00, 0x20, 0xA9, 0x42, 0x85, 0x10,
    0xA9, 0x77, 0x8D, 0x14, 0x08, 0xA9, 0x00, 0xA2, 0x05, 0x86, 0x13, 0x18, 0x65, 0x13,
    0xCA, 0xD0, 0xF8, 0x85, 0x12, 0xA9, 0x0F, 0x8D, 0x15, 0x40, 0xA9, 0x40, 0x8D, 0x17,
    0x40, 0x2C, 0x02, 0x20, 0x10, 0xFB, 0xA9, 0x80, 0x8D, 0x00, 0x20, 0xA9, 0x1E, 0x8D,
    0x01, 0x20, 0x4C, 0x3A, 0xC0, 0xE6, 0x16, 0x40, 0x40,
};

static uint8_t g_img[16 + 2 * 16384];

static uint32_t build_ines(uint32_t banks, uint8_t fill)
{
    memset(g_img, fill, sizeof g_img);
    memset(g_img, 0, 16);
    g_img[0] = 'N';
    g_img[1] = 'E';
    g_img[2] = 'S';
    g_img[3] = 0x1A;
    g_img[4] = (uint8_t) banks; /* PRG-ROM in 16KB units; mapper 0, no CHR */
    return 16u + banks * 16384u;
}

static void set_vectors(uint32_t last_bank_off, uint16_t nmi, uint16_t reset, uint16_t irq)
{
    uint8_t *v = &g_img[last_bank_off + 0x3FFA];
    v[0] = (uint8_t) nmi;
    v[1] = (uint8_t) (nmi >> 8);
    v[2] = (uint8_t) reset;
    v[3] = (uint8_t) (reset >> 8);
    v[4] = (uint8_t) irq;
    v[5] = (uint8_t) (irq >> 8);
}

static void test_embedded_program(void)
{
    uint32_t len = build_ines(1, 0x00);
    memcpy(&g_img[16], k_prog, sizeof k_prog);
    set_vectors(16, 0xC03D, 0xC000, 0xC040);

    CHECK(nes_is_ines(g_img, len));
    CHECK(nes_load_ines(&g_nes, g_img, len) == 1);
    /* a single 16KB bank is mirrored at $8000 and $C000 */
    CHECK(g_nes.prg[0] == 0x78 && g_nes.prg[0x4000] == 0x78);
    CHECK(g_nes.prg[0x7FFC] == 0x00 && g_nes.prg[0x7FFD] == 0xC0);

    /* 20000 instructions, vblank every 1000: the wait loop exits at the
     * first vblank (i = 999); NMIs are then delivered at i = 1999 .. 19999
     * (19), and the last one is taken on the final step, so its handler
     * never runs: 18 handled. */
    nes_run(&g_nes, 20000u, 1000u);
    CHECK(g_nes.insn == 20000u);
    CHECK(!g_nes.cpu.jammed);
    CHECK(g_nes.cpu.illegal == 0);

    CHECK(g_nes.ram[0x10] == 0x42);   /* zero-page store       */
    CHECK(g_nes.ram[0x14] == 0x77);   /* $0814 mirrors $0014   */
    CHECK(g_nes.ram[0x12] == 15);     /* ADC/DEX/BNE loop      */
    CHECK(g_nes.ram[0x13] == 1);      /* last X stored         */
    /* SP was set to $FF; each handled NMI pushed 3 bytes and its RTI pulled
     * them, and the final pending NMI left its 3-byte frame on the stack. */
    CHECK(g_nes.cpu.sp == 0xFC);
    CHECK(g_nes.cpu.pc == 0xC03D);    /* about to run the NMI handler */

    CHECK(g_nes.ppu_ctrl == 0x80);
    CHECK(g_nes.ppu_mask == 0x1E);
    CHECK(g_nes.ppu_reg_writes == 3); /* $2000, $2000, $2001  */
    CHECK(g_nes.apu_io_writes == 2);  /* $4015, $4017         */
    CHECK(g_nes.ppu_status_reads >= 2); /* polled $2002 until vblank */
    CHECK(g_nes.nmis_taken == 19);
    CHECK(g_nes.ram[0x16] == 18);
    CHECK(nes_is_running(&g_nes));

    printf("  embedded NROM: insn=%u ppu_w=%u status_rd=%u apu=%u nmi=%u handled=%u -> %s\n",
           g_nes.insn, g_nes.ppu_reg_writes, g_nes.ppu_status_reads, g_nes.apu_io_writes,
           g_nes.nmis_taken, g_nes.ram[0x16], nes_is_running(&g_nes) ? "RUNNING" : "no");
}

static void test_bank_layout(void)
{
    /* 32KB PRG: first bank at $8000, last bank (with the vectors) at $C000. */
    uint32_t len = build_ines(2, 0xEA);
    g_img[16] = 0x11;
    g_img[16 + 16384] = 0x22;
    set_vectors(16 + 16384, 0xC000, 0xC000, 0xC000);
    CHECK(nes_load_ines(&g_nes, g_img, len) == 1);
    CHECK(g_nes.prg[0] == 0x11);
    CHECK(g_nes.prg[0x4000] == 0x22);
}

static void test_not_running(void)
{
    /* A PRG of NOPs with a valid reset vector executes cleanly but never
     * touches the PPU, so the machine must not call it a running game. */
    uint32_t len = build_ines(1, 0xEA);
    set_vectors(16, 0xC000, 0xC000, 0xC000);
    CHECK(nes_load_ines(&g_nes, g_img, len) == 1);
    nes_run(&g_nes, 5000u, 1000u);
    CHECK(g_nes.insn == 5000u);
    CHECK(g_nes.cpu.illegal == 0);
    CHECK(g_nes.ppu_reg_writes == 0);
    CHECK(!nes_is_running(&g_nes));

    /* Not iNES, and an iNES header with no PRG, are both refused. */
    static const uint8_t junk[32] = {'N', 'E', 'Z', 0x1A, 1};
    CHECK(!nes_is_ines(junk, sizeof junk));
    CHECK(nes_load_ines(&g_nes, junk, sizeof junk) == 0);
    CHECK(!nes_is_ines(g_img, 15)); /* shorter than the header */
    len = build_ines(1, 0x00);
    g_img[4] = 0;
    CHECK(nes_load_ines(&g_nes, g_img, len) == 0);
}

static void run_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("  open FAIL %s\n", path);
        return;
    }
    static uint8_t buf[1 << 20];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    const char *base = path;
    for (const char *p = path; *p; p++)
        if (*p == '/') base = p + 1;
    if (!nes_is_ines(buf, (uint32_t) n)) {
        printf("  [not-iNES]  %-40.40s (%zu bytes)\n", base, n);
    }
    if (!nes_load_ines(&g_nes, buf, (uint32_t) n)) {
        printf("  [load-skip] %-40.40s\n", base);
        return;
    }
    nes_run(&g_nes, 300000u, 2000u);
    printf("  %-8s %-40.40s ppu_w=%-5u ppu_status_rd=%-6u apu_io=%-5u nmi=%-4u illegal=%u\n",
           nes_is_running(&g_nes) ? "RUNNING" : "no", base, g_nes.ppu_reg_writes,
           g_nes.ppu_status_reads, g_nes.apu_io_writes, g_nes.nmis_taken, g_nes.cpu.illegal);
}

int main(int argc, char **argv)
{
    test_embedded_program();
    test_bank_layout();
    test_not_running();
    CHECK(nes_selfcheck() == 1);
    for (int i = 1; i < argc; i++) run_file(argv[i]);
    printf("test_nes: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
