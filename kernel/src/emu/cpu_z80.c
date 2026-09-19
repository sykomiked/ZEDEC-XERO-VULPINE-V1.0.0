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

/* ---- port I/O wrappers ---- */
static inline uint8_t z80_in(cpu_z80_t *c, uint16_t port){ return c->in ? c->in(c, port) : 0xFFu; }
static inline void    z80_out(cpu_z80_t *c, uint16_t port, uint8_t v){ if (c->out) c->out(c, port, v); }

/* set S Z 5 3 P flags from a result, plus an explicit carry (for rotates/shifts) */
static uint8_t z80_szpc(cpu_z80_t *c, uint8_t r, int carry){
    uint8_t f = 0;
    if (r & 0x80) f |= Z80_S;
    if (r == 0)   f |= Z80_Z;
    f |= (r & (Z80_F5 | Z80_F3));
    if (parity8(r)) f |= Z80_PV;
    if (carry) f |= Z80_C;
    c->f = f; return r;
}

/* ---- CB prefix: rotates/shifts + BIT/RES/SET ---- */
static void do_cb(cpu_z80_t *c){
    uint8_t op = fetch(c);
    int reg = op & 7, y = (op >> 3) & 7, x = op >> 6;
    uint8_t v = reg_get(c, reg), r = v, co = 0;
    if (x == 0){
        switch (y){
            case 0: co = v >> 7; r = (uint8_t)((v << 1) | co); break;            /* RLC */
            case 1: co = v & 1;  r = (uint8_t)((v >> 1) | (co << 7)); break;      /* RRC */
            case 2: co = v >> 7; r = (uint8_t)((v << 1) | ((c->f & Z80_C)?1:0)); break; /* RL */
            case 3: co = v & 1;  r = (uint8_t)((v >> 1) | (((c->f & Z80_C)?1:0) << 7)); break; /* RR */
            case 4: co = v >> 7; r = (uint8_t)(v << 1); break;                    /* SLA */
            case 5: co = v & 1;  r = (uint8_t)((v >> 1) | (v & 0x80)); break;     /* SRA */
            case 6: co = v >> 7; r = (uint8_t)((v << 1) | 1); break;              /* SLL (undoc) */
            default:co = v & 1;  r = (uint8_t)(v >> 1); break;                    /* SRL */
        }
        z80_szpc(c, r, co); reg_set(c, reg, r);
    } else if (x == 1){                                                          /* BIT b,r */
        uint8_t bit = v & (uint8_t)(1u << y);
        uint8_t f = (uint8_t)(c->f & Z80_C) | Z80_H;
        if (bit == 0) f |= (Z80_Z | Z80_PV);
        if (y == 7 && bit) f |= Z80_S;
        f |= (v & (Z80_F5 | Z80_F3));
        c->f = f;
    } else if (x == 2){                                                          /* RES */
        reg_set(c, reg, (uint8_t)(v & ~(1u << y)));
    } else {                                                                     /* SET */
        reg_set(c, reg, (uint8_t)(v | (1u << y)));
    }
}

/* 16-bit ADC/SBC HL,dd */
static void adc_hl(cpu_z80_t *c, uint16_t v){
    uint16_t hl = HL(c); int cf = (c->f & Z80_C) ? 1 : 0;
    unsigned r = (unsigned)hl + v + cf; uint8_t f = 0;
    if ((r & 0xFFFF) == 0) f |= Z80_Z; if (r & 0x8000) f |= Z80_S;
    if (((hl & 0x0FFF) + (v & 0x0FFF) + cf) > 0x0FFF) f |= Z80_H;
    if (r > 0xFFFF) f |= Z80_C;
    if ((~(hl ^ v) & (hl ^ r) & 0x8000)) f |= Z80_PV;
    f |= (uint8_t)((r >> 8) & (Z80_F5 | Z80_F3));
    c->f = f; setHL(c, (uint16_t)r);
}
static void sbc_hl(cpu_z80_t *c, uint16_t v){
    uint16_t hl = HL(c); int cf = (c->f & Z80_C) ? 1 : 0;
    int r = (int)hl - v - cf; uint8_t f = Z80_N;
    if ((r & 0xFFFF) == 0) f |= Z80_Z; if (r & 0x8000) f |= Z80_S;
    if (((hl & 0x0FFF) - (v & 0x0FFF) - cf) < 0) f |= Z80_H;
    if (r < 0) f |= Z80_C;
    if (((hl ^ v) & (hl ^ (unsigned)r) & 0x8000)) f |= Z80_PV;
    f |= (uint8_t)(((unsigned)r >> 8) & (Z80_F5 | Z80_F3));
    c->f = f; setHL(c, (uint16_t)r);
}
static void ld_a_ir(cpu_z80_t *c, uint8_t v){   /* LD A,I / LD A,R flags */
    uint8_t f = (uint8_t)(c->f & Z80_C);
    if (v & 0x80) f |= Z80_S; if (v == 0) f |= Z80_Z;
    f |= (v & (Z80_F5 | Z80_F3));
    if (c->iff2) f |= Z80_PV;
    c->f = f; c->a = v;
}
/* block move LDI/LDD/LDIR/LDDR */
static int block_ld(cpu_z80_t *c, int dir, int repeat){
    uint8_t v = rd(c, HL(c)); wr(c, DE(c), v);
    setHL(c, (uint16_t)(HL(c) + dir)); setDE(c, (uint16_t)(DE(c) + dir));
    setBC(c, (uint16_t)(BC(c) - 1));
    c->f = (uint8_t)(c->f & (Z80_S | Z80_Z | Z80_C)) | (BC(c) != 0 ? Z80_PV : 0);
    if (repeat && BC(c) != 0){ c->pc -= 2; return 21; }   /* re-execute ED xx */
    return 16;
}

