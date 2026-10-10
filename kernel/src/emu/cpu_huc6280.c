/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cpu_huc6280.c — Hudson HuC6280 (PC Engine CPU): 65C02 + MPR banking + ST0-2 +
 * block transfers. See cpu_huc6280.h. Every logical access goes through the
 * MPRs. Decimal mode and a few rare encodings are approximated/counted. */
#include "cpu_huc6280.h"

#define VDC_PHYS 0x1FE000u   /* hardware page $FF: VDC address/data ports */

static inline uint8_t rd(cpu_huc6280_t *c, uint16_t a){ return c->read(c, huc_phys(c,a)); }
static inline void    wr(cpu_huc6280_t *c, uint16_t a, uint8_t v){ c->write(c, huc_phys(c,a), v); }
static inline uint8_t fetch(cpu_huc6280_t *c){ return rd(c, c->pc++); }
static inline uint16_t fetch16(cpu_huc6280_t *c){ uint16_t lo=fetch(c); uint16_t hi=fetch(c); return lo|(hi<<8); }

static inline void setnz(cpu_huc6280_t *c, uint8_t v){ c->p=(c->p&~(HUC_N|HUC_Z))|(v?0:HUC_Z)|(v&HUC_N); }
static inline void push(cpu_huc6280_t *c, uint8_t v){ c->write(c, huc_phys(c,0x0100|c->sp), v); c->sp--; }
static inline uint8_t pull(cpu_huc6280_t *c){ c->sp++; return c->read(c, huc_phys(c,0x0100|c->sp)); }

/* logical effective-address helpers */
static uint16_t a_zp (cpu_huc6280_t *c){ return fetch(c); }
static uint16_t a_zpx(cpu_huc6280_t *c){ return (uint8_t)(fetch(c)+c->x); }
static uint16_t a_zpy(cpu_huc6280_t *c){ return (uint8_t)(fetch(c)+c->y); }
static uint16_t a_abs(cpu_huc6280_t *c){ return fetch16(c); }
static uint16_t a_abx(cpu_huc6280_t *c){ return fetch16(c)+c->x; }
static uint16_t a_aby(cpu_huc6280_t *c){ return fetch16(c)+c->y; }
static uint16_t a_izx(cpu_huc6280_t *c){ uint8_t p=(uint8_t)(fetch(c)+c->x); return rd(c,p)|(rd(c,(uint8_t)(p+1))<<8); }
static uint16_t a_izy(cpu_huc6280_t *c){ uint8_t p=fetch(c); return (rd(c,p)|(rd(c,(uint8_t)(p+1))<<8))+c->y; }
static uint16_t a_izp(cpu_huc6280_t *c){ uint8_t p=fetch(c); return rd(c,p)|(rd(c,(uint8_t)(p+1))<<8); }

static void ADC(cpu_huc6280_t *c, uint8_t v){ unsigned cy=(c->p&HUC_C)?1:0, r=c->a+v+cy;
    c->p&=~(HUC_C|HUC_V); if(r>0xFF)c->p|=HUC_C; if((~(c->a^v)&(c->a^r)&0x80))c->p|=HUC_V; c->a=r; setnz(c,c->a); }
static void SBC(cpu_huc6280_t *c, uint8_t v){ ADC(c, v^0xFF); }
static void CMP_(cpu_huc6280_t *c, uint8_t reg, uint8_t v){ uint16_t r=reg-v; c->p=(c->p&~HUC_C)|((reg>=v)?HUC_C:0); setnz(c,(uint8_t)r); }
static uint8_t ASL_(cpu_huc6280_t *c, uint8_t v){ c->p=(c->p&~HUC_C)|((v&0x80)?HUC_C:0); v<<=1; setnz(c,v); return v; }
static uint8_t LSR_(cpu_huc6280_t *c, uint8_t v){ c->p=(c->p&~HUC_C)|((v&1)?HUC_C:0); v>>=1; setnz(c,v); return v; }
static uint8_t ROL_(cpu_huc6280_t *c, uint8_t v){ unsigned cy=(c->p&HUC_C)?1:0; c->p=(c->p&~HUC_C)|((v&0x80)?HUC_C:0); v=(v<<1)|cy; setnz(c,v); return v; }
static uint8_t ROR_(cpu_huc6280_t *c, uint8_t v){ unsigned cy=(c->p&HUC_C)?0x80:0; c->p=(c->p&~HUC_C)|((v&1)?HUC_C:0); v=(v>>1)|cy; setnz(c,v); return v; }

