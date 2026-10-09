/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* genesis.c — Sega Genesis / Mega Drive machine around the 68000. See genesis.h.
 * Big-endian bus. */
#include "genesis.h"

int genesis_is_genesis(const uint8_t *img, uint32_t len){
    if (!img || len < 0x200) return 0;
    return img[0x100]=='S' && img[0x101]=='E' && img[0x102]=='G' && img[0x103]=='A';
}

/* ---- 16-bit big-endian bus (the VDP/IO are word devices) ------------------- */
static uint16_t gen_rd16(cpu_m68k_t *c, uint32_t a){
    genesis_t *g = (genesis_t*)c->ctx; a &= 0xFFFFFF;
    if (a < GEN_ROM_CAP){ uint32_t o=a; return g->rom_size ? ((g->rom[o%g->rom_size]<<8)|g->rom[(o+1)%g->rom_size]) : 0; }
    if (a >= 0xFF0000){ uint32_t o=a&0xFFFF; return (g->ram[o]<<8)|g->ram[(o+1)&0xFFFF]; }
    if (a >= 0xA00000 && a < 0xA02000){ uint32_t o=a&0x1FFF; return (g->zram[o]<<8)|g->zram[(o+1)&0x1FFF]; }
    if (a == 0xA11100) return 0;                       /* Z80 bus available */
    if (a >= 0xA10000 && a <= 0xA1001F) return 0x00FF; /* I/O: version + open controllers */
    if (a >= 0xC00000 && a <= 0xC00006){ g->vdp_status_reads++;   /* VDP status */
        return (g->vblank?0x0008:0) | 0x0200 | 0x3400; }          /* VBlank + FIFO empty */
    if (a >= 0xC00008 && a <= 0xC0000E) return (uint16_t)((g->v_counter&0xFF)<<8) | 0;  /* HV counter */
    return 0;
}
static void gen_wr16(cpu_m68k_t *c, uint32_t a, uint16_t v){
    genesis_t *g = (genesis_t*)c->ctx; a &= 0xFFFFFF;
    if (a >= 0xFF0000){ uint32_t o=a&0xFFFF; g->ram[o]=v>>8; g->ram[(o+1)&0xFFFF]=v&0xFF; return; }
    if (a >= 0xA00000 && a < 0xA02000){ uint32_t o=a&0x1FFF; g->zram[o]=v>>8; g->zram[(o+1)&0x1FFF]=v&0xFF; return; }
    if (a >= 0xC00000 && a <= 0xC00002){ g->vdp_data_writes++; return; }          /* VDP data port */
    if (a >= 0xC00004 && a <= 0xC00006){                                          /* VDP control port */
        if (v & 0x8000){ int reg=(v>>8)&0x1F; g->vdp_reg[reg]=v&0xFF; g->vdp_reg_writes++;
            if (reg==1) g->display_on = (v>>6)&1; g->vdp_latch=0; }
        else g->vdp_latch ^= 1;
        return;
    }
    /* ROM, TMSS $A14000, Z80 control, IO: ignored for the verdict */
}
static uint8_t gen_rd8(cpu_m68k_t *c, uint32_t a){
    uint16_t w = gen_rd16(c, a & ~1u); return (a & 1) ? (w & 0xFF) : (w >> 8);
}
static uint32_t gen_rd32(cpu_m68k_t *c, uint32_t a){ return ((uint32_t)gen_rd16(c,a)<<16) | gen_rd16(c,a+2); }
static void gen_wr8(cpu_m68k_t *c, uint32_t a, uint8_t v){
    genesis_t *g=(genesis_t*)c->ctx; a&=0xFFFFFF;
    if (a >= 0xFF0000){ g->ram[a&0xFFFF]=v; return; }
    if (a >= 0xA00000 && a < 0xA02000){ g->zram[a&0x1FFF]=v; return; }
    /* byte writes to VDP are rare; fold to a word write */
    uint16_t w = (a&1) ? v : (v<<8); gen_wr16(c, a&~1u, w);
}
static void gen_wr32(cpu_m68k_t *c, uint32_t a, uint32_t v){ gen_wr16(c,a,v>>16); gen_wr16(c,a+2,v&0xFFFF); }

int genesis_load(genesis_t *g, const uint8_t *img, uint32_t len){
    if (!img || len < 0x200) return 0;
    for (unsigned i = 0; i < sizeof *g; i++) ((uint8_t*)g)[i] = 0;
    uint32_t n = len; if (n > GEN_ROM_CAP) n = GEN_ROM_CAP;
    for (uint32_t i = 0; i < n; i++) g->rom[i] = img[i];
    g->rom_size = n;
    g->cpu.read8=gen_rd8; g->cpu.read16=gen_rd16; g->cpu.read32=gen_rd32;
    g->cpu.write8=gen_wr8; g->cpu.write16=gen_wr16; g->cpu.write32=gen_wr32;
    g->cpu.ctx = g;
    cpu_m68k_reset(&g->cpu);
    return 1;
}

