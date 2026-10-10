/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cpu_m68k.c — Motorola 68000 interpreter (big-endian). See cpu_m68k.h. Covers
 * the instruction groups Genesis game code uses; rare encodings are counted in
 * `illegal`. Operation sizes are byte(1)/word(2)/long(4). */
#include "cpu_m68k.h"

static inline uint32_t msk(int sz){ return sz==1?0xFFu : sz==2?0xFFFFu : 0xFFFFFFFFu; }
static inline uint32_t sgn(int sz){ return sz==1?0x80u : sz==2?0x8000u : 0x80000000u; }

static inline uint16_t fetch16(cpu_m68k_t *c){ uint16_t v=c->read16(c,c->pc); c->pc+=2; return v; }
static inline uint32_t fetch32(cpu_m68k_t *c){ uint32_t v=c->read32(c,c->pc); c->pc+=4; return v; }

static inline void set_sr_s(cpu_m68k_t *c, int super){
    int cur = (c->sr & M68_S) ? 1 : 0;
    if (cur == super) return;
    if (super){ c->usp = c->a[7]; c->a[7] = c->ssp; c->sr |= M68_S; }
    else      { c->ssp = c->a[7]; c->a[7] = c->usp; c->sr &= ~M68_S; }
}

/* Set the whole SR, handling the supervisor-bit stack-pointer swap. */
static void set_sr(cpu_m68k_t *c, uint16_t ns){ set_sr_s(c, (ns & M68_S) ? 1 : 0); c->sr = ns; }

static uint32_t read_mem(cpu_m68k_t *c, uint32_t a, int sz){
    a &= 0xFFFFFF;
    return sz==1 ? c->read8(c,a) : sz==2 ? c->read16(c,a) : c->read32(c,a);
}
static void write_mem(cpu_m68k_t *c, uint32_t a, int sz, uint32_t v){
    a &= 0xFFFFFF;
    if (sz==1) c->write8(c,a,(uint8_t)v); else if (sz==2) c->write16(c,a,(uint16_t)v); else c->write32(c,a,v);
}

/* effective-address handle */
typedef struct { int kind; uint32_t addr; int reg; uint32_t imm; } ea_t; /* kind:0 Dn 1 An 2 mem 3 imm */

static uint32_t index_ea(cpu_m68k_t *c, uint32_t base){
    uint16_t ext = fetch16(c);
    int da=(ext>>15)&1, rg=(ext>>12)&7, wl=(ext>>11)&1;
    uint32_t idx = da ? c->a[rg] : c->d[rg];
    if (!wl) idx = (uint32_t)(int32_t)(int16_t)idx;
    return base + (uint32_t)(int32_t)(int8_t)(ext & 0xFF) + idx;
}

static void decode_ea(cpu_m68k_t *c, int mode, int reg, int sz, ea_t *ea){
    switch (mode){
        case 0: ea->kind=0; ea->reg=reg; break;
        case 1: ea->kind=1; ea->reg=reg; break;
        case 2: ea->kind=2; ea->addr=c->a[reg]; break;
        case 3: ea->kind=2; ea->addr=c->a[reg]; { int inc=(reg==7&&sz==1)?2:sz; c->a[reg]+=inc; } break;
        case 4: { int dec=(reg==7&&sz==1)?2:sz; c->a[reg]-=dec; ea->kind=2; ea->addr=c->a[reg]; } break;
        case 5: { int16_t d=(int16_t)fetch16(c); ea->kind=2; ea->addr=c->a[reg]+d; } break;
        case 6: ea->kind=2; ea->addr=index_ea(c, c->a[reg]); break;
        case 7:
            switch (reg){
                case 0: ea->kind=2; ea->addr=(uint32_t)(int32_t)(int16_t)fetch16(c); break;
                case 1: ea->kind=2; ea->addr=fetch32(c); break;
                case 2: { uint32_t b=c->pc; int16_t d=(int16_t)fetch16(c); ea->kind=2; ea->addr=b+d; } break;
                case 3: { uint32_t b=c->pc; ea->kind=2; ea->addr=index_ea(c,b); } break;
                default: ea->kind=3; ea->imm = (sz==4)?fetch32(c):(uint32_t)fetch16(c) & (sz==1?0xFF:0xFFFF); break;
            }
            break;
    }
}
static uint32_t read_ea(cpu_m68k_t *c, ea_t *ea, int sz){
    switch (ea->kind){
        case 0: return c->d[ea->reg] & msk(sz);
        case 1: return sz==2 ? (uint32_t)(int32_t)(int16_t)c->a[ea->reg] : c->a[ea->reg];
        case 2: return read_mem(c, ea->addr, sz);
        default: return ea->imm & msk(sz);
    }
}
static void write_ea(cpu_m68k_t *c, ea_t *ea, int sz, uint32_t v){
    switch (ea->kind){
        case 0: { uint32_t m=msk(sz); c->d[ea->reg]=(c->d[ea->reg]&~m)|(v&m); break; }
        case 1: c->a[ea->reg] = sz==2 ? (uint32_t)(int32_t)(int16_t)v : v; break;
        case 2: write_mem(c, ea->addr, sz, v); break;
        default: break;
    }
}

