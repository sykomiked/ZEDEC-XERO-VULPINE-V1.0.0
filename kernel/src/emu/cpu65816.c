/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* cpu65816.c — WDC 65C816 core. See cpu65816.h. Every opcode is decoded for its
 * correct length via the addressing-mode table (immediate width folded in), so
 * PC never desyncs; the operations real boot/init code uses are executed, and
 * the few left as stubs are counted in `illegal`. Binary arithmetic only
 * (decimal ADC/SBC is approximated as binary — init code rarely uses it). */
#include "cpu65816.h"

/* ---- addressing modes ------------------------------------------------------ */
enum { IMP, ACC, IMM_M, IMM_X, IMM8, DP, DPX, DPY, IDP, IDX, IDPY, IDL, IDLY,
       ABS, ABSX, ABSY, ABL, ABLX, IND, INDX, IAL, REL, RELL, SR, SRY, BM,
       ABSJ, ABLJ };

/* ---- operations ------------------------------------------------------------ */
enum { oILL, oNOP, oLDA, oLDX, oLDY, oSTA, oSTX, oSTY, oSTZ,
       oADC, oSBC, oAND, oORA, oEOR, oCMP, oCPX, oCPY, oBIT,
       oASL, oLSR, oROL, oROR, oINC, oDEC, oTSB, oTRB,
       oINX, oINY, oDEX, oDEY,
       oTAX, oTAY, oTXA, oTYA, oTXY, oTYX, oTSX, oTXS, oTCD, oTDC, oTCS, oTSC, oXBA,
       oBCC, oBCS, oBEQ, oBNE, oBMI, oBPL, oBVC, oBVS, oBRA, oBRL,
       oJMP, oJML, oJSR, oJSL, oRTS, oRTL, oRTI,
       oPHA, oPLA, oPHX, oPLX, oPHY, oPLY, oPHP, oPLP, oPHB, oPLB, oPHK, oPHD, oPLD,
       oPEA, oPEI, oPER,
       oCLC, oSEC, oCLI, oSEI, oCLD, oSED, oCLV, oREP, oSEP, oXCE,
       oMVN, oMVP, oBRK, oCOP, oWAI, oSTP, oWDM };

static const uint8_t MODE[256] = {
/*00*/ IMM8,IDX,IMM8,SR,DP,DP,DP,IDL, IMP,IMM_M,ACC,IMP,ABS,ABS,ABS,ABL,
/*10*/ REL,IDPY,IDP,SRY,DP,DPX,DPX,IDLY, IMP,ABSY,ACC,IMP,ABS,ABSX,ABSX,ABLX,
/*20*/ ABSJ,IDX,ABLJ,SR,DP,DP,DP,IDL, IMP,IMM_M,ACC,IMP,ABS,ABS,ABS,ABL,
/*30*/ REL,IDPY,IDP,SRY,DPX,DPX,DPX,IDLY, IMP,ABSY,ACC,IMP,ABSX,ABSX,ABSX,ABLX,
/*40*/ IMP,IDX,IMM8,SR,BM,DP,DP,IDL, IMP,IMM_M,ACC,IMP,ABSJ,ABS,ABS,ABL,
/*50*/ REL,IDPY,IDP,SRY,BM,DPX,DPX,IDLY, IMP,ABSY,IMP,IMP,ABLJ,ABSX,ABSX,ABLX,
/*60*/ IMP,IDX,RELL,SR,DP,DP,DP,IDL, IMP,IMM_M,ACC,IMP,IND,ABS,ABS,ABL,
/*70*/ REL,IDPY,IDP,SRY,DPX,DPX,DPX,IDLY, IMP,ABSY,IMP,IMP,INDX,ABSX,ABSX,ABLX,
/*80*/ REL,IDX,RELL,SR,DP,DP,DP,IDL, IMP,IMM_M,IMP,IMP,ABS,ABS,ABS,ABL,
/*90*/ REL,IDPY,IDP,SRY,DPX,DPX,DPY,IDLY, IMP,ABSY,IMP,IMP,ABS,ABSX,ABSX,ABLX,
/*A0*/ IMM_X,IDX,IMM_X,SR,DP,DP,DP,IDL, IMP,IMM_M,IMP,IMP,ABS,ABS,ABS,ABL,
/*B0*/ REL,IDPY,IDP,SRY,DPX,DPX,DPY,IDLY, IMP,ABSY,IMP,IMP,ABSX,ABSX,ABSY,ABLX,
/*C0*/ IMM_X,IDX,IMM8,SR,DP,DP,DP,IDL, IMP,IMM_M,IMP,IMP,ABS,ABS,ABS,ABL,
/*D0*/ REL,IDPY,IDP,SRY,DP,DPX,DPX,IDLY, IMP,ABSY,IMP,IMP,IAL,ABSX,ABSX,ABLX,
/*E0*/ IMM_X,IDX,IMM8,SR,DP,DP,DP,IDL, IMP,IMM_M,IMP,IMP,ABS,ABS,ABS,ABL,
/*F0*/ REL,IDPY,IDP,SRY,ABS,DPX,DPX,IDLY, IMP,ABSY,IMP,IMP,INDX,ABSX,ABSX,ABLX,
};

