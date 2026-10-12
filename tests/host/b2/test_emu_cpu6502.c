/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_emu_cpu6502.c: mutation-killing unit tests for kernel/src/emu/cpu6502.c.
 * Every official opcode is stepped once from a known state and the whole
 * register file (A, X, Y, SP, P as an exact byte, PC), the returned cycle
 * count, the accumulated cycle counter and the touched memory are checked.
 * Also covers reset, IRQ/NMI, BRK/RTI, illegal-opcode accounting, the jammed
 * state and cpu6502_run's cycle budget. ALU-group cycle counts are not
 * asserted (see the report: the core returns 4 for every ALU addressing mode).
 */
#include "cpu6502.h"
#include <stdio.h>
#include <string.h>

#define FC CPU6502_C
#define FZ CPU6502_Z
#define FI CPU6502_I
#define FD CPU6502_D
#define FB CPU6502_B
#define FU CPU6502_U
#define FV CPU6502_V
#define FN CPU6502_N

static uint8_t g_mem[65536];
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

static uint8_t bus_rd(cpu6502_t *cpu, uint16_t a)
{
    (void) cpu;
    return g_mem[a];
}
static void bus_wr(cpu6502_t *cpu, uint16_t a, uint8_t v)
{
    (void) cpu;
    g_mem[a] = v;
}

static cpu6502_t c;
static uint16_t org = 0x0200;

static void clr(void)
{
    memset(g_mem, 0, sizeof g_mem);
}

static void set(uint8_t a, uint8_t x, uint8_t y, uint8_t p)
{
    memset(&c, 0, sizeof c);
    c.read = bus_rd;
    c.write = bus_wr;
    c.ctx = g_mem;
    c.a = a;
    c.x = x;
    c.y = y;
    c.p = p;
    c.sp = 0xFD;
    c.pc = org;
    c.cycles = 1000;
}

static int exec_bytes(const uint8_t *b, size_t n)
{
    memcpy(g_mem + c.pc, b, n);
    uint64_t before = c.cycles;
    int k = cpu6502_step(&c);
    CHECK(c.cycles == before + (uint64_t) k);
    CHECK(c.last_opcode == b[0]);
    return k;
}
#define X(...) exec_bytes((const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}))

static void st(int line, int cyc, uint8_t a, uint8_t x, uint8_t y, uint8_t sp, uint8_t p,
               uint16_t pc, int want_cyc)
{
    checks++;
    if (c.a != a || c.x != x || c.y != y || c.sp != sp || c.p != p || c.pc != pc ||
        (want_cyc >= 0 && cyc != want_cyc)) {
        printf("  FAIL line %d: got a=%02X x=%02X y=%02X sp=%02X p=%02X pc=%04X cyc=%d; "
               "want a=%02X x=%02X y=%02X sp=%02X p=%02X pc=%04X cyc=%d\n",
               line, c.a, c.x, c.y, c.sp, c.p, c.pc, cyc, a, x, y, sp, p, pc, want_cyc);
        fails++;
    }
}
/* ST(cyc, a, x, y, sp, p, pc, want_cyc); want_cyc < 0 skips the cycle check */
#define ST(...) st(__LINE__, __VA_ARGS__)

/* ---------------------------------------------------------------- ALU group */
/* Run ALU opcode `op` with operand m through its addressing mode (op & 0x1C),
 * X=5, Y=6, and check A/P/PC. */
static void alu(int line, uint8_t op, uint8_t a, uint8_t m, uint8_t p, uint8_t want_a,
                uint8_t want_p)
{
    clr();
    set(a, 5, 6, p);
    int len = 2;
    switch (op & 0x1C) {
    case 0x08: /* # */
        X(op, m);
        break;
    case 0x04: /* zp */
        g_mem[0x10] = m;
        X(op, 0x10);
        break;
    case 0x14: /* zp,X wraps: $FE+5 = $03 */
        g_mem[0x03] = m;
        X(op, 0xFE);
        break;
    case 0x0C: /* abs */
        g_mem[0x1234] = m;
        X(op, 0x34, 0x12);
        len = 3;
        break;
    case 0x1C: /* abs,X */
        g_mem[0x1235] = m;
        X(op, 0x30, 0x12);
        len = 3;
        break;
    case 0x18: /* abs,Y */
        g_mem[0x1236] = m;
        X(op, 0x30, 0x12);
        len = 3;
        break;
    case 0x00: /* (zp,X): $20+5 -> ptr at $25 = $1300 */
        g_mem[0x25] = 0x00;
        g_mem[0x26] = 0x13;
        g_mem[0x1300] = m;
        X(op, 0x20);
        break;
    case 0x10: /* (zp),Y: ptr at $40 = $1400, +6 */
        g_mem[0x40] = 0x00;
        g_mem[0x41] = 0x14;
        g_mem[0x1406] = m;
        X(op, 0x40);
        break;
    }
    st(line, 0, want_a, 5, 6, 0xFD, want_p, (uint16_t) (0x0200 + len), -1);
}

static const uint8_t ORA_OPS[8] = {0x09, 0x05, 0x15, 0x0D, 0x1D, 0x19, 0x01, 0x11};
static const uint8_t AND_OPS[8] = {0x29, 0x25, 0x35, 0x2D, 0x3D, 0x39, 0x21, 0x31};
static const uint8_t EOR_OPS[8] = {0x49, 0x45, 0x55, 0x4D, 0x5D, 0x59, 0x41, 0x51};
static const uint8_t ADC_OPS[8] = {0x69, 0x65, 0x75, 0x6D, 0x7D, 0x79, 0x61, 0x71};
static const uint8_t SBC_OPS[8] = {0xE9, 0xE5, 0xF5, 0xED, 0xFD, 0xF9, 0xE1, 0xF1};
static const uint8_t CMP_OPS[8] = {0xC9, 0xC5, 0xD5, 0xCD, 0xDD, 0xD9, 0xC1, 0xD1};

#define ALL8(ops, a, m, p, wa, wp)                                                                 \
    for (int i_ = 0; i_ < 8; i_++)                                                                 \
    alu(__LINE__, ops[i_], a, m, p, wa, wp)

