/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* cpu_lr35902.c — Sharp LR35902 (Game Boy / GBC CPU). See cpu_lr35902.h.
 * The regular blocks $40-$7F (LD r,r') and $80-$BF (ALU A,r) are decoded
 * algorithmically; the rest are explicit. Undefined opcodes are counted. */
#include "cpu_lr35902.h"

static inline uint8_t  rb(cpu_lr35902_t *c, uint16_t a){ return c->read(c, a); }
static inline void     wb(cpu_lr35902_t *c, uint16_t a, uint8_t v){ c->write(c, a, v); }
static inline uint16_t rw(cpu_lr35902_t *c, uint16_t a){ return (uint16_t)(rb(c,a) | (rb(c,a+1)<<8)); }
static inline uint8_t  fetch(cpu_lr35902_t *c){ return rb(c, c->pc++); }
static inline uint16_t fetch16(cpu_lr35902_t *c){ uint16_t v = rw(c, c->pc); c->pc += 2; return v; }

static inline uint16_t HL(cpu_lr35902_t *c){ return (uint16_t)((c->h<<8)|c->l); }
static inline void setHL(cpu_lr35902_t *c, uint16_t v){ c->h = v>>8; c->l = v & 0xFF; }
static inline uint16_t BC(cpu_lr35902_t *c){ return (uint16_t)((c->b<<8)|c->c); }
static inline uint16_t DE(cpu_lr35902_t *c){ return (uint16_t)((c->d<<8)|c->e); }

/* register slot order: 0=B 1=C 2=D 3=E 4=H 5=L 6=(HL) 7=A */
static uint8_t get_r(cpu_lr35902_t *c, int i){
    switch (i){ case 0:return c->b; case 1:return c->c; case 2:return c->d; case 3:return c->e;
                case 4:return c->h; case 5:return c->l; case 6:return rb(c, HL(c)); default:return c->a; }
}
static void set_r(cpu_lr35902_t *c, int i, uint8_t v){
    switch (i){ case 0:c->b=v;break; case 1:c->c=v;break; case 2:c->d=v;break; case 3:c->e=v;break;
                case 4:c->h=v;break; case 5:c->l=v;break; case 6:wb(c,HL(c),v);break; default:c->a=v;break; }
}

static inline void setf(cpu_lr35902_t *c, uint8_t z, uint8_t n, uint8_t hf, uint8_t cf){
    c->f = (z?LR_FLAG_Z:0) | (n?LR_FLAG_N:0) | (hf?LR_FLAG_H:0) | (cf?LR_FLAG_C:0);
}

/* the 8 ALU ops on A with operand v (op: 0 ADD 1 ADC 2 SUB 3 SBC 4 AND 5 XOR 6 OR 7 CP) */
static void alu(cpu_lr35902_t *c, int op, uint8_t v){
    uint8_t a = c->a; unsigned cy = (c->f & LR_FLAG_C) ? 1 : 0; unsigned r;
    switch (op){
        case 0: r = a + v;      setf(c,(r&0xFF)==0,0,((a&0xF)+(v&0xF))>0xF,r>0xFF); c->a=r; break;
        case 1: r = a + v + cy; setf(c,(r&0xFF)==0,0,((a&0xF)+(v&0xF)+cy)>0xF,r>0xFF); c->a=r; break;
        case 2: r = a - v;      setf(c,(r&0xFF)==0,1,(a&0xF)<(v&0xF),a<v); c->a=r; break;
        case 3: r = a - v - cy; setf(c,(r&0xFF)==0,1,(a&0xF)<((v&0xF)+cy),a<(v+cy)); c->a=r; break;
        case 4: c->a = a & v;   setf(c,c->a==0,0,1,0); break;
        case 5: c->a = a ^ v;   setf(c,c->a==0,0,0,0); break;
        case 6: c->a = a | v;   setf(c,c->a==0,0,0,0); break;
        default: r = a - v;     setf(c,(r&0xFF)==0,1,(a&0xF)<(v&0xF),a<v); break;   /* CP */
    }
}

static uint8_t inc8(cpu_lr35902_t *c, uint8_t v){ uint8_t r=v+1;
    c->f = (c->f&LR_FLAG_C) | (r==0?LR_FLAG_Z:0) | (((v&0xF)+1>0xF)?LR_FLAG_H:0); return r; }
static uint8_t dec8(cpu_lr35902_t *c, uint8_t v){ uint8_t r=v-1;
    c->f = (c->f&LR_FLAG_C) | LR_FLAG_N | (r==0?LR_FLAG_Z:0) | (((v&0xF)==0)?LR_FLAG_H:0); return r; }
static void add16(cpu_lr35902_t *c, uint16_t v){ uint32_t hl=HL(c), r=hl+v;
    c->f = (c->f&LR_FLAG_Z) | (((hl&0xFFF)+(v&0xFFF))>0xFFF?LR_FLAG_H:0) | (r>0xFFFF?LR_FLAG_C:0); setHL(c,r); }

static void push(cpu_lr35902_t *c, uint16_t v){ c->sp-=2; wb(c,c->sp,v&0xFF); wb(c,c->sp+1,v>>8); }
static uint16_t pop(cpu_lr35902_t *c){ uint16_t v=rw(c,c->sp); c->sp+=2; return v; }

static void do_cb(cpu_lr35902_t *c){
    uint8_t op = fetch(c); int reg = op & 7; uint8_t v = get_r(c, reg); uint8_t bit;
    if (op < 0x40){
        int sub = (op>>3)&7; uint8_t cy = (c->f&LR_FLAG_C)?1:0; uint8_t r; uint8_t co;
        switch (sub){
            case 0: co=v>>7; r=(v<<1)|co; break;              /* RLC */
            case 1: co=v&1;  r=(v>>1)|(co<<7); break;         /* RRC */
            case 2: co=v>>7; r=(v<<1)|cy; break;              /* RL  */
            case 3: co=v&1;  r=(v>>1)|(cy<<7); break;         /* RR  */
            case 4: co=v>>7; r=v<<1; break;                   /* SLA */
            case 5: co=v&1;  r=(v>>1)|(v&0x80); break;        /* SRA */
            case 6: co=0;    r=(v>>4)|(v<<4); break;          /* SWAP */
            default:co=v&1;  r=v>>1; break;                   /* SRL */
        }
        setf(c, r==0, 0, 0, co);
        set_r(c, reg, r);
    } else if (op < 0x80){                                     /* BIT b,r */
        bit = (op>>3)&7;
        c->f = (c->f&LR_FLAG_C) | LR_FLAG_H | ((v&(1<<bit))?0:LR_FLAG_Z);
    } else if (op < 0xC0){                                     /* RES b,r */
        bit = (op>>3)&7; set_r(c, reg, v & ~(1<<bit));
    } else {                                                   /* SET b,r */
        bit = (op>>3)&7; set_r(c, reg, v | (1<<bit));
    }
}

static void daa(cpu_lr35902_t *c){
    uint8_t a = c->a; unsigned corr = 0; unsigned setc = (c->f&LR_FLAG_C)?1:0;
    if (!(c->f&LR_FLAG_N)){
        if ((c->f&LR_FLAG_H) || (a & 0x0F) > 9) corr |= 0x06;
        if (setc || a > 0x99){ corr |= 0x60; setc = 1; }
        a += corr;
    } else {
        if (c->f&LR_FLAG_H) corr |= 0x06;
        if (setc) corr |= 0x60;
        a -= corr;
    }
    c->f = (c->f & LR_FLAG_N) | (a==0?LR_FLAG_Z:0) | (setc?LR_FLAG_C:0);
    c->a = a;
}

void cpu_lr_reset(cpu_lr35902_t *c){
    c->a=0x01; c->f=0xB0; c->b=0x00; c->c=0x13; c->d=0x00; c->e=0xD8;
    c->h=0x01; c->l=0x4D; c->sp=0xFFFE; c->pc=0x0100;
    c->ime=0; c->halted=0; c->stopped=0; c->cycles=0; c->illegal=0; c->last_opcode=0;
}

int cpu_lr_interrupt(cpu_lr35902_t *c, uint16_t vector){
    c->halted = 0;
    if (!c->ime) return 0;
    c->ime = 0; push(c, c->pc); c->pc = vector; c->cycles += 20; return 1;
}

int cpu_lr_step(cpu_lr35902_t *c){
    if (c->halted || c->stopped){ c->cycles += 4; return 4; }
    uint8_t op = fetch(c); c->last_opcode = op;
    int t = 4;

    /* Block: LD r,r' ($40-$7F), with $76 = HALT */
    if (op >= 0x40 && op <= 0x7F){
        if (op == 0x76){ c->halted = 1; return 4; }
        int dst = (op>>3)&7, src = op&7;
        set_r(c, dst, get_r(c, src));
        return (dst==6 || src==6) ? 8 : 4;
    }
    /* Block: ALU A,r ($80-$BF) */
    if (op >= 0x80 && op <= 0xBF){
        int aop = (op>>3)&7, src = op&7;
        alu(c, aop, get_r(c, src));
        return (src==6) ? 8 : 4;
    }

    switch (op){
        case 0x00: break;                                        /* NOP */
        case 0x10: c->pc++; c->stopped = 1; break;               /* STOP */
        case 0x08: { uint16_t a = fetch16(c); wb(c,a,c->sp&0xFF); wb(c,a+1,c->sp>>8); t=20; } break;
        /* 16-bit loads */
        case 0x01: { uint16_t v=fetch16(c); c->b=v>>8; c->c=v&0xFF; t=12; } break;
        case 0x11: { uint16_t v=fetch16(c); c->d=v>>8; c->e=v&0xFF; t=12; } break;
        case 0x21: { uint16_t v=fetch16(c); setHL(c,v); t=12; } break;
        case 0x31: c->sp=fetch16(c); t=12; break;
        /* INC/DEC 16 */
        case 0x03: { uint16_t v=BC(c)+1; c->b=v>>8; c->c=v&0xFF; t=8; } break;
        case 0x13: { uint16_t v=DE(c)+1; c->d=v>>8; c->e=v&0xFF; t=8; } break;
        case 0x23: setHL(c,HL(c)+1); t=8; break;
        case 0x33: c->sp++; t=8; break;
        case 0x0B: { uint16_t v=BC(c)-1; c->b=v>>8; c->c=v&0xFF; t=8; } break;
        case 0x1B: { uint16_t v=DE(c)-1; c->d=v>>8; c->e=v&0xFF; t=8; } break;
        case 0x2B: setHL(c,HL(c)-1); t=8; break;
        case 0x3B: c->sp--; t=8; break;
        /* ADD HL,rr */
        case 0x09: add16(c,BC(c)); t=8; break;
        case 0x19: add16(c,DE(c)); t=8; break;
        case 0x29: add16(c,HL(c)); t=8; break;
        case 0x39: add16(c,c->sp); t=8; break;
        /* INC/DEC 8 (r) */
        case 0x04: c->b=inc8(c,c->b); break;   case 0x05: c->b=dec8(c,c->b); break;
        case 0x0C: c->c=inc8(c,c->c); break;   case 0x0D: c->c=dec8(c,c->c); break;
        case 0x14: c->d=inc8(c,c->d); break;   case 0x15: c->d=dec8(c,c->d); break;
        case 0x1C: c->e=inc8(c,c->e); break;   case 0x1D: c->e=dec8(c,c->e); break;
        case 0x24: c->h=inc8(c,c->h); break;   case 0x25: c->h=dec8(c,c->h); break;
        case 0x2C: c->l=inc8(c,c->l); break;   case 0x2D: c->l=dec8(c,c->l); break;
        case 0x34: wb(c,HL(c),inc8(c,rb(c,HL(c)))); t=12; break;
        case 0x35: wb(c,HL(c),dec8(c,rb(c,HL(c)))); t=12; break;
        case 0x3C: c->a=inc8(c,c->a); break;   case 0x3D: c->a=dec8(c,c->a); break;
        /* LD r,d8 */
        case 0x06: c->b=fetch(c); t=8; break;  case 0x0E: c->c=fetch(c); t=8; break;
        case 0x16: c->d=fetch(c); t=8; break;  case 0x1E: c->e=fetch(c); t=8; break;
        case 0x26: c->h=fetch(c); t=8; break;  case 0x2E: c->l=fetch(c); t=8; break;
        case 0x36: wb(c,HL(c),fetch(c)); t=12; break;
        case 0x3E: c->a=fetch(c); t=8; break;
        /* indirect A loads/stores */
        case 0x02: wb(c,BC(c),c->a); t=8; break;
        case 0x12: wb(c,DE(c),c->a); t=8; break;
        case 0x0A: c->a=rb(c,BC(c)); t=8; break;
        case 0x1A: c->a=rb(c,DE(c)); t=8; break;
        case 0x22: wb(c,HL(c),c->a); setHL(c,HL(c)+1); t=8; break;   /* LD (HL+),A */
        case 0x32: wb(c,HL(c),c->a); setHL(c,HL(c)-1); t=8; break;   /* LD (HL-),A */
        case 0x2A: c->a=rb(c,HL(c)); setHL(c,HL(c)+1); t=8; break;   /* LD A,(HL+) */
        case 0x3A: c->a=rb(c,HL(c)); setHL(c,HL(c)-1); t=8; break;   /* LD A,(HL-) */
        /* rotates on A (Z cleared on GB) */
        case 0x07: { uint8_t co=c->a>>7; c->a=(c->a<<1)|co; setf(c,0,0,0,co); } break;  /* RLCA */
        case 0x0F: { uint8_t co=c->a&1; c->a=(c->a>>1)|(co<<7); setf(c,0,0,0,co); } break; /* RRCA */
        case 0x17: { uint8_t cy=(c->f&LR_FLAG_C)?1:0, co=c->a>>7; c->a=(c->a<<1)|cy; setf(c,0,0,0,co); } break; /* RLA */
        case 0x1F: { uint8_t cy=(c->f&LR_FLAG_C)?1:0, co=c->a&1; c->a=(c->a>>1)|(cy<<7); setf(c,0,0,0,co); } break; /* RRA */
        case 0x27: daa(c); break;
        case 0x2F: c->a = ~c->a; c->f |= (LR_FLAG_N|LR_FLAG_H); break;  /* CPL */
        case 0x37: c->f = (c->f&LR_FLAG_Z) | LR_FLAG_C; break;          /* SCF */
        case 0x3F: c->f = (c->f&LR_FLAG_Z) | ((c->f&LR_FLAG_C)?0:LR_FLAG_C); break; /* CCF */
        /* relative jumps */
        case 0x18: { int8_t r=(int8_t)fetch(c); c->pc+=r; t=12; } break;
        case 0x20: { int8_t r=(int8_t)fetch(c); if(!(c->f&LR_FLAG_Z)){c->pc+=r;t=12;}else t=8; } break;
        case 0x28: { int8_t r=(int8_t)fetch(c); if( (c->f&LR_FLAG_Z)){c->pc+=r;t=12;}else t=8; } break;
        case 0x30: { int8_t r=(int8_t)fetch(c); if(!(c->f&LR_FLAG_C)){c->pc+=r;t=12;}else t=8; } break;
        case 0x38: { int8_t r=(int8_t)fetch(c); if( (c->f&LR_FLAG_C)){c->pc+=r;t=12;}else t=8; } break;
        /* ALU A,d8 */
        case 0xC6: alu(c,0,fetch(c)); t=8; break;  case 0xCE: alu(c,1,fetch(c)); t=8; break;
        case 0xD6: alu(c,2,fetch(c)); t=8; break;  case 0xDE: alu(c,3,fetch(c)); t=8; break;
        case 0xE6: alu(c,4,fetch(c)); t=8; break;  case 0xEE: alu(c,5,fetch(c)); t=8; break;
        case 0xF6: alu(c,6,fetch(c)); t=8; break;  case 0xFE: alu(c,7,fetch(c)); t=8; break;
        /* stack */
        case 0xC1: { uint16_t v=pop(c); c->b=v>>8; c->c=v&0xFF; t=12; } break;
        case 0xD1: { uint16_t v=pop(c); c->d=v>>8; c->e=v&0xFF; t=12; } break;
        case 0xE1: setHL(c,pop(c)); t=12; break;
        case 0xF1: { uint16_t v=pop(c); c->a=v>>8; c->f=v&0xF0; t=12; } break;
        case 0xC5: push(c,BC(c)); t=16; break;   case 0xD5: push(c,DE(c)); t=16; break;
        case 0xE5: push(c,HL(c)); t=16; break;   case 0xF5: push(c,(c->a<<8)|(c->f&0xF0)); t=16; break;
        /* jumps / calls / returns */
        case 0xC3: c->pc=fetch16(c); t=16; break;
        case 0xC2: { uint16_t a=fetch16(c); if(!(c->f&LR_FLAG_Z)){c->pc=a;t=16;}else t=12; } break;
        case 0xCA: { uint16_t a=fetch16(c); if( (c->f&LR_FLAG_Z)){c->pc=a;t=16;}else t=12; } break;
        case 0xD2: { uint16_t a=fetch16(c); if(!(c->f&LR_FLAG_C)){c->pc=a;t=16;}else t=12; } break;
        case 0xDA: { uint16_t a=fetch16(c); if( (c->f&LR_FLAG_C)){c->pc=a;t=16;}else t=12; } break;
        case 0xE9: c->pc=HL(c); break;
        case 0xCD: { uint16_t a=fetch16(c); push(c,c->pc); c->pc=a; t=24; } break;
        case 0xC4: { uint16_t a=fetch16(c); if(!(c->f&LR_FLAG_Z)){push(c,c->pc);c->pc=a;t=24;}else t=12; } break;
        case 0xCC: { uint16_t a=fetch16(c); if( (c->f&LR_FLAG_Z)){push(c,c->pc);c->pc=a;t=24;}else t=12; } break;
        case 0xD4: { uint16_t a=fetch16(c); if(!(c->f&LR_FLAG_C)){push(c,c->pc);c->pc=a;t=24;}else t=12; } break;
        case 0xDC: { uint16_t a=fetch16(c); if( (c->f&LR_FLAG_C)){push(c,c->pc);c->pc=a;t=24;}else t=12; } break;
        case 0xC9: c->pc=pop(c); t=16; break;
        case 0xD9: c->pc=pop(c); c->ime=1; t=16; break;                 /* RETI */
        case 0xC0: if(!(c->f&LR_FLAG_Z)){c->pc=pop(c);t=20;}else t=8; break;
        case 0xC8: if( (c->f&LR_FLAG_Z)){c->pc=pop(c);t=20;}else t=8; break;
        case 0xD0: if(!(c->f&LR_FLAG_C)){c->pc=pop(c);t=20;}else t=8; break;
        case 0xD8: if( (c->f&LR_FLAG_C)){c->pc=pop(c);t=20;}else t=8; break;
        /* RST */
        case 0xC7: push(c,c->pc); c->pc=0x00; t=16; break;
        case 0xCF: push(c,c->pc); c->pc=0x08; t=16; break;
        case 0xD7: push(c,c->pc); c->pc=0x10; t=16; break;
        case 0xDF: push(c,c->pc); c->pc=0x18; t=16; break;
        case 0xE7: push(c,c->pc); c->pc=0x20; t=16; break;
        case 0xEF: push(c,c->pc); c->pc=0x28; t=16; break;
        case 0xF7: push(c,c->pc); c->pc=0x30; t=16; break;
        case 0xFF: push(c,c->pc); c->pc=0x38; t=16; break;
        /* high I/O + absolute A loads/stores */
        case 0xE0: wb(c, 0xFF00 + fetch(c), c->a); t=12; break;         /* LDH (n),A */
        case 0xF0: c->a = rb(c, 0xFF00 + fetch(c)); t=12; break;        /* LDH A,(n) */
        case 0xE2: wb(c, 0xFF00 + c->c, c->a); t=8; break;             /* LD (C),A  */
        case 0xF2: c->a = rb(c, 0xFF00 + c->c); t=8; break;           /* LD A,(C)  */
        case 0xEA: { uint16_t a=fetch16(c); wb(c,a,c->a); t=16; } break;
        case 0xFA: { uint16_t a=fetch16(c); c->a=rb(c,a); t=16; } break;
        /* SP arithmetic */
        case 0xE8: { int8_t r=(int8_t)fetch(c); uint16_t sp=c->sp;
                     setf(c,0,0,((sp&0xF)+(r&0xF))>0xF,((sp&0xFF)+(uint8_t)r)>0xFF); c->sp=sp+r; t=16; } break;
        case 0xF8: { int8_t r=(int8_t)fetch(c); uint16_t sp=c->sp;
                     setf(c,0,0,((sp&0xF)+(r&0xF))>0xF,((sp&0xFF)+(uint8_t)r)>0xFF); setHL(c,sp+r); t=12; } break;
        case 0xF9: c->sp=HL(c); t=8; break;
        /* interrupts */
        case 0xF3: c->ime=0; break;                                    /* DI */
        case 0xFB: c->ime=1; break;                                    /* EI */
        case 0xCB: do_cb(c); t=8; break;
        /* undefined opcodes */
        case 0xD3: case 0xDB: case 0xDD: case 0xE3: case 0xE4:
        case 0xEB: case 0xEC: case 0xED: case 0xF4: case 0xFC: case 0xFD:
            c->illegal++; break;
        default: c->illegal++; break;
    }
    c->cycles += t;
    return t;
}

uint64_t cpu_lr_run(cpu_lr35902_t *c, uint64_t max_cycles){
    uint64_t start = c->cycles;
    while (c->cycles - start < max_cycles && !c->stopped) cpu_lr_step(c);
    return c->cycles - start;
}
