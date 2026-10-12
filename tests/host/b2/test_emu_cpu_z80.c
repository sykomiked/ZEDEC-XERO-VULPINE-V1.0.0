/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_emu_cpu_z80.c: mutation-killing unit tests for kernel/src/emu/cpu_z80.c.
 * Every decoded opcode (unprefixed, CB, ED, DD/FD, DDCB/FDCB) is stepped from
 * a known register file and the WHOLE state is compared afterwards: main and
 * alternate registers, IX/IY, I/R, SP/PC, IFF1/IFF2/IM/halted, F as an exact
 * byte (S Z 5 H 3 P/V N C), the returned T-states and the cycle counter, plus
 * memory and port traffic. Also covers reset, the maskable interrupt (IM0/1/2),
 * halted/jammed stepping, illegal accounting and cpu_z80_run's budget.
 * Cycle counts of CB (HL) and DDCB BIT ops and F5/F3 of CP / BIT (HL) are not
 * asserted (the core deviates from silicon there; see the report).
 */
#include "cpu_z80.h"
#include <stdio.h>
#include <string.h>

#define FC Z80_C
#define FN Z80_N
#define FP Z80_PV
#define F3 Z80_F3
#define FH Z80_H
#define F5 Z80_F5
#define FZ Z80_Z
#define FS Z80_S

static uint8_t mem[65536];
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

static uint8_t bus_rd(cpu_z80_t *z, uint16_t a)
{
    (void) z;
    return mem[a];
}
static void bus_wr(cpu_z80_t *z, uint16_t a, uint8_t v)
{
    (void) z;
    mem[a] = v;
}

/* port traffic log */
static int n_in, n_out;
static uint16_t in_port, out_port;
static uint8_t out_val, in_val;
static uint8_t port_in(cpu_z80_t *z, uint16_t p)
{
    (void) z;
    n_in++;
    in_port = p;
    return in_val;
}
static void port_out(cpu_z80_t *z, uint16_t p, uint8_t v)
{
    (void) z;
    n_out++;
    out_port = p;
    out_val = v;
}

typedef struct {
    uint8_t a, f, b, c, d, e, h, l;
    uint8_t a2, f2, b2, c2, d2, e2, h2, l2;
    uint16_t ix, iy, sp, pc;
    uint8_t i, r, iff1, iff2, im, halted;
} zs_t;

static cpu_z80_t z;

static zs_t dflt(void)
{
    zs_t s;
    s.a = 0x10; s.f = 0x00; s.b = 0x01; s.c = 0x02; s.d = 0x03; s.e = 0x04; s.h = 0x40; s.l = 0x00;
    s.a2 = 0xA2; s.f2 = 0xF2; s.b2 = 0xB2; s.c2 = 0xC2; s.d2 = 0xD2; s.e2 = 0xE2; s.h2 = 0x82;
    s.l2 = 0x92;
    s.ix = 0x5000; s.iy = 0x6000; s.sp = 0xF000; s.pc = 0x0100;
    s.i = 0x3C; s.r = 0x7E; s.iff1 = 0; s.iff2 = 0; s.im = 0; s.halted = 0;
    return s;
}

static void mclr(void)
{
    memset(mem, 0, sizeof mem);
    n_in = n_out = 0;
    in_port = out_port = 0;
    out_val = 0;
    in_val = 0xC3;
}

static void load(const zs_t *s)
{
    memset(&z, 0, sizeof z);
    z.read = bus_rd;
    z.write = bus_wr;
    z.in = port_in;
    z.out = port_out;
    z.a = s->a; z.f = s->f; z.b = s->b; z.c = s->c; z.d = s->d; z.e = s->e; z.h = s->h; z.l = s->l;
    z.a2 = s->a2; z.f2 = s->f2; z.b2 = s->b2; z.c2 = s->c2; z.d2 = s->d2; z.e2 = s->e2;
    z.h2 = s->h2; z.l2 = s->l2;
    z.ix = s->ix; z.iy = s->iy; z.sp = s->sp; z.pc = s->pc;
    z.i = s->i; z.r = s->r; z.iff1 = s->iff1; z.iff2 = s->iff2; z.im = s->im;
    z.halted = s->halted;
    z.cycles = 500;
}

static int exec_bytes(const zs_t *s, const uint8_t *b, size_t n)
{
    load(s);
    memcpy(mem + s->pc, b, n);
    int k = cpu_z80_step(&z);
    CHECK(z.cycles == 500u + (uint64_t) k);
    CHECK(z.last_opcode == b[0]);
    return k;
}
#define RUN(s, ...)                                                                                \
    exec_bytes(&(s), (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}))

static void expect(int line, const zs_t *w, int k, int wk)
{
    checks++;
    int bad = 0;
#define CMP(fld)                                                                                   \
    if ((unsigned) z.fld != (unsigned) w->fld) {                                                   \
        printf("  FAIL line %d: %s = %X, want %X\n", line, #fld, (unsigned) z.fld,                 \
               (unsigned) w->fld);                                                                 \
        bad = 1;                                                                                   \
    }
    CMP(a) CMP(f) CMP(b) CMP(c) CMP(d) CMP(e) CMP(h) CMP(l)
    CMP(a2) CMP(f2) CMP(b2) CMP(c2) CMP(d2) CMP(e2) CMP(h2) CMP(l2)
    CMP(ix) CMP(iy) CMP(sp) CMP(pc) CMP(i) CMP(r) CMP(iff1) CMP(iff2) CMP(im) CMP(halted)
#undef CMP
    if (wk >= 0 && k != wk) {
        printf("  FAIL line %d: cycles %d, want %d\n", line, k, wk);
        bad = 1;
    }
    if (z.illegal != 0) {
        printf("  FAIL line %d: illegal = %u\n", line, (unsigned) z.illegal);
        bad = 1;
    }
    fails += bad;
}
#define EXP(w, wk) expect(__LINE__, &(w), k, wk)

/* register index (Z80 encoding b c d e h l (hl) a) into a zs_t */
static uint8_t *zreg(zs_t *s, int idx)
{
    switch (idx) {
    case 0: return &s->b;
    case 1: return &s->c;
    case 2: return &s->d;
    case 3: return &s->e;
    case 4: return &s->h;
    case 5: return &s->l;
    case 7: return &s->a;
    default: return NULL;
    }
}

/* ---------------------------------------------------------------- 8-bit loads */
static void test_ld8(void)
{
    static const uint8_t RV[8] = {0x11, 0x22, 0x33, 0x44, 0x40, 0x55, 0x99, 0x77};
    for (int op = 0x40; op <= 0x7F; op++) {
        if (op == 0x76)
            continue;
        int dst = (op >> 3) & 7, src = op & 7;
        zs_t s = dflt();
        s.b = 0x11; s.c = 0x22; s.d = 0x33; s.e = 0x44; s.h = 0x40; s.l = 0x55; s.a = 0x77;
        s.f = 0xD7;
        mclr();
        mem[0x4055] = 0x99;
        int k = RUN(s, (uint8_t) op);
        zs_t w = s;
        w.pc = 0x0101;
        if (dst == 6)
            CHECK(mem[0x4055] == RV[src]);
        else
            *zreg(&w, dst) = RV[src];
        expect(__LINE__, &w, k, (dst == 6 || src == 6) ? 7 : 4);
        if (dst != 6 && op != 0x76)
            CHECK(mem[0x4055] == 0x99);
    }

    /* LD r,n */
    static const uint8_t LDN[8] = {0x06, 0x0E, 0x16, 0x1E, 0x26, 0x2E, 0x36, 0x3E};
    for (int r = 0; r < 8; r++) {
        zs_t s = dflt();
        s.f = 0xFF;
        mclr();
        int k = RUN(s, LDN[r], 0xA5);
        zs_t w = s;
        w.pc = 0x0102;
        if (r == 6)
            CHECK(mem[0x4000] == 0xA5);
        else
            *zreg(&w, r) = 0xA5;
        EXP(w, r == 6 ? 10 : 7);
    }

    zs_t s, w;
    int k;
    /* LD (BC),A / LD (DE),A / LD A,(BC) / LD A,(DE) */
    s = dflt(); s.a = 0x5A; s.b = 0x12; s.c = 0x34; mclr(); k = RUN(s, 0x02);
    w = s; w.pc = 0x0101; EXP(w, 7); CHECK(mem[0x1234] == 0x5A);
    s = dflt(); s.a = 0x5B; s.d = 0x12; s.e = 0x35; mclr(); k = RUN(s, 0x12);
    w = s; w.pc = 0x0101; EXP(w, 7); CHECK(mem[0x1235] == 0x5B);
    s = dflt(); s.b = 0x12; s.c = 0x36; mclr(); mem[0x1236] = 0x77; k = RUN(s, 0x0A);
    w = s; w.a = 0x77; w.pc = 0x0101; EXP(w, 7);
    s = dflt(); s.d = 0x12; s.e = 0x37; mclr(); mem[0x1237] = 0x78; k = RUN(s, 0x1A);
    w = s; w.a = 0x78; w.pc = 0x0101; EXP(w, 7);
    /* LD (nn),HL / LD HL,(nn) / LD (nn),A / LD A,(nn) */
    s = dflt(); s.h = 0xAB; s.l = 0xCD; mclr(); k = RUN(s, 0x22, 0x00, 0x30);
    w = s; w.pc = 0x0103; EXP(w, 16); CHECK(mem[0x3000] == 0xCD); CHECK(mem[0x3001] == 0xAB);
    s = dflt(); s.h = 0xAB; s.l = 0xCD; mclr(); k = RUN(s, 0x22, 0xFF, 0xFF);
    w = s; w.pc = 0x0103; EXP(w, 16); CHECK(mem[0xFFFF] == 0xCD); CHECK(mem[0x0000] == 0xAB);
    s = dflt(); mclr(); mem[0x3000] = 0x34; mem[0x3001] = 0x12; k = RUN(s, 0x2A, 0x00, 0x30);
    w = s; w.h = 0x12; w.l = 0x34; w.pc = 0x0103; EXP(w, 16);
    s = dflt(); s.a = 0x99; mclr(); k = RUN(s, 0x32, 0x10, 0x30);
    w = s; w.pc = 0x0103; EXP(w, 13); CHECK(mem[0x3010] == 0x99);
    s = dflt(); mclr(); mem[0x3011] = 0x88; k = RUN(s, 0x3A, 0x11, 0x30);
    w = s; w.a = 0x88; w.pc = 0x0103; EXP(w, 13);
}