static void setNZ(cpu_m68k_t *c, uint32_t v, int sz){
    c->sr &= ~(M68_N|M68_Z);
    if ((v & msk(sz)) == 0) c->sr |= M68_Z;
    if (v & sgn(sz)) c->sr |= M68_N;
}
static void logic_flags(cpu_m68k_t *c, uint32_t v, int sz){ setNZ(c,v,sz); c->sr &= ~(M68_V|M68_C); }

static uint32_t do_add(cpu_m68k_t *c, uint32_t a, uint32_t b, int sz, int with_x){
    uint32_t m=msk(sz), s=sgn(sz); unsigned x = (with_x && (c->sr&M68_X)) ? 1:0;
    uint64_t t=(uint64_t)(a&m)+(b&m)+x; uint32_t r=(uint32_t)t & m;
    int carry = t > m; int ov = (~(a^b) & (a^r) & s) != 0;
    c->sr &= ~(M68_C|M68_V|M68_X);
    if (carry) c->sr |= (M68_C|M68_X); if (ov) c->sr |= M68_V;
    setNZ(c,r,sz); return r;
}
static uint32_t do_sub(cpu_m68k_t *c, uint32_t a, uint32_t b, int sz, int with_x, int cmp){
    uint32_t m=msk(sz), s=sgn(sz); unsigned x=(with_x && (c->sr&M68_X))?1:0;
    uint64_t t=(uint64_t)(a&m) - (b&m) - x; uint32_t r=(uint32_t)t & m;
    int borrow = (uint64_t)(b&m)+x > (a&m); int ov = ((a^b) & (a^r) & s) != 0;
    c->sr &= ~(M68_C|M68_V|(cmp?0:M68_X));
    if (borrow){ c->sr |= M68_C; if(!cmp) c->sr |= M68_X; } if (ov) c->sr |= M68_V;
    setNZ(c,r,sz); return r;
}

static int cond(cpu_m68k_t *c, int cc){
    int C=!!(c->sr&M68_C),V=!!(c->sr&M68_V),Z=!!(c->sr&M68_Z),N=!!(c->sr&M68_N);
    switch (cc){
        case 0: return 1;             case 1: return 0;
        case 2: return !C && !Z;      case 3: return C || Z;
        case 4: return !C;            case 5: return C;
        case 6: return !Z;            case 7: return Z;
        case 8: return !V;            case 9: return V;
        case 0xA: return !N;          case 0xB: return N;
        case 0xC: return N==V;        case 0xD: return N!=V;
        case 0xE: return !Z && (N==V);default: return Z || (N!=V);
    }
}

static void push32(cpu_m68k_t *c, uint32_t v){ c->a[7]-=4; c->write32(c,c->a[7]&0xFFFFFF,v); }
static uint32_t pop32(cpu_m68k_t *c){ uint32_t v=c->read32(c,c->a[7]&0xFFFFFF); c->a[7]+=4; return v; }

static void trap(cpu_m68k_t *c, int vec){
    uint16_t oldsr = c->sr; set_sr_s(c,1);
    push32(c, c->pc); c->a[7]-=2; c->write16(c,c->a[7]&0xFFFFFF,oldsr);
    c->pc = c->read32(c, vec*4);
}

void cpu_m68k_reset(cpu_m68k_t *c){
    c->sr = 0x2700;
    c->ssp = c->read32(c, 0x000000); c->a[7] = c->ssp;
    c->pc  = c->read32(c, 0x000004);
    c->cycles=0; c->stopped=0; c->illegal=0; c->last_group=0;
}