/* block I/O: OUTI/OUTD/OTIR/OTDR and INI/IND/INIR/INDR */
static int block_out(cpu_z80_t *c, int dir, int repeat){
    uint8_t v = rd(c, HL(c));
    c->b--;
    z80_out(c, BC(c), v);
    setHL(c, (uint16_t)(HL(c) + dir));
    c->f = (uint8_t)(c->f & Z80_C) | Z80_N | (c->b == 0 ? Z80_Z : 0);
    if (repeat && c->b != 0){ c->pc -= 2; return 21; }
    return 16;
}
static int block_in(cpu_z80_t *c, int dir, int repeat){
    uint8_t v = z80_in(c, BC(c));
    wr(c, HL(c), v);
    c->b--;
    setHL(c, (uint16_t)(HL(c) + dir));
    c->f = (uint8_t)(c->f & Z80_C) | Z80_N | (c->b == 0 ? Z80_Z : 0);
    if (repeat && c->b != 0){ c->pc -= 2; return 21; }
    return 16;
}

/* ---- ED prefix ---- */
static int do_ed(cpu_z80_t *c){
    uint8_t op = fetch(c);
    switch (op){
        case 0x46: case 0x66: c->im = 0; return 8;
        case 0x56: case 0x76: c->im = 1; return 8;
        case 0x5E: case 0x7E: c->im = 2; return 8;
        case 0x47: c->i = c->a; return 9;                 /* LD I,A */
        case 0x4F: c->r = c->a; return 9;                 /* LD R,A */
        case 0x57: ld_a_ir(c, c->i); return 9;            /* LD A,I */
        case 0x5F: ld_a_ir(c, c->r); return 9;            /* LD A,R */
        case 0x4D: c->pc = pop16(c); c->iff1 = c->iff2; return 14;  /* RETI */
        case 0x45: case 0x55: case 0x65: case 0x75:
                   c->pc = pop16(c); c->iff1 = c->iff2; return 14;  /* RETN */
        case 0x44: case 0x54: case 0x64: case 0x74: {      /* NEG */
                   uint8_t a = c->a; c->a = 0; alu_sub(c, a, 0); return 8; }
        case 0x4A: adc_hl(c, BC(c)); return 15; case 0x5A: adc_hl(c, DE(c)); return 15;
        case 0x6A: adc_hl(c, HL(c)); return 15; case 0x7A: adc_hl(c, c->sp); return 15;
        case 0x42: sbc_hl(c, BC(c)); return 15; case 0x52: sbc_hl(c, DE(c)); return 15;
        case 0x62: sbc_hl(c, HL(c)); return 15; case 0x72: sbc_hl(c, c->sp); return 15;
        case 0x43: { uint16_t a = fetch16(c); wr(c,a,c->c); wr(c,a+1,c->b); return 20; } /* LD (nn),BC */
        case 0x53: { uint16_t a = fetch16(c); wr(c,a,c->e); wr(c,a+1,c->d); return 20; }
        case 0x63: { uint16_t a = fetch16(c); wr(c,a,c->l); wr(c,a+1,c->h); return 20; }
        case 0x73: { uint16_t a = fetch16(c); wr(c,a,(uint8_t)c->sp); wr(c,a+1,(uint8_t)(c->sp>>8)); return 20; }
        case 0x4B: { uint16_t a = fetch16(c); setBC(c,(uint16_t)(rd(c,a)|(rd(c,a+1)<<8))); return 20; }
        case 0x5B: { uint16_t a = fetch16(c); setDE(c,(uint16_t)(rd(c,a)|(rd(c,a+1)<<8))); return 20; }
        case 0x6B: { uint16_t a = fetch16(c); setHL(c,(uint16_t)(rd(c,a)|(rd(c,a+1)<<8))); return 20; }
        case 0x7B: { uint16_t a = fetch16(c); c->sp = (uint16_t)(rd(c,a)|(rd(c,a+1)<<8)); return 20; }
        case 0xA0: return block_ld(c, +1, 0);             /* LDI  */
        case 0xA8: return block_ld(c, -1, 0);             /* LDD  */
        case 0xB0: return block_ld(c, +1, 1);             /* LDIR */
        case 0xB8: return block_ld(c, -1, 1);             /* LDDR */
        case 0xA3: return block_out(c, +1, 0);            /* OUTI */
        case 0xAB: return block_out(c, -1, 0);            /* OUTD */
        case 0xB3: return block_out(c, +1, 1);            /* OTIR */
        case 0xBB: return block_out(c, -1, 1);            /* OTDR */
        case 0xA2: return block_in(c, +1, 0);             /* INI  */
        case 0xAA: return block_in(c, -1, 0);             /* IND  */
        case 0xB2: return block_in(c, +1, 1);             /* INIR */
        case 0xBA: return block_in(c, -1, 1);             /* INDR */
        case 0x40: case 0x48: case 0x50: case 0x58: case 0x60: case 0x68: case 0x78: {
                   uint8_t v = z80_in(c, BC(c)); int reg = (op >> 3) & 7;
                   if (reg != 6) reg_set(c, reg, v); z80_szpc(c, v, (c->f & Z80_C) ? 1 : 0); return 12; }
        case 0x41: case 0x49: case 0x51: case 0x59: case 0x61: case 0x69: case 0x79: {
                   int reg = (op >> 3) & 7; uint8_t v = (reg == 6) ? 0 : reg_get(c, reg);
                   z80_out(c, BC(c), v); return 12; }
        default: c->illegal++; return 8;
    }
}