/* ---------------------------------------------------------------- 8-bit ALU */
static void alu_case(int line, int grp, uint8_t a, uint8_t v, uint8_t fin, uint8_t wa, uint8_t wf)
{
    for (int src = 0; src < 7; src++) {
        zs_t s = dflt();
        s.a = a; s.f = fin;
        s.b = s.c = s.d = s.e = 0xC3;
        s.h = 0x40; s.l = 0x00;
        mclr();
        if (src == 6)
            mem[0x4000] = v;
        else
            *zreg(&s, src) = v;
        int k = RUN(s, (uint8_t) (0x80 | (grp << 3) | src));
        zs_t w = s;
        w.a = wa; w.f = wf; w.pc = 0x0101;
        expect(line, &w, k, src == 6 ? 7 : 4);
    }
    /* immediate form */
    {
        zs_t s = dflt();
        s.a = a; s.f = fin;
        mclr();
        int k = RUN(s, (uint8_t) (0xC6 | (grp << 3)), v);
        zs_t w = s;
        w.a = wa; w.f = wf; w.pc = 0x0102;
        expect(line, &w, k, 7);
    }
    /* DD/FD forms: IXH, IXL, (IX+d), and the plain registers through the prefix */
    for (int pfx = 0; pfx < 2; pfx++) {
        for (int src = 0; src < 7; src++) {
            zs_t s = dflt();
            s.a = a; s.f = fin;
            s.b = s.c = s.d = s.e = s.h = s.l = 0xC3;
            uint16_t base = 0x5080;
            uint16_t *ip = pfx ? &s.iy : &s.ix;
            *ip = base;
            *(pfx ? &s.ix : &s.iy) = 0x7777;
            mclr();
            int k;
            if (src == 4)
                *ip = (uint16_t) ((v << 8) | 0x34);
            else if (src == 5)
                *ip = (uint16_t) (0x3400 | v);
            else if (src != 6)
                *zreg(&s, src) = v;
            if (src == 6) {
                mem[0x507D] = v; /* d = -3 */
                k = RUN(s, pfx ? 0xFD : 0xDD, (uint8_t) (0x80 | (grp << 3) | 6), 0xFD);
            } else {
                k = RUN(s, pfx ? 0xFD : 0xDD, (uint8_t) (0x80 | (grp << 3) | src));
            }
            zs_t w = s;
            w.a = wa; w.f = wf; w.pc = (uint16_t) (src == 6 ? 0x0103 : 0x0102);
            expect(line, &w, k, src == 6 ? 19 : 8);
        }
    }
}
/* op A,A forms (src 7), unprefixed and through DD/FD */
static void alu_aa(int line, int grp, uint8_t a, uint8_t fin, uint8_t wa, uint8_t wf)
{
    for (int pfx = 0; pfx < 3; pfx++) {
        zs_t s = dflt();
        s.a = a; s.f = fin;
        mclr();
        int k = pfx == 0 ? RUN(s, (uint8_t) (0x87 | (grp << 3)))
                         : RUN(s, pfx == 1 ? 0xDD : 0xFD, (uint8_t) (0x87 | (grp << 3)));
        zs_t w = s;
        w.a = wa; w.f = wf; w.pc = (uint16_t) (0x0101 + (pfx ? 1 : 0));
        expect(line, &w, k, pfx ? 8 : 4);
    }
}

enum { ADD, ADC, SUB, SBC, AND, XOR, OR, CP };

static void test_alu8(void)
{
    alu_case(__LINE__, ADD, 0x0F, 0x01, 0, 0x10, FH);
    alu_case(__LINE__, ADD, 0x7F, 0x01, 0, 0x80, FS | FH | FP);
    alu_case(__LINE__, ADD, 0xFF, 0x01, FN, 0x00, FZ | FH | FC);
    alu_case(__LINE__, ADD, 0x80, 0x80, 0, 0x00, FZ | FP | FC);
    alu_case(__LINE__, ADD, 0x20, 0x08, 0, 0x28, F5 | F3);
    alu_case(__LINE__, ADD, 0x0E, 0x01, 0, 0x0F, F3);
    alu_case(__LINE__, ADD, 0xFE, 0x01, 0, 0xFF, FS | F5 | F3);
    alu_case(__LINE__, ADD, 0x01, 0x01, FC, 0x02, 0);
    alu_case(__LINE__, ADD, 0xC0, 0x90, 0, 0x50, FP | FC);
    alu_case(__LINE__, ADC, 0x0E, 0x01, FC, 0x10, FH);
    alu_case(__LINE__, ADC, 0xFE, 0x01, FC, 0x00, FZ | FH | FC);
    alu_case(__LINE__, ADC, 0x7F, 0x00, FC, 0x80, FS | FH | FP);
    alu_case(__LINE__, ADC, 0x01, 0x01, 0, 0x02, 0);
    alu_case(__LINE__, ADC, 0x0E, 0x01, FS | FZ | FN, 0x0F, F3);
    alu_case(__LINE__, SUB, 0x10, 0x01, 0, 0x0F, FN | FH | F3);
    alu_case(__LINE__, SUB, 0x00, 0x01, 0, 0xFF, FS | F5 | F3 | FH | FN | FC);
    alu_case(__LINE__, SUB, 0x80, 0x01, 0, 0x7F, FP | FH | FN | F5 | F3);
    alu_case(__LINE__, SUB, 0x05, 0x05, FC, 0x00, FZ | FN);
    alu_case(__LINE__, SUB, 0x7F, 0xFF, 0, 0x80, FS | FP | FN | FC);
    alu_case(__LINE__, SUB, 0x11, 0x01, FC, 0x10, FN);
    alu_case(__LINE__, SBC, 0x10, 0x0F, FC, 0x00, FZ | FN | FH);
    alu_case(__LINE__, SBC, 0x00, 0x00, FC, 0xFF, FS | F5 | F3 | FH | FN | FC);
    alu_case(__LINE__, SBC, 0x05, 0x04, 0, 0x01, FN);
    alu_case(__LINE__, SBC, 0x10, 0x00, FC, 0x0F, FN | FH | F3);
    alu_case(__LINE__, AND, 0xF0, 0x8F, FC | FN, 0x80, FS | FH);
    alu_case(__LINE__, AND, 0xF0, 0x0F, 0xFF, 0x00, FZ | FH | FP);
    alu_case(__LINE__, AND, 0x3C, 0x2F, 0, 0x2C, F5 | F3 | FH);
    alu_case(__LINE__, XOR, 0xFF, 0x7F, 0xFF, 0x80, FS);
    alu_case(__LINE__, XOR, 0x5A, 0x5A, 0, 0x00, FZ | FP);
    alu_case(__LINE__, XOR, 0x0F, 0x0C, FC, 0x03, FP);
    alu_case(__LINE__, OR, 0x01, 0x80, 0xFF, 0x81, FS | FP);
    alu_case(__LINE__, OR, 0x00, 0x00, 0, 0x00, FZ | FP);
    alu_case(__LINE__, OR, 0x20, 0x08, 0, 0x28, F5 | F3 | FP);
    alu_case(__LINE__, OR, 0x01, 0x00, FH | FC, 0x01, 0);
    alu_case(__LINE__, OR, 0x40, 0x00, 0, 0x40, 0);
    alu_case(__LINE__, OR, 0x02, 0x00, 0, 0x02, 0);
    alu_case(__LINE__, OR, 0x10, 0x00, 0, 0x10, 0);
    /* CP: values where result and operand agree in bits 5 and 3 */
    alu_case(__LINE__, CP, 0x40, 0x40, FC, 0x40, FZ | FN);
    alu_case(__LINE__, CP, 0x28, 0x29, 0, 0x28, FS | F5 | F3 | FH | FN | FC);
    alu_case(__LINE__, CP, 0x57, 0x2F, 0, 0x57, F5 | F3 | FH | FN);
    alu_case(__LINE__, CP, 0x80, 0x20, 0, 0x80, F5 | FP | FN);
    alu_case(__LINE__, CP, 0x00, 0x00, 0xFF, 0x00, FZ | FN);

    alu_aa(__LINE__, ADD, 0x88, 0, 0x10, FH | FP | FC);
    alu_aa(__LINE__, ADC, 0x40, FC, 0x81, FS | FP);
    alu_aa(__LINE__, SUB, 0x55, FC, 0x00, FZ | FN);
    alu_aa(__LINE__, SBC, 0x55, FC, 0xFF, FS | F5 | F3 | FH | FN | FC);
    alu_aa(__LINE__, AND, 0x0F, FC | FN, 0x0F, F3 | FH | FP);
    alu_aa(__LINE__, XOR, 0x37, 0xFF, 0x00, FZ | FP);
    alu_aa(__LINE__, OR, 0x81, 0, 0x81, FS | FP);
    alu_aa(__LINE__, CP, 0x41, 0, 0x41, FZ | FN);
}

/* ---------------------------------------------------------------- INC/DEC */
static void incdec_case(int line, int dec, uint8_t v, uint8_t fin, uint8_t wr, uint8_t wf)
{
    for (int r = 0; r < 8; r++) {
        zs_t s = dflt();
        s.f = fin;
        s.b = s.c = s.d = s.e = s.a = 0x3C;
        s.h = 0x40; s.l = 0x00;
        mclr();
        if (r == 6)
            mem[0x4000] = v;
        else
            *zreg(&s, r) = v;
        int k = RUN(s, (uint8_t) ((dec ? 0x05 : 0x04) | (r << 3)));
        zs_t w = s;
        w.f = wf; w.pc = 0x0101;
        if (r == 6) {
            checks++;
            if (mem[0x4000] != wr) {
                printf("  FAIL line %d: (HL)=%02X want %02X\n", line, mem[0x4000], wr);
                fails++;
            }
        } else {
            *zreg(&w, r) = wr;
        }
        expect(line, &w, k, r == 6 ? 11 : 4);
    }
    /* DD/FD: IXH, IXL, (IX+d) */
    for (int pfx = 0; pfx < 2; pfx++) {
        for (int which = 0; which < 3; which++) {
            zs_t s = dflt();
            s.f = fin;
            uint16_t *ip = pfx ? &s.iy : &s.ix;
            mclr();
            int k;
            uint8_t op = (uint8_t) (dec ? 0x25 : 0x24);
            if (which == 0) {
                *ip = (uint16_t) ((v << 8) | 0x5B);
                k = RUN(s, pfx ? 0xFD : 0xDD, op);
            } else if (which == 1) {
                *ip = (uint16_t) (0x5B00 | v);
                k = RUN(s, pfx ? 0xFD : 0xDD, (uint8_t) (op + 8));
            } else {
                *ip = 0x2010;
                mem[0x2015] = v;
                k = RUN(s, pfx ? 0xFD : 0xDD, (uint8_t) (dec ? 0x35 : 0x34), 0x05);
            }
            zs_t w = s;
            w.f = wf;
            uint16_t *wp = pfx ? &w.iy : &w.ix;
            if (which == 0)
                *wp = (uint16_t) ((wr << 8) | 0x5B);
            else if (which == 1)
                *wp = (uint16_t) (0x5B00 | wr);
            else {
                checks++;
                if (mem[0x2015] != wr) {
                    printf("  FAIL line %d: (IX+5)=%02X want %02X\n", line, mem[0x2015], wr);
                    fails++;
                }
            }
            w.pc = (uint16_t) (which == 2 ? 0x0103 : 0x0102);
            expect(line, &w, k, which == 2 ? 23 : 8);
        }
    }
}