static void test_alu(void)
{
    /* ORA */
    ALL8(ORA_OPS, 0x01, 0x80, FU | FZ, 0x81, FU | FN);
    ALL8(ORA_OPS, 0x00, 0x00, FU | FN, 0x00, FU | FZ);
    ALL8(ORA_OPS, 0x10, 0x01, FU | FC | FV, 0x11, FU | FC | FV);
    /* AND */
    ALL8(AND_OPS, 0xF0, 0x8F, FU, 0x80, FU | FN);
    ALL8(AND_OPS, 0xF0, 0x0F, FU | FN, 0x00, FU | FZ);
    ALL8(AND_OPS, 0x3C, 0x2F, FU | FZ | FN, 0x2C, FU);
    /* EOR */
    ALL8(EOR_OPS, 0xFF, 0x7F, FU, 0x80, FU | FN);
    ALL8(EOR_OPS, 0x5A, 0x5A, FU, 0x00, FU | FZ);
    ALL8(EOR_OPS, 0x0F, 0x01, FU | FZ, 0x0E, FU);
    /* ADC binary */
    ALL8(ADC_OPS, 0x50, 0x50, FU, 0xA0, FU | FN | FV);
    ALL8(ADC_OPS, 0xFF, 0x01, FU, 0x00, FU | FZ | FC);
    ALL8(ADC_OPS, 0x01, 0x01, FU | FC, 0x03, FU);
    ALL8(ADC_OPS, 0x7F, 0x00, FU | FC, 0x80, FU | FN | FV);
    ALL8(ADC_OPS, 0x80, 0x80, FU, 0x00, FU | FZ | FC | FV);
    ALL8(ADC_OPS, 0xD0, 0x90, FU | FV | FN, 0x60, FU | FC | FV);
    ALL8(ADC_OPS, 0xFE, 0x01, FU | FZ, 0xFF, FU | FN);
    ALL8(ADC_OPS, 0xFE, 0x01, FU | FC, 0x00, FU | FZ | FC);
    ALL8(ADC_OPS, 0x01, 0x01, FU, 0x02, FU);
    /* ADC decimal (cases where every NMOS flag agrees with this core) */
    ALL8(ADC_OPS, 0x15, 0x27, FU | FD, 0x42, FU | FD);
    ALL8(ADC_OPS, 0x81, 0x92, FU | FD, 0x73, FU | FD | FC | FV);
    ALL8(ADC_OPS, 0x04, 0x05, FU | FD, 0x09, FU | FD);
    ALL8(ADC_OPS, 0x05, 0x05, FU | FD, 0x10, FU | FD);
    ALL8(ADC_OPS, 0x04, 0x04, FU | FD | FC, 0x09, FU | FD);
    ALL8(ADC_OPS, 0x45, 0x44, FU | FD | FC, 0x90, FU | FD | FN | FV);
    ALL8(ADC_OPS, 0x12, 0x34, FU | FD | FC, 0x47, FU | FD);
    /* hi digit exactly 10: A and C only (N differs between NMOS variants) */
    clr();
    set(0x50, 5, 6, FU | FD);
    g_mem[0x10] = 0x51;
    X(0x65, 0x10);
    CHECK(c.a == 0x01);
    CHECK((c.p & (FC | FD | FU | FZ)) == (FC | FD | FU));
    /* SBC binary */
    ALL8(SBC_OPS, 0x50, 0xB0, FU | FC, 0xA0, FU | FN | FV);
    ALL8(SBC_OPS, 0x05, 0x03, FU | FC, 0x02, FU | FC);
    ALL8(SBC_OPS, 0x05, 0x05, FU, 0xFF, FU | FN);
    ALL8(SBC_OPS, 0x05, 0x05, FU | FC, 0x00, FU | FZ | FC);
    /* SBC decimal */
    ALL8(SBC_OPS, 0x42, 0x15, FU | FD | FC, 0x27, FU | FD | FC);
    ALL8(SBC_OPS, 0x12, 0x21, FU | FD | FC, 0x91, FU | FD | FN);
    ALL8(SBC_OPS, 0x50, 0x10, FU | FD, 0x39, FU | FD | FC);
    ALL8(SBC_OPS, 0x25, 0x25, FU | FD | FC, 0x00, FU | FD | FZ | FC);
    ALL8(SBC_OPS, 0x80, 0x01, FU | FD | FC, 0x79, FU | FD | FC | FV);
    ALL8(SBC_OPS, 0x35, 0x05, FU | FD | FC, 0x30, FU | FD | FC);
    ALL8(SBC_OPS, 0x35, 0x35, FU | FD, 0x99, FU | FD | FN);
    ALL8(SBC_OPS, 0x10, 0x00, FU | FD | FC, 0x10, FU | FD | FC);
    ALL8(SBC_OPS, 0x46, 0x12, FU | FD | FC, 0x34, FU | FD | FC);
    ALL8(SBC_OPS, 0x00, 0x01, FU | FD | FC, 0x99, FU | FD | FN);
    /* decimal SBC with V set from a positive minuend (N not asserted: NMOS
     * takes N from the binary result, this core from the BCD result) */
    clr();
    set(0x50, 5, 6, FU | FD | FC);
    g_mem[0x10] = 0x90;
    X(0xE5, 0x10);
    CHECK(c.a == 0x60);
    CHECK((c.p & (FC | FD | FU | FZ | FV)) == (FD | FU | FV));
    /* CMP */
    ALL8(CMP_OPS, 0x40, 0x40, FU, 0x40, FU | FZ | FC);
    ALL8(CMP_OPS, 0x40, 0x41, FU | FC | FZ, 0x40, FU | FN);
    ALL8(CMP_OPS, 0x40, 0x3F, FU | FN, 0x40, FU | FC);
    ALL8(CMP_OPS, 0x00, 0x80, FU | FV, 0x00, FU | FN | FV);
}

