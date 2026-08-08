/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* gba.c — Game Boy Advance machine around the ARM7TDMI. See gba.h. */
#include "gba.h"

/* The GBA cartridge header: the Nintendo logo begins at $04 with these bytes,
 * offset $B2 is the fixed constant $96, and the entry word at $00 is an ARM
 * branch. Together these reject non-GBA data with negligible collision. */
static const uint8_t GBA_LOGO4[4] = { 0x24, 0xFF, 0xAE, 0x51 };

int gba_is_gba(const uint8_t *img, uint32_t len){
    if (!img || len < 0xC0) return 0;
    if (img[0xB2] != 0x96) return 0;
    for (int i = 0; i < 4; i++) if (img[0x04 + i] != GBA_LOGO4[i]) return 0;
    uint32_t entry = img[0] | (img[1]<<8) | (img[2]<<16) | ((uint32_t)img[3]<<24);
    if ((entry & 0x0F000000) != 0x0A000000) return 0;   /* entry must be a branch */
    return 1;
}

/* ---- I/O register byte access ---------------------------------------------- */
static uint8_t io_rd8(gba_t *g, uint32_t off){
    switch (off){
        case 0x006: g->vcount_reads++; return g->vcount & 0xFF;             /* VCOUNT lo */
        case 0x007: g->vcount_reads++; return (g->vcount >> 8) & 0xFF;      /* VCOUNT hi */
        case 0x004: return (g->vcount >= 160 ? 0x01 : 0)                    /* DISPSTAT  */
                         | (g->vcount == (g->io[0x28] | (g->io[0x29]<<8)) ? 0x04 : 0);
        case 0x130: return 0xFF;                                            /* KEYINPUT lo (no keys) */
        case 0x131: return 0x03;                                            /* KEYINPUT hi */
        default: return g->io[off & 0x3FF];
    }
}
static void io_wr8(gba_t *g, uint32_t off, uint8_t v){
    off &= 0x3FF;
    g->io_writes++;
    if (off == 0x000 || off == 0x001) g->dispcnt_set = 1;                   /* DISPCNT */
    g->io[off] = v;
}

/* A minimal BIOS IRQ dispatcher, exposed in the BIOS region. On IRQ the CPU
 * jumps to $18, which branches to $128; that saves regs and calls the game's
 * handler through the pointer at $03FFFFFC (mirror of IWRAM $03007FFC), then
 * returns. This is exactly what real games rely on for their VBlank logic. */
static const uint32_t BIOS_IRQ[6] = {
    0xE92D500F,  /* stmfd sp!, {r0-r3,r12,lr}        */
    0xE3A00301,  /* mov   r0, #0x4000000            */
    0xE28FE000,  /* add   lr, pc, #0                */
    0xE510F004,  /* ldr   pc, [r0, #-4]  ; user handler at [0x03FFFFFC] */
    0xE8BD500F,  /* ldmfd sp!, {r0-r3,r12,lr}       */
    0xE25EF004,  /* subs  pc, lr, #4                */
};
static uint8_t bios_rd8(uint32_t off){
    uint32_t w = 0;
    if (off >= 0x18 && off < 0x1C) w = 0xEA000042u;              /* b 0x128 */
    else if (off >= 0x128 && off < 0x140) w = BIOS_IRQ[(off - 0x128) >> 2];
    return (uint8_t)(w >> ((off & 3) * 8));
}