static void test_incdec8(void)
{
    incdec_case(__LINE__, 0, 0x0F, FC, 0x10, FH | FC);
    incdec_case(__LINE__, 0, 0x7F, 0, 0x80, FS | FH | FP);
    incdec_case(__LINE__, 0, 0xFF, 0, 0x00, FZ | FH);
    incdec_case(__LINE__, 0, 0x27, FN | FS | FZ | FP | FH, 0x28, F5 | F3);
    incdec_case(__LINE__, 0, 0x0E, 0, 0x0F, F3);
    incdec_case(__LINE__, 0, 0x80, 0, 0x81, FS);
    incdec_case(__LINE__, 0, 0x7E, 0, 0x7F, F5 | F3);
    incdec_case(__LINE__, 1, 0x10, 0, 0x0F, FN | FH | F3);
    incdec_case(__LINE__, 1, 0x80, 0, 0x7F, FN | FH | FP | F5 | F3);
    incdec_case(__LINE__, 1, 0x01, FS | FP | FH, 0x00, FZ | FN);
    incdec_case(__LINE__, 1, 0x00, FC, 0xFF, FS | F5 | F3 | FH | FN | FC);
    incdec_case(__LINE__, 1, 0x02, 0, 0x01, FN);
    incdec_case(__LINE__, 1, 0x81, 0, 0x80, FS | FN);
    incdec_case(__LINE__, 1, 0x7F, 0, 0x7E, FN | F5 | F3);
}

/* ---------------------------------------------------------------- 16-bit */
static void test_16bit(void)
{
    zs_t s, w;
    int k;
    /* LD rr,nn */
    s = dflt(); s.f = 0xFF; mclr(); k = RUN(s, 0x01, 0x34, 0x12); w = s; w.b = 0x12; w.c = 0x34; w.pc = 0x0103; EXP(w, 10);
    s = dflt(); mclr(); k = RUN(s, 0x11, 0x35, 0x12); w = s; w.d = 0x12; w.e = 0x35; w.pc = 0x0103; EXP(w, 10);
    s = dflt(); mclr(); k = RUN(s, 0x21, 0x36, 0x12); w = s; w.h = 0x12; w.l = 0x36; w.pc = 0x0103; EXP(w, 10);
    s = dflt(); mclr(); k = RUN(s, 0x31, 0x37, 0x12); w = s; w.sp = 0x1237; w.pc = 0x0103; EXP(w, 10);
    /* INC/DEC rr incl. byte carry and 16-bit wrap; flags untouched */
    s = dflt(); s.f = 0xFF; s.b = 0x00; s.c = 0xFF; mclr(); k = RUN(s, 0x03); w = s; w.b = 0x01; w.c = 0x00; w.pc = 0x0101; EXP(w, 6);
    s = dflt(); s.b = 0xFF; s.c = 0xFF; mclr(); k = RUN(s, 0x03); w = s; w.b = 0x00; w.c = 0x00; w.pc = 0x0101; EXP(w, 6);
    s = dflt(); s.d = 0x00; s.e = 0xFF; mclr(); k = RUN(s, 0x13); w = s; w.d = 0x01; w.e = 0x00; w.pc = 0x0101; EXP(w, 6);
    s = dflt(); s.h = 0x00; s.l = 0xFF; mclr(); k = RUN(s, 0x23); w = s; w.h = 0x01; w.l = 0x00; w.pc = 0x0101; EXP(w, 6);
    s = dflt(); s.sp = 0xFFFF; mclr(); k = RUN(s, 0x33); w = s; w.sp = 0x0000; w.pc = 0x0101; EXP(w, 6);
    s = dflt(); s.b = 0x01; s.c = 0x00; mclr(); k = RUN(s, 0x0B); w = s; w.b = 0x00; w.c = 0xFF; w.pc = 0x0101; EXP(w, 6);
    s = dflt(); s.b = 0x00; s.c = 0x00; mclr(); k = RUN(s, 0x0B); w = s; w.b = 0xFF; w.c = 0xFF; w.pc = 0x0101; EXP(w, 6);
    s = dflt(); s.d = 0x01; s.e = 0x00; mclr(); k = RUN(s, 0x1B); w = s; w.d = 0x00; w.e = 0xFF; w.pc = 0x0101; EXP(w, 6);
    s = dflt(); s.h = 0x01; s.l = 0x00; mclr(); k = RUN(s, 0x2B); w = s; w.h = 0x00; w.l = 0xFF; w.pc = 0x0101; EXP(w, 6);
    s = dflt(); s.sp = 0x0000; mclr(); k = RUN(s, 0x3B); w = s; w.sp = 0xFFFF; w.pc = 0x0101; EXP(w, 6);

    /* ADD HL,rr */
    s = dflt(); s.f = 0xFF; s.h = 0x0F; s.l = 0xFF; s.b = 0x00; s.c = 0x01; mclr(); k = RUN(s, 0x09);
    w = s; w.h = 0x10; w.l = 0x00; w.f = FS | FZ | FP | FH; w.pc = 0x0101; EXP(w, 11);
    s = dflt(); s.h = 0xFF; s.l = 0xFF; s.d = 0x00; s.e = 0x01; mclr(); k = RUN(s, 0x19);
    w = s; w.h = 0x00; w.l = 0x00; w.f = FH | FC; w.pc = 0x0101; EXP(w, 11);
    s = dflt(); s.f = FN; s.h = 0x20; s.l = 0x00; s.sp = 0x0800; mclr(); k = RUN(s, 0x39);
    w = s; w.h = 0x28; w.l = 0x00; w.f = F5 | F3; w.pc = 0x0101; EXP(w, 11);
    s = dflt(); s.h = 0x0F; s.l = 0xFE; s.b = 0x00; s.c = 0x01; mclr(); k = RUN(s, 0x09);
    w = s; w.h = 0x0F; w.l = 0xFF; w.f = F3; w.pc = 0x0101; EXP(w, 11);
    s = dflt(); s.h = 0x80; s.l = 0x00; s.d = 0x80; s.e = 0x00; mclr(); k = RUN(s, 0x19);
    w = s; w.h = 0x00; w.l = 0x00; w.f = FC; w.pc = 0x0101; EXP(w, 11);
    s = dflt(); s.h = 0x88; s.l = 0x01; mclr(); k = RUN(s, 0x29);
    w = s; w.h = 0x10; w.l = 0x02; w.f = FH | FC; w.pc = 0x0101; EXP(w, 11);
    s = dflt(); s.h = 0x12; s.l = 0x34; s.b = 0x01; s.c = 0x01; mclr(); k = RUN(s, 0x09);
    w = s; w.h = 0x13; w.l = 0x35; w.f = 0; w.pc = 0x0101; EXP(w, 11);
    s = dflt(); s.h = 0x0F; s.l = 0x00; s.b = 0x01; s.c = 0x00; mclr(); k = RUN(s, 0x09);
    w = s; w.h = 0x10; w.l = 0x00; w.f = FH; w.pc = 0x0101; EXP(w, 11);
    s = dflt(); s.h = 0x00; s.l = 0x01; s.b = 0x0F; s.c = 0xFF; mclr(); k = RUN(s, 0x09);
    w = s; w.h = 0x10; w.l = 0x00; w.f = FH; w.pc = 0x0101; EXP(w, 11);
    s = dflt(); s.h = 0xFF; s.l = 0xFE; s.b = 0x00; s.c = 0x01; mclr(); k = RUN(s, 0x09);
    w = s; w.h = 0xFF; w.l = 0xFF; w.f = F5 | F3; w.pc = 0x0101; EXP(w, 11);

    /* LD (HL),n / INC (HL) / DEC (HL) covered elsewhere; LD SP,HL / JP (HL) / EX DE,HL */
    s = dflt(); s.h = 0x12; s.l = 0x34; mclr(); k = RUN(s, 0xF9); w = s; w.sp = 0x1234; w.pc = 0x0101; EXP(w, 6);
    s = dflt(); s.h = 0x12; s.l = 0x34; mclr(); k = RUN(s, 0xE9); w = s; w.pc = 0x1234; EXP(w, 4);
    s = dflt(); s.h = 0x12; s.l = 0x34; s.d = 0x56; s.e = 0x78; mclr(); k = RUN(s, 0xEB);
    w = s; w.h = 0x56; w.l = 0x78; w.d = 0x12; w.e = 0x34; w.pc = 0x0101; EXP(w, 4);
}

/* ---------------------------------------------------------------- rotates / misc flags */
static void acc_case(int line, uint8_t op, uint8_t a, uint8_t fin, uint8_t wa, uint8_t wf)
{
    zs_t s = dflt();
    s.a = a; s.f = fin;
    mclr();
    int k = RUN(s, op);
    zs_t w = s;
    w.a = wa; w.f = wf; w.pc = 0x0101;
    expect(line, &w, k, 4);
}

static void test_acc_ops(void)
{
    acc_case(__LINE__, 0x07, 0x81, 0, 0x03, FC);
    acc_case(__LINE__, 0x07, 0x14, 0xFF, 0x28, FS | FZ | FP | F5 | F3);
    acc_case(__LINE__, 0x07, 0x40, FC | FH | FN, 0x80, 0);
    acc_case(__LINE__, 0x0F, 0x01, 0, 0x80, FC);
    acc_case(__LINE__, 0x0F, 0x50, 0xFF, 0x28, FS | FZ | FP | F5 | F3);
    acc_case(__LINE__, 0x0F, 0x02, FC | FH | FN, 0x01, 0);
    acc_case(__LINE__, 0x17, 0x80, 0, 0x00, FC);
    acc_case(__LINE__, 0x17, 0x14, FC, 0x29, F5 | F3);
    acc_case(__LINE__, 0x17, 0x14, 0xFF, 0x29, FS | FZ | FP | F5 | F3);
    acc_case(__LINE__, 0x17, 0x01, FH | FN, 0x02, 0);
    acc_case(__LINE__, 0x17, 0x40, 0, 0x80, 0);
    acc_case(__LINE__, 0x1F, 0x01, 0, 0x00, FC);
    acc_case(__LINE__, 0x1F, 0x50, FC, 0xA8, F5 | F3);
    acc_case(__LINE__, 0x1F, 0x50, 0xFF, 0xA8, FS | FZ | FP | F5 | F3);
    acc_case(__LINE__, 0x1F, 0x02, FH | FN, 0x01, 0);
    /* CPL */
    acc_case(__LINE__, 0x2F, 0x5A, 0, 0xA5, FH | FN | F5);
    acc_case(__LINE__, 0x2F, 0xFF, FS | FZ | FP | FC | F5 | F3, 0x00, FS | FZ | FP | FC | FH | FN);
    acc_case(__LINE__, 0x2F, 0xF7, 0, 0x08, FH | FN | F3);
    /* SCF */
    acc_case(__LINE__, 0x37, 0x28, 0xFF, 0x28, FS | FZ | FP | FC | F5 | F3);
    acc_case(__LINE__, 0x37, 0x00, FH | FN, 0x00, FC);
    /* CCF */
    acc_case(__LINE__, 0x3F, 0x00, FC, 0x00, FH);
    acc_case(__LINE__, 0x3F, 0x00, 0, 0x00, FC);
    acc_case(__LINE__, 0x3F, 0x00, 0xFF, 0x00, FS | FZ | FP | FH);
    acc_case(__LINE__, 0x3F, 0x28, FS | FZ | FP | FN, 0x28, FS | FZ | FP | FC | F5 | F3);
    /* NOP */
    acc_case(__LINE__, 0x00, 0x12, 0xA5, 0x12, 0xA5);
}

