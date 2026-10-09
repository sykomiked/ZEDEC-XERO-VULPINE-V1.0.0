/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cpu_arm7.c — ARM7TDMI (ARMv4T) interpreter: ARM + THUMB. See cpu_arm7.h.
 * Covers the instruction classes GBA boot + game code use; rarer encodings are
 * counted in `illegal`. PC (r[15]) is kept pointing at the fetched instruction
 * and read as +8 (ARM) / +4 (THUMB) — the pipeline. */
#include "cpu_arm7.h"

static int midx(uint32_t mode){
    switch (mode){ case MODE_FIQ:return 0; case MODE_IRQ:return 1; case MODE_SVC:return 2;
                   case MODE_ABT:return 3; case MODE_UND:return 4; default:return 5; }
}
static void set_mode(cpu_arm7_t *c, uint32_t nm){
    uint32_t om = c->cpsr & 0x1F; if (om == nm) return;
    int oi = midx(om), ni = midx(nm);
    c->bank_sp[oi]=c->r[13]; c->bank_lr[oi]=c->r[14]; c->bank_spsr[oi]=c->spsr;
    c->r[13]=c->bank_sp[ni]; c->r[14]=c->bank_lr[ni]; c->spsr=c->bank_spsr[ni];
    c->cpsr = (c->cpsr & ~0x1Fu) | nm;
}

static inline void setNZ(cpu_arm7_t *c, uint32_t v){
    c->cpsr = (c->cpsr & ~(ARM_N|ARM_Z)) | (v & ARM_N) | (v==0 ? ARM_Z : 0);
}
static inline void setC(cpu_arm7_t *c, int on){ c->cpsr = on ? (c->cpsr|ARM_C) : (c->cpsr&~ARM_C); }
static inline void setV(cpu_arm7_t *c, int on){ c->cpsr = on ? (c->cpsr|ARM_V) : (c->cpsr&~ARM_V); }

static int cond_pass(cpu_arm7_t *c, uint32_t cond){
    uint32_t p = c->cpsr;
    int N=!!(p&ARM_N), Z=!!(p&ARM_Z), C=!!(p&ARM_C), V=!!(p&ARM_V);
    switch (cond){
        case 0x0: return Z;            case 0x1: return !Z;
        case 0x2: return C;            case 0x3: return !C;
        case 0x4: return N;            case 0x5: return !N;
        case 0x6: return V;            case 0x7: return !V;
        case 0x8: return C && !Z;      case 0x9: return !C || Z;
        case 0xA: return N==V;         case 0xB: return N!=V;
        case 0xC: return !Z && (N==V); case 0xD: return Z || (N!=V);
        default:  return 1;            /* AL / undefined-as-always */
    }
}

/* barrel shifter for a register operand; updates *carry with the shifter carry */
static uint32_t bshift(cpu_arm7_t *c, uint32_t val, uint32_t type, uint32_t amt, int by_reg, int *carry){
    int cin = !!(c->cpsr & ARM_C);
    if (by_reg && amt == 0){ *carry = cin; return val; }           /* Rs==0: unchanged */
    switch (type){
        case 0: /* LSL */
            if (amt == 0){ *carry = cin; return val; }
            if (amt < 32){ *carry = (val >> (32-amt)) & 1; return val << amt; }
            if (amt == 32){ *carry = val & 1; return 0; }
            *carry = 0; return 0;
        case 1: /* LSR */
            if (amt == 0 || amt == 32){ *carry = (val>>31)&1; return 0; }
            if (amt < 32){ *carry = (val >> (amt-1)) & 1; return val >> amt; }
            *carry = 0; return 0;
        case 2: /* ASR */
            if (amt == 0 || amt >= 32){ *carry = (val>>31)&1; return (val&0x80000000)?0xFFFFFFFF:0; }
            *carry = (val >> (amt-1)) & 1; return (uint32_t)((int32_t)val >> amt);
        default: /* ROR */
            if (amt == 0){ /* RRX */ *carry = val & 1; return (val >> 1) | (cin << 31); }
            amt &= 31; if (amt == 0){ *carry = (val>>31)&1; return val; }
            *carry = (val >> (amt-1)) & 1; return (val >> amt) | (val << (32-amt));
    }
}