/* ---- byte bus -------------------------------------------------------------- */
static uint8_t gba_rd8(cpu_arm7_t *c, uint32_t a){
    gba_t *g = (gba_t*)c->ctx;
    switch ((a >> 24) & 0xFF){
        case 0x00: case 0x01: return bios_rd8(a & 0x3FFF);                  /* BIOS */
        case 0x02: return g->ewram[a & 0x3FFFF];
        case 0x03: return g->iwram[a & 0x7FFF];
        case 0x04: return io_rd8(g, a & 0x3FF);
        case 0x05: return g->pal[a & 0x3FF];
        case 0x06: { uint32_t v = a & 0x1FFFF; if (v >= 0x18000) v -= 0x8000; return g->vram[v]; }
        case 0x07: return g->oam[a & 0x3FF];
        case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D:
            { uint32_t o = a & 0x01FFFFFF; return g->rom_size ? g->rom[o % g->rom_size] : 0; }
        case 0x0E: case 0x0F: return g->sram[a & 0xFFFF];
        default: return 0;
    }
}
static void gba_wr8(cpu_arm7_t *c, uint32_t a, uint8_t v){
    gba_t *g = (gba_t*)c->ctx;
    switch ((a >> 24) & 0xFF){
        case 0x02: g->ewram[a & 0x3FFFF] = v; return;
        case 0x03: g->iwram[a & 0x7FFF] = v; return;
        case 0x04: io_wr8(g, a & 0x3FF, v); return;
        case 0x05: g->pal[a & 0x3FF] = v; return;
        case 0x06: { uint32_t o = a & 0x1FFFF; if (o >= 0x18000) o -= 0x8000; g->vram[o] = v; return; }
        case 0x07: g->oam[a & 0x3FF] = v; return;
        case 0x0E: case 0x0F: g->sram[a & 0xFFFF] = v; return;
        default: return;                                                    /* ROM ignored */
    }
}
static uint16_t gba_rd16(cpu_arm7_t *c, uint32_t a){ return (uint16_t)(gba_rd8(c,a) | (gba_rd8(c,a+1)<<8)); }
static uint32_t gba_rd32(cpu_arm7_t *c, uint32_t a){
    return (uint32_t)gba_rd8(c,a) | ((uint32_t)gba_rd8(c,a+1)<<8)
         | ((uint32_t)gba_rd8(c,a+2)<<16) | ((uint32_t)gba_rd8(c,a+3)<<24);
}
static void gba_wr16(cpu_arm7_t *c, uint32_t a, uint16_t v){ gba_wr8(c,a,v&0xFF); gba_wr8(c,a+1,v>>8); }
static void gba_wr32(cpu_arm7_t *c, uint32_t a, uint32_t v){
    gba_wr8(c,a,v&0xFF); gba_wr8(c,a+1,v>>8); gba_wr8(c,a+2,v>>16); gba_wr8(c,a+3,v>>24);
}

/* Minimal BIOS SWI service. Games depend on the BIOS for division, block copies,
 * decompression, and VBlank waits; without these many stall in init. We provide
 * the common ones so games proceed (not a full, cycle-accurate BIOS). */
static void gba_swi(cpu_arm7_t *c, uint8_t comment){
    gba_t *g = (gba_t*)c->ctx;
    switch (comment){
        case 0x04: case 0x05:   /* IntrWait / VBlankIntrWait: pretend a VBlank arrived */
            g->vcount = 160; g->io[0x02] &= ~0x01;   /* clear IF VBlank */
            break;
        case 0x06: {            /* Div: r0/r1 -> r0=quot r1=rem r3=abs(quot) */
            int32_t num=(int32_t)c->r[0], den=(int32_t)c->r[1];
            if (den != 0){ int32_t q=num/den, r=num%den; c->r[0]=(uint32_t)q; c->r[1]=(uint32_t)r;
                           c->r[3]=(uint32_t)(q<0?-q:q); }
            break;
        }
        case 0x07: {            /* DivArm: r1/r0 */
            int32_t num=(int32_t)c->r[1], den=(int32_t)c->r[0];
            if (den != 0){ int32_t q=num/den, r=num%den; c->r[0]=(uint32_t)q; c->r[1]=(uint32_t)r;
                           c->r[3]=(uint32_t)(q<0?-q:q); }
            break;
        }
        case 0x0B: {            /* CpuSet: r0=src r1=dst r2=len|flags */
            uint32_t src=c->r[0], dst=c->r[1], ctl=c->r[2];
            uint32_t count = ctl & 0x1FFFFF; int word = (ctl>>26)&1; int fill = (ctl>>24)&1;
            uint32_t guard=0;
            if (word){ uint32_t v=c->read32(c,src&~3u);
                for (uint32_t i=0;i<count && guard<0x40000;i++,guard++){ if(!fill) v=c->read32(c,(src+i*4)&~3u);
                    c->write32(c,(dst+i*4)&~3u,v);} }
            else { uint16_t v=c->read16(c,src&~1u);
                for (uint32_t i=0;i<count && guard<0x40000;i++,guard++){ if(!fill) v=c->read16(c,(src+i*2)&~1u);
                    c->write16(c,(dst+i*2)&~1u,v);} }
            break;
        }
        case 0x0C: {            /* CpuFastSet: 32-bit, count rounded to 8 */
            uint32_t src=c->r[0], dst=c->r[1], ctl=c->r[2];
            uint32_t count=(ctl & 0x1FFFFF); int fill=(ctl>>24)&1; uint32_t guard=0;
            uint32_t v=c->read32(c,src&~3u);
            for (uint32_t i=0;i<count && guard<0x40000;i++,guard++){ if(!fill) v=c->read32(c,(src+i*4)&~3u);
                c->write32(c,(dst+i*4)&~3u,v); }
            break;
        }
        default:                /* Halt/decompress/etc.: continue without effect */
            break;
    }
}