int cpu_m68k_irq(cpu_m68k_t *c, int level){
    int mask = (c->sr >> 8) & 7;
    if (level != 7 && level <= mask) return 0;
    c->stopped = 0;
    uint16_t oldsr = c->sr; set_sr_s(c,1);
    push32(c, c->pc); c->a[7]-=2; c->write16(c,c->a[7]&0xFFFFFF,oldsr);
    c->sr = (c->sr & ~0x0700) | (level << 8);
    c->pc = c->read32(c, (24 + level) * 4);   /* autovector */
    return 1;
}

/* MOVEM: register list transfer */
static void do_movem(cpu_m68k_t *c, uint16_t op){
    int dir=(op>>10)&1, sz=(op&0x40)?4:2; int mode=(op>>3)&7, reg=op&7;
    uint16_t list=fetch16(c);
    if (dir==0 && mode==4){ /* registers -> -(An), reversed order */
        uint32_t addr=c->a[reg];
        for (int i=0;i<16;i++) if (list&(1<<i)){
            addr -= sz; uint32_t val = (15-i)<8 ? c->d[15-i] : c->a[(15-i)-8];
            write_mem(c,addr,sz,val);
        }
        c->a[reg]=addr;
    } else {
        ea_t ea; decode_ea(c,mode,reg,sz,&ea);
        uint32_t addr = ea.addr;
        for (int i=0;i<16;i++) if (list&(1<<i)){
            if (dir){ uint32_t v=read_mem(c,addr,sz); if(sz==2) v=(uint32_t)(int32_t)(int16_t)v;
                      if(i<8) c->d[i]=v; else c->a[i-8]=v; }
            else    { uint32_t v = i<8 ? c->d[i] : c->a[i-8]; write_mem(c,addr,sz,v); }
            addr += sz;
        }
        if (mode==3) c->a[reg]=addr;   /* (An)+ writeback for mem->reg */
    }
}

/* shifts / rotates (group E) */
static uint32_t do_shift(cpu_m68k_t *c, int type, int dir, uint32_t v, int cnt, int sz){
    uint32_t m=msk(sz), s=sgn(sz); v&=m; int carry=0;
    for (int i=0;i<cnt;i++){
        switch (type){
            case 0: if(dir){carry=(v&s)?1:0; v=(v<<1)&m;} else {carry=v&1; v=((v>>1)|(v&s))&m;} break; /* ASL/ASR (ASR keeps sign) */
            case 1: if(dir){carry=(v&s)?1:0; v=(v<<1)&m;} else {carry=v&1; v=(v>>1)&m;} break; /* LSL/LSR (logical) */
            case 2: if(dir){carry=(v&s)?1:0; v=((v<<1)|carry)&m;} else {carry=v&1; v=((v>>1)|(carry?s:0))&m;} break; /* ROXL/ROXR */
            default: if(dir){carry=(v&s)?1:0; v=((v<<1)|carry)&m;} else {carry=v&1; v=((v>>1)|(carry?s:0))&m;} break; /* ROL/ROR */
        }
    }
    if (cnt){ c->sr &= ~(M68_C|M68_X); if(carry) c->sr|=(M68_C|M68_X); }
    else c->sr &= ~M68_C;
    setNZ(c,v,sz); c->sr &= ~M68_V;
    return v;
}