static const uint8_t OP[256] = {
/*00*/ oBRK,oORA,oCOP,oORA,oTSB,oORA,oASL,oORA, oPHP,oORA,oASL,oPHD,oTSB,oORA,oASL,oORA,
/*10*/ oBPL,oORA,oORA,oORA,oTRB,oORA,oASL,oORA, oCLC,oORA,oINC,oTCS,oTRB,oORA,oASL,oORA,
/*20*/ oJSR,oAND,oJSL,oAND,oBIT,oAND,oROL,oAND, oPLP,oAND,oROL,oPLD,oBIT,oAND,oROL,oAND,
/*30*/ oBMI,oAND,oAND,oAND,oBIT,oAND,oROL,oAND, oSEC,oAND,oDEC,oTSC,oBIT,oAND,oROL,oAND,
/*40*/ oRTI,oEOR,oWDM,oEOR,oMVP,oEOR,oLSR,oEOR, oPHA,oEOR,oLSR,oPHK,oJMP,oEOR,oLSR,oEOR,
/*50*/ oBVC,oEOR,oEOR,oEOR,oMVN,oEOR,oLSR,oEOR, oCLI,oEOR,oPHY,oTCD,oJML,oEOR,oLSR,oEOR,
/*60*/ oRTS,oADC,oPER,oADC,oSTZ,oADC,oROR,oADC, oPLA,oADC,oROR,oRTL,oJMP,oADC,oROR,oADC,
/*70*/ oBVS,oADC,oADC,oADC,oSTZ,oADC,oROR,oADC, oSEI,oADC,oPLY,oTDC,oJMP,oADC,oROR,oADC,
/*80*/ oBRA,oSTA,oBRL,oSTA,oSTY,oSTA,oSTX,oSTA, oDEY,oBIT,oTXA,oPHB,oSTY,oSTA,oSTX,oSTA,
/*90*/ oBCC,oSTA,oSTA,oSTA,oSTY,oSTA,oSTX,oSTA, oTYA,oSTA,oTXS,oTXY,oSTZ,oSTA,oSTZ,oSTA,
/*A0*/ oLDY,oLDA,oLDX,oLDA,oLDY,oLDA,oLDX,oLDA, oTAY,oLDA,oTAX,oPLB,oLDY,oLDA,oLDX,oLDA,
/*B0*/ oBCS,oLDA,oLDA,oLDA,oLDY,oLDA,oLDX,oLDA, oCLV,oLDA,oTSX,oTYX,oLDY,oLDA,oLDX,oLDA,
/*C0*/ oCPY,oCMP,oREP,oCMP,oCPY,oCMP,oDEC,oCMP, oINY,oCMP,oDEX,oWAI,oCPY,oCMP,oDEC,oCMP,
/*D0*/ oBNE,oCMP,oCMP,oCMP,oPEI,oCMP,oDEC,oCMP, oCLD,oCMP,oPHX,oSTP,oJML,oCMP,oDEC,oCMP,
/*E0*/ oCPX,oSBC,oSEP,oSBC,oCPX,oSBC,oINC,oSBC, oINX,oSBC,oNOP,oXBA,oCPX,oSBC,oINC,oSBC,
/*F0*/ oBEQ,oSBC,oSBC,oSBC,oPEA,oSBC,oINC,oSBC, oSED,oSBC,oPLX,oXCE,oJSR,oSBC,oINC,oSBC,
};