int gba_load(gba_t *g, const uint8_t *img, uint32_t len){
    if (!img || len < 0xC0) return 0;
    for (unsigned i = 0; i < sizeof *g; i++) ((uint8_t*)g)[i] = 0;
    uint32_t n = len; if (n > GBA_ROM_CAP) n = GBA_ROM_CAP;
    for (uint32_t i = 0; i < n; i++) g->rom[i] = img[i];
    g->rom_size = n;
    g->cpu.read8=gba_rd8; g->cpu.read16=gba_rd16; g->cpu.read32=gba_rd32;
    g->cpu.write8=gba_wr8; g->cpu.write16=gba_wr16; g->cpu.write32=gba_wr32;
    g->cpu.swi = gba_swi;
    g->cpu.ctx = g;
    cpu_arm7_reset(&g->cpu, 0x08000000);
    return 1;
}

void gba_run(gba_t *g, uint32_t budget){
    /* ~1000 instructions per scanline; 228 lines, VBlank at VCOUNT 160. Driven
     * off the instruction count (behavioural). No BIOS IRQ dispatch: games poll
     * VCOUNT/DISPSTAT, which we present. */
    const uint32_t per_line = 1000;
    for (uint32_t i = 0; i < budget; i++){
        cpu_arm7_step(&g->cpu);
        g->insn++;
        if ((i % per_line) == (per_line - 1)){
            g->vcount++;
            if (g->vcount == 160){                                 /* enter VBlank */
                g->io[0x202] |= 0x01;                              /* IF: VBlank */
                uint16_t ime = g->io[0x208] | (g->io[0x209]<<8);
                uint16_t ie  = g->io[0x200] | (g->io[0x201]<<8);
                if ((ime & 1) && (ie & 1) && cpu_arm7_irq(&g->cpu)) /* dispatch to game handler */
                    g->vblank_irqs++;
            }
            if (g->vcount > 227) g->vcount = 0;
        }
    }
}

int gba_is_running(const gba_t *g){
    /* Alive iff it configured the display, drove the I/O file, and is polling the
     * frame counter (VCOUNT/DISPSTAT), with a low undefined-instruction rate.
     * Random ARM/THUMB data does not set DISPCNT and poll VCOUNT coherently. */
    int disp   = g->dispcnt_set;
    int io_ok  = (g->io_writes >= 8);
    int frame  = (g->vcount_reads >= 8) || (g->vblank_irqs >= 1);
    int clean  = (g->insn > 0) && (g->cpu.illegal * 20 < g->insn);   /* <5% illegal */
    return disp && io_ok && frame && clean;
}

int gba_selfcheck(void){
    static uint8_t img[0x400];
    for (unsigned i = 0; i < sizeof img; i++) img[i] = 0;
    /* header: entry branch to $080000C0, logo prefix, $96 constant */
    uint32_t entry = 0xEA000000u | ((0xC0 - 8) >> 2);   /* B #0xC0 */
    img[0]=entry&0xFF; img[1]=(entry>>8)&0xFF; img[2]=(entry>>16)&0xFF; img[3]=(entry>>24)&0xFF;
    for (int i=0;i<4;i++) img[0x04+i] = GBA_LOGO4[i];
    img[0xB2] = 0x96;
    /* ARM code at $C0: set DISPCNT twice, then poll VCOUNT forever */
    uint32_t p = 0xC0;
    #define W(v) do{ uint32_t _v=(v); img[p]=_v&0xFF; img[p+1]=(_v>>8)&0xFF; img[p+2]=(_v>>16)&0xFF; img[p+3]=(_v>>24)&0xFF; p+=4; }while(0)
    W(0xE3A00404);   /* MOV R0,#0x04000000       */
    W(0xE3A01C01);   /* MOV R1,#0x100            */
    W(0xE5801000);   /* STR R1,[R0]  DISPCNT     */
    W(0xE5801000);   /* STR R1,[R0]  (more I/O)  */
    W(0xE1D020B6);   /* loop: LDRH R2,[R0,#6]  VCOUNT */
    W(0xEAFFFFFD);   /* B loop                   */

    static gba_t g;
    if (!gba_is_gba(img, sizeof img)) return 0;
    if (!gba_load(&g, img, sizeof img)) return 0;
    gba_run(&g, 20000);
    return gba_is_running(&g);
}
