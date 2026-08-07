/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* cpu_z80.c — Zilog Z80 core (unprefixed 8-bit ISA). See cpu_z80.h for scope.
 * Integer-only, freestanding. Written for correctness of the *flag model* above
 * all else, because the flags are exactly what makes the Z80 non-compatible
 * with the 6502 — and the relationship between the two flag worlds is what the
 * M5 event space measures. */
#include "cpu_z80.h"

static inline uint8_t  rd(cpu_z80_t *c, uint16_t a)            { return c->read(c, a); }
static inline void     wr(cpu_z80_t *c, uint16_t a, uint8_t v) { c->write(c, a, v); }
static inline uint8_t  fetch(cpu_z80_t *c)                     { return rd(c, c->pc++); }
static inline uint16_t fetch16(cpu_z80_t *c) { uint8_t lo = fetch(c); uint8_t hi = fetch(c); return (uint16_t)(lo | (hi << 8)); }

/* 16-bit register pair accessors. */
static inline uint16_t BC(cpu_z80_t *c) { return (uint16_t)((c->b << 8) | c->c); }
static inline uint16_t DE(cpu_z80_t *c) { return (uint16_t)((c->d << 8) | c->e); }
static inline uint16_t HL(cpu_z80_t *c) { return (uint16_t)((c->h << 8) | c->l); }
static inline void setBC(cpu_z80_t *c, uint16_t v) { c->b = (uint8_t)(v >> 8); c->c = (uint8_t)v; }
static inline void setDE(cpu_z80_t *c, uint16_t v) { c->d = (uint8_t)(v >> 8); c->e = (uint8_t)v; }
static inline void setHL(cpu_z80_t *c, uint16_t v) { c->h = (uint8_t)(v >> 8); c->l = (uint8_t)v; }

static inline void push16(cpu_z80_t *c, uint16_t v) {
    c->sp--; wr(c, c->sp, (uint8_t)(v >> 8));
    c->sp--; wr(c, c->sp, (uint8_t)v);
}
static inline uint16_t pop16(cpu_z80_t *c) {
    uint8_t lo = rd(c, c->sp); c->sp++;
    uint8_t hi = rd(c, c->sp); c->sp++;
    return (uint16_t)(lo | (hi << 8));
}

static inline int parity8(uint8_t v) {
    v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
    return (v & 1) == 0;   /* even parity -> PV set */
}
static inline void set_szp(cpu_z80_t *c, uint8_t r) {
    uint8_t f = 0;
    if (r & 0x80) f |= Z80_S;
    if (r == 0)   f |= Z80_Z;
    f |= (r & (Z80_F5 | Z80_F3));
    if (parity8(r)) f |= Z80_PV;
    c->f = f;   /* H=0, N=0, C=0 for logic ops (caller adjusts) */
}

/* ---- 8-bit ALU on the accumulator ---- */
static void alu_add(cpu_z80_t *c, uint8_t v, int carry) {
    int cin = carry ? (c->f & Z80_C ? 1 : 0) : 0;
    unsigned r = (unsigned)c->a + v + cin;
    uint8_t res = (uint8_t)r;
    uint8_t f = 0;
    if (res & 0x80) f |= Z80_S;
    if (res == 0)   f |= Z80_Z;
    f |= (res & (Z80_F5 | Z80_F3));
    if (((c->a & 0xF) + (v & 0xF) + cin) > 0xF) f |= Z80_H;
    if ((~(c->a ^ v) & (c->a ^ res) & 0x80)) f |= Z80_PV;
    if (r > 0xFF) f |= Z80_C;
    c->f = f; c->a = res;
}
static void alu_sub_core(cpu_z80_t *c, uint8_t v, int carry, int store) {
    int cin = carry ? (c->f & Z80_C ? 1 : 0) : 0;
    int r = (int)c->a - v - cin;
    uint8_t res = (uint8_t)r;
    uint8_t f = Z80_N;
    if (res & 0x80) f |= Z80_S;
    if (res == 0)   f |= Z80_Z;
    f |= (res & (Z80_F5 | Z80_F3));
    if (((c->a & 0xF) - (v & 0xF) - cin) < 0) f |= Z80_H;
    if (((c->a ^ v) & (c->a ^ res) & 0x80)) f |= Z80_PV;
    if (r < 0) f |= Z80_C;
    c->f = f;
    if (store) c->a = res;
}
static inline void alu_sub(cpu_z80_t *c, uint8_t v, int carry) { alu_sub_core(c, v, carry, 1); }
static inline void alu_cp (cpu_z80_t *c, uint8_t v)            { alu_sub_core(c, v, 0, 0); }