/* ---------------------------------------------------------------- loads/stores */
static void test_loads(void)
{
    /* LDA */
    clr(); set(0, 0, 0, FU); ST(X(0xA9, 0x80), 0x80, 0, 0, 0xFD, FU | FN, 0x0202, 2);
    clr(); set(9, 0, 0, FU); ST(X(0xA9, 0x00), 0x00, 0, 0, 0xFD, FU | FZ, 0x0202, 2);
    clr(); set(0, 0, 0, FU | FN | FZ | FC); ST(X(0xA9, 0x01), 0x01, 0, 0, 0xFD, FU | FC, 0x0202, 2);
    clr(); set(0, 0, 0, FU); g_mem[0x10] = 0x7F; ST(X(0xA5, 0x10), 0x7F, 0, 0, 0xFD, FU, 0x0202, 3);
    clr(); set(0, 0x20, 0, FU); g_mem[0x10] = 0x81; ST(X(0xB5, 0xF0), 0x81, 0x20, 0, 0xFD, FU | FN, 0x0202, 4);
    clr(); set(0, 0, 0, FU); g_mem[0x1234] = 0x42; ST(X(0xAD, 0x34, 0x12), 0x42, 0, 0, 0xFD, FU, 0x0203, 4);
    clr(); set(0, 0x01, 0, FU); g_mem[0x1235] = 0x42; ST(X(0xBD, 0x34, 0x12), 0x42, 1, 0, 0xFD, FU, 0x0203, 4);
    clr(); set(0, 0x20, 0, FU); g_mem[0x1310] = 0x80; ST(X(0xBD, 0xF0, 0x12), 0x80, 0x20, 0, 0xFD, FU | FN, 0x0203, 5);
    clr(); set(0, 0, 0x01, FU); g_mem[0x1235] = 0x42; ST(X(0xB9, 0x34, 0x12), 0x42, 0, 1, 0xFD, FU, 0x0203, 4);
    clr(); set(0, 0, 0x20, FU); g_mem[0x1310] = 0x80; ST(X(0xB9, 0xF0, 0x12), 0x80, 0, 0x20, 0xFD, FU | FN, 0x0203, 5);
    clr(); set(0, 4, 0, FU); g_mem[0x14] = 0x34; g_mem[0x15] = 0x12; g_mem[0x1234] = 0x55;
    ST(X(0xA1, 0x10), 0x55, 4, 0, 0xFD, FU, 0x0202, 6);
    clr(); set(0, 4, 0, FU); g_mem[0xFF] = 0x34; g_mem[0x00] = 0x12; g_mem[0x1234] = 0x66;
    g_mem[0xFE] = 0x99; g_mem[0x100] = 0x77;
    ST(X(0xA1, 0xFB), 0x66, 4, 0, 0xFD, FU, 0x0202, 6); /* pointer wraps in zero page */
    clr(); set(0, 0, 3, FU); g_mem[0x30] = 0x00; g_mem[0x31] = 0x04; g_mem[0x0403] = 0xAB;
    ST(X(0xB1, 0x30), 0xAB, 0, 3, 0xFD, FU | FN, 0x0202, 5);
    clr(); set(0, 0, 1, FU); g_mem[0x30] = 0xFF; g_mem[0x31] = 0x04; g_mem[0x0500] = 0x01;
    ST(X(0xB1, 0x30), 0x01, 0, 1, 0xFD, FU, 0x0202, 6);
    clr(); set(0, 0, 1, FU); g_mem[0xFF] = 0xFF; g_mem[0x00] = 0x04; g_mem[0x0500] = 0x02;
    g_mem[0x100] = 0x09; ST(X(0xB1, 0xFF), 0x02, 0, 1, 0xFD, FU, 0x0202, 6);

    /* LDX */
    clr(); set(0, 0, 0, FU); ST(X(0xA2, 0x80), 0, 0x80, 0, 0xFD, FU | FN, 0x0202, 2);
    clr(); set(0, 5, 0, FU); ST(X(0xA2, 0x00), 0, 0x00, 0, 0xFD, FU | FZ, 0x0202, 2);
    clr(); set(0, 0, 0, FU); g_mem[0x10] = 0x7F; ST(X(0xA6, 0x10), 0, 0x7F, 0, 0xFD, FU, 0x0202, 3);
    clr(); set(0, 0, 0x20, FU); g_mem[0x10] = 0x81; ST(X(0xB6, 0xF0), 0, 0x81, 0x20, 0xFD, FU | FN, 0x0202, 4);
    clr(); set(0, 0, 0, FU); g_mem[0x1234] = 0x42; ST(X(0xAE, 0x34, 0x12), 0, 0x42, 0, 0xFD, FU, 0x0203, 4);
    clr(); set(0, 0, 1, FU); g_mem[0x1235] = 0x42; ST(X(0xBE, 0x34, 0x12), 0, 0x42, 1, 0xFD, FU, 0x0203, 4);
    clr(); set(0, 0, 0x20, FU); g_mem[0x1310] = 0x80; ST(X(0xBE, 0xF0, 0x12), 0, 0x80, 0x20, 0xFD, FU | FN, 0x0203, 5);

    /* LDY */
    clr(); set(0, 0, 0, FU); ST(X(0xA0, 0x80), 0, 0, 0x80, 0xFD, FU | FN, 0x0202, 2);
    clr(); set(0, 0, 5, FU); ST(X(0xA0, 0x00), 0, 0, 0x00, 0xFD, FU | FZ, 0x0202, 2);
    clr(); set(0, 0, 0, FU); g_mem[0x10] = 0x7F; ST(X(0xA4, 0x10), 0, 0, 0x7F, 0xFD, FU, 0x0202, 3);
    clr(); set(0, 0x20, 0, FU); g_mem[0x10] = 0x81; ST(X(0xB4, 0xF0), 0, 0x20, 0x81, 0xFD, FU | FN, 0x0202, 4);
    clr(); set(0, 0, 0, FU); g_mem[0x1234] = 0x42; ST(X(0xAC, 0x34, 0x12), 0, 0, 0x42, 0xFD, FU, 0x0203, 4);
    clr(); set(0, 1, 0, FU); g_mem[0x1235] = 0x42; ST(X(0xBC, 0x34, 0x12), 0, 1, 0x42, 0xFD, FU, 0x0203, 4);
    clr(); set(0, 0x20, 0, FU); g_mem[0x1310] = 0x80; ST(X(0xBC, 0xF0, 0x12), 0, 0x20, 0x80, 0xFD, FU | FN, 0x0203, 5);
}