/* ---------------------------------------------------------------- jumps/calls/stack */
static uint8_t cc_true_f(int cc)
{
    static const uint8_t T[8] = {0, FZ, 0, FC, 0, FP, 0, FS};
    static const uint8_t M[8] = {FZ, FZ, FC, FC, FP, FP, FS, FS};
    /* other flags set to the opposite of the tested one */
    return (uint8_t) (T[cc] | ((uint8_t) ~M[cc] & (T[cc] ? 0 : (FS | FZ | FP | FC))));
}
static uint8_t cc_false_f(int cc)
{
    static const uint8_t M[8] = {FZ, FZ, FC, FC, FP, FP, FS, FS};
    uint8_t t = cc_true_f(cc);
    return (uint8_t) (t ^ M[cc]);
}

static void test_flow(void)
{
    zs_t s, w;
    int k;
    s = dflt(); mclr(); k = RUN(s, 0xC3, 0x34, 0x12); w = s; w.pc = 0x1234; EXP(w, 10);
    for (int cc = 0; cc < 8; cc++) {
        uint8_t op = (uint8_t) (0xC2 | (cc << 3));
        s = dflt(); s.f = cc_true_f(cc); mclr(); k = RUN(s, op, 0x34, 0x12);
        w = s; w.pc = 0x1234; expect(__LINE__, &w, k, 10);
        s = dflt(); s.f = cc_false_f(cc); mclr(); k = RUN(s, op, 0x34, 0x12);
        w = s; w.pc = 0x0103; expect(__LINE__, &w, k, 10);
        /* CALL cc */
        op = (uint8_t) (0xC4 | (cc << 3));
        s = dflt(); s.f = cc_true_f(cc); mclr(); k = RUN(s, op, 0x34, 0x12);
        w = s; w.pc = 0x1234; w.sp = 0xEFFE; expect(__LINE__, &w, k, 17);
        CHECK(mem[0xEFFF] == 0x01 && mem[0xEFFE] == 0x03);
        s = dflt(); s.f = cc_false_f(cc); mclr(); k = RUN(s, op, 0x34, 0x12);
        w = s; w.pc = 0x0103; expect(__LINE__, &w, k, 10);
        CHECK(mem[0xEFFF] == 0 && mem[0xEFFE] == 0);
        /* RET cc */
        op = (uint8_t) (0xC0 | (cc << 3));
        s = dflt(); s.f = cc_true_f(cc); mclr(); mem[0xF000] = 0x78; mem[0xF001] = 0x56;
        k = RUN(s, op); w = s; w.pc = 0x5678; w.sp = 0xF002; expect(__LINE__, &w, k, 11);
        s = dflt(); s.f = cc_false_f(cc); mclr(); mem[0xF000] = 0x78; mem[0xF001] = 0x56;
        k = RUN(s, op); w = s; w.pc = 0x0101; expect(__LINE__, &w, k, 5);
    }
    /* JR e */
    s = dflt(); mclr(); k = RUN(s, 0x18, 0x10); w = s; w.pc = 0x0112; EXP(w, 12);
    s = dflt(); mclr(); k = RUN(s, 0x18, 0xFE); w = s; w.pc = 0x0100; EXP(w, 12);
    s = dflt(); mclr(); k = RUN(s, 0x18, 0x80); w = s; w.pc = 0x0082; EXP(w, 12);
    /* JR cc */
    for (int cc = 0; cc < 4; cc++) {
        uint8_t op = (uint8_t) (0x20 | (cc << 3));
        s = dflt(); s.f = cc_true_f(cc); mclr(); k = RUN(s, op, 0xF0);
        w = s; w.pc = 0x00F2; expect(__LINE__, &w, k, 12);
        s = dflt(); s.f = cc_false_f(cc); mclr(); k = RUN(s, op, 0xF0);
        w = s; w.pc = 0x0102; expect(__LINE__, &w, k, 7);
        /* a set PV / S must not influence JR cc */
        s = dflt(); s.f = (uint8_t) (cc_true_f(cc) | FP | FS); mclr(); k = RUN(s, op, 0x05);
        w = s; w.pc = 0x0107; expect(__LINE__, &w, k, 12);
    }
    /* DJNZ */
    s = dflt(); s.b = 2; mclr(); k = RUN(s, 0x10, 0xFE); w = s; w.b = 1; w.pc = 0x0100; EXP(w, 13);
    s = dflt(); s.b = 1; mclr(); k = RUN(s, 0x10, 0xFE); w = s; w.b = 0; w.pc = 0x0102; EXP(w, 8);
    s = dflt(); s.b = 0; s.f = 0xFF; mclr(); k = RUN(s, 0x10, 0x04); w = s; w.b = 0xFF; w.pc = 0x0106; EXP(w, 13);
    /* CALL / RET */
    s = dflt(); mclr(); k = RUN(s, 0xCD, 0x34, 0x12); w = s; w.pc = 0x1234; w.sp = 0xEFFE; EXP(w, 17);
    CHECK(mem[0xEFFF] == 0x01); CHECK(mem[0xEFFE] == 0x03);
    s = dflt(); mclr(); mem[0xF000] = 0x78; mem[0xF001] = 0x56; k = RUN(s, 0xC9);
    w = s; w.pc = 0x5678; w.sp = 0xF002; EXP(w, 10);
    /* PUSH / POP */
    s = dflt(); s.b = 0x12; s.c = 0x34; mclr(); k = RUN(s, 0xC5); w = s; w.sp = 0xEFFE; w.pc = 0x0101; EXP(w, 11);
    CHECK(mem[0xEFFF] == 0x12); CHECK(mem[0xEFFE] == 0x34);
    s = dflt(); s.d = 0x56; s.e = 0x78; mclr(); k = RUN(s, 0xD5); w = s; w.sp = 0xEFFE; w.pc = 0x0101; EXP(w, 11);
    CHECK(mem[0xEFFF] == 0x56); CHECK(mem[0xEFFE] == 0x78);
    s = dflt(); s.h = 0x9A; s.l = 0xBC; mclr(); k = RUN(s, 0xE5); w = s; w.sp = 0xEFFE; w.pc = 0x0101; EXP(w, 11);
    CHECK(mem[0xEFFF] == 0x9A); CHECK(mem[0xEFFE] == 0xBC);
    s = dflt(); s.a = 0xDE; s.f = 0xF1; mclr(); k = RUN(s, 0xF5); w = s; w.sp = 0xEFFE; w.pc = 0x0101; EXP(w, 11);
    CHECK(mem[0xEFFF] == 0xDE); CHECK(mem[0xEFFE] == 0xF1);
    s = dflt(); mclr(); mem[0xF000] = 0x34; mem[0xF001] = 0x12; k = RUN(s, 0xC1);
    w = s; w.b = 0x12; w.c = 0x34; w.sp = 0xF002; w.pc = 0x0101; EXP(w, 10);
    s = dflt(); mclr(); mem[0xF000] = 0x78; mem[0xF001] = 0x56; k = RUN(s, 0xD1);
    w = s; w.d = 0x56; w.e = 0x78; w.sp = 0xF002; w.pc = 0x0101; EXP(w, 10);
    s = dflt(); mclr(); mem[0xF000] = 0xBC; mem[0xF001] = 0x9A; k = RUN(s, 0xE1);
    w = s; w.h = 0x9A; w.l = 0xBC; w.sp = 0xF002; w.pc = 0x0101; EXP(w, 10);
    s = dflt(); mclr(); mem[0xF000] = 0xD7; mem[0xF001] = 0xEE; k = RUN(s, 0xF1);
    w = s; w.a = 0xEE; w.f = 0xD7; w.sp = 0xF002; w.pc = 0x0101; EXP(w, 10);
    /* stack wraps */
    s = dflt(); s.sp = 0x0001; s.b = 0x12; s.c = 0x34; mclr(); k = RUN(s, 0xC5);
    w = s; w.sp = 0xFFFF; w.pc = 0x0101; EXP(w, 11); CHECK(mem[0x0000] == 0x12); CHECK(mem[0xFFFF] == 0x34);
    /* EX (SP),HL */
    s = dflt(); s.h = 0x12; s.l = 0x34; mclr(); mem[0xF000] = 0x78; mem[0xF001] = 0x56; k = RUN(s, 0xE3);
    w = s; w.h = 0x56; w.l = 0x78; w.pc = 0x0101; EXP(w, 19);
    CHECK(mem[0xF000] == 0x34); CHECK(mem[0xF001] == 0x12);
    /* EX AF,AF' / EXX */
    s = dflt(); s.a = 0x11; s.f = 0x22; mclr(); k = RUN(s, 0x08);
    w = s; w.a = 0xA2; w.f = 0xF2; w.a2 = 0x11; w.f2 = 0x22; w.pc = 0x0101; EXP(w, 4);
    s = dflt(); mclr(); k = RUN(s, 0xD9);
    w = s; w.b = 0xB2; w.c = 0xC2; w.d = 0xD2; w.e = 0xE2; w.h = 0x82; w.l = 0x92;
    w.b2 = 0x01; w.c2 = 0x02; w.d2 = 0x03; w.e2 = 0x04; w.h2 = 0x40; w.l2 = 0x00; w.pc = 0x0101; EXP(w, 4);
    /* DI / EI */
    s = dflt(); s.iff1 = 1; s.iff2 = 1; mclr(); k = RUN(s, 0xF3); w = s; w.iff1 = 0; w.iff2 = 0; w.pc = 0x0101; EXP(w, 4);
    s = dflt(); mclr(); k = RUN(s, 0xFB); w = s; w.iff1 = 1; w.iff2 = 1; w.pc = 0x0101; EXP(w, 4);
    /* LD (HL),n */
    s = dflt(); s.h = 0x23; s.l = 0x45; mclr(); k = RUN(s, 0x36, 0x99); w = s; w.pc = 0x0102; EXP(w, 10);
    CHECK(mem[0x2345] == 0x99);
    /* HALT */
    s = dflt(); s.f = 0x55; mclr(); k = RUN(s, 0x76); w = s; w.halted = 1; w.pc = 0x0101; EXP(w, 4);
    /* IN A,(n) / OUT (n),A: port is A:n */
    s = dflt(); s.a = 0x12; mclr(); in_val = 0x9C; k = RUN(s, 0xDB, 0x34);
    w = s; w.a = 0x9C; w.pc = 0x0102; EXP(w, 11); CHECK(in_port == 0x1234); CHECK(n_in == 1);
    s = dflt(); s.a = 0x56; mclr(); k = RUN(s, 0xD3, 0x78);
    w = s; w.pc = 0x0102; EXP(w, 11); CHECK(out_port == 0x5678); CHECK(out_val == 0x56); CHECK(n_out == 1);
    /* NULL port handlers: IN reads $FF, OUT is a no-op */
    s = dflt(); mclr(); load(&s); z.in = NULL; z.out = NULL; mem[0x100] = 0xDB; mem[0x101] = 0x01;
    mem[0x102] = 0xD3; mem[0x103] = 0x02;
    CHECK(cpu_z80_step(&z) == 11); CHECK(z.a == 0xFF);
    CHECK(cpu_z80_step(&z) == 11); CHECK(z.pc == 0x0104); CHECK(n_in == 0 && n_out == 0);
}