static void add_flags(cpu_arm7_t *c, uint32_t a, uint32_t b, uint32_t r){
    setC(c, r < a); setV(c, (~(a^b) & (a^r)) >> 31);
}
static void sub_flags(cpu_arm7_t *c, uint32_t a, uint32_t b, uint32_t r){
    setC(c, a >= b); setV(c, ((a^b) & (a^r)) >> 31);
}

void cpu_arm7_reset(cpu_arm7_t *c, uint32_t entry){
    for (int i=0;i<16;i++) c->r[i]=0;
    c->cpsr = MODE_SVC | ARM_I | ARM_F;
    c->spsr = 0; c->cycles = 0; c->halted = 0; c->illegal = 0;
    for (int i=0;i<8;i++){ c->bank_sp[i]=0; c->bank_lr[i]=0; c->bank_spsr[i]=0; }
    c->r[13] = 0x03007F00;      /* a sane default SP (IWRAM) */
    c->r[15] = entry;
}

int cpu_arm7_irq(cpu_arm7_t *c){
    if (c->cpsr & ARM_I) return 0;
    uint32_t ret = c->r[15];               /* next instruction to execute */
    uint32_t saved = c->cpsr;
    set_mode(c, MODE_IRQ);
    c->spsr = saved;
    c->r[14] = ret + 4;                    /* LR_irq; BIOS does SUBS PC,LR,#4 */
    c->cpsr |= ARM_I;
    c->cpsr &= ~ARM_T;
    c->r[15] = 0x18;                       /* BIOS IRQ vector */
    c->halted = 0;
    return 1;
}

/* restore CPSR from SPSR (mode/flags) on a return-to-PC with S bit */
static void restore_cpsr(cpu_arm7_t *c){
    uint32_t s = c->spsr;
    set_mode(c, s & 0x1F);
    c->cpsr = s;
}