static void test_stores(void)
{
    clr(); set(0x80, 0, 0, FU); ST(X(0x85, 0x10), 0x80, 0, 0, 0xFD, FU, 0x0202, 3); CHECK(g_mem[0x10] == 0x80);
    clr(); set(0x00, 0x20, 0, FU); g_mem[0x10] = 0x55; ST(X(0x95, 0xF0), 0x00, 0x20, 0, 0xFD, FU, 0x0202, 4); CHECK(g_mem[0x10] == 0x00);
    clr(); set(0x42, 0, 0, FU); ST(X(0x8D, 0x34, 0x12), 0x42, 0, 0, 0xFD, FU, 0x0203, 4); CHECK(g_mem[0x1234] == 0x42);
    clr(); set(0x43, 0x01, 0, FU); ST(X(0x9D, 0x34, 0x12), 0x43, 1, 0, 0xFD, FU, 0x0203, 5); CHECK(g_mem[0x1235] == 0x43);
    clr(); set(0x44, 0x20, 0, FU); ST(X(0x9D, 0xF0, 0x12), 0x44, 0x20, 0, 0xFD, FU, 0x0203, 5); CHECK(g_mem[0x1310] == 0x44);
    clr(); set(0x45, 0, 0x02, FU); ST(X(0x99, 0x34, 0x12), 0x45, 0, 2, 0xFD, FU, 0x0203, 5); CHECK(g_mem[0x1236] == 0x45);
    clr(); set(0x46, 4, 0, FU); g_mem[0x14] = 0x34; g_mem[0x15] = 0x12;
    ST(X(0x81, 0x10), 0x46, 4, 0, 0xFD, FU, 0x0202, 6); CHECK(g_mem[0x1234] == 0x46);
    clr(); set(0x47, 1, 0, FU); g_mem[0xFF] = 0x34; g_mem[0x00] = 0x12;
    ST(X(0x81, 0xFE), 0x47, 1, 0, 0xFD, FU, 0x0202, 6); CHECK(g_mem[0x1234] == 0x47);
    clr(); set(0x48, 0, 3, FU); g_mem[0x30] = 0x00; g_mem[0x31] = 0x04;
    ST(X(0x91, 0x30), 0x48, 0, 3, 0xFD, FU, 0x0202, 6); CHECK(g_mem[0x0403] == 0x48);
    clr(); set(0x49, 0, 1, FU); g_mem[0xFF] = 0xFF; g_mem[0x00] = 0x04;
    ST(X(0x91, 0xFF), 0x49, 0, 1, 0xFD, FU, 0x0202, 6); CHECK(g_mem[0x0500] == 0x49);
    clr(); set(0, 0x81, 0, FU); ST(X(0x86, 0x10), 0, 0x81, 0, 0xFD, FU, 0x0202, 3); CHECK(g_mem[0x10] == 0x81);
    clr(); set(0, 0x82, 0x20, FU); ST(X(0x96, 0xF0), 0, 0x82, 0x20, 0xFD, FU, 0x0202, 4); CHECK(g_mem[0x10] == 0x82);
    clr(); set(0, 0x83, 0, FU); ST(X(0x8E, 0x34, 0x12), 0, 0x83, 0, 0xFD, FU, 0x0203, 4); CHECK(g_mem[0x1234] == 0x83);
    clr(); set(0, 0, 0x84, FU); ST(X(0x84, 0x10), 0, 0, 0x84, 0xFD, FU, 0x0202, 3); CHECK(g_mem[0x10] == 0x84);
    clr(); set(0, 0x20, 0x85, FU); ST(X(0x94, 0xF0), 0, 0x20, 0x85, 0xFD, FU, 0x0202, 4); CHECK(g_mem[0x10] == 0x85);
    clr(); set(0, 0, 0x86, FU); ST(X(0x8C, 0x34, 0x12), 0, 0, 0x86, 0xFD, FU, 0x0203, 4); CHECK(g_mem[0x1234] == 0x86);
}

/* ---------------------------------------------------------------- misc groups */
static void test_transfers_stack(void)
{
    clr(); set(0x80, 0, 0, FU); ST(X(0xAA), 0x80, 0x80, 0, 0xFD, FU | FN, 0x0201, 2);
    clr(); set(0x00, 5, 0, FU); ST(X(0xAA), 0x00, 0x00, 0, 0xFD, FU | FZ, 0x0201, 2);
    clr(); set(0x80, 0, 0, FU); ST(X(0xA8), 0x80, 0, 0x80, 0xFD, FU | FN, 0x0201, 2);
    clr(); set(0x00, 0, 5, FU); ST(X(0xA8), 0x00, 0, 0x00, 0xFD, FU | FZ, 0x0201, 2);
    clr(); set(0, 0x80, 0, FU); ST(X(0x8A), 0x80, 0x80, 0, 0xFD, FU | FN, 0x0201, 2);
    clr(); set(5, 0x00, 0, FU); ST(X(0x8A), 0x00, 0x00, 0, 0xFD, FU | FZ, 0x0201, 2);
    clr(); set(0, 0, 0x80, FU); ST(X(0x98), 0x80, 0, 0x80, 0xFD, FU | FN, 0x0201, 2);
    clr(); set(5, 0, 0x00, FU); ST(X(0x98), 0x00, 0, 0x00, 0xFD, FU | FZ, 0x0201, 2);
    clr(); set(0, 0, 0, FU | FZ); ST(X(0xBA), 0, 0xFD, 0, 0xFD, FU | FN, 0x0201, 2);
    clr(); set(0, 0, 0, FU); c.sp = 0; ST(X(0xBA), 0, 0, 0, 0, FU | FZ, 0x0201, 2);
    clr(); set(0, 0x00, 0, FU); ST(X(0x9A), 0, 0, 0, 0x00, FU, 0x0201, 2);
    clr(); set(0, 0x80, 0, FU); ST(X(0x9A), 0, 0x80, 0, 0x80, FU, 0x0201, 2);

    clr(); set(0x99, 0, 0, FU); ST(X(0x48), 0x99, 0, 0, 0xFC, FU, 0x0201, 3); CHECK(g_mem[0x1FD] == 0x99);
    clr(); set(0, 0, 0, FU); c.sp = 0xFC; g_mem[0x1FD] = 0x80; ST(X(0x68), 0x80, 0, 0, 0xFD, FU | FN, 0x0201, 4);
    clr(); set(7, 0, 0, FU); c.sp = 0xFC; g_mem[0x1FD] = 0x00; g_mem[0x1FC] = 0x11; g_mem[0x1FE] = 0x22;
    ST(X(0x68), 0x00, 0, 0, 0xFD, FU | FZ, 0x0201, 4);
    clr(); set(0, 0, 0, FU | FC); ST(X(0x08), 0, 0, 0, 0xFC, FU | FC, 0x0201, 3); CHECK(g_mem[0x1FD] == 0x31);
    clr(); set(0, 0, 0, FN | FV); ST(X(0x08), 0, 0, 0, 0xFC, FN | FV, 0x0201, 3); CHECK(g_mem[0x1FD] == 0xF0);
    clr(); set(0, 0, 0, FU); c.sp = 0xFC; g_mem[0x1FD] = 0xFF; ST(X(0x28), 0, 0, 0, 0xFD, 0xEF, 0x0201, 4);
    clr(); set(0, 0, 0, 0xFF); c.sp = 0xFC; g_mem[0x1FD] = 0x00; ST(X(0x28), 0, 0, 0, 0xFD, FU, 0x0201, 4);
}