/* ---- bus helpers ----------------------------------------------------------- */
static inline uint8_t  rd8 (cpu65816_t *c, uint32_t a){ return c->read(c, a & 0xFFFFFF); }
static inline void     wr8 (cpu65816_t *c, uint32_t a, uint8_t v){ c->write(c, a & 0xFFFFFF, v); }
static inline uint16_t rd16(cpu65816_t *c, uint32_t a){ return (uint16_t)(rd8(c,a) | (rd8(c,a+1)<<8)); }
/* direct-page / bank-0 pointer fetches wrap within bank 0 */
static inline uint16_t dptr16(cpu65816_t *c, uint32_t off){
    return (uint16_t)(rd8(c, off & 0xFFFF) | (rd8(c, (off+1) & 0xFFFF) << 8));
}
static inline uint32_t dptr24(cpu65816_t *c, uint32_t off){
    return (uint32_t)rd8(c, off & 0xFFFF)
         | ((uint32_t)rd8(c, (off+1) & 0xFFFF) << 8)
         | ((uint32_t)rd8(c, (off+2) & 0xFFFF) << 16);
}

static uint8_t mode_len(uint8_t mode, int acc8, int idx8){
    switch (mode){
        case IMP: case ACC:                                   return 0;
        case IMM_M:                                            return acc8 ? 1 : 2;
        case IMM_X:                                            return idx8 ? 1 : 2;
        case IMM8: case DP: case DPX: case DPY: case IDP:
        case IDX: case IDPY: case IDL: case IDLY: case REL:
        case SR:  case SRY:                                    return 1;
        case ABS: case ABSX: case ABSY: case IND: case INDX:
        case IAL: case RELL: case BM: case ABSJ:               return 2;
        case ABL: case ABLX: case ABLJ:                        return 3;
        default:                                               return 1;
    }
}

/* ---- flag helpers ---------------------------------------------------------- */
static inline void setflag(cpu65816_t *c, uint8_t f, int on){ if (on) c->p |= f; else c->p &= ~f; }
static inline void setnz(cpu65816_t *c, uint32_t v, int wide){
    if (wide){ setflag(c, CPU816_Z, (v & 0xFFFF)==0); setflag(c, CPU816_N, v & 0x8000); }
    else     { setflag(c, CPU816_Z, (v & 0xFF)==0);   setflag(c, CPU816_N, v & 0x80); }
}

/* When switching indexes to 8-bit, the high bytes of X/Y are lost. */
static void apply_widths(cpu65816_t *c){
    if (c->e){ c->p |= (CPU816_M | CPU816_X); c->sp = 0x0100 | (c->sp & 0xFF); }
    if (cpu816_idx8(c)){ c->x &= 0xFF; c->y &= 0xFF; }
}

/* ---- stack ----------------------------------------------------------------- */
static void push8(cpu65816_t *c, uint8_t v){
    wr8(c, c->sp, v);
    if (c->e) c->sp = 0x0100 | ((c->sp - 1) & 0xFF);
    else      c->sp = (c->sp - 1) & 0xFFFF;
}
static uint8_t pull8(cpu65816_t *c){
    if (c->e) c->sp = 0x0100 | ((c->sp + 1) & 0xFF);
    else      c->sp = (c->sp + 1) & 0xFFFF;
    return rd8(c, c->sp);
}
static void push16(cpu65816_t *c, uint16_t v){ push8(c, v >> 8); push8(c, v & 0xFF); }
static uint16_t pull16(cpu65816_t *c){ uint16_t lo = pull8(c); uint16_t hi = pull8(c); return lo | (hi << 8); }

