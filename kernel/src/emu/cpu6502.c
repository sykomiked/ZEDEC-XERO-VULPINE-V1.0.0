/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* cpu6502.c — MOS 6502 core. All official opcodes + addressing modes. See .h. */
#include "cpu6502.h"

#define C CPU6502_C
#define Z CPU6502_Z
#define I CPU6502_I
#define D CPU6502_D
#define B CPU6502_B
#define U CPU6502_U
#define V CPU6502_V
#define N CPU6502_N

static inline uint8_t  rd(cpu6502_t *c, uint16_t a)              { return c->read(c, a); }
static inline void     wr(cpu6502_t *c, uint16_t a, uint8_t v)   { c->write(c, a, v); }
static inline uint16_t rd16(cpu6502_t *c, uint16_t a)           { return (uint16_t)(rd(c, a) | (rd(c, (uint16_t)(a + 1)) << 8)); }
/* zero-page 16-bit read with page wrap (low/high both in $00xx) */
static inline uint16_t rd16zp(cpu6502_t *c, uint8_t zp)         { return (uint16_t)(rd(c, zp) | (rd(c, (uint8_t)(zp + 1)) << 8)); }

static inline void push(cpu6502_t *c, uint8_t v)  { wr(c, (uint16_t)(0x100 + c->sp), v); c->sp--; }
static inline uint8_t pull(cpu6502_t *c)          { c->sp++; return rd(c, (uint16_t)(0x100 + c->sp)); }
static inline void setflag(cpu6502_t *c, uint8_t m, int on) { if (on) c->p |= m; else c->p &= (uint8_t)~m; }
static inline void setzn(cpu6502_t *c, uint8_t v) { setflag(c, Z, v == 0); setflag(c, N, v & 0x80); }

static void adc(cpu6502_t *c, uint8_t v) {
    uint16_t s = (uint16_t)c->a + v + (c->p & C ? 1 : 0);
    setflag(c, V, (~(c->a ^ v) & (c->a ^ s) & 0x80) != 0);
    if (c->p & D) {                              /* NMOS decimal add */
        uint16_t lo = (c->a & 0x0F) + (v & 0x0F) + (c->p & C ? 1 : 0);
        uint16_t hi = (c->a >> 4) + (v >> 4) + (lo > 9 ? 1 : 0);
        if (lo > 9) lo += 6;
        setflag(c, C, hi > 9);
        if (hi > 9) hi += 6;
        c->a = (uint8_t)(((hi & 0xF) << 4) | (lo & 0xF));
    } else {
        setflag(c, C, s > 0xFF);
        c->a = (uint8_t)s;
    }
    setzn(c, c->a);
}
static void sbc(cpu6502_t *c, uint8_t v) {
    if (c->p & D) {                              /* NMOS decimal subtract */
        int carry = (c->p & C) ? 1 : 0;
        int lo = (c->a & 0x0F) - (v & 0x0F) - (1 - carry);
        int hi = (c->a >> 4) - (v >> 4);
        if (lo < 0) { lo += 10; hi--; }
        if (hi < 0) hi += 10;
        uint16_t bin = (uint16_t)c->a + (uint8_t)~v + carry;
        setflag(c, V, (((c->a ^ v) & (c->a ^ (bin & 0xFF)) & 0x80)) != 0);
        setflag(c, C, bin > 0xFF);
        c->a = (uint8_t)(((hi & 0xF) << 4) | (lo & 0xF));
        setzn(c, c->a);
    } else {
        adc(c, (uint8_t)~v);
    }
}
static void cmp(cpu6502_t *c, uint8_t reg, uint8_t v) {
    uint16_t t = (uint16_t)reg - v;
    setflag(c, C, reg >= v);
    setzn(c, (uint8_t)t);
}
static void branch(cpu6502_t *c, int take, int8_t off, int *cyc) {
    if (take) {
        uint16_t old = c->pc;
        c->pc = (uint16_t)(c->pc + off);
        *cyc += 1 + (((old ^ c->pc) & 0xFF00) ? 1 : 0);   /* +1 taken, +1 page cross */
    }
}
static uint8_t asl(cpu6502_t *c, uint8_t v) { setflag(c, C, v & 0x80); v = (uint8_t)(v << 1); setzn(c, v); return v; }
static uint8_t lsr(cpu6502_t *c, uint8_t v) { setflag(c, C, v & 0x01); v = (uint8_t)(v >> 1); setzn(c, v); return v; }
static uint8_t rol(cpu6502_t *c, uint8_t v) { uint8_t cin = c->p & C ? 1 : 0; setflag(c, C, v & 0x80); v = (uint8_t)((v << 1) | cin); setzn(c, v); return v; }
static uint8_t ror(cpu6502_t *c, uint8_t v) { uint8_t cin = c->p & C ? 0x80 : 0; setflag(c, C, v & 0x01); v = (uint8_t)((v >> 1) | cin); setzn(c, v); return v; }