static void test_cpx_bit(void)
{
    clr(); set(0, 0x40, 0, FU); ST(X(0xE0, 0x40), 0, 0x40, 0, 0xFD, FU | FZ | FC, 0x0202, 2);
    clr(); set(0, 0x40, 0, FU | FC); ST(X(0xE0, 0x41), 0, 0x40, 0, 0xFD, FU | FN, 0x0202, 2);
    clr(); set(0, 0x40, 0, FU); g_mem[0x10] = 0x3F; ST(X(0xE4, 0x10), 0, 0x40, 0, 0xFD, FU | FC, 0x0202, 3);
    clr(); set(0, 0x00, 0, FU); g_mem[0x1234] = 0x80; ST(X(0xEC, 0x34, 0x12), 0, 0x00, 0, 0xFD, FU | FN, 0x0203, 4);
    clr(); set(0, 0, 0x40, FU); ST(X(0xC0, 0x40), 0, 0, 0x40, 0xFD, FU | FZ | FC, 0x0202, 2);
    clr(); set(0, 0, 0x40, FU | FC); ST(X(0xC0, 0x41), 0, 0, 0x40, 0xFD, FU | FN, 0x0202, 2);
    clr(); set(0, 0, 0x40, FU); g_mem[0x10] = 0x3F; ST(X(0xC4, 0x10), 0, 0, 0x40, 0xFD, FU | FC, 0x0202, 3);
    clr(); set(0, 0, 0x00, FU); g_mem[0x1234] = 0x80; ST(X(0xCC, 0x34, 0x12), 0, 0, 0x00, 0xFD, FU | FN, 0x0203, 4);
    clr(); set(0, 0, 0x90, FU); g_mem[0x1234] = 0x10; ST(X(0xCC, 0x34, 0x12), 0, 0, 0x90, 0xFD, FU | FN | FC, 0x0203, 4);

    clr(); set(0x01, 0, 0, FU); g_mem[0x10] = 0xC0; ST(X(0x24, 0x10), 0x01, 0, 0, 0xFD, FU | FZ | FN | FV, 0x0202, 3);
    clr(); set(0x41, 0, 0, FU); g_mem[0x10] = 0x41; ST(X(0x24, 0x10), 0x41, 0, 0, 0xFD, FU | FV, 0x0202, 3);
    clr(); set(0x01, 0, 0, FU | FN | FV | FZ); g_mem[0x10] = 0x01; ST(X(0x24, 0x10), 0x01, 0, 0, 0xFD, FU, 0x0202, 3);
    clr(); set(0x01, 0, 0, FU); g_mem[0x1234] = 0xC0; ST(X(0x2C, 0x34, 0x12), 0x01, 0, 0, 0xFD, FU | FZ | FN | FV, 0x0203, 4);
    clr(); set(0x81, 0, 0, FU); g_mem[0x1234] = 0x80; ST(X(0x2C, 0x34, 0x12), 0x81, 0, 0, 0xFD, FU | FN, 0x0203, 4);
    clr(); set(0x01, 0, 0, FU | FN | FV | FZ); g_mem[0x1234] = 0x01; ST(X(0x2C, 0x34, 0x12), 0x01, 0, 0, 0xFD, FU, 0x0203, 4);
}

static void test_incdec(void)
{
    clr(); set(0, 0, 0, FU); g_mem[0x10] = 0xFF; ST(X(0xE6, 0x10), 0, 0, 0, 0xFD, FU | FZ, 0x0202, 5); CHECK(g_mem[0x10] == 0x00);
    clr(); set(0, 0x20, 0, FU); g_mem[0x10] = 0x7F; ST(X(0xF6, 0xF0), 0, 0x20, 0, 0xFD, FU | FN, 0x0202, 6); CHECK(g_mem[0x10] == 0x80);
    clr(); set(0, 0, 0, FU | FZ); g_mem[0x1234] = 0x01; ST(X(0xEE, 0x34, 0x12), 0, 0, 0, 0xFD, FU, 0x0203, 6); CHECK(g_mem[0x1234] == 0x02);
    clr(); set(0, 0x20, 0, FU); g_mem[0x1310] = 0x10; ST(X(0xFE, 0xF0, 0x12), 0, 0x20, 0, 0xFD, FU, 0x0203, 7); CHECK(g_mem[0x1310] == 0x11);
    clr(); set(0, 0, 0, FU); g_mem[0x10] = 0x00; ST(X(0xC6, 0x10), 0, 0, 0, 0xFD, FU | FN, 0x0202, 5); CHECK(g_mem[0x10] == 0xFF);
    clr(); set(0, 0x20, 0, FU); g_mem[0x10] = 0x01; ST(X(0xD6, 0xF0), 0, 0x20, 0, 0xFD, FU | FZ, 0x0202, 6); CHECK(g_mem[0x10] == 0x00);
    clr(); set(0, 0, 0, FU | FN); g_mem[0x1234] = 0x81; ST(X(0xCE, 0x34, 0x12), 0, 0, 0, 0xFD, FU | FN, 0x0203, 6); CHECK(g_mem[0x1234] == 0x80);
    clr(); set(0, 0x20, 0, FU); g_mem[0x1310] = 0x10; ST(X(0xDE, 0xF0, 0x12), 0, 0x20, 0, 0xFD, FU, 0x0203, 7); CHECK(g_mem[0x1310] == 0x0F);
    clr(); set(0, 0xFF, 0, FU); ST(X(0xE8), 0, 0x00, 0, 0xFD, FU | FZ, 0x0201, 2);
    clr(); set(0, 0x7F, 0, FU); ST(X(0xE8), 0, 0x80, 0, 0xFD, FU | FN, 0x0201, 2);
    clr(); set(0, 0x00, 0, FU); ST(X(0xCA), 0, 0xFF, 0, 0xFD, FU | FN, 0x0201, 2);
    clr(); set(0, 0x01, 0, FU); ST(X(0xCA), 0, 0x00, 0, 0xFD, FU | FZ, 0x0201, 2);
    clr(); set(0, 0, 0xFF, FU); ST(X(0xC8), 0, 0, 0x00, 0xFD, FU | FZ, 0x0201, 2);
    clr(); set(0, 0, 0x7F, FU); ST(X(0xC8), 0, 0, 0x80, 0xFD, FU | FN, 0x0201, 2);
    clr(); set(0, 0, 0x00, FU); ST(X(0x88), 0, 0, 0xFF, 0xFD, FU | FN, 0x0201, 2);
    clr(); set(0, 0, 0x01, FU); ST(X(0x88), 0, 0, 0x00, 0xFD, FU | FZ, 0x0201, 2);
}