static void alu_and(cpu_z80_t *c, uint8_t v) { c->a &= v; set_szp(c, c->a); c->f |= Z80_H; }
static void alu_xor(cpu_z80_t *c, uint8_t v) { c->a ^= v; set_szp(c, c->a); }
static void alu_or (cpu_z80_t *c, uint8_t v) { c->a |= v; set_szp(c, c->a); }

static uint8_t alu_inc(cpu_z80_t *c, uint8_t v) {
    uint8_t r = (uint8_t)(v + 1);
    uint8_t keepC = c->f & Z80_C;
    uint8_t f = keepC;
    if (r & 0x80) f |= Z80_S;
    if (r == 0)   f |= Z80_Z;
    f |= (r & (Z80_F5 | Z80_F3));
    if ((v & 0xF) == 0xF) f |= Z80_H;
    if (v == 0x7F) f |= Z80_PV;
    c->f = f;
    return r;
}
static uint8_t alu_dec(cpu_z80_t *c, uint8_t v) {
    uint8_t r = (uint8_t)(v - 1);
    uint8_t keepC = c->f & Z80_C;
    uint8_t f = keepC | Z80_N;
    if (r & 0x80) f |= Z80_S;
    if (r == 0)   f |= Z80_Z;
    f |= (r & (Z80_F5 | Z80_F3));
    if ((v & 0xF) == 0x00) f |= Z80_H;
    if (v == 0x80) f |= Z80_PV;
    c->f = f;
    return r;
}
static void add_hl(cpu_z80_t *c, uint16_t v) {
    uint16_t hl = HL(c);
    unsigned r = (unsigned)hl + v;
    uint8_t f = c->f & (Z80_S | Z80_Z | Z80_PV);   /* S,Z,PV preserved */
    if (((hl & 0x0FFF) + (v & 0x0FFF)) > 0x0FFF) f |= Z80_H;
    if (r > 0xFFFF) f |= Z80_C;
    f |= ((r >> 8) & (Z80_F5 | Z80_F3));
    c->f = f;
    setHL(c, (uint16_t)r);
}

/* ---- register selector for the LD r,r' / ALU r blocks (bits 2..0 / 5..3) ---- */
static uint8_t reg_get(cpu_z80_t *c, int idx) {
    switch (idx & 7) {
        case 0: return c->b; case 1: return c->c; case 2: return c->d; case 3: return c->e;
        case 4: return c->h; case 5: return c->l; case 6: return rd(c, HL(c)); default: return c->a;
    }
}
static void reg_set(cpu_z80_t *c, int idx, uint8_t v) {
    switch (idx & 7) {
        case 0: c->b = v; break; case 1: c->c = v; break; case 2: c->d = v; break; case 3: c->e = v; break;
        case 4: c->h = v; break; case 5: c->l = v; break; case 6: wr(c, HL(c), v); break; default: c->a = v; break;
    }
}

static int cond_met(cpu_z80_t *c, int cc) {
    switch (cc & 7) {
        case 0: return !(c->f & Z80_Z);   /* NZ */
        case 1: return  (c->f & Z80_Z);   /* Z  */
        case 2: return !(c->f & Z80_C);   /* NC */
        case 3: return  (c->f & Z80_C);   /* C  */
        case 4: return !(c->f & Z80_PV);  /* PO */
        case 5: return  (c->f & Z80_PV);  /* PE */
        case 6: return !(c->f & Z80_S);   /* P  */
        default:return  (c->f & Z80_S);   /* M  */
    }
}

void cpu_z80_reset(cpu_z80_t *c) {
    c->a = c->f = c->b = c->c = c->d = c->e = c->h = c->l = 0;
    c->sp = 0xFFFF; c->pc = 0;
    c->iff1 = c->iff2 = 0; c->halted = 0;
    c->cycles = 0; c->jammed = 0; c->last_opcode = 0; c->illegal = 0;
}