void genesis_run(genesis_t *g, uint32_t budget){
    /* ~488 CPU cycles per line; 262 lines, VBlank at line 224. Driven off the
     * instruction count. VBlank IRQ (level 6) fires when the game enables it
     * (VDP reg1 bit5) and has lowered the interrupt mask; it vectors through the
     * game's own ROM handler. */
    const uint32_t per_line = 400;
    for (uint32_t i = 0; i < budget; i++){
        cpu_m68k_step(&g->cpu);
        g->insn++;
        if ((i % per_line) == (per_line - 1)){
            g->v_counter++;
            if (g->v_counter == 224){
                g->vblank = 1;
                if ((g->vdp_reg[1] & 0x20) && cpu_m68k_irq(&g->cpu, 6)) g->vblank_irqs++;
            }
            if (g->v_counter > 261){ g->v_counter = 0; g->vblank = 0; }
        }
    }
}

int genesis_is_running(const genesis_t *g){
    /* Alive iff it programmed the VDP (a stream of register writes), uploaded
     * data / polled status or took a VBlank IRQ, and kept a low illegal rate.
     * Random data does not write a coherent VDP register stream. */
    int vdp    = (g->vdp_reg_writes >= 5);
    int active = (g->vdp_data_writes >= 4) || (g->vdp_status_reads >= 8) || (g->vblank_irqs >= 1);
    int clean  = (g->insn > 0) && (g->cpu.illegal * 20 < g->insn);   /* <5% illegal */
    return vdp && active && clean;
}

int genesis_selfcheck(void){
    static uint8_t img[0x8000];
    for (unsigned i = 0; i < sizeof img; i++) img[i] = 0;
    #define W16(a,v) do{ img[(a)]=(uint8_t)((v)>>8); img[(a)+1]=(uint8_t)(v); }while(0)
    #define W32(a,v) do{ img[(a)]=(uint8_t)((v)>>24); img[(a)+1]=(uint8_t)((v)>>16); \
                         img[(a)+2]=(uint8_t)((v)>>8); img[(a)+3]=(uint8_t)(v); }while(0)
    W32(0x00, 0x00FFFFF0);   /* initial SP */
    W32(0x04, 0x00000200);   /* initial PC */
    W32(0x78, 0x00000300);   /* VBlank autovector (level 6) -> handler */
    img[0x100]='S'; img[0x101]='E'; img[0x102]='G'; img[0x103]='A';

    uint32_t p = 0x200;
    #define EMIT(v) do{ W16(p,(v)); p+=2; }while(0)
    W16(p,0x41F9); p+=2; W32(p,0x00C00004); p+=4;   /* LEA $C00004,A0 (control) */
    W16(p,0x43F9); p+=2; W32(p,0x00C00000); p+=4;   /* LEA $C00000,A1 (data)    */
    EMIT(0x30BC); EMIT(0x8004);                     /* MOVE.W #$8004,(A0) reg0  */
    EMIT(0x30BC); EMIT(0x8164);                     /* reg1 = display+VBlankIRQ */
    EMIT(0x30BC); EMIT(0x8230);                     /* reg2 */
    EMIT(0x30BC); EMIT(0x8328);                     /* reg3 */
    EMIT(0x30BC); EMIT(0x8407);                     /* reg4 */
    EMIT(0x30BC); EMIT(0x857C);                     /* reg5 */
    EMIT(0x30BC); EMIT(0x8700);                     /* reg7 */
    EMIT(0x30BC); EMIT(0x4000);                     /* MOVE.W #$4000,(A0) addr  */
    EMIT(0x30BC); EMIT(0x0000);                     /* MOVE.W #$0000,(A0)       */
    EMIT(0x32BC); EMIT(0x0FF0);                     /* MOVE.W #$0FF0,(A1) data  */
    EMIT(0x32BC); EMIT(0x0FF0);
    EMIT(0x32BC); EMIT(0x0FF0);
    EMIT(0x32BC); EMIT(0x0FF0);
    uint32_t loop = p;
    EMIT(0x3010);                                   /* loop: MOVE.W (A0),D0 (status) */
    { int8_t d=(int8_t)((int32_t)loop - (int32_t)(p+2)); W16(p, 0x6000 | (d & 0xFF)); p+=2; } /* BRA loop */

    p = 0x300;                                       /* VBlank handler */
    EMIT(0x33FC); EMIT(0x0001); W32(p,0x00FF0000); p+=4;  /* MOVE.W #1,$FF0000 */
    EMIT(0x4E73);                                    /* RTE */

    static genesis_t g;
    if (!genesis_is_genesis(img, sizeof img)) return 0;
    if (!genesis_load(&g, img, sizeof img)) return 0;
    genesis_run(&g, 20000);
    return genesis_is_running(&g);
}