/* ---------------- ARM mode ---------------- */
static void exec_arm(cpu_arm7_t *c, uint32_t op){
    uint32_t cond = op >> 28;
    if (!cond_pass(c, cond)) return;

    /* Branch and Exchange: BX Rn */
    if ((op & 0x0FFFFFF0) == 0x012FFF10){
        uint32_t rn = c->r[op & 0xF];
        if (rn & 1){ c->cpsr |= ARM_T; c->r[15] = rn & ~1u; }
        else { c->cpsr &= ~ARM_T; c->r[15] = rn & ~3u; }
        return;
    }
    /* Branch / Branch-with-link */
    if ((op & 0x0E000000) == 0x0A000000){
        int32_t off = (int32_t)(op << 8) >> 6;         /* sign-extend 24-bit <<2 */
        if (op & 0x01000000) c->r[14] = c->r[15] - 4;  /* BL: LR = next insn */
        c->r[15] = c->r[15] + off;
        return;
    }
    /* Multiply / multiply-accumulate */
    if ((op & 0x0FC000F0) == 0x00000090){
        int rd=(op>>16)&0xF, rn=(op>>12)&0xF, rs=(op>>8)&0xF, rm=op&0xF;
        uint32_t res = c->r[rm] * c->r[rs];
        if (op & 0x00200000) res += c->r[rn];          /* MLA */
        c->r[rd] = res;
        if (op & 0x00100000) setNZ(c, res);
        return;
    }
    /* Multiply long */
    if ((op & 0x0F8000F0) == 0x00800090){
        int rdhi=(op>>16)&0xF, rdlo=(op>>12)&0xF, rs=(op>>8)&0xF, rm=op&0xF;
        uint64_t res;
        if (op & 0x00400000) res = (uint64_t)((int64_t)(int32_t)c->r[rm] * (int64_t)(int32_t)c->r[rs]);
        else                 res = (uint64_t)c->r[rm] * (uint64_t)c->r[rs];
        if (op & 0x00200000) res += ((uint64_t)c->r[rdhi]<<32) | c->r[rdlo];
        c->r[rdlo]=(uint32_t)res; c->r[rdhi]=(uint32_t)(res>>32);
        if (op & 0x00100000){ setNZ(c, (uint32_t)(res>>32) | ((res&0xFFFFFFFF)?1:0) ? (uint32_t)(res>>32):0);
            c->cpsr = (c->cpsr&~(ARM_N|ARM_Z)) | ((res>>63)?ARM_N:0) | (res==0?ARM_Z:0); }
        return;
    }
    /* MRS */
    if ((op & 0x0FBF0FFF) == 0x010F0000){
        int rd=(op>>12)&0xF; c->r[rd] = (op&0x00400000) ? c->spsr : c->cpsr; return;
    }
    /* MSR (register or immediate to CPSR/SPSR) */
    if ((op & 0x0DB0F000) == 0x0120F000){
        uint32_t val;
        if (op & 0x02000000){ uint32_t imm=op&0xFF, rot=((op>>8)&0xF)*2; val=(imm>>rot)|(imm<<(32-rot)); }
        else val = c->r[op&0xF];
        uint32_t mask = 0;
        if (op & 0x00080000) mask |= 0xFF000000;
        if (op & 0x00010000) mask |= 0x000000FF;       /* control byte (privileged) */
        int spsr = (op & 0x00400000);
        if (spsr){ c->spsr = (c->spsr & ~mask) | (val & mask); }
        else {
            if ((op & 0x00010000) && (c->cpsr & 0x1F) != MODE_USR) set_mode(c, val & 0x1F);
            c->cpsr = (c->cpsr & ~mask) | (val & mask);
        }
        return;
    }
    /* Halfword / signed byte transfer */
    if ((op & 0x0E000090) == 0x00000090 && (op & 0x60)){
        int rn=(op>>16)&0xF, rd=(op>>12)&0xF; int pre=!!(op&0x01000000), up=!!(op&0x00800000);
        int wb=!!(op&0x00200000), load=!!(op&0x00100000); int sh=(op>>5)&3;
        uint32_t offset = (op&0x00400000) ? (((op>>4)&0xF0)|(op&0xF)) : c->r[op&0xF];
        uint32_t base = c->r[rn]; uint32_t addr = base + (up?offset:-offset);
        uint32_t ea = pre ? addr : base;
        if (load){
            uint32_t v;
            if (sh==1) v = c->read16(c, ea & ~1u);
            else if (sh==2) v = (uint32_t)(int32_t)(int8_t)c->read8(c, ea);
            else v = (uint32_t)(int32_t)(int16_t)c->read16(c, ea & ~1u);
            c->r[rd]=v;
        } else {
            c->write16(c, ea & ~1u, (uint16_t)c->r[rd]);
        }
        if (!pre) addr = base + (up?offset:-offset);
        if ((!pre || wb) && rn != rd) c->r[rn] = pre ? addr : addr;
        return;
    }
    /* Data processing */
    if ((op & 0x0C000000) == 0x00000000){
        uint32_t opc=(op>>21)&0xF; int S=!!(op&0x00100000);
        int rn=(op>>16)&0xF, rd=(op>>12)&0xF; int carry=!!(c->cpsr&ARM_C);
        uint32_t op2;
        if (op & 0x02000000){ uint32_t imm=op&0xFF, rot=((op>>8)&0xF)*2;
            op2 = rot ? ((imm>>rot)|(imm<<(32-rot))) : imm;
            if (rot) carry = (op2>>31)&1; }
        else {
            uint32_t rm=c->r[op&0xF], type=(op>>5)&3, amt; int by_reg=!!(op&0x10);
            if (by_reg){ amt=c->r[(op>>8)&0xF]&0xFF; } else { amt=(op>>7)&0x1F; }
            op2 = bshift(c, rm, type, amt, by_reg, &carry);
        }
        uint32_t a=c->r[rn], res=0; int logical=0, write=1;
        switch (opc){
            case 0x0: res=a&op2; logical=1; break;                 /* AND */
            case 0x1: res=a^op2; logical=1; break;                 /* EOR */
            case 0x2: res=a-op2; if(S)sub_flags(c,a,op2,res); break;/* SUB */
            case 0x3: res=op2-a; if(S)sub_flags(c,op2,a,res); break;/* RSB */
            case 0x4: res=a+op2; if(S)add_flags(c,a,op2,res); break;/* ADD */
            case 0x5: { uint32_t cf=!!(c->cpsr&ARM_C); uint64_t t=(uint64_t)a+op2+cf; res=(uint32_t)t;
                        if(S){ setC(c,t>>32); setV(c,(~(a^op2)&(a^res))>>31);} } break; /* ADC */
            case 0x6: { uint32_t cf=!!(c->cpsr&ARM_C); uint64_t t=(uint64_t)a-op2-(1-cf); res=(uint32_t)t;
                        if(S){ setC(c,a>=(uint64_t)op2+(1-cf)); setV(c,((a^op2)&(a^res))>>31);} } break; /* SBC */
            case 0x7: { uint32_t cf=!!(c->cpsr&ARM_C); uint64_t t=(uint64_t)op2-a-(1-cf); res=(uint32_t)t;
                        if(S){ setC(c,op2>=(uint64_t)a+(1-cf)); setV(c,((op2^a)&(op2^res))>>31);} } break; /* RSC */
            case 0x8: res=a&op2; logical=1; write=0; break;        /* TST */
            case 0x9: res=a^op2; logical=1; write=0; break;        /* TEQ */
            case 0xA: res=a-op2; sub_flags(c,a,op2,res); write=0; S=1; break; /* CMP */
            case 0xB: res=a+op2; add_flags(c,a,op2,res); write=0; S=1; break; /* CMN */
            case 0xC: res=a|op2; logical=1; break;                 /* ORR */
            case 0xD: res=op2; logical=1; break;                   /* MOV */
            case 0xE: res=a&~op2; logical=1; break;                /* BIC */
            case 0xF: res=~op2; logical=1; break;                  /* MVN */
        }
        if (S && logical){ setNZ(c,res); setC(c,carry); }
        else if (S && write) setNZ(c,res);
        if (write){
            c->r[rd]=res;
            if (rd==15){ if (S) restore_cpsr(c); c->r[15] &= (c->cpsr&ARM_T)? ~1u : ~3u; }
        }
        return;
    }
    /* Single data transfer LDR/STR */
    if ((op & 0x0C000000) == 0x04000000){
        int rn=(op>>16)&0xF, rd=(op>>12)&0xF;
        int pre=!!(op&0x01000000), up=!!(op&0x00800000), byte=!!(op&0x00400000);
        int wb=!!(op&0x00200000), load=!!(op&0x00100000);
        uint32_t offset;
        if (op & 0x02000000){ uint32_t rm=c->r[op&0xF], type=(op>>5)&3, amt=(op>>7)&0x1F; int cy;
            offset = bshift(c, rm, type, amt, 0, &cy); }
        else offset = op & 0xFFF;
        uint32_t base=c->r[rn]; uint32_t addr = base + (up?offset:-offset);
        uint32_t ea = pre ? addr : base;
        if (load){
            uint32_t v = byte ? c->read8(c,ea) : c->read32(c, ea & ~3u);
            if (!byte && (ea&3)){ uint32_t r=(ea&3)*8; v=(v>>r)|(v<<(32-r)); }  /* rotated */
            c->r[rd]=v; if (rd==15) c->r[15]&= ~1u;
        } else {
            uint32_t v=c->r[rd]; if (rd==15) v+=4;
            if (byte) c->write8(c,ea,(uint8_t)v); else c->write32(c, ea & ~3u, v);
        }
        if (!pre) addr = base + (up?offset:-offset);
        if ((!pre || wb) && !(load && rn==rd)) c->r[rn]=addr;
        return;
    }
    /* Block data transfer LDM/STM */
    if ((op & 0x0E000000) == 0x08000000){
        int rn=(op>>16)&0xF; int pre=!!(op&0x01000000), up=!!(op&0x00800000);
        int wb=!!(op&0x00200000), load=!!(op&0x00100000);
        uint32_t list=op&0xFFFF; uint32_t base=c->r[rn]; int n=0;
        for (int i=0;i<16;i++) if (list&(1<<i)) n++;
        uint32_t addr = up ? base : base - n*4;
        uint32_t final = up ? base + n*4 : base - n*4;
        for (int i=0;i<16;i++){
            if (!(list&(1<<i))) continue;
            uint32_t ea = addr + (pre==up ? 4 : 0);
            if (load){ c->r[i]=c->read32(c, ea & ~3u); if (i==15) c->r[15]&= ~1u; }
            else { uint32_t v=c->r[i]; if(i==15) v+=4; c->write32(c, ea & ~3u, v); }
            addr += 4;
        }
        if (wb) c->r[rn]=final;
        if (load && (list&0x8000) && (op&0x00400000)) restore_cpsr(c);
        return;
    }
    /* SWI (BIOS call) — dispatch to the BIOS handler if the machine set one. */
    if ((op & 0x0F000000) == 0x0F000000){ if (c->swi) c->swi(c, (op >> 16) & 0xFF); return; }

    c->illegal++;
}