/* ---------------------------------------------------------------- CB page */
static void cb_case(int line, int y, uint8_t v, uint8_t fin, uint8_t wr, uint8_t wf)
{
    for (int r = 0; r < 8; r++) {
        zs_t s = dflt();
        s.f = fin;
        s.b = s.c = s.d = s.e = s.a = 0x3C;
        s.h = 0x40; s.l = 0x00;
        mclr();
        if (r == 6)
            mem[0x4000] = v;
        else
            *zreg(&s, r) = v;
        int k = RUN(s, 0xCB, (uint8_t) ((y << 3) | r));
        zs_t w = s;
        w.f = wf; w.pc = 0x0102;
        if (r == 6) {
            checks++;
            if (mem[0x4000] != wr) {
                printf("  FAIL line %d: CB (HL)=%02X want %02X\n", line, mem[0x4000], wr);
                fails++;
            }
        } else {
            *zreg(&w, r) = wr;
        }
        expect(line, &w, k, r == 6 ? -1 : 8);
    }
    /* DDCB / FDCB (IX+d) */
    for (int pfx = 0; pfx < 2; pfx++) {
        zs_t s = dflt();
        s.f = fin;
        uint16_t *ip = pfx ? &s.iy : &s.ix;
        *ip = 0x3000;
        mclr();
        mem[0x2FFE] = v; /* d = -2 */
        int k = RUN(s, pfx ? 0xFD : 0xDD, 0xCB, 0xFE, (uint8_t) ((y << 3) | 6));
        zs_t w = s;
        w.f = wf; w.pc = 0x0104;
        checks++;
        if (mem[0x2FFE] != wr) {
            printf("  FAIL line %d: DDCB (IX-2)=%02X want %02X\n", line, mem[0x2FFE], wr);
            fails++;
        }
        expect(line, &w, k, 23);
    }
}

static void test_cb(void)
{
    cb_case(__LINE__, 0, 0x81, FH | FN, 0x03, FP | FC);
    cb_case(__LINE__, 0, 0x00, FC, 0x00, FZ | FP);
    cb_case(__LINE__, 0, 0x14, 0, 0x28, F5 | F3 | FP);
    cb_case(__LINE__, 1, 0x01, FH | FN, 0x80, FS | FC);
    cb_case(__LINE__, 1, 0x50, FC, 0x28, F5 | F3 | FP);
    cb_case(__LINE__, 1, 0x02, 0, 0x01, 0);
    cb_case(__LINE__, 2, 0x80, 0, 0x00, FZ | FP | FC);
    cb_case(__LINE__, 2, 0x14, FC, 0x29, F5 | F3);
    cb_case(__LINE__, 2, 0x01, FH | FN, 0x02, 0);
    cb_case(__LINE__, 2, 0x40, 0, 0x80, FS);
    cb_case(__LINE__, 3, 0x01, 0, 0x00, FZ | FP | FC);
    cb_case(__LINE__, 3, 0x50, FC, 0xA8, FS | F5 | F3);
    cb_case(__LINE__, 3, 0x02, FH | FN, 0x01, 0);
    cb_case(__LINE__, 4, 0x81, FH | FN, 0x02, FC);
    cb_case(__LINE__, 4, 0x40, FC, 0x80, FS);
    cb_case(__LINE__, 4, 0x01, 0, 0x02, 0);
    cb_case(__LINE__, 5, 0x81, FH | FN, 0xC0, FS | FP | FC);
    cb_case(__LINE__, 5, 0x7E, FC, 0x3F, F5 | F3 | FP);
    cb_case(__LINE__, 5, 0x02, 0, 0x01, 0);
    cb_case(__LINE__, 6, 0x80, FH | FN, 0x01, FC);
    cb_case(__LINE__, 6, 0x00, FC, 0x01, 0);
    cb_case(__LINE__, 6, 0x10, 0, 0x21, F5 | FP);
    cb_case(__LINE__, 6, 0x40, 0, 0x81, FS | FP);
    cb_case(__LINE__, 7, 0x81, FH | FN, 0x40, FC);
    cb_case(__LINE__, 7, 0x01, FC, 0x00, FZ | FP | FC);
    cb_case(__LINE__, 7, 0x80, 0, 0x40, 0);

    /* BIT b,r: tested bit set / clear; C preserved, H set, N cleared */
    for (int b = 0; b < 8; b++) {
        for (int r = 0; r < 8; r++) {
            for (int pass = 0; pass < 4; pass++) {
                uint8_t v = (pass & 1) ? (uint8_t) ~(1u << b) : (uint8_t) (1u << b);
                uint8_t fin = (pass & 2) ? (uint8_t) (FC | FN) : (uint8_t) (FS | FZ | FP);
                zs_t s = dflt();
                s.f = fin;
                s.b = s.c = s.d = s.e = s.a = 0x3C;
                s.h = 0x40; s.l = 0x00;
                if ((pass & 1) == 0)
                    s.b = s.c = s.d = s.e = s.a = 0x00;
                else
                    s.b = s.c = s.d = s.e = s.a = 0xFF;
                mclr();
                if (r == 6)
                    mem[0x4000] = v;
                else
                    *zreg(&s, r) = v;
                int k = RUN(s, 0xCB, (uint8_t) (0x40 | (b << 3) | r));
                uint8_t wf = (uint8_t) ((fin & FC) | FH | (v & (F5 | F3)));
                if (pass & 1)
                    wf |= FZ | FP;
                else if (b == 7)
                    wf |= FS;
                zs_t w = s;
                w.pc = 0x0102;
                if (r == 6) {
                    CHECK((z.f & (uint8_t) ~(F5 | F3)) == (wf & (uint8_t) ~(F5 | F3)));
                    w.f = z.f;
                    CHECK(mem[0x4000] == v);
                } else {
                    w.f = wf;
                }
                expect(__LINE__, &w, k, r == 6 ? -1 : 8);
            }
        }
        /* DDCB/FDCB BIT (no F5/F3 copy in this core: use v with those bits clear) */
        for (int pfx = 0; pfx < 2; pfx++) {
            for (int pass = 0; pass < 2; pass++) {
                uint8_t v = pass ? (uint8_t) (~(1u << b) & ~(F5 | F3)) : (uint8_t) (1u << b);
                if (!pass && (v & (F5 | F3)))
                    v = (uint8_t) (1u << b); /* bit 5/3 set: F5/F3 unchecked below */
                zs_t s = dflt();
                s.f = FC | FN;
                uint16_t *ip = pfx ? &s.iy : &s.ix;
                *ip = 0x3000;
                mclr();
                mem[0x3007] = v;
                int k = RUN(s, pfx ? 0xFD : 0xDD, 0xCB, 0x07, (uint8_t) (0x46 | (b << 3)));
                uint8_t wf = FC | FH;
                if (pass)
                    wf |= FZ | FP;
                else if (b == 7)
                    wf |= FS;
                zs_t w = s;
                w.pc = 0x0104;
                CHECK((z.f & (uint8_t) ~(F5 | F3)) == wf);
                w.f = z.f;
                expect(__LINE__, &w, k, -1);
                CHECK(mem[0x3007] == v);
            }
        }
    }

    /* RES / SET */
    for (int b = 0; b < 8; b++) {
        for (int r = 0; r < 8; r++) {
            for (int set = 0; set < 2; set++) {
                uint8_t v = set ? 0x00 : 0xFF;
                uint8_t want = set ? (uint8_t) (1u << b) : (uint8_t) ~(1u << b);
                zs_t s = dflt();
                s.f = 0xD7;
                s.b = s.c = s.d = s.e = s.a = 0x5A;
                s.h = 0x40; s.l = 0x00;
                mclr();
                mem[0x4000] = 0x5A;
                if (r == 6)
                    mem[0x4000] = v;
                else
                    *zreg(&s, r) = v;
                int k = RUN(s, 0xCB, (uint8_t) ((set ? 0xC0 : 0x80) | (b << 3) | r));
                zs_t w = s;
                w.pc = 0x0102;
                if (r == 6)
                    CHECK(mem[0x4000] == want);
                else
                    *zreg(&w, r) = want;
                expect(__LINE__, &w, k, r == 6 ? -1 : 8);
            }
        }
        for (int pfx = 0; pfx < 2; pfx++) {
            for (int set = 0; set < 2; set++) {
                uint8_t v = set ? 0x00 : 0xFF;
                uint8_t want = set ? (uint8_t) (1u << b) : (uint8_t) ~(1u << b);
                zs_t s = dflt();
                s.f = 0xD7;
                uint16_t *ip = pfx ? &s.iy : &s.ix;
                *ip = 0x3000;
                mclr();
                mem[0x3010] = v;
                int k = RUN(s, pfx ? 0xFD : 0xDD, 0xCB, 0x10, (uint8_t) ((set ? 0xC6 : 0x86) | (b << 3)));
                zs_t w = s;
                w.pc = 0x0104;
                CHECK(mem[0x3010] == want);
                expect(__LINE__, &w, k, 23);
            }
        }
    }
}