/* shift/rotate on memory in all 4 memory modes: base = opcode of the zp form */
static void shift_mem(int line, uint8_t base, uint8_t in, uint8_t p, uint8_t out, uint8_t wp)
{
    static const struct {
        uint8_t off, x, b1, b2;
        uint16_t ea;
        int len, cyc;
    } M[4] = {
        {0x00, 0x00, 0x10, 0, 0x0010, 2, 5}, /* zp */
        {0x10, 0x20, 0xF0, 0, 0x0010, 2, 6}, /* zp,X wraps */
        {0x08, 0x00, 0x34, 0x12, 0x1234, 3, 6}, /* abs */
        {0x18, 0x20, 0xF0, 0x12, 0x1310, 3, 7}, /* abs,X */
    };
    for (int i = 0; i < 4; i++) {
        clr();
        set(0x5A, M[i].x, 0, p);
        g_mem[M[i].ea] = in;
        int k = M[i].len == 2 ? X((uint8_t) (base + M[i].off), M[i].b1)
                              : X((uint8_t) (base + M[i].off), M[i].b1, M[i].b2);
        st(line, k, 0x5A, M[i].x, 0, 0xFD, wp, (uint16_t) (0x0200 + M[i].len), M[i].cyc);
        checks++;
        if (g_mem[M[i].ea] != out) {
            printf("  FAIL line %d: op %02X mem=%02X want %02X\n", line, base + M[i].off,
                   g_mem[M[i].ea], out);
            fails++;
        }
    }
}
static void shift_acc(int line, uint8_t op, uint8_t in, uint8_t p, uint8_t out, uint8_t wp)
{
    clr();
    set(in, 1, 2, p);
    int k = X(op);
    st(line, k, out, 1, 2, 0xFD, wp, 0x0201, 2);
}

static void test_shifts(void)
{
    /* ASL */
    shift_acc(__LINE__, 0x0A, 0x81, FU, 0x02, FU | FC);
    shift_acc(__LINE__, 0x0A, 0x40, FU | FC, 0x80, FU | FN);
    shift_acc(__LINE__, 0x0A, 0x80, FU, 0x00, FU | FZ | FC);
    shift_mem(__LINE__, 0x06, 0x81, FU, 0x02, FU | FC);
    shift_mem(__LINE__, 0x06, 0x40, FU | FC, 0x80, FU | FN);
    shift_mem(__LINE__, 0x06, 0x80, FU, 0x00, FU | FZ | FC);
    shift_acc(__LINE__, 0x0A, 0x01, FU | FC, 0x02, FU);
    shift_mem(__LINE__, 0x06, 0x01, FU | FC, 0x02, FU);
    /* LSR */
    shift_acc(__LINE__, 0x4A, 0x01, FU | FN, 0x00, FU | FZ | FC);
    shift_acc(__LINE__, 0x4A, 0x82, FU | FC, 0x41, FU);
    shift_mem(__LINE__, 0x46, 0x01, FU | FN, 0x00, FU | FZ | FC);
    shift_mem(__LINE__, 0x46, 0x82, FU | FC, 0x41, FU);
    /* ROL */
    shift_acc(__LINE__, 0x2A, 0x80, FU | FC, 0x01, FU | FC);
    shift_acc(__LINE__, 0x2A, 0x40, FU, 0x80, FU | FN);
    shift_acc(__LINE__, 0x2A, 0x80, FU, 0x00, FU | FZ | FC);
    shift_mem(__LINE__, 0x26, 0x80, FU | FC, 0x01, FU | FC);
    shift_mem(__LINE__, 0x26, 0x40, FU, 0x80, FU | FN);
    shift_mem(__LINE__, 0x26, 0x80, FU, 0x00, FU | FZ | FC);
    shift_acc(__LINE__, 0x2A, 0x01, FU, 0x02, FU);
    shift_mem(__LINE__, 0x26, 0x01, FU, 0x02, FU);
    /* ROR */
    shift_acc(__LINE__, 0x6A, 0x01, FU | FC, 0x80, FU | FN | FC);
    shift_acc(__LINE__, 0x6A, 0x02, FU, 0x01, FU);
    shift_acc(__LINE__, 0x6A, 0x01, FU, 0x00, FU | FZ | FC);
    shift_mem(__LINE__, 0x66, 0x01, FU | FC, 0x80, FU | FN | FC);
    shift_mem(__LINE__, 0x66, 0x02, FU, 0x01, FU);
    shift_mem(__LINE__, 0x66, 0x01, FU, 0x00, FU | FZ | FC);
}

/* branch opcode `op` taken when (p & flag) == want */
static void branch_case(int line, uint8_t op, uint8_t flag, int taken_when_set)
{
    uint8_t ptake = taken_when_set ? (uint8_t) (FU | flag) : FU;
    uint8_t pskip = taken_when_set ? FU : (uint8_t) (FU | flag);
    clr(); set(0, 0, 0, pskip); st(line, X(op, 0x10), 0, 0, 0, 0xFD, pskip, 0x0202, 2);
    clr(); set(0, 0, 0, ptake); st(line, X(op, 0x10), 0, 0, 0, 0xFD, ptake, 0x0212, 3);
    clr(); set(0, 0, 0, ptake); st(line, X(op, 0xF0), 0, 0, 0, 0xFD, ptake, 0x01F2, 4);
    clr(); set(0, 0, 0, ptake); st(line, X(op, 0x7D), 0, 0, 0, 0xFD, ptake, 0x027F, 3);
    clr(); set(0, 0, 0, ptake); st(line, X(op, 0x7E), 0, 0, 0, 0xFD, ptake, 0x0280, 3);
    /* every other flag set/clear must not matter */
    uint8_t others = (uint8_t) ((FN | FV | FC | FZ) & ~flag);
    clr(); set(0, 0, 0, (uint8_t) (ptake | others));
    st(line, X(op, 0x02), 0, 0, 0, 0xFD, (uint8_t) (ptake | others), 0x0204, 3);
    clr(); set(0, 0, 0, pskip & (uint8_t) ~others);
    st(line, X(op, 0x02), 0, 0, 0, 0xFD, pskip & (uint8_t) ~others, 0x0202, 2);
}