int cpu_z80_step(cpu_z80_t *c) {
    if (c->jammed) return 0;
    if (c->halted) { c->cycles += 4; return 4; }
    uint8_t op = fetch(c);
    c->last_opcode = op;
    int cyc = 4;

    /* LD r,r' block: 0x40..0x7F (0x76 = HALT). */
    if (op >= 0x40 && op <= 0x7F) {
        if (op == 0x76) { c->halted = 1; c->cycles += 4; return 4; }
        int dst = (op >> 3) & 7, src = op & 7;
        reg_set(c, dst, reg_get(c, src));
        cyc = (dst == 6 || src == 6) ? 7 : 4;
        c->cycles += cyc; return cyc;
    }
    /* ALU A,r block: 0x80..0xBF. */
    if (op >= 0x80 && op <= 0xBF) {
        uint8_t v = reg_get(c, op & 7);
        int grp = (op >> 3) & 7;
        switch (grp) {
            case 0: alu_add(c, v, 0); break; case 1: alu_add(c, v, 1); break;
            case 2: alu_sub(c, v, 0); break; case 3: alu_sub(c, v, 1); break;
            case 4: alu_and(c, v);    break; case 5: alu_xor(c, v);    break;
            case 6: alu_or(c, v);     break; default: alu_cp(c, v);    break;
        }
        cyc = ((op & 7) == 6) ? 7 : 4;
        c->cycles += cyc; return cyc;
    }

    switch (op) {
        case 0x00: cyc = 4; break;                                         /* NOP        */
        case 0x01: setBC(c, fetch16(c)); cyc = 10; break;                  /* LD BC,nn   */
        case 0x11: setDE(c, fetch16(c)); cyc = 10; break;                  /* LD DE,nn   */
        case 0x21: setHL(c, fetch16(c)); cyc = 10; break;                  /* LD HL,nn   */
        case 0x31: c->sp = fetch16(c);   cyc = 10; break;                  /* LD SP,nn   */
        case 0x02: wr(c, BC(c), c->a); cyc = 7; break;                     /* LD (BC),A  */
        case 0x12: wr(c, DE(c), c->a); cyc = 7; break;                     /* LD (DE),A  */
        case 0x0A: c->a = rd(c, BC(c)); cyc = 7; break;                    /* LD A,(BC)  */
        case 0x1A: c->a = rd(c, DE(c)); cyc = 7; break;                    /* LD A,(DE)  */
        case 0x22: { uint16_t a = fetch16(c); wr(c, a, c->l); wr(c, a+1, c->h); cyc = 16; } break; /* LD (nn),HL */
        case 0x2A: { uint16_t a = fetch16(c); c->l = rd(c, a); c->h = rd(c, a+1); cyc = 16; } break; /* LD HL,(nn) */
        case 0x32: { uint16_t a = fetch16(c); wr(c, a, c->a); cyc = 13; } break;  /* LD (nn),A */
        case 0x3A: { uint16_t a = fetch16(c); c->a = rd(c, a); cyc = 13; } break; /* LD A,(nn) */
        case 0x06: c->b = fetch(c); cyc = 7; break;                        /* LD B,n */
        case 0x0E: c->c = fetch(c); cyc = 7; break;                        /* LD C,n */
        case 0x16: c->d = fetch(c); cyc = 7; break;                        /* LD D,n */
        case 0x1E: c->e = fetch(c); cyc = 7; break;                        /* LD E,n */
        case 0x26: c->h = fetch(c); cyc = 7; break;                        /* LD H,n */
        case 0x2E: c->l = fetch(c); cyc = 7; break;                        /* LD L,n */
        case 0x3E: c->a = fetch(c); cyc = 7; break;                        /* LD A,n */
        case 0x36: { uint8_t n = fetch(c); wr(c, HL(c), n); cyc = 10; } break; /* LD (HL),n */
        case 0x03: setBC(c, BC(c)+1); cyc = 6; break;                      /* INC BC */
        case 0x13: setDE(c, DE(c)+1); cyc = 6; break;                      /* INC DE */
        case 0x23: setHL(c, HL(c)+1); cyc = 6; break;                      /* INC HL */
        case 0x33: c->sp++; cyc = 6; break;                               /* INC SP */
        case 0x0B: setBC(c, BC(c)-1); cyc = 6; break;                      /* DEC BC */
        case 0x1B: setDE(c, DE(c)-1); cyc = 6; break;                      /* DEC DE */
        case 0x2B: setHL(c, HL(c)-1); cyc = 6; break;                      /* DEC HL */
        case 0x3B: c->sp--; cyc = 6; break;                               /* DEC SP */
        case 0x04: c->b = alu_inc(c, c->b); cyc = 4; break;                /* INC B */
        case 0x0C: c->c = alu_inc(c, c->c); cyc = 4; break;                /* INC C */
        case 0x14: c->d = alu_inc(c, c->d); cyc = 4; break;                /* INC D */
        case 0x1C: c->e = alu_inc(c, c->e); cyc = 4; break;                /* INC E */
        case 0x24: c->h = alu_inc(c, c->h); cyc = 4; break;                /* INC H */
        case 0x2C: c->l = alu_inc(c, c->l); cyc = 4; break;                /* INC L */
        case 0x3C: c->a = alu_inc(c, c->a); cyc = 4; break;                /* INC A */
        case 0x34: { uint8_t v = alu_inc(c, rd(c, HL(c))); wr(c, HL(c), v); cyc = 11; } break; /* INC (HL) */
        case 0x05: c->b = alu_dec(c, c->b); cyc = 4; break;                /* DEC B */
        case 0x0D: c->c = alu_dec(c, c->c); cyc = 4; break;                /* DEC C */
        case 0x15: c->d = alu_dec(c, c->d); cyc = 4; break;                /* DEC D */
        case 0x1D: c->e = alu_dec(c, c->e); cyc = 4; break;                /* DEC E */
        case 0x25: c->h = alu_dec(c, c->h); cyc = 4; break;                /* DEC H */
        case 0x2D: c->l = alu_dec(c, c->l); cyc = 4; break;                /* DEC L */
        case 0x3D: c->a = alu_dec(c, c->a); cyc = 4; break;                /* DEC A */
        case 0x35: { uint8_t v = alu_dec(c, rd(c, HL(c))); wr(c, HL(c), v); cyc = 11; } break; /* DEC (HL) */
        case 0x09: add_hl(c, BC(c)); cyc = 11; break;                      /* ADD HL,BC */
        case 0x19: add_hl(c, DE(c)); cyc = 11; break;                      /* ADD HL,DE */
        case 0x29: add_hl(c, HL(c)); cyc = 11; break;                      /* ADD HL,HL */
        case 0x39: add_hl(c, c->sp); cyc = 11; break;                      /* ADD HL,SP */
        case 0xC6: alu_add(c, fetch(c), 0); cyc = 7; break;                /* ADD A,n */
        case 0xCE: alu_add(c, fetch(c), 1); cyc = 7; break;                /* ADC A,n */
        case 0xD6: alu_sub(c, fetch(c), 0); cyc = 7; break;                /* SUB n   */
        case 0xDE: alu_sub(c, fetch(c), 1); cyc = 7; break;                /* SBC A,n */
        case 0xE6: alu_and(c, fetch(c)); cyc = 7; break;                   /* AND n   */
        case 0xEE: alu_xor(c, fetch(c)); cyc = 7; break;                   /* XOR n   */
        case 0xF6: alu_or(c, fetch(c)); cyc = 7; break;                    /* OR n    */
        case 0xFE: alu_cp(c, fetch(c)); cyc = 7; break;                    /* CP n    */
        case 0x07: { /* RLCA */ uint8_t a = c->a; uint8_t co = a >> 7; c->a = (uint8_t)((a << 1) | co);
                     c->f = (c->f & (Z80_S|Z80_Z|Z80_PV)) | (co ? Z80_C : 0) | (c->a & (Z80_F5|Z80_F3)); cyc = 4; } break;
        case 0x0F: { /* RRCA */ uint8_t a = c->a; uint8_t co = a & 1; c->a = (uint8_t)((a >> 1) | (co << 7));
                     c->f = (c->f & (Z80_S|Z80_Z|Z80_PV)) | (co ? Z80_C : 0) | (c->a & (Z80_F5|Z80_F3)); cyc = 4; } break;
        case 0x17: { /* RLA */ uint8_t a = c->a; uint8_t ci = (c->f & Z80_C) ? 1 : 0; uint8_t co = a >> 7;
                     c->a = (uint8_t)((a << 1) | ci);
                     c->f = (c->f & (Z80_S|Z80_Z|Z80_PV)) | (co ? Z80_C : 0) | (c->a & (Z80_F5|Z80_F3)); cyc = 4; } break;
        case 0x1F: { /* RRA */ uint8_t a = c->a; uint8_t ci = (c->f & Z80_C) ? 1 : 0; uint8_t co = a & 1;
                     c->a = (uint8_t)((a >> 1) | (ci << 7));
                     c->f = (c->f & (Z80_S|Z80_Z|Z80_PV)) | (co ? Z80_C : 0) | (c->a & (Z80_F5|Z80_F3)); cyc = 4; } break;
        case 0x2F: c->a = (uint8_t)~c->a; c->f |= (Z80_H | Z80_N); c->f = (c->f & ~(Z80_F5|Z80_F3)) | (c->a & (Z80_F5|Z80_F3)); cyc = 4; break; /* CPL */
        case 0x37: c->f = (c->f & (Z80_S|Z80_Z|Z80_PV)) | Z80_C | (c->a & (Z80_F5|Z80_F3)); cyc = 4; break; /* SCF */
        case 0x3F: { uint8_t cf = c->f & Z80_C; c->f = (c->f & (Z80_S|Z80_Z|Z80_PV)) | (cf ? Z80_H : Z80_C) | (c->a & (Z80_F5|Z80_F3)); cyc = 4; } break; /* CCF */
        case 0xEB: { uint16_t t = DE(c); setDE(c, HL(c)); setHL(c, t); cyc = 4; } break; /* EX DE,HL */
        case 0xC3: c->pc = fetch16(c); cyc = 10; break;                    /* JP nn */
        case 0xC2: case 0xCA: case 0xD2: case 0xDA:
        case 0xE2: case 0xEA: case 0xF2: case 0xFA: {                       /* JP cc,nn */
            uint16_t a = fetch16(c); if (cond_met(c, (op >> 3) & 7)) c->pc = a; cyc = 10; } break;
        case 0x18: { int8_t e = (int8_t)fetch(c); c->pc = (uint16_t)(c->pc + e); cyc = 12; } break; /* JR e */
        case 0x20: case 0x28: case 0x30: case 0x38: {                      /* JR cc,e (NZ/Z/NC/C) */
            int8_t e = (int8_t)fetch(c);
            int cc = ((op >> 3) & 3);   /* 0=NZ,1=Z,2=NC,3=C -> map to cond_met codes 0..3 */
            if (cond_met(c, cc)) { c->pc = (uint16_t)(c->pc + e); cyc = 12; } else cyc = 7; } break;
        case 0x10: { int8_t e = (int8_t)fetch(c); c->b--; if (c->b != 0) { c->pc = (uint16_t)(c->pc + e); cyc = 13; } else cyc = 8; } break; /* DJNZ */
        case 0xCD: { uint16_t a = fetch16(c); push16(c, c->pc); c->pc = a; cyc = 17; } break; /* CALL nn */
        case 0xC4: case 0xCC: case 0xD4: case 0xDC:
        case 0xE4: case 0xEC: case 0xF4: case 0xFC: {                       /* CALL cc,nn */
            uint16_t a = fetch16(c); if (cond_met(c, (op >> 3) & 7)) { push16(c, c->pc); c->pc = a; cyc = 17; } else cyc = 10; } break;
        case 0xC9: c->pc = pop16(c); cyc = 10; break;                      /* RET */
        case 0xC0: case 0xC8: case 0xD0: case 0xD8:
        case 0xE0: case 0xE8: case 0xF0: case 0xF8:                         /* RET cc */
            if (cond_met(c, (op >> 3) & 7)) { c->pc = pop16(c); cyc = 11; } else cyc = 5; break;
        case 0xC5: push16(c, BC(c)); cyc = 11; break;                      /* PUSH BC */
        case 0xD5: push16(c, DE(c)); cyc = 11; break;                      /* PUSH DE */
        case 0xE5: push16(c, HL(c)); cyc = 11; break;                      /* PUSH HL */
        case 0xF5: push16(c, (uint16_t)((c->a << 8) | c->f)); cyc = 11; break; /* PUSH AF */
        case 0xC1: setBC(c, pop16(c)); cyc = 10; break;                    /* POP BC */
        case 0xD1: setDE(c, pop16(c)); cyc = 10; break;                    /* POP DE */
        case 0xE1: setHL(c, pop16(c)); cyc = 10; break;                    /* POP HL */
        case 0xF1: { uint16_t v = pop16(c); c->a = (uint8_t)(v >> 8); c->f = (uint8_t)v; cyc = 10; } break; /* POP AF */
        case 0xF3: c->iff1 = c->iff2 = 0; cyc = 4; break;                  /* DI */
        case 0xFB: c->iff1 = c->iff2 = 1; cyc = 4; break;                  /* EI */
        case 0xE9: c->pc = HL(c); cyc = 4; break;                          /* JP (HL) */
        case 0xF9: c->sp = HL(c); cyc = 6; break;                          /* LD SP,HL */
        /* Prefix pages we don't decode yet: flag + treat as NOP so the Game
         * Master logs the gap rather than executing garbage. */
        case 0xCB: case 0xED: case 0xDD: case 0xFD:
            (void)fetch(c); c->illegal++; cyc = 8; break;
        default:
            c->illegal++; cyc = 4; break;   /* any remaining unimplemented opcode */
    }
    c->cycles += cyc;
    return cyc;
}

uint64_t cpu_z80_run(cpu_z80_t *c, uint64_t max_cycles) {
    uint64_t start = c->cycles;
    while (c->cycles - start < max_cycles && !c->jammed && !c->halted)
        if (cpu_z80_step(c) == 0) break;
    return c->cycles - start;
}