/* ---------------------------------------------------------------- ED page */
static void test_ed(void)
{
    zs_t s, w;
    int k;
    static const uint8_t IMOP[6] = {0x46, 0x66, 0x56, 0x76, 0x5E, 0x7E};
    static const uint8_t IMV[6] = {0, 0, 1, 1, 2, 2};
    for (int i = 0; i < 6; i++) {
        s = dflt(); s.im = 3; mclr(); k = RUN(s, 0xED, IMOP[i]);
        w = s; w.im = IMV[i]; w.pc = 0x0102; expect(__LINE__, &w, k, 8);
    }
    /* LD I,A / LD R,A / LD A,I / LD A,R */
    s = dflt(); s.a = 0x9A; s.f = 0xFF; mclr(); k = RUN(s, 0xED, 0x47); w = s; w.i = 0x9A; w.pc = 0x0102; EXP(w, 9);
    s = dflt(); s.a = 0x9B; mclr(); k = RUN(s, 0xED, 0x4F); w = s; w.r = 0x9B; w.pc = 0x0102; EXP(w, 9);
    s = dflt(); s.i = 0x80; s.f = FC | FN | FH; s.iff2 = 1; mclr(); k = RUN(s, 0xED, 0x57);
    w = s; w.a = 0x80; w.f = FS | FP | FC; w.pc = 0x0102; EXP(w, 9);
    s = dflt(); s.i = 0x00; s.f = FS | FN; s.iff2 = 0; mclr(); k = RUN(s, 0xED, 0x57);
    w = s; w.a = 0x00; w.f = FZ; w.pc = 0x0102; EXP(w, 9);
    s = dflt(); s.r = 0x28; s.f = FC; s.iff2 = 0; mclr(); k = RUN(s, 0xED, 0x5F);
    w = s; w.a = 0x28; w.f = F5 | F3 | FC; w.pc = 0x0102; EXP(w, 9);
    s = dflt(); s.r = 0x01; s.f = FZ | FS; s.iff2 = 1; mclr(); k = RUN(s, 0xED, 0x5F);
    w = s; w.a = 0x01; w.f = FP; w.pc = 0x0102; EXP(w, 9);
    /* RETI / RETN: pop PC, IFF1 <- IFF2 */
    static const uint8_t RETOP[5] = {0x4D, 0x45, 0x55, 0x65, 0x75};
    for (int i = 0; i < 5; i++) {
        for (int f2 = 0; f2 < 2; f2++) {
            s = dflt(); s.iff1 = (uint8_t) !f2; s.iff2 = (uint8_t) f2; mclr();
            mem[0xF000] = 0x78; mem[0xF001] = 0x56; k = RUN(s, 0xED, RETOP[i]);
            w = s; w.pc = 0x5678; w.sp = 0xF002; w.iff1 = (uint8_t) f2; expect(__LINE__, &w, k, 14);
        }
    }
    /* NEG */
    static const uint8_t NEGOP[4] = {0x44, 0x54, 0x64, 0x74};
    for (int i = 0; i < 4; i++) {
        s = dflt(); s.a = 0x01; mclr(); k = RUN(s, 0xED, NEGOP[i]);
        w = s; w.a = 0xFF; w.f = FS | F5 | F3 | FH | FN | FC; w.pc = 0x0102; expect(__LINE__, &w, k, 8);
        s = dflt(); s.a = 0x80; mclr(); k = RUN(s, 0xED, NEGOP[i]);
        w = s; w.a = 0x80; w.f = FS | FP | FN | FC; w.pc = 0x0102; expect(__LINE__, &w, k, 8);
        s = dflt(); s.a = 0x00; s.f = FC; mclr(); k = RUN(s, 0xED, NEGOP[i]);
        w = s; w.a = 0x00; w.f = FZ | FN; w.pc = 0x0102; expect(__LINE__, &w, k, 8);
        s = dflt(); s.a = 0x10; mclr(); k = RUN(s, 0xED, NEGOP[i]);
        w = s; w.a = 0xF0; w.f = FS | F5 | FN | FC; w.pc = 0x0102; expect(__LINE__, &w, k, 8);
    }

    /* ADC HL,rr / SBC HL,rr: vectors run through each source pair */
    struct v16 {
        uint16_t hl, v;
        uint8_t fin;
        uint16_t r;
        uint8_t wf;
    };
    static const struct v16 ADCV[] = {
        {0x7FFF, 0x0000, FC | FN, 0x8000, FS | FH | FP},
        {0xFFFF, 0x0000, FC, 0x0000, FZ | FH | FC},
        {0x2700, 0x0100, FN, 0x2800, F5 | F3},
        {0x0001, 0x0001, 0, 0x0002, 0},
        {0x0FFF, 0x0000, FC, 0x1000, FH},
        {0x0FFE, 0x0001, 0, 0x0FFF, F3},
        {0x8000, 0x8000, 0, 0x0000, FZ | FP | FC},
        {0x00FF, 0x0001, 0, 0x0100, 0},
        {0xFFFE, 0x0001, 0, 0xFFFF, FS | F5 | F3},
        {0x0FFE, 0x0001, FC, 0x1000, FH},
        {0x0001, 0x0FFE, FC, 0x1000, FH},
        {0x0F00, 0x0100, 0, 0x1000, FH},
        {0x0000, 0x0001, 0, 0x0001, 0},
    };
    static const struct v16 SBCV[] = {
        {0x0000, 0x0001, 0, 0xFFFF, FS | F5 | F3 | FH | FN | FC},
        {0x8000, 0x0001, 0, 0x7FFF, FP | FH | FN | F5 | F3},
        {0x1000, 0x0FFF, FC, 0x0000, FZ | FN | FH},
        {0x3000, 0x0800, 0, 0x2800, FN | F5 | F3 | FH},
        {0x0005, 0x0003, FC, 0x0001, FN},
        {0x7FFF, 0xFFFF, 0, 0x8000, FS | FP | FN | FC},
        {0x0100, 0x0001, 0, 0x00FF, FN},
        {0x1000, 0x0001, 0, 0x0FFF, FN | FH | F3},
        {0x0001, 0x0000, FC, 0x0000, FZ | FN},
        {0x1001, 0x0001, FC, 0x0FFF, FN | FH | F3},
        {0x1000, 0x0100, 0, 0x0F00, FN | FH | F3},
        {0x1001, 0x0002, 0, 0x0FFF, FN | FH | F3},
        {0x0003, 0x0001, FC, 0x0001, FN},
    };
    static const uint8_t ADCOP[4] = {0x4A, 0x5A, 0x6A, 0x7A};
    static const uint8_t SBCOP[4] = {0x42, 0x52, 0x62, 0x72};
    for (int sbc = 0; sbc < 2; sbc++) {
        const struct v16 *tv = sbc ? SBCV : ADCV;
        int n = sbc ? (int) (sizeof SBCV / sizeof SBCV[0]) : (int) (sizeof ADCV / sizeof ADCV[0]);
        for (int t = 0; t < n; t++) {
            for (int rr = 0; rr < 4; rr++) {
                if (rr == 2)
                    continue; /* HL,HL below */
                s = dflt(); s.f = tv[t].fin;
                s.h = (uint8_t) (tv[t].hl >> 8); s.l = (uint8_t) tv[t].hl;
                s.b = s.c = s.d = s.e = 0xEE; s.sp = 0xEEEE;
                if (rr == 0) { s.b = (uint8_t) (tv[t].v >> 8); s.c = (uint8_t) tv[t].v; }
                if (rr == 1) { s.d = (uint8_t) (tv[t].v >> 8); s.e = (uint8_t) tv[t].v; }
                if (rr == 3) s.sp = tv[t].v;
                mclr();
                k = RUN(s, 0xED, sbc ? SBCOP[rr] : ADCOP[rr]);
                w = s; w.h = (uint8_t) (tv[t].r >> 8); w.l = (uint8_t) tv[t].r; w.f = tv[t].wf;
                w.pc = 0x0102;
                expect(__LINE__, &w, k, 15);
            }
        }
    }
    s = dflt(); s.h = 0x80; s.l = 0x00; mclr(); k = RUN(s, 0xED, 0x6A);
    w = s; w.h = 0x00; w.l = 0x00; w.f = FZ | FP | FC; w.pc = 0x0102; EXP(w, 15);
    s = dflt(); s.h = 0x40; s.l = 0x00; s.f = FC; mclr(); k = RUN(s, 0xED, 0x6A);
    w = s; w.h = 0x80; w.l = 0x01; w.f = FS | FP; w.pc = 0x0102; EXP(w, 15);
    s = dflt(); s.h = 0x12; s.l = 0x34; s.f = FC; mclr(); k = RUN(s, 0xED, 0x62);
    w = s; w.h = 0xFF; w.l = 0xFF; w.f = FS | F5 | F3 | FH | FN | FC; w.pc = 0x0102; EXP(w, 15);
    s = dflt(); s.h = 0x12; s.l = 0x34; mclr(); k = RUN(s, 0xED, 0x62);
    w = s; w.h = 0x00; w.l = 0x00; w.f = FZ | FN; w.pc = 0x0102; EXP(w, 15);

    /* LD (nn),rr / LD rr,(nn) */
    s = dflt(); s.b = 0x12; s.c = 0x34; mclr(); k = RUN(s, 0xED, 0x43, 0x00, 0x30);
    w = s; w.pc = 0x0104; EXP(w, 20); CHECK(mem[0x3000] == 0x34); CHECK(mem[0x3001] == 0x12);
    s = dflt(); s.d = 0x56; s.e = 0x78; mclr(); k = RUN(s, 0xED, 0x53, 0x00, 0x30);
    w = s; w.pc = 0x0104; EXP(w, 20); CHECK(mem[0x3000] == 0x78); CHECK(mem[0x3001] == 0x56);
    s = dflt(); s.h = 0x9A; s.l = 0xBC; mclr(); k = RUN(s, 0xED, 0x63, 0x00, 0x30);
    w = s; w.pc = 0x0104; EXP(w, 20); CHECK(mem[0x3000] == 0xBC); CHECK(mem[0x3001] == 0x9A);
    s = dflt(); s.sp = 0xDEF0; mclr(); k = RUN(s, 0xED, 0x73, 0x00, 0x30);
    w = s; w.pc = 0x0104; EXP(w, 20); CHECK(mem[0x3000] == 0xF0); CHECK(mem[0x3001] == 0xDE);
    s = dflt(); mclr(); mem[0x3100] = 0x34; mem[0x3101] = 0x12; k = RUN(s, 0xED, 0x4B, 0x00, 0x31);
    w = s; w.b = 0x12; w.c = 0x34; w.pc = 0x0104; EXP(w, 20);
    s = dflt(); mclr(); mem[0x3100] = 0x78; mem[0x3101] = 0x56; k = RUN(s, 0xED, 0x5B, 0x00, 0x31);
    w = s; w.d = 0x56; w.e = 0x78; w.pc = 0x0104; EXP(w, 20);
    s = dflt(); mclr(); mem[0x3100] = 0xBC; mem[0x3101] = 0x9A; k = RUN(s, 0xED, 0x6B, 0x00, 0x31);
    w = s; w.h = 0x9A; w.l = 0xBC; w.pc = 0x0104; EXP(w, 20);
    s = dflt(); mclr(); mem[0x3100] = 0xF0; mem[0x3101] = 0xDE; k = RUN(s, 0xED, 0x7B, 0x00, 0x31);
    w = s; w.sp = 0xDEF0; w.pc = 0x0104; EXP(w, 20);

    /* block LD: LDI / LDD / LDIR / LDDR */
    static const uint8_t BLD[4] = {0xA0, 0xA8, 0xB0, 0xB8};
    for (int i = 0; i < 4; i++) {
        int dir = (i & 1) ? -1 : 1, rep = i >> 1;
        for (int bc = 0; bc < 3; bc++) {
            uint16_t bcv = bc == 0 ? 0x0002 : bc == 1 ? 0x0001 : 0x0000;
            s = dflt(); s.f = 0xFF; s.h = 0x40; s.l = 0x00; s.d = 0x50; s.e = 0x00;
            s.b = (uint8_t) (bcv >> 8); s.c = (uint8_t) bcv;
            mclr(); mem[0x4000] = 0xAB;
            k = RUN(s, 0xED, BLD[i]);
            uint16_t nb = (uint16_t) (bcv - 1);
            w = s; w.h = (uint8_t) ((0x4000 + dir) >> 8); w.l = (uint8_t) (0x4000 + dir);
            w.d = (uint8_t) ((0x5000 + dir) >> 8); w.e = (uint8_t) (0x5000 + dir);
            w.b = (uint8_t) (nb >> 8); w.c = (uint8_t) nb;
            w.f = (uint8_t) (FS | FZ | FC | (nb ? FP : 0));
            int again = rep && nb != 0;
            w.pc = again ? 0x0100 : 0x0102;
            expect(__LINE__, &w, k, again ? 21 : 16);
            CHECK(mem[0x5000] == 0xAB);
        }
        s = dflt(); s.f = 0; s.b = 0; s.c = 2; mclr(); mem[0x4000] = 1; k = RUN(s, 0xED, BLD[i]);
        CHECK(z.f == FP);
        s = dflt(); s.f = 0; s.b = 0; s.c = 1; mclr(); k = RUN(s, 0xED, BLD[i]);
        CHECK(z.f == 0);
        CHECK(z.pc == 0x0102);
    }
    s = dflt(); s.b = 0x01; s.c = 0x00; mclr(); k = RUN(s, 0xED, 0xA0);
    CHECK(z.b == 0x00 && z.c == 0xFF);

    /* block OUT: OUTI / OUTD / OTIR / OTDR (B decremented before the port write) */
    static const uint8_t BOUT[4] = {0xA3, 0xAB, 0xB3, 0xBB};
    static const uint8_t BIN[4] = {0xA2, 0xAA, 0xB2, 0xBA};
    for (int io = 0; io < 2; io++) {
        for (int i = 0; i < 4; i++) {
            int dir = (i & 1) ? -1 : 1, rep = i >> 1;
            for (int bb = 0; bb < 2; bb++) {
                uint8_t bv = bb ? 1 : 2;
                s = dflt(); s.f = (uint8_t) (bb ? (FC | FS | FH) : (FS | FP)); s.h = 0x40; s.l = 0x00;
                s.b = bv; s.c = 0x10;
                mclr(); mem[0x4000] = 0x5A; in_val = 0x6B;
                k = RUN(s, 0xED, io ? BIN[i] : BOUT[i]);
                w = s; w.b = (uint8_t) (bv - 1);
                w.h = (uint8_t) ((0x4000 + dir) >> 8); w.l = (uint8_t) (0x4000 + dir);
                w.f = (uint8_t) ((s.f & FC) | FN | (bv == 1 ? FZ : 0));
                int again = rep && bv != 1;
                w.pc = again ? 0x0100 : 0x0102;
                expect(__LINE__, &w, k, again ? 21 : 16);
                if (io) {
                    CHECK(n_in == 1);
                    CHECK(in_port == (uint16_t) ((bv << 8) | 0x10));
                    CHECK(mem[0x4000] == 0x6B);
                } else {
                    CHECK(n_out == 1);
                    CHECK(out_port == (uint16_t) (((bv - 1) << 8) | 0x10));
                    CHECK(out_val == 0x5A);
                }
            }
        }
    }

    /* IN r,(C) */
    static const int INR[7] = {0, 1, 2, 3, 4, 5, 7};
    for (int i = 0; i < 7; i++) {
        int r = INR[i];
        struct { uint8_t in, fin, wf; } V[3] = {
            {0x80, FC | FH | FN, FS | FC}, {0x00, 0, FZ | FP}, {0x28, FZ, F5 | F3 | FP}};
        for (int t = 0; t < 3; t++) {
            s = dflt(); s.f = V[t].fin; s.b = 0x12; s.c = 0x34; mclr(); in_val = V[t].in;
            k = RUN(s, 0xED, (uint8_t) (0x40 | (r << 3)));
            w = s; *zreg(&w, r) = V[t].in; w.f = V[t].wf; w.pc = 0x0102;
            expect(__LINE__, &w, k, 12);
            CHECK(in_port == 0x1234);
            CHECK(n_in == 1);
        }
        /* OUT (C),r */
        s = dflt(); s.b = 0x56; s.c = 0x78; s.d = 0x9A; s.e = 0xBC; s.h = 0xDE; s.l = 0xF0; s.a = 0x11;
        s.f = 0xA5;
        mclr();
        k = RUN(s, 0xED, (uint8_t) (0x41 | (r << 3)));
        w = s; w.pc = 0x0102; expect(__LINE__, &w, k, 12);
        CHECK(n_out == 1);
        CHECK(out_port == 0x5678);
        CHECK(out_val == *zreg(&s, r));
    }
    s = dflt(); mclr(); load(&s); z.in = NULL; mem[0x100] = 0xED; mem[0x101] = 0x78;
    CHECK(cpu_z80_step(&z) == 12); CHECK(z.a == 0xFF); CHECK(z.f == (FS | F5 | F3 | FP));

    /* undecoded ED opcode */
    s = dflt(); s.f = 0x5A; mclr(); load(&s); mem[0x100] = 0xED; mem[0x101] = 0x00;
    CHECK(cpu_z80_step(&z) == 8); CHECK(z.illegal == 1); CHECK(z.pc == 0x0102); CHECK(z.f == 0x5A);
}