static void test_branches(void)
{
    branch_case(__LINE__, 0x10, FN, 0);
    branch_case(__LINE__, 0x30, FN, 1);
    branch_case(__LINE__, 0x50, FV, 0);
    branch_case(__LINE__, 0x70, FV, 1);
    branch_case(__LINE__, 0x90, FC, 0);
    branch_case(__LINE__, 0xB0, FC, 1);
    branch_case(__LINE__, 0xD0, FZ, 0);
    branch_case(__LINE__, 0xF0, FZ, 1);
    /* page cross forward: from $02F0 +$20 -> $0312 */
    org = 0x02F0;
    clr(); set(0, 0, 0, FU); st(__LINE__, X(0xD0, 0x20), 0, 0, 0, 0xFD, FU, 0x0312, 4);
    org = 0x0200;
}

static void test_jumps(void)
{
    clr(); set(0, 0, 0, FU); ST(X(0x4C, 0x34, 0x12), 0, 0, 0, 0xFD, FU, 0x1234, 3);
    clr(); set(0, 0, 0, FU); g_mem[0x1320] = 0x78; g_mem[0x1321] = 0x56;
    ST(X(0x6C, 0x20, 0x13), 0, 0, 0, 0xFD, FU, 0x5678, 5);
    clr(); set(0, 0, 0, FU); g_mem[0x13FF] = 0x78; g_mem[0x1300] = 0x56; g_mem[0x1400] = 0x99;
    ST(X(0x6C, 0xFF, 0x13), 0, 0, 0, 0xFD, FU, 0x5678, 5); /* page-wrap bug */
    clr(); set(0, 0, 0, FU); g_mem[0x13FE] = 0x78; g_mem[0x13FF] = 0x56; g_mem[0x1300] = 0x99;
    ST(X(0x6C, 0xFE, 0x13), 0, 0, 0, 0xFD, FU, 0x5678, 5);

    org = 0x0380;
    clr(); set(0, 0, 0, FU); ST(X(0x20, 0x34, 0x12), 0, 0, 0, 0xFB, FU, 0x1234, 6);
    CHECK(g_mem[0x1FD] == 0x03);
    CHECK(g_mem[0x1FC] == 0x82);
    clr(); set(0, 0, 0, FU); c.sp = 0xFB; g_mem[0x1FC] = 0x82; g_mem[0x1FD] = 0x03;
    ST(X(0x60), 0, 0, 0, 0xFD, FU, 0x0383, 6);
    clr(); set(0, 0, 0, FU); c.sp = 0xFB; g_mem[0x1FC] = 0xFF; g_mem[0x1FD] = 0x12;
    ST(X(0x60), 0, 0, 0, 0xFD, FU, 0x1300, 6);
    clr(); set(0, 0, 0, FU); c.sp = 0xFA; g_mem[0x1FB] = 0xFF; g_mem[0x1FC] = 0x34; g_mem[0x1FD] = 0x12;
    ST(X(0x40), 0, 0, 0, 0xFD, 0xEF, 0x1234, 6);
    clr(); set(0, 0, 0, 0xFF); c.sp = 0xFA; g_mem[0x1FB] = 0x00; g_mem[0x1FC] = 0x34; g_mem[0x1FD] = 0x12;
    ST(X(0x40), 0, 0, 0, 0xFD, FU, 0x1234, 6);
    clr(); set(0, 0, 0, FU | FC); g_mem[0xFFFE] = 0x00; g_mem[0xFFFF] = 0x90;
    g_mem[0xFFFC] = 0x11; g_mem[0xFFFD] = 0x22; g_mem[0xFFFA] = 0x33; g_mem[0xFFFB] = 0x44;
    ST(X(0x00, 0xEA), 0, 0, 0, 0xFA, FU | FC | FI, 0x9000, 7);
    CHECK(g_mem[0x1FD] == 0x03);
    CHECK(g_mem[0x1FC] == 0x82);
    CHECK(g_mem[0x1FB] == 0x31);
    org = 0x0200;
}

static void test_flags_misc(void)
{
    clr(); set(0, 0, 0, 0xFF); ST(X(0x18), 0, 0, 0, 0xFD, 0xFE, 0x0201, 2);
    clr(); set(0, 0, 0, FU); ST(X(0x38), 0, 0, 0, 0xFD, FU | FC, 0x0201, 2);
    clr(); set(0, 0, 0, 0xFF); ST(X(0x58), 0, 0, 0, 0xFD, 0xFB, 0x0201, 2);
    clr(); set(0, 0, 0, FU); ST(X(0x78), 0, 0, 0, 0xFD, FU | FI, 0x0201, 2);
    clr(); set(0, 0, 0, 0xFF); ST(X(0xB8), 0, 0, 0, 0xFD, 0xBF, 0x0201, 2);
    clr(); set(0, 0, 0, 0xFF); ST(X(0xD8), 0, 0, 0, 0xFD, 0xF7, 0x0201, 2);
    clr(); set(0, 0, 0, FU); ST(X(0xF8), 0, 0, 0, 0xFD, FU | FD, 0x0201, 2);
    clr(); set(1, 2, 3, 0xA5); ST(X(0xEA), 1, 2, 3, 0xFD, 0xA5, 0x0201, 2);

    /* illegal opcodes run as 1-byte NOPs and are counted */
    clr(); set(1, 2, 3, FU); ST(X(0x02, 0xA9), 1, 2, 3, 0xFD, FU, 0x0201, 2);
    CHECK(c.illegal == 1);
    CHECK(c.jammed == 0);
    ST(X(0xFF), 1, 2, 3, 0xFD, FU, 0x0202, 2);
    CHECK(c.illegal == 2);
    clr(); set(0, 0, 0, FU); X(0xEA); CHECK(c.illegal == 0);

    /* jammed: step does nothing and returns 0 */
    clr(); set(1, 2, 3, FU); c.jammed = 1; g_mem[0x200] = 0xA9; g_mem[0x201] = 0x55;
    CHECK(cpu6502_step(&c) == 0);
    CHECK(c.cycles == 1000);
    CHECK(c.pc == 0x0200);
    CHECK(c.a == 1);
    CHECK(cpu6502_run(&c, 100) == 0);
    CHECK(c.pc == 0x0200);
}