/* ---- effective address for memory modes ------------------------------------ */
static uint32_t ea_of(cpu65816_t *c, uint8_t mode, uint32_t oper){
    switch (mode){
        case DP:   return (uint16_t)(c->d + (oper & 0xFF));
        case DPX:  return (uint16_t)(c->d + (oper & 0xFF) + c->x);
        case DPY:  return (uint16_t)(c->d + (oper & 0xFF) + c->y);
        case IDP:  return (((uint32_t)c->dbr << 16) + dptr16(c, c->d + (oper & 0xFF))) & 0xFFFFFF;
        case IDX:  return (((uint32_t)c->dbr << 16) + dptr16(c, c->d + (oper & 0xFF) + c->x)) & 0xFFFFFF;
        case IDPY: return (((uint32_t)c->dbr << 16) + dptr16(c, c->d + (oper & 0xFF)) + c->y) & 0xFFFFFF;
        case IDL:  return dptr24(c, c->d + (oper & 0xFF));
        case IDLY: return (dptr24(c, c->d + (oper & 0xFF)) + c->y) & 0xFFFFFF;
        case ABS:  return ((uint32_t)c->dbr << 16) + (oper & 0xFFFF);
        case ABSX: return (((uint32_t)c->dbr << 16) + (oper & 0xFFFF) + c->x) & 0xFFFFFF;
        case ABSY: return (((uint32_t)c->dbr << 16) + (oper & 0xFFFF) + c->y) & 0xFFFFFF;
        case ABL:  return oper & 0xFFFFFF;
        case ABLX: return (oper + c->x) & 0xFFFFFF;
        case SR:   return (uint16_t)(c->sp + (oper & 0xFF));
        case SRY:  return (((uint32_t)c->dbr << 16) + dptr16(c, c->sp + (oper & 0xFF)) + c->y) & 0xFFFFFF;
        default:   return 0;
    }
}

/* width-aware data access */
static uint16_t rd_m(cpu65816_t *c, uint32_t ea){ return cpu816_acc8(c) ? rd8(c,ea) : rd16(c,ea); }
static uint16_t rd_x(cpu65816_t *c, uint32_t ea){ return cpu816_idx8(c) ? rd8(c,ea) : rd16(c,ea); }
static void wr_m(cpu65816_t *c, uint32_t ea, uint16_t v){ wr8(c,ea,v & 0xFF); if (!cpu816_acc8(c)) wr8(c,ea+1,v>>8); }
static void wr_x(cpu65816_t *c, uint32_t ea, uint16_t v){ wr8(c,ea,v & 0xFF); if (!cpu816_idx8(c)) wr8(c,ea+1,v>>8); }

/* value for a read op: immediate uses oper directly, else load from EA */
static uint16_t val_m(cpu65816_t *c, uint8_t mode, uint32_t oper){
    return (mode == IMM_M) ? (uint16_t)oper : rd_m(c, ea_of(c, mode, oper));
}
static uint16_t val_x(cpu65816_t *c, uint8_t mode, uint32_t oper){
    return (mode == IMM_X) ? (uint16_t)oper : rd_x(c, ea_of(c, mode, oper));
}

/* set A respecting width (preserve high byte in 8-bit mode) */
static inline void set_a(cpu65816_t *c, uint16_t v){ c->a = cpu816_acc8(c) ? ((c->a & 0xFF00) | (v & 0xFF)) : v; }

static void adc(cpu65816_t *c, uint16_t m){
    unsigned carry = (c->p & CPU816_C) ? 1 : 0;
    if (cpu816_acc8(c)){
        unsigned a = c->a & 0xFF, r = a + (m & 0xFF) + carry;
        setflag(c, CPU816_V, (~(a ^ m) & (a ^ r) & 0x80));
        setflag(c, CPU816_C, r > 0xFF);
        set_a(c, r); setnz(c, r, 0);
    } else {
        unsigned a = c->a, r = a + m + carry;
        setflag(c, CPU816_V, (~(a ^ m) & (a ^ r) & 0x8000));
        setflag(c, CPU816_C, r > 0xFFFF);
        c->a = r; setnz(c, r, 1);
    }
}
static void sbc(cpu65816_t *c, uint16_t m){ adc(c, cpu816_acc8(c) ? (~m & 0xFF) : (uint16_t)~m); }

static void cmp_generic(cpu65816_t *c, uint16_t reg, uint16_t m, int wide){
    uint32_t r = (uint32_t)(wide ? reg : (reg & 0xFF)) - (wide ? m : (m & 0xFF));
    setflag(c, CPU816_C, ((wide ? reg : (reg&0xFF)) >= (wide ? m : (m&0xFF))));
    setnz(c, r, wide);
}

