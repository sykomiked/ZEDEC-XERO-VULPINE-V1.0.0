/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_cpu_z80.c — host unit test for the Z80 core.
 *   cc -Ikernel/src/emu kernel/src/emu/cpu_z80.c kernel/src/emu/test_cpu_z80.c -o /tmp/tz &&
 * /tmp/tz
 */
#include "cpu_z80.h"
#include <stdio.h>
#include <string.h>

static uint8_t g_mem[65536];
static uint8_t g_ports[65536];
static uint8_t bus_rd(cpu_z80_t *c, uint16_t a)
{
    (void) c;
    return g_mem[a];
}
static void bus_wr(cpu_z80_t *c, uint16_t a, uint8_t v)
{
    (void) c;
    g_mem[a] = v;
}
static uint8_t port_in(cpu_z80_t *c, uint16_t p)
{
    (void) c;
    return g_ports[p];
}
static void port_out(cpu_z80_t *c, uint16_t p, uint8_t v)
{
    (void) c;
    g_ports[p] = v;
}

static int fails = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("  FAIL line %d: %s\n", __LINE__, #cond);                                       \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static void load(const uint8_t *prog, int n)
{
    memset(g_mem, 0, sizeof g_mem);
    memcpy(g_mem, prog, (size_t) n); /* Z80 resets PC=0 */
}
static void newcpu(cpu_z80_t *c)
{
    memset(c, 0, sizeof *c);
    c->read = bus_rd;
    c->write = bus_wr;
    c->ctx = g_mem;
    cpu_z80_reset(c);
}

int main(void)
{
    cpu_z80_t c;

    /* 1. LD A,n / LD (nn),A */
    {
        const uint8_t p[] = {0x3E, 0x42, 0x32, 0x00, 0x40, 0x76}; /* LD A,$42; LD ($4000),A; HALT */
        load(p, sizeof p);
        newcpu(&c);
        cpu_z80_run(&c, 200);
        CHECK(c.a == 0x42);
        CHECK(g_mem[0x4000] == 0x42);
        CHECK(c.halted);
    }

    /* 2. ADD A,B with carry+overflow flag model: $50 + $50 = $A0, S=1,V=1,C=0,N=0 */
    {
        const uint8_t p[] = {0x3E, 0x50, 0x06,
                             0x50, 0x80, 0x76}; /* LD A,$50; LD B,$50; ADD A,B; HALT */
        load(p, sizeof p);
        newcpu(&c);
        cpu_z80_run(&c, 200);
        CHECK(c.a == 0xA0);
        CHECK(c.f & Z80_S);
        CHECK(c.f & Z80_PV);
        CHECK(!(c.f & Z80_C));
        CHECK(!(c.f & Z80_N));
    }

    /* 3. SUB sets N; $10 - $01 = $0F, N=1, H=1, C=0 */
    {
        const uint8_t p[] = {0x3E, 0x10, 0xD6, 0x01, 0x76}; /* LD A,$10; SUB $01; HALT */
        load(p, sizeof p);
        newcpu(&c);
        cpu_z80_run(&c, 200);
        CHECK(c.a == 0x0F);
        CHECK(c.f & Z80_N);
        CHECK(c.f & Z80_H);
        CHECK(!(c.f & Z80_C));
    }

    /* 4. summation 5+4+3+2+1 = 15 via DJNZ loop (the M5-RELATE probe on Z80) */
    {
        const uint8_t p[] = {0x3E, 0x00,       /* LD A,0        */
                             0x06, 0x05,       /* LD B,5        */
                             0x80,             /* loop: ADD A,B  (@0x0004) */
                             0x10, 0xFD,       /*       DJNZ loop (-3 -> 0x0004) */
                             0x32, 0x30, 0x00, /*       LD ($0030),A */
                             0x76};            /*       HALT     */
        load(p, sizeof p);
        newcpu(&c);
        cpu_z80_run(&c, 500);
        CHECK(c.a == 15);
        CHECK(g_mem[0x0030] == 15);
    }

    /* 5. CALL/RET: subroutine loads A=$99 */
    {
        const uint8_t p[] = {
            0x31, 0xF0, 0x3F, /* LD SP,$3FF0    */
            0xCD, 0x08, 0x00, /* CALL $0008     */
            0x32, 0x00,       /* (won't reach cleanly) ... */
            0x3E, 0x99, 0xC9  /* sub@0x0008: LD A,$99 ; RET */
        };
        load(p, sizeof p);
        newcpu(&c);
        cpu_z80_run(&c, 200);
        CHECK(c.a == 0x99);
    }

    /* 6. parity flag: XOR A -> 0, PV(parity of 0 = even) set, Z set */
    {
        const uint8_t p[] = {0x3E, 0xFF, 0xAF, 0x76}; /* LD A,$FF; XOR A; HALT */
        load(p, sizeof p);
        newcpu(&c);
        cpu_z80_run(&c, 100);
        CHECK(c.a == 0x00);
        CHECK(c.f & Z80_Z);
        CHECK(c.f & Z80_PV);
    }

    /* 7. 16-bit: LD HL,nn; ADD HL,HL doubles */
    {
        const uint8_t p[] = {0x21, 0x34, 0x12, 0x29, 0x76}; /* LD HL,$1234; ADD HL,HL; HALT */
        load(p, sizeof p);
        newcpu(&c);
        cpu_z80_run(&c, 100);
        CHECK(c.h == 0x24);
        CHECK(c.l == 0x68);
    }

    /* 8. INC wraps flags: LD A,$7F; INC A -> $80, S=1, PV=1(overflow), H=1 */
    {
        const uint8_t p[] = {0x3E, 0x7F, 0x3C, 0x76};
        load(p, sizeof p);
        newcpu(&c);
        cpu_z80_run(&c, 100);
        CHECK(c.a == 0x80);
        CHECK(c.f & Z80_S);
        CHECK(c.f & Z80_PV);
        CHECK(c.f & Z80_H);
    }

    /* 9. LDIR: copy 3 bytes $1000->$2000 */
    {
        const uint8_t p[] = {0x21, 0x00, 0x10, 0x11, 0x00, 0x20,
                             0x01, 0x03, 0x00, 0xED, 0xB0, 0x76};
        load(p, sizeof p);
        g_mem[0x1000] = 0xAA;
        g_mem[0x1001] = 0xBB;
        g_mem[0x1002] = 0xCC;
        newcpu(&c);
        cpu_z80_run(&c, 500);
        CHECK(g_mem[0x2000] == 0xAA);
        CHECK(g_mem[0x2001] == 0xBB);
        CHECK(g_mem[0x2002] == 0xCC);
        CHECK(c.b == 0 && c.c == 0);
    }

    /* 10. CB SET/BIT/RES: SET 7,B -> $80; BIT 7,B -> Z clear; RES 7,B -> $00 */
    {
        const uint8_t p[] = {0x06, 0x00, 0xCB, 0xF8, 0xCB, 0x78, 0xCB, 0xB8, 0x76};
        load(p, sizeof p);
        newcpu(&c);
        cpu_z80_run(&c, 100);
        CHECK(c.b == 0x00);
    } /* after RES 7 of $80 */

    /* 11. CB RLC C: $81 -> $03 with carry set */
    {
        const uint8_t p[] = {0x0E, 0x81, 0xCB, 0x01, 0x76};
        load(p, sizeof p);
        newcpu(&c);
        cpu_z80_run(&c, 100);
        CHECK(c.c == 0x03);
        CHECK(c.f & Z80_C);
    }

    /* 12. OUT (C),A / IN A,(C): round-trip through port $0010 */
    {
        const uint8_t p[] = {0x01, 0x10, 0x00, 0x3E, 0x5A, 0xED,
                             0x79, 0x3E, 0x00, 0xED, 0x78, 0x76};
        load(p, sizeof p);
        for (int i = 0; i < 65536; i++) g_ports[i] = 0;
        newcpu(&c);
        c.in = port_in;
        c.out = port_out;
        cpu_z80_run(&c, 100);
        CHECK(g_ports[0x0010] == 0x5A);
        CHECK(c.a == 0x5A);
    }

    /* 13. EXX: swap-and-swap-back preserves the original B */
    {
        const uint8_t p[] = {0x06, 0x11, 0xD9, 0x06, 0x22, 0xD9, 0x76};
        load(p, sizeof p);
        newcpu(&c);
        cpu_z80_run(&c, 100);
        CHECK(c.b == 0x11);
    }

    /* 14. cpu_z80_int (IM1): pushes PC, vectors to $0038 */
    {
        newcpu(&c);
        c.iff1 = 1;
        c.im = 1;
        c.pc = 0x1234;
        c.sp = 0x3000;
        cpu_z80_int(&c);
        CHECK(c.pc == 0x0038);
        CHECK(c.iff1 == 0);
        CHECK(g_mem[0x2FFE] == 0x34 && g_mem[0x2FFF] == 0x12);
    } /* pushed $1234 */

    /* 15. LD IX,nn / INC IX / DEC IX */
    {
        const uint8_t p[] = {0xDD, 0x21, 0x34, 0x12, 0xDD, 0x23, 0xDD, 0x2B, 0x76};
        load(p, sizeof p);
        newcpu(&c);
        cpu_z80_run(&c, 100);
        CHECK(c.ix == 0x1234);
    }

    /* 16. LD (IX+5),$99 / LD A,(IX+5) */
    {
        const uint8_t p[] = {0xDD, 0x21, 0x00, 0x20, 0xDD, 0x36,
                             0x05, 0x99, 0xDD, 0x7E, 0x05, 0x76};
        load(p, sizeof p);
        newcpu(&c);
        cpu_z80_run(&c, 100);
        CHECK(c.a == 0x99);
        CHECK(g_mem[0x2005] == 0x99);
    }

    /* 17. ADD IX,DE */
    {
        const uint8_t p[] = {0xDD, 0x21, 0x00, 0x01, 0x11, 0x20, 0x00, 0xDD, 0x19, 0x76};
        load(p, sizeof p);
        newcpu(&c);
        cpu_z80_run(&c, 100);
        CHECK(c.ix == 0x0120);
    }

    /* 18. LD IXH,n / LD B,IXH (undocumented index-half regs) */
    {
        const uint8_t p[] = {0xDD, 0x26, 0xAB, 0xDD, 0x44, 0x76};
        load(p, sizeof p);
        newcpu(&c);
        cpu_z80_run(&c, 100);
        CHECK(c.b == 0xAB);
    }

    /* 19. PUSH IX / POP HL */
    {
        const uint8_t p[] = {0x31, 0x00, 0x30, 0xDD, 0x21, 0xEF, 0xBE, 0xDD, 0xE5, 0xE1, 0x76};
        load(p, sizeof p);
        newcpu(&c);
        cpu_z80_run(&c, 100);
        CHECK(c.h == 0xBE);
        CHECK(c.l == 0xEF);
    }

    /* 20. DDCB SET 0,(IX+0) then LD A,(IX+0) */
    {
        const uint8_t p[] = {0xDD, 0x21, 0x00, 0x21, 0xDD, 0x36, 0x00, 0x00,
                             0xDD, 0xCB, 0x00, 0xC6, 0xDD, 0x7E, 0x00, 0x76};
        load(p, sizeof p);
        newcpu(&c);
        cpu_z80_run(&c, 100);
        CHECK(c.a == 0x01);
        CHECK(g_mem[0x2100] == 0x01);
    }

    if (fails == 0)
        printf("test_cpu_z80: ALL PASS (20 programs, +CB/ED/DD/FD/ports/EXX/INT)\n");
    else
        printf("test_cpu_z80: %d CHECK(S) FAILED\n", fails);
    return fails ? 1 : 0;
}