int cpu_m68k_step(cpu_m68k_t *c){
    if (c->stopped){ return 4; }
    uint16_t op = fetch16(c);
    c->last_group = op>>12;
    int grp = op>>12;

    /* MOVE.b/.w/.l */
    if (grp==1 || grp==2 || grp==3){
        int sz = grp==1?1 : grp==3?2 : 4;
        ea_t src; decode_ea(c, (op>>3)&7, op&7, sz, &src);
        uint32_t v = read_ea(c,&src,sz);
        int dmode=(op>>6)&7, dreg=(op>>9)&7;
        if (dmode==1){ /* MOVEA */ c->a[dreg] = sz==2 ? (uint32_t)(int32_t)(int16_t)v : v; }
        else { ea_t dst; decode_ea(c,dmode,dreg,sz,&dst); write_ea(c,&dst,sz,v); setNZ(c,v,sz); c->sr&=~(M68_V|M68_C); }
        return 8;
    }
    /* MOVEQ */
    if (grp==7){ int dreg=(op>>9)&7; uint32_t v=(uint32_t)(int32_t)(int8_t)(op&0xFF);
        c->d[dreg]=v; setNZ(c,v,4); c->sr&=~(M68_V|M68_C); return 4; }
    /* Bcc / BRA / BSR */
    if (grp==6){
        int cc=(op>>8)&0xF; int32_t disp=(int8_t)(op&0xFF);
        uint32_t base=c->pc;
        if ((op&0xFF)==0) disp=(int16_t)fetch16(c);
        if (cc==1){ /* BSR */ push32(c, c->pc); c->pc=base+disp; return 18; }
        if (cond(c,cc)) c->pc=base+disp;
        return 10;
    }
    /* ADDQ/SUBQ/Scc/DBcc */
    if (grp==5){
        if ((op&0xC0)==0xC0){ int cc=(op>>8)&0xF, mode=(op>>3)&7, reg=op&7;
            if (mode==1){ /* DBcc: disp relative to the displacement-word address */
                uint32_t base=c->pc; int16_t d=(int16_t)fetch16(c);
                if (!cond(c,cc)){ uint16_t v=(c->d[reg]&0xFFFF)-1; c->d[reg]=(c->d[reg]&~0xFFFF)|v;
                    if (v!=0xFFFF) c->pc=base+d; } return 10; }
            ea_t ea; decode_ea(c,mode,reg,1,&ea); write_ea(c,&ea,1, cond(c,cc)?0xFF:0x00); return 8; }
        int data=(op>>9)&7; if(data==0)data=8; int sz=1<<((op>>6)&3); int sub=(op>>8)&1;
        ea_t ea; decode_ea(c,(op>>3)&7,op&7,sz,&ea);
        if (ea.kind==1){ c->a[ea.reg] += sub?-data:data; return 8; }
        uint32_t v=read_ea(c,&ea,sz); v = sub?do_sub(c,v,data,sz,0,0):do_add(c,v,data,sz,0);
        write_ea(c,&ea,sz,v); return 8;
    }
    /* immediates + bit ops (group 0) */
    if (grp==0){
        /* ORI/ANDI/EORI to CCR/SR (interrupt-mask management — very common) */
        if (op==0x003C){ c->sr=(c->sr&0xFF00)|((c->sr|fetch16(c))&0xFF); return 20; } /* ORI CCR  */
        if (op==0x007C){ set_sr(c, c->sr|fetch16(c)); return 20; }                     /* ORI SR   */
        if (op==0x023C){ c->sr=(c->sr&0xFF00)|((c->sr&fetch16(c))&0xFF); return 20; }  /* ANDI CCR */
        if (op==0x027C){ set_sr(c, c->sr&fetch16(c)); return 20; }                     /* ANDI SR  */
        if (op==0x0A3C){ c->sr=(c->sr&0xFF00)|((c->sr^fetch16(c))&0xFF); return 20; }  /* EORI CCR */
        if (op==0x0A7C){ set_sr(c, c->sr^fetch16(c)); return 20; }                     /* EORI SR  */
        int sz=1<<((op>>6)&3);
        if (((op>>8)&0xF)==0 || ((op>>8)&0xF)==2 || ((op>>8)&0xF)==4 || ((op>>8)&0xF)==6 ||
            ((op>>8)&0xF)==0xA || ((op>>8)&0xF)==0xC){
            if ((op&0x00C0)!=0x00C0){
                uint32_t imm = sz==4?fetch32(c):(uint32_t)fetch16(c)&(sz==1?0xFF:0xFFFF);
                ea_t ea; decode_ea(c,(op>>3)&7,op&7,sz,&ea); uint32_t v=read_ea(c,&ea,sz), r;
                switch ((op>>8)&0xF){
                    case 0x0: r=v|imm; write_ea(c,&ea,sz,r); logic_flags(c,r,sz); break;   /* ORI */
                    case 0x2: r=v&imm; write_ea(c,&ea,sz,r); logic_flags(c,r,sz); break;   /* ANDI */
                    case 0x4: r=do_sub(c,v,imm,sz,0,0); write_ea(c,&ea,sz,r); break;       /* SUBI */
                    case 0x6: r=do_add(c,v,imm,sz,0); write_ea(c,&ea,sz,r); break;         /* ADDI */
                    case 0xA: r=v^imm; write_ea(c,&ea,sz,r); logic_flags(c,r,sz); break;   /* EORI */
                    case 0xC: do_sub(c,v,imm,sz,0,1); break;                               /* CMPI */
                }
                return 12;
            }
        }
        /* bit ops: BTST/BCHG/BCLR/BSET (immediate or Dn bit number) */
        {
            int type=(op>>6)&3; int mode=(op>>3)&7, reg=op&7; uint32_t bit;
            int dynamic=(op&0x0100);
            if (dynamic) bit=c->d[(op>>9)&7]; else bit=fetch16(c);
            int sz = (mode==0)?4:1; bit &= (mode==0)?31:7;
            ea_t ea; decode_ea(c,mode,reg,sz,&ea); uint32_t v=read_ea(c,&ea,sz);
            c->sr = (c->sr&~M68_Z) | (((v>>bit)&1)?0:M68_Z);
            switch (type){ case 1: v^=(1u<<bit); write_ea(c,&ea,sz,v); break;   /* BCHG */
                           case 2: v&=~(1u<<bit); write_ea(c,&ea,sz,v); break;  /* BCLR */
                           case 3: v|=(1u<<bit); write_ea(c,&ea,sz,v); break;   /* BSET */
                           default: break; }                                     /* BTST */
            return 8;
        }
    }
    /* OR/AND/EOR/CMP/ADD/SUB families */
    if (grp==0x8 || grp==0xC || grp==0x9 || grp==0xD || grp==0xB){
        int dreg=(op>>9)&7, opmode=(op>>6)&7, mode=(op>>3)&7, reg=op&7;
        /* MULU/MULS (C, opmode 3/7 with size word) */
        if (grp==0xC && (opmode==3||opmode==7)){ ea_t ea; decode_ea(c,mode,reg,2,&ea);
            uint32_t s=read_ea(c,&ea,2); uint32_t r;
            if (opmode==3) r=(c->d[dreg]&0xFFFF)*(s&0xFFFF);
            else r=(uint32_t)((int32_t)(int16_t)(c->d[dreg]&0xFFFF)*(int32_t)(int16_t)s);
            c->d[dreg]=r; setNZ(c,r,4); c->sr&=~(M68_V|M68_C); return 40; }
        if (grp==0x8 && (opmode==3||opmode==7)){ ea_t ea; decode_ea(c,mode,reg,2,&ea); /* DIVU/DIVS */
            uint32_t dv=read_ea(c,&ea,2)&0xFFFF; if(dv==0){ trap(c,5); return 10; }
            if (opmode==3){ uint32_t q=c->d[dreg]/dv, rem=c->d[dreg]%dv;
                if(q>0xFFFF){c->sr|=M68_V;} else { c->d[dreg]=(rem<<16)|(q&0xFFFF); setNZ(c,q,2); c->sr&=~(M68_V|M68_C);} }
            else { int32_t q=(int32_t)c->d[dreg]/(int32_t)(int16_t)dv, rem=(int32_t)c->d[dreg]%(int32_t)(int16_t)dv;
                c->d[dreg]=((uint32_t)(rem&0xFFFF)<<16)|((uint32_t)q&0xFFFF); setNZ(c,(uint32_t)q,2); c->sr&=~(M68_V|M68_C); }
            return 80; }
        /* ADDA/SUBA/CMPA (address dest, opmode 3=word 7=long) */
        if ((grp==0xD||grp==0x9||grp==0xB) && (opmode==3||opmode==7)){
            int sz=opmode==3?2:4; ea_t ea; decode_ea(c,mode,reg,sz,&ea);
            uint32_t s=read_ea(c,&ea,sz); if(sz==2) s=(uint32_t)(int32_t)(int16_t)s;
            if (grp==0xD) c->a[dreg]+=s; else if (grp==0x9) c->a[dreg]-=s;
            else do_sub(c,c->a[dreg],s,4,0,1);
            return 8; }
        /* standard <ea> op Dn / Dn op <ea> */
        int sz=1<<(opmode&3); int dir=(opmode&4);
        ea_t ea; decode_ea(c,mode,reg,sz,&ea); uint32_t s=read_ea(c,&ea,sz), dn=c->d[dreg]&msk(sz), r;
        switch (grp){
            case 0x8: r=dir?(s|dn):(dn|s); logic_flags(c,r,sz); if(dir)write_ea(c,&ea,sz,r); else c->d[dreg]=(c->d[dreg]&~msk(sz))|(r&msk(sz)); break; /* OR */
            case 0xC: r=dir?(s&dn):(dn&s); logic_flags(c,r,sz); if(dir)write_ea(c,&ea,sz,r); else c->d[dreg]=(c->d[dreg]&~msk(sz))|(r&msk(sz)); break; /* AND */
            case 0xD: r=do_add(c,dn,s,sz,0); if(dir)write_ea(c,&ea,sz,r); else c->d[dreg]=(c->d[dreg]&~msk(sz))|(r&msk(sz)); break; /* ADD */
            case 0x9: r=do_sub(c,dn,s,sz,0,0); if(dir)write_ea(c,&ea,sz,r); else c->d[dreg]=(c->d[dreg]&~msk(sz))|(r&msk(sz)); break; /* SUB */
            default:  /* B: CMP (dir=0) / EOR (dir=1) */
                if (dir){ r=s^dn; write_ea(c,&ea,sz,r); logic_flags(c,r,sz); }
                else do_sub(c,dn,s,sz,0,1);
                break;
        }
        return 8;
    }
    /* shifts / rotates */
    if (grp==0xE){
        if ((op&0xC0)==0xC0){ /* memory shift by 1 */ int type=(op>>9)&3,dir=(op>>8)&1;
            ea_t ea; decode_ea(c,(op>>3)&7,op&7,2,&ea); uint32_t v=read_ea(c,&ea,2);
            v=do_shift(c,type,dir,v,1,2); write_ea(c,&ea,2,v); return 8; }
        int sz=1<<((op>>6)&3); int dir=(op>>8)&1; int ir=(op>>5)&1; int cr=(op>>9)&7; int type=(op>>3)&3;
        int cnt = ir ? (int)(c->d[cr]&63) : (cr==0?8:cr);
        int reg=op&7; uint32_t v=c->d[reg]&msk(sz);
        v=do_shift(c,type,dir,v,cnt,sz);
        c->d[reg]=(c->d[reg]&~msk(sz))|(v&msk(sz));
        return 6;
    }
    /* group 4: misc — ordered most-specific first */
    if (grp==4){
        if (op==0x4E75){ c->pc=pop32(c); return 16; }                             /* RTS */
        if (op==0x4E71){ return 4; }                                              /* NOP */
        if (op==0x4E72){ c->sr=fetch16(c); c->stopped=1; return 4; }              /* STOP */
        if (op==0x4E73){ uint16_t ns=c->read16(c,c->a[7]&0xFFFFFF); c->a[7]+=2; uint32_t np=pop32(c);
            set_sr_s(c,(ns&M68_S)?1:0); c->sr=ns; c->pc=np; return 20; }          /* RTE */
        if (op==0x4E77){ c->sr=(c->sr&0xFF00)|(c->read16(c,c->a[7]&0xFFFFFF)&0xFF); c->a[7]+=2; c->pc=pop32(c); return 20; } /* RTR */
        if ((op&0xFFF0)==0x4E60){ int reg=op&7; if(op&8) c->a[reg]=c->usp; else c->usp=c->a[reg]; return 4; } /* MOVE USP */
        if ((op&0xFFF8)==0x4E50){ int reg=op&7; int16_t d=(int16_t)fetch16(c);    /* LINK */
            push32(c,c->a[reg]); c->a[reg]=c->a[7]; c->a[7]+=d; return 16; }
        if ((op&0xFFF8)==0x4E58){ int reg=op&7; c->a[7]=c->a[reg]; c->a[reg]=pop32(c); return 12; } /* UNLK */
        if ((op&0xFFF0)==0x4E40){ trap(c, 32 + (op&0xF)); return 34; }            /* TRAP #n */
        if ((op&0xFFC0)==0x4E80){ ea_t ea; decode_ea(c,(op>>3)&7,op&7,4,&ea); push32(c,c->pc); c->pc=ea.addr; return 18; } /* JSR */
        if ((op&0xFFC0)==0x4EC0){ ea_t ea; decode_ea(c,(op>>3)&7,op&7,4,&ea); c->pc=ea.addr; return 8; }  /* JMP */
        if ((op&0xF1C0)==0x41C0){ int areg=(op>>9)&7; ea_t ea; decode_ea(c,(op>>3)&7,op&7,4,&ea); c->a[areg]=ea.addr; return 4; } /* LEA */
        if ((op&0xFFF8)==0x4840){ int reg=op&7; uint32_t v=c->d[reg]; c->d[reg]=(v>>16)|(v<<16); /* SWAP (mode 0) */
            setNZ(c,c->d[reg],4); c->sr&=~(M68_V|M68_C); return 4; }
        if ((op&0xFFC0)==0x4840){ ea_t ea; decode_ea(c,(op>>3)&7,op&7,4,&ea); push32(c,ea.addr); return 12; } /* PEA */
        if ((op&0xFFB8)==0x4880){ int reg=op&7; int sz=(op&0x40)?4:2; /* EXT.W/EXT.L (mask ignores the size bit) */
            if ((op&0x38)==0){
                if (sz==2){ uint32_t v=(uint32_t)(int32_t)(int8_t)(c->d[reg]&0xFF); c->d[reg]=(c->d[reg]&0xFFFF0000)|(v&0xFFFF); setNZ(c,v,2);}
                else { c->d[reg]=(uint32_t)(int32_t)(int16_t)(c->d[reg]&0xFFFF); setNZ(c,c->d[reg],4);}
                c->sr&=~(M68_V|M68_C); return 4;
            }
        }
        if ((op&0xFB80)==0x4880){ do_movem(c,op); return 24; }                    /* MOVEM */
        /* SR/CCR moves + TAS (the size-3 forms of the CLR/NEG/NOT/TST line) */
        if ((op&0xFFC0)==0x40C0){ ea_t ea; decode_ea(c,(op>>3)&7,op&7,2,&ea); write_ea(c,&ea,2,c->sr); return 6; }        /* MOVE SR,<ea> */
        if ((op&0xFFC0)==0x42C0){ ea_t ea; decode_ea(c,(op>>3)&7,op&7,2,&ea); write_ea(c,&ea,2,c->sr&0xFF); return 6; }   /* MOVE CCR,<ea> */
        if ((op&0xFFC0)==0x44C0){ ea_t ea; decode_ea(c,(op>>3)&7,op&7,2,&ea); c->sr=(c->sr&0xFF00)|(read_ea(c,&ea,2)&0xFF); return 12; } /* MOVE <ea>,CCR */
        if ((op&0xFFC0)==0x46C0){ ea_t ea; decode_ea(c,(op>>3)&7,op&7,2,&ea); set_sr(c,(uint16_t)read_ea(c,&ea,2)); return 12; } /* MOVE <ea>,SR */
        if ((op&0xFFC0)==0x4AC0){ ea_t ea; decode_ea(c,(op>>3)&7,op&7,1,&ea); uint32_t v=read_ea(c,&ea,1); /* TAS */
            logic_flags(c,v,1); write_ea(c,&ea,1,v|0x80); return 10; }
        if ((op&0xFF00)==0x4200 && (op&0xC0)!=0xC0){ int sz=1<<((op>>6)&3); ea_t ea; decode_ea(c,(op>>3)&7,op&7,sz,&ea); /* CLR */
            write_ea(c,&ea,sz,0); c->sr=(c->sr&~(M68_N|M68_V|M68_C))|M68_Z; return 6; }
        if ((op&0xFFC0)==0x4A00 || (op&0xFFC0)==0x4A40 || (op&0xFFC0)==0x4A80){ int sz=1<<((op>>6)&3); /* TST */
            ea_t ea; decode_ea(c,(op>>3)&7,op&7,sz,&ea); uint32_t v=read_ea(c,&ea,sz); logic_flags(c,v,sz); return 4; }
        if ((op&0xFF00)==0x4400 && (op&0xC0)!=0xC0){ int sz=1<<((op>>6)&3); ea_t ea; decode_ea(c,(op>>3)&7,op&7,sz,&ea); /* NEG */
            uint32_t v=read_ea(c,&ea,sz); uint32_t r=do_sub(c,0,v,sz,0,0); write_ea(c,&ea,sz,r); return 6; }
        if ((op&0xFF00)==0x4600 && (op&0xC0)!=0xC0){ int sz=1<<((op>>6)&3); ea_t ea; decode_ea(c,(op>>3)&7,op&7,sz,&ea); /* NOT */
            uint32_t v=~read_ea(c,&ea,sz); write_ea(c,&ea,sz,v); logic_flags(c,v,sz); return 6; }
        c->illegal++; return 4;
    }

    c->illegal++;
    return 4;
}

uint64_t cpu_m68k_run(cpu_m68k_t *c, uint64_t max_cycles){
    uint64_t start=c->cycles;
    while (c->cycles-start < max_cycles && !c->stopped){ int t=cpu_m68k_step(c); c->cycles+=t; }
    return c->cycles-start;
}