/* read-modify-write on accumulator or memory, width-aware */
static uint16_t rmw_load(cpu65816_t *c, uint8_t mode, uint32_t oper, uint32_t *ea_out){
    if (mode == ACC){ *ea_out = 0xFFFFFFFF; return cpu816_acc8(c) ? (c->a & 0xFF) : c->a; }
    uint32_t ea = ea_of(c, mode, oper); *ea_out = ea; return rd_m(c, ea);
}
static void rmw_store(cpu65816_t *c, uint32_t ea, uint16_t v){
    if (ea == 0xFFFFFFFF) set_a(c, v); else wr_m(c, ea, v);
}

/* ---- reset / interrupts ---------------------------------------------------- */
void cpu65816_reset(cpu65816_t *c){
    c->e = 1; c->p = CPU816_M | CPU816_X | CPU816_I; /* decimal cleared */
    c->d = 0; c->dbr = 0; c->pbr = 0;
    c->sp = 0x01FF; c->x &= 0xFF; c->y &= 0xFF;
    c->pc = rd16(c, 0x00FFFC);
    c->cycles = 0; c->stopped = 0; c->illegal = 0; c->last_opcode = 0;
}
static void interrupt(cpu65816_t *c, uint32_t nat_vec, uint32_t emu_vec){
    c->stopped = 0;
    if (c->e){ push16(c, c->pc); push8(c, c->p & ~0x10); c->pc = rd16(c, emu_vec); }
    else     { push8(c, c->pbr); push16(c, c->pc); push8(c, c->p); c->pc = rd16(c, nat_vec); }
    c->pbr = 0; c->p |= CPU816_I; c->p &= ~CPU816_D;
}
void cpu65816_nmi(cpu65816_t *c){ interrupt(c, 0x00FFEA, 0x00FFFA); }
void cpu65816_irq(cpu65816_t *c){ if (!(c->p & CPU816_I)) interrupt(c, 0x00FFEE, 0x00FFFE); }