static void branch(cpu_huc6280_t *c, int take){ int8_t d=(int8_t)fetch(c); if(take) c->pc+=d; }

/* block transfer: mode 0 TII(+,+) 1 TDD(-,-) 2 TIN(+,fix) 3 TIA(+,alt) 4 TAI(alt,+) */
static void block_xfer(cpu_huc6280_t *c, int mode){
    uint16_t src=fetch16(c), dst=fetch16(c), len=fetch16(c); int alt=0; uint32_t guard=0;
    while (len-- && guard<0x10000){
        wr(c, dst, rd(c, src)); guard++;
        switch (mode){
            case 0: src++; dst++; break;
            case 1: src--; dst--; break;
            case 2: src++; break;
            case 3: src++; dst += alt?-1:1; alt^=1; break;
            case 4: dst++; src += alt?-1:1; alt^=1; break;
        }
    }
    c->cycles += 6;
}

void cpu_huc6280_reset(cpu_huc6280_t *c){
    for (int i=0;i<8;i++) c->mpr[i]=0;
    c->mpr[7]=0;
    c->sp=0xFF; c->p=HUC_I|HUC_T; c->a=c->x=c->y=0;
    c->pc = rd(c,0xFFFE) | (rd(c,0xFFFF)<<8);
    c->cycles=0; c->jammed=0; c->illegal=0; c->last_opcode=0;
}
static void vector(cpu_huc6280_t *c, uint16_t vec){
    push(c, c->pc>>8); push(c, c->pc&0xFF); push(c, c->p & ~HUC_B);
    c->p |= HUC_I; c->pc = rd(c,vec) | (rd(c,vec+1)<<8);
}
void cpu_huc6280_nmi(cpu_huc6280_t *c){ vector(c, 0xFFFC); }
void cpu_huc6280_irq(cpu_huc6280_t *c){ if(!(c->p&HUC_I)) vector(c, 0xFFF8); }  /* IRQ1 (VDC) */