/* 16-bit ADD IX/IY,dd */
static void idx_add(cpu_z80_t *c, uint16_t *idx, uint16_t v){
    uint16_t hl = *idx; unsigned r = (unsigned)hl + v;
    uint8_t f = (uint8_t)(c->f & (Z80_S | Z80_Z | Z80_PV));
    if (((hl & 0x0FFF) + (v & 0x0FFF)) > 0x0FFF) f |= Z80_H;
    if (r > 0xFFFF) f |= Z80_C;
    f |= (uint8_t)((r >> 8) & (Z80_F5 | Z80_F3));
    c->f = f; *idx = (uint16_t)r;
}
/* DDCB / FDCB: rotate/shift/bit/res/set on the byte at (IX+d) */
static void do_ddcb(cpu_z80_t *c, uint16_t a, uint8_t op){
    int y = (op >> 3) & 7, x = op >> 6;
    uint8_t v = rd(c, a), r = v, co = 0;
    if (x == 0){
        switch (y){
            case 0: co = v >> 7; r = (uint8_t)((v << 1) | co); break;
            case 1: co = v & 1;  r = (uint8_t)((v >> 1) | (co << 7)); break;
            case 2: co = v >> 7; r = (uint8_t)((v << 1) | ((c->f & Z80_C)?1:0)); break;
            case 3: co = v & 1;  r = (uint8_t)((v >> 1) | (((c->f & Z80_C)?1:0) << 7)); break;
            case 4: co = v >> 7; r = (uint8_t)(v << 1); break;
            case 5: co = v & 1;  r = (uint8_t)((v >> 1) | (v & 0x80)); break;
            case 6: co = v >> 7; r = (uint8_t)((v << 1) | 1); break;
            default:co = v & 1;  r = (uint8_t)(v >> 1); break;
        }
        z80_szpc(c, r, co); wr(c, a, r);
    } else if (x == 1){
        uint8_t bit = v & (uint8_t)(1u << y);
        uint8_t f = (uint8_t)(c->f & Z80_C) | Z80_H;
        if (bit == 0) f |= (Z80_Z | Z80_PV);
        if (y == 7 && bit) f |= Z80_S;
        c->f = f;
    } else if (x == 2){ wr(c, a, (uint8_t)(v & ~(1u << y))); }
    else            { wr(c, a, (uint8_t)(v |  (1u << y))); }
}
/* DD/FD prefix: the following opcode uses IX/IY (and (IX+d)) in place of HL. */
static int do_index(cpu_z80_t *c, uint16_t *idx){
    uint8_t op = fetch(c);
    /* LD r,r' block with index substitution */
    if (op >= 0x40 && op <= 0x7F && op != 0x76){
        int dst = (op >> 3) & 7, src = op & 7;
        if (src == 6 || dst == 6){                 /* one side is (IX+d) */
            int8_t d = (int8_t)fetch(c); uint16_t a = (uint16_t)(*idx + d);
            if (src == 6) reg_set(c, dst, rd(c, a));   /* register side stays normal H/L */
            else          wr(c, a, reg_get(c, src));
            return 19;
        }
        uint8_t v = (src == 4) ? (uint8_t)(*idx >> 8) : (src == 5) ? (uint8_t)*idx : reg_get(c, src);
        if (dst == 4)      *idx = (uint16_t)((*idx & 0x00FF) | (v << 8));
        else if (dst == 5) *idx = (uint16_t)((*idx & 0xFF00) | v);
        else               reg_set(c, dst, v);
        return 8;
    }
    /* ALU A,(IX+d) / A,IXH / A,IXL */
    if (op >= 0x80 && op <= 0xBF){
        int rsel = op & 7; uint8_t v; int cyc = 8;
        if (rsel == 6){ int8_t d = (int8_t)fetch(c); v = rd(c, (uint16_t)(*idx + d)); cyc = 19; }
        else if (rsel == 4) v = (uint8_t)(*idx >> 8);
        else if (rsel == 5) v = (uint8_t)*idx;
        else v = reg_get(c, rsel);
        switch ((op >> 3) & 7){
            case 0: alu_add(c, v, 0); break; case 1: alu_add(c, v, 1); break;
            case 2: alu_sub(c, v, 0); break; case 3: alu_sub(c, v, 1); break;
            case 4: alu_and(c, v);    break; case 5: alu_xor(c, v);    break;
            case 6: alu_or(c, v);     break; default: alu_cp(c, v);    break;
        }
        return cyc;
    }
    switch (op){
        case 0x21: *idx = fetch16(c); return 14;                 /* LD IX,nn   */
        case 0x22: { uint16_t a = fetch16(c); wr(c,a,(uint8_t)*idx); wr(c,a+1,(uint8_t)(*idx>>8)); return 20; }
        case 0x2A: { uint16_t a = fetch16(c); *idx = (uint16_t)(rd(c,a)|(rd(c,a+1)<<8)); return 20; }
        case 0x23: (*idx)++; return 10;                          /* INC IX     */
        case 0x2B: (*idx)--; return 10;                          /* DEC IX     */
        case 0x09: idx_add(c, idx, BC(c)); return 15;
        case 0x19: idx_add(c, idx, DE(c)); return 15;
        case 0x29: idx_add(c, idx, *idx);  return 15;
        case 0x39: idx_add(c, idx, c->sp); return 15;
        case 0x24: { uint8_t v = alu_inc(c,(uint8_t)(*idx>>8)); *idx=(uint16_t)((*idx&0x00FF)|(v<<8)); return 8; }
        case 0x25: { uint8_t v = alu_dec(c,(uint8_t)(*idx>>8)); *idx=(uint16_t)((*idx&0x00FF)|(v<<8)); return 8; }
        case 0x2C: { uint8_t v = alu_inc(c,(uint8_t)*idx); *idx=(uint16_t)((*idx&0xFF00)|v); return 8; }
        case 0x2D: { uint8_t v = alu_dec(c,(uint8_t)*idx); *idx=(uint16_t)((*idx&0xFF00)|v); return 8; }
        case 0x26: *idx=(uint16_t)((*idx&0x00FF)|(fetch(c)<<8)); return 11;   /* LD IXH,n */
        case 0x2E: *idx=(uint16_t)((*idx&0xFF00)|fetch(c)); return 11;        /* LD IXL,n */
        case 0x34: { int8_t d=(int8_t)fetch(c); uint16_t a=(uint16_t)(*idx+d); wr(c,a,alu_inc(c,rd(c,a))); return 23; }
        case 0x35: { int8_t d=(int8_t)fetch(c); uint16_t a=(uint16_t)(*idx+d); wr(c,a,alu_dec(c,rd(c,a))); return 23; }
        case 0x36: { int8_t d=(int8_t)fetch(c); uint8_t n=fetch(c); wr(c,(uint16_t)(*idx+d),n); return 19; }
        case 0xE9: c->pc = *idx; return 8;                        /* JP (IX)    */
        case 0xF9: c->sp = *idx; return 10;                       /* LD SP,IX   */
        case 0xE5: push16(c, *idx); return 15;                    /* PUSH IX    */
        case 0xE1: *idx = pop16(c); return 14;                    /* POP IX     */
        case 0xE3: { uint16_t lo=rd(c,c->sp), hi=rd(c,c->sp+1);   /* EX (SP),IX */
                     wr(c,c->sp,(uint8_t)*idx); wr(c,c->sp+1,(uint8_t)(*idx>>8));
                     *idx=(uint16_t)(lo|(hi<<8)); return 23; }
        case 0xCB: { int8_t d=(int8_t)fetch(c); uint16_t a=(uint16_t)(*idx+d);
                     uint8_t sub=fetch(c); do_ddcb(c, a, sub); return 23; }
        default: c->illegal++; return 8;
    }
}