void cpu6502_reset(cpu6502_t *c) {
    c->sp = 0xFD; c->p = U | I; c->a = c->x = c->y = 0;
    c->pc = rd16(c, 0xFFFC); c->cycles = 0; c->jammed = 0;
}
void cpu6502_nmi(cpu6502_t *c) {
    push(c, (uint8_t)(c->pc >> 8)); push(c, (uint8_t)c->pc);
    push(c, (uint8_t)((c->p & ~B) | U)); c->p |= I;
    c->pc = rd16(c, 0xFFFA); c->cycles += 7;
}
void cpu6502_irq(cpu6502_t *c) {
    if (c->p & I) return;
    push(c, (uint8_t)(c->pc >> 8)); push(c, (uint8_t)c->pc);
    push(c, (uint8_t)((c->p & ~B) | U)); c->p |= I;
    c->pc = rd16(c, 0xFFFE); c->cycles += 7;
}

/* addressing helpers set `ea` (effective address) and, for X/Y-indexed, page cross */
#define FETCH()  rd(c, c->pc++)
#define PGX(base) do { if (((base) & 0xFF00) != ((ea) & 0xFF00)) cyc++; } while (0)

int cpu6502_step(cpu6502_t *c) {
    if (c->jammed) return 0;
    uint8_t op = FETCH();
    c->last_opcode = op;
    uint16_t ea = 0; uint8_t m = 0; int cyc = 2;

    switch (op) {
    /* ---- loads ---- */
    case 0xA9: ea = c->pc++; c->a = rd(c, ea); setzn(c, c->a); cyc = 2; break;               /* LDA #  */
    case 0xA5: ea = FETCH(); c->a = rd(c, ea); setzn(c, c->a); cyc = 3; break;               /* LDA zp */
    case 0xB5: ea = (uint8_t)(FETCH() + c->x); c->a = rd(c, ea); setzn(c, c->a); cyc = 4; break;
    case 0xAD: ea = rd16(c, c->pc); c->pc += 2; c->a = rd(c, ea); setzn(c, c->a); cyc = 4; break;
    case 0xBD: { uint16_t b = rd16(c, c->pc); c->pc += 2; ea = (uint16_t)(b + c->x); c->a = rd(c, ea); setzn(c, c->a); cyc = 4; PGX(b); } break;
    case 0xB9: { uint16_t b = rd16(c, c->pc); c->pc += 2; ea = (uint16_t)(b + c->y); c->a = rd(c, ea); setzn(c, c->a); cyc = 4; PGX(b); } break;
    case 0xA1: { uint8_t zp = (uint8_t)(FETCH() + c->x); ea = rd16zp(c, zp); c->a = rd(c, ea); setzn(c, c->a); cyc = 6; } break;
    case 0xB1: { uint8_t zp = FETCH(); uint16_t b = rd16zp(c, zp); ea = (uint16_t)(b + c->y); c->a = rd(c, ea); setzn(c, c->a); cyc = 5; PGX(b); } break;

    case 0xA2: ea = c->pc++; c->x = rd(c, ea); setzn(c, c->x); cyc = 2; break;               /* LDX # */
    case 0xA6: ea = FETCH(); c->x = rd(c, ea); setzn(c, c->x); cyc = 3; break;
    case 0xB6: ea = (uint8_t)(FETCH() + c->y); c->x = rd(c, ea); setzn(c, c->x); cyc = 4; break;
    case 0xAE: ea = rd16(c, c->pc); c->pc += 2; c->x = rd(c, ea); setzn(c, c->x); cyc = 4; break;
    case 0xBE: { uint16_t b = rd16(c, c->pc); c->pc += 2; ea = (uint16_t)(b + c->y); c->x = rd(c, ea); setzn(c, c->x); cyc = 4; PGX(b); } break;

    case 0xA0: ea = c->pc++; c->y = rd(c, ea); setzn(c, c->y); cyc = 2; break;               /* LDY # */
    case 0xA4: ea = FETCH(); c->y = rd(c, ea); setzn(c, c->y); cyc = 3; break;
    case 0xB4: ea = (uint8_t)(FETCH() + c->x); c->y = rd(c, ea); setzn(c, c->y); cyc = 4; break;
    case 0xAC: ea = rd16(c, c->pc); c->pc += 2; c->y = rd(c, ea); setzn(c, c->y); cyc = 4; break;
    case 0xBC: { uint16_t b = rd16(c, c->pc); c->pc += 2; ea = (uint16_t)(b + c->x); c->y = rd(c, ea); setzn(c, c->y); cyc = 4; PGX(b); } break;

    /* ---- stores ---- */
    case 0x85: ea = FETCH(); wr(c, ea, c->a); cyc = 3; break;                                /* STA zp */
    case 0x95: ea = (uint8_t)(FETCH() + c->x); wr(c, ea, c->a); cyc = 4; break;
    case 0x8D: ea = rd16(c, c->pc); c->pc += 2; wr(c, ea, c->a); cyc = 4; break;
    case 0x9D: ea = (uint16_t)(rd16(c, c->pc) + c->x); c->pc += 2; wr(c, ea, c->a); cyc = 5; break;
    case 0x99: ea = (uint16_t)(rd16(c, c->pc) + c->y); c->pc += 2; wr(c, ea, c->a); cyc = 5; break;
    case 0x81: { uint8_t zp = (uint8_t)(FETCH() + c->x); ea = rd16zp(c, zp); wr(c, ea, c->a); cyc = 6; } break;
    case 0x91: { uint8_t zp = FETCH(); ea = (uint16_t)(rd16zp(c, zp) + c->y); wr(c, ea, c->a); cyc = 6; } break;
    case 0x86: ea = FETCH(); wr(c, ea, c->x); cyc = 3; break;                                /* STX */
    case 0x96: ea = (uint8_t)(FETCH() + c->y); wr(c, ea, c->x); cyc = 4; break;
    case 0x8E: ea = rd16(c, c->pc); c->pc += 2; wr(c, ea, c->x); cyc = 4; break;
    case 0x84: ea = FETCH(); wr(c, ea, c->y); cyc = 3; break;                                /* STY */
    case 0x94: ea = (uint8_t)(FETCH() + c->x); wr(c, ea, c->y); cyc = 4; break;
    case 0x8C: ea = rd16(c, c->pc); c->pc += 2; wr(c, ea, c->y); cyc = 4; break;

    /* ---- transfers ---- */
    case 0xAA: c->x = c->a; setzn(c, c->x); break;   /* TAX */
    case 0xA8: c->y = c->a; setzn(c, c->y); break;   /* TAY */
    case 0x8A: c->a = c->x; setzn(c, c->a); break;   /* TXA */
    case 0x98: c->a = c->y; setzn(c, c->a); break;   /* TYA */
    case 0xBA: c->x = c->sp; setzn(c, c->x); break;  /* TSX */
    case 0x9A: c->sp = c->x; break;                  /* TXS */

    /* ---- stack ---- */
    case 0x48: push(c, c->a); cyc = 3; break;                                   /* PHA */
    case 0x68: c->a = pull(c); setzn(c, c->a); cyc = 4; break;                   /* PLA */
    case 0x08: push(c, (uint8_t)(c->p | B | U)); cyc = 3; break;                 /* PHP */
    case 0x28: c->p = (uint8_t)((pull(c) & ~B) | U); cyc = 4; break;             /* PLP */

    /* ---- logic / arithmetic (grouped by base opcode) ---- */
    #define ALU_READ(v) do { \
        switch (op & 0x1C) { \
        case 0x08: ea = c->pc++; break;                                              /* # */ \
        case 0x04: ea = FETCH(); break;                                              /* zp */ \
        case 0x14: ea = (uint8_t)(FETCH() + c->x); break;                            /* zp,X */ \
        case 0x0C: ea = rd16(c, c->pc); c->pc += 2; break;                           /* abs */ \
        case 0x1C: { uint16_t b = rd16(c, c->pc); c->pc += 2; ea = (uint16_t)(b + c->x); PGX(b);} break; /* abs,X */ \
        case 0x18: { uint16_t b = rd16(c, c->pc); c->pc += 2; ea = (uint16_t)(b + c->y); PGX(b);} break; /* abs,Y */ \
        case 0x00: { uint8_t zp = (uint8_t)(FETCH() + c->x); ea = rd16zp(c, zp);} break;         /* (zp,X) */ \
        case 0x10: { uint8_t zp = FETCH(); uint16_t b = rd16zp(c, zp); ea = (uint16_t)(b + c->y); PGX(b);} break; /* (zp),Y */ \
        } v = rd(c, ea); cyc = 4; \
    } while (0)
    case 0x09: case 0x05: case 0x15: case 0x0D: case 0x1D: case 0x19: case 0x01: case 0x11:
        ALU_READ(m); c->a |= m; setzn(c, c->a); break;                              /* ORA */
    case 0x29: case 0x25: case 0x35: case 0x2D: case 0x3D: case 0x39: case 0x21: case 0x31:
        ALU_READ(m); c->a &= m; setzn(c, c->a); break;                              /* AND */
    case 0x49: case 0x45: case 0x55: case 0x4D: case 0x5D: case 0x59: case 0x41: case 0x51:
        ALU_READ(m); c->a ^= m; setzn(c, c->a); break;                              /* EOR */
    case 0x69: case 0x65: case 0x75: case 0x6D: case 0x7D: case 0x79: case 0x61: case 0x71:
        ALU_READ(m); adc(c, m); break;                                              /* ADC */
    case 0xE9: case 0xE5: case 0xF5: case 0xED: case 0xFD: case 0xF9: case 0xE1: case 0xF1:
        ALU_READ(m); sbc(c, m); break;                                              /* SBC */
    case 0xC9: case 0xC5: case 0xD5: case 0xCD: case 0xDD: case 0xD9: case 0xC1: case 0xD1:
        ALU_READ(m); cmp(c, c->a, m); break;                                        /* CMP */

    case 0xE0: ea = c->pc++; cmp(c, c->x, rd(c, ea)); break;                        /* CPX # */
    case 0xE4: ea = FETCH(); cmp(c, c->x, rd(c, ea)); cyc = 3; break;
    case 0xEC: ea = rd16(c, c->pc); c->pc += 2; cmp(c, c->x, rd(c, ea)); cyc = 4; break;
    case 0xC0: ea = c->pc++; cmp(c, c->y, rd(c, ea)); break;                        /* CPY # */
    case 0xC4: ea = FETCH(); cmp(c, c->y, rd(c, ea)); cyc = 3; break;
    case 0xCC: ea = rd16(c, c->pc); c->pc += 2; cmp(c, c->y, rd(c, ea)); cyc = 4; break;

    /* ---- BIT ---- */
    case 0x24: ea = FETCH(); m = rd(c, ea); setflag(c, Z, (c->a & m) == 0); setflag(c, N, m & 0x80); setflag(c, V, m & 0x40); cyc = 3; break;
    case 0x2C: ea = rd16(c, c->pc); c->pc += 2; m = rd(c, ea); setflag(c, Z, (c->a & m) == 0); setflag(c, N, m & 0x80); setflag(c, V, m & 0x40); cyc = 4; break;

    /* ---- inc / dec ---- */
    case 0xE6: ea = FETCH(); m = (uint8_t)(rd(c, ea) + 1); wr(c, ea, m); setzn(c, m); cyc = 5; break;        /* INC */
    case 0xF6: ea = (uint8_t)(FETCH() + c->x); m = (uint8_t)(rd(c, ea) + 1); wr(c, ea, m); setzn(c, m); cyc = 6; break;
    case 0xEE: ea = rd16(c, c->pc); c->pc += 2; m = (uint8_t)(rd(c, ea) + 1); wr(c, ea, m); setzn(c, m); cyc = 6; break;
    case 0xFE: ea = (uint16_t)(rd16(c, c->pc) + c->x); c->pc += 2; m = (uint8_t)(rd(c, ea) + 1); wr(c, ea, m); setzn(c, m); cyc = 7; break;
    case 0xC6: ea = FETCH(); m = (uint8_t)(rd(c, ea) - 1); wr(c, ea, m); setzn(c, m); cyc = 5; break;        /* DEC */
    case 0xD6: ea = (uint8_t)(FETCH() + c->x); m = (uint8_t)(rd(c, ea) - 1); wr(c, ea, m); setzn(c, m); cyc = 6; break;
    case 0xCE: ea = rd16(c, c->pc); c->pc += 2; m = (uint8_t)(rd(c, ea) - 1); wr(c, ea, m); setzn(c, m); cyc = 6; break;
    case 0xDE: ea = (uint16_t)(rd16(c, c->pc) + c->x); c->pc += 2; m = (uint8_t)(rd(c, ea) - 1); wr(c, ea, m); setzn(c, m); cyc = 7; break;
    case 0xE8: c->x++; setzn(c, c->x); break;   /* INX */
    case 0xCA: c->x--; setzn(c, c->x); break;   /* DEX */
    case 0xC8: c->y++; setzn(c, c->y); break;   /* INY */
    case 0x88: c->y--; setzn(c, c->y); break;   /* DEY */

    /* ---- shifts / rotates (accumulator + memory) ---- */
    case 0x0A: c->a = asl(c, c->a); break;                                                       /* ASL A */
    case 0x06: ea = FETCH(); wr(c, ea, asl(c, rd(c, ea))); cyc = 5; break;
    case 0x16: ea = (uint8_t)(FETCH() + c->x); wr(c, ea, asl(c, rd(c, ea))); cyc = 6; break;
    case 0x0E: ea = rd16(c, c->pc); c->pc += 2; wr(c, ea, asl(c, rd(c, ea))); cyc = 6; break;
    case 0x1E: ea = (uint16_t)(rd16(c, c->pc) + c->x); c->pc += 2; wr(c, ea, asl(c, rd(c, ea))); cyc = 7; break;
    case 0x4A: c->a = lsr(c, c->a); break;                                                       /* LSR A */
    case 0x46: ea = FETCH(); wr(c, ea, lsr(c, rd(c, ea))); cyc = 5; break;
    case 0x56: ea = (uint8_t)(FETCH() + c->x); wr(c, ea, lsr(c, rd(c, ea))); cyc = 6; break;
    case 0x4E: ea = rd16(c, c->pc); c->pc += 2; wr(c, ea, lsr(c, rd(c, ea))); cyc = 6; break;
    case 0x5E: ea = (uint16_t)(rd16(c, c->pc) + c->x); c->pc += 2; wr(c, ea, lsr(c, rd(c, ea))); cyc = 7; break;
    case 0x2A: c->a = rol(c, c->a); break;                                                       /* ROL A */
    case 0x26: ea = FETCH(); wr(c, ea, rol(c, rd(c, ea))); cyc = 5; break;
    case 0x36: ea = (uint8_t)(FETCH() + c->x); wr(c, ea, rol(c, rd(c, ea))); cyc = 6; break;
    case 0x2E: ea = rd16(c, c->pc); c->pc += 2; wr(c, ea, rol(c, rd(c, ea))); cyc = 6; break;
    case 0x3E: ea = (uint16_t)(rd16(c, c->pc) + c->x); c->pc += 2; wr(c, ea, rol(c, rd(c, ea))); cyc = 7; break;
    case 0x6A: c->a = ror(c, c->a); break;                                                       /* ROR A */
    case 0x66: ea = FETCH(); wr(c, ea, ror(c, rd(c, ea))); cyc = 5; break;
    case 0x76: ea = (uint8_t)(FETCH() + c->x); wr(c, ea, ror(c, rd(c, ea))); cyc = 6; break;
    case 0x6E: ea = rd16(c, c->pc); c->pc += 2; wr(c, ea, ror(c, rd(c, ea))); cyc = 6; break;
    case 0x7E: ea = (uint16_t)(rd16(c, c->pc) + c->x); c->pc += 2; wr(c, ea, ror(c, rd(c, ea))); cyc = 7; break;

    /* ---- branches ---- */
    case 0x10: { int8_t o = (int8_t)FETCH(); branch(c, !(c->p & N), o, &cyc); } break;   /* BPL */
    case 0x30: { int8_t o = (int8_t)FETCH(); branch(c,  (c->p & N), o, &cyc); } break;   /* BMI */
    case 0x50: { int8_t o = (int8_t)FETCH(); branch(c, !(c->p & V), o, &cyc); } break;   /* BVC */
    case 0x70: { int8_t o = (int8_t)FETCH(); branch(c,  (c->p & V), o, &cyc); } break;   /* BVS */
    case 0x90: { int8_t o = (int8_t)FETCH(); branch(c, !(c->p & C), o, &cyc); } break;   /* BCC */
    case 0xB0: { int8_t o = (int8_t)FETCH(); branch(c,  (c->p & C), o, &cyc); } break;   /* BCS */
    case 0xD0: { int8_t o = (int8_t)FETCH(); branch(c, !(c->p & Z), o, &cyc); } break;   /* BNE */
    case 0xF0: { int8_t o = (int8_t)FETCH(); branch(c,  (c->p & Z), o, &cyc); } break;   /* BEQ */

    /* ---- jumps / calls ---- */
    case 0x4C: c->pc = rd16(c, c->pc); cyc = 3; break;                                   /* JMP abs */
    case 0x6C: { uint16_t p = rd16(c, c->pc);                                            /* JMP (ind) w/ page bug */
                 uint16_t lo = rd(c, p);
                 uint16_t hi = rd(c, (uint16_t)((p & 0xFF00) | ((p + 1) & 0x00FF)));
                 c->pc = (uint16_t)(lo | (hi << 8)); cyc = 5; } break;
    case 0x20: { uint16_t t = rd16(c, c->pc); uint16_t ret = (uint16_t)(c->pc + 1);      /* JSR */
                 push(c, (uint8_t)(ret >> 8)); push(c, (uint8_t)ret); c->pc = t; cyc = 6; } break;
    case 0x60: { uint16_t lo = pull(c); uint16_t hi = pull(c); c->pc = (uint16_t)((lo | (hi << 8)) + 1); cyc = 6; } break; /* RTS */
    case 0x40: { c->p = (uint8_t)((pull(c) & ~B) | U); uint16_t lo = pull(c); uint16_t hi = pull(c); c->pc = (uint16_t)(lo | (hi << 8)); cyc = 6; } break; /* RTI */
    case 0x00: { c->pc++; push(c, (uint8_t)(c->pc >> 8)); push(c, (uint8_t)c->pc);       /* BRK */
                 push(c, (uint8_t)(c->p | B | U)); c->p |= I; c->pc = rd16(c, 0xFFFE); cyc = 7; } break;

    /* ---- flags ---- */
    case 0x18: c->p &= (uint8_t)~C; break;   /* CLC */
    case 0x38: c->p |= C; break;             /* SEC */
    case 0x58: c->p &= (uint8_t)~I; break;   /* CLI */
    case 0x78: c->p |= I; break;             /* SEI */
    case 0xB8: c->p &= (uint8_t)~V; break;   /* CLV */
    case 0xD8: c->p &= (uint8_t)~D; break;   /* CLD */
    case 0xF8: c->p |= D; break;             /* SED */

    case 0xEA: break;                        /* NOP */

    default:
        /* Undocumented/illegal opcode. For dataset stress we keep executing as a
         * 1-byte NOP but flag it, so the Game Master can log illegal-opcode
         * density per ROM rather than jamming on the first stray byte. */
        c->jammed = 0;
        break;
    }

    c->cycles += (uint64_t)cyc;
    return cyc;
}

uint64_t cpu6502_run(cpu6502_t *c, uint64_t max_cycles) {
    uint64_t start = c->cycles;
    while (c->cycles - start < max_cycles && !c->jammed) {
        if (cpu6502_step(c) == 0) break;
    }
    return c->cycles - start;
}