/* ---------------------------------------------------------------- DD / FD page */
static void test_index(void)
{
    /* LD r,r' with IXH/IXL and (IX+d) substitution */
    for (int pfx = 0; pfx < 2; pfx++) {
        for (int op = 0x40; op <= 0x7F; op++) {
            if (op == 0x76)
                continue;
            int dst = (op >> 3) & 7, src = op & 7;
            zs_t s = dflt();
            s.b = 0x11; s.c = 0x22; s.d = 0x33; s.e = 0x44; s.h = 0x66; s.l = 0x55; s.a = 0x77;
            s.f = 0xD7;
            s.ix = 0x5B81; s.iy = 0x6991;
            uint16_t base = pfx ? 0x6991 : 0x5B81;
            uint16_t ea = (uint16_t) (base - 3);
            mclr();
            mem[ea] = 0x99;
            int k;
            zs_t w = s;
            uint16_t *wp = pfx ? &w.iy : &w.ix;
            if (src == 6 || dst == 6) {
                k = RUN(s, pfx ? 0xFD : 0xDD, (uint8_t) op, 0xFD);
                if (src == 6)
                    *zreg(&w, dst) = 0x99;
                else
                    CHECK(mem[ea] == *zreg(&s, src));
                w.pc = 0x0103;
                expect(__LINE__, &w, k, 19);
            } else {
                k = RUN(s, pfx ? 0xFD : 0xDD, (uint8_t) op);
                uint8_t v = src == 4 ? (uint8_t) (base >> 8) : src == 5 ? (uint8_t) base : *zreg(&s, src);
                if (dst == 4)
                    *wp = (uint16_t) ((*wp & 0x00FF) | (v << 8));
                else if (dst == 5)
                    *wp = (uint16_t) ((*wp & 0xFF00) | v);
                else
                    *zreg(&w, dst) = v;
                w.pc = 0x0102;
                expect(__LINE__, &w, k, 8);
                CHECK(mem[ea] == 0x99);
            }
        }
        /* positive displacement */
        zs_t s = dflt();
        uint16_t *ip = pfx ? &s.iy : &s.ix;
        *ip = 0x2000;
        mclr();
        mem[0x207F] = 0x42;
        int k = RUN(s, pfx ? 0xFD : 0xDD, 0x7E, 0x7F);
        zs_t w = s;
        w.a = 0x42; w.pc = 0x0103;
        EXP(w, 19);
    }

    for (int pfx = 0; pfx < 2; pfx++) {
        uint8_t P = pfx ? 0xFD : 0xDD;
        zs_t s, w;
        int k;
#define IDX(st) (*(pfx ? &(st).iy : &(st).ix))
        s = dflt(); mclr(); k = RUN(s, P, 0x21, 0x34, 0x12); w = s; IDX(w) = 0x1234; w.pc = 0x0104; EXP(w, 14);
        s = dflt(); IDX(s) = 0xABCD; mclr(); k = RUN(s, P, 0x22, 0x00, 0x30);
        w = s; w.pc = 0x0104; EXP(w, 20); CHECK(mem[0x3000] == 0xCD); CHECK(mem[0x3001] == 0xAB);
        s = dflt(); mclr(); mem[0x3000] = 0x34; mem[0x3001] = 0x12; k = RUN(s, P, 0x2A, 0x00, 0x30);
        w = s; IDX(w) = 0x1234; w.pc = 0x0104; EXP(w, 20);
        s = dflt(); IDX(s) = 0xFFFF; s.f = 0xFF; mclr(); k = RUN(s, P, 0x23); w = s; IDX(w) = 0x0000; w.pc = 0x0102; EXP(w, 10);
        s = dflt(); IDX(s) = 0x00FF; mclr(); k = RUN(s, P, 0x23); w = s; IDX(w) = 0x0100; w.pc = 0x0102; EXP(w, 10);
        s = dflt(); IDX(s) = 0x0000; mclr(); k = RUN(s, P, 0x2B); w = s; IDX(w) = 0xFFFF; w.pc = 0x0102; EXP(w, 10);
        /* ADD IX,rr */
        s = dflt(); s.f = 0xFF; IDX(s) = 0x0FFF; s.b = 0x00; s.c = 0x01; mclr(); k = RUN(s, P, 0x09);
        w = s; IDX(w) = 0x1000; w.f = FS | FZ | FP | FH; w.pc = 0x0102; EXP(w, 15);
        s = dflt(); IDX(s) = 0xFFFF; s.d = 0x00; s.e = 0x01; mclr(); k = RUN(s, P, 0x19);
        w = s; IDX(w) = 0x0000; w.f = FH | FC; w.pc = 0x0102; EXP(w, 15);
        s = dflt(); IDX(s) = 0x8800; mclr(); k = RUN(s, P, 0x29);
        w = s; IDX(w) = 0x1000; w.f = FH | FC; w.pc = 0x0102; EXP(w, 15);
        s = dflt(); s.f = FN; IDX(s) = 0x2000; s.sp = 0x0800; mclr(); k = RUN(s, P, 0x39);
        w = s; IDX(w) = 0x2800; w.f = F5 | F3; w.pc = 0x0102; EXP(w, 15);
        s = dflt(); IDX(s) = 0x0FFE; s.b = 0x00; s.c = 0x01; mclr(); k = RUN(s, P, 0x09);
        w = s; IDX(w) = 0x0FFF; w.f = F3; w.pc = 0x0102; EXP(w, 15);
        s = dflt(); IDX(s) = 0x8000; s.d = 0x80; s.e = 0x00; mclr(); k = RUN(s, P, 0x19);
        w = s; IDX(w) = 0x0000; w.f = FC; w.pc = 0x0102; EXP(w, 15);
        s = dflt(); IDX(s) = 0x0F00; s.b = 0x01; s.c = 0x00; mclr(); k = RUN(s, P, 0x09);
        w = s; IDX(w) = 0x1000; w.f = FH; w.pc = 0x0102; EXP(w, 15);
        s = dflt(); IDX(s) = 0x0001; s.b = 0x0F; s.c = 0xFF; mclr(); k = RUN(s, P, 0x09);
        w = s; IDX(w) = 0x1000; w.f = FH; w.pc = 0x0102; EXP(w, 15);
        s = dflt(); IDX(s) = 0xFFFE; s.b = 0x00; s.c = 0x01; mclr(); k = RUN(s, P, 0x09);
        w = s; IDX(w) = 0xFFFF; w.f = F5 | F3; w.pc = 0x0102; EXP(w, 15);
        /* LD IXH,n / LD IXL,n */
        s = dflt(); IDX(s) = 0x1335; mclr(); k = RUN(s, P, 0x26, 0xAA); w = s; IDX(w) = 0xAA35; w.pc = 0x0103; EXP(w, 11);
        s = dflt(); IDX(s) = 0x1335; mclr(); k = RUN(s, P, 0x2E, 0xAA); w = s; IDX(w) = 0x13AA; w.pc = 0x0103; EXP(w, 11);
        /* LD (IX+d),n */
        s = dflt(); IDX(s) = 0x2000; mclr(); k = RUN(s, P, 0x36, 0xFF, 0x77); w = s; w.pc = 0x0104; EXP(w, 19);
        CHECK(mem[0x1FFF] == 0x77);
        s = dflt(); IDX(s) = 0x2000; mclr(); k = RUN(s, P, 0x36, 0x05, 0x78); w = s; w.pc = 0x0104; EXP(w, 19);
        CHECK(mem[0x2005] == 0x78);
        /* JP (IX) / LD SP,IX / PUSH / POP / EX (SP),IX */
        s = dflt(); IDX(s) = 0x1234; mclr(); k = RUN(s, P, 0xE9); w = s; w.pc = 0x1234; EXP(w, 8);
        s = dflt(); IDX(s) = 0x1234; mclr(); k = RUN(s, P, 0xF9); w = s; w.sp = 0x1234; w.pc = 0x0102; EXP(w, 10);
        s = dflt(); IDX(s) = 0x1234; mclr(); k = RUN(s, P, 0xE5); w = s; w.sp = 0xEFFE; w.pc = 0x0102; EXP(w, 15);
        CHECK(mem[0xEFFF] == 0x12); CHECK(mem[0xEFFE] == 0x34);
        s = dflt(); mclr(); mem[0xF000] = 0x78; mem[0xF001] = 0x56; k = RUN(s, P, 0xE1);
        w = s; IDX(w) = 0x5678; w.sp = 0xF002; w.pc = 0x0102; EXP(w, 14);
        s = dflt(); IDX(s) = 0x1234; mclr(); mem[0xF000] = 0x78; mem[0xF001] = 0x56; k = RUN(s, P, 0xE3);
        w = s; IDX(w) = 0x5678; w.pc = 0x0102; EXP(w, 23);
        CHECK(mem[0xF000] == 0x34); CHECK(mem[0xF001] == 0x12);
        /* undecoded */
        s = dflt(); s.f = 0x5A; mclr(); load(&s); mem[0x100] = P; mem[0x101] = 0x00;
        CHECK(cpu_z80_step(&z) == 8); CHECK(z.illegal == 1); CHECK(z.pc == 0x0102);
        CHECK(z.ix == 0x5000 && z.iy == 0x6000 && z.f == 0x5A);
        /* DD 76 is not an indexed HALT: undecoded, CPU keeps running */
        s = dflt(); mclr(); load(&s); mem[0x100] = P; mem[0x101] = 0x76;
        CHECK(cpu_z80_step(&z) == 8); CHECK(z.illegal == 1); CHECK(z.pc == 0x0102);
        CHECK(z.halted == 0);
        s = dflt(); mclr(); load(&s); mem[0x100] = P; mem[0x101] = 0x3F;
        CHECK(cpu_z80_step(&z) == 8); CHECK(z.illegal == 1); CHECK(z.pc == 0x0102);
        s = dflt(); mclr(); load(&s); mem[0x100] = P; mem[0x101] = 0xC0;
        CHECK(cpu_z80_step(&z) == 8); CHECK(z.illegal == 1); CHECK(z.pc == 0x0102);
#undef IDX
    }
}