void cpu_z80_int(cpu_z80_t *c){
    if (!c->iff1) return;
    c->halted = 0; c->iff1 = c->iff2 = 0;
    push16(c, c->pc);
    if (c->im == 2){ uint16_t v = (uint16_t)((c->i << 8) | 0xFF);
                     c->pc = (uint16_t)(rd(c, v) | (rd(c, v+1) << 8)); }
    else c->pc = 0x0038u;                                 /* IM0/IM1 -> RST 38 */
    c->cycles += 13;
}

void cpu_z80_reset(cpu_z80_t *c) {
    c->a = c->f = c->b = c->c = c->d = c->e = c->h = c->l = 0;
    c->a2 = c->f2 = c->b2 = c->c2 = c->d2 = c->e2 = c->h2 = c->l2 = 0;
    c->ix = c->iy = 0; c->i = c->r = 0; c->im = 0;
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
        case 0xDB: { uint8_t n = fetch(c); c->a = z80_in(c, (uint16_t)((c->a << 8) | n)); cyc = 11; } break; /* IN A,(n) */
        case 0xD3: { uint8_t n = fetch(c); z80_out(c, (uint16_t)((c->a << 8) | n), c->a); cyc = 11; } break; /* OUT (n),A */
        case 0x08: { uint8_t t; t=c->a;c->a=c->a2;c->a2=t; t=c->f;c->f=c->f2;c->f2=t; cyc=4; } break; /* EX AF,AF' */
        case 0xD9: { uint8_t t;                                            /* EXX */
                     t=c->b;c->b=c->b2;c->b2=t; t=c->c;c->c=c->c2;c->c2=t;
                     t=c->d;c->d=c->d2;c->d2=t; t=c->e;c->e=c->e2;c->e2=t;
                     t=c->h;c->h=c->h2;c->h2=t; t=c->l;c->l=c->l2;c->l2=t; cyc=4; } break;
        case 0xE3: { uint8_t lo=rd(c,c->sp), hi=rd(c,c->sp+1);             /* EX (SP),HL */
                     wr(c,c->sp,c->l); wr(c,c->sp+1,c->h); c->l=lo; c->h=hi; cyc=19; } break;
        case 0xCB: cyc = 8; do_cb(c); break;                              /* CB: bit/rotate ops */
        case 0xED: cyc = do_ed(c); break;                                 /* ED: extended ops   */
        case 0xDD: cyc = do_index(c, &c->ix); break;                      /* DD: IX ops */
        case 0xFD: cyc = do_index(c, &c->iy); break;                      /* FD: IY ops */
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

/* ---- DECLARATION -----------------------------------------------------------

 * The Z80 core. emu/sms.o's ENTIRE undefined set is {cpu_z80_reset,
 * cpu_z80_step, cpu_z80_int} -- a console is a CPU plus its own glue, and
 * that is the only edge crossing between them. cpu_z80.o's own `nm -u` is
 * empty: an interpreter that calls nothing.
 */
#include "zxv_decl.h"
ZXV_DECLARE(cpu_z80,
    ZXV_PROVIDES(z80_cpu_ready),
    ZXV_REQUIRES_NONE,
    ZXV_NO_BRINGUP);