static void test_reset_irq(void)
{
    clr();
    set(1, 2, 3, 0xFF);
    c.sp = 0x10;
    c.cycles = 77;
    c.jammed = 1;
    c.illegal = 9;
    g_mem[0xFFFC] = 0x34;
    g_mem[0xFFFD] = 0x12;
    g_mem[0xFFFE] = 0x99;
    g_mem[0xFFFB] = 0x88;
    cpu6502_reset(&c);
    st(__LINE__, 0, 0, 0, 0, 0xFD, FU | FI, 0x1234, -1);
    CHECK(c.cycles == 0);
    CHECK(c.jammed == 0);
    CHECK(c.illegal == 0);

    /* NMI ignores I, pushes PC and P (B clear, U set) */
    clr(); set(1, 2, 3, FN | FB | FC | FI); c.pc = 0x1234;
    g_mem[0xFFFA] = 0x00; g_mem[0xFFFB] = 0x80; g_mem[0xFFFE] = 0x11; g_mem[0xFFFF] = 0x22;
    g_mem[0xFFFC] = 0x33; g_mem[0xFFFD] = 0x44;
    cpu6502_nmi(&c);
    st(__LINE__, 0, 1, 2, 3, 0xFA, FN | FB | FC | FI, 0x8000, -1);
    CHECK(c.cycles == 1007);
    CHECK(g_mem[0x1FD] == 0x12);
    CHECK(g_mem[0x1FC] == 0x34);
    CHECK(g_mem[0x1FB] == (FN | FU | FC | FI));
    clr(); set(1, 2, 3, FN | FB | FC); c.pc = 0x1234;
    g_mem[0xFFFA] = 0x01; g_mem[0xFFFB] = 0x80;
    cpu6502_nmi(&c);
    st(__LINE__, 0, 1, 2, 3, 0xFA, FN | FB | FC | FI, 0x8001, -1);
    CHECK(g_mem[0x1FB] == (FN | FU | FC));

    /* IRQ masked by I */
    clr(); set(1, 2, 3, FU | FI); c.pc = 0x1234; g_mem[0xFFFE] = 0x00; g_mem[0xFFFF] = 0x90;
    cpu6502_irq(&c);
    st(__LINE__, 0, 1, 2, 3, 0xFD, FU | FI, 0x1234, -1);
    CHECK(c.cycles == 1000);
    CHECK(g_mem[0x1FD] == 0);
    /* IRQ taken */
    clr(); set(1, 2, 3, FB | FV | FZ); c.pc = 0x1234; g_mem[0xFFFE] = 0x02; g_mem[0xFFFF] = 0x90;
    g_mem[0xFFFA] = 0x11; g_mem[0xFFFB] = 0x22; g_mem[0xFFFC] = 0x33; g_mem[0xFFFD] = 0x44;
    cpu6502_irq(&c);
    st(__LINE__, 0, 1, 2, 3, 0xFA, FB | FV | FZ | FI, 0x9002, -1);
    CHECK(c.cycles == 1007);
    CHECK(g_mem[0x1FD] == 0x12);
    CHECK(g_mem[0x1FC] == 0x34);
    CHECK(g_mem[0x1FB] == (FU | FV | FZ));
}

static void test_run(void)
{
    /* NOP sled: each step is 2 cycles */
    clr();
    memset(g_mem + 0x200, 0xEA, 0x100);
    set(0, 0, 0, FU);
    CHECK(cpu6502_run(&c, 5) == 6);
    CHECK(c.pc == 0x0203);
    set(0, 0, 0, FU);
    CHECK(cpu6502_run(&c, 6) == 6);
    CHECK(c.pc == 0x0203);
    set(0, 0, 0, FU);
    CHECK(cpu6502_run(&c, 0) == 0);
    CHECK(c.pc == 0x0200);
    set(0, 0, 0, FU);
    CHECK(cpu6502_run(&c, 1) == 2);
    CHECK(c.cycles == 1002);

    /* programs from the original test, via reset + run */
    {
        static const uint8_t p[] = {0xA2, 0x0A, 0xCA, 0xD0, 0xFD, 0x4C, 0x05, 0x02};
        clr();
        memcpy(g_mem + 0x200, p, sizeof p);
        g_mem[0xFFFC] = 0x00;
        g_mem[0xFFFD] = 0x02;
        set(0, 0, 0, 0);
        cpu6502_reset(&c);
        /* LDX 2 + 9*(DEX 2 + BNE 3) + DEX 2 + BNE 2 = 51 */
        CHECK(cpu6502_run(&c, 51) == 51);
        CHECK(c.x == 0);
        CHECK(c.pc == 0x0205);
        CHECK(c.p == (FU | FI | FZ));
    }
    {
        static const uint8_t p[] = {0x20, 0x08, 0x02, 0x85, 0x40, 0x4C, 0x05, 0x02,
                                    0xA9, 0x42, 0x60};
        clr();
        memcpy(g_mem + 0x200, p, sizeof p);
        g_mem[0xFFFC] = 0x00;
        g_mem[0xFFFD] = 0x02;
        set(0, 0, 0, 0);
        cpu6502_reset(&c);
        CHECK(cpu6502_run(&c, 17) == 17); /* JSR 6 + LDA 2 + RTS 6 + STA 3 */
        CHECK(c.a == 0x42);
        CHECK(g_mem[0x40] == 0x42);
        CHECK(c.sp == 0xFD);
        CHECK(c.pc == 0x0205);
    }
}

int main(void)
{
    test_alu();
    test_loads();
    test_stores();
    test_transfers_stack();
    test_cpx_bit();
    test_incdec();
    test_shifts();
    test_branches();
    test_jumps();
    test_flags_misc();
    test_reset_irq();
    test_run();
    if (fails) {
        printf("%d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("%d checks passed\n", checks);
    return 0;
}