/* ---------------- THUMB mode ---------------- */
static void exec_thumb(cpu_arm7_t *c, uint16_t op){
    if ((op & 0xF800) == 0x1800){            /* ADD/SUB (fmt 2) */
        int rd=op&7, rs=(op>>3)&7; uint32_t a=c->r[rs];
        uint32_t b = (op&0x0400) ? ((op>>6)&7) : c->r[(op>>6)&7];
        uint32_t r = (op&0x0200) ? a-b : a+b;
        c->r[rd]=r; setNZ(c,r);
        if (op&0x0200) sub_flags(c,a,b,r); else add_flags(c,a,b,r);
        return;
    }
    if ((op & 0xE000) == 0x0000){            /* move shifted register (fmt 1) */
        int rd=op&7, rs=(op>>3)&7, off=(op>>6)&0x1F, type=(op>>11)&3; int cy;
        uint32_t r=bshift(c, c->r[rs], type, off, (type!=0&&off==0)?0:0, &cy);
        /* LSR/ASR by 0 mean 32 in THUMB fmt1 */
        if (type!=0 && off==0){ r=bshift(c, c->r[rs], type, 32, 0, &cy); }
        c->r[rd]=r; setNZ(c,r); setC(c,cy);
        return;
    }
    if ((op & 0xE000) == 0x2000){            /* mov/cmp/add/sub imm (fmt 3) */
        int rd=(op>>8)&7; uint32_t imm=op&0xFF, a=c->r[rd], r; int sub=0;
        switch ((op>>11)&3){
            case 0: r=imm; c->r[rd]=r; setNZ(c,r); return;                 /* MOV */
            case 1: r=a-imm; setNZ(c,r); sub_flags(c,a,imm,r); return;     /* CMP */
            case 2: r=a+imm; c->r[rd]=r; setNZ(c,r); add_flags(c,a,imm,r); return; /* ADD */
            default: r=a-imm; c->r[rd]=r; setNZ(c,r); sub_flags(c,a,imm,r); (void)sub; return; /* SUB */
        }
    }
    if ((op & 0xFC00) == 0x4000){            /* ALU ops (fmt 4) */
        int rd=op&7, rs=(op>>3)&7; uint32_t a=c->r[rd], b=c->r[rs], r; int cy;
        switch ((op>>6)&0xF){
            case 0x0: r=a&b; c->r[rd]=r; setNZ(c,r); break;                 /* AND */
            case 0x1: r=a^b; c->r[rd]=r; setNZ(c,r); break;                 /* EOR */
            case 0x2: r=bshift(c,a,0,b&0xFF,1,&cy); c->r[rd]=r; setNZ(c,r); setC(c,cy); break; /* LSL */
            case 0x3: r=bshift(c,a,1,b&0xFF,1,&cy); c->r[rd]=r; setNZ(c,r); setC(c,cy); break; /* LSR */
            case 0x4: r=bshift(c,a,2,b&0xFF,1,&cy); c->r[rd]=r; setNZ(c,r); setC(c,cy); break; /* ASR */
            case 0x5: { uint32_t cf=!!(c->cpsr&ARM_C); uint64_t t=(uint64_t)a+b+cf; r=(uint32_t)t;
                        c->r[rd]=r; setNZ(c,r); setC(c,t>>32); setV(c,(~(a^b)&(a^r))>>31);} break; /* ADC */
            case 0x6: { uint32_t cf=!!(c->cpsr&ARM_C); uint64_t t=(uint64_t)a-b-(1-cf); r=(uint32_t)t;
                        c->r[rd]=r; setNZ(c,r); setC(c,a>=(uint64_t)b+(1-cf)); setV(c,((a^b)&(a^r))>>31);} break; /* SBC */
            case 0x7: r=bshift(c,a,3,b&0xFF,1,&cy); c->r[rd]=r; setNZ(c,r); setC(c,cy); break; /* ROR */
            case 0x8: r=a&b; setNZ(c,r); break;                             /* TST */
            case 0x9: r=(uint32_t)-(int32_t)b; c->r[rd]=r; setNZ(c,r); sub_flags(c,0,b,r); break; /* NEG */
            case 0xA: r=a-b; setNZ(c,r); sub_flags(c,a,b,r); break;         /* CMP */
            case 0xB: r=a+b; setNZ(c,r); add_flags(c,a,b,r); break;         /* CMN */
            case 0xC: r=a|b; c->r[rd]=r; setNZ(c,r); break;                 /* ORR */
            case 0xD: r=a*b; c->r[rd]=r; setNZ(c,r); break;                 /* MUL */
            case 0xE: r=a&~b; c->r[rd]=r; setNZ(c,r); break;                /* BIC */
            case 0xF: r=~b; c->r[rd]=r; setNZ(c,r); break;                  /* MVN */
        }
        return;
    }
    if ((op & 0xFC00) == 0x4400){            /* hi register ops / BX (fmt 5) */
        int rd=(op&7)|((op>>4)&8), rs=((op>>3)&0xF); uint32_t vs=c->r[rs];
        switch ((op>>8)&3){
            case 0: c->r[rd]+=vs; if(rd==15) c->r[15]&=~1u; return;         /* ADD */
            case 1: { uint32_t a=c->r[rd]; uint32_t r=a-vs; setNZ(c,r); sub_flags(c,a,vs,r); return; } /* CMP */
            case 2: c->r[rd]=vs; if(rd==15) c->r[15]&=~1u; return;          /* MOV */
            default: /* BX */
                if (vs & 1){ c->cpsr|=ARM_T; c->r[15]=vs&~1u; }
                else { c->cpsr&=~ARM_T; c->r[15]=vs&~3u; }
                return;
        }
    }
    if ((op & 0xF800) == 0x4800){            /* PC-relative load (fmt 6) */
        int rd=(op>>8)&7; uint32_t addr=((c->r[15]) & ~2u) + ((op&0xFF)<<2);
        c->r[rd]=c->read32(c, addr & ~3u); return;
    }
    if ((op & 0xF200) == 0x5000){            /* load/store reg offset (fmt 7) */
        int rd=op&7, rb=(op>>3)&7, ro=(op>>6)&7; uint32_t addr=c->r[rb]+c->r[ro];
        switch ((op>>10)&3){
            case 0: c->write32(c,addr&~3u,c->r[rd]); return;
            case 1: c->write8(c,addr,(uint8_t)c->r[rd]); return;
            case 2: { uint32_t v=c->read32(c,addr&~3u); if(addr&3){uint32_t r=(addr&3)*8;v=(v>>r)|(v<<(32-r));} c->r[rd]=v; return; }
            default: c->r[rd]=c->read8(c,addr); return;
        }
    }
    if ((op & 0xF200) == 0x5200){            /* load/store sign-ext halfword (fmt 8) */
        int rd=op&7, rb=(op>>3)&7, ro=(op>>6)&7; uint32_t addr=c->r[rb]+c->r[ro];
        switch ((op>>10)&3){
            case 0: c->write16(c,addr&~1u,(uint16_t)c->r[rd]); return;      /* STRH */
            case 1: c->r[rd]=(uint32_t)(int32_t)(int8_t)c->read8(c,addr); return; /* LDSB */
            case 2: c->r[rd]=c->read16(c,addr&~1u); return;                 /* LDRH */
            default: c->r[rd]=(uint32_t)(int32_t)(int16_t)c->read16(c,addr&~1u); return; /* LDSH */
        }
    }
    if ((op & 0xE000) == 0x6000){            /* load/store imm offset (fmt 9) */
        int rd=op&7, rb=(op>>3)&7, off=(op>>6)&0x1F; int byte=!!(op&0x1000), load=!!(op&0x0800);
        uint32_t addr=c->r[rb] + (byte?off:(off<<2));
        if (load){ if(byte) c->r[rd]=c->read8(c,addr); else { uint32_t v=c->read32(c,addr&~3u); if(addr&3){uint32_t r=(addr&3)*8;v=(v>>r)|(v<<(32-r));} c->r[rd]=v; } }
        else { if(byte) c->write8(c,addr,(uint8_t)c->r[rd]); else c->write32(c,addr&~3u,c->r[rd]); }
        return;
    }
    if ((op & 0xF000) == 0x8000){            /* load/store halfword (fmt 10) */
        int rd=op&7, rb=(op>>3)&7, off=((op>>6)&0x1F)<<1; uint32_t addr=c->r[rb]+off;
        if (op&0x0800) c->r[rd]=c->read16(c,addr&~1u); else c->write16(c,addr&~1u,(uint16_t)c->r[rd]);
        return;
    }
    if ((op & 0xF000) == 0x9000){            /* SP-relative load/store (fmt 11) */
        int rd=(op>>8)&7; uint32_t addr=c->r[13]+((op&0xFF)<<2);
        if (op&0x0800) c->r[rd]=c->read32(c,addr&~3u); else c->write32(c,addr&~3u,c->r[rd]);
        return;
    }
    if ((op & 0xF000) == 0xA000){            /* load address (fmt 12) */
        int rd=(op>>8)&7; uint32_t base=(op&0x0800)?c->r[13]:((c->r[15])&~2u);
        c->r[rd]=base+((op&0xFF)<<2); return;
    }
    if ((op & 0xFF00) == 0xB000){            /* add offset to SP (fmt 13) */
        uint32_t off=(op&0x7F)<<2; c->r[13]+=(op&0x80)?-off:off; return;
    }
    if ((op & 0xF600) == 0xB400){            /* push/pop (fmt 14) */
        int load=!!(op&0x0800), pclr=!!(op&0x0100); uint32_t list=op&0xFF;
        if (load){ /* POP */
            for (int i=0;i<8;i++) if(list&(1<<i)){ c->r[i]=c->read32(c,c->r[13]&~3u); c->r[13]+=4; }
            if (pclr){ uint32_t v=c->read32(c,c->r[13]&~3u); c->r[13]+=4;
                       if(v&1){c->cpsr|=ARM_T;c->r[15]=v&~1u;}else{c->cpsr&=~ARM_T;c->r[15]=v&~3u;} }
        } else { /* PUSH */
            if (pclr){ c->r[13]-=4; c->write32(c,c->r[13]&~3u,c->r[14]); }
            for (int i=7;i>=0;i--) if(list&(1<<i)){ c->r[13]-=4; c->write32(c,c->r[13]&~3u,c->r[i]); }
        }
        return;
    }
    if ((op & 0xF000) == 0xC000){            /* multiple load/store (fmt 15) */
        int rb=(op>>8)&7, load=!!(op&0x0800); uint32_t list=op&0xFF, addr=c->r[rb];
        for (int i=0;i<8;i++) if(list&(1<<i)){ if(load) c->r[i]=c->read32(c,addr&~3u); else c->write32(c,addr&~3u,c->r[i]); addr+=4; }
        c->r[rb]=addr;
        return;
    }
    if ((op & 0xFF00) == 0xDF00){ if (c->swi) c->swi(c, op & 0xFF); return; }  /* SWI (fmt 17) */
    if ((op & 0xF000) == 0xD000){            /* conditional branch (fmt 16) */
        uint32_t cond=(op>>8)&0xF; if (!cond_pass(c,cond)) return;
        int32_t off=(int32_t)(int8_t)(op&0xFF); c->r[15]=c->r[15]+(off<<1); return;
    }
    if ((op & 0xF800) == 0xE000){            /* unconditional branch (fmt 18) */
        int32_t off=(int32_t)((op&0x7FF)<<21)>>20; c->r[15]=c->r[15]+off; return;
    }
    if ((op & 0xF000) == 0xF000){            /* long branch with link (fmt 19) */
        if (!(op & 0x0800)){ int32_t hi=(int32_t)((op&0x7FF)<<21)>>9; c->r[14]=c->r[15]+hi; }
        else { uint32_t next=c->r[15]-2; c->r[15]=(c->r[14]+((op&0x7FF)<<1)); c->r[14]=next|1; }
        return;
    }
    c->illegal++;
}

int cpu_arm7_step(cpu_arm7_t *c){
    if (c->halted){ c->cycles += 1; return 1; }
    if (c->cpsr & ARM_T){
        uint32_t pc = c->r[15] & ~1u;
        uint16_t op = c->read16(c, pc);
        c->r[15] = pc + 4;                    /* PC reads +4 */
        exec_thumb(c, op);
        if ((c->r[15] & ~1u) == pc + 4) c->r[15] = pc + 2;   /* not branched -> next */
        c->cycles += 1; return 1;
    } else {
        uint32_t pc = c->r[15] & ~3u;
        uint32_t op = c->read32(c, pc);
        c->r[15] = pc + 8;                    /* PC reads +8 */
        exec_arm(c, op);
        if ((c->cpsr & ARM_T) == 0 && (c->r[15] & ~3u) == pc + 8) c->r[15] = pc + 4;
        c->cycles += 1; return 1;
    }
}

uint64_t cpu_arm7_run(cpu_arm7_t *c, uint64_t max_cycles){
    uint64_t start=c->cycles;
    while (c->cycles - start < max_cycles && !c->halted) cpu_arm7_step(c);
    return c->cycles - start;
}