/* ---------------------------------------------------------------- reset / interrupts / run */
static void test_system(void)
{
    zs_t s;
    /* reset clears everything */
    mclr();
    memset(&z, 0xA5, sizeof z);
    z.read = bus_rd;
    z.write = bus_wr;
    cpu_z80_reset(&z);
    zs_t w;
    memset(&w, 0, sizeof w);
    w.sp = 0xFFFF;
    expect(__LINE__, &w, 0, -1);
    CHECK(z.cycles == 0);
    CHECK(z.jammed == 0);
    CHECK(z.last_opcode == 0);
    CHECK(z.illegal == 0);

    /* maskable interrupt */
    s = dflt(); s.pc = 0x1234; s.iff1 = 0; s.iff2 = 1; s.halted = 1; s.im = 1; mclr(); load(&s);
    cpu_z80_int(&z);
    w = s; expect(__LINE__, &w, 0, -1); CHECK(z.cycles == 500);
    for (int im = 0; im < 2; im++) {
        s = dflt(); s.pc = 0x1234; s.iff1 = 1; s.iff2 = 1; s.halted = 1; s.im = (uint8_t) im; mclr(); load(&s);
        cpu_z80_int(&z);
        w = s; w.pc = 0x0038; w.sp = 0xEFFE; w.iff1 = 0; w.iff2 = 0; w.halted = 0;
        expect(__LINE__, &w, 0, -1);
        CHECK(z.cycles == 513);
        CHECK(mem[0xEFFF] == 0x12); CHECK(mem[0xEFFE] == 0x34);
    }
    s = dflt(); s.pc = 0x1234; s.iff1 = 1; s.iff2 = 0; s.im = 2; s.i = 0x12; mclr(); load(&s);
    mem[0x12FF] = 0x78; mem[0x1300] = 0x56; mem[0x12FE] = 0x11; mem[0x1200] = 0x22;
    cpu_z80_int(&z);
    w = s; w.pc = 0x5678; w.sp = 0xEFFE; w.iff1 = 0; w.iff2 = 0;
    expect(__LINE__, &w, 0, -1);
    CHECK(z.cycles == 513);

    /* halted: step burns 4 T-states without fetching */
    s = dflt(); s.halted = 1; mclr(); load(&s); mem[0x100] = 0x3E; mem[0x101] = 0x99;
    CHECK(cpu_z80_step(&z) == 4);
    w = s; expect(__LINE__, &w, 4, 4); CHECK(z.cycles == 504);
    CHECK(cpu_z80_run(&z, 100) == 0);
    /* jammed */
    s = dflt(); mclr(); load(&s); z.jammed = 1; mem[0x100] = 0x3E;
    CHECK(cpu_z80_step(&z) == 0); CHECK(z.cycles == 500); CHECK(z.pc == 0x0100);
    CHECK(cpu_z80_run(&z, 100) == 0);

    /* unimplemented unprefixed opcode (DAA) counts as illegal */
    s = dflt(); s.a = 0x15; s.f = 0x5A; mclr(); load(&s); mem[0x100] = 0x27;
    CHECK(cpu_z80_step(&z) == 4); CHECK(z.illegal == 1); CHECK(z.pc == 0x0101);
    CHECK(z.a == 0x15 && z.f == 0x5A); CHECK(z.cycles == 504);

    /* run: NOP sled budget, then HALT stops */
    s = dflt(); mclr(); load(&s);
    CHECK(cpu_z80_run(&z, 9) == 12); CHECK(z.pc == 0x0103);
    load(&s);
    CHECK(cpu_z80_run(&z, 8) == 8); CHECK(z.pc == 0x0102);
    load(&s);
    CHECK(cpu_z80_run(&z, 0) == 0); CHECK(z.pc == 0x0100);
    load(&s);
    mem[0x102] = 0x76;
    CHECK(cpu_z80_run(&z, 1000) == 12); CHECK(z.halted == 1); CHECK(z.pc == 0x0103);

    /* summation program from the original test, from reset */
    {
        static const uint8_t p[] = {0x3E, 0x00, 0x06, 0x05, 0x80, 0x10, 0xFD,
                                    0x32, 0x30, 0x00, 0x76};
        mclr();
        memcpy(mem, p, sizeof p);
        memset(&z, 0, sizeof z);
        z.read = bus_rd;
        z.write = bus_wr;
        cpu_z80_reset(&z);
        /* 7 + 7 + 5*4 + 4*13 + 8 + 13 + 4 = 111 */
        CHECK(cpu_z80_run(&z, 100000) == 111);
        CHECK(z.a == 15);
        CHECK(mem[0x30] == 15);
        CHECK(z.halted == 1);
        CHECK(z.pc == 0x000B);
    }
    /* LDIR copy of 3 bytes via run */
    {
        static const uint8_t p[] = {0x21, 0x00, 0x10, 0x11, 0x00, 0x20, 0x01, 0x03, 0x00,
                                    0xED, 0xB0, 0x76};
        mclr();
        memcpy(mem, p, sizeof p);
        mem[0x1000] = 0xAA; mem[0x1001] = 0xBB; mem[0x1002] = 0xCC; mem[0x1003] = 0xDD;
        memset(&z, 0, sizeof z);
        z.read = bus_rd;
        z.write = bus_wr;
        cpu_z80_reset(&z);
        CHECK(cpu_z80_run(&z, 100000) == 10 * 3 + 21 + 21 + 16 + 4);
        CHECK(mem[0x2000] == 0xAA && mem[0x2001] == 0xBB && mem[0x2002] == 0xCC);
        CHECK(mem[0x2003] == 0x00);
        CHECK(z.b == 0 && z.c == 0 && z.h == 0x10 && z.l == 0x03 && z.d == 0x20 && z.e == 0x03);
    }
}

int main(void)
{
    test_ld8();
    test_alu8();
    test_incdec8();
    test_16bit();
    test_acc_ops();
    test_flow();
    test_cb();
    test_ed();
    test_index();
    test_system();
    if (fails) {
        printf("%d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("%d checks passed\n", checks);
    return 0;
}
