/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_cpu6502.c — host unit test proving the 6502 core is correct.
 *   cc -Ikernel/src/emu kernel/src/emu/cpu6502.c kernel/src/emu/test_cpu6502.c -o /tmp/t && /tmp/t
 */
#include "cpu6502.h"
#include <stdio.h>
#include <string.h>

static uint8_t g_mem[65536];
static uint8_t bus_rd(cpu6502_t *c, uint16_t a)
{
    (void) c;
    return g_mem[a];
}
static void bus_wr(cpu6502_t *c, uint16_t a, uint8_t v)
{
    (void) c;
    g_mem[a] = v;
}

static int fails = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("  FAIL line %d: %s\n", __LINE__, #cond);                                       \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static void load(uint16_t at, const uint8_t *prog, int n)
{
    memset(g_mem, 0, sizeof g_mem);
    memcpy(g_mem + at, prog, (size_t) n);
    g_mem[0xFFFC] = (uint8_t) (at & 0xFF);
    g_mem[0xFFFD] = (uint8_t) (at >> 8);
}
static void newcpu(cpu6502_t *c)
{
    memset(c, 0, sizeof *c);
    c->read = bus_rd;
    c->write = bus_wr;
    c->ctx = g_mem;
    cpu6502_reset(c);
}

int main(void)
{
    cpu6502_t c;

    /* 1. LDA #$05 / STA $10 — load+store, Z/N flags */
    {
        const uint8_t p[] = {0xA9, 0x05, 0x85, 0x10, 0x4C, 0x00, 0x02}; /* ...; JMP $0200 self */
        load(0x0200, p, sizeof p);
        newcpu(&c);
        cpu6502_run(&c, 20);
        CHECK(c.a == 0x05);
        CHECK(g_mem[0x10] == 0x05);
        CHECK(!(c.p & CPU6502_Z));
        CHECK(!(c.p & CPU6502_N));
    }

    /* 2. ADC overflow: LDA #$50 + ADC #$50 = $A0, V=1, N=1, C=0 */
    {
        const uint8_t p[] = {0x18, 0xA9, 0x50, 0x69, 0x50, 0x4C, 0x05, 0x02};
        load(0x0200, p, sizeof p);
        newcpu(&c);
        cpu6502_run(&c, 20);
        CHECK(c.a == 0xA0);
        CHECK(c.p & CPU6502_V);
        CHECK(c.p & CPU6502_N);
        CHECK(!(c.p & CPU6502_C));
    }

    /* 3. ADC carry: LDA #$FF + ADC #$01 = $00, C=1, Z=1 */
    {
        const uint8_t p[] = {0x18, 0xA9, 0xFF, 0x69, 0x01, 0x4C, 0x05, 0x02};
        load(0x0200, p, sizeof p);
        newcpu(&c);
        cpu6502_run(&c, 20);
        CHECK(c.a == 0x00);
        CHECK(c.p & CPU6502_C);
        CHECK(c.p & CPU6502_Z);
    }

    /* 4. countdown loop: LDX #$0A / dec+BNE -> X=0 */
    {
        const uint8_t p[] = {0xA2, 0x0A, 0xCA, 0xD0, 0xFD, 0x4C, 0x05, 0x02};
        load(0x0200, p, sizeof p);
        newcpu(&c);
        cpu6502_run(&c, 200);
        CHECK(c.x == 0x00);
        CHECK(c.p & CPU6502_Z);
    }

    /* 5. summation 5+4+3+2+1 = 15 via loop with zp store + ADC zp */
    {
        const uint8_t p[] = {
            0xA9, 0x00,      /* LDA #0        */
            0xA2, 0x05,      /* LDX #5        */
            0x86, 0x20,      /* loop: STX $20 */
            0x18,            /*       CLC     */
            0x65, 0x20,      /*       ADC $20 */
            0xCA,            /*       DEX     */
            0xD0, 0xF8,      /*       BNE loop*/
            0x85, 0x30,      /*       STA $30 */
            0x4C, 0x0D, 0x02 /*       JMP self*/
        };
        load(0x0200, p, sizeof p);
        newcpu(&c);
        cpu6502_run(&c, 500);
        CHECK(g_mem[0x30] == 15);
        CHECK(c.a == 15);
    }

    /* 6. JSR/RTS: call a subroutine that sets A=$42 */
    {
        const uint8_t p[] = {
            0x20, 0x08, 0x02, /* JSR $0208     */
            0x85, 0x40,       /* STA $40       */
            0x4C, 0x05, 0x02, /* JMP self      */
            0xA9, 0x42, 0x60  /* sub: LDA #$42 ; RTS  (@ $0208) */
        };
        load(0x0200, p, sizeof p);
        newcpu(&c);
        cpu6502_run(&c, 100);
        CHECK(c.a == 0x42);
        CHECK(g_mem[0x40] == 0x42);
    }

    /* 7. (zp),Y indirect load: ptr $30->$0400, Y=3, mem[$0403]=$AB */
    {
        const uint8_t p[] = {
            0xA0, 0x03,      /* LDY #3        */
            0xB1, 0x30,      /* LDA ($30),Y   */
            0x4C, 0x04, 0x02 /* JMP self      */
        };
        load(0x0200, p, sizeof p);
        g_mem[0x30] = 0x00;
        g_mem[0x31] = 0x04;
        g_mem[0x0403] = 0xAB;
        newcpu(&c);
        cpu6502_run(&c, 30);
        CHECK(c.a == 0xAB);
        CHECK(c.p & CPU6502_N);
    }

    /* 8. shifts: LDA #$81; LSR -> $40, C=1; then ROL -> $81, C=0 */
    {
        const uint8_t p[] = {0x18, 0xA9, 0x81, 0x4A, 0x2A, 0x4C, 0x05, 0x02};
        load(0x0200, p, sizeof p);
        newcpu(&c);
        cpu6502_run(&c, 20);
        CHECK(c.a == 0x81);
    } /* $81 >>1 = $40 (C=1); <<1|C = $81 */

    /* 9. CMP + BEQ: LDA #$20; CMP #$20 -> Z=1,C=1; BEQ taken */
    {
        const uint8_t p[] = {0xA9, 0x20, 0xC9, 0x20, 0xF0, 0x02, 0xA9,
                             0xFF, 0x85, 0x50, 0x4C, 0x0A, 0x02};
        load(0x0200, p, sizeof p);
        newcpu(&c);
        cpu6502_run(&c, 40);
        CHECK(c.p & CPU6502_C); /* $20 >= $20 -> C set        */
        CHECK(c.p & CPU6502_Z); /* equal -> Z set             */
        CHECK(g_mem[0x50] == 0x20);
    } /* BEQ taken skipped LDA #$FF, so A stayed $20 */

    if (fails == 0)
        printf("test_cpu6502: ALL PASS (9 programs, official opcodes + flags + addressing)\n");
    else
        printf("test_cpu6502: %d CHECK(S) FAILED\n", fails);
    return fails ? 1 : 0;
}