int cpu_huc6280_step(cpu_huc6280_t *c){
    if (c->jammed) return 0;
    uint8_t op = fetch(c); c->last_opcode = op;
    uint16_t ea; uint8_t v;
    switch (op){
        /* ---- loads/stores ---- */
        case 0xA9: c->a=fetch(c); setnz(c,c->a); break;                 /* LDA # */
        case 0xA5: c->a=rd(c,a_zp(c)); setnz(c,c->a); break;
        case 0xB5: c->a=rd(c,a_zpx(c)); setnz(c,c->a); break;
        case 0xAD: c->a=rd(c,a_abs(c)); setnz(c,c->a); break;
        case 0xBD: c->a=rd(c,a_abx(c)); setnz(c,c->a); break;
        case 0xB9: c->a=rd(c,a_aby(c)); setnz(c,c->a); break;
        case 0xA1: c->a=rd(c,a_izx(c)); setnz(c,c->a); break;
        case 0xB1: c->a=rd(c,a_izy(c)); setnz(c,c->a); break;
        case 0xB2: c->a=rd(c,a_izp(c)); setnz(c,c->a); break;           /* LDA (zp) 65C02 */
        case 0xA2: c->x=fetch(c); setnz(c,c->x); break;                 /* LDX # */
        case 0xA6: c->x=rd(c,a_zp(c)); setnz(c,c->x); break;
        case 0xB6: c->x=rd(c,a_zpy(c)); setnz(c,c->x); break;
        case 0xAE: c->x=rd(c,a_abs(c)); setnz(c,c->x); break;
        case 0xBE: c->x=rd(c,a_aby(c)); setnz(c,c->x); break;
        case 0xA0: c->y=fetch(c); setnz(c,c->y); break;                 /* LDY # */
        case 0xA4: c->y=rd(c,a_zp(c)); setnz(c,c->y); break;
        case 0xB4: c->y=rd(c,a_zpx(c)); setnz(c,c->y); break;
        case 0xAC: c->y=rd(c,a_abs(c)); setnz(c,c->y); break;
        case 0xBC: c->y=rd(c,a_abx(c)); setnz(c,c->y); break;
        case 0x85: wr(c,a_zp(c),c->a); break;                           /* STA */
        case 0x95: wr(c,a_zpx(c),c->a); break;
        case 0x8D: wr(c,a_abs(c),c->a); break;
        case 0x9D: wr(c,a_abx(c),c->a); break;
        case 0x99: wr(c,a_aby(c),c->a); break;
        case 0x81: wr(c,a_izx(c),c->a); break;
        case 0x91: wr(c,a_izy(c),c->a); break;
        case 0x92: wr(c,a_izp(c),c->a); break;                          /* STA (zp) */
        case 0x86: wr(c,a_zp(c),c->x); break;                           /* STX */
        case 0x96: wr(c,a_zpy(c),c->x); break;
        case 0x8E: wr(c,a_abs(c),c->x); break;
        case 0x84: wr(c,a_zp(c),c->y); break;                           /* STY */
        case 0x94: wr(c,a_zpx(c),c->y); break;
        case 0x8C: wr(c,a_abs(c),c->y); break;
        case 0x64: wr(c,a_zp(c),0); break;                              /* STZ 65C02 */
        case 0x74: wr(c,a_zpx(c),0); break;
        case 0x9C: wr(c,a_abs(c),0); break;
        case 0x9E: wr(c,a_abx(c),0); break;
        /* ---- transfers/stack ---- */
        case 0xAA: c->x=c->a; setnz(c,c->x); break;                     /* TAX */
        case 0xA8: c->y=c->a; setnz(c,c->y); break;                     /* TAY */
        case 0x8A: c->a=c->x; setnz(c,c->a); break;                     /* TXA */
        case 0x98: c->a=c->y; setnz(c,c->a); break;                     /* TYA */
        case 0xBA: c->x=c->sp; setnz(c,c->x); break;                    /* TSX */
        case 0x9A: c->sp=c->x; break;                                   /* TXS */
        case 0x48: push(c,c->a); break;                                 /* PHA */
        case 0x68: c->a=pull(c); setnz(c,c->a); break;                  /* PLA */
        case 0x08: push(c,c->p|HUC_B); break;                           /* PHP */
        case 0x28: c->p=pull(c); break;                                 /* PLP */
        case 0xDA: push(c,c->x); break;                                 /* PHX */
        case 0xFA: c->x=pull(c); setnz(c,c->x); break;                  /* PLX */
        case 0x5A: push(c,c->y); break;                                 /* PHY */
        case 0x7A: c->y=pull(c); setnz(c,c->y); break;                  /* PLY */
        /* ---- ALU ---- */
        case 0x69: ADC(c,fetch(c)); break;
        case 0x65: ADC(c,rd(c,a_zp(c))); break;   case 0x75: ADC(c,rd(c,a_zpx(c))); break;
        case 0x6D: ADC(c,rd(c,a_abs(c))); break;  case 0x7D: ADC(c,rd(c,a_abx(c))); break;
        case 0x79: ADC(c,rd(c,a_aby(c))); break;  case 0x61: ADC(c,rd(c,a_izx(c))); break;
        case 0x71: ADC(c,rd(c,a_izy(c))); break;  case 0x72: ADC(c,rd(c,a_izp(c))); break;
        case 0xE9: SBC(c,fetch(c)); break;
        case 0xE5: SBC(c,rd(c,a_zp(c))); break;   case 0xF5: SBC(c,rd(c,a_zpx(c))); break;
        case 0xED: SBC(c,rd(c,a_abs(c))); break;  case 0xFD: SBC(c,rd(c,a_abx(c))); break;
        case 0xF9: SBC(c,rd(c,a_aby(c))); break;  case 0xE1: SBC(c,rd(c,a_izx(c))); break;
        case 0xF1: SBC(c,rd(c,a_izy(c))); break;  case 0xF2: SBC(c,rd(c,a_izp(c))); break;
        case 0x29: c->a&=fetch(c); setnz(c,c->a); break;
        case 0x25: c->a&=rd(c,a_zp(c)); setnz(c,c->a); break;  case 0x35: c->a&=rd(c,a_zpx(c)); setnz(c,c->a); break;
        case 0x2D: c->a&=rd(c,a_abs(c)); setnz(c,c->a); break; case 0x3D: c->a&=rd(c,a_abx(c)); setnz(c,c->a); break;
        case 0x39: c->a&=rd(c,a_aby(c)); setnz(c,c->a); break; case 0x21: c->a&=rd(c,a_izx(c)); setnz(c,c->a); break;
        case 0x31: c->a&=rd(c,a_izy(c)); setnz(c,c->a); break; case 0x32: c->a&=rd(c,a_izp(c)); setnz(c,c->a); break;
        case 0x09: c->a|=fetch(c); setnz(c,c->a); break;
        case 0x05: c->a|=rd(c,a_zp(c)); setnz(c,c->a); break;  case 0x15: c->a|=rd(c,a_zpx(c)); setnz(c,c->a); break;
        case 0x0D: c->a|=rd(c,a_abs(c)); setnz(c,c->a); break; case 0x1D: c->a|=rd(c,a_abx(c)); setnz(c,c->a); break;
        case 0x19: c->a|=rd(c,a_aby(c)); setnz(c,c->a); break; case 0x01: c->a|=rd(c,a_izx(c)); setnz(c,c->a); break;
        case 0x11: c->a|=rd(c,a_izy(c)); setnz(c,c->a); break; case 0x12: c->a|=rd(c,a_izp(c)); setnz(c,c->a); break;
        case 0x49: c->a^=fetch(c); setnz(c,c->a); break;
        case 0x45: c->a^=rd(c,a_zp(c)); setnz(c,c->a); break;  case 0x55: c->a^=rd(c,a_zpx(c)); setnz(c,c->a); break;
        case 0x4D: c->a^=rd(c,a_abs(c)); setnz(c,c->a); break; case 0x5D: c->a^=rd(c,a_abx(c)); setnz(c,c->a); break;
        case 0x59: c->a^=rd(c,a_aby(c)); setnz(c,c->a); break; case 0x41: c->a^=rd(c,a_izx(c)); setnz(c,c->a); break;
        case 0x51: c->a^=rd(c,a_izy(c)); setnz(c,c->a); break; case 0x52: c->a^=rd(c,a_izp(c)); setnz(c,c->a); break;
        case 0xC9: CMP_(c,c->a,fetch(c)); break;
        case 0xC5: CMP_(c,c->a,rd(c,a_zp(c))); break;  case 0xD5: CMP_(c,c->a,rd(c,a_zpx(c))); break;
        case 0xCD: CMP_(c,c->a,rd(c,a_abs(c))); break; case 0xDD: CMP_(c,c->a,rd(c,a_abx(c))); break;
        case 0xD9: CMP_(c,c->a,rd(c,a_aby(c))); break; case 0xC1: CMP_(c,c->a,rd(c,a_izx(c))); break;
        case 0xD1: CMP_(c,c->a,rd(c,a_izy(c))); break; case 0xD2: CMP_(c,c->a,rd(c,a_izp(c))); break;
        case 0xE0: CMP_(c,c->x,fetch(c)); break;  case 0xE4: CMP_(c,c->x,rd(c,a_zp(c))); break;  case 0xEC: CMP_(c,c->x,rd(c,a_abs(c))); break;
        case 0xC0: CMP_(c,c->y,fetch(c)); break;  case 0xC4: CMP_(c,c->y,rd(c,a_zp(c))); break;  case 0xCC: CMP_(c,c->y,rd(c,a_abs(c))); break;
        /* BIT */
        case 0x89: c->p=(c->p&~HUC_Z)|((c->a&fetch(c))?0:HUC_Z); break;   /* BIT # (65C02: only Z) */
        case 0x24: v=rd(c,a_zp(c)); c->p=(c->p&~(HUC_Z|HUC_N|HUC_V))|((c->a&v)?0:HUC_Z)|(v&(HUC_N|HUC_V)); break;
        case 0x2C: v=rd(c,a_abs(c)); c->p=(c->p&~(HUC_Z|HUC_N|HUC_V))|((c->a&v)?0:HUC_Z)|(v&(HUC_N|HUC_V)); break;
        case 0x34: v=rd(c,a_zpx(c)); c->p=(c->p&~(HUC_Z|HUC_N|HUC_V))|((c->a&v)?0:HUC_Z)|(v&(HUC_N|HUC_V)); break;
        case 0x3C: v=rd(c,a_abx(c)); c->p=(c->p&~(HUC_Z|HUC_N|HUC_V))|((c->a&v)?0:HUC_Z)|(v&(HUC_N|HUC_V)); break;
        /* INC/DEC */
        case 0xE6: ea=a_zp(c); wr(c,ea,(v=rd(c,ea)+1)); setnz(c,v); break;
        case 0xF6: ea=a_zpx(c); wr(c,ea,(v=rd(c,ea)+1)); setnz(c,v); break;
        case 0xEE: ea=a_abs(c); wr(c,ea,(v=rd(c,ea)+1)); setnz(c,v); break;
        case 0xFE: ea=a_abx(c); wr(c,ea,(v=rd(c,ea)+1)); setnz(c,v); break;
        case 0xC6: ea=a_zp(c); wr(c,ea,(v=rd(c,ea)-1)); setnz(c,v); break;
        case 0xD6: ea=a_zpx(c); wr(c,ea,(v=rd(c,ea)-1)); setnz(c,v); break;
        case 0xCE: ea=a_abs(c); wr(c,ea,(v=rd(c,ea)-1)); setnz(c,v); break;
        case 0xDE: ea=a_abx(c); wr(c,ea,(v=rd(c,ea)-1)); setnz(c,v); break;
        case 0x1A: c->a++; setnz(c,c->a); break;                        /* INC A 65C02 */
        case 0x3A: c->a--; setnz(c,c->a); break;                        /* DEC A 65C02 */
        case 0xE8: c->x++; setnz(c,c->x); break;  case 0xCA: c->x--; setnz(c,c->x); break;
        case 0xC8: c->y++; setnz(c,c->y); break;  case 0x88: c->y--; setnz(c,c->y); break;
        /* shifts */
        case 0x0A: c->a=ASL_(c,c->a); break;
        case 0x06: ea=a_zp(c); wr(c,ea,ASL_(c,rd(c,ea))); break;  case 0x16: ea=a_zpx(c); wr(c,ea,ASL_(c,rd(c,ea))); break;
        case 0x0E: ea=a_abs(c); wr(c,ea,ASL_(c,rd(c,ea))); break; case 0x1E: ea=a_abx(c); wr(c,ea,ASL_(c,rd(c,ea))); break;
        case 0x4A: c->a=LSR_(c,c->a); break;
        case 0x46: ea=a_zp(c); wr(c,ea,LSR_(c,rd(c,ea))); break;  case 0x56: ea=a_zpx(c); wr(c,ea,LSR_(c,rd(c,ea))); break;
        case 0x4E: ea=a_abs(c); wr(c,ea,LSR_(c,rd(c,ea))); break; case 0x5E: ea=a_abx(c); wr(c,ea,LSR_(c,rd(c,ea))); break;
        case 0x2A: c->a=ROL_(c,c->a); break;
        case 0x26: ea=a_zp(c); wr(c,ea,ROL_(c,rd(c,ea))); break;  case 0x36: ea=a_zpx(c); wr(c,ea,ROL_(c,rd(c,ea))); break;
        case 0x2E: ea=a_abs(c); wr(c,ea,ROL_(c,rd(c,ea))); break; case 0x3E: ea=a_abx(c); wr(c,ea,ROL_(c,rd(c,ea))); break;
        case 0x6A: c->a=ROR_(c,c->a); break;
        case 0x66: ea=a_zp(c); wr(c,ea,ROR_(c,rd(c,ea))); break;  case 0x76: ea=a_zpx(c); wr(c,ea,ROR_(c,rd(c,ea))); break;
        case 0x6E: ea=a_abs(c); wr(c,ea,ROR_(c,rd(c,ea))); break; case 0x7E: ea=a_abx(c); wr(c,ea,ROR_(c,rd(c,ea))); break;
        /* TSB/TRB */
        case 0x04: ea=a_zp(c); v=rd(c,ea); c->p=(c->p&~HUC_Z)|((c->a&v)?0:HUC_Z); wr(c,ea,v|c->a); break;
        case 0x0C: ea=a_abs(c); v=rd(c,ea); c->p=(c->p&~HUC_Z)|((c->a&v)?0:HUC_Z); wr(c,ea,v|c->a); break;
        case 0x14: ea=a_zp(c); v=rd(c,ea); c->p=(c->p&~HUC_Z)|((c->a&v)?0:HUC_Z); wr(c,ea,v&~c->a); break;
        case 0x1C: ea=a_abs(c); v=rd(c,ea); c->p=(c->p&~HUC_Z)|((c->a&v)?0:HUC_Z); wr(c,ea,v&~c->a); break;
        /* branches */
        case 0x10: branch(c,!(c->p&HUC_N)); break;  case 0x30: branch(c,c->p&HUC_N); break;
        case 0x50: branch(c,!(c->p&HUC_V)); break;  case 0x70: branch(c,c->p&HUC_V); break;
        case 0x90: branch(c,!(c->p&HUC_C)); break;  case 0xB0: branch(c,c->p&HUC_C); break;
        case 0xD0: branch(c,!(c->p&HUC_Z)); break;  case 0xF0: branch(c,c->p&HUC_Z); break;
        case 0x80: branch(c,1); break;                                  /* BRA 65C02 */
        /* jumps */
        case 0x4C: c->pc=a_abs(c); break;                               /* JMP abs */
        case 0x6C: ea=a_abs(c); c->pc=rd(c,ea)|(rd(c,(uint16_t)(ea+1))<<8); break; /* JMP (abs) */
        case 0x7C: ea=(uint16_t)(a_abs(c)+c->x); c->pc=rd(c,ea)|(rd(c,(uint16_t)(ea+1))<<8); break; /* JMP (abs,X) */
        case 0x20: { uint16_t t=a_abs(c); uint16_t r=c->pc-1; push(c,r>>8); push(c,r&0xFF); c->pc=t; } break; /* JSR */
        case 0x60: c->pc=(pull(c)|(pull(c)<<8))+1; break;               /* RTS */
        case 0x40: c->p=pull(c); c->pc=pull(c)|(pull(c)<<8); break;      /* RTI */
        /* flags */
        case 0x18: c->p&=~HUC_C; break;  case 0x38: c->p|=HUC_C; break;
        case 0x58: c->p&=~HUC_I; break;  case 0x78: c->p|=HUC_I; break;
        case 0xB8: c->p&=~HUC_V; break;
        case 0xD8: c->p&=~HUC_D; break;  case 0xF8: c->p|=HUC_D; break;
        case 0xEA: break;                                               /* NOP */
        /* ---- HuC6280 extensions ---- */
        case 0x53: { uint8_t m=fetch(c); for(int i=0;i<8;i++) if(m&(1<<i)) c->mpr[i]=c->a; } break;  /* TAM */
        case 0x43: { uint8_t m=fetch(c); for(int i=0;i<8;i++) if(m&(1<<i)){ c->a=c->mpr[i]; break; } } break; /* TMA */
        case 0x03: c->write(c, VDC_PHYS+0, fetch(c)); break;            /* ST0 (VDC addr) */
        case 0x13: c->write(c, VDC_PHYS+2, fetch(c)); break;            /* ST1 (VDC data lo) */
        case 0x23: c->write(c, VDC_PHYS+3, fetch(c)); break;            /* ST2 (VDC data hi) */
        case 0x73: block_xfer(c,0); break;                             /* TII */
        case 0xC3: block_xfer(c,1); break;                             /* TDD */
        case 0xD3: block_xfer(c,2); break;                             /* TIN */
        case 0xE3: block_xfer(c,3); break;                             /* TIA */
        case 0xF3: block_xfer(c,4); break;                             /* TAI */
        case 0x02: { uint8_t t=c->x; c->x=c->y; c->y=t; } break;        /* SXY */
        case 0x22: { uint8_t t=c->a; c->a=c->x; c->x=t; } break;        /* SAX */
        case 0x42: { uint8_t t=c->a; c->a=c->y; c->y=t; } break;        /* SAY */
        case 0x62: c->a=0; break;                                       /* CLA */
        case 0x82: c->x=0; break;                                       /* CLX */
        case 0xC2: c->y=0; break;                                       /* CLY */
        case 0xD4: break;  case 0x54: break;                            /* CSH/CSL */
        case 0xF4: c->p|=HUC_T; break;                                  /* SET */
        case 0x00: c->pc++; vector(c,0xFFF6); break;                    /* BRK -> IRQ2/BRK vector */
        default: c->illegal++; break;                                   /* incl. RMBx/SMBx/BBRx/BBSx (rare) */
    }
    c->cycles += 3;
    return 3;
}

uint64_t cpu_huc6280_run(cpu_huc6280_t *c, uint64_t max_cycles){
    uint64_t start=c->cycles;
    while (c->cycles-start < max_cycles && !c->jammed) cpu_huc6280_step(c);
    return c->cycles-start;
}