/* ---- one instruction ------------------------------------------------------- */
int cpu65816_step(cpu65816_t *c){
    if (c->stopped) return 0;
    uint32_t pcbank = (uint32_t)c->pbr << 16;
    uint8_t opcode = rd8(c, pcbank | c->pc); c->last_opcode = opcode;
    c->pc = (c->pc + 1) & 0xFFFF;
    uint8_t mode = MODE[opcode], op = OP[opcode];
    uint8_t len = mode_len(mode, cpu816_acc8(c), cpu816_idx8(c));

    uint32_t oper = 0;
    for (uint8_t i = 0; i < len; i++){
        oper |= (uint32_t)rd8(c, ((uint32_t)c->pbr << 16) | c->pc) << (8 * i);
        c->pc = (c->pc + 1) & 0xFFFF;
    }

    int accW = !cpu816_acc8(c), idxW = !cpu816_idx8(c);
    uint32_t ea;

    switch (op){
        /* loads */
        case oLDA: { uint16_t v = val_m(c, mode, oper); set_a(c, v); setnz(c, v, accW); } break;
        case oLDX: { uint16_t v = val_x(c, mode, oper); c->x = cpu816_idx8(c)?(v&0xFF):v; setnz(c, c->x, idxW); } break;
        case oLDY: { uint16_t v = val_x(c, mode, oper); c->y = cpu816_idx8(c)?(v&0xFF):v; setnz(c, c->y, idxW); } break;
        /* stores */
        case oSTA: wr_m(c, ea_of(c, mode, oper), cpu816_acc8(c)?(c->a&0xFF):c->a); break;
        case oSTX: wr_x(c, ea_of(c, mode, oper), c->x); break;
        case oSTY: wr_x(c, ea_of(c, mode, oper), c->y); break;
        case oSTZ: wr_m(c, ea_of(c, mode, oper), 0); break;
        /* alu */
        case oADC: adc(c, val_m(c, mode, oper)); break;
        case oSBC: sbc(c, val_m(c, mode, oper)); break;
        case oAND: { uint16_t v = c->a & val_m(c, mode, oper); set_a(c, v); setnz(c, cpu816_acc8(c)?(v&0xFF):v, accW); } break;
        case oORA: { uint16_t v = c->a | val_m(c, mode, oper); set_a(c, v); setnz(c, cpu816_acc8(c)?(v&0xFF):v, accW); } break;
        case oEOR: { uint16_t v = c->a ^ val_m(c, mode, oper); set_a(c, v); setnz(c, cpu816_acc8(c)?(v&0xFF):v, accW); } break;
        case oCMP: cmp_generic(c, c->a, val_m(c, mode, oper), accW); break;
        case oCPX: cmp_generic(c, c->x, val_x(c, mode, oper), idxW); break;
        case oCPY: cmp_generic(c, c->y, val_x(c, mode, oper), idxW); break;
        case oBIT: { uint16_t v = val_m(c, mode, oper); uint16_t a = cpu816_acc8(c)?(c->a&0xFF):c->a;
                     setflag(c, CPU816_Z, (a & v)==0);
                     if (mode != IMM_M){ setflag(c, CPU816_N, v & (accW?0x8000:0x80));
                                         setflag(c, CPU816_V, v & (accW?0x4000:0x40)); } } break;
        /* rmw */
        case oASL: { ea = 0; uint32_t e2; uint16_t v = rmw_load(c, mode, oper, &e2);
                     setflag(c, CPU816_C, v & (accW?0x8000:0x80)); v <<= 1; rmw_store(c, e2, v); setnz(c, v, accW); (void)ea; } break;
        case oLSR: { uint32_t e2; uint16_t v = rmw_load(c, mode, oper, &e2);
                     setflag(c, CPU816_C, v & 1); v = (accW?(v&0xFFFF):(v&0xFF)) >> 1; rmw_store(c, e2, v); setnz(c, v, accW); } break;
        case oROL: { uint32_t e2; uint16_t v = rmw_load(c, mode, oper, &e2); unsigned cy=(c->p&CPU816_C)?1:0;
                     setflag(c, CPU816_C, v & (accW?0x8000:0x80)); v = (v<<1)|cy; rmw_store(c, e2, v); setnz(c, v, accW); } break;
        case oROR: { uint32_t e2; uint16_t v = rmw_load(c, mode, oper, &e2); unsigned cy=(c->p&CPU816_C)?(accW?0x8000:0x80):0;
                     setflag(c, CPU816_C, v & 1); v = ((accW?(v&0xFFFF):(v&0xFF))>>1)|cy; rmw_store(c, e2, v); setnz(c, v, accW); } break;
        case oINC: { uint32_t e2; uint16_t v = rmw_load(c, mode, oper, &e2)+1; rmw_store(c, e2, v); setnz(c, v, accW); } break;
        case oDEC: { uint32_t e2; uint16_t v = rmw_load(c, mode, oper, &e2)-1; rmw_store(c, e2, v); setnz(c, v, accW); } break;
        case oTSB: { ea = ea_of(c, mode, oper); uint16_t v = rd_m(c,ea), a = cpu816_acc8(c)?(c->a&0xFF):c->a;
                     setflag(c, CPU816_Z, (a & v)==0); wr_m(c, ea, v | a); } break;
        case oTRB: { ea = ea_of(c, mode, oper); uint16_t v = rd_m(c,ea), a = cpu816_acc8(c)?(c->a&0xFF):c->a;
                     setflag(c, CPU816_Z, (a & v)==0); wr_m(c, ea, v & ~a); } break;
        /* index inc/dec */
        case oINX: c->x = cpu816_idx8(c)?((c->x+1)&0xFF):((c->x+1)&0xFFFF); setnz(c, c->x, idxW); break;
        case oINY: c->y = cpu816_idx8(c)?((c->y+1)&0xFF):((c->y+1)&0xFFFF); setnz(c, c->y, idxW); break;
        case oDEX: c->x = cpu816_idx8(c)?((c->x-1)&0xFF):((c->x-1)&0xFFFF); setnz(c, c->x, idxW); break;
        case oDEY: c->y = cpu816_idx8(c)?((c->y-1)&0xFF):((c->y-1)&0xFFFF); setnz(c, c->y, idxW); break;
        /* transfers */
        case oTAX: c->x = cpu816_idx8(c)?(c->a&0xFF):c->a; setnz(c, c->x, idxW); break;
        case oTAY: c->y = cpu816_idx8(c)?(c->a&0xFF):c->a; setnz(c, c->y, idxW); break;
        case oTXA: set_a(c, c->x); setnz(c, cpu816_acc8(c)?(c->a&0xFF):c->a, accW); break;
        case oTYA: set_a(c, c->y); setnz(c, cpu816_acc8(c)?(c->a&0xFF):c->a, accW); break;
        case oTXY: c->y = c->x; setnz(c, c->y, idxW); break;
        case oTYX: c->x = c->y; setnz(c, c->x, idxW); break;
        case oTSX: c->x = cpu816_idx8(c)?(c->sp&0xFF):c->sp; setnz(c, c->x, idxW); break;
        case oTXS: c->sp = c->e ? (0x0100 | (c->x & 0xFF)) : c->x; break;
        case oTCD: c->d = c->a; setnz(c, c->d, 1); break;
        case oTDC: c->a = c->d; setnz(c, c->a, 1); break;
        case oTCS: c->sp = c->e ? (0x0100 | (c->a & 0xFF)) : c->a; break;
        case oTSC: c->a = c->sp; setnz(c, c->a, 1); break;
        case oXBA: c->a = ((c->a & 0xFF) << 8) | (c->a >> 8); setnz(c, c->a & 0xFF, 0); break;
        /* branches */
        case oBCC: if (!(c->p & CPU816_C)) c->pc = (c->pc + (int8_t)(oper & 0xFF)) & 0xFFFF; break;
        case oBCS: if (  c->p & CPU816_C ) c->pc = (c->pc + (int8_t)(oper & 0xFF)) & 0xFFFF; break;
        case oBEQ: if (  c->p & CPU816_Z ) c->pc = (c->pc + (int8_t)(oper & 0xFF)) & 0xFFFF; break;
        case oBNE: if (!(c->p & CPU816_Z)) c->pc = (c->pc + (int8_t)(oper & 0xFF)) & 0xFFFF; break;
        case oBMI: if (  c->p & CPU816_N ) c->pc = (c->pc + (int8_t)(oper & 0xFF)) & 0xFFFF; break;
        case oBPL: if (!(c->p & CPU816_N)) c->pc = (c->pc + (int8_t)(oper & 0xFF)) & 0xFFFF; break;
        case oBVC: if (!(c->p & CPU816_V)) c->pc = (c->pc + (int8_t)(oper & 0xFF)) & 0xFFFF; break;
        case oBVS: if (  c->p & CPU816_V ) c->pc = (c->pc + (int8_t)(oper & 0xFF)) & 0xFFFF; break;
        case oBRA: c->pc = (c->pc + (int8_t)(oper & 0xFF)) & 0xFFFF; break;
        case oBRL: c->pc = (c->pc + (int16_t)(oper & 0xFFFF)) & 0xFFFF; break;
        /* jumps / calls */
        case oJMP: c->pc = (mode == IND)  ? rd16(c, oper & 0xFFFF)
                        : (mode == INDX) ? rd16(c, ((uint32_t)c->pbr<<16) + ((oper + c->x) & 0xFFFF))
                        : (uint16_t)oper; break;                      /* ABSJ */
        case oJML: if (mode == IAL){ uint32_t p = dptr24(c, oper & 0xFFFF); c->pc = p & 0xFFFF; c->pbr = p >> 16; }
                   else { c->pc = oper & 0xFFFF; c->pbr = (oper >> 16) & 0xFF; } break; /* ABLJ */
        case oJSR: if (mode == INDX){ push16(c, (c->pc - 1) & 0xFFFF);
                        c->pc = rd16(c, ((uint32_t)c->pbr<<16) + ((oper + c->x) & 0xFFFF)); }
                   else { push16(c, (c->pc - 1) & 0xFFFF); c->pc = (uint16_t)oper; } break;
        case oJSL: push8(c, c->pbr); push16(c, (c->pc - 1) & 0xFFFF);
                   c->pbr = (oper >> 16) & 0xFF; c->pc = oper & 0xFFFF; break;
        case oRTS: c->pc = (pull16(c) + 1) & 0xFFFF; break;
        case oRTL: c->pc = (pull16(c) + 1) & 0xFFFF; c->pbr = pull8(c); break;
        case oRTI: c->p = pull8(c); c->pc = pull16(c); if (!c->e) c->pbr = pull8(c); apply_widths(c); break;
        /* stack */
        case oPHA: if (cpu816_acc8(c)) push8(c, c->a & 0xFF); else push16(c, c->a); break;
        case oPLA: if (cpu816_acc8(c)){ set_a(c, pull8(c)); } else c->a = pull16(c); setnz(c, cpu816_acc8(c)?(c->a&0xFF):c->a, accW); break;
        case oPHX: if (cpu816_idx8(c)) push8(c, c->x & 0xFF); else push16(c, c->x); break;
        case oPLX: c->x = cpu816_idx8(c)?pull8(c):pull16(c); setnz(c, c->x, idxW); break;
        case oPHY: if (cpu816_idx8(c)) push8(c, c->y & 0xFF); else push16(c, c->y); break;
        case oPLY: c->y = cpu816_idx8(c)?pull8(c):pull16(c); setnz(c, c->y, idxW); break;
        case oPHP: push8(c, c->p); break;
        case oPLP: c->p = pull8(c); apply_widths(c); break;
        case oPHB: push8(c, c->dbr); break;
        case oPLB: c->dbr = pull8(c); setnz(c, c->dbr, 0); break;
        case oPHK: push8(c, c->pbr); break;
        case oPHD: push16(c, c->d); break;
        case oPLD: c->d = pull16(c); setnz(c, c->d, 1); break;
        case oPEA: push16(c, (uint16_t)oper); break;
        case oPEI: push16(c, dptr16(c, c->d + (oper & 0xFF))); break;
        case oPER: push16(c, (uint16_t)(c->pc + (int16_t)(oper & 0xFFFF))); break;
        /* flags / modes */
        case oCLC: c->p &= ~CPU816_C; break;
        case oSEC: c->p |=  CPU816_C; break;
        case oCLI: c->p &= ~CPU816_I; break;
        case oSEI: c->p |=  CPU816_I; break;
        case oCLD: c->p &= ~CPU816_D; break;
        case oSED: c->p |=  CPU816_D; break;
        case oCLV: c->p &= ~CPU816_V; break;
        case oREP: c->p &= ~(uint8_t)(oper & 0xFF); if (c->e) c->p |= (CPU816_M|CPU816_X); apply_widths(c); break;
        case oSEP: c->p |=  (uint8_t)(oper & 0xFF); apply_widths(c); break;
        case oXCE: { uint8_t oldc = (c->p & CPU816_C)?1:0; setflag(c, CPU816_C, c->e); c->e = oldc; apply_widths(c); } break;
        /* block move: dst bank = low operand byte, src bank = high operand byte */
        case oMVN: case oMVP: {
            uint8_t dstb = oper & 0xFF, srcb = (oper >> 8) & 0xFF;
            wr8(c, ((uint32_t)dstb<<16) | c->y, rd8(c, ((uint32_t)srcb<<16) | c->x));
            c->dbr = dstb;
            if (op == oMVN){ c->x = (c->x+1)&0xFFFF; c->y = (c->y+1)&0xFFFF; }
            else           { c->x = (c->x-1)&0xFFFF; c->y = (c->y-1)&0xFFFF; }
            c->a = (c->a - 1) & 0xFFFF;
            if (c->a != 0xFFFF) c->pc = (c->pc - 3) & 0xFFFF;  /* re-execute until done */
        } break;
        /* control */
        case oNOP: break;
        case oWAI: c->stopped = 1; break;   /* released by the machine on IRQ/NMI */
        case oSTP: c->stopped = 1; break;
        case oBRK: interrupt(c, 0x00FFE6, 0x00FFFE); break;
        case oCOP: interrupt(c, 0x00FFE4, 0x00FFF4); break;
        case oWDM: c->illegal++; break;     /* reserved 2-byte NOP */
        default:   c->illegal++; break;     /* any op left as a stub */
    }

    int cyc = 2 + len; c->cycles += cyc;
    return cyc;
}

uint64_t cpu65816_run(cpu65816_t *c, uint64_t max_cycles){
    uint64_t start = c->cycles;
    while (c->cycles - start < max_cycles && !c->stopped) cpu65816_step(c);
    return c->cycles - start;
}
